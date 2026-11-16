// 2.50: « Migrer vers un nouveau compte » (docs/online/DEVICES.md). An account is bound to one
// computer (2.47, devices.ts): a player who moves to a new computer creates a new account there,
// then brings the old account's server data to it.
//
// * On the OLD computer, the old account makes a one-time code (24 h, stored hashed only).
// * On the NEW computer, the new account sends the old account's username, password and code:
//   the server checks them (failures: one generic answer, 5 per 10 minutes per new account and
//   per old account), then, in ONE D1 batch, merges the old account's data into the new one
//   (« le meilleur des deux côtés est gardé ») and closes the old account (no login, sessions
//   revoked, computer released, username reserved 30 days). The old computer's latest copy of
//   the progression (devices.ts) becomes a progression to restore on the new computer.
// * An admin (ADMINISTRATION › Joueurs) previews and runs a migration, « merge » or « replace »,
//   of chosen parts, after a full backup made by the existing backup mechanism (backups.ts).
//
// Never trust the client: the player is always the session (the new account); the old account
// is identified by its password AND its code, never by an id sent by the game.

import achievementCatalog from "./achievementCatalog.json";
import { requireAdminRole } from "./adminRoles";
import { audit, checkPassword, credentialsOf, requireAuth, type AuthInfo } from "./auth";
import { createBackup, maintenanceActive } from "./backups";
import { COSMETIC_CATEGORIES, defaultItem, revalidateCosmetics, type CosmeticCategory } from "./cosmetics";
import { randomBytes, secretsEqual, sha256Hex } from "./crypto";
import { latestCopyRow, parseSummary, playerOr404, type ProgressCopyRow } from "./devices";
import { ApiError, Errors, json, log, nowIso, readJsonObject, type Bindings, type Ctx } from "./http";
import type { PlayerRow } from "./model";
import { notify } from "./notifications";
import { RULES, assertUnderLimit, countFailure, rateLimit } from "./ratelimit";
import { clearSpectateRoom } from "./spectate";
import { Validator, normalizeUsername } from "./validate";

// ------------------------------------------------------------------ constants

export const MIGRATION_ITEMS = ["records", "achievements", "rewards", "playtime", "levels", "progress"] as const;
export type MigrationItem = (typeof MIGRATION_ITEMS)[number];
export const MIGRATION_MODES = ["merge", "replace"] as const;
export type MigrationMode = (typeof MIGRATION_MODES)[number];
/** With a code, a player brings everything but the community levels and projects (an admin's « levels »). */
export const SELF_SERVICE_ITEMS: readonly MigrationItem[] = ["records", "achievements", "rewards", "playtime", "progress"];
/** What an admin types to confirm a migration. */
export const ADMIN_CONFIRM = "MIGRER";

/** 9 characters without 0/O, 1/I/L (31 symbols, ~44.6 bits), shown as K8R-4TZ-9QW. */
const CODE_ALPHABET = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
const CODE_LENGTH = 9;
export const CODE_TTL_MS = 24 * 3600_000;
/** Replays of the new account's replaced rows (« replace ») removed per request (the rest stays, reported). */
const MAX_RETIRED_FILES = 250;

const STARS: Record<string, number> = (achievementCatalog as { stars: Record<string, number> }).stars;
const isObject = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);

// ------------------------------------------------------------------ the code

function newCode(): string {
    let out = "";
    while (out.length < CODE_LENGTH)
        for (const b of randomBytes(16)) {
            if (b >= 248) continue; // 248 = 8 × 31: every symbol equally likely
            out += CODE_ALPHABET[b % CODE_ALPHABET.length];
            if (out.length === CODE_LENGTH) break;
        }
    return `${out.slice(0, 3)}-${out.slice(3, 6)}-${out.slice(6)}`;
}

/** What the player typed: case, spaces and dashes ignored; null when it cannot be a code. */
export function normalizeCode(raw: string): string | null {
    const s = raw.toUpperCase().replace(/[\s-]/g, "");
    return s.length === CODE_LENGTH && [...s].every((c) => CODE_ALPHABET.includes(c)) ? s : null;
}

/** Never stored in clear: SHA-256 of the account and the normalised code. */
async function codeHash(playerId: string, normalized: string): Promise<string> {
    return sha256Hex(`cosmic.migration:${playerId}:${normalized}`);
}

interface CodeRow {
    code_hash: string;
    expires_at: string;
    used_at: string | null;
}

async function codeOf(ctx: Ctx, playerId: string): Promise<CodeRow | null> {
    return ctx.env.DB.prepare("SELECT code_hash, expires_at, used_at FROM account_migration_codes WHERE player_id = ?1").bind(playerId).first<CodeRow>();
}

const isActiveCode = (c: CodeRow | null): c is CodeRow => !!c && !c.used_at && Date.parse(c.expires_at) > Date.now();

/** POST /players/me/migration: a new one-time code (24 h) for this account; it replaces the previous one. */
export async function createCode(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.migrationCode, "player:" + me);
    const code = newCode();
    const now = Date.now();
    const expires = new Date(now + CODE_TTL_MS).toISOString();
    await ctx.env.DB.prepare(
        `INSERT INTO account_migration_codes (player_id, code_hash, created_at, expires_at) VALUES (?1, ?2, ?3, ?4)
         ON CONFLICT (player_id) DO UPDATE SET code_hash = excluded.code_hash, created_at = excluded.created_at,
           expires_at = excluded.expires_at, used_at = NULL, used_by = NULL, claim_id = NULL`,
    )
        .bind(me, await codeHash(me, normalizeCode(code)!), new Date(now).toISOString(), expires)
        .run();
    await audit(ctx, me, "migration_code_created", { expires_at: expires });
    return json(ctx, { code, expires_at: expires }, { status: 201 });
}

/** GET /players/me/migration: whether a code is active, until when (never the code itself). */
export async function getCode(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const c = await codeOf(ctx, auth.player.id);
    const active = isActiveCode(c);
    return json(ctx, { active, expires_at: active ? c.expires_at : null });
}

/** DELETE /players/me/migration: cancels the active code. */
export async function cancelCode(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const res = await ctx.env.DB.prepare("DELETE FROM account_migration_codes WHERE player_id = ?1 AND used_at IS NULL").bind(auth.player.id).run();
    if (res.meta.changes) await audit(ctx, auth.player.id, "migration_code_cancelled");
    return json(ctx, { cancelled: res.meta.changes > 0, active: false });
}

// ------------------------------------------------------------------ counts (preview, answers)

/** What a player has on the server, by part of a migration. */
export async function countsOf(ctx: Ctx, playerId: string, copy: ProgressCopyRow | null) {
    const c = await ctx.env.DB.prepare(
        `SELECT
           (SELECT COUNT(*) FROM records WHERE player_id = ?1) AS records,
           (SELECT COUNT(*) FROM records WHERE player_id = ?1 AND verification_status = 'VERIFIED') AS verified,
           (SELECT COUNT(*) FROM player_bests WHERE player_id = ?1) AS bests,
           (SELECT COUNT(*) FROM replay_metadata WHERE player_id = ?1) AS replays,
           (SELECT COUNT(*) FROM community_records WHERE player_id = ?1) AS community_records,
           (SELECT COUNT(*) FROM race_runs WHERE player_id = ?1) AS race_runs,
           (SELECT COUNT(*) FROM challenge_runs WHERE player_id = ?1) AS challenge_runs,
           (SELECT COUNT(*) FROM challenge_attempts WHERE player_id = ?1) AS challenge_attempts,
           (SELECT COALESCE(MAX(count), 0) FROM player_achievements WHERE player_id = ?1) AS achievements,
           (SELECT COALESCE(MAX(stars), 0) FROM player_achievements WHERE player_id = ?1) AS stars,
           (SELECT COALESCE(MAX(CASE WHEN json_valid(ids) THEN json_array_length(ids) ELSE 0 END), 0) FROM player_badges WHERE player_id = ?1) AS badges,
           (SELECT COUNT(*) FROM player_won_rewards WHERE player_id = ?1) AS won,
           (SELECT COUNT(*) FROM player_won_rewards WHERE player_id = ?1 AND reward_kind = 'title') AS titles,
           (SELECT COUNT(*) FROM player_won_rewards WHERE player_id = ?1 AND reward_kind = 'suit') AS creations,
           (SELECT COUNT(*) FROM player_won_rewards WHERE player_id = ?1 AND reward_kind = 'item') AS items,
           (SELECT COUNT(*) FROM player_cosmetics WHERE player_id = ?1) AS look,
           (SELECT COALESCE(SUM(seconds), 0) FROM player_playtime WHERE player_id = ?1) AS seconds,
           (SELECT COUNT(*) FROM player_playtime WHERE player_id = ?1) AS playtime_levels,
           (SELECT COUNT(*) FROM community_levels WHERE author_id = ?1 AND status = 'published') AS published,
           (SELECT COUNT(*) FROM community_levels WHERE author_id = ?1 AND status <> 'removed') AS levels,
           (SELECT COUNT(*) FROM projects WHERE owner_id = ?1 AND deleted_at IS NULL) AS projects`,
    )
        .bind(playerId)
        .first<Record<string, number>>();
    const n = (k: string) => c?.[k] ?? 0;
    // Boss statistics are kept by the game only: what its latest copy of the progression says.
    const summary = copy ? parseSummary(copy.summary) : null;
    const bosses = isObject(summary) && typeof summary.bosses === "number" ? summary.bosses : null;
    return {
        records: {
            records: n("records"),
            verified: n("verified"),
            bests: n("bests"),
            replays: n("replays"),
            community_records: n("community_records"),
            race_runs: n("race_runs"),
            challenge_runs: n("challenge_runs"),
            challenge_attempts: n("challenge_attempts"),
        },
        achievements: { count: n("achievements"), stars: n("stars"), badges: n("badges") },
        rewards: { won: n("won"), titles: n("titles"), creations: n("creations"), items: n("items"), look: n("look") > 0 },
        playtime: { seconds: n("seconds"), levels: n("playtime_levels"), boss_stats: bosses },
        levels: { published: n("published"), total: n("levels"), projects: n("projects") },
    };
}

// ------------------------------------------------------------------ the migration itself

export interface MigrationPlan {
    id: string;
    source: PlayerRow;
    target: PlayerRow;
    mode: MigrationMode;
    items: readonly MigrationItem[];
    closeSource: boolean;
    /** The administrator (null: the player, with a code). */
    by: { id: string; username: string } | null;
    /** The backup made just before (an admin's migration). */
    backupId: string | null;
    /** Self-service: the hash of the code, claimed by the batch itself (used once). */
    codeHash: string | null;
    /** What a failed check inside the batch means for the caller (nothing was changed). */
    conflict: () => ApiError;
}

interface RestoreRow {
    player_id: string;
    migration_id: string;
    from_player_id: string | null;
    from_username: string;
    file_key: string;
    size: number;
    sha256: string;
    summary: string;
    game_version: string;
    copy_created_at: string;
    device_name: string | null;
    created_at: string;
    postponed_at: string | null;
}

export interface MigrationOutcome {
    restore: RestoreRow | null;
    files: { removed: number; kept: number };
}

function idsOf(text: string | null | undefined): string[] {
    try {
        const v = JSON.parse(text ?? "[]") as unknown;
        return Array.isArray(v) ? v.filter((x): x is string => typeof x === "string") : [];
    } catch {
        return [];
    }
}

/** Ids of the server's catalogue only (as achievements.ts), sorted, with their stars. */
function achievementsOf(ids: Iterable<string>) {
    const known = [...new Set([...ids].filter((id) => STARS[id] !== undefined))].sort();
    return { ids: known, stars: known.reduce((s, id) => s + STARS[id]!, 0), count: known.length };
}

type Look = Record<CosmeticCategory, string>;

/**
 * Merges (or replaces) the source's data into the target in ONE D1 batch, then closes the
 * source if asked. The first statements claim the code (self-service) and write the migration
 * with a CHECK that fails, rolling everything back, unless both accounts are still active, the
 * code was claimed by this migration and an admin is left. Callers check everything before.
 */
export async function performMigration(ctx: Ctx, plan: MigrationPlan): Promise<MigrationOutcome> {
    const db = ctx.env.DB;
    const S = plan.source.id;
    const T = plan.target.id;
    const now = nowIso();
    const has = (i: MigrationItem) => plan.items.includes(i);
    const replace = plan.mode === "replace";
    // ?1 = the source, ?2 = the target, then `extra` from ?3: exactly as many values as the SQL uses.
    const st = (sql: string, ...extra: unknown[]): D1PreparedStatement => {
        const n = Math.max(0, ...[...sql.matchAll(/\?(\d+)/g)].map((m) => Number(m[1])));
        return db.prepare(sql).bind(...[S, T, ...extra].slice(0, n));
    };
    const stmts: D1PreparedStatement[] = [];

    if (plan.codeHash)
        stmts.push(
            st(
                `UPDATE account_migration_codes SET used_at = ?4, used_by = ?2, claim_id = ?3
                 WHERE player_id = ?1 AND code_hash = ?5 AND used_at IS NULL AND expires_at > ?4`,
                plan.id,
                now,
                plan.codeHash,
            ),
        );
    const closingAdmin = plan.closeSource && plan.source.role === "admin";
    stmts.push(
        st(
            `INSERT INTO account_migrations (id, source_id, source_username, target_id, target_username, mode, items, close_source, by_player,
               by_username, backup_id, created_at, checked)
             VALUES (?3, ?1, ?4, ?2, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12,
               CASE WHEN (SELECT COUNT(*) FROM players WHERE id IN (?1, ?2) AND account_status = 'active' AND closed_at IS NULL) = 2
                     AND (?13 = 0 OR EXISTS (SELECT 1 FROM account_migration_codes WHERE player_id = ?1 AND claim_id = ?3))
                     AND (?14 = 0 OR EXISTS (SELECT 1 FROM players WHERE role = 'admin' AND account_status = 'active' AND id <> ?1))
                    THEN 1 ELSE 0 END)`,
            plan.id,
            plan.source.username,
            plan.target.username,
            plan.mode,
            JSON.stringify(plan.items),
            plan.closeSource ? 1 : 0,
            plan.by?.id ?? null,
            plan.by?.username ?? null,
            plan.backupId,
            now,
            plan.codeHash ? 1 : 0,
            closingAdmin ? 1 : 0,
        ),
    );

    // Records and replays: every board (campaign, community levels, races, challenges). A row
    // whose unique key (player, attempt) the target already has stays on the source.
    let retired: string[] = [];
    if (has("records")) {
        if (replace) {
            retired = (
                await db
                    .prepare(
                        `SELECT storage_key AS k FROM replay_metadata WHERE player_id = ?1
                         UNION ALL SELECT replay_key FROM race_runs WHERE player_id = ?1 AND replay_key IS NOT NULL
                         UNION ALL SELECT replay_key FROM community_records WHERE player_id = ?1 AND replay_key IS NOT NULL
                         UNION ALL SELECT replay_key FROM challenge_runs WHERE player_id = ?1 AND replay_key IS NOT NULL
                         UNION ALL SELECT replay_key FROM challenge_attempts WHERE player_id = ?1 AND replay_key IS NOT NULL`,
                    )
                    .bind(T)
                    .all<{ k: string }>()
            ).results.map((r) => r.k);
            stmts.push(
                // Its bests and replays follow the records (ON DELETE CASCADE).
                st("DELETE FROM records WHERE player_id = ?2"),
                st("DELETE FROM race_runs WHERE player_id = ?2"),
                st("DELETE FROM community_records WHERE player_id = ?2"),
                st("DELETE FROM community_level_players WHERE player_id = ?2"),
                st("DELETE FROM challenge_runs WHERE player_id = ?2"),
                st("DELETE FROM challenge_attempts WHERE player_id = ?2"),
            );
        }
        stmts.push(
            st(
                `UPDATE records SET player_id = ?2 WHERE player_id = ?1
                   AND NOT EXISTS (SELECT 1 FROM records x WHERE x.player_id = ?2 AND x.attempt_id = records.attempt_id AND x.category = records.category)`,
            ),
            st(
                `UPDATE replay_metadata SET player_id = ?2 WHERE player_id = ?1 AND record_id IN (SELECT id FROM records WHERE player_id = ?2)
                   AND NOT EXISTS (SELECT 1 FROM replay_metadata x WHERE x.player_id = ?2 AND x.attempt_id = replay_metadata.attempt_id)`,
            ),
            // Personal bests (one per player, level revision and category): the better time wins
            // (an equal time: the earlier one; still equal: the target's).
            st(
                `DELETE FROM player_bests WHERE player_id = ?2 AND EXISTS (
                   SELECT 1 FROM player_bests s WHERE s.player_id = ?1 AND s.level_hash = player_bests.level_hash AND s.category = player_bests.category
                     AND s.record_id IN (SELECT id FROM records WHERE player_id = ?2)
                     AND (s.time_ms < player_bests.time_ms OR (s.time_ms = player_bests.time_ms AND s.achieved_at < player_bests.achieved_at)))`,
            ),
            st(
                `UPDATE player_bests SET player_id = ?2 WHERE player_id = ?1 AND record_id IN (SELECT id FROM records WHERE player_id = ?2)
                   AND NOT EXISTS (SELECT 1 FROM player_bests t WHERE t.player_id = ?2 AND t.level_hash = player_bests.level_hash AND t.category = player_bests.category)`,
            ),
            st("DELETE FROM player_bests WHERE player_id = ?1 AND record_id IN (SELECT id FROM records WHERE player_id = ?2)"),
            // Races of the EXTRÊME levels: the attempts, « premier à vérifier ».
            st(
                `UPDATE race_runs SET player_id = ?2 WHERE player_id = ?1
                   AND NOT EXISTS (SELECT 1 FROM race_runs x WHERE x.player_id = ?2 AND x.attempt_id = race_runs.attempt_id)`,
            ),
            st("UPDATE race_victors SET player_id = ?2 WHERE player_id = ?1"),
            st("UPDATE community_levels SET verified_by = ?2 WHERE verified_by = ?1"),
            // Community levels: the records, the levels played (the earliest date; never one's own level).
            st(
                `UPDATE community_records SET player_id = ?2 WHERE player_id = ?1
                   AND NOT EXISTS (SELECT 1 FROM community_records x WHERE x.player_id = ?2 AND x.attempt_id = community_records.attempt_id)`,
            ),
            st(
                `INSERT INTO community_level_players (level_id, player_id, first_at)
                 SELECT level_id, ?2, first_at FROM community_level_players WHERE player_id = ?1
                 ON CONFLICT (level_id, player_id) DO UPDATE SET first_at = min(community_level_players.first_at, excluded.first_at)`,
            ),
            st("DELETE FROM community_level_players WHERE player_id = ?1"),
            st("DELETE FROM community_level_players WHERE player_id = ?2 AND level_id IN (SELECT id FROM community_levels WHERE author_id = ?2)"),
            // Challenges: the runs and the « Plus loin » attempts (the rewards they unlock follow).
            st(
                `UPDATE challenge_runs SET player_id = ?2 WHERE player_id = ?1
                   AND NOT EXISTS (SELECT 1 FROM challenge_runs x WHERE x.player_id = ?2 AND x.attempt_id = challenge_runs.attempt_id)`,
            ),
            st(
                `UPDATE challenge_attempts SET player_id = ?2 WHERE player_id = ?1
                   AND NOT EXISTS (SELECT 1 FROM challenge_attempts x WHERE x.player_id = ?2 AND x.attempt_id = challenge_attempts.attempt_id)`,
            ),
            // The trust the verified records earned (records.ts) follows them: the higher of the two.
            st(
                replace
                    ? "UPDATE players SET trust_level = (SELECT trust_level FROM players WHERE id = ?1) WHERE id = ?2"
                    : "UPDATE players SET trust_level = max(trust_level, (SELECT trust_level FROM players WHERE id = ?1)) WHERE id = ?2",
            ),
        );
    }

    // Achievements (declared by the game): the union of both lists; the profile badges: the
    // target's choice, or the source's when the target has none.
    if (has("achievements")) {
        const rows = await db
            .prepare("SELECT player_id, ids FROM player_achievements WHERE player_id IN (?1, ?2)")
            .bind(S, T)
            .all<{ player_id: string; ids: string }>();
        const src = rows.results.find((r) => r.player_id === S);
        const tgt = rows.results.find((r) => r.player_id === T);
        if (src) {
            const a = achievementsOf(replace ? idsOf(src.ids) : [...idsOf(src.ids), ...idsOf(tgt?.ids)]);
            stmts.push(
                st(
                    `INSERT INTO player_achievements (player_id, ids, stars, count, updated_at) VALUES (?2, ?3, ?4, ?5, ?6)
                     ON CONFLICT (player_id) DO UPDATE SET ids = excluded.ids, stars = excluded.stars, count = excluded.count, updated_at = excluded.updated_at`,
                    JSON.stringify(a.ids),
                    a.stars,
                    a.count,
                    now,
                ),
            );
        } else if (replace) stmts.push(st("DELETE FROM player_achievements WHERE player_id = ?2"));
        stmts.push(st("DELETE FROM player_achievements WHERE player_id = ?1"));
        const badges = await db
            .prepare("SELECT player_id, ids FROM player_badges WHERE player_id IN (?1, ?2)")
            .bind(S, T)
            .all<{ player_id: string; ids: string }>();
        const sb = badges.results.find((r) => r.player_id === S);
        const tb = badges.results.find((r) => r.player_id === T);
        if (sb && (replace || idsOf(tb?.ids).length === 0))
            stmts.push(
                st(
                    `INSERT INTO player_badges (player_id, ids, updated_at) VALUES (?2, ?3, ?4)
                     ON CONFLICT (player_id) DO UPDATE SET ids = excluded.ids, updated_at = excluded.updated_at`,
                    JSON.stringify(idsOf(sb.ids)),
                    now,
                ),
            );
        else if (!sb && replace) stmts.push(st("DELETE FROM player_badges WHERE player_id = ?2"));
        stmts.push(st("DELETE FROM player_badges WHERE player_id = ?1"));
    }

    // Rewards won (titles, suits created by an admin, items): the union, the earliest date; the
    // look worn: the target's, each piece left at its default taking the source's.
    if (has("rewards")) {
        if (replace) stmts.push(st("DELETE FROM player_won_rewards WHERE player_id = ?2"));
        stmts.push(
            st(
                `INSERT INTO player_won_rewards (player_id, challenge_id, reward_kind, reward_ref, granted_at)
                 SELECT ?2, challenge_id, reward_kind, reward_ref, granted_at FROM player_won_rewards WHERE player_id = ?1
                 ON CONFLICT (player_id, challenge_id, reward_kind, reward_ref) DO UPDATE SET granted_at = min(player_won_rewards.granted_at, excluded.granted_at)`,
            ),
            st("DELETE FROM player_won_rewards WHERE player_id = ?1"),
        );
        const looks = await db
            .prepare("SELECT player_id, suit, trail, frame, title, victory FROM player_cosmetics WHERE player_id IN (?1, ?2)")
            .bind(S, T)
            .all<Look & { player_id: string }>();
        const sl = looks.results.find((r) => r.player_id === S);
        const tl = looks.results.find((r) => r.player_id === T);
        if (sl) {
            const look = {} as Look;
            for (const c of COSMETIC_CATEGORIES) look[c] = !replace && tl && tl[c] !== defaultItem(c).id ? tl[c] : sl[c];
            stmts.push(
                st(
                    `INSERT INTO player_cosmetics (player_id, suit, trail, frame, title, victory, updated_at) VALUES (?2, ?3, ?4, ?5, ?6, ?7, ?8)
                     ON CONFLICT (player_id) DO UPDATE SET suit = excluded.suit, trail = excluded.trail, frame = excluded.frame,
                       title = excluded.title, victory = excluded.victory, updated_at = excluded.updated_at`,
                    look.suit,
                    look.trail,
                    look.frame,
                    look.title,
                    look.victory,
                    now,
                ),
            );
        } else if (replace) stmts.push(st("DELETE FROM player_cosmetics WHERE player_id = ?2"));
        stmts.push(st("DELETE FROM player_cosmetics WHERE player_id = ?1"));
    }

    // Play time: summed per level.
    if (has("playtime")) {
        if (replace) stmts.push(st("DELETE FROM player_playtime WHERE player_id = ?2"));
        stmts.push(
            st(
                `INSERT INTO player_playtime (player_id, level_key, seconds, updated_at)
                 SELECT ?2, level_key, seconds, updated_at FROM player_playtime WHERE player_id = ?1
                 ON CONFLICT (player_id, level_key) DO UPDATE SET seconds = player_playtime.seconds + excluded.seconds,
                   updated_at = max(player_playtime.updated_at, excluded.updated_at)`,
            ),
            st("DELETE FROM player_playtime WHERE player_id = ?1"),
        );
    }

    // Community levels and projects (an admin's « levels »): their creator becomes the target.
    // « replace » never deletes the target's own levels or projects: other players' records,
    // comments and work are on them.
    if (has("levels"))
        stmts.push(
            st("UPDATE community_levels SET author_id = ?2 WHERE author_id = ?1"),
            st("DELETE FROM community_level_players WHERE player_id = ?2 AND level_id IN (SELECT id FROM community_levels WHERE author_id = ?2)"),
            st("UPDATE challenges SET featured_player_id = ?2 WHERE featured_player_id = ?1"),
            st("UPDATE project_members SET role = 'owner' WHERE player_id = ?2 AND project_id IN (SELECT id FROM projects WHERE owner_id = ?1)"),
            st(
                `UPDATE projects SET member_count = member_count - 1 WHERE owner_id = ?1
                   AND EXISTS (SELECT 1 FROM project_members m WHERE m.project_id = projects.id AND m.player_id = ?2)`,
            ),
            st(
                `DELETE FROM project_members WHERE player_id = ?1 AND project_id IN (SELECT id FROM projects WHERE owner_id = ?1)
                   AND EXISTS (SELECT 1 FROM project_members m WHERE m.project_id = project_members.project_id AND m.player_id = ?2)`,
            ),
            st("UPDATE project_members SET player_id = ?2 WHERE player_id = ?1 AND project_id IN (SELECT id FROM projects WHERE owner_id = ?1)"),
            st("DELETE FROM project_join_requests WHERE player_id = ?2 AND project_id IN (SELECT id FROM projects WHERE owner_id = ?1)"),
            st("UPDATE projects SET owner_id = ?2 WHERE owner_id = ?1"),
        );

    // The progression to restore on the new computer: the source's latest copy, copied for the
    // target (its own file: the source's copies may be pruned or deleted later).
    let restore: RestoreRow | null = null;
    let previousRestoreKey: string | null = null;
    if (has("progress")) {
        previousRestoreKey =
            (await db.prepare("SELECT file_key FROM progress_restores WHERE player_id = ?1").bind(T).first<{ file_key: string }>())?.file_key ?? null;
        const copy = await latestCopyRow(ctx, S);
        const obj = copy ? await ctx.env.STORAGE.get(copy.file_key) : null;
        if (copy && !obj) log("warn", "migration_copy_missing", { migration_id: plan.id, request_id: ctx.requestId });
        if (copy && obj) {
            const key = `progress/${T}/restore/${plan.id}.json`;
            await ctx.env.STORAGE.put(key, await obj.arrayBuffer(), {
                httpMetadata: { contentType: "application/json; charset=utf-8" },
                customMetadata: { sha256: copy.sha256 },
            });
            restore = {
                player_id: T,
                migration_id: plan.id,
                from_player_id: S,
                from_username: plan.source.username,
                file_key: key,
                size: copy.size,
                sha256: copy.sha256,
                summary: copy.summary,
                game_version: copy.game_version,
                copy_created_at: copy.created_at,
                device_name: copy.device_name ?? plan.source.device_name ?? null,
                created_at: now,
                postponed_at: null,
            };
            stmts.push(
                st(
                    `INSERT INTO progress_restores (player_id, migration_id, from_player_id, from_username, file_key, size, sha256, summary, game_version,
                       copy_created_at, device_name, created_at, postponed_at)
                     VALUES (?2, ?3, ?1, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, NULL)
                     ON CONFLICT (player_id) DO UPDATE SET migration_id = excluded.migration_id, from_player_id = excluded.from_player_id,
                       from_username = excluded.from_username, file_key = excluded.file_key, size = excluded.size, sha256 = excluded.sha256,
                       summary = excluded.summary, game_version = excluded.game_version, copy_created_at = excluded.copy_created_at,
                       device_name = excluded.device_name, created_at = excluded.created_at, postponed_at = NULL`,
                    plan.id,
                    restore.from_username,
                    key,
                    restore.size,
                    restore.sha256,
                    restore.summary,
                    restore.game_version,
                    restore.copy_created_at,
                    restore.device_name,
                    now,
                ),
            );
        } else if (replace) stmts.push(st("DELETE FROM progress_restores WHERE player_id = ?2"));
    }

    // The source closed: no login (« suspended » + closed_at), sessions revoked, its computer
    // released, no admin role, its code used; its username stays reserved 30 days.
    if (plan.closeSource)
        stmts.push(
            st(
                `UPDATE players SET account_status = 'suspended', closed_at = ?3, merged_into = ?2, role = 'player', device_id = NULL,
                   device_name = NULL, device_bound_at = NULL, device_rebind = 0, presence_state = NULL, presence_at = NULL
                 WHERE id = ?1`,
                now,
            ),
            st("UPDATE player_sessions SET revoked_at = ?3 WHERE player_id = ?1 AND revoked_at IS NULL", now),
            st("UPDATE account_migration_codes SET used_at = COALESCE(used_at, ?3) WHERE player_id = ?1", now),
        );

    try {
        await db.batch(stmts);
    } catch (e) {
        if (restore) await ctx.env.STORAGE.delete(restore.file_key).catch(() => undefined);
        if (/CHECK constraint failed/i.test(String(e))) throw plan.conflict();
        throw e;
    }

    // After the batch: files, derived states, notifications. The migration is done: a failure
    // here is logged, never answered as if nothing had happened (a retry would find the code used).
    const after = async (step: string, run: () => Promise<unknown>) => {
        try {
            await run();
        } catch (e) {
            log("error", "migration_after_failed", { migration_id: plan.id, step, error: e instanceof Error ? e.message : String(e) });
        }
    };
    if (previousRestoreKey && previousRestoreKey !== restore?.file_key && (restore || replace))
        await after("previous_restore", () => ctx.env.STORAGE.delete(previousRestoreKey!));
    let files = { removed: 0, kept: retired.length };
    await after("files", async () => {
        files = await retireFiles(ctx.env, plan.backupId, retired);
    });
    // What the merged achievements and results no longer unlock is taken off (never the other way).
    if (has("rewards") || has("achievements") || has("records")) await after("cosmetics", () => revalidateCosmetics(ctx, T));
    if (plan.closeSource) await after("spectate", () => clearSpectateRoom(ctx.env, S));
    const by = plan.by ? "admin" : "player";
    await after("notify_target", () =>
        notify(ctx, T, "security", {
            data: {
                reason: "migration_in",
                from_username: plan.source.username,
                migration_id: plan.id,
                by,
                ...(restore ? { action: "progress_restore" } : {}),
            },
        }),
    );
    await after("notify_source", () =>
        notify(ctx, S, "security", {
            data: { reason: "migration_out", to_username: plan.target.username, migration_id: plan.id, closed: plan.closeSource, by },
        }),
    );
    if (plan.closeSource)
        await after("audit_source", () => audit(ctx, S, "account_closed_migrated", { migration_id: plan.id, to: T, to_username: plan.target.username }));
    return { restore, files };
}

/**
 * The replays of the target's rows a « replace » deleted: saved into the backup made just
 * before (backups/<id>/objects/<key>, as an import saves what it replaces: the backup's download
 * then still has them), then deleted. At most MAX_RETIRED_FILES per request; the rest is reported.
 */
async function retireFiles(env: Bindings, backupId: string | null, keys: string[]): Promise<{ removed: number; kept: number }> {
    let removed = 0;
    if (!backupId) return { removed, kept: keys.length };
    for (const key of keys.slice(0, MAX_RETIRED_FILES))
        try {
            const live = await env.STORAGE.get(key);
            if (live)
                await env.STORAGE.put(`backups/${backupId}/objects/${key}`, await live.arrayBuffer(), {
                    httpMetadata: live.httpMetadata,
                    customMetadata: live.customMetadata,
                });
            await env.STORAGE.delete(key);
            removed += 1;
        } catch (e) {
            log("warn", "migration_file_kept", { error: e instanceof Error ? e.message : String(e) });
        }
    return { removed, kept: keys.length - removed };
}

function migrationJson(plan: MigrationPlan, outcome: MigrationOutcome) {
    return {
        id: plan.id,
        mode: plan.mode,
        items: plan.items,
        close_source: plan.closeSource,
        source: { id: plan.source.id, username: plan.source.username, closed: plan.closeSource },
        target: { id: plan.target.id, username: plan.target.username },
        restore: outcome.restore ? restoreJson(outcome.restore) : null,
        files: outcome.files,
        backup_id: plan.backupId,
    };
}

/** What the Journal keeps of a migration (adminRoles.ts reads it back). */
function auditOf(plan: MigrationPlan, outcome: MigrationOutcome) {
    return {
        id: plan.id,
        from: { id: plan.source.id, username: plan.source.username },
        to: { id: plan.target.id, username: plan.target.username },
        mode: plan.mode,
        items: plan.items,
        close_source: plan.closeSource,
        restore: outcome.restore !== null,
        backup_id: plan.backupId,
        by_admin: plan.by !== null,
    };
}

async function otherAdminExists(ctx: Ctx, playerId: string): Promise<boolean> {
    return !!(await ctx.env.DB.prepare("SELECT 1 AS x FROM players WHERE role = 'admin' AND account_status = 'active' AND id <> ?1 LIMIT 1")
        .bind(playerId)
        .first());
}

const lastAdmin = () => Errors.conflict("LAST_ADMIN", "C'est le dernier compte admin : il ne peut pas être fermé.");

// ------------------------------------------------------------------ the player, on the new computer

const invalidMigration = () =>
    new ApiError(403, "MIGRATION_INVALID", "Pseudonyme, mot de passe ou code de migration incorrect, ou code expiré.");

/**
 * POST /players/me/migrate-from {username, password, code}: the session's account (the new one)
 * receives the data of the account `username` (the old one), whose password and code prove it is
 * the same player. Every failure gives the same answer and counts (5 per 10 minutes per new
 * account and per old account). Then everything is merged in one batch and the old account closed.
 */
export async function migrateFrom(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const target = auth.player;
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["username", "password", "code"]);
    const username = v.str("username", { min: 1, max: 32, trim: true });
    const password = v.str("password", { min: 1, max: 128 });
    const code = v.str("code", { min: 1, max: 32, trim: true });
    v.done();
    const norm = normalizeUsername(username!);
    if (norm === target.username_normalized)
        throw Errors.conflict("SAME_ACCOUNT", "C'est le compte connecté : saisissez le pseudonyme de l'ANCIEN compte.");
    const targetKey = "player:" + target.id;
    const sourceKey = "user:" + norm;
    await assertUnderLimit(ctx, RULES.migrationFailTarget, targetKey);
    await assertUnderLimit(ctx, RULES.migrationFailSource, sourceKey);

    // The password is always checked (the same work for an unknown account), then the code.
    const source = await credentialsOf(ctx, norm);
    const password_ = await checkPassword(ctx, source, password!);
    const normalized = normalizeCode(code!);
    const hash = source && normalized ? await codeHash(source.id, normalized) : null;
    const stored = source ? await codeOf(ctx, source.id) : null;
    const codeOk = hash !== null && isActiveCode(stored) && (await secretsEqual(hash, stored.code_hash));
    const sourceOk = !!source && source.account_status === "active" && !source.closed_at;
    if (password_ !== "ok" || !codeOk || !sourceOk) {
        await countFailure(ctx, RULES.migrationFailTarget, targetKey);
        await countFailure(ctx, RULES.migrationFailSource, sourceKey);
        // Which part was wrong stays in the server's logs (never in an answer, nor in the player's data).
        log("info", "migration_refused", {
            request_id: ctx.requestId,
            reason: password_ !== "ok" ? `password_${password_}` : !codeOk ? "code" : "source_inactive",
        });
        await audit(ctx, target.id, "migration_failed");
        // The right password with a wrong code: the old account's owner is told (as a refused login).
        if (source && password_ === "ok" && !codeOk)
            await notify(ctx, source.id, "security", { data: { reason: "migration_refused" }, groupKey: "security:migration_refused" });
        throw invalidMigration();
    }
    if (source.role === "admin" && !(await otherAdminExists(ctx, source.id))) throw lastAdmin();
    const plan: MigrationPlan = {
        id: crypto.randomUUID(),
        source,
        target,
        mode: "merge",
        items: SELF_SERVICE_ITEMS,
        closeSource: true,
        by: null,
        backupId: null,
        codeHash: hash,
        // The code was used, or an account changed, between the checks and the batch.
        conflict: invalidMigration,
    };
    const outcome = await performMigration(ctx, plan);
    await audit(ctx, target.id, "account_migrated", {
        target: source.id,
        target_username: source.username,
        migration: auditOf(plan, outcome),
    });
    return json(ctx, { migration: migrationJson(plan, outcome), counts: await countsOf(ctx, target.id, await latestCopyRow(ctx, target.id)) });
}

// ------------------------------------------------------------------ the progression to restore

async function restoreOf(ctx: Ctx, playerId: string): Promise<RestoreRow | null> {
    return ctx.env.DB.prepare("SELECT * FROM progress_restores WHERE player_id = ?1").bind(playerId).first<RestoreRow>();
}

function restoreJson(r: RestoreRow) {
    return {
        from_username: r.from_username,
        created_at: r.created_at,
        postponed_at: r.postponed_at,
        copy: {
            created_at: r.copy_created_at,
            game_version: r.game_version,
            size: r.size,
            device_name: r.device_name,
            summary: parseSummary(r.summary),
        },
    };
}

const noRestore = () => Errors.notFound("RESTORE_NOT_FOUND", "Aucune progression à restaurer.");

/** GET /players/me/progress-restore: the progression of the old computer waiting to be restored, or null. */
export async function getRestore(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const r = await restoreOf(ctx, auth.player.id);
    return json(ctx, { restore: r ? restoreJson(r) : null });
}

/** GET /players/me/progress-restore/file: the copy exactly as the old computer sent it (its `files`). */
export async function getRestoreFile(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const r = await restoreOf(ctx, auth.player.id);
    if (!r) throw noRestore();
    const obj = await ctx.env.STORAGE.get(r.file_key);
    if (!obj) throw Errors.notFound("RESTORE_NOT_FOUND", "Le fichier de la progression à restaurer est introuvable.");
    return new Response(obj.body, {
        headers: {
            "content-type": "application/json; charset=utf-8",
            "content-length": String(obj.size),
            "x-progress-sha256": r.sha256,
            "cache-control": "no-store",
            "x-content-type-options": "nosniff",
        },
    });
}

/** POST /players/me/progress-restore {action: "done" | "later"}: restored (forgotten, file deleted) or later (kept). */
export async function answerRestore(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.progressRestore, "player:" + me);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["action"]);
    const action = v.oneOf("action", ["done", "later"] as const);
    v.done();
    const r = await restoreOf(ctx, me);
    if (!r) throw noRestore();
    const now = nowIso();
    if (action === "later") {
        await ctx.env.DB.prepare("UPDATE progress_restores SET postponed_at = ?3 WHERE player_id = ?1 AND migration_id = ?2").bind(me, r.migration_id, now).run();
        return json(ctx, { restore: restoreJson({ ...r, postponed_at: now }) });
    }
    const res = await ctx.env.DB.prepare("DELETE FROM progress_restores WHERE player_id = ?1 AND migration_id = ?2").bind(me, r.migration_id).run();
    if (res.meta.changes) {
        await ctx.env.STORAGE.delete(r.file_key);
        await audit(ctx, me, "progress_restored", { migration_id: r.migration_id, from_username: r.from_username });
    }
    return json(ctx, { restore: null });
}

// ------------------------------------------------------------------ administration

function accountJson(p: PlayerRow) {
    return {
        id: p.id,
        username: p.username,
        role: p.role ?? "player",
        account_status: p.account_status,
        closed_at: p.closed_at ?? null,
        device: p.device_id ? { name: p.device_name ?? null, bound_at: p.device_bound_at ?? null } : null,
    };
}

async function playerByUsername(ctx: Ctx, username: string): Promise<PlayerRow | null> {
    return ctx.env.DB.prepare("SELECT * FROM players WHERE username_normalized = ?1").bind(normalizeUsername(username)).first<PlayerRow>();
}

const usable = (p: PlayerRow) => p.account_status === "active" && !p.closed_at;

/** What both accounts have in common: what a « merge » decides. */
async function sharedOf(ctx: Ctx, source: string, target: string) {
    const r = await ctx.env.DB.prepare(
        `SELECT
           (SELECT COUNT(*) FROM player_bests s JOIN player_bests t ON t.player_id = ?2 AND t.level_hash = s.level_hash AND t.category = s.category
             WHERE s.player_id = ?1) AS bests,
           (SELECT COUNT(*) FROM player_bests s JOIN player_bests t ON t.player_id = ?2 AND t.level_hash = s.level_hash AND t.category = s.category
             WHERE s.player_id = ?1 AND (s.time_ms < t.time_ms OR (s.time_ms = t.time_ms AND s.achieved_at < t.achieved_at))) AS bests_source_better,
           (SELECT COUNT(*) FROM player_playtime s JOIN player_playtime t ON t.player_id = ?2 AND t.level_key = s.level_key WHERE s.player_id = ?1) AS playtime_levels,
           (SELECT COUNT(*) FROM player_won_rewards s JOIN player_won_rewards t ON t.player_id = ?2 AND t.challenge_id = s.challenge_id
             AND t.reward_kind = s.reward_kind AND t.reward_ref = s.reward_ref WHERE s.player_id = ?1) AS rewards`,
    )
        .bind(source, target)
        .first<{ bests: number; bests_source_better: number; playtime_levels: number; rewards: number }>();
    const rows = await ctx.env.DB.prepare("SELECT player_id, ids FROM player_achievements WHERE player_id IN (?1, ?2)")
        .bind(source, target)
        .all<{ player_id: string; ids: string }>();
    const theirs = new Set(achievementsOf(idsOf(rows.results.find((x) => x.player_id === target)?.ids)).ids);
    const added = achievementsOf(idsOf(rows.results.find((x) => x.player_id === source)?.ids)).ids.filter((id) => !theirs.has(id));
    return {
        bests: r?.bests ?? 0,
        bests_source_better: r?.bests_source_better ?? 0,
        playtime_levels: r?.playtime_levels ?? 0,
        rewards: r?.rewards ?? 0,
        achievements_new: added.length,
    };
}

/** Why a migration from `source` to `target` cannot be done (codes of POST …/migrate). */
async function problemsOf(ctx: Ctx, source: PlayerRow, target: PlayerRow | null): Promise<string[]> {
    const out: string[] = [];
    if (!usable(source)) out.push(source.closed_at ? "SOURCE_CLOSED" : "SOURCE_INACTIVE");
    if (!target) out.push("TARGET_NOT_FOUND");
    else if (target.id === source.id) out.push("SAME_ACCOUNT");
    else if (!usable(target)) out.push("TARGET_INACTIVE");
    // Closing is optional: an admin source can only be closed if another admin is left.
    if (source.role === "admin" && !(await otherAdminExists(ctx, source.id))) out.push("LAST_ADMIN");
    return out;
}

/**
 * GET /admin/players/{playerId}/migration?to=<username>: what a migration of this account to
 * `to` would bring: counts per part for both accounts, what they have in common, the source's
 * latest copy of the progression (when, which computer), what would refuse it.
 */
export async function adminPreview(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const source = await playerOr404(ctx);
    const to = (ctx.url.searchParams.get("to") ?? "").trim();
    if (to.length > 32 || /[\u0000-\u001f\u007f]/.test(to)) throw Errors.validation([{ field: "to", message: "pseudonyme du compte cible attendu" }]);
    const target = to ? await playerByUsername(ctx, to) : null;
    const copy = await latestCopyRow(ctx, source.id);
    return json(ctx, {
        source: { ...accountJson(source), counts: await countsOf(ctx, source.id, copy) },
        target_found: target !== null,
        target: target ? { ...accountJson(target), counts: await countsOf(ctx, target.id, await latestCopyRow(ctx, target.id)) } : null,
        shared: target && target.id !== source.id ? await sharedOf(ctx, source.id, target.id) : null,
        copy: copy
            ? {
                  created_at: copy.created_at,
                  device_name: copy.device_name ?? source.device_name ?? null,
                  game_version: copy.game_version,
                  size: copy.size,
                  summary: parseSummary(copy.summary),
              }
            : null,
        problems: to ? await problemsOf(ctx, source, target) : [],
        items: MIGRATION_ITEMS,
        modes: MIGRATION_MODES,
        confirm: ADMIN_CONFIRM,
    });
}

async function refuse(ctx: Ctx, auth: AuthInfo, error: ApiError, details: Record<string, unknown>): Promise<never> {
    await audit(ctx, auth.player.id, "admin_refused", { ...details, reason: error.code, by_username: auth.player.username });
    throw error;
}

/**
 * POST /admin/players/{playerId}/migrate {to_username, mode: "merge"|"replace", items, close_source, confirm: "MIGRER"}:
 * a full backup first (the existing mechanism: a server-side copy, finished before anything
 * changes; no backup, no migration), then the migration in one batch. Journal, notifications.
 */
export async function adminMigrate(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const source = await playerOr404(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["to_username", "mode", "items", "close_source", "confirm"]);
    const to = v.str("to_username", { min: 1, max: 32, trim: true });
    const mode = v.oneOf("mode", MIGRATION_MODES);
    const closeSource = v.bool("close_source", { optional: true }) ?? false;
    const items: MigrationItem[] = [];
    if (!Array.isArray(body.items) || body.items.length === 0) v.fail("items", `liste non vide parmi : ${MIGRATION_ITEMS.join(", ")}`);
    else
        for (const i of body.items as unknown[]) {
            if (typeof i !== "string" || !(MIGRATION_ITEMS as readonly string[]).includes(i)) v.fail("items", `valeurs attendues : ${MIGRATION_ITEMS.join(", ")}`);
            else if (items.includes(i as MigrationItem)) v.fail("items", "une même partie ne peut être donnée qu'une fois");
            else items.push(i as MigrationItem);
        }
    v.done();
    const where = { action: "migrate", target: source.id, target_username: source.username, to_username: to };
    if (typeof body.confirm !== "string" || body.confirm.normalize("NFC").trim() !== ADMIN_CONFIRM)
        await refuse(ctx, auth, new ApiError(400, "CONFIRMATION_REQUIRED", `Pour confirmer, tapez exactement : ${ADMIN_CONFIRM}`), where);
    const target = await playerByUsername(ctx, to!);
    if (!target) await refuse(ctx, auth, Errors.notFound("TARGET_NOT_FOUND", "Compte cible introuvable."), where);
    if (target!.id === source.id) await refuse(ctx, auth, Errors.conflict("SAME_ACCOUNT", "Le compte cible est le compte source."), where);
    if (!usable(source)) await refuse(ctx, auth, Errors.conflict("SOURCE_INACTIVE", "Le compte source est fermé ou suspendu."), where);
    if (!usable(target!)) await refuse(ctx, auth, Errors.conflict("TARGET_INACTIVE", "Le compte cible est fermé ou suspendu."), where);
    if (closeSource && source.id === auth.player.id)
        await refuse(ctx, auth, Errors.conflict("OWN_ACCOUNT", "Vous ne pouvez pas fermer votre propre compte."), where);
    if (closeSource && source.role === "admin" && !(await otherAdminExists(ctx, source.id))) await refuse(ctx, auth, lastAdmin(), where);
    if (await maintenanceActive(ctx.env))
        await refuse(ctx, auth, Errors.conflict("IMPORT_IN_PROGRESS", "Un import est en train de remplacer les données : attendez la fin."), where);
    // The full backup BEFORE anything changes (backups.ts: a server-side copy of every table and
    // the list of the files; createBackup returns once the copy is ready, or throws).
    await rateLimit(ctx, RULES.backupCreate, "player:" + auth.player.id);
    const actor = { id: auth.player.id, username: auth.player.username };
    let backup: Awaited<ReturnType<typeof createBackup>>;
    try {
        backup = await createBackup(ctx.env, "manual", actor);
    } catch (e) {
        if (e instanceof ApiError)
            await audit(ctx, auth.player.id, "backup_failed", {
                backup_id: (e.details as { backup_id?: string } | undefined)?.backup_id ?? null,
                backup_kind: "manual",
                reason: "migration",
                by_username: auth.player.username,
            });
        throw e;
    }
    if (backup.status !== "ready") throw new ApiError(500, "BACKUP_FAILED", "La sauvegarde n'est pas prête : la migration n'a pas été faite.", { backup_id: backup.id });
    await audit(ctx, auth.player.id, "backup_created", {
        backup_id: backup.id,
        backup_kind: "manual",
        reason: "migration",
        rows: backup.rows_count,
        files: backup.files_count,
        by_username: auth.player.username,
    });
    const plan: MigrationPlan = {
        id: crypto.randomUUID(),
        source,
        target: target!,
        mode: mode!,
        items,
        closeSource,
        by: actor,
        backupId: backup.id,
        codeHash: null,
        conflict: () => Errors.conflict("MIGRATION_CONFLICT", "Un des deux comptes a changé entre-temps : rien n'a été migré (la sauvegarde est faite)."),
    };
    const outcome = await performMigration(ctx, plan);
    await audit(ctx, auth.player.id, "admin_account_migrated", {
        target: source.id,
        target_username: source.username,
        backup_id: backup.id,
        by_username: auth.player.username,
        migration: auditOf(plan, outcome),
    });
    return json(ctx, {
        migration: migrationJson(plan, outcome),
        backup: { id: backup.id, created_at: backup.created_at, finished_at: backup.finished_at, rows: backup.rows_count, files: backup.files_count },
        counts: {
            source: await countsOf(ctx, source.id, await latestCopyRow(ctx, source.id)),
            target: await countsOf(ctx, target!.id, await latestCopyRow(ctx, target!.id)),
        },
    });
}
