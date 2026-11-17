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
    // 2.41: ADMINISTRATION › Calendrier des défis (adminChallenges.ts).
    challenge: ["admin_challenge_set", "admin_challenge_reset", "admin_challenge_rewards", "admin_title_created"],
    refused: ["admin_refused", "backup_refused"],
    // 2.45: ADMINISTRATION › Campagne (campaignRevisions.ts): a revision published, or an earlier one made current again.
    campaign: ["admin_campaign_published", "admin_campaign_reverted"],
    // ADMINISTRATION › Créations (creations.ts): a suit (2.59: a trail, a frame, a victory effect) created, edited, archived or restored.
    // 2.61: its image sent from the Studio (creationAssets.ts).
    creation: ["admin_creation_created", "admin_creation_updated", "admin_creation_archived", "admin_creation_restored", "admin_creation_image"],
    // 2.46: ADMINISTRATION › Ennemis (enemyRevisions.ts): a native entity published as a revision.
    enemy: ["admin_enemy_published"],
    // 2.47: ADMINISTRATION › Joueurs: the computer an account is bound to (devices.ts).
    // 2.50: an account's data migrated to another one, by an admin or by the player with a code (accountMigration.ts).
    account: ["admin_device_unbound", "admin_device_bound", "admin_account_migrated", "account_migrated"],
    // 2.66: ADMINISTRATION › Clans (clans.ts): a clan renamed or dissolved by an administrator.
    clan: ["admin_clan_renamed", "admin_clan_dissolved"],
    // ADMINISTRATION › Raids (raids.ts): a world event created, edited, published or cancelled.
    raid: ["admin_raid_created", "admin_raid_updated", "admin_raid_published", "admin_raid_cancelled"],
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
const ALL_ADMIN_ACTIONS: readonly string[] = [
    ...new Set<string>([
        ...ADMIN_ACTIONS.role,
        ...ADMIN_ACTIONS.challenge,
        ...ADMIN_ACTIONS.refused,
        ...ADMIN_ACTIONS.backup,
        ...ADMIN_ACTIONS.campaign,
        ...ADMIN_ACTIONS.creation,
        ...ADMIN_ACTIONS.enemy,
        ...ADMIN_ACTIONS.account,
        ...ADMIN_ACTIONS.clan,
        ...ADMIN_ACTIONS.raid,
    ]),
];
const LOG_KINDS = ["all", "role", "challenge", "refused", "backup", "campaign", "creation", "enemy", "account", "clan", "raid"] as const;

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
        // 2.50: closed after a migration of its data to another account.
        closed_at: p.closed_at ?? null,
        created_at: p.created_at,
        last_seen_at: presenceHidden ? null : p.last_seen_at,
        presence: presenceHidden ? "hidden" : presenceOf(p),
        creations: p.creations ?? 0,
        // 2.51: the game version its client declared last (null before 2.51 or never connected).
        client_version: p.client_version ?? null,
        client_version_at: p.client_version_at ?? null,
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
    const latest = await ctx.env.DB.prepare(
        "SELECT version FROM client_releases WHERE platform = 'windows' ORDER BY version_key DESC LIMIT 1",
    ).first<{ version: string }>();
    const list = rows.results.slice(0, page.limit);
    return json(ctx, {
        players: list.map(adminPlayerJson),
        total: count?.n ?? 0,
        players_total: totals?.n ?? 0,
        admins_total: totals?.a ?? 0,
        has_more: rows.results.length > page.limit,
        offset: page.offset,
        // 2.51: the latest published Windows installer, to compare each player's version with.
        latest_client_version: latest?.version ?? null,
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

const isObject = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);

/**
 * What the Journal shows of a challenge an administrator set, put back to the draw or tried to
 * change (2.41): the period, and what the server wrote at the time (level, rules, objective,
 * tiers, what it replaced or what was drawn instead). Only the keys the server writes.
 */
function challengeOfDetails(d: Record<string, unknown>) {
    if (typeof d.challenge !== "string") return null;
    const pick = (k: string) => (isObject(d[k]) ? d[k] : null);
    return {
        id: d.challenge,
        kind: typeof d.kind === "string" ? d.kind : null,
        period: typeof d.period === "string" ? d.period : null,
        start: typeof d.start === "string" ? d.start : null,
        action: typeof d.action === "string" ? d.action : null,
        level: pick("level"),
        rules: pick("rules"),
        objective: typeof d.objective === "string" ? d.objective : null,
        tiers: pick("tiers"),
        replaced: pick("replaced"),
        removed: pick("removed"),
        drawn: pick("drawn"),
        draws_on: typeof d.draws_on === "string" ? d.draws_on : null,
        field: typeof d.field === "string" ? d.field : null,
        // 2.43: its rewards (set, or tried), and those they replaced.
        rewards: Array.isArray(d.rewards) ? d.rewards.filter(isObject) : null,
        previous: Array.isArray(d.previous) ? d.previous.filter(isObject) : null,
        // 2.60: changed while it ran (nobody had a result on it), or refused because players had one.
        running: d.running === true,
        results_count: typeof d.results_count === "number" ? d.results_count : null,
    };
}

/**
 * What the Journal shows of a campaign revision published, made current again, or refused
 * (2.45): the level, the revision, the one it replaced, the choice about the records.
 */
function campaignOfDetails(d: Record<string, unknown>) {
    const c = d.campaign;
    if (!isObject(c) || typeof c.level_id !== "string") return null;
    const num = (k: string) => (typeof c[k] === "number" ? (c[k] as number) : null);
    const str = (k: string) => (typeof c[k] === "string" ? (c[k] as string) : null);
    return {
        level_id: c.level_id,
        level_name: str("level_name"),
        version: num("version"),
        content_hash: str("content_hash"),
        from_version: num("from_version"),
        base_version: num("base_version"),
        records: str("records"),
        carried_bests: num("carried_bests"),
        action: str("action"),
    };
}

/**
 * What the Journal shows of a creation (ADMINISTRATION › Créations) created, edited, archived,
 * restored or refused: its id, kind, name, colours and (2.59) style, a trail's width, life and
 * sparkles, what it was before an edit, and (a refusal) what was tried. Only the keys the
 * server writes.
 */
function creationOfDetails(d: Record<string, unknown>) {
    const c = d.creation;
    if (!isObject(c)) return null;
    const pick = (o: Record<string, unknown>) => ({
        id: typeof o.id === "number" ? o.id : null,
        ref: typeof o.ref === "string" ? o.ref : null,
        kind: typeof o.kind === "string" ? o.kind : null,
        name: typeof o.name === "string" ? o.name : null,
        colors: isObject(o.colors) ? o.colors : null,
        style: typeof o.style === "string" ? o.style : null,
        width: typeof o.width === "number" ? o.width : null,
        life_ms: typeof o.life_ms === "number" ? o.life_ms : null,
        particles: typeof o.particles === "boolean" ? o.particles : null,
    });
    return { ...pick(c), action: typeof c.action === "string" ? c.action : null, previous: isObject(c.previous) ? pick(c.previous) : null };
}

/**
 * 2.61: the image of a creation sent from the Studio (or refused): its hash, its size in bytes and
 * in pixels, how it is cut, the hash it replaced. Only the keys the server writes.
 */
function imageOfDetails(d: Record<string, unknown>) {
    const i = d.image;
    if (!isObject(i)) return null;
    const num = (k: string) => (typeof i[k] === "number" ? (i[k] as number) : null);
    const str = (k: string) => (typeof i[k] === "string" ? (i[k] as string) : null);
    return {
        hash: str("hash"),
        previous_hash: str("previous_hash"),
        bytes: num("bytes"),
        width: num("width"),
        height: num("height"),
        frame_w: num("frame_w"),
        frame_h: num("frame_h"),
        frames: num("frames"),
        fps: num("fps"),
    };
}

/**
 * What the Journal shows of a native entity published as a revision, or refused (2.46): the
 * entity, its revision, and each campaign level given a new revision with its records choice.
 */
function enemyOfDetails(d: Record<string, unknown>) {
    const e = d.enemy;
    if (!isObject(e) || typeof e.enemy_id !== "string") return null;
    const num = (o: Record<string, unknown>, k: string) => (typeof o[k] === "number" ? (o[k] as number) : null);
    const str = (o: Record<string, unknown>, k: string) => (typeof o[k] === "string" ? (o[k] as string) : null);
    const levels = Array.isArray(e.levels) ? e.levels.filter(isObject).slice(0, 100) : [];
    return {
        enemy_id: e.enemy_id,
        enemy_name: str(e, "enemy_name"),
        revision: num(e, "revision"),
        from_revision: num(e, "from_revision"),
        base_revision: num(e, "base_revision"),
        levels: levels.map((l) => ({
            level_id: str(l, "level_id"),
            level_name: str(l, "level_name"),
            version: num(l, "version"),
            records: str(l, "records"),
            carried_bests: num(l, "carried_bests"),
        })),
    };
}

/**
 * What the Journal shows of a migration of an account's data to another one (2.50): both
 * accounts, the mode, what was migrated, whether the old account was closed, the backup made
 * before (an admin's migration), a progression to restore. Only the keys the server writes.
 */
function migrationOfDetails(d: Record<string, unknown>) {
    const m = d.migration;
    if (!isObject(m) || typeof m.id !== "string") return null;
    const who = (o: unknown) =>
        isObject(o) && typeof o.id === "string" ? { id: o.id, username: typeof o.username === "string" ? o.username : null } : null;
    return {
        id: m.id,
        from: who(m.from),
        to: who(m.to),
        mode: typeof m.mode === "string" ? m.mode : null,
        items: Array.isArray(m.items) ? m.items.filter((i): i is string => typeof i === "string") : [],
        close_source: m.close_source === true,
        restore: m.restore === true,
        backup_id: typeof m.backup_id === "string" ? m.backup_id : null,
        by_admin: m.by_admin === true,
    };
}

/**
 * What the Journal shows of a clan renamed, dissolved, or a refused rename (2.66): the clan,
 * its name and tag before a rename, its members when dissolved. Only the keys the server writes.
 */
function clanOfDetails(d: Record<string, unknown>) {
    const c = d.clan;
    if (!isObject(c) || typeof c.id !== "string") return null;
    const str = (o: Record<string, unknown>, k: string) => (typeof o[k] === "string" ? (o[k] as string) : null);
    const prev = isObject(c.previous) ? { name: str(c.previous, "name"), tag: str(c.previous, "tag") } : null;
    return { id: c.id, name: str(c, "name"), tag: str(c, "tag"), previous: prev, members: typeof c.members === "number" ? c.members : null };
}

/** A world event of ADMINISTRATION › Raids as the Journal keeps it. */
function raidOfDetails(d: Record<string, unknown>) {
    const r = d.raid;
    if (!isObject(r) || typeof r.id !== "string") return null;
    const str = (k: string) => (typeof r[k] === "string" ? (r[k] as string) : null);
    return {
        id: r.id,
        name: str("name"),
        format: str("format"),
        starts_at: str("starts_at"),
        changes: Array.isArray(r.changes) ? r.changes.filter((c): c is string => typeof c === "string") : null,
        notified: typeof r.notified === "number" ? r.notified : null,
    };
}

/**
 * GET /admin/log?kind=all|role|challenge|refused|backup|campaign|creation|enemy|account|clan|raid&actor=<player id>&limit=&offset=
 * The administration's own actions, newest first: who (the admin's current username, or the
 * one written at the time when the account is gone), what, when. Never the IP hash.
 */
export async function listLog(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const page = paging(ctx.url, 50, 100);
    const kind = queryEnum(ctx.url, "kind", LOG_KINDS, "all");
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
            // 2.41: a challenge of the calendar (set, put back to the draw, or refused).
            challenge: challengeOfDetails(d),
            // 2.43: a title created on the server (or refused): { id?, text, color }.
            title: isObject(d.title) ? d.title : null,
            // 2.45: a campaign revision (published, made current again, or refused).
            campaign: campaignOfDetails(d),
            // 2.46: a native entity published as a revision (or refused).
            enemy: enemyOfDetails(d),
            // A creation (ADMINISTRATION › Créations): created, edited, archived, restored or refused.
            creation: creationOfDetails(d),
            // 2.61: the image of a creation (the Studio), sent or refused.
            image: imageOfDetails(d),
            // 2.50: a migration of an account's data to another one.
            migration: migrationOfDetails(d),
            // 2.66: a clan renamed or dissolved (or a refused rename).
            clan: clanOfDetails(d),
            // A world event of ADMINISTRATION › Raids (created, edited, published, cancelled).
            raid: raidOfDetails(d),
        };
    });
    return json(ctx, { entries, has_more: rows.results.length > page.limit, offset: page.offset });
}
