// 2.95 (docs/RAIDS.md §39): the raid's chat. « Groupe » through an instance's relay, « Raid » through the event's room
// to every instance of the event; the author always the session's (never the message's), the text cleaned and kept at
// 200 characters, the pace (3 at once, then one a second), the blocks, the history given to a game that comes, a
// spectator of another group on « Raid » only, a test's room alone. The pure parts first.

import { env, SELF } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { Msg } from "../src/coop";
import {
    Bucket,
    CHAT_LINE,
    CHAT_MAX_CHARS,
    CHAT_SEND,
    ChatRefusal,
    FEATURE_CHAT,
    FLAG_HISTORY,
    FLAG_NOTICE,
    chatLength,
    chatRoom,
    cleanChatText,
    decodeChatSend,
    encodeChatLine,
    setChatHeaders,
} from "../src/raidChat";
import { api, freshIp, registerPlayer, seedAll, type TestPlayer } from "./helpers";
import { raidGroups } from "./raidGroups";

beforeEach(seedAll);

const V = "/api/v1";
const EMBLEM = { shape: "star", primary: "#FFD27A", secondary: "#a66bff" };
const iso = (minutes: number) => new Date(Date.now() + minutes * 60000).toISOString();

// ------------------------------------------------------------------ the wire

function chatSend(channel: number, text: string, extra = 0): Uint8Array {
    const b = new TextEncoder().encode(text);
    return Uint8Array.from([CHAT_SEND, channel, b.length & 255, b.length >> 8, ...b, ...(extra ? [0] : [])]);
}

interface Line {
    channel: number;
    flags: number;
    at: number;
    account: string;
    name: string;
    label: string;
    text: string;
}

function readLine(m: Uint8Array): Line {
    let at = 1;
    const u8 = () => m[at++]!;
    const dec = new TextDecoder();
    const str = () => {
        const n = u8();
        const s = dec.decode(m.subarray(at, at + n));
        at += n;
        return s;
    };
    const channel = u8();
    const flags = u8();
    const t = new DataView(m.buffer, m.byteOffset + at, 4).getUint32(0, true);
    at += 4;
    const account = str();
    const name = str();
    const label = str();
    const n = u8() | (u8() << 8);
    const text = dec.decode(m.subarray(at, at + n));
    return { channel, flags, at: t, account, name, label, text };
}

describe("raid chat: what a message may be", () => {
    it("one line, no control nor direction character, spaces gathered, 200 characters counted as the game counts them", () => {
        expect(cleanChatText("  On   y\tva\r\n !  ")).toBe("On y va !");
        expect(cleanChatText("a\u0000b\u0007c‮d​e﻿")).toBe("abcde");
        expect(cleanChatText("\u0001\u0002 ")).toBe("");
        expect(chatLength("é".repeat(CHAT_MAX_CHARS))).toBe(CHAT_MAX_CHARS);
        expect(chatLength("🙂🙂")).toBe(2);
    });

    it("a CHAT_SEND is read exactly or refused (channel, length, UTF-8)", () => {
        expect(decodeChatSend(chatSend(0, "Salut"))).toEqual({ channel: 0, text: "Salut" });
        expect(decodeChatSend(chatSend(1, "Prêt !"))).toEqual({ channel: 1, text: "Prêt !" });
        expect(decodeChatSend(chatSend(2, "x"))).toBeNull(); // no third channel
        expect(decodeChatSend(chatSend(0, "x", 1))).toBeNull(); // a byte too many
        expect(decodeChatSend(Uint8Array.from([CHAT_SEND, 0, 2, 0, 0xc3]))).toBeNull(); // truncated
        expect(decodeChatSend(Uint8Array.from([CHAT_SEND, 0, 2, 0, 0xc3, 0x28]))).toBeNull(); // not UTF-8
        expect(decodeChatSend(Uint8Array.from([CHAT_SEND, 0, 0x01, 0x04, ...new Uint8Array(1025)]))).toBeNull(); // over 1024 bytes
    });

    it("a CHAT_LINE carries its author, its label and its text, never cut inside a character", () => {
        const b = encodeChatLine({ channel: 1, flags: FLAG_HISTORY, at: 1_800_000_000, account: "a-b", name: "Nova", label: "[AUBE]·2", text: "é".repeat(600) });
        expect(b[0]).toBe(CHAT_LINE);
        const l = readLine(b);
        expect(l).toMatchObject({ channel: 1, flags: FLAG_HISTORY, at: 1_800_000_000, account: "a-b", name: "Nova", label: "[AUBE]·2" });
        expect(new TextEncoder().encode(l.text).length).toBe(1024); // 512 « é » of 2 bytes: whole characters
        expect(l.text).toBe("é".repeat(512));
    });

    it("the pace: 3 at once, then one a second", () => {
        const b = new Bucket(3, 1000, 0);
        expect([b.take(0), b.take(0), b.take(0), b.take(0)]).toEqual([true, true, true, false]);
        expect(b.take(500)).toBe(false);
        expect(b.take(1000)).toBe(true);
        expect(b.take(1100)).toBe(false);
        expect(b.take(5000)).toBe(true);
    });

    it("the Worker always sets the chat's headers itself (never the game's)", () => {
        const h = new Headers({ "x-chat-label": "[FAUX]·9", "x-chat-member": "1" });
        setChatHeaders(h, null);
        expect(h.get("x-chat-label")).toBeNull();
        expect(h.get("x-chat-member")).toBeNull();
        setChatHeaders(h, { label: "[AUBE]·2", member: false });
        expect(decodeURIComponent(h.get("x-chat-label")!)).toBe("[AUBE]·2");
        expect(h.get("x-chat-member")).toBe("0");
    });
});

// ------------------------------------------------------------------ an event, its clans, their instances

async function clanIn(ev: string, tag: string, n: number, now: string): Promise<TestPlayer[]> {
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
    await env.DB.prepare("INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at) VALUES (?1, ?2, ?3, ?3, '', ?4, ?4)")
        .bind(ev, id, chief.id, now)
        .run();
    let k = 0;
    for (const p of players)
        await env.DB.prepare("INSERT INTO raid_registrations (event_id, player_id, clan_id, created_at) VALUES (?1, ?2, ?3, ?4)")
            .bind(ev, p.id, id, new Date(Date.now() - 60000 + ++k * 10).toISOString())
            .run();
    await raidGroups(ev, id, { crane: players }); // §41: the clan's seven groups, these players in group 1
    return players;
}

async function eventWith(clans: [string, number][]): Promise<{ ev: string; clans: TestPlayer[][] }> {
    const now = new Date().toISOString();
    const ev = crypto.randomUUID();
    await env.DB.prepare(
        `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity, difficulty, rewards_text, published_at, created_at, updated_at)
         VALUES (?1, 'world', 'asterion', 'standard', NULL, ?2, 30, ?3, ?4, 1, 0, 50, 3, '', ?5, ?5, ?5)`,
    )
        .bind(ev, iso(-10), iso(-40), iso(50), now)
        .run();
    const out: TestPlayer[][] = [];
    for (const [tag, n] of clans) out.push(await clanIn(ev, tag, n, now));
    return { ev, clans: out };
}

async function instanceOf(ev: string, p: TestPlayer): Promise<string> {
    const r = await api("POST", `${V}/raids/events/${ev}/instance`, { token: p.token, body: {} });
    expect(r.status, JSON.stringify(r.body)).toBe(200);
    return r.body.data.instance.id as string;
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
async function until(cond: () => boolean, ms = 3000): Promise<void> {
    const end = Date.now() + ms;
    while (!cond()) {
        if (Date.now() > end) throw new Error("condition not reached in time");
        await sleep(10);
    }
}

class Game {
    got: Uint8Array[] = [];
    closed = false;
    constructor(public ws: WebSocket) {
        ws.accept();
        ws.binaryType = "arraybuffer";
        ws.addEventListener("message", (ev) => {
            this.got.push(new Uint8Array(ev.data as ArrayBuffer));
        });
        ws.addEventListener("close", () => (this.closed = true));
        ws.addEventListener("error", () => (this.closed = true));
    }
    send(b: Uint8Array) {
        if (!this.closed) this.ws.send(b);
    }
    welcome() {
        return this.got.filter((m) => m[0] === Msg.Welcome).at(-1);
    }
    lines(): Line[] {
        return this.got.filter((m) => m[0] === CHAT_LINE).map(readLine);
    }
    hello() {
        const name = new TextEncoder().encode("Faux nom");
        this.send(Uint8Array.from([Msg.Hello, 1, 0, name.length, ...name]));
    }
}

async function connect(p: TestPlayer, inst: string, query = "", headers: Record<string, string> = {}): Promise<Game> {
    const res = await SELF.fetch(`http://cosmic.test${V}/raids/instances/${inst}/socket${query}`, {
        headers: { upgrade: "websocket", authorization: `Bearer ${p.token}`, "cf-connecting-ip": freshIp(), ...headers },
    });
    expect(res.status).toBe(101);
    const g = new Game(res.webSocket!);
    g.hello();
    await until(() => !!g.welcome());
    return g;
}

describe("raid chat: the relays of an event", () => {
    it("« Groupe » stays in the instance, « Raid » reaches every instance; the author is the session's, cleaned, at the pace", async () => {
        const { ev, clans } = await eventWith([
            ["AUBE", 3],
            ["NUIT", 2],
        ]);
        const [aube, nuit] = clans as [TestPlayer[], TestPlayer[]];
        const instA = await instanceOf(ev, aube[0]!);
        const instB = await instanceOf(ev, nuit[0]!);
        expect(instA).not.toBe(instB);
        // A game told to forge its label and its group: the Worker's headers win.
        const a = await Promise.all(aube.map((p) => connect(p, instA, "", { "x-chat-label": "[FAUX]·9", "x-chat-member": "1" })));
        const b = await Promise.all(nuit.map((p) => connect(p, instB)));
        // WELCOME says the relay carries the chat.
        for (const g of [...a, ...b]) expect(g.welcome()![3]).toBe(FEATURE_CHAT);
        // « Groupe »: the three of AUBE, never NUIT; the author's account and name from the session (not « Faux nom »).
        a[1]!.send(chatSend(0, "  On   attaque\tle golem ?\u0007 "));
        await until(() => a.every((g) => g.lines().length >= 1));
        for (const g of a) expect(g.lines()[0]).toMatchObject({ channel: 0, flags: 0, account: aube[1]!.id, name: "AUBEMem1", text: "On attaque le golem ?" });
        await sleep(50);
        for (const g of b) expect(g.lines()).toHaveLength(0);
        // « Raid »: every player of the event, with the author's clan and group (the server's, not the forged one).
        b[0]!.send(chatSend(1, "La Main est ouverte !"));
        await until(() => [...a, ...b].every((g) => g.lines().some((l) => l.channel === 1)));
        for (const g of [...a, ...b])
            expect(g.lines().find((l) => l.channel === 1)).toMatchObject({ account: nuit[0]!.id, label: "[NUIT]·1", text: "La Main est ouverte !" });
        a[0]!.send(chatSend(1, "On arrive."));
        await until(() => b[1]!.lines().some((l) => l.text === "On arrive."));
        expect(b[1]!.lines().find((l) => l.text === "On arrive.")!.label).toBe("[AUBE]·1");
        // Too long (201 characters, under 1024 bytes): told to its author only.
        a[2]!.send(chatSend(0, "x".repeat(CHAT_MAX_CHARS + 1)));
        await until(() => a[2]!.lines().some((l) => l.flags === FLAG_NOTICE));
        expect(a[2]!.lines().find((l) => l.flags === FLAG_NOTICE)!.text).toBe(ChatRefusal.TooLong);
        expect(a[0]!.lines().some((l) => l.flags === FLAG_NOTICE)).toBe(false);
        // The pace: 3 at once, the 4th refused (to its author).
        for (let i = 0; i < 4; ++i) a[2]!.send(chatSend(0, `vite ${i}`));
        await until(() => a[2]!.lines().filter((l) => l.flags === FLAG_NOTICE).length === 2);
        expect(a[2]!.lines().filter((l) => l.flags === FLAG_NOTICE)[1]!.text).toBe(ChatRefusal.TooFast);
        await until(() => a[0]!.lines().filter((l) => l.text.startsWith("vite")).length === 3);
        await sleep(50);
        expect(a[0]!.lines().filter((l) => l.text.startsWith("vite"))).toHaveLength(3);
        // A malformed message closes its socket, as any other.
        a[2]!.send(Uint8Array.from([CHAT_SEND, 7, 1, 0, 65]));
        await until(() => a[2]!.closed);
        // The event's room knows both instances.
        const st = (await (await chatRoom(env, ev).fetch("https://raid-chat/status")).json()) as { rooms: string[]; history: number };
        expect(new Set(st.rooms)).toEqual(new Set([instA, instB]));
        expect(st.history).toBe(2);
    });

    it("a blocked player is never heard; a game that comes gets the last lines once, as history", async () => {
        const { ev, clans } = await eventWith([["BLOC", 3]]);
        const [p0, p1, p2] = clans[0] as [TestPlayer, TestPlayer, TestPlayer];
        await env.DB.prepare("INSERT INTO player_blocks (player_id, blocked_id, created_at) VALUES (?1, ?2, ?3)").bind(p0.id, p1.id, new Date().toISOString()).run();
        const inst = await instanceOf(ev, p0);
        const g0 = await connect(p0, inst);
        const g1 = await connect(p1, inst);
        await sleep(50); // the blocks are read once per connection
        g1.send(chatSend(0, "Tu m'entends ?"));
        g1.send(chatSend(1, "Et sur le raid ?"));
        await until(() => g1.lines().length === 2);
        await sleep(80);
        expect(g0.lines()).toHaveLength(0); // p0 blocked p1: neither channel
        g0.send(chatSend(0, "Le groupe, à l'ouest."));
        await until(() => g0.lines().length === 1);
        await sleep(50);
        expect(g1.lines().some((l) => l.account === p0.id)).toBe(false); // nor the other way
        // p2 comes: the group's last lines and the raid's, marked as history.
        const g2 = await connect(p2, inst);
        await until(() => g2.lines().length >= 3);
        const h = g2.lines();
        expect(h.every((l) => l.flags === FLAG_HISTORY)).toBe(true);
        expect(h.map((l) => l.text).sort()).toEqual(["Et sur le raid ?", "Le groupe, à l'ouest.", "Tu m'entends ?"].sort());
    });

    it("a spectator of another group talks on « Raid » only; a room nobody is in leaves the event's room", async () => {
        const { ev, clans } = await eventWith([
            ["VOIR", 2],
            ["JEUX", 2],
        ]);
        const [voir, jeux] = clans as [TestPlayer[], TestPlayer[]];
        const instJ = await instanceOf(ev, jeux[0]!);
        const player = await connect(jeux[0]!, instJ);
        const watcher = await connect(voir[0]!, instJ, "?watch=1");
        expect(watcher.welcome()![1]).toBe(0xfe); // a watcher's seat
        expect(watcher.welcome()![3]).toBe(FEATURE_CHAT);
        watcher.send(chatSend(0, "Je regarde."));
        await until(() => watcher.lines().some((l) => l.flags === FLAG_NOTICE));
        expect(watcher.lines()[0]!.text).toBe(ChatRefusal.NotYourGroup);
        await sleep(50);
        expect(player.lines()).toHaveLength(0);
        watcher.send(chatSend(1, "Bravo JEUX !"));
        await until(() => player.lines().some((l) => l.text === "Bravo JEUX !"));
        // Everybody leaves: the next raid line drops the empty room from the event's room.
        player.ws.close();
        watcher.ws.close();
        await until(() => player.closed && watcher.closed);
        await sleep(50);
        const instV = await instanceOf(ev, voir[1]!);
        const other = await connect(voir[1]!, instV);
        other.send(chatSend(1, "Quelqu'un ?"));
        await until(() => other.lines().some((l) => l.text === "Quelqu'un ?"));
        const st = (await (await chatRoom(env, ev).fetch("https://raid-chat/status")).json()) as { rooms: string[] };
        expect(st.rooms).toEqual([instV]);
    });
});

describe("raid chat: an administrator's test with invited players (one room)", () => {
    it("« Groupe » is the group of the test each one is placed in, « Raid » the whole test", async () => {
        const a = await registerPlayer("ChatAdmin");
        await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(a.id).run();
        const p = await registerPlayer("ChatInvite");
        await env.DB.prepare("UPDATE players SET presence_state = 'active', presence_at = ?2, presence_visible = 1 WHERE id = ?1")
            .bind(p.id, new Date().toISOString())
            .run();
        const FP = "2.80.0|s17|c3|t7";
        const r = await api("POST", `${V}/admin/raid-tests`, {
            token: a.token,
            body: { mode: "raid", config: { skill: 1, invincible: false, bots_invincible: false, players: 21, full_groups: true }, fingerprint: FP },
        });
        expect(r.status, JSON.stringify(r.body)).toBe(201);
        const test = r.body.data.test.id as string;
        const open = async (pl: TestPlayer) => {
            const res = await SELF.fetch(`http://cosmic.test${V}/raid-tests/${test}/socket`, {
                headers: { upgrade: "websocket", authorization: `Bearer ${pl.token}`, "cf-connecting-ip": freshIp() },
            });
            expect(res.status).toBe(101);
            return new Game(res.webSocket!);
        };
        const ga = await open(a);
        ga.hello();
        await until(() => !!ga.welcome());
        expect(ga.welcome()![3]).toBe(FEATURE_CHAT);
        ga.send(Uint8Array.from([Msg.Start, 8]));
        await until(() => ga.got.some((m) => m[0] === Msg.Go));
        expect((await api("POST", `${V}/admin/raid-tests/${test}/invitations`, { token: a.token, body: { player_id: p.id, group: 2 } })).status).toBe(201);
        expect((await api("POST", `${V}/raid-tests/${test}/accept`, { token: p.token, body: { fingerprint: FP } })).status).toBe(200);
        const gp = await open(p);
        gp.hello();
        await until(() => !!gp.welcome());
        // Not the same group: « Groupe » never crosses, « Raid » reaches the whole test (its room alone).
        ga.send(chatSend(0, "Groupe 1, à vous."));
        ga.send(chatSend(1, "Tout le monde : on commence."));
        await until(() => gp.lines().some((l) => l.channel === 1));
        await until(() => ga.lines().length === 2);
        expect(gp.lines().map((l) => l.text)).toEqual(["Tout le monde : on commence."]);
        expect(gp.lines()[0]!.account).toBe(a.id);
        // The administrator joins group 3: its « Groupe » is the invited player's now.
        expect((await api("POST", `${V}/admin/raid-tests/${test}/seat`, { token: a.token, body: { group: 2 } })).status).toBe(200);
        gp.send(chatSend(0, "Bienvenue dans le groupe 3."));
        await until(() => ga.lines().some((l) => l.text === "Bienvenue dans le groupe 3."));
    });
});
