// Workshop (Cosmic Online 2.9): favorites, comments, replies, reactions and their moderation
// on the community levels. A level's creator can pin comments but never delete the comments
// of others (negative reviews stay): only their authors can, or the moderation after
// reports. Since 2.19 the same comment engine serves the official levels of the campaign and
// the runs of the leaderboards (src/campaign.ts). The player is always identified by the
// session.

import { audit, optionalAuth, requireAuth } from "./auth";
import { sha256Hex } from "./crypto";
import { ApiError, Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import { playerSummary, type PlayerRow } from "./model";
import { notify } from "./notifications";
import { RULES, rateLimit } from "./ratelimit";
import { LEVEL_ID_RE, Validator, paging } from "./validate";

const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
export const REACTIONS = ["like", "love", "funny", "impressive", "not_useful"] as const;
type Reaction = (typeof REACTIONS)[number];
const COLUMN: Record<Reaction, string> = { like: "r_like", love: "r_love", funny: "r_funny", impressive: "r_impressive", not_useful: "r_not_useful" };
const MAX_COMMENT = 1000;
const MAX_PINNED = 3;
// 2.77.4: shared with the reviews of the raids (raidReviews.ts): the same reports, the same threshold.
export const REPORTS_TO_HIDE = 3;
export const COMMENT_REPORT_REASONS = ["spam", "harassment", "inappropriate", "spoiler", "other"];

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
//
// One comment engine for every thread: a community level, an official level of the campaign
// (shown in ONLINE only) and a run of the leaderboards (2.19). Same length, links, anti-spam,
// rate limits, replies, reactions, reports, moderation and deletion rules everywhere; threads
// only differ by where their comments are stored and by who owns their subject.

/** Where the comments of a kind of thread are stored. */
export interface Store {
    name: "community" | "campaign";
    comments: string;
    reactions: string;
    reports: string;
    /** Column holding the thread's key. */
    threadCol: string;
}
export const COMMUNITY_STORE: Store = {
    name: "community",
    comments: "community_comments",
    reactions: "community_comment_reactions",
    reports: "community_comment_reports",
    threadCol: "level_id",
};
export const CAMPAIGN_STORE: Store = {
    name: "campaign",
    comments: "campaign_comments",
    reactions: "campaign_comment_reactions",
    reports: "campaign_comment_reports",
    threadCol: "thread",
};

/** What a thread is about, resolved by the server (never described by the client). */
export interface Thread {
    store: Store;
    /** Value of the store's thread column: community level id, 'level:<id>', 'run:<record id>',
     *  'race:<race_runs id>' (2.85: an attempt of a solo race) or 'coop:<coop_runs id>' (a team's). */
    key: string;
    kind: "community" | "level" | "run" | "race" | "coop";
    /** Owner of the subject (level creator, run author): badge, pins, notifications. Null: official level, team. */
    ownerId: string | null;
    /** 2.85: every owner of the subject when there are several (a team's players); else [ownerId]. */
    owners?: string[];
    /** 2.85: the attempt a race thread is about, returned with its comments (as a row of its board). */
    subject?: Record<string, unknown>;
    /** Name of the subject in notifications. */
    title: string;
    /** Level of the subject (community or official level id): notifications.level_id. */
    levelId: string;
    recordId: string | null;
    /** Why new comments are refused, or null. */
    closed: { code: string; message: string } | null;
    /** Copied into the notifications (campaign threads: where to open them). */
    notifyData: Record<string, unknown>;
}

interface CommentRow {
    id: string;
    level_id: string;
    thread?: string;
    record_id?: string | null;
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

const commentSql = (s: Store) => `SELECT c.*, p.username, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility,
    (SELECT kind FROM ${s.reactions} x WHERE x.comment_id = c.id AND x.player_id = ?1) AS my_reaction,
    (SELECT 1 FROM ${s.reports} x WHERE x.comment_id = c.id AND x.reporter_id = ?1) AS reported_by_me,
    EXISTS (SELECT 1 FROM community_levels cl WHERE cl.author_id = c.author_id AND cl.status = 'published') AS is_creator
  FROM ${s.comments} c JOIN players p ON p.id = c.author_id`;

// ------------------------------------------------------------------ threads

/** A community level's thread. `strict`: the viewer must be able to see the level. */
async function communityThread(ctx: Ctx, levelId: string, viewer: string | null, strict: boolean): Promise<Thread> {
    let l: LevelRow | null;
    if (strict) l = await publishedLevel(ctx, levelId, viewer);
    else
        l = await ctx.env.DB.prepare("SELECT id, author_id, title, status, current_version FROM community_levels WHERE id = ?1").bind(levelId).first<LevelRow>();
    const level = l ?? { id: levelId, author_id: "", title: "", status: "deleted", current_version: 0 };
    return {
        store: COMMUNITY_STORE,
        key: level.id,
        kind: "community",
        ownerId: level.author_id || null,
        title: level.title,
        levelId: level.id,
        recordId: null,
        closed: level.status !== "published" ? { code: "LEVEL_NOT_PUBLISHED", message: "Les commentaires s'ouvrent quand le niveau est publié." } : null,
        notifyData: {},
    };
}

/** The thread of an official level of the campaign (ONLINE): open to every connected player. */
export async function officialLevelThread(ctx: Ctx, levelId: string): Promise<Thread> {
    const l = LEVEL_ID_RE.test(levelId)
        ? await ctx.env.DB.prepare("SELECT id, name FROM levels WHERE id = ?1").bind(levelId).first<{ id: string; name: string }>()
        : null;
    if (!l) throw Errors.notFound("LEVEL_NOT_FOUND", "Niveau inconnu du serveur.");
    const key = "level:" + l.id;
    return { store: CAMPAIGN_STORE, key, kind: "level", ownerId: null, title: l.name, levelId: l.id, recordId: null, closed: null, notifyData: { thread: key, level_id: l.id } };
}

/**
 * The thread of a run (a record of the leaderboards). Others only see the verified runs of
 * active accounts, and nothing of a player who hid their profile (`strict`); its author
 * always sees it. Comments open once the run is verified.
 */
export async function runThread(ctx: Ctx, recordId: string, viewer: string | null, strict: boolean): Promise<Thread> {
    if (!ID_RE.test(recordId)) throw Errors.notFound("RECORD_NOT_FOUND", "Record introuvable.");
    const r = await ctx.env.DB.prepare(
        `SELECT r.id, r.player_id, r.level_id, r.verification_status, p.username, p.profile_visibility, p.account_status, l.name AS level_name
         FROM records r JOIN players p ON p.id = r.player_id JOIN levels l ON l.id = r.level_id WHERE r.id = ?1`,
    )
        .bind(recordId)
        .first<{
            id: string;
            player_id: string;
            level_id: string;
            verification_status: string;
            username: string;
            profile_visibility: string;
            account_status: string;
            level_name: string;
        }>();
    if (!r) throw Errors.notFound("RECORD_NOT_FOUND", "Record introuvable.");
    if (strict && viewer !== r.player_id) {
        if (r.verification_status !== "VERIFIED" || r.account_status !== "active") throw Errors.notFound("RECORD_NOT_FOUND", "Record introuvable.");
        if (r.profile_visibility === "private")
            throw Errors.forbidden("PROFILE_PRIVATE", "Le joueur a masqué son profil : les commentaires de ses courses sont fermés.");
    }
    const key = "run:" + r.id;
    return {
        store: CAMPAIGN_STORE,
        key,
        kind: "run",
        ownerId: r.player_id,
        title: `${r.level_name} (course de ${r.username})`,
        levelId: r.level_id,
        recordId: r.id,
        closed: r.verification_status !== "VERIFIED" ? { code: "RUN_NOT_VERIFIED", message: "Les commentaires s'ouvrent quand la course est vérifiée." } : null,
        notifyData: { thread: key, level_id: r.level_id, level_name: r.level_name, record_id: r.id },
    };
}

/** The owners of a thread's subject: its badge, its pins and its notifications. */
const ownersOf = (t: Thread): string[] => t.owners ?? (t.ownerId ? [t.ownerId] : []);
/** A thread about an attempt (a run of the leaderboards, of a race, of a team). */
const aboutRun = (t: Thread) => t.kind === "run" || t.kind === "race" || t.kind === "coop";

/** The official level of a race board ('o:<level>:<hash>'): its comments live with the campaign's. */
async function raceLevel(ctx: Ctx, board: string): Promise<{ id: string; name: string }> {
    const [k, levelId] = board.split(":");
    // The races of the community levels have no comments (campaign_comments names an official level).
    if (k !== "o" || !levelId) throw Errors.forbidden("RACE_COMMENTS_CLOSED", "Les tentatives ne se commentent que sur les niveaux de la campagne.");
    const l = await ctx.env.DB.prepare("SELECT id, name FROM levels WHERE id = ?1").bind(levelId).first<{ id: string; name: string }>();
    if (!l) throw Errors.notFound("RUN_NOT_FOUND", "Tentative introuvable.");
    return l;
}

/**
 * 2.85: the thread of an attempt of a solo « course à la vérification » (race_runs) of an official
 * level, with the rules of a run's: others only see the verified attempts of active accounts, and
 * nothing of a player who hid their profile (`strict`); its author always sees it, and is notified.
 */
export async function raceThread(ctx: Ctx, runId: string, viewer: string | null, strict: boolean): Promise<Thread> {
    if (!ID_RE.test(runId)) throw Errors.notFound("RUN_NOT_FOUND", "Tentative introuvable.");
    const r = await ctx.env.DB.prepare(
        `SELECT r.id, r.board, r.player_id, r.status, r.score, r.finished, r.time_ms, r.verified_at, r.replay_key IS NOT NULL AS has_replay,
                p.username, p.avatar, p.profile_visibility, p.account_status
         FROM race_runs r JOIN players p ON p.id = r.player_id WHERE r.id = ?1`,
    )
        .bind(runId)
        .first<{
            id: string;
            board: string;
            player_id: string;
            status: string;
            score: number | null;
            finished: number | null;
            time_ms: number;
            verified_at: string | null;
            has_replay: number;
            username: string;
            avatar: string;
            profile_visibility: string;
            account_status: string;
        }>();
    if (!r) throw Errors.notFound("RUN_NOT_FOUND", "Tentative introuvable.");
    if (strict && viewer !== r.player_id) {
        if (r.status !== "VERIFIED" || r.account_status !== "active") throw Errors.notFound("RUN_NOT_FOUND", "Tentative introuvable.");
        if (r.profile_visibility === "private")
            throw Errors.forbidden("PROFILE_PRIVATE", "Le joueur a masqué son profil : les commentaires de ses courses sont fermés.");
    }
    const l = await raceLevel(ctx, r.board);
    const key = "race:" + r.id;
    return {
        store: CAMPAIGN_STORE,
        key,
        kind: "race",
        ownerId: r.player_id,
        title: `${l.name} (course de ${r.username})`,
        levelId: l.id,
        recordId: null,
        closed: r.status !== "VERIFIED" ? { code: "RUN_NOT_VERIFIED", message: "Les commentaires s'ouvrent quand la course est vérifiée." } : null,
        notifyData: { thread: key, level_id: l.id, level_name: l.name, run_id: r.id },
        subject: {
            size: 1,
            run_id: r.id,
            level_id: l.id,
            player: { id: r.player_id, username: r.username, avatar: r.avatar },
            score: r.score ?? 0,
            finished: r.finished === 1,
            time_ms: r.time_ms,
            at: r.verified_at,
            has_replay: r.has_replay === 1,
            me: r.player_id === viewer,
        },
    };
}

/**
 * 2.85: the thread of a team's attempt (coop_runs, Duo or Trio) of an official level. Others only
 * see a ranked attempt (verified, confirmed by all its players); its players always see it, may
 * pin, and are all notified.
 */
export async function coopThread(ctx: Ctx, runId: string, viewer: string | null, strict: boolean): Promise<Thread> {
    if (!ID_RE.test(runId)) throw Errors.notFound("RUN_NOT_FOUND", "Tentative introuvable.");
    const db = ctx.env.DB;
    const r = await db
        .prepare(
            `SELECT r.id, r.board, r.size, r.status, r.score, r.revives, r.incomplete, r.finished, r.time_ms, r.verified_at, r.replay_key IS NOT NULL AS has_replay,
                    NOT EXISTS (SELECT 1 FROM coop_run_members m WHERE m.run_id = r.id
                                AND NOT EXISTS (SELECT 1 FROM coop_confirms c WHERE c.attempt_id = r.attempt_id AND c.player_id = m.player_id)) AS confirmed
             FROM coop_runs r WHERE r.id = ?1`,
        )
        .bind(runId)
        .first<{
            id: string;
            board: string;
            size: number;
            status: string;
            score: number | null;
            revives: number | null;
            incomplete: number | null;
            finished: number | null;
            time_ms: number;
            verified_at: string | null;
            has_replay: number;
            confirmed: number;
        }>();
    if (!r) throw Errors.notFound("RUN_NOT_FOUND", "Tentative introuvable.");
    const members = (
        await db
            .prepare("SELECT p.id, p.username, p.avatar FROM coop_run_members m JOIN players p ON p.id = m.player_id WHERE m.run_id = ?1 ORDER BY m.seat")
            .bind(r.id)
            .all<{ id: string; username: string; avatar: string }>()
    ).results;
    const ranked = r.status === "VERIFIED" && r.confirmed === 1;
    const member = viewer !== null && members.some((m) => m.id === viewer);
    if (strict && !member && !ranked) throw Errors.notFound("RUN_NOT_FOUND", "Tentative introuvable.");
    const l = await raceLevel(ctx, r.board);
    const key = "coop:" + r.id;
    return {
        store: CAMPAIGN_STORE,
        key,
        kind: "coop",
        ownerId: null,
        owners: members.map((m) => m.id),
        title: `${l.name} (course de ${members.map((m) => m.username).join(" · ")})`,
        levelId: l.id,
        recordId: null,
        closed: ranked ? null : { code: "RUN_NOT_VERIFIED", message: "Les commentaires s'ouvrent quand la course est vérifiée." },
        notifyData: { thread: key, level_id: l.id, level_name: l.name, run_id: r.id, size: r.size },
        subject: {
            size: r.size,
            run_id: r.id,
            level_id: l.id,
            players: members,
            score: r.score ?? 0,
            revives: r.revives ?? 0,
            incomplete: r.incomplete === 1,
            finished: r.finished === 1,
            time_ms: r.time_ms,
            at: r.verified_at,
            has_replay: r.has_replay === 1,
            me: member,
        },
    };
}

async function campaignThread(ctx: Ctx, key: string, viewer: string | null, strict: boolean): Promise<Thread> {
    if (key.startsWith("level:")) return officialLevelThread(ctx, key.slice(6));
    if (key.startsWith("run:")) return runThread(ctx, key.slice(4), viewer, strict);
    if (key.startsWith("race:")) return raceThread(ctx, key.slice(5), viewer, strict);
    if (key.startsWith("coop:")) return coopThread(ctx, key.slice(5), viewer, strict);
    throw Errors.notFound("COMMENT_NOT_FOUND", "Commentaire introuvable.");
}

function threadOf(ctx: Ctx, store: Store, c: CommentRow, viewer: string | null, strict: boolean): Promise<Thread> {
    return store.name === "community" ? communityThread(ctx, c.level_id, viewer, strict) : campaignThread(ctx, c.thread ?? "", viewer, strict);
}

/** What a viewer may see of a comment: hidden ones only by their author; deleted ones as a placeholder. */
function commentView(c: CommentJoin, me: string, t: Thread) {
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
        // The owner of the subject: the level's creator, or the run's author.
        level_author: !deleted && ownersOf(t).includes(c.author_id),
        thread: t.kind,
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
        can_pin: !deleted && me !== "" && t.kind !== "level" && ownersOf(t).includes(me) && c.parent_id === null,
    };
}

/** Comments visible to `me`: not hidden (except one's own), without blocked authors. */
const VISIBLE = `(c.hidden = 0 OR c.author_id = ?1)
  AND c.author_id NOT IN (SELECT blocked_id FROM player_blocks WHERE player_id = ?1)
  AND c.author_id NOT IN (SELECT player_id FROM player_blocks WHERE blocked_id = ?1)`;

/** Top-level comments of a thread, pinned first: `?sort=recent|popular|oldest&limit=&offset=`. */
export async function listThread(ctx: Ctx, t: Thread, me: string): Promise<Response> {
    const s = t.store;
    const sort = ctx.url.searchParams.get("sort") ?? "recent";
    const order = {
        recent: "c.created_at DESC",
        oldest: "c.created_at ASC",
        popular: "(c.r_like + c.r_love + c.r_funny + c.r_impressive - c.r_not_useful + 2 * c.reply_count) DESC, c.created_at DESC",
    }[sort];
    if (!order) throw Errors.validation([{ field: "sort", message: "recent, popular ou oldest" }]);
    const { limit, offset } = paging(ctx.url, 20, 50);
    const where = `c.${s.threadCol} = ?2 AND c.parent_id IS NULL AND ${VISIBLE}`;
    const total = await ctx.env.DB.prepare(`SELECT COUNT(*) AS n FROM ${s.comments} c WHERE ${where}`).bind(me, t.key).first<{ n: number }>();
    const rows = await ctx.env.DB.prepare(`${commentSql(s)} WHERE ${where} ORDER BY (c.pinned_at IS NOT NULL) DESC, c.pinned_at DESC, ${order} LIMIT ?3 OFFSET ?4`)
        .bind(me, t.key, limit, offset)
        .all<CommentJoin>();
    const n = total?.n ?? 0;
    return json(ctx, { comments: rows.results.map((c) => commentView(c, me, t)), sort, comment_count: n, ...(t.subject ? { attempt: t.subject } : {}) }, {
        meta: { limit, offset, total: n, next_offset: offset + limit < n ? offset + limit : null },
    });
}

/** GET /community/levels/{id}/comments?sort=recent|popular|oldest&limit=&offset= : top-level comments, pinned first. */
export async function listComments(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const me = viewer?.player.id ?? "";
    return listThread(ctx, await communityThread(ctx, ctx.params.levelId!, me || null, true), me);
}

/** GET /community/comments/{id}/replies?limit=&offset= (oldest first), in any thread. */
export async function listReplies(ctx: Ctx): Promise<Response> {
    const viewer = await optionalAuth(ctx);
    const me = viewer?.player.id ?? "";
    const { c: parent, store } = await loadComment(ctx, ctx.params.commentId!);
    const t = await threadOf(ctx, store, parent, me || null, true);
    const { limit, offset } = paging(ctx.url, 20, 50);
    const where = `c.parent_id = ?2 AND ${VISIBLE}`;
    const total = await ctx.env.DB.prepare(`SELECT COUNT(*) AS n FROM ${store.comments} c WHERE ${where}`).bind(me, parent.id).first<{ n: number }>();
    const rows = await ctx.env.DB.prepare(`${commentSql(store)} WHERE ${where} ORDER BY c.created_at ASC LIMIT ?3 OFFSET ?4`)
        .bind(me, parent.id, limit, offset)
        .all<CommentJoin>();
    const n = total?.n ?? 0;
    return json(ctx, { replies: rows.results.map((c) => commentView(c, me, t)) }, {
        meta: { limit, offset, total: n, next_offset: offset + limit < n ? offset + limit : null },
    });
}

/** A comment of any thread, with the store that holds it. */
async function loadComment(ctx: Ctx, id: string): Promise<{ c: CommentRow; store: Store }> {
    if (!ID_RE.test(id)) throw Errors.notFound("COMMENT_NOT_FOUND", "Commentaire introuvable.");
    for (const store of [COMMUNITY_STORE, CAMPAIGN_STORE]) {
        const c = await ctx.env.DB.prepare(`SELECT * FROM ${store.comments} WHERE id = ?1`).bind(id).first<CommentRow>();
        if (c) return { c, store };
    }
    throw Errors.notFound("COMMENT_NOT_FOUND", "Commentaire introuvable.");
}

async function loadIn(ctx: Ctx, store: Store, id: string): Promise<CommentRow> {
    if (!ID_RE.test(id)) throw Errors.notFound("COMMENT_NOT_FOUND", "Commentaire introuvable.");
    const c = await ctx.env.DB.prepare(`SELECT * FROM ${store.comments} WHERE id = ?1`).bind(id).first<CommentRow>();
    if (!c) throw Errors.notFound("COMMENT_NOT_FOUND", "Commentaire introuvable.");
    return c;
}

/** Text of a comment: trimmed, at most two blank lines in a row, few links (spam). */
function cleanBody(v: Validator, field = "body"): string {
    return cleanCommentText(v, field, { max: MAX_COMMENT, optional: false });
}

/**
 * The filters of every comment's text (2.77.4: also the reviews of the raids, raidReviews.ts):
 * no control characters, `max` characters, at most two blank lines in a row, two links at most.
 * `optional`: an empty text is allowed ("").
 */
export function cleanCommentText(v: Validator, field: string, opt: { max: number; optional: boolean }): string {
    const raw = v.str(field, { min: opt.optional ? 0 : 1, max: opt.max, trim: true, multiline: true, optional: opt.optional }) ?? "";
    const body = raw.replace(/\r\n?/g, "\n").replace(/\n{3,}/g, "\n\n").trim();
    if ((body.match(/https?:\/\//gi) ?? []).length > 2) v.fail(field, "trop de liens");
    if (!body && !opt.optional) v.fail(field, "obligatoire");
    return body;
}

/**
 * Posts `{body, parent_id?}` in a thread for `me` (connected, rate limit already counted).
 * A reply to a reply goes under the same comment; the subject's owner (level creator, run
 * author) is notified of new top-level comments, a comment's author of the replies.
 */
export async function postToThread(ctx: Ctx, t: Thread, me: string): Promise<Response> {
    const s = t.store;
    if (t.closed) throw Errors.forbidden(t.closed.code, t.closed.message);
    for (const owner of ownersOf(t))
        if (await blocked(ctx, me, owner))
            throw Errors.forbidden("BLOCKED", aboutRun(t) ? "Vous ne pouvez pas commenter cette course." : "Vous ne pouvez pas commenter ce niveau.");
    const v = new Validator(await readJsonObject(ctx), ["body", "parent_id"]);
    const body = cleanBody(v);
    const parentId = v.str("parent_id", { max: 36, optional: true });
    v.done();
    let parent: CommentRow | null = null;
    if (parentId) {
        parent = await loadIn(ctx, s, parentId);
        if ((parent as unknown as Record<string, unknown>)[s.threadCol] !== t.key)
            throw Errors.validation([{ field: "parent_id", message: t.kind === "community" ? "commentaire d'un autre niveau" : "commentaire d'un autre fil" }]);
        if (parent.parent_id) parent = await loadIn(ctx, s, parent.parent_id); // one level of replies
        if (parent.deleted_at || parent.hidden) throw Errors.forbidden("COMMENT_CLOSED", "On ne peut plus répondre à ce commentaire.");
        if (await blocked(ctx, me, parent.author_id)) throw Errors.forbidden("BLOCKED", "Vous ne pouvez pas répondre à ce commentaire.");
    }
    // Anti-spam: the same text twice in a day in the same thread is refused.
    const hash = await sha256Hex(new TextEncoder().encode(body.toLowerCase().replace(/\s+/g, " ")));
    const since = new Date(Date.now() - 86400 * 1000).toISOString();
    const dup = await ctx.env.DB.prepare(
        `SELECT 1 AS x FROM ${s.comments} WHERE author_id = ?1 AND ${s.threadCol} = ?2 AND body_hash = ?3 AND created_at > ?4 AND deleted_at IS NULL`,
    )
        .bind(me, t.key, hash, since)
        .first();
    if (dup) throw Errors.conflict("COMMENT_DUPLICATE", "Vous avez déjà publié ce commentaire.");
    const id = crypto.randomUUID();
    const now = nowIso();
    const stmts =
        s.name === "community"
            ? [
                  ctx.env.DB.prepare(
                      "INSERT INTO community_comments (id, level_id, author_id, parent_id, body, body_hash, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
                  ).bind(id, t.key, me, parent?.id ?? null, body, hash, now),
              ]
            : [
                  ctx.env.DB.prepare(
                      `INSERT INTO campaign_comments (id, thread, level_id, record_id, author_id, parent_id, body, body_hash, created_at)
                       VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)`,
                  ).bind(id, t.key, t.levelId, t.recordId, me, parent?.id ?? null, body, hash, now),
              ];
    if (parent) stmts.push(ctx.env.DB.prepare(`UPDATE ${s.comments} SET reply_count = reply_count + 1 WHERE id = ?1`).bind(parent.id));
    else if (s.name === "community") stmts.push(ctx.env.DB.prepare("UPDATE community_levels SET comment_count = comment_count + 1 WHERE id = ?1").bind(t.key));
    await ctx.env.DB.batch(stmts);
    if (parent)
        await notify(ctx, parent.author_id, "comment_reply", {
            actor: me,
            levelId: t.levelId,
            commentId: parent.id,
            data: { title: t.title, ...t.notifyData },
            groupKey: "reply:" + parent.id,
        });
    else if (t.kind === "community" && t.ownerId)
        await notify(ctx, t.ownerId, "level_comment", { actor: me, levelId: t.levelId, commentId: id, data: { title: t.title }, groupKey: "comments:" + t.key });
    else if (aboutRun(t))
        for (const owner of ownersOf(t))
            await notify(ctx, owner, "run_comment", {
                actor: me,
                levelId: t.levelId,
                commentId: id,
                data: { title: t.title, ...t.notifyData },
                groupKey: "comments:" + t.key,
            });
    const row = await ctx.env.DB.prepare(`${commentSql(s)} WHERE c.id = ?2`).bind(me, id).first<CommentJoin>();
    return json(ctx, { comment: commentView(row!, me, t) }, { status: 201 });
}

/** POST /community/levels/{id}/comments {body, parent_id?} */
export async function postComment(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.communityComment, "player:" + me);
    return postToThread(ctx, await communityThread(ctx, ctx.params.levelId!, me, true), me);
}

/** PATCH /community/comments/{id} {body}: its author only, in any thread. */
export async function editComment(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.communityComment, "player:" + me);
    const { c, store } = await loadComment(ctx, ctx.params.commentId!);
    if (c.author_id !== me) throw Errors.forbidden("NOT_COMMENT_AUTHOR", "Ce commentaire ne vous appartient pas.");
    if (c.deleted_at) throw Errors.notFound("COMMENT_NOT_FOUND", "Commentaire introuvable.");
    const v = new Validator(await readJsonObject(ctx), ["body"]);
    const body = cleanBody(v);
    v.done();
    const hash = await sha256Hex(new TextEncoder().encode(body.toLowerCase().replace(/\s+/g, " ")));
    await ctx.env.DB.prepare(`UPDATE ${store.comments} SET body = ?2, body_hash = ?3, edited_at = ?4 WHERE id = ?1`).bind(c.id, body, hash, nowIso()).run();
    const t = await threadOf(ctx, store, c, me, false);
    const row = await ctx.env.DB.prepare(`${commentSql(store)} WHERE c.id = ?2`).bind(me, c.id).first<CommentJoin>();
    return json(ctx, { comment: commentView(row!, me, t) });
}

/**
 * DELETE /community/comments/{id}: its author only (the level's creator or the run's author
 * cannot delete the comments of others). With replies, the comment stays as "deleted" to
 * keep the thread.
 */
export async function deleteComment(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const { c, store } = await loadComment(ctx, ctx.params.commentId!);
    if (c.author_id !== auth.player.id) throw Errors.forbidden("NOT_COMMENT_AUTHOR", "Seul l'auteur d'un commentaire peut le supprimer.");
    if (c.deleted_at) return json(ctx, { deleted: true });
    await removeComment(ctx, store, c);
    return json(ctx, { deleted: true });
}

async function removeComment(ctx: Ctx, store: Store, c: CommentRow): Promise<void> {
    const db = ctx.env.DB;
    if (c.reply_count > 0) {
        await db.prepare(`UPDATE ${store.comments} SET deleted_at = ?2, body = '', pinned_at = NULL WHERE id = ?1`).bind(c.id, nowIso()).run();
        return;
    }
    const stmts = [db.prepare(`DELETE FROM ${store.comments} WHERE id = ?1`).bind(c.id)];
    if (c.parent_id) stmts.push(db.prepare(`UPDATE ${store.comments} SET reply_count = MAX(0, reply_count - 1) WHERE id = ?1`).bind(c.parent_id));
    else if (store.name === "community")
        stmts.push(db.prepare("UPDATE community_levels SET comment_count = MAX(0, comment_count - 1) WHERE id = ?1").bind(c.level_id));
    await db.batch(stmts);
    // A deleted reply may leave its "deleted" parent with nothing left to show.
    if (c.parent_id) {
        const p = await db.prepare(`SELECT * FROM ${store.comments} WHERE id = ?1`).bind(c.parent_id).first<CommentRow>();
        if (p && p.deleted_at && p.reply_count === 0) await removeComment(ctx, store, p);
    }
}

/**
 * POST /community/comments/{id}/pin and DELETE …/pin: the owner of the subject (the level's
 * creator, the run's author; nobody on an official level), top-level comments, 3 at most.
 */
export async function pinComment(ctx: Ctx, pin: boolean): Promise<Response> {
    const auth = await requireAuth(ctx);
    const { c, store } = await loadComment(ctx, ctx.params.commentId!);
    const t = await threadOf(ctx, store, c, auth.player.id, false);
    if (t.kind === "level") throw Errors.forbidden("NOT_AUTHOR", "Les commentaires d'un niveau officiel ne s'épinglent pas.");
    if (!ownersOf(t).includes(auth.player.id))
        throw Errors.forbidden(
            "NOT_AUTHOR",
            t.kind === "coop"
                ? "Seuls les joueurs de l'équipe peuvent épingler un commentaire."
                : aboutRun(t)
                  ? "Seul l'auteur de la course peut épingler un commentaire."
                  : "Seul le créateur du niveau peut épingler un commentaire.",
        );
    if (c.parent_id || c.deleted_at || c.hidden) throw Errors.forbidden("COMMENT_NOT_PINNABLE", "Ce commentaire ne peut pas être épinglé.");
    if (pin && !c.pinned_at) {
        const n = await ctx.env.DB.prepare(`SELECT COUNT(*) AS n FROM ${store.comments} WHERE ${store.threadCol} = ?1 AND pinned_at IS NOT NULL`)
            .bind(t.key)
            .first<{ n: number }>();
        if ((n?.n ?? 0) >= MAX_PINNED) throw Errors.conflict("TOO_MANY_PINNED", `${MAX_PINNED} commentaires épinglés au plus.`);
    }
    await ctx.env.DB.prepare(`UPDATE ${store.comments} SET pinned_at = ?2 WHERE id = ?1`).bind(c.id, pin ? nowIso() : null).run();
    return json(ctx, { pinned: pin });
}

/** PUT /community/comments/{id}/reaction {kind}: one active reaction per player, changeable. */
export async function reactComment(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.communityReaction, "player:" + me);
    const { c, store } = await loadComment(ctx, ctx.params.commentId!);
    if (c.deleted_at || (c.hidden && c.author_id !== me)) throw Errors.notFound("COMMENT_NOT_FOUND", "Commentaire introuvable.");
    const t = await threadOf(ctx, store, c, me, true);
    if (await blocked(ctx, me, c.author_id)) throw Errors.forbidden("BLOCKED", "Vous ne pouvez pas réagir à ce commentaire.");
    const v = new Validator(await readJsonObject(ctx), ["kind"]);
    const kind = v.oneOf("kind", REACTIONS as unknown as string[]) as Reaction;
    v.done();
    const before = await ctx.env.DB.prepare(`SELECT kind FROM ${store.reactions} WHERE comment_id = ?1 AND player_id = ?2`)
        .bind(c.id, me)
        .first<{ kind: Reaction }>();
    await ctx.env.DB.batch([
        ctx.env.DB.prepare(
            `INSERT INTO ${store.reactions} (comment_id, player_id, kind, created_at) VALUES (?1, ?2, ?3, ?4)
             ON CONFLICT (comment_id, player_id) DO UPDATE SET kind = excluded.kind`,
        ).bind(c.id, me, kind, nowIso()),
        recount(ctx, store, c.id),
    ]);
    if (!before && c.author_id !== me)
        await notify(ctx, c.author_id, "comment_reaction", {
            actor: me,
            levelId: t.levelId,
            commentId: c.id,
            data: { title: t.title, reaction: kind, ...t.notifyData },
            groupKey: "reaction:" + c.id,
        });
    return reactionResult(ctx, store, c.id, me);
}

/** DELETE /community/comments/{id}/reaction */
export async function unreactComment(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const { c, store } = await loadComment(ctx, ctx.params.commentId!);
    await ctx.env.DB.batch([
        ctx.env.DB.prepare(`DELETE FROM ${store.reactions} WHERE comment_id = ?1 AND player_id = ?2`).bind(c.id, auth.player.id),
        recount(ctx, store, c.id),
    ]);
    return reactionResult(ctx, store, c.id, auth.player.id);
}

function recount(ctx: Ctx, store: Store, id: string) {
    const sets = REACTIONS.map((r) => `${COLUMN[r]} = (SELECT COUNT(*) FROM ${store.reactions} WHERE comment_id = ?1 AND kind = '${r}')`);
    return ctx.env.DB.prepare(`UPDATE ${store.comments} SET ${sets.join(", ")} WHERE id = ?1`).bind(id);
}

async function reactionResult(ctx: Ctx, store: Store, id: string, me: string): Promise<Response> {
    const c = await loadIn(ctx, store, id);
    const mine = await ctx.env.DB.prepare(`SELECT kind FROM ${store.reactions} WHERE comment_id = ?1 AND player_id = ?2`).bind(id, me).first<{ kind: string }>();
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
    const { c, store } = await loadComment(ctx, ctx.params.commentId!);
    if (c.author_id === me) throw Errors.forbidden("OWN_COMMENT", "Vous ne pouvez pas signaler votre propre commentaire.");
    const v = new Validator(await readJsonObject(ctx), ["reason"]);
    const reason = v.oneOf("reason", COMMENT_REPORT_REASONS);
    v.done();
    const res = await ctx.env.DB.prepare(`INSERT OR IGNORE INTO ${store.reports} (comment_id, reporter_id, reason, created_at) VALUES (?1, ?2, ?3, ?4)`)
        .bind(c.id, me, reason, nowIso())
        .run();
    if (res.meta.changes > 0) {
        await ctx.env.DB.prepare(
            `UPDATE ${store.comments} SET report_count = report_count + 1, hidden = CASE WHEN report_count + 1 >= ?2 THEN 1 ELSE hidden END WHERE id = ?1`,
        )
            .bind(c.id, REPORTS_TO_HIDE)
            .run();
        await audit(ctx, me, "comment_reported", { comment_id: c.id, reason });
    }
    return json(ctx, { reported: true });
}

// --------------------------------------------------------------- moderation

/** GET /admin/comments?status=reported (admin token): comments of every thread waiting for a decision. */
export async function adminListComments(ctx: Ctx): Promise<Response> {
    const query = (store: Store) =>
        ctx.env.DB.prepare(
            `SELECT c.id, c.${store.threadCol} AS thread_key, c.author_id, c.body, c.created_at, c.report_count, c.hidden, p.username,
                    (SELECT group_concat(reason) FROM ${store.reports} r WHERE r.comment_id = c.id) AS reasons
             FROM ${store.comments} c JOIN players p ON p.id = c.author_id
             WHERE c.report_count > 0 AND c.deleted_at IS NULL ORDER BY c.hidden DESC, c.report_count DESC LIMIT 100`,
        ).all<Record<string, unknown>>();
    const [community, campaign] = await Promise.all([query(COMMUNITY_STORE), query(CAMPAIGN_STORE)]);
    const rows: Record<string, unknown>[] = [
        // Community comments keep their level_id; campaign ones say their thread (level:… or run:…).
        ...community.results.map(({ thread_key, ...r }) => ({ ...r, level_id: thread_key, thread: "community" })),
        ...campaign.results.map(({ thread_key, ...r }) => ({ ...r, level_id: null, thread: thread_key })),
    ];
    rows.sort((a, b) => Number(b.hidden) - Number(a.hidden) || Number(b.report_count) - Number(a.report_count));
    rows.splice(100);
    return json(ctx, { comments: rows });
}

/** POST /admin/comments/{id}/moderate {action: restore|remove} (admin token), in any thread. */
export async function adminModerateComment(ctx: Ctx): Promise<Response> {
    const { c, store } = await loadComment(ctx, ctx.params.commentId!);
    const v = new Validator(await readJsonObject(ctx), ["action"]);
    const action = v.oneOf("action", ["restore", "remove"]);
    v.done();
    if (action === "restore") {
        await ctx.env.DB.batch([
            ctx.env.DB.prepare(`UPDATE ${store.comments} SET hidden = 0, report_count = 0 WHERE id = ?1`).bind(c.id),
            ctx.env.DB.prepare(`DELETE FROM ${store.reports} WHERE comment_id = ?1`).bind(c.id),
        ]);
    } else {
        await removeComment(ctx, store, c);
    }
    await audit(ctx, null, "comment_moderated", { comment_id: c.id, action });
    return json(ctx, { action });
}

export function assertReaction(kind: string): asserts kind is Reaction {
    if (!(REACTIONS as readonly string[]).includes(kind)) throw new ApiError(400, "VALIDATION_ERROR", "Réaction inconnue.");
}
