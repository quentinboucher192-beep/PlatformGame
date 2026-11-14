// Writes src/achievementCatalog.json (achievement id -> stars) from the game's catalogue
// (data/achievements/achievements.json), so that the server counts the stars itself.
//     node scripts/achievements-catalog.mjs
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const data = JSON.parse(readFileSync(join(root, "data", "achievements", "achievements.json"), "utf8"));
const stars = {};
for (const a of data.achievements) stars[a.id] = Math.max(1, Math.min(5, a.stars | 0));
writeFileSync(join(root, "server", "src", "achievementCatalog.json"), JSON.stringify({ version: data.version, stars }, null, 1) + "\n");
console.log(`${Object.keys(stars).length} succès, ${Object.values(stars).reduce((s, v) => s + v, 0)} étoiles`);
