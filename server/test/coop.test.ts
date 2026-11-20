// Cooperation (2.62, docs/COOP.md): the rooms (host, invited friends only), the relay over
// WebSockets (it orders the inputs, gives everyone the same team frames, decides the standby
// frames of an absent player alone, takes a game back after it caught up), what it refuses
// (bad bits, the Standby flag, unknown messages), and the session it notes for the ranking.

import { env, SELF } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { ACTION_MASK, EndReason, FLAG_STANDBY, Msg, canonical, emptyFrame, readFrames, writeFrames, type Frame } from "../src/coop";
import { COOP_SIMULATION_VERSION } from "../src/coopRace";
import { EXIT, LEVEL_HASH, SPAWN, api, freshIp, registerPlayer, seedAll, type TestPlayer } from "./helpers";
import { LH, PNG, octet, publish, zip } from "./publish";

beforeEach(seedAll);

const V = "/api/v1";

async function befriend(a: TestPlayer, b: TestPlayer): Promise<void> {
    expect((await api("POST", `${V}/friends/request`, { token: a.token, body: { player_id: b.id } })).status).toBe(201);
    expect((await api("POST", `${V}/friends/accept`, { token: b.token, body: { player_id: a.id } })).status).toBe(200);
}

// ------------------------------------------------------------------ bytes

class Bytes {
    b: number[] = [];
    u8(v: number) {
        this.b.push(v & 255);
        return this;
    }
    u16(v: number) {
        return this.u8(v).u8(v >>> 8);
    }
    u32(v: number) {
        for (let i = 0; i < 4; ++i) this.u8(v >>> (8 * i));
        return this;
    }
    str(s: string) {
        const e = new TextEncoder().encode(s);
        this.u8(e.length);
        for (const x of e) this.u8(x);
        return this;
    }
    done() {
        return Uint8Array.from(this.b);
    }
}

const hello = () => new Bytes().u8(Msg.Hello).u8(1).u8(255).str("Test").done();
/** 2.80: a game that says its rules of cooperation (after its account, empty here). */
const helloRules = (rules: number) => new Bytes().u8(Msg.Hello).u8(1).u8(255).str("Test").str("").u8(rules).done();
const ping = (rtt: number) => new Bytes().u8(Msg.Ping).u32(1234).u16(rtt).done();
const ready = () => new Bytes().u8(Msg.Ready).u8(1).done();
const start = () => new Bytes().u8(Msg.Start).u8(2).done();
function input(first: number, frames: Frame[]): Uint8Array {
    const head = new Bytes().u8(Msg.Input).u32(first).u8(frames.length).done();
    // writeFrames writes into the server's own writer: rebuild it here with the same rules.
    const body: number[] = [];
    let prev = emptyFrame();
    for (const f of frames) {
        let m = 0;
        if (f.held !== prev.held) m |= 1;
        if (f.pressed) m |= 2;
        if (f.released) m |= 4;
        if (f.mx !== prev.mx) m |= 8;
        if (f.my !== prev.my) m |= 16;
        if (f.flags) m |= 32;
        if (f.choice) m |= 64;
        body.push(m);
        const u32 = (v: number) => {
            for (let i = 0; i < 4; ++i) body.push((v >>> (8 * i)) & 255);
        };
        if (m & 1) u32(f.held);
        if (m & 2) u32(f.pressed);
        if (m & 4) u32(f.released);
        if (m & 8) body.push(f.mx & 255);
        if (m & 16) body.push(f.my & 255);
        if (m & 32) body.push(f.flags);
        if (m & 64) body.push(f.choice);
        prev = f;
    }
    return Uint8Array.from([...head, ...body]);
}

/** A game's socket: what it received, by type. */
class Game {
    got: Uint8Array[] = [];
    closed = false;
    constructor(public ws: WebSocket) {
        ws.accept();
        ws.binaryType = "arraybuffer";
        ws.addEventListener("message", (ev) => {
            this.got.push(new Uint8Array(ev.data as ArrayBuffer));
        });
        ws.addEventListener("close", () => {
            this.closed = true;
        });
    }
    send(b: Uint8Array) {
        this.ws.send(b);
    }
    of(type: number) {
        return this.got.filter((m) => m[0] === type);
    }
    /** Every team frame received, step by step (players frames each). */
    frames(players: number): Frame[][] {
        const out: Frame[][] = [];
        for (const m of this.of(Msg.Frames)) {
            const v = new DataView(m.buffer, m.byteOffset, m.byteLength);
            const first = v.getUint32(1, true);
            const n = v.getUint16(5, true);
            const r = { b: m, at: 7, ok: true } as any;
            // the reader of the server module needs its class: reuse readFrames through a shim
            const seats: Frame[][] = [];
            const reader = new (class {
                at = 7;
                ok = true;
                b = m;
                need(k: number) {
                    if (!this.ok || this.at + k > this.b.length) this.ok = false;
                    return this.ok;
                }
                u8() {
                    return this.need(1) ? this.b[this.at++]! : 0;
                }
                u32() {
                    return (this.u8() | (this.u8() << 8) | (this.u8() << 16) | (this.u8() << 24)) >>> 0;
                }
                i8() {
                    const x = this.u8();
                    return x > 127 ? x - 256 : x;
                }
            })();
            void r;
            for (let s = 0; s < players; ++s) seats.push(readFrames(reader as any, n)!);
            for (let i = 0; i < n; ++i) out[first + i] = seats.map((f) => f[i]!);
        }
        return out;
    }
}

const settle = () => new Promise((r) => setTimeout(r, 20));

async function connect(p: TestPlayer, room: string): Promise<Game> {
    const res = await SELF.fetch(`http://cosmic.test${V}/coop/rooms/${room}/socket`, {
        headers: { upgrade: "websocket", authorization: `Bearer ${p.token}`, "cf-connecting-ip": freshIp() },
    });
    expect(res.status).toBe(101);
    return new Game(res.webSocket!);
}

async function roomOf(host: TestPlayer): Promise<string> {
    const r = await api("POST", `${V}/coop/rooms`, { token: host.token, body: { level: { source: "campaign", id: "test_01", hash: LEVEL_HASH, title: "Niveau test" } } });
    expect(r.status).toBe(201);
    return r.body.data.room.id as string;
}

const RIGHT: Frame = { ...emptyFrame(), held: 2, mx: 127 };

describe("coop: frames on the wire", () => {
    it("are compact and read back exactly; bad bits and flags are refused", () => {
        const frames: Frame[] = [RIGHT, RIGHT, { ...RIGHT, pressed: 16, held: 18 }, RIGHT, emptyFrame()];
        const o = { bytes: [] as number[], u8(v: number) { this.bytes.push(v & 255); }, u32(v: number) { for (let i = 0; i < 4; ++i) this.u8(v >>> (8 * i)); } };
        writeFrames(o as any, frames);
        // A repeated step costs one byte.
        expect(o.bytes.length).toBeLessThan(frames.length * 6);
        const reader = { b: Uint8Array.from(o.bytes), at: 0, ok: true, need(k: number) { if (!this.ok || this.at + k > this.b.length) this.ok = false; return this.ok; }, u8() { return this.need(1) ? this.b[this.at++]! : 0; }, u32() { return (this.u8() | (this.u8() << 8) | (this.u8() << 16) | (this.u8() << 24)) >>> 0; }, i8() { const x = this.u8(); return x > 127 ? x - 256 : x; } };
        expect(readFrames(reader as any, frames.length)).toEqual(frames);
        const bad = { ...reader, b: Uint8Array.from([1, 0, 0, 0, 0x80]), at: 0, ok: true };
        expect(readFrames(bad as any, 1)).toBeNull(); // an action bit beyond the recorded ones
        expect(ACTION_MASK).toBe((1 << 21) - 1);
        expect(canonical([RIGHT]).length).toBe(16);
    });
});

describe("coop: rooms", () => {
    it("only the host invites, only an accepted friend, who gets a notification", async () => {
        const nova = await registerPlayer("Nova");
        const lyra = await registerPlayer("Lyra");
        const intrus = await registerPlayer("Intrus");
        await befriend(nova, lyra);
        const room = await roomOf(nova);
        expect((await api("POST", `${V}/coop/rooms/${room}/invite`, { token: nova.token, body: { player_id: intrus.id } })).status).toBe(403);
        expect((await api("POST", `${V}/coop/rooms/${room}/invite`, { token: lyra.token, body: { player_id: nova.id } })).status).toBe(404);
        expect((await api("POST", `${V}/coop/rooms/${room}/invite`, { token: nova.token, body: { player_id: lyra.id } })).status).toBe(200);
        const inv = await api("GET", `${V}/coop/invitations`, { token: lyra.token });
        expect(inv.body.data.invitations).toHaveLength(1);
        expect(inv.body.data.invitations[0].room).toBe(room);
        // Nobody else gets into the room.
        const res = await SELF.fetch(`http://cosmic.test${V}/coop/rooms/${room}/socket`, {
            headers: { upgrade: "websocket", authorization: `Bearer ${intrus.token}`, "cf-connecting-ip": freshIp() },
        });
        expect(res.status).toBe(404);
    });
});

describe("coop: the relay", () => {
    it("orders the inputs, gives everyone the same frames, takes an absent player back", async () => {
        const nova = await registerPlayer("Nova");
        const lyra = await registerPlayer("Lyra");
        await befriend(nova, lyra);
        const room = await roomOf(nova);
        await api("POST", `${V}/coop/rooms/${room}/invite`, { token: nova.token, body: { player_id: lyra.id } });
        const a = await connect(nova, room);
        const b = await connect(lyra, room);
        a.send(hello());
        await settle();
        b.send(hello());
        await settle();
        expect(a.of(Msg.Welcome)[0]![1]).toBe(0); // the host has the first seat
        expect(b.of(Msg.Welcome).at(-1)![1]).toBe(1);
        a.send(ping(40));
        b.send(ping(60));
        b.send(ready());
        await settle();
        a.send(start());
        await settle();
        const go = a.of(Msg.Go)[0]!;
        expect(go).toBeDefined();
        const delay = go[10]!;
        expect(delay).toBeGreaterThanOrEqual(3);
        // Each game sends its inputs from the step `delay` on.
        a.send(input(delay, [RIGHT, RIGHT, RIGHT]));
        await settle();
        expect(a.frames(2).length).toBe(delay); // the empty steps only: player 2 is missing
        b.send(input(delay, [emptyFrame(), emptyFrame()]));
        await settle();
        const fa = a.frames(2);
        const fb = b.frames(2);
        expect(fa).toEqual(fb);
        expect(fa.length).toBe(delay + 2);
        expect(fa[delay]![0]).toEqual(RIGHT);
        // A frame with the Standby flag from a game: refused (the socket is closed).
        b.send(input(delay + 2, [{ ...emptyFrame(), flags: FLAG_STANDBY }]));
        await settle();
        expect(b.closed).toBe(true);
        // Player 2 is absent: its frames are standby frames, decided by the relay alone.
        a.send(input(delay + 3, [RIGHT, RIGHT]));
        await settle();
        const after = a.frames(2);
        expect(after.length).toBeGreaterThan(delay + 3);
        expect(after[delay + 3]![1]!.flags).toBe(FLAG_STANDBY);
        // It comes back: the whole history, then a step of return for everyone.
        const c = await connect(lyra, room);
        c.send(hello());
        await settle();
        expect(c.of(Msg.Go)).toHaveLength(1);
        expect(c.frames(2)).toEqual(a.frames(2));
        c.send(new Bytes().u8(Msg.CatchupDone).u32(after.length - 1).done());
        await settle();
        const rejoin = a.of(Msg.Rejoin)[0]!;
        expect(rejoin[1]).toBe(1);
        const at = new DataView(rejoin.buffer, rejoin.byteOffset).getUint32(2, true);
        expect(at).toBeGreaterThan(after.length);
        // The session is noted for the ranking: the accounts by seat.
        const row = await env.DB.prepare("SELECT players, size FROM coop_sessions").first<{ players: string; size: number }>();
        expect(row?.size).toBe(2);
        expect(JSON.parse(row!.players)).toEqual([nova.id, lyra.id]);
    });
});

describe("coop: one version of the rules of cooperation per room (2.80)", () => {
    it("the room plays its host's rules: a game of another version is refused (End « Rules »), the same one is seated", async () => {
        expect(COOP_SIMULATION_VERSION).toBe(4);
        expect(EndReason.Rules).toBe(4);
        const nova = await registerPlayer("Nova");
        const lyra = await registerPlayer("Lyra");
        const vega = await registerPlayer("Vega");
        await befriend(nova, lyra);
        await befriend(nova, vega);
        const room = await roomOf(nova);
        await api("POST", `${V}/coop/rooms/${room}/invite`, { token: nova.token, body: { player_id: lyra.id } });
        await api("POST", `${V}/coop/rooms/${room}/invite`, { token: nova.token, body: { player_id: vega.id } });
        const a = await connect(nova, room);
        a.send(helloRules(COOP_SIMULATION_VERSION));
        await settle();
        expect(a.of(Msg.Welcome)[0]![1]).toBe(0);
        // A game before 2.80 (no rules in its HELLO: 3 at most): refused, told why.
        const b = await connect(lyra, room);
        b.send(hello());
        await settle();
        expect(b.of(Msg.Welcome)).toHaveLength(0);
        expect(b.of(Msg.End)[0]![1]).toBe(EndReason.Rules);
        // A game of the same rules: seated.
        const c = await connect(vega, room);
        c.send(helloRules(COOP_SIMULATION_VERSION));
        await settle();
        expect(c.of(Msg.Welcome).at(-1)![1]).toBe(1);
    });

    it("a room of a game before 2.80 (rules 3) refuses a game of rules 4", async () => {
        const nova = await registerPlayer("Nova");
        const lyra = await registerPlayer("Lyra");
        await befriend(nova, lyra);
        const room = await roomOf(nova);
        await api("POST", `${V}/coop/rooms/${room}/invite`, { token: nova.token, body: { player_id: lyra.id } });
        const a = await connect(nova, room);
        a.send(hello());
        await settle();
        expect(a.of(Msg.Welcome)[0]![1]).toBe(0);
        const b = await connect(lyra, room);
        b.send(helloRules(4));
        await settle();
        expect(b.of(Msg.End)[0]![1]).toBe(EndReason.Rules);
    });
});

/** An EXTRÊME (one life) community level, published (version 1, fingerprint LH). */
async function publishExtreme(author: TestPlayer): Promise<string> {
    const route = Array.from({ length: 60 }, (_, i) => [SPAWN.x + ((EXIT.x + 10 - SPAWN.x) * i) / 59, SPAWN.y + ((EXIT.y + 40 - SPAWN.y) * i) / 59]);
    const start = await api("POST", `${V}/community/levels`, {
        token: author.token,
        body: { title: "Le Gouffre Final", planet: "pyralis", difficulty: 5, level_hash: LH, min_game_version: "2.16.0", race: { route, bosses: [], ambushes: [] } },
    });
    expect(start.status).toBe(201);
    const level = JSON.stringify({
        format: "cosmic.level",
        id: "mon_niveau",
        name: "Mon niveau",
        extreme: true,
        lives: 1,
        bounds: [0, 0, 6000, 1200],
        timer: { mode: "limited", limit: 600 },
        objects: [
            { id: "o1", type: "player_start", rect: [SPAWN.x - 5, SPAWN.y - 22, 10, 22] },
            { id: "o2", type: "exit", rect: [EXIT.x, EXIT.y, EXIT.w, EXIT.h] },
        ],
    });
    const data = await zip({
        "COSMIC_LEVEL/manifest.json": JSON.stringify({ format: "cosmic.package", levelId: "mon_niveau" }),
        "COSMIC_LEVEL/level.json": level,
        "COSMIC_LEVEL/metadata/thumbnail.png": PNG,
    });
    const p = await api("PUT", start.body.data.upload.package, { token: author.token, raw: data, headers: octet });
    expect(p.status).toBeLessThan(300);
    return start.body.data.id as string;
}

describe("coop: a community EXTRÊME level (2.63.5)", () => {
    it("only its published version, by its fingerprint; the lobby gives its version so a guest downloads exactly it", async () => {
        const nova = await registerPlayer("Nova");
        const levelId = await publishExtreme(nova);
        const room = (level: Record<string, unknown>) => api("POST", `${V}/coop/rooms`, { token: nova.token, body: { level } });
        // Another fingerprint than the version's: refused (another copy would not play the same run).
        expect((await room({ source: "community", id: levelId, version: 1, hash: "f".repeat(64), title: "Copie" })).status).toBe(409);
        // A version that does not exist, no version, a campaign id: refused.
        expect((await room({ source: "community", id: levelId, version: 2, hash: LH, title: "Copie" })).status).toBe(404);
        expect((await room({ source: "community", id: levelId, hash: LH, title: "Copie" })).status).toBe(400);
        expect((await room({ source: "community", id: "test_01", version: 1, hash: LH, title: "Copie" })).status).toBe(400);
        // A community level that is not EXTRÊME: no co-op.
        const plain = await publish(nova, { title: "Niveau tranquille" });
        expect((await room({ source: "community", id: plain.id, version: plain.version, hash: LH, title: "Copie" })).status).toBe(400);
        const ok = await room({ source: "community", id: levelId, version: 1, hash: LH, title: "Titre venu du jeu" });
        expect(ok.status).toBe(201);
        expect(ok.body.data.room.level.title).toBe("Le Gouffre Final"); // the server's, never the game's
        expect(ok.body.data.room.level.version).toBe(1);
        const a = await connect(nova, ok.body.data.room.id as string);
        a.send(hello());
        await settle();
        const lobby = JSON.parse(new TextDecoder().decode(a.of(Msg.Lobby).at(-1)!.subarray(1)));
        expect(lobby.source).toBe("community");
        expect(lobby.level).toBe(levelId);
        expect(lobby.version).toBe(1);
        expect(lobby.hash).toBe(LH);
        expect(lobby.size).toBeGreaterThan(0);
        // The Duo / Trio boards of this version exist (kind community), empty for now.
        const board = await api("GET", `${V}/coop/board?size=2&kind=community&level_id=${levelId}&version=1`);
        expect(board.status).toBe(200);
        expect(board.body.data.race.kind).toBe("community");
        expect(board.body.data.race.version).toBe(1);
        expect(board.body.data.ranking).toEqual([]);
    });
});
