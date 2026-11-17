// 2.47: an account is private to one computer: bound at registration (or at the first login
// with a 2.47 game), the right password from another computer refused (written down, the
// owner told), one account per computer, an admin may unbind or bind it again; the copies of
// the progression the game sends (the 5 most recent kept, deleted with the account).

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { api, deviceOf, registerPlayer, seedAll, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";
const OTHER = { id: "f".repeat(32), name: "Autre PC" };

function login(p: TestPlayer, device: unknown = deviceOf(p.username)) {
    return api("POST", `${V}/auth/login`, { body: { username: p.username, password: p.password, device } });
}

async function makeAdmin(p: TestPlayer): Promise<void> {
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
}

function copyBody(levels: number, extra: Record<string, unknown> = {}) {
    return {
        format: "cosmic.progress_copy",
        game_version: "2.47.0",
        summary: { levels, achievements: 3, stars: 5, play_seconds: 600, bosses: 1 },
        files: { progress: { completed: ["sylvanox_01"] }, achievements: { unlocked: { voyage_first: "2026-11-16 15:00" } } },
        ...extra,
    };
}

describe("an account bound to one computer", () => {
    it("binds at registration; the same computer logs in, another one is refused and the owner told", async () => {
        const nova = await registerPlayer("Nova");
        const row = await env.DB.prepare("SELECT device_id, device_name FROM players WHERE id = ?1").bind(nova.id).first<{ device_id: string; device_name: string }>();
        expect(row).toEqual({ device_id: deviceOf("Nova").id, device_name: "PC de test" });
        expect((await login(nova)).status).toBe(200);
        // The right password on another computer: refused, written down, the owner told.
        const other = await login(nova, OTHER);
        expect(other.status).toBe(403);
        expect(other.body.error.code).toBe("ACCOUNT_BOUND_ELSEWHERE");
        const wrong = await api("POST", `${V}/auth/login`, { body: { username: "Nova", password: "not the password", device: OTHER } });
        expect(wrong.body.error.code).toBe("INVALID_CREDENTIALS"); // a wrong password says nothing more
        const me = await api("GET", `${V}/players/me/device`, { token: (await login(nova)).body.data.session.access_token });
        expect(me.body.data.device).toMatchObject({ name: "PC de test" });
        expect(me.body.data.events.map((e: any) => e.kind)).toEqual(["refused", "bound"]);
        const notes = await api("GET", `${V}/notifications`, { token: nova.token });
        const sec = notes.body.data.notifications.filter((n: any) => n.kind === "security");
        expect(sec.map((n: any) => n.data.reason)).toContain("login_refused");
        // A second refusal merges with the unread alert.
        await login(nova, OTHER);
        const again = await api("GET", `${V}/notifications`, { token: nova.token });
        expect(again.body.data.notifications.find((n: any) => n.data.reason === "login_refused").count).toBe(2);
        // After 5 refusals, that computer waits.
        for (let i = 0; i < 3; i++) await login(nova, OTHER);
        expect((await login(nova, OTHER)).status).toBe(429);
    });

    it("one account per computer; a game without a computer id must be updated", async () => {
        const a = await registerPlayer("Altair");
        const b = await api("POST", `${V}/auth/register`, {
            body: { username: "Bellatrix", password: "correct horse battery", accept_tos: true, tos_version: "2026-11", device: deviceOf("Altair") },
        });
        expect(b.status).toBe(409);
        expect(b.body.error.code).toBe("DEVICE_BOUND");
        const old = await api("POST", `${V}/auth/login`, { body: { username: a.username, password: a.password } });
        expect(old.status).toBe(426);
        const bad = await login(a, { id: "nope", name: "" });
        expect(bad.status).toBe(400);
    });

    it("an account from before 2.47 is bound at its first login, never on a computer that has one", async () => {
        const vega = await registerPlayer("Vega");
        const deneb = await registerPlayer("Deneb");
        await env.DB.prepare("UPDATE players SET device_id = NULL, device_name = NULL, device_bound_at = NULL WHERE id = ?1").bind(vega.id).run();
        // Deneb's computer has an account: Vega cannot bind it.
        const taken = await login(vega, deviceOf("Deneb"));
        expect(taken.body.error.code).toBe("DEVICE_BOUND");
        const first = await login(vega, OTHER);
        expect(first.status).toBe(200);
        expect((await env.DB.prepare("SELECT device_id FROM players WHERE id = ?1").bind(vega.id).first<{ device_id: string }>())?.device_id).toBe(OTHER.id);
        expect((await login(vega, deviceOf("Vega"))).body.error.code).toBe("ACCOUNT_BOUND_ELSEWHERE");
        expect(deneb.id).toBeTruthy();
    });

    it("an admin unbinds an account or binds it to a refused attempt; Journal and notification", async () => {
        const admin = await registerPlayer("NovaRider");
        await makeAdmin(admin);
        const rigel = await registerPlayer("Rigel");
        const player = await registerPlayer("Kestrel");
        expect((await api("GET", `${V}/admin/players/${rigel.id}/device`, { token: player.token })).status).toBe(404);
        await login(rigel, OTHER);
        const view = await api("GET", `${V}/admin/players/${rigel.id}/device`, { token: admin.token });
        expect(view.body.data).toMatchObject({ device: { name: "PC de test" }, rebind_allowed: false });
        expect(view.body.data.attempts).toHaveLength(1);
        expect(view.body.data.attempts[0].device_id).toBeUndefined(); // never shown
        // Bind it to the refused attempt's computer: the old sessions end.
        const bound = await api("POST", `${V}/admin/players/${rigel.id}/device`, { token: admin.token, body: { action: "bind", attempt_id: view.body.data.attempts[0].id } });
        expect(bound.status).toBe(200);
        expect((await api("GET", `${V}/auth/me`, { token: rigel.token })).status).toBe(401);
        expect((await login(rigel, OTHER)).status).toBe(200);
        // Unbind: the next login, from any computer, binds it again.
        const un = await api("POST", `${V}/admin/players/${rigel.id}/device`, { token: admin.token, body: { action: "unbind" } });
        expect(un.body.data.rebind_allowed).toBe(true);
        const fresh = { id: "e".repeat(32), name: "Nouveau PC" };
        expect((await login(rigel, fresh)).status).toBe(200);
        expect((await env.DB.prepare("SELECT device_id, device_rebind FROM players WHERE id = ?1").bind(rigel.id).first())).toEqual({ device_id: fresh.id, device_rebind: 0 });
        expect((await login(rigel, OTHER)).body.error.code).toBe("ACCOUNT_BOUND_ELSEWHERE");
        const log = await api("GET", `${V}/admin/log?kind=account`, { token: admin.token });
        expect(log.body.data.entries.map((e: any) => e.action)).toEqual(["admin_device_unbound", "admin_device_bound"]);
        const notes = await api("GET", `${V}/notifications`, { token: (await login(rigel, fresh)).body.data.session.access_token });
        const reasons = notes.body.data.notifications.filter((n: any) => n.kind === "security").map((n: any) => n.data.reason);
        expect(reasons).toEqual(expect.arrayContaining(["admin_bound", "admin_unbound", "device_rebound", "login_refused"]));
    });

    it("renames the computer", async () => {
        const p = await registerPlayer("Lyra");
        const r = await api("PATCH", `${V}/players/me/device`, { token: p.token, body: { name: "  PC   du salon " } });
        expect(r.body.data.device.name).toBe("PC du salon");
        expect((await api("PATCH", `${V}/players/me/device`, { token: p.token, body: { name: "<b>" } })).status).toBe(400);
    });
});

describe("copies of the progression", () => {
    it("keeps the 5 most recent, checks them, and deletes them with the account", async () => {
        const p = await registerPlayer("Orion");
        expect((await api("GET", `${V}/players/me/progress-copy`, { token: p.token })).body.data.copy).toBeNull();
        for (let i = 1; i <= 7; i++) {
            const r = await api("PUT", `${V}/players/me/progress-copy`, { token: p.token, body: copyBody(i) });
            expect(r.status, JSON.stringify(r.body)).toBe(201);
        }
        const last = await api("GET", `${V}/players/me/progress-copy`, { token: p.token });
        expect(last.body.data.copy).toMatchObject({ game_version: "2.47.0", summary: { levels: 7 } });
        const rows = await env.DB.prepare("SELECT file_key FROM progress_copies WHERE player_id = ?1").bind(p.id).all<{ file_key: string }>();
        expect(rows.results).toHaveLength(5);
        expect((await env.STORAGE.list({ prefix: `progress/${p.id}/` })).objects).toHaveLength(5);
        // Checked: format, numbers, only the known files.
        for (const bad of [copyBody(1, { format: "autre" }), copyBody(1, { summary: { levels: -1 } }), copyBody(1, { files: { settings: {} } })])
            expect((await api("PUT", `${V}/players/me/progress-copy`, { token: p.token, body: bad })).status).toBe(400);
        // The account goes: the copies too.
        const del = await api("DELETE", `${V}/players/me`, { token: p.token, body: { password: p.password, confirm: "DELETE" } });
        expect(del.status).toBe(200);
        expect((await env.STORAGE.list({ prefix: `progress/${p.id}/` })).objects).toHaveLength(0);
    });

    it("2.55: carries the statistics (stats), an object of 512 KB at most", async () => {
        const p = await registerPlayer("Lyra");
        const files = (stats: unknown) => ({ files: { progress: { completedLevels: ["sylvanox_01"] }, stats } });
        const stats = { names: ["attack"], days: [[20774, 600.5, 0, 2, 10, 7, 20, 9, 12, [0, 1, 0, 0, 0, 0, 2, 0], [[0, 15, 6]]]], seq: 3 };
        const ok = await api("PUT", `${V}/players/me/progress-copy`, { token: p.token, body: copyBody(1, files(stats)) });
        expect(ok.status, JSON.stringify(ok.body)).toBe(201);
        const objects = (await env.STORAGE.list({ prefix: `progress/${p.id}/` })).objects;
        expect(objects).toHaveLength(1);
        const kept = JSON.parse(await (await env.STORAGE.get(objects[0]!.key))!.text());
        expect(kept.files.stats).toEqual(stats); // kept as sent, for the restoration
        // Not an object, or bigger than 512 KB (the game keeps it under 300 KB): refused, nothing kept.
        const big = { names: [], days: [], pad: "x".repeat(512 * 1024) };
        for (const bad of [copyBody(1, files([1, 2])), copyBody(1, files("stats")), copyBody(1, files(big))]) {
            const r = await api("PUT", `${V}/players/me/progress-copy`, { token: p.token, body: bad });
            expect(r.status).toBe(400);
            expect(JSON.stringify(r.body)).toContain("files.stats");
        }
        expect((await env.STORAGE.list({ prefix: `progress/${p.id}/` })).objects).toHaveLength(1);
        // Just under the limit: accepted.
        const fits = { pad: "x".repeat(512 * 1024 - 20) };
        expect((await api("PUT", `${V}/players/me/progress-copy`, { token: p.token, body: copyBody(1, files(fits)) })).status).toBe(201);
    });
});
