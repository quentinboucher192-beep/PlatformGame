// The native entities edited by an administrator (2.46, ADMINISTRATION › Ennemis). A native
// entity of the game (data/enemies, copied in src/enemyCatalog.json by
// scripts/enemies-catalog.mjs) opened in the game's enemy editor is published as a numbered
// REVISION of its definition. docs/online/ENEMY_REVISIONS.md.
//
// The ranked fingerprint of a level includes the definitions of its enemies: a new definition
// is a new content for every campaign level that uses the entity. So one publication is ONE
// batch: the enemy revision, and the next revision of each of those levels (the same level,
// `metadata.revision` bumped, its binding `metadata.enemyRevisions` naming the new enemy
// revision), each with the administrator's choice about its records (« reset »: new boards;
// « keep »: the bests carried over as for a campaign revision, campaignRevisions.ts). Every
// enemy revision is kept: an older level revision (a challenge, a replay, an archived record)
// keeps the binding it was published with and plays the definition it had.
//
// Never trust the client: the entity must be a native of the game, its definition is checked
// (format, id, sizes, numbers), each level file as a campaign revision (checkLevelFile, the
// manifest, its binding); the routes exist only for an active administrator (requireAdminRole).
// Community levels and the players' own levels keep the shipped definitions.

import { refreshAllClanPoints } from "./clans";
import { audit, type AuthInfo } from "./auth";
import { parseManifestLevel } from "./admin";
import { requireAdminRole } from "./adminRoles";
import {
    MAX_LEVEL_FILE_BYTES,
    RECORDS,
    type RecordsChoice,
    carryOverStatements,
    checkLevelFile,
    fileKey as levelFileKey,
    filePath as levelFilePath,
    finite,
    isObject,
} from "./campaignRevisions";
import { sha256Hex } from "./crypto";
import catalog from "./enemyCatalog.json";
import { ApiError, Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import type { LevelRow } from "./model";
import { parseLayout } from "./race";
import { RULES, rateLimit } from "./ratelimit";
import { LEVEL_ID_RE, SEMVER_RE, Validator, compareVersions } from "./validate";

/** Largest definition file accepted (the biggest native is about 20 Kio; the game refuses more than 256 Kio). */
export const MAX_DEFINITION_BYTES = 256 * 1024;
/** At most this many campaign levels in one publication. */
export const MAX_PUBLISH_LEVELS = 60;
const ENEMY_ID_RE = /^[a-z0-9_]{1,64}$/;

const NATIVES: ReadonlyMap<string, string> = new Map((catalog.enemies as { id: string; name: string }[]).map((e) => [e.id, e.name]));

interface RevisionRow {
    enemy_id: string;
    revision: number;
    file_key: string;
    file_sha256: string;
    file_size: number;
    min_game_version: string;
    levels: string;
    published_by: string | null;
    published_by_username: string | null;
    published_at: string;
}

function fileKey(enemyId: string, revision: number, sha: string): string {
    return `enemies/${enemyId}/${revision}-${sha}.json`;
}

function filePath(enemyId: string, revision: number): string {
    return `/api/v1/enemies/${enemyId}/revisions/${revision}/file`;
}

function publisherJson(r: RevisionRow) {
    return r.published_by || r.published_by_username ? { id: r.published_by, username: r.published_by_username } : null;
}

function levelsOf(r: RevisionRow): Record<string, unknown>[] {
    try {
        const l = JSON.parse(r.levels) as unknown;
        return Array.isArray(l) ? l.filter(isObject) : [];
    } catch {
        return [];
    }
}

function nativeOr404(ctx: Ctx): string {
    const id = ctx.params.enemyId ?? "";
    if (!ENEMY_ID_RE.test(id) || !NATIVES.has(id)) throw Errors.notFound("ENEMY_NOT_FOUND", "Entité native inconnue.");
    return id;
}

/** The current revision of every entity that has one (enemy id -> revision). */
async function currentRevisions(ctx: Ctx): Promise<Map<string, number>> {
    const rows = await ctx.env.DB.prepare("SELECT enemy_id, MAX(revision) AS revision FROM enemy_revisions GROUP BY enemy_id").all<{
        enemy_id: string;
        revision: number;
    }>();
    return new Map(rows.results.map((r) => [r.enemy_id, r.revision]));
}

// ------------------------------------------------------------------ the definition

/**
 * Checks a definition as the game's enemy format (EnemyDefinition::fromJson): an object of
 * format cosmic.enemy with this id and a name, finite numbers within sane bounds, lists and
 * objects of reasonable size. Throws a 422 DEFINITION_INVALID listing the problems.
 */
export function checkDefinition(text: string, enemyId: string): Record<string, unknown> {
    const problems: string[] = [];
    const fail = (): never => {
        throw new ApiError(422, "DEFINITION_INVALID", "La définition de l'entité n'est pas valide.", problems);
    };
    let d: unknown;
    try {
        d = JSON.parse(text);
    } catch {
        problems.push("JSON invalide");
        return fail();
    }
    if (!isObject(d)) {
        problems.push("objet JSON attendu");
        return fail();
    }
    if (d.format !== "cosmic.enemy") problems.push("format inconnu (attendu cosmic.enemy)");
    if (d.id !== enemyId) problems.push(`identifiant attendu : ${enemyId}`);
    if (typeof d.name !== "string" || d.name.length < 1 || d.name.length > 80) problems.push("nom invalide (1 à 80 caractères)");
    if (typeof d.animation !== "string" || !/^[A-Za-z0-9_./-]{1,128}$/.test(d.animation) || d.animation.includes(".."))
        problems.push("animation invalide");
    if (!finite(d.hp) || d.hp < 1 || d.hp > 10_000_000) problems.push("hp : de 1 à 10 000 000");
    // Every number of the definition, at any depth: finite and bounded; every list and object bounded.
    let nodes = 0;
    const walk = (v: unknown, path: string, depth: number) => {
        if (problems.length > 20) return;
        if (++nodes > 20_000) return void problems.push("définition trop grande");
        if (depth > 12) return void problems.push(`${path} : trop imbriqué`);
        if (typeof v === "number") {
            if (!Number.isFinite(v) || Math.abs(v) > 10_000_000) problems.push(`${path} : nombre invalide`);
        } else if (typeof v === "string") {
            if (v.length > 2000) problems.push(`${path} : texte trop long`);
        } else if (Array.isArray(v)) {
            if (v.length > 256) problems.push(`${path} : liste trop longue`);
            v.forEach((x, i) => walk(x, `${path}[${i}]`, depth + 1));
        } else if (isObject(v)) {
            const keys = Object.keys(v);
            if (keys.length > 128) problems.push(`${path} : trop de champs`);
            for (const k of keys) walk(v[k], path ? `${path}.${k}` : k, depth + 1);
        } else if (v !== null && typeof v !== "boolean") problems.push(`${path} : valeur invalide`);
    };
    walk(d, "", 0);
    if (problems.length) fail();
    return d;
}

// ------------------------------------------------------------------ administration

/** GET /admin/enemies: every native entity, with its current revision online (null: the game's own definition). */
export async function adminList(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const rows = await ctx.env.DB.prepare(
        `SELECT r.*, c.n FROM enemy_revisions r
         JOIN (SELECT enemy_id, MAX(revision) AS revision, COUNT(*) AS n FROM enemy_revisions GROUP BY enemy_id) c
           ON c.enemy_id = r.enemy_id AND c.revision = r.revision`,
    ).all<RevisionRow & { n: number }>();
    const cur = new Map(rows.results.map((r) => [r.enemy_id, r]));
    return json(ctx, {
        enemies: [...NATIVES].map(([id, name]) => {
            const r = cur.get(id);
            return {
                enemy_id: id,
                name,
                current: r ? { revision: r.revision, published_by: publisherJson(r), published_at: r.published_at } : null,
                revisions: r?.n ?? 0,
            };
        }),
    });
}

/**
 * GET /admin/enemies/{enemyId}/revisions: its revisions (newest first, with the levels each one
 * gave a new revision and their records choice), the next number, the current revision of every
 * entity, and every campaign level with its current revision and best times (the game finds
 * which ones use the entity: the server does not read level files).
 */
export async function adminHistory(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const id = nativeOr404(ctx);
    const [revs, current, levels, maxes, bests] = await Promise.all([
        ctx.env.DB.prepare("SELECT * FROM enemy_revisions WHERE enemy_id = ?1 ORDER BY revision DESC").bind(id).all<RevisionRow>(),
        currentRevisions(ctx),
        ctx.env.DB.prepare("SELECT * FROM levels ORDER BY planet_id, level_order, id").all<LevelRow>(),
        ctx.env.DB.prepare("SELECT level_id, MAX(version) AS max_version FROM level_versions GROUP BY level_id").all<{ level_id: string; max_version: number }>(),
        ctx.env.DB.prepare("SELECT level_hash, COUNT(*) AS n FROM player_bests WHERE level_hash IN (SELECT content_hash FROM levels) GROUP BY level_hash").all<{
            level_hash: string;
            n: number;
        }>(),
    ]);
    const cur = current.get(id) ?? 0;
    const maxOf = new Map(maxes.results.map((m) => [m.level_id, m.max_version]));
    const bestsOf = new Map(bests.results.map((b) => [b.level_hash, b.n]));
    return json(ctx, {
        enemy_id: id,
        name: NATIVES.get(id),
        current_revision: cur,
        next_revision: cur + 1,
        current_revisions: Object.fromEntries(current),
        levels: levels.results.map((l) => ({
            level_id: l.id,
            name: l.name,
            planet_id: l.planet_id,
            planet_name: l.planet_name,
            version: l.current_version,
            next_version: Math.max(maxOf.get(l.id) ?? 0, l.current_version) + 1,
            content_hash: l.content_hash,
            bests: bestsOf.get(l.content_hash) ?? 0,
        })),
        revisions: revs.results.map((r) => ({
            revision: r.revision,
            is_current: r.revision === cur,
            sha256: r.file_sha256,
            size: r.file_size,
            min_game_version: r.min_game_version,
            published_by: publisherJson(r),
            published_at: r.published_at,
            levels: levelsOf(r),
        })),
    });
}

async function refuse(ctx: Ctx, auth: AuthInfo, error: ApiError, details: Record<string, unknown>): Promise<never> {
    await audit(ctx, auth.player.id, "admin_refused", { enemy: details, reason: error.code, by_username: auth.player.username });
    throw error;
}

interface PreparedLevel {
    level: LevelRow;
    version: number;
    records: RecordsChoice;
    bytes: Uint8Array;
    sha: string;
    key: string;
    hash: string;
    manifest: ReturnType<typeof parseManifestLevel>;
    race: unknown;
    rating: Record<string, unknown> | null;
    carried: number;
}

/**
 * POST /admin/enemies/{enemyId}/revisions
 * {definition, base_revision, game_version, levels: [{level_id, base_version, records: "reset"|"keep",
 *  level_file, manifest, rating?}]}
 * `definition`: the entity as the game wrote it (stored byte for byte); `base_revision`: the
 * revision the administrator edited (another publication in between: BASE_CHANGED); `levels`:
 * the next revision of each campaign level using the entity, as for a campaign revision, its
 * file bound to the new enemy revision (metadata.enemyRevisions).
 */
export async function publish(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const enemyId = nativeOr404(ctx);
    const body = await readJsonObject(ctx, 32 * 1024 * 1024);
    const v = new Validator(body, ["definition", "base_revision", "game_version", "levels"]);
    const definition = v.str("definition", { min: 2, max: MAX_DEFINITION_BYTES, multiline: true });
    const baseRevision = v.int("base_revision", { min: 0, max: 1_000_000 });
    const gameVersion = v.str("game_version", { max: 16, pattern: SEMVER_RE });
    const rawLevels = body.levels;
    if (!Array.isArray(rawLevels) || rawLevels.length > MAX_PUBLISH_LEVELS) v.fail("levels", `liste de 0 à ${MAX_PUBLISH_LEVELS} niveaux attendue`);
    v.done();
    const defBytes = new TextEncoder().encode(definition!);
    if (defBytes.byteLength > MAX_DEFINITION_BYTES) throw new ApiError(413, "PAYLOAD_TOO_LARGE", `Définition trop volumineuse (maximum ${MAX_DEFINITION_BYTES} octets).`);
    if (compareVersions(gameVersion!, ctx.env.MIN_GAME_VERSION) < 0)
        throw new ApiError(426, "GAME_VERSION_UNSUPPORTED", "Version du jeu trop ancienne pour publier une révision.");
    checkDefinition(definition!, enemyId);

    const current = await currentRevisions(ctx);
    const cur = current.get(enemyId) ?? 0;
    const revision = cur + 1;
    const what: Record<string, unknown> = { enemy_id: enemyId, enemy_name: NATIVES.get(enemyId), base_revision: baseRevision };
    if (baseRevision !== cur)
        await refuse(ctx, auth, Errors.conflict("BASE_CHANGED", "La définition en ligne a changé entre-temps : rouvrez l'entité.", { current_revision: cur }), what);

    // Each level: the fields, then its file and manifest checked as a campaign revision.
    const items = (rawLevels as unknown[]).map((raw, i) => {
        const where = `levels[${i}]`;
        if (!isObject(raw)) throw Errors.validation([{ field: where, message: "objet attendu" }]);
        const lv = new Validator(raw, ["level_id", "base_version", "records", "level_file", "manifest", "rating"]);
        const levelId = lv.str("level_id", { min: 1, max: 64, pattern: LEVEL_ID_RE });
        const baseVersion = lv.int("base_version", { min: 1, max: 1_000_000 });
        const records = lv.oneOf("records", RECORDS);
        const text = lv.str("level_file", { min: 2, max: MAX_LEVEL_FILE_BYTES, multiline: true });
        const r = raw.rating;
        if (r !== undefined && r !== null && (!isObject(r) || !finite(r.score) || r.score < 0 || r.score > 5 || JSON.stringify(r).length > 4096))
            lv.fail("rating", "note de difficulté invalide");
        if (lv.errors.length) throw Errors.validation(lv.errors.map((e) => ({ field: `${where}.${e.field}`, message: e.message })));
        return { where, levelId: levelId!, baseVersion: baseVersion!, records: records!, text: text!, manifest: raw.manifest, rating: isObject(r) ? r : null };
    });
    const ids = items.map((x) => x.levelId);
    if (new Set(ids).size !== ids.length) throw Errors.validation([{ field: "levels", message: "niveau en double" }]);

    const idsJson = JSON.stringify(ids);
    const [levelRows, maxes, hashes] = await Promise.all([
        ctx.env.DB.prepare("SELECT * FROM levels WHERE id IN (SELECT value FROM json_each(?1))").bind(idsJson).all<LevelRow>(),
        ctx.env.DB.prepare("SELECT level_id, MAX(version) AS max_version FROM level_versions WHERE level_id IN (SELECT value FROM json_each(?1)) GROUP BY level_id")
            .bind(idsJson)
            .all<{ level_id: string; max_version: number }>(),
        ctx.env.DB.prepare("SELECT level_id, version, content_hash FROM level_versions WHERE level_id IN (SELECT value FROM json_each(?1))")
            .bind(idsJson)
            .all<{ level_id: string; version: number; content_hash: string }>(),
    ]);
    const levelOf = new Map(levelRows.results.map((l) => [l.id, l]));
    const maxOf = new Map(maxes.results.map((m) => [m.level_id, m.max_version]));
    const prepared: PreparedLevel[] = [];
    for (const it of items) {
        const level = levelOf.get(it.levelId);
        if (!level) throw Errors.notFound("LEVEL_NOT_FOUND", `Niveau inconnu du serveur : ${it.levelId}.`);
        const version = Math.max(maxOf.get(level.id) ?? 0, level.current_version) + 1;
        if (it.baseVersion !== level.current_version)
            await refuse(ctx, auth, Errors.conflict("BASE_CHANGED", `La révision en ligne de « ${level.name} » a changé entre-temps : rouvrez l'entité.`, {
                level_id: level.id,
                current_version: level.current_version,
            }), what);
        let checked: ReturnType<typeof checkLevelFile>;
        try {
            checked = checkLevelFile(it.text, { id: level.id, planet: level.planet_id, version });
        } catch (e) {
            if (e instanceof ApiError) throw new ApiError(e.status, e.code, `${level.name} : ${e.message}`, e.details);
            throw e;
        }
        // The binding: this entity at the new revision, every other one at a revision that exists.
        const parsed = JSON.parse(it.text) as Record<string, unknown>;
        const binding = isObject(parsed.metadata) ? parsed.metadata.enemyRevisions : undefined;
        const bad: string[] = [];
        if (!isObject(binding)) bad.push("metadata.enemyRevisions attendu");
        else {
            if (binding[enemyId] !== revision) bad.push(`metadata.enemyRevisions.${enemyId} attendu : ${revision}`);
            for (const [k, r] of Object.entries(binding)) {
                if (k === enemyId) continue;
                if (!NATIVES.has(k)) bad.push(`metadata.enemyRevisions.${k} : entité native inconnue`);
                else if (typeof r !== "number" || !Number.isInteger(r) || r < 1 || r > (current.get(k) ?? 0)) bad.push(`metadata.enemyRevisions.${k} : révision inconnue`);
            }
        }
        if (bad.length) throw new ApiError(422, "LEVEL_INVALID", `${level.name} : le fichier du niveau n'est pas valide.`, bad);
        const rawManifest = isObject(it.manifest) ? { ...it.manifest } : it.manifest;
        let race: unknown = null;
        if (isObject(rawManifest) && rawManifest.race !== undefined) {
            race = parseLayout(rawManifest.race);
            if (!race) throw Errors.validation([{ field: `${it.where}.manifest.race`, message: "tracé de course invalide" }]);
            delete rawManifest.race;
        }
        const m = parseManifestLevel(rawManifest, `${it.where}.manifest`);
        const mismatch: string[] = [];
        if (m.id !== level.id) mismatch.push("manifest.id");
        if (m.version !== version) mismatch.push("manifest.version");
        if (m.planet_id !== level.planet_id) mismatch.push("manifest.planet_id");
        if (m.bounds[0] !== checked.bounds[0] || m.bounds[1] !== checked.bounds[1]) mismatch.push("manifest.bounds");
        if (m.exit.some((x, i) => x !== checked.exit[i])) mismatch.push("manifest.exit");
        if (m.min_time_ms >= m.time_limit_ms) mismatch.push("manifest.min_time_ms");
        if (mismatch.length) throw new ApiError(422, "MANIFEST_MISMATCH", `${level.name} : le manifeste ne correspond pas au fichier du niveau.`, mismatch);
        const used = hashes.results.find((h) => h.level_id === level.id && h.content_hash === m.content_hash);
        if (used) throw Errors.conflict("HASH_EXISTS", `${level.name} : ce contenu est déjà la révision ${used.version} de ce niveau.`, { level_id: level.id, version: used.version });
        const bytes = new TextEncoder().encode(it.text);
        const sha = await sha256Hex(bytes);
        prepared.push({
            level,
            version,
            records: it.records,
            bytes,
            sha,
            key: levelFileKey(level.id, version, sha),
            hash: m.content_hash,
            manifest: m,
            race,
            rating: it.rating,
            carried: 0,
        });
    }
    if (new Set(prepared.map((p) => p.hash)).size !== prepared.length) throw Errors.validation([{ field: "levels", message: "deux niveaux ont la même empreinte" }]);

    // The bests carried over by « keep » (for the answer and the Journal).
    const keepHashes = prepared.filter((p) => p.records === "keep").map((p) => p.level.content_hash);
    if (keepHashes.length) {
        const counts = await ctx.env.DB.prepare("SELECT level_hash, COUNT(*) AS n FROM player_bests WHERE level_hash IN (SELECT value FROM json_each(?1)) GROUP BY level_hash")
            .bind(JSON.stringify(keepHashes))
            .all<{ level_hash: string; n: number }>();
        const n = new Map(counts.results.map((c) => [c.level_hash, c.n]));
        for (const p of prepared) if (p.records === "keep") p.carried = n.get(p.level.content_hash) ?? 0;
    }

    // The files first (private bucket), removed again when the batch is refused.
    const defSha = await sha256Hex(defBytes);
    const defKey = fileKey(enemyId, revision, defSha);
    const keys = [defKey, ...prepared.map((p) => p.key)];
    await ctx.env.STORAGE.put(defKey, defBytes, { httpMetadata: { contentType: "application/json; charset=utf-8" }, customMetadata: { sha256: defSha } });
    for (const p of prepared)
        await ctx.env.STORAGE.put(p.key, p.bytes, { httpMetadata: { contentType: "application/json; charset=utf-8" }, customMetadata: { sha256: p.sha } });

    // One batch. Its first statement checks, inside the batch, that every level is still at its
    // base revision and that no other revision of the entity was published (base_checked = 1, a
    // CHECK): otherwise it fails and nothing is kept.
    const now = nowIso();
    const journalLevels = prepared.map((p) => ({
        level_id: p.level.id,
        level_name: p.level.name,
        version: p.version,
        content_hash: p.hash,
        from_version: p.level.current_version,
        records: p.records,
        carried_bests: p.carried,
    }));
    const bases = JSON.stringify(prepared.map((p) => [p.level.id, p.level.current_version, p.level.content_hash]));
    const rows = JSON.stringify(
        prepared.map((p) => ({
            id: p.level.id,
            v: p.version,
            h: p.hash,
            fv: p.level.current_version,
            fh: p.level.content_hash,
            r: p.records,
            c: p.carried,
            key: p.key,
            sha: p.sha,
            size: p.bytes.byteLength,
            rating: p.rating ? JSON.stringify(p.rating) : null,
            race: p.race ? JSON.stringify(p.race) : null,
            tc: p.manifest.total_collectibles,
            te: p.manifest.total_enemies,
            tl: p.manifest.time_limit_ms,
            mt: p.manifest.min_time_ms,
            bw: p.manifest.bounds[0],
            bh: p.manifest.bounds[1],
            sx: p.manifest.spawn[0],
            sy: p.manifest.spawn[1],
            ex: p.manifest.exit[0],
            ey: p.manifest.exit[1],
            ew: p.manifest.exit[2],
            eh: p.manifest.exit[3],
        })),
    );
    const j = (k: string) => `json_extract(value, '$.${k}')`;
    const stmts: D1PreparedStatement[] = [
        ctx.env.DB.prepare(
            `INSERT INTO enemy_revisions (enemy_id, revision, file_key, file_sha256, file_size, min_game_version, levels, published_by,
               published_by_username, published_at, base_checked)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10,
               CASE WHEN COALESCE((SELECT MAX(revision) FROM enemy_revisions WHERE enemy_id = ?1), 0) = ?11
                     AND (SELECT COUNT(*) FROM levels l JOIN json_each(?12) b
                            ON l.id = json_extract(b.value, '$[0]') AND l.current_version = json_extract(b.value, '$[1]')
                           AND l.content_hash = json_extract(b.value, '$[2]')) = json_array_length(?12)
                    THEN 1 ELSE 0 END)`,
        ).bind(enemyId, revision, defKey, defSha, defBytes.byteLength, gameVersion, JSON.stringify(journalLevels), auth.player.id, auth.player.username, now, cur, bases),
    ];
    if (prepared.length) {
        stmts.push(
            ctx.env.DB.prepare(
                `INSERT INTO level_versions (level_id, version, content_hash, compatibility_status, min_game_version, total_collectibles,
                   total_enemies, time_limit_ms, min_time_ms, bounds_w, bounds_h, spawn_x, spawn_y, exit_x, exit_y, exit_w, exit_h, created_at, race)
                 SELECT ${j("id")}, ${j("v")}, ${j("h")}, 'active', ?2, ${j("tc")}, ${j("te")}, ${j("tl")}, ${j("mt")}, ${j("bw")}, ${j("bh")},
                   ${j("sx")}, ${j("sy")}, ${j("ex")}, ${j("ey")}, ${j("ew")}, ${j("eh")}, ?3, ${j("race")} FROM json_each(?1)`,
            ).bind(rows, gameVersion, now),
            ctx.env.DB.prepare(
                `UPDATE levels SET
                   current_version = (SELECT ${j("v")} FROM json_each(?1) WHERE ${j("id")} = levels.id),
                   content_hash = (SELECT ${j("h")} FROM json_each(?1) WHERE ${j("id")} = levels.id)
                 WHERE id IN (SELECT ${j("id")} FROM json_each(?1))`,
            ).bind(rows),
            ctx.env.DB.prepare(
                `UPDATE level_versions SET compatibility_status = 'legacy'
                 WHERE compatibility_status = 'active' AND level_id IN (SELECT ${j("id")} FROM json_each(?1))
                   AND content_hash <> (SELECT ${j("h")} FROM json_each(?1) WHERE ${j("id")} = level_versions.level_id)`,
            ).bind(rows),
            ctx.env.DB.prepare(
                `INSERT INTO campaign_level_files (level_id, version, content_hash, file_key, file_sha256, file_size, rating, published_by,
                   published_by_username, published_at)
                 SELECT ${j("id")}, ${j("v")}, ${j("h")}, ${j("key")}, ${j("sha")}, ${j("size")}, ${j("rating")}, ?2, ?3, ?4 FROM json_each(?1)`,
            ).bind(rows, auth.player.id, auth.player.username, now),
            ctx.env.DB.prepare(
                `INSERT INTO campaign_level_changes (level_id, action, version, content_hash, from_version, from_hash, records, carried_bests,
                   by_player, by_username, created_at)
                 SELECT ${j("id")}, 'publish', ${j("v")}, ${j("h")}, ${j("fv")}, ${j("fh")}, ${j("r")}, ${j("c")}, ?2, ?3, ?4 FROM json_each(?1)`,
            ).bind(rows, auth.player.id, auth.player.username, now),
        );
        for (const p of prepared) if (p.records === "keep") stmts.push(...carryOverStatements(ctx, p.level.id, p.level.content_hash, p.hash, "1 = 1", []));
    }
    try {
        await ctx.env.DB.batch(stmts);
    } catch (e) {
        for (const k of keys) await ctx.env.STORAGE.delete(k);
        const msg = String(e);
        if (msg.includes("CHECK") || msg.includes("UNIQUE"))
            await refuse(ctx, auth, Errors.conflict("BASE_CHANGED", "La définition ou un niveau a changé entre-temps : rouvrez l'entité."), what);
        throw e;
    }
    // 2.66: new revisions of campaign levels: the clans' points follow them.
    await refreshAllClanPoints(ctx.env.DB);
    await audit(ctx, auth.player.id, "admin_enemy_published", {
        enemy: { enemy_id: enemyId, enemy_name: NATIVES.get(enemyId), revision, from_revision: cur, levels: journalLevels },
        by_username: auth.player.username,
    });
    return json(
        ctx,
        {
            enemy_id: enemyId,
            revision,
            sha256: defSha,
            size: defBytes.byteLength,
            path: filePath(enemyId, revision),
            levels: prepared.map((p) => ({
                level_id: p.level.id,
                version: p.version,
                content_hash: p.hash,
                records: p.records,
                carried_bests: p.carried,
                file: { sha256: p.sha, size: p.bytes.byteLength, path: levelFilePath(p.level.id, p.version) },
            })),
        },
        { status: 201 },
    );
}

// ------------------------------------------------------------------ public (the games)

/**
 * GET /enemies/revisions (public): every revision of every native entity, which the games
 * download and keep (a level revision plays the enemy revisions it is bound to).
 */
export async function manifest(ctx: Ctx): Promise<Response> {
    const rows = await ctx.env.DB.prepare("SELECT * FROM enemy_revisions ORDER BY enemy_id, revision").all<RevisionRow>();
    const byId = new Map<string, RevisionRow[]>();
    for (const r of rows.results) {
        if (!NATIVES.has(r.enemy_id)) continue;
        const list = byId.get(r.enemy_id) ?? [];
        list.push(r);
        byId.set(r.enemy_id, list);
    }
    return json(ctx, {
        format: "cosmic.enemy_revisions",
        enemies: [...byId].map(([id, list]) => ({
            enemy_id: id,
            current: list[list.length - 1]!.revision,
            revisions: list.map((r) => ({
                revision: r.revision,
                sha256: r.file_sha256,
                size: r.file_size,
                min_game_version: r.min_game_version,
                path: filePath(id, r.revision),
            })),
        })),
    });
}

/** GET /enemies/{enemyId}/revisions/{revision}/file (public): a published definition, from the private bucket. */
export async function file(ctx: Ctx): Promise<Response> {
    const id = ctx.params.enemyId ?? "";
    const revision = Number(ctx.params.revision ?? "");
    if (!ENEMY_ID_RE.test(id) || !Number.isSafeInteger(revision) || revision < 1) throw Errors.notFound("REVISION_NOT_FOUND", "Révision introuvable.");
    const r = await ctx.env.DB.prepare("SELECT * FROM enemy_revisions WHERE enemy_id = ?1 AND revision = ?2").bind(id, revision).first<RevisionRow>();
    const obj = r ? await ctx.env.STORAGE.get(r.file_key) : null;
    if (!r || !obj) throw Errors.notFound("REVISION_NOT_FOUND", "Révision introuvable.");
    return new Response(obj.body, {
        status: 200,
        headers: {
            "content-type": "application/json; charset=utf-8",
            "x-content-type-options": "nosniff",
            "cache-control": "public, max-age=3600",
            "x-content-sha256": r.file_sha256,
            "x-enemy-revision": String(r.revision),
            "x-request-id": ctx.requestId,
        },
    });
}
