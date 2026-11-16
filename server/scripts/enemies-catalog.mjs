// Writes src/enemyCatalog.json from the game's native enemies (data/enemies/*.json): the server
// only accepts a revision of an entity shipped with the game (ADMINISTRATION › Ennemis,
// docs/online/ENEMY_REVISIONS.md) and only shows names from this copy.
//     node scripts/enemies-catalog.mjs
import { readFileSync, readdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const dir = join(root, "data", "enemies");
const enemies = [];
for (const f of readdirSync(dir).filter((f) => f.endsWith(".json")).sort()) {
    const d = JSON.parse(readFileSync(join(dir, f), "utf8"));
    if (d.format !== "cosmic.enemy" || typeof d.id !== "string" || !/^[a-z0-9_]{1,64}$/.test(d.id)) throw new Error(`${f} : entité invalide`);
    if (enemies.some((e) => e.id === d.id)) throw new Error(`${d.id} : identifiant en double`);
    enemies.push({ id: d.id, name: typeof d.name === "string" ? d.name : d.id });
}
writeFileSync(join(root, "server", "src", "enemyCatalog.json"), JSON.stringify({ enemies }, null, 1) + "\n");
console.log(`${enemies.length} entités natives écrites dans src/enemyCatalog.json`);
