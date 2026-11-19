// Player profiles, privacy settings, statistics, search, GDPR export and account deletion,
// custom avatars.

import { clanAfterAccountDeletion, clanOfPlayer } from "./clans";
import { progressCopyKeys } from "./devices";
import { adminPresenceOf, presenceOf } from "./friends";
import { adminViewing, auditAdminView, profileLocks, statusLock, type AdminLock } from "./adminView";
import { achievementSummary } from "./achievements";
import { profileCosmetics } from "./cosmetics";
import { profileBadges } from "./badges";
import { creatorStats, followState, playtime } from "./creators";
import { audit, countryExists, releaseClosedUsernames, requireAuth, type AuthInfo } from "./auth";
import { verifyPassword } from "./crypto";
import { ApiError, Errors, json, nowIso, readJsonObject, readLimited, type Ctx } from "./http";
import { playerSummary, privateProfile, recordJson, visibleCountry, type PlayerRow, type RecordRow } from "./model";
import { campaignAfterAccountDeletion, campaignBeforeAccountDeletion, campaignExport } from "./campaign";
import { challengeReplayKeys } from "./challenges";
import { communityKeys } from "./community";
import { ownedProjectKeys } from "./projects";
import { RULES, rateLimit } from "./ratelimit";
import { clearSpectateRoom } from "./spectate";
import {
    AVATAR_RE,
    CATEGORIES,
    COUNTRY_RE,
    LEVEL_ID_RE,
    LOCATION_VISIBILITY,
    PROFILE_VISIBILITY,
    REGION_RE,
    USERNAME_RE,
    Validator,
    isReservedUsername,
    likePrefix,
    normalizeUsername,
    paging,
    queryEnum,
    queryString,
} from "./validate";

const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

type ProfileRow = PlayerRow & { follows_visibility?: string | null };

/** A player; 2.75.3: an administrator (the session's role, `viewer`) also reads an account that is not active. */
async function loadPlayer(ctx: Ctx, id: string, viewer: AuthInfo | null = null): Promise<ProfileRow> {
    if (!ID_RE.test(id)) throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    const p = await ctx.env.DB.prepare("SELECT * FROM players WHERE id = ?1").bind(id).first<ProfileRow>();
    if (!p || (p.account_status !== "active" && !adminViewing(viewer, p.id))) throw Errors.notFound("PLAYER_NOT_FOUND", "Joueur introuvable.");
    return p;
}

/**
 * 2.75.3: the locks of a profile's own pages (masked profile, account not active) an administrator
 * goes through; [] for anyone else, and for an administrator on a profile without them.
 */
function gateLocks(p: ProfileRow, viewer: AuthInfo | null): AdminLock[] {
    if (!adminViewing(viewer, p.id)) return [];
    return profileLocks(p).filter((l) => l === "profile_private" || l === "account_inactive");
}

/** The relation between the viewer and a player: friends, a request sent or received, or none. */
async function friendshipOf(ctx: Ctx, viewer: AuthInfo | null, playerId: string): Promise<string | null> {
    if (!viewer) return null;
    const f = await ctx.env.DB.prepare(
        `SELECT player_id, status FROM player_friends WHERE (player_id = ?1 AND friend_id = ?2) OR (player_id = ?2 AND friend_id = ?1)`,
    )
        .bind(viewer.player.id, playerId)
        .all<{ player_id: string; status: string }>();
    if (f.results.some((r) => r.status === "accepted")) return "friends";
    if (f.results.some((r) => r.player_id === viewer.player.id)) return "request_sent";
    if (f.results.length) return "request_received";
    return null;
}

/**
 * 2.75.3: a profile as an administrator sees it (moderation): everything the other players see
 * on a public profile, even when it is masked or the account not active, and `admin_view` — the
 * locks the others meet, the privacy settings (the country's setting only: a hidden country stays
 * hidden), the real presence and activity, the friends, the clan requests (never their message),
 * the records not verified. Written to the Journal (grouped).
 */
async function adminProfile(ctx: Ctx, admin: AuthInfo, p: ProfileRow): Promise<Record<string, unknown>> {
    const db = ctx.env.DB;
    const locks = profileLocks(p);
    const friendship = await friendshipOf(ctx, admin, p.id);
    const real = adminPresenceOf(p);
    const [countsR, friendsR, clanR] = await db.batch([
        db
            .prepare(
                `SELECT
                   (SELECT COUNT(*) FROM player_friends f JOIN players q ON q.id = f.friend_id
                      WHERE f.player_id = ?1 AND f.status = 'accepted' AND q.account_status = 'active') AS friends,
                   (SELECT COUNT(*) FROM player_friends WHERE friend_id = ?1 AND status = 'pending') AS incoming,
                   (SELECT COUNT(*) FROM player_friends WHERE player_id = ?1 AND status = 'pending') AS outgoing,
                   (SELECT COUNT(*) FROM records WHERE player_id = ?1 AND verification_status = 'PENDING') AS pending,
                   (SELECT COUNT(*) FROM records WHERE player_id = ?1 AND verification_status = 'REJECTED') AS rejected`,
            )
            .bind(p.id),
        db
            .prepare(
                `SELECT f.created_at AS since, q.* FROM player_friends f JOIN players q ON q.id = f.friend_id
                 WHERE f.player_id = ?1 AND f.status = 'accepted' AND q.account_status = 'active'
                 ORDER BY q.username_normalized LIMIT 100`,
            )
            .bind(p.id),
        db
            .prepare(
                `SELECT q.kind, q.created_at, c.id, c.tag, c.name FROM clan_requests q JOIN clans c ON c.id = q.clan_id
                 WHERE q.player_id = ?1 ORDER BY q.created_at DESC LIMIT 20`,
            )
            .bind(p.id),
    ]);
    const n = (countsR!.results[0] ?? {}) as Record<string, number>;
    await auditAdminView(ctx, admin, "admin_profile_viewed", { id: p.id, username: p.username }, locks);
    return {
        player: {
            ...playerSummary(p),
            region_code: visibleCountry(p) ? p.region_code : null,
            created_at: p.created_at,
            // What the other players meet (the game shows it all to the administrator, marked).
            hidden: p.profile_visibility === "private",
            friendship,
            ...(friendship === "friends" ? { presence: presenceOf(p) } : {}),
        },
        statistics: await statistics(ctx, p.id),
        creator: await creatorStats(ctx, p.id),
        follow: await followState(ctx, admin.player.id, p.id),
        playtime: await playtime(ctx, p.id),
        achievements: await achievementSummary(ctx, p.id),
        cosmetics: await profileCosmetics(ctx, p.id),
        badges: await profileBadges(ctx, p.id),
        clan: await clanOfPlayer(db, p.id),
        admin_view: {
            locks,
            account_status: p.account_status,
            closed_at: p.closed_at ?? null,
            created_at: p.created_at,
            last_seen_at: p.last_seen_at,
            presence: real.presence,
            presence_hidden: real.presence_hidden,
            privacy: {
                profile_visibility: p.profile_visibility,
                location_visibility: p.location_visibility,
                allow_friend_requests: p.allow_friend_requests === 1,
                presence_visible: p.presence_visible !== 0,
                follows_visibility: p.follows_visibility ?? "public",
                spectate_allowed: p.spectate_allowed !== 0,
            },
            friends: {
                total: n.friends ?? 0,
                incoming: n.incoming ?? 0,
                outgoing: n.outgoing ?? 0,
                list: (friendsR!.results as (PlayerRow & { since: string })[]).map((f) => ({ player: playerSummary(f), since: f.since })),
            },
            clan_requests: (clanR!.results as { kind: string; created_at: string; id: string; tag: string; name: string }[]).map((q) => ({
                kind: q.kind,
                clan: { id: q.id, tag: q.tag, name: q.name },
                created_at: q.created_at,
            })),
            records: { pending: n.pending ?? 0, rejected: n.rejected ?? 0 },
        },
    };
}

function isSelf(viewer: AuthInfo | null, id: string): boolean {
    return viewer?.player.id === id;
}

async function statistics(ctx: Ctx, playerId: string) {
    const counts = await ctx.env.DB.prepare(
        `SELECT
           (SELECT COUNT(*) FROM records WHERE player_id = ?1 AND verification_status = 'VERIFIED') AS verified_records,
           (SELECT COUNT(*) FROM records WHERE player_id = ?1) AS submitted_records,
           (SELECT COUNT(*) FROM player_bests b JOIN levels l ON l.content_hash = b.level_hash WHERE b.player_id = ?1) AS personal_bests,
           (SELECT COUNT(DISTINCT b.level_id) FROM player_bests b JOIN levels l ON l.content_hash = b.level_hash
              WHERE b.player_id = ?1 AND b.category = 'ANY_PERCENT') AS levels_completed`,
    )
        .bind(playerId)
        .first<{ verified_records: number; submitted_records: number; personal_bests: number; levels_completed: number }>();
    // First places on current revisions (ties broken by the earliest record).
    const first = await ctx.env.DB.prepare(
        `SELECT COUNT(*) AS n FROM player_bests b JOIN levels l ON l.content_hash = b.level_hash
         WHERE b.player_id = ?1 AND NOT EXISTS (
           SELECT 1 FROM player_bests o JOIN players op ON op.id = o.player_id
           WHERE o.level_hash = b.level_hash AND o.category = b.category AND op.account_status = 'active' AND o.player_id <> b.player_id
             AND (o.time_ms < b.time_ms OR (o.time_ms = b.time_ms AND o.achieved_at < b.achieved_at)))`,
    )
        .bind(playerId)
        .first<{ n: number }>();
    const bests = await ctx.env.DB.prepare(
        `SELECT b.level_id, l.name AS level_name, l.planet_id, b.category, b.time_ms, b.record_id, b.achieved_at,
           (SELECT COUNT(*) FROM player_bests o JOIN players op ON op.id = o.player_id
             WHERE o.level_hash = b.level_hash AND o.category = b.category AND op.account_status = 'active'
               AND (o.time_ms < b.time_ms OR (o.time_ms = b.time_ms AND (o.achieved_at < b.achieved_at
                    OR (o.achieved_at = b.achieved_at AND o.player_id < b.player_id))))) + 1 AS rank
         FROM player_bests b JOIN levels l ON l.content_hash = b.level_hash
         WHERE b.player_id = ?1 ORDER BY l.planet_id, l.level_order, b.category`,
    )
        .bind(playerId)
        .all<{ level_id: string; level_name: string; planet_id: string; category: string; time_ms: number; record_id: string; achieved_at: string; rank: number }>();
    const planets = await ctx.env.DB.prepare(
        `SELECT l.planet_id, MAX(l.planet_name) AS planet_name, COUNT(*) AS levels,
                SUM(CASE WHEN EXISTS (SELECT 1 FROM player_bests b WHERE b.player_id = ?1 AND b.level_hash = l.content_hash
                                       AND b.category = 'ANY_PERCENT') THEN 1 ELSE 0 END) AS done
         FROM levels l WHERE l.ranking_enabled = 1 GROUP BY l.planet_id ORDER BY l.planet_id`,
    )
        .bind(playerId)
        .all<{ planet_id: string; planet_name: string; levels: number; done: number }>();
    return {
        verified_records: counts?.verified_records ?? 0,
        submitted_records: counts?.submitted_records ?? 0,
        personal_bests: counts?.personal_bests ?? 0,
        levels_completed: counts?.levels_completed ?? 0,
        first_places: first?.n ?? 0,
        planets: planets.results.map((p) => ({ planet_id: p.planet_id, planet_name: p.planet_name, levels: p.levels, completed_levels: p.done, completed: p.done === p.levels })),
        planets_completed: planets.results.filter((p) => p.done === p.levels).length,
        best_times: bests.results,
    };
}

/** GET /players/{id}: public profile (reduced when the profile is private). */
export async function getPlayer(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const p = await loadPlayer(ctx, ctx.params.playerId!, viewer);
    if (isSelf(viewer, p.id))
        return json(ctx, {
            player: { ...privateProfile(p), hidden: false, follows_visibility: (p as unknown as { follows_visibility?: string }).follows_visibility ?? "public" },
            statistics: await statistics(ctx, p.id),
            creator: await creatorStats(ctx, p.id),
            playtime: await playtime(ctx, p.id),
            achievements: await achievementSummary(ctx, p.id),
            cosmetics: await profileCosmetics(ctx, p.id),
            badges: await profileBadges(ctx, p.id),
            clan: await clanOfPlayer(ctx.env.DB, p.id),
        });
    // 2.75.3: an administrator (the session's role, never a parameter) sees everything, marked.
    if (adminViewing(viewer, p.id)) return json(ctx, await adminProfile(ctx, viewer!, p));
    if (p.profile_visibility === "private")
        return json(ctx, {
            player: { ...playerSummary(p), hidden: true },
            statistics: null,
            creator: await creatorStats(ctx, p.id),
            follow: await followState(ctx, viewer?.player.id ?? null, p.id),
            playtime: null,
            achievements: null,
            cosmetics: null,
            badges: null,
            // 2.66: a clan is public (its page lists its members): its tag shows even here.
            clan: await clanOfPlayer(ctx.env.DB, p.id),
        });
    const friendship = await friendshipOf(ctx, viewer, p.id);
    return json(ctx, {
        player: {
            ...playerSummary(p), region_code: visibleCountry(p) ? p.region_code : null, created_at: p.created_at, hidden: false, friendship,
            // Presence (2.33.1): friends only, as in the friends list.
            ...(friendship === "friends" ? { presence: presenceOf(p) } : {}),
        },
        statistics: await statistics(ctx, p.id),
        creator: await creatorStats(ctx, p.id),
        follow: await followState(ctx, viewer?.player.id ?? null, p.id),
        playtime: await playtime(ctx, p.id),
        achievements: await achievementSummary(ctx, p.id),
        cosmetics: await profileCosmetics(ctx, p.id),
        // 2.41: the badges chosen in the game, only those still valid (achievements, stars).
        badges: await profileBadges(ctx, p.id),
        // 2.66: the player's clan (tag, name, emblem, role), null without one.
        clan: await clanOfPlayer(ctx.env.DB, p.id),
    });
}

/** GET /players/{id}/statistics */
export async function getStatistics(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const p = await loadPlayer(ctx, ctx.params.playerId!, viewer);
    const admin = adminViewing(viewer, p.id);
    if (!isSelf(viewer, p.id) && !admin && p.profile_visibility === "private") throw Errors.forbidden("PROFILE_PRIVATE", "Le joueur a masqué son profil.");
    // 2.75.3: an administrator through a masked profile or an account not active: marked, journaled.
    const locks = gateLocks(p, viewer);
    if (locks.length) await auditAdminView(ctx, viewer!, "admin_profile_viewed", { id: p.id, username: p.username }, locks);
    return json(ctx, { player_id: p.id, statistics: await statistics(ctx, p.id), ...(admin ? { admin_view: { locks } } : {}) });
}

/**
 * GET /players/{id}/records: verified records for others, every status for the owner. 2.75.3: and
 * for an administrator (the session's role), even on a masked profile or an account not active,
 * each with `admin_view` (its status, the reason of a refusal; a record pending or refused never
 * counts as verified).
 */
export async function getPlayerRecords(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const p = await loadPlayer(ctx, ctx.params.playerId!, viewer);
    const self = isSelf(viewer, p.id);
    const admin = !self && adminViewing(viewer, p.id);
    if (!self && !admin && p.profile_visibility === "private") throw Errors.forbidden("PROFILE_PRIVATE", "Le joueur a masqué son profil.");
    const page = paging(ctx.url, 50, 200);
    const levelId = queryString(ctx.url, "level_id", LEVEL_ID_RE);
    const category = ctx.url.searchParams.get("category") ? queryEnum(ctx.url, "category", CATEGORIES, "ANY_PERCENT") : undefined;
    const status = self || admin ? queryString(ctx.url, "status", /^(PENDING|VERIFIED|REJECTED)$/) : "VERIFIED";
    const where = ["player_id = ?1"];
    const binds: unknown[] = [p.id];
    if (levelId) {
        binds.push(levelId);
        where.push(`level_id = ?${binds.length}`);
    }
    if (category) {
        binds.push(category);
        where.push(`category = ?${binds.length}`);
    }
    if (status) {
        binds.push(status);
        where.push(`verification_status = ?${binds.length}`);
    }
    const sql = `SELECT *, COUNT(*) OVER () AS total FROM records WHERE ${where.join(" AND ")} ORDER BY created_at DESC LIMIT ?${binds.length + 1} OFFSET ?${binds.length + 2}`;
    const rows = await ctx.env.DB.prepare(sql).bind(...binds, page.limit, page.offset).all<RecordRow & { total: number }>();
    const total = rows.results[0]?.total ?? 0;
    const meta = { meta: { limit: page.limit, offset: page.offset, total, next_offset: page.offset + page.limit < total ? page.offset + page.limit : null } };
    if (!admin) return json(ctx, { player_id: p.id, records: rows.results.map((r) => recordJson(r, { owner: self })) }, meta);
    // 2.75.3: the administrator's view: each record's status and what stops the others.
    const gate = gateLocks(p, viewer);
    const replays = new Map<string, string>();
    const ids = rows.results.map((r) => r.replay_id).filter((x): x is string => !!x);
    if (ids.length) {
        const m = await ctx.env.DB.prepare("SELECT id, verification_status FROM replay_metadata WHERE id IN (SELECT value FROM json_each(?1))")
            .bind(JSON.stringify(ids))
            .all<{ id: string; verification_status: string }>();
        for (const r of m.results) replays.set(r.id, r.verification_status);
    }
    const lifted = new Set<string>(gate);
    const records = rows.results.map((r) => {
        const sl = statusLock(r.verification_status);
        if (sl) lifted.add(sl);
        return {
            ...recordJson(r, { owner: false }),
            admin_view: {
                locks: sl ? [...gate, sl] : gate,
                status: r.verification_status,
                rejection_reason: r.rejection_reason,
                verification_notes: r.verification_notes,
                replay_status: r.replay_id ? (replays.get(r.replay_id) ?? null) : null,
            },
        };
    });
    if (lifted.size) await auditAdminView(ctx, viewer!, "admin_profile_viewed", { id: p.id, username: p.username }, [...lifted]);
    return json(ctx, { player_id: p.id, records, admin_view: { locks: gate } }, meta);
}

/** GET /players/search?q=: pseudonym prefix search (public identities only). */
export async function searchPlayers(ctx: Ctx): Promise<Response> {
    const q = queryString(ctx.url, "q", /^[A-Za-z0-9_.-]{2,20}$/);
    if (!q) throw Errors.validation([{ field: "q", message: "2 caractères minimum" }]);
    const rows = await ctx.env.DB.prepare(
        `SELECT * FROM players WHERE username_normalized LIKE ?1 ESCAPE '\\' AND account_status = 'active'
         ORDER BY length(username_normalized), username_normalized LIMIT 20`,
    )
        .bind(likePrefix(normalizeUsername(q)))
        .all<PlayerRow>();
    return json(ctx, { players: rows.results.map(playerSummary) });
}

/** PATCH /players/me: profile and privacy settings. */
export async function patchMe(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.profileWrite, "player:" + auth.player.id);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, [
        "username", "avatar", "country_code", "region_code", "location_visibility", "profile_visibility", "allow_friend_requests", "follows_visibility",
        "presence_visible", "spectate_allowed",
    ]);
    const username = v.str("username", { min: 3, max: 20, pattern: USERNAME_RE, trim: true, optional: true });
    const avatar = v.str("avatar", { max: 16, pattern: AVATAR_RE, optional: true });
    // country_code / region_code accept null to clear them.
    const clearCountry = body.country_code === null;
    const clearRegion = body.region_code === null;
    const country = v.str("country_code", { max: 2, pattern: COUNTRY_RE, optional: true });
    const region = v.str("region_code", { max: 6, pattern: REGION_RE, optional: true });
    const locVis = v.oneOf("location_visibility", LOCATION_VISIBILITY, { optional: true });
    const profVis = v.oneOf("profile_visibility", PROFILE_VISIBILITY, { optional: true });
    const allowFriends = v.bool("allow_friend_requests", { optional: true });
    const followsVis = v.oneOf("follows_visibility", ["public", "private"], { optional: true });
    const presenceVisible = v.bool("presence_visible", { optional: true });
    const spectateAllowed = v.bool("spectate_allowed", { optional: true });
    if (username && isReservedUsername(username)) v.fail("username", "pseudonyme réservé");
    v.done();
    const p = auth.player;
    const next = {
        username: username ?? p.username,
        avatar: avatar ?? p.avatar,
        country: clearCountry ? null : (country ?? p.country_code),
        region: clearRegion || clearCountry ? null : (region ?? p.region_code),
        locVis: locVis ?? p.location_visibility,
        profVis: profVis ?? p.profile_visibility,
        allowFriends: allowFriends === undefined ? p.allow_friend_requests : allowFriends ? 1 : 0,
    };
    // Without a country nothing can be shown: visibility falls back to hidden.
    if (!next.country) next.locVis = "hidden";
    if (next.region && (!next.country || !next.region.startsWith(next.country + "-")))
        throw Errors.validation([{ field: "region_code", message: "la région doit appartenir au pays choisi" }]);
    if (country && !(await countryExists(ctx, country))) throw Errors.validation([{ field: "country_code", message: "pays inconnu" }]);
    // 2.50: the username of an account closed by a migration more than 30 days ago is free again.
    if (username && normalizeUsername(username) !== p.username_normalized) await releaseClosedUsernames(ctx.env.DB, normalizeUsername(username)).run();
    try {
        await ctx.env.DB.prepare(
            `UPDATE players SET username = ?1, username_normalized = ?2, avatar = ?3, country_code = ?4, region_code = ?5,
               location_visibility = ?6, profile_visibility = ?7, allow_friend_requests = ?8 WHERE id = ?9`,
        )
            .bind(next.username, normalizeUsername(next.username), next.avatar, next.country, next.region, next.locVis, next.profVis, next.allowFriends, p.id)
            .run();
    } catch (e) {
        if (String(e).includes("UNIQUE")) throw Errors.conflict("USERNAME_TAKEN", "Ce pseudonyme est déjà utilisé.");
        throw e;
    }
    if (followsVis) await ctx.env.DB.prepare("UPDATE players SET follows_visibility = ?2 WHERE id = ?1").bind(p.id, followsVis).run();
    if (presenceVisible !== undefined)
        await ctx.env.DB.prepare("UPDATE players SET presence_visible = ?2 WHERE id = ?1").bind(p.id, presenceVisible ? 1 : 0).run();
    if (spectateAllowed !== undefined)
        await ctx.env.DB.prepare("UPDATE players SET spectate_allowed = ?2 WHERE id = ?1").bind(p.id, spectateAllowed ? 1 : 0).run();
    // Nobody may watch any more (setting off, or the presence hidden): the run kept in the
    // player's live room is forgotten at once.
    if (spectateAllowed === false || presenceVisible === false) await clearSpectateRoom(ctx.env, p.id);
    if (avatar && p.avatar === "custom" && p.avatar_custom_key) {
        await ctx.env.STORAGE.delete(p.avatar_custom_key);
        await ctx.env.DB.prepare("UPDATE players SET avatar_custom_key = NULL WHERE id = ?1").bind(p.id).run();
    }
    const changed = Object.keys(body);
    if (username && username !== p.username) await audit(ctx, p.id, "username_changed", { from: p.username, to: username });
    if (changed.some((k) => k.endsWith("visibility") || k.endsWith("_visible") || k === "spectate_allowed" || k.startsWith("country") || k.startsWith("region")))
        await audit(ctx, p.id, "privacy_changed", { fields: changed });
    const fresh = await ctx.env.DB.prepare("SELECT * FROM players WHERE id = ?1").bind(p.id).first<PlayerRow>();
    return json(ctx, { player: privateProfile(fresh!) });
}

// -------------------------------------------------------------------- GDPR

/** GET /players/me/export: every personal data item stored about the player. */
export async function exportMe(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const id = auth.player.id;
    const [sessions, records, friends, replays, auditRows, conversations, messages, blocks, cLevels, cRecords, cRatings, favs, comments, reactions, follows, notes, playtimeRows, projectRows, projectMessages, raceRows, achievementRows, cosmeticRows, badgeRows] = await ctx.env.DB.batch([
        ctx.env.DB.prepare("SELECT id, created_at, last_used_at, expires_at, refresh_expires_at, revoked_at, client_info FROM player_sessions WHERE player_id = ?1").bind(id),
        ctx.env.DB.prepare("SELECT * FROM records WHERE player_id = ?1 ORDER BY created_at").bind(id),
        ctx.env.DB.prepare("SELECT player_id, friend_id, status, created_at FROM player_friends WHERE player_id = ?1 OR friend_id = ?1").bind(id),
        ctx.env.DB.prepare("SELECT id, record_id, attempt_id, file_size, sha256, format_version, frame_count, duration_ms, verification_status, created_at FROM replay_metadata WHERE player_id = ?1").bind(id),
        ctx.env.DB.prepare("SELECT action, details, created_at FROM audit_log WHERE player_id = ?1 ORDER BY created_at").bind(id),
        ctx.env.DB.prepare(
            `SELECT c.id, c.kind, c.name, m.role, m.joined_at, m.last_read_at FROM conversation_members m
             JOIN conversations c ON c.id = m.conversation_id WHERE m.player_id = ?1 ORDER BY m.joined_at`,
        ).bind(id),
        ctx.env.DB.prepare("SELECT id, conversation_id, body, created_at, deleted_at FROM messages WHERE sender_id = ?1 ORDER BY created_at").bind(id),
        ctx.env.DB.prepare("SELECT blocked_id, created_at FROM player_blocks WHERE player_id = ?1").bind(id),
        ctx.env.DB.prepare(
            "SELECT id, title, description, planet, difficulty, status, current_version, downloads, completions, created_at, updated_at FROM community_levels WHERE author_id = ?1",
        ).bind(id),
        ctx.env.DB.prepare("SELECT id, level_id, version, time_ms, deaths, status, created_at FROM community_records WHERE player_id = ?1").bind(id),
        ctx.env.DB.prepare("SELECT level_id, stars, created_at FROM community_ratings WHERE player_id = ?1").bind(id),
        ctx.env.DB.prepare("SELECT level_id, level_title, author_name, created_at FROM community_favorites WHERE player_id = ?1").bind(id),
        ctx.env.DB.prepare("SELECT id, level_id, parent_id, body, created_at, edited_at, deleted_at, pinned_at FROM community_comments WHERE author_id = ?1").bind(id),
        ctx.env.DB.prepare("SELECT comment_id, kind, created_at FROM community_comment_reactions WHERE player_id = ?1").bind(id),
        ctx.env.DB.prepare("SELECT follower_id, creator_id, notify, created_at FROM player_follows WHERE follower_id = ?1 OR creator_id = ?1").bind(id),
        ctx.env.DB.prepare("SELECT kind, level_id, data, count, created_at, read_at FROM notifications WHERE player_id = ?1 ORDER BY created_at").bind(id),
        ctx.env.DB.prepare("SELECT level_key, seconds, updated_at FROM player_playtime WHERE player_id = ?1").bind(id),
        ctx.env.DB.prepare(
            "SELECT m.project_id, p.name AS project, m.role, m.joined_at FROM project_members m JOIN projects p ON p.id = m.project_id WHERE m.player_id = ?1",
        ).bind(id),
        ctx.env.DB.prepare("SELECT id, project_id, body, reply_to, created_at FROM project_messages WHERE author_id = ?1 ORDER BY id").bind(id),
        ctx.env.DB.prepare(
            "SELECT id, board, status, score, route, bosses_beaten, waves, finished, time_ms, created_at, verified_at FROM race_runs WHERE player_id = ?1 ORDER BY created_at",
        ).bind(id),
        ctx.env.DB.prepare("SELECT ids, stars, count, updated_at FROM player_achievements WHERE player_id = ?1").bind(id),
        ctx.env.DB.prepare("SELECT suit, trail, frame, title, victory, updated_at FROM player_cosmetics WHERE player_id = ?1").bind(id),
        ctx.env.DB.prepare("SELECT ids, updated_at FROM player_badges WHERE player_id = ?1").bind(id),
    ]);
    await audit(ctx, id, "data_export");
    return json(ctx, {
        exported_at: nowIso(),
        player: privateProfile(auth.player),
        sessions: sessions?.results ?? [],
        records: records?.results ?? [],
        friends: friends?.results ?? [],
        replays: replays?.results ?? [],
        audit_log: auditRows?.results ?? [],
        conversations: conversations?.results ?? [],
        messages_sent: messages?.results ?? [],
        blocked_players: blocks?.results ?? [],
        community_levels: cLevels?.results ?? [],
        community_records: cRecords?.results ?? [],
        community_ratings: cRatings?.results ?? [],
        community_favorites: favs?.results ?? [],
        community_comments: comments?.results ?? [],
        comment_reactions: reactions?.results ?? [],
        follows: follows?.results ?? [],
        notifications: notes?.results ?? [],
        playtime: playtimeRows?.results ?? [],
        projects: projectRows?.results ?? [],
        project_messages: projectMessages?.results ?? [],
        race_attempts: raceRows?.results ?? [],
        achievements: achievementRows?.results ?? [],
        cosmetics: cosmeticRows?.results ?? [],
        badges: badgeRows?.results ?? [],
        // 2.19: ratings of the official levels, comments of the campaign and of the runs.
        ...(await campaignExport(ctx, id)),
        // 2.66: the player's clan (role, since when) and their pending invitations or applications.
        clan: await ctx.env.DB.prepare(
            `SELECT c.id, c.name, c.tag, m.role, m.joined_at FROM clan_members m JOIN clans c ON c.id = m.clan_id WHERE m.player_id = ?1`,
        )
            .bind(id)
            .first(),
        clan_requests: (
            await ctx.env.DB.prepare(
                `SELECT q.clan_id, c.tag, q.kind, q.message, q.created_at FROM clan_requests q JOIN clans c ON c.id = q.clan_id
                 WHERE q.player_id = ?1 ORDER BY q.created_at`,
            )
                .bind(id)
                .all()
        ).results,
        // 2.31: runs of the daily challenges.
        challenge_runs: (
            await ctx.env.DB.prepare(
                `SELECT id, challenge_id, attempt_id, time_ms, deaths, damage_taken, tier, status, late, rejection_reason, replay_status,
                   game_version, created_at, verified_at FROM challenge_runs WHERE player_id = ?1 ORDER BY created_at`,
            )
                .bind(id)
                .all()
        ).results,
        // 2.33: attempts of « Plus loin » (progress computed by the server from their replay).
        challenge_attempts: (
            await ctx.env.DB.prepare(
                `SELECT id, challenge_id, attempt_id, time_ms, claim, late, status, progress, route, detail, reasons, game_version, created_at,
                   verified_at FROM challenge_attempts WHERE player_id = ?1 ORDER BY created_at`,
            )
                .bind(id)
                .all()
        ).results,
        // 2.50: the migrations between this account and another one, the progression to restore.
        account_migrations: (
            await ctx.env.DB.prepare(
                `SELECT id, CASE WHEN source_id = ?1 THEN 'source' ELSE 'target' END AS side, source_username, target_username, mode, items,
                   close_source, by_player IS NOT NULL AS by_admin, created_at
                 FROM account_migrations WHERE source_id = ?1 OR target_id = ?1 ORDER BY created_at`,
            )
                .bind(id)
                .all()
        ).results,
        progress_restore: await ctx.env.DB.prepare(
            "SELECT from_username, size, game_version, copy_created_at, created_at, postponed_at FROM progress_restores WHERE player_id = ?1",
        )
            .bind(id)
            .first(),
        notes: "Le mot de passe n'est stocké que sous forme d'empreinte PBKDF2 et n'est pas exportable. Les adresses IP ne sont jamais stockées en clair.",
    });
}

/** DELETE /players/me: deletes the account and every personal data item (password required). */
export async function deleteMe(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.accountDelete, "player:" + auth.player.id);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["password", "confirm"]);
    const password = v.str("password", { min: 1, max: 128 });
    const confirm = v.str("confirm", { max: 16 });
    if (confirm !== undefined && confirm !== "DELETE") v.fail("confirm", "doit valoir DELETE");
    v.done();
    const cred = await ctx.env.DB.prepare("SELECT password_hash, password_salt, password_iterations FROM player_credentials WHERE player_id = ?1")
        .bind(auth.player.id)
        .first<{ password_hash: string; password_salt: string; password_iterations: number }>();
    if (!cred || !(await verifyPassword(password!, { hash: cred.password_hash, salt: cred.password_salt, iterations: cred.password_iterations })))
        throw new ApiError(401, "INVALID_CREDENTIALS", "Mot de passe incorrect.");
    const id = auth.player.id;
    // Files first (R2), then rows (D1 cascades sessions, records, bests, replays, friends).
    const keys = await ctx.env.DB.prepare("SELECT storage_key FROM replay_metadata WHERE player_id = ?1").bind(id).all<{ storage_key: string }>();
    const toDelete = keys.results.map((k) => k.storage_key);
    if (auth.player.avatar_custom_key) toDelete.push(auth.player.avatar_custom_key);
    // Community levels of the player (packages, thumbnails, replays of every player on them)
    // and the player's replays on other levels.
    toDelete.push(...(await communityKeys(ctx, "l.author_id = ?1", id)));
    toDelete.push(...(await ownedProjectKeys(ctx, id)));
    const own = await ctx.env.DB.prepare("SELECT replay_key FROM community_records WHERE player_id = ?1 AND replay_key IS NOT NULL").bind(id).all<{ replay_key: string }>();
    for (const r of own.results) if (!toDelete.includes(r.replay_key)) toDelete.push(r.replay_key);
    // Replays of the player's attempts in the races of EXTRÊME levels.
    const race = await ctx.env.DB.prepare("SELECT replay_key FROM race_runs WHERE player_id = ?1 AND replay_key IS NOT NULL").bind(id).all<{ replay_key: string }>();
    for (const r of race.results) if (!toDelete.includes(r.replay_key)) toDelete.push(r.replay_key);
    // Replays of the player's daily challenge runs (the runs follow the account: ON DELETE CASCADE).
    for (const k of await challengeReplayKeys(ctx, id)) if (!toDelete.includes(k)) toDelete.push(k);
    // 2.47: the copies of the progression sent by the game.
    toDelete.push(...(await progressCopyKeys(ctx, id)));
    for (let i = 0; i < toDelete.length; i += 500) await ctx.env.STORAGE.delete(toDelete.slice(i, i + 500));
    // 2.66: the player's clan gets a new chief (or disappears when it was its last member).
    const clanAfter = await clanAfterAccountDeletion(ctx.env.DB, id);
    await ctx.env.DB.batch([
        ctx.env.DB.prepare("DELETE FROM audit_log WHERE player_id = ?1").bind(id),
        // The races of the player's community levels go with the levels (their own attempts
        // elsewhere follow the account: ON DELETE CASCADE).
        ctx.env.DB.prepare(
            "DELETE FROM race_runs WHERE EXISTS (SELECT 1 FROM community_levels l WHERE l.author_id = ?1 AND race_runs.board LIKE 'c:' || l.id || ':%')",
        ).bind(id),
        ctx.env.DB.prepare(
            "DELETE FROM race_victors WHERE EXISTS (SELECT 1 FROM community_levels l WHERE l.author_id = ?1 AND race_victors.board LIKE 'c:' || l.id || ':%')",
        ).bind(id),
        // Averages and counters of the campaign recomputed without the player (2.19).
        ...campaignBeforeAccountDeletion(ctx, id),
        ctx.env.DB.prepare("DELETE FROM players WHERE id = ?1").bind(id),
        ...campaignAfterAccountDeletion(ctx),
        // Groups the player owned go to their oldest member; emptied conversations disappear.
        ctx.env.DB.prepare(
            `UPDATE conversation_members SET role = 'owner' WHERE rowid IN (
               SELECT (SELECT m2.rowid FROM conversation_members m2 WHERE m2.conversation_id = c.id ORDER BY m2.joined_at LIMIT 1)
               FROM conversations c WHERE c.kind = 'group' AND c.clan_id IS NULL
                 AND NOT EXISTS (SELECT 1 FROM conversation_members m3 WHERE m3.conversation_id = c.id AND m3.role = 'owner'))`,
        ),
        ctx.env.DB.prepare("DELETE FROM conversations WHERE NOT EXISTS (SELECT 1 FROM conversation_members m WHERE m.conversation_id = conversations.id)"),
        ...clanAfter,
    ]);
    await clearSpectateRoom(ctx.env, id);  // a run being played live (memory only)
    // A trace without any personal identifier.
    await audit(ctx, null, "account_deleted");
    return json(ctx, { deleted: true });
}

// ------------------------------------------------------------------ avatars

const MAX_AVATAR_BYTES = 64 * 1024;

/** Strict PNG check: signature, IHDR first, square 32-256 px, 8-bit, IEND present. */
export function checkAvatarPng(data: Uint8Array): string | null {
    const sig = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];
    if (data.byteLength < 45 || sig.some((b, i) => data[i] !== b)) return "fichier PNG attendu";
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    if (view.getUint32(8) !== 13 || String.fromCharCode(...data.subarray(12, 16)) !== "IHDR") return "en-tête PNG invalide";
    const w = view.getUint32(16);
    const h = view.getUint32(20);
    if (w !== h || w < 32 || w > 256) return "image carrée de 32 à 256 pixels attendue";
    if (data[24] !== 8) return "profondeur 8 bits attendue";
    // Walk the chunks: lengths must be consistent and the file must end with IEND.
    let at = 8;
    let sawEnd = false;
    while (at + 12 <= data.byteLength) {
        const len = view.getUint32(at);
        const type = String.fromCharCode(...data.subarray(at + 4, at + 8));
        if (!/^[A-Za-z]{4}$/.test(type) || at + 12 + len > data.byteLength) return "structure PNG invalide";
        at += 12 + len;
        if (type === "IEND") {
            sawEnd = true;
            break;
        }
    }
    if (!sawEnd || at !== data.byteLength) return "structure PNG invalide";
    return null;
}

export async function putAvatar(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.profileWrite, "player:" + auth.player.id);
    if (!(ctx.request.headers.get("content-type") ?? "").toLowerCase().startsWith("image/png"))
        throw new ApiError(415, "UNSUPPORTED_MEDIA_TYPE", "Avatar PNG attendu (image/png).");
    const data = await readLimited(ctx.request, MAX_AVATAR_BYTES);
    const problem = checkAvatarPng(data);
    if (problem) throw new ApiError(422, "INVALID_IMAGE", `Avatar refusé : ${problem}.`);
    const key = `avatars/${auth.player.id}.png`;
    await ctx.env.STORAGE.put(key, data, { httpMetadata: { contentType: "image/png" } });
    await ctx.env.DB.prepare("UPDATE players SET avatar = 'custom', avatar_custom_key = ?1 WHERE id = ?2").bind(key, auth.player.id).run();
    await audit(ctx, auth.player.id, "avatar_uploaded", { bytes: data.byteLength });
    const fresh = await ctx.env.DB.prepare("SELECT * FROM players WHERE id = ?1").bind(auth.player.id).first<PlayerRow>();
    return json(ctx, { player: privateProfile(fresh!) });
}

export async function getAvatar(ctx: Ctx): Promise<Response> {
    const p = await loadPlayer(ctx, ctx.params.playerId!);
    if (p.avatar !== "custom" || !p.avatar_custom_key) throw Errors.notFound("AVATAR_NOT_FOUND", "Pas d'avatar personnalisé.");
    const obj = await ctx.env.STORAGE.get(p.avatar_custom_key);
    if (!obj) throw Errors.notFound("AVATAR_NOT_FOUND", "Pas d'avatar personnalisé.");
    return new Response(obj.body, {
        headers: { "content-type": "image/png", "cache-control": "public, max-age=300", "x-content-type-options": "nosniff" },
    });
}
