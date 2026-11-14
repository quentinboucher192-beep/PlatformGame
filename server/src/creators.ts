// Creators and followers (Cosmic Online 2.9), and time played. A player becomes a creator
// as soon as one of their community levels is published and available (drafts, hidden or
// refused levels do not count): the status is computed by the server, never declared.
// Following a creator is not a friendship; nobody can follow themselves, and a block in
// either direction ends the follow. The player is always identified by the session.

import { type AuthInfo, optionalAuth, requireAuth } from "./auth";
import { Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import { playerSummary, type PlayerRow } from "./model";
import { notify } from "./notifications";
import { RULES, rateLimit } from "./ratelimit";
import { Validator, paging } from "./validate";

const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const LEVEL_KEY_RE = /^(other|[a-z0-9_]{1,64}|[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})$/;
const MAX_SESSION_SECONDS = 6 * 3600;

async function loadPlayer(ctx: Ctx, id: string): Promise<PlayerRow & { follows_visibility: string }> {
    if (!ID_RE.test(id)) throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    const p = await ctx.env.DB.prepare("SELECT * FROM players WHERE id = ?1 AND account_status = 'active'").bind(id).first<PlayerRow & { follows_visibility: string }>();
    if (!p) throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    return p;
}

async function blockedBetween(ctx: Ctx, a: string, b: string): Promise<boolean> {
    const r = await ctx.env.DB.prepare(
        "SELECT 1 AS x FROM player_blocks WHERE (player_id = ?1 AND blocked_id = ?2) OR (player_id = ?2 AND blocked_id = ?1)",
    )
        .bind(a, b)
        .first();
    return r !== null;
}

/** Creator statistics of a player (all computed from the published community levels). */
export async function creatorStats(ctx: Ctx, playerId: string) {
    const s = await ctx.env.DB.prepare(
        `SELECT
           (SELECT COUNT(*) FROM community_levels WHERE author_id = ?1 AND status = 'published') AS levels_published,
           (SELECT COALESCE(SUM(downloads), 0) FROM community_levels WHERE author_id = ?1 AND status = 'published') AS downloads,
           (SELECT COALESCE(SUM(favorite_count), 0) FROM community_levels WHERE author_id = ?1 AND status = 'published') AS favorites,
           (SELECT COALESCE(SUM(rating_sum), 0) FROM community_levels WHERE author_id = ?1 AND status = 'published') AS rating_sum,
           (SELECT COALESCE(SUM(rating_count), 0) FROM community_levels WHERE author_id = ?1 AND status = 'published') AS rating_count,
           (SELECT COUNT(DISTINCT lp.player_id) FROM community_level_players lp JOIN community_levels l ON l.id = lp.level_id
              WHERE l.author_id = ?1 AND l.status = 'published' AND lp.player_id <> ?1) AS unique_players,
           (SELECT MIN(v.published_at) FROM community_level_versions v JOIN community_levels l ON l.id = v.level_id
              WHERE l.author_id = ?1 AND l.status = 'published' AND v.published_at IS NOT NULL) AS first_published_at,
           (SELECT COUNT(*) FROM player_follows WHERE creator_id = ?1) AS followers,
           (SELECT COUNT(*) FROM player_follows WHERE follower_id = ?1) AS following`,
    )
        .bind(playerId)
        .first<Record<string, number | string | null>>();
    const n = (k: string) => Number(s?.[k] ?? 0);
    return {
        is_creator: n("levels_published") > 0,
        levels_published: n("levels_published"),
        downloads: n("downloads"),
        unique_players: n("unique_players"),
        favorites: n("favorites"),
        rating: n("rating_count") > 0 ? { average: Math.round((n("rating_sum") / n("rating_count")) * 10) / 10, count: n("rating_count") } : null,
        first_published_at: (s?.first_published_at as string | null) ?? null,
        followers: n("followers"),
        following: n("following"),
    };
}

/** Follow relation between the viewer and a player. */
export async function followState(ctx: Ctx, viewer: string | null, playerId: string) {
    if (!viewer || viewer === playerId) return { following: false, followed_by: false, notify: false };
    const r = await ctx.env.DB.prepare(
        `SELECT (SELECT notify FROM player_follows WHERE follower_id = ?1 AND creator_id = ?2) AS mine,
                (SELECT 1 FROM player_follows WHERE follower_id = ?2 AND creator_id = ?1) AS theirs`,
    )
        .bind(viewer, playerId)
        .first<{ mine: number | null; theirs: number | null }>();
    return { following: r?.mine != null, followed_by: r?.theirs != null, notify: r?.mine === 1 };
}

/** Time played: total and per level (titles of official and community levels). */
export async function playtime(ctx: Ctx, playerId: string, limit = 10) {
    const total = await ctx.env.DB.prepare("SELECT COALESCE(SUM(seconds), 0) AS s FROM player_playtime WHERE player_id = ?1").bind(playerId).first<{ s: number }>();
    const rows = await ctx.env.DB.prepare(
        `SELECT t.level_key, t.seconds, t.updated_at,
                COALESCE((SELECT name FROM levels WHERE id = t.level_key), (SELECT title FROM community_levels WHERE id = t.level_key AND status = 'published')) AS title
         FROM player_playtime t WHERE t.player_id = ?1 AND t.level_key <> 'other' ORDER BY t.seconds DESC LIMIT ?2`,
    )
        .bind(playerId, limit)
        .all<{ level_key: string; seconds: number; updated_at: string; title: string | null }>();
    return {
        total_seconds: total?.s ?? 0,
        levels: rows.results.filter((r) => r.title).map((r) => ({ level_id: r.level_key, title: r.title, seconds: r.seconds, last_played_at: r.updated_at })),
    };
}

// ------------------------------------------------------------------- routes

/** POST /players/{id}/follow {notify?}: idempotent. */
export async function follow(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.follow, "player:" + me);
    const p = await loadPlayer(ctx, ctx.params.playerId!);
    if (p.id === me) throw Errors.forbidden("FOLLOW_SELF", "Vous ne pouvez pas vous suivre vous-même.");
    if (await blockedBetween(ctx, me, p.id)) throw Errors.forbidden("BLOCKED", "Vous ne pouvez pas suivre ce joueur.");
    let wants = true;
    if ((ctx.request.headers.get("content-type") ?? "").startsWith("application/json")) {
        const v = new Validator(await readJsonObject(ctx), ["notify"]);
        wants = v.bool("notify", { optional: true }) ?? true;
        v.done();
    }
    const res = await ctx.env.DB.prepare(
        `INSERT INTO player_follows (follower_id, creator_id, notify, created_at) VALUES (?1, ?2, ?3, ?4)
         ON CONFLICT (follower_id, creator_id) DO UPDATE SET notify = excluded.notify`,
    )
        .bind(me, p.id, wants ? 1 : 0, nowIso())
        .run();
    const existed = res.meta.changes > 0 && (await ctx.env.DB.prepare("SELECT created_at FROM player_follows WHERE follower_id = ?1 AND creator_id = ?2").bind(me, p.id).first());
    void existed;
    await notify(ctx, p.id, "new_follower", { actor: me, groupKey: "followers" });
    const stats = await creatorStats(ctx, p.id);
    return json(ctx, { following: true, notify: wants, followers: stats.followers });
}

/** DELETE /players/{id}/follow */
export async function unfollow(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const id = ctx.params.playerId ?? "";
    if (!ID_RE.test(id)) throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    await ctx.env.DB.prepare("DELETE FROM player_follows WHERE follower_id = ?1 AND creator_id = ?2").bind(auth.player.id, id).run();
    const stats = await creatorStats(ctx, id);
    return json(ctx, { following: false, followers: stats.followers });
}

/** Lists of followers / followed creators: visible to their owner, or when the player made them public. */
async function followList(ctx: Ctx, viewer: AuthInfo | null, which: "followers" | "following"): Promise<Response> {
    const p = await loadPlayer(ctx, ctx.params.playerId!);
    const me = viewer?.player.id ?? null;
    const own = me === p.id;
    if (!own && (p.follows_visibility === "private" || p.profile_visibility === "private"))
        throw Errors.forbidden("FOLLOWS_PRIVATE", "Ce joueur garde ses abonnements privés.");
    const { limit, offset } = paging(ctx.url, 30, 100);
    const col = which === "followers" ? "f.follower_id" : "f.creator_id";
    const key = which === "followers" ? "f.creator_id" : "f.follower_id";
    const where = `${key} = ?1 AND pp.account_status = 'active'
      AND NOT EXISTS (SELECT 1 FROM player_blocks b WHERE (b.player_id = ?2 AND b.blocked_id = pp.id) OR (b.player_id = pp.id AND b.blocked_id = ?2))`;
    const total = await ctx.env.DB.prepare(`SELECT COUNT(*) AS n FROM player_follows f JOIN players pp ON pp.id = ${col} WHERE ${where}`)
        .bind(p.id, me ?? "")
        .first<{ n: number }>();
    const rows = await ctx.env.DB.prepare(
        `SELECT pp.*, f.created_at AS since,
                EXISTS (SELECT 1 FROM community_levels cl WHERE cl.author_id = pp.id AND cl.status = 'published') AS is_creator,
                (SELECT COUNT(*) FROM player_follows x WHERE x.creator_id = pp.id) AS followers
         FROM player_follows f JOIN players pp ON pp.id = ${col} WHERE ${where} ORDER BY f.created_at DESC LIMIT ?3 OFFSET ?4`,
    )
        .bind(p.id, me ?? "", limit, offset)
        .all<PlayerRow & { since: string; is_creator: number; followers: number }>();
    const n = total?.n ?? 0;
    return json(ctx, { players: rows.results.map((r) => ({ ...playerSummary(r), creator: r.is_creator === 1, followers: r.followers, since: r.since })) }, {
        meta: { limit, offset, total: n, next_offset: offset + limit < n ? offset + limit : null },
    });
}

export const followers = (ctx: Ctx, viewer: AuthInfo | null) => followList(ctx, viewer, "followers");
export const following = (ctx: Ctx, viewer: AuthInfo | null) => followList(ctx, viewer, "following");

/** GET /players/{id}/creations?sort=popular|recent|top&limit=&offset= : published levels (and drafts for oneself). */
export async function creations(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const p = await loadPlayer(ctx, ctx.params.playerId!);
    const me = viewer?.player.id ?? "";
    const sort = ctx.url.searchParams.get("sort") ?? "recent";
    const order = {
        popular: "(l.downloads + 3 * l.completions + 2 * l.rating_sum + 2 * l.favorite_count) DESC, l.updated_at DESC",
        recent: "l.updated_at DESC",
        top: "((l.rating_sum + 9.0) / (l.rating_count + 3.0)) DESC, l.rating_count DESC",
    }[sort];
    if (!order) throw Errors.validation([{ field: "sort", message: "popular, recent ou top" }]);
    const { limit, offset } = paging(ctx.url, 20, 50);
    const where = me === p.id ? "l.author_id = ?1 AND l.status <> 'removed'" : "l.author_id = ?1 AND l.status = 'published'";
    const total = await ctx.env.DB.prepare(`SELECT COUNT(*) AS n FROM community_levels l WHERE ${where}`).bind(p.id).first<{ n: number }>();
    const rows = await ctx.env.DB.prepare(
        `SELECT l.id, l.title, l.description, l.planet, l.difficulty, l.status, l.current_version, l.downloads, l.completions, l.rating_sum, l.rating_count,
                l.favorite_count, l.comment_count, l.created_at, l.updated_at,
                (SELECT MIN(published_at) FROM community_level_versions v WHERE v.level_id = l.id AND v.published_at IS NOT NULL) AS first_published_at
         FROM community_levels l WHERE ${where} ORDER BY ${order} LIMIT ?2 OFFSET ?3`,
    )
        .bind(p.id, limit, offset)
        .all<Record<string, unknown>>();
    const n = total?.n ?? 0;
    return json(ctx, {
        creations: rows.results.map((l) => ({
            id: l.id,
            title: l.title,
            description: l.description,
            planet: l.planet,
            difficulty: l.difficulty,
            status: l.status,
            version: l.current_version,
            downloads: l.downloads,
            completions: l.completions,
            favorites: l.favorite_count,
            comments: l.comment_count,
            rating: (l.rating_count as number) > 0 ? { average: Math.round(((l.rating_sum as number) / (l.rating_count as number)) * 10) / 10, count: l.rating_count } : null,
            first_published_at: l.first_published_at,
            updated_at: l.updated_at,
            thumbnail_url: `/api/v1/community/levels/${l.id}/thumbnail`,
        })),
        sort,
    }, { meta: { limit, offset, total: n, next_offset: offset + limit < n ? offset + limit : null } });
}

/** GET /creators?q=&sort=followers|levels|downloads|recent&limit=&offset= : players with a published level. */
export async function searchCreators(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const me = viewer?.player.id ?? "";
    const sort = ctx.url.searchParams.get("sort") ?? "followers";
    const order = { followers: "followers DESC, levels DESC", levels: "levels DESC, followers DESC", downloads: "downloads DESC", recent: "last_published DESC" }[sort];
    if (!order) throw Errors.validation([{ field: "sort", message: "followers, levels, downloads ou recent" }]);
    const q = (ctx.url.searchParams.get("q") ?? "").trim().toLowerCase().slice(0, 32);
    const { limit, offset } = paging(ctx.url, 20, 50);
    const args: unknown[] = [me];
    let filter = "";
    if (q) {
        args.push(q.replace(/[\\%_]/g, (c) => "\\" + c) + "%");
        filter = ` AND p.username_normalized LIKE ?${args.length} ESCAPE '\\'`;
    }
    const base = `FROM players p WHERE p.account_status = 'active' AND p.profile_visibility = 'public'
      AND EXISTS (SELECT 1 FROM community_levels l WHERE l.author_id = p.id AND l.status = 'published')
      AND NOT EXISTS (SELECT 1 FROM player_blocks b WHERE (b.player_id = ?1 AND b.blocked_id = p.id) OR (b.player_id = p.id AND b.blocked_id = ?1))${filter}`;
    const total = await ctx.env.DB.prepare(`SELECT COUNT(*) AS n ${base}`).bind(...args).first<{ n: number }>();
    args.push(limit, offset);
    const rows = await ctx.env.DB.prepare(
        `SELECT p.*,
           (SELECT COUNT(*) FROM player_follows f WHERE f.creator_id = p.id) AS followers,
           (SELECT COUNT(*) FROM community_levels l WHERE l.author_id = p.id AND l.status = 'published') AS levels,
           (SELECT COALESCE(SUM(downloads), 0) FROM community_levels l WHERE l.author_id = p.id AND l.status = 'published') AS downloads,
           (SELECT MAX(updated_at) FROM community_levels l WHERE l.author_id = p.id AND l.status = 'published') AS last_published,
           EXISTS (SELECT 1 FROM player_follows f WHERE f.follower_id = ?1 AND f.creator_id = p.id) AS followed
         ${base} ORDER BY ${order} LIMIT ?${args.length - 1} OFFSET ?${args.length}`,
    )
        .bind(...args)
        .all<PlayerRow & { followers: number; levels: number; downloads: number; last_published: string; followed: number }>();
    const n = total?.n ?? 0;
    return json(ctx, {
        creators: rows.results.map((r) => ({ ...playerSummary(r), followers: r.followers, levels: r.levels, downloads: r.downloads, last_published_at: r.last_published, following: r.followed === 1 })),
        sort,
    }, { meta: { limit, offset, total: n, next_offset: offset + limit < n ? offset + limit : null } });
}

/**
 * POST /players/me/playtime {sessions: [{level_id, seconds}]}: time played since the last
 * report. Informational only (no ranking uses it), and still capped by the server: never
 * more in total than the time elapsed since the previous report (plus a margin), never more
 * than 6 h per entry.
 */
export async function reportPlaytime(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.playtime, "player:" + me);
    const body = await readJsonObject(ctx, 16 * 1024);
    const sessions = body.sessions;
    if (!Array.isArray(sessions) || sessions.length > 50) throw Errors.validation([{ field: "sessions", message: "liste de 50 entrées au plus" }]);
    const items: { key: string; seconds: number }[] = [];
    for (const [i, s] of sessions.entries()) {
        const v = new Validator((s ?? {}) as Record<string, unknown>, ["level_id", "seconds"]);
        const key = v.str("level_id", { min: 1, max: 64, pattern: LEVEL_KEY_RE });
        const seconds = v.int("seconds", { min: 1, max: MAX_SESSION_SECONDS });
        try {
            v.done();
        } catch {
            throw Errors.validation([{ field: `sessions[${i}]`, message: "level_id et seconds attendus" }]);
        }
        items.push({ key: key!, seconds: seconds! });
    }
    const p = await ctx.env.DB.prepare("SELECT playtime_reported_at FROM players WHERE id = ?1").bind(me).first<{ playtime_reported_at: string | null }>();
    const now = Date.now();
    const since = p?.playtime_reported_at ? Math.max(0, (now - Date.parse(p.playtime_reported_at)) / 1000) : MAX_SESSION_SECONDS;
    let budget = Math.min(MAX_SESSION_SECONDS * 4, since + 300);
    const accepted: { key: string; seconds: number }[] = [];
    for (const it of items) {
        const s = Math.min(it.seconds, Math.floor(budget));
        if (s <= 0) break;
        budget -= s;
        accepted.push({ key: it.key, seconds: s });
    }
    const iso = new Date(now).toISOString();
    const stmts = accepted.map((a) =>
        ctx.env.DB.prepare(
            `INSERT INTO player_playtime (player_id, level_key, seconds, updated_at) VALUES (?1, ?2, ?3, ?4)
             ON CONFLICT (player_id, level_key) DO UPDATE SET seconds = seconds + excluded.seconds, updated_at = excluded.updated_at`,
        ).bind(me, a.key, a.seconds, iso),
    );
    stmts.push(ctx.env.DB.prepare("UPDATE players SET playtime_reported_at = ?2 WHERE id = ?1").bind(me, iso));
    await ctx.env.DB.batch(stmts);
    return json(ctx, { accepted_seconds: accepted.reduce((s, a) => s + a.seconds, 0), playtime: await playtime(ctx, me) });
}

/** GET /players/{id}/playtime : total and per level (profiles that are visible). */
export async function getPlaytime(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const p = await loadPlayer(ctx, ctx.params.playerId!);
    if (viewer?.player.id !== p.id && p.profile_visibility === "private") throw Errors.forbidden("PROFILE_PRIVATE", "Ce profil est privé.");
    return json(ctx, { player_id: p.id, playtime: await playtime(ctx, p.id, 50) });
}
