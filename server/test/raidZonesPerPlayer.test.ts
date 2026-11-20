// 2.78 (docs/RAIDS.md §26): each player of an instance changes zone alone. The games report where each
// seat is (« zones », the same consensus as every report; every zone checked reachable), the server keeps
// it for its maps (who is in which zone) and the retakes; a group wiped across several zones leaves each
// of them to retake; the lair's passage waits for every ambush of the zone before it (« ambushes:7 »);
// the zones' tables count the deaths and the revivals (null on the tables of before: shown « — »), and the
// results add every zone up per player (« Raid entier »).

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { ambushesIn, catalogById, lockOpenIn, reachableIn, seatZonesOf, zonesOf } from "../src/raidMap";
import { BRANCH, implausibleZone, lockOpen, maskOf, zoneCanonical, type ZoneContribution } from "../src/raidZones";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const EMBLEM = { shape: "star", primary: "#FFD27A", secondary: "#a66bff" };
const iso = (minutes: number) => new Date(Date.now() + minutes * 60000).toISOString();
const H1 = "0123456789abcdef";

async function clanOf(tag: string, n: number): Promise<{ id: string; players: TestPlayer[] }> {
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
    return { id, players };
}

async function runningEvent(c: { id: string; players: TestPlayer[] }): Promise<string> {
    const id = crypto.randomUUID();
    const now = new Date().toISOString();
    await env.DB.prepare(
        `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity, difficulty, rewards_text, published_at, created_at, updated_at)
         VALUES (?1, 'world', 'asterion', 'standard', NULL, ?2, 30, ?3, ?4, 1, 0, 50, 3, '', ?5, ?5, ?5)`,
    )
        .bind(id, iso(-10), iso(-40), iso(50), now)
        .run();
    await env.DB.prepare("INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at) VALUES (?1, ?2, ?3, ?3, '', ?4, ?4)")
        .bind(id, c.id, c.players[0]!.id, now)
        .run();
    let k = 0;
    for (const p of c.players)
        await env.DB.prepare("INSERT INTO raid_registrations (event_id, player_id, clan_id, created_at) VALUES (?1, ?2, ?3, ?4)")
            .bind(id, p.id, c.id, new Date(Date.now() - 60000 + ++k * 10).toISOString())
            .run();
    return id;
}

async function instanceOf(event: string, players: TestPlayer[]): Promise<string> {
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

const report = (p: TestPlayer, instance: string, body: Record<string, unknown>) =>
    api("POST", `${V}/raids/instances/${instance}/reports`, { token: p.token, body: { zone: 1, tick: 6000, hash: H1, ...body } });

const seatsOf = (...zones: number[]) => [...zones, ...new Array(8 - zones.length).fill(0)];

describe("2.78: the lair waits for every ambush of the zone before it", () => {
    it("opens with all four ambushes of the Poignet-verrou, never with the guard alone (the server's rules, the game's)", () => {
        const c = catalogById(BRANCH.id)!;
        const ids = c.objectives.filter((o) => o.zone === 7 && o.role === "ambush").map((o) => o.id);
        expect(ids).toHaveLength(4);
        expect(ids).toContain("poignet_verrou");
        expect(ambushesIn(c, 7, maskOf(ids.slice(0, 3)))).toEqual({ total: 4, left: 1 });
        expect(lockOpen("ambushes:7", maskOf(["poignet_verrou"]))).toBe(false);
        expect(lockOpen("ambushes:7", maskOf(ids.slice(0, 3)))).toBe(false);
        expect(lockOpen("ambushes:7", maskOf(ids))).toBe(true);
        expect(lockOpenIn(c, "ambushes:7", maskOf(ids))).toBe(true);
        // The lair is reachable only then (the generators open the Poignet-verrou).
        const gens = ["veine_generateur", "avantbras_generateur"];
        expect(reachableIn(c, maskOf([...gens, "poignet_verrou"]))[8]).toBe(false);
        expect(reachableIn(c, maskOf([...gens, ...ids]))[8]).toBe(false); // titan 9: not even past the Épaule's guard
        expect(reachableIn(c, maskOf(["epaule_arene", ...gens, ...ids]))[8]).toBe(true);
    });

    it("refuses a report from the lair before its ambushes are accepted", async () => {
        const a = await clanOf("AUBE", 1);
        const ev = await runningEvent(a);
        const ia = await instanceOf(ev, a.players);
        const r = await report(a.players[0]!, ia, { kind: "zones", zone: 8, seats: seatsOf(8) });
        expect(r.body.data).toMatchObject({ status: "rejected", reason: "zone verrouillée pour la branche" });
    });
});

describe("2.78: where each player is (report « zones »)", () => {
    it("is accepted by the majority, kept on the instance, shown on the branch's map with the names of my clan's players", async () => {
        const a = await clanOf("AUBE", 3);
        const ev = await runningEvent(a);
        const ia = await instanceOf(ev, a.players);
        const seats = seatsOf(3, 1, 1);
        // Titan 9: the Veine only once the Épaule's guard (an ambush) is beaten.
        expect((await report(a.players[0]!, ia, { kind: "zones", zone: 3, tick: 900, seats })).body.data.reason).toBe("zone verrouillée pour la branche");
        for (const p of a.players) await report(p, ia, { kind: "objective", objective: "epaule_arene", zone: 1, tick: 800 });
        expect((await report(a.players[0]!, ia, { kind: "zones", zone: 3, tick: 900, seats })).body.data.status).toBe("pending");
        // A liar (all in the Veine): a minority.
        expect((await report(a.players[2]!, ia, { kind: "zones", zone: 3, tick: 900, seats: seatsOf(3, 3, 3) })).body.data.status).toBe("pending");
        expect((await report(a.players[1]!, ia, { kind: "zones", zone: 3, tick: 900, seats })).body.data.status).toBe("accepted");
        const inst = await env.DB.prepare("SELECT zone, seat_zones FROM raid_instances WHERE id = ?1").bind(ia).first<{ zone: number; seat_zones: string }>();
        expect(inst).toEqual({ zone: 1, seat_zones: "3,1,1,0,0,0,0,0" });  // the instance's zone: the one of the most players
        expect(seatZonesOf(inst!)).toEqual([3, 1, 1, 0, 0, 0, 0, 0]);
        expect(zonesOf(inst!)).toEqual([1, 3]);
        const map = await api("GET", `${V}/raids/events/${ev}/branches/bras_droit`, { token: a.players[1]!.token });
        expect(map.status).toBe(200);
        const mine = map.body.data.instances.find((i: { id: string }) => i.id === ia);
        expect(mine.zones).toEqual([1, 3]);
        expect(map.body.data.zones.find((z: { id: number }) => z.id === 3).players_here).toEqual(["AUBEChef"]);
        expect(map.body.data.zones.find((z: { id: number }) => z.id === 1).players_here.sort()).toEqual(["AUBEMem1", "AUBEMem2"]);
        expect(map.body.data.my.zone).toBe(1);
        // An older report never moves them back (the newest step wins).
        for (const p of a.players.slice(0, 2)) await report(p, ia, { kind: "zones", zone: 1, tick: 600, seats: seatsOf(1, 1, 1) });
        const again = await env.DB.prepare("SELECT seat_zones FROM raid_instances WHERE id = ?1").bind(ia).first<{ seat_zones: string }>();
        expect(again!.seat_zones).toBe("3,1,1,0,0,0,0,0");
    });

    it("refuses a zone that does not exist, a locked one, or a report whose zone entered is not among the seats", async () => {
        const a = await clanOf("NEBU", 1);
        const ev = await runningEvent(a);
        const ia = await instanceOf(ev, a.players);
        expect((await report(a.players[0]!, ia, { kind: "zones", zone: 2, seats: seatsOf(1) })).body.data.reason).toContain("absente");
        expect((await report(a.players[0]!, ia, { kind: "zones", zone: 7, seats: seatsOf(7) })).body.data.reason).toContain("verrouillée");
        expect((await report(a.players[0]!, ia, { kind: "zones", zone: 1, seats: [1, 2, 3] })).status).toBe(400);
        expect((await report(a.players[0]!, ia, { kind: "zone", zone: 1, seats: seatsOf(1) })).status).toBe(400);
    });

    it("a group wiped across two zones leaves both to retake", async () => {
        const a = await clanOf("ORBE", 2);
        const ev = await runningEvent(a);
        const ia = await instanceOf(ev, a.players);
        for (const p of a.players) await report(p, ia, { kind: "objective", objective: "epaule_arene", zone: 1, tick: 2000 });  // titan 9
        for (const p of a.players) await report(p, ia, { kind: "wipe", zone: 3, tick: 3000, seats: seatsOf(3, 2) });  // titan 9: the Épaule is finished once anyone is past it
        const inst = await env.DB.prepare("SELECT wiped_zone, wiped_zones FROM raid_instances WHERE id = ?1").bind(ia).first<{ wiped_zone: number; wiped_zones: string }>();
        expect(inst).toEqual({ wiped_zone: 3, wiped_zones: "2,3" });
        const map = await api("GET", `${V}/raids/events/${ev}/branches/bras_droit`, { token: a.players[0]!.token });
        expect(map.body.data.to_retake.map((z: { zone: number }) => z.zone).sort()).toEqual([2, 3]);
    });
});

describe("2.78: deaths and revivals in the zones' tables", () => {
    const rows: ZoneContribution[] = [
        { seat: 1, from: 0, boss_damage: 120, damage: 300, support: 2, mechanisms: 1, objectives: 1, mini_damage: 40, deaths: 2, revives: 1, revived: 2 },
        { seat: 0, from: 0, boss_damage: 360, damage: 410, support: 0, mechanisms: 0, objectives: 1, mini_damage: 0, deaths: 1, revives: 2, revived: 1 },
    ];

    it("are compared as the games write them; impossible numbers refused before any majority; the tables of before stay valid", () => {
        expect(zoneCanonical(rows)).toBe("0,0,360,410,0,0,1,0,1,2,1;1,0,120,300,2,1,1,40,2,1,2;");
        expect(implausibleZone(rows, 6000)).toBe("");
        expect(implausibleZone([{ ...rows[0]!, revived: 3 }], 6000)).toContain("réanimations reçues");
        expect(implausibleZone([{ ...rows[0]!, mini_damage: 200 }], 6000)).toContain("mini-boss");
        expect(implausibleZone([{ ...rows[0]!, deaths: 500 }], 6000)).toContain("morts");
        const old: ZoneContribution[] = [{ seat: 0, from: 0, boss_damage: 360, damage: 410, support: 0, mechanisms: 0, objectives: 1 }];
        expect(zoneCanonical(old)).toBe("0,0,360,410,0,0,1;");
        expect(implausibleZone(old, 6000)).toBe("");
    });

    it("are kept with the table, null on a table of before, and added up per player in the results (« Raid entier »)", async () => {
        const a = await clanOf("AUBE", 2);
        const ev = await runningEvent(a);
        const ia = await instanceOf(ev, a.players);
        for (const p of a.players) await report(p, ia, { kind: "objective", objective: "epaule_arene", zone: 1, tick: 2000 });  // titan 9
        // All four or none.
        const partial = [{ seat: 0, from: 0, boss_damage: 0, damage: 0, support: 0, mechanisms: 0, objectives: 0, deaths: 1 }];
        expect((await report(a.players[0]!, ia, { kind: "zone_table", zone: 2, records: partial })).status).toBe(400);
        for (const p of a.players) expect((await report(p, ia, { kind: "zone_table", zone: 2, records: rows })).status).toBe(200);
        const old = [{ seat: 0, from: 0, boss_damage: 100, damage: 200, support: 1, mechanisms: 0, objectives: 1 }];
        for (const p of a.players) expect((await report(p, ia, { kind: "zone_table", zone: 3, records: old })).status).toBe(200);
        const res = await api("GET", `${V}/raids/events/${ev}/results`, { token: a.players[0]!.token });
        expect(res.status).toBe(200);
        const t2 = res.body.data.zone_tables.find((t: { zone: number }) => t.zone === 2);
        const t3 = res.body.data.zone_tables.find((t: { zone: number }) => t.zone === 3);
        expect(t2.rows.find((r: { seat: number }) => r.seat === 1)).toMatchObject({ deaths: 2, revives: 1, revived: 2, mini_damage: 4 });
        expect(t3.rows[0]).toMatchObject({ deaths: null, revives: null, revived: null, mini_damage: null });
        const totals = res.body.data.raid_totals as { username: string; deaths: number | null; revives: number | null; zones: number; boss_damage: number }[];
        const chef = totals.find((t) => t.username === "AUBEChef")!;
        expect(chef).toMatchObject({ zones: 2, deaths: 1, revives: 2, boss_damage: 46 });
        const mem = totals.find((t) => t.username === "AUBEMem1")!;
        expect(mem).toMatchObject({ zones: 1, deaths: 2, revives: 1 });
    });
});
