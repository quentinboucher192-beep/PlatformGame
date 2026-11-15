// Daily challenges (« Défis du jour », game 2.31). The contract with the game is
// docs/online/DAILY_CHALLENGES.md: one challenge per UTC day (`daily-AAAA-MM-JJ`), drawn by
// the server among the ranked campaign levels or chosen by the administrator
// (scripts/challenge-set.mjs, campaign or community level), with its rules (hearts, power
// slots, imposed powers, stations, checkpoints), the canonical form of the rules and its
// SHA-256, and the tiers. The challenges exist from today to 7 days ahead (drawn in advance so
// that the administrator can replace them) but a challenge is never shown before its day, and
// once its day has begun it never changes.
//
// Runs follow the records: the player comes from the session, idempotence by attempt_id,
// tier computed by the server, statuses PENDING / VERIFIED / REJECTED with the decision and
// trust rules of the records, and the replay (format 4 with the challenge section) checked by
// the server (structure, level revision, duration, plausible trajectory, health never above
// the challenge's hearts, section identical to the canonical rules). « Vérifié » means
// « replay checked by the server », not re-simulated.

import catalogJson from "./challengeCatalog.json";
import { audit, requireAuth, type AuthInfo } from "./auth";
import { pseudoVersionOf } from "./community";
import { boardCosmetics, withCosmetics, type BoardCosmetics } from "./cosmetics";
import { sha256Hex } from "./crypto";
import { ApiError, Errors, json, log, nowIso, readJsonObject, readLimited, type Ctx } from "./http";
import { avatarUrl, type LevelVersionRow, type RecordRow } from "./model";
import { notify } from "./notifications";
import { RULES, rateLimit } from "./ratelimit";
import { decide, type BoardContext, type Decision, type ReplayState } from "./records";
import { EventType, MAX_REPLAY_UPLOAD, PickupKind, ReplayError, analyzeReplay, inflateContainer, parseReplay, type Replay, type ReplayAnalysis } from "./replay";
import { COUNTRY_RE, HASH_RE, SEMVER_RE, UUID_RE, Validator, compareVersions, paging, queryEnum, queryString } from "./validate";

// ------------------------------------------------------------------ catalogue

/** A campaign level as the game's files describe it (scripts/challenge-catalog.mjs). */
export interface CatalogLevel {
    name: string;
    planet: string;
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
export const CHALLENGE_MIN_GAME_VERSION = "2.31.0";
/** Challenges exist from today to today + DAYS_AHEAD. */
export const DAYS_AHEAD = 7;
/** An automatic draw avoids the levels of the challenges this many days around it. */
const RECENT_DAYS = 10;
/** Runs are accepted until this long after the end of the day (a run finished at 23:59). */
export const LATE_SUBMIT_MS = 10 * 60 * 1000;
/**
 * Powers an automatic draw may add: they only help moving or surviving, and one is added
 * only when its input is not used by any power of the level (never a power the level needs
 * replaced, whatever the replacement rule).
 */
export const SAFE_EXTRA_POWERS = ["dash", "glide", "triple_jump", "shield", "wave", "grapple"] as const;
const HEART_CHOICES = [1, 2, 3, 5] as const;
const MAX_SLOTS = 8;

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

// --------------------------------------------------------------------- dates

const DATE_RE = /^\d{4}-\d{2}-\d{2}$/;
const CHALLENGE_ID_RE = /^daily-(\d{4}-\d{2}-\d{2})$/;

/** UTC day of an instant ('AAAA-MM-JJ'). */
export function utcDate(ms: number = Date.now()): string {
    return new Date(ms).toISOString().slice(0, 10);
}

export function addDays(date: string, n: number): string {
    return utcDate(Date.parse(`${date}T00:00:00Z`) + n * 86400000);
}

function isDate(s: string): boolean {
    return DATE_RE.test(s) && !Number.isNaN(Date.parse(`${s}T00:00:00Z`)) && utcDate(Date.parse(`${s}T00:00:00Z`)) === s;
}

export const challengeIdOf = (date: string) => `daily-${date}`;
const startsAt = (date: string) => `${date}T00:00:00Z`;
const endsAt = (date: string) => `${addDays(date, 1)}T00:00:00Z`;

/** The date of a well-formed challenge id, else null. */
function dateOfId(id: string): string | null {
    const m = CHALLENGE_ID_RE.exec(id);
    return m && isDate(m[1]!) ? m[1]! : null;
}

// ------------------------------------------------------------------ rows

interface ChallengeRow {
    id: string;
    kind: string;
    date: string;
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
    author_name: string | null;
}

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

const CHALLENGE_SQL = "SELECT c.*, p.username AS author_name FROM daily_challenges c LEFT JOIN players p ON p.id = c.featured_player_id";

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

/** The challenge as the game reads it (docs/online/DAILY_CHALLENGES.md §1). */
export function challengeJson(c: ChallengeRow) {
    return {
        id: c.id,
        kind: "daily",
        date: c.date,
        starts_at: startsAt(c.date),
        ends_at: endsAt(c.date),
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
        tiers: { silver_ms: c.silver_ms, gold_ms: c.gold_ms, gold_no_damage: c.gold_no_damage === 1 },
    };
}

/** Tier of a run, computed by the server from the challenge (never taken from the client). */
export function tierOf(c: Pick<ChallengeRow, "silver_ms" | "gold_ms" | "gold_no_damage">, timeMs: number, damage: number): Tier {
    if (timeMs <= c.gold_ms && (c.gold_no_damage !== 1 || damage === 0)) return "gold";
    if (timeMs <= c.silver_ms) return "silver";
    return "bronze";
}

function runJson(r: RunRow) {
    return { id: r.id, status: r.status, tier: r.tier, time_ms: r.time_ms };
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
 * finishable), the level's start powers plus 0 to 2 extra powers of the safe list whose
 * input no power of the level uses, at least the level's slots plus one per extra power,
 * hearts among 1, 2, 3, 5 or the player's maximum, checkpoints or not.
 */
export function drawRules(level: CatalogLevel, catalog: ChallengeCatalog = CATALOG): ChallengeRules {
    const inputs = new Set(level.level_powers.map((p) => catalog.powers[p] ?? p));
    const extras: string[] = [];
    const wanted = randomInt(3);
    for (const p of shuffled(SAFE_EXTRA_POWERS)) {
        if (extras.length >= wanted) break;
        const input = catalog.powers[p];
        if (input === undefined || level.level_powers.includes(p) || inputs.has(input)) continue;
        extras.push(p);
        inputs.add(input);
    }
    const powers = [...new Set([...level.start_powers, ...extras])].sort();
    const slots = Math.min(MAX_SLOTS, Math.max(level.slots + extras.length, powers.length));
    const hearts = [...new Set([...HEART_CHOICES, catalog.player_max_hearts])];
    return { hearts: hearts[randomInt(hearts.length)]!, slots, powers, stations: true, checkpoints: randomInt(2) === 1 };
}

interface CandidateRow {
    id: string;
    name: string;
    current_version: number;
    content_hash: string;
}

/**
 * Draws the challenge of `date` (campaign levels only, never a community level) and inserts
 * it unless one exists already (concurrent draws: the first one wins). Returns false when no
 * level can be drawn (no ranked level whose active revision is in the catalogue).
 */
async function drawChallenge(ctx: Ctx, date: string): Promise<boolean> {
    const db = ctx.env.DB;
    const rows = await db
        .prepare(
            `SELECT l.id, l.name, l.current_version, l.content_hash FROM levels l
             JOIN level_versions v ON v.level_id = l.id AND v.content_hash = l.content_hash
             WHERE l.ranking_enabled = 1 AND v.compatibility_status = 'active' ORDER BY l.id`,
        )
        .all<CandidateRow>();
    // The catalogue must describe the very revision registered on the server (its medals,
    // slots and powers), otherwise the level is left out until the catalogue is regenerated.
    const candidates = rows.results.filter((l) => {
        const c = CATALOG.levels[l.id];
        return c !== undefined && c.auto && c.revision === l.current_version;
    });
    if (candidates.length === 0) {
        log("warn", "challenge_draw_no_level", { date });
        return false;
    }
    // Not a level of the challenges around this day: among the candidates, those used the
    // longest ago (never used within the window first).
    const near = await db
        .prepare("SELECT level_id, date FROM daily_challenges WHERE level_source = 'campaign' AND date <> ?1 AND date BETWEEN ?2 AND ?3")
        .bind(date, addDays(date, -RECENT_DAYS), addDays(date, RECENT_DAYS))
        .all<{ level_id: string; date: string }>();
    const day = Date.parse(`${date}T00:00:00Z`);
    const distance = (id: string) =>
        near.results.filter((n) => n.level_id === id).reduce((m, n) => Math.min(m, Math.abs(Date.parse(`${n.date}T00:00:00Z`) - day)), Infinity);
    const far = Math.max(...candidates.map((l) => distance(l.id)));
    const pool = candidates.filter((l) => distance(l.id) === far);
    const level = pool[randomInt(pool.length)]!;
    const info = CATALOG.levels[level.id]!;
    const id = challengeIdOf(date);
    const rules = drawRules(info);
    const now = nowIso();
    const res = await db
        .prepare(
            `INSERT OR IGNORE INTO daily_challenges (id, kind, date, source, level_source, level_id, level_version, level_hash, level_title,
               hearts, slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, featured_player_id,
               featured_notified_at, created_at, updated_at)
             VALUES (?1, 'daily', ?2, 'auto', 'campaign', ?3, ?4, ?5, ?6, ?7, ?8, ?9, 1, ?10, ?11, ?12, ?13, 1, NULL, NULL, ?14, ?14)`,
        )
        .bind(
            id,
            date,
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
 * The challenge of the day features a community level: its author is told, once (the claim
 * on featured_notified_at makes concurrent calls send a single notification).
 */
async function notifyFeatured(ctx: Ctx, today: string): Promise<void> {
    const c = await ctx.env.DB.prepare(
        `SELECT id, level_id, level_title, featured_player_id FROM daily_challenges
         WHERE date = ?1 AND level_source = 'community' AND featured_player_id IS NOT NULL AND featured_notified_at IS NULL`,
    )
        .bind(today)
        .first<{ id: string; level_id: string; level_title: string; featured_player_id: string }>();
    if (!c) return;
    const claim = await ctx.env.DB.prepare("UPDATE daily_challenges SET featured_notified_at = ?2 WHERE id = ?1 AND featured_notified_at IS NULL")
        .bind(c.id, nowIso())
        .run();
    if (claim.meta.changes !== 1) return;
    await notify(ctx, c.featured_player_id, "challenge_featured", {
        levelId: c.level_id,
        data: { title: c.level_title, challenge_id: c.id, date: today, level_id: c.level_id },
    });
}

/**
 * The challenges of today to today + 7 exist (missing ones are drawn) and the creator of a
 * community level of the day is told. Called by every read of the current challenge and by
 * the daily maintenance; an existing challenge is never touched (INSERT OR IGNORE).
 */
export async function ensureChallenges(ctx: Ctx): Promise<void> {
    const today = utcDate();
    const last = addDays(today, DAYS_AHEAD);
    const have = await ctx.env.DB.prepare("SELECT date FROM daily_challenges WHERE date BETWEEN ?1 AND ?2").bind(today, last).all<{ date: string }>();
    const existing = new Set(have.results.map((r) => r.date));
    for (let i = 0; i <= DAYS_AHEAD; ++i) {
        const date = addDays(today, i);
        if (!existing.has(date) && !(await drawChallenge(ctx, date))) break;
    }
    await notifyFeatured(ctx, today);
}

// -------------------------------------------------------------- read routes

/** A challenge that has begun; a future or unknown one is « not found » (never revealed). */
async function startedChallenge(ctx: Ctx, id: string): Promise<ChallengeRow> {
    const date = dateOfId(id);
    if (!date || date > utcDate()) throw Errors.notFound("CHALLENGE_NOT_FOUND", "Défi introuvable.");
    const c = await ctx.env.DB.prepare(`${CHALLENGE_SQL} WHERE c.id = ?1`).bind(id).first<ChallengeRow>();
    if (!c) throw Errors.notFound("CHALLENGE_NOT_FOUND", "Défi introuvable.");
    return c;
}

/** GET /challenges/daily/current: { server_time, challenge, previous } (today's and yesterday's). */
export async function current(ctx: Ctx): Promise<Response> {
    try {
        await ensureChallenges(ctx);
    } catch (e) {
        // The read still answers with what exists.
        log("error", "challenge_ensure_failed", { request_id: ctx.requestId, error: e instanceof Error ? e.message : String(e) });
    }
    const now = Date.now();
    const today = utcDate(now);
    const yesterday = addDays(today, -1);
    const rows = await ctx.env.DB.prepare(`${CHALLENGE_SQL} WHERE c.date IN (?1, ?2)`).bind(today, yesterday).all<ChallengeRow>();
    const of = (d: string) => rows.results.find((c) => c.date === d);
    const t = of(today);
    const y = of(yesterday);
    return json(ctx, { server_time: new Date(now).toISOString(), challenge: t ? challengeJson(t) : null, previous: y ? challengeJson(y) : null });
}

/** GET /challenges/daily?before=AAAA-MM-JJ&limit=N: challenges that have begun, most recent first. */
export async function archive(ctx: Ctx): Promise<Response> {
    const before = queryString(ctx.url, "before", DATE_RE);
    if (before !== undefined && !isDate(before)) throw Errors.validation([{ field: "before", message: "date AAAA-MM-JJ attendue" }]);
    const { limit } = paging(ctx.url, 30, 30);
    const today = utcDate();
    const rows = await ctx.env.DB.prepare(`${CHALLENGE_SQL} WHERE c.date <= ?1 AND (?2 IS NULL OR c.date < ?2) ORDER BY c.date DESC LIMIT ?3`)
        .bind(today, before ?? null, limit)
        .all<ChallengeRow>();
    const last = rows.results[rows.results.length - 1];
    return json(ctx, {
        challenges: rows.results.map(challengeJson),
        next_before: rows.results.length === limit && last ? last.date : null,
    });
}

// ----------------------------------------------------------------- leaderboard

export const SCOPES = ["world", "country", "friends"] as const;

interface BoardRow {
    run_id: string;
    player_id: string;
    time_ms: number;
    tier: Tier;
    created_at: string;
    has_replay: number;
    username: string;
    avatar: string;
    avatar_custom_key: string | null;
    country_code: string | null;
    location_visibility: string;
    rank: number;
    total: number;
}

/** Best VERIFIED run of each active player on a challenge; ?1 = challenge, filters from ?2. */
function boardSql(filter: { country?: string; friendsOf?: string }): { sql: string; extra: unknown[] } {
    const where: string[] = [];
    const extra: unknown[] = [];
    let n = 2;
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
    const sql = `
      WITH best AS (
        SELECT run_id, player_id, time_ms, tier, created_at, has_replay FROM (
          SELECT r.id AS run_id, r.player_id, r.time_ms, r.tier, r.created_at,
                 (r.replay_key IS NOT NULL AND r.replay_status = 'VERIFIED') AS has_replay,
                 ROW_NUMBER() OVER (PARTITION BY r.player_id ORDER BY r.time_ms, r.created_at, r.id) AS rn
          FROM challenge_runs r WHERE r.challenge_id = ?1 AND r.status = 'VERIFIED') WHERE rn = 1),
      ranked AS (
        SELECT b.*, p.username, p.avatar, p.avatar_custom_key, p.country_code, p.location_visibility
        FROM best b JOIN players p ON p.id = b.player_id
        WHERE p.account_status = 'active'${where.length ? " AND " + where.join(" AND ") : ""}),
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

/**
 * GET /challenges/{id}/leaderboard?scope=world|country|friends&country=&limit=50:
 * `{ entries: [{ rank, run_id, player, time_ms, tier, status }], me, total }`. `me` is the
 * viewer's best run: VERIFIED (with its rank in this scope) or, when better, PENDING (rank
 * null).
 */
export async function leaderboard(ctx: Ctx, viewer: AuthInfo | null): Promise<Response> {
    const c = await startedChallenge(ctx, ctx.params.challengeId!);
    const scope = queryEnum(ctx.url, "scope", SCOPES, "world");
    const page = paging(ctx.url, 50, 100);
    let country = queryString(ctx.url, "country", COUNTRY_RE);
    const filter: { country?: string; friendsOf?: string } = {};
    if (scope === "country") {
        country ??= viewer?.player.country_code ?? undefined;
        if (!country) throw Errors.validation([{ field: "country", message: "code pays ISO à 2 lettres attendu" }]);
        filter.country = country;
    } else if (scope === "friends") {
        if (!viewer) throw Errors.unauthorized();
        filter.friendsOf = viewer.player.id;
    }
    const { sql, extra } = boardSql(filter);
    const n = 2 + extra.length;
    const rows = await ctx.env.DB.prepare(`${sql} ORDER BY rank LIMIT ?${n} OFFSET ?${n + 1}`)
        .bind(c.id, ...extra, page.limit, page.offset)
        .all<BoardRow>();
    let total = rows.results[0]?.total ?? 0;
    if (rows.results.length === 0 && page.offset > 0) {
        const t = await ctx.env.DB.prepare(`SELECT COUNT(*) AS total FROM (${sql})`).bind(c.id, ...extra).first<{ total: number }>();
        total = t?.total ?? 0;
    }
    let mine: BoardRow | null = null;
    let pending: { id: string; time_ms: number; tier: Tier } | null = null;
    let verified: { id: string; time_ms: number; tier: Tier } | null = null;
    if (viewer) {
        mine = await ctx.env.DB.prepare(`${sql} WHERE player_id = ?${n}`).bind(c.id, ...extra, viewer.player.id).first<BoardRow>();
        const best = (status: string) =>
            ctx.env.DB.prepare(
                "SELECT id, time_ms, tier FROM challenge_runs WHERE challenge_id = ?1 AND player_id = ?2 AND status = ?3 ORDER BY time_ms, created_at, id LIMIT 1",
            )
                .bind(c.id, viewer.player.id, status)
                .first<{ id: string; time_ms: number; tier: Tier }>();
        pending = await best("PENDING");
        verified = mine ? { id: mine.run_id, time_ms: mine.time_ms, tier: mine.tier } : await best("VERIFIED");
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
    });
    let me = null;
    if (viewer) {
        const who = playerJson(viewer.player, looks);
        if (pending && (!verified || pending.time_ms < verified.time_ms))
            me = { rank: null, run_id: pending.id, player: who, time_ms: pending.time_ms, tier: pending.tier, status: "PENDING" as const };
        else if (mine) me = entry(mine);
        else if (verified) me = { rank: null, run_id: verified.id, player: who, time_ms: verified.time_ms, tier: verified.tier, status: "VERIFIED" as const };
    }
    return json(ctx, {
        challenge_id: c.id,
        scope,
        country_code: filter.country ?? null,
        entries: rows.results.map(entry),
        me,
        total,
    });
}

// ----------------------------------------------------------------- decisions

/** Position this time would take among the other players' verified bests of the challenge. */
async function boardContext(ctx: Ctx, challengeId: string, timeMs: number, playerId: string): Promise<BoardContext> {
    const row = await ctx.env.DB.prepare(
        `SELECT SUM(CASE WHEN b.t <= ?2 THEN 1 ELSE 0 END) AS better, COUNT(*) AS size, MIN(b.t) AS wr FROM (
           SELECT r.player_id, MIN(r.time_ms) AS t FROM challenge_runs r JOIN players p ON p.id = r.player_id
           WHERE r.challenge_id = ?1 AND r.status = 'VERIFIED' AND r.player_id <> ?3 AND p.account_status = 'active'
           GROUP BY r.player_id) b`,
    )
        .bind(challengeId, timeMs, playerId)
        .first<{ better: number | null; size: number; wr: number | null }>();
    return { rank: (row?.better ?? 0) + 1, size: row?.size ?? 0, worldRecordMs: row?.wr ?? null };
}

/**
 * The decision rules of the records, as for a category that needs its replay: a run is only
 * VERIFIED once the server has checked its replay; the first place of an established board
 * by a recent account waits for a manual review.
 */
function decideRun(trust: number, replay: ReplayState, timeMs: number, board: BoardContext): Decision {
    const d = decide(trust, "SPEEDRUN", replay, timeMs, board);
    if (d.status === "PENDING" && replay !== "valid") d.note = "En attente du replay.";
    return d;
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
        const board = await boardContext(ctx, r.challenge_id, r.time_ms, r.player_id);
        const d = decideRun(r.trust_level, "valid", r.time_ms, board);
        if (d.status === "PENDING" && d.note === r.verification_notes) continue;
        await applyDecision(ctx, r, d);
        if (d.status !== "PENDING") ++changed;
    }
    return changed;
}

// ------------------------------------------------------------------ submission

/** The level revision a challenge is played on (campaign) or its community version's geometry. */
async function levelOf(ctx: Ctx, c: ChallengeRow): Promise<LevelVersionRow> {
    if (c.level_source === "campaign") {
        const v = await ctx.env.DB.prepare("SELECT * FROM level_versions WHERE level_id = ?1 AND content_hash = ?2")
            .bind(c.level_id, c.level_hash)
            .first<LevelVersionRow>();
        if (v) return v;
    } else {
        const v = await ctx.env.DB.prepare("SELECT * FROM community_level_versions WHERE level_id = ?1 AND version = ?2")
            .bind(c.level_id, c.level_version)
            .first<Record<string, unknown>>();
        if (v && v.level_hash === c.level_hash) return pseudoVersionOf(v);
    }
    throw Errors.notFound("LEVEL_VERSION_NOT_FOUND", "Le niveau de ce défi n'est plus disponible.");
}

/** POST /challenges/{challengeId}/runs: a run of the challenge; idempotent by attempt_id. */
export async function submitRun(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    await rateLimit(ctx, RULES.challengeSubmit, "player:" + auth.player.id);
    const id = ctx.params.challengeId!;
    const date = dateOfId(id);
    if (!date) throw Errors.notFound("CHALLENGE_NOT_FOUND", "Défi introuvable.");
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
    if (date > utcDate(now)) throw Errors.conflict("CHALLENGE_NOT_STARTED", "Ce défi n'a pas encore commencé.");
    const c = await ctx.env.DB.prepare(`${CHALLENGE_SQL} WHERE c.id = ?1`).bind(id).first<ChallengeRow>();
    if (!c) throw Errors.notFound("CHALLENGE_NOT_FOUND", "Défi introuvable.");

    // Idempotence: the same attempt can be sent again (lost response, offline queue), even
    // after the end of the challenge.
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

    if (now > Date.parse(endsAt(c.date)) + LATE_SUBMIT_MS)
        throw Errors.conflict("CHALLENGE_ENDED", "Ce défi est terminé.", { ends_at: endsAt(c.date) });
    const level = await levelOf(ctx, c);
    const minGame = [CHALLENGE_MIN_GAME_VERSION, ctx.env.MIN_GAME_VERSION, level.min_game_version].reduce((a, b) => (compareVersions(a, b) >= 0 ? a : b));
    if (compareVersions(gameVersion!, minGame) < 0)
        throw new ApiError(426, "GAME_VERSION_UNSUPPORTED", "Version du jeu trop ancienne pour les défis du jour.", { min_game_version: minGame });
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
        tier: tierOf(c, timeMs!, damage!),
        status: "PENDING",
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
               game_version, tier, status, created_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, 'PENDING', ?12)`,
        )
            .bind(run.id, run.challenge_id, run.player_id, run.attempt_id, run.time_ms, run.deaths, run.damage_taken, run.level_hash,
                run.rules_hash, run.game_version, run.tier, run.created_at)
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
    else decision = decideRun(auth.player.trust_level, "none", run.time_ms, await boardContext(ctx, c.id, run.time_ms, auth.player.id));
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
        const board = await boardContext(ctx, c.id, r.time_ms, r.player_id);
        const d = decideRun(r.trust_level, analysis.ok ? "valid" : "invalid", r.time_ms, board);
        if (!analysis.ok) d.note = analysis.reasons.join(" ; ").slice(0, 500);
        await applyDecision(ctx, r, d);
    }
    return json(ctx, { run: runJson(r), replay: { verification_status: replayStatus }, analysis }, { status: 201 });
}

/** GET /challenges/runs/{runId}/replay: public for a VERIFIED run (ghost of the day's first), else its owner. */
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

// ------------------------------------------------------------------- my results

/**
 * Streak: consecutive UTC days, up to today or yesterday, on which the player has at least
 * one run of that day's challenge that was not rejected. `dates`: sorted, distinct.
 */
export function streakOf(dates: string[], today: string): { current: number; best: number; last_date: string | null } {
    let best = 0;
    let run = 0;
    let prev: string | null = null;
    for (const d of dates) {
        run = prev !== null && addDays(prev, 1) === d ? run + 1 : 1;
        best = Math.max(best, run);
        prev = d;
    }
    const current = prev !== null && (prev === today || prev === addDays(today, -1)) ? run : 0;
    return { current, best, last_date: prev };
}

const TIER_RANK_SQL = "CASE r.tier WHEN 'gold' THEN 3 WHEN 'silver' THEN 2 ELSE 1 END";

/**
 * GET /challenges/me?limit=60: `{ streak: { current, best, last_date }, results: [{
 * challenge_id, date, best_time_ms, tier, status }] }` (most recent first). A result is the
 * best run that was not rejected (its status; its tier is the best one reached that day), or
 * the best rejected run (tier null) when every run was rejected.
 */
export async function mine(ctx: Ctx): Promise<Response> {
    const auth = await requireAuth(ctx);
    const { limit } = paging(ctx.url, 60, 366);
    const me = auth.player.id;
    // The game polls its results: a run held by an outdated board is decided again.
    await recheckHeldRuns(ctx, me, 20);
    const results = await ctx.env.DB.prepare(
        `WITH runs AS (
           SELECT r.challenge_id, r.time_ms, r.status,
                  ROW_NUMBER() OVER (PARTITION BY r.challenge_id
                                     ORDER BY (r.status = 'REJECTED'), r.time_ms, (r.status <> 'VERIFIED'), r.created_at, r.id) AS rn,
                  MAX(CASE WHEN r.status <> 'REJECTED' THEN ${TIER_RANK_SQL} ELSE 0 END) OVER (PARTITION BY r.challenge_id) AS tier_rank
           FROM challenge_runs r WHERE r.player_id = ?1)
         SELECT runs.challenge_id, c.date, runs.time_ms, runs.status, runs.tier_rank
         FROM runs JOIN daily_challenges c ON c.id = runs.challenge_id
         WHERE runs.rn = 1 ORDER BY c.date DESC LIMIT ?2`,
    )
        .bind(me, limit)
        .all<{ challenge_id: string; date: string; time_ms: number; status: RunRow["status"]; tier_rank: number }>();
    const days = await ctx.env.DB.prepare(
        `SELECT DISTINCT c.date FROM challenge_runs r JOIN daily_challenges c ON c.id = r.challenge_id
         WHERE r.player_id = ?1 AND r.status <> 'REJECTED' ORDER BY c.date`,
    )
        .bind(me)
        .all<{ date: string }>();
    return json(ctx, {
        streak: streakOf(
            days.results.map((d) => d.date),
            utcDate(),
        ),
        results: results.results.map((r) => ({
            challenge_id: r.challenge_id,
            date: r.date,
            best_time_ms: r.time_ms,
            tier: r.tier_rank > 0 ? TIERS[r.tier_rank - 1]! : null,
            status: r.status,
        })),
    });
}

// ------------------------------------------------------------------- account

/** R2 keys of a player's challenge replays (account deletion). */
export async function challengeReplayKeys(ctx: Ctx, playerId: string): Promise<string[]> {
    const rows = await ctx.env.DB.prepare("SELECT replay_key FROM challenge_runs WHERE player_id = ?1 AND replay_key IS NOT NULL")
        .bind(playerId)
        .all<{ replay_key: string }>();
    return rows.results.map((r) => r.replay_key);
}

/** Daily maintenance: missing challenges, the creator of the day, runs held for review. */
export async function maintenance(ctx: Ctx): Promise<{ rechecked: number }> {
    await ensureChallenges(ctx);
    return { rechecked: await recheckHeldRuns(ctx, null) };
}
