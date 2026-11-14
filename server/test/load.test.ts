// Phase 6: several accounts at once, load on a live room, refusals under pressure. Everything
// runs in parallel against the real Worker and its project room (Miniflare): the checks are
// that every accepted operation has exactly one place in the order, that nothing is applied
// twice or silently lost, and that concurrent saves and invitations stay consistent.

import { env, runDurableObjectAlarm, runInDurableObject } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const LEVEL = {
    id: "projet_charge",
    name: "Charge",
    planet: "sylvanox",
    layers: [{ id: "gameplay" }],
    objects: [{ id: "o1", type: "solid", rect: [0, 0, 64, 16] }],
};
let counter = 0;
const opId = (tag: string) => `${tag}-${Date.now().toString(36)}-${(counter++).toString(36)}-load`;
/** Reaches into a project room (its private state), as the platform would see it. */
const inRoom = runInDurableObject as unknown as (stub: unknown, cb: (room: any) => void) => Promise<void>;
const live = (p: string, action: string, who: TestPlayer, body: unknown = {}) => api("POST", `${V}/projects/${p}/live/${action}`, { token: who.token, body });

type Op = {
    t: string;
    id?: string;
    o?: Record<string, unknown>;
    f?: Record<string, unknown>;
    k?: string;
    v?: unknown;
};

/** The server's rules (live.ts applyOp), to replay the order independently. */
function apply(doc: Record<string, any>, op: Op): void {
    const objs = doc.objects as Record<string, unknown>[];
    if (op.t === "add") objs.push(structuredClone(op.o!));  // a copy: the operation stays as received
    else if (op.t === "del") doc.objects = objs.filter((o) => o.id !== op.id);
    else if (op.t === "set") {
        const o = objs.find((x) => x.id === op.id)!;
        for (const [k, v] of Object.entries(op.f!)) v === null ? delete o[k] : (o[k] = v);
    } else if (op.t === "doc") doc[op.k!] = op.v;
}

async function team(n: number, role = "editor"): Promise<{ owner: TestPlayer; members: TestPlayer[]; project: string }> {
    const owner = await registerPlayer("Chef");
    const members = await Promise.all(Array.from({ length: n }, (_, i) => registerPlayer(`Membre${i + 1}`)));
    const r = await api("POST", `${V}/projects`, {
        token: owner.token,
        body: { name: "Projet chargé", content: LEVEL },
    });
    expect(r.status).toBe(201);
    const project = r.body.data.project.id as string;
    const k = await api("POST", `${V}/projects/${project}/invites`, {
        token: owner.token,
        body: { role, max_uses: n },
    });
    for (const m of members)
        expect(
            (
                await api("POST", `${V}/projects/join`, {
                    token: m.token,
                    body: { key: k.body.data.key },
                })
            ).status,
        ).toBe(200);
    return { owner, members, project };
}

describe("several accounts and load", () => {
    it("eight editors at once: one order, nothing lost, nothing applied twice", async () => {
        const { owner, members, project } = await team(7);
        const everyone = [owner, ...members];
        for (const m of everyone) expect((await live(project, "join", m)).status).toBe(200);
        // Each member, in parallel: 3 batches of 10 operations on its own objects, and one
        // edit of the shared object o1 made from the initial state (seq 0).
        const sent = new Map<string, { who: string; op: Op }>();
        const batch = (m: TestPlayer, b: number) => {
            const ops = Array.from({ length: 10 }, (_, i) => {
                const oid = `${m.username}_${b}_${i}`;
                const op: Op =
                    i % 2 === 0 || b === 0
                        ? {
                              t: "add",
                              o: { id: oid, type: "oneway", rect: [i * 32, b * 32, 32, 8] },
                          }
                        : {
                              t: "set",
                              id: `${m.username}_0_${i}`,
                              f: { rect: [i, b, 16, 16] },
                          };
                const id = opId(m.username.toLowerCase());
                sent.set(id, { who: m.id, op });
                return { id, base_seq: 0, op };
            });
            return live(project, "ops", m, { ops });
        };
        const firstRound = await Promise.all(everyone.map((m) => batch(m, 0)));
        const shared = await Promise.all(
            everyone.map((m) => {
                const id = opId("shared");
                const op: Op = {
                    t: "set",
                    id: "o1",
                    f: { rect: [1, 2, 3, 4], by: m.username },
                };
                sent.set(id, { who: m.id, op });
                return live(project, "ops", m, { ops: [{ id, base_seq: 0, op }] });
            }),
        );
        const later = await Promise.all(everyone.flatMap((m) => [batch(m, 1), batch(m, 2)]));
        const results = [...firstRound, ...shared, ...later].flatMap((r) => {
            expect(r.status, JSON.stringify(r.body)).toBe(200);
            return r.body.data.results as {
                id: string;
                status: string;
                seq?: number;
                by?: string;
            }[];
        });
        expect(results).toHaveLength(sent.size);
        const ok = results.filter((r) => r.status === "ok");
        // The shared object: the first edit wins, the 7 others are told who changed it.
        const sharedResults = shared.map((r) => r.body.data.results[0]);
        expect(sharedResults.filter((r: { status: string }) => r.status === "ok")).toHaveLength(1);
        expect(sharedResults.filter((r: { status: string }) => r.status === "conflict")).toHaveLength(7);
        // Everything else went through: 8 members × 30 operations + 1 shared edit.
        expect(ok).toHaveLength(8 * 30 + 1);
        // One place each in the order, no gap, no duplicate.
        const seqs = ok.map((r) => r.seq!).sort((a, b) => a - b);
        expect(seqs).toEqual(Array.from({ length: seqs.length }, (_, i) => i + 1));
        // A retry of operations already applied (answers lost): same seq, nothing applied twice.
        const again = ok.slice(0, 20).map((r) => ({ id: r.id, base_seq: 0, op: sent.get(r.id)!.op }));
        const retry = await live(project, "ops", members[0]!, { ops: again });
        expect(retry.body.data.results.map((r: { seq: number }) => r.seq)).toEqual(ok.slice(0, 20).map((r) => r.seq));
        expect(retry.body.data.seq).toBe(seqs.length);
        // A newcomer replays the same order and gets the same document as the room.
        const late = await live(project, "join", owner);
        const hb = await live(project, "heartbeat", members[6]!, { since_seq: 0 });
        expect(hb.body.data.ops).toHaveLength(seqs.length);
        const doc = structuredClone(LEVEL) as Record<string, any>;
        for (const o of hb.body.data.ops as { seq: number; op: Op }[]) apply(doc, o.op);
        expect(doc.objects).toEqual(late.body.data.doc.objects);
        expect(doc.objects).toHaveLength(1 + 8 * (10 + 5 + 5));
        // Past 200 operations, the room wrote a revision by itself.
        const revs = await api("GET", `${V}/projects/${project}/revisions`, {
            token: owner.token,
        });
        const auto = (revs.body.data.revisions as { message: string }[])
            .map((r) => /^Édition en direct \(opérations 1 à (\d+)\)$/.exec(r.message))
            .find(Boolean);
        const upTo = Number(auto?.[1]);
        expect(upTo).toBeGreaterThanOrEqual(200);
        // That revision holds exactly the operations 1 to upTo, even though others arrived
        // while it was being written.
        const autoRev = (revs.body.data.revisions as { message: string; revision: number }[]).find((r) => r.message === auto![0])!.revision;
        const written = await api("GET", `${V}/projects/${project}/content?revision=${autoRev}`, { token: owner.token });
        const partial = structuredClone(LEVEL) as Record<string, any>;
        for (const o of hb.body.data.ops as { seq: number; op: Op }[]) if (o.seq <= upTo) apply(partial, o.op);
        expect((written.body as { objects: unknown[] }).objects).toEqual(partial.objects);
        // Everyone leaves: the rest becomes a revision too, and the content is the room's document.
        for (const m of everyone) await live(project, "leave", m);
        const content = await api("GET", `${V}/projects/${project}/content`, {
            token: owner.token,
        });
        const saved = content.body as { objects: unknown[] }; // the level JSON itself
        expect(saved.objects).toEqual(late.body.data.doc.objects);
    });

    it("a flood of operations is refused with 429 (kept by the game), the room stays usable", async () => {
        const { owner, members, project } = await team(1);
        const [ed] = members as [TestPlayer];
        await live(project, "join", ed);
        const burst = (n: number) =>
            live(project, "ops", ed, {
                ops: Array.from({ length: 50 }, (_, i) => ({
                    id: opId("flood"),
                    base_seq: 0,
                    op: { t: "add", o: { id: `f${n}_${i}`, type: "coin" } },
                })),
            });
        for (let n = 0; n < 6; ++n) expect((await burst(n)).status).toBe(200);
        const refused = await burst(6);
        expect(refused.status).toBe(429);
        expect(refused.body.error.code).toBe("RATE_LIMITED");
        expect(Number(refused.headers.get("retry-after"))).toBeGreaterThan(0);
        // Nothing of the refused batch was applied; the others still edit and see the room.
        const hb = await live(project, "heartbeat", owner, { since_seq: 0 });
        expect(hb.body.data.seq).toBe(300);
        const other = await live(project, "ops", owner, {
            ops: [
                {
                    id: opId("chef"),
                    base_seq: 300,
                    op: { t: "doc", k: "name", v: "Toujours là" },
                },
            ],
        });
        expect(other.body.data.results[0]).toMatchObject({
            status: "ok",
            seq: 301,
        });
    });

    it("concurrent saves and invitations stay consistent", async () => {
        const { owner, members, project } = await team(5);
        // Five editors save from revision 1 at the same time: one wins, the others are told.
        const saves = await Promise.all(
            members.map((m, i) =>
                api("PUT", `${V}/projects/${project}/content`, {
                    token: m.token,
                    body: {
                        base_revision: 1,
                        content: { ...LEVEL, name: `Version ${i}` },
                        message: `envoi ${i}`,
                    },
                }),
            ),
        );
        expect(saves.filter((r) => r.status === 201)).toHaveLength(1);
        const lost = saves.filter((r) => r.status === 409);
        expect(lost).toHaveLength(4);
        for (const r of lost)
            expect(r.body.error).toMatchObject({
                code: "REVISION_CONFLICT",
                details: { current_revision: 2 },
            });
        // A key for 3 uses tried by 6 players at once: exactly 3 come in.
        const key = (
            await api("POST", `${V}/projects/${project}/invites`, {
                token: owner.token,
                body: { role: "tester", max_uses: 3 },
            })
        ).body.data.key;
        const guests = await Promise.all(Array.from({ length: 6 }, (_, i) => registerPlayer(`Invite${i + 1}`)));
        const joins = await Promise.all(guests.map((g) => api("POST", `${V}/projects/join`, { token: g.token, body: { key } })));
        expect(joins.filter((r) => r.status === 200)).toHaveLength(3);
        for (const r of joins.filter((x) => x.status !== 200)) expect(r.body.error.code).toBe("INVITE_INVALID");
        const list = await api("GET", `${V}/projects/${project}/members`, {
            token: owner.token,
        });
        expect(list.body.data.members).toHaveLength(1 + 5 + 3);
        // Two versions with the same name at the same time: one only (versions never replace).
        const named = await Promise.all(
            members.slice(0, 2).map((m) =>
                api("POST", `${V}/projects/${project}/versions`, {
                    token: m.token,
                    body: { name: "Bêta" },
                }),
            ),
        );
        expect(named.map((r) => r.status).sort()).toEqual([201, 409]);
    });

    it("every account sees only what its role allows (live, history, versions)", async () => {
        const owner = await registerPlayer("Chef");
        const [admin, editor, tester, viewer, stranger] = (await Promise.all(
            ["Gerante", "Editeur", "Testeur", "Lecteur", "Inconnu"].map((n) => registerPlayer(n)),
        )) as [TestPlayer, TestPlayer, TestPlayer, TestPlayer, TestPlayer];
        const r = await api("POST", `${V}/projects`, {
            token: owner.token,
            body: { name: "Rôles", content: LEVEL },
        });
        const p = r.body.data.project.id as string;
        for (const [m, role] of [
            [admin, "admin"],
            [editor, "editor"],
            [tester, "tester"],
            [viewer, "viewer"],
        ] as [TestPlayer, string][]) {
            const k = await api("POST", `${V}/projects/${p}/invites`, {
                token: owner.token,
                body: { role, max_uses: 1 },
            });
            expect(
                (
                    await api("POST", `${V}/projects/join`, {
                        token: m.token,
                        body: { key: k.body.data.key },
                    })
                ).status,
            ).toBe(200);
        }
        const v = await api("POST", `${V}/projects/${p}/versions`, {
            token: owner.token,
            body: { name: "Départ" },
        });
        const vid = v.body.data.version.id as string;
        const checks: [string, string, unknown, number][] = [
            // method, path, body, lowest rank allowed (0 viewer, 1 tester, 2 editor)
            ["POST", `/projects/${p}/live/join`, {}, 0],
            ["POST", `/projects/${p}/live/heartbeat`, { since_seq: 0 }, 0],
            [
                "POST",
                `/projects/${p}/live/ops`,
                {
                    ops: [
                        {
                            id: opId("role"),
                            base_seq: 0,
                            op: { t: "doc", k: "name", v: "x" },
                        },
                    ],
                },
                2,
            ],
            ["GET", `/projects/${p}/revisions?q=initiale`, undefined, 0],
            ["GET", `/projects/${p}/compare?from=1&to=1`, undefined, 0],
            ["GET", `/projects/${p}/versions`, undefined, 0],
            ["GET", `/projects/${p}/versions/${vid}/content`, undefined, 0],
            ["POST", `/projects/${p}/versions`, { name: "Essai" }, 2],
            ["POST", `/projects/${p}/revisions/1/restore`, {}, 2],
        ];
        const ranks: [TestPlayer, number][] = [
            [viewer, 0],
            [tester, 1],
            [editor, 2],
            [admin, 3],
        ];
        for (const [method, path, body, min] of checks) {
            // Someone outside the project does not even learn that it exists.
            expect((await api(method, V + path, { token: stranger.token, body })).status, `${path} inconnu`).toBe(404);
            expect((await api(method, V + path, { body })).status, `${path} sans session`).toBe(401);
            for (const [who, rank] of ranks) {
                const res = await api(method, V + path, {
                    token: who.token,
                    body: path.endsWith("/versions") && method === "POST" ? { name: `Essai ${who.username}` } : body,
                });
                if (rank >= min) expect(res.status, `${path} ${who.username}`).toBeLessThan(300);
                else expect(res.status, `${path} ${who.username}`).toBe(403);
            }
        }
    });
});

describe("network cuts", () => {
    it("a member who disappears without leaving: the room writes the revision by itself", async () => {
        const { owner, members, project } = await team(1);
        const [ed] = members as [TestPlayer];
        await live(project, "join", owner);
        await live(project, "join", ed);
        const r = await live(project, "ops", ed, {
            ops: [
                {
                    id: opId("cut"),
                    base_seq: 0,
                    op: { t: "doc", k: "name", v: "Avant la coupure" },
                },
            ],
        });
        expect(r.body.data.results[0].status).toBe("ok");
        await live(project, "leave", owner); // the other one never says goodbye (game closed, network cut)
        const stub = env.PROJECT_ROOMS.get(env.PROJECT_ROOMS.idFromName(project));
        // 25 s later (past the 20 s of silence), the room's alarm runs.
        await inRoom(stub, (room) => {
            for (const p of room.participants.values()) p.lastSeen -= 25_000;
        });
        expect(await runDurableObjectAlarm(stub)).toBe(true);
        const revs = await api("GET", `${V}/projects/${project}/revisions`, {
            token: owner.token,
        });
        expect(revs.body.data.revisions[0]).toMatchObject({
            revision: 2,
            message: "Édition en direct (opérations 1 à 1)",
            live: true,
        });
        const content = await api("GET", `${V}/projects/${project}/content`, {
            token: owner.token,
        });
        expect((content.body as { name: string }).name).toBe("Avant la coupure");
        // A classic save works again (nothing is waiting outside a revision).
        expect(
            (
                await api("PUT", `${V}/projects/${project}/content`, {
                    token: owner.token,
                    body: { base_revision: 2, content: LEVEL },
                })
            ).status,
        ).toBe(201);
    });
});

describe("network cuts (room put to sleep)", () => {
    it("the alarm wakes a fresh room: it reads the edits back and writes the revision", async () => {
        const { owner, members, project } = await team(1);
        const [ed] = members as [TestPlayer];
        await live(project, "join", ed);
        await live(project, "ops", ed, {
            ops: [
                {
                    id: opId("sleep"),
                    base_seq: 0,
                    op: {
                        t: "add",
                        o: { id: "z1", type: "shard", rect: [0, 0, 16, 16] },
                    },
                },
            ],
        });
        const stub = env.PROJECT_ROOMS.get(env.PROJECT_ROOMS.idFromName(project));
        // The platform evicts the room: everything in memory is gone (presence, document, id).
        await inRoom(stub, (room) => {
            room.participants.clear();
            room.doc = null;
            room.projectId = "";
            room.seq = 0;
            room.snapshotSeq = 0;
        });
        expect(await runDurableObjectAlarm(stub)).toBe(true);
        const revs = await api("GET", `${V}/projects/${project}/revisions`, {
            token: owner.token,
        });
        expect(revs.body.data.revisions[0]).toMatchObject({
            revision: 2,
            message: "Édition en direct (opérations 1 à 1)",
        });
        const content = await api("GET", `${V}/projects/${project}/content`, {
            token: owner.token,
        });
        expect((content.body as { objects: { id: string }[] }).objects.map((o) => o.id)).toContain("z1");
    });
});

describe("catching up", () => {
    it("a late member receives each operation as it was made, not the object's later state", async () => {
        const { owner, members, project } = await team(1);
        const [ed] = members as [TestPlayer];
        await live(project, "join", ed);
        await live(project, "ops", ed, {
            ops: [
                { id: opId("late"), base_seq: 0, op: { t: "add", o: { id: "p1", type: "oneway", rect: [0, 0, 32, 8] } } },
                { id: opId("late"), base_seq: 1, op: { t: "set", id: "p1", f: { rect: [64, 64, 32, 8] } } },
            ],
        });
        const hb = await live(project, "heartbeat", owner, { since_seq: 0 });
        expect(hb.body.data.ops.map((o: { op: Op }) => o.op)).toEqual([
            { t: "add", o: { id: "p1", type: "oneway", rect: [0, 0, 32, 8] } },
            { t: "set", id: "p1", f: { rect: [64, 64, 32, 8] } },
        ]);
    });
});
