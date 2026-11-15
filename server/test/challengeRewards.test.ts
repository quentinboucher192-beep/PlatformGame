import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import catalog from "../src/cosmeticCatalog.json";
import { periodBounds, periodOf, type ChallengeKind } from "../src/challenges";
import { emptyRewardProgress, resultsCount, rewardProgressFrom } from "../src/challengeRewards";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const DEFAULTS = { suit: "suit_standard", trail: "trail_none", frame: "frame_simple", title: "title_none", victory: "victory_classic" };
const REWARDS = { suit: "suit_champion", trail: "trail_comete_mois", frame: "frame_couronne", title: "title_defiant", victory: "victory_serie_feu" };
const ids = (c: Record<string, { id: string }>) => Object.fromEntries(Object.entries(c).map(([k, v]) => [k, v.id]));

/** A past challenge of `kind` whose period contains `date` (inserted directly, like the admin's). */
async function challenge(kind: ChallengeKind, date: string): Promise<string> {
    const p = periodBounds(kind, periodOf(kind, date))!;
    const id = `${kind}-${p.period}`;
    await env.DB.prepare(
        `INSERT OR IGNORE INTO challenges (id, kind, period, date, end_date, source, level_source, level_id, level_version, level_hash, level_title,
           hearts, slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, created_at, updated_at)
         VALUES (?1, ?2, ?3, ?4, ?5, 'admin', 'campaign', 'glacior_02', 1, ?6, 'Glacior', 3, 4, '[]', 1, 0, ?7, 90000, 70000, 1, ?8, ?8)`,
    )
        .bind(id, kind, p.period, p.start, p.end, "2".repeat(64), "a".repeat(64), new Date().toISOString())
        .run();
    return id;
}

/** A run of `who` on a challenge, with the tier and the status the server gave it. */
async function run(who: TestPlayer, challengeId: string, tier: "bronze" | "silver" | "gold", status: "PENDING" | "VERIFIED" | "REJECTED", late = 0) {
    await env.DB.prepare(
        `INSERT INTO challenge_runs (id, challenge_id, player_id, attempt_id, time_ms, deaths, damage_taken, level_hash, rules_hash, game_version,
           tier, status, late, created_at) VALUES (?1, ?2, ?3, ?4, 60000, 0, 0, ?5, ?6, '2.34.0', ?7, ?8, ?9, ?10)`,
    )
        .bind(crypto.randomUUID(), challengeId, who.id, crypto.randomUUID(), "2".repeat(64), "a".repeat(64), tier, status, late, new Date().toISOString())
        .run();
}

async function wear(p: TestPlayer, items: Partial<typeof DEFAULTS>) {
    const r = await api("PUT", "/api/v1/players/me/cosmetics", { token: p.token, body: { ...DEFAULTS, ...items } });
    expect(r.status).toBe(200);
    return ids(r.body.data.cosmetics);
}

/** The days from `first`, `n` of them ("2026-09-01", "2026-09-02"…). */
function days(first: string, n: number): string[] {
    return Array.from({ length: n }, (_, i) => new Date(Date.parse(`${first}T00:00:00Z`) + i * 86400000).toISOString().slice(0, 10));
}

describe("rewards of the challenges (2.34)", () => {
    it("counts the challenges by kind at a tier or better, and the longest series of periods in time", () => {
        const p = rewardProgressFrom([
            { kind: "daily", date: "2026-09-01", tier: 3, in_time: 1 },
            { kind: "daily", date: "2026-09-02", tier: 2, in_time: 1 },
            { kind: "daily", date: "2026-09-03", tier: 1, in_time: 0 }, // late: a tier, never a streak
            { kind: "daily", date: "2026-09-04", tier: 1, in_time: 1 },
            { kind: "daily", date: "2026-09-05", tier: 1, in_time: 1 },
            { kind: "daily", date: "2026-09-06", tier: 1, in_time: 1 },
            // Weeks across the years (2026-W53 does not exist: W52 then 2027-W01).
            { kind: "weekly", date: "2026-12-21", tier: 3, in_time: 1 },
            { kind: "weekly", date: "2026-12-28", tier: 3, in_time: 1 },
            { kind: "weekly", date: "2027-01-04", tier: 1, in_time: 1 },
            { kind: "monthly", date: "2026-12-01", tier: 2, in_time: 1 },
            { kind: "monthly", date: "2027-01-01", tier: 3, in_time: 1 },
            { kind: "unknown", date: "2026-09-01", tier: 3, in_time: 1 },
        ]);
        expect(p.daily).toEqual({ gold: 1, silver: 2, bronze: 6, best_streak: 3 });
        expect(p.weekly).toEqual({ gold: 2, silver: 2, bronze: 3, best_streak: 3 });
        expect(p.monthly).toEqual({ gold: 1, silver: 2, bronze: 2, best_streak: 2 });
        expect(resultsCount(p, "any", "gold")).toBe(4);
        expect(resultsCount(p, "weekly", "silver")).toBe(2);
        expect(resultsCount(emptyRewardProgress(), "any", "bronze")).toBe(0);
    });

    it("lets a player wear a challenge reward only once the server verified the results it asks for", async () => {
        const a = await registerPlayer("Nova");
        // Nothing yet: every reward of the challenges comes back as the default.
        expect(await wear(a, REWARDS)).toEqual(DEFAULTS);
        // A gold run waiting for its check, a rejected one, a verified silver: still not « Défiant ».
        const d1 = await challenge("daily", "2026-09-01");
        await run(a, d1, "gold", "PENDING");
        await run(a, d1, "gold", "REJECTED");
        await run(a, d1, "silver", "VERIFIED");
        expect((await wear(a, { title: "title_defiant" })).title).toBe("title_none");
        // A verified gold, even late (« hors délai »): « Défiant », nothing of the week or the month.
        const d2 = await challenge("daily", "2026-09-02");
        await run(a, d2, "gold", "VERIFIED", 1);
        expect(await wear(a, REWARDS)).toEqual({ ...DEFAULTS, title: "title_defiant" });
        // Gold on a week: the crown; gold on a month: the comet.
        await run(a, await challenge("weekly", "2026-09-07"), "gold", "VERIFIED");
        await run(a, await challenge("monthly", "2026-08-01"), "silver", "VERIFIED");
        expect(await wear(a, REWARDS)).toEqual({ ...DEFAULTS, title: "title_defiant", frame: "frame_couronne" });
        await run(a, await challenge("monthly", "2026-07-01"), "gold", "VERIFIED");
        expect(await wear(a, REWARDS)).toEqual({ ...DEFAULTS, title: "title_defiant", frame: "frame_couronne", trail: "trail_comete_mois" });
        // Ten golds of any kind: the suit (3 so far, 7 more days).
        for (const d of days("2026-10-01", 6)) await run(a, await challenge("daily", d), "gold", "VERIFIED");
        expect((await wear(a, REWARDS)).suit).toBe("suit_standard");
        await run(a, await challenge("daily", "2026-10-20"), "gold", "VERIFIED");
        expect((await wear(a, REWARDS)).suit).toBe("suit_champion");
        // Another player's results never count.
        const b = await registerPlayer("Orion");
        expect(await wear(b, REWARDS)).toEqual(DEFAULTS);
    });

    it("needs seven days in a row of verified runs received in time for « Série de feu »", async () => {
        const a = await registerPlayer("Nova");
        const week = days("2026-09-10", 7);
        // Six days in time, the seventh late: no series of seven.
        for (const d of week.slice(0, 6)) await run(a, await challenge("daily", d), "bronze", "VERIFIED");
        await run(a, await challenge("daily", week[6]!), "gold", "VERIFIED", 1);
        expect((await wear(a, { victory: "victory_serie_feu" })).victory).toBe("victory_classic");
        // A pending run in time on that day does not count either.
        await run(a, await challenge("daily", week[6]!), "bronze", "PENDING");
        expect((await wear(a, { victory: "victory_serie_feu" })).victory).toBe("victory_classic");
        // Verified in time: the series is complete, and stays won once the days go on.
        await run(a, await challenge("daily", week[6]!), "bronze", "VERIFIED");
        expect((await wear(a, { victory: "victory_serie_feu" })).victory).toBe("victory_serie_feu");
        const me = await api("GET", "/api/v1/challenges/me", { token: a.token });
        expect(me.status).toBe(200);
        expect(me.body.data.verified.daily).toEqual({ gold: 1, silver: 1, bronze: 7, best_streak: 7 });
        expect(me.body.data.verified.weekly).toEqual({ gold: 0, silver: 0, bronze: 0, best_streak: 0 });
    });

    it("gives the counts in GET /challenges/me and keeps a challenge reward when the achievements are declared again", async () => {
        const a = await registerPlayer("Nova");
        const empty = await api("GET", "/api/v1/challenges/me", { token: a.token });
        expect(empty.body.data.verified).toEqual(emptyRewardProgress());
        await run(a, await challenge("weekly", "2026-09-07"), "gold", "VERIFIED");
        await run(a, await challenge("daily", "2026-09-08"), "silver", "VERIFIED");
        await run(a, await challenge("daily", "2026-09-09"), "gold", "PENDING");
        const me = await api("GET", "/api/v1/challenges/me", { token: a.token });
        expect(me.body.data.verified).toEqual({
            daily: { gold: 0, silver: 1, bronze: 1, best_streak: 1 },
            weekly: { gold: 1, silver: 1, bronze: 1, best_streak: 1 },
            monthly: { gold: 0, silver: 0, bronze: 0, best_streak: 0 },
        });
        expect(await wear(a, { frame: "frame_couronne", title: "title_defiant" })).toMatchObject({ frame: "frame_couronne", title: "title_defiant" });
        // Declaring achievements checks what is worn again: the challenge rewards stay.
        expect((await api("PUT", "/api/v1/players/me/achievements", { token: a.token, body: { unlocked: [] } })).status).toBe(200);
        expect(ids((await api("GET", `/api/v1/players/${a.id}`)).body.data.cosmetics)).toMatchObject({ frame: "frame_couronne", title: "title_defiant" });
    });

    it("has the five rewards of the challenges in the server's catalogue", () => {
        type Item = { id: string; challenge?: { kind: string; tier: string; count: number }; challengeStreak?: { kind: string; days: number } };
        const by = Object.fromEntries((catalog as unknown as { items: Item[] }).items.map((i) => [i.id, i]));
        expect(by.title_defiant!.challenge).toEqual({ kind: "any", tier: "gold", count: 1 });
        expect(by.frame_couronne!.challenge).toEqual({ kind: "weekly", tier: "gold", count: 1 });
        expect(by.trail_comete_mois!.challenge).toEqual({ kind: "monthly", tier: "gold", count: 1 });
        expect(by.suit_champion!.challenge).toEqual({ kind: "any", tier: "gold", count: 10 });
        expect(by.victory_serie_feu!.challengeStreak).toEqual({ kind: "daily", days: 7 });
    });
});
