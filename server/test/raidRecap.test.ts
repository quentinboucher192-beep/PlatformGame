// Clan raids « Asterion », 2.78.1 (docs/RAIDS.md §24): the « fiche récapitulative » of a finished raid, in the
// clan. Only its members (the session's player) and the administrators read it; only finished raids (the heart
// fallen, or the event over) the clan took part in; every figure from what the server accepted (objectives,
// zones, the heart's shared health, the contributions tables, the wipes, the rewards), what still waits for
// its majority counted apart and never in a figure; another clan's players never in the clan's bests.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { CATALOGS, HEART } from "../src/raidMap";
import { HEART_OBJECTIVE, RELICS } from "../src/raidHeart";
import { RECAP_PARTS, RECAP_ZONES_TOTAL, finishedRaids, raidRecap } from "../src/raidRecap";
import { bodyOf, countingDb, directCtx, fullScans } from "./d1count";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const EMBLEM = { shape: "star", primary: "#FFD27A", secondary: "#a66bff" };
const iso = (minutes: number) => new Date(Date.now() + minutes * 60000).toISOString();

async function clan(tag: string, n: number) {
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
type Clan = Awaited<ReturnType<typeof clan>>;

/** An event of `clans` from `start` to `end` (minutes from now), every player registered. */
async function event(clans: Clan[], start: number, end: number, kind: "world" | "clan" = "world"): Promise<string> {
    const id = crypto.randomUUID();
    const now = new Date().toISOString();
    await env.DB.prepare(
        `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity, difficulty, rewards_text, published_at, created_at, updated_at)
         VALUES (?1, ?2, 'asterion', 'standard', ?3, ?4, 30, ?5, ?6, 1, 0, 50, 3, '', ?7, ?7, ?7)`,
    )
        .bind(id, kind, kind === "clan" ? clans[0]!.id : null, iso(start), iso(start - 30), iso(end), now)
        .run();
    let k = 0;
    for (const c of clans) {
        await env.DB.prepare("INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at) VALUES (?1, ?2, ?3, ?3, '', ?4, ?4)")
            .bind(id, c.id, c.players[0]!.id, now)
            .run();
        for (const p of c.players)
            await env.DB.prepare("INSERT INTO raid_registrations (event_id, player_id, clan_id, created_at) VALUES (?1, ?2, ?3, ?4)")
                .bind(id, p.id, c.id, new Date(Date.now() - 600000 + ++k * 10).toISOString())
                .run();
    }
    return id;
}

/** A group's instance on a branch, its players seated (`relieves`: a group of reinforcement). */
async function instance(ev: string, c: Clan, group: number, branch: string, players: TestPlayer[], relieves: string | null = null): Promise<string> {
    const id = crypto.randomUUID();
    const now = new Date().toISOString();
    await env.DB.prepare(
        `INSERT INTO raid_instances (id, event_id, clan_id, group_no, branch, zone, players, started_at, created_at, updated_at, relieves)
         VALUES (?1, ?2, ?3, ?4, ?5, 1, ?6, ?7, ?7, ?7, ?8)`,
    )
        .bind(id, ev, c.id, group, branch, players.length, now, relieves)
        .run();
    let seat = 0;
    for (const p of players)
        await env.DB.prepare("INSERT INTO raid_instance_members (instance_id, player_id, seat, active, joined_at, seat_from) VALUES (?1, ?2, ?3, 1, ?4, 0)")
            .bind(id, p.id, seat++, now)
            .run();
    return id;
}

async function accept(ev: string, branch: string, objective: string, instanceId: string | null, minutes: number) {
    await env.DB.prepare("INSERT OR IGNORE INTO raid_branch_objectives (event_id, branch, objective_id, instance_id, accepted_at) VALUES (?1, ?2, ?3, ?4, ?5)")
        .bind(ev, branch, objective, instanceId, iso(minutes))
        .run();
}

/** A validated table of contributions (tenths of health for the damage). */
async function contribution(ev: string, inst: string, seat: number, p: TestPlayer, f: { boss?: number; rescues?: number; blocks?: number; objectives?: number; ticks?: number }) {
    await env.DB.prepare(
        `INSERT INTO raid_contributions (instance_id, event_id, seat, from_tick, player_id, damage, boss_damage, blocks, rescues, objectives, ticks, tick, accepted_at)
         VALUES (?1, ?2, ?3, 0, ?4, ?5, ?5, ?6, ?7, ?8, ?9, 1000, ?10)`,
    )
        .bind(inst, ev, seat, p.id, f.boss ?? 0, f.blocks ?? 0, f.rescues ?? 0, f.objectives ?? 0, f.ticks ?? 36000, iso(-20))
        .run();
}

async function zoneTable(ev: string, inst: string, branch: string, zone: number, seat: number, p: TestPlayer, mechanisms: number) {
    await env.DB.prepare(
        `INSERT INTO raid_zone_contributions (instance_id, event_id, branch, zone, seat, from_tick, player_id, mechanisms, tick, accepted_at)
         VALUES (?1, ?2, ?3, ?4, ?5, 0, ?6, ?7, 1000, ?8)`,
    )
        .bind(inst, ev, branch, zone, seat, p.id, mechanisms, iso(-20))
        .run();
}

const gigaOf = (branch: string) => CATALOGS.find((c) => c.id === branch)!.objectives.find((o) => o.role === "gigaboss")!.id;

/** A raid of clan A (with clan B beside it) won: the seven parts, four relics, the heart; groups, a wipe retaken. */
async function wonRaid() {
    const a = await clan("NOVA", 4);
    const b = await clan("AUBE", 2);
    const ev = await event([a, b], -170, -5);
    const [p0, p1, p2, p3] = a.players as [TestPlayer, TestPlayer, TestPlayer, TestPlayer];
    const g1 = await instance(ev, a, 1, CATALOGS[0]!.id, [p0, p1]);
    const g2 = await instance(ev, a, 2, CATALOGS[1]!.id, [p2, p3]);
    const other = await instance(ev, b, 1, CATALOGS[2]!.id, b.players);
    // Group 2 wiped in zone 3 of its branch, a group of reinforcement took the zone again and downed the part.
    await env.DB.prepare("UPDATE raid_instances SET wiped_zone = 3, wiped_at = ?2 WHERE id = ?1").bind(g2, iso(-100)).run();
    const relief = await instance(ev, a, 3, CATALOGS[1]!.id, [p2, p3], g2);
    await env.DB.prepare("INSERT INTO raid_zone_completions (event_id, branch, zone, instance_id, completed_at) VALUES (?1, ?2, 3, ?3, ?4)")
        .bind(ev, CATALOGS[1]!.id, relief, iso(-90))
        .run();
    for (const z of [1, 2]) await env.DB.prepare("INSERT INTO raid_zone_completions (event_id, branch, zone, instance_id, completed_at) VALUES (?1, ?2, ?3, ?4, ?5)").bind(ev, CATALOGS[0]!.id, z, g1, iso(-120)).run();
    // The seven gigaboss, in order, by group 1, the reinforcement, the other clan, then group 1 again.
    const by = [g1, relief, other, g1, g1, g1, g1];
    for (let i = 0; i < CATALOGS.length; ++i) await accept(ev, CATALOGS[i]!.id, gigaOf(CATALOGS[i]!.id), by[i]!, -150 + i * 10);
    for (const r of RELICS.slice(0, 4)) await accept(ev, r.branch, r.objective, g1, -140);
    // The heart: our group's part and the other clan's (their majorities), then its fall.
    const h1 = await instance(ev, a, 1, HEART!.id, [p0, p1, p2]);
    const h2 = await instance(ev, b, 1, HEART!.id, b.players);
    await env.DB.prepare("INSERT INTO raid_heart_damage (event_id, instance_id, damage, tick, accepted_at) VALUES (?1, ?2, 6000, 1, ?3), (?1, ?4, 4000, 1, ?3)")
        .bind(ev, h1, iso(-30), h2)
        .run();
    await accept(ev, HEART!.id, HEART_OBJECTIVE, h1, -25);
    await env.DB.prepare("INSERT INTO raid_heart_events (event_id, fallen_at, fallen_instance) VALUES (?1, ?2, ?3)").bind(ev, iso(-25), h1).run();
    // The validated tables: p0 strikes the gigaboss most, p1 the heart, p2 revives, p3 the objectives.
    await contribution(ev, g1, 0, p0, { boss: 9000, objectives: 2 });
    await contribution(ev, g1, 1, p1, { boss: 1000, blocks: 3 });
    await contribution(ev, relief, 0, p2, { boss: 2000, rescues: 5, blocks: 1 });
    await contribution(ev, relief, 1, p3, { objectives: 7 });
    await contribution(ev, h1, 0, p0, { boss: 1000 });
    await contribution(ev, h1, 1, p1, { boss: 5000 });
    await contribution(ev, h1, 2, p2, { boss: 2000, rescues: 1 });
    await zoneTable(ev, g1, CATALOGS[0]!.id, 1, 0, p0, 3);
    await zoneTable(ev, g1, CATALOGS[0]!.id, 2, 0, p0, 4);
    await zoneTable(ev, relief, CATALOGS[1]!.id, 3, 1, p3, 9);
    // The other clan's player: more than anyone, never among our bests.
    await contribution(ev, other, 0, b.players[0]!, { boss: 90000, rescues: 50, objectives: 50 });
    // Rewards granted by the server: the heart's badge to three of ours, a gigaboss badge, the clan's title.
    for (const p of [p0, p1, p2])
        await env.DB.prepare("INSERT INTO raid_reward_grants (player_id, reward, event_id, reason, granted_at) VALUES (?1, 'coeur_badge', ?2, 'x', ?3)").bind(p.id, ev, iso(-20)).run();
    await env.DB.prepare("INSERT INTO raid_reward_grants (player_id, reward, event_id, reason, granted_at) VALUES (?1, 'skullmaw_badge', ?2, 'x', ?3)").bind(p0.id, ev, iso(-20)).run();
    await env.DB.prepare("INSERT INTO raid_reward_grants (player_id, reward, event_id, reason, granted_at) VALUES (?1, 'coeur_clan', ?2, 'x', ?3)").bind(p1.id, ev, iso(-20)).run();
    await env.DB.prepare("INSERT INTO raid_reward_grants (player_id, reward, event_id, reason, granted_at) VALUES (?1, 'coeur_badge', ?2, 'x', ?3)").bind(b.players[0]!.id, ev, iso(-20)).run();
    await env.DB.prepare("INSERT INTO raid_clan_titles (clan_id, event_id, title, rank, granted_at) VALUES (?1, ?2, 'Briseurs d''étoile', 1, ?3)").bind(a.id, ev, iso(-20)).run();
    return { a, b, ev, g1, g2, relief, other, h1 };
}

describe("raids 2.78.1: the « fiche récapitulative » of a finished raid", () => {
    it("the seven parts in the titan's order, 56 zones, seven relics", () => {
        expect(RECAP_PARTS.map((p) => p.part)).toEqual(["crane", "bras_gauche", "bras_droit", "torse", "abdomen", "jambe_gauche", "jambe_droite"]);
        expect(RECAP_ZONES_TOTAL).toBe(56);
        expect(RECAP_PARTS.filter((p) => p.relic).length).toBe(7);
        expect(RECAP_PARTS.every((p) => p.objective)).toBe(true);
    });

    it("only the clan's members and the administrators read it; a player id in the request changes nothing", async () => {
        const { a, b, ev } = await wonRaid();
        const stranger = await registerPlayer("Passant");
        expect((await api("GET", `${V}/clans/${a.id}/raids/${ev}/recap`)).status).toBe(401);
        const no = await api("GET", `${V}/clans/${a.id}/raids/${ev}/recap`, { token: stranger.token });
        expect(no.status).toBe(403);
        expect(no.body.error.code).toBe("NOT_CLAN_MEMBER");
        expect((await api("GET", `${V}/clans/${a.id}/raids`, { token: b.players[0]!.token })).status).toBe(403);
        expect((await api("GET", `${V}/clans/not-a-clan-id/raids`, { token: stranger.token })).status).toBe(404);
        // The other clan reads its own recap of the same event, never ours through a parameter.
        const theirs = await api("GET", `${V}/clans/${b.id}/raids/${ev}/recap?player_id=${a.players[0]!.id}`, { token: b.players[0]!.token });
        expect(theirs.status).toBe(200);
        expect(theirs.body.data.clan.tag).toBe("AUBE");
        expect(theirs.body.data.me.heart_rank).toBe(0);
        // An administrator sees any clan's recap (moderation).
        await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(stranger.id).run();
        const admin = await api("GET", `${V}/clans/${a.id}/raids/${ev}/recap`, { token: stranger.token });
        expect(admin.status).toBe(200);
        expect(admin.body.data.me.played).toBe(false);
        expect(admin.body.data.viewer.member).toBe(false);
    });

    it("a won raid: header, figures, the parts and the heart with the group that downed each, from accepted data only", async () => {
        const { a, ev } = await wonRaid();
        const me = a.players[1]!;
        const list = await api("GET", `${V}/clans/${a.id}/raids`, { token: me.token });
        expect(list.status).toBe(200);
        expect(list.body.data.raids.length).toBe(1);
        expect(list.body.data.raids[0]).toMatchObject({ id: ev, heart_fallen: true, parts_down: 7, parts_total: 7 });
        const r = await api("GET", `${V}/clans/${a.id}/raids/${ev}/recap`, { token: me.token });
        expect(r.status, JSON.stringify(r.body)).toBe(200);
        const d = r.body.data;
        expect(d.clan).toMatchObject({ id: a.id, tag: "NOVA" });
        expect(d.event.heart_fallen).toBe(true);
        // Started 170 min ago, the heart fell 25 min ago: 145 min.
        expect(Math.abs(d.event.duration_s - 145 * 60)).toBeLessThan(5);
        expect(d.event.end_at).toBe(d.event.fallen_at);
        expect(d.event.final).toBe(false); // ended 5 min ago: within the grace of the last reports
        expect(d.people).toEqual({ registered: 4, players: 4, groups: 2, reinforcements: 1 });
        expect(d.figures).toMatchObject({ parts_down: 7, parts_total: 7, zones_done: 3, zones_total: 56, zones_by_clan: 3, relics: 4, relics_total: 7, revives: 6, wiped: 1, retaken: 1 });
        expect(d.figures.deaths).toBeNull();
        expect(d.parts.map((p: { part: string }) => p.part)).toEqual(RECAP_PARTS.map((p) => p.part));
        expect(d.parts[0]).toMatchObject({ fallen: true, relic: true, by: { clan_tag: "NOVA", group: 1, reinforcement: false, mine: true, me: true } });
        expect(d.parts[1].by).toMatchObject({ clan_tag: "NOVA", group: 3, reinforcement: true, mine: true, me: false });
        expect(d.parts[2].by).toMatchObject({ clan_tag: "AUBE", mine: false });
        expect(d.parts[6].relic).toBe(false);
        expect(d.heart).toMatchObject({ fallen: true, open: true, phase: 4, groups: 2, clan_groups: 1, clan_share: 0.6 });
        // The bests of OUR clan only (the other clan's player did more and is never there).
        expect(d.best.gigaboss_damage.player.username).toBe("NOVAChef");
        expect(d.best.gigaboss_damage.share).toBeCloseTo(9000 / 12000, 5);
        expect(d.best.heart_damage.player).toMatchObject({ username: "NOVAMem1", me: true });
        expect(d.best.heart_damage.share).toBeCloseTo(5000 / 8000, 5);
        expect(d.best.revives).toMatchObject({ player: { username: "NOVAMem2" }, value: 6 });
        expect(d.best.support.player.username).toBe("NOVAMem2");
        expect(d.best.mechanisms).toMatchObject({ player: { username: "NOVAMem3" }, value: 9 });
        expect(d.best.objectives).toMatchObject({ player: { username: "NOVAMem3" }, value: 7 });
        expect(d.best.fewest_deaths).toBeNull();
        // The clan's rewards: who of the clan got each (the other clan's player not counted), the gigaboss badges last.
        expect(d.rewards.map((x: { reward: string }) => x.reward)).toEqual(["coeur_badge", "coeur_clan", "skullmaw_badge"]);
        expect(d.rewards[0]).toMatchObject({ players: 3, kind: "badge", ref: "communaute_coeur", mine: true, gigaboss: false });
        expect(d.rewards[2]).toMatchObject({ players: 1, gigaboss: true, mine: false });
        expect(d.titles).toEqual([{ title: "Briseurs d'étoile", rank: 1 }]);
        // Mine: my groups, the parts they downed, my share of the clan's damage to the heart and my rank.
        expect(d.me.played).toBe(true);
        expect(d.me.groups.map((g: { part: string }) => g.part)).toEqual(["crane", "coeur"]);
        expect(d.me.parts).toEqual(["crane", "torse", "abdomen", "jambe_gauche", "jambe_droite"]);
        expect(d.me.heart_share).toBeCloseTo(5000 / 8000, 5);
        expect(d.me.heart_rank).toBe(1);
        expect(d.me.rewards).toEqual(["coeur_badge", "coeur_clan"]);
    });

    it("a raid not won: how far it got, the heart sealed; a running raid whose heart stands is not finished", async () => {
        const a = await clan("SOLO", 2);
        const ev = await event([a], -200, -60, "clan");
        const g = await instance(ev, a, 1, CATALOGS[0]!.id, a.players);
        for (const c of CATALOGS.slice(0, 3)) await accept(ev, c.id, gigaOf(c.id), g, -100);
        await env.DB.prepare("INSERT INTO raid_gigaboss_fights (instance_id, event_id, branch, boss, phase, updated_at) VALUES (?1, ?2, ?3, 'Heartbreaker', 2, ?4)")
            .bind(g, ev, CATALOGS[3]!.id, iso(-70))
            .run();
        const running = await event([a], -30, 60, "clan");
        const list = await api("GET", `${V}/clans/${a.id}/raids`, { token: a.players[1]!.token });
        expect(list.body.data.raids.map((x: { id: string }) => x.id)).toEqual([ev]);
        expect(list.body.data.raids[0]).toMatchObject({ heart_fallen: false, parts_down: 3, final: true });
        expect((await api("GET", `${V}/clans/${a.id}/raids/${running}/recap`, { token: a.players[0]!.token })).status).toBe(404);
        const d = (await api("GET", `${V}/clans/${a.id}/raids/${ev}/recap`, { token: a.players[0]!.token })).body.data;
        expect(d.event).toMatchObject({ heart_fallen: false, final: true });
        expect(Math.abs(d.event.duration_s - 140 * 60)).toBeLessThan(3);
        expect(d.event.end_at).toBe(d.event.ends_at);
        expect(d.heart).toMatchObject({ fallen: false, open: false, phase: 0 });
        expect(d.parts[3]).toMatchObject({ fallen: false, phase: 2, by: null });
        expect(d.best.gigaboss_damage.player).toBeNull();
    });

    it("what waits for its majority is counted apart, never in a figure; a cancelled raid is never listed", async () => {
        const a = await clan("WAIT", 2);
        const ev = await event([a], -120, -2);
        const g = await instance(ev, a, 1, CATALOGS[0]!.id, a.players);
        // A table reported by one of two players (no majority): pending, nothing counted.
        await env.DB.prepare("INSERT INTO raid_reports (instance_id, report_key, player_id, value, tick, created_at) VALUES (?1, 'r:5000', ?2, '1:5000:x', 5000, ?3)")
            .bind(g, a.players[0]!.id, iso(-3))
            .run();
        await env.DB.prepare("INSERT INTO raid_reports (instance_id, report_key, player_id, value, tick, created_at) VALUES (?1, ?2, ?3, '8:5000:x', 5000, ?4)")
            .bind(g, `o:${gigaOf(CATALOGS[0]!.id)}`, a.players[0]!.id, iso(-3))
            .run();
        const cancelled = await event([a], -300, -200);
        await env.DB.prepare("UPDATE raid_events SET cancelled_at = ?2 WHERE id = ?1").bind(cancelled, iso(-400)).run();
        const list = await api("GET", `${V}/clans/${a.id}/raids`, { token: a.players[0]!.token });
        expect(list.body.data.raids.map((x: { id: string }) => x.id)).toEqual([ev]);
        const d = (await api("GET", `${V}/clans/${a.id}/raids/${ev}/recap`, { token: a.players[0]!.token })).body.data;
        expect(d.pending).toEqual({ tables: 1, objectives: 1 });
        expect(d.figures.parts_down).toBe(0);
        expect(d.parts[0].fallen).toBe(false);
        expect(d.people.players).toBe(0);
        expect(d.event.final).toBe(false);
    });

    it("a few round trips, every statement on an index (no raid table read whole)", async () => {
        const { a, ev } = await wonRaid();
        const token = a.players[0]!.token;
        const list = countingDb(env.DB);
        const l = await bodyOf(await finishedRaids(directCtx({ method: "GET", path: "/", params: { clanId: a.id }, token, db: list.db }).ctx));
        expect(l.raids.length).toBe(1);
        const one = countingDb(env.DB);
        const d = await bodyOf(await raidRecap(directCtx({ method: "GET", path: "/", params: { clanId: a.id, eventId: ev }, token, db: one.db }).ctx));
        expect(d.figures.parts_down).toBe(7);
        expect(list.stats.calls).toBeLessThanOrEqual(5);
        expect(one.stats.calls).toBeLessThanOrEqual(8);
        expect(await fullScans([...list.stats.queries, ...one.stats.queries], /^raid_/)).toEqual([]);
    });
});
