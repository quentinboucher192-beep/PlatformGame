# Règles des classements et validation des records

## Identité d'un classement

Un classement est toujours défini par : **niveau** (`level_id`), **révision exacte**
(`level_version` + `level_hash`, SHA-256 du JSON canonique du niveau), **mode** (`STANDARD`),
**catégorie**, et une **version compatible du jeu** (≥ `min_game_version` de la révision et ≥
`MIN_GAME_VERSION` du serveur). Deux révisions d'un niveau ne sont jamais mélangées : quand
une nouvelle révision est publiée, l'ancienne devient *legacy* (consultable, figée).

L'empreinte est calculée par le jeu (`game/online/LevelFingerprint.cpp`) sur le JSON du
fichier relu puis réécrit par nlohmann::json (clés triées, nombres dans leur forme la plus
courte) : elle ne dépend ni des espaces ni des fins de ligne, donc Windows et Linux donnent la
même valeur. Seuls les niveaux livrés avec le jeu, lancés depuis leur début (sans point de
départ forcé, hors test de l'éditeur), produisent des performances classées.

## Catégories

| Catégorie | Identifiant | Règle |
|---|---|---|
| ANY% | `ANY_PERCENT` | terminer le niveau, le plus vite possible |
| 100% | `HUNDRED_PERCENT` | terminer avec **tous** les éclats et reliques du niveau (`collectibles == total_collectibles` de la révision) |
| NO DEATH | `NO_DEATH` | terminer sans mourir (`deaths == 0`) |
| NO DAMAGE | `NO_DAMAGE` | terminer sans mourir ni subir de dégâts (`deaths == 0`, `damage_taken == 0`) |
| SPEEDRUN | `SPEEDRUN` | run d'une traite depuis le début, **replay obligatoire et validé** pour être vérifié |

Un même run compte dans toutes les catégories qu'il remplit (une soumission par catégorie,
même `attempt_id`). Le jeu n'envoie une catégorie que si le temps améliore le meilleur temps
local de cette catégorie pour cette révision.

Le temps est celui du chronomètre du niveau (millisecondes) : il démarre quand le joueur prend
le contrôle et s'arrête à la sortie.

## Classements

| Classement | Calcul |
|---|---|
| Niveau (mondial) | meilleur temps **vérifié** de chaque joueur sur la révision courante ; égalité départagée par la date du record |
| Pays | même chose, limité aux joueurs qui affichent ce pays (les joueurs à localisation masquée n'y apparaissent pas) ; le rang mondial reste indiqué |
| Amis | vous et vos amis acceptés |
| Hebdomadaire | meilleur temps vérifié réalisé depuis lundi 00:00 UTC (remise à zéro logique : rien n'est effacé) |
| Historique | meilleur temps vérifié de tous les temps (table `player_bests`) |
| Planète | somme des meilleurs temps sur **tous** les niveaux classés de la planète (seuls les joueurs qui les ont tous terminés) |
| Global (points cosmiques) | somme sur tous les niveaux classés de `1000 × record mondial / votre meilleur temps` |

Les records **PENDING** et **REJECTED** n'apparaissent jamais dans un classement ni sur le
globe. Un joueur voit sa propre performance en attente à part (« EN ATTENTE »).

## Validation d'un record (serveur)

Dans l'ordre :

1. **Authentification** : le joueur est celui de la session. Le champ `player_id` est refusé.
2. **Schéma** : champs connus uniquement, types et bornes (`time_ms` entier de 1 ms à 24 h,
   `attempt_id` UUID v4, `level_hash` 64 hexadécimaux, version `x.y.z`...). Corps limité à 4 Kio.
3. **Niveau** existant et classé (`ranking_enabled`), **révision** exacte (version + empreinte),
   révision active (sinon `LEVEL_VERSION_MISMATCH` / `LEVEL_VERSION_RETIRED`).
4. **Version du jeu** compatible (sinon 426 `GAME_VERSION_UNSUPPORTED`).
5. **Règles de la catégorie** (sinon 422 `CATEGORY_RULES_NOT_MET`, rien n'est enregistré).
6. **Cohérence de progression** : total d'objets identique à la révision, objets ≤ total
   (sinon 422 `INCONSISTENT_PROGRESS`).
7. **Doublons** : même tentative et même catégorie → réponse idempotente (`duplicate: true`) ;
   même tentative avec d'autres données → 409 `ATTEMPT_CONFLICT`.
8. **Temps impossibles** : sous `min_time_ms` de la révision (35 % du temps de référence du
   concepteur, au moins 10 s) → **REJECTED** `IMPOSSIBLE_TIME` et niveau de confiance remis à 0 ;
   au-delà de la limite de temps → **REJECTED** `TIME_OVER_LIMIT`.
9. **Replay** (envoyé ensuite, voir ci-dessous) et **niveau de confiance** → décision.

## Analyse du replay

Le serveur décompresse (taille plafonnée à 4 Mio, protection contre les bombes de
décompression), lit le format (voir [REPLAY_FORMAT.md](REPLAY_FORMAT.md)) et vérifie :

* même niveau, même révision, même tentative que le record ;
* durée du replay = temps soumis (± 0,5 s) et nombre d'échantillons cohérent ;
* nombre de morts et d'objets du replay = valeurs du record ; pas de dégâts pour NO DAMAGE ;
* présence de l'événement de fin ; départ à moins de 96 px du point de départ mesuré du
  niveau ; arrivée dans la sortie (± 160 px) ;
* positions dans les limites du niveau ;
* aucun déplacement impossible entre deux échantillons (vitesse maximale de la révision avec
  25 % de marge ; sauts autorisés uniquement autour des morts, réapparitions et téléportations).

Un replay illisible est refusé (422, rien n'est stocké, le jeu peut réessayer). Un replay
lisible mais incohérent rend le record **REJECTED** (`REPLAY_INVALID`, raisons détaillées).

## Statuts et niveaux de confiance

| Confiance | Obtention |
|---|---|
| 0 | nouveau compte, ou record refusé dans les 30 derniers jours |
| 1 | au moins 3 records vérifiés avec replay |
| 2 | au moins 15 records vérifiés avec replay, aucun refus récent |
| 3 | attribué par un modérateur |

Décision (`decide()` dans `server/src/records.ts`) :

* replay invalide → **REJECTED** ;
* SPEEDRUN sans replay valide → **PENDING** ;
* nouveau record mondial battant l'ancien de plus de 20 % (confiance < 3) → **PENDING**
  (revue manuelle) ;
* nouveau record mondial sur un classement d'au moins 5 joueurs, confiance < 2 → **PENDING** ;
* replay valide → **VERIFIED** ;
* sans replay : confiance ≥ 2 → **VERIFIED**, sinon **PENDING** en attendant le replay.

Les statuts côté joueur : **LOCAL** (pas encore envoyé), **EN ATTENTE** (PENDING),
**VÉRIFIÉ**, **REFUSÉ**. Le jeu interroge régulièrement le serveur pour les records en attente
et notifie le joueur du résultat.

## Limites de l'anti-triche (honnêtement)

Le client est un programme distribué : il peut être modifié. Le serveur ne fait jamais
confiance aux données reçues et l'analyse de replay rend la triche coûteuse (il faut fabriquer
une trajectoire plausible, cohérente avec le niveau, les morts, les objets et le temps), mais
**elle ne la rend pas impossible** : la trajectoire n'est pas confrontée aux collisions du
niveau ni re-simulée (le moteur n'est pas déterministe d'un compilateur à l'autre). C'est
pourquoi les records mondiaux des comptes peu fiables passent par une revue manuelle
(`/api/v1/admin/records`), et le format de replay est versionné pour permettre une validation
plus stricte plus tard (re-simulation côté serveur, contrôle des collisions).
