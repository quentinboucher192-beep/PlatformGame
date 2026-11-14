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
| staging | `https://cosmic-online-staging.xpg-licences-workersdev.workers.dev` | déployé (D1 `cosmic-online-staging`, R2 `cosmic-online-staging-files`, migrations 0001–0010 : social, niveaux de la communauté, **révision 4 des 12 niveaux du jeu 2.2.0** (`0005_levels_2_2_0.sql`), **révision 5 du jeu 2.3.0** (`0006_levels_2_3_0.sql`, emplacements de pouvoirs), table des installateurs (`0007_client_releases.sql`, routes `/client/latest` et `/client/download`), installateur 2.4.0 (`0008_release_2_4_0.sql`, `scripts/release-migration.mjs`) et **révision 6 du jeu 2.5.0** (`0009_levels_2_5_0.sql`, nouveaux pouvoirs, boss qui gardent leurs blessures), installateur 2.5.0 (`0010_release_2_5_0.sql`) ; les migrations de niveaux sont générées par `scripts/levels-migration.mjs` depuis `CosmicPlatformer --online-manifest`, sans secret d'administration), `IP_HASH_SALT` défini, pas d'`ADMIN_TOKEN` (les revues manuelles ne sont donc pas possibles tant que vous n'en définissez pas un) |
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
* **Sauvegardes** : D1 propose la restauration à un instant donné (Time Travel) ; exportez
  aussi régulièrement : `npx wrangler d1 export cosmic-online-prod --remote --output backup.sql`.

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
