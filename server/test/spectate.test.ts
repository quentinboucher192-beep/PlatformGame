// Live spectating between friends (2.36): who may watch (accepted friends only, the setting
// on, the presence shown, never across a block), what the room keeps (one run, in memory,
// bounded, in order) and that the player watched learns nothing about the viewers.

import { env, runDurableObjectAlarm, runInDurableObject } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { canonicalRules, periodBounds, periodOf, rulesHash, utcDate } from "../src/challenges";
import { MAX_CHUNK_FRAMES, MAX_RUN_FRAMES, POSTS_PER_WINDOW, RUN_BYTES, invalidRuns } from "../src/spectate";
import { LEVEL_HASH, api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";

async function befriend(a: TestPlayer, b: TestPlayer): Promise<void> {
    expect((await api("POST", `${V}/friends/request`, { token: a.token, body: { player_id: b.id } })).status).toBe(201);
    expect((await api("POST", `${V}/friends/accept`, { token: b.token, body: { player_id: a.id } })).status).toBe(200);
}

interface Frame {
    held?: number;
    pressed?: number;
    released?: number;
    mx?: number;
    my?: number;
    flags?: number;
    choice?: number;
}

/** Inputs as the game encodes them: runs of 18 bytes (replay format 3), base64. */
function runsOf(runs: [number, Frame][]): string {
    const bytes = new Uint8Array(runs.length * RUN_BYTES);
    const v = new DataView(bytes.buffer);
    runs.forEach(([repeat, f], i) => {
        const at = i * RUN_BYTES;
        v.setUint16(at, repeat, true);
        v.setUint32(at + 2, f.held ?? 0, true);
        v.setUint32(at + 6, f.pressed ?? 0, true);
        v.setUint32(at + 10, f.released ?? 0, true);
        v.setInt8(at + 14, f.mx ?? 0);
        v.setInt8(at + 15, f.my ?? 0);
        v.setUint8(at + 16, f.flags ?? 0);
        v.setUint8(at + 17, f.choice ?? 0);
    });
    let s = "";
    for (const b of bytes) s += String.fromCharCode(b);
    return btoa(s);
}

const RIGHT = { held: 2, mx: 127 };
/** Reaches into a player's room (its private state), as the platform would see it. */
const inRoom = runInDurableObject as unknown as (stub: unknown, cb: (room: any) => void) => Promise<void>;

function startBody(runId: string, extra: Record<string, unknown> = {}) {
    return {
        action: "start",
        run_id: runId,
        level: { source: "campaign", id: "test_01", version: 1, hash: LEVEL_HASH },
        seed: "0123456789abcdef",
        keys_per_slot: true,
        sim_version: 11,
        game_version: "2.36.0",
        ...extra,
    };
}

const send = (p: TestPlayer, body: unknown) => api("POST", `${V}/spectate/broadcast`, { token: p.token, body });
const chunk = (p: TestPlayer, runId: string, seq: number, from: number, count: number, extra: Record<string, unknown> = {}) =>
    send(p, { action: "chunk", run_id: runId, seq, from_frame: from, count, runs: count ? runsOf([[count, RIGHT]]) : "", ...extra });
const read = (viewer: TestPlayer, target: TestPlayer, query = "") => api("GET", `${V}/spectate/${target.id}${query}`, { token: viewer.token });
const friendsOf = async (p: TestPlayer) => (await api("GET", `${V}/friends`, { token: p.token })).body.data.friends as { player: { id: string }; playing?: Record<string, unknown> }[];

/** Two friends: Nova plays (her game says it runs), Orion watches; Intrus is nobody's friend. */
async function cast() {
    const nova = await registerPlayer("Nova");
    const orion = await registerPlayer("Orion");
    const intrus = await registerPlayer("Intrus");
    await befriend(nova, orion);
    expect((await api("POST", `${V}/presence`, { token: nova.token, body: { state: "active" } })).status).toBe(200);
    return { nova, orion, intrus };
}

describe("live spectating: who may watch", () => {
    it("an accepted friend reads the run; a stranger never; the setting off or a hidden presence stop it", async () => {
        const { nova, orion, intrus } = await cast();
        // Not playing: nothing to read, no « Regarder ».
        expect((await read(orion, nova)).body.data).toEqual({ live: false });
        expect((await friendsOf(orion)).find((f) => f.player.id === nova.id)?.playing).toBeUndefined();

        const runId = crypto.randomUUID();
        const s = await send(nova, startBody(runId));
        expect(s.status).toBe(200);
        expect(s.body.data).toEqual({ accepted: true, run_id: runId });
        expect((await chunk(nova, runId, 1, 0, 60, { checks: [[60, "00000000deadbeef"]] })).body.data).toEqual({ accepted: true, seq: 1, frames: 60 });

        const r = await read(orion, nova);
        expect(r.status).toBe(200);
        const d = r.body.data;
        expect(d).toMatchObject({ live: true, last_seq: 1, frames: 60, ended: null, stale: false, more: false });
        // The header as the server checked it: the level's title comes from the server.
        expect(d.run).toMatchObject({
            run_id: runId,
            kind: "campaign",
            level: { source: "campaign", id: "test_01", version: 1, hash: LEVEL_HASH, title: "Niveau test" },
            challenge: null,
            seed: "0123456789abcdef",
            keys_per_slot: true,
            sim_version: 11,
        });
        expect(d.chunks).toEqual([{ seq: 1, from_frame: 0, count: 60, runs: runsOf([[60, RIGHT]]), checks: [[60, "00000000deadbeef"]] }]);
        // Only what came after.
        await chunk(nova, runId, 2, 60, 30);
        const next = await read(orion, nova, `?stream=${d.run.stream}&after=1`);
        expect(next.body.data.chunks.map((c: { seq: number }) => c.seq)).toEqual([2]);
        // Another stream (or none): everything from the start.
        expect((await read(orion, nova, "?after=1")).body.data.chunks).toHaveLength(2);

        // The friends list says it can be watched.
        expect((await friendsOf(orion)).find((f) => f.player.id === nova.id)?.playing).toMatchObject({ level_title: "Niveau test", kind: "campaign" });

        // A stranger, or nobody logged in: refused (the same answer whatever the reason).
        const stranger = await read(intrus, nova);
        expect(stranger.status).toBe(403);
        expect(stranger.body.error.code).toBe("SPECTATE_FORBIDDEN");
        expect((await api("GET", `${V}/spectate/${nova.id}`)).status).toBe(401);
        expect((await read(nova, nova)).status).toBe(403);
        // A pending request is not a friendship.
        await api("POST", `${V}/friends/request`, { token: intrus.token, body: { player_id: nova.id } });
        expect((await read(intrus, nova)).status).toBe(403);

        // Presence hidden: nobody may watch, the run is forgotten, the game is told to stop.
        expect((await api("PATCH", `${V}/players/me`, { token: nova.token, body: { presence_visible: false } })).status).toBe(200);
        expect((await read(orion, nova)).status).toBe(403);
        expect((await friendsOf(orion)).find((f) => f.player.id === nova.id)?.playing).toBeUndefined();
        expect((await chunk(nova, runId, 3, 90, 30)).body.error.code).toBe("SPECTATE_DISABLED");
        await api("PATCH", `${V}/players/me`, { token: nova.token, body: { presence_visible: true } });
        expect((await read(orion, nova)).body.data).toEqual({ live: false });

        // « Mes amis peuvent me regarder jouer » off: the same, and the setting is in the private profile.
        await send(nova, startBody(runId));
        const off = await api("PATCH", `${V}/players/me`, { token: nova.token, body: { spectate_allowed: false } });
        expect(off.status).toBe(200);
        expect(off.body.data.player.spectate_allowed).toBe(false);
        expect((await read(orion, nova)).status).toBe(403);
        expect((await send(nova, startBody(runId))).status).toBe(403);
        expect((await api("PATCH", `${V}/players/me`, { token: nova.token, body: { spectate_allowed: "oui" } })).status).toBe(400);
        const on = await api("PATCH", `${V}/players/me`, { token: nova.token, body: { spectate_allowed: true } });
        expect(on.body.data.player.spectate_allowed).toBe(true);
        expect((await read(orion, nova)).body.data).toEqual({ live: false });
        // In the export of the player's data too.
        expect((await api("GET", `${V}/players/me/export`, { token: nova.token })).body.data.player.spectate_allowed).toBe(true);
    });

    it("a block, either way, ends it (even if a friendship row were left)", async () => {
        const { nova, orion } = await cast();
        const runId = crypto.randomUUID();
        await send(nova, startBody(runId));
        expect((await read(orion, nova)).status).toBe(200);
        expect((await api("POST", `${V}/social/blocks`, { token: nova.token, body: { player_id: orion.id } })).status).toBe(201);
        expect((await read(orion, nova)).status).toBe(403);
        // Even with a friendship restored behind the server's back, the block wins.
        const now = new Date().toISOString();
        await env.DB.batch([
            env.DB.prepare("INSERT INTO player_friends (player_id, friend_id, status, created_at) VALUES (?1, ?2, 'accepted', ?3)").bind(nova.id, orion.id, now),
            env.DB.prepare("INSERT INTO player_friends (player_id, friend_id, status, created_at) VALUES (?1, ?2, 'accepted', ?3)").bind(orion.id, nova.id, now),
        ]);
        expect((await read(orion, nova)).status).toBe(403);
        expect((await friendsOf(orion)).find((f) => f.player.id === nova.id)?.playing).toBeUndefined();
        // The other way round too.
        await api("DELETE", `${V}/social/blocks/${orion.id}`, { token: nova.token });
        expect((await read(orion, nova)).status).toBe(200);
        expect((await api("POST", `${V}/social/blocks`, { token: orion.token, body: { player_id: nova.id } })).status).toBe(201);
        expect((await read(orion, nova)).status).toBe(403);
    });

    it("the player watched learns nothing: the same answers whether anybody watches or not", async () => {
        const { nova, orion } = await cast();
        const runId = crypto.randomUUID();
        const alone = await send(nova, startBody(runId));
        const c1 = await chunk(nova, runId, 1, 0, 60);
        await read(orion, nova);
        await read(orion, nova);
        const c2 = await chunk(nova, runId, 2, 60, 60);
        expect(Object.keys(alone.body.data).sort()).toEqual(["accepted", "run_id"]);
        expect(Object.keys(c1.body.data).sort()).toEqual(Object.keys(c2.body.data).sort());
        expect(Object.keys(c2.body.data).sort()).toEqual(["accepted", "frames", "seq"]);
        // Nothing about the viewers in the player's own data either: no notification, no audit line.
        expect((await api("GET", `${V}/notifications`, { token: nova.token })).body.data.notifications).toHaveLength(0);
        const audit = await env.DB.prepare("SELECT action FROM audit_log WHERE player_id = ?1").bind(nova.id).all<{ action: string }>();
        expect(audit.results.map((a) => a.action).filter((a) => a.includes("spectat") || a.includes("watch"))).toEqual([]);
        // Nothing of the run is written in the database.
        const tables = await env.DB.prepare("SELECT name FROM sqlite_master WHERE type = 'table' AND name LIKE '%spectat%'").all();
        expect(tables.results).toHaveLength(0);
    });
});

describe("live spectating: the run kept by the room", () => {
    it("in order, once, bounded; a run ended stays readable a moment and leaves the friends list", async () => {
        const { nova, orion } = await cast();
        const runId = crypto.randomUUID();
        // A chunk before any start: the game must send its run again from the start.
        const early = await chunk(nova, runId, 1, 0, 60);
        expect(early.status).toBe(409);
        expect(early.body.error.code).toBe("SPECTATE_RESYNC");
        await send(nova, startBody(runId));
        expect((await chunk(nova, runId, 1, 0, 60)).status).toBe(200);
        // The same chunk again (lost answer): accepted once.
        expect((await chunk(nova, runId, 1, 0, 60)).body.data).toEqual({ accepted: true, seq: 1, frames: 60 });
        // A gap, a wrong first step, another run: resync, with where the room stands.
        const gap = await chunk(nova, runId, 3, 60, 60);
        expect(gap.status).toBe(409);
        expect(gap.body.error.details).toEqual({ seq: 1, frames: 60 });
        expect((await chunk(nova, runId, 2, 59, 60)).status).toBe(409);
        expect((await chunk(nova, crypto.randomUUID(), 2, 60, 60)).status).toBe(409);
        // A keep-alive (no step while the game is paused) is a chunk too.
        expect((await chunk(nova, runId, 2, 60, 0)).body.data).toEqual({ accepted: true, seq: 2, frames: 60 });
        // Digests in order, inside the chunk.
        expect((await chunk(nova, runId, 3, 60, 60, { checks: [[60, "0000000000000001"]] })).status).toBe(400);
        expect((await chunk(nova, runId, 3, 60, 60, { checks: [[120, "0000000000000001"], [120, "0000000000000002"]] })).status).toBe(400);
        expect((await chunk(nova, runId, 3, 60, 60, { checks: [[120, "zz"]] })).status).toBe(400);
        expect((await chunk(nova, runId, 3, 60, 60, { checks: [[120, "0000000000000001"]] })).status).toBe(200);

        // Ended: still readable (the viewers play its last steps), no more in the friends list.
        expect((await send(nova, { action: "end", run_id: runId, reason: "finished" })).body.data).toEqual({ ended: true });
        const ended = await read(orion, nova);
        expect(ended.body.data).toMatchObject({ live: true, ended: "finished", frames: 120, last_seq: 3 });
        expect((await friendsOf(orion)).find((f) => f.player.id === nova.id)?.playing).toBeUndefined();
        expect((await chunk(nova, runId, 4, 120, 60)).status).toBe(409);
        // A new run replaces it at once (a restart).
        const next = crypto.randomUUID();
        await send(nova, startBody(next));
        const fresh = await read(orion, nova, `?stream=${ended.body.data.run.stream}&after=3`);
        expect(fresh.body.data).toMatchObject({ live: true, ended: null, frames: 0, last_seq: 0, chunks: [] });
        expect(fresh.body.data.run.run_id).toBe(next);
        expect(fresh.body.data.run.stream).not.toBe(ended.body.data.run.stream);
    });

    it("refuses what does not fit: bad inputs, too long a chunk, too long a run, too many posts", async () => {
        const { nova, orion } = await cast();
        const runId = crypto.randomUUID();
        await send(nova, startBody(runId));
        const bad = async (runs: string, count: number) => (await send(nova, { action: "chunk", run_id: runId, seq: 1, from_frame: 0, count, runs })).status;
        expect(await bad(runsOf([[59, RIGHT]]), 60)).toBe(400);  // fewer steps than said
        expect(await bad(runsOf([[30, RIGHT], [31, {}]]), 60)).toBe(400);  // more
        expect(await bad(runsOf([[0, RIGHT], [60, {}]]), 60)).toBe(400);  // an empty run
        expect(await bad(runsOf([[60, { held: 1 << 21 }]]), 60)).toBe(400);  // unknown action
        expect(await bad(runsOf([[60, { mx: -128 }]]), 60)).toBe(400);
        expect(await bad(runsOf([[60, { flags: 2 }]]), 60)).toBe(400);
        expect(await bad(runsOf([[60, { choice: 9 }]]), 60)).toBe(400);
        expect(await bad("not base64!", 60)).toBe(400);
        expect(await bad(runsOf([[60, RIGHT]]).slice(0, 20), 60)).toBe(400);
        expect(await bad("", 0)).toBe(200);
        expect(invalidRuns(runsOf([[60, { choice: 255, flags: 1, mx: -127, my: 127, held: (1 << 21) - 1 }]]), 60)).toBeNull();
        // One chunk carries 20 s at most.
        expect((await chunk(nova, runId, 2, 0, MAX_CHUNK_FRAMES + 1)).status).toBe(400);
        // Unknown fields, unknown actions, a header that does not check out.
        expect((await send(nova, { ...startBody(runId), viewer: "x" })).status).toBe(400);
        expect((await send(nova, { action: "watchers" })).status).toBe(400);
        expect((await send(nova, startBody(runId, { level: { source: "campaign", id: "test_01", version: 1, hash: "e".repeat(64) } }))).status).toBe(409);
        expect((await send(nova, startBody(runId, { level: { source: "campaign", id: "nope", version: 1, hash: LEVEL_HASH } }))).status).toBe(409);
        expect((await send(nova, startBody(runId, { seed: "xyz" }))).status).toBe(400);

        // 30 minutes of play at most: then the viewers are told, nothing more is kept. (The
        // room is filled directly: the posts of a player are limited to about 2 per second.)
        const long = crypto.randomUUID();
        await send(nova, startBody(long));
        const stub = env.SPECTATE_ROOMS.get(env.SPECTATE_ROOMS.idFromName(nova.id));
        const runs = runsOf([[120, RIGHT]]);
        const chunks = MAX_RUN_FRAMES / 120;
        await inRoom(stub, (room) => {
            for (let i = 1; i <= chunks; ++i) room.run.chunks.push({ seq: i, from_frame: (i - 1) * 120, count: 120, runs, checks: [] });
            room.run.seq = chunks;
            room.run.frames = MAX_RUN_FRAMES;
            room.run.bytes = chunks * runs.length;
        });
        const over = await chunk(nova, long, chunks + 1, MAX_RUN_FRAMES, 60);
        expect(over.status).toBe(413);
        expect(over.body.error.code).toBe("SPECTATE_RUN_TOO_LONG");
        const told = await read(orion, nova);
        expect(told.body.data).toMatchObject({ ended: "too_long", frames: MAX_RUN_FRAMES });
        // A long run is read in several pages, each bounded.
        expect(told.body.data.more).toBe(true);
        expect(told.body.data.chunks.length).toBeLessThan(chunks);
        let seen = told.body.data.chunks.length;
        let page = told.body.data;
        while (page.more) {
            page = (await read(orion, nova, `?stream=${page.run.stream}&after=${page.chunks[page.chunks.length - 1].seq}`)).body.data;
            seen += page.chunks.length;
        }
        expect(seen).toBe(chunks);
        expect((await chunk(nova, long, chunks + 1, MAX_RUN_FRAMES, 60)).status).toBe(413);
    });

    it("forgotten in time: silent for 30 s it reads « connexion perdue », then nothing", async () => {
        const { nova, orion } = await cast();
        const runId = crypto.randomUUID();
        await send(nova, startBody(runId));
        await chunk(nova, runId, 1, 0, 60);
        const stub = env.SPECTATE_ROOMS.get(env.SPECTATE_ROOMS.idFromName(nova.id));
        await inRoom(stub, (room) => {
            room.run.updatedAt = Date.now() - 31_000;
        });
        expect((await read(orion, nova)).body.data).toMatchObject({ live: true, stale: true, ended: null });
        expect((await friendsOf(orion)).find((f) => f.player.id === nova.id)?.playing).toBeUndefined();
        await inRoom(stub, (room) => {
            room.run.updatedAt = Date.now() - 121_000;
        });
        expect(await runDurableObjectAlarm(stub)).toBe(true);
        await inRoom(stub, (room) => {
            expect(room.run).toBeNull();
        });
        expect((await read(orion, nova)).body.data).toEqual({ live: false });
        // An ended run: a minute more, then forgotten as well.
        await send(nova, startBody(runId));
        await send(nova, { action: "end", run_id: runId, reason: "quit" });
        expect((await read(orion, nova)).body.data).toMatchObject({ ended: "quit" });
        await inRoom(stub, (room) => {
            room.run.endedAt = Date.now() - 61_000;
        });
        expect((await read(orion, nova)).body.data).toEqual({ live: false });
    });

    it("about two posts per second per player", async () => {
        const { nova } = await cast();
        const runId = crypto.randomUUID();
        await send(nova, startBody(runId));
        let status = 200,
            i = 1;
        for (; i <= POSTS_PER_WINDOW + 2 && status === 200; ++i) status = (await chunk(nova, runId, i, 0, 0)).status;
        expect(status).toBe(429);
        expect(i - 1).toBeGreaterThanOrEqual(POSTS_PER_WINDOW - 1);
    });

    it("community levels and challenges: checked against what the server knows", async () => {
        const { nova, orion } = await cast();
        const now = new Date().toISOString();
        const cid = crypto.randomUUID();
        const chash = "f".repeat(64);
        await env.DB.batch([
            env.DB.prepare(
                `INSERT INTO community_levels (id, author_id, title, title_norm, planet, status, current_version, created_at, updated_at)
                 VALUES (?1, ?2, 'Tour de cristal', 'tour de cristal', 'glacior', 'published', 1, ?3, ?3)`,
            ).bind(cid, orion.id, now),
            env.DB.prepare(
                `INSERT INTO community_level_versions (level_id, version, status, level_hash, min_game_version, created_at, published_at)
                 VALUES (?1, 1, 'published', ?2, '2.1.0', ?3, ?3)`,
            ).bind(cid, chash, now),
        ]);
        const community = { source: "community", id: cid, version: 1, hash: chash };
        expect((await send(nova, startBody(crypto.randomUUID(), { level: community }))).status).toBe(200);
        expect((await read(orion, nova)).body.data.run).toMatchObject({ kind: "community", level: { title: "Tour de cristal" } });
        expect((await send(nova, startBody(crypto.randomUUID(), { level: { ...community, version: 2 } }))).status).toBe(409);
        expect((await send(nova, startBody(crypto.randomUUID(), { level: { ...community, id: "test_01" } }))).status).toBe(400);

        // Today's challenge on test_01: its rules come from the server, never from the game.
        const p = periodBounds("daily", periodOf("daily", utcDate()))!;
        const id = `daily-${p.period}`;
        const rules = { hearts: 2, slots: 3, powers: ["dash"], stations: false, checkpoints: false };
        await env.DB.prepare(
            `INSERT INTO challenges (id, kind, period, date, end_date, source, level_source, level_id, level_version, level_hash, level_title, hearts,
               slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, created_at, updated_at)
             VALUES (?1, 'daily', ?2, ?3, ?4, 'admin', 'campaign', 'test_01', 1, ?5, 'Niveau test', 2, 3, '["dash"]', 0, 0, ?6, 90000, 70000, 1, ?7, ?7)`,
        )
            .bind(id, p.period, p.start, p.end, LEVEL_HASH, await rulesHash(id, rules), now)
            .run();
        expect((await send(nova, startBody(crypto.randomUUID(), { challenge_id: id }))).status).toBe(200);
        const run = (await read(orion, nova)).body.data.run;
        expect(run).toMatchObject({ kind: "challenge", challenge: { id, kind: "daily", rules: canonicalRules(id, rules) } });
        expect((await friendsOf(orion)).find((f) => f.player.id === nova.id)?.playing).toMatchObject({ kind: "challenge", level_source: "campaign" });
        // Another level than the challenge's, or a challenge that does not exist (or not begun).
        expect((await send(nova, startBody(crypto.randomUUID(), { challenge_id: id, level: community }))).status).toBe(409);
        expect((await send(nova, startBody(crypto.randomUUID(), { challenge_id: "daily-2099-01-01" }))).status).toBe(404);
    });
});
