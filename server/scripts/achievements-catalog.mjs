// Writes src/achievementCatalog.json (achievement id -> stars) from the game's catalogue
// (data/achievements/achievements.json), so that the server counts the stars itself. 2.41:
// also the steps of the profile badges (stars -> slots), the same numbers as the game's.
//     node scripts/achievements-catalog.mjs
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const data = JSON.parse(readFileSync(join(root, "data", "achievements", "achievements.json"), "utf8"));
const stars = {};
for (const a of data.achievements) stars[a.id] = Math.max(1, Math.min(5, a.stars | 0));
// Profile badges (2.41): from `stars` stars, `slots` badges; steps in increasing order.
const badgeSlots = (data.badgeSlots ?? []).map((b) => ({ stars: b.stars, slots: b.slots }));
for (let i = 0; i < badgeSlots.length; ++i) {
    const b = badgeSlots[i];
    if (!Number.isInteger(b.stars) || b.stars < 1 || !Number.isInteger(b.slots) || b.slots < 1 || b.slots > 10) throw new Error(`badgeSlots[${i}] invalide`);
    if (i > 0 && (b.stars <= badgeSlots[i - 1].stars || b.slots <= badgeSlots[i - 1].slots)) throw new Error("badgeSlots : paliers croissants attendus");
}
writeFileSync(join(root, "server", "src", "achievementCatalog.json"), JSON.stringify({ version: data.version, stars, badgeSlots }, null, 1) + "\n");
console.log(`${Object.keys(stars).length} succès, ${Object.values(stars).reduce((s, v) => s + v, 0)} étoiles`);
