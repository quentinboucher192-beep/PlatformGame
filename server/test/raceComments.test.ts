// 2.85: the comments of the attempts of a « course à la vérification » of an official level, as a
// run's (threads 'race:<attempt>' for a solo board, 'coop:<attempt>' for a Duo / Trio one): the
// same engine (replies, reactions, pins, reports), the same rate limit, the same privacy; the
// attempt's player — every player of a team — is notified; the boards count them on each row.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const LAYOUT = { route: [[0, 0], [100, 0]], bosses: [], ambushes: [] };

/** The official level made EXTRÊME (a race layout), its board key. */
async function officialRace(): Promise<{ levelId: string; board: string }> {
    const lv = await env.DB.prepare("SELECT level_id, content_hash FROM level_versions WHERE compatibility_status = 'active' LIMIT 1").first<{
        level_id: string;
        content_hash: string;
    }>();
    await env.DB.prepare("UPDATE level_versions SET race = ?2 WHERE content_hash = ?1").bind(lv!.content_hash, JSON.stringify(LAYOUT)).run();
    return { levelId: lv!.level_id, board: `o:${lv!.level_id}:${lv!.content_hash}` };
}

async function soloAttempt(p: TestPlayer, board: string, status = "VERIFIED"): Promise<string> {
    const id = crypto.randomUUID();
    const now = new Date().toISOString();
    await env.DB.prepare(
        `INSERT INTO race_runs (id, board, player_id, attempt_id, claim, time_ms, score, route, bosses_beaten, waves, finished, status, created_at, verified_at)
         VALUES (?1, ?2, ?3, ?4, '{}', 40000, 30.6, 0.32, 0, 0, 0, ?5, ?6, ?6)`,
    )
        .bind(id, board, p.id, crypto.randomUUID(), status, now)
        .run();
    return id;
}

async function teamAttempt(players: TestPlayer[], board: string, confirmed: TestPlayer[] = players): Promise<string> {
    const id = crypto.randomUUID();
    const attempt = crypto.randomUUID();
    const now = new Date().toISOString();
    await env.DB.prepare(
        `INSERT INTO coop_runs (id, board, size, team_key, attempt_id, submitter_id, mode, claim, time_ms, status, score, route, revives, incomplete, finished, created_at, verified_at)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, 'online', '{}', 50000, 'VERIFIED', 41.5, 0.4, 1, 0, 0, ?7, ?7)`,
    )
        .bind(id, board, players.length, players.map((p) => p.id).sort().join("+"), attempt, players[0]!.id, now)
        .run();
    for (const [seat, p] of players.entries())
        await env.DB.prepare("INSERT INTO coop_run_members (run_id, seat, player_id) VALUES (?1, ?2, ?3)").bind(id, seat, p.id).run();
    for (const p of confirmed)
        await env.DB.prepare("INSERT INTO coop_confirms (attempt_id, player_id, created_at) VALUES (?1, ?2, ?3)").bind(attempt, p.id, now).run();
    return id;
}

describe("commentaires des tentatives de la course à la vérification", () => {
    it("a solo attempt: its player is notified and may pin, the board counts them", async () => {
        const { levelId, board } = await officialRace();
        const nova = await registerPlayer("Nova");
        const orion = await registerPlayer("Orion");
        const run = await soloAttempt(nova, board);
        const post = await api("POST", `${V}/races/runs/${run}/comments`, { token: orion.token, body: { body: "Belle progression !" } });
        expect(post.status).toBe(201);
        expect(post.body.data.comment).toMatchObject({ thread: "race", level_author: false, can_pin: false });
        const own = await api("POST", `${V}/races/runs/${run}/comments`, { token: nova.token, body: { body: "Merci" } });
        expect(own.body.data.comment).toMatchObject({ level_author: true, can_pin: true });
        const n = await api("GET", `${V}/notifications?filter=comments`, { token: nova.token });
        expect(n.body.data.notifications).toHaveLength(1);
        expect(n.body.data.notifications[0]).toMatchObject({ kind: "run_comment", level_id: levelId, data: { thread: "race:" + run, run_id: run } });
        // The thread says what attempt it is about (as a row of its board).
        const list = await api("GET", `${V}/races/runs/${run}/comments`);
        expect(list.status).toBe(200);
        expect(list.body.data.comment_count).toBe(2);
        expect(list.body.data.attempt).toMatchObject({ size: 1, run_id: run, level_id: levelId, player: { id: nova.id, username: "Nova" }, score: 30.6 });
        const rk = await api("GET", `${V}/races/board?kind=official&level_id=${levelId}`);
        expect(rk.body.data.ranking[0]).toMatchObject({ run_id: run, comment_count: 2 });
        // Pins: the attempt's player only; replies, reactions through the comments' routes.
        const cid = post.body.data.comment.id;
        expect((await api("POST", `${V}/community/comments/${cid}/pin`, { token: orion.token })).status).toBe(403);
        expect((await api("POST", `${V}/community/comments/${cid}/pin`, { token: nova.token })).status).toBe(200);
        const reply = await api("POST", `${V}/races/runs/${run}/comments`, { token: nova.token, body: { body: "Oui !", parent_id: cid } });
        expect(reply.status).toBe(201);
        expect((await api("PUT", `${V}/community/comments/${cid}/reaction`, { token: nova.token, body: { kind: "love" } })).status).toBeLessThan(300);
        const after = await api("GET", `${V}/races/runs/${run}/comments`);
        expect(after.body.data.comments[0]).toMatchObject({ id: cid, pinned: true, reply_count: 1 });
        // A reply to another thread is refused.
        const other = await soloAttempt(orion, board);
        expect((await api("POST", `${V}/races/runs/${other}/comments`, { token: nova.token, body: { body: "Ici ?", parent_id: cid } })).status).toBe(400);
    });

    it("closed on pending attempts, private profiles, blocked players, unknown ids and community races", async () => {
        const { board } = await officialRace();
        const nova = await registerPlayer("Nova");
        const orion = await registerPlayer("Orion");
        const vega = await registerPlayer("Vega");
        const pending = await soloAttempt(nova, board, "PENDING");
        expect((await api("GET", `${V}/races/runs/${pending}/comments`, { token: orion.token })).status).toBe(404);
        expect((await api("POST", `${V}/races/runs/${pending}/comments`, { token: nova.token, body: { body: "Bientôt" } })).status).toBe(403);
        expect((await api("GET", `${V}/races/runs/not-an-attempt/comments`)).status).toBe(404);
        expect((await api("POST", `${V}/races/runs/${crypto.randomUUID()}/comments`, { token: orion.token, body: { body: "?" } })).status).toBe(404);
        expect((await api("POST", `${V}/races/runs/x/comments`, { body: { body: "Sans compte" } })).status).toBe(401);
        const run = await soloAttempt(nova, board);
        await api("POST", `${V}/social/blocks`, { token: nova.token, body: { player_id: vega.id } });
        expect((await api("POST", `${V}/races/runs/${run}/comments`, { token: vega.token, body: { body: "Coucou" } })).status).toBe(403);
        await api("POST", `${V}/races/runs/${run}/comments`, { token: orion.token, body: { body: "Bravo" } });
        await api("PATCH", `${V}/players/me`, { token: nova.token, body: { profile_visibility: "private" } });
        expect((await api("GET", `${V}/races/runs/${run}/comments`, { token: orion.token })).status).toBe(403);
        expect((await api("GET", `${V}/races/runs/${run}/comments`, { token: nova.token })).body.data.comments).toHaveLength(1);
        // The board: no count for a private player's attempt (as a record's), but for themselves.
        const levelId = board.split(":")[1];
        expect((await api("GET", `${V}/races/board?kind=official&level_id=${levelId}`)).body.data.ranking[0].comment_count).toBeNull();
        expect((await api("GET", `${V}/races/board?kind=official&level_id=${levelId}`, { token: nova.token })).body.data.ranking[0].comment_count).toBe(1);
        // A community level's race: no comments (its rows say null).
        const community = crypto.randomUUID();
        await env.DB.prepare(
            `INSERT INTO race_runs (id, board, player_id, attempt_id, claim, time_ms, score, route, bosses_beaten, waves, finished, status, created_at, verified_at)
             VALUES (?1, 'c:' || ?2 || ':1', ?3, ?4, '{}', 1000, 1, 0, 0, 0, 0, 'VERIFIED', '2026-01-01', '2026-01-01')`,
        )
            .bind(community, crypto.randomUUID(), orion.id, crypto.randomUUID())
            .run();
        const closed = await api("POST", `${V}/races/runs/${community}/comments`, { token: nova.token, body: { body: "Ici ?" } });
        expect(closed.status).toBe(403);
        expect(closed.body.error.code).toBe("RACE_COMMENTS_CLOSED");
    });

    it("shares the comments' rate limit with the runs and the levels", async () => {
        const { board } = await officialRace();
        const nova = await registerPlayer("Nova");
        const orion = await registerPlayer("Orion");
        const run = await soloAttempt(nova, board);
        const statuses: number[] = [];
        for (let i = 0; i < 9; ++i) statuses.push((await api("POST", `${V}/races/runs/${run}/comments`, { token: orion.token, body: { body: `Message ${i}` } })).status);
        expect(statuses.slice(0, 8).every((s) => s === 201)).toBe(true);
        expect(statuses[8]).toBe(429);
    });

    it("a team's attempt: every player is notified and may pin; ranked attempts only for others", async () => {
        const { levelId, board } = await officialRace();
        const ana = await registerPlayer("Ana");
        const bob = await registerPlayer("Bob");
        const cyd = await registerPlayer("Cyd");
        const run = await teamAttempt([ana, bob], board);
        const post = await api("POST", `${V}/coop/runs/${run}/comments`, { token: cyd.token, body: { body: "Quelle équipe !" } });
        expect(post.status).toBe(201);
        expect(post.body.data.comment).toMatchObject({ thread: "coop", level_author: false, can_pin: false });
        for (const p of [ana, bob]) {
            const n = await api("GET", `${V}/notifications?filter=comments`, { token: p.token });
            expect(n.body.data.notifications).toHaveLength(1);
            expect(n.body.data.notifications[0]).toMatchObject({ kind: "run_comment", level_id: levelId, data: { thread: "coop:" + run, run_id: run, size: 2 } });
        }
        const mine = await api("POST", `${V}/coop/runs/${run}/comments`, { token: bob.token, body: { body: "Merci" } });
        expect(mine.body.data.comment).toMatchObject({ level_author: true, can_pin: true });
        expect((await api("POST", `${V}/community/comments/${post.body.data.comment.id}/pin`, { token: cyd.token })).status).toBe(403);
        expect((await api("POST", `${V}/community/comments/${post.body.data.comment.id}/pin`, { token: ana.token })).status).toBe(200);
        const list = await api("GET", `${V}/coop/runs/${run}/comments`);
        expect(list.body.data.attempt).toMatchObject({ size: 2, run_id: run, players: [{ username: "Ana" }, { username: "Bob" }], revives: 1 });
        const rk = await api("GET", `${V}/coop/board?size=2&kind=official&level_id=${levelId}`);
        expect(rk.body.data.ranking[0]).toMatchObject({ run_id: run, comment_count: 2 });
        // Not yet confirmed by all its players: hidden from the others, closed to its players.
        const unconfirmed = await teamAttempt([ana, cyd], board, [ana]);
        expect((await api("GET", `${V}/coop/runs/${unconfirmed}/comments`, { token: bob.token })).status).toBe(404);
        expect((await api("GET", `${V}/coop/runs/${unconfirmed}/comments`, { token: cyd.token })).status).toBe(200);
        expect((await api("POST", `${V}/coop/runs/${unconfirmed}/comments`, { token: cyd.token, body: { body: "Confirme !" } })).status).toBe(403);
        // Blocked by one of its players: no comment.
        await api("POST", `${V}/social/blocks`, { token: bob.token, body: { player_id: cyd.id } });
        expect((await api("POST", `${V}/coop/runs/${run}/comments`, { token: cyd.token, body: { body: "Encore" } })).status).toBe(403);
    });

    it("a player's account deleted: the threads of their solo attempts go with them", async () => {
        const { board } = await officialRace();
        const nova = await registerPlayer("Nova");
        const orion = await registerPlayer("Orion");
        const run = await soloAttempt(nova, board);
        await api("POST", `${V}/races/runs/${run}/comments`, { token: orion.token, body: { body: "Bravo" } });
        const exp = await api("GET", `${V}/players/me/export`, { token: orion.token });
        expect(exp.body.data.campaign_comments.map((x: { thread: string }) => x.thread)).toEqual(["race:" + run]);
        expect((await api("DELETE", `${V}/players/me`, { token: nova.token, body: { password: nova.password, confirm: "DELETE" } })).status).toBe(200);
        const left = await env.DB.prepare("SELECT COUNT(*) AS n FROM campaign_comments").first<{ n: number }>();
        expect(left?.n).toBe(0);
    });
});
