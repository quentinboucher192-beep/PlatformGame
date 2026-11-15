import { createExecutionContext, createScheduledController, env, waitOnExecutionContext } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import fixture from "../../tests/data/challenge_rules_fixture.json";
import catalogJson from "../src/challengeCatalog.json";
import {
    CATALOG,
    KINDS,
    RULE_PROFILES,
    SAFE_EXTRA_POWERS,
    addDays,
    canonicalRules,
    drawRules,
    drawable,
    isoWeek,
    parseChallengeId,
    periodBounds,
    periodOf,
    periodStreak,
    rulesHash,
    shiftPeriod,
    streakOf,
    tierOf,
    utcDate,
    type ChallengeKind,
    type ChallengeRules,
} from "../src/challenges";
import worker from "../src/index";
import { inflateContainer, parseReplay } from "../src/replay";
import {
    buildChallengeSql,
    isoWeek as scriptIsoWeek,
    parseArgs,
    periodBounds as scriptPeriodBounds,
    canonicalRules as scriptCanonical,
    sha256Hex as scriptSha256,
} from "../scripts/challenge-sql.mjs";
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

/** The hardest levels (3rd / 4th / last of a planet), for the draws of the weeks and months. */
const HARD: Record<string, string> = {
    sylvanox_03: "5".repeat(64),
    glacior_04: "6".repeat(64),
    noyau_02: "7".repeat(64),
};

async function seedCampaign(levels: Record<string, string> = CAMPAIGN): Promise<void> {
    const stmts = [];
    for (const [id, hash] of Object.entries(levels)) {
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
    kind: ChallengeKind;
    period: string;
    date: string;
    level_id: string;
    level_hash: string;
    rules: ChallengeRules;
    hash: string;
    canonical: string;
}

/**
 * Inserts a challenge directly (past or begun periods: the admin script refuses them): the
 * challenge of `kind` (daily by default) whose period contains the day `date`.
 */
async function insertChallenge(
    date: string,
    o: Partial<ChallengeRules> & {
        kind?: ChallengeKind;
        level?: string;
        community?: { id: string; version: number; hash: string; author: string; title: string };
    } = {},
): Promise<TestChallenge> {
    const kind = o.kind ?? "daily";
    const p = periodBounds(kind, periodOf(kind, date))!;
    const id = `${kind}-${p.period}`;
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
        `INSERT INTO challenges (id, kind, period, date, end_date, source, level_source, level_id, level_version, level_hash, level_title, hearts,
           slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, featured_player_id, created_at, updated_at)
         VALUES (?1, ?16, ?17, ?2, ?18, 'admin', ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, 90000, 70000, 1, ?14, ?15, ?15)`,
    )
        .bind(
            id,
            p.start,
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
            kind,
            p.period,
            p.end,
        )
        .run();
    return { id, kind, period: p.period, date: p.start, level_id: levelId, level_hash: levelHash, rules, hash, canonical: canonicalRules(id, rules) };
}

/** A run inserted directly (any status, in time or late), for the results and the streaks. */
function insertRun(ch: TestChallenge, who: TestPlayer, time: number, tier: string, status: string, late = 0, at = `${ch.date}T12:00:00.000Z`) {
    return env.DB.prepare(
        `INSERT INTO challenge_runs (id, challenge_id, player_id, attempt_id, time_ms, deaths, damage_taken, level_hash, rules_hash, game_version,
           tier, status, late, created_at) VALUES (?1, ?2, ?3, ?4, ?5, 0, 0, ?6, ?7, '2.32.0', ?8, ?9, ?10, ?11)`,
    ).bind(crypto.randomUUID(), ch.id, who.id, crypto.randomUUID(), time, ch.level_hash, ch.hash, tier, status, late, at);
}

function runBody(ch: TestChallenge, o: Record<string, unknown> = {}): Record<string, any> {
    return {
        attempt_id: crypto.randomUUID(),
        time_ms: 80000,
        deaths: 0,
        damage_taken: 0,
        level_hash: ch.level_hash,
        rules_hash: ch.hash,
        game_version: "2.32.0",
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

        const rows = (await env.DB.prepare("SELECT * FROM challenges WHERE kind = 'daily' ORDER BY date").all<any>()).results;
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
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM challenges WHERE kind = 'daily'").first<{ n: number }>())?.n).toBe(8);

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
        const rows = await env.DB.prepare("SELECT kind, period, COUNT(*) AS n FROM challenges GROUP BY kind, period").all<{ kind: string; n: number }>();
        // Days J to J+7, weeks S to S+2, months M to M+1.
        expect(rows.results.filter((r) => r.kind === "daily")).toHaveLength(8);
        expect(rows.results.filter((r) => r.kind === "weekly")).toHaveLength(3);
        expect(rows.results.filter((r) => r.kind === "monthly")).toHaveLength(2);
        for (const r of rows.results) expect(r.n).toBe(1);
    });

    it("never changes a challenge that has begun; missing days are drawn again", async () => {
        await api("GET", "/api/v1/challenges/daily/current");
        const t = today();
        const daily = (d: string) => `daily-${d}`;
        await expect(env.DB.prepare("UPDATE challenges SET hearts = 1 WHERE id = ?1").bind(daily(t)).run()).rejects.toThrow(/already started/);
        await expect(env.DB.prepare("UPDATE challenges SET level_id = 'pyralis_04' WHERE id = ?1").bind(daily(t)).run()).rejects.toThrow();
        // A coming day may still change (the administrator's choice), and the bookkeeping of today may.
        await env.DB.prepare("UPDATE challenges SET hearts = 1 WHERE id = ?1").bind(daily(addDays(t, 2))).run();
        await env.DB.prepare("UPDATE challenges SET featured_notified_at = 'x' WHERE id = ?1").bind(daily(t)).run();
        const before = await env.DB.prepare("SELECT * FROM challenges WHERE id = ?1").bind(daily(t)).first();
        await env.DB.prepare("DELETE FROM challenges WHERE id = ?1").bind(daily(addDays(t, 5))).run();
        await api("GET", "/api/v1/challenges/daily/current");
        expect(await env.DB.prepare("SELECT date FROM challenges WHERE id = ?1").bind(daily(addDays(t, 5))).first()).not.toBeNull();
        expect(await env.DB.prepare("SELECT * FROM challenges WHERE id = ?1").bind(daily(t)).first()).toEqual(before);
        // The same for the week and the month that have begun; the next ones may still change.
        const week = `weekly-${isoWeek(t)}`;
        const month = `monthly-${t.slice(0, 7)}`;
        await expect(env.DB.prepare("UPDATE challenges SET hearts = 1 WHERE id = ?1").bind(week).run()).rejects.toThrow(/already started/);
        await expect(env.DB.prepare("UPDATE challenges SET rules_hash = 'x' WHERE id = ?1").bind(month).run()).rejects.toThrow(/already started/);
        const nextWeek = `weekly-${shiftPeriod("weekly", isoWeek(t), 1)}`;
        expect((await env.DB.prepare("UPDATE challenges SET hearts = 1 WHERE id = ?1").bind(nextWeek).run()).meta.changes).toBe(1);
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

    it("refuses runs before the challenge and runs of other rules, levels or games; a run after its end is late", async () => {
        const t = today();
        const ch = await insertChallenge(t);
        const old = await insertChallenge(addDays(t, -2));
        const next = await insertChallenge(addDays(t, 1));
        const p = await registerPlayer("Nova");
        // 2.33: an ended challenge takes the run « hors délai » (a 2.32 game sends the same body).
        const ended = await submit(p, old, runBody(old));
        expect(ended.status).toBe(201);
        expect(ended.body.data.run).toMatchObject({ status: "PENDING", late: true });
        const inTime = await submit(p, ch, runBody(ch));
        expect(inTime.body.data.run.late).toBe(false);
        await env.DB.prepare("DELETE FROM challenge_runs").run();
        const early = await submit(p, next, runBody(next));
        expect(early.status).toBe(409);
        expect(early.body.error.code).toBe("CHALLENGE_NOT_STARTED");
        expect((await submit(p, "daily-2020-01-01", runBody(ch))).body.error.code).toBe("CHALLENGE_NOT_FOUND");
        expect((await submit(p, "daily-2026-02-30", runBody(ch))).status).toBe(404);
        expect((await submit(p, "weekly-1", runBody(ch))).status).toBe(404);
        expect((await submit(p, "weekly-2021-W53", runBody(ch))).status).toBe(404);
        expect((await submit(p, "monthly-2026-13", runBody(ch))).status).toBe(404);
        expect((await submit(p, "yearly-2026", runBody(ch))).status).toBe(404);
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
            game_version: "2.32.0",
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
        const run = (ch: TestChallenge, time: number, tier: string, status: string, who = p) => insertRun(ch, who, time, tier, status);
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
        const more = (d: string) => ({ kind: "daily", period: d, late: false, best_progress: null });
        expect(me.body.data.results).toEqual([
            { challenge_id: chs[3]!.id, date: days[3], best_time_ms: 88000, tier: "silver", status: "VERIFIED", ...more(days[3]!) },
            { challenge_id: chs[2]!.id, date: days[2], best_time_ms: 66000, tier: "gold", status: "PENDING", ...more(days[2]!) },
            { challenge_id: chs[1]!.id, date: days[1], best_time_ms: 50000, tier: null, status: "REJECTED", ...more(days[1]!) },
            { challenge_id: chs[0]!.id, date: days[0], best_time_ms: 80000, tier: "silver", status: "VERIFIED", ...more(days[0]!) },
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
        const row = await env.DB.prepare("SELECT * FROM challenges WHERE id = ?1").bind(`daily-${date}`).first<any>();
        expect(row).toMatchObject({
            id: `daily-${date}`,
            kind: "daily",
            period: date,
            date,
            end_date: addDays(date, 1),
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
        const before = await env.DB.prepare("SELECT * FROM challenges WHERE id = ?1").bind(`daily-${t}`).first();
        const late = await buildChallengeSql({ date: t, level: "campaign:sylvanox_01", hearts: "1" }, catalogJson, addDays(t, -1));
        await apply(late.sql);
        expect(await env.DB.prepare("SELECT * FROM challenges WHERE id = ?1").bind(`daily-${t}`).first()).toEqual(before);
        expect((await lastAudit())?.action).toBe("challenge_set_skipped");
        // A day in the past that never had a challenge is not created either.
        const past = await buildChallengeSql({ date: addDays(t, -5), level: "campaign:sylvanox_01" }, catalogJson, addDays(t, -6));
        await apply(past.sql);
        expect(await env.DB.prepare("SELECT 1 AS x FROM challenges WHERE id = ?1").bind(`daily-${addDays(t, -5)}`).first()).toBeNull();
    });

    it("takes a published community version, its fingerprint, title and author, and tells the creator once", async () => {
        const t = today();
        const author = await registerPlayer("Createur");
        const { id } = await publish(author);
        await expect(buildChallengeSql({ date: addDays(t, 2), level: `community:${id}@1` }, catalogJson, t)).rejects.toThrow(/--silver/);
        const out = await buildChallengeSql({ date: addDays(t, 2), level: `community:${id}@1`, silver: "2:30", gold: "1:50", goldNoDamage: false }, catalogJson, t);
        expect(out.rules).toMatchObject({ slots: 4, powers: [] });
        await apply(out.sql);
        const row = await env.DB.prepare("SELECT * FROM challenges WHERE id = ?1").bind(`daily-${addDays(t, 2)}`).first<any>();
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
        expect((await env.DB.prepare("SELECT level_source FROM challenges WHERE id = ?1").bind(`daily-${addDays(t, 6)}`).first<any>())?.level_source ?? "campaign").toBe(
            "campaign",
        );
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
        const kept = await env.DB.prepare("SELECT featured_player_id, level_title, rules_hash FROM challenges WHERE id = ?1").bind(ch.id).first<any>();
        expect(kept).toEqual({ featured_player_id: null, level_title: "La Tour Ardente", rules_hash: ch.hash });
    });

    it("the daily cron draws the coming days, weeks and months", async () => {
        const exec = createExecutionContext();
        await worker.scheduled!(createScheduledController({ scheduledTime: Date.now(), cron: "17 3 * * *" }), env as never, exec);
        await waitOnExecutionContext(exec);
        const counts = await env.DB.prepare("SELECT kind, COUNT(*) AS n FROM challenges WHERE source = 'auto' GROUP BY kind ORDER BY kind").all<{ kind: string; n: number }>();
        expect(counts.results).toEqual([
            { kind: "daily", n: 8 },
            { kind: "monthly", n: 2 },
            { kind: "weekly", n: 3 },
        ]);
    });
});

// ------------------------------------------------------------------------------------------
// 2.33: challenges of the week and of the month, calendar, late runs (CHALLENGES_PERIODS.md)

describe("periods (2.33)", () => {
    it("names the ISO 8601 weeks and the months, with their UTC days, on the server and in the admin script", () => {
        expect(isoWeek("2026-11-16")).toBe("2026-W47"); // a Monday (the contract's example)
        expect(isoWeek("2026-11-22")).toBe("2026-W47"); // its Sunday
        expect(isoWeek("2026-11-23")).toBe("2026-W48");
        expect(isoWeek("2026-12-31")).toBe("2026-W53"); // 2026 has 53 weeks
        expect(isoWeek("2027-01-03")).toBe("2026-W53");
        expect(isoWeek("2027-01-04")).toBe("2027-W01");
        expect(isoWeek("2024-12-30")).toBe("2025-W01"); // a Monday that belongs to the next year
        expect(periodBounds("weekly", "2026-W47")).toEqual({ kind: "weekly", period: "2026-W47", start: "2026-11-16", end: "2026-11-23" });
        expect(periodBounds("weekly", "2026-W53")).toMatchObject({ start: "2026-12-28", end: "2027-01-04" });
        expect(periodBounds("weekly", "2025-W01")).toMatchObject({ start: "2024-12-30", end: "2025-01-06" });
        expect(periodBounds("monthly", "2026-12")).toEqual({ kind: "monthly", period: "2026-12", start: "2026-12-01", end: "2027-01-01" });
        expect(periodBounds("monthly", "2027-02")).toMatchObject({ start: "2027-02-01", end: "2027-03-01" });
        expect(periodBounds("daily", "2026-11-16")).toEqual({ kind: "daily", period: "2026-11-16", start: "2026-11-16", end: "2026-11-17" });
        for (const [kind, bad] of [
            ["weekly", "2027-W53"], // 2027 has 52 weeks
            ["weekly", "2026-W00"],
            ["weekly", "2026-W54"],
            ["weekly", "2026-47"],
            ["monthly", "2026-13"],
            ["monthly", "2026-00"],
            ["monthly", "1999-12"],
            ["daily", "2026-02-30"],
        ] as const)
            expect(periodBounds(kind, bad), `${kind} ${bad}`).toBeNull();
        expect(parseChallengeId("weekly-2026-W47")).toMatchObject({ kind: "weekly", start: "2026-11-16" });
        expect(parseChallengeId("monthly-2026-11")).toMatchObject({ kind: "monthly", end: "2026-12-01" });
        for (const bad of ["weekly-2026-47", "daily-2026-W47", "monthly-2026-11-01", "Weekly-2026-W47", "weekly-2026-W47 ", "event-2026-11"])
            expect(parseChallengeId(bad), bad).toBeNull();
        expect(shiftPeriod("weekly", "2026-W53", 1)).toBe("2027-W01");
        expect(shiftPeriod("weekly", "2027-W01", -1)).toBe("2026-W53");
        expect(shiftPeriod("monthly", "2026-12", 1)).toBe("2027-01");
        expect(shiftPeriod("monthly", "2026-01", -1)).toBe("2025-12");
        expect(periodOf("weekly", "2027-01-02")).toBe("2026-W53");
        expect(periodOf("monthly", "2027-01-02")).toBe("2027-01");
        // The admin script computes the same periods (four years of days, every week, every month).
        for (let d = "2024-12-20"; d < "2029-01-10"; d = addDays(d, 1)) {
            expect(scriptIsoWeek(d), d).toBe(isoWeek(d));
            const w = periodBounds("weekly", isoWeek(d))!;
            expect(scriptPeriodBounds("weekly", isoWeek(d)), d).toEqual({ start: w.start, end: w.end });
            const m = periodBounds("monthly", d.slice(0, 7))!;
            expect(scriptPeriodBounds("monthly", d.slice(0, 7)), d).toEqual({ start: m.start, end: m.end });
        }
        expect(scriptPeriodBounds("weekly", "2027-W53")).toBeNull();
    });

    it("counts the streaks over consecutive weeks and months, across the years", () => {
        expect(periodStreak("weekly", ["2026-W51", "2026-W52", "2026-W53", "2027-W01"], "2027-W02")).toEqual({ current: 4, best: 4, last_period: "2027-W01" });
        expect(periodStreak("weekly", ["2026-W50", "2026-W52"], "2026-W52")).toEqual({ current: 1, best: 1, last_period: "2026-W52" });
        expect(periodStreak("weekly", ["2026-W40", "2026-W41"], "2026-W47")).toEqual({ current: 0, best: 2, last_period: "2026-W41" });
        expect(periodStreak("monthly", ["2026-11", "2026-12", "2027-01"], "2027-01")).toEqual({ current: 3, best: 3, last_period: "2027-01" });
        expect(periodStreak("monthly", ["2026-10", "2026-12"], "2027-02")).toEqual({ current: 0, best: 1, last_period: "2026-12" });
        expect(periodStreak("monthly", [], "2027-02")).toEqual({ current: 0, best: 0, last_period: null });
    });

    it("draws the weeks and the months from the hardest levels, with more demanding rules", () => {
        const pools = Object.fromEntries(
            KINDS.map((k) => [
                k,
                Object.entries(CATALOG.levels)
                    .filter(([, l]) => drawable(k, l))
                    .map(([id]) => id)
                    .sort(),
            ]),
        );
        expect(pools.monthly).toEqual(["aethra_04", "glacior_04", "noyau_02", "pyralis_04", "sylvanox_04"]);
        expect(pools.weekly).toEqual(["aethra_03", "aethra_04", "glacior_03", "glacior_04", "noyau_02", "pyralis_03", "pyralis_04", "sylvanox_03", "sylvanox_04"]);
        // The boss rush (EXTRÊME) is never drawn: only the administrator chooses it.
        for (const k of KINDS) expect(pools[k]).not.toContain("noyau_01");
        for (const kind of ["weekly", "monthly"] as const)
            for (const id of pools[kind]!) {
                const level = CATALOG.levels[id]!;
                for (let i = 0; i < 40; ++i) {
                    const r = drawRules(level, CATALOG, kind);
                    expect(RULE_PROFILES[kind].hearts).toContain(r.hearts);
                    expect(r.stations).toBe(true);
                    const extras = r.powers.filter((p) => !level.start_powers.includes(p));
                    expect(extras.length).toBeLessThanOrEqual(RULE_PROFILES[kind].extras);
                    for (const p of level.start_powers) expect(r.powers).toContain(p);
                    expect(r.slots).toBeGreaterThanOrEqual(level.slots);
                }
            }
    });
});

describe("challenges of the week and of the month (2.33)", () => {
    it("draws days to J+7, weeks to S+2 and months to M+1, and shows the current and previous ones only", async () => {
        await seedCampaign(HARD);
        const res = await api("GET", "/api/v1/challenges/current");
        expect(res.status).toBe(200);
        const t = today();
        const week = periodBounds("weekly", isoWeek(t))!;
        const month = periodBounds("monthly", t.slice(0, 7))!;
        const d = res.body.data;
        expect(Object.keys(d).sort()).toEqual(["daily", "monthly", "previous", "server_time", "weekly"]);
        expect(d.daily).toMatchObject({ id: `daily-${t}`, kind: "daily", period: t, date: t, starts_at: `${t}T00:00:00Z`, ends_at: `${addDays(t, 1)}T00:00:00Z` });
        expect(d.weekly).toMatchObject({
            id: `weekly-${week.period}`,
            kind: "weekly",
            period: week.period,
            date: week.start,
            starts_at: `${week.start}T00:00:00Z`,
            ends_at: `${week.end}T00:00:00Z`,
            source: "auto",
        });
        expect(d.monthly).toMatchObject({
            id: `monthly-${month.period}`,
            kind: "monthly",
            period: month.period,
            date: month.start,
            starts_at: `${month.start}T00:00:00Z`,
            ends_at: `${month.end}T00:00:00Z`,
        });
        // Nothing was played before: no previous challenge of any kind.
        expect(d.previous).toEqual({ daily: null, weekly: null, monthly: null });
        for (const c of [d.weekly, d.monthly]) expect(c.rules_hash).toBe(await rulesHash(c.id, c.rules));

        const rows = (await env.DB.prepare("SELECT * FROM challenges ORDER BY kind, date").all<any>()).results;
        const weeks = rows.filter((r) => r.kind === "weekly");
        const months = rows.filter((r) => r.kind === "monthly");
        expect(weeks.map((r) => r.period)).toEqual([0, 1, 2].map((i) => shiftPeriod("weekly", week.period, i)));
        expect(months.map((r) => r.period)).toEqual([month.period, shiftPeriod("monthly", month.period, 1)]);
        for (const r of weeks) {
            expect(["pyralis_04", "sylvanox_03", "glacior_04", "noyau_02"]).toContain(r.level_id);
            expect(r.end_date).toBe(addDays(r.date, 7));
            expect([1, 2, 3]).toContain(r.hearts);
        }
        for (const r of months) {
            expect(["pyralis_04", "glacior_04", "noyau_02"]).toContain(r.level_id);
            expect(JSON.parse(r.powers)).toEqual([...CATALOG.levels[r.level_id]!.start_powers].sort());
            expect([1, 2, 3]).toContain(r.hearts);
        }
        // Consecutive weeks never repeat a level (four candidates).
        for (let i = 1; i < weeks.length; ++i) expect(weeks[i].level_id).not.toBe(weeks[i - 1].level_id);

        // The next week and the next month exist but are never shown.
        const nextWeek = `weekly-${weeks[1].period}`;
        const nextMonth = `monthly-${months[1].period}`;
        expect(JSON.stringify(res.body)).not.toContain(nextWeek);
        expect(JSON.stringify(res.body)).not.toContain(nextMonth);
        for (const id of [nextWeek, nextMonth]) {
            expect((await api("GET", `/api/v1/challenges/${id}`)).status).toBe(404);
            expect((await api("GET", `/api/v1/challenges/${id}/leaderboard`)).status).toBe(404);
        }
        const cal = await api("GET", `/api/v1/challenges/calendar?month=${months[1].period}`);
        expect(cal.body.data.challenges).toEqual([]);
        // The challenge itself, by its id.
        const one = await api("GET", `/api/v1/challenges/${d.weekly.id}`);
        expect(one.status).toBe(200);
        expect(one.body.data).toEqual(d.weekly);
        for (const bad of ["weekly-2021-W53", "monthly-2026-13", "daily-x", "current2"])
            expect((await api("GET", `/api/v1/challenges/${bad}`)).body.error.code, bad).toBe("CHALLENGE_NOT_FOUND");

        // Reading again draws nothing and changes nothing.
        const again = await api("GET", "/api/v1/challenges/current");
        expect(again.body.data.weekly).toEqual(d.weekly);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM challenges").first<{ n: number }>())?.n).toBe(13);
    });

    it("shows the previous day, week and month", async () => {
        const t = today();
        const lastWeek = periodBounds("weekly", shiftPeriod("weekly", isoWeek(t), -1))!;
        const lastMonth = periodBounds("monthly", shiftPeriod("monthly", t.slice(0, 7), -1))!;
        const y = await insertChallenge(addDays(t, -1));
        const w = await insertChallenge(lastWeek.start, { kind: "weekly", level: "pyralis_04" });
        const m = await insertChallenge(lastMonth.start, { kind: "monthly", level: "pyralis_04" });
        const res = await api("GET", "/api/v1/challenges/current");
        expect(res.body.data.previous.daily.id).toBe(y.id);
        expect(res.body.data.previous.weekly).toMatchObject({ id: w.id, period: lastWeek.period, ends_at: `${lastWeek.end}T00:00:00Z` });
        expect(res.body.data.previous.monthly).toMatchObject({ id: m.id, period: lastMonth.period, ends_at: `${lastMonth.end}T00:00:00Z` });
        expect(res.body.data.weekly.id).toBe(`weekly-${isoWeek(t)}`);
    });

    it("gives the calendar of a month: its days, the weeks that overlap it, the month, and my results", async () => {
        const p = await registerPlayer("Nova");
        const other = await registerPlayer("Orion");
        const w40 = await insertChallenge("2026-09-30", { kind: "weekly", level: "pyralis_04" }); // 2026-09-28 -> 10-05
        const sept = await insertChallenge("2026-09-30");
        const oct5 = await insertChallenge("2026-10-05");
        const oct31 = await insertChallenge("2026-10-31");
        const w44 = await insertChallenge("2026-10-27", { kind: "weekly", level: "pyralis_04" }); // 2026-10-26 -> 11-02
        const month = await insertChallenge("2026-10-12", { kind: "monthly", level: "pyralis_04" });
        await insertChallenge("2026-09-03", { kind: "monthly", level: "pyralis_04" });
        await env.DB.batch([
            insertRun(oct5, p, 80000, "silver", "VERIFIED"),
            // A better time, but late: my result stays the one in time.
            insertRun(oct5, p, 60000, "gold", "VERIFIED", 1, "2026-11-10T08:00:00.000Z"),
            insertRun(w44, p, 95000, "bronze", "PENDING", 1, "2026-11-10T08:00:00.000Z"),
            insertRun(oct31, p, 50000, "gold", "REJECTED"),
            insertRun(month, other, 70000, "gold", "VERIFIED"),
        ]);
        const anon = await api("GET", "/api/v1/challenges/calendar?month=2026-10");
        expect(anon.status).toBe(200);
        expect(anon.body.data.month).toBe("2026-10");
        expect(anon.body.data.first_month).toBe("2026-09");
        expect(anon.body.data.me).toBeNull();
        expect(anon.body.data.challenges.map((c: any) => c.id)).toEqual([w40.id, month.id, oct5.id, w44.id, oct31.id]);
        expect(anon.body.data.challenges[0]).toMatchObject({ kind: "weekly", period: "2026-W40", date: "2026-09-28", ends_at: "2026-10-05T00:00:00Z" });
        const mine = await api("GET", "/api/v1/challenges/calendar?month=2026-10", { token: p.token });
        expect(mine.body.data.challenges).toEqual(anon.body.data.challenges);
        expect(mine.body.data.me).toEqual({
            [oct5.id]: { best_time_ms: 80000, tier: "silver", status: "VERIFIED", late: false, best_progress: null },
            [w44.id]: { best_time_ms: 95000, tier: "bronze", status: "PENDING", late: true, best_progress: null },
            [oct31.id]: { best_time_ms: 50000, tier: null, status: "REJECTED", late: false, best_progress: null },
        });
        const september = await api("GET", "/api/v1/challenges/calendar?month=2026-09", { token: p.token });
        expect(september.body.data.challenges.map((c: any) => c.id)).toEqual([`monthly-2026-09`, w40.id, sept.id]);
        expect(september.body.data.me).toEqual({});
        // Default: the current month.
        const cur = await api("GET", "/api/v1/challenges/calendar");
        expect(cur.body.data.month).toBe(today().slice(0, 7));
        expect(cur.body.data.challenges.map((c: any) => c.id)).toContain(`daily-${today()}`);
        expect(cur.body.data.challenges.every((c: any) => c.date <= today())).toBe(true);
        for (const bad of ["2026-13", "2026-1", "26-10", "1999-12", "2026-10-01"])
            expect((await api("GET", `/api/v1/challenges/calendar?month=${bad}`)).status, bad).toBe(400);
    });

    it("takes late runs: ranked only in « Depuis », marked late, never in a streak", async () => {
        const t = today();
        const old = await insertChallenge(addDays(t, -3), { hearts: 3 });
        const [a, b] = [await registerPlayer("Alpha"), await registerPlayer("Bravo")];
        // In time (inserted: the day is over) for Bravo, late for Alpha through the API.
        await env.DB.batch([insertRun(old, b, 85000, "silver", "VERIFIED")]);
        const late = await verifiedRun(a, old, 70000);
        expect(late.status).toBe("VERIFIED");
        expect(late.body.data.run.late).toBe(true);
        expect((await env.DB.prepare("SELECT late FROM challenge_runs WHERE id = ?1").bind(late.runId).first<any>())?.late).toBe(1);

        const live = await api("GET", `/api/v1/challenges/${old.id}/leaderboard`);
        expect(live.body.data.period).toBe("live");
        expect(live.body.data.entries.map((e: any) => [e.player.username, e.late])).toEqual([["Bravo", false]]);
        const since = await api("GET", `/api/v1/challenges/${old.id}/leaderboard?period=all`, { token: a.token });
        expect(since.body.data.period).toBe("all");
        expect(since.body.data.entries.map((e: any) => [e.rank, e.player.username, e.time_ms, e.late])).toEqual([
            [1, "Alpha", 70000, true],
            [2, "Bravo", 85000, false],
        ]);
        expect(since.body.data.me).toMatchObject({ rank: 1, late: true, status: "VERIFIED" });
        const liveMe = await api("GET", `/api/v1/challenges/${old.id}/leaderboard`, { token: a.token });
        expect(liveMe.body.data.me).toBeNull();
        expect((await api("GET", `/api/v1/challenges/${old.id}/leaderboard?period=later`)).status).toBe(400);
        expect((await api("GET", `/api/v1/challenges/${old.id}/leaderboard?board=points`)).status).toBe(400);

        const me = await api("GET", "/api/v1/challenges/me", { token: a.token });
        expect(me.body.data.streak).toEqual({ current: 0, best: 0, last_date: null });
        expect(me.body.data.streaks.daily).toEqual({ current: 0, best: 0, last_period: null });
        expect(me.body.data.results).toEqual([
            { challenge_id: old.id, kind: "daily", period: old.period, date: old.date, best_time_ms: 70000, tier: "gold", status: "VERIFIED", late: true, best_progress: null },
        ]);
    });

    it("keeps a streak per kind: consecutive days, ISO weeks and months with a run in time", async () => {
        const t = today();
        const p = await registerPlayer("Nova");
        const w = isoWeek(t);
        const weeks = [-3, -2, -1, 0].map((i) => periodBounds("weekly", shiftPeriod("weekly", w, i))!);
        const months = [-3, -2, -1].map((i) => periodBounds("monthly", shiftPeriod("monthly", t.slice(0, 7), i))!);
        const wc: TestChallenge[] = [];
        for (const x of weeks) wc.push(await insertChallenge(x.start, { kind: "weekly", level: "pyralis_04" }));
        const mc: TestChallenge[] = [];
        for (const x of months) mc.push(await insertChallenge(x.start, { kind: "monthly", level: "pyralis_04" }));
        const day = await insertChallenge(addDays(t, -1));
        await env.DB.batch([
            insertRun(wc[0]!, p, 90000, "silver", "VERIFIED"),
            // Week -2: only a late run, the streak breaks there.
            insertRun(wc[1]!, p, 90000, "silver", "VERIFIED", 1),
            insertRun(wc[2]!, p, 90000, "silver", "PENDING"),
            insertRun(wc[3]!, p, 90000, "silver", "VERIFIED"),
            insertRun(mc[0]!, p, 90000, "silver", "VERIFIED"),
            insertRun(mc[1]!, p, 90000, "silver", "VERIFIED"),
            // Last month: only a rejected run.
            insertRun(mc[2]!, p, 90000, "silver", "REJECTED"),
            insertRun(day, p, 90000, "silver", "VERIFIED"),
        ]);
        const me = await api("GET", "/api/v1/challenges/me", { token: p.token });
        expect(me.body.data.streaks).toEqual({
            daily: { current: 1, best: 1, last_period: day.period },
            weekly: { current: 2, best: 2, last_period: w },
            monthly: { current: 0, best: 2, last_period: months[1]!.period },
        });
        expect(me.body.data.streak).toEqual({ current: 1, best: 1, last_date: day.period });
        const ids = me.body.data.results.map((r: any) => r.challenge_id);
        expect(ids).toEqual(expect.arrayContaining([...wc, ...mc, day].map((c) => c.id)));
        expect(me.body.data.results.find((r: any) => r.challenge_id === wc[1]!.id)).toMatchObject({ kind: "weekly", period: weeks[1]!.period, late: true });
    });

    it("plays a weekly challenge like a daily one: runs, replay, tier, board", async () => {
        const t = today();
        const ch = await insertChallenge(t, { kind: "weekly", level: "pyralis_04", hearts: 2 });
        const p = await registerPlayer("Nova");
        const ok = await verifiedRun(p, ch, 65000);
        expect(ok.status).toBe("VERIFIED");
        expect(ok.body.data.run).toMatchObject({ tier: "gold", late: false });
        const board = await api("GET", `/api/v1/challenges/${ch.id}/leaderboard`);
        expect(board.body.data.entries.map((e: any) => [e.player.username, e.time_ms, e.late])).toEqual([["Nova", 65000, false]]);
        // The rules of another kind of challenge (same level) are refused.
        const daily = await insertChallenge(t, { level: "pyralis_04", hearts: 2 });
        const body = runBody(ch, { time_ms: 70000 });
        const run = await submit(p, ch, body);
        const up = await putReplay(p, run.body.data.run.id, await challengeReplay(ch, body, { challengeRules: daily.canonical }));
        expect(up.body.data.run.status).toBe("REJECTED");
        expect((await submit(p, ch, runBody(ch, { rules_hash: daily.hash }))).body.error.code).toBe("RULES_MISMATCH");
    });
});

describe("2.32 games keep working (2.33)", () => {
    it("keeps the daily routes and their answers", async () => {
        const t = today();
        await seedCampaign(HARD);
        await insertChallenge(addDays(t, -1));
        await insertChallenge(addDays(t, -1), { kind: "weekly", level: "pyralis_04" });
        const cur = await api("GET", "/api/v1/challenges/daily/current");
        expect(cur.status).toBe(200);
        expect(Object.keys(cur.body.data).sort()).toEqual(["challenge", "previous", "server_time"]);
        const c = cur.body.data.challenge;
        for (const k of ["id", "kind", "date", "starts_at", "ends_at", "source", "level", "rules", "rules_hash", "tiers"]) expect(c, k).toHaveProperty(k);
        expect(c).toMatchObject({ id: `daily-${t}`, kind: "daily", date: t });
        expect(cur.body.data.previous).toMatchObject({ id: `daily-${addDays(t, -1)}`, kind: "daily" });
        // The archive lists the days only (the weeks and the months exist too).
        const list = await api("GET", "/api/v1/challenges/daily?limit=30");
        expect(list.body.data.challenges.map((x: any) => x.id)).toEqual([`daily-${t}`, `daily-${addDays(t, -1)}`]);
        expect(list.body.data.challenges.every((x: any) => x.kind === "daily")).toBe(true);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM challenges WHERE kind <> 'daily'").first<{ n: number }>())?.n).toBeGreaterThan(0);
    });

    it("takes a 2.32 run sent after the end (no CHALLENGE_ENDED), keeps the days' streak and the default board", async () => {
        const t = today();
        const ch = await insertChallenge(addDays(t, -1));
        const old = await insertChallenge(addDays(t, -2));
        const week = await insertChallenge(t, { kind: "weekly", level: "pyralis_04" });
        const p = await registerPlayer("Nova");
        // The body of a 2.32 game, unchanged.
        const body = {
            attempt_id: crypto.randomUUID(),
            time_ms: 80000,
            deaths: 0,
            damage_taken: 0,
            level_hash: old.level_hash,
            rules_hash: old.hash,
            game_version: "2.32.0",
            has_replay: true,
        };
        const res = await submit(p, old, body);
        expect(res.status).toBe(201);
        expect(res.body.data).toMatchObject({ duplicate: false, run: { status: "PENDING", tier: "silver", time_ms: 80000 }, replay: { required: true } });
        expect((await putReplay(p, res.body.data.run.id, await challengeReplay(old, body))).body.data.run.status).toBe("VERIFIED");
        // Sent again from its offline queue: the same run.
        expect((await submit(p, old, body)).body.data).toMatchObject({ duplicate: true, run: { id: res.body.data.run.id } });
        // The leaderboard read by a 2.32 game (no period): the runs in time only.
        expect((await api("GET", `/api/v1/challenges/${old.id}/leaderboard`)).body.data.entries).toEqual([]);
        // The streak of the days: yesterday in time counts, the late run and the week do not.
        await env.DB.batch([insertRun(ch, p, 90000, "bronze", "VERIFIED"), insertRun(week, p, 90000, "bronze", "VERIFIED")]);
        const me = await api("GET", "/api/v1/challenges/me", { token: p.token });
        expect(me.body.data.streak).toEqual({ current: 1, best: 1, last_date: ch.date });
        for (const r of me.body.data.results) for (const k of ["challenge_id", "date", "best_time_ms", "tier", "status"]) expect(r, k).toHaveProperty(k);
    });
});

describe("administrator: weeks and months (2.33)", () => {
    const sqlStatements = (sql: string) => sql.split("\n").filter((l) => l.trim() && !l.startsWith("--"));
    async function apply(sql: string) {
        for (const s of sqlStatements(sql)) await env.DB.prepare(s).run();
    }
    const lastAudit = () => env.DB.prepare("SELECT action, details FROM audit_log WHERE action LIKE 'challenge_set_%' ORDER BY id DESC LIMIT 1").first<any>();

    it("reads --kind and --period (and the 2.32 --date)", () => {
        expect(parseArgs(["--kind", "weekly", "--period", "2026-W48", "--level", "campaign:pyralis_04", "--hearts", "2"])).toMatchObject({
            kind: "weekly",
            period: "2026-W48",
            level: "campaign:pyralis_04",
            hearts: "2",
        });
        expect(parseArgs(["--date", "2026-11-20", "--level", "campaign:glacior_02"])).toMatchObject({ date: "2026-11-20" });
        expect(() => parseArgs(["--kind"])).toThrow(/manquante/);
    });

    it("writes the SQL of a coming week or month, refuses a period that has begun", async () => {
        const t = today();
        await seedCampaign(HARD);
        const w = isoWeek(t);
        const next = shiftPeriod("weekly", w, 1);
        const nextBounds = periodBounds("weekly", next)!;
        await expect(buildChallengeSql({ kind: "weekly", period: w, level: "campaign:pyralis_04" }, catalogJson, t)).rejects.toThrow(/refusée/);
        await expect(buildChallengeSql({ kind: "monthly", period: t.slice(0, 7), level: "campaign:pyralis_04" }, catalogJson, t)).rejects.toThrow(/refusée/);
        await expect(buildChallengeSql({ kind: "weekly", period: "2027-W53", level: "campaign:pyralis_04" }, catalogJson, t)).rejects.toThrow(/invalide/);
        await expect(buildChallengeSql({ kind: "weekly", date: addDays(t, 9), level: "campaign:pyralis_04" }, catalogJson, t)).rejects.toThrow(/--date/);
        await expect(buildChallengeSql({ kind: "yearly", period: "2027", level: "campaign:pyralis_04" }, catalogJson, t)).rejects.toThrow(/--kind/);
        await expect(buildChallengeSql({ kind: "weekly", level: "campaign:pyralis_04" }, catalogJson, t)).rejects.toThrow(/--period/);

        await api("GET", "/api/v1/challenges/current"); // the coming weeks and months are drawn
        const out = await buildChallengeSql({ kind: "weekly", period: next, level: "campaign:glacior_04", hearts: "1", checkpoints: false }, catalogJson, t);
        expect(out).toMatchObject({ id: `weekly-${next}`, kind: "weekly", period: next, start: nextBounds.start, end: nextBounds.end });
        expect(out.sql).toContain("de la semaine");
        await apply(out.sql);
        const row = await env.DB.prepare("SELECT * FROM challenges WHERE id = ?1").bind(`weekly-${next}`).first<any>();
        expect(row).toMatchObject({
            kind: "weekly",
            period: next,
            date: nextBounds.start,
            end_date: nextBounds.end,
            source: "admin",
            level_id: "glacior_04",
            level_hash: HARD.glacior_04,
            hearts: 1,
            checkpoints: 0,
        });
        expect(row.rules_hash).toBe(await rulesHash(`weekly-${next}`, out.rules));
        expect((await lastAudit())?.action).toBe("challenge_set_applied");
        expect(JSON.parse((await lastAudit())!.details)).toMatchObject({ challenge: `weekly-${next}`, kind: "weekly" });

        // A month chosen ahead: a community level (only the administrator chooses one).
        const author = await registerPlayer("Createur");
        const { id } = await publish(author);
        const nextMonth = shiftPeriod("monthly", t.slice(0, 7), 1);
        const m = await buildChallengeSql({ kind: "monthly", period: nextMonth, level: `community:${id}@1`, silver: "3:00", gold: "2:00" }, catalogJson, t);
        expect(m.sql).toContain("du mois");
        await apply(m.sql);
        expect(
            await env.DB.prepare("SELECT kind, level_source, level_id, featured_player_id, end_date FROM challenges WHERE id = ?1").bind(`monthly-${nextMonth}`).first(),
        ).toEqual({
            kind: "monthly",
            level_source: "community",
            level_id: id,
            featured_player_id: author.id,
            end_date: `${shiftPeriod("monthly", nextMonth, 1)}-01`,
        });

        // Generated before the current week began, applied once it has: nothing changes.
        const before = await env.DB.prepare("SELECT * FROM challenges WHERE id = ?1").bind(`weekly-${w}`).first();
        const lateSql = await buildChallengeSql({ kind: "weekly", period: w, level: "campaign:sylvanox_03" }, catalogJson, addDays(periodBounds("weekly", w)!.start, -1));
        await apply(lateSql.sql);
        expect(await env.DB.prepare("SELECT * FROM challenges WHERE id = ?1").bind(`weekly-${w}`).first()).toEqual(before);
        expect((await lastAudit())?.action).toBe("challenge_set_skipped");
    });

    it("tells the creator with the sentence of the kind, once", async () => {
        const t = today();
        const author = await registerPlayer("Createur");
        const { id } = await publish(author);
        const community = { id, version: 1, hash: LH, author: author.id, title: "La Tour Ardente" };
        const week = await insertChallenge(t, { kind: "weekly", community, powers: [] });
        const month = await insertChallenge(t, { kind: "monthly", community, powers: [] });
        await api("GET", "/api/v1/challenges/current");
        await api("GET", "/api/v1/challenges/current");
        const exec = createExecutionContext();
        await worker.scheduled!(createScheduledController({ scheduledTime: Date.now(), cron: "17 3 * * *" }), env as never, exec);
        await waitOnExecutionContext(exec);
        const notes = await api("GET", "/api/v1/notifications", { token: author.token });
        const featured = notes.body.data.notifications.filter((n: any) => n.kind === "challenge_featured");
        expect(featured.map((n: any) => n.text).sort()).toEqual([
            "Votre niveau « La Tour Ardente » est le défi de la semaine !",
            "Votre niveau « La Tour Ardente » est le défi du mois !",
        ]);
        expect(featured.map((n: any) => n.data.challenge_id).sort()).toEqual([month.id, week.id].sort());
        const cur = await api("GET", "/api/v1/challenges/current");
        expect(cur.body.data.weekly.level.author).toEqual({ id: author.id, username: "Createur" });
    });
});
