// Registers the level revisions of a game release on a Cosmic Online server.
//   1. CosmicPlatformer --online-manifest manifest.json
//   2. ADMIN_TOKEN=... node scripts/register-levels.mjs manifest.json https://<server>
// The admin token is read from the environment (or .dev.vars for the local server), never
// from the command line, so it does not end up in the shell history.
import { existsSync, readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const [manifestPath, serverArg] = process.argv.slice(2);
if (!manifestPath) {
    console.error("Usage : node scripts/register-levels.mjs <manifest.json> [url du serveur]");
    process.exit(2);
}
const server = (serverArg ?? "http://127.0.0.1:8787").replace(/\/+$/, "");
let token = process.env.ADMIN_TOKEN ?? "";
const devVars = resolve(dirname(fileURLToPath(import.meta.url)), "../.dev.vars");
if (!token && /^http:\/\/(127\.0\.0\.1|localhost)(:\d+)?$/.test(server) && existsSync(devVars)) {
    const m = /^ADMIN_TOKEN=(.+)$/m.exec(readFileSync(devVars, "utf8"));
    if (m) token = m[1].trim();
}
if (!token) {
    console.error("ADMIN_TOKEN manquant (variable d'environnement, ou .dev.vars pour le serveur local).");
    process.exit(2);
}
const manifest = JSON.parse(readFileSync(manifestPath, "utf8"));
if (manifest.format !== "cosmic.online_manifest") {
    console.error("Ce fichier n'est pas un manifeste produit par --online-manifest.");
    process.exit(2);
}
const res = await fetch(`${server}/api/v1/admin/levels`, {
    method: "POST",
    headers: { "content-type": "application/json", authorization: `Bearer ${token}` },
    body: JSON.stringify({ levels: manifest.levels, game_version: manifest.game_version }),
});
const body = await res.json().catch(() => null);
if (!res.ok || !body?.ok) {
    console.error(`Échec (${res.status}) :`, JSON.stringify(body?.error ?? body));
    process.exit(1);
}
for (const l of body.data.levels) console.log(`${l.id.padEnd(16)} révision ${l.version} : ${l.result}`);
