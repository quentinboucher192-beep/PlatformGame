// Rewards of the challenges (2.35): cosmetics of the Collection unlocked by challenge results
// (docs/online/CHALLENGES_PERIODS.md §6). Only the runs this server VERIFIED count, never what
// the game says: per kind of challenge, how many challenges have a verified run at a tier or
// better (received in time or late), and the longest series of consecutive periods (days,
// ISO weeks, months) with a verified run received in time. The same numbers decide what may
// be worn online (cosmetics.ts) and go to the game in GET /challenges/me (`verified`).
import cosmeticCatalog from "./cosmeticCatalog.json";
import { nowIso, type Ctx } from "./http";

export const REWARD_KINDS = ["daily", "weekly", "monthly"] as const;
export type RewardKind = (typeof REWARD_KINDS)[number];
export const REWARD_TIERS = ["bronze", "silver", "gold"] as const;
export type RewardTier = (typeof REWARD_TIERS)[number];

/** `gold`, `silver`, `bronze`: challenges with a verified run at that tier or better. */
export interface KindProgress {
    gold: number;
    silver: number;
    bronze: number;
    best_streak: number;
}
export type ChallengeRewardProgress = Record<RewardKind, KindProgress>;

/** `count` challenges of `kind` (or of any kind) with a verified run at `tier` or better. */
export interface ResultsCondition {
    kind: RewardKind | "any";
    tier: RewardTier;
    count: number;
}
/** `days` periods of `kind` in a row with a verified run received in time. */
export interface StreakCondition {
    kind: RewardKind;
    days: number;
}

/** One challenge played: its best verified tier (1 bronze .. 3 gold), a verified run in time. */
export interface VerifiedChallenge {
    kind: string;
    date: string;
    tier: number;
    in_time: number;
}

const isKind = (k: string): k is RewardKind => (REWARD_KINDS as readonly string[]).includes(k);

export function emptyRewardProgress(): ChallengeRewardProgress {
    const one = (): KindProgress => ({ gold: 0, silver: 0, bronze: 0, best_streak: 0 });
    return { daily: one(), weekly: one(), monthly: one() };
}

/**
 * A number that grows by one from a period to the next, from the first day of the period
 * (`date`, "AAAA-MM-JJ": the day, the Monday of the ISO week, the 1st of the month).
 */
function periodIndex(kind: RewardKind, date: string): number | null {
    const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(date);
    if (!m) return null;
    if (kind === "monthly") return Number(m[1]) * 12 + Number(m[2]) - 1;
    const day = Math.floor(Date.parse(`${date}T00:00:00Z`) / 86400000);
    if (!Number.isFinite(day)) return null;
    // 1970-01-05 (day 4) was a Monday: the Mondays of consecutive weeks give consecutive numbers.
    return kind === "weekly" ? Math.floor((day - 4) / 7) : day;
}

export function rewardProgressFrom(rows: VerifiedChallenge[]): ChallengeRewardProgress {
    const out = emptyRewardProgress();
    const periods: Record<RewardKind, Set<number>> = { daily: new Set(), weekly: new Set(), monthly: new Set() };
    for (const r of rows) {
        if (!isKind(r.kind)) continue;
        const k = out[r.kind];
        if (r.tier >= 3) k.gold++;
        if (r.tier >= 2) k.silver++;
        if (r.tier >= 1) k.bronze++;
        const i = r.in_time ? periodIndex(r.kind, r.date) : null;
        if (i !== null) periods[r.kind].add(i);
    }
    for (const kind of REWARD_KINDS) {
        let best = 0;
        let run = 0;
        let prev = Number.NaN;
        for (const i of [...periods[kind]].sort((a, b) => a - b)) {
            run = i === prev + 1 ? run + 1 : 1;
            best = Math.max(best, run);
            prev = i;
        }
        out[kind].best_streak = best;
    }
    return out;
}

/**
 * What the player's VERIFIED challenge results count for the rewards (one row per challenge
 * played): the finished runs, and (2.41) on a « Finir le niveau » challenge the attempts whose
 * progress, computed by the server from their replay, reached a tier (Bronze or Argent).
 */
export async function challengeRewardProgress(ctx: Ctx, playerId: string): Promise<ChallengeRewardProgress> {
    const rows = await ctx.env.DB.prepare(
        `SELECT c.kind, c.date,
                MAX(CASE x.tier WHEN 'gold' THEN 3 WHEN 'silver' THEN 2 WHEN 'bronze' THEN 1 ELSE 0 END) AS tier,
                MAX(CASE WHEN x.late = 0 THEN 1 ELSE 0 END) AS in_time
         FROM (SELECT r.challenge_id, r.tier, r.late FROM challenge_runs r WHERE r.player_id = ?1 AND r.status = 'VERIFIED'
               UNION ALL
               SELECT a.challenge_id, a.tier, a.late FROM challenge_attempts a
               WHERE a.player_id = ?1 AND a.status = 'VERIFIED' AND a.tier IS NOT NULL) x
         JOIN challenges c ON c.id = x.challenge_id
         GROUP BY c.id`,
    )
        .bind(playerId)
        .all<VerifiedChallenge>();
    return rewardProgressFrom(rows.results);
}

/** Challenges of `kind` (every kind: "any") with a verified run at `tier` or better. */
export function resultsCount(p: ChallengeRewardProgress, kind: RewardKind | "any", tier: RewardTier): number {
    if (kind === "any") return REWARD_KINDS.reduce((s, k) => s + p[k][tier], 0);
    return p[kind][tier];
}

export function resultsMet(p: ChallengeRewardProgress, c: ResultsCondition): boolean {
    return resultsCount(p, c.kind, c.tier) >= c.count;
}

export function streakMet(p: ChallengeRewardProgress, c: StreakCondition): boolean {
    return p[c.kind].best_streak >= c.days;
}

// ------------------------------------------------- rewards of one challenge (2.43)
//
// An administrator gives a challenge that has not begun its own rewards (ADMINISTRATION ›
// Calendrier des défis › Récompenses, adminChallenges.ts): each one an item of the catalogue
// (src/cosmeticCatalog.json), a title created on the server (server_titles, worn as
// "srv:<id>": no game update needed) or a suit created in ADMINISTRATION › Créations
// (creations.ts, worn as "srvsuit:<id>", drawn by the game from its three colours), and a condition on that challenge's results received in
// time and VERIFIED by this server: a tier reached (that tier or better, both objectives: the
// finished runs and, on « Finir le niveau », the attempts whose progress reached Bronze or
// Argent), a place of the final ranking (the challenge's main board, ties as the board breaks
// them: the earlier submission first), or the level finished. Granted once the period is over
// and settled (GRANT_DELAY_MS), by the daily cron or a reading of the results; never twice,
// never taken back. The same items then unlock in the Collection (cosmetics.ts: `won`).

export const PRIZE_KINDS = ["item", "title", "suit"] as const;
export type PrizeKind = (typeof PRIZE_KINDS)[number];
export const PRIZE_CONDITIONS = ["tier", "top", "finished"] as const;
export type PrizeCondition = (typeof PRIZE_CONDITIONS)[number];
export const TITLE_COLORS = ["gold", "silver", "cyan", "violet"] as const;
export type TitleColor = (typeof TITLE_COLORS)[number];
/** Rewards of one challenge, at most. */
export const MAX_PRIZES = 6;
/** « Top N du classement final »: N from 1 to this. */
export const MAX_TOP = 100;
/** A title created on the server: at most this many characters. */
export const TITLE_MAX_CHARS = 32;
/** The rewards of a challenge wait this long after its end (runs in time, their replays checked). */
export const GRANT_DELAY_MS = 60 * 60 * 1000;

/** A reward of a challenge and its condition (value: tier 1..3, top 1..MAX_TOP, finished 0). */
export interface PrizeRule {
    kind: PrizeKind;
    ref: string;
    condition: PrizeCondition;
    value: number;
}

interface CatalogEntry {
    id: string;
    category: string;
    name: string;
    default?: boolean;
}
const CATALOG_ITEMS = new Map((cosmeticCatalog as unknown as { items: CatalogEntry[] }).items.map((i) => [i.id, i]));

/** An item of the catalogue a challenge may give (never a category's default). */
export function prizeItem(id: string): CatalogEntry | null {
    const i = CATALOG_ITEMS.get(id);
    return i && !i.default ? i : null;
}

export const titleRef = (id: number) => `srv:${id}`;
/** The server_titles id of "srv:<id>", null for anything else. */
export function titleIdOf(ref: string | null | undefined): number | null {
    const m = /^srv:([1-9]\d{0,9})$/.exec(ref ?? "");
    return m ? Number(m[1]) : null;
}

export const suitRef = (id: number) => `srvsuit:${id}`;
/** The creations id of a suit "srvsuit:<id>", null for anything else. */
export function suitIdOf(ref: string | null | undefined): number | null {
    const m = /^srvsuit:([1-9]\d{0,9})$/.exec(ref ?? "");
    return m ? Number(m[1]) : null;
}

/** A suit created on the server as the games read it: its three colours ("#rrggbb"). */
export interface SuitColors {
    suit: string;
    accent: string;
    scarf: string;
}
export interface SuitJson {
    id: string;
    name: string;
    colors: SuitColors;
}
export function suitJson(ref: string, name: string, suit: string, accent: string, scarf: string): SuitJson {
    return { id: ref, name, colors: { suit, accent, scarf } };
}

/** Letters, marks, digits, punctuation and spaces: what the game's fonts draw (no emoji, no control). */
const TITLE_TEXT_RE = /^[\p{L}\p{M}\p{N}\p{P} ]+$/u;

/** A title's text as stored: trimmed, inner spaces collapsed; null when not acceptable. */
export function cleanTitleText(raw: string): string | null {
    const t = raw.normalize("NFC").replace(/\s+/gu, " ").trim();
    if (!t || [...t].length > TITLE_MAX_CHARS || !TITLE_TEXT_RE.test(t)) return null;
    return t;
}

/** The text compared for duplicates: without accents nor case. */
export function titleKey(text: string): string {
    return text
        .normalize("NFD")
        .replace(/\p{M}/gu, "")
        .toLowerCase()
        .replace(/\s+/gu, " ")
        .trim();
}

/** The server titles (`t`) and the suits created on the server (`s`) of the rules `r`. */
const PRIZE_JOINS = `LEFT JOIN server_titles t ON r.reward_kind = 'title' AND t.id = CAST(substr(r.reward_ref, 5) AS INTEGER)
     LEFT JOIN creations s ON r.reward_kind = 'suit' AND s.kind = 'suit' AND s.id = CAST(substr(r.reward_ref, 9) AS INTEGER)`;

/** SQL (a column of challenges `c`): its rewards as a JSON array, the titles (text, colour) and suits (name, colours) joined. */
export const PRIZES_SQL = `(SELECT json_group_array(json_object('position', r.position, 'kind', r.reward_kind, 'ref', r.reward_ref,
      'condition', r.condition, 'value', r.condition_value, 'title_text', t.text, 'title_color', t.color,
      'suit_name', s.name, 'suit_color', s.suit_color, 'accent_color', s.accent_color, 'scarf_color', s.scarf_color))
    FROM challenge_reward_rules r ${PRIZE_JOINS}
    WHERE r.challenge_id = c.id)`;

/** The rules of some challenges (?1: a JSON array of their ids), the titles and suits joined. */
export const PRIZE_ROWS_SQL = `SELECT r.challenge_id, r.position, r.reward_kind AS kind, r.reward_ref AS ref, r.condition, r.condition_value AS value,
       t.text AS title_text, t.color AS title_color, s.name AS suit_name, s.suit_color, s.accent_color, s.scarf_color
     FROM challenge_reward_rules r ${PRIZE_JOINS}`;

export interface PrizeRow {
    position: number;
    kind: string;
    ref: string;
    condition: string;
    value: number;
    title_text: string | null;
    title_color: string | null;
    suit_name?: string | null;
    suit_color?: string | null;
    accent_color?: string | null;
    scarf_color?: string | null;
}

/**
 * A reward as the game reads it: `item` (its id, category and name), `title` (its text and
 * colour) or `suit` (a suit created on the server: its name and colours; the key is only
 * present for one, so the earlier forms are unchanged).
 */
export interface PrizeJson {
    kind: PrizeKind;
    ref: string;
    condition: PrizeCondition;
    value: number;
    item: { id: string; category: string; name: string } | null;
    title: { id: string; text: string; color: string } | null;
    suit?: SuitJson;
}

/** The rewards (rows or the JSON of PRIZES_SQL) in their order; an unknown item or title is left out. */
export function prizesJson(raw: string | PrizeRow[] | null | undefined): PrizeJson[] {
    let rows: PrizeRow[] = [];
    if (Array.isArray(raw)) rows = raw;
    else if (typeof raw === "string")
        try {
            const v = JSON.parse(raw) as unknown;
            if (Array.isArray(v)) rows = v as PrizeRow[];
        } catch {
            rows = [];
        }
    const out: PrizeJson[] = [];
    for (const r of [...rows].sort((a, b) => a.position - b.position)) {
        if (!(PRIZE_CONDITIONS as readonly string[]).includes(r.condition)) continue;
        const base = { ref: r.ref, condition: r.condition as PrizeCondition, value: r.value };
        if (r.kind === "item") {
            const i = prizeItem(r.ref);
            if (i) out.push({ kind: "item", ...base, item: { id: i.id, category: i.category, name: i.name }, title: null });
        } else if (r.kind === "title" && r.title_text) {
            out.push({ kind: "title", ...base, item: null, title: { id: r.ref, text: r.title_text, color: r.title_color ?? "gold" } });
        } else if (r.kind === "suit" && r.suit_name && r.suit_color && r.accent_color && r.scarf_color) {
            out.push({ kind: "suit", ...base, item: null, title: null, suit: suitJson(r.ref, r.suit_name, r.suit_color, r.accent_color, r.scarf_color) });
        }
    }
    return out;
}

/** The name a reward goes by (notifications, the Journal). */
export function prizeName(p: { kind: string; ref: string; title_text?: string | null; suit_name?: string | null }): string {
    if (p.kind === "title") return p.title_text ?? p.ref;
    if (p.kind === "suit") return p.suit_name ?? p.ref;
    return prizeItem(p.ref)?.name ?? p.ref;
}

/** What a player won: catalogue items, server titles ("srv:<id>") and suits created on the server ("srvsuit:<id>"). */
export interface WonRewards {
    items: Set<string>;
    titles: Set<string>;
    suits: Set<string>;
}

export async function wonRewards(ctx: Ctx, playerId: string): Promise<WonRewards> {
    const rows = await ctx.env.DB.prepare("SELECT DISTINCT reward_kind AS kind, reward_ref AS ref FROM player_won_rewards WHERE player_id = ?1")
        .bind(playerId)
        .all<{ kind: string; ref: string }>();
    const out: WonRewards = { items: new Set(), titles: new Set(), suits: new Set() };
    for (const r of rows.results) (r.kind === "title" ? out.titles : r.kind === "suit" ? out.suits : out.items).add(r.ref);
    return out;
}

/** The challenge whose rewards are granted. */
export interface PrizeChallenge {
    id: string;
    kind: string;
    period: string;
    date: string;
}

const TIER_SQL = (col: string) => `CASE ${col} WHEN 'gold' THEN 3 WHEN 'silver' THEN 2 WHEN 'bronze' THEN 1 ELSE 0 END`;

/**
 * Grants the rewards of an ended challenge, once: `rankedSql` is its main board (the times, or
 * the progress of « Finir le niveau »), every active player with a `rank`, bound ?1 = the
 * challenge, ?2 = 0 (the runs received in time). One D1 batch (a transaction): the rewards won
 * (a reward already won is kept as it was), one notification per player who won something now
 * (« challenge_reward », unless turned off), and the mark that the challenge is granted.
 * Returns the rewards newly won.
 */
export async function grantPrizes(ctx: Ctx, c: PrizeChallenge, rankedSql: string): Promise<number> {
    const db = ctx.env.DB;
    const rules = await db.prepare(`${PRIZE_ROWS_SQL} WHERE r.challenge_id = ?1 ORDER BY r.position`).bind(c.id).all<PrizeRow>();
    const now = nowIso();
    const stmts: D1PreparedStatement[] = [];
    const insert = (select: string) =>
        `INSERT OR IGNORE INTO player_won_rewards (player_id, challenge_id, reward_kind, reward_ref, granted_at)
         SELECT w.pid, ?1, ?3, ?4, ?5 FROM (${select}) w JOIN players p ON p.id = w.pid WHERE p.account_status = 'active'`;
    for (const r of rules.results) {
        // A reward the server no longer knows (an item gone from the catalogue) is not given.
        if (r.kind === "item" ? !prizeItem(r.ref) : r.kind === "suit" ? !r.suit_name : !r.title_text) continue;
        if (r.condition === "tier") {
            const best = `SELECT x.player_id AS pid FROM (
                SELECT player_id, ${TIER_SQL("tier")} AS t FROM challenge_runs WHERE challenge_id = ?1 AND status = 'VERIFIED' AND late = ?2
                UNION ALL
                SELECT player_id, ${TIER_SQL("tier")} FROM challenge_attempts WHERE challenge_id = ?1 AND status = 'VERIFIED' AND late = ?2 AND tier IS NOT NULL) x
              GROUP BY x.player_id HAVING MAX(x.t) >= ?6`;
            stmts.push(db.prepare(insert(best)).bind(c.id, 0, r.kind, r.ref, now, Math.min(3, Math.max(1, r.value))));
        } else if (r.condition === "top") {
            stmts.push(db.prepare(insert(`SELECT player_id AS pid FROM (${rankedSql}) WHERE rank <= ?6`)).bind(c.id, 0, r.kind, r.ref, now, r.value));
        } else if (r.condition === "finished") {
            const done = "SELECT DISTINCT player_id AS pid FROM challenge_runs WHERE challenge_id = ?1 AND status = 'VERIFIED' AND late = ?2";
            stmts.push(db.prepare(insert(done)).bind(c.id, 0, r.kind, r.ref, now));
        }
    }
    const granted = stmts.length;
    if (granted > 0) {
        // The names of what was won, by reference (the notification's sentence).
        const named = rules.results;
        const cases = named.map((_r, i) => `WHEN ?${6 + i * 2} THEN ?${7 + i * 2}`).join(" ");
        stmts.push(
            db
                .prepare(
                    `INSERT INTO notifications (id, player_id, kind, data, count, created_at)
                     SELECT lower(hex(randomblob(16))), w.player_id, 'challenge_reward',
                            json_object('challenge_id', ?1, 'challenge_kind', ?2, 'period', ?3, 'date', ?4,
                                        'rewards', json_group_array(CASE w.reward_ref ${cases} ELSE w.reward_ref END)), 1, ?5
                     FROM player_won_rewards w
                     WHERE w.challenge_id = ?1 AND w.granted_at = ?5
                       AND instr(',' || (SELECT notifications_off FROM players WHERE id = w.player_id) || ',', ',challenge_reward,') = 0
                     GROUP BY w.player_id`,
                )
                .bind(c.id, c.kind, c.period, c.date, now, ...named.flatMap((r) => [r.ref, prizeName(r)])),
        );
    }
    stmts.push(db.prepare("INSERT INTO challenge_reward_grants (challenge_id, granted_at) VALUES (?1, ?2) ON CONFLICT (challenge_id) DO NOTHING").bind(c.id, now));
    const res = await db.batch(stmts);
    return res.slice(0, granted).reduce((s, r) => s + (r.meta.changes ?? 0), 0);
}
