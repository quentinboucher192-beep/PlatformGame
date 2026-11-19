// 2.76.2: the English notes of a game installer, written from the game's own dictionary
// (data/i18n/en.json, French → English). The notes are made of the changelog's entries
// (« Version x.y.z : », then « • entrée » lines) or of a short text: every line is a text of the
// dictionary (tools/i18n/extract.py reads the notes of the published installers too, and its
// --check asks for their translation). Used by release-migration.mjs; no side effect here.

/**
 * The English notes of `notes`, each line translated by `strings` (the dictionary's
 * « strings »); null when a line has no translation (the game then shows the French notes,
 * translated as it can).
 * @param {string} notes
 * @param {Record<string, string>} strings
 * @returns {string | null}
 */
export function englishNotes(notes, strings) {
    if (!notes) return "";
    const out = [];
    for (const line of String(notes).split("\n")) {
        if (line.trim() === "") {
            out.push("");
            continue;
        }
        const version = /^Version (\d{1,3}\.\d{1,3}\.\d{1,4}) :$/.exec(line.trim());
        if (version) {
            out.push(`Version ${version[1]}:`);
            continue;
        }
        const bullet = /^(\s*[•\-–]\s+)([\s\S]*)$/.exec(line);
        const prefix = bullet ? bullet[1] : "";
        const body = (bullet ? bullet[2] : line).trim();
        const en = Object.prototype.hasOwnProperty.call(strings, body) ? strings[body] : undefined;
        if (typeof en !== "string" || en.trim() === "") return null;
        out.push(prefix + en);
    }
    return out.join("\n");
}

/**
 * The notes of the installers registered by migrations (`*_release_*.sql`): [{ version, notes }].
 * @param {Record<string, string>} files name → SQL text
 */
export function releasesOfMigrations(files) {
    const out = [];
    for (const [name, sql] of Object.entries(files)) {
        if (!/_release_\d+_\d+_\d+\.sql$/.test(name)) continue;
        const m = /INSERT INTO client_releases \(([^)]*)\) VALUES \(([\s\S]*)\) ON CONFLICT/.exec(sql);
        if (!m) continue;
        const cols = m[1].split(",").map((c) => c.trim());
        const vals = [];
        const re = /'((?:[^']|'')*)'|(-?\d+)/g;
        let v;
        while ((v = re.exec(m[2])) !== null) vals.push(v[1] !== undefined ? v[1].replace(/''/g, "'") : v[2]);
        const at = (c) => vals[cols.indexOf(c)];
        if (cols.length !== vals.length || !at("version")) continue;
        out.push({ version: at("version"), notes: at("notes") ?? "" });
    }
    return out;
}

/** A SQL string literal. */
export const sqlText = (s) => "'" + String(s).replace(/'/g, "''") + "'";
