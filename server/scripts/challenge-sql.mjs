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

import {
    ChallengeSpecError,
    KINDS,
    MAX_HEARTS,
    MAX_SLOTS,
    addDays,
    canonicalRules,
    formatTime,
    isoWeek,
    parseTime,
    periodBounds,
    sha256Hex,
    validateChallengeSpec,
} from "../src/challengeSpec.mjs";

// The checks of what an administrator may choose are shared with the server's ADMINISTRATION
// routes (src/challengeSpec.mjs); this file only reads the command line and writes the SQL.
export { KINDS, MAX_HEARTS, MAX_SLOTS, canonicalRules, isoWeek, parseTime, periodBounds, sha256Hex };

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
            case "--objective": o.objective = value(i++); break;
            case "--bronze-pct": o.bronzePct = value(i++); break;
            case "--silver-pct": o.silverPct = value(i++); break;
            default: throw new Error(`option inconnue : ${a}`);
        }
    }
    return o;
}

/** The command line's option of each field of src/challengeSpec.mjs (its errors name them). */
const OPTIONS = {
    kind: "--kind",
    period: "--period",
    level: "--level",
    hearts: "--hearts",
    slots: "--slots",
    powers: "--powers",
    stations: "--no-stations",
    checkpoints: "--no-checkpoints",
    objective: "--objective",
    silver_ms: "--silver",
    gold_ms: "--gold",
    gold_no_damage: "--gold-damage-ok",
    bronze_progress: "--bronze-pct",
    silver_progress: "--silver-pct",
};

/** The server's draw an administrator's choice replaces: its title, kept for the calendar's editor. */
const REPLACED = "replaced_title = CASE WHEN challenges.source = 'auto' THEN challenges.level_title ELSE challenges.replaced_title END";

const q = (s) => "'" + String(s).replace(/'/g, "''") + "'";
const NOW = "strftime('%Y-%m-%dT%H:%M:%fZ', 'now')";

/**
 * Builds the migration. `opts` from parseArgs; `catalog` = src/challengeCatalog.json;
 * `today` = the UTC day ('AAAA-MM-JJ') at generation: the period must begin later.
 * Returns { sql, id, kind, period, start, end, rules, rulesHash, canonical, objective, warnings }.
 */
export async function buildChallengeSql(opts, catalog, today) {
    const { kind, period } = resolvePeriod(opts);
    const m = /^(campaign):([a-z0-9_]{1,64})$|^(community):([0-9a-f-]{36})@(\d{1,6})$/.exec(opts.level ?? "");
    if (!m) throw new Error("--level campaign:<id> ou community:<uuid>@<version> obligatoire");
    const community = m[3] === "community";
    let v;
    try {
        v = validateChallengeSpec(
            {
                kind,
                period,
                level: community ? { source: "community", id: m[4], version: m[5] } : { source: "campaign", id: m[2] },
                hearts: opts.hearts,
                slots: opts.slots,
                powers: opts.powers !== undefined ? String(opts.powers).split(",").map((p) => p.trim()).filter(Boolean) : undefined,
                stations: opts.stations !== false,
                checkpoints: opts.checkpoints !== false,
                objective: opts.objective,
                silver_ms: opts.silver !== undefined ? parseTime(opts.silver) : undefined,
                gold_ms: opts.gold !== undefined ? parseTime(opts.gold) : undefined,
                gold_no_damage: opts.goldNoDamage !== false,
                bronze_progress: opts.bronzePct,
                silver_progress: opts.silverPct,
            },
            catalog,
            today,
        );
    } catch (e) {
        if (e instanceof ChallengeSpecError) throw new Error(`${OPTIONS[e.field] ?? e.field} : ${e.message}`);
        throw e;
    }
    const { id, start, end, rules, warnings, objective } = v;
    const levelId = v.level.id;
    const version = v.level.version;
    const silver = v.silver_ms;
    const gold = v.gold_ms;
    const finish = objective === "finish";
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
        String(rules.hearts), String(rules.slots), q(JSON.stringify(rules.powers)), rules.stations ? "1" : "0", rules.checkpoints ? "1" : "0",
        q(rulesHash), String(silver), String(gold), v.gold_no_damage ? "1" : "0",
        q(objective), String(v.bronze_progress), String(v.silver_progress),
        community ? "l.author_id" : "NULL", "NULL", NOW, NOW,
    ];
    // « Finir le niveau » needs the level version's race layout (its route): without one the SQL changes nothing.
    const race = finish ? ` AND ${community ? "v" : "lv"}.race IS NOT NULL` : "";
    const from = community
        ? `FROM community_level_versions v JOIN community_levels l ON l.id = v.level_id WHERE v.level_id = ${q(levelId)} AND v.version = ${version} AND v.published_at IS NOT NULL AND v.status IN ('published', 'superseded') AND l.status = 'published'${race} AND ${notStarted}`
        : `FROM levels l JOIN level_versions lv ON lv.level_id = l.id AND lv.content_hash = l.content_hash WHERE l.id = ${q(levelId)} AND l.ranking_enabled = 1 AND lv.compatibility_status = 'active'${race} AND ${notStarted}`;
    const columns = "id, kind, period, date, end_date, source, level_source, level_id, level_version, level_hash, level_title, hearts, slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, objective, bronze_progress, silver_progress, featured_player_id, featured_notified_at, created_at, updated_at";
    const updated = ["source", "level_source", "level_id", "level_version", "level_hash", "level_title", "hearts", "slots", "powers", "stations", "checkpoints", "rules_hash", "silver_ms", "gold_ms", "gold_no_damage", "objective", "bronze_progress", "silver_progress", "featured_player_id", "featured_notified_at", "updated_at"];
    const level = community ? `community:${levelId}@${version}` : `campaign:${levelId}`;
    const applied = `EXISTS (SELECT 1 FROM challenges WHERE id = ${q(id)} AND source = 'admin' AND level_id = ${q(levelId)} AND rules_hash = ${q(rulesHash)} AND objective = ${q(objective)}${community ? ` AND level_version = ${version}` : ""})`;
    const details = q(JSON.stringify({ challenge: id, kind, level, rules_hash: rulesHash, objective }));
    const span = kind === "daily" ? period : `${period} (du ${start} au ${addDays(end, -1)})`;
    const tiers = finish
        ? `Objectif : finir le niveau (parcours tracé obligatoire). Paliers : Bronze ${v.bronze_progress} %, Argent ${v.silver_progress} %, Or fini${v.gold_no_damage ? " sans dégât" : ""}.`
        : `Paliers : Argent ${silver} ms (${formatTime(silver)}), Or ${gold} ms (${formatTime(gold)})${v.gold_no_damage ? " sans dégât" : ""}.`;
    const sql = [
        `-- Défi ${LABELS[kind]} ${span}, choisi par l'administrateur (scripts/challenge-set.mjs) : ${level}.`,
        `-- Règles : ${canonical}`,
        `-- ${tiers}`,
        `-- Ne modifie le défi que si sa période n'a pas commencé à l'application (audit_log : challenge_set_applied ou challenge_set_skipped).`,
        `INSERT INTO challenges (${columns}) SELECT ${values.join(", ")} ${from} ON CONFLICT (id) DO UPDATE SET ${updated.map((c) => `${c} = excluded.${c}`).join(", ")}, ${REPLACED} WHERE challenges.date > date('now');`,
        `INSERT INTO audit_log (player_id, action, details, created_at) SELECT NULL, CASE WHEN ${applied} THEN 'challenge_set_applied' ELSE 'challenge_set_skipped' END, ${details}, ${NOW};`,
        "",
    ].join("\n");
    return { sql, id, kind, period, start, end, rules, rulesHash, canonical, objective, warnings };
}
