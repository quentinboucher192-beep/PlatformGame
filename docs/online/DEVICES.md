# Un compte, un ordinateur (2.47)

Un compte Cosmic Online est **privé à un seul ordinateur**. Code : `server/src/devices.ts`,
`server/migrations/0091_device_binding.sql`, `game/online/DeviceIdentity.*`,
`ui/screens/OnlineComputer.*` (EN LIGNE › PARAMÈTRES › Cet ordinateur, « De retour en ligne »).

## Règles

- Le jeu tire au hasard un **identifiant d'ordinateur** la première fois (32 caractères
  hexadécimaux, aucune empreinte du matériel), gardé dans `<dossier utilisateur>/online/device.json`
  (une réinstallation le garde), avec un nom modifiable (« Mon ordinateur » par défaut). Il est
  envoyé à l'inscription et à chaque connexion. Un jeu plus ancien qui n'en envoie pas reçoit
  `426` : « Mettez le jeu à jour ».
- Le compte est **lié** à l'ordinateur où il est créé ; un compte d'avant la 2.47 est lié au
  premier ordinateur qui s'y connecte avec la 2.47 (les sessions ouvertes ont été fermées par la
  migration pour cela).
- **Depuis un autre ordinateur**, le bon mot de passe est refusé (`403 ACCOUNT_BOUND_ELSEWHERE`) :
  la tentative est notée (pays approximatif seulement) et le propriétaire reçoit une notification
  **Sécurité** (regroupée tant qu'elle n'est pas lue). Après 5 refus en 10 minutes, cet
  ordinateur attend (`429`). Un mauvais mot de passe reste un simple `INVALID_CREDENTIALS`.
- **Un seul compte par ordinateur** : un ordinateur déjà lié refuse l'inscription d'un autre
  compte et la connexion d'un autre compte (`409 DEVICE_BOUND`). Le jeu ne propose sur l'écran de
  connexion que le compte lié, et masque « Créer un compte ».
- Supprimer le compte libère l'ordinateur.
- Les notifications « Sécurité » ne se désactivent pas.

L'identifiant n'est jamais un secret (un fichier copié passerait) : la session, le mot de passe
et la liaison restent ceux du serveur.

## Local et en ligne

Une seule progression sur l'ordinateur, connecté ou non. Se déconnecter (« Se déconnecter (jouer
en local) ») note l'état de la progression ; à la connexion suivante, ce qui a été fait en local
s'affiche dans **« De retour en ligne »** (niveaux finis, temps à envoyer, succès, temps de jeu)
et part au compte : succès déclarés, temps en attente (vérifiés comme d'habitude), tenue, et une
copie de la progression.

## Copies de la progression

Le jeu envoie une copie de ses sauvegardes (`progress`, `records`, `boss_stats`, `achievements`,
`collection`) avec un résumé : après les parties (au plus toutes les 10 minutes), à chaque
connexion, et sur demande. Le serveur en garde 5 par compte, dans le bucket privé
(`progress/<joueur>/…`), supprimées avec le compte, avec le nom de l'ordinateur qui l'a envoyée
(2.50). Elles servent à migrer vers un nouveau compte (2.50, ci-dessous) et aux admins.

## Migrer vers un nouveau compte (2.50)

Code : `server/src/accountMigration.ts`, `server/migrations/0100_account_migrations.sql`
(numéro donné à l'intégration), tests `server/test/accountMigration.test.ts`.

Un compte ne sert que sur un ordinateur : sur un nouvel ordinateur, le joueur crée un **nouveau
compte**, puis y amène les données de l'ancien. Le meilleur des deux côtés est gardé ; l'ancien
compte est fermé.

### Pas à pas (le joueur, deux ordinateurs)

1. **Ancien ordinateur**, connecté à l'ancien compte : « Migrer vers un nouveau compte » envoie
   d'abord une copie de la progression (`PUT /players/me/progress-copy`), puis demande un
   **code de migration** (`POST /players/me/migration`) : 9 caractères affichés `K8R-4TZ-9QW`,
   sans `0`/`O`/`1`/`I`/`L`, valable **24 heures**, **une seule fois**. Un seul code actif par
   compte : en demander un autre remplace le précédent ; « Annuler » le supprime
   (`DELETE`). Le serveur ne garde qu'une empreinte (SHA-256 du compte et du code normalisé) :
   `GET /players/me/migration` dit seulement s'il est actif et jusqu'à quand, jamais le code.
2. **Nouvel ordinateur**, connecté au nouveau compte : le joueur saisit le pseudonyme et le mot
   de passe de l'ancien compte et le code (`POST /players/me/migrate-from`). Majuscules,
   espaces et tirets du code sont ignorés. Le serveur vérifie le mot de passe (la même
   vérification que la connexion : un mauvais mot de passe compte pour le verrouillage du
   compte), le code (empreinte, non expiré, pas utilisé), que les deux comptes sont différents et
   actifs. **Une seule réponse pour tout échec** (`403 MIGRATION_INVALID` : « Pseudonyme, mot de
   passe ou code de migration incorrect, ou code expiré. ») : rien ne dit quel champ était faux
   (la raison n'est que dans les journaux du serveur). Après **5 échecs en 10 minutes**, le
   nouveau compte attend (`429`), et l'ancien compte aussi, quel que soit le compte qui essaie.
   Le bon mot de passe avec un mauvais code prévient le propriétaire de l'ancien compte
   (notification **Sécurité** « Demande de migration de ce compte refusée… »).
3. La migration se fait en **un seul lot D1** (tout ou rien). Le premier ordre « prend » le code
   (`used_at`), le second écrit la migration dans `account_migrations` avec une contrainte CHECK
   qui échoue — et annule tout le lot — si le code n'a pas été pris par cette migration, si un
   des comptes n'est plus actif ou s'il ne resterait aucun admin. Deux demandes simultanées avec
   le même code : une seule passe.
4. La **progression** de l'ancien ordinateur (sa dernière copie) attend sur le nouveau : la
   notification du nouveau compte propose de la restaurer (`data.action = "progress_restore"`),
   le jeu lit `GET /players/me/progress-restore`, télécharge le fichier tel qu'il a été envoyé
   (`GET /players/me/progress-restore/file`), puis répond `done` (restaurée : oubliée, fichier
   supprimé) ou `later` (gardée). Le fichier est copié pour le nouveau compte
   (`progress/<nouveau>/restore/<migration>.json`) : il ne dépend plus des copies de l'ancien, et
   part avec le nouveau compte s'il est supprimé.

### Ce qui est fusionné (« le meilleur des deux côtés »)

| Partie | Tables | Règle |
|---|---|---|
| Records et replays | `records`, `replay_metadata`, `player_bests`, `community_records`, `community_level_players`, `race_runs`, `race_victors`, `community_levels.verified_by`, `challenge_runs`, `challenge_attempts` | Tout passe au nouveau compte. Meilleurs temps (un par révision et catégorie) : le **meilleur temps** gagne (à égalité le plus ancien, puis celui du nouveau compte) ; celui qui perd est retiré, ses records restent dans l'historique. Une ligne dont la clé unique `(joueur, tentative)` existe déjà côté nouveau compte reste sur l'ancien (jamais d'erreur d'unicité, jamais de doublon). Niveau de confiance (gagné par les records vérifiés) : le plus haut des deux. |
| Succès | `player_achievements`, `player_badges` | **Union** des succès (seulement ceux du catalogue du serveur ; étoiles recomptées par le serveur). Le serveur ne connaît pas les dates des succès (elles sont dans la copie de la progression). Badges du profil : ceux du nouveau compte, ou ceux de l'ancien s'il n'en a pas. |
| Récompenses | `player_won_rewards` (titres du serveur, combinaisons d'ADMINISTRATION › Créations, objets), `player_cosmetics` | **Union**, la date la plus ancienne. Tenue portée : celle du nouveau compte ; chaque pièce restée « par défaut » prend celle de l'ancien. Ce qui n'est plus débloqué revient au défaut (comme partout). |
| Temps de jeu | `player_playtime` | **Additionné** niveau par niveau. Les statistiques des boss ne sont que dans le jeu : elles viennent avec la progression à restaurer. |
| Progression | `progress_restores` | La dernière copie de l'ancien ordinateur, à restaurer (voir plus haut). |

Les niveaux publiés et les projets de l'ancien compte ne bougent pas dans la migration du
joueur : un admin peut les déplacer (partie « levels » ci-dessous).

### L'ancien compte fermé

- `account_status = 'suspended'` (la contrainte CHECK de la table ne change pas) et
  `closed_at`, `merged_into` (le compte qui a reçu ses données) : il n'apparaît plus nulle part
  (classements, profils, recherche).
- **Connexion refusée** : le bon mot de passe reçoit `403 ACCOUNT_CLOSED` (« Ce compte a été
  migré vers un autre compte : il est fermé. ») ; un mauvais mot de passe reste
  `INVALID_CREDENTIALS`. Toutes ses sessions sont révoquées.
- **L'ordinateur est libéré** (plus de liaison) : on peut y créer ou y connecter un autre compte.
- Son rôle admin est retiré ; le **dernier admin** ne peut pas être fermé (`409 LAST_ADMIN`).
- Son **pseudonyme reste réservé 30 jours** (`USERNAME_TAKEN`), puis il est libéré par la
  maintenance quotidienne, ou dès qu'une inscription ou un changement de pseudonyme le demande :
  la clé unique `username_normalized` devient `~<identifiant>` (aucun pseudonyme ne peut
  commencer par `~`), le nom affiché reste.
- Les deux comptes reçoivent une notification **Sécurité** : le nouveau « Les données de X ont
  été migrées sur votre compte » (avec l'action de restauration), l'ancien « Les données de ce
  compte ont été migrées sur le compte Y : ce compte est fermé. ».
- Journal (filtre « account ») : `account_migrated` (par le joueur, avec le code) ; l'export RGPD
  des deux comptes contient leurs migrations.

## Administration

ADMINISTRATION › Joueurs › « Ordinateur » : l'ordinateur lié, les tentatives refusées. **Délier**
(la prochaine connexion, depuis n'importe quel ordinateur, le lie de nouveau) ou **Lier à cet
ordinateur** (celui d'une tentative refusée ; les sessions de l'ancien se ferment). Journal
(filtre « account ») et notification « Sécurité » au joueur.

ADMINISTRATION › Joueurs › « Migrer » (2.50, rôle admin vérifié à chaque requête, `404` pour
un autre compte) :

- **Aperçu** (`GET /admin/players/{id}/migration?to=<pseudonyme>`) : pour le compte source et le
  compte cible, les nombres par partie (records, meilleurs temps, replays, courses, défis ;
  succès et étoiles, badges ; récompenses, titres, créations ; temps de jeu et statistiques des
  boss de la dernière copie ; niveaux publiés et projets), ce qu'ils ont en commun (meilleurs
  temps partagés et ceux où la source est meilleure, niveaux du temps de jeu, succès nouveaux),
  la dernière copie de la progression de la source (date, ordinateur), si la cible existe, et
  ce qui refuserait la migration (`problems` : `SOURCE_CLOSED`, `SOURCE_INACTIVE`,
  `TARGET_NOT_FOUND`, `SAME_ACCOUNT`, `TARGET_INACTIVE` ; `LAST_ADMIN` seulement si la source
  est fermée).
- **Migrer** (`POST /admin/players/{id}/migrate`) : `mode` « merge » (fusion, règles ci-dessus)
  ou « replace » (les lignes de la cible des parties choisies sont d'abord supprimées, puis
  celles de la source déplacées), `items` parmi `records`, `achievements`, `rewards`,
  `playtime`, `levels`, `progress`, `close_source` (fermer la source, facultatif), et
  `confirm: "MIGRER"` (sinon `400 CONFIRMATION_REQUIRED`, rien n'est fait).
  - **Une sauvegarde complète d'abord**, avec le mécanisme existant (BACKUP.md) : une copie
    serveur (`kind` « manual », au nom de l'admin) que `createBackup` termine avant de répondre ;
    si elle échoue (`500 BACKUP_FAILED`) ou si un import est en cours (`409
    IMPORT_IN_PROGRESS`), rien n'est migré. Elle compte dans `backup_create` (6 par heure).
  - « replace » : les replays des lignes supprimées de la cible sont d'abord rangés dans cette
    sauvegarde (`backups/<id>/objects/…`, comme un import range ce qu'il remplace : son
    téléchargement les contient encore), puis supprimés (250 au plus par migration ; la réponse
    donne `files: { removed, kept }`). « replace » ne supprime jamais les niveaux publiés ni les
    projets de la cible (les records, commentaires et travaux d'autres joueurs y sont).
  - `levels` : les niveaux de la communauté de la source (`community_levels.author_id`, les défis
    où elle était « créateur à l'honneur ») et ses projets (`projects.owner_id`, sa ligne
    `project_members` de propriétaire ; si la cible en était déjà membre, elle en devient
    propriétaire) passent à la cible.
  - `progress` : la dernière copie de la source devient la progression à restaurer de la cible.
  - Refus : `TARGET_NOT_FOUND`, `SAME_ACCOUNT`, `SOURCE_INACTIVE`, `TARGET_INACTIVE`,
    `OWN_ACCOUNT` (fermer son propre compte), `LAST_ADMIN`, `MIGRATION_CONFLICT` (un compte a
    changé pendant la migration : rien n'est fait). Chaque refus est noté (`admin_refused`).
  - Journal (filtre « account ») : `admin_account_migrated` avec `migration: { id, from, to,
    mode, items, close_source, restore, backup_id, by_admin }` ; filtre « backup » :
    `backup_created` (`reason: "migration"`). Notifications « Sécurité » aux deux joueurs.

## Routes

| Méthode | Route | Corps / réponse |
|---|---|---|
| POST | `/auth/register`, `/auth/login` | + `device: { id, name }` ; `403 ACCOUNT_BOUND_ELSEWHERE`, `409 DEVICE_BOUND`, `426` sans `device` |
| GET | `/players/me/device` | `{ device: { name, bound_at } \| null, events: [{ kind: "refused"\|"bound", country?, device_name?, at }], copy }` |
| PATCH | `/players/me/device` | `{ name }` |
| PUT | `/players/me/progress-copy` | `{ format: "cosmic.progress_copy", game_version, summary: { levels, achievements, stars, play_seconds, bosses }, files }` (≤ 4 Mio) → `201 { copy }` |
| GET | `/players/me/progress-copy` | `{ copy: { id, size, game_version, created_at, summary } \| null }` |
| GET | `/admin/players/{id}/device` | `{ player, device, rebind_allowed, attempts: [{ id, country, created_at }], copies }` |
| POST | `/admin/players/{id}/device` | `{ action: "unbind" }` ou `{ action: "bind", attempt_id }` |
| POST | `/players/me/migration` | (2.50) → `201 { code: "K8R-4TZ-9QW", expires_at }` (remplace le code précédent) |
| GET | `/players/me/migration` | `{ active, expires_at \| null }` (jamais le code) |
| DELETE | `/players/me/migration` | `{ cancelled, active: false }` |
| POST | `/players/me/migrate-from` | `{ username, password, code }` (ancien compte) → `{ migration, counts }` ; `403 MIGRATION_INVALID`, `409 SAME_ACCOUNT` / `LAST_ADMIN`, `429` |
| GET | `/players/me/progress-restore` | `{ restore: { from_username, created_at, postponed_at, copy: { created_at, game_version, size, device_name, summary } } \| null }` |
| GET | `/players/me/progress-restore/file` | la copie telle qu'envoyée (`application/json` : `{ format, game_version, player_id, created_at, summary, files }`, en-tête `x-progress-sha256`) ; `404 RESTORE_NOT_FOUND` |
| POST | `/players/me/progress-restore` | `{ action: "done" \| "later" }` → `{ restore }` (`null` après `done`) |
| GET | `/admin/players/{id}/migration?to=` | `{ source: { …, counts }, target_found, target: { …, counts } \| null, shared, copy: { created_at, device_name, game_version, size, summary } \| null, problems, items, modes, confirm }` |
| POST | `/admin/players/{id}/migrate` | `{ to_username, mode: "merge"\|"replace", items, close_source?, confirm: "MIGRER" }` → `{ migration, backup: { id, created_at, finished_at, rows, files }, counts: { source, target } }` |

`migration` : `{ id, mode, items, close_source, source: { id, username, closed }, target: { id,
username }, restore, files: { removed, kept }, backup_id }`. `counts` (par compte) :
`{ records: { records, verified, bests, replays, community_records, race_runs, challenge_runs,
challenge_attempts }, achievements: { count, stars, badges }, rewards: { won, titles, creations,
items, look }, playtime: { seconds, levels, boss_stats }, levels: { published, total, projects } }`.

Limites (API.md §3) : `migration_code` 10 / h par compte ; `migration_fail_target` et
`migration_fail_source` 5 **échecs** / 10 min par nouveau compte et par ancien compte ;
`progress_restore` 30 / h ; l'admin : `admin_write` et `backup_create`.
