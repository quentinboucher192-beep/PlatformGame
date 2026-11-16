// The administrator role of an account (2.40): the in-game ADMINISTRATION screen. Every route
// checks the role of the authenticated session on each request (never a flag sent by the game,
// never a key shipped with it); for any other account the routes do not exist (the same 404 as
// an unknown route). Each change is written to audit_log, which the Journal tab reads back
// without the IP hash.
//
// The older operator routes of admin.ts (ADMIN_TOKEN) are separate and unchanged.

import { audit, requireAuth, type AuthInfo } from "./auth";
import { PRESENCE_TIMEOUT_MS, presenceOf } from "./friends";
import { ApiError, Errors, json, readJsonObject, type Ctx } from "./http";
import { avatarUrl, visibleCountry, type PlayerRow } from "./model";
import { RULES, rateLimit } from "./ratelimit";
import { Validator, likePrefix, normalizeUsername, paging, queryEnum, queryString } from "./validate";

export type Role = "player" | "admin";

/** Actions shown by the Journal (audit_log.action), by kind. */
export const ADMIN_ACTIONS = {
    role: ["admin_role_granted", "admin_role_revoked"],
    refused: ["admin_refused", "backup_refused"],
    // Backups (ADMINISTRATION › Sauvegardes, backups.ts): copies, downloads, imports. Never a passphrase.
    backup: [
        "backup_created",
        "backup_failed",
        "backup_downloaded",
        "backup_import_created",
        "backup_import_checked",
        "backup_import_started",
        "backup_import_finished",
        "backup_import_failed",
        "backup_import_abandoned",
        "backup_refused",
    ],
} as const;
const ALL_ADMIN_ACTIONS: readonly string[] = [...new Set<string>([...ADMIN_ACTIONS.role, ...ADMIN_ACTIONS.refused, ...ADMIN_ACTIONS.backup])];

const ID_RE = /^[A-Za-z0-9-]{8,64}$/;

/** The session's player when it is an active administrator; for anyone else the route does not exist. */
export async function requireAdminRole(ctx: Ctx): Promise<AuthInfo> {
    const auth = await requireAuth(ctx);
    if (auth.player.role !== "admin" || auth.player.account_status !== "active") throw new ApiError(404, "NOT_FOUND", "Route inconnue.");
    return auth;
}

type AdminPlayerRow = PlayerRow & { creations: number };

/** A player as the Joueurs tab shows it: the location and presence only as the player shares them. */
function adminPlayerJson(p: AdminPlayerRow) {
    const presenceHidden = p.presence_visible === 0;
    return {
        id: p.id,
        username: p.username,
        avatar: p.avatar,
        avatar_url: avatarUrl(p),
        country_code: visibleCountry(p),
        role: (p.role ?? "player") as Role,
        account_status: p.account_status,
        created_at: p.created_at,
        last_seen_at: presenceHidden ? null : p.last_seen_at,
        presence: presenceHidden ? "hidden" : presenceOf(p),
        creations: p.creations ?? 0,
    };
}

const PLAYER_COLUMNS = `p.*, (SELECT COUNT(*) FROM community_levels c WHERE c.author_id = p.id AND c.status = 'published') AS creations`;

/**
 * GET /admin/players?q=&filter=all|admins|online&limit=&offset=
 * Search by username (anywhere in it, from 2 characters), newest accounts first; `total`
 * counts the matches, `players_total` and `admins_total` the whole server.
 */
export async function listPlayers(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const page = paging(ctx.url, 50, 100);
    const filter = queryEnum(ctx.url, "filter", ["all", "admins", "online"] as const, "all");
    const q = queryString(ctx.url, "q", /^[A-Za-z0-9_.-]{2,20}$/);
    const where: string[] = [];
    const binds: unknown[] = [];
    if (q) {
        binds.push("%" + likePrefix(normalizeUsername(q)));
        where.push(`p.username_normalized LIKE ?${binds.length} ESCAPE '\\'`);
    }
    if (filter === "admins") where.push("p.role = 'admin'");
    if (filter === "online") {
        binds.push(new Date(Date.now() - PRESENCE_TIMEOUT_MS).toISOString());
        where.push(`p.presence_visible != 0 AND p.presence_state IS NOT NULL AND p.presence_at >= ?${binds.length}`);
    }
    const cond = where.length ? "WHERE " + where.join(" AND ") : "";
    binds.push(page.limit + 1, page.offset);
    const [rows, count, totals] = await Promise.all([
        ctx.env.DB.prepare(
            `SELECT ${PLAYER_COLUMNS} FROM players p ${cond}
             ORDER BY p.created_at DESC, p.id LIMIT ?${binds.length - 1} OFFSET ?${binds.length}`,
        )
            .bind(...binds)
            .all<AdminPlayerRow>(),
        ctx.env.DB.prepare(`SELECT COUNT(*) AS n FROM players p ${cond}`)
            .bind(...binds.slice(0, -2))
            .first<{ n: number }>(),
        ctx.env.DB.prepare("SELECT COUNT(*) AS n, COALESCE(SUM(role = 'admin'), 0) AS a FROM players").first<{ n: number; a: number }>(),
    ]);
    const list = rows.results.slice(0, page.limit);
    return json(ctx, {
        players: list.map(adminPlayerJson),
        total: count?.n ?? 0,
        players_total: totals?.n ?? 0,
        admins_total: totals?.a ?? 0,
        has_more: rows.results.length > page.limit,
        offset: page.offset,
    });
}

async function refuse(ctx: Ctx, auth: AuthInfo, error: ApiError, details: Record<string, unknown>): Promise<never> {
    await audit(ctx, auth.player.id, "admin_refused", { ...details, reason: error.code, by_username: auth.player.username });
    throw error;
}

/**
 * POST /admin/players/{playerId}/role {"admin": true|false}
 * Never one's own role, and never the last administrator: the change is a single conditional
 * UPDATE (the requester still admin, at least one other admin left), so two administrators
 * removing each other at the same time cannot leave the server without one.
 */
export async function setRole(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const targetId = ctx.params.playerId ?? "";
    if (!ID_RE.test(targetId)) throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["admin"]);
    const makeAdmin = v.bool("admin");
    v.done();
    const role: Role = makeAdmin ? "admin" : "player";
    const target = await ctx.env.DB.prepare(`SELECT ${PLAYER_COLUMNS} FROM players p WHERE p.id = ?1`).bind(targetId).first<AdminPlayerRow>();
    if (!target) throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    const what = { target: target.id, target_username: target.username, role };
    if (target.id === auth.player.id)
        await refuse(ctx, auth, Errors.conflict("OWN_ROLE", "Vous ne pouvez pas changer votre propre rôle."), what);
    if ((target.role ?? "player") === role) return json(ctx, { player: adminPlayerJson(target), changed: false });
    if (makeAdmin && target.account_status !== "active")
        await refuse(ctx, auth, Errors.conflict("ACCOUNT_SUSPENDED", "Ce compte est suspendu : il ne peut pas devenir admin."), what);
    const res = makeAdmin
        ? await ctx.env.DB.prepare(
              `UPDATE players SET role = 'admin' WHERE id = ?1 AND role = 'player' AND account_status = 'active'
               AND EXISTS (SELECT 1 FROM players WHERE id = ?2 AND role = 'admin' AND account_status = 'active')`,
          )
              .bind(target.id, auth.player.id)
              .run()
        : await ctx.env.DB.prepare(
              `UPDATE players SET role = 'player' WHERE id = ?1 AND role = 'admin'
               AND EXISTS (SELECT 1 FROM players WHERE id = ?2 AND role = 'admin' AND account_status = 'active')
               AND (SELECT COUNT(*) FROM players WHERE role = 'admin' AND account_status = 'active' AND id != ?1) >= 1`,
          )
              .bind(target.id, auth.player.id)
              .run();
    if (!res.meta.changes)
        await refuse(
            ctx,
            auth,
            makeAdmin
                ? Errors.conflict("ROLE_CHANGED", "Le rôle a changé entre-temps : actualisez la liste.")
                : Errors.conflict("LAST_ADMIN", "Il doit toujours rester au moins un admin."),
            what,
        );
    await audit(ctx, auth.player.id, makeAdmin ? "admin_role_granted" : "admin_role_revoked", { ...what, by_username: auth.player.username });
    return json(ctx, { player: adminPlayerJson({ ...target, role }), changed: true });
}

interface AuditRow {
    id: number;
    player_id: string | null;
    action: string;
    details: string | null;
    created_at: string;
    actor_username: string | null;
    actor_avatar: string | null;
}

function parseDetails(raw: string | null): Record<string, unknown> {
    if (!raw) return {};
    try {
        const d = JSON.parse(raw) as unknown;
        return d && typeof d === "object" && !Array.isArray(d) ? (d as Record<string, unknown>) : {};
    } catch {
        return {};
    }
}

/**
 * GET /admin/log?kind=all|role|refused|backup&actor=<player id>&limit=&offset=
 * The administration's own actions, newest first: who (the admin's current username, or the
 * one written at the time when the account is gone), what, when. Never the IP hash.
 */
export async function listLog(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const page = paging(ctx.url, 50, 100);
    const kind = queryEnum(ctx.url, "kind", ["all", "role", "refused", "backup"] as const, "all");
    const actor = queryString(ctx.url, "actor", ID_RE);
    const actions = kind === "all" ? ALL_ADMIN_ACTIONS : ADMIN_ACTIONS[kind];
    const binds: unknown[] = [...actions];
    const placeholders = actions.map((_a, i) => `?${i + 1}`).join(", ");
    let cond = `a.action IN (${placeholders})`;
    if (actor) {
        binds.push(actor);
        cond += ` AND a.player_id = ?${binds.length}`;
    }
    binds.push(page.limit + 1, page.offset);
    const rows = await ctx.env.DB.prepare(
        `SELECT a.id, a.player_id, a.action, a.details, a.created_at, p.username AS actor_username, p.avatar AS actor_avatar
         FROM audit_log a LEFT JOIN players p ON p.id = a.player_id
         WHERE ${cond} ORDER BY a.created_at DESC, a.id DESC LIMIT ?${binds.length - 1} OFFSET ?${binds.length}`,
    )
        .bind(...binds)
        .all<AuditRow>();
    const entries = rows.results.slice(0, page.limit).map((r) => {
        const d = parseDetails(r.details);
        const by = typeof d.by_username === "string" ? d.by_username : null;
        const str = (k: string) => (typeof d[k] === "string" ? (d[k] as string) : null);
        return {
            id: r.id,
            at: r.created_at,
            action: r.action,
            actor: r.player_id ? { id: r.player_id, username: r.actor_username ?? by, avatar: r.actor_avatar ?? null } : null,
            target: str("target") ? { id: str("target"), username: str("target_username") } : null,
            role: str("role"),
            reason: str("reason"),
            by: str("by"),
            // Backups: which copy, which import, which kind of copy.
            backup_id: str("backup_id"),
            backup_kind: str("backup_kind"),
            import_id: str("import_id"),
        };
    });
    return json(ctx, { entries, has_more: rows.results.length > page.limit, offset: page.offset });
}
