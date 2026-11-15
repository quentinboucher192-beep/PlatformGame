// Cosmic Online 2.19: ratings of the official levels (only after a verified run, average
// computed by the server), comments of the official levels and of the runs (same rules as
// the community levels' comments), counts on the leaderboard rows, notifications, privacy,
// export and account deletion.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { api, registerPlayer, seedAll, submission, verifiedRun, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";

async function rate(p: TestPlayer, stars: unknown, level = "test_01") {
    return api("PUT", `${V}/levels/${level}/rating`, { token: p.token, body: { stars } });
}

describe("ratings of the official levels", () => {
    it("only after a verified run; the server computes the average; change and withdraw", async () => {
        const nova = await registerPlayer("Nova");
        const orion = await registerPlayer("Orion");
        // Anonymous: the average, nothing personal.
        const anon = await api("GET", `${V}/levels/test_01/rating`);
        expect(anon.status).toBe(200);
        expect(anon.body.data).toMatchObject({ level_id: "test_01", rating: { average: null, count: 0 }, my_stars: null, can_rate: false, reason: "NOT_LOGGED_IN" });
        expect((await api("PUT", `${V}/levels/test_01/rating`, { body: { stars: 4 } })).status).toBe(401);
        // Not finished yet: refused.
        const early = await rate(nova, 4);
        expect(early.status).toBe(403);
        expect(early.body.error.code).toBe("LEVEL_NOT_COMPLETED");
        expect((await api("GET", `${V}/levels/test_01/rating`, { token: nova.token })).body.data).toMatchObject({ can_rate: false, reason: "LEVEL_NOT_COMPLETED" });
        // A pending run (no replay) does not prove anything either.
        await api("POST", `${V}/records/submit`, { token: nova.token, body: submission({ time_ms: 61000 }) });
        expect((await rate(nova, 4)).status).toBe(403);
        // A verified run opens the rating.
        expect((await verifiedRun(nova, 60000)).status).toBe("VERIFIED");
        const r1 = await rate(nova, 4);
        expect(r1.status).toBe(200);
        expect(r1.body.data).toMatchObject({ rating: { average: 4, count: 1 }, my_stars: 4, can_rate: true, reason: null });
        await verifiedRun(orion, 62000);
        expect((await rate(orion, 5)).body.data.rating).toEqual({ average: 4.5, count: 2 });
        // Changed: still one rating per player.
        expect((await rate(orion, 2)).body.data.rating).toEqual({ average: 3, count: 2 });
        // Strict input: integers 1..5 only, no extra field (an average is never accepted).
        for (const bad of [0, 6, 2.5, "5", null]) expect((await rate(orion, bad)).status).toBe(400);
        const extra = await api("PUT", `${V}/levels/test_01/rating`, { token: orion.token, body: { stars: 5, average: 5 } });
        expect(extra.status).toBe(400);
        // Unknown level.
        expect((await api("GET", `${V}/levels/nope_99/rating`)).status).toBe(404);
        expect((await api("PUT", `${V}/levels/NOPE/rating`, { token: orion.token, body: { stars: 3 } })).status).toBe(404);
        // Every level at once (the campaign map), with mine.
        const list = await api("GET", `${V}/levels/ratings`, { token: orion.token });
        expect(list.body.data.levels).toEqual([{ level_id: "test_01", average: 3, count: 2 }]);
        expect(list.body.data.my_ratings).toEqual({ test_01: 2 });
        expect((await api("GET", `${V}/levels/ratings`)).body.data.my_ratings).toBeNull();
        // Withdrawn (twice: idempotent).
        const del = await api("DELETE", `${V}/levels/test_01/rating`, { token: orion.token });
        expect(del.body.data).toMatchObject({ rating: { average: 4, count: 1 }, my_stars: null, can_rate: true });
        expect((await api("DELETE", `${V}/levels/test_01/rating`, { token: orion.token })).status).toBe(200);
        await api("DELETE", `${V}/levels/test_01/rating`, { token: nova.token });
        expect((await api("GET", `${V}/levels/ratings`)).body.data.levels).toEqual([]);
    });

    it("a finished, verified attempt of the EXTRÊME race proves the level too", async () => {
        const vega = await registerPlayer("Vega");
        await env.DB.prepare(
            `INSERT INTO race_runs (id, board, player_id, attempt_id, claim, time_ms, finished, status, created_at)
             VALUES (?1, 'o:test_02:hash', ?2, 'a1', '{}', 90000, 1, 'VERIFIED', '2026-11-01T00:00:00.000Z')`,
        )
            .bind(crypto.randomUUID(), vega.id)
            .run();
        expect((await rate(vega, 5, "test_02")).status).toBe(200);
        // Not another level whose id merely starts the same way.
        expect((await rate(vega, 5, "test_01")).status).toBe(403);
    });

    it("has its own rate limit", async () => {
        const nova = await registerPlayer("Nova");
        await verifiedRun(nova, 60000);
        let last = 0;
        for (let i = 0; i < 61; ++i) last = (await rate(nova, (i % 5) + 1)).status;
        expect(last).toBe(429);
    });
});

describe("comments of the official levels", () => {
    it("same rules as the community levels: replies, reactions, edit, delete by the author, no pin", async () => {
        const nova = await registerPlayer("Nova");
        const orion = await registerPlayer("Orion");
        expect((await api("POST", `${V}/levels/test_01/comments`, { body: { body: "Salut" } })).status).toBe(401);
        const post = await api("POST", `${V}/levels/test_01/comments`, { token: nova.token, body: { body: "Le saut final est superbe" } });
        expect(post.status).toBe(201);
        const c = post.body.data.comment;
        expect(c).toMatchObject({ thread: "level", mine: true, level_author: false, can_pin: false });
        // Anti-spam, length, links: shared rules.
        expect((await api("POST", `${V}/levels/test_01/comments`, { token: nova.token, body: { body: "le saut FINAL est superbe" } })).status).toBe(409);
        expect((await api("POST", `${V}/levels/test_01/comments`, { token: nova.token, body: { body: "x".repeat(1001) } })).status).toBe(400);
        expect((await api("POST", `${V}/levels/test_01/comments`, { token: nova.token, body: { body: "a http://a b http://b c http://c" } })).status).toBe(400);
        expect((await api("POST", `${V}/levels/test_01/comments`, { token: nova.token, body: { body: "ok", player_id: orion.id } })).status).toBe(400);
        // A reply, reactions, edit (through the comment routes shared by every thread).
        const reply = await api("POST", `${V}/levels/test_01/comments`, { token: orion.token, body: { body: "Oui !", parent_id: c.id } });
        expect(reply.status).toBe(201);
        // A parent of another thread is refused.
        expect((await api("POST", `${V}/levels/test_02/comments`, { token: orion.token, body: { body: "Ailleurs", parent_id: c.id } })).status).toBe(400);
        const re = await api("PUT", `${V}/community/comments/${c.id}/reaction`, { token: orion.token, body: { kind: "impressive" } });
        expect(re.body.data).toMatchObject({ my_reaction: "impressive", reactions: { impressive: 1 } });
        const edit = await api("PATCH", `${V}/community/comments/${c.id}`, { token: nova.token, body: { body: "Le saut final est sublime" } });
        expect(edit.body.data.comment).toMatchObject({ body: "Le saut final est sublime", thread: "level" });
        expect((await api("PATCH", `${V}/community/comments/${c.id}`, { token: orion.token, body: { body: "pirate" } })).status).toBe(403);
        expect((await api("POST", `${V}/community/comments/${c.id}/pin`, { token: nova.token })).status).toBe(403);
        const list = await api("GET", `${V}/levels/test_01/comments`);
        expect(list.body.data.comment_count).toBe(1);
        expect(list.body.data.comments[0]).toMatchObject({ id: c.id, reply_count: 1, reactions: { impressive: 1 } });
        const replies = await api("GET", `${V}/community/comments/${c.id}/replies`);
        expect(replies.body.data.replies[0]).toMatchObject({ body: "Oui !", thread: "level" });
        // The level's rating says how many comments it has.
        expect((await api("GET", `${V}/levels/test_01/rating`)).body.data.comments).toBe(1);
        // Only its author deletes it; with a reply it stays as « deleted ».
        expect((await api("DELETE", `${V}/community/comments/${c.id}`, { token: orion.token })).status).toBe(403);
        expect((await api("DELETE", `${V}/community/comments/${c.id}`, { token: nova.token })).status).toBe(200);
        const after = await api("GET", `${V}/levels/test_01/comments`);
        expect(after.body.data.comments[0]).toMatchObject({ deleted: true, body: "", author: null });
        // The comment's author was notified of the reply and of the reaction, with the thread.
        const n = await api("GET", `${V}/notifications?filter=comments`, { token: nova.token });
        const kinds = n.body.data.notifications.map((x: { kind: string }) => x.kind).sort();
        expect(kinds).toEqual(["comment_reaction", "comment_reply"]);
        for (const x of n.body.data.notifications) expect(x).toMatchObject({ level_id: "test_01", data: { thread: "level:test_01", title: "Niveau test" } });
        expect((await api("GET", `${V}/levels/nope_99/comments`)).status).toBe(404);
    });

    it("reports hide a comment until moderation, the rate limit is shared with the community", async () => {
        const nova = await registerPlayer("Nova");
        const c = (await api("POST", `${V}/levels/test_01/comments`, { token: nova.token, body: { body: "Spam spam" } })).body.data.comment;
        expect((await api("POST", `${V}/community/comments/${c.id}/report`, { token: nova.token, body: { reason: "spam" } })).status).toBe(403);
        for (const name of ["Rapporteur1", "Rapporteur2", "Rapporteur3"]) {
            const p = await registerPlayer(name);
            expect((await api("POST", `${V}/community/comments/${c.id}/report`, { token: p.token, body: { reason: "spam" } })).status).toBe(200);
        }
        expect((await api("GET", `${V}/levels/test_01/comments`)).body.data.comments).toHaveLength(0);
        // Its author still sees it, marked hidden.
        expect((await api("GET", `${V}/levels/test_01/comments`, { token: nova.token })).body.data.comments[0].hidden).toBe(true);
        const admin = { authorization: `Bearer ${env.ADMIN_TOKEN}` };
        const queue = await api("GET", `${V}/admin/comments`, { headers: admin });
        expect(queue.body.data.comments[0]).toMatchObject({ id: c.id, thread: "level:test_01", hidden: 1 });
        expect((await api("POST", `${V}/admin/comments/${c.id}/moderate`, { headers: admin, body: { action: "restore" } })).status).toBe(200);
        expect((await api("GET", `${V}/levels/test_01/comments`)).body.data.comments).toHaveLength(1);
        // 8 comments per 10 minutes in every thread (the first one counted).
        let last = 0;
        for (let i = 0; i < 8; ++i) last = (await api("POST", `${V}/levels/test_02/comments`, { token: nova.token, body: { body: "Message " + i } })).status;
        expect(last).toBe(429);
    });
});

describe("comments of the runs", () => {
    it("on a verified run: its author is notified, the leaderboard row counts them, the author may pin", async () => {
        const nova = await registerPlayer("Nova");
        const orion = await registerPlayer("Orion");
        const { recordId } = await verifiedRun(nova, 60000);
        const post = await api("POST", `${V}/records/${recordId}/comments`, { token: orion.token, body: { body: "Quelle course !" } });
        expect(post.status).toBe(201);
        expect(post.body.data.comment).toMatchObject({ thread: "run", level_author: false, can_pin: false });
        const own = await api("POST", `${V}/records/${recordId}/comments`, { token: nova.token, body: { body: "Merci, 3 essais seulement" } });
        expect(own.body.data.comment).toMatchObject({ level_author: true, can_pin: true });
        // The run's author is notified (and not of their own comment).
        const n = await api("GET", `${V}/notifications?filter=comments`, { token: nova.token });
        expect(n.body.data.notifications).toHaveLength(1);
        expect(n.body.data.notifications[0]).toMatchObject({
            kind: "run_comment",
            level_id: "test_01",
            text: "Orion a commenté votre course : Niveau test",
            data: { thread: "run:" + recordId, record_id: recordId },
        });
        // Leaderboard rows: the number of comments of each run.
        const board = await api("GET", `${V}/leaderboards/level/test_01`);
        expect(board.body.data.entries[0]).toMatchObject({ record_id: recordId, comments: 2 });
        const mine = await api("GET", `${V}/leaderboards/level/test_01`, { token: nova.token });
        expect(mine.body.data.me.entry.comments).toBe(2);
        // The run's author pins a comment; nobody else can.
        const cid = post.body.data.comment.id;
        expect((await api("POST", `${V}/community/comments/${cid}/pin`, { token: orion.token })).status).toBe(403);
        expect((await api("POST", `${V}/community/comments/${cid}/pin`, { token: nova.token })).status).toBe(200);
        expect((await api("GET", `${V}/records/${recordId}/comments`)).body.data.comments[0]).toMatchObject({ id: cid, pinned: true });
        // Settings: the new category can be turned off.
        const settings = await api("GET", `${V}/notifications/settings`, { token: nova.token });
        expect(settings.body.data.settings.run_comment).toBe(true);
        await api("PUT", `${V}/notifications/settings`, { token: nova.token, body: { run_comment: false } });
        await api("POST", `${V}/records/${recordId}/comments`, { token: orion.token, body: { body: "Encore un" } });
        expect((await api("GET", `${V}/notifications/unread`, { token: nova.token })).body.data.unread).toBe(1);
    });

    it("closed on private profiles, pending runs and between blocked players", async () => {
        const nova = await registerPlayer("Nova");
        const orion = await registerPlayer("Orion");
        const vega = await registerPlayer("Vega");
        const { recordId } = await verifiedRun(nova, 60000);
        // A pending run: only its author sees it, and nobody comments it yet.
        const pending = await api("POST", `${V}/records/submit`, { token: vega.token, body: submission({ time_ms: 70000 }) });
        const pendingId = pending.body.data.record.id;
        expect((await api("GET", `${V}/records/${pendingId}/comments`, { token: orion.token })).status).toBe(404);
        expect((await api("POST", `${V}/records/${pendingId}/comments`, { token: vega.token, body: { body: "Bientôt" } })).status).toBe(403);
        expect((await api("GET", `${V}/records/not-a-record/comments`)).status).toBe(404);
        // Blocked in either direction.
        await api("POST", `${V}/social/blocks`, { token: nova.token, body: { player_id: vega.id } });
        expect((await api("POST", `${V}/records/${recordId}/comments`, { token: vega.token, body: { body: "Coucou" } })).status).toBe(403);
        // Private profile: closed for others (the row says null), still readable by its author.
        await api("POST", `${V}/records/${recordId}/comments`, { token: orion.token, body: { body: "Bravo" } });
        await api("PATCH", `${V}/players/me`, { token: nova.token, body: { profile_visibility: "private" } });
        const closed = await api("GET", `${V}/records/${recordId}/comments`, { token: orion.token });
        expect(closed.status).toBe(403);
        expect(closed.body.error.code).toBe("PROFILE_PRIVATE");
        expect((await api("POST", `${V}/records/${recordId}/comments`, { token: orion.token, body: { body: "Re" } })).status).toBe(403);
        expect((await api("GET", `${V}/records/${recordId}/comments`, { token: nova.token })).body.data.comments).toHaveLength(1);
        expect((await api("GET", `${V}/leaderboards/level/test_01`)).body.data.entries[0].comments).toBeNull();
        expect((await api("GET", `${V}/leaderboards/level/test_01`, { token: nova.token })).body.data.me.entry.comments).toBe(1);
    });
});

describe("export and account deletion", () => {
    it("exports ratings and comments; deleting the account recomputes the averages and counters", async () => {
        const nova = await registerPlayer("Nova");
        const orion = await registerPlayer("Orion");
        await verifiedRun(nova, 60000);
        const { recordId } = await verifiedRun(orion, 61000);
        await rate(nova, 1);
        await rate(orion, 5);
        const c = (await api("POST", `${V}/levels/test_01/comments`, { token: orion.token, body: { body: "Bel endroit" } })).body.data.comment;
        await api("PUT", `${V}/community/comments/${c.id}/reaction`, { token: nova.token, body: { kind: "love" } });
        await api("POST", `${V}/levels/test_01/comments`, { token: nova.token, body: { body: "Je confirme", parent_id: c.id } });
        await api("POST", `${V}/records/${recordId}/comments`, { token: nova.token, body: { body: "Pas mal" } });
        const exp = await api("GET", `${V}/players/me/export`, { token: nova.token });
        expect(exp.body.data.level_ratings).toMatchObject([{ level_id: "test_01", stars: 1 }]);
        expect(exp.body.data.campaign_comments.map((x: { thread: string }) => x.thread).sort()).toEqual(["level:test_01", "run:" + recordId]);
        expect(exp.body.data.campaign_comment_reactions).toMatchObject([{ comment_id: c.id, kind: "love" }]);
        expect((await api("GET", `${V}/levels/test_01/rating`)).body.data.rating).toEqual({ average: 3, count: 2 });
        expect((await api("DELETE", `${V}/players/me`, { token: nova.token, body: { password: nova.password, confirm: "DELETE" } })).status).toBe(200);
        // Without Nova: the average, the reactions and the replies are recomputed.
        expect((await api("GET", `${V}/levels/test_01/rating`)).body.data.rating).toEqual({ average: 5, count: 1 });
        const list = await api("GET", `${V}/levels/test_01/comments`);
        expect(list.body.data.comments[0]).toMatchObject({ id: c.id, reply_count: 0, reactions: { love: 0 } });
        expect((await api("GET", `${V}/records/${recordId}/comments`)).body.data.comments).toHaveLength(0);
        // Orion deletes their account: their run's thread goes with the run.
        await api("POST", `${V}/records/${recordId}/comments`, { token: orion.token, body: { body: "Seul ici" } });
        await api("DELETE", `${V}/players/me`, { token: orion.token, body: { password: orion.password, confirm: "DELETE" } });
        const left = await env.DB.prepare("SELECT COUNT(*) AS n FROM campaign_comments").first<{ n: number }>();
        expect(left?.n).toBe(0);
        expect((await api("GET", `${V}/levels/test_01/rating`)).body.data.rating).toEqual({ average: null, count: 0 });
    });
});
