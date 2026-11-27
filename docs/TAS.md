[← Retour à l'accueil (README)](../README.md)

# Courses de la machine (TAS)

Pour chaque niveau de la campagne et chaque catégorie des classements (ANY%, 100 %, Sans mort,
Sans dégât), une **course calculée par la machine** : un programme joue le moteur du jeu, qui est
déterministe, et garde la course la plus rapide qu'il trouve. C'est le **meilleur temps trouvé par
la machine**, jamais un optimum prouvé : une recherche plus longue, ou meilleure, peut trouver
plus vite.

* Un seul algorithme, le même pour tous les niveaux et toutes les catégories : aucun niveau n'est
  scripté, rien dans la recherche ne connaît un niveau.
* La recherche tourne **hors ligne**, sur nos machines ou sur l'ordinateur d'un administrateur
  (`cosmic_tas`, installé à côté du jeu et lancé par ADMINISTRATION › TAS), jamais d'elle-même
  sur l'ordinateur d'un joueur, jamais sur Cosmic Online.
* Le résultat est un **replay ordinaire**, au format du jeu, que le vrai moteur rejoue exactement.
* Il est rangé avec les niveaux (`data/tas/`), ou publié en ligne par un administrateur et
  **rejoué par chaque jeu** avant d'être montré ; il n'entre **jamais** dans les records ni les
  classements officiels.

## Les fichiers

Un fichier par niveau et par catégorie (`categoryId()` : `ANY_PERCENT`, `HUNDRED_PERCENT`,
`NO_DEATH`, `NO_DAMAGE`), son replay à côté :

```
data/tas/<niveau>/<CATÉGORIE>.json
data/tas/<niveau>/<CATÉGORIE>.cprz
```

```json
{
 "format": "cosmic.tas",
 "level": "sylvanox_01",
 "levelVersion": 1,
 "levelHash": "<64 hex : l'empreinte des classements, fingerprintRankedLevel>",
 "simVersion": 19,
 "category": "ANY_PERCENT",
 "timeMs": 41250,
 "splits": [{"checkpoint": "o63", "timeMs": 8125}],
 "explored": 1843200,
 "computedAt": "2026-11-27",
 "tool": "cosmic_tas 1 / 2.92.0 / sim 19",
 "routeIntervalMs": 250,
 "route": [[144.0, 928.0], [171.5, 928.0]],
 "deaths": 0, "damageTaken": 0, "collectibles": 12, "totalCollectibles": 75,
 "replay": "ANY_PERCENT.cprz"
}
```

| Champ | Sens |
|---|---|
| `levelHash` | l'empreinte du niveau que les classements utilisent pour cette version (`LevelFingerprints`) |
| `simVersion` | `kSimulationVersion` du jeu qui a calculé la course |
| `timeMs` | le chronomètre du niveau à la sortie, compté comme un record (`llround(temps × 1000)`) |
| `splits` | chaque point de passage atteint (son objet) et le chronomètre à ce moment |
| `explored` | les essais simulés : chaque suite d'entrées essayée depuis un état sauvegardé |
| `route` | les pieds de Nova toutes les 0,25 s du chronomètre, pour dessiner le tracé |
| `deaths`, `damageTaken`, `collectibles` | pour la fiche de la course |
| `replay` | le nom du replay, dans le même dossier (format CPRZ du jeu, `game/online/Replay.h`) |
| `guidedBy` | (2.99, absent sans guide) les courses de joueurs qui ont guidé la recherche : `player`, `timeMs` (son temps quand elle finit la catégorie, sinon 0), `replayId` ; voir « Les guides » |

Lecture (`game/tas/TasFile.h`) : `tasFor(content, fingerprints, niveau, catégorie)` rend la
course **seulement** si son fichier existe et se lit, si son `levelHash` est l'empreinte du niveau
aujourd'hui, si son `simVersion` est celle du jeu et si son replay est là. Sinon rien : **la course
reste cachée jusqu'à son nouveau calcul** (la règle du joueur). `tasForLevel` donne toutes les
catégories visibles d'un niveau, `loadTasReplay` son replay (à rejouer avec `ReplaySimulator` sur
un monde qui vient de charger le niveau, comme tout replay). Le libellé honnête de tous les écrans
est `tasTimeLabel()` : « Meilleur temps trouvé par la machine ».

## Lancer une recherche

```
cmake --build build --target cosmic_tas
./build/bin/cosmic_tas sylvanox_01 ANY_PERCENT --budget 20
./build/bin/cosmic_tas sylvanox_01 NO_DAMAGE --budget 60 --seed-replay ma_course.cprz
```

| Option | Rôle |
|---|---|
| `--budget <minutes>` | temps de recherche (30 par défaut) |
| `--threads <n>` | fils de recherche (2 par défaut) |
| `--beam <n>` | états gardés par fil et par génération (40 ; le faisceau s'élargit quand il piétine) |
| `--seed <n>` | les choix aléatoires de la recherche |
| `--seed-replay <fichier>` | une course de départ (replay exact de ce niveau) : la recherche part d'elle et l'améliore |
| `--keep-seed-sections <n>` | les `n` premières sections de la course de départ gardées telles quelles (mise au point) |
| `--save-partial <fichier>` | la course après chaque section, en replay de ses entrées (pour repartir de là avec `--seed-replay`) |
| `--out <dossier>` | ailleurs que `data/tas/` |
| `--probe <repère> <y>` | les distances vers un repère le long d'une ligne du niveau (vérifier le parcours), rien d'autre |
| `--until-stopped` | un entraînement : des tours de recherche, chacun parti de la meilleure course, jusqu'à l'arrêt (sans budget) |
| `--round <minutes>` | la durée d'un tour (10 par défaut) : sa meilleure course est vérifiée et écrite à la fin de chacun ; sans course encore, le tour suivant repart des sections atteintes, deux fois plus long (60 min au plus) |
| `--progress <fichier>` | la progression, réécrite (atomiquement) toutes les demi-secondes : `game/tas/TasProgress.h` |
| `--stop-file <fichier>` | s'arrêter proprement dès que ce fichier existe |
| `--stop-on-stdin` | s'arrêter proprement quand l'entrée standard lit `stop` ou se ferme (le jeu fermé, même par un plantage) |
| `--user-dir <dossier>`, `--server <url>` | le niveau tel que le jeu le joue sur cet ordinateur : les révisions en ligne (campagne, entités) que le jeu a téléchargées |
| `--guide <replay> [--guide-name <joueur>]` | (2.99, plusieurs fois, 8 au plus) une course d'un joueur qui guide la recherche : un replay exact de **cette version** du niveau et de la simulation ; d'une autre version du même niveau, son **tracé** seulement (jamais ses entrées) ; d'un autre niveau, refusé (« Guide refusé (…) : … ») ; voir « Les guides » |

La course trouvée est **vérifiée** avant d'être écrite ; un fichier qui garde une course plus
rapide (ou égale) du même niveau et de la même simulation n'est pas remplacé. Arrêtée (fichier ou
entrée standard), la recherche finit avec la meilleure course qu'elle tient, la vérifie, l'écrit si
elle est plus rapide, puis s'arrête : quelques secondes.

Le fichier de progression (`--progress`) :

```json
{"format": "cosmic.tas.progress", "phase": "searching", "level": "sylvanox_01", "category": "ANY_PERCENT",
 "elapsedMs": 61250, "explored": 30144, "seedMs": 86692, "bestMs": 86650, "candidateMs": 86601,
 "improvements": 1, "round": 2, "threads": 4, "sequence": 123, "message": "…"}
```

2.99 : `bestSplits` (les temps de `bestMs` à chaque point de passage), `bestSource`, `verifying` (un
candidat en vérification), `guides` (les courses de joueurs qui la guident : `player`, `timeMs`,
`splits`). `phase` : `starting`, `searching`, `verifying`, `stopped`, `failed` ; `bestMs` : la meilleure course
vérifiée de la session (celle de départ ou une plus rapide écrite) ; `candidateMs` : une course plus
rapide que la recherche tient, pas encore vérifiée ; `improvements` : les courses plus rapides que
celle de départ, vérifiées et écrites.

## La recherche

`tools/tas/` : `Course` (ce qu'une catégorie demande dans un niveau), `Search` (la recherche),
`Output` (l'enregistrement et la vérification).

**Le parcours, depuis la géométrie du niveau.** Les points de passage dans l'ordre de la route,
puis la sortie ; en 100 %, chaque éclat et chaque relique aussi, chacun dans la section où il fait
le plus court détour, l'ordre dans chaque section cherché (plus proche voisin puis 2-opt sur les
distances du niveau). Les distances viennent du modèle de déplacement de l'analyse des niveaux
(`LevelNavigator`, `game/levels/LevelAnalyzer.h` : la grille de 8 px, tomber partout, monter et
aller de côté à portée d'un appui, tous les pouvoirs, les portes ouvertes) : un champ de distance
vers chaque repère. La navigation des joueurs simulés des raids (`game/raid/nav`) n'a pas été
reprise : elle est faite pour les zones d'un raid et des cibles qui bougent, la recherche n'a
besoin que d'une distance le long du niveau, partout.

**La progression d'un état** : la distance le long des repères déjà passés moins la distance des
pieds de Nova au suivant, plus ce que le monde a fait en route, lu de façon générique : portes et
leviers ouverts, récepteurs allumés, arènes vidées, ennemis battus (`LevelEntity::keepState`),
barrières de pouvoir brisées, pouvoirs portés, phases et blessures des boss, cœurs restants. Une
arène (une zone de combat dont les portes se ferment jusqu'à la fin de ses vagues) compte ses
ennemis restants de la même façon avant et après son début : y entrer ne coûte rien, la commencer
est un pas en avant ; pendant son combat, la progression est mesurée depuis son centre et aller
vers ses ennemis compte.

**Section par section.** De point de passage en point de passage (puis jusqu'à la sortie) ; chaque
fil part du même état avec son propre faisceau et ses propres tirages, la meilleure arrivée de
tous (la plus tôt, un cœur perdu comptant pour du temps perdu) devient le départ de la section
suivante. Chaque section garde sa meilleure arrivée pour chaque jeu de pouvoirs portés : quand une
section ne peut pas être finie, la recherche revient à la section d'avant et prend son arrivée
suivante (quelques fois au plus). Après chaque section, la recherche vérifie que le monde est le
même dans tous les fils.

**Un faisceau synchrone.** Tous les états d'une génération ont joué le même nombre de pas ; ils
sont classés par leur progression et gardés variés (un par endroit et par situation). Quand la
progression piétine, le faisceau s'élargit, les coups s'allongent et on en essaie davantage ; ils
reviennent à la normale dès qu'elle repart. **Une mort n'est jamais gardée**, quelle que soit la
catégorie : le faisceau continue avec les états en vie, et s'il n'en reste aucun (une impasse prise
trop vite, par exemple sur des plateformes mobiles au-dessus de l'acide), la section recommence,
avec un faisceau plus large.

**Les coups** : quelques pas d'entrées d'un joueur — une direction, un saut tenu un temps, un
second saut, une attaque, une interaction, la touche d'un pouvoir porté, Bas ou Haut — et, quand
un pouvoir est proposé, chaque réponse (le laisser, ou le mettre à la place de chaque
emplacement). Les choix passent par les entrées (`InputFrame`), comme ceux d'un joueur. Une
cinématique qu'on peut passer est passée comme un joueur la passe : la touche tenue 0,8 s.

**Les règles des catégories sont des contraintes** : Sans mort et Sans dégât rejettent l'essai à
la première mort ou au premier coup reçu ; 100 % suit son ordre de ramassage et compte chaque
collectible, et une course qui n'a pas tout ramassé à la sortie est refusée.

**Affinage.** La course trouvée est rejouée avec un changement d'entrée (l'appui ou le relâchement
d'une touche : saut, Dash, pouvoir, attaque, direction) avancé ou retardé d'un ou deux pas, ou un
appui supprimé ; un changement est gardé quand la course finit encore la catégorie, plus tôt (ou
aussi tôt avec moins d'entrées). Les états sauvegardés tous les 2 s de la course évitent de la
rejouer depuis le début.

**Course de départ** (`--seed-replay`) : jouée d'abord ; si elle finit la catégorie, c'est la
course à battre (la « meilleure course ») et elle guide toute la recherche (ci-dessous) ;
l'affinage part d'elle si rien n'est plus rapide.

### La meilleure course guide la recherche

* **Ses images, à chaque section, même après une divergence.** Chaque section est proposée aux
  faisceaux avec les images de la meilleure course pour cette section : par l'index de la section
  et, quand la course trouvée l'a quittée, aussi depuis l'instant où Nova y était la plus proche de
  son état présent (position et vitesse, à ± 1 s). Elles sont jouées depuis l'état où la recherche
  se trouve (l'appui de la jointure recalculé comme un joueur appuierait) ; leurs états rejoignent
  le faisceau à chaque génération (le faisceau peut en partir), et leur arrivée est une arrivée
  comme les autres. Avant, elles n'étaient proposées que tant que la course restait identique à
  la meilleure : à la première divergence, les sections suivantes étaient cherchées à l'aveugle.
* **Les raccords.** À la fin de chaque section, les meilleures arrivées (et celles de la meilleure
  course) sont jouées jusqu'à la sortie **avec la suite de la meilleure course**, reprise à
  quelques endroits proches (là où Nova y était la plus proche, et sa propre arrivée à ± 2 pas) :
  un raccord qui finit plus vite est une course entière plus rapide, aussitôt (rejouée depuis le
  départ avant d'être gardée, puis vérifiée par la lecture normale des replays comme toute course).
* **Le gagnant d'une section** est l'arrivée qui donne **la meilleure fin mesurée** (par son
  raccord) ; seulement quand aucune ne se raccorde, la meilleure fin estimée (sa propre arrivée plus
  le reste de la meilleure course depuis la sienne, un cœur perdu compté en temps, une marge de
  0,25 s). Une arrivée plus tôt mais dans un plus mauvais état ne passe plus devant : sur
  `sylvanox_01`, des arrivées de plus d'une seconde en avance au premier point de passage ne se
  raccordent jamais (elles ont laissé derrière elles ce dont la suite a besoin), et la recherche
  d'avant s'y perdait jusqu'à la fin de son budget.

### Les tours

Un entraînement (`--until-stopped`) cherche par **tours** (`--round`, 10 min dans le jeu). Le
**premier tour** cherche tout le niveau, section par section, guidé comme ci-dessus. Dès le
**deuxième** (avec une course vérifiée), un tour cherche une **fenêtre** d'une ou deux sections
consécutives (une seule pour un niveau d'une ou deux sections) :

* les sections de la meilleure course **avant** la fenêtre sont **gardées exactement** (l'affinage
  n'y touche pas non plus) ; **après** la fenêtre, la course **rejoint la meilleure course** par
  ses raccords (si aucun ne finit, la recherche continue section par section, guidée, jusqu'à un
  raccord ou la sortie) ;
* la fenêtre est cherchée **en plusieurs passes**, de plus en plus profondes (faisceau ×1 à ×4,
  plus de coups par état, plus de générations après la première arrivée) ; sur une fenêtre de deux
  sections, une passe sur deux part de l'arrivée la plus tôt de la première section (son gain peut
  n'apparaître qu'avec la section suivante cherchée) ; le dernier sixième du tour est l'affinage, à
  partir du début de la fenêtre ;
* **les fenêtres tournent sur tout le niveau** (`WindowPlanner`, gardé tant que le processus vit) :
  d'abord les sections cherchées il y a le plus longtemps, et plus tôt de nouveau celles où un tour
  a gagné du temps (le poids d'un gain diminue de moitié à chaque tour qui n'y gagne rien) ;
* **jamais pire** : seule une course vérifiée plus rapide remplace la meilleure.

La progression (`--progress`) donne la fenêtre du tour (`focusFirst`, `focusLast` ; 0 : tout le
niveau) ; la courbe l'affiche (« Tour 3 · sections 4 à 5 (le reste suit la meilleure course) »).
Pendant une fenêtre, **« Finis » reste bas** : les essais du faisceau s'arrêtent à la fin de la
section cherchée ; ne vont jusqu'à la sortie que les raccords et l'affinage.

**Mesure** (2.98, `sylvanox_01` ANY%, depuis la course livrée 1:26.692, 4 fils, graine 1) : avant,
3 tours de 4 min restent à 1:26.692 (chaque tour recherchait tout le niveau, ne finissait pas la
section 3 dans son budget et rendait la course de départ) ; après, 2 tours de 3 min donnent
1:26.667 (tour 1 : raccords après les sections 1 et 4) puis 1:26.650 (tour 2, fenêtre 1 à 2).

### Les guides (2.99)

Une recherche qui reste près de sa meilleure course ne trouve pas un autre chemin ni une autre
technique : sur `sylvanox_01`, le PB d'un joueur avait plus de 4 s d'avance avant le boss (ses
segments 1 à 5 : 1,8 s, 4,1 s, 0,7 s, 0,7 s et 0,9 s plus rapides ; la machine, 10,6 s plus rapide
sur le boss). **Les rounds reviennent-ils sur les points de passage déjà passés ?** Oui : les
fenêtres tournent sur toutes les sections (`WindowPlanner`). Mais chaque fenêtre cherche **près de
la meilleure course** : un raccourci, un autre chemin, un autre ordre ou un autre usage des
pouvoirs restent hors de portée, sauf si un **guide** les apporte.

* `--guide <replay>` (plusieurs) : des courses de joueurs, chacune un replay exact de cette version
  du niveau (empreinte de classement, `kSimulationVersion`, touches par emplacement, course seule) ;
  sinon refusée avec la raison. Au départ, chacune est **mesurée** : son temps quand elle finit la
  catégorie (vérifiée comme une course de la machine), ses temps à chaque point de passage.
* Comme la meilleure course, chaque guide est **offert à chaque section** (par l'index de la section
  et réaligné sur la position de Nova : `Lead`, une suite d'entrées nommée par sa source, que le
  faisceau suit et dont il part) et chaque arrivée est **raccordée** à la meilleure course **et à
  chaque guide** qui finit (la fin la plus rapide).
* **L'assemblage** (la première chose qu'un tour fait avec des guides, puis à chaque meilleure
  course nouvelle) : depuis le départ, section par section, la voie de chaque source (la meilleure
  course, chaque guide : par l'index de la section et réalignée) est jouée depuis où l'assemblage
  en est ; les arrivées sont classées par la fin qu'elles donnent (raccordées, mesurées) et la
  meilleure est prise : une « somme des meilleurs segments » **qui se joue vraiment** (les états
  aux points de passage diffèrent : vitesse, énergie, santé, pouvoirs ; le raccord autour de chaque
  point de passage les absorbe, et ce qui ne se raccorde pas n'est pas pris). Une course entière
  plus rapide est rejouée depuis le départ puis vérifiée comme toute course ; le tour cherche
  ensuite **à partir d'elle**.
* **Honnêteté** : la course écrite dit qui l'a guidée (`guidedBy` : joueur, temps, replay) ; la
  courbe le dit (« Guidé par la course de NovaRider (1:18.442) ») ; elle reste « meilleur temps
  trouvé par la machine », jamais dans les classements ; aucun guide n'est cru : la course finale
  est vérifiée par la lecture normale des replays.
* **Dans le jeu** : l'entraînement d'une paire reçoit le **PB de cet ordinateur** (la tentative la
  plus rapide de la catégorie sur cette version du niveau dont le replay est gardé, non refusée ;
  « Guide : votre PB (oui / non) » dans les réglages, gardé dans `settings.json`).
* **Le tracé d'une ancienne version.** Une course d'une **autre version** du niveau (ses animations,
  ses cinématiques ont pu changer : ses entrées ne se rejouent plus pareil) n'est **jamais**
  rejouée ni assemblée : `--guide` la prend pour son **tracé** (`RouteHint` : les positions
  enregistrées dans son replay, à sa cadence, et ses temps aux points de passage). À chaque
  génération d'une section, en plus de sa sélection, le faisceau garde quelques états qui suivent
  ce tracé le plus loin (à 24 px d'un de ses points) : la recherche cherche les entrées
  d'aujourd'hui qui prennent le chemin du joueur, avec les règles d'aujourd'hui, et toute course
  est vérifiée comme d'habitude. La liste `routeHints` est générique (d'autres modules peuvent en
  proposer). Dit honnêtement : « Guidé par le tracé de la course de NovaRider (ancienne version du
  niveau) », et ses temps « CP5 : TAS +4,200 s sur NovaRider (ancienne version) » (une partie de
  l'écart peut venir du niveau changé, pas du jeu) ; ils n'entrent pas dans la somme des meilleurs
  segments. Le jeu passe le PB de cette version, sinon celui d'une ancienne version.
* **Un départ, jamais un plafond.** Un temps de joueur à un point de passage est une borne à
  battre : rien ne s'arrête ni ne ralentit quand la machine l'égale ; le tracé n'attire plus une
  section dès que la meilleure course y arrive aussi tôt que lui ; après un gain par un guide, les
  tours continuent comme d'habitude (fenêtres, affinage) ; l'écart passe au vert quand la machine
  passe devant.
* **Ce qui n'est pas encore fait** : les records du monde en ligne et les replays des meilleurs
  segments comme guides (le téléchargement des replays vérifiés existe, le lien avec
  l'entraînement non) ; un choix guide par guide ; les ensembles d'arrivées de Pareto aux points
  de passage (une arrivée plus lente mais plus rapide en vitesse ou en énergie gardée pour la
  suite).

Mesure (2.99, `sylvanox_01` ANY%, 2 fils, depuis la course livrée 1:26.692 avec comme guide une
course 1:26.617 d'un autre entraînement) : 1:26.600 vérifiée et écrite **6 s** après le départ (un
raccord dans le guide après la section 1), 1:26.592 à 24 s, 1:26.567 au deuxième tour (1 min 40).
Avec le record de NovaRider (1:34.392, jeu 2.88, simulation 17 : son tracé seulement), 1 fil : la
recherche arrive au CP1 en 9,667 s (la machine 11,025 s, le joueur 9,225 s), mais l'avance fond
aux CP2 (28,825 s) et CP3 (39,433 s) et ne se raccorde pas à la meilleure course : **continuer
après une arrivée de plusieurs secondes en avance** reste la limite (les éléments du niveau qui
bougent avec le temps ne sont plus au même endroit pour les images de la meilleure course).

### Vérification au fil de l'eau (2.99)

Chaque course entière plus rapide que la recherche tient (raccord, assemblage, course trouvée,
affinage) est confiée **aussitôt** à un fil de vérification (`SearchOptions::onRun`) pendant que la
recherche continue : vérifiée par la lecture normale des replays (ci-dessous), **écrite** si elle
est plus rapide, sinon rien. Une plus lente en attente est oubliée au profit de la plus rapide. La
progression le dit en quelques secondes : `candidateMs` et `verifying` (« candidat 1:26.642 ·
vérification… »), puis `bestMs`, ses temps aux points de passage et l'origine du gain
(`bestSplits`, `bestSource` : `beam` la recherche, `guide`, `splice` l'assemblage, `polish`
l'affinage). Le tour suivant part de la meilleure course vérifiée. Jamais une course non vérifiée
n'est écrite.

## Vérification

Avant d'écrire quoi que ce soit (`tools/tas/Output.cpp`, les étapes 2 et 3 dans
`game/tas/TasVerify.cpp`, que chaque jeu utilise aussi pour une course téléchargée) :

1. les entrées sont jouées dans un monde ordinaire (tas normal, réglages d'une vraie partie),
   enregistrées par l'enregistreur du jeu (`ReplayRecorder`, comme `GameScene` enregistre une
   course officielle), l'outro jouée jusqu'au bout comme un joueur ;
2. le replay obtenu, tel qu'il est écrit, est rejoué par la lecture normale des replays
   (`ReplaySimulator` sur un monde neuf, comme le lecteur de replays) : aucune désynchronisation,
   le niveau terminé, le **temps annoncé** au chronomètre ;
3. la catégorie est vérifiée avec les règles des records (`eligibleCategories`).

Sinon la course est refusée et rien n'est écrit.

## Entraînement depuis ADMINISTRATION

ADMINISTRATION › **TAS** (`ui/screens/AdminTas.cpp`, seulement pour un compte administrateur ; le
serveur vérifie le rôle de la session à chaque requête) liste **chaque niveau livré avec le jeu**
(la campagne dans son ordre, puis les autres ; les raids ne sont pas des niveaux) et **chaque
catégorie** (ANY%, 100 %, Sans mort, Sans dégât), avec :

* la course **livrée** avec le jeu (`data/tas`) ;
* la course **publiée** en ligne pour cette version du niveau, son numéro, sa date et son auteur ;
* le **temps d'entraînement total** et les **essais explorés** de tous les administrateurs et de
  toutes les sessions pour cette version (comptés par le serveur), la session en cours en direct ;
* l'**état** : « Libre », « En entraînement ici » (durée, meilleur temps contre le temps en ligne,
  améliorations), « En entraînement par X » (depuis…), « En file d'attente », « Amélioration
  locale non publiée », « À recalculer (niveau modifié) », ou l'échec du dernier entraînement.

**L'entraînement tourne sur l'ordinateur de l'administrateur**, jamais sur Cloudflare : chaque
paire en entraînement est **un processus `cosmic_tas` enfant du jeu** (`game/tas/TasTraining.cpp`,
`SDL_CreateProcess`, sans fenêtre), lancé avec `--until-stopped`, `--threads` (« Threads par
entraînement »), `--progress`, `--stop-file`, `--stop-on-stdin` et, quand le jeu a déjà une course
valide de la paire pour cette version (la sienne, une téléchargée, la livrée), `--seed-replay` : il
en repart pour l'améliorer ; sinon il part de rien, ou de la course partielle qu'un entraînement
précédent de cette version a laissée (`--save-partial`, `saves/tas_local/.training/`, gardée après
chaque section : une première course plus longue qu'une session n'est pas perdue). Le jeu ne lie
jamais la recherche ni son `operator new`.

* **Plusieurs paires à la fois** : « Entraînements en parallèle » (par défaut : les cœurs de
  l'ordinateur divisés par les threads d'un entraînement, 16 au plus), « Threads par
  entraînement » (2), réglages gardés dans `saves/tas_local/settings.json`.
* **« Tout entraîner »** met en file chaque paire affichée (filtres : catégorie, planète, « En
  cours et en file ») qui n'est ni en cours ni tenue par un autre administrateur ; le jeu en garde
  N en cours et lance la suivante dès qu'une s'arrête. **« Budget par paire »** (30 min par défaut,
  ou « Sans limite ») : une paire arrêtée par son budget repart au bout de la file (chacune son
  tour). La file vit tant que le jeu est ouvert (quitter la page ne l'arrête pas), pas au-delà.
* **Le verrou** : avant de lancer `cosmic_tas`, le jeu prend le verrou de la paire **pour cette
  version du niveau** (`POST /admin/tas/{niveau}/{catégorie}/lease`) ; un battement par minute le
  garde ; trois minutes sans battement et il expire (un ordinateur éteint ou planté libère ses
  paires tout seul). Un autre administrateur voit « En entraînement par X » et ne peut pas la
  lancer ; « Tout entraîner » la passe. Si le battement est refusé (verrou perdu) ou ne passe plus
  depuis près de trois minutes, l'entraînement s'arrête.
* **« Arrêter »** (une paire) ou **« Tout arrêter »** : `cosmic_tas` reçoit l'ordre (fichier
  d'arrêt et fin de son entrée standard), garde sa meilleure course (vérifiée, écrite dans
  `saves/tas_local/`), et le verrou est rendu **dès qu'il a fini** (sa dernière vérification,
  quelques secondes ; 20 s au plus) : sa dernière amélioration part avec le verrou rendu (voir
  « Courbe d'entraînement »), les autres voient la paire libre, et le temps de la session s'ajoute
  au total. Le budget par paire arrête de même. **Fermer le jeu** rend chaque verrou aussitôt
  (avant de quitter ; un plantage ferme l'entrée standard des enfants, qui s'arrêtent seuls ; le
  verrou expire alors en trois minutes).
* **« Publier »** (une paire) et **« Publier toutes les améliorations »** envoient la fiche et le
  replay de `saves/tas_local/` (`POST /admin/tas/{niveau}/{catégorie}/publish`), chacun vérifié par
  le serveur comme une publication seule : format, tailles, niveau et sa révision en ligne,
  catégorie et ses règles, en-tête du replay (niveau, empreinte, simulation, temps), plus rapide que
  la course publiée pour la même version ; le serveur garde aussi qui, quand, et les totaux
  d'entraînement de ce moment. Routes : [API.md](online/API.md) (« Courses de la machine »).

`cosmic_tas` est **installé à côté du jeu** (`cmake/Install.cmake`, ligne à part dans
`installer/CosmicPlatformer.iss` : un installateur sans lui ne se compile pas). Il ne contient aucun
secret et ne parle à aucun serveur ; pour un joueur, il ne sert à rien (la page n'existe que pour un
administrateur).

**Essai des catégories** (2.93, `sylvanox_01`, 5 min et 1 fil chacune, sans course de départ) :
Sans dégât trouve et vérifie une course (1:37.917, 0 mort, 0 dégât) ; 100 % et Sans mort ne
finissent pas le niveau dans ce budget et s'arrêtent proprement avec la raison (« section 2 / 3 non
terminée dans le budget ») sans rien écrire. En entraînement (`--until-stopped`), un tour qui n'a
pas encore de course ne recommence pas de zéro : le suivant repart des sections atteintes (gardées
telles quelles) avec un tour deux fois plus long (60 min au plus), jusqu'à une première course.

## Courbe d'entraînement

Chaque paire (niveau, catégorie) a sa **courbe d'entraînement**, **par version du niveau**
(empreinte de classement et `kSimulationVersion`) : un niveau modifié commence une nouvelle courbe,
les anciennes restent, en lecture seule (rien ne les entraîne plus). Modèle :
`game/tas/TasCurve.h` ; écran : `ui/screens/AdminTasCurve.cpp`.

**Des données réelles, jamais inventées.** `cosmic_tas --progress` écrit, en plus de son état :

* `bestMs`, `bestElapsedMs`, `bestExplored` : le meilleur temps vérifié, le moment de la session où
  il l'a été et les essais simulés alors ;
* `section` / `sections` : la section cherchée (0 : l'affinage d'une course entière) ;
* `focusFirst` / `focusLast` (2.98) : la fenêtre du tour (0 : tout le niveau ; absents des
  fichiers d'avant : tout le niveau) ; `checkpoints` : les points de passage du niveau que les
  fenêtres comptent, CP1 d'abord (l'ordre du parcours, leur nom dans le niveau) ;
* `windows` : les **fenêtres d'essais** (les 24 dernières, les plus anciennes d'abord) : les essais de la
  recherche sont **comptés, jamais listés**, par fenêtre de 5 s (fermée plus tôt quand la section
  ou le tour change) : combien, combien **finissent** (la sortie atteinte, la catégorie gardée) et
  leurs temps **meilleur / médian / pire** (médiane d'un échantillon de 256 temps au plus), et pour
  les autres la **section où ils se sont arrêtés**, combien d'entre eux sont **morts** (ou touchés
  en Sans dégât) et jusqu'où ils sont allés sur le parcours (médiane et maximum, en ‰). Un essai
  du faisceau qui casse la catégorie compte là où son coup commençait ; un essai de l'affinage
  (une course entière rejouée) est mesuré là où il s'arrête. Depuis 2.98, chaque fenêtre garde
  aussi ses **nuages** : **chaque essai fini** (son temps et son instant, 400 au plus par fenêtre ;
  au-delà, un échantillon uniforme où son meilleur est toujours) et, **par point de passage**
  (CP1, CP2… : les points de passage du niveau, comme les temps intermédiaires les numérotent ; un
  essai l'atteint quand il l'allume), combien d'essais l'ont atteint, leurs temps meilleur /
  médian / pire (la médiane d'un échantillon) et un **échantillon** de 32 temps au plus (son
  meilleur toujours). Les fichiers d'avant se lisent toujours (sans nuage).

Le jeu lit ce fichier toutes les demi-secondes (`TasTraining::noteCurve`) :

* un **point** chaque fois que le meilleur temps s'améliore (strictement), placé au **temps
  d'entraînement cumulé** de la version à ce moment (le total que montre la page : celui du serveur
  à son dernier battement, plus le temps depuis) et aux essais cumulés ; le premier, s'il vient de
  la course de départ vérifiée, est marqué « Départ » ;
* chaque **fenêtre d'essais**, placée de même ;
* les **courses publiées** sont des marques de la courbe de leur version (le serveur les donne,
  avec le temps d'entraînement au moment de la publication, la session en cours comprise).

Les nuages sont bornés : 30 000 points d'essais finis et autant de temps aux points de passage par
version sur cet ordinateur (au-delà, les plus anciennes fenêtres ne gardent que leur meilleur) ;
un battement en emporte au plus 96 par fenêtre et 8 par point de passage (4 fenêtres) ; le serveur
les vérifie comme les fenêtres (rôle de la session, bornes, chaque point dans sa fenêtre et entre
son meilleur et son pire temps, jamais plus de points que d'essais), les place sur sa propre mesure
du temps d'entraînement et en garde 60 000 par version (au-delà, une fenêtre ne garde que ses
meilleurs points ; migration `0235_tas_curve_clouds.sql`). Un serveur qui ne les connaît pas encore
les refuse : le jeu renvoie alors la courbe sans eux.

**Gardée sur cet ordinateur** (`saves/tas_local/.training/<niveau>_<CATÉGORIE>.curve.json`, lue et
écrite sur un fil à part : jamais sur le fil principal ; au plus 1 000 points et 1 500 fenêtres par
version, les fenêtres voisines d'une même session, d'un même tour et d'une même section fusionnées
au-delà) **et partagée** : les nouveaux points (16 au plus) et un **résumé** des fenêtres (4 au
plus : les fenêtres fusionnées, médianes pondérées, donc approchées) partent avec le **battement**
ou le **verrou rendu** (`curve`, `attempts`). Le serveur ne croit rien : il place chaque point et
chaque fenêtre sur **sa propre mesure** du temps d'entraînement et des essais, dans ce que ce
battement a compté (le jeu ne dit que « il y a combien de temps ») ; il refuse un appel mal formé
(types, bornes, comptes incohérents, plus d'essais que le temps n'en permet), ne garde un point
que s'il est **strictement plus rapide** que tous ceux de sa version (sinon il le compte comme
refusé) et en garde **1 000 au plus** (au-delà, le dernier cède la place : la courbe garde son
départ et son meilleur temps) ; 1 500 fenêtres au plus (au-delà, une fenêtre s'ajoute à la
dernière de sa session). Un point ou une fenêtre que le serveur n'a pas pris (verrou perdu,
réseau coupé au moment de rendre le verrou) reste sur cet ordinateur, marqué « cet ordinateur
seulement ». Un serveur qui ne connaît pas encore la courbe refuse ces champs : le jeu renvoie
alors le battement sans eux (le verrou n'est jamais perdu pour la courbe) et garde la courbe ici.
Chaque administrateur voit la même courbe : `GET /admin/tas` donne les points de chaque paire,
`GET /admin/tas/{niveau}/{catégorie}/curve` aussi ses fenêtres (demandées à l'ouverture, puis
toutes les 30 s) ; ce jeu garde ses propres fenêtres, plus fines, pour ses sessions, et prend
celles du serveur pour les sessions des autres ordinateurs. Routes : [API.md](online/API.md)
(« Courses de la machine »).

**Dans ADMINISTRATION › TAS** :

* chaque ligne : une **petite courbe en escalier** du meilleur temps le long de l'entraînement, et
  les essais en bref (« 1 800 finis · 0,03 % », « meilleur 1:25.100 »), mesurés à côté des
  autres colonnes ;
* une ligne **sélectionnée** (clic, Entrée / A ; flèches et croix pour y aller) ouvre sa courbe
  **sous le tableau** (le tableau garde trois lignes au moins, à 1280 × 720 comme à 1920 × 1080) ;
  Échap / B, « Fermer » ou la même ligne la referment ;
* **des onglets** au-dessus de la courbe : **« TOUT »** (la course entière : les temps à
  l'arrivée) puis **CP1, CP2… CPn** (les points de passage du niveau ; leur nom au-dessus des
  chiffres) ; à la souris, ou au clavier et à la manette quand la rangée a le focus (← →, LB / RB :
  ADMINISTRATION ne change alors pas d'onglet) ; les étiquettes rapetissent (puis « … ») quand il y
  en a beaucoup ;
* **les nuages** : derrière la ligne du meilleur temps, chaque essai fini (« TOUT ») ou chaque
  temps échantillonné au point de passage (CPk) est un petit point translucide (la densité se
  voit), le meilleur de chaque fenêtre plus vif ; l'échelle des temps s'arrête au 95ᵉ centile du
  nuage : les points plus lents sont des marques sur le bord haut, comptés dans une note (« Hors de
  la vue : 12 plus lents ») ; quand le nuage n'est qu'un échantillon, il le dit (« Nuage : 1 234
  points sur 5 678 ») ; sur un onglet CPk, la ligne est le meilleur temps à ce point de passage le
  long de l'entraînement et les chiffres à droite sont les siens (atteint, meilleur, médian, pire,
  l'échantillon) ; au-delà de 16 000 points dans la vue, une part régulière est dessinée (les
  meilleurs de chaque fenêtre toujours) ;
* **le zoom**, sur chaque axe indépendamment (`ui/widgets/ChartView.h`), jusqu'à 3 s
  d'entraînement et 4 ms à l'écran (1:26.658 et 1:26.660 bien séparés) : la molette autour du
  curseur (Maj ou Ctrl + molette : les temps seulement), glisser pour se déplacer, clic droit
  glissé (ou Maj + glisser) pour zoomer dans un rectangle ; au clavier, un réticule (← → ↑ ↓,
  + / − pour zoomer autour de lui, 0 ou Origine pour revenir) ; à la manette, le stick gauche ou la
  croix, les gâchettes ou le stick droit, Y pour revenir. Zoomée, une **vue d'ensemble** en haut à
  droite de la courbe montre où l'on est, et « Réinitialiser le zoom » est au bout des onglets ;
  les graduations suivent (millisecondes en ordonnée, secondes en abscisse : « 5 h 02:13 »),
  mesurées et éclaircies quand elles se serrent. En direct, la vue par défaut suit les nouvelles
  données, une vue zoomée reste où elle est ;
* **la courbe** : en abscisse le temps d'entraînement cumulé (graduations « 30 min », « 2 h »,
  « 1 h 30 »), en ordonnée le meilleur temps (« 1:26.692 ») ; une ligne en escalier qui descend
  jusqu'au temps d'entraînement actuel, un point plein par amélioration, un anneau pour la course
  de départ, un losange par course publiée ; les temps **livré** et **en ligne** en lignes
  pointillées, nommées à leur droite ; derrière, pour chaque fenêtre, un trait fin du meilleur au
  pire temps des essais finis et une marque à leur médiane (les fenêtres sans nuage : d'avant
  2.98, ou d'autres ordinateurs) ; sous l'axe, les **tours** de `cosmic_tas` avec ce qu'ils
  cherchent quand c'est connu (« Tour 3 (en cours) · sections 4 à 5 ») ; un tour recommence à 1 à
  chaque session : quand la courbe en a plusieurs, chaque marque dit la sienne (« S2 · Tour 3 ») ;
  les marques trop serrées sont éclaircies, le détail reste dans la bulle de la bande ; en dessous,
  la bande des **sections atteintes** (par
  fenêtre, la part de ses essais qui atteint chaque section, l'arrivée en haut : plus c'est sombre,
  moins ils y arrivent) ; une légende de chaque marque ; une grille discrète, des textes dans les
  couleurs du texte, des formes distinctes (pas seulement des couleurs) ;
* **ce que fait le tour**, à droite sous ses chiffres : « Tour 3 · sections 4 à 5 (le reste suit
  la meilleure course) », et pourquoi « Finis » reste bas pendant une fenêtre ;
* **les chiffres**, à droite : pour **tout l'entraînement** et pour **le tour en cours** (ou le
  dernier) : essais, essais finis et leur part, meilleur / médian / pire temps des essais finis,
  morts ; puis les **arrêts par section** (une barre, la part des morts en rouge, le nombre, les
  morts, ceux du tour), l'arrivée en dernier (toujours visible) ; quand toutes les sections ne
  tiennent pas, « … N autres sections » (souris, Entrée / A) les déplie toutes dans une zone qui
  défile (molette, barre, flèches, croix), « Réduire » les replie ; quand des essais ont été faits avant le suivi par
  fenêtre, la part détaillée est dite (« Détail de 1,2 M essais sur 12,3 M ») ;
* une **bulle** sur chaque point (temps, gain sur le précédent, temps d'entraînement, essais, date,
  l'ordinateur de quel administrateur, en attente de partage ou non) et sur chaque fenêtre (tour et
  section, essais, finis, meilleur · médian · pire, parcours des non finis, morts, sections
  atteintes) : à la souris, ou au clavier et à la manette (← → sur la courbe ou la bande) ;
* tout se met à jour **en direct** pendant que la paire s'entraîne ici ; une paire jamais entraînée
  pour cette version le dit (« Cette paire n'a jamais été entraînée pour cette version du
  niveau. ») ; « ‹ Version actuelle › » montre les anciennes versions, en lecture seule.

### Où est le gain (2.99)

Chaque meilleur temps vérifié garde ses **temps aux points de passage** (CP1 d'abord, la
numérotation des onglets ; `bestSplits` de la progression, `splits` du point de la courbe), et
d'où vient le gain (`source`). Comparés au meilleur point d'avant, ils disent **dans quel segment**
est le gain (`tasSegmentDeltas`) :

* la **bulle** d'une amélioration : « CP2 → CP3 : −16 ms », « Autres segments : = », « Trouvée par :
  l'assemblage des guides » ;
* la **ligne** de la paire en entraînement : « meilleur 1:26.642 · dernier gain −16 ms · CP2 → CP3 » ;
* chaque **onglet CPk** : un point par amélioration dont le segment qui finit à ce point de
  passage a changé (vert : plus rapide), à son temps là ; les temps des **guides** à ce point de
  passage et la **somme des meilleurs segments** (des guides et de la meilleure course) en lignes
  pointillées ; dans les chiffres, l'écart « CP5 : TAS +4,200 s sur NovaRider ».

Les temps aux points de passage partent avec le point (battement, verrou rendu : bornés, 32 au
plus) ; le serveur les vérifie comme le reste de la courbe (entiers, croissants, dans le temps du
point ; `source` : un mot court) et les garde (migration `0240_tas_curve_splits.sql`). Un serveur
qui ne les connaît pas les refuse avec les nuages : le jeu renvoie la courbe sans eux et les garde
ici. Les fichiers d'avant se lisent toujours (sans temps aux points de passage).

**Publier pendant l'entraînement** : dès que `cosmic_tas` a écrit une course plus rapide, le jeu
relit ses courses (quelques secondes) et « Publier » devient possible ; publier n'arrête, ne met
en pause ni ne remet à zéro l'entraînement (la courbe marque la publication). Grisé, sa bulle dit
pourquoi : « Rien de plus rapide que la version en ligne », « Vérification en cours… », « Tenu
par X ».

## Courses publiées en ligne

Chaque jeu lit la liste publique (`GET /api/v1/tas`, quelques centaines d'octets par course) une
minute après le démarrage puis toutes les 6 heures (`game/tas/TasOnline.cpp`). Il ne télécharge que
les courses **de sa version du niveau** (empreinte et simulation) **plus rapides** que la meilleure
qu'il a déjà, vérifie que les octets sont ceux annoncés (SHA-256, taille) et que la fiche dit ce que
dit la liste, puis **rejoue la course** sur un fil à part, avec son propre contenu (les révisions en
ligne de cet ordinateur comprises), exactement comme un replay (`verifyTasReplay` : aucune
désynchronisation, le niveau fini, le temps annoncé, les règles de la catégorie, les morts, dégâts et
collectibles de la fiche). Refusée, elle est oubliée (et ne sera pas retéléchargée) ; vérifiée, elle
est gardée dans `saves/tas_online/runs/` (la disposition de `data/tas`). `tasFor()` rend la course
valide **la plus rapide** des livrées et des téléchargées ; hors ligne, les courses déjà vérifiées et
les livrées restent là.

## Quand un niveau change

Un niveau change de **version** quand son empreinte de classement change (le niveau modifié dans
ADMINISTRATION › Campagne, régénéré par `tools/levelgen`, une révision d'une entité qu'il utilise) ou
quand la simulation change (`kSimulationVersion`). Alors :

* **Joueurs** : une course dont `levelHash` ou `simVersion` n'est pas celle du niveau tel que le jeu le
  joue n'est jamais montrée, livrée ou téléchargée ; une course d'une autre version de la liste en
  ligne n'est même pas téléchargée.
* **Serveur** : les courses sont rangées **par version** (`level_hash`, `sim_version`) et jamais
  supprimées ; une publication pour une version que le serveur sait dépassée est refusée
  (`LEVEL_VERSION_OUTDATED` : « Ce niveau a changé : sa version en ligne est la révision N… ») ; la
  liste de l'administration donne la course de la version en cours et, à part, la dernière d'une
  ancienne version (« archive ») ; les verrous et les totaux d'entraînement sont **par version**.
* **ADMINISTRATION › TAS** : une paire qui n'a de course que pour une ancienne version montre
  « À recalculer (niveau modifié) », l'ancien temps marqué « ancienne version, non comparable » ;
  elle compte comme jamais entraînée pour la nouvelle version et « Tout entraîner » la prend.
  L'entraînement de la nouvelle version **part de rien** : une ancienne course n'est jamais donnée
  comme course de départ (rien ne dit jusqu'où elle vaut sur le niveau changé), et rien n'est
  publié qui n'a pas été vérifié sur la version en cours.
* **Tests** : `tests/test_tas.cpp` rejoue chaque course livrée de la version en cours (une course
  qui annonce l'empreinte actuelle et ne se reproduit pas est un échec) ; une course livrée d'une
  ancienne version n'est qu'un **avertissement** qui liste les fichiers à recalculer (les niveaux
  sont régénérés souvent : la construction continue).

## États sauvegardés

`game/tas/ArenaWorld.h`, `engine/core/ArenaHeap.h`. Une course solo (son `GameWorld` et tout ce
qu'il possède — joueur, pouvoirs, entités, ennemis, boss et leurs parties, physique, événements,
minuteries, cinématiques, générateur aléatoire —, le `ResourceManager` et l'`AudioSystem` qu'il
utilise, et le nombre de pas) est construite **dans une arène** : un bloc de mémoire à adresse fixe
dont l'allocateur garde aussi sa comptabilité dans le bloc. Tant que l'arène est celle du fil
(`ArenaScope`), toutes les allocations C++ de ce fil s'y font (`operator new`, remplacé par
`tools/tas/HeapHooks.cpp` dans l'outil et les tests seulement — **jamais dans le jeu**). Un état
sauvegardé est la copie des octets utilisés du bloc ; le restaurer les recopie : chaque objet,
chaque pointeur entre objets et l'allocateur lui-même redeviennent exactement ce qu'ils étaient.
Rien du monde ne peut être oublié, quels que soient ses membres : c'est une copie profonde de toute
la mémoire de la course.

Ce qui doit rester hors de l'arène — la mémoire qui appartient à autre chose que le monde et qui
dure au-delà de l'appel qui l'alloue (le journal, le cache des traductions, les feuilles de parties
des boss lues une fois, les tables construites au premier usage, l'atlas des décors) — ouvre une
portée `ArenaOutside` : ses allocations vont au tas normal. Un état n'est valable que pour son
arène (ses pointeurs sont ceux de son bloc) : chaque fil de la recherche a la sienne.

Les tests (`tests/test_tas_savestate.cpp`) : sur le premier niveau de chaque planète et dans un
combat de boss (le giga-boss du Noyau en phase 5), N pas, un état sauvegardé, M pas (les caches
globaux lus une fois, comme les feuilles de parties d'un boss, se remplissent) ; restauré, les mêmes
M pas, deux fois, redonnent **la même empreinte à chaque pas** et **la même arène** (octets
utilisés, allocations vivantes, disposition de tous ses blocs : `Arena::layoutDigest`) ; pendant
ces pas, tout ce qui a été pris au tas normal (derrière `ArenaOutside`) lui est rendu : **rien de la
course ne reste hors de son arène** (compteurs des crochets, `heap::outsideCount`) ; après d'autres
entrées puis une nouvelle restauration, la même course encore, la mémoire libérée par la
restauration écrasée (rien ne doit encore s'en servir). Un monde dans une arène joue exactement
comme un monde sur le tas normal.

Les octets eux-mêmes ne sont pas comparés (2.95.1) : deux courses identiques peuvent laisser des
octets différents que personne n'a écrits — le bourrage d'une structure, la fin inutilisée d'une
chaîne courte que `std::string` de MSVC copie en entier depuis la pile (la pile, elle, n'est pas la
même d'une fois à l'autre). C'est ce que voyait le test sous Windows (« aethra_01 : les mêmes
entrées ne laissent pas les mêmes octets ») alors que la course était la même à chaque pas. La
restauration, elle, recopie tous les octets, ceux-là compris.

**Coût** (mesuré sur nos machines, 4 cœurs partagés et chargés) : un état sauvegardé de
« Les Ruines Suspendues » fait **1,3 Mo** (1,6 Mo au plus sur les premiers niveaux des planètes) ;
le sauver ou le restaurer coûte **0,2 à 0,3 ms**, le temps de 4 à 6 pas de simulation (un pas : 50 à
75 µs sur ce niveau). L'outil affiche ces coûts au début de chaque recherche.

## Audit du déterminisme

`tests/test_start_audit.cpp` : le premier niveau de chaque planète, lancé de six façons — intro
passée au premier pas, passée après 0,5 s, regardée jusqu'au bout ; avec l'affichage, le son, les
particules, les secousses, les préchargements du HUD et les temps de référence d'une vraie partie,
ou silencieux sans particules —, est le même monde au premier pas où Nova peut bouger, et le reste
après 20 s des mêmes entrées.

Un second test vérifie que l'audit voit une différence quand il y en a une : avec les règles de 18
(avant 2.92, le niveau continuait pendant l'intro), l'intro regardée ou passée ne donne pas le même
monde.

**Résultat (2.92.0, simulation 19)** : aucune divergence trouvée. Le départ retenu en 2.92 (le
niveau attend son chronomètre) rend bien le même monde quelle que soit la façon de lancer le
niveau ; rien n'a eu à changer dans la simulation (ni `kSimulationVersion`, ni empreintes). La
recherche vérifie aussi, après chaque section, que le monde est le même dans les arènes de tous
ses fils, et la course finale est rejouée sur le tas normal avec d'autres réglages d'affichage.

## Résultats

Calculés avec `cosmic_tas` 1 (jeu 2.92.0, simulation 19), sur nos machines, en une seule recherche
partie de rien (sans course de départ) :

| Niveau | Catégorie | Meilleur temps trouvé par la machine | Essais simulés | Recherche | Morts, dégâts, collectibles |
|---|---|---|---|---|---|
| Les Ruines Suspendues (`sylvanox_01`) | ANY% | **1:26.692** | 549 337 | 20 min, 2 fils | 0 mort, 1 dégât, 24 sur 75 |

Course trouvée après 9,7 min (1:26.733 ; les points de passage à 11,0 s, 30,1 s, 39,7 s, 51,4 s,
64,7 s et 83,5 s), puis affinée jusqu'à la fin du budget (1:26.692). Pour comparer : la médaille
de platine du niveau demande moins de 3:20. Les autres niveaux et catégories ne sont pas encore
calculés (sur demande) : leurs écrans n'affichent rien tant que leur fichier manque.

---

[← Retour à l'accueil (README)](../README.md)
