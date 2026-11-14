// Collaborative projects (Cosmic Online, phase 2): a level edited by several players.
//
// Roles, from the least to the most rights, every one checked here (never trusted from the
// game): viewer (sees the project and opens the level read-only) < tester (also plays it for
// testing) < editor (also saves new revisions) < admin (also invites, manages editors,
// testers, viewers, join requests and settings) < owner (also manages admins, transfers the
// project, deletes it).
//
// Joining: a key (random 128 bits, shown once to its creator, stored as SHA-256 only, with
// a role, a number of uses, an expiry, revocable), a direct invitation to one player, a join
// request (visibility "request") or, for an "open" project, joining with its join role.
// Content: every save is a new revision; a save made from an older revision is refused
// (409 REVISION_CONFLICT), never merged or overwritten silently.

import { requireAuth } from "./auth";
import { randomBytes, sha256Hex } from "./crypto";
import { ApiError, Errors, json, nowIso, readJsonObject, readLimited, type Ctx } from "./http";
import { playerSummary, type PlayerRow } from "./model";
import { notify } from "./notifications";
import { RULES, rateLimit } from "./ratelimit";
import { Validator, paging } from "./validate";

export const ROLES = ["viewer", "tester", "editor", "admin", "owner"] as const;
export type Role = (typeof ROLES)[number];
const RANK: Record<Role, number> = { viewer: 1, tester: 2, editor: 3, admin: 4, owner: 5 };
const VISIBILITIES = ["private", "invite", "request", "open"] as const;
const JOIN_ROLES = ["editor", "tester", "viewer"] as const;
const INVITE_ROLES = ["admin", "editor", "tester", "viewer"] as const;
const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
/** "CPJ-" + 26 base-32 characters (130 bits, of which 128 random). */
const KEY_RE = /^CPJ-[A-Z2-7]{26}$/;
export const MAX_CONTENT_BYTES = 2 * 1024 * 1024;
const MAX_MEMBERS = 50;
const MAX_OWNED = 30;
const MAX_INVITES = 50;

interface ProjectRow {
    id: string;
    owner_id: string;
    name: string;
    description: string;
    planet: string | null;
    visibility: (typeof VISIBILITIES)[number];
    join_role: (typeof JOIN_ROLES)[number];
    revision: number;
    member_count: number;
    created_at: string;
    updated_at: string;
    deleted_at: string | null;
}

// ------------------------------------------------------------------ helpers

function base32(bytes: Uint8Array): string {
    const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    let bits = 0,
        value = 0,
        out = "";
    for (const b of bytes) {
        value = (value << 8) | b;
        bits += 8;
        while (bits >= 5) {
            out += alphabet[(value >>> (bits - 5)) & 31];
            bits -= 5;
        }
    }
    if (bits > 0) out += alphabet[(value << (5 - bits)) & 31];
    return out;
}

/** A new invitation key (never stored: only its SHA-256 is). */
export function newInviteKey(): string {
    return "CPJ-" + base32(randomBytes(16));
}

/** Accepts the key itself or the link "cosmic://rejoindre/<key>" (spaces and case ignored). */
export function normalizeKey(input: string): string {
    let k = input.trim();
    const m = /^cosmic:\/\/rejoindre\/(.+)$/i.exec(k);
    if (m) k = m[1]!.replace(/\/+$/, "");  // Windows may add a final "/" to the link
    return k.replace(/\s+/g, "").toUpperCase();
}

async function loadProject(ctx: Ctx, id: string): Promise<ProjectRow> {
    if (!ID_RE.test(id)) throw Errors.notFound("PROJECT_NOT_FOUND", "Projet introuvable.");
    const p = await ctx.env.DB.prepare("SELECT * FROM projects WHERE id = ?1 AND deleted_at IS NULL").bind(id).first<ProjectRow>();
    if (!p) throw Errors.notFound("PROJECT_NOT_FOUND", "Projet introuvable.");
    return p;
}

async function roleOf(ctx: Ctx, projectId: string, playerId: string): Promise<Role | null> {
    const r = await ctx.env.DB.prepare("SELECT role FROM project_members WHERE project_id = ?1 AND player_id = ?2")
        .bind(projectId, playerId)
        .first<{ role: Role }>();
    return r?.role ?? null;
}

/** The project and the caller's role; a non-member only sees listed projects (else 404). */
async function access(ctx: Ctx, min: Role | null): Promise<{ me: string; p: ProjectRow; role: Role | null }> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const p = await loadProject(ctx, ctx.params.projectId!);
    const role = await roleOf(ctx, p.id, me);
    if (!role && (p.visibility === "private" || p.visibility === "invite")) throw Errors.notFound("PROJECT_NOT_FOUND", "Projet introuvable.");
    if (min && (!role || RANK[role] < RANK[min])) throw Errors.forbidden("PROJECT_ROLE", roleMessage(min));
    return { me, p, role };
}

function roleMessage(min: Role): string {
    switch (min) {
        case "owner":
            return "Seul le propriétaire du projet peut faire cela.";
        case "admin":
            return "Il faut être administrateur du projet.";
        case "editor":
            return "Il faut être éditeur du projet pour le modifier.";
        case "tester":
            return "Il faut être testeur du projet.";
        default:
            return "Il faut être membre du projet.";
    }
}

async function blockedBetween(ctx: Ctx, a: string, b: string): Promise<boolean> {
    const r = await ctx.env.DB.prepare("SELECT 1 AS x FROM player_blocks WHERE (player_id = ?1 AND blocked_id = ?2) OR (player_id = ?2 AND blocked_id = ?1)")
        .bind(a, b)
        .first();
    return r !== null;
}

function activity(ctx: Ctx, projectId: string, actor: string | null, action: string, details: Record<string, unknown> = {}) {
    return ctx.env.DB.prepare("INSERT INTO project_activity (id, project_id, actor_id, action, details, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)").bind(
        crypto.randomUUID(),
        projectId,
        actor,
        action,
        JSON.stringify(details),
        nowIso(),
    );
}

function projectView(p: ProjectRow, role: Role | null, owner?: PlayerRow | null) {
    return {
        id: p.id,
        name: p.name,
        description: p.description,
        planet: p.planet,
        visibility: p.visibility,
        join_role: p.join_role,
        revision: p.revision,
        member_count: p.member_count,
        created_at: p.created_at,
        updated_at: p.updated_at,
        owner: owner ? playerSummary(owner) : undefined,
        my_role: role,
        // What the caller may do (the server checks each action again).
        can: {
            edit: role !== null && RANK[role] >= RANK.editor,
            test: role !== null && RANK[role] >= RANK.tester,
            manage: role !== null && RANK[role] >= RANK.admin,
            own: role === "owner",
        },
    };
}

async function addMember(ctx: Ctx, p: ProjectRow, player: string, role: Role, by: string | null, how: string) {
    const count = await ctx.env.DB.prepare("SELECT COUNT(*) AS n FROM project_members WHERE project_id = ?1").bind(p.id).first<{ n: number }>();
    if ((count?.n ?? 0) >= MAX_MEMBERS) throw Errors.conflict("PROJECT_FULL", `Ce projet a déjà ${MAX_MEMBERS} membres.`);
    const now = nowIso();
    await ctx.env.DB.batch([
        ctx.env.DB.prepare("INSERT INTO project_members (project_id, player_id, role, joined_at, added_by) VALUES (?1, ?2, ?3, ?4, ?5)").bind(p.id, player, role, now, by),
        ctx.env.DB.prepare("DELETE FROM project_join_requests WHERE project_id = ?1 AND player_id = ?2").bind(p.id, player),
        ctx.env.DB.prepare("UPDATE projects SET member_count = (SELECT COUNT(*) FROM project_members WHERE project_id = ?1), updated_at = ?2 WHERE id = ?1").bind(p.id, now),
        activity(ctx, p.id, player, "member_joined", { role, how, by }),
    ]);
}

async function storeContent(ctx: Ctx, p: ProjectRow, author: string, content: unknown, message: string, base: number) {
    if (content === null || typeof content !== "object" || Array.isArray(content))
        throw Errors.validation([{ field: "content", message: "niveau attendu (objet JSON)" }]);
    const text = JSON.stringify(content);
    if (text.length > MAX_CONTENT_BYTES) throw new ApiError(413, "PAYLOAD_TOO_LARGE", "Niveau trop volumineux pour un projet (2 Mo au plus).");
    const revision = base + 1;
    const key = `projects/${p.id}/r${revision}.json`;
    const hash = await sha256Hex(text);
    const now = nowIso();
    // The revision is claimed first: of two saves made from the same revision, only one wins.
    const claim = await ctx.env.DB.prepare("UPDATE projects SET revision = ?2, updated_at = ?3 WHERE id = ?1 AND revision = ?4 AND deleted_at IS NULL")
        .bind(p.id, revision, now, base)
        .run();
    if (claim.meta.changes !== 1) {
        const cur = await ctx.env.DB.prepare(
            `SELECT r.revision, r.created_at, pl.username FROM projects p LEFT JOIN project_revisions r ON r.project_id = p.id AND r.revision = p.revision
             LEFT JOIN players pl ON pl.id = r.author_id WHERE p.id = ?1`,
        )
            .bind(p.id)
            .first<{ revision: number | null; created_at: string | null; username: string | null }>();
        throw Errors.conflict("REVISION_CONFLICT", "Le projet a été modifié entre-temps : votre version n'a pas été enregistrée par-dessus.", {
            current_revision: cur?.revision ?? 0,
            saved_by: cur?.username ?? null,
            saved_at: cur?.created_at ?? null,
        });
    }
    try {
        await ctx.env.STORAGE.put(key, text, { httpMetadata: { contentType: "application/json" } });
        await ctx.env.DB.batch([
            ctx.env.DB.prepare(
                "INSERT INTO project_revisions (project_id, revision, content_key, content_hash, size, author_id, message, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)",
            ).bind(p.id, revision, key, hash, text.length, author, message, now),
            activity(ctx, p.id, author, "saved", { revision, message }),
        ]);
    } catch (e) {
        // Give the revision back if the file could not be stored.
        await ctx.env.DB.prepare("UPDATE projects SET revision = ?2 WHERE id = ?1 AND revision = ?3").bind(p.id, base, revision).run();
        throw e;
    }
    return { revision, content_hash: hash, size: text.length, saved_at: now };
}

// ------------------------------------------------------------------- routes

/** POST /projects {name, description?, visibility?, join_role?, planet?, content?} */
export async function createProject(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.projectCreate, "player:" + me);
    const body = await readJsonObject(ctx, MAX_CONTENT_BYTES + 8 * 1024);
    const v = new Validator(body, ["name", "description", "visibility", "join_role", "planet", "content", "message"]);
    const name = v.str("name", { min: 3, max: 60 })!;
    const description = v.str("description", { max: 500, optional: true, multiline: true }) ?? "";
    const visibility = v.oneOf("visibility", VISIBILITIES, { optional: true }) ?? "private";
    const joinRole = v.oneOf("join_role", JOIN_ROLES, { optional: true }) ?? "viewer";
    const planet = v.str("planet", { max: 32, pattern: /^[a-z0-9_]+$/, optional: true }) ?? null;
    const message = v.str("message", { max: 200, optional: true }) ?? "";
    v.done();
    const owned = await ctx.env.DB.prepare("SELECT COUNT(*) AS n FROM projects WHERE owner_id = ?1 AND deleted_at IS NULL").bind(me).first<{ n: number }>();
    if ((owned?.n ?? 0) >= MAX_OWNED) throw Errors.conflict("PROJECT_LIMIT", `Vous possédez déjà ${MAX_OWNED} projets.`);
    const id = crypto.randomUUID();
    const now = nowIso();
    await ctx.env.DB.batch([
        ctx.env.DB.prepare(
            `INSERT INTO projects (id, owner_id, name, description, planet, visibility, join_role, revision, member_count, created_at, updated_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, 0, 1, ?8, ?8)`,
        ).bind(id, me, name, description, planet, visibility, joinRole, now),
        ctx.env.DB.prepare("INSERT INTO project_members (project_id, player_id, role, joined_at, added_by) VALUES (?1, ?2, 'owner', ?3, NULL)").bind(id, me, now),
        activity(ctx, id, me, "created", { name }),
    ]);
    const p = await loadProject(ctx, id);
    let saved = null;
    if (body.content !== undefined) saved = await storeContent(ctx, p, me, body.content, message || "Version initiale", 0);
    const fresh = await loadProject(ctx, id);
    return json(ctx, { project: projectView(fresh, "owner"), saved }, { status: 201 });
}

/** GET /projects : the caller's projects. */
export async function myProjects(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const rows = await ctx.env.DB.prepare(
        `SELECT p.*, m.role AS my_role, o.username AS o_username, o.avatar AS o_avatar, o.id AS o_id
         FROM project_members m JOIN projects p ON p.id = m.project_id JOIN players o ON o.id = p.owner_id
         WHERE m.player_id = ?1 AND p.deleted_at IS NULL ORDER BY p.updated_at DESC LIMIT 100`,
    )
        .bind(auth.player.id)
        .all<ProjectRow & { my_role: Role; o_username: string; o_avatar: string; o_id: string }>();
    return json(ctx, {
        projects: rows.results.map((r) => ({ ...projectView(r, r.my_role), owner: { id: r.o_id, username: r.o_username, avatar: r.o_avatar } })),
    });
}

/** GET /projects/discover?q= : projects open to requests or to everyone. */
export async function discoverProjects(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const q = (ctx.url.searchParams.get("q") ?? "").trim().toLowerCase().slice(0, 60);
    const { limit, offset } = paging(ctx.url, 20, 50);
    const args: unknown[] = [me];
    let filter = "";
    if (q) {
        args.push("%" + q.replace(/[\\%_]/g, (c) => "\\" + c) + "%");
        filter = ` AND (lower(p.name) LIKE ?${args.length} ESCAPE '\\' OR lower(o.username) LIKE ?${args.length} ESCAPE '\\')`;
    }
    const where = `p.deleted_at IS NULL AND p.visibility IN ('request', 'open') AND o.account_status = 'active'
      AND NOT EXISTS (SELECT 1 FROM player_blocks b WHERE (b.player_id = ?1 AND b.blocked_id = p.owner_id) OR (b.player_id = p.owner_id AND b.blocked_id = ?1))${filter}`;
    const total = await ctx.env.DB.prepare(`SELECT COUNT(*) AS n FROM projects p JOIN players o ON o.id = p.owner_id WHERE ${where}`)
        .bind(...args)
        .first<{ n: number }>();
    args.push(limit, offset);
    const rows = await ctx.env.DB.prepare(
        `SELECT p.*, o.id AS o_id, o.username AS o_username, o.avatar AS o_avatar,
                (SELECT role FROM project_members m WHERE m.project_id = p.id AND m.player_id = ?1) AS my_role,
                EXISTS (SELECT 1 FROM project_join_requests r WHERE r.project_id = p.id AND r.player_id = ?1) AS requested
         FROM projects p JOIN players o ON o.id = p.owner_id WHERE ${where} ORDER BY p.updated_at DESC LIMIT ?${args.length - 1} OFFSET ?${args.length}`,
    )
        .bind(...args)
        .all<ProjectRow & { o_id: string; o_username: string; o_avatar: string; my_role: Role | null; requested: number }>();
    const n = total?.n ?? 0;
    return json(
        ctx,
        {
            projects: rows.results.map((r) => ({
                ...projectView(r, r.my_role),
                owner: { id: r.o_id, username: r.o_username, avatar: r.o_avatar },
                requested: r.requested === 1,
            })),
        },
        { meta: { limit, offset, total: n, next_offset: offset + limit < n ? offset + limit : null } },
    );
}

/** GET /projects/{id} */
export async function getProject(ctx: Ctx): Promise<Response> {
    const { me, p, role } = await access(ctx, null);
    const owner = await ctx.env.DB.prepare("SELECT * FROM players WHERE id = ?1").bind(p.owner_id).first<PlayerRow>();
    const requested = role ? false : (await ctx.env.DB.prepare("SELECT 1 AS x FROM project_join_requests WHERE project_id = ?1 AND player_id = ?2").bind(p.id, me).first()) !== null;
    let last = null;
    if (role && p.revision > 0)
        last = await ctx.env.DB.prepare(
            `SELECT r.revision, r.created_at, r.message, r.size, r.content_hash, pl.username AS author FROM project_revisions r LEFT JOIN players pl ON pl.id = r.author_id
             WHERE r.project_id = ?1 AND r.revision = ?2`,
        )
            .bind(p.id, p.revision)
            .first();
    return json(ctx, { project: { ...projectView(p, role, owner), requested, last_revision: last } });
}

/** PATCH /projects/{id} {name?, description?, visibility?, join_role?, planet?} : admins. */
export async function updateProject(ctx: Ctx): Promise<Response> {
    const { me, p, role } = await access(ctx, "admin");
    const v = new Validator(await readJsonObject(ctx), ["name", "description", "visibility", "join_role", "planet"]);
    const name = v.str("name", { min: 3, max: 60, optional: true });
    const description = v.str("description", { max: 500, optional: true, multiline: true });
    const visibility = v.oneOf("visibility", VISIBILITIES, { optional: true });
    const joinRole = v.oneOf("join_role", JOIN_ROLES, { optional: true });
    const planet = v.str("planet", { max: 32, pattern: /^[a-z0-9_]+$/, optional: true });
    v.done();
    const next = {
        name: name ?? p.name,
        description: description ?? p.description,
        visibility: visibility ?? p.visibility,
        join_role: joinRole ?? p.join_role,
        planet: planet ?? p.planet,
    };
    await ctx.env.DB.batch([
        ctx.env.DB.prepare("UPDATE projects SET name = ?2, description = ?3, visibility = ?4, join_role = ?5, planet = ?6, updated_at = ?7 WHERE id = ?1").bind(
            p.id,
            next.name,
            next.description,
            next.visibility,
            next.join_role,
            next.planet,
            nowIso(),
        ),
        activity(ctx, p.id, me, "settings", { changed: Object.keys(next).filter((k) => (next as Record<string, unknown>)[k] !== (p as unknown as Record<string, unknown>)[k]) }),
    ]);
    // Leaving the "request" mode drops the pending requests.
    if (next.visibility !== "request") await ctx.env.DB.prepare("DELETE FROM project_join_requests WHERE project_id = ?1").bind(p.id).run();
    return json(ctx, { project: projectView(await loadProject(ctx, p.id), role) });
}

/** DELETE /projects/{id} : the owner deletes the project and its files. */
export async function deleteProject(ctx: Ctx): Promise<Response> {
    const { me, p } = await access(ctx, "owner");
    const keys = await ctx.env.DB.prepare("SELECT content_key FROM project_revisions WHERE project_id = ?1").bind(p.id).all<{ content_key: string }>();
    for (let i = 0; i < keys.results.length; i += 500) await ctx.env.STORAGE.delete(keys.results.slice(i, i + 500).map((k) => k.content_key));
    await ctx.env.DB.prepare("DELETE FROM projects WHERE id = ?1").bind(p.id).run();
    void me;
    return json(ctx, { deleted: true });
}

// ------------------------------------------------------------------ members

/** GET /projects/{id}/members : members only. */
export async function listMembers(ctx: Ctx): Promise<Response> {
    const { p } = await access(ctx, "viewer");
    const rows = await ctx.env.DB.prepare(
        `SELECT pl.*, m.role, m.joined_at FROM project_members m JOIN players pl ON pl.id = m.player_id WHERE m.project_id = ?1
         ORDER BY CASE m.role WHEN 'owner' THEN 0 WHEN 'admin' THEN 1 WHEN 'editor' THEN 2 WHEN 'tester' THEN 3 ELSE 4 END, m.joined_at`,
    )
        .bind(p.id)
        .all<PlayerRow & { role: Role; joined_at: string }>();
    return json(ctx, { members: rows.results.map((r) => ({ player: playerSummary(r), role: r.role, joined_at: r.joined_at })) });
}

/** Who may change whose role: the owner everyone else; an admin the editors, testers, viewers. */
function canManage(actor: Role, target: Role, newRole?: Role): boolean {
    if (target === "owner") return false;
    if (actor === "owner") return newRole !== "owner";
    if (actor === "admin") return RANK[target] < RANK.admin && (newRole === undefined || RANK[newRole] < RANK.admin);
    return false;
}

/** PATCH /projects/{id}/members/{playerId} {role} */
export async function setRole(ctx: Ctx): Promise<Response> {
    const { me, p, role } = await access(ctx, "admin");
    const target = ctx.params.playerId!;
    const v = new Validator(await readJsonObject(ctx), ["role"]);
    const newRole = v.oneOf("role", INVITE_ROLES)!;
    v.done();
    const current = await roleOf(ctx, p.id, target);
    if (!current) throw Errors.notFound("MEMBER_NOT_FOUND", "Ce joueur n'est pas membre du projet.");
    if (!canManage(role!, current, newRole)) throw Errors.forbidden("PROJECT_ROLE", "Vous ne pouvez pas donner ce rôle à ce membre.");
    if (current === newRole) return json(ctx, { role: newRole });
    await ctx.env.DB.batch([
        ctx.env.DB.prepare("UPDATE project_members SET role = ?3 WHERE project_id = ?1 AND player_id = ?2").bind(p.id, target, newRole),
        activity(ctx, p.id, me, "role_changed", { player: target, from: current, to: newRole }),
    ]);
    await notify(ctx, target, "project_role", { actor: me, data: { project: p.name, project_id: p.id, role: roleLabel(newRole) } });
    return json(ctx, { role: newRole });
}

export function roleLabel(r: Role): string {
    return { owner: "propriétaire", admin: "administrateur", editor: "éditeur", tester: "testeur", viewer: "lecteur" }[r];
}

/** DELETE /projects/{id}/members/{playerId} : leave (oneself) or remove (admins). */
export async function removeMember(ctx: Ctx): Promise<Response> {
    const { me, p, role } = await access(ctx, "viewer");
    const target = ctx.params.playerId!;
    const current = await roleOf(ctx, p.id, target);
    if (!current) throw Errors.notFound("MEMBER_NOT_FOUND", "Ce joueur n'est pas membre du projet.");
    if (target === me) {
        if (current === "owner") throw Errors.conflict("OWNER_CANNOT_LEAVE", "Transférez d'abord le projet à un autre membre, ou supprimez-le.");
    } else if (!canManage(role!, current)) {
        throw Errors.forbidden("PROJECT_ROLE", "Vous ne pouvez pas retirer ce membre.");
    }
    await ctx.env.DB.batch([
        ctx.env.DB.prepare("DELETE FROM project_members WHERE project_id = ?1 AND player_id = ?2").bind(p.id, target),
        ctx.env.DB.prepare("UPDATE projects SET member_count = (SELECT COUNT(*) FROM project_members WHERE project_id = ?1) WHERE id = ?1").bind(p.id),
        activity(ctx, p.id, me, target === me ? "member_left" : "member_removed", { player: target }),
    ]);
    return json(ctx, { removed: true });
}

/** POST /projects/{id}/transfer {player_id} : the owner hands the project to a member (and becomes admin). */
export async function transferProject(ctx: Ctx): Promise<Response> {
    const { me, p } = await access(ctx, "owner");
    const v = new Validator(await readJsonObject(ctx), ["player_id"]);
    const target = v.str("player_id", { max: 36, pattern: ID_RE })!;
    v.done();
    if (target === me) throw Errors.validation([{ field: "player_id", message: "vous êtes déjà propriétaire" }]);
    if (!(await roleOf(ctx, p.id, target))) throw Errors.notFound("MEMBER_NOT_FOUND", "Ce joueur n'est pas membre du projet.");
    await ctx.env.DB.batch([
        ctx.env.DB.prepare("UPDATE project_members SET role = 'admin' WHERE project_id = ?1 AND player_id = ?2").bind(p.id, me),
        ctx.env.DB.prepare("UPDATE project_members SET role = 'owner' WHERE project_id = ?1 AND player_id = ?2").bind(p.id, target),
        ctx.env.DB.prepare("UPDATE projects SET owner_id = ?2, updated_at = ?3 WHERE id = ?1").bind(p.id, target, nowIso()),
        activity(ctx, p.id, me, "transferred", { to: target }),
    ]);
    await notify(ctx, target, "project_role", { actor: me, data: { project: p.name, project_id: p.id, role: roleLabel("owner") } });
    return json(ctx, { owner: target });
}

// -------------------------------------------------------------- invitations

/** POST /projects/{id}/invites {role, max_uses?, expires_hours?, player_id?} : admins. The key is in this answer only. */
export async function createInvite(ctx: Ctx): Promise<Response> {
    const { me, p, role } = await access(ctx, "admin");
    await rateLimit(ctx, RULES.projectInvite, "player:" + me);
    const v = new Validator(await readJsonObject(ctx), ["role", "max_uses", "expires_hours", "player_id"]);
    const inviteRole = v.oneOf("role", INVITE_ROLES)!;
    const maxUses = v.int("max_uses", { min: 1, max: 100, optional: true }) ?? null;
    const hours = v.int("expires_hours", { min: 1, max: 24 * 30, optional: true }) ?? 24 * 7;
    const target = v.str("player_id", { max: 36, pattern: ID_RE, optional: true }) ?? null;
    v.done();
    if (inviteRole === "admin" && role !== "owner") throw Errors.forbidden("PROJECT_ROLE", "Seul le propriétaire peut inviter un administrateur.");
    const open = await ctx.env.DB.prepare(
        "SELECT COUNT(*) AS n FROM project_invites WHERE project_id = ?1 AND revoked_at IS NULL AND (expires_at IS NULL OR expires_at > ?2)",
    )
        .bind(p.id, nowIso())
        .first<{ n: number }>();
    if ((open?.n ?? 0) >= MAX_INVITES) throw Errors.conflict("INVITE_LIMIT", "Trop d'invitations actives : révoquez-en.");
    const id = crypto.randomUUID();
    const expires = new Date(Date.now() + hours * 3600 * 1000).toISOString();
    if (target) {
        if (await roleOf(ctx, p.id, target)) throw Errors.conflict("ALREADY_MEMBER", "Ce joueur est déjà membre du projet.");
        if (await blockedBetween(ctx, me, target)) throw Errors.forbidden("BLOCKED", "Vous ne pouvez pas inviter ce joueur.");
        const exists = await ctx.env.DB.prepare("SELECT 1 AS x FROM players WHERE id = ?1 AND account_status = 'active'").bind(target).first();
        if (!exists) throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
        await ctx.env.DB.batch([
            ctx.env.DB.prepare(
                `INSERT INTO project_invites (id, project_id, key_hash, key_hint, target_player_id, role, max_uses, uses, expires_at, created_by, created_at)
                 VALUES (?1, ?2, NULL, NULL, ?3, ?4, 1, 0, ?5, ?6, ?7)`,
            ).bind(id, p.id, target, inviteRole, expires, me, nowIso()),
            activity(ctx, p.id, me, "invited", { player: target, role: inviteRole }),
        ]);
        await notify(ctx, target, "project_invite", { actor: me, data: { project: p.name, project_id: p.id, invite_id: id, role: roleLabel(inviteRole) } });
        return json(ctx, { invite: { id, role: inviteRole, target_player_id: target, expires_at: expires } }, { status: 201 });
    }
    const key = newInviteKey();
    await ctx.env.DB.batch([
        ctx.env.DB.prepare(
            `INSERT INTO project_invites (id, project_id, key_hash, key_hint, target_player_id, role, max_uses, uses, expires_at, created_by, created_at)
             VALUES (?1, ?2, ?3, ?4, NULL, ?5, ?6, 0, ?7, ?8, ?9)`,
        ).bind(id, p.id, await sha256Hex(key), key.slice(-4), inviteRole, maxUses, expires, me, nowIso()),
        activity(ctx, p.id, me, "invite_created", { role: inviteRole, max_uses: maxUses, expires_at: expires }),
    ]);
    return json(
        ctx,
        { invite: { id, role: inviteRole, max_uses: maxUses, uses: 0, expires_at: expires, key_hint: key.slice(-4) }, key, link: "cosmic://rejoindre/" + key },
        { status: 201 },
    );
}

/** GET /projects/{id}/invites : admins (keys are never shown again). */
export async function listInvites(ctx: Ctx): Promise<Response> {
    const { p } = await access(ctx, "admin");
    const rows = await ctx.env.DB.prepare(
        `SELECT i.*, c.username AS creator, t.username AS target_name, t.avatar AS target_avatar FROM project_invites i
         LEFT JOIN players c ON c.id = i.created_by LEFT JOIN players t ON t.id = i.target_player_id
         WHERE i.project_id = ?1 ORDER BY i.created_at DESC LIMIT 100`,
    )
        .bind(p.id)
        .all<Record<string, unknown>>();
    const now = nowIso();
    return json(ctx, {
        invites: rows.results.map((i) => ({
            id: i.id,
            role: i.role,
            key_hint: i.key_hint,
            target: i.target_player_id ? { id: i.target_player_id, username: i.target_name, avatar: i.target_avatar } : null,
            max_uses: i.max_uses,
            uses: i.uses,
            expires_at: i.expires_at,
            created_by: i.creator,
            created_at: i.created_at,
            status: i.revoked_at
                ? "revoked"
                : i.expires_at && (i.expires_at as string) <= now
                  ? "expired"
                  : i.max_uses !== null && (i.uses as number) >= (i.max_uses as number)
                    ? "used"
                    : "active",
        })),
    });
}

/** DELETE /projects/{id}/invites/{inviteId} : revoke. */
export async function revokeInvite(ctx: Ctx): Promise<Response> {
    const { me, p } = await access(ctx, "admin");
    const r = await ctx.env.DB.prepare("UPDATE project_invites SET revoked_at = COALESCE(revoked_at, ?3) WHERE id = ?1 AND project_id = ?2")
        .bind(ctx.params.inviteId!, p.id, nowIso())
        .run();
    if (r.meta.changes !== 1) throw Errors.notFound("INVITE_NOT_FOUND", "Invitation introuvable.");
    await activity(ctx, p.id, me, "invite_revoked", { invite: ctx.params.inviteId }).run();
    return json(ctx, { revoked: true });
}

/** Uses an invitation (counted atomically: never more uses than allowed). */
async function useInvite(ctx: Ctx, invite: { id: string; project_id: string; role: Role }, me: string, how: string) {
    const p = await loadProject(ctx, invite.project_id);
    if (await roleOf(ctx, p.id, me)) throw Errors.conflict("ALREADY_MEMBER", "Vous êtes déjà membre de ce projet.");
    if (await blockedBetween(ctx, me, p.owner_id)) throw Errors.forbidden("BLOCKED", "Vous ne pouvez pas rejoindre ce projet.");
    const take = await ctx.env.DB.prepare(
        `UPDATE project_invites SET uses = uses + 1 WHERE id = ?1 AND revoked_at IS NULL AND (expires_at IS NULL OR expires_at > ?2)
           AND (max_uses IS NULL OR uses < max_uses)`,
    )
        .bind(invite.id, nowIso())
        .run();
    if (take.meta.changes !== 1) throw Errors.conflict("INVITE_INVALID", "Cette invitation n'est plus valable (expirée, épuisée ou révoquée).");
    try {
        await addMember(ctx, p, me, invite.role, null, how);
    } catch (e) {
        await ctx.env.DB.prepare("UPDATE project_invites SET uses = uses - 1 WHERE id = ?1").bind(invite.id).run();
        throw e;
    }
    return p;
}

/** POST /projects/join {key} : a key or a "cosmic://rejoindre/…" link. */
export async function joinWithKey(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.projectJoin, "player:" + me);
    const v = new Validator(await readJsonObject(ctx), ["key"]);
    const raw = v.str("key", { min: 4, max: 200 })!;
    v.done();
    const key = normalizeKey(raw);
    // Same answer for an unknown key and a malformed one: nothing to learn by trying keys.
    const invalid = Errors.conflict("INVITE_INVALID", "Clé d'invitation inconnue ou plus valable.");
    if (!KEY_RE.test(key)) throw invalid;
    const invite = await ctx.env.DB.prepare(
        "SELECT i.id, i.project_id, i.role FROM project_invites i JOIN projects p ON p.id = i.project_id WHERE i.key_hash = ?1 AND p.deleted_at IS NULL",
    )
        .bind(await sha256Hex(key))
        .first<{ id: string; project_id: string; role: Role }>();
    if (!invite) throw invalid;
    const p = await useInvite(ctx, invite, me, "key");
    return json(ctx, { project: projectView(await loadProject(ctx, p.id), invite.role) });
}

/** GET /projects/invitations : direct invitations waiting for the caller. */
export async function myInvitations(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const rows = await ctx.env.DB.prepare(
        `SELECT i.id, i.role, i.expires_at, i.created_at, p.id AS project_id, p.name, p.description, c.id AS c_id, c.username AS c_name, c.avatar AS c_avatar
         FROM project_invites i JOIN projects p ON p.id = i.project_id LEFT JOIN players c ON c.id = i.created_by
         WHERE i.target_player_id = ?1 AND i.revoked_at IS NULL AND i.uses = 0 AND (i.expires_at IS NULL OR i.expires_at > ?2) AND p.deleted_at IS NULL
           AND NOT EXISTS (SELECT 1 FROM project_members m WHERE m.project_id = p.id AND m.player_id = ?1)
         ORDER BY i.created_at DESC`,
    )
        .bind(auth.player.id, nowIso())
        .all<Record<string, unknown>>();
    return json(ctx, {
        invitations: rows.results.map((r) => ({
            id: r.id,
            role: r.role,
            expires_at: r.expires_at,
            created_at: r.created_at,
            project: { id: r.project_id, name: r.name, description: r.description },
            invited_by: r.c_id ? { id: r.c_id, username: r.c_name, avatar: r.c_avatar } : null,
        })),
    });
}

/** POST /projects/invitations/{inviteId}/accept · DELETE /projects/invitations/{inviteId} (decline) */
export async function answerInvitation(ctx: Ctx, accept: boolean): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const invite = await ctx.env.DB.prepare("SELECT id, project_id, role FROM project_invites WHERE id = ?1 AND target_player_id = ?2")
        .bind(ctx.params.inviteId!, me)
        .first<{ id: string; project_id: string; role: Role }>();
    if (!invite) throw Errors.notFound("INVITE_NOT_FOUND", "Invitation introuvable.");
    if (!accept) {
        await ctx.env.DB.prepare("UPDATE project_invites SET revoked_at = COALESCE(revoked_at, ?2) WHERE id = ?1").bind(invite.id, nowIso()).run();
        return json(ctx, { declined: true });
    }
    const p = await useInvite(ctx, invite, me, "invitation");
    return json(ctx, { project: projectView(await loadProject(ctx, p.id), invite.role) });
}

// --------------------------------------------------- open projects, requests

/** POST /projects/{id}/join {message?} : joins an open project, or asks to join a "request" one. */
export async function requestJoin(ctx: Ctx): Promise<Response> {
    const { me, p, role } = await access(ctx, null);
    if (role) throw Errors.conflict("ALREADY_MEMBER", "Vous êtes déjà membre de ce projet.");
    await rateLimit(ctx, RULES.projectJoin, "player:" + me);
    if (await blockedBetween(ctx, me, p.owner_id)) throw Errors.forbidden("BLOCKED", "Vous ne pouvez pas rejoindre ce projet.");
    let message = "";
    if ((ctx.request.headers.get("content-type") ?? "").startsWith("application/json")) {
        const v = new Validator(await readJsonObject(ctx), ["message"]);
        message = v.str("message", { max: 300, optional: true }) ?? "";
        v.done();
    }
    if (p.visibility === "open") {
        await addMember(ctx, p, me, p.join_role, null, "open");
        return json(ctx, { joined: true, role: p.join_role });
    }
    await ctx.env.DB.batch([
        ctx.env.DB.prepare(
            "INSERT INTO project_join_requests (project_id, player_id, message, created_at) VALUES (?1, ?2, ?3, ?4) ON CONFLICT (project_id, player_id) DO UPDATE SET message = excluded.message",
        ).bind(p.id, me, message, nowIso()),
        activity(ctx, p.id, me, "join_requested", {}),
    ]);
    return json(ctx, { requested: true }, { status: 202 });
}

/** DELETE /projects/{id}/join : withdraws one's request. */
export async function cancelJoinRequest(ctx: Ctx): Promise<Response> {
    const { me, p } = await access(ctx, null);
    await ctx.env.DB.prepare("DELETE FROM project_join_requests WHERE project_id = ?1 AND player_id = ?2").bind(p.id, me).run();
    return json(ctx, { requested: false });
}

/** GET /projects/{id}/requests : admins. */
export async function listRequests(ctx: Ctx): Promise<Response> {
    const { p } = await access(ctx, "admin");
    const rows = await ctx.env.DB.prepare(
        "SELECT pl.*, r.message, r.created_at AS asked_at FROM project_join_requests r JOIN players pl ON pl.id = r.player_id WHERE r.project_id = ?1 ORDER BY r.created_at",
    )
        .bind(p.id)
        .all<PlayerRow & { message: string; asked_at: string }>();
    return json(ctx, { requests: rows.results.map((r) => ({ player: playerSummary(r), message: r.message, created_at: r.asked_at })) });
}

/** POST /projects/{id}/requests/{playerId} {accept, role?} : admins. */
export async function answerRequest(ctx: Ctx): Promise<Response> {
    const { me, p, role } = await access(ctx, "admin");
    const target = ctx.params.playerId!;
    const v = new Validator(await readJsonObject(ctx), ["accept", "role"]);
    const accept = v.bool("accept")!;
    const newRole = v.oneOf("role", JOIN_ROLES, { optional: true }) ?? p.join_role;
    v.done();
    const req = await ctx.env.DB.prepare("SELECT 1 AS x FROM project_join_requests WHERE project_id = ?1 AND player_id = ?2").bind(p.id, target).first();
    if (!req) throw Errors.notFound("REQUEST_NOT_FOUND", "Demande introuvable.");
    if (!accept) {
        await ctx.env.DB.batch([
            ctx.env.DB.prepare("DELETE FROM project_join_requests WHERE project_id = ?1 AND player_id = ?2").bind(p.id, target),
            activity(ctx, p.id, me, "request_refused", { player: target }),
        ]);
        return json(ctx, { accepted: false });
    }
    if (!canManage(role!, "viewer", newRole)) throw Errors.forbidden("PROJECT_ROLE", "Vous ne pouvez pas donner ce rôle.");
    await addMember(ctx, p, target, newRole, me, "request");
    await notify(ctx, target, "project_role", { actor: me, data: { project: p.name, project_id: p.id, role: roleLabel(newRole) } });
    return json(ctx, { accepted: true, role: newRole });
}

// ------------------------------------------------------------------ content

/** GET /projects/{id}/content?revision= : the level (members). */
export async function getContent(ctx: Ctx): Promise<Response> {
    const { p } = await access(ctx, "viewer");
    const wanted = ctx.url.searchParams.get("revision");
    const rev = wanted === null ? p.revision : Number.parseInt(wanted, 10);
    if (!Number.isInteger(rev) || rev < 1 || rev > p.revision) throw Errors.notFound("REVISION_NOT_FOUND", "Révision introuvable.");
    const r = await ctx.env.DB.prepare("SELECT content_key, content_hash FROM project_revisions WHERE project_id = ?1 AND revision = ?2")
        .bind(p.id, rev)
        .first<{ content_key: string; content_hash: string }>();
    const obj = r ? await ctx.env.STORAGE.get(r.content_key) : null;
    if (!r || !obj) throw Errors.notFound("REVISION_NOT_FOUND", "Révision introuvable.");
    return new Response(obj.body, {
        status: 200,
        headers: {
            "content-type": "application/json; charset=utf-8",
            "cache-control": "private, no-store",
            "x-project-revision": String(rev),
            "x-content-sha256": r.content_hash,
            "x-request-id": ctx.requestId,
        },
    });
}

/** PUT /projects/{id}/content {base_revision, content, message?} : editors. */
export async function putContent(ctx: Ctx): Promise<Response> {
    const { me, p } = await access(ctx, "editor");
    await rateLimit(ctx, RULES.projectSave, "player:" + me);
    const bytes = await readLimited(ctx.request, MAX_CONTENT_BYTES + 8 * 1024);
    let body: Record<string, unknown>;
    try {
        const parsed = JSON.parse(new TextDecoder().decode(bytes));
        if (parsed === null || typeof parsed !== "object" || Array.isArray(parsed)) throw new Error("not an object");
        body = parsed as Record<string, unknown>;
    } catch {
        throw Errors.validation([{ field: "body", message: "JSON invalide" }]);
    }
    const v = new Validator(body, ["base_revision", "content", "message"]);
    const base = v.int("base_revision", { min: 0, max: 1_000_000 })!;
    const message = v.str("message", { max: 200, optional: true }) ?? "";
    v.done();
    const saved = await storeContent(ctx, p, me, body.content, message, base);
    return json(ctx, { saved }, { status: 201 });
}

/** GET /projects/{id}/revisions : members. */
export async function listRevisions(ctx: Ctx): Promise<Response> {
    const { p } = await access(ctx, "viewer");
    const { limit, offset } = paging(ctx.url, 30, 100);
    const rows = await ctx.env.DB.prepare(
        `SELECT r.revision, r.content_hash, r.size, r.message, r.created_at, pl.id AS a_id, pl.username AS a_name, pl.avatar AS a_avatar
         FROM project_revisions r LEFT JOIN players pl ON pl.id = r.author_id WHERE r.project_id = ?1 ORDER BY r.revision DESC LIMIT ?2 OFFSET ?3`,
    )
        .bind(p.id, limit, offset)
        .all<Record<string, unknown>>();
    return json(
        ctx,
        {
            revisions: rows.results.map((r) => ({
                revision: r.revision,
                content_hash: r.content_hash,
                size: r.size,
                message: r.message,
                created_at: r.created_at,
                author: r.a_id ? { id: r.a_id, username: r.a_name, avatar: r.a_avatar } : null,
            })),
        },
        { meta: { limit, offset, total: p.revision, next_offset: offset + limit < p.revision ? offset + limit : null } },
    );
}

/** GET /projects/{id}/activity : members. */
export async function listActivity(ctx: Ctx): Promise<Response> {
    const { p } = await access(ctx, "viewer");
    const { limit, offset } = paging(ctx.url, 40, 100);
    const rows = await ctx.env.DB.prepare(
        `SELECT a.action, a.details, a.created_at, pl.id AS a_id, pl.username AS a_name, pl.avatar AS a_avatar
         FROM project_activity a LEFT JOIN players pl ON pl.id = a.actor_id WHERE a.project_id = ?1 ORDER BY a.created_at DESC LIMIT ?2 OFFSET ?3`,
    )
        .bind(p.id, limit, offset)
        .all<Record<string, unknown>>();
    // Names of the players mentioned in the details (member added, role changed...).
    const ids = new Set<string>();
    const parsed = rows.results.map((r) => {
        let d: Record<string, unknown> = {};
        try {
            d = JSON.parse(r.details as string) as Record<string, unknown>;
        } catch {
            d = {};
        }
        for (const k of ["player", "to", "by"]) if (typeof d[k] === "string") ids.add(d[k] as string);
        return { r, d };
    });
    const names = new Map<string, string>();
    if (ids.size) {
        const list = [...ids];
        const rs = await ctx.env.DB.prepare(`SELECT id, username FROM players WHERE id IN (${list.map((_, i) => "?" + (i + 1)).join(",")})`)
            .bind(...list)
            .all<{ id: string; username: string }>();
        for (const x of rs.results) names.set(x.id, x.username);
    }
    return json(ctx, {
        activity: parsed.map(({ r, d }) => ({
            action: r.action,
            details: { ...d, player_name: names.get(d.player as string) ?? null, to_name: names.get(d.to as string) ?? null },
            created_at: r.created_at,
            actor: r.a_id ? { id: r.a_id, username: r.a_name, avatar: r.a_avatar } : null,
        })),
    });
}

/** Every R2 key of the projects owned by `playerId` (account deletion). */
export async function ownedProjectKeys(ctx: Ctx, playerId: string): Promise<string[]> {
    const rows = await ctx.env.DB.prepare(
        "SELECT r.content_key FROM project_revisions r JOIN projects p ON p.id = r.project_id WHERE p.owner_id = ?1",
    )
        .bind(playerId)
        .all<{ content_key: string }>();
    return rows.results.map((r) => r.content_key);
}
