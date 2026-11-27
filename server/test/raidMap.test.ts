// Clan raids « Asterion », phase 4 (raidMap.ts, docs/RAIDS.md): the seven branches of the titan,
// the links between them (an objective accepted in one branch acts on another, through the state
// the relays give their games), the consensus on a new branch, the global map that aggregates
// every instance, and the placement of the players (8 at most per instance, up to 50 per clan,
// late joiners where they are needed, the raid leader's moves and rebalancing).

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { CATALOGS, HEART, PARTS, TITAN, catalogByPart, lockOpenIn, reachableIn, stateMaskOf, statesOf } from "../src/raidMap";
import { HEART_OBJECTIVE } from "../src/raidHeart";
import { timesOf } from "../src/raids";
import { branchMaskOf } from "../src/raidZones";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";
import { fillRoster, raidGroups } from "./raidGroups";

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
        // Titan 9: the Jaw's guard (an ambush) opens the way on from the entry.
        const jaw = 1 << crane.objectives.find((o) => o.id === "crane_machoire")!.index;
        expect(crane.objectives.find((o) => o.id === "crane_machoire")!.role).toBe("ambush");
        expect(reachableIn(crane, st.get(crane.id)!)[7]).toBe(false);
        expect(reachableIn(crane, (st.get(crane.id)! | jaw) >>> 0)[7]).toBe(true);
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
        await raidGroups(ev, a.id, { crane: a.players }); // §41: the clan's seven groups, these three in the Crâne's
        const r = await join(a.players[0]!, ev, "crane");
        expect(r.status, JSON.stringify(r.body)).toBe(200);
        expect(r.body.data.instance.level).toBe("raid:asterion_crane");
        // Titan 13: the branch's version, then the event's difficulty (3 / 5 here): every game of the instance plays it.
        expect(r.body.data.instance.hash).toBe(`${catalogByPart("crane")!.revision}.${TITAN.revision}.d3`);
        expect(r.body.data.instance.difficulty).toBe(3);
        const inst = r.body.data.instance.id as string;
        for (const p of a.players.slice(1)) expect((await join(p, ev, "crane")).body.data.instance.id).toBe(inst);
        await playing(inst, a.players);
        const [p1, p2, p3] = a.players as [TestPlayer, TestPlayer, TestPlayer];
        // Titan 9: the Jaw's guard first (an ambush: the ways on wait for it).
        for (const p of a.players) await report(p, inst, { objective: "crane_machoire", zone: 1, tick: 3000 });
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
        await raidGroups(ev, a.id, { crane: a.players });
        await raidGroups(ev, b.id, { bras_droit: b.players });
        const ia = (await join(a.players[0]!, ev, "crane")).body.data.instance.id as string;
        const ib = (await join(b.players[0]!, ev, "bras_droit")).body.data.instance.id as string;
        await playing(ia, a.players);
        await playing(ib, b.players);
        expect((await report(a.players[0]!, ia, { objective: "crane_voute", zone: 7 })).body.data.reason).toBe("zone verrouillée pour la branche");
        // Titan 9: each branch's entry guard first (an ambush: the ways on wait for it).
        expect((await report(a.players[0]!, ia, { objective: "crane_machoire", zone: 1, tick: 3000 })).body.data.status).toBe("accepted");
        for (const o of [["epaule_arene", 1], ["veine_generateur", 3], ["avantbras_generateur", 6], ["poignet_verrou", 7]] as const)
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
        expect(d.totals.objectives_done).toBe(6); // titan 9: with the two entry guards (the Jaw, the Épaule)
        expect(d.totals.groups).toBe(2);
        expect(d.body.crane).toBe("attaque");
        expect(d.body.torse).toBe("inconnu");
        expect(d.links.find((l: { id: string }) => l.id === "bd_poignet").on).toBe(true);
        expect(d.feed.some((f: { kind: string }) => f.kind === "link")).toBe(true);
        expect(d.clans[0].tag).toBe("NEBU");
        expect(d.my.placement.branch).toBe("bras_droit");
    });
});

describe("§41 (titan 16): seven groups, one per branch", () => {
    const groupsOfClan = async (ev: string, clanId: string) =>
        (
            await env.DB.prepare(
                `SELECT i.id, i.group_no, i.branch, (SELECT COUNT(*) FROM raid_placements p WHERE p.instance_id = i.id) AS n FROM raid_instances i
                 WHERE i.event_id = ?1 AND i.clan_id = ?2 ORDER BY i.group_no`,
            )
                .bind(ev, clanId)
                .all<{ id: string; group_no: number; branch: string; n: number }>()
        ).results;

    it("14 registered at least to start; then always 7 groups, group k in branch k, as even as possible", { timeout: 60000 }, async () => {
        const a = await clan("SEPT", 13);
        const ev = await runningEvent(a);
        const few = await join(a.players[0]!, ev);
        expect(few.status).toBe(409);
        expect(few.body.error.code).toBe("RAID_TOO_FEW");
        expect(few.body.error.message).toContain("au moins 14");
        expect((await groupsOfClan(ev, a.id)).length).toBe(0); // nothing formed
        // The 14th (and six more: 20 registered) — the groups form at the first join: 3,3,3,3,3,3,2.
        await fillRoster(ev, a.id, 7);
        const r = await join(a.players[0]!, ev);
        expect(r.status, JSON.stringify(r.body)).toBe(200);
        expect(r.body.data.instance.group).toBe(1); // the raid leader (the chief) leads group 1: the Crâne
        expect(r.body.data.instance.part).toBe("crane");
        const groups = await groupsOfClan(ev, a.id);
        expect(groups.map((g) => g.group_no)).toEqual([1, 2, 3, 4, 5, 6, 7]);
        expect(groups.map((g) => g.branch)).toEqual(CATALOGS.map((c) => c.id));
        expect(groups.map((g) => g.n)).toEqual([3, 3, 3, 3, 3, 3, 2]);
        // Every player of the clan joins their own group (never a new instance).
        for (const p of a.players) expect((await join(p, ev)).status).toBe(200);
        expect((await groupsOfClan(ev, a.id)).length).toBe(7);
        // The roster screen: seven groups of 2 to 8.
        const view = await api("GET", `${V}/raids/events/${ev}/placements`, { token: a.players[5]!.token });
        expect(view.status).toBe(200);
        expect(view.body.data.rules).toMatchObject({ groups: 7, min_group: 2, max_group: 8, min_players: 14, max_in_play: 56 });
        expect(view.body.data.groups.map((g: { branch: string }) => g.branch)).toEqual(PARTS);
        expect(view.body.data.too_few).toBe(false);
    });

    it("56 in play at most, the others on a waiting list in their order; its first takes the place of one who unregisters", { timeout: 60000 }, async () => {
        const a = await clan("FILE", 3);
        // Registration open: the chief registers, then 54 placeholders, then two members (the 56th, the 57th). A clan holds
        // 50 members and an event 50 registrations per clan at most (clans.ts, raid_events CHECK): past them, straight in
        // the database, as a larger clan would register.
        const id = crypto.randomUUID();
        const now = new Date().toISOString();
        await env.DB.prepare(
            `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity, difficulty, rewards_text, published_at, created_at, updated_at)
             VALUES (?1, 'world', 'asterion', 'standard', NULL, ?2, 30, ?3, ?4, 1, 0, 50, 3, '', ?5, ?5, ?5)`,
        )
            .bind(id, iso(120), iso(90), iso(180), now)
            .run();
        await env.DB.prepare("INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at) VALUES (?1, ?2, ?3, ?3, '', ?4, ?4)")
            .bind(id, a.id, a.players[0]!.id, now)
            .run();
        expect((await api("POST", `${V}/raids/events/${id}/register`, { token: a.players[0]!.token, body: {} })).status).toBe(201);
        await fillRoster(id, a.id, 54);
        const late = async (p: TestPlayer, ms: number) =>
            env.DB.prepare("INSERT INTO raid_registrations (event_id, player_id, clan_id, created_at) VALUES (?1, ?2, ?3, ?4)")
                .bind(id, p.id, a.id, new Date(Date.now() + ms).toISOString())
                .run();
        const [, p56, p57] = a.players as [TestPlayer, TestPlayer, TestPlayer];
        await late(p56, 5000);
        await late(p57, 6000);
        let sheet = await api("GET", `${V}/raids/events/${id}`, { token: p57.token });
        expect(sheet.status).toBe(200);
        expect(sheet.body.data.event.my_clan).toMatchObject({ players: 57, in_play: 56, waiting: 1, groups: 7, min_players: 14, max_in_play: 56, ready: true });
        expect(sheet.body.data.waitlist.map((w: { id: string; rank: number }) => [w.id, w.rank])).toEqual([[p57.id, 1]]);
        expect(sheet.body.data.roster.find((r: { id: string }) => r.id === p57.id).waiting_rank).toBe(1);
        expect(sheet.body.data.roster.find((r: { id: string }) => r.id === p56.id).waiting_rank).toBeUndefined();
        expect(sheet.body.data.groups.map((g: { members: unknown[] }) => g.members.length)).toEqual([8, 8, 8, 8, 8, 8, 8]);
        expect(sheet.body.data.groups.map((g: { branch: string }) => g.branch)).toEqual(PARTS);
        expect(sheet.body.data.rules).toMatchObject({ groups: 7, min_group: 2, max_group: 8, min_players: 14, max_in_play: 56, minutes: 60 });
        // Someone in play unregisters before the start: the first waiting takes the place.
        expect((await api("DELETE", `${V}/raids/events/${id}/register`, { token: a.players[0]!.token })).status).toBe(200);
        sheet = await api("GET", `${V}/raids/events/${id}`, { token: p57.token });
        expect(sheet.body.data.waitlist).toEqual([]);
        expect(sheet.body.data.groups.flatMap((g: { members: { id: string }[] }) => g.members.map((m) => m.id))).toContain(p57.id);
        // Back on, after everyone: the first of the waiting list now.
        await late(a.players[0]!, 7000);
        sheet = await api("GET", `${V}/raids/events/${id}`, { token: p57.token });
        expect(sheet.body.data.waitlist.map((w: { id: string }) => w.id)).toEqual([a.players[0]!.id]);
        // The raid starts: the waiting player never plays; the others are 7 groups of 8.
        await env.DB.prepare("UPDATE raid_events SET starts_at = ?2, lock_at = ?3, ends_at = ?4 WHERE id = ?1").bind(id, iso(-1), iso(-31), iso(59)).run();
        const waiting = await join(a.players[0]!, id);
        expect(waiting.status).toBe(403);
        expect(waiting.body.error.code).toBe("RAID_WAITLIST");
        expect((await join(p57, id)).status).toBe(200);
        const groups = await groupsOfClan(id, a.id);
        expect(groups.map((g) => g.n)).toEqual([8, 8, 8, 8, 8, 8, 8]);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_placements WHERE event_id = ?1 AND player_id = ?2").bind(id, a.players[0]!.id).first<{ n: number }>())!.n).toBe(0);
    });

    it("a player only plays their group's branch and instance: another branch, another group, help: refused", { timeout: 60000 }, async () => {
        const a = await clan("SOLO", 14);
        const b = await clan("AUTR", 14);
        const ev = await runningEvent(a, b);
        const mine = await join(a.players[0]!, ev, "crane");
        expect(mine.status, JSON.stringify(mine.body)).toBe(200);
        const other = await join(a.players[0]!, ev, "torse");
        expect(other.status).toBe(409);
        expect(other.body.error.code).toBe("RAID_OTHER_BRANCH");
        expect(other.body.error.message).toContain("CRÂNE");
        // Another group of my clan, another clan's group: never (no help, no other instance).
        const torse = (await join(a.players[6]!, ev, "torse")).body.data.instance.id as string; // the 7th registered: group 4
        expect((await join(a.players[0]!, ev, undefined, { instance_id: torse })).body.error.code).toBe("RAID_OTHER_GROUP");
        const theirs = (await join(b.players[0]!, ev)).body.data.instance.id as string;
        expect((await join(a.players[0]!, ev, "crane", { instance_id: theirs })).status).toBe(409);
        // The phase-2 route too.
        expect((await api("POST", `${V}/raids/events/${ev}/instance`, { token: a.players[0]!.token, body: { instance_id: torse } })).body.error.code).toBe("RAID_OTHER_GROUP");
        // Seven instances per clan, never one more.
        const n = await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_instances WHERE event_id = ?1 AND clan_id = ?2").bind(ev, a.id).first<{ n: number }>();
        expect(n!.n).toBe(7);
    });

    it("the raid leader moves a member between groups (2 to 8 each, checked by the server) and evens them out", { timeout: 60000 }, async () => {
        const a = await clan("CHEF", 14);
        const ev = await runningEvent(a);
        const chief = a.players[0]!;
        expect((await join(chief, ev)).status).toBe(200); // 7 groups of 2
        const p2 = a.players[2]!; // group 2 (the Bras gauche)
        // A member cannot move anyone.
        expect((await api("POST", `${V}/raids/events/${ev}/placements`, { token: a.players[3]!.token, body: { player_id: p2.id, branch: "torse" } })).status).toBe(403);
        // Group 2 has 2 players: one leaving would leave 1 — refused.
        const small = await api("POST", `${V}/raids/events/${ev}/placements`, { token: chief.token, body: { player_id: p2.id, branch: "torse" } });
        expect(small.status).toBe(409);
        expect(small.body.error.code).toBe("GROUP_TOO_SMALL");
        // Filled up to 8 in the Torse's group (direct placements), a ninth is refused.
        await fillRoster(ev, a.id, 20);
        const torse = (await env.DB.prepare("SELECT id FROM raid_instances WHERE event_id = ?1 AND clan_id = ?2 AND group_no = 4").bind(ev, a.id).first<{ id: string }>())!.id;
        const fillers = (await env.DB.prepare("SELECT player_id FROM raid_registrations WHERE event_id = ?1 AND clan_id = ?2 ORDER BY created_at DESC LIMIT 20").bind(ev, a.id).all<{ player_id: string }>()).results;
        const crane = (await env.DB.prepare("SELECT id FROM raid_instances WHERE event_id = ?1 AND clan_id = ?2 AND group_no = 1").bind(ev, a.id).first<{ id: string }>())!.id;
        let k = 0;
        for (const f of fillers) {
            const to = k < 6 ? torse : crane;
            await env.DB.prepare(
                `INSERT INTO raid_placements (event_id, player_id, clan_id, branch, instance_id, placed_by, reason, created_at, updated_at)
                 SELECT ?1, ?2, ?3, branch, id, NULL, 'choix', ?5, ?5 FROM raid_instances WHERE id = ?4`,
            )
                .bind(ev, f.player_id, a.id, to, new Date().toISOString())
                .run();
            ++k;
        }
        expect(await placedIn(torse)).toBe(8);
        const full = await api("POST", `${V}/raids/events/${ev}/placements`, { token: chief.token, body: { player_id: a.players[13]!.id, branch: "torse" } });
        expect(full.status).toBe(409);
        expect(full.body.error.code).toBe("INSTANCE_FULL");
        // A move that keeps both groups in 2..8: the Crâne (16) gives one to the Abdomen (2).
        const moved = await api("POST", `${V}/raids/events/${ev}/placements`, { token: chief.token, body: { player_id: a.players[1]!.id, branch: "abdomen" } });
        expect(moved.status, JSON.stringify(moved.body)).toBe(200);
        expect(moved.body.data.placement.group).toBe(5);
        // Even them out: 34 players over 7 groups → 5,5,5,5,5,5,4 (sizes differ by one at most).
        const rb = await api("POST", `${V}/raids/events/${ev}/placements/rebalance`, { token: chief.token, body: {} });
        expect(rb.status, JSON.stringify(rb.body)).toBe(200);
        const sizes = (rb.body.data.instances as { placed: number }[]).map((i) => i.placed).sort();
        expect(sizes).toEqual([4, 5, 5, 5, 5, 5, 5]);
        expect(rb.body.data.instances.length).toBe(7);
        const log = await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_placement_log WHERE event_id = ?1 AND reason IN ('chef', 'equilibrage')").bind(ev).first<{ n: number }>();
        expect(log!.n).toBeGreaterThanOrEqual(2);
    });

    it("defeat: « temps écoulé » at the end of the 60 min with the Cœur standing, « tous les groupes éliminés » at once; victory once it fell", { timeout: 60000 }, async () => {
        // The standard raid lasts 60 minutes.
        const t = timesOf(Date.parse("2026-12-01T20:00:00Z"), 30, "standard");
        expect(Date.parse(t.ends_at) - Date.parse(t.starts_at)).toBe(60 * 60000);
        const a = await clan("PERD", 14);
        const ev = await runningEvent(a);
        expect((await join(a.players[0]!, ev)).status).toBe(200);
        const outcome = async () => (await api("GET", `${V}/raids/events/${ev}/titan`, { token: a.players[0]!.token })).body.data.outcome;
        expect((await outcome()).id).toBe("en_cours");
        // Every group wiped: defeated at once (nobody left to replace them).
        const groups = await groupsOfClan(ev, a.id);
        for (const g of groups.slice(0, 6)) await env.DB.prepare("UPDATE raid_instances SET wiped_zone = 1, wiped_at = ?2 WHERE id = ?1").bind(g.id, iso(-1)).run();
        expect((await outcome()).id).toBe("en_cours"); // one group still plays
        await env.DB.prepare("UPDATE raid_instances SET wiped_zone = 2, wiped_at = ?2 WHERE id = ?1").bind(groups[6]!.id, iso(-1)).run();
        expect(await outcome()).toEqual({ id: "defaite_groupes", text: "Défaite : tous les groupes éliminés" });
        const res = await api("GET", `${V}/raids/events/${ev}/results`, { token: a.players[0]!.token });
        expect(res.body.data.outcome.id).toBe("defaite_groupes");
        // Another clan, time over (the 60 min passed) with Gigaboss standing: « temps écoulé ».
        const b = await clan("TARD", 14);
        const ev2 = await runningEvent(b);
        expect((await join(b.players[0]!, ev2)).status).toBe(200);
        await env.DB.prepare("UPDATE raid_events SET starts_at = ?2, ends_at = ?3 WHERE id = ?1").bind(ev2, iso(-61), iso(-1)).run();
        const late = (await api("GET", `${V}/raids/events/${ev2}/titan`, { token: b.players[0]!.token })).body.data.outcome;
        expect(late).toEqual({ id: "defaite_temps", text: "Défaite : temps écoulé" });
        expect((await api("GET", `${V}/raids/events/${ev2}/results`, { token: b.players[0]!.token })).body.data.outcome.id).toBe("defaite_temps");
        // The Cœur fell (validated): victory, even at the end.
        await env.DB.prepare("INSERT INTO raid_branch_objectives (event_id, branch, objective_id, instance_id, accepted_at) VALUES (?1, ?2, ?3, NULL, ?4)")
            .bind(ev2, HEART!.id, HEART_OBJECTIVE, iso(-2))
            .run();
        expect((await api("GET", `${V}/raids/events/${ev2}/titan`, { token: b.players[0]!.token })).body.data.outcome.id).toBe("victoire");
    });
});
