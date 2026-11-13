# Installation et développement local

Tout fonctionne en local, **sans compte Cloudflare** : `wrangler dev` exécute le Worker dans
workerd (Miniflare) avec une base D1 et un stockage R2 simulés sur disque
(`server/.wrangler/state`).

## Prérequis

| Composant | Version |
|---|---|
| Node.js | 22 (npm 10) |
| Jeu | compilé selon [../BUILDING.md](../BUILDING.md) |
| Linux | `libcurl4-openssl-dev` pour le client HTTP du jeu (`sudo apt install libcurl4-openssl-dev`) |
| Windows | rien de plus : le jeu utilise WinHTTP, fourni avec Windows |

Sans libcurl, le jeu Linux se compile quand même (avertissement CMake) : Cosmic Online indique
alors « Réseau indisponible » et tout le reste fonctionne.

## 1. Serveur local

```
cd server
./scripts/dev-setup.sh --levels
```

Sous Windows : `powershell -ExecutionPolicy Bypass -File scripts\dev-setup.ps1 -Levels`.

Le script :

1. installe les outils (`npm ci` : wrangler, vitest, TypeScript — aucune dépendance n'est
   embarquée dans le Worker) ;
2. crée `server/.dev.vars` avec des secrets **locaux** aléatoires (`IP_HASH_SALT`,
   `ADMIN_TOKEN`) — fichier ignoré par Git ;
3. applique les migrations D1 en local (`migrations/0001_initial_schema.sql`,
   `0002_countries.sql`) ;
4. démarre le serveur sur `http://127.0.0.1:8787` ;
5. avec `--levels` : demande au jeu le manifeste de ses niveaux
   (`CosmicPlatformer --online-manifest`) et les enregistre sur le serveur
   (`scripts/register-levels.mjs`, route d'administration). Chemin du jeu modifiable avec
   `COSMIC_GAME_BIN`.

Vérification : `curl http://127.0.0.1:8787/api/v1/health`.

Commandes utiles (`server/package.json`) :

| Commande | Rôle |
|---|---|
| `npm run dev` | serveur local (environnement `dev`) |
| `npm test` | 55 tests du Worker (D1 / R2 locaux, aucune connexion requise) |
| `npm run typecheck` | vérification TypeScript |
| `npm run db:migrate:local` | migrations locales |
| `npm run types` | régénère `worker-configuration.d.ts` après un changement de `wrangler.jsonc` |

## 2. Jeu connecté au serveur local

L'adresse du serveur est choisie dans cet ordre :

1. `--online-server <url>` en ligne de commande ;
2. variable d'environnement `COSMIC_ONLINE_URL` ;
3. réglage avancé `online.serverOverride` de `settings.json` ;
4. `data/online/config.json`, entrée de l'environnement `environment` (ou
   `COSMIC_ONLINE_ENV=development|staging|production`).

Seules les adresses **HTTPS** sont acceptées, sauf la machine locale (`http://127.0.0.1`,
`http://localhost`).

```
CosmicPlatformer --online-server http://127.0.0.1:8787
# ou
COSMIC_ONLINE_ENV=development CosmicPlatformer
```

Menu principal → **Online** (un compte est demandé pour cette section). Au premier passage, l'écran d'accueil explique ce qui
est partagé et propose de créer un compte, de se connecter ou de rester hors ligne.

## 3. Tests

```
ctest --test-dir build --output-on-failure            # tous les tests du jeu
build/bin/cosmic_tests online                         # module en ligne uniquement
build/bin/cosmic_tests globe                          # logique du globe
```

Test d'intégration de bout en bout (deux joueurs jouent un niveau de test, leurs runs sont
vérifiés par le serveur grâce aux replays, classés par D1 et placés sur le globe) :

```
COSMIC_ONLINE_TEST_URL=http://127.0.0.1:8787 \
COSMIC_ONLINE_TEST_ADMIN_TOKEN=<ADMIN_TOKEN de server/.dev.vars> \
build/bin/cosmic_tests "online live"
```

Sans ces variables, ce test est ignoré (le reste de la suite n'a besoin d'aucun serveur : un
faux serveur scripté et un serveur TCP local volontairement défaillant couvrent les erreurs
réseau).

## 4. Données d'essai (serveur local uniquement)

Pour essayer l'écran et le globe avec des données qui ont suivi tout le circuit :

```
COSMIC_ONLINE_TEST_URL=http://127.0.0.1:8787 \
COSMIC_ONLINE_TEST_ADMIN_TOKEN=<ADMIN_TOKEN> \
COSMIC_ONLINE_TEST_POPULATE=1 \
COSMIC_ONLINE_TEST_SESSION_DIR=$HOME/cosmic-demo \
build/bin/cosmic_tests populate
CosmicPlatformer --online-server http://127.0.0.1:8787 --user-dir $HOME/cosmic-demo --online online_test_corridor
```

Une vingtaine de comptes de test jouent réellement (simulation du moteur) un couloir de test
enregistré sur le serveur local ; le dossier `cosmic-demo` reçoit la session du compte
« Astra » et une copie du niveau pour regarder les replays. **Ne jamais pointer ces
variables vers un serveur de production** : ce sont des comptes de test.

## 5. Options de vérification automatique

| Option | Effet |
|---|---|
| `--online [<niveau>]` | démarre sur l'écran ONLINE RANKING |
| `--online-focus <pseudo>` | sélectionne le marqueur de ce joueur sur le globe |
| `--online-open register[=pseudo]\|login\|settings\|friends\|profile:<pseudo>\|replay:<pseudo>` | ouvre la fenêtre correspondante (`register=Nom` préremplit le pseudonyme) |
| `--online-manifest <fichier>` | écrit le manifeste des niveaux (sans fenêtre) |

Avec `--headless --frames N --shot K:capture.png`, elles servent aux captures d'écran
automatiques.
