// Clan raids « Asterion », phase 6 (2.72, docs/RAIDS.md §16): LE CŒUR D'ASTERION, the final fight.
//
// * It OPENS once the seven gigaboss of the titan fell (their objectives accepted for the event): the
//   heart's catalog (raidTitan.json `heart_catalog`, written by tools/levelgen/raid_coeur.py) is then a
//   branch players join like the others (raidMap.join), every group of every clan at the same time; the
//   registered players are told once (raid_heart_events.notified_open).
// * Its health is SHARED. Each instance runs its own simulation of the heart; its games report the part of
//   the heart's health that instance took (ten-thousandths, `{kind: "heart", damage}`), accepted by the
//   consensus of its players like every report (raidZones.report). The highest value accepted per instance
//   is kept (raid_heart_damage: one row per instance, MAX: a late, repeated or concurrent report never
//   lowers it nor counts twice). The shared health is 10 000 minus the sum (capped); its phase follows it.
// * Every heart relay (coop.ts, CoopRoom) gives its games, with the branch's state at a step (RAID_STATE),
//   what the OTHER instances took (`others`), the shared phase and the relics of the seven branches
//   validated (each one weakens the heart for everyone): every game of the instance applies it at that
//   same step (lockstep stays exact). Nothing comes from a game but its own validated part.
// * It FALLS for the world when the sum reaches 10 000: its objective (coeur_asterion) is written once in
//   raid_branch_objectives (INSERT OR IGNORE: two instances crossing the line together count it once), the
//   relays give it to every heart instance at a step (their victory film), the fall is journaled, the
//   registered players are told « Asterion est tombé » once, the rewards are granted (raidHeartRewards.ts,
//   each once). The phase-5 outcome « Asterion est tombé » (raidWorld.ts) needs it with the seven gigaboss.

import { ask } from "./coop";
import { log, nowIso } from "./http";
import { CATALOGS, HEART, catalogById, isDone, linksTo, partOf, type Catalog } from "./raidMap";
import { HEART_CLAN_TITLE, HEART_PARTICIPATION_TICKS, HEART_REWARDS, HEART_TOP_PLAYERS } from "./raidHeartRewards";
import { scoreOf } from "./raidRewards";
import { insertNotifications, journal, notifData } from "./raidWorld";

export const HEART_UNITS = 10000;
/** The heart's own objective (role « gigaboss »): its fall for the world. */
export const HEART_OBJECTIVE = HEART?.objectives.find((o) => o.role === "gigaboss")?.id ?? "coeur_asterion";
/** The relics of the seven branches (their secret zones' optional objectives): each one validated weakens the heart. */
export const RELICS: { branch: string; objective: string }[] = CATALOGS.flatMap((c) =>
    c.objectives.filter((o) => o.optional && c.zones.find((z) => z.id === o.zone)?.secret).map((o) => ({ branch: c.id, objective: o.id })),
);

export const isHeart = (branch: string) => !!HEART && branch === HEART.id;

/** The seven gigaboss fallen (validated): the heart is open. */
export function heartOpenIn(done: Map<string, Set<string>>): boolean {
    return gigabossDownIn(done) === CATALOGS.length && CATALOGS.length > 0;
}

export function gigabossDownIn(done: Map<string, Set<string>>): number {
    let n = 0;
    for (const c of CATALOGS) {
        const g = c.objectives.find((o) => o.role === "gigaboss");
        if (g && done.get(c.id)?.has(g.id)) n++;
    }
    return n;
}

export function heartFallenIn(done: Map<string, Set<string>>): boolean {
    return !!HEART && !!done.get(HEART.id)?.has(HEART_OBJECTIVE);
}

export function relicsIn(done: Map<string, Set<string>>): number {
    return RELICS.filter((r) => done.get(r.branch)?.has(r.objective)).length;
}

/** The shared phase (1 to 4) of the damage taken (its enemy's thresholds: 75 %, 50 %, 25 % of its health). */
export function phaseOf(taken: number): number {
    return taken >= 7500 ? 4 : taken >= 5000 ? 3 : taken >= 2500 ? 2 : 1;
}

/** RAID_STATE's u32 (raid::HeartShared::pack): others | phase << 16 | relics << 24 (phase 5: fallen). */
export function packHeart(others: number, phase: number, relics: number): number {
    return ((Math.max(0, Math.min(HEART_UNITS, others)) & 0xffff) | ((phase & 0xff) << 16) | ((Math.min(7, relics) & 0xff) << 24)) >>> 0;
}

export interface HeartDamage {
    total: number;
    rows: { instance_id: string; damage: number }[];
}

export async function heartDamageOf(db: D1Database, eventId: string): Promise<HeartDamage> {
    try {
        const rows = (await db.prepare("SELECT instance_id, damage FROM raid_heart_damage WHERE event_id = ?1").bind(eventId).all<{ instance_id: string; damage: number }>())
            .results;
        return { total: Math.min(HEART_UNITS, rows.reduce((n, r) => n + r.damage, 0)), rows };
    } catch {
        return { total: 0, rows: [] }; // before migration 0148
    }
}

async function doneIn(db: D1Database, eventId: string): Promise<Map<string, Set<string>>> {
    const rows = await db.prepare("SELECT branch, objective_id FROM raid_branch_objectives WHERE event_id = ?1").bind(eventId).all<{ branch: string; objective_id: string }>();
    const done = new Map<string, Set<string>>();
    for (const r of rows.results) {
        if (!done.has(r.branch)) done.set(r.branch, new Set());
        done.get(r.branch)!.add(r.objective_id);
    }
    return done;
}

/** What a heart relay gives its games (coop.ts): the others' part, the shared phase, the relics. */
export async function heartPackFor(db: D1Database, eventId: string, instanceId: string): Promise<number> {
    const [dmg, done] = await Promise.all([heartDamageOf(db, eventId), doneIn(db, eventId)]);
    const raw = dmg.rows.reduce((n, r) => n + r.damage, 0);
    const own = dmg.rows.find((r) => r.instance_id === instanceId)?.damage ?? 0;
    const fallen = heartFallenIn(done);
    return packHeart(Math.min(HEART_UNITS, raw - own), fallen ? 5 : phaseOf(dmg.total), relicsIn(done));
}

/** An accepted report of an instance's part (raidZones.report): kept if higher (one row per instance). */
export async function storeHeartDamage(db: D1Database, eventId: string, instanceId: string, damage: number, tick: number, now: string): Promise<void> {
    await db
        .prepare(
            `INSERT INTO raid_heart_damage (event_id, instance_id, damage, tick, accepted_at) VALUES (?1, ?2, ?3, ?4, ?5)
             ON CONFLICT (event_id, instance_id) DO UPDATE SET damage = MAX(damage, excluded.damage), tick = MAX(tick, excluded.tick),
                accepted_at = CASE WHEN excluded.damage > damage THEN excluded.accepted_at ELSE accepted_at END`,
        )
        .bind(eventId, instanceId, Math.max(0, Math.min(HEART_UNITS, damage)), tick, now)
        .run();
}

/** Every relay of the event's heart instances reads the shared state now (else their alarms within seconds). */
export async function wakeHeartRelays(env: { DB: D1Database } & Record<string, unknown>, eventId: string): Promise<void> {
    if (!HEART) return;
    const rooms = await env.DB.prepare("SELECT id FROM raid_instances WHERE event_id = ?1 AND branch = ?2 AND players > 0").bind(eventId, HEART.id).all<{ id: string }>();
    for (const r of rooms.results) {
        try {
            await ask(env as never, r.id, "/raid-state");
        } catch {
            /* the room's alarm reads it within seconds */
        }
    }
}

/** An event as the routes read it (its row: the fields a notification names, when known). */
type EventLike = { id: string; raid?: string; format?: string; kind?: string; starts_at?: string };
const notifEvent = (e: EventLike) => ({ id: e.id, raid: e.raid ?? "asterion", format: e.format ?? "standard", kind: e.kind ?? "clan", starts_at: e.starts_at ?? "" });

async function registered(db: D1Database, eventId: string): Promise<string[]> {
    return (await db.prepare("SELECT player_id FROM raid_registrations WHERE event_id = ?1").bind(eventId).all<{ player_id: string }>()).results.map((r) => r.player_id);
}

/**
 * The seven gigaboss fallen: the heart opens (once per event: journaled, the registered players told).
 * Called after each accepted gigaboss objective. True when it opened now.
 */
export async function checkHeartOpen(db: D1Database, e: EventLike, now = nowIso()): Promise<boolean> {
    if (!HEART) return false;
    try {
        if (!heartOpenIn(await doneIn(db, e.id))) return false;
        const claim = await db
            .prepare(
                `INSERT INTO raid_heart_events (event_id, opened_at, notified_open) VALUES (?1, ?2, ?2)
                 ON CONFLICT (event_id) DO UPDATE SET opened_at = COALESCE(opened_at, excluded.opened_at), notified_open = COALESCE(notified_open, excluded.notified_open)
                 WHERE raid_heart_events.notified_open IS NULL`,
            )
            .bind(e.id, now)
            .run();
        if (!claim.meta.changes) return false;
        await journal(db, e.id, "info", "heart_open", "Les sept gigaboss sont tombés : le Cœur d'Asterion s'ouvre, tous les groupes peuvent le combattre", {}, null, "heart_open");
        await insertNotifications(db, await registered(db, e.id), notifData(notifEvent(e) as never, "heart_open"), `raid:heart_open:${e.id}`);
        return true;
    } catch (err) {
        log("error", "raid_heart_open_failed", { error: err instanceof Error ? err.message : String(err) });
        return false;
    }
}

/**
 * The shared health gone (the validated parts summed reach 10 000): the heart falls for the world, its
 * objective written once (the instance whose report crossed the line keeps the credit), journaled, the
 * rewards granted, the registered players told (each once). True when it is fallen (now or before).
 */
export async function checkHeartFall(db: D1Database, e: EventLike, instanceId: string | null, now = nowIso()): Promise<boolean> {
    if (!HEART) return false;
    const dmg = await heartDamageOf(db, e.id);
    if (dmg.total < HEART_UNITS) return false;
    const won = await db
        .prepare("INSERT OR IGNORE INTO raid_branch_objectives (event_id, branch, objective_id, instance_id, accepted_at) VALUES (?1, ?2, ?3, ?4, ?5)")
        .bind(e.id, HEART.id, HEART_OBJECTIVE, instanceId, now)
        .run();
    try {
        await db
            .prepare(
                `INSERT INTO raid_heart_events (event_id, fallen_at, fallen_instance) VALUES (?1, ?2, ?3)
                 ON CONFLICT (event_id) DO UPDATE SET fallen_at = COALESCE(fallen_at, excluded.fallen_at), fallen_instance = COALESCE(fallen_instance, excluded.fallen_instance)`,
            )
            .bind(e.id, now, instanceId)
            .run();
        if (won.meta.changes)
            await journal(
                db,
                e.id,
                "info",
                "heart_fallen",
                `Asterion est tombé : le Cœur a cédé sous les coups de ${dmg.rows.filter((r) => r.damage > 0).length} groupe(s), sa vie partagée validée`,
                { groups: dmg.rows.filter((r) => r.damage > 0).length },
                null,
                "heart_fallen",
            );
        await grantHeart(db, e.id, now);
        const told = await db.prepare("UPDATE raid_heart_events SET notified_fall = ?2 WHERE event_id = ?1 AND notified_fall IS NULL").bind(e.id, now).run();
        if (told.meta.changes) await insertNotifications(db, await registered(db, e.id), notifData(notifEvent(e) as never, "heart_fallen"), `raid:heart_fallen:${e.id}`);
    } catch (err) {
        log("error", "raid_heart_fall_failed", { error: err instanceof Error ? err.message : String(err) });
    }
    return true;
}

async function grantMany(db: D1Database, eventId: string, reward: keyof typeof HEART_REWARDS, players: string[], now: string): Promise<number> {
    let n = 0;
    const why = HEART_REWARDS[reward].why;
    for (let at = 0; at < players.length; at += 100) {
        const res = await db.batch(
            players
                .slice(at, at + 100)
                .map((p) =>
                    db
                        .prepare("INSERT OR IGNORE INTO raid_reward_grants (player_id, reward, event_id, instance_id, reason, granted_at) VALUES (?1, ?2, ?3, NULL, ?4, ?5)")
                        .bind(p, reward, eventId, why, now),
                ),
        );
        n += res.reduce((k, r) => k + (r.meta.changes ?? 0), 0);
    }
    return n;
}

/**
 * The rewards of the heart's fall that do not wait for the end (each once): « Supernova d'Asterion » to
 * each player with a minute validated in a heart instance; the collective title « Briseurs d'étoile » to
 * each clan with a player present at the fall (raid_clan_titles), and its item to the clan's players with
 * validated contributions. Called at the fall and again with each later table of a heart instance.
 */
export async function grantHeart(db: D1Database, eventId: string, now = nowIso()): Promise<number> {
    if (!HEART) return 0;
    const fallen = await db
        .prepare("SELECT 1 AS x FROM raid_branch_objectives WHERE event_id = ?1 AND branch = ?2 AND objective_id = ?3")
        .bind(eventId, HEART.id, HEART_OBJECTIVE)
        .first();
    if (!fallen) return 0;
    let n = 0;
    const fighters = (
        await db
            .prepare(
                `SELECT c.player_id FROM raid_contributions c JOIN raid_instances i ON i.id = c.instance_id
                 WHERE c.event_id = ?1 AND i.branch = ?2 AND c.player_id IS NOT NULL GROUP BY c.player_id HAVING SUM(c.ticks) >= ?3`,
            )
            .bind(eventId, HEART.id, HEART_PARTICIPATION_TICKS)
            .all<{ player_id: string }>()
    ).results.map((r) => r.player_id);
    n += await grantMany(db, eventId, "coeur_supernova", fighters, now);
    const clans = (
        await db
            .prepare(
                `SELECT r.clan_id, COUNT(DISTINCT c.player_id) AS n FROM raid_contributions c JOIN raid_instances i ON i.id = c.instance_id
                 JOIN raid_gigaboss_fights f ON f.instance_id = c.instance_id AND f.phase = 4
                 JOIN raid_registrations r ON r.event_id = c.event_id AND r.player_id = c.player_id
                 WHERE c.event_id = ?1 AND i.branch = ?2 AND c.gigaboss = 1 AND c.player_id IS NOT NULL
                 GROUP BY r.clan_id ORDER BY n DESC, r.clan_id`,
            )
            .bind(eventId, HEART.id)
            .all<{ clan_id: string; n: number }>()
    ).results;
    if (clans.length) {
        await db.batch(
            clans.map((c, i) =>
                db
                    .prepare("INSERT OR IGNORE INTO raid_clan_titles (clan_id, event_id, title, rank, granted_at) SELECT ?1, ?2, ?3, ?4, ?5 WHERE EXISTS (SELECT 1 FROM clans WHERE id = ?1)")
                    .bind(c.clan_id, eventId, HEART_CLAN_TITLE, i + 1, now),
            ),
        );
        const marks = clans.map((_, i) => `?${i + 2}`).join(",");
        const members = (
            await db
                .prepare(
                    `SELECT DISTINCT c.player_id FROM raid_contributions c JOIN raid_registrations r ON r.event_id = c.event_id AND r.player_id = c.player_id
                     WHERE c.event_id = ?1 AND c.player_id IS NOT NULL AND r.clan_id IN (${marks})`,
                )
                .bind(eventId, ...clans.map((c) => c.clan_id))
                .all<{ player_id: string }>()
        ).results.map((r) => r.player_id);
        n += await grantMany(db, eventId, "coeur_clan", members, now);
    }
    await db.prepare("UPDATE raid_heart_events SET rewards_at = COALESCE(rewards_at, ?2) WHERE event_id = ?1").bind(eventId, now).run();
    return n;
}

/**
 * At the end of an event whose heart fell (the results read after it, the cron): the profile decor of
 * the 10 best mixed scores of the event among the heart's fighters (never only the damage), once.
 */
export async function grantHeartTop(db: D1Database, eventId: string, now = nowIso()): Promise<number> {
    if (!HEART) return 0;
    try {
        const fallen = await db
            .prepare("SELECT 1 AS x FROM raid_branch_objectives WHERE event_id = ?1 AND branch = ?2 AND objective_id = ?3")
            .bind(eventId, HEART.id, HEART_OBJECTIVE)
            .first();
        if (!fallen) return 0;
        const done = await db.prepare("SELECT 1 AS x FROM raid_reward_grants WHERE event_id = ?1 AND reward = 'coeur_decor' LIMIT 1").bind(eventId).first();
        if (done) return 0; // granted once, at the end: the top never grows afterwards
        const rows = (
            await db
                .prepare(
                    `SELECT player_id, SUM(damage) AS damage, SUM(boss_damage) AS boss_damage, SUM(avoided) AS avoided, SUM(blocks) AS blocks,
                            SUM(rescues) AS rescues, SUM(vents) AS vents, SUM(weak_points) AS weak_points, SUM(objectives) AS objectives,
                            SUM(ticks) AS ticks, MAX(gigaboss) AS gigaboss
                     FROM raid_contributions WHERE event_id = ?1 AND player_id IS NOT NULL
                       AND player_id IN (SELECT c.player_id FROM raid_contributions c JOIN raid_instances i ON i.id = c.instance_id WHERE c.event_id = ?1 AND i.branch = ?2)
                     GROUP BY player_id`,
                )
                .bind(eventId, HEART.id)
                .all<Parameters<typeof scoreOf>[0] & { player_id: string }>()
        ).results;
        const top = rows
            .map((t) => ({ id: t.player_id, score: scoreOf(t).total }))
            .filter((t) => t.score > 0)
            .sort((a, b) => b.score - a.score || (a.id < b.id ? -1 : 1))
            .slice(0, HEART_TOP_PLAYERS);
        return await grantMany(
            db,
            eventId,
            "coeur_decor",
            top.map((t) => t.id),
            now,
        );
    } catch (err) {
        log("error", "raid_heart_top_failed", { error: err instanceof Error ? err.message : String(err) });
        return 0;
    }
}

/** The ended events whose heart fell and whose decor was not granted yet (the cron of raids.ts). */
export async function grantEndedHeartTops(db: D1Database, now: string): Promise<number> {
    if (!HEART) return 0;
    let n = 0;
    try {
        const ended = await db
            .prepare(
                `SELECT e.id FROM raid_events e JOIN raid_branch_objectives o ON o.event_id = e.id AND o.branch = ?3 AND o.objective_id = ?4
                 WHERE e.ends_at <= ?1 AND e.ends_at > ?2 AND NOT EXISTS (SELECT 1 FROM raid_reward_grants g WHERE g.event_id = e.id AND g.reward = 'coeur_decor')`,
            )
            .bind(now, new Date(Date.parse(now) - 7 * 86400000).toISOString(), HEART.id, HEART_OBJECTIVE)
            .all<{ id: string }>();
        for (const e of ended.results) n += await grantHeartTop(db, e.id, now);
    } catch {
        /* before migration 0148 */
    }
    return n;
}

/** The part of the titan whose gigaboss's fall a « modifier » link of the heart comes from. */
function partOfLink(l: { from: { branch: string }[] }): string {
    return l.from[0] ? partOf(l.from[0].branch) : "";
}

/**
 * The heart on the titan's map (raidMap.titanView) and the sheet of an event: sealed until the seven
 * gigaboss fell, then its shared health (validated only), its phase, the groups fighting it, the effects
 * of the parts fallen (its « modifier » links), the relics; fallen once its objective is written.
 */
export async function heartView(
    db: D1Database,
    eventId: string,
    done: Map<string, Set<string>>,
    states: Map<string, number>,
    instances: { branch: string; players: number; clan_id: string; tag: string; group_no: number }[],
    myClan: string | null,
) {
    const heart = HEART;
    if (!heart) return null;
    const dmg = await heartDamageOf(db, eventId);
    const open = heartOpenIn(done);
    const fallen = heartFallenIn(done);
    const here = instances.filter((i) => i.branch === heart.id && i.players > 0);
    const mask = states.get(heart.id) ?? 0;
    let at: string | null = null;
    try {
        at = (await db.prepare("SELECT fallen_at FROM raid_heart_events WHERE event_id = ?1").bind(eventId).first<{ fallen_at: string | null }>())?.fallen_at ?? null;
    } catch {
        /* before migration 0148 */
    }
    return {
        part: partOf(heart.id),
        id: heart.id,
        name: heart.name,
        boss: heart.gigaboss,
        state: fallen ? "tombe" : !open ? "scelle" : here.length > 0 ? "combat" : "ouvert",
        open,
        fallen,
        fallen_at: at,
        health: fallen ? 0 : HEART_UNITS - dmg.total,
        damage: dmg.total,
        phase: fallen ? 4 : phaseOf(dmg.total),
        groups: here.length,
        players: here.reduce((n, i) => n + i.players, 0),
        instances: dmg.rows.filter((r) => r.damage > 0).length,
        my_groups: myClan ? here.filter((i) => i.clan_id === myClan).map((i) => `[${i.tag}]·${i.group_no}`) : [],
        gigaboss_defeated: gigabossDownIn(done),
        gigaboss: CATALOGS.length,
        relics: relicsIn(done),
        relics_total: RELICS.length,
        modifiers: linksTo(heart).map((l) => ({ id: l.id, part: partOfLink(l), label: l.label, on: isDone(mask, l.bit) })),
    };
}

/** The body state of the heart for the titan's figure (raidMap.titanView). */
export function heartBodyState(v: NonNullable<Awaited<ReturnType<typeof heartView>>>): string {
    if (v.fallen) return "detruit";
    if (!v.open) return "intact"; // sealed: it beats in its chest as before
    if (v.groups > 0) return v.phase >= 3 ? "affaibli" : "attaque";
    return v.phase >= 3 ? "affaibli" : "intact";
}

export const heartCatalog = (): Catalog | undefined => HEART;
export const heartBranchName = (): string => (HEART ? (catalogById(HEART.id)?.name ?? "") : "");
