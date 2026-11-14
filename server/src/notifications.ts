// Notification centre (Cosmic Online 2.9): new levels of followed creators, new versions of
// favorite levels, replies and reactions to comments, comments on one's levels, new
// followers (and, with the collaborative projects, invitations, roles and versions).
// Repeated events merge while unread (group_key) so that a busy comment never floods the
// list; players turn categories off in their settings. The player is always the session.

import { requireAuth } from "./auth";
import { Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import { Validator, paging } from "./validate";

export const NOTIFICATION_KINDS = [
    "new_level", //         a followed creator published a new level
    "level_update", //      a new version of a favorite level
    "comment_reply", //     a reply to one of my comments
    "comment_reaction", //  reactions to one of my comments
    "level_comment", //     comments on one of my levels
    "new_follower", //      someone follows me
    "project_invite", //    collaborative projects (phase 2)
    "project_role",
    "project_version",
    "project_mention",
] as const;
export type NotificationKind = (typeof NOTIFICATION_KINDS)[number];

/** Filter groups of the notification screen. */
const GROUPS: Record<string, NotificationKind[]> = {
    creators: ["new_level", "level_update", "new_follower"],
    comments: ["comment_reply", "comment_reaction", "level_comment"],
    projects: ["project_invite", "project_role", "project_version", "project_mention"],
};
const MAX_PER_PLAYER = 500;

export interface NotifyOptions {
    actor?: string | null;
    levelId?: string | null;
    commentId?: string | null;
    data?: Record<string, unknown>;
    /** Merge with an unread notification of the same key (count + 1) instead of adding one. */
    groupKey?: string | null;
}

/** SQL fragment: the recipient (column `rcol`) did not turn this kind off (bound at ?k). */
const accepts = (rcol: string, k: string) => `instr(',' || (SELECT notifications_off FROM players WHERE id = ${rcol}) || ',', ',' || ${k} || ',') = 0`;

/** One notification for one player (skipped for oneself, blocks, or a category turned off). */
export async function notify(ctx: Ctx, recipient: string, kind: NotificationKind, o: NotifyOptions = {}): Promise<void> {
    if (o.actor && o.actor === recipient) return;
    const now = nowIso();
    const data = JSON.stringify(o.data ?? {});
    const db = ctx.env.DB;
    const ok = await db
        .prepare(
            `SELECT 1 AS x FROM players p WHERE p.id = ?1 AND ${accepts("p.id", "?2")}
               AND NOT EXISTS (SELECT 1 FROM player_blocks b WHERE (b.player_id = ?1 AND b.blocked_id = ?3) OR (b.player_id = ?3 AND b.blocked_id = ?1))`,
        )
        .bind(recipient, kind, o.actor ?? "")
        .first();
    if (!ok) return;
    if (o.groupKey) {
        const merged = await db
            .prepare(
                `UPDATE notifications SET count = count + 1, actor_id = ?3, data = ?4, created_at = ?5
                 WHERE player_id = ?1 AND group_key = ?2 AND read_at IS NULL`,
            )
            .bind(recipient, o.groupKey, o.actor ?? null, data, now)
            .run();
        if (merged.meta.changes > 0) return;
    }
    await db
        .prepare(
            `INSERT INTO notifications (id, player_id, kind, actor_id, level_id, comment_id, data, group_key, count, created_at)
             VALUES (lower(hex(randomblob(16))), ?1, ?2, ?3, ?4, ?5, ?6, ?7, 1, ?8)`,
        )
        .bind(recipient, kind, o.actor ?? null, o.levelId ?? null, o.commentId ?? null, data, o.groupKey ?? null, now)
        .run();
}

/**
 * The same notification for many players at once, chosen by `recipientsSql` (a SELECT of
 * player ids, binding ?1 = actor and ?2 = level). One statement whatever the audience.
 */
export async function notifyMany(
    ctx: Ctx,
    recipientsSql: string,
    kind: NotificationKind,
    o: NotifyOptions & { actor: string; levelId: string },
): Promise<number> {
    const res = await ctx.env.DB.prepare(
        `INSERT INTO notifications (id, player_id, kind, actor_id, level_id, data, count, created_at)
         SELECT lower(hex(randomblob(16))), r.pid, ?3, ?1, ?2, ?4, 1, ?5 FROM (${recipientsSql}) r
         WHERE r.pid <> ?1 AND ${accepts("r.pid", "?3")}
           AND NOT EXISTS (SELECT 1 FROM player_blocks b WHERE (b.player_id = r.pid AND b.blocked_id = ?1) OR (b.player_id = ?1 AND b.blocked_id = r.pid))`,
    )
        .bind(o.actor, o.levelId, kind, JSON.stringify(o.data ?? {}), nowIso())
        .run();
    return res.meta.changes;
}

interface NotificationRow {
    id: string;
    kind: NotificationKind;
    actor_id: string | null;
    level_id: string | null;
    comment_id: string | null;
    data: string;
    count: number;
    created_at: string;
    read_at: string | null;
    actor_name: string | null;
    actor_avatar: string | null;
}

/** The French sentence of a notification (the client shows it as is). */
function sentence(n: NotificationRow, d: Record<string, string>): string {
    const who = n.actor_name ?? "Un joueur";
    const more = n.count > 1 ? ` (+${n.count - 1})` : "";
    switch (n.kind) {
        case "new_level":
            return `${who} vient de publier un nouveau niveau : ${d.title ?? ""}`;
        case "level_update":
            return `Nouvelle version d'un de vos favoris : ${d.title ?? ""} (v${d.version ?? "?"})`;
        case "comment_reply":
            return n.count > 1 ? `${n.count} réponses à votre commentaire sur ${d.title ?? ""}` : `${who} a répondu à votre commentaire sur ${d.title ?? ""}`;
        case "comment_reaction":
            return n.count > 1 ? `${n.count} réactions à votre commentaire sur ${d.title ?? ""}` : `${who} a réagi à votre commentaire sur ${d.title ?? ""}`;
        case "level_comment":
            return n.count > 1 ? `${n.count} nouveaux commentaires sur ${d.title ?? ""}` : `${who} a commenté ${d.title ?? ""}`;
        case "new_follower":
            return `${who} vous suit${more}`;
        case "project_invite":
            return `${who} vous invite dans le projet ${d.project ?? ""}`;
        case "project_role":
            return `Votre rôle dans ${d.project ?? ""} est maintenant : ${d.role ?? ""}`;
        case "project_version":
            return `Nouvelle version de ${d.project ?? ""} : ${d.version ?? ""}`;
        case "project_mention":
            return `${who} vous a mentionné dans ${d.project ?? ""}`;
    }
}

function view(n: NotificationRow) {
    let d: Record<string, string> = {};
    try {
        d = JSON.parse(n.data) as Record<string, string>;
    } catch {
        d = {};
    }
    return {
        id: n.id,
        kind: n.kind,
        text: sentence(n, d),
        count: n.count,
        actor: n.actor_id ? { id: n.actor_id, username: n.actor_name, avatar: n.actor_avatar } : null,
        level_id: n.level_id,
        comment_id: n.comment_id,
        data: d,
        created_at: n.created_at,
        read: n.read_at !== null,
    };
}

// ------------------------------------------------------------------- routes

/** GET /notifications?filter=all|unread|creators|comments|projects&limit=&offset= */
export async function listNotifications(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const filter = ctx.url.searchParams.get("filter") ?? "all";
    if (!["all", "unread", ...Object.keys(GROUPS)].includes(filter))
        throw Errors.validation([{ field: "filter", message: "all, unread, creators, comments ou projects" }]);
    const { limit, offset } = paging(ctx.url, 30, 100);
    const where = ["n.player_id = ?1"];
    if (filter === "unread") where.push("n.read_at IS NULL");
    else if (filter !== "all") where.push(`n.kind IN (${GROUPS[filter]!.map((k) => `'${k}'`).join(", ")})`);
    const db = ctx.env.DB;
    const total = await db.prepare(`SELECT COUNT(*) AS n FROM notifications n WHERE ${where.join(" AND ")}`).bind(auth.player.id).first<{ n: number }>();
    const rows = await db
        .prepare(
            `SELECT n.*, a.username AS actor_name, a.avatar AS actor_avatar FROM notifications n LEFT JOIN players a ON a.id = n.actor_id
             WHERE ${where.join(" AND ")} ORDER BY n.created_at DESC LIMIT ?2 OFFSET ?3`,
        )
        .bind(auth.player.id, limit, offset)
        .all<NotificationRow>();
    const unread = await db.prepare("SELECT COUNT(*) AS n FROM notifications WHERE player_id = ?1 AND read_at IS NULL").bind(auth.player.id).first<{ n: number }>();
    const n = total?.n ?? 0;
    return json(ctx, { notifications: rows.results.map(view), unread: unread?.n ?? 0 }, {
        meta: { limit, offset, total: n, next_offset: offset + limit < n ? offset + limit : null },
    });
}

/** GET /notifications/unread: the counter of the menus. */
export async function unreadNotifications(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const r = await ctx.env.DB.prepare("SELECT COUNT(*) AS n FROM notifications WHERE player_id = ?1 AND read_at IS NULL").bind(auth.player.id).first<{ n: number }>();
    return json(ctx, { unread: r?.n ?? 0 });
}

/** POST /notifications/{id}/read */
export async function readNotification(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const r = await ctx.env.DB.prepare("UPDATE notifications SET read_at = COALESCE(read_at, ?3) WHERE id = ?1 AND player_id = ?2")
        .bind(ctx.params.notificationId ?? "", auth.player.id, nowIso())
        .run();
    if (r.meta.changes === 0) throw Errors.notFound("NOTIFICATION_NOT_FOUND", "Notification introuvable.");
    return json(ctx, { read: true });
}

/** POST /notifications/read-all */
export async function readAllNotifications(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const r = await ctx.env.DB.prepare("UPDATE notifications SET read_at = ?2 WHERE player_id = ?1 AND read_at IS NULL").bind(auth.player.id, nowIso()).run();
    return json(ctx, { read: r.meta.changes });
}

/** DELETE /notifications/{id} */
export async function deleteNotification(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const r = await ctx.env.DB.prepare("DELETE FROM notifications WHERE id = ?1 AND player_id = ?2").bind(ctx.params.notificationId ?? "", auth.player.id).run();
    if (r.meta.changes === 0) throw Errors.notFound("NOTIFICATION_NOT_FOUND", "Notification introuvable.");
    return json(ctx, { deleted: true });
}

/** GET /notifications/settings: the categories, on or off. */
export async function getNotificationSettings(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const off = new Set((auth.player as unknown as { notifications_off?: string }).notifications_off?.split(",").filter(Boolean) ?? []);
    return json(ctx, { settings: Object.fromEntries(NOTIFICATION_KINDS.map((k) => [k, !off.has(k)])) });
}

/** PUT /notifications/settings {kind: bool, ...} */
export async function putNotificationSettings(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, [...NOTIFICATION_KINDS]);
    const off = new Set((auth.player as unknown as { notifications_off?: string }).notifications_off?.split(",").filter(Boolean) ?? []);
    for (const k of NOTIFICATION_KINDS) {
        const on = v.bool(k, { optional: true });
        if (on === true) off.delete(k);
        else if (on === false) off.add(k);
    }
    v.done();
    const list = [...off].filter((k) => (NOTIFICATION_KINDS as readonly string[]).includes(k)).sort();
    await ctx.env.DB.prepare("UPDATE players SET notifications_off = ?2 WHERE id = ?1").bind(auth.player.id, list.join(",")).run();
    return json(ctx, { settings: Object.fromEntries(NOTIFICATION_KINDS.map((k) => [k, !list.includes(k)])) });
}

/** Daily maintenance: read notifications older than 60 days, and the overflow of very busy players. */
export async function pruneNotifications(ctx: Ctx): Promise<void> {
    const cutoff = new Date(Date.now() - 60 * 86400 * 1000).toISOString();
    await ctx.env.DB.prepare("DELETE FROM notifications WHERE read_at IS NOT NULL AND created_at < ?1").bind(cutoff).run();
    await ctx.env.DB.prepare(
        `DELETE FROM notifications WHERE id IN (
           SELECT id FROM (SELECT id, ROW_NUMBER() OVER (PARTITION BY player_id ORDER BY created_at DESC) AS rn FROM notifications) WHERE rn > ?1)`,
    )
        .bind(MAX_PER_PLAYER)
        .run();
}
