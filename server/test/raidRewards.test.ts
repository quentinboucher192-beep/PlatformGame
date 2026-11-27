// Clan raids « Asterion », phase 3 (2.68, raidZones.ts + raidRewards.ts, docs/RAIDS.md): the
// gigaboss PYROCLAST's fights (phases accepted by the consensus), the players' contributions
// (tables computed by the games, accepted by the majority, mapped to the players by seat and
// step; implausible numbers refused; ties never accepted), Asterion's body from the validated
// state only, the results of an event (« en attente de validation » while a table waits), the
// rewards granted once per player, reward and event (a double claim, a replayed request, a
// reconnection or two acceptances never grant twice), the badge an account may only declare once
// claimed, and the administrators' tests that never report.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { canonical, scoreOf, PERFORMANCE_SCORE, type Contribution } from "../src/raidRewards";
import { holderOf, implausible } from "../src/raidZones";
import { api, deviceOf, registerPlayer, seedAll, type TestPlayer } from "./helpers";
import { raidGroups } from "./raidGroups";

beforeEach(seedAll);

const V = "/api/v1";
const EMBLEM = { shape: "star", primary: "#FFD27A", secondary: "#a66bff" };
const iso = (minutes: number) => new Date(Date.now() + minutes * 60000).toISOString();
const H1 = "0123456789abcdef";
const H2 = "fedcba9876543210";

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

async function runningEvent(c: Clan, endsIn = 50): Promise<string> {
    const id = crypto.randomUUID();
    const now = new Date().toISOString();
    await env.DB.prepare(
        `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity, difficulty, rewards_text, published_at, created_at, updated_at)
         VALUES (?1, 'world', 'asterion', 'standard', NULL, ?2, 30, ?3, ?4, 1, 0, 50, 3, '', ?5, ?5, ?5)`,
    )
        .bind(id, iso(-30), iso(-60), iso(endsIn), now)
        .run();
    await env.DB.prepare("INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at) VALUES (?1, ?2, ?3, ?3, '', ?4, ?4)")
        .bind(id, c.id, c.players[0]!.id, now)
        .run();
    let k = 0;
    for (const p of c.players)
        await env.DB.prepare("INSERT INTO raid_registrations (event_id, player_id, clan_id, created_at) VALUES (?1, ?2, ?3, ?4)")
            .bind(id, p.id, c.id, new Date(Date.now() - 60000 + ++k * 10).toISOString())
            .run();
    await raidGroups(id, c.id, { bras_droit: c.players }); // §41: the clan's seven groups (14 registered at least)
    return id;
}

/** The wrist broken (the long level before the hand): the gigaboss's zone is reachable. */
async function unlockHand(event: string) {
    // 2.78: every ambush of the wrist beaten (the lair's passage waits for all of them); titan 9: and the Épaule's guard.
    for (const id of ["epaule_arene", "poignet_verrou", "poignet_embuscade_1", "poignet_embuscade_2", "poignet_embuscade_3", "poignet_galerie_1"])
        await env.DB.prepare("INSERT OR IGNORE INTO raid_branch_objectives (event_id, branch, objective_id, instance_id, accepted_at) VALUES (?1, 'asterion_bras_droit', ?3, NULL, ?2)")
            .bind(event, new Date().toISOString(), id)
            .run();
}

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

/** A plausible row: a player who held seat `seat` from step 0 for `ticks` steps. */
function row(seat: number, over: Partial<Contribution> = {}): Contribution {
    return { seat, from: 0, damage: 4000, boss_damage: 3000, avoided: 600, blocks: 4, rescues: 1, vents: 3, weak_points: 2, objectives: 3, ticks: 120000, gigaboss: 1, ...over };
}

const TICK = 150000; // 20 min 50 s into the instance (started 25 min ago)
const send = (p: TestPlayer, inst: string, body: Record<string, unknown>) =>
    api("POST", `${V}/raids/instances/${inst}/reports`, { token: p.token, body: { zone: 8, tick: TICK, hash: H1, ...body } });

async function grants(eventId: string) {
    return (await env.DB.prepare("SELECT player_id, reward, claimed_at FROM raid_reward_grants WHERE event_id = ?1 ORDER BY player_id, reward").bind(eventId).all<{
        player_id: string;
        reward: string;
        claimed_at: string | null;
    }>()).results;
}

/** Two players of one instance: Pyroclast fell, their table accepted. */
async function fallen(tag: string) {
    const a = await clan(tag, 2);
    const ev = await runningEvent(a);
    await unlockHand(ev);
    const inst = await instanceOf(ev, a.players);
    const [p1, p2] = a.players as [TestPlayer, TestPlayer];
    for (const p of [p1, p2]) expect((await send(p, inst, { kind: "boss", phase: 4 })).body.data.status).not.toBe("rejected");
    const records = [row(0), row(1, { damage: 200, boss_damage: 100, vents: 0, weak_points: 0, ticks: 30000 })];
    expect((await send(p1, inst, { kind: "result", records })).body.data.status).toBe("pending");
    expect((await send(p2, inst, { kind: "result", records })).body.data.status).toBe("accepted");
    return { a, ev, inst, p1, p2 };
}

describe("raids phase 3: rules of the contributions", () => {
    it("the score mixes every category: the damage alone never wins", () => {
        const hitter = scoreOf({ damage: 100000, boss_damage: 100000, avoided: 0, blocks: 0, rescues: 0, vents: 0, weak_points: 0, objectives: 0, ticks: 36000, gigaboss: 1 });
        const helper = scoreOf({ damage: 2000, boss_damage: 1000, avoided: 800, blocks: 10, rescues: 3, vents: 6, weak_points: 3, objectives: 4, ticks: 36000, gigaboss: 1 });
        expect(hitter.damage).toBe(500); // capped
        expect(helper.total).toBeGreaterThan(hitter.total);
        expect(hitter.total).toBeLessThan(PERFORMANCE_SCORE);
    });

    it("the table's text is the game's (Contributions::canonical), whatever the order", () => {
        const t = canonical([row(1, { gigaboss: 0 }), row(0)]);
        expect(t.startsWith("0,0,4000,3000,600,4,1,3,2,3,120000,1;1,0,")).toBe(true);
        expect(canonical([])).toBe("");
    });

    it("implausible tables are refused", () => {
        expect(implausible([row(0)], TICK)).toBe("");
        expect(implausible([row(0, { ticks: TICK + 10 })], TICK)).not.toBe("");
        expect(implausible([row(0, { vents: 5000 })], TICK)).not.toBe("");
        expect(implausible([row(0, { boss_damage: 5000 })], TICK)).not.toBe(""); // more than all the damage
        expect(implausible([row(0, { from: TICK + 1 })], TICK)).not.toBe("");
        expect(implausible([row(0), row(0)], TICK)).not.toBe(""); // the same player twice
        expect(implausible([row(0, { rescues: 9000 })], TICK)).not.toBe("");
    });

    it("a seat's rows go to whoever held it from that step (a late join takes a freed seat)", () => {
        const members = [
            { player_id: "a", seat: 2, seat_from: 0, joined_at: "1" },
            { player_id: "b", seat: 2, seat_from: 9000, joined_at: "2" },
            { player_id: "c", seat: 3, seat_from: null, joined_at: "3" },
        ];
        expect(holderOf(members, 2, 0)).toBe("a");
        expect(holderOf(members, 2, 9100)).toBe("b");
        expect(holderOf(members, 3, 50)).toBe("c");
        expect(holderOf(members, 5, 0)).toBeNull();
    });
});

describe("raids phase 3: consensus of the gigaboss and of the tables", () => {
    it("the fall and the table need the majority; the rows are the players'; the rewards come from them", async () => {
        const { ev, inst, p1, p2 } = await fallen("PYRO");
        const fight = await env.DB.prepare("SELECT phase, defeated_tick FROM raid_gigaboss_fights WHERE instance_id = ?1").bind(inst).first<{ phase: number; defeated_tick: number }>();
        expect(fight).toEqual({ phase: 4, defeated_tick: TICK });
        const rows = (await env.DB.prepare("SELECT seat, player_id, score FROM raid_contributions WHERE instance_id = ?1 ORDER BY seat").bind(inst).all<{ seat: number; player_id: string; score: number }>()).results;
        expect(rows.map((r) => r.player_id)).toEqual([p1.id, p2.id]);
        expect(rows[0]!.score).toBeGreaterThanOrEqual(PERFORMANCE_SCORE);
        // p1: present at the fall, 16 min 40 s, a high mixed score; p2: present at the fall only (4 min 10 s).
        // 2.69: a world event also gives its exclusive suit to the 5 minutes of validated combat (raidWorld.ts).
        const g = await grants(ev);
        expect(g.filter((x) => x.player_id === p1.id).map((x) => x.reward)).toEqual(["asterion_ecorce", "asterion_participation", "pyroclast_badge", "pyroclast_performance"]);
        expect(g.filter((x) => x.player_id === p2.id).map((x) => x.reward)).toEqual(["pyroclast_badge"]);
    });

    it("a lying minority is journaled and ignored; a tie is never accepted", async () => {
        const a = await clan("TIE", 2);
        const ev = await runningEvent(a);
        await unlockHand(ev);
        const inst = await instanceOf(ev, a.players);
        const [p1, p2] = a.players as [TestPlayer, TestPlayer];
        expect((await send(p1, inst, { kind: "result", records: [row(0), row(1)] })).body.data.status).toBe("pending");
        // The other says another table: one against one, nobody wins.
        expect((await send(p2, inst, { kind: "result", records: [row(0, { damage: 9000 }), row(1)] })).body.data.status).toBe("pending");
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_contributions WHERE instance_id = ?1").bind(inst).first<{ n: number }>())!.n).toBe(0);
        expect(await grants(ev)).toEqual([]);
        // Three players: the majority wins, the liar is journaled.
        const b = await clan("TRIO", 3);
        const ev2 = await runningEvent(b);
        await unlockHand(ev2);
        const inst2 = await instanceOf(ev2, b.players);
        const [q1, q2, q3] = b.players as [TestPlayer, TestPlayer, TestPlayer];
        const good = [row(0), row(1), row(2)];
        await send(q1, inst2, { kind: "result", records: good });
        await send(q2, inst2, { kind: "result", records: [row(0), row(1, { vents: 40 }), row(2)], hash: H2 });
        expect((await send(q3, inst2, { kind: "result", records: good })).body.data.status).toBe("accepted");
        const journal = (await env.DB.prepare("SELECT player_id FROM raid_report_rejections WHERE instance_id = ?1").bind(inst2).all<{ player_id: string }>()).results;
        expect(journal.map((j) => j.player_id)).toEqual([q2.id]);
        const vents = (await env.DB.prepare("SELECT vents FROM raid_contributions WHERE instance_id = ?1 AND seat = 1").bind(inst2).first<{ vents: number }>())!.vents;
        expect(vents).toBe(3);
    });

    it("an implausible table and a gigaboss outside its zone are refused before any majority", async () => {
        const a = await clan("BAD", 1);
        const ev = await runningEvent(a);
        await unlockHand(ev);
        const inst = await instanceOf(ev, a.players);
        const p = a.players[0]!;
        const r1 = await send(p, inst, { kind: "result", records: [row(0, { ticks: TICK * 2 })] });
        expect(r1.body.data.status).toBe("rejected");
        const r2 = await send(p, inst, { kind: "boss", phase: 4, zone: 3 });
        expect(r2.body.data.status).toBe("rejected");
        const r3 = await send(p, inst, { kind: "result", records: [{ ...row(0), bonus: 3 }] });
        expect(r3.status).toBe(400);
        expect(await grants(ev)).toEqual([]);
    });

    it("an administrator's test never reports: a report flagged as a test is refused, admin or not", async () => {
        const a = await clan("ADM", 1);
        const ev = await runningEvent(a);
        await unlockHand(ev);
        const inst = await instanceOf(ev, a.players);
        const p = a.players[0]!;
        await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
        for (const body of [{ kind: "boss", phase: 4, test: true }, { kind: "result", records: [row(0)], test: true }]) {
            const r = await send(p, inst, body);
            expect(r.status).toBe(403);
            expect(r.body.error.code).toBe("RAID_TEST_REPORT");
        }
        const stranger = await registerPlayer("PasAdmin");
        expect((await send(stranger, inst, { kind: "boss", phase: 4, test: true })).status).toBe(403);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_reports WHERE instance_id = ?1").bind(inst).first<{ n: number }>())!.n).toBe(0);
        expect(await grants(ev)).toEqual([]);
    });
});

describe("raids phase 3: Asterion's body, results and rewards", () => {
    it("the right arm is attacked, weakened, then destroyed from the validated state only", async () => {
        const a = await clan("BODY", 1);
        const ev = await runningEvent(a);
        const inst = await instanceOf(ev, a.players);
        const p = a.players[0]!;
        const body = async () => (await api("GET", `${V}/raids/events/${ev}`, { token: p.token })).body.data.event;
        expect((await body()).body).toEqual({});
        await unlockHand(ev); // the wrist broken: the arm is under attack
        expect((await body()).body.bras_droit).toBe("attaque");
        await send(p, inst, { kind: "boss", phase: 1 });
        expect((await body()).body.bras_droit).toBe("attaque");
        await send(p, inst, { kind: "boss", phase: 2, tick: TICK + 1 });
        expect((await body()).body.bras_droit).toBe("affaibli");
        await send(p, inst, { kind: "objective", objective: "main_pyroclast", tick: TICK + 2 });
        const e = await body();
        expect(e.body.bras_droit).toBe("detruit");
        expect(e.effects[0].text).toContain("pluies de lave");
        const overview = await api("GET", `${V}/raids`, { token: p.token });
        expect(overview.body.data.world.find((x: { id: string }) => x.id === ev).body.bras_droit).toBe("detruit");
        // The titan's map (phase 4) agrees: Pyroclast fought and fallen; 2.69 (phase 4B): the lairs of
        // Voidworm, Graviton and Titanus open too, the others still sealed.
        const titan = (await api("GET", `${V}/raids/events/${ev}/titan`, { token: p.token })).body.data;
        const bd = titan.branches.find((b: { part: string }) => b.part === "bras_droit");
        expect(bd).toMatchObject({ sealed: false, gigaboss_down: true, state: "detruit" });
        expect(titan.totals.gigaboss_defeated).toBe(1);
        const sealed = titan.branches.filter((b: { sealed: boolean }) => b.sealed).map((b: { part: string }) => b.part);
        for (const part of ["bras_droit", "abdomen", "jambe_gauche", "jambe_droite"]) expect(sealed).not.toContain(part);
    });

    it("the results: clans, players in every category, my table waiting « en attente de validation »", async () => {
        const { ev, inst, p1, p2 } = await fallen("RES");
        let r = await api("GET", `${V}/raids/events/${ev}/results`, { token: p1.token });
        expect(r.status).toBe(200);
        expect(r.body.data.event.final).toBe(false);
        expect(r.body.data.gigaboss.defeated).toBe(true);
        expect(r.body.data.clans[0].tag).toBe("RES");
        expect(r.body.data.players.map((x: { id: string }) => x.id)).toEqual([p1.id, p2.id]);
        expect(r.body.data.players[0].me).toBe(true);
        expect(r.body.data.my.pending).toBe(false);
        // A newer table reported by one of two: waiting for its majority, never counted yet.
        await send(p2, inst, { kind: "result", tick: TICK + 600, records: [row(0, { ticks: 120600 }), row(1, { damage: 200, boss_damage: 100, vents: 0, weak_points: 0, ticks: 30600 })] });
        r = await api("GET", `${V}/raids/events/${ev}/results`, { token: p1.token });
        expect(r.body.data.my.pending).toBe(true);
        expect(r.body.data.players[1].seconds).toBe(Math.floor(30000 / 120));
        const stranger = await registerPlayer("Curieux");
        expect((await api("GET", `${V}/raids/events/${ev}/results`, { token: stranger.token })).status).toBe(200); // a world event: anyone may read it
    });

    it("rewards: granted once, claimed once (double claim, replayed request, reconnection), unlocked in the profile", async () => {
        const { ev, inst, p1, p2 } = await fallen("CLAIM");
        // Another acceptance of the same results grants nothing more.
        await send(p1, inst, { kind: "boss", phase: 4 });
        expect((await grants(ev)).length).toBe(5); // 2.69: with the world event's suit (raidWorld.ts)
        const before = await api("GET", `${V}/raids`, { token: p1.token });
        expect(before.body.data.rewards.map((g: { reward: string }) => g.reward).sort()).toEqual([
            "asterion_ecorce",
            "asterion_participation",
            "pyroclast_badge",
            "pyroclast_performance",
        ]);
        const c1 = await api("POST", `${V}/raids/rewards/claim`, { token: p1.token, body: {} });
        expect(c1.status).toBe(200);
        expect(c1.body.data.claimed).toBe(4);
        expect(c1.body.data.rewards).toEqual([]);
        // The same request again (a retry after a lost answer): nothing more.
        const c2 = await api("POST", `${V}/raids/rewards/claim`, { token: p1.token, body: {} });
        expect(c2.body.data.claimed).toBe(0);
        // Reconnected (a new session): still claimed once.
        const again = await api("POST", `${V}/auth/login`, { body: { username: p1.username, password: p1.password, device: deviceOf(p1.username) } });
        expect(again.status).toBe(200);
        const token = again.body.data.session.access_token as string;
        const c3 = await api("POST", `${V}/raids/rewards/claim`, { token, body: {} });
        expect(c3.body.data.claimed).toBe(0);
        const won = (await env.DB.prepare("SELECT reward_ref FROM player_won_rewards WHERE player_id = ?1 ORDER BY reward_ref").bind(p1.id).all<{ reward_ref: string }>()).results;
        expect(won.map((w) => w.reward_ref)).toEqual(["suit_ecorce_asterion", "title_assaillant_asterion", "title_briseur_magma"]);
        expect((await api("GET", `${V}/raids`, { token: p1.token })).body.data.rewards_claimed.sort()).toEqual([
            "asterion_ecorce",
            "asterion_participation",
            "pyroclast_badge",
            "pyroclast_performance",
        ]);
        // Claiming one reward by its id; someone else's id is not found.
        const mine = (await api("GET", `${V}/raids`, { token: p2.token })).body.data.rewards[0];
        expect((await api("POST", `${V}/raids/rewards/claim`, { token: p1.token, body: { id: mine.id } })).status).toBe(404);
        expect((await api("POST", `${V}/raids/rewards/claim`, { token: p2.token, body: { id: mine.id } })).body.data.claimed).toBe(1);
        // The title is worn online once won; the badge is an achievement only a claimer may declare.
        const put = await api("PUT", `${V}/players/me/achievements`, { token: p2.token, body: { unlocked: ["communaute_pyroclast"] } });
        expect(put.status).toBe(200);
        const ids = (await env.DB.prepare("SELECT ids FROM player_achievements WHERE player_id = ?1").bind(p2.id).first<{ ids: string }>())!.ids;
        expect(JSON.parse(ids)).toEqual(["communaute_pyroclast"]);
        const cheat = await registerPlayer("Tricheur");
        await api("PUT", `${V}/players/me/achievements`, { token: cheat.token, body: { unlocked: ["communaute_pyroclast"] } });
        const cheatIds = await env.DB.prepare("SELECT ids FROM player_achievements WHERE player_id = ?1").bind(cheat.id).first<{ ids: string }>();
        expect(JSON.parse(cheatIds?.ids ?? "[]")).toEqual([]);
    });

    it("the top of an ended event's ranking is granted once", async () => {
        const { ev, p1 } = await fallen("TOP");
        await env.DB.prepare("UPDATE raid_events SET ends_at = ?2 WHERE id = ?1").bind(ev, iso(-1)).run();
        const r1 = await api("GET", `${V}/raids/events/${ev}/results`, { token: p1.token });
        expect(r1.body.data.event.final).toBe(true);
        await api("GET", `${V}/raids/events/${ev}/results`, { token: p1.token });
        const top = (await grants(ev)).filter((g) => g.reward === "pyroclast_top");
        expect(top.length).toBe(2); // both were at the fall (3 places)
    });
});
