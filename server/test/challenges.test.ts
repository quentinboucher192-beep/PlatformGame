import { createExecutionContext, createScheduledController, env, waitOnExecutionContext } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import fixture from "../../tests/data/challenge_rules_fixture.json";
import catalogJson from "../src/challengeCatalog.json";
import {
    CATALOG,
    SAFE_EXTRA_POWERS,
    addDays,
    canonicalRules,
    drawRules,
    rulesHash,
    streakOf,
    tierOf,
    utcDate,
    type ChallengeRules,
} from "../src/challenges";
import worker from "../src/index";
import { inflateContainer, parseReplay } from "../src/replay";
import { buildChallengeSql, canonicalRules as scriptCanonical, sha256Hex as scriptSha256 } from "../scripts/challenge-sql.mjs";
import { EXIT, SPAWN, api, buildReplay, registerPlayer, seedAll, submission, uploadReplay, type ReplaySpec, type TestPlayer } from "./helpers";
import { LH, publish } from "./publish";

/** Campaign levels registered on the test server, at the revision of the catalogue. */
const CAMPAIGN: Record<string, string> = {
    sylvanox_01: "1".repeat(64),
    glacior_02: "2".repeat(64),
    pyralis_04: "3".repeat(64),
    // EXTRÊME: never drawn automatically.
    noyau_01: "4".repeat(64),
};

async function seedCampaign(): Promise<void> {
    const stmts = [];
    for (const [id, hash] of Object.entries(CAMPAIGN)) {
        const info = CATALOG.levels[id]!;
        stmts.push(
            env.DB.prepare("INSERT INTO levels VALUES (?1, ?2, ?3, ?3, 1, ?4, ?5, 1)").bind(id, info.name, info.planet, info.revision, hash),
            env.DB.prepare(
                `INSERT INTO level_versions (level_id, version, content_hash, compatibility_status, min_game_version, total_collectibles,
                   total_enemies, time_limit_ms, min_time_ms, max_speed_px_s, bounds_w, bounds_h, spawn_x, spawn_y, exit_x, exit_y, exit_w, exit_h, created_at)
                 VALUES (?1, ?2, ?3, 'active', '2.20.0', 10, 20, 900000, 30000, 1500, 6000, 1200, ?4, ?5, ?6, ?7, ?8, ?9, '2026-11-01T00:00:00.000Z')`,
            ).bind(id, info.revision, hash, SPAWN.x, SPAWN.y, EXIT.x, EXIT.y, EXIT.w, EXIT.h),
        );
    }
    await env.DB.batch(stmts);
}

beforeEach(async () => {
    await seedAll();
    await seedCampaign();
});

const today = () => utcDate();

interface TestChallenge {
    id: string;
    date: string;
    level_id: string;
    level_hash: string;
    rules: ChallengeRules;
    hash: string;
    canonical: string;
}

/** Inserts a challenge directly (past days, today: the admin script refuses them). */
async function insertChallenge(
    date: string,
    o: Partial<ChallengeRules> & { level?: string; community?: { id: string; version: number; hash: string; author: string; title: string } } = {},
): Promise<TestChallenge> {
    const id = `daily-${date}`;
    const rules: ChallengeRules = {
        hearts: o.hearts ?? 3,
        slots: o.slots ?? 4,
        powers: o.powers ?? ["dash"],
        stations: o.stations ?? true,
        checkpoints: o.checkpoints ?? false,
    };
    const levelId = o.community?.id ?? o.level ?? "glacior_02";
    const levelHash = o.community?.hash ?? CAMPAIGN[levelId]!;
    const hash = await rulesHash(id, rules);
    await env.DB.prepare(
        `INSERT INTO daily_challenges (id, kind, date, source, level_source, level_id, level_version, level_hash, level_title, hearts, slots,
           powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, featured_player_id, created_at, updated_at)
         VALUES (?1, 'daily', ?2, 'admin', ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, 90000, 70000, 1, ?14, ?15, ?15)`,
    )
        .bind(
            id,
            date,
            o.community ? "community" : "campaign",
            levelId,
            o.community?.version ?? CATALOG.levels[levelId]?.revision ?? 1,
            levelHash,
            o.community?.title ?? CATALOG.levels[levelId]?.name ?? levelId,
            rules.hearts,
            rules.slots,
            JSON.stringify([...rules.powers].sort()),
            rules.stations ? 1 : 0,
            rules.checkpoints ? 1 : 0,
            hash,
            o.community?.author ?? null,
            new Date().toISOString(),
        )
        .run();
    return { id, date, level_id: levelId, level_hash: levelHash, rules, hash, canonical: canonicalRules(id, rules) };
}

function runBody(ch: TestChallenge, o: Record<string, unknown> = {}): Record<string, any> {
    return {
        attempt_id: crypto.randomUUID(),
        time_ms: 80000,
        deaths: 0,
        damage_taken: 0,
        level_hash: ch.level_hash,
        rules_hash: ch.hash,
        game_version: "2.31.0",
        has_replay: true,
        ...o,
    };
}

function submit(p: TestPlayer | null, ch: TestChallenge | string, body: Record<string, unknown>) {
    const id = typeof ch === "string" ? ch : ch.id;
    return api("POST", `/api/v1/challenges/${id}/runs`, { token: p?.token, body });
}

function challengeReplay(ch: TestChallenge, body: Record<string, any>, o: Partial<ReplaySpec> = {}) {
    return buildReplay({
        levelId: ch.level_id,
        levelHash: ch.level_hash,
        attemptId: body.attempt_id,
        durationMs: body.time_ms,
        deaths: body.deaths,
        challengeRules: ch.canonical,
        health: ch.rules.hearts,
        ...o,
    });
}

function putReplay(p: TestPlayer, runId: string, data: Uint8Array) {
    return api("PUT", `/api/v1/challenges/runs/${runId}/replay`, { token: p.token, raw: data, headers: { "content-type": "application/octet-stream" } });
}

/** Submits a run and uploads its valid format 4 replay. */
async function verifiedRun(p: TestPlayer, ch: TestChallenge, timeMs: number, o: Record<string, unknown> = {}) {
    const body = runBody(ch, { time_ms: timeMs, ...o });
    const res = await submit(p, ch, body);
    expect(res.status).toBe(201);
    const up = await putReplay(p, res.body.data.run.id, await challengeReplay(ch, body));
    expect(up.status).toBe(201);
    return { runId: res.body.data.run.id as string, status: up.body.data.run.status as string, body: up.body };
}

describe("daily challenges: rules", () => {
    it("builds the canonical form and rules_hash of the shared fixture (server and admin script)", async () => {
        expect(fixture.cases.length).toBeGreaterThanOrEqual(3);
        for (const c of fixture.cases) {
            const r = c.input as ChallengeRules & { challenge: string };
            expect(canonicalRules(r.challenge, r)).toBe(c.canonical);
            expect(await rulesHash(r.challenge, r)).toBe(c.sha256);
            expect(scriptCanonical(r.challenge, r)).toBe(c.canonical);
            expect(await scriptSha256(scriptCanonical(r.challenge, r))).toBe(c.sha256);
        }
    });

    it("computes the tier on the server", () => {
        const c = { silver_ms: 90000, gold_ms: 70000, gold_no_damage: 1 };
        expect(tierOf(c, 70000, 0)).toBe("gold");
        expect(tierOf(c, 65000, 1)).toBe("silver");
        expect(tierOf({ ...c, gold_no_damage: 0 }, 65000, 1)).toBe("gold");
        expect(tierOf(c, 90000, 3)).toBe("silver");
        expect(tierOf(c, 90001, 0)).toBe("bronze");
    });

    it("draws safe rules for every campaign level of the catalogue", () => {
        const hearts = new Set([1, 2, 3, 5, CATALOG.player_max_hearts]);
        for (const [id, level] of Object.entries(CATALOG.levels)) {
            for (let i = 0; i < 60; ++i) {
                const r = drawRules(level);
                expect(r.stations, id).toBe(true);
                expect(r.slots, id).toBeGreaterThanOrEqual(level.slots);
                expect(r.slots, id).toBeLessThanOrEqual(8);
                expect(r.powers.length, id).toBeLessThanOrEqual(r.slots);
                expect(hearts.has(r.hearts), id).toBe(true);
                expect([...r.powers].sort(), id).toEqual(r.powers);
                for (const p of level.start_powers) expect(r.powers, id).toContain(p);
                const extras = r.powers.filter((p) => !level.start_powers.includes(p));
                expect(extras.length, id).toBeLessThanOrEqual(2);
                const levelInputs = level.level_powers.map((p) => CATALOG.powers[p]);
                for (const p of extras) {
                    expect(SAFE_EXTRA_POWERS as readonly string[], id).toContain(p);
                    expect(levelInputs, id).not.toContain(CATALOG.powers[p]);
                }
                expect(new Set(extras.map((p) => CATALOG.powers[p])).size).toBe(extras.length);
            }
        }
    });

    it("computes the streak over consecutive UTC days up to today or yesterday", () => {
        const t = "2026-11-20";
        expect(streakOf([], t)).toEqual({ current: 0, best: 0, last_date: null });
        expect(streakOf(["2026-11-18", "2026-11-19", "2026-11-20"], t)).toEqual({ current: 3, best: 3, last_date: "2026-11-20" });
        expect(streakOf(["2026-11-18", "2026-11-19"], t)).toEqual({ current: 2, best: 2, last_date: "2026-11-19" });
        expect(streakOf(["2026-11-10", "2026-11-11", "2026-11-12", "2026-11-17", "2026-11-18"], t)).toEqual({ current: 0, best: 3, last_date: "2026-11-18" });
        expect(streakOf(["2026-10-31", "2026-11-01", "2026-11-20"], t)).toEqual({ current: 1, best: 2, last_date: "2026-11-20" });
    });
});

describe("daily challenges: generation", () => {
    it("draws today to today + 7 from ranked campaign levels only and never shows a future challenge", async () => {
        const author = await registerPlayer("Createur");
        await publish(author); // a community level is never drawn
        const res = await api("GET", "/api/v1/challenges/daily/current");
        expect(res.status).toBe(200);
        const t = today();
        expect(res.body.data.challenge).toMatchObject({ id: `daily-${t}`, kind: "daily", date: t, source: "auto", starts_at: `${t}T00:00:00Z`, ends_at: `${addDays(t, 1)}T00:00:00Z` });
        expect(res.body.data.previous).toBeNull();
        expect(Date.parse(res.body.data.server_time)).toBeGreaterThan(0);
        const c = res.body.data.challenge;
        expect(c.level.source).toBe("campaign");
        expect(c.level.author).toBeNull();
        expect(c.level.level_hash).toBe(CAMPAIGN[c.level.level_id]);
        expect(c.level.level_version).toBe(CATALOG.levels[c.level.level_id]!.revision);
        expect(c.level.title).toBe(CATALOG.levels[c.level.level_id]!.name);
        expect(c.rules_hash).toBe(await rulesHash(c.id, c.rules));
        expect(c.tiers).toEqual({
            silver_ms: CATALOG.levels[c.level.level_id]!.silver_ms,
            gold_ms: CATALOG.levels[c.level.level_id]!.gold_ms,
            gold_no_damage: true,
        });

        const rows = (await env.DB.prepare("SELECT * FROM daily_challenges ORDER BY date").all<any>()).results;
        expect(rows.map((r) => r.date)).toEqual(Array.from({ length: 8 }, (_, i) => addDays(t, i)));
        for (const r of rows) {
            expect(r).toMatchObject({ source: "auto", level_source: "campaign", stations: 1, featured_player_id: null });
            expect(["sylvanox_01", "glacior_02", "pyralis_04"]).toContain(r.level_id);
            const info = CATALOG.levels[r.level_id]!;
            const powers = JSON.parse(r.powers) as string[];
            expect(r.slots).toBeGreaterThanOrEqual(info.slots);
            for (const p of info.start_powers) expect(powers).toContain(p);
            expect(r.rules_hash).toBe(
                await rulesHash(r.id, { hearts: r.hearts, slots: r.slots, powers, stations: true, checkpoints: r.checkpoints === 1 }),
            );
        }
        // Three drawable levels: consecutive days never repeat a level.
        for (let i = 1; i < rows.length; ++i) expect(rows[i].level_id).not.toBe(rows[i - 1].level_id);

        // Reading again draws nothing new and changes nothing.
        const again = await api("GET", "/api/v1/challenges/daily/current");
        expect(again.body.data.challenge).toEqual(res.body.data.challenge);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM daily_challenges").first<{ n: number }>())?.n).toBe(8);

        // No route shows a challenge before its day.
        const tomorrow = `daily-${addDays(t, 1)}`;
        expect(JSON.stringify(again.body)).not.toContain(tomorrow);
        const list = await api("GET", `/api/v1/challenges/daily?before=${addDays(t, 30)}&limit=30`);
        expect(list.status).toBe(200);
        expect(list.body.data.challenges.map((x: any) => x.id)).toEqual([`daily-${t}`]);
        expect(JSON.stringify(list.body)).not.toContain(tomorrow);
        const board = await api("GET", `/api/v1/challenges/${tomorrow}/leaderboard`);
        expect(board.status).toBe(404);
        expect(board.body.error.code).toBe("CHALLENGE_NOT_FOUND");
        const p = await registerPlayer("Nova");
        const early = await submit(p, tomorrow, { ...runBody({ id: tomorrow, level_hash: CAMPAIGN.glacior_02!, hash: "0".repeat(64) } as TestChallenge) });
        expect(early.status).toBe(409);
        expect(early.body.error.code).toBe("CHALLENGE_NOT_STARTED");
        expect(JSON.stringify(early.body)).not.toContain("rules");
        // The same answer for a far day that does not exist: nothing is revealed.
        const far = await submit(p, `daily-${addDays(t, 40)}`, runBody({ level_hash: "0".repeat(64), hash: "0".repeat(64) } as TestChallenge));
        expect(far.body.error.code).toBe("CHALLENGE_NOT_STARTED");
        expect((await api("GET", "/api/v1/challenges/daily?limit=31")).status).toBe(400);
    });

    it("draws each day once when many readers arrive together", async () => {
        const reads = await Promise.all(Array.from({ length: 6 }, () => api("GET", "/api/v1/challenges/daily/current")));
        for (const r of reads) expect(r.status).toBe(200);
        const ids = new Set(reads.map((r) => JSON.stringify(r.body.data.challenge)));
        expect(ids.size).toBe(1);
        const rows = await env.DB.prepare("SELECT date, COUNT(*) AS n FROM daily_challenges GROUP BY date").all<{ date: string; n: number }>();
        expect(rows.results).toHaveLength(8);
        for (const r of rows.results) expect(r.n).toBe(1);
    });

    it("never changes a challenge that has begun; missing days are drawn again", async () => {
        await api("GET", "/api/v1/challenges/daily/current");
        const t = today();
        await expect(env.DB.prepare("UPDATE daily_challenges SET hearts = 1 WHERE date = ?1").bind(t).run()).rejects.toThrow(/already started/);
        await expect(env.DB.prepare("UPDATE daily_challenges SET level_id = 'pyralis_04' WHERE date = ?1").bind(t).run()).rejects.toThrow();
        // A coming day may still change (the administrator's choice), and the bookkeeping of today may.
        await env.DB.prepare("UPDATE daily_challenges SET hearts = 1 WHERE date = ?1").bind(addDays(t, 2)).run();
        await env.DB.prepare("UPDATE daily_challenges SET featured_notified_at = 'x' WHERE date = ?1").bind(t).run();
        const before = await env.DB.prepare("SELECT * FROM daily_challenges WHERE date = ?1").bind(t).first();
        await env.DB.prepare("DELETE FROM daily_challenges WHERE date = ?1").bind(addDays(t, 5)).run();
        await api("GET", "/api/v1/challenges/daily/current");
        expect(await env.DB.prepare("SELECT date FROM daily_challenges WHERE date = ?1").bind(addDays(t, 5)).first()).not.toBeNull();
        expect(await env.DB.prepare("SELECT * FROM daily_challenges WHERE date = ?1").bind(t).first()).toEqual(before);
    });

    it("lists the challenges that have begun, most recent first", async () => {
        const t = today();
        for (const d of [3, 2, 1]) await insertChallenge(addDays(t, -d), { level: d === 2 ? "sylvanox_01" : "glacior_02" });
        const cur = await api("GET", "/api/v1/challenges/daily/current");
        expect(cur.body.data.previous.id).toBe(`daily-${addDays(t, -1)}`);
        const page1 = await api("GET", "/api/v1/challenges/daily?limit=2");
        expect(page1.body.data.challenges.map((c: any) => c.date)).toEqual([t, addDays(t, -1)]);
        expect(page1.body.data.next_before).toBe(addDays(t, -1));
        const page2 = await api("GET", `/api/v1/challenges/daily?limit=2&before=${page1.body.data.next_before}`);
        expect(page2.body.data.challenges.map((c: any) => c.date)).toEqual([addDays(t, -2), addDays(t, -3)]);
        expect(page2.body.data.challenges[0].level.level_id).toBe("sylvanox_01");
        expect((await api("GET", "/api/v1/challenges/daily?before=2026-02-30")).status).toBe(400);
    });
});

describe("daily challenges: runs", () => {
    it("takes the player from the session, is idempotent and computes the tier itself", async () => {
        const ch = await insertChallenge(today());
        const p = await registerPlayer("Nova");
        expect((await submit(null, ch, runBody(ch))).status).toBe(401);
        const spoof = await submit(p, ch, runBody(ch, { player_id: "someone-else" }));
        expect(spoof.status).toBe(400);
        expect(spoof.body.error.details[0]).toMatchObject({ field: "player_id", message: "champ inconnu" });
        // A tier from the client is not even accepted.
        expect((await submit(p, ch, runBody(ch, { tier: "gold" }))).status).toBe(400);

        const body = runBody(ch, { time_ms: 65000 });
        const a = await submit(p, ch, body);
        expect(a.status).toBe(201);
        expect(a.body.data).toMatchObject({ duplicate: false, run: { status: "PENDING", tier: "gold", time_ms: 65000 }, replay: { required: true } });
        expect(a.body.data.replay.upload_path).toBe(`/api/v1/challenges/runs/${a.body.data.run.id}/replay`);
        const row = await env.DB.prepare("SELECT player_id, tier FROM challenge_runs WHERE id = ?1").bind(a.body.data.run.id).first<any>();
        expect(row).toEqual({ player_id: p.id, tier: "gold" });
        const b = await submit(p, ch, body);
        expect(b.status).toBe(200);
        expect(b.body.data.duplicate).toBe(true);
        expect(b.body.data.run.id).toBe(a.body.data.run.id);
        const c = await submit(p, ch, { ...body, time_ms: 64000 });
        expect(c.status).toBe(409);
        expect(c.body.error.code).toBe("ATTEMPT_CONFLICT");

        expect((await submit(p, ch, runBody(ch, { time_ms: 65000, damage_taken: 2 }))).body.data.run.tier).toBe("silver");
        expect((await submit(p, ch, runBody(ch, { time_ms: 85000 }))).body.data.run.tier).toBe("silver");
        expect((await submit(p, ch, runBody(ch, { time_ms: 95000 }))).body.data.run.tier).toBe("bronze");
        // Impossible time for the level revision.
        const fast = await submit(p, ch, runBody(ch, { time_ms: 5000 }));
        expect(fast.body.data.run.status).toBe("REJECTED");
        expect(fast.body.data.replay.required).toBe(false);
    });

    it("refuses runs outside the challenge's window and runs of other rules, levels or games", async () => {
        const t = today();
        const ch = await insertChallenge(t);
        const old = await insertChallenge(addDays(t, -2));
        const next = await insertChallenge(addDays(t, 1));
        const p = await registerPlayer("Nova");
        const ended = await submit(p, old, runBody(old));
        expect(ended.status).toBe(409);
        expect(ended.body.error.code).toBe("CHALLENGE_ENDED");
        const early = await submit(p, next, runBody(next));
        expect(early.status).toBe(409);
        expect(early.body.error.code).toBe("CHALLENGE_NOT_STARTED");
        expect((await submit(p, "daily-2020-01-01", runBody(ch))).body.error.code).toBe("CHALLENGE_NOT_FOUND");
        expect((await submit(p, "daily-2026-02-30", runBody(ch))).status).toBe(404);
        expect((await submit(p, "weekly-1", runBody(ch))).status).toBe(404);
        const rules = await submit(p, ch, runBody(ch, { rules_hash: "f".repeat(64) }));
        expect(rules.status).toBe(409);
        expect(rules.body.error.code).toBe("RULES_MISMATCH");
        const level = await submit(p, ch, runBody(ch, { level_hash: CAMPAIGN.sylvanox_01 }));
        expect(level.status).toBe(409);
        expect(level.body.error.code).toBe("LEVEL_MISMATCH");
        const game = await submit(p, ch, runBody(ch, { game_version: "2.30.0" }));
        expect(game.status).toBe(426);
        expect(game.body.error.code).toBe("GAME_VERSION_UNSUPPORTED");
        expect(await env.DB.prepare("SELECT COUNT(*) AS n FROM challenge_runs").first<{ n: number }>()).toEqual({ n: 0 });
    });

    it("verifies a format 4 replay and rejects format 3, other rules and health above the hearts", async () => {
        const ch = await insertChallenge(today(), { hearts: 3 });
        const [a, b, c, d] = [await registerPlayer("Alpha"), await registerPlayer("Bravo"), await registerPlayer("Charlie"), await registerPlayer("Delta")];

        const ok = await verifiedRun(a, ch, 80000);
        expect(ok.status).toBe("VERIFIED");
        expect(ok.body.data.analysis.ok).toBe(true);
        expect(ok.body.data.replay.verification_status).toBe("VERIFIED");
        const dl = await api("GET", `/api/v1/challenges/runs/${ok.runId}/replay`);
        expect(dl.status).toBe(200);
        expect(dl.headers.get("content-type")).toBe("application/octet-stream");
        // Only the owner uploads, and only once.
        const again = await putReplay(a, ok.runId, await challengeReplay(ch, runBody(ch)));
        expect(again.body.error.code).toBe("REPLAY_EXISTS");

        const reject = async (p: TestPlayer, o: Partial<ReplaySpec>, reason: string) => {
            const body = runBody(ch, { time_ms: 81000 });
            const run = await submit(p, ch, body);
            expect(run.status).toBe(201);
            const up = await putReplay(p, run.body.data.run.id, await challengeReplay(ch, body, o));
            expect(up.status).toBe(201);
            expect(up.body.data.run.status).toBe("REJECTED");
            expect(up.body.data.analysis.reasons.join(" ")).toContain(reason);
            // A rejected run's replay is not public.
            expect((await api("GET", `/api/v1/challenges/runs/${run.body.data.run.id}/replay`)).status).toBe(404);
            return run.body.data.run.id as string;
        };
        await reject(b, { challengeRules: undefined, inputFormat: 3, exact: true }, "format 4");
        const other = canonicalRules(ch.id, { ...ch.rules, hearts: 5 });
        await reject(c, { challengeRules: other }, "règles");
        const runD = await reject(d, { health: 4 }, "santé");

        // Someone else's run.
        const body = runBody(ch);
        const run = await submit(b, ch, body);
        const stolen = await putReplay(c, run.body.data.run.id, await challengeReplay(ch, body));
        expect(stolen.status).toBe(403);
        expect(stolen.body.error.code).toBe("NOT_OWNER");
        // A rejected run takes no replay any more.
        expect((await putReplay(d, runD, await challengeReplay(ch, runBody(ch)))).status).toBe(409);
        // An unreadable file stores nothing.
        const bad = await putReplay(b, run.body.data.run.id, new TextEncoder().encode("pas un replay"));
        expect(bad.status).toBe(422);
        expect(bad.body.error.code).toBe("REPLAY_MALFORMED");
    });

    it("reads format 4 and keeps formats 1 to 3 as they were", async () => {
        const attemptId = crypto.randomUUID();
        const canonical = canonicalRules("daily-2026-11-16", { hearts: 2, slots: 3, powers: ["dash"], stations: true, checkpoints: false });
        const v4 = parseReplay(await inflateContainer(await buildReplay({ attemptId, durationMs: 40000, challengeRules: canonical })));
        expect(v4.formatVersion).toBe(4);
        expect(v4.challengeRules).toBe(canonical);
        expect(v4.inputs?.keysPerSlot).toBe(true);
        for (const f of [{ inputFormat: 3 as const, exact: true }, { inputFormat: 2 as const, exact: true }, {}]) {
            const r = parseReplay(await inflateContainer(await buildReplay({ attemptId, durationMs: 40000, ...f })));
            expect(r.challengeRules).toBeNull();
        }
        // The challenge bit belongs to format 4: a format 3 file with it is refused.
        const raw = await inflateContainer(await buildReplay({ attemptId, durationMs: 40000, challengeRules: canonical }));
        raw[4] = 3;
        expect(() => parseReplay(raw)).toThrow(/section des entrées/);
        // A section of the wrong length is refused.
        const cut = await inflateContainer(await buildReplay({ attemptId, durationMs: 40000, challengeRules: canonical }));
        expect(() => parseReplay(cut.subarray(0, cut.length - 1))).toThrow(/section du défi/);
    });

    it("never counts a challenge run as a record", async () => {
        const ch = await insertChallenge(today(), { level: "glacior_02" });
        const p = await registerPlayer("Nova");
        const sub = submission({
            level_id: "glacior_02",
            level_version: CATALOG.levels.glacior_02!.revision,
            level_hash: CAMPAIGN.glacior_02,
            collectibles: 4,
            game_version: "2.31.0",
        });
        const res = await api("POST", "/api/v1/records/submit", { token: p.token, body: sub });
        expect(res.status).toBe(201);
        const replay = await buildReplay({
            levelId: "glacior_02",
            levelHash: CAMPAIGN.glacior_02,
            attemptId: sub.attempt_id as string,
            durationMs: sub.time_ms as number,
            challengeRules: ch.canonical,
        });
        const up = await uploadReplay(p.token, res.body.data.record.id, replay);
        expect(up.status).toBe(201);
        expect(up.body.data.verification_status).toBe("REJECTED");
        expect(up.body.data.analysis.reasons.join(" ")).toContain("partie de défi");
    });
});

describe("daily challenges: leaderboard and results", () => {
    it("ranks the best VERIFIED time of each player by scope, ties by earlier submission, and shows my PENDING time", async () => {
        const t = today();
        const ch = await insertChallenge(t);
        const amelie = await registerPlayer("Amelie", { country_code: "FR", location_visibility: "country" });
        const bastien = await registerPlayer("Bastien", { country_code: "US", location_visibility: "country" });
        const chloe = await registerPlayer("Chloe", { country_code: "FR", location_visibility: "hidden" });
        const dario = await registerPlayer("Dario", { country_code: "FR", location_visibility: "country" });
        await verifiedRun(amelie, ch, 70000);
        await verifiedRun(amelie, ch, 65000);
        await verifiedRun(bastien, ch, 65000); // same time, later: second
        await verifiedRun(chloe, ch, 90000);
        // Dario's better time waits for its replay: never ranked, but shown to him.
        await verifiedRun(dario, ch, 99000);
        const pending = await submit(dario, ch, runBody(ch, { time_ms: 60000 }));
        expect(pending.body.data.run.status).toBe("PENDING");

        const world = await api("GET", `/api/v1/challenges/${ch.id}/leaderboard`);
        expect(world.status).toBe(200);
        expect(world.body.data.total).toBe(4);
        expect(world.body.data.entries.map((e: any) => [e.rank, e.player.username, e.time_ms, e.status])).toEqual([
            [1, "Amelie", 65000, "VERIFIED"],
            [2, "Bastien", 65000, "VERIFIED"],
            [3, "Chloe", 90000, "VERIFIED"],
            [4, "Dario", 99000, "VERIFIED"],
        ]);
        expect(world.body.data.entries[0]).toMatchObject({ tier: "gold", has_replay: true, player: { country_code: "FR", frame: "simple", title: null } });
        expect(world.body.data.entries[0].run_id).toEqual(expect.any(String));
        expect(world.body.data.entries[2].player.country_code).toBeNull();
        expect(world.body.data.me).toBeNull();

        const mine = await api("GET", `/api/v1/challenges/${ch.id}/leaderboard`, { token: dario.token });
        expect(mine.body.data.me).toMatchObject({ rank: null, time_ms: 60000, status: "PENDING", run_id: pending.body.data.run.id });
        const amelieView = await api("GET", `/api/v1/challenges/${ch.id}/leaderboard?scope=country`, { token: amelie.token });
        expect(amelieView.body.data.country_code).toBe("FR");
        expect(amelieView.body.data.entries.map((e: any) => e.player.username)).toEqual(["Amelie", "Dario"]);
        expect(amelieView.body.data.me).toMatchObject({ rank: 1, time_ms: 65000, status: "VERIFIED" });
        const us = await api("GET", `/api/v1/challenges/${ch.id}/leaderboard?scope=country&country=US&limit=1`);
        expect(us.body.data.entries.map((e: any) => e.player.username)).toEqual(["Bastien"]);
        expect((await api("GET", `/api/v1/challenges/${ch.id}/leaderboard?scope=country`)).status).toBe(400);

        expect((await api("GET", `/api/v1/challenges/${ch.id}/leaderboard?scope=friends`)).status).toBe(401);
        const now = new Date().toISOString();
        await env.DB.batch([
            env.DB.prepare("INSERT INTO player_friends VALUES (?1, ?2, 'accepted', ?3)").bind(chloe.id, bastien.id, now),
            env.DB.prepare("INSERT INTO player_friends VALUES (?1, ?2, 'accepted', ?3)").bind(bastien.id, chloe.id, now),
        ]);
        const friends = await api("GET", `/api/v1/challenges/${ch.id}/leaderboard?scope=friends`, { token: chloe.token });
        expect(friends.body.data.entries.map((e: any) => [e.rank, e.player.username])).toEqual([
            [1, "Bastien"],
            [2, "Chloe"],
        ]);
        expect(friends.body.data.me).toMatchObject({ rank: 2, status: "VERIFIED" });
        expect(friends.body.data.total).toBe(2);
    });

    it("gives my results and my streak (days with a run that was not rejected)", async () => {
        const t = today();
        const days = [-4, -3, -2, -1, 0].map((d) => addDays(t, d));
        const chs = [];
        for (const d of days) chs.push(await insertChallenge(d));
        const p = await registerPlayer("Nova");
        const other = await registerPlayer("Orion");
        const run = (ch: TestChallenge, time: number, tier: string, status: string, who = p) =>
            env.DB.prepare(
                `INSERT INTO challenge_runs (id, challenge_id, player_id, attempt_id, time_ms, deaths, damage_taken, level_hash, rules_hash, game_version,
                   tier, status, created_at) VALUES (?1, ?2, ?3, ?4, ?5, 0, 0, ?6, ?7, '2.31.0', ?8, ?9, ?10)`,
            ).bind(crypto.randomUUID(), ch.id, who.id, crypto.randomUUID(), time, ch.level_hash, ch.hash, tier, status, `${ch.date}T12:00:00.000Z`);
        await env.DB.batch([
            run(chs[0]!, 80000, "silver", "VERIFIED"),
            // Day -3: only a rejected run, the streak breaks there.
            run(chs[1]!, 50000, "gold", "REJECTED"),
            run(chs[2]!, 95000, "bronze", "VERIFIED"),
            run(chs[2]!, 66000, "gold", "PENDING"),
            run(chs[2]!, 75000, "silver", "VERIFIED"),
            run(chs[3]!, 88000, "silver", "VERIFIED"),
            run(chs[0]!, 60000, "gold", "VERIFIED", other),
        ]);
        expect((await api("GET", "/api/v1/challenges/me")).status).toBe(401);
        const me = await api("GET", "/api/v1/challenges/me", { token: p.token });
        expect(me.status).toBe(200);
        // Played -2 and -1, not today yet: the streak still holds.
        expect(me.body.data.streak).toEqual({ current: 2, best: 2, last_date: days[3] });
        expect(me.body.data.results).toEqual([
            { challenge_id: chs[3]!.id, date: days[3], best_time_ms: 88000, tier: "silver", status: "VERIFIED" },
            { challenge_id: chs[2]!.id, date: days[2], best_time_ms: 66000, tier: "gold", status: "PENDING" },
            { challenge_id: chs[1]!.id, date: days[1], best_time_ms: 50000, tier: null, status: "REJECTED" },
            { challenge_id: chs[0]!.id, date: days[0], best_time_ms: 80000, tier: "silver", status: "VERIFIED" },
        ]);
        // Today's run extends the streak.
        await submit(p, chs[4]!, runBody(chs[4]!));
        const after = await api("GET", "/api/v1/challenges/me?limit=2", { token: p.token });
        expect(after.body.data.streak).toEqual({ current: 3, best: 3, last_date: days[4] });
        expect(after.body.data.results).toHaveLength(2);
        expect(after.body.data.results[0]).toMatchObject({ date: days[4], status: "PENDING", tier: "silver" });
    });
});

describe("daily challenges: administrator and creator", () => {
    const sqlStatements = (sql: string) => sql.split("\n").filter((l) => l.trim() && !l.startsWith("--"));
    async function apply(sql: string) {
        for (const s of sqlStatements(sql)) await env.DB.prepare(s).run();
    }
    const lastAudit = () => env.DB.prepare("SELECT action, details FROM audit_log WHERE action LIKE 'challenge_set_%' ORDER BY id DESC LIMIT 1").first<any>();

    it("writes SQL that replaces a coming day, refuses past days and leaves a day that has begun alone", async () => {
        const t = today();
        await expect(buildChallengeSql({ date: t, level: "campaign:glacior_02" }, catalogJson, t)).rejects.toThrow(/refusée/);
        await expect(buildChallengeSql({ date: addDays(t, -3), level: "campaign:glacior_02" }, catalogJson, t)).rejects.toThrow(/refusée/);
        await expect(buildChallengeSql({ date: addDays(t, 1), level: "campaign:inconnu" }, catalogJson, t)).rejects.toThrow(/catalogue/);
        await expect(buildChallengeSql({ date: addDays(t, 1), level: "campaign:glacior_02", powers: "dash,vol" }, catalogJson, t)).rejects.toThrow(/inconnu/);
        await expect(buildChallengeSql({ date: addDays(t, 1), level: "campaign:glacior_02", slots: 1, powers: "dash,glide" }, catalogJson, t)).rejects.toThrow();

        await api("GET", "/api/v1/challenges/daily/current"); // the coming days are drawn
        const date = addDays(t, 3);
        const out = await buildChallengeSql(
            { date, level: "campaign:pyralis_04", hearts: "2", powers: "dash,glide,ground_pound,charge_shot,shield", slots: "5", checkpoints: false, silver: "1:40", gold: "1:20" },
            catalogJson,
            t,
        );
        expect(out.warnings).toEqual([]);
        await apply(out.sql);
        const row = await env.DB.prepare("SELECT * FROM daily_challenges WHERE date = ?1").bind(date).first<any>();
        expect(row).toMatchObject({
            id: `daily-${date}`,
            source: "admin",
            level_source: "campaign",
            level_id: "pyralis_04",
            level_version: CATALOG.levels.pyralis_04!.revision,
            level_hash: CAMPAIGN.pyralis_04,
            level_title: CATALOG.levels.pyralis_04!.name,
            hearts: 2,
            slots: 5,
            powers: JSON.stringify(["charge_shot", "dash", "glide", "ground_pound", "shield"]),
            stations: 1,
            checkpoints: 0,
            silver_ms: 100000,
            gold_ms: 80000,
            gold_no_damage: 1,
            featured_player_id: null,
        });
        expect(row.rules_hash).toBe(
            await rulesHash(row.id, { hearts: 2, slots: 5, powers: ["charge_shot", "dash", "glide", "ground_pound", "shield"], stations: true, checkpoints: false }),
        );
        expect((await lastAudit())?.action).toBe("challenge_set_applied");
        // Defaults of a campaign level: its slots, start powers and medals.
        const defaults = await buildChallengeSql({ date: addDays(t, 4), level: "campaign:glacior_02" }, catalogJson, t);
        expect(defaults.rules).toMatchObject({ hearts: CATALOG.player_max_hearts, slots: 4, stations: true, checkpoints: true });
        expect(defaults.rules.powers).toEqual([...CATALOG.levels.glacior_02!.start_powers].sort());

        // Generated yesterday for today: by the time it is applied the day has begun.
        const before = await env.DB.prepare("SELECT * FROM daily_challenges WHERE date = ?1").bind(t).first();
        const late = await buildChallengeSql({ date: t, level: "campaign:sylvanox_01", hearts: "1" }, catalogJson, addDays(t, -1));
        await apply(late.sql);
        expect(await env.DB.prepare("SELECT * FROM daily_challenges WHERE date = ?1").bind(t).first()).toEqual(before);
        expect((await lastAudit())?.action).toBe("challenge_set_skipped");
        // A day in the past that never had a challenge is not created either.
        const past = await buildChallengeSql({ date: addDays(t, -5), level: "campaign:sylvanox_01" }, catalogJson, addDays(t, -6));
        await apply(past.sql);
        expect(await env.DB.prepare("SELECT 1 AS x FROM daily_challenges WHERE date = ?1").bind(addDays(t, -5)).first()).toBeNull();
    });

    it("takes a published community version, its fingerprint, title and author, and tells the creator once", async () => {
        const t = today();
        const author = await registerPlayer("Createur");
        const { id } = await publish(author);
        await expect(buildChallengeSql({ date: addDays(t, 2), level: `community:${id}@1` }, catalogJson, t)).rejects.toThrow(/--silver/);
        const out = await buildChallengeSql({ date: addDays(t, 2), level: `community:${id}@1`, silver: "2:30", gold: "1:50", goldNoDamage: false }, catalogJson, t);
        expect(out.rules).toMatchObject({ slots: 4, powers: [] });
        await apply(out.sql);
        const row = await env.DB.prepare("SELECT * FROM daily_challenges WHERE date = ?1").bind(addDays(t, 2)).first<any>();
        expect(row).toMatchObject({
            source: "admin",
            level_source: "community",
            level_id: id,
            level_version: 1,
            level_hash: LH,
            level_title: "La Tour Ardente",
            featured_player_id: author.id,
            silver_ms: 150000,
            gold_ms: 110000,
            gold_no_damage: 0,
        });
        // A version that is not published changes nothing.
        const unpublished = await buildChallengeSql({ date: addDays(t, 6), level: `community:${id}@7`, silver: "2:30", gold: "1:50" }, catalogJson, t);
        await apply(unpublished.sql);
        expect((await lastAudit())?.action).toBe("challenge_set_skipped");
        expect((await env.DB.prepare("SELECT level_source FROM daily_challenges WHERE date = ?1").bind(addDays(t, 6)).first<any>())?.level_source ?? "campaign").toBe("campaign");
        // Nothing of it shows before its day.
        expect(JSON.stringify((await api("GET", `/api/v1/challenges/daily?before=${addDays(t, 30)}`)).body)).not.toContain(id);
        expect((await api("GET", `/api/v1/challenges/daily-${addDays(t, 2)}/leaderboard`)).status).toBe(404);

        // The day it is the challenge: the author is told once (reads and the daily cron).
        const ch = await insertChallenge(t, { community: { id, version: 1, hash: LH, author: author.id, title: "La Tour Ardente" }, powers: [] });
        const cur = await api("GET", "/api/v1/challenges/daily/current");
        expect(JSON.stringify(cur.body)).not.toContain(`daily-${addDays(t, 2)}`);
        expect(cur.body.data.challenge.level).toEqual({
            source: "community",
            level_id: id,
            level_version: 1,
            level_hash: LH,
            title: "La Tour Ardente",
            author: { id: author.id, username: "Createur" },
        });
        await api("GET", "/api/v1/challenges/daily/current");
        const exec = createExecutionContext();
        await worker.scheduled!(createScheduledController({ scheduledTime: Date.now(), cron: "17 3 * * *" }), env as never, exec);
        await waitOnExecutionContext(exec);
        const notes = await api("GET", "/api/v1/notifications", { token: author.token });
        const featured = notes.body.data.notifications.filter((n: any) => n.kind === "challenge_featured");
        expect(featured).toHaveLength(1);
        expect(featured[0].text).toBe("Votre niveau « La Tour Ardente » est le défi du jour !");
        expect(featured[0].data).toMatchObject({ challenge_id: ch.id, level_id: id });

        // A run on the community level is checked against its version's geometry.
        const p = await registerPlayer("Joueuse");
        const body = runBody(ch, { time_ms: 62000 });
        const run = await submit(p, ch, body);
        expect(run.status).toBe(201);
        const up = await putReplay(p, run.body.data.run.id, await challengeReplay(ch, body, { levelId: "mon_niveau" }));
        expect(up.body.data.run.status).toBe("VERIFIED");

        // The author leaves: the challenge that has begun keeps everything but the link.
        const del = await api("DELETE", "/api/v1/players/me", { token: author.token, body: { password: author.password, confirm: "DELETE" } });
        expect(del.status).toBe(200);
        const kept = await env.DB.prepare("SELECT featured_player_id, level_title, rules_hash FROM daily_challenges WHERE id = ?1").bind(ch.id).first<any>();
        expect(kept).toEqual({ featured_player_id: null, level_title: "La Tour Ardente", rules_hash: ch.hash });
    });

    it("the daily cron draws the coming week", async () => {
        const exec = createExecutionContext();
        await worker.scheduled!(createScheduledController({ scheduledTime: Date.now(), cron: "17 3 * * *" }), env as never, exec);
        await waitOnExecutionContext(exec);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM daily_challenges WHERE source = 'auto'").first<{ n: number }>())?.n).toBe(8);
    });
});
