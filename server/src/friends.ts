// Friends: requests, acceptance, removal. The target's "allow friend requests" setting is
// respected and a player never learns more than the public identity of another one.

import { audit, requireAuth, type AuthInfo } from "./auth";
import { Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import { playerSummary, type PlayerRow } from "./model";
import { RULES, rateLimit } from "./ratelimit";
import { playingNow, spectatable } from "./spectate";
import { USERNAME_RE, Validator, normalizeUsername } from "./validate";

const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const MAX_FRIENDS = 200;

export async function listFriends(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const id = auth.player.id;
    const rows = await ctx.env.DB.prepare(
        `SELECT f.player_id AS a, f.friend_id AS b, f.status, f.created_at, p.*, kc.tag AS clan_tag
         FROM player_friends f JOIN players p ON p.id = CASE WHEN f.player_id = ?1 THEN f.friend_id ELSE f.player_id END
         LEFT JOIN clan_members km ON km.player_id = p.id LEFT JOIN clans kc ON kc.id = km.clan_id
         WHERE (f.player_id = ?1 OR f.friend_id = ?1) AND p.account_status = 'active'
         ORDER BY p.username_normalized`,
    )
        .bind(id)
        .all<PlayerRow & { a: string; b: string; status: string; created_at: string; clan_tag: string | null }>();
    const friends = [];
    const incoming = [];
    const outgoing = [];
    for (const r of rows.results) {
        // 2.66: the clan tag next to the username (only when the player has a clan).
        const player = r.clan_tag ? { ...playerSummary(r), clan_tag: r.clan_tag } : playerSummary(r);
        const entry = { player, since: r.created_at, ...(r.status === "accepted" ? { presence: presenceOf(r) } : {}) };
        if (r.status === "accepted") {
            if (r.a === id) friends.push(entry);
        } else if (r.a === id) outgoing.push(entry);
        else incoming.push(entry);
    }
    // Live spectating (2.36): `playing` for a friend playing a run now who lets friends watch
    // (setting on, presence visible), so « Regarder » only shows when it can work. Only the
    // rooms of the friends whose game runs (presence online or away) are asked.
    const blocks = await ctx.env.DB.prepare("SELECT player_id, blocked_id FROM player_blocks WHERE player_id = ?1 OR blocked_id = ?1")
        .bind(id)
        .all<{ player_id: string; blocked_id: string }>();
    const blocked = new Set(blocks.results.map((b) => (b.player_id === id ? b.blocked_id : b.player_id)));
    const candidates = rows.results.filter(
        (r) => r.status === "accepted" && r.a === id && !blocked.has(r.id) && spectatable(r) && (presenceOf(r) === "online" || presenceOf(r) === "away"),
    );
    const playing = candidates.length ? await playingNow(ctx.env, candidates.map((r) => r.id)) : new Map<string, unknown>();
    const withPlaying = friends.map((f) => (playing.has(f.player.id) ? { ...f, playing: playing.get(f.player.id) } : f));
    return json(ctx, { friends: withPlaying, incoming, outgoing });
}

export async function requestFriend(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.friendRequest, "player:" + auth.player.id);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["username", "player_id"]);
    const username = v.str("username", { min: 3, max: 20, pattern: USERNAME_RE, optional: true });
    const playerId = v.str("player_id", { min: 36, max: 36, pattern: ID_RE, optional: true });
    if (!username && !playerId) v.fail("username", "pseudonyme ou identifiant obligatoire");
    v.done();
    const target = username
        ? await ctx.env.DB.prepare("SELECT * FROM players WHERE username_normalized = ?1").bind(normalizeUsername(username)).first<PlayerRow>()
        : await ctx.env.DB.prepare("SELECT * FROM players WHERE id = ?1").bind(playerId).first<PlayerRow>();
    if (!target || target.account_status !== "active") throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    const me = auth.player.id;
    if (target.id === me) throw Errors.validation([{ field: "player_id", message: "vous ne pouvez pas vous ajouter vous-même" }]);
    // A block (either way) stops friend requests; the message does not tell who blocked whom.
    const blocked = await ctx.env.DB.prepare(
        "SELECT 1 AS x FROM player_blocks WHERE (player_id = ?1 AND blocked_id = ?2) OR (player_id = ?2 AND blocked_id = ?1)",
    )
        .bind(me, target.id)
        .first();
    if (blocked) throw Errors.forbidden("FRIEND_REQUESTS_DISABLED", "Ce joueur n'accepte pas les demandes d'amis.");
    const existing = await ctx.env.DB.prepare(
        "SELECT player_id, status FROM player_friends WHERE (player_id = ?1 AND friend_id = ?2) OR (player_id = ?2 AND friend_id = ?1)",
    )
        .bind(me, target.id)
        .all<{ player_id: string; status: string }>();
    if (existing.results.some((r) => r.status === "accepted")) throw Errors.conflict("ALREADY_FRIENDS", "Vous êtes déjà amis.");
    if (existing.results.some((r) => r.player_id === me)) throw Errors.conflict("REQUEST_ALREADY_SENT", "Demande déjà envoyée.");
    const count = await ctx.env.DB.prepare("SELECT COUNT(*) AS n FROM player_friends WHERE player_id = ?1").bind(me).first<{ n: number }>();
    if ((count?.n ?? 0) >= MAX_FRIENDS) throw Errors.conflict("FRIEND_LIMIT", `Limite de ${MAX_FRIENDS} amis atteinte.`);
    const now = nowIso();
    // A request in the other direction already exists: sending one back accepts it.
    if (existing.results.some((r) => r.player_id === target.id)) {
        await ctx.env.DB.batch([
            ctx.env.DB.prepare("UPDATE player_friends SET status = 'accepted' WHERE player_id = ?1 AND friend_id = ?2").bind(target.id, me),
            ctx.env.DB.prepare("INSERT INTO player_friends (player_id, friend_id, status, created_at) VALUES (?1, ?2, 'accepted', ?3)").bind(me, target.id, now),
        ]);
        await audit(ctx, me, "friend_accepted", { friend_id: target.id });
        return json(ctx, { status: "accepted", player: playerSummary(target) });
    }
    if (target.allow_friend_requests !== 1) throw Errors.forbidden("FRIEND_REQUESTS_DISABLED", "Ce joueur n'accepte pas les demandes d'amis.");
    await ctx.env.DB.prepare("INSERT INTO player_friends (player_id, friend_id, status, created_at) VALUES (?1, ?2, 'pending', ?3)")
        .bind(me, target.id, now)
        .run();
    return json(ctx, { status: "pending", player: playerSummary(target) }, { status: 201 });
}

export async function acceptFriend(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["player_id"]);
    const playerId = v.str("player_id", { min: 36, max: 36, pattern: ID_RE });
    v.done();
    const me = auth.player.id;
    const req = await ctx.env.DB.prepare("SELECT 1 AS x FROM player_friends WHERE player_id = ?1 AND friend_id = ?2 AND status = 'pending'")
        .bind(playerId, me)
        .first();
    if (!req) throw Errors.notFound("REQUEST_NOT_FOUND", "Aucune demande d'ami de ce joueur.");
    await ctx.env.DB.batch([
        ctx.env.DB.prepare("UPDATE player_friends SET status = 'accepted' WHERE player_id = ?1 AND friend_id = ?2").bind(playerId, me),
        ctx.env.DB.prepare(
            `INSERT INTO player_friends (player_id, friend_id, status, created_at) VALUES (?1, ?2, 'accepted', ?3)
             ON CONFLICT (player_id, friend_id) DO UPDATE SET status = 'accepted'`,
        ).bind(me, playerId, nowIso()),
    ]);
    await audit(ctx, me, "friend_accepted", { friend_id: playerId });
    const p = await ctx.env.DB.prepare("SELECT * FROM players WHERE id = ?1").bind(playerId).first<PlayerRow>();
    return json(ctx, { status: "accepted", player: p ? playerSummary(p) : null });
}

/** DELETE /friends/{playerId}: removes a friend, declines or cancels a request. */
export async function deleteFriend(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const other = ctx.params.playerId!;
    if (!ID_RE.test(other)) throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    const res = await ctx.env.DB.prepare(
        "DELETE FROM player_friends WHERE (player_id = ?1 AND friend_id = ?2) OR (player_id = ?2 AND friend_id = ?1)",
    )
        .bind(auth.player.id, other)
        .run();
    if (!res.meta.changes) throw Errors.notFound("FRIEND_NOT_FOUND", "Aucun lien avec ce joueur.");
    return json(ctx, { removed: true });
}

/**
 * A friend's presence: « online », « away » (idle), « offline » (silent for 150 s), or « hidden »
 * (2.47: « Montrer ma présence » unticked — the others see a crossed circle, never whether the
 * player is there).
 */
export const PRESENCE_TIMEOUT_MS = 150_000;
export type Presence = "online" | "away" | "offline" | "hidden";
export function presenceOf(p: Pick<PlayerRow, "presence_state" | "presence_at" | "presence_visible">, now = Date.now()): Presence {
    if (p.presence_visible === 0) return "hidden";
    if (!p.presence_at || !p.presence_state) return "offline";
    const at = Date.parse(p.presence_at);
    if (!Number.isFinite(at) || now - at > PRESENCE_TIMEOUT_MS) return "offline";
    return p.presence_state === "idle" ? "away" : "online";
}

/**
 * 2.72: a player's presence in the lists of a clan (CLANS › Membres, the registrants of a raid
 * of the clan), shown to that clan's members only: « online », « away » or « offline ». A hidden
 * presence (« Montrer ma présence » unticked) reads « offline » there, and counts so: the list
 * never tells that the player hides it.
 */
export type ListPresence = Exclude<Presence, "hidden">;
export function listPresenceOf(p: Pick<PlayerRow, "presence_state" | "presence_at" | "presence_visible">, now = Date.now()): ListPresence {
    const s = presenceOf(p, now);
    return s === "hidden" ? "offline" : s;
}

/**
 * 2.75: an active administrator. For moderation, the administrators see the presence and the hidden
 * profiles with their REAL state (« Montrer ma présence » unticked, profile not public), marked
 * « masqué » so they know; nobody else ever does (the server decides from the session, never from what
 * a client says). The location is NOT part of it: never precise, and the country stays hidden unless
 * the player shows it (visibleCountry), administrators included.
 */
export function isAdminViewer(viewer: AuthInfo | null | undefined): boolean {
    return !!viewer && viewer.player.role === "admin" && viewer.player.account_status === "active";
}

/** 2.75: a presence as an administrator sees it: the real state, and whether the player hides it. */
export function adminPresenceOf(
    p: Pick<PlayerRow, "presence_state" | "presence_at" | "presence_visible">,
    now = Date.now(),
): { presence: ListPresence; presence_hidden: boolean } {
    return { presence: presenceOf({ ...p, presence_visible: 1 }, now) as ListPresence, presence_hidden: p.presence_visible === 0 };
}

/** The « X » of « X/Y en ligne »: the players of a list « online » now (« away » and « offline » are not). */
export function countOnline(list: readonly (Presence | null | undefined)[]): number {
    return list.reduce((n, p) => n + (p === "online" ? 1 : 0), 0);
}

/** POST /presence {state: active|idle|offline}: the game's heartbeat (every minute while it runs). */
export async function postPresence(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.presence, "player:" + auth.player.id);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["state"]);
    const state = v.oneOf("state", ["active", "idle", "offline"]);
    v.done();
    if (state === "offline")
        await ctx.env.DB.prepare("UPDATE players SET presence_state = NULL, presence_at = NULL WHERE id = ?1").bind(auth.player.id).run();
    else
        await ctx.env.DB.prepare("UPDATE players SET presence_state = ?2, presence_at = ?3 WHERE id = ?1").bind(auth.player.id, state, nowIso()).run();
    return json(ctx, { state });
}
