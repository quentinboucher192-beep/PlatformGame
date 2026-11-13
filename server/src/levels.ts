// Level registry lookups. A leaderboard is always bound to ONE content hash: records of
// different revisions are never mixed.

import { ApiError, Errors, type Ctx } from "./http";
import type { LevelRow, LevelVersionRow } from "./model";
import { HASH_RE, LEVEL_ID_RE, queryString } from "./validate";

export interface Board {
    level: LevelRow;
    version: LevelVersionRow;
}

export async function getLevel(ctx: Ctx, levelId: string): Promise<LevelRow> {
    if (!LEVEL_ID_RE.test(levelId)) throw Errors.validation([{ field: "level_id", message: "format invalide" }]);
    const level = await ctx.env.DB.prepare("SELECT * FROM levels WHERE id = ?1").bind(levelId).first<LevelRow>();
    if (!level) throw Errors.notFound("LEVEL_NOT_FOUND", "Niveau inconnu du serveur.");
    return level;
}

/**
 * Board of a level: the current revision by default, or the revision designated by the
 * `level_hash` / `level_version` query parameters (to consult legacy leaderboards).
 */
export async function resolveBoard(ctx: Ctx, levelId: string): Promise<Board> {
    const level = await getLevel(ctx, levelId);
    const hash = queryString(ctx.url, "level_hash", HASH_RE);
    const versionRaw = queryString(ctx.url, "level_version", /^\d{1,6}$/);
    let version: LevelVersionRow | null;
    if (hash)
        version = await ctx.env.DB.prepare("SELECT * FROM level_versions WHERE level_id = ?1 AND content_hash = ?2")
            .bind(level.id, hash)
            .first<LevelVersionRow>();
    else if (versionRaw)
        version = await ctx.env.DB.prepare("SELECT * FROM level_versions WHERE level_id = ?1 AND version = ?2")
            .bind(level.id, Number(versionRaw))
            .first<LevelVersionRow>();
    else
        version = await ctx.env.DB.prepare("SELECT * FROM level_versions WHERE level_id = ?1 AND content_hash = ?2")
            .bind(level.id, level.content_hash)
            .first<LevelVersionRow>();
    if (!version) throw Errors.notFound("LEVEL_VERSION_NOT_FOUND", "Version de niveau inconnue.");
    return { level, version };
}

export function versionJson(level: LevelRow, v: LevelVersionRow) {
    return {
        level_id: level.id,
        name: level.name,
        planet_id: level.planet_id,
        planet_name: level.planet_name,
        version: v.version,
        content_hash: v.content_hash,
        compatibility_status: v.compatibility_status,
        is_current: v.content_hash === level.content_hash,
        ranking_enabled: level.ranking_enabled === 1,
    };
}

export async function listLevels(ctx: Ctx) {
    const rows = await ctx.env.DB.prepare(
        `SELECT l.*, v.version AS v_version, v.min_game_version, v.total_collectibles, v.time_limit_ms
         FROM levels l JOIN level_versions v ON v.level_id = l.id AND v.content_hash = l.content_hash
         ORDER BY l.planet_id, l.level_order, l.id`,
    ).all<LevelRow & { v_version: number; min_game_version: string; total_collectibles: number; time_limit_ms: number }>();
    return rows.results.map((l) => ({
        id: l.id,
        name: l.name,
        planet_id: l.planet_id,
        planet_name: l.planet_name,
        order: l.level_order,
        version: l.v_version,
        content_hash: l.content_hash,
        ranking_enabled: l.ranking_enabled === 1,
        min_game_version: l.min_game_version,
        total_collectibles: l.total_collectibles,
        time_limit_ms: l.time_limit_ms,
    }));
}

export function ensureRankingEnabled(level: LevelRow): void {
    if (level.ranking_enabled !== 1) throw new ApiError(403, "RANKING_DISABLED", "Le classement de ce niveau est désactivé.");
}
