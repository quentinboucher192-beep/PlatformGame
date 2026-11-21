// Gifts (« Cadeaux », migration 0205, docs/COLLECTION.md « Cadeaux »): an administrator gives rewards of the
// real catalogue to one or more players (ADMINISTRATION › Cadeaux), each in one of eleven wraps, with an
// optional message. The server never trusts the game:
//   - the sender is the session's account and must hold the admin role (requireAdminRole: the routes do not
//     exist for anyone else), every send and every cancellation goes to the Journal;
//   - a reward is an item of src/cosmeticCatalog.json in its own category (never a category's default) or a
//     profile badge the server grants itself (raidRewards.ts RAID_ACHIEVEMENTS); a recipient who already owns
//     it (or has it waiting in a gift) is skipped and reported;
//   - the content of a gift stays hidden from its recipient until it is opened, and OPENING is what grants it,
//     through the existing ownership: an item goes to player_won_rewards (challenge_id « gift:<id> », read by
//     GET /players/me/rewards and by what may be worn online), a badge to a claimed raid_reward_grants row
//     (event « gift:<id> »: the achievement the player may then declare). One conditional UPDATE marks it
//     opened; the grants are INSERT OR IGNORE: opening twice grants nothing more;
//   - « Colis » (migration 0210): with `bundle`, a send gives each recipient ONE parcel holding the rewards they do
//     not own yet. A parcel is one wrapped entry: neither what it holds nor how many are known before it is opened
//     (its gifts are only written then, so no id betrays them); opening it writes one opened gift per reward, in
//     order, and grants each the same way, once.
//   - 2.85 (migration 0215): no limit on the rewards (only those of the real catalogue exist) nor on the players
//     (`players: "all"`: every active player). A send is written in steps of GIFT_STEP_PLAYERS players, each one
//     D1 batch holding its progress; it stays invisible (state 'writing') until its last batch makes it 'sent'
//     with its notifications. A request that stops after GIFT_STEPS_PER_REQUEST steps answers 202 with its
//     `request_key` (one that fails leaves the send 'writing'): sent again with that key, the same request goes on
//     where it stopped; once sent, it answers the send again (never twice). A send left 'writing' for an hour is
//     removed with what it wrote.
import achievementCatalog from "./achievementCatalog.json";
import cosmeticCatalog from "./cosmeticCatalog.json";
import { audit, requireAuth } from "./auth";
import { sha256Hex } from "./crypto";
import { requireAdminRole } from "./adminRoles";
import { rewardProgressFrom, type VerifiedChallenge, type WonRewards } from "./challengeRewards";
import { unlocked, type CosmeticItem, type CosmeticProgress } from "./cosmetics";
import { PRESENCE_TIMEOUT_MS } from "./friends";
import { ApiError, Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import { RAID_ACHIEVEMENTS, RAID_REWARDS, rewardsOf } from "./raidRewards";
import { RULES, rateLimit } from "./ratelimit";
import { Validator, likePrefix, normalizeUsername, paging, queryEnum, queryString } from "./validate";

/** The eleven wraps (tools/assetgen/environment/gift_wraps.py draws them for the game). */
export const GIFT_WRAPS = ["or", "amethyste", "azur", "rubis", "emeraude", "nebuleuse", "givre", "braise", "cosmos", "rouage", "asterion"] as const;
export type GiftWrap = (typeof GIFT_WRAPS)[number];
/** The five categories of the Collection, and the profile badges. */
export const GIFT_KINDS = ["suit", "trail", "frame", "title", "victory", "badge"] as const;
export type GiftKind = (typeof GIFT_KINDS)[number];

export const MAX_GIFT_MESSAGE = 140;
/** Players written per step (one D1 batch: their gifts or parcels, and the send's progress). */
export const GIFT_STEP_PLAYERS = 500;
/** Steps one request writes at most (D1's queries per request, the Worker's time); then 202 and the game goes on. */
export const GIFT_STEPS_PER_REQUEST = 40;
/** Players chosen by ids in one request (its body stays under 1 MiB); more: `players: "all"`. */
const MAX_BODY = 1024 * 1024;
/** Players whose tags « possède déjà » one page asks for. */
const MAX_TAGGED = 200;
/** The skipped pairs an answer lists (`skipped_count` counts them all). */
const MAX_SKIPPED_LISTED = 200;
/** A send left 'writing' this long (no step) is removed. */
const STALE_WRITING_MS = 3600 * 1000;

const ITEMS = new Map((cosmeticCatalog as unknown as { items: CosmeticItem[] }).items.map((i) => [i.id, i]));
const STARS = (achievementCatalog as { stars: Record<string, number> }).stars;
const ID_RE = /^[a-z0-9_]{1,64}$/;
const PLAYER_ID_RE = /^[A-Za-z0-9-]{8,64}$/;

export interface GiftReward {
    kind: GiftKind;
    id: string;
}

const keyOf = (r: GiftReward) => `${r.kind}:${r.id}`;

/** A reward of the real catalogue that may be given (null when unknown, a default, or not given by the server). */
export function giftReward(kind: unknown, id: unknown): (GiftReward & { name: string }) | null {
    if (typeof kind !== "string" || typeof id !== "string" || !ID_RE.test(id)) return null;
    if (kind === "badge") {
        const reward = RAID_ACHIEVEMENTS[id];
        if (!reward || STARS[id] === undefined) return null;
        return { kind: "badge", id, name: RAID_REWARDS[reward]?.name ?? id };
    }
    if (!(GIFT_KINDS as readonly string[]).includes(kind)) return null;
    const item = ITEMS.get(id);
    if (!item || item.category !== kind || item.default) return null;
    return { kind: kind as GiftKind, id, name: item.name };
}

/** The message as kept: control and direction characters removed, trimmed (null when too long). */
export function cleanGiftMessage(raw: string): string | null {
    // eslint-disable-next-line no-control-regex
    const s = raw.replace(/[\u0000-\u001f\u007f-\u009f​-‏‪-‮⁦-⁩﻿]/g, "").trim();
    return [...s].length <= MAX_GIFT_MESSAGE ? s : null;
}

// ------------------------------------------------------------------ who owns what

type Owned = Map<string, Map<string, "owned" | "pending">>;

const IN_JSON = (col: string, at: string) => `${col} IN (SELECT value FROM json_each(${at}))`;

/**
 * For each player, the rewards among `rewards` they already own (as the server counts it: an item unlocked
 * by the achievements they declared, the challenge results it verified, or won; a badge claimed) or have
 * waiting in an unopened gift. A few queries whatever the number of players.
 */
export async function giftOwnership(db: D1Database, players: string[], rewards: GiftReward[]): Promise<Owned> {
    const out: Owned = new Map();
    if (players.length === 0 || rewards.length === 0) return out;
    const ids = JSON.stringify(players);
    const mark = (p: string, key: string, v: "owned" | "pending") => {
        let m = out.get(p);
        if (!m) out.set(p, (m = new Map()));
        if (m.get(key) !== "owned") m.set(key, v);
    };
    const items = rewards.filter((r) => r.kind !== "badge").map((r) => ITEMS.get(r.id)!).filter(Boolean);
    const badges = rewards.filter((r) => r.kind === "badge");
    if (items.length) {
        const needChallenges = items.some((i) => i.challenge || i.challengeStreak);
        const [ach, won, runs] = await Promise.all([
            db.prepare(`SELECT player_id, ids, stars FROM player_achievements WHERE ${IN_JSON("player_id", "?1")}`).bind(ids).all<{ player_id: string; ids: string; stars: number }>(),
            db.prepare(`SELECT DISTINCT player_id, reward_kind AS kind, reward_ref AS ref FROM player_won_rewards WHERE ${IN_JSON("player_id", "?1")} AND reward_kind = 'item'`)
                .bind(ids)
                .all<{ player_id: string; kind: string; ref: string }>(),
            needChallenges
                ? db
                      .prepare(
                          `SELECT x.player_id, c.kind, c.date,
                                  MAX(CASE x.tier WHEN 'gold' THEN 3 WHEN 'silver' THEN 2 WHEN 'bronze' THEN 1 ELSE 0 END) AS tier,
                                  MAX(CASE WHEN x.late = 0 THEN 1 ELSE 0 END) AS in_time
                           FROM (SELECT r.player_id, r.challenge_id, r.tier, r.late FROM challenge_runs r WHERE ${IN_JSON("r.player_id", "?1")} AND r.status = 'VERIFIED'
                                 UNION ALL
                                 SELECT a.player_id, a.challenge_id, a.tier, a.late FROM challenge_attempts a
                                 WHERE ${IN_JSON("a.player_id", "?1")} AND a.status = 'VERIFIED' AND a.tier IS NOT NULL) x
                           JOIN challenges c ON c.id = x.challenge_id
                           GROUP BY x.player_id, c.id`,
                      )
                      .bind(ids)
                      .all<VerifiedChallenge & { player_id: string }>()
                : Promise.resolve({ results: [] as (VerifiedChallenge & { player_id: string })[] }),
        ]);
        const achOf = new Map(ach.results.map((r) => [r.player_id, r]));
        const wonOf = new Map<string, Set<string>>();
        for (const r of won.results) (wonOf.get(r.player_id) ?? wonOf.set(r.player_id, new Set()).get(r.player_id)!).add(r.ref);
        const runsOf = new Map<string, VerifiedChallenge[]>();
        for (const r of runs.results) (runsOf.get(r.player_id) ?? runsOf.set(r.player_id, []).get(r.player_id)!).push(r);
        for (const p of players) {
            const a = achOf.get(p);
            let declared: unknown = [];
            try {
                declared = JSON.parse(a?.ids ?? "[]");
            } catch {
                declared = [];
            }
            const wonRewards: WonRewards = { items: wonOf.get(p) ?? new Set(), titles: new Set(), suits: new Set(), looks: new Set() };
            const progress: CosmeticProgress = {
                stars: a?.stars ?? 0,
                ids: new Set(Array.isArray(declared) ? declared.filter((v): v is string => typeof v === "string") : []),
                challenges: needChallenges ? rewardProgressFrom(runsOf.get(p) ?? []) : undefined,
                won: wonRewards,
            };
            for (const i of items) if (unlocked(i, progress)) mark(p, `${i.category}:${i.id}`, "owned");
        }
    }
    if (badges.length) {
        const claimed = await db
            .prepare(`SELECT DISTINCT player_id, reward FROM raid_reward_grants WHERE ${IN_JSON("player_id", "?1")} AND claimed_at IS NOT NULL`)
            .bind(ids)
            .all<{ player_id: string; reward: string }>();
        for (const r of claimed.results)
            for (const b of badges) if (RAID_ACHIEVEMENTS[b.id] === r.reward) mark(r.player_id, keyOf(b), "owned");
    }
    const waiting = await db
        .prepare(
            `SELECT DISTINCT recipient_id, reward_kind, reward_id FROM gifts WHERE ${IN_JSON("recipient_id", "?1")} AND opened_at IS NULL AND cancelled_at IS NULL
             UNION
             SELECT p.recipient_id, json_extract(j.value, '$.kind'), json_extract(j.value, '$.id') FROM gift_parcels p, json_each(p.rewards) j
             WHERE ${IN_JSON("p.recipient_id", "?1")} AND p.opened_at IS NULL AND p.cancelled_at IS NULL`,
        )
        .bind(ids)
        .all<{ recipient_id: string; reward_kind: string; reward_id: string }>();
    const wanted = new Set(rewards.map(keyOf));
    for (const r of waiting.results) {
        const k = `${r.reward_kind}:${r.reward_id}`;
        if (wanted.has(k)) mark(r.recipient_id, k, "pending");
    }
    return out;
}

/** The rewards of a request (`rewards`: a list of {kind, id}), each once, all of the catalogue. */
function parseRewards(v: Validator, raw: unknown, field = "rewards"): (GiftReward & { name: string })[] {
    if (!Array.isArray(raw) || raw.length === 0) {
        v.fail(field, "liste de récompenses attendue (une au moins)");
        return [];
    }
    const out: (GiftReward & { name: string })[] = [];
    for (const r of raw) {
        const o = r !== null && typeof r === "object" && !Array.isArray(r) ? (r as Record<string, unknown>) : {};
        const g = giftReward(o.kind, o.id);
        if (!g) {
            v.fail(field, `récompense inconnue du catalogue : ${String(o.kind ?? "?")}:${String(o.id ?? "?")}`);
            continue;
        }
        if (!out.some((x) => x.kind === g.kind && x.id === g.id)) out.push(g);
    }
    return out;
}

/** "suit:suit_or,badge:communaute_coeur" (a query parameter), the unknown ones ignored. */
function rewardsOfQuery(raw: string | null): GiftReward[] {
    const out: GiftReward[] = [];
    for (const part of (raw ?? "").split(",")) {
        const at = part.indexOf(":");
        const g = at > 0 ? giftReward(part.slice(0, at), part.slice(at + 1)) : null;
        if (g && !out.some((x) => x.kind === g.kind && x.id === g.id)) out.push({ kind: g.kind, id: g.id });
    }
    return out;
}

// ------------------------------------------------------------------ administrators

interface CandidateRow {
    id: string;
    username: string;
    avatar: string;
    presence_state: string | null;
    presence_at: string | null;
    clan_tag: string | null;
}

/**
 * GET /admin/gifts/players?filter=online|all|clan|ids&q=&ids=<id,id>&rewards=<kind:id,…>&limit=&offset=
 * The players an administrator may choose: connected now, everyone (by username), the members of the
 * administrator's own clan, or the ids given (the ones already chosen). Each with what it already owns among
 * `rewards` (`owned`) or has waiting in a gift (`pending`): the « possède déjà » of the tab.
 */
export async function adminPlayers(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    const page = paging(ctx.url, 40, 100);
    const filter = queryEnum(ctx.url, "filter", ["online", "all", "clan", "ids"] as const, "online");
    const q = queryString(ctx.url, "q", /^[A-Za-z0-9_.-]{1,20}$/);
    const rewards = rewardsOfQuery(ctx.url.searchParams.get("rewards"));
    const db = ctx.env.DB;
    const clan = await db
        .prepare("SELECT c.id, c.tag, c.name FROM clan_members m JOIN clans c ON c.id = m.clan_id WHERE m.player_id = ?1")
        .bind(auth.player.id)
        .first<{ id: string; tag: string; name: string }>();
    const where = ["p.account_status = 'active'"];
    const binds: unknown[] = [];
    if (q) {
        binds.push("%" + likePrefix(normalizeUsername(q)));
        where.push(`p.username_normalized LIKE ?${binds.length} ESCAPE '\\'`);
    }
    let order = "p.username_normalized, p.id";
    if (filter === "online") {
        binds.push(new Date(Date.now() - PRESENCE_TIMEOUT_MS).toISOString());
        where.push(`p.presence_state IS NOT NULL AND p.presence_at >= ?${binds.length}`);
        order = "p.presence_at DESC, p.id";
    } else if (filter === "clan") {
        if (!clan) return json(ctx, { players: [], has_more: false, clan: null });
        binds.push(clan.id);
        where.push(`EXISTS (SELECT 1 FROM clan_members cm WHERE cm.player_id = p.id AND cm.clan_id = ?${binds.length})`);
    } else if (filter === "ids") {
        const ids = (ctx.url.searchParams.get("ids") ?? "").split(",").filter((s) => PLAYER_ID_RE.test(s)).slice(0, MAX_TAGGED);
        if (ids.length === 0) return json(ctx, { players: [], has_more: false, clan: clan ? { tag: clan.tag, name: clan.name } : null });
        binds.push(JSON.stringify(ids));
        where.push(IN_JSON("p.id", `?${binds.length}`));
    }
    binds.push(page.limit + 1, page.offset);
    const rows = await db
        .prepare(
            `SELECT p.id, p.username, p.avatar, p.presence_state, p.presence_at,
                    (SELECT c.tag FROM clan_members m JOIN clans c ON c.id = m.clan_id WHERE m.player_id = p.id) AS clan_tag
             FROM players p WHERE ${where.join(" AND ")} ORDER BY ${order} LIMIT ?${binds.length - 1} OFFSET ?${binds.length}`,
        )
        .bind(...binds)
        .all<CandidateRow>();
    const list = rows.results.slice(0, page.limit);
    const owned = await giftOwnership(
        db,
        list.map((p) => p.id),
        rewards,
    );
    const since = new Date(Date.now() - PRESENCE_TIMEOUT_MS).toISOString();
    // 2.85: how many players « Tous les joueurs » reaches (the confirmation says it).
    const active = await db.prepare("SELECT COUNT(*) AS n FROM players WHERE account_status = 'active'").first<{ n: number }>();
    return json(ctx, {
        active_players: active?.n ?? 0,
        players: list.map((p) => {
            const m = owned.get(p.id) ?? new Map();
            return {
                id: p.id,
                username: p.username,
                avatar: p.avatar,
                online: p.presence_state !== null && (p.presence_at ?? "") >= since,
                clan_tag: p.clan_tag,
                owned: [...m].filter(([, v]) => v === "owned").map(([k]) => k),
                pending: [...m].filter(([, v]) => v === "pending").map(([k]) => k),
            };
        }),
        has_more: rows.results.length > page.limit,
        clan: clan ? { tag: clan.tag, name: clan.name } : null,
    });
}

interface Skipped {
    player_id: string;
    username: string;
    reward: string;
    reason: "owned" | "pending";
}

interface SendState {
    id: number;
    state: string;
    request_key: string;
    request_hash: string | null;
    progress: string | null;
    bundle: number;
}

/**
 * POST /admin/gifts {rewards: [{kind, id}], players: [ids] | "all", wrap, message?, bundle?, request_key?}: one gift
 * per reward and player, except for those who already own the reward or have it waiting (`skipped`). With `bundle:
 * true`, ONE parcel per player instead, holding the rewards not skipped for them (a player for whom every reward is
 * skipped gets none; one reward left is still a parcel: its recipient cannot tell). No limit on the rewards (each of
 * the catalogue, once) nor on the players (`"all"`: every active player, in the order of their ids).
 *
 * Written in steps (GIFT_STEP_PLAYERS players: their gifts or parcels and the send's progress in one D1 batch),
 * invisible until the last batch makes it 'sent' with the recipients' notification (« gift », merged while unread;
 * a parcel counts as one). 201 {send, created, parcels, skipped, skipped_count, request_key} (`created`: the rewards
 * given; `parcels`: how many parcels, 0 without `bundle`; `skipped`: the first ones skipped by this request). 202
 * {request_key, state: "writing", done, total?} when it stopped before the end: the same request (same key) goes on.
 * The same key once sent: 200, the send again (`replayed`). 409 NOTHING_TO_SEND when every pair is skipped; 409
 * REQUEST_KEY_REUSED for the same key with another request. The Journal keeps it (admin_gift_sent), once.
 */
export async function adminSend(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const body = await readJsonObject(ctx, MAX_BODY);
    const v = new Validator(body, ["rewards", "players", "wrap", "message", "bundle", "request_key"]);
    const rewards = parseRewards(v, body.rewards);
    const wrap = v.oneOf("wrap", GIFT_WRAPS);
    const bundle = v.bool("bundle", { optional: true }) ?? false;
    const rawMessage = body.message === undefined || body.message === null ? "" : body.message;
    let message = "";
    if (typeof rawMessage !== "string") v.fail("message", "doit être une chaîne");
    else {
        const m = cleanGiftMessage(rawMessage);
        if (m === null) v.fail("message", `${MAX_GIFT_MESSAGE} caractères au plus`);
        else message = m;
    }
    const rawPlayers = body.players;
    const all = rawPlayers === "all";
    let players: string[] = [];
    if (!all) {
        if (!Array.isArray(rawPlayers) || rawPlayers.length === 0) v.fail("players", "liste de joueurs (un au moins) ou « all » attendue");
        else if (!rawPlayers.every((p) => typeof p === "string" && PLAYER_ID_RE.test(p))) v.fail("players", "identifiants de joueurs attendus");
        else players = [...new Set(rawPlayers as string[])];
    }
    const rawKey = body.request_key;
    if (rawKey !== undefined && rawKey !== null && (typeof rawKey !== "string" || !/^[A-Za-z0-9-]{8,64}$/.test(rawKey)))
        v.fail("request_key", "8 à 64 lettres, chiffres ou tirets");
    v.done();
    const db = ctx.env.DB;
    const now = nowIso();
    // A send abandoned while it was written goes, with what it wrote (ON DELETE CASCADE).
    await db.prepare("DELETE FROM gift_sends WHERE state = 'writing' AND touched_at < ?1").bind(new Date(Date.now() - STALE_WRITING_MS).toISOString()).run();
    const requestHash = await sha256Hex(
        JSON.stringify({ rewards: rewards.map(keyOf), players: all ? "all" : players, wrap, message, bundle }),
    );
    const key = typeof rawKey === "string" ? rawKey : null;
    let send = key
        ? await db
              .prepare("SELECT id, state, request_key, request_hash, progress, bundle FROM gift_sends WHERE sender_id = ?1 AND request_key = ?2")
              .bind(auth.player.id, key)
              .first<SendState>()
        : null;
    if (send && send.request_hash !== requestHash)
        throw Errors.conflict("REQUEST_KEY_REUSED", "Cette clé d'envoi sert déjà à un autre envoi.");
    if (send?.state === "sent") return json(ctx, { ...(await sendResult(db, send.id, [], send.request_key)), replayed: true });
    if (!send) {
        // A new send: the rate limit counts it once (its next steps are the same send), its players must exist.
        await rateLimit(ctx, RULES.giftSend, "player:" + auth.player.id);
        if (!all) {
            const unknown: string[] = [];
            for (let i = 0; i < players.length; i += 1000) {
                const part = players.slice(i, i + 1000);
                const found = await db
                    .prepare(`SELECT id FROM players WHERE ${IN_JSON("id", "?1")} AND account_status = 'active'`)
                    .bind(JSON.stringify(part))
                    .all<{ id: string }>();
                const ok = new Set(found.results.map((p) => p.id));
                unknown.push(...part.filter((p) => !ok.has(p)));
            }
            if (unknown.length) throw new ApiError(404, "PLAYER_NOT_FOUND", "Joueur introuvable ou compte inactif.", { players: unknown.slice(0, 100) });
        }
        const requestKey = key ?? crypto.randomUUID();
        try {
            send = await db
                .prepare(
                    `INSERT INTO gift_sends (sender_id, sender_name, wrap, message, rewards, created_at, bundle, state, request_key, request_hash, progress, touched_at)
                     VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, 'writing', ?8, ?9, '', ?6)
                     RETURNING id, state, request_key, request_hash, progress, bundle`,
                )
                .bind(auth.player.id, auth.player.username, wrap!, message, JSON.stringify(rewards.map((r) => ({ kind: r.kind, id: r.id }))), now, bundle ? 1 : 0, requestKey, requestHash)
                .first<SendState>();
        } catch {
            throw Errors.conflict("SEND_IN_PROGRESS", "Cet envoi est déjà en cours d'écriture : renvoyez la même demande.");
        }
    }
    const sendId = send!.id;
    const requestKey = send!.request_key;
    const stepPlayers = Math.max(1, Number(ctx.env.GIFT_STEP_PLAYERS ?? GIFT_STEP_PLAYERS) || GIFT_STEP_PLAYERS);
    const stepsPerRequest = Math.max(1, Number(ctx.env.GIFT_STEPS_PER_REQUEST ?? GIFT_STEPS_PER_REQUEST) || GIFT_STEPS_PER_REQUEST);
    // The steps: the next players, what they own, their gifts or parcels, and the progress — one batch each.
    let progress = send!.progress ?? "";
    const skipped: Skipped[] = [];
    for (let step = 0; ; ++step) {
        let chunk: { id: string; username: string }[];
        let next: string;
        if (all) {
            chunk = (
                await db
                    .prepare("SELECT id, username FROM players WHERE account_status = 'active' AND id > ?1 ORDER BY id LIMIT ?2")
                    .bind(progress, stepPlayers)
                    .all<{ id: string; username: string }>()
            ).results;
            next = chunk.length ? chunk[chunk.length - 1]!.id : progress;
        } else {
            const at = Number(progress || "0");
            const ids = players.slice(at, at + stepPlayers);
            next = String(at + ids.length);
            const found = ids.length
                ? await db
                      .prepare(`SELECT id, username FROM players WHERE ${IN_JSON("id", "?1")} AND account_status = 'active'`)
                      .bind(JSON.stringify(ids))
                      .all<{ id: string; username: string }>()
                : { results: [] };
            const names = new Map(found.results.map((p) => [p.id, p.username]));
            chunk = ids.filter((p) => names.has(p)).map((p) => ({ id: p, username: names.get(p)! }));
            if (ids.length === 0) chunk = [];
            else if (chunk.length === 0) {
                // Every player of this step left meanwhile: only the progress moves.
                await db.prepare("UPDATE gift_sends SET progress = ?2, touched_at = ?3 WHERE id = ?1 AND progress = ?4").bind(sendId, next, nowIso(), progress).run();
                progress = next;
                continue;
            }
        }
        if (chunk.length === 0) break;
        if (step >= stepsPerRequest)
            return json(ctx, { request_key: requestKey, state: "writing", done: all ? null : Number(progress), total: all ? null : players.length }, { status: 202 });
        const ids = chunk.map((p) => p.id);
        const owned = await giftOwnership(db, ids, rewards);
        let skippedHere = 0;
        const per = new Map<string, string[]>(); // reward key → recipients
        const parcelOf = new Map<string, GiftReward[]>(); // recipient → the rewards of its parcel, in order
        for (const r of rewards) {
            const to: string[] = [];
            for (const p of chunk) {
                const state = owned.get(p.id)?.get(keyOf(r));
                if (state) {
                    ++skippedHere;
                    if (skipped.length < MAX_SKIPPED_LISTED) skipped.push({ player_id: p.id, username: p.username, reward: keyOf(r), reason: state });
                } else {
                    to.push(p.id);
                    (parcelOf.get(p.id) ?? parcelOf.set(p.id, []).get(p.id)!).push({ kind: r.kind, id: r.id });
                }
            }
            per.set(keyOf(r), to);
        }
        const stmts: D1PreparedStatement[] = [];
        if (bundle) {
            // One parcel per recipient (in the order of the players), its rewards in the order of the send.
            const contents = ids.filter((p) => parcelOf.has(p)).map((p) => ({ to: p, rewards: parcelOf.get(p)! }));
            if (contents.length)
                stmts.push(
                    db
                        .prepare(
                            `INSERT OR IGNORE INTO gift_parcels (send_id, recipient_id, sender_id, wrap, message, rewards, created_at)
                             SELECT ?1, json_extract(j.value, '$.to'), ?2, ?3, ?4, json_extract(j.value, '$.rewards'), ?5 FROM json_each(?6) j ORDER BY j.key`,
                        )
                        .bind(sendId, auth.player.id, wrap!, message, now, JSON.stringify(contents)),
                );
        } else {
            for (const r of rewards) {
                const to = per.get(keyOf(r))!;
                if (to.length === 0) continue;
                stmts.push(
                    db
                        .prepare(
                            `INSERT OR IGNORE INTO gifts (send_id, recipient_id, sender_id, reward_kind, reward_id, wrap, message, created_at)
                             SELECT ?1, j.value, ?2, ?3, ?4, ?5, ?6, ?7 FROM json_each(?8) j`,
                        )
                        .bind(sendId, auth.player.id, r.kind, r.id, wrap!, message, now, JSON.stringify(to)),
                );
            }
        }
        // The progress, in the same batch: a step is written whole or not at all, and never twice.
        stmts.push(
            db
                .prepare("UPDATE gift_sends SET progress = ?2, skipped = skipped + ?3, touched_at = ?4 WHERE id = ?1 AND state = 'writing' AND progress = ?5")
                .bind(sendId, next, skippedHere, nowIso(), progress),
        );
        await db.batch(stmts);
        progress = next;
    }
    // The end: nothing to give → the send goes; else it becomes 'sent' with its notifications (one batch, once).
    const units = await sendUnits(db, sendId);
    if (units.created === 0) {
        await db.prepare("DELETE FROM gift_sends WHERE id = ?1 AND state = 'writing'").bind(sendId).run();
        throw Errors.conflict("NOTHING_TO_SEND", "Chaque joueur choisi possède déjà ces récompenses.", { skipped });
    }
    const data = JSON.stringify({ send_id: sendId });
    const writing = "EXISTS (SELECT 1 FROM gift_sends w WHERE w.id = ?1 AND w.state = 'writing')";
    const recipientsSql = bundle
        ? "SELECT recipient_id, 1 AS n FROM gift_parcels WHERE send_id = ?1"
        : "SELECT recipient_id, COUNT(*) AS n FROM gifts WHERE send_id = ?1 AND parcel_id IS NULL GROUP BY recipient_id";
    const done = await db.batch([
        db
            .prepare(
                `UPDATE notifications SET count = count + (SELECT u.n FROM (${recipientsSql}) u WHERE u.recipient_id = notifications.player_id),
                        actor_id = ?2, data = ?3, created_at = ?4
                 WHERE kind = 'gift' AND group_key = 'gifts' AND read_at IS NULL AND ${writing}
                   AND player_id IN (SELECT recipient_id FROM (${recipientsSql}))`,
            )
            .bind(sendId, auth.player.id, data, now),
        db
            .prepare(
                `INSERT INTO notifications (id, player_id, kind, actor_id, data, group_key, count, created_at)
                 SELECT lower(hex(randomblob(16))), u.recipient_id, 'gift', ?2, ?3, 'gifts', u.n, ?4 FROM (${recipientsSql}) u
                 WHERE ${writing}
                   AND instr(',' || (SELECT notifications_off FROM players WHERE id = u.recipient_id) || ',', ',gift,') = 0
                   AND NOT EXISTS (SELECT 1 FROM notifications x WHERE x.player_id = u.recipient_id AND x.kind = 'gift' AND x.group_key = 'gifts' AND x.read_at IS NULL)`,
            )
            .bind(sendId, auth.player.id, data, now),
        db.prepare("UPDATE gift_sends SET state = 'sent', touched_at = ?2 WHERE id = ?1 AND state = 'writing'").bind(sendId, nowIso()),
    ]);
    const result = await sendResult(db, sendId, skipped, requestKey);
    // The Journal: once, by the request that made it 'sent'.
    if ((done[2]?.meta.changes ?? 0) > 0) {
        const names = await db
            .prepare(
                `SELECT q.username FROM players q WHERE q.id IN (SELECT recipient_id FROM gifts WHERE send_id = ?1 UNION SELECT recipient_id FROM gift_parcels WHERE send_id = ?1)
                 ORDER BY q.username_normalized LIMIT 5`,
            )
            .bind(sendId)
            .all<{ username: string }>();
        await audit(ctx, auth.player.id, "admin_gift_sent", {
            by_username: auth.player.username,
            gift: {
                send_id: sendId,
                rewards: rewards.map((r) => ({ kind: r.kind, id: r.id, name: r.name })),
                wrap,
                recipients: units.recipients,
                gifts: units.created,
                ...(bundle ? { parcels: units.parcels } : {}),
                ...(all ? { all_players: true } : {}),
                skipped: result.skipped_count,
                names: names.results.map((n) => n.username),
            },
        });
    }
    return json(ctx, result, { status: 201 });
}

/** What a send gave: its rewards given (a parcel counts what it holds), its parcels, its recipients. */
async function sendUnits(db: D1Database, id: number): Promise<{ created: number; parcels: number; recipients: number }> {
    const r = await db
        .prepare(
            `SELECT (SELECT COUNT(*) FROM gifts WHERE send_id = ?1 AND parcel_id IS NULL) AS gifts,
                    (SELECT COUNT(*) FROM gift_parcels WHERE send_id = ?1) AS parcels,
                    (SELECT COALESCE(SUM(json_array_length(rewards)), 0) FROM gift_parcels WHERE send_id = ?1) AS held,
                    (SELECT COUNT(*) FROM (SELECT recipient_id FROM gifts WHERE send_id = ?1 AND parcel_id IS NULL
                                           UNION SELECT recipient_id FROM gift_parcels WHERE send_id = ?1)) AS recipients`,
        )
        .bind(id)
        .first<{ gifts: number; parcels: number; held: number; recipients: number }>();
    return { created: (r?.gifts ?? 0) + (r?.held ?? 0), parcels: r?.parcels ?? 0, recipients: r?.recipients ?? 0 };
}

/** The answer of a send written: the send, what it gave, the pairs skipped (the first ones, and how many). */
async function sendResult(db: D1Database, id: number, skipped: Skipped[], requestKey: string) {
    const units = await sendUnits(db, id);
    const row = await db.prepare("SELECT skipped FROM gift_sends WHERE id = ?1").bind(id).first<{ skipped: number }>();
    return {
        send: await sendView(db, id),
        created: units.created,
        parcels: units.parcels,
        skipped,
        skipped_count: row?.skipped ?? skipped.length,
        request_key: requestKey,
    };
}

interface SendRow {
    id: number;
    sender_id: string | null;
    sender_name: string;
    sender_username: string | null;
    wrap: string;
    message: string;
    rewards: string;
    created_at: string;
    bundle: number;
    total: number;
    opened: number;
    cancelled: number;
    recipients: number;
    names: string | null;
}

// A send's units: its gifts, or its parcels (the gifts a parcel wrote once opened are not counted again).
const SEND_SQL = `SELECT s.id, s.sender_id, s.sender_name, p.username AS sender_username, s.wrap, s.message, s.rewards, s.created_at, s.bundle,
        COUNT(g.recipient_id) AS total, COALESCE(SUM(g.opened_at IS NOT NULL), 0) AS opened, COALESCE(SUM(g.cancelled_at IS NOT NULL), 0) AS cancelled,
        COUNT(DISTINCT g.recipient_id) AS recipients,
        (SELECT json_group_array(username) FROM (SELECT DISTINCT q.username, q.username_normalized FROM
                                                     (SELECT recipient_id FROM gifts WHERE send_id = s.id UNION SELECT recipient_id FROM gift_parcels WHERE send_id = s.id) h
                                                 JOIN players q ON q.id = h.recipient_id ORDER BY q.username_normalized LIMIT 6)) AS names
    FROM gift_sends s LEFT JOIN players p ON p.id = s.sender_id
    LEFT JOIN (SELECT send_id, recipient_id, opened_at, cancelled_at FROM gifts WHERE parcel_id IS NULL
               UNION ALL SELECT send_id, recipient_id, opened_at, cancelled_at FROM gift_parcels) g ON g.send_id = s.id`;

function parseList(text: string | null): unknown[] {
    try {
        const v = JSON.parse(text ?? "[]");
        return Array.isArray(v) ? v : [];
    } catch {
        return [];
    }
}

function sendJson(r: SendRow) {
    return {
        id: r.id,
        at: r.created_at,
        sender: { id: r.sender_id, username: r.sender_username ?? r.sender_name },
        wrap: r.wrap,
        message: r.message,
        // A send of parcels (« Colis (N) »): `total`, `opened`, `cancelled` and `waiting` count its parcels.
        bundle: r.bundle === 1,
        rewards: parseList(r.rewards)
            .map((x) => {
                const o = (x ?? {}) as Record<string, unknown>;
                const g = giftReward(o.kind, o.id);
                return g ? { kind: g.kind, id: g.id, name: g.name } : null;
            })
            .filter((x) => x !== null),
        recipients: r.recipients,
        names: parseList(r.names).filter((x): x is string => typeof x === "string"),
        total: r.total,
        opened: r.opened,
        cancelled: r.cancelled,
        waiting: r.total - r.opened - r.cancelled,
    };
}

async function sendView(db: D1Database, id: number) {
    const r = await db.prepare(`${SEND_SQL} WHERE s.id = ?1 AND s.state = 'sent' GROUP BY s.id`).bind(id).first<SendRow>();
    return r ? sendJson(r) : null;
}

/** GET /admin/gifts?limit=&offset=: the sends, newest first, with their gifts opened, cancelled, waiting. */
export async function adminHistory(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const page = paging(ctx.url, 20, 100);
    const rows = await ctx.env.DB.prepare(`${SEND_SQL} WHERE s.state = 'sent' GROUP BY s.id ORDER BY s.created_at DESC, s.id DESC LIMIT ?1 OFFSET ?2`)
        .bind(page.limit + 1, page.offset)
        .all<SendRow>();
    return json(ctx, { sends: rows.results.slice(0, page.limit).map(sendJson), has_more: rows.results.length > page.limit, offset: page.offset });
}

/** POST /admin/gifts/{sendId}/cancel: withdraws the gifts (or the parcels) of a send not opened yet (the opened ones stay). */
export async function adminCancel(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const id = Number(ctx.params.sendId);
    if (!Number.isSafeInteger(id) || id < 1) throw Errors.notFound("GIFT_SEND_NOT_FOUND", "Envoi introuvable.");
    const db = ctx.env.DB;
    const before = await sendView(db, id);
    if (!before) throw Errors.notFound("GIFT_SEND_NOT_FOUND", "Envoi introuvable.");
    const now = nowIso();
    const res = await db.batch([
        db.prepare("UPDATE gifts SET cancelled_at = ?2 WHERE send_id = ?1 AND opened_at IS NULL AND cancelled_at IS NULL").bind(id, now),
        db.prepare("UPDATE gift_parcels SET cancelled_at = ?2 WHERE send_id = ?1 AND opened_at IS NULL AND cancelled_at IS NULL").bind(id, now),
    ]);
    const cancelled = res.reduce((n, x) => n + (x.meta.changes ?? 0), 0);
    if (cancelled > 0)
        await audit(ctx, auth.player.id, "admin_gift_cancelled", {
            by_username: auth.player.username,
            gift: {
                send_id: id,
                rewards: before.rewards,
                wrap: before.wrap,
                recipients: before.recipients,
                gifts: before.total,
                cancelled,
                ...(before.bundle ? { parcels: before.total } : {}),
            },
        });
    return json(ctx, { cancelled, send: await sendView(db, id) });
}

// ------------------------------------------------------------------ players

interface GiftRow {
    id: number;
    reward_kind: GiftKind;
    reward_id: string;
    wrap: string;
    message: string;
    created_at: string;
    opened_at: string | null;
    sender_id: string | null;
    sender_name: string;
    sender_username: string | null;
}

const GIFT_SQL = `SELECT g.id, g.reward_kind, g.reward_id, g.wrap, g.message, g.created_at, g.opened_at, g.sender_id, s.sender_name,
        p.username AS sender_username
    FROM gifts g JOIN gift_sends s ON s.id = g.send_id LEFT JOIN players p ON p.id = g.sender_id`;

/** A gift as its recipient sees it: what it holds only once opened. */
function giftJson(g: GiftRow) {
    const opened = g.opened_at !== null;
    const reward = opened ? giftReward(g.reward_kind, g.reward_id) : null;
    return {
        id: g.id,
        parcel: false,
        from: { id: g.sender_id, username: g.sender_username ?? g.sender_name },
        wrap: g.wrap,
        message: g.message,
        created_at: g.created_at,
        opened,
        opened_at: g.opened_at,
        // Withheld until opened: the game cannot tell what a wrapped gift holds.
        reward: opened ? { kind: g.reward_kind, id: g.reward_id, name: reward?.name ?? g.reward_id } : null,
    };
}

interface ParcelRow {
    id: number;
    rewards: string;
    wrap: string;
    message: string;
    created_at: string;
    opened_at: string | null;
    sender_id: string | null;
    sender_name: string;
    sender_username: string | null;
}

const PARCEL_SQL = `SELECT c.id, c.rewards, c.wrap, c.message, c.created_at, c.opened_at, c.sender_id, s.sender_name, p.username AS sender_username
    FROM gift_parcels c JOIN gift_sends s ON s.id = c.send_id LEFT JOIN players p ON p.id = c.sender_id`;

/** The rewards a parcel holds, in their order (as written by adminSend: {kind, id}). */
function parcelRewards(text: string): GiftReward[] {
    return parseList(text)
        .map((x) => {
            const o = (x ?? {}) as Record<string, unknown>;
            return typeof o.kind === "string" && typeof o.id === "string" && (GIFT_KINDS as readonly string[]).includes(o.kind) && ID_RE.test(o.id)
                ? { kind: o.kind as GiftKind, id: o.id }
                : null;
        })
        .filter((x) => x !== null);
}

/**
 * A parcel as its recipient sees it: one wrapped entry. Before it is opened, NOTHING tells what it holds nor how
 * many: no reward, no list, no count (`contents` is null). Once opened: `contents`, its rewards in their order.
 */
function parcelJson(c: ParcelRow) {
    const opened = c.opened_at !== null;
    return {
        id: c.id,
        parcel: true,
        from: { id: c.sender_id, username: c.sender_username ?? c.sender_name },
        wrap: c.wrap,
        message: c.message,
        created_at: c.created_at,
        opened,
        opened_at: c.opened_at,
        reward: null,
        contents: opened ? parcelRewards(c.rewards).map((r) => ({ kind: r.kind, id: r.id, name: giftReward(r.kind, r.id)?.name ?? r.id })) : null,
    };
}

/**
 * GET /gifts/me: my gifts and parcels (never the cancelled ones), newest first, with how many wait to be opened
 * (a parcel counts as one). The gifts a parcel held are listed in it (`contents`), once opened, never apart.
 */
export async function myGifts(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const db = ctx.env.DB;
    const [rows, parcels] = await Promise.all([
        db
            .prepare(`${GIFT_SQL} WHERE g.recipient_id = ?1 AND g.cancelled_at IS NULL AND g.parcel_id IS NULL AND s.state = 'sent' ORDER BY g.created_at DESC, g.id DESC LIMIT 300`)
            .bind(auth.player.id)
            .all<GiftRow>(),
        db
            .prepare(`${PARCEL_SQL} WHERE c.recipient_id = ?1 AND c.cancelled_at IS NULL AND s.state = 'sent' ORDER BY c.created_at DESC, c.id DESC LIMIT 300`)
            .bind(auth.player.id)
            .all<ParcelRow>(),
    ]);
    const gifts = [...rows.results.map(giftJson), ...parcels.results.map(parcelJson)]
        .sort((a, b) => (a.created_at < b.created_at ? 1 : a.created_at > b.created_at ? -1 : 0))
        .slice(0, 300);
    return json(ctx, { gifts, unopened: gifts.filter((g) => !g.opened).length, total: gifts.length });
}

/**
 * POST /gifts/{giftId}/open: opens one of my gifts and grants what it holds (idempotent: opening it again
 * returns it as it is and grants nothing more). `raid_rewards_claimed`: the raid rewards this account holds
 * (a badge's achievement may then be declared), as CLANS › RAIDS gives them.
 */
export async function openGift(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.giftOpen, "player:" + me);
    const id = Number(ctx.params.giftId);
    if (!Number.isSafeInteger(id) || id < 1) throw Errors.notFound("GIFT_NOT_FOUND", "Cadeau introuvable.");
    const db = ctx.env.DB;
    const g = await db
        .prepare(`${GIFT_SQL} WHERE g.id = ?1 AND g.recipient_id = ?2 AND g.cancelled_at IS NULL AND g.parcel_id IS NULL AND s.state = 'sent'`)
        .bind(id, me)
        .first<GiftRow>();
    if (!g) throw Errors.notFound("GIFT_NOT_FOUND", "Cadeau introuvable (il a peut-être été retiré).");
    const now = nowIso();
    const grant =
        g.reward_kind === "badge"
            ? db
                  .prepare(
                      `INSERT OR IGNORE INTO raid_reward_grants (player_id, reward, event_id, instance_id, reason, granted_at, claimed_at)
                       SELECT g.recipient_id, ?3, 'gift:' || g.id, NULL, 'Cadeau de ' || s.sender_name, g.opened_at, g.opened_at
                       FROM gifts g JOIN gift_sends s ON s.id = g.send_id WHERE g.id = ?1 AND g.recipient_id = ?2 AND g.opened_at IS NOT NULL`,
                  )
                  .bind(id, me, RAID_ACHIEVEMENTS[g.reward_id] ?? "")
            : db
                  .prepare(
                      `INSERT OR IGNORE INTO player_won_rewards (player_id, challenge_id, reward_kind, reward_ref, granted_at)
                       SELECT g.recipient_id, 'gift:' || g.id, 'item', g.reward_id, g.opened_at FROM gifts g
                       WHERE g.id = ?1 AND g.recipient_id = ?2 AND g.opened_at IS NOT NULL`,
                  )
                  .bind(id, me);
    const known = g.reward_kind === "badge" ? RAID_ACHIEVEMENTS[g.reward_id] !== undefined : ITEMS.has(g.reward_id);
    const res = await db.batch([
        db.prepare("UPDATE gifts SET opened_at = ?3 WHERE id = ?1 AND recipient_id = ?2 AND opened_at IS NULL AND cancelled_at IS NULL").bind(id, me, now),
        ...(known ? [grant] : []),
    ]);
    const openedNow = (res[0]!.meta.changes ?? 0) > 0;
    const after = await db.prepare(`${GIFT_SQL} WHERE g.id = ?1`).bind(id).first<GiftRow>();
    const mine = await rewardsOf(db, me);
    return json(ctx, { gift: giftJson(after!), opened_now: openedNow, raid_rewards_claimed: mine.claimed });
}

/**
 * POST /gifts/parcels/{parcelId}/open: opens one of my parcels. One batch: the parcel marked opened (a conditional
 * UPDATE), one opened gift written per reward it holds, in order (gifts.parcel_id, only if none was written yet),
 * each granted as a gift is (player_won_rewards « gift:<id> », a claimed raid badge; INSERT OR IGNORE). Idempotent:
 * opening it again returns it as it is and grants nothing more. 200 {gift (the parcel, `contents` now listed),
 * items (what to reveal, in order), opened_now, raid_rewards_claimed}.
 */
export async function openParcel(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.giftOpen, "player:" + me);
    const id = Number(ctx.params.parcelId);
    if (!Number.isSafeInteger(id) || id < 1) throw Errors.notFound("GIFT_NOT_FOUND", "Colis introuvable.");
    const db = ctx.env.DB;
    const c = await db.prepare(`${PARCEL_SQL} WHERE c.id = ?1 AND c.recipient_id = ?2 AND c.cancelled_at IS NULL AND s.state = 'sent'`).bind(id, me).first<ParcelRow>();
    if (!c) throw Errors.notFound("GIFT_NOT_FOUND", "Colis introuvable (il a peut-être été retiré).");
    const rewards = parcelRewards(c.rewards);
    // What may be granted: the items still in the catalogue, the badges the server still gives (by achievement).
    const items = rewards.filter((r) => r.kind !== "badge" && ITEMS.has(r.id)).map((r) => r.id);
    const badges: Record<string, string> = {};
    for (const r of rewards) if (r.kind === "badge" && RAID_ACHIEVEMENTS[r.id] !== undefined) badges[r.id] = RAID_ACHIEVEMENTS[r.id]!;
    const now = nowIso();
    const res = await db.batch([
        db.prepare("UPDATE gift_parcels SET opened_at = ?3 WHERE id = ?1 AND recipient_id = ?2 AND opened_at IS NULL AND cancelled_at IS NULL").bind(id, me, now),
        db
            .prepare(
                `INSERT INTO gifts (send_id, recipient_id, sender_id, reward_kind, reward_id, wrap, message, created_at, opened_at, parcel_id)
                 SELECT c.send_id, c.recipient_id, c.sender_id, json_extract(j.value, '$.kind'), json_extract(j.value, '$.id'), c.wrap, c.message, c.created_at,
                        c.opened_at, c.id
                 FROM gift_parcels c, json_each(c.rewards) j
                 WHERE c.id = ?1 AND c.recipient_id = ?2 AND c.opened_at IS NOT NULL AND NOT EXISTS (SELECT 1 FROM gifts x WHERE x.parcel_id = c.id)
                   AND json_extract(j.value, '$.kind') IN (SELECT value FROM json_each(?3))
                 ORDER BY j.key`,
            )
            .bind(id, me, JSON.stringify(GIFT_KINDS)),
        db
            .prepare(
                `INSERT OR IGNORE INTO player_won_rewards (player_id, challenge_id, reward_kind, reward_ref, granted_at)
                 SELECT g.recipient_id, 'gift:' || g.id, 'item', g.reward_id, g.opened_at FROM gifts g
                 WHERE g.parcel_id = ?1 AND g.recipient_id = ?2 AND g.reward_kind <> 'badge' AND ${IN_JSON("g.reward_id", "?3")}`,
            )
            .bind(id, me, JSON.stringify(items)),
        db
            .prepare(
                `INSERT OR IGNORE INTO raid_reward_grants (player_id, reward, event_id, instance_id, reason, granted_at, claimed_at)
                 SELECT g.recipient_id, json_extract(?3, '$.' || g.reward_id), 'gift:' || g.id, NULL, 'Cadeau de ' || s.sender_name, g.opened_at, g.opened_at
                 FROM gifts g JOIN gift_sends s ON s.id = g.send_id
                 WHERE g.parcel_id = ?1 AND g.recipient_id = ?2 AND g.reward_kind = 'badge' AND json_extract(?3, '$.' || g.reward_id) IS NOT NULL`,
            )
            .bind(id, me, JSON.stringify(badges)),
    ]);
    const openedNow = (res[0]!.meta.changes ?? 0) > 0;
    const after = await db.prepare(`${PARCEL_SQL} WHERE c.id = ?1`).bind(id).first<ParcelRow>();
    const parcel = parcelJson(after!);
    const mine = await rewardsOf(db, me);
    return json(ctx, { gift: parcel, items: parcel.contents ?? [], opened_now: openedNow, raid_rewards_claimed: mine.claimed });
}
