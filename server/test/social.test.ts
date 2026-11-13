import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";
import { cleanMessage } from "../src/social";

beforeEach(seedAll);

async function befriend(a: TestPlayer, b: TestPlayer): Promise<void> {
    expect((await api("POST", "/api/v1/friends/request", { token: a.token, body: { player_id: b.id } })).status).toBe(201);
    expect((await api("POST", "/api/v1/friends/accept", { token: b.token, body: { player_id: a.id } })).status).toBe(200);
}

async function direct(a: TestPlayer, b: TestPlayer): Promise<string> {
    const r = await api("POST", "/api/v1/social/direct", { token: a.token, body: { player_id: b.id } });
    expect(r.status).toBe(200);
    return r.body.data.conversation.id;
}

describe("private conversations", () => {
    it("only between friends, created once, with unread counts and read marks", async () => {
        const a = await registerPlayer("Nova");
        const b = await registerPlayer("Orion");
        expect((await api("POST", "/api/v1/social/direct", { body: { player_id: b.id } })).status).toBe(401);
        const notFriends = await api("POST", "/api/v1/social/direct", { token: a.token, body: { player_id: b.id } });
        expect(notFriends.status).toBe(403);
        expect(notFriends.body.error.code).toBe("NOT_FRIENDS");
        await befriend(a, b);
        const id = await direct(a, b);
        expect(await direct(b, a)).toBe(id);

        const sent = await api("POST", `/api/v1/social/conversations/${id}/messages`, { token: a.token, body: { body: "  Salut !\n\n\n\n\nÇa va ?\u0007 " } });
        expect(sent.status).toBe(201);
        expect(sent.body.data.message.body).toBe("Salut !\n\n\nÇa va ?");
        await api("POST", `/api/v1/social/conversations/${id}/messages`, { token: a.token, body: { body: "Course sur Sylvanox ?" } });

        const unread = await api("GET", "/api/v1/social/unread", { token: b.token });
        expect(unread.body.data.total).toBe(2);
        expect(unread.body.data.latest[0]).toMatchObject({ conversation_id: id, sender: "Nova", body: "Course sur Sylvanox ?", kind: "direct" });
        expect((await api("GET", "/api/v1/social/unread", { token: a.token })).body.data.total).toBe(0);

        const list = await api("GET", "/api/v1/social/conversations", { token: b.token });
        expect(list.body.data.unread_total).toBe(2);
        expect(list.body.data.conversations[0]).toMatchObject({ id, kind: "direct", name: "Nova", unread: 2 });
        expect(list.body.data.conversations[0].last_message).toMatchObject({ sender: "Nova", body: "Course sur Sylvanox ?" });

        const msgs = await api("GET", `/api/v1/social/conversations/${id}/messages`, { token: b.token });
        expect(msgs.body.data.messages.map((m: any) => m.body)).toEqual(["Salut !\n\n\nÇa va ?", "Course sur Sylvanox ?"]);
        expect(msgs.body.data.messages[0]).toMatchObject({ mine: false, sender: { username: "Nova" } });
        expect((await api("POST", `/api/v1/social/conversations/${id}/read`, { token: b.token })).status).toBe(200);
        expect((await api("GET", "/api/v1/social/unread", { token: b.token })).body.data.total).toBe(0);

        // Paging with "before".
        const page = await api("GET", `/api/v1/social/conversations/${id}/messages?limit=1`, { token: a.token });
        expect(page.body.data.has_more).toBe(true);
        const older = await api("GET", `/api/v1/social/conversations/${id}/messages?limit=1&before=${page.body.data.messages[0].created_at}`, {
            token: a.token,
        });
        expect(older.body.data.messages[0].body).toBe("Salut !\n\n\nÇa va ?");
    });

    it("hides conversations from outsiders and validates every input", async () => {
        const a = await registerPlayer("Nova");
        const b = await registerPlayer("Orion");
        const c = await registerPlayer("Intrus");
        await befriend(a, b);
        const id = await direct(a, b);
        expect((await api("GET", `/api/v1/social/conversations/${id}/messages`, { token: c.token })).status).toBe(404);
        expect((await api("POST", `/api/v1/social/conversations/${id}/messages`, { token: c.token, body: { body: "coucou" } })).status).toBe(404);
        expect((await api("GET", "/api/v1/social/conversations/' OR 1=1/messages", { token: a.token })).status).toBe(404);
        expect((await api("POST", `/api/v1/social/conversations/${id}/messages`, { token: a.token, body: { body: "" } })).status).toBe(400);
        expect((await api("POST", `/api/v1/social/conversations/${id}/messages`, { token: a.token, body: { body: "x".repeat(501) } })).status).toBe(400);
        expect((await api("POST", `/api/v1/social/conversations/${id}/messages`, { token: a.token, body: { body: 42 } })).status).toBe(400);
        expect((await api("POST", `/api/v1/social/conversations/${id}/messages`, { token: a.token, body: { body: "ok", sender_id: c.id } })).status).toBe(400);
        expect((await api("POST", "/api/v1/social/direct", { token: a.token, body: { player_id: a.id } })).status).toBe(400);
        expect(cleanMessage("a‮b\r\nc\td")).toBe("ab\nc d");
    });

    it("a friendship that ends closes the conversation", async () => {
        const a = await registerPlayer("Nova");
        const b = await registerPlayer("Orion");
        await befriend(a, b);
        const id = await direct(a, b);
        await api("DELETE", `/api/v1/friends/${b.id}`, { token: a.token });
        const r = await api("POST", `/api/v1/social/conversations/${id}/messages`, { token: b.token, body: { body: "?" } });
        expect(r.status).toBe(403);
        // History stays readable.
        expect((await api("GET", `/api/v1/social/conversations/${id}/messages`, { token: b.token })).status).toBe(200);
    });

    it("the sender deletes a message, others can report it once", async () => {
        const a = await registerPlayer("Nova");
        const b = await registerPlayer("Orion");
        await befriend(a, b);
        const id = await direct(a, b);
        const m1 = (await api("POST", `/api/v1/social/conversations/${id}/messages`, { token: a.token, body: { body: "oups" } })).body.data.message.id;
        const m2 = (await api("POST", `/api/v1/social/conversations/${id}/messages`, { token: a.token, body: { body: "insulte" } })).body.data.message.id;
        expect((await api("DELETE", `/api/v1/social/messages/${m1}`, { token: b.token })).status).toBe(404);
        expect((await api("DELETE", `/api/v1/social/messages/${m1}`, { token: a.token })).status).toBe(200);
        const msgs = await api("GET", `/api/v1/social/conversations/${id}/messages`, { token: b.token });
        expect(msgs.body.data.messages[0]).toMatchObject({ deleted: true, body: "" });
        expect((await api("POST", `/api/v1/social/messages/${m2}/report`, { token: a.token, body: {} })).status).toBe(400);
        expect((await api("POST", `/api/v1/social/messages/${m2}/report`, { token: b.token, body: { reason: "insulte" } })).status).toBe(201);
        expect((await api("POST", `/api/v1/social/messages/${m2}/report`, { token: b.token, body: {} })).status).toBe(201);
        const n = await env.DB.prepare("SELECT COUNT(*) AS n FROM message_reports WHERE message_id = ?1").bind(m2).first("n");
        expect(n).toBe(1);
        const c = await registerPlayer("Intrus");
        expect((await api("POST", `/api/v1/social/messages/${m2}/report`, { token: c.token, body: {} })).status).toBe(404);
    });
});

describe("groups", () => {
    it("gathers friends, the owner manages members, ownership passes on", async () => {
        const [a, b, c, d] = [await registerPlayer("Nova"), await registerPlayer("Orion"), await registerPlayer("Lyra"), await registerPlayer("Vega")];
        await befriend(a, b);
        await befriend(a, c);
        // d is not a's friend.
        const bad = await api("POST", "/api/v1/social/groups", { token: a.token, body: { name: "Équipe", member_ids: [b.id, d.id] } });
        expect(bad.status).toBe(403);
        expect((await api("POST", "/api/v1/social/groups", { token: a.token, body: { name: "", member_ids: [b.id] } })).status).toBe(400);
        expect((await api("POST", "/api/v1/social/groups", { token: a.token, body: { name: "X", member_ids: "all" } })).status).toBe(400);
        const g = await api("POST", "/api/v1/social/groups", { token: a.token, body: { name: "  Équipe Sylvanox ", member_ids: [b.id, c.id, b.id] } });
        expect(g.status).toBe(201);
        const id = g.body.data.conversation.id;
        expect(g.body.data.conversation).toMatchObject({ kind: "group", name: "Équipe Sylvanox" });
        expect(g.body.data.conversation.members).toHaveLength(3);

        await api("POST", `/api/v1/social/conversations/${id}/messages`, { token: b.token, body: { body: "Salut le groupe" } });
        expect((await api("GET", "/api/v1/social/unread", { token: a.token })).body.data.total).toBe(1);
        expect((await api("GET", "/api/v1/social/unread", { token: c.token })).body.data.latest[0]).toMatchObject({ kind: "group", name: "Équipe Sylvanox" });

        // Only the owner renames, adds and removes.
        expect((await api("PATCH", `/api/v1/social/groups/${id}`, { token: b.token, body: { name: "Mien" } })).status).toBe(403);
        expect((await api("PATCH", `/api/v1/social/groups/${id}`, { token: a.token, body: { name: "Les Pionniers" } })).status).toBe(200);
        expect((await api("POST", `/api/v1/social/groups/${id}/members`, { token: a.token, body: { player_id: d.id } })).status).toBe(403);
        await befriend(a, d);
        expect((await api("POST", `/api/v1/social/groups/${id}/members`, { token: b.token, body: { player_id: d.id } })).status).toBe(403);
        const added = await api("POST", `/api/v1/social/groups/${id}/members`, { token: a.token, body: { player_id: d.id } });
        expect(added.body.data.conversation.members).toHaveLength(4);
        expect((await api("DELETE", `/api/v1/social/groups/${id}/members/${c.id}`, { token: b.token })).status).toBe(403);
        expect((await api("DELETE", `/api/v1/social/groups/${id}/members/${c.id}`, { token: a.token })).status).toBe(200);
        expect((await api("GET", `/api/v1/social/conversations/${id}/messages`, { token: c.token })).status).toBe(404);

        // The owner leaves: the oldest member (b) takes over.
        expect((await api("DELETE", `/api/v1/social/groups/${id}/members/${a.id}`, { token: a.token })).status).toBe(200);
        const list = await api("GET", "/api/v1/social/conversations", { token: b.token });
        expect(list.body.data.conversations[0]).toMatchObject({ name: "Les Pionniers", role: "owner" });
        await api("DELETE", `/api/v1/social/groups/${id}/members/${d.id}`, { token: d.token });
        const last = await api("DELETE", `/api/v1/social/groups/${id}/members/${b.id}`, { token: b.token });
        expect(last.body.data.group_deleted).toBe(true);
        expect(await env.DB.prepare("SELECT COUNT(*) AS n FROM conversations").first("n")).toBe(0);
    });
});

describe("blocks", () => {
    it("ends the friendship, hides messages and stops new requests", async () => {
        const [a, b, c] = [await registerPlayer("Nova"), await registerPlayer("Orion"), await registerPlayer("Lyra")];
        await befriend(a, b);
        await befriend(a, c);
        await befriend(b, c);
        const g = (await api("POST", "/api/v1/social/groups", { token: a.token, body: { name: "Trio", member_ids: [b.id, c.id] } })).body.data.conversation.id;
        const blocked = await api("POST", "/api/v1/social/blocks", { token: a.token, body: { player_id: b.id } });
        expect(blocked.status).toBe(201);
        const friends = await api("GET", "/api/v1/friends", { token: a.token });
        expect(friends.body.data.friends.map((f: any) => f.player.username)).toEqual(["Lyra"]);
        // b cannot ask again, and a cannot write to b privately.
        expect((await api("POST", "/api/v1/friends/request", { token: b.token, body: { player_id: a.id } })).status).toBe(403);
        expect((await api("POST", "/api/v1/social/direct", { token: a.token, body: { player_id: b.id } })).status).toBe(403);
        // In the group, b's messages are hidden from a only.
        await api("POST", `/api/v1/social/conversations/${g}/messages`, { token: b.token, body: { body: "spam" } });
        expect((await api("GET", "/api/v1/social/unread", { token: a.token })).body.data.total).toBe(0);
        expect((await api("GET", `/api/v1/social/conversations/${g}/messages`, { token: a.token })).body.data.messages).toHaveLength(0);
        expect((await api("GET", `/api/v1/social/conversations/${g}/messages`, { token: c.token })).body.data.messages).toHaveLength(1);
        const list = await api("GET", "/api/v1/social/blocks", { token: a.token });
        expect(list.body.data.blocked[0].player.username).toBe("Orion");
        expect((await api("DELETE", `/api/v1/social/blocks/${b.id}`, { token: a.token })).status).toBe(200);
        expect((await api("DELETE", `/api/v1/social/blocks/${b.id}`, { token: a.token })).status).toBe(404);
        expect((await api("GET", "/api/v1/social/unread", { token: a.token })).body.data.total).toBe(1);
    });

    it("exports the player's messages and deletes them with the account", async () => {
        const a = await registerPlayer("Nova");
        const b = await registerPlayer("Orion");
        await befriend(a, b);
        const id = await direct(a, b);
        await api("POST", `/api/v1/social/conversations/${id}/messages`, { token: a.token, body: { body: "Mes données" } });
        const exp = await api("GET", "/api/v1/players/me/export", { token: a.token });
        expect(exp.body.data.messages_sent[0].body).toBe("Mes données");
        expect(exp.body.data.conversations).toHaveLength(1);
        await api("DELETE", "/api/v1/players/me", { token: a.token, body: { password: a.password, confirm: "DELETE" } });
        expect(await env.DB.prepare("SELECT COUNT(*) AS n FROM messages").first("n")).toBe(0);
        const list = await api("GET", "/api/v1/social/conversations", { token: b.token });
        expect(list.body.data.conversations[0]).toMatchObject({ name: "Joueur supprimé" });
    });
});
