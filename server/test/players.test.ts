import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { api, registerPlayer, seedAll, submission, verifiedRun } from "./helpers";

beforeEach(seedAll);

describe("profiles", () => {
    it("shows public statistics and hides private profiles", async () => {
        const a = await registerPlayer("Nova", { country_code: "FR", location_visibility: "country" });
        await verifiedRun(a, 61000);
        const pub = await api("GET", `/api/v1/players/${a.id}`);
        expect(pub.status).toBe(200);
        expect(pub.body.data.player).toMatchObject({ username: "Nova", country_code: "FR", hidden: false });
        expect(pub.body.data.statistics).toMatchObject({ verified_records: 1, first_places: 1, levels_completed: 1 });
        expect(pub.body.data.statistics.best_times[0]).toMatchObject({ level_id: "test_01", rank: 1, time_ms: 61000 });
        // Private data is never in a public profile.
        expect(pub.body.data.player.trust_level).toBeUndefined();
        expect(pub.body.data.player.location_visibility).toBeUndefined();

        await api("PATCH", "/api/v1/players/me", { token: a.token, body: { profile_visibility: "private" } });
        const hidden = await api("GET", `/api/v1/players/${a.id}`);
        expect(hidden.body.data.player.hidden).toBe(true);
        expect(hidden.body.data.statistics).toBeNull();
        expect((await api("GET", `/api/v1/players/${a.id}/records`)).status).toBe(403);
        expect((await api("GET", `/api/v1/players/${a.id}/statistics`)).status).toBe(403);
        // The owner still sees everything.
        const self = await api("GET", `/api/v1/players/${a.id}`, { token: a.token });
        expect(self.body.data.player.profile_visibility).toBe("private");
        expect((await api("GET", "/api/v1/players/00000000-0000-4000-8000-000000000000")).status).toBe(404);
        expect((await api("GET", "/api/v1/players/'%20OR%201=1")).status).toBe(404);
    });

    it("shows only verified records to other players", async () => {
        const a = await registerPlayer("Nova");
        await verifiedRun(a, 61000);
        await api("POST", "/api/v1/records/submit", { token: a.token, body: submission({ time_ms: 59000 }) });
        const others = await api("GET", `/api/v1/players/${a.id}/records`);
        expect(others.body.data.records.map((r: any) => r.verification_status)).toEqual(["VERIFIED"]);
        expect(others.body.data.records[0].attempt_id).toBeUndefined();
        const own = await api("GET", `/api/v1/players/${a.id}/records`, { token: a.token });
        expect(own.body.data.records).toHaveLength(2);
        const pending = await api("GET", `/api/v1/players/${a.id}/records?status=PENDING`, { token: a.token });
        expect(pending.body.data.records).toHaveLength(1);
    });

    it("updates settings with validation", async () => {
        const a = await registerPlayer("Nova");
        const ok = await api("PATCH", "/api/v1/players/me", {
            token: a.token,
            body: { country_code: "JP", location_visibility: "globe", avatar: "avatar_07", allow_friend_requests: false },
        });
        expect(ok.status).toBe(200);
        expect(ok.body.data.player).toMatchObject({ country_code: "JP", location_visibility: "globe", avatar: "avatar_07", allow_friend_requests: false });
        // Clearing the country also hides the location.
        const cleared = await api("PATCH", "/api/v1/players/me", { token: a.token, body: { country_code: null } });
        expect(cleared.body.data.player).toMatchObject({ country_code: null, location_visibility: "hidden" });
        expect((await api("PATCH", "/api/v1/players/me", { token: a.token, body: { avatar: "../../secret" } })).status).toBe(400);
        expect((await api("PATCH", "/api/v1/players/me", { token: a.token, body: { trust_level: 3 } })).status).toBe(400);
        expect((await api("PATCH", "/api/v1/players/me", { token: a.token, body: { region_code: "US-CA" } })).status).toBe(400);
        await registerPlayer("Orion");
        const taken = await api("PATCH", "/api/v1/players/me", { token: a.token, body: { username: "ORION" } });
        expect(taken.status).toBe(409);
        const renamed = await api("PATCH", "/api/v1/players/me", { token: a.token, body: { username: "NovaPrime" } });
        expect(renamed.body.data.player.username).toBe("NovaPrime");
    });

    it("searches pseudonyms by prefix without injection", async () => {
        await registerPlayer("Nova");
        await registerPlayer("Novak");
        await registerPlayer("Orion");
        const res = await api("GET", "/api/v1/players/search?q=nov");
        expect(res.body.data.players.map((p: any) => p.username)).toEqual(["Nova", "Novak"]);
        expect((await api("GET", "/api/v1/players/search?q=%25%25")).status).toBe(400);
        expect((await api("GET", "/api/v1/players/search?q=a'%20OR%20'1'='1")).status).toBe(400);
    });

    it("accepts only small square PNG avatars", async () => {
        const a = await registerPlayer("Nova");
        const bad = await api("PUT", "/api/v1/players/me/avatar", { token: a.token, raw: new Uint8Array([1, 2, 3]), headers: { "content-type": "image/png" } });
        expect(bad.status).toBe(422);
        const gif = await api("PUT", "/api/v1/players/me/avatar", { token: a.token, raw: "GIF89a", headers: { "content-type": "image/gif" } });
        expect(gif.status).toBe(415);
        // Minimal valid 32x32 PNG structure (IHDR + IDAT + IEND).
        const png = minimalPng(32, 32);
        const ok = await api("PUT", "/api/v1/players/me/avatar", { token: a.token, raw: png, headers: { "content-type": "image/png" } });
        expect(ok.status).toBe(200);
        expect(ok.body.data.player.avatar).toBe("custom");
        const got = await api("GET", `/api/v1/players/${a.id}/avatar`);
        expect(got.status).toBe(200);
        expect(got.headers.get("content-type")).toBe("image/png");
        const rect = await api("PUT", "/api/v1/players/me/avatar", { token: a.token, raw: minimalPng(64, 32), headers: { "content-type": "image/png" } });
        expect(rect.status).toBe(422);
    });
});

describe("GDPR", () => {
    it("exports personal data", async () => {
        const a = await registerPlayer("Nova", { country_code: "FR" });
        await verifiedRun(a, 61000);
        const res = await api("GET", "/api/v1/players/me/export", { token: a.token });
        expect(res.status).toBe(200);
        expect(res.body.data.player.username).toBe("Nova");
        expect(res.body.data.records).toHaveLength(1);
        expect(JSON.stringify(res.body.data)).not.toContain("password_hash");
        expect(JSON.stringify(res.body.data)).not.toContain("token_hash");
    });

    it("deletes the account and its data after password confirmation", async () => {
        const a = await registerPlayer("Nova");
        const b = await registerPlayer("Orion");
        const run = await verifiedRun(a, 61000);
        await api("POST", "/api/v1/friends/request", { token: a.token, body: { username: "Orion" } });
        const wrong = await api("DELETE", "/api/v1/players/me", { token: a.token, body: { password: "wrong one", confirm: "DELETE" } });
        expect(wrong.status).toBe(401);
        const noConfirm = await api("DELETE", "/api/v1/players/me", { token: a.token, body: { password: a.password, confirm: "yes" } });
        expect(noConfirm.status).toBe(400);
        const key = await env.DB.prepare("SELECT storage_key FROM replay_metadata WHERE record_id = ?1").bind(run.recordId).first<string>("storage_key");
        expect(await env.STORAGE.head(key!)).not.toBeNull();
        const ok = await api("DELETE", "/api/v1/players/me", { token: a.token, body: { password: a.password, confirm: "DELETE" } });
        expect(ok.status).toBe(200);
        for (const t of ["players", "records", "player_bests", "player_sessions", "replay_metadata", "player_credentials"]) {
            const n = await env.DB.prepare(`SELECT COUNT(*) AS n FROM ${t} WHERE ${t === "players" ? "id" : "player_id"} = ?1`).bind(a.id).first("n");
            expect(n, t).toBe(0);
        }
        expect(await env.STORAGE.head(key!)).toBeNull();
        const friends = await api("GET", "/api/v1/friends", { token: b.token });
        expect(friends.body.data.incoming).toHaveLength(0);
        expect((await api("GET", "/api/v1/auth/me", { token: a.token })).status).toBe(401);
    });
});

describe("friends", () => {
    it("handles requests, acceptance, refusal and removal", async () => {
        const a = await registerPlayer("Nova");
        const b = await registerPlayer("Orion");
        const c = await registerPlayer("Shy", { allow_friend_requests: false });
        const req = await api("POST", "/api/v1/friends/request", { token: a.token, body: { username: "orion" } });
        expect(req.status).toBe(201);
        expect(req.body.data.status).toBe("pending");
        expect((await api("POST", "/api/v1/friends/request", { token: a.token, body: { username: "Orion" } })).status).toBe(409);
        const lists = await api("GET", "/api/v1/friends", { token: b.token });
        expect(lists.body.data.incoming.map((f: any) => f.player.username)).toEqual(["Nova"]);
        const acc = await api("POST", "/api/v1/friends/accept", { token: b.token, body: { player_id: a.id } });
        expect(acc.body.data.status).toBe("accepted");
        const fa = await api("GET", "/api/v1/friends", { token: a.token });
        expect(fa.body.data.friends.map((f: any) => f.player.username)).toEqual(["Orion"]);
        expect((await api("POST", "/api/v1/friends/request", { token: a.token, body: { username: "Shy" } })).status).toBe(403);
        expect((await api("POST", "/api/v1/friends/request", { token: a.token, body: { player_id: a.id } })).status).toBe(400);
        expect((await api("POST", "/api/v1/friends/accept", { token: c.token, body: { player_id: a.id } })).status).toBe(404);
        expect((await api("DELETE", `/api/v1/friends/${b.id}`, { token: a.token })).status).toBe(200);
        expect((await api("GET", "/api/v1/friends", { token: b.token })).body.data.friends).toHaveLength(0);
        expect((await api("DELETE", `/api/v1/friends/${b.id}`, { token: a.token })).status).toBe(404);
    });

    it("accepts mutual requests automatically", async () => {
        const a = await registerPlayer("Nova");
        const b = await registerPlayer("Orion", { allow_friend_requests: false });
        // Orion refuses requests but sends one himself: Nova sending back accepts it.
        await api("POST", "/api/v1/friends/request", { token: b.token, body: { username: "Nova" } });
        const res = await api("POST", "/api/v1/friends/request", { token: a.token, body: { username: "Orion" } });
        expect(res.body.data.status).toBe("accepted");
    });
});

// ------------------------------------------------------------------ helpers

function crc32(bytes: Uint8Array): number {
    let c = ~0;
    for (const b of bytes) {
        c ^= b;
        for (let k = 0; k < 8; ++k) c = (c >>> 1) ^ (0xedb88320 & -(c & 1));
    }
    return ~c >>> 0;
}

function chunk(type: string, data: Uint8Array): Uint8Array {
    const out = new Uint8Array(12 + data.length);
    const v = new DataView(out.buffer);
    v.setUint32(0, data.length);
    out.set(new TextEncoder().encode(type), 4);
    out.set(data, 8);
    v.setUint32(8 + data.length, crc32(out.subarray(4, 8 + data.length)));
    return out;
}

function minimalPng(w: number, h: number): Uint8Array {
    const ihdr = new Uint8Array(13);
    const v = new DataView(ihdr.buffer);
    v.setUint32(0, w);
    v.setUint32(4, h);
    ihdr[8] = 8;
    ihdr[9] = 6;
    const parts = [new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk("IHDR", ihdr), chunk("IDAT", new Uint8Array([0x78, 0x9c, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01])), chunk("IEND", new Uint8Array())];
    const total = parts.reduce((n, p) => n + p.length, 0);
    const out = new Uint8Array(total);
    let at = 0;
    for (const p of parts) {
        out.set(p, at);
        at += p.length;
    }
    return out;
}
