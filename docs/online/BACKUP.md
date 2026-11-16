# Sauvegarde complète et restauration (ADMINISTRATION › Sauvegardes)

Un compte **admin** peut sauvegarder **toutes les données de tous les joueurs** de Cosmic
Online, télécharger cette sauvegarde dans un fichier **chiffré** avec une phrase secrète de son
choix, et la réimporter pour **remplacer toutes les données** du serveur (après une perte, ou
pour revenir en arrière). Code : `server/src/backups.ts` (routes, copies, import),
`server/src/backupFormat.ts` (format `.cpbk`), migration `server/migrations/XXXX_backups.sql`,
tests `server/test/backups.test.ts`, outil hors serveur `server/scripts/backup-extract.mjs`.

En résumé :

* **Copies côté serveur** : chaque nuit (tâche planifiée de 03:17 UTC), à la demande d'un admin,
  et automatiquement juste avant chaque import. Elles restent dans le bucket R2 privé.
* **Téléchargement** : une copie devient un fichier `cosmic-backup-<date>.cpbk`, chiffré par
  AES-256-GCM avec une clé tirée de la phrase secrète. **Sans la phrase, le fichier est
  illisible** ; elle n'est ni conservée ni écrite dans un journal : la perdre, c'est perdre
  l'usage du fichier.
* **Import** : le fichier est envoyé en parties, **vérifié sans rien changer**, puis appliqué
  pas à pas : copie de sécurité de l'état actuel, maintenance (les écritures des joueurs
  attendent), puis remplacement de toutes les tables en **une seule transaction**. Il faut
  taper la phrase de confirmation `REMPLACER TOUTES LES DONNÉES`.
* Chaque action et chaque refus sont écrits dans le **Journal** (onglet ADMINISTRATION ›
  Journal, type `backup`), jamais la phrase secrète.

## 1. Contenu d'une sauvegarde

### Base D1

Toutes les tables de l'application, découvertes dans `sqlite_master` (pas de liste figée :
une table ajoutée par une migration future est sauvegardée sans rien changer ici), toutes leurs
lignes, et la **version du schéma** = nom de la dernière migration appliquée (`d1_migrations`).
Le manifeste donne pour chaque table ses colonnes, son nombre de lignes, sa clé primaire et ses
clés étrangères.

Ne sont **pas** sauvegardées (et un import ne les touche pas) :

| Table | Raison |
|---|---|
| `player_sessions` | Sessions de connexion. Après une restauration, **chaque joueur se reconnecte** avec son mot de passe ; seul l'admin qui fait l'import garde la sienne s'il est encore admin dans les données restaurées. |
| `rate_limits` | Compteurs temporaires de limitation des requêtes. |
| `server_state` | État du serveur (drapeau de maintenance). |
| `backups`, `backup_imports`, `backup_import_parts` | La mécanique des sauvegardes elle-même (la liste des copies survit à un import). |
| `sqlite_*`, `_cf_*`, `d1_migrations` | Tables internes de SQLite / D1. |

Tout le reste est inclus : joueurs, **empreintes** des mots de passe (`player_credentials`,
PBKDF2 ; jamais un mot de passe en clair), records et meilleurs temps, métadonnées des replays,
amis, conversations et messages, niveaux de la communauté, projets et leur historique, défis,
notifications, succès, tenues, journal d'audit, catalogue des niveaux et des installateurs…

### Fichiers R2

Les fichiers des joueurs vers lesquels pointe la base. Préfixes utilisés aujourd'hui par
`server/src/*.ts` :

| Préfixe | Contenu |
|---|---|
| `avatars/` | avatars personnalisés (`players.avatar_custom_key`) |
| `replays/` | replays des records (`replay_metadata.storage_key`) |
| `community/` | niveaux publiés (`.cplanet`), vignettes, replays de fin et replays des temps de la communauté |
| `projects/` | révisions des projets collaboratifs (JSON) |
| `races/` | replays de la course à la vérification |
| `challenges/` | replays des défis (parties et essais « Plus loin ») |

La copie prend **tous les préfixes de premier niveau du bucket sauf** `releases/`
(installateurs du jeu, republiés par la procédure de version), `backups/` et `imports/` : un
préfixe ajouté plus tard est donc sauvegardé lui aussi. Chaque fichier est listé avec sa taille,
son SHA-256, son type et ses métadonnées.

## 2. Copies côté serveur

Rangement dans R2 (privé, servi uniquement par les routes ci-dessous) :

```
backups/<id>/manifest.json            manifeste (écrit en dernier : sa présence = copie complète)
backups/<id>/d1/<table>/<nnnn>.jsonl  lignes de la table, ~4 Mio par partie
backups/<id>/files/<nnnn>.jsonl       liste des fichiers R2 : { key, size, sha256, etag, uploaded,
                                      content_type, custom_metadata } par ligne
backups/<id>/objects/<clé>            (copie pre_import seulement) fichiers remplacés par l'import,
                                      dans leur version d'avant
```

Chaque ligne d'un `.jsonl` de table est un tableau JSON des valeurs, dans l'ordre des
`columns` du manifeste (`null`, nombre ou texte ; une valeur binaire n'existe dans aucune table
et ferait échouer la copie avec un message clair).

**Les fichiers R2 ne sont pas recopiés** dans une copie serveur : elle en garde la liste, et
c'est le téléchargement qui y met leurs octets. Conséquence : un fichier supprimé après la
copie (compte supprimé, niveau retiré) manque dans son téléchargement ; il est alors listé
dans `end.json` et la vérification de l'import le signale (« Attention : … »). Le SHA-256 d'un
fichier est repris de la copie précédente quand l'objet n'a pas changé (même `etag`, taille,
date), sinon des métadonnées posées par le serveur, sinon calculé en relisant l'objet.

**Rétention** (tâche planifiée) : les **14** dernières copies `auto`, les **10** dernières
`manual`, les copies `pre_import` pendant **30 jours** ; une copie `failed` 7 jours ; une copie
restée `running` plus de 6 heures devient `failed`. La tâche ne fait ni copie ni rétention
pendant un import.

## 3. API (compte admin)

Toutes les routes sont sous `/api/v1/admin/backups`. Le rôle admin de la **session** est
vérifié à chaque requête (`requireAdminRole`) : sans session `401 UNAUTHORIZED`, pour tout
autre compte `404 NOT_FOUND` (« Route inconnue. »), comme si la route n'existait pas. Les
réponses suivent l'enveloppe habituelle `{ ok, data, request_id }` (voir API.md), sauf le
téléchargement.

| Méthode et chemin | Corps | Réponse | Limite |
|---|---|---|---|
| `GET /admin/backups` | — | `200 { backups, retention, maintenance, imports }` | `reads` |
| `POST /admin/backups` | — | `201 { backup }` | `backup_create` 6 / h |
| `POST /admin/backups/{id}/download` | `{ passphrase }` | `200` fichier `.cpbk` (flux) | `backup_download` 10 / h |
| `POST /admin/backups/imports` | `{ size, sha256 }` | `201 { import_id, part_size, parts, max_size }` | `admin_write` 60 / 5 min |
| `PUT /admin/backups/imports/{importId}/parts/{n}` | octets bruts | `200 { received, parts_received, parts, complete }` | `backup_upload` 400 / h |
| `GET /admin/backups/imports/{importId}` | — | `200 { import }` | `reads` |
| `POST /admin/backups/imports/{importId}/check` | `{ passphrase }` | `200 { backup, compatible, problems }` | `backup_check` 20 / h |
| `POST /admin/backups/imports/{importId}/apply` | `{ passphrase, confirm }` | `200 { done: false, … }` puis `{ done: true, summary }` | `backup_apply` 600 / h |
| `DELETE /admin/backups/imports/{importId}` | — | `200 { abandoned: true, import_id }` | `admin_write` |

Les limites sont comptées par compte admin.

**`backup`** (liste, création) :
`{ id, created_at, finished_at, kind: "auto"|"manual"|"pre_import", by: { id, username } | null,
schema, tables, rows, files, bytes, status, error }` — `by` vaut `null` pour la copie de la
tâche planifiée ; `schema` = nom de la dernière migration ; `bytes` = taille du contenu avant
chiffrement (lignes en JSON + fichiers) ; `status` vaut `ready` ou `failed`, et `running`
pendant qu'une copie se fait (une copie interrompue passe `failed`) ; `error` est une phrase en
français pour une copie échouée. Liste : plus récentes d'abord, 200 au plus.
`retention` = `{ auto: 14, manual: 10, pre_import_days: 30 }`.
`maintenance` = `{ active: true, import_id, since, expires_at }` pendant un import, sinon `null`.
`imports` = les imports non terminés (même forme que `GET …/imports/{id}`).

**Créer une copie** (`POST /admin/backups`) : la requête dure le temps de la copie (lignes de
D1, liste des fichiers ; la première copie relit chaque fichier sans SHA-256 connu, les
suivantes non). Erreurs : `409 IMPORT_IN_PROGRESS` pendant un import ; `500 BACKUP_FAILED`
(« La sauvegarde a échoué : … », `details.backup_id`), la copie restant listée `failed`.

**Télécharger** : `passphrase` de **12 à 200 caractères** (sinon `400 INVALID_PASSPHRASE`,
« La phrase secrète doit contenir de 12 à 200 caractères. » ; pas de caractère de contrôle).
Réponse `application/octet-stream`, `Content-Disposition: attachment;
filename="cosmic-backup-AAAA-MM-JJ.cpbk"` (date de la copie), `X-Backup-Id`,
`X-Backup-Plaintext-Bytes` (taille avant chiffrement, pour une barre de progression : le
fichier fait 0,003 % de plus, sans `Content-Length` car il est produit au fil de l'envoi).
Erreurs : `404 BACKUP_NOT_FOUND`, `409 BACKUP_NOT_READY`. Si une lecture échoue pendant
l'envoi, le flux s'arrête : le fichier, incomplet, sera refusé à l'import (dernier bloc absent).

**Annoncer un import** : `size` en octets (1 Gio au plus, sinon `413 PAYLOAD_TOO_LARGE` ;
trop petit : `400 BACKUP_INVALID`), `sha256` en hexadécimal (64 caractères). `part_size` =
**8 Mio** ; envoyer les parties `0` à `parts - 1`, chacune de `part_size` octets sauf la
dernière (`application/octet-stream` ; mauvaise taille : `400 PART_SIZE` ; numéro hors
limites : `400 INVALID_PART`), dans n'importe quel ordre, et renvoyer une partie la remplace.
`received` = octets reçus au total. Trois imports non terminés au plus
(`409 TOO_MANY_IMPORTS`) ; un import sans nouvelle pendant 48 heures expire (parties
effacées). Les parties sont rangées sous `imports/<id>/` dans R2.

**`import`** (`GET …/imports/{id}` et la liste) : `{ id, status, created_at, updated_at, by,
size, sha256, part_size, parts, parts_received, received, backup, compatible, problems, step,
progress, pre_import_backup_id, error }`. `status` : `uploading`, `checked`, `applying`,
`done`, `failed` (rien n'a été remplacé : on peut relancer), `abandoned`, `expired`.

**Vérifier** (`check`) : lit tout le fichier en une requête (empreinte SHA-256 de ce qui a été
reçu, déchiffrement et contrôle de chaque bloc et de chaque entrée), **sans rien changer**.

```json
{ "backup": { "id": "…", "kind": "manual", "created_at": "…", "schema": "0075_….sql",
              "environment": "production", "tables": 52, "rows": 183402, "files": 9120,
              "bytes": 412339012 },
  "compatible": true,
  "problems": [] }
```

`problems` est une liste de phrases en français. Avec `compatible: false`, elles disent
pourquoi l'import est impossible ; les phrases qui commencent par « Attention : » sont des
avertissements qui n'empêchent pas l'import (fichiers qui manquaient déjà, schéma plus ancien,
aucun admin dans la sauvegarde, votre compte pas admin dans la sauvegarde…).

| Erreur | Message |
|---|---|
| `409 IMPORT_INCOMPLETE` | « Envoi incomplet : X partie(s) reçue(s) sur Y. » |
| `400 IMPORT_SHA256_MISMATCH` | « Le fichier reçu ne correspond pas à l'empreinte SHA-256 annoncée : envoyez-le de nouveau. » |
| `400 WRONG_PASSPHRASE` | « Phrase secrète incorrecte : impossible de déchiffrer ce fichier. » |
| `400 BACKUP_CORRUPTED` | « Fichier de sauvegarde altéré ou tronqué (bloc N) : il ne peut pas être importé. » |
| `400 BACKUP_INVALID` | « Ce fichier n'est pas une sauvegarde Cosmic Online valide : … » |
| `409 IMPORT_BUSY` / `IMPORT_CLOSED` | import en cours d'application / terminé ou abandonné |

Une phrase secrète fausse se reconnaît dès l'en-tête (valeur de contrôle) ; à la première
vérification d'un import, le serveur lit quand même tout le fichier pour distinguer un envoi
abîmé (`IMPORT_SHA256_MISMATCH`) d'une mauvaise phrase.

**Appliquer** : corps `{ "passphrase": "…", "confirm": "REMPLACER TOUTES LES DONNÉES" }` (casse
et accent exacts, espaces autour ignorés ; sinon `400 CONFIRMATION_REQUIRED`, « Pour
confirmer, tapez exactement : REMPLACER TOUTES LES DONNÉES »). Il faut une vérification
réussie et compatible (`409 IMPORT_NOT_CHECKED`, `409 IMPORT_INCOMPATIBLE`). **Rappeler la
route tant que `done` vaut `false`** (mêmes corps à chaque fois) :

```json
{ "done": false, "step": "files", "progress": 0.41, "message": "Restauration des fichiers…",
  "pre_import_backup_id": "…" }
```

`step` est l'étape **à venir** : `pre_import` (« Copie de sécurité de l'état actuel… »),
`tables` (« Restauration des tables… »), `files` (« Restauration des fichiers… »), `swap`
(« Remplacement des données… »), `finalize` (« Remplacement des fichiers modifiés… »),
`cleanup` (« Finalisation… »). `progress` va de 0 à 1. Fin :

```json
{ "done": true, "step": "done", "progress": 1,
  "summary": { "pre_import_backup_id": "…", "backup": { … }, "tables": 52, "tables_emptied": 0,
               "rows": 183402, "orphans_removed": 0, "duplicates_removed": 0,
               "files_written": 120, "files_unchanged": 9000, "files_replaced": 0,
               "files_missing": 0, "sessions_reset": true, "session_kept": true,
               "finished_at": "…",
               "message": "Restauration terminée. Les joueurs doivent se reconnecter." } }
```

`session_kept: false` : l'admin qui a fait l'import n'est plus admin (ou n'existe plus) dans
les données restaurées ; il est déconnecté comme les autres. Autres réponses :
`409 IMPORT_BUSY` (« Une étape de cet import est déjà en cours : réessayez dans un instant. »,
`Retry-After: 5`), `409 IMPORT_IN_PROGRESS` (un autre import a la maintenance),
`500 IMPORT_FAILED` (voir §5 ; `details = { step, pre_import_backup_id, data_replaced }`).

**Abandonner** (`DELETE`) : efface les parties ; possible tant que les données n'ont pas été
remplacées (`409 IMPORT_ALREADY_APPLIED` sinon) ; lève la maintenance si l'import l'avait.

**Journal** (`GET /admin/log?kind=backup`) : `backup_created` (copie manuelle ou `pre_import`),
`backup_failed`, `backup_downloaded` (qui, quelle copie), `backup_import_created`,
`backup_import_checked`, `backup_import_started`, `backup_import_finished`,
`backup_import_failed`, `backup_import_abandoned`, `backup_refused` (aussi dans
`kind=refused` ; `reason` = code de l'erreur). Les entrées ont en plus `backup_id`,
`backup_kind` et `import_id`. Les entrées `backup_*` du Journal survivent à un import.

## 4. Le fichier `.cpbk`

Entiers en petit-boutiste (little-endian).

### En-tête (48 octets)

| Octets | Contenu |
|---|---|
| 0–3 | `CPBK` (ASCII) |
| 4–5 | version du format, `1` (u16) |
| 6 | dérivation de clé, `1` = PBKDF2-HMAC-SHA256 |
| 7 | chiffrement, `1` = AES-256-GCM |
| 8–11 | nombre d'itérations PBKDF2 (u32) : **300 000** pour les fichiers de ce serveur ; un import accepte de 210 000 à 2 000 000 |
| 12–27 | sel PBKDF2, 16 octets aléatoires |
| 28–31 | `chunk_size`, octets en clair par bloc (u32) : 1 048 576 |
| 32–47 | valeur de contrôle de la clé (16 octets) |

### Clés

```
maître   = PBKDF2-HMAC-SHA256(phrase, sel, itérations, 32 octets)
           phrase = la phrase secrète en Unicode NFC, encodée en UTF-8
clé      = HKDF-SHA256(maître, sel vide, info = "cosmic-backup v1 encryption", 32 octets)
contrôle = HKDF-SHA256(maître, sel vide, info = "cosmic-backup v1 key check", 16 octets)
```

Le Worker calcule PBKDF2 au-delà de 100 000 itérations (limite de Web Crypto dans Workers)
avec une HMAC-SHA256 par itération : c'est le même PBKDF2 standard (testé contre Python
`hashlib.pbkdf2_hmac`), il coûte environ 1 s de calcul par requête qui déchiffre.

### Blocs

Après l'en-tête, une suite d'enregistrements ; le bloc `i` contient `chunk_size` octets en
clair, sauf le dernier (1 à `chunk_size` octets) :

```
enregistrement i = IV (12 octets aléatoires, nouveaux pour chaque bloc)
                 | AES-256-GCM(clé, IV, bloc i, données associées)   (texte chiffré + étiquette de 16 octets)
données associées = en-tête (48 octets) | i (u64) | 1 si c'est le dernier bloc, sinon 0
```

Un enregistrement complet fait `chunk_size + 28` octets ; le nombre de blocs se déduit de la
taille du fichier. Comme l'index et le drapeau « dernier » sont authentifiés, un bloc modifié,
déplacé, retiré ou un fichier coupé (même juste après un bloc) échoue au déchiffrement.

### Contenu en clair : une suite d'entrées

```
type (u8) | longueur du nom (u16) | nom (UTF-8) | longueur des méta (u32) | méta (JSON UTF-8)
          | taille des données (u64) | données | SHA-256 des données (32 octets)
```

Dans cet ordre :

| Type | Nom | Données |
|---|---|---|
| 1 | `manifest.json` | le manifeste de la copie (JSON) |
| 2 | `d1/<table>/<nnnn>.jsonl` | lignes d'une table (une ligne JSON = un tableau de valeurs dans l'ordre de `columns`) |
| 3 | `files/<nnnn>.jsonl` | liste des fichiers R2 de la copie |
| 4 | la clé R2 (ex. `replays/<joueur>/<tentative>.cprz`) | le fichier ; méta = `{ "content_type", "custom_metadata" }` |
| 5 | `end.json` | `{ format: "cosmic-backup-end", backup_id, files_included, files_missing_count, files_missing, files_changed_count, files_changed, completed_at }` |

Le manifeste :

```json
{ "format": "cosmic-backup", "format_version": 1, "backup_id": "…", "kind": "manual",
  "created_at": "…", "finished_at": "…", "created_by": { "id": "…", "username": "…" },
  "environment": "production", "schema": "0075_….sql", "migrations": ["0001_initial_schema.sql", "…"],
  "tables": [ { "name": "message_reports", "columns": ["…"], "rows": 3, "bytes": 412,
                "parts": ["d1/message_reports/0000.jsonl"], "primary_key": ["…"],
                "foreign_keys": [ { "parent": "players", "from": ["reporter_id"], "to": ["id"],
                                    "on_delete": "CASCADE" } ] }, "…" ],
  "excluded_tables": [ { "name": "player_sessions", "reason": "…" }, "…" ],
  "files": { "count": 9120, "bytes": 401230112, "lists": ["files/0000.jsonl"],
             "excluded_prefixes": ["releases/", "backups/", "imports/"] },
  "totals": { "tables": 52, "rows": 183402, "files": 9120, "bytes": 412339012 },
  "notes": ["Les sessions de connexion (player_sessions) ne sont pas sauvegardées : …", "…"] }
```

`tables` est dans l'ordre de la copie : **les tables enfants avant leurs parents** (restaurer
dans l'ordre inverse). `files_changed` liste les fichiers qui ont changé entre la copie et le
téléchargement (le fichier contient leur version au moment du téléchargement).

## 5. Ce que fait un import

1. **Vérification** (`check`) : rien n'est modifié. Compatibilité avec le schéma du serveur :
   * même schéma : rien à dire ;
   * schéma **plus ancien** (le nom est dans les migrations du serveur) : accepté avec un
     avertissement ; les colonnes ajoutées depuis prennent leur **valeur par défaut**, les
     colonnes disparues sont ignorées, une table créée depuis est **vidée** ; les données que les
     migrations suivantes avaient ajoutées ou transformées (par exemple les lignes
     d'installateurs ou de niveaux qu'elles insèrent) ne sont **pas** rejouées : à vérifier après
     coup, et à réappliquer si besoin (leurs `INSERT … ON CONFLICT DO NOTHING` peuvent être
     relancés avec `wrangler d1 execute --file`) ;
   * schéma **plus récent** que le serveur, ou inconnu, ou **table inconnue**, ou colonne
     obligatoire sans valeur par défaut absente : refusé (« La sauvegarde vient d'un serveur
     plus récent (schéma …) que celui-ci (schéma …) : mettez d'abord ce serveur à jour. »).
2. **Application**, une requête après l'autre :
   1. **Maintenance** : un drapeau dans `server_state`. Le routeur répond alors
      `503 MAINTENANCE` (« Maintenance en cours : réessayez dans quelques minutes. »,
      `Retry-After: 120`) à **toute écriture** (`POST`, `PUT`, `PATCH`, `DELETE`) qui n'est pas
      une route `/api/v1/admin/…`, sauf `POST /auth/login`, `/auth/refresh` et `/auth/logout`
      (un admin doit pouvoir se reconnecter pour finir ou contrôler une restauration). Les
      lectures continuent. Le routeur relit le drapeau au plus toutes les 2 s par instance ; la
      copie de l'étape suivante attend 5 s après la pose du drapeau (requêtes en vol, autres
      instances). Le drapeau **expire 15 minutes** après le dernier appel d'`apply` : un import
      oublié ne bloque pas le service ; s'il avait expiré, le prochain appel recommence tout
      (nouvelle copie de sécurité), car des joueurs ont pu écrire entre-temps.
   2. **Copie de sécurité** `pre_import` de l'état actuel (son `id` est dans chaque réponse).
   3. **Tables** : les lignes du fichier vont dans des tables temporaires
      (`backup_stage__<table>`), avec les colonnes que le serveur et le fichier ont en commun ;
      la position dans le fichier est enregistrée dans la même transaction que chaque lot, donc
      une requête interrompue reprend sans doublon.
   4. **Fichiers** : un fichier absent du bucket est écrit ; identique, il n'est pas touché ; s'il
      existe avec un autre contenu (avatar changé depuis), il est mis de côté
      (`imports/<id>/swap/`) jusqu'au remplacement de la base. Les fichiers du bucket qui ne
      sont pas dans la sauvegarde **ne sont pas effacés** (rien ne pointe plus vers eux ; la
      copie `pre_import` en a besoin pour rester restaurable).
   5. **Remplacement** : les lignes orphelines (parent absent : une copie se fait pendant que
      les joueurs jouent) sont retirées, ou leur lien mis à `NULL` pour une clé
      `ON DELETE SET NULL` ; une ligne copiée deux fois ne garde que sa dernière version. Puis,
      **en une seule transaction D1** (`batch`, `PRAGMA defer_foreign_keys = ON`) : toutes les
      tables sont vidées (enfants d'abord) puis remplies (parents d'abord), toutes les sessions
      supprimées sauf celle de l'admin qui applique (s'il est encore admin actif dans les
      données restaurées), et les entrées `backup_*` du Journal absentes du fichier recopiées.
      Si cette transaction échoue, **rien n'a changé**.
   6. **Fichiers modifiés** : la version actuelle est rangée dans la copie `pre_import`
      (`backups/<id>/objects/`, son téléchargement la reprend), puis remplacée.
   7. **Fin** : tables temporaires supprimées, maintenance levée, parties de l'import effacées,
      `backup_import_finished` au Journal.
3. **Échec d'une étape** : avant le remplacement, la maintenance est levée, l'import passe
   `failed`, **rien n'a été remplacé** et « Appliquer » peut être relancé (nouvelle copie de
   sécurité). Après le remplacement, la base est restaurée : la maintenance est levée et
   « Appliquer » termine les dernières étapes. Dans les deux cas l'erreur
   `500 IMPORT_FAILED` donne la copie `pre_import` pour revenir à l'état d'avant (la
   télécharger puis l'importer).
4. **Tâche planifiée** : un import resté `applying` sans nouvelle depuis 15 minutes est soit
   annulé (avant le remplacement : rien n'a changé, `failed`), soit terminé (après).

Ce qu'un import ne contrôle pas : les salles d'édition en direct (Durable Objects
`ProjectRoom`) peuvent encore écrire une révision de projet à leur réveil pendant la
maintenance ; une révision écrite avant le remplacement est remplacée comme le reste.

## 6. Limites et coût

| Élément | Limite |
|---|---|
| Fichier importé | 1 Gio (parties de 8 Mio : 128 requêtes) ; au-delà, procédure manuelle (§8) |
| Phrase secrète | 12 à 200 caractères |
| Entrée du fichier | 64 Mio (les fichiers du jeu font 16 Mio au plus) ; une ligne de table : 2 Mo (limite de D1) |
| Copie (`POST /admin/backups`, tâche, `pre_import`) | une seule requête : ~1 requête D1 par 5 000 lignes et par table, 1 écriture R2 par 4 Mio, la liste du bucket, et la relecture des fichiers dont le SHA-256 n'est pas connu (la première fois seulement) |
| Téléchargement | une seule réponse en flux : une lecture R2 par fichier (4 en avance), mémoire bornée (~1 Mio + un fichier) |
| Vérification | une seule requête qui lit tout le fichier (≈ 1 s de PBKDF2, puis SHA-256, AES-GCM et lecture JSON de toutes les lignes) |
| Chaque appel d'`apply` | ≈ 1 s de PBKDF2, puis au plus ~20 s de travail, 800 opérations R2 et 150 lots D1 ; une partie de table = des lots de 4 requêtes de 900 Kio |
| Remplacement | une transaction D1 : quelques secondes pour quelques centaines de milliers de lignes ; une base trop grosse pour une transaction (limites de durée de D1) fait échouer l'import **sans rien changer** : procédure manuelle |

Nombre d'appels d'`apply` : 1 (copie de sécurité) + ceux des tables et des fichiers + 1
(remplacement et fin). Chaque appel avance tant qu'il reste du temps (~20 s) : pour les tables,
au plus 150 lots d'environ 3,6 Mio ; pour les fichiers, environ 400 fichiers (deux opérations R2
chacun, 16 en parallèle). Ordre de grandeur : une base de 100 Mo et 10 000 replays → une
trentaine d'appels. Chaque réponse arrive en moins de ~25 s ; le jeu n'a qu'à rappeler la route.

Les copies côté serveur sont **en clair** dans le bucket R2 privé, comme la base D1 elle-même
(mêmes accès). Seul le fichier téléchargé est chiffré : gardez-le dans un endroit sûr, avec la
phrase secrète ailleurs.

## 7. Données personnelles

Une sauvegarde contient les données personnelles de tous les joueurs (voir PRIVACY.md).
Conservation : copies automatiques 14 jours, copies `pre_import` 30 jours, copies manuelles
jusqu'à ce que 10 plus récentes existent ; un compte supprimé disparaît donc des copies au
plus tard à ces échéances. Un fichier `.cpbk` téléchargé sort du serveur : il est sous la
responsabilité de l'admin qui l'a téléchargé (le Journal garde qui et quand). Restaurer une
sauvegarde rétablit l'état de sa date, y compris des comptes supprimés depuis : à éviter sauf
nécessité réelle.

## 8. Restauration manuelle (dernier recours)

Quand le serveur ne peut pas faire l'import (Worker hors service, fichier de plus de 1 Gio,
base trop grosse pour une transaction), sur un ordinateur avec Node.js 20 ou plus :

```bash
cd server
node scripts/backup-extract.mjs cosmic-backup-2026-11-16.cpbk restauration/
# phrase secrète demandée sans écho (ou variable CPBK_PASSPHRASE)
```

Le script vérifie chaque bloc et chaque empreinte et écrit : `manifest.json`, `end.json`,
`d1/<table>/*.jsonl`, `files/*.jsonl`, `r2/<clé>` (fichiers ; une clé inhabituelle va dans
`r2-autres/`, voir `r2-index.jsonl`), `restore-d1.sql` (toutes les tables vidées puis
remplies, parents d'abord, orphelins écartés, instructions de moins de 100 Ko) et
`restore-r2.sh` / `restore-r2.ps1` (un `wrangler r2 object put` par fichier).

Puis, depuis `server/` (exemple de la production ; staging : `--env staging`, base
`cosmic-online-staging`, bucket `cosmic-online-staging-files`) :

```bash
# 1. Le schéma : les migrations jusqu'à celle de la sauvegarde (manifest.json, "schema").
npx wrangler d1 migrations apply cosmic-online-prod --remote
# 2. Les données (toutes les tables listées sont remplacées).
npx wrangler d1 execute cosmic-online-prod --remote --file restauration/restore-d1.sql
# 3. Les fichiers.
BUCKET=cosmic-online-prod-files bash restauration/restore-r2.sh
#    (PowerShell : $env:BUCKET="cosmic-online-prod-files"; .\restauration\restore-r2.ps1)
```

Les métadonnées personnalisées des objets R2 ne sont pas reposées par cette voie (le jeu n'en
a pas besoin : la base garde les empreintes des replays). Les joueurs se reconnectent ; nommer
un admin si besoin :
`npx wrangler d1 execute cosmic-online-prod --remote --command "UPDATE players SET role = 'admin' WHERE username_normalized = 'novarider'"`.

**Time Travel de D1** : indépendamment de ces sauvegardes, D1 garde l'historique de la base
**30 jours** (offre Workers payante ; 7 jours en offre gratuite) et peut la ramener à un
instant donné (sans les fichiers R2, qui ne sont pas concernés) :

```bash
npx wrangler d1 time-travel info cosmic-online-prod
npx wrangler d1 time-travel restore cosmic-online-prod --timestamp=2026-11-16T03:00:00Z
```

C'est le moyen le plus rapide de revenir en arrière après une erreur récente ; les sauvegardes
`.cpbk` couvrent ce que Time Travel ne couvre pas (fichiers R2, plus de 30 jours, perte du
compte Cloudflare ou de la base).

## 9. Tests

`server/test/backups.test.ts` (dans `npx vitest run`) : routes refusées (401 sans session, 404
pour un joueur) ; copie et liste (sessions, compteurs et installateurs exclus, SHA-256 des
fichiers, aucun mot de passe) ; aller-retour complet (joueurs, records et replays, message,
avatar, gros fichier sur plusieurs blocs → copie → téléchargement → changements → envoi en
parties → vérification → application jusqu'au bout) avec données identiques à la copie,
sessions non restaurées, maintenance (`503` pour une écriture d'un joueur, lectures
possibles) puis levée, copie `pre_import` et fichier remplacé gardé ; phrase fausse, SHA-256
faux, bloc modifié, blocs inversés, fichier coupé après un bloc ou au milieu ; phrase de
confirmation fausse ; abandon ; schéma plus récent et table inconnue refusés ; schéma plus
ancien restauré (colonne absente → valeur par défaut) ; orphelins et doublons d'une copie faite
pendant que les joueurs écrivent ; rétention ; tâche planifiée (et pas de copie pendant un
import) ; PBKDF2 comparé à la fonction standard ; lecture des entrées coupées n'importe où.
Le Journal ne contient jamais la phrase secrète.
