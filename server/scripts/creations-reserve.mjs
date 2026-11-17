// 2.60 « Réserve du jeu »: the creations of ADMINISTRATION › Créations the game brings itself
// (suits, trails, avatar frames, victory effects), ready to be given as the reward of a
// challenge (Calendrier des défis › Récompenses) and invisible to the players until then.
//
//   node scripts/creations-reserve.mjs > migrations/0118_creations_reserve.sql
//   node scripts/creations-reserve.mjs --check migrations/0118_creations_reserve.sql
//
// The output is the same from one run to the next (no clock, no randomness): the order, the
// names and the colours below, one INSERT per creation that does nothing when a creation of the
// same kind already has its name (accents and case aside, as the server compares them: a
// creation of an administrator is never replaced, and running it twice adds nothing). Every
// creation is checked here as the server checks one an administrator sends
// (server/src/creations.ts): a name of 1 to 32 characters (letters, digits, punctuation,
// spaces), "#rrggbb" colours, a style of its kind's list (read from creations.ts and from the
// game's game/cosmetics/CreationLooks.cpp: it must be in both), a trail's width 50 to 200 % and
// life 200 to 1000 ms. server/test/creationsReserve.test.ts sends each one through the
// administration's route too.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, "..", "..");
/** The creations of the reserve carry this date (the version that brought them), never the clock's. */
const ADDED_AT = "2026-11-17T00:00:00.000Z";

// ------------------------------------------------------------------ the reserve

/** Combinaisons: [name, suit, accent (visor, energy), scarf]. */
const SUITS = [
    // The planets of the game.
    ["Brume d'Aethra", "#d9d2f2", "#8fe3ff", "#7b5cd6"],
    ["Banquise de Glacior", "#e6f4ff", "#5fd0ff", "#2f6fb3"],
    ["Lave de Pyralis", "#3a2422", "#ff9a3c", "#e2442f"],
    ["Mousse de Sylvanox", "#5f8f4e", "#d6f27a", "#a8572e"],
    ["Cœur du Noyau", "#4a3b6b", "#ffd25a", "#ff6b3d"],
    ["Sentinelle des Confins", "#2b3346", "#7cf5d9", "#b03a5b"],
    // The seasons.
    ["Printemps en fleurs", "#f3e6ef", "#7ed99a", "#f08bb4"],
    ["Été caniculaire", "#f7d38a", "#4ec2e8", "#e8603c"],
    ["Automne roux", "#b8643a", "#ffd27a", "#6e3b23"],
    ["Hiver de cristal", "#f4f8ff", "#9fd8ff", "#5577aa"],
    // The precious stones.
    ["Rubis ardent", "#9e1b32", "#ffb3c1", "#f2d16b"],
    ["Saphir royal", "#1f3b8c", "#8fd3ff", "#e8c25a"],
    ["Émeraude sauvage", "#1f7a4d", "#a6ffcf", "#e8e2c8"],
    ["Améthyste", "#6b3fa0", "#e2b8ff", "#2c1a4a"],
    ["Topaze dorée", "#d99a2b", "#fff2b0", "#7a4a12"],
    ["Opale de lune", "#e8eef2", "#ffc6e8", "#8fd6d0"],
    ["Grenat profond", "#5c1424", "#ff6f7d", "#c9a96e"],
    ["Jade impérial", "#3e8f6c", "#e6ffef", "#b33a2e"],
    ["Onyx et argent", "#1d1d24", "#d7dde8", "#6f7a8c"],
    ["Turquoise des sables", "#3fb5b0", "#fff0c2", "#c2703d"],
    // The nebulae.
    ["Nébuleuse du Crabe", "#2a2050", "#ff8fd0", "#4fd6ff"],
    ["Tête de Cheval", "#3b1f1c", "#ff7a4a", "#6d2fa0"],
    ["Piliers de l'Aigle", "#4a3a26", "#9ff5c8", "#e0a24c"],
    ["Œil de Chat", "#123b4a", "#6dffd8", "#ff5a8c"],
    ["Nuage de Magellan", "#2e2a5e", "#b6c8ff", "#ffdd88"],
    // The metals.
    ["Cuivre patiné", "#b0663c", "#5fe0c2", "#2f6b5e"],
    ["Bronze antique", "#8c6239", "#ffcf7a", "#3d2a1a"],
    ["Titane brossé", "#8e97a3", "#61e8ff", "#2b2f36"],
    ["Chrome miroir", "#d5dbe3", "#ffffff", "#4a5566"],
    ["Acier trempé", "#4f5b6a", "#ff9b45", "#1e252e"],
    ["Platine glacé", "#e1e4ea", "#a9d8ff", "#7b8496"],
    // The sky and the elements.
    ["Comète d'argent", "#c9d2e0", "#80e8ff", "#355a9e"],
    ["Éclipse totale", "#121018", "#ffcf5a", "#6a1b2b"],
    ["Aube boréale", "#1f3d4f", "#8cffb5", "#c48bff"],
    ["Crépuscule pourpre", "#4b2547", "#ffa77a", "#f2c14e"],
    ["Abysse luminescent", "#0f2a3a", "#39ffd0", "#2a86c9"],
    ["Cendre volcanique", "#5a5552", "#ff6a2b", "#2a2523"],
    ["Corail des récifs", "#ff8a73", "#6ee7f0", "#f5f0e1"],
    ["Pollen d'or", "#f2d064", "#ffffff", "#8a6b1f"],
    ["Orage magnétique", "#2c3150", "#c7a6ff", "#ffe066"],
];

/** Traînées: [name, style, start (by Nova), end (its tail), width %, life ms, sparkles]. */
const TRAILS = [
    ["Sillage d'Aethra", "comet", "#e3d9ff", "#7b5cd6", 120, 520, true],
    ["Queue de comète glacée", "comet", "#ffffff", "#5fd0ff", 140, 700, true],
    ["Bolide de Pyralis", "comet", "#fff1a8", "#ff4a1c", 160, 450, true],
    ["Météore émeraude", "comet", "#d6ffe6", "#1f9a5a", 110, 600, false],
    ["Étoile filante rose", "comet", "#ffe3f1", "#e0559b", 90, 380, true],
    ["Poudre de saphir", "stardust", "#bfe3ff", "#1f3b8c", 100, 650, true],
    ["Paillettes d'or", "stardust", "#fff2b0", "#c98a1a", 90, 500, true],
    ["Givre scintillant", "stardust", "#ffffff", "#8fd3ff", 80, 800, true],
    ["Pollen de Sylvanox", "stardust", "#f5ffb0", "#7fc24a", 120, 900, true],
    ["Cendres d'étoile", "stardust", "#ffd2a8", "#5a4a6a", 100, 1000, false],
    ["Ruban d'améthyste", "prism", "#e2b8ff", "#6b3fa0", 130, 550, false],
    ["Ruban de jade", "prism", "#e6ffef", "#3e8f6c", 120, 500, true],
    ["Reflet de lune", "prism", "#ffc6e8", "#8fd6d0", 110, 650, true],
    ["Ruban de cuivre", "prism", "#ffcf9a", "#b0663c", 140, 450, false],
    ["Faisceau de titane", "prism", "#e9f2ff", "#61e8ff", 70, 300, false],
    ["Braises de Pyralis", "flame", "#ffd36b", "#e2442f", 130, 600, true],
    ["Flamme bleue", "flame", "#c6f0ff", "#2f6fff", 120, 550, true],
    ["Feu follet", "flame", "#e6ffcf", "#39c46a", 100, 700, true],
    ["Flamme spectrale", "flame", "#f2e6ff", "#8a4dff", 110, 650, true],
    ["Tison d'automne", "flame", "#ffb35a", "#6e3b23", 150, 500, false],
    ["Aurore de Glacior", "aurora", "#8cffb5", "#4fb4ff", 160, 900, true],
    ["Voile de nébuleuse", "aurora", "#ff8fd0", "#4fd6ff", 180, 1000, false],
    ["Lueur du Noyau", "aurora", "#ffe066", "#ff6b3d", 150, 750, true],
    ["Crépuscule d'été", "aurora", "#ffa77a", "#b04bd6", 170, 850, false],
    ["Écharpe boréale", "aurora", "#b6ffd8", "#c48bff", 200, 950, true],
    ["Perles des abysses", "bubbles", "#c9fff4", "#1b7fb0", 110, 800, true],
    ["Écume de corail", "bubbles", "#fff0e6", "#ff8a73", 100, 700, true],
    ["Bulles de champagne", "bubbles", "#fff8d6", "#e8c25a", 80, 600, true],
    ["Gouttes de rosée", "bubbles", "#ffffff", "#7ed99a", 90, 650, false],
    ["Méduses des Confins", "bubbles", "#e8c8ff", "#7cf5d9", 130, 900, true],
];

/** Cadres: [name, style, border, glow]. */
const FRAMES = [
    ["Anneau de cuivre", "metal", "#c47a45", "#ffb27a"],
    ["Cerclage de titane", "metal", "#9aa4b2", "#61e8ff"],
    ["Anneau de vermeil", "metal", "#e0a86a", "#ffe0a0"],
    ["Couronne d'acier", "metal", "#6c7889", "#c7d6ea"],
    ["Platine rosé", "metal", "#e8c9c4", "#ffd6e8"],
    ["Sertissage de rubis", "emerald", "#9e1b32", "#ff6f7d"],
    ["Sertissage de saphirs", "emerald", "#1f3b8c", "#8fd3ff"],
    ["Couronne d'améthystes", "emerald", "#6b3fa0", "#e2b8ff"],
    ["Écrin de topazes", "emerald", "#d99a2b", "#fff2b0"],
    ["Collier d'opales", "emerald", "#8fd6d0", "#ffc6e8"],
    ["Lunes d'Aethra", "orbit", "#b9a8ff", "#e3d9ff"],
    ["Satellites de Glacior", "orbit", "#8fd3ff", "#ffffff"],
    ["Ronde des comètes", "orbit", "#ffd36b", "#ff7a4a"],
    ["Orbite de Sylvanox", "orbit", "#7fc24a", "#d6f27a"],
    ["Gravitation", "orbit", "#4fd6ff", "#c48bff"],
    ["Nuée pourpre", "nebula", "#8a4dff", "#ff8fd0"],
    ["Nébuleuse d'émeraude", "nebula", "#1f9a5a", "#6dffd8"],
    ["Fumées de Pyralis", "nebula", "#e2442f", "#ffb35a"],
    ["Voile d'Orion", "nebula", "#2f6fff", "#ff8fd0"],
    ["Grand Nuage de Magellan", "nebula", "#b6c8ff", "#ffdd88"],
    ["Grande Ourse", "constellation", "#c7d6ea", "#ffe066"],
    ["Constellation du Cygne", "constellation", "#8fd3ff", "#ffffff"],
    ["Ceinture d'Orion", "constellation", "#ffd27a", "#7b5cd6"],
    ["Croix du Sud", "constellation", "#7cf5d9", "#4fb4ff"],
    ["Cassiopée", "constellation", "#ffb3c1", "#c48bff"],
    ["Runes de givre", "runic", "#9fd8ff", "#ffffff"],
    ["Glyphes de braise", "runic", "#ff9a3c", "#ffd36b"],
    ["Sceau de Sylvanox", "runic", "#7fc24a", "#f5ffb0"],
    ["Cercle des Confins", "runic", "#7cf5d9", "#b03a5b"],
    ["Runes du Noyau", "runic", "#ffd25a", "#ff6b3d"],
];

/** Effets de victoire: [name, effect, main, second]. */
const VICTORIES = [
    ["Bouquet final", "fireworks", "#ff5a8c", "#ffe066"],
    ["Feux de Pyralis", "fireworks", "#ff7a2b", "#ffd36b"],
    ["Gerbe de saphirs", "fireworks", "#4f8bff", "#bfe3ff"],
    ["Fête des moissons", "fireworks", "#ffb35a", "#7fc24a"],
    ["Éclats de jade", "fireworks", "#3e8f6c", "#e6ffef"],
    ["Pluie d'Aethra", "starfall", "#b9a8ff", "#ffffff"],
    ["Neige de Glacior", "starfall", "#ffffff", "#8fd3ff"],
    ["Averse dorée", "starfall", "#ffd27a", "#fff2b0"],
    ["Pétales de printemps", "starfall", "#f08bb4", "#f3e6ef"],
    ["Feuilles d'automne", "starfall", "#d0703a", "#ffcf7a"],
    ["Éveil du Noyau", "supernova", "#ffd25a", "#ff6b3d"],
    ["Nova bleue", "supernova", "#8fd3ff", "#2f6fff"],
    ["Éclat d'améthyste", "supernova", "#e2b8ff", "#6b3fa0"],
    ["Naine blanche", "supernova", "#ffffff", "#c7d6ea"],
    ["Géante rouge", "supernova", "#ff6f5a", "#9e1b32"],
    ["Tourbillon des Confins", "vortex", "#7cf5d9", "#b03a5b"],
    ["Spirale de Magellan", "vortex", "#b6c8ff", "#ffdd88"],
    ["Cyclone d'émeraude", "vortex", "#6dffd8", "#1f9a5a"],
    ["Trou noir", "vortex", "#c48bff", "#ff8fd0"],
    ["Maelström", "vortex", "#4fd6ff", "#2a86c9"],
    ["Rideau boréal", "borealis", "#8cffb5", "#4fb4ff"],
    ["Aurore australe", "borealis", "#ff8fd0", "#8a4dff"],
    ["Voile de Sylvanox", "borealis", "#d6f27a", "#39c46a"],
    ["Ciel de Glacior", "borealis", "#9fd8ff", "#c48bff"],
    ["Lueurs du crépuscule", "borealis", "#ffa77a", "#b04bd6"],
    ["Éclipse d'or", "corona", "#ffd25a", "#fff2b0"],
    ["Couronne de Pyralis", "corona", "#ff9a3c", "#e2442f"],
    ["Soleil d'hiver", "corona", "#e6f4ff", "#9fd8ff"],
    ["Halo d'argent", "corona", "#d7dde8", "#8e97a3"],
    ["Soleil pourpre", "corona", "#c48bff", "#ff6b3d"],
];

// ------------------------------------------------------------------ the server's rules

/** The styles a kind accepts, read from a list of the source (`export const TRAIL_STYLES = [...]`, `kTrailStyles[] = {...}`). */
function listIn(file, re) {
    const text = fs.readFileSync(path.join(ROOT, file), "utf8");
    const m = re.exec(text);
    if (!m) throw new Error(`${file} : liste introuvable (${re})`);
    return [...m[1].matchAll(/"([a-z_]+)"/g)].map((x) => x[1]).filter((s, i, all) => all.indexOf(s) === i);
}
const SERVER_STYLES = {
    trail: listIn("server/src/creations.ts", /TRAIL_STYLES = \[([^\]]*)\]/),
    frame: listIn("server/src/creations.ts", /FRAME_STYLES = \[([^\]]*)\]/),
    victory: listIn("server/src/creations.ts", /VICTORY_STYLES = \[([^\]]*)\]/),
};
/** The game's lists: { "comet", "Comète" } pairs, the ids only (the first string of each pair). */
function gameList(name) {
    const text = fs.readFileSync(path.join(ROOT, "game/cosmetics/CreationLooks.cpp"), "utf8");
    const m = new RegExp(`${name}\\[\\] = \\{([\\s\\S]*?)\\n\\};`).exec(text);
    if (!m) throw new Error(`CreationLooks.cpp : ${name} introuvable`);
    return [...m[1].matchAll(/\{"([a-z_]+)",/g)].map((x) => x[1]);
}
const GAME_STYLES = { trail: gameList("kTrailStyles"), frame: gameList("kFrameStyles"), victory: gameList("kVictoryStyles") };

/** As creations.ts / challengeRewards.ts: cleanTitleText() and titleKey(). */
const TEXT_RE = /^[\p{L}\p{M}\p{N}\p{P} ]+$/u;
function cleanName(raw) {
    const t = raw.normalize("NFC").replace(/\s+/gu, " ").trim();
    if (!t || [...t].length > 32 || !TEXT_RE.test(t)) return null;
    return t;
}
const nameKey = (text) =>
    text
        .normalize("NFD")
        .replace(/\p{M}/gu, "")
        .toLowerCase()
        .replace(/\s+/gu, " ")
        .trim();
const COLOR_RE = /^#[0-9a-f]{6}$/;

function check(kind, name, colors, style, extra) {
    const where = `${kind} « ${name} »`;
    if (cleanName(name) !== name) throw new Error(`${where} : nom refusé (1 à 32 caractères, déjà nettoyé)`);
    for (const c of colors) if (!COLOR_RE.test(c)) throw new Error(`${where} : couleur ${c} (#rrggbb en minuscules)`);
    if (kind === "suit") return;
    if (!SERVER_STYLES[kind].includes(style)) throw new Error(`${where} : style ${style} hors de la liste du serveur`);
    if (!GAME_STYLES[kind].includes(style)) throw new Error(`${where} : style ${style} inconnu du jeu`);
    if (kind === "trail") {
        const { width, life, particles } = extra;
        if (!Number.isInteger(width) || width < 50 || width > 200) throw new Error(`${where} : largeur ${width} (50 à 200)`);
        if (!Number.isInteger(life) || life < 200 || life > 1000) throw new Error(`${where} : durée ${life} (200 à 1000)`);
        if (typeof particles !== "boolean") throw new Error(`${where} : étincelles oui / non`);
    }
}

// ------------------------------------------------------------------ the migration

const q = (v) => (v === null ? "NULL" : typeof v === "number" ? String(v) : "'" + String(v).replace(/'/g, "''") + "'");

/** Every creation of the reserve, in order: { kind, name, key, columns }. */
function reserve() {
    const out = [];
    const keys = new Set();
    const add = (kind, name, cols, style, extra = {}) => {
        const colors = kind === "suit" ? [cols.suit_color, cols.accent_color, cols.scarf_color] : [cols.color_a, cols.color_b];
        check(kind, name, colors, style, extra);
        const key = nameKey(name);
        if (keys.has(`${kind}:${key}`)) throw new Error(`${kind} « ${name} » : nom en double (accents et casse ignorés)`);
        keys.add(`${kind}:${key}`);
        out.push({ kind, name, key, cols });
    };
    const none = { suit_color: null, accent_color: null, scarf_color: null, color_a: null, color_b: null, style: null, width: null, life_ms: null, particles: null };
    for (const [name, suit, accent, scarf] of SUITS) add("suit", name, { ...none, suit_color: suit, accent_color: accent, scarf_color: scarf });
    for (const [name, style, a, b, width, life, particles] of TRAILS)
        add("trail", name, { ...none, color_a: a, color_b: b, style, width, life_ms: life, particles: particles ? 1 : 0 }, style, { width, life, particles });
    for (const [name, style, a, b] of FRAMES) add("frame", name, { ...none, color_a: a, color_b: b, style }, style);
    for (const [name, style, a, b] of VICTORIES) add("victory", name, { ...none, color_a: a, color_b: b, style }, style);
    return out;
}

const COLUMNS = ["suit_color", "accent_color", "scarf_color", "color_a", "color_b", "style", "width", "life_ms", "particles"];
const TITLES = { suit: "Combinaisons", trail: "Traînées", frame: "Cadres", victory: "Effets de victoire" };

export function migrationSql() {
    const rows = reserve();
    const count = (k) => rows.filter((r) => r.kind === k).length;
    const lines = [
        "-- 2.60 ADMINISTRATION › Créations: the reserve of the game (« Réserve du jeu »). Generated by",
        "-- server/scripts/creations-reserve.mjs: do not edit by hand (run it again, the output is the same).",
        `-- ${count("suit")} suits, ${count("trail")} trails, ${count("frame")} avatar frames and ${count("victory")} victory effects, each with its`,
        "-- French name and its look, ready to be given as the reward of a challenge (Calendrier des",
        "-- défis › Récompenses). Until a challenge that has begun gives one (or a player won it), no",
        "-- player sees it: GET /creations lists only those (creations.ts, publicCreations).",
        "--",
        "-- `game_reserve` = 1: brought by the game, not made by an administrator (`created_by` NULL);",
        "-- the administration shows « Réserve du jeu » instead of an author. An administrator may then",
        "-- rename, redraw, archive or give them like any other creation.",
        "--",
        "-- Each INSERT does nothing when a creation of the same kind already has that name (accents and",
        "-- case aside, `name_key`, archived or not): a creation of an administrator is never replaced,",
        "-- and applying this file again adds nothing. Display only: no run, replay or record changes.",
        "",
        "ALTER TABLE creations ADD COLUMN game_reserve INTEGER NOT NULL DEFAULT 0 CHECK (game_reserve IN (0, 1));",
    ];
    let kind = "";
    for (const r of rows) {
        if (r.kind !== kind) {
            kind = r.kind;
            lines.push("", `-- ${TITLES[kind]} (${count(kind)})`);
        }
        const values = [q(r.kind), q(r.name), q(r.key), ...COLUMNS.map((c) => q(r.cols[c])), "NULL", q(ADDED_AT), q(ADDED_AT), "1"];
        lines.push(
            `INSERT INTO creations (kind, name, name_key, ${COLUMNS.join(", ")}, created_by, created_at, updated_at, game_reserve)`,
            `SELECT ${values.join(", ")}`,
            `WHERE NOT EXISTS (SELECT 1 FROM creations WHERE kind = ${q(r.kind)} AND name_key = ${q(r.key)});`,
        );
    }
    return lines.join("\n") + "\n";
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const sql = migrationSql();
    const [flag, file] = process.argv.slice(2);
    if (flag === "--check") {
        if (!file) throw new Error("usage : --check migrations/0118_creations_reserve.sql");
        if (fs.readFileSync(file, "utf8") !== sql) {
            console.error(`${file} ne correspond pas à la sortie du script : relancez-le.`);
            process.exit(1);
        }
        console.log(`${file} : à jour.`);
    } else {
        process.stdout.write(sql);
    }
}
