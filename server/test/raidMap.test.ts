// Clan raids « Asterion », phase 4 (raidMap.ts, docs/RAIDS.md): the seven branches of the titan,
// the links between them (an objective accepted in one branch acts on another, through the state
// the relays give their games), the consensus on a new branch, the global map that aggregates
// every instance, and the placement of the players (8 at most per instance, up to 50 per clan,
// late joiners where they are needed, the raid leader's moves and rebalancing).

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { CATALOGS, TITAN, catalogByPart, lockOpenIn, reachableIn, stateMaskOf, statesOf } from "../src/raidMap";
import { branchMaskOf } from "../src/raidZones";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

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
        const p = await registerPlayer(`${tag}M${i}`);
        expect((await api("POST", `${V}/clans/${id}/join`, { token: p.token, body: {} })).status).toBe(200);
        players.push(p);
    }
    return { id, tag, players };
}

/** A world event running now, `clans` registered with every member (the chief leads the raid). */
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
    }
    return id;
}

const join = (p: TestPlayer, ev: string, part?: string, body: Record<string, unknown> = {}) =>
    api("POST", part ? `${V}/raids/events/${ev}/branches/${part}/join` : `${V}/raids/events/${ev}/join`, { token: p.token, body });

/** The relay's view of an instance: started 10 min ago, `players` active in it. */
async function playing(instance: string, players: TestPlayer[]) {
    await env.DB.prepare("UPDATE raid_instances SET started_at = ?2, players = ?3 WHERE id = ?1").bind(instance, iso(-10), players.length).run();
    let seat = 0;
    for (const p of players)
        await env.DB.prepare("INSERT OR REPLACE INTO raid_instance_members (instance_id, player_id, seat, active, joined_at, left_at) VALUES (?1, ?2, ?3, 1, ?4, NULL)")
            .bind(instance, p.id, seat++, iso(-10))
            .run();
}

const report = (p: TestPlayer, instance: string, body: Record<string, unknown>) =>
    api("POST", `${V}/raids/instances/${instance}/reports`, { token: p.token, body: { kind: "objective", zone: 1, tick: 6000, hash: H1, ...body } });

async function placedIn(instance: string): Promise<number> {
    return (await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_placements WHERE instance_id = ?1").bind(instance).first<{ n: number }>())!.n;
}

describe("raids phase 4: the titan's rules", () => {
    it("seven branches of eight zones; every link acts on another branch with its own bit", () => {
        expect(CATALOGS.map((c) => c.branch)).toEqual(["crane", "bras_gauche", "bras_droit", "torse", "abdomen", "jambe_gauche", "jambe_droite"]);
        expect(CATALOGS.reduce((n, c) => n + c.zones.length, 0)).toBe(56);
        for (const c of CATALOGS) {
            expect(c.zones[7]!.boss).toBe(true);
            expect(c.objectives.length).toBeLessThan(24);
            const bits = TITAN.links.filter((l) => l.to === c.id).map((l) => l.bit);
            expect(new Set(bits).size).toBe(bits.length);
            for (const b of bits) expect(b).toBeGreaterThanOrEqual(24);
        }
        for (const l of TITAN.links) for (const f of l.from) expect(f.branch).not.toBe(l.to);
        // The Right arm's wrist broken opens the Skull's Temple onto the Vault (a passage locked "link:").
        const crane = catalogByPart("crane")!;
        const bd = catalogByPart("bras_droit")!;
        const done = new Map([[bd.id, new Set(["poignet_verrou"])]]);
        const st = statesOf(done);
        expect(lockOpenIn(crane, "link:bd_poignet", 0)).toBe(false);
        expect(lockOpenIn(crane, "link:bd_poignet", st.get(crane.id)!)).toBe(true);
        expect(reachableIn(crane, 0)[7]).toBe(false);
        expect(reachableIn(crane, st.get(crane.id)!)[7]).toBe(true);
        // A link of two objectives is on with both only.
        const eyes = new Map([[crane.id, new Set(["crane_oeil_gauche"])]]);
        expect(statesOf(eyes).get(catalogByPart("bras_gauche")!.id)).toBe(0);
        eyes.get(crane.id)!.add("crane_oeil_droit");
        expect(statesOf(eyes).get(catalogByPart("bras_gauche")!.id)).toBe(1 << 24);
    });
});

describe("raids phase 4: a new branch, its consensus and the links", () => {
    it("the Skull: majority accepted, the liar journaled; the eyes act on the Left arm's state", async () => {
        const a = await clan("AUBE", 3);
        const ev = await runningEvent(a);
        const r = await join(a.players[0]!, ev, "crane");
        expect(r.status, JSON.stringify(r.body)).toBe(200);
        expect(r.body.data.instance.level).toBe("raid:asterion_crane");
        expect(r.body.data.instance.hash).toBe(`${catalogByPart("crane")!.revision}.${TITAN.revision}`);
        const inst = r.body.data.instance.id as string;
        for (const p of a.players.slice(1)) expect((await join(p, ev, "crane")).body.data.instance.id).toBe(inst);
        await playing(inst, a.players);
        const [p1, p2, p3] = a.players as [TestPlayer, TestPlayer, TestPlayer];
        expect((await report(p1, inst, { objective: "crane_oeil_gauche", zone: 2 })).body.data.status).toBe("pending");
        expect((await report(p2, inst, { objective: "crane_oeil_gauche", zone: 2, hash: H2 })).body.data.status).toBe("pending");
        expect((await report(p3, inst, { objective: "crane_oeil_gauche", zone: 2 })).body.data.status).toBe("accepted");
        const rej = await env.DB.prepare("SELECT player_id FROM raid_report_rejections WHERE instance_id = ?1").bind(inst).all<{ player_id: string }>();
        expect(rej.results.map((x) => x.player_id)).toEqual([p2.id]);
        // An objective of another branch's catalog, or of a zone still locked, is refused.
        expect((await report(p1, inst, { objective: "epaule_arene", zone: 1 })).body.data.reason).toBe("objectif inconnu");
        expect((await report(p1, inst, { objective: "crane_voute", zone: 7 })).body.data.reason).toBe("zone verrouillée pour la branche");
        const gauche = catalogByPart("bras_gauche")!.id;
        expect(await stateMaskOf(env.DB, ev, gauche)).toBe(0);
        for (const p of a.players) await report(p, inst, { objective: "crane_oeil_droit", zone: 3 });
        // Both eyes: the Left arm's turrets go dark — its relays give bit 24 to their games.
        expect(await branchMaskOf(env.DB, ev, gauche)).toBe(1 << 24);
        // The vault of the Skull opens with its two eyes (its own generators).
        expect((await report(p1, inst, { objective: "crane_voute", zone: 7, tick: 9000 })).body.data.status).toBe("pending");
    });

    it("a passage opened from another branch: the Right arm's wrist lets the Skull's groups into the Vault", async () => {
        const a = await clan("AUBE", 1);
        const b = await clan("NEBU", 1);
        const ev = await runningEvent(a, b);
        const ia = (await join(a.players[0]!, ev, "crane")).body.data.instance.id as string;
        const ib = (await join(b.players[0]!, ev, "bras_droit")).body.data.instance.id as string;
        await playing(ia, a.players);
        await playing(ib, b.players);
        expect((await report(a.players[0]!, ia, { objective: "crane_voute", zone: 7 })).body.data.reason).toBe("zone verrouillée pour la branche");
        for (const o of [["veine_generateur", 3], ["avantbras_generateur", 6], ["poignet_verrou", 7]] as const)
            expect((await report(b.players[0]!, ib, { objective: o[0], zone: o[1], tick: 7000 })).body.data.status).toBe("accepted");
        const crane = catalogByPart("crane")!;
        expect((await stateMaskOf(env.DB, ev, crane.id)) & (1 << 24)).toBe(1 << 24);
        expect((await report(a.players[0]!, ia, { objective: "crane_voute", zone: 7, tick: 7100 })).body.data.status).toBe("accepted");
        // The maps show it: the Skull's link on, its Vault reachable; the titan's feed and totals.
        const map = await api("GET", `${V}/raids/events/${ev}/branches/crane`, { token: a.players[0]!.token });
        expect(map.status).toBe(200);
        expect(map.body.data.zones.find((z: { id: number }) => z.id === 7).reachable).toBe(true);
        expect(map.body.data.effects_in.find((l: { id: string }) => l.id === "bd_poignet").on).toBe(true);
        expect(map.body.data.edges.find((x: { lock: string }) => x.lock === "link:bd_poignet").open).toBe(true);
        const titan = await api("GET", `${V}/raids/events/${ev}/titan`, { token: b.players[0]!.token });
        expect(titan.status).toBe(200);
        const d = titan.body.data;
        expect(d.branches.length).toBe(7);
        expect(d.totals.zones).toBe(56);
        expect(d.totals.objectives_done).toBe(4);
        expect(d.totals.groups).toBe(2);
        expect(d.body.crane).toBe("attaque");
        expect(d.body.torse).toBe("inconnu");
        expect(d.links.find((l: { id: string }) => l.id === "bd_poignet").on).toBe(true);
        expect(d.feed.some((f: { kind: string }) => f.kind === "link")).toBe(true);
        expect(d.clans[0].tag).toBe("NEBU");
        expect(d.my.placement.branch).toBe("bras_droit");
    });
});

describe("raids phase 4: placement of the players", () => {
    it("50 players of a clan on one branch: never more than 8 per instance, new instances as they fill", { timeout: 60000 }, async () => {
        const a = await clan("CINQ", 50);
        const ev = await runningEvent(a);
        // All at once (concurrent placements must never overfill an instance).
        const answers = await Promise.all(a.players.map((p) => join(p, ev, "jambe_gauche")));
        for (const r of answers) expect(r.status, JSON.stringify(r.body)).toBe(200);
        const rows = await env.DB.prepare(
            "SELECT i.id, i.group_no, (SELECT COUNT(*) FROM raid_placements p WHERE p.instance_id = i.id) AS n FROM raid_instances i WHERE i.event_id = ?1",
        )
            .bind(ev)
            .all<{ id: string; group_no: number; n: number }>();
        expect(rows.results.reduce((s, r) => s + r.n, 0)).toBe(50);
        for (const r of rows.results) expect(r.n).toBeLessThanOrEqual(8);
        expect(rows.results.filter((r) => r.n > 0).length).toBeGreaterThanOrEqual(7);
        // Joining again keeps one's place.
        const again = await join(a.players[3]!, ev, "jambe_gauche");
        expect(again.body.data.instance.id).toBe(answers[3]!.body.data.instance.id);
    });

    it("a late joiner goes where the clan fights and needs players; the raid leader moves and rebalances", async () => {
        const a = await clan("AUBE", 12);
        const ev = await runningEvent(a);
        const chief = a.players[0]!;
        // Five on the Torso, fighting; two on the Abdomen, not yet in play.
        const torso = (await join(a.players[1]!, ev, "torse")).body.data.instance.id as string;
        for (const p of a.players.slice(2, 6)) await join(p, ev, "torse");
        await playing(torso, a.players.slice(1, 6));
        for (const p of a.players.slice(6, 8)) await join(p, ev, "abdomen");
        // A late joiner without a choice: the group in combat that has room.
        const late = await join(a.players[8]!, ev);
        expect(late.status).toBe(200);
        expect(late.body.data.instance.id).toBe(torso);
        expect(late.body.data.instance.part).toBe("torse");
        // A member cannot move anyone; the raid leader (the chief here) can.
        const p9 = a.players[9]!;
        await join(p9, ev, "crane");
        const refused = await api("POST", `${V}/raids/events/${ev}/placements`, { token: a.players[2]!.token, body: { player_id: p9.id, branch: "torse" } });
        expect(refused.status).toBe(403);
        const moved = await api("POST", `${V}/raids/events/${ev}/placements`, { token: chief.token, body: { player_id: p9.id, branch: "torse" } });
        expect(moved.status, JSON.stringify(moved.body)).toBe(200);
        expect(moved.body.data.placement.instance_id).toBe(torso);
        expect(await placedIn(torso)).toBe(7);
        // Two more on the Torso: the first one fills it (8), the next opens a second group.
        await api("POST", `${V}/raids/events/${ev}/placements`, { token: chief.token, body: { player_id: a.players[10]!.id, branch: "torse" } });
        const second = await api("POST", `${V}/raids/events/${ev}/placements`, { token: chief.token, body: { player_id: a.players[11]!.id, branch: "torse" } });
        expect(second.body.data.placement.instance_id).not.toBe(torso);
        expect(await placedIn(torso)).toBe(8);
        // Into a full instance: refused.
        const full = await api("POST", `${V}/raids/events/${ev}/placements`, { token: chief.token, body: { player_id: chief.id, branch: "torse", instance_id: torso } });
        expect(full.status).toBe(409);
        // Rebalanced: 9 players over 2 groups → 5 and 4 (the players in combat stay where they are).
        const rb = await api("POST", `${V}/raids/events/${ev}/placements/rebalance`, { token: chief.token, body: { branch: "torse" } });
        expect(rb.status, JSON.stringify(rb.body)).toBe(200);
        const sizes = (rb.body.data.instances as { placed: number }[]).map((i) => i.placed).sort();
        expect(sizes).toEqual([4, 5]);
        const keptInCombat = await env.DB.prepare(
            "SELECT COUNT(*) AS n FROM raid_placements WHERE instance_id = ?1 AND player_id IN (SELECT player_id FROM raid_instance_members WHERE instance_id = ?1)",
        )
            .bind(torso)
            .first<{ n: number }>();
        expect(keptInCombat!.n).toBe(5);
        // The clan's view: every member and their place; the moves are journaled.
        const view = await api("GET", `${V}/raids/events/${ev}/placements`, { token: a.players[4]!.token });
        expect(view.status).toBe(200);
        expect(view.body.data.can_manage).toBe(false);
        expect(view.body.data.members.length).toBe(12);
        expect(view.body.data.members.find((m: { id: string }) => m.id === p9.id).branch).toBe("torse");
        const log = await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_placement_log WHERE event_id = ?1 AND reason IN ('chef', 'equilibrage')").bind(ev).first<{ n: number }>();
        expect(log!.n).toBeGreaterThanOrEqual(4);
    });

    it("helping another clan's group: only while it has room", async () => {
        const a = await clan("AUBE", 8);
        const b = await clan("NEBU", 1);
        const ev = await runningEvent(a, b);
        const ia = (await join(a.players[0]!, ev, "abdomen")).body.data.instance.id as string;
        for (const p of a.players.slice(1, 7)) await join(p, ev, "abdomen");
        await playing(ia, a.players.slice(0, 7));
        const help = await join(b.players[0]!, ev, "abdomen", { instance_id: ia });
        expect(help.status).toBe(200);
        expect(help.body.data.instance.clan_tag).toBe("AUBE");
        expect(await placedIn(ia)).toBe(8);
        // The eighth of AUBE is placed in a new group of AUBE.
        const last = await join(a.players[7]!, ev, "abdomen");
        expect(last.body.data.instance.id).not.toBe(ia);
        expect(last.body.data.instance.group).toBe(2);
    });
});
