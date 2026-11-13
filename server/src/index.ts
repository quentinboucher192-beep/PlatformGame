// Cosmic Online Ranking - Worker entry point: routing, error handling, request logs, edge
// caching of anonymous reads, and the daily maintenance cron.

import * as admin from "./admin";
import { login, logout, me, optionalAuth, refresh, register, usernameAvailable, type AuthInfo } from "./auth";
import * as friends from "./friends";
import * as globe from "./globe";
import { ApiError, errorResponse, json, log, type Bindings, type Ctx } from "./http";
import * as boards from "./leaderboards";
import { listLevels } from "./levels";
import type { RecordRow } from "./model";
import * as players from "./players";
import { RULES, rateLimit } from "./ratelimit";
import * as records from "./records";
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
route("GET", `${V}/players/{playerId}`, players.getPlayer, { viewer: true });
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

// Moderation (only with the ADMIN_TOKEN secret)
route("GET", `${V}/admin/records`, (ctx) => admin.listForReview(ctx));
route("POST", `${V}/admin/records/{recordId}/review`, (ctx) => admin.review(ctx));
route("POST", `${V}/admin/levels`, (ctx) => admin.registerLevels(ctx));

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
