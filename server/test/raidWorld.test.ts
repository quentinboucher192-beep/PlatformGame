// Clan raids « Asterion », phase 5 (2.69, raidWorld.ts, docs/RAIDS.md §13 à §16): the world events
// shared by several clans. One titan for every clan (an objective destroyed by two instances at once
// counts once for the world, and for each clan whose group validated it; the duplicate journaled),
// the aggregation of the validated results only (idempotent: a duplicate or late table, parallel
// reports never count twice), what waits for its majority apart (« en attente de validation »), the
// official result frozen once at the close (a trigger forbids any change), the exclusive rewards
// granted once (the suit, the top-10 clan title), the clans and players told once, the catch-up
// sessions (silver suit, the official result unchanged), the late arrivals, the administrators'
// suspension and resumption, the map's check, the live panel and its journal.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { aggregate, checkMap, worldTick } from "../src/raidWorld";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const EMBLEM = { shape: "star", primary: "#FFD27A", secondary: "#a66bff" };
const iso = (minutes: number) => new Date(Date.now() + minutes * 60000).toISOString();
const H1 = "0123456789abcdef";
const H2 = "fedcba9876543210";
const TICK = 150000; // 20 min 50 s into an instance started 25 min ago

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

async function admin(): Promise<TestPlayer> {
    const p = await registerPlayer("AdminRaid");
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
    return p;
}

/** A world event running now (started 30 min ago, ends in 50), `clans` registered with every member. */
async function runningEvent(...clans: Clan[]): Promise<string> {
    const id = crypto.randomUUID();
    const now = new Date().toISOString();
    await env.DB.prepare(
        `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity, difficulty, rewards_text, published_at, created_at, updated_at)
         VALUES (?1, 'world', 'asterion', 'standard', NULL, ?2, 30, ?3, ?4, 1, 0, 50, 3, '', ?5, ?5, ?5)`,
    )
        .bind(id, iso(-30), iso(-60), iso(50), now)
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
    }
    return id;
}

/** The instance of the first player's group, started 25 min ago, `players` active in it (as its relay writes them). */
async function instanceOf(event: string, players: TestPlayer[]): Promise<string> {
    const r = await api("POST", `${V}/raids/events/${event}/instance`, { token: players[0]!.token, body: {} });
    expect(r.status, JSON.stringify(r.body)).toBe(200);
    const id = r.body.data.instance.id as string;
    await env.DB.prepare("UPDATE raid_instances SET started_at = ?2, players = ?3 WHERE id = ?1").bind(id, iso(-25), players.length).run();
    let seat = 0;
    for (const p of players)
        await env.DB.prepare("INSERT OR REPLACE INTO raid_instance_members (instance_id, player_id, seat, active, joined_at, left_at, seat_from) VALUES (?1, ?2, ?3, 1, ?4, NULL, 0)")
            .bind(id, p.id, seat++, iso(-25))
            .run();
    return id;
}

/** A plausible row of a table: the player of `seat`, from step 0. */
function row(seat: number, over: Record<string, number> = {}) {
    return { seat, from: 0, damage: 4000, boss_damage: 0, avoided: 600, blocks: 4, rescues: 1, vents: 0, weak_points: 0, objectives: 2, ticks: 60000, gigaboss: 0, ...over };
}
const table = (n: number, over: Record<string, number> = {}) => Array.from({ length: n }, (_, i) => row(i, over));

const report = (p: TestPlayer, inst: string, body: Record<string, unknown>) =>
    api("POST", `${V}/raids/instances/${inst}/reports`, { token: p.token, body: { kind: "objective", zone: 1, tick: TICK, hash: H1, ...body } });
const result = (p: TestPlayer, inst: string, records: unknown[], tick = TICK) => report(p, inst, { kind: "result", records, tick });

/** Every player of an instance reports the same table (the majority accepts it). */
async function accepted(players: TestPlayer[], inst: string, records: unknown[], tick = TICK) {
    let last = "";
    for (const p of players) last = (await result(p, inst, records, tick)).body.data.status;
    expect(last).toBe("accepted");
}

const world = (p: TestPlayer, ev: string) => api("GET", `${V}/raids/events/${ev}/world`, { token: p.token });
const count = async (sql: string, ...binds: unknown[]) => (await env.DB.prepare(sql).bind(...binds).first<{ n: number }>())!.n;

async function grants(eventId: string, reward: string) {
    return (await env.DB.prepare("SELECT player_id FROM raid_reward_grants WHERE event_id = ?1 AND reward = ?2 ORDER BY player_id").bind(eventId, reward).all<{ player_id: string }>()).results.map(
        (r) => r.player_id,
    );
}

/** Moves a running event into the past: ended 15 min ago (after the last reports' grace). */
const endEvent = (ev: string) => env.DB.prepare("UPDATE raid_events SET starts_at = ?2, lock_at = ?3, ends_at = ?4 WHERE id = ?1").bind(ev, iso(-100), iso(-130), iso(-15)).run();

describe("raids phase 5: the titan's map is checked by the server", () => {
    it("the shipped catalogs are valid: 7 branches, 56 zones, 7 lairs", () => {
        const c = checkMap();
        expect(c.problems).toEqual([]);
        expect(c.ok).toBe(true);
        expect(c.branches).toBe(7);
        expect(c.zones).toBe(56);
        expect(c.bosses).toBe(7);
        expect(c.fights).toBeGreaterThanOrEqual(1);
    });

    it("« Valider la carte » is stored and journaled; publishing needs a valid map", async () => {
        const a = await admin();
        const c = await api("POST", `${V}/admin/raids`, { token: a.token, body: { format: "standard", starts_at: iso(3 * 1440), late_minutes: 20 } });
        expect(c.status, JSON.stringify(c.body)).toBe(201);
        const id = c.body.data.event.id as string;
        expect(c.body.data.event.late_minutes).toBe(20);
        const v = await api("POST", `${V}/admin/raids/${id}/validate-map`, { token: a.token });
        expect(v.status).toBe(200);
        expect(v.body.data.map.ok).toBe(true);
        expect(v.body.data.map.zones).toBe(56);
        expect((await api("POST", `${V}/admin/raids/${id}/publish`, { token: a.token })).status).toBe(200);
        expect(await count("SELECT COUNT(*) AS n FROM audit_log WHERE action = 'admin_raid_map_validated'")).toBe(1);
        expect(await count("SELECT COUNT(*) AS n FROM raid_world_journal WHERE event_id = ?1 AND code = 'map_validated' AND actor_id = ?2", id, a.id)).toBe(1);
        // A forged invalid check of the catalogs played now refuses a publication.
        const d = await api("POST", `${V}/admin/raids`, { token: a.token, body: { format: "court", starts_at: iso(2 * 1440) } });
        const id2 = d.body.data.event.id as string;
        const bad = { ...v.body.data.map, ok: false, problems: ["Crâne : zone 3 inatteignable"] };
        await env.DB.prepare("UPDATE raid_events SET map_check = ?2 WHERE id = ?1").bind(id2, JSON.stringify(bad)).run();
        const refused = await api("POST", `${V}/admin/raids/${id2}/publish`, { token: a.token });
        expect(refused.status).toBe(409);
        expect(refused.body.error.code).toBe("RAID_MAP_INVALID");
        // The late arrivals: null is « until the end ».
        expect((await api("PATCH", `${V}/admin/raids/${id2}`, { token: a.token, body: { late_minutes: null } })).status).toBe(200);
    });
});

describe("raids phase 5: several clans, one titan", () => {
    it("a clan of 2, a clan of 8 and another of 2: an objective destroyed at once by two clans counts once for the world, once for each clan, journaled", async () => {
        const a = await clan("AUBE", 2);
        const b = await clan("NEBU", 8);
        const c = await clan("KRN", 2);
        const ev = await runningEvent(a, b, c);
        const ia = await instanceOf(ev, a.players);
        const ib = await instanceOf(ev, b.players);
        await instanceOf(ev, c.players);
        // Every report of both instances at the same moment.
        const all = await Promise.all([
            ...a.players.map((p) => report(p, ia, { objective: "epaule_arene" })),
            ...b.players.map((p) => report(p, ib, { objective: "epaule_arene", hash: H2 })),
        ]);
        expect(all.every((r) => r.status === 200 && r.body.data.status !== "rejected")).toBe(true);
        expect(await count("SELECT COUNT(*) AS n FROM raid_branch_objectives WHERE event_id = ?1", ev)).toBe(1);
        const w = await world(a.players[0]!, ev);
        expect(w.status, JSON.stringify(w.body)).toBe(200);
        const d = w.body.data;
        expect(d.world.objectives_done).toBe(1);
        expect(d.world.clans).toBe(3);
        expect(d.event.final).toBe(false);
        const byTag = (t: string) => d.clans.find((x: { tag: string }) => x.tag === t);
        expect(byTag("AUBE").objectives).toBe(1);
        expect(byTag("NEBU").objectives).toBe(1);
        expect(byTag("KRN").objectives).toBe(0);
        expect(byTag("AUBE").first + byTag("NEBU").first).toBe(1); // credited once, to whoever was first
        expect(byTag("AUBE").mine).toBe(true);
        expect(d.groups.filter((g: { objectives: number }) => g.objectives === 1)).toHaveLength(2);
        // The duplicate journaled for the administrators: counted once.
        const dup = await env.DB.prepare("SELECT message FROM raid_world_journal WHERE event_id = ?1 AND code = 'duplicate_objective'").bind(ev).all<{ message: string }>();
        expect(dup.results).toHaveLength(1);
        expect(dup.results[0]!.message).toContain("compté une fois");
        // Another of the same objective, later: still once.
        expect((await report(c.players[0]!, (await instanceOf(ev, c.players)), { objective: "epaule_arene" })).body.data.status).not.toBe("rejected");
        expect((await aggregate(env.DB, ev)).world.objectives_done).toBe(1);
    });

    it("the aggregation is idempotent: a repeated, late or older table never counts twice; parallel reports neither", async () => {
        const a = await clan("AUBE", 8);
        const b = await clan("NEBU", 2);
        const ev = await runningEvent(a, b);
        const ia = await instanceOf(ev, a.players);
        const ib = await instanceOf(ev, b.players);
        // Both instances' players all at once (8 and 2 reports in parallel).
        await Promise.all([...a.players.map((p) => result(p, ia, table(8))), ...b.players.map((p) => result(p, ib, table(2, { damage: 9000 })))]);
        expect(await count("SELECT COUNT(*) AS n FROM raid_accepted WHERE instance_id IN (?1, ?2) AND report_key LIKE 'r:%'", ia, ib)).toBe(2);
        const first = await aggregate(env.DB, ev);
        expect(first.world.players).toBe(10);
        expect(first.clans.map((c) => c.players).sort()).toEqual([2, 8]);
        // The same tables again (a reconnection, a repeated request), in parallel: nothing moves.
        await Promise.all([...a.players.map((p) => result(p, ia, table(8))), ...b.players.map((p) => result(p, ib, table(2, { damage: 9000 })))]);
        expect(await aggregate(env.DB, ev)).toEqual(first);
        // An older table accepted late (a lower step): ignored.
        await accepted(b.players, ib, table(2, { damage: 100, ticks: 1000 }), TICK - 12000);
        expect(await aggregate(env.DB, ev)).toEqual(first);
        // A newer one replaces it (the numbers only grow).
        await accepted(b.players, ib, table(2, { damage: 9500, ticks: 70000 }), TICK + 600);
        const after = await aggregate(env.DB, ev);
        expect(after.players.find((p) => p.id === b.players[0]!.id)!.damage).toBe(950);
        expect(after.world.players).toBe(10);
    });

    it("what waits for its majority is apart, « en attente de validation », never ranked", async () => {
        const a = await clan("AUBE", 2);
        const ev = await runningEvent(a);
        const ia = await instanceOf(ev, a.players);
        const [p1, p2] = a.players as [TestPlayer, TestPlayer];
        expect((await result(p1, ia, table(2))).body.data.status).toBe("pending");
        let d = (await world(p1, ev)).body.data;
        expect(d.players).toHaveLength(0);
        expect(d.pending.tables).toBe(1);
        expect(d.my.pending).toBe(true);
        expect(d.my.rank).toBe(0);
        expect((await result(p2, ia, table(2))).body.data.status).toBe("accepted");
        d = (await world(p1, ev)).body.data;
        expect(d.players).toHaveLength(2);
        expect(d.pending.tables).toBe(0);
        expect(d.my.pending).toBe(false);
        expect(d.my.rank).toBeGreaterThan(0);
        expect(d.my.clan_rank).toBe(1);
        // A clan raid has no world ranking.
        const cr = crypto.randomUUID();
        const now = new Date().toISOString();
        await env.DB.prepare(
            `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity, difficulty, rewards_text, published_at, created_at, updated_at)
             VALUES (?1, 'clan', 'asterion', 'court', ?2, ?3, 30, ?4, ?5, 1, 8, 50, 3, '', ?6, ?6, ?6)`,
        )
            .bind(cr, a.id, iso(-10), iso(-40), iso(20), now)
            .run();
        expect((await world(p1, cr)).status).toBe(404);
    });

    it("late arrivals: after the time allowed, only the players already in play come back", async () => {
        const a = await clan("AUBE", 3);
        const ev = await runningEvent(a);
        const [p1, p2] = a.players as [TestPlayer, TestPlayer];
        await instanceOf(ev, [p1]);
        await env.DB.prepare("UPDATE raid_events SET late_minutes = 10 WHERE id = ?1").bind(ev).run(); // started 30 min ago
        const back = await api("POST", `${V}/raids/events/${ev}/join`, { token: p1.token, body: {} });
        expect(back.status, JSON.stringify(back.body)).toBe(200);
        const late = await api("POST", `${V}/raids/events/${ev}/join`, { token: p2.token, body: {} });
        expect(late.status).toBe(409);
        expect(late.body.error.code).toBe("RAID_LATE");
        await env.DB.prepare("UPDATE raid_events SET late_minutes = NULL WHERE id = ?1").bind(ev).run();
        expect((await api("POST", `${V}/raids/events/${ev}/join`, { token: p2.token, body: {} })).status).toBe(200);
    });
});

describe("raids phase 5: the close, the official result and the exclusive rewards", () => {
    async function played() {
        const a = await clan("AUBE", 2);
        const b = await clan("NEBU", 2);
        const ev = await runningEvent(a, b);
        const ia = await instanceOf(ev, a.players);
        const ib = await instanceOf(ev, b.players);
        await accepted(a.players, ia, table(2, { objectives: 4, rescues: 2 }));
        await accepted(b.players, ib, table(2, { ticks: 20000 })); // 2 min 46 s: no participation suit
        for (const p of a.players) await report(p, ia, { objective: "epaule_arene" });
        return { a, b, ev, ia, ib };
    }

    it("the suit is granted during the event from validated tables only, once; the close freezes once, rewards the top 10 once, tells once", async () => {
        const { a, b, ev, ia } = await played();
        // The exclusive suit as soon as a table with 5 min of combat is accepted (never twice).
        expect(await grants(ev, "asterion_ecorce")).toEqual(a.players.map((p) => p.id).sort());
        await accepted(a.players, ia, table(2, { objectives: 4, rescues: 2 }));
        expect(await grants(ev, "asterion_ecorce")).toHaveLength(2);
        expect(await worldTick(env.DB)).toBe(0); // still running: nothing closes
        await endEvent(ev);
        expect(await worldTick(env.DB)).toBe(1);
        const frozen = await env.DB.prepare("SELECT outcome, data, rewards_at, notified_at FROM raid_event_results WHERE event_id = ?1").bind(ev).first<{
            outcome: string;
            data: string;
            rewards_at: string | null;
            notified_at: string | null;
        }>();
        expect(frozen!.outcome).toBe("survecu");
        expect(frozen!.rewards_at).not.toBeNull();
        expect(frozen!.notified_at).not.toBeNull();
        // Both clans are in the top 10 with validated points: the clan title and its players' title.
        expect(await count("SELECT COUNT(*) AS n FROM raid_clan_titles WHERE event_id = ?1", ev)).toBe(2);
        expect(await grants(ev, "asterion_clan_top10")).toEqual([...a.players, ...b.players].map((p) => p.id).sort());
        const notes = async (p: TestPlayer) =>
            ((await api("GET", `${V}/notifications?filter=raids&limit=50`, { token: p.token })).body.data.notifications as { text: string; data: { event: string } }[]).filter(
                (n) => n.data.event === "results",
            );
        const told = await notes(a.players[1]!);
        expect(told).toHaveLength(1);
        expect(told[0]!.text).toContain("Résultats officiels");
        expect(told[0]!.text).toContain("[AUBE] 1er sur 2 clans");
        // The cron again, a reading again: nothing twice.
        expect(await worldTick(env.DB)).toBe(0);
        const w = await world(a.players[0]!, ev);
        expect(w.body.data.event.final).toBe(true);
        expect(w.body.data.outcome.text).toBe("Asterion a survécu");
        expect(w.body.data.clans[0].tag).toBe("AUBE");
        expect(await notes(a.players[1]!)).toHaveLength(1);
        expect(await grants(ev, "asterion_clan_top10")).toHaveLength(4);
        // Frozen: the database refuses any change, a late report is refused.
        await expect(env.DB.prepare("UPDATE raid_event_results SET data = '{}' WHERE event_id = ?1").bind(ev).run()).rejects.toThrow();
        expect((await report(a.players[0]!, ia, { kind: "result", records: table(2, { damage: 99999 }) })).body.data.status).toBe("rejected");
        // Claimed twice: once.
        const c1 = await api("POST", `${V}/raids/rewards/claim`, { token: a.players[0]!.token, body: {} });
        expect(c1.body.data.claimed).toBeGreaterThanOrEqual(2);
        expect((await api("POST", `${V}/raids/rewards/claim`, { token: a.players[0]!.token, body: {} })).body.data.claimed).toBe(0);
        expect(await count("SELECT COUNT(*) AS n FROM player_won_rewards WHERE player_id = ?1 AND reward_ref = 'suit_ecorce_asterion'", a.players[0]!.id)).toBe(1);
        expect(await count("SELECT COUNT(*) AS n FROM player_won_rewards WHERE player_id = ?1 AND reward_ref = 'title_devoreurs_devores'", a.players[0]!.id)).toBe(1);
        // The history of the tab RAIDS keeps the official result.
        const ov = await api("GET", `${V}/raids`, { token: b.players[0]!.token });
        const h = (ov.body.data.history as { id: string; result: { text: string; my_clan_rank: number } | null }[]).find((x) => x.id === ev)!;
        expect(h.result!.text).toBe("Asterion a survécu");
        expect(h.result!.my_clan_rank).toBe(2);
    });

    it("a catch-up session: the silver suit, its own ranking, the official result never changes", async () => {
        const { a, ev } = await played();
        await endEvent(ev);
        await worldTick(env.DB);
        const before = (await env.DB.prepare("SELECT data FROM raid_event_results WHERE event_id = ?1").bind(ev).first<{ data: string }>())!.data;
        const officialView = (await world(a.players[0]!, ev)).body.data;
        const adm = await admin();
        const open = await api("POST", `${V}/admin/raids/${ev}/catchup`, { token: adm.token, body: { days: 3 } });
        expect(open.status, JSON.stringify(open.body)).toBe(201);
        const cu = open.body.data.event.id as string;
        expect((await api("POST", `${V}/admin/raids/${ev}/catchup`, { token: adm.token, body: { days: 3 } })).body.error.code).toBe("CATCHUP_OPEN");
        expect((await api("POST", `${V}/admin/raids/${cu}/catchup`, { token: adm.token, body: {} })).body.error.code).toBe("RAID_IS_CATCHUP");
        expect(await count("SELECT COUNT(*) AS n FROM audit_log WHERE action = 'admin_raid_catchup'")).toBe(1);
        // Absent players of another clan discover it: a member's registration enters the clan.
        const k = await clan("KRN", 2);
        for (const p of k.players) expect((await api("POST", `${V}/raids/events/${cu}/register`, { token: p.token })).status).toBe(201);
        const sheet = await api("GET", `${V}/raids/events/${cu}`, { token: k.players[0]!.token });
        expect(sheet.body.data.event.catchup).toBe(true);
        expect(sheet.body.data.event.catchup_of).toBe(ev);
        const inst = await instanceOf(cu, k.players);
        await accepted(k.players, inst, table(2, { damage: 50000, objectives: 9 }));
        for (const p of k.players) await report(p, inst, { objective: "epaule_arene" });
        expect(await grants(cu, "asterion_ecorce_argent")).toEqual(k.players.map((p) => p.id).sort());
        expect(await grants(cu, "asterion_ecorce")).toHaveLength(0);
        // The official result: the same bytes, the same view.
        expect((await env.DB.prepare("SELECT data FROM raid_event_results WHERE event_id = ?1").bind(ev).first<{ data: string }>())!.data).toBe(before);
        const again = (await world(a.players[0]!, ev)).body.data;
        expect(again.clans).toEqual(officialView.clans);
        expect(again.world).toEqual(officialView.world);
        const cw = (await world(k.players[0]!, cu)).body.data;
        expect(cw.event.catchup).toBe(true);
        expect(cw.official.frozen).toBe(true);
        expect(cw.official.text).toBe("Asterion a survécu");
        expect(cw.players).toHaveLength(2);
        // Ended, the session is never frozen as an official result, and grants no ranking reward.
        await endEvent(cu);
        await worldTick(env.DB);
        expect(await count("SELECT COUNT(*) AS n FROM raid_event_results WHERE event_id = ?1", cu)).toBe(0);
        expect(await grants(cu, "asterion_clan_top10")).toHaveLength(0);
    });
});

describe("raids phase 5: the administrators", () => {
    it("suspend and resume: no entry, no report while suspended, the time given back; every action journaled", async () => {
        const a = await clan("AUBE", 2);
        const ev = await runningEvent(a);
        const ia = await instanceOf(ev, a.players);
        const adm = await admin();
        expect((await api("POST", `${V}/admin/raids/${ev}/suspend`, { token: a.players[0]!.token, body: {} })).status).toBe(404); // not an admin
        const s = await api("POST", `${V}/admin/raids/${ev}/suspend`, { token: adm.token, body: { reason: "Désynchronisation" } });
        expect(s.status, JSON.stringify(s.body)).toBe(200);
        expect((await api("POST", `${V}/admin/raids/${ev}/suspend`, { token: adm.token, body: {} })).body.error.code).toBe("RAID_SUSPENDED");
        const sheet = await api("GET", `${V}/raids/events/${ev}`, { token: a.players[0]!.token });
        expect(sheet.body.data.event.suspended).toBe(true);
        expect(sheet.body.data.event.suspended_reason).toBe("Désynchronisation");
        const join = await api("POST", `${V}/raids/events/${ev}/join`, { token: a.players[1]!.token, body: {} });
        expect(join.status).toBe(409);
        expect(join.body.error.code).toBe("RAID_SUSPENDED");
        const r = await report(a.players[0]!, ia, { objective: "epaule_arene" });
        expect(r.body.data.status).toBe("rejected");
        expect(r.body.data.reason).toContain("suspendu");
        const endsBefore = (await env.DB.prepare("SELECT ends_at FROM raid_events WHERE id = ?1").bind(ev).first<{ ends_at: string }>())!.ends_at;
        await env.DB.prepare("UPDATE raid_events SET suspended_at = ?2 WHERE id = ?1").bind(ev, iso(-5)).run(); // suspended 5 min ago
        const res = await api("POST", `${V}/admin/raids/${ev}/resume`, { token: adm.token });
        expect(res.status, JSON.stringify(res.body)).toBe(200);
        expect(res.body.data.extended_minutes).toBe(5);
        const endsAfter = (await env.DB.prepare("SELECT ends_at FROM raid_events WHERE id = ?1").bind(ev).first<{ ends_at: string }>())!.ends_at;
        expect(Date.parse(endsAfter) - Date.parse(endsBefore)).toBeGreaterThanOrEqual(4.9 * 60000);
        expect((await report(a.players[0]!, ia, { objective: "epaule_arene" })).body.data.status).toBe("pending");
        const actions = (await env.DB.prepare("SELECT action FROM audit_log WHERE action LIKE 'admin_raid_%' ORDER BY id").all<{ action: string }>()).results.map((x) => x.action);
        expect(actions).toEqual(["admin_raid_suspended", "admin_raid_resumed"]);
        const log = await api("GET", `${V}/admin/log?kind=raid`, { token: adm.token });
        expect((log.body.data.entries as { action: string }[]).map((e) => e.action)).toContain("admin_raid_suspended");
        const notes = (await api("GET", `${V}/notifications?filter=raids&limit=50`, { token: a.players[1]!.token })).body.data.notifications as { data: { event: string } }[];
        expect(notes.filter((n) => n.data.event === "suspended")).toHaveLength(1);
        expect(notes.filter((n) => n.data.event === "resumed")).toHaveLength(1);
    });

    it("the live panel shows real figures: players in play, instances, reconnections, refused reports, the journal", async () => {
        const a = await clan("AUBE", 3);
        const ev = await runningEvent(a);
        const ia = await instanceOf(ev, a.players);
        const adm = await admin();
        // A reconnection as the relay writes it (absent, then back): counted by the database.
        await env.DB.prepare("UPDATE raid_instance_members SET active = 0 WHERE instance_id = ?1 AND player_id = ?2").bind(ia, a.players[2]!.id).run();
        await env.DB.prepare("UPDATE raid_instance_members SET active = 1 WHERE instance_id = ?1 AND player_id = ?2").bind(ia, a.players[2]!.id).run();
        await env.DB.prepare("UPDATE raid_instance_members SET active = 1 WHERE instance_id = ?1 AND player_id = ?2").bind(ia, a.players[2]!.id).run(); // no change: not a reconnection
        // A minority that disagrees: refused and journaled.
        await report(a.players[0]!, ia, { objective: "epaule_arene" });
        await report(a.players[1]!, ia, { objective: "epaule_arene" });
        await report(a.players[2]!, ia, { objective: "epaule_arene", hash: H2 });
        await worldTick(env.DB); // the start journaled once
        await worldTick(env.DB);
        const l = await api("GET", `${V}/admin/raids/${ev}/live`, { token: adm.token });
        expect(l.status, JSON.stringify(l.body)).toBe(200);
        const d = l.body.data;
        expect(d.stats.players).toBe(3);
        expect(d.stats.instances).toBe(1);
        expect(d.stats.reconnects).toBe(1);
        expect(d.stats.rejected).toBe(1);
        expect(d.stats.gigaboss).toBe(7);
        expect(d.stats.registered).toBe(3);
        expect(d.instances).toHaveLength(1);
        expect(d.instances[0].zone_name).toBe("Épaule");
        expect(d.instances[0].latency_ms).toBeNull(); // no game measured it: nothing invented
        const texts = (d.journal as { text: string }[]).map((j) => j.text);
        expect(texts.some((t) => t.startsWith("Rapport refusé"))).toBe(true);
        expect(texts.filter((t) => t.startsWith("Début de l'événement"))).toHaveLength(1);
        expect((await api("GET", `${V}/admin/raids/${ev}/live`, { token: a.players[0]!.token })).status).toBe(404);
    });
});
