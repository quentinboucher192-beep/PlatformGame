// §41 (titan 16, docs/RAIDS.md): a clan's seven groups formed for a test, as the server forms them at the first join
// (raidMap.ensureGroups): always seven groups, group k in an instance of branch k, 14 registered at least.

import { env } from "cloudflare:test";
import { CATALOGS, partOf } from "../src/raidMap";

/** The branches of the titan in the order of its body (group k plays branch k: raidMap CATALOGS). */
export const RAID_PARTS = CATALOGS.map((c) => partOf(c.id));

let fillerCounter = 0;

/**
 * Registers `n` placeholder players for a clan (after everyone already registered: they never play), straight in the
 * database (a clan has 50 members at most; the waiting list starts after 56).
 */
export async function fillRoster(eventId: string, clanId: string, n: number): Promise<string[]> {
    const now = Date.now();
    const out: string[] = [];
    for (let k = 0; k < n; ++k) {
        const id = crypto.randomUUID();
        const name = `zfill${++fillerCounter}x${Math.floor(Math.random() * 1e6)}`;
        const at = new Date(now + 1000 + k).toISOString();
        await env.DB.prepare(
            "INSERT INTO players (id, username, username_normalized, tos_version, tos_accepted_at, created_at, last_seen_at) VALUES (?1, ?2, ?3, '2026-11', ?4, ?4, ?4)",
        )
            .bind(id, name, name, at)
            .run();
        await env.DB.prepare("INSERT INTO raid_registrations (event_id, player_id, clan_id, created_at) VALUES (?1, ?2, ?3, ?4)").bind(eventId, id, clanId, at).run();
        out.push(id);
    }
    return out;
}

/**
 * The clan's seven groups formed for a test. `teams`: a branch (its part) → the clan's real players of that group
 * (registered already). The roster is filled up to `min` registered (14: the fewest a clan starts with) with
 * placeholder players, and every registered player of the clan not in `teams` is placed in a group no team uses (else
 * spread over all of them). Returns each branch's instance id (group k: RAID_PARTS[k − 1]).
 */
export async function raidGroups(eventId: string, clanId: string, teams: Record<string, { id: string }[]> = {}, min = 14): Promise<Record<string, string>> {
    const count = (await env.DB.prepare("SELECT COUNT(*) AS n FROM raid_registrations WHERE event_id = ?1 AND clan_id = ?2").bind(eventId, clanId).first<{ n: number }>())!.n;
    if (count < min) await fillRoster(eventId, clanId, min - count);
    const ids: Record<string, string> = {};
    const stamp = new Date().toISOString();
    for (let g = 0; g < CATALOGS.length; ++g) {
        const c = CATALOGS[g]!;
        await env.DB.prepare(
            `INSERT OR IGNORE INTO raid_instances (id, event_id, clan_id, group_no, branch, zone, players, created_at, updated_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, 0, ?7, ?7)`,
        )
            .bind(crypto.randomUUID(), eventId, clanId, g + 1, c.id, c.start_zone, stamp)
            .run();
        ids[RAID_PARTS[g]!] = (await env.DB.prepare("SELECT id FROM raid_instances WHERE event_id = ?1 AND clan_id = ?2 AND group_no = ?3 AND branch = ?4")
            .bind(eventId, clanId, g + 1, c.id)
            .first<{ id: string }>())!.id;
    }
    const place = async (playerId: string, part: string) =>
        env.DB.prepare(
            `INSERT INTO raid_placements (event_id, player_id, clan_id, branch, instance_id, placed_by, reason, created_at, updated_at)
             VALUES (?1, ?2, ?3, ?4, ?5, NULL, 'choix', ?6, ?6)
             ON CONFLICT (event_id, player_id) DO UPDATE SET branch = excluded.branch, instance_id = excluded.instance_id`,
        )
            .bind(eventId, playerId, clanId, CATALOGS[RAID_PARTS.indexOf(part)]!.id, ids[part]!, stamp)
            .run();
    const registered = new Set(
        (await env.DB.prepare("SELECT player_id FROM raid_registrations WHERE event_id = ?1 AND clan_id = ?2").bind(eventId, clanId).all<{ player_id: string }>()).results.map(
            (r) => r.player_id,
        ),
    );
    const placed = new Set<string>();
    for (const [part, players] of Object.entries(teams))
        for (const p of players) {
            if (!registered.has(p.id)) continue; // a member who did not register has no place
            await place(p.id, part);
            placed.add(p.id);
        }
    const free = RAID_PARTS.filter((p) => !(p in teams));
    const spread = free.length ? free : RAID_PARTS;
    const rest = (
        await env.DB.prepare("SELECT player_id FROM raid_registrations WHERE event_id = ?1 AND clan_id = ?2 ORDER BY created_at").bind(eventId, clanId).all<{ player_id: string }>()
    ).results
        .map((r) => r.player_id)
        .filter((id) => !placed.has(id))
        .slice(0, 56);
    let k = 0;
    for (const id of rest) await place(id, spread[k++ % spread.length]!);
    return ids;
}
