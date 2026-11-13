// Record submission and verification.
//
// Validation levels (in order): schema -> level exists and is ranked -> exact level
// revision (id + version + content hash) -> compatible game version -> category rules ->
// progression consistency -> duplicate attempt -> impossible times -> replay analysis and
// trust level. The client is never trusted: a submitted time is only VERIFIED by the
// server's own decision.

import { audit, requireAuth, type AuthInfo } from "./auth";
import { sha256Hex } from "./crypto";
import { ApiError, Errors, json, nowIso, readJsonObject, readLimited, type Ctx } from "./http";
import { ensureRankingEnabled, getLevel } from "./levels";
import { playerSummary, recordJson, type LevelVersionRow, type PlayerRow, type RecordRow } from "./model";
import { RULES, rateLimit } from "./ratelimit";
import { MAX_REPLAY_UPLOAD, ReplayError, analyzeReplay, inflateContainer, parseReplay, type ReplayAnalysis } from "./replay";
import {
    CATEGORIES,
    HASH_RE,
    LEVEL_ID_RE,
    MODES,
    SEMVER_RE,
    UUID_RE,
    Validator,
    compareVersions,
    type Category,
} from "./validate";

type ReplayState = "none" | "valid" | "invalid";

interface Decision {
    status: RecordRow["verification_status"];
    reason: string | null;
    note: string | null;
}

/**
 * Trust levels: 0 new account, 1 has verified runs with replays, 2 long clean history,
 * 3 set by moderators. A new world record on an established board by an account below
 * level 2, or any record crushing the world record, is reviewed manually.
 */
export interface BoardContext {
    /** Rank this time would take among the verified bests of other players. */
    rank: number;
    /** Number of players ranked on the board. */
    size: number;
    /** Current world record (ms), null on an empty board. */
    worldRecordMs: number | null;
}

export function decide(trust: number, category: Category, replay: ReplayState, timeMs: number, board: BoardContext): Decision {
    if (replay === "invalid") return { status: "REJECTED", reason: "REPLAY_INVALID", note: null };
    if (category === "SPEEDRUN" && replay !== "valid")
        return { status: "PENDING", reason: null, note: "La catégorie SPEEDRUN exige un replay valide." };
    if (board.rank === 1 && board.worldRecordMs !== null && timeMs < board.worldRecordMs * 0.8 && trust < 3)
        return { status: "PENDING", reason: null, note: "Record mondial battu de plus de 20 % : vérification manuelle." };
    if (board.rank === 1 && board.size >= 5 && trust < 2)
        return { status: "PENDING", reason: null, note: "Nouveau record mondial : vérification manuelle." };
    if (replay === "valid") return { status: "VERIFIED", reason: null, note: null };
    if (trust >= 2) return { status: "VERIFIED", reason: null, note: null };
    return { status: "PENDING", reason: null, note: "En attente du replay." };
}

/** Position this time would take on the board (other players' verified bests). */
async function boardContext(ctx: Ctx, levelHash: string, category: string, timeMs: number, playerId: string): Promise<BoardContext> {
    const row = await ctx.env.DB.prepare(
        `SELECT SUM(CASE WHEN b.time_ms <= ?3 THEN 1 ELSE 0 END) AS better, COUNT(*) AS size, MIN(b.time_ms) AS wr
         FROM player_bests b JOIN players p ON p.id = b.player_id
         WHERE b.level_hash = ?1 AND b.category = ?2 AND b.player_id <> ?4 AND p.account_status = 'active'`,
    )
        .bind(levelHash, category, timeMs, playerId)
        .first<{ better: number | null; size: number; wr: number | null }>();
    return { rank: (row?.better ?? 0) + 1, size: row?.size ?? 0, worldRecordMs: row?.wr ?? null };
}

/** Current rank of a player on a board (null when not ranked). */
export async function playerRank(ctx: Ctx, levelHash: string, category: string, playerId: string): Promise<number | null> {
    const row = await ctx.env.DB.prepare(
        `SELECT (SELECT COUNT(*) FROM player_bests o JOIN players p ON p.id = o.player_id
                 WHERE o.level_hash = b.level_hash AND o.category = b.category AND p.account_status = 'active'
                   AND (o.time_ms < b.time_ms OR (o.time_ms = b.time_ms AND (o.achieved_at < b.achieved_at
                        OR (o.achieved_at = b.achieved_at AND o.player_id < b.player_id))))) + 1 AS rank
         FROM player_bests b WHERE b.player_id = ?1 AND b.level_hash = ?2 AND b.category = ?3`,
    )
        .bind(playerId, levelHash, category)
        .first<{ rank: number }>();
    return row ? row.rank : null;
}

async function boardSize(ctx: Ctx, levelHash: string, category: string): Promise<number> {
    const row = await ctx.env.DB.prepare(
        `SELECT COUNT(*) AS n FROM player_bests b JOIN players p ON p.id = b.player_id
         WHERE b.level_hash = ?1 AND b.category = ?2 AND p.account_status = 'active'`,
    )
        .bind(levelHash, category)
        .first<{ n: number }>();
    return row?.n ?? 0;
}

/** Updates the historic best of the player if this verified record improves it. */
async function updateBest(ctx: Ctx, r: RecordRow): Promise<{ improved: boolean; previousMs: number | null }> {
    const prev = await ctx.env.DB.prepare("SELECT time_ms FROM player_bests WHERE player_id = ?1 AND level_hash = ?2 AND category = ?3")
        .bind(r.player_id, r.level_hash, r.category)
        .first<{ time_ms: number }>();
    if (prev && prev.time_ms <= r.time_ms) return { improved: false, previousMs: prev.time_ms };
    await ctx.env.DB.prepare(
        `INSERT INTO player_bests (player_id, level_id, level_hash, category, record_id, time_ms, achieved_at)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
         ON CONFLICT (player_id, level_hash, category) DO UPDATE SET record_id = ?5, time_ms = ?6, achieved_at = ?7
         WHERE excluded.time_ms < player_bests.time_ms`,
    )
        .bind(r.player_id, r.level_id, r.level_hash, r.category, r.id, r.time_ms, r.created_at)
        .run();
    return { improved: true, previousMs: prev ? prev.time_ms : null };
}

/** Promotes / demotes the trust level after a verification decision. */
async function updateTrust(ctx: Ctx, playerId: string): Promise<void> {
    const stats = await ctx.env.DB.prepare(
        `SELECT
           (SELECT COUNT(*) FROM records WHERE player_id = ?1 AND verification_status = 'VERIFIED' AND replay_id IS NOT NULL) AS verified,
           (SELECT COUNT(*) FROM records WHERE player_id = ?1 AND verification_status = 'REJECTED' AND created_at >= ?2) AS rejected,
           (SELECT trust_level FROM players WHERE id = ?1) AS trust`,
    )
        .bind(playerId, new Date(Date.now() - 30 * 86400000).toISOString())
        .first<{ verified: number; rejected: number; trust: number }>();
    if (!stats) return;
    let trust = stats.trust;
    if (stats.rejected > 0) trust = Math.min(trust, 0);
    else if (trust < 3) {
        if (stats.verified >= 15) trust = Math.max(trust, 2);
        else if (stats.verified >= 3) trust = Math.max(trust, 1);
    }
    if (trust !== stats.trust) {
        await ctx.env.DB.prepare("UPDATE players SET trust_level = ?1 WHERE id = ?2").bind(trust, playerId).run();
        await audit(ctx, playerId, "trust_level_changed", { from: stats.trust, to: trust });
    }
}

export interface RankingChange {
    previous_rank: number | null;
    rank: number | null;
    total: number;
    personal_best_improved: boolean;
    previous_best_ms: number | null;
}

/** Applies a decision to a stored record; maintains bests and trust. */
export async function applyDecision(ctx: Ctx, r: RecordRow, d: Decision): Promise<RankingChange> {
    const previousRank = await playerRank(ctx, r.level_hash, r.category, r.player_id);
    const now = nowIso();
    await ctx.env.DB.prepare(
        "UPDATE records SET verification_status = ?1, rejection_reason = ?2, verification_notes = ?3, verified_at = ?4 WHERE id = ?5",
    )
        .bind(d.status, d.reason, d.note, d.status === "VERIFIED" ? now : null, r.id)
        .run();
    r.verification_status = d.status;
    r.rejection_reason = d.reason;
    r.verification_notes = d.note;
    r.verified_at = d.status === "VERIFIED" ? now : null;
    let improved = false;
    let previousMs: number | null = null;
    if (d.status === "VERIFIED") ({ improved, previousMs } = await updateBest(ctx, r));
    else {
        const best = await ctx.env.DB.prepare("SELECT time_ms FROM player_bests WHERE player_id = ?1 AND level_hash = ?2 AND category = ?3")
            .bind(r.player_id, r.level_hash, r.category)
            .first<{ time_ms: number }>();
        previousMs = best?.time_ms ?? null;
    }
    if (d.status !== "PENDING") await updateTrust(ctx, r.player_id);
    return {
        previous_rank: previousRank,
        rank: await playerRank(ctx, r.level_hash, r.category, r.player_id),
        total: await boardSize(ctx, r.level_hash, r.category),
        personal_best_improved: improved,
        previous_best_ms: previousMs,
    };
}

// -------------------------------------------------------------------- submit

export async function submit(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.submit, "player:" + auth.player.id);
    const body = await readJsonObject(ctx, 4 * 1024);
    // `player_id` is deliberately not accepted: the player comes from the session.
    const v = new Validator(body, [
        "level_id", "level_version", "level_hash", "mode", "category", "time_ms", "deaths", "damage_taken",
        "collectibles", "total_collectibles", "kills", "score", "game_version", "attempt_id", "has_replay",
    ]);
    const levelId = v.str("level_id", { max: 64, pattern: LEVEL_ID_RE });
    const levelVersion = v.int("level_version", { min: 1, max: 1_000_000 });
    const levelHash = v.str("level_hash", { min: 64, max: 64, pattern: HASH_RE });
    const mode = v.oneOf("mode", MODES, { optional: true }) ?? "STANDARD";
    const category = v.oneOf("category", CATEGORIES);
    const timeMs = v.int("time_ms", { min: 1, max: 24 * 3600 * 1000 });
    const deaths = v.int("deaths", { min: 0, max: 9999 });
    const damage = v.int("damage_taken", { min: 0, max: 1_000_000, optional: true }) ?? 0;
    const collectibles = v.int("collectibles", { min: 0, max: 10_000, optional: true }) ?? 0;
    const totalCollectibles = v.int("total_collectibles", { min: 0, max: 10_000, optional: true }) ?? 0;
    const kills = v.int("kills", { min: 0, max: 100_000, optional: true }) ?? 0;
    const score = v.int("score", { min: 0, max: 100_000_000, optional: true }) ?? 0;
    const gameVersion = v.str("game_version", { max: 16, pattern: SEMVER_RE });
    const attemptId = v.str("attempt_id", { min: 36, max: 36, pattern: UUID_RE });
    const hasReplay = v.bool("has_replay", { optional: true }) ?? false;
    v.done();

    // Level and exact revision.
    const level = await getLevel(ctx, levelId!);
    ensureRankingEnabled(level);
    const version = await ctx.env.DB.prepare("SELECT * FROM level_versions WHERE level_id = ?1 AND version = ?2")
        .bind(level.id, levelVersion)
        .first<LevelVersionRow>();
    if (!version || version.content_hash !== levelHash)
        throw Errors.conflict("LEVEL_VERSION_MISMATCH", "Cette version du niveau n'est pas reconnue par le serveur.", {
            current_version: level.current_version,
        });
    if (version.compatibility_status !== "active")
        throw Errors.conflict("LEVEL_VERSION_RETIRED", "Cette version du niveau n'est plus classée.", {
            current_version: level.current_version,
        });
    if (compareVersions(gameVersion!, version.min_game_version) < 0 || compareVersions(gameVersion!, ctx.env.MIN_GAME_VERSION) < 0)
        throw new ApiError(426, "GAME_VERSION_UNSUPPORTED", "Version du jeu trop ancienne pour le classement en ligne.", {
            min_game_version: version.min_game_version,
        });

    // Category rules: a client that submits a run in a category it does not meet is buggy
    // or modified; nothing is stored.
    const ruleErrors: string[] = [];
    if (category === "HUNDRED_PERCENT" && collectibles < version.total_collectibles) ruleErrors.push("tous les objets ne sont pas récupérés");
    if ((category === "NO_DEATH" || category === "NO_DAMAGE") && deaths! > 0) ruleErrors.push("le run contient des morts");
    if (category === "NO_DAMAGE" && damage > 0) ruleErrors.push("le run contient des dégâts");
    if (ruleErrors.length) throw new ApiError(422, "CATEGORY_RULES_NOT_MET", "Le run ne respecte pas les règles de la catégorie.", ruleErrors);
    // Progression consistency with the level revision.
    if (totalCollectibles !== version.total_collectibles || collectibles > version.total_collectibles)
        throw new ApiError(422, "INCONSISTENT_PROGRESS", "Données de progression incohérentes avec le niveau.");

    // Idempotence: the same attempt can be resubmitted (lost response, offline queue).
    const existing = await ctx.env.DB.prepare("SELECT * FROM records WHERE player_id = ?1 AND attempt_id = ?2")
        .bind(auth.player.id, attemptId)
        .all<RecordRow>();
    for (const r of existing.results) {
        if (r.time_ms !== timeMs || r.deaths !== deaths || r.level_hash !== levelHash)
            throw Errors.conflict("ATTEMPT_CONFLICT", "Cette tentative a déjà été soumise avec d'autres données.");
        if (r.category === category) {
            return json(ctx, {
                duplicate: true,
                record: recordJson(r, { owner: true }),
                replay: { required: r.replay_id === null && r.verification_status === "PENDING", upload_path: `/api/v1/records/${r.id}/replay` },
                ranking: null,
            });
        }
    }

    const record: RecordRow = {
        id: crypto.randomUUID(),
        player_id: auth.player.id,
        level_id: level.id,
        level_version: version.version,
        level_hash: version.content_hash,
        mode,
        category: category!,
        time_ms: timeMs!,
        deaths: deaths!,
        damage_taken: damage,
        collectibles,
        total_collectibles: totalCollectibles,
        kills,
        score,
        game_version: gameVersion!,
        attempt_id: attemptId!,
        verification_status: "PENDING",
        rejection_reason: null,
        verification_notes: null,
        replay_id: existing.results.find((r) => r.replay_id)?.replay_id ?? null,
        created_at: nowIso(),
        verified_at: null,
    };
    try {
        await ctx.env.DB.prepare(
            `INSERT INTO records (id, player_id, level_id, level_version, level_hash, mode, category, time_ms, deaths, damage_taken,
               collectibles, total_collectibles, kills, score, game_version, attempt_id, verification_status, replay_id, created_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, 'PENDING', ?17, ?18)`,
        )
            .bind(record.id, record.player_id, record.level_id, record.level_version, record.level_hash, record.mode, record.category,
                record.time_ms, record.deaths, record.damage_taken, record.collectibles, record.total_collectibles, record.kills,
                record.score, record.game_version, record.attempt_id, record.replay_id, record.created_at)
            .run();
    } catch (e) {
        // Two identical submissions racing each other.
        if (String(e).includes("UNIQUE")) throw Errors.conflict("DUPLICATE_SUBMISSION", "Tentative déjà enregistrée.");
        throw e;
    }

    let decision: Decision;
    if (record.time_ms < version.min_time_ms) {
        decision = { status: "REJECTED", reason: "IMPOSSIBLE_TIME", note: null };
        await audit(ctx, auth.player.id, "record_rejected_impossible_time", { record_id: record.id, time_ms: record.time_ms });
    } else if (record.time_ms > version.time_limit_ms) {
        decision = { status: "REJECTED", reason: "TIME_OVER_LIMIT", note: null };
    } else {
        // A replay already analysed for this attempt (another category) counts here too.
        let replayState: ReplayState = "none";
        if (record.replay_id) {
            const meta = await ctx.env.DB.prepare("SELECT verification_status FROM replay_metadata WHERE id = ?1")
                .bind(record.replay_id)
                .first<{ verification_status: string }>();
            replayState = meta?.verification_status === "VERIFIED" ? "valid" : meta?.verification_status === "REJECTED" ? "invalid" : "none";
        }
        const board = await boardContext(ctx, record.level_hash, record.category, record.time_ms, auth.player.id);
        decision = decide(auth.player.trust_level, record.category as Category, replayState, record.time_ms, board);
    }
    const ranking = await applyDecision(ctx, record, decision);
    return json(
        ctx,
        {
            duplicate: false,
            record: recordJson(record, { owner: true }),
            replay: {
                required: record.replay_id === null && record.verification_status === "PENDING",
                accepted: hasReplay,
                upload_path: `/api/v1/records/${record.id}/replay`,
            },
            ranking,
        },
        { status: 201 },
    );
}

// ----------------------------------------------------------------------- get

async function loadRecord(ctx: Ctx, id: string): Promise<RecordRow & { owner_visibility: string; owner_status: string }> {
    if (!/^[0-9a-f-]{36}$/.test(id)) throw Errors.notFound("RECORD_NOT_FOUND", "Record introuvable.");
    const r = await ctx.env.DB.prepare(
        `SELECT r.*, p.profile_visibility AS owner_visibility, p.account_status AS owner_status
         FROM records r JOIN players p ON p.id = r.player_id WHERE r.id = ?1`,
    )
        .bind(id)
        .first<RecordRow & { owner_visibility: string; owner_status: string }>();
    if (!r) throw Errors.notFound("RECORD_NOT_FOUND", "Record introuvable.");
    return r;
}

/** Others only see VERIFIED records of public profiles; the owner sees everything. */
function canView(r: RecordRow & { owner_visibility: string; owner_status: string }, viewer: AuthInfo | null): boolean {
    if (viewer && viewer.player.id === r.player_id) return true;
    return r.verification_status === "VERIFIED" && r.owner_status === "active";
}

export async function getRecord(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const r = await loadRecord(ctx, ctx.params.recordId!);
    if (!canView(r, viewer)) throw Errors.notFound("RECORD_NOT_FOUND", "Record introuvable.");
    const owner = viewer?.player.id === r.player_id;
    const p = await ctx.env.DB.prepare("SELECT * FROM players WHERE id = ?1").bind(r.player_id).first<PlayerRow>();
    const rank = r.verification_status === "VERIFIED" ? await playerRank(ctx, r.level_hash, r.category, r.player_id) : null;
    const best = await ctx.env.DB.prepare("SELECT record_id FROM player_bests WHERE player_id = ?1 AND level_hash = ?2 AND category = ?3")
        .bind(r.player_id, r.level_hash, r.category)
        .first<{ record_id: string }>();
    return json(ctx, {
        record: recordJson(r, { owner }),
        player: playerSummary(p!),
        is_personal_best: best?.record_id === r.id,
        rank: best?.record_id === r.id ? rank : null,
    });
}

// ------------------------------------------------------------------- replays

export async function uploadReplay(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.replayUpload, "player:" + auth.player.id);
    const r = await loadRecord(ctx, ctx.params.recordId!);
    if (r.player_id !== auth.player.id) throw Errors.forbidden("NOT_OWNER", "Ce record ne vous appartient pas.");
    if (r.replay_id) throw Errors.conflict("REPLAY_EXISTS", "Un replay est déjà associé à cette tentative.");
    if (r.verification_status === "REJECTED") throw Errors.conflict("RECORD_REJECTED", "Ce record a été refusé.");
    const type = (ctx.request.headers.get("content-type") ?? "").toLowerCase();
    if (!type.startsWith("application/octet-stream"))
        throw new ApiError(415, "UNSUPPORTED_MEDIA_TYPE", "Le replay doit être envoyé en application/octet-stream.");
    const data = await readLimited(ctx.request, MAX_REPLAY_UPLOAD);
    const version = await ctx.env.DB.prepare("SELECT * FROM level_versions WHERE level_id = ?1 AND content_hash = ?2")
        .bind(r.level_id, r.level_hash)
        .first<LevelVersionRow>();
    if (!version) throw Errors.notFound("LEVEL_VERSION_NOT_FOUND", "Version de niveau inconnue.");

    let analysis: ReplayAnalysis;
    let frameCount = 0;
    let durationMs = 0;
    try {
        const replay = parseReplay(await inflateContainer(data));
        frameCount = replay.frames.length;
        durationMs = replay.durationMs;
        analysis = analyzeReplay(replay, r, version);
    } catch (e) {
        if (!(e instanceof ReplayError)) throw e;
        // An unreadable file is a client error: nothing is stored, the client may retry.
        throw new ApiError(422, "REPLAY_MALFORMED", `Replay illisible : ${e.message}.`);
    }

    const replayId = crypto.randomUUID();
    const key = `replays/${r.player_id}/${r.attempt_id}.cprz`;
    const digest = await sha256Hex(data);
    // R2 is private: the object is only ever served through GET /records/{id}/replay.
    await ctx.env.STORAGE.put(key, data, {
        httpMetadata: { contentType: "application/octet-stream" },
        customMetadata: { record_id: r.id, sha256: digest, format: "CPRZ/1" },
    });
    const status = analysis.ok ? "VERIFIED" : "REJECTED";
    try {
        await ctx.env.DB.batch([
            ctx.env.DB.prepare(
                `INSERT INTO replay_metadata (id, record_id, player_id, attempt_id, storage_key, file_size, sha256, format_version,
                   frame_count, duration_ms, verification_status, analysis, created_at)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, 1, ?8, ?9, ?10, ?11, ?12)`,
            ).bind(replayId, r.id, r.player_id, r.attempt_id, key, data.byteLength, digest, frameCount, durationMs, status,
                JSON.stringify(analysis), nowIso()),
            ctx.env.DB.prepare("UPDATE records SET replay_id = ?1 WHERE player_id = ?2 AND attempt_id = ?3").bind(replayId, r.player_id, r.attempt_id),
        ]);
    } catch (e) {
        if (String(e).includes("UNIQUE")) throw Errors.conflict("REPLAY_EXISTS", "Un replay est déjà associé à cette tentative.");
        throw e;
    }
    if (!analysis.ok) await audit(ctx, r.player_id, "replay_rejected", { record_id: r.id, reasons: analysis.reasons.slice(0, 5) });

    // Re-evaluate every record of this attempt that is still pending.
    const siblings = await ctx.env.DB.prepare(
        "SELECT * FROM records WHERE player_id = ?1 AND attempt_id = ?2 AND verification_status = 'PENDING'",
    )
        .bind(r.player_id, r.attempt_id)
        .all<RecordRow>();
    const fresh = await ctx.env.DB.prepare("SELECT trust_level FROM players WHERE id = ?1").bind(r.player_id).first<{ trust_level: number }>();
    const results = [];
    for (const s of siblings.results) {
        s.replay_id = replayId;
        const board = await boardContext(ctx, s.level_hash, s.category, s.time_ms, s.player_id);
        const d = decide(fresh?.trust_level ?? 0, s.category as Category, analysis.ok ? "valid" : "invalid", s.time_ms, board);
        if (!analysis.ok) d.note = analysis.reasons.join(" ; ").slice(0, 500);
        const ranking = await applyDecision(ctx, s, d);
        results.push({ record: recordJson(s, { owner: true }), ranking });
    }
    return json(ctx, { replay_id: replayId, verification_status: status, analysis, records: results }, { status: 201 });
}

export async function downloadReplay(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const r = await loadRecord(ctx, ctx.params.recordId!);
    if (!canView(r, viewer)) throw Errors.notFound("RECORD_NOT_FOUND", "Record introuvable.");
    const isOwner = viewer?.player.id === r.player_id;
    if (!isOwner && r.owner_visibility === "private") throw Errors.forbidden("PROFILE_PRIVATE", "Le joueur a masqué son profil.");
    if (!r.replay_id) throw Errors.notFound("REPLAY_NOT_FOUND", "Aucun replay pour ce record.");
    const meta = await ctx.env.DB.prepare("SELECT storage_key, sha256, verification_status FROM replay_metadata WHERE id = ?1")
        .bind(r.replay_id)
        .first<{ storage_key: string; sha256: string; verification_status: string }>();
    if (!meta || (!isOwner && meta.verification_status !== "VERIFIED")) throw Errors.notFound("REPLAY_NOT_FOUND", "Aucun replay pour ce record.");
    const obj = await ctx.env.STORAGE.get(meta.storage_key);
    if (!obj) throw Errors.notFound("REPLAY_NOT_FOUND", "Fichier de replay absent.");
    return new Response(obj.body, {
        headers: {
            "content-type": "application/octet-stream",
            "content-length": String(obj.size),
            "x-replay-sha256": meta.sha256,
            "cache-control": "private, max-age=3600",
            "x-content-type-options": "nosniff",
        },
    });
}
