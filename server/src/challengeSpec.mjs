// An administrator's challenge — of a day, of an ISO week or of a month —: the pure checks of
// what may be chosen (2.41), shared by the routes of ADMINISTRATION › Calendrier des défis
// (src/adminChallenges.ts) and by the command line (scripts/challenge-sql.mjs). No file, no
// network, no database: whether the level version is published, ranked or has a race layout
// is the caller's to check. Web Crypto only (Node 18+ and Workers).

export const MAX_SLOTS = 8;
export const MAX_HEARTS = 20;
export const KINDS = ["daily", "weekly", "monthly"];
/** speed: the best times (tiers in time); finish: the progress, like « Plus loin » (tiers in % of the route). */
export const OBJECTIVES = ["speed", "finish"];
/** « Finir le niveau »: Bronze and Argent ask for this share of the route (in %), Or for the finish. */
export const PROGRESS_MIN = 10;
export const PROGRESS_MAX = 95;
export const DEFAULT_BRONZE_PROGRESS = 50;
export const DEFAULT_SILVER_PROGRESS = 80;
/** Longest time of a tier the game reads (its 2.32 check). */
export const MAX_TIER_MS = 2 * 3600 * 1000;
/**
 * The times a « Finir le niveau » challenge keeps for the older games, which only know time
 * tiers: every finished run is within them, so they show what the server decides for a
 * finished run (Or, or Argent with damage when Or asks for none).
 */
export const FINISH_TIER_MS = MAX_TIER_MS;

const DATE_RE = /^\d{4}-\d{2}-\d{2}$/;
const WEEK_RE = /^(\d{4})-W(\d{2})$/;
const MONTH_RE = /^(\d{4})-(\d{2})$/;
const DAY_MS = 86400000;
const UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const LEVEL_ID_RE = /^[a-z0-9_]{1,64}$/;
const POWER_RE = /^[a-z0-9_]{1,64}$/;

/** A refused choice: `field` names what is wrong (the API's field, the script's option). */
export class ChallengeSpecError extends Error {
    constructor(field, message) {
        super(message);
        this.name = "ChallengeSpecError";
        this.field = field;
    }
}

/** Canonical form of the rules (docs/online/DAILY_CHALLENGES.md), as src/challenges.ts. */
export function canonicalRules(challengeId, r) {
    const powers = [...r.powers].sort();
    return (
        `{"challenge":${JSON.stringify(challengeId)},"hearts":${r.hearts},"slots":${r.slots},` +
        `"powers":${JSON.stringify(powers)},"stations":${r.stations},"checkpoints":${r.checkpoints}}`
    );
}

export async function sha256Hex(text) {
    const d = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text));
    return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

export function isDate(s) {
    if (typeof s !== "string" || !DATE_RE.test(s)) return false;
    const t = Date.parse(`${s}T00:00:00Z`);
    return !Number.isNaN(t) && new Date(t).toISOString().slice(0, 10) === s;
}

const day = (ms) => new Date(ms).toISOString().slice(0, 10);
export const addDays = (date, n) => day(Date.parse(`${date}T00:00:00Z`) + n * DAY_MS);

export function addMonths(month, n) {
    const [y, m] = month.split("-").map(Number);
    const i = y * 12 + (m - 1) + n;
    return `${String(Math.floor(i / 12)).padStart(4, "0")}-${String((i % 12) + 1).padStart(2, "0")}`;
}

/** ISO 8601 week of a UTC day ('AAAA-Www'), as src/challenges.ts. */
export function isoWeek(date) {
    const t = Date.parse(`${date}T00:00:00Z`);
    const thursday = new Date(t - ((new Date(t).getUTCDay() + 6) % 7) * DAY_MS + 3 * DAY_MS);
    const year = thursday.getUTCFullYear();
    const week = Math.floor((thursday.getTime() - Date.UTC(year, 0, 1)) / (7 * DAY_MS)) + 1;
    return `${year}-W${String(week).padStart(2, "0")}`;
}

/**
 * The UTC days of a period, as src/challenges.ts: { start, end } (end excluded), or null when
 * the period is malformed or does not exist (W53 of a year that has 52 weeks).
 */
export function periodBounds(kind, period) {
    if (typeof period !== "string") return null;
    if (kind === "daily") return isDate(period) ? { start: period, end: addDays(period, 1) } : null;
    if (kind === "weekly") {
        const m = WEEK_RE.exec(period);
        const year = Number(m?.[1]);
        const week = Number(m?.[2]);
        if (!m || year < 2000 || year > 2999 || week < 1 || week > 53) return null;
        const jan4 = Date.UTC(year, 0, 4);
        const start = day(jan4 - ((new Date(jan4).getUTCDay() + 6) % 7) * DAY_MS + (week - 1) * 7 * DAY_MS);
        return isoWeek(start) === period ? { start, end: addDays(start, 7) } : null;
    }
    if (kind === "monthly") {
        const m = MONTH_RE.exec(period);
        const year = Number(m?.[1]);
        const month = Number(m?.[2]);
        if (!m || year < 2000 || year > 2999 || month < 1 || month > 12) return null;
        return { start: `${period}-01`, end: `${addMonths(period, 1)}-01` };
    }
    return null;
}

/** "m:ss" (or "h:mm:ss", or seconds) -> milliseconds. */
export function parseTime(s) {
    const parts = String(s).trim().split(":");
    if (parts.length > 3 || !parts.every((p) => /^\d{1,4}(\.\d{1,3})?$/.test(p))) throw new Error(`temps invalide « ${s} » (m:ss attendu)`);
    const seconds = parts.reduce((acc, p) => acc * 60 + Number(p), 0);
    if (!(seconds > 0) || seconds > 24 * 3600) throw new Error(`temps invalide « ${s} »`);
    return Math.round(seconds * 1000);
}

/** "1:45", "1:05:30", "1:45.250": a time as the Journal and the script write it. */
export function formatTime(ms) {
    const total = Math.floor(ms / 1000);
    const h = Math.floor(total / 3600);
    const m = Math.floor((total % 3600) / 60);
    const s = total % 60;
    const rest = ms % 1000;
    const base = h > 0 ? `${h}:${String(m).padStart(2, "0")}:${String(s).padStart(2, "0")}` : `${m}:${String(s).padStart(2, "0")}`;
    return rest ? `${base}.${String(rest).padStart(3, "0")}` : base;
}

/** An integer (a number, or a string of digits as the command line gives it) in [min, max]. */
function intIn(field, raw, min, max, what) {
    const n = typeof raw === "number" ? raw : /^\d{1,9}$/.test(String(raw)) ? Number(raw) : Number.NaN;
    if (!Number.isInteger(n) || n < min || n > max) throw new ChallengeSpecError(field, `${what} : entier de ${min} à ${max} attendu`);
    return n;
}

function boolOr(field, raw, def) {
    if (raw === undefined || raw === null) return def;
    if (typeof raw !== "boolean") throw new ChallengeSpecError(field, "vrai ou faux attendu");
    return raw;
}

/**
 * Checks an administrator's challenge and fills in what is left out.
 *
 * `spec`: { kind, period, level: { source: "campaign"|"community", id, version? }, hearts?,
 * slots?, powers?: string[], stations?, checkpoints?, objective?: "speed"|"finish",
 * silver_ms?, gold_ms?, gold_no_damage?, bronze_progress?, silver_progress? };
 * `catalog`: src/challengeCatalog.json; `today`: the UTC day ('AAAA-MM-JJ'): the period must
 * begin later, or (`options.running`, 2.60: the routes of the calendar, which check in the same
 * write that nobody has a result on it) be running; a period that is over never. Defaults: a campaign level keeps its slots, its start powers and the times of
 * its gold and platinum medals; a community level gets 4 slots and no imposed power, and needs
 * its times (« Vitesse »). Hearts: the player's maximum. Stations and checkpoints on, Or
 * without damage; « Finir le niveau »: Bronze 50 %, Argent 80 % of the route.
 *
 * Returns { id, kind, period, start, end, level: { source, id, version }, rules, objective,
 * silver_ms, gold_ms, gold_no_damage, bronze_progress, silver_progress, warnings }; throws a
 * ChallengeSpecError (its field and a French message) at the first problem.
 */
export function validateChallengeSpec(spec, catalog, today, options = {}) {
    const kind = spec.kind;
    if (!KINDS.includes(kind)) throw new ChallengeSpecError("kind", "sorte de défi inconnue (jour, semaine ou mois attendu)");
    const bounds = periodBounds(kind, spec.period);
    if (!bounds) throw new ChallengeSpecError("period", `période invalide « ${spec.period} »`);
    if (!isDate(today)) throw new ChallengeSpecError("period", "date du jour invalide");
    const running = options?.running === true && bounds.start <= today && bounds.end > today;
    if (bounds.start <= today && !running)
        throw new ChallengeSpecError(
            "period",
            `période ${spec.period} (début ${bounds.start}) refusée : un défi commencé ou passé ne change plus (aujourd'hui : ${today} UTC)`,
        );
    const id = `${kind}-${spec.period}`;

    const lv = spec.level ?? {};
    const community = lv.source === "community";
    if (!community && lv.source !== "campaign") throw new ChallengeSpecError("level", "niveau de la campagne ou de la communauté attendu");
    if (typeof lv.id !== "string" || !(community ? UUID_RE : LEVEL_ID_RE).test(lv.id)) throw new ChallengeSpecError("level", "identifiant de niveau invalide");
    const version = community ? intIn("level", lv.version, 1, 100000, "version") : null;
    const info = community ? null : catalog.levels[lv.id];
    if (!community && !info) throw new ChallengeSpecError("level", `niveau ${lv.id} absent du catalogue (src/challengeCatalog.json)`);

    const slots = spec.slots !== undefined && spec.slots !== null ? intIn("slots", spec.slots, 1, MAX_SLOTS, "emplacements") : community ? 4 : info.slots;
    const hearts = spec.hearts !== undefined && spec.hearts !== null ? intIn("hearts", spec.hearts, 1, MAX_HEARTS, "cœurs") : catalog.player_max_hearts;
    let powers;
    if (spec.powers !== undefined && spec.powers !== null) {
        if (!Array.isArray(spec.powers)) throw new ChallengeSpecError("powers", "liste de pouvoirs attendue");
        powers = [...spec.powers];
    } else powers = community ? [] : [...info.start_powers];
    for (const p of powers)
        if (typeof p !== "string" || !POWER_RE.test(p) || !Object.hasOwn(catalog.powers, p)) throw new ChallengeSpecError("powers", `pouvoir inconnu : ${p}`);
    if (new Set(powers).size !== powers.length) throw new ChallengeSpecError("powers", "pouvoir en double");
    if (powers.length > slots) throw new ChallengeSpecError("powers", `${powers.length} pouvoirs pour ${slots} emplacements`);
    powers.sort();
    const rules = {
        hearts,
        slots,
        powers,
        stations: boolOr("stations", spec.stations, true),
        checkpoints: boolOr("checkpoints", spec.checkpoints, true),
    };

    const objective = spec.objective ?? "speed";
    if (!OBJECTIVES.includes(objective)) throw new ChallengeSpecError("objective", "objectif « Vitesse » ou « Finir le niveau » attendu");
    const goldNoDamage = boolOr("gold_no_damage", spec.gold_no_damage, true);
    let silver = FINISH_TIER_MS;
    let gold = FINISH_TIER_MS;
    let bronzeProgress = DEFAULT_BRONZE_PROGRESS;
    let silverProgress = DEFAULT_SILVER_PROGRESS;
    if (objective === "speed") {
        silver = spec.silver_ms ?? info?.silver_ms;
        gold = spec.gold_ms ?? info?.gold_ms;
        if (silver === undefined || gold === undefined)
            throw new ChallengeSpecError("silver_ms", "temps de l'Argent et de l'Or obligatoires pour un niveau de la communauté");
        silver = intIn("silver_ms", silver, 1000, MAX_TIER_MS, "temps de l'Argent (ms)");
        gold = intIn("gold_ms", gold, 1000, MAX_TIER_MS, "temps de l'Or (ms)");
        if (gold > silver) throw new ChallengeSpecError("gold_ms", "le temps de l'Or doit être inférieur ou égal à celui de l'Argent");
    } else {
        if (spec.bronze_progress !== undefined && spec.bronze_progress !== null)
            bronzeProgress = intIn("bronze_progress", spec.bronze_progress, PROGRESS_MIN, PROGRESS_MAX, "part du parcours du Bronze (%)");
        if (spec.silver_progress !== undefined && spec.silver_progress !== null)
            silverProgress = intIn("silver_progress", spec.silver_progress, PROGRESS_MIN, PROGRESS_MAX, "part du parcours de l'Argent (%)");
        if (silverProgress <= bronzeProgress)
            throw new ChallengeSpecError("silver_progress", "l'Argent doit demander une plus grande part du parcours que le Bronze");
    }

    // Allowed, but the administrator should know that the level may no longer be finishable.
    const warnings = [];
    if (info) {
        const missing = info.start_powers.filter((p) => !powers.includes(p));
        if (missing.length) warnings.push(`pouvoirs de départ du niveau absents des règles : ${missing.join(", ")}`);
        if (slots < info.slots) warnings.push(`moins d'emplacements (${slots}) que le niveau (${info.slots})`);
        if (!rules.stations) warnings.push("stations de pouvoirs désactivées");
    } else warnings.push("niveau de la communauté : vérifiez qu'il reste faisable avec ces règles (pouvoirs, emplacements, stations)");
    return {
        id,
        kind,
        period: spec.period,
        start: bounds.start,
        end: bounds.end,
        level: { source: community ? "community" : "campaign", id: lv.id, version },
        rules,
        objective,
        silver_ms: silver,
        gold_ms: gold,
        gold_no_damage: goldNoDamage,
        bronze_progress: bronzeProgress,
        silver_progress: silverProgress,
        warnings,
    };
}
