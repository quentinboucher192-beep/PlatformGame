// Clan raids « Asterion », phase 7 (2.74, docs/RAIDS.md §15 « Fiabilité, charge et anti-triche »): the
// relay of an instance (CoopRoom in raid mode) under real conditions. Eight games over WebSockets
// with latency, jitter and inputs out of order get the same frames; a game that loses its link comes
// back within 60 s to the same game (history, catch-up, REJOIN), even when it was the only one; the
// seat is freed after 60 s; a late join takes a vacant seat; nobody is the host; a second connection
// of the same account takes its seat back; the object restarted mid-raid comes back from storage.
// Before 2.74, the last player leaving made the relay give standby frames to nobody up to the 2 h
// limit (the object's CPU and memory exhausted) and forget the game at once.

import { env, SELF, abortAllDurableObjects, runDurableObjectAlarm, runInDurableObject } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { FLAG_STANDBY, FLAG_VACANT, HISTORY_CHUNK, MAX_RAID_PLAYERS, Msg, decodeHistory, emptyFrame, encodeHistory, readFrames, type Frame } from "../src/coop";
import { api, freshIp, registerPlayer, seedAll, type TestPlayer } from "./helpers";
import { raidGroups } from "./raidGroups";

beforeEach(seedAll);

const V = "/api/v1";
const EMBLEM = { shape: "star", primary: "#FFD27A", secondary: "#a66bff" };
const iso = (minutes: number) => new Date(Date.now() + minutes * 60000).toISOString();

// ------------------------------------------------------------------ a clan, an event, an instance

async function clanOf(tag: string, n: number): Promise<TestPlayer[]> {
    const chief = await registerPlayer(`${tag}Chef`);
    const r = await api("POST", `${V}/clans`, { token: chief.token, body: { name: `Clan ${tag}`, tag, motto: "Ensemble.", emblem: EMBLEM, join_policy: "open" } });
    expect(r.status, JSON.stringify(r.body)).toBe(201);
    const id = r.body.data.clan.id as string;
    const players = [chief];
    for (let i = 1; i < n; ++i) {
        const p = await registerPlayer(`${tag}Mem${i}`);
        expect((await api("POST", `${V}/clans/${id}/join`, { token: p.token, body: {} })).status).toBe(200);
        players.push(p);
    }
    const now = new Date().toISOString();
    const ev = crypto.randomUUID();
    await env.DB.prepare(
        `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity, difficulty, rewards_text, published_at, created_at, updated_at)
         VALUES (?1, 'world', 'asterion', 'standard', NULL, ?2, 30, ?3, ?4, 1, 0, 50, 3, '', ?5, ?5, ?5)`,
    )
        .bind(ev, iso(-10), iso(-40), iso(50), now)
        .run();
    await env.DB.prepare("INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at) VALUES (?1, ?2, ?3, ?3, '', ?4, ?4)")
        .bind(ev, id, chief.id, now)
        .run();
    let k = 0;
    for (const p of players)
        await env.DB.prepare("INSERT INTO raid_registrations (event_id, player_id, clan_id, created_at) VALUES (?1, ?2, ?3, ?4)")
            .bind(ev, p.id, id, new Date(Date.now() - 60000 + ++k * 10).toISOString())
            .run();
    // §41: the clan's seven groups (14 registered at least), these players in the Bras droit's group.
    await raidGroups(ev, id, { bras_droit: players });
    (players as TestPlayer[] & { event?: string }).event = ev;
    return players;
}

async function instanceFor(players: TestPlayer[]): Promise<string> {
    const ev = (players as TestPlayer[] & { event?: string }).event!;
    const r = await api("POST", `${V}/raids/events/${ev}/instance`, { token: players[0]!.token, body: {} });
    expect(r.status, JSON.stringify(r.body)).toBe(200);
    return r.body.data.instance.id as string;
}

const room = (inst: string) => env.COOP_ROOMS.get(env.COOP_ROOMS.idFromName(inst));
/** Reaches into a room (its private state), as the platform would see it. */
const inRoom = runInDurableObject as unknown as (stub: unknown, cb: (room: any) => void) => Promise<void>;
async function status(inst: string): Promise<any> {
    return await (await room(inst).fetch("https://coop/status")).json();
}

// ------------------------------------------------------------------ games on the wire

const hello = (account = "") => {
    const name = new TextEncoder().encode("Test");
    const acc = new TextEncoder().encode(account);
    return Uint8Array.from([Msg.Hello, 1, 0, name.length, ...name, ...(account ? [acc.length, ...acc] : [])]);
};
const start = () => Uint8Array.from([Msg.Start, 8]);
const u32 = (v: number) => [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255];
const catchupDone = (t: number) => Uint8Array.from([Msg.CatchupDone, ...u32(t)]);
const ping = () => Uint8Array.from([Msg.Ping, ...u32(1234), 20, 0]);

/** INPUT: `frames` from step `first`, written against the previous one (the game's writeFrames). */
function input(first: number, frames: Frame[]): Uint8Array {
    const out: number[] = [Msg.Input, ...u32(first), frames.length];
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
        out.push(m);
        if (m & 1) out.push(...u32(f.held));
        if (m & 2) out.push(...u32(f.pressed));
        if (m & 4) out.push(...u32(f.released));
        if (m & 8) out.push(f.mx & 255);
        if (m & 16) out.push(f.my & 255);
        if (m & 32) out.push(f.flags);
        if (m & 64) out.push(f.choice);
        prev = f;
    }
    return Uint8Array.from(out);
}

/** Seat k's scripted input at step t (distinct for every seat and step). */
const scripted = (k: number, t: number): Frame => ({ ...emptyFrame(), held: ((t >> 3) + k) % 2 ? 2 : 1, mx: ((t + k * 13) % 200) - 100, my: 0 });

class Game {
    got: Uint8Array[] = [];
    closed = false;
    constructor(public ws: WebSocket) {
        ws.accept();
        ws.binaryType = "arraybuffer";
        ws.addEventListener("message", (ev) => this.got.push(new Uint8Array(ev.data as ArrayBuffer)));
        ws.addEventListener("close", () => (this.closed = true));
        ws.addEventListener("error", () => (this.closed = true));
    }
    send(b: Uint8Array) {
        if (!this.closed) this.ws.send(b);
    }
    /** How many steps were given so far (the last FRAMES: its first step + its count). */
    count(): number {
        const m = this.of(Msg.Frames).at(-1);
        if (!m) return 0;
        const v = new DataView(m.buffer, m.byteOffset, m.byteLength);
        return v.getUint32(1, true) + v.getUint16(5, true);
    }
    of(type: number) {
        return this.got.filter((m) => m[0] === type);
    }
    seat(): number {
        return this.of(Msg.Welcome).at(-1)?.[1] ?? -1;
    }
    delay(): number {
        return this.of(Msg.Go)[0]?.[10] ?? 0;
    }
    rejoinOf(seat: number): number {
        for (const m of this.of(Msg.Rejoin)) if (m[1] === seat) return new DataView(m.buffer, m.byteOffset).getUint32(2, true);
        return 0;
    }
    /** Every team frame received, step by step (8 frames each), in the order of the relay. */
    frames(players = MAX_RAID_PLAYERS): Frame[][] {
        const out: Frame[][] = [];
        for (const m of this.of(Msg.Frames)) {
            const v = new DataView(m.buffer, m.byteOffset, m.byteLength);
            const first = v.getUint32(1, true);
            const n = v.getUint16(5, true);
            const reader = {
                b: m,
                at: 7,
                ok: true,
                need(k: number) {
                    if (!this.ok || this.at + k > this.b.length) this.ok = false;
                    return this.ok;
                },
                u8() {
                    return this.need(1) ? this.b[this.at++]! : 0;
                },
                u32() {
                    return (this.u8() | (this.u8() << 8) | (this.u8() << 16) | (this.u8() << 24)) >>> 0;
                },
                i8() {
                    const x = this.u8();
                    return x > 127 ? x - 256 : x;
                },
            };
            const seats: Frame[][] = [];
            for (let s = 0; s < players; ++s) seats.push(readFrames(reader as never, n)!);
            for (let i = 0; i < n; ++i) out[first + i] = seats.map((f) => f[i]!);
        }
        return out;
    }
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
async function until(cond: () => boolean | Promise<boolean>, ms = 3000): Promise<void> {
    const end = Date.now() + ms;
    while (!(await cond())) {
        if (Date.now() > end) throw new Error("condition not reached in time");
        await sleep(10);
    }
}

async function connect(p: TestPlayer, inst: string, headers: Record<string, string> = {}): Promise<Game> {
    const res = await SELF.fetch(`http://cosmic.test${V}/raids/instances/${inst}/socket`, {
        headers: { upgrade: "websocket", authorization: `Bearer ${p.token}`, "cf-connecting-ip": freshIp(), ...headers },
    });
    expect(res.status).toBe(101);
    return new Game(res.webSocket!);
}

/** The games say HELLO in turn, the first starts the instance; the GO's input delay. */
async function startAll(games: Game[]): Promise<number> {
    for (const g of games) {
        g.send(hello());
        await until(() => g.seat() >= 0);
    }
    games[0]!.send(start());
    await until(() => games.every((g) => g.of(Msg.Go).length === 1));
    return games[0]!.delay();
}

/** Each game sends its seat's inputs for steps [from, to) in batches of `batch` (never more than 64 ahead). */
async function play(games: Game[], from: number, to: number, batch = 32) {
    for (let t = from; t < to; t += batch) {
        const n = Math.min(batch, to - t);
        for (const g of games) {
            const k = g.seat();
            g.send(input(t, Array.from({ length: n }, (_, i) => scripted(k, t + i))));
        }
        if (((t - from) / batch) % 8 === 7) await sleep(15); // under the relay's 200 messages a second
    }
}

async function memberRow(inst: string, p: TestPlayer) {
    return await env.DB.prepare("SELECT seat, active, left_at, seat_from FROM raid_instance_members WHERE instance_id = ?1 AND player_id = ?2")
        .bind(inst, p.id)
        .first<{ seat: number; active: number; left_at: string | null; seat_from: number | null }>();
}

// ------------------------------------------------------------------ tests

describe("raids phase 7: the history stored by parts", () => {
    it("is compact and read back exactly; a damaged part is refused", () => {
        const players = 8;
        const steps = 300;
        const history: Frame[] = [];
        for (let t = 0; t < steps; ++t) for (let k = 0; k < players; ++k) history.push(k === 7 ? { ...emptyFrame(), flags: FLAG_VACANT } : scripted(k, t));
        const bytes = encodeHistory(history, 100, 200, players);
        expect(bytes.length).toBeLessThan(200 * players * 6); // a repeated input costs one byte
        expect(decodeHistory(bytes, 200, players)).toEqual(history.slice(100 * players, 300 * players));
        expect(decodeHistory(bytes.subarray(0, bytes.length - 1), 200, players)).toBeNull();
        expect(decodeHistory(Uint8Array.from([...bytes, 0]), 200, players)).toBeNull();
        expect(HISTORY_CHUNK * 8 * 17).toBeLessThan(128 * 1024); // the worst part fits in one stored value
    });
});

describe("raids phase 7: the relay of an instance under real conditions", () => {
    it("eight games, latency, jitter and inputs out of order: the same frames for all, every input in its step", async () => {
        const players = await clanOf("OCTO", 8);
        const inst = await instanceFor(players);
        const games = await Promise.all(players.map((p) => connect(p, inst)));
        const delay = await startAll(games);
        expect(new Set(games.map((g) => g.seat())).size).toBe(8);
        // Each game sends its batches on its own clock: 0 to 25 ms of delay each (jitter), and one
        // batch out of three before the previous one (out of order). Like a real game, it never
        // sends further than its input delay ahead of the steps it was given (the relay's window).
        const END = delay + 600;
        await Promise.all(
            games.map(async (g, gi) => {
                const k = g.seat();
                const batches: [number, number][] = [];
                for (let t = delay; t < END; t += 24) batches.push([t, Math.min(24, END - t)]);
                for (let i = 0; i < batches.length; ++i) {
                    if (i % 3 === 2) continue; // already sent before the previous one
                    const order = i % 3 === 1 && i + 1 < batches.length ? [batches[i + 1]!, batches[i]!] : [batches[i]!];
                    const reach = Math.max(...order.map(([t, n]) => t + n));
                    await until(() => g.count() >= reach - 60, 5000);
                    for (const [t, n] of order) g.send(input(t, Array.from({ length: n }, (_, j) => scripted(k, t + j))));
                    await sleep((gi * 7 + i * 11) % 26);
                }
            }),
        );
        await until(() => games.every((g) => g.count() >= END), 5000);
        const ref = games[0]!.frames();
        for (const g of games) expect(g.frames().slice(0, END)).toEqual(ref.slice(0, END));
        for (const g of games) for (let t = delay; t < END; t += 37) expect(ref[t]![g.seat()]).toEqual(scripted(g.seat(), t));
        expect((await status(inst)).seats.filter((s: { connected: boolean }) => s.connected).length).toBe(8);
        for (const g of games) g.ws.close();
    });

    it("the last player leaves: the steps stand still (never standby frames given to nobody), the game waits 60 s for them", async () => {
        const players = await clanOf("SOLO", 1);
        const inst = await instanceFor(players);
        const g = await connect(players[0]!, inst);
        const delay = await startAll([g]);
        await play([g], delay, 400);
        await until(() => g.frames().length >= 400);
        g.ws.close();
        await until(async () => (await status(inst)).seats[0].absent === true);
        await sleep(50);
        const st = await status(inst);
        expect(st.started).toBe(true); // kept for the return
        expect(st.next).toBeLessThan(500); // before 2.74: 864 000 (the 2 h limit), then forgotten
        expect((await memberRow(inst, players[0]!))!.active).toBe(0);
        // Back within 60 s: the same game, its history, its step of return.
        const back = await connect(players[0]!, inst);
        back.send(hello());
        await until(() => back.frames().length >= 400);
        expect(back.seat()).toBe(0);
        expect(back.frames().slice(0, 400)).toEqual(g.frames().slice(0, 400));
        back.send(catchupDone(st.next - 1));
        await until(() => back.rejoinOf(0) > 0);
        const at = back.rejoinOf(0);
        expect(at).toBe(st.next + back.delay() + 1);
        await until(() => back.frames().length >= at); // up to its return, given at once (nobody else plays)
        await play([back], at, at + 64);
        await until(() => back.frames().length >= at + 64);
        const f = back.frames();
        expect(f[at - 1]![0]!.flags).toBe(FLAG_STANDBY);
        expect(f[at]![0]).toEqual(scripted(0, at));
        await until(async () => (await memberRow(inst, players[0]!))!.active === 1);
        // Gone again for 60 s on the clock while nobody plays: the seat is freed and the game ends.
        back.ws.close();
        await until(async () => (await status(inst)).seats[0].absent === true);
        await inRoom(room(inst), (r) => {
            r.seats[0].absentAt -= 61_000;
        });
        expect(await runDurableObjectAlarm(room(inst))).toBe(true);
        const end = await status(inst);
        expect(end.started).toBe(false);
        expect(end.exists).toBe(true); // the room stays (a new game starts when someone comes)
        await until(async () => (await memberRow(inst, players[0]!))!.left_at !== null);
        expect((await memberRow(inst, players[0]!))!.active).toBe(0);
    });

    it("after 60 s of steps the seat is freed (vacant frames); a late join takes it and holds it from its step of return", async () => {
        const players = await clanOf("DUO", 3);
        const inst = await instanceFor(players);
        const [a, b] = await Promise.all([connect(players[0]!, inst), connect(players[1]!, inst)]);
        const delay = await startAll([a, b]);
        await play([a, b], delay, 200);
        b.ws.close();
        await until(async () => (await status(inst)).seats[b.seat()].absent === true);
        const gone = (await status(inst)).next;
        // A alone plays a bit more than 60 s of steps.
        for (let t = 200; t < gone + 7300; t += 64 * 8) {
            await play([a], t, Math.min(t + 64 * 8, gone + 7300), 64);
            await until(() => a.count() >= Math.min(t + 64 * 8, gone + 7300));
        }
        const f = a.frames();
        expect(f[gone + 10]![1]!.flags).toBe(FLAG_STANDBY);
        expect(f.at(-1)![1]!.flags).toBe(FLAG_VACANT);
        await until(async () => (await memberRow(inst, players[1]!))!.left_at !== null);
        // A third member arrives late: the vacant seat 1, the states then the history, vacant until its return.
        const c = await connect(players[2]!, inst);
        c.send(hello());
        await until(() => c.frames().length >= f.length);
        expect(c.seat()).toBe(1);
        const order = c.got.map((m) => m[0]);
        expect(order.indexOf(Msg.RaidState)).toBeLessThan(order.indexOf(Msg.Frames));
        const last = (await status(inst)).next;
        c.send(catchupDone(last - 1));
        await until(() => a.rejoinOf(1) > 0);
        const join = a.rejoinOf(1);
        await play([a], last, join + 16, 16);
        await play([c], join, join + 16, 16);
        await until(() => a.frames().length >= join + 16);
        const g = a.frames();
        expect(g[join - 1]![1]!.flags).toBe(FLAG_VACANT);
        expect(g[join]![1]).toEqual(scripted(1, join));
        await until(async () => (await memberRow(inst, players[2]!))?.seat_from === join);
        expect((await memberRow(inst, players[2]!))!.active).toBe(1);
        a.ws.close();
        c.ws.close();
    });

    it("nobody is the host: the player who started leaves, the others go on; a second link of the same account takes its seat back", async () => {
        const players = await clanOf("TRIO", 3);
        const inst = await instanceFor(players);
        const games = await Promise.all(players.map((p) => connect(p, inst)));
        const delay = await startAll(games);
        await play(games, delay, 120);
        await until(() => games[2]!.frames().length >= 120);
        games[0]!.ws.close(); // the one who started
        await until(async () => (await status(inst)).seats[games[0]!.seat()].absent === true);
        await play(games.slice(1), 120, 240);
        await until(() => games[1]!.frames().length >= 240);
        expect(games[1]!.of(Msg.End)).toHaveLength(0);
        // Player 3's link dies silently and its game connects again at once: the new link wins.
        const again = await connect(players[2]!, inst);
        again.send(hello());
        await until(() => again.of(Msg.Go).length === 1 && games[2]!.closed);
        expect(again.seat()).toBe(games[2]!.seat());
        const st = await status(inst);
        expect(st.seats[again.seat()].absent).toBe(true);
        again.send(catchupDone(st.next - 1));
        await until(() => again.rejoinOf(again.seat()) > 0);
        expect(again.of(Msg.End)).toHaveLength(0);
        games[1]!.ws.close();
        again.ws.close();
    });

    it("the session decides who plays: a forged player header or another account in HELLO changes nothing", async () => {
        const players = await clanOf("VRAI", 2);
        const inst = await instanceFor(players);
        const a = await connect(players[0]!, inst);
        const delay = await startAll([a]);
        expect(delay).toBeGreaterThanOrEqual(3);
        // Player 2 claims to be player 1 (header and HELLO's account) and seat 0.
        const b = await connect(players[1]!, inst, { "x-player-id": players[0]!.id, "x-player-name": "Imposteur" });
        b.send(hello(players[0]!.id));
        await until(() => b.seat() >= 0);
        expect(b.seat()).toBe(1); // a vacant seat as a late join, never player 1's
        expect(a.closed).toBe(false); // player 1's link is not replaced by somebody else
        const st = await status(inst);
        expect(st.seats[1].player_id).toBe(players[1]!.id);
        expect(st.seats[0].player_id).toBe(players[0]!.id);
        // A flood (more than 200 messages a second) closes the socket.
        for (let i = 0; i < 230; ++i) b.send(ping());
        await until(() => b.closed);
        a.ws.close();
    });

    it("the object restarted mid-raid: the game comes back from storage, every player returns to it", async () => {
        const players = await clanOf("FENIX", 2);
        const inst = await instanceFor(players);
        const [a, b] = await Promise.all([connect(players[0]!, inst), connect(players[1]!, inst)]);
        const delay = await startAll([a, b]);
        const END = HISTORY_CHUNK * 2 + 150; // two whole parts and the start of a third
        for (let t = delay; t < END; t += 32 * 6) {
            await play([a, b], t, Math.min(t + 32 * 6, END));
            await until(() => a.frames().length >= Math.min(t + 32 * 6, END));
        }
        expect(await runDurableObjectAlarm(room(inst))).toBe(true); // the store of every second
        const before = a.frames();
        expect((await status(inst)).raid.stored).toBe(END);
        // The object restarts (a deployment): memory lost, links closed, storage kept.
        await abortAllDurableObjects();
        await until(() => a.closed && b.closed);
        const st = await status(inst);
        expect(st.started).toBe(true);
        expect(st.next).toBe(END);
        expect(st.raid.restores).toBe(1);
        expect(st.seats.slice(0, 2).every((s: { absent: boolean }) => s.absent)).toBe(true);
        expect((await memberRow(inst, players[0]!))!.active).toBe(0); // the consensus no longer counts them
        // Both come back: the same history, then their returns.
        const [a2, b2] = await Promise.all([connect(players[0]!, inst), connect(players[1]!, inst)]);
        a2.send(hello());
        b2.send(hello());
        await until(() => a2.frames().length >= END && b2.frames().length >= END);
        expect(a2.seat()).toBe(a.seat());
        expect(b2.seat()).toBe(b.seat());
        expect(a2.frames()).toEqual(before);
        expect(b2.frames()).toEqual(before);
        a2.send(catchupDone(END - 1));
        b2.send(catchupDone(END - 1));
        await until(() => a2.rejoinOf(a2.seat()) > 0 && a2.rejoinOf(b2.seat()) > 0);
        const at = Math.max(a2.rejoinOf(a2.seat()), a2.rejoinOf(b2.seat()));
        await play([a2, b2], Math.min(a2.rejoinOf(a2.seat()), a2.rejoinOf(b2.seat())), at + 32);
        await until(() => a2.frames().length >= at + 32);
        expect(a2.frames()[at + 5]![a2.seat()]).toEqual(scripted(a2.seat(), at + 5));
        expect(a2.frames()).toEqual(b2.frames());
        await until(async () => (await memberRow(inst, players[1]!))!.active === 1);
        const reconnects = await env.DB.prepare("SELECT reconnects FROM raid_instances WHERE id = ?1").bind(inst).first<{ reconnects: number }>();
        expect(reconnects!.reconnects).toBeGreaterThanOrEqual(2); // counted by the database (0 → 1)
        a2.ws.close();
        b2.ws.close();
    });

    it("a ninth player is refused while the eight seats are held", async () => {
        const players = await clanOf("NEUF", 9);
        const ev = (players as TestPlayer[] & { event?: string }).event!;
        const r = await api("POST", `${V}/raids/events/${ev}/instance`, { token: players[0]!.token, body: {} });
        const inst = r.body.data.instance.id as string;
        const games = await Promise.all(players.slice(0, 8).map((p) => connect(p, inst)));
        await startAll(games);
        const ninth = await connect(players[8]!, inst);
        ninth.send(hello());
        await until(() => ninth.of(Msg.End).length === 1);
        expect(ninth.of(Msg.End)[0]![1]).toBe(3); // Refused
        expect(ninth.seat()).toBe(-1);
        for (const g of games) g.ws.close();
    });
});
