// Clan raids « Asterion », phase 3 (2.68, docs/RAIDS.md §7-9): the results of the raid and its
// rewards. Everything here comes from what the consensus of each instance accepted
// (raidZones.ts): the players' contributions (raid_contributions), each instance's fight against
// the gigaboss (raid_gigaboss_fights) and the branch's objectives. A table that is still waiting
// for its majority is « en attente de validation », never a result.
//
// The score of a player mixes every category, each one capped: the damage alone never makes the
// top of a ranking (the mechanisms, the support, the rescues, the objectives, the presence count
// as much). The rewards are granted by the server only (raid_reward_grants, unique per player,
// reward and event: a reconnection, a repeated request or two acceptances at once grant them
// once), then claimed by the player: its items go to player_won_rewards (the profile and the
// vestiaire unlock them), its badge to the achievements the server lets the player declare.

import { requireAuth } from "./auth";
import { Errors, json, nowIso, readJsonObject, type Ctx } from "./http";
import { raidState } from "./raids";
import { RULES, rateLimit } from "./ratelimit";
import { Validator } from "./validate";
import { WORLD_REWARDS } from "./raidWorldRewards";
import { HEART_REWARDS } from "./raidHeartRewards";
import { grantEndedHeartTops, grantHeartTop } from "./raidHeart";

/** One player's contributions (the numbers the games computed, tenths of health for the damage). */
export interface Contribution {
    seat: number;
    from: number;
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
export const CONTRIBUTION_FIELDS = ["seat", "from", "damage", "boss_damage", "avoided", "blocks", "rescues", "vents", "weak_points", "objectives", "ticks", "gigaboss"] as const;

/** The table as the games write it (Contributions::canonical): sorted by seat, then step. */
export function canonical(records: Contribution[]): string {
    return [...records]
        .sort((a, b) => a.seat - b.seat || a.from - b.from)
        .map((c) => `${c.seat},${c.from},${c.damage},${c.boss_damage},${c.avoided},${c.blocks},${c.rescues},${c.vents},${c.weak_points},${c.objectives},${c.ticks},${c.gigaboss};`)
        .join("");
}

export interface ScoreParts {
    damage: number;
    avoided: number;
    support: number;
    progress: number;
    presence: number;
    total: number;
}

/**
 * The mixed score of a player (docs/RAIDS.md §7): every category capped, so that the damage alone
 * never wins. Damage: 500 at most; avoided: 300; support (shield blocks, rescues): 400;
 * progression (vents, weak points, objectives, the gigaboss down): 1 880; presence: 600.
 */
export function scoreOf(c: Omit<Contribution, "seat" | "from">): ScoreParts {
    const damage = Math.min(500, Math.floor(c.boss_damage / 20) + Math.floor(Math.max(0, c.damage - c.boss_damage) / 40));
    const avoided = Math.min(300, Math.floor(c.avoided / 5));
    const support = Math.min(400, c.blocks * 15 + c.rescues * 120);
    const progress = Math.min(480, c.vents * 80) + Math.min(400, c.weak_points * 100) + Math.min(600, c.objectives * 100) + (c.gigaboss ? 400 : 0);
    const presence = Math.min(600, Math.floor(c.ticks / 720));
    return { damage, avoided, support, progress, presence, total: damage + avoided + support + progress + presence };
}

// ------------------------------------------------------------------ rewards

export interface RaidReward {
    id: string;
    name: string;
    /** "item": a cosmetic of the catalogue (player_won_rewards); "badge": the achievement shown on the profile. */
    kind: "item" | "badge";
    ref: string;
    why: string;
    rank: "participation" | "gigaboss" | "performance";
}

/** The rewards of phase 3 (texts in French, shown as they are). */
export const RAID_REWARDS: Record<string, RaidReward> = {
    pyroclast_badge: {
        id: "pyroclast_badge",
        name: "Badge « Main de Pyroclast »",
        kind: "badge",
        ref: "communaute_pyroclast",
        why: "Présent à la chute de Pyroclast (combat validé par votre instance)",
        rank: "gigaboss",
    },
    asterion_participation: {
        id: "asterion_participation",
        name: "Titre « Assaillant d'Asterion »",
        kind: "item",
        ref: "title_assaillant_asterion",
        why: "Participation : 5 minutes de combat validées dans le raid",
        rank: "participation",
    },
    pyroclast_performance: {
        id: "pyroclast_performance",
        name: "Titre « Briseur de magma »",
        kind: "item",
        ref: "title_briseur_magma",
        why: "Score de contribution d'au moins 1 500 (dégâts, évités, soutien, évents, objectifs, présence)",
        rank: "performance",
    },
    pyroclast_top: {
        id: "pyroclast_top",
        name: "Titre « Fléau de Pyroclast »",
        kind: "item",
        ref: "title_fleau_pyroclast",
        why: "Parmi les 3 meilleurs scores de contribution de l'événement, Pyroclast vaincu",
        rank: "performance",
    },
    // 2.69 (phase 4B): the badge of each gigaboss of the abdomen and the legs (present at its fall).
    voidworm_badge: {
        id: "voidworm_badge",
        name: "Badge « Ventre de Voidworm »",
        kind: "badge",
        ref: "communaute_voidworm",
        why: "Présent à la chute de Voidworm (combat validé par votre instance)",
        rank: "gigaboss",
    },
    graviton_badge: {
        id: "graviton_badge",
        name: "Badge « Pied de Graviton »",
        kind: "badge",
        ref: "communaute_graviton",
        why: "Présent à la chute de Graviton (combat validé par votre instance)",
        rank: "gigaboss",
    },
    titanus_badge: {
        id: "titanus_badge",
        name: "Badge « Talon de Titanus »",
        kind: "badge",
        ref: "communaute_titanus",
        why: "Présent à la chute de Titanus (combat validé par votre instance)",
        rank: "gigaboss",
    },
    // 2.69 (phase 4B): the skull, the left arm and the torso.
    skullmaw_badge: {
        id: "skullmaw_badge",
        name: "Badge « Regard de Skullmaw »",
        kind: "badge",
        ref: "communaute_skullmaw",
        why: "Présent à la chute de Skullmaw (combat validé par votre instance)",
        rank: "gigaboss",
    },
    dreadfang_badge: {
        id: "dreadfang_badge",
        name: "Badge « Griffes de Dreadfang »",
        kind: "badge",
        ref: "communaute_dreadfang",
        why: "Présent à la chute de Dreadfang (combat validé par votre instance)",
        rank: "gigaboss",
    },
    heartbreaker_badge: {
        id: "heartbreaker_badge",
        name: "Badge « Cage de Heartbreaker »",
        kind: "badge",
        ref: "communaute_heartbreaker",
        why: "Présent à la chute de Heartbreaker (combat validé par votre instance)",
        rank: "gigaboss",
    },
    // 2.69 (phase 5): the exclusive rewards of the world events and their catch-up sessions (raidWorld.ts).
    ...(WORLD_REWARDS as Record<string, RaidReward>),
    // 2.72 (phase 6): the rewards of the Cœur d'Asterion's fall (raidHeart.ts).
    ...(HEART_REWARDS as Record<string, RaidReward>),
};
/** 2.69 (phase 4B): the badge of each gigaboss (raid_gigaboss_fights.boss), present at its fall. */
export const GIGABOSS_BADGES: Record<string, string> = {
    Pyroclast: "pyroclast_badge",
    Voidworm: "voidworm_badge",
    Graviton: "graviton_badge",
    Titanus: "titanus_badge",
    Skullmaw: "skullmaw_badge",
    Dreadfang: "dreadfang_badge",
    Heartbreaker: "heartbreaker_badge",
    "Cœur d'Asterion": "coeur_badge", // 2.72 (phase 6): present at its (shared) fall
};
export const PARTICIPATION_TICKS = 5 * 60 * 120;
export const PERFORMANCE_SCORE = 1500;
export const TOP_PLAYERS = 3;
/** The achievement each badge reward lets the player declare (achievements.ts). */
export const RAID_ACHIEVEMENTS: Record<string, string> = {
    communaute_pyroclast: "pyroclast_badge",
    communaute_voidworm: "voidworm_badge",
    communaute_graviton: "graviton_badge",
    communaute_titanus: "titanus_badge",
    communaute_skullmaw: "skullmaw_badge",
    communaute_dreadfang: "dreadfang_badge",
    communaute_heartbreaker: "heartbreaker_badge",
    communaute_coeur: "coeur_badge", // 2.72 (phase 6)
};

/** The prefix of the challenge_id of a raid's items in player_won_rewards. */
export const raidSource = (eventId: string) => `raid:${eventId}`;

/** The player totals of an event (every instance they played in), from the validated contributions. */
const TOTALS_SQL = `SELECT player_id, SUM(damage) AS damage, SUM(boss_damage) AS boss_damage, SUM(avoided) AS avoided, SUM(blocks) AS blocks,
        SUM(rescues) AS rescues, SUM(vents) AS vents, SUM(weak_points) AS weak_points, SUM(objectives) AS objectives, SUM(ticks) AS ticks,
        MAX(gigaboss) AS gigaboss
    FROM raid_contributions WHERE event_id = ?1 AND player_id IS NOT NULL`;

type Totals = Omit<Contribution, "seat" | "from"> & { player_id: string };

async function totalsOf(db: D1Database, eventId: string, players?: string[]): Promise<Totals[]> {
    if (players && players.length === 0) return [];
    const sql = players ? `${TOTALS_SQL} AND player_id IN (${players.map((_, i) => `?${i + 2}`).join(",")}) GROUP BY player_id` : `${TOTALS_SQL} GROUP BY player_id`;
    const rows = await db
        .prepare(sql)
        .bind(eventId, ...(players ?? []))
        .all<Totals>();
    return rows.results;
}

/**
 * Grants what the validated results of an event give to `players` (all of them when omitted):
 * participation, the gigaboss's badge (a contribution « present at the fall » in an instance whose
 * fall was accepted), the performance. INSERT OR IGNORE on the unique key: idempotent.
 */
export async function grantFor(db: D1Database, eventId: string, players?: string[]): Promise<number> {
    const now = nowIso();
    const totals = await totalsOf(db, eventId, players);
    const fallen = await db
        .prepare(
            `SELECT DISTINCT c.player_id, c.instance_id, f.boss FROM raid_contributions c JOIN raid_gigaboss_fights f ON f.instance_id = c.instance_id
             WHERE c.event_id = ?1 AND c.gigaboss = 1 AND f.phase = 4 AND c.player_id IS NOT NULL ORDER BY c.instance_id`,
        )
        .bind(eventId)
        .all<{ player_id: string; instance_id: string; boss: string }>();
    // 2.69 (phase 4B): the badge of the gigaboss whose fall each player saw (its first instance).
    const atFall = new Map<string, Map<string, string>>();
    for (const r of fallen.results) {
        const badge = GIGABOSS_BADGES[r.boss] ?? "pyroclast_badge";
        const mine = atFall.get(r.player_id) ?? new Map<string, string>();
        if (!mine.has(badge)) mine.set(badge, r.instance_id);
        atFall.set(r.player_id, mine);
    }
    const stmts: D1PreparedStatement[] = [];
    const grant = (player: string, reward: string, instance: string | null) =>
        stmts.push(
            db
                .prepare("INSERT OR IGNORE INTO raid_reward_grants (player_id, reward, event_id, instance_id, reason, granted_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)")
                .bind(player, reward, eventId, instance, RAID_REWARDS[reward]!.why, now),
        );
    for (const t of totals) {
        if (t.ticks >= PARTICIPATION_TICKS) grant(t.player_id, "asterion_participation", null);
        for (const [badge, instance] of atFall.get(t.player_id) ?? []) grant(t.player_id, badge, instance);
        if (scoreOf(t).total >= PERFORMANCE_SCORE) grant(t.player_id, "pyroclast_performance", null);
    }
    if (!stmts.length) return 0;
    const res = await db.batch(stmts);
    return res.reduce((n, r) => n + (r.meta.changes ?? 0), 0);
}

/** At the end of an event (the cron, or the first reading of its results after): the top of its ranking. */
export async function grantTop(db: D1Database, eventId: string): Promise<number> {
    const totals = await totalsOf(db, eventId);
    const fallen = new Set(
        (
            await db
                .prepare(
                    "SELECT DISTINCT c.player_id FROM raid_contributions c JOIN raid_gigaboss_fights f ON f.instance_id = c.instance_id WHERE c.event_id = ?1 AND c.gigaboss = 1 AND f.phase = 4 AND f.boss = 'Pyroclast'",
                )
                .bind(eventId)
                .all<{ player_id: string }>()
        ).results.map((r) => r.player_id),
    );
    const top = totals
        .filter((t) => fallen.has(t.player_id))
        .map((t) => ({ id: t.player_id, score: scoreOf(t).total }))
        .sort((a, b) => b.score - a.score || (a.id < b.id ? -1 : 1))
        .slice(0, TOP_PLAYERS);
    const heart = await grantHeartTop(db, eventId); // 2.72 (phase 6): the decor of the heart's top 10, once
    if (!top.length) return heart;
    const now = nowIso();
    const res = await db.batch(
        top.map((t) =>
            db
                .prepare("INSERT OR IGNORE INTO raid_reward_grants (player_id, reward, event_id, instance_id, reason, granted_at) VALUES (?1, 'pyroclast_top', ?2, NULL, ?3, ?4)")
                .bind(t.id, eventId, RAID_REWARDS.pyroclast_top!.why, now),
        ),
    );
    return heart + res.reduce((n, r) => n + (r.meta.changes ?? 0), 0);
}

/** The ended events whose top was not granted yet (the cron of raids.ts). */
export async function grantEndedTops(db: D1Database, now: string): Promise<number> {
    let n = 0;
    try {
        const ended = await db
            .prepare(
                // 2.74: from the events that just ended (index on ends_at, migration 0149), never every fight ever fought.
                `SELECT e.id FROM raid_events e
                 WHERE e.ends_at <= ?1 AND e.ends_at > ?2
                   AND EXISTS (SELECT 1 FROM raid_gigaboss_fights f WHERE f.event_id = e.id AND f.phase = 4 AND f.boss = 'Pyroclast')
                   AND NOT EXISTS (SELECT 1 FROM raid_reward_grants g WHERE g.event_id = e.id AND g.reward = 'pyroclast_top')`,
            )
            .bind(now, new Date(Date.parse(now) - 7 * 86400000).toISOString())
            .all<{ id: string }>();
        for (const e of ended.results) n += await grantTop(db, e.id);
    } catch {
        /* before migration 0143 */
    }
    return n + (await grantEndedHeartTops(db, now)); // 2.72 (phase 6)
}

interface GrantRow {
    id: number;
    reward: string;
    event_id: string;
    reason: string;
    granted_at: string;
    claimed_at: string | null;
}

/** A reward of a player as the game shows it (« Récompenses à récupérer », the results). */
export function grantView(g: GrantRow) {
    const r = RAID_REWARDS[g.reward];
    return {
        id: g.id,
        reward: g.reward,
        name: r?.name ?? g.reward,
        kind: r?.kind ?? "item",
        ref: r?.ref ?? "",
        why: g.reason,
        event_id: g.event_id,
        granted_at: g.granted_at,
        claimed: g.claimed_at !== null,
        claimed_at: g.claimed_at,
    };
}

/** The rewards of a player: those to claim, and the ids of those claimed (their badges, the game's cache). */
export async function rewardsOf(db: D1Database, playerId: string) {
    if (!playerId) return { pending: [], claimed: [] as string[] };
    try {
        const rows = await db
            .prepare("SELECT id, reward, event_id, reason, granted_at, claimed_at FROM raid_reward_grants WHERE player_id = ?1 ORDER BY granted_at DESC, id DESC LIMIT 100")
            .bind(playerId)
            .all<GrantRow>();
        return {
            pending: rows.results.filter((g) => !g.claimed_at).map(grantView),
            claimed: [...new Set(rows.results.filter((g) => g.claimed_at).map((g) => g.reward))],
        };
    } catch {
        return { pending: [], claimed: [] as string[] }; // before migration 0143
    }
}

/** The raid achievements a player may declare: those of the rewards they claimed. */
export async function claimedAchievements(db: D1Database, playerId: string): Promise<Set<string>> {
    const out = new Set<string>();
    try {
        const rows = await db.prepare("SELECT DISTINCT reward FROM raid_reward_grants WHERE player_id = ?1 AND claimed_at IS NOT NULL").bind(playerId).all<{ reward: string }>();
        const claimed = new Set(rows.results.map((r) => r.reward));
        for (const [achievement, reward] of Object.entries(RAID_ACHIEVEMENTS)) if (claimed.has(reward)) out.add(achievement);
    } catch {
        /* before migration 0143 */
    }
    return out;
}

/**
 * POST /raids/rewards/claim {id?}: claims one reward (`id`) or every one waiting. Idempotent: a
 * reward already claimed stays as it is, a repeated request claims nothing more, and the items of
 * the claimed rewards are (re)written to player_won_rewards in the same transaction.
 */
export async function claim(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    const db = ctx.env.DB;
    await rateLimit(ctx, RULES.raidClaim, "player:" + me);
    const body = await readJsonObject(ctx);
    const v = new Validator(body, ["id"]);
    const id = v.int("id", { min: 1, max: 2 ** 31 - 1, optional: true });
    v.done();
    const now = nowIso();
    if (id !== undefined) {
        const g = await db.prepare("SELECT id FROM raid_reward_grants WHERE id = ?1 AND player_id = ?2").bind(id, me).first<{ id: number }>();
        if (!g) throw Errors.notFound("REWARD_NOT_FOUND", "Récompense introuvable.");
    }
    const items = Object.values(RAID_REWARDS).filter((r) => r.kind === "item");
    const res = await db.batch([
        id !== undefined
            ? db.prepare("UPDATE raid_reward_grants SET claimed_at = ?3 WHERE id = ?1 AND player_id = ?2 AND claimed_at IS NULL").bind(id, me, now)
            : db.prepare("UPDATE raid_reward_grants SET claimed_at = ?2 WHERE player_id = ?1 AND claimed_at IS NULL").bind(me, now),
        // Every item of a claimed reward unlocked (once: the primary key of player_won_rewards).
        ...items.map((r) =>
            db
                .prepare(
                    `INSERT OR IGNORE INTO player_won_rewards (player_id, challenge_id, reward_kind, reward_ref, granted_at)
                     SELECT g.player_id, 'raid:' || g.event_id, 'item', ?2, g.claimed_at FROM raid_reward_grants g
                     WHERE g.player_id = ?1 AND g.reward = ?3 AND g.claimed_at IS NOT NULL`,
                )
                .bind(me, r.ref, r.id),
        ),
    ]);
    const claimed = res[0]!.meta.changes ?? 0;
    const mine = await rewardsOf(db, me);
    return json(ctx, { claimed, rewards: mine.pending, claimed_rewards: mine.claimed });
}

// ------------------------------------------------------------------ Asterion's body

/** What destroying each part does (data/raids/asterion.json; 2.69: the abdomen and the legs too). */
export const PART_EFFECTS: Record<string, string> = {
    bras_droit: "Plus de pluies de lave : le poing de magma d'Asterion est éteint.",
    abdomen: "Plus de renforts : les parasites d'Asterion sont chassés.",
    jambe_gauche: "Plus de pièges de gravité : ses générateurs sont coupés.",
    jambe_droite: "Ses charges ralentissent : Asterion chancelle.",
    crane: "Asterion est aveuglé : ses attaques perdent en précision.",
    bras_gauche: "Plus de balayages : les griffes d'Asterion sont arrachées.",
    torse: "La cage thoracique est brisée : la voie du cœur s'ouvre.",
    coeur: "Asterion est tombé : son cœur a cédé sous les coups de tous les clans.", // 2.72 (phase 6)
};
const BRAS_DROIT_ID = "asterion_bras_droit";
export const GIGABOSS_OBJECTIVES: Record<string, { part: string; boss: string }> = {
    main_pyroclast: { part: "bras_droit", boss: "Pyroclast" },
    abd_voidworm: { part: "abdomen", boss: "Voidworm" }, // 2.69 (phase 4B)
    jg_graviton: { part: "jambe_gauche", boss: "Graviton" },
    jd_titanus: { part: "jambe_droite", boss: "Titanus" },
    crane_skullmaw: { part: "crane", boss: "Skullmaw" },
    bg_dreadfang: { part: "bras_gauche", boss: "Dreadfang" },
    torse_heartbreaker: { part: "torse", boss: "Heartbreaker" },
    coeur_asterion: { part: "coeur", boss: "Cœur d'Asterion" }, // 2.72 (phase 6): its shared fall, written once
};
/** 2.69: the part of the titan a fight's branch is (raid_gigaboss_fights.branch), when its gigaboss is fought. */
const FOUGHT_PARTS = new Set(Object.values(GIGABOSS_OBJECTIVES).map((g) => g.part));
const partOfBranch = (branch: string) => (branch.startsWith("asterion_") ? branch.slice("asterion_".length) : branch);

export interface BodyState {
    body: Record<string, string>;
    effects: { part: string; text: string; since: string }[];
}

/**
 * The state of Asterion's parts in each event, from the validated state only: « detruit » once the
 * branch's gigaboss objective was accepted, « affaibli » when an instance's fight reached its 2nd
 * phase, « attaque » when the branch has validated progress (an objective, a fight begun).
 */
export async function bodiesOf(db: D1Database, eventIds: string[]): Promise<Map<string, BodyState>> {
    const out = new Map<string, BodyState>();
    const ids = [...new Set(eventIds)].slice(0, 50);
    if (!ids.length) return out;
    const marks = ids.map((_, i) => `?${i + 1}`).join(",");
    try {
        const [objR, fightR] = await db.batch([
            db.prepare(`SELECT event_id, branch, objective_id, accepted_at FROM raid_branch_objectives WHERE event_id IN (${marks})`).bind(...ids),
            db.prepare(`SELECT event_id, branch, MAX(phase) AS phase FROM raid_gigaboss_fights WHERE event_id IN (${marks}) GROUP BY event_id, branch`).bind(...ids),
        ]);
        const fights = fightR!.results as { event_id: string; branch: string; phase: number }[];
        for (const id of ids) out.set(id, { body: {}, effects: [] });
        for (const r of objR!.results as { event_id: string; branch: string; objective_id: string; accepted_at: string }[]) {
            const s = out.get(r.event_id)!;
            const giga = GIGABOSS_OBJECTIVES[r.objective_id];
            if (giga) {
                s.body[giga.part] = "detruit";
                s.effects.push({ part: giga.part, text: PART_EFFECTS[giga.part] ?? "", since: r.accepted_at });
            } else if (r.branch === BRAS_DROIT_ID && !s.body.bras_droit) {
                // 2.68: only the Right arm's own progress (the six other branches have their titan map).
                s.body.bras_droit = "attaque";
            }
        }
        // 2.69 (phase 4B): each fought part, by its own fights (the Bras droit, the Abdomen, the legs).
        for (const f of fights) {
            const s = out.get(f.event_id)!;
            const part = partOfBranch(f.branch);
            if (!FOUGHT_PARTS.has(part) || s.body[part] === "detruit") continue;
            s.body[part] = f.phase >= 2 ? "affaibli" : "attaque";
        }
    } catch {
        /* before migration 0142 / 0143 */
    }
    return out;
}

// ------------------------------------------------------------------ results

interface ResultRow extends Totals {
    username: string;
    clan_tag: string | null;
    clan_id: string | null;
}

/**
 * GET /raids/events/{eventId}/results: the results of an event (maquette « Résultats »), from the
 * validated tables only: the clans (sum of their players' scores), the players with every
 * category, the gigaboss's fights, my instance's table still waiting for its majority.
 */
export async function results(ctx: Ctx, eventRow: Parameters<typeof raidState>[0] & { id: string }, me: string): Promise<Response> {
    const db = ctx.env.DB;
    const now = nowIso();
    const state = raidState(eventRow, now);
    if (state === "termine") await grantTop(db, eventRow.id);
    const [rowsR, fightsR, pendingR, myR] = await db.batch([
        db
            .prepare(
                `SELECT t.*, p.username, c.tag AS clan_tag, c.id AS clan_id FROM (${TOTALS_SQL} GROUP BY player_id) t
                 JOIN players p ON p.id = t.player_id
                 LEFT JOIN raid_registrations r ON r.event_id = ?1 AND r.player_id = t.player_id LEFT JOIN clans c ON c.id = r.clan_id`,
            )
            .bind(eventRow.id),
        db
            .prepare(
                `SELECT f.instance_id, f.phase, f.defeated_at, f.boss, i.group_no, c.tag FROM raid_gigaboss_fights f
                 JOIN raid_instances i ON i.id = f.instance_id JOIN clans c ON c.id = i.clan_id WHERE f.event_id = ?1 ORDER BY f.phase DESC, f.defeated_at`,
            )
            .bind(eventRow.id),
        // My instances' tables not accepted yet (« en attente de validation »).
        db
            .prepare(
                `SELECT r.instance_id, r.report_key, COUNT(*) AS n FROM raid_reports r JOIN raid_instance_members m ON m.instance_id = r.instance_id AND m.player_id = ?2
                 JOIN raid_instances i ON i.id = r.instance_id
                 WHERE i.event_id = ?1 AND r.report_key LIKE 'r:%'
                   AND NOT EXISTS (SELECT 1 FROM raid_accepted a WHERE a.instance_id = r.instance_id AND a.report_key = r.report_key)
                   AND CAST(substr(r.report_key, 3) AS INTEGER) > COALESCE((SELECT MAX(x.tick) FROM raid_contributions x WHERE x.instance_id = r.instance_id), -1)
                 GROUP BY r.instance_id, r.report_key ORDER BY r.created_at DESC LIMIT 5`,
            )
            .bind(eventRow.id, me),
        db.prepare("SELECT MAX(accepted_at) AS at FROM raid_contributions WHERE event_id = ?1 AND player_id = ?2").bind(eventRow.id, me),
    ]);
    const rows = rowsR!.results as ResultRow[];
    const players = rows
        .map((r) => {
            const s = scoreOf(r);
            return {
                id: r.player_id,
                username: r.username,
                clan_tag: r.clan_tag ?? "",
                score: s.total,
                parts: s,
                damage: Math.round(r.damage / 10),
                boss_damage: Math.round(r.boss_damage / 10),
                avoided: Math.round(r.avoided / 10),
                support: r.blocks + r.rescues,
                blocks: r.blocks,
                rescues: r.rescues,
                vents: r.vents,
                weak_points: r.weak_points,
                objectives: r.objectives,
                gigaboss: r.gigaboss ? 1 : 0,
                minutes: Math.floor(r.ticks / 7200),
                seconds: Math.floor(r.ticks / 120),
                progress: s.progress,
                me: r.player_id === me,
                clan_id: r.clan_id,
            };
        })
        .sort((a, b) => b.score - a.score || (a.username < b.username ? -1 : 1));
    const clans = new Map<string, { tag: string; points: number; players: number; gigaboss: number }>();
    for (const p of players) {
        if (!p.clan_id) continue;
        const c = clans.get(p.clan_id) ?? { tag: p.clan_tag, points: 0, players: 0, gigaboss: 0 };
        c.points += p.score;
        c.players += 1;
        c.gigaboss = Math.max(c.gigaboss, p.gigaboss);
        clans.set(p.clan_id, c);
    }
    const fights = fightsR!.results as { instance_id: string; phase: number; defeated_at: string | null; boss: string; group_no: number; tag: string }[];
    const pending = pendingR!.results as { instance_id: string; report_key: string; n: number }[];
    const mine = await rewardsOf(db, me);
    return json(ctx, {
        server_time: now,
        event: { id: eventRow.id, state, final: state === "termine" },
        gigaboss: {
            boss: fights.find((f) => f.phase === 4)?.boss ?? "Pyroclast",
            defeated: fights.some((f) => f.phase === 4),
            // 2.69 (phase 4B): several gigaboss: each fight names its own, the fallen ones listed once.
            defeated_bosses: [...new Set(fights.filter((f) => f.phase === 4).map((f) => f.boss))],
            fights: fights.map((f) => ({ boss: f.boss, group: `[${f.tag}]·${f.group_no}`, phase: f.phase, defeated_at: f.defeated_at })),
        },
        clans: [...clans.entries()]
            .map(([id, c]) => ({ id, ...c }))
            .sort((a, b) => b.points - a.points || (a.tag < b.tag ? -1 : 1))
            .slice(0, 50)
            .map((c, i) => ({ rank: i + 1, ...c })),
        players: players.slice(0, 100).map((p, i) => ({ rank: i + 1, ...p, clan_id: undefined })),
        my: {
            rank: players.findIndex((p) => p.me) + 1,
            validated_at: (myR!.results[0] as { at: string | null } | undefined)?.at ?? null,
            pending: pending.length > 0,
            pending_reports: pending.reduce((n, p) => Math.max(n, p.n), 0),
        },
        rewards: mine.pending.filter((g) => g.event_id === eventRow.id),
        rewards_claimed: mine.claimed,
    });
}
