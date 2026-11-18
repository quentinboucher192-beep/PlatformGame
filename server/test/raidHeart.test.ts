// Clan raids « Asterion », phase 6 (2.72, docs/RAIDS.md §15): LE CŒUR D'ASTERION. Sealed until the seven
// gigaboss fell (validated), then joined by every group at once; its health SHARED: each instance's
// consensus validates its own part, kept as the highest value per instance, summed by the server and given
// back to every heart relay (the others' part, the shared phase, the relics); its fall written once when
// the parts reach its whole health (two groups crossing the line together: once), journaled, told once,
// rewarded once (badge, « Supernova d'Asterion », the clans' collective title, the top's profile decor);
// the world's outcome « Asterion est tombé » needs it with the seven gigaboss.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { CATALOGS, HEART, catalogByPart, partOf, type Catalog } from "../src/raidMap";
import { HEART_OBJECTIVE, checkHeartFall, grantHeart, grantHeartTop, heartPackFor, packHeart, phaseOf, RELICS } from "../src/raidHeart";
import { GIGABOSS_BADGES, RAID_ACHIEVEMENTS, RAID_REWARDS } from "../src/raidRewards";
import { aggregate, checkMap } from "../src/raidWorld";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const EMBLEM = { shape: "star", primary: "#FFD27A", secondary: "#a66bff" };
const iso = (minutes: number) => new Date(Date.now() + minutes * 60000).toISOString();
const H1 = "0123456789abcdef";
const TICK = 150000;

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

async function worldEvent(clans: { id: string; players: TestPlayer[] }[]): Promise<string> {
    const id = crypto.randomUUID();
    const now = new Date().toISOString();
    await env.DB.prepare(
        `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity, difficulty, rewards_text, published_at, created_at, updated_at)
         VALUES (?1, 'world', 'asterion', 'standard', NULL, ?2, 30, ?3, ?4, 1, 0, 50, 3, '', ?5, ?5, ?5)`,
    )
        .bind(id, iso(-30), iso(-60), iso(50), now)
        .run();
    let k = 0;
    for (const c of clans) {
        await env.DB.prepare("INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at) VALUES (?1, ?2, ?3, ?3, '', ?4, ?4)")
            .bind(id, c.id, c.players[0]!.id, now)
            .run();
        for (const p of c.players)
            await env.DB.prepare("INSERT INTO raid_registrations (event_id, player_id, clan_id, created_at) VALUES (?1, ?2, ?3, ?4)")
                .bind(id, p.id, c.id, new Date(Date.now() - 60000 + ++k * 10).toISOString())
                .run();
    }
    return id;
}

async function accept(ev: string, c: Catalog, objective: string) {
    await env.DB.prepare("INSERT OR IGNORE INTO raid_branch_objectives (event_id, branch, objective_id, instance_id, accepted_at) VALUES (?1, ?2, ?3, NULL, ?4)")
        .bind(ev, c.id, objective, new Date().toISOString())
        .run();
}

/** The seven gigaboss fallen (validated): the heart opens; its aorta crossed (its arena reachable). */
async function openHeart(ev: string) {
    for (const c of CATALOGS) await accept(ev, c, c.objectives.find((o) => o.role === "gigaboss")!.id);
    await accept(ev, HEART!, "coeur_aorte");
}

/** A clan's instance in the heart, its players seated and active. */
async function heartInstance(ev: string, c: { players: TestPlayer[] }) {
    const j = await api("POST", `${V}/raids/events/${ev}/branches/coeur/join`, { token: c.players[0]!.token, body: {} });
    expect(j.status, JSON.stringify(j.body)).toBe(200);
    expect(j.body.data.instance.part).toBe("coeur");
    const inst = j.body.data.instance.id as string;
    await env.DB.prepare("UPDATE raid_instances SET started_at = ?2, players = ?3 WHERE id = ?1").bind(inst, iso(-25), c.players.length).run();
    let seat = 0;
    for (const p of c.players)
        await env.DB.prepare("INSERT OR REPLACE INTO raid_instance_members (instance_id, player_id, seat, active, joined_at, left_at, seat_from) VALUES (?1, ?2, ?3, 1, ?4, NULL, 0)")
            .bind(inst, p.id, seat++, iso(-25))
            .run();
    const send = (p: TestPlayer, body: Record<string, unknown>) =>
        api("POST", `${V}/raids/instances/${inst}/reports`, { token: p.token, body: { zone: 2, tick: TICK, hash: H1, ...body } });
    return { inst, send };
}

const damageOf = async (inst: string) =>
    (await env.DB.prepare("SELECT damage FROM raid_heart_damage WHERE instance_id = ?1").bind(inst).first<{ damage: number }>())?.damage ?? null;

describe("raids phase 6: the Cœur d'Asterion", () => {
    it("its catalog is checked with the titan's: an arena, its fall, a « modifier » link from each of the seven parts", () => {
        expect(HEART).toBeTruthy();
        expect(catalogByPart("coeur")?.id).toBe(HEART!.id);
        expect(partOf(HEART!.id)).toBe("coeur");
        const c = checkMap();
        expect(c.problems).toEqual([]);
        expect(c.heart).toBe(true);
        expect(c.branches).toBe(7);
        expect(RELICS.length).toBe(7);
        expect(GIGABOSS_BADGES[HEART!.gigaboss]).toBe("coeur_badge");
        expect(RAID_ACHIEVEMENTS[RAID_REWARDS.coeur_badge!.ref]).toBe("coeur_badge");
        expect(packHeart(4321, 3, 6)).toBe((4321 | (3 << 16) | (6 << 24)) >>> 0);
        expect([phaseOf(0), phaseOf(2500), phaseOf(5000), phaseOf(9999)]).toEqual([1, 2, 3, 4]);
    });

    it("sealed until the seven gigaboss fell; then every group joins it, the players told once", async () => {
        const a = await clan("SCEL", 2);
        const ev = await worldEvent([a]);
        const sealed = await api("POST", `${V}/raids/events/${ev}/branches/coeur/join`, { token: a.players[0]!.token, body: {} });
        expect(sealed.status).toBe(409);
        expect(sealed.body.error.code).toBe("HEART_SEALED");
        for (const c of CATALOGS.slice(0, 6)) await accept(ev, c, c.objectives.find((o) => o.role === "gigaboss")!.id);
        expect((await api("POST", `${V}/raids/events/${ev}/branches/coeur/join`, { token: a.players[0]!.token, body: {} })).status).toBe(409);
        await openHeart(ev);
        const titan = await api("GET", `${V}/raids/events/${ev}/titan`, { token: a.players[0]!.token });
        expect(titan.body.data.heart.state).toBe("ouvert");
        expect(titan.body.data.heart.modifiers.filter((m: { on: boolean }) => m.on).length).toBe(7);
        const { inst } = await heartInstance(ev, a);
        expect(inst).toBeTruthy();
        // The server sends a group that lets it choose to the heart now.
        const b = await registerPlayer("SCELLibre");
        expect((await api("POST", `${V}/clans/${a.id}/join`, { token: b.token, body: {} })).status).toBe(200);
        await env.DB.prepare("INSERT INTO raid_registrations (event_id, player_id, clan_id, created_at) VALUES (?1, ?2, ?3, ?4)").bind(ev, b.id, a.id, iso(-1)).run();
        const auto = await api("POST", `${V}/raids/events/${ev}/join`, { token: b.token, body: {} });
        expect(auto.status, JSON.stringify(auto.body)).toBe(200);
        expect(auto.body.data.instance.part).toBe("coeur");
    });

    it("its shared health: each instance's part by its majority, the highest kept, the others' part given to each relay", async () => {
        const a = await clan("PART", 2);
        const b = await clan("AUTR", 2);
        const ev = await worldEvent([a, b]);
        await openHeart(ev);
        const x = await heartInstance(ev, a);
        const y = await heartInstance(ev, b);
        expect((await x.send(a.players[0]!, { kind: "heart", damage: 6000 })).body.data.status).toBe("pending");
        expect((await x.send(a.players[1]!, { kind: "heart", damage: 6000 })).body.data.status).toBe("accepted");
        expect(await damageOf(x.inst)).toBe(6000);
        // A later table lower than the one kept never lowers it (a late or repeated report).
        for (const p of a.players) await x.send(p, { kind: "heart", damage: 5000, tick: TICK + 1200 });
        expect(await damageOf(x.inst)).toBe(6000);
        // The other instance's relay gives its games what the first one took, the shared phase.
        const pack = await heartPackFor(env.DB, ev, y.inst);
        expect(pack & 0xffff).toBe(6000);
        expect((pack >>> 16) & 0xff).toBe(3);
        expect((await heartPackFor(env.DB, ev, x.inst)) & 0xffff).toBe(0);
        // Implausible: a part a group cannot take that fast; the heart outside its arena; a test.
        expect((await y.send(b.players[0]!, { kind: "heart", damage: 9000, tick: 600 })).body.data.status).toBe("rejected");
        expect((await y.send(b.players[0]!, { kind: "heart", damage: 100, zone: 1 })).body.data.status).toBe("rejected");
        expect((await y.send(b.players[0]!, { kind: "heart", damage: 100, test: true })).status).toBe(403);
        expect((await y.send(b.players[0]!, { kind: "zone", damage: 100 })).status).toBe(400);
    });

    it("it falls once for the world when the parts reach its health (two groups at once), told and rewarded once", async () => {
        const a = await clan("CHUA", 2);
        const b = await clan("CHUB", 2);
        const ev = await worldEvent([a, b]);
        await openHeart(ev);
        for (const c of CATALOGS) for (const r of RELICS.filter((x) => x.branch === c.id)) await accept(ev, c, r.objective);
        const x = await heartInstance(ev, a);
        const y = await heartInstance(ev, b);
        for (const p of a.players) await x.send(p, { kind: "heart", damage: 5500 });
        expect(await checkHeartFall(env.DB, { id: ev }, null)).toBe(false);
        // Both groups' last reports arrive together: its objective written once.
        await Promise.all([...b.players.map((p) => y.send(p, { kind: "heart", damage: 4500 })), ...a.players.map((p) => x.send(p, { kind: "heart", damage: 5600, tick: TICK + 120 }))]);
        const rows = await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_branch_objectives WHERE event_id = ?1 AND branch = ?2 AND objective_id = ?3")
            .bind(ev, HEART!.id, HEART_OBJECTIVE)
            .first<{ n: number }>();
        expect(rows!.n).toBe(1);
        expect(await checkHeartFall(env.DB, { id: ev }, null)).toBe(true);
        const journal = await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_world_journal WHERE event_id = ?1 AND code = 'heart_fallen'").bind(ev).first<{ n: number }>();
        expect(journal!.n).toBe(1);
        const told = await env.DB.prepare("SELECT COUNT(*) AS n FROM notifications WHERE group_key = ?1").bind(`raid:heart_fallen:${ev}`).first<{ n: number }>();
        expect(told!.n).toBe(4);
        // Every relay now gives the fall (phase 5) and the relics; the world's outcome needs it.
        const pack = await heartPackFor(env.DB, ev, x.inst);
        expect((pack >>> 16) & 0xff).toBe(5);
        expect(pack >>> 24).toBe(7);
        const agg = await aggregate(env.DB, ev);
        expect(agg.world.heart_down).toBe(true);
        expect(agg.outcome).toBe("vaincu");
        const titan = await api("GET", `${V}/raids/events/${ev}/titan`, { token: a.players[0]!.token });
        expect(titan.body.data.heart.state).toBe("tombe");
        expect(titan.body.data.body.coeur).toBe("detruit");
        const sheet = await api("GET", `${V}/raids/events/${ev}`, { token: a.players[0]!.token });
        expect(sheet.body.data.event?.body?.coeur ?? sheet.body.data.body?.coeur).toBe("detruit");
    });

    it("its rewards: the badge at its fall, « Supernova » to its fighters, the clans' title, the top's decor at the end — each once", async () => {
        const a = await clan("RECA", 2);
        const ev = await worldEvent([a]);
        await openHeart(ev);
        const x = await heartInstance(ev, a);
        for (const p of a.players) await x.send(p, { kind: "boss", phase: 4 });
        const records = [0, 1].map((seat) => ({
            seat,
            from: 0,
            damage: 4000,
            boss_damage: 3000,
            avoided: 600,
            blocks: 4,
            rescues: 1,
            vents: 3,
            weak_points: 2,
            objectives: 1,
            ticks: 90000,
            gigaboss: 1,
        }));
        for (const p of a.players) await x.send(p, { kind: "result", records });
        for (const p of a.players) await x.send(p, { kind: "heart", damage: 10000 });
        const grants = async () =>
            (await env.DB.prepare("SELECT player_id, reward FROM raid_reward_grants WHERE event_id = ?1 ORDER BY player_id, reward").bind(ev).all<{ player_id: string; reward: string }>())
                .results;
        const g = await grants();
        for (const p of a.players) {
            const mine = g.filter((r) => r.player_id === p.id).map((r) => r.reward);
            expect(mine).toContain("coeur_badge");
            expect(mine).toContain("coeur_supernova");
            expect(mine).toContain("coeur_clan");
        }
        const title = await env.DB.prepare("SELECT title FROM raid_clan_titles WHERE event_id = ?1 AND clan_id = ?2").bind(ev, a.id).all<{ title: string }>();
        expect(title.results.map((t) => t.title)).toContain("Briseurs d'étoile");
        // Again (a replayed report, the cron): nothing more.
        expect(await grantHeart(env.DB, ev)).toBe(0);
        expect((await grants()).length).toBe(g.length);
        // The profile decor: the top 10 of the mixed score, once (at the end).
        expect(await grantHeartTop(env.DB, ev)).toBe(2);
        expect(await grantHeartTop(env.DB, ev)).toBe(0);
        // Claimed: the items unlocked (the profile, the vestiaire).
        const claim = await api("POST", `${V}/raids/rewards/claim`, { token: a.players[0]!.token, body: {} });
        expect(claim.status).toBe(200);
        const won = await env.DB.prepare("SELECT reward_ref FROM player_won_rewards WHERE player_id = ?1 ORDER BY reward_ref").bind(a.players[0]!.id).all<{ reward_ref: string }>();
        expect(won.results.map((r) => r.reward_ref)).toEqual(expect.arrayContaining(["victory_supernova_asterion", "frame_coeur_asterion", "title_briseurs_etoile"]));
    });
});
