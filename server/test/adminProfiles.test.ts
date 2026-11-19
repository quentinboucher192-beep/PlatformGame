// 2.75.3: an administrator sees everything of a profile (moderation) — a masked profile, an account
// that is not active, its tabs (statistics, time played, follows, creations, progressions), the
// records and replays not verified (marked, never counted as verified), its privacy settings,
// friends, clan requests, real presence — identified by the role of the SESSION, never by a
// parameter or a header. Each lock: an administrator sees, another player is refused or sees
// nothing, exactly as before. A country the player hides stays hidden, administrators included;
// no secret is ever sent; a technical impossibility keeps its reason; the Journal groups the
// consultations.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { LEVEL_HASH, api, buildReplay, registerPlayer, seedAll, submission, verifiedRun, type TestPlayer } from "./helpers";

beforeEach(seedAll);

const V = "/api/v1";

async function makeAdmin(p: TestPlayer): Promise<void> {
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
}

async function journal(action: string) {
    return (
        await env.DB.prepare("SELECT player_id, details FROM audit_log WHERE action = ?1 ORDER BY id").bind(action).all<{ player_id: string; details: string }>()
    ).results.map((r) => ({ player_id: r.player_id, details: JSON.parse(r.details) as Record<string, any> }));
}

async function mask(p: TestPlayer): Promise<void> {
    expect((await api("PATCH", `${V}/players/me`, { token: p.token, body: { profile_visibility: "private" } })).status).toBe(200);
}

/** Three players: Nova (watched), a moderator (admin), a curious one (not admin). */
async function cast() {
    const nova = await registerPlayer("NovaRider");
    const admin = await registerPlayer("Moderatrice");
    const other = await registerPlayer("Curieux");
    await makeAdmin(admin);
    return { nova, admin, other };
}

/** A PENDING record of `p` (sent without its replay): never public. */
async function pendingRecord(p: TestPlayer, timeMs = 70000): Promise<string> {
    const res = await api("POST", `${V}/records/submit`, { token: p.token, body: submission({ time_ms: timeMs }) });
    expect(res.status).toBe(201);
    return res.body.data.record.id as string;
}

describe("2.75.3: the profile itself", () => {
    it("a masked profile: the others see its reduced form, an administrator all of it with what the others meet", async () => {
        const { nova, admin, other } = await cast();
        await verifiedRun(nova, 61000);
        await mask(nova);
        // A country Nova hides, a hidden presence, private follows.
        await env.DB.prepare("UPDATE players SET country_code = 'FR', region_code = 'IDF', location_visibility = 'hidden', presence_visible = 0, follows_visibility = 'private', presence_state = 'active', presence_at = ?2 WHERE id = ?1")
            .bind(nova.id, new Date().toISOString())
            .run();
        // Another player: exactly the reduced profile of before, nothing of the administrator's view.
        for (const token of [other.token, undefined]) {
            const seen = await api("GET", `${V}/players/${nova.id}`, { token });
            expect(seen.status).toBe(200);
            expect(seen.body.data.player.hidden).toBe(true);
            expect(seen.body.data.statistics).toBeNull();
            expect(seen.body.data.playtime).toBeNull();
            expect(seen.body.data.badges).toBeNull();
            expect(seen.body.data.cosmetics).toBeNull();
            expect(seen.body.data.admin_view).toBeUndefined();
        }
        // A parameter or a header is no key.
        const spoof = await api("GET", `${V}/players/${nova.id}?admin=1`, { token: other.token, headers: { "x-admin-view": "1", "x-role": "admin" } });
        expect(spoof.body.data.statistics).toBeNull();
        expect(spoof.body.data.admin_view).toBeUndefined();
        expect(await journal("admin_profile_viewed")).toHaveLength(0);
        // The administrator: everything, with what the others meet.
        const res = await api("GET", `${V}/players/${nova.id}`, { token: admin.token });
        expect(res.status).toBe(200);
        const d = res.body.data;
        expect(d.player.hidden).toBe(true); // what the others meet, said
        expect(d.statistics.verified_records).toBe(1);
        expect(d.playtime).not.toBeNull();
        expect(d.achievements).not.toBeNull();
        expect(d.admin_view.locks).toEqual(["profile_private", "presence_hidden", "follows_private"]);
        expect(d.admin_view.presence).toBe("online");
        expect(d.admin_view.presence_hidden).toBe(true);
        expect(d.admin_view.privacy).toMatchObject({ profile_visibility: "private", location_visibility: "hidden", follows_visibility: "private", presence_visible: false });
        // The player's consent: a hidden country and region stay hidden, even from an administrator.
        expect(d.player.country_code).toBeNull();
        expect(d.player.region_code).toBeNull();
        expect(JSON.stringify(d)).not.toContain('"FR"');
        expect(JSON.stringify(d)).not.toContain("IDF");
        // Never a secret.
        for (const secret of ["password", "token", "ip_hash", "device_id", "tos_accepted_at"]) expect(JSON.stringify(d)).not.toContain(secret);
        // The owner sees their own profile as before, without the administrator's view.
        const own = await api("GET", `${V}/players/${nova.id}`, { token: nova.token });
        expect(own.body.data.admin_view).toBeUndefined();
        expect(own.body.data.player.country_code).toBe("FR");
    });

    it("an account that is not active: not found for the others, seen by an administrator (marked)", async () => {
        const { nova, admin, other } = await cast();
        await env.DB.prepare("UPDATE players SET account_status = 'suspended' WHERE id = ?1").bind(nova.id).run();
        expect((await api("GET", `${V}/players/${nova.id}`, { token: other.token })).status).toBe(404);
        expect((await api("GET", `${V}/players/${nova.id}/statistics`, { token: other.token })).status).toBe(404);
        const res = await api("GET", `${V}/players/${nova.id}`, { token: admin.token });
        expect(res.status).toBe(200);
        expect(res.body.data.admin_view.locks).toContain("account_inactive");
        expect(res.body.data.admin_view.account_status).toBe("suspended");
        expect((await api("GET", `${V}/players/${nova.id}/statistics`, { token: admin.token })).status).toBe(200);
        // A suspended administrator is no administrator.
        await env.DB.prepare("UPDATE players SET account_status = 'suspended' WHERE id = ?1").bind(admin.id).run();
        expect((await api("GET", `${V}/players/${other.id}`, { token: admin.token })).body.data?.admin_view).toBeUndefined();
    });

    it("friends and clan requests: never shown to the others, to an administrator without the message", async () => {
        const { nova, admin, other } = await cast();
        const friend = await registerPlayer("Amie");
        const now = new Date().toISOString();
        await env.DB.batch([
            env.DB.prepare("INSERT INTO player_friends (player_id, friend_id, status, created_at) VALUES (?1, ?2, 'accepted', ?3)").bind(nova.id, friend.id, now),
            env.DB.prepare("INSERT INTO player_friends (player_id, friend_id, status, created_at) VALUES (?2, ?1, 'accepted', ?3)").bind(nova.id, friend.id, now),
            env.DB.prepare("INSERT INTO player_friends (player_id, friend_id, status, created_at) VALUES (?1, ?2, 'pending', ?3)").bind(other.id, nova.id, now),
        ]);
        await env.DB.prepare("UPDATE players SET country_code = 'DE', location_visibility = 'hidden' WHERE id = ?1").bind(friend.id).run();
        const clan = await api("POST", `${V}/clans`, {
            token: other.token,
            body: { name: "Les Veilleurs", tag: "VEIL", emblem: { shape: "star", primary: "#FFD27A", secondary: "#a66bff" }, join_policy: "request" },
        });
        expect(clan.status).toBe(201);
        await env.DB.prepare("INSERT INTO clan_requests (clan_id, player_id, kind, message, created_at) VALUES (?1, ?2, 'apply', 'mot secret du joueur', ?3)")
            .bind(clan.body.data.clan.id, nova.id, now)
            .run();
        const seen = await api("GET", `${V}/players/${nova.id}`, { token: other.token });
        expect(seen.body.data.admin_view).toBeUndefined();
        expect(JSON.stringify(seen.body)).not.toContain(friend.id);
        const res = await api("GET", `${V}/players/${nova.id}`, { token: admin.token });
        const f = res.body.data.admin_view.friends;
        expect(f).toMatchObject({ total: 1, incoming: 1, outgoing: 0 });
        expect(f.list.map((x: { player: { id: string } }) => x.player.id)).toEqual([friend.id]);
        // The friend's hidden country stays hidden too.
        expect(f.list[0].player.country_code).toBeNull();
        const reqs = res.body.data.admin_view.clan_requests;
        expect(reqs).toHaveLength(1);
        expect(reqs[0]).toMatchObject({ kind: "apply", clan: { tag: "VEIL", name: "Les Veilleurs" } });
        expect(JSON.stringify(res.body)).not.toContain("mot secret du joueur");
    });

    it("the Journal: one entry per administrator and player, the consultations grouped (count, last one, locks)", async () => {
        const { nova, admin, other } = await cast();
        await mask(nova);
        for (let i = 0; i < 3; i++) expect((await api("GET", `${V}/players/${nova.id}`, { token: admin.token })).status).toBe(200);
        // A tab of the same profile joins the same entry.
        expect((await api("GET", `${V}/players/${nova.id}/statistics`, { token: admin.token })).status).toBe(200);
        // Another player looked at: its own entry.
        expect((await api("GET", `${V}/players/${other.id}`, { token: admin.token })).status).toBe(200);
        const entries = await journal("admin_profile_viewed");
        expect(entries).toHaveLength(2);
        const novaEntry = entries.find((e) => e.details.target === nova.id)!;
        expect(novaEntry.player_id).toBe(admin.id);
        expect(novaEntry.details).toMatchObject({ count: 4, target_username: "NovaRider", locks: ["profile_private"], lock: "profile_private" });
        expect(entries.find((e) => e.details.target === other.id)!.details).toMatchObject({ count: 1, locks: [] });
        // An hour later, a new entry.
        await env.DB.prepare("UPDATE audit_log SET details = json_set(details, '$.last_at', '2020-01-01T00:00:00.000Z') WHERE action = 'admin_profile_viewed'").run();
        await api("GET", `${V}/players/${nova.id}`, { token: admin.token });
        expect(await journal("admin_profile_viewed")).toHaveLength(3);
        // The Journal tab reads them (administrators only).
        const log = await api("GET", `${V}/admin/log?kind=account`, { token: admin.token });
        expect(log.status).toBe(200);
        const first = log.body.data.entries.find((e: { action: string; target: { id: string } | null; count: number }) => e.action === "admin_profile_viewed" && e.target?.id === nova.id && e.count === 4);
        expect(first).toMatchObject({ locks: ["profile_private"], items: [] });
        expect(first.last_at).toBeTruthy();
        expect((await api("GET", `${V}/admin/log?kind=account`, { token: other.token })).status).toBe(404);
        // The others never write there.
        await api("GET", `${V}/players/${nova.id}`, { token: other.token });
        expect((await journal("admin_profile_viewed")).every((e) => e.player_id === admin.id)).toBe(true);
    });
});

describe("2.75.3: the tabs of a profile", () => {
    it("statistics and time played of a masked profile: 403 for the others, seen by an administrator", async () => {
        const { nova, admin, other } = await cast();
        await mask(nova);
        for (const path of ["statistics", "playtime"]) {
            const refused = await api("GET", `${V}/players/${nova.id}/${path}`, { token: other.token });
            expect(refused.status).toBe(403);
            expect(refused.body.error.code).toBe("PROFILE_PRIVATE");
            const seen = await api("GET", `${V}/players/${nova.id}/${path}`, { token: admin.token });
            expect(seen.status).toBe(200);
            expect(seen.body.data.admin_view.locks).toEqual(["profile_private"]);
        }
    });

    it("private follows: FOLLOWS_PRIVATE for the others, the lists for an administrator", async () => {
        const { nova, admin, other } = await cast();
        expect((await api("POST", `${V}/players/${nova.id}/follow`, { token: other.token })).status).toBe(200);
        await env.DB.prepare("UPDATE players SET follows_visibility = 'private' WHERE id = ?1").bind(nova.id).run();
        for (const list of ["followers", "following"]) {
            const refused = await api("GET", `${V}/players/${nova.id}/${list}`, { token: other.token });
            expect(refused.status).toBe(403);
            expect(refused.body.error.code).toBe("FOLLOWS_PRIVATE");
            const seen = await api("GET", `${V}/players/${nova.id}/${list}`, { token: admin.token });
            expect(seen.status).toBe(200);
            expect(seen.body.data.admin_view.locks).toEqual(["follows_private"]);
        }
        const followers = await api("GET", `${V}/players/${nova.id}/followers`, { token: admin.token });
        expect(followers.body.data.players.map((p: { id: string }) => p.id)).toEqual([other.id]);
    });

    it("creations: the drafts and levels hidden by reports for an administrator only", async () => {
        const { nova, admin, other } = await cast();
        await env.DB.batch([
            env.DB.prepare(
                "INSERT INTO community_levels (id, author_id, title, title_norm, planet, status, created_at, updated_at) VALUES ('11111111-1111-4111-8111-111111111111', ?1, 'Public', 'public', 'p', 'published', '2026-11-01', '2026-11-01')",
            ).bind(nova.id),
            env.DB.prepare(
                "INSERT INTO community_levels (id, author_id, title, title_norm, planet, status, created_at, updated_at) VALUES ('22222222-2222-4222-8222-222222222222', ?1, 'Brouillon', 'brouillon', 'p', 'draft', '2026-11-02', '2026-11-02')",
            ).bind(nova.id),
        ]);
        const seen = await api("GET", `${V}/players/${nova.id}/creations`, { token: other.token });
        expect(seen.body.data.creations.map((c: { title: string }) => c.title)).toEqual(["Public"]);
        expect(seen.body.data.admin_view).toBeUndefined();
        const all = await api("GET", `${V}/players/${nova.id}/creations`, { token: admin.token });
        expect(all.body.data.creations.map((c: { title: string; status: string }) => [c.title, c.status]).sort()).toEqual([
            ["Brouillon", "draft"],
            ["Public", "published"],
        ]);
        expect(all.body.data.admin_view.locks).toEqual(["levels_unpublished"]);
    });
});

describe("2.75.3: records and their replays, verified or not", () => {
    it("the records of a profile: verified ones only for the others, every status for an administrator (never counted as verified)", async () => {
        const { nova, admin, other } = await cast();
        const ok = await verifiedRun(nova, 61000);
        const pending = await pendingRecord(nova);
        const refused = await verifiedRun(nova, 62000);
        await env.DB.prepare("UPDATE records SET verification_status = 'REJECTED', rejection_reason = 'temps impossible' WHERE id = ?1").bind(refused.recordId).run();
        const theirs = await api("GET", `${V}/players/${nova.id}/records?status=PENDING`, { token: other.token });
        expect(theirs.body.data.records.map((r: { id: string }) => r.id)).toEqual([ok.recordId]);
        expect(theirs.body.data.records[0].admin_view).toBeUndefined();
        const all = await api("GET", `${V}/players/${nova.id}/records`, { token: admin.token });
        expect(all.status).toBe(200);
        const byId = new Map(all.body.data.records.map((r: { id: string }) => [r.id, r]));
        expect(byId.size).toBe(3);
        expect((byId.get(pending) as any).admin_view).toMatchObject({ status: "PENDING", locks: ["replay_pending"] });
        expect((byId.get(refused.recordId) as any).admin_view).toMatchObject({ status: "REJECTED", rejection_reason: "temps impossible", locks: ["replay_rejected"] });
        expect((byId.get(ok.recordId) as any).admin_view.locks).toEqual([]);
        // A record not verified never counts as one: the statistics still count the verified only.
        const stats = await api("GET", `${V}/players/${nova.id}`, { token: admin.token });
        expect(stats.body.data.statistics.verified_records).toBe(1);
        expect(stats.body.data.admin_view.records).toEqual({ pending: 1, rejected: 1 });
        // On a masked profile: 403 for the others, everything for an administrator.
        await mask(nova);
        expect((await api("GET", `${V}/players/${nova.id}/records`, { token: other.token })).status).toBe(403);
        const masked = await api("GET", `${V}/players/${nova.id}/records?status=VERIFIED`, { token: admin.token });
        expect(masked.body.data.records.map((r: { id: string }) => r.id)).toEqual([ok.recordId]);
        expect(masked.body.data.admin_view.locks).toEqual(["profile_private"]);
    });

    it("a replay pending or refused: never served to the others, to an administrator marked and journaled", async () => {
        const { nova, admin, other } = await cast();
        const run = await verifiedRun(nova, 61000);
        await env.DB.prepare("UPDATE records SET verification_status = 'PENDING' WHERE id = ?1").bind(run.recordId).run();
        expect((await api("GET", `${V}/records/${run.recordId}`, { token: other.token })).status).toBe(404);
        expect((await api("GET", `${V}/records/${run.recordId}/replay`, { token: other.token })).status).toBe(404);
        const rec = await api("GET", `${V}/records/${run.recordId}`, { token: admin.token });
        expect(rec.status).toBe(200);
        expect(rec.body.data.admin_view).toMatchObject({ status: "PENDING", locks: ["replay_pending"] });
        expect(rec.body.data.rank).toBeNull();
        const rep = await api("GET", `${V}/records/${run.recordId}/replay`, { token: admin.token });
        expect(rep.status).toBe(200);
        expect(rep.headers.get("x-admin-replay-status")).toBe("PENDING");
        expect(rep.headers.get("x-admin-view")).toBeNull(); // no privacy lock here
        // A refused replay on a masked profile: both said.
        await env.DB.prepare("UPDATE records SET verification_status = 'VERIFIED' WHERE id = ?1").bind(run.recordId).run();
        await env.DB.prepare("UPDATE replay_metadata SET verification_status = 'REJECTED' WHERE record_id = ?1").bind(run.recordId).run();
        await mask(nova);
        expect((await api("GET", `${V}/records/${run.recordId}/replay`, { token: other.token })).status).toBe(403);
        const both = await api("GET", `${V}/records/${run.recordId}/replay`, { token: admin.token });
        expect(both.status).toBe(200);
        expect(both.headers.get("x-admin-view")).toBe("profile_private");
        expect(both.headers.get("x-admin-replay-status")).toBe("REJECTED");
        // One Journal entry for both views (grouped), the level named.
        const logged = await journal("admin_replay_viewed");
        expect(logged).toHaveLength(1);
        expect(logged[0]!.details).toMatchObject({ target: nova.id, count: 2, items: ["Niveau test"] });
        expect(logged[0]!.details.locks).toEqual(expect.arrayContaining(["replay_pending", "profile_private", "replay_rejected"]));
        // The owner: unmarked, as before.
        const own = await api("GET", `${V}/records/${run.recordId}/replay`, { token: nova.token });
        expect(own.status).toBe(200);
        expect(own.headers.get("x-admin-view")).toBeNull();
        expect(own.headers.get("x-admin-replay-status")).toBeNull();
    });

    it("a technical impossibility keeps its reason, even for an administrator", async () => {
        const { nova, admin } = await cast();
        const pending = await pendingRecord(nova);
        const none = await api("GET", `${V}/records/${pending}/replay`, { token: admin.token });
        expect(none.status).toBe(404);
        expect(none.body.error.code).toBe("REPLAY_NOT_FOUND");
        const run = await verifiedRun(nova, 61000);
        const meta = await env.DB.prepare("SELECT storage_key FROM replay_metadata WHERE record_id = ?1").bind(run.recordId).first<{ storage_key: string }>();
        await env.STORAGE.delete(meta!.storage_key);
        await mask(nova);
        const gone = await api("GET", `${V}/records/${run.recordId}/replay`, { token: admin.token });
        expect(gone.status).toBe(404);
        expect(gone.body.error.message).toContain("absent");
    });
});

describe("2.75.3: progressions (course à la vérification)", () => {
    const BOARD = `o:test_01:${LEVEL_HASH}`;
    let n = 0;
    async function run(p: TestPlayer, status: string, score: number, replay: boolean): Promise<string> {
        n += 1;
        const id = crypto.randomUUID();
        const key = replay ? `races/${p.id}/${id}.cprz` : null;
        if (key) await env.STORAGE.put(key, await buildReplay({ attemptId: `a-${n}`, durationMs: 40000 }));
        const at = new Date(Date.now() - n * 1000).toISOString();
        await env.DB.prepare(
            `INSERT INTO race_runs (id, board, player_id, attempt_id, claim, time_ms, score, route, bosses_beaten, waves, finished, status, replay_key, created_at, verified_at)
             VALUES (?1, ?2, ?3, ?4, '{}', 40000, ?5, 0.5, 0, 0, 0, ?6, ?7, ?8, ?9)`,
        )
            .bind(id, BOARD, p.id, `a-${n}`, score, status, key, at, status === "VERIFIED" ? at : null)
            .run();
        return id;
    }

    it("a masked profile's progressions and the attempts not verified: refused to the others, listed for an administrator", async () => {
        const { nova, admin, other } = await cast();
        const best = await run(nova, "VERIFIED", 42.5, true);
        const pending = await run(nova, "PENDING", 60, true);
        const refused = await run(nova, "REJECTED", 80, false);
        const theirs = await api("GET", `${V}/players/${nova.id}/races`, { token: other.token });
        expect(theirs.body.data.races.map((r: { run_id: string }) => r.run_id)).toEqual([best]);
        expect(theirs.body.data.unverified).toBeUndefined();
        await mask(nova);
        expect((await api("GET", `${V}/players/${nova.id}/races`, { token: other.token })).status).toBe(403);
        const all = await api("GET", `${V}/players/${nova.id}/races`, { token: admin.token });
        expect(all.status).toBe(200);
        expect(all.body.data.races.map((r: { run_id: string }) => r.run_id)).toEqual([best]);
        expect(all.body.data.admin_view.locks).toEqual(["profile_private"]);
        const u = all.body.data.unverified as { run_id: string; status: string; rank: null; has_replay: boolean; title: string }[];
        expect(u.map((r) => [r.run_id, r.status, r.has_replay]).sort()).toEqual(
            [
                [pending, "PENDING", true],
                [refused, "REJECTED", false],
            ].sort(),
        );
        expect(u.every((r) => r.rank === null && r.title === "Niveau test")).toBe(true);
        // Their replays: the pending one for the administrator (marked), never for the others.
        expect((await api("GET", `${V}/races/runs/${pending}/replay`, { token: other.token })).status).toBe(404);
        const rep = await api("GET", `${V}/races/runs/${pending}/replay`, { token: admin.token });
        expect(rep.status).toBe(200);
        expect(rep.headers.get("x-admin-view")).toBe("profile_private");
        expect(rep.headers.get("x-admin-replay-status")).toBe("PENDING");
        // The verified one of a masked profile: 403 for the others, served to the administrator.
        expect((await api("GET", `${V}/races/runs/${best}/replay`, { token: other.token })).status).toBe(403);
        expect((await api("GET", `${V}/races/runs/${best}/replay`, { token: admin.token })).headers.get("x-admin-view")).toBe("profile_private");
        // No replay kept: a technical impossibility, with its reason.
        const none = await api("GET", `${V}/races/runs/${refused}/replay`, { token: admin.token });
        expect(none.status).toBe(404);
        expect(none.body.error.code).toBe("REPLAY_NOT_FOUND");
        const logged = await journal("admin_replay_viewed");
        expect(logged).toHaveLength(1);
        expect(logged[0]!.details).toMatchObject({ target: nova.id, count: 2, items: ["Niveau test"] });
    });

    it("the race board: a masked profile's replay hidden from the others, watchable by an administrator (marked)", async () => {
        const { nova, admin, other } = await cast();
        await env.DB.prepare("UPDATE level_versions SET race = ?2 WHERE content_hash = ?1")
            .bind(LEVEL_HASH, JSON.stringify({ route: [[100, 500], [5000, 400]], bosses: [], ambushes: [] }))
            .run();
        await run(nova, "VERIFIED", 42.5, true);
        await mask(nova);
        const theirs = await api("GET", `${V}/races/board?kind=official&level_id=test_01`, { token: other.token });
        expect(theirs.status).toBe(200);
        expect(theirs.body.data.ranking[0]).toMatchObject({ has_replay: false });
        expect(theirs.body.data.ranking[0].admin_view).toBeUndefined();
        const seen = await api("GET", `${V}/races/board?kind=official&level_id=test_01`, { token: admin.token });
        expect(seen.body.data.ranking[0]).toMatchObject({ has_replay: true, admin_view: { locks: ["profile_private"] } });
    });
});

describe("2.75.3: ADMINISTRATION › Joueurs — latest levels", () => {
    it("a replay not verified is watchable by an administrator (its status said); `available` keeps its 2.75 meaning", async () => {
        const { nova, admin } = await cast();
        const run = await verifiedRun(nova, 61000);
        await env.DB.prepare("UPDATE records SET verification_status = 'PENDING' WHERE id = ?1").bind(run.recordId).run();
        await env.DB.prepare("INSERT INTO player_playtime (player_id, level_key, seconds, updated_at) VALUES (?1, 'test_01', 30, '2026-11-01T10:00:00.000Z')").bind(nova.id).run();
        const list = await api("GET", `${V}/admin/players/${nova.id}/levels`, { token: admin.token });
        expect(list.body.data.levels[0].replay).toMatchObject({ available: false, watchable: true, status: "PENDING" });
        expect(list.body.data.levels[0].best.verified).toBe(false);
    });
});
