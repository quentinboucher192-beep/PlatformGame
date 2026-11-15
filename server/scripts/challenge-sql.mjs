// SQL of an administrator's daily challenge (used by scripts/challenge-set.mjs and by the
// server tests). Pure: no file, no network; Web Crypto only (Node 18+ and Workers).
//
// The SQL only ever writes a challenge whose day has not begun when it is APPLIED
// (`date > date('now')` on the insert and on the update), takes the level revision / the
// community version's fingerprint, title and author from the database, and records in
// audit_log whether it was applied or skipped (level not ranked, version not published, day
// already begun): a migration never fails because of it, so it never blocks the next ones.

export const MAX_SLOTS = 8;
export const MAX_HEARTS = 20;
const DATE_RE = /^\d{4}-\d{2}-\d{2}$/;
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
 * `today` = the UTC day ('AAAA-MM-JJ') at generation: the date must be later.
 * Returns { sql, id, rules, rulesHash, canonical, warnings }.
 */
export async function buildChallengeSql(opts, catalog, today) {
    if (!opts.date || !isDate(opts.date)) throw new Error("--date AAAA-MM-JJ obligatoire");
    if (!isDate(today)) throw new Error("date du jour invalide");
    if (opts.date <= today) throw new Error(`date ${opts.date} refusée : un défi passé ou du jour ne change plus (aujourd'hui : ${today} UTC)`);
    const id = `daily-${opts.date}`;
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
    const notStarted = `${q(opts.date)} > date('now')`;
    const values = [
        q(id), "'daily'", q(opts.date), "'admin'",
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
    const columns = "id, kind, date, source, level_source, level_id, level_version, level_hash, level_title, hearts, slots, powers, stations, checkpoints, rules_hash, silver_ms, gold_ms, gold_no_damage, featured_player_id, featured_notified_at, created_at, updated_at";
    const updated = ["source", "level_source", "level_id", "level_version", "level_hash", "level_title", "hearts", "slots", "powers", "stations", "checkpoints", "rules_hash", "silver_ms", "gold_ms", "gold_no_damage", "featured_player_id", "featured_notified_at", "updated_at"];
    const level = community ? `community:${levelId}@${version}` : `campaign:${levelId}`;
    const applied = `EXISTS (SELECT 1 FROM daily_challenges WHERE id = ${q(id)} AND source = 'admin' AND level_id = ${q(levelId)} AND rules_hash = ${q(rulesHash)}${community ? ` AND level_version = ${version}` : ""})`;
    const details = q(JSON.stringify({ challenge: id, level, rules_hash: rulesHash }));
    const sql = [
        `-- Défi du jour ${opts.date} choisi par l'administrateur (scripts/challenge-set.mjs) : ${level}.`,
        `-- Règles : ${canonical}`,
        `-- Paliers : Argent ${silver} ms, Or ${gold} ms${opts.goldNoDamage === false ? "" : " sans dégât"}.`,
        `-- Ne modifie le défi que si son jour n'a pas commencé à l'application (audit_log : challenge_set_applied ou challenge_set_skipped).`,
        `INSERT INTO daily_challenges (${columns}) SELECT ${values.join(", ")} ${from} ON CONFLICT (id) DO UPDATE SET ${updated.map((c) => `${c} = excluded.${c}`).join(", ")} WHERE daily_challenges.date > date('now');`,
        `INSERT INTO audit_log (player_id, action, details, created_at) SELECT NULL, CASE WHEN ${applied} THEN 'challenge_set_applied' ELSE 'challenge_set_skipped' END, ${details}, ${NOW};`,
        "",
    ].join("\n");
    return { sql, id, rules, rulesHash, canonical, warnings };
}
