// Globe data. Players appear on the globe only if they chose location_visibility = 'globe',
// and only at the approximate centre of their country (never a precise position). Hidden
// players still count in ranks but have no marker.

import type { AuthInfo } from "./auth";
import { Errors, json, weekStart, type Ctx } from "./http";
import { resolveBoard } from "./levels";
import { avatarUrl } from "./model";
import { CATEGORIES, COUNTRY_RE, LEVEL_ID_RE, queryEnum, queryString } from "./validate";
import { PERIODS, type Period } from "./leaderboards";

interface MarkerRow {
    record_id: string;
    player_id: string;
    username: string;
    avatar: string;
    avatar_custom_key: string | null;
    country_code: string;
    latitude: number;
    longitude: number;
    time_ms: number;
    achieved_at: string;
    world_rank: number;
    level_id: string;
}

function intParam(ctx: Ctx, name: string, def: number, min: number, max: number): number {
    const raw = ctx.url.searchParams.get(name);
    if (raw === null || raw === "") return def;
    if (!/^\d{1,5}$/.test(raw)) throw Errors.validation([{ field: name, message: "entier attendu" }]);
    return Math.min(Math.max(Number(raw), min), max);
}

/** Ranked rows of one board with the location of globe-visible players. */
function markersSql(period: Period, countryFilter: boolean): string {
    const source =
        period === "all"
            ? `SELECT b.player_id, b.record_id, b.time_ms, b.achieved_at, b.level_id FROM player_bests b
               WHERE b.level_hash = ?1 AND b.category = ?2 AND (?3 IS NULL OR 1 = 1)`
            : `SELECT player_id, record_id, time_ms, achieved_at, level_id FROM (
                 SELECT r.player_id, r.id AS record_id, r.time_ms, r.created_at AS achieved_at, r.level_id,
                        ROW_NUMBER() OVER (PARTITION BY r.player_id ORDER BY r.time_ms, r.created_at) AS rn
                 FROM records r WHERE r.level_hash = ?1 AND r.category = ?2 AND r.verification_status = 'VERIFIED'
                   AND r.created_at >= ?3) WHERE rn = 1`;
    return `
      WITH src AS (${source}),
      ranked AS (
        SELECT s.*, p.username, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility,
               ROW_NUMBER() OVER (ORDER BY s.time_ms, s.achieved_at, s.player_id) AS world_rank
        FROM src s JOIN players p ON p.id = s.player_id WHERE p.account_status = 'active')
      SELECT r.record_id, r.player_id, r.username, r.avatar, r.avatar_custom_key, r.country_code, c.latitude, c.longitude,
             r.time_ms, r.achieved_at, r.world_rank, r.level_id
      FROM ranked r JOIN countries c ON c.code = r.country_code
      WHERE r.location_visibility = 'globe' AND c.latitude IS NOT NULL ${countryFilter ? "AND r.country_code = ?4" : ""}
      ORDER BY r.world_rank`;
}

function markerJson(r: MarkerRow, viewer: AuthInfo | null) {
    return {
        record_id: r.record_id,
        player_id: r.player_id,
        player_name: r.username,
        avatar: r.avatar,
        avatar_url: avatarUrl({ id: r.player_id, avatar: r.avatar, avatar_custom_key: r.avatar_custom_key }),
        country_code: r.country_code,
        // Approximate representation point of the country, never a personal position.
        latitude: r.latitude,
        longitude: r.longitude,
        time_ms: r.time_ms,
        rank: r.world_rank,
        level_id: r.level_id,
        achieved_at: r.achieved_at,
        is_me: viewer?.player.id === r.player_id,
    };
}

/** GET /globe/records: markers of a level board, or the podiums of every level. */
export async function globeRecords(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const category = queryEnum(ctx.url, "category", CATEGORIES, "ANY_PERCENT");
    const period = queryEnum(ctx.url, "period", PERIODS, "all");
    const limit = intParam(ctx, "limit", 300, 1, 1000);
    const levelId = queryString(ctx.url, "level_id", LEVEL_ID_RE);
    const ws = weekStart().toISOString();
    let markers: MarkerRow[] = [];
    if (levelId) {
        const board = await resolveBoard(ctx, levelId);
        const rows = await ctx.env.DB.prepare(`${markersSql(period, false)} LIMIT ?4`)
            .bind(board.version.content_hash, category, ws, limit)
            .all<MarkerRow>();
        markers = rows.results;
    } else {
        // Overview: the best players of every ranked level.
        const perLevel = intParam(ctx, "per_level", 10, 1, 100);
        const levels = await ctx.env.DB.prepare("SELECT id, content_hash FROM levels WHERE ranking_enabled = 1 ORDER BY planet_id, level_order")
            .all<{ id: string; content_hash: string }>();
        const stmts = levels.results.map((l) =>
            ctx.env.DB.prepare(`${markersSql(period, false)} LIMIT ?4`).bind(l.content_hash, category, ws, perLevel),
        );
        if (stmts.length) {
            const results = await ctx.env.DB.batch<MarkerRow>(stmts);
            for (const r of results) markers.push(...r.results);
        }
        markers = markers.slice(0, limit);
    }
    return json(ctx, {
        level_id: levelId ?? null,
        category,
        period,
        records: markers.map((m) => markerJson(m, viewer)),
        generated_at: new Date().toISOString(),
    });
}

/** GET /globe/records/{countryCode}: markers of one country (paginated by rank). */
export async function globeCountryRecords(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const cc = ctx.params.countryCode!.toUpperCase();
    if (!COUNTRY_RE.test(cc)) throw Errors.validation([{ field: "countryCode", message: "code pays ISO à 2 lettres attendu" }]);
    const levelId = queryString(ctx.url, "level_id", LEVEL_ID_RE);
    if (!levelId) throw Errors.validation([{ field: "level_id", message: "obligatoire" }]);
    const category = queryEnum(ctx.url, "category", CATEGORIES, "ANY_PERCENT");
    const period = queryEnum(ctx.url, "period", PERIODS, "all");
    const limit = intParam(ctx, "limit", 100, 1, 500);
    const board = await resolveBoard(ctx, levelId);
    const rows = await ctx.env.DB.prepare(`${markersSql(period, true)} LIMIT ?5`)
        .bind(board.version.content_hash, category, weekStart().toISOString(), cc, limit)
        .all<MarkerRow>();
    return json(ctx, {
        country_code: cc,
        level_id: levelId,
        category,
        period,
        records: rows.results.map((m) => markerJson(m, viewer)),
        generated_at: new Date().toISOString(),
    });
}

/** GET /globe/countries: aggregation per country (used when the globe is zoomed out). */
export async function globeCountries(ctx: Ctx): Promise<Response> {
    const category = queryEnum(ctx.url, "category", CATEGORIES, "ANY_PERCENT");
    const period = queryEnum(ctx.url, "period", PERIODS, "all");
    const levelId = queryString(ctx.url, "level_id", LEVEL_ID_RE);
    type Agg = { code: string; name_fr: string; name_en: string; latitude: number; longitude: number; players: number; best_time_ms: number | null; best_rank: number | null; best_player: string | null };
    let rows: Agg[];
    if (levelId) {
        const board = await resolveBoard(ctx, levelId);
        const res = await ctx.env.DB.prepare(
            `WITH m AS (${markersSql(period, false)}),
             agg AS (SELECT country_code, COUNT(*) AS players, MIN(world_rank) AS best_rank FROM m GROUP BY country_code)
             SELECT c.code, c.name_fr, c.name_en, c.latitude, c.longitude, agg.players, agg.best_rank,
                    m.time_ms AS best_time_ms, m.username AS best_player
             FROM agg JOIN countries c ON c.code = agg.country_code
             JOIN m ON m.country_code = agg.country_code AND m.world_rank = agg.best_rank
             ORDER BY agg.best_rank`,
        )
            .bind(board.version.content_hash, category, weekStart().toISOString())
            .all<Agg>();
        rows = res.results;
    } else {
        const res = await ctx.env.DB.prepare(
            `SELECT c.code, c.name_fr, c.name_en, c.latitude, c.longitude, COUNT(DISTINCT p.id) AS players,
                    NULL AS best_time_ms, NULL AS best_rank, NULL AS best_player
             FROM players p JOIN countries c ON c.code = p.country_code
             WHERE p.location_visibility = 'globe' AND p.account_status = 'active' AND c.latitude IS NOT NULL
               AND EXISTS (SELECT 1 FROM player_bests b WHERE b.player_id = p.id AND b.category = ?1)
             GROUP BY c.code ORDER BY players DESC`,
        )
            .bind(category)
            .all<Agg>();
        rows = res.results;
    }
    return json(ctx, {
        level_id: levelId ?? null,
        category,
        period,
        countries: rows.map((r) => ({
            country_code: r.code,
            name_fr: r.name_fr,
            name_en: r.name_en,
            latitude: r.latitude,
            longitude: r.longitude,
            players: r.players,
            best_time_ms: r.best_time_ms,
            best_rank: r.best_rank,
            best_player: r.best_player,
        })),
        generated_at: new Date().toISOString(),
    });
}
