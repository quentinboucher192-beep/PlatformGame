// The chat of a collaborative project (live editor), the counters of the project's tabs, and
// what a removed member is told.

import { beforeEach, describe, expect, it } from "vitest";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const LEVEL = { id: "projet_chat", name: "Chat", planet: "sylvanox", layers: [{ id: "gameplay" }], objects: [] };

async function project(owner: TestPlayer, members: [TestPlayer, string][]) {
    const r = await api("POST", `${V}/projects`, { token: owner.token, body: { name: "Projet bavard", visibility: "request", content: LEVEL } });
    const id = r.body.data.project.id as string;
    for (const [m, role] of members) {
        const k = await api("POST", `${V}/projects/${id}/invites`, { token: owner.token, body: { role, max_uses: 1 } });
        expect((await api("POST", `${V}/projects/join`, { token: m.token, body: { key: k.body.data.key } })).status).toBe(200);
    }
    return id;
}

describe("project chat", () => {
    it("members write and answer, messages are kept, the live room tells who is there", async () => {
        const nova = await registerPlayer("Nova");
        const orion = await registerPlayer("Orion");
        const stranger = await registerPlayer("Inconnu");
        const p = await project(nova, [[orion, "viewer"]]);
        // Someone in the live editor: the room gives the latest message id.
        const joined = await api("POST", `${V}/projects/${p}/live/join`, { token: orion.token, body: {} });
        expect(joined.body.data.chat_seq).toBe(0);
        const m1 = await api("POST", `${V}/projects/${p}/messages`, { token: nova.token, body: { body: "On attaque la salle 2 ?" } });
        expect(m1.status).toBe(201);
        const id1 = m1.body.data.message.id as number;
        // Even a reader may chat; an answer carries what it answers.
        const m2 = await api("POST", `${V}/projects/${p}/messages`, { token: orion.token, body: { body: "Oui, je regarde le pont.", reply_to: id1 } });
        expect(m2.status).toBe(201);
        expect(m2.body.data.message.reply_to).toMatchObject({ id: id1, username: "Nova", excerpt: "On attaque la salle 2 ?" });
        const hb = await api("POST", `${V}/projects/${p}/live/heartbeat`, { token: orion.token, body: { since_seq: 0 } });
        expect(hb.body.data.chat_seq).toBe(m2.body.data.message.id);
        // Kept: read back, oldest first; only new ones after an id.
        const all = await api("GET", `${V}/projects/${p}/messages`, { token: orion.token });
        expect(all.body.data.messages.map((m: { body: string }) => m.body)).toEqual(["On attaque la salle 2 ?", "Oui, je regarde le pont."]);
        const since = await api("GET", `${V}/projects/${p}/messages?after=${id1}`, { token: nova.token });
        expect(since.body.data.messages).toHaveLength(1);
        // Nova is told about the answer.
        const notes = await api("GET", `${V}/notifications`, { token: nova.token });
        expect(notes.body.data.notifications.some((n: { kind: string }) => n.kind === "project_reply")).toBe(true);
        // Outsiders (the project is listed: they know it exists) neither read nor write; bad input is refused.
        expect((await api("GET", `${V}/projects/${p}/messages`, { token: stranger.token })).status).toBe(403);
        expect((await api("POST", `${V}/projects/${p}/messages`, { token: stranger.token, body: { body: "coucou" } })).status).toBe(403);
        expect((await api("POST", `${V}/projects/${p}/messages`, { token: nova.token, body: { body: "" } })).status).toBe(400);
        expect((await api("POST", `${V}/projects/${p}/messages`, { token: nova.token, body: { body: "x".repeat(501) } })).status).toBe(400);
        expect((await api("POST", `${V}/projects/${p}/messages`, { token: nova.token, body: { body: "ok", reply_to: 999999 } })).status).toBe(404);
        // Older messages, page by page.
        for (let i = 0; i < 5; ++i) await api("POST", `${V}/projects/${p}/messages`, { token: nova.token, body: { body: `message ${i}` } });
        const last3 = await api("GET", `${V}/projects/${p}/messages?limit=3`, { token: nova.token });
        expect(last3.body.data.messages.map((m: { body: string }) => m.body)).toEqual(["message 2", "message 3", "message 4"]);
        expect(last3.body.data.more).toBe(true);
        const older = await api("GET", `${V}/projects/${p}/messages?limit=3&before=${last3.body.data.messages[0].id}`, { token: nova.token });
        expect(older.body.data.messages.map((m: { body: string }) => m.body)).toEqual(["Oui, je regarde le pont.", "message 0", "message 1"]);
        // In the player's data export.
        const exp = await api("GET", `${V}/players/me/export`, { token: orion.token });
        expect(exp.body.data.project_messages).toHaveLength(1);
    });

    it("tab counters and a change stamp; a removed member is told", async () => {
        const nova = await registerPlayer("Nova");
        const orion = await registerPlayer("Orion");
        const kael = await registerPlayer("Kael");
        const p = await project(nova, [[orion, "editor"]]);
        const d1 = await api("GET", `${V}/projects/${p}`, { token: nova.token });
        expect(d1.body.data.project.counts).toMatchObject({ members: 2, requests: 0, invites: 0 });
        // A request and a key: the counters and the stamp move.
        await api("POST", `${V}/projects/${p}/join`, { token: kael.token, body: { message: "Je peux aider ?" } });
        await api("POST", `${V}/projects/${p}/invites`, { token: nova.token, body: { role: "tester", max_uses: 2 } });
        const d2 = await api("GET", `${V}/projects/${p}`, { token: nova.token });
        expect(d2.body.data.project.counts).toMatchObject({ requests: 1, invites: 1 });
        expect(d2.body.data.project.stamp).not.toBe(d1.body.data.project.stamp);
        // An editor does not see the managers' counters; the list shows waiting requests to managers.
        const asEditor = await api("GET", `${V}/projects/${p}`, { token: orion.token });
        expect(asEditor.body.data.project.counts.requests).toBeUndefined();
        const mine = await api("GET", `${V}/projects`, { token: nova.token });
        expect(mine.body.data.projects[0].pending_requests).toBe(1);
        // A role change moves the member's stamp too (the game refreshes on both sides).
        const before = (await api("GET", `${V}/projects/${p}`, { token: orion.token })).body.data.project.stamp;
        await api("PATCH", `${V}/projects/${p}/members/${orion.id}`, { token: nova.token, body: { role: "viewer" } });
        const after = await api("GET", `${V}/projects/${p}`, { token: orion.token });
        expect(after.body.data.project.my_role).toBe("viewer");
        expect(after.body.data.project.stamp).not.toBe(before);
        // Removed: told by a notification, and the project is gone for them (live editor included).
        await api("DELETE", `${V}/projects/${p}/members/${orion.id}`, { token: nova.token });
        const notes = await api("GET", `${V}/notifications`, { token: orion.token });
        expect(notes.body.data.notifications.some((n: { kind: string; text?: string }) => n.kind === "project_removed")).toBe(true);
        expect((await api("GET", `${V}/projects/${p}`, { token: orion.token })).body.data.project.my_role).toBeNull();
        expect((await api("POST", `${V}/projects/${p}/live/heartbeat`, { token: orion.token, body: { since_seq: 0 } })).status).toBe(404);
    });
});
