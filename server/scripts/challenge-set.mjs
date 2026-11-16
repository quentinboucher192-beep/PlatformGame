// The administrator chooses a coming challenge — of a day, of an ISO week or of a month —
// (docs/online/DAILY_CHALLENGES.md, docs/online/CHALLENGES_PERIODS.md):
//   node scripts/challenge-set.mjs --kind daily --period 2026-11-20 --level campaign:glacior_02 \
//       [--hearts N] [--slots N] [--powers a,b] [--no-stations] [--no-checkpoints] \
//       [--silver m:ss] [--gold m:ss] [--gold-damage-ok] \
//       [--objective speed|finish] [--bronze-pct N] [--silver-pct N] > migrations/00NN_challenge_daily_2026_11_20.sql
//   node scripts/challenge-set.mjs --kind weekly --period 2026-W48 --level campaign:pyralis_04 > …
//   node scripts/challenge-set.mjs --kind monthly --period 2026-12 --level community:<uuid>@<version> --silver 2:30 --gold 1:50 > …
// (the 2.32 form `--date AAAA-MM-JJ` still means `--kind daily --period AAAA-MM-JJ`), then
// apply it like the releases (no administration secret anywhere):
//   npx wrangler d1 migrations apply cosmic-online-staging --remote --env staging
// A period that has begun is refused, and the SQL itself only changes a challenge whose
// period has not begun when it is applied (audit_log says challenge_set_applied or
// challenge_set_skipped). Defaults: a campaign level keeps its slots, its start powers and
// the times of its gold and platinum medals (src/challengeCatalog.json,
// scripts/challenge-catalog.mjs); a community level gets 4 slots and no imposed power, and
// needs --silver and --gold. Hearts: the player's maximum. Stations and checkpoints on, Gold
// without damage. 2.41: `--objective finish` (« Finir le niveau », only on a level version
// with a race layout: else the SQL changes nothing) ranks the progress like « Plus loin », its
// tiers a share of the route (--bronze-pct, default 50; --silver-pct, default 80; Or: finished).
// The same checks as ADMINISTRATION › Calendrier des défis in the game (src/challengeSpec.mjs),
// which is the usual way now: this script stays for a migration written by hand.
import { readdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { buildChallengeSql, parseArgs } from "./challenge-sql.mjs";

const server = join(dirname(fileURLToPath(import.meta.url)), "..");
try {
    const opts = parseArgs(process.argv.slice(2));
    const catalog = JSON.parse(readFileSync(join(server, "src", "challengeCatalog.json"), "utf8"));
    const today = new Date().toISOString().slice(0, 10);
    const { sql, id, start, end, canonical, warnings } = await buildChallengeSql(opts, catalog, today);
    process.stdout.write(sql);
    const numbers = readdirSync(join(server, "migrations"))
        .map((f) => /^(\d{4})_/.exec(f)?.[1])
        .filter(Boolean)
        .map(Number);
    const next = String(Math.max(0, ...numbers) + 1).padStart(4, "0");
    console.error(`Défi ${id} : du ${start} 00:00 UTC au ${end} 00:00 UTC.`);
    console.error(`Règles : ${canonical}`);
    for (const w of warnings) console.error(`Attention : ${w}.`);
    console.error(`À enregistrer dans migrations/${next}_challenge_${id.replace(/-/g, "_")}.sql puis appliquer (wrangler d1 migrations apply).`);
} catch (e) {
    console.error(`Erreur : ${e instanceof Error ? e.message : String(e)}`);
    process.exit(2);
}
