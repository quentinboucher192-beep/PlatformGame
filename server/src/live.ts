// Live editing of collaborative projects (Cosmic Online, phases 3-4).
//
// One Durable Object per project (ProjectRoom) keeps, in memory only, who is in the editor
// (presence: cursor, view, selection, current activity, colour) and the temporary locks
// (an object selected by a member cannot be changed by another one). It also gives every
// operation its sequence number, so that all the members apply the same operations in the
// same order. The operations are persistent (D1 project_ops); every 200 of them, and when
// the last member leaves, the room writes the document as a new revision of the project.
//
// Operations (on the level's JSON document): add an object, set fields of an object, delete
// an object, replace a top-level key of the document (layers, events, settings...). An
// operation made from an older state that touches what another member changed since is
// refused (conflict) with the current value: nothing is ever overwritten silently. The
// room is reached only through the Worker, which checks the session and the role first.

import { DurableObject } from "cloudflare:workers";
import { requireAuth } from "./auth";
import { Errors, json, nowIso, readJsonObject, readLimited, type Bindings, type Ctx } from "./http";

const OBJECT_ID_RE = /^[A-Za-z0-9_.-]{1,60}$/;
const CLIENT_OP_RE = /^[A-Za-z0-9-]{8,64}$/;
const DOC_KEY_RE = /^[A-Za-z][A-Za-z0-9_]{0,40}$/;
const MAX_OP_BYTES = 64 * 1024;
const MAX_OPS_PER_REQUEST = 50;
const SNAPSHOT_EVERY = 200;
const STALE_MS = 20_000;
const BUFFER = 1000;
const COLORS = ["#39F0D8", "#FFB347", "#FF4D6D", "#B04CFF", "#7FB8FF", "#78E68C", "#FFD98A", "#FF8A5C"];
const ROLE_RANK: Record<string, number> = { viewer: 1, tester: 2, editor: 3, admin: 4, owner: 5 };

export type Op =
    | { t: "add"; o: Record<string, unknown> }
    | { t: "set"; id: string; f: Record<string, unknown> }
    | { t: "del"; id: string }
    | { t: "doc"; k: string; v: unknown };

interface Participant {
    id: string;
    name: string;
    avatar: string;
    role: string;
    color: string;
    cursor: { x: number; y: number } | null;
    view: { x: number; y: number; zoom: number } | null;
    selection: string[];
    activity: string;
    lastSeen: number;
    joinedAt: number;
    lastBeat: number;
}

interface StoredOp {
    seq: number;
    id: string;
    author: string | null;
    op: Op;
}

// ------------------------------------------------------------- operations

/** Checks the shape of an operation from the game (never trusted). Returns a message or null. */
export function invalidOp(op: unknown): string | null {
    if (!op || typeof op !== "object" || Array.isArray(op)) return "opération attendue";
    const o = op as Record<string, unknown>;
    if (JSON.stringify(o).length > MAX_OP_BYTES) return "opération trop volumineuse";
    switch (o.t) {
        case "add": {
            const obj = o.o as Record<string, unknown> | undefined;
            if (!obj || typeof obj !== "object" || Array.isArray(obj)) return "objet attendu";
            if (typeof obj.id !== "string" || !OBJECT_ID_RE.test(obj.id)) return "id d'objet invalide";
            if (typeof obj.type !== "string" || obj.type.length > 40) return "type d'objet invalide";
            return null;
        }
        case "set": {
            if (typeof o.id !== "string" || !OBJECT_ID_RE.test(o.id)) return "id d'objet invalide";
            const f = o.f as Record<string, unknown> | undefined;
            if (!f || typeof f !== "object" || Array.isArray(f) || Object.keys(f).length === 0) return "champs attendus";
            if (Object.keys(f).some((k) => !DOC_KEY_RE.test(k) || k === "id")) return "champ invalide";
            return null;
        }
        case "del":
            return typeof o.id === "string" && OBJECT_ID_RE.test(o.id) ? null : "id d'objet invalide";
        case "doc":
            if (typeof o.k !== "string" || !DOC_KEY_RE.test(o.k) || o.k === "objects" || o.k === "id") return "clé de document invalide";
            return "v" in o ? null : "valeur attendue";
        default:
            return "type d'opération inconnu";
    }
}

function objects(doc: Record<string, unknown>): Record<string, unknown>[] {
    if (!Array.isArray(doc.objects)) doc.objects = [];
    return doc.objects as Record<string, unknown>[];
}

/** Applies an accepted operation to the document (the game does exactly the same). */
export function applyOp(doc: Record<string, unknown>, op: Op): void {
    const list = objects(doc);
    switch (op.t) {
        case "add":
            list.push(op.o);
            break;
        case "set": {
            // null removes the field.
            const obj = list.find((x) => x.id === op.id);
            if (obj)
                for (const [k, v] of Object.entries(op.f)) {
                    if (v === null) delete obj[k];
                    else obj[k] = v;
                }
            break;
        }
        case "del": {
            const i = list.findIndex((x) => x.id === op.id);
            if (i >= 0) list.splice(i, 1);
            break;
        }
        case "doc":
            if (op.v === null) delete doc[op.k];
            else doc[op.k] = op.v;
            break;
    }
}

function touchKey(op: Op): string {
    return op.t === "doc" ? "d:" + op.k : "o:" + (op.t === "add" ? (op.o.id as string) : op.id);
}

// -------------------------------------------------------------------- room

export class ProjectRoom extends DurableObject<Bindings> {
    private projectId = "";
    private doc: Record<string, unknown> | null = null;
    private revision = 0;
    private snapshotSeq = 0;
    private seq = 0;
    private participants = new Map<string, Participant>();
    private touched = new Map<string, { seq: number; author: string | null }>();
    private recent: StoredOp[] = [];
    private snapshotting = false;
    /** Changes when the document is replaced from outside (save, restore): members rejoin. */
    private epoch = 0;

    async fetch(request: Request): Promise<Response> {
        const url = new URL(request.url);
        this.projectId = request.headers.get("x-project-id") ?? this.projectId;
        try {
            switch (url.pathname) {
                case "/join":
                    return Response.json(await this.join(request));
                case "/heartbeat":
                    return Response.json(await this.heartbeat(request));
                case "/ops":
                    return Response.json(await this.ops(request));
                case "/leave":
                    return Response.json(await this.leave(request));
                case "/reload":
                    this.doc = null;
                    this.epoch += 1;
                    return Response.json({ reloaded: true });
                case "/flush":
                    // Live edits not yet in a revision are written now (before a restore).
                    if (this.projectId) {
                        await this.load();
                        if (this.seq > this.snapshotSeq) await this.snapshot("Édition en direct");
                    }
                    return Response.json({ revision: this.revision });
                default:
                    return Response.json({ error: "unknown" }, { status: 404 });
            }
        } catch (e) {
            return Response.json({ error: e instanceof Error ? e.message : String(e) }, { status: 500 });
        }
    }

    async alarm(): Promise<void> {
        this.prune();
        if (this.participants.size === 0 && this.doc && this.seq > this.snapshotSeq) await this.snapshot("Édition en direct");
        if (this.participants.size > 0) await this.ctx.storage.setAlarm(Date.now() + 30_000);
    }

    // ------------------------------------------------------------ state

    private async load(): Promise<void> {
        if (this.doc) return;
        const db = this.env.DB;
        const p = await db.prepare("SELECT revision, ops_seq FROM projects WHERE id = ?1 AND deleted_at IS NULL").bind(this.projectId).first<{ revision: number; ops_seq: number }>();
        if (!p) throw new Error("project gone");
        let doc: Record<string, unknown> = { objects: [] };
        let snapshotSeq = 0;
        if (p.revision > 0) {
            const r = await db.prepare("SELECT content_key, ops_seq FROM project_revisions WHERE project_id = ?1 AND revision = ?2")
                .bind(this.projectId, p.revision)
                .first<{ content_key: string; ops_seq: number }>();
            const obj = r ? await this.env.STORAGE.get(r.content_key) : null;
            if (!r || !obj) throw new Error("revision missing");
            doc = (await obj.json()) as Record<string, unknown>;
            snapshotSeq = r.ops_seq;
        }
        const rows = await db
            .prepare("SELECT seq, client_op_id, author_id, op FROM project_ops WHERE project_id = ?1 AND seq > ?2 ORDER BY seq")
            .bind(this.projectId, snapshotSeq)
            .all<{ seq: number; client_op_id: string; author_id: string | null; op: string }>();
        this.touched.clear();
        this.recent = [];
        let seq = snapshotSeq;
        for (const row of rows.results) {
            const op = JSON.parse(row.op) as Op;
            applyOp(doc, op);
            this.touched.set(touchKey(op), { seq: row.seq, author: row.author_id });
            this.recent.push({ seq: row.seq, id: row.client_op_id, author: row.author_id, op });
            seq = row.seq;
        }
        this.recent = this.recent.slice(-BUFFER);
        this.doc = doc;
        this.revision = p.revision;
        this.snapshotSeq = snapshotSeq;
        this.seq = Math.max(seq, p.ops_seq);
    }

    private prune(): void {
        const now = Date.now();
        for (const [id, p] of this.participants) if (now - p.lastSeen > STALE_MS) this.participants.delete(id);
    }

    /** Objects selected by someone else than `me`: they are locked for `me`. */
    private locks(): Record<string, string> {
        const out: Record<string, string> = {};
        for (const p of this.participants.values()) for (const id of p.selection) out[id] ??= p.id;
        return out;
    }

    private presence(me: string) {
        return [...this.participants.values()]
            .sort((a, b) => a.joinedAt - b.joinedAt)
            .map((p) => ({
                id: p.id,
                username: p.name,
                avatar: p.avatar,
                role: p.role,
                color: p.color,
                cursor: p.cursor,
                view: p.view,
                selection: p.selection,
                activity: p.activity,
                idle_ms: Date.now() - p.lastSeen,
                me: p.id === me,
            }));
    }

    private async opsSince(since: number): Promise<{ ops: unknown[]; resync: boolean }> {
        if (since >= this.seq) return { ops: [], resync: false };
        const first = this.recent.length ? this.recent[0]!.seq : this.seq + 1;
        if (since + 1 < first) return { ops: [], resync: true };  // too far behind: rejoin (full document)
        return {
            ops: this.recent.filter((o) => o.seq > since).map((o) => ({ seq: o.seq, id: o.id, author: o.author, op: o.op })),
            resync: false,
        };
    }

    private identity(request: Request) {
        const h = request.headers;
        return {
            id: h.get("x-player-id")!,
            name: h.get("x-player-name") ?? "",
            avatar: h.get("x-player-avatar") ?? "avatar_01",
            role: h.get("x-player-role") ?? "viewer",
        };
    }

    private touch(me: ReturnType<ProjectRoom["identity"]>, body: Record<string, unknown>): Participant {
        let p = this.participants.get(me.id);
        const now = Date.now();
        if (!p) {
            const used = new Set([...this.participants.values()].map((x) => x.color));
            p = {
                ...me,
                color: COLORS.find((c) => !used.has(c)) ?? COLORS[this.participants.size % COLORS.length]!,
                cursor: null,
                view: null,
                selection: [],
                activity: "",
                lastSeen: now,
                joinedAt: now,
                lastBeat: 0,
            };
            this.participants.set(me.id, p);
        }
        p.role = me.role;
        p.name = me.name;
        p.lastSeen = now;
        const num = (v: unknown, lim = 1e7) => (typeof v === "number" && Number.isFinite(v) ? Math.max(-lim, Math.min(lim, v)) : 0);
        const c = body.cursor as Record<string, unknown> | null | undefined;
        if (c && typeof c === "object") p.cursor = { x: num(c.x), y: num(c.y) };
        else if (c === null) p.cursor = null;
        const v = body.view as Record<string, unknown> | undefined;
        if (v && typeof v === "object") p.view = { x: num(v.x), y: num(v.y), zoom: Math.max(0.05, Math.min(8, num(v.zoom, 8) || 1)) };
        if (Array.isArray(body.selection))
            // Only editors lock what they select.
            p.selection = ROLE_RANK[p.role]! >= ROLE_RANK.editor! ? body.selection.filter((s): s is string => typeof s === "string" && OBJECT_ID_RE.test(s)).slice(0, 200) : [];
        if (typeof body.activity === "string") p.activity = body.activity.slice(0, 80);
        return p;
    }

    // ----------------------------------------------------------- routes

    private async join(request: Request) {
        await this.load();
        this.prune();
        const me = this.identity(request);
        const body = (await request.json()) as Record<string, unknown>;
        const p = this.touch(me, body);
        await this.ctx.storage.setAlarm(Date.now() + 30_000);
        return {
            doc: this.doc,
            seq: this.seq,
            revision: this.revision,
            epoch: this.epoch,
            color: p.color,
            participants: this.presence(me.id),
            locks: this.locks(),
        };
    }

    private async heartbeat(request: Request) {
        await this.load();
        this.prune();
        const me = this.identity(request);
        const body = (await request.json()) as Record<string, unknown>;
        // Too frequent: the state (cursor, selection...) is kept, the answer is short.
        const known = this.participants.get(me.id);
        const tooSoon = known !== undefined && Date.now() - known.lastBeat < 200;
        const p = this.touch(me, body);
        if (tooSoon) return { throttled: true, seq: this.seq };
        p.lastBeat = Date.now();
        const since = typeof body.since_seq === "number" ? body.since_seq : this.seq;
        const { ops, resync } = await this.opsSince(since);
        // The document was replaced since this member joined: rejoin to get it.
        const replaced = typeof body.epoch === "number" && body.epoch !== this.epoch;
        return { seq: this.seq, revision: this.revision, epoch: this.epoch, participants: this.presence(me.id), locks: this.locks(), ops, resync: resync || replaced };
    }

    private async leave(request: Request) {
        const me = this.identity(request);
        this.participants.delete(me.id);
        if (this.participants.size === 0 && this.doc && this.seq > this.snapshotSeq) await this.snapshot("Édition en direct");
        return { left: true };
    }

    private async ops(request: Request) {
        await this.load();
        this.prune();
        const me = this.identity(request);
        if (ROLE_RANK[me.role]! < ROLE_RANK.editor!) return { error: "role" };
        const body = (await request.json()) as { ops?: unknown };
        const list = Array.isArray(body.ops) ? body.ops.slice(0, MAX_OPS_PER_REQUEST) : [];
        const locks = this.locks();
        const doc = this.doc!;
        const results: Record<string, unknown>[] = [];
        const accepted: StoredOp[] = [];
        for (const raw of list) {
            const item = (raw ?? {}) as Record<string, unknown>;
            const cid = typeof item.id === "string" ? item.id : "";
            if (!CLIENT_OP_RE.test(cid)) {
                results.push({ id: cid, status: "invalid", message: "id d'opération invalide" });
                continue;
            }
            // Already applied (a retry after a lost answer): same answer, applied once.
            const done = this.recent.find((o) => o.id === cid) ?? accepted.find((o) => o.id === cid);
            if (done) {
                results.push({ id: cid, status: "ok", seq: done.seq });
                continue;
            }
            const problem = invalidOp(item.op);
            if (problem) {
                results.push({ id: cid, status: "invalid", message: problem });
                continue;
            }
            const op = item.op as Op;
            const base = typeof item.base_seq === "number" ? item.base_seq : 0;
            const key = touchKey(op);
            const objectId = op.t === "doc" ? null : op.t === "add" ? (op.o.id as string) : op.id;
            const existing = objectId ? objects(doc).find((o) => o.id === objectId) : null;
            if (objectId && locks[objectId] && locks[objectId] !== me.id) {
                const who = this.participants.get(locks[objectId]!);
                results.push({ id: cid, status: "locked", by: who?.name ?? null, current: existing ?? null });
                continue;
            }
            if (op.t === "add" && existing) {
                results.push({ id: cid, status: "id_taken", current: existing });
                continue;
            }
            if ((op.t === "set" || op.t === "del") && !existing) {
                results.push({ id: cid, status: "not_found" });
                continue;
            }
            const last = this.touched.get(key);
            if (last && last.seq > base && last.author !== me.id) {
                const who = last.author ? [...this.participants.values()].find((x) => x.id === last.author)?.name : null;
                results.push({ id: cid, status: "conflict", by: who ?? null, seq: last.seq, current: op.t === "doc" ? (doc[op.k] ?? null) : (existing ?? null) });
                continue;
            }
            applyOp(doc, op);
            this.seq += 1;
            const stored = { seq: this.seq, id: cid, author: me.id, op };
            this.touched.set(key, { seq: this.seq, author: me.id });
            accepted.push(stored);
            results.push({ id: cid, status: "ok", seq: this.seq });
        }
        if (accepted.length) {
            const now = nowIso();
            const db = this.env.DB;
            await db.batch([
                ...accepted.map((a) =>
                    db.prepare("INSERT OR IGNORE INTO project_ops (project_id, seq, client_op_id, author_id, op, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)").bind(
                        this.projectId,
                        a.seq,
                        a.id,
                        a.author,
                        JSON.stringify(a.op),
                        now,
                    ),
                ),
                db.prepare("UPDATE projects SET ops_seq = ?2, updated_at = ?3 WHERE id = ?1").bind(this.projectId, this.seq, now),
            ]);
            this.recent.push(...accepted);
            if (this.recent.length > BUFFER) this.recent = this.recent.slice(-BUFFER);
            if (this.seq - this.snapshotSeq >= SNAPSHOT_EVERY) await this.snapshot("Édition en direct");
        }
        return { results, seq: this.seq };
    }

    /** Writes the document as a new revision (taking the next revision number, never replacing one). */
    private async snapshot(message: string): Promise<void> {
        if (this.snapshotting || !this.doc) return;
        this.snapshotting = true;
        try {
            const db = this.env.DB;
            const p = await db.prepare("SELECT revision FROM projects WHERE id = ?1").bind(this.projectId).first<{ revision: number }>();
            if (!p) return;
            const revision = p.revision + 1;
            const text = JSON.stringify(this.doc);
            const claim = await db.prepare("UPDATE projects SET revision = ?2 WHERE id = ?1 AND revision = ?3").bind(this.projectId, revision, p.revision).run();
            if (claim.meta.changes !== 1) return;
            const key = `projects/${this.projectId}/r${revision}.json`;
            await this.env.STORAGE.put(key, text, { httpMetadata: { contentType: "application/json" } });
            const hash = [...new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text)))].map((b) => b.toString(16).padStart(2, "0")).join("");
            await db
                .prepare(
                    "INSERT INTO project_revisions (project_id, revision, content_key, content_hash, size, author_id, message, created_at, ops_seq) VALUES (?1, ?2, ?3, ?4, ?5, NULL, ?6, ?7, ?8)",
                )
                .bind(this.projectId, revision, key, hash, text.length, `${message} (opérations ${this.snapshotSeq + 1} à ${this.seq})`, nowIso(), this.seq)
                .run();
            this.revision = revision;
            this.snapshotSeq = this.seq;
        } finally {
            this.snapshotting = false;
        }
    }
}

// -------------------------------------------------------------- Worker side

const LIVE_ACTIONS = ["join", "heartbeat", "ops", "leave"] as const;

/** POST /projects/{id}/live/{action}: checks the session and the role, then asks the room. */
export async function liveRoute(ctx: Ctx, action: (typeof LIVE_ACTIONS)[number]): Promise<Response> {
    const auth = await requireAuth(ctx);
    const id = ctx.params.projectId!;
    if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(id)) throw Errors.notFound("PROJECT_NOT_FOUND", "Projet introuvable.");
    const row = await ctx.env.DB.prepare(
        "SELECT m.role FROM project_members m JOIN projects p ON p.id = m.project_id WHERE m.project_id = ?1 AND m.player_id = ?2 AND p.deleted_at IS NULL",
    )
        .bind(id, auth.player.id)
        .first<{ role: string }>();
    if (!row) throw Errors.notFound("PROJECT_NOT_FOUND", "Projet introuvable.");
    if (action === "ops" && ROLE_RANK[row.role]! < ROLE_RANK.editor!) throw Errors.forbidden("PROJECT_ROLE", "Il faut être éditeur du projet pour le modifier.");
    let body: Record<string, unknown> = {};
    if (action === "ops") {
        const bytes = await readLimited(ctx.request, MAX_OPS_PER_REQUEST * MAX_OP_BYTES);
        try {
            body = JSON.parse(new TextDecoder().decode(bytes)) as Record<string, unknown>;
        } catch {
            throw Errors.validation([{ field: "body", message: "JSON invalide" }]);
        }
        if (!Array.isArray(body.ops) || body.ops.length === 0 || body.ops.length > MAX_OPS_PER_REQUEST)
            throw Errors.validation([{ field: "ops", message: `1 à ${MAX_OPS_PER_REQUEST} opérations` }]);
    } else if ((ctx.request.headers.get("content-type") ?? "").startsWith("application/json")) {
        body = await readJsonObject(ctx, 16 * 1024);
    }
    const stub = ctx.env.PROJECT_ROOMS.get(ctx.env.PROJECT_ROOMS.idFromName(id));
    const res = await stub.fetch(`https://room/${action}`, {
        method: "POST",
        headers: {
            "content-type": "application/json",
            "x-project-id": id,
            "x-player-id": auth.player.id,
            "x-player-name": auth.player.username,
            "x-player-avatar": auth.player.avatar,
            "x-player-role": row.role,
        },
        body: JSON.stringify(body),
    });
    const data = (await res.json()) as Record<string, unknown>;
    if (!res.ok || data.error) throw new Error("live room: " + String(data.error ?? res.status));
    return json(ctx, data);
}

/** Asks the room to write its pending live edits as a revision (no-op without a room). */
export async function flushRoom(env: Bindings, projectId: string): Promise<void> {
    const stub = env.PROJECT_ROOMS.get(env.PROJECT_ROOMS.idFromName(projectId));
    await stub.fetch("https://room/flush", { method: "POST", headers: { "x-project-id": projectId } });
}

/** Tells the room of a project that its content changed outside the live session. */
export async function reloadRoom(env: Bindings, projectId: string): Promise<void> {
    const stub = env.PROJECT_ROOMS.get(env.PROJECT_ROOMS.idFromName(projectId));
    await stub.fetch("https://room/reload", { method: "POST", headers: { "x-project-id": projectId } });
}
