// Clan raids « Asterion », phase 4 (2.68, docs/RAIDS.md): the whole titan. Its seven branches
// (the BRAS DROIT of phase 2, raidBranches.json, and the six others of raidTitan.json, both
// written by tools/levelgen: never edit them by hand), the links between branches, the global map,
// each branch's map, and the placement of the players in instances.
//
// * The state of a branch is the set of its objectives accepted (raid_branch_objectives, by the
//   consensus of raidZones.ts) plus the LINKS of the titan that act on it: a link is on when all
//   its source objectives (in other branches) are accepted; it is the bit `bit` (24 and up) of
//   the target branch's state. The relays read that state (raidZones.branchMaskOf, polled every
//   3 s and on each acceptance) and give it to their games at a step (RAID_STATE): a passage of
//   another branch opens, its lasers go dark, its reinforcements are gone or its danger stops, at
//   the same step on every computer of an instance. Nothing comes from a game.
// * Placement, §41 (titan 16, docs/RAIDS.md): ALWAYS seven groups per clan at the start, group k in
//   an instance of branch k (14 registered at least, 56 in play at most, the others on a waiting
//   list; raids.ts groupsOf). A player only ever plays their group's instance: another branch or
//   another group's instance is refused (no help, no new instance); a group leaves its branch only
//   to replace a wiped group of its clan (raidWipe.ts retake) and, once the seven Gigaboss fell,
//   for its own instance of the Cœur. The raid leader, the chief and the officers move a member
//   between groups (2 to 8 players each, one conditional statement) or even them out. Every move is
//   journaled (raid_placement_log, migration 0144).

import { requireAuth } from "./auth";
import { ask } from "./coop";
import { ApiError, Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import brasDroitJson from "./raidBranches.json";
import titanJson from "./raidTitan.json";
import { MAX_IN_PLAY, MIN_CLAN_PLAYERS, MIN_GROUP, RAID_GROUPS, groupsOf, raidState } from "./raids";
import { requireEntry } from "./raidWorld";
import { HEART_OBJECTIVE, gigabossDownIn, heartBodyState, heartOpenIn, heartView } from "./raidHeart";
import { RULES, rateLimit } from "./ratelimit";
import { Validator } from "./validate";
import { spectatorAnswer, spectatorOf, zoneFinished, zonesToRetake } from "./raidWipe";

export interface CatalogZone {
    id: number;
    name: string;
    sub: string;
    x: number;
    y: number;
    inside: string;
    event: string;
    secret: boolean;
    boss: boolean;
    groups_required: number;
}
export interface CatalogObjective {
    id: string;
    index: number;
    zone: number;
    label: string;
    optional: boolean;
    role: string;
    pair: string;
}
export interface Catalog {
    id: string;
    revision: number;
    branch: string;
    name: string;
    gigaboss: string;
    start_zone: number;
    generators_needed: number;
    lock_texts?: Record<string, { hint: string; message: string; objective: string }>;
    zones: CatalogZone[];
    edges: { a: number; b: number; lock: string; secret: boolean }[];
    objectives: CatalogObjective[];
}
export interface TitanLink {
    id: string;
    label: string;
    from: { branch: string; objective_id: string }[];
    to: string;
    bit: number;
    kind: "open" | "disable" | "remove" | "hazard_off" | "modifier";
    zones: number[];
    objects: string[];
}
interface TitanFile {
    revision: number;
    branches: { part: string; id: string; dir: string }[];
    links: TitanLink[];
    catalogs: Catalog[];
    /** 2.74 (phase 6): its heart, the final fight (raidHeart.ts), apart from the seven branches. */
    heart?: { part: string; id: string; dir: string };
    heart_catalog?: Catalog;
}

export const TITAN = titanJson as unknown as TitanFile;
const BRAS_DROIT = brasDroitJson as unknown as Catalog;
/** The seven branches in the order of the titan's body. */
export const CATALOGS: Catalog[] = TITAN.branches.map((b) => (b.id === BRAS_DROIT.id ? BRAS_DROIT : TITAN.catalogs.find((c) => c.id === b.id)!));
export const PARTS = TITAN.branches.map((b) => b.part);
/** 2.74 (phase 6): the Cœur d'Asterion's catalog (two zones: its aorta, its arena), joined like a branch once open. */
export const HEART: Catalog | undefined = TITAN.heart_catalog;
/** The seven branches and the heart: what an instance may play. */
const PLAYABLE: Catalog[] = HEART ? [...CATALOGS, HEART] : CATALOGS;
export const INSTANCE_SIZE = 8;
const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const PLAYER_ID_RE = /^[A-Za-z0-9_-]{1,64}$/;

export const catalogById = (id: string) => PLAYABLE.find((c) => c.id === id);
export const catalogByPart = (part: string) => {
    if (HEART && TITAN.heart && part === TITAN.heart.part) return HEART; // 2.74 (phase 6)
    const b = TITAN.branches.find((x) => x.part === part);
    return b ? catalogById(b.id) : undefined;
};
export const partOf = (id: string) => TITAN.branches.find((b) => b.id === id)?.part ?? (HEART && TITAN.heart && id === HEART.id ? TITAN.heart.part : "");
/** The relay's level of an instance and its hash: every game must have the same branch and titan. */
export const levelIdOf = (c: Catalog) => "raid:" + c.id;
export const levelHashOf = (c: Catalog) => `${c.revision}.${TITAN.revision}`;
/**
 * Titan 13: the level hash an instance's relay gives its games (GO): the branch's, then the event's difficulty
 * (« 3.13.d4 »: game/raid/Difficulty.h, readLevelHash). Every game of the instance plays the Gigaboss and the
 * Heart at that difficulty; a game of another titan refuses it (« mettez le jeu à jour »).
 */
export const DIFFICULTY_MIN = 1;
export const DIFFICULTY_MAX = 5;
export const difficultyOf = (e: { difficulty?: number | null }) =>
    Math.min(DIFFICULTY_MAX, Math.max(DIFFICULTY_MIN, Math.round(Number(e.difficulty ?? 3)) || 3));
export const levelHashFor = (c: Catalog, e: { difficulty?: number | null }) => `${levelHashOf(c)}.d${difficultyOf(e)}`;

// ------------------------------------------------------------------ rules of a state (as game/raid/RaidBranch)

export const isDone = (mask: number, index: number) => index >= 0 && index < 32 && ((mask >>> index) & 1) === 1;
export const objectiveIn = (c: Catalog, id: string) => c.objectives.find((o) => o.id === id);
export const linksTo = (c: Catalog) => TITAN.links.filter((l) => l.to === c.id);
export const linksFrom = (c: Catalog) => TITAN.links.filter((l) => l.from.some((f) => f.branch === c.id));

export function maskOfIds(c: Catalog, ids: Iterable<string>): number {
    let m = 0;
    for (const id of ids) {
        const o = objectiveIn(c, id);
        if (o) m = (m | (1 << o.index)) >>> 0;
    }
    return m;
}

export function generatorsOf(c: Catalog, mask: number): number {
    return c.objectives.filter((o) => o.role === "generator" && isDone(mask, o.index)).length;
}

export function lockOpenIn(c: Catalog, lock: string, mask: number): boolean {
    if (!lock) return true;
    if (lock === "generators") return generatorsOf(c, mask) >= c.generators_needed;
    if (lock === "shortcut") return c.objectives.some((o) => o.role === "shortcut" && isDone(mask, o.index));
    if (lock.startsWith("objective:")) {
        const o = objectiveIn(c, lock.slice(10));
        return !!o && isDone(mask, o.index);
    }
    if (lock.startsWith("link:")) {
        const l = linksTo(c).find((x) => x.id === lock.slice(5));
        return !!l && l.kind === "open" && isDone(mask, l.bit);
    }
    if (lock.startsWith("ambushes:")) {
        // 2.78: the lair's passage: every ambush of the zone before it (its objectives of role « ambush ») beaten.
        const a = ambushesIn(c, Number(lock.slice(9)), mask);
        return a.total > 0 && a.left === 0;
    }
    return false;
}

/** 2.78: the ambushes of a zone (objectives of role « ambush »): how many, how many still to beat. */
export function ambushesIn(c: Catalog, zone: number, mask: number): { left: number; total: number } {
    const all = c.objectives.filter((o) => o.zone === zone && o.role === "ambush");
    return { total: all.length, left: all.filter((o) => !isDone(mask, o.index)).length };
}

/**
 * 2.78: the zone of each seat of an instance (`seat_zones`, « z0,…,z7 », accepted from the games' reports
 * « zones »; 0 for a free seat or an armory). Without it (an instance of before 2.78, or no report yet):
 * every player in the instance's zone.
 */
export function seatZonesOf(i: { zone: number; seat_zones?: string | null }): number[] {
    if (!i.seat_zones) return [];
    return i.seat_zones.split(",").map((x) => Number(x) || 0);
}

/** 2.78: the zones where an instance has players (its zone alone before the first report « zones »). */
export function zonesOf(i: { zone: number; seat_zones?: string | null }): number[] {
    const seats = seatZonesOf(i).filter((z) => z > 0);
    return seats.length ? [...new Set(seats)].sort((a, b) => a - b) : [i.zone];
}

/** The zones reachable from the start with the passages open (index = zone id). */
export function reachableIn(c: Catalog, mask: number): boolean[] {
    const r = new Array<boolean>(c.zones.length + 1).fill(false);
    const todo = [c.start_zone];
    r[c.start_zone] = true;
    while (todo.length) {
        const z = todo.shift()!;
        for (const e of c.edges) {
            if (e.a !== z && e.b !== z) continue;
            const other = e.a === z ? e.b : e.a;
            if (r[other] || !lockOpenIn(c, e.lock, mask)) continue;
            r[other] = true;
            todo.push(other);
        }
    }
    return r;
}

export function controlledIn(c: Catalog, zone: number, mask: number): boolean {
    const mandatory = c.objectives.filter((o) => o.zone === zone && !o.optional);
    return mandatory.length > 0 && mandatory.every((o) => isDone(mask, o.index));
}

/**
 * 2.75: a zone is FINISHED (game/raid/ZoneGoals: zoneFinished, the same rule) when every objective of it
 * that is not optional is done; a zone of optional objectives only (the secret one): all of them.
 */
export function zoneFinishedIn(c: Catalog, zone: number, mask: number): boolean {
    const all = c.objectives.filter((o) => o.zone === zone);
    const mandatory = all.filter((o) => !o.optional);
    if (mandatory.length > 0) return mandatory.every((o) => isDone(mask, o.index));
    return all.length > 0 && all.every((o) => isDone(mask, o.index));
}

/** A link is on when every one of its source objectives is accepted (`done`: branch id → objectives). */
export function linkOn(l: TitanLink, done: Map<string, Set<string>>): boolean {
    return l.from.every((f) => done.get(f.branch)?.has(f.objective_id) ?? false);
}

/** The state of each branch: its objectives accepted (bits below 24) and the links on (their bits). */
export function statesOf(done: Map<string, Set<string>>): Map<string, number> {
    const out = new Map<string, number>();
    for (const c of PLAYABLE) { // 2.72: the heart too (its « modifier » links: the parts fallen)
        let m = maskOfIds(c, done.get(c.id) ?? []);
        for (const l of linksTo(c)) if (linkOn(l, done)) m = (m | (1 << l.bit)) >>> 0;
        out.set(c.id, m);
    }
    return out;
}

/** What is accepted in an event, by branch (raid_branch_objectives). */
export async function doneOf(db: D1Database, eventId: string): Promise<Map<string, Set<string>>> {
    const rows = await db.prepare("SELECT branch, objective_id FROM raid_branch_objectives WHERE event_id = ?1").bind(eventId).all<{ branch: string; objective_id: string }>();
    const done = new Map<string, Set<string>>();
    for (const r of rows.results) {
        if (!done.has(r.branch)) done.set(r.branch, new Set());
        done.get(r.branch)!.add(r.objective_id);
    }
    return done;
}

/** The state of one branch of an event (what its relays give their games). */
export async function stateMaskOf(db: D1Database, eventId: string, branch: string): Promise<number> {
    return statesOf(await doneOf(db, eventId)).get(branch) ?? 0;
}

// ------------------------------------------------------------------ D1 helpers

interface EventRow {
    id: string;
    kind: string;
    difficulty?: number | null; // titan 13: the Gigaboss' and the Heart's (1 to 5)
    clan_id: string | null;
    starts_at: string;
    lock_at: string;
    ends_at: string;
    registration_open: number;
    capacity: number;
    published_at: string | null;
    cancelled_at: string | null;
}
interface InstanceRow {
    id: string;
    event_id: string;
    clan_id: string;
    group_no: number;
    branch: string;
    zone: number;
    players: number;
    /** 2.75 (raidWipe.ts): the zone where the group was wiped (null: in play), a reinforcement's first zone. */
    wiped_zone?: number | null;
    start_zone?: number | null;
    relieves?: string | null;
}

async function eventOf(db: D1Database, id: string): Promise<EventRow> {
    if (!ID_RE.test(id)) throw Errors.notFound("RAID_NOT_FOUND", "Événement introuvable.");
    const e = await db.prepare("SELECT * FROM raid_events WHERE id = ?1").bind(id).first<EventRow>();
    if (!e || !e.published_at) throw Errors.notFound("RAID_NOT_FOUND", "Événement introuvable.");
    return e;
}

function requireRunning(e: EventRow, now: string) {
    if (raidState(e, now) !== "en_cours") throw Errors.conflict("RAID_NOT_RUNNING", "Le raid n'est pas en cours : les combats se jouent pendant l'événement.");
}

async function registrationOf(db: D1Database, eventId: string, playerId: string) {
    return await db
        .prepare("SELECT r.clan_id, c.tag FROM raid_registrations r JOIN clans c ON c.id = r.clan_id WHERE r.event_id = ?1 AND r.player_id = ?2")
        .bind(eventId, playerId)
        .first<{ clan_id: string; tag: string }>();
}

/** A clan member who may see the event's maps (registered, or in a clan taking part). */
async function requireViewer(db: D1Database, e: EventRow, me: string) {
    const reg = await registrationOf(db, e.id, me);
    if (e.kind === "clan" && !reg) {
        const mine = await db
            .prepare("SELECT 1 AS x FROM raid_clan_entries x JOIN clan_members m ON m.clan_id = x.clan_id WHERE x.event_id = ?1 AND m.player_id = ?2")
            .bind(e.id, me)
            .first();
        if (!mine) throw Errors.notFound("RAID_NOT_FOUND", "Événement introuvable.");
    }
    return reg;
}

/** The chief and the officers of the clan, and the clan's raid leader of the event, place the players. */
async function canManage(db: D1Database, eventId: string, clanId: string, me: string): Promise<boolean> {
    const r = await db
        .prepare(
            `SELECT (SELECT 1 FROM clan_members WHERE clan_id = ?2 AND player_id = ?3 AND role IN ('leader', 'officer')) AS officer,
                    (SELECT 1 FROM raid_clan_entries WHERE event_id = ?1 AND clan_id = ?2 AND leader_id = ?3) AS raid_leader`,
        )
        .bind(eventId, clanId, me)
        .first<{ officer: number | null; raid_leader: number | null }>();
    return !!r && (r.officer === 1 || r.raid_leader === 1);
}

// ------------------------------------------------------------------ placement: §41, the seven groups

interface Placement {
    instance_id: string;
    branch: string;
    clan_id: string;
}

async function placementOf(db: D1Database, eventId: string, playerId: string) {
    return await db.prepare("SELECT instance_id, branch, clan_id FROM raid_placements WHERE event_id = ?1 AND player_id = ?2").bind(eventId, playerId).first<Placement>();
}

/**
 * Puts `playerId` (of `clanId`) in instance `inst`, unless 8 others are already placed there (one
 * statement: two placements at once never make a ninth). Journals a change. True when placed.
 */
export async function putIn(db: D1Database, eventId: string, clanId: string, playerId: string, inst: InstanceRow, by: string | null, reason: string): Promise<boolean> {
    const before = await placementOf(db, eventId, playerId);
    if (before?.instance_id === inst.id) return true;
    const now = nowIso();
    const r = await db
        .prepare(
            `INSERT INTO raid_placements (event_id, player_id, clan_id, branch, instance_id, placed_by, reason, created_at, updated_at)
             SELECT ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?8
             WHERE (SELECT COUNT(*) FROM raid_placements WHERE instance_id = ?5 AND player_id != ?2) < ?9
             ON CONFLICT (event_id, player_id) DO UPDATE SET clan_id = excluded.clan_id, branch = excluded.branch, instance_id = excluded.instance_id,
                 placed_by = excluded.placed_by, reason = excluded.reason, updated_at = excluded.updated_at`,
        )
        .bind(eventId, playerId, clanId, inst.branch, inst.id, by, reason, now, INSTANCE_SIZE)
        .run();
    if (!r.meta.changes) return false;
    await db
        .prepare("INSERT INTO raid_placement_log (event_id, player_id, from_instance, to_instance, by_player, reason, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)")
        .bind(eventId, playerId, before?.instance_id ?? null, inst.id, by, reason, now)
        .run();
    return true;
}

/** Every instance of a clan in an event (its seven groups, the reliefs, the heart's), oldest first. */
export async function clanInstancesOf(db: D1Database, eventId: string, clanId: string): Promise<InstanceRow[]> {
    return (
        await db
            .prepare("SELECT * FROM raid_instances WHERE event_id = ?1 AND clan_id = ?2 ORDER BY created_at, group_no, id LIMIT 100")
            .bind(eventId, clanId)
            .all<InstanceRow>()
    ).results;
}

/**
 * §41: where group `group` of a clan is now: its newest instance (a group that replaced a wiped one plays its relief
 * instance; once the Cœur opened, its heart instance). Null when that newest instance was wiped (the group is out for
 * good, its players spectators) or the group was never formed.
 */
export function currentOf(rows: InstanceRow[], group: number): InstanceRow | null {
    let cur: InstanceRow | null = null;
    for (const i of rows) if (i.group_no === group) cur = i; // rows oldest first: the last one is the newest
    return cur && cur.wiped_zone == null ? cur : null;
}

/** §41: the groups of a clan still in play (their current instance), by number. */
export function groupsInPlay(rows: InstanceRow[]): InstanceRow[] {
    const out: InstanceRow[] = [];
    for (let k = 1; k <= RAID_GROUPS; ++k) {
        const i = currentOf(rows, k);
        if (i) out.push(i);
    }
    return out;
}

const TOO_FEW = (n: number) =>
    Errors.conflict(
        "RAID_TOO_FEW",
        `Votre clan n'a que ${n} inscrit(s) : il en faut au moins ${MIN_CLAN_PLAYERS} pour commencer (sept groupes de 2 à 8 joueurs, un par branche du titan).`,
        { registered: n, min: MIN_CLAN_PLAYERS },
    );

/**
 * §41 (titan 16): the clan's seven groups, formed once (at the first join, view or move of the raid in progress): its
 * roster in the order of registration (raids.ts groupsOf: the 56 first, as even as possible, the raid leader in group
 * 1), group k in an instance of branch k of the titan, each player placed in their group. Fewer than 14 registered:
 * refused (RAID_TOO_FEW). Two players at once form them once (the unique group/branch of an instance; a placement
 * already there is kept). Returns every instance of the clan.
 */
export async function ensureGroups(db: D1Database, eventId: string, clanId: string): Promise<InstanceRow[]> {
    const rows = await clanInstancesOf(db, eventId, clanId);
    if (rows.length) return rows;
    const [entry, roster] = await db.batch([
        db.prepare("SELECT leader_id FROM raid_clan_entries WHERE event_id = ?1 AND clan_id = ?2").bind(eventId, clanId),
        db
            .prepare("SELECT p.id FROM raid_registrations r JOIN players p ON p.id = r.player_id WHERE r.event_id = ?1 AND r.clan_id = ?2 ORDER BY r.created_at, p.username LIMIT 200")
            .bind(eventId, clanId),
    ]);
    const leader = (entry!.results[0] as { leader_id: string | null } | undefined)?.leader_id ?? null;
    const list = roster!.results as { id: string }[];
    if (Math.min(list.length, MAX_IN_PLAY) < MIN_CLAN_PLAYERS) throw TOO_FEW(list.length);
    const groups = groupsOf(list, leader);
    const now = nowIso();
    await db.batch(
        groups.map((g) => {
            const c = CATALOGS[g.branch]!;
            return db
                .prepare(
                    `INSERT OR IGNORE INTO raid_instances (id, event_id, clan_id, group_no, branch, zone, players, created_at, updated_at)
                     VALUES (?1, ?2, ?3, ?4, ?5, ?6, 0, ?7, ?7)`,
                )
                .bind(crypto.randomUUID(), eventId, clanId, g.number, c.id, c.start_zone, now);
        }),
    );
    const seats = JSON.stringify(groups.flatMap((g) => g.members.map((m) => [m.id, g.number, CATALOGS[g.branch]!.id])));
    await db.batch([
        db
            .prepare(
                `INSERT INTO raid_placements (event_id, player_id, clan_id, branch, instance_id, placed_by, reason, created_at, updated_at)
                 SELECT ?1, json_extract(j.value, '$[0]'), ?2, i.branch, i.id, NULL, 'choix', ?4, ?4 FROM json_each(?3) j
                 JOIN raid_instances i ON i.event_id = ?1 AND i.clan_id = ?2 AND i.group_no = json_extract(j.value, '$[1]') AND i.branch = json_extract(j.value, '$[2]')
                 WHERE true ON CONFLICT (event_id, player_id) DO NOTHING`,
            )
            .bind(eventId, clanId, seats, now),
        db
            .prepare(
                `INSERT INTO raid_placement_log (event_id, player_id, from_instance, to_instance, by_player, reason, created_at)
                 SELECT ?1, p.player_id, NULL, p.instance_id, NULL, 'groupe', ?3 FROM raid_placements p
                 WHERE p.event_id = ?1 AND p.clan_id = ?2 AND NOT EXISTS (SELECT 1 FROM raid_placement_log l WHERE l.event_id = ?1 AND l.player_id = p.player_id)`,
            )
            .bind(eventId, clanId, now),
    ]);
    return await clanInstancesOf(db, eventId, clanId);
}

/** §41: the instance of group `group` on `c` (the heart's), made once (the unique group/branch of an instance). */
async function groupInstance(db: D1Database, eventId: string, clanId: string, group: number, c: Catalog): Promise<InstanceRow> {
    const now = nowIso();
    await db
        .prepare(
            `INSERT OR IGNORE INTO raid_instances (id, event_id, clan_id, group_no, branch, zone, players, created_at, updated_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, 0, ?7, ?7)`,
        )
        .bind(crypto.randomUUID(), eventId, clanId, group, c.id, c.start_zone, now)
        .run();
    const inst = await db
        .prepare("SELECT * FROM raid_instances WHERE event_id = ?1 AND clan_id = ?2 AND group_no = ?3 AND branch = ?4")
        .bind(eventId, clanId, group, c.id)
        .first<InstanceRow>();
    if (!inst) throw Errors.conflict("INSTANCE_BUSY", "Beaucoup de joueurs arrivent en même temps : réessayez.");
    return inst;
}

/** §41: a player's place follows their group (its relief, the heart): their placement moved there, journaled. */
export async function follow(db: D1Database, eventId: string, playerId: string, from: string | null, to: InstanceRow, reason: string): Promise<void> {
    const now = nowIso();
    await db.batch([
        db
            .prepare("UPDATE raid_placements SET instance_id = ?3, branch = ?4, updated_at = ?5 WHERE event_id = ?1 AND player_id = ?2")
            .bind(eventId, playerId, to.id, to.branch, now),
        db
            .prepare("INSERT INTO raid_placement_log (event_id, player_id, from_instance, to_instance, by_player, reason, created_at) VALUES (?1, ?2, ?3, ?4, NULL, ?5, ?6)")
            .bind(eventId, playerId, from, to.id, reason, now),
    ]);
}

const nameOf = (branch: string) => catalogById(branch)?.name ?? branch;

/** §41: a registered player's rank in their clan's order of registration (0 first; -1: not registered). */
async function rosterRank(db: D1Database, eventId: string, clanId: string, playerId: string): Promise<number> {
    const rows = (
        await db
            .prepare("SELECT p.id FROM raid_registrations r JOIN players p ON p.id = r.player_id WHERE r.event_id = ?1 AND r.clan_id = ?2 ORDER BY r.created_at, p.username LIMIT 200")
            .bind(eventId, clanId)
            .all<{ id: string }>()
    ).results;
    return rows.findIndex((r) => r.id === playerId);
}

/**
 * §41 (titan 16): a player's place: their group's instance, never another (group k plays branch k; a group that
 * replaced a wiped one, its relief instance; once the seven Gigaboss fell, its own heart instance). `c`: the branch
 * asked (refused when it is not the group's: RAID_OTHER_BRANCH); `wanted`: an instance asked (refused when it is not
 * the group's: RAID_OTHER_GROUP — no help, no other group). A registered player without a place yet (a catch-up
 * session's late registration) joins the smallest group with room; beyond the 56 in play: the waiting list.
 */
export async function groupPlace(db: D1Database, e: { id: string }, clanId: string, me: string, c?: Catalog, wanted?: string): Promise<InstanceRow> {
    let rows = await ensureGroups(db, e.id, clanId);
    let placement = await placementOf(db, e.id, me);
    if (!placement || placement.clan_id !== clanId || !rows.some((i) => i.id === placement!.instance_id)) {
        const order = await rosterRank(db, e.id, clanId, me);
        if (order < 0 || order >= MAX_IN_PLAY) {
            const rank = Math.max(1, order - MAX_IN_PLAY + 1);
            throw new ApiError(
                403,
                "RAID_WAITLIST",
                `Vous êtes sur la liste d'attente de votre clan (rang ${rank}) : seuls les ${MAX_IN_PLAY} premiers inscrits jouent, sept groupes de huit au plus.`,
                { rank },
            );
        }
        const counts = await db
            .prepare("SELECT instance_id, COUNT(*) AS n FROM raid_placements WHERE event_id = ?1 AND clan_id = ?2 GROUP BY instance_id")
            .bind(e.id, clanId)
            .all<{ instance_id: string; n: number }>();
        const n = (id: string) => counts.results.find((r) => r.instance_id === id)?.n ?? 0;
        const open = groupsInPlay(rows).sort((a, b) => n(a.id) - n(b.id) || a.group_no - b.group_no);
        let placed = false;
        for (const g of open)
            if (await putIn(db, e.id, clanId, me, g, null, "choix")) {
                placed = true;
                break;
            }
        if (!placed) throw Errors.conflict("INSTANCE_FULL", "Les sept groupes de votre clan sont complets (8 joueurs chacun).");
        rows = await clanInstancesOf(db, e.id, clanId);
        placement = await placementOf(db, e.id, me);
    }
    const group = rows.find((i) => i.id === placement!.instance_id)?.group_no ?? 0;
    let cur = currentOf(rows, group);
    if (!cur) throw Errors.conflict("INSTANCE_WIPED", "Votre groupe a été éliminé : vous ne pouvez plus que regarder les autres groupes.");
    // 2.74 (phase 6), §41: the Cœur d'Asterion opens once the seven Gigaboss fell (validated): every group still in play
    // fights it, each in its own instance (one health for all, raidHeart.ts).
    const done = await doneOf(db, e.id);
    const heartOpen = !!HEART && heartOpenIn(done);
    if (c && HEART && c.id === HEART.id && !heartOpen)
        throw Errors.conflict("HEART_SEALED", "Le Cœur d'Asterion s'ouvre quand les sept gigaboss sont tombés (" + gigabossDownIn(done) + " / " + CATALOGS.length + ").");
    if (heartOpen && HEART && cur.branch !== HEART.id) cur = await groupInstance(db, e.id, clanId, group, HEART);
    if (c && c.id !== cur.branch)
        throw Errors.conflict(
            "RAID_OTHER_BRANCH",
            `Votre groupe joue la branche ${nameOf(cur.branch)} : chaque groupe joue la sienne, jamais une autre.`,
            { branch: partOf(cur.branch) },
        );
    if (wanted && wanted !== cur.id)
        throw Errors.conflict("RAID_OTHER_GROUP", "Chaque groupe joue sa propre instance : on ne rejoint ni n'aide un autre groupe (il ne vient que remplacer un groupe éliminé).");
    if (placement!.instance_id !== cur.id) await follow(db, e.id, me, placement!.instance_id, cur, "choix");
    return cur;
}

/** The answer of a join: the instance, its branch and the relay's level (made once). */
export async function enter(ctx: Ctx, e: EventRow, inst: InstanceRow, me: string, spectator = false) {
    const c = catalogById(inst.branch);
    if (!c) throw Errors.notFound("INSTANCE_NOT_FOUND", "Ce groupe n'est plus en jeu.");
    await ask(ctx.env, inst.id, "/create", {
        id: inst.id,
        host: me,
        level: { source: "raid", id: levelIdOf(c), hash: levelHashFor(c, e), title: "Asterion · " + c.name },
        raid: { instance_id: inst.id, event_id: e.id, branch: c.id, revision: c.revision },
    });
    const tag = await ctx.env.DB.prepare("SELECT tag FROM clans WHERE id = ?1").bind(inst.clan_id).first<{ tag: string }>();
    return json(ctx, {
        // 2.75 (raidWipe.ts): a spectator (their group wiped) only watches: a socket that never takes a seat.
        spectator,
        instance: {
            start_zone: inst.start_zone ?? 0, // a group taking a wiped zone again starts there (0: the armory, then the start zone)
            wiped_zone: inst.wiped_zone ?? null,
            relieves: inst.relieves ?? null,
            id: inst.id,
            group: inst.group_no,
            clan_tag: tag?.tag ?? "",
            zone: inst.zone,
            players: inst.players,
            branch: c.id,
            part: partOf(c.id),
            revision: c.revision,
            level: levelIdOf(c),
            hash: levelHashFor(c, e),
            difficulty: difficultyOf(e), // titan 13
        },
    });
}

// ------------------------------------------------------------------ routes: joining

/**
 * POST /raids/events/{eventId}/branches/{part}/join {instance_id?} and POST /raids/events/{eventId}/join {}: my place,
 * my group's instance (§41: group k plays branch k; never another branch, never another group's instance: refused
 * with RAID_OTHER_BRANCH / RAID_OTHER_GROUP). A registered player only, while the event runs; the clan's seven groups
 * are formed at the first join (14 registered at least: RAID_TOO_FEW); beyond the 56 in play: RAID_WAITLIST.
 */
export async function join(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const db = ctx.env.DB;
    await rateLimit(ctx, RULES.raidWrite, "player:" + me);
    const e = await eventOf(db, ctx.params.eventId ?? "");
    requireRunning(e, nowIso());
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["instance_id"]);
    const wanted = v.str("instance_id", { min: 36, max: 36, pattern: ID_RE, optional: true });
    v.done();
    const reg = await registrationOf(db, e.id, me);
    if (!reg) throw Errors.forbidden("RAID_NOT_REGISTERED", "Inscrivez-vous au raid pour y combattre.");
    await requireEntry(db, e, me); // 2.69: not while suspended; the late arrivals' rule
    const part = ctx.params.part;
    let c: Catalog | undefined;
    if (part !== undefined) {
        c = catalogByPart(part);
        if (!c) throw Errors.notFound("BRANCH_NOT_FOUND", "Cette branche du titan n'existe pas.");
    }
    // 2.75: a player whose group was wiped only watches from now on (their seat is closed for good).
    const spect = await spectatorOf(db, e.id, me);
    if (spect) return spectatorAnswer(ctx, e, spect, me);
    if (wanted) {
        const inst = await db.prepare("SELECT * FROM raid_instances WHERE id = ?1 AND event_id = ?2").bind(wanted, e.id).first<InstanceRow>();
        if (!inst || (c && inst.branch !== c.id)) throw Errors.notFound("INSTANCE_NOT_FOUND", "Ce groupe n'est plus en jeu.");
        if (inst.wiped_zone != null) throw Errors.conflict("INSTANCE_WIPED", "Ce groupe a été éliminé : sa zone attend un groupe qui la reprenne.");
    }
    const inst = await groupPlace(db, e, reg.clan_id, me, c, wanted);
    return enter(ctx, e, inst, me);
}

// ------------------------------------------------------------------ routes: the clan's placement

/** §41: the seven groups of a clan as the roster screen shows them (number, branch, instance, players, out, waiting). */
function groupViews(rows: InstanceRow[], counts: Map<string, number>, done: Map<string, Set<string>>) {
    const states = statesOf(done);
    const out = [];
    for (let k = 1; k <= RAID_GROUPS; ++k) {
        const first = rows.find((i) => i.group_no === k);
        if (!first) continue;
        const cur = currentOf(rows, k);
        const branch = cur?.branch ?? first.branch;
        const c = catalogById(branch);
        const giga = c?.objectives.find((o) => o.role === "gigaboss");
        out.push({
            group: k,
            branch: partOf(branch),
            branch_name: c?.name ?? "",
            instance_id: cur?.id ?? null,
            placed: cur ? (counts.get(cur.id) ?? 0) : 0,
            out: !cur,
            // its branch's Gigaboss fell: the group waits there (it leaves only to replace a wiped group)
            waiting: !!cur && !!giga && !!c && isDone(states.get(c.id) ?? 0, giga.index) && !(HEART && cur.branch === HEART.id),
            relieves: !!cur?.relieves,
        });
    }
    return out;
}

/**
 * GET /raids/events/{eventId}/placements: my clan's players (registered) and their place (branch, group, zone, in play
 * or not), my clan's seven groups (§41), its waiting list, and whether I may move them.
 */
export async function placements(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const db = ctx.env.DB;
    const e = await eventOf(db, ctx.params.eventId ?? "");
    const reg = await registrationOf(db, e.id, me);
    if (!reg) throw Errors.forbidden("RAID_NOT_REGISTERED", "Inscrivez-vous au raid pour voir la répartition de votre clan.");
    // §41: the seven groups are formed once the raid runs (14 registered at least).
    let tooFew = false;
    if (raidState(e, nowIso()) === "en_cours") {
        try {
            await ensureGroups(db, e.id, reg.clan_id);
        } catch (err) {
            if (!(err instanceof ApiError) || err.code !== "RAID_TOO_FEW") throw err;
            tooFew = true;
        }
    }
    const [members, insts, order] = await db.batch([
        db
            .prepare(
                `SELECT p.id, p.username, pl.instance_id, pl.branch, pl.reason,
                        EXISTS (SELECT 1 FROM raid_instance_members m WHERE m.instance_id = pl.instance_id AND m.player_id = p.id AND m.active = 1 AND m.left_at IS NULL) AS playing
                 FROM raid_registrations r JOIN players p ON p.id = r.player_id
                 LEFT JOIN raid_placements pl ON pl.event_id = r.event_id AND pl.player_id = r.player_id
                 WHERE r.event_id = ?1 AND r.clan_id = ?2 ORDER BY p.username LIMIT 200`,
            )
            .bind(e.id, reg.clan_id),
        db
            .prepare(
                `SELECT i.*, c.tag, (SELECT COUNT(*) FROM raid_placements x WHERE x.instance_id = i.id) AS placed
                 FROM raid_instances i JOIN clans c ON c.id = i.clan_id
                 WHERE i.event_id = ?1 AND i.clan_id = ?2 ORDER BY i.created_at, i.group_no, i.id LIMIT 100`,
            )
            .bind(e.id, reg.clan_id),
        db
            .prepare("SELECT p.id FROM raid_registrations r JOIN players p ON p.id = r.player_id WHERE r.event_id = ?1 AND r.clan_id = ?2 ORDER BY r.created_at, p.username LIMIT 200")
            .bind(e.id, reg.clan_id),
    ]);
    const rows = insts!.results as (InstanceRow & { tag: string; placed: number })[];
    const counts = new Map(rows.map((i) => [i.id, i.placed]));
    const current = new Set(groupsInPlay(rows).map((i) => i.id));
    const instances = rows
        .filter((i) => current.has(i.id))
        .map((i) => ({
            id: i.id,
            branch: partOf(i.branch),
            branch_name: catalogById(i.branch)?.name ?? "",
            clan_tag: i.tag,
            group: i.group_no,
            zone: i.zone,
            zone_name: catalogById(i.branch)?.zones.find((z) => z.id === i.zone)?.name ?? "",
            players: i.players,
            placed: i.placed,
            mine: true,
        }));
    const byId = new Map(rows.map((i) => [i.id, i]));
    const ranks = (order!.results as { id: string }[]).map((r) => r.id);
    return json(ctx, {
        event: { id: e.id, state: raidState(e, nowIso()) },
        clan_tag: reg.tag,
        can_manage: await canManage(db, e.id, reg.clan_id, me),
        branches: CATALOGS.map((c) => ({ part: partOf(c.id), name: c.name })),
        // §41: the rules the screen keeps (7 groups of 2 to 8), whether the clan has its 14, its groups and waiting list.
        rules: { groups: RAID_GROUPS, min_group: MIN_GROUP, max_group: INSTANCE_SIZE, min_players: MIN_CLAN_PLAYERS, max_in_play: MAX_IN_PLAY },
        too_few: tooFew,
        registered: ranks.length,
        groups: groupViews(rows, counts, await doneOf(db, e.id)),
        waitlist: ranks.slice(MAX_IN_PLAY).map((id, k) => ({ id, rank: k + 1 })),
        members: (members!.results as { id: string; username: string; instance_id: string | null; branch: string | null; reason: string | null; playing: number }[]).map((m) => {
            const i = m.instance_id ? byId.get(m.instance_id) : undefined;
            const cur = i ? currentOf(rows, i.group_no) : null;
            const rank = ranks.indexOf(m.id);
            return {
                id: m.id,
                username: m.username,
                me: m.id === me,
                branch: cur ? partOf(cur.branch) : m.branch ? partOf(m.branch) : null,
                instance_id: cur?.id ?? m.instance_id,
                group: i ? `[${i.tag}]·${i.group_no}` : null,
                group_no: i?.group_no ?? 0,
                zone_name: cur ? (catalogById(cur.branch)?.zones.find((z) => z.id === cur.zone)?.name ?? null) : null,
                playing: m.playing === 1,
                reason: m.reason,
                waiting_rank: rank >= MAX_IN_PLAY ? rank - MAX_IN_PLAY + 1 : 0,
            };
        }),
        instances,
    });
}

async function requireManager(ctx: Ctx) {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const db = ctx.env.DB;
    await rateLimit(ctx, RULES.raidWrite, "player:" + me);
    const e = await eventOf(db, ctx.params.eventId ?? "");
    const now = nowIso();
    const st = raidState(e, now);
    if (st === "termine" || st === "annule") throw Errors.conflict("RAID_OVER", "Le raid est terminé.");
    const reg = await registrationOf(db, e.id, me);
    if (!reg || !(await canManage(db, e.id, reg.clan_id, me)))
        throw Errors.forbidden("NOT_RAID_MANAGER", "Seuls le chef, les officiers et le chef de raid du clan répartissent les joueurs.");
    if (st !== "en_cours") throw Errors.conflict("RAID_NOT_RUNNING", "Les groupes se forment au début du raid : la répartition se fait pendant l'événement.");
    return { me, db, e, clanId: reg.clan_id };
}

/**
 * POST /raids/events/{eventId}/placements {player_id, branch?, instance_id?}: the raid leader, the chief or an officer
 * moves a member of the clan to another of its seven groups (the one playing `branch` now, or the instance given).
 * §41: every group keeps 2 to 8 players — one statement checks both (two moves at once never break it). The player's
 * game in progress is not cut: the place counts from their next entry.
 */
export async function move(ctx: Ctx): Promise<Response> {
    const { me, db, e, clanId } = await requireManager(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["player_id", "branch", "instance_id"]);
    const playerId = v.str("player_id", { max: 64, pattern: PLAYER_ID_RE })!;
    const part = v.str("branch", { max: 32, pattern: /^[a-z_]{1,32}$/, optional: true });
    const wanted = v.str("instance_id", { min: 36, max: 36, pattern: ID_RE, optional: true });
    if (part === undefined && wanted === undefined) v.fail("branch", "branche ou instance attendue");
    v.done();
    const c = part !== undefined ? catalogByPart(part) : undefined;
    if (part !== undefined && !c) throw Errors.validation([{ field: "branch", message: "branche inconnue" }]);
    const target = await registrationOf(db, e.id, playerId);
    if (!target || target.clan_id !== clanId) throw Errors.notFound("PLAYER_NOT_FOUND", "Ce joueur n'est pas inscrit au raid avec votre clan.");
    const rows = await ensureGroups(db, e.id, clanId);
    const from = await placementOf(db, e.id, playerId);
    if (!from) throw Errors.conflict("RAID_WAITLIST", "Ce joueur est sur la liste d'attente du clan : il n'a pas de groupe.");
    const groups = groupsInPlay(rows);
    const dest = wanted ? groups.find((i) => i.id === wanted) : groups.find((i) => i.branch === c!.id);
    if (!dest) throw Errors.notFound("INSTANCE_NOT_FOUND", "Aucun groupe de votre clan ne joue là : il y a sept groupes, un par branche.");
    if (from.instance_id === dest.id) return json(ctx, { placement: { player_id: playerId, branch: partOf(dest.branch), instance_id: dest.id, group: dest.group_no } });
    const now = nowIso();
    const r = await db
        .prepare(
            `UPDATE raid_placements SET instance_id = ?3, branch = ?4, placed_by = ?5, reason = 'chef', updated_at = ?6
             WHERE event_id = ?1 AND player_id = ?2 AND instance_id = ?7
               AND (SELECT COUNT(*) FROM raid_placements WHERE instance_id = ?3) < ?8
               AND (SELECT COUNT(*) FROM raid_placements WHERE instance_id = ?7) > ?9`,
        )
        .bind(e.id, playerId, dest.id, dest.branch, me, now, from.instance_id, INSTANCE_SIZE, MIN_GROUP)
        .run();
    if (!r.meta.changes) {
        const n = await db.prepare("SELECT COUNT(*) AS n FROM raid_placements WHERE instance_id = ?1").bind(dest.id).first<{ n: number }>();
        if ((n?.n ?? 0) >= INSTANCE_SIZE) throw Errors.conflict("INSTANCE_FULL", "Ce groupe a déjà 8 joueurs.");
        throw Errors.conflict("GROUP_TOO_SMALL", `Un groupe garde au moins ${MIN_GROUP} joueurs : celui de ce joueur n'en a pas assez pour qu'il parte.`);
    }
    await db
        .prepare("INSERT INTO raid_placement_log (event_id, player_id, from_instance, to_instance, by_player, reason, created_at) VALUES (?1, ?2, ?3, ?4, ?5, 'chef', ?6)")
        .bind(e.id, playerId, from.instance_id, dest.id, me, now)
        .run();
    return json(ctx, { placement: { player_id: playerId, branch: partOf(dest.branch), instance_id: dest.id, group: dest.group_no } });
}

/**
 * POST /raids/events/{eventId}/placements/rebalance {branch?}: §41, the clan's players spread evenly over its groups
 * still in play (sizes differ by one at most; `branch` is no longer needed: every group at once). The fullest groups
 * give players to the emptiest; players not in play move first, those in play keep their game and change at their next
 * entry.
 */
export async function rebalance(ctx: Ctx): Promise<Response> {
    const { me, db, e, clanId } = await requireManager(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["branch"]);
    v.str("branch", { max: 32, pattern: /^[a-z_]{1,32}$/, optional: true });
    v.done();
    const groups = groupsInPlay(await ensureGroups(db, e.id, clanId));
    const ids = new Set(groups.map((g) => g.id));
    const rows = (
        await db
            .prepare(
                `SELECT pl.player_id, pl.instance_id,
                        EXISTS (SELECT 1 FROM raid_instance_members m WHERE m.instance_id = pl.instance_id AND m.player_id = pl.player_id AND m.active = 1 AND m.left_at IS NULL) AS playing
                 FROM raid_placements pl WHERE pl.event_id = ?1 AND pl.clan_id = ?2 ORDER BY pl.updated_at DESC, pl.player_id`,
            )
            .bind(e.id, clanId)
            .all<{ player_id: string; instance_id: string; playing: number }>()
    ).results.filter((r) => ids.has(r.instance_id));
    const n = rows.length;
    const k = Math.max(1, groups.length);
    const size = (id: string) => rows.filter((r) => r.instance_id === id).length;
    // The fullest groups keep the larger shares (fewer moves), the lowest numbers on a tie.
    const byFill = [...groups].sort((a, b) => size(b.id) - size(a.id) || a.group_no - b.group_no);
    const target = new Map(byFill.map((g, i) => [g.id, Math.floor(n / k) + (i < n % k ? 1 : 0)]));
    // Who moves: the extra players of a group above its share (not in play first).
    const movers = [...rows].sort((a, b) => a.playing - b.playing);
    const leaving = new Map<string, number>();
    const chosen: typeof rows = [];
    for (const r of movers) {
        const extra = size(r.instance_id) - target.get(r.instance_id)! - (leaving.get(r.instance_id) ?? 0);
        if (extra <= 0) continue;
        leaving.set(r.instance_id, (leaving.get(r.instance_id) ?? 0) + 1);
        chosen.push(r);
    }
    let moved = 0;
    for (const r of chosen) {
        // The group furthest below its share takes them.
        const dest = [...groups].sort((a, b) => size(a.id) - target.get(a.id)! - (size(b.id) - target.get(b.id)!) || a.group_no - b.group_no)[0]!;
        if (size(dest.id) >= target.get(dest.id)!) break;
        if (await putIn(db, e.id, clanId, r.player_id, dest, me, "equilibrage")) {
            r.instance_id = dest.id;
            ++moved;
        }
    }
    return json(ctx, {
        moved,
        instances: groups.map((i) => ({ id: i.id, group: i.group_no, branch: partOf(i.branch), placed: size(i.id) })),
    });
}

// ------------------------------------------------------------------ routes: the maps

interface ObjRow {
    branch: string;
    objective_id: string;
    accepted_at: string;
    group_no: number | null;
    clan_id: string | null;
    tag: string | null;
    clan_name: string | null;
}
interface InstRow {
    id: string;
    branch: string;
    group_no: number;
    zone: number;
    players: number;
    clan_id: string;
    tag: string;
    placed: number;
    wiped_zone: number | null;  // 2.75 (raidWipe.ts)
    wiped_at: string | null;
    start_zone: number | null;
    relieves: string | null;
    seat_zones: string | null;  // 2.78: the zone of each seat
    started_at: string | null;  // §41: the seven groups are made at the start: a branch is « inconnu » until its group plays
}

async function snapshot(db: D1Database, eventId: string) {
    const [objR, instR, zoneR] = await db.batch([
        db
            .prepare(
                `SELECT o.branch, o.objective_id, o.accepted_at, i.group_no, i.clan_id, c.tag, c.name AS clan_name FROM raid_branch_objectives o
                 LEFT JOIN raid_instances i ON i.id = o.instance_id LEFT JOIN clans c ON c.id = i.clan_id
                 WHERE o.event_id = ?1 ORDER BY o.accepted_at DESC, o.objective_id`,
            )
            .bind(eventId),
        db
            .prepare(
                `SELECT i.id, i.branch, i.group_no, i.zone, i.players, i.clan_id, c.tag, i.wiped_zone, i.wiped_at, i.start_zone, i.relieves, i.seat_zones, i.started_at, (SELECT COUNT(*) FROM raid_placements p WHERE p.instance_id = i.id) AS placed
                 FROM raid_instances i JOIN clans c ON c.id = i.clan_id WHERE i.event_id = ?1 ORDER BY c.tag, i.branch, i.group_no LIMIT 500`,
            )
            .bind(eventId),
        // 2.75: the zones finished (once each, raid_zone_completions) and by which group.
        db
            .prepare(
                `SELECT z.branch, z.zone, z.completed_at, i.group_no, c.tag, i.relieves FROM raid_zone_completions z
                 LEFT JOIN raid_instances i ON i.id = z.instance_id LEFT JOIN clans c ON c.id = i.clan_id
                 WHERE z.event_id = ?1 ORDER BY z.completed_at DESC LIMIT 200`,
            )
            .bind(eventId),
    ]);
    const finished = zoneR!.results as { branch: string; zone: number; completed_at: string; group_no: number | null; tag: string | null; relieves: string | null }[];
    const objectives = objR!.results as ObjRow[];
    const instances = instR!.results as InstRow[];
    const done = new Map<string, Set<string>>();
    for (const o of objectives) {
        if (!done.has(o.branch)) done.set(o.branch, new Set());
        done.get(o.branch)!.add(o.objective_id);
    }
    return { objectives, instances, done, states: statesOf(done), finished };
}

const groupTag = (tag: string | null, group: number | null) => (tag ? `[${tag}]·${group ?? 1}` : null);

/** The links of the titan as the maps show them: on or not, how far, from where to where. */
function linkViews(done: Map<string, Set<string>>, objectives: ObjRow[]) {
    return TITAN.links.map((l) => {
        const got = l.from.filter((f) => done.get(f.branch)?.has(f.objective_id)).length;
        const on = got === l.from.length;
        const at = on
            ? objectives
                  .filter((o) => l.from.some((f) => f.branch === o.branch && f.objective_id === o.objective_id))
                  .map((o) => o.accepted_at)
                  .sort()
                  .at(-1) ?? null
            : null;
        return {
            id: l.id,
            label: l.label,
            kind: l.kind,
            on,
            progress: { done: got, needed: l.from.length },
            from: [...new Set(l.from.map((f) => partOf(f.branch)))],
            sources: l.from.map((f) => ({ branch: partOf(f.branch), objective: f.objective_id, done: !!done.get(f.branch)?.has(f.objective_id) })),
            to: partOf(l.to),
            to_name: catalogById(l.to)?.name ?? "",
            zones: l.zones.map((z) => catalogById(l.to)?.zones.find((x) => x.id === z)?.name ?? String(z)),
            at,
        };
    });
}

/** Whether a branch's Gigaboss fell (validated) in these states. */
export function gigabossFallen(c: Catalog, states: Map<string, number>): boolean {
    const giga = c.objectives.find((o) => o.role === "gigaboss");
    return !!giga && isDone(states.get(c.id) ?? 0, giga.index);
}

export type RaidOutcome = "victoire" | "defaite_temps" | "defaite_groupes" | "en_cours";

/**
 * §41 (titan 16): how the raid ends for a clan. « Victoire » once the Cœur d'Asterion fell (validated). « Défaite :
 * temps écoulé » at the end of the event (60 min for the standard raid) with the Cœur standing — some Gigaboss still
 * up, or the Cœur itself. « Défaite : tous les groupes éliminés » at once when every group of the clan is out (each one
 * wiped: nobody is left to replace it). Else « en cours ». `rows`: the clan's instances (null: no clan, the time only).
 */
export function outcomeOf(e: { ends_at: string }, now: string, done: Map<string, Set<string>>, rows: InstanceRow[] | null): { id: RaidOutcome; text: string } {
    if (HEART && done.get(HEART.id)?.has(HEART_OBJECTIVE)) return { id: "victoire", text: "Victoire : Asterion est tombé" };
    if (rows && rows.length && groupsInPlay(rows).length === 0) return { id: "defaite_groupes", text: "Défaite : tous les groupes éliminés" };
    if (now >= e.ends_at) return { id: "defaite_temps", text: "Défaite : temps écoulé" };
    return { id: "en_cours", text: "En cours" };
}

/**
 * GET /raids/events/{eventId}/titan: the global map (maquette « Carte du titan »): each branch
 * (state, zones controlled, objectives, groups and players there, my clan's groups, its gigaboss
 * still sealed, the zones that ask for another group), the titan's body states, the links on and
 * to come, the totals, the clans by objectives accepted, the latest events, my place.
 */
export async function titanView(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const db = ctx.env.DB;
    const now = nowIso();
    const e = await eventOf(db, ctx.params.eventId ?? "");
    const reg = await requireViewer(db, e, me);
    const { objectives, instances, done, states, finished } = await snapshot(db, e.id);
    const placement = await placementOf(db, e.id, me);
    const body: Record<string, string> = {};
    let zonesControlled = 0;
    let gigabossDefeated = 0;
    // §41 (titan 16): no zone asks for another group any more (a pair is done by one group, split in two): always empty.
    const needs: { branch: string; zone: number; name: string; groups: number; required: number }[] = [];
    const branches = CATALOGS.map((c) => {
        const mask = states.get(c.id) ?? 0;
        const reach = reachableIn(c, mask);
        // §41: the groups exist from the start (seven instances): a branch is known once one of them played there.
        const here = instances.filter((i) => i.branch === c.id && (i.players > 0 || !!i.started_at));
        const fighting = here.filter((i) => i.players > 0);
        const controlled = c.zones.filter((z) => controlledIn(c, z.id, mask)).length;
        zonesControlled += controlled;
        const own = done.get(c.id)?.size ?? 0;
        // How the titan's part looks: unknown until a group goes there, attacked while groups fight
        // there, weakened once half of its zones are taken, else intact.
        // 2.68 (phase 3): a branch whose gigaboss is fought (an objective of role « gigaboss »: the
        // Bras droit's Pyroclast) is destroyed once its fall is accepted; the others stay sealed.
        const giga = c.objectives.find((o) => o.role === "gigaboss");
        const fallen = !!giga && isDone(mask, giga.index);
        if (fallen) gigabossDefeated++;
        const state = fallen
            ? "detruit"
            : here.length === 0 && own === 0
              ? "inconnu"
              : fighting.length > 0
                ? "attaque"
                : controlled >= 3
                  ? "affaibli"
                  : "intact";
        body[partOf(c.id)] = state;
        return {
            part: partOf(c.id),
            id: c.id,
            name: c.name,
            gigaboss: c.gigaboss,
            sealed: !giga,
            gigaboss_down: fallen,
            state,
            zones: c.zones.length,
            zones_controlled: controlled,
            zones_reachable: reach.filter(Boolean).length,
            objectives_done: c.objectives.filter((o) => isDone(mask, o.index)).length,
            objectives_total: c.objectives.length,
            groups: fighting.length,
            players: fighting.reduce((n, i) => n + i.players, 0),
            my_groups: reg ? fighting.filter((i) => i.clan_id === reg.clan_id).map((i) => groupTag(i.tag, i.group_no)) : [],
            links_in: linksTo(c).filter((l) => (mask >>> l.bit) & 1).length,
        };
    });
    const links = linkViews(done, objectives);
    // The clans by objectives accepted (their groups did them first).
    const perClan = new Map<string, { tag: string; name: string; objectives: number; mine: boolean }>();
    for (const o of objectives) {
        if (!o.clan_id || !o.tag) continue;
        const x = perClan.get(o.clan_id) ?? { tag: o.tag, name: o.clan_name ?? "", objectives: 0, mine: !!reg && reg.clan_id === o.clan_id };
        x.objectives++;
        perClan.set(o.clan_id, x);
    }
    const clans = [...perClan.values()].sort((a, b) => b.objectives - a.objectives || a.tag.localeCompare(b.tag)).slice(0, 5);
    // The latest events of the whole titan: objectives accepted, links on.
    const feed = [
        ...objectives.slice(0, 12).map((o) => {
            const c = catalogById(o.branch);
            return { at: o.accepted_at, kind: "objective", branch: partOf(o.branch), branch_name: c?.name ?? "", by: groupTag(o.tag, o.group_no), text: objectiveIn(c ?? CATALOGS[0]!, o.objective_id)?.label ?? o.objective_id };
        }),
        ...links.filter((l) => l.on && l.at).map((l) => ({ at: l.at!, kind: "link", branch: l.to, branch_name: l.to_name, by: null, text: l.label })),
        // 2.75: a zone finished (told once: raid_zone_completions).
        ...finished.slice(0, 12).map((z) => {
            const c = catalogById(z.branch);
            const name = c?.zones.find((x) => x.id === z.zone)?.name ?? String(z.zone);
            // 2.75.1: finished by a group that took it back after another one was wiped there.
            const text = z.relieves ? `Zone reprise par un groupe de renfort : ${name}` : `Zone terminée : ${name}`; // 2.76.2: models the game translates
            return { at: z.completed_at, kind: z.relieves ? "retake" : "zone", branch: partOf(z.branch), branch_name: c?.name ?? "", by: groupTag(z.tag, z.group_no), text, zone: z.zone };
        }),
        // 2.75.1 (raidWipe.ts): a group wiped (every player down at once): its zone to take back.
        ...instances
            .filter((i) => i.wiped_zone != null && i.wiped_at)
            .slice(0, 12)
            .map((i) => {
                const c = catalogById(i.branch);
                const name = c?.zones.find((x) => x.id === i.wiped_zone)?.name ?? String(i.wiped_zone);
                return { at: i.wiped_at!, kind: "wipe", branch: partOf(i.branch), branch_name: c?.name ?? "", by: groupTag(i.tag, i.group_no), text: `Groupe éliminé : ${name} (zone à reprendre)`, zone: i.wiped_zone! };
            }),
    ]
        .sort((a, b) => (a.at < b.at ? 1 : a.at > b.at ? -1 : 0))
        .slice(0, 12);
    const fighting = instances.filter((i) => i.players > 0);
    const myInst = placement ? instances.find((i) => i.id === placement.instance_id) : undefined;
    // 2.74 (phase 6): the Cœur d'Asterion: sealed, open, fought (its shared health), fallen.
    const heart = await heartView(db, e.id, done, states, instances, reg?.clan_id ?? null);
    if (heart) body[heart.part] = heartBodyState(heart);
    // §41: my clan's groups (seven, one per branch) and how the raid ends for it.
    const mine = reg ? await clanInstancesOf(db, e.id, reg.clan_id) : [];
    const myGroupNo = myInst?.group_no ?? 0;
    const myCur = myGroupNo ? currentOf(mine, myGroupNo) : null;
    return json(ctx, {
        server_time: now,
        event: { id: e.id, state: raidState(e, now), kind: e.kind, ends_at: e.ends_at },
        outcome: outcomeOf(e, now, done, reg ? mine : null),
        titan: { revision: TITAN.revision },
        totals: {
            zones_controlled: zonesControlled,
            zones: CATALOGS.reduce((n, c) => n + c.zones.length, 0),
            objectives_done: branches.reduce((n, b) => n + b.objectives_done, 0),
            objectives: branches.reduce((n, b) => n + b.objectives_total, 0),
            groups: fighting.length,
            players: fighting.reduce((n, i) => n + i.players, 0),
            clans: new Set(fighting.map((i) => i.clan_id)).size,
            gigaboss_defeated: gigabossDefeated,
            gigaboss: CATALOGS.length,
            links_on: links.filter((l) => l.on).length,
            links: links.length,
        },
        branches,
        heart,
        body,
        links,
        needs: needs.slice(0, 6),
        clans,
        feed,
        my: {
            registered: !!reg,
            clan_tag: reg?.tag ?? null,
            can_manage: reg ? await canManage(db, e.id, reg.clan_id, me) : false,
            placement: myInst ? { branch: partOf(myInst.branch), instance_id: myInst.id, group: groupTag(myInst.tag, myInst.group_no), zone: myInst.zone } : null,
            // §41: my group now (its branch, waiting there once its Gigaboss fell, out once wiped), my clan's groups in play.
            group: myGroupNo
                ? {
                      number: myGroupNo,
                      branch: myCur ? partOf(myCur.branch) : null,
                      instance_id: myCur?.id ?? null,
                      out: !myCur,
                      waiting: !!myCur && !!catalogById(myCur.branch) && !(HEART && myCur.branch === HEART.id) && gigabossFallen(catalogById(myCur.branch)!, states),
                  }
                : null,
            groups_in_play: reg ? groupsInPlay(mine).length : 0,
        },
    });
}

/**
 * GET /raids/events/{eventId}/branches/{part}: a branch's map (maquette « Branche »), as the phase-2
 * map of the BRAS DROIT (zones, passages, groups, objectives and by whom) for any branch, with
 * the links of the titan that act on it (« reçus ») and those it gives (« donnés »).
 */
export async function branchView(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const db = ctx.env.DB;
    const now = nowIso();
    const c = catalogByPart(ctx.params.part ?? "");
    if (!c) throw Errors.notFound("BRANCH_NOT_FOUND", "Cette branche du titan n'existe pas.");
    const e = await eventOf(db, ctx.params.eventId ?? "");
    const reg = await requireViewer(db, e, me);
    const { objectives, instances: all, done, states, finished } = await snapshot(db, e.id);
    const mask = states.get(c.id) ?? 0;
    const finishedBy = new Map(finished.filter((z) => z.branch === c.id).map((z) => [z.zone, { by: groupTag(z.tag, z.group_no), at: z.completed_at }]));
    const reach = reachableIn(c, mask);
    const mineDone = objectives.filter((o) => o.branch === c.id);
    const by = new Map(mineDone.map((d) => [d.objective_id, groupTag(d.tag, d.group_no)]));
    const instances = all
        .filter((i) => i.branch === c.id)
        .slice(0, 100)
        .map((i) => ({
            id: i.id,
            clan_tag: i.tag,
            group: i.group_no,
            zone: i.zone,
            // 2.78: each player changes zone alone: the zones of the group, and how many players in each.
            zones: zonesOf(i),
            seat_zones: seatZonesOf(i),
            players: i.players,
            placed: i.placed,
            free: Math.max(0, INSTANCE_SIZE - Math.max(i.players, i.placed)),
            mine: !!reg && i.clan_id === reg.clan_id,
            members: [] as { name: string; seat: number; zone: number }[],
            wiped_zone: i.wiped_zone ?? null,  // 2.75: the group was wiped there (spectators)
            start_zone: i.start_zone ?? null,
            relieves: i.relieves ?? null,
        }));
    const placement = await placementOf(db, e.id, me);
    let mine = placement && placement.branch === c.id ? instances.find((i) => i.id === placement.instance_id) : undefined;
    if (!mine) {
        // 2.75: the instance I play in without a placement (my group's, joined directly: raidZones.joinInstance).
        const seat = await db
            .prepare(
                `SELECT m.instance_id FROM raid_instance_members m JOIN raid_instances i ON i.id = m.instance_id
                 WHERE i.event_id = ?1 AND i.branch = ?2 AND m.player_id = ?3 AND m.left_at IS NULL AND i.wiped_zone IS NULL ORDER BY m.joined_at DESC LIMIT 1`,
            )
            .bind(e.id, c.id, me)
            .first<{ instance_id: string }>();
        if (seat) mine = instances.find((i) => i.id === seat.instance_id);
    }
    // 2.78: who is in which zone, for the groups of my clan (their names: another clan's group stays private).
    const ownIds = instances.filter((i) => i.mine).map((i) => i.id);
    if (ownIds.length) {
        const rows = (
            await db
                .prepare(
                    `SELECT m.instance_id, m.seat, m.player_id, p.username FROM raid_instance_members m JOIN players p ON p.id = m.player_id
                     WHERE m.instance_id IN (SELECT value FROM json_each(?1)) AND m.left_at IS NULL ORDER BY m.instance_id, m.seat LIMIT 400`,
                )
                .bind(JSON.stringify(ownIds))
                .all<{ instance_id: string; seat: number; player_id: string; username: string }>()
        ).results;
        for (const r of rows) {
            const i = instances.find((x) => x.id === r.instance_id);
            if (!i) continue;
            const z = i.seat_zones.length ? (i.seat_zones[r.seat] ?? 0) : i.zone;
            i.members.push({ name: r.username, seat: r.seat, zone: z });
        }
    }
    // 2.78: my own zone (my seat's), for « my zone finished » (the retakes).
    let myZone = mine?.zone ?? 0;
    if (mine && mine.seat_zones.length) {
        const own = await db
            .prepare("SELECT seat FROM raid_instance_members WHERE instance_id = ?1 AND player_id = ?2 AND left_at IS NULL")
            .bind(mine.id, me)
            .first<{ seat: number }>();
        if (own && (mine.seat_zones[own.seat] ?? 0) > 0) myZone = mine.seat_zones[own.seat]!;
    }
    // 2.75 (raidWipe.ts): the zones to take again, whether I only watch, and what my group may take again.
    const retakes = await zonesToRetake(db, e.id, c, mask);
    const spect = await spectatorOf(db, e.id, me);
    const myZoneDone = !!mine && mine.wiped_zone == null && zoneFinished(c, myZone, mask);
    // §41 (titan 16): only a group WAITING (its branch's Gigaboss fell, the Cœur not open yet) replaces a wiped group of
    // its clan, in any branch where the clan has no group in play any more: it takes that zone, the branch's state kept.
    const clanRows = reg ? await clanInstancesOf(db, e.id, reg.clan_id) : [];
    const myGroupNo = clanRows.find((i) => i.id === placement?.instance_id)?.group_no ?? 0;
    const myCur = myGroupNo ? currentOf(clanRows, myGroupNo) : null;
    const myCat = myCur ? catalogById(myCur.branch) : undefined;
    const myWaiting = !!myCur && !!myCat && !(HEART && myCur.branch === HEART.id) && gigabossFallen(myCat, states) && !heartOpenIn(done);
    const clanHere = groupsInPlay(clanRows).some((i) => i.branch === c.id);
    const ownRetakes = retakes.filter((r) => !r.relieved_by && clanRows.some((i) => i.id === r.instance_id));
    const links = linkViews(done, objectives);
    return json(ctx, {
        server_time: now,
        event: { id: e.id, state: raidState(e, now) },
        branch: { id: c.id, part: partOf(c.id), name: c.name, gigaboss: c.gigaboss, revision: c.revision },
        mask,
        generators: { done: generatorsOf(c, mask), needed: c.generators_needed },
        shortcut: lockOpenIn(c, "shortcut", mask),
        lock_texts: c.lock_texts ?? {},
        zones: c.zones.map((z) => ({
            ...z,
            reachable: !!reach[z.id],
            controlled: controlledIn(c, z.id, mask),
            // 2.75: finished (every required objective accepted), when and by which group.
            finished: zoneFinishedIn(c, z.id, mask),
            finished_by: finishedBy.get(z.id)?.by ?? null,
            finished_at: finishedBy.get(z.id)?.at ?? null,
            groups: instances.filter((i) => i.zones.includes(z.id) && i.players > 0 && i.wiped_zone == null).length,
            // 2.78: the players of my clan's groups in this zone (names), each group's players spread on its zones.
            players_here: instances.filter((i) => i.mine && i.wiped_zone == null).flatMap((i) => i.members.filter((m) => m.zone === z.id).map((m) => m.name)),
            // 2.75 (raidWipe.ts): « zone à reprendre »: the group wiped there (shown by the game's panel and map).
            to_retake: retakes.some((r) => r.zone === z.id),
            retake_by: (() => {
                const r = retakes.find((x) => x.zone === z.id);
                return r ? groupTag(r.clan_tag, r.group) : null;
            })(),
            objectives: c.objectives
                .filter((o) => o.zone === z.id)
                .map((o) => ({ id: o.id, label: o.label, optional: o.optional, done: isDone(mask, o.index), by: by.get(o.id) ?? null })),
            effects: linksTo(c)
                .filter((l) => l.zones.includes(z.id))
                .map((l) => ({ id: l.id, label: l.label, kind: l.kind, on: isDone(mask, l.bit) })),
        })),
        edges: c.edges.map((x) => ({ ...x, open: lockOpenIn(c, x.lock, mask) })),
        effects_in: links.filter((l) => l.to === partOf(c.id)),
        effects_out: links.filter((l) => l.from.includes(partOf(c.id))),
        instances,
        my: {
            registered: !!reg,
            clan_tag: reg?.tag ?? null,
            instance_id: mine?.id ?? null,
            zone: myZone,  // 2.78: the zone of my seat
            placed_branch: placement ? partOf(placement.branch) : null,
            spectator: spect ? { instance_id: spect.instance_id, zone: spect.zone, part: partOf(spect.branch) } : null,
            zone_finished: myZoneDone,
            retake_offers: myWaiting && !spect && !clanHere ? ownRetakes.map((r) => ({ zone: r.zone, name: c.zones.find((z) => z.id === r.zone)?.name ?? "" })) : [],
            // §41: my group's instance now (any branch: the retake is asked from it) and whether it waits.
            group: myGroupNo,
            group_instance_id: myCur?.id ?? null,
            group_branch: myCur ? partOf(myCur.branch) : null,
            group_waiting: myWaiting,
        },
        to_retake: retakes,
        journal: mineDone.slice(0, 10).map((d) => ({ objective: d.objective_id, label: objectiveIn(c, d.objective_id)?.label ?? d.objective_id, by: by.get(d.objective_id) ?? null, at: d.accepted_at })),
    });
}
