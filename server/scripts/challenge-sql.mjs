// SQL of an administrator's challenge — of a day, of an ISO week or of a month — (used by
// scripts/challenge-set.mjs and by the server tests). Pure: no file, no network; Web Crypto
// only (Node 18+ and Workers).
//
// The SQL only ever writes a challenge whose period has not begun when it is APPLIED
// (`date > date('now')` on the insert and on the update, `date` being its first UTC day),
// takes the level revision / the community version's fingerprint, title and author from the
// database, and records in audit_log whether it was applied or skipped (level not ranked,
// version not published, period already begun): a migration never fails because of it, so it
// never blocks the next ones.

export const MAX_SLOTS = 8;
export const MAX_HEARTS = 20;
export const KINDS = ["daily", "weekly", "monthly"];
const DATE_RE = /^\d{4}-\d{2}-\d{2}$/;
const WEEK_RE = /^(\d{4})-W(\d{2})$/;
const MONTH_RE = /^(\d{4})-(\d{2})$/;
const DAY_MS = 86400000;
const UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const LEVEL_ID_RE = /^[a-z0-9_]{1,64}$/;
const POWER_RE = /^[a-z0-9_]{1,64}$/;

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

function isDate(s) {
    if (!DATE_RE.test(s)) return false;
    const t = Date.parse(`${s}T00:00:00Z`);
    return !Number.isNaN(t) && new Date(t).toISOString().slice(0, 10) === s;
}

const day = (ms) => new Date(ms).toISOString().slice(0, 10);
const addDays = (date, n) => day(Date.parse(`${date}T00:00:00Z`) + n * DAY_MS);

function addMonths(month, n) {
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

const FORMATS = { daily: "AAAA-MM-JJ", weekly: "AAAA-Www (semaine ISO 8601)", monthly: "AAAA-MM" };
const LABELS = { daily: "du jour", weekly: "de la semaine", monthly: "du mois" };

/**
 * The challenge's kind and period from the options: `--kind` and `--period`, or the 2.32 form
 * `--date AAAA-MM-JJ` (a daily challenge).
 */
export function resolvePeriod(opts) {
    const kind = opts.kind ?? "daily";
    if (!KINDS.includes(kind)) throw new Error("--kind daily, weekly ou monthly attendu");
    let period = opts.period;
    if (opts.date !== undefined) {
        if (kind !== "daily") throw new Error("--date ne vaut que pour un défi du jour (sinon --period)");
        if (period !== undefined && period !== opts.date) throw new Error("--date et --period différents");
        period = opts.date;
    }
    if (period === undefined) throw new Error(kind === "daily" ? "--period AAAA-MM-JJ (ou --date AAAA-MM-JJ) obligatoire" : `--period ${FORMATS[kind]} obligatoire`);
    const bounds = periodBounds(kind, String(period));
    if (!bounds) throw new Error(`période invalide « ${period} » : ${FORMATS[kind]} attendu`);
    return { kind, period: String(period), ...bounds };
}

/** "m:ss" (or "h:mm:ss", or seconds) -> milliseconds. */
export function parseTime(s) {
    const parts = String(s).trim().split(":");
    if (parts.length > 3 || !parts.every((p) => /^\d{1,4}(\.\d{1,3})?$/.test(p))) throw new Error(`temps invalide « ${s} » (m:ss attendu)`);
    const seconds = parts.reduce((acc, p) => acc * 60 + Number(p), 0);
    if (!(seconds > 0) || seconds > 24 * 3600) throw new Error(`temps invalide « ${s} »`);
    return Math.round(seconds * 1000);
}

/** Parses the command line of challenge-set.mjs. */
export function parseArgs(argv) {
    const o = { stations: true, checkpoints: true, goldNoDamage: true };
    const value = (i) => {
        if (i + 1 >= argv.length) throw new Error(`valeur manquante après ${argv[i]}`);
        return argv[i + 1];
    };
    for (let i = 0; i < argv.length; ++i) {
        const a = argv[i];
        switch (a) {
            case "--kind": o.kind = value(i++); break;
            case "--period": o.period = value(i++); break;
            case "--date": o.date = value(i++); break;
            case "--level": o.level = value(i++); break;
            case "--hearts": o.hearts = value(i++); break;
            case "--slots": o.slots = value(i++); break;
            case "--powers": o.powers = value(i++); break;
            case "--silver": o.silver = value(i++); break;
            case "--gold": o.gold = value(i++); break;
            case "--no-stations": o.stations = false; break;
            case "--no-checkpoints": o.checkpoints = false; break;
            case "--gold-damage-ok": o.goldNoDamage = false; break;
            default: throw new Error(`option inconnue : ${a}`);
        }
    }
    return o;
}

function intIn(name, raw, min, max) {
    const n = Number(raw);
    if (!/^\d+$/.test(String(raw)) || n < min || n > max) throw new Error(`${name} : entier de ${min} à ${max} attendu`);
    return n;
}

const q = (s) => "'" + String(s).replace(/'/g, "''") + "'";
const NOW = "strftime('%Y-%m-%dT%H:%M:%fZ', 'now')";

/**
 * Builds the migration. `opts` from parseArgs; `catalog` = src/challengeCatalog.json;
 * `today` = the UTC day ('AAAA-MM-JJ') at generation: the period must begin later.
 * Returns { sql, id, kind, period, start, end, rules, rulesHash, canonical, warnings }.
 */
export async function buildChallengeSql(opts, catalog, today) {
    const { kind, period, start, end } = resolvePeriod(opts);
    if (!isDate(today)) throw new Error("date du jour invalide");
    if (start <= today)
        throw new Error(`période ${period} (début ${start}) refusée : un défi commencé ou passé ne change plus (aujourd'hui : ${today} UTC)`);
    const id = `${kind}-${period}`;
    const m = /^(campaign):([a-z0-9_]{1,64})$|^(community):([0-9a-f-]{36})@(\d{1,6})$/.exec(opts.level ?? "");
    if (!m) throw new Error("--level campaign:<id> ou community:<uuid>@<version> obligatoire");
    const community = m[3] === "community";
    const levelId = community ? m[4] : m[2];
    if (community ? !UUID_RE.test(levelId) : !LEVEL_ID_RE.test(levelId)) throw new Error("identifiant de niveau invalide");
    const version = community ? intIn("version", m[5], 1, 100000) : null;
    const info = community ? null : catalog.levels[levelId];
    if (!community && !info) throw new Error(`niveau ${levelId} absent du catalogue (src/challengeCatalog.json)`);

    const slots = opts.slots !== undefined ? intIn("--slots", opts.slots, 1, MAX_SLOTS) : community ? 4 : info.slots;
    const hearts = opts.hearts !== undefined ? intIn("--hearts", opts.hearts, 1, MAX_HEARTS) : catalog.player_max_hearts;
    let powers;
    if (opts.powers !== undefined) powers = String(opts.powers).split(",").map((p) => p.trim()).filter(Boolean);
    else powers = community ? [] : [...info.start_powers];
    for (const p of powers) if (!POWER_RE.test(p) || !(p in catalog.powers)) throw new Error(`pouvoir inconnu : ${p}`);
    if (new Set(powers).size !== powers.length) throw new Error("pouvoir en double");
    if (powers.length > slots) throw new Error(`${powers.length} pouvoirs pour ${slots} emplacements`);
    powers.sort();
    const silver = opts.silver !== undefined ? parseTime(opts.silver) : info?.silver_ms;
    const gold = opts.gold !== undefined ? parseTime(opts.gold) : info?.gold_ms;
    if (silver === undefined || gold === undefined) throw new Error("--silver et --gold obligatoires pour un niveau de la communauté");
    if (gold > silver) throw new Error("le temps de l'Or doit être inférieur ou égal à celui de l'Argent");

    const rules = { hearts, slots, powers, stations: opts.stations !== false, checkpoints: opts.checkpoints !== false };
    // Allowed, but the administrator should know that the level may no longer be finishable.
    const warnings = [];
    if (info) {
        const missing = info.start_powers.filter((p) => !powers.includes(p));
        if (missing.length) warnings.push(`pouvoirs de départ du niveau absents des règles : ${missing.join(", ")}`);
        if (slots < info.slots) warnings.push(`moins d'emplacements (${slots}) que le niveau (${info.slots})`);
        if (!rules.stations) warnings.push("stations de pouvoirs désactivées");
    } else warnings.push("niveau de la communauté : vérifiez qu'il reste faisable avec ces règles (pouvoirs, emplacements, stations)");
    const canonical = canonicalRules(id, rules);
    const rulesHash = await sha256Hex(canonical);
    const notStarted = `${q(start)} > date('now')`;
    const values = [
        q(id), q(kind), q(period), q(start), q(end), "'admin'",
        community ? "'community'" : "'campaign'",
        community ? "v.level_id" : "l.id",
        community ? "v.version" : "l.current_version",
        community ? "v.level_hash" : "l.content_hash",
        community ? "l.title" : "l.name",
        String(hearts), String(slots), q(JSON.stringify(powers)), rules.stations ? "1" : "0", rules.checkpoints ? "1" : "0",
        q(rulesHash), String(silver), String(gold), opts.goldNoDamage === false ? "0" : "1",
        community ? "l.author_id" : "NULL", "NULL", NOW, NOW,
    ];
    const from = community
        ? `FROM community_level_versions v JOIN community_levels l ON l.id = v.level_id WHERE v.level_id = ${q(levelId)} AND v.version = ${version} AND v.published_at IS NOT NULL AND v.status IN ('published', 'superseded') AND l.status = 'published' AND ${notStarted}`
        : `FROM levels l JOIN level_versions lv ON lv.level_id = l.id AND lv.content_hash = l.content_hash WHERE l.id = ${q(levelId)} AND l.ranking_enabled = 1 AND lv.compatibility_status = 'active' AND ${notStarted}`;
    const columns = "id, kind, period, date, end_date, source, level_source, level_id, level_version, level_hash, level_title, hearts, slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, featured_player_id, featured_notified_at, created_at, updated_at";
    const updated = ["source", "level_source", "level_id", "level_version", "level_hash", "level_title", "hearts", "slots", "powers", "stations", "checkpoints", "rules_hash", "silver_ms", "gold_ms", "gold_no_damage", "featured_player_id", "featured_notified_at", "updated_at"];
    const level = community ? `community:${levelId}@${version}` : `campaign:${levelId}`;
    const applied = `EXISTS (SELECT 1 FROM challenges WHERE id = ${q(id)} AND source = 'admin' AND level_id = ${q(levelId)} AND rules_hash = ${q(rulesHash)}${community ? ` AND level_version = ${version}` : ""})`;
    const details = q(JSON.stringify({ challenge: id, kind, level, rules_hash: rulesHash }));
    const span = kind === "daily" ? period : `${period} (du ${start} au ${addDays(end, -1)})`;
    const sql = [
        `-- Défi ${LABELS[kind]} ${span}, choisi par l'administrateur (scripts/challenge-set.mjs) : ${level}.`,
        `-- Règles : ${canonical}`,
        `-- Paliers : Argent ${silver} ms, Or ${gold} ms${opts.goldNoDamage === false ? "" : " sans dégât"}.`,
        `-- Ne modifie le défi que si sa période n'a pas commencé à l'application (audit_log : challenge_set_applied ou challenge_set_skipped).`,
        `INSERT INTO challenges (${columns}) SELECT ${values.join(", ")} ${from} ON CONFLICT (id) DO UPDATE SET ${updated.map((c) => `${c} = excluded.${c}`).join(", ")} WHERE challenges.date > date('now');`,
        `INSERT INTO audit_log (player_id, action, details, created_at) SELECT NULL, CASE WHEN ${applied} THEN 'challenge_set_applied' ELSE 'challenge_set_skipped' END, ${details}, ${NOW};`,
        "",
    ].join("\n");
    return { sql, id, kind, period, start, end, rules, rulesHash, canonical, warnings };
}
