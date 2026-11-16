// The campaign edited by an administrator (2.45): a campaign level published as a new revision
// (file checked by the server, kept in R2, served to the games), the REQUIRED choice about the
// records (reset: new boards, the old ones archived; keep: the bests carried over to the new
// hash, weekly boards included), the return to an earlier revision, the public manifest, and
// the Journal. The routes do not exist for a player.

import { SELF, env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { EXIT, LEVEL_HASH, SPAWN, api, registerPlayer, seedAll, submission, verifiedRun, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const NEW_HASH = "e".repeat(64);
const NEW_HASH_3 = "f".repeat(64);

async function makeAdmin(p: TestPlayer): Promise<void> {
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
}

/** A level file of test_01 as the game writes it (format cosmic.level), at `revision`. */
function levelFile(revision: number, extra: Record<string, unknown> = {}): string {
    const level = {
        format: "cosmic.level",
        formatVersion: 3,
        id: "test_01",
        name: "Niveau test",
        planet: "testworld",
        bounds: [0, 0, 6000, 1200],
        metadata: { revision, generator: "tools/levelgen/test.py" },
        layers: [{ id: "gameplay", name: "Gameplay", order: 2, visible: true, locked: false }],
        objects: [
            { id: "o1", type: "solid", layer: "terrain", rect: [0.0, 1100.0, 6000.0, 100.0], props: { material: "ruin_stone" } },
            { id: "start", type: "player_start", layer: "gameplay", rect: [80.0, 1036.0, 40.0, 64.0] },
            { id: "exit", type: "exit", layer: "gameplay", rect: [EXIT.x, EXIT.y, EXIT.w, EXIT.h] },
        ],
        events: [],
        ...extra,
    };
    return JSON.stringify(level, null, 1);
}

function manifestEntry(version: number, hash: string, extra: Record<string, unknown> = {}) {
    return {
        id: "test_01",
        name: "Niveau test",
        planet_id: "testworld",
        planet_name: "Monde test",
        order: 1,
        version,
        content_hash: hash,
        total_collectibles: 10,
        total_enemies: 20,
        time_limit_ms: 900000,
        min_time_ms: 30000,
        bounds: [6000, 1200],
        spawn: [SPAWN.x, SPAWN.y],
        exit: [EXIT.x, EXIT.y, EXIT.w, EXIT.h],
        ...extra,
    };
}

function publishBody(version: number, hash: string, records: string, base = version - 1, extra: Record<string, unknown> = {}) {
    return {
        level_file: levelFile(version),
        base_version: base,
        records,
        game_version: "2.45.0",
        manifest: manifestEntry(version, hash),
        rating: { version: 2, score: 3.5, raw: 9.1 },
        ...extra,
    };
}

async function publish(admin: TestPlayer, body: Record<string, unknown>) {
    return api("POST", `${V}/admin/campaign/test_01/revisions`, { token: admin.token, body });
}

async function board(query = ""): Promise<[string, number][]> {
    const res = await api("GET", `${V}/leaderboards/level/test_01${query}`);
    expect(res.status).toBe(200);
    return res.body.data.entries.map((e: any) => [e.player.username, e.time_ms]);
}

describe("campaign revisions (admin)", () => {
    it("the routes do not exist for a player, and need a session", async () => {
        const p = await registerPlayer("Kestrel");
        for (const [m, path, body] of [
            ["GET", `${V}/admin/campaign`, undefined],
            ["GET", `${V}/admin/campaign/test_01/revisions`, undefined],
            ["POST", `${V}/admin/campaign/test_01/revisions`, publishBody(2, NEW_HASH, "reset")],
            ["POST", `${V}/admin/campaign/test_01/revert`, { version: 1, base_version: 1, records: "reset" }],
        ] as const) {
            expect((await api(m, path, { body })).status).toBe(401);
            const res = await api(m, path, { token: p.token, body });
            expect(res.status).toBe(404);
            expect(res.body.error.code).toBe("NOT_FOUND");
        }
        expect((await env.DB.prepare("SELECT current_version FROM levels WHERE id = 'test_01'").first<{ current_version: number }>())?.current_version).toBe(1);
    });

    it("lists the campaign by planet with the current revision and the next number", async () => {
        const admin = await registerPlayer("NovaRider");
        await makeAdmin(admin);
        const res = await api("GET", `${V}/admin/campaign`, { token: admin.token });
        expect(res.status).toBe(200);
        const planets = res.body.data.planets;
        expect(planets.map((p: any) => p.planet_id)).toEqual(["other", "testworld"]);
        const t1 = planets[1].levels[0];
        expect(t1).toMatchObject({ level_id: "test_01", next_version: 2, revisions: 1 });
        expect(t1.current).toMatchObject({ version: 1, content_hash: LEVEL_HASH, source: "release", has_file: false, change: null });
    });

    it("checks the level file and the manifest, never trusting the game", async () => {
        const admin = await registerPlayer("NovaRider");
        await makeAdmin(admin);
        const bad = async (body: Record<string, unknown>, status: number, code: string) => {
            const res = await publish(admin, body);
            expect(res.status, JSON.stringify(res.body)).toBe(status);
            expect(res.body.error.code).toBe(code);
        };
        // The choice about the records is required.
        const { records: _r, ...noChoice } = publishBody(2, NEW_HASH, "reset");
        await bad(noChoice, 400, "VALIDATION_ERROR");
        await bad(publishBody(2, NEW_HASH, "maybe"), 400, "VALIDATION_ERROR");
        // Not JSON, another level, the wrong revision number, no start.
        await bad(publishBody(2, NEW_HASH, "reset", 1, { level_file: "{ pas du json" }), 422, "LEVEL_INVALID");
        await bad(publishBody(2, NEW_HASH, "reset", 1, { level_file: levelFile(2).replace('"test_01"', '"test_02"') }), 422, "LEVEL_INVALID");
        await bad(publishBody(2, NEW_HASH, "reset", 1, { level_file: levelFile(7) }), 422, "LEVEL_INVALID");
        await bad(publishBody(2, NEW_HASH, "reset", 1, { level_file: levelFile(2, { objects: [{ id: "x", type: "exit", rect: [1, 2, 3, 4] }] }) }), 422, "LEVEL_INVALID");
        await bad(publishBody(2, NEW_HASH, "reset", 1, { level_file: levelFile(2, { format: "autre" }) }), 422, "LEVEL_INVALID");
        // The manifest must describe this file (bounds, exit, revision).
        await bad(publishBody(2, NEW_HASH, "reset", 1, { manifest: manifestEntry(2, NEW_HASH, { bounds: [5000, 1200] }) }), 422, "MANIFEST_MISMATCH");
        await bad(publishBody(2, NEW_HASH, "reset", 1, { manifest: manifestEntry(2, NEW_HASH, { exit: [1, 2, 3, 4] }) }), 422, "MANIFEST_MISMATCH");
        await bad(publishBody(2, NEW_HASH, "reset", 1, { manifest: manifestEntry(2, "zz") }), 400, "VALIDATION_ERROR");
        // The same content as an existing revision, and an out of date base.
        await bad(publishBody(2, LEVEL_HASH, "reset"), 409, "HASH_EXISTS");
        await bad(publishBody(2, NEW_HASH, "reset", 5), 409, "BASE_CHANGED");
        // Too big.
        const huge = levelFile(2, { description: "x".repeat(2 * 1024 * 1024) });
        expect([400, 413]).toContain((await publish(admin, publishBody(2, NEW_HASH, "reset", 1, { level_file: huge }))).status);
        // Nothing changed, nothing stored.
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM level_versions WHERE level_id = 'test_01'").first<{ n: number }>())?.n).toBe(1);
        expect((await env.STORAGE.list({ prefix: "campaign/" })).objects.filter((o) => o.key.includes("/2-"))).toHaveLength(0);
    });

    it("« Réinitialiser les records »: a new revision with its own boards, the old ones archived", async () => {
        const admin = await registerPlayer("NovaRider");
        await makeAdmin(admin);
        const kes = await registerPlayer("Kestrel");
        await verifiedRun(kes, 61000);
        expect(await board()).toEqual([["Kestrel", 61000]]);

        const body = publishBody(2, NEW_HASH, "reset");
        const res = await publish(admin, body);
        expect(res.status, JSON.stringify(res.body)).toBe(201);
        expect(res.body.data).toMatchObject({ version: 2, content_hash: NEW_HASH, records: "reset", carried_bests: 0 });
        // The new revision is current and empty; the old one is legacy with its records.
        expect(await board()).toEqual([]);
        expect(await board(`?level_hash=${LEVEL_HASH}`)).toEqual([["Kestrel", 61000]]);
        const v1 = await env.DB.prepare("SELECT compatibility_status FROM level_versions WHERE level_id = 'test_01' AND version = 1").first<{ compatibility_status: string }>();
        expect(v1?.compatibility_status).toBe("legacy");
        // A run of the old revision is no longer ranked; one of the new revision is.
        const old = await api("POST", `${V}/records/submit`, { token: kes.token, body: submission() });
        expect(old.body.error.code).toBe("LEVEL_VERSION_RETIRED");
        const fresh = await api("POST", `${V}/records/submit`, { token: kes.token, body: submission({ level_version: 2, level_hash: NEW_HASH, game_version: "2.45.0" }) });
        expect(fresh.status).toBe(201);

        // The file: kept byte for byte, served through the Worker with its SHA-256.
        const file = res.body.data.file;
        expect(file.path).toBe(`${V}/campaign/levels/test_01/revisions/2/file`);
        const dl = await SELF.fetch(`http://cosmic.test${file.path}`);
        expect(dl.status).toBe(200);
        expect(await dl.text()).toBe(body.level_file);
        expect(dl.headers.get("x-content-sha256")).toBe(file.sha256);
        expect(dl.headers.get("x-level-hash")).toBe(NEW_HASH);
        const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(body.level_file));
        expect([...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("")).toBe(file.sha256);
        expect((await api("GET", `${V}/campaign/levels/test_01/revisions/1/file`)).status).toBe(404);

        // The list shows who published it, and the history both revisions.
        const list = await api("GET", `${V}/admin/campaign`, { token: admin.token });
        const t1 = list.body.data.planets.find((p: any) => p.planet_id === "testworld").levels[0];
        expect(t1.current).toMatchObject({ version: 2, source: "admin", has_file: true, published_by: { username: "NovaRider" } });
        expect(t1.current.change).toMatchObject({ action: "publish", records: "reset", from_version: 1, by: { username: "NovaRider" } });
        expect(t1.next_version).toBe(3);
        const hist = await api("GET", `${V}/admin/campaign/test_01/revisions`, { token: admin.token });
        expect(hist.body.data.revisions.map((r: any) => [r.version, r.is_current, r.source, r.bests])).toEqual([
            [2, true, "admin", 0],
            [1, false, "release", 1],
        ]);

        // The Journal: who, what, which revision, which choice.
        const log = await api("GET", `${V}/admin/log?kind=campaign`, { token: admin.token });
        expect(log.body.data.entries).toHaveLength(1);
        expect(log.body.data.entries[0]).toMatchObject({
            action: "admin_campaign_published",
            actor: { username: "NovaRider" },
            campaign: { level_id: "test_01", version: 2, from_version: 1, records: "reset", carried_bests: 0 },
        });
    });

    it("« Garder les records »: the bests and the weekly boards carried over to the new hash", async () => {
        const admin = await registerPlayer("NovaRider");
        await makeAdmin(admin);
        const kes = await registerPlayer("Kestrel");
        const ame = await registerPlayer("Amelie");
        await verifiedRun(kes, 61000);
        await verifiedRun(ame, 64000);
        // A run of revision 1 still waiting for its verification when the revision changes.
        const pendingSub = submission({ time_ms: 59000 });
        const pending = await api("POST", `${V}/records/submit`, { token: ame.token, body: pendingSub });
        expect(pending.body.data.record.verification_status).toBe("PENDING");

        const res = await publish(admin, publishBody(2, NEW_HASH, "keep"));
        expect(res.status, JSON.stringify(res.body)).toBe(201);
        expect(res.body.data).toMatchObject({ version: 2, records: "keep", carried_bests: 2 });
        // Same board on the new hash (all time and this week); the old one stays readable.
        expect(await board()).toEqual([
            ["Kestrel", 61000],
            ["Amelie", 64000],
        ]);
        expect(await board("?period=weekly")).toEqual([
            ["Kestrel", 61000],
            ["Amelie", 64000],
        ]);
        expect(await board(`?level_hash=${LEVEL_HASH}`)).toHaveLength(2);
        // The global ranking counts the current revisions: the carried bests are in it.
        const global = await api("GET", `${V}/leaderboards/global`);
        expect(global.body.data.entries.map((e: any) => e.player.username)).toEqual(["Kestrel", "Amelie"]);
        // A run on the new revision beats a carried best like any other.
        await verifiedRun(ame, 60000, { level_version: 2, level_hash: NEW_HASH, game_version: "2.45.0" });
        expect(await board()).toEqual([
            ["Amelie", 60000],
            ["Kestrel", 61000],
        ]);
        // The run of revision 1 verified afterwards counts on revision 2 too.
        const rec = await env.DB.prepare("SELECT * FROM records WHERE id = ?1").bind(pending.body.data.record.id).first();
        expect(rec).not.toBeNull();
        const review = await api("POST", `${V}/admin/records/${pending.body.data.record.id}/review`, {
            body: { decision: "VERIFIED" },
            headers: { authorization: "Bearer test-admin-token-0123456789abcdef0123456789" },
        });
        expect(review.status).toBe(200);
        expect(await board()).toEqual([
            ["Amelie", 59000],
            ["Kestrel", 61000],
        ]);

        // A second compatible revision carries everything again (the chain is kept).
        const third = await publish(admin, publishBody(3, NEW_HASH_3, "keep"));
        expect(third.status).toBe(201);
        expect(await board("?period=weekly")).toEqual([
            ["Amelie", 59000],
            ["Kestrel", 61000],
        ]);
        const links = await env.DB.prepare("SELECT from_hash FROM campaign_board_links WHERE to_hash = ?1 ORDER BY from_hash").bind(NEW_HASH_3).all<{ from_hash: string }>();
        expect(links.results.map((l) => l.from_hash)).toEqual([LEVEL_HASH, NEW_HASH]);
        const log = await api("GET", `${V}/admin/log?kind=campaign`, { token: admin.token });
        expect(log.body.data.entries.map((e: any) => [e.campaign.version, e.campaign.records])).toEqual([
            [3, "keep"],
            [2, "keep"],
        ]);
    });

    it("goes back to an earlier revision, its archived boards or the current bests", async () => {
        const admin = await registerPlayer("NovaRider");
        await makeAdmin(admin);
        const kes = await registerPlayer("Kestrel");
        const ame = await registerPlayer("Amelie");
        await verifiedRun(kes, 61000);
        expect((await publish(admin, publishBody(2, NEW_HASH, "reset"))).status).toBe(201);
        await verifiedRun(ame, 58000, { level_version: 2, level_hash: NEW_HASH, game_version: "2.45.0" });
        expect(await board()).toEqual([["Amelie", 58000]]);

        // Wrong base, unknown revision, the current one: refused.
        const revert = (body: Record<string, unknown>) => api("POST", `${V}/admin/campaign/test_01/revert`, { token: admin.token, body });
        expect((await revert({ version: 1, base_version: 1, records: "reset" })).body.error.code).toBe("BASE_CHANGED");
        expect((await revert({ version: 9, base_version: 2, records: "reset" })).status).toBe(404);
        expect((await revert({ version: 2, base_version: 2, records: "reset" })).body.error.code).toBe("ALREADY_CURRENT");
        expect((await revert({ version: 1, base_version: 2 })).status).toBe(400);

        // « Réinitialiser »: revision 1 with the board it had.
        const back = await revert({ version: 1, base_version: 2, records: "reset" });
        expect(back.status, JSON.stringify(back.body)).toBe(200);
        expect(await board()).toEqual([["Kestrel", 61000]]);
        const statuses = await env.DB.prepare("SELECT version, compatibility_status FROM level_versions WHERE level_id = 'test_01' ORDER BY version").all();
        expect(statuses.results).toEqual([
            { version: 1, compatibility_status: "active" },
            { version: 2, compatibility_status: "legacy" },
        ]);
        // Runs of revision 1 are ranked again.
        expect((await api("POST", `${V}/records/submit`, { token: kes.token, body: submission({ time_ms: 70000 }) })).status).toBe(201);

        // « Garder »: back to revision 2 with the bests of revision 1 merged (best time wins).
        const again = await revert({ version: 2, base_version: 1, records: "keep" });
        expect(again.body.data).toMatchObject({ version: 2, records: "keep", carried_bests: 1 });
        expect(await board()).toEqual([
            ["Amelie", 58000],
            ["Kestrel", 61000],
        ]);
        // The next publication is revision 3, and the Journal has both returns.
        const list = await api("GET", `${V}/admin/campaign`, { token: admin.token });
        expect(list.body.data.planets.find((p: any) => p.planet_id === "testworld").levels[0].next_version).toBe(3);
        const log = await api("GET", `${V}/admin/log?kind=campaign`, { token: admin.token });
        expect(log.body.data.entries.map((e: any) => [e.action, e.campaign.version, e.campaign.records])).toEqual([
            ["admin_campaign_reverted", 2, "keep"],
            ["admin_campaign_reverted", 1, "reset"],
            ["admin_campaign_published", 2, "reset"],
        ]);
        const refused = await api("GET", `${V}/admin/log?kind=refused`, { token: admin.token });
        expect(refused.body.data.entries[0]).toMatchObject({ action: "admin_refused", reason: "BASE_CHANGED", campaign: { level_id: "test_01" } });
    });

    it("the public manifest: current revisions, their files, and those a challenge still plays", async () => {
        const admin = await registerPlayer("NovaRider");
        await makeAdmin(admin);
        let man = await api("GET", `${V}/campaign/revisions`);
        expect(man.status).toBe(200);
        expect(man.body.data.format).toBe("cosmic.campaign_revisions");
        const t1 = () => man.body.data.levels.find((l: any) => l.level_id === "test_01");
        expect(t1()).toMatchObject({ version: 1, content_hash: LEVEL_HASH, max_version: 1, file: null, pinned: [] });

        expect((await publish(admin, publishBody(2, NEW_HASH, "reset"))).status).toBe(201);
        // A challenge drawn on revision 2, still running when revision 3 comes.
        const day = new Date().toISOString().slice(0, 10);
        const end = new Date(Date.now() + 86400000).toISOString().slice(0, 10);
        await env.DB.prepare(
            `INSERT INTO challenges (id, kind, period, date, end_date, source, level_source, level_id, level_version, level_hash, level_title, hearts,
               slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, objective, created_at, updated_at)
             VALUES ('daily-' || ?4, 'daily', ?4, ?4, ?1, 'auto', 'campaign', 'test_01', 2, ?2, 'Niveau test', 3, 4, '[]', 1, 1,
               ?3, 90000, 60000, 1, 'speed', '2026-11-16T00:00:00Z', '2026-11-16T00:00:00Z')`,
        )
            .bind(end, NEW_HASH, "0".repeat(64), day)
            .run();
        expect((await publish(admin, publishBody(3, NEW_HASH_3, "reset"))).status).toBe(201);
        man = await api("GET", `${V}/campaign/revisions`);
        expect(t1()).toMatchObject({ version: 3, content_hash: NEW_HASH_3, max_version: 3, min_game_version: "2.45.0" });
        expect(t1().file).toMatchObject({ version: 3, path: `${V}/campaign/levels/test_01/revisions/3/file`, size: levelFile(3).length });
        expect(t1().rating).toMatchObject({ score: 3.5 });
        expect(t1().pinned.map((p: any) => [p.version, p.content_hash])).toEqual([[2, NEW_HASH]]);
        // Both files can be downloaded by anyone (they are part of the game).
        expect((await api("GET", t1().pinned[0].path)).status).toBe(200);
        expect((await api("GET", t1().file.path)).status).toBe(200);
        expect((await api("GET", `${V}/campaign/levels/test_01/revisions/abc/file`)).status).toBe(404);
    });
});
