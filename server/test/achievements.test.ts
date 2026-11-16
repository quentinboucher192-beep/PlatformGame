import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import catalog from "../src/achievementCatalog.json";
import game from "../../data/achievements/achievements.json";
import { api, registerPlayer, seedAll } from "./helpers";

beforeEach(seedAll);

describe("achievements (declared by the game)", () => {
    it("counts the stars itself from its catalogue and ignores unknown ids", async () => {
        const a = await registerPlayer("Nova");
        const r = await api("PUT", "/api/v1/players/me/achievements", {
            token: a.token,
            body: { unlocked: ["voyage_first", "voyage_5", "gardiens_astrarque", "futur_succes", "voyage_first"] },
        });
        expect(r.status).toBe(200);
        // 1 + 1 + 5 stars, the unknown id ignored, the duplicate counted once.
        expect(r.body.data.achievements).toMatchObject({ stars: 7, count: 3, declared: true });
        expect(r.body.data.achievements.total_stars).toBeGreaterThan(300);
        // Shown on the public profile, as declared.
        const pub = await api("GET", `/api/v1/players/${a.id}`);
        expect(pub.body.data.achievements).toMatchObject({ stars: 7, count: 3, declared: true });
        // An older game (no `merge`): its list replaces the account's.
        const r2 = await api("PUT", "/api/v1/players/me/achievements", { token: a.token, body: { unlocked: ["voyage_first"] } });
        expect(r2.body.data.achievements).toMatchObject({ stars: 1, count: 1 });
        // 2.46.1 (`merge`): the lists of the account's computers add up, nothing is taken off.
        const put = (unlocked: string[]) => api("PUT", "/api/v1/players/me/achievements", { token: a.token, body: { unlocked, merge: true } });
        expect((await put(["voyage_5", "gardiens_astrarque"])).body.data.achievements).toMatchObject({ stars: 7, count: 3 });
        expect((await put(["voyage_first"])).body.data.achievements).toMatchObject({ stars: 7, count: 3 });
        expect((await put(["gardiens_first"])).body.data.achievements.count).toBe(4);
        expect((await api("PUT", "/api/v1/players/me/achievements", { token: a.token, body: { unlocked: [], merge: "oui" } })).status).toBe(400);
        // An id the catalogue no longer has is dropped at the next declaration.
        await env.DB.prepare("UPDATE player_achievements SET ids = ?2 WHERE player_id = ?1")
            .bind(a.id, JSON.stringify(["gardiens_first", "succes_retire", "voyage_5", "voyage_first", "gardiens_astrarque"]))
            .run();
        expect((await put([])).body.data.achievements.count).toBe(4);
        const row = await env.DB.prepare("SELECT ids FROM player_achievements WHERE player_id = ?1").bind(a.id).first<{ ids: string }>();
        expect(JSON.parse(row!.ids)).not.toContain("succes_retire");
    });

    it("refuses bad input, needs an account, hides a private profile's stars", async () => {
        const a = await registerPlayer("Astrid");
        expect((await api("PUT", "/api/v1/players/me/achievements", { body: { unlocked: [] } })).status).toBe(401);
        for (const bad of [{ unlocked: "voyage_first" }, { unlocked: [1, 2] }, { unlocked: ["../x"] }, { unlocked: Array(1001).fill("voyage_first") }])
            expect((await api("PUT", "/api/v1/players/me/achievements", { token: a.token, body: bad })).status).toBe(400);
        await api("PUT", "/api/v1/players/me/achievements", { token: a.token, body: { unlocked: ["voyage_first"] } });
        await api("PATCH", "/api/v1/players/me", { token: a.token, body: { profile_visibility: "private" } });
        const hidden = await api("GET", `/api/v1/players/${a.id}`);
        expect(hidden.body.data.achievements).toBeNull();
        const self = await api("GET", `/api/v1/players/${a.id}`, { token: a.token });
        expect(self.body.data.achievements.stars).toBe(1);
        // In the data export.
        const exp = await api("GET", "/api/v1/players/me/export", { token: a.token });
        expect(exp.body.data.achievements[0].stars).toBe(1);
    });

    it("the server's catalogue is the game's", () => {
        const stars: Record<string, number> = {};
        for (const x of (game as { achievements: { id: string; stars: number }[] }).achievements) stars[x.id] = x.stars;
        expect((catalog as { stars: Record<string, number> }).stars).toEqual(stars);
    });
});
