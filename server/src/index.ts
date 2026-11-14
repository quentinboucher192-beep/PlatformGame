// Cosmic Online Ranking - Worker entry point: routing, error handling, request logs, edge
// caching of anonymous reads, and the daily maintenance cron.

import * as admin from "./admin";
import { login, logout, me, optionalAuth, refresh, register, usernameAvailable, type AuthInfo } from "./auth";
import * as friends from "./friends";
import * as globe from "./globe";
import { ApiError, errorResponse, json, log, type Bindings, type Ctx } from "./http";
import * as boards from "./leaderboards";
import * as creators from "./creators";
import * as notifications from "./notifications";
import * as workshop from "./workshop";
import * as projects from "./projects";
import { liveRoute } from "./live";
// The Durable Object class of the live editing rooms is exported by the Worker module.
export { ProjectRoom } from "./live";
import { listLevels } from "./levels";
import type { RecordRow } from "./model";
import * as players from "./players";
import { RULES, rateLimit } from "./ratelimit";
import * as social from "./social";
import * as community from "./community";
import * as records from "./records";
import * as releases from "./releases";
import { CATEGORIES, MODES } from "./validate";

const API_VERSION = "1.0.0";

type Handler = (ctx: Ctx, viewer: AuthInfo | null) => Promise<Response>;

interface Route {
    method: string;
    pattern: RegExp;
    keys: string[];
    template: string;
    handler: Handler;
    /** Resolve an optional session before calling the handler. */
    viewer: boolean;
    /** Anonymous responses may be cached at the edge. */
    cacheable: boolean;
}

const routes: Route[] = [];

function route(method: string, template: string, handler: Handler, opts: { viewer?: boolean; cacheable?: boolean } = {}): void {
    const keys: string[] = [];
    const pattern = new RegExp(
        "^" +
            template.replace(/\{(\w+)\}/g, (_m, k: string) => {
                keys.push(k);
                return "([^/]+)";
            }) +
            "$",
    );
    routes.push({ method, pattern, keys, template, handler, viewer: opts.viewer ?? false, cacheable: opts.cacheable ?? false });
}

const V = "/api/v1";

// Health and metadata
route("GET", `${V}/health`, async (ctx) => {
    try {
        await ctx.env.DB.prepare("SELECT 1").first();
    } catch {
        // Homogeneous error envelope: clients never mistake a degraded service for a success.
        throw new ApiError(503, "SERVICE_UNAVAILABLE", "Service temporairement indisponible.", { database: "unavailable" }, { "retry-after": "30" });
    }
    return json(ctx, { status: "ok", api_version: API_VERSION, environment: ctx.env.ENVIRONMENT, database: "ok", time: new Date().toISOString() });
});
route(
    "GET",
    `${V}/meta`,
    async (ctx) =>
        json(ctx, {
            api_version: API_VERSION,
            environment: ctx.env.ENVIRONMENT,
            tos_version: ctx.env.TOS_VERSION,
            min_game_version: ctx.env.MIN_GAME_VERSION,
            categories: CATEGORIES,
            modes: MODES,
            levels: await listLevels(ctx),
        }),
    { cacheable: true },
);

// Authentication
route("GET", `${V}/auth/username-available`, (ctx) => usernameAvailable(ctx));
route("POST", `${V}/auth/register`, (ctx) => register(ctx));
route("POST", `${V}/auth/login`, (ctx) => login(ctx));
route("POST", `${V}/auth/refresh`, (ctx) => refresh(ctx));
route("POST", `${V}/auth/logout`, (ctx) => logout(ctx));
route("GET", `${V}/auth/me`, (ctx) => me(ctx));

// Players
route("GET", `${V}/players/search`, (ctx) => players.searchPlayers(ctx));
route("GET", `${V}/players/me/export`, (ctx) => players.exportMe(ctx));
route("PATCH", `${V}/players/me`, (ctx) => players.patchMe(ctx));
route("DELETE", `${V}/players/me`, (ctx) => players.deleteMe(ctx));
route("PUT", `${V}/players/me/avatar`, (ctx) => players.putAvatar(ctx));
route("POST", `${V}/players/me/playtime`, (ctx) => creators.reportPlaytime(ctx));
route("GET", `${V}/players/{playerId}`, players.getPlayer, { viewer: true });
route("GET", `${V}/players/{playerId}/creations`, creators.creations, { viewer: true });
route("GET", `${V}/players/{playerId}/followers`, creators.followers, { viewer: true });
route("GET", `${V}/players/{playerId}/following`, creators.following, { viewer: true });
route("GET", `${V}/players/{playerId}/playtime`, creators.getPlaytime, { viewer: true });
route("POST", `${V}/players/{playerId}/follow`, (ctx) => creators.follow(ctx));
route("DELETE", `${V}/players/{playerId}/follow`, (ctx) => creators.unfollow(ctx));
route("GET", `${V}/creators`, (ctx) => creators.searchCreators(ctx));

// Notification centre
route("GET", `${V}/notifications`, (ctx) => notifications.listNotifications(ctx));
route("GET", `${V}/notifications/unread`, (ctx) => notifications.unreadNotifications(ctx));
route("POST", `${V}/notifications/read-all`, (ctx) => notifications.readAllNotifications(ctx));
route("GET", `${V}/notifications/settings`, (ctx) => notifications.getNotificationSettings(ctx));
route("PUT", `${V}/notifications/settings`, (ctx) => notifications.putNotificationSettings(ctx));
route("POST", `${V}/notifications/{notificationId}/read`, (ctx) => notifications.readNotification(ctx));
route("DELETE", `${V}/notifications/{notificationId}`, (ctx) => notifications.deleteNotification(ctx));
route("GET", `${V}/players/{playerId}/records`, players.getPlayerRecords, { viewer: true });
route("GET", `${V}/players/{playerId}/statistics`, players.getStatistics, { viewer: true });
route("GET", `${V}/players/{playerId}/avatar`, (ctx) => players.getAvatar(ctx));

// Leaderboards
route("GET", `${V}/leaderboards/global`, boards.routeGlobal, { viewer: true, cacheable: true });
route("GET", `${V}/leaderboards/level/{levelId}`, boards.routeLevel, { viewer: true, cacheable: true });
route("GET", `${V}/leaderboards/planet/{planetId}`, boards.routePlanet, { viewer: true, cacheable: true });
route("GET", `${V}/leaderboards/country/{countryCode}`, boards.routeCountry, { viewer: true, cacheable: true });
route("GET", `${V}/leaderboards/friends`, (ctx) => boards.routeFriends(ctx));
route("GET", `${V}/leaderboards/weekly`, boards.routeWeekly, { viewer: true, cacheable: true });

// Records
route("POST", `${V}/records/submit`, (ctx) => records.submit(ctx));
route("GET", `${V}/records/{recordId}`, records.getRecord, { viewer: true });
route("GET", `${V}/records/{recordId}/replay`, records.downloadReplay, { viewer: true });
route("PUT", `${V}/records/{recordId}/replay`, (ctx) => records.uploadReplay(ctx));

// Globe
route("GET", `${V}/globe/records`, globe.globeRecords, { viewer: true, cacheable: true });
route("GET", `${V}/globe/countries`, (ctx) => globe.globeCountries(ctx), { cacheable: true });
route("GET", `${V}/globe/records/{countryCode}`, globe.globeCountryRecords, { viewer: true, cacheable: true });

// Friends
route("GET", `${V}/friends`, (ctx) => friends.listFriends(ctx));
route("POST", `${V}/friends/request`, (ctx) => friends.requestFriend(ctx));
route("POST", `${V}/friends/accept`, (ctx) => friends.acceptFriend(ctx));
route("DELETE", `${V}/friends/{playerId}`, (ctx) => friends.deleteFriend(ctx));

// Social: conversations between friends, groups, blocks
route("GET", `${V}/social/conversations`, (ctx) => social.listConversations(ctx));
route("GET", `${V}/social/unread`, (ctx) => social.unread(ctx));
route("POST", `${V}/social/direct`, (ctx) => social.openDirect(ctx));
route("POST", `${V}/social/groups`, (ctx) => social.createGroup(ctx));
route("PATCH", `${V}/social/groups/{conversationId}`, (ctx) => social.renameGroup(ctx));
route("POST", `${V}/social/groups/{conversationId}/members`, (ctx) => social.addMember(ctx));
route("DELETE", `${V}/social/groups/{conversationId}/members/{playerId}`, (ctx) => social.removeMember(ctx));
route("GET", `${V}/social/conversations/{conversationId}/messages`, (ctx) => social.listMessages(ctx));
route("POST", `${V}/social/conversations/{conversationId}/messages`, (ctx) => social.sendMessage(ctx));
route("POST", `${V}/social/conversations/{conversationId}/read`, (ctx) => social.markRead(ctx));
route("DELETE", `${V}/social/messages/{messageId}`, (ctx) => social.deleteMessage(ctx));
route("POST", `${V}/social/messages/{messageId}/report`, (ctx) => social.reportMessage(ctx));
route("GET", `${V}/social/blocks`, (ctx) => social.listBlocks(ctx));
route("POST", `${V}/social/blocks`, (ctx) => social.blockPlayer(ctx));
route("DELETE", `${V}/social/blocks/{playerId}`, (ctx) => social.unblockPlayer(ctx));

// Community levels: publish, browse, download, rate, report, per-level leaderboards
route("GET", `${V}/community/levels`, (ctx) => community.listLevels(ctx));
route("POST", `${V}/community/levels`, (ctx) => community.startPublish(ctx));
route("GET", `${V}/community/levels/{levelId}`, (ctx) => community.getLevel(ctx));
route("DELETE", `${V}/community/levels/{levelId}`, (ctx) => community.removeLevel(ctx));
route("PUT", `${V}/community/levels/{levelId}/versions/{version}/package`, (ctx) => community.uploadPackage(ctx));
route("PUT", `${V}/community/levels/{levelId}/versions/{version}/replay`, (ctx) => community.uploadCompletion(ctx));
route("GET", `${V}/community/levels/{levelId}/package`, (ctx) => community.downloadPackage(ctx));

// Game installers (in-game "nouvelle version" notification)
route("GET", `${V}/client/latest`, (ctx) => releases.latest(ctx), { cacheable: true });
route("GET", `${V}/client/download/{version}`, (ctx) => releases.download(ctx));
route("GET", `${V}/community/levels/{levelId}/thumbnail`, (ctx) => community.thumbnail(ctx));
route("GET", `${V}/community/levels/{levelId}/leaderboard`, (ctx) => community.leaderboard(ctx));
route("POST", `${V}/community/levels/{levelId}/rate`, (ctx) => community.rate(ctx));
route("GET", `${V}/community/favorites`, (ctx) => workshop.listFavorites(ctx));
route("POST", `${V}/community/levels/{levelId}/favorite`, (ctx) => workshop.addFavorite(ctx));
route("DELETE", `${V}/community/levels/{levelId}/favorite`, (ctx) => workshop.removeFavorite(ctx));
route("GET", `${V}/community/levels/{levelId}/comments`, (ctx) => workshop.listComments(ctx));
route("POST", `${V}/community/levels/{levelId}/comments`, (ctx) => workshop.postComment(ctx));
route("GET", `${V}/community/comments/{commentId}/replies`, (ctx) => workshop.listReplies(ctx));
route("PATCH", `${V}/community/comments/{commentId}`, (ctx) => workshop.editComment(ctx));
route("DELETE", `${V}/community/comments/{commentId}`, (ctx) => workshop.deleteComment(ctx));
route("POST", `${V}/community/comments/{commentId}/pin`, (ctx) => workshop.pinComment(ctx, true));
route("DELETE", `${V}/community/comments/{commentId}/pin`, (ctx) => workshop.pinComment(ctx, false));
route("PUT", `${V}/community/comments/{commentId}/reaction`, (ctx) => workshop.reactComment(ctx));
route("DELETE", `${V}/community/comments/{commentId}/reaction`, (ctx) => workshop.unreactComment(ctx));
route("POST", `${V}/community/comments/{commentId}/report`, (ctx) => workshop.reportComment(ctx));
route("POST", `${V}/community/levels/{levelId}/report`, (ctx) => community.report(ctx));
route("POST", `${V}/community/levels/{levelId}/records`, (ctx) => community.submitRecord(ctx));
route("PUT", `${V}/community/records/{recordId}/replay`, (ctx) => community.uploadRecordReplay(ctx));
route("GET", `${V}/community/records/{recordId}/replay`, (ctx) => community.downloadRecordReplay(ctx));

// Collaborative projects (phase 2): roles checked by the server, invitations, revisions
route("POST", `${V}/projects`, (ctx) => projects.createProject(ctx));
route("GET", `${V}/projects`, (ctx) => projects.myProjects(ctx));
route("GET", `${V}/projects/discover`, (ctx) => projects.discoverProjects(ctx));
route("POST", `${V}/projects/join`, (ctx) => projects.joinWithKey(ctx));
route("GET", `${V}/projects/invitations`, (ctx) => projects.myInvitations(ctx));
route("POST", `${V}/projects/invitations/{inviteId}/accept`, (ctx) => projects.answerInvitation(ctx, true));
route("DELETE", `${V}/projects/invitations/{inviteId}`, (ctx) => projects.answerInvitation(ctx, false));
route("GET", `${V}/projects/{projectId}`, (ctx) => projects.getProject(ctx));
route("PATCH", `${V}/projects/{projectId}`, (ctx) => projects.updateProject(ctx));
route("DELETE", `${V}/projects/{projectId}`, (ctx) => projects.deleteProject(ctx));
route("GET", `${V}/projects/{projectId}/members`, (ctx) => projects.listMembers(ctx));
route("PATCH", `${V}/projects/{projectId}/members/{playerId}`, (ctx) => projects.setRole(ctx));
route("DELETE", `${V}/projects/{projectId}/members/{playerId}`, (ctx) => projects.removeMember(ctx));
route("POST", `${V}/projects/{projectId}/transfer`, (ctx) => projects.transferProject(ctx));
route("POST", `${V}/projects/{projectId}/invites`, (ctx) => projects.createInvite(ctx));
route("GET", `${V}/projects/{projectId}/invites`, (ctx) => projects.listInvites(ctx));
route("DELETE", `${V}/projects/{projectId}/invites/{inviteId}`, (ctx) => projects.revokeInvite(ctx));
route("POST", `${V}/projects/{projectId}/join`, (ctx) => projects.requestJoin(ctx));
route("DELETE", `${V}/projects/{projectId}/join`, (ctx) => projects.cancelJoinRequest(ctx));
route("GET", `${V}/projects/{projectId}/requests`, (ctx) => projects.listRequests(ctx));
route("POST", `${V}/projects/{projectId}/requests/{playerId}`, (ctx) => projects.answerRequest(ctx));
route("GET", `${V}/projects/{projectId}/content`, (ctx) => projects.getContent(ctx));
route("PUT", `${V}/projects/{projectId}/content`, (ctx) => projects.putContent(ctx));
route("GET", `${V}/projects/{projectId}/revisions`, (ctx) => projects.listRevisions(ctx));
route("GET", `${V}/projects/{projectId}/activity`, (ctx) => projects.listActivity(ctx));
// History and named versions (phase 5)
route("GET", `${V}/projects/{projectId}/compare`, (ctx) => projects.compareRevisions(ctx));
route("POST", `${V}/projects/{projectId}/revisions/{revision}/restore`, (ctx) => projects.restoreRevision(ctx));
route("GET", `${V}/projects/{projectId}/versions`, (ctx) => projects.listVersions(ctx));
route("POST", `${V}/projects/{projectId}/versions`, (ctx) => projects.createVersion(ctx));
route("GET", `${V}/projects/{projectId}/versions/{versionId}/content`, (ctx) => projects.versionContent(ctx));
// Live editing (phases 3-4): presence, temporary locks and operations, in the project's room
route("POST", `${V}/projects/{projectId}/live/join`, (ctx) => liveRoute(ctx, "join"));
route("POST", `${V}/projects/{projectId}/live/heartbeat`, (ctx) => liveRoute(ctx, "heartbeat"));
route("POST", `${V}/projects/{projectId}/live/ops`, (ctx) => liveRoute(ctx, "ops"));
route("POST", `${V}/projects/{projectId}/live/leave`, (ctx) => liveRoute(ctx, "leave"));

// Moderation (only with the ADMIN_TOKEN secret)
route("GET", `${V}/admin/records`, (ctx) => admin.listForReview(ctx));
route("POST", `${V}/admin/records/{recordId}/review`, (ctx) => admin.review(ctx));
route("POST", `${V}/admin/levels`, (ctx) => admin.registerLevels(ctx));
route("GET", `${V}/admin/comments`, async (ctx) => {
    await admin.requireAdmin(ctx);
    return workshop.adminListComments(ctx);
});
route("POST", `${V}/admin/comments/{commentId}/moderate`, async (ctx) => {
    await admin.requireAdmin(ctx);
    return workshop.adminModerateComment(ctx);
});

function match(method: string, path: string): { route: Route; params: Record<string, string> } | "method" | null {
    let pathMatched = false;
    for (const r of routes) {
        const m = r.pattern.exec(path);
        if (!m) continue;
        pathMatched = true;
        if (r.method !== method && !(method === "HEAD" && r.method === "GET")) continue;
        const params: Record<string, string> = {};
        r.keys.forEach((k, i) => {
            params[k] = decodeURIComponent(m[i + 1] ?? "");
        });
        return { route: r, params };
    }
    return pathMatched ? "method" : null;
}

function edgeCacheSeconds(env: Bindings): number {
    const n = Number(env.EDGE_CACHE_SECONDS ?? (env.ENVIRONMENT === "development" ? "0" : "15"));
    return Number.isFinite(n) && n > 0 ? Math.min(n, 300) : 0;
}

async function handle(request: Request, env: Bindings, exec: ExecutionContext, requestId: string): Promise<Response> {
    const url = new URL(request.url);
    const m = match(request.method, url.pathname);
    if (m === null) throw new ApiError(404, "NOT_FOUND", "Route inconnue.");
    if (m === "method") throw new ApiError(405, "METHOD_NOT_ALLOWED", "Méthode non autorisée pour cette route.");
    const ctx: Ctx = {
        request,
        env,
        exec,
        url,
        params: m.params,
        requestId,
        ip: request.headers.get("cf-connecting-ip") ?? "local",
    };
    const anonymous = !request.headers.has("authorization");
    const cacheTtl = m.route.cacheable && anonymous && request.method === "GET" ? edgeCacheSeconds(env) : 0;
    const cache = cacheTtl > 0 ? await caches.open("cosmic-online") : null;
    if (cache) {
        const hit = await cache.match(request);
        if (hit) return hit;
    }
    // The health check must work even when D1 (which stores the counters) is down.
    if (request.method === "GET" && m.route.template !== `${V}/health`) await rateLimit(ctx, RULES.reads, ctx.ip);
    const viewer = m.route.viewer ? await optionalAuth(ctx) : null;
    const response = await m.route.handler(ctx, viewer);
    if (cache && response.status === 200) {
        const cached = new Response(response.body, response);
        cached.headers.set("cache-control", `public, max-age=${cacheTtl}`);
        exec.waitUntil(cache.put(request, cached.clone()));
        return cached;
    }
    return response;
}

/** Records held PENDING with a verified replay are re-decided with the current rules. */
async function recheckHeldRecords(env: Bindings, exec: ExecutionContext): Promise<number> {
    const url = new URL("https://maintenance.invalid/");
    const ctx: Ctx = { request: new Request(url), env, exec, url, params: {}, requestId: crypto.randomUUID(), ip: "maintenance" };
    await notifications.pruneNotifications(ctx);
    const held = await env.DB.prepare(
        "SELECT * FROM records WHERE verification_status = 'PENDING' AND replay_id IS NOT NULL ORDER BY created_at LIMIT 200",
    ).all<RecordRow>();
    let changed = 0;
    for (const r of held.results) if (await records.recheckHeld(ctx, r)) ++changed;
    return changed;
}

/** Daily maintenance: expired sessions, stale rate-limit windows, old audit entries. */
async function maintenance(env: Bindings, exec: ExecutionContext): Promise<void> {
    const now = Date.now();
    const iso = (ms: number) => new Date(ms).toISOString();
    const results = await env.DB.batch([
        env.DB.prepare("DELETE FROM player_sessions WHERE refresh_expires_at < ?1 OR (revoked_at IS NOT NULL AND revoked_at < ?2)").bind(
            iso(now),
            iso(now - 30 * 86400000),
        ),
        env.DB.prepare("DELETE FROM rate_limits WHERE window_start < ?1").bind(Math.floor(now / 1000) - 86400),
        env.DB.prepare("DELETE FROM audit_log WHERE created_at < ?1").bind(iso(now - 180 * 86400000)),
    ]);
    const rechecked = await recheckHeldRecords(env, exec);
    log("info", "maintenance", { deleted: results.map((r) => r.meta.changes), rechecked });
}

export default {
    async fetch(request, env, exec): Promise<Response> {
        const requestId = crypto.randomUUID();
        const started = Date.now();
        let response: Response;
        try {
            response = await handle(request, env, exec, requestId);
        } catch (e) {
            if (e instanceof ApiError) response = errorResponse(requestId, e);
            else {
                // Internal details stay in the logs; the client gets a generic message.
                log("error", "unhandled_error", { request_id: requestId, error: e instanceof Error ? e.message : String(e) });
                response = errorResponse(requestId, new ApiError(500, "INTERNAL_ERROR", "Erreur interne du serveur."));
            }
        }
        const url = new URL(request.url);
        log(response.status >= 500 ? "error" : "info", "request", {
            request_id: requestId,
            method: request.method,
            path: url.pathname.replace(/[0-9a-f]{8}-[0-9a-f-]{27}/g, "{id}"),
            status: response.status,
            duration_ms: Date.now() - started,
        });
        return response;
    },
    async scheduled(_controller, env, exec): Promise<void> {
        exec.waitUntil(maintenance(env, exec));
    },
} satisfies ExportedHandler<Bindings>;
