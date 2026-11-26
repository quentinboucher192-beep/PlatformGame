[← Retour à l'accueil (README)](../README.md)

# Ghost Racing, succès et mode concentré (2.18.0)

## Ghost Racing

Courir contre des **fantômes** : des courses déjà faites, rejouées en même temps que la
vôtre, sur le chronomètre du niveau.

### D'où viennent les fantômes

| Fantôme | Origine | Stockage |
|---|---|---|
| Mon meilleur temps | votre course la plus rapide sur **cette version** du niveau | `ghosts/local/<niveau>-<version>/pb.cprz` |
| Mon record précédent | le meilleur temps que le dernier a remplacé | `…/prev.cprz` |
| Ma dernière course | la dernière course terminée | `…/last.cprz` |
| Record mondial | 1re place du classement (officiel : Any%, classement « historique » ; niveau de joueur : version en cours) | `ghosts/cache/<record>.cprz` |
| Record d'un ami | classement des amis (officiel) ou amis présents au classement (niveau de joueur) | idem |
| Record du classement | autres records avec replay (officiel, 10 premiers) | idem |
| Replay choisi | « Importer un replay… » (fichier `.cprz`, vérifié puis copié) | `ghosts/files/` |

Chaque replay garde à côté de lui une description `.json` (joueur, niveau, empreinte exacte
de la version, record, catégorie, durée, date, temps de passage, version du jeu, vérifié en
ligne ou non). Le replay lui-même n'est jamais modifié.

Les replays téléchargés sont gardés dans un **cache** (64 Mo par défaut, réglable dans
`saves/settings.json` → `ghosts.cacheMB`) ; les plus anciens partent en premier.

### Ce qu'est un fantôme (et ce qu'il n'est pas)

* Un fantôme montre la **trajectoire enregistrée** de la course (position des pieds,
  orientation, état d'animation, planeur, bouclier, 30 fois par seconde, interpolée entre
  deux échantillons). Ce n'est **pas** une re-simulation : le monde n'est pas rejoué pour lui.
* Il est **purement visuel** : aucune collision, aucun projectile, aucun ennemi, aucun
  mécanisme, aucun point de sauvegarde. Il n'entre jamais dans la simulation : une partie
  avec ou sans fantôme donne exactement la même simulation et le même replay.
* Un fantôme n'est **jamais une preuve** : seules les vérifications du serveur (inchangées)
  décident des records en ligne. Un fantôme « vérifié en ligne » vient d'un record que le
  serveur a vérifié ; un fantôme « local » n'a été vérifié par personne.

### Écarts

* **En direct** : le temps qu'il vous a fallu pour atteindre le point le plus avancé de la
  route du niveau (la même que celle de la barre de progression) moins le temps qu'il a
  fallu au fantôme pour le même point. « +2,450 s » : retard ; « -1,230 s » : avance ;
  « 0,000 s » : égalité. Entre deux points de sauvegarde, c'est une estimation (la route est
  une ligne, le joueur peut passer ailleurs).
* **Aux points de sauvegarde** : exact. Depuis la 2.18, chaque replay contient l'événement
  `Split` (temps de passage à la milliseconde) ; pour un replay plus ancien, le temps de
  passage est lu sur l'échantillon (environ 1/30 s près) et marqué comme approximatif.
  Les points de sauvegarde sont appariés **par position** (un point sauté ou repris ne décale
  pas la comparaison).
* Les écarts se comparent **à la milliseconde** : une égalité reste une égalité.
* **Segments** : la durée entre deux points (pas le temps cumulé) ; un segment plus court que
  votre meilleur est en **or**. L'écran « Segments » de fin de course sépare toujours les
  durées et les temps cumulés.

### Versions du niveau

Un fantôme est lié à l'**empreinte exacte** de la version du niveau sur laquelle il a été
fait. Un fantôme d'une autre version (niveau modifié après publication, règles du jeu
changées) reste utilisable mais est affiché « **autre version : comparaison non officielle** »
et n'est jamais compté comme une victoire dans les succès.

### Vérifications d'un replay avant la course

Lecture sûre (taille limitée à 4 Mo, conteneur vérifié), trajectoire présente, durée possible
(2 h au plus), nombre d'échantillons cohérent avec la durée, course terminée, positions dans
le niveau, aucun déplacement impossible hors mort, réapparition ou téléportation (3
tolérés : téléporteurs du niveau), temps de passage dans l'ordre. Un replay refusé est
annoncé (« Fantôme ignoré : … ») et la course continue sans lui.

### Mode entraînement

Départ à un point de sauvegarde choisi, arrivée au suivant (ou à la sortie). Les pouvoirs des
tablettes placées avant le départ sur la route sont donnés (les derniers si les
emplacements manquent). Le fantôme part du moment où il est passé à ce point. Le segment
recommence tout seul ; R (ou Vue à la manette) recommence à tout moment.
**Aucun record, aucune progression, rien n'est envoyé en ligne.**

### Commandes

| Action | Clavier | Manette |
|---|---|---|
| Afficher / masquer les fantômes | G | clic du stick gauche |
| Mode concentré | T | Partage (Micro sur PS5) ou pavé tactile |
| Recommencer (contre-la-montre, entraînement) | R | Vue |

Toutes se changent dans *Paramètres → Contrôles*. Les options des fantômes (nombre à la fois,
opacité, couleurs, noms, écart, panneau) sont dans la pause (*Fantômes*) et sur l'écran
d'avant le départ.

### Performances

Les replays sont lus, vérifiés et placés sur la route **hors du fil principal** pendant le
chargement du niveau ; la mise à jour de 8 fantômes coûte environ 0,001 ms par image (test
`ghost: several ghosts at once`). Aucune requête réseau pendant la course : les
téléchargements se font avant le départ.

### Limites connues

* Le fantôme est la trajectoire enregistrée, pas une re-simulation : les ennemis, blocs et
  portes du monde ne sont pas montrés dans l'état où ils étaient pour lui.
* L'état des pouvoirs montré se limite au planeur et au bouclier (et à l'animation).
* En entraînement, l'état du niveau n'est pas celui d'une course complète arrivée à ce point
  (ennemis déjà vaincus, leviers, portes) : seuls les pouvoirs sont reconstitués.
* L'écart en direct est une estimation entre deux points de sauvegarde.
* Les classements en ligne ne gardent un replay que pour les records vérifiés : un joueur
  sans record vérifié n'a pas de fantôme en ligne.

## Une seule somme des meilleurs, enchaînements, startpos et mesures (2.91)

### Une seule SOB par version du niveau

Il n'existe plus qu'**une** somme des meilleurs segments (SOB) personnelle par version du
niveau (`GhostStore::levelKey`), tenue par `PersonalBests` (`game/ghost/PersonalBests.h`,
fichier `saves/bests.json`). Toutes les sources s'y rejoignent :

| Source (étiquette) | Ce qui compte |
|---|---|
| **Partie** | chaque course depuis le départ du niveau, terminée ou non : tout ce qu'elle a traversé en entier |
| **Entraînement** | l'entraînement d'un segment (« Répéter un segment ») |
| **Enchaînement** | l'entraînement de plusieurs segments à la suite (ex. zone 1 → 2) |
| **StartPos** | après une startpos, chaque segment parcouru **en entier** (de son point de départ à son point d'arrivée) |
| **En ligne** | les courses du joueur vérifiées en ligne (jamais comptées deux fois) |

Chaque essai donne au magasin les **bornes** qu'il a franchies (départ du niveau, points de
passage, arrivée) avec son chronomètre à chacune. Pour **toute paire de bornes** qui se
suivent sur la route du niveau (aucun point sauté, aucun autre ordre), le magasin compte un
**passage** et garde le **meilleur temps** (à la milliseconde : une égalité garde l'ancien),
sa source, sa date et le **replay exact** de l'essai qui l'a fait (copie dans
`ghosts/bests/<version>/`, gardée tant qu'un meilleur temps la désigne ; 256 Mo au plus,
les versions jouées il y a le plus longtemps perdent d'abord leurs replays) :

* une paire de bornes voisines = **un segment** : son meilleur temps est sa part de la SOB
  (la diagonale de la grille des enchaînements) ;
* une paire plus éloignée = **un enchaînement** : son record (les cases au-dessus de la
  diagonale). Chaque segment traversé pendant un enchaînement étant aussi un temps de
  segment, un enchaînement ne peut jamais battre la somme de ses meilleurs, au mieux
  l'égaler (case « OR ») ;
* (départ, arrivée) = la course entière. Les courses complètes (Partie, En ligne) sont aussi
  gardées à part (les 20 dernières, et la meilleure : le PB personnel avec ses temps de
  passage).

La **route** (l'ordre des points de passage) vient du niveau (`setRoute`, calculée par les
écrans d'entraînement) ; tant qu'elle n'est pas donnée, elle est apprise de la première
course complète. La SOB est la somme des meilleurs des segments de cette route ; elle est
inconnue (−1) tant qu'un segment n'a jamais été traversé. Ses changements sont notés jour
par jour (`sobHistory`, 60 points au plus, avec le PB du moment).

**Tout reste personnel** : rien de ce magasin n'est envoyé ; les records en ligne et les
records du monde ne comptent que les courses complètes vérifiées par le serveur, comme
avant.

#### Reprise des anciennes données

Au premier lancement, `saves/segments.json` (segments d'or des courses complètes, 2.30 à
2.90) et `saves/training.json` (meilleurs temps d'entraînement) sont **lus une fois** et
fusionnés ; leurs replays (`ghosts/golds`, `ghosts/training`) sont copiés dans
`ghosts/bests`, et les anciens dossiers supprimés seulement une fois le nouveau fichier écrit
(les deux anciens fichiers restent tels quels sur le disque). Un segment d'or dont les
points de passage n'étaient pas connus attend que la route du niveau le dise. Puis les
meilleurs temps de segments des statistiques (`stats.json`, qui voyage avec la copie de la
progression) sont fusionnés une fois, et à nouveau après chaque restauration d'une copie
(« Migrer vers un nouveau compte ») : un temps meilleur devient le meilleur (sans replay),
les passages ne diminuent jamais. Un fichier abîmé est mis de côté (`bests.json.corrupt`) ;
un fichier écrit par un jeu plus récent est lu, jamais réécrit.

### Startpos (principe de Geometry Dash)

En entraînement, une **startpos** se pose n'importe où (`game/gameplay/StartPos.h`). Elle
prend Nova telle qu'elle est : position, direction, pouvoirs (dans l'ordre des
emplacements), cœurs (et cœurs bonus), énergie, et les points de passage déjà allumés. Un
essai qui part d'elle remet le niveau comme au début (ennemis, mécanismes), place Nova ainsi
(départ arrêté : recharges et élan remis à zéro ; c'est aussi son point de réapparition) et
rallume en silence les points déjà allumés (sans temps de passage : revenir sur l'un d'eux
ne fait pas partir un segment du mauvais côté).

* **Poser, choisir passent par les entrées** : `InputFrame::PlaceStartPos`,
  `PrevStartPos`, `NextStartPos` (et `PlaceMeasure` pour les points de mesure) sont des
  drapeaux de l'entrée du pas. Après ce pas, le monde lit l'état de Nova
  (`GameWorld::noteMarks`, `takeMarks`) : une lecture, jamais une partie de la simulation ni
  de son empreinte. Une course joue donc exactement pareil avec ou sans eux (même empreinte
  finale), et son replay, qui garde l'octet des drapeaux tel quel, rend les mêmes marques aux
  mêmes pas (test `startpos: placed through the inputs…`).
* **Un essai depuis une startpos** a pour départ `SectionTrainingStart::startPos` (le même
  départ que l'entraînement d'un segment, appliqué avant le premier pas par la course et par
  son replay) : son replay se rejoue à l'identique avec ce départ (test `startpos: an attempt
  from it…`).
* Aucune règle du déroulement d'une partie ne change (les courses existantes, leurs replays
  et leurs empreintes restent identiques) : `kSimulationVersion`, `kRankingGeneration` et les
  empreintes de référence ne bougent pas.
* Jamais une partie complète, ni un record, ni envoyée en ligne.
* **Rémanence** : les startpos restent d'une session à l'autre, par version du niveau
  (`StartPosStore`, `saves/startpos.json`, 32 par version), rangées dans l'ordre de la route
  (« SP 1 », « SP 2 »…, Précédente / Suivante en boucle). Une autre version du niveau a sa
  propre liste.

### Points de mesure

Deux points de passage fictifs **A** puis **B** (`game/ghost/MeasurePoints.h`) : franchir A
lance un chrono, franchir B l'arrête. Une porte est verticale (de 12 m au-dessus des pieds de
Nova quand elle a été posée à 2 m en dessous ; **1 m = 1 tuile = 16 pixels**) et se franchit
dans les deux sens ; le moment exact est interpolé entre deux positions (même temps à 120 Hz
en jeu qu'à 30 Hz sur la trace d'un replay). Repasser A relance le chrono ; un saut de plus de
20 m entre deux positions (réapparition, saut dans un replay) l'arrête.

* `MeasureTimer` chronomètre un flux de positions (`feed(t, pieds)` rend le temps A → B
  quand B est franchi), `measureTimes` toute une trace : en entraînement et dans **tous les
  replays** (les vôtres et ceux en ligne).
* `MeasureStore` (`saves/measures.json`, par version du niveau, 16 paires au plus) garde les
  paires, leur nom, le meilleur temps de l'entraînement et les 20 derniers. Déplacer A ou B
  oublie ses temps (ils ne se comparent plus).
* Un temps A → B reste personnel : il ne touche ni la SOB ni aucun record.

## Lecteur de replay avancé (2.19)

*Voir le replay*, *Revoir la partie*, `CosmicPlatformer --replay fichier.cprz` : un replay exact
(format 2, même version du niveau) est **re-simulé** (voir `docs/online/REPLAY_FORMAT.md`).

* **Lecture / pause**, **image par image** en avant et en arrière (une image = un pas de
  simulation, 1/120 s), **vitesses** ×0,25, ×0,5, ×1, ×2, ×4.
* **Barre de temps** : un clic ou un glissement va à n'importe quel instant (le replay saute
  quand on relâche le bouton) ; repères des points de sauvegarde au-dessus.
* **Sauts** au point de sauvegarde précédent / suivant (et à l'arrivée), ±5 s, début, fin.
* **Chronomètre exact** (celui du niveau, à la milliseconde), **temps de passage** de chaque
  point (temps cumulé et durée du segment ; un clic y va), **entrées du joueur** du pas en
  cours (touches maintenues), HUD du jeu (vie, énergie, pouvoirs, objets, boss).
* **Aller en arrière sans jamais fausser l'image** : la simulation ne sait qu'avancer et un
  monde ne se copie pas. Le lecteur garde donc des **points de reprise** : trois autres
  copies du niveau rejouées avec les mêmes entrées, à environ 0,75 s, 3 s et 12 s derrière
  l'instant montré (entretenues pendant la lecture). Un retour en arrière repart de la plus
  proche (quelques dizaines de pas à simuler : immédiat) ; sans point de reprise, le niveau
  est rechargé et rejoué depuis le début (« Recherche de l'instant… », quelques milliers de
  pas par image). Dans tous les cas, l'état montré est celui de la simulation à ce pas :
  le même qu'en lecture continue (test `replay: a jump in time gives the same state as
  playing continuously`). Un saut vers un temps va au premier pas où le chronomètre
  l'atteint (le chronomètre s'arrête pendant l'intro et les arrêts sur image).
* Un replay ancien (format 1) ou désynchronisé montre sa trajectoire enregistrée, sans les
  entrées.

| Action | Clavier | Manette | Souris |
|---|---|---|---|
| Lecture / pause | K (ou Entrée / Espace sur « Lecture ») | X (ou A sur « Lecture ») | bouton |
| Image précédente / suivante | `,` / `.` (`;` en AZERTY) | LT / RT (maintenus : répétés) | boutons « ‹ Image » / « Image › » |
| −5 s / +5 s | J / L | stick droit ← / → | boutons |
| Point précédent / suivant | Q / E (Page préc. / Page suiv.) | LB / RB | boutons, lignes des temps de passage |
| Vitesse | − / + | bouton « Vitesse » | bouton |
| Début / fin | Origine / Fin | bouton « Début » | barre de temps |
| Retour | Échap | B | bouton |

Les flèches (croix directionnelle, stick gauche) déplacent le choix entre les boutons, comme
dans tous les menus.

## Comparer deux replays (2.19)

Deux courses du même niveau côte à côte, pour voir où l'une gagne du temps sur l'autre.

### Accès et choix des replays

* Lecteur de replay → **Comparer…** (le replay en cours est A) ;
* écran **Segments** de fin de course → **Comparer des replays…** (A : cette course, B : mon
  record, ou celui qu'elle vient de battre) ;
* écran d'avant course des fantômes → **Comparer deux replays…** (les fantômes cochés) ;
* **classement en ligne** (niveau) → **Comparer…** (A : le record choisi, B : le meilleur autre
  record avec replay).

L'écran « Comparer deux replays » propose les mêmes sources que l'avant-course : mes courses
de ce niveau (meilleur temps, record précédent, dernière course), les replays téléchargés
(cache) ou importés, les records en ligne (record mondial, amis, 10 premiers ; téléchargés
quand on les choisit, **avant** la lecture), plus le replay en lecture. Chaque ligne a ses
boutons **A** et **B** ; « Échanger A et B » ; chaque fichier est relu et vérifié (mêmes
vérifications que les fantômes) avant l'ouverture.

### Ce qui est montré (et ce qui est exact)

* **A est rejoué** : re-simulé à l'identique s'il est un replay exact de la version installée
  du niveau (ennemis, HUD et caméra du jeu, contrôles d'empreinte) ; sinon sa trajectoire
  enregistrée.
* **B est un fantôme** : sa trajectoire enregistrée (positions à 30 Hz interpolées, comme en
  Ghost Racing), sur le chronomètre de A. Re-simuler deux mondes à la fois n'apporterait rien
  de plus exact pour B : sa trajectoire *est* ce que son monde a produit.
* Leurs **trajectoires** sont tracées (toute la course en léger, les 2 dernières secondes en
  vif, les points de sauvegarde passés entourés) ; « Tracés » les masque.
* **Caméra** : « Suivre A » (la caméra du jeu en re-simulation), « Suivre B », « Les deux »
  (cadre les deux, en dézoomant si besoin, dans la partie de l'écran que les panneaux
  laissent libre).

### Décalage de B

Le décalage **d** déplace B sur le chronomètre de A : à l'instant t de A, B est montré où il
était à t − d. « décalage +1,250 s » : B est montré 1,25 s plus tard qu'il n'a couru ;
« décalage -0,500 s » : 0,5 s plus tôt.

* Réglage fin ±10 ms, ±100 ms, gros ±1 s, **saisie directe** (« +1,250 », « -0,5 s »,
  « 250 ms », « 1:02,5 »), **Zéro** ;
* **Recaler B** sur un point de sauvegarde choisi (ou l'arrivée) : d = (passage de A) −
  (passage de B) à ce point, à la milliseconde. B y passe en même temps que A (écart 0,000).
* Le décalage est borné (± la somme des deux durées). Le chronomètre de la comparaison
  continue après l'arrivée de A tant que B (décalé) n'a pas fini.

**Tous les écarts affichés tiennent compte du décalage** et le disent : « décalage +1,250 s
compris », « B recalé sur Grand saut ». Le décalage ne change pas les durées des segments
(ce que chaque course a mis), seulement les passages de B sur l'horloge de A et les écarts.

### Écart et tableau

* **Écart A − B en direct** (au-dessus de la course depuis la 2.91, avec sa courbe en bas :
  voir « Écart à la référence » plus bas) : le temps de A pour atteindre le point le plus
  avancé de la route qu'il a atteint, moins celui de B (décalage compris) — l'estimation du
  Ghost Racing ; exact aux points de passage et quand les deux ont fini. Positif : A en
  retard ; négatif : A en avance.
* **Tableau segment par segment** (points appariés par position, comme en Ghost Racing) :
  durée de A, durée de B, **A − B** sur le segment, **écart** au point (A − (B + d)) ; la
  durée la plus rapide des deux est en **or** ; la ligne du point de recalage est marquée.
  Un point que B n'a pas passé laisse « — » (et la durée suivante de B, qui couvrirait deux
  segments, n'est pas comparée). Ligne **Total** : les deux temps, leur différence, l'écart
  final. Un clic sur une ligne va au passage de A à ce point.

### Versions du niveau

Deux replays de versions différentes du niveau restent comparables, marqués « **autre
version : comparaison non officielle** ». Un replay A d'une autre version que celle
installée ne peut pas être re-simulé : sa trajectoire est montrée.

### Lecture seule

Aucun record, aucune progression, aucun succès, rien n'est envoyé au serveur : la
comparaison ne fait que lire. Les replays en ligne sont téléchargés (dans le cache des
fantômes) avant la lecture ; aucune requête réseau pendant la lecture.

| Action (comparaison) | Clavier | Manette |
|---|---|---|
| Caméra A / B / les deux | C | Y |
| Recaler B sur le point choisi | R | bouton « Recaler B » |
| Afficher / masquer les tracés | T | bouton « Tracés » |
| Décalage, saisie, points | boutons du panneau (flèches + Entrée) | croix + A |

Code : `game/ghost/ReplayCompare.*` (décalage, recalage, tableau, écart),
`game/ghost/ReplayPlayback.*` (re-simulation avec sauts et points de reprise),
`ui/screens/ReplayScene.cpp` (lecteur et comparaison), `ui/screens/ReplayCompareSetupScene.cpp`
(choix de A et B). Tests : `tests/test_replay_compare.cpp`.

## Écart à la référence et mesures dans les replays (2.91)

Le lecteur de replay et la comparaison montrent **l'écart à une référence** tout au long de la
course : en direct au-dessus de la course, et en **courbe** sur toute la durée du replay en bas
de l'écran.

### Référence

* **Lecteur** : au choix (boutons au-dessus de la courbe, clavier, manette, souris)
  * **Record (PB)** : votre meilleur temps de **cette version** du niveau
    (`ghosts/local/<niveau>-<version>/pb.cprz`) ; si le replay regardé *est* ce record (« écart
    nul »), la somme des meilleurs est choisie d'elle-même quand elle existe ;
  * **Somme des meilleurs** : la SOB unique de `PersonalBests` (§ « Une seule SOB ») : chaque
    meilleur segment, quelle que soit sa source, lu sur le replay exact qui le tient, mis bout à
    bout. La courbe est **continue seulement si chaque meilleur segment a son replay** sur cet
    ordinateur ; sinon l'écart n'est comparé **qu'aux points de passage** (somme des meilleurs
    segments jusque-là), et l'écran le dit (« 3 segments sur 5 ont leur replay : comparé aux
    points de passage seulement ») ;
  * **Autre replay…** : l'écran « Comparer deux replays » choisit B (le replay regardé reste A) ;
    B devient la référence (choisir un autre A ouvre la comparaison complète).
* **Comparaison** : la référence est B, **décalage compris** (et dit : « décalage de B :
  +1,250 s, compris dans la courbe »).

La référence est dessinée en fantôme dans le lecteur (« Record (PB) · 22/11 »), sur le
chronomètre du replay ; celle de la somme des meilleurs passe d'un replay à l'autre à chaque
point de passage.

### Ce qui est mesuré (et ce qui est exact)

Tout vient de ce que les replays ont enregistré : leurs **trajectoires** (positions des pieds à
30 Hz) et leurs **temps de passage** (à la milliseconde) ; jamais une valeur inventée pour
l'affichage (`game/ghost/ReplayGap.*`).

* **Le trajet le long de la route** (`RouteRun`) : chaque position est projetée sur la route du
  niveau (celle de la barre de progression), sur ses segments et non seulement sur ses points ;
  on garde la distance la plus avancée atteinte (une chute en arrière ne la fait pas reculer),
  les positions loin de la route (plus de 200 px : chute, détour) ne comptent pas, la sortie est
  atteinte au temps final.
* **En temps** : le temps qu'il a fallu au replay pour atteindre le point le plus avancé de la
  route, moins celui de la référence (décalage compris) — l'estimation du Ghost Racing.
  **Exact** aux points de passage (les deux temps de passage, appariés par position, ou par
  identifiant de point pour la somme des meilleurs) et à l'arrivée. « +0,846 s » : en retard.
* **En distance** : où chacun se trouve sur la route à cet instant ; « 6,8 m derrière ».
  **1 m = 1 tuile = 16 pixels** (une convention : le jeu n'a pas d'unité de longueur ; dit à
  l'écran). Dans la courbe, « en retard » est toujours **au-dessus de zéro** (en distance :
  des mètres négatifs, derrière).
* La courbe a un point tous les 1/30 s au plus (3 000 en tout) plus chaque point de passage et
  l'arrivée ; elle est calculée une fois, puis gardée tant que la référence ou le décalage ne
  changent pas. Les replays de la référence sont lus et placés sur la route **hors du fil
  principal**.

### À l'écran

* **En direct** (en haut, au centre de la partie libre, sous la note d'administration, jamais
  sur la vie, les pouvoirs ou les objets : ceux-ci passent dessous) : « ÉCART AU RECORD (PB), EN
  DIRECT », « +0,846 s · 6,8 m derrière », « en retard sur le record à cet instant » ; aux
  points de passage seulement : l'écart au dernier passé (« … à Racines »). En retard / en
  avance est toujours **écrit**, jamais dit par la couleur seule.
* **La courbe** (en bas) **est la barre de temps** : un clic ou un glissement va à cet instant ;
  le curseur suit la lecture, une bulle donne l'écart (en temps et en distance) à l'instant
  montré ou sous la souris ; les points de passage sont en pointillés et nommés (chaque nom
  dans la place avant le suivant) ; en comparaison, les passages de B (sur l'horloge de A) sont
  marqués dessous. Rouge au-dessus de zéro : en retard ; bleu dessous : en avance. Boutons « En
  temps » / « En distance ».
* **Temps de passage** (lecteur) : une colonne « Écart » donne l'écart exact à chaque point
  passé.

### Points de mesure dans les replays

Les mêmes portes A / B qu'en entraînement (§ « Points de mesure », `MeasureStore`, par version
du niveau) sont chronométrées dans **tous les replays**, les vôtres et ceux en ligne, quand
« Mesurer aussi dans les replays » est activé ; leurs portes se dessinent dans le niveau
(« Montrer les portes de mesure »).

* **Lecteur** : « MESURE A1 → B1 · nom » : le temps de **ce replay** (« en cours · 2,734 s »
  entre A et B), **votre meilleur** (entraînement) et **le PB** sur A → B (lu sur la trajectoire
  du record).
* **Comparaison** : **A − B** sur A → B, **A** (rejoué : exact), **B** (sa trajectoire
  enregistrée, « à 0,03 s près », dit comme tel) et votre meilleur.
* **Exact ou à 0,03 s près** : un replay exact qu'on lit en continu à travers A et B est
  chronométré **sur la simulation** (1/120 s, interpolé) ; sinon (saut dans le temps, ancien
  replay, B) sur la trajectoire enregistrée (30 Hz, interpolée). Chaque valeur dit laquelle.
* Le passage de A entre les deux portes est **grisé en or** sur la courbe (« entre A1 et B1 »).
* **F8** (manette : **Vue + ↑**) pose une porte aux pieds de A, comme en entraînement (A, puis
  B de la même paire) ; ‹ › passent d'une paire à l'autre.
* **Lecture seule** : un temps mesuré dans un replay n'est jamais écrit (ni meilleur temps de la
  paire, ni statistiques, ni SOB, ni record) ; seule la porte posée est gardée.

| Action (écart, mesures) | Clavier | Manette | Souris |
|---|---|---|---|
| Référence, temps / distance | flèches + Entrée sur les boutons | croix + A | boutons |
| Aller à un instant | J / L, Q / E… (lecteur) | (lecteur) | clic ou glissement dans la courbe |
| Poser une porte de mesure | F8 | Vue + ↑ (Vue seule : infos) | — |
| Paire de mesure montrée | flèches + Entrée sur ‹ › | croix + A | ‹ › |

Code : `game/ghost/ReplayGap.*` (trajet sur la route, écart en temps et en distance, somme des
meilleurs bout à bout, passages A → B), `ui/screens/ReplayGapSources.*` (références lues hors
du fil principal), `ui/screens/ReplayGapView.*` (la courbe), `ui/screens/ReplayScene.cpp`.
Tests : `tests/test_replay_gap.cpp`.

## Succès

<p align="center"><img src="media/achievements.png" width="820" alt="Succès : la toile des succès par catégorie et la liste d'une catégorie"></p>

218 succès en 13 catégories (`data/achievements/achievements.json`), chacun avec 1 à 5 étoiles
de difficulté (597 étoiles). Ils se débloquent d'après les vraies sauvegardes
(progression, records, statistiques des boss) — y compris rétroactivement — et des compteurs
de partie gardés dans `saves/achievements.json` (ennemis vaincus, sauts, chutes, pouvoirs
utilisés, segments en or…). Certains succès en suivent d'autres (les fils de la toile) : un
succès ne se débloque qu'une fois les précédents obtenus.

* Les succès « niveau » (sans mourir, sans dégât, tout ramasser, tous les secrets, tous les
  ennemis) comptent les niveaux de la **campagne** joués depuis le départ. « Tout ramasser »
  et « tous les secrets » ne comptent que les 13 niveaux qui ont des éclats, des reliques ou
  des secrets (ni les niveaux des gardiens, ni le Noyau) ; certains se limitent à une planète
  (Aethra).
* Les pouvoirs passifs (Triple saut, Ruée aérienne, Aimant) se comptent en niveaux terminés
  en les possédant ; le Planeur à chaque ouverture.
* Depuis la 2.24 : les ennemis vaincus par famille (créatures terrestres, robots, créatures
  volantes, ennemis lourds, rapides, tourelles, utilisateurs de pouvoirs), l'or et le platine
  sur tous les niveaux d'une planète (Aethra), chaque phase d'un gardien d'Aethra battue
  sans être touché (pas forcément dans le même combat), les replays comparés et recalés
  (lecteur de replays), et la catégorie **Collection** : récompenses équipées, tenue complète,
  morceaux du juke-box entendus, créatures du bestiaire rencontrées, images des planètes
  enregistrées depuis la Galerie, musique du menu choisie.
* Les succès de la catégorie Communauté demandent Cosmic Online (commentaires publiés, niveaux
  notés : comptés quand le serveur les a acceptés) ; aucun n'est nécessaire au reste du jeu.

**Profil en ligne** : quand vous êtes connecté, la liste des succès débloqués est envoyée
(`PUT /api/v1/players/me/achievements`). Le serveur ne garde que les identifiants qu'il
connaît, recompte lui-même les étoiles avec sa copie du catalogue
(`server/src/achievementCatalog.json`, régénérée par `node scripts/achievements-catalog.mjs`)
et les affiche sur le profil comme **déclarées par le jeu, non vérifiées**.

## Mode concentré

Un HUD réduit : par défaut la vie, l'énergie, les pouvoirs et la vie du boss. Les éléments
gardés se choisissent dans l'éditeur du HUD (onglet « Mode concentré ») ; places et tailles
sont communes aux deux modes (deux éléments ne se chevauchent dans aucun des deux). Les
**messages du niveau** (indications, titres de pouvoirs et de boss) s'affichent ou non dans
chaque mode.

---

[← Retour à l'accueil (README)](../README.md)
