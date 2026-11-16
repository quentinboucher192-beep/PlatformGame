// The native entities edited by an administrator (2.46): a native entity published as a
// revision together with the next revision of each campaign level that uses it, each with its
// own choice about the records (reset: new boards; keep: bests carried over), in one batch; the
// definition checked (a native, the game's format, sane numbers), each level file bound to the
// new revision; the public manifest and files; the Journal. The routes do not exist for a player.

import { SELF, env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { EXIT, LEVEL2_HASH, LEVEL_HASH, SPAWN, api, registerPlayer, seedAll, verifiedRun, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const H1 = "e".repeat(64);
const H2 = "f".repeat(64);

async function makeAdmin(p: TestPlayer): Promise<void> {
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
}

function definition(extra: Record<string, unknown> = {}): string {
    return (
        JSON.stringify(
            { format: "cosmic.enemy", id: "bomber", name: "Bombardier", animation: "animations/bomber.json", hp: 55, speed: 140, attacks: [{ type: "bomb", damage: 2 }], ...extra },
            null,
            1,
        ) + "\n"
    );
}

/** A level file of `id` (planet testworld) at `revision`, bound to bomber revision `bound`. */
function levelFile(id: string, revision: number, bound: Record<string, unknown> | null = { bomber: 1 }): string {
    return JSON.stringify(
        {
            format: "cosmic.level",
            formatVersion: 3,
            id,
            name: id,
            planet: "testworld",
            bounds: [0, 0, 6000, 1200],
            metadata: bound ? { revision, enemyRevisions: bound } : { revision },
            objects: [
                { id: "o1", type: "solid", rect: [0, 1100, 6000, 100] },
                { id: "b1", type: "enemy", rect: [900, 1000, 40, 40], props: { enemy: "bomber" } },
                { id: "start", type: "player_start", rect: [80, 1036, 40, 64] },
                { id: "exit", type: "exit", rect: [EXIT.x, EXIT.y, EXIT.w, EXIT.h] },
            ],
        },
        null,
        1,
    );
}

function manifestEntry(id: string, version: number, hash: string) {
    return {
        id,
        name: id,
        planet_id: "testworld",
        planet_name: "Monde test",
        order: id === "test_01" ? 1 : 2,
        version,
        content_hash: hash,
        total_collectibles: 10,
        total_enemies: 20,
        time_limit_ms: 900000,
        min_time_ms: 30000,
        bounds: [6000, 1200],
        spawn: [SPAWN.x, SPAWN.y],
        exit: [EXIT.x, EXIT.y, EXIT.w, EXIT.h],
    };
}

function levelEntry(id: string, records: string, hash: string, extra: Record<string, unknown> = {}) {
    return { level_id: id, base_version: 1, records, level_file: levelFile(id, 2), manifest: manifestEntry(id, 2, hash), rating: { version: 2, score: 3 }, ...extra };
}

function body(levels: unknown[], extra: Record<string, unknown> = {}) {
    return { definition: definition(), base_revision: 0, game_version: "2.46.0", levels, ...extra };
}

async function publish(admin: TestPlayer, b: Record<string, unknown>, enemy = "bomber") {
    return api("POST", `${V}/admin/enemies/${enemy}/revisions`, { token: admin.token, body: b });
}

async function board(level: string, query = ""): Promise<[string, number][]> {
    const res = await api("GET", `${V}/leaderboards/level/${level}${query}`);
    expect(res.status).toBe(200);
    return res.body.data.entries.map((e: any) => [e.player.username, e.time_ms]);
}

async function nothingStored(): Promise<void> {
    expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM enemy_revisions").first<{ n: number }>())?.n).toBe(0);
    expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM level_versions WHERE version = 2 AND level_id LIKE 'test_%'").first<{ n: number }>())?.n).toBe(0);
    expect((await env.STORAGE.list({ prefix: "enemies/" })).objects).toHaveLength(0);
}

describe("enemy revisions (admin)", () => {
    it("the routes do not exist for a player, and need a session", async () => {
        const p = await registerPlayer("Kestrel");
        for (const [m, path, b] of [
            ["GET", `${V}/admin/enemies`, undefined],
            ["GET", `${V}/admin/enemies/bomber/revisions`, undefined],
            ["POST", `${V}/admin/enemies/bomber/revisions`, body([])],
        ] as const) {
            expect((await api(m, path, { body: b })).status).toBe(401);
            const res = await api(m, path, { token: p.token, body: b });
            expect(res.status).toBe(404);
            expect(res.body.error.code).toBe("NOT_FOUND");
        }
        await nothingStored();
    });

    it("lists the natives, and an entity's levels with their revision and bests", async () => {
        const admin = await registerPlayer("NovaRider");
        await makeAdmin(admin);
        const kes = await registerPlayer("Kestrel");
        await verifiedRun(kes, 61000);
        const list = await api("GET", `${V}/admin/enemies`, { token: admin.token });
        expect(list.status).toBe(200);
        expect(list.body.data.enemies.find((e: any) => e.enemy_id === "bomber")).toMatchObject({ current: null, revisions: 0 });
        expect((await api("GET", `${V}/admin/enemies/not_an_enemy/revisions`, { token: admin.token })).status).toBe(404);
        const hist = await api("GET", `${V}/admin/enemies/bomber/revisions`, { token: admin.token });
        expect(hist.body.data).toMatchObject({ enemy_id: "bomber", current_revision: 0, next_revision: 1, revisions: [] });
        expect(hist.body.data.levels.find((l: any) => l.level_id === "test_01")).toMatchObject({ version: 1, next_version: 2, content_hash: LEVEL_HASH, bests: 1 });
    });

    it("checks the entity, its definition and every level file, never trusting the game", async () => {
        const admin = await registerPlayer("NovaRider");
        await makeAdmin(admin);
        const bad = async (b: Record<string, unknown>, status: number, code: string, enemy = "bomber") => {
            const res = await publish(admin, b, enemy);
            expect(res.status, JSON.stringify(res.body)).toBe(status);
            expect(res.body.error.code).toBe(code);
        };
        const ok = [levelEntry("test_01", "reset", H1)];
        await bad(body(ok), 404, "ENEMY_NOT_FOUND", "my_own_enemy");
        await bad(body(ok, { definition: definition({ id: "walker" }) }), 422, "DEFINITION_INVALID");
        await bad(body(ok, { definition: definition({ format: "autre" }) }), 422, "DEFINITION_INVALID");
        await bad(body(ok, { definition: definition({ hp: 1e12 }) }), 422, "DEFINITION_INVALID");
        await bad(body(ok, { definition: "{ pas du json" }), 422, "DEFINITION_INVALID");
        await bad(body(ok, { base_revision: 3 }), 409, "BASE_CHANGED");
        await bad(body([levelEntry("test_01", "maybe", H1)]), 400, "VALIDATION_ERROR");
        await bad(body([levelEntry("test_01", "reset", H1, { base_version: 4 })]), 409, "BASE_CHANGED");
        await bad(body([levelEntry("test_01", "reset", H1), levelEntry("test_01", "keep", H2)]), 400, "VALIDATION_ERROR");
        // The level must be bound to the new revision, and only to revisions that exist.
        await bad(body([levelEntry("test_01", "reset", H1, { level_file: levelFile("test_01", 2, null) })]), 422, "LEVEL_INVALID");
        await bad(body([levelEntry("test_01", "reset", H1, { level_file: levelFile("test_01", 2, { bomber: 2 }) })]), 422, "LEVEL_INVALID");
        await bad(body([levelEntry("test_01", "reset", H1, { level_file: levelFile("test_01", 2, { bomber: 1, walker: 1 }) })]), 422, "LEVEL_INVALID");
        await bad(body([levelEntry("test_01", "reset", H1, { level_file: levelFile("test_01", 3) })]), 422, "LEVEL_INVALID");
        await bad(body([levelEntry("test_01", "reset", LEVEL_HASH)]), 409, "HASH_EXISTS");
        await bad(body([levelEntry("test_01", "reset", H1, { manifest: { ...manifestEntry("test_01", 2, H1), bounds: [5000, 1200] } })]), 422, "MANIFEST_MISMATCH");
        await nothingStored();
        // Each refusal of a stale base is in the Journal.
        const log = await api("GET", `${V}/admin/log?kind=refused`, { token: admin.token });
        expect(log.body.data.entries.some((e: any) => e.enemy?.enemy_id === "bomber")).toBe(true);
    });

    it("publishes in one batch, each level with its own choice: reset or keep", async () => {
        const admin = await registerPlayer("NovaRider");
        await makeAdmin(admin);
        const kes = await registerPlayer("Kestrel");
        await verifiedRun(kes, 61000);
        await verifiedRun(kes, 62000, { level_id: "test_02", level_hash: LEVEL2_HASH });
        expect(await board("test_01")).toEqual([["Kestrel", 61000]]);
        expect(await board("test_02")).toEqual([["Kestrel", 62000]]);

        const b = body([levelEntry("test_01", "reset", H1), levelEntry("test_02", "keep", H2)]);
        const res = await publish(admin, b);
        expect(res.status, JSON.stringify(res.body)).toBe(201);
        expect(res.body.data).toMatchObject({ enemy_id: "bomber", revision: 1, path: `${V}/enemies/bomber/revisions/1/file` });
        expect(res.body.data.levels).toEqual([
            expect.objectContaining({ level_id: "test_01", version: 2, content_hash: H1, records: "reset", carried_bests: 0 }),
            expect.objectContaining({ level_id: "test_02", version: 2, content_hash: H2, records: "keep", carried_bests: 1 }),
        ]);
        // test_01: new boards, the old ones archived; test_02: the bests carried over.
        expect(await board("test_01")).toEqual([]);
        expect(await board("test_01", `?level_hash=${LEVEL_HASH}`)).toEqual([["Kestrel", 61000]]);
        expect(await board("test_02")).toEqual([["Kestrel", 62000]]);

        // The definition: kept byte for byte, served with its SHA-256; listed by the manifest.
        const dl = await SELF.fetch(`http://cosmic.test${res.body.data.path}`);
        expect(dl.status).toBe(200);
        expect(await dl.text()).toBe(b.definition);
        expect(dl.headers.get("x-content-sha256")).toBe(res.body.data.sha256);
        const man = await api("GET", `${V}/enemies/revisions`);
        expect(man.body.data).toMatchObject({
            format: "cosmic.enemy_revisions",
            enemies: [{ enemy_id: "bomber", current: 1, revisions: [{ revision: 1, sha256: res.body.data.sha256, min_game_version: "2.46.0" }] }],
        });
        // The level revisions are campaign revisions like the others (manifest, files, history).
        const camp = await api("GET", `${V}/campaign/revisions`);
        const t2 = camp.body.data.levels.find((l: any) => l.level_id === "test_02");
        expect(t2).toMatchObject({ version: 2, content_hash: H2, file: { path: `${V}/campaign/levels/test_02/revisions/2/file` } });
        const ch = await api("GET", `${V}/admin/campaign/test_02/revisions`, { token: admin.token });
        expect(ch.body.data.changes[0]).toMatchObject({ action: "publish", records: "keep", from_version: 1 });

        // History and Journal: who, which revision, which levels with which choice.
        const hist = await api("GET", `${V}/admin/enemies/bomber/revisions`, { token: admin.token });
        expect(hist.body.data).toMatchObject({ current_revision: 1, next_revision: 2, current_revisions: { bomber: 1 } });
        expect(hist.body.data.revisions[0]).toMatchObject({ revision: 1, is_current: true, published_by: { username: "NovaRider" } });
        expect(hist.body.data.revisions[0].levels.map((l: any) => [l.level_id, l.records])).toEqual([
            ["test_01", "reset"],
            ["test_02", "keep"],
        ]);
        const log = await api("GET", `${V}/admin/log?kind=enemy`, { token: admin.token });
        expect(log.body.data.entries).toHaveLength(1);
        expect(log.body.data.entries[0]).toMatchObject({
            action: "admin_enemy_published",
            enemy: { enemy_id: "bomber", revision: 1, from_revision: 0, levels: [{ level_id: "test_01", records: "reset" }, { level_id: "test_02", records: "keep", carried_bests: 1 }] },
        });

        // The same base again: refused (another revision was published in between).
        const again = await publish(admin, body([levelEntry("test_01", "reset", "1".repeat(64), { base_version: 2, level_file: levelFile("test_01", 3) })]));
        expect(again.status).toBe(409);
        expect(again.body.error.code).toBe("BASE_CHANGED");
        // A revision without any level (an entity no campaign level uses) is fine.
        const alone = await publish(admin, body([], { base_revision: 1 }));
        expect(alone.status, JSON.stringify(alone.body)).toBe(201);
        expect(alone.body.data.revision).toBe(2);
        expect((await env.DB.prepare("SELECT current_version FROM levels WHERE id = 'test_01'").first<{ current_version: number }>())?.current_version).toBe(2);
    });
});
