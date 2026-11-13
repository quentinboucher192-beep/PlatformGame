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
`/globe/countries`) ignorent l'en-tête.

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
| `replay_upload` | 20 | 1 min | joueur | `PUT /records/{recordId}/replay` |
| `profile_write` | 20 | 5 min | joueur | `PATCH /players/me`, `PUT /players/me/avatar` (compteur partagé) |
| `friend_request` | 30 | 1 h | joueur | `POST /friends/request` |
| `account_delete` | 5 | 1 h | joueur | `DELETE /players/me` |
| `reads` | 300 | 1 min | IP | toute requête `GET` non servie par le cache |

`POST /auth/logout`, `POST /friends/accept`, `DELETE /friends/{playerId}` et les `POST`
d'administration n'ont pas de limite propre.

## 4. Limites de taille

| Contenu | Limite |
|---|---|
| Corps JSON (par défaut) | 8 Kio (8 192 octets) |
| `POST /records/submit` | 4 Kio |
| `POST /admin/levels` | 256 Kio, 1 à 500 niveaux |
| Avatar `PUT /players/me/avatar` | 64 Kio, PNG carré 32 à 256 px, profondeur 8 bits |
| Replay `PUT /records/{recordId}/replay` | 1 Mio compressé, 72 o à 4 Mio décompressé |

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
| 403 | `PROFILE_PRIVATE` | Profil privé (historique, statistiques, replay d'un autre joueur) |
| 403 | `NOT_OWNER` | Envoi d'un replay pour le record d'un autre joueur |
| 403 | `RANKING_DISABLED` | Classement désactivé pour ce niveau |
| 403 | `FRIEND_REQUESTS_DISABLED` | Le joueur n'accepte pas les demandes d'amis |
| 404 | `NOT_FOUND` | Route inconnue ; routes d'administration désactivées |
| 404 | `PLAYER_NOT_FOUND` | Joueur inconnu, suspendu, ou identifiant mal formé |
| 404 | `LEVEL_NOT_FOUND` | Niveau inconnu du serveur |
| 404 | `LEVEL_VERSION_NOT_FOUND` | Révision de niveau inconnue |
| 404 | `PLANET_NOT_FOUND` | Aucun niveau classé sur cette planète |
| 404 | `RECORD_NOT_FOUND` | Record inconnu ou non visible par ce lecteur |
| 404 | `REPLAY_NOT_FOUND` | Pas de replay (ou replay non vérifié pour un tiers, ou fichier absent) |
| 404 | `AVATAR_NOT_FOUND` | Pas d'avatar personnalisé |
| 404 | `REQUEST_NOT_FOUND` | Aucune demande d'ami de ce joueur |
| 404 | `FRIEND_NOT_FOUND` | Aucun lien d'amitié avec ce joueur |
| 405 | `METHOD_NOT_ALLOWED` | Méthode non autorisée sur cette route |
| 409 | `USERNAME_TAKEN` | Pseudonyme déjà utilisé (sans tenir compte de la casse) |
| 409 | `LEVEL_VERSION_MISMATCH` | `level_version` / `level_hash` inconnus du serveur ; `details.current_version` |
| 409 | `LEVEL_VERSION_RETIRED` | Révision plus classée (legacy) ; `details.current_version` |
| 409 | `ATTEMPT_CONFLICT` | `attempt_id` déjà soumis avec d'autres `time_ms`, `deaths` ou `level_hash` |
| 409 | `DUPLICATE_SUBMISSION` | Deux soumissions identiques simultanées |
| 409 | `REPLAY_EXISTS` | Un replay est déjà associé à la tentative |
| 409 | `RECORD_REJECTED` | Replay envoyé pour un record refusé |
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

`FORBIDDEN`, valeur par défaut de `Errors.forbidden` dans `http.ts`, n'est émis par aucune route.

## 6. Confidentialité

| `location_visibility` | Pays vu par les autres | Classements par pays | Globe |
|---|---|---|---|
| `hidden` (défaut) | non (`country_code: null`, `region_code: null`) | exclu | absent |
| `country` | oui | inclus | absent |
| `globe` | oui | inclus | marqueur au centre du pays |

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
  reste présent dans les classements, la recherche et (s'il l'a choisi) le globe, et ses
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
  `REJECTED` (`REPLAY_INVALID`) ; `SPEEDRUN` sans replay valide → `PENDING` ; nouveau record
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

#### `GET /meta` — métadonnées · cache
Auth : aucune. Version d'API, environnement, version des CGU à accepter, version minimale du
jeu, catégories, modes et tous les niveaux enregistrés (révision courante).
Exemple : `curl -s $API/meta`
```json
{ "ok": true, "data": { "api_version": "1.0.0", "environment": "production", "tos_version": "2026-11",
  "min_game_version": "2.1.0",
  "categories": ["ANY_PERCENT", "HUNDRED_PERCENT", "NO_DEATH", "NO_DAMAGE", "SPEEDRUN"], "modes": ["STANDARD"],
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
401, 403 (`PROFILE_PRIVATE`, `ACCOUNT_SUSPENDED`), 404.

#### `GET /players/{playerId}/records` — historique
Auth : facultative. Paramètres : `limit` (1 à 200, défaut 50), `offset`, `level_id`,
`category`, `status` (`PENDING` / `VERIFIED` / `REJECTED`, propriétaire uniquement). Plus
récents d'abord. Les autres lecteurs ne voient que les records `VERIFIED` en vue publique ;
le propriétaire voit tout, avec `attempt_id`, `rejection_reason`, `verification_notes`.
```sh
curl -s "$API/players/a908a1fb-a574-47c3-b6ed-f550f36cc212/records?level_id=sylvanox_01&limit=10"
```
`200` : `{ "player_id": "…", "records": [ { …record… } ] }` + `meta`. Erreurs : 400, 401,
403 (`PROFILE_PRIVATE`, `ACCOUNT_SUSPENDED`), 404.

Un **record** (vue publique) : `id`, `player_id`, `level_id`, `level_version`, `level_hash`,
`mode`, `category`, `time_ms`, `deaths`, `damage_taken`, `collectibles`,
`total_collectibles`, `score`, `game_version`, `verification_status`, `has_replay`,
`created_at`, `verified_at`.

#### `PATCH /players/me` — modifier son profil
Auth : requise · limite `profile_write`. Tous les champs sont facultatifs : `username`
(mêmes règles qu'à l'inscription), `avatar` (`avatar_01`…`avatar_16` ; supprime l'avatar
personnalisé), `country_code` (`null` efface pays **et** région et force `hidden`),
`region_code` (`null` l'efface ; doit appartenir au pays, nouveau ou actuel),
`location_visibility`, `profile_visibility`, `allow_friend_requests`.
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
    "player": { "id": "…", "username": "NovaRider", "avatar": "avatar_07", "avatar_url": null, "country_code": "FR" },
    "time_ms": 61000, "record_id": "…", "achieved_at": "…", "has_replay": true, "is_me": false }],
  "me": { "entry": null, "pending": { "record_id": "…", "time_ms": 59000, "created_at": "…",
    "note": "En attente du replay.", "status": "PENDING" } } },
  "meta": { "limit": 25, "offset": 0, "total": 1, "next_offset": null }, "request_id": "…" }
```
`rank` = position dans ce classement (filtré), `world_rank` = position mondiale ;
`has_replay` = un replay vérifié existe ; `me` vaut `null` en anonyme, sinon la ligne du
lecteur (même hors de la page) et son meilleur temps en attente. Erreurs communes : 400
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
| `category` | oui | `ANY_PERCENT`, `HUNDRED_PERCENT`, `NO_DEATH`, `NO_DAMAGE`, `SPEEDRUN` |
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

### 8.7 Amis

Les amitiés sont réciproques ; une demande est en attente jusqu'à acceptation. Les comptes
suspendus sont omis.

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

### 8.8 Administration

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
