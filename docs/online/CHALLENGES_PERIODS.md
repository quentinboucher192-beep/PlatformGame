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

### Précisions du serveur (2.33, lecture la plus simple de ce qui précède)

- `GET /challenges/{challengeId}` : `data` est **l'objet défi lui-même** (même forme que dans
  `/challenges/current`), `404 CHALLENGE_NOT_FOUND` pour un défi futur, inconnu ou mal formé
  (`weekly-2027-W53` n'existe pas : 2027 a 52 semaines).
- `/challenges/current` : `previous.<sorte>` = le défi de la période précédente (hier, la
  semaine dernière, le mois dernier) ; `null` quand il n'y en a pas.
- `/challenges/calendar` : `month` facultatif (défaut : le mois en cours), `400
  VALIDATION_ERROR` s'il n'est pas `AAAA-MM` (années 2000 à 2999). `challenges` triés par
  premier jour, le mois puis la semaine puis le jour pour un même premier jour. `me` vaut
  `null` sans connexion, `{}` connecté sans partie ; seuls les défis où j'ai une partie y
  figurent. `first_month` = le mois en cours tant qu'aucun défi n'existe.
- Résultat d'un défi (`me` du calendrier, `results[]`) : les parties **à l'heure** d'abord.
  Avec au moins une partie à l'heure non rejetée : la meilleure d'entre elles (son temps, son
  statut ; `tier` = meilleur palier à l'heure), `late: false`. Sinon, avec seulement des
  parties hors délai non rejetées : la meilleure d'entre elles, `late: true`. Sinon (tout est
  rejeté) : la meilleure partie rejetée, `tier: null`, `status: "REJECTED"`. `best_progress`
  : voir §5 (serveur 2.33 : toujours `null`). `results[]` garde `date` (= premier jour) et
  contient aussi les défis de la semaine et du mois (un jeu 2.32 les range par
  `challenge_id`, sans effet).
- Classement : la réponse rappelle `period` et `board` ; chaque entrée et `me` ont `late`
  (toujours `false` avec `period=live`). Un joueur qui n'a que des parties hors délai n'est
  pas dans `period=live` (son `me` y vaut `null`). `board` : `time` ou (serveur 2.34)
  `progress`, `400` sinon.
- Partie : `run.late` aussi dans la réponse idempotente (`duplicate: true`) et dans celle du
  replay. Le contrôle de la partie (paliers, replay, première place en revue) est le même ;
  la position utilisée pour la revue est celle du classement `live` pour une partie à
  l'heure, `all` pour une partie hors délai. Le message de `426` dit « défis » (plus « défis
  du jour »).
- Tirage automatique (le serveur seul, depuis `src/challengeCatalog.json`, version 2 : place
  du niveau dans sa planète). Tous les niveaux de la campagne ont un boss, donc « niveaux à
  boss » ne distingue rien : **semaine** = 3e, 4e ou dernier niveau d'une planète (avec le
  catalogue actuel : `*_03`, `*_04` et `noyau_02`), **mois** = dernier niveau de chaque
  planète (les Épreuves `*_04` et le niveau final `noyau_02`). Le boss rush `noyau_01`
  (EXTRÊME, une seule vie) n'est jamais tiré : comme tout niveau non tirable du catalogue et
  tout niveau de la communauté, seul l'administrateur peut le choisir. Règles « plus
  exigeantes » : semaine = cœurs 1, 2 ou 3 et 0 ou 1 pouvoir en plus ; mois = cœurs 1, 2 ou 3
  et aucun pouvoir en plus ; stations toujours actives, points de passage au hasard, paliers
  comme le jour. Le tirage évite les niveaux des défis de la même sorte des 10 jours / 4
  semaines / 2 mois autour.
- Script : `--kind` vaut `daily` par défaut ; `--date X` = `--kind daily --period X`
  (`--date` avec une autre sorte est refusé). Le nom de fichier conseillé devient
  `NNNN_challenge_<sorte>_<période>.sql`. La migration n'écrit que si le premier jour de la
  période n'a pas commencé quand elle est appliquée.
- Notification `challenge_featured` : envoyée une fois quand la période commence ; `data`
  ajoute `challenge_kind` et `period` (le texte vient du serveur ; une ancienne notification
  sans `challenge_kind` reste « du jour »).
- Base : la table `daily_challenges` devient `challenges` (colonnes `period`, `end_date` ;
  déclencheur `challenges_frozen`), `challenge_runs` gagne `late` (migration
  `0058_challenge_periods.sql`, qui recopie les défis et parties de 2.32).

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

### Précisions du serveur (2.34, lecture la plus simple de ce qui précède)

- Objet défi : `progress_available` (booléen) en plus, sur toutes les routes qui le donnent.
  Il est vrai quand la version du niveau du défi a un tracé. **Avec les fichiers actuels,
  seul `noyau_01` (EXTRÊME) en a un dans la campagne**, et les niveaux EXTRÊME de la
  communauté en ont un : un défi tiré automatiquement a donc `progress_available: false` tant
  que les autres niveaux de la campagne n'ont pas de tracé enregistré.
- `POST /challenges/{challengeId}/attempts` (connecté · limite `challenge_submit`, corps 4 Kio
  au plus, aucun autre champ : ni joueur, ni score) : `claim.finished` doit valoir `false`
  (une partie finie s'envoie par `…/runs`) ; `claim.bosses` : un entier 0..1000 par boss du
  tracé, `claim.waves` : un entier 0..99 par embuscade du tracé, dans l'ordre du tracé
  (`400 VALIDATION_ERROR` sinon). Mêmes contrôles et mêmes erreurs que `…/runs`
  (`CHALLENGE_NOT_STARTED`, `CHALLENGE_NOT_FOUND`, `426`, `LEVEL_MISMATCH`,
  `RULES_MISMATCH`), plus `409 PROGRESS_UNAVAILABLE` (pas de tracé). Idempotent par
  `attempt_id` (`ATTEMPT_CONFLICT` si les données diffèrent) ; hors délai comme les parties
  (`late`). Réponse `201 { duplicate: false, attempt: { attempt_id, status: "PENDING",
  progress: null, finished: false, late }, replay: { required: true, upload_path } }` (`200`
  et `duplicate: true` pour un essai déjà reçu). Un essai plus long que la limite de temps du
  niveau est refusé d'emblée (`status: "REJECTED"`, `required: false`).
- `PUT /challenges/attempts/{attemptId}/replay` (connecté · limite `replay_upload`,
  `application/octet-stream`, 1 Mio au plus) : `{attemptId}` est **l'`attempt_id` envoyé par
  le jeu** (cherché parmi les essais du joueur connecté seulement ; `upload_path` le donne
  tel quel). Erreurs : `404 ATTEMPT_NOT_FOUND`, `409 REPLAY_EXISTS` (déjà reçu), `409
  ATTEMPT_REJECTED`, `422 REPLAY_MALFORMED` (rien n'est gardé), `409 PROGRESS_UNAVAILABLE`.
  Réponse `201 { attempt: { attempt_id, status: "VERIFIED" | "REJECTED", progress, finished:
  false, late, rank, reasons }, new_best, best_progress }` : `progress` (une décimale, < 100)
  calculé par le serveur, `null` si l'essai est refusé ; `rank` dans `board=progress` de sa
  période (`live` à l'heure, `all` hors délai) ; `new_best` : meilleur que tous mes essais et
  parties finies vérifiés de ce défi ; `best_progress` : ma meilleure progression vérifiée sur
  ce défi après cet essai. Contrôles : ceux de la course à la vérification (version du niveau,
  `attempt_id`, durée, départ, trajectoire, boss et vagues **identiques** à ce que le replay
  montre) + section défi exactement la forme canonique (format 4), vie jamais au-dessus de
  `hearts`, durée dans la limite du niveau. `checkpoints: false` : une seule vie (l'essai
  s'arrête à sa première mort ; un `Respawn` ou deux morts le font refuser) ; `checkpoints:
  true` : morts et retours permis, la trajectoire peut alors sauter à une mort ou un retour.
  « Vérifié » = replay contrôlé par le serveur, comme les parties.
- Un seul replay d'essai par joueur et par défi : celui du meilleur essai vérifié ; un essai
  moins bon n'est pas gardé, l'ancien meilleur perd le sien. Ce replay se regarde depuis le
  classement : `GET /challenges/attempts/{id}/replay` (connexion facultative ; `{id}` =
  `attempt_id` d'une entrée de `board=progress`, l'**identifiant du serveur** de l'essai, pas
  celui du jeu), public pour un essai `VERIFIED` d'un compte actif, toujours pour son joueur ;
  `403 PROFILE_PRIVATE` pour un profil masqué ; `404 ATTEMPT_NOT_FOUND` (inconnu, refusé, en
  attente) ou `404 REPLAY_NOT_FOUND` (replay non gardé). Réponse :
  `application/octet-stream`, en-tête `x-replay-sha256`, comme `GET /challenges/runs/{runId}/replay`.
- `board=progress` : `409 PROGRESS_UNAVAILABLE` sans tracé ; `period`, `scope`, `limit`,
  `offset` comme `board=time`. Entrées `{ rank, player, progress, finished, time_ms, status:
  "VERIFIED", late, is_me, attempt_id, run_id, has_replay }` : un essai (`finished: false`,
  son `time_ms`, `attempt_id` = identifiant du serveur pour regarder son replay, `run_id:
  null`) ou une partie finie vérifiée (`progress: 100`, `finished: true`, son temps,
  `attempt_id: null`, `run_id` pour `GET /challenges/runs/{runId}/replay`). `has_replay` : le
  replay est gardé et ce lecteur peut le regarder (`false` pour un profil masqué, sauf le
  sien ; un essai qui n'est plus le meilleur de son joueur n'a plus de replay). À score égal, le plus tôt
  envoyé d'abord (entre parties finies aussi : la première envoyée, pas la plus rapide).
  `me` : ma meilleure progression vérifiée de cette période (`rank: null` hors de ce
  `scope`), sinon `null` (un essai en attente de son replay n'a pas encore de score).
- `best_progress` (`results[]`, `me` du calendrier) : ma meilleure progression vérifiée sur
  ce défi, à l'heure ou hors délai (essais ; 100 pour une partie finie vérifiée, même sans
  tracé), `null` sans. Un défi seulement essayé (aucune partie finie) figure dans les
  résultats avec `best_time_ms: null`, `tier: null`, `status` et `late` de son meilleur essai.
  Les essais ne comptent pas pour les séries.
- Base : table `challenge_attempts` (migration `0059_challenge_progress.sql`) ; dans l'export
  des données (`challenge_attempts`) ; supprimée avec le compte, replays compris.
