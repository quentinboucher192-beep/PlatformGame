// EXTRÊME levels: published without their author's completion, one life, and ranked by the
// « course à la vérification » — the progress score computed by the server from the replay
// (route, bosses' health, ambushes' waves); the first to finish verifies the level.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { api, buildReplay, EXIT, registerPlayer, seedAll, SPAWN, type TestPlayer } from "./helpers";
import { LH, octet, PNG, zip } from "./publish";

beforeEach(seedAll);

const V = "/api/v1";

/** The intended route, dense like the game's (from the start to the exit). */
const ROUTE: [number, number][] = Array.from({ length: 60 }, (_, i) => [SPAWN.x + ((EXIT.x + 10 - SPAWN.x) * i) / 59, SPAWN.y + ((EXIT.y + 40 - SPAWN.y) * i) / 59]);
const LAYOUT = { route: ROUTE, bosses: [{ id: "b1", name: "Gardien" }], ambushes: [{ id: "z1", waves: 2 }] };

function extremeLevel(lives: number) {
    return JSON.stringify({
        format: "cosmic.level",
        id: "mon_niveau",
        name: "Mon niveau",
        extreme: true,
        lives,
        bounds: [0, 0, 6000, 1200],
        timer: { mode: "limited", limit: 600 },
        objects: [
            { id: "o1", type: "player_start", rect: [SPAWN.x - 5, SPAWN.y - 22, 10, 22] },
            { id: "o2", type: "exit", rect: [EXIT.x, EXIT.y, EXIT.w, EXIT.h] },
        ],
    });
}

async function publishExtreme(author: TestPlayer, lives = 1) {
    const start = await api("POST", `${V}/community/levels`, {
        token: author.token,
        body: { title: "Le Gouffre Final", planet: "pyralis", difficulty: 5, level_hash: LH, min_game_version: "2.16.0", race: LAYOUT },
    });
    expect(start.status).toBe(201);
    const packageData = await zip({
        "COSMIC_LEVEL/manifest.json": JSON.stringify({ format: "cosmic.package", levelId: "mon_niveau" }),
        "COSMIC_LEVEL/level.json": extremeLevel(lives),
        "COSMIC_LEVEL/metadata/thumbnail.png": PNG,
    });
    const p = await api("PUT", start.body.data.upload.package, { token: author.token, raw: packageData, headers: octet });
    return { id: start.body.data.id as string, put: p };
}

/** One attempt: declare it, then send its replay (the server computes the score). */
async function attempt(p: TestPlayer, levelId: string, opts: { upTo: number; boss: number; waves: number; finished?: boolean; claimBoss?: number }) {
    const attemptId = crypto.randomUUID();
    const durationMs = 40000;
    const sub = await api("POST", `${V}/races/runs`, {
        token: p.token,
        body: { kind: "community", level_id: levelId, attempt_id: attemptId, time_ms: durationMs, finished: opts.finished === true, bosses: [opts.claimBoss ?? opts.boss], waves: [opts.waves] },
    });
    expect(sub.status, JSON.stringify(sub.body)).toBe(201);
    const extra = [];
    if (opts.boss > 0) extra.push({ frame: 200, type: 12, arg: 0, value: opts.boss });
    if (opts.waves > 0) extra.push({ frame: 300, type: 13, arg: 0, value: opts.waves });
    if (!opts.finished) extra.push({ frame: 100000, type: 3, arg: 0, value: 0 }); // the one death ends it
    const replay = await buildReplay({
        levelId: "mon_niveau",
        levelHash: LH,
        attemptId,
        durationMs,
        collectibles: 0,
        finish: opts.finished === true,
        extraEvents: extra,
        path: (i, n) => {
            const t = (opts.finished ? 1 : opts.upTo) * (i / (n - 1));
            return { x: SPAWN.x + (EXIT.x + 10 - SPAWN.x) * t, y: SPAWN.y + (EXIT.y + 40 - SPAWN.y) * t };
        },
    });
    const r = await api("PUT", sub.body.data.upload, { token: p.token, raw: replay, headers: octet });
    expect(r.status, JSON.stringify(r.body)).toBe(200);
    return r.body.data;
}

describe("course à la vérification", () => {
    it("an extreme level needs one life, and is published without its author's completion", async () => {
        const author = await registerPlayer("Createur");
        const bad = await publishExtreme(author, 3);
        expect(bad.put.status).toBe(422);
        expect(bad.put.body.error.code).toBe("EXTREME_LIVES");
        const ok = await publishExtreme(author, 1);
        expect(ok.put.status).toBe(200);
        expect(ok.put.body.data.published).toBe(true);
        const card = await api("GET", `${V}/community/levels/${ok.id}`, { token: author.token });
        expect(card.body.data.level).toMatchObject({ extreme: true, verified: null });
    });

    it("attempts are ranked by the server's score; the first to finish verifies the level", async () => {
        const author = await registerPlayer("Createur");
        const { id } = await publishExtreme(author);
        const [ana, ben, cyd] = await Promise.all(["Ana", "Ben", "Cyd"].map((n) => registerPlayer(n)));
        // Ana: half the route, half the boss, 1 wave of 2 -> 40×0.5 + 60×(2×0.5 + 0.5)/3 ≈ 50.
        const a = await attempt(ana!, id, { upTo: 0.5, boss: 500, waves: 1 });
        expect(a.run.status).toBe("VERIFIED");
        expect(a.run.score).toBeGreaterThan(48);
        expect(a.run.score).toBeLessThan(51);
        expect(a.run.rank).toBe(1);
        // Ben declares more than his replay shows: refused (the score is the server's).
        const b = await attempt(ben!, id, { upTo: 0.3, boss: 100, waves: 0, claimBoss: 900 });
        expect(b.run.status).toBe("REJECTED");
        expect(b.run.reasons.join(" ")).toContain("boss");
        // Cyd finishes: 100, verifier; Ana and the author are told.
        const c = await attempt(cyd!, id, { upTo: 1, boss: 1000, waves: 2, finished: true });
        expect(c.run).toMatchObject({ status: "VERIFIED", score: 100, finished: true, rank: 1 });
        expect(c.verifier).toBe(true);
        const board = await api("GET", `${V}/races/board?kind=community&level_id=${id}`, { token: ana!.token });
        expect(board.body.data.verifier.player.username).toBe("Cyd");
        expect(board.body.data.ranking.map((r: { player: { username: string } }) => r.player.username)).toEqual(["Cyd", "Ana"]);
        expect(board.body.data.ranking[1].detail.bosses[0]).toMatchObject({ name: "Gardien", removed: 0.5 });
        // Like the leaderboards (2.23): the frame, title and suit of each player.
        expect(board.body.data.ranking[0].player).toMatchObject({ frame: "simple", title: null, suit: "standard" });
        for (const who of [ana!, author]) {
            const n = await api("GET", `${V}/notifications`, { token: who.token });
            expect(n.body.data.notifications.some((x: { kind: string }) => x.kind === "race_verified")).toBe(true);
        }
        const card = await api("GET", `${V}/community/levels/${id}`, { token: ana!.token });
        expect(card.body.data.level.verified.player).toBe("Cyd");
        expect(card.body.data.level.race_best).toBe(100);
        // A second finisher does not take the title.
        const d = await attempt(ana!, id, { upTo: 1, boss: 1000, waves: 2, finished: true });
        expect(d.verifier).toBe(false);
    });

    it("every ranked attempt's replay can be watched from the board, never a private player's (2.32.2)", async () => {
        const author = await registerPlayer("Createur");
        const { id } = await publishExtreme(author);
        const [ana, ben] = await Promise.all(["Ana", "Ben"].map((n) => registerPlayer(n)));
        await attempt(ana!, id, { upTo: 0.5, boss: 500, waves: 1 });
        await attempt(ben!, id, { upTo: 0.3, boss: 0, waves: 0 });
        const board = await api("GET", `${V}/races/board?kind=community&level_id=${id}`);
        const rows = board.body.data.ranking as { run_id: string; has_replay: boolean; player: { username: string } }[];
        expect(rows.map((r) => r.has_replay)).toEqual([true, true]);
        for (const r of rows) {
            const rep = await api("GET", `${V}/races/runs/${r.run_id}/replay`);
            expect(rep.status).toBe(200);
            expect((rep.body as ArrayBuffer).byteLength).toBeGreaterThan(16);
        }
        // Ben hides his profile: his replay is no longer served to others (he still sees it).
        await env.DB.prepare("UPDATE players SET profile_visibility = 'private' WHERE id = ?1").bind(ben!.id).run();
        const benRun = rows.find((r) => r.player.username === "Ben")!.run_id;
        expect((await api("GET", `${V}/races/runs/${benRun}/replay`)).status).toBe(403);
        expect((await api("GET", `${V}/races/runs/${benRun}/replay`, { token: ben!.token })).status).toBe(200);
        const again = await api("GET", `${V}/races/board?kind=community&level_id=${id}`);
        expect(again.body.data.ranking.find((r: { player: { username: string } }) => r.player.username === "Ben").has_replay).toBe(false);
        expect((await api("GET", `${V}/races/runs/00000000-0000-4000-8000-000000000000/replay`)).status).toBe(404);
    });

    it("a player's profile lists their best progression on every EXTRÊME level, hidden when private (2.32.2)", async () => {
        const author = await registerPlayer("Createur");
        const { id } = await publishExtreme(author);
        const [ana, ben] = await Promise.all(["Ana", "Ben"].map((n) => registerPlayer(n)));
        await attempt(ana!, id, { upTo: 0.2, boss: 0, waves: 0 });
        await attempt(ana!, id, { upTo: 0.5, boss: 500, waves: 1 });
        await attempt(ben!, id, { upTo: 0.6, boss: 1000, waves: 2 });
        const list = await api("GET", `${V}/players/${ana!.id}/races`);
        expect(list.status).toBe(200);
        expect(list.body.data.races).toHaveLength(1);
        expect(list.body.data.races[0]).toMatchObject({ kind: "community", level_id: id, version: 1, rank: 2, participants: 2, finished: false, has_replay: true });
        expect(list.body.data.races[0].score).toBeGreaterThan(48);
        await env.DB.prepare("UPDATE players SET profile_visibility = 'private' WHERE id = ?1").bind(ana!.id).run();
        expect((await api("GET", `${V}/players/${ana!.id}/races`)).status).toBe(403);
        expect((await api("GET", `${V}/players/${ana!.id}/races`, { token: ana!.token })).status).toBe(200);
        expect((await api("GET", `${V}/players/00000000-0000-4000-8000-000000000000/races`)).status).toBe(404);
    });

    it("a player who comes back counts with his best attempt; ties go to whoever was first", async () => {
        const author = await registerPlayer("Createur");
        const { id } = await publishExtreme(author);
        const [ana, ben] = await Promise.all(["Ana", "Ben"].map((n) => registerPlayer(n)));
        await attempt(ana!, id, { upTo: 0.2, boss: 0, waves: 0 });
        await attempt(ben!, id, { upTo: 0.6, boss: 0, waves: 0 });
        await attempt(ana!, id, { upTo: 0.6, boss: 0, waves: 0 });
        const board = await api("GET", `${V}/races/board?kind=community&level_id=${id}`);
        expect(board.body.data.ranking).toHaveLength(2);
        expect(board.body.data.ranking[0].player.username).toBe("Ben");  // same score, earlier
        expect(board.body.data.ranking[0].score).toBe(board.body.data.ranking[1].score);
    });

    it("the race leaves with its level, and a player's attempts are in their data export", async () => {
        const author = await registerPlayer("Createur");
        const { id } = await publishExtreme(author);
        const ana = await registerPlayer("Ana");
        await attempt(ana, id, { upTo: 0.4, boss: 300, waves: 1 });
        const exp = await api("GET", `${V}/players/me/export`, { token: ana.token });
        expect(exp.status).toBe(200);
        expect(exp.body.data.race_attempts).toHaveLength(1);
        expect(exp.body.data.race_attempts[0]).toMatchObject({ board: `c:${id}:1`, status: "VERIFIED" });
        const del = await api("DELETE", `${V}/community/levels/${id}`, { token: author.token });
        expect(del.status).toBe(200);
        const left = await env.DB.prepare("SELECT COUNT(*) AS n FROM race_runs WHERE board LIKE ?1").bind(`c:${id}:%`).first<{ n: number }>();
        expect(left!.n).toBe(0);
    });

    it("official extreme levels have a race too (layout registered with the level version)", async () => {
        const p = await registerPlayer("Ana");
        const lv = await env.DB.prepare("SELECT level_id, content_hash FROM level_versions WHERE compatibility_status = 'active' LIMIT 1").first<{ level_id: string; content_hash: string }>();
        const sub0 = await api("POST", `${V}/races/runs`, {
            token: p.token,
            body: { kind: "official", level_id: lv!.level_id, level_hash: lv!.content_hash, attempt_id: crypto.randomUUID(), time_ms: 1000, finished: false, bosses: [], waves: [] },
        });
        expect(sub0.status).toBe(404);  // not an extreme level
        await env.DB.prepare("UPDATE level_versions SET race = ?2 WHERE content_hash = ?1").bind(lv!.content_hash, JSON.stringify({ ...LAYOUT, bosses: [], ambushes: [] })).run();
        const board = await api("GET", `${V}/races/board?kind=official&level_id=${lv!.level_id}`);
        expect(board.status).toBe(200);
        expect(board.body.data.race).toMatchObject({ kind: "official", bosses: [], ambushes: 0 });
    });
});
