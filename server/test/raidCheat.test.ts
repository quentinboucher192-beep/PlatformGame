// Clan raids « Asterion », phase 7 (2.74, docs/RAIDS.md §15 « Fiabilité, charge et anti-triche »): the
// consensus against cheats and races. Absent accomplices never outvote the players in play; a
// minority report that crosses the acceptance is refused and journaled once (never told
// « accepted »); a close that crosses an acceptance wins; ties are never accepted; an instance of
// liars only gets what is plausible; replayed, duplicated and out-of-order reports change nothing;
// the identity is the session's; floods are limited; the frozen official result cannot be changed
// by any statement.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { report } from "../src/raidZones";
import { bodyOf, countingDb, directCtx } from "./d1count";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const EMBLEM = { shape: "star", primary: "#FFD27A", secondary: "#a66bff" };
const iso = (minutes: number) => new Date(Date.now() + minutes * 60000).toISOString();
const H1 = "0123456789abcdef";
const H2 = "fedcba9876543210";
const TICK = 6000;

let clanNo = 0;
/** A clan of `n`, registered to a running world event; its first instance (bras droit), started 10 min ago. */
async function setup(n: number): Promise<{ ev: string; inst: string; players: TestPlayer[]; clan: string }> {
    const tag = `T${++clanNo}`;
    const chief = await registerPlayer(`${tag}Chef`);
    const r = await api("POST", `${V}/clans`, { token: chief.token, body: { name: `Clan ${tag}`, tag, motto: "Ensemble.", emblem: EMBLEM, join_policy: "open" } });
    expect(r.status, JSON.stringify(r.body)).toBe(201);
    const clan = r.body.data.clan.id as string;
    const players = [chief];
    for (let i = 1; i < n; ++i) {
        const p = await registerPlayer(`${tag}M${i}`);
        expect((await api("POST", `${V}/clans/${clan}/join`, { token: p.token, body: {} })).status).toBe(200);
        players.push(p);
    }
    const ev = crypto.randomUUID();
    const now = new Date().toISOString();
    await env.DB.prepare(
        `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity, difficulty, rewards_text, published_at, created_at, updated_at)
         VALUES (?1, 'world', 'asterion', 'standard', NULL, ?2, 30, ?3, ?4, 1, 0, 50, 3, '', ?5, ?5, ?5)`,
    )
        .bind(ev, iso(-10), iso(-40), iso(50), now)
        .run();
    await env.DB.prepare("INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at) VALUES (?1, ?2, ?3, ?3, '', ?4, ?4)")
        .bind(ev, clan, chief.id, now)
        .run();
    let k = 0;
    for (const p of players)
        await env.DB.prepare("INSERT INTO raid_registrations (event_id, player_id, clan_id, created_at) VALUES (?1, ?2, ?3, ?4)")
            .bind(ev, p.id, clan, new Date(Date.now() - 60000 + ++k * 10).toISOString())
            .run();
    const j = await api("POST", `${V}/raids/events/${ev}/instance`, { token: chief.token, body: {} });
    expect(j.status, JSON.stringify(j.body)).toBe(200);
    const inst = j.body.data.instance.id as string;
    await env.DB.prepare("UPDATE raid_instances SET started_at = ?2, players = ?3 WHERE id = ?1").bind(inst, iso(-10), n).run();
    return { ev, inst, players, clan };
}

/** As the relay writes them: `active` in play, the others absent (still members: within their 60 s). */
async function seat(inst: string, players: TestPlayer[], active: boolean) {
    for (const p of players)
        await env.DB.prepare(
            `INSERT INTO raid_instance_members (instance_id, player_id, seat, active, joined_at, left_at, seat_from) VALUES (?1, ?2, ?3, ?4, ?5, NULL, 0)
             ON CONFLICT (instance_id, player_id) DO UPDATE SET active = ?4`,
        )
            .bind(inst, p.id, (await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_instance_members WHERE instance_id = ?1").bind(inst).first<{ n: number }>())!.n, active ? 1 : 0, iso(-10))
            .run();
}

const send = (p: TestPlayer, inst: string, body: Record<string, unknown>) =>
    api("POST", `${V}/raids/instances/${inst}/reports`, { token: p.token, body: { kind: "objective", objective: "epaule_arene", zone: 1, tick: TICK, hash: H1, ...body } });
const statusOf = async (p: TestPlayer, inst: string, body: Record<string, unknown> = {}) => (await send(p, inst, body)).body.data?.status as string;
const accepted = (inst: string, key: string) => env.DB.prepare("SELECT value FROM raid_accepted WHERE instance_id = ?1 AND report_key = ?2").bind(inst, key).first<{ value: string }>();
const journaled = async (inst: string, p: TestPlayer) =>
    (await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_report_rejections WHERE instance_id = ?1 AND player_id = ?2").bind(inst, p.id).first<{ n: number }>())!.n;

/** One report called directly, with hooks run just before some of its statements (a race made exact). */
async function racedReport(p: TestPlayer, inst: string, body: Record<string, unknown>, hooks: { match: RegExp; run: () => Promise<void> }[]) {
    const { db } = countingDb(env.DB, hooks);
    const { ctx, waits } = directCtx({
        method: "POST",
        path: `${V}/raids/instances/${inst}/reports`,
        params: { instanceId: inst },
        token: p.token,
        body: { kind: "objective", objective: "epaule_arene", zone: 1, tick: TICK, hash: H1, ...body },
        db,
    });
    const data = await bodyOf(await report(ctx));
    await Promise.allSettled(waits);
    return data as { status: string; reason?: string };
}

describe("raids phase 7: the consensus against cheats", () => {
    it("absent accomplices never outvote the players in play (before 2.74, three absent members' reports won against two in play)", async () => {
        const { inst, players } = await setup(5);
        const [a, b, c, d, e] = players as [TestPlayer, TestPlayer, TestPlayer, TestPlayer, TestPlayer];
        await seat(inst, [a, b], true);
        await seat(inst, [c, d, e], false);
        for (const liar of [c, d, e]) expect(await statusOf(liar, inst, { hash: H2 })).toBe("pending");
        expect(await statusOf(a, inst)).toBe("pending");
        expect(await statusOf(b, inst)).toBe("accepted");
        expect((await accepted(inst, "o:epaule_arene"))!.value).toBe(`1:${TICK}:${H1}`);
        for (const liar of [c, d, e]) expect(await journaled(inst, liar)).toBe(1);
        // An absent member back in play counts again (the relay writes it active).
        await seat(inst, [c], true);
        expect(await statusOf(c, inst, { objective: "biceps_gardien", zone: 2 })).toBe("pending");
    });

    it("a minority report that crosses the acceptance is refused and journaled once, never told « accepted »", async () => {
        const { inst, players } = await setup(3);
        const [a, b, liar] = players as [TestPlayer, TestPlayer, TestPlayer];
        await seat(inst, players, true);
        expect(await statusOf(a, inst)).toBe("pending");
        // The liar's request reads « nothing accepted yet », then B's report is accepted, then the liar counts.
        const res = await racedReport(liar, inst, { hash: H2 }, [
            {
                match: /^\s*SELECT r\.player_id, r\.value/,
                run: async () => expect(await statusOf(b, inst)).toBe("accepted"),
            },
        ]);
        expect(res.status).toBe("rejected");
        expect(res.reason).toContain("minoritaire");
        expect(await journaled(inst, liar)).toBe(1);
        expect((await accepted(inst, "o:epaule_arene"))!.value).toBe(`1:${TICK}:${H1}`);
        // In parallel, many times: an honest report is never refused, a liar never accepted.
        for (let round = 0; round < 3; ++round) {
            const s = await setup(3);
            await seat(s.inst, s.players, true);
            const [r1, r2, r3] = await Promise.all([statusOf(s.players[0]!, s.inst), statusOf(s.players[1]!, s.inst), statusOf(s.players[2]!, s.inst, { hash: H2 })]);
            expect([r1, r2]).not.toContain("rejected");
            expect(r3).not.toBe("accepted");
            expect(await journaled(s.inst, s.players[2]!)).toBe(1);
        }
    });

    it("the official result frozen while a report completes its majority: the close wins, nothing is accepted after it", async () => {
        const { ev, inst, players } = await setup(2);
        const [a, b] = players as [TestPlayer, TestPlayer];
        await seat(inst, players, true);
        expect(await statusOf(a, inst)).toBe("pending");
        const res = await racedReport(b, inst, {}, [
            {
                match: /^\s*INSERT OR IGNORE INTO raid_accepted/,
                run: async () => {
                    await env.DB.prepare("INSERT INTO raid_event_results (event_id, outcome, data, frozen_at) VALUES (?1, 'survecu', '{}', ?2)").bind(ev, new Date().toISOString()).run();
                },
            },
        ]);
        expect(res.status).toBe("rejected");
        expect(res.reason).toContain("figés");
        expect(await accepted(inst, "o:epaule_arene")).toBeNull();
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_branch_objectives WHERE event_id = ?1").bind(ev).first<{ n: number }>())!.n).toBe(0);
        // The same while the administrators suspend the event.
        const s = await setup(2);
        await seat(s.inst, s.players, true);
        expect(await statusOf(s.players[0]!, s.inst)).toBe("pending");
        const sus = await racedReport(s.players[1]!, s.inst, {}, [
            {
                match: /^\s*INSERT OR IGNORE INTO raid_accepted/,
                run: async () => void (await env.DB.prepare("UPDATE raid_events SET suspended_at = ?2, suspended_reason = 'test' WHERE id = ?1").bind(s.ev, new Date().toISOString()).run()),
            },
        ]);
        expect(sus.status).toBe("rejected");
        expect(sus.reason).toContain("suspendu");
        expect(await accepted(s.inst, "o:epaule_arene")).toBeNull();
    });

    it("ties are never accepted; an instance of liars only gets what is plausible, every impossible claim journaled", async () => {
        const tie = await setup(4);
        await seat(tie.inst, tie.players, true);
        const [w, x, y, z] = tie.players as [TestPlayer, TestPlayer, TestPlayer, TestPlayer];
        expect(await statusOf(w, tie.inst)).toBe("pending");
        expect(await statusOf(x, tie.inst)).toBe("pending");
        expect(await statusOf(y, tie.inst, { hash: H2 })).toBe("pending");
        expect(await statusOf(z, tie.inst, { hash: H2 })).toBe("pending");
        expect(await accepted(tie.inst, "o:epaule_arene")).toBeNull();
        expect(await statusOf(w, tie.inst, { hash: H2 })).toBe("rejected"); // one value per player and key
        // Three liars, the whole instance: they agree, but only on what could have happened.
        const { inst, players } = await setup(3);
        await seat(inst, players, true);
        const claims: [Record<string, unknown>, string][] = [
            [{ objective: "poignet_verrou", zone: 7 }, "zone verrouillée pour la branche"],
            [{ objective: "biceps_gardien", zone: 3 }, "objectif d'une autre zone"],
            [{ objective: "crane_machoire", zone: 1 }, "objectif inconnu"],
            [{ tick: 120 * 3600 }, "pas impossible depuis le début de l'instance"],
            [{ kind: "boss", phase: 4, zone: 1, objective: undefined }, "le gigaboss n'est pas dans cette zone"],
            [{ kind: "result", objective: undefined, records: [{ seat: 0, from: 0, damage: 999999, boss_damage: 0, avoided: 0, blocks: 0, rescues: 0, vents: 0, weak_points: 0, objectives: 0, ticks: 10, gigaboss: 0 }] }, "dégâts impossibles"],
            [{ kind: "result", objective: undefined, records: [{ seat: 0, from: 0, damage: 0, boss_damage: 0, avoided: 0, blocks: 0, rescues: 0, vents: 50, weak_points: 0, objectives: 0, ticks: 10, gigaboss: 0 }] }, "évents impossibles"],
        ];
        for (const [body, reason] of claims)
            for (const p of players) {
                const r = await send(p, inst, body);
                expect(r.body.data.status, JSON.stringify(body)).toBe("rejected");
                expect(r.body.data.reason).toBe(reason);
            }
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_report_rejections WHERE instance_id = ?1").bind(inst).first<{ n: number }>())!.n).toBe(claims.length * 3);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_accepted WHERE instance_id = ?1").bind(inst).first<{ n: number }>())!.n).toBe(0);
    });

    it("replayed, duplicated and out-of-order reports change nothing; the identity is the session's; floods are limited", async () => {
        const { ev, inst, players } = await setup(1);
        const [p] = players as [TestPlayer];
        await seat(inst, players, true);
        // The same report twenty times at once: one report, one objective.
        const same = await Promise.all(Array.from({ length: 20 }, () => statusOf(p, inst)));
        expect(same.every((s) => s === "accepted")).toBe(true);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_reports WHERE instance_id = ?1").bind(inst).first<{ n: number }>())!.n).toBe(1);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_branch_objectives WHERE event_id = ?1").bind(ev).first<{ n: number }>())!.n).toBe(1);
        // Two zone reports accepted in the other order (their requests crossed): the newest step stays on the map.
        expect(await statusOf(p, inst, { kind: "zone", zone: 4, tick: 9000, objective: undefined })).toBe("accepted");
        expect(await statusOf(p, inst, { kind: "zone", zone: 3, tick: 7000, objective: undefined })).toBe("accepted");
        expect((await env.DB.prepare("SELECT zone, zone_tick FROM raid_instances WHERE id = ?1").bind(inst).first<{ zone: number; zone_tick: number }>())).toEqual({ zone: 4, zone_tick: 9000 });
        // Two tables in the other order: the newest numbers stay.
        const table = (damage: number) => [{ seat: 0, from: 0, damage, boss_damage: 0, avoided: 0, blocks: 0, rescues: 0, vents: 0, weak_points: 0, objectives: 1, ticks: 5000, gigaboss: 0 }];
        expect(await statusOf(p, inst, { kind: "result", tick: 6000, objective: undefined, records: table(5000) })).toBe("accepted");
        expect(await statusOf(p, inst, { kind: "result", tick: 5500, objective: undefined, records: table(4000) })).toBe("accepted");
        expect((await env.DB.prepare("SELECT damage, tick FROM raid_contributions WHERE instance_id = ?1").bind(inst).first<{ damage: number; tick: number }>())).toEqual({ damage: 5000, tick: 6000 });
        // Who reports is the session's account: a player or a seat in the body is refused.
        expect((await send(p, inst, { player_id: crypto.randomUUID() })).status).toBe(400);
        expect((await send(p, inst, { seat: 0 })).status).toBe(400);
        // A registered player who does not play in this instance, a stranger: refused.
        const stranger = await registerPlayer("Etranger");
        expect((await send(stranger, inst, {})).status).toBe(403);
        await env.DB.prepare("UPDATE raid_instance_members SET left_at = ?2, active = 0 WHERE instance_id = ?1").bind(inst, new Date().toISOString()).run();
        expect((await send(p, inst, {})).status).toBe(403); // its seat was freed
        await env.DB.prepare("UPDATE raid_instance_members SET left_at = NULL, active = 1 WHERE instance_id = ?1").bind(inst).run();
        // A flood: past 600 reports in 10 minutes, 429.
        await env.DB.prepare("UPDATE rate_limits SET count = 600 WHERE bucket LIKE 'raid_report:%'").run();
        const flood = await send(p, inst, {});
        expect(flood.status).toBe(429);
        expect(flood.headers.get("retry-after")).not.toBeNull();
    });

    it("the frozen official result: no update, no delete, no replace (only its event's deletion takes it)", async () => {
        const { ev } = await setup(1);
        await env.DB.prepare("INSERT INTO raid_event_results (event_id, outcome, data, frozen_at) VALUES (?1, 'survecu', '{\"v\":1}', ?2)").bind(ev, new Date().toISOString()).run();
        await expect(env.DB.prepare("UPDATE raid_event_results SET outcome = 'vaincu' WHERE event_id = ?1").bind(ev).run()).rejects.toThrow(/frozen/);
        await expect(env.DB.prepare("DELETE FROM raid_event_results WHERE event_id = ?1").bind(ev).run()).rejects.toThrow(/frozen/);
        const replace = await env.DB.prepare("INSERT OR REPLACE INTO raid_event_results (event_id, outcome, data, frozen_at) VALUES (?1, 'vaincu', '{}', ?2)").bind(ev, new Date().toISOString()).run();
        expect(replace.meta.changes).toBe(0);
        expect(await env.DB.prepare("SELECT outcome, data FROM raid_event_results WHERE event_id = ?1").bind(ev).first()).toEqual({ outcome: "survecu", data: '{"v":1}' });
        // The marks of the close (rewards, notifications) may still be written once.
        expect((await env.DB.prepare("UPDATE raid_event_results SET rewards_at = ?2 WHERE event_id = ?1 AND rewards_at IS NULL").bind(ev, new Date().toISOString()).run()).meta.changes).toBe(1);
        await env.DB.prepare("DELETE FROM raid_events WHERE id = ?1").bind(ev).run();
        expect(await env.DB.prepare("SELECT 1 AS x FROM raid_event_results WHERE event_id = ?1").bind(ev).first()).toBeNull();
    });
});
