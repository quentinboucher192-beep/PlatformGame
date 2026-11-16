// 2.50: « Migrer vers un nouveau compte » (docs/online/DEVICES.md). The one-time code made on
// the old computer (hashed, 24 h, used once, cancelled); the migration asked from the new
// account (one generic refusal, failures rate limited per account), merged in one batch keeping
// the best of both sides; the old account closed (no login, computer freed, username reserved
// 30 days); the progression to restore; the admin's preview and migration (merge or replace)
// after a full backup.

import { createExecutionContext, env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { performMigration } from "../src/accountMigration";
import { releaseClosedUsernames } from "../src/auth";
import type { StoredManifest } from "../src/backups";
import { ApiError, type Ctx } from "../src/http";
import type { PlayerRow } from "../src/model";
import { LEVEL2_HASH, LEVEL_HASH, api, deviceOf, registerPlayer, seedAll, setTrust, submission, verifiedRun, type TestPlayer } from "./helpers";

const V = "/api/v1";
const CODE_RE = /^[2-9A-HJKMNP-Z]{3}-[2-9A-HJKMNP-Z]{3}-[2-9A-HJKMNP-Z]{3}$/;

beforeEach(async () => {
    await seedAll();
    await env.DB.prepare("DELETE FROM backups").run();
});

function login(p: TestPlayer, device: unknown = deviceOf(p.username)) {
    return api("POST", `${V}/auth/login`, { body: { username: p.username, password: p.password, device } });
}

async function makeAdmin(p: TestPlayer): Promise<void> {
    await env.DB.prepare("UPDATE players SET role = 'admin' WHERE id = ?1").bind(p.id).run();
}

async function newCode(p: TestPlayer): Promise<string> {
    const r = await api("POST", `${V}/players/me/migration`, { token: p.token });
    expect(r.status, JSON.stringify(r.body)).toBe(201);
    return r.body.data.code as string;
}

function migrate(target: TestPlayer, source: { username: string; password: string }, code: string) {
    return api("POST", `${V}/players/me/migrate-from`, { token: target.token, body: { username: source.username, password: source.password, code } });
}

async function putCopy(p: TestPlayer, levels: number) {
    const r = await api("PUT", `${V}/players/me/progress-copy`, {
        token: p.token,
        body: {
            format: "cosmic.progress_copy",
            game_version: "2.47.0",
            summary: { levels, achievements: 3, stars: 5, play_seconds: 600, bosses: 2 },
            files: { progress: { completed: ["sylvanox_01"], levels }, boss_stats: { astrarque: { defeats: 2 } } },
        },
    });
    expect(r.status).toBe(201);
}

async function declare(p: TestPlayer, unlocked: string[]) {
    expect((await api("PUT", `${V}/players/me/achievements`, { token: p.token, body: { unlocked } })).status).toBe(200);
}

async function wear(p: TestPlayer, title: string) {
    const r = await api("PUT", `${V}/players/me/cosmetics`, {
        token: p.token,
        body: { suit: "suit_standard", trail: "trail_none", frame: "frame_simple", title, victory: "victory_classic" },
    });
    expect(r.body.data.cosmetics.title.id).toBe(title);
}

async function playtime(p: TestPlayer, rows: [string, number][]) {
    for (const [key, seconds] of rows)
        await env.DB.prepare("INSERT INTO player_playtime (player_id, level_key, seconds, updated_at) VALUES (?1, ?2, ?3, '2026-11-10T00:00:00.000Z')")
            .bind(p.id, key, seconds)
            .run();
}

async function wonReward(p: TestPlayer, kind: string, ref: string, at: string) {
    await env.DB.prepare(
        "INSERT INTO player_won_rewards (player_id, challenge_id, reward_kind, reward_ref, granted_at) VALUES (?1, 'daily-2026-11-10', ?2, ?3, ?4)",
    )
        .bind(p.id, kind, ref, at)
        .run();
}

async function raceRun(p: TestPlayer, attemptId: string) {
    await env.DB.prepare(
        "INSERT INTO race_runs (id, board, player_id, attempt_id, claim, time_ms, created_at) VALUES (?1, 'o:test_01:x', ?2, ?3, '{}', 50000, '2026-11-10T00:00:00.000Z')",
    )
        .bind(crypto.randomUUID(), p.id, attemptId)
        .run();
}

async function challengeRun(p: TestPlayer, tier: string) {
    await env.DB.prepare(
        `INSERT OR IGNORE INTO challenges (id, kind, period, date, end_date, source, level_source, level_id, level_version, level_hash, level_title,
           hearts, slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, created_at, updated_at)
         VALUES ('daily-2026-11-10', 'daily', '2026-11-10', '2026-11-10', '2026-11-11', 'admin', 'campaign', 'test_01', 1, ?1, 'Niveau test',
           3, 4, '[]', 1, 0, ?2, 90000, 70000, '2026-11-01T00:00:00.000Z', '2026-11-01T00:00:00.000Z')`,
    )
        .bind(LEVEL_HASH, "e".repeat(64))
        .run();
    await env.DB.prepare(
        `INSERT INTO challenge_runs (id, challenge_id, player_id, attempt_id, time_ms, deaths, damage_taken, level_hash, rules_hash, game_version,
           tier, status, late, created_at) VALUES (?1, 'daily-2026-11-10', ?2, ?3, 65000, 0, 0, ?4, ?5, '2.47.0', ?6, 'VERIFIED', 0, '2026-11-10T10:00:00.000Z')`,
    )
        .bind(crypto.randomUUID(), p.id, crypto.randomUUID(), LEVEL_HASH, "e".repeat(64), tier)
        .run();
}

async function count(sql: string, ...binds: unknown[]): Promise<number> {
    return (await env.DB.prepare(sql).bind(...binds).first<{ n: number }>())?.n ?? 0;
}

async function bestOf(p: TestPlayer, hash: string, category: string) {
    return env.DB.prepare("SELECT record_id, time_ms FROM player_bests WHERE player_id = ?1 AND level_hash = ?2 AND category = ?3")
        .bind(p.id, hash, category)
        .first<{ record_id: string; time_ms: number }>();
}

async function securityReasons(playerId: string): Promise<string[]> {
    const r = await env.DB.prepare("SELECT data FROM notifications WHERE player_id = ?1 AND kind = 'security' ORDER BY created_at")
        .bind(playerId)
        .all<{ data: string }>();
    return r.results.map((n) => JSON.parse(n.data).reason as string);
}

// ---------------------------------------------------------------------------------------------

describe("the migration code (old computer)", () => {
    it("is made once, stored hashed, never shown again, replaced, cancelled, and expires after 24 h", async () => {
        const orion = await registerPlayer("Orion");
        expect((await api("POST", `${V}/players/me/migration`)).status).toBe(401);
        expect((await api("GET", `${V}/players/me/migration`, { token: orion.token })).body.data).toEqual({ active: false, expires_at: null });
        const made = await api("POST", `${V}/players/me/migration`, { token: orion.token });
        expect(made.status).toBe(201);
        const code = made.body.data.code as string;
        expect(code).toMatch(CODE_RE);
        const left = Date.parse(made.body.data.expires_at) - Date.now();
        expect(left).toBeGreaterThan(23.9 * 3600_000);
        expect(left).toBeLessThanOrEqual(24 * 3600_000);
        // Stored hashed only.
        const row = await env.DB.prepare("SELECT * FROM account_migration_codes WHERE player_id = ?1").bind(orion.id).first<Record<string, string>>();
        expect(row!.code_hash).toMatch(/^[0-9a-f]{64}$/);
        expect(JSON.stringify(row)).not.toContain(code.replace(/-/g, ""));
        expect(JSON.stringify(row)).not.toContain(code);
        // Asked again: whether it is active, never the code.
        const got = await api("GET", `${V}/players/me/migration`, { token: orion.token });
        expect(got.body.data).toEqual({ active: true, expires_at: made.body.data.expires_at });
        // A new code replaces the previous one (one per account).
        const second = await newCode(orion);
        expect(second).not.toBe(code);
        expect(await count("SELECT COUNT(*) AS n FROM account_migration_codes WHERE player_id = ?1", orion.id)).toBe(1);
        // Cancelled.
        const del = await api("DELETE", `${V}/players/me/migration`, { token: orion.token });
        expect(del.body.data).toMatchObject({ cancelled: true, active: false });
        expect((await api("GET", `${V}/players/me/migration`, { token: orion.token })).body.data.active).toBe(false);
        expect((await api("DELETE", `${V}/players/me/migration`, { token: orion.token })).body.data.cancelled).toBe(false);
        // Expired after 24 h.
        await newCode(orion);
        await env.DB.prepare("UPDATE account_migration_codes SET expires_at = '2026-01-01T00:00:00.000Z' WHERE player_id = ?1").bind(orion.id).run();
        expect((await api("GET", `${V}/players/me/migration`, { token: orion.token })).body.data).toEqual({ active: false, expires_at: null });
    });
});

describe("migrating with the code (new computer)", () => {
    it("refuses a wrong password, a wrong, replaced, cancelled or expired code with one generic answer, and limits the failures", async () => {
        const orion = await registerPlayer("Orion");
        const nova = await registerPlayer("OrionNew");
        await playtime(orion, [["test_01", 100]]);
        const old = await newCode(orion);
        const code = await newCode(orion); // `old` is replaced
        const generic = (r: { status: number; body: any }) => {
            expect(r.status, JSON.stringify(r.body)).toBe(403);
            expect(r.body.error.code).toBe("MIGRATION_INVALID");
            return r.body.error.message as string;
        };
        const messages = new Set<string>();
        messages.add(generic(await migrate(nova, { username: "Orion", password: "not the password" }, code)));
        messages.add(generic(await migrate(nova, orion, old)));
        messages.add(generic(await migrate(nova, { username: "Nobody", password: orion.password }, code)));
        messages.add(generic(await migrate(nova, orion, "not a code")));
        expect(messages.size).toBe(1); // nothing tells which field was wrong
        // The right password with a wrong code: the old account's owner is told.
        expect(await securityReasons(orion.id)).toContain("migration_refused");
        // 5 failures in 10 minutes for this new account: it waits, even with the right code.
        await env.DB.prepare("UPDATE account_migration_codes SET expires_at = '2026-01-01T00:00:00.000Z' WHERE player_id = ?1").bind(orion.id).run();
        generic(await migrate(nova, orion, code)); // expired
        const limited = await migrate(nova, orion, code);
        expect(limited.status).toBe(429);
        // Per old account too: 4 failures against Orion so far; one more from another account, then it waits.
        const third = await registerPlayer("Kestrel");
        const code2 = await newCode(orion);
        generic(await migrate(third, orion, "ZZZ-ZZZ-ZZZ"));
        expect((await migrate(third, orion, code2)).status).toBe(429);
        // Its own username: said plainly.
        const same = await migrate(nova, nova, code2);
        expect(same.status).toBe(409);
        expect(same.body.error.code).toBe("SAME_ACCOUNT");
        // Nothing moved, the old account is still open.
        expect(await count("SELECT COUNT(*) AS n FROM player_playtime WHERE player_id = ?1", orion.id)).toBe(1);
        expect((await login(orion)).status).toBe(200);
        expect(await count("SELECT COUNT(*) AS n FROM account_migrations")).toBe(0);
    });

    it("merges keeping the best of both sides: bests, achievements, rewards, play time, runs; unique keys respected", async () => {
        const vega = await registerPlayer("Vega"); // old account
        const nova = await registerPlayer("VegaNew"); // new account
        // Records: a better ANY% on the source, a better NO_DEATH on the target, a level only the source played.
        const a = await verifiedRun(vega, 61000);
        const b = await verifiedRun(vega, 70000, { level_id: "test_02", level_hash: LEVEL2_HASH });
        const c = await verifiedRun(vega, 66000, { category: "NO_DEATH" });
        const d = await verifiedRun(nova, 65000);
        const e = await verifiedRun(nova, 62000, { category: "NO_DEATH" });
        for (const r of [a, b, c, d, e]) expect(r.status).toBe("VERIFIED");
        // A source record whose (player, attempt, category) the target already has: it stays on the source.
        const clash = submission({ level_id: "test_02", level_hash: LEVEL2_HASH, category: "NO_DEATH", time_ms: 75000 });
        const f = await verifiedRun(vega, 75000, clash);
        await env.DB.prepare(
            `INSERT INTO records (id, player_id, level_id, level_version, level_hash, category, time_ms, deaths, game_version, attempt_id,
               verification_status, created_at) VALUES (?1, ?2, 'test_02', 1, ?3, 'NO_DEATH', 90000, 0, '2.1.0', ?4, 'REJECTED', '2026-11-10T00:00:00.000Z')`,
        )
            .bind(crypto.randomUUID(), nova.id, LEVEL2_HASH, clash.attempt_id)
            .run();
        // Races (one attempt id on both sides) and challenges.
        await raceRun(vega, "11111111-1111-4111-8111-111111111111");
        await raceRun(vega, "22222222-2222-4222-8222-222222222222");
        await raceRun(nova, "11111111-1111-4111-8111-111111111111");
        await challengeRun(vega, "gold");
        await challengeRun(nova, "bronze");
        // Achievements, look, badges, rewards, play time.
        await declare(vega, ["voyage_5", "voyage_sylvanox"]);
        await declare(nova, ["voyage_first", "voyage_sylvanox"]);
        await wear(vega, "title_explorateur");
        await wear(nova, "title_gardien_sylvanox");
        await env.DB.prepare("INSERT INTO player_badges (player_id, ids, updated_at) VALUES (?1, '[\"voyage_5\"]', '2026-11-10T00:00:00.000Z')").bind(vega.id).run();
        await wonReward(vega, "title", "srv:1", "2026-11-01T00:00:00.000Z");
        await wonReward(nova, "title", "srv:1", "2026-11-05T00:00:00.000Z");
        await wonReward(vega, "suit", "srvsuit:2", "2026-11-02T00:00:00.000Z");
        await playtime(vega, [["test_01", 100], ["other", 50]]);
        await playtime(nova, [["test_01", 30], ["test_02", 20]]);
        await setTrust(vega.id, 2);
        await setTrust(nova.id, 0);

        const res = await migrate(nova, vega, (await newCode(vega)).toLowerCase().replace(/-/g, " "));
        expect(res.status, JSON.stringify(res.body)).toBe(200);
        expect(res.body.data.migration).toMatchObject({
            mode: "merge",
            items: ["records", "achievements", "rewards", "playtime", "progress"],
            close_source: true,
            source: { id: vega.id, username: "Vega", closed: true },
            target: { id: nova.id, username: "VegaNew" },
            restore: null, // the old computer never sent a copy
        });

        // Bests: the better time of each board, its record now the target's.
        expect(await bestOf(nova, LEVEL_HASH, "ANY_PERCENT")).toEqual({ record_id: a.recordId, time_ms: 61000 });
        expect(await bestOf(nova, LEVEL2_HASH, "ANY_PERCENT")).toEqual({ record_id: b.recordId, time_ms: 70000 });
        expect(await bestOf(nova, LEVEL_HASH, "NO_DEATH")).toEqual({ record_id: e.recordId, time_ms: 62000 });
        // Every record and replay moved, except the clashing one (kept on the source with its best and replay).
        expect(await count("SELECT COUNT(*) AS n FROM records WHERE player_id = ?1", nova.id)).toBe(6);
        expect(await count("SELECT COUNT(*) AS n FROM records WHERE player_id = ?1", vega.id)).toBe(1);
        expect(await count("SELECT COUNT(*) AS n FROM records WHERE id = ?1 AND player_id = ?2", f.recordId, vega.id)).toBe(1);
        expect(await count("SELECT COUNT(*) AS n FROM replay_metadata WHERE player_id = ?1", nova.id)).toBe(5);
        expect(await count("SELECT COUNT(*) AS n FROM player_bests WHERE player_id = ?1", vega.id)).toBe(1);
        expect(await count("SELECT COUNT(*) AS n FROM player_bests b JOIN records r ON r.id = b.record_id WHERE b.player_id <> r.player_id")).toBe(0);
        // The trust the verified records earned: the higher of the two.
        expect(await count("SELECT trust_level AS n FROM players WHERE id = ?1", nova.id)).toBe(2);
        // Races: the clashing attempt stays; challenge runs move (the target now has both).
        expect(await count("SELECT COUNT(*) AS n FROM race_runs WHERE player_id = ?1", nova.id)).toBe(2);
        expect(await count("SELECT COUNT(*) AS n FROM race_runs WHERE player_id = ?1", vega.id)).toBe(1);
        expect(await count("SELECT COUNT(*) AS n FROM challenge_runs WHERE player_id = ?1", nova.id)).toBe(2);
        // Achievements: the union, stars counted by the server; badges: the target had none.
        const ach = await env.DB.prepare("SELECT ids, stars, count FROM player_achievements WHERE player_id = ?1").bind(nova.id).first<{ ids: string; stars: number; count: number }>();
        expect(JSON.parse(ach!.ids)).toEqual(["voyage_5", "voyage_first", "voyage_sylvanox"]);
        expect(ach).toMatchObject({ stars: 4, count: 3 });
        expect(await count("SELECT COUNT(*) AS n FROM player_achievements WHERE player_id = ?1", vega.id)).toBe(0);
        expect((await env.DB.prepare("SELECT ids FROM player_badges WHERE player_id = ?1").bind(nova.id).first<{ ids: string }>())?.ids).toBe('["voyage_5"]');
        // The look: the target's title kept.
        expect((await env.DB.prepare("SELECT title FROM player_cosmetics WHERE player_id = ?1").bind(nova.id).first<{ title: string }>())?.title).toBe(
            "title_gardien_sylvanox",
        );
        // Rewards: the union, the earliest date of a reward both won.
        const won = await env.DB.prepare("SELECT reward_ref, granted_at FROM player_won_rewards WHERE player_id = ?1 ORDER BY reward_ref").bind(nova.id).all();
        expect(won.results).toEqual([
            { reward_ref: "srv:1", granted_at: "2026-11-01T00:00:00.000Z" },
            { reward_ref: "srvsuit:2", granted_at: "2026-11-02T00:00:00.000Z" },
        ]);
        // Play time: summed per level.
        const pt = await env.DB.prepare("SELECT level_key, seconds FROM player_playtime WHERE player_id = ?1 ORDER BY level_key").bind(nova.id).all();
        expect(pt.results).toEqual([
            { level_key: "other", seconds: 50 },
            { level_key: "test_01", seconds: 130 },
            { level_key: "test_02", seconds: 20 },
        ]);
        expect(res.body.data.counts.playtime.seconds).toBe(200);
        // The profile shows it.
        const profile = await api("GET", `${V}/players/${nova.id}`);
        expect(profile.body.data.achievements).toMatchObject({ stars: 4, count: 3 });
        expect(profile.body.data.statistics.personal_bests).toBe(3);
        // Journal (kind account) and the player's own log.
        const m = await env.DB.prepare("SELECT * FROM account_migrations").first<Record<string, unknown>>();
        expect(m).toMatchObject({ source_id: vega.id, target_id: nova.id, mode: "merge", close_source: 1, by_player: null, checked: 1 });
    });

    it("closes the old account: no login, sessions revoked, computer freed, username reserved 30 days, code used once", async () => {
        const orion = await registerPlayer("Orion");
        const nova = await registerPlayer("OrionNew");
        const code = await newCode(orion);
        const res = await migrate(nova, orion, code);
        expect(res.status, JSON.stringify(res.body)).toBe(200);
        // No login (said only after the right password), no session.
        const again = await login(orion);
        expect(again.status).toBe(403);
        expect(again.body.error.code).toBe("ACCOUNT_CLOSED");
        expect((await login({ ...orion, password: "wrong password" })).body.error.code).toBe("INVALID_CREDENTIALS");
        expect((await api("GET", `${V}/auth/me`, { token: orion.token })).status).toBe(401);
        expect((await api("POST", `${V}/auth/refresh`, { body: { refresh_token: orion.refresh } })).status).toBe(401);
        const row = await env.DB.prepare("SELECT * FROM players WHERE id = ?1").bind(orion.id).first<PlayerRow>();
        expect(row).toMatchObject({ account_status: "suspended", merged_into: nova.id, device_id: null, role: "player" });
        expect(row!.closed_at).toBeTruthy();
        // Hidden like a suspended account.
        expect((await api("GET", `${V}/players/${orion.id}`)).status).toBe(404);
        // The code was used once: never again.
        const used = await env.DB.prepare("SELECT used_at, used_by FROM account_migration_codes WHERE player_id = ?1").bind(orion.id).first();
        expect(used).toMatchObject({ used_by: nova.id });
        const other = await registerPlayer("Kestrel");
        expect((await migrate(other, orion, code)).body.error.code).toBe("MIGRATION_INVALID");
        // Its computer is free: a new account can be made there.
        const fresh = await api("POST", `${V}/auth/register`, {
            body: { username: "OrionAgain", password: "correct horse battery", accept_tos: true, tos_version: "2026-11", device: deviceOf("Orion") },
        });
        expect(fresh.status).toBe(201);
        // Its username is reserved for 30 days...
        const taken = await api("POST", `${V}/auth/register`, {
            body: { username: "orion", password: "correct horse battery", accept_tos: true, tos_version: "2026-11", device: { id: "1".repeat(32), name: "PC" } },
        });
        expect(taken.body.error.code).toBe("USERNAME_TAKEN");
        expect((await api("GET", `${V}/auth/username-available?username=Orion`)).body.data.available).toBe(false);
        // ... then free (a registration that wants it, or the daily maintenance).
        await env.DB.prepare("UPDATE players SET closed_at = '2026-01-01T00:00:00.000Z' WHERE id = ?1").bind(orion.id).run();
        expect((await api("GET", `${V}/auth/username-available?username=Orion`)).body.data.available).toBe(true);
        const reborn = await api("POST", `${V}/auth/register`, {
            body: { username: "Orion", password: "correct horse battery", accept_tos: true, tos_version: "2026-11", device: { id: "2".repeat(32), name: "PC" } },
        });
        expect(reborn.status, JSON.stringify(reborn.body)).toBe(201);
        expect((await env.DB.prepare("SELECT username, username_normalized FROM players WHERE id = ?1").bind(orion.id).first())).toEqual({
            username: "Orion",
            username_normalized: "~" + orion.id,
        });
        // The maintenance frees the others.
        const vega = await registerPlayer("Vega");
        const vegaNew = await registerPlayer("VegaNew");
        expect((await migrate(vegaNew, vega, await newCode(vega))).status).toBe(200);
        await env.DB.prepare("UPDATE players SET closed_at = '2026-01-01T00:00:00.000Z' WHERE id = ?1").bind(vega.id).run();
        await releaseClosedUsernames(env.DB).run();
        expect((await env.DB.prepare("SELECT username_normalized FROM players WHERE id = ?1").bind(vega.id).first<{ username_normalized: string }>())?.username_normalized).toBe(
            "~" + vega.id,
        );
        // Both accounts were told.
        expect(await securityReasons(orion.id)).toContain("migration_out");
        expect(await securityReasons(nova.id)).toContain("migration_in");
    });

    it("an admin account is never closed if it is the last one", async () => {
        const boss = await registerPlayer("Bossa");
        await makeAdmin(boss);
        const nova = await registerPlayer("BossaNew");
        const r = await migrate(nova, boss, await newCode(boss));
        expect(r.status).toBe(409);
        expect(r.body.error.code).toBe("LAST_ADMIN");
        expect((await login(boss)).status).toBe(200);
    });

    it("the old computer's progression is waiting on the new one: read, downloaded as stored, later, done", async () => {
        const orion = await registerPlayer("Orion");
        const nova = await registerPlayer("OrionNew");
        await putCopy(orion, 2);
        await putCopy(orion, 3);
        expect((await api("GET", `${V}/players/me/progress-restore`, { token: nova.token })).body.data.restore).toBeNull();
        const res = await migrate(nova, orion, await newCode(orion));
        expect(res.status).toBe(200);
        expect(res.body.data.migration.restore).toMatchObject({ from_username: "Orion", copy: { summary: { levels: 3 } } });
        // The notification of the new account offers to restore it.
        const notes = await api("GET", `${V}/notifications`, { token: nova.token });
        const n = notes.body.data.notifications.find((x: any) => x.kind === "security" && x.data.reason === "migration_in");
        expect(n.text).toContain("Les données de Orion ont été migrées sur votre compte");
        expect(n.data.action).toBe("progress_restore");
        // What is waiting.
        const got = await api("GET", `${V}/players/me/progress-restore`, { token: nova.token });
        expect(got.body.data.restore).toMatchObject({
            from_username: "Orion",
            postponed_at: null,
            copy: { game_version: "2.47.0", device_name: "PC de test", summary: { levels: 3, bosses: 2 } },
        });
        // The file exactly as the old computer sent it.
        const copyKey = (await env.DB.prepare("SELECT file_key FROM progress_copies WHERE player_id = ?1 ORDER BY id DESC LIMIT 1").bind(orion.id).first<{ file_key: string }>())!.file_key;
        const original = await (await env.STORAGE.get(copyKey))!.text();
        const file = await api("GET", `${V}/players/me/progress-restore/file`, { token: nova.token });
        expect(file.status).toBe(200);
        expect(file.headers.get("content-type")).toContain("application/json");
        expect(file.body).toEqual(JSON.parse(original));
        expect(file.body.files.progress).toEqual({ completed: ["sylvanox_01"], levels: 3 });
        // Nobody else gets it.
        expect((await api("GET", `${V}/players/me/progress-restore/file`, { token: (await registerPlayer("Kestrel")).token })).status).toBe(404);
        // Later: kept.
        const later = await api("POST", `${V}/players/me/progress-restore`, { token: nova.token, body: { action: "later" } });
        expect(later.body.data.restore.postponed_at).toBeTruthy();
        expect((await api("GET", `${V}/players/me/progress-restore`, { token: nova.token })).body.data.restore).not.toBeNull();
        expect((await api("POST", `${V}/players/me/progress-restore`, { token: nova.token, body: { action: "maybe" } })).status).toBe(400);
        // Done: forgotten, its file deleted.
        const restoreKey = (await env.DB.prepare("SELECT file_key FROM progress_restores WHERE player_id = ?1").bind(nova.id).first<{ file_key: string }>())!.file_key;
        expect(restoreKey.startsWith(`progress/${nova.id}/`)).toBe(true);
        const done = await api("POST", `${V}/players/me/progress-restore`, { token: nova.token, body: { action: "done" } });
        expect(done.body.data.restore).toBeNull();
        expect(await env.STORAGE.get(restoreKey)).toBeNull();
        expect((await api("GET", `${V}/players/me/progress-restore`, { token: nova.token })).body.data.restore).toBeNull();
        expect((await api("GET", `${V}/players/me/progress-restore/file`, { token: nova.token })).status).toBe(404);
        expect((await api("POST", `${V}/players/me/progress-restore`, { token: nova.token, body: { action: "done" } })).status).toBe(404);
    });

    it("rolls the whole batch back when a check fails inside it (code already used, account closed meanwhile)", async () => {
        const orion = await registerPlayer("Orion");
        const nova = await registerPlayer("OrionNew");
        await playtime(orion, [["test_01", 100]]);
        const load = (id: string) => env.DB.prepare("SELECT * FROM players WHERE id = ?1").bind(id).first<PlayerRow>();
        const ctx = {
            request: new Request("https://test.invalid/"),
            env,
            exec: createExecutionContext(),
            url: new URL("https://test.invalid/"),
            params: {},
            requestId: "test",
            ip: "test",
        } as unknown as Ctx;
        const plan = async (codeHash: string | null) => ({
            id: crypto.randomUUID(),
            source: (await load(orion.id))!,
            target: (await load(nova.id))!,
            mode: "merge" as const,
            items: ["playtime"] as const,
            closeSource: true,
            by: null,
            backupId: null,
            codeHash,
            conflict: () => new ApiError(409, "TEST_CONFLICT", "conflit"),
        });
        // A code that this batch cannot claim (used, or another one).
        await newCode(orion);
        await expect(performMigration(ctx, await plan("0".repeat(64)))).rejects.toMatchObject({ code: "TEST_CONFLICT" });
        // The source closed between the checks and the batch.
        const p = await plan(null);
        await env.DB.prepare("UPDATE players SET account_status = 'suspended' WHERE id = ?1").bind(orion.id).run();
        await expect(performMigration(ctx, p)).rejects.toMatchObject({ code: "TEST_CONFLICT" });
        // Nothing changed.
        expect(await count("SELECT COUNT(*) AS n FROM player_playtime WHERE player_id = ?1", orion.id)).toBe(1);
        expect(await count("SELECT COUNT(*) AS n FROM player_playtime WHERE player_id = ?1", nova.id)).toBe(0);
        expect(await count("SELECT COUNT(*) AS n FROM account_migrations")).toBe(0);
        expect((await load(orion.id))?.closed_at).toBeNull();
        expect((await env.DB.prepare("SELECT used_at FROM account_migration_codes WHERE player_id = ?1").bind(orion.id).first())?.used_at).toBeNull();
    });
});

// ---------------------------------------------------------------------------------------------

async function backupRows(backupId: string, table: string): Promise<Record<string, unknown>[]> {
    const prefix = `backups/${backupId}/`;
    const manifest = JSON.parse(await (await env.STORAGE.get(prefix + "manifest.json"))!.text()) as StoredManifest;
    const t = manifest.tables.find((x) => x.name === table)!;
    const out: Record<string, unknown>[] = [];
    for (const part of t.parts)
        for (const line of (await (await env.STORAGE.get(prefix + part))!.text()).split("\n"))
            if (line) out.push(Object.fromEntries((JSON.parse(line) as unknown[]).map((v, i) => [t.columns[i]!, v])));
    return out;
}

function adminMigrate(admin: TestPlayer, sourceId: string, body: Record<string, unknown>) {
    return api("POST", `${V}/admin/players/${sourceId}/migrate`, { token: admin.token, body });
}

describe("administration: ADMINISTRATION › Joueurs › Migrer", () => {
    it("is refused to non-admins; the preview counts both accounts, what they share, the old computer's copy", async () => {
        const admin = await registerPlayer("NovaRider");
        await makeAdmin(admin);
        const vega = await registerPlayer("Vega");
        const nova = await registerPlayer("VegaNew");
        expect((await api("GET", `${V}/admin/players/${vega.id}/migration?to=VegaNew`, { token: nova.token })).status).toBe(404);
        expect((await adminMigrate(nova, vega.id, { to_username: "VegaNew", mode: "merge", items: ["playtime"], confirm: "MIGRER" })).status).toBe(404);
        await verifiedRun(vega, 61000);
        await verifiedRun(nova, 65000);
        await declare(vega, ["voyage_5", "voyage_sylvanox"]);
        await declare(nova, ["voyage_sylvanox"]);
        await playtime(vega, [["test_01", 100]]);
        await playtime(nova, [["test_01", 30]]);
        await wonReward(vega, "title", "srv:1", "2026-11-01T00:00:00.000Z");
        await putCopy(vega, 4);
        await env.DB.prepare(
            "INSERT INTO community_levels (id, author_id, title, title_norm, planet, status, created_at, updated_at) VALUES ('lvl-1', ?1, 'Mon niveau', 'mon niveau', 'p', 'published', '2026-11-01', '2026-11-01')",
        )
            .bind(vega.id)
            .run();
        const r = await api("GET", `${V}/admin/players/${vega.id}/migration?to=vegaNEW`, { token: admin.token });
        expect(r.status, JSON.stringify(r.body)).toBe(200);
        const d = r.body.data;
        expect(d.target_found).toBe(true);
        expect(d.source).toMatchObject({ id: vega.id, username: "Vega", account_status: "active", device: { name: "PC de test" } });
        expect(d.source.counts).toMatchObject({
            records: { records: 1, bests: 1, replays: 1 },
            achievements: { count: 2, stars: 3 },
            rewards: { won: 1, titles: 1, creations: 0 },
            playtime: { seconds: 100, boss_stats: 2 },
            levels: { published: 1, projects: 0 },
        });
        expect(d.target).toMatchObject({ id: nova.id, username: "VegaNew", counts: { records: { bests: 1 }, playtime: { seconds: 30 } } });
        expect(d.shared).toEqual({ bests: 1, bests_source_better: 1, playtime_levels: 1, rewards: 0, achievements_new: 1 });
        expect(d.copy).toMatchObject({ device_name: "PC de test", game_version: "2.47.0", summary: { levels: 4 } });
        expect(d.problems).toEqual([]);
        expect(d.confirm).toBe("MIGRER");
        // An unknown target, the account itself.
        const none = await api("GET", `${V}/admin/players/${vega.id}/migration?to=Personne`, { token: admin.token });
        expect(none.body.data).toMatchObject({ target_found: false, target: null, problems: ["TARGET_NOT_FOUND"] });
        expect((await api("GET", `${V}/admin/players/${vega.id}/migration?to=Vega`, { token: admin.token })).body.data.problems).toEqual(["SAME_ACCOUNT"]);
    });

    it("requires the confirmation and a valid target before anything (no backup, nothing moved)", async () => {
        const admin = await registerPlayer("NovaRider");
        await makeAdmin(admin);
        const vega = await registerPlayer("Vega");
        await registerPlayer("VegaNew");
        await playtime(vega, [["test_01", 100]]);
        const base = { to_username: "VegaNew", mode: "merge", items: ["playtime"] };
        for (const confirm of [undefined, "migrer ?", "OUI"]) {
            const r = await adminMigrate(admin, vega.id, { ...base, ...(confirm ? { confirm } : {}) });
            expect(r.status).toBe(400);
            expect(r.body.error.code).toBe("CONFIRMATION_REQUIRED");
        }
        expect((await adminMigrate(admin, vega.id, { ...base, items: ["everything"], confirm: "MIGRER" })).status).toBe(400);
        expect((await adminMigrate(admin, vega.id, { ...base, items: [], confirm: "MIGRER" })).status).toBe(400);
        expect((await adminMigrate(admin, vega.id, { ...base, mode: "swap", confirm: "MIGRER" })).status).toBe(400);
        expect((await adminMigrate(admin, vega.id, { ...base, to_username: "Personne", confirm: "MIGRER" })).body.error.code).toBe("TARGET_NOT_FOUND");
        expect((await adminMigrate(admin, vega.id, { ...base, to_username: "Vega", confirm: "MIGRER" })).body.error.code).toBe("SAME_ACCOUNT");
        expect((await adminMigrate(admin, admin.id, { ...base, close_source: true, confirm: "MIGRER" })).body.error.code).toBe("OWN_ACCOUNT");
        expect(await count("SELECT COUNT(*) AS n FROM backups")).toBe(0);
        expect(await count("SELECT COUNT(*) AS n FROM player_playtime WHERE player_id = ?1", vega.id)).toBe(1);
        const log = await api("GET", `${V}/admin/log?kind=refused`, { token: admin.token });
        expect(log.body.data.entries.map((e: any) => e.reason)).toContain("CONFIRMATION_REQUIRED");
    });

    it("« merge »: a full backup first, then records, achievements, play time, levels and projects, the progression; the source stays open", async () => {
        const admin = await registerPlayer("NovaRider");
        await makeAdmin(admin);
        const vega = await registerPlayer("Vega");
        const nova = await registerPlayer("VegaNew");
        const a = await verifiedRun(vega, 61000);
        await verifiedRun(nova, 65000);
        await declare(vega, ["voyage_5"]);
        await declare(nova, ["voyage_first"]);
        await playtime(vega, [["test_01", 100]]);
        await playtime(nova, [["test_01", 30]]);
        await putCopy(vega, 5);
        await env.DB.batch([
            env.DB.prepare(
                "INSERT INTO community_levels (id, author_id, title, title_norm, planet, status, created_at, updated_at) VALUES ('lvl-1', ?1, 'Mon niveau', 'mon niveau', 'p', 'published', '2026-11-01', '2026-11-01')",
            ).bind(vega.id),
            env.DB.prepare(
                "INSERT INTO projects (id, owner_id, name, member_count, created_at, updated_at) VALUES ('prj-1', ?1, 'Projet', 2, '2026-11-01', '2026-11-01')",
            ).bind(vega.id),
            env.DB.prepare("INSERT INTO project_members (project_id, player_id, role, joined_at) VALUES ('prj-1', ?1, 'owner', '2026-11-01')").bind(vega.id),
            // The target was already an editor of it: it becomes its owner, one member less.
            env.DB.prepare("INSERT INTO project_members (project_id, player_id, role, joined_at) VALUES ('prj-1', ?1, 'editor', '2026-11-02')").bind(nova.id),
        ]);
        const r = await adminMigrate(admin, vega.id, {
            to_username: "VegaNew",
            mode: "merge",
            items: ["records", "achievements", "playtime", "levels", "progress"],
            close_source: false,
            confirm: "MIGRER",
        });
        expect(r.status, JSON.stringify(r.body)).toBe(200);
        const backupId = r.body.data.backup.id as string;
        expect(r.body.data.migration).toMatchObject({ mode: "merge", close_source: false, backup_id: backupId, source: { closed: false } });
        // The backup was made, complete, BEFORE the migration: it still has the source's data.
        const b = await env.DB.prepare("SELECT kind, status, created_by, finished_at FROM backups WHERE id = ?1").bind(backupId).first<Record<string, string>>();
        expect(b).toMatchObject({ kind: "manual", status: "ready", created_by: admin.id });
        const m = await env.DB.prepare("SELECT created_at, backup_id, by_player FROM account_migrations").first<Record<string, string>>();
        expect(m).toMatchObject({ backup_id: backupId, by_player: admin.id });
        expect(String(b!.finished_at) <= String(m!.created_at)).toBe(true);
        expect((await backupRows(backupId, "records")).find((x) => x.id === a.recordId)?.player_id).toBe(vega.id);
        expect((await backupRows(backupId, "player_playtime")).filter((x) => x.player_id === vega.id)).toHaveLength(1);
        // Merged.
        expect(await bestOf(nova, LEVEL_HASH, "ANY_PERCENT")).toEqual({ record_id: a.recordId, time_ms: 61000 });
        expect((await env.DB.prepare("SELECT seconds FROM player_playtime WHERE player_id = ?1").bind(nova.id).first<{ seconds: number }>())?.seconds).toBe(130);
        expect((await env.DB.prepare("SELECT count FROM player_achievements WHERE player_id = ?1").bind(nova.id).first<{ count: number }>())?.count).toBe(2);
        // Levels and projects: their creator is the target now.
        expect((await env.DB.prepare("SELECT author_id FROM community_levels WHERE id = 'lvl-1'").first<{ author_id: string }>())?.author_id).toBe(nova.id);
        expect((await env.DB.prepare("SELECT owner_id, member_count FROM projects WHERE id = 'prj-1'").first())).toEqual({ owner_id: nova.id, member_count: 1 });
        expect((await env.DB.prepare("SELECT player_id, role FROM project_members WHERE project_id = 'prj-1'").all()).results).toEqual([
            { player_id: nova.id, role: "owner" },
        ]);
        // The progression to restore.
        expect((await api("GET", `${V}/players/me/progress-restore`, { token: nova.token })).body.data.restore).toMatchObject({
            from_username: "Vega",
            copy: { summary: { levels: 5 } },
        });
        // The source stays open (close_source: false); both were told.
        expect((await login(vega)).status).toBe(200);
        expect(await securityReasons(vega.id)).toContain("migration_out");
        expect(await securityReasons(nova.id)).toContain("migration_in");
        // Journal: the backup, then the migration.
        const log = await api("GET", `${V}/admin/log?kind=account`, { token: admin.token });
        const entry = log.body.data.entries.find((e: any) => e.action === "admin_account_migrated");
        expect(entry).toMatchObject({
            actor: { id: admin.id },
            target: { id: vega.id, username: "Vega" },
            backup_id: backupId,
            migration: {
                from: { id: vega.id, username: "Vega" },
                to: { id: nova.id, username: "VegaNew" },
                mode: "merge",
                items: ["records", "achievements", "playtime", "levels", "progress"],
                close_source: false,
                restore: true,
                backup_id: backupId,
                by_admin: true,
            },
        });
        const backups = await api("GET", `${V}/admin/log?kind=backup`, { token: admin.token });
        expect(backups.body.data.entries.map((e: any) => e.action)).toContain("backup_created");
    });

    it("« replace »: the target's rows of the chosen parts are deleted first (their files kept in the backup), then the source's moved; the source closed", async () => {
        const admin = await registerPlayer("NovaRider");
        await makeAdmin(admin);
        const vega = await registerPlayer("Vega");
        const nova = await registerPlayer("VegaNew");
        const a = await verifiedRun(vega, 66000);
        const mine = await verifiedRun(nova, 61000); // better, but replaced
        const replayKey = (await env.DB.prepare("SELECT storage_key FROM replay_metadata WHERE record_id = ?1").bind(mine.recordId).first<{ storage_key: string }>())!
            .storage_key;
        expect(await env.STORAGE.get(replayKey)).not.toBeNull();
        await declare(vega, ["voyage_5"]);
        await wear(vega, "title_explorateur");
        await declare(nova, ["voyage_sylvanox"]);
        await wear(nova, "title_gardien_sylvanox");
        await playtime(vega, [["test_01", 100]]);
        await playtime(nova, [["test_01", 30], ["test_02", 20]]);
        const r = await adminMigrate(admin, vega.id, {
            to_username: "VegaNew",
            mode: "replace",
            items: ["records", "achievements", "rewards", "playtime"],
            close_source: true,
            confirm: "MIGRER",
        });
        expect(r.status, JSON.stringify(r.body)).toBe(200);
        expect(r.body.data.migration.files).toEqual({ removed: 1, kept: 0 });
        // Only the source's records now, even where the target was faster.
        expect(await bestOf(nova, LEVEL_HASH, "ANY_PERCENT")).toEqual({ record_id: a.recordId, time_ms: 66000 });
        expect(await count("SELECT COUNT(*) AS n FROM records WHERE id = ?1", mine.recordId)).toBe(0);
        // The replaced replay: kept in the backup made just before, gone from its place.
        expect(await env.STORAGE.get(replayKey)).toBeNull();
        expect(await env.STORAGE.get(`backups/${r.body.data.backup.id}/objects/${replayKey}`)).not.toBeNull();
        // Achievements, look and play time are the source's.
        expect(JSON.parse((await env.DB.prepare("SELECT ids FROM player_achievements WHERE player_id = ?1").bind(nova.id).first<{ ids: string }>())!.ids)).toEqual([
            "voyage_5",
        ]);
        expect((await env.DB.prepare("SELECT title FROM player_cosmetics WHERE player_id = ?1").bind(nova.id).first<{ title: string }>())?.title).toBe("title_explorateur");
        const pt = await env.DB.prepare("SELECT level_key, seconds FROM player_playtime WHERE player_id = ?1").bind(nova.id).all();
        expect(pt.results).toEqual([{ level_key: "test_01", seconds: 100 }]);
        // The source is closed.
        expect((await login(vega)).body.error.code).toBe("ACCOUNT_CLOSED");
        const list = await api("GET", `${V}/admin/players?q=Vega`, { token: admin.token });
        expect(list.body.data.players.find((p: any) => p.id === vega.id)).toMatchObject({ account_status: "suspended" });
        expect(list.body.data.players.find((p: any) => p.id === vega.id).closed_at).toBeTruthy();
        // A closed account cannot be migrated again.
        const again = await adminMigrate(admin, vega.id, { to_username: "VegaNew", mode: "merge", items: ["playtime"], confirm: "MIGRER" });
        expect(again.body.error.code).toBe("SOURCE_INACTIVE");
    });
});
