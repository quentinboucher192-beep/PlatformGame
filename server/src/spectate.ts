// Live spectating between friends (2.36, docs/online/API.md §8.7 bis).
//
// A player who allows it (« Mes amis peuvent me regarder jouer », players.spectate_allowed,
// on by default) streams the run they are playing: the game sends its header when the run
// starts (level, version and fingerprint, seed, keys of the powers, challenge) and the inputs
// of the simulation steps about every half second, with the state digests of the replay
// format. A friend's game re-simulates the run from them, exactly like an exact replay.
//
// One Durable Object per player (SpectateRoom) keeps the current run in MEMORY only: never
// written to D1, R2 or the object's storage. It is bounded (30 minutes of play, 3 MB of
// inputs) and forgotten one minute after the run ends, or two minutes after the game stops
// sending. Only the setting is stored (players.spectate_allowed).
//
// Who watches is never told to the player watched: no counter, no list, no log they can
// read. The room does not even know who reads: the Worker checks the friendship (accepted,
// no block either way), the setting and the presence's visibility, then asks the room for
// the data without saying who asks.

import { DurableObject } from "cloudflare:workers";
import { requireAuth } from "./auth";
import { challengeForSpectate } from "./challenges";
import { ApiError, Errors, json, nowIso, readJsonObject, type Bindings, type Ctx } from "./http";
import type { PlayerRow } from "./model";
import { HASH_RE, LEVEL_ID_RE, SEMVER_RE, UUID_RE, Validator } from "./validate";

const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const SEED_RE = /^[0-9a-f]{16}$/;
const CHALLENGE_ID_RE = /^(daily|weekly|monthly)-[0-9W-]{4,10}$/;
const BASE64_RE = /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/;

/** One run of identical inputs: u16 repeat, u32 held, u32 pressed, u32 released, i8 moveX, i8 moveY, u8 flags, u8 choice (replay format 3). */
export const RUN_BYTES = 18;
/** Recorded actions: MoveLeft .. PowerSlot8 (InputFrame::kActions = 21 bits). */
const ACTION_MASK = (1 << 21) - 1;
/** Steps per second of the simulation. */
const TICK_HZ = 120;
/** Steps of one chunk at most (20 s: a game catching up after a cut sends bigger chunks). */
export const MAX_CHUNK_FRAMES = TICK_HZ * 20;
/** A run is followed for 30 minutes of play at most (bounded memory per room). */
export const MAX_RUN_FRAMES = TICK_HZ * 60 * 30;
export const MAX_RUN_BYTES = 3 * 1024 * 1024;
export const MAX_RUN_CHUNKS = 6000;
/** Posts of one player per window (about 2 per second; more: 429). */
export const POSTS_PER_WINDOW = 25;
export const POSTS_WINDOW_MS = 10_000;
/** Nothing received for this long: the run reads « connexion perdue » (« stale »). */
export const STALE_MS = 30_000;
/** Then it is forgotten. */
export const DROP_MS = 120_000;
/** An ended run stays readable for this long (the viewers play its last inputs). */
export const ENDED_KEEP_MS = 60_000;
/** Chunks of one read at most (more: « more: true », the game reads again at once). */
const READ_CHUNKS = 240;
const READ_BYTES = 256 * 1024;

const END_REASONS = ["finished", "failed", "quit"] as const;
type EndReason = (typeof END_REASONS)[number] | "too_long";

export interface SpectateHeader {
    run_id: string;
    /** Changes at every start received (a game that sends its run again after a cut). */
    stream: string;
    kind: "campaign" | "community" | "challenge";
    level: { source: "campaign" | "community"; id: string; version: number; hash: string; title: string };
    challenge: { id: string; kind: string; rules: string } | null;
    seed: string;
    keys_per_slot: boolean;
    sim_version: number;
    game_version: string;
    started_at: string;
}

interface Chunk {
    seq: number;
    from_frame: number;
    count: number;
    runs: string;
    checks: [number, string][];
}

interface Run {
    header: SpectateHeader;
    chunks: Chunk[];
    seq: number;
    frames: number;
    bytes: number;
    lastCheck: number;
    updatedAt: number;
    ended: EndReason | null;
    endedAt: number;
}

// ------------------------------------------------------------------ inputs

function decodeBase64(s: string): Uint8Array | null {
    if (s.length % 4 !== 0 || !BASE64_RE.test(s)) return null;
    try {
        const bin = atob(s);
        const out = new Uint8Array(bin.length);
        for (let i = 0; i < bin.length; ++i) out[i] = bin.charCodeAt(i);
        return out;
    } catch {
        return null;
    }
}

/**
 * Checks the inputs of a chunk (never trusted): « count » steps exactly, each run repeated at
 * least once, only the recorded actions, sticks within -127..127, the known flags (skip a
 * cinematic) and power choices (1..8, 255 = leave it). Returns a message or null.
 */
export function invalidRuns(runs: string, count: number): string | null {
    if (count === 0) return runs === "" ? null : "aucune entrée attendue";
    const bytes = decodeBase64(runs);
    if (!bytes || bytes.length === 0 || bytes.length % RUN_BYTES !== 0) return "entrées illisibles";
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    let total = 0;
    for (let at = 0; at < bytes.length; at += RUN_BYTES) {
        const repeat = view.getUint16(at, true);
        if (repeat === 0) return "plage vide";
        for (const k of [2, 6, 10]) if ((view.getUint32(at + k, true) & ~ACTION_MASK) !== 0) return "action inconnue";
        const mx = view.getInt8(at + 14),
            my = view.getInt8(at + 15),
            flags = view.getUint8(at + 16),
            choice = view.getUint8(at + 17);
        if (mx < -127 || my < -127) return "direction invalide";
        if ((flags & ~1) !== 0) return "drapeau inconnu";
        if (choice > 8 && choice !== 255) return "choix de pouvoir invalide";
        total += repeat;
        if (total > count) return "plus d'entrées qu'annoncé";
    }
    return total === count ? null : "moins d'entrées qu'annoncé";
}

// -------------------------------------------------------------------- room

type RoomAnswer = Record<string, unknown>;

export class SpectateRoom extends DurableObject<Bindings> {
    private run: Run | null = null;
    private budget = { start: 0, count: 0 };

    async fetch(request: Request): Promise<Response> {
        const url = new URL(request.url);
        this.expire();
        try {
            switch (url.pathname) {
                case "/start":
                    return Response.json(await this.start((await request.json()) as SpectateHeader));
                case "/chunk":
                    return Response.json(this.chunk((await request.json()) as { run_id: string } & Chunk));
                case "/end":
                    return Response.json(this.end((await request.json()) as { run_id: string; reason: EndReason }));
                case "/read":
                    return Response.json(this.read(url.searchParams.get("stream") ?? "", Number(url.searchParams.get("after") ?? "0")));
                case "/status":
                    return Response.json(this.status());
                case "/clear":
                    this.run = null;
                    return Response.json({ cleared: true });
                default:
                    return Response.json({ error: "unknown" }, { status: 404 });
            }
        } catch (e) {
            return Response.json({ error: e instanceof Error ? e.message : String(e) }, { status: 500 });
        }
    }

    /** Forgets the run after its time (also done by the alarm when nobody asks anything). */
    async alarm(): Promise<void> {
        this.expire();
        if (this.run) await this.ctx.storage.setAlarm(Date.now() + STALE_MS);
    }

    private expire(): void {
        const r = this.run;
        if (!r) return;
        const now = Date.now();
        if ((r.ended && now - r.endedAt > ENDED_KEEP_MS) || now - r.updatedAt > DROP_MS) this.run = null;
    }

    /** Counts one more post of the player: 0, or the seconds to wait over the budget (about 2 per second). */
    private spend(): number {
        const now = Date.now();
        if (now - this.budget.start >= POSTS_WINDOW_MS) this.budget = { start: now, count: 0 };
        if (this.budget.count >= POSTS_PER_WINDOW) return Math.max(1, Math.ceil((this.budget.start + POSTS_WINDOW_MS - now) / 1000));
        this.budget.count += 1;
        return 0;
    }

    private async start(header: SpectateHeader): Promise<RoomAnswer> {
        const wait = this.spend();
        if (wait) return { error: "throttled", retry_after: wait };
        const now = Date.now();
        this.run = { header: { ...header, stream: crypto.randomUUID() }, chunks: [], seq: 0, frames: 0, bytes: 0, lastCheck: 0, updatedAt: now, ended: null, endedAt: 0 };
        await this.ctx.storage.setAlarm(now + STALE_MS);
        return { accepted: true, run_id: header.run_id };
    }

    private chunk(c: { run_id: string } & Chunk): RoomAnswer {
        const wait = this.spend();
        if (wait) return { error: "throttled", retry_after: wait };
        const r = this.run;
        if (!r || r.header.run_id !== c.run_id) return { error: "resync", seq: 0, frames: 0 };
        if (r.ended === "too_long") return { error: "too_long" };
        // The same chunk again (its answer was lost): accepted once.
        if (c.seq <= r.seq) {
            const same = r.chunks.find((k) => k.seq === c.seq);
            if (same && same.from_frame === c.from_frame && same.count === c.count) return { accepted: true, seq: r.seq, frames: r.frames };
            return { error: "resync", seq: r.seq, frames: r.frames };
        }
        if (c.seq !== r.seq + 1 || c.from_frame !== r.frames || r.ended) return { error: "resync", seq: r.seq, frames: r.frames };
        if (c.checks.some(([t]) => t <= r.lastCheck)) return { error: "resync", seq: r.seq, frames: r.frames };
        if (r.frames + c.count > MAX_RUN_FRAMES || r.bytes + c.runs.length > MAX_RUN_BYTES || r.chunks.length >= MAX_RUN_CHUNKS) {
            // Too long to be followed: the viewers are told, nothing more is kept.
            r.ended = "too_long";
            r.endedAt = Date.now();
            r.updatedAt = r.endedAt;
            return { error: "too_long" };
        }
        r.chunks.push({ seq: c.seq, from_frame: c.from_frame, count: c.count, runs: c.runs, checks: c.checks });
        r.seq = c.seq;
        r.frames += c.count;
        r.bytes += c.runs.length;
        if (c.checks.length) r.lastCheck = c.checks[c.checks.length - 1]![0];
        r.updatedAt = Date.now();
        return { accepted: true, seq: r.seq, frames: r.frames };
    }

    private end(e: { run_id: string; reason: EndReason }): RoomAnswer {
        const wait = this.spend();
        if (wait) return { error: "throttled", retry_after: wait };
        const r = this.run;
        if (r && r.header.run_id === e.run_id && !r.ended) {
            r.ended = e.reason;
            r.endedAt = Date.now();
            r.updatedAt = r.endedAt;
        }
        return { ended: true };
    }

    /** What a viewer reads: the header and the chunks after « after » (all of them for another stream). */
    private read(stream: string, after: number): RoomAnswer {
        const r = this.run;
        if (!r) return { live: false };
        const from = stream === r.header.stream && Number.isInteger(after) && after > 0 ? after : 0;
        const chunks: Chunk[] = [];
        let bytes = 0;
        let more = false;
        for (const c of r.chunks) {
            if (c.seq <= from) continue;
            if (chunks.length >= READ_CHUNKS || (chunks.length > 0 && bytes + c.runs.length > READ_BYTES)) {
                more = true;
                break;
            }
            chunks.push(c);
            bytes += c.runs.length;
        }
        const idle = Date.now() - r.updatedAt;
        return {
            live: true,
            run: r.header,
            chunks,
            more,
            last_seq: r.seq,
            frames: r.frames,
            ended: r.ended,
            stale: !r.ended && idle > STALE_MS,
        };
    }

    /** For the friends list: is a run being played now (not ended, not silent)? */
    private status(): RoomAnswer {
        const r = this.run;
        if (!r || r.ended || Date.now() - r.updatedAt > STALE_MS) return { live: false };
        return { live: true, level_title: r.header.level.title, kind: r.header.kind, level_source: r.header.level.source, started_at: r.header.started_at };
    }
}

// -------------------------------------------------------------- Worker side

function room(env: Bindings, playerId: string) {
    return env.SPECTATE_ROOMS.get(env.SPECTATE_ROOMS.idFromName(playerId));
}

async function ask(env: Bindings, playerId: string, path: string, body?: unknown): Promise<RoomAnswer> {
    const res = await room(env, playerId).fetch("https://spectate" + path, body === undefined ? {} : { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify(body) });
    const data = (await res.json()) as RoomAnswer;
    if (!res.ok) throw new Error("spectate room: " + String(data.error ?? res.status));
    return data;
}

/** The player's run, forgotten at once (the setting was turned off, the account deleted). */
export async function clearSpectateRoom(env: Bindings, playerId: string): Promise<void> {
    await ask(env, playerId, "/clear", {});
}

/** Whether « p » may be watched by friends at all: the setting and a visible presence. */
export function spectatable(p: Pick<PlayerRow, "spectate_allowed" | "presence_visible">): boolean {
    return p.spectate_allowed !== 0 && p.presence_visible !== 0;
}

/** For GET /friends: the friends' runs being played now, by player id (one room each). */
export async function playingNow(env: Bindings, ids: string[]): Promise<Map<string, RoomAnswer>> {
    const out = new Map<string, RoomAnswer>();
    const answers = await Promise.all(ids.map((id) => ask(env, id, "/status").catch(() => ({ live: false }) as RoomAnswer)));
    ids.forEach((id, i) => {
        const a = answers[i]!;
        if (a.live === true) out.set(id, { level_title: a.level_title, kind: a.kind, level_source: a.level_source, started_at: a.started_at });
    });
    return out;
}

function throttled(data: RoomAnswer): never {
    const retry = Number(data.retry_after) || 1;
    throw new ApiError(429, "RATE_LIMITED", "Trop d'envois, réessayez dans un instant.", { retry_after_seconds: retry }, { "retry-after": String(retry) });
}

/** The level of a run as the server knows it: its title comes from here, never from the game. */
async function verifiedLevel(ctx: Ctx, source: "campaign" | "community", id: string, version: number, hash: string): Promise<string> {
    if (source === "campaign") {
        const row = await ctx.env.DB.prepare(
            `SELECT l.name FROM levels l JOIN level_versions v ON v.level_id = l.id
             WHERE l.id = ?1 AND v.version = ?2 AND v.content_hash = ?3 AND v.compatibility_status <> 'disabled'`,
        )
            .bind(id, version, hash)
            .first<{ name: string }>();
        if (!row) throw Errors.conflict("LEVEL_MISMATCH", "Cette version du niveau n'est pas connue du serveur.");
        return row.name;
    }
    const row = await ctx.env.DB.prepare(
        `SELECT l.title FROM community_levels l JOIN community_level_versions v ON v.level_id = l.id
         WHERE l.id = ?1 AND l.status = 'published' AND v.version = ?2 AND v.level_hash = ?3 AND v.status IN ('published', 'superseded')`,
    )
        .bind(id, version, hash)
        .first<{ title: string }>();
    if (!row) throw Errors.conflict("LEVEL_MISMATCH", "Cette version du niveau n'est pas publiée.");
    return row.title;
}

/**
 * POST /spectate/broadcast (the player's own room):
 *   {action: "start", run_id, level: {source, id, version, hash}, challenge_id?, seed, keys_per_slot, sim_version, game_version}
 *   {action: "chunk", run_id, seq, from_frame, count, runs, checks?}
 *   {action: "end", run_id, reason}
 * The answer never says anything about who watches.
 */
export async function broadcast(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player;
    if (!spectatable(me)) throw Errors.forbidden("SPECTATE_DISABLED", "Le direct est désactivé dans vos paramètres en ligne.");
    const body = await readJsonObject(ctx, 64 * 1024);
    const action = body.action;
    if (action === "start") {
        const v = new Validator(body, ["action", "run_id", "level", "challenge_id", "seed", "keys_per_slot", "sim_version", "game_version"]);
        const runId = v.str("run_id", { min: 36, max: 36, pattern: UUID_RE });
        const challengeId = v.str("challenge_id", { max: 24, pattern: CHALLENGE_ID_RE, optional: true });
        const seed = v.str("seed", { min: 16, max: 16, pattern: SEED_RE });
        const keysPerSlot = v.bool("keys_per_slot");
        const simVersion = v.int("sim_version", { min: 1, max: 1000 });
        const gameVersion = v.str("game_version", { max: 16, pattern: SEMVER_RE });
        const raw = body.level;
        let level: { source: "campaign" | "community"; id: string; version: number; hash: string } | undefined;
        if (!raw || typeof raw !== "object" || Array.isArray(raw)) v.fail("level", "objet attendu");
        else {
            const lv = new Validator(raw as Record<string, unknown>, ["source", "id", "version", "hash"]);
            const source = lv.oneOf("source", ["campaign", "community"] as const);
            const id = lv.str("id", { min: 1, max: 64, pattern: source === "community" ? ID_RE : LEVEL_ID_RE });
            const version = lv.int("version", { min: 1, max: 1_000_000 });
            const hash = lv.str("hash", { min: 64, max: 64, pattern: HASH_RE });
            for (const e of lv.errors) v.fail("level." + e.field, e.message);
            if (source && id && version !== undefined && hash) level = { source, id, version, hash };
        }
        v.done();
        let kind: SpectateHeader["kind"] = level!.source;
        let challenge: SpectateHeader["challenge"] = null;
        let title: string;
        if (challengeId) {
            // The rules come from the server's challenge (begun, never a future one).
            const c = await challengeForSpectate(ctx, challengeId);
            if (!c) throw Errors.notFound("CHALLENGE_NOT_FOUND", "Défi introuvable.");
            if (c.level.source !== level!.source || c.level.id !== level!.id || c.level.version !== level!.version || c.level.hash !== level!.hash)
                throw Errors.conflict("LEVEL_MISMATCH", "Ce n'est pas le niveau du défi.");
            kind = "challenge";
            challenge = { id: challengeId, kind: c.kind, rules: c.rules };
            title = c.level.title;
        } else {
            title = await verifiedLevel(ctx, level!.source, level!.id, level!.version, level!.hash);
        }
        const header: SpectateHeader = {
            run_id: runId!,
            stream: "",
            kind,
            level: { ...level!, title },
            challenge,
            seed: seed!,
            keys_per_slot: keysPerSlot!,
            sim_version: simVersion!,
            game_version: gameVersion!,
            started_at: nowIso(),
        };
        const data = await ask(ctx.env, me.id, "/start", header);
        if (data.error === "throttled") throttled(data);
        return json(ctx, { accepted: true, run_id: runId });
    }
    if (action === "chunk") {
        const v = new Validator(body, ["action", "run_id", "seq", "from_frame", "count", "runs", "checks"]);
        const runId = v.str("run_id", { min: 36, max: 36, pattern: UUID_RE });
        const seq = v.int("seq", { min: 1, max: MAX_RUN_CHUNKS });
        const from = v.int("from_frame", { min: 0, max: MAX_RUN_FRAMES });
        const count = v.int("count", { min: 0, max: MAX_CHUNK_FRAMES });
        const runs = v.str("runs", { max: Math.ceil((MAX_CHUNK_FRAMES * RUN_BYTES) / 3) * 4 });
        const checks: [number, string][] = [];
        if (body.checks !== undefined) {
            if (!Array.isArray(body.checks) || body.checks.length > Math.floor((count ?? 0) / 60) + 2) v.fail("checks", "liste de contrôles invalide");
            else {
                let prev = from ?? 0;
                for (const c of body.checks) {
                    const ok =
                        Array.isArray(c) && c.length === 2 && Number.isSafeInteger(c[0]) && typeof c[1] === "string" && SEED_RE.test(c[1]) &&
                        (c[0] as number) > prev && (c[0] as number) <= (from ?? 0) + (count ?? 0);
                    if (!ok) {
                        v.fail("checks", "contrôle hors de ce morceau");
                        break;
                    }
                    prev = c[0] as number;
                    checks.push([c[0] as number, c[1] as string]);
                }
            }
        }
        if (runs !== undefined && count !== undefined) {
            const problem = invalidRuns(runs, count);
            if (problem) v.fail("runs", problem);
        }
        v.done();
        const data = await ask(ctx.env, me.id, "/chunk", { run_id: runId, seq, from_frame: from, count, runs, checks });
        if (data.error === "throttled") throttled(data);
        if (data.error === "too_long") throw new ApiError(413, "SPECTATE_RUN_TOO_LONG", "Partie trop longue pour être suivie en direct.");
        if (data.error === "resync")
            throw Errors.conflict("SPECTATE_RESYNC", "Le direct reprend depuis le début de la partie.", { seq: data.seq, frames: data.frames });
        return json(ctx, { accepted: true, seq: data.seq, frames: data.frames });
    }
    if (action === "end") {
        const v = new Validator(body, ["action", "run_id", "reason"]);
        const runId = v.str("run_id", { min: 36, max: 36, pattern: UUID_RE });
        const reason = v.oneOf("reason", END_REASONS);
        v.done();
        const data = await ask(ctx.env, me.id, "/end", { run_id: runId, reason });
        if (data.error === "throttled") throttled(data);
        return json(ctx, { ended: true });
    }
    throw Errors.validation([{ field: "action", message: "valeur attendue : start, chunk, end" }]);
}

/**
 * GET /spectate/{playerId}?stream=&after=: the run a friend is playing (header and the chunks
 * after « after » of the same stream). 403 unless both are friends (accepted, no block either
 * way) and the friend allows it (setting on, presence visible); the same answer whatever the
 * reason. « {live: false} » when no run is being played.
 */
export async function watch(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const other = ctx.params.playerId!;
    if (!ID_RE.test(other)) throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    const stream = ctx.url.searchParams.get("stream") ?? "";
    const afterText = ctx.url.searchParams.get("after") ?? "0";
    if ((stream !== "" && !ID_RE.test(stream)) || !/^\d{1,6}$/.test(afterText))
        throw Errors.validation([{ field: "after", message: "paramètres de lecture invalides" }]);
    const row = await ctx.env.DB.prepare(
        `SELECT p.account_status, p.spectate_allowed, p.presence_visible,
           EXISTS (SELECT 1 FROM player_friends f WHERE f.status = 'accepted'
                   AND ((f.player_id = ?1 AND f.friend_id = ?2) OR (f.player_id = ?2 AND f.friend_id = ?1))) AS friends,
           EXISTS (SELECT 1 FROM player_blocks b
                   WHERE (b.player_id = ?1 AND b.blocked_id = ?2) OR (b.player_id = ?2 AND b.blocked_id = ?1)) AS blocked
         FROM players p WHERE p.id = ?2`,
    )
        .bind(me, other)
        .first<Pick<PlayerRow, "account_status" | "spectate_allowed" | "presence_visible"> & { friends: number; blocked: number }>();
    if (!row || other === me || row.account_status !== "active" || !row.friends || row.blocked || !spectatable(row))
        throw Errors.forbidden("SPECTATE_FORBIDDEN", "Vous ne pouvez pas regarder ce joueur.");
    const data = await ask(ctx.env, other, `/read?stream=${encodeURIComponent(stream)}&after=${afterText}`);
    return json(ctx, data);
}
