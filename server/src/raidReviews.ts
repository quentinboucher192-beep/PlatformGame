// 2.77.4: the players' reviews of the raid events (docs/RAIDS.md §24, migration 0185). After
// playing an event, a player gives it 1 to 5 stars and, if they wish, a comment (500 characters
// at most). One review per player and event (the table's unique key: a second write changes it),
// changed or deleted by its author only.
//
// Who may review is decided by the SERVER from its own data, never from what the game says: the
// player held a seat in an instance of the event (raid_instance_members, written by the relay) AND
// played there (a contribution accepted by the consensus of the instance: raid_contributions with
// steps in play, or a zone's table, raid_zone_contributions). Being registered, or a member of a
// clan taking part, is not enough.
//
// The moderation is the comments' own (workshop.ts): the same text filters (cleanCommentText: no
// control characters, at most two blank lines in a row, two links at most), the same rate limits
// (community_comment for the writes, community_report for the reports), the same report reasons and
// threshold (the comment hidden for everyone after 3 reports, until an administrator decides), and the
// authors a player blocks (or who block them) left out of their list. An administrator (ADMINISTRATION
// › Raids › Avis) sees every review of an event with its reports, and masks or restores a comment
// (Journal). A masked comment keeps its stars: the rating comes from a real participant, only its
// text is withheld (its author still sees it, with the reason).

import { audit, requireAuth, type AuthInfo } from "./auth";
import { requireAdminRole } from "./adminRoles";
import { Errors, json, log, nowIso, readJsonObject, type Ctx } from "./http";
import { playerSummary, type PlayerRow } from "./model";
import { journalRaid, myClanOf, visibleEvent } from "./raids";
import { RULES, rateLimit } from "./ratelimit";
import { Validator, paging } from "./validate";
import { COMMENT_REPORT_REASONS, REPORTS_TO_HIDE, cleanCommentText } from "./workshop";

/** The longest comment of a review (characters, as the Validator counts them). */
export const MAX_REVIEW_TEXT = 500;
export const MIN_STARS = 1;
export const MAX_STARS = 5;

const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

const RAID_NOT_FOUND = () => Errors.notFound("RAID_NOT_FOUND", "Raid introuvable.");
const REVIEW_NOT_FOUND = () => Errors.notFound("REVIEW_NOT_FOUND", "Avis introuvable.");
const NOT_PLAYED = () => Errors.forbidden("RAID_NOT_PLAYED", "Seuls les joueurs qui ont joué cet événement peuvent le noter.");

/** The average and the number of reviews of an event (average: one decimal, null without any review). */
export interface ReviewSummary {
    average: number | null;
    count: number;
}
export const NO_REVIEWS: ReviewSummary = { average: null, count: 0 };

interface ReviewRow {
    id: string;
    event_id: string;
    player_id: string;
    stars: number;
    body: string;
    hidden: number;
    hidden_by: string | null;
    hidden_at: string | null;
    report_count: number;
    created_at: string;
    edited_at: string | null;
}
type ReviewJoin = ReviewRow &
    Pick<PlayerRow, "username" | "avatar" | "avatar_custom_key" | "country_code" | "location_visibility"> & { reported_by_me?: number; reasons?: string | null };

/** The average of `count` reviews whose stars add up to `sum`, to one decimal (half up); null for none. */
export function averageOf(sum: number, count: number): number | null {
    return count > 0 ? Math.round((sum * 10) / count) / 10 : null;
}

function eventIdOf(ctx: Ctx): string {
    const id = ctx.params.eventId ?? "";
    if (!ID_RE.test(id)) throw RAID_NOT_FOUND();
    return id;
}

/**
 * The reviews' average and count of each event of `ids` (the cards of CLANS › RAIDS, the sheet, the
 * administration). Every review counts, its comment masked or not. Before migration 0185: none.
 */
export async function reviewSummaries(db: D1Database, ids: string[]): Promise<Map<string, ReviewSummary>> {
    const out = new Map<string, ReviewSummary>();
    const list = [...new Set(ids)];
    if (!list.length) return out;
    try {
        const r = await db
            .prepare(
                `SELECT event_id, COUNT(*) AS n, SUM(stars) AS total FROM raid_reviews
                 WHERE event_id IN (SELECT value FROM json_each(?1)) GROUP BY event_id`,
            )
            .bind(JSON.stringify(list))
            .all<{ event_id: string; n: number; total: number }>();
        for (const x of r.results) out.set(x.event_id, { average: averageOf(x.total, x.n), count: x.n });
    } catch (e) {
        log("error", "raid_reviews_unavailable", { error: e instanceof Error ? e.message : String(e) });
    }
    return out;
}

/**
 * Whether `playerId` played the event, from the server's own data only: a seat in one of its
 * instances (the relay wrote it) and a contribution there that a consensus accepted.
 */
export async function playedEvent(db: D1Database, eventId: string, playerId: string): Promise<boolean> {
    if (!playerId) return false;
    const r = await db
        .prepare(
            `SELECT EXISTS (SELECT 1 FROM raid_instance_members m JOIN raid_instances i ON i.id = m.instance_id
                            WHERE i.event_id = ?1 AND m.player_id = ?2) AS seated,
                    EXISTS (SELECT 1 FROM raid_contributions c WHERE c.event_id = ?1 AND c.player_id = ?2 AND c.ticks > 0) AS fought,
                    EXISTS (SELECT 1 FROM raid_zone_contributions z WHERE z.event_id = ?1 AND z.player_id = ?2) AS zoned`,
        )
        .bind(eventId, playerId)
        .first<{ seated: number; fought: number; zoned: number }>();
    return !!r && r.seated === 1 && (r.fought === 1 || r.zoned === 1);
}

const REVIEW_SQL = `SELECT r.*, p.username, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility,
    EXISTS (SELECT 1 FROM raid_review_reports x WHERE x.review_id = r.id AND x.reporter_id = ?1) AS reported_by_me
  FROM raid_reviews r JOIN players p ON p.id = r.player_id`;

/** The reviews `me` may see in the list: not theirs (shown apart), without the authors blocked either way. */
const LISTED = `r.event_id = ?2 AND r.player_id <> ?1
  AND r.player_id NOT IN (SELECT blocked_id FROM player_blocks WHERE player_id = ?1)
  AND r.player_id NOT IN (SELECT player_id FROM player_blocks WHERE blocked_id = ?1)`;

/** A review as `me` sees it: a masked comment only by its author (and the administrators, `admin`). */
function reviewView(r: ReviewJoin, me: string, admin = false) {
    const mine = r.player_id === me;
    const hidden = r.hidden === 1;
    return {
        id: r.id,
        author: playerSummary({
            id: r.player_id,
            username: r.username ?? "",
            avatar: r.avatar ?? "avatar_01",
            avatar_custom_key: r.avatar_custom_key ?? null,
            country_code: r.country_code ?? null,
            location_visibility: r.location_visibility ?? "hidden",
        }),
        stars: r.stars,
        body: hidden && !mine && !admin ? "" : r.body,
        has_body: r.body !== "",
        hidden,
        // « reports »: 3 reports, waiting for the administrators; « moderation »: masked by one of them.
        hidden_reason: hidden ? (r.hidden_by === "reports" ? "reports" : "moderation") : null,
        created_at: r.created_at,
        edited_at: r.edited_at,
        mine,
        reported: r.reported_by_me === 1,
        can_report: me !== "" && !mine && !hidden && r.body !== "",
        ...(admin
            ? {
                  report_count: r.report_count,
                  reasons: (r.reasons ?? "").split(",").filter((x) => x !== ""),
                  hidden_at: r.hidden_at,
                  hidden_by_reports: hidden && r.hidden_by === "reports",
              }
            : {}),
    };
}

async function summaryOf(db: D1Database, eventId: string): Promise<ReviewSummary> {
    return (await reviewSummaries(db, [eventId])).get(eventId) ?? NO_REVIEWS;
}

async function myReview(db: D1Database, eventId: string, me: string): Promise<ReviewJoin | null> {
    return await db.prepare(`${REVIEW_SQL} WHERE r.event_id = ?2 AND r.player_id = ?1`).bind(me, eventId).first<ReviewJoin>();
}

// ------------------------------------------------------------------ players

/**
 * GET /raids/events/{eventId}/reviews?limit=&offset=: the reviews of an event (whoever may see its
 * sheet), newest first, the viewer's own apart (`me.review`, shown first by the game) with whether
 * they may review it (`me.can_review`: they played it, as the server knows).
 */
export async function listReviews(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const id = eventIdOf(ctx);
    const me = viewer?.player.id ?? "";
    const db = ctx.env.DB;
    const clan = me ? await myClanOf(db, me) : null;
    await visibleEvent(db, id, clan, me);
    const { limit, offset } = paging(ctx.url, 20, 50);
    const [total, rows] = await db.batch([
        db.prepare(`SELECT COUNT(*) AS n FROM raid_reviews r WHERE ${LISTED}`).bind(me, id),
        db.prepare(`${REVIEW_SQL} WHERE ${LISTED} ORDER BY r.created_at DESC, r.id LIMIT ?3 OFFSET ?4`).bind(me, id, limit, offset),
    ]);
    const n = (total!.results[0] as { n: number } | undefined)?.n ?? 0;
    const mine = me ? await myReview(db, id, me) : null;
    const played = me ? await playedEvent(db, id, me) : false;
    return json(
        ctx,
        {
            summary: await summaryOf(db, id),
            me: me ? { played, can_review: played, review: mine ? reviewView(mine, me) : null } : null,
            reviews: (rows!.results as ReviewJoin[]).map((r) => reviewView(r, me)),
            total: n,
        },
        { meta: { limit, offset, total: n, next_offset: offset + limit < n ? offset + limit : null } },
    );
}

/**
 * PUT /raids/events/{eventId}/review {stars (1-5), body? (500)}: the player's review, written or
 * changed (one per player and event). Refused (403 RAID_NOT_PLAYED) to anyone who did not play it.
 */
export async function putReview(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.communityComment, "player:" + me);
    const id = eventIdOf(ctx);
    const v = new Validator(await readJsonObject(ctx), ["stars", "body"]);
    const stars = v.int("stars", { min: MIN_STARS, max: MAX_STARS });
    const body = cleanCommentText(v, "body", { max: MAX_REVIEW_TEXT, optional: true });
    v.done();
    const db = ctx.env.DB;
    const e = await db.prepare("SELECT id, published_at FROM raid_events WHERE id = ?1").bind(id).first<{ id: string; published_at: string | null }>();
    if (!e || !e.published_at) throw RAID_NOT_FOUND();
    if (!(await playedEvent(db, id, me))) throw NOT_PLAYED();
    const now = nowIso();
    const before = await db.prepare("SELECT id FROM raid_reviews WHERE event_id = ?1 AND player_id = ?2").bind(id, me).first<{ id: string }>();
    // The unique key keeps one review per player and event, even for two writes at once.
    await db
        .prepare(
            `INSERT INTO raid_reviews (id, event_id, player_id, stars, body, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)
             ON CONFLICT (event_id, player_id) DO UPDATE SET stars = excluded.stars, body = excluded.body, edited_at = ?6`,
        )
        .bind(crypto.randomUUID(), id, me, stars, body, now)
        .run();
    const row = (await myReview(db, id, me))!;
    return json(ctx, { review: reviewView(row, me), summary: await summaryOf(db, id), created: !before }, { status: before ? 200 : 201 });
}

/** DELETE /raids/events/{eventId}/review: the player deletes their review (its reports with it). */
export async function deleteReview(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.communityComment, "player:" + me);
    const id = eventIdOf(ctx);
    const db = ctx.env.DB;
    const r = await db.batch([
        db.prepare("DELETE FROM raid_review_reports WHERE review_id IN (SELECT id FROM raid_reviews WHERE event_id = ?1 AND player_id = ?2)").bind(id, me),
        db.prepare("DELETE FROM raid_reviews WHERE event_id = ?1 AND player_id = ?2").bind(id, me),
    ]);
    if (!r[1]!.meta.changes) throw Errors.notFound("REVIEW_NOT_FOUND", "Vous n'avez pas d'avis sur cet événement.");
    return json(ctx, { deleted: true, summary: await summaryOf(db, id) });
}

/** POST /raids/reviews/{reviewId}/report {reason}: as a comment's report (masked after 3, until an administrator decides). */
export async function reportReview(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.communityReport, "player:" + me);
    const rid = ctx.params.reviewId ?? "";
    if (!ID_RE.test(rid)) throw REVIEW_NOT_FOUND();
    const db = ctx.env.DB;
    const r = await db.prepare("SELECT * FROM raid_reviews WHERE id = ?1").bind(rid).first<ReviewRow>();
    if (!r) throw REVIEW_NOT_FOUND();
    await visibleEvent(db, r.event_id, await myClanOf(db, me), me); // a clan raid's reviews are its clans' business
    if (r.player_id === me) throw Errors.forbidden("OWN_REVIEW", "Vous ne pouvez pas signaler votre propre avis.");
    if (r.body === "") throw Errors.conflict("NOTHING_TO_REPORT", "Cet avis n'a pas de commentaire : il n'y a rien à signaler.");
    const v = new Validator(await readJsonObject(ctx), ["reason"]);
    const reason = v.oneOf("reason", COMMENT_REPORT_REASONS);
    v.done();
    const now = nowIso();
    const res = await db
        .prepare("INSERT OR IGNORE INTO raid_review_reports (review_id, reporter_id, reason, created_at) VALUES (?1, ?2, ?3, ?4)")
        .bind(r.id, me, reason, now)
        .run();
    if (res.meta.changes > 0) {
        // Every right side reads the row before the update: the third report masks it once.
        await db
            .prepare(
                `UPDATE raid_reviews SET report_count = report_count + 1,
                    hidden_by = CASE WHEN hidden = 0 AND report_count + 1 >= ?2 THEN 'reports' ELSE hidden_by END,
                    hidden_at = CASE WHEN hidden = 0 AND report_count + 1 >= ?2 THEN ?3 ELSE hidden_at END,
                    hidden = CASE WHEN report_count + 1 >= ?2 THEN 1 ELSE hidden END
                 WHERE id = ?1`,
            )
            .bind(r.id, REPORTS_TO_HIDE, now)
            .run();
        await audit(ctx, me, "raid_review_reported", { review_id: r.id, event_id: r.event_id, reason });
    }
    return json(ctx, { reported: true });
}

// ------------------------------------------------------------------ administrators

/** GET /admin/raids/{eventId}/reviews?limit=&offset=: every review of an event, masked ones and reports included. */
export async function adminListReviews(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    const id = eventIdOf(ctx);
    const db = ctx.env.DB;
    const e = await db.prepare("SELECT id FROM raid_events WHERE id = ?1").bind(id).first();
    if (!e) throw RAID_NOT_FOUND();
    const { limit, offset } = paging(ctx.url, 50, 200);
    const [total, rows] = await db.batch([
        db.prepare("SELECT COUNT(*) AS n FROM raid_reviews WHERE event_id = ?1").bind(id),
        db
            .prepare(
                `SELECT r.*, p.username, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility, 0 AS reported_by_me,
                        (SELECT group_concat(reason) FROM raid_review_reports x WHERE x.review_id = r.id) AS reasons
                 FROM raid_reviews r JOIN players p ON p.id = r.player_id WHERE r.event_id = ?1
                 ORDER BY r.report_count DESC, r.hidden DESC, r.created_at DESC, r.id LIMIT ?2 OFFSET ?3`,
            )
            .bind(id, limit, offset),
    ]);
    const n = (total!.results[0] as { n: number } | undefined)?.n ?? 0;
    return json(
        ctx,
        { summary: await summaryOf(db, id), reviews: (rows!.results as ReviewJoin[]).map((r) => reviewView(r, auth.player.id, true)), total: n },
        { meta: { limit, offset, total: n, next_offset: offset + limit < n ? offset + limit : null } },
    );
}

/**
 * POST /admin/raids/{eventId}/reviews/{reviewId}/moderate {action: hide|restore}: an administrator
 * masks a review's comment (its stars still count) or restores it (its reports cleared, as a
 * restored comment). Journal: admin_raid_review_hidden / admin_raid_review_restored.
 */
export async function adminModerateReview(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const id = eventIdOf(ctx);
    const rid = ctx.params.reviewId ?? "";
    if (!ID_RE.test(rid)) throw REVIEW_NOT_FOUND();
    const v = new Validator(await readJsonObject(ctx), ["action"]);
    const action = v.oneOf("action", ["hide", "restore"] as const);
    v.done();
    const db = ctx.env.DB;
    const r = await db
        .prepare(
            `SELECT r.*, p.username, e.raid, e.format, e.starts_at FROM raid_reviews r JOIN players p ON p.id = r.player_id
             JOIN raid_events e ON e.id = r.event_id WHERE r.id = ?1 AND r.event_id = ?2`,
        )
        .bind(rid, id)
        .first<ReviewRow & { username: string; raid: string; format: string; starts_at: string }>();
    if (!r) throw REVIEW_NOT_FOUND();
    const now = nowIso();
    if (action === "hide") {
        await db.prepare("UPDATE raid_reviews SET hidden = 1, hidden_by = ?2, hidden_at = ?3 WHERE id = ?1").bind(r.id, auth.player.id, now).run();
    } else {
        await db.batch([
            db.prepare("UPDATE raid_reviews SET hidden = 0, hidden_by = NULL, hidden_at = NULL, report_count = 0 WHERE id = ?1").bind(r.id),
            db.prepare("DELETE FROM raid_review_reports WHERE review_id = ?1").bind(r.id),
        ]);
    }
    await audit(ctx, auth.player.id, action === "hide" ? "admin_raid_review_hidden" : "admin_raid_review_restored", {
        raid: journalRaid({ id, raid: r.raid, format: r.format as "standard", starts_at: r.starts_at }, { review_author: r.username, stars: r.stars }),
        by_username: auth.player.username,
    });
    const row = await db
        .prepare(
            `SELECT r.*, p.username, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility, 0 AS reported_by_me,
                    (SELECT group_concat(reason) FROM raid_review_reports x WHERE x.review_id = r.id) AS reasons
             FROM raid_reviews r JOIN players p ON p.id = r.player_id WHERE r.id = ?1`,
        )
        .bind(r.id)
        .first<ReviewJoin>();
    return json(ctx, { review: reviewView(row!, auth.player.id, true), summary: await summaryOf(db, id) });
}
