// Clan raids « ASTERION, LE DÉVOREUR DE MONDES », phase 1 (migration 0141): the events and the
// registrations, the reminders and the notifications. The combat is in raidZones.ts (decided by
// the consensus of each instance's players); 2.68 (phase 3): the « rewards to claim » and the
// state of Asterion's body (each event's « body ») come from its validated results (raidRewards.ts).
//
// Two kinds of events:
//   - world events, scheduled by the game's administrators (ADMINISTRATION › Raids): a draft
//     until published, then announced to every clan; a clan takes part when its chief or an
//     officer registers it, then each member registers themselves;
//   - clan raids, scheduled by the chief or an officer of a clan, internal to that clan and to
//     the clans it invites to a raid in common (their chief or an officer answers).
// Every time is UTC (ISO 8601, « …Z »): the game converts to its own time zone (with its
// daylight saving time) and sends UTC; a time with an offset is refused. The states
// (inscription ouverte / programmé / imminent / en cours / terminé) are computed from UTC.
//
// The player is always the session's (never an id from the request); every write checks the
// role in its own statement. The rules two requests could race on are decided inside one
// statement: the last place (capacity of the event and of the clan), a clan registered twice
// (primary key), an invitation answered twice (WHERE status = 'pending'), a reminder sent twice
// (raid_reminders_sent, written in the same transaction as the notifications).

import { audit, requireAuth, type AuthInfo } from "./auth";
import { requireAdminRole } from "./adminRoles";
import { adminPresenceOf, countOnline, isAdminViewer, listPresenceOf } from "./friends";
import { Errors, json, log, nowIso, readJsonObject, type Ctx } from "./http";
import type { PlayerRow } from "./model";
import { notify } from "./notifications";
import { bodiesOf, rewardsOf } from "./raidRewards";
import { requireValidMap, resultSummaries } from "./raidWorld";
import { RULES, rateLimit } from "./ratelimit";
import { Validator } from "./validate";

export const RAID_FORMATS = {
    court: { min: 25, max: 30 },
    standard: { min: 45, max: 60 },
    legendaire: { min: 75, max: 90 },
} as const;
export type RaidFormat = keyof typeof RAID_FORMATS;
export const FORMAT_IDS = ["court", "standard", "legendaire"] as const;

/** The raids the game knows (one for now). Texts in French, shown as they are. */
export const RAIDS = {
    asterion: {
        id: "asterion",
        name: "Asterion",
        display_name: "ASTERION",
        display_title: "LE DÉVOREUR DE MONDES",
        description:
            "Un titan cosmique avale les planètes de la bordure. Les clans l'affronteront ensemble, branche par branche, sur son propre corps, jusqu'à son cœur.",
        rewards_text:
            "Pyroclast (bras droit) : badge « Main de Pyroclast » à sa chute ; Skullmaw, Dreadfang, Heartbreaker, Voidworm, Graviton, Titanus (crâne, bras gauche, torse, abdomen, jambes) : un badge chacun à leur chute ; titre « Assaillant d'Asterion » pour 5 min de combat, titres de performance (score mixte : dégâts, soutien, mécanismes, objectifs, présence).",
    },
} as const;
export type RaidId = keyof typeof RAIDS;

export const GROUP_SIZE = 8;
/** Reminders a player may choose (minutes before the start); 0 is the start itself, always sent. */
export const REMINDER_CHOICES = [1440, 60, 10] as const;
const TICK_OFFSETS = [1440, 60, 10, 0];
/** A due reminder is still sent this long after its time (a missed minute of the cron), never later. */
const REMINDER_GRACE_MS = 15 * 60000;
export const MAX_INSTRUCTIONS = 600;
export const MAX_REWARDS_TEXT = 300;
/** Clan raids to come at once per clan, invitations per clan raid. */
export const MAX_CLAN_RAIDS = 10;
export const MAX_INVITATIONS = 10;
/** A clan raid: between 15 minutes and 90 days ahead; a world event: up to a year. */
const MIN_AHEAD_MS = 15 * 60000;
const MAX_CLAN_AHEAD_MS = 90 * 86400000;
const MAX_WORLD_AHEAD_MS = 365 * 86400000;
/** The cron of the reminders (wrangler.jsonc), beside the daily maintenance. */
export const RAID_CRON = "* * * * *";

export type RaidState = "brouillon" | "inscription" | "programme" | "imminent" | "en_cours" | "termine" | "annule";

const ID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const PLAYER_ID_RE = /^[A-Za-z0-9-]{8,64}$/;
const UTC_RE = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})(?::(\d{2})(?:\.\d{1,3})?)?Z$/;
const TAG_RE = /^[A-Za-z0-9]{2,5}$/;

interface EventRow {
    id: string;
    kind: "world" | "clan";
    raid: string;
    format: RaidFormat;
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
    created_by: string | null;
    created_at: string;
    updated_at: string;
    // 2.69 (phase 5, migration 0147, raidWorld.ts): suspended by an administrator, late arrivals,
    // a catch-up session of an official world event, the map's latest check.
    suspended_at?: string | null;
    suspended_reason?: string;
    late_minutes?: number | null;
    catchup_of?: string | null;
    map_check?: string | null;
    org_tag?: string | null;
    org_name?: string | null;
    clans_count?: number;
    players_count?: number;
    my_clan_in?: number;
    my_clan_players?: number;
    me_in?: number;
    leader_name?: string | null;
}

interface MyClan {
    id: string;
    tag: string;
    name: string;
    role: "leader" | "officer" | "member";
}

// ------------------------------------------------------------------ rules

/** Milliseconds since 1970 of a UTC time « YYYY-MM-DDTHH:MM[:SS[.mmm]]Z »; null otherwise (an offset, an impossible date). */
export function parseUtc(s: string): number | null {
    const m = UTC_RE.exec(s);
    if (!m) return null;
    const [y, mo, d, h, mi, se] = [Number(m[1]), Number(m[2]), Number(m[3]), Number(m[4]), Number(m[5]), Number(m[6] ?? 0)];
    if (h > 23 || mi > 59 || se > 59) return null;
    const ms = Date.UTC(y, mo - 1, d, h, mi, se);
    const back = new Date(ms);
    if (back.getUTCFullYear() !== y || back.getUTCMonth() !== mo - 1 || back.getUTCDate() !== d) return null;
    return ms;
}

const iso = (ms: number) => new Date(ms).toISOString();

/** lock_at and ends_at of an event starting at `startMs`. */
export function timesOf(startMs: number, lockMinutes: number, format: RaidFormat): { starts_at: string; lock_at: string; ends_at: string } {
    return { starts_at: iso(startMs), lock_at: iso(startMs - lockMinutes * 60000), ends_at: iso(startMs + RAID_FORMATS[format].max * 60000) };
}

/** The state of an event at `now` (ISO UTC): every time compared in UTC. */
export function raidState(
    e: Pick<EventRow, "starts_at" | "lock_at" | "ends_at" | "registration_open" | "capacity" | "published_at" | "cancelled_at"> & { players_count?: number },
    now: string,
): RaidState {
    if (e.cancelled_at) return "annule";
    if (!e.published_at) return "brouillon";
    if (now >= e.ends_at) return "termine";
    if (now >= e.starts_at) return "en_cours";
    if (now >= e.lock_at) return "imminent";
    const full = e.capacity > 0 && (e.players_count ?? 0) >= e.capacity;
    return e.registration_open === 1 && !full ? "inscription" : "programme";
}

function definition(raid: string) {
    return RAIDS[raid as RaidId] ?? RAIDS.asterion;
}

function eventView(e: EventRow, now: string) {
    const def = definition(e.raid);
    const f = RAID_FORMATS[e.format] ?? RAID_FORMATS.standard;
    const mine = e.my_clan_players ?? 0;
    return {
        id: e.id,
        kind: e.kind,
        raid: e.raid,
        name: def.name,
        display_name: def.display_name,
        display_title: def.display_title,
        format: e.format,
        format_min: f.min,
        format_max: f.max,
        starts_at: e.starts_at,
        lock_at: e.lock_at,
        ends_at: e.ends_at,
        lock_minutes: e.lock_minutes,
        registration_open: e.registration_open === 1,
        capacity: e.capacity,
        clan_capacity: e.clan_capacity,
        difficulty: e.difficulty,
        state: raidState(e, now),
        published: e.published_at !== null,
        published_at: e.published_at,
        cancelled: e.cancelled_at !== null,
        organizer: e.clan_id ? { id: e.clan_id, tag: e.org_tag ?? "", name: e.org_name ?? "" } : null,
        clans: e.clans_count ?? 0,
        players: e.players_count ?? 0,
        my_clan: { registered: !!e.my_clan_in, players: mine, groups: Math.ceil(mine / GROUP_SIZE), leader: e.leader_name ?? null },
        registered: !!e.me_in,
        // 2.69 (phase 5): suspended by the administrators (nothing counts until the resumption), the
        // late arrivals (minutes after the start, null: until the end), a catch-up session.
        suspended: !!e.suspended_at,
        suspended_reason: e.suspended_at ? (e.suspended_reason ?? "") : "",
        late_minutes: e.late_minutes ?? null,
        catchup: !!e.catchup_of,
        catchup_of: e.catchup_of ?? null,
    };
}

/** The columns of an event with its counts, for the clan bound at ?1 and the player at ?2. */
const EVENT_SELECT = `SELECT e.*, oc.tag AS org_tag, oc.name AS org_name,
    (SELECT COUNT(*) FROM raid_clan_entries x WHERE x.event_id = e.id) AS clans_count,
    (SELECT COUNT(*) FROM raid_registrations x WHERE x.event_id = e.id) AS players_count,
    EXISTS (SELECT 1 FROM raid_clan_entries x WHERE x.event_id = e.id AND x.clan_id = ?1) AS my_clan_in,
    (SELECT COUNT(*) FROM raid_registrations x WHERE x.event_id = e.id AND x.clan_id = ?1) AS my_clan_players,
    EXISTS (SELECT 1 FROM raid_registrations x WHERE x.event_id = e.id AND x.player_id = ?2) AS me_in,
    (SELECT p.username FROM raid_clan_entries x JOIN players p ON p.id = x.leader_id WHERE x.event_id = e.id AND x.clan_id = ?1) AS leader_name
  FROM raid_events e LEFT JOIN clans oc ON oc.id = e.clan_id`;

/** The groups of 8 of a clan's roster: the raid leader leads the first one, the first registered each other one. */
export function groupsOf<T extends { id: string }>(roster: T[], leaderId: string | null) {
    const ordered = [...roster];
    const li = leaderId ? ordered.findIndex((p) => p.id === leaderId) : -1;
    if (li > 0) ordered.unshift(...ordered.splice(li, 1));
    const groups: { number: number; leader: T; members: T[] }[] = [];
    for (let at = 0; at < ordered.length; at += GROUP_SIZE) {
        const members = ordered.slice(at, at + GROUP_SIZE);
        groups.push({ number: groups.length + 1, leader: members[0]!, members });
    }
    return groups;
}

// ------------------------------------------------------------------ helpers

async function myClanOf(db: D1Database, playerId: string): Promise<MyClan | null> {
    return await db
        .prepare("SELECT c.id, c.tag, c.name, m.role FROM clan_members m JOIN clans c ON c.id = m.clan_id WHERE m.player_id = ?1")
        .bind(playerId)
        .first<MyClan>();
}

const isManager = (c: MyClan | null) => !!c && (c.role === "leader" || c.role === "officer");

function eventIdParam(ctx: Ctx): string {
    const id = ctx.params.eventId ?? "";
    if (!ID_RE.test(id)) throw RAID_NOT_FOUND();
    return id;
}

const RAID_NOT_FOUND = () => Errors.notFound("RAID_NOT_FOUND", "Raid introuvable.");
const NO_CLAN = () => Errors.forbidden("NO_CLAN", "Rejoignez un clan pour participer aux raids.");
const MANAGERS_ONLY = () => Errors.forbidden("NOT_CLAN_OFFICER", "Seuls le chef et les officiers du clan peuvent le faire.");
const LOCKED = (e: EventRow) =>
    Errors.conflict("RAID_LOCKED", `Les inscriptions sont verrouillées (${e.lock_minutes} min avant le début).`, { lock_at: e.lock_at });
const CANCELLED = () => Errors.conflict("RAID_CANCELLED", "Ce raid est annulé.");
const FINISHED = () => Errors.conflict("RAID_FINISHED", "Ce raid est terminé.");

/** An event with its counts, for this clan and player ("" for none). */
async function loadEvent(db: D1Database, id: string, clanId: string, me: string): Promise<EventRow | null> {
    return await db.prepare(`${EVENT_SELECT} WHERE e.id = ?3`).bind(clanId, me, id).first<EventRow>();
}

/**
 * The event as `clan` may see it: a published world event, or a clan raid of its own clan,
 * one it takes part in or one it was invited to; else « introuvable » (a raid is internal).
 */
async function visibleEvent(db: D1Database, id: string, clan: MyClan | null, me: string): Promise<EventRow> {
    const e = await loadEvent(db, id, clan?.id ?? "", me);
    if (!e) throw RAID_NOT_FOUND();
    if (e.kind === "world") {
        if (!e.published_at) throw RAID_NOT_FOUND();
        return e;
    }
    if (!clan) throw RAID_NOT_FOUND();
    if (e.clan_id === clan.id || e.my_clan_in) return e;
    const inv = await db.prepare("SELECT 1 AS x FROM raid_invitations WHERE event_id = ?1 AND clan_id = ?2").bind(id, clan.id).first();
    if (!inv) throw RAID_NOT_FOUND();
    return e;
}

function notifData(e: Pick<EventRow, "id" | "raid" | "format" | "kind" | "starts_at">, event: string, extra: Record<string, unknown> = {}) {
    return { event, event_id: e.id, raid_name: definition(e.raid).name, format: e.format, raid_kind: e.kind, starts_at: e.starts_at, ...extra };
}

/** One « raid » notification for some players (skipped for the actor, blocks and the kind turned off). */
async function notifyPlayers(ctx: Ctx, recipients: string[], actor: string | null, data: Record<string, unknown>): Promise<void> {
    const list = [...new Set(recipients)].filter((r) => r !== actor);
    try {
        for (let at = 0; at < list.length; at += 500)
            await ctx.env.DB.prepare(
                `INSERT INTO notifications (id, player_id, kind, actor_id, data, count, created_at)
                 SELECT lower(hex(randomblob(16))), r.value, 'raid', ?2, ?3, 1, ?4 FROM json_each(?1) r
                 WHERE EXISTS (SELECT 1 FROM players p WHERE p.id = r.value AND instr(',' || p.notifications_off || ',', ',raid,') = 0)
                   AND NOT EXISTS (SELECT 1 FROM player_blocks b WHERE (b.player_id = r.value AND b.blocked_id = ?2) OR (b.player_id = ?2 AND b.blocked_id = r.value))`,
            )
                .bind(JSON.stringify(list.slice(at, at + 500)), actor, JSON.stringify(data), nowIso())
                .run();
    } catch (e) {
        log("error", "raid_notify_failed", { error: e instanceof Error ? e.message : String(e) });
    }
}

/**
 * The members of a clan (`clanId`) or of every clan (null), once: a member who still has an
 * unread notification of the same `groupKey` gets nothing more. Returns how many were told.
 */
async function notifyClanMembers(ctx: Ctx, clanId: string | null, actor: string | null, data: Record<string, unknown>, groupKey: string | null): Promise<number> {
    try {
        const r = await ctx.env.DB.prepare(
            `INSERT INTO notifications (id, player_id, kind, actor_id, data, group_key, count, created_at)
             SELECT lower(hex(randomblob(16))), m.player_id, 'raid', ?2, ?3, ?4, 1, ?5 FROM clan_members m JOIN players p ON p.id = m.player_id
             WHERE (?1 IS NULL OR m.clan_id = ?1) AND m.player_id IS NOT ?2 AND instr(',' || p.notifications_off || ',', ',raid,') = 0
               AND (?4 IS NULL OR NOT EXISTS (SELECT 1 FROM notifications n WHERE n.player_id = m.player_id AND n.group_key = ?4 AND n.read_at IS NULL))
               AND NOT EXISTS (SELECT 1 FROM player_blocks b WHERE (b.player_id = m.player_id AND b.blocked_id = ?2) OR (b.player_id = ?2 AND b.blocked_id = m.player_id))`,
        )
            .bind(clanId, actor, JSON.stringify(data), groupKey, nowIso())
            .run();
        return r.meta.changes;
    } catch (e) {
        log("error", "raid_notify_failed", { error: e instanceof Error ? e.message : String(e) });
        return 0;
    }
}

/** The chief and the officers of a clan. */
async function managersOf(db: D1Database, clanId: string): Promise<string[]> {
    const r = await db.prepare("SELECT player_id FROM clan_members WHERE clan_id = ?1 AND role IN ('leader', 'officer')").bind(clanId).all<{ player_id: string }>();
    return r.results.map((x) => x.player_id);
}

/** Everyone registered to an event. */
async function registrantsOf(db: D1Database, eventId: string, clanId?: string): Promise<string[]> {
    const r = clanId
        ? await db.prepare("SELECT player_id FROM raid_registrations WHERE event_id = ?1 AND clan_id = ?2").bind(eventId, clanId).all<{ player_id: string }>()
        : await db.prepare("SELECT player_id FROM raid_registrations WHERE event_id = ?1").bind(eventId).all<{ player_id: string }>();
    return r.results.map((x) => x.player_id);
}

/**
 * A member (un)registered: their clan's chief, officers and raid leader are told, grouped (one
 * notification per event and direction while unread, its counter growing), unless they turned
 * these updates off in their raid reminders.
 */
async function notifyMemberUpdate(ctx: Ctx, e: EventRow, clanId: string, actor: string, direction: "in" | "out"): Promise<void> {
    const db = ctx.env.DB;
    const rows = await db
        .prepare(
            `SELECT DISTINCT x.pid FROM (
               SELECT player_id AS pid FROM clan_members WHERE clan_id = ?1 AND role IN ('leader', 'officer')
               UNION SELECT leader_id AS pid FROM raid_clan_entries WHERE event_id = ?2 AND clan_id = ?1 AND leader_id IS NOT NULL) x
             LEFT JOIN raid_prefs f ON f.player_id = x.pid WHERE COALESCE(f.member_updates, 1) = 1 AND x.pid <> ?3`,
        )
        .bind(clanId, e.id, actor)
        .all<{ pid: string }>();
    for (const r of rows.results)
        await notify(ctx, r.pid, "raid", {
            actor,
            data: notifData(e, direction === "in" ? "member_in" : "member_out"),
            groupKey: `raid:${direction}:${e.id}:${clanId}`,
        });
}

// ------------------------------------------------------------------ reads

/**
 * GET /raids: the tab RAIDS of CLANS. The next world events (anyone), and with a session the
 * player's clan: its raids, the invitations it received, its history, the unread raid
 * notifications and the player's reminders. The rewards to claim come with the combats.
 */
export async function overview(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const now = nowIso();
    const me = viewer?.player.id ?? "";
    const db = ctx.env.DB;
    const clan = me ? await myClanOf(db, me) : null;
    const c = clan?.id ?? "";
    const stmts = [
        db
            .prepare(`${EVENT_SELECT} WHERE e.kind = 'world' AND e.published_at IS NOT NULL AND e.cancelled_at IS NULL AND e.ends_at > ?3 ORDER BY e.starts_at, e.id LIMIT 5`)
            .bind(c, me, now),
        db
            .prepare(
                `${EVENT_SELECT} WHERE e.ends_at > ?3 AND e.cancelled_at IS NULL AND e.published_at IS NOT NULL
                   AND (e.clan_id = ?1 OR EXISTS (SELECT 1 FROM raid_clan_entries x WHERE x.event_id = e.id AND x.clan_id = ?1))
                 ORDER BY e.starts_at, e.id LIMIT 20`,
            )
            .bind(c, me, now),
        db
            .prepare(
                `SELECT e.*, oc.tag AS org_tag, oc.name AS org_name, i.created_at AS invited_at, b.username AS by_name FROM raid_invitations i
                 JOIN raid_events e ON e.id = i.event_id LEFT JOIN clans oc ON oc.id = e.clan_id LEFT JOIN players b ON b.id = i.by_player
                 WHERE i.clan_id = ?1 AND i.status = 'pending' AND e.cancelled_at IS NULL AND e.lock_at > ?2 ORDER BY e.starts_at LIMIT 20`,
            )
            .bind(c, now),
        db
            .prepare(
                `${EVENT_SELECT} WHERE (e.ends_at <= ?3 OR e.cancelled_at IS NOT NULL) AND e.published_at IS NOT NULL
                   AND (EXISTS (SELECT 1 FROM raid_clan_entries x WHERE x.event_id = e.id AND x.clan_id = ?1)
                        OR (e.kind = 'world' AND e.catchup_of IS NULL AND e.cancelled_at IS NULL))
                 ORDER BY e.starts_at DESC, e.id LIMIT 10`,
            )
            .bind(c, me, now),
        db.prepare("SELECT COUNT(*) AS n FROM notifications WHERE player_id = ?1 AND kind = 'raid' AND read_at IS NULL").bind(me),
        db.prepare("SELECT reminders, member_updates FROM raid_prefs WHERE player_id = ?1").bind(me),
    ];
    const [world, mine, invites, history, unread, prefs] = await db.batch(stmts);
    const inv = invites!.results as (EventRow & { invited_at: string; by_name: string | null })[];
    // 2.68: Asterion's body in each event (validated results only), my rewards to claim.
    const listed = [world, mine, history].flatMap((r) => (r!.results as EventRow[]).map((e) => e.id));
    const bodies = await bodiesOf(db, listed);
    // 2.69: the official result of the world events closed (frozen), with my clan's rank.
    const results = await resultSummaries(db, listed, c);
    const withBody = (e: EventRow) => ({ ...eventView(e, now), ...(bodies.get(e.id) ?? { body: {}, effects: [] }), result: results.get(e.id) ?? null });
    const rewards = await rewardsOf(db, me);
    return json(ctx, {
        server_time: now,
        raids: Object.values(RAIDS).map((r) => ({ ...r, formats: RAID_FORMATS })),
        clan: clan ? { id: clan.id, tag: clan.tag, name: clan.name, role: clan.role } : null,
        world: (world!.results as EventRow[]).map(withBody),
        clan_events: (mine!.results as EventRow[]).map(withBody),
        invitations: inv.map((e) => ({ event: eventView(e, now), by: e.by_name, created_at: e.invited_at })),
        // 2.68: granted by the server from validated results only, to claim (POST /raids/rewards/claim).
        rewards: rewards.pending,
        rewards_claimed: rewards.claimed,
        history: (history!.results as EventRow[]).map(withBody),
        unread: (unread!.results[0] as { n: number } | undefined)?.n ?? 0,
        prefs: prefsJson(prefs!.results[0] as PrefsRow | undefined),
    });
}

/**
 * GET /raids/events/{eventId}: the sheet of an event. My clan's entry (raid leader,
 * instructions), its roster and groups of 8 (only my clan's members see them), the clans
 * taking part, the invitations of a clan raid. 2.72: each registrant's presence (online, away,
 * offline; a hidden one reads offline) and `roster_online` (the « X » of « X/Y en ligne »), for
 * the same viewers as the roster, and only for the registrants still members of their clan.
 */
export async function getEvent(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const now = nowIso();
    const id = eventIdParam(ctx);
    const me = viewer?.player.id ?? "";
    const db = ctx.env.DB;
    const clan = me ? await myClanOf(db, me) : null;
    const e = await visibleEvent(db, id, clan, me);
    const c = clan?.id ?? "";
    const [entryR, rosterR, partsR, invR] = await db.batch([
        db
            .prepare(
                `SELECT x.leader_id, l.username AS leader_name, x.instructions, x.registered_by, r.username AS registered_by_name, x.created_at, x.updated_at
                 FROM raid_clan_entries x LEFT JOIN players l ON l.id = x.leader_id LEFT JOIN players r ON r.id = x.registered_by
                 WHERE x.event_id = ?1 AND x.clan_id = ?2`,
            )
            .bind(id, c),
        db
            .prepare(
                `SELECT p.id, p.username, p.avatar, r.created_at, p.presence_state, p.presence_at, p.presence_visible, p.profile_visibility,
                        EXISTS (SELECT 1 FROM clan_members m WHERE m.clan_id = ?2 AND m.player_id = p.id) AS in_clan
                 FROM raid_registrations r JOIN players p ON p.id = r.player_id
                 WHERE r.event_id = ?1 AND r.clan_id = ?2 ORDER BY r.created_at, p.username LIMIT 200`,
            )
            .bind(id, c),
        db
            .prepare(
                `SELECT c.id, c.tag, c.name, (SELECT COUNT(*) FROM raid_registrations r WHERE r.event_id = x.event_id AND r.clan_id = x.clan_id) AS players
                 FROM raid_clan_entries x JOIN clans c ON c.id = x.clan_id WHERE x.event_id = ?1 ORDER BY players DESC, c.tag LIMIT 50`,
            )
            .bind(id),
        db
            .prepare("SELECT c.id, c.tag, c.name, i.status, i.created_at FROM raid_invitations i JOIN clans c ON c.id = i.clan_id WHERE i.event_id = ?1 ORDER BY i.created_at")
            .bind(id),
    ]);
    type Entry = {
        leader_id: string | null;
        leader_name: string | null;
        instructions: string;
        registered_by: string | null;
        registered_by_name: string | null;
        created_at: string;
        updated_at: string;
    };
    const entry = (entryR!.results[0] as Entry | undefined) ?? null;
    type Registrant = { id: string; username: string; avatar: string | null; created_at: string; in_clan: number; profile_visibility?: string } & Pick<
        PlayerRow,
        "presence_state" | "presence_at" | "presence_visible"
    >;
    const roster = entry ? (rosterR!.results as Registrant[]) : [];
    // 2.72: the registrants' presence, to the members of their clan who see the roster (the
    // viewer is one: the roster is the viewer's clan's), from the game's heartbeat.
    const nowMs = Date.now();
    // 2.75: an administrator (moderation) sees the real presence, the hidden ones marked « masqué ».
    const admin = isAdminViewer(viewer);
    const adminView = roster.map((p) => (admin && clan && p.in_clan === 1 ? adminPresenceOf(p, nowMs) : null));
    const presence = roster.map((p, i) => adminView[i]?.presence ?? (clan && p.in_clan === 1 ? listPresenceOf(p, nowMs) : null));
    const invitations = invR!.results as { id: string; tag: string; name: string; status: string; created_at: string }[];
    const organizer = !!clan && e.clan_id === clan.id;
    const mineInv = clan ? invitations.find((i) => i.id === clan.id) : undefined;
    const def = definition(e.raid);
    const body = (await bodiesOf(db, [e.id])).get(e.id) ?? { body: {}, effects: [] };
    const result = (await resultSummaries(db, [e.id], c)).get(e.id) ?? null; // 2.69: the official result once frozen
    return json(ctx, {
        server_time: now,
        event: { ...eventView(e, now), ...body, result },
        description: def.description,
        rewards_text: e.rewards_text || def.rewards_text,
        my: {
            clan: clan ? { id: clan.id, tag: clan.tag, name: clan.name, role: clan.role } : null,
            entry: entry
                ? {
                      leader: entry.leader_id ? { id: entry.leader_id, username: entry.leader_name } : null,
                      instructions: entry.instructions,
                      registered_by: entry.registered_by ? { id: entry.registered_by, username: entry.registered_by_name } : null,
                      updated_at: entry.updated_at,
                  }
                : null,
            registered: !!e.me_in,
            can_manage: isManager(clan),
            can_call: isManager(clan) || (!!entry && entry.leader_id === me && me !== ""),
            is_organizer: organizer,
        },
        roster: roster.map((p, i) => ({
            id: p.id,
            username: p.username,
            avatar: p.avatar,
            registered_at: p.created_at,
            ...(presence[i] ? { presence: presence[i] } : {}),
            ...(adminView[i] ? { presence_hidden: adminView[i]!.presence_hidden, profile_hidden: p.profile_visibility === "private" } : {}),
        })),
        ...(entry && clan ? { roster_online: countOnline(presence) } : {}),
        groups: groupsOf(roster, entry?.leader_id ?? null).map((g) => ({
            number: g.number,
            leader: { id: g.leader.id, username: g.leader.username },
            members: g.members.map((p) => ({ id: p.id, username: p.username })),
        })),
        participants: (partsR!.results as { id: string; tag: string; name: string; players: number }[]).map((p) => ({ ...p })),
        // The invitations of a clan raid: all of them for its organizer, mine for an invited clan.
        invitations: organizer ? invitations.map((i) => ({ clan: { id: i.id, tag: i.tag, name: i.name }, status: i.status, created_at: i.created_at })) : [],
        invitation: mineInv && !organizer ? { status: mineInv.status, from: e.clan_id ? { id: e.clan_id, tag: e.org_tag, name: e.org_name } : null } : null,
    });
}

// ------------------------------------------------------------------ preferences

interface PrefsRow {
    reminders: string;
    member_updates: number;
}

function prefsJson(r: PrefsRow | undefined) {
    const list = (r?.reminders ?? "1440,60,10")
        .split(",")
        .map(Number)
        .filter((n) => (REMINDER_CHOICES as readonly number[]).includes(n));
    return { reminders: [...new Set(list)].sort((a, b) => b - a), member_updates: (r?.member_updates ?? 1) === 1 };
}

/** GET /raids/prefs */
export async function getPrefs(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const r = await ctx.env.DB.prepare("SELECT reminders, member_updates FROM raid_prefs WHERE player_id = ?1").bind(auth.player.id).first<PrefsRow>();
    return json(ctx, { prefs: prefsJson(r ?? undefined) });
}

/** PUT /raids/prefs {reminders?: [1440, 60, 10], member_updates?: bool} */
export async function putPrefs(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.raidWrite, "player:" + auth.player.id);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["reminders", "member_updates"]);
    let reminders: number[] | undefined;
    if (body.reminders !== undefined) {
        if (!Array.isArray(body.reminders) || body.reminders.some((n) => !(REMINDER_CHOICES as readonly unknown[]).includes(n)))
            v.fail("reminders", "liste parmi 1440, 60 et 10 (minutes avant le début)");
        else reminders = [...new Set(body.reminders as number[])].sort((a, b) => b - a);
    }
    const members = v.bool("member_updates", { optional: true });
    v.done();
    const db = ctx.env.DB;
    const cur = prefsJson((await db.prepare("SELECT reminders, member_updates FROM raid_prefs WHERE player_id = ?1").bind(auth.player.id).first<PrefsRow>()) ?? undefined);
    const next = { reminders: reminders ?? cur.reminders, member_updates: members ?? cur.member_updates };
    await db
        .prepare(
            `INSERT INTO raid_prefs (player_id, reminders, member_updates) VALUES (?1, ?2, ?3)
             ON CONFLICT (player_id) DO UPDATE SET reminders = excluded.reminders, member_updates = excluded.member_updates`,
        )
        .bind(auth.player.id, next.reminders.join(","), next.member_updates ? 1 : 0)
        .run();
    return json(ctx, { prefs: next });
}

// ------------------------------------------------------------------ clan raids

/** The start of a form: UTC only (the game converts its local time), between `minAhead` and `maxAhead` from now. */
function readStart(v: Validator, raw: string | undefined, minAheadMs: number, maxAheadMs: number): number | null {
    if (raw === undefined) return null;
    const ms = parseUtc(raw);
    if (ms === null) {
        v.fail("starts_at", "heure UTC attendue (AAAA-MM-JJTHH:MM:SSZ) : le jeu convertit l'heure locale");
        return null;
    }
    const now = Date.now();
    if (ms < now + minAheadMs) v.fail("starts_at", `au moins ${Math.round(minAheadMs / 60000)} minutes à l'avance`);
    else if (ms > now + maxAheadMs) v.fail("starts_at", `au plus ${Math.round(maxAheadMs / 86400000)} jours à l'avance`);
    return ms;
}

/**
 * POST /raids/events {raid?, format, starts_at, capacity, lock_minutes?, instructions?, leader_id?}:
 * the chief or an officer schedules a raid of their clan (its members are told).
 */
export async function scheduleClanRaid(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.raidSchedule, "player:" + me);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["raid", "format", "starts_at", "capacity", "lock_minutes", "instructions", "leader_id"]);
    const raid = v.str("raid", { max: 32, optional: true }) ?? "asterion";
    if (!(raid in RAIDS)) v.fail("raid", "raid inconnu");
    const format = v.str("format", { max: 16 }) as RaidFormat | undefined;
    if (format !== undefined && !FORMAT_IDS.includes(format)) v.fail("format", "court, standard ou legendaire");
    const capacity = v.int("capacity", { min: 2, max: 50 });
    const lock = v.int("lock_minutes", { min: 0, max: 120, optional: true }) ?? 30;
    const instructions = v.str("instructions", { max: MAX_INSTRUCTIONS, optional: true, trim: true, multiline: true }) ?? "";
    const leaderId = v.str("leader_id", { max: 64, pattern: PLAYER_ID_RE, optional: true });
    const start = readStart(v, v.str("starts_at", { max: 40 }), Math.max(MIN_AHEAD_MS, (lock + 5) * 60000), MAX_CLAN_AHEAD_MS);
    v.done();
    const db = ctx.env.DB;
    const clan = await myClanOf(db, me);
    if (!clan) throw NO_CLAN();
    if (!isManager(clan)) throw MANAGERS_ONLY();
    if (leaderId) {
        const ok = await db.prepare("SELECT 1 AS x FROM clan_members WHERE player_id = ?1 AND clan_id = ?2").bind(leaderId, clan.id).first();
        if (!ok) throw Errors.validation([{ field: "leader_id", message: "le chef de raid doit être un membre du clan" }]);
    }
    const now = nowIso();
    const t = timesOf(start!, lock, format!);
    const id = crypto.randomUUID();
    // The role and the number of raids to come are checked by the INSERT itself.
    const r = await db.batch([
        db
            .prepare(
                `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity,
                   difficulty, rewards_text, published_at, created_by, created_at, updated_at)
                 SELECT ?1, 'clan', ?2, ?3, ?4, ?5, ?6, ?7, ?8, 1, ?9, 50, 3, '', ?10, ?11, ?10, ?10
                 WHERE EXISTS (SELECT 1 FROM clan_members m WHERE m.player_id = ?11 AND m.clan_id = ?4 AND m.role IN ('leader', 'officer'))
                   AND (SELECT COUNT(*) FROM raid_events x WHERE x.clan_id = ?4 AND x.cancelled_at IS NULL AND x.ends_at > ?10) < ?12`,
            )
            .bind(id, raid, format, clan.id, t.starts_at, lock, t.lock_at, t.ends_at, capacity, now, me, MAX_CLAN_RAIDS),
        db
            .prepare(
                `INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at)
                 SELECT ?1, ?2, ?3, ?4, ?5, ?6, ?6 WHERE EXISTS (SELECT 1 FROM raid_events WHERE id = ?1)`,
            )
            .bind(id, clan.id, me, leaderId ?? me, instructions, now),
    ]);
    if (!r[0]!.meta.changes) {
        if (!isManager(await myClanOf(db, me))) throw MANAGERS_ONLY();
        throw Errors.conflict("TOO_MANY_RAIDS", `Votre clan a déjà ${MAX_CLAN_RAIDS} raids à venir : annulez-en un ou attendez la fin d'un raid.`);
    }
    const e = (await loadEvent(db, id, clan.id, me))!;
    await notifyClanMembers(ctx, clan.id, me, notifData(e, "scheduled", { tag: clan.tag }), null);
    if (leaderId && leaderId !== me) await notifyPlayers(ctx, [leaderId], me, notifData(e, "leader", { tag: clan.tag }));
    return json(ctx, { event: eventView(e, now) }, { status: 201 });
}

/** DELETE /raids/events/{eventId}: the organizer's chief or an officer cancels a clan raid (everyone concerned is told). */
export async function cancelClanRaid(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.raidWrite, "player:" + me);
    const id = eventIdParam(ctx);
    const db = ctx.env.DB;
    const clan = await myClanOf(db, me);
    const e = await visibleEvent(db, id, clan, me);
    if (e.kind !== "clan" || e.clan_id !== clan?.id) throw Errors.forbidden("NOT_ORGANIZER", "Seul le clan qui organise ce raid peut l'annuler.");
    const now = nowIso();
    const r = await db
        .prepare(
            `UPDATE raid_events SET cancelled_at = ?2, updated_at = ?2 WHERE id = ?1 AND kind = 'clan' AND cancelled_at IS NULL AND ends_at > ?2
               AND EXISTS (SELECT 1 FROM clan_members m WHERE m.player_id = ?3 AND m.clan_id = raid_events.clan_id AND m.role IN ('leader', 'officer'))`,
        )
        .bind(id, now, me)
        .run();
    if (!r.meta.changes) {
        if (e.cancelled_at) throw CANCELLED();
        if (now >= e.ends_at) throw FINISHED();
        throw MANAGERS_ONLY();
    }
    const invited = await db.prepare("SELECT clan_id FROM raid_invitations WHERE event_id = ?1 AND status = 'pending'").bind(id).all<{ clan_id: string }>();
    const told = [...(await registrantsOf(db, id))];
    for (const i of invited.results) told.push(...(await managersOf(db, i.clan_id)));
    await notifyPlayers(ctx, told, me, notifData(e, "cancelled", { tag: clan.tag }));
    return json(ctx, { cancelled: true });
}

/** POST /raids/events/{eventId}/invites {tag}: the organizer invites another clan to a raid in common. */
export async function inviteClan(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.raidWrite, "player:" + me);
    const id = eventIdParam(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["tag"]);
    const tag = v.str("tag", { max: 5, pattern: TAG_RE });
    v.done();
    const db = ctx.env.DB;
    const clan = await myClanOf(db, me);
    const e = await visibleEvent(db, id, clan, me);
    if (e.kind !== "clan" || e.clan_id !== clan?.id) throw Errors.forbidden("NOT_ORGANIZER", "Seul le clan qui organise ce raid peut inviter d'autres clans.");
    if (!isManager(clan)) throw MANAGERS_ONLY();
    const now = nowIso();
    if (e.cancelled_at) throw CANCELLED();
    if (now >= e.lock_at) throw LOCKED(e);
    const target = await db.prepare("SELECT id, tag, name FROM clans WHERE tag_normalized = ?1").bind(tag!.toUpperCase()).first<{ id: string; tag: string; name: string }>();
    if (!target) throw Errors.notFound("CLAN_NOT_FOUND", `Aucun clan ne porte le tag [${tag}].`);
    if (target.id === clan.id) throw Errors.validation([{ field: "tag", message: "c'est votre propre clan" }]);
    const r = await db
        .prepare(
            `INSERT INTO raid_invitations (event_id, clan_id, by_player, status, created_at)
             SELECT ?1, ?2, ?3, 'pending', ?4 WHERE (SELECT COUNT(*) FROM raid_invitations WHERE event_id = ?1) < ?5
               AND EXISTS (SELECT 1 FROM clan_members m WHERE m.player_id = ?3 AND m.clan_id = ?6 AND m.role IN ('leader', 'officer'))
             ON CONFLICT (event_id, clan_id) DO UPDATE SET status = 'pending', by_player = excluded.by_player, created_at = excluded.created_at, answered_at = NULL
               WHERE raid_invitations.status = 'declined'`,
        )
        .bind(id, target.id, me, now, MAX_INVITATIONS, clan.id)
        .run();
    if (!r.meta.changes) {
        const cur = await db.prepare("SELECT status FROM raid_invitations WHERE event_id = ?1 AND clan_id = ?2").bind(id, target.id).first<{ status: string }>();
        if (cur) throw Errors.conflict("ALREADY_INVITED", cur.status === "accepted" ? `[${target.tag}] participe déjà à ce raid.` : `[${target.tag}] est déjà invité.`);
        throw Errors.conflict("TOO_MANY_INVITATIONS", `${MAX_INVITATIONS} clans invités au plus par raid.`);
    }
    await notifyPlayers(ctx, await managersOf(db, target.id), me, notifData(e, "invite", { tag: target.tag, from_tag: clan.tag }));
    return json(ctx, { invited: { id: target.id, tag: target.tag, name: target.name } }, { status: 201 });
}

/** POST /raids/events/{eventId}/invitation {accept}: an invited clan's chief or officer answers (accepted: the clan takes part). */
export async function answerInvitation(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.raidWrite, "player:" + me);
    const id = eventIdParam(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["accept"]);
    const accept = v.bool("accept");
    v.done();
    const db = ctx.env.DB;
    const clan = await myClanOf(db, me);
    if (!clan) throw NO_CLAN();
    const e = await visibleEvent(db, id, clan, me);
    const now = nowIso();
    const status = accept ? "accepted" : "declined";
    const r = await db.batch([
        db
            .prepare(
                `UPDATE raid_invitations SET status = ?3, answered_at = ?4 WHERE event_id = ?1 AND clan_id = ?2 AND status = 'pending'
                   AND EXISTS (SELECT 1 FROM clan_members m WHERE m.player_id = ?5 AND m.clan_id = ?2 AND m.role IN ('leader', 'officer'))
                   AND EXISTS (SELECT 1 FROM raid_events x WHERE x.id = ?1 AND x.cancelled_at IS NULL AND ?4 < x.lock_at)`,
            )
            .bind(id, clan.id, status, now, me),
        db
            .prepare(
                `INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at)
                 SELECT ?1, ?2, ?3, ?3, '', ?4, ?4 WHERE ?5 = 1 AND EXISTS (SELECT 1 FROM raid_invitations WHERE event_id = ?1 AND clan_id = ?2 AND status = 'accepted')
                 ON CONFLICT (event_id, clan_id) DO NOTHING`,
            )
            .bind(id, clan.id, me, now, accept ? 1 : 0),
    ]);
    if (!r[0]!.meta.changes) {
        const cur = await db.prepare("SELECT status FROM raid_invitations WHERE event_id = ?1 AND clan_id = ?2").bind(id, clan.id).first<{ status: string }>();
        if (!cur) throw Errors.notFound("INVITATION_NOT_FOUND", "Votre clan n'est pas invité à ce raid.");
        if (!isManager(clan)) throw MANAGERS_ONLY();
        if (cur.status !== "pending") throw Errors.conflict("ALREADY_ANSWERED", "Le clan a déjà répondu à cette invitation.");
        if (e.cancelled_at) throw CANCELLED();
        throw LOCKED(e);
    }
    await notifyPlayers(ctx, await managersOf(db, e.clan_id!), me, notifData(e, accept ? "invite_accepted" : "invite_declined", { tag: clan.tag }));
    if (accept) await notifyClanMembers(ctx, clan.id, me, notifData(e, "joined", { tag: clan.tag }), null);
    return json(ctx, { answered: status });
}

// ------------------------------------------------------------------ clans and players

/** POST /raids/events/{eventId}/clan: the chief or an officer registers their clan to a world event. */
export async function registerClan(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.raidWrite, "player:" + me);
    const id = eventIdParam(ctx);
    const db = ctx.env.DB;
    const clan = await myClanOf(db, me);
    if (!clan) throw NO_CLAN();
    const e = await visibleEvent(db, id, clan, me);
    if (e.kind !== "world") throw Errors.conflict("NOT_WORLD_EVENT", "Un raid de clan se rejoint sur invitation du clan qui l'organise.");
    const now = nowIso();
    const r = await db
        .prepare(
            `INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at)
             SELECT e.id, m.clan_id, ?2, ?2, '', ?3, ?3 FROM raid_events e JOIN clan_members m ON m.player_id = ?2 AND m.role IN ('leader', 'officer')
             WHERE e.id = ?1 AND e.kind = 'world' AND e.published_at IS NOT NULL AND e.cancelled_at IS NULL AND e.registration_open = 1 AND ?3 < e.lock_at
             ON CONFLICT (event_id, clan_id) DO NOTHING`,
        )
        .bind(id, me, now)
        .run();
    if (!r.meta.changes) {
        if (e.my_clan_in || (await db.prepare("SELECT 1 AS x FROM raid_clan_entries WHERE event_id = ?1 AND clan_id = ?2").bind(id, clan.id).first()))
            return json(ctx, { registered: true, already: true });
        if (!isManager(clan)) throw MANAGERS_ONLY();
        if (e.cancelled_at) throw CANCELLED();
        if (now >= e.lock_at) throw LOCKED(e);
        throw Errors.conflict("REGISTRATION_CLOSED", "Les inscriptions de cet événement sont fermées.");
    }
    await notifyClanMembers(ctx, clan.id, me, notifData(e, "clan_registered", { tag: clan.tag }), null);
    return json(ctx, { registered: true }, { status: 201 });
}

/** DELETE /raids/events/{eventId}/clan: the clan withdraws (world event, or a raid in common it joined); its registrations go with it. */
export async function withdrawClan(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.raidWrite, "player:" + me);
    const id = eventIdParam(ctx);
    const db = ctx.env.DB;
    const clan = await myClanOf(db, me);
    if (!clan) throw NO_CLAN();
    const e = await visibleEvent(db, id, clan, me);
    if (e.clan_id === clan.id) throw Errors.conflict("ORGANIZER", "Le clan qui organise un raid l'annule au lieu de s'en retirer.");
    if (!isManager(clan)) throw MANAGERS_ONLY();
    const now = nowIso();
    if (now >= e.lock_at) throw LOCKED(e);
    const told = await registrantsOf(db, id, clan.id);
    const r = await db.batch([
        db
            .prepare(
                `DELETE FROM raid_clan_entries WHERE event_id = ?1 AND clan_id = ?2
                   AND EXISTS (SELECT 1 FROM raid_events x WHERE x.id = ?1 AND ?3 < x.lock_at AND x.clan_id IS NOT ?2)
                   AND EXISTS (SELECT 1 FROM clan_members m WHERE m.player_id = ?4 AND m.clan_id = ?2 AND m.role IN ('leader', 'officer'))`,
            )
            .bind(id, clan.id, now, me),
        db.prepare("UPDATE raid_invitations SET status = 'declined', answered_at = ?3 WHERE event_id = ?1 AND clan_id = ?2 AND status = 'accepted'").bind(id, clan.id, now),
    ]);
    if (!r[0]!.meta.changes) throw Errors.notFound("CLAN_NOT_REGISTERED", "Votre clan n'est pas inscrit à ce raid.");
    await notifyPlayers(ctx, told, me, notifData(e, "withdrawn", { tag: clan.tag }));
    return json(ctx, { withdrawn: true });
}

/** PATCH /raids/events/{eventId}/clan {leader_id?, instructions?}: the clan's raid leader and internal instructions (chief, officers). */
export async function updateEntry(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.raidWrite, "player:" + me);
    const id = eventIdParam(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["leader_id", "instructions"]);
    const setLeader = body.leader_id !== undefined;
    const leaderId = body.leader_id === null ? null : (v.str("leader_id", { max: 64, pattern: PLAYER_ID_RE, optional: true }) ?? null);
    const instructions = v.str("instructions", { max: MAX_INSTRUCTIONS, optional: true, trim: true, multiline: true });
    if (!setLeader && instructions === undefined) v.fail("body", "leader_id ou instructions");
    v.done();
    const db = ctx.env.DB;
    const clan = await myClanOf(db, me);
    if (!clan) throw NO_CLAN();
    const e = await visibleEvent(db, id, clan, me);
    if (!e.my_clan_in) throw Errors.notFound("CLAN_NOT_REGISTERED", "Votre clan n'est pas inscrit à ce raid.");
    if (!isManager(clan)) throw MANAGERS_ONLY();
    const now = nowIso();
    if (e.cancelled_at) throw CANCELLED();
    if (now >= e.ends_at) throw FINISHED();
    if (leaderId) {
        const ok = await db.prepare("SELECT 1 AS x FROM clan_members WHERE player_id = ?1 AND clan_id = ?2").bind(leaderId, clan.id).first();
        if (!ok) throw Errors.validation([{ field: "leader_id", message: "le chef de raid doit être un membre du clan" }]);
    }
    const before = await db.prepare("SELECT leader_id FROM raid_clan_entries WHERE event_id = ?1 AND clan_id = ?2").bind(id, clan.id).first<{ leader_id: string | null }>();
    const r = await db
        .prepare(
            `UPDATE raid_clan_entries SET leader_id = CASE WHEN ?3 = 1 THEN ?4 ELSE leader_id END, instructions = COALESCE(?5, instructions), updated_at = ?6
             WHERE event_id = ?1 AND clan_id = ?2
               AND EXISTS (SELECT 1 FROM clan_members m WHERE m.player_id = ?7 AND m.clan_id = ?2 AND m.role IN ('leader', 'officer'))`,
        )
        .bind(id, clan.id, setLeader ? 1 : 0, leaderId, instructions ?? null, now, me)
        .run();
    if (!r.meta.changes) throw MANAGERS_ONLY();
    if (setLeader && leaderId && leaderId !== before?.leader_id) await notifyPlayers(ctx, [leaderId], me, notifData(e, "leader", { tag: clan.tag }));
    return json(ctx, { updated: true });
}

/** POST /raids/events/{eventId}/register: the player registers themselves, for their clan (registered first). */
export async function registerMe(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.raidWrite, "player:" + me);
    const id = eventIdParam(ctx);
    const db = ctx.env.DB;
    const clan = await myClanOf(db, me);
    if (!clan) throw NO_CLAN();
    const e = await visibleEvent(db, id, clan, me);
    if (e.me_in) return json(ctx, { registered: true, already: true });
    const now = nowIso();
    // 2.69 (phase 5): a catch-up session needs no chief: a member's registration enters their clan.
    if (e.catchup_of && !e.my_clan_in)
        await db
            .prepare(
                `INSERT INTO raid_clan_entries (event_id, clan_id, registered_by, leader_id, instructions, created_at, updated_at)
                 SELECT e.id, m.clan_id, ?2, NULL, '', ?3, ?3 FROM raid_events e JOIN clan_members m ON m.player_id = ?2
                 WHERE e.id = ?1 AND e.catchup_of IS NOT NULL AND e.cancelled_at IS NULL AND ?3 < e.lock_at
                 ON CONFLICT (event_id, clan_id) DO NOTHING`,
            )
            .bind(id, me, now)
            .run();
    // Every condition, the capacity of the event and of the clan included, in the INSERT itself.
    const r = await db
        .prepare(
            `INSERT INTO raid_registrations (event_id, player_id, clan_id, created_at)
             SELECT e.id, ?2, m.clan_id, ?3 FROM raid_events e JOIN clan_members m ON m.player_id = ?2
             JOIN raid_clan_entries x ON x.event_id = e.id AND x.clan_id = m.clan_id
             WHERE e.id = ?1 AND e.published_at IS NOT NULL AND e.cancelled_at IS NULL AND e.registration_open = 1 AND ?3 < e.lock_at
               AND (e.capacity = 0 OR (SELECT COUNT(*) FROM raid_registrations r WHERE r.event_id = e.id) < e.capacity)
               AND (SELECT COUNT(*) FROM raid_registrations r WHERE r.event_id = e.id AND r.clan_id = m.clan_id) < e.clan_capacity
             ON CONFLICT (event_id, player_id) DO NOTHING`,
        )
        .bind(id, me, now)
        .run();
    if (!r.meta.changes) {
        const fresh = (await loadEvent(db, id, clan.id, me))!;
        if (fresh.me_in) return json(ctx, { registered: true, already: true });
        if (fresh.cancelled_at) throw CANCELLED();
        if (now >= fresh.lock_at) throw LOCKED(fresh);
        if (!fresh.my_clan_in)
            throw Errors.conflict(
                "CLAN_NOT_REGISTERED",
                fresh.kind === "world" ? "Votre clan n'est pas inscrit : le chef ou un officier l'inscrit d'abord." : "Votre clan ne participe pas à ce raid.",
            );
        if (fresh.registration_open !== 1) throw Errors.conflict("REGISTRATION_CLOSED", "Les inscriptions de cet événement sont fermées.");
        if ((fresh.my_clan_players ?? 0) >= fresh.clan_capacity) throw Errors.conflict("CLAN_FULL", `Votre clan a déjà ${fresh.clan_capacity} inscrits : c'est complet.`);
        throw Errors.conflict("RAID_FULL", "Ce raid est complet.");
    }
    await notifyMemberUpdate(ctx, e, clan.id, me, "in");
    const after = (await loadEvent(db, id, clan.id, me))!;
    return json(ctx, { registered: true, event: eventView(after, now) }, { status: 201 });
}

/** DELETE /raids/events/{eventId}/register: the player unregisters, until the lock time. */
export async function unregisterMe(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.raidWrite, "player:" + me);
    const id = eventIdParam(ctx);
    const db = ctx.env.DB;
    const now = nowIso();
    const reg = await db.prepare("SELECT clan_id FROM raid_registrations WHERE event_id = ?1 AND player_id = ?2").bind(id, me).first<{ clan_id: string }>();
    if (!reg) throw Errors.notFound("NOT_REGISTERED", "Vous n'êtes pas inscrit à ce raid.");
    const r = await db
        .prepare(
            `DELETE FROM raid_registrations WHERE event_id = ?1 AND player_id = ?2
               AND EXISTS (SELECT 1 FROM raid_events e WHERE e.id = ?1 AND ?3 < e.lock_at AND e.cancelled_at IS NULL)`,
        )
        .bind(id, me, now)
        .run();
    const e = (await loadEvent(db, id, reg.clan_id, me))!;
    if (!r.meta.changes) {
        if (e.cancelled_at) throw CANCELLED();
        throw LOCKED(e);
    }
    await notifyMemberUpdate(ctx, e, reg.clan_id, me, "out");
    return json(ctx, { registered: false, event: eventView(e, now) });
}

/**
 * POST /raids/events/{eventId}/call: « Inviter des membres… » — the members of my clan not
 * registered yet are asked to register (chief, officers, raid leader). Once per member while
 * the previous call is unread.
 */
export async function callMembers(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.raidCall, "player:" + me);
    const id = eventIdParam(ctx);
    const db = ctx.env.DB;
    const clan = await myClanOf(db, me);
    if (!clan) throw NO_CLAN();
    const e = await visibleEvent(db, id, clan, me);
    if (!e.my_clan_in) throw Errors.notFound("CLAN_NOT_REGISTERED", "Votre clan n'est pas inscrit à ce raid.");
    const entry = await db.prepare("SELECT leader_id FROM raid_clan_entries WHERE event_id = ?1 AND clan_id = ?2").bind(id, clan.id).first<{ leader_id: string | null }>();
    if (!isManager(clan) && entry?.leader_id !== me) throw Errors.forbidden("NOT_RAID_LEADER", "Le chef, les officiers et le chef de raid invitent les membres.");
    const now = nowIso();
    if (e.cancelled_at) throw CANCELLED();
    if (now >= e.lock_at) throw LOCKED(e);
    const r = await db
        .prepare(
            `INSERT INTO notifications (id, player_id, kind, actor_id, data, group_key, count, created_at)
             SELECT lower(hex(randomblob(16))), m.player_id, 'raid', ?2, ?3, ?4, 1, ?5 FROM clan_members m JOIN players p ON p.id = m.player_id
             WHERE m.clan_id = ?1 AND m.player_id <> ?2 AND instr(',' || p.notifications_off || ',', ',raid,') = 0
               AND NOT EXISTS (SELECT 1 FROM raid_registrations r WHERE r.event_id = ?6 AND r.player_id = m.player_id)
               AND NOT EXISTS (SELECT 1 FROM notifications n WHERE n.player_id = m.player_id AND n.group_key = ?4 AND n.read_at IS NULL)
               AND NOT EXISTS (SELECT 1 FROM player_blocks b WHERE (b.player_id = m.player_id AND b.blocked_id = ?2) OR (b.player_id = ?2 AND b.blocked_id = m.player_id))`,
        )
        .bind(clan.id, me, JSON.stringify(notifData(e, "call", { tag: clan.tag })), `raid:call:${id}`, now, id)
        .run();
    return json(ctx, { notified: r.meta.changes });
}

/**
 * clans.ts: the statements that follow a member leaving or excluded from a clan (in the same
 * batch, after the removal): their registrations to raids still to come go, and they are no
 * longer anyone's raid leader there. Nothing happens when the removal was refused.
 */
export function raidLeaveStatements(db: D1Database, clanId: string, playerId: string): D1PreparedStatement[] {
    const gone = "NOT EXISTS (SELECT 1 FROM clan_members WHERE player_id = ?1 AND clan_id = ?2)";
    return [
        db
            .prepare(`DELETE FROM raid_registrations WHERE player_id = ?1 AND clan_id = ?2 AND ${gone} AND event_id IN (SELECT id FROM raid_events WHERE ends_at > ?3)`)
            .bind(playerId, clanId, nowIso()),
        db.prepare(`UPDATE raid_clan_entries SET leader_id = NULL WHERE clan_id = ?2 AND leader_id = ?1 AND ${gone}`).bind(playerId, clanId),
    ];
}

// ------------------------------------------------------------------ reminders (cron)

/**
 * The reminders due at `nowMs`: 24 h, 1 h and 10 min before the start (each player's choice)
 * and the start itself, to every player registered. Each one within its window only (never a
 * « 24 h » reminder to a player who registered 5 h before), and never twice: the players told
 * are written in raid_reminders_sent in the same transaction as their notifications.
 * Returns the number of notifications sent.
 */
export async function raidTick(db: D1Database, nowMs = Date.now()): Promise<number> {
    const now = iso(nowMs);
    let sent = 0;
    for (let i = 0; i < TICK_OFFSETS.length; i++) {
        const o = TICK_OFFSETS[i]!;
        const next = TICK_OFFSETS[i + 1];
        const hi = iso(nowMs + o * 60000);
        const lo = iso(Math.max(nowMs + o * 60000 - REMINDER_GRACE_MS, next === undefined ? -Infinity : nowMs + next * 60000));
        const due = await db
            .prepare(
                "SELECT id, raid, format, kind, starts_at FROM raid_events WHERE published_at IS NOT NULL AND cancelled_at IS NULL AND suspended_at IS NULL AND starts_at > ?1 AND starts_at <= ?2 LIMIT 200",
            )
            .bind(lo, hi)
            .all<Pick<EventRow, "id" | "raid" | "format" | "kind" | "starts_at">>();
        for (const e of due.results) {
            const minutes = o === 0 ? 0 : Math.max(1, Math.round((Date.parse(e.starts_at) - nowMs) / 60000));
            const data = JSON.stringify(notifData(e, o === 0 ? "start" : "reminder", { minutes }));
            const r = await db.batch([
                db
                    .prepare(
                        `INSERT INTO notifications (id, player_id, kind, data, count, created_at)
                         SELECT lower(hex(randomblob(16))), r.player_id, 'raid', ?3, 1, ?4 FROM raid_registrations r JOIN players p ON p.id = r.player_id
                         LEFT JOIN raid_prefs f ON f.player_id = r.player_id
                         WHERE r.event_id = ?1 AND instr(',' || p.notifications_off || ',', ',raid,') = 0
                           AND (?2 = 0 OR instr(',' || COALESCE(f.reminders, '1440,60,10') || ',', ?6) > 0)
                           AND NOT EXISTS (SELECT 1 FROM raid_reminders_sent s WHERE s.event_id = r.event_id AND s.player_id = r.player_id
                                           AND s.offset_minutes = ?2 AND s.starts_at = ?5)`,
                    )
                    .bind(e.id, o, data, now, e.starts_at, `,${o},`),
                db
                    .prepare(
                        `INSERT OR IGNORE INTO raid_reminders_sent (event_id, player_id, offset_minutes, starts_at, sent_at)
                         SELECT r.event_id, r.player_id, ?2, ?3, ?4 FROM raid_registrations r WHERE r.event_id = ?1`,
                    )
                    .bind(e.id, o, e.starts_at, now),
            ]);
            sent += r[0]!.meta.changes;
        }
    }
    return sent;
}

/** Daily maintenance: the reminders sent more than 30 days ago are forgotten. */
export async function pruneRaidReminders(db: D1Database): Promise<void> {
    await db.prepare("DELETE FROM raid_reminders_sent WHERE sent_at < ?1").bind(iso(Date.now() - 30 * 86400000)).run();
}

// ------------------------------------------------------------------ administration

/** What the Journal keeps of an event (adminRoles.ts reads it back). */
function journalRaid(e: Pick<EventRow, "id" | "raid" | "format" | "starts_at">, extra: Record<string, unknown> = {}) {
    return { id: e.id, raid: e.raid, name: definition(e.raid).name, format: e.format, starts_at: e.starts_at, ...extra };
}

interface WorldFields {
    raid?: string;
    format?: RaidFormat;
    start?: number | null;
    lock_minutes?: number;
    registration_open?: boolean;
    capacity?: number;
    clan_capacity?: number;
    difficulty?: number;
    rewards_text?: string;
    /** 2.69: minutes after the start a newcomer may still enter play; null: until the end. */
    late_minutes?: number | null;
}

function readWorldFields(body: Record<string, unknown>, required: boolean): WorldFields {
    const v = new Validator(body, ["raid", "format", "starts_at", "lock_minutes", "registration_open", "capacity", "clan_capacity", "difficulty", "rewards_text", "late_minutes"]);
    const f: WorldFields = {};
    if (body.late_minutes === null) f.late_minutes = null;
    else f.late_minutes = v.int("late_minutes", { min: 0, max: 1440, optional: true });
    f.raid = v.str("raid", { max: 32, optional: true });
    if (f.raid !== undefined && !(f.raid in RAIDS)) v.fail("raid", "raid inconnu");
    f.format = v.str("format", { max: 16, optional: !required }) as RaidFormat | undefined;
    if (f.format !== undefined && !FORMAT_IDS.includes(f.format)) v.fail("format", "court, standard ou legendaire");
    f.lock_minutes = v.int("lock_minutes", { min: 0, max: 240, optional: true });
    f.registration_open = v.bool("registration_open", { optional: true });
    f.capacity = v.int("capacity", { min: 0, max: 100000, optional: true });
    f.clan_capacity = v.int("clan_capacity", { min: 1, max: 50, optional: true });
    f.difficulty = v.int("difficulty", { min: 1, max: 5, optional: true });
    f.rewards_text = v.str("rewards_text", { max: MAX_REWARDS_TEXT, optional: true, trim: true });
    f.start = readStart(v, v.str("starts_at", { max: 40, optional: !required }), Math.max(MIN_AHEAD_MS, ((f.lock_minutes ?? 30) + 5) * 60000), MAX_WORLD_AHEAD_MS);
    v.done();
    return f;
}

/** GET /admin/raids: the world events (drafts included), latest start first, with their registrations. */
export async function adminList(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const now = nowIso();
    const rows = await ctx.env.DB.prepare(`${EVENT_SELECT} WHERE e.kind = 'world' ORDER BY e.starts_at DESC, e.id LIMIT 60`).bind("", "").all<EventRow>();
    return json(ctx, {
        server_time: now,
        raids: Object.values(RAIDS).map((r) => ({ ...r, formats: RAID_FORMATS })),
        events: rows.results.map((e) => ({ ...eventView(e, now), rewards_text: e.rewards_text })),
    });
}

/** GET /admin/raids/{eventId}: one world event and its clans (registrations: the only live figures of phase 1). */
export async function adminGet(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const id = eventIdParam(ctx);
    const now = nowIso();
    const db = ctx.env.DB;
    const e = await loadEvent(db, id, "", "");
    if (!e || e.kind !== "world") throw RAID_NOT_FOUND();
    const clans = await db
        .prepare(
            `SELECT c.id, c.tag, c.name, l.username AS leader, (SELECT COUNT(*) FROM raid_registrations r WHERE r.event_id = x.event_id AND r.clan_id = x.clan_id) AS players
             FROM raid_clan_entries x JOIN clans c ON c.id = x.clan_id LEFT JOIN players l ON l.id = x.leader_id
             WHERE x.event_id = ?1 ORDER BY players DESC, c.tag LIMIT 200`,
        )
        .bind(id)
        .all<{ id: string; tag: string; name: string; leader: string | null; players: number }>();
    return json(ctx, { server_time: now, event: { ...eventView(e, now), rewards_text: e.rewards_text }, clans: clans.results });
}

/** POST /admin/raids: a world event, as a draft (Journal: admin_raid_created). */
export async function adminCreate(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const f = readWorldFields(await readJsonObject(ctx), true);
    const id = crypto.randomUUID();
    const now = nowIso();
    const lock = f.lock_minutes ?? 30;
    const t = timesOf(f.start!, lock, f.format!);
    const db = ctx.env.DB;
    await db
        .prepare(
            `INSERT INTO raid_events (id, kind, raid, format, clan_id, starts_at, lock_minutes, lock_at, ends_at, registration_open, capacity, clan_capacity,
               difficulty, rewards_text, published_at, created_by, created_at, updated_at, late_minutes)
             VALUES (?1, 'world', ?2, ?3, NULL, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, NULL, ?13, ?14, ?14, ?15)`,
        )
        .bind(id, f.raid ?? "asterion", f.format, t.starts_at, lock, t.lock_at, t.ends_at, f.registration_open === false ? 0 : 1, f.capacity ?? 0,
            f.clan_capacity ?? 50, f.difficulty ?? 3, f.rewards_text ?? "", auth.player.id, now, f.late_minutes ?? null)
        .run();
    const e = (await loadEvent(db, id, "", ""))!;
    await audit(ctx, auth.player.id, "admin_raid_created", { raid: journalRaid(e), by_username: auth.player.username });
    return json(ctx, { event: { ...eventView(e, now), rewards_text: e.rewards_text } }, { status: 201 });
}

/**
 * PATCH /admin/raids/{eventId}: edit a world event (Journal: admin_raid_updated). Once
 * published: a new time tells the registered players (and reminds them again at the new
 * time); registrations opened again tell every clan (once while unread).
 */
export async function adminUpdate(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const id = eventIdParam(ctx);
    const body = await readJsonObject(ctx);
    const startGiven = body.starts_at !== undefined;
    const f = readWorldFields(body, false);
    const db = ctx.env.DB;
    const e = await loadEvent(db, id, "", "");
    if (!e || e.kind !== "world") throw RAID_NOT_FOUND();
    const now = nowIso();
    if (e.cancelled_at) throw CANCELLED();
    if (now >= e.ends_at) throw FINISHED();
    // 2.69: a catch-up session keeps its dates (registrations open until its end): cancel it and open another.
    if (e.catchup_of) throw Errors.conflict("RAID_IS_CATCHUP", "Une session de rattrapage ne se modifie pas : annulez-la et ouvrez-en une autre.");
    if (startGiven && now >= e.starts_at) throw Errors.conflict("RAID_STARTED", "L'événement a commencé : son heure ne change plus.");
    const format = f.format ?? e.format;
    const lock = f.lock_minutes ?? e.lock_minutes;
    const t = timesOf(f.start ?? Date.parse(e.starts_at), lock, format);
    const open = f.registration_open === undefined ? e.registration_open : f.registration_open ? 1 : 0;
    const next = {
        raid: f.raid ?? e.raid,
        format,
        ...t,
        lock_minutes: lock,
        registration_open: open,
        capacity: f.capacity ?? e.capacity,
        clan_capacity: f.clan_capacity ?? e.clan_capacity,
        difficulty: f.difficulty ?? e.difficulty,
        rewards_text: f.rewards_text ?? e.rewards_text,
        late_minutes: f.late_minutes === undefined ? (e.late_minutes ?? null) : f.late_minutes,
    };
    const changes = (Object.keys(next) as (keyof typeof next)[]).filter((k) => next[k] !== ((e as unknown as Record<string, unknown>)[k] ?? null));
    if (changes.length === 0) return json(ctx, { event: { ...eventView(e, now), rewards_text: e.rewards_text }, changes });
    await db
        .prepare(
            `UPDATE raid_events SET raid = ?2, format = ?3, starts_at = ?4, lock_at = ?5, ends_at = ?6, lock_minutes = ?7, registration_open = ?8,
               capacity = ?9, clan_capacity = ?10, difficulty = ?11, rewards_text = ?12, updated_at = ?13, late_minutes = ?14
             WHERE id = ?1 AND cancelled_at IS NULL`,
        )
        .bind(id, next.raid, next.format, next.starts_at, next.lock_at, next.ends_at, next.lock_minutes, next.registration_open, next.capacity,
            next.clan_capacity, next.difficulty, next.rewards_text, now, next.late_minutes)
        .run();
    const after = (await loadEvent(db, id, "", ""))!;
    if (after.published_at) {
        if (next.starts_at !== e.starts_at)
            for (const pid of await registrantsOf(db, id))
                await notify(ctx, pid, "raid", { data: notifData(after, "moved"), groupKey: `raid:moved:${id}` });
        if (open === 1 && e.registration_open === 0) await notifyClanMembers(ctx, null, null, notifData(after, "open"), `raid:open:${id}`);
    }
    await audit(ctx, auth.player.id, "admin_raid_updated", { raid: journalRaid(after, { changes }), by_username: auth.player.username });
    return json(ctx, { event: { ...eventView(after, now), rewards_text: after.rewards_text }, changes });
}

/** POST /admin/raids/{eventId}/publish: the draft becomes public and every clan's members are told (Journal: admin_raid_published). */
export async function adminPublish(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const id = eventIdParam(ctx);
    const db = ctx.env.DB;
    const now = nowIso();
    // 2.69 (phase 5): never published on a titan whose map is not valid (« Valider la carte », raidWorld.ts).
    const draft = await loadEvent(db, id, "", "");
    if (draft && draft.kind === "world" && !draft.published_at && !draft.cancelled_at) await requireValidMap(db, draft as Parameters<typeof requireValidMap>[1], auth.player.username);
    const r = await db
        .prepare("UPDATE raid_events SET published_at = ?2, updated_at = ?2 WHERE id = ?1 AND kind = 'world' AND published_at IS NULL AND cancelled_at IS NULL AND lock_at > ?2")
        .bind(id, now)
        .run();
    const e = await loadEvent(db, id, "", "");
    if (!e || e.kind !== "world") throw RAID_NOT_FOUND();
    if (!r.meta.changes) {
        if (e.cancelled_at) throw CANCELLED();
        if (e.published_at) throw Errors.conflict("ALREADY_PUBLISHED", "Cet événement est déjà publié.");
        throw Errors.conflict("RAID_TOO_LATE", "Le verrouillage des inscriptions est passé : changez l'heure de début avant de publier.");
    }
    const notified = await notifyClanMembers(ctx, null, null, notifData(e, "world"), `raid:world:${id}`);
    await audit(ctx, auth.player.id, "admin_raid_published", { raid: journalRaid(e, { notified }), by_username: auth.player.username });
    return json(ctx, { event: { ...eventView(e, now), rewards_text: e.rewards_text }, notified });
}

/** DELETE /admin/raids/{eventId} {reason?}: the event is cancelled; the clans and players registered are told (Journal: admin_raid_cancelled). */
export async function adminCancel(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const id = eventIdParam(ctx);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["reason"]);
    const reason = v.str("reason", { max: 200, optional: true, trim: true }) ?? null;
    v.done();
    const db = ctx.env.DB;
    const now = nowIso();
    const r = await db
        .prepare("UPDATE raid_events SET cancelled_at = ?2, updated_at = ?2 WHERE id = ?1 AND kind = 'world' AND cancelled_at IS NULL AND ends_at > ?2")
        .bind(id, now)
        .run();
    const e = await loadEvent(db, id, "", "");
    if (!e || e.kind !== "world") throw RAID_NOT_FOUND();
    if (!r.meta.changes) throw e.cancelled_at ? CANCELLED() : FINISHED();
    if (e.published_at) {
        const told = await registrantsOf(db, id);
        const entries = await db.prepare("SELECT clan_id FROM raid_clan_entries WHERE event_id = ?1").bind(id).all<{ clan_id: string }>();
        for (const x of entries.results) told.push(...(await managersOf(db, x.clan_id)));
        await notifyPlayers(ctx, told, null, notifData(e, "cancelled"));
    }
    await audit(ctx, auth.player.id, "admin_raid_cancelled", { raid: journalRaid(e), by_username: auth.player.username, reason });
    return json(ctx, { cancelled: true });
}
