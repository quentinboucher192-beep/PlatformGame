// Accounts and sessions: registration, login, token refresh with rotation and reuse
// detection, logout, current player. The player is ALWAYS identified from the session,
// never from an id sent by the client.

import { DEFAULT_PBKDF2_ITERATIONS, dummyPasswordCheck, hashIp, hashPassword, randomToken, sha256Hex, verifyPassword } from "./crypto";
import { ApiError, Errors, json, log, nowIso, readJsonObject, type Ctx } from "./http";
import { privateProfile, type PlayerRow } from "./model";
import { RULES, rateLimit } from "./ratelimit";
import {
    AVATAR_RE,
    COUNTRY_RE,
    LOCATION_VISIBILITY,
    PROFILE_VISIBILITY,
    REGION_RE,
    USERNAME_RE,
    Validator,
    isReservedUsername,
    normalizeUsername,
} from "./validate";

export interface AuthInfo {
    player: PlayerRow;
    sessionId: string;
}

const MAX_FAILED_LOGINS = 10;
const LOCK_MINUTES = 15;
const REFRESH_GRACE_SECONDS = 120;

function iterations(ctx: Ctx): number {
    const n = Number(ctx.env.PBKDF2_ITERATIONS ?? DEFAULT_PBKDF2_ITERATIONS);
    return Number.isInteger(n) && n >= 10_000 && n <= DEFAULT_PBKDF2_ITERATIONS ? n : DEFAULT_PBKDF2_ITERATIONS;
}

export async function audit(ctx: Ctx, playerId: string | null, action: string, details?: Record<string, unknown>): Promise<void> {
    const ipHash = await hashIp(ctx.ip, ctx.env.IP_HASH_SALT ?? "");
    await ctx.env.DB.prepare("INSERT INTO audit_log (player_id, action, ip_hash, details, created_at) VALUES (?1, ?2, ?3, ?4, ?5)")
        .bind(playerId, action, ipHash, details ? JSON.stringify(details) : null, nowIso())
        .run();
    log("info", "audit", { action, player_id: playerId, request_id: ctx.requestId });
}

// ------------------------------------------------------------------ sessions

interface IssuedSession {
    access_token: string;
    access_expires_at: string;
    refresh_token: string;
    refresh_expires_at: string;
}

function ttl(ctx: Ctx): { accessMs: number; refreshMs: number } {
    const access = Number(ctx.env.ACCESS_TOKEN_TTL_SECONDS);
    const refresh = Number(ctx.env.REFRESH_TOKEN_TTL_DAYS);
    return {
        accessMs: (Number.isFinite(access) && access > 0 ? access : 3600) * 1000,
        refreshMs: (Number.isFinite(refresh) && refresh > 0 ? refresh : 30) * 86400 * 1000,
    };
}

async function newTokens(ctx: Ctx) {
    const access = randomToken();
    const refresh = randomToken();
    const { accessMs, refreshMs } = ttl(ctx);
    const now = Date.now();
    return {
        access,
        refresh,
        accessHash: await sha256Hex(access),
        refreshHash: await sha256Hex(refresh),
        accessExpires: new Date(now + accessMs).toISOString(),
        refreshExpires: new Date(now + refreshMs).toISOString(),
    };
}

function clientInfo(ctx: Ctx): string {
    return (ctx.request.headers.get("user-agent") ?? "").slice(0, 120);
}

export async function createSession(ctx: Ctx, playerId: string): Promise<IssuedSession> {
    const t = await newTokens(ctx);
    const now = nowIso();
    await ctx.env.DB.prepare(
        `INSERT INTO player_sessions (id, player_id, token_hash, refresh_hash, created_at, expires_at, refresh_expires_at, last_used_at, client_info)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?5, ?8)`,
    )
        .bind(crypto.randomUUID(), playerId, t.accessHash, t.refreshHash, now, t.accessExpires, t.refreshExpires, clientInfo(ctx))
        .run();
    return { access_token: t.access, access_expires_at: t.accessExpires, refresh_token: t.refresh, refresh_expires_at: t.refreshExpires };
}

function bearer(ctx: Ctx): string | null {
    const h = ctx.request.headers.get("authorization");
    if (!h) return null;
    const m = /^Bearer ([A-Za-z0-9_-]{20,100})$/.exec(h);
    if (!m || !m[1]) throw Errors.unauthorized("INVALID_TOKEN", "Jeton d'accès invalide.");
    return m[1];
}

/** Resolves the session of the request; null when the request carries no token. */
export async function optionalAuth(ctx: Ctx): Promise<AuthInfo | null> {
    const token = bearer(ctx);
    if (token === null) return null;
    const row = await ctx.env.DB.prepare(
        `SELECT s.id AS session_id, s.expires_at AS session_expires, s.last_used_at AS session_used, p.*
         FROM player_sessions s JOIN players p ON p.id = s.player_id
         WHERE s.token_hash = ?1 AND s.revoked_at IS NULL`,
    )
        .bind(await sha256Hex(token))
        .first<PlayerRow & { session_id: string; session_expires: string; session_used: string }>();
    if (!row) throw Errors.unauthorized("INVALID_TOKEN", "Session inconnue ou révoquée.");
    if (Date.parse(row.session_expires) <= Date.now())
        throw Errors.unauthorized("TOKEN_EXPIRED", "Jeton expiré : utilisez le jeton de renouvellement.");
    if (row.account_status !== "active") throw Errors.forbidden("ACCOUNT_SUSPENDED", "Ce compte est suspendu.");
    // Activity timestamps are refreshed at most every 5 minutes (fewer writes).
    if (Date.now() - Date.parse(row.session_used) > 5 * 60 * 1000) {
        const now = nowIso();
        ctx.exec.waitUntil(
            ctx.env.DB.batch([
                ctx.env.DB.prepare("UPDATE player_sessions SET last_used_at = ?1 WHERE id = ?2").bind(now, row.session_id),
                ctx.env.DB.prepare("UPDATE players SET last_seen_at = ?1 WHERE id = ?2").bind(now, row.id),
            ]),
        );
    }
    const { session_id, session_expires: _e, session_used: _u, ...player } = row;
    return { player: player as PlayerRow, sessionId: session_id };
}

export async function requireAuth(ctx: Ctx): Promise<AuthInfo> {
    const auth = await optionalAuth(ctx);
    if (!auth) throw Errors.unauthorized();
    return auth;
}

// ------------------------------------------------------------------- routes

export async function countryExists(ctx: Ctx, code: string): Promise<boolean> {
    return (await ctx.env.DB.prepare("SELECT 1 AS x FROM countries WHERE code = ?1").bind(code).first()) !== null;
}

export async function register(ctx: Ctx): Promise<Response> {
    await rateLimit(ctx, RULES.register, ctx.ip);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, [
        "username", "password", "avatar", "country_code", "region_code", "location_visibility",
        "profile_visibility", "allow_friend_requests", "accept_tos", "tos_version",
    ]);
    const username = v.str("username", { min: 3, max: 20, pattern: USERNAME_RE, trim: true });
    const password = v.str("password", { min: 8, max: 128 });
    const avatar = v.str("avatar", { max: 16, pattern: AVATAR_RE, optional: true }) ?? "avatar_01";
    const country = v.str("country_code", { max: 2, pattern: COUNTRY_RE, optional: true });
    const region = v.str("region_code", { max: 6, pattern: REGION_RE, optional: true });
    const locVis = v.oneOf("location_visibility", LOCATION_VISIBILITY, { optional: true }) ?? "hidden";
    const profVis = v.oneOf("profile_visibility", PROFILE_VISIBILITY, { optional: true }) ?? "public";
    const allowFriends = v.bool("allow_friend_requests", { optional: true }) ?? true;
    const accept = v.bool("accept_tos");
    const tos = v.str("tos_version", { max: 32 });
    if (accept === false) v.fail("accept_tos", "les conditions d'utilisation doivent être acceptées");
    if (tos !== undefined && tos !== ctx.env.TOS_VERSION) v.fail("tos_version", "version des conditions obsolète");
    if (username && isReservedUsername(username)) v.fail("username", "pseudonyme réservé");
    if (password && username && password.toLowerCase().includes(username.toLowerCase()))
        v.fail("password", "le mot de passe ne doit pas contenir le pseudonyme");
    if (region && (!country || !region.startsWith(country + "-"))) v.fail("region_code", "la région doit appartenir au pays choisi");
    if (locVis !== "hidden" && !country) v.fail("location_visibility", "un pays est nécessaire pour être visible");
    v.done();
    if (country && !(await countryExists(ctx, country)))
        throw Errors.validation([{ field: "country_code", message: "pays inconnu" }]);

    const id = crypto.randomUUID();
    const now = nowIso();
    const pwd = await hashPassword(password!, iterations(ctx));
    try {
        await ctx.env.DB.batch([
            ctx.env.DB.prepare(
                `INSERT INTO players (id, username, username_normalized, avatar, country_code, region_code, location_visibility,
                   profile_visibility, allow_friend_requests, tos_version, tos_accepted_at, created_at, last_seen_at)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?11, ?11)`,
            ).bind(id, username!, normalizeUsername(username!), avatar, country ?? null, region ?? null, locVis, profVis,
                allowFriends ? 1 : 0, tos!, now),
            ctx.env.DB.prepare(
                "INSERT INTO player_credentials (player_id, password_hash, password_salt, password_iterations, updated_at) VALUES (?1, ?2, ?3, ?4, ?5)",
            ).bind(id, pwd.hash, pwd.salt, pwd.iterations, now),
        ]);
    } catch (e) {
        if (String(e).includes("UNIQUE")) throw Errors.conflict("USERNAME_TAKEN", "Ce pseudonyme est déjà utilisé.");
        throw e;
    }
    const session = await createSession(ctx, id);
    await audit(ctx, id, "register");
    const player = await ctx.env.DB.prepare("SELECT * FROM players WHERE id = ?1").bind(id).first<PlayerRow>();
    return json(ctx, { player: privateProfile(player!), session }, { status: 201 });
}

export async function login(ctx: Ctx): Promise<Response> {
    await rateLimit(ctx, RULES.loginIp, ctx.ip);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["username", "password"]);
    const username = v.str("username", { min: 1, max: 32, trim: true });
    const password = v.str("password", { min: 1, max: 128 });
    v.done();
    const norm = normalizeUsername(username!);
    await rateLimit(ctx, RULES.loginUser, "user:" + norm);
    const row = await ctx.env.DB.prepare(
        `SELECT p.*, c.password_hash, c.password_salt, c.password_iterations, c.failed_attempts, c.locked_until
         FROM players p JOIN player_credentials c ON c.player_id = p.id WHERE p.username_normalized = ?1`,
    )
        .bind(norm)
        .first<PlayerRow & { password_hash: string; password_salt: string; password_iterations: number; failed_attempts: number; locked_until: string | null }>();
    const invalid = new ApiError(401, "INVALID_CREDENTIALS", "Pseudonyme ou mot de passe incorrect.");
    if (!row) {
        await dummyPasswordCheck(password!, iterations(ctx));
        await audit(ctx, null, "login_failed_unknown_user");
        throw invalid;
    }
    if (row.locked_until && Date.parse(row.locked_until) > Date.now()) {
        await audit(ctx, row.id, "login_blocked_locked");
        throw new ApiError(423, "ACCOUNT_LOCKED", "Trop d'échecs de connexion : compte temporairement verrouillé.", {
            locked_until: row.locked_until,
        });
    }
    const good = await verifyPassword(password!, { hash: row.password_hash, salt: row.password_salt, iterations: row.password_iterations });
    if (!good) {
        const failed = row.failed_attempts + 1;
        const lock = failed >= MAX_FAILED_LOGINS ? new Date(Date.now() + LOCK_MINUTES * 60000).toISOString() : null;
        await ctx.env.DB.prepare("UPDATE player_credentials SET failed_attempts = ?1, locked_until = ?2 WHERE player_id = ?3")
            .bind(lock ? 0 : failed, lock, row.id)
            .run();
        await audit(ctx, row.id, lock ? "login_failed_locked" : "login_failed");
        throw invalid;
    }
    if (row.account_status !== "active") throw Errors.forbidden("ACCOUNT_SUSPENDED", "Ce compte est suspendu.");
    const now = nowIso();
    const updates = [
        ctx.env.DB.prepare("UPDATE player_credentials SET failed_attempts = 0, locked_until = NULL WHERE player_id = ?1").bind(row.id),
        ctx.env.DB.prepare("UPDATE players SET last_seen_at = ?1 WHERE id = ?2").bind(now, row.id),
    ];
    // Transparent upgrade of older hashes to the current iteration count.
    if (row.password_iterations < iterations(ctx)) {
        const pwd = await hashPassword(password!, iterations(ctx));
        updates.push(
            ctx.env.DB.prepare("UPDATE player_credentials SET password_hash = ?1, password_salt = ?2, password_iterations = ?3, updated_at = ?4 WHERE player_id = ?5")
                .bind(pwd.hash, pwd.salt, pwd.iterations, now, row.id),
        );
    }
    await ctx.env.DB.batch(updates);
    const session = await createSession(ctx, row.id);
    await audit(ctx, row.id, "login");
    const player = await ctx.env.DB.prepare("SELECT * FROM players WHERE id = ?1").bind(row.id).first<PlayerRow>();
    return json(ctx, { player: privateProfile(player!), session });
}

export async function refresh(ctx: Ctx): Promise<Response> {
    await rateLimit(ctx, RULES.refresh, ctx.ip);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["refresh_token"]);
    const token = v.str("refresh_token", { min: 20, max: 100, pattern: /^[A-Za-z0-9_-]+$/ });
    v.done();
    const hash = await sha256Hex(token!);
    const now = nowIso();
    type SessionRow = { id: string; player_id: string; refresh_expires_at: string; rotated_at: string | null; account_status: string };
    let s = await ctx.env.DB.prepare(
        `SELECT s.id, s.player_id, s.refresh_expires_at, s.rotated_at, p.account_status FROM player_sessions s
         JOIN players p ON p.id = s.player_id WHERE s.refresh_hash = ?1 AND s.revoked_at IS NULL`,
    )
        .bind(hash)
        .first<SessionRow>();
    if (!s) {
        // A refresh token that was already rotated: legitimate retry within a short grace
        // period (lost response), otherwise a possible theft -> the whole session is revoked.
        const prev = await ctx.env.DB.prepare(
            `SELECT s.id, s.player_id, s.refresh_expires_at, s.rotated_at, p.account_status FROM player_sessions s
             JOIN players p ON p.id = s.player_id WHERE s.previous_refresh_hash = ?1 AND s.revoked_at IS NULL`,
        )
            .bind(hash)
            .first<SessionRow>();
        if (prev && prev.rotated_at && Date.now() - Date.parse(prev.rotated_at) < REFRESH_GRACE_SECONDS * 1000) {
            s = prev;
        } else {
            if (prev) {
                await ctx.env.DB.prepare("UPDATE player_sessions SET revoked_at = ?1 WHERE id = ?2").bind(now, prev.id).run();
                await audit(ctx, prev.player_id, "refresh_token_reuse_revoked");
                throw Errors.unauthorized("SESSION_REVOKED", "Session révoquée par sécurité : reconnectez-vous.");
            }
            throw Errors.unauthorized("INVALID_REFRESH_TOKEN", "Jeton de renouvellement invalide.");
        }
    }
    if (Date.parse(s.refresh_expires_at) <= Date.now()) throw Errors.unauthorized("SESSION_EXPIRED", "Session expirée : reconnectez-vous.");
    if (s.account_status !== "active") throw Errors.forbidden("ACCOUNT_SUSPENDED", "Ce compte est suspendu.");
    const t = await newTokens(ctx);
    // Rotation: the refresh token presented becomes "previous"; the expiry of the refresh
    // token slides forward.
    const res = await ctx.env.DB.prepare(
        `UPDATE player_sessions SET token_hash = ?1, refresh_hash = ?2, previous_refresh_hash = ?3, rotated_at = ?4,
           expires_at = ?5, refresh_expires_at = ?6, last_used_at = ?4 WHERE id = ?7 AND revoked_at IS NULL`,
    )
        .bind(t.accessHash, t.refreshHash, hash, now, t.accessExpires, t.refreshExpires, s.id)
        .run();
    if (!res.meta.changes) throw Errors.unauthorized("SESSION_REVOKED", "Session révoquée.");
    return json(ctx, {
        session: { access_token: t.access, access_expires_at: t.accessExpires, refresh_token: t.refresh, refresh_expires_at: t.refreshExpires },
    });
}

export async function logout(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    let all = false;
    if (ctx.request.headers.get("content-type")?.startsWith("application/json")) {
        const body = await readJsonObject(ctx);
        const v = new Validator(body, ["all"]);
        all = v.bool("all", { optional: true }) ?? false;
        v.done();
    }
    const now = nowIso();
    if (all)
        await ctx.env.DB.prepare("UPDATE player_sessions SET revoked_at = ?1 WHERE player_id = ?2 AND revoked_at IS NULL")
            .bind(now, auth.player.id)
            .run();
    else await ctx.env.DB.prepare("UPDATE player_sessions SET revoked_at = ?1 WHERE id = ?2").bind(now, auth.sessionId).run();
    await audit(ctx, auth.player.id, all ? "logout_all" : "logout");
    return json(ctx, { logged_out: true, all_sessions: all });
}

export async function me(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    return json(ctx, { player: privateProfile(auth.player) });
}
