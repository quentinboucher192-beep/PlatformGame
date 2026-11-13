import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { api, freshIp, registerPlayer, seedAll } from "./helpers";

beforeEach(seedAll);

describe("health and meta", () => {
    it("reports a healthy service", async () => {
        const res = await api("GET", "/api/v1/health");
        expect(res.status).toBe(200);
        expect(res.body.ok).toBe(true);
        expect(res.body.data.database).toBe("ok");
        expect(typeof res.body.request_id).toBe("string");
    });

    it("lists ranked levels with their current revision", async () => {
        const res = await api("GET", "/api/v1/meta");
        expect(res.status).toBe(200);
        const ids = res.body.data.levels.map((l: any) => l.id);
        expect(ids).toContain("test_01");
        expect(res.body.data.categories).toContain("NO_DAMAGE");
    });

    it("answers unknown routes and methods with homogeneous errors", async () => {
        const a = await api("GET", "/api/v1/nope");
        expect(a.status).toBe(404);
        expect(a.body).toMatchObject({ ok: false, error: { code: "NOT_FOUND" } });
        const b = await api("DELETE", "/api/v1/health");
        expect(b.status).toBe(405);
        expect(b.body.error.code).toBe("METHOD_NOT_ALLOWED");
    });
});

describe("registration", () => {
    it("creates an account, hashes the password and opens a session", async () => {
        const res = await api("POST", "/api/v1/auth/register", {
            body: { username: "NovaRider", password: "sup3r secret pass", country_code: "FR", location_visibility: "globe", accept_tos: true, tos_version: "2026-11" },
        });
        expect(res.status).toBe(201);
        const { player, session } = res.body.data;
        expect(player.username).toBe("NovaRider");
        expect(player.country_code).toBe("FR");
        expect(session.access_token).toMatch(/^[A-Za-z0-9_-]{43}$/);
        const cred = await env.DB.prepare("SELECT * FROM player_credentials WHERE player_id = ?1").bind(player.id).first<any>();
        expect(cred.password_hash).not.toContain("sup3r");
        expect(cred.password_iterations).toBe(10000);
        // Tokens are stored hashed only.
        const s = await env.DB.prepare("SELECT token_hash FROM player_sessions WHERE player_id = ?1").bind(player.id).first<any>();
        expect(s.token_hash).not.toBe(session.access_token);
        expect(s.token_hash).toMatch(/^[0-9a-f]{64}$/);
    });

    it("says whether a username is free before registering", async () => {
        await registerPlayer("Orion");
        const check = async (name: string) =>
            (await api("GET", `/api/v1/auth/username-available?username=${encodeURIComponent(name)}`)).body.data;
        expect(await check("Andromeda")).toMatchObject({ available: true, reason: null });
        expect(await check("orion")).toMatchObject({ available: false, reason: "taken" });
        expect(await check("ORION")).toMatchObject({ available: false, reason: "taken" });
        expect(await check("Admin")).toMatchObject({ available: false, reason: "reserved" });
        expect(await check("a")).toMatchObject({ available: false, reason: "invalid" });
        expect(await check("_bad_")).toMatchObject({ available: false, reason: "invalid" });
        expect(await check("")).toMatchObject({ available: false, reason: "invalid" });
        // The answer is never cached and the check is rate limited per IP.
        const res = await api("GET", "/api/v1/auth/username-available?username=Andromeda");
        expect(res.headers.get("cache-control")).toBe("no-store");
        const ip = freshIp();
        let limited = 0;
        for (let i = 0; i < 65; ++i) {
            const r = await api("GET", `/api/v1/auth/username-available?username=Name${i}x`, { ip });
            if (r.status === 429) ++limited;
        }
        expect(limited).toBeGreaterThan(0);
    });

    it("refuses duplicates, reserved names, weak passwords and missing consent", async () => {
        await registerPlayer("Orion");
        const dup = await api("POST", "/api/v1/auth/register", { body: { username: "orion", password: "another password", accept_tos: true, tos_version: "2026-11" } });
        expect(dup.status).toBe(409);
        expect(dup.body.error.code).toBe("USERNAME_TAKEN");
        const reserved = await api("POST", "/api/v1/auth/register", { body: { username: "Admin", password: "another password", accept_tos: true, tos_version: "2026-11" } });
        expect(reserved.status).toBe(400);
        const weak = await api("POST", "/api/v1/auth/register", { body: { username: "Vega", password: "short", accept_tos: true, tos_version: "2026-11" } });
        expect(weak.status).toBe(400);
        expect(weak.body.error.details[0].field).toBe("password");
        const noTos = await api("POST", "/api/v1/auth/register", { body: { username: "Vega", password: "long enough pass", accept_tos: false, tos_version: "2026-11" } });
        expect(noTos.status).toBe(400);
        const oldTos = await api("POST", "/api/v1/auth/register", { body: { username: "Vega", password: "long enough pass", accept_tos: true, tos_version: "2020-01" } });
        expect(oldTos.status).toBe(400);
    });

    it("validates country and location consent", async () => {
        const unknown = await api("POST", "/api/v1/auth/register", {
            body: { username: "Lyra", password: "long enough pass", country_code: "ZZ", accept_tos: true, tos_version: "2026-11" },
        });
        expect(unknown.status).toBe(400);
        const noCountry = await api("POST", "/api/v1/auth/register", {
            body: { username: "Lyra", password: "long enough pass", location_visibility: "globe", accept_tos: true, tos_version: "2026-11" },
        });
        expect(noCountry.status).toBe(400);
        const p = await registerPlayer("Lyra", { country_code: "DE" });
        const me = await api("GET", "/api/v1/auth/me", { token: p.token });
        // Location is hidden unless the player opts in.
        expect(me.body.data.player.location_visibility).toBe("hidden");
    });

    it("rejects unknown fields and non-JSON bodies", async () => {
        const extra = await api("POST", "/api/v1/auth/register", {
            body: { username: "Lyra", password: "long enough pass", accept_tos: true, tos_version: "2026-11", is_admin: true },
        });
        expect(extra.status).toBe(400);
        const text = await api("POST", "/api/v1/auth/register", { raw: "username=x", headers: { "content-type": "text/plain" } });
        expect(text.status).toBe(415);
        const broken = await api("POST", "/api/v1/auth/register", { raw: "{nope", headers: { "content-type": "application/json" } });
        expect(broken.status).toBe(400);
        expect(broken.body.error.code).toBe("INVALID_JSON");
    });

    it("limits registrations per IP", async () => {
        const ip = freshIp();
        for (let i = 0; i < 5; ++i) {
            const r = await api("POST", "/api/v1/auth/register", {
                ip,
                body: { username: `Pilot${i}x`, password: "long enough pass", accept_tos: true, tos_version: "2026-11" },
            });
            expect(r.status).toBe(201);
        }
        const blocked = await api("POST", "/api/v1/auth/register", {
            ip,
            body: { username: "PilotLast", password: "long enough pass", accept_tos: true, tos_version: "2026-11" },
        });
        expect(blocked.status).toBe(429);
        expect(blocked.body.error.code).toBe("RATE_LIMITED");
        expect(Number(blocked.headers.get("retry-after"))).toBeGreaterThan(0);
        // Rate limit keys never contain the raw IP.
        const rows = await env.DB.prepare("SELECT bucket FROM rate_limits").all<{ bucket: string }>();
        expect(rows.results.every((r) => !r.bucket.includes(ip))).toBe(true);
    });
});

describe("login and sessions", () => {
    it("logs in with the right password only, with a generic error", async () => {
        const p = await registerPlayer("Sirius");
        const ok = await api("POST", "/api/v1/auth/login", { body: { username: "SIRIUS", password: p.password } });
        expect(ok.status).toBe(200);
        expect(ok.body.data.player.id).toBe(p.id);
        const bad = await api("POST", "/api/v1/auth/login", { body: { username: "Sirius", password: "wrong password" } });
        expect(bad.status).toBe(401);
        const ghost = await api("POST", "/api/v1/auth/login", { body: { username: "Nobody", password: "wrong password" } });
        expect(ghost.status).toBe(401);
        expect(ghost.body.error.code).toBe(bad.body.error.code);
        expect(ghost.body.error.message).toBe(bad.body.error.message);
    });

    it("locks the account after repeated failures", async () => {
        const p = await registerPlayer("Altair");
        for (let i = 0; i < 10; ++i) {
            const r = await api("POST", "/api/v1/auth/login", { body: { username: "Altair", password: "nope nope" } });
            expect(r.status).toBe(401);
        }
        const locked = await api("POST", "/api/v1/auth/login", { body: { username: "Altair", password: p.password } });
        expect(locked.status).toBe(423);
        expect(locked.body.error.code).toBe("ACCOUNT_LOCKED");
        const logs = await env.DB.prepare("SELECT action FROM audit_log WHERE player_id = ?1").bind(p.id).all<{ action: string }>();
        expect(logs.results.map((l) => l.action)).toContain("login_failed_locked");
    });

    it("requires a valid, unexpired access token", async () => {
        const p = await registerPlayer("Deneb");
        expect((await api("GET", "/api/v1/auth/me")).status).toBe(401);
        expect((await api("GET", "/api/v1/auth/me", { headers: { authorization: "Bearer not-a-real-token-at-all-000" } })).status).toBe(401);
        expect((await api("GET", "/api/v1/auth/me", { headers: { authorization: "Basic abc" } })).status).toBe(401);
        await env.DB.prepare("UPDATE player_sessions SET expires_at = '2000-01-01T00:00:00.000Z' WHERE player_id = ?1").bind(p.id).run();
        const expired = await api("GET", "/api/v1/auth/me", { token: p.token });
        expect(expired.status).toBe(401);
        expect(expired.body.error.code).toBe("TOKEN_EXPIRED");
    });

    it("rotates tokens on refresh and revokes a session on refresh token reuse", async () => {
        const p = await registerPlayer("Rigel");
        const r1 = await api("POST", "/api/v1/auth/refresh", { body: { refresh_token: p.refresh } });
        expect(r1.status).toBe(200);
        const s1 = r1.body.data.session;
        expect(s1.refresh_token).not.toBe(p.refresh);
        // The old access token is no longer valid, the new one is.
        expect((await api("GET", "/api/v1/auth/me", { token: p.token })).status).toBe(401);
        expect((await api("GET", "/api/v1/auth/me", { token: s1.access_token })).status).toBe(200);
        // Reuse of the old refresh token long after rotation: the session is revoked.
        await env.DB.prepare("UPDATE player_sessions SET rotated_at = '2000-01-01T00:00:00.000Z' WHERE player_id = ?1").bind(p.id).run();
        const reuse = await api("POST", "/api/v1/auth/refresh", { body: { refresh_token: p.refresh } });
        expect(reuse.status).toBe(401);
        expect(reuse.body.error.code).toBe("SESSION_REVOKED");
        expect((await api("GET", "/api/v1/auth/me", { token: s1.access_token })).status).toBe(401);
        expect((await api("POST", "/api/v1/auth/refresh", { body: { refresh_token: s1.refresh_token } })).status).toBe(401);
        const garbage = await api("POST", "/api/v1/auth/refresh", { body: { refresh_token: "x".repeat(43) } });
        expect(garbage.status).toBe(401);
    });

    it("accepts a retried refresh within the grace period (lost response)", async () => {
        const p = await registerPlayer("Pollux");
        const r1 = await api("POST", "/api/v1/auth/refresh", { body: { refresh_token: p.refresh } });
        expect(r1.status).toBe(200);
        const retry = await api("POST", "/api/v1/auth/refresh", { body: { refresh_token: p.refresh } });
        expect(retry.status).toBe(200);
        expect((await api("GET", "/api/v1/auth/me", { token: retry.body.data.session.access_token })).status).toBe(200);
    });

    it("expires sessions and logs out", async () => {
        const p = await registerPlayer("Vega");
        await env.DB.prepare("UPDATE player_sessions SET refresh_expires_at = '2000-01-01T00:00:00.000Z' WHERE player_id = ?1").bind(p.id).run();
        const r = await api("POST", "/api/v1/auth/refresh", { body: { refresh_token: p.refresh } });
        expect(r.status).toBe(401);
        expect(r.body.error.code).toBe("SESSION_EXPIRED");
        const q = await registerPlayer("Capella");
        const out = await api("POST", "/api/v1/auth/logout", { token: q.token });
        expect(out.status).toBe(200);
        expect((await api("GET", "/api/v1/auth/me", { token: q.token })).status).toBe(401);
    });

    it("blocks suspended accounts", async () => {
        const p = await registerPlayer("Mira");
        await env.DB.prepare("UPDATE players SET account_status = 'suspended' WHERE id = ?1").bind(p.id).run();
        const r = await api("GET", "/api/v1/auth/me", { token: p.token });
        expect(r.status).toBe(403);
        expect(r.body.error.code).toBe("ACCOUNT_SUSPENDED");
    });
});
