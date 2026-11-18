// Clan raids « Asterion », phase 2 (2.65, docs/RAIDS.md): the branch « BRAS DROIT », its
// instances of up to 8 players and the state of its zones.
//
// An instance is a group of a clan (the groups of 8 of raids.ts) playing the branch in lockstep:
// one CoopRoom Durable Object in raid mode (coop.ts: 8 seats, vacant seats, late join, the 60 s
// rule) relays its inputs; the server never simulates anything. What the instance does (an
// objective done, a zone entered) is REPORTED by each of its games with the step and the digest
// of the state; a report is accepted only when the MAJORITY of the instance's active players
// reported the same value (consensus), and when it is plausible (the objective is in the zone,
// the zone is reachable with the branch's state, the step is possible since the instance
// started, the two bellows come from two distinct instances). A minority that disagrees is
// rejected and journaled (raid_report_rejections); nothing a single game says is trusted.
//
// The branch's state is the set of objectives done (raid_branch_objectives): idempotent by
// objective id (two instances, or two concurrent acceptances, count it once). The rooms read it
// (branchMaskOf) and give it to their games at a step, so every game of an instance applies it at
// the same step. The catalog (raidBranches.json) is written by tools/levelgen/raid_bras_droit.py.
//
// 2.68 (phase 3): the gigaboss PYROCLAST in the Main (zone 8). Two more kinds of reports, by the
// same consensus: « boss » (the phase its fight reached, 4 = its fall: raid_gigaboss_fights) and
// « result » (the players' contributions computed by the games: raid_contributions, mapped to the
// players by the seat they held and the step they took it from). Implausible numbers are refused
// before any majority; the rewards (raidRewards.ts) come from accepted tables only.

import { requireAuth } from "./auth";
import { ask, stub, MAX_TICKS } from "./coop";
import { ApiError, Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import { CONTRIBUTION_FIELDS, canonical, grantFor, results, scoreOf, type Contribution } from "./raidRewards";
import { catalogById, doneOf, levelHashOf, maskOfIds, objectiveIn, reachableIn, stateMaskOf, type Catalog } from "./raidMap";
import { raidState } from "./raids";
import { grantWorldFor, journalDuplicate, reportHalt, requireEntry } from "./raidWorld";
import { HEART_OBJECTIVE, HEART_UNITS, checkHeartFall, checkHeartOpen, grantHeart, heartFallenIn, heartOpenIn, isHeart, storeHeartDamage, wakeHeartRelays } from "./raidHeart";
import { RULES, rateLimit } from "./ratelimit";
import catalogJson from "./raidBranches.json";
import { Validator } from "./validate";

export interface BranchZone {
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
export interface BranchObjective {
    id: string;
    index: number;
    zone: number;
    label: string;
    optional: boolean;
    role: string;
    pair: string;
}
export interface BranchCatalog {
    id: string;
    revision: number;
    name: string;
    gigaboss: string;
    start_zone: number;
    generators_needed: number;
    zones: BranchZone[];
    edges: { a: number; b: number; lock: string; secret: boolean }[];
    objectives: BranchObjective[];
}

export const BRANCH = catalogJson as unknown as BranchCatalog;
export const BRANCH_LEVEL = "raid:" + BRANCH.id;
export const INSTANCE_SIZE = 8;
/** A report's step may be ahead of the server's clock since the start by this much (late clocks, delays). */
const TICK_SLACK = 120 * 60;
/** 2.68: the zone of the branch's gigaboss and its name. */
export const GIGABOSS_ZONE = BRANCH.zones.find((z) => z.boss)?.id ?? 0;
export const GIGABOSS_OBJECTIVE = BRANCH.objectives.find((o) => o.role === "gigaboss")?.id ?? "";

/**
 * 2.68: why a table of contributions is impossible ("" when plausible): each number bounded by
 * what a player can do in the steps they held their seat (the games' own rules: 3 s on a vent,
 * 2 s by a soul, three weak points per exposure...).
 */
export function implausible(records: Contribution[], tick: number, objectives = BRANCH.objectives.length): string {
    if (records.length > 32) return "trop de joueurs dans le tableau";
    const seen = new Set<string>();
    for (const c of records) {
        const key = `${c.seat}:${c.from}`;
        if (seen.has(key)) return "joueur en double dans le tableau";
        seen.add(key);
        const span = tick - c.from + 1;
        if (c.from > tick) return "contribution d'après le pas du rapport";
        if (c.ticks > span) return "temps de participation impossible";
        if (c.boss_damage > c.damage) return "dégâts au gigaboss supérieurs aux dégâts";
        if (c.damage > span * 6 + 600) return "dégâts impossibles";
        if (c.avoided > span * 4 + 600) return "dégâts évités impossibles";
        if (c.blocks > Math.floor(span / 12) + 5) return "blocages impossibles";
        if (c.rescues > Math.floor(span / 240) + 2) return "sauvetages impossibles";
        if (c.vents > Math.floor(span / 360) + 1) return "évents impossibles";
        if (c.weak_points > 3 * (Math.floor(span / 1200) + 1)) return "points faibles impossibles";
        if (c.objectives > objectives) return "objectifs impossibles"; // 2.68: those of the instance's branch
        if (c.gigaboss > 1) return "valeur impossible";
    }
    return "";
}

function readRecords(raw: unknown, fail: (m: string) => never): Contribution[] {
    if (!Array.isArray(raw) || raw.length > 32) return fail("records : un tableau de 32 lignes au plus");
    return raw.map((r) => {
        if (!r || typeof r !== "object" || Array.isArray(r)) return fail("records : une ligne n'est pas un objet");
        const o = r as Record<string, unknown>;
        const c = {} as Contribution;
        for (const k of Object.keys(o)) if (!(CONTRIBUTION_FIELDS as readonly string[]).includes(k)) return fail(`records : champ inconnu ${k}`);
        for (const k of CONTRIBUTION_FIELDS) {
            const v = o[k];
            if (typeof v !== "number" || !Number.isInteger(v) || v < 0 || v > 2_000_000_000) return fail(`records : ${k} doit être un entier positif`);
            c[k] = v;
        }
        if (c.seat > 7) return fail("records : siège de 0 à 7");
        return c;
    });
}

const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const HASH16_RE = /^[0-9a-f]{16}$/;

// ------------------------------------------------------------------ rules (pure, shared with the game)

export const isDone = (mask: number, index: number) => index >= 0 && index < 32 && ((mask >>> index) & 1) === 1;

export function objectiveOf(id: string): BranchObjective | undefined {
    return BRANCH.objectives.find((o) => o.id === id);
}

export function maskOf(ids: string[]): number {
    let m = 0;
    for (const id of ids) {
        const o = objectiveOf(id);
        if (o) m = (m | (1 << o.index)) >>> 0;
    }
    return m;
}

export function generatorsOf(mask: number): number {
    return BRANCH.objectives.filter((o) => o.role === "generator" && isDone(mask, o.index)).length;
}

export function lockOpen(lock: string, mask: number): boolean {
    if (!lock) return true;
    if (lock === "generators") return generatorsOf(mask) >= BRANCH.generators_needed;
    if (lock === "shortcut") return BRANCH.objectives.some((o) => o.role === "shortcut" && isDone(mask, o.index));
    if (lock.startsWith("objective:")) {
        const o = objectiveOf(lock.slice(10));
        return !!o && isDone(mask, o.index);
    }
    return false;
}

/** The zones reachable from the start with the passages open (index = zone id). */
export function reachable(mask: number): boolean[] {
    const r = new Array<boolean>(BRANCH.zones.length + 1).fill(false);
    const todo = [BRANCH.start_zone];
    r[BRANCH.start_zone] = true;
    while (todo.length) {
        const z = todo.shift()!;
        for (const e of BRANCH.edges) {
            if (e.a !== z && e.b !== z) continue;
            const other = e.a === z ? e.b : e.a;
            if (r[other] || !lockOpen(e.lock, mask)) continue;
            r[other] = true;
            todo.push(other);
        }
    }
    return r;
}

/** Every objective of the zone that is not optional is done (a zone without one: never). */
export function controlled(zone: number, mask: number): boolean {
    const mandatory = BRANCH.objectives.filter((o) => o.zone === zone && !o.optional);
    return mandatory.length > 0 && mandatory.every((o) => isDone(mask, o.index));
}

/** The players a report needs: more than half of the instance's active players (at least 1). */
export function majority(active: number): number {
    return Math.floor(Math.max(1, active) / 2) + 1;
}

/** The value a majority of `values` agrees on (one value per player), else null. */
export function decide(values: string[], active: number): string | null {
    const need = majority(Math.max(active, 1));
    const count = new Map<string, number>();
    for (const v of values) count.set(v, (count.get(v) ?? 0) + 1);
    let best: string | null = null;
    let bestN = 0;
    for (const [v, n] of count) if (n > bestN) [best, bestN] = [v, n];
    return bestN >= need ? best : null;
}

// ------------------------------------------------------------------ D1

/**
 * The branch's state of an event: the objectives accepted so far (bits of the catalog) and, 2.68,
 * the links of the titan on (bits 24 and up: raidMap.ts), for any of the seven branches.
 */
export async function branchMaskOf(db: D1Database, eventId: string, branch: string): Promise<number> {
    return stateMaskOf(db, eventId, branch);
}

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
    started_at: string | null;
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

/** My group number in my clan's roster (raids.ts groupsOf: the raid leader leads group 1). */
async function myGroup(db: D1Database, eventId: string, clanId: string, playerId: string): Promise<number> {
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

async function journal(db: D1Database, instanceId: string, playerId: string, key: string, value: string, reason: string, once = false) {
    // 2.74: `once`: not again when this player's report of this key is already journaled (a minority
    // report that raced with the acceptance: the winning request may have journaled it already).
    await db
        .prepare(
            `INSERT INTO raid_report_rejections (instance_id, player_id, report_key, value, reason, created_at) SELECT ?1, ?2, ?3, ?4, ?5, ?6
             WHERE ?7 = 0 OR NOT EXISTS (SELECT 1 FROM raid_report_rejections WHERE instance_id = ?1 AND report_key = ?3 AND player_id = ?2)`,
        )
        .bind(instanceId, playerId, key, value, reason, nowIso(), once ? 1 : 0)
        .run();
}

// ------------------------------------------------------------------ routes

/**
 * POST /raids/events/{eventId}/instance {instance_id?}: the instance of my group (made the first
 * time), or, with `instance_id`, another group's instance of the same event that I help (« Aider »).
 * A registered player only, while the event runs.
 */
export async function joinInstance(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const db = ctx.env.DB;
    const now = nowIso();
    const e = await eventOf(db, ctx.params.eventId ?? "");
    requireRunning(e, now);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["instance_id"]);
    const wanted = v.str("instance_id", { min: 36, max: 36, pattern: ID_RE, optional: true });
    v.done();
    const reg = await registrationOf(db, e.id, me);
    if (!reg) throw Errors.forbidden("RAID_NOT_REGISTERED", "Inscrivez-vous au raid pour y combattre.");
    await requireEntry(db, e, me); // 2.69: not while suspended; the late arrivals' rule
    let inst: InstanceRow | null;
    if (wanted) {
        inst = await db.prepare("SELECT * FROM raid_instances WHERE id = ?1 AND event_id = ?2").bind(wanted, e.id).first<InstanceRow>();
        if (!inst) throw Errors.notFound("INSTANCE_NOT_FOUND", "Ce groupe n'est plus en jeu.");
        if (inst.players >= INSTANCE_SIZE) throw Errors.conflict("INSTANCE_FULL", "Ce groupe a déjà 8 joueurs.");
    } else {
        const group = await myGroup(db, e.id, reg.clan_id, me);
        if (group < 1) throw Errors.forbidden("RAID_NOT_REGISTERED", "Inscrivez-vous au raid pour y combattre.");
        await db
            .prepare(
                `INSERT OR IGNORE INTO raid_instances (id, event_id, clan_id, group_no, branch, zone, players, created_at, updated_at)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, 0, ?7, ?7)`,
            )
            .bind(crypto.randomUUID(), e.id, reg.clan_id, group, BRANCH.id, BRANCH.start_zone, now)
            .run();
        inst = await db
            .prepare("SELECT * FROM raid_instances WHERE event_id = ?1 AND clan_id = ?2 AND group_no = ?3 AND branch = ?4")
            .bind(e.id, reg.clan_id, group, BRANCH.id)
            .first<InstanceRow>();
    }
    if (!inst) throw Errors.notFound("INSTANCE_NOT_FOUND", "Ce groupe n'est plus en jeu.");
    // Its relay (made once: the room answers 409 when it exists).
    await ask(ctx.env, inst.id, "/create", {
        id: inst.id,
        host: me,
        level: { source: "raid", id: BRANCH_LEVEL, hash: levelHashOf(BRANCH as unknown as Catalog), title: "Asterion · " + BRANCH.name },
        raid: { instance_id: inst.id, event_id: e.id, branch: BRANCH.id, revision: BRANCH.revision },
    });
    const tag = await db.prepare("SELECT tag FROM clans WHERE id = ?1").bind(inst.clan_id).first<{ tag: string }>();
    return json(ctx, {
        instance: { id: inst.id, group: inst.group_no, clan_tag: tag?.tag ?? "", zone: inst.zone, players: inst.players, branch: BRANCH.id, revision: BRANCH.revision, level: BRANCH_LEVEL },
    });
}

/** GET /raids/instances/{instanceId}/socket: the WebSocket of a game of the instance (a registered player). */
export async function instanceSocket(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const db = ctx.env.DB;
    const id = ctx.params.instanceId ?? "";
    if (!ID_RE.test(id)) throw Errors.notFound("INSTANCE_NOT_FOUND", "Instance introuvable.");
    if (ctx.request.headers.get("upgrade")?.toLowerCase() !== "websocket") throw new ApiError(426, "UPGRADE_REQUIRED", "Connexion WebSocket attendue.");
    const inst = await db.prepare("SELECT * FROM raid_instances WHERE id = ?1").bind(id).first<InstanceRow>();
    if (!inst) throw Errors.notFound("INSTANCE_NOT_FOUND", "Instance introuvable.");
    const e = await eventOf(db, inst.event_id);
    requireRunning(e, nowIso());
    if (!(await registrationOf(db, e.id, auth.player.id))) throw Errors.forbidden("RAID_NOT_REGISTERED", "Inscrivez-vous au raid pour y combattre.");
    await requireEntry(db, e, auth.player.id); // 2.69: not while suspended; the late arrivals' rule
    const headers = new Headers(ctx.request.headers);
    headers.set("x-player-id", auth.player.id);
    headers.set("x-player-name", auth.player.username);
    const res = await stub(ctx.env, id).fetch(new Request("https://coop/socket", { headers }));
    if (res.status === 403 || res.status === 404) throw Errors.notFound("INSTANCE_NOT_FOUND", "Instance introuvable.");
    return res;
}

/**
 * POST /raids/instances/{instanceId}/reports {kind: "objective" | "zone", objective?, zone, tick, hash}:
 * what this game saw (an objective done, a zone entered) at a step, with the digest of its state.
 * Accepted when the majority of the instance's active players reported the same; a minority that
 * disagrees, or an implausible report, is rejected and journaled.
 */
export async function report(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const db = ctx.env.DB;
    await rateLimit(ctx, RULES.raidReport, "player:" + me);
    const id = ctx.params.instanceId ?? "";
    if (!ID_RE.test(id)) throw Errors.notFound("INSTANCE_NOT_FOUND", "Instance introuvable.");
    const body = await readJsonObject(ctx);
    // 2.68: an administrator's test (ADMINISTRATION › Raids › « Tester Asterion ») never reports:
    // a report flagged as a test is refused whoever sends it (an administrator or not), before anything.
    if (body.test !== undefined) throw Errors.forbidden("RAID_TEST_REPORT", "Un test d'administration ne compte jamais : rapport refusé.");
    const v = new Validator(body, ["kind", "objective", "zone", "tick", "hash", "phase", "records", "damage"]);
    const kind = v.oneOf("kind", ["objective", "zone", "boss", "result", "heart"] as const)!;
    const objective = v.str("objective", { min: 1, max: 64, pattern: /^[a-z0-9_]{1,64}$/, optional: kind !== "objective" });
    const zone = v.int("zone", { min: 1, max: BRANCH.zones.length })!;
    const tick = v.int("tick", { min: 0, max: MAX_TICKS })!;
    const hash = v.str("hash", { min: 16, max: 16, pattern: HASH16_RE })!;
    // 2.68: the gigaboss's phase reached (1 to 4: its fall), the table of the contributions.
    const phase = v.int("phase", { min: 1, max: 4, optional: kind !== "boss" });
    // 2.72 (phase 6): the part of the Cœur d'Asterion's shared health this instance took (ten-thousandths).
    const damage = v.int("damage", { min: 0, max: HEART_UNITS, optional: kind !== "heart" });
    if (kind !== "heart" && body.damage !== undefined) v.fail("damage", "réservé aux rapports « heart »");
    const records = kind === "result" ? readRecords(body.records, (m) => { v.fail("records", m); return v.done() as never; }) : [];
    if (kind !== "result" && body.records !== undefined) v.fail("records", "réservé aux rapports « result »");
    v.done();
    const now = nowIso();
    const inst = await db.prepare("SELECT * FROM raid_instances WHERE id = ?1").bind(id).first<InstanceRow>();
    if (!inst) throw Errors.notFound("INSTANCE_NOT_FOUND", "Instance introuvable.");
    const member = await db
        .prepare("SELECT active FROM raid_instance_members WHERE instance_id = ?1 AND player_id = ?2 AND left_at IS NULL")
        .bind(id, me)
        .first<{ active: number }>();
    if (!member) throw Errors.forbidden("NOT_IN_INSTANCE", "Vous ne jouez pas dans cette instance.");
    const e = await eventOf(db, inst.event_id);
    const key =
        kind === "objective" ? "o:" + objective : kind === "boss" ? `b:${phase}` : kind === "result" ? `r:${tick}` : kind === "heart" ? `h:${tick}` : `z:${zone}@${tick}`;
    // A table's value is its canonical text, rebuilt here from the numbers (never the game's text).
    const value = kind === "result" ? `${zone}:${tick}:${hash}:${canonical(records)}` : kind === "heart" ? `${zone}:${tick}:${hash}:${damage}` : `${zone}:${tick}:${hash}`;
    const reject = async (reason: string) => {
        await journal(db, id, me, key, value, reason);
        return json(ctx, { status: "rejected", reason });
    };

    // 2.69 (phase 5): nothing counts while the event is suspended, nor once its official result is frozen.
    const halt = await reportHalt(db, e);
    if (halt) return reject(halt);
    // Plausibility first: nothing impossible reaches the consensus.
    const state = raidState(e, now);
    if (state !== "en_cours" && !(state === "termine" && Date.parse(now) - Date.parse(e.ends_at) < 10 * 60000))
        return reject("le raid n'est pas en cours");
    const started = inst.started_at ? Date.parse(inst.started_at) : NaN;
    if (!Number.isFinite(started) || tick > Math.ceil(((Date.parse(now) - started) / 1000) * 120) + TICK_SLACK) return reject("pas impossible depuis le début de l'instance");
    // 2.68: the instance's branch (any of the seven) and its state with the titan's links.
    const cat = catalogById(inst.branch) ?? (BRANCH as unknown as Catalog);
    if (zone > cat.zones.length) return reject("zone inconnue");
    if (kind === "boss") {
        // 2.68 (phase 3): only a gigaboss that is fought (its objective in the catalog), in its own zone.
        const bossZone = cat.zones.find((z) => (z as { boss?: boolean }).boss)?.id ?? 0;
        const fought = (cat.objectives as { role?: string }[]).some((o) => o.role === "gigaboss");
        if (!fought || zone !== bossZone) return reject("le gigaboss n'est pas dans cette zone");
    }
    if (kind === "result") {
        const why = implausible(records, tick, cat.objectives.length);
        if (why) return reject(why);
    }
    if (kind === "heart") {
        // 2.72 (phase 6): only the heart's arena, once open (the seven gigaboss fallen), at a pace a group can
        // keep (never the whole heart in less than a minute of the instance).
        const bossZone = cat.zones.find((z) => (z as { boss?: boolean }).boss)?.id ?? 0;
        if (!isHeart(inst.branch) || zone !== bossZone) return reject("le Cœur d'Asterion n'est pas dans cette zone");
        const done = await doneOf(db, e.id);
        if (!heartOpenIn(done)) return reject("le Cœur d'Asterion est encore scellé");
        if (damage! > Math.ceil((tick * HEART_UNITS) / (120 * 60)) + 100) return reject("dégâts au Cœur impossibles");
        // 2.74 (with phase 7): its shared health is frozen once its fall is validated (the last parts every
        // instance sends as its own heart falls change nothing, and are no anomaly: never journaled).
        if (heartFallenIn(done)) return json(ctx, { status: "frozen", reason: "le Cœur d'Asterion est tombé : sa vie partagée est figée" });
    }
    const mask = await branchMaskOf(db, e.id, inst.branch);
    let obj: BranchObjective | undefined;
    if (kind === "objective") {
        obj = objectiveIn(cat, objective!);
        if (!obj) return reject("objectif inconnu");
        if (obj.zone !== zone) return reject("objectif d'une autre zone");
    }
    if (!reachableIn(cat, mask)[zone]) {
        // An objective of a zone that opened with this instance's own completions: its locks
        // count what the instance itself accepted too (the generators it switched on itself).
        const own = await db.prepare("SELECT report_key FROM raid_accepted WHERE instance_id = ?1 AND report_key LIKE 'o:%'").bind(id).all<{ report_key: string }>();
        const mine = maskOfIds(cat, own.results.map((r) => r.report_key.slice(2)));
        if (!reachableIn(cat, (mask | mine) >>> 0)[zone]) return reject("zone verrouillée pour la branche");
    }

    // The player's report (one per key: a second, different one is refused).
    const put = await db
        .prepare("INSERT OR IGNORE INTO raid_reports (instance_id, report_key, player_id, value, tick, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)")
        .bind(id, key, me, value, tick, now)
        .run();
    if (!put.meta.changes) {
        const prev = await db.prepare("SELECT value FROM raid_reports WHERE instance_id = ?1 AND report_key = ?2 AND player_id = ?3").bind(id, key, me).first<{ value: string }>();
        if (prev?.value !== value) return reject("déjà signalé avec une autre valeur");
    }
    const accepted = await db.prepare("SELECT value FROM raid_accepted WHERE instance_id = ?1 AND report_key = ?2").bind(id, key).first<{ value: string }>();
    if (accepted) {
        if (accepted.value === value) return json(ctx, { status: "accepted" });
        return reject("minoritaire : l'instance a validé une autre valeur");
    }
    // 2.74 (phase 7): only the votes of the players in play count (the majority is of the active
    // players): a report of a member who left the game, or is absent, waits and never tips the count.
    // Before, absent members' reports counted against a majority computed on the active ones only
    // (three absent accomplices outvoted two players in play).
    const [all, act] = await db.batch([
        db
            .prepare(
                `SELECT r.player_id, r.value, COALESCE(m.active, 0) AS active FROM raid_reports r
                 LEFT JOIN raid_instance_members m ON m.instance_id = r.instance_id AND m.player_id = r.player_id AND m.left_at IS NULL
                 WHERE r.instance_id = ?1 AND r.report_key = ?2`,
            )
            .bind(id, key),
        db.prepare("SELECT COUNT(*) AS n FROM raid_instance_members WHERE instance_id = ?1 AND active = 1 AND left_at IS NULL").bind(id),
    ]);
    const reports = all!.results as { player_id: string; value: string; active: number }[];
    const votes = reports.filter((r) => r.active === 1);
    const active = Math.max(1, (act!.results[0] as { n: number }).n);
    const winner = decide(
        votes.map((r) => r.value),
        active,
    );
    if (!winner) return json(ctx, { status: "pending", reports: votes.length, needed: majority(active) });

    // The majority agrees: accepted once (concurrent acceptances: the first insert wins). 2.74: never
    // once the official result is frozen or while the event is suspended (the same statement: a
    // close that runs at this very moment is either before it or after it, never half).
    const won = await db
        .prepare(
            `INSERT OR IGNORE INTO raid_accepted (instance_id, report_key, value, accepted_at) SELECT ?1, ?2, ?3, ?4
             WHERE NOT EXISTS (SELECT 1 FROM raid_event_results WHERE event_id = ?5)
               AND NOT EXISTS (SELECT 1 FROM raid_events WHERE id = ?5 AND suspended_at IS NOT NULL)`,
        )
        .bind(id, key, winner, now, e.id)
        .run();
    if (!won.meta.changes && !(await db.prepare("SELECT 1 AS x FROM raid_accepted WHERE instance_id = ?1 AND report_key = ?2").bind(id, key).first())) {
        // Not accepted, and nobody else did: the event was frozen or suspended meanwhile.
        const fresh = await db.prepare("SELECT * FROM raid_events WHERE id = ?1").bind(e.id).first<EventRow>();
        return reject((await reportHalt(db, fresh ?? e)) || "résultats officiels figés : l'événement est clos");
    }
    if (won.meta.changes) {
        for (const r of reports) if (r.value !== winner) await journal(db, id, r.player_id, key, r.value, "minoritaire : l'instance a validé une autre valeur");
        if (obj && isHeart(inst.branch) && obj.id === HEART_OBJECTIVE) {
            // 2.72 (phase 6): the heart falls for the world only when the parts validated of every instance
            // add up to its whole health (checkHeartFall writes its objective once), never on one group's word.
            if (await checkHeartFall(db, e, id, now)) await wakeHeartRelays(ctx.env as never, e.id);
        } else if (obj) {
            // The bellows: one per instance; the other one must come from another group.
            const pairOwn = obj.pair
                ? await db
                      .prepare("SELECT objective_id FROM raid_branch_objectives WHERE event_id = ?1 AND branch = ?2 AND instance_id = ?3")
                      .bind(e.id, inst.branch, id)
                      .all<{ objective_id: string }>()
                : null;
            if (pairOwn && pairOwn.results.some((r) => r.objective_id !== obj!.id && objectiveIn(cat, r.objective_id)?.pair === obj!.pair)) {
                for (const r of reports) await journal(db, id, r.player_id, key, r.value, "un autre groupe doit accomplir l'autre objectif de la paire");
                return json(ctx, { status: "rejected", reason: "un autre groupe doit accomplir l'autre objectif de la paire" });
            }
            // Idempotent by objective id: two instances (or two acceptances) count it once.
            const counted = await db
                .prepare("INSERT OR IGNORE INTO raid_branch_objectives (event_id, branch, objective_id, instance_id, accepted_at) VALUES (?1, ?2, ?3, ?4, ?5)")
                .bind(e.id, inst.branch, obj.id, id, now)
                .run();
            // 2.69: already counted by another group (or at the same moment): journaled for the administrators.
            if (!counted.meta.changes) await journalDuplicate(db, e.id, id, inst.branch, obj.id, obj.label, now);
            // 2.72 (phase 6): the seventh gigaboss down opens the Cœur d'Asterion (told once).
            if (obj.role === "gigaboss") await checkHeartOpen(db, e, now);
            // Every relay of the event gives the new state to its games (a few seconds anyway);
            // 2.68: those of the other branches too (the titan's links).
            // 2.74: all at once and after the answer (dozens of instances in a world event: before, one
            // after the other, inside the request); a room that does not answer reads it within 3 s anyway.
            const rooms = await db.prepare("SELECT id FROM raid_instances WHERE event_id = ?1 AND players > 0").bind(e.id).all<{ id: string }>();
            ctx.exec.waitUntil(Promise.allSettled(rooms.results.map((r) => ask(ctx.env, r.id, "/raid-state"))));
        } else if (kind === "boss") {
            // 2.68: the fight of this instance reached a phase (4: the gigaboss fell).
            await db
                .prepare(
                    `INSERT INTO raid_gigaboss_fights (instance_id, event_id, branch, boss, phase, defeated_tick, defeated_at, updated_at)
                     VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)
                     ON CONFLICT (instance_id) DO UPDATE SET phase = MAX(phase, excluded.phase),
                        defeated_tick = COALESCE(defeated_tick, excluded.defeated_tick), defeated_at = COALESCE(defeated_at, excluded.defeated_at), updated_at = excluded.updated_at`,
                )
                .bind(id, e.id, inst.branch, cat.gigaboss ?? BRANCH.gigaboss, phase!, phase === 4 ? tick : null, phase === 4 ? now : null, now)
                .run();
            if (phase === 4) await grantForInstance(db, e.id, id);
        } else if (kind === "heart") {
            // 2.72 (phase 6): this instance's part (the highest accepted), the others told at a step; the fall
            // when the parts of every instance reach its whole health.
            await storeHeartDamage(db, e.id, id, damage!, tick, now);
            await checkHeartFall(db, e, id, now);
            await wakeHeartRelays(ctx.env as never, e.id);
        } else if (kind === "result") {
            await storeResult(db, e.id, id, tick, records, now);
            if (isHeart(inst.branch)) await grantHeart(db, e.id, now); // 2.72: its rewards follow the tables after its fall
        } else {
            // 2.74: the newest step wins (two zone reports accepted in the other order — their requests
            // crossed on the network — never move the group back on the map).
            await db
                .prepare("UPDATE raid_instances SET zone = ?2, zone_tick = ?4, updated_at = ?3 WHERE id = ?1 AND (zone_tick IS NULL OR zone_tick <= ?4)")
                .bind(id, zone, now, tick)
                .run();
        }
    }
    // 2.74: a minority report that crossed the acceptance (it was not there when the majority was
    // counted): rejected and journaled once, never told « accepted ».
    if (value !== winner) {
        if (!won.meta.changes) await journal(db, id, me, key, value, "minoritaire : l'instance a validé une autre valeur", true);
        return json(ctx, { status: "rejected", reason: "minoritaire : l'instance a validé une autre valeur" });
    }
    return json(ctx, { status: "accepted" });
}

/** The players of an instance, by seat and the step they hold it from. */
async function membersOf(db: D1Database, instanceId: string) {
    return (
        await db
            .prepare("SELECT player_id, seat, seat_from, joined_at FROM raid_instance_members WHERE instance_id = ?1")
            .bind(instanceId)
            .all<{ player_id: string; seat: number; seat_from: number | null; joined_at: string }>()
    ).results;
}

/** The player who held `seat` from step `from` (the relay's seat_from; without it, the latest on that seat). */
export function holderOf(members: { player_id: string; seat: number; seat_from: number | null; joined_at: string }[], seat: number, from: number): string | null {
    const onSeat = members.filter((m) => m.seat === seat);
    const timed = onSeat.filter((m) => m.seat_from !== null && m.seat_from <= from).sort((a, b) => b.seat_from! - a.seat_from!);
    if (timed.length) return timed[0]!.player_id;
    const untimed = onSeat.filter((m) => m.seat_from === null).sort((a, b) => (a.joined_at < b.joined_at ? 1 : -1));
    return untimed[0]?.player_id ?? null;
}

/** 2.68: an accepted table: each row kept if it is newer than the one stored (the numbers only grow). */
async function storeResult(db: D1Database, eventId: string, instanceId: string, tick: number, records: Contribution[], now: string) {
    const members = await membersOf(db, instanceId);
    const stmts = records.map((c) =>
        db
            .prepare(
                `INSERT INTO raid_contributions (instance_id, event_id, seat, from_tick, player_id, damage, boss_damage, avoided, blocks, rescues, vents,
                    weak_points, objectives, ticks, gigaboss, score, tick, accepted_at)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18)
                 ON CONFLICT (instance_id, seat, from_tick) DO UPDATE SET player_id = COALESCE(excluded.player_id, player_id), damage = excluded.damage,
                    boss_damage = excluded.boss_damage, avoided = excluded.avoided, blocks = excluded.blocks, rescues = excluded.rescues,
                    vents = excluded.vents, weak_points = excluded.weak_points, objectives = excluded.objectives, ticks = excluded.ticks,
                    gigaboss = MAX(gigaboss, excluded.gigaboss), score = excluded.score, tick = excluded.tick, accepted_at = excluded.accepted_at
                 WHERE excluded.tick >= raid_contributions.tick`,
            )
            .bind(
                instanceId,
                eventId,
                c.seat,
                c.from,
                holderOf(members, c.seat, c.from),
                c.damage,
                c.boss_damage,
                c.avoided,
                c.blocks,
                c.rescues,
                c.vents,
                c.weak_points,
                c.objectives,
                c.ticks,
                c.gigaboss ? 1 : 0,
                scoreOf(c).total,
                tick,
                now,
            ),
    );
    if (stmts.length) await db.batch(stmts);
    await grantForInstance(db, eventId, instanceId);
}

/** The rewards of the players of an instance after an acceptance (idempotent). */
async function grantForInstance(db: D1Database, eventId: string, instanceId: string) {
    const ids = (await db.prepare("SELECT DISTINCT player_id FROM raid_contributions WHERE instance_id = ?1 AND player_id IS NOT NULL").bind(instanceId).all<{ player_id: string }>()).results.map(
        (r) => r.player_id,
    );
    if (ids.length) await grantFor(db, eventId, ids);
    if (ids.length) await grantWorldFor(db, eventId, ids); // 2.69: the world event's exclusive suit (or its silver version)
}

/**
 * GET /raids/events/{eventId}/results (2.68): the results of an event (raidRewards.ts), for the
 * players who may see its branch (a registered player, or a member of a clan taking part).
 */
export async function eventResults(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const db = ctx.env.DB;
    const e = await eventOf(db, ctx.params.eventId ?? "");
    const reg = await registrationOf(db, e.id, me);
    if (e.kind === "clan" && !reg) {
        const mine = await db
            .prepare("SELECT 1 AS x FROM raid_clan_entries x JOIN clan_members m ON m.clan_id = x.clan_id WHERE x.event_id = ?1 AND m.player_id = ?2")
            .bind(e.id, me)
            .first();
        if (!mine) throw Errors.notFound("RAID_NOT_FOUND", "Événement introuvable.");
    }
    return results(ctx, e, me);
}

/**
 * GET /raids/events/{eventId}/branch: the branch's map (maquette « Branche »): every zone with
 * its state (reachable, controlled, objectives done and by which group), the passages, the
 * generators and the shortcut, the instances (clan, group, zone, players) and mine, the latest
 * objectives accepted.
 */
export async function branchView(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const db = ctx.env.DB;
    const now = nowIso();
    const e = await eventOf(db, ctx.params.eventId ?? "");
    const reg = await registrationOf(db, e.id, me);
    if (e.kind === "clan" && !reg) {
        const mine = await db
            .prepare("SELECT 1 AS x FROM raid_clan_entries x JOIN clan_members m ON m.clan_id = x.clan_id WHERE x.event_id = ?1 AND m.player_id = ?2")
            .bind(e.id, me)
            .first();
        if (!mine) throw Errors.notFound("RAID_NOT_FOUND", "Événement introuvable.");
    }
    const [objR, instR] = await db.batch([
        db
            .prepare(
                `SELECT o.objective_id, o.accepted_at, i.group_no, c.tag FROM raid_branch_objectives o
                 LEFT JOIN raid_instances i ON i.id = o.instance_id LEFT JOIN clans c ON c.id = i.clan_id
                 WHERE o.event_id = ?1 AND o.branch = ?2 ORDER BY o.accepted_at DESC`,
            )
            .bind(e.id, BRANCH.id),
        db
            .prepare(
                `SELECT i.id, i.group_no, i.zone, i.players, i.clan_id, c.tag FROM raid_instances i JOIN clans c ON c.id = i.clan_id
                 WHERE i.event_id = ?1 AND i.branch = ?2 ORDER BY c.tag, i.group_no LIMIT 100`,
            )
            .bind(e.id, BRANCH.id),
    ]);
    const done = objR!.results as { objective_id: string; accepted_at: string; group_no: number | null; tag: string | null }[];
    const mask = maskOf(done.map((d) => d.objective_id));
    const reach = reachable(mask);
    const by = new Map(done.map((d) => [d.objective_id, d.tag ? `[${d.tag}]·${d.group_no}` : null]));
    const instances = (instR!.results as { id: string; group_no: number; zone: number; players: number; clan_id: string; tag: string }[]).map((i) => ({
        id: i.id,
        clan_tag: i.tag,
        group: i.group_no,
        zone: i.zone,
        players: i.players,
        free: Math.max(0, INSTANCE_SIZE - i.players),
        mine: !!reg && i.clan_id === reg.clan_id,
    }));
    const myGroupNo = reg ? await myGroup(db, e.id, reg.clan_id, me) : 0;
    const mine = reg ? instances.find((i) => i.mine && i.group === myGroupNo) : undefined;
    return json(ctx, {
        server_time: now,
        event: { id: e.id, state: raidState(e, now) },
        branch: { id: BRANCH.id, name: BRANCH.name, gigaboss: BRANCH.gigaboss, revision: BRANCH.revision },
        mask,
        generators: { done: generatorsOf(mask), needed: BRANCH.generators_needed },
        shortcut: lockOpen("shortcut", mask),
        zones: BRANCH.zones.map((z) => ({
            ...z,
            reachable: !!reach[z.id],
            controlled: controlled(z.id, mask),
            groups: instances.filter((i) => i.zone === z.id && i.players > 0).length,
            objectives: BRANCH.objectives
                .filter((o) => o.zone === z.id)
                .map((o) => ({ id: o.id, label: o.label, optional: o.optional, done: isDone(mask, o.index), by: by.get(o.id) ?? null })),
        })),
        edges: BRANCH.edges.map((x) => ({ ...x, open: lockOpen(x.lock, mask) })),
        instances,
        my: { registered: !!reg, clan_tag: reg?.tag ?? null, group: myGroupNo, instance_id: mine?.id ?? null },
        journal: done.slice(0, 10).map((d) => ({ objective: d.objective_id, label: objectiveOf(d.objective_id)?.label ?? d.objective_id, by: by.get(d.objective_id) ?? null, at: d.accepted_at })),
    });
}
