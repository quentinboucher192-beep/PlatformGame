// Clan raids « Asterion », 2.75 (docs/RAIDS.md §20): the group wiped, its spectators and the zones
// to take again.
//
// * In a raid a player down comes back as many times as a teammate revives them. When EVERY player of
//   an instance is down at once, its games report a « wipe » (raidZones.report, the same consensus as
//   every report): once accepted, the instance is out of that zone for good — no restart, no
//   checkpoint, no seat any more, no report accepted from it (its zone is NOT validated by it) —
//   and the players who held a seat in it are SPECTATORS for the rest of the event (raid_spectators):
//   a join gives them a spectator link (a socket that watches, never a seat), on their instance or on
//   any instance of their branch still playing (GET /raids/instances/{id}/watch lists its players).
// * The zone stays « à reprendre » (to retake) while one of its objectives that is not optional is
//   not done. A player of a group that FINISHED its own zone (every objective of its zone done) takes
//   it again (POST /raids/instances/{id}/retake): the server places them in a fresh instance of that
//   zone (`start_zone`, the validated state of the branch kept), made once per wiped instance and
//   clan; their group follows them one by one (8 at most). Only that new group's reports can
//   validate the zone.

import { requireAuth } from "./auth";
import { ask } from "./coop";
import { Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import { catalogById, enter, isDone, putIn, stateMaskOf, type Catalog } from "./raidMap";
import { raidState } from "./raids";
import { requireEntry } from "./raidWorld";
import { RULES, rateLimit } from "./ratelimit";
import { Validator } from "./validate";

const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
/** Players of an instance at most (a reinforcement group too). */
const INSTANCE_SIZE = 8;

export interface WipeInstanceRow {
    id: string;
    event_id: string;
    clan_id: string;
    group_no: number;
    branch: string;
    zone: number;
    players: number;
    started_at: string | null;
    wiped_zone: number | null;
    wiped_at: string | null;
    start_zone: number | null;
    relieves: string | null;
    seat_zones?: string | null;   // 2.78
    wiped_zones?: string | null;  // 2.78
}

export interface Spectator {
    instance_id: string;
    branch: string;
    zone: number;
}

/** Whether a player is a spectator of an event (their group was wiped): null when they play. */
export async function spectatorOf(db: D1Database, eventId: string, playerId: string): Promise<Spectator | null> {
    return await db
        .prepare("SELECT instance_id, branch, zone FROM raid_spectators WHERE event_id = ?1 AND player_id = ?2")
        .bind(eventId, playerId)
        .first<Spectator>();
}

/** A zone is finished when every objective of it that is not optional is done (none: nothing to finish). */
export function zoneFinished(c: Catalog, zone: number, mask: number): boolean {
    return c.objectives.filter((o) => o.zone === zone && !o.optional).every((o) => isDone(mask, o.index));
}

export interface ZoneToRetake {
    zone: number;
    instance_id: string;
    clan_tag: string;
    group: number;
    since: string;
    relieved_by: string | null;
}

/** The zones of a branch to take again: a group was wiped there and they are not finished since (oldest first). */
export async function zonesToRetake(db: D1Database, eventId: string, c: Catalog, mask: number): Promise<ZoneToRetake[]> {
    const rows = (
        await db
            .prepare(
                `SELECT i.id, i.wiped_zone, i.wiped_zones, i.wiped_at, i.group_no, c.tag,
                    (SELECT r.id FROM raid_instances r WHERE r.relieves = i.id AND r.wiped_zone IS NULL ORDER BY r.created_at LIMIT 1) AS relief
                 FROM raid_instances i JOIN clans c ON c.id = i.clan_id
                 WHERE i.event_id = ?1 AND i.branch = ?2 AND i.wiped_zone IS NOT NULL ORDER BY i.wiped_at, i.id LIMIT 200`,
            )
            .bind(eventId, c.id)
            .all<{ id: string; wiped_zone: number; wiped_zones: string | null; wiped_at: string; group_no: number; tag: string; relief: string | null }>()
    ).results;
    const out: ZoneToRetake[] = [];
    for (const r of rows) {
        // 2.78: a group wiped across several zones (its players spread over them): each one to retake.
        const zones = r.wiped_zones ? r.wiped_zones.split(",").map((x) => Number(x)).filter((z) => z > 0) : [r.wiped_zone];
        for (const zone of zones) {
            if (zoneFinished(c, zone, mask) || out.some((z) => z.zone === zone)) continue;
            out.push({ zone, instance_id: r.id, clan_tag: r.tag, group: r.group_no, since: r.wiped_at, relieved_by: r.relief });
        }
    }
    return out;
}

/**
 * An accepted wipe: the instance is out of `zone` for good (once), the players who held a seat in it
 * become spectators (their placements freed: a join never seats them again), its relay closes its seats.
 */
export async function acceptWipe(ctx: Ctx, eventId: string, inst: { id: string; branch: string }, zone: number, now: string, zones: number[] = [zone]): Promise<void> {
    const db = ctx.env.DB;
    await db.batch([
        db
            .prepare("UPDATE raid_instances SET wiped_zone = ?2, wiped_zones = ?4, wiped_at = ?3, updated_at = ?3 WHERE id = ?1 AND wiped_zone IS NULL")
            .bind(inst.id, zone, now, zones.join(",")),
        db
            .prepare(
                `INSERT OR IGNORE INTO raid_spectators (event_id, player_id, instance_id, branch, zone, created_at)
                 SELECT ?1, player_id, ?2, ?3, ?4, ?5 FROM raid_instance_members WHERE instance_id = ?2`,
            )
            .bind(eventId, inst.id, inst.branch, zone, now),
        db.prepare("DELETE FROM raid_placements WHERE instance_id = ?1 AND player_id IN (SELECT player_id FROM raid_spectators WHERE instance_id = ?1)").bind(inst.id),
    ]);
    ctx.exec.waitUntil(ask(ctx.env, inst.id, "/wiped").catch(() => undefined));
}

/** The answer of a spectator's join: their wiped instance, to watch (never a seat). */
export async function spectatorAnswer(ctx: Ctx, e: { id: string }, s: Spectator, me: string): Promise<Response> {
    const inst = await ctx.env.DB.prepare("SELECT * FROM raid_instances WHERE id = ?1").bind(s.instance_id).first<WipeInstanceRow>();
    if (!inst) throw Errors.notFound("INSTANCE_NOT_FOUND", "Ce groupe n'est plus en jeu.");
    return enter(ctx, e as never, inst, me, true);
}

async function instanceOf(db: D1Database, id: string): Promise<WipeInstanceRow> {
    if (!ID_RE.test(id)) throw Errors.notFound("INSTANCE_NOT_FOUND", "Instance introuvable.");
    const inst = await db.prepare("SELECT * FROM raid_instances WHERE id = ?1").bind(id).first<WipeInstanceRow>();
    if (!inst) throw Errors.notFound("INSTANCE_NOT_FOUND", "Instance introuvable.");
    return inst;
}

async function runningEvent(db: D1Database, id: string, now: string) {
    const e = await db.prepare("SELECT * FROM raid_events WHERE id = ?1").bind(id).first<{ id: string; starts_at: string; ends_at: string; published_at: string | null }>();
    if (!e || !e.published_at) throw Errors.notFound("RAID_NOT_FOUND", "Événement introuvable.");
    if (raidState(e as never, now) !== "en_cours") throw Errors.conflict("RAID_NOT_RUNNING", "Le raid n'est pas en cours.");
    return e;
}

/**
 * POST /raids/instances/{instanceId}/retake {zone}: a player of this instance (their group finished
 * its zone) takes a wiped zone of the branch again: placed in the fresh instance of that zone (made
 * once per wiped instance and clan), whose games start in that zone with the branch's state.
 */
export async function retake(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const db = ctx.env.DB;
    await rateLimit(ctx, RULES.raidWrite, "player:" + me);
    const now = nowIso();
    const inst = await instanceOf(db, ctx.params.instanceId ?? "");
    const e = await runningEvent(db, inst.event_id, now);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["zone"]);
    const zone = v.int("zone", { min: 1, max: 32 })!;
    v.done();
    const reg = await db.prepare("SELECT clan_id FROM raid_registrations WHERE event_id = ?1 AND player_id = ?2").bind(e.id, me).first<{ clan_id: string }>();
    if (!reg || reg.clan_id !== inst.clan_id) throw Errors.forbidden("NOT_IN_INSTANCE", "Vous ne jouez pas dans ce groupe.");
    await requireEntry(db, e, me);
    if (await spectatorOf(db, e.id, me)) throw Errors.forbidden("RAID_SPECTATOR", "Votre groupe a été éliminé : vous ne pouvez plus que regarder.");
    const member = await db
        .prepare(
            `SELECT 1 AS x FROM raid_instance_members WHERE instance_id = ?1 AND player_id = ?2 AND left_at IS NULL
             UNION SELECT 1 FROM raid_placements WHERE instance_id = ?1 AND player_id = ?2`,
        )
        .bind(inst.id, me)
        .first();
    if (!member) throw Errors.forbidden("NOT_IN_INSTANCE", "Vous ne jouez pas dans ce groupe.");
    if (inst.wiped_zone !== null) throw Errors.conflict("INSTANCE_WIPED", "Ce groupe a été éliminé.");
    const c = catalogById(inst.branch);
    if (!c) throw Errors.notFound("INSTANCE_NOT_FOUND", "Ce groupe n'est plus en jeu.");
    const mask = await stateMaskOf(db, e.id, c.id);
    // 2.78: the zone of the player's own seat (each player changes zone alone), else the instance's.
    let myZone = inst.zone;
    if (inst.seat_zones) {
        const own = await db.prepare("SELECT seat FROM raid_instance_members WHERE instance_id = ?1 AND player_id = ?2 AND left_at IS NULL").bind(inst.id, me).first<{ seat: number }>();
        const z = own ? Number(inst.seat_zones.split(",")[own.seat] ?? 0) : 0;
        if (z > 0) myZone = z;
    }
    if (!zoneFinished(c, myZone, mask))
        throw Errors.conflict("RAID_ZONE_UNFINISHED", "Finissez d'abord la zone de votre groupe : seuls les groupes qui ont fini la leur reprennent une zone.");
    const target = (await zonesToRetake(db, e.id, c, mask)).find((z) => z.zone === zone);
    if (!target) throw Errors.conflict("RAID_NOTHING_TO_RETAKE", "Cette zone n'est pas à reprendre.");
    // The fresh instance of that zone for this clan (made once per wiped instance: the others join it).
    let relief = await db
        .prepare("SELECT * FROM raid_instances WHERE relieves = ?1 AND clan_id = ?2 AND wiped_zone IS NULL ORDER BY created_at LIMIT 1")
        .bind(target.instance_id, inst.clan_id)
        .first<WipeInstanceRow>();
    if (!relief) {
        const id = crypto.randomUUID();
        await db
            .prepare(
                `INSERT INTO raid_instances (id, event_id, clan_id, group_no, branch, zone, players, created_at, updated_at, start_zone, relieves)
                 SELECT ?1, ?2, ?3, COALESCE(MAX(group_no), 0) + 1, ?4, ?5, 0, ?6, ?6, ?5, ?7 FROM raid_instances WHERE event_id = ?2 AND clan_id = ?3 AND branch = ?4
                   AND NOT EXISTS (SELECT 1 FROM raid_instances WHERE relieves = ?7 AND clan_id = ?3 AND wiped_zone IS NULL)`,
            )
            .bind(id, e.id, inst.clan_id, c.id, zone, now, target.instance_id)
            .run();
        relief = await db
            .prepare("SELECT * FROM raid_instances WHERE relieves = ?1 AND clan_id = ?2 AND wiped_zone IS NULL ORDER BY created_at LIMIT 1")
            .bind(target.instance_id, inst.clan_id)
            .first<WipeInstanceRow>();
    }
    if (!relief) throw Errors.conflict("INSTANCE_BUSY", "Beaucoup de joueurs arrivent en même temps : réessayez.");
    if (!(await putIn(db, e.id, inst.clan_id, me, relief as never, me, "choix")))  // the player's own choice (the placement journal's reasons)
        throw Errors.conflict("INSTANCE_FULL", "Le groupe qui reprend cette zone a déjà 8 joueurs.");
    return enter(ctx, e as never, relief, me);
}

/**
 * GET /raids/instances/{instanceId}/watch: what a spectator of this instance's branch can watch: the
 * players still playing in it (instances not wiped, seats in play), by clan, group and seat.
 */
export async function watchTargets(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const db = ctx.env.DB;
    const inst = await instanceOf(db, ctx.params.instanceId ?? "");
    const reg = await db.prepare("SELECT 1 AS x FROM raid_registrations WHERE event_id = ?1 AND player_id = ?2").bind(inst.event_id, me).first();
    if (!reg) throw Errors.forbidden("RAID_NOT_REGISTERED", "Inscrivez-vous au raid pour y combattre.");
    const rows = (
        await db
            .prepare(
                `SELECT m.instance_id, m.seat, p.username AS name, i.group_no, i.zone, i.seat_zones, i.start_zone, c.tag FROM raid_instance_members m
                 JOIN raid_instances i ON i.id = m.instance_id JOIN players p ON p.id = m.player_id JOIN clans c ON c.id = i.clan_id
                 WHERE i.event_id = ?1 AND i.branch = ?2 AND i.wiped_zone IS NULL AND m.active = 1 AND m.left_at IS NULL
                 ORDER BY c.tag, i.group_no, m.seat LIMIT 200`,
            )
            .bind(inst.event_id, inst.branch)
            .all<{ instance_id: string; seat: number; name: string; group_no: number; zone: number; seat_zones: string | null; start_zone: number | null; tag: string }>()
    ).results;
    const s = await spectatorOf(db, inst.event_id, me);
    return json(ctx, {
        instance: { id: inst.id, wiped_zone: inst.wiped_zone, zone: inst.zone },
        spectator: !!s,
        // 2.78: each player in their own zone (a spectator can watch the other zones).
        targets: rows.map((r) => ({
            instance_id: r.instance_id,
            seat: r.seat,
            name: r.name,
            clan_tag: r.tag,
            group: r.group_no,
            zone: (r.seat_zones ? Number(r.seat_zones.split(",")[r.seat] ?? 0) : 0) || r.zone,
            start_zone: r.start_zone ?? 0,
        })),
    });
}
