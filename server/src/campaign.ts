// The social side of the campaign (Cosmic Online 2.19): players' ratings of the official
// levels, comments on the official levels (shown by the game in ONLINE only) and comments on
// the runs of the leaderboards. Ratings are given only by players who finished the level
// (a run verified by the server); every average is computed here from the stored ratings,
// never sent by the game. The comment threads reuse the engine of the community levels'
// comments (src/workshop.ts). The player is always identified by the session.

import { optionalAuth, requireAuth } from "./auth";
import { Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import { RULES, rateLimit } from "./ratelimit";
import { LEVEL_ID_RE, Validator } from "./validate";
import { coopThread, listThread, officialLevelThread, postToThread, raceThread, runThread } from "./workshop";

interface Summary {
    average: number | null;
    count: number;
}

function summary(sum: number, count: number): Summary {
    return { average: count > 0 ? Math.round((sum / count) * 10) / 10 : null, count };
}

async function officialLevel(ctx: Ctx, levelId: string): Promise<{ id: string; name: string }> {
    const l = LEVEL_ID_RE.test(levelId)
        ? await ctx.env.DB.prepare("SELECT id, name FROM levels WHERE id = ?1").bind(levelId).first<{ id: string; name: string }>()
        : null;
    if (!l) throw Errors.notFound("LEVEL_NOT_FOUND", "Niveau inconnu du serveur.");
    return l;
}

/**
 * Did the player finish this official level? Proof kept by the server: a VERIFIED record of
 * the level (any revision, any category), or a finished and verified attempt of its
 * « course à la vérification » (EXTRÊME levels, board 'o:<level>:<hash>').
 */
export async function finishedLevel(ctx: Ctx, playerId: string, levelId: string): Promise<boolean> {
    const prefix = `o:${levelId}:`;
    const r = await ctx.env.DB.prepare(
        `SELECT (EXISTS (SELECT 1 FROM records WHERE player_id = ?1 AND level_id = ?2 AND verification_status = 'VERIFIED')
              OR EXISTS (SELECT 1 FROM race_runs WHERE player_id = ?1 AND substr(board, 1, ?4) = ?3 AND finished = 1 AND status = 'VERIFIED')) AS ok`,
    )
        .bind(playerId, levelId, prefix, prefix.length)
        .first<{ ok: number }>();
    return r?.ok === 1;
}

/** Recomputes a level's average from its ratings (same batch as the change). */
function statsStatement(ctx: Ctx, levelId: string) {
    return ctx.env.DB.prepare(
        `INSERT INTO level_rating_stats (level_id, rating_sum, rating_count, updated_at)
         SELECT ?1, COALESCE(SUM(stars), 0), COUNT(*), ?2 FROM level_ratings WHERE level_id = ?1
         ON CONFLICT (level_id) DO UPDATE SET rating_sum = excluded.rating_sum, rating_count = excluded.rating_count, updated_at = excluded.updated_at`,
    ).bind(levelId, nowIso());
}

/** The rating of a level as one viewer sees it (average, their stars, whether they may rate). */
async function ratingView(ctx: Ctx, level: { id: string; name: string }, me: string | null) {
    const db = ctx.env.DB;
    const [stats, mine, comments] = await db.batch([
        db.prepare("SELECT rating_sum, rating_count FROM level_rating_stats WHERE level_id = ?1").bind(level.id),
        db.prepare("SELECT stars FROM level_ratings WHERE level_id = ?1 AND player_id = ?2").bind(level.id, me ?? ""),
        db.prepare(
            "SELECT COUNT(*) AS n FROM campaign_comments WHERE thread = ?1 AND parent_id IS NULL AND hidden = 0 AND deleted_at IS NULL",
        ).bind("level:" + level.id),
    ]);
    const s = (stats?.results[0] ?? { rating_sum: 0, rating_count: 0 }) as { rating_sum: number; rating_count: number };
    const stars = (mine?.results[0] as { stars: number } | undefined)?.stars ?? null;
    const canRate = me !== null && (await finishedLevel(ctx, me, level.id));
    return {
        level_id: level.id,
        name: level.name,
        rating: summary(s.rating_sum, s.rating_count),
        my_stars: stars,
        can_rate: canRate,
        reason: me === null ? "NOT_LOGGED_IN" : canRate ? null : "LEVEL_NOT_COMPLETED",
        comments: ((comments?.results[0] as { n: number } | undefined)?.n ?? 0) as number,
    };
}

// ------------------------------------------------------------------ ratings

/** GET /levels/ratings: the players' average of every rated official level (and mine). */
export async function listRatings(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const rows = await ctx.env.DB.prepare(
        `SELECT s.level_id, s.rating_sum, s.rating_count FROM level_rating_stats s JOIN levels l ON l.id = s.level_id
         WHERE s.rating_count > 0 ORDER BY l.planet_id, l.level_order, l.id`,
    ).all<{ level_id: string; rating_sum: number; rating_count: number }>();
    let mine: Record<string, number> | null = null;
    if (viewer) {
        const m = await ctx.env.DB.prepare("SELECT level_id, stars FROM level_ratings WHERE player_id = ?1").bind(viewer.player.id).all<{ level_id: string; stars: number }>();
        mine = Object.fromEntries(m.results.map((r) => [r.level_id, r.stars]));
    }
    return json(ctx, {
        levels: rows.results.map((r) => ({ level_id: r.level_id, ...summary(r.rating_sum, r.rating_count) })),
        my_ratings: mine,
    });
}

/** GET /levels/{levelId}/rating */
export async function getRating(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const level = await officialLevel(ctx, ctx.params.levelId!);
    return json(ctx, await ratingView(ctx, level, viewer?.player.id ?? null));
}

/** PUT /levels/{levelId}/rating {stars}: gives or changes one's rating (level finished first). */
export async function putRating(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.levelRate, "player:" + me);
    const level = await officialLevel(ctx, ctx.params.levelId!);
    const v = new Validator(await readJsonObject(ctx), ["stars"]);
    const stars = v.int("stars", { min: 1, max: 5 });
    v.done();
    if (!(await finishedLevel(ctx, me, level.id)))
        throw Errors.forbidden("LEVEL_NOT_COMPLETED", "Terminez ce niveau avec un temps vérifié par Cosmic Online pour le noter.");
    const now = nowIso();
    await ctx.env.DB.batch([
        ctx.env.DB.prepare(
            `INSERT INTO level_ratings (level_id, player_id, stars, created_at, updated_at) VALUES (?1, ?2, ?3, ?4, ?4)
             ON CONFLICT (level_id, player_id) DO UPDATE SET stars = excluded.stars, updated_at = excluded.updated_at`,
        ).bind(level.id, me, stars!, now),
        statsStatement(ctx, level.id),
    ]);
    return json(ctx, await ratingView(ctx, level, me));
}

/** DELETE /levels/{levelId}/rating: withdraws one's rating (idempotent). */
export async function deleteRating(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.levelRate, "player:" + me);
    const level = await officialLevel(ctx, ctx.params.levelId!);
    await ctx.env.DB.batch([
        ctx.env.DB.prepare("DELETE FROM level_ratings WHERE level_id = ?1 AND player_id = ?2").bind(level.id, me),
        statsStatement(ctx, level.id),
    ]);
    return json(ctx, await ratingView(ctx, level, me));
}

// ------------------------------------------------------------------ threads

/** GET /levels/{levelId}/comments: the comments of an official level (ONLINE). */
export async function listLevelComments(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    return listThread(ctx, await officialLevelThread(ctx, ctx.params.levelId!), viewer?.player.id ?? "");
}

/** POST /levels/{levelId}/comments {body, parent_id?} */
export async function postLevelComment(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.communityComment, "player:" + auth.player.id);
    return postToThread(ctx, await officialLevelThread(ctx, ctx.params.levelId!), auth.player.id);
}

/** GET /records/{recordId}/comments: the comments of a run of the leaderboards. */
export async function listRunComments(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const me = viewer?.player.id ?? "";
    return listThread(ctx, await runThread(ctx, ctx.params.recordId!, me || null, true), me);
}

/** POST /records/{recordId}/comments {body, parent_id?}: its author is notified. */
export async function postRunComment(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.communityComment, "player:" + auth.player.id);
    return postToThread(ctx, await runThread(ctx, ctx.params.recordId!, auth.player.id, true), auth.player.id);
}

/** GET /races/runs/{runId}/comments (2.85): the comments of an attempt of a solo race (official level). */
export async function listRaceComments(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const me = viewer?.player.id ?? "";
    return listThread(ctx, await raceThread(ctx, ctx.params.runId!, me || null, true), me);
}

/** POST /races/runs/{runId}/comments {body, parent_id?}: its player is notified. */
export async function postRaceComment(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.communityComment, "player:" + auth.player.id);
    return postToThread(ctx, await raceThread(ctx, ctx.params.runId!, auth.player.id, true), auth.player.id);
}

/** GET /coop/runs/{runId}/comments (2.85): the comments of a team's attempt (Duo, Trio). */
export async function listCoopComments(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const me = viewer?.player.id ?? "";
    return listThread(ctx, await coopThread(ctx, ctx.params.runId!, me || null, true), me);
}

/** POST /coop/runs/{runId}/comments {body, parent_id?}: every player of the team is notified. */
export async function postCoopComment(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.communityComment, "player:" + auth.player.id);
    return postToThread(ctx, await coopThread(ctx, ctx.params.runId!, auth.player.id, true), auth.player.id);
}

/**
 * 2.85: number of visible top-level comments of each attempt of a race board (`prefix` 'race:'
 * for the solo boards, 'coop:' for the Duo / Trio ones), in one statement.
 */
export async function attemptCommentCounts(ctx: Ctx, prefix: "race:" | "coop:", runIds: string[]): Promise<Map<string, number>> {
    const out = new Map<string, number>();
    if (runIds.length === 0) return out;
    const rows = await ctx.env.DB.prepare(
        `SELECT thread, COUNT(*) AS n FROM campaign_comments
         WHERE thread IN (SELECT ?2 || value FROM json_each(?1)) AND parent_id IS NULL AND hidden = 0 AND deleted_at IS NULL
         GROUP BY thread`,
    )
        .bind(JSON.stringify(runIds), prefix)
        .all<{ thread: string; n: number }>();
    for (const r of rows.results) out.set(r.thread.slice(prefix.length), r.n);
    return out;
}

/**
 * Number of visible top-level comments of each run (leaderboard rows), in one statement
 * whatever the page size.
 */
export async function runCommentCounts(ctx: Ctx, recordIds: string[]): Promise<Map<string, number>> {
    const out = new Map<string, number>();
    if (recordIds.length === 0) return out;
    const rows = await ctx.env.DB.prepare(
        `SELECT record_id, COUNT(*) AS n FROM campaign_comments
         WHERE thread IN (SELECT 'run:' || value FROM json_each(?1)) AND parent_id IS NULL AND hidden = 0 AND deleted_at IS NULL
         GROUP BY record_id`,
    )
        .bind(JSON.stringify(recordIds))
        .all<{ record_id: string; n: number }>();
    for (const r of rows.results) out.set(r.record_id, r.n);
    return out;
}

// ----------------------------------------------------------------------- GDPR

/** What the export of a player's data contains about the campaign's social side. */
export async function campaignExport(ctx: Ctx, playerId: string) {
    const db = ctx.env.DB;
    const [ratings, comments, reactions] = await db.batch([
        db.prepare("SELECT level_id, stars, created_at, updated_at FROM level_ratings WHERE player_id = ?1 ORDER BY created_at").bind(playerId),
        db.prepare(
            "SELECT id, thread, level_id, record_id, parent_id, body, created_at, edited_at, deleted_at, pinned_at FROM campaign_comments WHERE author_id = ?1 ORDER BY created_at",
        ).bind(playerId),
        db.prepare("SELECT comment_id, kind, created_at FROM campaign_comment_reactions WHERE player_id = ?1").bind(playerId),
    ]);
    return {
        level_ratings: ratings?.results ?? [],
        campaign_comments: comments?.results ?? [],
        campaign_comment_reactions: reactions?.results ?? [],
    };
}

/**
 * Statements run just before the player's row is deleted (same batch): the averages and the
 * counters they took part in are recomputed without them (their ratings, reactions and
 * replies then go with the account through ON DELETE CASCADE).
 */
export function campaignBeforeAccountDeletion(ctx: Ctx, playerId: string) {
    const db = ctx.env.DB;
    const reactions = ["like", "love", "funny", "impressive", "not_useful"]
        .map((k) => `r_${k} = (SELECT COUNT(*) FROM campaign_comment_reactions x WHERE x.comment_id = campaign_comments.id AND x.kind = '${k}' AND x.player_id <> ?1)`)
        .join(", ");
    return [
        // 2.85: the threads of their solo race attempts go with them (race_runs ON DELETE CASCADE).
        db.prepare("DELETE FROM campaign_comments WHERE thread IN (SELECT 'race:' || id FROM race_runs WHERE player_id = ?1)").bind(playerId),
        db.prepare(
            `UPDATE level_rating_stats SET
               rating_sum = (SELECT COALESCE(SUM(stars), 0) FROM level_ratings r WHERE r.level_id = level_rating_stats.level_id AND r.player_id <> ?1),
               rating_count = (SELECT COUNT(*) FROM level_ratings r WHERE r.level_id = level_rating_stats.level_id AND r.player_id <> ?1),
               updated_at = ?2
             WHERE level_id IN (SELECT level_id FROM level_ratings WHERE player_id = ?1)`,
        ).bind(playerId, nowIso()),
        db.prepare(`UPDATE campaign_comments SET ${reactions} WHERE id IN (SELECT comment_id FROM campaign_comment_reactions WHERE player_id = ?1)`).bind(playerId),
        db.prepare(
            `UPDATE campaign_comments SET reply_count = (SELECT COUNT(*) FROM campaign_comments r WHERE r.parent_id = campaign_comments.id AND r.author_id <> ?1)
             WHERE id IN (SELECT parent_id FROM campaign_comments WHERE author_id = ?1 AND parent_id IS NOT NULL)`,
        ).bind(playerId),
    ];
}

/** After the deletion (same batch): "deleted" comments whose replies all went away. */
export function campaignAfterAccountDeletion(ctx: Ctx) {
    return [ctx.env.DB.prepare("DELETE FROM campaign_comments WHERE deleted_at IS NOT NULL AND reply_count = 0")];
}
