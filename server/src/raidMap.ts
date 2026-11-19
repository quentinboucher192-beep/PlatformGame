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
// * Placement (spec §8): 8 players at most per instance (a group of a clan on a branch), up to 50
//   per clan. A player picks a branch (or lets the server choose where the clan needs them); the
//   server places them in their clan's instance of that branch that has room (their own group of
//   the roster first, then the groups already fighting), opens a new instance when all are full,
//   and never places a ninth (one conditional statement: concurrent placements cannot overfill).
//   The raid leader, the chief and the officers move a member or rebalance a branch. Every move
//   is journaled (raid_placement_log, migration 0144).

import { requireAuth } from "./auth";
import { ask } from "./coop";
import { Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import brasDroitJson from "./raidBranches.json";
import titanJson from "./raidTitan.json";
import { raidState } from "./raids";
import { requireEntry } from "./raidWorld";
import { HEART_OBJECTIVE, gigabossDownIn, heartBodyState, heartOpenIn, heartView } from "./raidHeart";
import { RULES, rateLimit } from "./ratelimit";
import { Validator } from "./validate";

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
/** Instances of one clan on one branch (50 players fit in 7). */
const MAX_CLAN_INSTANCES = 16;
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
    return false;
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

/** My group of 8 in my clan's roster (raids.ts groupsOf: the raid leader leads group 1). */
async function rosterGroup(db: D1Database, eventId: string, clanId: string, playerId: string): Promise<number> {
    const [entry, roster] = await db.batch([
        db.prepare("SELECT leader_id FROM raid_clan_entries WHERE event_id = ?1 AND clan_id = ?2").bind(eventId, clanId),
        db.prepare("SELECT p.id FROM raid_registrations r JOIN players p ON p.id = r.player_id WHERE r.event_id = ?1 AND r.clan_id = ?2 ORDER BY r.created_at, p.username").bind(eventId, clanId),
    ]);
    const leader = (entry!.results[0] as { leader_id: string | null } | undefined)?.leader_id ?? null;
    const ids = (roster!.results as { id: string }[]).map((r) => r.id);
    const li = leader ? ids.indexOf(leader) : -1;
    if (li > 0) ids.unshift(...ids.splice(li, 1));
    const at = ids.indexOf(playerId);
    return at < 0 ? 0 : Math.floor(at / INSTANCE_SIZE) + 1;
}

// ------------------------------------------------------------------ placement

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
async function putIn(db: D1Database, eventId: string, clanId: string, playerId: string, inst: InstanceRow, by: string, reason: string): Promise<boolean> {
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

async function placedCounts(db: D1Database, ids: string[]): Promise<Map<string, number>> {
    const out = new Map<string, number>();
    if (!ids.length) return out;
    const rows = await db
        .prepare(`SELECT instance_id, COUNT(*) AS n FROM raid_placements WHERE instance_id IN (${ids.map((_, i) => "?" + (i + 1)).join(", ")}) GROUP BY instance_id`)
        .bind(...ids)
        .all<{ instance_id: string; n: number }>();
    for (const r of rows.results) out.set(r.instance_id, r.n);
    return out;
}

async function clanInstances(db: D1Database, eventId: string, clanId: string, branch: string): Promise<InstanceRow[]> {
    return (
        await db
            .prepare("SELECT * FROM raid_instances WHERE event_id = ?1 AND clan_id = ?2 AND branch = ?3 ORDER BY group_no")
            .bind(eventId, clanId, branch)
            .all<InstanceRow>()
    ).results;
}

/**
 * A new instance of the clan on the branch (the next group number). `whenFull`: only when every
 * instance of the clan there has its 8 players placed (one statement: many players arriving at
 * once open one instance, not one each); null when another one has room.
 */
async function openInstance(db: D1Database, eventId: string, clanId: string, c: Catalog, whenFull = false): Promise<InstanceRow | null> {
    const now = nowIso();
    const id = crypto.randomUUID();
    const full = whenFull
        ? ` AND NOT EXISTS (SELECT 1 FROM raid_instances j WHERE j.event_id = ?2 AND j.clan_id = ?3 AND j.branch = ?4
                AND (SELECT COUNT(*) FROM raid_placements p WHERE p.instance_id = j.id) < ${INSTANCE_SIZE})`
        : "";
    await db
        .prepare(
            `INSERT OR IGNORE INTO raid_instances (id, event_id, clan_id, group_no, branch, zone, players, created_at, updated_at)
             SELECT ?1, ?2, ?3, COALESCE(MAX(group_no), 0) + 1, ?4, ?5, 0, ?6, ?6 FROM raid_instances WHERE event_id = ?2 AND clan_id = ?3 AND branch = ?4${full}`,
        )
        .bind(id, eventId, clanId, c.id, c.start_zone, now)
        .run();
    return await db.prepare("SELECT * FROM raid_instances WHERE id = ?1").bind(id).first<InstanceRow>();
}

/**
 * Places a player of a clan on a branch: their own instance there when they have one; else their
 * roster group's instance with room, then the clan's instances already fighting (the fewest
 * players first: they need them), then any with room; else a new instance (once all are full).
 */
export async function placeOnBranch(db: D1Database, eventId: string, clanId: string, playerId: string, c: Catalog, by: string, reason: string): Promise<InstanceRow> {
    const current = await placementOf(db, eventId, playerId);
    if (current && current.branch === c.id) {
        const inst = await db.prepare("SELECT * FROM raid_instances WHERE id = ?1").bind(current.instance_id).first<InstanceRow>();
        if (inst) return inst;
    }
    const group = await rosterGroup(db, eventId, clanId, playerId);
    // Others may be placed at the same time: read again and retry until a place is taken.
    for (let attempt = 0; attempt < 12; ++attempt) {
        const mine = await clanInstances(db, eventId, clanId, c.id);
        const counts = await placedCounts(
            db,
            mine.map((i) => i.id),
        );
        const room = (i: InstanceRow) => (counts.get(i.id) ?? 0) < INSTANCE_SIZE;
        const order = [
            ...mine.filter((i) => i.group_no === group && room(i)),
            ...mine.filter((i) => i.players > 0 && room(i)).sort((a, b) => (counts.get(a.id) ?? 0) - (counts.get(b.id) ?? 0) || a.group_no - b.group_no),
            ...mine.filter(room),
        ];
        const tried = new Set<string>();
        for (const inst of order) {
            if (tried.has(inst.id)) continue;
            tried.add(inst.id);
            if (await putIn(db, eventId, clanId, playerId, inst, by, reason)) return inst;
        }
        if (mine.length >= MAX_CLAN_INSTANCES) throw Errors.conflict("BRANCH_FULL", "Votre clan a déjà toutes ses instances pleines sur cette branche.");
        const opened = await openInstance(db, eventId, clanId, c, true);
        if (opened && (await putIn(db, eventId, clanId, playerId, opened, by, reason))) return opened;
    }
    throw Errors.conflict("INSTANCE_BUSY", "Beaucoup de joueurs arrivent en même temps : réessayez.");
}

/**
 * The branch where the clan needs a player who did not choose: one of the clan's instances in
 * combat that is not full (the fewest players first); else the branch where the clan has the
 * fewest players placed (the titan's order on a tie).
 */
async function neededBranch(db: D1Database, eventId: string, clanId: string): Promise<Catalog> {
    // 2.74 (phase 6): once the seven gigaboss fell, every group is needed at the heart (until it falls).
    if (HEART) {
        const done = await doneOf(db, eventId);
        if (heartOpenIn(done) && !done.get(HEART.id)?.has(HEART_OBJECTIVE)) return HEART;
    }
    const [inst, placed] = await db.batch([
        db.prepare("SELECT id, branch, players FROM raid_instances WHERE event_id = ?1 AND clan_id = ?2").bind(eventId, clanId),
        db.prepare("SELECT branch, instance_id FROM raid_placements WHERE event_id = ?1 AND clan_id = ?2").bind(eventId, clanId),
    ]);
    const rows = inst!.results as { id: string; branch: string; players: number }[];
    const places = placed!.results as { branch: string; instance_id: string }[];
    const count = (id: string) => places.filter((p) => p.instance_id === id).length;
    const fighting = rows.filter((r) => r.players > 0 && count(r.id) < INSTANCE_SIZE).sort((a, b) => count(a.id) - count(b.id));
    const found = fighting.map((r) => catalogById(r.branch)).find((c) => !!c);
    if (found) return found;
    const per = (c: Catalog) => places.filter((p) => p.branch === c.id).length;
    return [...CATALOGS].sort((a, b) => per(a) - per(b) || CATALOGS.indexOf(a) - CATALOGS.indexOf(b))[0]!;
}

/** The answer of a join: the instance, its branch and the relay's level (made once). */
async function enter(ctx: Ctx, e: EventRow, inst: InstanceRow, me: string) {
    const c = catalogById(inst.branch);
    if (!c) throw Errors.notFound("INSTANCE_NOT_FOUND", "Ce groupe n'est plus en jeu.");
    await ask(ctx.env, inst.id, "/create", {
        id: inst.id,
        host: me,
        level: { source: "raid", id: levelIdOf(c), hash: levelHashOf(c), title: "Asterion · " + c.name },
        raid: { instance_id: inst.id, event_id: e.id, branch: c.id, revision: c.revision },
    });
    const tag = await ctx.env.DB.prepare("SELECT tag FROM clans WHERE id = ?1").bind(inst.clan_id).first<{ tag: string }>();
    return json(ctx, {
        instance: {
            id: inst.id,
            group: inst.group_no,
            clan_tag: tag?.tag ?? "",
            zone: inst.zone,
            players: inst.players,
            branch: c.id,
            part: partOf(c.id),
            revision: c.revision,
            level: levelIdOf(c),
            hash: levelHashOf(c),
        },
    });
}

// ------------------------------------------------------------------ routes: joining

/**
 * POST /raids/events/{eventId}/branches/{part}/join {instance_id?}: my place on a branch (placed by
 * the server, see placeOnBranch), or, with `instance_id`, another group's instance of this branch
 * that I help or a friend's (when it has room). POST /raids/events/{eventId}/join {}: my place
 * where it is (or where my clan needs me). A registered player only, while the event runs.
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
    // 2.74 (phase 6): the Cœur d'Asterion opens once the seven gigaboss fell (validated).
    const sealedHeart = async (branch: string) => {
        if (!HEART || branch !== HEART.id) return;
        const done = await doneOf(db, e.id);
        if (!heartOpenIn(done))
            throw Errors.conflict("HEART_SEALED", "Le Cœur d'Asterion s'ouvre quand les sept gigaboss sont tombés (" + gigabossDownIn(done) + " / " + CATALOGS.length + ").");
    };
    if (c) await sealedHeart(c.id);
    if (wanted) {
        const inst = await db.prepare("SELECT * FROM raid_instances WHERE id = ?1 AND event_id = ?2").bind(wanted, e.id).first<InstanceRow>();
        if (!inst || (c && inst.branch !== c.id)) throw Errors.notFound("INSTANCE_NOT_FOUND", "Ce groupe n'est plus en jeu.");
        await sealedHeart(inst.branch);
        if (inst.players >= INSTANCE_SIZE || !(await putIn(db, e.id, reg.clan_id, me, inst, me, "aide")))
            throw Errors.conflict("INSTANCE_FULL", "Ce groupe a déjà 8 joueurs.");
        return enter(ctx, e, inst, me);
    }
    if (!c) {
        const current = await placementOf(db, e.id, me);
        c = (current && catalogById(current.branch)) || (await neededBranch(db, e.id, reg.clan_id));
    }
    const inst = await placeOnBranch(db, e.id, reg.clan_id, me, c, me, "choix");
    return enter(ctx, e, inst, me);
}

// ------------------------------------------------------------------ routes: the clan's placement

/**
 * GET /raids/events/{eventId}/placements: my clan's players (registered) and their place (branch,
 * group, zone, in play or not), my clan's instances, and whether I may move them.
 */
export async function placements(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const db = ctx.env.DB;
    const e = await eventOf(db, ctx.params.eventId ?? "");
    const reg = await registrationOf(db, e.id, me);
    if (!reg) throw Errors.forbidden("RAID_NOT_REGISTERED", "Inscrivez-vous au raid pour voir la répartition de votre clan.");
    const [members, insts] = await db.batch([
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
                `SELECT i.id, i.branch, i.group_no, i.zone, i.players, i.clan_id, c.tag, (SELECT COUNT(*) FROM raid_placements x WHERE x.instance_id = i.id) AS placed
                 FROM raid_instances i JOIN clans c ON c.id = i.clan_id
                 WHERE i.event_id = ?1 AND (i.clan_id = ?2 OR i.id IN (SELECT instance_id FROM raid_placements WHERE event_id = ?1 AND clan_id = ?2))
                 ORDER BY i.branch, i.group_no LIMIT 200`,
            )
            .bind(e.id, reg.clan_id),
    ]);
    const instances = (insts!.results as { id: string; branch: string; group_no: number; zone: number; players: number; clan_id: string; tag: string; placed: number }[]).map((i) => ({
        id: i.id,
        branch: partOf(i.branch),
        branch_name: catalogById(i.branch)?.name ?? "",
        clan_tag: i.tag,
        group: i.group_no,
        zone: i.zone,
        zone_name: catalogById(i.branch)?.zones.find((z) => z.id === i.zone)?.name ?? "",
        players: i.players,
        placed: i.placed,
        mine: i.clan_id === reg.clan_id,
    }));
    const byId = new Map(instances.map((i) => [i.id, i]));
    return json(ctx, {
        event: { id: e.id, state: raidState(e, nowIso()) },
        clan_tag: reg.tag,
        can_manage: await canManage(db, e.id, reg.clan_id, me),
        branches: CATALOGS.map((c) => ({ part: partOf(c.id), name: c.name })),
        members: (members!.results as { id: string; username: string; instance_id: string | null; branch: string | null; reason: string | null; playing: number }[]).map((m) => {
            const i = m.instance_id ? byId.get(m.instance_id) : undefined;
            return {
                id: m.id,
                username: m.username,
                me: m.id === me,
                branch: m.branch ? partOf(m.branch) : null,
                instance_id: m.instance_id,
                group: i ? `[${i.clan_tag}]·${i.group}` : null,
                zone_name: i?.zone_name ?? null,
                playing: m.playing === 1,
                reason: m.reason,
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
    return { me, db, e, clanId: reg.clan_id };
}

/**
 * POST /raids/events/{eventId}/placements {player_id, branch, instance_id?}: the raid leader, the
 * chief or an officer moves a member of the clan to a branch (the server picks the instance with
 * room) or to an instance of it. The player's game in progress is not cut: the place counts from
 * their next entry.
 */
export async function move(ctx: Ctx): Promise<Response> {
    const { me, db, e, clanId } = await requireManager(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["player_id", "branch", "instance_id"]);
    const playerId = v.str("player_id", { max: 64, pattern: PLAYER_ID_RE })!;
    const part = v.str("branch", { max: 32, pattern: /^[a-z_]{1,32}$/ })!;
    const wanted = v.str("instance_id", { min: 36, max: 36, pattern: ID_RE, optional: true });
    v.done();
    const c = catalogByPart(part);
    if (!c) throw Errors.validation([{ field: "branch", message: "branche inconnue" }]);
    const target = await registrationOf(db, e.id, playerId);
    if (!target || target.clan_id !== clanId) throw Errors.notFound("PLAYER_NOT_FOUND", "Ce joueur n'est pas inscrit au raid avec votre clan.");
    let inst: InstanceRow;
    if (wanted) {
        const i = await db.prepare("SELECT * FROM raid_instances WHERE id = ?1 AND event_id = ?2 AND branch = ?3").bind(wanted, e.id, c.id).first<InstanceRow>();
        if (!i) throw Errors.notFound("INSTANCE_NOT_FOUND", "Cette instance n'existe pas sur cette branche.");
        if (!(await putIn(db, e.id, clanId, playerId, i, me, "chef"))) throw Errors.conflict("INSTANCE_FULL", "Cette instance a déjà 8 joueurs.");
        inst = i;
    } else {
        // Their place on that branch (kept when they already have one there): the upsert moves them.
        inst = await placeOnBranch(db, e.id, clanId, playerId, c, me, "chef");
    }
    return json(ctx, { placement: { player_id: playerId, branch: partOf(inst.branch), instance_id: inst.id, group: inst.group_no } });
}

/**
 * POST /raids/events/{eventId}/placements/rebalance {branch}: the clan's players of a branch spread
 * evenly over as few instances as they need (8 at most each): those of the groups beyond are
 * moved into the others, and the fullest groups give players to the emptiest. Players not in
 * play move first; those in play keep their game and change at their next entry.
 */
export async function rebalance(ctx: Ctx): Promise<Response> {
    const { me, db, e, clanId } = await requireManager(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["branch"]);
    const part = v.str("branch", { max: 32, pattern: /^[a-z_]{1,32}$/ })!;
    v.done();
    const c = catalogByPart(part);
    if (!c) throw Errors.validation([{ field: "branch", message: "branche inconnue" }]);
    const mine = await clanInstances(db, e.id, clanId, c.id);
    const rows = (
        await db
            .prepare(
                `SELECT pl.player_id, pl.instance_id,
                        EXISTS (SELECT 1 FROM raid_instance_members m WHERE m.instance_id = pl.instance_id AND m.player_id = pl.player_id AND m.active = 1 AND m.left_at IS NULL) AS playing
                 FROM raid_placements pl JOIN raid_instances i ON i.id = pl.instance_id
                 WHERE pl.event_id = ?1 AND pl.clan_id = ?2 AND pl.branch = ?3 AND i.clan_id = ?2 ORDER BY pl.updated_at DESC`,
            )
            .bind(e.id, clanId, c.id)
            .all<{ player_id: string; instance_id: string; playing: number }>()
    ).results;
    const n = rows.length;
    const k = Math.max(1, Math.ceil(n / INSTANCE_SIZE));
    // The k groups kept: those with the most players in play, then the lowest numbers.
    const playingIn = (id: string) => rows.filter((r) => r.instance_id === id && r.playing === 1).length;
    const kept = [...mine].sort((a, b) => playingIn(b.id) - playingIn(a.id) || a.group_no - b.group_no).slice(0, k);
    while (kept.length < k) {
        const opened = await openInstance(db, e.id, clanId, c);
        if (!opened) break;
        kept.push(opened);
    }
    const target = new Map(kept.map((inst, i) => [inst.id, Math.floor(n / k) + (i < n % k ? 1 : 0)]));
    const size = (id: string) => rows.filter((r) => r.instance_id === id).length;
    // Who moves: everyone of a group not kept, then the extra players of a kept group (not in play first).
    const movers = rows
        .filter((r) => !target.has(r.instance_id) || size(r.instance_id) > target.get(r.instance_id)!)
        .sort((a, b) => a.playing - b.playing);
    const leaving = new Map<string, number>();
    const chosen: typeof rows = [];
    for (const r of movers) {
        if (target.has(r.instance_id)) {
            const extra = size(r.instance_id) - target.get(r.instance_id)! - (leaving.get(r.instance_id) ?? 0);
            if (extra <= 0) continue;
        }
        leaving.set(r.instance_id, (leaving.get(r.instance_id) ?? 0) + 1);
        chosen.push(r);
    }
    let moved = 0;
    for (const r of chosen) {
        // The kept group furthest below its share takes them.
        const dest = [...kept].sort((a, b) => size(a.id) - target.get(a.id)! - (size(b.id) - target.get(b.id)!))[0]!;
        if (size(dest.id) >= target.get(dest.id)!) break;
        if (await putIn(db, e.id, clanId, r.player_id, dest, me, "equilibrage")) {
            r.instance_id = dest.id;
            ++moved;
        }
    }
    return json(ctx, {
        moved,
        instances: kept.map((i) => ({ id: i.id, group: i.group_no, placed: size(i.id) })),
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
                `SELECT i.id, i.branch, i.group_no, i.zone, i.players, i.clan_id, c.tag, (SELECT COUNT(*) FROM raid_placements p WHERE p.instance_id = i.id) AS placed
                 FROM raid_instances i JOIN clans c ON c.id = i.clan_id WHERE i.event_id = ?1 ORDER BY c.tag, i.branch, i.group_no LIMIT 500`,
            )
            .bind(eventId),
        // 2.75: the zones finished (once each, raid_zone_completions) and by which group.
        db
            .prepare(
                `SELECT z.branch, z.zone, z.completed_at, i.group_no, c.tag FROM raid_zone_completions z
                 LEFT JOIN raid_instances i ON i.id = z.instance_id LEFT JOIN clans c ON c.id = i.clan_id
                 WHERE z.event_id = ?1 ORDER BY z.completed_at DESC LIMIT 200`,
            )
            .bind(eventId),
    ]);
    const finished = zoneR!.results as { branch: string; zone: number; completed_at: string; group_no: number | null; tag: string | null }[];
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
    const needs: { branch: string; zone: number; name: string; groups: number; required: number }[] = [];
    const branches = CATALOGS.map((c) => {
        const mask = states.get(c.id) ?? 0;
        const reach = reachableIn(c, mask);
        const here = instances.filter((i) => i.branch === c.id);
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
        for (const z of c.zones) {
            if (z.groups_required < 2) continue;
            const open = c.objectives.filter((o) => o.zone === z.id && !o.optional && !isDone(mask, o.index)).length;
            const groups = fighting.filter((i) => i.zone === z.id).length;
            if (open > 0 && groups > 0 && groups < z.groups_required) needs.push({ branch: partOf(c.id), zone: z.id, name: z.name, groups, required: z.groups_required });
        }
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
            return { at: z.completed_at, kind: "zone", branch: partOf(z.branch), branch_name: c?.name ?? "", by: groupTag(z.tag, z.group_no), text: "Zone terminée : " + name, zone: z.zone };
        }),
    ]
        .sort((a, b) => (a.at < b.at ? 1 : a.at > b.at ? -1 : 0))
        .slice(0, 12);
    const fighting = instances.filter((i) => i.players > 0);
    const myInst = placement ? instances.find((i) => i.id === placement.instance_id) : undefined;
    // 2.74 (phase 6): the Cœur d'Asterion: sealed, open, fought (its shared health), fallen.
    const heart = await heartView(db, e.id, done, states, instances, reg?.clan_id ?? null);
    if (heart) body[heart.part] = heartBodyState(heart);
    return json(ctx, {
        server_time: now,
        event: { id: e.id, state: raidState(e, now), kind: e.kind },
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
            players: i.players,
            placed: i.placed,
            free: Math.max(0, INSTANCE_SIZE - Math.max(i.players, i.placed)),
            mine: !!reg && i.clan_id === reg.clan_id,
        }));
    const placement = await placementOf(db, e.id, me);
    const mine = placement && placement.branch === c.id ? instances.find((i) => i.id === placement.instance_id) : undefined;
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
            groups: instances.filter((i) => i.zone === z.id && i.players > 0).length,
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
            placed_branch: placement ? partOf(placement.branch) : null,
        },
        journal: mineDone.slice(0, 10).map((d) => ({ objective: d.objective_id, label: objectiveIn(c, d.objective_id)?.label ?? d.objective_id, by: by.get(d.objective_id) ?? null, at: d.accepted_at })),
    });
}
