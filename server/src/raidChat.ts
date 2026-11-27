// 2.95 (docs/RAIDS.md §39): the chat of a raid, two channels.
//
// * « Groupe » — the players of one instance (its relay, the CoopRoom of the instance: coop.ts) and those who
//   watch it after their group was wiped. An administrator's test with invited players (2.80) is one room: its
//   « Groupe » is the players placed in the same group of the test.
// * « Raid » — every player of the event, every group: the event's room (RaidChatRoom, one Durable Object per
//   event, made for the chat: no other object of an event existed). Each instance's room joins it when a game
//   connects and gives it the raid's lines it is sent; the event's room gives each line to every room that joined
//   (each one to its own sockets). In a test, the test's room alone.
//
// Nothing a game sends is trusted. The author is the session's account and name (the Worker's headers, never the
// message); the text is cleaned like the discussions' messages (social.ts cleanMessage: no control nor direction
// character) then put on one line, kept at 200 characters; a game writes at most 3 messages at once then one a
// second (its room counts them; the event's room counts each player again, and the whole raid: 20 at once, then
// 5 a second, so that a raid's line never costs more than its rooms). A player never gets the lines of a player they
// blocked or who blocked them (player_blocks, read once per connection). Only the last 50 lines of each channel
// are kept, in memory (given again to a game that (re)connects): nothing is written in D1.
//
// The messages ride on the relay's protocol (game/coop/CoopProtocol.h), never on a step of the game:
//   CHAT_SEND (21, game → relay): u8 channel (0 Groupe, 1 Raid), u16 n, n bytes of UTF-8 (1024 at most).
//   CHAT_LINE (22, relay → game): u8 channel, u8 flags (1 the relay's notice to this player, 2 history), u32 Unix
//   seconds, str account, str name, str label (« [AUBE]·2 »), u16 n + the text.
// A relay says it carries the chat in WELCOME (a 4th byte, FEATURE_CHAT): a game before never sends it.

import { DurableObject } from "cloudflare:workers";
import { log, type Bindings } from "./http";
import { cleanMessage } from "./social";

export const CHAT_SEND = 21;
export const CHAT_LINE = 22;
export const FEATURE_CHAT = 1;
export const CHAT_MAX_CHARS = 200;
export const CHAT_MAX_BYTES = 1024;
export const CHAT_HISTORY = 50;
/** A game: 3 messages at once, then one a second. */
export const CHAT_BURST = 3;
export const CHAT_REFILL_MS = 1000;
/** The whole raid's channel: 20 lines at once, then 5 a second (each one goes to every instance of the event). */
export const EVENT_BURST = 20;
export const EVENT_REFILL_MS = 200;
/** The rooms an event's room gives its lines to, at most. */
export const MAX_EVENT_ROOMS = 1000;
export const ChatChannel = { Group: 0, Raid: 1 } as const;
export const FLAG_NOTICE = 1;
export const FLAG_HISTORY = 2;

/** What the relay tells a player whose message it did not give (French: the game translates it). */
export const ChatRefusal = {
    TooFast: "Trop de messages à la suite : attendez une seconde.",
    TooLong: "Message trop long : 200 caractères au plus.", // CHAT_MAX_CHARS
    NotYourGroup: "Vous regardez un autre groupe : écrivez sur le canal Raid.",
    NoTestGroup: "Vous n'êtes placé dans aucun groupe du test : écrivez sur le canal Raid.",
    RaidBusy: "Le canal Raid est très actif : attendez un instant.",
    RaidUnavailable: "Le canal Raid n'est pas disponible pour l'instant.",
} as const;

/** A message's text: one line (line breaks and tabs become spaces), as the discussions' messages, spaces gathered. */
export function cleanChatText(raw: string): string {
    return cleanMessage(raw.replace(/[\r\n\t]+/g, " ").replace(/\uFEFF/g, ""))
        .replace(/ {2,}/g, " ")
        .trim();
}

/** Its characters (code points, as the game counts them). */
export const chatLength = (s: string): number => Array.from(s).length;

/** A token bucket: `burst` at once, then one every `refillMs`. */
export class Bucket {
    private tokens: number;
    private last: number;
    constructor(
        private burst: number,
        private refillMs: number,
        now = Date.now(),
    ) {
        this.tokens = burst;
        this.last = now;
    }
    take(now = Date.now()): boolean {
        this.tokens = Math.min(this.burst, this.tokens + Math.max(0, now - this.last) / this.refillMs);
        this.last = now;
        if (this.tokens < 1) return false;
        this.tokens -= 1;
        return true;
    }
}

export interface ChatLine {
    channel: number;
    flags: number;
    at: number; // Unix seconds
    account: string;
    name: string;
    label: string;
    text: string;
}

/** The UTF-8 bytes of `s`, at most `max`, never cutting a character. */
function utf8(s: string, max: number): Uint8Array {
    const b = new TextEncoder().encode(s);
    if (b.length <= max) return b;
    let n = max;
    while (n > 0 && (b[n]! & 0xc0) === 0x80) --n;
    return b.subarray(0, n);
}

export function encodeChatLine(l: ChatLine): Uint8Array {
    const parts: number[] = [CHAT_LINE, l.channel & 0xff, l.flags & 0xff];
    const at = Math.max(0, Math.floor(l.at)) >>> 0;
    for (let i = 0; i < 4; ++i) parts.push((at >>> (8 * i)) & 0xff);
    for (const s of [l.account, l.name, l.label]) {
        const b = utf8(s, 64);
        parts.push(b.length, ...b);
    }
    const t = utf8(l.text, CHAT_MAX_BYTES);
    parts.push(t.length & 0xff, t.length >>> 8, ...t);
    return Uint8Array.from(parts);
}

/** A CHAT_SEND (null when malformed: its channel, its length, its UTF-8). */
export function decodeChatSend(bytes: Uint8Array): { channel: number; text: string } | null {
    if (bytes.length < 4 || bytes[0] !== CHAT_SEND) return null;
    const channel = bytes[1]!;
    const n = bytes[2]! | (bytes[3]! << 8);
    if (channel > 1 || n > CHAT_MAX_BYTES || bytes.length !== 4 + n) return null;
    try {
        return { channel, text: new TextDecoder("utf-8", { fatal: true, ignoreBOM: true }).decode(bytes.subarray(4)) };
    } catch {
        return null;
    }
}

/** The last CHAT_HISTORY lines of a channel. */
export class ChatHistory<T = ChatLine> {
    lines: T[] = [];
    push(l: T): void {
        this.lines.push(l);
        if (this.lines.length > CHAT_HISTORY) this.lines.splice(0, this.lines.length - CHAT_HISTORY);
    }
}

/** A socket of a room, for the chat: who it is (the Worker's headers), its pace, the players it does not hear. */
export interface ChatPeer {
    playerId: string;
    name: string;
    label: string;
    /** It belongs to this instance's group (its seat, or its own group wiped): « Groupe » is its own. */
    member: boolean;
    bucket: Bucket;
    blocked: Set<string>;
}

/** The headers a Worker gives a room's socket for the chat (always set or removed: never the game's own). */
export function setChatHeaders(h: Headers, chat: { label: string; member: boolean } | null): void {
    h.delete("x-chat-label");
    h.delete("x-chat-member");
    if (!chat) return;
    h.set("x-chat-label", encodeURIComponent(chat.label.slice(0, 40)));
    h.set("x-chat-member", chat.member ? "1" : "0");
}

export function chatPeerOf(h: Headers, playerId: string, name: string): ChatPeer {
    let label = "";
    try {
        label = decodeURIComponent(h.get("x-chat-label") ?? "").slice(0, 40);
    } catch {
        label = "";
    }
    return { playerId, name, label, member: h.get("x-chat-member") !== "0", bucket: new Bucket(CHAT_BURST, CHAT_REFILL_MS), blocked: new Set() };
}

/** The players `playerId` blocked or who blocked them (both never hear each other). */
export async function blocksOf(db: D1Database, playerId: string): Promise<Set<string>> {
    try {
        const rows = await db
            .prepare(
                `SELECT blocked_id AS id FROM player_blocks WHERE player_id = ?1
                 UNION SELECT player_id AS id FROM player_blocks WHERE blocked_id = ?1 LIMIT 2000`,
            )
            .bind(playerId)
            .all<{ id: string }>();
        return new Set(rows.results.map((r) => r.id));
    } catch {
        return new Set();
    }
}

export const unixNow = (): number => Math.floor(Date.now() / 1000);

/** A notice of the relay to one player (why their message was not given). */
export function notice(channel: number, text: string): ChatLine {
    return { channel, flags: FLAG_NOTICE, at: unixNow(), account: "", name: "", label: "", text };
}

// ------------------------------------------------------------------ the event's room

export function chatRoom(env: Bindings, eventId: string) {
    return env.RAID_CHAT_ROOMS.get(env.RAID_CHAT_ROOMS.idFromName("raid-chat:" + eventId));
}

/**
 * The « Raid » channel of one event: the rooms of its instances that joined (stored: they outlive an eviction of
 * this object), its last lines (memory only), the pace of each player and of the whole raid.
 */
export class RaidChatRoom extends DurableObject<Bindings> {
    private rooms = new Set<string>();
    private history = new ChatHistory();
    private players = new Map<string, Bucket>();
    private raid = new Bucket(EVENT_BURST, EVENT_REFILL_MS);

    constructor(ctx: DurableObjectState, env: Bindings) {
        super(ctx, env);
        void ctx.blockConcurrencyWhile(async () => {
            const saved = await ctx.storage.get<string[]>("rooms");
            if (Array.isArray(saved)) for (const r of saved.slice(0, MAX_EVENT_ROOMS)) this.rooms.add(r);
        });
    }

    async fetch(request: Request): Promise<Response> {
        const url = new URL(request.url);
        const body = request.method === "POST" ? ((await request.json().catch(() => ({}))) as Record<string, unknown>) : {};
        const room = typeof body.room === "string" ? body.room : "";
        switch (url.pathname) {
            case "/join": {
                // A room of the event with someone in it: its lines from now on, the last ones for who just came.
                if (room && !this.rooms.has(room) && this.rooms.size < MAX_EVENT_ROOMS) {
                    this.rooms.add(room);
                    await this.ctx.storage.put("rooms", [...this.rooms]);
                }
                return Response.json({ history: this.history.lines });
            }
            case "/leave":
                if (this.rooms.delete(room)) await this.ctx.storage.put("rooms", [...this.rooms]);
                return Response.json({ ok: true });
            case "/post": {
                // A line of a player of `room` (its room checked the session, cleaned the text and counted the pace).
                const account = typeof body.account === "string" ? body.account : "";
                const text = typeof body.text === "string" ? cleanChatText(body.text) : "";
                if (!account || !text || chatLength(text) > CHAT_MAX_CHARS) return Response.json({ error: "invalid" }, { status: 400 });
                const now = Date.now();
                let bucket = this.players.get(account);
                if (!bucket) {
                    if (this.players.size > 5000) this.players.clear();
                    bucket = new Bucket(CHAT_BURST, CHAT_REFILL_MS, now);
                    this.players.set(account, bucket);
                }
                // The player's pace first (one game cannot spend the raid's), then the raid's.
                if (!bucket.take(now)) return Response.json({ error: "too_fast" }, { status: 429 });
                if (!this.raid.take(now)) return Response.json({ error: "busy" }, { status: 429 });
                if (room && !this.rooms.has(room) && this.rooms.size < MAX_EVENT_ROOMS) this.rooms.add(room);
                const line: ChatLine = {
                    channel: ChatChannel.Raid,
                    flags: 0,
                    at: unixNow(),
                    account,
                    name: typeof body.name === "string" ? body.name.slice(0, 40) : "",
                    label: typeof body.label === "string" ? body.label.slice(0, 40) : "",
                    text,
                };
                this.history.push(line);
                await this.deliver(line);
                return Response.json({ ok: true, rooms: this.rooms.size });
            }
            case "/status":
                return Response.json({ rooms: [...this.rooms], history: this.history.lines.length });
            default:
                return Response.json({ error: "unknown" }, { status: 404 });
        }
    }

    /** Each room that joined gives the line to its sockets; a room with nobody any more leaves. */
    private async deliver(line: ChatLine): Promise<void> {
        const rooms = [...this.rooms];
        const answers = await Promise.allSettled(
            rooms.map(async (id) => {
                const res = await this.env.COOP_ROOMS.get(this.env.COOP_ROOMS.idFromName(id)).fetch("https://coop/chat-deliver", {
                    method: "POST",
                    headers: { "content-type": "application/json" },
                    body: JSON.stringify(line),
                });
                const r = (await res.json().catch(() => ({}))) as { listeners?: number };
                return res.ok ? (r.listeners ?? 0) : 0;
            }),
        );
        let left = false;
        answers.forEach((a, i) => {
            if (a.status === "rejected") {
                // Not reached this time (a restart): it stays, its games still there get the next ones.
                log("warn", "raid_chat_deliver_failed", { room: rooms[i] });
                return;
            }
            if (a.value > 0) return;
            this.rooms.delete(rooms[i]!);  // nobody in it any more: it joins again with its next game
            left = true;
        });
        if (left) await this.ctx.storage.put("rooms", [...this.rooms]);
    }
}
