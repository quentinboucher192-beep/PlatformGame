// Test helpers: HTTP calls into the Worker, seed data and replay files.

import { env, SELF } from "cloudflare:test";

export const LEVEL_HASH = "a".repeat(64);
export const LEVEL_HASH_V2 = "b".repeat(64);
export const OTHER_HASH = "c".repeat(64);
export const LEVEL2_HASH = "d".repeat(64);

export interface ApiResult<T = any> {
    status: number;
    body: T;
    headers: Headers;
}

let ipCounter = 1;
/** Every helper call comes from its own IP unless one is given (rate limits are per IP). */
export function freshIp(): string {
    ipCounter += 1;
    return `10.${(ipCounter >> 16) & 255}.${(ipCounter >> 8) & 255}.${ipCounter & 255}`;
}

export async function api<T = any>(
    method: string,
    path: string,
    opts: { body?: unknown; token?: string; ip?: string; headers?: Record<string, string>; raw?: BodyInit } = {},
): Promise<ApiResult<T>> {
    const headers: Record<string, string> = { "cf-connecting-ip": opts.ip ?? freshIp(), ...opts.headers };
    let body: BodyInit | undefined;
    if (opts.raw !== undefined) body = opts.raw;
    else if (opts.body !== undefined) {
        body = JSON.stringify(opts.body);
        headers["content-type"] ??= "application/json";
    }
    if (opts.token) headers.authorization = `Bearer ${opts.token}`;
    const res = await SELF.fetch(`http://cosmic.test${path}`, { method, headers, body });
    const type = res.headers.get("content-type") ?? "";
    const parsed = type.includes("application/json") ? await res.json() : await res.arrayBuffer();
    return { status: res.status, body: parsed as T, headers: res.headers };
}

export async function seedCountries(): Promise<void> {
    await env.DB.batch([
        env.DB.prepare("INSERT INTO countries VALUES ('FR', 'France', 'France', 46.6, 2.4)"),
        env.DB.prepare("INSERT INTO countries VALUES ('US', 'États-Unis', 'United States', 39.8, -98.6)"),
        env.DB.prepare("INSERT INTO countries VALUES ('JP', 'Japon', 'Japan', 36.2, 138.3)"),
        env.DB.prepare("INSERT INTO countries VALUES ('DE', 'Allemagne', 'Germany', 51.1, 10.4)"),
        env.DB.prepare("INSERT INTO countries VALUES ('AQ', 'Antarctique', 'Antarctica', NULL, NULL)"),
    ]);
}

export const SPAWN = { x: 100, y: 500 };
export const EXIT = { x: 5000, y: 400, w: 64, h: 96 };

function versionInsert(levelId: string, version: number, hash: string, status = "active", minTime = 30000) {
    return env.DB.prepare(
        `INSERT INTO level_versions (level_id, version, content_hash, compatibility_status, min_game_version, total_collectibles,
           total_enemies, time_limit_ms, min_time_ms, max_speed_px_s, bounds_w, bounds_h, spawn_x, spawn_y, exit_x, exit_y, exit_w, exit_h, created_at)
         VALUES (?1, ?2, ?3, ?4, '2.1.0', 10, 20, 900000, ?5, 1500, 6000, 1200, ?6, ?7, ?8, ?9, ?10, ?11, '2026-11-01T00:00:00.000Z')`,
    ).bind(levelId, version, hash, status, minTime, SPAWN.x, SPAWN.y, EXIT.x, EXIT.y, EXIT.w, EXIT.h);
}

/** Two levels on planet "testworld" plus one on "other". */
export async function seedLevels(): Promise<void> {
    await env.DB.batch([
        env.DB.prepare("INSERT INTO levels VALUES ('test_01', 'Niveau test', 'testworld', 'Monde test', 1, 1, ?1, 1)").bind(LEVEL_HASH),
        env.DB.prepare("INSERT INTO levels VALUES ('test_02', 'Niveau test 2', 'testworld', 'Monde test', 2, 1, ?1, 1)").bind(LEVEL2_HASH),
        env.DB.prepare("INSERT INTO levels VALUES ('locked_01', 'Fermé', 'other', 'Autre', 1, 1, ?1, 0)").bind(OTHER_HASH),
        versionInsert("test_01", 1, LEVEL_HASH),
        versionInsert("test_02", 1, LEVEL2_HASH),
        versionInsert("locked_01", 1, OTHER_HASH),
    ]);
}

/** Publishes revision 2 of test_01 (revision 1 becomes legacy). */
export async function publishV2(): Promise<void> {
    await env.DB.batch([
        versionInsert("test_01", 2, LEVEL_HASH_V2),
        env.DB.prepare("UPDATE level_versions SET compatibility_status = 'legacy' WHERE level_id = 'test_01' AND version = 1"),
        env.DB.prepare("UPDATE levels SET current_version = 2, content_hash = ?1 WHERE id = 'test_01'").bind(LEVEL_HASH_V2),
    ]);
}

/** Empties every table (storage persists between tests of a file). */
export async function resetDb(): Promise<void> {
    const tables = ["notifications", "player_playtime", "player_follows", "community_level_players", "community_comment_reports", "community_comment_reactions",
        "community_comments", "community_favorites", "client_releases", "rate_limits", "audit_log", "community_records", "community_reports", "community_ratings", "community_level_versions",
        "community_levels", "message_reports", "messages", "conversation_members", "conversations", "player_blocks", "player_friends", "player_bests", "replay_metadata", "records", "player_sessions",
        "player_credentials", "players", "level_versions", "levels", "countries"];
    await env.DB.batch(tables.map((t) => env.DB.prepare(`DELETE FROM ${t}`)));
}

export async function seedAll(): Promise<void> {
    await resetDb();
    await seedCountries();
    await seedLevels();
}

export interface TestPlayer {
    id: string;
    username: string;
    password: string;
    token: string;
    refresh: string;
}

export async function registerPlayer(username: string, extra: Record<string, unknown> = {}): Promise<TestPlayer> {
    const password = "correct horse battery";
    const res = await api("POST", "/api/v1/auth/register", {
        body: { username, password, accept_tos: true, tos_version: "2026-11", ...extra },
    });
    if (res.status !== 201) throw new Error(`register ${username}: ${res.status} ${JSON.stringify(res.body)}`);
    return {
        id: res.body.data.player.id,
        username,
        password,
        token: res.body.data.session.access_token,
        refresh: res.body.data.session.refresh_token,
    };
}

export async function setTrust(playerId: string, trust: number): Promise<void> {
    await env.DB.prepare("UPDATE players SET trust_level = ?1 WHERE id = ?2").bind(trust, playerId).run();
}

export function submission(overrides: Record<string, unknown> = {}): Record<string, unknown> {
    return {
        level_id: "test_01",
        level_version: 1,
        level_hash: LEVEL_HASH,
        category: "ANY_PERCENT",
        time_ms: 60000,
        deaths: 0,
        damage_taken: 0,
        collectibles: 4,
        total_collectibles: 10,
        game_version: "2.1.0",
        attempt_id: crypto.randomUUID(),
        ...overrides,
    };
}

// ------------------------------------------------------------------- replays

export interface ReplaySpec {
    levelId?: string;
    levelHash?: string;
    attemptId: string;
    durationMs: number;
    sampleHz?: number;
    deaths?: number;
    collectibles?: number;
    /** Override the trajectory: (frame index, frame count) -> position. */
    path?: (i: number, n: number) => { x: number; y: number };
    corrupt?: "magic" | "truncated" | "bomb";
    /** Format 2: number of simulation steps recorded (defaults to the duration + 3 s of intro). */
    exactTicks?: number;
    exact?: boolean;
}

function hexToBytes(hex: string): Uint8Array {
    const out = new Uint8Array(hex.length / 2);
    for (let i = 0; i < out.length; ++i) out[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
    return out;
}

async function deflate(data: Uint8Array): Promise<Uint8Array> {
    const stream = new Blob([data]).stream().pipeThrough(new CompressionStream("deflate"));
    return new Uint8Array(await new Response(stream).arrayBuffer());
}

/** Builds a CPRZ replay exactly like the game does (see docs/REPLAY_FORMAT.md). */
export async function buildReplay(spec: ReplaySpec): Promise<Uint8Array> {
    const hz = spec.sampleHz ?? 30;
    const frames = Math.round((spec.durationMs * hz) / 1000) + 1;
    const levelId = new TextEncoder().encode(spec.levelId ?? "test_01");
    const gameVersion = new TextEncoder().encode("2.1.0");
    const events: { frame: number; type: number; arg: number; value: number }[] = [];
    const deaths = spec.deaths ?? 0;
    for (let d = 0; d < deaths; ++d) {
        const f = Math.floor(((d + 1) * frames) / (deaths + 2));
        events.push({ frame: f, type: 3, arg: 0, value: 0 }, { frame: f + 1, type: 4, arg: 0, value: 0 });
    }
    const collect = spec.collectibles ?? 4;
    for (let c = 0; c < collect; ++c) events.push({ frame: Math.floor(((c + 1) * frames) / (collect + 3)), type: 5, arg: c % 2, value: 0 });
    events.push({ frame: frames - 1, type: 11, arg: 0, value: 0 });
    events.sort((a, b) => a.frame - b.frame);
    const exact = spec.exact || spec.exactTicks !== undefined;
    const ticks = spec.exactTicks ?? Math.round((spec.durationMs / 1000 + 3) * 120);
    // Inputs: one run of 1000 steps at most (u16 repeat), then a digest every 60 steps.
    const runs: number[] = [];
    for (let left = ticks; left > 0; left -= Math.min(left, 1000)) runs.push(Math.min(left, 1000));
    const checks = Math.floor(ticks / 60);
    const trailer = exact ? 20 + runs.length * 12 + 4 + checks * 12 : 0;
    const size = 68 + 1 + levelId.length + 1 + gameVersion.length + frames * 12 + events.length * 8 + trailer;
    const raw = new Uint8Array(size);
    const view = new DataView(raw.buffer);
    raw.set(new TextEncoder().encode(spec.corrupt === "magic" ? "XXXX" : "CPRP"), 0);
    view.setUint16(4, exact ? 2 : 1, true);
    view.setUint16(6, hz, true);
    view.setUint32(8, frames, true);
    view.setUint32(12, spec.durationMs, true);
    view.setUint32(16, events.length, true);
    raw.set(hexToBytes(spec.levelHash ?? LEVEL_HASH), 20);
    raw.set(hexToBytes(spec.attemptId.replace(/-/g, "")), 52);
    let at = 68;
    raw[at++] = levelId.length;
    raw.set(levelId, at);
    at += levelId.length;
    raw[at++] = gameVersion.length;
    raw.set(gameVersion, at);
    at += gameVersion.length;
    const path =
        spec.path ??
        ((i: number, n: number) => ({ x: SPAWN.x + ((EXIT.x + 10 - SPAWN.x) * i) / (n - 1), y: SPAWN.y + ((EXIT.y + 40 - SPAWN.y) * i) / (n - 1) }));
    for (let i = 0; i < frames; ++i) {
        const p = path(i, frames);
        view.setFloat32(at, p.x, true);
        view.setFloat32(at + 4, p.y, true);
        raw[at + 8] = 1;
        raw[at + 9] = 2;
        raw[at + 10] = 10;
        at += 12;
    }
    for (const e of events) {
        view.setUint32(at, e.frame, true);
        raw[at + 4] = e.type;
        raw[at + 5] = e.arg;
        view.setUint16(at + 6, e.value, true);
        at += 8;
    }
    if (exact) {
        view.setUint32(at, 0x1234, true);
        view.setUint32(at + 4, 0, true);
        view.setUint16(at + 8, 120, true);
        view.setUint32(at + 12, ticks, true);
        view.setUint32(at + 16, runs.length, true);
        at += 20;
        for (const r of runs) {
            view.setUint16(at, r, true);
            view.setUint16(at + 2, 2, true); // MoveRight held
            raw[at + 8] = 127;
            at += 12;
        }
        view.setUint32(at, checks, true);
        at += 4;
        for (let c = 1; c <= checks; ++c) {
            view.setUint32(at, c * 60, true);
            view.setUint32(at + 4, c, true);
            at += 12;
        }
    }
    const body = await deflate(spec.corrupt === "truncated" ? raw.subarray(0, raw.length - 20) : raw);
    const out = new Uint8Array(8 + body.length);
    out.set(new TextEncoder().encode("CPRZ"), 0);
    new DataView(out.buffer).setUint32(4, spec.corrupt === "bomb" ? 100 : raw.length, true);
    out.set(body, 8);
    return out;
}

export async function uploadReplay(token: string, recordId: string, data: Uint8Array): Promise<ApiResult> {
    return api("PUT", `/api/v1/records/${recordId}/replay`, { token, raw: data, headers: { "content-type": "application/octet-stream" } });
}

/** Submits a run and uploads a matching valid replay; returns the final record status. */
export async function verifiedRun(p: TestPlayer, timeMs: number, overrides: Record<string, unknown> = {}): Promise<{ recordId: string; status: string; body: any }> {
    const sub = submission({ time_ms: timeMs, ...overrides });
    const res = await api("POST", "/api/v1/records/submit", { token: p.token, body: sub });
    if (res.status !== 201) throw new Error(`submit: ${res.status} ${JSON.stringify(res.body)}`);
    const recordId = res.body.data.record.id as string;
    const replay = await buildReplay({
        attemptId: sub.attempt_id as string,
        durationMs: timeMs,
        levelId: sub.level_id as string,
        levelHash: sub.level_hash as string,
        deaths: sub.deaths as number,
        collectibles: sub.collectibles as number,
    });
    const up = await uploadReplay(p.token, recordId, replay);
    if (up.status !== 201) throw new Error(`replay: ${up.status} ${JSON.stringify(up.body)}`);
    return { recordId, status: up.body.data.records[0]?.record.verification_status ?? up.body.data.verification_status, body: up.body };
}
