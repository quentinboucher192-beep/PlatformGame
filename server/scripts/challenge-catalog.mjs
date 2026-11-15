// Writes src/challengeCatalog.json from the game's files, for the daily challenges: per
// campaign level (data/planets/*.json lists them, data/levels/<id>.json describes them) its
// revision (metadata.revision, the number the server registers), power slots, start powers,
// every power the level offers or asks for, and the times of its gold and platinum medals
// (the Silver and Gold tiers of an automatic challenge), its place in its planet (2.33: the
// weeks draw the 3rd / 4th / last level of a planet, the months the last one: the « Épreuves »
// and the final level); per power (data/powers) its input; the player's maximum health
// (data/player.json). The server draws the automatic challenges from this copy and never
// from client data.
//     node scripts/challenge-catalog.mjs
import { readdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const read = (...p) => JSON.parse(readFileSync(join(root, ...p), "utf8"));
const MAX_SLOTS = 8; // LevelData::kMaxPowerSlots
const DEFAULT_SLOTS = 4; // LevelData::kDefaultMaxPowers

const powers = {};
for (const f of readdirSync(join(root, "data", "powers")).sort()) {
    if (!f.endsWith(".json")) continue;
    const p = read("data", "powers", f);
    if (p.format !== "cosmic.power" || typeof p.id !== "string" || !/^[a-z0-9_]{1,64}$/.test(p.id)) continue;
    powers[p.id] = typeof p.input === "string" ? p.input : "";
}

const campaign = [];
for (const f of readdirSync(join(root, "data", "planets")).sort()) {
    if (!f.endsWith(".json")) continue;
    const planet = read("data", "planets", f);
    const list = planet.levels ?? [];
    list.forEach((id, i) => campaign.push({ id, planet: planet.id, order: i + 1, last: i === list.length - 1 }));
}

const levels = {};
for (const { id, planet, order, last } of campaign.sort((a, b) => (a.id < b.id ? -1 : a.id > b.id ? 1 : 0))) {
    const l = read("data", "levels", `${id}.json`);
    if (l.format !== "cosmic.level" || l.id !== id) throw new Error(`${id} : fichier de niveau inattendu`);
    const start = (l.startPowers ?? []).filter((p) => typeof p === "string" && p in powers);
    const offered = new Set(start);
    for (const o of l.objects ?? []) {
        const p = o?.props?.power;
        if ((o.type === "power_unlock" || o.type === "power_gate") && typeof p === "string" && p in powers) offered.add(p);
    }
    // As LevelData::powerSlots(): never fewer than the start powers; 0 = no limit.
    const maxPowers = Number.isInteger(l.maxPowers) ? l.maxPowers : DEFAULT_SLOTS;
    const slots = maxPowers <= 0 ? MAX_SLOTS : Math.min(MAX_SLOTS, Math.max(maxPowers, start.length));
    const gold = l.medals?.gold?.maxTime;
    const platinum = l.medals?.platinum?.maxTime;
    if (!(gold > 0) || !(platinum > 0)) throw new Error(`${id} : médailles d'or et de platine attendues`);
    const timerMode = l.timer?.mode ?? "unlimited";
    levels[id] = {
        name: l.name,
        planet,
        // Place in the planet (1 = first level) and whether it is the planet's last one.
        order,
        last,
        revision: Math.max(1, l.metadata?.revision | 0 || 1),
        slots,
        start_powers: start,
        level_powers: [...offered].sort(),
        silver_ms: Math.round(gold * 1000),
        gold_ms: Math.round(platinum * 1000),
        // EXTRÊME levels and levels with counted lives are never drawn automatically: their
        // rules already decide how many times the player may fall.
        auto: l.extreme !== true && timerMode !== "challenge" && !((l.lives | 0) > 0),
    };
}

const player = read("data", "player.json");
const out = { version: 2, player_max_hearts: Math.max(1, Math.min(20, player.maxHealth | 0)), powers, levels };
writeFileSync(join(root, "server", "src", "challengeCatalog.json"), JSON.stringify(out, null, 1) + "\n");
const auto = Object.values(levels).filter((l) => l.auto).length;
console.log(`${Object.keys(levels).length} niveaux (${auto} tirables), ${Object.keys(powers).length} pouvoirs`);
