// Replay format ("CPRP" v1) and server-side analysis.
//
// The game records a compact trace of the run instead of a video: the player's feet
// position, state and health sampled at `sample_hz` plus discrete events (jumps, deaths,
// pickups, powers...). The upload is a "CPRZ" container: magic, raw size, zlib stream.
// The server parses it with strict bounds and checks that it is consistent with the
// submitted record and physically plausible for the level. This raises the cost of
// cheating considerably but does not make it impossible: a modified client can forge a
// plausible trace. Podium records of new accounts therefore stay PENDING for review.
//
// Layout (little-endian), see docs/REPLAY_FORMAT.md:
//   char[4] "CPRP" | u16 format_version | u16 sample_hz | u32 frame_count | u32 duration_ms
//   u32 event_count | u8[32] level_hash | u8[16] attempt_id (UUID bytes)
//   u8 len + level_id | u8 len + game_version
//   frame_count x { f32 x, f32 y, u8 state, u8 flags, u8 health, u8 reserved }   (12 bytes)
//   event_count x { u32 frame, u8 type, u8 arg, u16 value }                       (8 bytes)

import { toHex } from "./crypto";
import type { LevelVersionRow, RecordRow } from "./model";

/** Latest format written by the game. Version 1 (trajectory only) is still accepted. */
export const REPLAY_FORMAT_VERSION = 2;
/** Version 2: simulation steps per second and the longest run accepted (2 h of steps). */
export const REPLAY_TICK_HZ = 120;
export const REPLAY_MAX_TICKS = 120 * 7200;
export const REPLAY_CHECK_TICKS = 60;
export const MAX_REPLAY_UPLOAD = 1024 * 1024; // compressed
export const MAX_REPLAY_RAW = 4 * 1024 * 1024; // inflated

export const EventType = {
    Jump: 1,
    AirJump: 2,
    Death: 3,
    Respawn: 4,
    Pickup: 5,
    Power: 6,
    Checkpoint: 7,
    Damage: 8,
    Kill: 9,
    Teleport: 10,
    Finish: 11,
} as const;

/** Pickup kinds (arg of a Pickup event). Shards and relics are the collectibles. */
export const PickupKind = { Shard: 0, Relic: 1, Health: 2, Energy: 3, HeartContainer: 4, Secret: 5 } as const;

export interface ReplayFrame {
    x: number;
    y: number;
    state: number;
    flags: number;
    health: number;
}

export interface ReplayEvent {
    frame: number;
    type: number;
    arg: number;
    value: number;
}

/**
 * Exact replay data (format 2): the gameplay seed and the input of every simulation step,
 * with a digest of the simulation state every 0.5 s. The game re-simulates the run with it;
 * the server checks its consistency and keeps it for a later server-side re-simulation.
 */
export interface ReplayInputs {
    seed: string; // 16 hex characters
    tickHz: number;
    ticks: number;
    runs: number;
    checks: number;
}

export interface Replay {
    formatVersion: number;
    inputs: ReplayInputs | null;
    sampleHz: number;
    durationMs: number;
    levelHash: string;
    attemptId: string;
    levelId: string;
    gameVersion: string;
    frames: ReplayFrame[];
    events: ReplayEvent[];
}

export class ReplayError extends Error {}

function uuidFromBytes(b: Uint8Array): string {
    const h = toHex(b);
    return `${h.slice(0, 8)}-${h.slice(8, 12)}-${h.slice(12, 16)}-${h.slice(16, 20)}-${h.slice(20)}`;
}

/** Inflates a CPRZ container with a hard cap on the inflated size (zip-bomb safe). */
export async function inflateContainer(data: Uint8Array): Promise<Uint8Array> {
    if (data.byteLength < 10) throw new ReplayError("fichier trop court");
    const magic = String.fromCharCode(data[0]!, data[1]!, data[2]!, data[3]!);
    if (magic !== "CPRZ") throw new ReplayError("en-tête CPRZ absent");
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const rawSize = view.getUint32(4, true);
    if (rawSize < 72 || rawSize > MAX_REPLAY_RAW) throw new ReplayError("taille décompressée invalide");
    const stream = new Blob([data.subarray(8)]).stream().pipeThrough(new DecompressionStream("deflate"));
    const reader = stream.getReader();
    const out = new Uint8Array(rawSize);
    let at = 0;
    try {
        for (;;) {
            const { done, value } = await reader.read();
            if (done) break;
            if (at + value.byteLength > rawSize) {
                await reader.cancel();
                throw new ReplayError("données décompressées plus longues qu'annoncé");
            }
            out.set(value, at);
            at += value.byteLength;
        }
    } catch (e) {
        if (e instanceof ReplayError) throw e;
        throw new ReplayError("flux compressé corrompu");
    }
    if (at !== rawSize) throw new ReplayError("données décompressées tronquées");
    return out;
}

export function parseReplay(raw: Uint8Array): Replay {
    const view = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
    let at = 0;
    const need = (n: number) => {
        if (at + n > raw.byteLength) throw new ReplayError("fichier tronqué");
    };
    need(68);
    const magic = String.fromCharCode(raw[0]!, raw[1]!, raw[2]!, raw[3]!);
    if (magic !== "CPRP") throw new ReplayError("en-tête CPRP absent");
    const formatVersion = view.getUint16(4, true);
    if (formatVersion !== 1 && formatVersion !== 2) throw new ReplayError(`version de format ${formatVersion} non prise en charge`);
    const sampleHz = view.getUint16(6, true);
    const frameCount = view.getUint32(8, true);
    const durationMs = view.getUint32(12, true);
    const eventCount = view.getUint32(16, true);
    if (sampleHz < 10 || sampleHz > 120) throw new ReplayError("fréquence d'échantillonnage invalide");
    const levelHash = toHex(raw.subarray(20, 52));
    const attemptId = uuidFromBytes(raw.subarray(52, 68));
    at = 68;
    const readStr = (max: number): string => {
        need(1);
        const len = raw[at]!;
        at += 1;
        if (len > max) throw new ReplayError("chaîne trop longue");
        need(len);
        const s = new TextDecoder("utf-8", { fatal: true, ignoreBOM: false }).decode(raw.subarray(at, at + len));
        at += len;
        return s;
    };
    const levelId = readStr(64);
    const gameVersion = readStr(16);
    if (frameCount === 0) throw new ReplayError("aucune image");
    need(frameCount * 12 + eventCount * 8);
    if (formatVersion === 1 && at + frameCount * 12 + eventCount * 8 !== raw.byteLength) throw new ReplayError("taille incohérente");
    const frames: ReplayFrame[] = new Array(frameCount);
    for (let i = 0; i < frameCount; ++i) {
        frames[i] = {
            x: view.getFloat32(at, true),
            y: view.getFloat32(at + 4, true),
            state: raw[at + 8]!,
            flags: raw[at + 9]!,
            health: raw[at + 10]!,
        };
        at += 12;
    }
    const events: ReplayEvent[] = new Array(eventCount);
    for (let i = 0; i < eventCount; ++i) {
        events[i] = { frame: view.getUint32(at, true), type: raw[at + 4]!, arg: raw[at + 5]!, value: view.getUint16(at + 6, true) };
        at += 8;
    }
    let inputs: ReplayInputs | null = null;
    if (formatVersion === 2) {
        // Seed, run-length encoded inputs (12 bytes per run), state digests (12 bytes each).
        need(20);
        const seed = view.getUint32(at + 4, true).toString(16).padStart(8, "0") + view.getUint32(at, true).toString(16).padStart(8, "0");
        const tickHz = view.getUint16(at + 8, true);
        const ticks = view.getUint32(at + 12, true);
        const runs = view.getUint32(at + 16, true);
        at += 20;
        if (tickHz !== REPLAY_TICK_HZ) throw new ReplayError("fréquence de simulation invalide");
        if (ticks === 0 || ticks > REPLAY_MAX_TICKS || runs === 0 || runs > ticks) throw new ReplayError("section des entrées incohérente");
        need(runs * 12 + 4);
        let total = 0;
        for (let r = 0; r < runs; ++r) {
            const repeat = view.getUint16(at, true);
            if (repeat === 0) throw new ReplayError("plages d'entrées incohérentes");
            total += repeat;
            at += 12;
        }
        if (total !== ticks) throw new ReplayError("nombre d'entrées incohérent");
        const checks = view.getUint32(at, true);
        at += 4;
        if (checks > Math.floor(ticks / REPLAY_CHECK_TICKS) + 2 || at + checks * 12 !== raw.byteLength)
            throw new ReplayError("sommes de contrôle incohérentes");
        let prev = 0;
        for (let c = 0; c < checks; ++c) {
            const tick = view.getUint32(at, true);
            if (tick <= prev || tick > ticks) throw new ReplayError("sommes de contrôle incohérentes");
            prev = tick;
            at += 12;
        }
        inputs = { seed, tickHz, ticks, runs, checks };
    }
    return { formatVersion, inputs, sampleHz, durationMs, levelHash, attemptId, levelId, gameVersion, frames, events };
}

export interface ReplayAnalysis {
    ok: boolean;
    reasons: string[];
    stats: {
        frames: number;
        duration_ms: number;
        deaths: number;
        collectibles: number;
        damage: number;
        max_step_px: number;
        jumps: number;
        /** Format 2: simulation steps recorded (null for a format 1 trajectory). */
        ticks: number | null;
        exact: boolean;
    };
}

/**
 * Checks a replay against its record and the level revision. Returns every problem found
 * (an empty list means the trace is consistent and plausible).
 */
export function analyzeReplay(rep: Replay, record: RecordRow, level: LevelVersionRow): ReplayAnalysis {
    const reasons: string[] = [];
    if (rep.levelId !== record.level_id) reasons.push("niveau différent du record");
    if (rep.levelHash !== record.level_hash) reasons.push("version de niveau différente du record");
    if (rep.attemptId !== record.attempt_id) reasons.push("tentative différente du record");
    if (Math.abs(rep.durationMs - record.time_ms) > 500) reasons.push("durée du replay différente du temps soumis");
    const expectedFrames = (rep.durationMs * rep.sampleHz) / 1000;
    if (Math.abs(rep.frames.length - expectedFrames) > rep.sampleHz * 1.5) reasons.push("nombre d'images incohérent avec la durée");
    if (rep.frames.length > (level.time_limit_ms / 1000 + 5) * rep.sampleHz) reasons.push("replay plus long que la limite du niveau");

    // Events: ordered, within the trace, counts matching the record.
    let deaths = 0;
    let collectibles = 0;
    let damage = 0;
    let jumps = 0;
    let finishFrame = -1;
    let lastFrame = 0;
    const discontinuity = new Set<number>();
    const teleports = new Set<number>();
    for (const e of rep.events) {
        if (e.frame < lastFrame) {
            reasons.push("événements non ordonnés");
            break;
        }
        lastFrame = e.frame;
        if (e.frame >= rep.frames.length) {
            reasons.push("événement hors du replay");
            break;
        }
        switch (e.type) {
            case EventType.Death:
                ++deaths;
                discontinuity.add(e.frame);
                break;
            case EventType.Respawn:
                discontinuity.add(e.frame);
                break;
            case EventType.Teleport:
                teleports.add(e.frame);
                break;
            case EventType.Pickup:
                if (e.arg === PickupKind.Shard || e.arg === PickupKind.Relic) ++collectibles;
                break;
            case EventType.Damage:
                damage += e.value;
                break;
            case EventType.Jump:
            case EventType.AirJump:
                ++jumps;
                break;
            case EventType.Finish:
                finishFrame = e.frame;
                break;
            default:
                break;
        }
    }
    if (deaths !== record.deaths) reasons.push(`morts du replay (${deaths}) ≠ record (${record.deaths})`);
    if (collectibles !== record.collectibles) reasons.push(`objets du replay (${collectibles}) ≠ record (${record.collectibles})`);
    if (record.damage_taken === 0 && damage > 0) reasons.push("dégâts présents dans le replay");
    if (finishFrame < 0 || finishFrame < rep.frames.length - rep.sampleHz) reasons.push("fin du niveau absente du replay");

    // Trajectory: starts at the spawn, ends in the exit, stays in the level, no impossible
    // movement between two samples (except at deaths / respawns / teleports).
    const first = rep.frames[0]!;
    const last = rep.frames[rep.frames.length - 1]!;
    if (Math.hypot(first.x - level.spawn_x, first.y - level.spawn_y) > 96) reasons.push("départ éloigné du point de départ");
    const m = 160;
    if (last.x < level.exit_x - m || last.x > level.exit_x + level.exit_w + m || last.y < level.exit_y - m || last.y > level.exit_y + level.exit_h + m)
        reasons.push("arrivée hors de la sortie du niveau");
    const step = (level.max_speed_px_s / rep.sampleHz) * 1.25 + 4;
    let maxStep = 0;
    let outOfBounds = false;
    let tooFast = 0;
    for (let i = 0; i < rep.frames.length; ++i) {
        const f = rep.frames[i]!;
        if (!Number.isFinite(f.x) || !Number.isFinite(f.y) || f.x < -512 || f.y < -1024 || f.x > level.bounds_w + 512 || f.y > level.bounds_h + 1024)
            outOfBounds = true;
        if (i === 0) continue;
        const p = rep.frames[i - 1]!;
        const d = Math.hypot(f.x - p.x, f.y - p.y);
        const near = (s: Set<number>) => s.has(i) || s.has(i - 1) || s.has(i + 1);
        if (near(discontinuity)) continue;
        const allowed = near(teleports) ? step + 200 : step;
        if (d > allowed) ++tooFast;
        else maxStep = Math.max(maxStep, d);
    }
    if (outOfBounds) reasons.push("position hors du niveau");
    if (tooFast > 0) reasons.push(`${tooFast} déplacement(s) impossible(s)`);
    // Format 2: the recorded steps cover the intro and the whole run (never less than the
    // run itself), with a state digest every 0.5 s.
    if (rep.inputs) {
        const stepsMs = (rep.inputs.ticks * 1000) / rep.inputs.tickHz;
        if (stepsMs + 500 < rep.durationMs) reasons.push("entrées enregistrées plus courtes que la partie");
        if (stepsMs > rep.durationMs + 600_000) reasons.push("entrées enregistrées bien plus longues que la partie");
        if (rep.inputs.checks < Math.floor(rep.inputs.ticks / REPLAY_CHECK_TICKS)) reasons.push("sommes de contrôle manquantes");
    }
    return {
        ok: reasons.length === 0,
        reasons,
        stats: {
            frames: rep.frames.length,
            duration_ms: rep.durationMs,
            deaths,
            collectibles,
            damage,
            max_step_px: Math.round(maxStep),
            jumps,
            ticks: rep.inputs ? rep.inputs.ticks : null,
            exact: rep.inputs !== null,
        },
    };
}
