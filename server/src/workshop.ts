// Workshop (Cosmic Online 2.9): favorites, comments, replies, reactions and their moderation
// on the community levels. A level's creator can pin comments but never delete the comments
// of others (negative reviews stay): only their authors can, or the moderation after
// reports. The player is always identified by the session.

import { audit, optionalAuth, requireAuth } from "./auth";
import { sha256Hex } from "./crypto";
import { ApiError, Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import { playerSummary, type PlayerRow } from "./model";
import { notify } from "./notifications";
import { RULES, rateLimit } from "./ratelimit";
import { Validator, paging } from "./validate";

const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
export const REACTIONS = ["like", "love", "funny", "impressive", "not_useful"] as const;
type Reaction = (typeof REACTIONS)[number];
const COLUMN: Record<Reaction, string> = { like: "r_like", love: "r_love", funny: "r_funny", impressive: "r_impressive", not_useful: "r_not_useful" };
const MAX_COMMENT = 1000;
const MAX_PINNED = 3;
const REPORTS_TO_HIDE = 3;
const COMMENT_REPORT_REASONS = ["spam", "harassment", "inappropriate", "spoiler", "other"];

interface LevelRow {
    id: string;
    author_id: string;
    title: string;
    status: string;
    current_version: number;
}

async function publishedLevel(ctx: Ctx, id: string, viewer: string | null): Promise<LevelRow> {
    if (!ID_RE.test(id)) throw Errors.notFound("COMMUNITY_LEVEL_NOT_FOUND", "Niveau introuvable.");
    const l = await ctx.env.DB.prepare("SELECT id, author_id, title, status, current_version FROM community_levels WHERE id = ?1").bind(id).first<LevelRow>();
    if (!l || (l.status !== "published" && l.author_id !== viewer)) throw Errors.notFound("COMMUNITY_LEVEL_NOT_FOUND", "Niveau introuvable.");
    return l;
}

async function blocked(ctx: Ctx, a: string, b: string): Promise<boolean> {
    const r = await ctx.env.DB.prepare(
        "SELECT 1 AS x FROM player_blocks WHERE (player_id = ?1 AND blocked_id = ?2) OR (player_id = ?2 AND blocked_id = ?1)",
    )
        .bind(a, b)
        .first();
    return r !== null;
}

// ---------------------------------------------------------------- favorites

/** POST /community/levels/{id}/favorite (idempotent: one favorite per player and level) */
export async function addFavorite(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.communityFavorite, "player:" + me);
    const l = await publishedLevel(ctx, ctx.params.levelId!, me);
    const author = await ctx.env.DB.prepare("SELECT username FROM players WHERE id = ?1").bind(l.author_id).first<{ username: string }>();
    const res = await ctx.env.DB.prepare(
        "INSERT OR IGNORE INTO community_favorites (player_id, level_id, level_title, author_name, created_at) VALUES (?1, ?2, ?3, ?4, ?5)",
    )
        .bind(me, l.id, l.title, author?.username ?? "", nowIso())
        .run();
    if (res.meta.changes > 0) await ctx.env.DB.prepare("UPDATE community_levels SET favorite_count = favorite_count + 1 WHERE id = ?1").bind(l.id).run();
    const c = await ctx.env.DB.prepare("SELECT favorite_count AS n FROM community_levels WHERE id = ?1").bind(l.id).first<{ n: number }>();
    return json(ctx, { favorite: true, favorites: c?.n ?? 0 });
}

/** DELETE /community/levels/{id}/favorite (also for a level that is no longer available) */
export async function removeFavorite(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const id = ctx.params.levelId ?? "";
    if (!ID_RE.test(id)) throw Errors.notFound("COMMUNITY_LEVEL_NOT_FOUND", "Niveau introuvable.");
    const res = await ctx.env.DB.prepare("DELETE FROM community_favorites WHERE player_id = ?1 AND level_id = ?2").bind(auth.player.id, id).run();
    if (res.meta.changes > 0)
        await ctx.env.DB.prepare("UPDATE community_levels SET favorite_count = MAX(0, favorite_count - 1) WHERE id = ?1").bind(id).run();
    const c = await ctx.env.DB.prepare("SELECT favorite_count AS n FROM community_levels WHERE id = ?1").bind(id).first<{ n: number }>();
    return json(ctx, { favorite: false, favorites: c?.n ?? 0 });
}

/**
 * GET /community/favorites?sort=added|title|rating|downloads|updated&filter=all|available|unavailable&q=
 * The player's favorites, available or not (hidden, deleted: shown with the title and author
 * of when they were added, and the reason).
 */
export async function listFavorites(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const sort = ctx.url.searchParams.get("sort") ?? "added";
    const order = {
        added: "f.created_at DESC",
        title: "COALESCE(l.title, f.level_title) COLLATE NOCASE ASC",
        rating: "COALESCE(CAST(l.rating_sum AS REAL) / NULLIF(l.rating_count, 0), 0) DESC, f.created_at DESC",
        downloads: "COALESCE(l.downloads, 0) DESC, f.created_at DESC",
        updated: "COALESCE(l.updated_at, '') DESC",
    }[sort];
    if (!order) throw Errors.validation([{ field: "sort", message: "added, title, rating, downloads ou updated" }]);
    const filter = ctx.url.searchParams.get("filter") ?? "all";
    if (!["all", "available", "unavailable"].includes(filter)) throw Errors.validation([{ field: "filter", message: "all, available ou unavailable" }]);
    const q = (ctx.url.searchParams.get("q") ?? "").trim().toLowerCase().slice(0, 60);
    const { limit, offset } = paging(ctx.url, 20, 50);
    const where = ["f.player_id = ?1"];
    const args: unknown[] = [auth.player.id];
    if (filter === "available") where.push("l.status = 'published'");
    if (filter === "unavailable") where.push("(l.id IS NULL OR l.status <> 'published')");
    if (q) {
        args.push("%" + q.replace(/[\\%_]/g, (c) => "\\" + c) + "%");
        where.push(`(lower(COALESCE(l.title, f.level_title)) LIKE ?${args.length} ESCAPE '\\' OR lower(f.author_name) LIKE ?${args.length} ESCAPE '\\')`);
    }
    const from = `FROM community_favorites f LEFT JOIN community_levels l ON l.id = f.level_id LEFT JOIN players p ON p.id = l.author_id WHERE ${where.join(" AND ")}`;
    const total = await ctx.env.DB.prepare(`SELECT COUNT(*) AS n ${from}`).bind(...args).first<{ n: number }>();
    args.push(limit, offset);
    const rows = await ctx.env.DB.prepare(
        `SELECT f.level_id, f.level_title, f.author_name, f.created_at AS favorited_at, l.title, l.status, l.downloads, l.rating_sum, l.rating_count,
                l.favorite_count, l.comment_count, l.current_version, l.updated_at, l.author_id, p.username, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility
         ${from} ORDER BY ${order} LIMIT ?${args.length - 1} OFFSET ?${args.length}`,
    )
        .bind(...args)
        .all<Record<string, unknown>>();
    const favorites = rows.results.map((r) => {
        const available = r.status === "published";
        return {
            level_id: r.level_id,
            title: (r.title as string | null) ?? r.level_title,
            author_name: (r.username as string | null) ?? r.author_name,
            available,
            unavailable_reason: available ? null : r.status == null ? "deleted" : r.status === "hidden" ? "hidden" : "private",
            favorited_at: r.favorited_at,
            version: available ? r.current_version : null,
            downloads: available ? r.downloads : null,
            favorites: available ? r.favorite_count : null,
            comments: available ? r.comment_count : null,
            rating:
                available && (r.rating_count as number) > 0
                    ? { average: Math.round(((r.rating_sum as number) / (r.rating_count as number)) * 10) / 10, count: r.rating_count }
                    : null,
            updated_at: r.updated_at ?? null,
            thumbnail_url: available ? `/api/v1/community/levels/${r.level_id}/thumbnail` : null,
        };
    });
    const n = total?.n ?? 0;
    return json(ctx, { favorites, sort, filter }, { meta: { limit, offset, total: n, next_offset: offset + limit < n ? offset + limit : null } });
}

// ----------------------------------------------------------------- comments

interface CommentRow {
    id: string;
    level_id: string;
    author_id: string;
    parent_id: string | null;
    body: string;
    created_at: string;
    edited_at: string | null;
    deleted_at: string | null;
    pinned_at: string | null;
    hidden: number;
    report_count: number;
    reply_count: number;
    r_like: number;
    r_love: number;
    r_funny: number;
    r_impressive: number;
    r_not_useful: number;
}

type CommentJoin = CommentRow &
    Partial<PlayerRow> & { my_reaction: string | null; is_creator: number; reported_by_me: number };

const COMMENT_SQL = `SELECT c.*, p.username, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility,
    (SELECT kind FROM community_comment_reactions x WHERE x.comment_id = c.id AND x.player_id = ?1) AS my_reaction,
    (SELECT 1 FROM community_comment_reports x WHERE x.comment_id = c.id AND x.reporter_id = ?1) AS reported_by_me,
    EXISTS (SELECT 1 FROM community_levels cl WHERE cl.author_id = c.author_id AND cl.status = 'published') AS is_creator
  FROM community_comments c JOIN players p ON p.id = c.author_id`;

/** What a viewer may see of a comment: hidden ones only by their author; deleted ones as a placeholder. */
function commentView(c: CommentJoin, me: string, levelAuthor: string) {
    const deleted = c.deleted_at !== null;
    const reactions = Object.fromEntries(REACTIONS.map((r) => [r, c[COLUMN[r] as keyof CommentRow] as number]));
    return {
        id: c.id,
        parent_id: c.parent_id,
        author: deleted
            ? null
            : playerSummary({
                  id: c.author_id,
                  username: c.username ?? "",
                  avatar: c.avatar ?? "avatar_01",
                  avatar_custom_key: c.avatar_custom_key ?? null,
                  country_code: c.country_code ?? null,
                  location_visibility: c.location_visibility ?? "hidden",
              } as PlayerRow),
        creator: !deleted && c.is_creator === 1,
        level_author: !deleted && c.author_id === levelAuthor,
        body: deleted ? "" : c.body,
        deleted,
        hidden: c.hidden === 1,
        pinned: c.pinned_at !== null,
        created_at: c.created_at,
        edited_at: c.edited_at,
        reply_count: c.reply_count,
        reactions,
        reaction_total: REACTIONS.reduce((s, r) => s + (reactions[r] as number), 0),
        my_reaction: c.my_reaction,
        reported: c.reported_by_me === 1,
        mine: c.author_id === me,
        can_edit: !deleted && c.author_id === me,
        can_pin: !deleted && levelAuthor === me && c.parent_id === null,
    };
}

/** Comments visible to `me`: not hidden (except one's own), without blocked authors. */
const VISIBLE = `(c.hidden = 0 OR c.author_id = ?1)
  AND c.author_id NOT IN (SELECT blocked_id FROM player_blocks WHERE player_id = ?1)
  AND c.author_id NOT IN (SELECT player_id FROM player_blocks WHERE blocked_id = ?1)`;

/** GET /community/levels/{id}/comments?sort=recent|popular|oldest&limit=&offset= : top-level comments, pinned first. */
export async function listComments(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const me = viewer?.player.id ?? "";
    const l = await publishedLevel(ctx, ctx.params.levelId!, me || null);
    const sort = ctx.url.searchParams.get("sort") ?? "recent";
    const order = {
        recent: "c.created_at DESC",
        oldest: "c.created_at ASC",
        popular: "(c.r_like + c.r_love + c.r_funny + c.r_impressive - c.r_not_useful + 2 * c.reply_count) DESC, c.created_at DESC",
    }[sort];
    if (!order) throw Errors.validation([{ field: "sort", message: "recent, popular ou oldest" }]);
    const { limit, offset } = paging(ctx.url, 20, 50);
    const where = `c.level_id = ?2 AND c.parent_id IS NULL AND ${VISIBLE}`;
    const total = await ctx.env.DB.prepare(`SELECT COUNT(*) AS n FROM community_comments c WHERE ${where}`).bind(me, l.id).first<{ n: number }>();
    const rows = await ctx.env.DB.prepare(`${COMMENT_SQL} WHERE ${where} ORDER BY (c.pinned_at IS NOT NULL) DESC, c.pinned_at DESC, ${order} LIMIT ?3 OFFSET ?4`)
        .bind(me, l.id, limit, offset)
        .all<CommentJoin>();
    const n = total?.n ?? 0;
    return json(ctx, { comments: rows.results.map((c) => commentView(c, me, l.author_id)), sort, comment_count: n }, {
        meta: { limit, offset, total: n, next_offset: offset + limit < n ? offset + limit : null },
    });
}

/** GET /community/comments/{id}/replies?limit=&offset= (oldest first) */
export async function listReplies(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const me = viewer?.player.id ?? "";
    const parent = await loadComment(ctx, ctx.params.commentId!);
    const l = await publishedLevel(ctx, parent.level_id, me || null);
    const { limit, offset } = paging(ctx.url, 20, 50);
    const where = `c.parent_id = ?2 AND ${VISIBLE}`;
    const total = await ctx.env.DB.prepare(`SELECT COUNT(*) AS n FROM community_comments c WHERE ${where}`).bind(me, parent.id).first<{ n: number }>();
    const rows = await ctx.env.DB.prepare(`${COMMENT_SQL} WHERE ${where} ORDER BY c.created_at ASC LIMIT ?3 OFFSET ?4`)
        .bind(me, parent.id, limit, offset)
        .all<CommentJoin>();
    const n = total?.n ?? 0;
    return json(ctx, { replies: rows.results.map((c) => commentView(c, me, l.author_id)) }, {
        meta: { limit, offset, total: n, next_offset: offset + limit < n ? offset + limit : null },
    });
}

async function loadComment(ctx: Ctx, id: string): Promise<CommentRow> {
    if (!ID_RE.test(id)) throw Errors.notFound("COMMENT_NOT_FOUND", "Commentaire introuvable.");
    const c = await ctx.env.DB.prepare("SELECT * FROM community_comments WHERE id = ?1").bind(id).first<CommentRow>();
    if (!c) throw Errors.notFound("COMMENT_NOT_FOUND", "Commentaire introuvable.");
    return c;
}

/** Text of a comment: trimmed, at most two blank lines in a row, few links (spam). */
function cleanBody(v: Validator, field = "body"): string {
    const raw = v.str(field, { min: 1, max: MAX_COMMENT, trim: true, multiline: true }) ?? "";
    const body = raw.replace(/\r\n?/g, "\n").replace(/\n{3,}/g, "\n\n").trim();
    if ((body.match(/https?:\/\//gi) ?? []).length > 2) v.fail(field, "trop de liens");
    if (!body) v.fail(field, "obligatoire");
    return body;
}

/** POST /community/levels/{id}/comments {body, parent_id?} */
export async function postComment(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.communityComment, "player:" + me);
    const l = await publishedLevel(ctx, ctx.params.levelId!, me);
    if (l.status !== "published") throw Errors.forbidden("LEVEL_NOT_PUBLISHED", "Les commentaires s'ouvrent quand le niveau est publié.");
    if (await blocked(ctx, me, l.author_id)) throw Errors.forbidden("BLOCKED", "Vous ne pouvez pas commenter ce niveau.");
    const v = new Validator(await readJsonObject(ctx), ["body", "parent_id"]);
    const body = cleanBody(v);
    const parentId = v.str("parent_id", { max: 36, optional: true });
    v.done();
    let parent: CommentRow | null = null;
    if (parentId) {
        parent = await loadComment(ctx, parentId);
        if (parent.level_id !== l.id) throw Errors.validation([{ field: "parent_id", message: "commentaire d'un autre niveau" }]);
        if (parent.parent_id) parent = await loadComment(ctx, parent.parent_id); // one level of replies
        if (parent.deleted_at || parent.hidden) throw Errors.forbidden("COMMENT_CLOSED", "On ne peut plus répondre à ce commentaire.");
        if (await blocked(ctx, me, parent.author_id)) throw Errors.forbidden("BLOCKED", "Vous ne pouvez pas répondre à ce commentaire.");
    }
    // Anti-spam: the same text twice in a day on the same level is refused.
    const hash = await sha256Hex(new TextEncoder().encode(body.toLowerCase().replace(/\s+/g, " ")));
    const since = new Date(Date.now() - 86400 * 1000).toISOString();
    const dup = await ctx.env.DB.prepare(
        "SELECT 1 AS x FROM community_comments WHERE author_id = ?1 AND level_id = ?2 AND body_hash = ?3 AND created_at > ?4 AND deleted_at IS NULL",
    )
        .bind(me, l.id, hash, since)
        .first();
    if (dup) throw Errors.conflict("COMMENT_DUPLICATE", "Vous avez déjà publié ce commentaire.");
    const id = crypto.randomUUID();
    const now = nowIso();
    const stmts = [
        ctx.env.DB.prepare(
            "INSERT INTO community_comments (id, level_id, author_id, parent_id, body, body_hash, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
        ).bind(id, l.id, me, parent?.id ?? null, body, hash, now),
    ];
    if (parent) stmts.push(ctx.env.DB.prepare("UPDATE community_comments SET reply_count = reply_count + 1 WHERE id = ?1").bind(parent.id));
    else stmts.push(ctx.env.DB.prepare("UPDATE community_levels SET comment_count = comment_count + 1 WHERE id = ?1").bind(l.id));
    await ctx.env.DB.batch(stmts);
    if (parent)
        await notify(ctx, parent.author_id, "comment_reply", {
            actor: me,
            levelId: l.id,
            commentId: parent.id,
            data: { title: l.title },
            groupKey: "reply:" + parent.id,
        });
    else await notify(ctx, l.author_id, "level_comment", { actor: me, levelId: l.id, commentId: id, data: { title: l.title }, groupKey: "comments:" + l.id });
    const row = await ctx.env.DB.prepare(`${COMMENT_SQL} WHERE c.id = ?2`).bind(me, id).first<CommentJoin>();
    return json(ctx, { comment: commentView(row!, me, l.author_id) }, { status: 201 });
}

/** PATCH /community/comments/{id} {body}: its author only. */
export async function editComment(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.communityComment, "player:" + me);
    const c = await loadComment(ctx, ctx.params.commentId!);
    if (c.author_id !== me) throw Errors.forbidden("NOT_COMMENT_AUTHOR", "Ce commentaire ne vous appartient pas.");
    if (c.deleted_at) throw Errors.notFound("COMMENT_NOT_FOUND", "Commentaire introuvable.");
    const v = new Validator(await readJsonObject(ctx), ["body"]);
    const body = cleanBody(v);
    v.done();
    const hash = await sha256Hex(new TextEncoder().encode(body.toLowerCase().replace(/\s+/g, " ")));
    await ctx.env.DB.prepare("UPDATE community_comments SET body = ?2, body_hash = ?3, edited_at = ?4 WHERE id = ?1").bind(c.id, body, hash, nowIso()).run();
    const l = await ctx.env.DB.prepare("SELECT author_id FROM community_levels WHERE id = ?1").bind(c.level_id).first<{ author_id: string }>();
    const row = await ctx.env.DB.prepare(`${COMMENT_SQL} WHERE c.id = ?2`).bind(me, c.id).first<CommentJoin>();
    return json(ctx, { comment: commentView(row!, me, l?.author_id ?? "") });
}

/**
 * DELETE /community/comments/{id}: its author only (the level's creator cannot delete the
 * comments of others). With replies, the comment stays as "deleted" to keep the thread.
 */
export async function deleteComment(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const c = await loadComment(ctx, ctx.params.commentId!);
    if (c.author_id !== auth.player.id) throw Errors.forbidden("NOT_COMMENT_AUTHOR", "Seul l'auteur d'un commentaire peut le supprimer.");
    if (c.deleted_at) return json(ctx, { deleted: true });
    await removeComment(ctx, c);
    return json(ctx, { deleted: true });
}

async function removeComment(ctx: Ctx, c: CommentRow): Promise<void> {
    const db = ctx.env.DB;
    if (c.reply_count > 0) {
        await db.prepare("UPDATE community_comments SET deleted_at = ?2, body = '', pinned_at = NULL WHERE id = ?1").bind(c.id, nowIso()).run();
        return;
    }
    const stmts = [db.prepare("DELETE FROM community_comments WHERE id = ?1").bind(c.id)];
    if (c.parent_id) stmts.push(db.prepare("UPDATE community_comments SET reply_count = MAX(0, reply_count - 1) WHERE id = ?1").bind(c.parent_id));
    else stmts.push(db.prepare("UPDATE community_levels SET comment_count = MAX(0, comment_count - 1) WHERE id = ?1").bind(c.level_id));
    await db.batch(stmts);
    // A deleted reply may leave its "deleted" parent with nothing left to show.
    if (c.parent_id) {
        const p = await db.prepare("SELECT * FROM community_comments WHERE id = ?1").bind(c.parent_id).first<CommentRow>();
        if (p && p.deleted_at && p.reply_count === 0) await removeComment(ctx, p);
    }
}

/** POST /community/comments/{id}/pin and DELETE …/pin: the level's creator, top-level comments, 3 at most. */
export async function pinComment(ctx: Ctx, pin: boolean): Promise<Response> {
    const auth = await requireAuth(ctx);
    const c = await loadComment(ctx, ctx.params.commentId!);
    const l = await ctx.env.DB.prepare("SELECT author_id FROM community_levels WHERE id = ?1").bind(c.level_id).first<{ author_id: string }>();
    if (!l || l.author_id !== auth.player.id) throw Errors.forbidden("NOT_AUTHOR", "Seul le créateur du niveau peut épingler un commentaire.");
    if (c.parent_id || c.deleted_at || c.hidden) throw Errors.forbidden("COMMENT_NOT_PINNABLE", "Ce commentaire ne peut pas être épinglé.");
    if (pin && !c.pinned_at) {
        const n = await ctx.env.DB.prepare("SELECT COUNT(*) AS n FROM community_comments WHERE level_id = ?1 AND pinned_at IS NOT NULL")
            .bind(c.level_id)
            .first<{ n: number }>();
        if ((n?.n ?? 0) >= MAX_PINNED) throw Errors.conflict("TOO_MANY_PINNED", `${MAX_PINNED} commentaires épinglés au plus.`);
    }
    await ctx.env.DB.prepare("UPDATE community_comments SET pinned_at = ?2 WHERE id = ?1").bind(c.id, pin ? nowIso() : null).run();
    return json(ctx, { pinned: pin });
}

/** PUT /community/comments/{id}/reaction {kind}: one active reaction per player, changeable. */
export async function reactComment(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.communityReaction, "player:" + me);
    const c = await loadComment(ctx, ctx.params.commentId!);
    if (c.deleted_at || (c.hidden && c.author_id !== me)) throw Errors.notFound("COMMENT_NOT_FOUND", "Commentaire introuvable.");
    const l = await publishedLevel(ctx, c.level_id, me);
    if (await blocked(ctx, me, c.author_id)) throw Errors.forbidden("BLOCKED", "Vous ne pouvez pas réagir à ce commentaire.");
    const v = new Validator(await readJsonObject(ctx), ["kind"]);
    const kind = v.oneOf("kind", REACTIONS as unknown as string[]) as Reaction;
    v.done();
    const before = await ctx.env.DB.prepare("SELECT kind FROM community_comment_reactions WHERE comment_id = ?1 AND player_id = ?2")
        .bind(c.id, me)
        .first<{ kind: Reaction }>();
    await ctx.env.DB.batch([
        ctx.env.DB.prepare(
            `INSERT INTO community_comment_reactions (comment_id, player_id, kind, created_at) VALUES (?1, ?2, ?3, ?4)
             ON CONFLICT (comment_id, player_id) DO UPDATE SET kind = excluded.kind`,
        ).bind(c.id, me, kind, nowIso()),
        recount(ctx, c.id),
    ]);
    if (!before && c.author_id !== me)
        await notify(ctx, c.author_id, "comment_reaction", {
            actor: me,
            levelId: c.level_id,
            commentId: c.id,
            data: { title: l.title, reaction: kind },
            groupKey: "reaction:" + c.id,
        });
    return reactionResult(ctx, c.id, me);
}

/** DELETE /community/comments/{id}/reaction */
export async function unreactComment(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const c = await loadComment(ctx, ctx.params.commentId!);
    await ctx.env.DB.batch([
        ctx.env.DB.prepare("DELETE FROM community_comment_reactions WHERE comment_id = ?1 AND player_id = ?2").bind(c.id, auth.player.id),
        recount(ctx, c.id),
    ]);
    return reactionResult(ctx, c.id, auth.player.id);
}

function recount(ctx: Ctx, id: string) {
    const sets = REACTIONS.map((r) => `${COLUMN[r]} = (SELECT COUNT(*) FROM community_comment_reactions WHERE comment_id = ?1 AND kind = '${r}')`);
    return ctx.env.DB.prepare(`UPDATE community_comments SET ${sets.join(", ")} WHERE id = ?1`).bind(id);
}

async function reactionResult(ctx: Ctx, id: string, me: string): Promise<Response> {
    const c = await loadComment(ctx, id);
    const mine = await ctx.env.DB.prepare("SELECT kind FROM community_comment_reactions WHERE comment_id = ?1 AND player_id = ?2").bind(id, me).first<{ kind: string }>();
    return json(ctx, {
        reactions: Object.fromEntries(REACTIONS.map((r) => [r, c[COLUMN[r] as keyof CommentRow] as number])),
        my_reaction: mine?.kind ?? null,
    });
}

/** POST /community/comments/{id}/report {reason}: hidden for everyone after 3 reports, until moderation. */
export async function reportComment(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.communityReport, "player:" + me);
    const c = await loadComment(ctx, ctx.params.commentId!);
    if (c.author_id === me) throw Errors.forbidden("OWN_COMMENT", "Vous ne pouvez pas signaler votre propre commentaire.");
    const v = new Validator(await readJsonObject(ctx), ["reason"]);
    const reason = v.oneOf("reason", COMMENT_REPORT_REASONS);
    v.done();
    const res = await ctx.env.DB.prepare(
        "INSERT OR IGNORE INTO community_comment_reports (comment_id, reporter_id, reason, created_at) VALUES (?1, ?2, ?3, ?4)",
    )
        .bind(c.id, me, reason, nowIso())
        .run();
    if (res.meta.changes > 0) {
        await ctx.env.DB.prepare(
            `UPDATE community_comments SET report_count = report_count + 1, hidden = CASE WHEN report_count + 1 >= ?2 THEN 1 ELSE hidden END WHERE id = ?1`,
        )
            .bind(c.id, REPORTS_TO_HIDE)
            .run();
        await audit(ctx, me, "comment_reported", { comment_id: c.id, reason });
    }
    return json(ctx, { reported: true });
}

// --------------------------------------------------------------- moderation

/** GET /admin/comments?status=reported (admin token): comments waiting for a decision. */
export async function adminListComments(ctx: Ctx): Promise<Response> {
    const rows = await ctx.env.DB.prepare(
        `SELECT c.id, c.level_id, c.author_id, c.body, c.created_at, c.report_count, c.hidden, p.username,
                (SELECT group_concat(reason) FROM community_comment_reports r WHERE r.comment_id = c.id) AS reasons
         FROM community_comments c JOIN players p ON p.id = c.author_id
         WHERE c.report_count > 0 AND c.deleted_at IS NULL ORDER BY c.hidden DESC, c.report_count DESC LIMIT 100`,
    ).all();
    return json(ctx, { comments: rows.results });
}

/** POST /admin/comments/{id}/moderate {action: restore|remove} (admin token). */
export async function adminModerateComment(ctx: Ctx): Promise<Response> {
    const c = await loadComment(ctx, ctx.params.commentId!);
    const v = new Validator(await readJsonObject(ctx), ["action"]);
    const action = v.oneOf("action", ["restore", "remove"]);
    v.done();
    if (action === "restore") {
        await ctx.env.DB.batch([
            ctx.env.DB.prepare("UPDATE community_comments SET hidden = 0, report_count = 0 WHERE id = ?1").bind(c.id),
            ctx.env.DB.prepare("DELETE FROM community_comment_reports WHERE comment_id = ?1").bind(c.id),
        ]);
    } else {
        await removeComment(ctx, c);
    }
    await audit(ctx, null, "comment_moderated", { comment_id: c.id, action });
    return json(ctx, { action });
}

export function assertReaction(kind: string): asserts kind is Reaction {
    if (!(REACTIONS as readonly string[]).includes(kind)) throw new ApiError(400, "VALIDATION_ERROR", "Réaction inconnue.");
}
