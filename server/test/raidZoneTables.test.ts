// 2.75 (docs/RAIDS.md §19): the zones finished (written once, told in the raid's feed and on the
// branch's map), the « Tableau des contributions » of a zone (report « zone_table », the same
// consensus as the other tables, kept in the results), and what the administrators see for moderation
// (the real presence of a player who hides it, marked; never to anyone else).

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { zoneFinishedIn, catalogById } from "../src/raidMap";
import { BRANCH, implausibleZone, maskOf, zoneCanonical, type ZoneContribution } from "../src/raidZones";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const EMBLEM = { shape: "star", primary: "#FFD27A", secondary: "#a66bff" };
const iso = (minutes: number) => new Date(Date.now() + minutes * 60000).toISOString();
const H1 = "0123456789abcdef";

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
    api("POST", `${V}/raids/instances/${instance}/reports`, { token: p.token, body: { kind: "objective", zone: 1, tick: 6000, hash: H1, ...body } });

const completions = async (event: string) =>
    (await env.DB.prepare("SELECT branch, zone, instance_id FROM raid_zone_completions WHERE event_id = ?1 ORDER BY zone").bind(event).all<{ branch: string; zone: number; instance_id: string }>())
        .results;

describe("2.75: a zone finished", () => {
    it("is finished when every required objective is done (the secret zone: its optional ones)", () => {
        const c = catalogById(BRANCH.id)!;
        expect(zoneFinishedIn(c, 4, maskOf(["forge_soufflet_ouest"]))).toBe(false);
        expect(zoneFinishedIn(c, 4, maskOf(["forge_soufflet_ouest", "forge_soufflet_est"]))).toBe(true); // the anvil is optional
        expect(zoneFinishedIn(c, 5, 0)).toBe(false);
        expect(zoneFinishedIn(c, 5, maskOf(["grotte_relique"]))).toBe(true);
        expect(zoneFinishedIn(c, 6, maskOf(["avantbras_raccourci"]))).toBe(false); // the lever is optional, the generator is not
    });

    it("is written once, told in the titan's feed and on the branch's map, whoever accepts it", async () => {
        const a = await clan("AUBE", 2);
        const b = await clan("NEBU", 1);
        const ev = await runningEvent(a, b);
        const ia = await instanceOf(ev, a.players);
        for (const p of a.players) expect((await report(p, ia, { objective: "biceps_gardien", zone: 2 })).status).toBe(200);
        expect((await report(a.players[1]!, ia, { objective: "biceps_gardien", zone: 2 })).body.data.status).toBe("accepted");
        expect(await completions(ev)).toEqual([{ branch: BRANCH.id, zone: 2, instance_id: ia }]);
        // Another group accepts the same objective: still told once.
        const ib = await instanceOf(ev, b.players);
        expect((await report(b.players[0]!, ib, { objective: "biceps_gardien", zone: 2 })).body.data.status).toBe("accepted");
        expect(await completions(ev)).toHaveLength(1);
        // An objective that does not finish its zone (the bellows need two groups): nothing.
        expect((await report(b.players[0]!, ib, { objective: "forge_soufflet_ouest", zone: 4 })).body.data.status).toBe("accepted");
        expect(await completions(ev)).toHaveLength(1);
        const titan = await api("GET", `${V}/raids/events/${ev}/titan`, { token: a.players[0]!.token });
        const zoneFeed = titan.body.data.feed.filter((f: { kind: string }) => f.kind === "zone");
        expect(zoneFeed).toHaveLength(1);
        expect(zoneFeed[0]).toMatchObject({ by: "[AUBE]·1", zone: 2 });
        expect(zoneFeed[0].text).toContain("Zone terminée");
        const map = await api("GET", `${V}/raids/events/${ev}/branches/bras_droit`, { token: a.players[0]!.token });
        const z2 = map.body.data.zones.find((z: { id: number }) => z.id === 2);
        expect(z2).toMatchObject({ finished: true, finished_by: "[AUBE]·1" });
        expect(map.body.data.zones.find((z: { id: number }) => z.id === 4).finished).toBe(false);
    });
});

describe("2.75: the « Tableau des contributions » of a zone", () => {
    const rows: ZoneContribution[] = [
        { seat: 1, from: 0, boss_damage: 120, damage: 300, support: 2, mechanisms: 1, objectives: 1 },
        { seat: 0, from: 0, boss_damage: 360, damage: 410, support: 0, mechanisms: 0, objectives: 1 },
    ];

    it("is compared as the games write it, and impossible tables are refused before any majority", () => {
        expect(zoneCanonical(rows)).toBe("0,0,360,410,0,0,1;1,0,120,300,2,1,1;");
        expect(implausibleZone(rows, 6000)).toBe("");
        expect(implausibleZone([{ ...rows[0]!, boss_damage: 400 }], 6000)).toContain("boss");
        expect(implausibleZone([{ ...rows[0]!, from: 7000 }], 6000)).toContain("après");
        expect(implausibleZone([rows[0]!, rows[0]!], 6000)).toContain("double");
    });

    it("counts once the majority agrees, is kept in the results with each share", async () => {
        const a = await clan("AUBE", 3);
        const ev = await runningEvent(a);
        const ia = await instanceOf(ev, a.players);
        const table = (p: TestPlayer, r: ZoneContribution[]) => report(p, ia, { kind: "zone_table", zone: 2, records: r });
        expect((await table(a.players[0]!, rows)).body.data.status).toBe("pending");
        // A liar (more for themself): a minority, refused once the others agree.
        const liar = [{ ...rows[0]!, damage: 900 }, rows[1]!];
        expect((await table(a.players[2]!, liar)).body.data.status).toBe("pending");
        expect((await table(a.players[1]!, rows)).body.data.status).toBe("accepted");
        expect((await table(a.players[2]!, liar)).body.data.status).toBe("rejected");
        expect((await table(a.players[0]!, [{ ...rows[0]!, boss_damage: 999 }])).body.data.status).toBe("rejected"); // impossible
        const stored = await env.DB.prepare("SELECT seat, player_id, boss_damage FROM raid_zone_contributions WHERE instance_id = ?1 ORDER BY seat").bind(ia).all();
        expect(stored.results).toEqual([
            { seat: 0, player_id: a.players[0]!.id, boss_damage: 360 },
            { seat: 1, player_id: a.players[1]!.id, boss_damage: 120 },
        ]);
        const res = await api("GET", `${V}/raids/events/${ev}/results`, { token: a.players[1]!.token });
        expect(res.status).toBe(200);
        const t = res.body.data.zone_tables;
        expect(t).toHaveLength(1);
        expect(t[0]).toMatchObject({ zone: 2, group: "[AUBE]·1", share_of: "boss" });
        expect(t[0].rows.map((r: { share: number }) => r.share)).toEqual([0.75, 0.25]);
        expect(t[0].rows.find((r: { me: boolean }) => r.me).seat).toBe(1);
        // Another clan's player never sees this group's tables.
        const b = await clan("NEBU", 1);
        await env.DB.prepare("INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at) VALUES (?1, ?2, ?3, ?3, '', ?4, ?4)")
            .bind(ev, b.id, b.players[0]!.id, new Date().toISOString())
            .run();
        await env.DB.prepare("INSERT INTO raid_registrations (event_id, player_id, clan_id, created_at) VALUES (?1, ?2, ?3, ?4)")
            .bind(ev, b.players[0]!.id, b.id, new Date().toISOString())
            .run();
        expect((await api("GET", `${V}/raids/events/${ev}/results`, { token: b.players[0]!.token })).body.data.zone_tables).toEqual([]);
    });
});

describe("2.75: the administrators see the hidden presence (moderation), nobody else", () => {
    it("clan members, raid registrants and the players' list: the real state, marked « masqué », for an administrator only", async () => {
        const a = await clan("AUBE", 3);
        const [chief, hidden, mate] = a.players as [TestPlayer, TestPlayer, TestPlayer];
        const ev = await runningEvent(a);
        for (const p of [chief, hidden, mate]) expect((await api("POST", `${V}/presence`, { token: p.token, body: { state: "active" } })).status).toBe(200);
        expect((await api("PATCH", `${V}/players/me`, { token: hidden.token, body: { presence_visible: false, profile_visibility: "private" } })).status).toBe(200);
        const member = (data: any) => data.members.find((m: any) => m.player.id === hidden.id);
        const registrant = (data: any) => data.roster.find((r: any) => r.id === hidden.id);
        // A member (not an administrator): « hors ligne », never that it is hidden.
        const asMate = (await api("GET", `${V}/clans/${a.id}`, { token: mate.token })).body.data;
        expect(member(asMate).presence).toBe("offline");
        expect(member(asMate)).not.toHaveProperty("presence_hidden");
        expect(member(asMate)).not.toHaveProperty("profile_hidden");
        const sheetMate = (await api("GET", `${V}/raids/events/${ev}`, { token: mate.token })).body.data;
        expect(registrant(sheetMate).presence).toBe("offline");
        expect(registrant(sheetMate)).not.toHaveProperty("presence_hidden");
        // An outsider: nothing; and the players' list does not exist for them.
        const outsider = await registerPlayer("Curieux");
        expect(member((await api("GET", `${V}/clans/${a.id}`, { token: outsider.token })).body.data)).not.toHaveProperty("presence");
        expect((await api("GET", `${V}/admin/players`, { token: outsider.token })).status).toBe(404);
        // An administrator (outside the clan): the real state, marked; the location as the player shows it.
        const admin = await registerPlayer("Moderatrice");
        await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(admin.id).run();
        const asAdmin = (await api("GET", `${V}/clans/${a.id}`, { token: admin.token })).body.data;
        expect(member(asAdmin)).toMatchObject({ presence: "online", presence_hidden: true, profile_hidden: true });
        expect(asAdmin.members.find((m: any) => m.player.id === mate.id)).toMatchObject({ presence: "online", presence_hidden: false });
        expect(member(asAdmin).player.country_code ?? null).toBeNull();
        const list = (await api("GET", `${V}/admin/players?q=${encodeURIComponent(hidden.username)}`, { token: admin.token })).body.data.players;
        expect(list[0]).toMatchObject({ presence: "online", presence_hidden: true, profile_hidden: true });
        // An administrator who is also the clan's chief sees the registrants' real state too.
        await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(chief.id).run();
        const sheetAdmin = (await api("GET", `${V}/raids/events/${ev}`, { token: chief.token })).body.data;
        expect(registrant(sheetAdmin)).toMatchObject({ presence: "online", presence_hidden: true, profile_hidden: true });
        expect(sheetAdmin.roster_online).toBe(3);
        // A suspended administrator is no administrator.
        await env.DB.prepare("UPDATE players SET account_status = 'suspended' WHERE id = ?1").bind(admin.id).run();
        const suspended = await api("GET", `${V}/clans/${a.id}`, { token: admin.token });
        if (suspended.status === 200) expect(member(suspended.body.data)).not.toHaveProperty("presence_hidden");
    });
});
