# Défis de la semaine et du mois, calendrier, « Plus loin » (2.33 et 2.34)

Suite de `DAILY_CHALLENGES.md` (défi du jour, 2.32), qui reste valable pour tout ce qui n'est
pas changé ici. Mêmes principes : jouable sans compte et hors ligne, le serveur ne fait jamais
confiance au client, un défi futur n'est jamais révélé, un défi commencé ne change plus,
« Vérifié » = replay contrôlé par le serveur (pas de re-simulation : étape suivante).

Deux livraisons :

- **2.33.0** : défis de la semaine et du mois, calendrier, anciens défis jouables (§1 à §4).
- **2.34.0** : classement « Plus loin » (essais qui vont le plus loin, finis ou non) (§5).

Les jeux 2.32.x continuent de fonctionner sans changement (routes et réponses existantes
gardées, voir §3).

## 1. Trois sortes de défis

| sorte | identifiant | période (UTC) | tirage automatique |
|---|---|---|---|
| `daily` | `daily-AAAA-MM-JJ` | le jour, 00:00 → 00:00 | comme en 2.32 |
| `weekly` | `weekly-AAAA-Www` (semaine ISO 8601, `W01`..`W53`) | lundi 00:00 → lundi suivant 00:00 | niveau plus long ou plus dur (3e/4e niveau d'une planète, niveaux à boss), règles plus exigeantes |
| `monthly` | `monthly-AAAA-MM` | le 1er 00:00 → 1er du mois suivant 00:00 | le plus dur (Épreuves, niveau final, boss rush, ou niveau EXTRÊME de la communauté **choisi par l'administrateur** seulement) |

- L'objet défi (réponse JSON) garde la forme de 2.32, avec `kind` = `daily|weekly|monthly`,
  `period` (`"2026-11-16"`, `"2026-W47"`, `"2026-11"`), `starts_at`, `ends_at` ; `date` reste
  présent (= jour de début) pour les jeux 2.32.
- Forme canonique des règles et `rules_hash` : **inchangées** (la clé `"challenge"` porte
  l'identifiant, quelle que soit la sorte). Le replay format 4 est inchangé. Aucune règle de
  simulation nouvelle : `kSimulationVersion` reste 11, `kRankingGeneration` reste 8.
- Paliers : mêmes trois paliers (Bronze / Argent / Or), mêmes règles de calcul.
- Tirage à l'avance (pour que l'administrateur puisse remplacer) : jours jusqu'à J+7,
  semaines jusqu'à S+2, mois jusqu'à M+1. Jamais montrés avant leur début.
- Script d'administration : `node scripts/challenge-set.mjs --kind daily|weekly|monthly
  --period <AAAA-MM-JJ | AAAA-Www | AAAA-MM> --level campaign:<id> | community:<uuid>@<version>
  [règles…]` (l'ancienne forme `--date AAAA-MM-JJ` reste acceptée pour le jour). Refuse une
  période commencée. Créateur à l'honneur : même notification (`challenge_featured`) pour les
  trois sortes, texte adapté (« … est le défi de la semaine / du mois ! »).

## 2. Anciens défis jouables (« hors délai »)

- Un défi terminé reste jouable, aussi longtemps que son niveau est disponible à la même
  version (même `level_hash` ; sinon : « S'entraîner » seulement, hors classement).
- Une partie envoyée après `ends_at` + 10 min est acceptée avec `late = true` (plus de
  `CHALLENGE_ENDED` pour un jeu 2.33 ; un jeu 2.32 qui l'envoie quand même est aussi accepté :
  rien ne casse). Elle est contrôlée comme les autres.
- Une partie hors délai **ne compte pas** pour la série ni pour les futurs badges, et
  n'apparaît que dans le classement « Depuis » (§3, `period=all`).

## 3. Routes (`/api/v1`)

Gardées telles quelles pour les jeux 2.32 : `GET /challenges/daily/current`,
`GET /challenges/daily`, et toutes les routes par identifiant de défi.

| méthode | route | accès | rôle |
|---|---|---|---|
| GET | `/challenges/current` | public | `{ server_time, daily, weekly, monthly, previous: { daily, weekly, monthly } }` (chacun : un défi ou `null`) |
| GET | `/challenges/calendar?month=AAAA-MM` | public (+ `me` si connecté) | `{ month, first_month, challenges: [défi…], me: { "<challenge id>": { best_time_ms, tier, status, late, best_progress } } }` : tous les défis **commencés** qui touchent ce mois (jours du mois, semaines qui le chevauchent, le mois). `first_month` = mois du plus ancien défi (pour bloquer la navigation). |
| GET | `/challenges/{challengeId}` | public | un défi commencé (404 sinon) |
| GET | `/challenges/{challengeId}/leaderboard?scope=world\|country\|friends&period=live\|all&board=time\|progress&limit=50` | public (`friends` : connecté) | `period=live` (défaut) : parties reçues pendant le défi (+10 min) ; `all` : aussi les parties hors délai, marquées `late: true`. `board=time` (défaut) : comme 2.32. `board=progress` : §5 (2.34). |
| POST | `/challenges/{challengeId}/runs` | connecté | comme 2.32 ; réponse : `run.late` en plus |
| GET | `/challenges/me?limit=60` | connecté | comme 2.32 (`streak` = série des jours) **+** `streaks: { daily, weekly, monthly }` (mêmes champs `current, best, last_period`), et `results[]` avec `kind`, `period`, `late`, `best_progress` |

Série d'une sorte : périodes consécutives (jours, semaines ISO, mois) jusqu'à la période en
cours ou la précédente, avec au moins une partie **non hors délai** et non rejetée.

## 4. Côté jeu (2.33.0)

- Écran « Défis » : onglets **Jour**, **Semaine**, **Mois**, **Calendrier** (remplace
  « Archives »). Les trois premiers montrent le défi en cours de leur sorte, avec la même fiche
  qu'en 2.32 (niveau, règles, paliers, mon meilleur temps, série de la sorte, classement,
  Jouer / Revoir / S'entraîner, « Touches… » avant le départ).
- **Calendrier** : un mois en grille (lundi → dimanche), navigation mois précédent / suivant
  (jamais avant `first_month`, jamais après le mois en cours). Chaque jour : le défi du jour
  (mon palier ou mon état : vérifié, en attente, hors délai, rien), le jour courant mis en
  valeur, les jours à venir grisés et sans contenu. Chaque semaine (bande ou colonne) : le
  défi de la semaine ; le défi du mois en tête. Clic / Entrée / bouton A sur une case : la
  fiche de ce défi (comme les onglets), avec pour un défi terminé le classement
  « Pendant le défi » / « Depuis » et « Jouer (hors délai) ». Jouable au clavier, à la manette
  et à la souris.
- Hors ligne : le calendrier montre ce qui est en cache et le dit.
- Fin d'une partie hors délai : « Hors délai : classé dans "Depuis", ne compte pas pour la série ».
- Classement : chaque ligne qui a un replay (`has_replay`, `run_id`) a son bouton « Voir »
  (`GET /challenges/runs/{runId}/replay`, public pour une partie VERIFIED, comme en 2.32).

### Précisions (lecture retenue par le jeu 2.33)

- Un défi dont l'`id` n'est pas exactement `<kind>-<period>`, ou dont `starts_at` / `ends_at`
  ne sont pas les bornes UTC de sa période (et `date` son premier jour), est refusé par le jeu.
  Un défi sans `kind` ni `period` (serveur 2.32) est un défi du jour (`period` = `date`).
- Serveur pas encore à jour : `GET /challenges/current` → 404 : le jeu lit
  `GET /challenges/daily/current` (le jour seulement) ; `GET /challenges/calendar` → 404 : le
  jeu prend les jours du mois dans `GET /challenges/daily?before=<1er du mois suivant>&limit=30`
  (deux pages au plus) ; un défi plus ancien que le mois dit que le calendrier va plus loin.
- Classement : `period=live` (défaut) n'est pas écrit dans la requête ; `period=all` l'est.
  Avec `period=all`, chaque entrée (et `me`) porte `late`.
- `me` du calendrier : une clé par identifiant de défi (`null` sans session) ; `best_time_ms`
  et `tier` `null` quand le joueur n'a fini aucune partie de ce défi (des essais seulement).
- `streaks.*.last_period` : la dernière période de la série (`"2026-11-16"`, `"2026-W47"`,
  `"2026-11"`) ; le jeu lit encore `streak.last_date` d'un serveur 2.32 pour le jour.
- Le jeu estime qu'une partie est hors délai quand elle finit plus de 10 min après `ends_at`
  (horloge du serveur) ; il garde ensuite le `run.late` que le serveur renvoie.

## 5. « Plus loin » (2.34.0)

Pour les défis que peu de joueurs finissent : chaque essai est noté par sa progression, comme
la course à la vérification (`server/src/race.ts`, `game/race/Race.cpp`) : 40 points pour le
trajet atteint le long du tracé du niveau, 60 pour les boss (2 parts chacun, santé retirée) et
les vagues d'embuscades (1 part chacune), 100 seulement si le niveau est fini. Le tracé vient
de `level_versions.race` (campagne) ou `community_level_versions.race` (communauté) ; sans
tracé, pas de « Plus loin » pour ce défi (`progress_available: false` dans l'objet défi).

- Le jeu calcule la progression d'un essai non fini (même fonction `raceScore`) seulement
  pour décider s'il bat son meilleur essai sur ce défi ; si oui, il l'envoie :
  `POST /challenges/{challengeId}/attempts` `{ attempt_id, time_ms, level_hash, rules_hash,
  game_version, claim: { finished: false, bosses: [‰…], waves: [n…] } }` puis
  `PUT /challenges/attempts/{attemptId}/replay` (format 4). Le **serveur** calcule le score
  depuis le replay (trajectoire projetée sur le tracé, événements BossHit / vagues) ; il ne
  prend jamais celui du jeu. Morts et retours aux points de passage permis si les règles du
  défi ont `checkpoints: true` (le score = le plus loin atteint pendant l'essai).
- Une partie finie compte 100.
- `board=progress` : meilleur score par joueur (VERIFIED), du plus haut au plus bas ; à score
  égal, le plus tôt envoyé d'abord. Entrées : `{ rank, player, progress, finished, time_ms,
  status, late }`.
- Fiche du défi : bascule **« Meilleurs temps » / « Plus loin »** au-dessus du classement ;
  « Mon meilleur essai : 63,4 % » à côté de mon meilleur temps ; fin d'un essai non fini :
  progression de l'essai, « Nouveau record de progression » si c'est le meilleur, état de
  l'envoi.
- Limites de débit : comme les parties ; un joueur ne garde qu'un replay d'essai par défi
  (le meilleur).
