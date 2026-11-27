// ADMINISTRATION › TAS (tas.ts): the leases of the trainings run on the administrators' own
// computers (one administrator per level and category, a heartbeat, an expiry, a release), the
// training time measured by the server, the publication of a run (everything the server can
// check without playing it), the public list and files. The routes of /admin/tas do not exist
// for a player.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
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
