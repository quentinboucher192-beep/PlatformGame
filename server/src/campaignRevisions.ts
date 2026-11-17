// The campaign edited by an administrator (2.45, ADMINISTRATION › Campagne). A campaign level
// opened in the game's editor is published as a NEW revision of that level: new revision
// number, new content hash, so its leaderboards are its own (level_versions), exactly as the
// revisions of a game release. Its file is kept in the private R2 bucket and served through
// the Worker; the games download the current revisions they do not have (GET /campaign/revisions)
// and play them instead of their bundled file. docs/online/CAMPAIGN_REVISIONS.md.
//
// The choice about the records is REQUIRED on every publication and every return to an
// earlier revision:
//   reset — the revision has its own boards; the records of the other revisions stay archived
//           under them (readable with ?level_hash=), never mixed;
//   keep  — the change has no effect on play (decoration…): the revision is declared
//           compatible, the bests of the revision it replaces are copied to it (the best time
//           per player and category), its weekly boards also read the runs of the revisions
//           it carries (campaign_board_links), and a run of those verified later counts too.
// Both are written to the Journal (who, when, level, revision, choice).
//
// Never trust the client: the level file is parsed and checked here (size, JSON, the game's
// level format, its id, planet, revision number, bounds and exit against the declared
// manifest entry); the routes exist only for an active administrator (requireAdminRole).

import { refreshAllClanPoints } from "./clans";
import { audit, type AuthInfo } from "./auth";
import { parseManifestLevel } from "./admin";
import { parseLayout } from "./race";
import { requireAdminRole } from "./adminRoles";
import { sha256Hex } from "./crypto";
import { ApiError, Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import type { LevelRow, LevelVersionRow } from "./model";
import { RULES, rateLimit } from "./ratelimit";
import { LEVEL_ID_RE, SEMVER_RE, Validator, compareVersions } from "./validate";

/** Largest level file accepted (the biggest campaign level is about 200 Kio). */
export const MAX_LEVEL_FILE_BYTES = 2 * 1024 * 1024;
/** Highest level format the server accepts (LevelData::kFormatVersion of the game, with room to grow). */
const MAX_FORMAT_VERSION = 50;
export const RECORDS = ["reset", "keep"] as const;
export type RecordsChoice = (typeof RECORDS)[number];

interface FileRow {
    level_id: string;
    version: number;
    content_hash: string;
    file_key: string;
    file_sha256: string;
    file_size: number;
    rating: string | null;
    published_by: string | null;
    published_by_username: string | null;
    published_at: string;
}

interface ChangeRow {
    id: number;
    level_id: string;
    action: "publish" | "revert";
    version: number;
    content_hash: string;
    from_version: number;
    from_hash: string;
    records: RecordsChoice;
    carried_bests: number;
    by_player: string | null;
    by_username: string | null;
    created_at: string;
}

export const isObject = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);
export const finite = (v: unknown): v is number => typeof v === "number" && Number.isFinite(v);

function parseRating(raw: string | null): Record<string, unknown> | null {
    if (!raw) return null;
    try {
        const r = JSON.parse(raw) as unknown;
        return isObject(r) ? r : null;
    } catch {
        return null;
    }
}

export function fileKey(levelId: string, version: number, sha: string): string {
    return `campaign/${levelId}/${version}-${sha}.json`;
}

export function filePath(levelId: string, version: number): string {
    return `/api/v1/campaign/levels/${levelId}/revisions/${version}/file`;
}

// ------------------------------------------------------------------ the level file

export interface CheckedLevelFile {
    bounds: [number, number];
    exit: [number, number, number, number];
    objects: number;
}

/**
 * Checks a level file as the game's loader would read it (LevelData::fromJson), plus what a
 * campaign revision needs: its id, its planet and its revision number (metadata.revision,
 * which is part of its fingerprint). Throws a 422 LEVEL_INVALID listing the problems.
 */
export function checkLevelFile(text: string, expect: { id: string; planet: string; version: number }): CheckedLevelFile {
    const problems: string[] = [];
    const fail = (): never => {
        throw new ApiError(422, "LEVEL_INVALID", "Le fichier du niveau n'est pas valide.", problems);
    };
    let level: unknown;
    try {
        level = JSON.parse(text);
    } catch {
        problems.push("JSON invalide");
        return fail();
    }
    if (!isObject(level)) {
        problems.push("objet JSON attendu");
        return fail();
    }
    if (level.format !== "cosmic.level") problems.push("format inconnu (attendu cosmic.level)");
    const fv = level.formatVersion ?? 1;
    if (typeof fv !== "number" || !Number.isInteger(fv) || fv < 1 || fv > MAX_FORMAT_VERSION) problems.push("formatVersion invalide");
    if (level.id !== expect.id) problems.push(`identifiant attendu : ${expect.id}`);
    if (typeof level.name !== "string" || level.name.length < 1 || level.name.length > 80) problems.push("nom invalide (1 à 80 caractères)");
    if ((level.planet ?? "sylvanox") !== expect.planet) problems.push(`planète attendue : ${expect.planet}`);
    const meta = level.metadata;
    if (!isObject(meta) || meta.revision !== expect.version) problems.push(`metadata.revision attendu : ${expect.version}`);
    const b = level.bounds;
    let bounds: [number, number] = [0, 0];
    if (!Array.isArray(b) || b.length !== 4 || !b.every(finite)) problems.push("bounds : 4 nombres attendus");
    else {
        bounds = [b[2] as number, b[3] as number];
        if (bounds[0] < 480 || bounds[1] < 270 || bounds[0] > 200000 || bounds[1] > 50000) problems.push("dimensions du niveau invalides");
    }
    for (const k of ["layers", "events", "startPowers", "background"])
        if (level[k] !== undefined && !Array.isArray(level[k])) problems.push(`${k} : liste attendue`);
    for (const k of ["timer", "intro", "outro", "medals", "powerKeys"])
        if (level[k] !== undefined && !isObject(level[k])) problems.push(`${k} : objet attendu`);
    const objects = level.objects;
    let exit: [number, number, number, number] = [0, 0, 0, 0];
    let starts = 0;
    let exits = 0;
    if (!Array.isArray(objects) || objects.length === 0 || objects.length > 50000) problems.push("liste des objets attendue (de 1 à 50 000)");
    else {
        const ids = new Set<string>();
        objects.forEach((o, i) => {
            if (problems.length > 20) return;
            if (!isObject(o)) return void problems.push(`objects[${i}] : objet attendu`);
            if (typeof o.id !== "string" || o.id.length < 1 || o.id.length > 96) problems.push(`objects[${i}].id invalide`);
            else if (ids.has(o.id)) problems.push(`objects[${i}].id en double : ${o.id}`);
            else ids.add(o.id);
            if (typeof o.type !== "string" || !/^[a-z0-9_]{1,40}$/.test(o.type)) problems.push(`objects[${i}].type invalide`);
            const r = o.rect;
            if (!Array.isArray(r) || r.length !== 4 || !r.every(finite)) problems.push(`objects[${i}].rect : 4 nombres attendus`);
            if (o.props !== undefined && !isObject(o.props)) problems.push(`objects[${i}].props : objet attendu`);
            if (o.type === "player_start") starts++;
            if (o.type === "exit" && Array.isArray(r)) {
                exits++;
                exit = [r[0] as number, r[1] as number, r[2] as number, r[3] as number];
            }
        });
        if (starts !== 1) problems.push("il faut exactement un départ du joueur");
        if (exits < 1) problems.push("il faut une sortie du niveau");
    }
    if (problems.length) fail();
    return { bounds, exit, objects: (objects as unknown[]).length };
}

// ------------------------------------------------------------------ JSON views

function publisherJson(f: FileRow | null) {
    if (!f) return null;
    return f.published_by || f.published_by_username ? { id: f.published_by, username: f.published_by_username } : null;
}

function changeJson(c: ChangeRow | null) {
    if (!c) return null;
    return {
        action: c.action,
        version: c.version,
        content_hash: c.content_hash,
        from_version: c.from_version,
        records: c.records,
        carried_bests: c.carried_bests,
        by: c.by_player || c.by_username ? { id: c.by_player, username: c.by_username } : null,
        at: c.created_at,
    };
}

/** The last change that made `hash` current, when it is still the current one. */
async function lastChanges(ctx: Ctx): Promise<Map<string, ChangeRow>> {
    const rows = await ctx.env.DB.prepare(
        `SELECT c.* FROM campaign_level_changes c
         WHERE c.id = (SELECT MAX(id) FROM campaign_level_changes WHERE level_id = c.level_id)`,
    ).all<ChangeRow>();
    return new Map(rows.results.map((r) => [r.level_id, r]));
}

/**
 * GET /admin/campaign: the campaign levels by planet, each with its current revision online
 * (a game release's, or an administrator's: who and when, the choice about the records).
 */
export async function adminList(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const [levels, files, changes, maxes] = await Promise.all([
        ctx.env.DB.prepare(
            `SELECT l.*, v.version AS v_version, v.min_game_version, v.created_at AS v_created_at
             FROM levels l JOIN level_versions v ON v.level_id = l.id AND v.content_hash = l.content_hash
             ORDER BY l.planet_id, l.level_order, l.id`,
        ).all<LevelRow & { v_version: number; min_game_version: string; v_created_at: string }>(),
        ctx.env.DB.prepare("SELECT * FROM campaign_level_files").all<FileRow>(),
        lastChanges(ctx),
        ctx.env.DB.prepare("SELECT level_id, MAX(version) AS max_version, COUNT(*) AS revisions FROM level_versions GROUP BY level_id").all<{
            level_id: string;
            max_version: number;
            revisions: number;
        }>(),
    ]);
    const fileOf = new Map(files.results.map((f) => [`${f.level_id}:${f.content_hash}`, f]));
    const maxOf = new Map(maxes.results.map((m) => [m.level_id, m]));
    const planets: { planet_id: string; planet_name: string; levels: unknown[] }[] = [];
    for (const l of levels.results) {
        let p = planets.find((x) => x.planet_id === l.planet_id);
        if (!p) planets.push((p = { planet_id: l.planet_id, planet_name: l.planet_name, levels: [] }));
        const f = fileOf.get(`${l.id}:${l.content_hash}`) ?? null;
        const c = changes.get(l.id);
        const m = maxOf.get(l.id);
        p.levels.push({
            level_id: l.id,
            name: l.name,
            order: l.level_order,
            ranking_enabled: l.ranking_enabled === 1,
            current: {
                version: l.v_version,
                content_hash: l.content_hash,
                min_game_version: l.min_game_version,
                created_at: l.v_created_at,
                source: f ? "admin" : "release",
                has_file: f !== null,
                published_by: publisherJson(f),
                published_at: f?.published_at ?? null,
                // What made it current (a publication or a return), when an administrator did.
                change: c && c.content_hash === l.content_hash ? changeJson(c) : null,
            },
            next_version: (m?.max_version ?? l.current_version) + 1,
            revisions: m?.revisions ?? 1,
        });
    }
    return json(ctx, { planets });
}

async function levelOr404(ctx: Ctx): Promise<LevelRow> {
    const id = ctx.params.levelId ?? "";
    if (!LEVEL_ID_RE.test(id)) throw Errors.notFound("LEVEL_NOT_FOUND", "Niveau inconnu du serveur.");
    const l = await ctx.env.DB.prepare("SELECT * FROM levels WHERE id = ?1").bind(id).first<LevelRow>();
    if (!l) throw Errors.notFound("LEVEL_NOT_FOUND", "Niveau inconnu du serveur.");
    return l;
}

/** GET /admin/campaign/{levelId}/revisions: every revision (newest first) and the changes. */
export async function adminHistory(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const level = await levelOr404(ctx);
    const [versions, files, changes, bests] = await Promise.all([
        ctx.env.DB.prepare("SELECT * FROM level_versions WHERE level_id = ?1 ORDER BY version DESC").bind(level.id).all<LevelVersionRow>(),
        ctx.env.DB.prepare("SELECT * FROM campaign_level_files WHERE level_id = ?1").bind(level.id).all<FileRow>(),
        ctx.env.DB.prepare("SELECT * FROM campaign_level_changes WHERE level_id = ?1 ORDER BY id DESC LIMIT 100").bind(level.id).all<ChangeRow>(),
        ctx.env.DB.prepare("SELECT level_hash, COUNT(*) AS n FROM player_bests WHERE level_id = ?1 GROUP BY level_hash")
            .bind(level.id)
            .all<{ level_hash: string; n: number }>(),
    ]);
    const fileOf = new Map(files.results.map((f) => [f.content_hash, f]));
    const bestsOf = new Map(bests.results.map((b) => [b.level_hash, b.n]));
    const max = versions.results.reduce((m, v) => Math.max(m, v.version), level.current_version);
    return json(ctx, {
        level: { id: level.id, name: level.name, planet_id: level.planet_id, planet_name: level.planet_name },
        current_version: level.current_version,
        content_hash: level.content_hash,
        next_version: max + 1,
        revisions: versions.results.map((v) => {
            const f = fileOf.get(v.content_hash) ?? null;
            return {
                version: v.version,
                content_hash: v.content_hash,
                compatibility_status: v.compatibility_status,
                min_game_version: v.min_game_version,
                created_at: v.created_at,
                is_current: v.content_hash === level.content_hash,
                source: f ? "admin" : "release",
                has_file: f !== null,
                published_by: publisherJson(f),
                published_at: f?.published_at ?? null,
                bests: bestsOf.get(v.content_hash) ?? 0,
            };
        }),
        changes: changes.results.map(changeJson),
    });
}

// ------------------------------------------------------------------ carry-over

/**
 * « Garder les records »: the bests of `fromHash` are copied to `toHash` (the best time per
 * player and category wins when the target already has some), and the target's weekly boards
 * read the runs of `fromHash` and of every revision it already carried. Returns the
 * statements (run in the batch that makes `toHash` current) and the number of bests copied.
 */
async function carryOver(ctx: Ctx, levelId: string, fromHash: string, toHash: string, guard: string, guardBinds: unknown[]) {
    const count = await ctx.env.DB.prepare("SELECT COUNT(*) AS n FROM player_bests WHERE level_hash = ?1").bind(fromHash).first<{ n: number }>();
    return { stmts: carryOverStatements(ctx, levelId, fromHash, toHash, guard, guardBinds), carried: count?.n ?? 0 };
}

/** The statements of carryOver() (2.46: also used by a publication of an enemy revision). */
export function carryOverStatements(ctx: Ctx, levelId: string, fromHash: string, toHash: string, guard: string, guardBinds: unknown[]) {
    const n = guardBinds.length;
    const now = nowIso();
    return [
        ctx.env.DB.prepare(
            `INSERT INTO player_bests (player_id, level_id, level_hash, category, record_id, time_ms, achieved_at)
             SELECT player_id, level_id, ?${n + 2}, category, record_id, time_ms, achieved_at FROM player_bests
             WHERE level_hash = ?${n + 1} AND ${guard}
             ON CONFLICT (player_id, level_hash, category) DO UPDATE SET record_id = excluded.record_id, time_ms = excluded.time_ms,
               achieved_at = excluded.achieved_at WHERE excluded.time_ms < player_bests.time_ms`,
        ).bind(...guardBinds, fromHash, toHash),
        ctx.env.DB.prepare(
            `INSERT OR IGNORE INTO campaign_board_links (level_id, to_hash, from_hash, created_at)
             SELECT ?${n + 1}, ?${n + 3}, ?${n + 2}, ?${n + 4} WHERE ${guard}
             UNION SELECT ?${n + 1}, ?${n + 3}, from_hash, ?${n + 4} FROM campaign_board_links WHERE to_hash = ?${n + 2} AND from_hash <> ?${n + 3} AND ${guard}`,
        ).bind(...guardBinds, levelId, fromHash, toHash, now),
    ];
}

async function refuse(ctx: Ctx, auth: AuthInfo, error: ApiError, details: Record<string, unknown>): Promise<never> {
    await audit(ctx, auth.player.id, "admin_refused", { campaign: details, reason: error.code, by_username: auth.player.username });
    throw error;
}

function changeStatement(ctx: Ctx, c: Omit<ChangeRow, "id">, guard: string, guardBinds: unknown[]) {
    const n = guardBinds.length;
    return ctx.env.DB.prepare(
        `INSERT INTO campaign_level_changes (level_id, action, version, content_hash, from_version, from_hash, records, carried_bests,
           by_player, by_username, created_at)
         SELECT ?${n + 1}, ?${n + 2}, ?${n + 3}, ?${n + 4}, ?${n + 5}, ?${n + 6}, ?${n + 7}, ?${n + 8}, ?${n + 9}, ?${n + 10}, ?${n + 11} WHERE ${guard}`,
    ).bind(...guardBinds, c.level_id, c.action, c.version, c.content_hash, c.from_version, c.from_hash, c.records, c.carried_bests, c.by_player,
        c.by_username, c.created_at);
}

/**
 * POST /admin/campaign/{levelId}/revisions
 * {level_file, base_version, records: "reset"|"keep", game_version, manifest, rating?}
 * `level_file`: the level as the game wrote it (stored byte for byte: its fingerprint is
 * computed by the games from these bytes); `manifest`: the entry of --online-manifest for it
 * (fingerprint, totals, spawn, exit, time bounds); `base_version`: the current revision the
 * administrator edited (another publication in between is refused: BASE_CHANGED).
 */
export async function publish(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const level = await levelOr404(ctx);
    const body = await readJsonObject(ctx, MAX_LEVEL_FILE_BYTES * 2 + 64 * 1024);
    const v = new Validator(body, ["level_file", "base_version", "records", "game_version", "manifest", "rating"]);
    const text = v.str("level_file", { min: 2, max: MAX_LEVEL_FILE_BYTES, multiline: true });
    const baseVersion = v.int("base_version", { min: 1, max: 1_000_000 });
    const records = v.oneOf("records", RECORDS);
    const gameVersion = v.str("game_version", { max: 16, pattern: SEMVER_RE });
    if (body.rating !== undefined && body.rating !== null) {
        const r = body.rating;
        if (!isObject(r) || !finite(r.score) || r.score < 0 || r.score > 5 || JSON.stringify(r).length > 4096) v.fail("rating", "note de difficulté invalide");
    }
    v.done();
    const bytes = new TextEncoder().encode(text!);
    if (bytes.byteLength > MAX_LEVEL_FILE_BYTES) throw new ApiError(413, "PAYLOAD_TOO_LARGE", `Niveau trop volumineux (maximum ${MAX_LEVEL_FILE_BYTES} octets).`);
    if (compareVersions(gameVersion!, ctx.env.MIN_GAME_VERSION) < 0)
        throw new ApiError(426, "GAME_VERSION_UNSUPPORTED", "Version du jeu trop ancienne pour publier une révision.");
    // The race layout of an EXTRÊME level (--online-manifest writes it next to the entry).
    const rawManifest = isObject(body.manifest) ? { ...body.manifest } : body.manifest;
    let race: unknown = null;
    if (isObject(rawManifest) && rawManifest.race !== undefined) {
        race = parseLayout(rawManifest.race);
        if (!race) throw Errors.validation([{ field: "manifest.race", message: "tracé de course invalide" }]);
        delete rawManifest.race;
    }
    const m = parseManifestLevel(rawManifest, "manifest");
    const what = { level_id: level.id, level_name: level.name, base_version: baseVersion, records };

    // The revision: the next number of this level, computed here (never the client's choice).
    const max = await ctx.env.DB.prepare("SELECT MAX(version) AS v FROM level_versions WHERE level_id = ?1").bind(level.id).first<{ v: number | null }>();
    const version = Math.max(max?.v ?? 0, level.current_version) + 1;
    if (baseVersion !== level.current_version)
        await refuse(ctx, auth, Errors.conflict("BASE_CHANGED", "La révision en ligne a changé entre-temps : rouvrez le niveau.", {
            current_version: level.current_version,
            next_version: version,
        }), what);
    const checked = checkLevelFile(text!, { id: level.id, planet: level.planet_id, version });
    const mismatch: string[] = [];
    if (m.id !== level.id) mismatch.push("manifest.id");
    if (m.version !== version) mismatch.push("manifest.version");
    if (m.planet_id !== level.planet_id) mismatch.push("manifest.planet_id");
    if (m.bounds[0] !== checked.bounds[0] || m.bounds[1] !== checked.bounds[1]) mismatch.push("manifest.bounds");
    if (m.exit.some((x, i) => x !== checked.exit[i])) mismatch.push("manifest.exit");
    if (m.min_time_ms >= m.time_limit_ms) mismatch.push("manifest.min_time_ms");
    if (mismatch.length) throw new ApiError(422, "MANIFEST_MISMATCH", "Le manifeste ne correspond pas au fichier du niveau.", mismatch);
    const used = await ctx.env.DB.prepare("SELECT version FROM level_versions WHERE level_id = ?1 AND content_hash = ?2")
        .bind(level.id, m.content_hash)
        .first<{ version: number }>();
    if (used) throw Errors.conflict("HASH_EXISTS", `Ce contenu est déjà la révision ${used.version} de ce niveau.`, { version: used.version });

    const sha = await sha256Hex(bytes);
    const key = fileKey(level.id, version, sha);
    await ctx.env.STORAGE.put(key, bytes, { httpMetadata: { contentType: "application/json; charset=utf-8" }, customMetadata: { sha256: sha } });

    // One batch, guarded by « the current revision is still the base »: two administrators
    // publishing at once cannot both win, and nothing half-done stays.
    const now = nowIso();
    const guard = "EXISTS (SELECT 1 FROM levels WHERE id = ?1 AND current_version = ?2 AND content_hash = ?3)";
    const gb = [level.id, level.current_version, level.content_hash];
    const after = "EXISTS (SELECT 1 FROM levels WHERE id = ?1 AND content_hash = ?2)";
    const carry = records === "keep" ? await carryOver(ctx, level.id, level.content_hash, m.content_hash, after, [level.id, m.content_hash]) : null;
    const stmts = [
        ctx.env.DB.prepare(
            `INSERT INTO level_versions (level_id, version, content_hash, compatibility_status, min_game_version, total_collectibles,
               total_enemies, time_limit_ms, min_time_ms, bounds_w, bounds_h, spawn_x, spawn_y, exit_x, exit_y, exit_w, exit_h, created_at, race)
             SELECT ?1, ?4, ?5, 'active', ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20 WHERE ${guard}`,
        ).bind(...gb, version, m.content_hash, gameVersion, m.total_collectibles, m.total_enemies, m.time_limit_ms, m.min_time_ms, m.bounds[0],
            m.bounds[1], m.spawn[0], m.spawn[1], m.exit[0], m.exit[1], m.exit[2], m.exit[3], now, race ? JSON.stringify(race) : null),
        ctx.env.DB.prepare(`UPDATE levels SET current_version = ?4, content_hash = ?5 WHERE id = ?1 AND current_version = ?2 AND content_hash = ?3`).bind(
            ...gb,
            version,
            m.content_hash,
        ),
        ctx.env.DB.prepare(
            `UPDATE level_versions SET compatibility_status = 'legacy' WHERE level_id = ?1 AND content_hash <> ?2 AND compatibility_status = 'active' AND ${after}`,
        ).bind(level.id, m.content_hash),
        ctx.env.DB.prepare(
            `INSERT INTO campaign_level_files (level_id, version, content_hash, file_key, file_sha256, file_size, rating, published_by,
               published_by_username, published_at)
             SELECT ?1, ?3, ?2, ?4, ?5, ?6, ?7, ?8, ?9, ?10 WHERE ${after}`,
        ).bind(level.id, m.content_hash, version, key, sha, bytes.byteLength, isObject(body.rating) ? JSON.stringify(body.rating) : null,
            auth.player.id, auth.player.username, now),
        ...(carry?.stmts ?? []),
        changeStatement(
            ctx,
            {
                level_id: level.id,
                action: "publish",
                version,
                content_hash: m.content_hash,
                from_version: level.current_version,
                from_hash: level.content_hash,
                records: records!,
                carried_bests: carry?.carried ?? 0,
                by_player: auth.player.id,
                by_username: auth.player.username,
                created_at: now,
            },
            after,
            [level.id, m.content_hash],
        ),
    ];
    let results: D1Result[];
    try {
        results = await ctx.env.DB.batch(stmts);
    } catch (e) {
        await ctx.env.STORAGE.delete(key);
        if (String(e).includes("UNIQUE")) throw Errors.conflict("REVISION_CONFLICT", "Une autre révision vient d'être publiée : rouvrez le niveau.");
        throw e;
    }
    if (!results[1]?.meta.changes) {
        await ctx.env.STORAGE.delete(key);
        await refuse(ctx, auth, Errors.conflict("BASE_CHANGED", "La révision en ligne a changé entre-temps : rouvrez le niveau."), what);
    }
    const carried = carry?.carried ?? 0;
    // 2.66: the clans' points follow the current revision of the EXTRÊME levels.
    await refreshAllClanPoints(ctx.env.DB);
    await audit(ctx, auth.player.id, "admin_campaign_published", {
        campaign: { level_id: level.id, level_name: level.name, version, content_hash: m.content_hash, from_version: level.current_version, records, carried_bests: carried },
        by_username: auth.player.username,
    });
    return json(
        ctx,
        {
            level_id: level.id,
            version,
            content_hash: m.content_hash,
            records,
            carried_bests: carried,
            file: { sha256: sha, size: bytes.byteLength, path: filePath(level.id, version) },
        },
        { status: 201 },
    );
}

/**
 * POST /admin/campaign/{levelId}/revert {version, base_version, records}
 * An earlier revision becomes current again (its content hash and its archived boards come
 * back); « keep » carries the bests of the revision it replaces over to it.
 */
export async function revert(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const level = await levelOr404(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["version", "base_version", "records"]);
    const version = v.int("version", { min: 1, max: 1_000_000 });
    const baseVersion = v.int("base_version", { min: 1, max: 1_000_000 });
    const records = v.oneOf("records", RECORDS);
    v.done();
    const what = { level_id: level.id, level_name: level.name, version, base_version: baseVersion, records, action: "revert" };
    const target = await ctx.env.DB.prepare("SELECT * FROM level_versions WHERE level_id = ?1 AND version = ?2")
        .bind(level.id, version)
        .first<LevelVersionRow>();
    if (!target) throw Errors.notFound("LEVEL_VERSION_NOT_FOUND", "Révision inconnue.");
    if (baseVersion !== level.current_version)
        await refuse(ctx, auth, Errors.conflict("BASE_CHANGED", "La révision en ligne a changé entre-temps : actualisez la liste."), what);
    if (target.content_hash === level.content_hash) throw Errors.conflict("ALREADY_CURRENT", "C'est déjà la révision en ligne.");
    if (target.compatibility_status === "disabled") throw Errors.conflict("REVISION_DISABLED", "Cette révision est désactivée.");

    const now = nowIso();
    const guard = "EXISTS (SELECT 1 FROM levels WHERE id = ?1 AND current_version = ?2 AND content_hash = ?3)";
    const gb = [level.id, level.current_version, level.content_hash];
    const after = "EXISTS (SELECT 1 FROM levels WHERE id = ?1 AND content_hash = ?2)";
    const carry = records === "keep" ? await carryOver(ctx, level.id, level.content_hash, target.content_hash, after, [level.id, target.content_hash]) : null;
    const results = await ctx.env.DB.batch([
        ctx.env.DB.prepare(`UPDATE levels SET current_version = ?4, content_hash = ?5 WHERE id = ?1 AND current_version = ?2 AND content_hash = ?3`).bind(
            ...gb,
            target.version,
            target.content_hash,
        ),
        ctx.env.DB.prepare(
            `UPDATE level_versions SET compatibility_status = CASE WHEN content_hash = ?2 THEN 'active' ELSE 'legacy' END
             WHERE level_id = ?1 AND compatibility_status IN ('active', 'legacy') AND (content_hash = ?2 OR compatibility_status = 'active') AND ${after}`,
        ).bind(level.id, target.content_hash),
        ...(carry?.stmts ?? []),
        changeStatement(
            ctx,
            {
                level_id: level.id,
                action: "revert",
                version: target.version,
                content_hash: target.content_hash,
                from_version: level.current_version,
                from_hash: level.content_hash,
                records: records!,
                carried_bests: carry?.carried ?? 0,
                by_player: auth.player.id,
                by_username: auth.player.username,
                created_at: now,
            },
            after,
            [level.id, target.content_hash],
        ),
    ]);
    if (!results[0]?.meta.changes)
        await refuse(ctx, auth, Errors.conflict("BASE_CHANGED", "La révision en ligne a changé entre-temps : actualisez la liste."), what);
    const carried = carry?.carried ?? 0;
    await refreshAllClanPoints(ctx.env.DB);
    await audit(ctx, auth.player.id, "admin_campaign_reverted", {
        campaign: {
            level_id: level.id,
            level_name: level.name,
            version: target.version,
            content_hash: target.content_hash,
            from_version: level.current_version,
            records,
            carried_bests: carried,
        },
        by_username: auth.player.username,
    });
    return json(ctx, { level_id: level.id, version: target.version, content_hash: target.content_hash, records, carried_bests: carried });
}

// ------------------------------------------------------------------ public side

/**
 * GET /campaign/revisions (public): the current revision of every campaign level, with its
 * file when an administrator published it (the games download it, check its SHA-256 and its
 * fingerprint, then play it instead of their bundled file); `max_version`: the highest
 * revision the server knows (a game whose bundled level is newer keeps its own); `pinned`:
 * the other revisions with a file that a challenge not over yet still plays (a challenge
 * keeps the revision it was drawn on); `kept_hashes` (2.49.1): the fingerprints of the earlier
 * revisions whose records count on the current one (« Garder les records »: campaign_board_links),
 * so that a game keeps its own best time of those instead of starting the level from scratch.
 */
export async function manifest(ctx: Ctx): Promise<Response> {
    const today = new Date().toISOString().slice(0, 10);
    const [levels, files, maxes, pinned, links] = await Promise.all([
        ctx.env.DB.prepare(
            `SELECT l.id, l.current_version, l.content_hash, v.version, v.min_game_version
             FROM levels l JOIN level_versions v ON v.level_id = l.id AND v.content_hash = l.content_hash ORDER BY l.planet_id, l.level_order, l.id`,
        ).all<{ id: string; current_version: number; content_hash: string; version: number; min_game_version: string }>(),
        ctx.env.DB.prepare(
            `SELECT f.*, v.min_game_version FROM campaign_level_files f
             JOIN level_versions v ON v.level_id = f.level_id AND v.content_hash = f.content_hash`,
        ).all<FileRow & { min_game_version: string }>(),
        ctx.env.DB.prepare("SELECT level_id, MAX(version) AS max_version FROM level_versions GROUP BY level_id").all<{ level_id: string; max_version: number }>(),
        ctx.env.DB.prepare(
            `SELECT DISTINCT level_id, level_hash FROM challenges WHERE level_source = 'campaign' AND end_date > ?1`,
        )
            .bind(today)
            .all<{ level_id: string; level_hash: string }>(),
        ctx.env.DB.prepare("SELECT level_id, to_hash, from_hash FROM campaign_board_links ORDER BY level_id, from_hash").all<{
            level_id: string;
            to_hash: string;
            from_hash: string;
        }>(),
    ]);
    const fileOf = new Map(files.results.map((f) => [`${f.level_id}:${f.content_hash}`, f]));
    const maxOf = new Map(maxes.results.map((m) => [m.level_id, m.max_version]));
    const fileJson = (f: FileRow & { min_game_version: string }) => ({
        version: f.version,
        content_hash: f.content_hash,
        sha256: f.file_sha256,
        size: f.file_size,
        min_game_version: f.min_game_version,
        path: filePath(f.level_id, f.version),
    });
    return json(ctx, {
        format: "cosmic.campaign_revisions",
        levels: levels.results.map((l) => {
            const f = fileOf.get(`${l.id}:${l.content_hash}`);
            const pins = pinned.results
                .filter((p) => p.level_id === l.id && p.level_hash !== l.content_hash)
                .map((p) => fileOf.get(`${l.id}:${p.level_hash}`))
                .filter((x): x is FileRow & { min_game_version: string } => x !== undefined);
            return {
                level_id: l.id,
                version: l.version,
                content_hash: l.content_hash,
                max_version: maxOf.get(l.id) ?? l.version,
                min_game_version: l.min_game_version,
                file: f ? fileJson(f) : null,
                rating: f ? parseRating(f.rating) : null,
                pinned: pins.map(fileJson),
                kept_hashes: links.results.filter((k) => k.level_id === l.id && k.to_hash === l.content_hash).map((k) => k.from_hash),
            };
        }),
    });
}

/** GET /campaign/levels/{levelId}/revisions/{version}/file (public): a published level file, from the private bucket. */
export async function file(ctx: Ctx): Promise<Response> {
    const id = ctx.params.levelId ?? "";
    const version = Number(ctx.params.version ?? "");
    if (!LEVEL_ID_RE.test(id) || !Number.isSafeInteger(version) || version < 1) throw Errors.notFound("REVISION_NOT_FOUND", "Révision introuvable.");
    const f = await ctx.env.DB.prepare("SELECT * FROM campaign_level_files WHERE level_id = ?1 AND version = ?2").bind(id, version).first<FileRow>();
    const obj = f ? await ctx.env.STORAGE.get(f.file_key) : null;
    if (!f || !obj) throw Errors.notFound("REVISION_NOT_FOUND", "Révision introuvable.");
    return new Response(obj.body, {
        status: 200,
        headers: {
            "content-type": "application/json; charset=utf-8",
            "x-content-type-options": "nosniff",
            "cache-control": "public, max-age=3600",
            "x-content-sha256": f.file_sha256,
            "x-level-hash": f.content_hash,
            "x-level-version": String(f.version),
            "x-request-id": ctx.requestId,
        },
    });
}
