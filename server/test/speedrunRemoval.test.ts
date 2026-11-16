// SPEEDRUN removed (2.40.1): the cleaning migration hands a replay owned by a SPEEDRUN record to
// another record of the same attempt before deleting the SPEEDRUN records, so that the ANY% record
// keeps its replay; the SPEEDRUN bests and notifications go.

import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { api, registerPlayer, seedAll, submission } from "./helpers";

beforeEach(seedAll);

/** Runs the migration again, now that the database holds rows written before 2.40.1. */
async function runMigration(): Promise<void> {
    const m = env.TEST_MIGRATIONS.find((x) => x.name === "0074_remove_speedrun.sql");
    expect(m).toBeDefined();
    for (const q of m!.queries) await env.DB.prepare(q).run();
}

describe("SPEEDRUN removal", () => {
    it("keeps the replay of the attempt for ANY% and deletes the SPEEDRUN records, bests and notifications", async () => {
        const p = await registerPlayer("Nova");
        const sub = submission();
        const res = await api("POST", "/api/v1/records/submit", { token: p.token, body: sub });
        expect(res.status).toBe(201);
        const anyId = res.body.data.record.id as string;
        // A SPEEDRUN twin of the same attempt, written before 2.40.1, owning the replay.
        await env.DB.prepare(
            `INSERT INTO records (id, player_id, level_id, level_version, level_hash, mode, category, time_ms, deaths, damage_taken, collectibles,
               total_collectibles, kills, score, game_version, attempt_id, verification_status, created_at)
             SELECT 'speed-1', player_id, level_id, level_version, level_hash, mode, 'SPEEDRUN', time_ms, deaths, damage_taken, collectibles,
               total_collectibles, kills, score, game_version, attempt_id, 'VERIFIED', created_at FROM records WHERE id = ?1`,
        )
            .bind(anyId)
            .run();
        await env.DB.prepare(
            `INSERT INTO replay_metadata (id, record_id, player_id, attempt_id, storage_key, file_size, sha256, format_version, frame_count,
               duration_ms, verification_status, created_at)
             VALUES ('rep-1', 'speed-1', ?1, ?2, 'replays/x.cprz', 10, 'h', 3, 100, 1000, 'VERIFIED', '2026-11-01T00:00:00.000Z')`,
        )
            .bind(p.id, sub.attempt_id)
            .run();
        await env.DB.prepare("UPDATE records SET replay_id = 'rep-1' WHERE player_id = ?1 AND attempt_id = ?2").bind(p.id, sub.attempt_id).run();
        await env.DB.prepare(
            `INSERT INTO player_bests (player_id, level_id, level_hash, category, record_id, time_ms, achieved_at)
             SELECT player_id, level_id, level_hash, 'SPEEDRUN', id, time_ms, created_at FROM records WHERE id = 'speed-1'`,
        ).run();
        await env.DB.prepare(
            `INSERT INTO notifications (id, player_id, kind, data, created_at) VALUES ('n-1', ?1, 'record_beaten', '{"category":"SPEEDRUN"}', '2026-11-01T00:00:00.000Z'),
             ('n-2', ?1, 'record_beaten', '{"category":"ANY_PERCENT"}', '2026-11-01T00:00:00.000Z')`,
        )
            .bind(p.id)
            .run();

        await runMigration();

        const left = await env.DB.prepare("SELECT category FROM records WHERE player_id = ?1").bind(p.id).all<{ category: string }>();
        expect(left.results.map((r) => r.category)).toEqual(["ANY_PERCENT"]);
        const replay = await env.DB.prepare("SELECT record_id FROM replay_metadata WHERE id = 'rep-1'").first<{ record_id: string }>();
        expect(replay?.record_id).toBe(anyId);
        const any = await env.DB.prepare("SELECT replay_id FROM records WHERE id = ?1").bind(anyId).first<{ replay_id: string }>();
        expect(any?.replay_id).toBe("rep-1");
        expect((await env.DB.prepare("SELECT COUNT(*) AS n FROM player_bests WHERE category = 'SPEEDRUN'").first<{ n: number }>())?.n).toBe(0);
        const notes = await env.DB.prepare("SELECT id FROM notifications WHERE player_id = ?1").bind(p.id).all<{ id: string }>();
        expect(notes.results.map((n) => n.id)).toEqual(["n-2"]);
    });
});
