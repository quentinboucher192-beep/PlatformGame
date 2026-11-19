// 2.75: ADMINISTRATION › Joueurs — a player's latest levels played (real data: the time the game reported
// per level, the records and attempts), and a replay watched through a privacy lock. Administrators only
// (the session's role, never a parameter), each written to the Journal; the other players keep exactly
// the locks they had; a technical impossibility (a replay not verified) is never bypassed.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { LEVEL2_HASH, api, registerPlayer, seedAll, submission, verifiedRun, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";

async function makeAdmin(p: TestPlayer): Promise<void> {
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
}

async function journal(action: string) {
    return (await env.DB.prepare("SELECT player_id, details FROM audit_log WHERE action = ?1").bind(action).all<{ player_id: string; details: string }>()).results;
}

describe("2.75: ADMINISTRATION › Joueurs — latest levels and replays", () => {
    it("lists a player's latest levels played, newest first, with the best attempt and its replay; administrators only, journaled", async () => {
        const nova = await registerPlayer("NovaRider");
        const admin = await registerPlayer("Moderatrice");
        const other = await registerPlayer("Curieux");
        await makeAdmin(admin);
        const run = await verifiedRun(nova, 61000);
        expect(run.status).toBe("VERIFIED");
        // The game's report of the time played (two levels, the second one later).
        await env.DB.prepare("INSERT INTO player_playtime (player_id, level_key, seconds, updated_at) VALUES (?1, 'test_01', 340, '2026-11-01T10:00:00.000Z')")
            .bind(nova.id)
            .run();
        await env.DB.prepare("INSERT INTO player_playtime (player_id, level_key, seconds, updated_at) VALUES (?1, 'test_02', 120, '2026-11-02T10:00:00.000Z')")
            .bind(nova.id)
            .run();
        // Nobody but an administrator: the route does not exist for the others (and the parameter is no key).
        expect((await api("GET", `${V}/admin/players/${nova.id}/levels`, { token: other.token })).status).toBe(404);
        expect((await api("GET", `${V}/admin/players/${nova.id}/levels?admin=1`, { token: nova.token })).status).toBe(404);
        expect((await api("GET", `${V}/admin/players/${nova.id}/levels`)).status).toBe(401);
        const res = await api("GET", `${V}/admin/players/${nova.id}/levels`, { token: admin.token });
        expect(res.status).toBe(200);
        const levels = res.body.data.levels;
        expect(levels.map((l: { level_id: string }) => l.level_id)).toEqual(["test_02", "test_01"]); // newest first
        expect(res.body.data.total).toBe(2);
        const l1 = levels[1];
        expect(l1).toMatchObject({ seconds: 340, attempts: 1 });
        expect(l1.best).toMatchObject({ record_id: run.recordId, time_ms: 61000, verified: true });
        expect(l1.replay).toMatchObject({ record_id: run.recordId, available: true, locked: null });
        expect(levels[0].best).toBeNull();
        expect(levels[0].replay).toBeNull();
        const logged = await journal("admin_player_levels_viewed");
        expect(logged).toHaveLength(1);
        expect(logged[0]!.player_id).toBe(admin.id);
        expect(JSON.parse(logged[0]!.details).target).toBe(nova.id);
    });

    it("an administrator watches a replay through a hidden profile (marked, journaled); the others keep the lock", async () => {
        const nova = await registerPlayer("NovaRider");
        const admin = await registerPlayer("Moderatrice");
        const other = await registerPlayer("Curieux");
        await makeAdmin(admin);
        const run = await verifiedRun(nova, 61000);
        expect(run.status).toBe("VERIFIED");
        expect((await api("PATCH", `${V}/players/me`, { token: nova.token, body: { profile_visibility: "private" } })).status).toBe(200);
        // Another player: exactly the lock of before.
        const refused = await api("GET", `${V}/records/${run.recordId}/replay`, { token: other.token });
        expect(refused.status).toBe(403);
        expect(refused.body.error.code).toBe("PROFILE_PRIVATE");
        expect((await api("GET", `${V}/records/${run.recordId}/replay`)).status).toBe(403);
        // The administrator: the replay, said watched as an administrator, and journaled.
        const seen = await api("GET", `${V}/records/${run.recordId}/replay`, { token: admin.token });
        expect(seen.status).toBe(200);
        expect(seen.headers.get("x-admin-view")).toBe("profile_private");
        const logged = await journal("admin_replay_viewed");
        expect(logged).toHaveLength(1);
        expect(JSON.parse(logged[0]!.details)).toMatchObject({ target: nova.id, record_id: run.recordId, lock: "profile_private" });
        // The list says the lock.
        const list = await api("GET", `${V}/admin/players/${nova.id}/levels`, { token: admin.token });
        expect(list.body.data.player.profile_hidden).toBe(true);
        // The owner still sees their own, unmarked.
        const own = await api("GET", `${V}/records/${run.recordId}/replay`, { token: nova.token });
        expect(own.status).toBe(200);
        expect(own.headers.get("x-admin-view")).toBeNull();
        // A suspended account: the others never; an administrator does (moderation).
        await env.DB.prepare("UPDATE players SET account_status = 'suspended', profile_visibility = 'public' WHERE id = ?1").bind(nova.id).run();
        expect((await api("GET", `${V}/records/${run.recordId}/replay`, { token: other.token })).status).toBe(404);
        const suspended = await api("GET", `${V}/records/${run.recordId}/replay`, { token: admin.token });
        expect(suspended.status).toBe(200);
        expect(suspended.headers.get("x-admin-view")).toBe("account_inactive");
    });

    // 2.75.3: a replay not verified (pending, refused) is no technical impossibility: an administrator watches it,
    // its status said (x-admin-replay-status), never counted as verified; the others keep the lock. A replay never
    // sent stays unavailable to everyone, with its reason.
    it("a replay not verified: watched by an administrator (marked), never by the others; a technical impossibility stays one", async () => {
        const nova = await registerPlayer("NovaRider");
        const admin = await registerPlayer("Moderatrice");
        const other = await registerPlayer("Curieux");
        await makeAdmin(admin);
        const run = await verifiedRun(nova, 61000);
        await env.DB.prepare("UPDATE replay_metadata SET verification_status = 'PENDING' WHERE record_id = ?1").bind(run.recordId).run();
        expect((await api("GET", `${V}/records/${run.recordId}/replay`, { token: other.token })).status).toBe(404);
        const seen = await api("GET", `${V}/records/${run.recordId}/replay`, { token: admin.token });
        expect(seen.status).toBe(200);
        expect(seen.headers.get("x-admin-replay-status")).toBe("PENDING");
        await env.DB.prepare("INSERT INTO player_playtime (player_id, level_key, seconds, updated_at) VALUES (?1, 'test_01', 30, '2026-11-01T10:00:00.000Z')").bind(nova.id).run();
        const list = await api("GET", `${V}/admin/players/${nova.id}/levels`, { token: admin.token });
        expect(list.body.data.levels[0].replay).toMatchObject({ available: false, watchable: true, status: "PENDING" });
        expect(list.body.data.levels[0].replay.reason).toBeTruthy();
        // No replay sent: unavailable, even to an administrator.
        const sub = await api("POST", `${V}/records/submit`, { token: nova.token, body: submission({ level_id: "test_02", level_hash: LEVEL2_HASH, time_ms: 70000 }) });
        expect(sub.status).toBe(201);
        const none = await api("GET", `${V}/records/${sub.body.data.record.id}/replay`, { token: admin.token });
        expect(none.status).toBe(404);
        expect(none.body.error.code).toBe("REPLAY_NOT_FOUND");
    });
});
