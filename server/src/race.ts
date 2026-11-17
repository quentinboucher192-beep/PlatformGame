// The « course à la vérification » of EXTRÊME levels (one life): every attempt is ranked by
// its progress score, even far from the end; the first to finish is the level's verifier.
// The score is computed here from the attempt's replay (never taken from the client): the
// route reached (40 points) is the trajectory projected on the level's intended route, and
// the bosses' health removed and the ambushes' waves beaten (60 points: 2 shares per boss,
// 1 per ambush) come from the replay's events. Limit (honestly): the server does not
// re-simulate the run; it checks the replay's consistency and plausibility, as for records.
// The same analysis scores the attempts of a challenge (« Plus loin », challenges.ts): with
// RaceOptions, the replay carries the challenge's rules, and deaths with returns to the
// checkpoints are allowed when the challenge has its checkpoints on.

import { refreshClanOfPlayer } from "./clans";
import { optionalAuth, requireAuth } from "./auth";
import { pseudoVersionOf } from "./community";
import { boardCosmetics, withCosmetics } from "./cosmetics";
import { ApiError, Errors, json, nowIso, readJsonObject, readLimited, type Ctx } from "./http";
import type { LevelVersionRow } from "./model";
import { notify } from "./notifications";
import { RULES, rateLimit } from "./ratelimit";
import { CHALLENGE_REPLAY_ELSEWHERE, EventType, inflateContainer, MAX_REPLAY_UPLOAD, parseReplay, ReplayError, type Replay } from "./replay";
import { Validator } from "./validate";

export interface RaceLayout {
    route: [number, number][];
    bosses: { id: string; name: string }[];
    ambushes: { id: string; waves: number }[];
}

const OBJECT_ID_RE = /^[A-Za-z0-9_.:-]{1,64}$/;
const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const HASH_RE = /^[0-9a-f]{64}$/;
const ATTEMPT_RE = /^[A-Za-z0-9-]{8,64}$/;

/** A layout declared with a level version (validated: bounded sizes, finite numbers). */
export function parseLayout(v: unknown): RaceLayout | null {
    if (typeof v !== "object" || v === null) return null;
    const o = v as Record<string, unknown>;
    if (!Array.isArray(o.route) || o.route.length < 2 || o.route.length > 500) return null;
    const route: [number, number][] = [];
    for (const p of o.route) {
        if (!Array.isArray(p) || p.length !== 2 || !p.every((n) => typeof n === "number" && Number.isFinite(n) && Math.abs(n) < 1e6)) return null;
        route.push([p[0] as number, p[1] as number]);
    }
    const bosses: RaceLayout["bosses"] = [];
    const ambushes: RaceLayout["ambushes"] = [];
    if (!Array.isArray(o.bosses) || o.bosses.length > 64 || !Array.isArray(o.ambushes) || o.ambushes.length > 64) return null;
    for (const b of o.bosses) {
        const r = b as Record<string, unknown>;
        if (typeof r?.id !== "string" || !OBJECT_ID_RE.test(r.id)) return null;
        bosses.push({ id: r.id, name: typeof r.name === "string" ? r.name.slice(0, 40) : r.id });
    }
    for (const a of o.ambushes) {
        const r = a as Record<string, unknown>;
        if (typeof r?.id !== "string" || !OBJECT_ID_RE.test(r.id) || typeof r.waves !== "number" || !Number.isInteger(r.waves) || r.waves < 1 || r.waves > 99)
            return null;
        ambushes.push({ id: r.id, waves: r.waves });
    }
    return { route, bosses, ambushes };
}

/** The progress score (0..100, one decimal); 100 only when finished. Same as game/race/Race.cpp. */
export function raceScore(layout: RaceLayout, route: number, bossRemoved: number[], wavesDone: number[], finished: boolean): number {
    if (finished) return 100;
    const clamp = (x: number) => Math.min(1, Math.max(0, x));
    const shares = 2 * layout.bosses.length + layout.ambushes.length;
    let score: number;
    if (shares === 0) score = 100 * clamp(route);
    else {
        let got = 0;
        layout.bosses.forEach((_, i) => (got += 2 * clamp(bossRemoved[i] ?? 0)));
        layout.ambushes.forEach((a, i) => (got += clamp((wavesDone[i] ?? 0) / Math.max(1, a.waves))));
        score = 40 * clamp(route) + (60 * got) / shares;
    }
    return Math.min(99.9, Math.floor(score * 10 + 1e-6) / 10);
}

/** The furthest fraction of the route reached by a trajectory (local search, like the game). */
export function routeReached(route: [number, number][], frames: { x: number; y: number }[]): number {
    if (route.length < 2) return 0;
    const cum = [0];
    for (let i = 1; i < route.length; ++i) cum.push(cum[i - 1]! + Math.hypot(route[i]![0] - route[i - 1]![0], route[i]![1] - route[i - 1]![1]));
    const length = cum[cum.length - 1]!;
    if (length <= 1) return 0;
    const d2 = (i: number, f: { x: number; y: number }) => (route[i]![0] - f.x) ** 2 + (route[i]![1] - f.y) ** 2;
    const nearest = (a: number, b: number, f: { x: number; y: number }) => {
        let best = a;
        for (let i = a; i < b; ++i) if (d2(i, f) < d2(best, f)) best = i;
        return best;
    };
    let hint = 0;
    let best = 0;
    for (const f of frames) {
        let i = nearest(Math.max(0, hint - 60), Math.min(route.length, hint + 60), f);
        if (d2(i, f) > 160 * 160) {
            const g = nearest(0, route.length, f);
            if (d2(g, f) < d2(i, f)) i = g;
        }
        hint = i;
        best = Math.max(best, cum[i]! / length);
    }
    return best;
}

export interface Claim {
    finished: boolean;
    bosses: number[]; // per mille removed, per boss of the layout
    waves: number[];
}

export interface RaceAnalysis {
    reasons: string[];
    route: number;
    bossRemoved: number[];
    wavesDone: number[];
    finished: boolean;
}

/** An attempt of a challenge (« Plus loin ») instead of a race attempt. */
export interface RaceOptions {
    /** Canonical form of the challenge's rules: the replay's section (format 4) must be exactly this. */
    challengeRules: string;
    /** The challenge's hearts: the sampled health never goes above. */
    hearts: number;
    /** The challenge's checkpoints are on: deaths and returns to a checkpoint are allowed. */
    respawns: boolean;
}

/**
 * Checks an attempt's replay (one life, plausible trajectory) and reads its progress from it.
 * With `challenge`, an attempt of a challenge: its rules in the replay, health within its
 * hearts, the level's time limit, and (checkpoints on) as many lives as needed — the
 * trajectory may then jump at a death or a return to a checkpoint, and the progress is the
 * furthest reached.
 */
export function analyzeRace(
    rep: Replay,
    attemptId: string,
    timeMs: number,
    claim: Claim,
    layout: RaceLayout,
    level: LevelVersionRow,
    challenge?: RaceOptions,
): RaceAnalysis {
    const reasons: string[] = [];
    if (rep.levelHash !== level.content_hash) reasons.push("version de niveau différente");
    if (rep.attemptId !== attemptId) reasons.push("tentative différente");
    if (Math.abs(rep.durationMs - timeMs) > 500) reasons.push("durée du replay différente de la tentative");
    if (rep.frames.length === 0) reasons.push("replay vide");
    if (!challenge) {
        if (rep.challengeRules !== null) reasons.push(CHALLENGE_REPLAY_ELSEWHERE);
    } else {
        if (rep.formatVersion < 4 || rep.challengeRules === null) reasons.push("replay sans les règles du défi (format 4 attendu)");
        else if (rep.challengeRules !== challenge.challengeRules) reasons.push("règles du replay différentes de celles du défi");
        const maxHealth = rep.frames.reduce((m, f) => Math.max(m, f.health), 0);
        if (maxHealth > challenge.hearts) reasons.push(`santé du replay (${maxHealth}) au-dessus des cœurs du défi (${challenge.hearts})`);
        if (rep.frames.length > (level.time_limit_ms / 1000 + 5) * rep.sampleHz) reasons.push("replay plus long que la limite du niveau");
    }
    const lives = challenge?.respawns === true;
    const expectedFrames = (rep.durationMs * rep.sampleHz) / 1000;
    if (Math.abs(rep.frames.length - expectedFrames) > rep.sampleHz * 1.5) reasons.push("nombre d'images incohérent avec la durée");
    const bossRemoved = layout.bosses.map(() => 0);
    const wavesDone = layout.ambushes.map(() => 0);
    let deaths = 0;
    let finishFrame = -1;
    let lastFrame = 0;
    // Frames where the trajectory may jump (several lives only): a death, a return, a teleport.
    const jumps = new Set<number>();
    for (const e of rep.events) {
        if (e.frame < lastFrame || e.frame >= rep.frames.length) {
            reasons.push("événements incohérents");
            break;
        }
        lastFrame = e.frame;
        if (lives && (e.type === EventType.Death || e.type === EventType.Respawn || e.type === EventType.Teleport)) jumps.add(e.frame);
        if (e.type === EventType.Death) ++deaths;
        else if (e.type === EventType.Respawn) {
            if (!lives) reasons.push("une seule vie : retour à un point de passage impossible");
        } else if (e.type === EventType.Finish) finishFrame = e.frame;
        else if (e.type === 12 /* BossHit */) {
            if (e.arg >= bossRemoved.length || e.value > 1000) reasons.push("boss inconnu");
            else bossRemoved[e.arg] = Math.max(bossRemoved[e.arg]!, e.value);
        } else if (e.type === 13 /* WaveCleared */) {
            if (e.arg >= wavesDone.length || e.value > layout.ambushes[e.arg]!.waves) reasons.push("embuscade inconnue");
            else wavesDone[e.arg] = Math.max(wavesDone[e.arg]!, e.value);
        }
    }
    if (deaths > 1 && !lives) reasons.push("plusieurs vies utilisées");
    // What the game declared must be what the replay shows.
    if (claim.bosses.length !== bossRemoved.length || claim.bosses.some((v, i) => Math.abs(v - bossRemoved[i]!) > 10))
        reasons.push("dégâts aux boss différents du replay");
    if (claim.waves.length !== wavesDone.length || claim.waves.some((v, i) => v !== wavesDone[i])) reasons.push("vagues différentes du replay");
    // Trajectory: from the spawn, inside the level, no impossible movement.
    const first = rep.frames[0];
    if (first && Math.hypot(first.x - level.spawn_x, first.y - level.spawn_y) > 96) reasons.push("départ éloigné du point de départ");
    const step = (level.max_speed_px_s / rep.sampleHz) * 1.25 + 4;
    let tooFast = 0;
    for (let i = 0; i < rep.frames.length; ++i) {
        const f = rep.frames[i]!;
        if (!Number.isFinite(f.x) || !Number.isFinite(f.y) || f.x < -512 || f.y < -1024 || f.x > level.bounds_w + 512 || f.y > level.bounds_h + 1024) {
            reasons.push("position hors du niveau");
            break;
        }
        if (i > 0 && !(jumps.has(i) || jumps.has(i - 1) || jumps.has(i + 1))) {
            const p = rep.frames[i - 1]!;
            if (Math.hypot(f.x - p.x, f.y - p.y) > step + 200) ++tooFast;
        }
    }
    if (tooFast > 2) reasons.push("déplacements impossibles");
    let finished = false;
    if (claim.finished) {
        const last = rep.frames[rep.frames.length - 1];
        const m = 160;
        const inExit = last && last.x >= level.exit_x - m && last.x <= level.exit_x + level.exit_w + m && last.y >= level.exit_y - m && last.y <= level.exit_y + level.exit_h + m;
        if (finishFrame < 0 || !inExit) reasons.push("fin du niveau absente du replay");
        else finished = true;
    }
    return {
        reasons: [...new Set(reasons)],
        route: routeReached(layout.route, rep.frames),
        bossRemoved: bossRemoved.map((v) => v / 1000),
        wavesDone,
        finished,
    };
}

// --------------------------------------------------------------------- boards

interface Board {
    key: string;
    /** The boards this one counts (2.47.1): itself, and for an official level the boards of the
     *  revisions it carried with « Garder les records » (campaign_board_links), as the level
     *  boards count their bests. A race is never copied: it is read where it was run. */
    keys: string[];
    kind: "community" | "official";
    levelId: string;
    version: number;
    title: string;
    layout: RaceLayout;
    level: LevelVersionRow;
    authorId: string | null;
}

async function loadBoard(ctx: Ctx, kind: string, levelId: string, version: number | null, levelHash: string | null): Promise<Board> {
    if (kind === "community") {
        if (!ID_RE.test(levelId)) throw Errors.notFound("RACE_NOT_FOUND", "Course introuvable.");
        const l = await ctx.env.DB.prepare("SELECT id, title, author_id, extreme, current_version, status FROM community_levels WHERE id = ?1")
            .bind(levelId)
            .first<{ id: string; title: string; author_id: string; extreme: number; current_version: number; status: string }>();
        if (!l || l.status !== "published" || !l.extreme) throw Errors.notFound("RACE_NOT_FOUND", "Ce niveau n'est pas un niveau EXTRÊME publié.");
        const v = version ?? l.current_version;
        const row = await ctx.env.DB.prepare("SELECT * FROM community_level_versions WHERE level_id = ?1 AND version = ?2 AND status <> 'uploading'")
            .bind(l.id, v)
            .first<Record<string, unknown>>();
        const layout = row ? parseLayout(JSON.parse(String(row.race ?? "null"))) : null;
        if (!row || !layout) throw Errors.notFound("RACE_NOT_FOUND", "Pas de course pour cette version.");
        const key = `c:${l.id}:${v}`;
        return { key, keys: [key], kind, levelId: l.id, version: v, title: l.title, layout, level: pseudoVersionOf(row), authorId: l.author_id };
    }
    if (kind === "official") {
        if (!/^[a-z0-9_]{1,64}$/.test(levelId)) throw Errors.notFound("RACE_NOT_FOUND", "Course introuvable.");
        const row = await ctx.env.DB.prepare(
            levelHash
                ? "SELECT * FROM level_versions WHERE level_id = ?1 AND content_hash = ?2"
                : "SELECT * FROM level_versions WHERE level_id = ?1 AND compatibility_status = 'active' ORDER BY version DESC LIMIT 1",
        )
            .bind(levelId, ...(levelHash ? [levelHash] : []))
            .first<LevelVersionRow & { race: string | null }>();
        const layout = row ? parseLayout(JSON.parse(row.race ?? "null")) : null;
        if (!row || !layout) throw Errors.notFound("RACE_NOT_FOUND", "Ce niveau n'a pas de course à la vérification.");
        const name = await ctx.env.DB.prepare("SELECT name FROM levels WHERE id = ?1").bind(levelId).first<{ name: string }>();
        return {
            key: `o:${levelId}:${row.content_hash}`,
            keys: await officialKeys(ctx, levelId, row.content_hash),
            kind,
            levelId,
            version: row.version,
            title: name?.name ?? levelId,
            layout,
            level: row,
            authorId: null,
        };
    }
    throw Errors.validation([{ field: "kind", message: "community ou official" }]);
}

/**
 * The race boards an official board of hash `hash` counts: its own and those of the revisions
 * it carried (« Garder les records », campaign_board_links, kept transitive at publication).
 */
async function officialKeys(ctx: Ctx, levelId: string, hash: string): Promise<string[]> {
    const links = await ctx.env.DB.prepare("SELECT from_hash FROM campaign_board_links WHERE level_id = ?1 AND to_hash = ?2 ORDER BY from_hash")
        .bind(levelId, hash)
        .all<{ from_hash: string }>();
    return [`o:${levelId}:${hash}`, ...links.results.map((l) => `o:${levelId}:${l.from_hash}`)];
}

/** SQL list of the boards of a Board, bound as JSON. */
const IN_KEYS = (n: number) => `IN (SELECT value FROM json_each(?${n}))`;

/** What an attempt did, per boss and ambush of the layout (for the boards). */
export function detailOf(layout: RaceLayout, a: RaceAnalysis) {
    return {
        bosses: layout.bosses.map((b, i) => ({ name: b.name, removed: Math.round((a.bossRemoved[i] ?? 0) * 1000) / 1000 })),
        ambushes: layout.ambushes.map((x, i) => ({ waves: x.waves, done: a.wavesDone[i] ?? 0 })),
    };
}

/** POST /races/runs {kind, level_id, version?, level_hash?, attempt_id, time_ms, finished, bosses, waves} */
export async function submitRun(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.raceSubmit, "player:" + me);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["kind", "level_id", "version", "level_hash", "attempt_id", "time_ms", "finished", "bosses", "waves"]);
    const kind = v.oneOf("kind", ["community", "official"] as const)!;
    const levelId = v.str("level_id", { min: 1, max: 64 })!;
    const version = v.int("version", { min: 1, max: 100000, optional: true }) ?? null;
    const levelHash = v.str("level_hash", { min: 64, max: 64, pattern: HASH_RE, optional: true }) ?? null;
    const attemptId = v.str("attempt_id", { min: 8, max: 64, pattern: ATTEMPT_RE })!;
    const timeMs = v.int("time_ms", { min: 0, max: 7200 * 1000 })!;
    if (typeof body.finished !== "boolean") v.fail("finished", "booléen attendu");
    const ints = (k: string, max: number) => {
        const a = body[k];
        if (!Array.isArray(a) || a.length > 64 || !a.every((n) => Number.isInteger(n) && n >= 0 && n <= max)) {
            v.fail(k, "liste d'entiers attendue");
            return [];
        }
        return a as number[];
    };
    const claim: Claim = { finished: body.finished === true, bosses: ints("bosses", 1000), waves: ints("waves", 99) };
    v.done();
    const board = await loadBoard(ctx, kind, levelId, version, levelHash);
    const id = crypto.randomUUID();
    try {
        await ctx.env.DB.prepare("INSERT INTO race_runs (id, board, player_id, attempt_id, claim, time_ms, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)")
            .bind(id, board.key, me, attemptId, JSON.stringify(claim), timeMs, nowIso())
            .run();
    } catch (e) {
        if (String(e).includes("UNIQUE")) throw Errors.conflict("RUN_EXISTS", "Cette tentative a déjà été envoyée.");
        throw e;
    }
    return json(ctx, { run: { id, status: "PENDING" }, upload: `/api/v1/races/runs/${id}/replay` }, { status: 201 });
}

/** PUT /races/runs/{runId}/replay : the attempt's replay; the server computes the score. */
export async function uploadRunReplay(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.raceSubmit, "player:" + me);
    const run = await ctx.env.DB.prepare("SELECT * FROM race_runs WHERE id = ?1").bind(ctx.params.runId!).first<Record<string, unknown>>();
    if (!run || run.player_id !== me) throw Errors.notFound("RUN_NOT_FOUND", "Tentative introuvable.");
    if (run.replay_key) throw Errors.conflict("REPLAY_EXISTS", "Le replay de cette tentative a déjà été envoyé.");
    const type = (ctx.request.headers.get("content-type") ?? "").toLowerCase();
    if (!type.startsWith("application/octet-stream")) throw new ApiError(415, "UNSUPPORTED_MEDIA_TYPE", "Le replay doit être envoyé en application/octet-stream.");
    const data = await readLimited(ctx.request, MAX_REPLAY_UPLOAD);
    let rep: Replay;
    try {
        rep = parseReplay(await inflateContainer(data));
    } catch (e) {
        if (e instanceof ReplayError) throw new ApiError(422, "REPLAY_MALFORMED", `Replay illisible : ${e.message}.`);
        throw e;
    }
    const [kind, levelId, rest] = String(run.board).split(":") as [string, string, string];
    const board = await loadBoard(ctx, kind === "c" ? "community" : "official", levelId, kind === "c" ? Number(rest) : null, kind === "o" ? rest : null);
    const claim = JSON.parse(String(run.claim)) as Claim;
    const a = analyzeRace(rep, String(run.attempt_id), Number(run.time_ms), claim, board.layout, board.level);
    const ok = a.reasons.length === 0;
    const score = raceScore(board.layout, a.route, a.bossRemoved, a.wavesDone, a.finished);
    const key = `races/${board.key.replace(/:/g, "_")}/${run.id}.cprz`;
    await ctx.env.STORAGE.put(key, data, { httpMetadata: { contentType: "application/octet-stream" } });
    const now = nowIso();
    await ctx.env.DB.prepare(
        `UPDATE race_runs SET replay_key = ?2, status = ?3, score = ?4, route = ?5, bosses_beaten = ?6, waves = ?7, finished = ?8, detail = ?9,
           reasons = ?10, verified_at = ?11 WHERE id = ?1`,
    )
        .bind(
            run.id,
            key,
            ok ? "VERIFIED" : "REJECTED",
            score,
            a.route,
            a.bossRemoved.filter((b) => b >= 0.999).length,
            a.wavesDone.reduce((s, w) => s + w, 0),
            a.finished ? 1 : 0,
            JSON.stringify(detailOf(board.layout, a)),
            ok ? null : JSON.stringify(a.reasons.slice(0, 8)),
            ok ? now : null,
        )
        .run();
    let verifier = false;
    if (ok && a.finished) verifier = await crown(ctx, board, me, String(run.id), now);
    // 2.66: the official EXTRÊME levels count for the clans (their best per level).
    if (ok && board.kind === "official") await refreshClanOfPlayer(ctx, me);
    const rank = ok ? await rankOf(ctx, board.keys, me) : null;
    return json(ctx, {
        run: { id: run.id, status: ok ? "VERIFIED" : "REJECTED", score, finished: a.finished, reasons: ok ? [] : a.reasons.slice(0, 8), rank },
        verifier,
    });
}

/** The first finished run of a board crowns its player (once); everyone in the race is told. */
async function crown(ctx: Ctx, board: Board, me: string, runId: string, now: string): Promise<boolean> {
    // A board that carried an earlier revision keeps its verifier (the race goes on).
    if (board.keys.length > 1) {
        const earlier = await ctx.env.DB.prepare(`SELECT 1 AS x FROM race_victors WHERE board ${IN_KEYS(1)} LIMIT 1`).bind(JSON.stringify(board.keys)).first();
        if (earlier) return false;
    }
    const r = await ctx.env.DB.prepare("INSERT OR IGNORE INTO race_victors (board, player_id, run_id, at) VALUES (?1, ?2, ?3, ?4)").bind(board.key, me, runId, now).run();
    if (r.meta.changes !== 1) return false;
    if (board.kind === "community")
        await ctx.env.DB.prepare("UPDATE community_levels SET verified_by = ?2, verified_at = ?3, verified_version = ?4 WHERE id = ?1")
            .bind(board.levelId, me, now, board.version)
            .run();
    const others = await ctx.env.DB.prepare(`SELECT DISTINCT player_id FROM race_runs WHERE board ${IN_KEYS(1)} AND player_id <> ?2`)
        .bind(JSON.stringify(board.keys), me)
        .all<{ player_id: string }>();
    const told = new Set(others.results.map((o) => o.player_id));
    if (board.authorId && board.authorId !== me) told.add(board.authorId);
    for (const pid of told) await notify(ctx, pid, "race_verified", {
            actor: me,
            levelId: board.kind === "community" ? board.levelId : null,
            data: board.kind === "official" ? { title: board.title, official: board.levelId } : { title: board.title },
        });
    return true;
}

async function rankOf(ctx: Ctx, keys: string[], me: string): Promise<number | null> {
    const r = await ctx.env.DB.prepare(
        `WITH best AS (SELECT player_id, score, verified_at, ROW_NUMBER() OVER (PARTITION BY player_id ORDER BY score DESC, verified_at ASC) AS n
                       FROM race_runs WHERE board ${IN_KEYS(1)} AND status = 'VERIFIED'),
              ranked AS (SELECT player_id, ROW_NUMBER() OVER (ORDER BY score DESC, verified_at ASC) AS rank FROM best WHERE n = 1)
         SELECT rank FROM ranked WHERE player_id = ?2`,
    )
        .bind(JSON.stringify(keys), me)
        .first<{ rank: number }>();
    return r?.rank ?? null;
}

/** GET /races/board?kind=&level_id=&version=|level_hash= : the ranking (best verified attempt of each player). */
export async function getBoard(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const me = viewer?.player.id ?? "";
    const q = ctx.url.searchParams;
    const ver = q.get("version");
    const board = await loadBoard(ctx, q.get("kind") ?? "", q.get("level_id") ?? "", ver ? Number(ver) : null, q.get("level_hash"));
    const rows = await ctx.env.DB.prepare(
        `WITH best AS (SELECT r.*, ROW_NUMBER() OVER (PARTITION BY r.player_id ORDER BY r.score DESC, r.verified_at ASC) AS n
                       FROM race_runs r WHERE r.board ${IN_KEYS(1)} AND r.status = 'VERIFIED')
         SELECT b.id, b.player_id, b.score, b.route, b.bosses_beaten, b.waves, b.finished, b.detail, b.time_ms, b.verified_at,
                b.replay_key IS NOT NULL AND p.profile_visibility != 'private' AS has_replay,
                p.username, p.avatar FROM best b JOIN players p ON p.id = b.player_id WHERE b.n = 1
         ORDER BY b.score DESC, b.verified_at ASC LIMIT 100`,
    )
        .bind(JSON.stringify(board.keys))
        .all<Record<string, unknown>>();
    const victor = await ctx.env.DB.prepare(
        `SELECT v.at, p.id AS pid, p.username, p.avatar FROM race_victors v LEFT JOIN players p ON p.id = v.player_id
         WHERE v.board ${IN_KEYS(1)} ORDER BY v.at LIMIT 1`,
    )
        .bind(JSON.stringify(board.keys))
        .first<Record<string, unknown>>();
    const pending = me
        ? await ctx.env.DB.prepare(`SELECT COUNT(*) AS n FROM race_runs WHERE board ${IN_KEYS(1)} AND player_id = ?2 AND status = 'PENDING'`)
              .bind(JSON.stringify(board.keys), me)
              .first<{ n: number }>()
        : null;
    const participants = await ctx.env.DB.prepare(`SELECT COUNT(DISTINCT player_id) AS n FROM race_runs WHERE board ${IN_KEYS(1)} AND status = 'VERIFIED'`)
        .bind(JSON.stringify(board.keys))
        .first<{ n: number }>();
    // Frame, title and suit of each ranked player (2.23), when their profile is visible.
    const looks = await boardCosmetics(ctx, rows.results.map((r) => String(r.player_id)));
    return json(ctx, {
        race: {
            kind: board.kind,
            level_id: board.levelId,
            version: board.version,
            title: board.title,
            bosses: board.layout.bosses.map((b) => b.name),
            ambushes: board.layout.ambushes.length,
            participants: participants?.n ?? 0,
        },
        verifier: victor ? { player: victor.pid ? { id: victor.pid, username: victor.username, avatar: victor.avatar } : null, at: victor.at } : null,
        ranking: rows.results.map((r, i) => ({
            rank: i + 1,
            player: withCosmetics({ id: String(r.player_id), username: r.username, avatar: r.avatar }, looks),
            score: r.score,
            route: r.route,
            bosses_beaten: r.bosses_beaten,
            waves: r.waves,
            finished: r.finished === 1,
            detail: JSON.parse(String(r.detail ?? "null")),
            time_ms: r.time_ms,
            at: r.verified_at,
            run_id: String(r.id),
            has_replay: r.has_replay === 1 || r.player_id === me,
            me: r.player_id === me,
        })),
        my_pending: pending?.n ?? 0,
    });
}

/**
 * GET /races/runs/{runId}/replay : the replay of a ranked attempt (2.32.2: watched from the
 * board). Public for a VERIFIED attempt of an active, non-private player; the owner always.
 */
export async function downloadRunReplay(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const id = ctx.params.runId ?? "";
    if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(id)) throw Errors.notFound("RUN_NOT_FOUND", "Tentative introuvable.");
    const r = await ctx.env.DB.prepare(
        `SELECT r.player_id, r.status, r.replay_key, p.account_status, p.profile_visibility
           FROM race_runs r JOIN players p ON p.id = r.player_id WHERE r.id = ?1`,
    )
        .bind(id)
        .first<{ player_id: string; status: string; replay_key: string | null; account_status: string; profile_visibility: string }>();
    const isOwner = !!r && viewer?.player.id === r.player_id;
    if (!r || (!isOwner && (r.status !== "VERIFIED" || r.account_status !== "active"))) throw Errors.notFound("RUN_NOT_FOUND", "Tentative introuvable.");
    if (!isOwner && r.profile_visibility === "private") throw Errors.forbidden("PROFILE_PRIVATE", "Le joueur a masqué son profil.");
    if (!r.replay_key) throw Errors.notFound("REPLAY_NOT_FOUND", "Aucun replay pour cette tentative.");
    const obj = await ctx.env.STORAGE.get(r.replay_key);
    if (!obj) throw Errors.notFound("REPLAY_NOT_FOUND", "Fichier de replay absent.");
    return new Response(obj.body, {
        headers: {
            "content-type": "application/octet-stream",
            "content-length": String(obj.size),
            "cache-control": "private, max-age=3600",
            "x-content-type-options": "nosniff",
        },
    });
}

/**
 * GET /players/{playerId}/races : a player's best progressions (2.32.2, profile tab
 * « Meilleures progressions ») — their best VERIFIED attempt on every EXTRÊME level they
 * raced, with its rank on that board. Hidden for a private profile (except to its owner).
 */
export async function playerRaces(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const pid = ctx.params.playerId ?? "";
    const p = await ctx.env.DB.prepare("SELECT id, account_status, profile_visibility FROM players WHERE id = ?1")
        .bind(pid)
        .first<{ id: string; account_status: string; profile_visibility: string }>();
    if (!p || p.account_status !== "active") throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    const self = viewer?.player.id === p.id;
    if (!self && p.profile_visibility === "private") throw Errors.forbidden("PROFILE_PRIVATE", "Ce profil est privé.");
    const rows = await ctx.env.DB.prepare(
        `WITH best AS (SELECT r.*, ROW_NUMBER() OVER (PARTITION BY r.board ORDER BY r.score DESC, r.verified_at ASC) AS n
                       FROM race_runs r WHERE r.player_id = ?1 AND r.status = 'VERIFIED')
         SELECT id, board, score, finished, time_ms, verified_at, replay_key IS NOT NULL AS has_replay FROM best WHERE n = 1
         ORDER BY verified_at DESC LIMIT 50`,
    )
        .bind(p.id)
        .all<{ id: string; board: string; score: number; finished: number; time_ms: number; verified_at: string; has_replay: number }>();
    // An official race run on a revision the current one carried (« Garder les records ») counts
    // on the current board (2.47.1): one line per level, its best, ranked on the merged board.
    type Row = (typeof rows.results)[number];
    const resolved: { row: Row; key: string; keys: string[] }[] = [];
    const currentOf = new Map<string, string[] | null>();
    for (const r of rows.results) {
        const [k, levelId, third] = r.board.split(":");
        if (k !== "o" || !levelId || !third) {
            resolved.push({ row: r, key: r.board, keys: [r.board] });
            continue;
        }
        if (!currentOf.has(levelId)) {
            const cur = await ctx.env.DB.prepare("SELECT content_hash FROM levels WHERE id = ?1").bind(levelId).first<{ content_hash: string }>();
            currentOf.set(levelId, cur ? await officialKeys(ctx, levelId, cur.content_hash) : null);
        }
        const cur = currentOf.get(levelId);
        if (cur && cur.includes(r.board)) resolved.push({ row: r, key: cur[0]!, keys: cur });
        else resolved.push({ row: r, key: r.board, keys: [r.board] });
    }
    const best = new Map<string, { row: Row; key: string; keys: string[] }>();
    for (const e of resolved) {
        const b = best.get(e.key);
        if (!b || e.row.score > b.row.score || (e.row.score === b.row.score && e.row.verified_at < b.row.verified_at)) best.set(e.key, e);
    }
    const races = [];
    for (const { row: r, key: boardKey, keys } of [...best.values()].sort((x, y) => (x.row.verified_at < y.row.verified_at ? 1 : -1))) {
        const [k, levelId, third] = boardKey.split(":");
        if (!levelId || !third) continue;
        let title: string | null = null;
        let version: number | null = null;
        if (k === "c") {
            const l = await ctx.env.DB.prepare("SELECT title FROM community_levels WHERE id = ?1 AND status = 'published'")
                .bind(levelId)
                .first<{ title: string }>();
            if (!l) continue;  // a level no longer public: not shown
            title = l.title;
            version = Number(third);
        } else if (k === "o") {
            const l = await ctx.env.DB.prepare("SELECT name FROM levels WHERE id = ?1").bind(levelId).first<{ name: string }>();
            title = l?.name ?? levelId;
            const v = await ctx.env.DB.prepare("SELECT version FROM level_versions WHERE level_id = ?1 AND content_hash = ?2")
                .bind(levelId, third)
                .first<{ version: number }>();
            version = v?.version ?? null;
        } else continue;
        const participants = await ctx.env.DB.prepare(`SELECT COUNT(DISTINCT player_id) AS n FROM race_runs WHERE board ${IN_KEYS(1)} AND status = 'VERIFIED'`)
            .bind(JSON.stringify(keys))
            .first<{ n: number }>();
        races.push({
            kind: k === "c" ? "community" : "official",
            level_id: levelId,
            version,
            title,
            score: r.score,
            finished: r.finished === 1,
            time_ms: r.time_ms,
            rank: await rankOf(ctx, keys, p.id),
            participants: participants?.n ?? 0,
            run_id: r.id,
            has_replay: r.has_replay === 1,
            at: r.verified_at,
        });
    }
    return json(ctx, { player_id: p.id, races });
}
