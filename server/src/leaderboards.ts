// Leaderboards. Every board is computed from D1:
//   * level boards (world / country / friends / weekly) rank the best VERIFIED time of each
//     player on ONE level revision and category;
//   * the planet board sums the best times of players who finished every ranked level of
//     a planet (current revisions);
//   * the global board ranks "cosmic points": 1000 x world best / player best, summed over
//     every ranked level.
// PENDING and REJECTED records never appear in rankings.

import type { AuthInfo } from "./auth";
import { requireAuth } from "./auth";
import { ApiError, Errors, json, weekStart, type Ctx, type PageMeta } from "./http";
import { resolveBoard, versionJson, type Board } from "./levels";
import { avatarUrl } from "./model";
import {
    CATEGORIES,
    COUNTRY_RE,
    LEVEL_ID_RE,
    MODES,
    PLANET_ID_RE,
    likePrefix,
    normalizeUsername,
    paging,
    queryEnum,
    queryString,
    type Category,
} from "./validate";

export type Period = "all" | "weekly";
export const PERIODS = ["all", "weekly"] as const;

interface BoardFilter {
    country?: string;
    friendsOf?: string;
    search?: string;
}

interface BoardRow {
    player_id: string;
    username: string;
    avatar: string;
    avatar_custom_key: string | null;
    country_code: string | null;
    location_visibility: string;
    record_id: string;
    time_ms: number;
    achieved_at: string;
    has_replay: number;
    world_rank: number;
    rank: number;
    total: number;
}

/**
 * Builds the SQL of a level board. Parameters: ?1 level hash, ?2 category, ?3 week start
 * (weekly only), then filter parameters from ?4.
 */
function boardSql(period: Period, filter: BoardFilter): { sql: string; extra: unknown[] } {
    const source =
        period === "all"
            ? `SELECT b.player_id, b.record_id, b.time_ms, b.achieved_at FROM player_bests b
               WHERE b.level_hash = ?1 AND b.category = ?2 AND (?3 IS NULL OR 1 = 1)`
            : `SELECT player_id, record_id, time_ms, achieved_at FROM (
                 SELECT r.player_id, r.id AS record_id, r.time_ms, r.created_at AS achieved_at,
                        ROW_NUMBER() OVER (PARTITION BY r.player_id ORDER BY r.time_ms, r.created_at) AS rn
                 FROM records r WHERE r.level_hash = ?1 AND r.category = ?2 AND r.verification_status = 'VERIFIED'
                   AND r.created_at >= ?3) WHERE rn = 1`;
    const where: string[] = [];
    const extra: unknown[] = [];
    let n = 4;
    if (filter.country) {
        // Players hiding their location never appear in a country board.
        where.push(`country_code = ?${n++} AND location_visibility <> 'hidden'`);
        extra.push(filter.country);
    }
    if (filter.friendsOf) {
        where.push(`(player_id = ?${n} OR player_id IN (SELECT friend_id FROM player_friends WHERE player_id = ?${n} AND status = 'accepted'))`);
        n++;
        extra.push(filter.friendsOf);
    }
    if (filter.search) {
        where.push(`username_normalized LIKE ?${n++} ESCAPE '\\'`);
        extra.push(likePrefix(normalizeUsername(filter.search)));
    }
    const sql = `
      WITH src AS (${source}),
      ranked AS (
        SELECT s.*, p.username, p.username_normalized, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility,
               (SELECT COUNT(*) FROM replay_metadata m JOIN records rr ON rr.replay_id = m.id
                 WHERE rr.id = s.record_id AND m.verification_status = 'VERIFIED') AS has_replay,
               ROW_NUMBER() OVER (ORDER BY s.time_ms, s.achieved_at, s.player_id) AS world_rank
        FROM src s JOIN players p ON p.id = s.player_id WHERE p.account_status = 'active'),
      filtered AS (
        SELECT ranked.*, ROW_NUMBER() OVER (ORDER BY world_rank) AS rank FROM ranked
        ${where.length ? "WHERE " + where.join(" AND ") : ""})
      SELECT *, COUNT(*) OVER () AS total FROM filtered`;
    return { sql, extra };
}

function entryJson(r: BoardRow, viewer: AuthInfo | null) {
    return {
        rank: r.rank,
        world_rank: r.world_rank,
        player: {
            id: r.player_id,
            username: r.username,
            avatar: r.avatar,
            avatar_url: avatarUrl({ id: r.player_id, avatar: r.avatar, avatar_custom_key: r.avatar_custom_key }),
            country_code: r.location_visibility === "hidden" ? null : r.country_code,
        },
        time_ms: r.time_ms,
        record_id: r.record_id,
        achieved_at: r.achieved_at,
        has_replay: r.has_replay > 0,
        is_me: viewer?.player.id === r.player_id,
    };
}

async function pendingOf(ctx: Ctx, viewer: AuthInfo | null, board: Board, category: string) {
    if (!viewer) return null;
    // The viewer's own pending performance is reported separately, never ranked.
    const row = await ctx.env.DB.prepare(
        `SELECT id, time_ms, created_at, verification_notes FROM records
         WHERE player_id = ?1 AND level_hash = ?2 AND category = ?3 AND verification_status = 'PENDING'
         ORDER BY time_ms LIMIT 1`,
    )
        .bind(viewer.player.id, board.version.content_hash, category)
        .first<{ id: string; time_ms: number; created_at: string; verification_notes: string | null }>();
    return row ? { record_id: row.id, time_ms: row.time_ms, created_at: row.created_at, note: row.verification_notes, status: "PENDING" } : null;
}

export async function levelBoard(ctx: Ctx, viewer: AuthInfo | null, levelId: string, filter: BoardFilter): Promise<Response> {
    const board = await resolveBoard(ctx, levelId);
    const category = queryEnum(ctx.url, "category", CATEGORIES, "ANY_PERCENT");
    queryEnum(ctx.url, "mode", MODES, "STANDARD");
    const period = queryEnum(ctx.url, "period", PERIODS, "all");
    const search = queryString(ctx.url, "player", /^[A-Za-z0-9_.-]{1,20}$/);
    const page = paging(ctx.url);
    const ws = weekStart();
    const { sql, extra } = boardSql(period, { ...filter, search });
    const rows = await ctx.env.DB.prepare(`${sql} ORDER BY rank LIMIT ?${4 + extra.length} OFFSET ?${5 + extra.length}`)
        .bind(board.version.content_hash, category, ws.toISOString(), ...extra, page.limit, page.offset)
        .all<BoardRow>();
    let total = rows.results[0]?.total ?? 0;
    if (rows.results.length === 0 && page.offset > 0) {
        const c = await ctx.env.DB.prepare(`SELECT COUNT(*) AS total FROM (${sql})`)
            .bind(board.version.content_hash, category, ws.toISOString(), ...extra)
            .first<{ total: number }>();
        total = c?.total ?? 0;
    }
    let me = null;
    if (viewer) {
        const mine = await ctx.env.DB.prepare(`${sql} WHERE player_id = ?${4 + extra.length}`)
            .bind(board.version.content_hash, category, ws.toISOString(), ...extra, viewer.player.id)
            .first<BoardRow>();
        me = { entry: mine ? entryJson(mine, viewer) : null, pending: await pendingOf(ctx, viewer, board, category) };
    }
    const meta: PageMeta = { limit: page.limit, offset: page.offset, total, next_offset: page.offset + page.limit < total ? page.offset + page.limit : null };
    return json(
        ctx,
        {
            board: {
                ...versionJson(board.level, board.version),
                category,
                mode: "STANDARD",
                period,
                week_start: period === "weekly" ? ws.toISOString() : null,
                week_end: period === "weekly" ? new Date(ws.getTime() + 7 * 86400000).toISOString() : null,
                country_code: filter.country ?? null,
                friends: filter.friendsOf !== undefined,
            },
            entries: rows.results.map((r) => entryJson(r, viewer)),
            me,
        },
        { meta },
    );
}

// --------------------------------------------------------------- level routes

export function routeLevel(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    return levelBoard(ctx, viewer, ctx.params.levelId!, {});
}

export function routeCountry(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const cc = ctx.params.countryCode!.toUpperCase();
    if (!COUNTRY_RE.test(cc)) throw Errors.validation([{ field: "countryCode", message: "code pays ISO à 2 lettres attendu" }]);
    return levelBoard(ctx, viewer, requiredLevel(ctx), { country: cc });
}

export async function routeFriends(ctx: Ctx): Promise<Response> {
    const viewer = await requireAuth(ctx);
    return levelBoard(ctx, viewer, requiredLevel(ctx), { friendsOf: viewer.player.id });
}

export function routeWeekly(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    ctx.url.searchParams.set("period", "weekly");
    return levelBoard(ctx, viewer, requiredLevel(ctx), {});
}

function requiredLevel(ctx: Ctx): string {
    const id = queryString(ctx.url, "level_id", LEVEL_ID_RE);
    if (!id) throw Errors.validation([{ field: "level_id", message: "obligatoire" }]);
    return id;
}

/**
 * GET /leaderboards/global: with `level_id`, the world ranking of that level (alias of the
 * level board); without, the cosmic points ranking over every ranked level.
 */
export async function routeGlobal(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const levelId = queryString(ctx.url, "level_id", LEVEL_ID_RE);
    if (levelId) return levelBoard(ctx, viewer, levelId, {});
    const category = queryEnum(ctx.url, "category", CATEGORIES, "ANY_PERCENT");
    const country = queryString(ctx.url, "country", COUNTRY_RE);
    const page = paging(ctx.url);
    const sql = `
      WITH cur AS (SELECT content_hash FROM levels WHERE ranking_enabled = 1),
      bests AS (SELECT b.* FROM player_bests b JOIN players p ON p.id = b.player_id
                WHERE b.category = ?1 AND p.account_status = 'active' AND b.level_hash IN (SELECT content_hash FROM cur)),
      wb AS (SELECT level_hash, MIN(time_ms) AS best FROM bests GROUP BY level_hash),
      pts AS (SELECT b.player_id, SUM(CAST(1000.0 * wb.best / b.time_ms AS INTEGER)) AS points, COUNT(*) AS levels,
                     SUM(b.time_ms) AS total_time
              FROM bests b JOIN wb ON wb.level_hash = b.level_hash GROUP BY b.player_id),
      ranked AS (SELECT pts.*, p.username, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility,
                   ROW_NUMBER() OVER (ORDER BY points DESC, levels DESC, total_time, pts.player_id) AS world_rank
                 FROM pts JOIN players p ON p.id = pts.player_id),
      filtered AS (SELECT ranked.*, ROW_NUMBER() OVER (ORDER BY world_rank) AS rank FROM ranked
                   ${country ? "WHERE country_code = ?2 AND location_visibility <> 'hidden'" : ""})
      SELECT *, COUNT(*) OVER () AS total FROM filtered`;
    const binds: unknown[] = country ? [category, country] : [category];
    const n = binds.length;
    const rows = await ctx.env.DB.prepare(`${sql} ORDER BY rank LIMIT ?${n + 1} OFFSET ?${n + 2}`)
        .bind(...binds, page.limit, page.offset)
        .all<{
            player_id: string; username: string; avatar: string; avatar_custom_key: string | null; country_code: string | null;
            location_visibility: string; points: number; levels: number; total_time: number; world_rank: number; rank: number; total: number;
        }>();
    const toEntry = (r: (typeof rows.results)[number]) => ({
        rank: r.rank,
        world_rank: r.world_rank,
        player: {
            id: r.player_id,
            username: r.username,
            avatar: r.avatar,
            avatar_url: avatarUrl({ id: r.player_id, avatar: r.avatar, avatar_custom_key: r.avatar_custom_key }),
            country_code: r.location_visibility === "hidden" ? null : r.country_code,
        },
        points: r.points,
        levels_completed: r.levels,
        total_time_ms: r.total_time,
        is_me: viewer?.player.id === r.player_id,
    });
    let me = null;
    if (viewer) {
        const mine = await ctx.env.DB.prepare(`${sql} WHERE player_id = ?${n + 1}`).bind(...binds, viewer.player.id).first<(typeof rows.results)[number]>();
        me = { entry: mine ? toEntry(mine) : null, pending: null };
    }
    const total = rows.results[0]?.total ?? 0;
    return json(
        ctx,
        { board: { kind: "cosmic_points", category, country_code: country ?? null }, entries: rows.results.map(toEntry), me },
        { meta: { limit: page.limit, offset: page.offset, total, next_offset: page.offset + page.limit < total ? page.offset + page.limit : null } },
    );
}

/** GET /leaderboards/planet/{planetId}: sum of best times over every level of the planet. */
export async function routePlanet(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const planetId = ctx.params.planetId!;
    if (!PLANET_ID_RE.test(planetId)) throw Errors.validation([{ field: "planetId", message: "format invalide" }]);
    const category = queryEnum(ctx.url, "category", CATEGORIES, "ANY_PERCENT");
    const country = queryString(ctx.url, "country", COUNTRY_RE);
    const page = paging(ctx.url);
    const levels = await ctx.env.DB.prepare(
        "SELECT id, name, content_hash FROM levels WHERE planet_id = ?1 AND ranking_enabled = 1 ORDER BY level_order",
    )
        .bind(planetId)
        .all<{ id: string; name: string; content_hash: string }>();
    if (levels.results.length === 0) throw Errors.notFound("PLANET_NOT_FOUND", "Aucun niveau classé pour cette planète.");
    const sql = `
      WITH lv AS (SELECT content_hash FROM levels WHERE planet_id = ?1 AND ranking_enabled = 1),
      sums AS (SELECT b.player_id, SUM(b.time_ms) AS total_time, COUNT(*) AS levels FROM player_bests b
               WHERE b.category = ?2 AND b.level_hash IN (SELECT content_hash FROM lv)
               GROUP BY b.player_id HAVING COUNT(*) = (SELECT COUNT(*) FROM lv)),
      ranked AS (SELECT sums.*, p.username, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility,
                   ROW_NUMBER() OVER (ORDER BY total_time, sums.player_id) AS world_rank
                 FROM sums JOIN players p ON p.id = sums.player_id WHERE p.account_status = 'active'),
      filtered AS (SELECT ranked.*, ROW_NUMBER() OVER (ORDER BY world_rank) AS rank FROM ranked
                   ${country ? "WHERE country_code = ?3 AND location_visibility <> 'hidden'" : ""})
      SELECT *, COUNT(*) OVER () AS total FROM filtered`;
    const binds: unknown[] = country ? [planetId, category, country] : [planetId, category];
    const n = binds.length;
    const rows = await ctx.env.DB.prepare(`${sql} ORDER BY rank LIMIT ?${n + 1} OFFSET ?${n + 2}`)
        .bind(...binds, page.limit, page.offset)
        .all<{
            player_id: string; username: string; avatar: string; avatar_custom_key: string | null; country_code: string | null;
            location_visibility: string; total_time: number; levels: number; world_rank: number; rank: number; total: number;
        }>();
    const toEntry = (r: (typeof rows.results)[number]) => ({
        rank: r.rank,
        world_rank: r.world_rank,
        player: {
            id: r.player_id,
            username: r.username,
            avatar: r.avatar,
            avatar_url: avatarUrl({ id: r.player_id, avatar: r.avatar, avatar_custom_key: r.avatar_custom_key }),
            country_code: r.location_visibility === "hidden" ? null : r.country_code,
        },
        total_time_ms: r.total_time,
        levels_completed: r.levels,
        is_me: viewer?.player.id === r.player_id,
    });
    let me = null;
    if (viewer) {
        const mine = await ctx.env.DB.prepare(`${sql} WHERE player_id = ?${n + 1}`).bind(...binds, viewer.player.id).first<(typeof rows.results)[number]>();
        me = { entry: mine ? toEntry(mine) : null, pending: null };
    }
    const total = rows.results[0]?.total ?? 0;
    return json(
        ctx,
        {
            board: { kind: "planet_total_time", planet_id: planetId, category, levels: levels.results.map((l) => ({ id: l.id, name: l.name })), country_code: country ?? null },
            entries: rows.results.map(toEntry),
            me,
        },
        { meta: { limit: page.limit, offset: page.offset, total, next_offset: page.offset + page.limit < total ? page.offset + page.limit : null } },
    );
}

export function assertCategory(c: string): Category {
    if (!(CATEGORIES as readonly string[]).includes(c)) throw new ApiError(400, "VALIDATION_ERROR", "Catégorie inconnue.");
    return c as Category;
}
