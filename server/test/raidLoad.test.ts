// Clan raids « Asterion », phase 7 (2.74, docs/RAIDS.md §15 « Fiabilité, charge et anti-triche »): the
// load. Four clans of 50 players register at once, are placed on the titan at once (dozens of
// instances of 8), every instance validates its objectives and its contributions at the same
// moment, the world is aggregated while the reports still arrive, everyone claims their rewards
// twice at once, the cron and the readers close the event together. Nothing is counted twice,
// granted twice or lost; every route makes a bounded number of D1 round trips (the same with 2
// instances or 30), no hot statement reads a whole raid table, and the time of the heavy requests
// is measured (the numbers of docs/RAIDS.md).

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { CATALOGS, branchView as raidMapBranch, join, titanView, type Catalog } from "../src/raidMap";
import { claim, grantEndedTops } from "../src/raidRewards";
import { raidTick } from "../src/raids";
import { adminLive, aggregate, finalizeEvent, worldTick, worldView } from "../src/raidWorld";
import { branchView, report } from "../src/raidZones";
import { bodyOf, countingDb, directCtx, fullScans, type Recorded } from "./d1count";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const EMBLEM = { shape: "star", primary: "#FFD27A", secondary: "#a66bff" };
const iso = (minutes: number) => new Date(Date.now() + minutes * 60000).toISOString();
const TICK = 150000; // 20 min 50 s into instances started 25 min ago
const H1 = "0123456789abcdef";
const RAID_TABLES = /^raid_/;

interface Clan {
    id: string;
    tag: string;
    players: TestPlayer[];
}

/** Runs `n` tasks, `width` at a time (the Worker serves them concurrently). */
async function parallel<T>(n: number, width: number, task: (i: number) => Promise<T>): Promise<T[]> {
    const out: T[] = new Array(n);
    for (let at = 0; at < n; at += width) {
        const part = await Promise.all(Array.from({ length: Math.min(width, n - at) }, (_, k) => task(at + k)));
        part.forEach((v, k) => (out[at + k] = v));
    }
    return out;
}

async function clanOf(tag: string, n: number): Promise<Clan> {
    const chief = await registerPlayer(`${tag}Chef`);
    const r = await api("POST", `${V}/clans`, { token: chief.token, body: { name: `Clan ${tag}`, tag, motto: "Ensemble.", emblem: EMBLEM, join_policy: "open" } });
    expect(r.status, JSON.stringify(r.body)).toBe(201);
    const id = r.body.data.clan.id as string;
    const members = await parallel(n - 1, 25, (i) => registerPlayer(`${tag}M${i + 1}`));
    await parallel(members.length, 25, async (i) => expect((await api("POST", `${V}/clans/${id}/join`, { token: members[i]!.token, body: {} })).status).toBe(200));
    return { id, tag, players: [chief, ...members] };
}

/** A world event; `open`: its registrations still open (lock in 1 h), else running (started 30 min ago, ends in 50). */
async function worldEvent(clans: Clan[], open: boolean, capacity = 0): Promise<string> {
    const id = crypto.randomUUID();
    const now = new Date().toISOString();
    await env.DB.prepare(
        `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity, difficulty, rewards_text, published_at, created_at, updated_at)
         VALUES (?1, 'world', 'asterion', 'standard', NULL, ?2, 30, ?3, ?4, 1, ?6, 50, 3, '', ?5, ?5, ?5)`,
    )
        .bind(id, open ? iso(90) : iso(-30), open ? iso(60) : iso(-60), open ? iso(150) : iso(50), now, capacity)
        .run();
    await env.DB.batch(
        clans.map((c) =>
            env.DB.prepare("INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at) VALUES (?1, ?2, ?3, ?3, '', ?4, ?4)").bind(
                id,
                c.id,
                c.players[0]!.id,
                now,
            ),
        ),
    );
    return id;
}

const running = (ev: string) => env.DB.prepare("UPDATE raid_events SET starts_at = ?2, lock_at = ?3, ends_at = ?4 WHERE id = ?1").bind(ev, iso(-30), iso(-60), iso(50)).run();
const count = async (sql: string, ...binds: unknown[]) => (await env.DB.prepare(sql).bind(...binds).first<{ n: number }>())!.n;

/** The relay's work, done directly: every placed player is a member in play of their instance, started 25 min ago. */
async function seatPlaced(ev: string): Promise<Map<string, string[]>> {
    const rows = (await env.DB.prepare("SELECT instance_id, player_id FROM raid_placements WHERE event_id = ?1 ORDER BY instance_id, created_at").bind(ev).all<{ instance_id: string; player_id: string }>())
        .results;
    const by = new Map<string, string[]>();
    for (const r of rows) by.set(r.instance_id, [...(by.get(r.instance_id) ?? []), r.player_id]);
    const stmts: D1PreparedStatement[] = [];
    for (const [inst, ids] of by) {
        stmts.push(env.DB.prepare("UPDATE raid_instances SET started_at = ?2, players = ?3 WHERE id = ?1").bind(inst, iso(-25), ids.length));
        ids.forEach((p, seat) =>
            stmts.push(
                env.DB.prepare("INSERT INTO raid_instance_members (instance_id, player_id, seat, active, joined_at, left_at, seat_from) VALUES (?1, ?2, ?3, 1, ?4, NULL, 0)").bind(inst, p, seat, iso(-25)),
            ),
        );
    }
    for (let at = 0; at < stmts.length; at += 50) await env.DB.batch(stmts.slice(at, at + 50));
    return by;
}

function row(seat: number) {
    return { seat, from: 0, damage: 4000 + seat, boss_damage: 0, avoided: 600, blocks: 4, rescues: 1, vents: 0, weak_points: 0, objectives: 2, ticks: 60000, gigaboss: 0 };
}

const shuffled = <T>(xs: T[], seed: number) => {
    const a = [...xs];
    let s = seed;
    for (let i = a.length - 1; i > 0; --i) {
        s = (s * 1103515245 + 12345) & 0x7fffffff;
        const j = s % (i + 1);
        [a[i], a[j]] = [a[j]!, a[i]!];
    }
    return a;
};

describe("raids phase 7: four clans of 50 at once", () => {
    it(
        "registrations, placements, simultaneous objectives and tables, aggregation, claims and the close: nothing twice, nothing lost",
        async () => {
            const t0 = Date.now();
            const clans = await Promise.all(["AUBE", "NEBU", "ORIO", "VEGA"].map((t) => clanOf(t, 50)));
            const everyone = clans.flatMap((c) => c.players);
            const tokenOf = new Map(everyone.map((p) => [p.id, p.token]));
            const timings: Record<string, number> = { setup_ms: Date.now() - t0 };

            // 1. 200 registrations at once for 180 places (50 a clan at most): exactly 180, none twice.
            const ev = await worldEvent(clans, true, 180);
            let t = Date.now();
            const regs = await parallel(everyone.length, 50, (i) => api("POST", `${V}/raids/events/${ev}/register`, { token: everyone[i]!.token, body: {} }));
            timings.register_200_ms = Date.now() - t;
            expect(regs.filter((r) => r.status === 201).length).toBe(180);
            expect(regs.every((r) => r.status === 201 || r.status === 409)).toBe(true);
            expect(await count("SELECT COUNT(*) AS n FROM raid_registrations WHERE event_id = ?1", ev)).toBe(180);
            for (const c of clans) expect(await count("SELECT COUNT(*) AS n FROM raid_registrations WHERE event_id = ?1 AND clan_id = ?2", ev, c.id)).toBeLessThanOrEqual(50);
            const registered = everyone.filter((_, i) => regs[i]!.status === 201);

            // 2. The event starts; the 180 ask for their place at once: never a ninth in an instance.
            await running(ev);
            t = Date.now();
            const placed = await parallel(registered.length, 60, async (i) => {
                for (let attempt = 0; attempt < 8; ++attempt) {
                    const r = await api("POST", `${V}/raids/events/${ev}/join`, { token: registered[i]!.token, body: {} });
                    if (r.status === 200) return r.body.data.instance.id as string;
                    expect(r.body.error?.code, JSON.stringify(r.body)).toBe("INSTANCE_BUSY"); // « réessayez » (the game retries)
                }
                throw new Error("never placed");
            });
            timings.place_180_ms = Date.now() - t;
            const sizes = await env.DB.prepare("SELECT instance_id, COUNT(*) AS n FROM raid_placements WHERE event_id = ?1 GROUP BY instance_id").bind(ev).all<{ instance_id: string; n: number }>();
            expect(sizes.results.every((s) => s.n <= 8)).toBe(true);
            expect(await count("SELECT COUNT(*) AS n FROM raid_placements WHERE event_id = ?1", ev)).toBe(180);
            expect(new Set(placed).size).toBe(sizes.results.length);
            expect(sizes.results.length).toBeGreaterThanOrEqual(23); // 180 / 8: dozens of instances
            timings.instances = sizes.results.length;
            const members = await seatPlaced(ev);
            const branchOf = new Map(
                (await env.DB.prepare("SELECT id, branch FROM raid_instances WHERE event_id = ?1").bind(ev).all<{ id: string; branch: string }>()).results.map((r) => [r.id, r.branch]),
            );
            const startObjective = (branch: string) => {
                const c = CATALOGS.find((x) => x.id === branch) as Catalog;
                return c.objectives.find((o) => o.zone === c.start_zone && !o.optional)!.id;
            };

            // 3. Every player of every instance reports its branch's first objective at the same moment.
            const sends: { player: string; inst: string; body: Record<string, unknown> }[] = [];
            for (const [inst, ids] of members) for (const p of ids) sends.push({ player: p, inst, body: { kind: "objective", objective: startObjective(branchOf.get(inst)!), zone: 1, tick: TICK, hash: H1 } });
            const fire = (list: typeof sends) =>
                parallel(list.length, 60, (i) => api("POST", `${V}/raids/instances/${list[i]!.inst}/reports`, { token: tokenOf.get(list[i]!.player)!, body: list[i]!.body }));
            t = Date.now();
            const res = await fire(shuffled(sends, 7));
            timings.objective_reports_180_ms = Date.now() - t;
            expect(res.every((r) => r.status === 200 && ["accepted", "pending"].includes(r.body.data.status))).toBe(true);
            const branches = new Set(branchOf.values());
            // Once for the world per branch, once per instance for its own consensus, every duplicate journaled.
            expect(await count("SELECT COUNT(*) AS n FROM raid_branch_objectives WHERE event_id = ?1", ev)).toBe(branches.size);
            expect(await count("SELECT COUNT(*) AS n FROM raid_accepted a JOIN raid_instances i ON i.id = a.instance_id WHERE i.event_id = ?1 AND a.report_key LIKE 'o:%'", ev)).toBe(members.size);
            expect(await count("SELECT COUNT(*) AS n FROM raid_world_journal WHERE event_id = ?1 AND code = 'duplicate_objective'", ev)).toBe(members.size - branches.size);
            expect(await count("SELECT COUNT(*) AS n FROM raid_report_rejections j JOIN raid_instances i ON i.id = j.instance_id WHERE i.event_id = ?1", ev)).toBe(0);

            // 4. Every instance's table of contributions, at once, while the world is read.
            const tables: typeof sends = [];
            for (const [inst, ids] of members) for (const p of ids) tables.push({ player: p, inst, body: { kind: "result", zone: 1, tick: TICK, hash: H1, records: ids.map((_, s) => row(s)) } });
            t = Date.now();
            const [tres] = await Promise.all([fire(shuffled(tables, 11)), aggregate(env.DB, ev), aggregate(env.DB, ev)]);
            timings.table_reports_180_ms = Date.now() - t;
            expect(tres.every((r) => r.status === 200 && r.body.data.status !== "rejected")).toBe(true);
            expect(await count("SELECT COUNT(*) AS n FROM raid_contributions WHERE event_id = ?1", ev)).toBe(180);
            t = Date.now();
            const agg = await aggregate(env.DB, ev);
            timings.aggregate_ms = Date.now() - t;
            expect(agg.players.length).toBe(180);
            expect(agg.groups.length).toBe(members.size);
            expect(agg.clans.reduce((n, c) => n + c.points, 0)).toBe(agg.players.reduce((n, p) => n + p.score, 0));
            expect(agg.world.objectives_done).toBe(branches.size);

            // 5. Everything again, in another order (retries, reconnections): the same aggregate.
            const again = await fire(shuffled([...sends, ...tables], 3));
            expect(again.every((r) => r.status === 200 && r.body.data.status === "accepted")).toBe(true);
            expect(await aggregate(env.DB, ev)).toEqual(agg);
            expect(await count("SELECT COUNT(*) AS n FROM raid_contributions WHERE event_id = ?1", ev)).toBe(180);

            // 6. Rewards granted once each; everybody claims twice at the same time.
            const grants = await count("SELECT COUNT(*) AS n FROM raid_reward_grants WHERE event_id = ?1", ev);
            expect(await count("SELECT COUNT(*) AS n FROM raid_reward_grants WHERE event_id = ?1 AND reward = 'asterion_participation'", ev)).toBe(180);
            expect(await count("SELECT COUNT(*) AS n FROM raid_reward_grants WHERE event_id = ?1 AND reward = 'asterion_ecorce'", ev)).toBe(180);
            t = Date.now();
            const claims = await parallel(registered.length * 2, 60, (i) => api("POST", `${V}/raids/rewards/claim`, { token: registered[i % registered.length]!.token, body: {} }));
            timings.claims_360_ms = Date.now() - t;
            expect(claims.every((r) => r.status === 200)).toBe(true);
            expect(claims.reduce((n, r) => n + (r.body.data.claimed as number), 0)).toBe(grants);
            expect(await count("SELECT COUNT(*) AS n FROM raid_reward_grants WHERE event_id = ?1 AND claimed_at IS NULL", ev)).toBe(0);
            const won = await env.DB.prepare("SELECT player_id, reward_ref, COUNT(*) AS n FROM player_won_rewards WHERE challenge_id = ?1 GROUP BY player_id, reward_ref HAVING n > 1")
                .bind(`raid:${ev}`)
                .all();
            expect(won.results).toEqual([]);

            // 7. The close: three crons and two readers at once: frozen once, rewards once, told once.
            await env.DB.prepare("UPDATE raid_events SET starts_at = ?2, lock_at = ?3, ends_at = ?4 WHERE id = ?1").bind(ev, iso(-100), iso(-130), iso(-15)).run();
            const reader = registered[0]!;
            t = Date.now();
            await Promise.all([
                worldTick(env.DB),
                worldTick(env.DB),
                finalizeEvent(env.DB, ev),
                api("GET", `${V}/raids/events/${ev}/world`, { token: reader.token }),
                api("GET", `${V}/raids/events/${ev}/world`, { token: registered[1]!.token }),
                worldTick(env.DB),
            ]);
            timings.close_ms = Date.now() - t;
            expect(await count("SELECT COUNT(*) AS n FROM raid_event_results WHERE event_id = ?1", ev)).toBe(1);
            expect(await count("SELECT COUNT(*) AS n FROM raid_world_journal WHERE event_id = ?1 AND once_key = 'close'", ev)).toBe(1);
            const told = await env.DB.prepare("SELECT player_id, COUNT(*) AS n FROM notifications WHERE group_key = ?1 GROUP BY player_id HAVING n > 1").bind(`raid:results:${ev}`).all();
            expect(told.results).toEqual([]);
            expect(await count("SELECT COUNT(*) AS n FROM notifications WHERE group_key = ?1", `raid:results:${ev}`)).toBeGreaterThanOrEqual(180);
            const titles = await count("SELECT COUNT(*) AS n FROM raid_clan_titles WHERE event_id = ?1", ev);
            expect(titles).toBe(4); // the four clans are in the top 10
            // A report after the freeze: refused, nothing written.
            const [firstInst, firstIds] = [...members.entries()][0]!;
            const late = await api("POST", `${V}/raids/instances/${firstInst}/reports`, { token: tokenOf.get(firstIds[0]!)!, body: { kind: "zone", zone: 1, tick: TICK + 10, hash: H1 } });
            expect(late.body.data.status).toBe("rejected");
            expect(late.body.data.reason).toContain("figés");
            console.log(JSON.stringify({ raid_load: timings }));
        },
        240_000,
    );
});

// ------------------------------------------------------------------ bounded work per request

/** `n` instances of one clan on the right arm, `per` members in play each, started 25 min ago. */
async function smallWorld(n: number, per: number): Promise<{ ev: string; inst: string[]; players: TestPlayer[][] }> {
    const chief = await registerPlayer(`Q${n}Chef`);
    const r = await api("POST", `${V}/clans`, { token: chief.token, body: { name: `Clan Q${n}`, tag: `Q${n}`, motto: "Ensemble.", emblem: EMBLEM, join_policy: "open" } });
    const clanId = r.body.data.clan.id as string;
    const others = await parallel(n * per - 1, 25, (i) => registerPlayer(`Q${n}M${i + 1}`));
    const all = [chief, ...others];
    await parallel(others.length, 25, async (i) => void (await api("POST", `${V}/clans/${clanId}/join`, { token: others[i]!.token, body: {} })));
    const ev = await worldEvent([{ id: clanId, tag: `Q${n}`, players: all }], false);
    const now = new Date().toISOString();
    const stmts: D1PreparedStatement[] = [];
    const inst: string[] = [];
    const players: TestPlayer[][] = [];
    for (let g = 0; g < n; ++g) {
        const id = crypto.randomUUID();
        inst.push(id);
        players.push(all.slice(g * per, (g + 1) * per));
        stmts.push(
            env.DB.prepare(
                "INSERT INTO raid_instances (id, event_id, clan_id, group_no, branch, zone, players, started_at, created_at, updated_at) VALUES (?1, ?2, ?3, ?4, 'asterion_bras_droit', 1, ?5, ?6, ?7, ?7)",
            ).bind(id, ev, clanId, g + 1, per, iso(-25), now),
        );
        players[g]!.forEach((p, seat) =>
            stmts.push(
                env.DB.prepare("INSERT INTO raid_instance_members (instance_id, player_id, seat, active, joined_at, left_at, seat_from) VALUES (?1, ?2, ?3, 1, ?4, NULL, 0)").bind(id, p.id, seat, now),
            ),
        );
    }
    for (const p of all) stmts.push(env.DB.prepare("INSERT INTO raid_registrations (event_id, player_id, clan_id, created_at) VALUES (?1, ?2, ?3, ?4)").bind(ev, p.id, clanId, now));
    for (let at = 0; at < stmts.length; at += 50) await env.DB.batch(stmts.slice(at, at + 50));
    return { ev, inst, players };
}

/** The D1 round trips of one report called directly (and the statements it ran). */
async function reportCost(p: TestPlayer, inst: string, body: Record<string, unknown>) {
    const { db, stats } = countingDb(env.DB);
    const { ctx, waits } = directCtx({ method: "POST", path: `${V}/raids/instances/${inst}/reports`, params: { instanceId: inst }, token: p.token, body, db });
    const t = Date.now();
    const data = await bodyOf(await report(ctx));
    const ms = Date.now() - t;
    await Promise.allSettled(waits);
    return { status: data.status as string, calls: stats.calls, queries: stats.queries, ms };
}

describe("raids phase 7: bounded work per request", () => {
    it(
        "a report, the aggregation, the world view and a claim: the same D1 round trips with 2 instances or 30; no full scan of a raid table",
        async () => {
            const recorded: Recorded[] = [];
            const measure: Record<string, number[]> = {};
            let last: Awaited<ReturnType<typeof smallWorld>> | undefined;
            for (const n of [2, 30]) {
                const w = await smallWorld(n, 3);
                last = w;
                const [a, b, c] = w.players[0]! as [TestPlayer, TestPlayer, TestPlayer];
                // Every other instance already validated the objective: the duplicate path (journal, fan-out to 30 rooms).
                for (let g = 1; g < n; ++g)
                    await env.DB.prepare("INSERT INTO raid_accepted (instance_id, report_key, value, accepted_at) VALUES (?1, 'o:epaule_arene', ?2, ?3)").bind(w.inst[g], `1:${TICK}:${H1}`, iso(-1)).run();
                await env.DB.prepare("INSERT INTO raid_branch_objectives (event_id, branch, objective_id, instance_id, accepted_at) VALUES (?1, 'asterion_bras_droit', 'epaule_arene', ?2, ?3)").bind(w.ev, w.inst[1], iso(-1)).run();
                const body = { kind: "objective", objective: "epaule_arene", zone: 1, tick: TICK, hash: H1 };
                const pending = await reportCost(a, w.inst[0]!, body);
                const accepted = await reportCost(b, w.inst[0]!, body);
                const repeat = await reportCost(c, w.inst[0]!, body);
                expect([pending.status, accepted.status, repeat.status]).toEqual(["pending", "accepted", "accepted"]);
                const table = await reportCost(a, w.inst[0]!, { kind: "result", zone: 1, tick: TICK, hash: H1, records: [row(0), row(1), row(2)] });
                await reportCost(b, w.inst[0]!, { kind: "result", zone: 1, tick: TICK, hash: H1, records: [row(0), row(1), row(2)] });
                const zone = await reportCost(a, w.inst[0]!, { kind: "zone", zone: 2, tick: TICK, hash: H1 });
                recorded.push(...pending.queries, ...accepted.queries, ...table.queries, ...zone.queries);
                const agg = countingDb(env.DB);
                const ta = Date.now();
                await aggregate(agg.db, w.ev);
                const aggMs = Date.now() - ta;
                const view = countingDb(env.DB);
                const v = directCtx({ method: "GET", path: `${V}/raids/events/${w.ev}/world`, params: { eventId: w.ev }, token: a.token, db: view.db });
                expect((await worldView(v.ctx)).status).toBe(200);
                const cl = countingDb(env.DB);
                const k = directCtx({ method: "POST", path: `${V}/raids/rewards/claim`, params: {}, token: a.token, body: {}, db: cl.db });
                expect((await claim(k.ctx)).status).toBe(200);
                recorded.push(...agg.stats.queries, ...view.stats.queries, ...cl.stats.queries);
                const put = (key: string, v: number) => (measure[key] = [...(measure[key] ?? []), v]);
                put("report_pending", pending.calls);
                put("report_accepted", accepted.calls);
                put("report_repeat", repeat.calls);
                put("report_table", table.calls);
                put("aggregate", agg.stats.calls);
                put("world_view", view.stats.calls);
                put("claim", cl.stats.calls);
                put(`ms_accepted_${n}`, accepted.ms);
                put(`ms_aggregate_${n}`, aggMs);
            }
            // The other hot paths of a raid, in the last (larger) world: the maps, a place, the live
            // panel, the minute's cron and the close.
            {
                const w = last!;
                const p = w.players[1]![0]!;
                const admin = w.players[2]![0]!;
                await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(admin.id).run();
                const calls: [string, (db: D1Database) => Promise<unknown>][] = [
                    ["titan", (db) => titanView(directCtx({ method: "GET", path: "/", params: { eventId: w.ev }, token: p.token, db }).ctx)],
                    ["branch_map", (db) => raidMapBranch(directCtx({ method: "GET", path: "/", params: { eventId: w.ev, part: "bras_droit" }, token: p.token, db }).ctx)],
                    ["branch_zones", (db) => branchView(directCtx({ method: "GET", path: "/", params: { eventId: w.ev }, token: p.token, db }).ctx)],
                    ["join", (db) => join(directCtx({ method: "POST", path: "/", params: { eventId: w.ev }, token: p.token, body: {}, db }).ctx)],
                    ["live", (db) => adminLive(directCtx({ method: "GET", path: "/", params: { eventId: w.ev }, token: admin.token, db }).ctx)],
                    ["cron_world", (db) => worldTick(db)],
                    ["cron_tops", (db) => grantEndedTops(db, new Date().toISOString())],
                    ["cron_reminders", (db) => raidTick(db)],
                ];
                for (const [name, call] of calls) {
                    const c = countingDb(env.DB);
                    await call(c.db);
                    recorded.push(...c.stats.queries);
                    measure[name] = [c.stats.calls];
                }
                await env.DB.prepare("UPDATE raid_events SET starts_at = ?2, lock_at = ?3, ends_at = ?4 WHERE id = ?1").bind(w.ev, iso(-100), iso(-130), iso(-15)).run();
                const c = countingDb(env.DB);
                expect(await finalizeEvent(c.db, w.ev)).toBe(true);
                recorded.push(...c.stats.queries);
                measure.close = [c.stats.calls];
            }
            console.log(JSON.stringify({ raid_round_trips: measure }));
            // Independent of the number of instances (before 2.74 the accepted report woke the 30 rooms one after the other inside the request).
            for (const key of ["report_pending", "report_accepted", "report_repeat", "report_table", "aggregate", "world_view", "claim"]) {
                const [small, large] = measure[key]!;
                expect(large, key).toBe(small);
            }
            expect(measure.aggregate![0]).toBe(1); // one batch
            expect(measure.report_accepted![0]).toBeLessThanOrEqual(20);
            // Every hot statement uses an index (migration 0149 added the two that were missing).
            expect(await fullScans(recorded, RAID_TABLES)).toEqual([]);
        },
        240_000,
    );
});
