// The administrator role (2.40): invisible to the other accounts (the routes answer like an
// unknown route), never one's own role, never the last admin, every change and refusal in the
// Journal, and the Joueurs list showing a player's location and presence only as they share them.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { api, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";

async function makeAdmin(p: TestPlayer): Promise<void> {
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
}

describe("admin role", () => {
    it("is part of the account's own profile and nobody else's", async () => {
        const nova = await registerPlayer("NovaRider");
        const other = await registerPlayer("Kestrel");
        expect((await api("GET", `${V}/auth/me`, { token: nova.token })).body.data.player.role).toBe("player");
        await makeAdmin(nova);
        expect((await api("GET", `${V}/auth/me`, { token: nova.token })).body.data.player.role).toBe("admin");
        const pub = await api("GET", `${V}/players/${nova.id}`, { token: other.token });
        expect(pub.status).toBe(200);
        expect(JSON.stringify(pub.body)).not.toContain('"role"');
    });

    it("the routes do not exist for a player, and need a session", async () => {
        const p = await registerPlayer("Kestrel");
        for (const [m, path, body] of [
            ["GET", `${V}/admin/players`, undefined],
            ["GET", `${V}/admin/log`, undefined],
            ["POST", `${V}/admin/players/${p.id}/role`, { admin: true }],
        ] as const) {
            const anon = await api(m, path, { body });
            expect(anon.status).toBe(401);
            const res = await api(m, path, { token: p.token, body });
            expect(res.status).toBe(404);
            expect(res.body.error.code).toBe("NOT_FOUND");
        }
        // A player cannot make themselves admin through their profile either.
        const patch = await api("PATCH", `${V}/players/me`, { token: p.token, body: { role: "admin" } });
        expect(patch.status).toBe(400);
        const row = await env.DB.prepare("SELECT role FROM players WHERE id = ?1").bind(p.id).first<{ role: string }>();
        expect(row?.role).toBe("player");
    });

    it("lists and searches the players with totals, location and presence as shared", async () => {
        const nova = await registerPlayer("NovaRider", { country_code: "FR", location_visibility: "country" });
        await registerPlayer("Kestrel", { country_code: "FR", location_visibility: "hidden" });
        await registerPlayer("Kestrella", { country_code: "US", location_visibility: "globe" });
        const hidden = await registerPlayer("Kesh_77");
        await makeAdmin(nova);
        await env.DB.prepare("UPDATE players SET presence_visible = 0, presence_state = 'active', presence_at = ?2 WHERE id = ?1")
            .bind(hidden.id, new Date().toISOString())
            .run();
        expect((await api("POST", `${V}/presence`, { token: nova.token, body: { state: "active" } })).status).toBe(200);

        const all = await api("GET", `${V}/admin/players`, { token: nova.token });
        expect(all.status).toBe(200);
        expect(all.body.data.players_total).toBe(4);
        expect(all.body.data.admins_total).toBe(1);

        const kes = await api("GET", `${V}/admin/players?q=kes`, { token: nova.token });
        const names = kes.body.data.players.map((p: { username: string }) => p.username).sort();
        expect(names).toEqual(["Kesh_77", "Kestrel", "Kestrella"]);
        expect(kes.body.data.total).toBe(3);
        const byName = Object.fromEntries(kes.body.data.players.map((p: { username: string }) => [p.username, p]));
        expect(byName.Kestrel.country_code).toBeNull();
        expect(byName.Kestrella.country_code).toBe("US");
        expect(byName.Kesh_77.presence).toBe("hidden");
        expect(byName.Kesh_77.last_seen_at).toBeNull();
        for (const p of kes.body.data.players) {
            expect(p).not.toHaveProperty("email");
            expect(p).not.toHaveProperty("ip_hash");
            expect(p).not.toHaveProperty("region_code");
        }

        // Anywhere in the name, and the filters.
        expect((await api("GET", `${V}/admin/players?q=ella`, { token: nova.token })).body.data.players[0].username).toBe("Kestrella");
        const admins = await api("GET", `${V}/admin/players?filter=admins`, { token: nova.token });
        expect(admins.body.data.players.map((p: { username: string }) => p.username)).toEqual(["NovaRider"]);
        const online = await api("GET", `${V}/admin/players?filter=online`, { token: nova.token });
        expect(online.body.data.players.map((p: { username: string }) => p.username)).toEqual(["NovaRider"]);
        expect(online.body.data.players[0].presence).toBe("online");

        // Paging.
        const page = await api("GET", `${V}/admin/players?limit=2`, { token: nova.token });
        expect(page.body.data.players).toHaveLength(2);
        expect(page.body.data.has_more).toBe(true);
        expect((await api("GET", `${V}/admin/players?q=%25`, { token: nova.token })).status).toBe(400);
        expect((await api("GET", `${V}/admin/players?filter=nope`, { token: nova.token })).status).toBe(400);
    });

    it("grants and removes the role, written to the Journal without the IP", async () => {
        const nova = await registerPlayer("NovaRider");
        const kes = await registerPlayer("Kestrel");
        await makeAdmin(nova);
        const grant = await api("POST", `${V}/admin/players/${kes.id}/role`, { token: nova.token, body: { admin: true } });
        expect(grant.status).toBe(200);
        expect(grant.body.data.player.role).toBe("admin");
        expect(grant.body.data.changed).toBe(true);
        // The new admin sees the screen at once.
        expect((await api("GET", `${V}/admin/log`, { token: kes.token })).status).toBe(200);
        // Same role again: nothing changes, nothing written.
        const again = await api("POST", `${V}/admin/players/${kes.id}/role`, { token: nova.token, body: { admin: true } });
        expect(again.body.data.changed).toBe(false);

        const revoke = await api("POST", `${V}/admin/players/${kes.id}/role`, { token: nova.token, body: { admin: false } });
        expect(revoke.status).toBe(200);
        expect(revoke.body.data.player.role).toBe("player");
        expect((await api("GET", `${V}/admin/log`, { token: kes.token })).status).toBe(404);

        const log = await api("GET", `${V}/admin/log`, { token: nova.token });
        expect(log.status).toBe(200);
        const entries = log.body.data.entries;
        expect(entries.map((e: { action: string }) => e.action)).toEqual(["admin_role_revoked", "admin_role_granted"]);
        expect(entries[1].actor.username).toBe("NovaRider");
        expect(entries[1].target.username).toBe("Kestrel");
        expect(JSON.stringify(log.body)).not.toContain("ip_hash");
        const roleOnly = await api("GET", `${V}/admin/log?kind=refused`, { token: nova.token });
        expect(roleOnly.body.data.entries).toHaveLength(0);
        const byActor = await api("GET", `${V}/admin/log?actor=${kes.id}`, { token: nova.token });
        expect(byActor.body.data.entries).toHaveLength(0);
    });

    it("never one's own role, never the last admin, and the refusals are in the Journal", async () => {
        const nova = await registerPlayer("NovaRider");
        const orion = await registerPlayer("Orion");
        await makeAdmin(nova);
        const own = await api("POST", `${V}/admin/players/${nova.id}/role`, { token: nova.token, body: { admin: false } });
        expect(own.status).toBe(409);
        expect(own.body.error.code).toBe("OWN_ROLE");
        await makeAdmin(orion);
        // Orion removes NovaRider: allowed (Orion stays).
        expect((await api("POST", `${V}/admin/players/${nova.id}/role`, { token: orion.token, body: { admin: false } })).status).toBe(200);
        // NovaRider is no longer admin: the route is gone for them.
        expect((await api("POST", `${V}/admin/players/${orion.id}/role`, { token: nova.token, body: { admin: false } })).status).toBe(404);
        const left = await env.DB.prepare("SELECT COUNT(*) AS n FROM players WHERE role = 'admin'").first<{ n: number }>();
        expect(left?.n).toBe(1);
        const refused = await api("GET", `${V}/admin/log?kind=refused`, { token: orion.token });
        expect(refused.body.data.entries[0].reason).toBe("OWN_ROLE");
        expect(refused.body.data.entries[0].actor.username).toBe("NovaRider");
    });

    it("the conditional update keeps at least one admin", async () => {
        const nova = await registerPlayer("NovaRider");
        const orion = await registerPlayer("Orion");
        await makeAdmin(nova);
        await makeAdmin(orion);
        // Both remove the other at the same time: one wins, the other is refused.
        const [a, b] = await Promise.all([
            api("POST", `${V}/admin/players/${orion.id}/role`, { token: nova.token, body: { admin: false } }),
            api("POST", `${V}/admin/players/${nova.id}/role`, { token: orion.token, body: { admin: false } }),
        ]);
        const statuses = [a.status, b.status].sort();
        expect(statuses[0]).toBe(200);
        expect([404, 409]).toContain(statuses[1]);
        const n = await env.DB.prepare("SELECT COUNT(*) AS n FROM players WHERE role = 'admin'").first<{ n: number }>();
        expect(n?.n).toBe(1);
    });

    it("refuses an unknown player and a bad body", async () => {
        const nova = await registerPlayer("NovaRider");
        await makeAdmin(nova);
        expect((await api("POST", `${V}/admin/players/00000000-0000-0000-0000-000000000000/role`, { token: nova.token, body: { admin: true } })).status).toBe(404);
        const kes = await registerPlayer("Kestrel");
        expect((await api("POST", `${V}/admin/players/${kes.id}/role`, { token: nova.token, body: { admin: "yes" } })).status).toBe(400);
        expect((await api("POST", `${V}/admin/players/${kes.id}/role`, { token: nova.token, body: { admin: true, role: "admin" } })).status).toBe(400);
    });

    it("shows the game version each player's client declared, and the latest installer (2.51)", async () => {
        const nova = await registerPlayer("NovaRider");
        const kes = await registerPlayer("Kestrel");
        await makeAdmin(nova);
        await env.DB.prepare(
            `INSERT INTO client_releases (version, version_key, platform, file_key, file_name, size, sha256, notes, published_at)
             VALUES ('2.51.0', 2051000, 'windows', 'releases/x.exe', 'x.exe', 1, ?1, '', '2026-11-16T00:00:00Z')`,
        )
            .bind("0".repeat(64))
            .run();
        const versionOf = async (id: string) =>
            (await api("GET", `${V}/admin/players`, { token: nova.token })).body.data.players.find((p: { id: string }) => p.id === id);
        // Never declared: null.
        expect((await versionOf(kes.id)).client_version).toBeNull();
        // Declared on a session renewal (the game's start), from X-Cosmic-Client.
        const r = await api("POST", `${V}/auth/refresh`, {
            body: { refresh_token: kes.refresh },
            headers: { "x-cosmic-client": "CosmicPlatformer/2.50.0" },
        });
        expect(r.status).toBe(200);
        const row = await versionOf(kes.id);
        expect(row.client_version).toBe("2.50.0");
        expect(typeof row.client_version_at).toBe("string");
        // A malformed or foreign header is ignored (declarative, never trusted for anything else).
        await api("POST", `${V}/auth/refresh`, {
            body: { refresh_token: r.body.data.session.refresh_token },
            headers: { "x-cosmic-client": "CosmicPlatformer/2.50.0; DROP TABLE", "user-agent": "curl/8.0" },
        });
        expect((await versionOf(kes.id)).client_version).toBe("2.50.0");
        const list = await api("GET", `${V}/admin/players`, { token: nova.token });
        expect(list.body.data.latest_client_version).toBe("2.51.0");
        // Players never see it, not even their own public profile.
        const pub = await api("GET", `${V}/players/${kes.id}`, { token: nova.token });
        expect(JSON.stringify(pub.body)).not.toContain("client_version");
    });
});
