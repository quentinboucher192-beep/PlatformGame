[← Retour : Cosmic Online](README.md) · [Accueil (README)](../../README.md)

# API Cosmic Online (v1)

Référence de l'API REST du classement en ligne de Cosmic Platformer, implémentée par le
Worker Cloudflare de `server/src` (TypeScript, D1 pour les données, R2 privé pour les replays
et avatars). Ce document décrit ce que fait le code ; la description machine (OpenAPI 3.1,
schémas complets et exemples validés) est dans [`server/openapi.yaml`](../../server/openapi.yaml).

Dans les exemples : `API=http://127.0.0.1:8787/api/v1` et `TOKEN` contient un jeton d'accès.

## 1. Conventions

### URL de base

Toutes les routes sont sous `/api/v1`.

| Environnement | URL de base | Lancement |
|---|---|---|
| Développement | `http://127.0.0.1:8787/api/v1` | `npm run dev` (`wrangler dev --env dev`) |
| Préproduction | `https://<votre Worker déployé>/api/v1` | `wrangler deploy --env staging` |
| Production | `https://<votre Worker déployé>/api/v1` | `wrangler deploy` |

### Enveloppes JSON

Toute réponse JSON a la même forme. Succès :

```json
{ "ok": true, "data": { … }, "meta": { … }, "request_id": "c4c16a63-d3a9-450b-b73a-925f325b55b0" }
```

`meta` n'existe que sur les listes paginées. Erreur :

```json
{ "ok": false, "error": { "code": "VALIDATION_ERROR", "message": "Données invalides.",
  "details": [{ "field": "time_ms", "message": "doit être un entier" }] }, "request_id": "c4c16a63-…" }
```

- `error.code` est stable (à tester côté client) ; `error.message` est en français et ne
  contient jamais de détail interne ; `details` est facultatif (voir §5).
- `request_id` (UUID) identifie la requête dans les journaux du Worker. Une réponse servie par
  le cache de périphérie répète le `request_id` de la requête qui l'a remplie.
- En-têtes de toute réponse JSON : `Content-Type: application/json; charset=utf-8`,
  `Cache-Control: no-store` (sauf cache, ci-dessous), `X-Content-Type-Options: nosniff`,
  `Referrer-Policy: no-referrer`.
- Les réponses binaires (avatar PNG, replay CPRZ) n'ont pas d'enveloppe ni de `request_id`.

### Corps des requêtes

- `Content-Type: application/json` obligatoire (sinon `415 UNSUPPORTED_MEDIA_TYPE`), corps =
  un **objet** JSON UTF-8 (sinon `400 INVALID_JSON`), taille limitée (§4).
- Validation stricte et complète : un champ inconnu, un type faux, une valeur hors bornes ou un
  caractère de contrôle donnent `400 VALIDATION_ERROR` avec **toutes** les erreurs dans
  `details` (`champ inconnu`, `obligatoire`, `doit être une chaîne`, `format invalide`,
  `longueur attendue entre 3 et 20`, `doit être entre 1 et 100`, `doit être un booléen`,
  `valeur attendue : …`, `caractères de contrôle interdits`…).
- Une valeur `null` vaut « absent », sauf `country_code` / `region_code` de
  `PATCH /players/me` où `null` efface la valeur.

### Pagination

Paramètres `limit` (défaut 25, max 100 ; 50/200 pour l'historique d'un joueur et la
modération) et `offset` (0 à 10 000). Une valeur non numérique ou hors bornes donne `400`.

```json
"meta": { "limit": 25, "offset": 0, "total": 57, "next_offset": 25 }
```

`next_offset` vaut `offset + limit` tant qu'il reste des entrées, sinon `null`.

### Formats

Dates ISO 8601 UTC (`2026-11-13T09:20:01.004Z`) ; durées en millisecondes ; identifiants de
joueur, record, tentative et replay = UUID en minuscules ; `level_hash` = SHA-256 hexadécimal
(64 caractères) du contenu d'une révision de niveau ; pays = ISO 3166-1 alpha-2 (`FR`),
région = ISO 3166-2 (`FR-IDF`).

### Cache, routes inconnues, CORS

- Les GET **anonymes** (sans en-tête `Authorization`) des routes marquées « cache » peuvent
  être servis par le cache Cloudflare : 15 s par défaut hors développement (variable
  `EDGE_CACHE_SECONDS`, 300 s max, 0 en développement), en-tête
  `Cache-Control: public, max-age=N`. Une requête authentifiée n'est jamais mise en cache.
- Chemin inconnu → `404 NOT_FOUND` ; chemin connu avec une autre méthode →
  `405 METHOD_NOT_ALLOWED` ; `HEAD` est accepté partout où `GET` l'est.
- Aucun en-tête CORS n'est émis : l'API est destinée au client du jeu, pas aux navigateurs.

## 2. Authentification et sessions

Le joueur est **toujours identifié par sa session**, jamais par un identifiant envoyé par le
client. `POST /records/submit` refuse même un champ `player_id` (`400 VALIDATION_ERROR`,
`{"field": "player_id", "message": "champ inconnu"}`).

1. `POST /auth/register` ou `POST /auth/login` renvoie le profil privé et une `session` :
   ```json
   { "access_token": "JA4X-o_Dknel7UZHAScY6YGbw_CupBBdkzcEv8hNxoM", "access_expires_at": "2026-11-13T10:12:44.530Z",
     "refresh_token": "ggeyYnOLmtUCbypAk4h7cqglixzPd297BVpJkG3akwM", "refresh_expires_at": "2026-12-13T09:12:44.530Z" }
   ```
   Jetons opaques de 256 bits en base64url (43 caractères), stockés côté serveur uniquement
   sous forme d'empreinte SHA-256. Durées par défaut : accès 1 h
   (`ACCESS_TOKEN_TTL_SECONDS`), renouvellement 30 jours (`REFRESH_TOKEN_TTL_DAYS`).
2. Chaque appel authentifié envoie `Authorization: Bearer <access_token>` ; l'en-tête doit
   respecter `^Bearer [A-Za-z0-9_-]{20,100}$`, sinon `401 INVALID_TOKEN`.
3. Sur `401 TOKEN_EXPIRED`, le client appelle `POST /auth/refresh` avec son
   `refresh_token`. **Rotation** : une nouvelle paire est émise, l'ancien jeton d'accès cesse
   immédiatement de fonctionner, l'expiration du renouvellement repart pour 30 jours.
4. **Grâce de 120 s** : si la réponse du renouvellement s'est perdue, représenter l'ancien
   `refresh_token` dans les 120 s qui suivent la rotation est accepté (nouvelle rotation ; la
   paire émise juste avant devient invalide).
5. **Réutilisation** : représenter un `refresh_token` déjà tourné après ces 120 s est traité
   comme un vol possible : la session entière est révoquée (`401 SESSION_REVOKED`, entrée
   d'audit `refresh_token_reuse_revoked`) et le joueur doit se reconnecter. Un jeton inconnu
   donne `401 INVALID_REFRESH_TOKEN`, un renouvellement expiré `401 SESSION_EXPIRED`.
6. `POST /auth/logout` révoque la session courante, ou toutes avec `{"all": true}`.

Autres règles : après 10 mots de passe faux consécutifs, le compte est verrouillé 15 minutes
(`423 ACCOUNT_LOCKED`, `details.locked_until`) ; un pseudonyme inconnu et un mauvais mot de
passe donnent la même erreur, dans le même temps ; un compte suspendu reçoit
`403 ACCOUNT_SUSPENDED` (connexion, renouvellement, toute route qui lit la session). Mots de
passe : PBKDF2-HMAC-SHA256, 100 000 itérations par défaut, sel aléatoire.

Les routes à session **facultative** (mention « Auth : facultative » plus bas) fonctionnent
anonymement, mais si un en-tête `Authorization` est présent il doit être valide : un jeton
expiré y donne aussi `401`. Les routes sans session (santé, méta, recherche, avatar,
`/globe/countries`, `/globe/players`) ignorent l'en-tête.

## 3. Limites de débit

Fenêtres fixes stockées dans D1 ; les clés (IP, pseudonyme) sont hachées avec un sel serveur.
Au-delà : `429 RATE_LIMITED`, en-tête `Retry-After` (secondes) et
`details.retry_after_seconds`. La variable `RATE_LIMIT_SCALE` (50 pour l'environnement `dev`
de `wrangler.jsonc`, plafonnée à 1 000) multiplie toutes les limites.

| Règle | Limite | Fenêtre | Clé | Routes |
|---|---|---|---|---|
| `register` | 5 | 1 h | IP | `POST /auth/register` |
| `username_check` | 60 | 5 min | IP | `GET /auth/username-available` |
| `login_ip` | 30 | 15 min | IP | `POST /auth/login` |
| `login_user` | 20 | 15 min | pseudonyme | `POST /auth/login` |
| `refresh` | 60 | 1 min | IP | `POST /auth/refresh` |
| `submit` | 30 | 1 min | joueur | `POST /records/submit` |
| `replay_upload` | 20 | 1 min | joueur | `PUT /records/{recordId}/replay`, `PUT /challenges/runs/{runId}/replay`, `PUT /challenges/attempts/{attemptId}/replay` (compteur partagé) |
| `profile_write` | 20 | 5 min | joueur | `PATCH /players/me`, `PUT /players/me/avatar` (compteur partagé) |
| `friend_request` | 30 | 1 h | joueur | `POST /friends/request` |
| `presence` | 6 | 1 min | joueur | `POST /presence` |
| `admin_write` | 60 | 5 min | admin | `POST /admin/players/{playerId}/role` (2.40), `PUT` / `DELETE /admin/challenges/{challengeId}` (2.41, calendrier des défis : voir CHALLENGES_PERIODS.md §7), `PUT /admin/challenges/{challengeId}/rewards` et `POST /admin/titles` (2.43, §8), `POST /admin/campaign/{levelId}/revisions` et `…/revert` (2.45, [CAMPAIGN_REVISIONS.md](CAMPAIGN_REVISIONS.md)), `POST /admin/players/{playerId}/migrate` (2.50, qui compte aussi dans `backup_create` : il fait une sauvegarde complète) |
| *(salle du joueur, en mémoire)* | 25 | 10 s | joueur | `POST /spectate/broadcast` (2.36, compté par la salle `SpectateRoom`, pas dans D1) |
| `account_delete` | 5 | 1 h | joueur | `DELETE /players/me` |
| `social_message` | 30 | 1 min | joueur | `POST /social/conversations/{id}/messages` |
| `social_group` | 10 | 1 h | joueur | `POST /social/groups` |
| `social_report` | 20 | 1 h | joueur | `POST /social/messages/{id}/report` |
| `social_block` | 30 | 1 h | joueur | `POST /social/blocks` |
| `clan_create` | 5 | 1 h | joueur | `POST /clans` (2.66) |
| `clan_apply` | 30 | 1 h | joueur | `POST /clans/{id}/join`, `POST /clans/{id}/invites` |
| `clan_write` | 120 | 1 h | joueur | les autres changements d'un clan (réglages, rôles, réponses, exclusions, départ, dissolution) |
| `raid_write` | 120 | 1 h | joueur | inscriptions aux raids (joueur, clan), réponses aux invitations, consignes, rappels |
| `raid_call` | 6 | 1 h | joueur | `POST /raids/events/{id}/call` (« Inviter des membres… ») |
| `community_publish` | 10 | 1 h | joueur | `POST /community/levels` |
| `community_upload` | 40 | 1 h | joueur | `PUT …/versions/{v}/package` et `…/replay` |
| `community_download` | 120 | 1 h | joueur (ou IP) | `GET /community/levels/{id}/package` |
| `community_rate` / `community_report` | 60 / 20 | 1 h | joueur | notes, signalements |
| `community_record` | 30 | 1 min | joueur | `POST /community/levels/{id}/records` |
| `community_favorite` | 120 | 1 h | joueur | `POST`/`DELETE /community/levels/{id}/favorite` |
| `community_comment` | 8 | 10 min | joueur | `POST /community/levels/{id}/comments`, `POST /levels/{id}/comments`, `POST /records/{id}/comments`, `PATCH /community/comments/{id}` (compteur partagé par tous les fils) |
| `community_reaction` | 120 | 10 min | joueur | `PUT /community/comments/{id}/reaction` |
| `level_rate` | 60 | 1 h | joueur | `PUT` / `DELETE /levels/{id}/rating` (notes des niveaux de la campagne) |
| `follow` | 60 | 1 h | joueur | `POST /players/{id}/follow` |
| `playtime` | 40 | 1 h | joueur | `POST /players/me/playtime` |
| `cosmetics` | 60 | 1 h | joueur | `PUT /players/me/cosmetics` (tenue portée en ligne) |
| `badges` | 60 | 1 h | joueur | `PUT /players/me/badges` (badges du profil, 2.41) |
| `project_create` / `project_invite` | 10 / 30 | 1 h | joueur | `POST /projects`, `POST /projects/{id}/invites` |
| `project_join` | 20 | 1 h | joueur | `POST /projects/join`, `POST /projects/{id}/join` |
| `project_save` | 120 | 1 h | joueur | `PUT /projects/{id}/content` |
| `challenge_submit` | 30 | 1 min | joueur | `POST /challenges/{challengeId}/runs`, `POST /challenges/{challengeId}/attempts` (compteur partagé ; leurs replays comptent dans `replay_upload`) |
| `migration_code` | 10 | 1 h | joueur | `POST /players/me/migration` (2.50, code de migration) |
| `migration_fail_target` | 5 échecs | 10 min | joueur (nouveau compte) | `POST /players/me/migrate-from` : seuls les échecs comptent ; au-delà, `429` même avec les bons identifiants |
| `migration_fail_source` | 5 échecs | 10 min | pseudonyme (ancien compte) | `POST /players/me/migrate-from` (idem, quel que soit le compte qui essaie) |
| `progress_restore` | 30 | 1 h | joueur | `POST /players/me/progress-restore` |
| `reads` | 300 | 1 min | IP | toute requête `GET` non servie par le cache |

`POST /auth/logout`, `POST /friends/accept`, `DELETE /friends/{playerId}` et les `POST`
d'administration n'ont pas de limite propre.

## 4. Limites de taille

| Contenu | Limite |
|---|---|
| Corps JSON (par défaut) | 8 Kio (8 192 octets) |
| `POST /records/submit`, `POST /challenges/{challengeId}/runs`, `POST /challenges/{challengeId}/attempts` | 4 Kio |
| `PUT /players/me/cosmetics` | 1 Kio |
| `PUT /players/me/badges` | 2 Kio |
| `POST /admin/levels` | 256 Kio, 1 à 500 niveaux |
| Avatar `PUT /players/me/avatar` | 64 Kio, PNG carré 32 à 256 px, profondeur 8 bits |
| Replay `PUT /records/{recordId}/replay`, `PUT /challenges/runs/{runId}/replay`, `PUT /challenges/attempts/{attemptId}/replay` | 1 Mio compressé, 72 o à 4 Mio décompressé |

Un `Content-Length` déclaré trop grand est refusé avant lecture ; un corps plus long que
prévu est coupé et refusé (`413 PAYLOAD_TOO_LARGE`). Un `Content-Length` invalide donne
`400 BAD_REQUEST`.

## 5. Codes d'erreur

| HTTP | Code | Signification |
|---|---|---|
| 400 | `VALIDATION_ERROR` | Corps ou paramètre invalide ; `details` = liste `{field, message}` |
| 400 | `INVALID_JSON` | Corps non JSON, UTF-8 invalide, ou pas un objet |
| 400 | `BAD_REQUEST` | En-tête `Content-Length` invalide |
| 401 | `UNAUTHORIZED` | Authentification requise (pas d'en-tête) ; jeton admin absent ou faux |
| 401 | `INVALID_TOKEN` | En-tête mal formé, session inconnue ou révoquée |
| 401 | `TOKEN_EXPIRED` | Jeton d'accès expiré : appeler `/auth/refresh` |
| 401 | `INVALID_CREDENTIALS` | Pseudonyme ou mot de passe incorrect (login) ; mot de passe incorrect (suppression du compte) |
| 401 | `INVALID_REFRESH_TOKEN` | Jeton de renouvellement inconnu |
| 401 | `SESSION_REVOKED` | Jeton de renouvellement réutilisé après la grâce, ou session révoquée entre-temps |
| 401 | `SESSION_EXPIRED` | Jeton de renouvellement expiré : se reconnecter |
| 403 | `ACCOUNT_SUSPENDED` | Compte suspendu |
| 403 | `ACCOUNT_CLOSED` | (2.50) Connexion, avec le bon mot de passe, à un compte fermé après la migration de ses données vers un autre compte |
| 403 | `MIGRATION_INVALID` | (2.50) `POST /players/me/migrate-from` : pseudonyme, mot de passe ou code de migration incorrect, ou code expiré ou déjà utilisé (une seule réponse pour tous les cas) |
| 403 | `PROFILE_PRIVATE` | Profil privé (historique, statistiques, replay ou commentaires des courses d'un autre joueur) |
| 403 | `NOT_OWNER` | Envoi d'un replay pour le record (ou la partie de défi) d'un autre joueur |
| 403 | `RANKING_DISABLED` | Classement désactivé pour ce niveau |
| 403 | `FRIEND_REQUESTS_DISABLED` | Le joueur n'accepte pas les demandes d'amis |
| 403 | `LEVEL_NOT_COMPLETED` | Note d'un niveau officiel sans temps vérifié sur ce niveau (§8.9 decies) |
| 403 | `RUN_NOT_VERIFIED` | Commentaire sur une course pas encore vérifiée |
| 404 | `NOT_FOUND` | Route inconnue ; routes d'administration désactivées |
| 404 | `PLAYER_NOT_FOUND` | Joueur inconnu, suspendu, ou identifiant mal formé |
| 404 | `LEVEL_NOT_FOUND` | Niveau inconnu du serveur |
| 404 | `LEVEL_VERSION_NOT_FOUND` | Révision de niveau inconnue |
| 404 | `PLANET_NOT_FOUND` | Aucun niveau classé sur cette planète |
| 404 | `RECORD_NOT_FOUND` | Record inconnu ou non visible par ce lecteur |
| 404 | `REPLAY_NOT_FOUND` | Pas de replay (ou replay non vérifié pour un tiers, ou fichier absent) |
| 404 | `CHALLENGE_NOT_FOUND` | Défi inconnu, identifiant mal formé, ou défi dont la période n'a pas commencé (jamais révélé) |
| 404 | `RUN_NOT_FOUND` | Partie de défi inconnue, ou non visible par ce lecteur |
| 404 | `ATTEMPT_NOT_FOUND` | Essai « Plus loin » inconnu : parmi ceux du joueur connecté (envoi du replay), ou essai non `VERIFIED` / d'un compte inactif pour un tiers (téléchargement) |
| 404 | `AVATAR_NOT_FOUND` | Pas d'avatar personnalisé |
| 404 | `REQUEST_NOT_FOUND` | Aucune demande d'ami de ce joueur |
| 404 | `FRIEND_NOT_FOUND` | Aucun lien d'amitié avec ce joueur |
| 404 | `RESTORE_NOT_FOUND` | (2.50) Aucune progression à restaurer (ou son fichier absent) |
| 405 | `METHOD_NOT_ALLOWED` | Méthode non autorisée sur cette route |
| 409 | `USERNAME_TAKEN` | Pseudonyme déjà utilisé (sans tenir compte de la casse), ou celui d'un compte fermé par une migration il y a moins de 30 jours |
| 409 | `SAME_ACCOUNT` | (2.50) Migration d'un compte vers lui-même |
| 409 | `LAST_ADMIN` | (2.50) Migration qui fermerait le dernier compte admin |
| 409 | `LEVEL_VERSION_MISMATCH` | `level_version` / `level_hash` inconnus du serveur ; `details.current_version` |
| 409 | `LEVEL_VERSION_PENDING` | révision plus récente que toutes celles du serveur (jeu mis à jour avant l'enregistrement des niveaux) : le jeu garde la performance et la renvoie plus tard |
| 409 | `LEVEL_VERSION_RETIRED` | Révision plus classée (legacy) ; `details.current_version` |
| 409 | `ATTEMPT_CONFLICT` | `attempt_id` déjà soumis avec d'autres `time_ms`, `deaths` ou `level_hash` |
| 409 | `DUPLICATE_SUBMISSION` | Deux soumissions identiques simultanées |
| 409 | `REPLAY_EXISTS` | Un replay est déjà associé à la tentative |
| 409 | `RECORD_REJECTED` | Replay envoyé pour un record refusé |
| 409 | `RUN_REJECTED` | Replay envoyé pour une partie de défi refusée |
| 409 | `ATTEMPT_REJECTED` | Replay envoyé pour un essai « Plus loin » refusé (plus long que la limite du niveau) |
| 409 | `PROGRESS_UNAVAILABLE` | « Plus loin » demandé (essai, `board=progress`) sur un défi dont la version du niveau n'a pas de tracé |
| 409 | `CHALLENGE_NOT_STARTED` | Partie envoyée pour un défi dont la période n'a pas commencé (qu'il existe ou non) |
| 409 | `CHALLENGE_ENDED` | (2.32 seulement) plus jamais renvoyé depuis 2.33 : une partie reçue plus de 10 minutes après la fin du défi est acceptée hors délai (`late: true`) |
| 409 | `RULES_MISMATCH` | `rules_hash` différent de celui du défi ; `details.rules_hash` |
| 409 | `LEVEL_MISMATCH` | `level_hash` différent de celui du niveau du défi ; `details.level_hash` |
| 409 | `CHALLENGE_CHANGED` | (2.60) le défi a changé (un admin, personne n'y avait de résultat) entre la lecture de la partie et son écriture : elle n'est jamais classée sur sa nouvelle version |
| 409 | `CHALLENGE_HAS_RESULTS` | (2.60, admin) défi commencé sur lequel des joueurs ont déjà un résultat : il ne change plus ; `details.results_count` |
| 409 | `ALREADY_FRIENDS` | Déjà amis |
| 409 | `REQUEST_ALREADY_SENT` | Demande déjà envoyée |
| 409 | `FRIEND_LIMIT` | 200 liens sortants atteints |
| 409 | `NOT_PENDING` | Modération d'un record qui n'est pas `PENDING` |
| 409 | `REVISION_CONFLICT` | Révision déjà enregistrée avec un autre contenu |
| 413 | `PAYLOAD_TOO_LARGE` | Corps trop volumineux (§4) |
| 415 | `UNSUPPORTED_MEDIA_TYPE` | `Content-Type` inattendu (JSON, `image/png`, `application/octet-stream`) |
| 422 | `CATEGORY_RULES_NOT_MET` | Run hors des règles de la catégorie ; `details` = liste de phrases |
| 422 | `INCONSISTENT_PROGRESS` | Objets incohérents avec la révision du niveau |
| 422 | `REPLAY_MALFORMED` | Fichier de replay illisible (rien n'est stocké) |
| 422 | `INVALID_IMAGE` | Avatar PNG refusé (le message donne la raison) |
| 423 | `ACCOUNT_LOCKED` | Trop d'échecs de connexion ; `details.locked_until` |
| 426 | `GAME_VERSION_UNSUPPORTED` | Jeu trop ancien ; `details.min_game_version` |
| 429 | `RATE_LIMITED` | Limite de débit ; `Retry-After`, `details.retry_after_seconds` |
| 500 | `INTERNAL_ERROR` | Erreur inattendue (détails seulement dans les journaux, via `request_id`) |
| 503 | `SERVICE_UNAVAILABLE` | `GET /health` quand la base D1 ne répond pas ; `details.database`, `Retry-After: 30` |
| 503 | `MAINTENANCE` | Une restauration de sauvegarde remplace les données : toute écriture (`POST`, `PUT`, `PATCH`, `DELETE`) hors administration et hors `/auth/login`, `/auth/refresh`, `/auth/logout` est refusée quelques minutes ; les lectures marchent ; `Retry-After: 120` (BACKUP.md) |

`FORBIDDEN`, valeur par défaut de `Errors.forbidden` dans `http.ts`, n'est émis par aucune route.

## 6. Confidentialité

| `location_visibility` | Pays vu par les autres | Classements par pays | Globe |
|---|---|---|---|
| `hidden` (défaut) | non (`country_code: null`, `region_code: null`) | exclu | absent |
| `country` | oui | inclus | absent |
| `globe` | oui | inclus | marqueur au centre du pays ; compté dans `GET /globe/players` si le profil est public |

- Un joueur `hidden` reste classé dans les classements mondiaux, de planète, d'amis et par
  points ; il garde son rang mais n'a pas de marqueur sur le globe.
- Les coordonnées du globe sont le **centre approximatif du pays** (table `countries`,
  générée par `scripts/generate-countries.mjs`), jamais une position personnelle. Un pays sans
  coordonnées ne produit aucun marqueur.
- Une visibilité autre que `hidden` exige un pays : refusée (400) à l'inscription, ramenée
  silencieusement à `hidden` par `PATCH /players/me` si le profil n'a plus de pays.
- `profile_visibility: private` : les autres ne voient que l'identité publique
  (`PlayerSummary`) avec `hidden: true` et `statistics: null` ; l'historique, les
  statistiques et le téléchargement de replays leur répondent `403 PROFILE_PRIVATE`. Le joueur
  reste présent dans les classements, la recherche et (s'il l'a choisi) le globe des
  records, mais n'est jamais compté dans le globe des joueurs de SOCIAL, et ses
  records vérifiés restent lisibles par `GET /records/{recordId}`.
- `allow_friend_requests: false` refuse les nouvelles demandes (`403`).
- Jamais visibles des autres joueurs : niveau de confiance, réglages de confidentialité,
  `attempt_id`, raisons de refus, notes de vérification, version des CGU, dernière connexion.
  Les IP ne sont stockées qu'hachées (sel `IP_HASH_SALT`), les jetons qu'en empreinte.
- RGPD : export complet (`GET /players/me/export`) et suppression (`DELETE /players/me`).

## 7. Vérification des records et classements

- Statuts : `PENDING` (en attente), `VERIFIED`, `REJECTED`. **Seuls les records `VERIFIED`
  apparaissent dans les classements**, les rangs des statistiques et le globe. Le meilleur
  temps `PENDING` du lecteur est rapporté à part (`me.pending`), jamais classé. Les comptes
  suspendus ne sont jamais classés.
- Décision du serveur à la soumission (le client n'est jamais cru) :
  temps < `min_time_ms` de la révision → `REJECTED` (`IMPOSSIBLE_TIME`) ;
  temps > `time_limit_ms` → `REJECTED` (`TIME_OVER_LIMIT`) ; replay déjà analysé et invalide →
  `REJECTED` (`REPLAY_INVALID`) ; nouveau record
  du monde plus de 20 % sous l'actuel et confiance < 3 → `PENDING` (vérification manuelle) ;
  nouveau record du monde sur un classement d'au moins 5 joueurs et confiance < 2 →
  `PENDING` ; replay valide → `VERIFIED` ; confiance ≥ 2 → `VERIFIED` ; sinon `PENDING` en
  attente du replay.
- Niveau de confiance (`trust_level`) : 0 nouveau compte, 1 dès 3 runs vérifiés avec replay,
  2 dès 15, 3 attribué à la main en base (aucune route ne le fixe) ; un record `REJECTED`
  dans les 30 derniers jours le ramène à 0, même depuis 3.
- L'envoi du replay réévalue tous les records `PENDING` de la même tentative ; la modération
  (`/admin/records/{recordId}/review`) tranche les cas restants.
- Un classement est lié à **une révision** de niveau (son `level_hash`) et une catégorie :
  publier une révision rend les précédentes `legacy` (soumissions refusées avec
  `LEVEL_VERSION_RETIRED`, classements toujours lisibles via `level_version` ou
  `level_hash`), jamais mélangées.
- Égalités : temps, puis date d'obtention la plus ancienne, puis identifiant du joueur.
  Hebdomadaire : meilleur record vérifié créé depuis le lundi 00:00 UTC (remise à zéro
  logique, l'historique est conservé).

## 8. Routes

« Auth » : *aucune*, *facultative* (session lue si présente), *requise* (`bearerAuth`) ou
*admin* (`adminToken`). Toutes les routes peuvent aussi répondre `500 INTERNAL_ERROR`, et
toutes les routes `GET` `429` (règle `reads`).

### 8.1 Santé et méta

#### `GET /health` — santé du service
Auth : aucune. Teste D1 (`SELECT 1`).
```sh
curl -s $API/health
```
```json
{ "ok": true, "data": { "status": "ok", "api_version": "1.0.0", "environment": "production",
  "database": "ok", "time": "2026-11-13T09:00:00.000Z" }, "request_id": "…" }
```
`503 SERVICE_UNAVAILABLE` (enveloppe d'erreur, `details.database: "unavailable"`,
`Retry-After: 30`) si `SELECT 1` échoue. Cette route n'est pas soumise à la limite `reads`
(stockée dans D1) : elle reste utilisable pendant une panne de la base.

**Latence affichée par le jeu (2.72)** : « En ligne - 25ms » en haut à droite des écrans de
COSMIC ONLINE. Le jeu chronomètre sur son fil réseau ses requêtes légères déjà envoyées —
`GET /health` et `POST /presence` (rien de plus n'est envoyé pour la mesurer) — de l'envoi de la
requête au premier octet de la réponse (libcurl : sans la résolution, la connexion ni la poignée
de main TLS ; WinHTTP : connexions gardées ouvertes par la session), et affiche la médiane des
5 dernières mesures (`game/online/LatencyMeter`). « En ligne » seul tant qu'aucune mesure n'existe ;
les mesures sont oubliées dès que le serveur ne répond plus (« Hors ligne »).

#### `GET /meta` — métadonnées · cache
Auth : aucune. Version d'API, environnement, version des CGU à accepter, version minimale du
jeu, catégories, modes et tous les niveaux enregistrés (révision courante).
Exemple : `curl -s $API/meta`
```json
{ "ok": true, "data": { "api_version": "1.0.0", "environment": "production", "tos_version": "2026-11",
  "min_game_version": "2.1.0",
  "categories": ["ANY_PERCENT", "HUNDRED_PERCENT", "NO_DEATH", "NO_DAMAGE"], "modes": ["STANDARD"],
  "levels": [{ "id": "sylvanox_01", "name": "Les Ruines Suspendues", "planet_id": "sylvanox",
    "planet_name": "Sylvanox", "order": 1, "version": 1, "content_hash": "0652…7b08",
    "ranking_enabled": true, "min_game_version": "2.1.0", "total_collectibles": 10,
    "time_limit_ms": 900000 }] }, "request_id": "…" }
```

### 8.2 Authentification

#### `GET /auth/username-available?username=<pseudo>` — pseudonyme libre ?
Auth : aucune · limite `username_check` (60 / 5 min par IP) · jamais mis en cache.
Applique les mêmes règles que l'inscription. Réponse `200` :
`{ "username": "NovaRider", "available": false, "reason": "taken", "message": "Ce pseudonyme est déjà utilisé." }`
avec `reason` parmi `null` (libre), `invalid` (format), `reserved`, `taken`. L'inscription
reste l'autorité (un pseudonyme peut être pris entre la vérification et l'envoi : 409
`USERNAME_TAKEN`). Le jeu interroge cette route 0,5 s après la dernière frappe.

#### `POST /auth/register` — créer un compte
Auth : aucune · limite `register` (vérifiée avant lecture du corps).

| Champ | Requis | Règles |
|---|---|---|
| `username` | oui | 3 à 20 caractères, `^[A-Za-z0-9](?:[A-Za-z0-9_.-]{1,18})[A-Za-z0-9]$`, espaces de bord retirés, unique sans casse, non réservé* |
| `password` | oui | 8 à 128 caractères, ne contient pas le pseudonyme (sans casse) |
| `accept_tos` | oui | doit valoir `true` |
| `tos_version` | oui | ≤ 32 caractères, égale à `tos_version` de `GET /meta` |
| `avatar` | non | `avatar_01` à `avatar_16` (défaut `avatar_01`) |
| `country_code` | non | `^[A-Z]{2}$`, pays connu du serveur |
| `region_code` | non | `^[A-Z]{2}-[A-Z0-9]{1,3}$`, commence par `<country_code>-` |
| `location_visibility` | non | `hidden` (défaut), `country`, `globe` — autre que `hidden` exige un pays |
| `profile_visibility` | non | `public` (défaut), `private` |
| `allow_friend_requests` | non | booléen (défaut `true`) |

\* Réservés : après passage en minuscules et suppression de `_ . -`, égal à admin,
administrator, moderator, modo, cosmic, cosmicstudio, system, support, root, null,
undefined, anonymous, staff, ou commençant par `<un de ces noms>team` ou par `official`.
```sh
curl -s -X POST $API/auth/register -H 'content-type: application/json' -d '{"username":"NovaRider",
  "password":"sup3r secret pass","country_code":"FR","location_visibility":"globe",
  "accept_tos":true,"tos_version":"2026-11"}'
```
`201` :
```json
{ "ok": true, "data": {
  "player": { "id": "a908a1fb-a574-47c3-b6ed-f550f36cc212", "username": "NovaRider", "avatar": "avatar_01",
    "avatar_url": null, "country_code": "FR", "region_code": null, "location_visibility": "globe",
    "profile_visibility": "public", "allow_friend_requests": true, "trust_level": 0,
    "tos_version": "2026-11", "created_at": "…", "last_seen_at": "…", "account_status": "active" },
  "session": { "access_token": "…", "access_expires_at": "…", "refresh_token": "…", "refresh_expires_at": "…" } },
  "request_id": "…" }
```
Erreurs : 400 (`VALIDATION_ERROR`, `INVALID_JSON`, `BAD_REQUEST`), 409 `USERNAME_TAKEN`, 413,
415, 429.

#### `POST /auth/login` — se connecter
Auth : aucune · limites `login_ip` puis `login_user`. Corps : `username` (1 à 32, sans
casse), `password` (1 à 128).
```sh
curl -s -X POST $API/auth/login -H 'content-type: application/json' \
  -d '{"username":"novarider","password":"sup3r secret pass"}'
```
`200` : `{ "player": { …profil privé… }, "session": { … } }` (nouvelle session à chaque
connexion). Erreurs : 400, 401 `INVALID_CREDENTIALS`, 403 `ACCOUNT_SUSPENDED`, 413, 415,
423 `ACCOUNT_LOCKED`, 429.

#### `POST /auth/refresh` — renouveler les jetons
Auth : aucune (le jeton est dans le corps) · limite `refresh`. Corps : `refresh_token`
(20 à 100 caractères `[A-Za-z0-9_-]`).
```sh
curl -s -X POST $API/auth/refresh -H 'content-type: application/json' -d '{"refresh_token":"ggeyYnOL…akwM"}'
```
`200` : `{ "session": { "access_token": "…", "access_expires_at": "…", "refresh_token": "…",
"refresh_expires_at": "…" } }`. Erreurs : 400, 401 (`INVALID_REFRESH_TOKEN`,
`SESSION_REVOKED`, `SESSION_EXPIRED`), 403 `ACCOUNT_SUSPENDED`, 413, 415, 429. Voir §2.

#### `POST /auth/logout` — se déconnecter
Auth : requise. Corps facultatif, lu seulement si `Content-Type` est JSON : `{"all": true}`
révoque toutes les sessions du joueur.
```sh
curl -s -X POST $API/auth/logout -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' -d '{"all":true}'
```
`200` : `{ "logged_out": true, "all_sessions": true }`. Erreurs : 400, 401, 403, 413.

#### `GET /auth/me` — mon profil privé
Auth : requise. Exemple : `curl -s $API/auth/me -H "authorization: Bearer $TOKEN"`.
`200` : `{ "player": { …profil privé… } }`. Erreurs : 401, 403.

### 8.3 Joueurs

Le **profil privé** (`PrivateProfile`) contient `id`, `username`, `avatar`, `avatar_url`,
`country_code`, `region_code`, `location_visibility`, `profile_visibility`,
`allow_friend_requests`, `trust_level`, `tos_version`, `created_at`, `last_seen_at`,
`account_status`. L'**identité publique** (`PlayerSummary`) se limite à `id`, `username`,
`avatar`, `avatar_url` (`/api/v1/players/{id}/avatar` si avatar personnalisé, sinon `null`)
et `country_code` (`null` si masqué).

#### `GET /players/search?q=` — recherche par préfixe
Auth : aucune. `q` : 2 à 20 caractères `[A-Za-z0-9_.-]`, obligatoire. Comptes actifs, au plus
20 résultats, noms les plus courts d'abord, pas de pagination.
```sh
curl -s "$API/players/search?q=nov"
```
`200` : `{ "players": [{ "id": "…", "username": "Nova", "avatar": "avatar_01", "avatar_url": null,
"country_code": null }, …] }`. Erreurs : 400.

#### `GET /players/{playerId}` — profil et statistiques
Auth : facultative. Trois formes de `player` :
- le joueur lui-même : profil privé + `hidden: false` ;
- profil public vu par un autre : `PlayerSummary` + `region_code` (`null` si pays masqué),
  `created_at`, `hidden: false`, `friendship` (`friends`, `request_sent`,
  `request_received` ou `null`, toujours `null` en anonyme) ;
- profil privé vu par un autre : `PlayerSummary` + `hidden: true`, et `statistics: null`.

**Administrateur (2.75.3, rôle de la session)** : le profil d'un autre joueur, même masqué ou
d'un compte non actif, en entier (`statistics`, `playtime`, `achievements`, `cosmetics`,
`badges` ; `player.hidden` dit toujours ce que voient les autres), avec `admin_view` :
`{ locks: ["profile_private" | "account_inactive" | "presence_hidden" | "follows_private"…],
account_status, closed_at, created_at, last_seen_at, presence ("online" | "away" | "offline",
l'état réel), presence_hidden, privacy: { profile_visibility, location_visibility,
allow_friend_requests, presence_visible, follows_visibility, spectate_allowed }, friends: { total,
incoming, outgoing, list: [{ player: PlayerSummary, since }] (100 au plus) }, clan_requests: [{
kind: "apply" | "invite", clan: { id, tag, name }, created_at }] (jamais le message), records: {
pending, rejected } }`. Le pays et la région masqués restent `null`. Écrit au journal
(`admin_profile_viewed`, regroupé). Voir [PRIVACY.md](PRIVACY.md#ce-que-voit-un-administrateur-2753).
```sh
curl -s $API/players/a908a1fb-a574-47c3-b6ed-f550f36cc212
```
```json
{ "ok": true, "data": {
  "player": { "id": "a908a1fb-…", "username": "NovaRider", "avatar": "avatar_07", "avatar_url": null,
    "country_code": "FR", "region_code": null, "created_at": "…", "hidden": false, "friendship": null },
  "statistics": { "verified_records": 3, "submitted_records": 4, "personal_bests": 2,
    "levels_completed": 1, "first_places": 1,
    "planets": [{ "planet_id": "sylvanox", "planet_name": "Sylvanox", "levels": 3, "completed_levels": 1, "completed": false }],
    "planets_completed": 0,
    "best_times": [{ "level_id": "sylvanox_01", "level_name": "Les Ruines Suspendues", "planet_id": "sylvanox",
      "category": "ANY_PERCENT", "time_ms": 61000, "record_id": "…", "achieved_at": "…", "rank": 1 }] } },
  "request_id": "…" }
```
`verified_records` et `submitted_records` (tout statut) comptent toutes les révisions ; les
autres valeurs portent sur les meilleurs temps vérifiés des révisions courantes
(`levels_completed` et `planets` : catégorie `ANY_PERCENT` ; `planets` : niveaux classés).
Erreurs : 401, 403 `ACCOUNT_SUSPENDED` (lecteur), 404 `PLAYER_NOT_FOUND` (identifiant mal
formé, inconnu ou suspendu).

#### `GET /players/{playerId}/statistics`
Auth : facultative. Exemple : `curl -s $API/players/a908a1fb-a574-47c3-b6ed-f550f36cc212/statistics`.
`200` : `{ "player_id": "…", "statistics": { … } }` (même objet que ci-dessus). Erreurs :
401, 403 (`PROFILE_PRIVATE`, `ACCOUNT_SUSPENDED`), 404. 2.75.3 : un administrateur passe le
profil masqué et le compte non actif (`admin_view: { locks }`, journal) ; de même pour
`GET /players/{id}/playtime`, `/followers` et `/following` (abonnements privés : `follows_private`),
`/creations` (brouillons et niveaux masqués par les signalements : `levels_unpublished`) et
`/races` (avec `unverified` : les 30 dernières tentatives `PENDING` / `REJECTED`, `status`,
`rank: null`).

#### `GET /players/{playerId}/records` — historique
Auth : facultative. Paramètres : `limit` (1 à 200, défaut 50), `offset`, `level_id`,
`category`, `status` (`PENDING` / `VERIFIED` / `REJECTED`, propriétaire uniquement). Plus
récents d'abord. Les autres lecteurs ne voient que les records `VERIFIED` en vue publique ;
le propriétaire voit tout, avec `attempt_id`, `rejection_reason`, `verification_notes`.
```sh
curl -s "$API/players/a908a1fb-a574-47c3-b6ed-f550f36cc212/records?level_id=sylvanox_01&limit=10"
```
`200` : `{ "player_id": "…", "records": [ { …record… } ] }` + `meta`. Erreurs : 400, 401,
403 (`PROFILE_PRIVATE`, `ACCOUNT_SUSPENDED`), 404. 2.75.3 : un administrateur (rôle de la
session) voit tous les statuts (filtre `status` permis), même sur un profil masqué, chaque record
avec `admin_view: { locks, status, rejection_reason, verification_notes, replay_status }` ; un
record `PENDING` ou `REJECTED` ne compte jamais comme vérifié. `GET /records/{recordId}` lui
répond de même (`admin_view`) pour tout statut.

Un **record** (vue publique) : `id`, `player_id`, `level_id`, `level_version`, `level_hash`,
`mode`, `category`, `time_ms`, `deaths`, `damage_taken`, `collectibles`,
`total_collectibles`, `score`, `game_version`, `verification_status`, `has_replay`,
`created_at`, `verified_at`.

#### `PATCH /players/me` — modifier son profil
Auth : requise · limite `profile_write`. Tous les champs sont facultatifs : `username`
(mêmes règles qu'à l'inscription), `avatar` (`avatar_01`…`avatar_16` ; supprime l'avatar
personnalisé), `country_code` (`null` efface pays **et** région et force `hidden`),
`region_code` (`null` l'efface ; doit appartenir au pays, nouveau ou actuel),
`location_visibility`, `profile_visibility`, `allow_friend_requests`, `follows_visibility`,
`presence_visible` (2.32.3), `spectate_allowed` (2.36 : « Mes amis peuvent me regarder
jouer », vrai par défaut ; `false`, ou `presence_visible: false`, oublie aussitôt la partie
en cours diffusée, §8.7 bis). Le profil privé rendu contient `spectate_allowed`.
```sh
curl -s -X PATCH $API/players/me -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' \
  -d '{"country_code":"JP","region_code":null,"location_visibility":"globe","avatar":"avatar_07"}'
```
`200` : `{ "player": { …profil privé à jour… } }`. Erreurs : 400 (dont pseudonyme réservé,
région hors du pays, pays inconnu), 401, 403, 409 `USERNAME_TAKEN`, 413, 415, 429. Il n'existe
pas de `GET /players/me` : utiliser `GET /auth/me`.

#### `PUT /players/me/avatar` — avatar personnalisé
Auth : requise · limite `profile_write`. Corps brut `image/png` ≤ 64 Kio : signature PNG,
`IHDR` en premier, image carrée de 32 à 256 px, profondeur 8 bits, blocs cohérents, fin
exacte sur `IEND`.
```sh
curl -s -X PUT $API/players/me/avatar -H "authorization: Bearer $TOKEN" -H 'content-type: image/png' --data-binary @avatar.png
```
`200` : `{ "player": { …, "avatar": "custom", "avatar_url": "/api/v1/players/a908a1fb-…/avatar", … } }`.
Erreurs : 400 `BAD_REQUEST`, 401, 403, 413, 415, 422 `INVALID_IMAGE`
(« Avatar refusé : image carrée de 32 à 256 pixels attendue. »), 429.

#### `GET /players/{playerId}/avatar`
Auth : aucune. Exemple : `curl -s -o avatar.png $API/players/a908a1fb-a574-47c3-b6ed-f550f36cc212/avatar`. `200 image/png`, `Cache-Control: public, max-age=300` (servi même pour un
profil privé). Erreurs : 404 (`PLAYER_NOT_FOUND`, `AVATAR_NOT_FOUND`).

#### `GET /players/me/export` — export RGPD
Auth : requise. Profil privé, sessions (`id`, dates, `revoked_at`, `client_info` = User-Agent
tronqué), lignes brutes des records (dont `kills`), liens d'amitié, métadonnées des replays,
journal d'audit du joueur. Ni empreinte de mot de passe, ni empreinte de jeton, ni IP.
Exemple : `curl -s $API/players/me/export -H "authorization: Bearer $TOKEN" > export.json`
```json
{ "ok": true, "data": { "exported_at": "…", "player": { … }, "sessions": [ … ], "records": [ … ],
  "friends": [ … ], "replays": [ … ], "audit_log": [{ "action": "register", "details": null, "created_at": "…" }],
  "notes": "Le mot de passe n'est stocké que sous forme d'empreinte PBKDF2 …" }, "request_id": "…" }
```
Erreurs : 401, 403.

#### `DELETE /players/me` — supprimer son compte
Auth : requise · limite `account_delete`. Corps : `password` (1 à 128) et
`"confirm": "DELETE"`. Supprime les fichiers (replays, avatar) puis toutes les données du
joueur (sessions, records, meilleurs temps, replays, amitiés, journal d'audit).
```sh
curl -s -X DELETE $API/players/me -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' \
  -d '{"password":"sup3r secret pass","confirm":"DELETE"}'
```
`200` : `{ "deleted": true }`. Erreurs : 400 (`confirm` ≠ `DELETE`), 401 (dont
`INVALID_CREDENTIALS` si mot de passe faux), 403, 413, 415, 429.

### 8.4 Classements

Uniquement des records **VERIFIED** de comptes actifs. Paramètres communs des classements de
niveau : `category` (défaut `ANY_PERCENT`), `mode` (`STANDARD`), `period` (`all` ou
`weekly`), `player` (filtre par préfixe de pseudonyme, 1 à 20 caractères
`[A-Za-z0-9_.-]`), `limit`/`offset`, et `level_version` ou `level_hash` pour consulter une
ancienne révision (`level_hash` prioritaire ; défaut : révision courante).

Réponse d'un **classement de niveau** :
```json
{ "ok": true, "data": {
  "board": { "level_id": "sylvanox_01", "name": "Les Ruines Suspendues", "planet_id": "sylvanox",
    "planet_name": "Sylvanox", "version": 1, "content_hash": "0652…7b08", "compatibility_status": "active",
    "is_current": true, "ranking_enabled": true, "category": "ANY_PERCENT", "mode": "STANDARD",
    "period": "all", "week_start": null, "week_end": null, "country_code": null, "friends": false },
  "entries": [{ "rank": 1, "world_rank": 1,
    "player": { "id": "…", "username": "NovaRider", "avatar": "avatar_07", "avatar_url": null, "country_code": "FR",
      "frame": "gold", "title": { "id": "title_explorateur", "name": "Explorateur" }, "suit": "givre" },
    "time_ms": 61000, "record_id": "…", "achieved_at": "…", "has_replay": true, "is_me": false, "comments": 3 }],
  "me": { "entry": null, "pending": { "record_id": "…", "time_ms": 59000, "created_at": "…",
    "note": "En attente du replay.", "status": "PENDING" } } },
  "meta": { "limit": 25, "offset": 0, "total": 1, "next_offset": null }, "request_id": "…" }
```
`rank` = position dans ce classement (filtré), `world_rank` = position mondiale ;
`has_replay` = un replay vérifié existe ; `comments` (2.19) = nombre de commentaires visibles
de la course (`record_id`), `null` quand son auteur a masqué son profil (§8.9 decies) ;
`player.frame`, `player.title` et `player.suit` (2.23) = la tenue du joueur (§8.9 undecies),
absents quand il a masqué son profil, dans tous les classements (niveau, pays, amis, semaine,
planète, points cosmiques, course à la vérification) ;
`me` vaut `null` en anonyme, sinon la ligne du lecteur (même hors de la page) et son meilleur
temps en attente. Erreurs communes : 400
(paramètre invalide), 401 (jeton envoyé mais invalide), 403 `ACCOUNT_SUSPENDED`, 404
(`LEVEL_NOT_FOUND`, `LEVEL_VERSION_NOT_FOUND`), 429.

#### `GET /leaderboards/level/{levelId}` — classement mondial d'un niveau · cache
Auth : facultative. Lisible même si le classement du niveau est désactivé
(`ranking_enabled: false`).
```sh
curl -s "$API/leaderboards/level/sylvanox_01?category=NO_DEATH&limit=10"
curl -s "$API/leaderboards/level/sylvanox_01?level_version=1"   # ancienne révision
```

#### `GET /leaderboards/country/{countryCode}?level_id=` — par pays · cache
Auth : facultative. `countryCode` : 2 lettres (mis en majuscules) ; `level_id` obligatoire.
Joueurs du pays dont la localisation n'est pas `hidden` ; `board.country_code` est rempli.
```sh
curl -s "$API/leaderboards/country/FR?level_id=sylvanox_01"
```

#### `GET /leaderboards/friends?level_id=` — le joueur et ses amis
Auth : requise (jamais en cache). Amitiés acceptées uniquement ; `board.friends: true`.
```sh
curl -s "$API/leaderboards/friends?level_id=sylvanox_01" -H "authorization: Bearer $TOKEN"
```

#### `GET /leaderboards/weekly?level_id=` — classement de la semaine · cache
Auth : facultative. `period` forcé à `weekly` ; `board.week_start` = lundi 00:00 UTC,
`board.week_end` = lundi suivant. Exemple : `curl -s "$API/leaderboards/weekly?level_id=sylvanox_01"`
→ `{ "board": { …, "period": "weekly", "week_start": "2026-11-09T00:00:00.000Z",
"week_end": "2026-11-16T00:00:00.000Z" }, "entries": [ … ], "me": null }` + `meta`.

#### `GET /leaderboards/global` — points cosmiques · cache
Auth : facultative. Avec `level_id` : identique au classement de ce niveau. Sans
`level_id` : classement par **points cosmiques** sur tous les niveaux classés (révisions
courantes) — pour chaque niveau, `floor(1000 × meilleur temps mondial / meilleur temps du
joueur)`, additionnés ; départage : points, niveaux terminés, temps total. Paramètres :
`category`, `country` (2 lettres **majuscules**, exclut les localisations masquées),
`limit`, `offset`.
```sh
curl -s "$API/leaderboards/global?country=FR"
```
```json
{ "ok": true, "data": { "board": { "kind": "cosmic_points", "category": "ANY_PERCENT", "country_code": "FR" },
  "entries": [{ "rank": 1, "world_rank": 3, "player": { … }, "points": 1909, "levels_completed": 2,
    "total_time_ms": 156000, "is_me": false }], "me": null }, "meta": { … }, "request_id": "…" }
```

#### `GET /leaderboards/planet/{planetId}` — temps total d'une planète · cache
Auth : facultative. Joueurs ayant un temps vérifié sur **tous** les niveaux classés de la
planète, classés par somme de leurs meilleurs temps. Paramètres : `category`, `country`,
`limit`, `offset`. Exemple : `curl -s "$API/leaderboards/planet/sylvanox?category=NO_DEATH"`
```json
{ "ok": true, "data": { "board": { "kind": "planet_total_time", "planet_id": "sylvanox", "category": "ANY_PERCENT",
    "levels": [{ "id": "sylvanox_01", "name": "Les Ruines Suspendues" }, …], "country_code": null },
  "entries": [{ "rank": 1, "world_rank": 1, "player": { … }, "total_time_ms": 152000,
    "levels_completed": 3, "is_me": false }], "me": null }, "meta": { … }, "request_id": "…" }
```
Erreurs : 400, 401, 403, 404 `PLANET_NOT_FOUND`, 429.

### 8.5 Records et replays

#### `POST /records/submit` — soumettre un run
Auth : requise · limite `submit` · corps JSON ≤ 4 Kio. Pas de `player_id` : le joueur est
celui de la session.

| Champ | Requis | Règles |
|---|---|---|
| `level_id` | oui | `^[a-z0-9_]{1,64}$` |
| `level_version` | oui | entier 1 à 1 000 000 |
| `level_hash` | oui | 64 caractères hexadécimaux minuscules (révision exacte jouée) |
| `category` | oui | `ANY_PERCENT`, `HUNDRED_PERCENT`, `NO_DEATH`, `NO_DAMAGE` (SPEEDRUN retirée en 2.40.1) |
| `time_ms` | oui | entier 1 à 86 400 000 |
| `deaths` | oui | entier 0 à 9 999 |
| `game_version` | oui | `^\d{1,3}\.\d{1,3}\.\d{1,4}$` |
| `attempt_id` | oui | UUID v4 minuscule généré par le client pour ce run |
| `mode` | non | `STANDARD` (défaut) |
| `damage_taken` | non | 0 à 1 000 000 (défaut 0) |
| `collectibles` | non | 0 à 10 000 (défaut 0) |
| `total_collectibles` | non | 0 à 10 000 (défaut 0), doit égaler celui de la révision |
| `kills` | non | 0 à 100 000 (défaut 0) |
| `score` | non | 0 à 100 000 000 (défaut 0) |
| `has_replay` | non | booléen, renvoyé tel quel dans `replay.accepted` |

Contrôles dans l'ordre : niveau connu (404), classement activé (403 `RANKING_DISABLED`),
révision exacte (409 `LEVEL_VERSION_MISMATCH`), révision encore active (409
`LEVEL_VERSION_RETIRED`), version du jeu (426), règles de catégorie (422
`CATEGORY_RULES_NOT_MET` : 100 % des objets pour `HUNDRED_PERCENT`, 0 mort pour `NO_DEATH`
et `NO_DAMAGE`, 0 dégât pour `NO_DAMAGE`), cohérence des objets (422
`INCONSISTENT_PROGRESS`), puis idempotence et décision (§7).
```sh
curl -s -X POST $API/records/submit -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' -d '{
  "level_id":"sylvanox_01","level_version":1,"level_hash":"0652230a14e586a34efa302cc4c617c8175bdc63c4ff8ae2cdc34dbda9227b08",
  "category":"ANY_PERCENT","time_ms":61000,"deaths":0,"collectibles":4,"total_collectibles":10,
  "game_version":"2.1.0","attempt_id":"72c3f50d-4b27-46e4-8b70-f90de2db728a","has_replay":true}'
```
`201` :
```json
{ "ok": true, "data": { "duplicate": false,
  "record": { "id": "4b9c3c4e-374c-4fa8-a579-383df21bd26e", "level_id": "sylvanox_01", "category": "ANY_PERCENT",
    "time_ms": 61000, "verification_status": "PENDING", "has_replay": false, "verified_at": null,
    "attempt_id": "72c3f50d-…", "rejection_reason": null, "verification_notes": "En attente du replay.", … },
  "replay": { "required": true, "accepted": true, "upload_path": "/api/v1/records/4b9c3c4e-…/replay" },
  "ranking": { "previous_rank": null, "rank": null, "total": 4, "personal_best_improved": false,
    "previous_best_ms": null } }, "request_id": "…" }
```
Idempotence : la même tentative peut être renvoyée (réponse perdue, file hors ligne) ; même
`attempt_id` et même catégorie avec les mêmes `time_ms`, `deaths` et `level_hash` →
`200` avec `"duplicate": true`, le record existant et `"ranking": null`. La même tentative
peut être soumise dans une autre catégorie qu'elle remplit (nouveau record, même replay).
Autres erreurs : 400, 401, 403 `ACCOUNT_SUSPENDED`, 409 `ATTEMPT_CONFLICT` /
`DUPLICATE_SUBMISSION`, 413, 415, 429.

#### `GET /records/{recordId}` — un record
Auth : facultative. Les autres lecteurs ne voient que les records `VERIFIED` de joueurs
actifs (sinon 404) ; le propriétaire voit tout, en vue propriétaire.
Exemple : `curl -s $API/records/4b9c3c4e-374c-4fa8-a579-383df21bd26e`
```json
{ "ok": true, "data": { "record": { …record… }, "player": { …PlayerSummary… },
  "is_personal_best": true, "rank": 1 }, "request_id": "…" }
```
`rank` n'est rempli que si le record est vérifié et constitue le meilleur temps du joueur.
Erreurs : 401, 403 `ACCOUNT_SUSPENDED`, 404 `RECORD_NOT_FOUND`.

#### `PUT /records/{recordId}/replay` — envoyer le replay
Auth : requise (propriétaire) · limite `replay_upload`. Corps brut
`application/octet-stream` ≤ 1 Mio au format CPRZ (annexe). Un seul replay par tentative,
rattaché à tous ses records. Un fichier illisible est refusé (422, rien n'est stocké, le
client peut réessayer) ; un fichier lisible est stocké puis analysé et la réponse est `201`
qu'il soit valide ou non.
```sh
curl -s -X PUT $API/records/4b9c3c4e-374c-4fa8-a579-383df21bd26e/replay -H "authorization: Bearer $TOKEN" \
  -H 'content-type: application/octet-stream' --data-binary @run.cprz
```
```json
{ "ok": true, "data": { "replay_id": "890a226a-5041-4ddf-a1bd-917adc04bcf2", "verification_status": "VERIFIED",
  "analysis": { "ok": true, "reasons": [], "stats": { "frames": 1831, "duration_ms": 61000, "deaths": 0,
    "collectibles": 4, "damage": 0, "max_step_px": 82, "jumps": 37 } },
  "records": [{ "record": { …, "verification_status": "VERIFIED", "has_replay": true },
    "ranking": { "previous_rank": null, "rank": 1, "total": 1, "personal_best_improved": true,
      "previous_best_ms": null } }] }, "request_id": "…" }
```
`records` liste les records de la tentative qui étaient `PENDING`, réévalués (un replay
invalide les rend `REJECTED` avec les raisons dans `verification_notes`). Erreurs : 400
`BAD_REQUEST`, 401, 403 (`NOT_OWNER`, `ACCOUNT_SUSPENDED`), 404 (`RECORD_NOT_FOUND`,
`LEVEL_VERSION_NOT_FOUND`), 409 (`REPLAY_EXISTS`, `RECORD_REJECTED`), 413, 415,
422 `REPLAY_MALFORMED`, 429.

#### `GET /records/{recordId}/replay` — télécharger le replay
Auth : facultative. Un tiers ne peut télécharger que le replay **vérifié** d'un record
vérifié d'un profil public ; le propriétaire télécharge le sien quel que soit son statut.
Exemple : `curl -s -D - -o run.cprz $API/records/4b9c3c4e-374c-4fa8-a579-383df21bd26e/replay`.
`200 application/octet-stream` (fichier CPRZ tel qu'envoyé), en-têtes `Content-Length`,
`X-Replay-SHA256`, `Cache-Control: private, max-age=3600`. Erreurs : 401, 403
(`PROFILE_PRIVATE`, `ACCOUNT_SUSPENDED`), 404 (`RECORD_NOT_FOUND`, `REPLAY_NOT_FOUND`).

### 8.6 Globe

Marqueurs : seuls les joueurs `location_visibility: globe`, placés au **centre approximatif
de leur pays**. Paramètres communs : `category`, `period` (`all` / `weekly`), et avec un
niveau `level_version` / `level_hash`. Ici `limit` et `per_level` hors bornes sont ramenés
dans l'intervalle (seule une valeur non numérique ou de plus de 5 chiffres est refusée).

#### `GET /globe/records` — marqueurs · cache
Auth : facultative. Avec `level_id` : marqueurs de ce classement par rang mondial, au plus
`limit` (défaut 300, 1 à 1 000). Sans `level_id` : pour chaque niveau classé, les
`per_level` (défaut 10, 1 à 100) premiers marqueurs, le tout tronqué à `limit`.
```sh
curl -s "$API/globe/records?level_id=sylvanox_01"
```
```json
{ "ok": true, "data": { "level_id": "sylvanox_01", "category": "ANY_PERCENT", "period": "all",
  "records": [{ "record_id": "…", "player_id": "…", "player_name": "NovaRider", "avatar": "avatar_07",
    "avatar_url": null, "country_code": "FR", "latitude": 46.6, "longitude": 2.4, "time_ms": 61000,
    "rank": 2, "level_id": "sylvanox_01", "achieved_at": "…", "is_me": false }],
  "generated_at": "2026-11-13T10:00:00.000Z" }, "request_id": "…" }
```
`rank` est le rang mondial (les joueurs masqués comptent dans les rangs). Erreurs : 400,
401, 403, 404 (`LEVEL_NOT_FOUND`, `LEVEL_VERSION_NOT_FOUND`).

#### `GET /globe/records/{countryCode}?level_id=` — marqueurs d'un pays · cache
Auth : facultative. `countryCode` 2 lettres, `level_id` obligatoire, `limit` défaut 100
(1 à 500). Exemple : `curl -s "$API/globe/records/FR?level_id=sylvanox_01"`.
`200` : `{ "country_code": "FR", "level_id": "…", "category": "…", "period": "…",
"records": [ …marqueurs… ], "generated_at": "…" }`. Erreurs : 400, 401, 403, 404.

#### `GET /globe/countries` — agrégat par pays · cache
Auth : aucune. Avec `level_id` : par pays, nombre de joueurs visibles sur le globe dans ce
classement, meilleur rang, temps et pseudonyme, triés par meilleur rang. Sans `level_id` :
par pays, nombre de joueurs visibles ayant au moins un meilleur temps vérifié dans la
catégorie, triés par ce nombre (`best_*` à `null`, `period` renvoyé mais non utilisé).
Exemple : `curl -s "$API/globe/countries?level_id=sylvanox_01"`
```json
{ "ok": true, "data": { "level_id": "sylvanox_01", "category": "ANY_PERCENT", "period": "all",
  "countries": [{ "country_code": "FR", "name_fr": "France", "name_en": "France", "latitude": 46.6,
    "longitude": 2.4, "players": 1, "best_time_ms": 61000, "best_rank": 2, "best_player": "NovaRider" }],
  "generated_at": "…" }, "request_id": "…" }
```
Erreurs : 400, 404.

#### `GET /globe/players` — tous les joueurs par pays (globe de SOCIAL) · cache
Auth : aucune (l'en-tête est ignoré ; la réponse est la même pour tous). Nombre de joueurs
par pays, **sans identifiant, pseudonyme ni record** : seuls comptent les comptes actifs qui
ont choisi `location_visibility: globe` **et** un profil public (`profile_visibility:
public`), au **centre approximatif de leur pays** (un pays sans coordonnées n'est pas
listé). Un joueur qui masque sa position ou son profil disparaît du compte à la réponse
suivante (au plus `EDGE_CACHE_SECONDS` plus tard pour les requêtes anonymes). Limite `reads`
comme toute lecture. Triés par nombre de joueurs décroissant.
Exemple : `curl -s "$API/globe/players"`
```json
{ "ok": true, "data": { "countries": [{ "country_code": "FR", "name_fr": "France", "name_en": "France",
    "latitude": 46.6, "longitude": 2.4, "players": 2 }],
  "total_players": 2, "total_countries": 1, "generated_at": "…" }, "request_id": "…" }
```
Erreurs : 405 (autre méthode que `GET`/`HEAD`), 429.

### 8.7 Amis

Les amitiés sont réciproques ; une demande est en attente jusqu'à acceptation. Les comptes
suspendus sont omis.

#### `POST /presence` (2.32.3)

Corps `{ "state": "active" | "idle" | "offline" }` (joueur de la session). Le jeu l'envoie
chaque minute et à chaque changement : `idle` après 5 minutes sans action, `offline` en se
fermant. Seuls le dernier état et son heure sont gardés. Réponse `{ "state" }`.

Dans `GET /friends`, chaque ami accepté porte `presence` : `online`, `away` (inactif) ou
`offline` (rien reçu depuis 150 s, fermé, ou présence masquée par `presence_visible: false`
dans `PATCH /players/me`).

#### `GET /friends`
Auth : requise. Triés par pseudonyme. Exemple : `curl -s $API/friends -H "authorization: Bearer $TOKEN"`
```json
{ "ok": true, "data": { "friends": [{ "player": { …PlayerSummary… }, "since": "…" }],
  "incoming": [ … ], "outgoing": [ … ] }, "request_id": "…" }
```
Erreurs : 401, 403.

#### `POST /friends/request`
Auth : requise · limite `friend_request`. Corps : `username` (3 à 20, sans casse,
prioritaire) ou `player_id` (UUID). Si la cible vous a déjà envoyé une demande, celle-ci est
acceptée (`200`, `"status": "accepted"`, même si la cible refuse les nouvelles demandes) ;
sinon une demande est créée (`201`, `"status": "pending"`).
```sh
curl -s -X POST $API/friends/request -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' -d '{"username":"Orion"}'
```
`201` : `{ "status": "pending", "player": { …PlayerSummary… } }`. Erreurs : 400 (aucun des
deux champs, ou soi-même), 401, 403 (`FRIEND_REQUESTS_DISABLED`, `ACCOUNT_SUSPENDED`),
404 `PLAYER_NOT_FOUND`, 409 (`ALREADY_FRIENDS`, `REQUEST_ALREADY_SENT`, `FRIEND_LIMIT` :
200 liens sortants), 413, 415, 429.

#### `POST /friends/accept`
Auth : requise. Corps : `player_id` (UUID du demandeur). Exemple :
`curl -s -X POST $API/friends/accept -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' -d '{"player_id":"a908a1fb-a574-47c3-b6ed-f550f36cc212"}'`.
`200` : `{ "status": "accepted", "player": { …PlayerSummary… } }`. Erreurs : 400, 401, 403,
404 `REQUEST_NOT_FOUND`, 413, 415.

#### `DELETE /friends/{playerId}`
Auth : requise. Retire un ami, refuse une demande reçue ou annule une demande envoyée (les
deux sens sont supprimés). Exemple : `curl -s -X DELETE $API/friends/a908a1fb-a574-47c3-b6ed-f550f36cc212 -H "authorization: Bearer $TOKEN"`.
`200` : `{ "removed": true }`. Erreurs : 401, 403, 404
(`PLAYER_NOT_FOUND` si identifiant mal formé, `FRIEND_NOT_FOUND`).

### 8.7 bis Regarder un ami jouer (2.36)

Un joueur qui l'autorise (`spectate_allowed`, réglage « Mes amis peuvent me regarder
jouer » de `PATCH /players/me`, **vrai par défaut**, avec une présence montrée) diffuse la
partie qu'il joue ; ses amis acceptés la rejouent exactement (même niveau et version, même
graine, mêmes commandes : les règles des replays exacts, `REPLAY_FORMAT.md`), à environ une
seconde du direct. Une salle par joueur (Durable Object `SpectateRoom`) garde la partie en
cours **en mémoire seulement** (jamais dans D1, R2 ni le stockage de l'objet), au plus
30 minutes de jeu (216 000 pas, 3 Mo de commandes, 6 000 morceaux) ; elle l'oublie une minute
après sa fin, deux minutes après le dernier envoi, dès qu'une autre partie commence, que le
réglage est décoché ou la présence masquée (`PATCH /players/me`), ou que le compte est
supprimé. **Le joueur regardé n'apprend rien des spectateurs** : aucune réponse, aucune
notification, aucun compteur ni journal ne les mentionne, et la salle ne sait pas qui lit.

Seuls la campagne, les niveaux de la communauté installés et les défis sont diffusés (jamais
un test de l'éditeur, un entraînement ou un niveau de l'atelier).

#### `POST /spectate/broadcast`
Auth : requise (la salle du joueur de la session) · au plus **25 envois par 10 s** par
joueur (le jeu en fait environ 2 par seconde ; au-delà : 429). 403 `SPECTATE_DISABLED` si
le réglage est décoché ou la présence masquée (le jeu cesse de diffuser). Corps (JSON, 64 Ko
au plus, champs inconnus refusés), selon `action` :

* `start` — une partie commence (un recommencement est une autre partie) :
  `{ "action": "start", "run_id": UUIDv4, "level": { "source": "campaign" | "community",
  "id", "version", "hash" }, "challenge_id"?: "daily-…", "seed": 16 hex, "keys_per_slot": bool,
  "sim_version": int, "game_version": "2.36.0" }`. Le niveau est vérifié par le serveur
  (campagne : révision et empreinte classée connues ; communauté : version publiée et son
  empreinte ; défi : un défi commencé, de ce niveau), qui fournit le titre et, pour un défi,
  la forme canonique de ses règles : rien de ce que voient les amis ne vient du jeu. Réponse
  `{ "accepted": true, "run_id" }`. Erreurs : 400, 404 `CHALLENGE_NOT_FOUND`,
  409 `LEVEL_MISMATCH`, 429.
* `chunk` — les pas suivants : `{ "action": "chunk", "run_id", "seq": 1, 2…, "from_frame",
  "count": 0..2400, "runs": base64, "checks"?: [[pas, "16 hex"], …] }`. `runs` : des plages de
  pas identiques de 18 octets (u16 répétitions, u32 tenu, appuyé, relâché, i8 x, i8 y, u8
  drapeaux, u8 choix de pouvoir), exactement `count` pas, seulement les actions enregistrées
  (21 bits), le drapeau « passer la cinématique » et les choix 1..8 ou 255 ; `checks` : les
  empreintes d'état du replay prises parmi ces pas, croissantes. `count: 0` : signe de vie (le
  jeu en pause). Le même morceau renvoyé (réponse perdue) est pris une fois. Réponse
  `{ "accepted": true, "seq", "frames" }`. Erreurs : 400, 409 `SPECTATE_RESYNC` (`details :
  { seq, frames }` : la salle n'a pas cette partie ou pas à cet endroit, le jeu renvoie la
  partie depuis le début), 413 `SPECTATE_RUN_TOO_LONG`, 429.
* `end` — `{ "action": "end", "run_id", "reason": "finished" | "failed" | "quit" }` →
  `{ "ended": true }`.

#### `GET /spectate/{playerId}?stream=&after=`
Auth : requise · limite `reads`. La partie que joue cet ami : l'en-tête et les morceaux
après `after` du même `stream` (un autre `stream`, ou aucun : tout depuis le début ; au plus
240 morceaux ou 256 Ko par réponse, `more: true` s'il en reste). **403
`SPECTATE_FORBIDDEN`**, la même réponse quelle que soit la raison, sauf entre amis acceptés,
sans blocage dans un sens ou dans l'autre, et si le joueur l'autorise (réglage et présence) ;
on ne se regarde pas soi-même. Le jeu lit toutes les 0,5 s, sans cache.
```json
{ "ok": true, "data": { "live": true, "run": { "run_id": "…", "stream": "…", "kind": "campaign",
    "level": { "source": "campaign", "id": "sylvanox_01", "version": 3, "hash": "…", "title": "…" },
    "challenge": null, "seed": "0123456789abcdef", "keys_per_slot": true, "sim_version": 11,
    "game_version": "2.36.0", "started_at": "…" },
  "chunks": [{ "seq": 1, "from_frame": 0, "count": 60, "runs": "…", "checks": [[60, "…"]] }],
  "more": false, "last_seq": 1, "frames": 60, "ended": null, "stale": false }, "request_id": "…" }
```
`{ "live": false }` quand rien n'est joué. `ended` : `finished`, `failed`, `quit` ou
`too_long` (la partie reste lisible une minute) ; `stale` : rien reçu du jeu depuis 30 s.

Dans `GET /friends`, un ami accepté qui joue une partie regardable (réglage coché, présence
montrée, présence « en ligne » ou « absent », pas de blocage, partie ni finie ni silencieuse)
porte `playing: { level_title, kind: "campaign" | "community" | "challenge", level_source,
started_at }` : le bouton « Regarder » n'apparaît que lorsqu'il peut servir.

### 8.8 Social : conversations, groupes, blocages

Toutes ces routes exigent une session. Le serveur identifie le joueur par sa session et ne
montre une conversation qu'à ses membres (`404 CONVERSATION_NOT_FOUND` pour les autres, sans
révéler qu'elle existe). On n'écrit qu'à ses **amis** ; un **blocage** (dans un sens ou dans
l'autre) met fin à l'amitié, empêche les demandes d'amis et les conversations privées, et masque
les messages du joueur bloqué pour celui qui bloque, y compris dans les groupes.

Un message : 1 à 500 caractères après nettoyage (caractères de contrôle et d'inversion de sens
supprimés, sauts de ligne conservés, 3 lignes vides au plus, espaces de bord retirés).

| Route | Rôle |
|---|---|
| `GET /social/conversations` | conversations du joueur (100 dernières actives) : `id`, `kind` (`direct`/`group`), `name` (nom du groupe ou pseudonyme de l'ami), `members[]` (profil public + `role`), `role`, `unread`, `last_message` ; `unread_total` |
| `GET /social/unread` | `total` des messages non lus, dont `direct` (conversations privées) et `groups` (groupes) ; `conversations[]` : `conversation_id`, `kind`, `unread` des seules conversations qui ont des messages non lus (les plus nombreux d'abord) ; `latest[]` : les 5 plus récents (`id`, `conversation_id`, `created_at`, `body` tronqué à 120, `kind`, `name`, `sender`) — interrogé par le jeu toutes les 30 s pour les badges (bouton SOCIAL, entrée « En ligne » du menu, onglet « Discussions », chaque conversation et groupe) et les notifications |
| `POST /social/direct` `{player_id}` | ouvre (ou crée une fois) la conversation privée avec un ami — `403 NOT_FRIENDS` / `403 BLOCKED` |
| `POST /social/groups` `{name, member_ids}` | crée un groupe (nom 1-40, 1 à 19 amis : 20 membres au plus) ; le créateur en est le propriétaire |
| `PATCH /social/groups/{id}` `{name}` | renommer (propriétaire) |
| `POST /social/groups/{id}/members` `{player_id}` | ajouter un ami (propriétaire) — `409 GROUP_FULL` |
| `DELETE /social/groups/{id}/members/{playerId}` | quitter (soi-même) ou retirer un membre (propriétaire). Le propriétaire qui part transmet le groupe au membre le plus ancien ; un groupe vide est supprimé |
| `GET /social/conversations/{id}/messages?limit=1..100&before=<date ISO>` | messages du plus ancien au plus récent (`id`, `sender`, `mine`, `body`, `deleted`, `created_at`), `has_more` |
| `POST /social/conversations/{id}/messages` `{body}` | envoie un message (`201`) ; dans une conversation privée, il faut être encore amis et sans blocage |
| `POST /social/conversations/{id}/read` | marque tout comme lu (le compteur de la conversation retombe à 0) |
| `DELETE /social/messages/{id}` | l'auteur efface son message (le texte est supprimé, la bulle « Message supprimé » reste) |
| `POST /social/messages/{id}/report` `{reason?}` | signale le message d'un autre membre aux modérateurs (une fois par joueur et par message, journal d'audit) |
| `GET /social/blocks` · `POST /social/blocks` `{player_id}` · `DELETE /social/blocks/{playerId}` | joueurs bloqués, bloquer, débloquer (le joueur bloqué n'est pas prévenu) |

### 8.8 bis Clans (2.66, En ligne → CLANS)

Migration `0120_clans.sql`, code `server/src/clans.ts`. Le joueur est toujours celui de la session,
jamais un identifiant de la requête. **Un clan par joueur** (clé primaire de `clan_members`), **50
membres** au plus, **un seul chef** (index unique partiel), des officiers et des membres. Nom : 3 à 32
caractères (lettres latines avec accents, chiffres, espaces et `' - _ . ! &`) ; tag : 2 à 5 lettres ou
chiffres, **unique sans tenir compte de la casse**, affiché `[TAG]` ; devise : 80 caractères ;
emblème : `{shape: orb|star|shield|diamond|hexagon|crescent, primary: "#rrggbb", secondary: "#rrggbb"}` ;
adhésion `join_policy` : `invite` (sur invitation), `request` (sur candidature, message de 120
caractères au plus) ou `open`.

Les règles que deux requêtes pourraient se disputer sont décidées dans **une seule instruction ou un
seul lot D1** (une transaction) : l'`INSERT` qui ajoute un membre calcule son rôle à partir du nombre de
membres à cet instant et un rôle impossible fait échouer la contrainte `CHECK` (tout le lot est annulé :
jamais de 51ᵉ membre) ; chaque départ nomme un chef s'il n'y en a plus (l'officier le plus ancien, sinon
le membre le plus ancien) et le dernier départ dissout le clan ; chaque changement revérifie le rôle de
l'acteur dans sa propre clause `WHERE`.

| Route | Rôle |
|---|---|
| `GET /clans/me` | mon clan (`clan` + `role`, ou `null`), les `invitations` reçues (`clan`, `by`) et mes `applications` (`clan`, `message`) |
| `GET /clans?q=&limit=&offset=` | les clans, meilleurs points d'abord ; `q` : un tag (exact, en tête) ou une partie du nom (accents et casse ignorés) ; `has_more` |
| `GET /clans/ranking?limit=&offset=` | classement des clans ayant des points : `rank`, `clan` ; `total` ; `me` (mon clan et son rang) |
| `GET /clans/{id}` | page d'un clan : `clan` (+ `rank`), `members[]` (`player` avec `clan_tag`, `role`, `joined_at`, `best`, et `presence` **seulement pour les membres du clan** : `online`, `away` ou `offline` — 2.72 : une présence masquée se lit `offline`), `members_online` (2.72, **membres seulement** : combien de membres sont `online`, le « X » de « X/Y en ligne »), `levels[]` (chaque niveau EXTRÊME officiel : `best` et `teams[]` avec `mode` `solo`/`duo`/`trio`, `team_size`, `score`, `finished`, `players`, `rank` sur sa course), `me` (`role`, `request`), `requests[]` (**chef et officiers seulement**), `conversation_id` (membres seulement) |
| `POST /clans` `{name, tag, motto?, emblem, join_policy}` | crée le clan (`201`) ; le créateur en est le chef ; sa discussion est créée avec lui — `409 ALREADY_IN_CLAN` / `409 TAG_TAKEN` |
| `PATCH /clans/{id}` `{name?, tag?, motto?, emblem?, join_policy?}` | modifier le clan (chef) — `403 NOT_CLAN_LEADER`, `409 TAG_TAKEN` |
| `DELETE /clans/{id}` `{confirm: true}` | dissoudre (chef) : membres, demandes, discussion et points disparaissent ; les membres sont prévenus |
| `POST /clans/{id}/join` `{message?}` | clan ouvert ou invitation reçue : entrée immédiate (`200 {joined: true}`) ; clan sur candidature : candidature (`201`, message remplacé si renvoyée ; 5 en attente au plus par joueur) — `403 INVITE_ONLY`, `409 CLAN_FULL`, `409 ALREADY_IN_CLAN`, `409 TOO_MANY_APPLICATIONS` |
| `DELETE /clans/{id}/join` | retirer ma candidature ou refuser l'invitation |
| `POST /clans/{id}/invites` `{username \| player_id}` | inviter (chef, officiers) ; un joueur qui avait postulé entre aussitôt — `403 NOT_CLAN_OFFICER`, `403 BLOCKED`, `409 CLAN_FULL` |
| `DELETE /clans/{id}/invites/{playerId}` | retirer une invitation (chef, officiers) |
| `POST /clans/{id}/requests/{playerId}` `{accept}` | accepter ou refuser une candidature (chef, officiers) — `409 CLAN_FULL`, `409 REQUEST_GONE` |
| `PATCH /clans/{id}/members/{playerId}` `{role: officer\|member}` | nommer ou retirer un officier (chef) — `409 IS_LEADER` |
| `POST /clans/{id}/transfer` `{player_id}` | transmettre le rôle de chef (l'ancien chef devient officier) ; deux transferts simultanés : un seul aboutit (`409 TRANSFER_FAILED`) |
| `DELETE /clans/{id}/members/{playerId}` | quitter (soi-même) ou exclure : le chef exclut officiers et membres, un officier les membres seulement (`403 CANNOT_EXCLUDE_OFFICER`), personne le chef (`403 CANNOT_EXCLUDE_LEADER`) ; `{left, dissolved, leader}` |

**Points et classement** : `clan_level_bests` garde, pour chaque clan, sa meilleure tentative
**VÉRIFIÉE** (`race_runs.status = 'VERIFIED'`, score calculé par le serveur à partir du replay) sur chaque
niveau EXTRÊME **officiel** (révision courante et révisions gardées par « Garder les records ») ; les
niveaux de la communauté ne comptent pas (n'importe qui pourrait en publier un facile). `team_size` (1 =
Solo) est prêt pour la coopération Duo et Trio. Les points d'un clan sont la somme de ces meilleurs
résultats ; ils sont recalculés à chaque tentative vérifiée d'un membre, à chaque entrée ou départ, à
chaque révision publiée et chaque nuit. À égalité, le clan qui a atteint ses points le premier passe devant.

**Discussion** : une conversation de groupe de SOCIAL (`conversations.clan_id`) dont les membres suivent
ceux du clan ; les routes de groupe (`PATCH`, ajout, départ) répondent `409 CLAN_CONVERSATION`. Dans
SOCIAL, sa conversation porte `clan: {id, tag}`.

**Tag à côté du pseudo** : `clan_tag` dans les joueurs des classements (niveaux, planètes, pays, amis,
semaine, défis, course à la vérification — aussi pour un profil privé : un clan est public), de
`GET /friends`, des membres et messages des conversations ; `clan` (`id`, `name`, `tag`, `role`,
`emblem`) dans `GET /players/{id}`. Notifications de type `clan` (`event` : `invite`, `apply`,
`accepted`, `kicked`, `leader`, `dissolved`). L'export des données (`GET /players/me/export`) contient
`clan` et `clan_requests`.

**Administration** (rôle admin) : `GET /admin/clans?q=` (avec le chef), `PATCH /admin/clans/{id}`
`{name?, tag?, reason?}` (Journal : `admin_clan_renamed`, un tag pris : `admin_refused`),
`DELETE /admin/clans/{id}` `{reason?}` (Journal : `admin_clan_dissolved`, membres prévenus). Le Journal
les filtre avec `kind=clan`.

### 8.8 ter Raids de clan « Asterion », phase 1 (En ligne → CLANS › RAIDS)

Migration `0141_raids.sql`, code `server/src/raids.ts`. Les combats viennent plus tard (leur résultat sera
décidé par le consensus des joueurs de chaque instance) : ici, les événements, les inscriptions, les rappels
et les notifications. **Toutes les heures sont en UTC** (`AAAA-MM-JJTHH:MM[:SS[.mmm]]Z`) : le jeu convertit
vers le fuseau de l'ordinateur (changement d'heure compris) et envoie de l'UTC ; une heure avec décalage est
refusée (`400`). Le joueur est toujours celui de la session ; chaque écriture revérifie le rôle dans sa
propre requête SQL. Formats : `court` 25 à 30 min, `standard` 45 à 60 min, `legendaire` 75 à 90 min ;
`lock_at` = début − `lock_minutes` (30 par défaut), `ends_at` = début + durée maximale du format.

États (`state`, calculés depuis l'UTC) : `brouillon` (événement mondial non publié), `inscription`
(inscriptions ouvertes, places libres), `programme` (fermées ou complet), `imminent` (verrouillées),
`en_cours`, `termine`, `annule`. Vue d'un événement : `{ id, kind: "world"|"clan", raid, name,
display_name, display_title, format, format_min, format_max, starts_at, lock_at, ends_at, lock_minutes,
registration_open, capacity (0 : illimité), clan_capacity, difficulty, state, published, cancelled,
organizer: {id, tag, name}|null, clans, players, my_clan: {registered, players, groups, leader},
registered }` ; 2.77.4 : `reviews: {average, count}` (les avis des joueurs, sur les vues de `GET /raids`, de la fiche et de `GET /admin/raids`).

| Méthode | Route | Rôle / corps / réponse |
|---|---|---|
| GET | `/raids` | session facultative : `{ server_time, raids, clan, world (5 prochains événements mondiaux publiés), clan_events (raids de mon clan et ceux où il participe), invitations [{event, by, created_at}], rewards: [] (rien avant les combats), history (10 derniers terminés ou annulés), unread (notifications « raid » non lues), prefs }` |
| GET | `/raids/events/{id}` | session facultative : `{ server_time, event, description, rewards_text, my: {clan, entry: {leader, instructions, registered_by}, registered, can_manage, can_call, is_organizer}, roster (les inscrits de mon clan ; 2.72 : `presence` `online`/`away`/`offline` de chacun encore membre du clan, une présence masquée se lit `offline`), roster_online (2.72 : combien d'inscrits sont `online`, seulement avec le `roster` de mon clan), groups [{number, leader, members}] (groupes de 8 : le chef de raid mène le groupe 1), participants, invitations (organisateur), invitation (clan invité) }` ; un raid de clan n'existe (`404 RAID_NOT_FOUND`) que pour son clan, les clans qui y participent et ceux invités |
| POST | `/raids/events` | 2.77.4 : **refusé** à tous, `403 RAID_EVENTS_ONLY` (« Les raids sont des événements publiés par l'équipe du jeu… ») : seuls les administrateurs créent et publient les événements (`/admin/raids`). Les raids de clan programmés avant restent lisibles et jouables jusqu'à leur fin |
| DELETE | `/raids/events/{id}` | chef ou officier du clan organisateur : annule (inscrits et clans invités prévenus) ; `403 NOT_ORGANIZER` |
| POST | `/raids/events/{id}/clan` | chef ou officier : inscrit son clan à un événement mondial → `201`, `200 {already}` ; deux officiers en même temps : une seule inscription ; `409 NOT_WORLD_EVENT` / `REGISTRATION_CLOSED` / `RAID_LOCKED` |
| PATCH | `/raids/events/{id}/clan` | chef ou officier : `{ leader_id? (membre du clan, null), instructions? (600) }` |
| DELETE | `/raids/events/{id}/clan` | chef ou officier : retire le clan (avant le verrouillage), ses inscriptions avec |
| POST | `/raids/events/{id}/register` | le joueur lui-même, pour son clan inscrit → `201 {event}` ; capacité de l'événement et du clan vérifiées dans l'INSERT (jamais deux joueurs sur la dernière place) ; `403 NO_CLAN`, `409 CLAN_NOT_REGISTERED` / `RAID_LOCKED` / `REGISTRATION_CLOSED` / `RAID_FULL` / `CLAN_FULL` / `RAID_CANCELLED` |
| DELETE | `/raids/events/{id}/register` | se désinscrire jusqu'au verrouillage (`409 RAID_LOCKED` ensuite) |
| POST | `/raids/events/{id}/invites` | organisateur (chef, officier) : `{ tag }` invite un autre clan à un raid commun (10 au plus) ; `409 ALREADY_INVITED` |
| POST | `/raids/events/{id}/invitation` | chef ou officier du clan invité : `{ accept }` ; accepté : le clan participe ; `409 ALREADY_ANSWERED` |
| POST | `/raids/events/{id}/call` | chef, officier ou chef de raid : prévient les membres non inscrits (une fois tant que non lue) → `{ notified }` |
| GET / PUT | `/raids/prefs` | `{ reminders: [1440, 60, 10] (sous-ensemble), member_updates: bool }` |
| GET | `/raids/events/{id}/reviews?limit=&offset=` | 2.77.4, session facultative (ceux qui voient la fiche) : `{ summary: {average (une décimale, null sans avis), count}, me: {played, can_review, review} \| null, reviews [{id, author, stars, body, has_body, hidden, hidden_reason, created_at, edited_at, mine, reported, can_report}], total }` ; mon avis à part, les auteurs bloqués exclus ; un commentaire masqué n'a pas de texte sauf pour son auteur |
| PUT | `/raids/events/{id}/review` | 2.77.4 : `{ stars (1-5), body? (500 caractères) }` → `201` (écrit) ou `200` (modifié) `{ review, summary, created }` ; un avis par joueur et par événement ; `403 RAID_NOT_PLAYED` sans place dans une instance de l'événement **et** contribution validée (données du serveur) ; filtres et limite `community_comment` des commentaires |
| DELETE | `/raids/events/{id}/review` | 2.77.4 : supprime mon avis → `{ deleted, summary }` ; `404 REVIEW_NOT_FOUND` |
| POST | `/raids/reviews/{reviewId}/report` | 2.77.4 : `{ reason }` (motifs des commentaires) ; jamais le sien (`403 OWN_REVIEW`) ; masqué après 3 signalements ; limite `community_report` |
| GET | `/clans/{id}/raids` | 2.78.1, membres du clan (session) et administrateurs : les 20 derniers raids **terminés** du clan (Cœur tombé ou événement fini, jamais annulés) `{ clan, raids [{id, kind, format, starts_at, ends_at, end_at, duration_s, heart_fallen, fallen_at, final, parts_down, parts_total}] }` ; `403 NOT_CLAN_MEMBER`, `404 CLAN_NOT_FOUND` |
| GET | `/clans/{id}/raids/{eventId}/recap` | 2.78.1, fiche récapitulative (mêmes lecteurs) : `{ clan (emblème), event, viewer {member}, pending {tables, objectives}, reviews {average, count}, world (résultat officiel, rang du clan), people {registered, players, groups, reinforcements}, figures {parts_down, zones_done, zones_by_clan, relics, revives, deaths, wiped, retaken…}, parts [{part, boss, fallen, at, by {clan_tag, group, reinforcement, mine, me}, relic, phase}], heart {…, groups, clan_groups, clan_share}, best {catégorie: {player {id, username, avatar, me}, value, share}}, rewards [{reward, name, kind, ref, players, mine, gigaboss}], titles, contributions {zone_tables, raid_totals} (le tableau des contributions du clan, comme `/results`), me {played, groups, parts, zones, revives, deaths, heart_share, heart_rank, rewards} }` ; `deaths` : `null` quand aucun tableau des zones ne les compte (avant la 2.78) — seulement des données acceptées par le serveur ; `404 RAID_NOT_FOUND` pour un raid pas terminé ou sans le clan |
| GET | `/admin/raids/{id}/reviews` | 2.77.4, admin : tous les avis, masqués compris, avec `report_count`, `reasons`, `hidden_by_reports` |
| POST | `/admin/raids/{id}/reviews/{reviewId}/moderate` | 2.77.4, admin : `{ action: hide \| restore }` masque (la note compte toujours) ou rétablit un commentaire (signalements effacés) ; Journal `admin_raid_review_hidden` / `admin_raid_review_restored` |
| GET | `/admin/raids` | admin : les événements mondiaux (brouillons compris) avec leurs inscriptions |
| POST | `/admin/raids` | admin : `{ format, starts_at, lock_minutes? (0-240), registration_open?, capacity? (0 : illimité), clan_capacity? (1-50), difficulty? (1-5), rewards_text? (300) }` → brouillon (Journal `admin_raid_created`) |
| GET | `/admin/raids/{id}` | admin : l'événement et ses clans inscrits (chef de raid, inscrits) |
| PATCH | `/admin/raids/{id}` | admin : les mêmes champs ; publié : nouvelle heure → inscrits prévenus et rappels recalculés ; inscriptions rouvertes → tous les clans prévenus (une fois tant que non lue) ; `409 RAID_STARTED` (Journal `admin_raid_updated`) |
| POST | `/admin/raids/{id}/publish` | admin : publie et prévient tous les membres de tous les clans → `{ event, notified }` ; `409 ALREADY_PUBLISHED` / `RAID_TOO_LATE` (Journal `admin_raid_published`) |
| DELETE | `/admin/raids/{id}` | admin : `{ reason? }` annule (inscrits et chefs des clans inscrits prévenus ; Journal `admin_raid_cancelled`) |
| POST | `/admin/raid-tests` | 2.80, admin : `{ mode: zone \| raid, config, fingerprint }` ouvre un **test d'Asterion avec joueurs invités** (réglages vérifiés champ par champ et récrits ; un test ouvert du même administrateur se termine) → `201 { test, invitations }` ; son relais : un `CoopRoom` marqué test (limite `raid_test_create`) |
| GET | `/admin/raid-tests/{id}` | 2.80, son administrateur : `{ server_time, test, invitations [{player {id, username, role}, group, invincible, status (pending, accepted, declined, expired, cancelled), expires_at, playing {group, seat, connected} \| null}] }` |
| GET | `/admin/raid-test-players?q=&test=&limit=&offset=` | 2.80, admin : les joueurs **connectés maintenant** (présence montrée et récente), actifs, jamais soi-même ni un joueur bloqué, sans ceux déjà invités au test → `{ players [{id, username, role, presence}], total, has_more }` |
| POST | `/admin/raid-tests/{id}/invitations` | 2.80, son administrateur : `{ player_id, group?, invincible? }` invite (3 minutes ; notification `raid`, `event: test_invite`) ; `409 PLAYER_OFFLINE`, `RAID_TEST_ALREADY_INVITED`, `RAID_TEST_GROUP_FULL` (plus de joueur simulé à remplacer), `RAID_TEST_FULL` (7 invités) ; limite `raid_test_invite` |
| PATCH | `/admin/raid-tests/{id}/invitations/{playerId}` | 2.80, son administrateur : `{ invincible?, group? }`, donné par le relais à tous les jeux du test au premier pas suivant |
| DELETE | `/admin/raid-tests/{id}/invitations/{playerId}` | 2.80, son administrateur : annule ; un joueur dans le test en sort (END « retiré »), son joueur simulé reprend sa place |
| POST | `/admin/raid-tests/{id}/seat` | 2.80, son administrateur : `{ group }` sa place dans le raid complet simulé (`-1` : il regarde), donnée à un pas |
| POST | `/admin/raid-tests/{id}/end` | 2.80, son administrateur : termine le test (invitations en attente expirées, chaque jeu prévenu) |
| GET | `/raid-tests/invitations` | 2.80, joueur : `{ server_time, invitations [{test_id, mode, title, config, fingerprint, group, invincible, admin, expires_at, status (pending \| accepted)}] }` ; aussi `raid_test_invitations` de `GET /social/unread` |
| POST | `/raid-tests/{id}/accept`, `/raid-tests/{id}/decline` | 2.80, l'invité : accepter demande `{ fingerprint }` identique à celle du test (`409 RAID_TEST_VERSION` sinon) ; `409 RAID_TEST_INVITATION_EXPIRED`, `RAID_TEST_ENDED` |
| GET | `/raid-tests/{id}/socket` | 2.80 : le relais du test (WebSocket), à son administrateur et aux invités qui ont accepté ; message `TEST_SEATS` (19) : à partir d'un pas, pour chaque place du relais, son groupe, sa place du test et « Invincible » ; `TEST_ORDER` (20) : un ordre de l'administrateur aux joueurs simulés (2.79), envoyé par son jeu (`u8 kind 1-6, u16 x, u16 y`, jamais par un invité) et donné à tous à partir d'un pas. Un test ne rapporte jamais rien : `POST /raids/instances/{id}/reports` nommant un test → `403 RAID_TEST_REPORT` |

Notifications de type `raid` (centre de notifications, filtre `raids`, `POST /notifications/read-all?filter=raids`) :
`data.event` = `world`, `open`, `scheduled`, `clan_registered`, `joined`, `reminder` (`minutes`), `start`,
`member_in` / `member_out` (regroupées tant que non lues, pour le chef, les officiers et le chef de raid, sauf
`member_updates: false`), `invite`, `invite_accepted`, `invite_declined`, `leader`, `call`, `cancelled`,
`moved`, `withdrawn` ; avec `event_id`, `raid_name`, `format`, `raid_kind`, `starts_at`. Jamais d'heure dans
la phrase : chaque jeu l'affiche à son fuseau. **Rappels** : le cron `* * * * *` (`raidTick`) envoie 24 h,
1 h et 10 min avant (au choix de chaque joueur) puis au début, chacun dans sa fenêtre seulement et **jamais
deux fois** (`raid_reminders_sent`, écrit dans la même transaction que les notifications ; un événement
déplacé rappelle de nouveau). Un membre qui quitte son clan perd ses inscriptions aux raids à venir. Journal
(`GET /admin/log?kind=raid`) : `raid: { id, name, format, starts_at, changes, notified }`.

### 8.9 Niveaux de la communauté (NIVEAUX EN LIGNE)

Les joueurs publient leurs niveaux ; le serveur ne fait confiance à rien de ce qui est envoyé.

**Publication (compte requis)** — trois requêtes :

1. `POST /community/levels` `{community_id?, title (3-60), description (≤ 500), planet, difficulty
   (0,5-5, note calculée par le jeu), level_hash, min_game_version}` → `201 {id, version, upload:
   {package, replay}}`. Avec `community_id` (auteur seulement) : nouvelle version du même niveau.
   `level_hash` est l'**empreinte de gameplay** (`fingerprintGameplay`, côté jeu) : géométrie,
   objets, événements, cinématiques, minuterie, pouvoirs de départ, définitions des ennemis et
   pouvoirs, joueur, version de simulation — sans nom, auteur, musique ni métadonnées, et
   insensible aux renommages faits à l'installation.
2. `PUT …/versions/{v}/package` (`application/octet-stream`, 16 Mo au plus) : le `.cplanet` est
   vérifié avant d'être stocké — ZIP lu par son répertoire central, aucun chiffrement, chemins
   sûrs (ni absolu, ni `..`, ni `\`, ni lecteur, ni caractère de contrôle), tout sous
   `COSMIC_LEVEL/`, extensions de données seulement (json, png, jpg, bmp, wav, ogg, mp3, flac,
   ttf, otf, txt, md), 2 000 fichiers et 64 Mo décompressés au plus, taux de compression
   plafonné, `manifest.json` et `level.json` valides, un départ et une sortie. La vignette
   (`metadata/thumbnail.png`) est extraite. Erreur : `422 PACKAGE_INVALID` avec la raison.
3. `PUT …/versions/{v}/replay` : le replay de **fin de niveau de l'auteur** sur cette version
   (même `level_hash`, trajectoire du départ à la sortie, durée cohérente) — un niveau impossible
   ne peut pas être publié. `422 REPLAY_LEVEL_MISMATCH` / `REPLAY_REJECTED` sinon.

La version est publiée quand les deux envois sont acceptés (`published: true`) ; l'ancienne
version passe `superseded` (ses temps restent dans son classement).

| Route | Rôle |
|---|---|
| `GET /community/levels?sort=popular\|hardest\|recent\|top\|friends\|mine&q=&planet=&limit=&offset=` | listes ; `top` = moyenne bayésienne des notes, `friends` et `mine` demandent une session, `q` cherche dans le titre (sans accents) et le pseudonyme de l'auteur |
| `GET /community/levels/{id}` | fiche, versions, classement de la version courante |
| `GET /community/levels/{id}/package?version=` | le `.cplanet` (versions publiées ; l'auteur voit les siennes), en-têtes `x-level-hash` et `x-content-sha256` ; compte un téléchargement (sauf l'auteur) |
| `GET /community/levels/{id}/thumbnail` | vignette PNG |
| `POST /community/levels/{id}/rate` `{stars 1-5}` | une note par joueur (modifiable), jamais sur son propre niveau |
| `POST /community/levels/{id}/report` `{reason?}` | un signalement par joueur ; 5 signalements masquent le niveau en attendant la modération |
| `DELETE /community/levels/{id}` | l'auteur retire son niveau (fichiers R2 supprimés) |
| `POST /community/levels/{id}/records` `{version, attempt_id, time_ms, deaths}` puis `PUT /community/records/{id}/replay` | un temps sur un niveau de la communauté ; il n'entre au classement (`REPLAY_OK`) qu'après vérification du replay (structure, version du niveau, tentative, durée, trajectoire, objets et dégâts lus dans le replay) ; sinon `REJECTED` avec les raisons |
| `GET /community/levels/{id}/leaderboard?version=` | meilleur temps de chaque joueur ; `verification` rappelle que ces temps ne sont pas re-simulés |
| `GET /community/records/{id}/replay` | replay d'un temps classé |

Côté jeu : *ATELIER → Mes niveaux → Publier* (le niveau doit avoir été terminé depuis son départ
dans l'éditeur, F5 ; le package ne contient que le niveau et ses fichiers propres, les fichiers
du jeu étant sur chaque installation de la même version) ; *En ligne → NIVEAUX EN LIGNE* (Populaires,
Plus durs, Récents, Mieux notés, Amis, Recherche, Les miens ; *Télécharger et jouer* passe le
package par les mêmes vérifications que tout import `.cplanet` et contrôle l'empreinte de
gameplay de la copie installée).

### 8.9 ter Workshop : favoris, commentaires, créateurs, abonnements, notifications, temps joué

Toutes ces routes identifient le joueur par sa session (jamais par un identifiant envoyé) ;
le serveur vérifie lui-même chaque droit (auteur, créateur du niveau, blocages).

**Favoris** (un par joueur et par niveau ; un favori survit au retrait du niveau et apparaît
« indisponible », avec le titre et l'auteur qu'il avait).

| Méthode | Route | Réponse |
|---|---|---|
| POST / DELETE | `/community/levels/{id}/favorite` | `{ favorite, favorites }` (idempotent) |
| GET | `/community/favorites?sort=added\|title\|rating\|downloads\|updated&filter=all\|available\|unavailable&q=` | `{ favorites: [{ level_id, title, author_name, available, unavailable_reason (deleted\|hidden\|private), favorited_at, version, downloads, favorites, comments, rating, thumbnail_url }] }` paginé |

Les cartes de niveau (`GET /community/levels…`) donnent aussi `favorites`, `comments` et
`my_favorite`.

**Commentaires** (un seul niveau de réponses ; 1 000 caractères, 2 liens au plus ; le même
texte n'est accepté qu'une fois par 24 h ; les auteurs bloqués dans un sens ou l'autre ne se
voient pas).

| Méthode | Route | Règle |
|---|---|---|
| GET | `/community/levels/{id}/comments?sort=recent\|popular\|oldest` | commentaires principaux, épinglés d'abord ; `{ comments, comment_count }` paginé |
| GET | `/community/comments/{id}/replies` | réponses, des plus anciennes aux plus récentes |
| POST | `/community/levels/{id}/comments` `{ body, parent_id? }` | `201 { comment }` ; une réponse à une réponse est rangée sous le même commentaire |
| PATCH | `/community/comments/{id}` `{ body }` | auteur seulement (`edited_at`) |
| DELETE | `/community/comments/{id}` | **auteur seulement** : le créateur du niveau ne peut pas effacer un avis, même négatif ; un commentaire qui a des réponses reste affiché « supprimé » |
| POST / DELETE | `/community/comments/{id}/pin` | créateur du niveau, commentaires principaux, 3 au plus |
| PUT / DELETE | `/community/comments/{id}/reaction` `{ kind }` | `like`, `love`, `funny`, `impressive`, `not_useful` : une réaction active par joueur, modifiable ; `{ reactions, my_reaction }` |
| POST | `/community/comments/{id}/report` `{ reason }` | `spam`, `harassment`, `inappropriate`, `spoiler`, `other` ; masqué après 3 signalements, en attente de modération (son auteur le voit encore, marqué `hidden`) |

Chaque commentaire : `{ id, parent_id, author, creator, level_author, body, deleted, hidden,
pinned, created_at, edited_at, reply_count, reactions, reaction_total, my_reaction, reported,
mine, can_edit, can_pin }`.

**Créateurs et abonnements** (est créateur tout joueur qui a au moins un niveau publié : statut
calculé par le serveur, jamais déclaré par le jeu ; suivre n'est pas une amitié).

| Méthode | Route | Réponse |
|---|---|---|
| GET | `/players/{id}` | en plus du profil : `creator { is_creator, levels_published, downloads, unique_players, favorites, rating, first_published_at, followers, following }`, `follow { following, followed_by, notify, blocked }`, `playtime` |
| GET | `/players/{id}/creations?sort=popular\|recent\|top` | niveaux publiés (et brouillons pour soi) |
| POST | `/players/{id}/follow` `{ notify? }` | `{ following, notify, followers }` ; jamais soi-même ni en cas de blocage ; seul un nouvel abonné est notifié |
| DELETE | `/players/{id}/follow` | `{ following: false, followers }` |
| GET | `/players/{id}/followers`, `/players/{id}/following` | listes (`403 FOLLOWS_PRIVATE` si le joueur les a rendues privées : `PATCH /players/me { follows_visibility: "private" }`) |
| GET | `/creators?q=&sort=followers\|levels\|downloads\|recent` | recherche de créateurs (profils publics) |

Un blocage retire les abonnements dans les deux sens.

**Centre de notifications** (les événements répétés non lus sont regroupés : réactions à un
même commentaire, abonnés…).

| Méthode | Route | Réponse |
|---|---|---|
| GET | `/notifications?filter=all\|unread\|creators\|comments\|projects` | `{ notifications: [{ id, kind, text, count, actor, level_id, comment_id, created_at, read }], unread }` paginé |
| GET | `/notifications/unread` | `{ unread }` |
| POST | `/notifications/{id}/read`, `/notifications/read-all` | lu |
| DELETE | `/notifications/{id}` | supprimée |
| GET / PUT | `/notifications/settings` `{ <kind>: bool }` | catégories reçues (`new_level`, `level_update`, `comment_reply`, `comment_reaction`, `level_comment`, `new_follower`, `project_invite`, `project_role`, `project_version`, `project_mention`, `project_reply`, `project_removed`, `race_verified` et, depuis 2.19, `run_comment` : commentaire sur une de mes courses ; depuis 2.32, `challenge_featured` : un de mes niveaux est le défi du jour — depuis 2.33, aussi de la semaine ou du mois) |

Le jeu lit les notifications non lues à la connexion puis chaque minute ; les nouvelles sont
annoncées une fois. Les notifications lues de plus de 60 jours sont purgées (500 au plus par
joueur).

**Temps joué** (profils : total et par niveau, en heures et minutes).

| Méthode | Route | Règle |
|---|---|---|
| POST | `/players/me/playtime` `{ sessions: [{ level_id, seconds }] }` | 50 entrées au plus, 6 h au plus chacune ; le serveur n'accepte jamais plus que le temps écoulé depuis le précédent envoi (+ 5 min) : `{ accepted_seconds, playtime }` |
| GET | `/players/{id}/playtime` | `{ playtime: { total_seconds, levels: [{ level_id, title, seconds, last_played_at }] } }` (profils visibles) |

`level_id` : identifiant d'un niveau officiel, d'un niveau de la communauté, ou `other` (niveaux
personnels, jamais détaillés). Le jeu garde le temps à envoyer dans `online/playtime.json`
tant qu'il n'a pas été accepté.

### 8.9 quater Projets collaboratifs (En ligne → PROJETS)

Un projet est un niveau construit à plusieurs. Rôles, du moins au plus étendu, **tous vérifiés
par le serveur** : `viewer` (lecteur : voit le projet, ses membres, son activité, télécharge une
copie) < `tester` (joue la dernière version) < `editor` (envoie de nouvelles révisions) <
`admin` (invitations, demandes, rôles des éditeurs/testeurs/lecteurs, réglages) < `owner`
(administrateurs, transfert, suppression). Un non-membre ne voit qu'un projet listé
(`request` ou `open`), sans son contenu ; un projet `private` ou `invite` lui répond 404.

| Méthode | Route | Rôle | Règle |
|---|---|---|---|
| POST | `/projects` `{ name, description?, visibility?, join_role?, planet?, content?, message? }` | — | crée le projet (30 par joueur) ; `content` (niveau JSON, 2 Mo) devient la révision 1 |
| GET | `/projects` | — | mes projets et mon rôle |
| GET | `/projects/discover?q=` | — | projets `request` / `open` (blocages exclus) |
| GET / PATCH / DELETE | `/projects/{id}` | — / admin / owner | fiche (`my_role`, `can`, `last_revision`) ; réglages (`name`, `description`, `visibility` : `private` \| `invite` \| `request` \| `open`, `join_role`) ; suppression avec ses fichiers |
| GET | `/projects/{id}/members` | viewer | membres et rôles |
| PATCH | `/projects/{id}/members/{playerId}` `{ role }` | admin | un admin gère éditeurs, testeurs, lecteurs ; seul le propriétaire nomme ou retire un admin |
| DELETE | `/projects/{id}/members/{playerId}` | soi / admin | quitter ou retirer ; le propriétaire doit d'abord transférer |
| POST | `/projects/{id}/transfer` `{ player_id }` | owner | le nouveau propriétaire doit être membre ; l'ancien devient admin |
| POST | `/projects/{id}/invites` `{ role, max_uses?, expires_hours?, player_id? }` | admin | clé `CPJ-…` (128 bits aléatoires, **renvoyée une seule fois**, stockée en SHA-256) et lien `cosmic://rejoindre/<clé>` ; ou invitation directe d'un joueur (notifiée) |
| GET / DELETE | `/projects/{id}/invites`, `/projects/{id}/invites/{inviteId}` | admin | liste (statut `active` \| `used` \| `expired` \| `revoked`, jamais la clé) ; révocation |
| POST | `/projects/join` `{ key }` | — | clé ou lien ; utilisations comptées atomiquement ; même réponse `409 INVITE_INVALID` pour une clé inconnue, mal formée, épuisée, expirée ou révoquée |
| GET | `/projects/invitations` | — | mes invitations directes |
| POST / DELETE | `/projects/invitations/{inviteId}/accept`, `/projects/invitations/{inviteId}` | — | accepter / refuser |
| POST / DELETE | `/projects/{id}/join` `{ message? }` | — | rejoindre un projet `open` (avec `join_role`) ou demander à rejoindre un projet `request` ; annuler sa demande |
| GET / POST | `/projects/{id}/requests`, `/projects/{id}/requests/{playerId}` `{ accept, role? }` | admin | demandes en attente ; accepter (rôle notifié) ou refuser |
| GET | `/projects/{id}/content?revision=` | viewer | le niveau (JSON brut, en-têtes `x-project-revision`, `x-content-sha256`) |
| PUT | `/projects/{id}/content` `{ base_revision, content, message? }` | editor | nouvelle révision ; faite d'après une révision dépassée : **`409 REVISION_CONFLICT`** (`details.current_revision`, `saved_by`, `saved_at`), rien n'est écrasé |
| GET | `/projects/{id}/revisions?author=&q=`, `/projects/{id}/activity` | viewer | révisions (auteur, message, empreinte, `live` pour l'édition en direct, `version` si elle est nommée), filtrées par auteur ou par mots du message ; journal du projet |

Un joueur bloqué par le propriétaire (ou qui l'a bloqué) ne peut ni rejoindre ni être invité.
Dans le jeu, chaque membre travaille sur une copie locale (un niveau de ATELIER → Mes niveaux,
lié au projet dans `online/projects.json`) ; « Récupérer la dernière version » garde d'abord une
copie modifiée sous un autre nom. L'installateur peut associer les liens `cosmic://` au jeu :
ouvrir un lien place la clé dans PROJETS → Rejoindre, sans rejoindre automatiquement.

### 8.9 quinquies Édition en direct (présence, verrous, opérations)

Chaque projet a une « salle » (Durable Object `ProjectRoom`) atteinte seulement par le Worker,
qui vérifie la session et le rôle à chaque requête. La présence (curseur, vue, sélection,
activité, couleur) n'existe qu'en mémoire et disparaît 20 s après le dernier signe de vie ; les
opérations sont persistantes (D1 `project_ops`) et numérotées par la salle, dans le même ordre
pour tous.

| Méthode | Route | Rôle | Réponse |
|---|---|---|---|
| POST | `/projects/{id}/live/join` `{ view? }` | lecteur | `{ doc, seq, revision, epoch, chat_seq, color, participants, locks }` (le document courant : dernière révision + opérations suivantes) |
| POST | `/projects/{id}/live/heartbeat` `{ since_seq, epoch?, cursor?, view?, selection?, activity? }` | lecteur | `{ seq, revision, epoch, chat_seq, participants, locks, ops, resync }` (`resync` : rejoindre, le document a été remplacé par une restauration ou un enregistrement) ; toutes les ~0,6 s ; plus de 5 par seconde : réponse courte `{ throttled }` |
| POST | `/projects/{id}/live/ops` `{ ops: [{ id, base_seq, op }] }` | éditeur | `{ results: [{ id, status, seq?, by?, current? }], seq }` ; 50 opérations au plus par requête, 300 par membre et par 10 s (au-delà : `429 RATE_LIMITED` avec `Retry-After`, rien n'est appliqué ; le jeu garde ses opérations et les renvoie) |
| POST | `/projects/{id}/live/leave` | lecteur | quitte la salle |

Opérations : `{ t: "add", o }` (objet avec `id` et `type`), `{ t: "set", id, f }` (champs de
l'objet ; `null` retire un champ), `{ t: "del", id }`, `{ t: "doc", k, v }` (clé du document autre
que `objects` et `id`) ; 64 Ko au plus chacune. Statuts : `ok` ; `conflict` (un autre membre a
modifié la même chose après `base_seq` : `current` donne sa version) ; `locked` (sélectionné par
un autre membre : verrou temporaire) ; `not_found` (supprimé) ; `id_taken` ; `invalid`. Une
opération renvoyée avec le même `id` (réponse perdue) n'est appliquée qu'une fois. Les objets
sélectionnés par un éditeur sont verrouillés pour les autres (les sélections des lecteurs et
testeurs ne verrouillent rien). Toutes les 200 opérations, et quand le dernier membre quitte la
salle, le document devient une nouvelle révision (« Édition en direct (opérations a à b) »). Un
enregistrement classique (`PUT /projects/{id}/content`) faite alors que des opérations ne sont
pas encore dans une révision est refusé (`409 LIVE_CHANGES`) : rien n'est écrasé.

Dans le jeu (PROJETS → « Édition en direct »), l'éditeur envoie la différence entre le document
connu de la salle et le sien après chaque modification (y compris annuler / rétablir), applique
les opérations des autres au niveau et à tout l'historique d'annulation (« Annuler » ne défait que
ses propres modifications), garde ses opérations non confirmées dans `online/live_<projet>.json`
(rien n'est perdu hors ligne) et montre chaque refus avec le choix de réappliquer sa modification
ou de garder la version du projet.

**Fiche d'un projet pour ses membres** : `counts` (`members`, `versions`, et pour les
administrateurs `requests` en attente et `invites` actives) et `stamp`, une empreinte qui change dès
que quelque chose change dans le projet (révision, opérations, membres, rôles, demandes,
invitations, versions, activité). Le jeu la relit toutes les 4 s et ne recharge l'onglet affiché
que si elle a changé ; la liste `GET /projects` donne aux administrateurs `pending_requests`.
Un membre retiré par un administrateur reçoit la notification `project_removed` ; s'il était dans
l'éditeur en direct, le jeu le ramène au menu principal (sa version du niveau est gardée comme
niveau indépendant).

### 8.9 septies Discussion du projet (éditeur en direct)

| Méthode | Route | Rôle | Règle |
|---|---|---|---|
| GET | `/projects/{id}/messages?after=&before=&limit=` | viewer | messages du plus ancien au plus récent (`after` : les nouveaux depuis un id ; `before` : les précédents ; 50 par défaut, 100 au plus) et `more` |
| POST | `/projects/{id}/messages` `{ body, reply_to? }` | viewer | 1 à 500 caractères, une ligne ; `reply_to` : un message du même projet (sinon `404 MESSAGE_NOT_FOUND`) ; 30 messages par minute et par joueur ; l'auteur du message auquel on répond reçoit `project_reply` (regroupées tant qu'elles ne sont pas lues) |

Chaque message : `{ id, body, created_at, author: { id, username, avatar }, reply_to: { id, username, excerpt } | null }`.
Les messages sont gardés avec le projet. La salle du projet donne `chat_seq` (id du dernier
message) avec `join` et chaque `heartbeat` : le jeu ne relit la discussion que quand il change.

### 8.9 sexies Historique et versions nommées

Une révision ne change jamais : son contenu (R2) est relu avec son SHA-256 à chaque lecture
(`500 REVISION_CORRUPTED` s'il ne correspond plus). Restaurer une ancienne révision en crée une
**nouvelle** ; rien n'est effacé de l'historique.

| Méthode | Route | Rôle | Règle |
|---|---|---|---|
| GET | `/projects/{id}/compare?from=&to=` | viewer | `{ from, to, counts: { added, removed, changed, settings }, added, removed, changed: [{ id, type, fields }], settings }` (200 objets au plus par liste) |
| POST | `/projects/{id}/revisions/{revision}/restore` | editor | `201 { saved, restored_from }` ; les opérations de l'édition en direct pas encore enregistrées deviennent d'abord une révision, puis la salle recharge le document (les membres présents le reçoivent : `epoch` change, `resync`) |
| POST | `/projects/{id}/versions` `{ name, revision?, notes? }` | editor | nomme une révision (la dernière par défaut) ; nom unique dans le projet (`409 VERSION_EXISTS`) ; **immuable** : aucune route ne modifie ni ne supprime une version ; les autres membres reçoivent la notification `project_version` |
| GET | `/projects/{id}/versions` | viewer | versions (nom, notes, révision, SHA-256, auteur, date) |
| GET | `/projects/{id}/versions/{versionId}/content` | viewer | le niveau de la version, vérifié contre l'empreinte enregistrée (`500 VERSION_CORRUPTED` sinon) |

Dans le jeu (PROJETS → onglets « Révisions » et « Versions ») : filtres par auteur et par texte,
comparaison de deux révisions, restauration (éditeurs, après confirmation), téléchargement d'une
révision ou d'une version comme niveau indépendant (ATELIER → Mes niveaux), création d'une
version nommée.

### 8.9 octies Course à la vérification (niveaux EXTRÊME)

Un niveau **EXTRÊME** se joue avec **une seule vie** (`lives = 1`, ou mode défi sans mort
autorisée) ; l'éditeur verrouille ce réglage. Un niveau de la communauté EXTRÊME est publié
**sans** la fin de niveau de son auteur : il déclare à la place le tracé de sa course
(`race` dans `POST /community/levels`), et le premier joueur qui le termine en devient le
**vérificateur**. L'enchaînement des boss de la campagne (`noyau_01`) est EXTRÊME ; son tracé
est enregistré avec la version du niveau (`level_versions.race`, migration 0027).

Tracé (`race`) : `{ route: [[x, y], …] (2 à 500 points, parcours prévu du départ à la sortie),
bosses: [{ id, name }], ambushes: [{ id, waves }] }`. Le paquet doit contenir un niveau
`extreme: true` à une seule vie (`422 EXTREME_LIVES`) et sa version un tracé
(`422 RACE_LAYOUT_MISSING`).

Score de progression (0 à 100, une décimale, arrondi vers le bas ; 100 seulement si la sortie
est atteinte) : `40 × parcours + 60 × parts obtenues / parts`, où chaque boss compte 2 parts
(selon la part de sa vie retirée) et chaque embuscade 1 part (vagues battues / vagues). Sans
boss ni embuscade, le parcours compte pour 100.

| Méthode | Route | Compte | Règle |
|---|---|---|---|
| POST | `/races/runs` `{ kind, level_id, version? \| level_hash?, attempt_id, time_ms, finished, bosses, waves }` | oui | `kind` : `community` (id en ligne, `version` facultative) ou `official` (`level_hash` facultatif) ; `bosses` : pour mille de vie retirée par boss du tracé ; `waves` : vagues battues par embuscade ; `201 { run: { id, status: PENDING }, upload }` ; `409 RUN_EXISTS` pour une tentative déjà envoyée ; 240 envois par heure |
| PUT | `/races/runs/{runId}/replay` | oui (le joueur de la tentative) | le replay (`application/octet-stream`) ; le serveur calcule le score : `{ run: { status: VERIFIED \| REJECTED, score, finished, reasons, rank }, verifier }` |
| GET | `/races/board?kind=&level_id=&version=\|level_hash=` | non | `{ race: { title, version, bosses, ambushes, participants }, verifier: { player, at } \| null, ranking: [{ rank, player, score, route, bosses_beaten, waves, finished, detail, time_ms, at, me }], my_pending }` (meilleure tentative vérifiée de chaque joueur, 100 au plus ; égalité : la plus ancienne d'abord) |

Ce que le serveur vérifie (sinon `REJECTED`, avec les raisons) : le replay est celui de la
tentative et de cette version du niveau ; au plus une mort et aucune réapparition ; départ au
point de départ ; trajectoire dans les limites du niveau et à une vitesse possible ; la vie
retirée aux boss et les vagues déclarées correspondent aux événements du replay (`BossHit`,
`WaveCleared`, tolérance de 1 % de vie) ; une tentative « finie » a l'événement de fin et
arrive dans la sortie. Le parcours est la plus lointaine projection de la trajectoire sur le
tracé déclaré. Le premier joueur dont la tentative finie est acceptée devient le vérificateur
(une seule fois par version, jamais remplacé) ; les autres participants et l'auteur reçoivent
la notification `race_verified`. Les cartes des NIVEAUX EN LIGNE donnent `extreme`,
`verified { player, at }` (pour la version courante) et `race_best`.

### 8.9 nonies Succès déclarés (profil)

`PUT /api/v1/players/me/achievements` (authentifié, 30 fois par heure) — corps
`{"unlocked": ["voyage_first", ...]}` (1000 identifiants au plus, `[a-z0-9_]`). La liste
**remplace** la précédente. Le serveur ignore les identifiants qu'il ne connaît pas et compte
lui-même les étoiles avec sa copie du catalogue (`src/achievementCatalog.json`, générée depuis
`data/achievements/achievements.json` par `scripts/achievements-catalog.mjs`) : le client
n'envoie jamais de nombre d'étoiles. Réponse : `{"achievements": {stars, count, total_stars,
total_count, declared: true, updated_at}}`.

`GET /api/v1/players/{id}` ajoute `achievements` (le même objet ; `null` pour un profil masqué).
Ces succès sont **déclarés par le jeu** : débloqués sur l'ordinateur du joueur, ils ne sont
pas vérifiés par le serveur et le profil le dit. Ils figurent dans l'export des données et
disparaissent avec le compte (table `player_achievements`, migration 0031).

Ghost Racing (2.18) n'ajoute aucune route : les fantômes en ligne sont les replays des
records déjà servis par `GET /api/v1/records/{id}/replay` (records vérifiés) et
`GET /api/v1/community/records/{id}/replay`. Le nouvel événement de replay `Split` (type 14,
temps de passage) est ignoré par l'analyse du serveur (voir l'annexe).

### 8.9 decies Campagne : notes des niveaux, commentaires des niveaux et des courses (2.19)

Le joueur est toujours celui de la session (jamais un identifiant envoyé). Trois ajouts autour
des niveaux officiels (`levels`) et de leurs classements :

**Notes des joueurs** (1 à 5 étoiles, distinctes de la difficulté /5 calculée par le jeu).
Seul un joueur qui a **terminé le niveau** peut le noter : le serveur exige un record
`VERIFIED` de ce niveau (toute révision, toute catégorie) ou une tentative terminée et
vérifiée de sa course à la vérification (niveaux EXTRÊME). Un temps `PENDING` (replay pas
encore envoyé ou vérification manuelle) ne suffit pas. La moyenne et le nombre de notes sont
recalculés par le serveur à partir des notes enregistrées (table `level_rating_stats`, mise à
jour dans le même lot que chaque changement et à la suppression d'un compte) ; le jeu n'envoie
jamais de moyenne.

| Méthode | Route | Auth | Réponse |
|---|---|---|---|
| GET | `/levels/ratings` · cache | facultative | `{ levels: [{ level_id, average, count }], my_ratings: { <level_id>: étoiles } \| null }` (niveaux notés seulement ; `my_ratings` : `null` en anonyme) |
| GET | `/levels/{levelId}/rating` · cache | facultative | `RatingView` |
| PUT | `/levels/{levelId}/rating` `{ stars }` | requise · `level_rate` | `RatingView` ; donne ou change sa note ; `403 LEVEL_NOT_COMPLETED` sans temps vérifié |
| DELETE | `/levels/{levelId}/rating` | requise · `level_rate` | `RatingView` ; retire sa note (idempotent) |

`RatingView` : `{ level_id, name, rating: { average, count }, my_stars, can_rate, reason,
comments }` — `average` arrondie au dixième (`null` sans note), `my_stars` (`null` sans note
du lecteur), `can_rate` (le lecteur a terminé le niveau), `reason` : `NOT_LOGGED_IN`,
`LEVEL_NOT_COMPLETED` ou `null`, `comments` = commentaires principaux visibles du niveau.
`stars` : entier de 1 à 5, aucun autre champ accepté. Erreurs : 400, 401, 403, 404
`LEVEL_NOT_FOUND`, 429.
```sh
curl -s -X PUT "$API/levels/sylvanox_01/rating" -H "authorization: Bearer $TOKEN" \
  -H 'content-type: application/json' -d '{"stars":4}'
```
```json
{ "ok": true, "data": { "level_id": "sylvanox_01", "name": "Les Ruines Suspendues",
  "rating": { "average": 4.3, "count": 128 }, "my_stars": 4, "can_rate": true, "reason": null,
  "comments": 12 }, "request_id": "…" }
```

**Commentaires** : mêmes règles que ceux des niveaux de la communauté (§8.9 ter : 1 000
caractères, 2 liens, même texte refusé 24 h dans le même fil, une seule profondeur de
réponses, réactions, signalements — masqué après 3 —, modération, suppression par l'auteur
seulement, blocages, limite `community_comment` partagée). Le jeu ne montre ceux des niveaux
officiels que dans la section En ligne.

| Méthode | Route | Auth | Règle |
|---|---|---|---|
| GET | `/levels/{levelId}/comments?sort=recent\|popular\|oldest` | facultative | fil d'un niveau officiel : `{ comments, comment_count }` paginé |
| POST | `/levels/{levelId}/comments` `{ body, parent_id? }` | requise | `201 { comment }` ; tout joueur connecté ; personne n'y épingle |
| GET | `/records/{recordId}/comments?sort=…` | facultative | fil d'une course (un record des classements) |
| POST | `/records/{recordId}/comments` `{ body, parent_id? }` | requise | `201 { comment }` ; l'auteur de la course est notifié (`run_comment`) et peut épingler 3 commentaires |

Les autres actions sur un commentaire, quel que soit son fil, passent par les routes
existantes : `GET /community/comments/{id}/replies`, `PATCH` / `DELETE
/community/comments/{id}`, `POST` / `DELETE …/pin`, `PUT` / `DELETE …/reaction`, `POST
…/report`. Chaque commentaire porte en plus `thread` (`community`, `level` ou `run`) ;
`level_author` y désigne le propriétaire du sujet (créateur du niveau, auteur de la course).
Une course n'est visible et commentable par les autres que si elle est `VERIFIED`, d'un
compte actif, et que son auteur n'a pas masqué son profil (`403 PROFILE_PRIVATE` ; son
auteur garde l'accès à son fil). Un parent d'un autre fil donne `400`. Le fil d'une course
disparaît avec elle (suppression du compte de son auteur). Les notifications de ces fils
(`comment_reply`, `comment_reaction`, `run_comment`) portent `data.thread`
(`level:<id>` ou `run:<record>`), `data.level_id` et, pour une course, `data.record_id` et
`data.level_name`, pour que le jeu ouvre le bon écran.

### 8.9 undecies Tenue en ligne : cadre, titre, combinaison (2.23)

Ce que le joueur porte dans la Collection du jeu le suit en ligne : le **cadre d'avatar** et le
**titre** sur son profil et dans les classements, la **combinaison** sur son profil et sur ses
fantômes (les fantômes d'un classement la reprennent).

`PUT /api/v1/players/me/cosmetics` (authentifié, joueur de la session uniquement, 60 fois par
heure, corps de 1 Kio au plus) — corps `{"suit": "suit_givre", "trail": "trail_comet",
"frame": "frame_gold", "title": "title_explorateur", "victory": "victory_classic"}` : les
cinq identifiants d'éléments, obligatoires (`[a-z0-9_]`, 64 caractères au plus ; aucun autre
champ accepté, jamais d'identifiant de joueur). Le serveur ne fait pas confiance au jeu : chaque
identifiant doit exister **dans sa catégorie** dans sa copie du catalogue
(`src/cosmeticCatalog.json`, générée depuis `data/cosmetics/cosmetics.json` par
`scripts/cosmetics-catalog.mjs`) et être débloqué par les succès **déclarés** du joueur
(§8.9 nonies, table `player_achievements`) : étoiles recomptées par le serveur ≥ seuil,
`"all"` = toutes les étoiles du catalogue des succès du serveur, ou succès requis dans la liste
déclarée ; les éléments par défaut sont toujours permis. Un identifiant inconnu (jeu plus
récent), d'une autre catégorie ou verrouillé est **remplacé par l'élément par défaut** de la
catégorie avant d'être enregistré, jamais conservé tel quel. Réponse : la tenue effective,
`{"cosmetics": {"suit": {id, look, name}, "trail": {…}, "frame": {…}, "title": {…},
"victory": {…}}}` (`look` : ce que le jeu dessine, `name` : nom du catalogue du serveur). Le
jeu envoie sa tenue après ses succès, puis à chaque changement. Erreurs : 400, 401, 413, 429.

Quand `PUT /players/me/achievements` déclare moins de succès, la tenue enregistrée est
revérifiée : ce qui n'est plus débloqué redevient l'élément par défaut (l'inverse n'est jamais
automatique : le jeu renvoie son choix).

`GET /api/v1/players/{id}` ajoute `cosmetics` (même objet ; éléments par défaut pour un joueur
qui n'a rien envoyé ; `null` pour un profil masqué vu par un autre joueur). Chaque entrée de
classement (§8.4, et `ranking` de la course à la vérification) ajoute à `player` :
`frame` (apparence du cadre : `simple`, `bronze`, `silver`, `gold`, `platinum`,
`constellation`, et depuis la 2.24 `emerald`, `orbit`, `nebula`, `laurels`), `title` (`{id, name}` ou `null` sans titre) et `suit` (apparence de la
combinaison : `standard`, `givre`…), **seulement si le profil n'est pas masqué** (sinon aucun
de ces champs : le jeu dessine alors le cadre simple, sans titre). Les titres affichés sont
toujours des noms du catalogue du serveur, jamais un texte du client. La tenue figure dans
l'export des données (`cosmetics`) et disparaît avec le compte (table `player_cosmetics`,
migration 0039).

### 8.9 undecies bis Badges du profil (2.41)

Des succès obtenus, choisis dans la Collection du jeu (Vestiaire › Badges du profil), affichés
sur le profil à côté du nom et du titre. Le nombre de badges dépend des **étoiles** : aucun en
dessous de 100, **2 dès 100 étoiles**, **3 dès 250**. Ces paliers sont écrits une seule fois,
dans `data/achievements/achievements.json` (`badgeSlots`) ; le serveur lit les mêmes dans sa
copie `src/achievementCatalog.json` (générée par `scripts/achievements-catalog.mjs`).

`PUT /api/v1/players/me/badges` (authentifié, joueur de la session uniquement, 60 fois par
heure, corps de 2 Kio au plus) — corps `{"badges": ["voyage_5", "gardiens_first"]}` : des
identifiants de succès (`[a-z0-9_]`, 64 caractères au plus), dans l'ordre d'affichage, chacun
une fois, 3 au plus ; aucun autre champ (jamais d'étoiles ni d'identifiant de joueur). Le
serveur ne fait pas confiance au jeu : chaque identifiant doit être un succès **de son
catalogue** que **ce joueur a déclaré** (§8.9 nonies, `player_achievements`), sinon
`403 BADGE_NOT_UNLOCKED` (`details.badges` : les identifiants refusés) ; leur nombre ne peut
dépasser les places ouvertes par les étoiles **que le serveur recompte lui-même** à partir de
ces succès, sinon `403 BADGE_SLOTS` (`details` : `{slots, stars}`). Une liste vide est toujours
acceptée. Le choix remplace le précédent. Réponse : `{"badges": [{id, stars}], "slots", "stars"}`.
Erreurs : 400 (format, doublon, plus de 3), 401, 403, 413, 429.

`GET /api/v1/players/{id}` ajoute `badges` : `[{id, stars}]` dans l'ordre choisi, **revérifiés
à chaque lecture** (un succès qui n'est plus déclaré ou plus au catalogue est omis ; avec moins
d'étoiles, seuls les premiers dans la limite des places) ; `null` pour un profil masqué vu par
un autre joueur. Le jeu dessine chaque badge avec son propre catalogue (nom, catégorie) ; un
identifiant qu'il ne connaît pas (jeu plus ancien) n'est pas affiché. Les badges ne figurent
pas dans les classements. Ils sont dans l'export des données (`badges`) et disparaissent avec le
compte (table `player_badges`, migration `profile_badges`). Le jeu envoie son choix après ses
succès, puis à chaque changement ; sans compte ou hors ligne, il le garde
(`saves/collection.json`) et l'envoie une fois connecté.

### 8.9 duodecies Défis du jour, de la semaine et du mois (2.32, 2.33)

Contrats complets entre le jeu et le serveur : [DAILY_CHALLENGES.md](DAILY_CHALLENGES.md)
(2.32 : règles, forme canonique, paliers, replay format 4) et
[CHALLENGES_PERIODS.md](CHALLENGES_PERIODS.md) (2.33 : semaine, mois, calendrier, parties
hors délai). Trois sortes de défis, chacune sur une période UTC :

| sorte | identifiant | période |
|---|---|---|
| `daily` | `daily-AAAA-MM-JJ` | le jour, 00:00Z → 00:00Z |
| `weekly` | `weekly-AAAA-Www` (semaine ISO 8601, `W01` à `W52` ou `W53`) | lundi 00:00Z → lundi suivant 00:00Z |
| `monthly` | `monthly-AAAA-MM` | le 1er 00:00Z → le 1er du mois suivant 00:00Z |

**Tirage et choix.** Le serveur garantit que les défis existent de la période en cours jusqu'à
J+7 (jours), S+2 (semaines) et M+1 (mois) — à chaque lecture de `GET /challenges/current`, de
`GET /challenges/daily/current` ou du calendrier, et par la tâche planifiée quotidienne ; des
tirages simultanés ne créent jamais de doublon — mais **aucune route ne montre un défi avant
le début de sa période** ; un défi terminé ne change plus, un défi commencé non plus dès qu'un
joueur y a un résultat (2.60 : conditions SQL de chaque écriture, déclencheurs
`challenges_frozen` et `challenges_have_results` de la migration 0117). Le tirage automatique (`crypto.getRandomValues`) ne prend que des
niveaux de la campagne classés dont la révision active est celle du catalogue
`src/challengeCatalog.json` (généré depuis les fichiers du jeu par
`scripts/challenge-catalog.mjs`), jamais un niveau EXTRÊME ou à vies comptées (dont le boss
rush `noyau_01`) ni un niveau de la communauté : le jour, tout niveau tirable ; la semaine, le
3e, le 4e ou le dernier niveau d'une planète ; le mois, le dernier niveau de chaque planète
(les Épreuves et le niveau final). Il évite les niveaux des défis de la même sorte des 10
jours, 4 semaines ou 2 mois autour. Règles sûres : stations actives, pouvoirs = pouvoirs de
départ du niveau + des pouvoirs de la liste sûre (`dash`, `glide`, `triple_jump`, `shield`,
`wave`, `grapple`) dont l'entrée n'est utilisée par aucun pouvoir du niveau (0 à 2 le jour, 0
ou 1 la semaine, aucun le mois), emplacements = ceux du niveau + un par pouvoir ajouté, cœurs
parmi 1, 2, 3, 5 ou le maximum du joueur (10) le jour, parmi 1, 2 ou 3 la semaine et le mois,
points de passage au hasard ; paliers : Argent = temps de la médaille d'or, Or = temps de la
médaille de platine, sans dégât. L'administrateur choisit un jour, une semaine ou un mois à
venir avec `scripts/challenge-set.mjs` (migration SQL, voir [DEPLOYMENT.md](DEPLOYMENT.md)) ;
un niveau de la communauté doit avoir une version publiée, dont l'empreinte, le titre et
l'auteur viennent de la base. Quand la période d'un défi sur un niveau de la communauté
commence, son auteur reçoit une seule fois la notification `challenge_featured` (« Votre
niveau « … » est le défi du jour ! », « … de la semaine ! », « … du mois ! », groupe
`creators` ; `data` : `title`, `challenge_id`, `challenge_kind`, `period`, `date`,
`level_id`) ; le défi garde `featured_player_id`.

| Méthode | Route | Auth | Réponse |
|---|---|---|---|
| GET | `/challenges/current` | aucune | `{ server_time, daily, weekly, monthly, previous: { daily, weekly, monthly } }` : le défi de la période en cours de chaque sorte et celui de la période précédente (`null` s'il n'y en a pas) |
| GET | `/challenges/calendar?month=AAAA-MM` · cache | facultative | `{ month, first_month, challenges: [Challenge], me }` : défis commencés qui touchent le mois (ses jours, les semaines qui le chevauchent, le mois), par premier jour (mois, puis semaine, puis jour) ; `month` par défaut : le mois en cours ; `first_month` : mois du plus ancien défi ; `me` : `null` en anonyme, sinon `{ "<challenge id>": { best_time_ms, tier, status, late, best_progress } }` pour les défis où le joueur a une partie |
| GET | `/challenges/{challengeId}` · cache | aucune | le défi (`Challenge`) s'il a commencé, sinon `404 CHALLENGE_NOT_FOUND` |
| GET | `/challenges/daily/current` | aucune | (2.32) `{ server_time, challenge, previous }` : défi du jour et celui d'hier (`null` s'il n'y en a pas) |
| GET | `/challenges/daily?before=AAAA-MM-JJ&limit=N` · cache | aucune | (2.32) `{ challenges: [Challenge], next_before }` : défis **du jour** commencés, du plus récent au plus ancien, `before` exclu ; `limit` 1 à 30 (défaut 30) ; `next_before` : la valeur de `before` de la page suivante (`null` à la fin) |
| GET | `/challenges/{challengeId}/leaderboard?scope=world\|country\|friends&country=XX&period=live\|all&board=time\|progress&limit=50&offset=0` · cache | facultative (`friends` : requise) | `{ challenge_id, scope, period, board, country_code, entries, me, total }` ; `board=time` : entrées `{ rank, run_id, player, time_ms, tier, status, has_replay, is_me, late }` ; `board=progress` (2.34) : entrées `{ rank, player, progress, finished, time_ms, status, late, is_me, attempt_id, run_id, has_replay }` |
| POST | `/challenges/{challengeId}/runs` | requise · `challenge_submit` | `201 { duplicate: false, run: { id, status, tier, time_ms, late }, replay: { required, accepted, upload_path } }` ; `200` avec `duplicate: true` pour une tentative déjà reçue |
| PUT | `/challenges/runs/{runId}/replay` | requise, propriétaire · `replay_upload` | `201 { run: { id, status, tier, time_ms, late }, replay: { verification_status }, analysis }` |
| POST | `/challenges/{challengeId}/attempts` | requise · `challenge_submit` | (2.34, « Plus loin ») `201 { duplicate: false, attempt: { attempt_id, status, progress: null, finished: false, late }, replay: { required, upload_path } }` ; `200` avec `duplicate: true` pour un essai déjà reçu |
| PUT | `/challenges/attempts/{attemptId}/replay` | requise (ses essais seulement) · `replay_upload` | (2.34) `201 { attempt: { attempt_id, status, progress, finished: false, late, rank, reasons }, new_best, best_progress }` |
| GET | `/challenges/attempts/{id}/replay` | facultative | (2.34) le replay gardé d'un essai (`application/octet-stream`, en-tête `x-replay-sha256`) : `{id}` = `attempt_id` d'une entrée de `board=progress` (identifiant du serveur) ; public pour un essai `VERIFIED` d'un compte actif (`403 PROFILE_PRIVATE` si son auteur a masqué son profil), son joueur toujours ; `404 ATTEMPT_NOT_FOUND` (inconnu, refusé, en attente), `404 REPLAY_NOT_FOUND` (essai qui n'est plus le meilleur de son joueur : son replay n'est pas gardé) |
| GET | `/challenges/runs/{runId}/replay` | facultative | le replay (`application/octet-stream`, en-tête `x-replay-sha256`) : public pour une partie `VERIFIED` (fantôme du 1er ; `403 PROFILE_PRIVATE` si son auteur a masqué son profil), sinon son propriétaire seulement |
| GET | `/challenges/me?limit=60` | requise | `{ streak: { current, best, last_date }, streaks: { daily, weekly, monthly }, results: [{ challenge_id, kind, period, date, best_time_ms, tier, status, late, best_progress }] }` (du plus récent au plus ancien, `limit` 1 à 366 ; chaque série : `{ current, best, last_period }`) |

`Challenge` : `{ id, kind: "daily" | "weekly" | "monthly", period, date, starts_at, ends_at,
source: "auto" | "admin", level: { source: "campaign" | "community", level_id, level_version,
level_hash, title, author: { id, username } | null }, rules: { hearts, slots, powers,
stations, checkpoints }, rules_hash, tiers: { silver_ms, gold_ms, gold_no_damage },
progress_available }` (`period` : `AAAA-MM-JJ`, `AAAA-Www` ou `AAAA-MM` ; `date` : premier
jour de la période, lu par les jeux 2.32 ; `powers` triés ; `author` : auteur d'un niveau de
la communauté, `null` sinon ; `progress_available` (2.34) : la version du niveau a un tracé,
donc un classement « Plus loin »). `tier` vaut `bronze`, `silver` ou `gold`. La forme canonique des règles et
`rules_hash` sont les mêmes pour les trois sortes (la clé `challenge` porte l'identifiant).

**Classement.** Meilleur temps `VERIFIED` de chaque joueur actif sur le défi ; égalité : la
partie envoyée la première, puis l'identifiant du joueur. `period=live` (défaut, et ce que
lit un jeu 2.32) : les parties reçues pendant le défi (+ 10 min) ; `period=all` (« Depuis ») :
aussi les parties hors délai, marquées `late: true`. `board=time` (défaut) ou `board=progress`
(« Plus loin », ci-dessous).
`scope=country` : pays `country` (par défaut celui du lecteur connecté ; `400` sans pays),
joueurs qui ne masquent pas leur localisation ; `scope=friends` : le lecteur et ses amis.
`player` a la forme des classements (§8.4 : `country_code` masqué selon la confidentialité,
`frame`, `title`, `suit` si le profil est visible). `me` : la meilleure partie du lecteur dans
cette période de classement, `VERIFIED` (avec son rang dans ce classement, `null` s'il n'en
fait pas partie) ou, si elle est meilleure, `PENDING` (`rank: null`) ; `null` en anonyme ou
sans partie. Un défi inconnu ou futur donne `404 CHALLENGE_NOT_FOUND`.

**Soumission.** Corps `{ attempt_id, time_ms, deaths, damage_taken, level_hash, rules_hash,
game_version, has_replay? }` (aucun autre champ : ni joueur, ni palier). Contrôles dans
l'ordre : défi dont la période n'a pas commencé → `409 CHALLENGE_NOT_STARTED` (même s'il
n'existe pas : rien n'est révélé) ; défi inconnu → `404` ; tentative déjà reçue → réponse
idempotente (`409 ATTEMPT_CONFLICT` si les données diffèrent) ; jeu plus ancien que 2.32.0 (ou
que la version minimale du niveau) → `426 GAME_VERSION_UNSUPPORTED` ; `409 LEVEL_MISMATCH`,
`409 RULES_MISMATCH`. Une partie reçue plus de 10 minutes après `ends_at` est **acceptée hors
délai** (`late: true` ; depuis 2.33, `CHALLENGE_ENDED` n'est plus jamais renvoyé, y compris à
un jeu 2.32) et contrôlée comme les autres, tant que la version du niveau du défi existe
(même `level_hash`). Le **palier est calculé par le serveur** : Or si `time_ms <= gold_ms` (et
`damage_taken == 0` si `gold_no_damage`), Argent si `time_ms <= silver_ms`, sinon Bronze.
Statut : temps sous `min_time_ms` de la révision → `REJECTED` (`IMPOSSIBLE_TIME`), au-delà de
sa limite → `REJECTED` (`TIME_OVER_LIMIT`), sinon les règles de décision des records (§7) pour
une catégorie qui exige son replay : `PENDING` jusqu'au replay, `VERIFIED` quand le replay est
valide ; la première place d'un classement d'au moins 5 joueurs (`live` pour une partie à
l'heure, `all` pour une partie hors délai) par un compte de confiance < 2 (ou plus de 20 %
sous la première, confiance < 3) reste `PENDING` ; la tâche quotidienne et `GET
/challenges/me` revoient ces parties quand le classement a changé.

**Replay.** Format 4 obligatoire (section défi, voir DAILY_CHALLENGES.md §3) ; en plus de
l'analyse des records (structure, révision — ou version du niveau de la communauté et ses
limites —, durée, morts, trajectoire plausible, aucun dégât si `damage_taken` vaut 0), la
section doit être **exactement** la forme canonique des règles du défi et la vie
échantillonnée ne doit jamais dépasser `hearts`. Un replay de format 3 ou moins, une autre
section ou trop de vie rendent la partie `REJECTED` (raisons dans `analysis.reasons`, fichier
conservé) ; un fichier illisible donne `422 REPLAY_MALFORMED` sans rien stocker. À l'inverse,
un replay de défi n'est jamais accepté pour un record, pour la fin de niveau ou un temps d'un
niveau de la communauté, ni pour une course à la vérification. « Vérifié » veut dire « replay
contrôlé par le serveur » : la re-simulation complète viendra à l'étape 3.

**Séries.** `streaks.<sorte>` : périodes consécutives (jours UTC, semaines ISO, mois) jusqu'à
la période en cours ou la précédente, où le joueur a au moins une partie **à l'heure** et non
rejetée du défi de cette période ; `best` : la plus longue, `last_period` : la dernière de ces
périodes. `streak` (2.32) = la série des jours, avec `last_date`. Une partie hors délai ne
compte jamais. `results` : par défi joué, les parties à l'heure d'abord — la meilleure partie
à l'heure non rejetée (son temps et son statut ; `tier` : meilleur palier à l'heure, `late:
false`), sinon la meilleure partie hors délai non rejetée (`late: true`), sinon la meilleure
partie rejetée (`tier: null`, `status: "REJECTED"`) ; un défi seulement essayé (« Plus loin »,
aucune partie) : `best_time_ms` et `tier` à `null`, `status` et `late` de son meilleur essai ;
`best_progress` : meilleure progression vérifiée, à l'heure ou hors délai (essais ; 100 pour
une partie finie vérifiée), `null` sans.

**« Plus loin » (2.34).** Sur un défi dont la version du niveau a un tracé
(`level_versions.race`, `community_level_versions.race` : avec les fichiers actuels, `noyau_01`
dans la campagne et les niveaux EXTRÊME de la communauté), un essai qui n'arrive pas au bout
est noté par sa progression, comme la course à la vérification (§8.9 octies) : 40 points pour
le trajet atteint le long du tracé, 60 pour la santé retirée aux boss (2 parts chacun) et les
vagues d'embuscades (1 part chacune) ; une décimale, toujours sous 100 ; une partie finie
vérifiée compte 100. Le jeu déclare l'essai (`POST …/attempts` : `{ attempt_id, time_ms,
level_hash, rules_hash, game_version, claim: { finished: false, bosses: [‰ par boss du
tracé], waves: [vagues par embuscade du tracé] } }`, aucun autre champ, mêmes contrôles et
erreurs que les parties, `409 PROGRESS_UNAVAILABLE` sans tracé, hors délai comme elles ; un
essai plus long que la limite du niveau est refusé d'emblée) puis envoie son replay format 4
(`PUT /challenges/attempts/{attemptId}/replay`, où `attemptId` est son `attempt_id`, cherché
parmi les essais du joueur connecté). Le **serveur calcule le score depuis le replay** avec
l'analyse de la course (`src/race.ts` : version du niveau, tentative, durée, départ,
trajectoire plausible, boss et vagues déclarés identiques au replay) plus la section défi
exacte, la vie au plus `hearts` et la limite de temps du niveau ; sans points de passage
(`checkpoints: false`) une seule vie, avec eux morts et retours permis (le score est le plus
loin atteint). Réponse : `progress` (`null` si refusé, raisons dans `reasons`), `rank` dans
`board=progress` de la période de l'essai, `new_best` (meilleur que tous mes essais et
parties finies vérifiés de ce défi), `best_progress`. Un seul replay d'essai est gardé par
joueur et par défi, celui du meilleur essai vérifié ; tout le monde peut le regarder depuis
le classement (`GET /challenges/attempts/{id}/replay`, mêmes règles que le replay d'une
partie).
`board=progress` : meilleure progression `VERIFIED` de chaque joueur (essais et parties
finies), de la plus haute à la plus basse, à égalité le plus tôt envoyé d'abord ; `me` : ma
meilleure progression vérifiée de cette période (`rank: null` hors du `scope`), sinon `null`.
Chaque entrée dit où regarder son replay : `attempt_id` (identifiant du serveur de l'essai,
pour `GET /challenges/attempts/{id}/replay` ; `null` pour une partie finie) ou `run_id` (la
partie finie, pour `GET /challenges/runs/{runId}/replay` ; `null` pour un essai), et
`has_replay` (le replay est gardé et visible par ce lecteur : `false` pour un profil masqué,
sauf le sien ; `false` aussi pour un essai qui n'est plus le meilleur de son joueur, son
replay n'étant pas gardé).
Les essais ne comptent ni pour les séries ni pour les paliers.

Les parties de défi et les essais « Plus loin » figurent dans l'export des données
(`challenge_runs`, avec `late` ; `challenge_attempts`) et disparaissent avec le compte, leurs
replays compris (tables `challenges`, `challenge_runs` et `challenge_attempts`, migrations
0049, 0054 et 0055).

### 8.9 bis Installateurs du jeu (notification « nouvelle version »)

Publics (aucun compte, rien de personnel n'est envoyé : le jeu ne transmet pas sa version, il
compare lui-même).

| Méthode | Route | Réponse |
|---|---|---|
| GET | `/client/latest?platform=windows` | `{ release: { version, file_name, size, sha256, notes, published_at, download } }` ou `{ release: null }` |
| GET | `/client/download/{version}` | l'installateur (`application/octet-stream`, `content-disposition: attachment`, en-tête `x-content-sha256`) ; 6 téléchargements par heure et par IP (`429` au-delà) |

Le jeu ne propose « Lancer l'installateur » qu'après avoir vérifié que l'empreinte SHA-256 du
fichier reçu est celle annoncée par `/client/latest`. Publication d'une version :
`node scripts/release-migration.mjs <version> <fichier.exe> "<notes>" > migrations/000N_release_<v>.sql`,
envoi du fichier dans le bucket (`wrangler r2 object put <bucket>/releases/CosmicPlatformer-Setup-<v>.exe --file … --remote`)
puis `wrangler d1 migrations apply`.

### 8.10 Administration

Sauvegardes de toutes les données (compte admin, `/admin/backups…`) : copies, téléchargement
chiffré, import en parties, vérification, application pas à pas : voir [BACKUP.md](BACKUP.md)
(routes, réponses, codes d'erreur, limites de débit `backup_*`).

Révisions de la campagne (2.45, compte admin, `/admin/campaign…`) : liste des niveaux et de
leur révision en ligne, historique, publication d'une révision depuis l'éditeur du jeu, retour à
une révision, avec le choix obligatoire « Réinitialiser » / « Garder les records » ; et les
routes publiques `GET /campaign/revisions` (manifeste) et
`GET /campaign/levels/{levelId}/revisions/{version}/file` : voir
[CAMPAIGN_REVISIONS.md](CAMPAIGN_REVISIONS.md).

Joueurs (2.75, compte admin, rôle de la session) : `GET /admin/players` donne la présence et la
dernière activité réelles d'un joueur qui les masque (`presence_hidden: true`) et `profile_hidden`
(profil masqué) ; `GET /admin/players/{playerId}/levels?limit=&offset=` (10 par défaut, 20 au plus) :
ses derniers niveaux joués, du plus récent au plus ancien (`player_playtime` : temps rapporté par
le jeu, `last_played_at`), chacun avec `best` (sa meilleure tentative : `time_ms`, `category`,
`collectibles` / `total_collectibles`, `verified`), `attempts` (tentatives envoyées) et `replay`
(`record_id`, `available` seulement une fois vérifié, `reason` sinon, `locked` : `profile_private`
ou `account_inactive`, le verrou qui arrêterait un autre joueur) ; la première page est écrite au
journal (`admin_player_levels_viewed`). `GET /records/{recordId}/replay` sert un replay vérifié à
un admin malgré un profil masqué ou un compte inactif, avec l'en-tête `x-admin-view:
profile_private | account_inactive` et une entrée `admin_replay_viewed` ; un replay non vérifié
reste indisponible, et les autres joueurs gardent `403 PROFILE_PRIVATE`.

2.75.3 (tout voir des profils, `server/src/adminView.ts`) : un replay **non vérifié** (record ou
replay `PENDING` / `REJECTED`) est servi à un admin, avec `x-admin-replay-status: PENDING |
REJECTED` (jamais compté comme vérifié) ; de même `GET /races/runs/{runId}/replay` (profil
masqué, compte inactif, tentative non vérifiée) ; `GET /races/board` donne `has_replay: true` et
`admin_view: { locks: ["profile_private"] }` à un admin pour le replay d'un profil masqué. Un
replay jamais envoyé ou dont le fichier manque reste `404 REPLAY_NOT_FOUND` pour tous. Dans
`GET /admin/players/{playerId}/levels`, `replay` gagne `watchable` (l'admin peut le regarder :
le replay existe, vérifié ou non) et `status` ; `available` garde son sens de 2.75 (vérifié) ;
`reason` : « replay pas encore vérifié » ou « replay incomplet ». Le journal regroupe les
consultations (`admin_profile_viewed`, `admin_replay_viewed`) : une entrée par admin, joueur et
sorte tant qu'elles se suivent à moins d'une heure ; `GET /admin/log` donne pour chacune `count`,
`last_at`, `locks` (verrous levés) et `items` (niveaux vus). Le détail :
[PRIVACY.md](PRIVACY.md#ce-que-voit-un-administrateur-2753).

Modération des commentaires : `GET /admin/comments` (commentaires masqués ou signalés, avec
les motifs, de tous les fils : `thread` vaut `community` — avec `level_id` — ou
`level:<niveau officiel>` / `run:<record>`) et `POST /admin/comments/{id}/moderate { action:
"restore" | "remove" }`.


Réservé aux opérateurs : actif seulement si le secret `ADMIN_TOKEN` (≥ 32 caractères, posé
avec `wrangler secret put`) existe, sinon ces routes répondent `404 NOT_FOUND`. En-tête
`Authorization: Bearer <ADMIN_TOKEN>`, comparé en temps constant ; un jeton absent ou faux
donne `401 UNAUTHORIZED` et une entrée d'audit `admin_auth_failed`. Le jeton n'est jamais
livré avec le jeu.

#### `GET /admin/records?status=PENDING`
`status` : `PENDING` (défaut), `VERIFIED`, `REJECTED` ; `limit` 1 à 200 (défaut 50),
`offset`. Plus anciens d'abord ; lignes brutes (toutes les colonnes du record, `username`,
`trust_level`, `analysis` = analyse du replay en texte JSON), sans `meta`.
```sh
curl -s "$API/admin/records?status=PENDING" -H "authorization: Bearer $ADMIN_TOKEN"
```
`200` : `{ "records": [ { …, "username": "Rookie", "trust_level": 0, "analysis": "{\"ok\":true,…}" } ] }`.
Erreurs : 400, 401, 404, 429.

#### `POST /admin/records/{recordId}/review`
Corps : `decision` (`VERIFIED` ou `REJECTED`), `reason` (facultatif, ≤ 200). Un refus
enregistre `reason` (ou `MODERATION`) comme `rejection_reason` ; les meilleurs temps, rangs et
niveaux de confiance sont mis à jour.
```sh
curl -s -X POST $API/admin/records/4b9c3c4e-374c-4fa8-a579-383df21bd26e/review \
  -H "authorization: Bearer $ADMIN_TOKEN" -H 'content-type: application/json' -d '{"decision":"VERIFIED","reason":"Replay vérifié"}'
```
`200` : `{ "record_id": "…", "verification_status": "VERIFIED", "ranking": { "previous_rank": null,
"rank": 1, "total": 6, "personal_best_improved": true, "previous_best_ms": null } }`.
Erreurs : 400, 401, 404 (`NOT_FOUND`, `RECORD_NOT_FOUND`), 409 `NOT_PENDING`, 413, 415.

#### `POST /admin/levels` — enregistrer les révisions d'une version du jeu
Corps (≤ 256 Kio) : `levels` (1 à 500 entrées produites par
`CosmicPlatformer --online-manifest`) et `game_version` facultatif (version minimale du jeu
des nouvelles révisions ; défaut `MIN_GAME_VERSION`). Chaque niveau : `id`, `name` (1 à 80),
`planet_id`, `planet_name` (facultatif), `order` (0 à 1 000), `version` (≥ 1),
`content_hash`, `total_collectibles`, `total_enemies`, `time_limit_ms` (≥ 1 000),
`min_time_ms`, `bounds` `[l, h]`, `spawn` `[x, y]`, `exit` `[x, y, l, h]`. Les erreurs de
validation sont nommées `levels[i].champ`. Le script `server/scripts/register-levels.mjs`
fait cet appel.
```sh
ADMIN_TOKEN=… node scripts/register-levels.mjs manifest.json http://127.0.0.1:8787
```
`200` : `{ "levels": [{ "id": "sylvanox_01", "version": 2, "result": "current" }] }` —
`current` (nouvelle révision courante, les précédentes deviennent `legacy`), `unchanged`
(déjà enregistrée, idempotent), `added_as_legacy` (révision plus ancienne que la courante).
Erreurs : 400, 401, 404, 409 `REVISION_CONFLICT` (même numéro, autre contenu ; les niveaux
précédents de la liste sont déjà enregistrés), 413, 415.

### 8.10 bis Créations d'un admin : combinaisons recolorées (2.45), traînées, cadres et effets de victoire (2.59)

Détails : [CREATIONS.md](CREATIONS.md).

| Méthode | Route | Corps / réponse |
|---|---|---|
| GET | `/creations` (public) | `{ creations: [{ id, ref: "srv<kind>:<n>", kind, name, colors, style?, width?, life_ms?, particles?, updated_at }] }` : 2.60, seulement celles qu'un joueur peut rencontrer — gagnées par un joueur, ou données par un défi commencé (archivées ou non) ; jamais la réserve ni une création prévue pour un défi à venir (une combinaison : comme en 2.45, `colors` seulement) |
| GET | `/admin/creations` | `{ creations: [{ id, ref, kind, name, colors, style?, width?, life_ms?, particles?, created_at, updated_at, created_by, archived, archived_at, uses, owners, game_reserve, status, given_by }], kinds, styles: { trail, frame, victory }, max_chars }` — 2.60 : `game_reserve` (réserve du jeu, `created_by` `null`), `status` `reserve` (jamais donnée) / `given` / `archived`, `given_by` le premier défi qui la donne `{ id, kind, period, date, condition, value }` ou `null` |
| POST | `/admin/creations` | `{ kind: "suit", name, colors: { suit, accent, scarf } }`, `{ kind: "trail", name, colors: { start, end }, style, width: 50..200, life_ms: 200..1000, particles }`, `{ kind: "frame", name, colors: { border, glow }, style }`, `{ kind: "victory", name, colors: { main, second }, style }` → `201 { creation }` |
| PUT | `/admin/creations/{id}` | `{ name, colors, (style, width, life_ms, particles selon le type), updated_at? }` → `{ creation, changed }` ; `409 CREATION_ARCHIVED` / `CREATION_CHANGED` |
| POST | `/admin/creations/{id}/archive` | `{ archived: true\|false }` → `{ creation, changed }` |

Rôle admin vérifié à chaque requête (`404` pour un autre compte, `401` sans session) ; `400
VALIDATION_ERROR` (nom, couleurs `#rrggbb`, style hors de la liste de son type, réglages d'une
traînée hors bornes, champ inconnu), `409` pour un nom déjà pris dans le même type.
Récompenses d'un défi (`PUT /admin/challenges/{id}/rewards`) : `kind` `trail`, `frame` ou
`victory` avec `ref` `srvtrail:<n>`, `srvframe:<n>`, `srvvictory:<n>` ; dans les défis et
`GET /players/me/rewards`, la récompense porte alors `creation: { id, kind, name, colors, style,
… }`. 2.60 : chaque récompense de `GET /players/me/rewards` dit aussi par quelle règle de son défi
elle a été gagnée (`condition` `tier` / `top` / `finished`, `value` ; `null` si inconnue). Les classements donnent `player.frame` (`srvframe:<n>` pour un cadre créé) et
`player.trail` (la traînée, dessinée derrière le fantôme).
### 8.10 ter Entités natives modifiées par un admin (2.46)

Détails et routes : [ENEMY_REVISIONS.md](ENEMY_REVISIONS.md). Admin : `GET /admin/enemies`,
`GET /admin/enemies/{id}/revisions`, `POST /admin/enemies/{id}/revisions` (la définition et la
révision suivante de chaque niveau de la campagne qui l'utilise, chacun avec `records:
"reset"|"keep"`, en un seul lot). Publiques : `GET /enemies/revisions`,
`GET /enemies/{id}/revisions/{revision}/file`.

### 8.10 quater Un compte, un ordinateur (2.47)

Détails et routes : [DEVICES.md](DEVICES.md). `device: { id, name }` à l'inscription et à la
connexion (`403 ACCOUNT_BOUND_ELSEWHERE`, `409 DEVICE_BOUND`, `426` sans), `GET/PATCH
/players/me/device`, `GET/PUT /players/me/progress-copy`, `GET/POST /admin/players/{id}/device`.

### 8.10 quinquies Migrer vers un nouveau compte (2.50)

Détails (règles de fusion, fermeture de l'ancien compte, sauvegarde avant une migration admin) :
[DEVICES.md](DEVICES.md#migrer-vers-un-nouveau-compte-250). Le joueur est toujours celui de la
session ; l'ancien compte n'est reconnu que par son mot de passe ET son code.

| Méthode | Route | Corps / réponse |
|---|---|---|
| POST | `/players/me/migration` | ancien ordinateur, ancien compte → `201 { code: "K8R-4TZ-9QW", expires_at }` : 9 caractères sans `0 O 1 I L`, 24 h, une fois ; remplace le code précédent ; stocké haché seulement |
| GET | `/players/me/migration` | `{ active, expires_at \| null }` (jamais le code) |
| DELETE | `/players/me/migration` | annule le code actif → `{ cancelled, active: false }` |
| POST | `/players/me/migrate-from` | nouvel ordinateur, nouveau compte : `{ username, password, code }` de l'ancien compte (casse, espaces et tirets du code ignorés) → `{ migration: { id, mode: "merge", items, close_source: true, source: { id, username, closed }, target: { id, username }, restore, files, backup_id: null }, counts }` ; `403 MIGRATION_INVALID` (tout échec), `409 SAME_ACCOUNT`, `409 LAST_ADMIN`, `429` (5 échecs / 10 min par compte) |
| GET | `/players/me/progress-restore` | `{ restore: { from_username, created_at, postponed_at, copy: { created_at, game_version, size, device_name, summary } } \| null }` |
| GET | `/players/me/progress-restore/file` | la copie telle que l'ancien ordinateur l'a envoyée (`application/json`, hors enveloppe : `{ format, game_version, player_id, created_at, summary, files }`), `x-progress-sha256` ; `404 RESTORE_NOT_FOUND` |
| POST | `/players/me/progress-restore` | `{ action: "done" \| "later" }` → `{ restore }` (`null` après `done` : oubliée, fichier supprimé) ; `404 RESTORE_NOT_FOUND` |
| GET | `/admin/players/{id}/migration?to=<pseudonyme>` | aperçu (admin) : `{ source: { id, username, role, account_status, closed_at, device, counts }, target_found, target \| null, shared: { bests, bests_source_better, playtime_levels, rewards, achievements_new } \| null, copy \| null, problems, items, modes, confirm: "MIGRER" }` |
| POST | `/admin/players/{id}/migrate` | (admin) `{ to_username, mode: "merge" \| "replace", items: [records, achievements, rewards, playtime, levels, progress], close_source?, confirm: "MIGRER" }` → sauvegarde complète, puis `{ migration, backup: { id, created_at, finished_at, rows, files }, counts: { source, target } }` ; `400 CONFIRMATION_REQUIRED`, `404 TARGET_NOT_FOUND`, `409 SAME_ACCOUNT` / `SOURCE_INACTIVE` / `TARGET_INACTIVE` / `OWN_ACCOUNT` / `LAST_ADMIN` / `IMPORT_IN_PROGRESS` / `MIGRATION_CONFLICT`, `500 BACKUP_FAILED` |

`counts` : `{ records: { records, verified, bests, replays, community_records, race_runs,
challenge_runs, challenge_attempts }, achievements: { count, stars, badges }, rewards: { won,
titles, creations, items, look }, playtime: { seconds, levels, boss_stats }, levels: { published,
total, projects } }` (`boss_stats` : le nombre de la dernière copie de la progression, `null`
sans copie). Notifications `security` : `data.reason` `migration_in` (avec `from_username` et,
s'il y a une progression à restaurer, `action: "progress_restore"`), `migration_out`
(`to_username`, `closed`), `migration_refused` (bon mot de passe, mauvais code). Les copies de
la progression (`GET /players/me/progress-copy`, `GET /admin/players/{id}/device`) donnent
aussi `device_name` (2.50, `null` avant).

## Annexe — format du replay (CPRZ / CPRP v1)

Le jeu enregistre une trace compacte du run, pas une vidéo. Conteneur envoyé
(**CPRZ**) : octets 0-3 `CPRZ`, octets 4-7 taille décompressée (u32 little-endian, 72 à
4 194 304), puis un flux zlib. Contenu décompressé (**CPRP**, little-endian) :

| Champ | Type |
|---|---|
| magie `CPRP` | `char[4]` |
| `format_version` (= 1), `sample_hz` (10 à 120) | `u16`, `u16` |
| `frame_count` (> 0), `duration_ms`, `event_count` | `u32` × 3 |
| `level_hash` | `u8[32]` |
| `attempt_id` (octets de l'UUID) | `u8[16]` |
| `level_id` (≤ 64 o), `game_version` (≤ 16 o) | `u8` longueur + UTF-8, deux fois |
| images ×`frame_count` | `f32 x, f32 y, u8 state, u8 flags, u8 health, u8 réservé` (12 o) |
| événements ×`event_count` | `u32 frame, u8 type, u8 arg, u16 value` (8 o) |

La taille décompressée doit correspondre exactement. Types d'événements : 1 saut, 2 saut
aérien, 3 mort, 4 réapparition, 5 ramassage (`arg` : 0 éclat, 1 relique, 2 soin, 3 énergie,
4 cœur, 5 secret), 6 pouvoir, 7 point de contrôle, 8 dégâts (`value` = quantité),
9 ennemi vaincu, 10 téléportation, 11 arrivée.

Analyse serveur (chaque problème est listé dans `analysis.reasons`) : même `level_id`,
`level_hash` et `attempt_id` que le record ; durée à ±500 ms de `time_ms` ; nombre d'images
cohérent avec la durée ; pas plus long que la limite du niveau + 5 s ; événements ordonnés et
dans la trace ; autant de morts et d'objets (éclats + reliques) que le record ; aucun dégât si
`damage_taken` vaut 0 ; événement d'arrivée dans la dernière seconde ; départ à moins de
96 px du point d'apparition ; arrivée à moins de 160 px de la zone de sortie ; positions dans
les limites du niveau ; aucun déplacement impossible entre deux échantillons (hors
mort/réapparition, tolérance élargie autour d'une téléportation). Cela renchérit fortement la
triche sans la rendre impossible : les records de tête des comptes récents restent soumis à
vérification manuelle.

---

[← Retour : Cosmic Online](README.md) · [Accueil (README)](../../README.md)
