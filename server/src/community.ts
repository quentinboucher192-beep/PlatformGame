// Community levels: players publish their levels (validated .cplanet packages stored in the
// private R2 bucket, served only through these routes), with versions, a completion replay
// of the author (no impossible level), downloads, ratings, reports and a leaderboard per
// level and version. The player is always identified by the session.

import { audit, optionalAuth, requireAuth } from "./auth";
import { sha256Hex } from "./crypto";
import { ApiError, Errors, json, nowIso, readJsonObject, readLimited, type Ctx } from "./http";
import { playerSummary, type LevelVersionRow, type PlayerRow, type RecordRow } from "./model";
import { RULES, rateLimit } from "./ratelimit";
import { analyzeReplay, EventType, inflateContainer, MAX_REPLAY_UPLOAD, parseReplay, PickupKind, ReplayError, type Replay } from "./replay";
import { HASH_RE, PLANET_ID_RE, SEMVER_RE, Validator, paging } from "./validate";
import { listZip, readEntry, safeEntryName, ZipError } from "./zip";

const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
export const MAX_PACKAGE = 16 * 1024 * 1024;
const MAX_UNPACKED = 64 * 1024 * 1024;
const MAX_ENTRIES = 2000;
const MAX_THUMBNAIL = 512 * 1024;
const MAX_LEVELS_PER_AUTHOR = 50;
const REPORTS_TO_HIDE = 5;
const ALLOWED_EXT = [".json", ".png", ".jpg", ".jpeg", ".bmp", ".wav", ".ogg", ".mp3", ".flac", ".ttf", ".otf", ".txt", ".md"];
const ROOT = "COSMIC_LEVEL/";

interface CommunityLevelRow {
    id: string;
    author_id: string;
    title: string;
    description: string;
    planet: string;
    difficulty: number | null;
    status: "draft" | "published" | "hidden" | "removed";
    current_version: number;
    downloads: number;
    completions: number;
    rating_sum: number;
    rating_count: number;
    report_count: number;
    created_at: string;
    updated_at: string;
}

interface VersionRow {
    level_id: string;
    version: number;
    status: "uploading" | "published" | "superseded";
    level_hash: string;
    package_key: string | null;
    package_sha256: string | null;
    package_size: number | null;
    thumbnail_key: string | null;
    replay_key: string | null;
    completion_ms: number | null;
    min_game_version: string;
    bounds_w: number | null;
    bounds_h: number | null;
    spawn_x: number | null;
    spawn_y: number | null;
    exit_x: number | null;
    exit_y: number | null;
    exit_w: number | null;
    exit_h: number | null;
    time_limit_ms: number | null;
    created_at: string;
    published_at: string | null;
}

export function normalizeTitle(s: string): string {
    return s
        .normalize("NFD")
        .replace(/[̀-ͯ]/g, "")
        .toLowerCase();
}

// ------------------------------------------------------------------ package

export interface PackageInfo {
    sha256: string;
    files: number;
    unpacked: number;
    levelId: string;
    levelName: string;
    bounds: [number, number, number, number];
    spawn: [number, number];
    exit: [number, number, number, number];
    timeLimitMs: number;
    thumbnail: Uint8Array | null;
}

/** Checks a .cplanet package without trusting anything in it. Throws ApiError 422. */
export async function inspectPackage(data: Uint8Array): Promise<PackageInfo> {
    const bad = (m: string) => new ApiError(422, "PACKAGE_INVALID", `Package refusé : ${m}.`);
    let entries;
    try {
        entries = listZip(data, MAX_ENTRIES);
    } catch (e) {
        if (e instanceof ZipError) throw bad(e.message);
        throw e;
    }
    let unpacked = 0;
    let files = 0;
    const byName = new Map<string, (typeof entries)[number]>();
    for (const e of entries) {
        if (!safeEntryName(e.name)) throw bad(`chemin dangereux « ${e.name.slice(0, 80)} »`);
        if (!e.name.startsWith(ROOT)) throw bad("fichier hors du dossier COSMIC_LEVEL");
        if (e.name.endsWith("/")) continue;
        const ext = e.name.slice(e.name.lastIndexOf(".")).toLowerCase();
        if (!ALLOWED_EXT.includes(ext)) throw bad(`type de fichier interdit (${ext})`);
        if (e.size > 32 * 1024 * 1024) throw bad("fichier trop volumineux");
        if (e.size > 1024 * 1024 && e.compressedSize > 0 && e.size / e.compressedSize > 250) throw bad("taux de compression suspect");
        if (byName.has(e.name)) throw bad("fichier en double");
        byName.set(e.name, e);
        unpacked += e.size;
        files += 1;
    }
    if (unpacked > MAX_UNPACKED) throw bad("contenu décompressé trop volumineux");
    const readJson = async (name: string): Promise<Record<string, unknown>> => {
        const e = byName.get(ROOT + name);
        if (!e) throw bad(`${name} manquant`);
        if (e.size > 8 * 1024 * 1024) throw bad(`${name} trop volumineux`);
        let text: string;
        try {
            text = new TextDecoder("utf-8", { fatal: true, ignoreBOM: false }).decode(await readEntry(data, e));
        } catch (err) {
            if (err instanceof ZipError) throw bad(err.message);
            throw bad(`${name} n'est pas en UTF-8`);
        }
        let v: unknown;
        try {
            v = JSON.parse(text);
        } catch {
            throw bad(`${name} n'est pas un JSON valide`);
        }
        if (typeof v !== "object" || v === null || Array.isArray(v)) throw bad(`${name} : objet JSON attendu`);
        return v as Record<string, unknown>;
    };
    const manifest = await readJson("manifest.json");
    if (manifest.format !== "cosmic.package") throw bad("manifest.json n'est pas celui d'un package Cosmic Platformer");
    const level = await readJson("level.json");
    if (level.format !== "cosmic.level") throw bad("level.json n'est pas un niveau");
    const levelId = typeof level.id === "string" ? level.id : "";
    if (!/^[a-z0-9_-]{1,64}$/.test(levelId)) throw bad("identifiant de niveau invalide");
    const objects = Array.isArray(level.objects) ? level.objects : null;
    if (!objects || objects.length > 20000) throw bad("liste d'objets absente ou trop longue");
    const nums = (v: unknown, n: number): number[] | null =>
        Array.isArray(v) && v.length === n && v.every((x) => typeof x === "number" && Number.isFinite(x)) ? (v as number[]) : null;
    const bounds = nums(level.bounds, 4);
    if (!bounds || bounds[2]! < 160 || bounds[3]! < 90 || bounds[2]! > 400000 || bounds[3]! > 100000) throw bad("limites du niveau invalides");
    let spawn: [number, number] | null = null;
    let exit: [number, number, number, number] | null = null;
    for (const o of objects) {
        if (typeof o !== "object" || o === null) continue;
        const ob = o as Record<string, unknown>;
        const r = nums(ob.rect, 4);
        if (!r) continue;
        if (ob.type === "player_start" && !spawn) spawn = [r[0]! + r[2]! / 2, r[1]! + r[3]!];
        if (ob.type === "exit" && !exit) exit = [r[0]!, r[1]!, r[2]!, r[3]!];
    }
    if (!spawn) throw bad("aucun départ du joueur");
    if (!exit) throw bad("aucune sortie");
    const timer = typeof level.timer === "object" && level.timer !== null ? (level.timer as Record<string, unknown>) : {};
    const limit = typeof timer.limit === "number" && Number.isFinite(timer.limit) ? timer.limit : 0;
    let thumbnail: Uint8Array | null = null;
    const te = byName.get(ROOT + "metadata/thumbnail.png");
    if (te && te.size <= MAX_THUMBNAIL) {
        const png = await readEntry(data, te).catch(() => null);
        if (png && png.byteLength > 8 && png[0] === 0x89 && png[1] === 0x50 && png[2] === 0x4e && png[3] === 0x47) thumbnail = png;
    }
    return {
        sha256: await sha256Hex(data),
        files,
        unpacked,
        levelId,
        levelName: typeof level.name === "string" ? level.name.slice(0, 80) : levelId,
        bounds: [bounds[0]!, bounds[1]!, bounds[2]!, bounds[3]!],
        spawn,
        exit,
        timeLimitMs: Math.max(0, Math.round(limit * 1000)),
        thumbnail,
    };
}

// ------------------------------------------------------------------ helpers

async function loadLevel(ctx: Ctx, id: string): Promise<CommunityLevelRow> {
    if (!ID_RE.test(id)) throw Errors.notFound("COMMUNITY_LEVEL_NOT_FOUND", "Niveau introuvable.");
    const l = await ctx.env.DB.prepare("SELECT * FROM community_levels WHERE id = ?1").bind(id).first<CommunityLevelRow>();
    if (!l || l.status === "removed") throw Errors.notFound("COMMUNITY_LEVEL_NOT_FOUND", "Niveau introuvable.");
    return l;
}

/** A visible level: published, or any level of its own author. */
async function visibleLevel(ctx: Ctx, id: string, viewer: string | null): Promise<CommunityLevelRow> {
    const l = await loadLevel(ctx, id);
    if (l.status !== "published" && l.author_id !== viewer) throw Errors.notFound("COMMUNITY_LEVEL_NOT_FOUND", "Niveau introuvable.");
    return l;
}

async function loadVersion(ctx: Ctx, id: string, version: number): Promise<VersionRow> {
    const v = await ctx.env.DB.prepare("SELECT * FROM community_level_versions WHERE level_id = ?1 AND version = ?2")
        .bind(id, version)
        .first<VersionRow>();
    if (!v) throw Errors.notFound("COMMUNITY_VERSION_NOT_FOUND", "Version introuvable.");
    return v;
}

function pathVersion(ctx: Ctx): number {
    const v = Number(ctx.params.version);
    if (!Number.isSafeInteger(v) || v < 1 || v > 100000) throw Errors.notFound("COMMUNITY_VERSION_NOT_FOUND", "Version introuvable.");
    return v;
}

/** The replay checks of the official records, against this level version's geometry. */
function pseudoVersion(v: VersionRow): LevelVersionRow {
    return {
        id: 0,
        level_id: v.level_id,
        version: v.version,
        content_hash: v.level_hash,
        compatibility_status: "active",
        min_game_version: v.min_game_version,
        total_collectibles: 0,
        total_enemies: 0,
        time_limit_ms: v.time_limit_ms && v.time_limit_ms > 0 ? v.time_limit_ms : 7200 * 1000,
        min_time_ms: 0,
        max_speed_px_s: 1500,
        bounds_w: v.bounds_w ?? 0,
        bounds_h: v.bounds_h ?? 0,
        spawn_x: v.spawn_x ?? 0,
        spawn_y: v.spawn_y ?? 0,
        exit_x: v.exit_x ?? 0,
        exit_y: v.exit_y ?? 0,
        exit_w: v.exit_w ?? 0,
        exit_h: v.exit_h ?? 0,
        created_at: v.created_at,
    };
}

/**
 * What the run declares, for the replay checks. Collectibles and damage are not part of a
 * community time: they are taken from the replay itself (only the time, the deaths, the
 * level version and the trajectory are checked against the declaration).
 */
function pseudoRecord(rep: Replay, timeMs: number, deaths: number | null, attemptId: string): RecordRow {
    let collectibles = 0;
    let damage = 0;
    let replayDeaths = 0;
    for (const e of rep.events) {
        if (e.type === EventType.Pickup && (e.arg === PickupKind.Shard || e.arg === PickupKind.Relic)) ++collectibles;
        if (e.type === EventType.Damage) damage += e.value;
        if (e.type === EventType.Death) ++replayDeaths;
    }
    return {
        level_id: rep.levelId,
        level_hash: rep.levelHash,
        attempt_id: attemptId,
        time_ms: timeMs,
        deaths: deaths ?? replayDeaths,
        collectibles,
        damage_taken: damage,
    } as unknown as RecordRow;
}

async function readReplay(ctx: Ctx): Promise<{ data: Uint8Array; replay: Replay }> {
    const type = (ctx.request.headers.get("content-type") ?? "").toLowerCase();
    if (!type.startsWith("application/octet-stream"))
        throw new ApiError(415, "UNSUPPORTED_MEDIA_TYPE", "Le replay doit être envoyé en application/octet-stream.");
    const data = await readLimited(ctx.request, MAX_REPLAY_UPLOAD);
    try {
        return { data, replay: parseReplay(await inflateContainer(data)) };
    } catch (e) {
        if (e instanceof ReplayError) throw new ApiError(422, "REPLAY_MALFORMED", `Replay illisible : ${e.message}.`);
        throw e;
    }
}

function card(l: CommunityLevelRow & Partial<PlayerRow> & { best_ms?: number | null; best_player?: string | null; my_stars?: number | null }) {
    return {
        id: l.id,
        title: l.title,
        description: l.description,
        planet: l.planet,
        difficulty: l.difficulty,
        status: l.status,
        version: l.current_version,
        downloads: l.downloads,
        completions: l.completions,
        rating: { average: l.rating_count > 0 ? Math.round((l.rating_sum / l.rating_count) * 10) / 10 : null, count: l.rating_count },
        author: playerSummary({
            id: l.author_id,
            username: l.username ?? "",
            avatar: l.avatar ?? "avatar_01",
            avatar_custom_key: l.avatar_custom_key ?? null,
            country_code: l.country_code ?? null,
            location_visibility: l.location_visibility ?? "hidden",
        } as PlayerRow),
        thumbnail_url: `/api/v1/community/levels/${l.id}/thumbnail`,
        best: l.best_ms != null ? { time_ms: l.best_ms, player: l.best_player ?? null } : null,
        my_rating: l.my_stars ?? null,
        created_at: l.created_at,
        updated_at: l.updated_at,
    };
}

const CARD_SQL = `SELECT l.*, p.username, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility,
    (SELECT MIN(r.time_ms) FROM community_records r WHERE r.level_id = l.id AND r.version = l.current_version AND r.status = 'REPLAY_OK') AS best_ms,
    (SELECT pp.username FROM community_records r JOIN players pp ON pp.id = r.player_id
       WHERE r.level_id = l.id AND r.version = l.current_version AND r.status = 'REPLAY_OK' ORDER BY r.time_ms, r.created_at LIMIT 1) AS best_player,
    (SELECT stars FROM community_ratings cr WHERE cr.level_id = l.id AND cr.player_id = ?1) AS my_stars
  FROM community_levels l JOIN players p ON p.id = l.author_id`;

// ------------------------------------------------------------------- routes

/** GET /community/levels?sort=popular|hardest|recent|top|friends|mine&q=&planet=&limit=&offset= */
export async function listLevels(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const me = viewer?.player.id ?? "";
    const sort = ctx.url.searchParams.get("sort") ?? "popular";
    if (!["popular", "hardest", "recent", "top", "friends", "mine"].includes(sort))
        throw Errors.validation([{ field: "sort", message: "popular, hardest, recent, top, friends ou mine" }]);
    if ((sort === "friends" || sort === "mine") && !viewer) throw Errors.unauthorized();
    const q = (ctx.url.searchParams.get("q") ?? "").trim().slice(0, 60);
    const planet = ctx.url.searchParams.get("planet");
    if (planet && !PLANET_ID_RE.test(planet)) throw Errors.validation([{ field: "planet", message: "format invalide" }]);
    const { limit, offset } = paging(ctx.url, 20, 50);
    // ?1 (the viewer) is always bound: referenced by every filter so that D1 accepts it.
    const where: string[] = [sort === "mine" ? "l.author_id = ?1 AND l.status <> 'removed'" : "l.status = 'published' AND ?1 IS NOT NULL"];
    const args: unknown[] = [me];
    if (sort === "friends") where.push("l.author_id IN (SELECT friend_id FROM player_friends WHERE player_id = ?1 AND status = 'accepted')");
    if (q) {
        args.push("%" + normalizeTitle(q).replace(/[\\%_]/g, (c) => "\\" + c) + "%");
        where.push(`(l.title_norm LIKE ?${args.length} ESCAPE '\\' OR p.username_normalized LIKE ?${args.length} ESCAPE '\\')`);
    }
    if (planet) {
        args.push(planet);
        where.push(`l.planet = ?${args.length}`);
    }
    const order = {
        popular: "(l.downloads + 3 * l.completions + 2 * l.rating_sum) DESC, l.updated_at DESC",
        hardest: "COALESCE(l.difficulty, 0) DESC, l.completions ASC, l.updated_at DESC",
        recent: "l.updated_at DESC",
        // Bayesian average: a level needs several ratings to top the list.
        top: "((l.rating_sum + 9.0) / (l.rating_count + 3.0)) DESC, l.rating_count DESC",
        friends: "l.updated_at DESC",
        mine: "l.updated_at DESC",
    }[sort];
    const total = await ctx.env.DB.prepare(
        `SELECT COUNT(*) AS n FROM community_levels l JOIN players p ON p.id = l.author_id WHERE ${where.join(" AND ")}`,
    )
        .bind(...args)
        .first<{ n: number }>();
    args.push(limit, offset);
    const rows = await ctx.env.DB.prepare(`${CARD_SQL} WHERE ${where.join(" AND ")} ORDER BY ${order} LIMIT ?${args.length - 1} OFFSET ?${args.length}`)
        .bind(...args)
        .all<CommunityLevelRow & PlayerRow & { best_ms: number | null; best_player: string | null; my_stars: number | null }>();
    const n = total?.n ?? 0;
    return json(ctx, { levels: rows.results.map(card), sort }, { meta: { limit, offset, total: n, next_offset: offset + limit < n ? offset + limit : null } });
}

/** GET /community/levels/{id}: card, versions and the leaderboard of the current version. */
export async function getLevel(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const me = viewer?.player.id ?? "";
    const l = await visibleLevel(ctx, ctx.params.levelId!, me || null);
    const row = await ctx.env.DB.prepare(`${CARD_SQL} WHERE l.id = ?2`)
        .bind(me, l.id)
        .first<CommunityLevelRow & PlayerRow & { best_ms: number | null; best_player: string | null; my_stars: number | null }>();
    const versions = await ctx.env.DB.prepare(
        `SELECT version, status, package_size, completion_ms, min_game_version, published_at, created_at FROM community_level_versions
         WHERE level_id = ?1 AND (status <> 'uploading' OR ?2 = 1) ORDER BY version DESC`,
    )
        .bind(l.id, l.author_id === me ? 1 : 0)
        .all();
    return json(ctx, {
        level: card(row!),
        versions: versions.results,
        leaderboard: await board(ctx, l.id, l.current_version, me, 20),
    });
}

async function board(ctx: Ctx, levelId: string, version: number, me: string, limit: number) {
    const rows = await ctx.env.DB.prepare(
        `SELECT r.id, r.player_id, MIN(r.time_ms) AS time_ms, r.deaths, p.username, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility, p.id AS pid
         FROM community_records r JOIN players p ON p.id = r.player_id
         WHERE r.level_id = ?1 AND r.version = ?2 AND r.status = 'REPLAY_OK' AND p.account_status = 'active'
         GROUP BY r.player_id ORDER BY time_ms, MIN(r.created_at) LIMIT ?3`,
    )
        .bind(levelId, version, limit)
        .all<PlayerRow & { id: string; player_id: string; time_ms: number; deaths: number; pid: string }>();
    const entries = rows.results.map((r, i) => ({ rank: i + 1, time_ms: r.time_ms, deaths: r.deaths, record_id: r.id, player: playerSummary({ ...r, id: r.pid }), me: r.player_id === me }));
    let mine = null;
    if (me) {
        const best = await ctx.env.DB.prepare(
            "SELECT MIN(time_ms) AS t FROM community_records WHERE level_id = ?1 AND version = ?2 AND player_id = ?3 AND status = 'REPLAY_OK'",
        )
            .bind(levelId, version, me)
            .first<{ t: number | null }>();
        if (best?.t != null) {
            const better = await ctx.env.DB.prepare(
                `SELECT COUNT(*) AS n FROM (SELECT player_id, MIN(time_ms) AS t FROM community_records
                  WHERE level_id = ?1 AND version = ?2 AND status = 'REPLAY_OK' GROUP BY player_id) WHERE t < ?3`,
            )
                .bind(levelId, version, best.t)
                .first<{ n: number }>();
            mine = { rank: (better?.n ?? 0) + 1, time_ms: best.t };
        }
    }
    return { version, entries, me: mine, verification: "replay vérifié (structure, version du niveau, durée, trajectoire), non re-simulé" };
}

/** GET /community/levels/{id}/leaderboard?version= */
export async function leaderboard(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const me = viewer?.player.id ?? "";
    const l = await visibleLevel(ctx, ctx.params.levelId!, me || null);
    const vq = ctx.url.searchParams.get("version");
    const version = vq ? Number(vq) : l.current_version;
    if (!Number.isSafeInteger(version) || version < 1) throw Errors.validation([{ field: "version", message: "entier attendu" }]);
    return json(ctx, await board(ctx, l.id, version, me, 50));
}

/**
 * POST /community/levels {community_id?, title, description, planet, difficulty, level_hash, min_game_version}:
 * starts a new level (or a new version of one of yours). The package and the completion
 * replay are uploaded next; the version is published once both are accepted.
 */
export async function startPublish(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.communityPublish, "player:" + me);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["community_id", "title", "description", "planet", "difficulty", "level_hash", "min_game_version"]);
    const communityId = v.str("community_id", { min: 36, max: 36, pattern: ID_RE, optional: true });
    const title = v.str("title", { min: 3, max: 60, trim: true });
    const description = v.str("description", { max: 500, trim: true, optional: true }) ?? "";
    const planet = v.str("planet", { min: 1, max: 64, pattern: PLANET_ID_RE });
    const levelHash = v.str("level_hash", { min: 64, max: 64, pattern: HASH_RE });
    const minGame = v.str("min_game_version", { min: 5, max: 14, pattern: SEMVER_RE });
    let difficulty: number | null = null;
    if (body.difficulty !== undefined && body.difficulty !== null) {
        if (typeof body.difficulty !== "number" || !(body.difficulty >= 0.5 && body.difficulty <= 5)) v.fail("difficulty", "entre 0,5 et 5");
        else difficulty = Math.round(body.difficulty * 2) / 2;
    }
    v.done();
    const now = nowIso();
    let id: string;
    let version: number;
    if (communityId) {
        const l = await loadLevel(ctx, communityId);
        if (l.author_id !== me) throw Errors.forbidden("NOT_AUTHOR", "Seul l'auteur peut publier une nouvelle version.");
        if (l.status === "hidden") throw Errors.forbidden("LEVEL_HIDDEN", "Ce niveau est masqué après des signalements : contactez la modération.");
        // An unfinished upload is replaced.
        await ctx.env.DB.prepare("DELETE FROM community_level_versions WHERE level_id = ?1 AND status = 'uploading'").bind(l.id).run();
        const last = await ctx.env.DB.prepare("SELECT MAX(version) AS v FROM community_level_versions WHERE level_id = ?1").bind(l.id).first<{ v: number | null }>();
        id = l.id;
        version = (last?.v ?? 0) + 1;
        await ctx.env.DB.prepare("UPDATE community_levels SET title = ?2, title_norm = ?3, description = ?4, planet = ?5, difficulty = ?6 WHERE id = ?1")
            .bind(id, title!, normalizeTitle(title!), description, planet!, difficulty)
            .run();
    } else {
        const count = await ctx.env.DB.prepare("SELECT COUNT(*) AS n FROM community_levels WHERE author_id = ?1 AND status <> 'removed'")
            .bind(me)
            .first<{ n: number }>();
        if ((count?.n ?? 0) >= MAX_LEVELS_PER_AUTHOR) throw Errors.conflict("LEVEL_LIMIT", `Au plus ${MAX_LEVELS_PER_AUTHOR} niveaux publiés par joueur.`);
        id = crypto.randomUUID();
        version = 1;
        await ctx.env.DB.prepare(
            `INSERT INTO community_levels (id, author_id, title, title_norm, description, planet, difficulty, status, created_at, updated_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, 'draft', ?8, ?8)`,
        )
            .bind(id, me, title!, normalizeTitle(title!), description, planet!, difficulty, now)
            .run();
    }
    await ctx.env.DB.prepare(
        "INSERT INTO community_level_versions (level_id, version, status, level_hash, min_game_version, created_at) VALUES (?1, ?2, 'uploading', ?3, ?4, ?5)",
    )
        .bind(id, version, levelHash!, minGame!, now)
        .run();
    const base = `/api/v1/community/levels/${id}/versions/${version}`;
    return json(ctx, { id, version, upload: { package: base + "/package", replay: base + "/replay" } }, { status: 201 });
}

async function authorVersion(ctx: Ctx): Promise<{ l: CommunityLevelRow; v: VersionRow; me: string }> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.communityUpload, "player:" + me);
    const l = await loadLevel(ctx, ctx.params.levelId!);
    if (l.author_id !== me) throw Errors.forbidden("NOT_AUTHOR", "Ce niveau ne vous appartient pas.");
    const v = await loadVersion(ctx, l.id, pathVersion(ctx));
    if (v.status !== "uploading") throw Errors.conflict("VERSION_PUBLISHED", "Cette version est déjà publiée.");
    return { l, v, me };
}

/** PUT /community/levels/{id}/versions/{version}/package (application/octet-stream) */
export async function uploadPackage(ctx: Ctx): Promise<Response> {
    const { l, v } = await authorVersion(ctx);
    const type = (ctx.request.headers.get("content-type") ?? "").toLowerCase();
    if (!type.startsWith("application/octet-stream"))
        throw new ApiError(415, "UNSUPPORTED_MEDIA_TYPE", "Le package doit être envoyé en application/octet-stream.");
    const data = await readLimited(ctx.request, MAX_PACKAGE);
    const info = await inspectPackage(data);
    const key = `community/${l.id}/v${v.version}.cplanet`;
    await ctx.env.STORAGE.put(key, data, {
        httpMetadata: { contentType: "application/octet-stream" },
        customMetadata: { level_id: l.id, version: String(v.version), sha256: info.sha256 },
    });
    let thumbKey: string | null = null;
    if (info.thumbnail) {
        thumbKey = `community/${l.id}/v${v.version}.png`;
        await ctx.env.STORAGE.put(thumbKey, info.thumbnail, { httpMetadata: { contentType: "image/png" } });
    }
    await ctx.env.DB.prepare(
        `UPDATE community_level_versions SET package_key = ?3, package_sha256 = ?4, package_size = ?5, thumbnail_key = ?6,
           bounds_w = ?7, bounds_h = ?8, spawn_x = ?9, spawn_y = ?10, exit_x = ?11, exit_y = ?12, exit_w = ?13, exit_h = ?14, time_limit_ms = ?15
         WHERE level_id = ?1 AND version = ?2`,
    )
        .bind(l.id, v.version, key, info.sha256, data.byteLength, thumbKey, info.bounds[2], info.bounds[3], info.spawn[0], info.spawn[1], info.exit[0],
            info.exit[1], info.exit[2], info.exit[3], info.timeLimitMs)
        .run();
    const published = await tryPublish(ctx, l.id, v.version);
    return json(ctx, { package: { sha256: info.sha256, size: data.byteLength, files: info.files, thumbnail: thumbKey !== null }, published });
}

/** PUT /community/levels/{id}/versions/{version}/replay: the author's completion run. */
export async function uploadCompletion(ctx: Ctx): Promise<Response> {
    const { l, v } = await authorVersion(ctx);
    if (!v.package_key) throw Errors.conflict("PACKAGE_MISSING", "Envoyez d'abord le package du niveau.");
    const { data, replay } = await readReplay(ctx);
    if (replay.levelHash !== v.level_hash)
        throw new ApiError(422, "REPLAY_LEVEL_MISMATCH", "Ce replay n'a pas été joué sur cette version du niveau : terminez la version publiée.");
    const analysis = analyzeReplay(replay, pseudoRecord(replay, replay.durationMs, null, replay.attemptId), pseudoVersion(v));
    if (!analysis.ok)
        throw new ApiError(422, "REPLAY_REJECTED", "Le replay de fin de niveau n'est pas valable.", { reasons: analysis.reasons.slice(0, 8) });
    const key = `community/${l.id}/v${v.version}.completion.cprz`;
    await ctx.env.STORAGE.put(key, data, { httpMetadata: { contentType: "application/octet-stream" } });
    await ctx.env.DB.prepare("UPDATE community_level_versions SET replay_key = ?3, completion_ms = ?4 WHERE level_id = ?1 AND version = ?2")
        .bind(l.id, v.version, key, replay.durationMs)
        .run();
    const published = await tryPublish(ctx, l.id, v.version);
    return json(ctx, { completion_ms: replay.durationMs, published });
}

async function tryPublish(ctx: Ctx, id: string, version: number): Promise<boolean> {
    const v = await loadVersion(ctx, id, version);
    if (!v.package_key || !v.replay_key) return false;
    const now = nowIso();
    await ctx.env.DB.batch([
        ctx.env.DB.prepare("UPDATE community_level_versions SET status = 'superseded' WHERE level_id = ?1 AND status = 'published'").bind(id),
        ctx.env.DB.prepare("UPDATE community_level_versions SET status = 'published', published_at = ?3 WHERE level_id = ?1 AND version = ?2").bind(
            id,
            version,
            now,
        ),
        ctx.env.DB.prepare(
            "UPDATE community_levels SET current_version = ?2, status = CASE WHEN status = 'hidden' THEN 'hidden' ELSE 'published' END, updated_at = ?3 WHERE id = ?1",
        ).bind(id, version, now),
    ]);
    const l = await loadLevel(ctx, id);
    await audit(ctx, l.author_id, "community_level_published", { level_id: id, version });
    return true;
}

/** GET /community/levels/{id}/package?version= : the .cplanet (published versions; the author's own uploads). */
export async function downloadPackage(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const me = viewer?.player.id ?? null;
    await rateLimit(ctx, RULES.communityDownload, me ? "player:" + me : "ip:" + ctx.ip);
    const l = await visibleLevel(ctx, ctx.params.levelId!, me);
    const vq = ctx.url.searchParams.get("version");
    const version = vq ? Number(vq) : l.current_version;
    if (!Number.isSafeInteger(version) || version < 1) throw Errors.notFound("COMMUNITY_VERSION_NOT_FOUND", "Version introuvable.");
    const v = await loadVersion(ctx, l.id, version);
    if ((v.status === "uploading" && l.author_id !== me) || !v.package_key) throw Errors.notFound("COMMUNITY_VERSION_NOT_FOUND", "Version introuvable.");
    const obj = await ctx.env.STORAGE.get(v.package_key);
    if (!obj) throw Errors.notFound("COMMUNITY_VERSION_NOT_FOUND", "Fichier introuvable.");
    if (me !== l.author_id) await ctx.env.DB.prepare("UPDATE community_levels SET downloads = downloads + 1 WHERE id = ?1").bind(l.id).run();
    return new Response(obj.body, {
        headers: {
            "content-type": "application/octet-stream",
            "content-disposition": `attachment; filename="${l.id}-v${version}.cplanet"`,
            "x-content-sha256": v.package_sha256 ?? "",
            "x-level-hash": v.level_hash,
            "x-request-id": ctx.requestId,
            "x-content-type-options": "nosniff",
        },
    });
}

/** GET /community/levels/{id}/thumbnail */
export async function thumbnail(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const l = await visibleLevel(ctx, ctx.params.levelId!, viewer?.player.id ?? null);
    const v = await ctx.env.DB.prepare(
        "SELECT thumbnail_key FROM community_level_versions WHERE level_id = ?1 AND thumbnail_key IS NOT NULL ORDER BY (version = ?2) DESC, version DESC LIMIT 1",
    )
        .bind(l.id, l.current_version)
        .first<{ thumbnail_key: string }>();
    const obj = v ? await ctx.env.STORAGE.get(v.thumbnail_key) : null;
    if (!obj) throw Errors.notFound("THUMBNAIL_NOT_FOUND", "Pas de vignette.");
    return new Response(obj.body, {
        headers: { "content-type": "image/png", "cache-control": "public, max-age=3600", "x-content-type-options": "nosniff" },
    });
}

/** POST /community/levels/{id}/rate {stars}: one rating per player (changeable), never your own level. */
export async function rate(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.communityRate, "player:" + me);
    const l = await visibleLevel(ctx, ctx.params.levelId!, null);
    if (l.author_id === me) throw Errors.forbidden("OWN_LEVEL", "Vous ne pouvez pas noter votre propre niveau.");
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["stars"]);
    const stars = v.int("stars", { min: 1, max: 5 });
    v.done();
    await ctx.env.DB.batch([
        ctx.env.DB.prepare(
            `INSERT INTO community_ratings (level_id, player_id, stars, created_at) VALUES (?1, ?2, ?3, ?4)
             ON CONFLICT (level_id, player_id) DO UPDATE SET stars = excluded.stars, created_at = excluded.created_at`,
        ).bind(l.id, me, stars!, nowIso()),
        ctx.env.DB.prepare(
            `UPDATE community_levels SET rating_sum = (SELECT COALESCE(SUM(stars), 0) FROM community_ratings WHERE level_id = ?1),
               rating_count = (SELECT COUNT(*) FROM community_ratings WHERE level_id = ?1) WHERE id = ?1`,
        ).bind(l.id),
    ]);
    const after = await loadLevel(ctx, l.id);
    return json(ctx, { stars, rating: { average: Math.round((after.rating_sum / Math.max(1, after.rating_count)) * 10) / 10, count: after.rating_count } });
}

/** POST /community/levels/{id}/report {reason}: reported to the moderators; hidden after several reports. */
export async function report(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.communityReport, "player:" + me);
    const l = await visibleLevel(ctx, ctx.params.levelId!, null);
    if (l.author_id === me) throw Errors.validation([{ field: "level", message: "vous ne pouvez pas signaler votre propre niveau" }]);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["reason"]);
    const reason = v.str("reason", { max: 200, optional: true, trim: true });
    v.done();
    const res = await ctx.env.DB.prepare(
        "INSERT OR IGNORE INTO community_reports (id, level_id, reporter_id, reason, created_at) VALUES (?1, ?2, ?3, ?4, ?5)",
    )
        .bind(crypto.randomUUID(), l.id, me, reason ?? null, nowIso())
        .run();
    if (res.meta.changes) {
        await ctx.env.DB.prepare(
            `UPDATE community_levels SET report_count = report_count + 1,
               status = CASE WHEN report_count + 1 >= ?2 AND status = 'published' THEN 'hidden' ELSE status END WHERE id = ?1`,
        )
            .bind(l.id, REPORTS_TO_HIDE)
            .run();
        await audit(ctx, me, "community_level_reported", { level_id: l.id, reason: reason ?? null });
    }
    return json(ctx, { reported: true }, { status: 201 });
}

/** DELETE /community/levels/{id}: the author withdraws the level (files deleted). */
export async function removeLevel(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const l = await loadLevel(ctx, ctx.params.levelId!);
    if (l.author_id !== auth.player.id) throw Errors.forbidden("NOT_AUTHOR", "Ce niveau ne vous appartient pas.");
    const keys = await communityKeys(ctx, "l.id = ?1", l.id);
    for (let i = 0; i < keys.length; i += 500) await ctx.env.STORAGE.delete(keys.slice(i, i + 500));
    await ctx.env.DB.prepare("DELETE FROM community_levels WHERE id = ?1").bind(l.id).run();
    await audit(ctx, auth.player.id, "community_level_removed", { level_id: l.id });
    return json(ctx, { removed: true });
}

/** Every R2 key of the community levels matching `where` (alias l) and of their records. */
export async function communityKeys(ctx: Ctx, where: string, arg: string): Promise<string[]> {
    const v = await ctx.env.DB.prepare(
        `SELECT v.package_key, v.thumbnail_key, v.replay_key FROM community_level_versions v JOIN community_levels l ON l.id = v.level_id WHERE ${where}`,
    )
        .bind(arg)
        .all<{ package_key: string | null; thumbnail_key: string | null; replay_key: string | null }>();
    const r = await ctx.env.DB.prepare(`SELECT r.replay_key FROM community_records r JOIN community_levels l ON l.id = r.level_id WHERE ${where}`)
        .bind(arg)
        .all<{ replay_key: string | null }>();
    const keys: string[] = [];
    for (const x of v.results) for (const k of [x.package_key, x.thumbnail_key, x.replay_key]) if (k) keys.push(k);
    for (const x of r.results) if (x.replay_key) keys.push(x.replay_key);
    return keys;
}

/** POST /community/levels/{id}/records {version, attempt_id, time_ms, deaths}: then PUT the replay. */
export async function submitRecord(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.communityRecord, "player:" + me);
    const l = await visibleLevel(ctx, ctx.params.levelId!, null);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["version", "attempt_id", "time_ms", "deaths"]);
    const version = v.int("version", { min: 1, max: 100000 });
    const attempt = v.str("attempt_id", { min: 36, max: 36, pattern: ID_RE });
    const timeMs = v.int("time_ms", { min: 1000, max: 7200 * 1000 });
    const deaths = v.int("deaths", { min: 0, max: 100000, optional: true }) ?? 0;
    v.done();
    const ver = await loadVersion(ctx, l.id, version!);
    if (ver.status === "uploading") throw Errors.notFound("COMMUNITY_VERSION_NOT_FOUND", "Version introuvable.");
    const id = crypto.randomUUID();
    try {
        await ctx.env.DB.prepare(
            "INSERT INTO community_records (id, level_id, version, player_id, attempt_id, time_ms, deaths, status, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, 'PENDING', ?8)",
        )
            .bind(id, l.id, version!, me, attempt!, timeMs!, deaths, nowIso())
            .run();
    } catch (e) {
        if (String(e).includes("UNIQUE")) throw Errors.conflict("ATTEMPT_EXISTS", "Cette tentative a déjà été envoyée.");
        throw e;
    }
    return json(ctx, { record: { id, status: "PENDING" }, upload: `/api/v1/community/records/${id}/replay` }, { status: 201 });
}

/** PUT /community/records/{id}/replay: the time counts once its replay is accepted. */
export async function uploadRecordReplay(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.replayUpload, "player:" + me);
    const rid = ctx.params.recordId!;
    if (!ID_RE.test(rid)) throw Errors.notFound("RECORD_NOT_FOUND", "Record introuvable.");
    const rec = await ctx.env.DB.prepare("SELECT * FROM community_records WHERE id = ?1").bind(rid).first<{
        id: string;
        level_id: string;
        version: number;
        player_id: string;
        attempt_id: string;
        time_ms: number;
        deaths: number;
        status: string;
        replay_key: string | null;
    }>();
    if (!rec || rec.player_id !== me) throw Errors.notFound("RECORD_NOT_FOUND", "Record introuvable.");
    if (rec.replay_key || rec.status !== "PENDING") throw Errors.conflict("REPLAY_EXISTS", "Un replay est déjà associé à ce temps.");
    const ver = await loadVersion(ctx, rec.level_id, rec.version);
    const { data, replay } = await readReplay(ctx);
    const reasons: string[] = [];
    if (replay.levelHash !== ver.level_hash) reasons.push("version de niveau différente");
    if (replay.attemptId !== rec.attempt_id) reasons.push("tentative différente");
    const analysis = analyzeReplay(replay, pseudoRecord(replay, rec.time_ms, rec.deaths, rec.attempt_id), pseudoVersion(ver));
    reasons.push(...analysis.reasons.filter((r) => !reasons.includes(r)));
    const ok = reasons.length === 0;
    const key = `community/${rec.level_id}/records/${me}/${rec.attempt_id}.cprz`;
    if (ok) await ctx.env.STORAGE.put(key, data, { httpMetadata: { contentType: "application/octet-stream" } });
    const firstCompletion = ok
        ? (await ctx.env.DB.prepare("SELECT COUNT(*) AS n FROM community_records WHERE level_id = ?1 AND player_id = ?2 AND status = 'REPLAY_OK'")
              .bind(rec.level_id, me)
              .first<{ n: number }>())!.n === 0
        : false;
    await ctx.env.DB.prepare("UPDATE community_records SET status = ?2, replay_key = ?3, reasons = ?4 WHERE id = ?1")
        .bind(rec.id, ok ? "REPLAY_OK" : "REJECTED", ok ? key : null, ok ? null : JSON.stringify(reasons.slice(0, 8)))
        .run();
    if (firstCompletion) await ctx.env.DB.prepare("UPDATE community_levels SET completions = completions + 1 WHERE id = ?1").bind(rec.level_id).run();
    const b = await board(ctx, rec.level_id, rec.version, me, 3);
    return json(ctx, { record: { id: rec.id, status: ok ? "REPLAY_OK" : "REJECTED", reasons: ok ? [] : reasons.slice(0, 8) }, me: b.me });
}

/** GET /community/records/{id}/replay: replays of accepted times are public. */
export async function downloadRecordReplay(ctx: Ctx): Promise<Response> {
    const rid = ctx.params.recordId!;
    if (!ID_RE.test(rid)) throw Errors.notFound("RECORD_NOT_FOUND", "Record introuvable.");
    const rec = await ctx.env.DB.prepare("SELECT replay_key, status FROM community_records WHERE id = ?1").bind(rid).first<{ replay_key: string | null; status: string }>();
    if (!rec || rec.status !== "REPLAY_OK" || !rec.replay_key) throw Errors.notFound("RECORD_NOT_FOUND", "Record introuvable.");
    const obj = await ctx.env.STORAGE.get(rec.replay_key);
    if (!obj) throw Errors.notFound("RECORD_NOT_FOUND", "Replay introuvable.");
    return new Response(obj.body, { headers: { "content-type": "application/octet-stream", "x-content-type-options": "nosniff" } });
}
