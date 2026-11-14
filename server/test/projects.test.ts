import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const LEVEL = { id: "projet_test", name: "Mon projet", planet: "sylvanox", tiles: [[0, 1], [1, 1]], entities: [] };

async function create(owner: TestPlayer, extra: Record<string, unknown> = {}) {
    const r = await api("POST", `${V}/projects`, { token: owner.token, body: { name: "La Forteresse", content: LEVEL, ...extra } });
    expect(r.status).toBe(201);
    return r.body.data.project as { id: string; revision: number };
}

async function keyFor(owner: TestPlayer, projectId: string, body: Record<string, unknown>) {
    const r = await api("POST", `${V}/projects/${projectId}/invites`, { token: owner.token, body });
    expect(r.status).toBe(201);
    return r.body.data as { key: string; link: string; invite: { id: string } };
}

describe("collaborative projects", () => {
    it("are private to their members; content is versioned, never overwritten", async () => {
        const nova = await registerPlayer("Nova");
        const orion = await registerPlayer("Orion");
        const p = await create(nova);
        expect(p.revision).toBe(1);
        // A stranger does not even learn that the project exists.
        expect((await api("GET", `${V}/projects/${p.id}`, { token: orion.token })).status).toBe(404);
        expect((await api("GET", `${V}/projects/${p.id}/content`, { token: orion.token })).status).toBe(404);
        const content = await api("GET", `${V}/projects/${p.id}/content`, { token: nova.token });
        expect(content.status).toBe(200);
        expect(content.headers.get("x-project-revision")).toBe("1");
        expect(content.body).toEqual(LEVEL);
        // Two editors save from the same revision: the second is refused, nothing is lost.
        const { key } = await keyFor(nova, p.id, { role: "editor" });
        expect((await api("POST", `${V}/projects/join`, { token: orion.token, body: { key } })).status).toBe(200);
        const a = await api("PUT", `${V}/projects/${p.id}/content`, { token: nova.token, body: { base_revision: 1, content: { ...LEVEL, name: "Version de Nova" } } });
        expect(a.status).toBe(201);
        expect(a.body.data.saved.revision).toBe(2);
        const b = await api("PUT", `${V}/projects/${p.id}/content`, { token: orion.token, body: { base_revision: 1, content: { ...LEVEL, name: "Version d'Orion" } } });
        expect(b.status).toBe(409);
        expect(b.body.error.code).toBe("REVISION_CONFLICT");
        expect(b.body.error.details).toMatchObject({ current_revision: 2, saved_by: "Nova" });
        expect((await api("GET", `${V}/projects/${p.id}/content`, { token: orion.token })).body.name).toBe("Version de Nova");
        // Older revisions stay readable.
        expect((await api("GET", `${V}/projects/${p.id}/content?revision=1`, { token: orion.token })).body.name).toBe("Mon projet");
        const revs = await api("GET", `${V}/projects/${p.id}/revisions`, { token: orion.token });
        expect(revs.body.data.revisions.map((r: { revision: number }) => r.revision)).toEqual([2, 1]);
        // Rebased on revision 2, Orion's save goes through.
        expect((await api("PUT", `${V}/projects/${p.id}/content`, { token: orion.token, body: { base_revision: 2, content: LEVEL, message: "fusion" } })).status).toBe(201);
        expect((await api("PUT", `${V}/projects/${p.id}/content`, { token: orion.token, body: { base_revision: 3, content: [1, 2] } })).status).toBe(400);
    });

    it("invitation keys: hashed, limited, expiring, revocable; links accepted", async () => {
        const nova = await registerPlayer("Nova");
        const p = await create(nova);
        const inv = await keyFor(nova, p.id, { role: "tester", max_uses: 2, expires_hours: 24 });
        expect(inv.key).toMatch(/^CPJ-[A-Z2-7]{26}$/);
        expect(inv.link).toBe("cosmic://rejoindre/" + inv.key);
        // Only the SHA-256 of the key is stored.
        const rows = await env.DB.prepare("SELECT key_hash, key_hint FROM project_invites WHERE project_id = ?1").bind(p.id).all<{ key_hash: string; key_hint: string }>();
        expect(rows.results[0]!.key_hash).toMatch(/^[0-9a-f]{64}$/);
        expect(JSON.stringify(rows.results)).not.toContain(inv.key);
        const a = await registerPlayer("Astro");
        const b = await registerPlayer("Bolide");
        const c = await registerPlayer("Comete");
        expect((await api("POST", `${V}/projects/join`, { token: a.token, body: { key: inv.link } })).body.data.project.my_role).toBe("tester");
        expect((await api("POST", `${V}/projects/join`, { token: b.token, body: { key: " " + inv.link.toUpperCase() + "/ " } })).status).toBe(200);
        const used = await api("POST", `${V}/projects/join`, { token: c.token, body: { key: inv.key } });
        expect(used.status).toBe(409);
        expect(used.body.error.code).toBe("INVITE_INVALID");
        // Unknown and malformed keys get the same answer.
        expect((await api("POST", `${V}/projects/join`, { token: c.token, body: { key: "CPJ-AAAAAAAAAAAAAAAAAAAAAAAAAA" } })).body.error.code).toBe("INVITE_INVALID");
        expect((await api("POST", `${V}/projects/join`, { token: c.token, body: { key: "n'importe quoi" } })).body.error.code).toBe("INVITE_INVALID");
        // Revoked, expired.
        const r = await keyFor(nova, p.id, { role: "viewer" });
        expect((await api("DELETE", `${V}/projects/${p.id}/invites/${r.invite.id}`, { token: nova.token })).status).toBe(200);
        expect((await api("POST", `${V}/projects/join`, { token: c.token, body: { key: r.key } })).status).toBe(409);
        const e = await keyFor(nova, p.id, { role: "viewer" });
        await env.DB.prepare("UPDATE project_invites SET expires_at = '2000-01-01T00:00:00.000Z' WHERE id = ?1").bind(e.invite.id).run();
        expect((await api("POST", `${V}/projects/join`, { token: c.token, body: { key: e.key } })).status).toBe(409);
        const list = await api("GET", `${V}/projects/${p.id}/invites`, { token: nova.token });
        expect(list.body.data.invites.map((i: { status: string }) => i.status).sort()).toEqual(["expired", "revoked", "used"]);
        expect(JSON.stringify(list.body.data)).not.toContain(inv.key);
        // A tester cannot see or create invitations.
        expect((await api("GET", `${V}/projects/${p.id}/invites`, { token: a.token })).status).toBe(403);
        expect((await api("POST", `${V}/projects/${p.id}/invites`, { token: a.token, body: { role: "viewer" } })).status).toBe(403);
    });

    it("roles are checked by the server for every action", async () => {
        const owner = await registerPlayer("Nova");
        const p = await create(owner);
        const join = async (name: string, role: string) => {
            const pl = await registerPlayer(name);
            const { key } = await keyFor(owner, p.id, { role });
            expect((await api("POST", `${V}/projects/join`, { token: pl.token, body: { key } })).status).toBe(200);
            return pl;
        };
        const admin = await join("Admiral", "admin");
        const editor = await join("Editeur", "editor");
        const tester = await join("Testeur", "tester");
        const viewer = await join("Lecteur", "viewer");
        const save = (pl: TestPlayer, base: number) => api("PUT", `${V}/projects/${p.id}/content`, { token: pl.token, body: { base_revision: base, content: LEVEL } });
        expect((await save(viewer, 1)).status).toBe(403);
        expect((await save(tester, 1)).status).toBe(403);
        expect((await save(editor, 1)).status).toBe(201);
        // Everyone reads.
        expect((await api("GET", `${V}/projects/${p.id}/content`, { token: viewer.token })).status).toBe(200);
        const members = await api("GET", `${V}/projects/${p.id}/members`, { token: viewer.token });
        expect(members.body.data.members.map((m: { role: string }) => m.role)).toEqual(["owner", "admin", "editor", "tester", "viewer"]);
        // Settings: admins only.
        expect((await api("PATCH", `${V}/projects/${p.id}`, { token: editor.token, body: { name: "Piraté" } })).status).toBe(403);
        expect((await api("PATCH", `${V}/projects/${p.id}`, { token: admin.token, body: { visibility: "request" } })).body.data.project.visibility).toBe("request");
        // An admin manages editors, testers, viewers, never admins or the owner.
        expect((await api("PATCH", `${V}/projects/${p.id}/members/${viewer.id}`, { token: admin.token, body: { role: "editor" } })).status).toBe(200);
        expect((await api("PATCH", `${V}/projects/${p.id}/members/${viewer.id}`, { token: admin.token, body: { role: "admin" } })).status).toBe(403);
        expect((await api("DELETE", `${V}/projects/${p.id}/members/${owner.id}`, { token: admin.token })).status).toBe(403);
        expect((await api("POST", `${V}/projects/${p.id}/invites`, { token: admin.token, body: { role: "admin" } })).status).toBe(403);
        expect((await api("DELETE", `${V}/projects/${p.id}/members/${tester.id}`, { token: admin.token })).status).toBe(200);
        // An editor cannot manage members; anyone can leave; the owner must transfer first.
        expect((await api("DELETE", `${V}/projects/${p.id}/members/${viewer.id}`, { token: editor.token })).status).toBe(403);
        expect((await api("DELETE", `${V}/projects/${p.id}/members/${editor.id}`, { token: editor.token })).status).toBe(200);
        expect((await api("DELETE", `${V}/projects/${p.id}/members/${owner.id}`, { token: owner.token })).body.error.code).toBe("OWNER_CANNOT_LEAVE");
        expect((await api("DELETE", `${V}/projects/${p.id}`, { token: admin.token })).status).toBe(403);
        // Transfer: the old owner becomes admin; the role change is notified.
        expect((await api("POST", `${V}/projects/${p.id}/transfer`, { token: owner.token, body: { player_id: admin.id } })).status).toBe(200);
        const after = await api("GET", `${V}/projects/${p.id}`, { token: owner.token });
        expect(after.body.data.project.my_role).toBe("admin");
        const n = await api("GET", `${V}/notifications?filter=projects`, { token: admin.token });
        expect(n.body.data.notifications[0].text).toBe("Votre rôle dans La Forteresse est maintenant : propriétaire");
        const act = await api("GET", `${V}/projects/${p.id}/activity`, { token: viewer.token });
        expect(act.body.data.activity.map((a: { action: string }) => a.action)).toContain("transferred");
    });

    it("join requests, open projects, direct invitations, blocks", async () => {
        const owner = await registerPlayer("Nova");
        const asker = await registerPlayer("Orion");
        const p = await create(owner, { visibility: "request", join_role: "tester" });
        // Listed for others (without its content).
        const disc = await api("GET", `${V}/projects/discover?q=forter`, { token: asker.token });
        expect(disc.body.data.projects[0]).toMatchObject({ id: p.id, my_role: null, requested: false });
        expect((await api("GET", `${V}/projects/${p.id}/content`, { token: asker.token })).status).toBe(403);
        expect((await api("POST", `${V}/projects/${p.id}/join`, { token: asker.token, body: { message: "Je veux aider" } })).status).toBe(202);
        const reqs = await api("GET", `${V}/projects/${p.id}/requests`, { token: owner.token });
        expect(reqs.body.data.requests[0]).toMatchObject({ message: "Je veux aider" });
        expect((await api("POST", `${V}/projects/${p.id}/requests/${asker.id}`, { token: owner.token, body: { accept: true } })).body.data.role).toBe("tester");
        expect((await api("GET", `${V}/projects/${p.id}`, { token: asker.token })).body.data.project.my_role).toBe("tester");
        // Open: joined with the join role.
        expect((await api("PATCH", `${V}/projects/${p.id}`, { token: owner.token, body: { visibility: "open", join_role: "viewer" } })).status).toBe(200);
        const walker = await registerPlayer("Passant");
        expect((await api("POST", `${V}/projects/${p.id}/join`, { token: walker.token })).body.data).toMatchObject({ joined: true, role: "viewer" });
        // Direct invitation, notified, accepted from the account.
        const guest = await registerPlayer("Invite");
        const inv = await api("POST", `${V}/projects/${p.id}/invites`, { token: owner.token, body: { role: "editor", player_id: guest.id } });
        expect(inv.status).toBe(201);
        expect(inv.body.data.key).toBeUndefined();
        const mine = await api("GET", `${V}/projects/invitations`, { token: guest.token });
        expect(mine.body.data.invitations[0]).toMatchObject({ role: "editor", project: { name: "La Forteresse" } });
        const notes = await api("GET", `${V}/notifications`, { token: guest.token });
        expect(notes.body.data.notifications[0].kind).toBe("project_invite");
        expect((await api("POST", `${V}/projects/invitations/${inv.body.data.invite.id}/accept`, { token: walker.token })).status).toBe(404);
        expect((await api("POST", `${V}/projects/invitations/${inv.body.data.invite.id}/accept`, { token: guest.token })).body.data.project.my_role).toBe("editor");
        // Someone the owner blocked can neither join nor be invited.
        const pest = await registerPlayer("Geneur");
        await api("POST", `${V}/social/blocks`, { token: owner.token, body: { player_id: pest.id } });
        expect((await api("POST", `${V}/projects/${p.id}/join`, { token: pest.token })).status).toBe(403);
        expect((await api("POST", `${V}/projects/${p.id}/invites`, { token: owner.token, body: { role: "viewer", player_id: pest.id } })).status).toBe(403);
        expect((await api("GET", `${V}/projects/discover`, { token: pest.token })).body.data.projects).toHaveLength(0);
    });

    it("deleting a project removes its files", async () => {
        const owner = await registerPlayer("Nova");
        const p = await create(owner);
        await api("PUT", `${V}/projects/${p.id}/content`, { token: owner.token, body: { base_revision: 1, content: LEVEL } });
        expect(await env.STORAGE.get(`projects/${p.id}/r2.json`)).not.toBeNull();
        expect((await api("DELETE", `${V}/projects/${p.id}`, { token: owner.token })).status).toBe(200);
        expect(await env.STORAGE.get(`projects/${p.id}/r1.json`)).toBeNull();
        expect(await env.STORAGE.get(`projects/${p.id}/r2.json`)).toBeNull();
        expect((await api("GET", `${V}/projects`, { token: owner.token })).body.data.projects).toHaveLength(0);
    });
});

describe("history and named versions", () => {
    it("filters, compares, restores as a new revision; versions are immutable and checked", async () => {
        const nova = await registerPlayer("Nova");
        const orion = await registerPlayer("Orion");
        const p = await create(nova);
        const { key } = await keyFor(nova, p.id, { role: "editor" });
        await api("POST", `${V}/projects/join`, { token: orion.token, body: { key } });
        const v2 = { ...LEVEL, name: "Deux", tiles: [[0, 1]], entities: [{ id: "e1" }] } as Record<string, unknown>;
        (v2 as { objects?: unknown }).objects = [{ id: "o1", type: "solid", x: 0 }, { id: "o2", type: "door" }];
        await api("PUT", `${V}/projects/${p.id}/content`, { token: orion.token, body: { base_revision: 1, content: v2, message: "Porte ajoutée" } });
        const v3 = { ...v2, objects: [{ id: "o1", type: "solid", x: 32 }] };
        await api("PUT", `${V}/projects/${p.id}/content`, { token: nova.token, body: { base_revision: 2, content: v3, message: "Sol déplacé" } });
        // Filters.
        const byOrion = await api("GET", `${V}/projects/${p.id}/revisions?author=${orion.id}`, { token: nova.token });
        expect(byOrion.body.data.revisions.map((r: { revision: number }) => r.revision)).toEqual([2]);
        const byText = await api("GET", `${V}/projects/${p.id}/revisions?q=sol`, { token: nova.token });
        expect(byText.body.data.revisions.map((r: { message: string }) => r.message)).toEqual(["Sol déplacé"]);
        // Compare 2 → 3.
        const cmp = await api("GET", `${V}/projects/${p.id}/compare?from=2&to=3`, { token: orion.token });
        expect(cmp.body.data.counts).toEqual({ added: 0, removed: 1, changed: 1, settings: 0 });
        expect(cmp.body.data.removed[0]).toEqual({ id: "o2", type: "door" });
        expect(cmp.body.data.changed[0]).toMatchObject({ id: "o1", fields: ["x"] });
        // Restore 2: a new revision 4, the old ones untouched.
        const rs = await api("POST", `${V}/projects/${p.id}/revisions/2/restore`, { token: nova.token });
        expect(rs.status).toBe(201);
        expect(rs.body.data.saved.revision).toBe(4);
        expect((await api("GET", `${V}/projects/${p.id}/content`, { token: nova.token })).body.name).toBe("Deux");
        expect((await api("GET", `${V}/projects/${p.id}/content?revision=3`, { token: nova.token })).body.objects).toHaveLength(1);
        const revs = await api("GET", `${V}/projects/${p.id}/revisions`, { token: nova.token });
        expect(revs.body.data.revisions[0].message).toBe("Restauration de la révision 2");
        // Named version of revision 3, with the SHA-256 of its content.
        const ver = await api("POST", `${V}/projects/${p.id}/versions`, { token: orion.token, body: { name: "Bêta 1", revision: 3, notes: "Avant les tests" } });
        expect(ver.status).toBe(201);
        expect(ver.body.data.version.content_hash).toMatch(/^[0-9a-f]{64}$/);
        expect((await api("POST", `${V}/projects/${p.id}/versions`, { token: nova.token, body: { name: "Bêta 1" } })).body.error.code).toBe("VERSION_EXISTS");
        const list = await api("GET", `${V}/projects/${p.id}/versions`, { token: nova.token });
        expect(list.body.data.versions[0]).toMatchObject({ name: "Bêta 1", revision: 3, notes: "Avant les tests", author: { username: "Orion" } });
        const content = await api("GET", `${V}/projects/${p.id}/versions/${ver.body.data.version.id}/content`, { token: nova.token });
        expect(content.status).toBe(200);
        expect(content.headers.get("x-content-sha256")).toBe(ver.body.data.version.content_hash);
        expect(content.body.objects).toHaveLength(1);
        // Members are told; viewers cannot restore nor name.
        const n = await api("GET", `${V}/notifications?filter=projects`, { token: nova.token });
        expect(n.body.data.notifications[0].text).toBe("Nouvelle version de La Forteresse : Bêta 1");
        const reader = await registerPlayer("Lecteur");
        const rk = await keyFor(nova, p.id, { role: "viewer" });
        await api("POST", `${V}/projects/join`, { token: reader.token, body: { key: rk.key } });
        expect((await api("POST", `${V}/projects/${p.id}/revisions/1/restore`, { token: reader.token })).status).toBe(403);
        expect((await api("POST", `${V}/projects/${p.id}/versions`, { token: reader.token, body: { name: "x" } })).status).toBe(403);
        // A revision whose stored content no longer matches its hash is refused, not served.
        const key3 = await env.DB.prepare("SELECT content_key FROM project_revisions WHERE project_id = ?1 AND revision = 3").bind(p.id).first<{ content_key: string }>();
        await env.STORAGE.put(key3!.content_key, JSON.stringify({ objects: [] }));
        expect((await api("GET", `${V}/projects/${p.id}/versions/${ver.body.data.version.id}/content`, { token: nova.token })).status).toBe(500);
    });

    it("a restore during live editing keeps the live edits and makes the members rejoin", async () => {
        const nova = await registerPlayer("Nova");
        const p = await create(nova);
        const live = (action: string, body: unknown = {}) => api("POST", `${V}/projects/${p.id}/live/${action}`, { token: nova.token, body });
        const j = await live("join");
        await live("ops", { ops: [{ id: "op-restore-test-1", base_seq: 0, op: { t: "doc", k: "name", v: "En direct" } }] });
        const rs = await api("POST", `${V}/projects/${p.id}/revisions/1/restore`, { token: nova.token });
        expect(rs.status).toBe(201);
        // Revision 2 holds the live edit, revision 3 is the restore.
        expect(rs.body.data.saved.revision).toBe(3);
        expect((await api("GET", `${V}/projects/${p.id}/content?revision=2`, { token: nova.token })).body.name).toBe("En direct");
        expect((await api("GET", `${V}/projects/${p.id}/content`, { token: nova.token })).body.name).toBe("Mon projet");
        await new Promise((r) => setTimeout(r, 220));
        const hb = await live("heartbeat", { since_seq: 1, epoch: j.body.data.epoch });
        expect(hb.body.data.resync).toBe(true);
        const again = await live("join");
        expect(again.body.data.doc.name).toBe("Mon projet");
    });
});
