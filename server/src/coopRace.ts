// The « course à la vérification » of teams (2.62, docs/COOP.md § 6): Duo (2 players) and Trio
// (3 players) boards, never mixed with each other nor with the solo boards (race_runs, whose
// records all stay valid).
//
// A team is its exact players (team_key: their ids sorted, joined by '+'). Its progress is its
// most advanced player's (the route reached by any of them; the bosses and ambushes are the
// team's). Ties: fewer revivals, then the earliest. A team that lost a player for good
// (« incomplète ») comes after a complete one at the same score.
//
// Verification, as for a solo attempt (consistency and plausibility of the replay, no
// re-simulation): every player's trajectory, the revivals (2 at most), one fall per player
// beside them, the inputs of every player for every step. Online, the inputs must be exactly
// those the relay gave (coop_sessions). Each player confirms the attempt with their own account
// (coop_confirms): the team is ranked once all of them did; a player without an account means
// no ranking.

import { optionalAuth, requireAuth } from "./auth";
import { clanPointsStatements } from "./clans";
import { ApiError, Errors, json, log, nowIso, readJsonObject, readLimited, type Ctx } from "./http";
import { IN_KEYS, loadBoard, raceScore, routeReached, type RaceLayout } from "./race";
import { RULES, rateLimit } from "./ratelimit";
import { EventType, inflateContainer, MAX_REPLAY_UPLOAD, parseReplay, ReplayError, type Replay, type ReplayFrame } from "./replay";
import { toHex } from "./crypto";
import type { LevelVersionRow } from "./model";
import { Validator } from "./validate";

/** Version of the team rules (game/online/LevelFingerprint.h: kCoopSimulationVersion). */
export const COOP_SIMULATION_VERSION = 4; // 2.75: the players pass through each other (version 1 attempts refused, their records kept); 3 (2.77): souls on the last safe place, revivals held (« Interagir »); 4 (2.80): a passage that closes behind the team waits for every player (the records kept)
export const MAX_REVIVES = 2;

const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const HASH_RE = /^[0-9a-f]{64}$/;
const Revive = 15;
const Regroup = 16;
const Return = 18;

export interface TeamClaim {
    finished: boolean;
    bosses: number[];
    waves: number[];
}

export interface TeamAnalysis {
    reasons: string[];
    route: number;
    bossRemoved: number[];
    wavesDone: number[];
    finished: boolean;
    revives: number;
    incomplete: boolean;
}

/** The SHA-256 of a team replay's inputs: step by step, seat by seat, 16 bytes each (as the relay hashes them). */
export async function teamInputsHash(rep: Replay): Promise<string | null> {
    if (!rep.team || !rep.inputs) return null;
    const ticks = rep.inputs.ticks;
    const players = rep.team.players.length;
    const out = new Uint8Array(ticks * players * 16);
    rep.team.players.forEach((p, s) => {
        let t = 0;
        for (let at = 0; at + 18 <= p.runs.length; at += 18) {
            const repeat = p.runs[at]! | (p.runs[at + 1]! << 8);
            const frame = p.runs.subarray(at + 2, at + 18);
            for (let k = 0; k < repeat && t < ticks; ++k, ++t) out.set(frame, (t * players + s) * 16);
        }
    });
    return toHex(new Uint8Array(await crypto.subtle.digest("SHA-256", out)));
}

/** Checks a team's replay (format 5) and reads its progress from it. */
export function analyzeTeam(rep: Replay, attemptId: string, timeMs: number, size: number, claim: TeamClaim, layout: RaceLayout, level: LevelVersionRow): TeamAnalysis {
    const reasons: string[] = [];
    const team = rep.team;
    if (!team) reasons.push("replay d'équipe attendu (format 5)");
    else {
        if (team.size !== size) reasons.push("taille d'équipe différente du replay");
        if (team.coopVersion !== COOP_SIMULATION_VERSION) reasons.push("version des règles de coopération différente");
    }
    if (rep.levelHash !== level.content_hash) reasons.push("version de niveau différente");
    if (rep.attemptId !== attemptId) reasons.push("tentative différente");
    if (Math.abs(rep.durationMs - timeMs) > 500) reasons.push("durée du replay différente de la tentative");
    if (rep.challengeRules !== null) reasons.push("partie de défi : elle ne compte que pour son défi");
    const expectedFrames = (rep.durationMs * rep.sampleHz) / 1000;
    if (Math.abs(rep.frames.length - expectedFrames) > rep.sampleHz * 1.5) reasons.push("nombre d'images incohérent avec la durée");
    const players = team ? team.players : [{ frames: rep.frames, events: rep.events, runs: new Uint8Array() }];
    const bossRemoved = layout.bosses.map(() => 0);
    const wavesDone = layout.ambushes.map(() => 0);
    let revives = 0;
    let finishFrame = -1;
    const step = (level.max_speed_px_s / rep.sampleHz) * 1.25 + 4;
    players.forEach((p, i) => {
        const who = `joueur ${i + 1}`;
        let deaths = 0;
        let mine = 0;
        let last = 0;
        const jumps = new Set<number>();
        for (const e of p.events) {
            if (e.frame < last || e.frame >= p.frames.length) {
                reasons.push(`événements incohérents (${who})`);
                break;
            }
            last = e.frame;
            if (e.type === EventType.Death) ++deaths;
            else if (e.type === EventType.Respawn) reasons.push(`une seule vie : retour à un point de passage impossible (${who})`);
            else if (e.type === Revive) {
                ++mine;
                jumps.add(e.frame);
            } else if (e.type === Regroup || e.type === Return || e.type === EventType.Teleport) jumps.add(e.frame);
            if (i === 0) {
                if (e.type === EventType.Finish) finishFrame = e.frame;
                else if (e.type === 12 /* BossHit */) {
                    if (e.arg >= bossRemoved.length || e.value > 1000) reasons.push("boss inconnu");
                    else bossRemoved[e.arg] = Math.max(bossRemoved[e.arg]!, e.value);
                } else if (e.type === 13 /* WaveCleared */) {
                    if (e.arg >= wavesDone.length || e.value > layout.ambushes[e.arg]!.waves) reasons.push("embuscade inconnue");
                    else wavesDone[e.arg] = Math.max(wavesDone[e.arg]!, e.value);
                }
            }
        }
        revives += mine;
        // One life each: one fall, plus one per revival of that player.
        if (deaths > 1 + mine) reasons.push(`plusieurs vies utilisées (${who})`);
        // Trajectory: from the start (side by side), inside the level, no impossible move but at
        // its revivals, its returns to the team and its teleports.
        const first = p.frames[0];
        if (first && Math.hypot(first.x - level.spawn_x, first.y - level.spawn_y) > 96 + 3 * 18) reasons.push(`départ éloigné du point de départ (${who})`);
        let tooFast = 0;
        for (let k = 0; k < p.frames.length; ++k) {
            const f = p.frames[k]!;
            if (!Number.isFinite(f.x) || !Number.isFinite(f.y) || f.x < -512 || f.y < -1024 || f.x > level.bounds_w + 512 || f.y > level.bounds_h + 1024) {
                reasons.push(`position hors du niveau (${who})`);
                break;
            }
            if (k > 0 && !(jumps.has(k) || jumps.has(k - 1) || jumps.has(k + 1))) {
                const q = p.frames[k - 1]!;
                if (Math.hypot(f.x - q.x, f.y - q.y) > step + 200) ++tooFast;
            }
        }
        if (tooFast > 2) reasons.push(`déplacements impossibles (${who})`);
    });
    if (team && revives !== team.revives) reasons.push("réanimations incohérentes");
    if (revives > MAX_REVIVES) reasons.push("plus de 2 réanimations");
    if (claim.bosses.length !== bossRemoved.length || claim.bosses.some((v, i) => Math.abs(v - bossRemoved[i]!) > 10))
        reasons.push("dégâts aux boss différents du replay");
    if (claim.waves.length !== wavesDone.length || claim.waves.some((v, i) => v !== wavesDone[i])) reasons.push("vagues différentes du replay");
    let finished = false;
    if (claim.finished) {
        const m = 160;
        const inExit = (f: ReplayFrame | undefined) =>
            !!f && f.x >= level.exit_x - m && f.x <= level.exit_x + level.exit_w + m && f.y >= level.exit_y - m && f.y <= level.exit_y + level.exit_h + m;
        if (finishFrame < 0 || !players.some((p) => inExit(p.frames[p.frames.length - 1]))) reasons.push("fin du niveau absente du replay");
        else finished = true;
    }
    // The most advanced player's route.
    const route = Math.max(...players.map((p) => routeReached(layout.route, p.frames)));
    return {
        reasons: [...new Set(reasons)],
        route,
        bossRemoved: bossRemoved.map((v) => v / 1000),
        wavesDone,
        finished,
        revives,
        incomplete: team?.incomplete ?? false,
    };
}

/** POST /coop/runs {kind, level_id, version?, level_hash?, attempt_id, time_ms, finished, bosses, waves, mode, players} */
export async function submitRun(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.raceSubmit, "player:" + me);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["kind", "level_id", "version", "level_hash", "attempt_id", "time_ms", "finished", "bosses", "waves", "mode", "players"]);
    const kind = v.oneOf("kind", ["community", "official"] as const)!;
    const levelId = v.str("level_id", { min: 1, max: 64 })!;
    const version = v.int("version", { min: 1, max: 100000, optional: true }) ?? null;
    const levelHash = v.str("level_hash", { min: 64, max: 64, pattern: HASH_RE, optional: true }) ?? null;
    const attemptId = v.str("attempt_id", { min: 36, max: 36, pattern: ID_RE })!;
    const timeMs = v.int("time_ms", { min: 0, max: 7200 * 1000 })!;
    const mode = v.oneOf("mode", ["online", "lan", "same_pc"] as const)!;
    if (typeof body.finished !== "boolean") v.fail("finished", "booléen attendu");
    const ints = (k: string, max: number) => {
        const a = body[k];
        if (!Array.isArray(a) || a.length > 64 || !a.every((n) => Number.isInteger(n) && n >= 0 && n <= max)) {
            v.fail(k, "liste d'entiers attendue");
            return [];
        }
        return a as number[];
    };
    const claim: TeamClaim = { finished: body.finished === true, bosses: ints("bosses", 1000), waves: ints("waves", 99) };
    const players = body.players;
    if (!Array.isArray(players) || players.length < 2 || players.length > 3 || !players.every((p) => typeof p === "string" && ID_RE.test(p)) ||
        new Set(players).size !== players.length)
        v.fail("players", "2 ou 3 joueurs distincts");
    v.done();
    const ids = players as string[];
    if (!ids.includes(me)) throw Errors.forbidden("NOT_IN_TEAM", "Vous n'êtes pas dans cette équipe.");
    const found = await ctx.env.DB.prepare(`SELECT COUNT(*) AS n FROM players WHERE id IN (SELECT value FROM json_each(?1)) AND account_status = 'active'`)
        .bind(JSON.stringify(ids))
        .first<{ n: number }>();
    if ((found?.n ?? 0) !== ids.length) throw Errors.validation([{ field: "players", message: "joueur inconnu" }]);
    if (mode === "online") {
        // Online, the relay noted who played: exactly these players, in this order.
        const s = await ctx.env.DB.prepare("SELECT players FROM coop_sessions WHERE attempt_id = ?1").bind(attemptId).first<{ players: string }>();
        if (!s || s.players !== JSON.stringify(ids)) throw Errors.conflict("SESSION_MISMATCH", "Cette partie en ligne n'a pas été jouée par cette équipe.");
    }
    const board = await loadBoard(ctx, kind, levelId, version, levelHash);
    const id = crypto.randomUUID();
    const now = nowIso();
    try {
        await ctx.env.DB.batch([
            ctx.env.DB.prepare(
                `INSERT INTO coop_runs (id, board, size, team_key, attempt_id, submitter_id, mode, claim, time_ms, created_at)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)`,
            ).bind(id, board.key, ids.length, [...ids].sort().join("+"), attemptId, me, mode, JSON.stringify(claim), timeMs, now),
            ...ids.map((p, seat) => ctx.env.DB.prepare("INSERT INTO coop_run_members (run_id, seat, player_id) VALUES (?1, ?2, ?3)").bind(id, seat, p)),
            ctx.env.DB.prepare("INSERT OR IGNORE INTO coop_confirms (attempt_id, player_id, created_at) VALUES (?1, ?2, ?3)").bind(attemptId, me, now),
        ]);
    } catch (e) {
        if (String(e).includes("UNIQUE")) throw Errors.conflict("RUN_EXISTS", "Cette tentative a déjà été envoyée.");
        throw e;
    }
    return json(ctx, { run: { id, status: "PENDING" }, upload: `/api/v1/coop/runs/${id}/replay` }, { status: 201 });
}

/** PUT /coop/runs/{runId}/replay : the team replay; the server checks it and computes the score. */
export async function uploadRunReplay(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.raceSubmit, "player:" + me);
    const run = await ctx.env.DB.prepare("SELECT * FROM coop_runs WHERE id = ?1").bind(ctx.params.runId ?? "").first<Record<string, unknown>>();
    if (!run || run.submitter_id !== me) throw Errors.notFound("RUN_NOT_FOUND", "Tentative introuvable.");
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
    const claim = JSON.parse(String(run.claim)) as TeamClaim;
    const a = analyzeTeam(rep, String(run.attempt_id), Number(run.time_ms), Number(run.size), claim, board.layout, board.level);
    if (run.mode === "online") {
        // The inputs must be those the relay gave, for exactly as many steps.
        const s = await ctx.env.DB.prepare("SELECT ticks, inputs_hash FROM coop_sessions WHERE attempt_id = ?1")
            .bind(run.attempt_id)
            .first<{ ticks: number | null; inputs_hash: string | null }>();
        const hash = await teamInputsHash(rep);
        if (!s || s.ticks === null || s.ticks !== rep.inputs?.ticks || s.inputs_hash !== hash) a.reasons.push("entrées différentes de celles relayées en ligne");
    }
    const ok = a.reasons.length === 0;
    const score = raceScore(board.layout, a.route, a.bossRemoved, a.wavesDone, a.finished);
    const key = `coop/${board.key.replace(/:/g, "_")}/${run.id}.cprz`;
    await ctx.env.STORAGE.put(key, data, { httpMetadata: { contentType: "application/octet-stream" } });
    const now = nowIso();
    const detail = {
        bosses: board.layout.bosses.map((b, i) => ({ name: b.name, removed: Math.round((a.bossRemoved[i] ?? 0) * 1000) / 1000 })),
        ambushes: board.layout.ambushes.map((x, i) => ({ waves: x.waves, done: a.wavesDone[i] ?? 0 })),
    };
    await ctx.env.DB.prepare(
        `UPDATE coop_runs SET replay_key = ?2, status = ?3, score = ?4, route = ?5, revives = ?6, incomplete = ?7, finished = ?8,
           detail = ?9, reasons = ?10, verified_at = ?11 WHERE id = ?1`,
    )
        .bind(run.id, key, ok ? "VERIFIED" : "REJECTED", score, a.route, a.revives, a.incomplete ? 1 : 0, a.finished ? 1 : 0, JSON.stringify(detail),
            ok ? null : JSON.stringify(a.reasons.slice(0, 8)), ok ? now : null)
        .run();
    if (ok) await refreshTeamClans(ctx, String(run.id));
    return json(ctx, { run: { id: run.id, status: ok ? "VERIFIED" : "REJECTED", score, finished: a.finished, reasons: ok ? [] : a.reasons.slice(0, 8) } });
}

/** POST /coop/attempts/{attemptId}/confirm : I played this team attempt (with my own account). */
export async function confirm(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.raceSubmit, "player:" + auth.player.id);
    const attemptId = ctx.params.attemptId ?? "";
    if (!ID_RE.test(attemptId)) throw Errors.notFound("ATTEMPT_NOT_FOUND", "Tentative introuvable.");
    await ctx.env.DB.prepare("INSERT OR IGNORE INTO coop_confirms (attempt_id, player_id, created_at) VALUES (?1, ?2, ?3)")
        .bind(attemptId, auth.player.id, nowIso())
        .run();
    // The last confirmation of a verified attempt ranks it: its clan's points follow.
    const run = await ctx.env.DB.prepare("SELECT id FROM coop_runs WHERE attempt_id = ?1 AND status = 'VERIFIED'").bind(attemptId).first<{ id: string }>();
    if (run) await refreshTeamClans(ctx, run.id);
    return json(ctx, { confirmed: attemptId });
}

/**
 * A team attempt verified or confirmed: the tag of the clan of all its players (shown on the
 * Duo / Trio boards, null when they are not all in one clan) and the points of their clans
 * (clans.ts: a team counts for a clan once every player is a member and confirmed). Never fails
 * the caller.
 */
async function refreshTeamClans(ctx: Ctx, runId: string): Promise<void> {
    try {
        const db = ctx.env.DB;
        await db
            .prepare(
                `UPDATE coop_runs SET clan_tag = (SELECT c.tag FROM clans c
                   WHERE (SELECT COUNT(*) FROM coop_run_members cm JOIN clan_members m ON m.player_id = cm.player_id AND m.clan_id = c.id
                           WHERE cm.run_id = coop_runs.id) = coop_runs.size)
                 WHERE id = ?1`,
            )
            .bind(runId)
            .run();
        const clans = await db
            .prepare("SELECT DISTINCT m.clan_id FROM coop_run_members cm JOIN clan_members m ON m.player_id = cm.player_id WHERE cm.run_id = ?1")
            .bind(runId)
            .all<{ clan_id: string }>();
        for (const c of clans.results) await db.batch(clanPointsStatements(db, c.clan_id));
    } catch (e) {
        log("error", "coop_clan_points_failed", { error: e instanceof Error ? e.message : String(e) });
    }
}

/** The best verified attempt of every team (all members confirmed), ranked. */
async function ranking(ctx: Ctx, keys: string[], size: number) {
    const rows = await ctx.env.DB.prepare(
        `WITH ok AS (
           SELECT r.* FROM coop_runs r
           WHERE r.board ${IN_KEYS(1)} AND r.size = ?2 AND r.status = 'VERIFIED'
             AND NOT EXISTS (SELECT 1 FROM coop_run_members m WHERE m.run_id = r.id
                             AND NOT EXISTS (SELECT 1 FROM coop_confirms c WHERE c.attempt_id = r.attempt_id AND c.player_id = m.player_id))),
         best AS (SELECT ok.*, ROW_NUMBER() OVER (PARTITION BY team_key ORDER BY score DESC, incomplete ASC, revives ASC, verified_at ASC) AS n FROM ok)
         SELECT id, team_key, clan_tag, score, route, revives, incomplete, finished, detail, time_ms, verified_at, replay_key IS NOT NULL AS has_replay
         FROM best WHERE n = 1 ORDER BY score DESC, incomplete ASC, revives ASC, verified_at ASC LIMIT 100`,
    )
        .bind(JSON.stringify(keys), size)
        .all<Record<string, unknown>>();
    const ids = rows.results.map((r) => String(r.id));
    const members = ids.length
        ? await ctx.env.DB.prepare(
              `SELECT m.run_id, m.seat, p.id, p.username, p.avatar FROM coop_run_members m JOIN players p ON p.id = m.player_id
               WHERE m.run_id IN (SELECT value FROM json_each(?1)) ORDER BY m.run_id, m.seat`,
          )
              .bind(JSON.stringify(ids))
              .all<{ run_id: string; seat: number; id: string; username: string; avatar: string }>()
        : { results: [] };
    return rows.results.map((r) => ({ ...r, players: members.results.filter((m) => m.run_id === r.id).map((m) => ({ id: m.id, username: m.username, avatar: m.avatar })) }) as Record<string, unknown> & { players: { id: string; username: string; avatar: string }[] });
}

/** GET /coop/board?kind=&level_id=&version=|level_hash=&size=2|3 : the Duo or Trio ranking. */
export async function getBoard(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const me = viewer?.player.id ?? "";
    const q = ctx.url.searchParams;
    const size = Number(q.get("size") ?? "2");
    if (size !== 2 && size !== 3) throw Errors.validation([{ field: "size", message: "2 (Duo) ou 3 (Trio)" }]);
    const ver = q.get("version");
    const board = await loadBoard(ctx, q.get("kind") ?? "", q.get("level_id") ?? "", ver ? Number(ver) : null, q.get("level_hash"));
    const rows = await ranking(ctx, board.keys, size);
    const pending = me
        ? await ctx.env.DB.prepare(
              `SELECT COUNT(*) AS n FROM coop_runs r JOIN coop_run_members m ON m.run_id = r.id
               WHERE r.board ${IN_KEYS(1)} AND r.size = ?2 AND m.player_id = ?3 AND r.status = 'PENDING'`,
          )
              .bind(JSON.stringify(board.keys), size, me)
              .first<{ n: number }>()
        : null;
    return json(ctx, {
        race: { kind: board.kind, level_id: board.levelId, version: board.version, title: board.title, bosses: board.layout.bosses.map((b) => b.name), size },
        ranking: rows.map((r, i) => ({
            rank: i + 1,
            run_id: r.id,
            players: r.players,
            clan_tag: r.clan_tag ?? null,
            score: r.score,
            route: r.route,
            revives: r.revives,
            incomplete: r.incomplete === 1,
            finished: r.finished === 1,
            detail: JSON.parse(String(r.detail ?? "null")),
            time_ms: r.time_ms,
            at: r.verified_at,
            has_replay: r.has_replay === 1,
            me: r.players.some((p) => p.id === me),
        })),
        my_pending: pending?.n ?? 0,
    });
}

/** GET /coop/runs/{runId}/replay : the replay of a ranked team attempt (its players: always). */
export async function downloadRunReplay(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const id = ctx.params.runId ?? "";
    if (!ID_RE.test(id)) throw Errors.notFound("RUN_NOT_FOUND", "Tentative introuvable.");
    const r = await ctx.env.DB.prepare("SELECT status, replay_key FROM coop_runs WHERE id = ?1").bind(id).first<{ status: string; replay_key: string | null }>();
    const member = viewer
        ? await ctx.env.DB.prepare("SELECT 1 AS x FROM coop_run_members WHERE run_id = ?1 AND player_id = ?2").bind(id, viewer.player.id).first()
        : null;
    if (!r || (r.status !== "VERIFIED" && !member)) throw Errors.notFound("RUN_NOT_FOUND", "Tentative introuvable.");
    if (!r.replay_key) throw Errors.notFound("REPLAY_NOT_FOUND", "Aucun replay pour cette tentative.");
    const obj = await ctx.env.STORAGE.get(r.replay_key);
    if (!obj) throw Errors.notFound("REPLAY_NOT_FOUND", "Fichier de replay absent.");
    return new Response(obj.body, {
        headers: { "content-type": "application/octet-stream", "content-length": String(obj.size), "cache-control": "private, max-age=3600", "x-content-type-options": "nosniff" },
    });
}
