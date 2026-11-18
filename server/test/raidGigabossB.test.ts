// Clan raids « Asterion », phase 4B (2.69, docs/RAIDS.md §13): the gigaboss of the abdomen and the
// legs (VOIDWORM, GRAVITON, TITANUS), of the skull, the left arm and the torso (SKULLMAW, DREADFANG,
// HEARTBREAKER). Their fights reported by the consensus of their branch's
// instance (zone 8 of the Abdomen, the Left leg, the Right leg), each its own badge granted once to
// those present at its fall (never Pyroclast's, never twice), the titan's part destroyed once its
// fall is accepted, the results naming each gigaboss fallen.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { GIGABOSS_BADGES, RAID_ACHIEVEMENTS, RAID_REWARDS, grantFor, type Contribution } from "../src/raidRewards";
import { catalogByPart } from "../src/raidMap";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const EMBLEM = { shape: "star", primary: "#FFD27A", secondary: "#a66bff" };
const iso = (minutes: number) => new Date(Date.now() + minutes * 60000).toISOString();
const H1 = "0123456789abcdef";
const TICK = 150000;

const BOSSES = [
    { part: "abdomen", boss: "Voidworm", objective: "abd_voidworm", before: ["abd_glande_nord", "abd_glande_sud", "abd_gosier"], badge: "voidworm_badge" },
    { part: "jambe_gauche", boss: "Graviton", objective: "jg_graviton", before: ["jg_generateur_ouest", "jg_generateur_est", "jg_cheville"], badge: "graviton_badge" },
    { part: "jambe_droite", boss: "Titanus", objective: "jd_titanus", before: ["jd_reacteur_ouest", "jd_reacteur_est", "jd_cheville"], badge: "titanus_badge" },
    { part: "crane", boss: "Skullmaw", objective: "crane_skullmaw", before: ["crane_oeil_gauche", "crane_oeil_droit", "crane_voute"], badge: "skullmaw_badge" },
    { part: "bras_gauche", boss: "Dreadfang", objective: "bg_dreadfang", before: ["bg_coeur_ouest", "bg_coeur_est", "bg_poignet"], badge: "dreadfang_badge" },
    {
        part: "torse",
        boss: "Heartbreaker",
        objective: "torse_heartbreaker",
        before: ["torse_plaque_nord", "torse_plaque_sud", "torse_plaque_est", "torse_plaque_ouest", "torse_cage"],
        badge: "heartbreaker_badge",
    },
] as const;

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

async function runningEvent(c: { id: string; players: TestPlayer[] }): Promise<string> {
    const id = crypto.randomUUID();
    const now = new Date().toISOString();
    await env.DB.prepare(
        `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity, difficulty, rewards_text, published_at, created_at, updated_at)
         VALUES (?1, 'world', 'asterion', 'standard', NULL, ?2, 30, ?3, ?4, 1, 0, 50, 3, '', ?5, ?5, ?5)`,
    )
        .bind(id, iso(-30), iso(-60), iso(50), now)
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

function row(seat: number, over: Partial<Contribution> = {}): Contribution {
    return { seat, from: 0, damage: 4000, boss_damage: 3000, avoided: 600, blocks: 4, rescues: 2, vents: 3, weak_points: 2, objectives: 3, ticks: 120000, gigaboss: 1, ...over };
}

/** Two players of one instance on the gigaboss's branch, its lair reachable (the objectives before it accepted). */
async function lairOf(b: (typeof BOSSES)[number], tag: string) {
    const a = await clan(tag, 2);
    const ev = await runningEvent(a);
    const branch = catalogByPart(b.part)!.id;
    for (const o of b.before)
        await env.DB.prepare("INSERT OR IGNORE INTO raid_branch_objectives (event_id, branch, objective_id, instance_id, accepted_at) VALUES (?1, ?2, ?3, NULL, ?4)")
            .bind(ev, branch, o, new Date().toISOString())
            .run();
    const j = await api("POST", `${V}/raids/events/${ev}/branches/${b.part}/join`, { token: a.players[0]!.token, body: {} });
    expect(j.status, JSON.stringify(j.body)).toBe(200);
    const inst = j.body.data.instance.id as string;
    await env.DB.prepare("UPDATE raid_instances SET started_at = ?2, players = ?3 WHERE id = ?1").bind(inst, iso(-25), 2).run();
    let seat = 0;
    for (const p of a.players)
        await env.DB.prepare("INSERT OR REPLACE INTO raid_instance_members (instance_id, player_id, seat, active, joined_at, left_at, seat_from) VALUES (?1, ?2, ?3, 1, ?4, NULL, 0)")
            .bind(inst, p.id, seat++, iso(-25))
            .run();
    const send = (p: TestPlayer, body: Record<string, unknown>) =>
        api("POST", `${V}/raids/instances/${inst}/reports`, { token: p.token, body: { zone: 8, tick: TICK, hash: H1, ...body } });
    return { a, ev, inst, send, p1: a.players[0]!, p2: a.players[1]! };
}

async function grants(eventId: string) {
    return (await env.DB.prepare("SELECT player_id, reward FROM raid_reward_grants WHERE event_id = ?1 ORDER BY player_id, reward").bind(eventId).all<{
        player_id: string;
        reward: string;
    }>()).results;
}

describe("raids phase 4B: Voidworm, Graviton, Titanus, Skullmaw, Dreadfang and Heartbreaker", () => {
    it("each its own badge, an achievement a claimer only may declare", () => {
        for (const b of BOSSES) {
            expect(GIGABOSS_BADGES[b.boss]).toBe(b.badge);
            const r = RAID_REWARDS[b.badge]!;
            expect(r.kind).toBe("badge");
            expect(RAID_ACHIEVEMENTS[r.ref]).toBe(b.badge);
            expect(catalogByPart(b.part)!.objectives.find((o) => o.id === b.objective)?.role).toBe("gigaboss");
        }
    });

    for (const b of BOSSES)
        it(`${b.boss}: its fall by the majority, its badge granted once, its part of the titan destroyed`, async () => {
            const { ev, inst, send, p1, p2 } = await lairOf(b, b.boss.slice(0, 4).toUpperCase());
            for (const p of [p1, p2]) expect((await send(p, { kind: "boss", phase: 4 })).body.data.status).not.toBe("rejected");
            const fight = await env.DB.prepare("SELECT boss, phase FROM raid_gigaboss_fights WHERE instance_id = ?1").bind(inst).first<{ boss: string; phase: number }>();
            expect(fight).toEqual({ boss: b.boss, phase: 4 });
            const records = [row(0), row(1, { damage: 200, boss_damage: 100, vents: 0, weak_points: 0, rescues: 0, ticks: 30000 })];
            expect((await send(p1, { kind: "result", records })).body.data.status).toBe("pending");
            expect((await send(p2, { kind: "result", records })).body.data.status).toBe("accepted");
            const g = await grants(ev);
            expect(g.filter((x) => x.player_id === p2.id).map((x) => x.reward)).toEqual([b.badge]);
            expect(g.some((x) => x.reward === "pyroclast_badge")).toBe(false);
            // Granted again (another acceptance, a replayed request): nothing more.
            await send(p1, { kind: "boss", phase: 4 });
            expect(await grantFor(env.DB, ev)).toBe(0);
            expect((await grants(ev)).length).toBe(g.length);
            // Its objective accepted: the titan's part is destroyed; the results name it.
            for (const p of [p1, p2]) await send(p, { kind: "objective", objective: b.objective });
            const titan = await api("GET", `${V}/raids/events/${ev}/titan`, { token: p1.token });
            expect(titan.status).toBe(200);
            expect(titan.body.data.body[b.part]).toBe("detruit");
            const res = await api("GET", `${V}/raids/events/${ev}/results`, { token: p1.token });
            expect(res.body.data.gigaboss.defeated_bosses).toEqual([b.boss]);
            expect(res.body.data.gigaboss.fights[0].boss).toBe(b.boss);
            // Claimed, its badge may be declared; never by a stranger.
            const claim = await api("POST", `${V}/raids/rewards/claim`, { token: p2.token, body: {} });
            expect(claim.body.data.claimed).toBe(1);
            const ach = RAID_REWARDS[b.badge]!.ref;
            expect((await api("PUT", `${V}/players/me/achievements`, { token: p2.token, body: { unlocked: [ach] } })).status).toBe(200);
            const ids = (await env.DB.prepare("SELECT ids FROM player_achievements WHERE player_id = ?1").bind(p2.id).first<{ ids: string }>())!.ids;
            expect(JSON.parse(ids)).toEqual([ach]);
            const cheat = await registerPlayer(`Faux${b.boss.slice(0, 3)}`);
            await api("PUT", `${V}/players/me/achievements`, { token: cheat.token, body: { unlocked: [ach] } });
            const cheatIds = await env.DB.prepare("SELECT ids FROM player_achievements WHERE player_id = ?1").bind(cheat.id).first<{ ids: string }>();
            expect(JSON.parse(cheatIds?.ids ?? "[]")).toEqual([]);
        });

    it("an administrator's test report is refused, whoever sends it", async () => {
        const { send, p1 } = await lairOf(BOSSES[0], "TEST");
        const r = await send(p1, { kind: "boss", phase: 4, test: true });
        expect(r.status).toBe(403);
    });
});
