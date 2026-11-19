// Clans (2.66, migration 0120): one clan per player, 50 members at most, a chief (one, who may
// hand the role over), officers and members. Joining by invitation, by application (a short
// message) or open, as the chief chooses. The clan's discussion is a group conversation of
// social.ts whose members follow the clan's (conversations.clan_id). The clan's points are the
// sum, over the official EXTRÊME levels, of its best VERIFIED result of the « course à la
// vérification » (race.ts): computed here from race_runs, never sent by a game.
//
// The player is always the session's (never an id from the request). Every rule that two
// requests could race on is decided inside one statement or one batch (a D1 transaction):
//   - the 51st member: the INSERT that adds a member computes its role from the member count
//     at that moment; an impossible role ('full', 'refused') fails the CHECK constraint, which
//     rolls the whole batch back;
//   - one clan per player: clan_members.player_id is the primary key;
//   - one chief: a partial unique index, and every batch that removes a member names a new
//     chief when the clan has none left (the oldest officer, else the oldest member);
//   - rights: each change re-checks the actor's role in its own WHERE clause.

import { audit, requireAuth, type AuthInfo } from "./auth";
import { requireAdminRole } from "./adminRoles";
import { adminPresenceOf, countOnline, isAdminViewer, listPresenceOf } from "./friends";
import { ApiError, Errors, json, log, nowIso, readJsonObject, type Ctx } from "./http";
import { playerSummary, type PlayerRow } from "./model";
import { raidLeaveStatements } from "./raids";
import { RULES, rateLimit } from "./ratelimit";
import { USERNAME_RE, Validator, normalizeUsername, paging, queryString } from "./validate";

export const MAX_CLAN_MEMBERS = 50;
export const EMBLEM_SHAPES = ["orb", "star", "shield", "diamond", "hexagon", "crescent"] as const;
export const JOIN_POLICIES = ["invite", "request", "open"] as const;
export type ClanRole = "leader" | "officer" | "member";
/** Pending applications a player may have at once, pending requests (both kinds) of a clan. */
export const MAX_APPLICATIONS = 5;
export const MAX_PENDING = 100;
export const NAME_MIN = 3;
export const NAME_MAX = 32;
export const MOTTO_MAX = 80;
export const MESSAGE_MAX = 120;

const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const TAG_RE = /^[A-Za-z0-9]{2,5}$/;
const COLOR_RE = /^#[0-9a-fA-F]{6}$/;
// Letters of the game's fonts (Latin with accents), digits, spaces and a little punctuation.
const NAME_CHARS = "0-9A-Za-z\\u00C0-\\u00D6\\u00D8-\\u00F6\\u00F8-\\u017F";
const NAME_RE = new RegExp(`^[${NAME_CHARS}][${NAME_CHARS} '’\\-_.!&]*$`);
const RESERVED_TAGS = new Set(["ADMIN", "MODO", "MOD", "STAFF", "ROOT", "NULL", "DEV", "SYS"]);

interface ClanRow {
    id: string;
    name: string;
    name_normalized: string;
    tag: string;
    tag_normalized: string;
    motto: string;
    emblem_shape: string;
    emblem_primary: string;
    emblem_secondary: string;
    join_policy: (typeof JOIN_POLICIES)[number];
    points: number;
    points_levels: number;
    points_at: string | null;
    created_by: string | null;
    created_at: string;
    updated_at: string;
    /** Members, counted by the query (MEMBERS). */
    members: number;
}

/** Members of the clan `c` (index clan_members_clan). */
const MEMBERS = "(SELECT COUNT(*) FROM clan_members x WHERE x.clan_id = c.id)";
const ROLE_ORDER = "CASE m.role WHEN 'leader' THEN 0 WHEN 'officer' THEN 1 ELSE 2 END";

// ------------------------------------------------------------------ text rules

/** A single line: control, zero-width and bidi characters removed, spaces collapsed. */
export function cleanLine(raw: string): string {
    return (
        raw
            // eslint-disable-next-line no-control-regex
            .replace(/[\u0000-\u001f\u007f-\u009f​-‏‪-‮⁦-⁩﻿]/g, " ")
            .replace(/\s+/g, " ")
            .trim()
    );
}

/** The search form of a name: lower case, without accents. */
export function normalizeClanName(name: string): string {
    return name.normalize("NFD").replace(/[̀-ͯ]/g, "").toLowerCase();
}

/** Checks a clan name (already cleaned); the error message or null. */
export function clanNameError(name: string): string | null {
    if (name.length < NAME_MIN || name.length > NAME_MAX) return `${NAME_MIN} à ${NAME_MAX} caractères`;
    if (!NAME_RE.test(name)) return "lettres, chiffres, espaces et ' - _ . ! & seulement, en commençant par une lettre ou un chiffre";
    return null;
}

/** Checks a tag; the error message or null. */
export function clanTagError(tag: string): string | null {
    if (!TAG_RE.test(tag)) return "2 à 5 lettres ou chiffres";
    if (RESERVED_TAGS.has(tag.toUpperCase())) return "ce tag est réservé";
    return null;
}

// --------------------------------------------------------------------- JSON

function clanJson(c: ClanRow) {
    return {
        id: c.id,
        name: c.name,
        tag: c.tag,
        motto: c.motto,
        emblem: { shape: c.emblem_shape, primary: c.emblem_primary, secondary: c.emblem_secondary },
        join_policy: c.join_policy,
        members: c.members,
        max_members: MAX_CLAN_MEMBERS,
        points: c.points,
        points_levels: c.points_levels,
        created_at: c.created_at,
    };
}

type MemberRow = Pick<
    PlayerRow,
    "id" | "username" | "avatar" | "avatar_custom_key" | "country_code" | "location_visibility" | "presence_state" | "presence_at" | "presence_visible"
> & { role: ClanRole; joined_at: string; profile_visibility?: string };

// ------------------------------------------------------------- shared SQL

/**
 * The race boards of the official EXTRÊME levels: the current revision of every level whose
 * revision has a race layout, and the earlier revisions it carried (« Garder les records »,
 * campaign_board_links), as race.ts counts them.
 */
const EXTREME_BOARDS = `ext(level_id, board) AS (
    SELECT l.id, 'o:' || l.id || ':' || l.content_hash FROM levels l
      JOIN level_versions v ON v.level_id = l.id AND v.content_hash = l.content_hash
     WHERE v.race IS NOT NULL
    UNION ALL
    SELECT l.id, 'o:' || l.id || ':' || k.from_hash FROM levels l
      JOIN level_versions v ON v.level_id = l.id AND v.content_hash = l.content_hash
      JOIN campaign_board_links k ON k.to_hash = l.content_hash
     WHERE v.race IS NOT NULL)`;

/** The row of clan_level_bests `b` that counts for its level: the best score, a smaller team on a tie. */
const COUNTED_BEST = `NOT EXISTS (SELECT 1 FROM clan_level_bests o WHERE o.clan_id = b.clan_id AND o.level_id = b.level_id
    AND (o.score > b.score OR (o.score = b.score AND o.team_size < b.team_size)))`;

/**
 * Recomputes the bests of one clan (or of every clan with null) and its points, in four
 * statements to put in a batch (so a membership change and the points move together): Solo
 * (race_runs), then Duo and Trio (coop_runs, 2.63: a team counts for a clan when every one of
 * its players is a member and confirmed the attempt, as the Duo / Trio boards rank it).
 */
export function clanPointsStatements(db: D1Database, clanId: string | null): D1PreparedStatement[] {
    const one = clanId !== null;
    const binds = one ? [clanId] : [];
    return [
        db.prepare(`DELETE FROM clan_level_bests${one ? " WHERE clan_id = ?1" : ""}`).bind(...binds),
        db
            .prepare(
                `WITH ${EXTREME_BOARDS},
                 best AS (SELECT m.clan_id, e.level_id, r.id AS run_id, r.player_id, r.score, r.finished, r.verified_at,
                                 ROW_NUMBER() OVER (PARTITION BY m.clan_id, e.level_id ORDER BY r.score DESC, r.verified_at, r.id) AS n
                            FROM clan_members m JOIN race_runs r ON r.player_id = m.player_id AND r.status = 'VERIFIED'
                            JOIN ext e ON e.board = r.board${one ? " WHERE m.clan_id = ?1" : ""})
                 INSERT INTO clan_level_bests (clan_id, level_id, team_size, score, finished, run_id, players, achieved_at)
                 SELECT clan_id, level_id, 1, score, finished, run_id, json_array(player_id), verified_at FROM best WHERE n = 1`,
            )
            .bind(...binds),
        db
            .prepare(
                `WITH ${EXTREME_BOARDS},
                 best AS (SELECT m.clan_id, e.level_id, r.size, r.id AS run_id, r.score, r.finished, r.verified_at,
                                 ROW_NUMBER() OVER (PARTITION BY m.clan_id, e.level_id, r.size
                                                    ORDER BY r.score DESC, r.incomplete, r.revives, r.verified_at, r.id) AS n
                            FROM coop_runs r JOIN ext e ON e.board = r.board
                            JOIN coop_run_members s0 ON s0.run_id = r.id AND s0.seat = 0
                            JOIN clan_members m ON m.player_id = s0.player_id
                           WHERE r.status = 'VERIFIED'${one ? " AND m.clan_id = ?1" : ""}
                             AND NOT EXISTS (SELECT 1 FROM coop_run_members x WHERE x.run_id = r.id
                                   AND (NOT EXISTS (SELECT 1 FROM clan_members y WHERE y.player_id = x.player_id AND y.clan_id = m.clan_id)
                                        OR NOT EXISTS (SELECT 1 FROM coop_confirms c WHERE c.attempt_id = r.attempt_id AND c.player_id = x.player_id))))
                 INSERT INTO clan_level_bests (clan_id, level_id, team_size, score, finished, run_id, players, achieved_at)
                 SELECT clan_id, level_id, size, score, finished, run_id,
                        (SELECT json_group_array(player_id) FROM (SELECT player_id FROM coop_run_members WHERE run_id = best.run_id ORDER BY seat)),
                        verified_at
                   FROM best WHERE n = 1`,
            )
            .bind(...binds),
        db
            .prepare(
                `UPDATE clans SET
                   points = COALESCE((SELECT ROUND(SUM(b.score), 1) FROM clan_level_bests b WHERE b.clan_id = clans.id AND ${COUNTED_BEST}), 0),
                   points_levels = (SELECT COUNT(DISTINCT b.level_id) FROM clan_level_bests b WHERE b.clan_id = clans.id),
                   points_at = (SELECT MAX(b.achieved_at) FROM clan_level_bests b WHERE b.clan_id = clans.id AND ${COUNTED_BEST})${one ? " WHERE id = ?1" : ""}`,
            )
            .bind(...binds),
    ];
}

/** After a member's attempt is verified on an official race: the points of their clan. Never fails the caller. */
export async function refreshClanOfPlayer(ctx: Ctx, playerId: string): Promise<void> {
    try {
        const m = await ctx.env.DB.prepare("SELECT clan_id FROM clan_members WHERE player_id = ?1").bind(playerId).first<{ clan_id: string }>();
        if (m) await ctx.env.DB.batch(clanPointsStatements(ctx.env.DB, m.clan_id));
    } catch (e) {
        log("error", "clan_points_failed", { error: e instanceof Error ? e.message : String(e) });
    }
}

/** Every clan's points (daily maintenance, a campaign revision published). Never fails the caller. */
export async function refreshAllClanPoints(db: D1Database): Promise<void> {
    try {
        await db.batch(clanPointsStatements(db, null));
    } catch (e) {
        log("error", "clan_points_failed", { error: e instanceof Error ? e.message : String(e) });
    }
}

/** A chief for a clan that has none (the oldest officer, else the oldest member). */
function ensureLeader(db: D1Database, clanId: string): D1PreparedStatement {
    return db
        .prepare(
            `UPDATE clan_members SET role = 'leader' WHERE player_id = (
               SELECT m.player_id FROM clan_members m WHERE m.clan_id = ?1
               ORDER BY CASE m.role WHEN 'officer' THEN 0 ELSE 1 END, m.joined_at, m.player_id LIMIT 1)
             AND NOT EXISTS (SELECT 1 FROM clan_members x WHERE x.clan_id = ?1 AND x.role = 'leader')`,
        )
        .bind(clanId);
}

/** A clan without members is gone (its discussion, requests and bests with it). */
function dropIfEmpty(db: D1Database, clanId: string): D1PreparedStatement {
    return db.prepare("DELETE FROM clans WHERE id = ?1 AND NOT EXISTS (SELECT 1 FROM clan_members WHERE clan_id = ?1)").bind(clanId);
}

/** The clan's discussion follows its members: a member who left is no longer in it. */
function chatLeave(db: D1Database, clanId: string, playerId: string): D1PreparedStatement {
    return db
        .prepare(
            `DELETE FROM conversation_members WHERE conversation_id = (SELECT id FROM conversations WHERE clan_id = ?1) AND player_id = ?2
               AND NOT EXISTS (SELECT 1 FROM clan_members WHERE clan_id = ?1 AND player_id = ?2)`,
        )
        .bind(clanId, playerId);
}

/** ...and a new member is in it (the earlier messages are there, already read). */
function chatJoin(db: D1Database, clanId: string, playerId: string): D1PreparedStatement {
    return db
        .prepare(
            `INSERT OR IGNORE INTO conversation_members (conversation_id, player_id, role, joined_at, last_read_at)
             SELECT c.id, m.player_id, 'member', m.joined_at, m.joined_at FROM conversations c
             JOIN clan_members m ON m.clan_id = c.clan_id AND m.player_id = ?2 WHERE c.clan_id = ?1`,
        )
        .bind(clanId, playerId);
}

/**
 * Adds `playerId` to the clan when `allowedSql` holds (binds: ?1 clan, ?2 player, ?3 now,
 * ?4 actor) and the clan has room, decided by the INSERT itself: otherwise its role is not one
 * of the allowed values and the CHECK constraint rolls the batch back. Followed by the
 * discussion, the end of the player's other requests and the clan's points.
 */
function joinStatements(db: D1Database, clanId: string, playerId: string, now: string, actor: string, allowedSql: string): D1PreparedStatement[] {
    return [
        db
            .prepare(
                `INSERT INTO clan_members (player_id, clan_id, role, joined_at)
                 SELECT ?2, ?1, CASE
                   WHEN (SELECT COUNT(*) FROM clan_members WHERE clan_id = ?1) >= ${MAX_CLAN_MEMBERS} THEN 'full'
                   WHEN ?4 IS NULL OR NOT (${allowedSql}) THEN 'refused'
                   ELSE 'member' END, ?3`,
            )
            .bind(clanId, playerId, now, actor),
        chatJoin(db, clanId, playerId),
        db.prepare("DELETE FROM clan_requests WHERE player_id = ?1").bind(playerId),
        ...clanPointsStatements(db, clanId),
    ];
}

/** The actor is the chief or an officer of the clan (?1 clan, ?4 actor). */
const ACTOR_MANAGES = "EXISTS (SELECT 1 FROM clan_members a WHERE a.clan_id = ?1 AND a.player_id = ?4 AND a.role IN ('leader', 'officer'))";
const HAS_APPLIED = "EXISTS (SELECT 1 FROM clan_requests q WHERE q.clan_id = ?1 AND q.player_id = ?2 AND q.kind = 'apply')";
const IS_INVITED = "EXISTS (SELECT 1 FROM clan_requests q WHERE q.clan_id = ?1 AND q.player_id = ?2 AND q.kind = 'invite')";
const IS_OPEN = "(SELECT join_policy FROM clans WHERE id = ?1) = 'open'";

function constraintOf(e: unknown): "member" | "tag" | "leader" | "check" | "fk" | null {
    const s = e instanceof Error ? e.message : String(e);
    if (s.includes("UNIQUE") && s.includes("clan_members.player_id")) return "member";
    if (s.includes("UNIQUE") && s.includes("clans.tag_normalized")) return "tag";
    if (s.includes("UNIQUE") && s.includes("clan_members.clan_id")) return "leader";
    if (s.includes("CHECK")) return "check";
    if (s.includes("FOREIGN KEY")) return "fk";
    return null;
}

const ALREADY_IN_CLAN = () => Errors.conflict("ALREADY_IN_CLAN", "Ce joueur est déjà dans un clan.");
const CLAN_FULL = () => Errors.conflict("CLAN_FULL", `Un clan compte au plus ${MAX_CLAN_MEMBERS} membres.`);
const CLAN_NOT_FOUND = () => Errors.notFound("CLAN_NOT_FOUND", "Clan introuvable.");

/** Runs a join batch; the refusals told apart (atomic decision, explained afterwards). */
async function runJoin(ctx: Ctx, clanId: string, stmts: D1PreparedStatement[], refused: ApiError, mine = false): Promise<void> {
    try {
        await ctx.env.DB.batch(stmts);
    } catch (e) {
        const k = constraintOf(e);
        if (k === "member") throw mine ? Errors.conflict("ALREADY_IN_CLAN", "Vous êtes déjà dans un clan.") : ALREADY_IN_CLAN();
        if (k === "fk") throw CLAN_NOT_FOUND();
        if (k === "check") {
            const n = await ctx.env.DB.prepare("SELECT COUNT(*) AS n FROM clan_members WHERE clan_id = ?1").bind(clanId).first<{ n: number }>();
            if ((n?.n ?? 0) >= MAX_CLAN_MEMBERS) throw CLAN_FULL();
            throw refused;
        }
        throw e;
    }
}

// ------------------------------------------------------------------ helpers

function clanIdParam(ctx: Ctx): string {
    const id = ctx.params.clanId ?? "";
    if (!ID_RE.test(id)) throw CLAN_NOT_FOUND();
    return id;
}

function playerIdParam(ctx: Ctx): string {
    const id = ctx.params.playerId ?? "";
    if (!ID_RE.test(id)) throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    return id;
}

async function loadClan(ctx: Ctx, id: string): Promise<ClanRow> {
    const c = await ctx.env.DB.prepare(`SELECT c.*, ${MEMBERS} AS members FROM clans c WHERE c.id = ?1`).bind(id).first<ClanRow>();
    if (!c) throw CLAN_NOT_FOUND();
    return c;
}

async function roleIn(ctx: Ctx, clanId: string, playerId: string): Promise<ClanRole | null> {
    const r = await ctx.env.DB.prepare("SELECT role FROM clan_members WHERE clan_id = ?1 AND player_id = ?2").bind(clanId, playerId).first<{ role: ClanRole }>();
    return r?.role ?? null;
}

const MANAGERS_ONLY = () => Errors.forbidden("NOT_CLAN_OFFICER", "Seuls le chef et les officiers du clan peuvent le faire.");
const LEADER_ONLY = () => Errors.forbidden("NOT_CLAN_LEADER", "Seul le chef du clan peut le faire.");

/** Clan notifications (kind « clan »), for some players at once; skipped for the actor, blocks and a kind turned off. */
async function notifyClan(ctx: Ctx, recipients: string[], actor: string | null, data: Record<string, unknown>): Promise<void> {
    if (recipients.length === 0) return;
    try {
        await ctx.env.DB.prepare(
            `INSERT INTO notifications (id, player_id, kind, actor_id, data, count, created_at)
             SELECT lower(hex(randomblob(16))), r.value, 'clan', ?2, ?3, 1, ?4 FROM json_each(?1) r
             WHERE r.value IS NOT ?2 AND EXISTS (SELECT 1 FROM players p WHERE p.id = r.value
                   AND instr(',' || p.notifications_off || ',', ',clan,') = 0)
               AND NOT EXISTS (SELECT 1 FROM player_blocks b WHERE (b.player_id = r.value AND b.blocked_id = ?2) OR (b.player_id = ?2 AND b.blocked_id = r.value))`,
        )
            .bind(JSON.stringify(recipients), actor, JSON.stringify(data), nowIso())
            .run();
    } catch (e) {
        log("error", "clan_notify_failed", { error: e instanceof Error ? e.message : String(e) });
    }
}

const clanRef = (c: Pick<ClanRow, "id" | "name" | "tag">) => ({ clan_id: c.id, name: c.name, tag: c.tag });

async function managersOf(ctx: Ctx, clanId: string): Promise<string[]> {
    const r = await ctx.env.DB.prepare("SELECT player_id FROM clan_members WHERE clan_id = ?1 AND role IN ('leader', 'officer')").bind(clanId).all<{ player_id: string }>();
    return r.results.map((x) => x.player_id);
}

/** The clan tags of some players (one indexed query per 90 ids): for SOCIAL and the boards. */
export async function clanTagsOf(db: D1Database, ids: Iterable<string>): Promise<Map<string, string>> {
    const list = [...new Set(ids)];
    const out = new Map<string, string>();
    for (let at = 0; at < list.length; at += 90) {
        const chunk = list.slice(at, at + 90);
        const rows = await db
            .prepare(`SELECT m.player_id, c.tag FROM clan_members m JOIN clans c ON c.id = m.clan_id WHERE m.player_id IN (${chunk.map((_, i) => `?${i + 1}`).join(", ")})`)
            .bind(...chunk)
            .all<{ player_id: string; tag: string }>();
        for (const r of rows.results) out.set(r.player_id, r.tag);
    }
    return out;
}

/** A player's clan for their profile (null without one). */
export async function clanOfPlayer(db: D1Database, playerId: string) {
    const c = await db
        .prepare(
            `SELECT c.id, c.name, c.tag, c.emblem_shape, c.emblem_primary, c.emblem_secondary, m.role FROM clan_members m
             JOIN clans c ON c.id = m.clan_id WHERE m.player_id = ?1`,
        )
        .bind(playerId)
        .first<{ id: string; name: string; tag: string; emblem_shape: string; emblem_primary: string; emblem_secondary: string; role: ClanRole }>();
    return c
        ? { id: c.id, name: c.name, tag: c.tag, role: c.role, emblem: { shape: c.emblem_shape, primary: c.emblem_primary, secondary: c.emblem_secondary } }
        : null;
}

/** `player` with its clan tag (the field only when the player has a clan). */
export function withClanTag<T extends { id: string }>(player: T, tags: Map<string, string>): T & { clan_tag?: string } {
    const t = tags.get(player.id);
    return t ? { ...player, clan_tag: t } : player;
}

/**
 * Account deletion (players.ts): the statements to run after the player's row is deleted
 * (their membership goes with it): a new chief if they led the clan, the clan gone if empty.
 */
export async function clanAfterAccountDeletion(db: D1Database, playerId: string): Promise<D1PreparedStatement[]> {
    const m = await db.prepare("SELECT clan_id FROM clan_members WHERE player_id = ?1").bind(playerId).first<{ clan_id: string }>();
    if (!m) return [];
    return [ensureLeader(db, m.clan_id), dropIfEmpty(db, m.clan_id), ...clanPointsStatements(db, m.clan_id)];
}

// ------------------------------------------------------------- validation

interface ClanFields {
    name?: string;
    tag?: string;
    motto?: string;
    emblem?: { shape: string; primary: string; secondary: string };
    join_policy?: (typeof JOIN_POLICIES)[number];
}

function readClanFields(body: Record<string, unknown>, required: boolean): ClanFields {
    const v = new Validator(body, ["name", "tag", "motto", "emblem", "join_policy"]);
    const out: ClanFields = {};
    if (body.name !== undefined || required) {
        if (typeof body.name !== "string") v.fail("name", "obligatoire");
        else {
            const name = cleanLine(body.name.slice(0, 200));
            const err = clanNameError(name);
            if (err) v.fail("name", err);
            else out.name = name;
        }
    }
    if (body.tag !== undefined || required) {
        if (typeof body.tag !== "string") v.fail("tag", "obligatoire");
        else {
            const err = clanTagError(body.tag);
            if (err) v.fail("tag", err);
            else out.tag = body.tag;
        }
    }
    if (body.motto !== undefined) {
        if (typeof body.motto !== "string" || body.motto.length > MOTTO_MAX * 2) v.fail("motto", `${MOTTO_MAX} caractères au plus`);
        else {
            const motto = cleanLine(body.motto);
            if (motto.length > MOTTO_MAX) v.fail("motto", `${MOTTO_MAX} caractères au plus`);
            else out.motto = motto;
        }
    }
    if (body.emblem !== undefined || required) {
        const e = body.emblem as Record<string, unknown> | undefined;
        if (typeof e !== "object" || e === null || Array.isArray(e)) v.fail("emblem", "objet { shape, primary, secondary } attendu");
        else {
            const ev = new Validator(e, ["shape", "primary", "secondary"]);
            const shape = ev.oneOf("shape", EMBLEM_SHAPES);
            const primary = ev.str("primary", { min: 7, max: 7, pattern: COLOR_RE });
            const secondary = ev.str("secondary", { min: 7, max: 7, pattern: COLOR_RE });
            for (const f of ev.errors) v.fail(`emblem.${f.field}`, f.message);
            if (shape && primary && secondary) out.emblem = { shape, primary: primary.toLowerCase(), secondary: secondary.toLowerCase() };
        }
    }
    if (body.join_policy !== undefined || required) out.join_policy = v.oneOf("join_policy", JOIN_POLICIES);
    v.done();
    return out;
}

// --------------------------------------------------------------- read routes

/**
 * GET /clans/me: the player's clan (with their role), the invitations they received and the
 * applications they sent (what the game keeps for offline display).
 */
export async function myClan(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const db = ctx.env.DB;
    const [mine, requests] = await db.batch([
        db.prepare(`SELECT c.*, ${MEMBERS} AS members, m.role AS my_role FROM clan_members m JOIN clans c ON c.id = m.clan_id WHERE m.player_id = ?1`).bind(me),
        db
            .prepare(
                `SELECT c.*, ${MEMBERS} AS members, q.kind AS q_kind, q.message AS q_message, q.created_at AS q_at, q.by_player AS q_by, b.username AS q_by_name
                 FROM clan_requests q JOIN clans c ON c.id = q.clan_id LEFT JOIN players b ON b.id = q.by_player
                 WHERE q.player_id = ?1 ORDER BY q.created_at DESC LIMIT 100`,
            )
            .bind(me),
    ]);
    const m = (mine!.results as (ClanRow & { my_role: ClanRole })[])[0];
    const reqs = requests!.results as (ClanRow & { q_kind: string; q_message: string | null; q_at: string; q_by: string | null; q_by_name: string | null })[];
    return json(ctx, {
        clan: m ? { ...clanJson(m), role: m.my_role } : null,
        invitations: reqs
            .filter((r) => r.q_kind === "invite")
            .map((r) => ({ clan: clanJson(r), by: r.q_by ? { id: r.q_by, username: r.q_by_name } : null, created_at: r.q_at })),
        applications: reqs.filter((r) => r.q_kind === "apply").map((r) => ({ clan: clanJson(r), message: r.q_message ?? "", created_at: r.q_at })),
    });
}

/**
 * GET /clans?q=&limit=&offset=: the clans, best points first; `q` (1 to 32 characters) finds
 * a tag exactly (first) or a part of a name (accents and case ignored).
 */
export async function searchClans(ctx: Ctx): Promise<Response> {
    const page = paging(ctx.url, 20, 50);
    const raw = ctx.url.searchParams.get("q");
    const q = raw === null ? "" : cleanLine(raw);
    if (q.length > 32) throw Errors.validation([{ field: "q", message: "32 caractères au plus" }]);
    const db = ctx.env.DB;
    const rows = q
        ? await db
              .prepare(
                  `SELECT c.*, ${MEMBERS} AS members FROM clans c WHERE c.tag_normalized = ?1 OR instr(c.name_normalized, ?2) > 0
                   ORDER BY c.tag_normalized = ?1 DESC, c.points DESC, c.points_at, c.id LIMIT ?3 OFFSET ?4`,
              )
              .bind(q.toUpperCase(), normalizeClanName(q), page.limit + 1, page.offset)
              .all<ClanRow>()
        : await db
              .prepare(`SELECT c.*, ${MEMBERS} AS members FROM clans c ORDER BY c.points DESC, c.points_at, c.id LIMIT ?1 OFFSET ?2`)
              .bind(page.limit + 1, page.offset)
              .all<ClanRow>();
    return json(ctx, { clans: rows.results.slice(0, page.limit).map(clanJson), has_more: rows.results.length > page.limit, offset: page.offset });
}

/** Clans ranked before `c` (points, then who reached them first): its rank minus one. */
const RANK_SQL = `SELECT COUNT(*) AS n FROM clans o WHERE o.points > ?1 OR (o.points = ?1 AND (o.points_at < ?2 OR (o.points_at = ?2 AND o.id < ?3)))`;

/**
 * GET /clans/ranking?limit=&offset=: the ranking of the clans (the sum of each clan's best
 * verified result on every EXTRÊME level), with the viewer's clan.
 */
export async function clanRanking(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const page = paging(ctx.url, 25, 100);
    const db = ctx.env.DB;
    const [rows, total, mine] = await db.batch([
        db
            .prepare(`SELECT c.*, ${MEMBERS} AS members FROM clans c WHERE c.points > 0 ORDER BY c.points DESC, c.points_at, c.id LIMIT ?1 OFFSET ?2`)
            .bind(page.limit + 1, page.offset),
        db.prepare("SELECT COUNT(*) AS n FROM clans WHERE points > 0"),
        db.prepare(`SELECT c.*, ${MEMBERS} AS members FROM clan_members m JOIN clans c ON c.id = m.clan_id WHERE m.player_id = ?1`).bind(viewer?.player.id ?? ""),
    ]);
    const list = rows!.results as ClanRow[];
    const my = (mine!.results as ClanRow[])[0];
    let me = null;
    if (my) {
        const before = my.points > 0 ? await db.prepare(RANK_SQL).bind(my.points, my.points_at, my.id).first<{ n: number }>() : null;
        me = { rank: before ? before.n + 1 : null, clan: clanJson(my) };
    }
    return json(ctx, {
        ranking: list.slice(0, page.limit).map((c, i) => ({ rank: page.offset + i + 1, clan: clanJson(c) })),
        total: (total!.results[0] as { n: number } | undefined)?.n ?? 0,
        has_more: list.length > page.limit,
        offset: page.offset,
        me,
    });
}

/**
 * GET /clans/{clanId}: the clan's page — members and roles (presence only for the clan's own
 * members, and only as each player shares it: a hidden presence reads « offline »; 2.72: with
 * `members_online`, the « X » of « X/Y en ligne »), the best team per EXTRÊME level (Solo, Duo
 * and Trio: `mode` and `team_size`; the rank on the level's race for the Solo one), the
 * clan's rank, the pending requests (chief and officers only), the discussion (members only).
 */
export async function getClan(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const id = clanIdParam(ctx);
    const me = viewer?.player.id ?? "";
    const db = ctx.env.DB;
    const res = await db.batch([
        db.prepare(`SELECT c.*, ${MEMBERS} AS members FROM clans c WHERE c.id = ?1`).bind(id),
        db
            .prepare(
                `SELECT m.role, m.joined_at, p.id, p.username, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility,
                        p.presence_state, p.presence_at, p.presence_visible, p.profile_visibility
                 FROM clan_members m JOIN players p ON p.id = m.player_id WHERE m.clan_id = ?1
                 ORDER BY ${ROLE_ORDER}, m.joined_at, p.id`,
            )
            .bind(id),
        // Every official EXTRÊME level, with the clan's bests (one row per team size).
        db
            .prepare(
                `SELECT l.id AS level_id, l.name AS title, b.team_size, b.score, b.finished, b.players, b.achieved_at,
                        CASE WHEN b.level_id IS NULL THEN NULL ELSE ${COUNTED_BEST} END AS counted
                 FROM levels l JOIN level_versions v ON v.level_id = l.id AND v.content_hash = l.content_hash AND v.race IS NOT NULL
                 LEFT JOIN clan_level_bests b ON b.clan_id = ?1 AND b.level_id = l.id
                 ORDER BY l.planet_id, l.level_order, l.id, b.team_size`,
            )
            .bind(id),
        // The rank of each Solo best on its level's race (players with a better best, plus one).
        db
            .prepare(
                `WITH ${EXTREME_BOARDS}
                 SELECT b.level_id, 1 + (SELECT COUNT(DISTINCT r.player_id) FROM ext e JOIN race_runs r ON r.board = e.board
                     WHERE e.level_id = b.level_id AND r.status = 'VERIFIED'
                       AND (r.score > b.score OR (r.score = b.score AND r.verified_at < b.achieved_at))) AS rank
                 FROM clan_level_bests b WHERE b.clan_id = ?1 AND b.team_size = 1`,
            )
            .bind(id),
        // Each member's best EXTRÊME result (the « meilleur » of the list).
        db
            .prepare(
                `WITH ${EXTREME_BOARDS},
                 best AS (SELECT r.player_id, e.level_id, r.score, r.finished,
                                 ROW_NUMBER() OVER (PARTITION BY r.player_id ORDER BY r.score DESC, r.verified_at, r.id) AS n
                            FROM clan_members m JOIN race_runs r ON r.player_id = m.player_id AND r.status = 'VERIFIED'
                            JOIN ext e ON e.board = r.board WHERE m.clan_id = ?1)
                 SELECT player_id, level_id, score, finished FROM best WHERE n = 1`,
            )
            .bind(id),
        // The pending requests: for the chief and the officers only (checked in the query).
        db
            .prepare(
                `SELECT q.kind, q.message, q.created_at, q.by_player, b.username AS by_username,
                        p.id, p.username, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility
                 FROM clan_requests q JOIN players p ON p.id = q.player_id LEFT JOIN players b ON b.id = q.by_player
                 WHERE q.clan_id = ?1 AND EXISTS (SELECT 1 FROM clan_members a WHERE a.clan_id = ?1 AND a.player_id = ?2 AND a.role IN ('leader', 'officer'))
                 ORDER BY q.kind, q.created_at LIMIT ${MAX_PENDING}`,
            )
            .bind(id, me),
        // The viewer's own request, and the discussion (members only).
        db.prepare("SELECT kind, message, created_at FROM clan_requests WHERE clan_id = ?1 AND player_id = ?2").bind(id, me),
        db
            .prepare("SELECT c.id FROM conversations c WHERE c.clan_id = ?1 AND EXISTS (SELECT 1 FROM clan_members m WHERE m.clan_id = ?1 AND m.player_id = ?2)")
            .bind(id, me),
    ]);
    const clan = (res[0]!.results as ClanRow[])[0];
    if (!clan) throw CLAN_NOT_FOUND();
    const members = res[1]!.results as MemberRow[];
    const myRole = members.find((m) => m.id === me)?.role ?? null;
    const titles = new Map<string, string>();
    for (const l of res[2]!.results as { level_id: string; title: string }[]) titles.set(l.level_id, l.title);
    const bestOf = new Map<string, { level_id: string; score: number; finished: number }>();
    for (const b of res[4]!.results as { player_id: string; level_id: string; score: number; finished: number }[]) bestOf.set(b.player_id, b);
    const byId = new Map(members.map((m) => [m.id, m]));
    const tag = clan.tag;
    const ranks = new Map<string, number>();
    for (const r of res[3]!.results as { level_id: string; rank: number }[]) ranks.set(r.level_id, r.rank);
    const levels: { level_id: string; title: string; best: unknown; teams: unknown[] }[] = [];
    for (const r of res[2]!.results as {
        level_id: string;
        title: string;
        team_size: number | null;
        score: number | null;
        finished: number | null;
        players: string | null;
        achieved_at: string | null;
        counted: number | null;
    }[]) {
        let level = levels[levels.length - 1];
        if (!level || level.level_id !== r.level_id) {
            level = { level_id: r.level_id, title: r.title, best: null, teams: [] };
            levels.push(level);
        }
        if (r.team_size === null || r.score === null) continue;
        let ids: string[] = [];
        try {
            ids = (JSON.parse(r.players ?? "[]") as unknown[]).filter((x): x is string => typeof x === "string");
        } catch {
            ids = [];
        }
        const team = {
            mode: r.team_size === 1 ? "solo" : r.team_size === 2 ? "duo" : "trio",
            team_size: r.team_size,
            score: r.score,
            finished: r.finished === 1,
            players: ids.map((pid) => {
                const m = byId.get(pid);
                return m ? { id: pid, username: m.username, clan_tag: tag } : { id: pid, username: null };
            }),
            at: r.achieved_at,
            rank: r.team_size === 1 ? (ranks.get(r.level_id) ?? null) : null,
        };
        level.teams.push(team);
        if (r.counted === 1) level.best = team;
    }
    let rank: number | null = null;
    if (clan.points > 0) {
        const before = await db.prepare(RANK_SQL).bind(clan.points, clan.points_at, clan.id).first<{ n: number }>();
        rank = (before?.n ?? 0) + 1;
    }
    const mine = (res[6]!.results as { kind: string; message: string | null; created_at: string }[])[0];
    // Presence: to the clan's members only, as each player shares it (2.72: a hidden presence
    // reads and counts « offline »), from the heartbeat of the game (never what a client says).
    const now = Date.now();
    // 2.75: an administrator (moderation) sees every member's real presence, the hidden ones marked.
    const admin = isAdminViewer(viewer);
    const adminView = admin ? members.map((m) => adminPresenceOf(m, now)) : null;
    const presence = adminView ? adminView.map((a) => a.presence) : myRole ? members.map((m) => listPresenceOf(m, now)) : null;
    return json(ctx, {
        clan: { ...clanJson(clan), rank },
        ...(presence ? { members_online: countOnline(presence) } : {}),
        members: members.map((m, i) => {
            const best = bestOf.get(m.id);
            return {
                player: { ...playerSummary(m), clan_tag: tag },
                role: m.role,
                joined_at: m.joined_at,
                ...(presence ? { presence: presence[i] } : {}),
                ...(adminView ? { presence_hidden: adminView[i]!.presence_hidden, profile_hidden: m.profile_visibility === "private" } : {}),
                best: best ? { level_id: best.level_id, title: titles.get(best.level_id) ?? best.level_id, score: best.score, finished: best.finished === 1 } : null,
            };
        }),
        levels,
        me: {
            role: myRole,
            request: mine ? { kind: mine.kind, message: mine.message ?? "", created_at: mine.created_at } : null,
        },
        requests: (res[5]!.results as (PlayerRow & { kind: string; message: string | null; created_at: string; by_player: string | null; by_username: string | null })[]).map(
            (q) => ({
                kind: q.kind,
                player: playerSummary(q),
                message: q.message ?? "",
                created_at: q.created_at,
                by: q.by_player ? { id: q.by_player, username: q.by_username } : null,
            }),
        ),
        conversation_id: (res[7]!.results as { id: string }[])[0]?.id ?? null,
    });
}

// -------------------------------------------------------------- write routes

/** POST /clans {name, tag, motto?, emblem, join_policy}: the creator is its chief; its discussion is created with it. */
export async function createClan(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.clanCreate, "player:" + me);
    const f = readClanFields(await readJsonObject(ctx), true);
    const db = ctx.env.DB;
    const id = crypto.randomUUID();
    const conv = crypto.randomUUID();
    const now = nowIso();
    try {
        await db.batch([
            db
                .prepare(
                    `INSERT INTO clans (id, name, name_normalized, tag, tag_normalized, motto, emblem_shape, emblem_primary, emblem_secondary,
                       join_policy, created_by, created_at, updated_at)
                     VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?12)`,
                )
                .bind(id, f.name!, normalizeClanName(f.name!), f.tag!, f.tag!.toUpperCase(), f.motto ?? "", f.emblem!.shape, f.emblem!.primary, f.emblem!.secondary, f.join_policy!, me, now),
            // A player already in a clan: the primary key refuses it and the batch is rolled back.
            db.prepare("INSERT INTO clan_members (player_id, clan_id, role, joined_at) VALUES (?1, ?2, 'leader', ?3)").bind(me, id, now),
            db.prepare("INSERT INTO conversations (id, kind, name, created_by, created_at, clan_id) VALUES (?1, 'group', ?2, ?3, ?4, ?5)").bind(conv, f.name!, me, now, id),
            chatJoin(db, id, me),
            db.prepare("DELETE FROM clan_requests WHERE player_id = ?1").bind(me),
            ...clanPointsStatements(db, id),
        ]);
    } catch (e) {
        const k = constraintOf(e);
        if (k === "member") throw Errors.conflict("ALREADY_IN_CLAN", "Vous êtes déjà dans un clan : quittez-le d'abord.");
        if (k === "tag") throw Errors.conflict("TAG_TAKEN", "Ce tag est déjà pris : choisissez-en un autre.");
        throw e;
    }
    await audit(ctx, me, "clan_created", { clan_id: id, tag: f.tag });
    return json(ctx, { clan: { ...clanJson(await loadClan(ctx, id)), role: "leader" as ClanRole }, conversation_id: conv }, { status: 201 });
}

/** PATCH /clans/{clanId} {name?, tag?, motto?, emblem?, join_policy?}: the chief only. */
export async function updateClan(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.clanWrite, "player:" + me);
    const id = clanIdParam(ctx);
    const f = readClanFields(await readJsonObject(ctx), false);
    await loadClan(ctx, id);
    const sets: string[] = [];
    const binds: unknown[] = [id, me, nowIso()];
    const set = (col: string, val: unknown) => {
        binds.push(val);
        sets.push(`${col} = ?${binds.length}`);
    };
    if (f.name !== undefined) {
        set("name", f.name);
        set("name_normalized", normalizeClanName(f.name));
    }
    if (f.tag !== undefined) {
        set("tag", f.tag);
        set("tag_normalized", f.tag.toUpperCase());
    }
    if (f.motto !== undefined) set("motto", f.motto);
    if (f.emblem !== undefined) {
        set("emblem_shape", f.emblem.shape);
        set("emblem_primary", f.emblem.primary);
        set("emblem_secondary", f.emblem.secondary);
    }
    if (f.join_policy !== undefined) set("join_policy", f.join_policy);
    if (sets.length === 0) throw Errors.validation([{ field: "body", message: "rien à modifier" }]);
    const db = ctx.env.DB;
    let changed = 0;
    try {
        const r = await db.batch([
            db
                .prepare(
                    `UPDATE clans SET ${sets.join(", ")}, updated_at = ?3 WHERE id = ?1
                     AND EXISTS (SELECT 1 FROM clan_members WHERE clan_id = ?1 AND player_id = ?2 AND role = 'leader')`,
                )
                .bind(...binds),
            db.prepare("UPDATE conversations SET name = (SELECT name FROM clans WHERE id = ?1) WHERE clan_id = ?1").bind(id),
        ]);
        changed = r[0]!.meta.changes;
    } catch (e) {
        if (constraintOf(e) === "tag") throw Errors.conflict("TAG_TAKEN", "Ce tag est déjà pris : choisissez-en un autre.");
        throw e;
    }
    if (!changed) throw LEADER_ONLY();
    return json(ctx, { clan: { ...clanJson(await loadClan(ctx, id)), role: "leader" as ClanRole } });
}

/** DELETE /clans/{clanId} {confirm: true}: the chief dissolves the clan (its discussion, requests and points with it). */
export async function dissolveClan(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.clanWrite, "player:" + me);
    const id = clanIdParam(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["confirm"]);
    if (v.bool("confirm") !== true) v.fail("confirm", "la dissolution doit être confirmée");
    v.done();
    const clan = await loadClan(ctx, id);
    const members = await ctx.env.DB.prepare("SELECT player_id FROM clan_members WHERE clan_id = ?1").bind(id).all<{ player_id: string }>();
    const r = await ctx.env.DB.prepare(
        "DELETE FROM clans WHERE id = ?1 AND EXISTS (SELECT 1 FROM clan_members WHERE clan_id = ?1 AND player_id = ?2 AND role = 'leader')",
    )
        .bind(id, me)
        .run();
    if (!r.meta.changes) throw LEADER_ONLY();
    await audit(ctx, me, "clan_dissolved", { clan_id: id, tag: clan.tag, members: members.results.length });
    await notifyClan(
        ctx,
        members.results.map((m) => m.player_id),
        me,
        { event: "dissolved", ...clanRef(clan) },
    );
    return json(ctx, { dissolved: true });
}

/**
 * POST /clans/{clanId}/join {message?}: an open clan, or an invitation received: the player
 * joins at once (if the clan has room). A clan « sur candidature »: an application with its
 * short message (sent again, the message is replaced). A clan « sur invitation »: refused.
 */
export async function joinClan(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.clanApply, "player:" + me);
    const id = clanIdParam(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["message"]);
    let message = "";
    if (body.message !== undefined && body.message !== null) {
        if (typeof body.message !== "string" || body.message.length > MESSAGE_MAX * 2) v.fail("message", `${MESSAGE_MAX} caractères au plus`);
        else {
            message = cleanLine(body.message);
            if (message.length > MESSAGE_MAX) v.fail("message", `${MESSAGE_MAX} caractères au plus`);
        }
    }
    v.done();
    const clan = await loadClan(ctx, id);
    const db = ctx.env.DB;
    const current = await db.prepare("SELECT clan_id FROM clan_members WHERE player_id = ?1").bind(me).first<{ clan_id: string }>();
    if (current) throw Errors.conflict("ALREADY_IN_CLAN", current.clan_id === id ? "Vous êtes déjà dans ce clan." : "Vous êtes déjà dans un clan : quittez-le d'abord.");
    const invited = await db.prepare("SELECT by_player FROM clan_requests WHERE clan_id = ?1 AND player_id = ?2 AND kind = 'invite'").bind(id, me).first<{ by_player: string | null }>();
    if (invited || clan.join_policy === "open") {
        await runJoin(
            ctx,
            id,
            joinStatements(db, id, me, nowIso(), me, `${IS_OPEN} OR ${IS_INVITED}`),
            Errors.forbidden("INVITATION_REQUIRED", "Ce clan ne recrute plus librement : il faut une invitation ou une candidature."),
            true,
        );
        return json(ctx, { joined: true, clan: { ...clanJson(await loadClan(ctx, id)), role: "member" as ClanRole } });
    }
    if (clan.join_policy === "invite") throw Errors.forbidden("INVITE_ONLY", "Ce clan recrute seulement sur invitation.");
    if (clan.members >= MAX_CLAN_MEMBERS) throw CLAN_FULL();
    const counts = await db
        .prepare(
            `SELECT (SELECT COUNT(*) FROM clan_requests WHERE player_id = ?1 AND kind = 'apply' AND clan_id <> ?2) AS mine,
                    (SELECT COUNT(*) FROM clan_requests WHERE clan_id = ?2) AS clan,
                    (SELECT 1 FROM clan_requests WHERE clan_id = ?2 AND player_id = ?1) AS again`,
        )
        .bind(me, id)
        .first<{ mine: number; clan: number; again: number | null }>();
    if ((counts?.mine ?? 0) >= MAX_APPLICATIONS)
        throw Errors.conflict("TOO_MANY_APPLICATIONS", `Vous avez déjà ${MAX_APPLICATIONS} candidatures en attente : retirez-en une d'abord.`);
    if (!counts?.again && (counts?.clan ?? 0) >= MAX_PENDING) throw Errors.conflict("CLAN_REQUESTS_FULL", "Ce clan a trop de demandes en attente : réessayez plus tard.");
    const now = nowIso();
    await db
        .prepare(
            `INSERT INTO clan_requests (clan_id, player_id, kind, message, created_at) VALUES (?1, ?2, 'apply', ?3, ?4)
             ON CONFLICT (clan_id, player_id) DO UPDATE SET message = excluded.message WHERE clan_requests.kind = 'apply'`,
        )
        .bind(id, me, message, now)
        .run();
    if (!counts?.again) await notifyClan(ctx, await managersOf(ctx, id), me, { event: "apply", ...clanRef(clan) });
    return json(ctx, { joined: false, application: { clan_id: id, message, created_at: now } }, { status: 201 });
}

/** DELETE /clans/{clanId}/join: the player withdraws their application or declines the invitation. */
export async function withdrawRequest(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const id = clanIdParam(ctx);
    const r = await ctx.env.DB.prepare("DELETE FROM clan_requests WHERE clan_id = ?1 AND player_id = ?2").bind(id, auth.player.id).run();
    if (!r.meta.changes) throw Errors.notFound("REQUEST_NOT_FOUND", "Aucune candidature ni invitation pour ce clan.");
    return json(ctx, { withdrawn: true });
}

/**
 * POST /clans/{clanId}/invites {username | player_id}: the chief or an officer invites a
 * player (who joins by accepting). A player who already applied joins at once.
 */
export async function invitePlayer(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.clanApply, "player:" + me);
    const id = clanIdParam(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["username", "player_id"]);
    const username = v.str("username", { min: 3, max: 20, pattern: USERNAME_RE, optional: true });
    const playerId = v.str("player_id", { min: 36, max: 36, pattern: ID_RE, optional: true });
    if (!username && !playerId) v.fail("username", "pseudonyme ou identifiant obligatoire");
    v.done();
    const clan = await loadClan(ctx, id);
    const db = ctx.env.DB;
    const target = username
        ? await db.prepare("SELECT id, username, account_status FROM players WHERE username_normalized = ?1").bind(normalizeUsername(username)).first<PlayerRow>()
        : await db.prepare("SELECT id, username, account_status FROM players WHERE id = ?1").bind(playerId).first<PlayerRow>();
    if (!target || target.account_status !== "active") throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    const role = await roleIn(ctx, id, me);
    if (role !== "leader" && role !== "officer") throw MANAGERS_ONLY();
    if (target.id === me) throw Errors.validation([{ field: "player_id", message: "vous êtes déjà dans ce clan" }]);
    const state = await db
        .prepare(
            `SELECT (SELECT clan_id FROM clan_members WHERE player_id = ?2) AS in_clan,
                    (SELECT kind FROM clan_requests WHERE clan_id = ?1 AND player_id = ?2) AS pending,
                    (SELECT COUNT(*) FROM clan_requests WHERE clan_id = ?1) AS requests,
                    EXISTS (SELECT 1 FROM player_blocks WHERE (player_id = ?2 AND blocked_id = ?3) OR (player_id = ?3 AND blocked_id = ?2)) AS blocked`,
        )
        .bind(id, target.id, me)
        .first<{ in_clan: string | null; pending: string | null; requests: number; blocked: number }>();
    if (state?.blocked) throw Errors.forbidden("BLOCKED", "Ce joueur ne peut pas être invité.");
    if (state?.in_clan) throw Errors.conflict("ALREADY_IN_CLAN", state.in_clan === id ? "Ce joueur est déjà dans le clan." : "Ce joueur est déjà dans un clan.");
    if (clan.members >= MAX_CLAN_MEMBERS) throw CLAN_FULL();
    const now = nowIso();
    if (state?.pending === "apply") {
        // An invitation answers the application: the player joins.
        await runJoin(ctx, id, joinStatements(db, id, target.id, now, me, `${HAS_APPLIED} AND ${ACTOR_MANAGES}`), MANAGERS_ONLY());
        await notifyClan(ctx, [target.id], me, { event: "accepted", ...clanRef(clan) });
        return json(ctx, { joined: true, player: { id: target.id, username: target.username } });
    }
    if (state?.pending === "invite") return json(ctx, { invited: true, already: true, player: { id: target.id, username: target.username } });
    if ((state?.requests ?? 0) >= MAX_PENDING) throw Errors.conflict("CLAN_REQUESTS_FULL", "Ce clan a trop de demandes en attente.");
    const r = await db
        .prepare(
            `INSERT OR IGNORE INTO clan_requests (clan_id, player_id, kind, by_player, created_at)
             SELECT ?1, ?2, 'invite', ?4, ?3 WHERE ${ACTOR_MANAGES} AND NOT EXISTS (SELECT 1 FROM clan_members WHERE player_id = ?2)`,
        )
        .bind(id, target.id, now, me)
        .run();
    if (!r.meta.changes) throw MANAGERS_ONLY();
    await notifyClan(ctx, [target.id], me, { event: "invite", ...clanRef(clan) });
    return json(ctx, { invited: true, already: false, player: { id: target.id, username: target.username } }, { status: 201 });
}

/** DELETE /clans/{clanId}/invites/{playerId}: the chief or an officer withdraws an invitation. */
export async function cancelInvite(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.clanWrite, "player:" + me);
    const id = clanIdParam(ctx);
    const pid = playerIdParam(ctx);
    const r = await ctx.env.DB.prepare(`DELETE FROM clan_requests WHERE clan_id = ?1 AND player_id = ?2 AND kind = 'invite' AND ${ACTOR_MANAGES}`)
        .bind(id, pid, null, me)
        .run();
    if (r.meta.changes) return json(ctx, { cancelled: true });
    const role = await roleIn(ctx, id, me);
    if (role !== "leader" && role !== "officer") throw MANAGERS_ONLY();
    throw Errors.notFound("REQUEST_NOT_FOUND", "Aucune invitation pour ce joueur.");
}

/** POST /clans/{clanId}/requests/{playerId} {accept}: the chief or an officer answers an application. */
export async function answerApplication(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.clanWrite, "player:" + me);
    const id = clanIdParam(ctx);
    const pid = playerIdParam(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["accept"]);
    const accept = v.bool("accept");
    v.done();
    const clan = await loadClan(ctx, id);
    const db = ctx.env.DB;
    if (!accept) {
        const r = await db.prepare(`DELETE FROM clan_requests WHERE clan_id = ?1 AND player_id = ?2 AND kind = 'apply' AND ${ACTOR_MANAGES}`).bind(id, pid, null, me).run();
        if (r.meta.changes) return json(ctx, { accepted: false });
        const role = await roleIn(ctx, id, me);
        if (role !== "leader" && role !== "officer") throw MANAGERS_ONLY();
        throw Errors.notFound("REQUEST_NOT_FOUND", "Aucune candidature de ce joueur.");
    }
    const role = await roleIn(ctx, id, me);
    if (role !== "leader" && role !== "officer") throw MANAGERS_ONLY();
    const applied = await db.prepare("SELECT 1 AS x FROM clan_requests WHERE clan_id = ?1 AND player_id = ?2 AND kind = 'apply'").bind(id, pid).first();
    if (!applied) throw Errors.notFound("REQUEST_NOT_FOUND", "Aucune candidature de ce joueur.");
    try {
        await runJoin(ctx, id, joinStatements(db, id, pid, nowIso(), me, `${HAS_APPLIED} AND ${ACTOR_MANAGES}`), Errors.conflict("REQUEST_GONE", "La candidature a été retirée entre-temps."));
    } catch (e) {
        // Already in another clan: the application no longer means anything.
        if (e instanceof ApiError && e.code === "ALREADY_IN_CLAN") await db.prepare("DELETE FROM clan_requests WHERE clan_id = ?1 AND player_id = ?2").bind(id, pid).run();
        throw e;
    }
    await notifyClan(ctx, [pid], me, { event: "accepted", ...clanRef(clan) });
    return json(ctx, { accepted: true });
}

/**
 * PATCH /clans/{clanId}/members/{playerId} {role: officer|member}: the chief names or removes
 * an officer (never their own role: the chief hands it over with /transfer).
 */
export async function setMemberRole(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.clanWrite, "player:" + me);
    const id = clanIdParam(ctx);
    const pid = playerIdParam(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["role"]);
    const role = v.oneOf("role", ["officer", "member"] as const);
    v.done();
    const r = await ctx.env.DB.prepare(
        `UPDATE clan_members SET role = ?3 WHERE clan_id = ?1 AND player_id = ?2 AND role IN ('officer', 'member')
         AND EXISTS (SELECT 1 FROM clan_members a WHERE a.clan_id = ?1 AND a.player_id = ?4 AND a.role = 'leader')`,
    )
        .bind(id, pid, role!, me)
        .run();
    if (r.meta.changes) return json(ctx, { player_id: pid, role });
    await loadClan(ctx, id);
    if ((await roleIn(ctx, id, me)) !== "leader") throw LEADER_ONLY();
    const target = await roleIn(ctx, id, pid);
    if (target === "leader") throw Errors.conflict("IS_LEADER", "Le chef garde son rôle : transmettez-le d'abord.");
    throw Errors.notFound("MEMBER_NOT_FOUND", "Ce joueur n'est pas dans le clan.");
}

/** POST /clans/{clanId}/transfer {player_id}: the chief hands the role to a member (and becomes an officer). */
export async function transferLeadership(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.clanWrite, "player:" + me);
    const id = clanIdParam(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["player_id"]);
    const pid = v.str("player_id", { min: 36, max: 36, pattern: ID_RE });
    v.done();
    if (pid === me) throw Errors.validation([{ field: "player_id", message: "vous êtes déjà le chef" }]);
    const clan = await loadClan(ctx, id);
    if ((await roleIn(ctx, id, me)) !== "leader") throw LEADER_ONLY();
    if (!(await roleIn(ctx, id, pid!))) throw Errors.notFound("MEMBER_NOT_FOUND", "Ce joueur n'est pas dans le clan.");
    const db = ctx.env.DB;
    // One transaction: the chief steps down only if the new one is still a member, and the new
    // one is named only if the clan has no chief at that moment (two transfers at once: one wins).
    await db.batch([
        db
            .prepare(
                `UPDATE clan_members SET role = 'officer' WHERE clan_id = ?1 AND player_id = ?2 AND role = 'leader'
                 AND EXISTS (SELECT 1 FROM clan_members WHERE clan_id = ?1 AND player_id = ?3)`,
            )
            .bind(id, me, pid!),
        db
            .prepare(
                `UPDATE clan_members SET role = 'leader' WHERE clan_id = ?1 AND player_id = ?2
                 AND NOT EXISTS (SELECT 1 FROM clan_members WHERE clan_id = ?1 AND role = 'leader')`,
            )
            .bind(id, pid!),
        ensureLeader(db, id),
    ]);
    if ((await roleIn(ctx, id, pid!)) !== "leader") throw Errors.conflict("TRANSFER_FAILED", "Le clan a changé entre-temps : actualisez la page.");
    await audit(ctx, me, "clan_leader_transferred", { clan_id: id, to: pid });
    await notifyClan(ctx, [pid!], me, { event: "leader", ...clanRef(clan) });
    return json(ctx, { leader: pid, role: "officer" as ClanRole });
}

/**
 * DELETE /clans/{clanId}/members/{playerId}: leave the clan (oneself) or exclude a member.
 * The chief excludes officers and members, an officer excludes members only; nobody excludes
 * the chief. A chief who leaves hands the role to the oldest officer (else the oldest
 * member); the last member who leaves dissolves the clan.
 */
export async function removeMember(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.clanWrite, "player:" + me);
    const id = clanIdParam(ctx);
    const pid = playerIdParam(ctx);
    const clan = await loadClan(ctx, id);
    const db = ctx.env.DB;
    const self = pid === me;
    const removal = self
        ? db.prepare("DELETE FROM clan_members WHERE clan_id = ?1 AND player_id = ?2").bind(id, me)
        : db
              .prepare(
                  `DELETE FROM clan_members WHERE clan_id = ?1 AND player_id = ?2 AND role <> 'leader'
                   AND EXISTS (SELECT 1 FROM clan_members a WHERE a.clan_id = ?1 AND a.player_id = ?3
                               AND (a.role = 'leader' OR (a.role = 'officer' AND clan_members.role = 'member')))`,
              )
              .bind(id, pid, me);
    const r = await db.batch([
        removal,
        chatLeave(db, id, pid),
        ensureLeader(db, id),
        dropIfEmpty(db, id),
        ...clanPointsStatements(db, id),
        // Their registrations to the raids to come go with them (nothing when the removal was refused).
        ...raidLeaveStatements(db, id, pid),
    ]);
    if (!r[0]!.meta.changes) {
        const mine = await roleIn(ctx, id, me);
        const target = await roleIn(ctx, id, pid);
        if (!target) throw Errors.notFound("MEMBER_NOT_FOUND", self ? "Vous n'êtes pas dans ce clan." : "Ce joueur n'est pas dans le clan.");
        if (target === "leader") throw Errors.forbidden("CANNOT_EXCLUDE_LEADER", "Personne ne peut exclure le chef du clan.");
        if (mine === "officer" && target === "officer") throw Errors.forbidden("CANNOT_EXCLUDE_OFFICER", "Un officier ne peut pas exclure un autre officier.");
        throw MANAGERS_ONLY();
    }
    const dissolved = r[3]!.meta.changes > 0;
    if (!self) {
        await audit(ctx, me, "clan_member_excluded", { clan_id: id, player_id: pid });
        await notifyClan(ctx, [pid], me, { event: "kicked", ...clanRef(clan) });
    }
    let leader: { id: string; username: string } | null = null;
    if (!dissolved) {
        leader = await db
            .prepare("SELECT p.id, p.username FROM clan_members m JOIN players p ON p.id = m.player_id WHERE m.clan_id = ?1 AND m.role = 'leader'")
            .bind(id)
            .first<{ id: string; username: string }>();
        // A chief who left: the new one is told.
        if (self && r[2]!.meta.changes && leader) await notifyClan(ctx, [leader.id], me, { event: "leader", ...clanRef(clan) });
    }
    return json(ctx, { removed: true, left: self, dissolved, leader });
}

// -------------------------------------------------------------- administration

/** What the Journal keeps of a clan (adminRoles.ts reads it back). */
function journalClan(c: Pick<ClanRow, "id" | "name" | "tag">, extra: Record<string, unknown> = {}) {
    return { id: c.id, name: c.name, tag: c.tag, ...extra };
}

/** GET /admin/clans?q=&limit=&offset=: the clans with their chief, newest first (search like /clans). */
export async function adminListClans(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const page = paging(ctx.url, 50, 100);
    const q = queryString(ctx.url, "q", /^.{1,32}$/u);
    const cleaned = q ? cleanLine(q) : "";
    const where = cleaned ? "WHERE c.tag_normalized = ?3 OR instr(c.name_normalized, ?4) > 0" : "";
    const db = ctx.env.DB;
    const [rows, totals] = await db.batch([
        db
            .prepare(
                `SELECT c.*, ${MEMBERS} AS members, p.id AS leader_id, p.username AS leader_name FROM clans c
                 LEFT JOIN clan_members m ON m.clan_id = c.id AND m.role = 'leader' LEFT JOIN players p ON p.id = m.player_id
                 ${where} ORDER BY c.created_at DESC, c.id LIMIT ?1 OFFSET ?2`,
            )
            .bind(page.limit + 1, page.offset, ...(cleaned ? [cleaned.toUpperCase(), normalizeClanName(cleaned)] : [])),
        db.prepare("SELECT COUNT(*) AS n FROM clans"),
    ]);
    const list = rows!.results as (ClanRow & { leader_id: string | null; leader_name: string | null })[];
    return json(ctx, {
        clans: list.slice(0, page.limit).map((c) => ({
            ...clanJson(c),
            leader: c.leader_id ? { id: c.leader_id, username: c.leader_name } : null,
            updated_at: c.updated_at,
        })),
        total: (totals!.results[0] as { n: number } | undefined)?.n ?? 0,
        has_more: list.length > page.limit,
        offset: page.offset,
    });
}

/** PATCH /admin/clans/{clanId} {name?, tag?, reason?}: an administrator renames a clan (Journal: admin_clan_renamed). */
export async function adminRenameClan(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const id = clanIdParam(ctx);
    const body = await readJsonObject(ctx);
    const reasonRaw = body.reason;
    delete body.reason;
    const f = readClanFields(body, false);
    if (f.motto !== undefined || f.emblem !== undefined || f.join_policy !== undefined)
        throw Errors.validation([{ field: "body", message: "seuls le nom et le tag se modifient ici" }]);
    if (f.name === undefined && f.tag === undefined) throw Errors.validation([{ field: "name", message: "nom ou tag obligatoire" }]);
    const reason = typeof reasonRaw === "string" ? cleanLine(reasonRaw).slice(0, 200) : null;
    const clan = await loadClan(ctx, id);
    const name = f.name ?? clan.name;
    const tag = f.tag ?? clan.tag;
    const db = ctx.env.DB;
    const what = { clan: journalClan({ id, name, tag }, { previous: { name: clan.name, tag: clan.tag } }), by_username: auth.player.username, reason };
    try {
        await db.batch([
            db
                .prepare("UPDATE clans SET name = ?2, name_normalized = ?3, tag = ?4, tag_normalized = ?5, updated_at = ?6 WHERE id = ?1")
                .bind(id, name, normalizeClanName(name), tag, tag.toUpperCase(), nowIso()),
            db.prepare("UPDATE conversations SET name = ?2 WHERE clan_id = ?1").bind(id, name),
        ]);
    } catch (e) {
        if (constraintOf(e) === "tag") {
            await audit(ctx, auth.player.id, "admin_refused", { ...what, action: "admin_clan_renamed", reason: "TAG_TAKEN" });
            throw Errors.conflict("TAG_TAKEN", "Ce tag est déjà pris par un autre clan.");
        }
        throw e;
    }
    await audit(ctx, auth.player.id, "admin_clan_renamed", what);
    return json(ctx, { clan: clanJson(await loadClan(ctx, id)) });
}

/** DELETE /admin/clans/{clanId} {reason?}: an administrator dissolves a clan (Journal: admin_clan_dissolved). */
export async function adminDissolveClan(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const id = clanIdParam(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["reason"]);
    const reason = v.str("reason", { max: 200, optional: true, trim: true }) ?? null;
    v.done();
    const clan = await loadClan(ctx, id);
    const members = await ctx.env.DB.prepare("SELECT player_id FROM clan_members WHERE clan_id = ?1").bind(id).all<{ player_id: string }>();
    const r = await ctx.env.DB.prepare("DELETE FROM clans WHERE id = ?1").bind(id).run();
    if (!r.meta.changes) throw CLAN_NOT_FOUND();
    await audit(ctx, auth.player.id, "admin_clan_dissolved", {
        clan: journalClan(clan, { members: members.results.length }),
        by_username: auth.player.username,
        reason,
    });
    await notifyClan(
        ctx,
        members.results.map((m) => m.player_id),
        null,
        { event: "dissolved", by_admin: true, ...clanRef(clan) },
    );
    return json(ctx, { dissolved: true });
}
