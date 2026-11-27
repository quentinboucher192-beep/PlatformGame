// ADMINISTRATION › TAS (tas.ts): the leases of the trainings run on the administrators' own
// computers (one administrator per level and category, a heartbeat, an expiry, a release), the
// training time measured by the server, the publication of a run (everything the server can
// check without playing it), the public list and files. The routes of /admin/tas do not exist
// for a player.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { MAX_CLOUD_FINISHES_PER_WINDOW, MAX_CLOUD_POINTS, MAX_CLOUD_SAMPLE_PER_CHECKPOINT, MAX_CURVE_POINTS, MAX_CURVE_WINDOWS, thinCloud } from "../src/tas";
import { LEVEL_HASH, LEVEL_HASH_V2, api, buildReplay, publishV2, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const SIM = 19;

async function makeAdmin(p: TestPlayer): Promise<void> {
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
}

async function admins(): Promise<[TestPlayer, TestPlayer]> {
    const a = await registerPlayer("ada");
    const b = await registerPlayer("bob");
    await makeAdmin(a);
    await makeAdmin(b);
    return [a, b];
}

function record(overrides: Record<string, unknown> = {}): string {
    return JSON.stringify(
        {
            format: "cosmic.tas",
            level: "test_01",
            levelVersion: 1,
            levelHash: LEVEL_HASH,
            simVersion: SIM,
            category: "ANY_PERCENT",
            timeMs: 41250,
            splits: [{ checkpoint: "cp_1", timeMs: 8125 }],
            explored: 1200,
            computedAt: "2026-11-27",
            tool: "cosmic_tas 1 / 2.93.0 / sim 19",
            routeIntervalMs: 250,
            route: [
                [100, 500],
                [131.5, 500],
            ],
            deaths: 0,
            damageTaken: 1,
            collectibles: 4,
            totalCollectibles: 10,
            replay: "ANY_PERCENT.cprz",
            ...overrides,
        },
        null,
        1,
    );
}

async function replay(timeMs: number, opts: { hash?: string; sim?: number; levelId?: string } = {}): Promise<string> {
    const bytes = await buildReplay({
        attemptId: crypto.randomUUID(),
        durationMs: timeMs,
        exact: true,
        inputFormat: 3,
        levelId: opts.levelId,
        levelHash: opts.hash ?? LEVEL_HASH,
        extraEvents: [{ frame: 0, type: 20, arg: 0, value: opts.sim ?? SIM }],
    });
    let bin = "";
    for (const b of bytes) bin += String.fromCharCode(b);
    return btoa(bin);
}

const lease = (p: TestPlayer, cat = "ANY_PERCENT", level = "test_01", hash = LEVEL_HASH, sim = SIM) =>
    api("POST", `${V}/admin/tas/${level}/${cat}/lease`, { token: p.token, body: { level_hash: hash, sim_version: sim } });
const beat = (p: TestPlayer, id: string, explored: number, cat = "ANY_PERCENT") =>
    api("POST", `${V}/admin/tas/test_01/${cat}/lease/heartbeat`, { token: p.token, body: { lease_id: id, explored } });
const release = (p: TestPlayer, id: string, explored: number, cat = "ANY_PERCENT") =>
    api("POST", `${V}/admin/tas/test_01/${cat}/lease/release`, { token: p.token, body: { lease_id: id, explored } });
const publish = (p: TestPlayer, body: Record<string, unknown>, cat = "ANY_PERCENT", level = "test_01") =>
    api("POST", `${V}/admin/tas/${level}/${cat}/publish`, { token: p.token, body });

/** Moves the lease's clock back by `ms` (its last heartbeat and its expiry), as if that time had passed. */
async function age(ms: number, cat = "ANY_PERCENT"): Promise<void> {
    await env.DB.prepare("UPDATE tas_training SET lease_beat_ms = lease_beat_ms - ?1, lease_expires_ms = lease_expires_ms - ?1 WHERE level_id = 'test_01' AND category = ?2")
        .bind(ms, cat)
        .run();
}

describe("TAS: the routes are an administrator's", () => {
    it("does not exist for a player, nor without a session; the list and files are public", async () => {
        const p = await registerPlayer("player1");
        for (const [method, path] of [
            ["GET", `${V}/admin/tas`],
            ["POST", `${V}/admin/tas/test_01/ANY_PERCENT/lease`],
            ["POST", `${V}/admin/tas/test_01/ANY_PERCENT/lease/heartbeat`],
            ["POST", `${V}/admin/tas/test_01/ANY_PERCENT/lease/release`],
            ["POST", `${V}/admin/tas/test_01/ANY_PERCENT/publish`],
            ["GET", `${V}/admin/tas/test_01/ANY_PERCENT/curve`],
        ] as const) {
            const r = await api(method, path, { token: p.token, body: method === "POST" ? {} : undefined });
            expect(r.status).toBe(404);
            const anon = await api(method, path, { body: method === "POST" ? {} : undefined });
            expect(anon.status).toBe(401);
        }
        const list = await api("GET", `${V}/tas`);
        expect(list.status).toBe(200);
        expect(list.body.data.runs).toEqual([]);
    });

    it("an administrator whose role was removed loses the routes at once", async () => {
        const [a] = await admins();
        expect((await lease(a)).status).toBe(200);
        await env.DB.prepare("UPDATE players SET role = 'player' WHERE id = ?1").bind(a.id).run();
        expect((await api("GET", `${V}/admin/tas`, { token: a.token })).status).toBe(404);
    });

    it("refuses an unknown category or a malformed level id", async () => {
        const [a] = await admins();
        expect((await lease(a, "SPEEDRUN")).status).toBe(404);
        expect((await lease(a, "ANY_PERCENT", "Test-01")).status).toBe(404);
    });
});

describe("TAS: the lease of a level and a category", () => {
    it("acquire, conflict for another administrator, release, then free for him", async () => {
        const [a, b] = await admins();
        const got = await lease(a);
        expect(got.status).toBe(200);
        expect(got.body.data.lease_id).toMatch(/^[0-9a-f-]{36}$/);
        expect(got.body.data.ttl_s).toBe(180);
        expect(got.body.data.heartbeat_s).toBe(60);
        // Bob is told who trains it, and cannot start.
        const other = await lease(b);
        expect(other.status).toBe(409);
        expect(other.body.error.code).toBe("TAS_LOCKED");
        expect(other.body.error.details.holder.username).toBe("ada");
        // Another category of the same level is another lock.
        expect((await lease(b, "NO_DEATH")).status).toBe(200);
        // The list says it, for both.
        const listB = await api("GET", `${V}/admin/tas`, { token: b.token });
        const pair = listB.body.data.pairs.find((x: any) => x.level_id === "test_01" && x.category === "ANY_PERCENT");
        expect(pair.trainings[0].level_hash).toBe(LEVEL_HASH);
        expect(pair.trainings[0].lease.holder.username).toBe("ada");
        expect(pair.trainings[0].lease.mine).toBe(false);
        const listA = await api("GET", `${V}/admin/tas`, { token: a.token });
        expect(listA.body.data.pairs.find((x: any) => x.category === "ANY_PERCENT").trainings[0].lease.mine).toBe(true);
        // Released: free at once.
        const rel = await release(a, got.body.data.lease_id, 10);
        expect(rel.status).toBe(200);
        expect(rel.body.data.released).toBe(true);
        expect((await lease(b)).status).toBe(200);
        // A lease given back cannot be kept alive.
        expect((await beat(a, got.body.data.lease_id, 20)).status).toBe(409);
    });

    it("expires without a heartbeat: the pair is free again (a computer turned off or crashed)", async () => {
        const [a, b] = await admins();
        const got = await lease(a);
        await age(181_000);
        const lost = await beat(a, got.body.data.lease_id, 100);
        expect(lost.status).toBe(409);
        expect(lost.body.error.code).toBe("LEASE_LOST");
        const list = await api("GET", `${V}/admin/tas`, { token: b.token });
        expect(list.body.data.pairs[0].trainings[0].lease).toBeNull();
        const taken = await lease(b);
        expect(taken.status).toBe(200);
        // Ada's old lease is no longer hers to release.
        expect((await release(a, got.body.data.lease_id, 100)).status).toBe(409);
    });

    it("a heartbeat keeps it; the server counts the time between two of them, and the attempts within bounds", async () => {
        const [a] = await admins();
        const got = await lease(a);
        const id = got.body.data.lease_id;
        await age(60_000);
        const b1 = await beat(a, id, 500_000);
        expect(b1.status).toBe(200);
        // About a minute measured by the server (never what the game says).
        expect(b1.body.data.training.total_ms).toBeGreaterThanOrEqual(60_000);
        expect(b1.body.data.training.total_ms).toBeLessThan(70_000);
        expect(b1.body.data.training.total_explored).toBe(500_000);
        // Absurd numbers are bounded by the time: 50 000 attempts a second at most.
        await age(10_000);
        const b2 = await beat(a, id, 1e12);
        expect(b2.status).toBe(200);
        expect(b2.body.data.training.total_explored).toBeLessThanOrEqual(500_000 + 50_000 * 11);
        // A long silence counts at most the lease's life (then it has expired).
        await age(170_000);
        const b3 = await beat(a, id, 1e12);
        expect(b3.status).toBe(200);
        expect(b3.body.data.training.total_ms).toBeLessThanOrEqual(70_000 + 11_000 + 180_000);
        const rel = await release(a, id, 1e12);
        expect(rel.status).toBe(200);
        // The totals stay for the next session, of anyone.
        const list = await api("GET", `${V}/admin/tas`, { token: a.token });
        expect(list.body.data.pairs[0].trainings[0].sessions).toBe(1);
        expect(list.body.data.pairs[0].trainings[0].total_ms).toBe(rel.body.data.training.total_ms);
    });

    it("two administrators at once: one wins", async () => {
        const [a, b] = await admins();
        const [ra, rb] = await Promise.all([lease(a), lease(b)]);
        expect([ra.status, rb.status].sort()).toEqual([200, 409]);
    });

    it("the same administrator takes his own lease again (another computer, a restart)", async () => {
        const [a] = await admins();
        const first = await lease(a);
        const second = await lease(a);
        expect(second.status).toBe(200);
        expect(second.body.data.lease_id).not.toBe(first.body.data.lease_id);
        expect((await beat(a, first.body.data.lease_id, 1)).status).toBe(409);
    });

    it("refuses a malformed heartbeat", async () => {
        const [a] = await admins();
        const got = await lease(a);
        expect((await beat(a, "nope", 1)).status).toBe(400);
        expect((await beat(a, got.body.data.lease_id, -5)).status).toBe(400);
    });
});

describe("TAS: publication", () => {
    it("publishes a run: R2 and D1, the totals, who and when; the public list and files", async () => {
        const [a] = await admins();
        const got = await lease(a);
        await age(30_000);
        await beat(a, got.body.data.lease_id, 9000);
        const rec = record();
        const res = await publish(a, { record: rec, replay: await replay(41250) });
        expect(res.status).toBe(201);
        expect(res.body.data.published.revision).toBe(1);
        expect(res.body.data.published.time_ms).toBe(41250);
        expect(res.body.data.published.published_by.username).toBe("ada");
        expect(res.body.data.published.training_ms_at_publish).toBeGreaterThanOrEqual(30_000);
        expect(res.body.data.published.explored_at_publish).toBe(9000);
        const list = await api("GET", `${V}/tas`);
        expect(list.body.data.runs).toHaveLength(1);
        const run = list.body.data.runs[0];
        expect(run.level_hash).toBe(LEVEL_HASH);
        expect(run.sim_version).toBe(SIM);
        expect(run.record_path).toBe(`${V}/tas/test_01/ANY_PERCENT/1/record`);
        expect(run.current).toBe(true);
        expect(run.published_by).toBeUndefined(); // the public list names nobody
        const file = await api("GET", run.record_path);
        expect(file.status).toBe(200);
        expect(file.body).toEqual(JSON.parse(rec));
        expect(file.headers.get("x-content-sha256")).toBe(run.record_sha256);
        const rep = await api("GET", run.replay_path);
        expect(rep.status).toBe(200);
        expect(rep.headers.get("x-content-sha256")).toBe(run.replay_sha256);
        expect((await api("GET", `${V}/tas/test_01/ANY_PERCENT/2/record`)).status).toBe(404);
        // The Journal has it.
        const log = await api("GET", `${V}/admin/log?kind=tas`, { token: a.token });
        expect(log.status).toBe(200);
        expect(JSON.stringify(log.body.data)).toContain("admin_tas_published");
        // Only a faster run replaces it; the old files are no longer served.
        const same = await publish(a, { record: rec, replay: await replay(41250) });
        expect(same.status).toBe(409);
        expect(same.body.error.code).toBe("NOT_FASTER");
        const faster = await publish(a, { record: record({ timeMs: 40000 }), replay: await replay(40000) });
        expect(faster.status).toBe(201);
        expect(faster.body.data.published.revision).toBe(2);
        // The list gives the new one; the old files are kept (never deleted).
        const after = await api("GET", `${V}/tas`);
        expect(after.body.data.runs).toHaveLength(1);
        expect(after.body.data.runs[0].revision).toBe(2);
        expect((await api("GET", `${V}/tas/test_01/ANY_PERCENT/1/record`)).status).toBe(200);
        expect((await api("GET", `${V}/tas/test_01/ANY_PERCENT/2/replay`)).status).toBe(200);
    });

    it("validates everything it can: format, level, category, version of the level, rules, replay header", async () => {
        const [a] = await admins();
        const rep = await replay(41250);
        const expectCode = async (body: Record<string, unknown>, code: string, cat = "ANY_PERCENT") => {
            const r = await publish(a, body, cat);
            if (r.body?.error?.code !== code) console.log(JSON.stringify(r.body));
            expect(r.body.error.code).toBe(code);
        };
        await expectCode({ record: "{nope", replay: rep }, "TAS_INVALID");
        await expectCode({ record: record({ format: "cosmic.other" }), replay: rep }, "TAS_INVALID");
        await expectCode({ record: record({ level: "test_02" }), replay: rep }, "TAS_INVALID");
        await expectCode({ record: record({ category: "NO_DEATH" }), replay: rep }, "TAS_INVALID");
        await expectCode({ record: record({ timeMs: 0 }), replay: rep }, "TAS_INVALID");
        await expectCode({ record: record({ replay: "../x.cprz" }), replay: rep }, "TAS_INVALID");
        await expectCode({ record: record({ route: [[1, "x"]] }), replay: rep }, "TAS_INVALID");
        // The category's rules.
        await expectCode({ record: record({ category: "NO_DAMAGE", replay: "NO_DAMAGE.cprz" }), replay: rep }, "TAS_INVALID", "NO_DAMAGE");
        await expectCode({ record: record({ category: "HUNDRED_PERCENT", replay: "HUNDRED_PERCENT.cprz" }), replay: rep }, "TAS_INVALID", "HUNDRED_PERCENT");
        // The replay against the record: its time, its level's fingerprint, its simulation, exact.
        await expectCode({ record: record(), replay: await replay(41000) }, "TAS_REPLAY_INVALID");
        await expectCode({ record: record(), replay: await replay(41250, { sim: 18 }) }, "TAS_REPLAY_INVALID");
        await expectCode({ record: record(), replay: await replay(41250, { hash: "c".repeat(64) }) }, "TAS_REPLAY_INVALID");
        await expectCode({ record: record(), replay: await replay(41250, { levelId: "test_02" }) }, "TAS_REPLAY_INVALID");
        await expectCode({ record: record(), replay: "%%%" }, "VALIDATION_ERROR");
        await expectCode({ record: record(), replay: btoa("CPRZ garbage") }, "TAS_REPLAY_INVALID");
        // The level as the server knows it: its current revision's fingerprint and collectibles.
        const otherHash = "c".repeat(64);
        await expectCode({ record: record({ levelHash: otherHash }), replay: await replay(41250, { hash: otherHash }) }, "LEVEL_VERSION_OUTDATED");
        await expectCode({ record: record({ totalCollectibles: 11 }), replay: rep }, "LEVEL_VERSION_MISMATCH");
        // Too big.
        const big = await publish(a, { record: record({ tool: "x".repeat(2_000_000) }), replay: rep });
        expect(big.status).toBeGreaterThanOrEqual(400);
        // Nothing was published.
        expect((await api("GET", `${V}/tas`)).body.data.runs).toEqual([]);
    });

    it("a level the server does not know is accepted on what it can check", async () => {
        const [a] = await admins();
        const h = "9".repeat(64);
        const r = await publish(
            a,
            { record: record({ level: "unknown_07", levelHash: h }), replay: await replay(41250, { hash: h, levelId: "unknown_07" }) },
            "ANY_PERCENT",
            "unknown_07",
        );
        expect(r.status).toBe(201);
    });

    it("refused while another administrator trains the pair", async () => {
        const [a, b] = await admins();
        await lease(b);
        const r = await publish(a, { record: record(), replay: await replay(41250) });
        expect(r.status).toBe(409);
        expect(r.body.error.code).toBe("TAS_LOCKED");
        // Its holder publishes.
        expect((await publish(b, { record: record(), replay: await replay(41250) })).status).toBe(201);
    });

    it("a run of an older simulation never replaces a newer one of the same level", async () => {
        const [a] = await admins();
        expect((await publish(a, { record: record(), replay: await replay(41250) })).status).toBe(201);
        const old = await publish(a, { record: record({ simVersion: 18, timeMs: 30000 }), replay: await replay(30000, { sim: 18 }) });
        expect(old.status).toBe(409);
        expect(old.body.error.code).toBe("SIM_OUTDATED");
    });
});

describe("TAS: when a level changes (a new version: its fingerprint, or the simulation)", () => {
    it("the old version's publication is refused with a clear error, the new one accepted; the old run stays as an archive", async () => {
        const [a] = await admins();
        expect((await publish(a, { record: record(), replay: await replay(41250) })).status).toBe(201);
        // The level is edited (revision 2, another fingerprint).
        await publishV2();
        const old = await publish(a, { record: record({ timeMs: 39000 }), replay: await replay(39000) });
        expect(old.status).toBe(409);
        expect(old.body.error.code).toBe("LEVEL_VERSION_OUTDATED");
        expect(old.body.error.message).toContain("révision 2");
        expect(old.body.error.details.level_hash).toBe(LEVEL_HASH_V2);
        // Training the old version is refused too; the new one is a new lease (and new totals).
        const oldLease = await lease(a);
        expect(oldLease.status).toBe(409);
        expect(oldLease.body.error.code).toBe("LEVEL_VERSION_OUTDATED");
        const got = await lease(a, "ANY_PERCENT", "test_01", LEVEL_HASH_V2);
        expect(got.status).toBe(200);
        expect(got.body.data.training.total_ms).toBe(0);
        expect(got.body.data.training.level_hash).toBe(LEVEL_HASH_V2);
        // Slower than the old version's run: not comparable, accepted.
        const v2 = await publish(a, { record: record({ levelVersion: 2, levelHash: LEVEL_HASH_V2, timeMs: 50000 }), replay: await replay(50000, { hash: LEVEL_HASH_V2 }) });
        expect(v2.status).toBe(201);
        expect(v2.body.data.published.revision).toBe(2);
        // The admin's list: the current version's run, the old one apart.
        const list = await api("GET", `${V}/admin/tas`, { token: a.token });
        const pair = list.body.data.pairs[0];
        expect(pair.current_hash).toBe(LEVEL_HASH_V2);
        expect(pair.published.level_hash).toBe(LEVEL_HASH_V2);
        expect(pair.published.time_ms).toBe(50000);
        expect(pair.archive.level_hash).toBe(LEVEL_HASH);
        expect(pair.archive.time_ms).toBe(41250);
        expect(pair.trainings).toHaveLength(1);
        // The public list: each version's latest run, the current one marked; the old files stay.
        const pub = await api("GET", `${V}/tas`);
        expect(pub.body.data.runs).toHaveLength(2);
        expect(pub.body.data.runs.find((r: any) => r.current).level_hash).toBe(LEVEL_HASH_V2);
        expect((await api("GET", `${V}/tas/test_01/ANY_PERCENT/1/replay`)).status).toBe(200);
    });

    it("a lease is per version: the same pair at two versions are two locks", async () => {
        const [a, b] = await admins();
        const h = "9".repeat(64);
        // A level the server does not know: any version can be trained.
        expect((await lease(a, "ANY_PERCENT", "unknown_07", h)).status).toBe(200);
        expect((await lease(b, "ANY_PERCENT", "unknown_07", h)).status).toBe(409);
        expect((await lease(b, "ANY_PERCENT", "unknown_07", "8".repeat(64))).status).toBe(200);
        expect((await lease(b, "ANY_PERCENT", "unknown_07", h, SIM + 1)).status).toBe(200);
    });
});

describe("TAS: the training curve (best times and attempts sent with the lease)", () => {
    const point = (time_ms: number, age_ms = 0, explored = 0, kind = "improvement") => ({ kind, time_ms, age_ms, explored });
    const win = (o: Record<string, unknown> = {}) => ({
        age_ms: 0,
        duration_ms: 5000,
        round: 1,
        section: 2,
        sections: 3,
        attempts: 1000,
        finished: 10,
        best_ms: 41000,
        median_ms: 42000,
        worst_ms: 50000,
        route_median: 400,
        route_max: 900,
        stopped: [100, 800, 90],
        broken: [10, 50, 0],
        ...o,
    });
    const beatWith = (p: TestPlayer, id: string, explored: number, extra: Record<string, unknown>, path = "heartbeat") =>
        api("POST", `${V}/admin/tas/test_01/ANY_PERCENT/lease/${path}`, { token: p.token, body: { lease_id: id, explored, ...extra } });
    const curveOf = async (p: TestPlayer) => {
        const list = await api("GET", `${V}/admin/tas`, { token: p.token });
        const pair = list.body.data.pairs.find((x: any) => x.level_id === "test_01" && x.category === "ANY_PERCENT");
        return pair?.curves?.find((c: any) => c.level_hash === LEVEL_HASH && c.sim_version === SIM);
    };

    it("places each best time on the server's own training time, keeps only improvements, and every administrator sees it", async () => {
        const [a, b] = await admins();
        const got = await lease(a);
        const id = got.body.data.lease_id;
        await age(60_000);
        // Two improvements found 40 s and 10 s ago; a third one slower than the second: refused.
        const r = await beatWith(a, id, 30_000, { curve: [point(41_250, 40_000, 5_000, "start"), point(41_000, 10_000, 20_000), point(41_100, 5_000, 25_000)] });
        expect(r.status).toBe(200);
        expect(r.body.data.curve.refused).toBe(1);
        const added = r.body.data.curve.points;
        expect(added.map((x: any) => x.time_ms)).toEqual([41_250, 41_000]);
        const total = r.body.data.training.total_ms;
        // Within this heartbeat's minute, on the server's measure (never what the game says the total is).
        expect(added[0].training_ms).toBe(total - 40_000);
        expect(added[1].training_ms).toBe(total - 10_000);
        expect(added[1].explored).toBe(r.body.data.training.total_explored - 10_000);
        expect(added[0].by.username).toBe("ada");
        expect(added[0].session).toBe(1);
        // An age older than the call: at its start, never before.
        await age(30_000);
        const old = await beatWith(a, id, 40_000, { curve: [point(40_500, 3_600_000, 30_000)] });
        expect(old.body.data.curve.points[0].training_ms).toBe(total);
        // Equal to the best: not an improvement.
        const same = await beatWith(a, id, 41_000, { curve: [point(40_500, 0, 41_000)] });
        expect(same.body.data.curve.refused).toBe(1);
        // Bob sees Ada's curve; the release brings the last one.
        const rel = await beatWith(a, id, 42_000, { curve: [point(40_400, 0, 42_000)] }, "release");
        expect(rel.status).toBe(200);
        expect(rel.body.data.curve.points).toHaveLength(1);
        const curve = await curveOf(b);
        expect(curve.points.map((x: any) => x.time_ms)).toEqual([41_250, 41_000, 40_500, 40_400]);
        expect(curve.points[0].kind).toBe("start");
        for (let i = 1; i < curve.points.length; ++i) expect(curve.points[i].training_ms).toBeGreaterThanOrEqual(curve.points[i - 1].training_ms);
        // A publication is a mark of the curve of its version, at the training time then.
        expect((await publish(a, { record: record({ timeMs: 40_400 }), replay: await replay(40_400) })).status).toBe(201);
        const marked = await curveOf(b);
        const pub = marked.points.find((x: any) => x.kind === "publish");
        expect(pub.time_ms).toBe(40_400);
        expect(pub.revision).toBe(1);
        expect(pub.training_ms).toBe(rel.body.data.training.total_ms);
    });

    it("refuses malformed points and windows (the whole call), never trusting the game", async () => {
        const [a] = await admins();
        const id = (await lease(a)).body.data.lease_id;
        const bad = async (extra: Record<string, unknown>) => {
            const r = await beatWith(a, id, 10, extra);
            if (r.status !== 400) console.log(JSON.stringify(r.body));
            expect(r.status).toBe(400);
            expect(r.body.error.code).toBe("VALIDATION_ERROR");
        };
        await bad({ curve: [point(0)] });
        await bad({ curve: [point(-5)] });
        await bad({ curve: [point(30 * 60 * 1000 + 1)] });
        await bad({ curve: [{ ...point(41_000), kind: "publish" }] });
        await bad({ curve: [{ ...point(41_000), training_ms: 5 }] });
        await bad({ curve: [point(41_000, -1)] });
        await bad({ curve: Array.from({ length: 17 }, (_, i) => point(41_000 - i)) });
        await bad({ curve: "nope" });
        await bad({ attempts: [win({ finished: 1001 })] });
        await bad({ attempts: [win({ best_ms: 43000 })] });
        await bad({ attempts: [win({ stopped: [100, 800, 91] })] });
        await bad({ attempts: [win({ broken: [101, 0, 0] })] });
        await bad({ attempts: [win({ stopped: [990] })] });
        await bad({ attempts: [win({ section: 4 })] });
        await bad({ attempts: [win({ sections: 33 })] });
        await bad({ attempts: [win({ route_max: 300 })] });
        await bad({ attempts: [win({ finished: 0 })] });
        // More attempts than a window's time allows (50 000 a second).
        await bad({ attempts: [win({ duration_ms: 1000, attempts: 50_001, finished: 0, best_ms: 0, median_ms: 0, worst_ms: 0, stopped: [50_001, 0, 0], broken: [0, 0, 0] })] });
        await bad({ attempts: Array.from({ length: 5 }, () => win()) });
        // Nothing was kept.
        expect(await curveOf(a)).toBeUndefined();
        // The lease is still there: a good call goes through.
        expect((await beatWith(a, id, 10, { curve: [point(41_000)], attempts: [win()] })).status).toBe(200);
    });

    it("the windows of attempts: on the server's training time, with their session, given by the curve route", async () => {
        const [a, b] = await admins();
        const id = (await lease(a)).body.data.lease_id;
        await age(60_000);
        const r = await beatWith(a, id, 50_000, { attempts: [win({ age_ms: 30_000 }), win({ age_ms: 0, round: 2, section: 0 })] });
        expect(r.status).toBe(200);
        expect(r.body.data.curve.windows).toBe(2);
        const c = await api("GET", `${V}/admin/tas/test_01/ANY_PERCENT/curve`, { token: b.token });
        expect(c.status).toBe(200);
        expect(c.body.data.versions).toHaveLength(1);
        const v = c.body.data.versions[0];
        expect(v.level_hash).toBe(LEVEL_HASH);
        expect(v.windows).toHaveLength(2);
        const [w1, w2] = v.windows;
        expect(w1.session).toBe(1);
        expect(w1.end_ms - w1.start_ms).toBe(5000);
        expect(w2.end_ms).toBe(r.body.data.training.total_ms);
        expect(w1.end_ms).toBe(w2.end_ms - 30_000);
        expect(w1.stopped).toEqual([100, 800, 90]);
        expect(w1.broken).toEqual([10, 50, 0]);
        expect(w2.round).toBe(2);
        expect(w1.by.username).toBe("ada");
        // The list of every pair gives the points, never the windows.
        const list = await api("GET", `${V}/admin/tas`, { token: b.token });
        expect(JSON.stringify(list.body.data)).not.toContain("route_median");
    });

    it("keeps at most 1 000 best points per version: the last one gives way, the start and the best stay", async () => {
        const [a] = await admins();
        const id = (await lease(a)).body.data.lease_id;
        const stmts = [];
        for (let i = 1; i <= MAX_CURVE_POINTS; ++i)
            stmts.push(
                env.DB.prepare(
                    `INSERT INTO tas_curve_points (level_id, category, level_hash, sim_version, seq, kind, time_ms, training_ms, explored, session, found_at)
                     VALUES ('test_01', 'ANY_PERCENT', ?1, ?2, ?3, 'improvement', ?4, ?5, 0, 1, '2026-11-27T10:00:00Z')`,
                ).bind(LEVEL_HASH, SIM, i, 100_000 - i, i),
            );
        for (let i = 0; i < stmts.length; i += 500) await env.DB.batch(stmts.slice(i, i + 500));
        const r = await beatWith(a, id, 10, { curve: [point(50_000)] });
        expect(r.status).toBe(200);
        expect(r.body.data.curve.points[0].time_ms).toBe(50_000);
        const n = await env.DB.prepare("SELECT COUNT(*) AS n, MIN(time_ms) AS best, MAX(time_ms) AS first FROM tas_curve_points").first<{ n: number; best: number; first: number }>();
        expect(n!.n).toBe(MAX_CURVE_POINTS);
        expect(n!.best).toBe(50_000);
        expect(n!.first).toBe(100_000 - 1);
        // Still strictly decreasing.
        expect((await beatWith(a, id, 20, { curve: [point(50_000)] })).body.data.curve.refused).toBe(1);
    });

    it("the clouds (2.98): each window's finished attempts and checkpoint times, checked, placed on the server's training time, bounded", async () => {
        const [a, b] = await admins();
        const id = (await lease(a)).body.data.lease_id;
        const cp = (o: Record<string, unknown> = {}) => ({ reached: 40, best_ms: 12_000, median_ms: 12_300, worst_ms: 13_000, sample: [[0, 12_000], [4_000, 12_500]], ...o });
        const cloudy = (o: Record<string, unknown> = {}) => win({ finishes: [[0, 41_000], [2_500, 45_000], [5_000, 50_000]], checkpoints: [cp(), cp({ reached: 0, best_ms: 0, median_ms: 0, worst_ms: 0, sample: [] })], ...o });
        const bad = async (w: Record<string, unknown>) => {
            const r = await beatWith(a, id, 10, { attempts: [w] });
            if (r.status !== 400) console.log(JSON.stringify(r.body));
            expect(r.status).toBe(400);
            expect(r.body.error.code).toBe("VALIDATION_ERROR");
        };
        await bad(cloudy({ finishes: [[0, 40_000]] }));  // faster than the window's best
        await bad(cloudy({ finishes: [[0, 51_000]] }));  // slower than its worst
        await bad(cloudy({ finishes: [[5_001, 41_000]] }));  // after the window's end
        await bad(cloudy({ finishes: [[-1, 41_000]] }));
        await bad(cloudy({ finishes: Array.from({ length: 11 }, () => [0, 41_000]) }));  // more points than finished attempts
        await bad(cloudy({ finished: 1000, stopped: [0, 0, 0], broken: [0, 0, 0], best_ms: 41_000, finishes: Array.from({ length: MAX_CLOUD_FINISHES_PER_WINDOW + 1 }, () => [0, 41_000]) }));
        await bad(cloudy({ finishes: "nope" }));
        await bad(cloudy({ checkpoints: [cp({ reached: 1001 })] }));  // more than the attempts
        await bad(cloudy({ checkpoints: [cp({ median_ms: 11_000 })] }));
        await bad(cloudy({ checkpoints: [cp({ sample: [[0, 11_000]] })] }));
        await bad(cloudy({ checkpoints: [cp({ sample: Array.from({ length: MAX_CLOUD_SAMPLE_PER_CHECKPOINT + 1 }, () => [0, 12_000]) })] }));
        await bad(cloudy({ checkpoints: [cp({ reached: 0 })] }));
        await bad(cloudy({ checkpoints: [cp({ extra: 1 })] }));
        await bad(cloudy({ checkpoints: Array.from({ length: 33 }, () => cp()) }));
        // A good one: on the server's training time (the window's start plus each point's offset).
        await age(60_000);
        const r = await beatWith(a, id, 50_000, { attempts: [cloudy({ age_ms: 10_000 })] });
        expect(r.status).toBe(200);
        const c = await api("GET", `${V}/admin/tas/test_01/ANY_PERCENT/curve`, { token: b.token });
        const w = c.body.data.versions[0].windows[0];
        expect(w.end_ms).toBe(r.body.data.training.total_ms - 10_000);
        expect(w.finishes).toEqual([
            [w.start_ms, 41_000],
            [w.start_ms + 2_500, 45_000],
            [w.end_ms, 50_000],
        ]);
        expect(w.checkpoints[0]).toEqual({ reached: 40, best_ms: 12_000, median_ms: 12_300, worst_ms: 13_000, sample: [[w.start_ms, 12_000], [w.start_ms + 4_000, 12_500]] });
        expect(w.checkpoints[1].reached).toBe(0);
        // A window without clouds (a game before 2.98) is still taken, with none.
        expect((await beatWith(a, id, 60_000, { attempts: [win()] })).status).toBe(200);
        const c2 = await api("GET", `${V}/admin/tas/test_01/ANY_PERCENT/curve`, { token: b.token });
        expect(c2.body.data.versions[0].windows[1].finishes).toEqual([]);
        // The version's cap: beyond it, a window keeps only its best points.
        await env.DB.prepare("UPDATE tas_curve_windows SET points = ?1 WHERE seq = 1").bind(MAX_CLOUD_POINTS).run();
        expect((await beatWith(a, id, 70_000, { attempts: [cloudy()] })).status).toBe(200);
        const last = await env.DB.prepare("SELECT finishes, checkpoints, points FROM tas_curve_windows ORDER BY seq DESC LIMIT 1").first<any>();
        expect(JSON.parse(last.finishes).map((p: number[]) => p[1])).toEqual([41_000]);
        expect(JSON.parse(last.checkpoints)[0].sample.map((p: number[]) => p[1])).toEqual([12_000]);
        expect(last.points).toBe(2);
        // Thinning keeps the best and spreads the rest.
        const pts: [number, number][] = Array.from({ length: 500 }, (_, i) => [i, 90_000 - (i === 321 ? 5_000 : i % 7)]);
        const thin = thinCloud(pts, 96);
        expect(thin).toHaveLength(96);
        expect(thin.some((p) => p[1] === 85_000)).toBe(true);
    });

    it("windows beyond the cap are added to the last one of the session", async () => {
        const [a] = await admins();
        const id = (await lease(a)).body.data.lease_id;
        const stmts = [];
        for (let i = 1; i <= MAX_CURVE_WINDOWS; ++i)
            stmts.push(
                env.DB.prepare(
                    `INSERT INTO tas_curve_windows (level_id, category, level_hash, sim_version, seq, session, round, section, sections, start_ms, end_ms, attempts, finished,
                       best_ms, median_ms, worst_ms, route_median, route_max, stopped, broken, at)
                     VALUES ('test_01', 'ANY_PERCENT', ?1, ?2, ?3, 1, 1, 2, 3, 0, 1, 10, 0, 0, 0, 0, 5, 9, '[0,10,0]', '[0,1,0]', '2026-11-27T10:00:00Z')`,
                ).bind(LEVEL_HASH, SIM, i),
            );
        for (let i = 0; i < stmts.length; i += 500) await env.DB.batch(stmts.slice(i, i + 500));
        const r = await beatWith(a, id, 2000, { attempts: [win()] });
        expect(r.status).toBe(200);
        const rows = await env.DB.prepare("SELECT COUNT(*) AS n FROM tas_curve_windows").first<{ n: number }>();
        expect(rows!.n).toBe(MAX_CURVE_WINDOWS);
        const last = await env.DB.prepare("SELECT * FROM tas_curve_windows ORDER BY seq DESC LIMIT 1").first<any>();
        expect(last.attempts).toBe(1010);
        expect(last.finished).toBe(10);
        expect(JSON.parse(last.stopped)).toEqual([100, 810, 90]);
        expect(JSON.parse(last.broken)).toEqual([10, 51, 0]);
        // Its clouds too, bounded per window.
        const more = await beatWith(a, id, 4000, {
            attempts: [win({ finishes: Array.from({ length: 10 }, (_, i) => [i * 100, 41_000 + i]), checkpoints: [{ reached: 5, best_ms: 1000, median_ms: 1100, worst_ms: 1200, sample: [[0, 1000]] }] })],
        });
        expect(more.status).toBe(200);
        const merged = await env.DB.prepare("SELECT * FROM tas_curve_windows ORDER BY seq DESC LIMIT 1").first<any>();
        expect(JSON.parse(merged.finishes)).toHaveLength(10);
        expect(JSON.parse(merged.checkpoints)[0].reached).toBe(5);
        expect(merged.points).toBe(11);
    });
});
