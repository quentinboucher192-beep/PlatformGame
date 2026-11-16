# Révisions de la campagne publiées par un admin (2.45)

ADMINISTRATION › **Campagne** : un compte admin ouvre un niveau de la campagne dans l'éditeur du
jeu, le modifie et le publie comme **nouvelle révision** de ce niveau, sans mise à jour du jeu.
Les jeux téléchargent la révision en ligne et la jouent à la place du fichier livré.

Code : `server/src/campaignRevisions.ts` (routes), `server/migrations/XXXX_campaign_revisions.sql`
(tables), `game/online/CampaignRevisions.*` (cache du jeu), `game/levels/CampaignPipeline.*`
(étapes de la campagne), `ui/screens/AdminCampaign.*` (onglet), `editors/level_editor/LevelEditorCampaign.cpp`
(publication depuis l'éditeur).

## 1. Une révision = un nouveau contenu, de nouveaux classements

Comme pour une mise à jour du jeu (`scripts/levels-migration.mjs`, manifeste
`--online-manifest`), chaque publication crée une ligne `level_versions` :

- numéro : `MAX(version) + 1` du niveau, **calculé par le serveur** ; le jeu l'écrit dans
  `metadata.revision` du fichier, qui fait partie de l'empreinte ;
- empreinte (`content_hash`) : l'empreinte classée du jeu (`fingerprintRankedLevel` : le niveau,
  ses ennemis, ses pouvoirs, le joueur, `kRankingGeneration`), calculée sur **les octets exacts**
  envoyés ; elle change donc à chaque publication ;
- la nouvelle révision devient `active` et courante (`levels.current_version / content_hash`),
  les autres passent `legacy` : leurs classements restent lisibles (`?level_hash=` /
  `?level_version=`), jamais mélangés.

Aucune règle de simulation ne change : `kSimulationVersion` et `kRankingGeneration` restent
les mêmes (une révision est un fichier de niveau comme ceux du jeu).

## 2. Le choix obligatoire sur les records

À chaque publication **et** à chaque « Revenir à cette révision », l'admin choisit :

| Choix | Effet |
|---|---|
| **Réinitialiser les records** (`records: "reset"`) | la révision a ses propres classements ; ceux de la révision remplacée restent archivés sous son empreinte. Un retour à une révision retrouve les classements qu'elle avait. |
| **Garder les records** (`records: "keep"`) | réservé aux changements **sans effet sur le jeu** (décor…) : la révision est déclarée compatible et les classements de la révision remplacée y sont reportés (règle ci-dessous). |

### Règle du report (« Garder »)

Soit `A` l'empreinte remplacée, `B` la nouvelle (ou celle à laquelle on revient) :

1. **Meilleurs temps** : chaque ligne `player_bests` de `A` est copiée sous `B`
   (`INSERT … SELECT … ON CONFLICT DO UPDATE … WHERE excluded.time_ms < player_bests.time_ms`) :
   le meilleur temps par joueur et par catégorie gagne quand `B` en a déjà (retour à une
   révision). La copie garde `record_id` (le record d'origine, joué sur `A`, avec son replay) et
   `achieved_at`. Les lignes de `A` restent (archives). Tout ce qui lit `player_bests` par
   empreinte suit donc : classement du niveau (tout temps), classement global, par planète,
   par pays, globe (tout temps), profil, rang et record du monde (`records.ts`).
2. **Classements de la semaine** : ils lisent les records VÉRIFIÉS ; la table
   `campaign_board_links (to_hash = B, from_hash = A)` — tenue transitive : `B` reçoit aussi les
   liens de `A` — fait lire à `B` les records de `A` (`LINKED_HASHES` dans `leaderboards.ts`, et
   `globe.ts`), ainsi que la performance en attente du joueur.
3. **Vérifications tardives** : un record de `A` encore `PENDING` au moment de la publication,
   vérifié plus tard (replay, modération, contrôle quotidien), améliore aussi les meilleurs temps
   de chaque `B` lié (`updateBest`).
4. Les records **déjà enregistrés** gardent leur `level_hash` d'origine (le replay est vérifié
   contre lui). Une course envoyée *après* la publication avec l'empreinte `A` est refusée
   (`LEVEL_VERSION_RETIRED`), comme pour toute révision : le jeu télécharge la révision en ligne
   au démarrage.
5. La **course à la vérification** d'un niveau EXTRÊME suit chaque révision (son tracé est
   envoyé avec elle, `level_versions.race`) : ses classements de course ne sont pas reportés,
   comme pour une révision d'une mise à jour du jeu.
6. Les **défis** gardent leur révision (`challenges.level_hash` ne change jamais) : leurs
   classements sont ceux du défi, ni réinitialisés ni reportés. Le serveur indique dans le
   manifeste (`pinned`) les révisions encore jouées par un défi pas terminé ; le jeu les garde
   et lance le défi sur le fichier de cette révision (livré ou téléchargé).

Les deux choix sont inscrits au Journal (`admin_campaign_published`, `admin_campaign_reverted`,
filtre « Campagne ») : qui, quand, niveau, révision, révision remplacée, choix, meilleurs temps
reportés. Un refus (`BASE_CHANGED`) est inscrit comme `admin_refused`.

## 3. Avant la publication : les étapes de la campagne

Le jeu applique au niveau modifié les étapes de `tools/levelgen` (CLAUDE.md), sur ce seul
niveau (`prepareCampaignRevision`, hors du fil principal) :

1. **sceaux** (`--seal-levels`) : `analyzeLevel` puis `suggestGates` ; portes ajoutées comme
   `levelkit.apply_seals` (solide de la matière la plus courante, `props.seal`, ids `seal_<n>`),
   celles déjà là sont gardées ;
2. **stations de pouvoirs** (`--plan-stations`) : les stations d'avant (`props.station`,
   `station_sign_*`) sont retirées puis `planPowerStations` replace tablettes et panneaux sur le
   parcours modifié, comme `levelkit.apply_power_slots` ;
3. **note** (`--rate-levels`) : `rateDifficulty`, envoyée avec la révision et reprise par les
   jeux à la place de `data/levels/ratings.json`.

La boîte de dialogue montre ce que chaque étape a fait. Les niveaux livrés restent générés par
`tools/levelgen` : une révision publiée en ligne n'est jamais écrite dans `data/levels`.

## 4. Routes

Admin (rôle vérifié à chaque requête, `requireAdminRole` ; pour tout autre compte : `404
NOT_FOUND`, sans session : `401`) — limite `admin_write` pour les écritures :

| Méthode | Route | Corps / réponse |
|---|---|---|
| GET | `/admin/campaign` | `{ planets: [{ planet_id, planet_name, levels: [{ level_id, name, order, current: { version, content_hash, min_game_version, created_at, source: "release"\|"admin", has_file, published_by, published_at, change }, next_version, revisions }] }] }` |
| GET | `/admin/campaign/{levelId}/revisions` | `{ level, current_version, content_hash, next_version, revisions: [{ version, content_hash, compatibility_status, is_current, source, has_file, published_by, published_at, bests }], changes: […] }` |
| POST | `/admin/campaign/{levelId}/revisions` | `{ level_file, base_version, records, game_version, manifest, rating? }` → `201 { level_id, version, content_hash, records, carried_bests, file: { sha256, size, path } }` |
| POST | `/admin/campaign/{levelId}/revert` | `{ version, base_version, records }` → `200 { level_id, version, content_hash, records, carried_bests }` |

Contrôles du serveur (jamais de confiance au client) : `level_file` ≤ 2 Mio, JSON, format
`cosmic.level`, `formatVersion`, `id` = le niveau, `planet` = sa planète, `metadata.revision` =
le numéro calculé par le serveur, `bounds`, objets (ids uniques, types, `rect`), exactement un
`player_start` et une sortie ; `manifest` (entrée de `--online-manifest`) cohérent avec le
fichier (révision, planète, `bounds`, sortie, temps) ; empreinte déjà utilisée par une autre
révision → `409 HASH_EXISTS` ; `base_version` ≠ révision courante → `409 BASE_CHANGED` (deux
admins ne s'écrasent pas : tout est fait dans un seul lot conditionné par la révision de base).
Erreurs : `400 VALIDATION_ERROR`, `422 LEVEL_INVALID` / `MANIFEST_MISMATCH` (avec la liste des
problèmes), `409 BASE_CHANGED` / `HASH_EXISTS` / `REVISION_CONFLICT` / `ALREADY_CURRENT` /
`REVISION_DISABLED`, `413`, `426 GAME_VERSION_UNSUPPORTED`.

Publiques (les fichiers de niveau font partie du jeu, rien de personnel) :

| Méthode | Route | Réponse |
|---|---|---|
| GET | `/campaign/revisions` | `{ format: "cosmic.campaign_revisions", levels: [{ level_id, version, content_hash, max_version, min_game_version, file: { version, content_hash, sha256, size, min_game_version, path } \| null, rating, pinned: [file…] }] }` |
| GET | `/campaign/levels/{levelId}/revisions/{version}/file` | le fichier (`application/json`), en-têtes `x-content-sha256`, `x-level-hash`, `x-level-version` ; `404 REVISION_NOT_FOUND` |

Les fichiers sont dans le bucket R2 privé, sous `campaign/<niveau>/<révision>-<sha256>.json`,
servis uniquement par le Worker (et inclus dans les sauvegardes).

## 5. Le jeu

- Au démarrage (et toutes les 30 minutes quand Cosmic Online est actif), le jeu lit
  `/campaign/revisions`, télécharge les révisions qui lui manquent dans
  `<dossier utilisateur>/online/campaign/` et les vérifie : SHA-256 des octets = celui du
  serveur, niveau de cet id et de ce numéro, **empreinte classée calculée par ce jeu = celle du
  serveur** (sinon la révision a été faite pour une autre version du jeu). Une révision pour un
  jeu plus récent (`min_game_version`) n'est pas téléchargée.
- La révision courante remplace le fichier livré (`ContentDatabase::setCampaignRevision` : même
  id, toujours un niveau officiel) ; empreintes, notes et noms suivent.
- Hors ligne, la dernière révision téléchargée continue d'être jouée (l'index garde le dernier
  manifeste et le serveur). Un fichier abîmé, inconnu ou fait pour une autre version du jeu
  n'est pas utilisé : le niveau livré est joué.
- Si le niveau livré est plus récent que toutes les révisions connues du serveur
  (`max_version`), le jeu garde le sien (le serveur enregistre les niveaux d'une version après
  sa sortie).

## 6. Versions du jeu suivantes

Une révision publiée en ligne prend un numéro de révision. La version suivante du jeu qui
régénère la campagne doit utiliser un `metadata.revision` plus grand que la plus haute révision
du serveur, sinon la migration de ses niveaux ne la rend pas courante (`levels-migration.mjs`
vérifie maintenant que ce numéro correspond bien à ce contenu). Pour intégrer une révision
publiée en ligne dans les niveaux livrés, la reproduire dans `tools/levelgen`.
