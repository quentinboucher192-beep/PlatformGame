import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const LEVEL = { id: "projet_live", name: "En direct", planet: "sylvanox", layers: [{ id: "gameplay" }], objects: [{ id: "o1", type: "solid", rect: [0, 0, 64, 16] }] };
let counter = 0;
const opId = () => `op-${Date.now().toString(36)}-${(counter++).toString(36)}-abcdef`;

async function project(owner: TestPlayer, members: [TestPlayer, string][]) {
    const r = await api("POST", `${V}/projects`, { token: owner.token, body: { name: "Projet en direct", content: LEVEL } });
    const id = r.body.data.project.id as string;
    for (const [m, role] of members) {
        const k = await api("POST", `${V}/projects/${id}/invites`, { token: owner.token, body: { role, max_uses: 1 } });
        expect((await api("POST", `${V}/projects/join`, { token: m.token, body: { key: k.body.data.key } })).status).toBe(200);
    }
    return id;
}

const live = (p: string, action: string, who: TestPlayer, body: unknown = {}) => api("POST", `${V}/projects/${p}/live/${action}`, { token: who.token, body });

describe("live editing", () => {
    it("presence, cursors, temporary locks, operations in one order", async () => {
        const nova = await registerPlayer("Nova");
        const orion = await registerPlayer("Orion");
        const p = await project(nova, [[orion, "editor"]]);
        const j1 = await live(p, "join", nova, { view: { x: 100, y: 50, zoom: 1 } });
        expect(j1.status).toBe(200);
        expect(j1.body.data).toMatchObject({ seq: 0, revision: 1 });
        expect(j1.body.data.doc.objects).toHaveLength(1);
        const j2 = await live(p, "join", orion, {});
        expect(j2.body.data.participants.map((x: { username: string }) => x.username)).toEqual(["Nova", "Orion"]);
        expect(j2.body.data.color).not.toBe(j1.body.data.color);
        // Nova adds a platform and selects it: it is locked for Orion.
        const add = await live(p, "ops", nova, { ops: [{ id: opId(), base_seq: 0, op: { t: "add", o: { id: "o2", type: "oneway", rect: [100, 50, 64, 8] } } }] });
        expect(add.body.data.results[0]).toMatchObject({ status: "ok", seq: 1 });
        await live(p, "heartbeat", nova, { since_seq: 1, cursor: { x: 120, y: 60 }, selection: ["o2"], activity: "déplace Plateforme" });
        const hb = await live(p, "heartbeat", orion, { since_seq: 0 });
        expect(hb.body.data.ops).toHaveLength(1);
        expect(hb.body.data.ops[0]).toMatchObject({ seq: 1, op: { t: "add" } });
        expect(hb.body.data.locks).toMatchObject({ o2: nova.id });
        const novaSeen = hb.body.data.participants.find((x: { username: string }) => x.username === "Nova");
        expect(novaSeen).toMatchObject({ cursor: { x: 120, y: 60 }, activity: "déplace Plateforme", selection: ["o2"] });
        const locked = await live(p, "ops", orion, { ops: [{ id: opId(), base_seq: 1, op: { t: "set", id: "o2", f: { rect: [0, 0, 8, 8] } } }] });
        expect(locked.body.data.results[0], JSON.stringify(locked.body.data.results[0])).toMatchObject({ status: "locked", by: "Nova" });
        // Nova lets it go and moves it again; Orion, still at seq 1, is told about it (conflict).
        await live(p, "heartbeat", nova, { selection: [] });
        await new Promise((r) => setTimeout(r, 220));
        await live(p, "ops", nova, { ops: [{ id: opId(), base_seq: 1, op: { t: "set", id: "o2", f: { rect: [200, 50, 64, 8] } } }] });
        const stale = await live(p, "ops", orion, { ops: [{ id: opId(), base_seq: 1, op: { t: "set", id: "o2", f: { rect: [0, 0, 8, 8] } } }] });
        expect(stale.body.data.results[0]).toMatchObject({ status: "conflict", by: "Nova", seq: 2 });
        expect(stale.body.data.results[0].current.rect).toEqual([200, 50, 64, 8]);
        // From the current state, it goes through.
        const ok = await live(p, "ops", orion, { ops: [{ id: opId(), base_seq: 2, op: { t: "set", id: "o2", f: { rect: [0, 0, 8, 8] } } }] });
        expect(ok.body.data.results[0]).toMatchObject({ status: "ok", seq: 3 });
        // Deleted object, taken id, invalid operation.
        const r = await live(p, "ops", orion, {
            ops: [
                { id: opId(), base_seq: 3, op: { t: "del", id: "nope" } },
                { id: opId(), base_seq: 3, op: { t: "add", o: { id: "o1", type: "solid" } } },
                { id: opId(), base_seq: 3, op: { t: "set", id: "o1", f: { id: "o9" } } },
                { id: opId(), base_seq: 3, op: { t: "doc", k: "objects", v: [] } },
            ],
        });
        expect(r.body.data.results.map((x: { status: string }) => x.status)).toEqual(["not_found", "id_taken", "invalid", "invalid"]);
    });

    it("a retried operation is applied once; viewers do not edit; strangers do not see", async () => {
        const nova = await registerPlayer("Nova");
        const reader = await registerPlayer("Lecteur");
        const stranger = await registerPlayer("Inconnu");
        const p = await project(nova, [[reader, "viewer"]]);
        await live(p, "join", nova);
        const id = opId();
        const op = { id, base_seq: 0, op: { t: "doc", k: "name", v: "Nouveau nom" } };
        expect((await live(p, "ops", nova, { ops: [op] })).body.data.results[0]).toMatchObject({ status: "ok", seq: 1 });
        expect((await live(p, "ops", nova, { ops: [op] })).body.data.results[0]).toMatchObject({ status: "ok", seq: 1 });
        const rows = await env.DB.prepare("SELECT COUNT(*) AS n FROM project_ops WHERE project_id = ?1").bind(p).first<{ n: number }>();
        expect(rows?.n).toBe(1);
        expect((await live(p, "join", reader)).status).toBe(200);
        expect((await live(p, "ops", reader, { ops: [{ id: opId(), base_seq: 1, op: { t: "del", id: "o1" } }] })).status).toBe(403);
        // A viewer's selection locks nothing.
        await live(p, "heartbeat", reader, { selection: ["o1"] });
        expect((await live(p, "heartbeat", nova, {})).body.data.locks).toEqual({});
        expect((await live(p, "join", stranger)).status).toBe(404);
    });

    it("operations become revisions; a plain save never hides them", async () => {
        const nova = await registerPlayer("Nova");
        const p = await project(nova, []);
        await live(p, "join", nova);
        await live(p, "ops", nova, { ops: [{ id: opId(), base_seq: 0, op: { t: "set", id: "o1", f: { rect: [8, 8, 64, 16] } } }] });
        // An editor working on an old copy cannot erase the live changes.
        const put = await api("PUT", `${V}/projects/${p}/content`, { token: nova.token, body: { base_revision: 1, content: LEVEL } });
        expect(put.status).toBe(409);
        expect(put.body.error.code).toBe("LIVE_CHANGES");
        // The last member leaves: the room writes a revision with the operations.
        await live(p, "leave", nova);
        const proj = await api("GET", `${V}/projects/${p}`, { token: nova.token });
        expect(proj.body.data.project.revision).toBe(2);
        const content = await api("GET", `${V}/projects/${p}/content`, { token: nova.token });
        expect(content.body.objects[0].rect).toEqual([8, 8, 64, 16]);
        expect((await api("PUT", `${V}/projects/${p}/content`, { token: nova.token, body: { base_revision: 2, content: LEVEL } })).status).toBe(201);
        // The room starts again from that save.
        const again = await live(p, "join", nova);
        expect(again.body.data.revision).toBe(3);
        expect(again.body.data.doc.objects[0].rect).toEqual([0, 0, 64, 16]);
        // Every 200 operations, a revision is written while editing.
        let base = again.body.data.seq as number;
        for (let batch = 0; batch < 4; ++batch) {
            const ops = Array.from({ length: 50 }, (_, i) => ({ id: opId(), base_seq: base, op: { t: "set", id: "o1", f: { z: batch * 50 + i } } }));
            const r = await live(p, "ops", nova, { ops });
            base = r.body.data.seq;
        }
        expect((await api("GET", `${V}/projects/${p}`, { token: nova.token })).body.data.project.revision).toBe(4);
        const revs = await api("GET", `${V}/projects/${p}/revisions`, { token: nova.token });
        expect(revs.body.data.revisions[0].message).toContain("Édition en direct");
    });
});
