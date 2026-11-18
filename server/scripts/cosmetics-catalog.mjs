// Writes src/cosmeticCatalog.json from the game's Collection (data/cosmetics/cosmetics.json):
// the server checks itself what a player may wear online (stars and achievements declared
// in player_achievements; from 2.34, the challenge results it verified itself) and only ever
// shows names from this copy, never client text.
//     node scripts/cosmetics-catalog.mjs
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const data = JSON.parse(readFileSync(join(root, "data", "cosmetics", "cosmetics.json"), "utf8"));
const categories = data.categories.map((c) => c.id);
const KINDS = ["daily", "weekly", "monthly"];
const TIERS = ["bronze", "silver", "gold"];
const count = (v) => (Number.isInteger(v) && v >= 1 && v <= 10000 ? v : null);
const items = [];
for (const i of data.items) {
    if (!categories.includes(i.category) || items.some((o) => o.id === i.id)) continue; // as the game: the first wins
    // 2.64: a souvenir is given on the player's computer (the tutorial finished): the server cannot
    // check it, so it never knows it (worn online, the category's default shows instead).
    if (typeof i.souvenir === "string" && i.souvenir) continue;
    const item = { id: i.id, category: i.category, look: i.look ?? "", name: i.name };
    if (i.default === true) item.default = true;
    else if (typeof i.raid === "string" && i.raid) item.raid = i.raid; // 2.68: a raid's reward, only once claimed (player_won_rewards)
    else if (typeof i.achievement === "string" && i.achievement) item.achievement = i.achievement;
    else if (i.challenge !== undefined) {
        // 2.34: N challenges of a kind (or any) with a verified run at a tier or better.
        const c = i.challenge;
        if (!c || ![...KINDS, "any"].includes(c.kind) || !TIERS.includes(c.tier) || count(c.count) === null)
            throw new Error(`${i.id} : condition de défi invalide`);
        item.challenge = { kind: c.kind, tier: c.tier, count: c.count };
    } else if (i.challengeStreak !== undefined) {
        // 2.34: N periods in a row with a verified run received in time.
        const c = i.challengeStreak;
        if (!c || !KINDS.includes(c.kind) || count(c.days) === null) throw new Error(`${i.id} : série de défis invalide`);
        item.challengeStreak = { kind: c.kind, days: c.days };
    } else if (i.stars === "all") item.stars = "all";
    else item.stars = Math.max(0, i.stars | 0);
    items.push(item);
}
for (const c of categories)
    if (items.filter((i) => i.category === c && i.default).length !== 1) throw new Error(`catégorie ${c} : un seul élément par défaut attendu`);
writeFileSync(join(root, "server", "src", "cosmeticCatalog.json"), JSON.stringify({ version: data.version, categories, items }, null, 1) + "\n");
console.log(`${items.length} récompenses, ${categories.length} catégories`);
