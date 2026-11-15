// Rewards of the challenges (2.34): cosmetics of the Collection unlocked by challenge results
// (docs/online/CHALLENGES_PERIODS.md §6). Only the runs this server VERIFIED count, never what
// the game says: per kind of challenge, how many challenges have a verified run at a tier or
// better (received in time or late), and the longest series of consecutive periods (days,
// ISO weeks, months) with a verified run received in time. The same numbers decide what may
// be worn online (cosmetics.ts) and go to the game in GET /challenges/me (`verified`).
import type { Ctx } from "./http";

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

/** What the player's VERIFIED challenge runs count for the rewards (one row per challenge played). */
export async function challengeRewardProgress(ctx: Ctx, playerId: string): Promise<ChallengeRewardProgress> {
    const rows = await ctx.env.DB.prepare(
        `SELECT c.kind, c.date,
                MAX(CASE r.tier WHEN 'gold' THEN 3 WHEN 'silver' THEN 2 WHEN 'bronze' THEN 1 ELSE 0 END) AS tier,
                MAX(CASE WHEN r.late = 0 THEN 1 ELSE 0 END) AS in_time
         FROM challenge_runs r JOIN challenges c ON c.id = r.challenge_id
         WHERE r.player_id = ?1 AND r.status = 'VERIFIED'
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
