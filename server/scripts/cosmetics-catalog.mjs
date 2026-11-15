// Writes src/cosmeticCatalog.json from the game's Collection (data/cosmetics/cosmetics.json):
// the server checks itself what a player may wear online (stars and achievements declared
// in player_achievements) and only ever shows names from this copy, never client text.
//     node scripts/cosmetics-catalog.mjs
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const data = JSON.parse(readFileSync(join(root, "data", "cosmetics", "cosmetics.json"), "utf8"));
const categories = data.categories.map((c) => c.id);
const items = [];
for (const i of data.items) {
    if (!categories.includes(i.category) || items.some((o) => o.id === i.id)) continue; // as the game: the first wins
    const item = { id: i.id, category: i.category, look: i.look ?? "", name: i.name };
    if (i.default === true) item.default = true;
    else if (typeof i.achievement === "string" && i.achievement) item.achievement = i.achievement;
    else if (i.stars === "all") item.stars = "all";
    else item.stars = Math.max(0, i.stars | 0);
    items.push(item);
}
for (const c of categories)
    if (items.filter((i) => i.category === c && i.default).length !== 1) throw new Error(`catégorie ${c} : un seul élément par défaut attendu`);
writeFileSync(join(root, "server", "src", "cosmeticCatalog.json"), JSON.stringify({ version: data.version, categories, items }, null, 1) + "\n");
console.log(`${items.length} récompenses, ${categories.length} catégories`);
