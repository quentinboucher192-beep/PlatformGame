# Secrets de Cosmic Online

Règles :

* **aucun secret dans le dépôt Git** ni dans `wrangler.jsonc` (seulement des variables non
  secrètes : environnement, durées de vie, version des conditions, version minimale du jeu) ;
* **aucun secret dans le jeu** : le client ne connaît que l'adresse publique du serveur ; il
  n'existe pas de clé d'API « du jeu ». Les joueurs s'authentifient avec leur compte ;
* `.dev.vars` (secrets locaux) est ignoré par Git ; seul `.dev.vars.example` est versionné.

## Secrets du Worker

| Nom | Obligatoire | Rôle | Valeur |
|---|---|---|---|
| `IP_HASH_SALT` | oui | sel des empreintes d'adresses IP (limitation de débit, journal d'audit) : aucune IP n'est stockée en clair | aléatoire, ≥ 32 caractères |
| `ADMIN_TOKEN` | non | active les routes `/api/v1/admin/*` (enregistrement des niveaux, modération). Sans lui (ou s'il fait moins de 32 caractères) ces routes répondent 404 | aléatoire, ≥ 32 caractères, comparé en temps constant |

Création / rotation :

```
openssl rand -hex 32 | npx wrangler secret put IP_HASH_SALT --env staging
openssl rand -hex 32 | npx wrangler secret put ADMIN_TOKEN --env staging
npx wrangler secret list --env staging
```

(sans `--env` pour la production). Changer `IP_HASH_SALT` remet simplement à zéro les
compteurs de limitation en cours. Changer `ADMIN_TOKEN` invalide immédiatement l'ancien jeton
des opérateurs.

En local, `server/scripts/dev-setup.sh` génère `.dev.vars` avec des valeurs aléatoires propres
à votre machine. Les tests utilisent des valeurs factices déclarées dans `vitest.config.ts`.

## Variables non secrètes (`wrangler.jsonc`, par environnement)

| Nom | Défaut | Rôle |
|---|---|---|
| `ENVIRONMENT` | development / staging / production | affiché par `/health` |
| `TOS_VERSION` | `2026-11` | version des conditions à accepter à l'inscription (doit correspondre à `data/online/config.json` et `terms_fr.txt`) |
| `MIN_GAME_VERSION` | `2.1.0` | version minimale du jeu pour envoyer des records |
| `ACCESS_TOKEN_TTL_SECONDS` | `3600` | durée de vie d'un jeton d'accès |
| `REFRESH_TOKEN_TTL_DAYS` | `30` | durée de vie d'une session sans activité |
| `RATE_LIMIT_SCALE` | `50` en dev uniquement | multiplie les limites (essais locaux répétés) |
| `EDGE_CACHE_SECONDS` | `15` (0 en dev) | cache des lectures anonymes |
| `PBKDF2_ITERATIONS` | `100000` | coût du hachage des mots de passe (10 000 à 100 000) |

## Jeton Cloudflare pour déployer

Pour `wrangler deploy` depuis une machine d'intégration continue, créez un jeton d'API
Cloudflare limité au compte concerné avec les permissions *Workers Scripts : Edit*,
*D1 : Edit*, *Workers R2 Storage : Edit*, *Account Settings : Read*, et fournissez-le dans la
variable d'environnement `CLOUDFLARE_API_TOKEN` (secret de la CI, jamais dans un fichier).
En poste de travail, préférez `npx wrangler login`.

## Côté joueur

* Le mot de passe n'est jamais enregistré par le jeu.
* La « connexion automatique » conserve uniquement le jeton de renouvellement de session :
  chiffré avec DPAPI (lié au compte Windows) sous Windows, dans un fichier lisible seulement
  par l'utilisateur (`0600`) sous Linux (`<dossier utilisateur>/online/session.json`). Le jeton
  d'accès reste en mémoire.
* Sur le serveur, jetons d'accès et de renouvellement ne sont stockés que sous forme
  d'empreinte SHA-256 ; un jeton de renouvellement déjà utilisé et présenté à nouveau hors du
  délai de grâce (120 s) révoque toute la session.
