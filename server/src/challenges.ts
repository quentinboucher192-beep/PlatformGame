// Challenges (« Défis », games 2.32 and 2.33). The contracts with the game are
// docs/online/DAILY_CHALLENGES.md (2.32) and docs/online/CHALLENGES_PERIODS.md (2.33): three
// kinds of challenges, each over a UTC period — the day (`daily-AAAA-MM-JJ`), the ISO 8601
// week (`weekly-AAAA-Www`) and the month (`monthly-AAAA-MM`) — drawn by the server among the
// ranked campaign levels (the week and the month from the hardest ones) or chosen by the
// administrator (scripts/challenge-set.mjs, campaign or community level), with their rules
// (hearts, power slots, imposed powers, stations, checkpoints), the canonical form of the
// rules and its SHA-256, and the tiers. Challenges are drawn ahead (days to J+7, weeks to
// S+2, months to M+1) so that the administrator can replace them, but a challenge is never
// shown before its period begins, and once begun it never changes.
//
// Runs follow the records: the player comes from the session, idempotence by attempt_id,
// tier computed by the server, statuses PENDING / VERIFIED / REJECTED with the decision and
// trust rules of the records, and the replay (format 4 with the challenge section) checked by
// the server (structure, level revision, duration, plausible trajectory, health never above
// the challenge's hearts, section identical to the canonical rules). « Vérifié » means
// « replay checked by the server », not re-simulated. A run received after the end of its
// challenge (+ 10 min) is accepted « hors délai » (late): it is ranked only in the board
// « Depuis » (period=all) and never counts for a streak.
//
// « Plus loin » (2.33): on a challenge whose level version has a race layout, an attempt that
// does not reach the end is sent with its replay; the server computes its progress score from
// the replay with the analysis of the « course à la vérification » (race.ts), never taking
// the game's, and a finished run counts 100. Only the player's best attempt keeps its replay.

import catalogJson from "./challengeCatalog.json";
import { audit, requireAuth, type AuthInfo } from "./auth";
import { GRANT_DELAY_MS, PRIZES_SQL, challengeRewardProgress, grantPrizes, prizeItem, prizesJson, suitJson } from "./challengeRewards";
import { pseudoVersionOf } from "./community";
import { boardCosmetics, withCosmetics, type BoardCosmetics } from "./cosmetics";
import { sha256Hex } from "./crypto";
import { ApiError, Errors, json, log, nowIso, readJsonObject, readLimited, type Ctx } from "./http";
import { avatarUrl, type LevelVersionRow, type RecordRow } from "./model";
import { notify } from "./notifications";
import { analyzeRace, detailOf, parseLayout, raceScore, type Claim, type RaceLayout } from "./race";
import { RULES, rateLimit } from "./ratelimit";
import { decide, type BoardContext, type Decision, type ReplayState } from "./records";
import { EventType, MAX_REPLAY_UPLOAD, PickupKind, ReplayError, analyzeReplay, inflateContainer, parseReplay, type Replay, type ReplayAnalysis } from "./replay";
import { COUNTRY_RE, HASH_RE, SEMVER_RE, UUID_RE, Validator, compareVersions, paging, queryEnum, queryString } from "./validate";

// ------------------------------------------------------------------ catalogue

/** A campaign level as the game's files describe it (scripts/challenge-catalog.mjs). */
export interface CatalogLevel {
    name: string;
    planet: string;
    /** Place in its planet (1 = first level). */
    order: number;
    /** The planet's last level (the « Épreuves » of each planet, the final level). */
    last: boolean;
    /** Revision (metadata.revision): the number the server registers for the level. */
    revision: number;
    /** Power slots of the level (LevelData::powerSlots). */
    slots: number;
    start_powers: string[];
    /** Every power the level gives (start, tablets, stations) or asks for (gates). */
    level_powers: string[];
    /** Time of the gold medal (Silver tier) and of the platinum medal (Gold tier). */
    silver_ms: number;
    gold_ms: number;
    /** May be drawn automatically (not EXTRÊME, no counted lives). */
    auto: boolean;
}

export interface ChallengeCatalog {
    version: number;
    player_max_hearts: number;
    /** Power id -> its input (two powers on one input may replace each other). */
    powers: Record<string, string>;
    levels: Record<string, CatalogLevel>;
}

export const CATALOG = catalogJson as ChallengeCatalog;

// ------------------------------------------------------------------ rules

/** Oldest game that plays challenges (format 4 replays, simulation 11). */
export const CHALLENGE_MIN_GAME_VERSION = "2.32.0";

export const KINDS = ["daily", "weekly", "monthly"] as const;
export type ChallengeKind = (typeof KINDS)[number];

/** Challenges exist from the current period to this many periods ahead (J+7, S+2, M+1). */
export const AHEAD: Record<ChallengeKind, number> = { daily: 7, weekly: 2, monthly: 1 };
export const DAYS_AHEAD = AHEAD.daily;
/** An automatic draw avoids the levels of the challenges of its kind this many periods around it. */
const RECENT: Record<ChallengeKind, number> = { daily: 10, weekly: 4, monthly: 2 };
/** Runs received later than this after the end of their challenge are « hors délai » (late). */
export const LATE_SUBMIT_MS = 10 * 60 * 1000;
/**
 * Powers an automatic draw may add: they only help moving or surviving, and one is added
 * only when its input is not used by any power of the level (never a power the level needs
 * replaced, whatever the replacement rule).
 */
export const SAFE_EXTRA_POWERS = ["dash", "glide", "triple_jump", "shield", "wave", "grapple"] as const;
const MAX_SLOTS = 8;

/**
 * Rules of an automatic draw per kind: the week's and the month's are more demanding (fewer
 * hearts, fewer extra powers). Stations always stay on: the level keeps its powers.
 */
export const RULE_PROFILES: Record<ChallengeKind, { hearts: readonly number[]; playerMax: boolean; extras: number }> = {
    daily: { hearts: [1, 2, 3, 5], playerMax: true, extras: 2 },
    weekly: { hearts: [1, 2, 3], playerMax: false, extras: 1 },
    monthly: { hearts: [1, 2, 3], playerMax: false, extras: 0 },
};

/**
 * Campaign levels an automatic draw may take: the day any drawable level, the week the
 * longer or harder ones (3rd and 4th level of a planet, its last level), the month the
 * hardest (the last level of each planet: the « Épreuves » and the final level). A level that
 * is not `auto` (EXTRÊME, counted lives: the boss rush) and a community level are only ever
 * chosen by the administrator.
 */
export function drawable(kind: ChallengeKind, level: CatalogLevel): boolean {
    if (!level.auto) return false;
    if (kind === "weekly") return level.order >= 3 || level.last;
    if (kind === "monthly") return level.last;
    return true;
}

export const TIERS = ["bronze", "silver", "gold"] as const;
export type Tier = (typeof TIERS)[number];

export interface ChallengeRules {
    hearts: number;
    slots: number;
    powers: string[];
    stations: boolean;
    checkpoints: boolean;
}

/**
 * Canonical form of the rules (rules_hash and the replay's challenge section): exactly these
 * keys in this order, powers sorted ascending, no whitespace. The game builds the same string
 * (tests/data/challenge_rules_fixture.json is checked on both sides).
 */
export function canonicalRules(challengeId: string, r: ChallengeRules): string {
    const powers = [...r.powers].sort();
    return (
        `{"challenge":${JSON.stringify(challengeId)},"hearts":${r.hearts},"slots":${r.slots},` +
        `"powers":${JSON.stringify(powers)},"stations":${r.stations},"checkpoints":${r.checkpoints}}`
    );
}

export function rulesHash(challengeId: string, r: ChallengeRules): Promise<string> {
    return sha256Hex(canonicalRules(challengeId, r));
}

// ------------------------------------------------------------------- periods

const DAY_MS = 86400000;
const DATE_RE = /^\d{4}-\d{2}-\d{2}$/;
const WEEK_RE = /^(\d{4})-W(\d{2})$/;
const MONTH_RE = /^(\d{4})-(\d{2})$/;
const CHALLENGE_ID_RE = /^(daily|weekly|monthly)-([0-9W-]{7,10})$/;

/** UTC day of an instant ('AAAA-MM-JJ'). */
export function utcDate(ms: number = Date.now()): string {
    return new Date(ms).toISOString().slice(0, 10);
}

const dayMs = (date: string) => Date.parse(`${date}T00:00:00Z`);

export function addDays(date: string, n: number): string {
    return utcDate(dayMs(date) + n * DAY_MS);
}

function isDate(s: string): boolean {
    return DATE_RE.test(s) && !Number.isNaN(dayMs(s)) && utcDate(dayMs(s)) === s;
}

function addMonths(month: string, n: number): string {
    const [y, m] = month.split("-").map(Number) as [number, number];
    const i = y * 12 + (m - 1) + n;
    return `${String(Math.floor(i / 12)).padStart(4, "0")}-${String((i % 12) + 1).padStart(2, "0")}`;
}

/** ISO 8601 week of a UTC day ('AAAA-Www'): a week (Monday to Sunday) belongs to the year of its Thursday. */
export function isoWeek(date: string): string {
    const t = dayMs(date);
    const thursday = new Date(t - ((new Date(t).getUTCDay() + 6) % 7) * DAY_MS + 3 * DAY_MS);
    const year = thursday.getUTCFullYear();
    const week = Math.floor((thursday.getTime() - Date.UTC(year, 0, 1)) / (7 * DAY_MS)) + 1;
    return `${year}-W${String(week).padStart(2, "0")}`;
}

/** A challenge's period: its kind, its name and its UTC days (`end` excluded). */
export interface Period {
    kind: ChallengeKind;
    period: string;
    start: string;
    end: string;
}

/** The period of `kind` that contains the UTC day `date`. */
export function periodOf(kind: ChallengeKind, date: string): string {
    return kind === "daily" ? date : kind === "weekly" ? isoWeek(date) : date.slice(0, 7);
}

/** The days of a period ('AAAA-MM-JJ', 'AAAA-Www', 'AAAA-MM'); null when malformed or nonexistent (W53 of a short year). */
export function periodBounds(kind: ChallengeKind, period: string): Period | null {
    if (kind === "daily") return isDate(period) ? { kind, period, start: period, end: addDays(period, 1) } : null;
    if (kind === "weekly") {
        const m = WEEK_RE.exec(period);
        const year = Number(m?.[1]);
        const week = Number(m?.[2]);
        if (!m || year < 2000 || year > 2999 || week < 1 || week > 53) return null;
        const jan4 = Date.UTC(year, 0, 4);
        const start = utcDate(jan4 - ((new Date(jan4).getUTCDay() + 6) % 7) * DAY_MS + (week - 1) * 7 * DAY_MS);
        return isoWeek(start) === period ? { kind, period, start, end: addDays(start, 7) } : null;
    }
    const m = MONTH_RE.exec(period);
    const year = Number(m?.[1]);
    const month = Number(m?.[2]);
    if (!m || year < 2000 || year > 2999 || month < 1 || month > 12) return null;
    return { kind, period, start: `${period}-01`, end: `${addMonths(period, 1)}-01` };
}

/** The period `n` periods after (before when negative) a valid `period` of `kind`. */
export function shiftPeriod(kind: ChallengeKind, period: string, n: number): string {
    if (kind === "daily") return addDays(period, n);
    if (kind === "monthly") return addMonths(period, n);
    return isoWeek(addDays(periodBounds(kind, period)!.start, 7 * n));
}

/** The period of a well-formed challenge id ('daily-…', 'weekly-…', 'monthly-…'), else null. */
export function parseChallengeId(id: string): Period | null {
    const m = CHALLENGE_ID_RE.exec(id);
    return m ? periodBounds(m[1] as ChallengeKind, m[2]!) : null;
}

export const challengeIdOf = (kind: ChallengeKind, period: string) => `${kind}-${period}`;
const instant = (date: string) => `${date}T00:00:00Z`;
export const kindOrder = (col: string) => `CASE ${col} WHEN 'monthly' THEN 0 WHEN 'weekly' THEN 1 ELSE 2 END`;

// ------------------------------------------------------------------ rows

export interface ChallengeRow {
    id: string;
    kind: ChallengeKind;
    period: string;
    /** First UTC day of the period. */
    date: string;
    /** First UTC day after the period. */
    end_date: string;
    source: "auto" | "admin";
    level_source: "campaign" | "community";
    level_id: string;
    level_version: number;
    level_hash: string;
    level_title: string;
    hearts: number;
    slots: number;
    powers: string;
    stations: number;
    checkpoints: number;
    rules_hash: string;
    silver_ms: number;
    gold_ms: number;
    gold_no_damage: number;
    featured_player_id: string | null;
    featured_notified_at: string | null;
    created_at: string;
    updated_at: string;
    /**
     * 2.41: what ranks its players: the times (« Vitesse », speed) or the progress (« Finir le
     * niveau », finish: like « Plus loin », its tiers a share of the route, `bronze_progress`
     * and `silver_progress` %, Or once finished).
     */
    objective: Objective;
    bronze_progress: number;
    silver_progress: number;
    /** The server's draw an administrator's choice replaced (its title), else null. */
    replaced_title: string | null;
    author_name: string | null;
    /** The level version has a race layout (« Plus loin »). */
    has_race: number;
    /** 2.43: its rewards (challengeRewards.ts, PRIZES_SQL), a JSON array. */
    rewards_json?: string | null;
}

export const OBJECTIVES = ["speed", "finish"] as const;
export type Objective = (typeof OBJECTIVES)[number];

interface RunRow {
    id: string;
    challenge_id: string;
    player_id: string;
    attempt_id: string;
    time_ms: number;
    deaths: number;
    damage_taken: number;
    level_hash: string;
    rules_hash: string;
    game_version: string;
    tier: Tier;
    status: "PENDING" | "VERIFIED" | "REJECTED";
    late: number;
    rejection_reason: string | null;
    verification_notes: string | null;
    replay_key: string | null;
    replay_status: "VERIFIED" | "REJECTED" | null;
    replay_sha256: string | null;
    replay_size: number | null;
    replay_format: number | null;
    replay_analysis: string | null;
    replay_at: string | null;
    created_at: string;
    verified_at: string | null;
}

export const CHALLENGE_SQL = `SELECT c.*, p.username AS author_name,
    CASE c.level_source
      WHEN 'campaign' THEN EXISTS (SELECT 1 FROM level_versions lv WHERE lv.level_id = c.level_id AND lv.content_hash = c.level_hash AND lv.race IS NOT NULL)
      ELSE EXISTS (SELECT 1 FROM community_level_versions cv
                   WHERE cv.level_id = c.level_id AND cv.version = c.level_version AND cv.level_hash = c.level_hash AND cv.race IS NOT NULL)
    END AS has_race,
    ${PRIZES_SQL} AS rewards_json
  FROM challenges c LEFT JOIN players p ON p.id = c.featured_player_id`;

function powersOf(c: Pick<ChallengeRow, "powers">): string[] {
    try {
        const v = JSON.parse(c.powers) as unknown;
        return Array.isArray(v) ? v.filter((p): p is string => typeof p === "string").sort() : [];
    } catch {
        return [];
    }
}

function rulesOf(c: ChallengeRow): ChallengeRules {
    return { hearts: c.hearts, slots: c.slots, powers: powersOf(c), stations: c.stations === 1, checkpoints: c.checkpoints === 1 };
}

/**
 * A challenge already begun (never a future one), as a friend's live run of it is shown
 * (2.36, spectate.ts): its level and the canonical form of its rules, from the server.
 */
export async function challengeForSpectate(ctx: Ctx, id: string) {
    const c = await ctx.env.DB.prepare("SELECT * FROM challenges WHERE id = ?1 AND date <= ?2").bind(id, utcDate()).first<ChallengeRow>();
    if (!c) return null;
    return {
        kind: c.kind,
        level: { source: c.level_source, id: c.level_id, version: c.level_version, hash: c.level_hash, title: c.level_title },
        rules: canonicalRules(c.id, rulesOf(c)),
    };
}

/** The challenge as the game reads it (DAILY_CHALLENGES.md §1, CHALLENGES_PERIODS.md §1). */
export function challengeJson(c: ChallengeRow) {
    return {
        id: c.id,
        kind: c.kind,
        period: c.period,
        // The first day: the 2.32 games read it.
        date: c.date,
        starts_at: instant(c.date),
        ends_at: instant(c.end_date),
        source: c.source,
        level: {
            source: c.level_source,
            level_id: c.level_id,
            level_version: c.level_version,
            level_hash: c.level_hash,
            title: c.level_title,
            author: c.level_source === "community" && c.featured_player_id && c.author_name ? { id: c.featured_player_id, username: c.author_name } : null,
        },
        rules: rulesOf(c),
        rules_hash: c.rules_hash,
        // 2.41, « Finir le niveau »: Bronze and Argent are shares of the route (the times stay
        // for the older games: every finished run is within them).
        tiers:
            c.objective === "finish"
                ? {
                      silver_ms: c.silver_ms,
                      gold_ms: c.gold_ms,
                      gold_no_damage: c.gold_no_damage === 1,
                      bronze_progress: c.bronze_progress,
                      silver_progress: c.silver_progress,
                  }
                : { silver_ms: c.silver_ms, gold_ms: c.gold_ms, gold_no_damage: c.gold_no_damage === 1 },
        // 2.33: the board « Plus loin » exists (the level version has a race layout).
        progress_available: c.has_race === 1,
        // 2.41: "speed" (the best times) or "finish" (the progress board is the challenge's board).
        objective: c.objective ?? "speed",
        // 2.43: the rewards an administrator gave it, each with its condition (granted by the
        // server once the period is over, from the verified results only).
        rewards: prizesJson(c.rewards_json),
    };
}

/** Tier of a run by its time (« Vitesse »), computed by the server (never taken from the client). */
export function tierOf(c: Pick<ChallengeRow, "silver_ms" | "gold_ms" | "gold_no_damage">, timeMs: number, damage: number): Tier {
    if (timeMs <= c.gold_ms && (c.gold_no_damage !== 1 || damage === 0)) return "gold";
    if (timeMs <= c.silver_ms) return "silver";
    return "bronze";
}

/**
 * Tier of a finished run of a challenge: by its time (« Vitesse »), or (« Finir le niveau »)
 * Or for any finish, Argent when Or asks for no damage and the run took some (the route is
 * then entirely reached, above any Argent share).
 */
export function runTierOf(
    c: Pick<ChallengeRow, "objective" | "silver_ms" | "gold_ms" | "gold_no_damage">,
    timeMs: number,
    damage: number,
): Tier {
    if (c.objective === "finish") return c.gold_no_damage !== 1 || damage === 0 ? "gold" : "silver";
    return tierOf(c, timeMs, damage);
}

/**
 * « Finir le niveau »: the tier of an unfinished attempt from the progress the server computed
 * from its replay (never the game's): Argent from `silver_progress` %, Bronze from
 * `bronze_progress` %, else none. Never Or (only a finish reaches it); none on a « Vitesse »
 * challenge.
 */
export function progressTierOf(
    c: Pick<ChallengeRow, "objective" | "bronze_progress" | "silver_progress">,
    progress: number | null,
): "bronze" | "silver" | null {
    if (c.objective !== "finish" || progress === null) return null;
    if (progress >= c.silver_progress) return "silver";
    if (progress >= c.bronze_progress) return "bronze";
    return null;
}

function runJson(r: RunRow) {
    return { id: r.id, status: r.status, tier: r.tier, time_ms: r.time_ms, late: r.late === 1 };
}

// ------------------------------------------------------------------ draw

/** Uniform integer in [0, n) from crypto.getRandomValues (no modulo bias). */
function randomInt(n: number): number {
    const buf = new Uint32Array(1);
    const limit = Math.floor(0x100000000 / n) * n;
    for (;;) {
        crypto.getRandomValues(buf);
        if (buf[0]! < limit) return buf[0]! % n;
    }
}

function shuffled<T>(a: readonly T[]): T[] {
    const out = [...a];
    for (let i = out.length - 1; i > 0; --i) {
        const j = randomInt(i + 1);
        [out[i], out[j]] = [out[j]!, out[i]!];
    }
    return out;
}

/**
 * Safe rules for a campaign level: stations kept (the level keeps its powers, so it stays
 * finishable), the level's start powers plus up to the kind's number of extra powers of the
 * safe list whose input no power of the level uses, at least the level's slots plus one per
 * extra power, hearts among the kind's choices (the day: 1, 2, 3, 5 or the player's maximum;
 * the week and the month: 1, 2 or 3), checkpoints or not.
 */
export function drawRules(level: CatalogLevel, catalog: ChallengeCatalog = CATALOG, kind: ChallengeKind = "daily"): ChallengeRules {
    const profile = RULE_PROFILES[kind];
    const inputs = new Set(level.level_powers.map((p) => catalog.powers[p] ?? p));
    const extras: string[] = [];
    const wanted = randomInt(profile.extras + 1);
    for (const p of shuffled(SAFE_EXTRA_POWERS)) {
        if (extras.length >= wanted) break;
        const input = catalog.powers[p];
        if (input === undefined || level.level_powers.includes(p) || inputs.has(input)) continue;
        extras.push(p);
        inputs.add(input);
    }
    const powers = [...new Set([...level.start_powers, ...extras])].sort();
    const slots = Math.min(MAX_SLOTS, Math.max(level.slots + extras.length, powers.length));
    const hearts = [...new Set([...profile.hearts, ...(profile.playerMax ? [catalog.player_max_hearts] : [])])];
    return { hearts: hearts[randomInt(hearts.length)]!, slots, powers, stations: true, checkpoints: randomInt(2) === 1 };
}

interface CandidateRow {
    id: string;
    name: string;
    current_version: number;
    content_hash: string;
}

/** Ranked campaign levels whose active revision is the one the catalogue describes. */
async function drawCandidates(ctx: Ctx): Promise<CandidateRow[]> {
    const rows = await ctx.env.DB.prepare(
        `SELECT l.id, l.name, l.current_version, l.content_hash FROM levels l
         JOIN level_versions v ON v.level_id = l.id AND v.content_hash = l.content_hash
         WHERE l.ranking_enabled = 1 AND v.compatibility_status = 'active' ORDER BY l.id`,
    ).all<CandidateRow>();
    // The catalogue must describe the very revision registered on the server (its medals,
    // slots and powers), otherwise the level is left out until the catalogue is regenerated.
    return rows.results.filter((l) => {
        const c = CATALOG.levels[l.id];
        return c !== undefined && c.revision === l.current_version;
    });
}

/**
 * Draws the challenge of a period (campaign levels of the kind's pool only, never a
 * community level) and inserts it unless one exists already (concurrent draws: the first one
 * wins). Returns false when no level can be drawn.
 */
async function drawChallenge(ctx: Ctx, p: Period, all: CandidateRow[]): Promise<boolean> {
    const db = ctx.env.DB;
    const candidates = all.filter((l) => drawable(p.kind, CATALOG.levels[l.id]!));
    if (candidates.length === 0) {
        log("warn", "challenge_draw_no_level", { kind: p.kind, period: p.period });
        return false;
    }
    const id = challengeIdOf(p.kind, p.period);
    // Not a level of the challenges of this kind around this period: among the candidates,
    // those used the longest ago (never used within the window first).
    const near = await db
        .prepare("SELECT level_id, date FROM challenges WHERE kind = ?1 AND level_source = 'campaign' AND id <> ?2 AND date BETWEEN ?3 AND ?4")
        .bind(
            p.kind,
            id,
            periodBounds(p.kind, shiftPeriod(p.kind, p.period, -RECENT[p.kind]))!.start,
            periodBounds(p.kind, shiftPeriod(p.kind, p.period, RECENT[p.kind]))!.start,
        )
        .all<{ level_id: string; date: string }>();
    const day = dayMs(p.start);
    const distance = (lid: string) =>
        near.results.filter((n) => n.level_id === lid).reduce((m, n) => Math.min(m, Math.abs(dayMs(n.date) - day)), Infinity);
    const far = Math.max(...candidates.map((l) => distance(l.id)));
    const pool = candidates.filter((l) => distance(l.id) === far);
    const level = pool[randomInt(pool.length)]!;
    const info = CATALOG.levels[level.id]!;
    const rules = drawRules(info, CATALOG, p.kind);
    const now = nowIso();
    const res = await db
        .prepare(
            `INSERT INTO challenges (id, kind, period, date, end_date, source, level_source, level_id, level_version, level_hash, level_title,
               hearts, slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, featured_player_id,
               featured_notified_at, created_at, updated_at)
             VALUES (?1, ?2, ?3, ?4, ?5, 'auto', 'campaign', ?6, ?7, ?8, ?9, ?10, ?11, ?12, 1, ?13, ?14, ?15, ?16, 1, NULL, NULL, ?17, ?17)
             ON CONFLICT DO NOTHING`,
        )
        .bind(
            id,
            p.kind,
            p.period,
            p.start,
            p.end,
            level.id,
            level.current_version,
            level.content_hash,
            level.name,
            rules.hearts,
            rules.slots,
            JSON.stringify(rules.powers),
            rules.checkpoints ? 1 : 0,
            await rulesHash(id, rules),
            info.silver_ms,
            Math.min(info.gold_ms, info.silver_ms),
            now,
        )
        .run();
    if (res.meta.changes > 0) log("info", "challenge_drawn", { id, level_id: level.id });
    return true;
}

/**
 * A challenge that has begun features a community level: its author is told, once (the claim
 * on featured_notified_at makes concurrent calls send a single notification), with the
 * sentence of its kind (« … est le défi du jour / de la semaine / du mois ! »).
 */
async function notifyFeatured(ctx: Ctx, today: string): Promise<void> {
    const rows = await ctx.env.DB.prepare(
        `SELECT id, kind, period, date, level_id, level_title, featured_player_id FROM challenges
         WHERE date <= ?1 AND end_date > ?1 AND level_source = 'community' AND featured_player_id IS NOT NULL AND featured_notified_at IS NULL`,
    )
        .bind(today)
        .all<{ id: string; kind: ChallengeKind; period: string; date: string; level_id: string; level_title: string; featured_player_id: string }>();
    for (const c of rows.results) {
        const claim = await ctx.env.DB.prepare("UPDATE challenges SET featured_notified_at = ?2 WHERE id = ?1 AND featured_notified_at IS NULL")
            .bind(c.id, nowIso())
            .run();
        if (claim.meta.changes !== 1) continue;
        await notify(ctx, c.featured_player_id, "challenge_featured", {
            levelId: c.level_id,
            data: { title: c.level_title, challenge_id: c.id, challenge_kind: c.kind, period: c.period, date: c.date, level_id: c.level_id },
        });
    }
}

/** The periods that must exist now: of each kind, the current one to AHEAD[kind] ahead. */
function wantedPeriods(today: string): Period[][] {
    return KINDS.map((kind) => {
        const current = periodOf(kind, today);
        return Array.from({ length: AHEAD[kind] + 1 }, (_, i) => periodBounds(kind, shiftPeriod(kind, current, i))!);
    });
}

/**
 * The challenges of the current periods and of the coming ones exist (missing ones are
 * drawn) and the creator of a community level that has just begun is told. Called by the
 * reads of the current challenges and by the daily maintenance; an existing challenge is
 * never touched (ON CONFLICT DO NOTHING).
 */
export async function ensureChallenges(ctx: Ctx): Promise<void> {
    const today = utcDate();
    const wanted = wantedPeriods(today);
    const ids = wanted.flat().map((p) => challengeIdOf(p.kind, p.period));
    const have = await ctx.env.DB.prepare(`SELECT id FROM challenges WHERE id IN (${ids.map((_, i) => `?${i + 1}`).join(", ")})`)
        .bind(...ids)
        .all<{ id: string }>();
    const existing = new Set(have.results.map((r) => r.id));
    let candidates: CandidateRow[] | null = null;
    for (const periods of wanted)
        for (const p of periods) {
            if (existing.has(challengeIdOf(p.kind, p.period))) continue;
            candidates ??= await drawCandidates(ctx);
            // No level for this kind: the next periods of the kind cannot be drawn either.
            if (!(await drawChallenge(ctx, p, candidates))) break;
        }
    await notifyFeatured(ctx, today);
}

// -------------------------------------------------------------- read routes

/** A challenge that has begun; a future or unknown one is « not found » (never revealed). */
async function startedChallenge(ctx: Ctx, id: string): Promise<ChallengeRow> {
    const p = parseChallengeId(id);
    if (!p || p.start > utcDate()) throw Errors.notFound("CHALLENGE_NOT_FOUND", "Défi introuvable.");
    const c = await ctx.env.DB.prepare(`${CHALLENGE_SQL} WHERE c.id = ?1`).bind(id).first<ChallengeRow>();
    if (!c) throw Errors.notFound("CHALLENGE_NOT_FOUND", "Défi introuvable.");
    return c;
}

async function ensureQuietly(ctx: Ctx): Promise<void> {
    try {
        await ensureChallenges(ctx);
    } catch (e) {
        // The read still answers with what exists.
        log("error", "challenge_ensure_failed", { request_id: ctx.requestId, error: e instanceof Error ? e.message : String(e) });
    }
}

async function challengesById(ctx: Ctx, ids: string[]): Promise<Map<string, ChallengeRow>> {
    const rows = await ctx.env.DB.prepare(`${CHALLENGE_SQL} WHERE c.id IN (${ids.map((_, i) => `?${i + 1}`).join(", ")})`)
        .bind(...ids)
        .all<ChallengeRow>();
    return new Map(rows.results.map((c) => [c.id, c]));
}

/** GET /challenges/daily/current (2.32): { server_time, challenge, previous } (today's and yesterday's daily challenges). */
export async function current(ctx: Ctx): Promise<Response> {
    await ensureQuietly(ctx);
    const now = Date.now();
    const today = utcDate(now);
    const ids = [challengeIdOf("daily", today), challengeIdOf("daily", addDays(today, -1))];
    const rows = await challengesById(ctx, ids);
    const of = (id: string) => {
        const c = rows.get(id);
        return c ? challengeJson(c) : null;
    };
    return json(ctx, { server_time: new Date(now).toISOString(), challenge: of(ids[0]!), previous: of(ids[1]!) });
}

/**
 * GET /challenges/current (2.33): `{ server_time, daily, weekly, monthly, previous: { daily,
 * weekly, monthly } }`: the challenge of each kind's current period and of the period before
 * (`null` when there is none).
 */
export async function currentAll(ctx: Ctx): Promise<Response> {
    await ensureQuietly(ctx);
    const now = Date.now();
    const today = utcDate(now);
    const cur = Object.fromEntries(KINDS.map((k) => [k, challengeIdOf(k, periodOf(k, today))])) as Record<ChallengeKind, string>;
    const prev = Object.fromEntries(KINDS.map((k) => [k, challengeIdOf(k, shiftPeriod(k, periodOf(k, today), -1))])) as Record<ChallengeKind, string>;
    const rows = await challengesById(ctx, [...Object.values(cur), ...Object.values(prev)]);
    const of = (id: string) => {
        const c = rows.get(id);
        return c ? challengeJson(c) : null;
    };
    return json(ctx, {
        server_time: new Date(now).toISOString(),
        daily: of(cur.daily),
        weekly: of(cur.weekly),
        monthly: of(cur.monthly),
        previous: { daily: of(prev.daily), weekly: of(prev.weekly), monthly: of(prev.monthly) },
    });
}

/** GET /challenges/daily?before=AAAA-MM-JJ&limit=N (2.32): daily challenges that have begun, most recent first. */
export async function archive(ctx: Ctx): Promise<Response> {
    const before = queryString(ctx.url, "before", DATE_RE);
    if (before !== undefined && !isDate(before)) throw Errors.validation([{ field: "before", message: "date AAAA-MM-JJ attendue" }]);
    const { limit } = paging(ctx.url, 30, 30);
    const today = utcDate();
    const rows = await ctx.env.DB.prepare(
        `${CHALLENGE_SQL} WHERE c.kind = 'daily' AND c.date <= ?1 AND (?2 IS NULL OR c.date < ?2) ORDER BY c.date DESC LIMIT ?3`,
    )
        .bind(today, before ?? null, limit)
        .all<ChallengeRow>();
    const last = rows.results[rows.results.length - 1];
    return json(ctx, {
        challenges: rows.results.map(challengeJson),
        next_before: rows.results.length === limit && last ? last.date : null,
    });
}

/** GET /challenges/{challengeId}: a challenge that has begun (the challenge object itself). */
export async function getChallenge(ctx: Ctx): Promise<Response> {
    return json(ctx, challengeJson(await startedChallenge(ctx, ctx.params.challengeId!)));
}

const MONTH_QUERY_RE = /^\d{4}-\d{2}$/;

/**
 * GET /challenges/calendar?month=AAAA-MM (default: the current month): `{ month, first_month,
 * challenges, me }`: every challenge that has begun and touches the month (its days, the
 * weeks that overlap it, the month itself), by first day (the month, then the week, then the
 * day for one same day); `first_month` = month of the oldest challenge (the current month
 * when there is none); `me` (signed in only, else null) = my result per challenge id.
 */
export async function calendar(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    await ensureQuietly(ctx);
    const today = utcDate();
    const month = queryString(ctx.url, "month", MONTH_QUERY_RE) ?? periodOf("monthly", today);
    const b = periodBounds("monthly", month);
    if (!b) throw Errors.validation([{ field: "month", message: "mois AAAA-MM attendu" }]);
    const rows = await ctx.env.DB.prepare(
        `${CHALLENGE_SQL} WHERE c.date <= ?1 AND c.date < ?3 AND c.end_date > ?2 ORDER BY c.date, ${kindOrder("c.kind")}`,
    )
        .bind(today, b.start, b.end)
        .all<ChallengeRow>();
    const first = await ctx.env.DB.prepare("SELECT MIN(date) AS d FROM challenges WHERE date <= ?1").bind(today).first<{ d: string | null }>();
    let me: Record<string, ReturnType<typeof resultView>> | null = null;
    if (viewer) {
        me = {};
        for (const r of await playerResults(ctx, viewer.player.id, { span: b, limit: 100 })) me[r.challenge_id] = resultView(r);
    }
    return json(ctx, {
        month,
        first_month: first?.d ? first.d.slice(0, 7) : periodOf("monthly", today),
        challenges: rows.results.map(challengeJson),
        me,
    });
}

// ----------------------------------------------------------------- leaderboard

export const SCOPES = ["world", "country", "friends"] as const;
/** live: runs received during the challenge (+ 10 min); all: also the late ones (« Depuis »). */
export const BOARD_PERIODS = ["live", "all"] as const;
/** time: best times; progress: « Plus loin », best progress of each player (2.33). */
export const BOARDS = ["time", "progress"] as const;

interface BoardRow {
    run_id: string;
    player_id: string;
    time_ms: number;
    tier: Tier;
    created_at: string;
    has_replay: number;
    late: number;
    username: string;
    avatar: string;
    avatar_custom_key: string | null;
    country_code: string | null;
    location_visibility: string;
    rank: number;
    total: number;
}

interface ProgressRow {
    player_id: string;
    progress: number;
    finished: number;
    time_ms: number;
    late: number;
    created_at: string;
    /** challenge_attempts.id (an attempt) or challenge_runs.id (a finished run). */
    item_id: string;
    /** Its replay is kept (an attempt: the player's best; a run: a verified replay). */
    replay: number;
    /** « Finir le niveau » (2.41): the tier of that attempt or finished run (null: none). */
    tier: Tier | null;
    username: string;
    avatar: string;
    avatar_custom_key: string | null;
    country_code: string | null;
    location_visibility: string;
    profile_visibility: string;
    rank: number;
    total: number;
}

type ScopeFilter = { country?: string; friendsOf?: string };

/** The scope's conditions on a board's players (`b` the best rows, `p` the players), bound from ?3. */
function scopeWhere(filter: ScopeFilter): { where: string; extra: unknown[] } {
    const where: string[] = [];
    const extra: unknown[] = [];
    let n = 3;
    if (filter.country) {
        // Players hiding their location never appear in a country board.
        where.push(`p.country_code = ?${n++} AND p.location_visibility <> 'hidden'`);
        extra.push(filter.country);
    }
    if (filter.friendsOf) {
        where.push(`(b.player_id = ?${n} OR b.player_id IN (SELECT friend_id FROM player_friends WHERE player_id = ?${n} AND status = 'accepted'))`);
        n++;
        extra.push(filter.friendsOf);
    }
    return { where: where.map((w) => " AND " + w).join(""), extra };
}

/**
 * « Plus loin »: best VERIFIED progress of each active player — the attempts' scores, 100 for
 * a finished run —, highest first, ties by the earlier submission; ?1 = challenge, ?2 = 1 to
 * take the late ones too, filters from ?3.
 */
function progressSql(filter: ScopeFilter): { sql: string; extra: unknown[] } {
    const { where, extra } = scopeWhere(filter);
    const sql = `
      WITH items AS (
        SELECT a.player_id, a.progress, 0 AS finished, a.time_ms, a.late, a.created_at, a.id AS item_id, (a.replay_key IS NOT NULL) AS replay, a.tier
        FROM challenge_attempts a WHERE a.challenge_id = ?1 AND a.status = 'VERIFIED' AND (?2 = 1 OR a.late = 0)
        UNION ALL
        SELECT r.player_id, 100.0, 1, r.time_ms, r.late, r.created_at, r.id, (r.replay_key IS NOT NULL AND r.replay_status = 'VERIFIED'), r.tier
        FROM challenge_runs r WHERE r.challenge_id = ?1 AND r.status = 'VERIFIED' AND (?2 = 1 OR r.late = 0)),
      best AS (
        SELECT player_id, progress, finished, time_ms, late, created_at, item_id, replay, tier FROM (
          SELECT items.*, ROW_NUMBER() OVER (PARTITION BY player_id ORDER BY progress DESC, created_at, item_id) AS rn FROM items)
        WHERE rn = 1),
      ranked AS (
        SELECT b.*, p.username, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility, p.profile_visibility
        FROM best b JOIN players p ON p.id = b.player_id
        WHERE p.account_status = 'active'${where}),
      filtered AS (SELECT ranked.*, ROW_NUMBER() OVER (ORDER BY progress DESC, created_at, player_id) AS rank FROM ranked)
      SELECT *, COUNT(*) OVER () AS total FROM filtered`;
    return { sql, extra };
}

/**
 * Best VERIFIED run of each active player on a challenge; ?1 = challenge, ?2 = 1 to take the
 * late runs too (period=all), filters from ?3.
 */
function boardSql(filter: ScopeFilter): { sql: string; extra: unknown[] } {
    const { where, extra } = scopeWhere(filter);
    const sql = `
      WITH best AS (
        SELECT run_id, player_id, time_ms, tier, created_at, has_replay, late FROM (
          SELECT r.id AS run_id, r.player_id, r.time_ms, r.tier, r.created_at, r.late,
                 (r.replay_key IS NOT NULL AND r.replay_status = 'VERIFIED') AS has_replay,
                 ROW_NUMBER() OVER (PARTITION BY r.player_id ORDER BY r.time_ms, r.created_at, r.id) AS rn
          FROM challenge_runs r WHERE r.challenge_id = ?1 AND r.status = 'VERIFIED' AND (?2 = 1 OR r.late = 0)) WHERE rn = 1),
      ranked AS (
        SELECT b.*, p.username, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility
        FROM best b JOIN players p ON p.id = b.player_id
        WHERE p.account_status = 'active'${where}),
      filtered AS (SELECT ranked.*, ROW_NUMBER() OVER (ORDER BY time_ms, created_at, player_id) AS rank FROM ranked)
      SELECT *, COUNT(*) OVER () AS total FROM filtered`;
    return { sql, extra };
}

function playerJson(
    p: { id: string; username: string; avatar: string; avatar_custom_key: string | null; country_code: string | null; location_visibility: string },
    looks: Map<string, BoardCosmetics>,
) {
    return withCosmetics(
        {
            id: p.id,
            username: p.username,
            avatar: p.avatar,
            avatar_url: avatarUrl(p),
            country_code: p.location_visibility === "hidden" ? null : p.country_code,
        },
        looks,
    );
}

const PROGRESS_UNAVAILABLE = () =>
    Errors.conflict("PROGRESS_UNAVAILABLE", "Pas de classement « Plus loin » pour ce défi : son niveau n'a pas de tracé.");

/**
 * GET /challenges/{id}/leaderboard?scope=world|country|friends&country=&period=live|all&board=time|progress&limit=50:
 * `{ challenge_id, scope, period, board, country_code, entries, me, total }`. board=time:
 * entries `{ rank, run_id, player, time_ms, tier, status, has_replay, is_me, late }`, `me` is
 * the viewer's best run: VERIFIED (with its rank in this scope) or, when better, PENDING (rank
 * null). board=progress (« Plus loin »): entries `{ rank, player, progress, finished, time_ms,
 * status, late, is_me, attempt_id, run_id, has_replay }`, `me` the viewer's best VERIFIED
 * progress.
 */
export async function leaderboard(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const c = await startedChallenge(ctx, ctx.params.challengeId!);
    // 2.43: the final board of an ended challenge is read: its rewards are granted if due.
    if (c.end_date <= utcDate(Date.now() - GRANT_DELAY_MS)) await grantQuietly(ctx, { onlyId: c.id, limit: 1 });
    const scope = queryEnum(ctx.url, "scope", SCOPES, "world");
    const period = queryEnum(ctx.url, "period", BOARD_PERIODS, "live");
    // board=time stays the default, also on a « Finir le niveau » challenge (2.41): the older
    // games read it as their only board; the 2.41 game asks for board=progress, its main board.
    const board = queryEnum(ctx.url, "board", BOARDS, "time");
    const page = paging(ctx.url, 50, 100);
    let country = queryString(ctx.url, "country", COUNTRY_RE);
    const filter: ScopeFilter = {};
    if (scope === "country") {
        country ??= viewer?.player.country_code ?? undefined;
        if (!country) throw Errors.validation([{ field: "country", message: "code pays ISO à 2 lettres attendu" }]);
        filter.country = country;
    } else if (scope === "friends") {
        if (!viewer) throw Errors.unauthorized();
        filter.friendsOf = viewer.player.id;
    }
    const all = period === "all" ? 1 : 0;
    if (board === "progress") {
        if (c.has_race !== 1) throw PROGRESS_UNAVAILABLE();
        return progressBoard(ctx, c, viewer, { scope, period, filter, all, page });
    }
    const { sql, extra } = boardSql(filter);
    const n = 3 + extra.length;
    const rows = await ctx.env.DB.prepare(`${sql} ORDER BY rank LIMIT ?${n} OFFSET ?${n + 1}`)
        .bind(c.id, all, ...extra, page.limit, page.offset)
        .all<BoardRow>();
    let total = rows.results[0]?.total ?? 0;
    if (rows.results.length === 0 && page.offset > 0) {
        const t = await ctx.env.DB.prepare(`SELECT COUNT(*) AS total FROM (${sql})`).bind(c.id, all, ...extra).first<{ total: number }>();
        total = t?.total ?? 0;
    }
    type Best = { id: string; time_ms: number; tier: Tier; late: number };
    let mine: BoardRow | null = null;
    let pending: Best | null = null;
    let verified: Best | null = null;
    if (viewer) {
        mine = await ctx.env.DB.prepare(`${sql} WHERE player_id = ?${n}`).bind(c.id, all, ...extra, viewer.player.id).first<BoardRow>();
        const best = (status: string) =>
            ctx.env.DB.prepare(
                `SELECT id, time_ms, tier, late FROM challenge_runs WHERE challenge_id = ?1 AND player_id = ?2 AND status = ?3 AND (?4 = 1 OR late = 0)
                 ORDER BY time_ms, created_at, id LIMIT 1`,
            )
                .bind(c.id, viewer.player.id, status, all)
                .first<Best>();
        pending = await best("PENDING");
        verified = mine ? { id: mine.run_id, time_ms: mine.time_ms, tier: mine.tier, late: mine.late } : await best("VERIFIED");
    }
    const looks = await boardCosmetics(ctx, [...rows.results.map((r) => r.player_id), ...(viewer ? [viewer.player.id] : [])]);
    const entry = (r: BoardRow) => ({
        rank: r.rank,
        run_id: r.run_id,
        player: playerJson({ ...r, id: r.player_id }, looks),
        time_ms: r.time_ms,
        tier: r.tier,
        status: "VERIFIED" as const,
        has_replay: r.has_replay === 1,
        is_me: viewer?.player.id === r.player_id,
        late: r.late === 1,
    });
    let me = null;
    if (viewer) {
        const who = playerJson(viewer.player, looks);
        const unranked = (b: Best, status: "PENDING" | "VERIFIED") => ({
            rank: null,
            run_id: b.id,
            player: who,
            time_ms: b.time_ms,
            tier: b.tier,
            status,
            late: b.late === 1,
        });
        if (pending && (!verified || pending.time_ms < verified.time_ms)) me = unranked(pending, "PENDING");
        else if (mine) me = entry(mine);
        else if (verified) me = unranked(verified, "VERIFIED");
    }
    return json(ctx, {
        challenge_id: c.id,
        scope,
        period,
        board,
        country_code: filter.country ?? null,
        entries: rows.results.map(entry),
        me,
        total,
    });
}

/** board=progress of the leaderboard (« Plus loin », 2.33). */
async function progressBoard(
    ctx: Ctx,
    c: ChallengeRow,
    viewer: AuthInfo | null,
    o: { scope: string; period: string; filter: ScopeFilter; all: number; page: { limit: number; offset: number } },
): Promise<Response> {
    const { sql, extra } = progressSql(o.filter);
    const n = 3 + extra.length;
    const rows = await ctx.env.DB.prepare(`${sql} ORDER BY rank LIMIT ?${n} OFFSET ?${n + 1}`)
        .bind(c.id, o.all, ...extra, o.page.limit, o.page.offset)
        .all<ProgressRow>();
    let total = rows.results[0]?.total ?? 0;
    if (rows.results.length === 0 && o.page.offset > 0) {
        const t = await ctx.env.DB.prepare(`SELECT COUNT(*) AS total FROM (${sql})`).bind(c.id, o.all, ...extra).first<{ total: number }>();
        total = t?.total ?? 0;
    }
    let mine: ProgressRow | null = null;
    let outside: ProgressRow | null = null;
    if (viewer) {
        mine = await ctx.env.DB.prepare(`${sql} WHERE player_id = ?${n}`).bind(c.id, o.all, ...extra, viewer.player.id).first<ProgressRow>();
        // Not in this scope (a country, friends): my best of the period, without a rank.
        if (!mine) outside = await ctx.env.DB.prepare(`${progressSql({}).sql} WHERE player_id = ?3`).bind(c.id, o.all, viewer.player.id).first<ProgressRow>();
    }
    const looks = await boardCosmetics(ctx, [...rows.results.map((r) => r.player_id), ...(viewer ? [viewer.player.id] : [])]);
    const entry = (r: ProgressRow) => ({
        rank: r.rank,
        player: playerJson({ ...r, id: r.player_id }, looks),
        progress: r.progress,
        finished: r.finished === 1,
        time_ms: r.time_ms,
        status: "VERIFIED" as const,
        late: r.late === 1,
        is_me: viewer?.player.id === r.player_id,
        // Its replay: GET /challenges/attempts/{attempt_id}/replay (an attempt) or
        // GET /challenges/runs/{run_id}/replay (a finished run); never for a private profile but mine.
        attempt_id: r.finished === 1 ? null : r.item_id,
        run_id: r.finished === 1 ? r.item_id : null,
        has_replay: r.replay === 1 && (r.profile_visibility !== "private" || viewer?.player.id === r.player_id),
        // « Finir le niveau » (2.41): the tier this progress reached, computed by the server.
        ...(c.objective === "finish" ? { tier: r.tier ?? null } : {}),
    });
    const me = mine ? entry(mine) : outside ? { ...entry(outside), rank: null } : null;
    return json(ctx, {
        challenge_id: c.id,
        scope: o.scope,
        period: o.period,
        board: "progress",
        objective: c.objective,
        country_code: o.filter.country ?? null,
        entries: rows.results.map(entry),
        me,
        total,
    });
}

// ----------------------------------------------------------------- decisions

/**
 * Position this time would take among the other players' verified bests of the challenge:
 * the board of the challenge's period for a run in time, the board « Depuis » for a late one.
 */
async function boardContext(ctx: Ctx, challengeId: string, timeMs: number, playerId: string, late: boolean): Promise<BoardContext> {
    const row = await ctx.env.DB.prepare(
        `SELECT SUM(CASE WHEN b.t <= ?2 THEN 1 ELSE 0 END) AS better, COUNT(*) AS size, MIN(b.t) AS wr FROM (
           SELECT r.player_id, MIN(r.time_ms) AS t FROM challenge_runs r JOIN players p ON p.id = r.player_id
           WHERE r.challenge_id = ?1 AND r.status = 'VERIFIED' AND r.player_id <> ?3 AND p.account_status = 'active' AND (?4 = 1 OR r.late = 0)
           GROUP BY r.player_id) b`,
    )
        .bind(challengeId, timeMs, playerId, late ? 1 : 0)
        .first<{ better: number | null; size: number; wr: number | null }>();
    return { rank: (row?.better ?? 0) + 1, size: row?.size ?? 0, worldRecordMs: row?.wr ?? null };
}

/**
 * The decision rules of the records, as for a category that needs its replay: a run is only
 * VERIFIED once the server has checked its replay; the first place of an established board
 * by a recent account waits for a manual review.
 */
function decideRun(trust: number, replay: ReplayState, timeMs: number, board: BoardContext): Decision {
    return decide(trust, replay, timeMs, board, { needsReplay: true });
}

async function applyDecision(ctx: Ctx, r: RunRow, d: Decision): Promise<void> {
    const at = d.status === "VERIFIED" ? nowIso() : null;
    await ctx.env.DB.prepare("UPDATE challenge_runs SET status = ?2, rejection_reason = ?3, verification_notes = ?4, verified_at = ?5 WHERE id = ?1")
        .bind(r.id, d.status, d.reason, d.note, at)
        .run();
    r.status = d.status;
    r.rejection_reason = d.reason;
    r.verification_notes = d.note;
    r.verified_at = at;
}

/**
 * Runs held PENDING although their replay was verified (a first place waiting for review)
 * are decided again with the current board and trust (another player may have gone ahead
 * since). Manual decisions are never undone. Returns the number of runs that changed.
 */
export async function recheckHeldRuns(ctx: Ctx, playerId: string | null, limit = 200): Promise<number> {
    const held = await ctx.env.DB.prepare(
        `SELECT r.*, p.trust_level FROM challenge_runs r JOIN players p ON p.id = r.player_id
         WHERE r.status = 'PENDING' AND r.replay_status = 'VERIFIED' AND (?1 IS NULL OR r.player_id = ?1) ORDER BY r.created_at LIMIT ?2`,
    )
        .bind(playerId, limit)
        .all<RunRow & { trust_level: number }>();
    let changed = 0;
    for (const r of held.results) {
        const board = await boardContext(ctx, r.challenge_id, r.time_ms, r.player_id, r.late === 1);
        const d = decideRun(r.trust_level, "valid", r.time_ms, board);
        if (d.status === "PENDING" && d.note === r.verification_notes) continue;
        await applyDecision(ctx, r, d);
        if (d.status !== "PENDING") ++changed;
    }
    return changed;
}

// ------------------------------------------------------------------ submission

/**
 * The level revision a challenge is played on (campaign) or its community version's geometry,
 * with the race layout of that version (« Plus loin »; null without one).
 */
async function levelVersionOf(ctx: Ctx, c: ChallengeRow): Promise<{ level: LevelVersionRow; layout: RaceLayout | null }> {
    const layoutOf = (race: unknown) => {
        if (typeof race !== "string") return null;
        try {
            return parseLayout(JSON.parse(race));
        } catch {
            return null;
        }
    };
    if (c.level_source === "campaign") {
        const v = await ctx.env.DB.prepare("SELECT * FROM level_versions WHERE level_id = ?1 AND content_hash = ?2")
            .bind(c.level_id, c.level_hash)
            .first<LevelVersionRow & { race: string | null }>();
        if (v) return { level: v, layout: layoutOf(v.race) };
    } else {
        const v = await ctx.env.DB.prepare("SELECT * FROM community_level_versions WHERE level_id = ?1 AND version = ?2")
            .bind(c.level_id, c.level_version)
            .first<Record<string, unknown>>();
        if (v && v.level_hash === c.level_hash) return { level: pseudoVersionOf(v), layout: layoutOf(v.race) };
    }
    throw Errors.notFound("LEVEL_VERSION_NOT_FOUND", "Le niveau de ce défi n'est plus disponible.");
}

async function levelOf(ctx: Ctx, c: ChallengeRow): Promise<LevelVersionRow> {
    return (await levelVersionOf(ctx, c)).level;
}

/** Whether a submission received at `now` comes after the end of its challenge (+ 10 min). */
function isLate(c: Pick<ChallengeRow, "end_date">, now: number): boolean {
    return now > Date.parse(instant(c.end_date)) + LATE_SUBMIT_MS;
}

/** POST /challenges/{challengeId}/runs: a run of the challenge; idempotent by attempt_id. */
export async function submitRun(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.challengeSubmit, "player:" + auth.player.id);
    const id = ctx.params.challengeId!;
    const period = parseChallengeId(id);
    if (!period) throw Errors.notFound("CHALLENGE_NOT_FOUND", "Défi introuvable.");
    const body = await readJsonObject(ctx, 4 * 1024);
    // `player_id` is deliberately not accepted: the player comes from the session.
    const v = new Validator(body, ["attempt_id", "time_ms", "deaths", "damage_taken", "level_hash", "rules_hash", "game_version", "has_replay"]);
    const attemptId = v.str("attempt_id", { min: 36, max: 36, pattern: UUID_RE });
    const timeMs = v.int("time_ms", { min: 1, max: 24 * 3600 * 1000 });
    const deaths = v.int("deaths", { min: 0, max: 9999 });
    const damage = v.int("damage_taken", { min: 0, max: 1_000_000 });
    const levelHash = v.str("level_hash", { min: 64, max: 64, pattern: HASH_RE });
    const hash = v.str("rules_hash", { min: 64, max: 64, pattern: HASH_RE });
    const gameVersion = v.str("game_version", { max: 16, pattern: SEMVER_RE });
    const hasReplay = v.bool("has_replay", { optional: true }) ?? false;
    v.done();

    const now = Date.now();
    // A future challenge is never revealed: whether it exists or not, it has not begun.
    if (period.start > utcDate(now)) throw Errors.conflict("CHALLENGE_NOT_STARTED", "Ce défi n'a pas encore commencé.");
    const c = await ctx.env.DB.prepare(`${CHALLENGE_SQL} WHERE c.id = ?1`).bind(id).first<ChallengeRow>();
    if (!c) throw Errors.notFound("CHALLENGE_NOT_FOUND", "Défi introuvable.");

    // Idempotence: the same attempt can be sent again (lost response, offline queue).
    const existing = await ctx.env.DB.prepare("SELECT * FROM challenge_runs WHERE player_id = ?1 AND attempt_id = ?2")
        .bind(auth.player.id, attemptId)
        .first<RunRow>();
    if (existing) {
        if (
            existing.challenge_id !== c.id ||
            existing.time_ms !== timeMs ||
            existing.deaths !== deaths ||
            existing.damage_taken !== damage ||
            existing.level_hash !== levelHash ||
            existing.rules_hash !== hash
        )
            throw Errors.conflict("ATTEMPT_CONFLICT", "Cette tentative a déjà été soumise avec d'autres données.");
        return json(ctx, {
            duplicate: true,
            run: runJson(existing),
            replay: { required: existing.replay_key === null && existing.status === "PENDING", upload_path: `/api/v1/challenges/runs/${existing.id}/replay` },
        });
    }

    // After the end (+ 10 min) the run is still taken, « hors délai » (2.33: never
    // CHALLENGE_ENDED any more; a 2.32 game that sends one is answered the same way).
    const late = isLate(c, now);
    const level = await levelOf(ctx, c);
    const minGame = [CHALLENGE_MIN_GAME_VERSION, ctx.env.MIN_GAME_VERSION, level.min_game_version].reduce((a, b) => (compareVersions(a, b) >= 0 ? a : b));
    if (compareVersions(gameVersion!, minGame) < 0)
        throw new ApiError(426, "GAME_VERSION_UNSUPPORTED", "Version du jeu trop ancienne pour les défis.", { min_game_version: minGame });
    if (levelHash !== c.level_hash) throw Errors.conflict("LEVEL_MISMATCH", "Ce n'est pas la version du niveau de ce défi.", { level_hash: c.level_hash });
    if (hash !== c.rules_hash) throw Errors.conflict("RULES_MISMATCH", "Ces règles ne sont pas celles du défi.", { rules_hash: c.rules_hash });

    const run: RunRow = {
        id: crypto.randomUUID(),
        challenge_id: c.id,
        player_id: auth.player.id,
        attempt_id: attemptId!,
        time_ms: timeMs!,
        deaths: deaths!,
        damage_taken: damage!,
        level_hash: c.level_hash,
        rules_hash: c.rules_hash,
        game_version: gameVersion!,
        tier: runTierOf(c, timeMs!, damage!),
        status: "PENDING",
        late: late ? 1 : 0,
        rejection_reason: null,
        verification_notes: null,
        replay_key: null,
        replay_status: null,
        replay_sha256: null,
        replay_size: null,
        replay_format: null,
        replay_analysis: null,
        replay_at: null,
        created_at: nowIso(),
        verified_at: null,
    };
    try {
        await ctx.env.DB.prepare(
            `INSERT INTO challenge_runs (id, challenge_id, player_id, attempt_id, time_ms, deaths, damage_taken, level_hash, rules_hash,
               game_version, tier, status, late, created_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, 'PENDING', ?12, ?13)`,
        )
            .bind(run.id, run.challenge_id, run.player_id, run.attempt_id, run.time_ms, run.deaths, run.damage_taken, run.level_hash,
                run.rules_hash, run.game_version, run.tier, run.late, run.created_at)
            .run();
    } catch (e) {
        // Two identical submissions racing each other.
        if (String(e).includes("UNIQUE")) throw Errors.conflict("DUPLICATE_SUBMISSION", "Tentative déjà enregistrée.");
        throw e;
    }

    let decision: Decision;
    if (run.time_ms < level.min_time_ms) {
        decision = { status: "REJECTED", reason: "IMPOSSIBLE_TIME", note: null };
        await audit(ctx, auth.player.id, "challenge_run_rejected_impossible_time", { run_id: run.id, time_ms: run.time_ms });
    } else if (run.time_ms > level.time_limit_ms) decision = { status: "REJECTED", reason: "TIME_OVER_LIMIT", note: null };
    else decision = decideRun(auth.player.trust_level, "none", run.time_ms, await boardContext(ctx, c.id, run.time_ms, auth.player.id, late));
    await applyDecision(ctx, run, decision);
    return json(
        ctx,
        {
            duplicate: false,
            run: runJson(run),
            replay: { required: run.status === "PENDING", accepted: hasReplay, upload_path: `/api/v1/challenges/runs/${run.id}/replay` },
        },
        { status: 201 },
    );
}

// --------------------------------------------------------------------- replays

async function loadRun(ctx: Ctx, id: string): Promise<RunRow & { owner_status: string; owner_visibility: string; trust_level: number }> {
    if (!/^[0-9a-f-]{36}$/.test(id)) throw Errors.notFound("RUN_NOT_FOUND", "Partie introuvable.");
    const r = await ctx.env.DB.prepare(
        `SELECT r.*, p.account_status AS owner_status, p.profile_visibility AS owner_visibility, p.trust_level
         FROM challenge_runs r JOIN players p ON p.id = r.player_id WHERE r.id = ?1`,
    )
        .bind(id)
        .first<RunRow & { owner_status: string; owner_visibility: string; trust_level: number }>();
    if (!r) throw Errors.notFound("RUN_NOT_FOUND", "Partie introuvable.");
    return r;
}

/** What the run declares, for the shared replay checks (collectibles do not count here). */
function recordView(rep: Replay, c: ChallengeRow, r: RunRow): RecordRow {
    let collectibles = 0;
    for (const e of rep.events) if (e.type === EventType.Pickup && (e.arg === PickupKind.Shard || e.arg === PickupKind.Relic)) ++collectibles;
    return {
        // A community level's package keeps its own internal id: its version hash identifies it.
        level_id: c.level_source === "campaign" ? c.level_id : rep.levelId,
        level_hash: c.level_hash,
        attempt_id: r.attempt_id,
        time_ms: r.time_ms,
        deaths: r.deaths,
        damage_taken: r.damage_taken,
        collectibles,
    } as RecordRow;
}

/** PUT /challenges/runs/{runId}/replay: the run's replay (CPRZ, format 4 with the challenge section). */
export async function uploadRunReplay(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.replayUpload, "player:" + auth.player.id);
    const r = await loadRun(ctx, ctx.params.runId!);
    if (r.player_id !== auth.player.id) throw Errors.forbidden("NOT_OWNER", "Cette partie ne vous appartient pas.");
    if (r.replay_key) throw Errors.conflict("REPLAY_EXISTS", "Un replay est déjà associé à cette partie.");
    if (r.status === "REJECTED") throw Errors.conflict("RUN_REJECTED", "Cette partie a été refusée.");
    const type = (ctx.request.headers.get("content-type") ?? "").toLowerCase();
    if (!type.startsWith("application/octet-stream"))
        throw new ApiError(415, "UNSUPPORTED_MEDIA_TYPE", "Le replay doit être envoyé en application/octet-stream.");
    const data = await readLimited(ctx.request, MAX_REPLAY_UPLOAD);
    const c = await ctx.env.DB.prepare(`${CHALLENGE_SQL} WHERE c.id = ?1`).bind(r.challenge_id).first<ChallengeRow>();
    if (!c) throw Errors.notFound("CHALLENGE_NOT_FOUND", "Défi introuvable.");
    const level = await levelOf(ctx, c);

    let replay: Replay;
    try {
        replay = parseReplay(await inflateContainer(data));
    } catch (e) {
        if (!(e instanceof ReplayError)) throw e;
        // An unreadable file is a client error: nothing is stored, the client may retry.
        throw new ApiError(422, "REPLAY_MALFORMED", `Replay illisible : ${e.message}.`);
    }
    const analysis: ReplayAnalysis = analyzeReplay(replay, recordView(replay, c, r), level, {
        canonical: canonicalRules(c.id, rulesOf(c)),
        hearts: c.hearts,
    });

    // R2 is private: the object is only ever served through GET /challenges/runs/{id}/replay.
    // The key is unique per upload so that two racing uploads never overwrite each other.
    const key = `challenges/${c.id}/${r.player_id}/${r.id}-${crypto.randomUUID()}.cprz`;
    const digest = await sha256Hex(data);
    await ctx.env.STORAGE.put(key, data, {
        httpMetadata: { contentType: "application/octet-stream" },
        customMetadata: { challenge_run_id: r.id, sha256: digest, format: "CPRZ/CPRP" },
    });
    const replayStatus = analysis.ok ? "VERIFIED" : "REJECTED";
    const claim = await ctx.env.DB.prepare(
        `UPDATE challenge_runs SET replay_key = ?2, replay_status = ?3, replay_sha256 = ?4, replay_size = ?5, replay_format = ?6,
           replay_analysis = ?7, replay_at = ?8 WHERE id = ?1 AND replay_key IS NULL AND status <> 'REJECTED'`,
    )
        .bind(r.id, key, replayStatus, digest, data.byteLength, replay.formatVersion, JSON.stringify(analysis), nowIso())
        .run();
    if (claim.meta.changes !== 1) {
        await ctx.env.STORAGE.delete(key);
        throw Errors.conflict("REPLAY_EXISTS", "Un replay est déjà associé à cette partie.");
    }
    r.replay_key = key;
    r.replay_status = replayStatus;
    if (!analysis.ok) await audit(ctx, r.player_id, "challenge_replay_rejected", { run_id: r.id, reasons: analysis.reasons.slice(0, 5) });

    if (r.status === "PENDING") {
        const board = await boardContext(ctx, c.id, r.time_ms, r.player_id, r.late === 1);
        const d = decideRun(r.trust_level, analysis.ok ? "valid" : "invalid", r.time_ms, board);
        if (!analysis.ok) d.note = analysis.reasons.join(" ; ").slice(0, 500);
        await applyDecision(ctx, r, d);
    }
    return json(ctx, { run: runJson(r), replay: { verification_status: replayStatus }, analysis }, { status: 201 });
}

/** GET /challenges/runs/{runId}/replay: public for a VERIFIED run (ghost of the challenge's first), else its owner. */
export async function downloadRunReplay(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const r = await loadRun(ctx, ctx.params.runId!);
    const isOwner = viewer?.player.id === r.player_id;
    if (!isOwner && (r.status !== "VERIFIED" || r.owner_status !== "active")) throw Errors.notFound("RUN_NOT_FOUND", "Partie introuvable.");
    if (!isOwner && r.owner_visibility === "private") throw Errors.forbidden("PROFILE_PRIVATE", "Le joueur a masqué son profil.");
    if (!r.replay_key || (!isOwner && r.replay_status !== "VERIFIED")) throw Errors.notFound("REPLAY_NOT_FOUND", "Aucun replay pour cette partie.");
    const obj = await ctx.env.STORAGE.get(r.replay_key);
    if (!obj) throw Errors.notFound("REPLAY_NOT_FOUND", "Fichier de replay absent.");
    return new Response(obj.body, {
        headers: {
            "content-type": "application/octet-stream",
            "content-length": String(obj.size),
            "x-replay-sha256": r.replay_sha256 ?? "",
            "cache-control": "private, max-age=3600",
            "x-content-type-options": "nosniff",
        },
    });
}

// ------------------------------------------------------- attempts (« Plus loin », 2.33)

interface AttemptRow {
    id: string;
    challenge_id: string;
    player_id: string;
    attempt_id: string;
    time_ms: number;
    level_hash: string;
    rules_hash: string;
    game_version: string;
    claim: string;
    late: number;
    status: "PENDING" | "VERIFIED" | "REJECTED";
    progress: number | null;
    route: number | null;
    detail: string | null;
    reasons: string | null;
    replay_key: string | null;
    replay_sha256: string | null;
    replay_size: number | null;
    replay_at: string | null;
    created_at: string;
    verified_at: string | null;
}

/** The upload path of an attempt's replay: the game's attempt id (looked up among the session player's attempts). */
const attemptUploadPath = (attemptId: string) => `/api/v1/challenges/attempts/${attemptId}/replay`;

function attemptJson(a: AttemptRow) {
    return { attempt_id: a.attempt_id, status: a.status, progress: a.progress, finished: false, late: a.late === 1 };
}

/**
 * What the game declares of an attempt, `{ finished: false, bosses: [‰ removed per boss of the
 * layout], waves: [waves beaten per ambush] }`: only compared with what the replay shows, never
 * taken as the score. A finished run is sent as a run (POST …/runs), not as an attempt.
 */
function readClaim(v: Validator, raw: unknown): Claim {
    const claim: Claim = { finished: false, bosses: [], waves: [] };
    if (typeof raw !== "object" || raw === null || Array.isArray(raw)) {
        v.fail("claim", "objet { finished, bosses, waves } attendu");
        return claim;
    }
    const o = raw as Record<string, unknown>;
    for (const k of Object.keys(o)) if (!["finished", "bosses", "waves"].includes(k)) v.fail(`claim.${k}`, "champ inconnu");
    if (o.finished !== false) v.fail("claim.finished", "false attendu : une partie finie s'envoie par POST /challenges/{id}/runs");
    const ints = (k: "bosses" | "waves", max: number) => {
        const a = o[k];
        if (!Array.isArray(a) || a.length > 64 || !a.every((n) => Number.isInteger(n) && n >= 0 && n <= max)) {
            v.fail(`claim.${k}`, `liste d'entiers de 0 à ${max} attendue (64 au plus)`);
            return [];
        }
        return a as number[];
    };
    claim.bosses = ints("bosses", 1000);
    claim.waves = ints("waves", 99);
    return claim;
}

/**
 * POST /challenges/{challengeId}/attempts `{ attempt_id, time_ms, level_hash, rules_hash,
 * game_version, claim }`: an attempt that did not reach the end, sent when the game finds it
 * better than the player's best on this challenge. Its score comes from its replay only (PUT
 * /challenges/attempts/{attemptId}/replay). Idempotent by attempt_id; late after the end of
 * the challenge (+ 10 min), like the runs.
 */
export async function submitAttempt(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.challengeSubmit, "player:" + auth.player.id);
    const id = ctx.params.challengeId!;
    const period = parseChallengeId(id);
    if (!period) throw Errors.notFound("CHALLENGE_NOT_FOUND", "Défi introuvable.");
    const body = await readJsonObject(ctx, 4 * 1024);
    // Neither `player_id` nor a score is accepted: the player comes from the session, the score from the replay.
    const v = new Validator(body, ["attempt_id", "time_ms", "level_hash", "rules_hash", "game_version", "claim"]);
    const attemptId = v.str("attempt_id", { min: 36, max: 36, pattern: UUID_RE });
    const timeMs = v.int("time_ms", { min: 1, max: 24 * 3600 * 1000 });
    const levelHash = v.str("level_hash", { min: 64, max: 64, pattern: HASH_RE });
    const hash = v.str("rules_hash", { min: 64, max: 64, pattern: HASH_RE });
    const gameVersion = v.str("game_version", { max: 16, pattern: SEMVER_RE });
    const claim = readClaim(v, body.claim);
    v.done();

    const now = Date.now();
    if (period.start > utcDate(now)) throw Errors.conflict("CHALLENGE_NOT_STARTED", "Ce défi n'a pas encore commencé.");
    const c = await ctx.env.DB.prepare(`${CHALLENGE_SQL} WHERE c.id = ?1`).bind(id).first<ChallengeRow>();
    if (!c) throw Errors.notFound("CHALLENGE_NOT_FOUND", "Défi introuvable.");

    const existing = await ctx.env.DB.prepare("SELECT * FROM challenge_attempts WHERE player_id = ?1 AND attempt_id = ?2")
        .bind(auth.player.id, attemptId)
        .first<AttemptRow>();
    if (existing) {
        if (
            existing.challenge_id !== c.id ||
            existing.time_ms !== timeMs ||
            existing.level_hash !== levelHash ||
            existing.rules_hash !== hash ||
            existing.claim !== JSON.stringify(claim)
        )
            throw Errors.conflict("ATTEMPT_CONFLICT", "Cet essai a déjà été envoyé avec d'autres données.");
        return json(ctx, {
            duplicate: true,
            attempt: attemptJson(existing),
            replay: { required: existing.replay_at === null && existing.status === "PENDING", upload_path: attemptUploadPath(existing.attempt_id) },
        });
    }

    const late = isLate(c, now);
    const { level, layout } = await levelVersionOf(ctx, c);
    if (!layout) throw PROGRESS_UNAVAILABLE();
    const minGame = [CHALLENGE_MIN_GAME_VERSION, ctx.env.MIN_GAME_VERSION, level.min_game_version].reduce((a, b) => (compareVersions(a, b) >= 0 ? a : b));
    if (compareVersions(gameVersion!, minGame) < 0)
        throw new ApiError(426, "GAME_VERSION_UNSUPPORTED", "Version du jeu trop ancienne pour les défis.", { min_game_version: minGame });
    if (levelHash !== c.level_hash) throw Errors.conflict("LEVEL_MISMATCH", "Ce n'est pas la version du niveau de ce défi.", { level_hash: c.level_hash });
    if (hash !== c.rules_hash) throw Errors.conflict("RULES_MISMATCH", "Ces règles ne sont pas celles du défi.", { rules_hash: c.rules_hash });
    if (claim.bosses.length !== layout.bosses.length || claim.waves.length !== layout.ambushes.length)
        throw Errors.validation([
            { field: "claim", message: `une valeur par boss (${layout.bosses.length}) et par embuscade (${layout.ambushes.length}) du tracé du niveau` },
        ]);

    // Longer than the level allows: refused at once, no replay to send.
    const overLimit = timeMs! > level.time_limit_ms;
    const a: AttemptRow = {
        id: crypto.randomUUID(),
        challenge_id: c.id,
        player_id: auth.player.id,
        attempt_id: attemptId!,
        time_ms: timeMs!,
        level_hash: c.level_hash,
        rules_hash: c.rules_hash,
        game_version: gameVersion!,
        claim: JSON.stringify(claim),
        late: late ? 1 : 0,
        status: overLimit ? "REJECTED" : "PENDING",
        progress: null,
        route: null,
        detail: null,
        reasons: overLimit ? JSON.stringify(["durée au-delà de la limite du niveau"]) : null,
        replay_key: null,
        replay_sha256: null,
        replay_size: null,
        replay_at: null,
        created_at: nowIso(),
        verified_at: null,
    };
    try {
        await ctx.env.DB.prepare(
            `INSERT INTO challenge_attempts (id, challenge_id, player_id, attempt_id, time_ms, level_hash, rules_hash, game_version, claim, late,
               status, reasons, created_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)`,
        )
            .bind(a.id, a.challenge_id, a.player_id, a.attempt_id, a.time_ms, a.level_hash, a.rules_hash, a.game_version, a.claim, a.late,
                a.status, a.reasons, a.created_at)
            .run();
    } catch (e) {
        if (String(e).includes("UNIQUE")) throw Errors.conflict("DUPLICATE_SUBMISSION", "Essai déjà enregistré.");
        throw e;
    }
    return json(
        ctx,
        { duplicate: false, attempt: attemptJson(a), replay: { required: a.status === "PENDING", upload_path: attemptUploadPath(a.attempt_id) } },
        { status: 201 },
    );
}

/**
 * PUT /challenges/attempts/{attemptId}/replay (`attemptId`: the game's attempt_id): the
 * attempt's replay (CPRZ, format 4 with the challenge section). The server checks it with the
 * race analysis (rules, hearts, plausible trajectory, one life unless the challenge has its
 * checkpoints on) and computes the progress from it (route reached along the layout, bosses'
 * health removed, waves beaten). Only the player's best attempt on the challenge keeps its
 * replay.
 */
export async function uploadAttemptReplay(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const me = auth.player.id;
    await rateLimit(ctx, RULES.replayUpload, "player:" + me);
    const attemptId = ctx.params.attemptId!;
    const a = UUID_RE.test(attemptId)
        ? await ctx.env.DB.prepare("SELECT * FROM challenge_attempts WHERE player_id = ?1 AND attempt_id = ?2").bind(me, attemptId).first<AttemptRow>()
        : null;
    if (!a) throw Errors.notFound("ATTEMPT_NOT_FOUND", "Essai introuvable.");
    if (a.replay_at !== null) throw Errors.conflict("REPLAY_EXISTS", "Le replay de cet essai a déjà été envoyé.");
    if (a.status === "REJECTED") throw Errors.conflict("ATTEMPT_REJECTED", "Cet essai a été refusé.");
    const type = (ctx.request.headers.get("content-type") ?? "").toLowerCase();
    if (!type.startsWith("application/octet-stream"))
        throw new ApiError(415, "UNSUPPORTED_MEDIA_TYPE", "Le replay doit être envoyé en application/octet-stream.");
    const data = await readLimited(ctx.request, MAX_REPLAY_UPLOAD);
    const c = await ctx.env.DB.prepare(`${CHALLENGE_SQL} WHERE c.id = ?1`).bind(a.challenge_id).first<ChallengeRow>();
    if (!c) throw Errors.notFound("CHALLENGE_NOT_FOUND", "Défi introuvable.");
    const { level, layout } = await levelVersionOf(ctx, c);
    if (!layout) throw PROGRESS_UNAVAILABLE();
    let replay: Replay;
    try {
        replay = parseReplay(await inflateContainer(data));
    } catch (e) {
        if (!(e instanceof ReplayError)) throw e;
        throw new ApiError(422, "REPLAY_MALFORMED", `Replay illisible : ${e.message}.`);
    }
    const analysis = analyzeRace(replay, a.attempt_id, a.time_ms, JSON.parse(a.claim) as Claim, layout, level, {
        challengeRules: canonicalRules(c.id, rulesOf(c)),
        hearts: c.hearts,
        respawns: c.checkpoints === 1,
    });
    const ok = analysis.reasons.length === 0;
    // Never 100: only a finished run reaches it.
    const progress = ok ? raceScore(layout, analysis.route, analysis.bossRemoved, analysis.wavesDone, false) : null;
    // « Finir le niveau » (2.41): the tier of this progress, from the server's score only.
    const tier = progressTierOf(c, progress);

    const db = ctx.env.DB;
    const before = await db
        .prepare(
            `SELECT (SELECT MAX(progress) FROM challenge_attempts WHERE challenge_id = ?1 AND player_id = ?2 AND status = 'VERIFIED' AND id <> ?3) AS attempts,
                    EXISTS (SELECT 1 FROM challenge_runs WHERE challenge_id = ?1 AND player_id = ?2 AND status = 'VERIFIED') AS finished`,
        )
        .bind(c.id, me, a.id)
        .first<{ attempts: number | null; finished: number }>();
    const previous = before?.attempts ?? null;
    // The player's best attempt on this challenge keeps its replay; the others do not.
    const keep = progress !== null && (previous === null || progress > previous);
    let key: string | null = null;
    let digest: string | null = null;
    if (keep) {
        key = `challenges/${c.id}/${me}/attempt-${a.id}-${crypto.randomUUID()}.cprz`;
        digest = await sha256Hex(data);
        await ctx.env.STORAGE.put(key, data, {
            httpMetadata: { contentType: "application/octet-stream" },
            customMetadata: { challenge_attempt_id: a.id, sha256: digest, format: "CPRZ/CPRP" },
        });
    }
    const now = nowIso();
    const claim = await db
        .prepare(
            `UPDATE challenge_attempts SET status = ?2, progress = ?3, route = ?4, detail = ?5, reasons = ?6, replay_key = ?7, replay_sha256 = ?8,
               replay_size = ?9, replay_at = ?10, verified_at = ?11, tier = ?12 WHERE id = ?1 AND status = 'PENDING' AND replay_at IS NULL`,
        )
        .bind(
            a.id,
            ok ? "VERIFIED" : "REJECTED",
            progress,
            analysis.route,
            JSON.stringify(detailOf(layout, analysis)),
            ok ? null : JSON.stringify(analysis.reasons.slice(0, 8)),
            key,
            digest,
            key ? data.byteLength : null,
            now,
            ok ? now : null,
            tier,
        )
        .run();
    if (claim.meta.changes !== 1) {
        if (key) await ctx.env.STORAGE.delete(key);
        throw Errors.conflict("REPLAY_EXISTS", "Le replay de cet essai a déjà été envoyé.");
    }
    if (keep) {
        // The previous best attempt loses its replay (never a better one sent at the same time).
        const old = await db
            .prepare(
                `SELECT id, replay_key FROM challenge_attempts WHERE challenge_id = ?1 AND player_id = ?2 AND id <> ?3 AND replay_key IS NOT NULL
                   AND (progress IS NULL OR progress < ?4)`,
            )
            .bind(c.id, me, a.id, progress)
            .all<{ id: string; replay_key: string }>();
        for (const o of old.results) {
            await ctx.env.STORAGE.delete(o.replay_key);
            await db.prepare("UPDATE challenge_attempts SET replay_key = NULL, replay_sha256 = NULL, replay_size = NULL WHERE id = ?1").bind(o.id).run();
        }
    }
    if (!ok) await audit(ctx, me, "challenge_attempt_rejected", { attempt_id: a.id, reasons: analysis.reasons.slice(0, 5) });
    // My rank on the board of the attempt's period (« Pendant le défi », or « Depuis » for a late one).
    const rank = ok ? ((await db.prepare(`${progressSql({}).sql} WHERE player_id = ?3`).bind(c.id, a.late, me).first<ProgressRow>())?.rank ?? null) : null;
    const best = before?.finished === 1 ? 100 : Math.max(previous ?? -1, progress ?? -1);
    return json(
        ctx,
        {
            attempt: {
                attempt_id: a.attempt_id,
                status: ok ? ("VERIFIED" as const) : ("REJECTED" as const),
                progress,
                finished: false,
                late: a.late === 1,
                rank,
                reasons: ok ? [] : analysis.reasons.slice(0, 8),
                ...(c.objective === "finish" ? { tier } : {}),
            },
            // A new record of progress on this challenge (better than every attempt and finished run before).
            new_best: keep && before?.finished !== 1,
            best_progress: best < 0 ? null : best,
        },
        { status: 201 },
    );
}

/**
 * GET /challenges/attempts/{id}/replay (`id`: the server's id of the attempt, the `attempt_id`
 * of a board=progress entry): the kept replay of a VERIFIED best attempt, public like the
 * replays of the runs (403 PROFILE_PRIVATE for a private profile); its player always.
 */
export async function downloadAttemptReplay(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const id = ctx.params.id!;
    const a = UUID_RE.test(id)
        ? await ctx.env.DB.prepare(
              `SELECT a.player_id, a.status, a.replay_key, a.replay_sha256, p.account_status AS owner_status, p.profile_visibility AS owner_visibility
               FROM challenge_attempts a JOIN players p ON p.id = a.player_id WHERE a.id = ?1`,
          )
              .bind(id)
              .first<{ player_id: string; status: string; replay_key: string | null; replay_sha256: string | null; owner_status: string; owner_visibility: string }>()
        : null;
    const isOwner = a !== null && viewer?.player.id === a.player_id;
    if (!a || (!isOwner && (a.status !== "VERIFIED" || a.owner_status !== "active"))) throw Errors.notFound("ATTEMPT_NOT_FOUND", "Essai introuvable.");
    if (!isOwner && a.owner_visibility === "private") throw Errors.forbidden("PROFILE_PRIVATE", "Le joueur a masqué son profil.");
    // Only the player's best attempt keeps its replay.
    if (!a.replay_key) throw Errors.notFound("REPLAY_NOT_FOUND", "Aucun replay pour cet essai.");
    const obj = await ctx.env.STORAGE.get(a.replay_key);
    if (!obj) throw Errors.notFound("REPLAY_NOT_FOUND", "Fichier de replay absent.");
    return new Response(obj.body, {
        headers: {
            "content-type": "application/octet-stream",
            "content-length": String(obj.size),
            "x-replay-sha256": a.replay_sha256 ?? "",
            "cache-control": "private, max-age=3600",
            "x-content-type-options": "nosniff",
        },
    });
}

// ------------------------------------------------------------------- my results

export interface Streak {
    current: number;
    best: number;
    last_period: string | null;
}

/**
 * Streak of a kind: consecutive periods (days, ISO weeks, months), up to the current one or
 * the one before, on which the player has at least one run of that period's challenge that
 * was received in time and not rejected. `periods`: sorted, distinct.
 */
export function periodStreak(kind: ChallengeKind, periods: string[], current: string): Streak {
    let best = 0;
    let run = 0;
    let prev: string | null = null;
    for (const p of periods) {
        run = prev !== null && shiftPeriod(kind, prev, 1) === p ? run + 1 : 1;
        best = Math.max(best, run);
        prev = p;
    }
    const cur = prev !== null && (prev === current || prev === shiftPeriod(kind, current, -1)) ? run : 0;
    return { current: cur, best, last_period: prev };
}

/** The 2.32 streak (days): `{ current, best, last_date }`. */
export function streakOf(dates: string[], today: string): { current: number; best: number; last_date: string | null } {
    const s = periodStreak("daily", dates, today);
    return { current: s.current, best: s.best, last_date: s.last_period };
}

const TIER_RANK_SQL = "CASE r.tier WHEN 'gold' THEN 3 WHEN 'silver' THEN 2 ELSE 1 END";
/** An attempt's tier (« Finir le niveau », 2.41): never Or; 0 without one. */
const ATTEMPT_TIER_RANK_SQL = "CASE a.tier WHEN 'silver' THEN 2 WHEN 'bronze' THEN 1 ELSE 0 END";

interface PlayerResult {
    challenge_id: string;
    kind: ChallengeKind;
    period: string;
    date: string;
    best_time_ms: number | null;
    tier: Tier | null;
    status: RunRow["status"];
    late: boolean;
    best_progress: number | null;
}

/**
 * The player's result on each challenge played (most recent first; `span`: only the
 * challenges that touch these days). The runs received in time come first: the result is
 * the best of them that was not rejected (its time and status; its tier is the best one
 * reached in time); with only late runs, the best late one (`late: true`, its tier the best
 * one reached late); with only rejected runs, the best rejected one (tier null). A challenge
 * only tried (attempts of « Plus loin », no run) gives the state of its best attempt, without
 * time nor tier. `best_progress`: the best VERIFIED progress on the challenge, in time or
 * late (attempts; 100 for a finished run), null without one.
 */
async function playerResults(ctx: Ctx, playerId: string, o: { span?: { start: string; end: string }; limit: number }): Promise<PlayerResult[]> {
    const rows = await ctx.env.DB.prepare(
        `WITH runs AS (
           SELECT r.challenge_id, r.time_ms, r.status, r.late,
                  ROW_NUMBER() OVER (PARTITION BY r.challenge_id
                                     ORDER BY (r.status = 'REJECTED'), r.late, r.time_ms, (r.status <> 'VERIFIED'), r.created_at, r.id) AS rn,
                  MAX(CASE WHEN r.status <> 'REJECTED' AND r.late = 0 THEN ${TIER_RANK_SQL} ELSE 0 END) OVER (PARTITION BY r.challenge_id) AS live_tier,
                  MAX(CASE WHEN r.status <> 'REJECTED' THEN ${TIER_RANK_SQL} ELSE 0 END) OVER (PARTITION BY r.challenge_id) AS any_tier
           FROM challenge_runs r WHERE r.player_id = ?1),
         attempts AS (
           SELECT a.challenge_id, a.status, a.late,
                  ROW_NUMBER() OVER (PARTITION BY a.challenge_id
                                     ORDER BY (a.status = 'REJECTED'), a.late, (a.status <> 'VERIFIED'), a.progress DESC, a.created_at, a.id) AS rn,
                  MAX(CASE WHEN a.status = 'VERIFIED' AND a.late = 0 THEN ${ATTEMPT_TIER_RANK_SQL} ELSE 0 END) OVER (PARTITION BY a.challenge_id) AS live_tier,
                  MAX(CASE WHEN a.status = 'VERIFIED' THEN ${ATTEMPT_TIER_RANK_SQL} ELSE 0 END) OVER (PARTITION BY a.challenge_id) AS any_tier
           FROM challenge_attempts a WHERE a.player_id = ?1),
         bests AS (
           SELECT challenge_id, MAX(p) AS best FROM (
             SELECT challenge_id, progress AS p FROM challenge_attempts WHERE player_id = ?1 AND status = 'VERIFIED'
             UNION ALL
             SELECT challenge_id, 100.0 FROM challenge_runs WHERE player_id = ?1 AND status = 'VERIFIED')
           GROUP BY challenge_id),
         played AS (SELECT challenge_id FROM runs WHERE rn = 1 UNION SELECT challenge_id FROM attempts WHERE rn = 1)
         SELECT played.challenge_id, c.kind, c.period, c.date, runs.time_ms, runs.status, runs.late, runs.live_tier, runs.any_tier,
                attempts.status AS attempt_status, attempts.late AS attempt_late, attempts.live_tier AS attempt_live_tier,
                attempts.any_tier AS attempt_any_tier, bests.best AS best_progress
         FROM played JOIN challenges c ON c.id = played.challenge_id
         LEFT JOIN runs ON runs.challenge_id = played.challenge_id AND runs.rn = 1
         LEFT JOIN attempts ON attempts.challenge_id = played.challenge_id AND attempts.rn = 1
         LEFT JOIN bests ON bests.challenge_id = played.challenge_id
         WHERE (?2 IS NULL OR (c.date < ?3 AND c.end_date > ?2))
         ORDER BY c.date DESC, ${kindOrder("c.kind")} DESC LIMIT ?4`,
    )
        .bind(playerId, o.span?.start ?? null, o.span?.end ?? null, o.limit)
        .all<{
            challenge_id: string;
            kind: ChallengeKind;
            period: string;
            date: string;
            time_ms: number | null;
            status: RunRow["status"] | null;
            late: number | null;
            live_tier: number | null;
            any_tier: number | null;
            attempt_status: RunRow["status"] | null;
            attempt_late: number | null;
            attempt_live_tier: number | null;
            attempt_any_tier: number | null;
            best_progress: number | null;
        }>();
    return rows.results.map((r) => {
        const base = { challenge_id: r.challenge_id, kind: r.kind, period: r.period, date: r.date, best_progress: r.best_progress };
        // « Finir le niveau » (2.41): an attempt's tier counts too (always 0 on a « Vitesse » challenge).
        const attemptRank = (late: boolean) => (late ? (r.attempt_any_tier ?? 0) : (r.attempt_live_tier ?? 0));
        if (r.status === null) {
            const rank = attemptRank(r.attempt_late === 1);
            return {
                ...base,
                best_time_ms: null,
                tier: rank > 0 ? TIERS[rank - 1]! : null,
                status: r.attempt_status ?? "PENDING",
                late: r.attempt_late === 1,
            };
        }
        const runRank = r.status === "REJECTED" ? 0 : r.late === 1 ? (r.any_tier ?? 0) : (r.live_tier ?? 0);
        const rank = Math.max(runRank, attemptRank(r.late === 1));
        return {
            ...base,
            best_time_ms: r.time_ms,
            tier: rank > 0 ? TIERS[rank - 1]! : null,
            status: r.status,
            late: r.late === 1,
        };
    });
}

/** A result as the calendar's `me` gives it. */
function resultView(r: PlayerResult) {
    return { best_time_ms: r.best_time_ms, tier: r.tier, status: r.status, late: r.late, best_progress: r.best_progress };
}

/**
 * GET /challenges/me?limit=60: `{ streak: { current, best, last_date }, streaks: { daily,
 * weekly, monthly } ({ current, best, last_period }), verified: { daily, weekly, monthly }
 * ({ gold, silver, bronze, best_streak }, 2.35: the rewards' counts, challengeRewards.ts),
 * results: [{ challenge_id, kind, period, date, best_time_ms, tier, status, late,
 * best_progress }] }` (most recent first). `streak` is the days' streak of the 2.32 games.
 */
export async function mine(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const { limit } = paging(ctx.url, 60, 366);
    const me = auth.player.id;
    // The game polls its results: a run held by an outdated board is decided again.
    await recheckHeldRuns(ctx, me, 20);
    const results = await playerResults(ctx, me, { limit });
    // A period counts with a run in time that was not rejected, or (« Finir le niveau », 2.41)
    // an attempt in time whose verified progress reached a tier.
    const played = await ctx.env.DB.prepare(
        `SELECT DISTINCT c.kind, c.period FROM (
           SELECT r.challenge_id FROM challenge_runs r WHERE r.player_id = ?1 AND r.status <> 'REJECTED' AND r.late = 0
           UNION SELECT a.challenge_id FROM challenge_attempts a WHERE a.player_id = ?1 AND a.status = 'VERIFIED' AND a.late = 0 AND a.tier IS NOT NULL) x
         JOIN challenges c ON c.id = x.challenge_id ORDER BY c.kind, c.period`,
    )
        .bind(me)
        .all<{ kind: ChallengeKind; period: string }>();
    const today = utcDate();
    const streaks = Object.fromEntries(
        KINDS.map((k) => [
            k,
            periodStreak(
                k,
                played.results.filter((p) => p.kind === k).map((p) => p.period),
                periodOf(k, today),
            ),
        ]),
    ) as Record<ChallengeKind, Streak>;
    return json(ctx, {
        streak: { current: streaks.daily.current, best: streaks.daily.best, last_date: streaks.daily.last_period },
        streaks,
        // 2.35: what the rewards of the challenges count (VERIFIED runs only, all of them).
        verified: await challengeRewardProgress(ctx, me),
        results: results.map((r) => ({
            challenge_id: r.challenge_id,
            kind: r.kind,
            period: r.period,
            date: r.date,
            best_time_ms: r.best_time_ms,
            tier: r.tier,
            status: r.status,
            late: r.late,
            best_progress: r.best_progress,
        })),
    });
}

// ------------------------------------------------------------------- account

/** R2 keys of a player's challenge replays: runs and (2.33) attempts (account deletion). */
export async function challengeReplayKeys(ctx: Ctx, playerId: string): Promise<string[]> {
    const rows = await ctx.env.DB.prepare(
        `SELECT replay_key FROM challenge_runs WHERE player_id = ?1 AND replay_key IS NOT NULL
         UNION ALL SELECT replay_key FROM challenge_attempts WHERE player_id = ?1 AND replay_key IS NOT NULL`,
    )
        .bind(playerId)
        .all<{ replay_key: string }>();
    return rows.results.map((r) => r.replay_key);
}

/**
 * Daily maintenance: missing challenges, the creators featured, runs held for review, then
 * (2.43) the rewards of the challenges that ended.
 */
export async function maintenance(ctx: Ctx): Promise<{ rechecked: number; rewards_won: number }> {
    await ensureChallenges(ctx);
    const rechecked = await recheckHeldRuns(ctx, null);
    return { rechecked, rewards_won: await grantDuePrizes(ctx, { limit: 50 }) };
}

// --------------------------------------------------- rewards of one challenge (2.43)

/**
 * Grants the rewards of the ended challenges not granted yet, from GRANT_DELAY_MS after their
 * end (the runs received in time are in and their replays checked): the oldest first, up to
 * `limit`, or the challenge `onlyId` alone. Each by its main board: the times (« Vitesse »)
 * or the progress (« Finir le niveau »). Returns the rewards newly won.
 */
export async function grantDuePrizes(ctx: Ctx, o: { limit?: number; onlyId?: string } = {}): Promise<number> {
    const cutoff = utcDate(Date.now() - GRANT_DELAY_MS);
    const rows = await ctx.env.DB.prepare(
        `SELECT c.id, c.kind, c.period, c.date, c.objective FROM challenges c
         WHERE c.end_date <= ?1 AND (?2 IS NULL OR c.id = ?2)
           AND EXISTS (SELECT 1 FROM challenge_reward_rules r WHERE r.challenge_id = c.id)
           AND NOT EXISTS (SELECT 1 FROM challenge_reward_grants g WHERE g.challenge_id = c.id)
         ORDER BY c.end_date, c.id LIMIT ?3`,
    )
        .bind(cutoff, o.onlyId ?? null, o.limit ?? 20)
        .all<{ id: string; kind: ChallengeKind; period: string; date: string; objective: Objective }>();
    let won = 0;
    for (const c of rows.results) won += await grantPrizes(ctx, c, (c.objective === "finish" ? progressSql({}) : boardSql({})).sql);
    return won;
}

/** The due rewards granted on a reading (a failure is logged; the reading answers all the same). */
async function grantQuietly(ctx: Ctx, o: { limit?: number; onlyId?: string }): Promise<void> {
    try {
        await grantDuePrizes(ctx, o);
    } catch (e) {
        log("error", "challenge_rewards_grant_failed", { request_id: ctx.requestId, error: e instanceof Error ? e.message : String(e) });
    }
}

interface WonRow {
    challenge_id: string;
    kind: string;
    ref: string;
    granted_at: string;
    challenge_kind: ChallengeKind | null;
    period: string | null;
    date: string | null;
    level_title: string | null;
    title_text: string | null;
    title_color: string | null;
    suit_name: string | null;
    suit_color: string | null;
    accent_color: string | null;
    scarf_color: string | null;
}

/**
 * GET /players/me/rewards (2.43): `{ rewards: [{ challenge: { id, kind, period, date, title },
 * kind: item|title|suit, ref, item: { id, category, name } | null, title: { id, text, color } |
 * null, suit?: { id, name, colors: { suit, accent, scarf } }, granted_at }] }`: the rewards of
 * challenges I won, the most recent first (the rewards due are granted first). An item of the
 * catalogue unlocks in the Collection, a title ("srv:<id>") or a suit created on the server
 * ("srvsuit:<id>", its current name and colours) may be worn (PUT /players/me/cosmetics).
 */
export async function myRewards(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await grantQuietly(ctx, { limit: 5 });
    const rows = await ctx.env.DB.prepare(
        `SELECT w.challenge_id, w.reward_kind AS kind, w.reward_ref AS ref, w.granted_at, c.kind AS challenge_kind, c.period, c.date, c.level_title,
                t.text AS title_text, t.color AS title_color, s.name AS suit_name, s.suit_color, s.accent_color, s.scarf_color
         FROM player_won_rewards w LEFT JOIN challenges c ON c.id = w.challenge_id
         LEFT JOIN server_titles t ON w.reward_kind = 'title' AND t.id = CAST(substr(w.reward_ref, 5) AS INTEGER)
         LEFT JOIN creations s ON w.reward_kind = 'suit' AND s.kind = 'suit' AND s.id = CAST(substr(w.reward_ref, 9) AS INTEGER)
         WHERE w.player_id = ?1 ORDER BY w.granted_at DESC, w.challenge_id, w.reward_ref LIMIT 500`,
    )
        .bind(auth.player.id)
        .all<WonRow>();
    const rewards = [];
    for (const r of rows.results) {
        const p = parseChallengeId(r.challenge_id);
        const item = r.kind === "item" ? prizeItem(r.ref) : null;
        const title = r.kind === "title" && r.title_text ? { id: r.ref, text: r.title_text, color: r.title_color ?? "gold" } : null;
        const suit =
            r.kind === "suit" && r.suit_name && r.suit_color && r.accent_color && r.scarf_color
                ? suitJson(r.ref, r.suit_name, r.suit_color, r.accent_color, r.scarf_color)
                : null;
        if (!item && !title && !suit) continue;
        rewards.push({
            challenge: {
                id: r.challenge_id,
                kind: r.challenge_kind ?? p?.kind ?? null,
                period: r.period ?? p?.period ?? null,
                date: r.date ?? p?.start ?? null,
                title: r.level_title,
            },
            kind: r.kind,
            ref: r.ref,
            item: item ? { id: item.id, category: item.category, name: item.name } : null,
            title,
            ...(suit ? { suit } : {}),
            granted_at: r.granted_at,
        });
    }
    return json(ctx, { rewards });
}
