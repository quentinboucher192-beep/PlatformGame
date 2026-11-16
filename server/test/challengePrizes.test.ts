// Rewards of one challenge (2.43): an administrator gives a coming challenge its rewards
// (items of the catalogue, or titles created on the server) with a condition; the server grants
// them once the period is over, from the verified results received in time only, once, and
// notifies the winners; a server title is worn only by the players who won it.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { grantDuePrizes } from "../src/challenges";
import { addDays, periodBounds, periodOf, utcDate, type ChallengeKind } from "../src/challenges";
import { cleanTitleText, titleKey } from "../src/challengeRewards";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

const V = "/api/v1";
const HASH = "2".repeat(64);
const RULES_HASH = "a".repeat(64);

beforeEach(seedAll);

const today = () => utcDate();

async function admin(name = "NovaRider"): Promise<TestPlayer> {
    const p = await registerPlayer(name);
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
    return p;
}

/** A challenge inserted directly (its period may have begun or be over). */
async function challenge(date: string, o: { kind?: ChallengeKind; objective?: "speed" | "finish" } = {}): Promise<string> {
    const kind = o.kind ?? "daily";
    const p = periodBounds(kind, periodOf(kind, date))!;
    const id = `${kind}-${p.period}`;
    await env.DB.prepare(
        `INSERT INTO challenges (id, kind, period, date, end_date, source, level_source, level_id, level_version, level_hash, level_title,
           hearts, slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, objective, created_at, updated_at)
         VALUES (?1, ?2, ?3, ?4, ?5, 'admin', 'campaign', 'glacior_02', 1, ?6, 'Glacior', 3, 4, '[]', 1, 0, ?7, 90000, 70000, 1, ?8, ?9, ?9)`,
    )
        .bind(id, kind, p.period, p.start, p.end, HASH, RULES_HASH, o.objective ?? "speed", new Date().toISOString())
        .run();
    return id;
}

/** A finished run with the tier and status the server gave it. */
async function run(
    who: TestPlayer,
    challengeId: string,
    o: { time?: number; tier?: "bronze" | "silver" | "gold"; status?: "PENDING" | "VERIFIED" | "REJECTED"; late?: number; at?: string } = {},
) {
    await env.DB.prepare(
        `INSERT INTO challenge_runs (id, challenge_id, player_id, attempt_id, time_ms, deaths, damage_taken, level_hash, rules_hash, game_version,
           tier, status, late, created_at) VALUES (?1, ?2, ?3, ?4, ?5, 0, 0, ?6, ?7, '2.43.0', ?8, ?9, ?10, ?11)`,
    )
        .bind(
            crypto.randomUUID(),
            challengeId,
            who.id,
            crypto.randomUUID(),
            o.time ?? 80000,
            HASH,
            RULES_HASH,
            o.tier ?? "bronze",
            o.status ?? "VERIFIED",
            o.late ?? 0,
            o.at ?? new Date().toISOString(),
        )
        .run();
}

/** An unfinished attempt of « Finir le niveau », its progress and tier computed by the server. */
async function attempt(who: TestPlayer, challengeId: string, progress: number, tier: "bronze" | "silver" | null, at = new Date().toISOString(), late = 0) {
    await env.DB.prepare(
        `INSERT INTO challenge_attempts (id, challenge_id, player_id, attempt_id, time_ms, level_hash, rules_hash, game_version, claim, late, status,
           progress, tier, created_at, verified_at) VALUES (?1, ?2, ?3, ?4, 40000, ?5, ?6, '2.43.0', '{}', ?7, 'VERIFIED', ?8, ?9, ?10, ?10)`,
    )
        .bind(crypto.randomUUID(), challengeId, who.id, crypto.randomUUID(), HASH, RULES_HASH, late, progress, tier, at)
        .run();
}

/** Rewards set directly (a period that has begun: the route refuses it). */
async function rules(challengeId: string, list: { kind: "item" | "title"; ref: string; condition: "tier" | "top" | "finished"; value: number }[]) {
    await env.DB.batch(
        list.map((r, i) =>
            env.DB.prepare(
                "INSERT INTO challenge_reward_rules (challenge_id, position, reward_kind, reward_ref, condition, condition_value) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
            ).bind(challengeId, i, r.kind, r.ref, r.condition, r.value),
        ),
    );
}

async function createTitle(p: TestPlayer, text: string, color = "gold") {
    return api("POST", `${V}/admin/titles`, { token: p.token, body: { text, color } });
}

async function won(p: TestPlayer): Promise<any[]> {
    const r = await api("GET", `${V}/players/me/rewards`, { token: p.token });
    expect(r.status, JSON.stringify(r.body)).toBe(200);
    return r.body.data.rewards;
}

const refs = (list: any[]) => list.map((r) => r.ref).sort();
const ctx = () => ({ env, requestId: "test", params: {}, url: new URL("https://test.invalid/"), ip: "test" }) as any;

describe("rewards of a challenge: administration", () => {
    it("does not exist for a player, and needs a session", async () => {
        const p = await registerPlayer("Kestrel");
        const id = `daily-${addDays(today(), 3)}`;
        for (const [m, path, body] of [
            ["GET", `${V}/admin/titles`, undefined],
            ["POST", `${V}/admin/titles`, { text: "Pionnier", color: "gold" }],
            ["PUT", `${V}/admin/challenges/${id}/rewards`, { rewards: [] }],
        ] as const) {
            expect((await api(m, path, { body })).status, `${m} ${path}`).toBe(401);
            const res = await api(m, path, { token: p.token, body });
            expect(res.status, `${m} ${path}`).toBe(404);
            expect(res.body.error.code).toBe("NOT_FOUND");
        }
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM server_titles").first<{ n: number }>())!.n).toBe(0);
    });

    it("creates titles: trimmed, at most 32 characters, no control characters nor emoji, never twice", async () => {
        const a = await admin();
        const ok = await createTitle(a, "  Pionnier   du givre ", "cyan");
        expect(ok.status, JSON.stringify(ok.body)).toBe(201);
        expect(ok.body.data.title).toMatchObject({ text: "Pionnier du givre", color: "cyan" });
        expect(ok.body.data.title.id).toMatch(/^srv:\d+$/);
        for (const [text, color] of <[string, string][]>[
            ["x".repeat(33), "gold"],
            ["Bonjour\u0007", "gold"],
            ["Étoile ⭐", "gold"],
            ["   ", "gold"],
            ["Correct", "pink"],
        ])
            expect((await createTitle(a, text, color)).status, text).toBe(400);
        expect((await createTitle(a, "é".repeat(32), "violet")).status).toBe(201);
        const dup = await createTitle(a, "PIONNIER DU GIVRÉ", "gold");
        expect(dup.status).toBe(409);
        expect(dup.body.error.code).toBe("TITLE_EXISTS");
        expect(dup.body.error.details.title.id).toBe(ok.body.data.title.id);
        const list = await api("GET", `${V}/admin/titles`, { token: a.token });
        expect(list.body.data.titles.map((t: any) => t.text)).toEqual(["é".repeat(32), "Pionnier du givre"]);
        expect(list.body.data.titles[1].created_by).toBe("NovaRider");
        const journal = (await api("GET", `${V}/admin/log?kind=challenge`, { token: a.token })).body.data.entries;
        expect(journal.filter((e: any) => e.action === "admin_title_created")).toHaveLength(2);
        const refused = (await api("GET", `${V}/admin/log?kind=refused`, { token: a.token })).body.data.entries;
        expect(refused.some((e: any) => e.reason === "TITLE_EXISTS" && e.title?.text === "PIONNIER DU GIVRÉ")).toBe(true);
        expect(cleanTitleText("a​b")).toBeNull();
        expect(titleKey("Éclair  Noir")).toBe("eclair noir");
    });

    it("sets the rewards of a coming period, shows them in the calendar, and refuses them once it has begun", async () => {
        const a = await admin();
        const t = (await createTitle(a, "Pionnier du givre")).body.data.title;
        const day = addDays(today(), 3);
        const id = `daily-${day}`;
        const body = {
            rewards: [
                { kind: "title", ref: t.id, condition: "tier", value: 3 },
                { kind: "item", ref: "frame_couronne", condition: "top", value: 10 },
                { kind: "item", ref: "trail_comete_mois", condition: "finished" },
            ],
        };
        const res = await api("PUT", `${V}/admin/challenges/${id}/rewards`, { token: a.token, body });
        expect(res.status, JSON.stringify(res.body)).toBe(200);
        expect(res.body.data.rewards).toEqual([
            { kind: "title", ref: t.id, condition: "tier", value: 3, item: null, title: { id: t.id, text: "Pionnier du givre", color: "gold" } },
            { kind: "item", ref: "frame_couronne", condition: "top", value: 10, item: { id: "frame_couronne", category: "frame", name: "Couronne de la semaine" }, title: null },
            expect.objectContaining({ kind: "item", ref: "trail_comete_mois", condition: "finished", value: 0 }),
        ]);
        const month = await api("GET", `${V}/admin/challenges?month=${day.slice(0, 7)}`, { token: a.token });
        const period = month.body.data.periods.find((p: any) => p.id === id);
        expect(period.rewards.map((r: any) => r.ref)).toEqual([t.id, "frame_couronne", "trail_comete_mois"]);
        expect(period.rewards_granted_at).toBeNull();
        // Its uses, then the Journal.
        expect((await api("GET", `${V}/admin/titles`, { token: a.token })).body.data.titles[0].uses).toBe(1);
        const entry = (await api("GET", `${V}/admin/log?kind=challenge`, { token: a.token })).body.data.entries.find(
            (e: any) => e.action === "admin_challenge_rewards",
        );
        expect(entry.challenge.id).toBe(id);
        expect(entry.challenge.rewards.map((r: any) => r.name)).toEqual(["Pionnier du givre", "Couronne de la semaine", expect.any(String)]);
        // Replaced as a whole; [] removes them.
        const again = await api("PUT", `${V}/admin/challenges/${id}/rewards`, { token: a.token, body: { rewards: [body.rewards[1]] } });
        expect(again.body.data.rewards).toHaveLength(1);
        expect((await api("PUT", `${V}/admin/challenges/${id}/rewards`, { token: a.token, body: { rewards: [] } })).body.data.rewards).toEqual([]);
        // A period that has begun never changes (nor its rewards): refused and written to the Journal.
        for (const started of [`daily-${today()}`, `daily-${addDays(today(), -2)}`, `monthly-${periodOf("monthly", today())}`]) {
            const r = await api("PUT", `${V}/admin/challenges/${started}/rewards`, { token: a.token, body });
            expect(r.status, started).toBe(409);
            expect(r.body.error.code).toBe("CHALLENGE_STARTED");
        }
        const refused = (await api("GET", `${V}/admin/log?kind=refused`, { token: a.token })).body.data.entries;
        expect(refused.filter((e: any) => e.reason === "CHALLENGE_STARTED" && e.challenge?.action === "rewards")).toHaveLength(3);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM challenge_reward_rules").first<{ n: number }>())!.n).toBe(0);
    });

    it("checks every reward: known item (not a default), existing title, condition values, no reward twice, six at most", async () => {
        const a = await admin();
        const id = `daily-${addDays(today(), 2)}`;
        const t = (await createTitle(a, "Comète")).body.data.title;
        const bad = [
            [{ kind: "item", ref: "frame_inconnu", condition: "finished" }],
            [{ kind: "item", ref: "frame_simple", condition: "finished" }],
            [{ kind: "title", ref: "srv:999999", condition: "finished" }],
            [{ kind: "title", ref: "title_explorateur", condition: "finished" }],
            [{ kind: "item", ref: "frame_couronne", condition: "top", value: 0 }],
            [{ kind: "item", ref: "frame_couronne", condition: "top", value: 101 }],
            [{ kind: "item", ref: "frame_couronne", condition: "tier", value: 4 }],
            [{ kind: "item", ref: "frame_couronne", condition: "tier" }],
            [{ kind: "item", ref: "frame_couronne", condition: "podium", value: 1 }],
            [
                { kind: "title", ref: t.id, condition: "tier", value: 1 },
                { kind: "title", ref: t.id, condition: "top", value: 3 },
            ],
            Array.from({ length: 7 }, () => ({ kind: "item", ref: "frame_couronne", condition: "finished" })),
        ];
        for (const rewards of bad) {
            const r = await api("PUT", `${V}/admin/challenges/${id}/rewards`, { token: a.token, body: { rewards } });
            expect(r.status, JSON.stringify(rewards)).toBe(400);
        }
        expect((await api("PUT", `${V}/admin/challenges/${id}/rewards`, { token: a.token, body: { rewards: [], extra: 1 } })).status).toBe(400);
    });
});

describe("rewards of a challenge: granted once the period is over", () => {
    it("grants by the final ranking (ties: the earlier submission), the tier and the finish, from verified runs in time only, once", async () => {
        const a = await admin();
        const t = (await createTitle(a, "Champion du jour", "violet")).body.data.title;
        const day = addDays(today(), -3);
        const id = await challenge(day);
        await rules(id, [
            { kind: "title", ref: t.id, condition: "top", value: 1 },
            { kind: "item", ref: "frame_couronne", condition: "top", value: 2 },
            { kind: "item", ref: "suit_champion", condition: "tier", value: 3 },
            { kind: "item", ref: "trail_comete_mois", condition: "finished", value: 0 },
        ]);
        const [first, second, third, gold, pending, late, rejected] = await Promise.all(
            ["Aster", "Borealis", "Cygnus", "Draco", "Eridan", "Fornax", "Gemini"].map((n) => registerPlayer(n)),
        );
        // A tie on the time: the earlier submission ranks first.
        await run(second!, id, { time: 60000, tier: "gold", at: `${day}T10:00:00.000Z` });
        await run(first!, id, { time: 60000, tier: "gold", at: `${day}T09:00:00.000Z` });
        await run(third!, id, { time: 75000, tier: "silver", at: `${day}T08:00:00.000Z` });
        await run(gold!, id, { time: 69000, tier: "gold", status: "PENDING" });
        await run(gold!, id, { time: 85000, tier: "silver" });
        await attempt(gold!, id, 99, null);
        await run(pending!, id, { time: 30000, tier: "gold", status: "PENDING" });
        await run(late!, id, { time: 20000, tier: "gold", late: 1 });
        await run(rejected!, id, { time: 10000, tier: "gold", status: "REJECTED" });
        // The challenge of today is running: nothing to grant there.
        const running = await challenge(today());
        await rules(running, [{ kind: "item", ref: "frame_couronne", condition: "finished", value: 0 }]);
        await run(first!, running, { time: 60000, tier: "gold" });
        // Notifications turned off by one winner: the rewards all the same, no notification.
        await api("PUT", `${V}/notifications/settings`, { token: third!.token, body: { challenge_reward: false } });

        expect(refs(await won(first!))).toEqual([t.id, "frame_couronne", "suit_champion", "trail_comete_mois"].sort());
        expect(refs(await won(second!))).toEqual(["frame_couronne", "suit_champion", "trail_comete_mois"]);
        expect(refs(await won(third!))).toEqual(["trail_comete_mois"]);
        expect(refs(await won(gold!))).toEqual(["trail_comete_mois"]);
        for (const p of [pending!, late!, rejected!]) expect(await won(p)).toEqual([]);
        const mine = (await won(first!)).find((r) => r.kind === "title");
        expect(mine).toMatchObject({ challenge: { id, kind: "daily", date: day, title: "Glacior" }, title: { id: t.id, text: "Champion du jour", color: "violet" }, item: null });
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM challenge_reward_grants WHERE challenge_id = ?1").bind(running).first<{ n: number }>())!.n).toBe(0);

        // One notification per winner, with what was won.
        const notes = async (p: TestPlayer) =>
            (await api("GET", `${V}/notifications`, { token: p.token })).body.data.notifications.filter((n: any) => n.kind === "challenge_reward");
        const n1 = await notes(first!);
        expect(n1).toHaveLength(1);
        expect(n1[0].data.challenge_id).toBe(id);
        expect(n1[0].text).toContain("« Champion du jour »");
        expect(n1[0].text).toContain(`Défi du jour du ${day.slice(8, 10)}/${day.slice(5, 7)}`);
        expect(await notes(third!)).toHaveLength(0);
        expect(await notes(pending!)).toHaveLength(0);

        // Once: a second pass (cron, readings) grants nothing more, notifies nobody again.
        const before = (await env.DB.prepare("SELECT COUNT(*) AS n FROM player_won_rewards").first<{ n: number }>())!.n;
        expect(await grantDuePrizes(ctx(), { limit: 50 })).toBe(0);
        await env.DB.prepare("DELETE FROM challenge_reward_grants").run();
        expect(await grantDuePrizes(ctx(), { limit: 50 })).toBe(0);
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM player_won_rewards").first<{ n: number }>())!.n).toBe(before);
        expect(await notes(first!)).toHaveLength(1);
        // The board read after a run verified late: the rewards already granted stay as they were.
        await env.DB.prepare("UPDATE challenge_runs SET status = 'VERIFIED' WHERE player_id = ?1 AND challenge_id = ?2").bind(pending!.id, id).run();
        await api("GET", `${V}/challenges/${id}/leaderboard`);
        expect(refs(await won(first!))).toContain(t.id);
        // Rewards never taken back: the rules removed, the rewards stay.
        await env.DB.prepare("DELETE FROM challenge_reward_rules").run();
        expect(refs(await won(second!))).toEqual(["frame_couronne", "suit_champion", "trail_comete_mois"]);
    });

    it("« Finir le niveau »: the tiers of the attempts and of the finished runs, the progress board for the ranking", async () => {
        const id = await challenge(addDays(today(), -4), { objective: "finish" });
        await rules(id, [
            { kind: "item", ref: "frame_couronne", condition: "tier", value: 2 },
            { kind: "item", ref: "suit_champion", condition: "tier", value: 1 },
            { kind: "item", ref: "victory_serie_feu", condition: "top", value: 2 },
            { kind: "item", ref: "trail_comete_mois", condition: "finished", value: 0 },
        ]);
        const [finisher, silver, bronze, none, lateSilver] = await Promise.all(["Hydra", "Indus", "Lyra", "Mensa", "Norma"].map((n) => registerPlayer(n)));
        await run(finisher!, id, { time: 90000, tier: "silver" }); // finished with damage: Argent
        await attempt(silver!, id, 85, "silver");
        await attempt(bronze!, id, 60, "bronze");
        await attempt(none!, id, 30, null);
        await attempt(lateSilver!, id, 90, "silver", new Date().toISOString(), 1);
        // The cron's pass.
        expect(await grantDuePrizes(ctx(), { limit: 50 })).toBeGreaterThan(0);
        expect(refs(await won(finisher!))).toEqual(["frame_couronne", "suit_champion", "trail_comete_mois", "victory_serie_feu"]);
        expect(refs(await won(silver!))).toEqual(["frame_couronne", "suit_champion", "victory_serie_feu"]);
        expect(refs(await won(bronze!))).toEqual(["suit_champion"]);
        expect(await won(none!)).toEqual([]);
        expect(await won(lateSilver!)).toEqual([]);
    });

    it("lets a player wear a server title only once won, shown in its colour on the profile and the boards; an item won unlocks", async () => {
        const a = await admin();
        const t = (await createTitle(a, "Étoile filante", "silver")).body.data.title;
        const other = (await createTitle(a, "Jamais gagné")).body.data.title;
        const id = await challenge(addDays(today(), -2));
        await rules(id, [
            { kind: "title", ref: t.id, condition: "finished", value: 0 },
            { kind: "item", ref: "frame_couronne", condition: "finished", value: 0 },
        ]);
        const p = await registerPlayer("Orion");
        const q = await registerPlayer("Pavo");
        await run(p, id, { time: 70000 });
        const wear = async (who: TestPlayer, title: string, frame = "frame_simple") =>
            api("PUT", `${V}/players/me/cosmetics`, {
                token: who.token,
                body: { suit: "suit_standard", trail: "trail_none", frame, title, victory: "victory_classic" },
            });
        // Before the grant: refused (the defaults).
        expect((await wear(p, t.id, "frame_couronne")).body.data.cosmetics.title.id).toBe("title_none");
        expect(refs(await won(p))).toEqual(["frame_couronne", t.id].sort());
        const worn = (await wear(p, t.id, "frame_couronne")).body.data.cosmetics;
        expect(worn.title).toEqual({ id: t.id, look: "Étoile filante", name: "Étoile filante", color: "silver" });
        expect(worn.frame.id).toBe("frame_couronne");
        // Never a title not won, nor one of another player, nor a malformed one.
        expect((await wear(p, other.id)).body.data.cosmetics.title.id).toBe("title_none");
        expect((await wear(q, t.id)).body.data.cosmetics.title.id).toBe("title_none");
        expect((await wear(p, "srv:abc")).status).toBe(400);
        await wear(p, t.id, "frame_couronne");
        const profile = await api("GET", `${V}/players/${p.id}`);
        expect(profile.body.data.cosmetics.title).toMatchObject({ id: t.id, name: "Étoile filante", color: "silver" });
        const board = await api("GET", `${V}/challenges/${id}/leaderboard`);
        expect(board.body.data.entries[0].player.title).toEqual({ id: t.id, name: "Étoile filante", color: "silver" });
        // Declaring achievements again never takes a won reward off.
        await api("PUT", `${V}/players/me/achievements`, { token: p.token, body: { unlocked: [] } });
        expect((await api("GET", `${V}/players/${p.id}`)).body.data.cosmetics.title.id).toBe(t.id);
    });

    it("shows a challenge's rewards to the players once it has begun", async () => {
        const a = await admin();
        const t = (await createTitle(a, "Aube", "cyan")).body.data.title;
        const id = await challenge(today());
        await rules(id, [
            { kind: "title", ref: t.id, condition: "top", value: 3 },
            { kind: "item", ref: "frame_couronne", condition: "tier", value: 3 },
        ]);
        const c = await api("GET", `${V}/challenges/${id}`);
        expect(c.body.data.rewards).toEqual([
            { kind: "title", ref: t.id, condition: "top", value: 3, item: null, title: { id: t.id, text: "Aube", color: "cyan" } },
            { kind: "item", ref: "frame_couronne", condition: "tier", value: 3, item: { id: "frame_couronne", category: "frame", name: "Couronne de la semaine" }, title: null },
        ]);
        const cur = await api("GET", `${V}/challenges/current`);
        expect(cur.body.data.daily.rewards).toHaveLength(2);
        expect(cur.body.data.weekly?.rewards ?? []).toEqual([]);
    });
});
