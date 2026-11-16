# Entités natives modifiées par un admin (2.46)

Les entités natives (ennemis et boss livrés avec le jeu, `data/enemies`) sont en lecture seule
dans l'éditeur d'ennemis du joueur (EN LIGNE › ATELIER). Un compte admin les modifie dans
ADMINISTRATION › **Ennemis** et publie une **révision** de leur définition, sans mise à jour du
jeu.

Code : `server/src/enemyRevisions.ts` (routes), `server/migrations/0088_enemy_revisions.sql`,
`server/src/enemyCatalog.json` (copie des entités natives, `node scripts/enemies-catalog.mjs`),
`game/online/EnemyRevisions.*` (révisions gardées par le jeu), `game/online/EnemyPublication.*`
(préparation des niveaux), `editors/enemy_editor/EnemyEditAccess.*` (lecture seule),
`editors/enemy_editor/EnemyEditorPublish.cpp` (publication), `ui/screens/AdminEnemies.*` (onglet).

## 1. Une révision d'entité = une révision de chaque niveau qui l'utilise

L'empreinte classée d'un niveau contient les définitions de ses ennemis : changer une entité
change le contenu de chaque niveau de la campagne qui l'utilise (placée, ou amenée par une autre
entité : invocation, division…). Une publication est donc **un seul lot** côté serveur :

- la révision de l'entité (numéro `MAX + 1`, calculé par le serveur), sa définition gardée
  octet pour octet dans le bucket R2 privé (`enemies/<id>/<révision>-<sha256>.json`) ;
- pour chaque niveau concerné, sa révision suivante (même contenu, `metadata.revision` augmenté)
  dont le fichier porte la **liaison** `metadata.enemyRevisions` (`{"<id>": <révision>}`, pour
  toutes les entités natives qu'il joue à leur révision en ligne), qui fait partie de son
  empreinte ; sa note de difficulté est recalculée. Ce sont des révisions de campagne comme les
  autres ([CAMPAIGN_REVISIONS.md](CAMPAIGN_REVISIONS.md)) : manifeste, fichiers, historique de
  l'onglet Campagne.

**Choix des records, niveau par niveau** : avant d'envoyer, l'admin voit la liste des niveaux
qui utilisent l'entité (nom, planète, révision actuelle → nouvelle, nombre de meilleurs temps,
exemplaires placés) avec une case **« Réinitialiser les records »** par niveau (cochée par
défaut ; *Tout cocher* / *Tout décocher*) :

| Case | Effet |
|---|---|
| cochée (`records: "reset"`) | la révision du niveau a ses propres classements ; ceux d'avant restent archivés sous leur empreinte |
| décochée (`records: "keep"`) | les meilleurs temps et les classements de la semaine sont reportés (règle « Garder » des révisions de campagne) |

## 2. Replays exacts : jamais de définition globale

Le jeu garde **toutes** les révisions d'entités téléchargées (`<dossier utilisateur>/online/enemies/`).
Une révision d'entité n'est jouée que par les révisions de niveau liées à elle
(`ContentDatabase::enemyFor`) : une ancienne révision d'un niveau (record archivé, replay,
fantôme, défi déjà commencé) garde la définition avec laquelle elle a été publiée. Les niveaux de
la communauté et ceux du joueur gardent les définitions livrées avec le jeu (leurs records ne
changent pas). Aucune règle de simulation ne change (`kSimulationVersion`, `kRankingGeneration`).

Au démarrage (et toutes les 30 minutes en ligne), le jeu lit `/enemies/revisions` **avant**
`/campaign/revisions`, télécharge les révisions qui lui manquent, vérifie leur SHA-256 et leur
format, puis vérifie chaque révision de niveau avec les définitions auxquelles elle est liée.
Hors ligne, tout ce qui a été téléchargé reste joué. Une révision faite par un jeu plus récent
n'est pas utilisée.

## 3. Contrôles du serveur (jamais de confiance au client)

Rôle admin vérifié à chaque requête (`requireAdminRole` : `404` pour un autre compte, `401` sans
session). L'entité doit être une entité native du jeu (`enemyCatalog.json`, sinon `404
ENEMY_NOT_FOUND`) ; la définition (≤ 256 Kio) : JSON, format `cosmic.enemy`, cet identifiant, un
nom, une animation, `hp` de 1 à 10 000 000, chaque nombre fini et borné, listes et objets de
taille raisonnable (`422 DEFINITION_INVALID`). Chaque niveau (60 au plus) est vérifié comme une
révision de campagne (fichier, manifeste, empreinte déjà utilisée) et sa liaison doit nommer la
nouvelle révision de l'entité et, pour les autres, des révisions qui existent (`422
LEVEL_INVALID`). `base_revision` et chaque `base_version` doivent être les révisions en ligne :
sinon `409 BASE_CHANGED`, vérifié une seconde fois **dans** le lot (colonne `base_checked`) pour
que deux admins ne s'écrasent jamais. Publications et refus sont inscrits au Journal (filtre
« Ennemis » : qui, quand, entité, révision, niveaux et choix des records, meilleurs temps
reportés).

## 4. Routes

| Méthode | Route | Corps / réponse |
|---|---|---|
| GET | `/admin/enemies` | `{ enemies: [{ enemy_id, name, current: { revision, published_by, published_at } \| null, revisions }] }` |
| GET | `/admin/enemies/{id}/revisions` | `{ enemy_id, current_revision, next_revision, current_revisions, levels: [{ level_id, name, planet_id, version, next_version, content_hash, bests }], revisions: [{ revision, is_current, sha256, size, published_by, published_at, levels }] }` |
| POST | `/admin/enemies/{id}/revisions` | `{ definition, base_revision, game_version, levels: [{ level_id, base_version, records, level_file, manifest, rating? }] }` → `201 { enemy_id, revision, sha256, size, path, levels: [{ level_id, version, content_hash, records, carried_bests, file }] }` |
| GET | `/enemies/revisions` (public) | `{ format: "cosmic.enemy_revisions", enemies: [{ enemy_id, current, revisions: [{ revision, sha256, size, min_game_version, path }] }] }` |
| GET | `/enemies/{id}/revisions/{revision}/file` (public) | la définition, en-têtes `x-content-sha256`, `x-enemy-revision` ; `404 REVISION_NOT_FOUND` |

Une entité ajoutée au jeu doit être recopiée dans `enemyCatalog.json`
(`node scripts/enemies-catalog.mjs`) avant de pouvoir être publiée.
