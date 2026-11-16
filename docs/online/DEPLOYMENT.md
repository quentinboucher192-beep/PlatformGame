# Déploiement sur Cloudflare (staging et production)

Le Worker est configuré dans `server/wrangler.jsonc` :

| Environnement | Commande | Worker | D1 | R2 |
|---|---|---|---|---|
| développement | `npm run dev` (local) | `cosmic-online-dev` | `cosmic-online-dev` (local) | `cosmic-online-dev-files` (local) |
| staging | `npm run deploy:staging` | `cosmic-online-staging` | `cosmic-online-staging` | `cosmic-online-staging-files` |
| production | `npm run deploy:production` | `cosmic-online` | `cosmic-online-prod` | `cosmic-online-prod-files` |

Chaque environnement a ses propres données : un record de staging n'apparaît jamais en
production.

## Prérequis

* un compte Cloudflare ; Workers, D1 et R2 activés (R2 demande l'activation dans le tableau de
  bord) ;
* **offre Workers Paid recommandée** : le hachage des mots de passe (PBKDF2, 100 000
  itérations) consomme plus que les 10 ms de CPU par requête de l'offre gratuite lors de
  l'inscription, de la connexion et de la suppression de compte. Sur l'offre gratuite, la
  variable `PBKDF2_ITERATIONS` peut être abaissée (minimum 10 000, moins sûr) ; les empreintes
  existantes sont mises à niveau automatiquement à la connexion suivante quand la valeur
  remonte ;
* authentification de Wrangler : `npx wrangler login` (navigateur) ou un jeton d'API dans
  `CLOUDFLARE_API_TOKEN` (permissions : Workers Scripts Edit, D1 Edit, Workers R2 Storage
  Edit, Account Settings Read) — voir [SECRETS.md](SECRETS.md).

## Première mise en service (exemple : staging)

```
cd server
npm ci
npx wrangler whoami                                   # vérifie le compte utilisé
```

1. **Ressources** — `database_id` et les buckets sont facultatifs dans `wrangler.jsonc` : au
   premier `wrangler deploy`, Wrangler crée automatiquement la base D1 et le bucket R2
   manquants. Pour les créer vous-même (et fixer leur emplacement / juridiction) :
   ```
   npx wrangler d1 create cosmic-online-staging
   npx wrangler r2 bucket create cosmic-online-staging-files
   ```
   puis copiez le `database_id` affiché dans `env.staging.d1_databases[0].database_id`
   (un identifiant n'est pas un secret). Les buckets ne sont **jamais** rendus publics : les
   fichiers ne sont servis que par l'API après contrôle d'accès.
2. **Secrets** :
   ```
   npx wrangler secret put IP_HASH_SALT --env staging     # valeur aléatoire longue
   npx wrangler secret put ADMIN_TOKEN --env staging      # facultatif, ≥ 32 caractères
   ```
3. **Déploiement du code** : `npm run deploy:staging`.
4. **Migrations** (après le premier déploiement si la base a été créée automatiquement) :
   `npm run db:migrate:staging`.
5. **Niveaux classés** : enregistrez les révisions de niveaux de la version du jeu :
   ```
   CosmicPlatformer --online-manifest manifest.json
   ADMIN_TOKEN=<valeur> node scripts/register-levels.mjs manifest.json https://cosmic-online-staging.<sous-domaine>.workers.dev
   ```
   Derrière un proxy d'entreprise, Node n'utilise le proxy que avec
   `NODE_USE_ENV_PROXY=1` (variables `HTTPS_PROXY` / `NO_PROXY`).
6. **Vérification** : `curl https://cosmic-online-staging.<sous-domaine>.workers.dev/api/v1/health`.
7. **Le jeu** : renseignez l'adresse dans `data/online/config.json`
   (`servers.staging` ou `servers.production`, sans `/` final) et `environment` ; reconstruisez
   l'installateur. Seule l'adresse publique du Worker est distribuée : aucune clé ni secret.

Production : mêmes étapes sans `--env staging` (`npm run deploy:production`,
`npm run db:migrate:production`, secrets avec `wrangler secret put ... ` sans `--env`).

## État actuel

| Environnement | Adresse | État |
|---|---|---|
| staging | `https://cosmic-online-staging.xpg-licences-workersdev.workers.dev` | déployé (D1 `cosmic-online-staging`, R2 `cosmic-online-staging-files`, migrations 0001–0013 : social, niveaux de la communauté, **révision 4 des 12 niveaux du jeu 2.2.0** (`0005_levels_2_2_0.sql`), **révision 5 du jeu 2.3.0** (`0006_levels_2_3_0.sql`, emplacements de pouvoirs), table des installateurs (`0007_client_releases.sql`, routes `/client/latest` et `/client/download`), installateur 2.4.0 (`0008_release_2_4_0.sql`, `scripts/release-migration.mjs`) et **révision 6 du jeu 2.5.0** (`0009_levels_2_5_0.sql`, nouveaux pouvoirs, boss qui gardent leurs blessures), installateurs 2.5.0 et 2.6.0 (`0010_release_2_5_0.sql`, `0011_release_2_6_0.sql`) et niveaux du Noyau stellaire, révision 7 du jeu 2.7.0 (`0012_levels_2_7_0.sql`) et son installateur (`0013_release_2_7_0.sql`) ; les migrations de niveaux sont générées par `scripts/levels-migration.mjs` depuis `CosmicPlatformer --online-manifest`, sans secret d'administration), `IP_HASH_SALT` défini, pas d'`ADMIN_TOKEN` (les revues manuelles ne sont donc pas possibles tant que vous n'en définissez pas un) |
| production | — | non déployée : `npm run deploy:production` avec un fichier de secrets au premier déploiement (`--secrets-file`), puis `npm run db:migrate:production` et l'enregistrement des niveaux |

Premier déploiement d'un Worker qui n'existe pas encore : les secrets déclarés comme requis
doivent être fournis avec `wrangler deploy --secrets-file <fichier>` (lignes `NOM=valeur`,
fichier hors du dépôt, supprimé après usage). Le bucket R2 doit exister (activation de R2
dans le tableau de bord, puis `wrangler r2 bucket create <nom>`).

## Nouvelle version du jeu

* Un niveau modifié change d'empreinte : incrémentez `metadata.revision` dans son fichier
  JSON (le serveur refuse une révision existante avec un contenu différent), générez le
  manifeste, enregistrez-le. L'ancienne révision devient *legacy* : ses classements restent
  consultables (`?level_version=N`) mais ne reçoivent plus de records et ne sont jamais
  mélangés avec la nouvelle.
* `MIN_GAME_VERSION` (variable de `wrangler.jsonc`) refuse les records des versions trop
  anciennes du jeu (réponse 426).
* Migrations : ajoutez un **nouveau** fichier `migrations/000N_*.sql` (ne modifiez jamais une
  migration appliquée), testez en local (`npm test` applique toutes les migrations), puis
  `db:migrate:staging`, puis production.

## Exploitation

* **Journaux et traces** : `observability` est activé (logs JSON structurés, traces
  échantillonnées). `npx wrangler tail --env staging` pour suivre en direct. Les journaux ne
  contiennent jamais de jeton, de mot de passe ni d'adresse IP.
* **Tâche planifiée** quotidienne (03:17 UTC) : purge des sessions expirées, des compteurs de
  limitation et du journal d'audit de plus de 180 jours.
* **Modération** (avec `ADMIN_TOKEN`) :
  ```
  curl -H "Authorization: Bearer $ADMIN_TOKEN" "$URL/api/v1/admin/records?status=PENDING"
  curl -X POST -H "Authorization: Bearer $ADMIN_TOKEN" -H "Content-Type: application/json" \
       -d '{"decision":"VERIFIED","reason":"Replay vérifié"}' "$URL/api/v1/admin/records/<id>/review"
  ```
* **Cache** : les classements et le globe consultés sans session sont mis en cache 15 s au bord
  du réseau (`EDGE_CACHE_SECONDS`, désactivé en développement).
* **Domaine personnalisé** : ajoutez une route `custom_domain` dans l'environnement voulu ;
  pensez à mettre à jour `config.json`.
* **Sauvegardes** : la tâche planifiée fait aussi chaque nuit une copie complète (base et
  fichiers R2 des joueurs), qu'un compte admin peut télécharger chiffrée et réimporter
  (ADMINISTRATION › Sauvegardes, voir [BACKUP.md](BACKUP.md), y compris la restauration
  manuelle avec wrangler). D1 propose en plus la restauration à un instant donné (Time
  Travel, 30 jours) ; un export SQL reste possible :
  `npx wrangler d1 export cosmic-online-prod --remote --output backup.sql`.

## Retour arrière

`npx wrangler rollback --env staging` revient à la version précédente du Worker. Les
migrations ne sont pas annulées automatiquement : une migration doit rester compatible avec
le code précédent le temps du déploiement.


## Enregistrer les niveaux d'une nouvelle version du jeu sans secret

```bash
CosmicPlatformer --online-manifest manifest.json
```

```bash
node scripts/levels-migration.mjs manifest.json > migrations/0006_levels_X_Y_Z.sql
```

```bash
npx wrangler d1 migrations apply cosmic-online-staging --remote --env staging
```

La migration a le même effet que `POST /admin/levels` : les nouvelles révisions deviennent
courantes, les anciennes passent « legacy », une révision existante n'est jamais modifiée.


## Défis du jour, de la semaine et du mois (2.32, 2.33)

Les défis (contrats : [DAILY_CHALLENGES.md](DAILY_CHALLENGES.md) et
[CHALLENGES_PERIODS.md](CHALLENGES_PERIODS.md), routes : API.md §8.9 duodecies) ont besoin des
migrations `0049_daily_challenges.sql` (2.32) et `0058_challenge_periods.sql` (2.33 : la table
`daily_challenges` devient `challenges`, avec les semaines et les mois, et `challenge_runs`
gagne `late` ; les défis et les parties existants sont recopiés tels quels) puis du Worker. La
migration 0054 reconstruit les deux tables : appliquez-la **avant** de déployer le Worker 2.33
(l'ancien Worker lit `daily_challenges` et ne fonctionne plus après elle ; le nouveau a besoin
d'elle), c'est-à-dire migration puis déploiement l'un juste après l'autre. La tâche planifiée
quotidienne (`17 3 * * *`) et chaque lecture des défis en cours tirent ensuite les défis
d'aujourd'hui à J+7, de la semaine en cours à S+2 et du mois en cours à M+1. Aucun secret
n'est nécessaire.

**« Plus loin » (2.34)** : migration `0059_challenge_progress.sql` (nouvelle table
`challenge_attempts`, rien d'autre ne change : elle peut être appliquée avant le Worker). Un
défi n'a ce classement que si la version de son niveau a un tracé (`level_versions.race`,
écrit par `levels-migration.mjs` quand le manifeste du jeu en donne un ;
`community_level_versions.race` pour un niveau EXTRÊME de la communauté). Aujourd'hui seul
`noyau_01` en a un dans la campagne : pour un défi « Plus loin » sur la campagne, choisissez-le
avec `challenge-set.mjs`, ou un niveau EXTRÊME de la communauté. Vérifier qu'un défi l'a :
`progress_available` dans `GET /api/v1/challenges/{id}`.

**Catalogue des niveaux.** Le tirage automatique ne prend un niveau de la campagne que si sa
révision active sur le serveur est celle de `src/challengeCatalog.json` (version 2 depuis
2.33 : il donne aussi la place de chaque niveau dans sa planète, pour les semaines et les
mois). À chaque nouvelle révision des niveaux (migration `levels-migration.mjs`),
régénérez-le depuis les fichiers du jeu et redéployez le Worker :

```bash
node scripts/challenge-catalog.mjs
```

Sans niveau utilisable pour une sorte, aucun défi de cette sorte n'est tiré (journal
`challenge_draw_no_level` avec `kind` et `period`) jusqu'au déploiement du catalogue à jour ;
le défi manquant est alors tiré à la lecture suivante.

**Choisir un défi à venir** — d'un jour, d'une semaine ISO ou d'un mois — (niveau de la
campagne ou version publiée d'un niveau ONLINE LEVELS ; un niveau de la communauté ou le boss
rush ne sont jamais tirés automatiquement, seul ce script les choisit) :

```bash
node scripts/challenge-set.mjs --kind daily --period 2026-11-20 --level campaign:glacior_02 --hearts 3 \
    > migrations/0055_challenge_daily_2026_11_20.sql
node scripts/challenge-set.mjs --kind weekly --period 2026-W49 --level campaign:noyau_01 --hearts 5 \
    > migrations/0056_challenge_weekly_2026_W49.sql
node scripts/challenge-set.mjs --kind monthly --period 2027-01 --level community:<uuid>@<version> \
    --silver 2:30 --gold 1:50 > migrations/0057_challenge_monthly_2027_01.sql
npx wrangler d1 migrations apply cosmic-online-staging --remote --env staging
```

`--period` : `AAAA-MM-JJ` (jour), `AAAA-Www` (semaine ISO 8601, du lundi au dimanche, UTC) ou
`AAAA-MM` (mois). L'ancienne forme `--date AAAA-MM-JJ` reste acceptée pour un jour (`--kind`
vaut `daily` par défaut). Options : `--hearts N`, `--slots N`, `--powers a,b`,
`--no-stations`, `--no-checkpoints`, `--silver m:ss`, `--gold m:ss`, `--gold-damage-ok`. Le
script refuse une période commencée ou passée (ou qui n'existe pas, comme `2027-W53`) et
affiche les dates de la période, les règles, le nom de fichier conseillé et des
avertissements (pouvoirs de départ absents, moins d'emplacements que le niveau…). La migration
ne modifie le défi que si sa période n'a pas commencé **au moment où elle est appliquée**, et
un niveau de la communauté doit alors avoir cette version publiée ; sinon elle ne change rien,
sans échouer. Quand la période commence, l'auteur d'un niveau de la communauté reçoit la
notification « … est le défi du jour / de la semaine / du mois ! ». Le résultat est dans
`audit_log` (`challenge_set_applied` ou `challenge_set_skipped`) :

```bash
npx wrangler d1 execute cosmic-online-staging --remote --env staging \
    --command "SELECT action, details, created_at FROM audit_log WHERE action LIKE 'challenge_set_%' ORDER BY id DESC LIMIT 5"
```
