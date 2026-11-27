// Clan raids « Asterion », phase 2 (raidZones.ts, docs/RAIDS.md): the branch « BRAS DROIT », its
// instances of up to 8 players, the consensus of their reports (a majority of the instance's
// active players must report the same value; a minority liar is ignored and journaled), the
// plausibility checks, the objectives counted once whoever does them (concurrent instances), and
// the relay in raid mode (8 seats, vacant seats, the branch's state given at a step).

import { env, SELF } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { FLAG_VACANT, Msg, readFrames, type Frame } from "../src/coop";
import { BRANCH, controlled, decide, lockOpen, majority, maskOf, reachable } from "../src/raidZones";
import { api, freshIp, registerPlayer, seedAll, type TestPlayer } from "./helpers";
import { raidGroups } from "./raidGroups";

beforeEach(seedAll);

const V = "/api/v1";
const EMBLEM = { shape: "star", primary: "#FFD27A", secondary: "#a66bff" };
const iso = (minutes: number) => new Date(Date.now() + minutes * 60000).toISOString();

interface Clan {
    id: string;
    tag: string;
    players: TestPlayer[];
}

async function clan(tag: string, n: number): Promise<Clan> {
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
    return { id, tag, players };
}

/** A world event running now, `clans` registered with every member. */
async function runningEvent(...clans: Clan[]): Promise<string> {
    const id = crypto.randomUUID();
    const now = new Date().toISOString();
    await env.DB.prepare(
        `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity, difficulty, rewards_text, published_at, created_at, updated_at)
         VALUES (?1, 'world', 'asterion', 'standard', NULL, ?2, 30, ?3, ?4, 1, 0, 50, 3, '', ?5, ?5, ?5)`,
    )
        .bind(id, iso(-10), iso(-40), iso(50), now)
        .run();
    for (const c of clans) {
        await env.DB.prepare("INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at) VALUES (?1, ?2, ?3, ?3, '', ?4, ?4)")
            .bind(id, c.id, c.players[0]!.id, now)
            .run();
        let k = 0;
        for (const p of c.players)
            await env.DB.prepare("INSERT INTO raid_registrations (event_id, player_id, clan_id, created_at) VALUES (?1, ?2, ?3, ?4)")
                .bind(id, p.id, c.id, new Date(Date.now() - 60000 + ++k * 10).toISOString())
                .run();
        // §41: the clan's seven groups (14 registered at least), its players in the Bras droit's group.
        await raidGroups(id, c.id, { bras_droit: c.players });
    }
    return id;
}

/** The instance of a clan's first group, started 10 min ago, `players` active in it (as its relay writes them). */
async function instanceOf(event: string, c: Clan, players: TestPlayer[]): Promise<string> {
    const r = await api("POST", `${V}/raids/events/${event}/instance`, { token: players[0]!.token, body: {} });
    expect(r.status, JSON.stringify(r.body)).toBe(200);
    const id = r.body.data.instance.id as string;
    await env.DB.prepare("UPDATE raid_instances SET started_at = ?2, players = ?3 WHERE id = ?1").bind(id, iso(-10), players.length).run();
    let seat = 0;
    for (const p of players)
        await env.DB.prepare("INSERT OR REPLACE INTO raid_instance_members (instance_id, player_id, seat, active, joined_at, left_at) VALUES (?1, ?2, ?3, 1, ?4, NULL)")
            .bind(id, p.id, seat++, iso(-10))
            .run();
    return id;
}

const H1 = "0123456789abcdef";
const H2 = "fedcba9876543210";
const report = (p: TestPlayer, instance: string, body: Record<string, unknown>) =>
    api("POST", `${V}/raids/instances/${instance}/reports`, { token: p.token, body: { kind: "objective", zone: 1, tick: 6000, hash: H1, ...body } });

async function rejections(instance: string) {
    return (await env.DB.prepare("SELECT player_id, reason FROM raid_report_rejections WHERE instance_id = ?1 ORDER BY id").bind(instance).all<{ player_id: string; reason: string }>()).results;
}

describe("raids phase 2: the branch's rules", () => {
    it("generators, shortcut and arena open the locked passages; zones reachable and controlled", () => {
        expect(BRANCH.zones.length).toBe(8);
        // Titan 9: the Épaule's guard is an ambush: its ways on wait for it (lock « ambushes:1 »).
        expect(reachable(0).filter(Boolean).length).toBe(1);
        expect(lockOpen("ambushes:1", 0)).toBe(false);
        const entry = maskOf(["epaule_arene"]);
        expect(lockOpen("ambushes:1", entry)).toBe(true);
        expect(reachable(entry).filter(Boolean).length).toBe(6); // 2.68: all but the wrist and the hand (behind the wrist's long level)
        expect(reachable(entry)[7]).toBe(false);
        expect(reachable(entry)[8]).toBe(false);
        // 2.78: the hand once every ambush of the wrist is beaten (the guard's alone no longer opens it).
        expect(reachable(maskOf(["epaule_arene", "poignet_verrou"]))[8]).toBe(false);
        expect(reachable(maskOf(["epaule_arene", "poignet_verrou", "poignet_embuscade_1", "poignet_embuscade_2", "poignet_embuscade_3", "poignet_galerie_1"]))[8]).toBe(true);
        expect(reachable(maskOf(["poignet_verrou", "poignet_embuscade_1", "poignet_embuscade_2", "poignet_embuscade_3"]))[8]).toBe(false);
        const gens = maskOf(["veine_generateur", "avantbras_generateur"]);
        expect(lockOpen("generators", maskOf(["veine_generateur"]))).toBe(false);
        expect(lockOpen("generators", gens)).toBe(true);
        expect(reachable(gens | entry)[7]).toBe(true);
        expect(reachable(gens)[7]).toBe(false);
        expect(lockOpen("shortcut", maskOf(["avantbras_raccourci"]))).toBe(true);
        expect(controlled(4, maskOf(["forge_soufflet_ouest"]))).toBe(false);
        expect(controlled(4, maskOf(["forge_soufflet_ouest", "forge_soufflet_est"]))).toBe(true); // the anvil is optional
        expect(controlled(8, 0)).toBe(false); // 2.68: the hand is controlled once Pyroclast fell
        expect(controlled(8, maskOf(["main_pyroclast"]))).toBe(true);
    });

    it("a majority is more than half of the active players", () => {
        expect(majority(1)).toBe(1);
        expect(majority(2)).toBe(2);
        expect(majority(3)).toBe(2);
        expect(majority(8)).toBe(5);
        expect(decide(["a", "a", "b"], 3)).toBe("a");
        expect(decide(["a", "a", "a", "a", "b", "b", "b", "b"], 8)).toBeNull(); // 4 against 4: nobody
        expect(decide(["a", "a", "a", "a", "a", "b", "b", "b"], 8)).toBe("a");
    });
});

describe("raids phase 2: instances and consensus", () => {
    it("my group's instance is made once; another group's instance is never helped (§41)", async () => {
        const a = await clan("AUBE", 2);
        const b = await clan("NEBU", 1);
        const ev = await runningEvent(a, b);
        const r1 = await api("POST", `${V}/raids/events/${ev}/instance`, { token: a.players[0]!.token, body: {} });
        const r2 = await api("POST", `${V}/raids/events/${ev}/instance`, { token: a.players[1]!.token, body: {} });
        expect(r1.status).toBe(200);
        expect(r2.body.data.instance.id).toBe(r1.body.data.instance.id);
        expect(r1.body.data.instance.group).toBe(3); // the Bras droit: the third branch of the titan, group 3
        const help = await api("POST", `${V}/raids/events/${ev}/instance`, { token: b.players[0]!.token, body: { instance_id: r1.body.data.instance.id } });
        expect(help.status).toBe(409);
        expect(help.body.error.code).toBe("RAID_OTHER_GROUP");
        const outsider = await registerPlayer("Intrus");
        expect((await api("POST", `${V}/raids/events/${ev}/instance`, { token: outsider.token, body: {} })).status).toBe(403);
    });

    it("an objective needs the majority; a minority liar is ignored and journaled", async () => {
        const a = await clan("AUBE", 3);
        const ev = await runningEvent(a);
        const inst = await instanceOf(ev, a, a.players);
        const [p1, p2, p3] = a.players as [TestPlayer, TestPlayer, TestPlayer];
        const r1 = await report(p1, inst, { objective: "epaule_arene" });
        expect(r1.body.data.status).toBe("pending");
        const liar = await report(p2, inst, { objective: "epaule_arene", hash: H2 });
        expect(liar.body.data.status).toBe("pending");
        const r3 = await report(p3, inst, { objective: "epaule_arene" });
        expect(r3.body.data.status).toBe("accepted");
        // The liar is journaled; saying something else again is refused.
        expect((await rejections(inst)).map((r) => r.player_id)).toEqual([p2.id]);
        expect((await report(p2, inst, { objective: "epaule_arene", hash: "1111111111111111" })).body.data.status).toBe("rejected");
        const view = await api("GET", `${V}/raids/events/${ev}/branch`, { token: p1.token });
        expect(view.status).toBe(200);
        const z1 = view.body.data.zones.find((z: { id: number }) => z.id === 1);
        expect(z1.controlled).toBe(true);
        expect(z1.objectives[0].by).toBe("[AUBE]·3");
        expect(view.body.data.my.instance_id).toBe(inst);
    });

    it("with 8 players, 4 identical reports are not enough, 5 are", async () => {
        const a = await clan("OCTO", 8);
        const ev = await runningEvent(a);
        const inst = await instanceOf(ev, a, a.players);
        for (let i = 0; i < 4; ++i) expect((await report(a.players[i]!, inst, { objective: "epaule_arene" })).body.data.status).toBe("pending");
        for (let i = 4; i < 7; ++i) expect((await report(a.players[i]!, inst, { objective: "epaule_arene", hash: H2 })).body.data.status).toBe("pending");
        expect((await report(a.players[7]!, inst, { objective: "epaule_arene" })).body.data.status).toBe("accepted");
        expect((await rejections(inst)).length).toBe(3); // the three that disagreed
    });

    it("concurrent destructions of the same objective by two instances count once", async () => {
        const a = await clan("AUBE", 1);
        const b = await clan("NEBU", 1);
        const ev = await runningEvent(a, b);
        const ia = await instanceOf(ev, a, a.players);
        const ib = await instanceOf(ev, b, b.players);
        const [ra, rb] = await Promise.all([report(a.players[0]!, ia, { objective: "epaule_arene" }), report(b.players[0]!, ib, { objective: "epaule_arene", hash: H2 })]);
        expect(ra.body.data.status).toBe("accepted");
        expect(rb.body.data.status).toBe("accepted");
        const rows = await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_branch_objectives WHERE event_id = ?1 AND objective_id = 'epaule_arene'").bind(ev).first<{ n: number }>();
        expect(rows!.n).toBe(1);
        // The same player's report again: idempotent.
        expect((await report(a.players[0]!, ia, { objective: "epaule_arene" })).body.data.status).toBe("accepted");
    });

    it("titan 13: a report played at another difficulty than the event's is refused (its Gigaboss' health and damage)", async () => {
        const a = await clan("AUBE", 1);
        const ev = await runningEvent(a);
        await env.DB.prepare("UPDATE raid_events SET difficulty = 4 WHERE id = ?1").bind(ev).run();
        const inst = await instanceOf(ev, a, a.players);
        const p = a.players[0]!;
        const wrong = await report(p, inst, { objective: "epaule_arene", difficulty: 3 });
        expect(wrong.body.data.status).toBe("rejected");
        expect(wrong.body.data.reason).toBe("difficulté différente de celle de l'événement");
        expect((await api("POST", `${V}/raids/instances/${inst}/reports`, { token: p.token, body: { kind: "objective", zone: 1, tick: 6000, hash: H1, difficulty: 9 } })).status).toBe(400);
        expect((await report(p, inst, { objective: "epaule_arene", difficulty: 4 })).body.data.status).toBe("accepted");
    });

    it("implausible reports are refused: locked zone, wrong zone, impossible step, stranger", async () => {
        const a = await clan("AUBE", 1);
        const ev = await runningEvent(a);
        const inst = await instanceOf(ev, a, a.players);
        const p = a.players[0]!;
        expect((await report(p, inst, { objective: "poignet_verrou", zone: 7 })).body.data.reason).toBe("zone verrouillée pour la branche");
        expect((await report(p, inst, { objective: "biceps_gardien", zone: 3 })).body.data.reason).toBe("objectif d'une autre zone");
        expect((await report(p, inst, { objective: "epaule_arene", tick: 120 * 3600 })).body.data.status).toBe("rejected");
        const stranger = await registerPlayer("Intrus");
        expect((await report(stranger, inst, { objective: "epaule_arene" })).status).toBe(403);
        // Titan 9: nothing beyond the Épaule before its guard (an ambush) is beaten.
        expect((await report(p, inst, { objective: "veine_generateur", zone: 3 })).body.data.reason).toBe("zone verrouillée pour la branche");
        expect((await report(p, inst, { objective: "epaule_arene", zone: 1, tick: 3000 })).body.data.status).toBe("accepted");
        // Both generators on: the wrist opens, its lock is accepted.
        expect((await report(p, inst, { objective: "veine_generateur", zone: 3 })).body.data.status).toBe("accepted");
        expect((await report(p, inst, { objective: "avantbras_generateur", zone: 6 })).body.data.status).toBe("accepted");
        expect((await report(p, inst, { objective: "poignet_verrou", zone: 7 })).body.data.status).toBe("accepted");
        expect((await rejections(inst)).length).toBe(4);
    });

    it("§41: the two bellows by ONE group, both within 3 s; a half alone never counts, nor two groups' halves; a zone report moves the group", async () => {
        const a = await clan("AUBE", 1);
        const b = await clan("NEBU", 1);
        const ev = await runningEvent(a, b);
        const ia = await instanceOf(ev, a, a.players);
        const ib = await instanceOf(ev, b, b.players);
        const done = async () =>
            (await env.DB.prepare("SELECT objective_id FROM raid_branch_objectives WHERE event_id = ?1 AND objective_id LIKE 'forge_soufflet_%'").bind(ev).all<{ objective_id: string }>()).results
                .map((r) => r.objective_id)
                .sort();
        // Titan 9: the Épaule's guard first (an ambush: the ways on wait for it); the branch's state, for both groups.
        expect((await report(a.players[0]!, ia, { objective: "epaule_arene", zone: 1, tick: 3000 })).body.data.status).toBe("accepted");
        // One half by each group: neither counts (a pair is one group's, split in two).
        expect((await report(a.players[0]!, ia, { objective: "forge_soufflet_ouest", zone: 4, tick: 6000 })).body.data.status).toBe("accepted");
        expect((await report(b.players[0]!, ib, { objective: "forge_soufflet_est", zone: 4, tick: 6000 })).body.data.status).toBe("accepted");
        expect(await done()).toEqual([]);
        // The same group's other half 4 s later: too late, refused and journaled; still nothing counted.
        const late = await report(a.players[0]!, ia, { objective: "forge_soufflet_est", zone: 4, tick: 6000 + 4 * 120 });
        expect(late.body.data.status).toBe("rejected");
        expect(late.body.data.reason).toContain("à 3 s près");
        expect(await done()).toEqual([]);
        // Group B does both halves within 3 s (2,5 s apart): both count for the branch, by B.
        expect((await report(b.players[0]!, ib, { objective: "forge_soufflet_ouest", zone: 4, tick: 6000 + 300 })).body.data.status).toBe("accepted");
        expect(await done()).toEqual(["forge_soufflet_est", "forge_soufflet_ouest"]);
        const by = await env.DB.prepare("SELECT DISTINCT instance_id FROM raid_branch_objectives WHERE event_id = ?1 AND objective_id LIKE 'forge_soufflet_%'").bind(ev).all<{ instance_id: string }>();
        expect(by.results.map((r) => r.instance_id)).toEqual([ib]);
        expect((await report(a.players[0]!, ia, { kind: "zone", zone: 4, tick: 7000 })).body.data.status).toBe("accepted");
        const view = await api("GET", `${V}/raids/events/${ev}/branch`, { token: a.players[0]!.token });
        expect(view.body.data.zones.find((z: { id: number }) => z.id === 4).controlled).toBe(true);
        expect(view.body.data.instances.find((i: { id: string }) => i.id === ia).zone).toBe(4);
    });
});

// ------------------------------------------------------------------ the relay in raid mode

class Game {
    got: Uint8Array[] = [];
    constructor(public ws: WebSocket) {
        ws.accept();
        ws.binaryType = "arraybuffer";
        ws.addEventListener("message", (ev) => this.got.push(new Uint8Array(ev.data as ArrayBuffer)));
    }
    of(type: number) {
        return this.got.filter((m) => m[0] === type);
    }
}
const settle = () => new Promise((r) => setTimeout(r, 30));
const hello = Uint8Array.from([Msg.Hello, 1, 255, 4, 84, 101, 115, 116]);

function framesOf(m: Uint8Array, players: number): Frame[][] {
    const v = new DataView(m.buffer, m.byteOffset, m.byteLength);
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
    return seats;
}

describe("raids phase 2: the relay of an instance", () => {
    it("8 seats: one player starts, the empty seats are vacant, the branch's state comes at a step", async () => {
        const a = await clan("AUBE", 2);
        const ev = await runningEvent(a);
        const r = await api("POST", `${V}/raids/events/${ev}/instance`, { token: a.players[0]!.token, body: {} });
        const inst = r.body.data.instance.id as string;
        const connect = async (p: TestPlayer) => {
            const res = await SELF.fetch(`http://cosmic.test${V}/raids/instances/${inst}/socket`, {
                headers: { upgrade: "websocket", authorization: `Bearer ${p.token}`, "cf-connecting-ip": freshIp() },
            });
            expect(res.status).toBe(101);
            return new Game(res.webSocket!);
        };
        const g1 = await connect(a.players[0]!);
        g1.ws.send(hello);
        await settle();
        g1.ws.send(Uint8Array.from([Msg.Start, 1]));
        await settle();
        const go = g1.of(Msg.Go)[0]!;
        expect(go[9]).toBe(8); // players: always 8 in a raid
        expect(g1.of(Msg.RaidState).length).toBe(1);
        const first = framesOf(g1.of(Msg.Frames)[0]!, 8);
        expect(first[0]![0]!.flags).toBe(0);
        expect(first[1]![0]!.flags).toBe(FLAG_VACANT);
        expect(first[7]![0]!.flags).toBe(FLAG_VACANT);
        // A late join: the second player gets a vacant seat and the history (state first).
        const g2 = await connect(a.players[1]!);
        g2.ws.send(hello);
        await settle();
        expect(g2.of(Msg.Welcome)[0]![1]).toBe(1);
        const order = g2.got.map((m) => m[0]);
        expect(order.indexOf(Msg.RaidState)).toBeLessThan(order.indexOf(Msg.Frames));
        // An objective accepted for the branch: every relay gives it from its next step.
        await env.DB.prepare("INSERT INTO raid_branch_objectives (event_id, branch, objective_id, instance_id, accepted_at) VALUES (?1, ?2, 'epaule_arene', NULL, ?3)")
            .bind(ev, BRANCH.id, new Date().toISOString())
            .run();
        const st = (await (await env.COOP_ROOMS.get(env.COOP_ROOMS.idFromName(inst)).fetch("https://coop/raid-state")).json()) as { raid: { mask: number } };
        expect(st.raid.mask).toBe(maskOf(["epaule_arene"]));
        await settle();
        const last = g1.of(Msg.RaidState).at(-1)!;
        expect(new DataView(last.buffer, last.byteOffset).getUint32(5, true)).toBe(maskOf(["epaule_arene"]));
        g1.ws.close();
        g2.ws.close();
    });
});
