import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import {
    EXIT,
    LEVEL_HASH,
    LEVEL_HASH_V2,
    api,
    buildReplay,
    publishV2,
    registerPlayer,
    seedAll,
    setTrust,
    submission,
    uploadReplay,
    verifiedRun,
} from "./helpers";

beforeEach(seedAll);

describe("record submission", () => {
    it("requires an authenticated session and ignores any client-provided player id", async () => {
        const anon = await api("POST", "/api/v1/records/submit", { body: submission() });
        expect(anon.status).toBe(401);
        const p = await registerPlayer("Nova");
        const spoof = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission({ player_id: "someone-else" }) });
        expect(spoof.status).toBe(400);
        expect(spoof.body.error.details[0]).toMatchObject({ field: "player_id", message: "champ inconnu" });
    });

    it("stores a new player's run as PENDING until a replay is analysed", async () => {
        const p = await registerPlayer("Nova");
        const res = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission() });
        expect(res.status).toBe(201);
        expect(res.body.data.record.verification_status).toBe("PENDING");
        expect(res.body.data.replay.required).toBe(true);
        // A pending run is never ranked.
        const board = await api("GET", "/api/v1/leaderboards/level/test_01");
        expect(board.body.data.entries).toHaveLength(0);
        const mine = await api("GET", "/api/v1/leaderboards/level/test_01", { token: p.token });
        expect(mine.body.data.me.entry).toBeNull();
        expect(mine.body.data.me.pending.status).toBe("PENDING");
    });

    it("verifies a run with a consistent replay and ranks it", async () => {
        const p = await registerPlayer("Nova");
        const run = await verifiedRun(p, 62000);
        expect(run.status).toBe("VERIFIED");
        expect(run.body.data.analysis.ok).toBe(true);
        const ranking = run.body.data.records[0].ranking;
        expect(ranking).toMatchObject({ previous_rank: null, rank: 1, total: 1, personal_best_improved: true });
        const board = await api("GET", "/api/v1/leaderboards/level/test_01");
        expect(board.body.data.entries[0]).toMatchObject({ rank: 1, time_ms: 62000, has_replay: true });
        // The replay is stored in the private bucket and served through the API only.
        const replay = await api("GET", `/api/v1/records/${run.recordId}/replay`);
        expect(replay.status).toBe(200);
        expect(replay.headers.get("content-type")).toBe("application/octet-stream");
    });

    it("is idempotent for the same attempt and refuses a conflicting resubmission", async () => {
        const p = await registerPlayer("Nova");
        const sub = submission();
        const a = await api("POST", "/api/v1/records/submit", { token: p.token, body: sub });
        expect(a.status).toBe(201);
        const b = await api("POST", "/api/v1/records/submit", { token: p.token, body: sub });
        expect(b.status).toBe(200);
        expect(b.body.data.duplicate).toBe(true);
        expect(b.body.data.record.id).toBe(a.body.data.record.id);
        const c = await api("POST", "/api/v1/records/submit", { token: p.token, body: { ...sub, time_ms: 31000 } });
        expect(c.status).toBe(409);
        expect(c.body.error.code).toBe("ATTEMPT_CONFLICT");
        const count = await env.DB.prepare("SELECT COUNT(*) AS n FROM records").first<{ n: number }>();
        expect(count?.n).toBe(1);
        // The same attempt may count in another category it qualifies for.
        const d = await api("POST", "/api/v1/records/submit", { token: p.token, body: { ...sub, category: "NO_DEATH" } });
        expect(d.status).toBe(201);
    });

    it("refuses unknown levels, other revisions, disabled boards and old game versions", async () => {
        const p = await registerPlayer("Nova");
        const unknown = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission({ level_id: "nowhere" }) });
        expect(unknown.status).toBe(404);
        const hash = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission({ level_hash: "f".repeat(64) }) });
        expect(hash.status).toBe(409);
        expect(hash.body.error.code).toBe("LEVEL_VERSION_MISMATCH");
        const locked = await api("POST", "/api/v1/records/submit", {
            token: p.token,
            body: submission({ level_id: "locked_01", level_hash: "c".repeat(64) }),
        });
        expect(locked.status).toBe(403);
        expect(locked.body.error.code).toBe("RANKING_DISABLED");
        const old = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission({ game_version: "2.0.0" }) });
        expect(old.status).toBe(426);
        await publishV2();
        const retired = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission() });
        expect(retired.status).toBe(409);
        expect(retired.body.error.code).toBe("LEVEL_VERSION_RETIRED");
        const v2 = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission({ level_version: 2, level_hash: LEVEL_HASH_V2 }) });
        expect(v2.status).toBe(201);
    });

    it("enforces category rules and progression consistency", async () => {
        const p = await registerPlayer("Nova");
        const noDeath = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission({ category: "NO_DEATH", deaths: 2 }) });
        expect(noDeath.status).toBe(422);
        expect(noDeath.body.error.code).toBe("CATEGORY_RULES_NOT_MET");
        const noDamage = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission({ category: "NO_DAMAGE", damage_taken: 3 }) });
        expect(noDamage.status).toBe(422);
        const hundred = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission({ category: "HUNDRED_PERCENT", collectibles: 9 }) });
        expect(hundred.status).toBe(422);
        const okHundred = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission({ category: "HUNDRED_PERCENT", collectibles: 10 }) });
        expect(okHundred.status).toBe(201);
        const tooMany = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission({ collectibles: 11 }) });
        expect(tooMany.status).toBe(422);
        const wrongTotal = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission({ total_collectibles: 3 }) });
        expect(wrongTotal.status).toBe(422);
        expect(wrongTotal.body.error.code).toBe("INCONSISTENT_PROGRESS");
    });

    it("validates every field strictly", async () => {
        const p = await registerPlayer("Nova");
        const cases: Record<string, unknown>[] = [
            { time_ms: -5 },
            { time_ms: 12.5 },
            { time_ms: "60000" },
            { deaths: -1 },
            { category: "TOOL_ASSISTED" },
            { attempt_id: "not-a-uuid" },
            { level_hash: "XYZ" },
            { level_id: "../../etc" },
            { level_id: "x'; DROP TABLE records; --" },
            { game_version: "latest" },
        ];
        for (const c of cases) {
            const r = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission(c) });
            expect(r.status, JSON.stringify(c)).toBe(400);
            expect(r.body.error.code).toBe("VALIDATION_ERROR");
        }
        const big = await api("POST", "/api/v1/records/submit", { token: p.token, raw: JSON.stringify({ pad: "x".repeat(10000) }), headers: { "content-type": "application/json" } });
        expect(big.status).toBe(413);
        expect(await env.DB.prepare("SELECT COUNT(*) AS n FROM records").first("n")).toBe(0);
    });

    it("rejects impossible times and lowers the trust level", async () => {
        const p = await registerPlayer("Nova");
        await setTrust(p.id, 1);
        const res = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission({ time_ms: 1200 }) });
        expect(res.status).toBe(201);
        expect(res.body.data.record.verification_status).toBe("REJECTED");
        expect(res.body.data.record.rejection_reason).toBe("IMPOSSIBLE_TIME");
        const trust = await env.DB.prepare("SELECT trust_level FROM players WHERE id = ?1").bind(p.id).first("trust_level");
        expect(trust).toBe(0);
        const over = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission({ time_ms: 950000 }) });
        expect(over.body.data.record.verification_status).toBe("REJECTED");
    });

    it("lets trusted players skip the replay, except for SPEEDRUN", async () => {
        const p = await registerPlayer("Veteran");
        await setTrust(p.id, 2);
        const any = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission() });
        expect(any.body.data.record.verification_status).toBe("VERIFIED");
        const speed = await api("POST", "/api/v1/records/submit", { token: p.token, body: submission({ category: "SPEEDRUN" }) });
        expect(speed.body.data.record.verification_status).toBe("PENDING");
    });

    it("updates personal bests and reports rank changes", async () => {
        const a = await registerPlayer("Alpha");
        const b = await registerPlayer("Bravo");
        await verifiedRun(a, 70000);
        await verifiedRun(b, 65000);
        const slower = await verifiedRun(a, 80000);
        expect(slower.body.data.records[0].ranking).toMatchObject({ previous_rank: 2, rank: 2, personal_best_improved: false, previous_best_ms: 70000 });
        const faster = await verifiedRun(a, 61000);
        expect(faster.body.data.records[0].ranking).toMatchObject({ previous_rank: 2, rank: 1, total: 2, personal_best_improved: true, previous_best_ms: 70000 });
        const board = await api("GET", "/api/v1/leaderboards/level/test_01");
        expect(board.body.data.entries.map((e: any) => [e.player.username, e.time_ms])).toEqual([
            ["Alpha", 61000],
            ["Bravo", 65000],
        ]);
        // History is kept.
        const history = await api("GET", `/api/v1/players/${a.id}/records`, { token: a.token });
        expect(history.body.data.records).toHaveLength(3);
    });

    it("holds a crushing world record of a new account for manual review", async () => {
        const players = [];
        for (let i = 0; i < 5; ++i) players.push(await registerPlayer(`Runner${i}x`));
        for (let i = 0; i < 5; ++i) await verifiedRun(players[i]!, 70000 + i * 1000);
        const rookie = await registerPlayer("Rookie");
        const wr = await verifiedRun(rookie, 50000);
        expect(wr.status).toBe("PENDING");
        expect(wr.body.data.records[0].record.verification_notes).toContain("vérification manuelle");
        // Moderation with the admin secret.
        const noAuth = await api("POST", `/api/v1/admin/records/${wr.recordId}/review`, { body: { decision: "VERIFIED" } });
        expect(noAuth.status).toBe(401);
        const ok = await api("POST", `/api/v1/admin/records/${wr.recordId}/review`, {
            headers: { authorization: `Bearer ${env.ADMIN_TOKEN}` },
            body: { decision: "VERIFIED", reason: "Replay vérifié" },
        });
        expect(ok.status).toBe(200);
        expect(ok.body.data.ranking.rank).toBe(1);
    });
});

describe("replays", () => {
    async function pending(name: string, overrides: Record<string, unknown> = {}) {
        const p = await registerPlayer(name);
        const sub = submission(overrides);
        const res = await api("POST", "/api/v1/records/submit", { token: p.token, body: sub });
        return { p, sub, recordId: res.body.data.record.id as string };
    }

    it("rejects a replay inconsistent with the submitted run", async () => {
        const { p, sub, recordId } = await pending("Nova");
        const replay = await buildReplay({ attemptId: sub.attempt_id as string, durationMs: 60000, deaths: 3 });
        const up = await uploadReplay(p.token, recordId, replay);
        expect(up.status).toBe(201);
        expect(up.body.data.verification_status).toBe("REJECTED");
        expect(up.body.data.analysis.reasons.join(" ")).toContain("morts");
        expect(up.body.data.records[0].record.verification_status).toBe("REJECTED");
    });

    it("rejects teleporting trajectories and wrong endings", async () => {
        const { p, sub, recordId } = await pending("Nova");
        const replay = await buildReplay({
            attemptId: sub.attempt_id as string,
            durationMs: 60000,
            path: (i, n) => (i < n / 2 ? { x: 100, y: 500 } : { x: EXIT.x + 10, y: EXIT.y + 40 }),
        });
        const up = await uploadReplay(p.token, recordId, replay);
        expect(up.body.data.verification_status).toBe("REJECTED");
        expect(up.body.data.analysis.reasons.join(" ")).toContain("impossible");
        const second = await pending("Orion");
        const nowhere = await buildReplay({ attemptId: second.sub.attempt_id as string, durationMs: 60000, path: (i) => ({ x: 100 + i, y: 500 }) });
        const up2 = await uploadReplay(second.p.token, second.recordId, nowhere);
        expect(up2.body.data.analysis.reasons.join(" ")).toContain("sortie");
    });

    it("refuses malformed files without storing anything", async () => {
        const { p, sub, recordId } = await pending("Nova");
        for (const corrupt of ["magic", "truncated", "bomb"] as const) {
            const replay = await buildReplay({ attemptId: sub.attempt_id as string, durationMs: 60000, corrupt });
            const up = await uploadReplay(p.token, recordId, replay);
            expect(up.status, corrupt).toBe(422);
            expect(up.body.error.code).toBe("REPLAY_MALFORMED");
        }
        const junk = await uploadReplay(p.token, recordId, new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]));
        expect(junk.status).toBe(422);
        const huge = await uploadReplay(p.token, recordId, new Uint8Array(1024 * 1024 + 10));
        expect(huge.status).toBe(413);
        expect(await env.DB.prepare("SELECT COUNT(*) AS n FROM replay_metadata").first("n")).toBe(0);
    });

    it("only lets the owner upload, once", async () => {
        const { p, sub, recordId } = await pending("Nova");
        const other = await registerPlayer("Thief");
        const replay = await buildReplay({ attemptId: sub.attempt_id as string, durationMs: 60000 });
        expect((await uploadReplay(other.token, recordId, replay)).status).toBe(403);
        expect((await uploadReplay(p.token, recordId, replay)).status).toBe(201);
        expect((await uploadReplay(p.token, recordId, replay)).status).toBe(409);
    });

    it("hides pending records and replays from other players", async () => {
        const { p, recordId } = await pending("Nova");
        expect((await api("GET", `/api/v1/records/${recordId}`)).status).toBe(404);
        expect((await api("GET", `/api/v1/records/${recordId}`, { token: p.token })).status).toBe(200);
        expect((await api("GET", `/api/v1/records/${recordId}/replay`)).status).toBe(404);
        expect((await api("GET", "/api/v1/records/not-an-id")).status).toBe(404);
    });

    it("keeps level revisions apart", async () => {
        const p = await registerPlayer("Nova");
        await verifiedRun(p, 60000);
        await publishV2();
        const current = await api("GET", "/api/v1/leaderboards/level/test_01");
        expect(current.body.data.board.version).toBe(2);
        expect(current.body.data.entries).toHaveLength(0);
        const legacy = await api("GET", "/api/v1/leaderboards/level/test_01?level_version=1");
        expect(legacy.body.data.board.content_hash).toBe(LEVEL_HASH);
        expect(legacy.body.data.board.is_current).toBe(false);
        expect(legacy.body.data.entries).toHaveLength(1);
    });
});

describe("level registration (admin)", () => {
    const level = (overrides: Record<string, unknown> = {}) => ({
        id: "new_01", name: "Nouveau", planet_id: "testworld", planet_name: "Monde test", order: 3, version: 1,
        content_hash: "e".repeat(64), total_collectibles: 5, total_enemies: 2, time_limit_ms: 600000, min_time_ms: 20000,
        bounds: [4000, 1000], spawn: [100, 800], exit: [3800, 700, 40, 64], ...overrides,
    });
    const admin = { authorization: `Bearer ${env.ADMIN_TOKEN}` };

    it("registers revisions, keeps old ones as legacy and refuses conflicting content", async () => {
        expect((await api("POST", "/api/v1/admin/levels", { body: { levels: [level()] } })).status).toBe(401);
        const first = await api("POST", "/api/v1/admin/levels", { headers: admin, body: { levels: [level()], game_version: "2.1.0" } });
        expect(first.status).toBe(200);
        expect(first.body.data.levels[0].result).toBe("current");
        const again = await api("POST", "/api/v1/admin/levels", { headers: admin, body: { levels: [level()] } });
        expect(again.body.data.levels[0].result).toBe("unchanged");
        const conflict = await api("POST", "/api/v1/admin/levels", { headers: admin, body: { levels: [level({ content_hash: "f".repeat(64) })] } });
        expect(conflict.status).toBe(409);
        const v2 = await api("POST", "/api/v1/admin/levels", { headers: admin, body: { levels: [level({ version: 2, content_hash: "f".repeat(64) })] } });
        expect(v2.body.data.levels[0].result).toBe("current");
        const meta = await api("GET", "/api/v1/meta");
        expect(meta.body.data.levels.find((l: any) => l.id === "new_01")).toMatchObject({ version: 2, content_hash: "f".repeat(64) });
        const old = await env.DB.prepare("SELECT compatibility_status FROM level_versions WHERE level_id = 'new_01' AND version = 1").first("compatibility_status");
        expect(old).toBe("legacy");
        const bad = await api("POST", "/api/v1/admin/levels", { headers: admin, body: { levels: [level({ spawn: [1] })] } });
        expect(bad.status).toBe(400);
    });
});
