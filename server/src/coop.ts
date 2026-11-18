// Cooperation on the EXTRÊME levels (2.62, docs/COOP.md): the online relay of a team.
//
// A host creates a room for an EXTRÊME level and invites accepted friends (a « coop_invite »
// notification). Each game opens a WebSocket to the room (one Durable Object per room,
// CoopRoom). The room is the relay of a game in lockstep: it orders the inputs of the
// players, gives each step's team frame to everyone, and decides alone, the same for
// everyone, that an absent player's frames are standby frames (the simulation counts the
// 60 s). A game that comes back gets every step given so far, catches up, then plays again
// from a step the room chooses. Everything is in MEMORY only (bounded: 2 h of steps), and
// forgotten when the room empties. For the ranking (stage 4), the room writes in D1 which
// accounts played and the SHA-256 of the inputs it relayed (coop_sessions).
//
// 2.74 (raids, phase 7): a raid instance is not in memory only. Its game is stored (the history in
// parts of 4 s, the seats, the branch's states) every second, and comes back after a restart of its
// object; it outlives its players for 60 s (a group that lost its link comes back to the same
// game); nobody in play means the steps stand still (never standby frames given to nobody); a
// second connection of the same account takes its seat back from a link that died silently.
//
// Nothing a game sends is trusted: the messages are bounded and checked (types, lengths,
// action bits, flags — never Standby —, power choices, steps, rate), a bad one closes its
// socket. The same protocol runs on a PC of the local network (game/coop/CoopRelay.cpp).

import { DurableObject } from "cloudflare:workers";
import { requireAuth } from "./auth";
import { toHex } from "./crypto";
import { ApiError, Errors, json, log, nowIso, readJsonObject, type Bindings, type Ctx } from "./http";
import { notify } from "./notifications";
import { branchMaskOf } from "./raidZones";
import { HASH_RE, LEVEL_ID_RE, Validator } from "./validate";

export const PROTOCOL = 1;
export const MAX_PLAYERS = 3;
/** 2.65: a raid instance (raidZones.ts, docs/RAIDS.md): 8 seats, vacant ones flagged. */
export const MAX_RAID_PLAYERS = 8;
export const MAX_BATCH = 255;
export const SILENCE_MS = 3000;
export const MAX_AHEAD = 64;
export const MAX_TICKS = 120 * 7200;
export const STANDBY_LIMIT_TICKS = 60 * 120;
/** Recorded actions: MoveLeft .. PowerSlot8 (InputFrame::kActions = 21 bits). */
export const ACTION_MASK = (1 << 21) - 1;
export const FLAG_SKIP = 1;
export const FLAG_STANDBY = 2;
/** 2.65 (raids): a seat nobody holds; set by the relay only, like Standby. */
export const FLAG_VACANT = 4;
/** Messages of one socket per second at most (inputs come about 60 times a second). */
export const MAX_MESSAGES_PER_SECOND = 200;
export const MAX_MESSAGE_BYTES = 16 * 1024;
/** An invitation is valid this long. */
export const INVITE_TTL_MS = 2 * 3600 * 1000;

export const Msg = {
    Hello: 1,
    Input: 2,
    Ping: 3,
    Ready: 4,
    Start: 5,
    CatchupDone: 6,
    Leave: 7,
    Result: 8,
    Welcome: 10,
    Lobby: 11,
    Go: 12,
    Frames: 13,
    Pong: 14,
    Rejoin: 15,
    End: 16,
    RaidState: 18,
} as const;
export const EndReason = { Finished: 0, HostLeft: 1, TooLong: 2, Refused: 3 } as const;

const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

// ------------------------------------------------------------------ frames

export interface Frame {
    held: number;
    pressed: number;
    released: number;
    mx: number;
    my: number;
    flags: number;
    choice: number;
}
export const emptyFrame = (): Frame => ({ held: 0, pressed: 0, released: 0, mx: 0, my: 0, flags: 0, choice: 0 });
const validChoice = (c: number) => c === 0 || c === 255 || (c >= 1 && c <= 8);

class Out {
    bytes: number[] = [];
    u8(v: number) {
        this.bytes.push(v & 0xff);
    }
    u16(v: number) {
        this.u8(v);
        this.u8(v >>> 8);
    }
    u32(v: number) {
        for (let i = 0; i < 4; ++i) this.u8(v >>> (8 * i));
    }
    str(s: string, max = 255) {
        const b = new TextEncoder().encode(s).slice(0, max);
        this.u8(b.length);
        for (const x of b) this.u8(x);
    }
    done(): Uint8Array {
        return Uint8Array.from(this.bytes);
    }
}

class In {
    at = 0;
    ok = true;
    constructor(public b: Uint8Array) {}
    need(n: number) {
        if (!this.ok || this.at + n > this.b.length) this.ok = false;
        return this.ok;
    }
    u8() {
        return this.need(1) ? this.b[this.at++]! : 0;
    }
    u16() {
        return this.u8() | (this.u8() << 8);
    }
    u32() {
        return (this.u8() | (this.u8() << 8) | (this.u8() << 16) | (this.u8() << 24)) >>> 0;
    }
    i8() {
        const v = this.u8();
        return v > 127 ? v - 256 : v;
    }
    str(max = 255) {
        const n = this.u8();
        if (n > max || !this.need(n)) {
            this.ok = false;
            return "";
        }
        const s = new TextDecoder("utf-8", { fatal: false, ignoreBOM: true }).decode(this.b.subarray(this.at, this.at + n));
        this.at += n;
        return s;
    }
    end() {
        return this.ok && this.at === this.b.length;
    }
}

/** Frames written against the previous one (game/coop/CoopProtocol.cpp: writeFrames). */
export function writeFrames(o: Out, frames: Frame[]): void {
    let prev = emptyFrame();
    for (const f of frames) {
        let mask = 0;
        if (f.held !== prev.held) mask |= 1;
        if (f.pressed) mask |= 2;
        if (f.released) mask |= 4;
        if (f.mx !== prev.mx) mask |= 8;
        if (f.my !== prev.my) mask |= 16;
        if (f.flags) mask |= 32;
        if (f.choice) mask |= 64;
        o.u8(mask);
        if (mask & 1) o.u32(f.held);
        if (mask & 2) o.u32(f.pressed);
        if (mask & 4) o.u32(f.released);
        if (mask & 8) o.u8(f.mx);
        if (mask & 16) o.u8(f.my);
        if (mask & 32) o.u8(f.flags);
        if (mask & 64) o.u8(f.choice);
        prev = f;
    }
}

/** Reads `count` frames (null if malformed: unknown bits, flags or choices). */
export function readFrames(r: In, count: number): Frame[] | null {
    const out: Frame[] = [];
    let prev = emptyFrame();
    for (let i = 0; i < count; ++i) {
        const mask = r.u8();
        if (!r.ok || mask & 0x80) return null;
        const f: Frame = {
            held: mask & 1 ? r.u32() : prev.held,
            pressed: mask & 2 ? r.u32() : 0,
            released: mask & 4 ? r.u32() : 0,
            mx: mask & 8 ? r.i8() : prev.mx,
            my: mask & 16 ? r.i8() : prev.my,
            flags: mask & 32 ? r.u8() : 0,
            choice: mask & 64 ? r.u8() : 0,
        };
        if (!r.ok) return null;
        if ((f.held | f.pressed | f.released) & ~ACTION_MASK) return null;
        if (f.flags & ~(FLAG_SKIP | FLAG_STANDBY | FLAG_VACANT) || !validChoice(f.choice) || f.mx === -128 || f.my === -128) return null;
        out.push(f);
        prev = f;
    }
    return out;
}

/** What a game may send: never the Standby flag (the relay alone decides it). */
export function validClientFrame(f: Frame): boolean {
    return (f.flags & ~FLAG_SKIP) === 0;
}

/** The canonical bytes of frames, for the digest of the inputs played: 16 bytes each (u32 held,
 *  pressed, released, i8 moveX, moveY, u8 flags, choice), step by step, seat by seat. */
export function canonical(frames: Frame[]): Uint8Array {
    const o = new Uint8Array(frames.length * 16);
    const v = new DataView(o.buffer);
    frames.forEach((f, i) => {
        const at = i * 16;
        v.setUint32(at, f.held >>> 0, true);
        v.setUint32(at + 4, f.pressed >>> 0, true);
        v.setUint32(at + 8, f.released >>> 0, true);
        v.setInt8(at + 12, f.mx);
        v.setInt8(at + 13, f.my);
        v.setUint8(at + 14, f.flags);
        v.setUint8(at + 15, f.choice);
    });
    return o;
}

// ------------------------------------------------------------------ the room

interface Seat {
    playerId: string;
    name: string;
    ws: WebSocket | null;
    ready: boolean;
    rtt: number;
    lastSeen: number;
    absent: boolean;
    absentFrom: number;
    /** 2.74: when it became absent (ms): with nobody in play the steps stand still, the 60 s are counted on the clock. */
    absentAt: number;
    rejoinAt: number;
    pending: (Frame | null)[];
    budget: { start: number; count: number };
    /** 2.65 (raids): nobody holds this seat (Vacant frames). */
    vacant: boolean;
}

/** 2.65: what makes a room a raid instance (raidZones.ts). */
export interface RaidRoomInfo {
    instance_id: string;
    event_id: string;
    branch: string;
    revision: number;
}

interface RoomInfo {
    id: string;
    host: string;
    /** 2.63.5: a community level carries its published version and its package's size. */
    level: { source: string; id: string; hash: string; title: string; version?: number; size?: number };
    invited: string[];
    created: number;
    raid?: RaidRoomInfo;
}

/** 2.74 (raids): steps per stored part of an instance's history (4 s; at most ~65 KB written compact). */
export const HISTORY_CHUNK = 480;
/** 2.74: 60 s on the clock (a seat absent while nobody plays is freed after it). */
export const STANDBY_LIMIT_MS = 60_000;

/** 2.74: a raid instance's game as its storage keeps it (the history is in parts « h:<n> »). */
interface SavedGame {
    players: number;
    delay: number;
    attempt: string;
    next: number;
    seats: { playerId: string; name: string; vacant: boolean; absent: boolean; absentFrom: number; absentAt: number }[];
    raidStates: [number, number][];
    raidMask: number;
}

/** 2.74: `count` steps of `players` frames (step by step, seat by seat), compact: each seat's frames in turn. */
export function encodeHistory(history: Frame[], first: number, count: number, players: number): Uint8Array {
    const o = new Out();
    for (let s = 0; s < players; ++s) {
        const seat: Frame[] = [];
        for (let i = 0; i < count; ++i) seat.push(history[(first + i) * players + s]!);
        writeFrames(o, seat);
    }
    return o.done();
}

/** 2.74: the steps of a stored part (null when damaged: the instance then starts again). */
export function decodeHistory(bytes: Uint8Array, count: number, players: number): Frame[] | null {
    const r = new In(bytes);
    const seats: Frame[][] = [];
    for (let s = 0; s < players; ++s) {
        const f = readFrames(r, count);
        if (!f) return null;
        seats.push(f);
    }
    if (!r.end()) return null;
    const out: Frame[] = [];
    for (let i = 0; i < count; ++i) for (let s = 0; s < players; ++s) out.push(seats[s]![i]!);
    return out;
}

export class CoopRoom extends DurableObject<Bindings> {
    private info: RoomInfo | null = null;
    private seats: Seat[] = [];
    private started = false;
    private players = 0;
    private delay = 3;
    private seed = "";
    private attempt = "";
    private next = 0;
    private history: Frame[] = [];
    private flushed = 0;
    private resultWritten = false;
    // 2.65 (raids): the branch's state given so far ((step, mask) in order) and its last poll.
    private raidStates: [number, number][] = [];
    private raidMask = 0;
    private raidPolled = 0;
    // 2.74 (raids): the parts of the history already stored, the restarts survived, the member
    // writes in order (an « absent » never overtakes the « back » that follows it).
    private savedChunks = 0;
    private storedSteps = 0;
    private restores = 0;
    private memberWrites: Promise<void> = Promise.resolve();

    constructor(ctx: DurableObjectState, env: Bindings) {
        super(ctx, env);
        // 2.74: a raid instance survives a restart (deployment, eviction) of its object: its room and
        // its game come back from storage before any request is served.
        void ctx.blockConcurrencyWhile(() => this.restore());
    }

    async fetch(request: Request): Promise<Response> {
        const url = new URL(request.url);
        switch (url.pathname) {
            case "/create": {
                const body = (await request.json()) as RoomInfo & { host_name: string };
                if (this.info) return Response.json({ error: "exists" }, { status: 409 });
                this.info = { id: body.id, host: body.host, level: body.level, invited: [], created: Date.now(), raid: body.raid };
                if (this.info.raid) await this.ctx.storage.put("info", this.info); // 2.74: a raid room outlives its object
                return Response.json({ ok: true });
            }
            case "/raid-state": // the routes and the tests: the branch's state now (else the alarm reads it)
                await this.pollRaidState(true);
                return Response.json(this.status());
            case "/invite": {
                const body = (await request.json()) as { player_id: string };
                if (!this.info) return Response.json({ error: "gone" }, { status: 404 });
                if (!this.info.invited.includes(body.player_id) && this.info.invited.length < 20) this.info.invited.push(body.player_id);
                return Response.json({ ok: true });
            }
            case "/status":
                return Response.json(this.status());
            case "/socket":
                return this.socket(request);
            case "/test/poll": // tests: the silences as the alarm checks them
                this.poll(Number(url.searchParams.get("now") ?? Date.now()));
                return Response.json(this.status());
            default:
                return Response.json({ error: "unknown" }, { status: 404 });
        }
    }

    async alarm(): Promise<void> {
        const now = Date.now();
        this.poll(now);
        const raidGame = !!this.info?.raid && this.started;
        if (raidGame) {
            // 2.74: nobody in play: the seats absent for 60 s on the clock are freed; once every seat is
            // free and nobody is connected, the instance's game ends (the next player starts a new one).
            this.freeIdle(now);
            if (this.seats.every((s) => s.vacant && !s.ws)) {
                await this.closeGame();
                return;
            }
            await this.pollRaidState(false);
            await this.persist();
        }
        if (this.seats.some((s) => s.ws) || raidGame) await this.ctx.storage.setAlarm(now + 1000);
    }

    status() {
        return {
            exists: this.info !== null,
            host: this.info?.host ?? null,
            level: this.info?.level ?? null,
            invited: this.info?.invited ?? [],
            started: this.started,
            next: this.next,
            // 2.69: `ping` (ms, -1 unknown): the latency each game measured, for the raids' live panel (raidWorld.ts).
            seats: this.seats.map((s, i) => ({ seat: i, player_id: s.playerId, name: s.name, connected: s.ws !== null && !s.absent, absent: s.absent, vacant: s.vacant, ping: s.rtt })),
            raid: this.info?.raid ? { mask: this.raidMask, states: this.raidStates.length, restores: this.restores, stored: this.storedSteps } : null,
        };
    }

    private socket(request: Request): Response {
        const playerId = request.headers.get("x-player-id") ?? "";
        const name = (request.headers.get("x-player-name") ?? "Joueur").slice(0, 24);
        if (!this.info || request.headers.get("upgrade")?.toLowerCase() !== "websocket")
            return Response.json({ error: "no room" }, { status: 404 });
        // A raid instance: the Worker checked the registration (raidZones.ts); a room: invited only.
        if (!this.info.raid && playerId !== this.info.host && !this.info.invited.includes(playerId)) return Response.json({ error: "not invited" }, { status: 403 });
        const pair = new WebSocketPair();
        const [client, server] = Object.values(pair) as [WebSocket, WebSocket];
        server.accept();
        server.binaryType = "arraybuffer";  // the messages are binary: never a Blob
        let seat = -1;
        server.addEventListener("message", (ev) => {
            if (typeof ev.data === "string") return this.drop(server, seat);  // binary messages only
            const bytes = ArrayBuffer.isView(ev.data) ? new Uint8Array(ev.data.buffer, ev.data.byteOffset, ev.data.byteLength) : new Uint8Array(ev.data as ArrayBuffer);
            if (seat < 0) {
                seat = this.hello(server, playerId, name, bytes);
                return;
            }
            this.received(seat, server, bytes);
        });
        const gone = () => {
            if (seat >= 0 && this.seats[seat]?.ws === server) this.disconnected(seat);
        };
        server.addEventListener("close", gone);
        server.addEventListener("error", gone);
        void this.ctx.storage.setAlarm(Date.now() + 1000);
        return new Response(null, { status: 101, webSocket: client });
    }

    private send(ws: WebSocket | null, bytes: Uint8Array) {
        if (!ws) return;
        try {
            ws.send(bytes);
        } catch {
            /* closed meanwhile: its close event follows */
        }
    }
    private sendAll(bytes: Uint8Array) {
        for (const s of this.seats) this.send(s.ws, bytes);
    }
    private drop(ws: WebSocket, seat: number) {
        try {
            ws.close(1008, "protocol");
        } catch {
            /* already closed */
        }
        if (seat >= 0 && this.seats[seat]?.ws === ws) this.disconnected(seat);
    }

    private hello(ws: WebSocket, playerId: string, name: string, bytes: Uint8Array): number {
        const r = new In(bytes);
        const type = r.u8();
        const protocol = r.u8();
        r.u8(); // the seat it wants: online, its account decides
        r.str(24);
        if (r.ok && r.at < r.b.length) r.str(64); // the account it says: online, the session's own is used
        if (type !== Msg.Hello || !r.end() || protocol !== PROTOCOL) {
            this.endTo(ws, EndReason.Refused);
            return -1;
        }
        let seat = this.seats.findIndex((s) => s.playerId === playerId && !s.vacant);
        const maxSeats = this.info!.raid ? MAX_RAID_PLAYERS : MAX_PLAYERS;
        if (this.started && seat >= 0 && this.seats[seat]!.ws) {
            // 2.74: the same account again while its old connection still counts (a link that died
            // without closing, a game started again at once): the new connection takes the seat back,
            // the old one is closed. The player is absent from the first step not given, then comes
            // back like any return (history, catch-up, REJOIN).
            const old = this.seats[seat]!.ws!;
            this.markAbsent(seat);
            try {
                old.close(1000, "replaced");
            } catch {
                /* closed */
            }
            this.flush();
        }
        if (this.started && this.info!.raid && seat < 0) {
            // 2.65 (raids): a late join takes a vacant seat (vacant frames until its REJOIN).
            seat = this.seats.findIndex((s) => s.vacant && !s.ws);
            if (seat < 0) {
                this.endTo(ws, EndReason.Refused);
                return -1;
            }
            const v = this.seats[seat]!;
            v.playerId = playerId;
            v.rejoinAt = 0;
            v.pending = [];
        } else if (this.started) {
            // Coming back: its own seat, in standby until it caught up.
            if (seat < 0 || !this.seats[seat]!.absent || this.seats[seat]!.ws) {
                this.endTo(ws, EndReason.Refused);
                return -1;
            }
        } else if (seat >= 0) {
            if (this.seats[seat]!.ws) this.drop(this.seats[seat]!.ws!, seat); // a second socket replaces the first
            seat = this.seats.findIndex((s) => s.playerId === playerId);
            if (seat < 0) seat = this.addSeat(playerId, name);
        } else {
            if (this.seats.length >= maxSeats) {
                this.endTo(ws, EndReason.Refused);
                return -1;
            }
            seat = this.addSeat(playerId, name);
        }
        const s = this.seats[seat]!;
        s.ws = ws;
        s.name = name;
        s.lastSeen = Date.now();
        // The host always has the first seat.
        if (!this.started && playerId === this.info!.host && seat !== 0) {
            this.seats.splice(seat, 1);
            this.seats.unshift(s);
            seat = 0;
        }
        const o = new Out();
        o.u8(Msg.Welcome);
        o.u8(seat);
        o.u8(PROTOCOL);
        this.send(ws, o.done());
        if (this.started) {
            this.send(ws, this.goMessage());
            this.flush();
            for (const [t, m] of this.raidStates) this.send(ws, this.raidStateMessage(t, m)); // before the frames
            for (let t = 0; t < this.next; t += MAX_BATCH) this.send(ws, this.framesMessage(t, Math.min(MAX_BATCH, this.next - t)));
        } else {
            this.welcomeAll();
            this.lobby();
        }
        return seat;
    }

    private addSeat(playerId: string, name: string): number {
        this.seats.push({
            playerId,
            name,
            ws: null,
            ready: false,
            rtt: -1,
            lastSeen: Date.now(),
            absent: false,
            absentFrom: 0,
            absentAt: 0,
            rejoinAt: 0,
            pending: [],
            budget: { start: Date.now(), count: 0 },
            vacant: false,
        });
        return this.seats.length - 1;
    }

    private welcomeAll() {
        this.seats.forEach((s, i) => {
            const o = new Out();
            o.u8(Msg.Welcome);
            o.u8(i);
            o.u8(PROTOCOL);
            this.send(s.ws, o.done());
        });
    }

    private endTo(ws: WebSocket, reason: number) {
        const o = new Out();
        o.u8(Msg.End);
        o.u8(reason);
        this.send(ws, o.done());
        try {
            ws.close(1000, "refused");
        } catch {
            /* closed */
        }
    }

    private received(seat: number, ws: WebSocket, bytes: Uint8Array) {
        const s = this.seats[seat];
        if (!s || s.ws !== ws) return;
        const now = Date.now();
        if (now - s.budget.start > 1000) s.budget = { start: now, count: 0 };
        if (++s.budget.count > MAX_MESSAGES_PER_SECOND || bytes.length === 0 || bytes.length > MAX_MESSAGE_BYTES) return this.drop(ws, seat);
        s.lastSeen = now;
        const r = new In(bytes);
        const type = r.u8();
        switch (type) {
            case Msg.Ping: {
                const clock = r.u32();
                const rtt = r.u16();
                if (!r.end()) return this.drop(ws, seat);
                s.rtt = rtt === 0xffff ? -1 : rtt;
                const o = new Out();
                o.u8(Msg.Pong);
                o.u32(clock);
                this.send(ws, o.done());
                if (!this.started) this.lobby();
                return;
            }
            case Msg.Ready: {
                const on = r.u8();
                if (!r.end() || this.started) return this.drop(ws, seat);
                s.ready = on !== 0;
                this.lobby();
                return;
            }
            case Msg.Start: {
                r.u8();
                // 2.65: any seat starts a raid instance.
                if (!r.end() || this.started || (seat !== 0 && !this.info?.raid)) return this.drop(ws, seat);
                this.start();
                return;
            }
            case Msg.Input: {
                if (!this.started || !this.input(seat, r)) return this.drop(ws, seat);
                this.flush();
                return;
            }
            case Msg.CatchupDone: {
                r.u32();
                if (!r.end() || !this.started || !s.absent) return this.drop(ws, seat);
                const at = this.next + this.delay + 1;
                if (!s.vacant && at - s.absentFrom > STANDBY_LIMIT_TICKS) {
                    this.endTo(ws, EndReason.Refused);
                    return this.drop(ws, seat);
                }
                s.rejoinAt = at;
                const o = new Out();
                o.u8(Msg.Rejoin);
                o.u8(seat);
                o.u32(at);
                this.sendAll(o.done());
                this.flush(); // 2.74: with nobody else in play, the steps up to its return are given now
                return;
            }
            case Msg.Result: {
                const ticks = r.u32();
                if (!r.end() || !this.started || ticks === 0 || ticks > this.next) return this.drop(ws, seat);
                void this.writeResult(ticks);
                return;
            }
            case Msg.Leave:
                return this.drop(ws, seat);
            default:
                return this.drop(ws, seat);
        }
    }

    private input(seat: number, r: In): boolean {
        const s = this.seats[seat]!;
        const first = r.u32();
        const n = r.u8();
        if (!r.ok || n === 0) return false;
        const frames = readFrames(r, n);
        if (!frames || !r.end() || !frames.every(validClientFrame)) return false;
        if (s.absent && s.rejoinAt === 0) return true; // not back yet
        for (let i = 0; i < n; ++i) {
            const t = first + i;
            if (t < this.next || (s.absent && t < s.rejoinAt)) continue;
            const k = t - this.next;
            if (k > MAX_AHEAD) return false;
            while (s.pending.length <= k) s.pending.push(null);
            s.pending[k] = frames[i]!;
        }
        return true;
    }

    private start() {
        const raid = !!this.info?.raid;
        const connected = this.seats.filter((s) => s.ws);
        if (connected.length < (raid ? 1 : 2) || connected.length !== this.seats.length) return;
        if (!raid && !this.seats.every((s, i) => i === 0 || s.ready)) return;
        const held = this.seats.length;
        this.started = true;
        // 2.65 (raids): always 8 seats; those nobody holds are vacant from the first step.
        if (raid) while (this.seats.length < MAX_RAID_PLAYERS) this.seats[this.addSeat("", "")]!.vacant = true;
        for (const s of this.seats) if (s.vacant) s.absent = true;
        this.players = this.seats.length;
        const worst = Math.max(0, ...this.seats.map((s) => s.rtt));
        this.delay = Math.min(15, Math.max(3, Math.ceil((worst + 16) / (1000 / 120))));
        this.attempt = crypto.randomUUID();
        this.history = [];
        for (let t = 0; t < this.delay; ++t) for (let k = 0; k < this.players; ++k) this.history.push(k < held ? emptyFrame() : { ...emptyFrame(), flags: FLAG_VACANT });
        this.next = this.delay;
        this.flushed = 0;
        this.savedChunks = 0;
        this.storedSteps = 0;
        this.sendAll(this.goMessage());
        if (raid) {
            this.raidStates = [[0, this.raidMask]];
            this.sendAll(this.raidStateMessage(0, this.raidMask));
            this.seats.forEach((s, i) => {
                if (!s.vacant) void this.writeMember(i, s.playerId, true, false, 0);
            });
        }
        this.flush();
        if (raid) void this.persist();
        if (!raid) void this.writeSession();
    }

    private raidStateMessage(tick: number, mask: number): Uint8Array {
        const o = new Out();
        o.u8(Msg.RaidState);
        o.u32(tick);
        o.u32(mask >>> 0);
        return o.done();
    }

    /** 2.65: the branch's state (raidZones.ts) from D1: a change is given from the next step on. */
    async pollRaidState(force: boolean): Promise<void> {
        const raid = this.info?.raid;
        if (!raid || (!force && Date.now() - this.raidPolled < 3000)) return;
        this.raidPolled = Date.now();
        let mask = 0;
        try {
            mask = await branchMaskOf(this.env.DB, raid.event_id, raid.branch);
        } catch {
            return; /* before migration 0142 */
        }
        if (mask === this.raidMask) return;
        this.raidMask = mask;
        if (!this.started) return;
        this.raidStates.push([this.next, mask]);
        this.sendAll(this.raidStateMessage(this.next, mask));
    }

    /**
     * 2.65: who plays in the instance (the consensus counts its active players). 2.68: `from`, the
     * step from which the player holds the seat (when they take it): their contributions are theirs.
     * 2.74: the player is given (a freed seat forgets its player at once); the writes go in order.
     */
    private writeMember(seat: number, playerId: string, active: boolean, left: boolean, from?: number): Promise<void> {
        const raid = this.info?.raid;
        if (!raid || !this.seats[seat] || !playerId) return Promise.resolve();
        const now = nowIso();
        const players = this.seats.filter((x) => !x.vacant && !x.absent && x.playerId).length;
        const db = this.env.DB;
        const write = async () => {
            try {
                await db.batch([
                    db
                        .prepare(
                            `INSERT INTO raid_instance_members (instance_id, player_id, seat, active, joined_at, left_at, seat_from) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
                             ON CONFLICT (instance_id, player_id) DO UPDATE SET seat = ?3, active = ?4, left_at = ?6, seat_from = COALESCE(?7, seat_from)`,
                        )
                        .bind(raid.instance_id, playerId, seat, active ? 1 : 0, now, left ? now : null, from ?? null),
                    db.prepare("UPDATE raid_instances SET players = ?2, started_at = COALESCE(started_at, ?3), updated_at = ?3 WHERE id = ?1").bind(raid.instance_id, players, now),
                ]);
            } catch {
                /* before migrations 0142 / 0143 */
            }
        };
        this.memberWrites = this.memberWrites.then(write);
        return this.memberWrites;
    }

    private goMessage(): Uint8Array {
        const o = new Out();
        o.u8(Msg.Go);
        // The seed is the level's (GameWorld: levelSeed): the game computes it; 0 here means so.
        o.u32(0);
        o.u32(0);
        o.u8(this.players);
        o.u8(this.delay);
        o.str(this.info!.level.id, 64);
        o.str(this.info!.level.hash, 64);
        o.str(this.attempt, 64);
        return o.done();
    }

    private framesMessage(first: number, count: number): Uint8Array {
        const o = new Out();
        o.u8(Msg.Frames);
        o.u32(first);
        o.u16(count);
        for (let s = 0; s < this.players; ++s) {
            const seat: Frame[] = [];
            for (let i = 0; i < count; ++i) seat.push(this.history[(first + i) * this.players + s]!);
            writeFrames(o, seat);
        }
        return o.done();
    }

    /** A seat in play: held and not absent, or coming back (its step of return chosen). */
    private static inPlay(s: Seat): boolean {
        return !s.absent || s.rejoinAt !== 0;
    }

    private flush() {
        if (!this.started) return;
        const raid = !!this.info?.raid;
        for (;;) {
            if (this.next >= MAX_TICKS) {
                const o = new Out();
                o.u8(Msg.End);
                o.u8(EndReason.TooLong);
                this.sendAll(o.done());
                this.started = false;
                if (raid) void this.closeGame();
                return;
            }
            // 2.74: nobody in play (every seat absent, none coming back): the steps stand still. Before,
            // the relay gave standby frames to nobody as fast as it could, up to the 2 h limit (CPU and
            // memory of the object exhausted when the last player of a room left).
            if (!this.seats.some(CoopRoom.inPlay)) break;
            let complete = true;
            for (let k = 0; k < this.players && complete; ++k) {
                const s = this.seats[k]!;
                // 2.65 (raids): 60 s of absence free the seat (the simulation counts the same).
                if (raid && s.absent && !s.vacant && s.rejoinAt === 0 && this.next - s.absentFrom > STANDBY_LIMIT_TICKS) this.freeSeat(k);
                if (s.absent && (s.rejoinAt === 0 || this.next < s.rejoinAt)) continue;
                if (!s.pending[0]) complete = false;
            }
            if (!complete) break;
            for (let k = 0; k < this.players; ++k) {
                const s = this.seats[k]!;
                let f: Frame;
                if (s.absent && (s.rejoinAt === 0 || this.next < s.rejoinAt)) {
                    f = { ...emptyFrame(), flags: s.vacant ? FLAG_VACANT : FLAG_STANDBY };
                } else {
                    if (s.absent) {
                        // 2.68: a seat taken by a late join is held from this step (the raids' contributions).
                        const taken = s.vacant;
                        s.absent = false;
                        s.vacant = false;
                        s.rejoinAt = 0;
                        void this.writeMember(k, s.playerId, true, false, taken ? this.next : undefined);
                    }
                    f = s.pending[0]!;
                }
                s.pending.shift();
                this.history.push(f);
            }
            ++this.next;
        }
        while (this.flushed < this.next) {
            const n = Math.min(MAX_BATCH, this.next - this.flushed);
            this.sendAll(this.framesMessage(this.flushed, n));
            this.flushed += n;
        }
    }

    /**
     * 2.65 (raids): the seat is free for anybody (vacant frames). 2.74: its player is forgotten at once
     * (a late join may take it before the write is done), unless that player is connected on it right
     * now, catching up: then it is their late join (the seat from their step of return).
     */
    private freeSeat(k: number) {
        const s = this.seats[k]!;
        const who = s.playerId;
        s.vacant = true;
        if (!s.ws) s.playerId = "";
        void this.writeMember(k, who, false, true);
    }

    /** 2.74 (raids): with nobody in play, a seat absent for 60 s on the clock is freed. */
    private freeIdle(now: number) {
        if (this.seats.some(CoopRoom.inPlay)) return; // someone plays: the steps count (flush)
        this.seats.forEach((s, k) => {
            if (s.absent && !s.vacant && !s.ws && now - s.absentAt > STANDBY_LIMIT_MS) this.freeSeat(k);
        });
    }

    /** The seat's player is absent from the first step not given yet (the same for everyone). */
    private markAbsent(seat: number) {
        const s = this.seats[seat]!;
        if (!s.absent) {
            s.absent = true;
            s.absentFrom = this.next;
            s.absentAt = Date.now();
            void this.writeMember(seat, s.playerId, false, false);
        }
        if (s.vacant) s.playerId = ""; // 2.74: a vacant seat taken, left before its step of return: free again
        s.rejoinAt = 0;
        s.pending = [];
        s.ws = null;
    }

    private disconnected(seat: number) {
        const s = this.seats[seat];
        if (!s) return;
        if (!this.started) {
            this.seats.splice(seat, 1);
            this.welcomeAll();
            this.lobby();
            return;
        }
        this.markAbsent(seat);
        this.flush();
        // The room empties: a team room forgets everything. 2.74: a raid instance keeps its game (its
        // players come back within 60 s; its alarm frees their seats, then ends it, and stores it meanwhile).
        if (this.seats.every((x) => !x.ws) && !this.info?.raid) this.forget();
    }

    poll(now: number) {
        this.seats.forEach((s, i) => {
            if (s.ws && now - s.lastSeen > SILENCE_MS) {
                const ws = s.ws;
                try {
                    ws.close(1001, "silence");
                } catch {
                    /* closed */
                }
                this.disconnected(i);
            }
        });
        this.flush();
    }

    /** The room empties: nothing is kept (the session row in D1 stays for the ranking). */
    private forget() {
        this.started = false;
        this.history = [];
        this.seats = [];
        this.next = 0;
        this.flushed = 0;
        this.savedChunks = 0;
        this.storedSteps = 0;
        this.raidStates = [];
    }

    /** 2.74 (raids): the instance's game is over (every seat free, nobody connected): memory and storage. */
    private async closeGame() {
        this.forget();
        try {
            await this.ctx.storage.deleteAll();
            if (this.info) await this.ctx.storage.put("info", this.info);
        } catch {
            /* the next start overwrites it */
        }
    }

    private savedGame(next: number): SavedGame {
        return {
            players: this.players,
            delay: this.delay,
            attempt: this.attempt,
            next,
            seats: this.seats.map((s) => ({ playerId: s.playerId, name: s.name, vacant: s.vacant, absent: s.absent, absentFrom: s.absentFrom, absentAt: s.absentAt })),
            raidStates: this.raidStates,
            raidMask: this.raidMask,
        };
    }

    /**
     * 2.74 (raids): stores the game (the alarm, every second): each complete part of the history once,
     * the last one again while it fills, and the seats; one write is atomic, so the stored steps always
     * match the stored state. After a restart the game goes on from the last stored step.
     */
    async persist(): Promise<void> {
        if (!this.info?.raid || !this.started) return;
        const next = this.next;
        const full = Math.floor(next / HISTORY_CHUNK);
        while (this.savedChunks < full) {
            const entries: Record<string, unknown> = {};
            for (let k = 0; k < 100 && this.savedChunks < full; ++k, ++this.savedChunks)
                entries["h:" + this.savedChunks] = encodeHistory(this.history, this.savedChunks * HISTORY_CHUNK, HISTORY_CHUNK, this.players);
            entries.game = this.savedGame(this.savedChunks * HISTORY_CHUNK);
            await this.ctx.storage.put(entries);
        }
        const entries: Record<string, unknown> = { game: this.savedGame(next) };
        const tail = next - full * HISTORY_CHUNK;
        if (tail > 0) entries["h:" + full] = encodeHistory(this.history, full * HISTORY_CHUNK, tail, this.players);
        await this.ctx.storage.put(entries);
        this.storedSteps = next;
    }

    /**
     * 2.74: after a restart of the object (a deployment, an eviction, a crash): the room (a raid
     * instance) and its game come back from storage. Nobody is connected any more: every player is
     * absent from the first step stored, written so in D1 (the consensus counts only who plays), and
     * comes back like any return (history, catch-up, REJOIN); the seats left free after 60 s.
     */
    private async restore(): Promise<void> {
        try {
            const saved = await this.ctx.storage.get(["info", "game"]);
            const info = saved.get("info") as RoomInfo | undefined;
            if (!info) return;
            this.info = info;
            const game = saved.get("game") as SavedGame | undefined;
            if (!info.raid || !game || game.players < 1 || game.players > MAX_RAID_PLAYERS) return;
            const parts = Math.ceil(game.next / HISTORY_CHUNK);
            const history: Frame[] = [];
            for (let at = 0; at < parts; at += 100) {
                const keys: string[] = [];
                for (let c = at; c < Math.min(parts, at + 100); ++c) keys.push("h:" + c);
                const got = await this.ctx.storage.get(keys);
                for (let c = at; c < Math.min(parts, at + 100); ++c) {
                    const bytes = got.get("h:" + c);
                    const count = Math.min(HISTORY_CHUNK, game.next - c * HISTORY_CHUNK);
                    const part = bytes instanceof Uint8Array ? decodeHistory(bytes, count, game.players) : null;
                    if (!part) {
                        log("error", "coop_restore_damaged", { room: info.id, part: c });
                        return; // damaged: the instance starts again (the branch's state is in D1)
                    }
                    for (const f of part) history.push(f);
                }
            }
            const now = Date.now();
            this.players = game.players;
            this.delay = game.delay;
            this.attempt = game.attempt;
            this.history = history;
            this.next = game.next;
            this.flushed = game.next;
            this.savedChunks = Math.floor(game.next / HISTORY_CHUNK);
            this.storedSteps = game.next;
            this.raidStates = game.raidStates;
            this.raidMask = game.raidMask;
            this.seats = game.seats.map((s) => ({
                playerId: s.vacant ? "" : s.playerId,
                name: s.name,
                ws: null,
                ready: true,
                rtt: -1,
                lastSeen: now,
                absent: true,
                absentFrom: s.absent ? Math.min(s.absentFrom, game.next) : game.next,
                absentAt: s.absent ? s.absentAt : now,
                rejoinAt: 0,
                pending: [],
                budget: { start: now, count: 0 },
                vacant: s.vacant,
            }));
            this.started = true;
            this.restores++;
            log("warn", "coop_restored", { room: info.id, steps: game.next });
            this.seats.forEach((s, k) => {
                if (!s.vacant && s.playerId) void this.writeMember(k, s.playerId, false, false);
            });
            await this.memberWrites;
            if ((await this.ctx.storage.getAlarm()) === null) await this.ctx.storage.setAlarm(now + 1000);
        } catch (err) {
            log("error", "coop_restore_failed", { error: err instanceof Error ? err.message : String(err) });
        }
    }

    private lobby() {
        const data = {
            level: this.info?.level.id ?? "",
            hash: this.info?.level.hash ?? "",
            title: this.info?.level.title ?? "",
            source: this.info?.level.source ?? "campaign",
            version: this.info?.level.version ?? 0,
            size: this.info?.level.size ?? 0,
            started: this.started,
            seats: this.seats.map((s, i) => ({ seat: i, name: s.name, account: s.playerId, ready: s.ready || i === 0 || !!this.info?.raid, ping: s.rtt, connected: s.ws !== null })),
        };
        const text = new TextEncoder().encode(JSON.stringify(data));
        const o = new Uint8Array(text.length + 1);
        o[0] = Msg.Lobby;
        o.set(text, 1);
        this.sendAll(o);
    }

    private async writeSession() {
        try {
            await this.env.DB.prepare(
                `INSERT OR IGNORE INTO coop_sessions (attempt_id, room_id, level_id, level_hash, size, players, created_at)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)`,
            )
                .bind(this.attempt, this.info!.id, this.info!.level.id, this.info!.level.hash, this.players, JSON.stringify(this.seats.map((s) => s.playerId)), nowIso())
                .run();
        } catch {
            /* before migration 0140: the game still runs, it is not ranked */
        }
    }

    /** The digest of the steps 0 .. ticks-1 relayed (the team replay must carry exactly these). */
    private async writeResult(ticks: number) {
        if (this.resultWritten) return;
        this.resultWritten = true;
        const digest = await crypto.subtle.digest("SHA-256", canonical(this.history.slice(0, ticks * this.players)));
        try {
            await this.env.DB.prepare("UPDATE coop_sessions SET ticks = ?2, inputs_hash = ?3 WHERE attempt_id = ?1 AND ticks IS NULL")
                .bind(this.attempt, ticks, toHex(new Uint8Array(digest)))
                .run();
        } catch {
            /* not ranked */
        }
    }
}

// ------------------------------------------------------------------ routes

export function stub(env: Bindings, roomId: string) {
    return env.COOP_ROOMS.get(env.COOP_ROOMS.idFromName(roomId));
}

export async function ask<T>(env: Bindings, roomId: string, path: string, body?: unknown): Promise<T> {
    const res = await stub(env, roomId).fetch("https://coop" + path, body === undefined ? {} : { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify(body) });
    return (await res.json()) as T;
}

/**
 * POST /coop/rooms {level: {source, id, hash, title, version?}} : a room hosted by me. 2.63.5: a
 * community level (`id` its online id, `version` a published version) must be an EXTRÊME level
 * published, and `hash` that version's fingerprint: every guest downloads exactly that version
 * (its title and package size come from here, never from the game).
 */
export async function createRoom(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["level"]);
    const level = body.level as Record<string, unknown> | undefined;
    if (!level || typeof level !== "object") v.fail("level", "niveau attendu");
    v.done();
    const lv = new Validator(level!, ["source", "id", "hash", "title", "version"]);
    const source = lv.oneOf("source", ["campaign", "community"] as const)!;
    const id = lv.str("id", { min: 1, max: 64, pattern: source === "community" ? ID_RE : LEVEL_ID_RE })!;
    const hash = lv.str("hash", { min: 64, max: 64, pattern: HASH_RE })!;
    let title = lv.str("title", { min: 1, max: 80 })!;
    const version = lv.int("version", { min: 1, max: 100000, optional: source !== "community" });
    lv.done();
    const info: RoomInfo["level"] = { source, id, hash, title };
    if (source === "community") {
        const row = await ctx.env.DB.prepare(
            `SELECT l.title, l.extreme, l.status, v.level_hash, v.package_size, v.status AS version_status
             FROM community_levels l JOIN community_level_versions v ON v.level_id = l.id AND v.version = ?2 WHERE l.id = ?1`,
        )
            .bind(id, version)
            .first<{ title: string; extreme: number; status: string; level_hash: string; package_size: number | null; version_status: string }>();
        if (!row || row.status !== "published" || row.version_status === "uploading")
            throw Errors.notFound("COMMUNITY_VERSION_NOT_FOUND", "Ce niveau n'est pas publié dans cette version.");
        if (!row.extreme) throw Errors.validation([{ field: "level", message: "la coopération se joue sur les niveaux EXTRÊME" }]);
        if (row.level_hash !== hash) throw Errors.conflict("LEVEL_VERSION_MISMATCH", "Cette copie du niveau n'est pas sa version publiée : retéléchargez-la.");
        title = row.title.slice(0, 80);
        info.title = title;
        info.version = version!;
        info.size = row.package_size ?? 0;
    }
    const roomId = crypto.randomUUID();
    await ask(ctx.env, roomId, "/create", { id: roomId, host: auth.player.id, host_name: auth.player.username, level: info });
    return json(ctx, { room: { id: roomId, host: auth.player.id, level: info } }, { status: 201 });
}

/** POST /coop/rooms/{roomId}/invite {player_id} : the host invites an accepted friend. */
export async function invite(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const roomId = ctx.params.roomId ?? "";
    if (!ID_RE.test(roomId)) throw Errors.notFound("ROOM_NOT_FOUND", "Salon introuvable.");
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["player_id"]);
    const friend = v.str("player_id", { min: 36, max: 36, pattern: ID_RE })!;
    v.done();
    const st = await ask<{ exists: boolean; host: string | null; level: RoomInfo["level"] | null; started: boolean }>(ctx.env, roomId, "/status");
    if (!st.exists || st.host !== auth.player.id) throw Errors.notFound("ROOM_NOT_FOUND", "Salon introuvable.");
    if (st.started) throw Errors.conflict("ROOM_STARTED", "La partie a déjà commencé.");
    const ok = await ctx.env.DB.prepare(
        `SELECT 1 AS x FROM players p WHERE p.id = ?2 AND p.account_status = 'active'
           AND EXISTS (SELECT 1 FROM player_friends f WHERE f.status = 'accepted'
                       AND ((f.player_id = ?1 AND f.friend_id = ?2) OR (f.player_id = ?2 AND f.friend_id = ?1)))
           AND NOT EXISTS (SELECT 1 FROM player_blocks b WHERE (b.player_id = ?1 AND b.blocked_id = ?2) OR (b.player_id = ?2 AND b.blocked_id = ?1))`,
    )
        .bind(auth.player.id, friend)
        .first();
    if (!ok) throw Errors.forbidden("NOT_FRIENDS", "On ne peut inviter qu'un ami.");
    await ask(ctx.env, roomId, "/invite", { player_id: friend });
    await notify(ctx, friend, "coop_invite", { actor: auth.player.id, data: { room: roomId, title: st.level?.title ?? "", level: st.level?.id ?? "" } });
    return json(ctx, { invited: friend });
}

/** GET /coop/invitations : the rooms I was invited to in the last 2 hours (still open). */
export async function invitations(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const since = new Date(Date.now() - INVITE_TTL_MS).toISOString();
    const rows = await ctx.env.DB.prepare(
        `SELECT n.data, n.created_at, p.id AS host_id, p.username AS host FROM notifications n LEFT JOIN players p ON p.id = n.actor_id
         WHERE n.player_id = ?1 AND n.kind = 'coop_invite' AND n.created_at >= ?2 ORDER BY n.created_at DESC LIMIT 10`,
    )
        .bind(auth.player.id, since)
        .all<{ data: string; created_at: string; host_id: string | null; host: string | null }>();
    const out = [];
    for (const r of rows.results) {
        const d = JSON.parse(r.data) as { room?: string; title?: string; level?: string };
        if (!d.room || !ID_RE.test(d.room)) continue;
        const st = await ask<{ exists: boolean; started: boolean; invited: string[] }>(ctx.env, d.room, "/status");
        if (!st.exists || !st.invited.includes(auth.player.id)) continue;
        out.push({ room: d.room, title: d.title ?? "", level: d.level ?? "", host: { id: r.host_id, username: r.host }, started: st.started, at: r.created_at });
    }
    return json(ctx, { invitations: out });
}

/** GET /coop/rooms/{roomId}/socket : the WebSocket of a game (the host or an invited friend). */
export async function socket(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const roomId = ctx.params.roomId ?? "";
    if (!ID_RE.test(roomId)) throw Errors.notFound("ROOM_NOT_FOUND", "Salon introuvable.");
    if (ctx.request.headers.get("upgrade")?.toLowerCase() !== "websocket") throw new ApiError(426, "UPGRADE_REQUIRED", "Connexion WebSocket attendue.");
    const headers = new Headers(ctx.request.headers);
    headers.set("x-player-id", auth.player.id);
    headers.set("x-player-name", auth.player.username);
    const res = await stub(ctx.env, roomId).fetch(new Request("https://coop/socket", { headers }));
    if (res.status === 403 || res.status === 404) throw Errors.notFound("ROOM_NOT_FOUND", "Salon introuvable.");
    return res;
}
