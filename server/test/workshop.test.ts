import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { api, registerPlayer, seedAll } from "./helpers";
import { LH2, publish } from "./publish";

beforeEach(seedAll);

const V = "/api/v1";

describe("favorites", () => {
    it("one favorite per player, counted on the card, kept when the level goes away", async () => {
        const nova = await registerPlayer("Nova");
        const orion = await registerPlayer("Orion");
        const { id } = await publish(nova, { title: "Forteresse du Néant" });
        expect((await api("POST", `${V}/community/levels/${id}/favorite`)).status).toBe(401);
        const add = await api("POST", `${V}/community/levels/${id}/favorite`, { token: orion.token });
        expect(add.body.data).toMatchObject({ favorite: true, favorites: 1 });
        // Twice: still one.
        expect((await api("POST", `${V}/community/levels/${id}/favorite`, { token: orion.token })).body.data.favorites).toBe(1);
        const card = await api("GET", `${V}/community/levels/${id}`, { token: orion.token });
        expect(card.body.data.level).toMatchObject({ favorites: 1, my_favorite: true });
        const list = await api("GET", `${V}/community/favorites?sort=title`, { token: orion.token });
        expect(list.body.data.favorites).toHaveLength(1);
        expect(list.body.data.favorites[0]).toMatchObject({ level_id: id, title: "Forteresse du Néant", available: true, author_name: "Nova" });
        // The author deletes the level: the favorite stays, marked unavailable.
        expect((await api("DELETE", `${V}/community/levels/${id}`, { token: nova.token })).status).toBe(200);
        const after = await api("GET", `${V}/community/favorites?filter=unavailable`, { token: orion.token });
        expect(after.body.data.favorites[0]).toMatchObject({ level_id: id, title: "Forteresse du Néant", available: false, unavailable_reason: "deleted" });
        expect((await api("DELETE", `${V}/community/levels/${id}/favorite`, { token: orion.token })).body.data.favorite).toBe(false);
        expect((await api("GET", `${V}/community/favorites`, { token: orion.token })).body.data.favorites).toHaveLength(0);
    });
});

describe("comments and reactions", () => {
    it("post, reply, edit, react once (changeable), pin; the creator cannot delete others' comments", async () => {
        const nova = await registerPlayer("Nova");
        const orion = await registerPlayer("Orion");
        const vega = await registerPlayer("Vega");
        const { id } = await publish(nova);
        const post = await api("POST", `${V}/community/levels/${id}/comments`, { token: orion.token, body: { body: "Trop dur\nmais génial" } });
        expect(post.status).toBe(201);
        const c = post.body.data.comment;
        expect(c).toMatchObject({ body: "Trop dur\nmais génial", mine: true, creator: false, pinned: false, reply_count: 0 });
        // The same text twice is refused (spam).
        expect((await api("POST", `${V}/community/levels/${id}/comments`, { token: orion.token, body: { body: "trop  DUR\nmais génial" } })).status).toBe(409);
        // A reply (and a reply to the reply goes under the same parent).
        const r1 = await api("POST", `${V}/community/levels/${id}/comments`, { token: vega.token, body: { body: "Pareil !", parent_id: c.id } });
        expect(r1.status).toBe(201);
        const r2 = await api("POST", `${V}/community/levels/${id}/comments`, { token: nova.token, body: { body: "Merci", parent_id: r1.body.data.comment.id } });
        expect(r2.body.data.comment.parent_id).toBe(c.id);
        expect(r2.body.data.comment.level_author).toBe(true);
        expect(r2.body.data.comment.creator).toBe(true);
        const replies = await api("GET", `${V}/community/comments/${c.id}/replies`);
        expect(replies.body.data.replies.map((x: { body: string }) => x.body)).toEqual(["Pareil !", "Merci"]);
        // Reactions: one per player, it can change.
        const re = await api("PUT", `${V}/community/comments/${c.id}/reaction`, { token: vega.token, body: { kind: "love" } });
        expect(re.body.data).toMatchObject({ my_reaction: "love", reactions: { love: 1, like: 0 } });
        const re2 = await api("PUT", `${V}/community/comments/${c.id}/reaction`, { token: vega.token, body: { kind: "funny" } });
        expect(re2.body.data.reactions).toMatchObject({ love: 0, funny: 1 });
        expect((await api("PUT", `${V}/community/comments/${c.id}/reaction`, { token: vega.token, body: { kind: "boo" } })).status).toBe(400);
        // Edit: author only.
        expect((await api("PATCH", `${V}/community/comments/${c.id}`, { token: vega.token, body: { body: "piraté" } })).status).toBe(403);
        const ed = await api("PATCH", `${V}/community/comments/${c.id}`, { token: orion.token, body: { body: "Très dur, mais génial" } });
        expect(ed.body.data.comment.edited_at).not.toBeNull();
        // The creator pins, but cannot delete a (negative) comment of someone else.
        expect((await api("POST", `${V}/community/comments/${c.id}/pin`, { token: orion.token })).status).toBe(403);
        expect((await api("POST", `${V}/community/comments/${c.id}/pin`, { token: nova.token })).body.data.pinned).toBe(true);
        expect((await api("DELETE", `${V}/community/comments/${c.id}`, { token: nova.token })).status).toBe(403);
        const top = await api("GET", `${V}/community/levels/${id}/comments?sort=popular`);
        expect(top.body.data.comments[0]).toMatchObject({ id: c.id, pinned: true, reply_count: 2, reaction_total: 1 });
        expect((await api("GET", `${V}/community/levels/${id}`)).body.data.level.comments).toBe(1);
        // The author deletes it: the thread stays readable ("deleted" placeholder).
        expect((await api("DELETE", `${V}/community/comments/${c.id}`, { token: orion.token })).status).toBe(200);
        const left = await api("GET", `${V}/community/levels/${id}/comments`);
        expect(left.body.data.comments[0]).toMatchObject({ id: c.id, deleted: true, body: "", author: null });
    });

    it("reports hide a comment until moderation; spam is rate limited", async () => {
        const nova = await registerPlayer("Nova");
        const troll = await registerPlayer("Troll");
        const { id } = await publish(nova);
        const c = (await api("POST", `${V}/community/levels/${id}/comments`, { token: troll.token, body: { body: "spam spam" } })).body.data.comment;
        expect((await api("POST", `${V}/community/comments/${c.id}/report`, { token: troll.token, body: { reason: "spam" } })).status).toBe(403);
        for (const name of ["A1", "B2", "C3"]) {
            const p = await registerPlayer("Rep" + name);
            expect((await api("POST", `${V}/community/comments/${c.id}/report`, { token: p.token, body: { reason: "spam" } })).status).toBe(200);
        }
        expect((await api("GET", `${V}/community/levels/${id}/comments`)).body.data.comments).toHaveLength(0);
        // Its author still sees it, marked hidden.
        const own = await api("GET", `${V}/community/levels/${id}/comments`, { token: troll.token });
        expect(own.body.data.comments[0]).toMatchObject({ id: c.id, hidden: true });
        // Moderation (admin token): restored.
        const admin = { authorization: `Bearer ${env.ADMIN_TOKEN}` };
        const queue = await api("GET", `${V}/admin/comments`, { headers: admin });
        expect(queue.status).toBe(200);
        expect(queue.body.data.comments[0].id).toBe(c.id);
        expect((await api("POST", `${V}/admin/comments/${c.id}/moderate`, { headers: admin, body: { action: "restore" } })).status).toBe(200);
        expect((await api("GET", `${V}/community/levels/${id}/comments`)).body.data.comments).toHaveLength(1);
        // Rate limit: 8 comments per 10 minutes.
        let limited = false;
        for (let i = 0; i < 10 && !limited; ++i)
            limited = (await api("POST", `${V}/community/levels/${id}/comments`, { token: troll.token, body: { body: "message " + i } })).status === 429;
        expect(limited).toBe(true);
    });
});

describe("creators, followers and notifications", () => {
    it("creator status, follow (never oneself), notification of a new level, merged reactions", async () => {
        const nova = await registerPlayer("Nebula");
        const fan = await registerPlayer("Astro");
        let prof = await api("GET", `${V}/players/${nova.id}`, { token: fan.token });
        expect(prof.body.data.creator.is_creator).toBe(false);
        expect((await api("POST", `${V}/players/${fan.id}/follow`, { token: fan.token })).status).toBe(403);
        const f = await api("POST", `${V}/players/${nova.id}/follow`, { token: fan.token });
        expect(f.body.data).toMatchObject({ following: true, followers: 1 });
        // Following is not a friendship.
        expect((await api("GET", `${V}/players/${nova.id}`, { token: fan.token })).body.data.player.friendship).toBeNull();
        const { id } = await publish(nova, { title: "Forteresse du Néant" });
        prof = await api("GET", `${V}/players/${nova.id}`, { token: fan.token });
        expect(prof.body.data.creator).toMatchObject({ is_creator: true, levels_published: 1, followers: 1 });
        expect(prof.body.data.follow).toMatchObject({ following: true });
        const n = await api("GET", `${V}/notifications`, { token: fan.token });
        expect(n.body.data.unread).toBe(1);
        expect(n.body.data.notifications[0]).toMatchObject({ kind: "new_level", level_id: id, read: false });
        expect(n.body.data.notifications[0].text).toBe("Nebula vient de publier un nouveau niveau : Forteresse du Néant");
        // A new version notifies the players who keep it in their favorites.
        await api("POST", `${V}/community/levels/${id}/favorite`, { token: fan.token });
        await publish(nova, { communityId: id, hash: LH2, title: "Forteresse du Néant" });
        const n2 = await api("GET", `${V}/notifications?filter=creators`, { token: fan.token });
        expect(n2.body.data.notifications.map((x: { kind: string }) => x.kind)).toContain("level_update");
        // Reactions to one comment merge into one notification while unread.
        const c = (await api("POST", `${V}/community/levels/${id}/comments`, { token: fan.token, body: { body: "Bravo" } })).body.data.comment;
        for (const name of ["R1", "R2", "R3"]) {
            const p = await registerPlayer("Fan" + name);
            await api("PUT", `${V}/community/comments/${c.id}/reaction`, { token: p.token, body: { kind: "like" } });
        }
        const n3 = await api("GET", `${V}/notifications?filter=comments`, { token: fan.token });
        const reactions = n3.body.data.notifications.filter((x: { kind: string }) => x.kind === "comment_reaction");
        expect(reactions).toHaveLength(1);
        expect(reactions[0].count).toBe(3);
        // Mark read, read all, delete, turn a category off.
        expect((await api("POST", `${V}/notifications/${reactions[0].id}/read`, { token: fan.token })).status).toBe(200);
        expect((await api("POST", `${V}/notifications/read-all`, { token: fan.token })).status).toBe(200);
        expect((await api("GET", `${V}/notifications/unread`, { token: fan.token })).body.data.unread).toBe(0);
        expect((await api("DELETE", `${V}/notifications/${reactions[0].id}`, { token: fan.token })).status).toBe(200);
        const set = await api("PUT", `${V}/notifications/settings`, { token: fan.token, body: { comment_reaction: false } });
        expect(set.body.data.settings.comment_reaction).toBe(false);
        const p4 = await registerPlayer("FanR4");
        await api("PUT", `${V}/community/comments/${c.id}/reaction`, { token: p4.token, body: { kind: "love" } });
        expect((await api("GET", `${V}/notifications/unread`, { token: fan.token })).body.data.unread).toBe(0);
        // Lists of followers / followed creators; creations; search of creators.
        expect((await api("GET", `${V}/players/${nova.id}/followers`)).body.data.players[0].username).toBe("Astro");
        expect((await api("GET", `${V}/players/${fan.id}/following`)).body.data.players[0]).toMatchObject({ username: "Nebula", creator: true });
        expect((await api("GET", `${V}/players/${nova.id}/creations?sort=popular`)).body.data.creations[0].id).toBe(id);
        expect((await api("GET", `${V}/creators?q=neb`)).body.data.creators[0].username).toBe("Nebula");
        // Blocking ends the follow.
        expect((await api("POST", `${V}/social/blocks`, { token: nova.token, body: { player_id: fan.id } })).status).toBeLessThan(300);
        expect((await api("GET", `${V}/players/${nova.id}/followers`)).body.data.players).toHaveLength(0);
        expect((await api("GET", `${V}/players/${fan.id}`, { token: nova.token })).body.data.follow.blocked).toBe(true);
        expect((await api("GET", `${V}/players/${nova.id}`, { token: fan.token })).body.data.follow.blocked).toBe(false);
        expect((await api("POST", `${V}/players/${nova.id}/follow`, { token: fan.token })).status).toBe(403);
    });

    it("follows can be private; unfollow", async () => {
        const nova = await registerPlayer("Nova");
        const fan = await registerPlayer("Fan");
        await api("POST", `${V}/players/${nova.id}/follow`, { token: fan.token, body: { notify: false } });
        // Changing the notification choice is not a new follower.
        expect((await api("POST", `${V}/players/${nova.id}/follow`, { token: fan.token, body: { notify: true } })).body.data).toMatchObject({ following: true, notify: true });
        const notes = await api("GET", `${V}/notifications`, { token: nova.token });
        expect(notes.body.data.notifications.filter((n: { kind: string }) => n.kind === "new_follower")).toHaveLength(1);
        await api("PATCH", `${V}/players/me`, { token: fan.token, body: { follows_visibility: "private" } });
        expect((await api("GET", `${V}/players/${fan.id}/following`)).status).toBe(403);
        expect((await api("GET", `${V}/players/${fan.id}/following`, { token: fan.token })).status).toBe(200);
        expect((await api("DELETE", `${V}/players/${nova.id}/follow`, { token: fan.token })).body.data).toMatchObject({ following: false, followers: 0 });
    });
});

describe("time played", () => {
    it("is added per level and capped by the time elapsed since the last report", async () => {
        const p = await registerPlayer("Nova");
        const first = await api("POST", `${V}/players/me/playtime`, {
            token: p.token,
            body: { sessions: [{ level_id: "test_01", seconds: 3000 }, { level_id: "other", seconds: 600 }] },
        });
        expect(first.status).toBe(200);
        expect(first.body.data.accepted_seconds).toBe(3600);
        // Right after: only the time elapsed since (a few seconds, plus a 5 minute margin).
        const second = await api("POST", `${V}/players/me/playtime`, { token: p.token, body: { sessions: [{ level_id: "test_01", seconds: 6 * 3600 }] } });
        expect(second.body.data.accepted_seconds).toBeLessThanOrEqual(310);
        expect((await api("POST", `${V}/players/me/playtime`, { token: p.token, body: { sessions: [{ level_id: "../x", seconds: 5 }] } })).status).toBe(400);
        const prof = await api("GET", `${V}/players/${p.id}`);
        expect(prof.body.data.playtime.total_seconds).toBeGreaterThanOrEqual(3600);
        expect(prof.body.data.playtime.levels[0]).toMatchObject({ level_id: "test_01", title: "Niveau test" });
        expect(prof.body.data.playtime.levels.map((l: { level_id: string }) => l.level_id)).not.toContain("other");
    });
});
