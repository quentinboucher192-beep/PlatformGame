// The machine's runs (TAS) improved by the administrators (ADMINISTRATION › TAS, docs/TAS.md
// « Entraînement depuis ADMINISTRATION », docs/online/API.md « TAS »).
//
// The search runs ON THE ADMINISTRATOR'S COMPUTER (`cosmic_tas`, a child process of the game):
// nothing here computes anything. The server only
//   * lends a LEASE per level, category and version of the level, so that two administrators
//     never train the same pair: taken at the start, kept by a heartbeat about every minute,
//     given back at the stop; without a heartbeat for LEASE_TTL_MS it expires (a computer
//     turned off or crashed frees its pairs by itself);
//   * counts the training time of every session, measured here between two heartbeats (never
//     more than the lease's life at once), and the attempts the heartbeats report, bounded by
//     that time (MAX_EXPLORED_PER_SECOND);
//   * keeps every run published (its JSON and its replay in R2, its metadata in D1), after
//     checking everything that can be checked without playing it: the format, the sizes, the
//     level and its current fingerprint when the server knows the level, the category's rules,
//     the replay's header (the level, its fingerprint, the simulation, the time) — and it must be
//     faster than the run published for the same version. Every game plays a downloaded run
//     again before it ever shows it (game/tas/TasOnline.h).
//
// A VERSION of a level is its ranked fingerprint (level_hash) and the version of the simulation
// (sim_version). When a level changes (edited in ADMINISTRATION › Campagne, regenerated, an enemy
// revision, new rules), its runs and its training start from nothing for the new version: a run
// for an older version is refused, the old ones stay (never deleted) and the list gives the last
// one apart (« archive »: not comparable).
//
// Every route of /admin/tas exists only for an active administrator (requireAdminRole: the role
// of the authenticated session, never anything the game sends). The list and the files of the
// published runs are public (GET /tas): the game reads them now and then.
//
// The TRAINING CURVE (docs/TAS.md « Courbe d'entraînement », migration 0225_tas_curve.sql): the
// heartbeat and the release of a lease may carry the new best times of the training (`curve`) and
// a summary of its attempts (`attempts`: windows of counts). Each one is placed on the server's
// own measure of the training time and of the attempts, within what this very call counted (the
// game only says how long ago it happened: `age_ms`); a best time is kept only when it is
// strictly below every one of its version; at most MAX_CURVE_POINTS points and
// MAX_CURVE_WINDOWS windows per version. The runs published are the curve's other marks.

import { audit } from "./auth";
import { requireAdminRole } from "./adminRoles";
import { fromBase64, sha256Hex } from "./crypto";
import { ApiError, Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import { RULES, rateLimit } from "./ratelimit";
import { MAX_REPLAY_UPLOAD, ReplayError, inflateContainer, parseReplay } from "./replay";
import { CATEGORIES, HASH_RE, LEVEL_ID_RE, Validator, type Category } from "./validate";

/** A lease lives this long after its last heartbeat. */
export const LEASE_TTL_MS = 180_000;
/** The heartbeat the game sends (the lease survives two missed ones). */
export const HEARTBEAT_MS = 60_000;
/** Attempts a training may report per second of lease (far above what a computer simulates). */
export const MAX_EXPLORED_PER_SECOND = 50_000;
/** The largest files of a run (the game's own bounds: game/tas/TasOnline.cpp). */
export const MAX_RECORD_BYTES = 1024 * 1024;
export const MAX_TAS_REPLAY_BYTES = MAX_REPLAY_UPLOAD;
/** The longest run (30 min of the level's clock, as game/tas/TasVerify.h). */
export const MAX_TAS_TIME_MS = 30 * 60 * 1000;
const SIMULATION_EVENT = 20;
const LEASE_ID_RE = /^[0-9a-f-]{36}$/;
/** The best points of the curve kept per version (beyond, the last one gives way: the curve keeps its start and its best). */
export const MAX_CURVE_POINTS = 1000;
/** The windows of attempts kept per version (beyond, a window is added to the last one of its session). */
export const MAX_CURVE_WINDOWS = 1500;
/** What one heartbeat or release may carry. */
export const MAX_CURVE_POINTS_PER_CALL = 16;
export const MAX_CURVE_WINDOWS_PER_CALL = 4;
/** The sections a window counts (the game's TasAttemptWindow::kMaxSections). */
export const MAX_CURVE_SECTIONS = 32;
/** How long ago a point or a window may say it happened, how long a window may last. */
const MAX_AGE_MS = 86_400_000;
const MAX_WINDOW_MS = 600_000;

interface TrainingRow {
    level_id: string;
    category: string;
    level_hash: string;
    sim_version: number;
    total_training_ms: number;
    total_explored: number;
    sessions: number;
    lease_id: string | null;
    lease_holder: string | null;
    lease_holder_username: string | null;
    lease_started_at: string | null;
    lease_beat_ms: number | null;
    lease_expires_ms: number | null;
    lease_explored: number;
}

interface RunRow {
    level_id: string;
    category: string;
    revision: number;
    level_hash: string;
    level_version: number;
    sim_version: number;
    time_ms: number;
    explored: number;
    record_key: string;
    record_sha256: string;
    record_size: number;
    replay_key: string;
    replay_sha256: string;
    replay_size: number;
    training_ms_at_publish: number;
    explored_at_publish: number;
    published_by: string | null;
    published_by_username: string | null;
    published_at: string;
}

interface CurvePointRow {
    level_id: string;
    category: string;
    level_hash: string;
    sim_version: number;
    seq: number;
    kind: string;
    time_ms: number;
    training_ms: number;
    explored: number;
    session: number;
    found_by: string | null;
    found_by_username: string | null;
    found_at: string;
}

interface CurveWindowRow {
    level_id: string;
    category: string;
    level_hash: string;
    sim_version: number;
    seq: number;
    session: number;
    round: number;
    section: number;
    sections: number;
    start_ms: number;
    end_ms: number;
    attempts: number;
    finished: number;
    best_ms: number;
    median_ms: number;
    worst_ms: number;
    route_median: number;
    route_max: number;
    stopped: string;
    broken: string;
    found_by: string | null;
    found_by_username: string | null;
    at: string;
}

function pairOr404(ctx: Ctx): { levelId: string; category: Category } {
    const levelId = ctx.params.levelId ?? "";
    const category = ctx.params.category ?? "";
    if (!LEVEL_ID_RE.test(levelId) || !(CATEGORIES as readonly string[]).includes(category)) throw Errors.notFound("TAS_NOT_FOUND", "Niveau ou catégorie inconnus.");
    return { levelId, category: category as Category };
}

function leaseActive(t: TrainingRow | null | undefined, now: number): boolean {
    return !!t && !!t.lease_id && (t.lease_expires_ms ?? 0) > now;
}

function leaseJson(t: TrainingRow, now: number, me: string) {
    if (!leaseActive(t, now)) return null;
    return {
        holder: { id: t.lease_holder, username: t.lease_holder_username },
        since: t.lease_started_at,
        expires_at: new Date(t.lease_expires_ms!).toISOString(),
        mine: t.lease_holder === me,
    };
}

function trainingJson(t: TrainingRow, now: number, me: string) {
    return {
        level_hash: t.level_hash,
        sim_version: t.sim_version,
        total_ms: t.total_training_ms,
        total_explored: t.total_explored,
        sessions: t.sessions,
        lease: leaseJson(t, now, me),
    };
}

function filePaths(r: RunRow) {
    const base = `/api/v1/tas/${r.level_id}/${r.category}/${r.revision}`;
    return { record_path: `${base}/record`, replay_path: `${base}/replay` };
}

function runJson(r: RunRow | null) {
    if (!r) return null;
    return {
        revision: r.revision,
        time_ms: r.time_ms,
        level_hash: r.level_hash,
        level_version: r.level_version,
        sim_version: r.sim_version,
        explored: r.explored,
        published_at: r.published_at,
        published_by: r.published_by || r.published_by_username ? { id: r.published_by, username: r.published_by_username } : null,
        training_ms_at_publish: r.training_ms_at_publish,
        explored_at_publish: r.explored_at_publish,
        ...filePaths(r),
    };
}

const sameVersion = (r: { level_hash: string; sim_version: number }, hash: string, sim: number) => r.level_hash === hash && r.sim_version === sim;

const byJson = (id: string | null, username: string | null) => (id || username ? { id, username } : null);

/** A best point of the curve. */
function curvePointJson(p: CurvePointRow) {
    return { kind: p.kind, time_ms: p.time_ms, training_ms: p.training_ms, explored: p.explored, at: p.found_at, session: p.session, by: byJson(p.found_by, p.found_by_username) };
}

/** A run published, as a mark of the curve of its version. */
function publishPointJson(r: RunRow) {
    return {
        kind: "publish",
        time_ms: r.time_ms,
        training_ms: r.training_ms_at_publish,
        explored: r.explored_at_publish,
        at: r.published_at,
        session: 0,
        revision: r.revision,
        by: byJson(r.published_by, r.published_by_username),
    };
}

const counts = (text: string): number[] => {
    try {
        const a = JSON.parse(text);
        return Array.isArray(a) ? a.filter((x) => Number.isSafeInteger(x)) : [];
    } catch {
        return [];
    }
};

function curveWindowJson(w: CurveWindowRow) {
    return {
        session: w.session,
        round: w.round,
        section: w.section,
        sections: w.sections,
        start_ms: w.start_ms,
        end_ms: w.end_ms,
        attempts: w.attempts,
        finished: w.finished,
        best_ms: w.best_ms,
        median_ms: w.median_ms,
        worst_ms: w.worst_ms,
        route_median: w.route_median,
        route_max: w.route_max,
        stopped: counts(w.stopped),
        broken: counts(w.broken),
        at: w.at,
        by: byJson(w.found_by, w.found_by_username),
    };
}

/**
 * The curves of a pair, one per version that has anything: its best points and its runs
 * published (by training time), and its windows of attempts when asked (`windows`).
 */
function curvesJson(points: CurvePointRow[], runs: RunRow[], windows: CurveWindowRow[] | null) {
    const versions = new Map<string, { level_hash: string; sim_version: number; points: ReturnType<typeof curvePointJson | typeof publishPointJson>[]; windows?: ReturnType<typeof curveWindowJson>[] }>();
    const at = (hash: string, sim: number) => {
        const k = `${hash}/${sim}`;
        let v = versions.get(k);
        if (!v) versions.set(k, (v = { level_hash: hash, sim_version: sim, points: [], ...(windows ? { windows: [] } : {}) }));
        return v;
    };
    for (const p of [...points].sort((a, b) => a.seq - b.seq)) at(p.level_hash, p.sim_version).points.push(curvePointJson(p));
    for (const r of [...runs].sort((a, b) => a.revision - b.revision)) at(r.level_hash, r.sim_version).points.push(publishPointJson(r));
    if (windows) for (const w of [...windows].sort((a, b) => a.seq - b.seq)) at(w.level_hash, w.sim_version).windows!.push(curveWindowJson(w));
    for (const v of versions.values()) v.points.sort((a, b) => a.training_ms - b.training_ms);
    return [...versions.values()];
}

/**
 * The runs of a pair, newest first, split as the list gives them: `current` (the latest run of
 * the current version: the fingerprint the server knows for the level, else the latest run's,
 * and the newest simulation among its runs) and `archive` (the latest run of any other version).
 */
function splitRuns(runs: RunRow[], currentHash: string | null): { current: RunRow | null; archive: RunRow | null; latestPerVersion: RunRow[] } {
    const sorted = [...runs].sort((a, b) => b.revision - a.revision);
    const latestPerVersion: RunRow[] = [];
    for (const r of sorted) if (!latestPerVersion.some((x) => sameVersion(x, r.level_hash, r.sim_version))) latestPerVersion.push(r);
    const hash = currentHash ?? sorted[0]?.level_hash ?? null;
    let current: RunRow | null = null;
    for (const r of latestPerVersion) if (r.level_hash === hash && (!current || r.sim_version > current.sim_version)) current = r;
    const archive = latestPerVersion.find((r) => r !== current) ?? null;
    return { current, archive, latestPerVersion };
}

async function levelOf(ctx: Ctx, levelId: string) {
    return ctx.env.DB.prepare(
        `SELECT l.current_version, l.content_hash, v.total_collectibles FROM levels l
         LEFT JOIN level_versions v ON v.level_id = l.id AND v.version = l.current_version WHERE l.id = ?1`,
    )
        .bind(levelId)
        .first<{ current_version: number; content_hash: string; total_collectibles: number | null }>();
}

function lockedError(t: TrainingRow): ApiError {
    return Errors.conflict("TAS_LOCKED", `En entraînement par ${t.lease_holder_username ?? "un autre administrateur"}.`, {
        holder: { id: t.lease_holder, username: t.lease_holder_username },
        since: t.lease_started_at,
        expires_at: new Date(t.lease_expires_ms ?? 0).toISOString(),
    });
}

/** A version of a level the server knows to be outdated: training or publishing it is refused. */
function outdatedError(level: { current_version: number; content_hash: string }): ApiError {
    return Errors.conflict(
        "LEVEL_VERSION_OUTDATED",
        `Ce niveau a changé : sa version en ligne est la révision ${level.current_version}. Mettez le jeu à jour, puis recalculez la course pour cette version.`,
        { current_version: level.current_version, level_hash: level.content_hash },
    );
}

// ------------------------------------------------------------------ administration

/**
 * GET /admin/tas: every pair trained or published: its current version's run (`published`), the
 * last run of an older version apart (`archive`), and the training of every version (totals, the
 * lease in force).
 */
export async function adminList(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    const [trainings, runs, levels, points] = await Promise.all([
        ctx.env.DB.prepare("SELECT * FROM tas_training").all<TrainingRow>(),
        ctx.env.DB.prepare("SELECT * FROM tas_runs").all<RunRow>(),
        ctx.env.DB.prepare("SELECT id, content_hash FROM levels").all<{ id: string; content_hash: string }>(),
        ctx.env.DB.prepare("SELECT * FROM tas_curve_points").all<CurvePointRow>(),
    ]);
    const now = Date.now();
    const hashOf = new Map(levels.results.map((l) => [l.id, l.content_hash]));
    const pairs = new Map<string, { levelId: string; category: string; t: TrainingRow[]; r: RunRow[]; c: CurvePointRow[] }>();
    const at = (levelId: string, category: string) => {
        const k = `${levelId}/${category}`;
        let e = pairs.get(k);
        if (!e) pairs.set(k, (e = { levelId, category, t: [], r: [], c: [] }));
        return e;
    };
    for (const t of trainings.results) at(t.level_id, t.category).t.push(t);
    for (const r of runs.results) at(r.level_id, r.category).r.push(r);
    for (const c of points.results) at(c.level_id, c.category).c.push(c);
    return json(ctx, {
        now: new Date(now).toISOString(),
        lease_ttl_s: LEASE_TTL_MS / 1000,
        heartbeat_s: HEARTBEAT_MS / 1000,
        pairs: [...pairs.entries()]
            .sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0))
            .map(([, p]) => {
                const currentHash = hashOf.get(p.levelId) ?? null;
                const { current, archive } = splitRuns(p.r, currentHash);
                return {
                    level_id: p.levelId,
                    category: p.category,
                    current_hash: currentHash,
                    published: runJson(current),
                    archive: runJson(archive),
                    trainings: p.t.map((t) => trainingJson(t, now, auth.player.id)),
                    // The training curve of every version: its best points and runs published (the windows: GET …/curve).
                    curves: curvesJson(p.c, p.r, null),
                };
            }),
    });
}

/**
 * GET /admin/tas/{levelId}/{category}/curve: the training curve of the pair, every version: its
 * best points, its runs published and its windows of attempts.
 */
export async function adminCurve(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const { levelId, category } = pairOr404(ctx);
    const [points, runs, windows] = await Promise.all([
        ctx.env.DB.prepare("SELECT * FROM tas_curve_points WHERE level_id = ?1 AND category = ?2").bind(levelId, category).all<CurvePointRow>(),
        ctx.env.DB.prepare("SELECT * FROM tas_runs WHERE level_id = ?1 AND category = ?2").bind(levelId, category).all<RunRow>(),
        ctx.env.DB.prepare("SELECT * FROM tas_curve_windows WHERE level_id = ?1 AND category = ?2").bind(levelId, category).all<CurveWindowRow>(),
    ]);
    return json(ctx, { level_id: levelId, category, versions: curvesJson(points.results, runs.results, windows.results) });
}

/**
 * POST /admin/tas/{levelId}/{category}/lease {level_hash, sim_version}: the lease of the pair at
 * this version of the level, for this administrator, when it is free (or expired, or already
 * his: another computer of his, a restart). Otherwise 409 TAS_LOCKED with the holder; a version
 * the server knows to be outdated: 409 LEVEL_VERSION_OUTDATED.
 */
export async function acquire(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const { levelId, category } = pairOr404(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["level_hash", "sim_version"]);
    const hash = v.str("level_hash", { min: 64, max: 64, pattern: HASH_RE });
    const sim = v.int("sim_version", { min: 1, max: 10_000 });
    v.done();
    const level = await levelOf(ctx, levelId);
    if (level && level.content_hash !== hash) throw outdatedError(level);
    const newer = await ctx.env.DB.prepare("SELECT MAX(sim_version) AS sim FROM tas_runs WHERE level_id = ?1 AND category = ?2 AND level_hash = ?3")
        .bind(levelId, category, hash)
        .first<{ sim: number | null }>();
    if (newer?.sim && newer.sim > sim!)
        throw Errors.conflict("SIM_OUTDATED", "Une course d'une version plus récente de la simulation est publiée : mettez le jeu à jour.", { sim_version: newer.sim });
    const now = Date.now();
    const leaseId = crypto.randomUUID();
    const [, upd] = await ctx.env.DB.batch([
        ctx.env.DB.prepare(
            "INSERT INTO tas_training (level_id, category, level_hash, sim_version) VALUES (?1, ?2, ?3, ?4) ON CONFLICT (level_id, category, level_hash, sim_version) DO NOTHING",
        ).bind(levelId, category, hash, sim),
        // One statement: taken only if free, expired or his (two administrators at once: one wins).
        ctx.env.DB.prepare(
            `UPDATE tas_training SET
               total_training_ms = total_training_ms + CASE WHEN lease_id IS NOT NULL AND lease_expires_ms > ?5 THEN MAX(0, MIN(?5 - lease_beat_ms, ?9)) ELSE 0 END,
               lease_id = ?6, lease_holder = ?7, lease_holder_username = ?8, lease_started_at = ?10,
               lease_beat_ms = ?5, lease_expires_ms = ?5 + ?9, lease_explored = 0, sessions = sessions + 1
             WHERE level_id = ?1 AND category = ?2 AND level_hash = ?3 AND sim_version = ?4
               AND (lease_id IS NULL OR lease_expires_ms <= ?5 OR lease_holder = ?7)`,
        ).bind(levelId, category, hash, sim, now, leaseId, auth.player.id, auth.player.username, LEASE_TTL_MS, new Date(now).toISOString()),
    ]);
    const t = await ctx.env.DB.prepare("SELECT * FROM tas_training WHERE level_id = ?1 AND category = ?2 AND level_hash = ?3 AND sim_version = ?4")
        .bind(levelId, category, hash, sim)
        .first<TrainingRow>();
    if (!upd!.meta.changes || !t || t.lease_id !== leaseId) throw lockedError(t!);
    return json(ctx, {
        lease_id: leaseId,
        expires_at: new Date(t.lease_expires_ms!).toISOString(),
        ttl_s: LEASE_TTL_MS / 1000,
        heartbeat_s: HEARTBEAT_MS / 1000,
        training: trainingJson(t, now, auth.player.id),
    });
}

/** The time since the last heartbeat and the attempts reported, added to the version's totals. */
function accountStatement(ctx: Ctx, levelId: string, category: string, leaseId: string, holder: string, now: number, explored: number, release: boolean) {
    // elapsed: the server's own clock, at most the lease's life; the attempts: at most what that
    // time allows (a lying client cannot inflate them beyond it).
    const elapsed = "MAX(0, MIN(?5 - lease_beat_ms, ?7))";
    const allowed = `(${elapsed} * ?8 / 1000)`;
    const counted = `MAX(0, MIN(?6 - lease_explored, ${allowed}))`;
    const after = release
        ? "lease_id = NULL, lease_holder = NULL, lease_holder_username = NULL, lease_started_at = NULL, lease_beat_ms = NULL, lease_expires_ms = NULL, lease_explored = 0"
        : `lease_beat_ms = ?5, lease_expires_ms = ?5 + ?7, lease_explored = lease_explored + ${counted}`;
    // SQLite: every expression of SET reads the row as it was before the update.
    return ctx.env.DB.prepare(
        `UPDATE tas_training SET
           total_training_ms = total_training_ms + ${elapsed},
           total_explored = total_explored + ${counted},
           ${after}
         WHERE level_id = ?1 AND category = ?2 AND lease_id = ?3 AND lease_holder = ?4 AND lease_expires_ms > ?5
         RETURNING level_hash, sim_version, total_training_ms, total_explored, sessions, lease_expires_ms`,
    ).bind(levelId, category, leaseId, holder, now, explored, LEASE_TTL_MS, MAX_EXPLORED_PER_SECOND);
}

interface CurvePointIn {
    kind: "start" | "improvement";
    time_ms: number;
    age_ms: number;
    explored: number;
}

interface CurveWindowIn {
    age_ms: number;
    duration_ms: number;
    round: number;
    section: number;
    sections: number;
    attempts: number;
    finished: number;
    best_ms: number;
    median_ms: number;
    worst_ms: number;
    route_median: number;
    route_max: number;
    stopped: number[];
    broken: number[];
}

/** `curve` of a heartbeat or a release: the new best times, each field checked (problems added to `v`). */
function curvePointsIn(v: Validator, raw: unknown): CurvePointIn[] {
    if (raw === undefined || raw === null) return [];
    if (!Array.isArray(raw) || raw.length > MAX_CURVE_POINTS_PER_CALL) {
        v.fail("curve", `liste de ${MAX_CURVE_POINTS_PER_CALL} points au plus attendue`);
        return [];
    }
    const out: CurvePointIn[] = [];
    raw.forEach((p, i) => {
        const f = `curve[${i}]`;
        if (!isObject(p) || Object.keys(p).some((k) => !["kind", "time_ms", "age_ms", "explored"].includes(k))) return v.fail(f, "point invalide");
        if (p.kind !== "start" && p.kind !== "improvement") return v.fail(`${f}.kind`, "start ou improvement attendu");
        if (!isInt(p.time_ms, 1, MAX_TAS_TIME_MS)) return v.fail(`${f}.time_ms`, `entier entre 1 et ${MAX_TAS_TIME_MS} attendu`);
        if (!isInt(p.age_ms, 0, MAX_AGE_MS)) return v.fail(`${f}.age_ms`, `entier entre 0 et ${MAX_AGE_MS} attendu`);
        if (!isInt(p.explored, 0, Number.MAX_SAFE_INTEGER)) return v.fail(`${f}.explored`, "entier positif attendu");
        out.push(p as unknown as CurvePointIn);
    });
    return out;
}

const WINDOW_KEYS = ["age_ms", "duration_ms", "round", "section", "sections", "attempts", "finished", "best_ms", "median_ms", "worst_ms", "route_median", "route_max", "stopped", "broken"];

/** `attempts` of a heartbeat or a release: windows of counts, each checked and consistent (problems added to `v`). */
function curveWindowsIn(v: Validator, raw: unknown): CurveWindowIn[] {
    if (raw === undefined || raw === null) return [];
    if (!Array.isArray(raw) || raw.length > MAX_CURVE_WINDOWS_PER_CALL) {
        v.fail("attempts", `liste de ${MAX_CURVE_WINDOWS_PER_CALL} fenêtres au plus attendue`);
        return [];
    }
    const out: CurveWindowIn[] = [];
    raw.forEach((w, i) => {
        const f = `attempts[${i}]`;
        if (!isObject(w) || Object.keys(w).some((k) => !WINDOW_KEYS.includes(k))) return v.fail(f, "fenêtre invalide");
        const bad = (field: string, why: string) => v.fail(`${f}.${field}`, why);
        if (!isInt(w.age_ms, 0, MAX_AGE_MS)) return bad("age_ms", `entier entre 0 et ${MAX_AGE_MS} attendu`);
        if (!isInt(w.duration_ms, 1, MAX_WINDOW_MS)) return bad("duration_ms", `entier entre 1 et ${MAX_WINDOW_MS} attendu`);
        if (!isInt(w.round, 1, 1_000_000)) return bad("round", "entier positif attendu");
        if (!isInt(w.sections, 0, MAX_CURVE_SECTIONS)) return bad("sections", `entier entre 0 et ${MAX_CURVE_SECTIONS} attendu`);
        if (!isInt(w.section, 0, w.sections)) return bad("section", "entre 0 et le nombre de sections attendu");
        // The attempts, bounded by the window's time (as those of the heartbeats).
        const most = Math.ceil((w.duration_ms * MAX_EXPLORED_PER_SECOND) / 1000);
        if (!isInt(w.attempts, 1, most)) return bad("attempts", `entier entre 1 et ${most} attendu`);
        if (!isInt(w.finished, 0, w.attempts)) return bad("finished", "entre 0 et le nombre d'essais attendu");
        if (w.finished === 0) {
            if (w.best_ms !== 0 || w.median_ms !== 0 || w.worst_ms !== 0) return bad("best_ms", "0 sans essai fini");
        } else if (!isInt(w.best_ms, 1, MAX_TAS_TIME_MS) || !isInt(w.median_ms, w.best_ms, MAX_TAS_TIME_MS) || !isInt(w.worst_ms, w.median_ms, MAX_TAS_TIME_MS)) {
            return bad("best_ms", "meilleur ≤ médian ≤ pire attendus");
        }
        if (!isInt(w.route_median, 0, 1000) || !isInt(w.route_max, w.route_median, 1000)) return bad("route_median", "‰ du parcours attendus (médian ≤ max)");
        const list = (x: unknown) => Array.isArray(x) && x.length === w.sections && x.every((n) => isInt(n, 0, w.attempts as number));
        if (!list(w.stopped) || !list(w.broken)) return bad("stopped", "un compte par section attendu");
        const stopped = w.stopped as number[], broken = w.broken as number[];
        if (broken.some((n, k) => n > stopped[k]!)) return bad("broken", "jamais plus que les arrêts de la section");
        if (w.sections > 0 && stopped.reduce((a, b) => a + b, 0) !== w.attempts - w.finished) return bad("stopped", "la somme doit valoir les essais non finis");
        out.push(w as unknown as CurveWindowIn);
    });
    return out;
}

/** Two windows of counts as one (counts added, best and worst kept, medians weighted by their counts). */
function mergedWindow(a: CurveWindowRow, b: CurveWindowIn & { start_ms: number; end_ms: number }): CurveWindowRow {
    const finished = a.finished + b.finished;
    const ua = a.attempts - a.finished,
        ub = b.attempts - b.finished;
    const lowest = (x: number, y: number) => (x <= 0 ? y : y <= 0 ? x : Math.min(x, y));
    const sa = counts(a.stopped),
        ba = counts(a.broken);
    const n = Math.max(a.sections, b.sections);
    const add = (x: number[], y: number[]) => Array.from({ length: n }, (_, k) => (x[k] ?? 0) + (y[k] ?? 0));
    return {
        ...a,
        round: b.round,
        section: b.section,
        sections: n,
        start_ms: Math.min(a.start_ms, b.start_ms),
        end_ms: Math.max(a.end_ms, b.end_ms),
        attempts: a.attempts + b.attempts,
        finished,
        best_ms: lowest(a.best_ms, b.best_ms),
        median_ms: finished > 0 ? Math.round((a.median_ms * a.finished + b.median_ms * b.finished) / finished) : 0,
        worst_ms: Math.max(a.worst_ms, b.worst_ms),
        route_median: ua + ub > 0 ? Math.round((a.route_median * ua + b.route_median * ub) / (ua + ub)) : 0,
        route_max: Math.max(a.route_max, b.route_max),
        stopped: JSON.stringify(add(sa, b.stopped)),
        broken: JSON.stringify(add(ba, b.broken)),
    };
}

/**
 * The curve's points and windows of a heartbeat or a release, placed within what this call
 * counted: the training time from `before` to `after` (the server's measure), the attempts too.
 */
async function storeCurve(
    ctx: Ctx,
    k: { levelId: string; category: string; hash: string; sim: number; session: number; by: { id: string; username: string }; now: number; sessionExplored: number },
    before: { total_training_ms: number; total_explored: number },
    after: { total_training_ms: number; total_explored: number },
    points: CurvePointIn[],
    windows: CurveWindowIn[],
) {
    const hiT = after.total_training_ms,
        loT = Math.min(before.total_training_ms, hiT);
    const hiE = after.total_explored,
        loE = Math.min(before.total_explored, hiE);
    const clamp = (x: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, x));
    const xOf = (ageMs: number) => clamp(hiT - ageMs, loT, hiT);
    const atOf = (x: number) => new Date(k.now - (hiT - x)).toISOString();
    const key = [k.levelId, k.category, k.hash, k.sim] as const;
    const where = "level_id = ?1 AND category = ?2 AND level_hash = ?3 AND sim_version = ?4";
    const [lastPoint, lastWindow] = await Promise.all([
        ctx.env.DB.prepare(`SELECT seq, time_ms, training_ms, (SELECT COUNT(*) FROM tas_curve_points WHERE ${where}) AS n FROM tas_curve_points WHERE ${where} ORDER BY seq DESC LIMIT 1`)
            .bind(...key)
            .first<{ seq: number; time_ms: number; training_ms: number; n: number }>(),
        ctx.env.DB.prepare(`SELECT *, (SELECT COUNT(*) FROM tas_curve_windows WHERE ${where}) AS n FROM tas_curve_windows WHERE ${where} ORDER BY seq DESC LIMIT 1`)
            .bind(...key)
            .first<CurveWindowRow & { n: number }>(),
    ]);
    const stmts: D1PreparedStatement[] = [];
    const added: ReturnType<typeof curvePointJson>[] = [];
    let refused = 0;
    // The best times, oldest first: each strictly below every one before (stored or just added).
    let prevTime = lastPoint?.time_ms ?? Number.POSITIVE_INFINITY;
    let prevX = lastPoint?.training_ms ?? 0;
    let seq = lastPoint?.seq ?? 0;
    let n = lastPoint?.n ?? 0;
    for (const p of [...points].sort((a, b) => b.age_ms - a.age_ms)) {
        if (p.time_ms >= prevTime) {
            ++refused;
            continue;
        }
        const x = Math.max(prevX, xOf(p.age_ms));
        const explored = clamp(hiE - Math.max(0, k.sessionExplored - p.explored), loE, hiE);
        const row: CurvePointRow = {
            level_id: k.levelId,
            category: k.category,
            level_hash: k.hash,
            sim_version: k.sim,
            seq: 0,
            kind: p.kind,
            time_ms: p.time_ms,
            training_ms: x,
            explored,
            session: k.session,
            found_by: k.by.id,
            found_by_username: k.by.username,
            found_at: atOf(x),
        };
        if (n >= MAX_CURVE_POINTS && seq > 0) {
            // The cap: the last point gives way (the curve keeps its start and its best).
            row.seq = seq;
            stmts.push(
                ctx.env.DB.prepare(
                    `UPDATE tas_curve_points SET kind = ?6, time_ms = ?7, training_ms = ?8, explored = ?9, session = ?10, found_by = ?11, found_by_username = ?12, found_at = ?13
                     WHERE ${where} AND seq = ?5 AND time_ms > ?7`,
                ).bind(...key, seq, row.kind, row.time_ms, row.training_ms, row.explored, row.session, row.found_by, row.found_by_username, row.found_at),
            );
        } else {
            row.seq = ++seq;
            ++n;
            // Strictly below every stored point of the version, checked again in the statement.
            stmts.push(
                ctx.env.DB.prepare(
                    `INSERT INTO tas_curve_points (level_id, category, level_hash, sim_version, seq, kind, time_ms, training_ms, explored, session, found_by, found_by_username, found_at)
                     SELECT ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13
                     WHERE NOT EXISTS (SELECT 1 FROM tas_curve_points WHERE ${where} AND time_ms <= ?7)
                     ON CONFLICT DO NOTHING`,
                ).bind(...key, row.seq, row.kind, row.time_ms, row.training_ms, row.explored, row.session, row.found_by, row.found_by_username, row.found_at),
            );
        }
        added.push(curvePointJson(row));
        prevTime = p.time_ms;
        prevX = x;
    }
    // The windows of attempts, oldest first.
    let wSeq = lastWindow?.seq ?? 0;
    let wN = lastWindow?.n ?? 0;
    let last: CurveWindowRow | null = lastWindow ?? null;
    let stored = 0;
    const columns =
        "level_id, category, level_hash, sim_version, seq, session, round, section, sections, start_ms, end_ms, attempts, finished, best_ms, median_ms, worst_ms, route_median, route_max, stopped, broken, found_by, found_by_username, at";
    const bindWindow = (s: D1PreparedStatement, w: CurveWindowRow) =>
        s.bind(w.level_id, w.category, w.level_hash, w.sim_version, w.seq, w.session, w.round, w.section, w.sections, w.start_ms, w.end_ms, w.attempts, w.finished,
            w.best_ms, w.median_ms, w.worst_ms, w.route_median, w.route_max, w.stopped, w.broken, w.found_by, w.found_by_username, w.at);
    for (const w of [...windows].sort((a, b) => b.age_ms - a.age_ms)) {
        const end = xOf(w.age_ms);
        const placed = { ...w, start_ms: Math.max(0, end - w.duration_ms), end_ms: end };
        if (wN >= MAX_CURVE_WINDOWS) {
            // The cap: added to the last window of its session, else left out.
            if (!last || last.session !== k.session) {
                ++refused;
                continue;
            }
            last = mergedWindow(last, placed);
            stmts.push(
                bindWindow(
                    ctx.env.DB.prepare(
                        `UPDATE tas_curve_windows SET level_id = ?1, category = ?2, level_hash = ?3, sim_version = ?4, session = ?6, round = ?7, section = ?8, sections = ?9,
                           start_ms = ?10, end_ms = ?11, attempts = ?12, finished = ?13, best_ms = ?14, median_ms = ?15, worst_ms = ?16, route_median = ?17, route_max = ?18,
                           stopped = ?19, broken = ?20, found_by = ?21, found_by_username = ?22, at = ?23
                         WHERE level_id = ?1 AND category = ?2 AND level_hash = ?3 AND sim_version = ?4 AND seq = ?5`,
                    ),
                    last,
                ),
            );
            ++stored;
            continue;
        }
        const row: CurveWindowRow = {
            level_id: k.levelId,
            category: k.category,
            level_hash: k.hash,
            sim_version: k.sim,
            seq: ++wSeq,
            session: k.session,
            round: w.round,
            section: w.section,
            sections: w.sections,
            start_ms: placed.start_ms,
            end_ms: placed.end_ms,
            attempts: w.attempts,
            finished: w.finished,
            best_ms: w.best_ms,
            median_ms: w.median_ms,
            worst_ms: w.worst_ms,
            route_median: w.route_median,
            route_max: w.route_max,
            stopped: JSON.stringify(w.stopped),
            broken: JSON.stringify(w.broken),
            found_by: k.by.id,
            found_by_username: k.by.username,
            at: atOf(end),
        };
        stmts.push(bindWindow(ctx.env.DB.prepare(`INSERT INTO tas_curve_windows (${columns}) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23) ON CONFLICT DO NOTHING`), row));
        last = row;
        ++wN;
        ++stored;
    }
    if (stmts.length) await ctx.env.DB.batch(stmts);
    return { points: added, refused, windows: stored };
}

async function leaseCall(ctx: Ctx, release: boolean): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    const { levelId, category } = pairOr404(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["lease_id", "explored", "curve", "attempts"]);
    const leaseId = v.str("lease_id", { min: 36, max: 36, pattern: LEASE_ID_RE });
    const explored = v.int("explored", { min: 0, max: Number.MAX_SAFE_INTEGER });
    const points = curvePointsIn(v, body.curve);
    const windows = curveWindowsIn(v, body.attempts);
    v.done();
    const now = Date.now();
    // What the lease had counted before this call (the curve is placed within this call's time), then the call: one transaction.
    const [beforeRes, afterRes] = await ctx.env.DB.batch([
        ctx.env.DB.prepare(
            "SELECT total_training_ms, total_explored FROM tas_training WHERE level_id = ?1 AND category = ?2 AND lease_id = ?3 AND lease_holder = ?4 AND lease_expires_ms > ?5",
        ).bind(levelId, category, leaseId, auth.player.id, now),
        accountStatement(ctx, levelId, category, leaseId!, auth.player.id, now, explored!, release),
    ]);
    const before = (beforeRes!.results as { total_training_ms: number; total_explored: number }[])[0];
    const row = (
        afterRes!.results as {
            level_hash: string;
            sim_version: number;
            total_training_ms: number;
            total_explored: number;
            sessions: number;
            lease_expires_ms: number | null;
        }[]
    )[0];
    if (!row || !before) throw Errors.conflict("LEASE_LOST", "Ce verrou n'est plus le vôtre (expiré ou repris).");
    const training = { level_hash: row.level_hash, sim_version: row.sim_version, total_ms: row.total_training_ms, total_explored: row.total_explored, sessions: row.sessions };
    const curve =
        points.length || windows.length
            ? await storeCurve(
                  ctx,
                  { levelId, category, hash: row.level_hash, sim: row.sim_version, session: row.sessions, by: { id: auth.player.id, username: auth.player.username }, now, sessionExplored: explored! },
                  before,
                  row,
                  points,
                  windows,
              )
            : undefined;
    if (release) return json(ctx, { released: true, training, ...(curve ? { curve } : {}) });
    return json(ctx, { expires_at: new Date(row.lease_expires_ms!).toISOString(), training, ...(curve ? { curve } : {}) });
}

/** POST /admin/tas/{levelId}/{category}/lease/heartbeat {lease_id, explored}: the lease kept three more minutes. */
export async function heartbeat(ctx: Ctx): Promise<Response> {
    return leaseCall(ctx, false);
}

/** POST /admin/tas/{levelId}/{category}/lease/release {lease_id, explored}: the pair free at once. */
export async function release(ctx: Ctx): Promise<Response> {
    return leaseCall(ctx, true);
}

// ------------------------------------------------------------------ publication

const isObject = (x: unknown): x is Record<string, unknown> => typeof x === "object" && x !== null && !Array.isArray(x);
const isInt = (x: unknown, min: number, max: number): x is number => typeof x === "number" && Number.isSafeInteger(x) && x >= min && x <= max;

/** The record of a run (format cosmic.tas, game/tas/TasFile.h), checked field by field. Throws a 422 TAS_INVALID listing the problems. */
export function checkRecord(text: string, levelId: string, category: Category): Record<string, unknown> & {
    levelHash: string;
    levelVersion: number;
    simVersion: number;
    timeMs: number;
    explored: number;
} {
    const problems: string[] = [];
    const fail = (): never => {
        throw new ApiError(422, "TAS_INVALID", "La fiche de la course n'est pas valide.", problems);
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
    if (d.format !== "cosmic.tas") problems.push("format inconnu (attendu cosmic.tas)");
    if (d.level !== levelId) problems.push(`niveau attendu : ${levelId}`);
    if (d.category !== category) problems.push(`catégorie attendue : ${category}`);
    if (typeof d.levelHash !== "string" || !HASH_RE.test(d.levelHash)) problems.push("levelHash invalide");
    if (!isInt(d.levelVersion, 1, 1_000_000)) problems.push("levelVersion invalide");
    if (!isInt(d.simVersion, 1, 10_000)) problems.push("simVersion invalide");
    if (!isInt(d.timeMs, 1, MAX_TAS_TIME_MS)) problems.push("timeMs invalide");
    if (!isInt(d.explored, 0, Number.MAX_SAFE_INTEGER)) problems.push("explored invalide");
    if (d.replay !== `${category}.cprz`) problems.push(`replay attendu : ${category}.cprz`);
    for (const k of ["deaths", "damageTaken", "collectibles", "totalCollectibles"]) if (!isInt(d[k], 0, 100_000)) problems.push(`${k} invalide`);
    if (d.computedAt !== undefined && (typeof d.computedAt !== "string" || d.computedAt.length > 32)) problems.push("computedAt invalide");
    if (d.tool !== undefined && (typeof d.tool !== "string" || d.tool.length > 200)) problems.push("tool invalide");
    if (d.routeIntervalMs !== undefined && !isInt(d.routeIntervalMs, 1, 60_000)) problems.push("routeIntervalMs invalide");
    const splits = d.splits ?? [];
    if (!Array.isArray(splits) || splits.length > 512) problems.push("splits : liste de 512 points de passage au plus");
    else
        splits.forEach((s, i) => {
            if (!isObject(s) || typeof s.checkpoint !== "string" || s.checkpoint.length > 64 || !isInt(s.timeMs, 0, MAX_TAS_TIME_MS))
                problems.push(`splits[${i}] invalide`);
        });
    const route = d.route ?? [];
    if (!Array.isArray(route) || route.length > MAX_TAS_TIME_MS / 250 + 16) problems.push("route trop longue");
    else if (!route.every((p) => Array.isArray(p) && p.length === 2 && p.every((x) => typeof x === "number" && Number.isFinite(x) && Math.abs(x) < 1e7)))
        problems.push("route invalide");
    if (problems.length) fail();
    // The category's rules (as the records: records.ts), on what the record says.
    const deaths = d.deaths as number, damage = d.damageTaken as number;
    if (category === "HUNDRED_PERCENT" && (d.collectibles as number) < (d.totalCollectibles as number)) problems.push("100 % : tous les objets ne sont pas récupérés");
    if ((category === "NO_DEATH" || category === "NO_DAMAGE") && deaths > 0) problems.push("la course contient des morts");
    if (category === "NO_DAMAGE" && damage > 0) problems.push("la course contient des dégâts");
    if ((d.collectibles as number) > (d.totalCollectibles as number)) problems.push("plus d'objets récupérés que le niveau n'en a");
    if (problems.length) fail();
    return d as never;
}

/**
 * POST /admin/tas/{levelId}/{category}/publish {record, replay}
 * `record`: the JSON of the run, as cosmic_tas wrote it (stored byte for byte); `replay`: its
 * replay file (CPRZ), in base64. Refused while another administrator holds the lease of the
 * pair at this version, and for a version of the level the server knows to be outdated.
 */
export async function publish(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const { levelId, category } = pairOr404(ctx);
    const body = await readJsonObject(ctx, 4 * 1024 * 1024);
    const v = new Validator(body, ["record", "replay"]);
    const recordText = v.str("record", { min: 2, max: MAX_RECORD_BYTES, multiline: true });
    const replayB64 = v.str("replay", { min: 4, max: Math.ceil((MAX_TAS_REPLAY_BYTES * 4) / 3) + 8 });
    v.done();
    const recordBytes = new TextEncoder().encode(recordText!);
    if (recordBytes.byteLength > MAX_RECORD_BYTES) throw new ApiError(413, "PAYLOAD_TOO_LARGE", "Fiche trop volumineuse.");
    let replayBytes: Uint8Array;
    try {
        replayBytes = fromBase64(replayB64!);
    } catch {
        throw Errors.validation([{ field: "replay", message: "base64 invalide" }]);
    }
    if (replayBytes.byteLength > MAX_TAS_REPLAY_BYTES) throw new ApiError(413, "PAYLOAD_TOO_LARGE", "Replay trop volumineux.");
    const rec = checkRecord(recordText!, levelId, category);

    // The replay's header: an exact replay of this level, of this fingerprint and simulation, of
    // this time, a run alone with the keys of the power slots.
    const problems: string[] = [];
    try {
        const rep = parseReplay(await inflateContainer(replayBytes));
        if (!rep.inputs) problems.push("pas un replay exact (entrées absentes)");
        else {
            if (!rep.inputs.keysPerSlot) problems.push("replay sans les touches des emplacements");
            if (rep.inputs.ticks * 1000 < rec.timeMs * rep.inputs.tickHz) problems.push("le replay est plus court que le temps annoncé");
        }
        if (rep.team) problems.push("replay d'une équipe");
        if (rep.challengeRules !== null) problems.push("replay d'un défi");
        if (rep.levelId !== levelId) problems.push("replay d'un autre niveau");
        if (rep.levelHash !== rec.levelHash) problems.push("replay d'une autre empreinte du niveau");
        if (rep.durationMs !== rec.timeMs) problems.push("le replay n'annonce pas le temps de la fiche");
        const sim = rep.events[0];
        if (!sim || sim.type !== SIMULATION_EVENT || sim.frame !== 0 || sim.value !== rec.simVersion) problems.push("le replay n'est pas de la version de simulation de la fiche");
    } catch (e) {
        if (!(e instanceof ReplayError)) throw e;
        problems.push(`replay illisible : ${e.message}`);
    }
    if (problems.length) throw new ApiError(422, "TAS_REPLAY_INVALID", "Le replay ne correspond pas à la fiche.", problems);

    // The level as the server knows it (when it does): a run for an older version is refused.
    const level = await levelOf(ctx, levelId);
    if (level) {
        if (rec.levelHash !== level.content_hash) throw outdatedError(level);
        const mismatch: string[] = [];
        if (rec.levelVersion !== level.current_version) mismatch.push(`levelVersion attendue : ${level.current_version}`);
        if (level.total_collectibles !== null && rec.totalCollectibles !== level.total_collectibles) mismatch.push("totalCollectibles différent du niveau");
        if (mismatch.length) throw new ApiError(422, "LEVEL_VERSION_MISMATCH", "La fiche ne correspond pas à la révision en ligne du niveau.", mismatch);
    }

    const [trainings, runs] = await Promise.all([
        ctx.env.DB.prepare("SELECT * FROM tas_training WHERE level_id = ?1 AND category = ?2").bind(levelId, category).all<TrainingRow>(),
        ctx.env.DB.prepare("SELECT * FROM tas_runs WHERE level_id = ?1 AND category = ?2 ORDER BY revision DESC").bind(levelId, category).all<RunRow>(),
    ]);
    const now = Date.now();
    const training = trainings.results.find((t) => sameVersion(t, rec.levelHash, rec.simVersion)) ?? null;
    if (training && leaseActive(training, now) && training.lease_holder !== auth.player.id) throw lockedError(training);
    const newerSim = runs.results.find((r) => r.level_hash === rec.levelHash && r.sim_version > rec.simVersion);
    if (newerSim)
        throw Errors.conflict("SIM_OUTDATED", "Une course d'une version plus récente de la simulation est déjà publiée.", { sim_version: newerSim.sim_version });
    const best = runs.results.find((r) => sameVersion(r, rec.levelHash, rec.simVersion)) ?? null;
    if (best && rec.timeMs >= best.time_ms)
        throw Errors.conflict("NOT_FASTER", "La course publiée pour cette version est déjà aussi rapide.", { time_ms: best.time_ms, revision: best.revision });

    // The files first (private bucket, never deleted), then the row: the revision is unique per
    // pair (a publication in between: this one is refused, its files removed).
    const revision = (runs.results[0]?.revision ?? 0) + 1;
    const recordSha = await sha256Hex(recordBytes);
    const replaySha = await sha256Hex(replayBytes);
    const recordKey = `tas/${levelId}/${category}/${revision}-${recordSha}.json`;
    const replayKey = `tas/${levelId}/${category}/${revision}-${replaySha}.cprz`;
    await ctx.env.STORAGE.put(recordKey, recordBytes, { httpMetadata: { contentType: "application/json; charset=utf-8" }, customMetadata: { sha256: recordSha } });
    await ctx.env.STORAGE.put(replayKey, replayBytes, { httpMetadata: { contentType: "application/octet-stream" }, customMetadata: { sha256: replaySha } });
    const publishedAt = nowIso();
    // The training time then (the mark of the run on the curve): the session under way counted
    // up to now when the publisher holds the lease (the server's clock, at most the lease's life).
    const trainedMs = training
        ? training.total_training_ms +
          (leaseActive(training, now) && training.lease_holder === auth.player.id ? Math.max(0, Math.min(now - (training.lease_beat_ms ?? now), LEASE_TTL_MS)) : 0)
        : 0;
    const res = await ctx.env.DB.prepare(
        `INSERT INTO tas_runs (level_id, category, revision, level_hash, level_version, sim_version, time_ms, explored, record_key, record_sha256,
           record_size, replay_key, replay_sha256, replay_size, training_ms_at_publish, explored_at_publish, published_by, published_by_username, published_at)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19)
         ON CONFLICT (level_id, category, revision) DO NOTHING`,
    )
        .bind(levelId, category, revision, rec.levelHash, rec.levelVersion, rec.simVersion, rec.timeMs, rec.explored, recordKey, recordSha,
            recordBytes.byteLength, replayKey, replaySha, replayBytes.byteLength, trainedMs, training?.total_explored ?? 0,
            auth.player.id, auth.player.username, publishedAt)
        .run();
    if (!res.meta.changes) {
        await ctx.env.STORAGE.delete([recordKey, replayKey]);
        throw Errors.conflict("BASE_CHANGED", "Une autre course vient d'être publiée pour ce niveau : rechargez la liste.");
    }
    await audit(ctx, auth.player.id, "admin_tas_published", {
        tas: { level_id: levelId, category, revision, time_ms: rec.timeMs, previous_ms: best?.time_ms ?? null, level_version: rec.levelVersion },
        by_username: auth.player.username,
    });
    const row = await ctx.env.DB.prepare("SELECT * FROM tas_runs WHERE level_id = ?1 AND category = ?2 AND revision = ?3")
        .bind(levelId, category, revision)
        .first<RunRow>();
    return json(ctx, { level_id: levelId, category, published: runJson(row) }, { status: 201 });
}

// ------------------------------------------------------------------ public

/**
 * GET /tas (public): the latest run of every version of every pair (each game takes the one of
 * its own version of the level, downloads it, plays it again and shows it); `current`: the
 * version the server knows as the level's current one.
 */
export async function list(ctx: Ctx): Promise<Response> {
    const [rows, levels] = await Promise.all([
        ctx.env.DB.prepare("SELECT * FROM tas_runs ORDER BY level_id, category, revision DESC").all<RunRow>(),
        ctx.env.DB.prepare("SELECT id, content_hash FROM levels").all<{ id: string; content_hash: string }>(),
    ]);
    const hashOf = new Map(levels.results.map((l) => [l.id, l.content_hash]));
    const byPair = new Map<string, RunRow[]>();
    for (const r of rows.results) {
        const k = `${r.level_id}/${r.category}`;
        byPair.set(k, [...(byPair.get(k) ?? []), r]);
    }
    const runs: Record<string, unknown>[] = [];
    for (const list of byPair.values()) {
        const { current, latestPerVersion } = splitRuns(list, hashOf.get(list[0]!.level_id) ?? null);
        for (const r of latestPerVersion)
            runs.push({
                level_id: r.level_id,
                category: r.category,
                revision: r.revision,
                level_hash: r.level_hash,
                level_version: r.level_version,
                sim_version: r.sim_version,
                time_ms: r.time_ms,
                published_at: r.published_at,
                current: r === current,
                record_sha256: r.record_sha256,
                record_size: r.record_size,
                replay_sha256: r.replay_sha256,
                replay_size: r.replay_size,
                ...filePaths(r),
            });
    }
    return json(ctx, { format: "cosmic.tas_runs", runs });
}

/** GET /tas/{levelId}/{category}/{revision}/record|replay (public): a file of a run published (any revision: they never change). */
export async function file(ctx: Ctx, which: "record" | "replay"): Promise<Response> {
    const { levelId, category } = pairOr404(ctx);
    const revision = Number(ctx.params.revision ?? "");
    const r =
        Number.isSafeInteger(revision) && revision >= 1
            ? await ctx.env.DB.prepare("SELECT * FROM tas_runs WHERE level_id = ?1 AND category = ?2 AND revision = ?3").bind(levelId, category, revision).first<RunRow>()
            : null;
    const obj = r ? await ctx.env.STORAGE.get(which === "record" ? r.record_key : r.replay_key) : null;
    if (!r || !obj) throw Errors.notFound("TAS_NOT_FOUND", "Course introuvable.");
    return new Response(obj.body, {
        status: 200,
        headers: {
            "content-type": which === "record" ? "application/json; charset=utf-8" : "application/octet-stream",
            "x-content-type-options": "nosniff",
            "cache-control": "public, max-age=86400, immutable",
            "x-content-sha256": which === "record" ? r.record_sha256 : r.replay_sha256,
            "x-request-id": ctx.requestId,
        },
    });
}
