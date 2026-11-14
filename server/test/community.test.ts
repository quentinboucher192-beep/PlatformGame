import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { api, buildReplay, registerPlayer, seedAll, type TestPlayer } from "./helpers";
import { LH, LH2, PNG, octet, pkg, publish, zip, levelJson } from "./publish";

beforeEach(seedAll);

describe("community levels", () => {
    it("are published with a validated package and the author's completion replay", async () => {
        const a = await registerPlayer("Nova");
        expect((await api("POST", "/api/v1/community/levels", { body: { title: "x" } })).status).toBe(401);
        const { id, version } = await publish(a);
        expect(version).toBe(1);
        const list = await api("GET", "/api/v1/community/levels?sort=recent");
        expect(list.body.data.levels).toHaveLength(1);
        expect(list.body.data.levels[0]).toMatchObject({ id, title: "La Tour Ardente", planet: "pyralis", difficulty: 3.5, version: 1 });
        expect(list.body.data.levels[0].author.username).toBe("Nova");
        // Download (counted for others), thumbnail.
        const b = await registerPlayer("Orion");
        const dl = await api("GET", `/api/v1/community/levels/${id}/package`, { token: b.token });
        expect(dl.status).toBe(200);
        expect(dl.headers.get("x-level-hash")).toBe(LH);
        expect(new Uint8Array(dl.body as ArrayBuffer).byteLength).toBeGreaterThan(100);
        expect((await api("GET", `/api/v1/community/levels/${id}/thumbnail`)).status).toBe(200);
        const detail = await api("GET", `/api/v1/community/levels/${id}`);
        expect(detail.body.data.level.downloads).toBe(1);
        expect(detail.body.data.versions[0]).toMatchObject({ version: 1, status: "published", completion_ms: 62000 });
        // A new version supersedes the first one.
        const v2 = await publish(a, { communityId: id, hash: LH2, title: "La Tour Ardente II" });
        expect(v2).toEqual({ id, version: 2 });
        const after = await api("GET", `/api/v1/community/levels/${id}`);
        expect(after.body.data.level).toMatchObject({ version: 2, title: "La Tour Ardente II" });
        expect(after.body.data.versions.map((v: any) => v.status)).toEqual(["published", "superseded"]);
        // Only the author publishes new versions.
        const other = await api("POST", "/api/v1/community/levels", {
            token: b.token,
            body: { community_id: id, title: "Volé", planet: "pyralis", level_hash: LH, min_game_version: "2.2.0" },
        });
        expect(other.status).toBe(403);
    });

    it("refuses dangerous or broken packages and replays of another version", async () => {
        const a = await registerPlayer("Nova");
        const start = await api("POST", "/api/v1/community/levels", {
            token: a.token,
            body: { title: "Piège", planet: "pyralis", level_hash: LH, min_game_version: "2.2.0" },
        });
        const { upload } = start.body.data;
        const put = async (data: Uint8Array) => api("PUT", upload.package, { token: a.token, raw: data, headers: octet });
        const base = { "COSMIC_LEVEL/manifest.json": JSON.stringify({ format: "cosmic.package" }), "COSMIC_LEVEL/level.json": levelJson() };
        expect((await put(await zip({ ...base, "COSMIC_LEVEL/../../evil.json": "{}" }))).body.error.code).toBe("PACKAGE_INVALID");
        expect((await put(await zip({ ...base, "COSMIC_LEVEL/script.exe": "MZ" }))).body.error.code).toBe("PACKAGE_INVALID");
        expect((await put(await zip({ ...base, "/etc/passwd.json": "{}" }))).status).toBe(422);
        expect((await put(await zip({ "COSMIC_LEVEL/manifest.json": JSON.stringify({ format: "cosmic.package" }) }))).status).toBe(422);
        expect((await put(new TextEncoder().encode("pas un zip du tout"))).status).toBe(422);
        const noExit = JSON.parse(levelJson());
        noExit.objects = noExit.objects.filter((o: any) => o.type !== "exit");
        expect((await put(await zip({ ...base, "COSMIC_LEVEL/level.json": JSON.stringify(noExit) }))).body.error.message).toContain("sortie");
        // Stored (not deflated) packages are fine too.
        expect((await put(await zip(base, false))).status).toBe(200);
        // A completion replay played on another version of the level is refused.
        const wrong = await buildReplay({ levelId: "mon_niveau", levelHash: LH2, attemptId: crypto.randomUUID(), durationMs: 60000, exact: true });
        const r = await api("PUT", upload.replay, { token: a.token, raw: wrong, headers: octet });
        expect(r.body.error.code).toBe("REPLAY_LEVEL_MISMATCH");
        // Unpublished: invisible to others.
        const b = await registerPlayer("Orion");
        expect((await api("GET", `/api/v1/community/levels/${start.body.data.id}`, { token: b.token })).status).toBe(404);
        expect((await api("GET", "/api/v1/community/levels?sort=mine", { token: a.token })).body.data.levels[0].status).toBe("draft");
    });

    it("rankings per level only count times whose replay was accepted", async () => {
        const a = await registerPlayer("Nova");
        const { id } = await publish(a);
        const b = await registerPlayer("Orion");
        const c = await registerPlayer("Lyra");
        const run = async (p: TestPlayer, timeMs: number, hash = LH) => {
            const attempt = crypto.randomUUID();
            const s = await api("POST", `/api/v1/community/levels/${id}/records`, {
                token: p.token,
                body: { version: 1, attempt_id: attempt, time_ms: timeMs, deaths: 0 },
            });
            expect(s.status).toBe(201);
            const replay = await buildReplay({ levelId: "mon_niveau", levelHash: hash, attemptId: attempt, durationMs: timeMs, exact: true });
            return api("PUT", s.body.data.upload, { token: p.token, raw: replay, headers: octet });
        };
        expect((await run(b, 58000)).body.data.record.status).toBe("REPLAY_OK");
        expect((await run(c, 55000, LH2)).body.data.record.status).toBe("REJECTED");
        const ok = await run(c, 61000);
        expect(ok.body.data.record.status).toBe("REPLAY_OK");
        expect(ok.body.data.me).toMatchObject({ rank: 2, time_ms: 61000 });
        const board = await api("GET", `/api/v1/community/levels/${id}/leaderboard`, { token: c.token });
        expect(board.body.data.entries.map((e: any) => [e.player.username, e.time_ms])).toEqual([["Orion", 58000], ["Lyra", 61000]]);
        expect(board.body.data.verification).toContain("non re-simulé");
        const list = await api("GET", "/api/v1/community/levels?sort=popular");
        expect(list.body.data.levels[0]).toMatchObject({ completions: 2, best: { time_ms: 58000, player: "Orion" } });
        expect((await api("GET", `/api/v1/community/records/${board.body.data.entries[0].record_id}/replay`)).status).toBe(200);
    });

    it("are rated, searched, sorted and hidden after several reports", async () => {
        const a = await registerPlayer("Nova");
        const one = await publish(a, { title: "Volcan Éternel", difficulty: 4.5 });
        const two = await publish(a, { title: "Prairie douce", difficulty: 1.5 });
        const raters = [await registerPlayer("Orion"), await registerPlayer("Lyra"), await registerPlayer("Vega")];
        expect((await api("POST", `/api/v1/community/levels/${one.id}/rate`, { token: a.token, body: { stars: 5 } })).status).toBe(403);
        for (const r of raters) await api("POST", `/api/v1/community/levels/${two.id}/rate`, { token: r.token, body: { stars: 5 } });
        await api("POST", `/api/v1/community/levels/${one.id}/rate`, { token: raters[0]!.token, body: { stars: 2 } });
        const again = await api("POST", `/api/v1/community/levels/${one.id}/rate`, { token: raters[0]!.token, body: { stars: 3 } });
        expect(again.body.data.rating).toEqual({ average: 3, count: 1 });
        expect((await api("POST", `/api/v1/community/levels/${one.id}/rate`, { token: raters[1]!.token, body: { stars: 9 } })).status).toBe(400);
        const top = await api("GET", "/api/v1/community/levels?sort=top");
        expect(top.body.data.levels.map((l: any) => l.title)).toEqual(["Prairie douce", "Volcan Éternel"]);
        const hard = await api("GET", "/api/v1/community/levels?sort=hardest");
        expect(hard.body.data.levels[0].title).toBe("Volcan Éternel");
        const search = await api("GET", "/api/v1/community/levels?sort=recent&q=eternel");
        expect(search.body.data.levels.map((l: any) => l.title)).toEqual(["Volcan Éternel"]);
        expect((await api("GET", "/api/v1/community/levels?sort=recent&q=nova")).body.data.levels).toHaveLength(2);
        // Friends' levels.
        await api("POST", "/api/v1/friends/request", { token: raters[2]!.token, body: { player_id: a.id } });
        await api("POST", "/api/v1/friends/accept", { token: a.token, body: { player_id: raters[2]!.id } });
        expect((await api("GET", "/api/v1/community/levels?sort=friends", { token: raters[2]!.token })).body.data.levels).toHaveLength(2);
        expect((await api("GET", "/api/v1/community/levels?sort=friends", { token: raters[0]!.token })).body.data.levels).toHaveLength(0);
        expect((await api("GET", "/api/v1/community/levels?sort=friends")).status).toBe(401);
        // Five reports hide a level.
        const reporters = [...raters, await registerPlayer("Kael"), await registerPlayer("Altair")];
        for (const r of reporters) await api("POST", `/api/v1/community/levels/${one.id}/report`, { token: r.token, body: { reason: "contenu" } });
        await api("POST", `/api/v1/community/levels/${one.id}/report`, { token: reporters[0]!.token, body: {} });
        expect((await api("GET", "/api/v1/community/levels?sort=recent")).body.data.levels.map((l: any) => l.title)).toEqual(["Prairie douce"]);
        expect((await api("GET", `/api/v1/community/levels/${one.id}`)).status).toBe(404);
    });

    it("are removed by their author, with their files, and with the author's account", async () => {
        const a = await registerPlayer("Nova");
        const one = await publish(a);
        const keys = await env.DB.prepare("SELECT package_key FROM community_level_versions WHERE level_id = ?1").bind(one.id).all<{ package_key: string }>();
        expect(await env.STORAGE.head(keys.results[0]!.package_key)).not.toBeNull();
        const b = await registerPlayer("Orion");
        expect((await api("DELETE", `/api/v1/community/levels/${one.id}`, { token: b.token })).status).toBe(403);
        expect((await api("DELETE", `/api/v1/community/levels/${one.id}`, { token: a.token })).status).toBe(200);
        expect(await env.STORAGE.head(keys.results[0]!.package_key)).toBeNull();
        const two = await publish(a);
        const exp = await api("GET", "/api/v1/players/me/export", { token: a.token });
        expect(exp.body.data.community_levels).toHaveLength(1);
        const k2 = await env.DB.prepare("SELECT package_key FROM community_level_versions WHERE level_id = ?1").bind(two.id).first<string>("package_key");
        await api("DELETE", "/api/v1/players/me", { token: a.token, body: { password: a.password, confirm: "DELETE" } });
        expect(await env.STORAGE.head(k2!)).toBeNull();
        expect(await env.DB.prepare("SELECT COUNT(*) AS n FROM community_levels").first("n")).toBe(0);
    });
});
