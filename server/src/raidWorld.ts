// Clan raids « Asterion », phase 5 (2.69, docs/RAIDS.md §13 à §16): the WORLD EVENTS.
//
// One world event is shared by every clan registered: its instances (groups of 8 of each clan,
// raidMap.ts) all act on ONE titan (raid_branch_objectives: an objective counts once for the world,
// whoever validated it first), at the same UTC times (raids.ts: start, registrations locked, late
// arrivals: `late_minutes`). Nothing a game says is trusted: everything here is computed from what
// the consensus of each instance accepted (raidZones.ts), the only validated results:
//
// * the AGGREGATION (`aggregate`): the world's progress (objectives, zones, gigaboss), each clan's
//   totals (contribution points of its players, objectives its groups validated, those it validated
//   first, gigaboss falls, groups), each group's, each player's in every category. It is a pure
//   function of keyed tables (an objective by its id, a table of contributions by seat and step and
//   only ever replaced by a later one, a fight by instance): two instances finishing the same
//   objective at once, a late or repeated report, a reconnection never count twice. What still waits
//   for its majority is « en attente de validation », apart, never in a ranking.
// * the CLOSE (`finalizeEvent`, the cron of raids.ts every minute and the first reading after): ten
//   minutes after the end (the last reports' grace), the official result is FROZEN once
//   (raid_event_results; a trigger forbids any change), then the rewards of the final ranking are
//   granted (each once), then the clans and players are told (once).
// * the exclusive REWARDS (raidWorldRewards.ts): « Écorce d'Asterion » for the validated participants
//   of the official event, « Dévoreurs dévorés » for the players of the clans of its top 10. The
//   catch-up sessions (« sessions de rattrapage », §16) are world events of their own (`catchup_of`):
//   the same titan to discover, their own instances and results, the silver version of the suit
//   only, and never a change to the official result (another event id, the official one frozen).
// * the ADMINISTRATORS (ADMINISTRATION › Raids): the live panel (real figures only: players in play
//   written by the relays, instances, gigaboss down, reconnections counted by the database, refused
//   reports, the latency each game measured), suspend / resume (no entry, no report accepted; the
//   time lost given back), « Valider la carte » (the titan's catalogs checked by the server), a
//   catch-up session; every action journaled (audit_log and raid_world_journal).

import { audit, requireAuth } from "./auth";
import { requireAdminRole } from "./adminRoles";
import { ask } from "./coop";
import { Errors, json, log, nowIso, readJsonObject, type Ctx } from "./http";
import { CATALOGS, HEART, TITAN, catalogById, controlledIn, isDone, linksTo, partOf, reachableIn, statesOf } from "./raidMap";
import { HEART_OBJECTIVE } from "./raidHeart";
import { PARTICIPATION_TICKS, rewardsOf, scoreOf } from "./raidRewards";
import { CLAN_TOP_RANK, CLAN_TOP_TITLE, WORLD_REWARDS } from "./raidWorldRewards";
import { raidState } from "./raids";
import { RULES, rateLimit } from "./ratelimit";
import { Validator } from "./validate";

/** The last reports of an ended event are still accepted this long (raidZones.ts: the same 10 min). */
export const RESULTS_GRACE_MS = 10 * 60000;
/** A catch-up session lasts 1 to 30 days (7 by default). */
export const MAX_CATCHUP_DAYS = 30;
/** The time given back at a resumption, at most (a suspension forgotten for a week gives 6 h). */
const MAX_EXTENSION_MS = 6 * 3600000;
const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
/** raidZones.ts: the reason of the second objective of a pair refused to the same instance. */
const PAIR_REFUSAL = "un autre groupe doit accomplir%";

export interface WorldEventRow {
    id: string;
    kind: "world" | "clan";
    raid: string;
    format: string;
    clan_id: string | null;
    starts_at: string;
    lock_minutes: number;
    lock_at: string;
    ends_at: string;
    registration_open: number;
    capacity: number;
    clan_capacity: number;
    difficulty: number;
    rewards_text: string;
    published_at: string | null;
    cancelled_at: string | null;
    suspended_at: string | null;
    suspended_reason: string;
    late_minutes: number | null;
    catchup_of: string | null;
    map_check: string | null;
}

const RAID_NOT_FOUND = () => Errors.notFound("RAID_NOT_FOUND", "Événement introuvable.");

export async function eventRow(db: D1Database, id: string): Promise<WorldEventRow | null> {
    if (!ID_RE.test(id)) return null;
    return await db.prepare("SELECT * FROM raid_events WHERE id = ?1").bind(id).first<WorldEventRow>();
}

// ------------------------------------------------------------------ the map's check (« Valider la carte »)

export interface MapCheck {
    ok: boolean;
    /** 2.74 (phase 6): the Cœur d'Asterion's catalog checked (its arena, its fall, a « modifier » link from each part). */
    heart: boolean;
    revision: string;
    branches: number;
    zones: number;
    bosses: number;
    fights: number;
    links: number;
    problems: string[];
    warnings: string[];
}

/**
 * The titan's catalogs as the server plays them (raidBranches.json, raidTitan.json, written by
 * tools/levelgen): seven branches, unique zones and objectives (bits below 24), passages and links
 * between known zones and objectives (link bits 24 to 31), one gigaboss's lair per branch, every
 * zone reachable once everything is done. A gigaboss whose fight is not in the game yet is a
 * warning (its lair stays sealed), never a problem.
 */
export function checkMap(): MapCheck {
    const problems: string[] = [];
    const warnings: string[] = [];
    if (TITAN.branches.length !== 7) problems.push(`${TITAN.branches.length} branches au lieu de 7`);
    const all = new Map<string, Set<string>>();
    for (const c of CATALOGS) if (c) all.set(c.id, new Set(c.objectives.map((o) => o.id)));
    if (HEART) all.set(HEART.id, new Set(HEART.objectives.map((o) => o.id)));  // 2.74: its aorta crossed too
    const states = statesOf(all);
    let zones = 0;
    let bosses = 0;
    let fights = 0;
    for (const c of CATALOGS) {
        if (!c) {
            problems.push("catalogue de branche manquant");
            continue;
        }
        const n = c.name;
        zones += c.zones.length;
        const ids = new Set<number>();
        for (const z of c.zones) {
            if (ids.has(z.id)) problems.push(`${n} : zone ${z.id} en double`);
            ids.add(z.id);
        }
        if (!ids.has(c.start_zone)) problems.push(`${n} : zone de départ ${c.start_zone} inconnue`);
        for (const e of c.edges) if (!ids.has(e.a) || !ids.has(e.b)) problems.push(`${n} : passage ${e.a} – ${e.b} vers une zone inconnue`);
        const oids = new Set<string>();
        const bits = new Set<number>();
        for (const o of c.objectives) {
            if (oids.has(o.id)) problems.push(`${n} : objectif ${o.id} en double`);
            if (bits.has(o.index) || o.index < 0 || o.index >= 24) problems.push(`${n} : objectif ${o.id} à l'index ${o.index} invalide`);
            if (!ids.has(o.zone)) problems.push(`${n} : objectif ${o.id} dans une zone inconnue`);
            oids.add(o.id);
            bits.add(o.index);
        }
        const lairs = c.zones.filter((z) => z.boss).length;
        if (lairs !== 1) problems.push(`${n} : ${lairs} antres de gigaboss au lieu d'une`);
        else bosses++;
        if (c.objectives.some((o) => o.role === "gigaboss")) fights++;
        else warnings.push(`${n} : combat de ${c.gigaboss} à venir (antre scellée)`);
        const generators = c.objectives.filter((o) => o.role === "generator").length;
        if (c.edges.some((e) => e.lock === "generators") && generators < c.generators_needed)
            problems.push(`${n} : ${generators} générateur(s) pour ${c.generators_needed} demandés`);
        const reach = reachableIn(c, states.get(c.id) ?? 0);
        for (const z of c.zones) if (!reach[z.id]) problems.push(`${n} : zone ${z.id} « ${z.name} » inatteignable même tout accompli`);
    }
    for (const l of TITAN.links) {
        const to = catalogById(l.to);
        if (!to) {
            problems.push(`lien ${l.id} : branche cible inconnue`);
            continue;
        }
        if (l.bit < 24 || l.bit > 31) problems.push(`lien ${l.id} : bit ${l.bit} hors de 24 à 31`);
        for (const f of l.from)
            if (!catalogById(f.branch)?.objectives.some((o) => o.id === f.objective_id)) problems.push(`lien ${l.id} : source ${f.branch} / ${f.objective_id} inconnue`);
        for (const z of l.zones) if (!to.zones.some((x) => x.id === z)) problems.push(`lien ${l.id} : zone ${z} inconnue dans ${to.name}`);
    }
    // 2.74 (phase 6): the heart: its arena (a lair), its fall (role « gigaboss »), reachable once its aorta is
    // crossed, and the fall of each of the seven parts acting on it (a link « modifier »).
    if (!HEART) problems.push("Cœur d'Asterion manquant");
    else {
        if (HEART.zones.filter((z) => z.boss).length !== 1) problems.push("Cœur d'Asterion : une arène attendue");
        if (!HEART.objectives.some((o) => o.id === HEART_OBJECTIVE && o.role === "gigaboss")) problems.push("Cœur d'Asterion : objectif de sa chute manquant");
        const reach = reachableIn(HEART, states.get(HEART.id) ?? 0);
        for (const z of HEART.zones) if (!reach[z.id]) problems.push(`Cœur d'Asterion : zone ${z.id} « ${z.name} » inatteignable même tout accompli`);
        const mods = linksTo(HEART).filter((l) => l.kind === "modifier");
        for (const c of CATALOGS) {
            const g = c.objectives.find((o) => o.role === "gigaboss");
            if (g && !mods.some((l) => l.from.some((f) => f.branch === c.id && f.objective_id === g.id))) problems.push(`Cœur d'Asterion : aucun effet de la chute de ${c.gigaboss}`);
        }
    }
    const revision =
        `titan ${TITAN.revision} · ` + CATALOGS.filter((c) => !!c).map((c) => `${partOf(c.id)} ${c.revision}`).join(" · ") + (HEART ? ` · coeur ${HEART.revision}` : "");
    return { ok: problems.length === 0, heart: !!HEART, revision, branches: CATALOGS.length, zones, bosses, fights, links: TITAN.links.length, problems: problems.slice(0, 20), warnings: warnings.slice(0, 10) };
}

/** The stored check of an event, when it is the check of the catalogs the server plays now. */
export function storedCheck(e: Pick<WorldEventRow, "map_check">): (MapCheck & { at: string; by: string | null }) | null {
    if (!e.map_check) return null;
    try {
        return JSON.parse(e.map_check) as MapCheck & { at: string; by: string | null };
    } catch {
        return null;
    }
}

// ------------------------------------------------------------------ the aggregation

export interface WorldClan {
    rank: number;
    id: string;
    tag: string;
    name: string;
    points: number;
    players: number;
    registered: number;
    groups: number;
    objectives: number;
    first: number;
    gigaboss: number;
}
export interface WorldGroup {
    rank: number;
    id: string;
    clan_id: string;
    clan_tag: string;
    group: number;
    branch: string;
    branch_name: string;
    points: number;
    players: number;
    objectives: number;
    boss_phase: number;
}
export interface WorldPlayer {
    rank: number;
    id: string;
    username: string;
    clan_id: string | null;
    clan_tag: string;
    score: number;
    damage: number;
    boss_damage: number;
    avoided: number;
    support: number;
    blocks: number;
    rescues: number;
    vents: number;
    weak_points: number;
    objectives: number;
    gigaboss: number;
    minutes: number;
    seconds: number;
    progress: number;
}
export interface WorldBranch {
    part: string;
    name: string;
    gigaboss: string;
    objectives_done: number;
    objectives: number;
    zones_controlled: number;
    zones: number;
    gigaboss_down: boolean;
}
export interface WorldProgress {
    progress: number;
    /** 2.74 (phase 6): the Cœur d'Asterion fell (validated: its shared health gone). */
    heart_down: boolean;
    objectives_done: number;
    objectives: number;
    mandatory_done: number;
    mandatory: number;
    zones_controlled: number;
    zones: number;
    gigaboss_defeated: number;
    gigaboss: number;
    clans: number;
    clans_playing: number;
    players: number;
    groups: number;
    branches: WorldBranch[];
}
export interface WorldAggregate {
    world: WorldProgress;
    outcome: "vaincu" | "survecu";
    clans: WorldClan[];
    groups: WorldGroup[];
    players: WorldPlayer[];
}

/** « Asterion est tombé » once the seven gigaboss and its heart are down (validated), else « Asterion a survécu ». */
export function outcomeText(outcome: string, final: boolean): string {
    if (outcome === "vaincu") return "Asterion est tombé";
    return final ? "Asterion a survécu" : "Asterion résiste encore";
}

/** The world's progress from the objectives validated (branch → their ids): the titan's state. */
export function worldOf(done: Map<string, Set<string>>): Omit<WorldProgress, "clans" | "clans_playing" | "players" | "groups"> {
    const states = statesOf(done);
    let objectives = 0;
    let objectivesDone = 0;
    let mandatory = 0;
    let mandatoryDone = 0;
    let zones = 0;
    let zonesControlled = 0;
    let defeated = 0;
    const branches: WorldBranch[] = [];
    for (const c of CATALOGS) {
        if (!c) continue;
        const mask = states.get(c.id) ?? 0;
        const od = c.objectives.filter((o) => isDone(mask, o.index)).length;
        const zc = c.zones.filter((z) => controlledIn(c, z.id, mask)).length;
        const giga = c.objectives.find((o) => o.role === "gigaboss");
        const down = !!giga && isDone(mask, giga.index);
        objectives += c.objectives.length;
        objectivesDone += od;
        mandatory += c.objectives.filter((o) => !o.optional).length;
        mandatoryDone += c.objectives.filter((o) => !o.optional && isDone(mask, o.index)).length;
        zones += c.zones.length;
        zonesControlled += zc;
        if (down) defeated++;
        branches.push({ part: partOf(c.id), name: c.name, gigaboss: c.gigaboss, objectives_done: od, objectives: c.objectives.length, zones_controlled: zc, zones: c.zones.length, gigaboss_down: down });
    }
    return {
        progress: mandatory > 0 ? Math.floor((100 * mandatoryDone) / mandatory) : 0,
        heart_down: !!HEART && !!done.get(HEART.id)?.has(HEART_OBJECTIVE),
        objectives_done: objectivesDone,
        objectives,
        mandatory_done: mandatoryDone,
        mandatory,
        zones_controlled: zonesControlled,
        zones,
        gigaboss_defeated: defeated,
        gigaboss: CATALOGS.length,
        branches,
    };
}

interface TotalsRow {
    player_id: string;
    username: string;
    clan_id: string | null;
    damage: number;
    boss_damage: number;
    avoided: number;
    blocks: number;
    rescues: number;
    vents: number;
    weak_points: number;
    objectives: number;
    ticks: number;
    gigaboss: number;
}
interface InstanceAgg {
    id: string;
    clan_id: string;
    tag: string;
    group_no: number;
    branch: string;
    points: number;
    contributors: number;
    boss_phase: number | null;
}

const byText = (a: string, b: string) => (a < b ? -1 : a > b ? 1 : 0);

/**
 * The aggregate of an event from its validated results only (see the head of this file). Every
 * figure is recomputed from keyed tables: the same reports, accepted in any order, any number of
 * times, give the same aggregate.
 */
export async function aggregate(db: D1Database, eventId: string): Promise<WorldAggregate> {
    const [totalsR, clansR, instR, ownR, doneR] = await db.batch([
        db
            .prepare(
                `SELECT c.player_id, p.username, r.clan_id, SUM(c.damage) AS damage, SUM(c.boss_damage) AS boss_damage, SUM(c.avoided) AS avoided,
                        SUM(c.blocks) AS blocks, SUM(c.rescues) AS rescues, SUM(c.vents) AS vents, SUM(c.weak_points) AS weak_points,
                        SUM(c.objectives) AS objectives, SUM(c.ticks) AS ticks, MAX(c.gigaboss) AS gigaboss
                 FROM raid_contributions c JOIN players p ON p.id = c.player_id
                 LEFT JOIN raid_registrations r ON r.event_id = c.event_id AND r.player_id = c.player_id
                 WHERE c.event_id = ?1 AND c.player_id IS NOT NULL GROUP BY c.player_id`,
            )
            .bind(eventId),
        db
            .prepare(
                `SELECT x.clan_id, c.tag, c.name, (SELECT COUNT(*) FROM raid_registrations r WHERE r.event_id = x.event_id AND r.clan_id = x.clan_id) AS registered
                 FROM raid_clan_entries x JOIN clans c ON c.id = x.clan_id WHERE x.event_id = ?1`,
            )
            .bind(eventId),
        db
            .prepare(
                `SELECT i.id, i.clan_id, c.tag, i.group_no, i.branch,
                        (SELECT COALESCE(SUM(k.score), 0) FROM raid_contributions k WHERE k.instance_id = i.id) AS points,
                        (SELECT COUNT(DISTINCT k.player_id) FROM raid_contributions k WHERE k.instance_id = i.id AND k.player_id IS NOT NULL) AS contributors,
                        (SELECT MAX(f.phase) FROM raid_gigaboss_fights f WHERE f.instance_id = i.id) AS boss_phase
                 FROM raid_instances i JOIN clans c ON c.id = i.clan_id WHERE i.event_id = ?1`,
            )
            .bind(eventId),
        // The objectives each instance validated by its own consensus that count for the titan (not
        // the second objective of a pair refused to the same group).
        db
            .prepare(
                `SELECT a.instance_id, substr(a.report_key, 3) AS objective_id FROM raid_accepted a JOIN raid_instances i ON i.id = a.instance_id
                 WHERE i.event_id = ?1 AND a.report_key LIKE 'o:%'
                   AND EXISTS (SELECT 1 FROM raid_branch_objectives o WHERE o.event_id = ?1 AND o.branch = i.branch AND o.objective_id = substr(a.report_key, 3))
                   AND NOT EXISTS (SELECT 1 FROM raid_report_rejections j WHERE j.instance_id = a.instance_id AND j.report_key = a.report_key AND j.reason LIKE ?2)`,
            )
            .bind(eventId, PAIR_REFUSAL),
        db.prepare("SELECT branch, objective_id, instance_id FROM raid_branch_objectives WHERE event_id = ?1").bind(eventId),
    ]);
    const totals = totalsR!.results as TotalsRow[];
    const entries = clansR!.results as { clan_id: string; tag: string; name: string; registered: number }[];
    const instances = instR!.results as InstanceAgg[];
    const own = ownR!.results as { instance_id: string; objective_id: string }[];
    const doneRows = doneR!.results as { branch: string; objective_id: string; instance_id: string | null }[];

    const done = new Map<string, Set<string>>();
    for (const d of doneRows) {
        if (!done.has(d.branch)) done.set(d.branch, new Set());
        done.get(d.branch)!.add(d.objective_id);
    }
    const instById = new Map(instances.map((i) => [i.id, i]));

    // The players: every category, the mixed score (never only the damage).
    const players: WorldPlayer[] = totals
        .map((t) => {
            const s = scoreOf(t);
            const tag = entries.find((c) => c.clan_id === t.clan_id)?.tag ?? "";
            return {
                rank: 0,
                id: t.player_id,
                username: t.username,
                clan_id: t.clan_id,
                clan_tag: tag,
                score: s.total,
                damage: Math.round(t.damage / 10),
                boss_damage: Math.round(t.boss_damage / 10),
                avoided: Math.round(t.avoided / 10),
                support: t.blocks + t.rescues,
                blocks: t.blocks,
                rescues: t.rescues,
                vents: t.vents,
                weak_points: t.weak_points,
                objectives: t.objectives,
                gigaboss: t.gigaboss ? 1 : 0,
                minutes: Math.floor(t.ticks / 7200),
                seconds: Math.floor(t.ticks / 120),
                progress: s.progress,
            };
        })
        .sort((a, b) => b.score - a.score || byText(a.username, b.username) || byText(a.id, b.id));
    players.forEach((p, i) => (p.rank = i + 1));

    // The groups (instances): their validated points, objectives and fight.
    const ownBy = new Map<string, Set<string>>();
    for (const o of own) {
        const inst = instById.get(o.instance_id);
        if (!inst) continue;
        if (!ownBy.has(o.instance_id)) ownBy.set(o.instance_id, new Set());
        ownBy.get(o.instance_id)!.add(`${inst.branch}:${o.objective_id}`);
    }
    const groups: WorldGroup[] = instances
        .map((i) => ({
            rank: 0,
            id: i.id,
            clan_id: i.clan_id,
            clan_tag: i.tag,
            group: i.group_no,
            branch: partOf(i.branch),
            branch_name: catalogById(i.branch)?.name ?? "",
            points: i.points,
            players: i.contributors,
            objectives: ownBy.get(i.id)?.size ?? 0,
            boss_phase: i.boss_phase ?? 0,
        }))
        .sort((a, b) => b.points - a.points || b.objectives - a.objectives || b.boss_phase - a.boss_phase || byText(a.clan_tag, b.clan_tag) || a.group - b.group || byText(a.branch, b.branch));
    groups.forEach((g, i) => (g.rank = i + 1));

    // The clans: every clan registered, its players' points, the objectives its groups validated
    // (each once per clan), those it validated first, its gigaboss falls.
    const clans: WorldClan[] = entries
        .map((c) => {
            const mine = instances.filter((i) => i.clan_id === c.clan_id);
            const objs = new Set<string>();
            for (const i of mine) for (const k of ownBy.get(i.id) ?? []) objs.add(k);
            const ps = players.filter((p) => p.clan_id === c.clan_id);
            return {
                rank: 0,
                id: c.clan_id,
                tag: c.tag,
                name: c.name,
                points: ps.reduce((n, p) => n + p.score, 0),
                players: ps.length,
                registered: c.registered,
                groups: mine.length,
                objectives: objs.size,
                first: doneRows.filter((d) => d.instance_id && instById.get(d.instance_id)?.clan_id === c.clan_id).length,
                gigaboss: mine.filter((i) => (i.boss_phase ?? 0) >= 4).length,
            };
        })
        .sort((a, b) => b.points - a.points || b.objectives - a.objectives || b.gigaboss - a.gigaboss || byText(a.tag, b.tag) || byText(a.id, b.id));
    clans.forEach((c, i) => (c.rank = i + 1));

    const w = worldOf(done);
    return {
        world: {
            ...w,
            clans: entries.length,
            clans_playing: clans.filter((c) => c.players > 0 || c.groups > 0).length,
            players: players.length,
            groups: groups.filter((g) => g.players > 0).length,
        },
        // 2.74 (phase 6): the seven gigaboss AND the Cœur d'Asterion (its shared fall, validated).
        outcome: w.gigaboss_defeated >= w.gigaboss && w.gigaboss > 0 && w.heart_down ? "vaincu" : "survecu",
        clans,
        groups,
        players,
    };
}

export interface Pending {
    tables: number;
    objectives: number;
    instances: number;
}

/**
 * What still waits for its majority (never in a ranking): the tables of contributions newer than
 * the instance's latest accepted one, the objectives not validated for the titan yet.
 */
export async function pendingOf(db: D1Database, eventId: string, clanId?: string): Promise<Pending> {
    const rows = (
        await db
            .prepare(
                `SELECT r.instance_id, i.branch, r.report_key FROM raid_reports r JOIN raid_instances i ON i.id = r.instance_id
                 WHERE i.event_id = ?1 AND (?2 IS NULL OR i.clan_id = ?2)
                   AND NOT EXISTS (SELECT 1 FROM raid_accepted a WHERE a.instance_id = r.instance_id AND a.report_key = r.report_key)
                   AND (r.report_key LIKE 'o:%' OR (r.report_key LIKE 'r:%'
                        AND CAST(substr(r.report_key, 3) AS INTEGER) > COALESCE((SELECT MAX(x.tick) FROM raid_contributions x WHERE x.instance_id = r.instance_id), -1)))
                   AND NOT (r.report_key LIKE 'o:%' AND EXISTS (SELECT 1 FROM raid_branch_objectives o WHERE o.event_id = ?1 AND o.branch = i.branch
                                                                AND o.objective_id = substr(r.report_key, 3)))
                 GROUP BY r.instance_id, r.report_key LIMIT 2000`,
            )
            .bind(eventId, clanId ?? null)
            .all<{ instance_id: string; branch: string; report_key: string }>()
    ).results;
    return {
        tables: rows.filter((r) => r.report_key.startsWith("r:")).length,
        objectives: rows.filter((r) => r.report_key.startsWith("o:")).length,
        instances: new Set(rows.map((r) => r.instance_id)).size,
    };
}

// ------------------------------------------------------------------ the journal

/** One line of an event's journal; `once`: written once per event (the start, the close). */
export async function journal(
    db: D1Database,
    eventId: string,
    kind: "admin" | "anomaly" | "info",
    code: string,
    message: string,
    data: Record<string, unknown> = {},
    actor: string | null = null,
    once: string | null = null,
): Promise<boolean> {
    try {
        const r = await db
            .prepare(
                `INSERT INTO raid_world_journal (event_id, kind, code, once_key, message, data, actor_id, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)
                 ON CONFLICT (event_id, once_key) DO NOTHING`,
            )
            .bind(eventId, kind, code, once, message.slice(0, 300), JSON.stringify(data), actor, nowIso())
            .run();
        return r.meta.changes > 0;
    } catch (e) {
        log("error", "raid_journal_failed", { error: e instanceof Error ? e.message : String(e) });
        return false;
    }
}

/**
 * raidZones.ts: an objective accepted by an instance was already counted for the titan (another
 * group, or the same moment): journaled for the administrators, counted once.
 */
export async function journalDuplicate(db: D1Database, eventId: string, instanceId: string, branch: string, objectiveId: string, label: string, now: string): Promise<void> {
    const first = await db
        .prepare(
            `SELECT o.instance_id, o.accepted_at, i.group_no, c.tag FROM raid_branch_objectives o LEFT JOIN raid_instances i ON i.id = o.instance_id
             LEFT JOIN clans c ON c.id = i.clan_id WHERE o.event_id = ?1 AND o.branch = ?2 AND o.objective_id = ?3`,
        )
        .bind(eventId, branch, objectiveId)
        .first<{ instance_id: string | null; accepted_at: string; group_no: number | null; tag: string | null }>();
    if (!first || first.instance_id === instanceId) return;
    const me = await db.prepare("SELECT i.group_no, c.tag FROM raid_instances i JOIN clans c ON c.id = i.clan_id WHERE i.id = ?1").bind(instanceId).first<{ group_no: number; tag: string }>();
    const gap = Math.max(0, Date.parse(now) - Date.parse(first.accepted_at));
    const who = (tag: string | null | undefined, n: number | null | undefined) => (tag ? `[${tag}]·${n ?? 1}` : "un groupe");
    const delay = gap < 1000 ? `${gap} ms` : gap < 120000 ? `${Math.round(gap / 1000)} s` : `${Math.round(gap / 60000)} min`;
    await journal(
        db,
        eventId,
        "anomaly",
        "duplicate_objective",
        `Objectif « ${label} » validé par ${who(first.tag, first.group_no)} puis par ${who(me?.tag, me?.group_no)} à ${delay} d'écart : compté une fois`,
        { branch: partOf(branch), objective: objectiveId, first: first.instance_id, second: instanceId, gap_ms: gap },
    );
}

// ------------------------------------------------------------------ entries and reports (raidMap.ts, raidZones.ts)

/**
 * A player enters play (join, an instance's socket): never while the event is suspended; after the
 * late arrivals' time (`late_minutes` after the start), only a player already placed or already in
 * an instance comes back (a reconnection), never a newcomer.
 */
export async function requireEntry(db: D1Database, e: { id: string; starts_at: string }, playerId: string): Promise<void> {
    const x = e as Partial<WorldEventRow>;
    if (x.suspended_at) throw Errors.conflict("RAID_SUSPENDED", "L'événement est suspendu par les administrateurs : aucune entrée en jeu avant la reprise.");
    if (x.late_minutes === null || x.late_minutes === undefined) return;
    if (Date.now() <= Date.parse(e.starts_at) + x.late_minutes * 60000) return;
    const known = await db
        .prepare(
            `SELECT 1 AS x FROM raid_placements WHERE event_id = ?1 AND player_id = ?2
             UNION ALL SELECT 1 AS x FROM raid_instance_members m JOIN raid_instances i ON i.id = m.instance_id WHERE i.event_id = ?1 AND m.player_id = ?2 LIMIT 1`,
        )
        .bind(e.id, playerId)
        .first();
    if (!known)
        throw Errors.conflict(
            "RAID_LATE",
            x.late_minutes === 0
                ? "Pas d'arrivée en retard pour cet événement : seuls les joueurs entrés au début reviennent en jeu."
                : `Arrivée en retard fermée ${x.late_minutes} min après le début : seuls les joueurs déjà placés reviennent en jeu.`,
        );
}

/** Why no report of this event may be accepted now ("" when they may): suspended, or its result frozen. */
export async function reportHalt(db: D1Database, e: { id: string }): Promise<string> {
    const x = e as Partial<WorldEventRow>;
    if (x.suspended_at) return "événement suspendu par les administrateurs : rien ne compte avant la reprise";
    try {
        const frozen = await db.prepare("SELECT 1 AS x FROM raid_event_results WHERE event_id = ?1").bind(e.id).first();
        if (frozen) return "résultats officiels figés : l'événement est clos";
    } catch {
        /* before migration 0147 */
    }
    return "";
}

// ------------------------------------------------------------------ rewards

/**
 * The exclusive suit of a world event (raidZones.ts after each acceptance, and the close): the
 * official event gives « Écorce d'Asterion », a catch-up session its silver version, to each
 * player with 5 minutes of validated combat (all of them when `players` is omitted). Never for a
 * clan raid. INSERT OR IGNORE on the unique key: once.
 */
export async function grantWorldFor(db: D1Database, eventId: string, players?: string[]): Promise<number> {
    if (players && players.length === 0) return 0;
    try {
        const e = await eventRow(db, eventId);
        if (!e || e.kind !== "world" || !e.published_at || e.cancelled_at) return 0;
        const reward = e.catchup_of ? WORLD_REWARDS.asterion_ecorce_argent : WORLD_REWARDS.asterion_ecorce;
        const filter = players ? ` AND player_id IN (${players.map((_, i) => `?${i + 3}`).join(",")})` : "";
        const rows = (
            await db
                .prepare(`SELECT player_id FROM raid_contributions WHERE event_id = ?1 AND player_id IS NOT NULL${filter} GROUP BY player_id HAVING SUM(ticks) >= ?2`)
                .bind(eventId, PARTICIPATION_TICKS, ...(players ?? []))
                .all<{ player_id: string }>()
        ).results;
        return await grantMany(db, eventId, reward.id, reward.why, rows.map((r) => r.player_id));
    } catch (err) {
        log("error", "raid_world_grant_failed", { error: err instanceof Error ? err.message : String(err) });
        return 0;
    }
}

async function grantMany(db: D1Database, eventId: string, reward: string, why: string, players: string[]): Promise<number> {
    let n = 0;
    const now = nowIso();
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

// ------------------------------------------------------------------ the close: frozen once

interface FrozenRow {
    event_id: string;
    outcome: "vaincu" | "survecu";
    data: string;
    frozen_at: string;
    rewards_at: string | null;
    notified_at: string | null;
}
export interface FrozenData extends WorldAggregate {
    unvalidated: Pending;
}

export async function frozenOf(db: D1Database, eventId: string): Promise<(FrozenRow & { parsed: FrozenData }) | null> {
    try {
        const r = await db.prepare("SELECT * FROM raid_event_results WHERE event_id = ?1").bind(eventId).first<FrozenRow>();
        if (!r) return null;
        return { ...r, parsed: JSON.parse(r.data) as FrozenData };
    } catch {
        return null;
    }
}

/** The official world event may be closed now (ended, the last reports' grace passed, not a catch-up session). */
function closable(e: WorldEventRow, nowMs: number): boolean {
    return e.kind === "world" && !e.catchup_of && !!e.published_at && !e.cancelled_at && nowMs >= Date.parse(e.ends_at) + RESULTS_GRACE_MS;
}

/** Freezes the official result (once: the primary key; the trigger forbids any change after). */
async function freeze(db: D1Database, e: WorldEventRow, now: string): Promise<void> {
    const agg = await aggregate(db, e.id);
    const data: FrozenData = {
        ...agg,
        clans: agg.clans.slice(0, 500),
        groups: agg.groups.slice(0, 300),
        players: agg.players.slice(0, 300),
        unvalidated: await pendingOf(db, e.id),
    };
    const r = await db
        .prepare("INSERT OR IGNORE INTO raid_event_results (event_id, outcome, data, frozen_at) VALUES (?1, ?2, ?3, ?4)")
        .bind(e.id, agg.outcome, JSON.stringify(data), now)
        .run();
    if (r.meta.changes)
        await journal(
            db,
            e.id,
            "info",
            "close",
            `Fin de l'événement · ${outcomeText(agg.outcome, true)} · progression mondiale ${agg.world.progress} % · ${agg.world.gigaboss_defeated} / ${agg.world.gigaboss} gigaboss · ${agg.clans.filter((c) => c.points > 0).length} clans classés · résultat officiel figé`,
            { outcome: agg.outcome, progress: agg.world.progress },
            null,
            "close",
        );
}

/** The rewards of the final ranking (once each): the suit (catch-all) and the top 10 of the clans. */
async function grantFinal(db: D1Database, e: WorldEventRow, data: FrozenData, now: string): Promise<void> {
    await grantWorldFor(db, e.id);
    const top = data.clans.filter((c) => c.rank <= CLAN_TOP_RANK && c.points > 0);
    if (top.length) {
        await db.batch(
            top.map((c) =>
                db
                    .prepare("INSERT OR IGNORE INTO raid_clan_titles (clan_id, event_id, title, rank, granted_at) SELECT ?1, ?2, ?3, ?4, ?5 WHERE EXISTS (SELECT 1 FROM clans WHERE id = ?1)")
                    .bind(c.id, e.id, CLAN_TOP_TITLE, c.rank, now),
            ),
        );
        const marks = top.map((_, i) => `?${i + 2}`).join(",");
        const players = (
            await db
                .prepare(
                    `SELECT DISTINCT c.player_id FROM raid_contributions c JOIN raid_registrations r ON r.event_id = c.event_id AND r.player_id = c.player_id
                     WHERE c.event_id = ?1 AND c.player_id IS NOT NULL AND r.clan_id IN (${marks})`,
                )
                .bind(e.id, ...top.map((c) => c.id))
                .all<{ player_id: string }>()
        ).results.map((r) => r.player_id);
        await grantMany(db, e.id, WORLD_REWARDS.asterion_clan_top10.id, WORLD_REWARDS.asterion_clan_top10.why, players);
    }
    await db.prepare("UPDATE raid_event_results SET rewards_at = ?2 WHERE event_id = ?1 AND rewards_at IS NULL").bind(e.id, now).run();
}

/** « 1er », « 2e »… (2.78.1: no « ᵉ », not in the game's fonts) */
export const ordinal = (n: number) => (n === 1 ? "1er" : `${n}e`);

/**
 * Every player registered and the chief and officers of each clan, each with their clan's rank: told
 * once (the claim first: a second close never tells again), 200 clans per statement.
 */
async function notifyResults(db: D1Database, e: WorldEventRow, data: FrozenData, now: string): Promise<number> {
    const claim = await db.prepare("UPDATE raid_event_results SET notified_at = ?2 WHERE event_id = ?1 AND notified_at IS NULL").bind(e.id, now).run();
    if (!claim.meta.changes) return 0;
    const ranked = data.clans.filter((c) => c.points > 0).length;
    const per = data.clans.map((c) => ({
        clan: c.id,
        data: notifData(e, "results", { tag: c.tag, rank: c.points > 0 ? c.rank : 0, clans: ranked, outcome: data.outcome, progress: data.world.progress }),
    }));
    let told = 0;
    try {
        for (let at = 0; at < per.length; at += 200) {
            const r = await db
                .prepare(
                    `INSERT INTO notifications (id, player_id, kind, actor_id, data, group_key, count, created_at)
                     SELECT lower(hex(randomblob(16))), x.player_id, 'raid', NULL, json_extract(k.value, '$.data'), ?2, 1, ?3
                     FROM json_each(?1) k
                     JOIN (SELECT player_id, clan_id FROM raid_registrations WHERE event_id = ?4
                           UNION SELECT player_id, clan_id FROM clan_members WHERE role IN ('leader', 'officer')) x ON x.clan_id = json_extract(k.value, '$.clan')
                     JOIN players p ON p.id = x.player_id
                     WHERE instr(',' || p.notifications_off || ',', ',raid,') = 0
                       AND NOT EXISTS (SELECT 1 FROM notifications n WHERE n.player_id = x.player_id AND n.group_key = ?2 AND n.read_at IS NULL)`,
                )
                .bind(JSON.stringify(per.slice(at, at + 200)), `raid:results:${e.id}`, now, e.id)
                .run();
            told += r.meta.changes;
        }
    } catch (err) {
        log("error", "raid_notify_failed", { error: err instanceof Error ? err.message : String(err) });
    }
    return told;
}

export function notifData(e: Pick<WorldEventRow, "id" | "raid" | "format" | "kind" | "starts_at">, event: string, extra: Record<string, unknown> = {}) {
    return { event, event_id: e.id, raid_name: e.raid === "asterion" ? "Asterion" : e.raid, format: e.format, raid_kind: e.kind, starts_at: e.starts_at, ...extra };
}

/** « raid » notifications to some players (the kind turned off skipped; once while one of `groupKey` is unread). */
export async function insertNotifications(db: D1Database, recipients: string[], data: Record<string, unknown>, groupKey: string | null, actor: string | null = null): Promise<number> {
    const list = [...new Set(recipients)].filter((r) => r !== actor);
    let n = 0;
    try {
        for (let at = 0; at < list.length; at += 500) {
            const r = await db
                .prepare(
                    `INSERT INTO notifications (id, player_id, kind, actor_id, data, group_key, count, created_at)
                     SELECT lower(hex(randomblob(16))), r.value, 'raid', ?2, ?3, ?4, 1, ?5 FROM json_each(?1) r
                     WHERE EXISTS (SELECT 1 FROM players p WHERE p.id = r.value AND instr(',' || p.notifications_off || ',', ',raid,') = 0)
                       AND (?4 IS NULL OR NOT EXISTS (SELECT 1 FROM notifications n WHERE n.player_id = r.value AND n.group_key = ?4 AND n.read_at IS NULL))`,
                )
                .bind(JSON.stringify(list.slice(at, at + 500)), actor, JSON.stringify(data), groupKey, nowIso())
                .run();
            n += r.meta.changes;
        }
    } catch (err) {
        log("error", "raid_notify_failed", { error: err instanceof Error ? err.message : String(err) });
    }
    return n;
}

/**
 * Closes an official world event when it is due: its result frozen, then the rewards of its final
 * ranking, then the clans and players told; each step once, whoever calls it (the cron, a reading).
 */
export async function finalizeEvent(db: D1Database, eventId: string, now = nowIso()): Promise<boolean> {
    const e = await eventRow(db, eventId);
    if (!e || !closable(e, Date.parse(now))) return false;
    let f = await frozenOf(db, e.id);
    if (!f) {
        await freeze(db, e, now);
        f = await frozenOf(db, e.id);
        if (!f) return false;
    }
    if (!f.rewards_at) await grantFinal(db, e, f.parsed, now);
    if (!f.notified_at) await notifyResults(db, e, f.parsed, now);
    return true;
}

/**
 * The cron (every minute, raids.ts beside the reminders): the start of each world event journaled
 * once, the official events ended (and their grace) closed. Returns the events closed.
 */
export async function worldTick(db: D1Database, nowMs = Date.now()): Promise<number> {
    const now = new Date(nowMs).toISOString();
    let n = 0;
    try {
        const started = (
            await db
                .prepare(
                    `SELECT e.id, (SELECT COUNT(*) FROM raid_clan_entries x WHERE x.event_id = e.id) AS clans,
                            (SELECT COUNT(*) FROM raid_registrations r WHERE r.event_id = e.id) AS players FROM raid_events e
                     WHERE e.kind = 'world' AND e.published_at IS NOT NULL AND e.cancelled_at IS NULL AND e.starts_at <= ?1 AND e.ends_at > ?1
                       AND NOT EXISTS (SELECT 1 FROM raid_world_journal j WHERE j.event_id = e.id AND j.once_key = 'start') LIMIT 20`,
                )
                .bind(now)
                .all<{ id: string; clans: number; players: number }>()
        ).results;
        for (const s of started)
            await journal(db, s.id, "info", "start", `Début de l'événement · ${s.clans} clan(s) · ${s.players} joueur(s) inscrit(s)`, { clans: s.clans, players: s.players }, null, "start");
        const due = (
            await db
                .prepare(
                    `SELECT e.id FROM raid_events e LEFT JOIN raid_event_results x ON x.event_id = e.id
                     WHERE e.kind = 'world' AND e.catchup_of IS NULL AND e.published_at IS NOT NULL AND e.cancelled_at IS NULL
                       AND e.ends_at <= ?1 AND e.ends_at > ?2 AND (x.event_id IS NULL OR x.rewards_at IS NULL OR x.notified_at IS NULL) LIMIT 10`,
                )
                .bind(new Date(nowMs - RESULTS_GRACE_MS).toISOString(), new Date(nowMs - 30 * 86400000).toISOString())
                .all<{ id: string }>()
        ).results;
        for (const d of due) if (await finalizeEvent(db, d.id, now)) n++;
    } catch (err) {
        log("error", "raid_world_tick_failed", { error: err instanceof Error ? err.message : String(err) });
    }
    return n;
}

/**
 * GET /raids (raids.ts): the summary of the frozen results of some events (the history), with the
 * rank of `clanId` in each.
 */
export async function resultSummaries(db: D1Database, ids: string[], clanId: string): Promise<Map<string, Record<string, unknown>>> {
    const out = new Map<string, Record<string, unknown>>();
    const list = [...new Set(ids)].slice(0, 40);
    if (!list.length) return out;
    try {
        const rows = await db
            .prepare(`SELECT event_id, outcome, data, frozen_at FROM raid_event_results WHERE event_id IN (${list.map((_, i) => `?${i + 1}`).join(",")})`)
            .bind(...list)
            .all<FrozenRow>();
        for (const r of rows.results) {
            const d = JSON.parse(r.data) as FrozenData;
            const mine = d.clans.find((c) => c.id === clanId);
            out.set(r.event_id, {
                outcome: r.outcome,
                text: outcomeText(r.outcome, true),
                progress: d.world.progress,
                gigaboss_defeated: d.world.gigaboss_defeated,
                gigaboss: d.world.gigaboss,
                clans: d.clans.filter((c) => c.points > 0).length,
                players: d.world.players,
                my_clan_rank: mine && mine.points > 0 ? mine.rank : 0,
                frozen_at: r.frozen_at,
            });
        }
    } catch {
        /* before migration 0147 */
    }
    return out;
}

// ------------------------------------------------------------------ GET /raids/events/{id}/world

const CLAN_ROWS = 100;
const GROUP_ROWS = 60;
const PLAYER_ROWS = 100;

/** The first `n` rows of a ranking, plus the one `keep` designates when it is further. */
function topWith<T>(rows: T[], n: number, keep: (t: T) => boolean): T[] {
    const head = rows.slice(0, n);
    const extra = rows.slice(n).find(keep);
    return extra ? [...head, extra] : head;
}

/**
 * GET /raids/events/{eventId}/world: the world rankings of a world event (spec §11): the world's
 * progress and the final result, every clan, the groups, the players in every category, from the
 * validated results only; the official result once frozen (« Résultat officiel »), the live one
 * before (« en cours »); what waits for its majority apart (`pending`), never ranked. A catch-up
 * session gives its own ranking and the official result it never changes.
 */
export async function worldView(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const db = ctx.env.DB;
    const e = await eventRow(db, ctx.params.eventId ?? "");
    if (!e || !e.published_at || e.kind !== "world") throw RAID_NOT_FOUND();
    const now = nowIso();
    const state = raidState(e, now);
    if (closable(e, Date.parse(now))) await finalizeEvent(db, e.id, now);
    const frozen = e.catchup_of ? null : await frozenOf(db, e.id);
    const agg: WorldAggregate = frozen ? frozen.parsed : await aggregate(db, e.id);
    const reg = await db
        .prepare("SELECT r.clan_id, c.tag FROM raid_registrations r JOIN clans c ON c.id = r.clan_id WHERE r.event_id = ?1 AND r.player_id = ?2")
        .bind(e.id, me)
        .first<{ clan_id: string; tag: string }>();
    const member = reg ?? (await db.prepare("SELECT c.id AS clan_id, c.tag FROM clan_members m JOIN clans c ON c.id = m.clan_id WHERE m.player_id = ?1").bind(me).first<{ clan_id: string; tag: string }>());
    const myClan = member?.clan_id ?? "";
    // My group: the instance where my validated contributions are, else my placement.
    const myInst = await db
        .prepare(
            `SELECT instance_id FROM (SELECT c.instance_id, SUM(c.ticks) AS t FROM raid_contributions c WHERE c.event_id = ?1 AND c.player_id = ?2 GROUP BY c.instance_id
             UNION ALL SELECT instance_id, -1 AS t FROM raid_placements WHERE event_id = ?1 AND player_id = ?2) ORDER BY t DESC LIMIT 1`,
        )
        .bind(e.id, me)
        .first<{ instance_id: string }>();
    // My instances' tables still waiting for their majority (« en attente de validation »).
    const myPending = await db
        .prepare(
            `SELECT r.instance_id, r.report_key, COUNT(*) AS n FROM raid_reports r JOIN raid_instance_members m ON m.instance_id = r.instance_id AND m.player_id = ?2
             JOIN raid_instances i ON i.id = r.instance_id
             WHERE i.event_id = ?1 AND r.report_key LIKE 'r:%'
               AND NOT EXISTS (SELECT 1 FROM raid_accepted a WHERE a.instance_id = r.instance_id AND a.report_key = r.report_key)
               AND CAST(substr(r.report_key, 3) AS INTEGER) > COALESCE((SELECT MAX(x.tick) FROM raid_contributions x WHERE x.instance_id = r.instance_id), -1)
             GROUP BY r.instance_id, r.report_key LIMIT 5`,
        )
        .bind(e.id, me)
        .all<{ instance_id: string; report_key: string; n: number }>();
    const pending = frozen ? frozen.parsed.unvalidated : await pendingOf(db, e.id);
    const titles = myClan
        ? (await db.prepare("SELECT title, rank FROM raid_clan_titles WHERE event_id = ?1 AND clan_id = ?2").bind(e.id, myClan).all<{ title: string; rank: number }>()).results
        : [];
    let official: Record<string, unknown> | null = null;
    if (e.catchup_of) {
        const o = await frozenOf(db, e.catchup_of);
        const oe = await eventRow(db, e.catchup_of);
        official = {
            id: e.catchup_of,
            starts_at: oe?.starts_at ?? null,
            frozen: !!o,
            outcome: o?.outcome ?? null,
            text: o ? outcomeText(o.outcome, true) : null,
            progress: o?.parsed.world.progress ?? null,
            gigaboss_defeated: o?.parsed.world.gigaboss_defeated ?? null,
        };
    }
    const mine = await rewardsOf(db, me);
    const final = !!frozen;
    const clanRank = agg.clans.find((c) => c.id === myClan);
    const groupRank = myInst ? agg.groups.find((g) => g.id === myInst.instance_id) : undefined;
    const playerRank = agg.players.find((p) => p.id === me);
    return json(ctx, {
        server_time: now,
        event: {
            id: e.id,
            state,
            final,
            frozen_at: frozen?.frozen_at ?? null,
            closing: !final && !e.catchup_of && state === "termine",
            suspended: !!e.suspended_at,
            catchup: !!e.catchup_of,
            catchup_of: e.catchup_of,
            starts_at: e.starts_at,
            ends_at: e.ends_at,
        },
        world: agg.world,
        outcome: { id: final || agg.outcome === "vaincu" ? agg.outcome : "en_cours", text: outcomeText(agg.outcome, final) },
        clans: topWith(agg.clans, CLAN_ROWS, (c) => c.id === myClan).map((c) => ({ ...c, mine: c.id === myClan })),
        groups: topWith(agg.groups, GROUP_ROWS, (g) => g.id === myInst?.instance_id).map(({ clan_id, ...g }) => ({ ...g, mine: clan_id === myClan, me: g.id === myInst?.instance_id })),
        players: topWith(agg.players, PLAYER_ROWS, (p) => p.id === me).map(({ clan_id: _c, ...p }) => ({ ...p, me: p.id === me })),
        totals: { clans: agg.clans.length, groups: agg.groups.length, players: agg.players.length },
        pending,
        official,
        my: {
            clan_tag: member?.tag ?? null,
            registered: !!reg,
            clan_rank: clanRank && (clanRank.points > 0 || clanRank.groups > 0) ? clanRank.rank : 0,
            clan_points: clanRank?.points ?? 0,
            group_rank: groupRank?.rank ?? 0,
            rank: playerRank?.rank ?? 0,
            pending: myPending.results.length > 0,
            pending_reports: myPending.results.reduce((n, p) => Math.max(n, p.n), 0),
            clan_titles: titles,
        },
        rewards: mine.pending.filter((g) => g.event_id === e.id),
        rewards_claimed: mine.claimed,
    });
}

// ------------------------------------------------------------------ administration

function journalRaid(e: WorldEventRow, extra: Record<string, unknown> = {}) {
    return { id: e.id, raid: e.raid, name: e.raid === "asterion" ? "Asterion" : e.raid, format: e.format, starts_at: e.starts_at, ...extra };
}

async function adminEvent(ctx: Ctx): Promise<WorldEventRow> {
    const e = await eventRow(ctx.env.DB, ctx.params.eventId ?? "");
    if (!e || e.kind !== "world") throw RAID_NOT_FOUND();
    return e;
}

/** Everyone concerned by an event: its registered players and the chiefs and officers of its clans. */
async function concernedBy(db: D1Database, eventId: string): Promise<string[]> {
    const r = await db
        .prepare(
            `SELECT player_id FROM raid_registrations WHERE event_id = ?1
             UNION SELECT m.player_id FROM raid_clan_entries x JOIN clan_members m ON m.clan_id = x.clan_id AND m.role IN ('leader', 'officer') WHERE x.event_id = ?1`,
        )
        .bind(eventId)
        .all<{ player_id: string }>();
    return r.results.map((x) => x.player_id);
}

/**
 * GET /admin/raids/{eventId}/live (maquette « Administration », EN DIRECT): real figures only —
 * the players in play (written by the relays), the instances (zone, players, latency measured by
 * the games, when their relay answers), the gigaboss down (validated), the reconnections (counted
 * by the database), the refused reports, what waits for its majority; the journal of the
 * administrators' actions and of the anomalies (refused reports, objectives counted once); the
 * registrations; the map's check.
 */
export async function adminLive(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const db = ctx.env.DB;
    const e = await adminEvent(ctx);
    const now = nowIso();
    const [instR, statR, rejR, journalR, clansR, doneR] = await db.batch([
        db
            .prepare(
                `SELECT i.id, i.clan_id, c.tag, i.group_no, i.branch, i.zone, i.players, i.reconnects, i.created_at, i.updated_at,
                        (SELECT COUNT(*) FROM raid_placements p WHERE p.instance_id = i.id) AS placed
                 FROM raid_instances i JOIN clans c ON c.id = i.clan_id WHERE i.event_id = ?1 ORDER BY i.created_at, i.id LIMIT 500`,
            )
            .bind(e.id),
        db
            .prepare(
                `SELECT (SELECT COUNT(*) FROM raid_clan_entries WHERE event_id = ?1) AS clans,
                        (SELECT COUNT(*) FROM raid_registrations WHERE event_id = ?1) AS registered,
                        (SELECT COUNT(*) FROM raid_report_rejections j JOIN raid_instances i ON i.id = j.instance_id WHERE i.event_id = ?1) AS rejected,
                        (SELECT COUNT(*) FROM raid_world_journal WHERE event_id = ?1 AND code = 'duplicate_objective') AS duplicates`,
            )
            .bind(e.id),
        db
            .prepare(
                `SELECT j.created_at, j.reason, j.report_key, i.group_no, c.tag, p.username FROM raid_report_rejections j JOIN raid_instances i ON i.id = j.instance_id
                 JOIN clans c ON c.id = i.clan_id LEFT JOIN players p ON p.id = j.player_id WHERE i.event_id = ?1 ORDER BY j.created_at DESC, j.id DESC LIMIT 30`,
            )
            .bind(e.id),
        db
            .prepare(
                `SELECT j.created_at, j.kind, j.code, j.message, p.username FROM raid_world_journal j LEFT JOIN players p ON p.id = j.actor_id
                 WHERE j.event_id = ?1 ORDER BY j.created_at DESC, j.id DESC LIMIT 30`,
            )
            .bind(e.id),
        db
            .prepare(
                `SELECT c.id, c.tag, c.name, l.username AS leader, (SELECT COUNT(*) FROM raid_registrations r WHERE r.event_id = x.event_id AND r.clan_id = x.clan_id) AS players
                 FROM raid_clan_entries x JOIN clans c ON c.id = x.clan_id LEFT JOIN players l ON l.id = x.leader_id
                 WHERE x.event_id = ?1 ORDER BY players DESC, c.tag LIMIT 200`,
            )
            .bind(e.id),
        db.prepare("SELECT branch, objective_id FROM raid_branch_objectives WHERE event_id = ?1").bind(e.id),
    ]);
    const instances = instR!.results as {
        id: string;
        clan_id: string;
        tag: string;
        group_no: number;
        branch: string;
        zone: number;
        players: number;
        reconnects: number;
        created_at: string;
        updated_at: string;
        placed: number;
    }[];
    const stats = statR!.results[0] as { clans: number; registered: number; rejected: number; duplicates: number };
    const done = new Map<string, Set<string>>();
    for (const d of doneR!.results as { branch: string; objective_id: string }[]) {
        if (!done.has(d.branch)) done.set(d.branch, new Set());
        done.get(d.branch)!.add(d.objective_id);
    }
    const world = worldOf(done);
    // The latency each game measured (its relay's seats): asked to the relays of the instances in play.
    const playing = instances.filter((i) => i.players > 0).slice(0, 24);
    const pings = new Map<string, number>();
    await Promise.all(
        playing.map(async (i) => {
            try {
                const s = await ask<{ seats?: { ping?: number; connected?: boolean }[] }>(ctx.env, i.id, "/status");
                const ms = (s.seats ?? []).filter((x) => x.connected && typeof x.ping === "number" && x.ping >= 0).map((x) => x.ping!);
                if (ms.length) pings.set(i.id, Math.round(ms.reduce((a, b) => a + b, 0) / ms.length));
            } catch {
                /* the relay did not answer: no figure rather than a guess */
            }
        }),
    );
    const pending = await pendingOf(db, e.id);
    const number = new Map(instances.map((i, k) => [i.id, k + 1]));
    const zoneName = (branch: string, zone: number) => catalogById(branch)?.zones.find((z) => z.id === zone)?.name ?? `zone ${zone}`;
    const shown = [...instances].sort((a, b) => (b.players > 0 ? 1 : 0) - (a.players > 0 ? 1 : 0) || b.players - a.players || number.get(a.id)! - number.get(b.id)!).slice(0, 60);
    const anomalies = (rejR!.results as { created_at: string; reason: string; report_key: string; group_no: number; tag: string; username: string | null }[]).map((r) => ({
        at: r.created_at,
        kind: "anomaly",
        code: "report_refused",
        text: r.username ? `Rapport refusé (${r.reason}) : [${r.tag}]·${r.group_no}, ${r.username}` : `Rapport refusé (${r.reason}) : [${r.tag}]·${r.group_no}`,
    }));
    const lines = (journalR!.results as { created_at: string; kind: string; code: string; message: string; username: string | null }[]).map((j) => ({
        at: j.created_at,
        kind: j.kind,
        code: j.code,
        text: j.username && j.kind === "admin" ? `${j.username} : ${j.message}` : j.message,
    }));
    const check = storedCheck(e);
    const state = raidState(e, now);
    const frozen = await frozenOf(db, e.id);
    const catchups = (
        await db.prepare("SELECT id, starts_at, ends_at, cancelled_at FROM raid_events WHERE catchup_of = ?1 ORDER BY starts_at DESC LIMIT 5").bind(e.id).all<{
            id: string;
            starts_at: string;
            ends_at: string;
            cancelled_at: string | null;
        }>()
    ).results;
    return json(ctx, {
        server_time: now,
        event: {
            id: e.id,
            state,
            started_at: e.starts_at,
            ends_at: e.ends_at,
            elapsed_s: state === "en_cours" ? Math.max(0, Math.floor((Date.parse(now) - Date.parse(e.starts_at)) / 1000)) : 0,
            suspended: !!e.suspended_at,
            suspended_at: e.suspended_at,
            suspended_reason: e.suspended_reason ?? "",
            late_minutes: e.late_minutes,
            catchup_of: e.catchup_of,
            frozen_at: frozen?.frozen_at ?? null,
            outcome: frozen ? outcomeText(frozen.outcome, true) : null,
        },
        stats: {
            players: instances.reduce((n, i) => n + i.players, 0),
            instances: instances.filter((i) => i.players > 0).length,
            instances_total: instances.length,
            gigaboss_defeated: world.gigaboss_defeated,
            gigaboss: world.gigaboss,
            reconnects: instances.reduce((n, i) => n + (i.reconnects ?? 0), 0),
            rejected: stats.rejected,
            duplicates: stats.duplicates,
            pending_tables: pending.tables,
            pending_objectives: pending.objectives,
            clans: stats.clans,
            registered: stats.registered,
            progress: world.progress,
        },
        instances: shown.map((i) => ({
            id: i.id,
            number: number.get(i.id)!,
            clan_tag: i.tag,
            group: i.group_no,
            branch: partOf(i.branch),
            branch_name: catalogById(i.branch)?.name ?? i.branch,
            zone: i.zone,
            zone_name: zoneName(i.branch, i.zone),
            players: i.players,
            placed: i.placed,
            latency_ms: pings.get(i.id) ?? null,
            reconnects: i.reconnects ?? 0,
            updated_at: i.updated_at,
        })),
        journal: [...lines, ...anomalies].sort((a, b) => byText(b.at, a.at)).slice(0, 40),
        clans: clansR!.results,
        map: check ? { ...check, current: check.revision === checkMap().revision } : null,
        catchups: catchups.map((c) => ({ ...c, cancelled: !!c.cancelled_at })),
    });
}

/** POST /admin/raids/{eventId}/suspend {reason?}: no entry into play, no report accepted until the resumption (Journal). */
export async function adminSuspend(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["reason"]);
    const reason = v.str("reason", { max: 200, optional: true, trim: true }) ?? "";
    v.done();
    const db = ctx.env.DB;
    const e = await adminEvent(ctx);
    const now = nowIso();
    const r = await db
        .prepare(
            `UPDATE raid_events SET suspended_at = ?2, suspended_reason = ?3, updated_at = ?2
             WHERE id = ?1 AND kind = 'world' AND published_at IS NOT NULL AND cancelled_at IS NULL AND suspended_at IS NULL AND ends_at > ?2`,
        )
        .bind(e.id, now, reason)
        .run();
    if (!r.meta.changes) {
        if (e.suspended_at) throw Errors.conflict("RAID_SUSPENDED", "L'événement est déjà suspendu.");
        if (!e.published_at) throw Errors.conflict("RAID_DRAFT", "Un brouillon ne se suspend pas : il n'est pas publié.");
        if (e.cancelled_at) throw Errors.conflict("RAID_CANCELLED", "Cet événement est annulé.");
        throw Errors.conflict("RAID_FINISHED", "Cet événement est terminé.");
    }
    await audit(ctx, auth.player.id, "admin_raid_suspended", { raid: journalRaid(e), by_username: auth.player.username, reason: reason || null });
    await journal(db, e.id, "admin", "suspended", reason ? `Événement suspendu · motif : ${reason}` : "Événement suspendu", { reason }, auth.player.id);
    const told = await insertNotifications(db, await concernedBy(db, e.id), notifData(e, "suspended"), `raid:suspended:${e.id}`);
    return json(ctx, { suspended: true, suspended_at: now, notified: told });
}

/**
 * POST /admin/raids/{eventId}/resume: the event goes on; the time it was suspended while running is
 * given back (its end moves by as much, 6 h at most). Journal.
 */
export async function adminResume(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const db = ctx.env.DB;
    const e = await adminEvent(ctx);
    if (!e.suspended_at) throw Errors.conflict("RAID_NOT_SUSPENDED", "L'événement n'est pas suspendu.");
    const nowMs = Date.now();
    const now = new Date(nowMs).toISOString();
    const lost = Math.min(MAX_EXTENSION_MS, Math.max(0, nowMs - Math.max(Date.parse(e.suspended_at), Date.parse(e.starts_at))));
    const endsAt = new Date(Date.parse(e.ends_at) + (nowMs > Date.parse(e.starts_at) ? lost : 0)).toISOString();
    const r = await db
        .prepare("UPDATE raid_events SET suspended_at = NULL, suspended_reason = '', ends_at = ?3, updated_at = ?4 WHERE id = ?1 AND suspended_at = ?2")
        .bind(e.id, e.suspended_at, endsAt, now)
        .run();
    if (!r.meta.changes) throw Errors.conflict("RAID_NOT_SUSPENDED", "L'événement n'est plus suspendu.");
    const minutes = Math.round((Date.parse(endsAt) - Date.parse(e.ends_at)) / 60000);
    await audit(ctx, auth.player.id, "admin_raid_resumed", { raid: journalRaid(e, { extended_minutes: minutes }), by_username: auth.player.username });
    await journal(db, e.id, "admin", "resumed", minutes > 0 ? `Événement repris · fin repoussée de ${minutes} min` : "Événement repris", { extended_minutes: minutes }, auth.player.id);
    const told = await insertNotifications(db, await concernedBy(db, e.id), notifData(e, "resumed", { minutes }), `raid:resumed:${e.id}`);
    return json(ctx, { suspended: false, ends_at: endsAt, extended_minutes: minutes, notified: told });
}

/** POST /admin/raids/{eventId}/validate-map: the titan's catalogs checked by the server (« Valider la carte »), stored on the event, journaled. */
export async function adminValidateMap(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const db = ctx.env.DB;
    const e = await adminEvent(ctx);
    const check = checkMap();
    const now = nowIso();
    const stored = { ...check, at: now, by: auth.player.username };
    await db.prepare("UPDATE raid_events SET map_check = ?2, updated_at = ?3 WHERE id = ?1").bind(e.id, JSON.stringify(stored), now).run();
    const summary = `${check.zones} zones, ${check.bosses} antres de gigaboss, ${check.fights} combat(s) prêt(s), ${check.links} liens`;
    await audit(ctx, auth.player.id, "admin_raid_map_validated", { raid: journalRaid(e, { ok: check.ok, summary, problems: check.problems.length }), by_username: auth.player.username });
    await journal(db, e.id, "admin", "map_validated", check.ok ? `Carte validée · ${summary}` : `Carte refusée · ${check.problems.length} problème(s) : ${check.problems[0] ?? ""}`, { ok: check.ok }, auth.player.id);
    return json(ctx, { map: { ...stored, current: true } });
}

/**
 * raids.ts (publish): the map must be valid. The stored check counts when it is the check of the
 * catalogs played now; else the server checks them again (and stores the check).
 */
export async function requireValidMap(db: D1Database, e: Pick<WorldEventRow, "id" | "map_check">, by: string | null): Promise<void> {
    const stored = storedCheck(e);
    const current = checkMap();
    const check = stored && stored.revision === current.revision ? stored : { ...current, at: nowIso(), by };
    if (check !== stored) await db.prepare("UPDATE raid_events SET map_check = ?2 WHERE id = ?1").bind(e.id, JSON.stringify(check)).run();
    if (!check.ok) throw Errors.conflict("RAID_MAP_INVALID", `La carte d'Asterion n'est pas valide : ${check.problems[0] ?? "problème inconnu"}`, { problems: check.problems });
}

/**
 * POST /admin/raids/{eventId}/catchup {days?}: a catch-up session (spec §16) after an official world
 * event ended: the same raid to discover for 1 to 30 days (7 by default), registrations open the
 * whole time (a member's registration enters their clan), its own instances and results, the
 * silver rewards; the official result never changes. Every clan is told (once while unread). Journal.
 */
export async function adminCatchup(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["days"]);
    const days = v.int("days", { min: 1, max: MAX_CATCHUP_DAYS, optional: true }) ?? 7;
    v.done();
    const db = ctx.env.DB;
    const e = await adminEvent(ctx);
    const nowMs = Date.now();
    const now = new Date(nowMs).toISOString();
    if (e.catchup_of) throw Errors.conflict("RAID_IS_CATCHUP", "C'est déjà une session de rattrapage : ouvrez-la depuis l'événement officiel.");
    if (!e.published_at || e.cancelled_at) throw Errors.conflict("RAID_NOT_PUBLISHED", "Seul un événement mondial publié, non annulé, a des sessions de rattrapage.");
    if (now < e.ends_at) throw Errors.conflict("RAID_NOT_FINISHED", "L'événement officiel n'est pas terminé : le rattrapage vient après lui.");
    const id = crypto.randomUUID();
    const endsAt = new Date(nowMs + days * 86400000).toISOString();
    // One session open at a time (the INSERT checks it: two clicks at once open one).
    const r = await db
        .prepare(
            `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity,
               difficulty, rewards_text, published_at, created_by, created_at, updated_at, catchup_of, late_minutes)
             SELECT ?1, 'world', o.raid, o.format, NULL, ?2, 0, ?3, ?3, 1, 0, o.clan_capacity, o.difficulty, ?4, ?2, ?5, ?2, ?2, o.id, NULL
             FROM raid_events o WHERE o.id = ?6
               AND NOT EXISTS (SELECT 1 FROM raid_events x WHERE x.catchup_of = ?6 AND x.cancelled_at IS NULL AND x.ends_at > ?2)`,
        )
        .bind(id, now, endsAt, "Session de rattrapage : versions argentées des récompenses exclusives, sans effet sur le résultat officiel de l'événement mondial.", auth.player.id, e.id)
        .run();
    if (!r.meta.changes) throw Errors.conflict("CATCHUP_OPEN", "Une session de rattrapage est déjà ouverte pour cet événement.");
    const session = (await eventRow(db, id))!;
    await audit(ctx, auth.player.id, "admin_raid_catchup", { raid: journalRaid(e, { catchup_id: id, days }), by_username: auth.player.username });
    await journal(db, e.id, "admin", "catchup", `Session de rattrapage ouverte pour ${days} jour(s) : le résultat officiel ne change pas`, { catchup_id: id, days }, auth.player.id);
    await journal(db, id, "info", "catchup_of", "Session de rattrapage de l'événement mondial : versions argentées, résultat officiel inchangé", { official: e.id });
    const members = (await db.prepare("SELECT player_id FROM clan_members").all<{ player_id: string }>()).results.map((x) => x.player_id);
    const told = await insertNotifications(db, members, notifData(session, "catchup", { days }), `raid:catchup:${e.id}`);
    return json(ctx, { event: { id, starts_at: now, ends_at: endsAt, catchup_of: e.id }, notified: told }, { status: 201 });
}
