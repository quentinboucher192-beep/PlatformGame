// Clan raids « Asterion », 2.78.1 (docs/RAIDS.md §27): the « fiche récapitulative » of a finished raid, in
// CLANS › RAIDS. Read by the members of a clan that took part (the session's player: never a player id from
// the request) and by the administrators (moderation: any clan).
//
// Everything comes from what the server accepted, never from what a game claims:
// * the objectives validated by the consensus of each instance (raid_branch_objectives): the seven parts
//   fallen (their gigaboss objective, the group whose report crossed the line), the relics, the heart;
// * the zones finished (raid_zone_completions), the heart's shared health (raid_heart_damage: each
//   instance's part, accepted by its majority), the highest phase of each gigaboss fight;
// * the contributions tables (raid_contributions, raid_zone_contributions), accepted by the majority of
//   each instance: the best of the clan in each category, my own figures;
// * the groups wiped (raid_instances.wiped_zone, written once their wipe was accepted) and the groups of
//   reinforcement (`relieves`, made by the server), the rewards granted (raid_reward_grants) and the
//   clan's titles (raid_clan_titles).
// What still waits for its majority (pendingOf: tables, objectives) is counted apart, never in a figure,
// and the game labels the figures « en attente de validation » until the event is final (its end and the
// grace of the last reports passed).
//
// GET /clans/{clanId}/raids: the clan's finished raids (the heart fallen, or the event over), newest first.
// GET /clans/{clanId}/raids/{eventId}/recap: one of them, whole.

import { requireAuth } from "./auth";
import { isAdminViewer } from "./friends";
import { Errors, json, nowIso, type Ctx } from "./http";
import { CATALOGS, HEART, partOf } from "./raidMap";
import { HEART_OBJECTIVE, HEART_UNITS, RELICS, phaseOf } from "./raidHeart";
import { RAID_REWARDS, GIGABOSS_BADGES } from "./raidRewards";
import { RESULTS_GRACE_MS, pendingOf, resultSummaries } from "./raidWorld";
import { NO_REVIEWS, reviewSummaries } from "./raidReviews";

const ID_RE = /^[A-Za-z0-9-]{8,64}$/;
/** The finished raids listed (newest first). */
export const RECAP_LIST_LIMIT = 20;

/** The seven parts in the titan's order: their branch, gigaboss objective, relic and zones. */
export const RECAP_PARTS = CATALOGS.map((c) => ({
    part: partOf(c.id),
    branch: c.id,
    name: c.name,
    boss: c.gigaboss,
    objective: c.objectives.find((o) => o.role === "gigaboss")?.id ?? "",
    relic: RELICS.find((r) => r.branch === c.id)?.objective ?? "",
    zones: c.zones.length,
}));
export const RECAP_ZONES_TOTAL = RECAP_PARTS.reduce((n, p) => n + p.zones, 0);
const HEART_BRANCH = HEART?.id ?? "asterion_coeur";
const GIGABOSS_OBJECTIVES = RECAP_PARTS.map((p) => p.objective).filter(Boolean);

/** The categories of « Les meilleurs du raid » (never only the damage). */
export const RECAP_CATEGORIES = ["gigaboss_damage", "heart_damage", "support", "revives", "mechanisms", "objectives", "fewest_deaths"] as const;
export type RecapCategory = (typeof RECAP_CATEGORIES)[number];

/** The order of the clan's rewards (the heart's first, then the event's, the badges of the gigaboss last). */
const REWARD_ORDER = [
    "coeur_badge",
    "coeur_clan",
    "asterion_ecorce",
    "asterion_ecorce_argent",
    "coeur_supernova",
    "coeur_decor",
    "asterion_clan_top10",
    "pyroclast_top",
    "pyroclast_performance",
    "asterion_participation",
];
const GIGABOSS_BADGE_IDS = new Set(Object.entries(GIGABOSS_BADGES).filter(([boss]) => boss !== HEART?.gigaboss).map(([, id]) => id));

interface ClanRow {
    id: string;
    tag: string;
    name: string;
    emblem_shape: string;
    emblem_primary: string;
    emblem_secondary: string;
    member: number;
}

interface EventRow {
    id: string;
    kind: string;
    raid: string;
    format: string;
    starts_at: string;
    ends_at: string;
    catchup_of: string | null;
    fallen_at: string | null;
}

/**
 * The clan of the route as the session may read it: a member of it, or an administrator. A clan that does
 * not exist is « introuvable », another clan's raids are refused (they are internal).
 */
async function clanOf(ctx: Ctx): Promise<{ me: string; clan: ClanRow }> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const id = ctx.params.clanId ?? "";
    if (!ID_RE.test(id)) throw Errors.notFound("CLAN_NOT_FOUND", "Clan introuvable.");
    const clan = await ctx.env.DB.prepare(
        `SELECT c.id, c.tag, c.name, c.emblem_shape, c.emblem_primary, c.emblem_secondary,
                EXISTS (SELECT 1 FROM clan_members m WHERE m.clan_id = c.id AND m.player_id = ?2) AS member
         FROM clans c WHERE c.id = ?1`,
    )
        .bind(id, me)
        .first<ClanRow>();
    if (!clan) throw Errors.notFound("CLAN_NOT_FOUND", "Clan introuvable.");
    if (!clan.member && !isAdminViewer(auth)) throw Errors.forbidden("NOT_CLAN_MEMBER", "Seuls les membres du clan voient les fiches de ses raids.");
    return { me, clan };
}

/** The events the clan took part in that are finished (the heart fallen, or the event over), newest first. */
const FINISHED_SQL = `SELECT e.id, e.kind, e.raid, e.format, e.starts_at, e.ends_at, e.catchup_of, ho.accepted_at AS fallen_at
    FROM raid_clan_entries x JOIN raid_events e ON e.id = x.event_id
    LEFT JOIN raid_branch_objectives ho ON ho.event_id = e.id AND ho.branch = ?3 AND ho.objective_id = ?4
    WHERE x.clan_id = ?1 AND e.published_at IS NOT NULL AND e.cancelled_at IS NULL AND (e.ends_at <= ?2 OR ho.accepted_at IS NOT NULL)`;

/** No figure moves any more: the event over and the grace of its last reports passed. */
function finalOf(e: Pick<EventRow, "ends_at">, nowMs: number): boolean {
    return nowMs >= Date.parse(e.ends_at) + RESULTS_GRACE_MS;
}

/** Where the raid ended: the heart's validated fall, else the event's end. */
function endOf(e: EventRow): string {
    return e.fallen_at && e.fallen_at < e.ends_at ? e.fallen_at : e.ends_at;
}

function seconds(from: string, to: string): number {
    return Math.max(0, Math.round((Date.parse(to) - Date.parse(from)) / 1000));
}

function eventJson(e: EventRow, nowMs: number) {
    const end = endOf(e);
    return {
        id: e.id,
        kind: e.kind,
        raid: e.raid,
        format: e.format,
        catchup: !!e.catchup_of,
        starts_at: e.starts_at,
        ends_at: e.ends_at,
        end_at: end,
        duration_s: seconds(e.starts_at, end),
        heart_fallen: !!e.fallen_at,
        fallen_at: e.fallen_at,
        final: finalOf(e, nowMs),
    };
}

/** GET /clans/{clanId}/raids: the clan's finished raids, won or not, with how far each went. */
export async function finishedRaids(ctx: Ctx): Promise<Response> {
    const { clan } = await clanOf(ctx);
    const db = ctx.env.DB;
    const now = nowIso();
    const nowMs = Date.parse(now);
    const events = (
        await db
            .prepare(`${FINISHED_SQL} ORDER BY e.starts_at DESC, e.id LIMIT ${RECAP_LIST_LIMIT}`)
            .bind(clan.id, now, HEART_BRANCH, HEART_OBJECTIVE)
            .all<EventRow>()
    ).results;
    const down = new Map<string, number>();
    if (events.length && GIGABOSS_OBJECTIVES.length) {
        const ids = events.map((e) => e.id);
        const rows = (
            await db
                .prepare(
                    `SELECT event_id, COUNT(*) AS n FROM raid_branch_objectives
                     WHERE event_id IN (${ids.map((_, i) => `?${i + 1}`).join(",")}) AND objective_id IN (${GIGABOSS_OBJECTIVES.map((_, i) => `?${ids.length + i + 1}`).join(",")})
                     GROUP BY event_id`,
                )
                .bind(...ids, ...GIGABOSS_OBJECTIVES)
                .all<{ event_id: string; n: number }>()
        ).results;
        for (const r of rows) down.set(r.event_id, r.n);
    }
    return json(ctx, {
        server_time: now,
        clan: { id: clan.id, tag: clan.tag, name: clan.name },
        raids: events.map((e) => ({ ...eventJson(e, nowMs), parts_down: down.get(e.id) ?? 0, parts_total: RECAP_PARTS.length })),
    });
}

interface ObjectiveRow {
    branch: string;
    objective_id: string;
    instance_id: string | null;
    accepted_at: string;
    clan_id: string | null;
    group_no: number | null;
    relieves: string | null;
    tag: string | null;
}
interface ClanInstanceRow {
    id: string;
    group_no: number;
    branch: string;
    relieves: string | null;
    wiped_zone: number | null;
    members: number;
}
interface ContribRow {
    player_id: string;
    username: string;
    avatar: string;
    heart: number;
    damage: number;
    boss_damage: number;
    blocks: number;
    rescues: number;
    objectives: number;
    ticks: number;
}
interface ZoneContribRow {
    player_id: string;
    mechanisms: number;
    support: number;
    zones: number;
}
interface MyInstanceRow {
    id: string;
    clan_id: string;
    group_no: number;
    branch: string;
    relieves: string | null;
    wiped_zone: number | null;
    tag: string;
}

/** The validated totals of one player of the clan (every instance they played in). */
interface PlayerTotals {
    id: string;
    username: string;
    avatar: string;
    gigabossDamage: number; // tenths, the seven parts' instances
    heartDamage: number; // tenths, the heart's instances
    support: number; // shield blocks and allies revived
    revives: number; // allies revived
    mechanisms: number; // the zones' tables: plates held, vents, weak points
    objectives: number;
    ticks: number;
    zones: number; // zones whose table names them
}

const byName = (a: PlayerTotals, b: PlayerTotals) => (a.username < b.username ? -1 : a.username > b.username ? 1 : a.id < b.id ? -1 : 1);

/** The best of the clan in a category (the highest value, then the name): null when nobody has any. */
function bestOf(players: PlayerTotals[], value: (p: PlayerTotals) => number, me: string, share: boolean) {
    const total = players.reduce((n, p) => n + value(p), 0);
    const ranked = players.filter((p) => value(p) > 0).sort((a, b) => value(b) - value(a) || byName(a, b));
    const top = ranked[0];
    if (!top) return { player: null, value: 0, share: 0 };
    return {
        player: { id: top.id, username: top.username, avatar: top.avatar, me: top.id === me },
        value: share ? Math.round(value(top) / 10) : value(top),
        share: share && total > 0 ? value(top) / total : 0,
    };
}

/** GET /clans/{clanId}/raids/{eventId}/recap: the « fiche récapitulative » of one finished raid of the clan. */
export async function raidRecap(ctx: Ctx): Promise<Response> {
    const { me, clan } = await clanOf(ctx);
    const db = ctx.env.DB;
    const now = nowIso();
    const nowMs = Date.parse(now);
    const eventId = ctx.params.eventId ?? "";
    if (!ID_RE.test(eventId)) throw Errors.notFound("RAID_NOT_FOUND", "Raid introuvable.");
    const e = await db.prepare(`${FINISHED_SQL} AND e.id = ?5`).bind(clan.id, now, HEART_BRANCH, HEART_OBJECTIVE, eventId).first<EventRow>();
    if (!e) throw Errors.notFound("RAID_NOT_FOUND", "Ce raid n'est pas terminé, ou le clan n'y a pas pris part.");
    const [objR, instR, heartR, fightsR, zonesR, contribR, zoneContribR, rewardsR, titlesR, regR, mineR] = await db.batch([
        db
            .prepare(
                `SELECT o.branch, o.objective_id, o.instance_id, o.accepted_at, i.clan_id, i.group_no, i.relieves, c.tag FROM raid_branch_objectives o
                 LEFT JOIN raid_instances i ON i.id = o.instance_id LEFT JOIN clans c ON c.id = i.clan_id WHERE o.event_id = ?1`,
            )
            .bind(e.id),
        db
            .prepare(
                `SELECT i.id, i.group_no, i.branch, i.relieves, i.wiped_zone, (SELECT COUNT(*) FROM raid_instance_members m WHERE m.instance_id = i.id) AS members
                 FROM raid_instances i WHERE i.event_id = ?1 AND i.clan_id = ?2`,
            )
            .bind(e.id, clan.id),
        db.prepare("SELECT h.instance_id, h.damage, i.clan_id FROM raid_heart_damage h JOIN raid_instances i ON i.id = h.instance_id WHERE h.event_id = ?1").bind(e.id),
        db.prepare("SELECT branch, MAX(phase) AS phase FROM raid_gigaboss_fights WHERE event_id = ?1 GROUP BY branch").bind(e.id),
        db.prepare("SELECT branch, zone, instance_id FROM raid_zone_completions WHERE event_id = ?1").bind(e.id),
        // The validated tables of the clan's players (their registration's clan), split between the heart and the parts.
        db
            .prepare(
                `SELECT c.player_id, p.username, p.avatar, (i.branch = ?3) AS heart, SUM(c.damage) AS damage, SUM(c.boss_damage) AS boss_damage,
                        SUM(c.blocks) AS blocks, SUM(c.rescues) AS rescues, SUM(c.objectives) AS objectives, SUM(c.ticks) AS ticks
                 FROM raid_contributions c JOIN raid_registrations r ON r.event_id = c.event_id AND r.player_id = c.player_id
                 JOIN raid_instances i ON i.id = c.instance_id JOIN players p ON p.id = c.player_id
                 WHERE c.event_id = ?1 AND r.clan_id = ?2 GROUP BY c.player_id, heart`,
            )
            .bind(e.id, clan.id, HEART_BRANCH),
        db
            .prepare(
                `SELECT z.player_id, SUM(z.mechanisms) AS mechanisms, SUM(z.support) AS support, COUNT(DISTINCT z.branch || ':' || z.zone) AS zones
                 FROM raid_zone_contributions z JOIN raid_registrations r ON r.event_id = z.event_id AND r.player_id = z.player_id
                 WHERE z.event_id = ?1 AND r.clan_id = ?2 GROUP BY z.player_id`,
            )
            .bind(e.id, clan.id),
        db
            .prepare(
                `SELECT g.reward, COUNT(DISTINCT g.player_id) AS players, MAX(g.player_id = ?3) AS mine FROM raid_reward_grants g
                 JOIN raid_registrations r ON r.event_id = g.event_id AND r.player_id = g.player_id
                 WHERE g.event_id = ?1 AND r.clan_id = ?2 GROUP BY g.reward`,
            )
            .bind(e.id, clan.id, me),
        db.prepare("SELECT title, rank FROM raid_clan_titles WHERE clan_id = ?2 AND event_id = ?1 ORDER BY rank, title").bind(e.id, clan.id),
        db.prepare("SELECT COUNT(*) AS n FROM raid_registrations WHERE event_id = ?1 AND clan_id = ?2").bind(e.id, clan.id),
        // The instances I held a seat in (any clan: a helper may play in another clan's group).
        db
            .prepare(
                `SELECT i.id, i.clan_id, i.group_no, i.branch, i.relieves, i.wiped_zone, c.tag FROM raid_instances i JOIN clans c ON c.id = i.clan_id
                 WHERE i.event_id = ?1 AND EXISTS (SELECT 1 FROM raid_instance_members m WHERE m.instance_id = i.id AND m.player_id = ?2)
                 ORDER BY i.created_at, i.id`,
            )
            .bind(e.id, me),
    ]);
    const objectives = objR!.results as ObjectiveRow[];
    const clanInstances = (instR!.results as ClanInstanceRow[]).filter((i) => i.members > 0);
    const clanIds = new Set((instR!.results as ClanInstanceRow[]).map((i) => i.id));
    const heartRows = heartR!.results as { instance_id: string; damage: number; clan_id: string }[];
    const fights = new Map((fightsR!.results as { branch: string; phase: number }[]).map((f) => [f.branch, f.phase]));
    const zonesDone = zonesR!.results as { branch: string; zone: number; instance_id: string | null }[];
    const mine = mineR!.results as MyInstanceRow[];
    const myIds = new Set(mine.map((i) => i.id));
    const done = new Map<string, Map<string, ObjectiveRow>>();
    for (const o of objectives) {
        if (!done.has(o.branch)) done.set(o.branch, new Map());
        done.get(o.branch)!.set(o.objective_id, o);
    }
    const byGroup = (o: ObjectiveRow | undefined) =>
        o && o.instance_id && o.group_no !== null
            ? { clan_tag: o.tag ?? "", group: o.group_no, reinforcement: !!o.relieves, mine: o.clan_id === clan.id, me: myIds.has(o.instance_id) }
            : null;

    // The seven parts and the heart, in the titan's order.
    const parts = RECAP_PARTS.map((p) => {
        const o = done.get(p.branch)?.get(p.objective);
        return {
            part: p.part,
            name: p.name,
            boss: p.boss,
            fallen: !!o,
            at: o?.accepted_at ?? null,
            by: byGroup(o),
            relic: !!p.relic && !!done.get(p.branch)?.has(p.relic),
            phase: o ? 4 : (fights.get(p.branch) ?? 0),
        };
    });
    const partsDown = parts.filter((p) => p.fallen).length;
    const heartO = done.get(HEART_BRANCH)?.get(HEART_OBJECTIVE);
    const heartTaken = Math.min(HEART_UNITS, heartRows.reduce((n, r) => n + r.damage, 0));
    const clanHeart = heartRows.filter((r) => r.clan_id === clan.id);
    const heart = {
        part: "coeur",
        name: HEART?.name ?? "",
        boss: HEART?.gigaboss ?? "",
        fallen: !!heartO,
        open: partsDown === RECAP_PARTS.length,
        at: heartO?.accepted_at ?? null,
        by: byGroup(heartO),
        phase: heartO ? 4 : partsDown === RECAP_PARTS.length ? phaseOf(heartTaken) : 0,
        taken: heartO ? HEART_UNITS : heartTaken,
        groups: heartRows.filter((r) => r.damage > 0).length,
        clan_groups: clanHeart.filter((r) => r.damage > 0).length,
        clan_share: Math.min(1, clanHeart.reduce((n, r) => n + r.damage, 0) / HEART_UNITS),
    };

    // The players of the clan, their validated totals.
    const totals = new Map<string, PlayerTotals>();
    for (const r of contribR!.results as ContribRow[]) {
        const t = totals.get(r.player_id) ?? {
            id: r.player_id,
            username: r.username,
            avatar: r.avatar,
            gigabossDamage: 0,
            heartDamage: 0,
            support: 0,
            revives: 0,
            mechanisms: 0,
            objectives: 0,
            ticks: 0,
            zones: 0,
        };
        if (r.heart) t.heartDamage += r.boss_damage;
        else t.gigabossDamage += r.boss_damage;
        t.support += r.blocks + r.rescues;
        t.revives += r.rescues;
        t.objectives += r.objectives;
        t.ticks += r.ticks;
        totals.set(r.player_id, t);
    }
    for (const z of zoneContribR!.results as ZoneContribRow[]) {
        const t = totals.get(z.player_id);
        if (!t) continue; // a zone's table without the player's contributions: counted with them only
        t.mechanisms += z.mechanisms;
        t.zones = z.zones;
    }
    const players = [...totals.values()].filter((t) => t.ticks > 0);
    const best = {
        gigaboss_damage: bestOf(players, (p) => p.gigabossDamage, me, true),
        heart_damage: bestOf(players, (p) => p.heartDamage, me, true),
        support: bestOf(players, (p) => p.support, me, false),
        revives: bestOf(players, (p) => p.revives, me, false),
        mechanisms: bestOf(players, (p) => p.mechanisms, me, false),
        objectives: bestOf(players, (p) => p.objectives, me, false),
        // 2.78.0 brings each player's deaths in the contributions: until then, nothing is said.
        fewest_deaths: null,
    };

    // The clan's groups: the groups of its roster that played, its groups of reinforcement, the wiped ones.
    const zoneDone = (branch: string, zone: number) => zonesDone.some((z) => z.branch === branch && z.zone === zone);
    const wiped = clanInstances.filter((i) => i.wiped_zone !== null);
    const sevenZones = zonesDone.filter((z) => RECAP_PARTS.some((p) => p.branch === z.branch));

    // The clan's rewards: each reward granted to its players for this event (the badges of the gigaboss together).
    const rewards = (rewardsR!.results as { reward: string; players: number; mine: number }[])
        .filter((r) => RAID_REWARDS[r.reward])
        .map((r) => {
            const def = RAID_REWARDS[r.reward]!;
            return {
                reward: r.reward,
                name: def.name,
                kind: def.kind,
                ref: def.ref,
                players: r.players,
                mine: r.mine === 1,
                gigaboss: GIGABOSS_BADGE_IDS.has(r.reward),
            };
        })
        .sort((a, b) => {
            const ra = REWARD_ORDER.indexOf(a.reward), rb = REWARD_ORDER.indexOf(b.reward);
            return Number(a.gigaboss) - Number(b.gigaboss) || (ra < 0 ? 99 : ra) - (rb < 0 ? 99 : rb) || (a.reward < b.reward ? -1 : 1);
        });

    // Mine: my groups and what they downed, my zones, my revives, my share of the clan's damage to the heart.
    const meTotals = totals.get(me);
    const clanHeartDamage = players.reduce((n, p) => n + p.heartDamage, 0);
    const heartRank = meTotals && meTotals.heartDamage > 0 ? players.filter((p) => p.heartDamage > meTotals.heartDamage).length + 1 : 0;
    const myParts = parts.filter((p) => p.by?.me).map((p) => p.part);
    const pending = await pendingOf(db, e.id, clan.id);
    const world = e.kind === "world" ? ((await resultSummaries(db, [e.id], clan.id)).get(e.id) ?? null) : null;
    // 2.77.4: the players' reviews of the event (only those who played it may write one): its average and count.
    const reviews = (await reviewSummaries(db, [e.id])).get(e.id) ?? NO_REVIEWS;
    return json(ctx, {
        server_time: now,
        clan: { id: clan.id, tag: clan.tag, name: clan.name, emblem: { shape: clan.emblem_shape, primary: clan.emblem_primary, secondary: clan.emblem_secondary } },
        event: eventJson(e, nowMs),
        // An administrator who is not a member reads the recap; the event's own screens stay the clan's.
        viewer: { member: clan.member === 1 },
        pending: { tables: pending.tables, objectives: pending.objectives },
        reviews,
        world,
        people: {
            registered: (regR!.results[0] as { n: number } | undefined)?.n ?? 0,
            players: players.length,
            groups: new Set(clanInstances.filter((i) => !i.relieves).map((i) => i.group_no)).size,
            reinforcements: clanInstances.filter((i) => !!i.relieves).length,
        },
        figures: {
            parts_down: partsDown,
            parts_total: RECAP_PARTS.length,
            zones_done: sevenZones.length,
            zones_total: RECAP_ZONES_TOTAL,
            zones_by_clan: sevenZones.filter((z) => z.instance_id && clanIds.has(z.instance_id)).length,
            relics: parts.filter((p) => p.relic).length,
            relics_total: RECAP_PARTS.filter((p) => p.relic).length,
            revives: players.reduce((n, p) => n + p.revives, 0),
            deaths: null, // 2.78.0
            wiped: wiped.length,
            retaken: wiped.filter((i) => zoneDone(i.branch, i.wiped_zone!)).length,
        },
        parts,
        heart,
        best,
        rewards,
        titles: (titlesR!.results as { title: string; rank: number }[]).map((t) => ({ title: t.title, rank: t.rank })),
        me: {
            played: mine.length > 0 || !!meTotals,
            groups: mine.map((i) => ({
                clan_tag: i.tag,
                group: i.group_no,
                part: partOf(i.branch),
                reinforcement: !!i.relieves,
                wiped: i.wiped_zone !== null,
                mine: i.clan_id === clan.id,
            })),
            parts: myParts,
            zones: meTotals?.zones ?? 0,
            revives: meTotals?.revives ?? 0,
            deaths: null, // 2.78.0
            heart_share: meTotals && clanHeartDamage > 0 ? meTotals.heartDamage / clanHeartDamage : 0,
            heart_rank: heartRank,
            rewards: rewards.filter((r) => r.mine).map((r) => r.reward),
        },
    });
}
