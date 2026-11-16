// ADMINISTRATION › Calendrier des défis (2.41): the routes exist only for an administrator, show
// the coming challenges (never to the players), set a coming period with the checks of
// scripts/challenge-set.mjs, put back the server's draw, never touch a period that has begun,
// and write every change and refusal to the Journal. And the objective « Finir le niveau »:
// only on a level with a route, its tiers computed by the server from the verified progress.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import catalogJson from "../src/challengeCatalog.json";
import { CATALOG, addDays, periodBounds, periodOf, progressTierOf, rulesHash, runTierOf, shiftPeriod, utcDate, type ChallengeKind, type ChallengeRules } from "../src/challenges";
import { validateChallengeSpec } from "../src/challengeSpec.mjs";
import { buildChallengeSql } from "../scripts/challenge-sql.mjs";
import { EXIT, SPAWN, api, buildReplay, registerPlayer, seedAll, type TestPlayer } from "./helpers";
import { LH, publish } from "./publish";

const V = "/api/v1";

/** Campaign levels registered on the test server, at the revision of the catalogue. */
const CAMPAIGN: Record<string, string> = {
    sylvanox_01: "1".repeat(64),
    glacior_02: "2".repeat(64),
    pyralis_04: "3".repeat(64),
    noyau_01: "4".repeat(64),
    sylvanox_03: "5".repeat(64),
    glacior_04: "6".repeat(64),
    noyau_02: "7".repeat(64),
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

async function makeAdmin(p: TestPlayer): Promise<void> {
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
}

async function admin(): Promise<TestPlayer> {
    const nova = await registerPlayer("NovaRider");
    await makeAdmin(nova);
    return nova;
}

/** A straight route (no boss, no ambush): an attempt's score is the share of the route reached. */
const ROUTE: [number, number][] = Array.from({ length: 60 }, (_, i) => [SPAWN.x + ((EXIT.x + 10 - SPAWN.x) * i) / 59, SPAWN.y + ((EXIT.y + 40 - SPAWN.y) * i) / 59]);
const LAYOUT = { route: ROUTE, bosses: [], ambushes: [] };

async function withLayout(levelId: string): Promise<void> {
    await env.DB.prepare("UPDATE level_versions SET race = ?2 WHERE level_id = ?1").bind(levelId, JSON.stringify(LAYOUT)).run();
}

function put(p: TestPlayer | null, id: string, body: unknown) {
    return api("PUT", `${V}/admin/challenges/${id}`, { token: p?.token, body });
}

function reset(p: TestPlayer | null, id: string) {
    return api("DELETE", `${V}/admin/challenges/${id}`, { token: p?.token });
}

async function log(p: TestPlayer, kind = "all") {
    return (await api("GET", `${V}/admin/log?kind=${kind}`, { token: p.token })).body.data.entries as any[];
}

interface Started {
    id: string;
    date: string;
    period: string;
    level_hash: string;
    rules: ChallengeRules;
    hash: string;
    canonical: string;
}

/** A challenge inserted directly (a period that has begun: the routes refuse it). */
async function insertChallenge(date: string, o: { kind?: ChallengeKind; objective?: "speed" | "finish"; level?: string; goldNoDamage?: boolean } = {}): Promise<Started> {
    const kind = o.kind ?? "daily";
    const p = periodBounds(kind, periodOf(kind, date))!;
    const id = `${kind}-${p.period}`;
    const levelId = o.level ?? "glacior_02";
    const rules: ChallengeRules = { hearts: 3, slots: 4, powers: ["dash"], stations: true, checkpoints: false };
    const hash = await rulesHash(id, rules);
    await env.DB.prepare(
        `INSERT INTO challenges (id, kind, period, date, end_date, source, level_source, level_id, level_version, level_hash, level_title, hearts,
           slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, objective, created_at, updated_at)
         VALUES (?1, ?2, ?3, ?4, ?5, 'admin', 'campaign', ?6, ?7, ?8, ?9, 3, 4, '["dash"]', 1, 0, ?10, ?11, ?11, ?12, ?13, ?14, ?14)`,
    )
        .bind(
            id,
            kind,
            p.period,
            p.start,
            p.end,
            levelId,
            CATALOG.levels[levelId]!.revision,
            CAMPAIGN[levelId]!,
            CATALOG.levels[levelId]!.name,
            hash,
            7_200_000,
            o.goldNoDamage === false ? 0 : 1,
            o.objective ?? "finish",
            new Date().toISOString(),
        )
        .run();
    const { canonicalRules } = await import("../src/challenges");
    return { id, date: p.start, period: p.period, level_hash: CAMPAIGN[levelId]!, rules, hash, canonical: canonicalRules(id, rules) };
}

describe("ADMINISTRATION › Calendrier des défis: access", () => {
    it("does not exist for a player, and needs a session", async () => {
        const p = await registerPlayer("Kestrel");
        const id = `daily-${addDays(today(), 3)}`;
        for (const [m, path, body] of [
            ["GET", `${V}/admin/challenges`, undefined],
            ["GET", `${V}/admin/challenges/levels`, undefined],
            ["PUT", `${V}/admin/challenges/${id}`, { level: { source: "campaign", id: "glacior_02" } }],
            ["DELETE", `${V}/admin/challenges/${id}`, undefined],
        ] as const) {
            const anon = await api(m, path, { body });
            expect(anon.status, `${m} ${path}`).toBe(401);
            const res = await api(m, path, { token: p.token, body });
            expect(res.status, `${m} ${path}`).toBe(404);
            expect(res.body.error.code).toBe("NOT_FOUND");
        }
        // Nothing was written by the player's attempts.
        const rows = await env.DB.prepare("SELECT COUNT(*) AS n FROM challenges WHERE source = 'admin'").first<{ n: number }>();
        expect(rows?.n).toBe(0);
    });
});

describe("ADMINISTRATION › Calendrier des défis: the month", () => {
    it("lists the days, weeks and month with the coming draws, never shown to the players", async () => {
        const nova = await admin();
        const t = today();
        const month = t.slice(0, 7);
        const res = await api("GET", `${V}/admin/challenges?month=${month}`, { token: nova.token });
        expect(res.status).toBe(200);
        const d = res.body.data;
        expect(d).toMatchObject({ month, today: t, ahead: { daily: 7, weekly: 2, monthly: 1 } });
        const byId = new Map<string, any>(d.periods.map((p: any) => [p.id, p]));
        // Every day of the month, the weeks that overlap it, the month.
        const days = d.periods.filter((p: any) => p.kind === "daily");
        expect(days.length).toBe((Date.parse(`${periodBounds("monthly", month)!.end}T00:00:00Z`) - Date.parse(`${month}-01T00:00:00Z`)) / 86400000);
        expect(d.periods.filter((p: any) => p.kind === "monthly").map((p: any) => p.id)).toEqual([`monthly-${month}`]);
        expect(d.periods.some((p: any) => p.kind === "weekly" && p.id === `weekly-${periodOf("weekly", t)}`)).toBe(true);
        // Today: running, locked; the coming days drawn (J+7) are there, editable.
        expect(byId.get(`daily-${t}`)).toMatchObject({ state: "running", editable: false, challenge: { source: "auto", kind: "daily" } });
        const tomorrow = addDays(t, 1);
        if (tomorrow.startsWith(month)) {
            const p = byId.get(`daily-${tomorrow}`);
            expect(p).toMatchObject({ state: "coming", editable: true, draws_on: null });
            expect(p.challenge.level.source).toBe("campaign");
            expect(p.challenge.rules_hash).toBe(await rulesHash(p.challenge.id, p.challenge.rules));
            expect(p.challenge.objective).toBe("speed");
        }
        // A day beyond the drawn window says when it is drawn.
        const far = addDays(t, 20);
        const next = await api("GET", `${V}/admin/challenges?month=${far.slice(0, 7)}`, { token: nova.token });
        const farDay = next.body.data.periods.find((p: any) => p.id === `daily-${far}`);
        expect(farDay).toMatchObject({ state: "coming", editable: true, challenge: null, draws_on: addDays(far, -7) });
        // The month after the current one is drawn (M+1), shown to the admin only.
        const nextMonth = shiftPeriod("monthly", month, 1);
        const nm = await api("GET", `${V}/admin/challenges?month=${nextMonth}`, { token: nova.token });
        expect(nm.body.data.periods.find((p: any) => p.id === `monthly-${nextMonth}`).challenge).not.toBeNull();
        const player = await api("GET", `${V}/challenges/calendar?month=${nextMonth}`);
        expect(JSON.stringify(player.body)).not.toContain(`monthly-${nextMonth}`);
        expect((await api("GET", `${V}/admin/challenges?month=2026-13`, { token: nova.token })).status).toBe(400);
    });

    it("lists the campaign levels with their draw kinds and route, and searches the community levels", async () => {
        const nova = await admin();
        await withLayout("noyau_01");
        const campaign = await api("GET", `${V}/admin/challenges/levels?source=campaign`, { token: nova.token });
        expect(campaign.status).toBe(200);
        const byId = new Map<string, any>(campaign.body.data.levels.map((l: any) => [l.level_id, l]));
        expect(byId.size).toBe(Object.keys(catalogJson.levels).length);
        expect(byId.get("glacior_02")).toMatchObject({ available: true, has_race: false, draw_kinds: ["daily"], title: CATALOG.levels.glacior_02!.name });
        expect(byId.get("noyau_01")).toMatchObject({ available: true, has_race: true, draw_kinds: [] });
        // In the catalogue but not ranked by this server: listed, not available.
        expect(byId.get("aethra_01")).toMatchObject({ available: false });
        const search = await api("GET", `${V}/admin/challenges/levels?source=campaign&q=${encodeURIComponent(CATALOG.levels.glacior_02!.name.slice(3, 9))}`, {
            token: nova.token,
        });
        expect(search.body.data.levels.map((l: any) => l.level_id)).toContain("glacior_02");

        const author = await registerPlayer("Createur");
        const { id } = await publish(author);
        const community = await api("GET", `${V}/admin/challenges/levels?source=community`, { token: nova.token });
        expect(community.status).toBe(200);
        expect(community.body.data.levels).toHaveLength(1);
        expect(community.body.data.levels[0]).toMatchObject({ level_id: id, version: 1, author: { username: "Createur" }, has_race: false });
        const none = await api("GET", `${V}/admin/challenges/levels?source=community&q=zzzz`, { token: nova.token });
        expect(none.body.data.levels).toHaveLength(0);
    });
});

describe("ADMINISTRATION › Calendrier des défis: setting a period", () => {
    it("sets a coming day, keeps it hidden from the players, then puts back the draw", async () => {
        const nova = await admin();
        const t = today();
        await api("GET", `${V}/admin/challenges`, { token: nova.token });
        const day = addDays(t, 2);
        const id = `daily-${day}`;
        const drawn = await env.DB.prepare("SELECT source, level_title FROM challenges WHERE id = ?1").bind(id).first<{ source: string; level_title: string }>();
        expect(drawn?.source).toBe("auto");

        const res = await put(nova, id, {
            level: { source: "campaign", id: "glacior_02" },
            hearts: 3,
            slots: 4,
            powers: ["grapple", "dash"],
            stations: true,
            checkpoints: false,
            objective: "speed",
            silver_ms: 105000,
            gold_ms: 80000,
            gold_no_damage: true,
        });
        expect(res.status, JSON.stringify(res.body)).toBe(200);
        const c = res.body.data.challenge;
        expect(c).toMatchObject({
            id,
            source: "admin",
            objective: "speed",
            rules: { hearts: 3, slots: 4, powers: ["dash", "grapple"], stations: true, checkpoints: false },
            tiers: { silver_ms: 105000, gold_ms: 80000, gold_no_damage: true },
            replaced_title: drawn!.level_title,
            level: { source: "campaign", level_id: "glacior_02", level_hash: CAMPAIGN.glacior_02, level_version: CATALOG.levels.glacior_02!.revision },
        });
        // The canonical rules' hash (the game and its replays), the objective outside it.
        expect(c.rules_hash).toBe(await rulesHash(id, { hearts: 3, slots: 4, powers: ["dash", "grapple"], stations: true, checkpoints: false }));
        // Still never revealed to a player before its day.
        expect((await api("GET", `${V}/challenges/${id}`)).status).toBe(404);

        // Set again: still the draw it replaced.
        const again = await put(nova, id, { level: { source: "campaign", id: "pyralis_04" } });
        expect(again.status).toBe(200);
        expect(again.body.data.challenge).toMatchObject({
            source: "admin",
            replaced_title: drawn!.level_title,
            // The catalogue's defaults: the level's slots, start powers, medals, the player's hearts.
            rules: { hearts: CATALOG.player_max_hearts, slots: CATALOG.levels.pyralis_04!.slots, powers: [...CATALOG.levels.pyralis_04!.start_powers].sort() },
            tiers: { silver_ms: CATALOG.levels.pyralis_04!.silver_ms, gold_ms: CATALOG.levels.pyralis_04!.gold_ms },
        });

        // « Revenir au tirage »: drawn again at once.
        const back = await reset(nova, id);
        expect(back.status).toBe(200);
        expect(back.body.data.challenge).toMatchObject({ id, source: "auto", replaced_title: null });
        expect(back.body.data.draws_on).toBeNull();
        const row = await env.DB.prepare("SELECT source FROM challenges WHERE id = ?1").bind(id).first<{ source: string }>();
        expect(row?.source).toBe("auto");
        // Already the draw: refused.
        const twice = await reset(nova, id);
        expect(twice.status).toBe(409);
        expect(twice.body.error.code).toBe("NOT_CHOSEN");

        // A day beyond the drawn window: set, then put back: drawn on its day.
        const far = addDays(t, 30);
        expect((await put(nova, `daily-${far}`, { level: { source: "campaign", id: "sylvanox_01" } })).status).toBe(200);
        const farBack = await reset(nova, `daily-${far}`);
        expect(farBack.body.data).toEqual({ challenge: null, draws_on: addDays(far, -7) });

        // A week and a month too.
        const week = shiftPeriod("weekly", periodOf("weekly", t), 1);
        const w = await put(nova, `weekly-${week}`, { level: { source: "campaign", id: "glacior_04" }, hearts: 2 });
        expect(w.body.data.challenge).toMatchObject({ kind: "weekly", period: week, source: "admin", rules: { hearts: 2 } });
        const month = shiftPeriod("monthly", periodOf("monthly", t), 1);
        const author = await registerPlayer("Createur");
        const { id: levelId } = await publish(author);
        const m = await put(nova, `monthly-${month}`, { level: { source: "community", id: levelId, version: 1 }, silver_ms: 150000, gold_ms: 110000 });
        expect(m.status, JSON.stringify(m.body)).toBe(200);
        expect(m.body.data.challenge.level).toMatchObject({ source: "community", level_id: levelId, level_version: 1, level_hash: LH, author: { username: "Createur" } });
    });

    it("refuses invalid rules with the script's checks, and writes each refusal to the Journal", async () => {
        const nova = await admin();
        const id = `daily-${addDays(today(), 3)}`;
        const author = await registerPlayer("Createur");
        const { id: levelId } = await publish(author);
        const cases: [Record<string, unknown>, string, string][] = [
            [{ level: { source: "campaign", id: "glacior_02" }, powers: ["dash", "vol"] }, "powers", "inconnu"],
            [{ level: { source: "campaign", id: "glacior_02" }, powers: ["dash", "dash"] }, "powers", "double"],
            [{ level: { source: "campaign", id: "glacior_02" }, slots: 1, powers: ["dash", "glide"] }, "powers", "emplacements"],
            [{ level: { source: "campaign", id: "glacior_02" }, silver_ms: 60000, gold_ms: 70000 }, "gold_ms", "Or"],
            [{ level: { source: "campaign", id: "inconnu" } }, "level", "catalogue"],
            [{ level: { source: "community", id: levelId, version: 1 } }, "silver_ms", "communauté"],
            [{ level: { source: "campaign", id: "glacior_02" }, objective: "finish", bronze_progress: 80, silver_progress: 60 }, "silver_progress", "Bronze"],
            [{ level: { source: "campaign", id: "glacior_02" }, objective: "finish", bronze_progress: 5 }, "bronze_progress", "10"],
            [{ level: { source: "campaign", id: "glacior_02" }, hearts: 0 }, "hearts", "entre"],
            [{ level: { source: "campaign", id: "glacior_02" }, hearts: 3, role: "admin" }, "role", "inconnu"],
            [{ level: "campaign:glacior_02" }, "level", "objet"],
            [{ level: { source: "campaign", id: "glacior_02" }, objective: "plus_vite" }, "objective", "valeur"],
        ];
        for (const [body, field, words] of cases) {
            const res = await put(nova, id, body);
            expect(res.status, JSON.stringify(body)).toBe(400);
            expect(res.body.error.code).toBe("VALIDATION_ERROR");
            const err = (res.body.error.details as { field: string; message: string }[]).find((e) => e.field === field);
            expect(err, JSON.stringify(res.body.error.details)).toBeDefined();
            expect(err!.message).toContain(words);
        }
        // A level the server does not rank, a version not published.
        const unranked = await put(nova, id, { level: { source: "campaign", id: "aethra_01" } });
        expect(unranked.status).toBe(409);
        expect(unranked.body.error.code).toBe("LEVEL_UNAVAILABLE");
        const unpublished = await put(nova, id, { level: { source: "community", id: levelId, version: 7 }, silver_ms: 150000, gold_ms: 110000 });
        expect(unpublished.body.error.code).toBe("LEVEL_UNAVAILABLE");
        const refused = await log(nova, "refused");
        expect(refused).toHaveLength(cases.length + 2);
        expect(refused.every((e) => e.action === "admin_refused" && e.challenge?.id === id && e.actor.username === "NovaRider")).toBe(true);
        expect(refused.map((e) => e.reason)).toContain("LEVEL_UNAVAILABLE");
        expect(refused.filter((e) => e.reason === "VALIDATION_ERROR")).toHaveLength(cases.length);
        // Nothing changed: still the server's draw (or nothing, for a day not drawn yet).
        const row = await env.DB.prepare("SELECT source FROM challenges WHERE id = ?1").bind(id).first<{ source: string }>();
        expect(row?.source ?? "auto").toBe("auto");
    });

    it("never changes nor removes a period that has begun or is over, and writes the refusal to the Journal", async () => {
        const nova = await admin();
        const t = today();
        await api("GET", `${V}/admin/challenges`, { token: nova.token });
        const before = await env.DB.prepare("SELECT * FROM challenges WHERE id = ?1").bind(`daily-${t}`).first<any>();
        for (const id of [`daily-${t}`, `daily-${addDays(t, -3)}`, `weekly-${periodOf("weekly", t)}`, `monthly-${periodOf("monthly", t)}`]) {
            const res = await put(nova, id, { level: { source: "campaign", id: "glacior_02" } });
            expect(res.status, id).toBe(409);
            expect(res.body.error.code).toBe("CHALLENGE_STARTED");
            const del = await reset(nova, id);
            expect(del.status, id).toBe(409);
            expect(del.body.error.code).toBe("CHALLENGE_STARTED");
        }
        expect(await env.DB.prepare("SELECT * FROM challenges WHERE id = ?1").bind(`daily-${t}`).first<any>()).toEqual(before);
        const refused = await log(nova, "refused");
        expect(refused).toHaveLength(8);
        expect(refused.every((e) => e.reason === "CHALLENGE_STARTED")).toBe(true);
        expect(refused.map((e) => e.challenge.action).sort()).toEqual(["reset", "reset", "reset", "reset", "set", "set", "set", "set"]);
        expect(refused.find((e) => e.challenge.id === `daily-${t}`).challenge).toMatchObject({ kind: "daily", period: t, start: t });
        // A malformed id: not found.
        expect((await put(nova, "daily-2026-02-30", { level: { source: "campaign", id: "glacior_02" } })).status).toBe(404);
        expect((await reset(nova, "yearly-2027")).status).toBe(404);
    });

    it("writes the sentence of each change to the Journal (kind « challenge »)", async () => {
        const nova = await admin();
        const t = today();
        await api("GET", `${V}/admin/challenges`, { token: nova.token });
        const id = `daily-${addDays(t, 4)}`;
        const drawn = await env.DB.prepare("SELECT level_title FROM challenges WHERE id = ?1").bind(id).first<{ level_title: string }>();
        await put(nova, id, {
            level: { source: "campaign", id: "glacior_02" },
            hearts: 3,
            checkpoints: false,
            silver_ms: 105000,
            gold_ms: 80000,
        });
        await reset(nova, id);
        const entries = await log(nova, "challenge");
        expect(entries.map((e) => e.action)).toEqual(["admin_challenge_reset", "admin_challenge_set"]);
        const set = entries[1];
        expect(set.actor.username).toBe("NovaRider");
        expect(set.challenge).toMatchObject({
            id,
            kind: "daily",
            period: addDays(t, 4),
            level: { source: "campaign", id: "glacior_02", title: CATALOG.levels.glacior_02!.name },
            rules: { hearts: 3, checkpoints: false },
            objective: "speed",
            tiers: { silver_ms: 105000, gold_ms: 80000, gold_no_damage: true },
            replaced: { source: "auto", title: drawn!.level_title },
        });
        expect(entries[0].challenge).toMatchObject({ id, removed: { level: { id: "glacior_02" } } });
        expect(entries[0].challenge.drawn.title).toBeTruthy();
        // The other filters.
        expect((await log(nova, "role")).length).toBe(0);
        expect((await log(nova, "all")).length).toBe(2);
        expect(JSON.stringify(await log(nova))).not.toContain("ip_hash");
        expect((await api("GET", `${V}/admin/log?kind=defis`, { token: nova.token })).status).toBe(400);
    });
});

describe("« Finir le niveau » (2.41)", () => {
    it("is refused on a level without a route, and set on one with a route", async () => {
        const nova = await admin();
        const id = `daily-${addDays(today(), 5)}`;
        const body = { level: { source: "campaign", id: "glacior_02" }, objective: "finish" };
        const res = await put(nova, id, body);
        expect(res.status).toBe(409);
        expect(res.body.error.code).toBe("FINISH_NEEDS_ROUTE");
        expect((await log(nova, "refused"))[0]).toMatchObject({ reason: "FINISH_NEEDS_ROUTE", challenge: { id } });
        await withLayout("glacior_02");
        const ok = await put(nova, id, { ...body, silver_progress: 85, gold_no_damage: false });
        expect(ok.status, JSON.stringify(ok.body)).toBe(200);
        expect(ok.body.data.challenge).toMatchObject({
            objective: "finish",
            progress_available: true,
            tiers: { bronze_progress: 50, silver_progress: 85, gold_no_damage: false, silver_ms: 7_200_000, gold_ms: 7_200_000 },
        });
        // The objective is not part of the canonical rules (the game and its replays).
        expect(ok.body.data.challenge.rules_hash).toBe(await rulesHash(id, ok.body.data.challenge.rules));
    });

    it("computes the tiers on the server from the verified progress, ranks by progress, counts for the rewards", async () => {
        await withLayout("glacior_02");
        const ch = await insertChallenge(today(), { objective: "finish" });
        const [ana, ben, cleo, dan] = [await registerPlayer("Ana"), await registerPlayer("Ben"), await registerPlayer("Cleo"), await registerPlayer("Dan")];

        // Unfinished attempts: the share of the route reached (no boss, no ambush), from the replay.
        const attempt = async (p: TestPlayer, k: number, claim: Record<string, unknown> = {}) => {
            const body = {
                attempt_id: crypto.randomUUID(),
                time_ms: 40000,
                level_hash: ch.level_hash,
                rules_hash: ch.hash,
                game_version: "2.41.0",
                claim: { finished: false, bosses: [], waves: [] },
                ...claim,
            };
            const res = await api("POST", `${V}/challenges/${ch.id}/attempts`, { token: p.token, body });
            if (res.status !== 201) return res;
            const at = (x: number) => ({ x: SPAWN.x + (EXIT.x + 10 - SPAWN.x) * x, y: SPAWN.y + (EXIT.y + 40 - SPAWN.y) * x });
            const replay = await buildReplay({
                levelId: "glacior_02",
                levelHash: ch.level_hash,
                attemptId: body.attempt_id,
                durationMs: body.time_ms,
                collectibles: 0,
                finish: false,
                challengeRules: ch.canonical,
                health: 3,
                extraEvents: [{ frame: 1_000_000, type: 3, arg: 0, value: 0 }],
                path: (i, n) => at(((k / 59) * i) / (n - 1)),
            });
            return api("PUT", `${V}/challenges/attempts/${body.attempt_id}/replay`, {
                token: p.token,
                raw: replay,
                headers: { "content-type": "application/octet-stream" },
            });
        };
        // The game never sends a tier nor a score.
        expect((await attempt(ana, 50, { tier: "silver" })).status).toBe(400);
        const silver = await attempt(ana, 50); // 84.7 %
        expect(silver.body.data.attempt).toMatchObject({ status: "VERIFIED", tier: "silver" });
        expect(silver.body.data.attempt.progress).toBeGreaterThanOrEqual(80);
        const bronze = await attempt(ben, 35); // 59.3 %
        expect(bronze.body.data.attempt).toMatchObject({ status: "VERIFIED", tier: "bronze" });
        const none = await attempt(cleo, 20); // 33.9 %
        expect(none.body.data.attempt).toMatchObject({ status: "VERIFIED", tier: null });

        // A finished run: Or without damage; with damage (Or asks for none): Argent.
        const run = async (p: TestPlayer, damage: number) => {
            const body = {
                attempt_id: crypto.randomUUID(),
                time_ms: 80000,
                deaths: 0,
                damage_taken: damage,
                level_hash: ch.level_hash,
                rules_hash: ch.hash,
                game_version: "2.41.0",
                has_replay: true,
            };
            const res = await api("POST", `${V}/challenges/${ch.id}/runs`, { token: p.token, body });
            expect(res.status).toBe(201);
            const replay = await buildReplay({
                levelId: "glacior_02",
                levelHash: ch.level_hash,
                attemptId: body.attempt_id,
                durationMs: body.time_ms,
                challengeRules: ch.canonical,
                health: 3,
                extraEvents: damage ? [{ frame: 100, type: 8, arg: 0, value: damage }] : [],
            });
            const up = await api("PUT", `${V}/challenges/runs/${res.body.data.run.id}/replay`, {
                token: p.token,
                raw: replay,
                headers: { "content-type": "application/octet-stream" },
            });
            expect(up.status).toBe(201);
            return up.body.data.run;
        };
        expect(await run(dan, 0)).toMatchObject({ tier: "gold", status: "VERIFIED" });
        expect(await run(cleo, 2)).toMatchObject({ tier: "silver" });

        // The challenge's board is the progress one: finished first (the first to finish), then the furthest.
        const board = await api("GET", `${V}/challenges/${ch.id}/leaderboard`);
        expect(board.status).toBe(200);
        expect(board.body.data.board).toBe("progress");
        expect(board.body.data.entries.map((e: any) => [e.player.username, e.finished, e.tier])).toEqual([
            ["Dan", true, "gold"],
            ["Cleo", true, "silver"],
            ["Ana", false, "silver"],
            ["Ben", false, "bronze"],
        ]);
        // My result: the attempt's tier (no time).
        const me = await api("GET", `${V}/challenges/me`, { token: ben.token });
        expect(me.body.data.results[0]).toMatchObject({ challenge_id: ch.id, best_time_ms: null, tier: "bronze", status: "VERIFIED" });
        expect(me.body.data.streaks.daily.current).toBe(1);
        // The rewards count the verified tiers (an attempt's too).
        expect(me.body.data.verified.daily).toMatchObject({ bronze: 1, silver: 0, gold: 0, best_streak: 1 });
        const anaMe = await api("GET", `${V}/challenges/me`, { token: ana.token });
        expect(anaMe.body.data.verified.daily).toMatchObject({ bronze: 1, silver: 1, gold: 0 });
        const challenge = await api("GET", `${V}/challenges/${ch.id}`);
        expect(challenge.body.data).toMatchObject({ objective: "finish", tiers: { bronze_progress: 50, silver_progress: 80 } });
    });

    it("leaves the « Vitesse » challenges as they were", async () => {
        expect(runTierOf({ objective: "speed", silver_ms: 90000, gold_ms: 70000, gold_no_damage: 1 }, 80000, 0)).toBe("silver");
        expect(runTierOf({ objective: "finish", silver_ms: 7_200_000, gold_ms: 7_200_000, gold_no_damage: 1 }, 80000, 1)).toBe("silver");
        expect(runTierOf({ objective: "finish", silver_ms: 7_200_000, gold_ms: 7_200_000, gold_no_damage: 0 }, 80000, 1)).toBe("gold");
        expect(progressTierOf({ objective: "speed", bronze_progress: 50, silver_progress: 80 }, 90)).toBeNull();
        expect(progressTierOf({ objective: "finish", bronze_progress: 50, silver_progress: 80 }, 79.9)).toBe("bronze");
        expect(progressTierOf({ objective: "finish", bronze_progress: 50, silver_progress: 80 }, 49.9)).toBeNull();
        expect(progressTierOf({ objective: "finish", bronze_progress: 50, silver_progress: 80 }, null)).toBeNull();
    });

    it("the shared checks and the script write the same objective", async () => {
        const t = today();
        const v = validateChallengeSpec({ kind: "daily", period: addDays(t, 2), level: { source: "campaign", id: "noyau_01" }, objective: "finish" }, CATALOG, t);
        expect(v).toMatchObject({ objective: "finish", bronze_progress: 50, silver_progress: 80, silver_ms: 7_200_000, gold_ms: 7_200_000 });
        expect(() => validateChallengeSpec({ kind: "daily", period: t, level: { source: "campaign", id: "noyau_01" } }, CATALOG, t)).toThrow(/refusée/);
        // The script's SQL only applies « Finir le niveau » on a version with a route.
        const sql = async (level: string) =>
            (await buildChallengeSql({ kind: "daily", period: addDays(t, 3), level: `campaign:${level}`, objective: "finish", silverPct: "90" }, catalogJson, t)).sql;
        const statements = (s: string) => s.split("\n").filter((l) => l.trim() && !l.startsWith("--"));
        for (const s of statements(await sql("glacior_02"))) await env.DB.prepare(s).run();
        expect(await env.DB.prepare("SELECT action FROM audit_log ORDER BY id DESC LIMIT 1").first<{ action: string }>()).toEqual({ action: "challenge_set_skipped" });
        await withLayout("glacior_02");
        for (const s of statements(await sql("glacior_02"))) await env.DB.prepare(s).run();
        const row = await env.DB.prepare("SELECT objective, silver_progress, source FROM challenges WHERE id = ?1").bind(`daily-${addDays(t, 3)}`).first<any>();
        expect(row).toEqual({ objective: "finish", silver_progress: 90, source: "admin" });
        await expect(buildChallengeSql({ date: addDays(t, 3), level: "campaign:glacior_02", objective: "vite" }, catalogJson, t)).rejects.toThrow(/--objective/);
    });
});
