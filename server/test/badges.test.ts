import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import catalog from "../src/achievementCatalog.json";
import game from "../../data/achievements/achievements.json";
import { BADGE_STEPS, MAX_BADGES, badgeSlots } from "../src/badges";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const STARS = (catalog as { stars: Record<string, number> }).stars;
const ALL_IDS = Object.keys(STARS);

/** Achievement ids worth `stars` stars or a little more (`extra` first, never those in `without`). */
function worth(stars: number, extra: string[] = [], without: string[] = []): string[] {
    const out = [...extra];
    let n = extra.reduce((s, id) => s + STARS[id]!, 0);
    for (const id of ALL_IDS) {
        if (n >= stars) break;
        if (out.includes(id) || without.includes(id)) continue;
        out.push(id);
        n += STARS[id]!;
    }
    return out;
}

/** Exactly `stars` stars or just below: one-star achievements to finish. */
function below(stars: number, extra: string[] = []): string[] {
    const out = [...extra];
    let n = extra.reduce((s, id) => s + STARS[id]!, 0);
    for (const id of ALL_IDS) {
        if (out.includes(id)) continue;
        if (n + STARS[id]! >= stars) continue;
        out.push(id);
        n += STARS[id]!;
    }
    return out;
}

const starsOf = (ids: string[]) => ids.reduce((s, id) => s + STARS[id]!, 0);

async function declare(p: TestPlayer, ids: string[]) {
    const r = await api("PUT", "/api/v1/players/me/achievements", { token: p.token, body: { unlocked: ids } });
    expect(r.status).toBe(200);
}

const show = (p: TestPlayer, badges: unknown) => api("PUT", "/api/v1/players/me/badges", { token: p.token, body: { badges } });
const shownOn = async (id: string, token?: string) => {
    const r = await api("GET", `/api/v1/players/${id}`, token ? { token } : {});
    expect(r.status).toBe(200);
    return r.body.data.badges as { id: string; stars: number }[] | null;
};

describe("profile badges", () => {
    it("uses the game's steps: 2 badges from 100 stars, 3 from 250", () => {
        expect((catalog as { badgeSlots: unknown }).badgeSlots).toEqual((game as { badgeSlots: unknown }).badgeSlots);
        expect(BADGE_STEPS).toEqual([
            { stars: 100, slots: 2 },
            { stars: 250, slots: 3 },
        ]);
        expect(MAX_BADGES).toBe(3);
        expect([0, 99, 100, 249, 250, 612].map(badgeSlots)).toEqual([0, 0, 2, 2, 3, 3]);
    });

    it("needs an account and refuses malformed input", async () => {
        const a = await registerPlayer("Nova");
        await declare(a, worth(260));
        expect((await api("PUT", "/api/v1/players/me/badges", { body: { badges: [] } })).status).toBe(401);
        const ids = worth(260);
        for (const bad of [
            { badges: "voyage_first" },
            { badges: [1] },
            { badges: ["../x"] },
            { badges: ["Voyage_First"] },
            { badges: [ids[0], ids[0]] }, // the same achievement twice
            { badges: ids.slice(0, MAX_BADGES + 1) }, // more than any step opens
            { badges: [ids[0]], stars: 612 }, // the game never says how many stars it has
            { badges: [ids[0]], player_id: a.id },
            {},
        ])
            expect((await api("PUT", "/api/v1/players/me/badges", { token: a.token, body: bad })).status).toBe(400);
        const big = await api("PUT", "/api/v1/players/me/badges", {
            token: a.token,
            raw: JSON.stringify({ badges: [], pad: "x".repeat(4000) }),
            headers: { "content-type": "application/json" },
        });
        expect(big.status).toBe(413);
        // Nothing was stored.
        expect(await shownOn(a.id)).toEqual([]);
    });

    it("counts the slots from the stars it knows itself, never from the game", async () => {
        const a = await registerPlayer("Nova");
        // No achievement declared: no slot at all.
        let r = await show(a, ["voyage_first"]);
        expect(r.status).toBe(403);
        // 99 stars: still none (the first step is at 100).
        const ninety = below(100, ["voyage_first", "voyage_5"]);
        expect(starsOf(ninety)).toBe(99);
        await declare(a, ninety);
        r = await show(a, ["voyage_first"]);
        expect(r.status).toBe(403);
        expect(r.body.error.code).toBe("BADGE_SLOTS");
        expect(r.body.error.details).toEqual({ slots: 0, stars: 99 });
        // An empty list is always accepted (nothing shown).
        expect((await show(a, [])).status).toBe(200);
        // 100 stars and more: two badges, not three.
        const hundred = worth(100, ["voyage_first", "voyage_5", "gardiens_first"]);
        expect(starsOf(hundred)).toBeLessThan(250);
        await declare(a, hundred);
        r = await show(a, ["voyage_first", "voyage_5", "gardiens_first"]);
        expect(r.status).toBe(403);
        expect(r.body.error.code).toBe("BADGE_SLOTS");
        expect(r.body.error.details.slots).toBe(2);
        r = await show(a, ["voyage_5", "voyage_first"]);
        expect(r.status).toBe(200);
        expect(r.body.data).toMatchObject({ slots: 2, stars: starsOf(hundred) });
        // The order chosen is kept.
        expect(r.body.data.badges).toEqual([
            { id: "voyage_5", stars: STARS.voyage_5 },
            { id: "voyage_first", stars: STARS.voyage_first },
        ]);
        // 250 stars: a third one.
        await declare(a, worth(250, ["voyage_first", "voyage_5", "gardiens_first"]));
        r = await show(a, ["gardiens_first", "voyage_first", "voyage_5"]);
        expect(r.status).toBe(200);
        expect(r.body.data.slots).toBe(3);
        expect((await shownOn(a.id))!.map((b) => b.id)).toEqual(["gardiens_first", "voyage_first", "voyage_5"]);
    });

    it("refuses achievements unknown to the server or not declared by this player", async () => {
        const a = await registerPlayer("Nova");
        const b = await registerPlayer("Orion");
        const mine = worth(260, ["voyage_first"], ["gardiens_astrarque"]);
        await declare(a, mine);
        await declare(b, ["gardiens_astrarque", ...worth(260, [], mine)]);
        for (const bad of [["futur_succes"], ["gardiens_astrarque"], ["voyage_first", "futur_succes"]]) {
            const r = await show(a, bad);
            expect(r.status).toBe(403);
            expect(r.body.error.code).toBe("BADGE_NOT_UNLOCKED");
        }
        // Another player's achievement is never shown on this profile.
        expect(await shownOn(a.id)).toEqual([]);
        expect((await show(b, ["gardiens_astrarque"])).status).toBe(200);
        expect((await shownOn(b.id))!.map((x) => x.id)).toEqual(["gardiens_astrarque"]);
    });

    it("shows only the badges still valid, and follows the profile's visibility", async () => {
        const a = await registerPlayer("Nova");
        const base = ["voyage_first", "voyage_5", "gardiens_first"];
        const rich = worth(260, base);
        await declare(a, rich);
        expect((await show(a, ["gardiens_first", "voyage_5", "voyage_first"])).status).toBe(200);
        // Anyone (even without an account) sees them on a public profile.
        expect((await shownOn(a.id))!.map((x) => x.id)).toEqual(["gardiens_first", "voyage_5", "voyage_first"]);
        // Fewer stars (between 100 and 250): the first two only.
        await declare(a, worth(120, base));
        expect((await shownOn(a.id))!.map((x) => x.id)).toEqual(["gardiens_first", "voyage_5"]);
        // An achievement no longer declared: left out, the next one takes its place.
        await declare(a, worth(120, ["voyage_first", "voyage_5"], ["gardiens_first"]));
        expect((await shownOn(a.id))!.map((x) => x.id)).toEqual(["voyage_5", "voyage_first"]);
        // Below the first step: none.
        await declare(a, ["voyage_first", "voyage_5"]);
        expect(await shownOn(a.id)).toEqual([]);
        // The choice was kept: back above 250 stars, all three again.
        await declare(a, rich);
        expect((await shownOn(a.id))!.map((x) => x.id)).toEqual(["gardiens_first", "voyage_5", "voyage_first"]);
        // A private profile shows nothing to others, everything to its owner.
        await api("PATCH", "/api/v1/players/me", { token: a.token, body: { profile_visibility: "private" } });
        expect(await shownOn(a.id)).toBeNull();
        const other = await registerPlayer("Orion");
        expect(await shownOn(a.id, other.token)).toBeNull();
        expect((await shownOn(a.id, a.token))!.map((x) => x.id)).toEqual(["gardiens_first", "voyage_5", "voyage_first"]);
        // In the data export, as chosen.
        const exp = await api("GET", "/api/v1/players/me/export", { token: a.token });
        expect(JSON.parse(exp.body.data.badges[0].ids)).toEqual(["gardiens_first", "voyage_5", "voyage_first"]);
    });

    it("is replaced, never added up, and goes with the account", async () => {
        const a = await registerPlayer("Nova");
        await declare(a, worth(260, ["voyage_first", "voyage_5"]));
        expect((await show(a, ["voyage_first", "voyage_5"])).status).toBe(200);
        expect((await show(a, ["voyage_5"])).status).toBe(200);
        expect((await shownOn(a.id))!.map((x) => x.id)).toEqual(["voyage_5"]);
        expect((await show(a, [])).status).toBe(200);
        expect(await shownOn(a.id)).toEqual([]);
        expect((await show(a, ["voyage_first"])).status).toBe(200);
        const del = await api("DELETE", "/api/v1/players/me", { token: a.token, body: { password: a.password, confirm: "DELETE" } });
        expect(del.status).toBe(200);
        const left = await env.DB.prepare("SELECT COUNT(*) AS n FROM player_badges").first<{ n: number }>();
        expect(left?.n).toBe(0);
    });
});
