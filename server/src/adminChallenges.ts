// ADMINISTRATION › Calendrier des défis (2.41). An administrator sees the challenges of a month,
// the coming ones included (no player route ever reveals them), and sets the challenge of a
// period that has not begun (UTC: its first day after today): its level (campaign, or a
// published community version), its rules, its objective and its tiers; or puts back the
// server's draw (the choice goes and the server draws the period again). The checks are those
// of scripts/challenge-set.mjs (src/challengeSpec.mjs), the rules' hash the canonical one
// (shared with the game and its replays: the objective is not part of it). Every change and
// every refusal is written to the Journal (audit_log). The administrator comes from the
// session only (requireAdminRole); a period that has begun never changes (the routes check it,
// the SQL guards each write, and the trigger challenges_frozen backs both).

import { audit, type AuthInfo } from "./auth";
import { requireAdminRole } from "./adminRoles";
import {
    MAX_PRIZES,
    MAX_TOP,
    PRIZE_CONDITIONS,
    PRIZE_KINDS,
    PRIZE_ROWS_SQL,
    TITLE_COLORS,
    TITLE_MAX_CHARS,
    cleanTitleText,
    prizeItem,
    prizesJson,
    creationOfRef,
    isCreationKind,
    titleIdOf,
    titleKey,
    titleRef,
    type PrizeRow,
    type PrizeRule,
} from "./challengeRewards";
import { ChallengeSpecError, MAX_TIER_MS, validateChallengeSpec, type ValidatedChallenge } from "./challengeSpec.mjs";
import {
    AHEAD,
    CATALOG,
    CHALLENGE_SQL,
    KINDS,
    challengeJson,
    drawable,
    ensureChallenges,
    parseChallengeId,
    periodBounds,
    periodOf,
    rulesHash,
    shiftPeriod,
    utcDate,
    type ChallengeRow,
    type Period,
} from "./challenges";
import { normalizeTitle } from "./community";
import { knownCreations, type CreationKind } from "./creations";
import { ApiError, Errors, json, log, nowIso, readJsonObject, type Ctx } from "./http";
import { RULES, rateLimit } from "./ratelimit";
import { Validator, paging, queryEnum, queryString } from "./validate";

const MONTH_RE = /^\d{4}-\d{2}$/;
/** The calendar goes this many months ahead of the current one. */
const MONTHS_AHEAD = 12;

/** A challenge as the calendar shows it: what the game reads, and who drew it. */
function adminChallengeJson(c: ChallengeRow) {
    return { ...challengeJson(c), replaced_title: c.replaced_title ?? null, updated_at: c.updated_at };
}

/** The UTC day a coming period without a challenge gets drawn (it enters the drawn window), null when that day is past. */
function drawsOn(p: Period, today: string): string | null {
    const day = periodBounds(p.kind, shiftPeriod(p.kind, p.period, -AHEAD[p.kind]))!.start;
    return day > today ? day : null;
}

function stateOf(p: Period, today: string): "past" | "running" | "coming" {
    return p.end <= today ? "past" : p.start <= today ? "running" : "coming";
}

/** The periods of a month: the month, the ISO weeks that overlap it, its days. */
function periodsOfMonth(month: Period): Period[] {
    const out: Period[] = [month];
    let week = periodBounds("weekly", periodOf("weekly", month.start))!;
    while (week.start < month.end) {
        out.push(week);
        week = periodBounds("weekly", shiftPeriod("weekly", week.period, 1))!;
    }
    for (let d = month.start; d < month.end; d = periodBounds("daily", d)!.end) out.push(periodBounds("daily", d)!);
    return out;
}

/**
 * GET /admin/challenges?month=AAAA-MM (default: the current month): `{ month, today,
 * server_time, first_month, last_month, ahead, periods }`. `periods`: the month, the weeks
 * that overlap it and its days, each `{ id, kind, period, date, end_date, starts_at, ends_at,
 * state: past|running|coming, editable, draws_on, challenge }` (challenge null when there is
 * none: a coming period not drawn yet says when it will be, `draws_on`). The periods the
 * server draws ahead are drawn first, so that the coming draws are those players will get.
 */
export async function listChallenges(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const today = utcDate();
    const month = queryString(ctx.url, "month", MONTH_RE) ?? periodOf("monthly", today);
    const bounds = periodBounds("monthly", month);
    if (!bounds) throw Errors.validation([{ field: "month", message: "mois AAAA-MM attendu" }]);
    try {
        await ensureChallenges(ctx);
    } catch (e) {
        // The calendar still shows what exists.
        log("error", "challenge_ensure_failed", { request_id: ctx.requestId, error: e instanceof Error ? e.message : String(e) });
    }
    const rows = await ctx.env.DB.prepare(`${CHALLENGE_SQL} WHERE c.date < ?2 AND c.end_date > ?1`).bind(bounds.start, bounds.end).all<ChallengeRow>();
    const byId = new Map(rows.results.map((c) => [c.id, c]));
    const monthPeriods = periodsOfMonth(bounds);
    // 2.43: the rewards of each period (set even before the server draws it) and when they were granted.
    const ids = JSON.stringify(monthPeriods.map((p) => `${p.kind}-${p.period}`));
    const [prizeRows, grants] = await Promise.all([
        ctx.env.DB.prepare(`${PRIZE_ROWS_SQL} WHERE r.challenge_id IN (SELECT value FROM json_each(?1)) ORDER BY r.challenge_id, r.position`)
            .bind(ids)
            .all<PrizeRow & { challenge_id: string }>(),
        ctx.env.DB.prepare("SELECT challenge_id, granted_at FROM challenge_reward_grants WHERE challenge_id IN (SELECT value FROM json_each(?1))")
            .bind(ids)
            .all<{ challenge_id: string; granted_at: string }>(),
    ]);
    const prizesOf = new Map<string, PrizeRow[]>();
    for (const r of prizeRows.results) prizesOf.set(r.challenge_id, [...(prizesOf.get(r.challenge_id) ?? []), r]);
    const grantedOf = new Map(grants.results.map((g) => [g.challenge_id, g.granted_at]));
    const first = await ctx.env.DB.prepare("SELECT MIN(date) AS d FROM challenges").first<{ d: string | null }>();
    const current = periodOf("monthly", today);
    const periods = monthPeriods.map((p) => {
        const id = `${p.kind}-${p.period}`;
        const c = byId.get(id);
        const state = stateOf(p, today);
        return {
            id,
            kind: p.kind,
            period: p.period,
            date: p.start,
            end_date: p.end,
            starts_at: `${p.start}T00:00:00Z`,
            ends_at: `${p.end}T00:00:00Z`,
            state,
            editable: state === "coming",
            draws_on: !c && state === "coming" ? drawsOn(p, today) : null,
            challenge: c ? adminChallengeJson(c) : null,
            rewards: prizesJson(prizesOf.get(id) ?? []),
            rewards_granted_at: grantedOf.get(id) ?? null,
        };
    });
    const firstMonth = first?.d ? first.d.slice(0, 7) : current;
    return json(ctx, {
        month,
        today,
        server_time: new Date().toISOString(),
        first_month: firstMonth < current ? firstMonth : current,
        last_month: shiftPeriod("monthly", current, MONTHS_AHEAD),
        ahead: AHEAD,
        periods,
    });
}

interface CampaignLevelRow {
    id: string;
    current_version: number;
    ranking_enabled: number;
    compatibility_status: string | null;
    has_race: number | null;
}

interface CommunityLevelRow {
    id: string;
    title: string;
    planet: string;
    difficulty: number | null;
    current_version: number;
    author_id: string;
    username: string;
    level_hash: string;
    completion_ms: number | null;
    has_race: number;
    extreme: number;
}

/** Lower case without accents, for the search of the campaign's levels. */
const folded = (s: string) =>
    s
        .normalize("NFD")
        .replace(/[̀-ͯ]/g, "")
        .toLowerCase();

/** Times proposed for a community level (« Vitesse »): from its author's completion time. */
function suggestedTimes(completionMs: number | null): { silver_ms: number; gold_ms: number } | null {
    if (!completionMs || completionMs <= 0) return null;
    const round = (ms: number) => Math.min(MAX_TIER_MS, Math.max(1000, Math.ceil(ms / 1000) * 1000));
    const silver = round(completionMs * 1.5);
    return { silver_ms: silver, gold_ms: Math.min(silver, round(completionMs * 1.15)) };
}

/**
 * GET /admin/challenges/levels?source=campaign|community&q=&limit=&offset=: the levels an
 * administrator may choose. campaign: every level of the catalogue (src/challengeCatalog.json),
 * with the kinds the server's draw may use it for (`draw_kinds`), whether the server ranks its
 * current revision (`available`), its route (`has_race`, needed by « Finir le niveau »), its
 * slots, powers and the times of its medals (the default tiers). community: the published
 * levels (title search from 2 characters, most played first, paged), at their current version.
 * `player_max_hearts` (campaign): the hearts of a challenge when none are given.
 */
export async function listLevels(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const source = queryEnum(ctx.url, "source", ["campaign", "community"] as const, "campaign");
    const q = (ctx.url.searchParams.get("q") ?? "").trim().slice(0, 60);
    if (source === "campaign") {
        const rows = await ctx.env.DB.prepare(
            `SELECT l.id, l.current_version, l.ranking_enabled, lv.compatibility_status, (lv.race IS NOT NULL) AS has_race
             FROM levels l LEFT JOIN level_versions lv ON lv.level_id = l.id AND lv.content_hash = l.content_hash`,
        ).all<CampaignLevelRow>();
        const server = new Map(rows.results.map((r) => [r.id, r]));
        const wanted = folded(q);
        const levels = Object.entries(CATALOG.levels)
            .filter(([id, info]) => !wanted || folded(info.name).includes(wanted) || id.includes(wanted))
            .map(([id, info]) => {
                const r = server.get(id);
                return {
                    source: "campaign" as const,
                    level_id: id,
                    title: info.name,
                    planet: info.planet,
                    order: info.order,
                    last: info.last,
                    version: r?.current_version ?? info.revision,
                    available: r !== undefined && r.ranking_enabled === 1 && r.compatibility_status === "active",
                    has_race: r?.has_race === 1,
                    draw_kinds: KINDS.filter((k) => drawable(k, info)),
                    slots: info.slots,
                    start_powers: info.start_powers,
                    level_powers: info.level_powers,
                    silver_ms: info.silver_ms,
                    gold_ms: Math.min(info.gold_ms, info.silver_ms),
                };
            });
        return json(ctx, { source, levels, total: levels.length, has_more: false, offset: 0, player_max_hearts: CATALOG.player_max_hearts });
    }
    const page = paging(ctx.url, 20, 50);
    const binds: unknown[] = [];
    let cond = "";
    if (q.length >= 2) {
        binds.push("%" + normalizeTitle(q).replace(/[\\%_]/g, (c) => "\\" + c) + "%");
        cond = ` AND l.title_norm LIKE ?${binds.length} ESCAPE '\\'`;
    }
    const from = `FROM community_levels l JOIN players p ON p.id = l.author_id
         JOIN community_level_versions v ON v.level_id = l.id AND v.version = l.current_version
         WHERE l.status = 'published' AND v.published_at IS NOT NULL AND v.status IN ('published', 'superseded')${cond}`;
    binds.push(page.limit + 1, page.offset);
    const [rows, count] = await Promise.all([
        ctx.env.DB.prepare(
            `SELECT l.id, l.title, l.planet, l.difficulty, l.current_version, l.author_id, p.username, v.level_hash, v.completion_ms,
                    (v.race IS NOT NULL) AS has_race, l.extreme
             ${from}
             ORDER BY (l.downloads + 3 * l.completions + 2 * l.rating_sum + 2 * l.favorite_count + l.comment_count) DESC, l.updated_at DESC, l.id
             LIMIT ?${binds.length - 1} OFFSET ?${binds.length}`,
        )
            .bind(...binds)
            .all<CommunityLevelRow>(),
        ctx.env.DB.prepare(`SELECT COUNT(*) AS n ${from}`)
            .bind(...binds.slice(0, -2))
            .first<{ n: number }>(),
    ]);
    const levels = rows.results.slice(0, page.limit).map((l) => ({
        source: "community" as const,
        level_id: l.id,
        version: l.current_version,
        level_hash: l.level_hash,
        title: l.title,
        author: { id: l.author_id, username: l.username },
        planet: l.planet,
        difficulty: l.difficulty,
        extreme: l.extreme === 1,
        has_race: l.has_race === 1,
        completion_ms: l.completion_ms,
        suggested: suggestedTimes(l.completion_ms),
    }));
    return json(ctx, { source, levels, total: count?.n ?? 0, has_more: rows.results.length > page.limit, offset: page.offset });
}

/** What the Journal says of a period. */
function periodDetails(p: Period) {
    return { challenge: `${p.kind}-${p.period}`, kind: p.kind, period: p.period, start: p.start };
}

/** A refusal: written to the Journal (admin_refused, with its reason), then answered. */
async function refuse(ctx: Ctx, auth: AuthInfo, error: ApiError, details: Record<string, unknown>): Promise<never> {
    await audit(ctx, auth.player.id, "admin_refused", { ...details, reason: error.code, by_username: auth.player.username });
    throw error;
}

const STARTED = () => Errors.conflict("CHALLENGE_STARTED", "Ce défi a déjà commencé : il ne change plus.");

/** The challenge's period; a malformed id is « not found ». */
function periodOfId(ctx: Ctx): Period {
    const p = parseChallengeId(ctx.params.challengeId ?? "");
    if (!p) throw Errors.notFound("CHALLENGE_NOT_FOUND", "Défi introuvable.");
    return p;
}

interface ChosenLevel {
    level_id: string;
    version: number;
    hash: string;
    title: string;
    author_id: string | null;
    author_name: string | null;
    has_race: boolean;
}

/** The level version as the server knows it: a ranked campaign revision, or a published community version. */
async function chosenLevel(ctx: Ctx, l: ValidatedChallenge["level"]): Promise<ChosenLevel | null> {
    if (l.source === "campaign") {
        const r = await ctx.env.DB.prepare(
            `SELECT l.id, l.name, l.current_version, l.content_hash, (lv.race IS NOT NULL) AS has_race
             FROM levels l JOIN level_versions lv ON lv.level_id = l.id AND lv.content_hash = l.content_hash
             WHERE l.id = ?1 AND l.ranking_enabled = 1 AND lv.compatibility_status = 'active'`,
        )
            .bind(l.id)
            .first<{ id: string; name: string; current_version: number; content_hash: string; has_race: number }>();
        return r
            ? { level_id: r.id, version: r.current_version, hash: r.content_hash, title: r.name, author_id: null, author_name: null, has_race: r.has_race === 1 }
            : null;
    }
    const r = await ctx.env.DB.prepare(
        `SELECT v.level_id, v.version, v.level_hash, l.title, l.author_id, p.username, (v.race IS NOT NULL) AS has_race
         FROM community_level_versions v JOIN community_levels l ON l.id = v.level_id JOIN players p ON p.id = l.author_id
         WHERE v.level_id = ?1 AND v.version = ?2 AND v.published_at IS NOT NULL AND v.status IN ('published', 'superseded') AND l.status = 'published'`,
    )
        .bind(l.id, l.version)
        .first<{ level_id: string; version: number; level_hash: string; title: string; author_id: string; username: string; has_race: number }>();
    return r
        ? {
              level_id: r.level_id,
              version: r.version,
              hash: r.level_hash,
              title: r.title,
              author_id: r.author_id,
              author_name: r.username,
              has_race: r.has_race === 1,
          }
        : null;
}

const SPEC_FIELDS = [
    "level",
    "hearts",
    "slots",
    "powers",
    "stations",
    "checkpoints",
    "objective",
    "silver_ms",
    "gold_ms",
    "gold_no_damage",
    "bronze_progress",
    "silver_progress",
] as const;

/** The body's shape (types and bounds); the rules themselves are checked by validateChallengeSpec. */
function readSpecBody(body: Record<string, unknown>, v: Validator) {
    const raw = body.level;
    let level: { source: string; id: string; version?: number } = { source: "", id: "" };
    if (typeof raw !== "object" || raw === null || Array.isArray(raw)) v.fail("level", "objet { source, id, version } attendu");
    else {
        const lv = new Validator(raw as Record<string, unknown>, ["source", "id", "version"]);
        const source = lv.oneOf("source", ["campaign", "community"] as const);
        const id = lv.str("id", { min: 1, max: 64 });
        const version = lv.int("version", { min: 1, max: 100000, optional: source !== "community" });
        for (const e of lv.errors) v.fail(`level.${e.field}`, e.message);
        level = { source: source ?? "", id: id ?? "", ...(version !== undefined ? { version } : {}) };
    }
    let powers: string[] | undefined;
    if (body.powers !== undefined && body.powers !== null) {
        if (!Array.isArray(body.powers) || body.powers.length > 16 || !body.powers.every((p) => typeof p === "string" && p.length <= 64))
            v.fail("powers", "liste de 16 identifiants de pouvoirs au plus attendue");
        else powers = body.powers as string[];
    }
    return {
        level,
        hearts: v.int("hearts", { min: 1, max: 20, optional: true }),
        slots: v.int("slots", { min: 1, max: 8, optional: true }),
        powers,
        stations: v.bool("stations", { optional: true }),
        checkpoints: v.bool("checkpoints", { optional: true }),
        objective: v.oneOf("objective", ["speed", "finish"] as const, { optional: true }),
        silver_ms: v.int("silver_ms", { min: 1, max: 24 * 3600 * 1000, optional: true }),
        gold_ms: v.int("gold_ms", { min: 1, max: 24 * 3600 * 1000, optional: true }),
        gold_no_damage: v.bool("gold_no_damage", { optional: true }),
        bronze_progress: v.int("bronze_progress", { min: 0, max: 100, optional: true }),
        silver_progress: v.int("silver_progress", { min: 0, max: 100, optional: true }),
    };
}

/** What the Journal keeps of a challenge (its sentence: level, rules, objective, tiers). */
function challengeSummary(c: ChallengeRow) {
    let powers: string[] = [];
    try {
        const v = JSON.parse(c.powers) as unknown;
        if (Array.isArray(v)) powers = v.filter((p): p is string => typeof p === "string");
    } catch {
        // kept empty
    }
    return {
        source: c.source,
        level: { source: c.level_source, id: c.level_id, version: c.level_version, title: c.level_title },
        rules: { hearts: c.hearts, slots: c.slots, powers, stations: c.stations === 1, checkpoints: c.checkpoints === 1 },
        objective: c.objective ?? "speed",
        tiers: {
            silver_ms: c.silver_ms,
            gold_ms: c.gold_ms,
            gold_no_damage: c.gold_no_damage === 1,
            bronze_progress: c.bronze_progress,
            silver_progress: c.silver_progress,
        },
    };
}

const COLUMNS = [
    "id",
    "kind",
    "period",
    "date",
    "end_date",
    "source",
    "level_source",
    "level_id",
    "level_version",
    "level_hash",
    "level_title",
    "hearts",
    "slots",
    "powers",
    "stations",
    "checkpoints",
    "rules_hash",
    "silver_ms",
    "gold_ms",
    "gold_no_damage",
    "objective",
    "bronze_progress",
    "silver_progress",
    "replaced_title",
    "featured_player_id",
    "featured_notified_at",
    "created_at",
    "updated_at",
] as const;
/** Changed when an existing (coming) challenge is set again; its id, period and creation stay. */
const UPDATED = COLUMNS.filter((c) => !["id", "kind", "period", "date", "end_date", "created_at"].includes(c));

/**
 * PUT /admin/challenges/{challengeId} `{ level: { source, id, version? }, hearts?, slots?,
 * powers?, stations?, checkpoints?, objective?, silver_ms?, gold_ms?, gold_no_damage?,
 * bronze_progress?, silver_progress? }`: the challenge of a period that has not begun, chosen
 * by an administrator (source 'admin'). Defaults as scripts/challenge-set.mjs. Refused, and
 * written to the Journal: a period that has begun (409 CHALLENGE_STARTED), invalid rules (400),
 * a level the server does not rank or a version not published (409 LEVEL_UNAVAILABLE),
 * « Finir le niveau » without a route (409 FINISH_NEEDS_ROUTE). Answers `{ challenge,
 * warnings }`, the challenge as the calendar shows it.
 */
export async function setChallenge(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const p = periodOfId(ctx);
    const what = { ...periodDetails(p), action: "set" };
    const today = utcDate();
    if (p.start <= today) await refuse(ctx, auth, STARTED(), what);
    const body = await readJsonObject(ctx, 4 * 1024);
    const v = new Validator(body, SPEC_FIELDS);
    const input = readSpecBody(body, v);
    if (v.errors.length > 0) await refuse(ctx, auth, Errors.validation(v.errors), { ...what, field: v.errors[0]!.field });
    let spec: ValidatedChallenge;
    try {
        spec = validateChallengeSpec({ kind: p.kind, period: p.period, ...input }, CATALOG, today);
    } catch (e) {
        if (!(e instanceof ChallengeSpecError)) throw e;
        return refuse(ctx, auth, Errors.validation([{ field: e.field, message: e.message }]), { ...what, field: e.field, message: e.message });
    }
    const level = await chosenLevel(ctx, spec.level);
    if (!level)
        await refuse(
            ctx,
            auth,
            Errors.conflict(
                "LEVEL_UNAVAILABLE",
                spec.level.source === "campaign"
                    ? "Ce niveau n'est pas classé par le serveur : il ne peut pas être un défi."
                    : "Cette version du niveau n'est pas publiée : elle ne peut pas être un défi.",
            ),
            { ...what, level: spec.level },
        );
    if (spec.objective === "finish" && !level!.has_race)
        await refuse(
            ctx,
            auth,
            Errors.conflict("FINISH_NEEDS_ROUTE", "« Finir le niveau » demande un niveau dont le parcours est tracé (comme les niveaux EXTRÊME)."),
            { ...what, level: { ...spec.level, title: level!.title } },
        );
    const lv = level!;
    const db = ctx.env.DB;
    const previous = await db.prepare(`${CHALLENGE_SQL} WHERE c.id = ?1`).bind(spec.id).first<ChallengeRow>();
    const hash = await rulesHash(spec.id, spec.rules);
    const now = nowIso();
    const replaced = previous ? (previous.source === "auto" ? previous.level_title : previous.replaced_title) : null;
    const values: Record<(typeof COLUMNS)[number], unknown> = {
        id: spec.id,
        kind: spec.kind,
        period: spec.period,
        date: spec.start,
        end_date: spec.end,
        source: "admin",
        level_source: spec.level.source,
        level_id: lv.level_id,
        level_version: lv.version,
        level_hash: lv.hash,
        level_title: lv.title,
        hearts: spec.rules.hearts,
        slots: spec.rules.slots,
        powers: JSON.stringify(spec.rules.powers),
        stations: spec.rules.stations ? 1 : 0,
        checkpoints: spec.rules.checkpoints ? 1 : 0,
        rules_hash: hash,
        silver_ms: spec.silver_ms,
        gold_ms: spec.gold_ms,
        gold_no_damage: spec.gold_no_damage ? 1 : 0,
        objective: spec.objective,
        bronze_progress: spec.bronze_progress,
        silver_progress: spec.silver_progress,
        replaced_title: replaced,
        featured_player_id: lv.author_id,
        featured_notified_at: null,
        created_at: now,
        updated_at: now,
    };
    const n = COLUMNS.length;
    // Only while the period has not begun (?n+1: its first day), on the insert and on the update.
    const res = await db
        .prepare(
            `INSERT INTO challenges (${COLUMNS.join(", ")}) SELECT ${COLUMNS.map((_c, i) => `?${i + 1}`).join(", ")} WHERE ?${n + 1} > date('now')
             ON CONFLICT (id) DO UPDATE SET ${UPDATED.map((c) => `${c} = excluded.${c}`).join(", ")} WHERE challenges.date > date('now')`,
        )
        .bind(...COLUMNS.map((c) => values[c]), spec.start)
        .run();
    if (!res.meta.changes) await refuse(ctx, auth, STARTED(), what);
    const row = (await db.prepare(`${CHALLENGE_SQL} WHERE c.id = ?1`).bind(spec.id).first<ChallengeRow>())!;
    await audit(ctx, auth.player.id, "admin_challenge_set", {
        ...periodDetails(p),
        ...challengeSummary(row),
        replaced: previous ? { source: previous.source, title: previous.level_title } : null,
        by_username: auth.player.username,
    });
    return json(ctx, { challenge: adminChallengeJson(row), warnings: spec.warnings });
}

/**
 * DELETE /admin/challenges/{challengeId}: « Revenir au tirage ». The administrator's choice of
 * a period that has not begun goes, and the server draws the period again at once when it is
 * within the days, weeks and months it draws ahead (else on the day `draws_on`). Answers `{
 * challenge, draws_on }` (the new draw, or null). Refused and written to the Journal: a period
 * that has begun, a challenge that is already the server's draw.
 */
export async function resetChallenge(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const p = periodOfId(ctx);
    const what = { ...periodDetails(p), action: "reset" };
    const today = utcDate();
    if (p.start <= today) await refuse(ctx, auth, STARTED(), what);
    const db = ctx.env.DB;
    const chosen = await db.prepare(`${CHALLENGE_SQL} WHERE c.id = ?1`).bind(`${p.kind}-${p.period}`).first<ChallengeRow>();
    if (!chosen || chosen.source !== "admin")
        await refuse(ctx, auth, Errors.conflict("NOT_CHOSEN", "Ce défi est déjà celui du tirage du serveur."), what);
    const res = await db
        .prepare("DELETE FROM challenges WHERE id = ?1 AND source = 'admin' AND date > date('now')")
        .bind(chosen!.id)
        .run();
    if (!res.meta.changes) await refuse(ctx, auth, STARTED(), what);
    try {
        await ensureChallenges(ctx);
    } catch (e) {
        log("error", "challenge_ensure_failed", { request_id: ctx.requestId, error: e instanceof Error ? e.message : String(e) });
    }
    const drawn = await db.prepare(`${CHALLENGE_SQL} WHERE c.id = ?1`).bind(chosen!.id).first<ChallengeRow>();
    const when = drawn ? null : drawsOn(p, today);
    await audit(ctx, auth.player.id, "admin_challenge_reset", {
        ...periodDetails(p),
        removed: challengeSummary(chosen!),
        drawn: drawn ? { title: drawn.level_title, level: { source: drawn.level_source, id: drawn.level_id } } : null,
        draws_on: when,
        by_username: auth.player.username,
    });
    return json(ctx, { challenge: drawn ? adminChallengeJson(drawn) : null, draws_on: when });
}

// ------------------------------------------------- rewards of a challenge (2.43)

/** A server title as the administration lists it. */
interface TitleRow {
    id: number;
    text: string;
    color: string;
    created_at: string;
    created_by_name: string | null;
    uses: number;
}

function titleJson(t: Pick<TitleRow, "id" | "text" | "color" | "created_at"> & Partial<TitleRow>) {
    return {
        id: titleRef(t.id),
        text: t.text,
        color: t.color,
        created_at: t.created_at,
        created_by: t.created_by_name ?? null,
        uses: t.uses ?? 0,
    };
}

/**
 * GET /admin/titles: `{ titles: [{ id: "srv:<n>", text, color, created_at, created_by, uses }],
 * max_chars, colors }`: the titles created on the server, the newest first (`uses`: the
 * challenges that give it).
 */
export async function listTitles(ctx: Ctx): Promise<Response> {
    await requireAdminRole(ctx);
    const rows = await ctx.env.DB.prepare(
        `SELECT t.id, t.text, t.color, t.created_at, p.username AS created_by_name,
                (SELECT COUNT(DISTINCT r.challenge_id) FROM challenge_reward_rules r WHERE r.reward_kind = 'title' AND r.reward_ref = 'srv:' || t.id) AS uses
         FROM server_titles t LEFT JOIN players p ON p.id = t.created_by ORDER BY t.id DESC LIMIT 500`,
    ).all<TitleRow>();
    return json(ctx, { titles: rows.results.map(titleJson), max_chars: TITLE_MAX_CHARS, colors: TITLE_COLORS });
}

/**
 * POST /admin/titles `{ text, color }`: a title created on the server (worn as "srv:<id>", no
 * game update needed). The text: trimmed, inner spaces collapsed, 1 to 32 characters, letters,
 * digits, punctuation and spaces only; the same text (accents and case aside) is refused (409
 * TITLE_EXISTS, `details.title` the existing one). Written to the Journal, refusals too.
 * Answers 201 `{ title }`.
 */
export async function createTitle(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const body = await readJsonObject(ctx, 1024);
    const v = new Validator(body, ["text", "color"]);
    const raw = v.str("text", { min: 1, max: 128, trim: true });
    const color = v.oneOf("color", TITLE_COLORS);
    const text = raw === undefined ? null : cleanTitleText(raw);
    if (raw !== undefined && !text) v.fail("text", `de 1 à ${TITLE_MAX_CHARS} caractères : lettres, chiffres, ponctuation et espaces`);
    const what = { action: "title", title: { text: (raw ?? "").slice(0, 64), color: color ?? null } };
    if (v.errors.length > 0 || !text) await refuse(ctx, auth, Errors.validation(v.errors), { ...what, field: v.errors[0]?.field ?? "text" });
    const key = titleKey(text!);
    const row = await ctx.env.DB.prepare(
        `INSERT INTO server_titles (text, text_key, color, created_by, created_at) VALUES (?1, ?2, ?3, ?4, ?5)
         ON CONFLICT (text_key) DO NOTHING RETURNING id, text, color, created_at`,
    )
        .bind(text, key, color, auth.player.id, nowIso())
        .first<TitleRow>();
    if (!row) {
        const existing = await ctx.env.DB.prepare("SELECT id, text, color, created_at FROM server_titles WHERE text_key = ?1").bind(key).first<TitleRow>();
        return refuse(
            ctx,
            auth,
            Errors.conflict("TITLE_EXISTS", "Ce titre existe déjà : choisissez-le dans la liste des titres.", existing ? { title: titleJson(existing) } : undefined),
            what,
        );
    }
    const title = titleJson({ ...row, created_by_name: auth.player.username });
    await audit(ctx, auth.player.id, "admin_title_created", { title: { id: title.id, text: title.text, color: title.color }, by_username: auth.player.username });
    return json(ctx, { title }, { status: 201 });
}

type FieldErrors = { field: string; message: string }[];

/** The words of the refusals about a creation of each kind (« combinaison inconnue du serveur »...). */
const CREATION_WORDS: Record<CreationKind, { one: string; expected: string; unknown: string; archived: string; it: string }> = {
    suit: { one: "combinaison", expected: "attendue", unknown: "inconnue", archived: "archivée", it: "la" },
    trail: { one: "traînée", expected: "attendue", unknown: "inconnue", archived: "archivée", it: "la" },
    frame: { one: "cadre", expected: "attendu", unknown: "inconnu", archived: "archivé", it: "le" },
    victory: { one: "effet de victoire", expected: "attendu", unknown: "inconnu", archived: "archivé", it: "le" },
};

/**
 * The body's rewards, checked: kinds, references the server knows, conditions and their
 * values, no reward twice. A creation of the server (a suit; 2.59, a trail, a frame, a victory
 * effect) must exist with that kind and not be archived, unless the challenge `challengeId`
 * already gives it (archived since: kept as it was).
 */
async function readPrizes(ctx: Ctx, body: Record<string, unknown>, challengeId: string): Promise<{ rules: PrizeRule[]; errors: FieldErrors }> {
    const errors: FieldErrors = [...new Validator(body, ["rewards"]).errors];
    const rules: PrizeRule[] = [];
    const list = body.rewards;
    if (!Array.isArray(list) || list.length > MAX_PRIZES) {
        errors.push({ field: "rewards", message: `liste de ${MAX_PRIZES} récompenses au plus attendue` });
        return { rules, errors };
    }
    const titleIds = new Set<number>();
    const creationIds = new Map<CreationKind, Set<number>>();
    list.forEach((raw: unknown, i) => {
        const field = `rewards[${i}]`;
        if (typeof raw !== "object" || raw === null || Array.isArray(raw)) {
            errors.push({ field, message: "objet { kind, ref, condition, value } attendu" });
            return;
        }
        const rv = new Validator(raw as Record<string, unknown>, ["kind", "ref", "condition", "value"]);
        const kind = rv.oneOf("kind", PRIZE_KINDS);
        const ref = rv.str("ref", { min: 1, max: 64 });
        const condition = rv.oneOf("condition", PRIZE_CONDITIONS);
        const max = condition === "tier" ? 3 : condition === "top" ? MAX_TOP : 0;
        const value = rv.int("value", { min: condition === "finished" ? 0 : 1, max, optional: condition === "finished" }) ?? 0;
        for (const e of rv.errors) errors.push({ field: `${field}.${e.field}`, message: e.message });
        if (rv.errors.length > 0 || !kind || !ref || !condition) return;
        if (kind === "item" && !prizeItem(ref)) {
            errors.push({ field: `${field}.ref`, message: "élément inconnu du catalogue (ou élément de départ)" });
            return;
        }
        if (kind === "title") {
            const id = titleIdOf(ref);
            if (id === null) {
                errors.push({ field: `${field}.ref`, message: "titre « srv:<n> » attendu" });
                return;
            }
            titleIds.add(id);
        }
        if (isCreationKind(kind)) {
            const c = creationOfRef(ref);
            if (c === null || c.kind !== kind) {
                errors.push({ field: `${field}.ref`, message: `${CREATION_WORDS[kind].one} « srv${kind}:<n> » ${CREATION_WORDS[kind].expected}` });
                return;
            }
            if (!creationIds.has(kind)) creationIds.set(kind, new Set());
            creationIds.get(kind)!.add(c.id);
        }
        if (rules.some((r) => r.kind === kind && r.ref === ref)) {
            errors.push({ field: `${field}.ref`, message: "cette récompense est déjà dans la liste" });
            return;
        }
        rules.push({ kind, ref, condition, value });
    });
    if (titleIds.size > 0 && errors.length === 0) {
        const known = await ctx.env.DB.prepare("SELECT id FROM server_titles WHERE id IN (SELECT value FROM json_each(?1))")
            .bind(JSON.stringify([...titleIds]))
            .all<{ id: number }>();
        const have = new Set(known.results.map((r) => r.id));
        rules.forEach((r, i) => {
            if (r.kind === "title" && !have.has(titleIdOf(r.ref)!)) errors.push({ field: `rewards[${i}].ref`, message: "titre inconnu du serveur" });
        });
    }
    if (creationIds.size > 0 && errors.length === 0) {
        const known = new Map<CreationKind, Map<number, { archived: boolean }>>();
        for (const [kind, ids] of creationIds) known.set(kind, await knownCreations(ctx, kind, [...ids]));
        const given = await ctx.env.DB.prepare("SELECT reward_kind AS kind, reward_ref AS ref FROM challenge_reward_rules WHERE challenge_id = ?1")
            .bind(challengeId)
            .all<{ kind: string; ref: string }>();
        const already = new Set(given.results.map((r) => `${r.kind} ${r.ref}`));
        rules.forEach((r, i) => {
            if (!isCreationKind(r.kind)) return;
            const s = known.get(r.kind)?.get(creationOfRef(r.ref)!.id);
            const w = CREATION_WORDS[r.kind];
            if (!s) errors.push({ field: `rewards[${i}].ref`, message: `${w.one} ${w.unknown} du serveur` });
            else if (s.archived && !already.has(`${r.kind} ${r.ref}`)) errors.push({ field: `rewards[${i}].ref`, message: `${w.one} ${w.archived} : restaurez-${w.it} d'abord` });
        });
    }
    return { rules, errors };
}

/**
 * PUT /admin/challenges/{challengeId}/rewards `{ rewards: [{ kind: item|title|suit|trail|frame|victory, ref, condition:
 * tier|top|finished, value }] }` (at most MAX_PRIZES, in their order; [] removes them): the
 * rewards of a period that has not begun, set even before the server draws its challenge
 * (they stay with the period through « Revenir au tirage »). value: tier 1 Bronze, 2 Argent,
 * 3 Or (that tier or better); top 1..100; finished: none. Refused, and written to the
 * Journal: a period that has begun (409 CHALLENGE_STARTED, also guarded by each write),
 * invalid rewards (400). Answers `{ rewards }`, as the calendar shows them.
 */
export async function setRewards(ctx: Ctx): Promise<Response> {
    const auth = await requireAdminRole(ctx);
    await rateLimit(ctx, RULES.adminWrite, "player:" + auth.player.id);
    const p = periodOfId(ctx);
    const what = { ...periodDetails(p), action: "rewards" };
    if (p.start <= utcDate()) await refuse(ctx, auth, STARTED(), what);
    const body = await readJsonObject(ctx, 4 * 1024);
    const id = `${p.kind}-${p.period}`;
    const { rules, errors } = await readPrizes(ctx, body, id);
    if (errors.length > 0) await refuse(ctx, auth, Errors.validation(errors), { ...what, field: errors[0]!.field });
    const db = ctx.env.DB;
    const before = await db.prepare(`${PRIZE_ROWS_SQL} WHERE r.challenge_id = ?1 ORDER BY r.position`).bind(id).all<PrizeRow>();
    // Each write only while the period has not begun (?2: its first day); the last statement says whether it had not.
    const res = await db.batch([
        db.prepare("DELETE FROM challenge_reward_rules WHERE challenge_id = ?1 AND ?2 > date('now')").bind(id, p.start),
        ...rules.map((r, i) =>
            db
                .prepare(
                    `INSERT INTO challenge_reward_rules (challenge_id, position, reward_kind, reward_ref, condition, condition_value)
                     SELECT ?1, ?3, ?4, ?5, ?6, ?7 WHERE ?2 > date('now')`,
                )
                .bind(id, p.start, i, r.kind, r.ref, r.condition, r.value),
        ),
        db.prepare("SELECT ?1 > date('now') AS open").bind(p.start),
    ]);
    const open = (res[res.length - 1]!.results as { open: number }[])[0]?.open === 1;
    if (!open) await refuse(ctx, auth, STARTED(), what);
    const after = await db.prepare(`${PRIZE_ROWS_SQL} WHERE r.challenge_id = ?1 ORDER BY r.position`).bind(id).all<PrizeRow>();
    const summary = (rows: PrizeRow[]) =>
        prizesJson(rows).map((r) => ({
            kind: r.kind,
            ref: r.ref,
            name: r.item?.name ?? r.title?.text ?? r.suit?.name ?? r.creation?.name ?? r.ref,
            category: r.item?.category ?? (isCreationKind(r.kind) ? r.kind : "title"),
            color: r.title?.color ?? null,
            condition: r.condition,
            value: r.value,
        }));
    await audit(ctx, auth.player.id, "admin_challenge_rewards", {
        ...periodDetails(p),
        rewards: summary(after.results),
        previous: summary(before.results),
        by_username: auth.player.username,
    });
    return json(ctx, { rewards: prizesJson(after.results) });
}
