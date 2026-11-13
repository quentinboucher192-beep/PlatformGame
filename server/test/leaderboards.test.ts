import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { LEVEL2_HASH, api, registerPlayer, seedAll, verifiedRun, type TestPlayer } from "./helpers";

beforeEach(seedAll);

async function field(): Promise<Record<string, TestPlayer>> {
    const ps: Record<string, TestPlayer> = {
        fr1: await registerPlayer("Amelie", { country_code: "FR", location_visibility: "globe" }),
        fr2: await registerPlayer("Bastien", { country_code: "FR", location_visibility: "country" }),
        us1: await registerPlayer("Carter", { country_code: "US", location_visibility: "globe" }),
        jp1: await registerPlayer("Daichi", { country_code: "JP", location_visibility: "hidden" }),
        de1: await registerPlayer("Elke", { country_code: "DE", location_visibility: "globe" }),
    };
    await verifiedRun(ps.fr1!, 61000);
    await verifiedRun(ps.fr2!, 64000);
    await verifiedRun(ps.us1!, 62000);
    await verifiedRun(ps.jp1!, 60000);
    await verifiedRun(ps.de1!, 66000);
    return ps;
}

describe("level leaderboards", () => {
    it("ranks the best verified time of each player", async () => {
        await field();
        const res = await api("GET", "/api/v1/leaderboards/level/test_01");
        expect(res.status).toBe(200);
        const names = res.body.data.entries.map((e: any) => [e.rank, e.player.username, e.time_ms]);
        expect(names).toEqual([
            [1, "Daichi", 60000],
            [2, "Amelie", 61000],
            [3, "Carter", 62000],
            [4, "Bastien", 64000],
            [5, "Elke", 66000],
        ]);
        // A hidden location never leaks.
        expect(res.body.data.entries[0].player.country_code).toBeNull();
        expect(res.body.data.entries[1].player.country_code).toBe("FR");
        expect(res.body.meta).toMatchObject({ total: 5, limit: 25, offset: 0, next_offset: null });
    });

    it("paginates consistently", async () => {
        await field();
        const p1 = await api("GET", "/api/v1/leaderboards/level/test_01?limit=2");
        expect(p1.body.data.entries.map((e: any) => e.rank)).toEqual([1, 2]);
        expect(p1.body.meta).toMatchObject({ total: 5, next_offset: 2 });
        const p3 = await api("GET", "/api/v1/leaderboards/level/test_01?limit=2&offset=4");
        expect(p3.body.data.entries.map((e: any) => e.rank)).toEqual([5]);
        expect(p3.body.meta.next_offset).toBeNull();
        const beyond = await api("GET", "/api/v1/leaderboards/level/test_01?limit=2&offset=40");
        expect(beyond.body.data.entries).toHaveLength(0);
        expect(beyond.body.meta.total).toBe(5);
        expect((await api("GET", "/api/v1/leaderboards/level/test_01?limit=0")).status).toBe(400);
        expect((await api("GET", "/api/v1/leaderboards/level/test_01?limit=abc")).status).toBe(400);
        expect((await api("GET", "/api/v1/leaderboards/level/test_01?category=CHEAT")).status).toBe(400);
    });

    it("filters by country (players hiding their location excluded) and by pseudonym", async () => {
        await field();
        const fr = await api("GET", "/api/v1/leaderboards/country/FR?level_id=test_01");
        expect(fr.body.data.entries.map((e: any) => [e.rank, e.world_rank, e.player.username])).toEqual([
            [1, 2, "Amelie"],
            [2, 4, "Bastien"],
        ]);
        const jp = await api("GET", "/api/v1/leaderboards/country/JP?level_id=test_01");
        expect(jp.body.data.entries).toHaveLength(0);
        const search = await api("GET", "/api/v1/leaderboards/level/test_01?player=car");
        expect(search.body.data.entries.map((e: any) => e.player.username)).toEqual(["Carter"]);
        expect((await api("GET", "/api/v1/leaderboards/country/FRA?level_id=test_01")).status).toBe(400);
        expect((await api("GET", "/api/v1/leaderboards/country/FR")).status).toBe(400);
        expect((await api("GET", "/api/v1/leaderboards/level/test_01?player=%25")).status).toBe(400);
    });

    it("reports the viewer's own position even outside the page", async () => {
        const ps = await field();
        const res = await api("GET", "/api/v1/leaderboards/level/test_01?limit=1", { token: ps.de1!.token });
        expect(res.body.data.entries).toHaveLength(1);
        expect(res.body.data.me.entry).toMatchObject({ rank: 5, time_ms: 66000, is_me: true });
    });

    it("builds the friends board from accepted friendships only", async () => {
        const ps = await field();
        expect((await api("GET", "/api/v1/leaderboards/friends?level_id=test_01")).status).toBe(401);
        await api("POST", "/api/v1/friends/request", { token: ps.de1!.token, body: { username: "Carter" } });
        const before = await api("GET", "/api/v1/leaderboards/friends?level_id=test_01", { token: ps.de1!.token });
        expect(before.body.data.entries.map((e: any) => e.player.username)).toEqual(["Elke"]);
        await api("POST", "/api/v1/friends/accept", { token: ps.us1!.token, body: { player_id: ps.de1!.id } });
        const after = await api("GET", "/api/v1/leaderboards/friends?level_id=test_01", { token: ps.de1!.token });
        expect(after.body.data.entries.map((e: any) => [e.rank, e.player.username])).toEqual([
            [1, "Carter"],
            [2, "Elke"],
        ]);
    });

    it("resets the weekly board logically without deleting history", async () => {
        const ps = await field();
        // Amelie's and Daichi's runs happened last month.
        await env.DB.prepare("UPDATE records SET created_at = '2026-10-01T10:00:00.000Z' WHERE player_id IN (?1, ?2)")
            .bind(ps.fr1!.id, ps.jp1!.id)
            .run();
        const weekly = await api("GET", "/api/v1/leaderboards/weekly?level_id=test_01");
        expect(weekly.body.data.board.period).toBe("weekly");
        expect(weekly.body.data.board.week_start).toMatch(/T00:00:00.000Z$/);
        expect(weekly.body.data.entries.map((e: any) => e.player.username)).toEqual(["Carter", "Bastien", "Elke"]);
        const historic = await api("GET", "/api/v1/leaderboards/level/test_01");
        expect(historic.body.data.entries).toHaveLength(5);
    });

    it("sums planet times only for players who finished every level", async () => {
        const ps = await field();
        await verifiedRun(ps.us1!, 90000, { level_id: "test_02", level_hash: LEVEL2_HASH });
        await verifiedRun(ps.fr1!, 95000, { level_id: "test_02", level_hash: LEVEL2_HASH });
        const res = await api("GET", "/api/v1/leaderboards/planet/testworld");
        expect(res.body.data.entries.map((e: any) => [e.player.username, e.total_time_ms])).toEqual([
            ["Carter", 152000],
            ["Amelie", 156000],
        ]);
        expect((await api("GET", "/api/v1/leaderboards/planet/nowhere")).status).toBe(404);
    });

    it("computes the global cosmic points ranking", async () => {
        const ps = await field();
        await verifiedRun(ps.de1!, 90000, { level_id: "test_02", level_hash: LEVEL2_HASH });
        const res = await api("GET", "/api/v1/leaderboards/global");
        expect(res.body.data.board.kind).toBe("cosmic_points");
        const top = res.body.data.entries[0];
        expect(top.player.username).toBe("Elke");
        expect(top.levels_completed).toBe(2);
        expect(top.points).toBe(Math.floor((1000 * 60000) / 66000) + 1000);
        const level = await api("GET", "/api/v1/leaderboards/global?level_id=test_01");
        expect(level.body.data.entries[0].player.username).toBe("Daichi");
    });

    it("never ranks suspended accounts", async () => {
        const ps = await field();
        await env.DB.prepare("UPDATE players SET account_status = 'suspended' WHERE id = ?1").bind(ps.jp1!.id).run();
        const res = await api("GET", "/api/v1/leaderboards/level/test_01");
        expect(res.body.data.entries[0].player.username).toBe("Amelie");
    });
});

describe("globe", () => {
    it("places only consenting players, at their country's centre", async () => {
        await field();
        const res = await api("GET", "/api/v1/globe/records?level_id=test_01");
        expect(res.status).toBe(200);
        const markers = res.body.data.records;
        expect(markers.map((m: any) => [m.player_name, m.rank])).toEqual([
            ["Amelie", 2],
            ["Carter", 3],
            ["Elke", 5],
        ]);
        const fr = markers[0];
        expect(fr).toMatchObject({ country_code: "FR", latitude: 46.6, longitude: 2.4, level_id: "test_01" });
        expect(res.body.data.generated_at).toMatch(/Z$/);
    });

    it("aggregates per country and lists a country's records", async () => {
        await field();
        const countries = await api("GET", "/api/v1/globe/countries?level_id=test_01");
        const fr = countries.body.data.countries.find((c: any) => c.country_code === "FR");
        // Bastien shows his country in rankings but chose not to appear on the globe.
        expect(fr).toMatchObject({ players: 1, best_rank: 2, best_player: "Amelie", best_time_ms: 61000 });
        expect(countries.body.data.countries.find((c: any) => c.country_code === "JP")).toBeUndefined();
        const us = await api("GET", "/api/v1/globe/records/US?level_id=test_01");
        expect(us.body.data.records.map((m: any) => m.player_name)).toEqual(["Carter"]);
        const overview = await api("GET", "/api/v1/globe/countries");
        expect(overview.body.data.countries.length).toBe(3);
    });

    it("shows the podiums of every level without a level filter", async () => {
        const ps = await field();
        await verifiedRun(ps.de1!, 90000, { level_id: "test_02", level_hash: LEVEL2_HASH });
        const res = await api("GET", "/api/v1/globe/records?per_level=2");
        expect(res.body.data.records.map((m: any) => [m.level_id, m.player_name])).toEqual([
            ["test_01", "Amelie"],
            ["test_01", "Carter"],
            ["test_02", "Elke"],
        ]);
    });

    it("hides a player who turns location off", async () => {
        const ps = await field();
        await api("PATCH", "/api/v1/players/me", { token: ps.fr1!.token, body: { location_visibility: "hidden" } });
        const res = await api("GET", "/api/v1/globe/records?level_id=test_01");
        expect(res.body.data.records.map((m: any) => m.player_name)).toEqual(["Carter", "Elke"]);
        const board = await api("GET", "/api/v1/leaderboards/level/test_01");
        const amelie = board.body.data.entries.find((e: any) => e.player.username === "Amelie");
        expect(amelie.player.country_code).toBeNull();
    });
});
