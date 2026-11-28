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
| `--no-planner` | sans le planificateur (« Le planificateur », ci-dessous) : pour comparer |
| `--portfolio` | le portefeuille de stratégies (faisceau, exploration, polissage, affinage) et la séparation et évaluation : [Le portefeuille](#le-portefeuille) |
| `--arms <a,b,…>` | le portefeuille avec ces bras seulement (`beam`, `explore`, `timing`, `refine`, ou un bras ajouté) |
| `--slice <secondes>` | la durée d'une tranche du portefeuille (45 ; une passe du faisceau : trois tranches au plus) |
| `--no-bnb`, `--bound-slack <s>` | sans séparation et évaluation ; ou avec une marge sur le temps à battre |
| `--user-dir <dossier>`, `--server <url>` | le niveau tel que le jeu le joue sur cet ordinateur : les révisions en ligne (campagne, entités) que le jeu a téléchargées |
| `--guide <replay> [--guide-name <joueur>]` | (2.99, plusieurs fois, 8 au plus) une course d'un joueur qui guide la recherche : un replay exact de **cette version** du niveau et de la simulation ; d'une autre version du même niveau, son **tracé** seulement (jamais ses entrées) ; d'un autre niveau, refusé (« Guide refusé (…) : … ») ; voir « Les guides » |
| `--phase`, `--phase-source <replay>`, `--realign <replay>` | le réalignement de phase : une arrivée en avance continuée par la suite des autres courses, après les attentes qui remettent le niveau en phase ; voir [Le réalignement de phase](#le-réalignement-de-phase) |
| `--branch <replay> [--branch-through <n>]` | une branche : les `n` premières sections du replay gardées, la suite entraînée avec sa propre meilleure course ; voir [La branche](#la-branche) |
| `--no-phase-leads` | sans les pistes de phase (pour comparer) |

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
vers chaque repère. La navigation des joueurs simulés des raids (`game/raid/nav`) ne sert pas à
cette distance (elle est faite pour les zones d'un raid et des cibles qui bougent) ; sa copie des
mouvements de Nova et du terrain sert au **planificateur** (ci-dessous).

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

### Le planificateur

`tools/tas/Planner.h` : une **source de plus** de candidats pour les faisceaux, à côté des images de
la meilleure course. Les coups tirés au hasard du faisceau enchaînent mal les dashs, les sauts
longs, les sauts muraux et les pouvoirs ; le planificateur les **planifie**, depuis l'état réel de
la course, vers le prochain repère (la fin de la section).

* **Où il planifie** : dans la copie légère des mouvements de Nova des joueurs simulés des raids
  (`game/raid/nav/BodySim`, vérifiée contre le vrai joueur par `tests/test_raid_nav.cpp` : la
  course, le saut et sa hauteur tenue, le temps de coyote et le saut tamponné, le double et le
  triple saut, la glissade et le saut mural, l'accroupi et la glissade au sol, la descente à
  travers une plateforme, les échelles, l'eau, les courants d'air, le dash et le saut long, la
  poussée ; **son énergie, sa régénération et ses temps de recharge**), contre une copie de la
  physique du niveau (`game/raid/nav/Terrain` : les plateformes mobiles là où elles **seront**, les
  plateformes qui s'effondrent, les zones). Un pas de la copie coûte environ 0,4 µs, contre 20 à
  75 µs pour un pas du vrai moteur et 0,1 à 0,3 ms pour restaurer un état sauvegardé : un plan de
  12 s se cherche en quelques dizaines de millisecondes.
* **Comment** : un faisceau synchrone dans le temps, sur des manœuvres de 8 pas : courir, le saut
  tenu (sa hauteur), relâché puis appuyé de nouveau à deux moments (le double saut, le saut mural,
  **le saut appuyé avant l'atterrissage**, que le tampon garde), un saut bas, Bas (glissade, chute
  rapide, descente à travers une plateforme), le dash à plat et en diagonale, **le saut long** (un
  saut appuyé pendant le dash au sol), le dash fini par un saut en l'air, la poussée. Un dash n'est
  proposé **que si l'énergie, la recharge et les usages en l'air le permettent** dans la manœuvre :
  sinon le faisceau attend la régénération ou fait autre chose. Tous les états d'une génération
  ont joué le même nombre de pas : la première arrivée est la plus rapide trouvée. Les états sont
  classés par une estimation du temps qui reste : la distance du niveau au repère (`LevelNavigator`)
  là où Nova est et là où son élan la portera un instant plus tard (**garder l'élan** plutôt
  qu'arriver « près » mais lent), moins un peu pour l'énergie et les sauts gardés ; un seul état par
  endroit, vitesse et ressources. Sans arrivée dans l'horizon (12 s), les plans les plus avancés.
* **Validé par le vrai jeu** : la copie ne connaît ni les ennemis, ni les coups, ni les dégâts.
  Chaque plan est joué dans le vrai monde de la recherche, morceau par morceau comme les images
  de la meilleure course ; **chacun de ses préfixes est proposé au faisceau, qui ne le garde que
  sur son mérite réel** (pas de place réservée, contrairement à la meilleure course) ; son
  arrivée est une arrivée comme les autres, classée, raccordée et vérifiée de la même façon ; la
  course finale est toujours vérifiée par la lecture normale des replays.
* **Quand** : au début de chaque section (depuis l'état de départ) et toutes les 8 générations
  depuis le meilleur état du faisceau (6 plans en cours au plus par fil). Les mêmes entrées
  donnent les mêmes plans (rien d'aléatoire).
* **La borne inférieure** (`Planner::lowerBoundSeconds`, `lowerBoundToFinish`) : un temps que Nova
  ne peut **jamais** battre pour toucher un repère (ou la sortie, en ligne droite : une course peut
  laisser des repères de côté), pour l'élagage des autres sources (« horloge + borne ≥ meilleur
  temps intermédiaire connu » : inutile de continuer). Par axe, l'écart entre la boîte de Nova et
  celle du repère divisé par la vitesse la plus grande possible sur cet axe : la course, la
  glissade, les sauts, le saut mural, le recul d'un coup, les tremplins et les plateformes mobiles
  du niveau, et les pouvoirs — portés, ou que le niveau peut encore donner (comptés comme portés) ;
  **le dash et la poussée ne comptent qu'à partir du moment où l'énergie et la recharge les
  permettent** ; une téléportation couvre sa distance d'un coup (une par recharge) ; un retour en
  arrière (le rappel) annule la borne. Admissible par construction (lignes droites, vitesses
  maximales), donc large : vérifiée sur la course livrée de `sylvanox_01` (une borne tous les
  30 pas vers chaque repère encore devant, jamais au-dessus du temps réellement mis) et sous les
  temps d'une force brute et du planificateur sur de petits mondes (`tests/test_tas_planner.cpp`).
  Ce qu'elle suppose : aucune mort ne rapproche du repère (la réapparition se fait à un point de
  passage déjà touché, derrière).
* **Ce qu'il compte** (journal, `SearchOutcome::planner`) : plans proposés, plans dont un état a
  été gardé par un faisceau, plans arrivés au bout de la section dans le vrai jeu, sections dont la
  gagnante vient d'un plan, et **l'écart entre la copie et le vrai jeu** (les pieds de Nova, en
  pixels, par nombre de pas joués du plan).
* **Un chemin humain** (facultatif, `SearchOptions::routeHints` : les positions et les temps de la
  trajectoire d'un fantôme) : seulement un point de départ (un chemin humain n'est pas la limite).
  Le planificateur vise toujours le repère, et aussi le point de ce chemin une seconde plus loin
  que Nova (une étape intermédiaire), tant qu'il est sur le chemin du repère ; de moins en moins
  souvent (un plan sur 2, 4… 16) dès que Nova y est aussi tôt que l'humain, ou plus tôt.
* Chaque plan est une `Lead` de source `planner` (comme la meilleure course et les guides, mais sans
  place réservée) : une section gagnée par un plan et une course plus rapide qui en vient le disent
  (`bestSource`).
* Une section ne s'arrête pas (quelques générations après la première arrivée) tant que les images
  de la meilleure course et des guides jouent encore : une arrivée plus tôt (celle d'un plan) ne
  doit pas cacher celle qui se raccorde.
* `--no-planner` l'arrête (pour comparer).

**Mesure du planificateur** (`sylvanox_01` ANY%, depuis la course livrée 1:26.692, 7 min, 1 fil,
graine 1, une recherche du niveau entier ; les deux recherches en même temps sur 4 cœurs partagés) :

| Point de passage (chrono) | livrée | sans planificateur | avec |
|---|---|---|---|
| CP1 (Seuil des Anciens) | 11,0 s | 10,975 s | **9,167 s** |
| CP2 (Canopée brisée) | 30,1 s | 29,525 s | **25,275 s** |
| CP3 | 39,7 s | 45,850 s | 40,592 s |
| CP4 | 51,4 s | 58,733 s | 55,267 s |
| CP5 | 64,7 s | 71,458 s | 67,067 s |
| sortie (la recherche seule) | 1:26,692 | 1:35,033 | 1:34,542 |

Le planificateur gagne 1,8 s sur la première section et 2,4 s de plus sur la deuxième (le joueur
le plus rapide : 1,8 s et 4,1 s devant la course livrée). Ces arrivées ne se raccordent pas à la
course livrée (fin estimée) et le combat du boss, cherché de zéro en quelques minutes, reste bien
plus lent que celui de la course livrée : la course gardée reste 1:26,692 (1:26,675 sans le
planificateur, par un raccord après la section 1). Le gain vient des faisceaux qui partent des
préfixes des plans (63 plans sur 144 gardés au moins une fois), aucun plan entier n'est arrivé tel
quel au bout de sa section dans le vrai jeu. Écart copie / jeu : 8,5 px en moyenne sur les 32
premiers pas d'un plan, 38 px jusqu'à 128 pas, 79 px au-delà (les ennemis, les coups et les
plateformes hors de la fenêtre de la copie). Coût : 6,2 s de planification sur 3,5 min (16 M pas de
la copie, environ 0,4 µs chacun) ; les essais par seconde ne baissent pas de façon mesurable.

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

### Le portefeuille

`--portfolio` (ou `--arms faisceau,…`) : un **portefeuille de stratégies** se partage chaque tour
(`tools/tas/Portfolio.*`). Avec une course de départ qui finit la catégorie, chaque tour cherche
une fenêtre dès le **premier** tour (`WindowPlanner` : d'abord les sections 1 et 2) et se découpe
en **tranches** (`--slice`, 45 s ; une passe du faisceau : deux tranches au plus) :

| Bras (`--arms`) | Stratégie | Fichiers |
|---|---|---|
| `beam` (faisceau) | une passe du faisceau sur la fenêtre (ci-dessus), raccords compris | `Search.cpp` |
| `explore` (exploration) | Go-Explore, ci-dessous | `Explore.*` |
| `timing` (polissage) | le polissage des timings, ci-dessous | `Timing.*` |
| `refine` (affinage) | l'affinage existant (dernier sixième du tour) | `Search.cpp` |
| autres | un bras ajouté par `Portfolio::addArm` (propositions du planificateur, raccords de guides…) | — |

* **Le bandit** (`Bandit`) : chaque tranche va, avec tous les fils, au bras tiré par
  *échantillonnage de Thompson* sur son **taux de gain** (millisecondes gagnées par seconde de
  calcul, loi gamma ; les tranches récentes pèsent plus, ×0,9 par tranche), avec un **plancher** :
  un bras sous 8 % du temps (moins s'ils sont nombreux) passe d'abord ; l'exploration garde
  **20 %** quoi que gagnent les autres (ses gains sont rares, et c'est elle qui cherche au-delà des
  routes connues, celle d'un joueur comprise). Le faisceau tourne pendant
  les cinq premiers sixièmes du tour, l'affinage seulement dans le dernier ; l'exploration, le
  polissage et les bras ajoutés partout.
* **Qui a trouvé quoi** : chaque course plus rapide est créditée au bras dont la tranche tournait ;
  le journal l'écrit (« Tranche exploration (45 s) : meilleure course … (améliorée) », puis
  « Portefeuille : faisceau 41 % du temps, … ») et le fichier de progression les donne
  (`strategies` : temps, gain, nombre de courses par bras). La courbe d'entraînement en fait deux
  lignes sous les comptes (« Trouvé par : exploration 2 · polissage 1 », « Part du temps : … »).
* **Les temps aux points de passage** de la course de départ et de chaque course écrite sont dans
  le journal (« Temps aux points de passage : CP1 10.975 CP2 30.110 … »).

**Go-Explore** (`Explore.*`) : une archive de **cellules** — l'endroit de Nova (grille de 24 px),
sa vitesse (par 50 px/s, montée ou chute), au sol ou non, sauts en l'air et Dash restants, énergie
(par quart), cœurs, pouvoirs portés et prêts, repère, ce que le monde a fait (jalons) — qui garde
pour chacune **le chemin le plus rapide connu** (ses entrées depuis le chargement, en arbre partagé,
et son état sauvegardé tant que la mémoire le permet : 140 par fil, les plus anciens oubliés et
rejoués depuis leur ancêtre le plus proche). On tire une cellule selon sa **promesse** (son avance
sur la meilleure course à progression égale, peu choisie, améliorée récemment), on y **revient** par
son état sauvegardé, on **explore** quelques coups au hasard (et ceux d'un planificateur, si un
crochet `proposals` en fournit), et chaque état **nouveau ou atteint plus tôt** devient ou améliore
une cellule. Rien n'est jeté pour être « loin du prochain repère » : c'est ce qui laisse vivre
d'autres routes. Une fin de section atteinte **plus tôt** que la meilleure course est jouée
jusqu'à la sortie avec la suite de la meilleure course (raccord réaligné) ; plus rapide, elle est
rejouée depuis le chargement puis offerte. L'exploration va **une section au-delà de la fenêtre** :
une arrivée plus tôt qui ne se raccorde pas tout de suite peut y trouver son propre chemin. L'archive vit tant que le processus vit (tour après tour,
une par fil : un état sauvegardé n'appartient qu'à son arène), bornée à 6 000 cellules par fil ; la
course de chaque tour y entre comme une suite de cellules « ancres » (toutes les 0,5 s). Des
**indices de route**, facultatifs (`GoExplore::routeHints` : des positions avec leur temps, prises
par exemple de la trajectoire d'un fantôme), favorisent les cellules proches de ce chemin (×1,5, et
jusqu'à ×3,5 quand elles y sont aussi tôt que l'indice ou plus tôt : ses temps aux points de
passage sont des cibles souples). Ils ne font qu'**amorcer** l'exploration : leur faveur diminue de
moitié toutes les 1 500 visites, et la nouveauté (cellules peu choisies, nouvelles ou améliorées)
comme l'avance sur la meilleure course continuent de choisir, hors de ce chemin aussi : la route
d'un joueur est un départ, pas la limite. Sans eux, l'archive trouve ses routes seule.

**Le polissage des timings** (`Timing.*`) : la fenêtre de la meilleure course vue comme ses
**événements d'entrée** (appui et relâchement de chaque touche, à leur image), puis, contre le vrai
jeu depuis des états sauvegardés toutes les secondes :

1. chaque événement **décalé de ±1 à ±3 images** (force brute image par image, comme les
   brute-forcers de TAS), d'abord ceux qui précèdent une **perte de vitesse** (vitesse horizontale
   qui chute, atterrissage, mur) ;
2. des **appuis courts ajoutés** (saut, touche d'un pouvoir, direction ; 1, 6 ou 12 images, juste
   avant ces pertes) ou **relâchés un instant**, des **appuis courts supprimés** ; puis ±4 à ±6 ;
3. une **évolution différentielle** (population 10) sur le vecteur des décalages des 8 événements
   les plus proches des pertes, arrondi aux images : les changements conjoints.

Mesure : la progression de Nova (le score de la recherche) à l'image où la meilleure course finit la
fenêtre ; une modification plus avancée est jouée jusqu'à la sortie avec la suite (raccord) ; plus
rapide, elle est rejouée depuis le chargement. Les images d'avant la fenêtre, d'une cinématique,
d'un passage de cinématique ou d'une réponse à un pouvoir ne changent jamais.

### Séparation et évaluation

Avec le portefeuille (`--no-bnb` pour l'enlever, `--bound-slack <s>` pour une marge), le faisceau
et l'exploration **abandonnent un état** quand son chrono plus une **borne inférieure** du temps
qu'il lui faut encore pour finir sa section dépasse le temps de la meilleure course à ce point de
passage (plus le retard pris avant la section, pour ne pas condamner une course déjà en retard)
(`Bound.*`). La borne doit rester **admissible** (jamais plus que la vérité) :

* horizontalement, l'écart à la boîte du point de passage (moins ce qu'une téléportation portée
  franchit d'un coup) sur la plus grande vitesse horizontale du jeu de pouvoirs : course, glissade,
  saut mural, recul, chaque vitesse des pouvoirs portés (Dash, Impulsion, Grappin…), sa vitesse du
  moment et la plus grande vitesse de la meilleure course (tremplins, vents), le tout **+25 %** ;
* le long du parcours, la distance du champ de `LevelNavigator` (×0,7 : sa grille et sa portée
  sont grossières) jusqu'au point de passage, ou jusqu'à la région où il compte comme dépassé, sur
  la plus grande vitesse dans toutes les directions (chutes, sauts) ;
* un crochet `Portfolio::lowerBound` prend une borne plus serrée d'ailleurs (le planificateur) :
  la plus grande de deux bornes admissibles l'est encore.

Le test (`tests/test_tas_portfolio.cpp`) rejoue la course livrée de `sylvanox_01` image par image :
la borne ne dépasse **jamais** le temps qu'il lui restait vraiment, aucun de ses états n'est abandonné,
et la même course retardée de 2 s l'est avant le premier point de passage. Les états que le
faisceau proposait lui-même (ses images de la meilleure course) ne sont jamais abandonnés ; une
génération entièrement abandonnée termine la section au lieu de la recommencer.

### Mesure du portefeuille

`sylvanox_01` ANY%, depuis la course livrée (1:26.692), entraînement en tours de 5 min, 2 fils,
graine 1, 20 min chacun, l'un après l'autre (machine de 4 cœurs partagée avec d'autres
compilations : les temps sont bruités). Meilleure course **vérifiée** à la fin de chaque tour :

| Configuration | 5 min | 10 min | 15 min | 20 min | Essais (20 min) | Gains trouvés par |
|---|---|---|---|---|---|---|
| Recherche d'avant (tour 1 : tout le niveau) | 1:26.675 | 1:26.667 | 1:26.667 | 1:26.642 | 1,84 M | faisceau (raccords) |
| Portefeuille entier (`--portfolio`) | 1:26.675 | 1:26.675 | 1:26.650 | **1:26.092** | 1,76 M | faisceau 6, exploration 0, polissage 0, affinage 0 |
| Exploration seule (`--arms explore`) | 1:26.675 | 1:26.675 | 1:26.675 | 1:26.675 | — | exploration 1 |
| Faisceau seul dans le portefeuille (`--arms beam,refine`) | 1:26.675 | 1:26.675 | 1:26.650 | 1:26.175 | 2,71 M | faisceau 3, affinage 1 |

Temps aux points de passage de la course finale (CP1 « Seuil des Anciens », CP2 « Canopée brisée ») :

| Course | CP1 | CP2 | CP3 | CP4 | CP5 | CP6 | Fin |
|---|---|---|---|---|---|---|---|
| Livrée | 11.025 | 30.142 | 39.658 | 51.433 | 64.667 | 83.525 | 86.692 |
| Recherche d'avant, 20 min | 10.983 | 30.117 | 39.633 | 51.408 | 64.617 | 83.475 | 86.642 |
| Portefeuille, 20 min | 10.983 | 30.125 | 39.642 | 51.417 | 64.625 | **82.917** | 86.092 |
| Exploration seule, dès 3 min | **10.850** | 30.125 | 39.642 | 51.417 | 64.650 | 83.508 | 86.675 |
| Faisceau seul dans le portefeuille, 20 min | 10.983 | 30.125 | 39.642 | 51.417 | 64.625 | 82.942 | 86.175 |

Ce que cela dit, honnêtement :

* **Le gain de 0,55 s du portefeuille est au segment du boss** (CP5 → CP6 : 18,29 s au lieu de
  18,86 s), trouvé par le **faisceau** pendant le tour 4, dont la fenêtre était les sections 6 et 7 :
  avec le portefeuille, les fenêtres commencent dès le tour 1 (sections 1-2, 2-3, 4-5, 6-7), alors
  que la recherche d'avant passe son premier tour sur tout le niveau et n'y était pas encore au bout
  de 20 min (1-2, 2-3, 4-5). Le faisceau seul dans le même cadre (fenêtres dès le tour 1,
  séparation et évaluation) trouve le même gain (1:26.175) : **il vient du calendrier des fenêtres
  et du faisceau, pas de l'exploration ni du polissage**, qui ont pris 39 % du temps sans gain ici
  (l'écart 1:26.092 / 1:26.175 est du bruit entre deux tirages). Le portefeuille n'est donc pas
  activé par défaut.
* **L'exploration finit le segment 1 en 10,850 s** (0,13 s plus tôt que le faisceau, 0,18 s plus tôt
  que la course livrée) : le gain à la fin n'est que de 17 ms, parce que **CP2 tombe à 30,125 s dans
  toutes les courses** : quelque chose du monde (une plateforme, une porte à minuterie…) fixe ce
  temps, et une avance au segment 1 est absorbée. Les arrivées jusqu'à 0,6 s plus tôt à CP2 que le
  faisceau trouve ne se raccordent pas à la suite de la meilleure course (leur monde n'est plus en
  phase) ; la séparation et évaluation les coupe à la section suivante quand elles ne peuvent plus
  battre le temps du point de passage. Gagner des secondes aux segments 1 et 2 demande donc de
  chercher **les sections suivantes depuis ces arrivées**, pas de rejouer la fin de la meilleure.
* **Le polissage** n'a rien trouvé sur cette course (7 tranches, ±1 à ±6 images, ajouts, retraits,
  évolution) : l'affinage d'avant l'a déjà beaucoup décalée image par image, et la plupart des
  décalages n'y changent rien (98 sur 130 au segment 1) ou cassent la suite (32) : le temps y est
  fixé plus loin. Il retrouve une image perdue quand elle compte (test).
* **Sur le petit niveau du test** (un mur, une porte au pied qui s'ouvre à 12 s, le champ de
  distance disant « par la porte »), l'exploration passe en 11,8 s au lieu de 16,1 s : **en
  glissade, Nova passe la porte FERMÉE** (pieds au sol, ~230 px/s, à 5,8 s). C'est peut-être un
  défaut du jeu (une porte fermée devrait bloquer une glissade) : à vérifier avant d'en faire une
  technique.
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
  suite ; la phase du monde, elle, est couverte par les attentes du réalignement de phase).

Mesure (2.99, `sylvanox_01` ANY%, 2 fils, depuis la course livrée 1:26.692 avec comme guide une
course 1:26.617 d'un autre entraînement) : 1:26.600 vérifiée et écrite **6 s** après le départ (un
raccord dans le guide après la section 1), 1:26.592 à 24 s, 1:26.567 au deuxième tour (1 min 40).
Avec le record de NovaRider (1:34.392, jeu 2.88, simulation 17 : son tracé seulement), 1 fil : la
recherche arrive au CP1 en 9,667 s (la machine 11,025 s, le joueur 9,225 s), mais l'avance fond
aux CP2 (28,825 s) et CP3 (39,433 s) et ne se raccorde pas à la meilleure course : **continuer
après une arrivée de plusieurs secondes en avance** reste la limite (les éléments du niveau qui
bougent avec le temps ne sont plus au même endroit pour les images de la meilleure course).

### Le réalignement de phase

**Le problème mesuré.** Une arrivée en avance à un point de passage ne joue plus la suite de la
meilleure course : ce qui bouge avec le temps (plateformes mobiles, lasers, patrouilles) n'est plus
au même endroit pour ses images. Sur `sylvanox_01`, l'entraînement « fresh » de la nuit (sans course
de départ, avec le planificateur) arrive au CP2 en 23,925 s, 6,2 s avant la meilleure course d'alors
(30,125 s), mais au CP3 en 39,517 s, presque en même temps qu'elle (39,642 s) ; sa course entière,
1:19.275, garde surtout l'avance des sections 1, 2, 4, 5 et du boss.

**Ce qui fixe le CP3 vers 39,5 s** (mesuré avec `--study`, le chemin de Nova toutes les 0,25 s) :
pas un minuteur du niveau. La course « fresh » reste **4 s devant la porte o171** (x ≈ 4300, de
27,9 s à 32,0 s) avant de se retourner pour tirer l'onde sur le récepteur o172 qu'elle a dépassé
(énergie 150 → 140 à 31,65 s) ; la meilleure course le tire en chemin et passe la porte à 32,4 s :
les deux passent la porte à 0,3 s près, puis les plateformes mobiles (o186 à o188 : périodes 7,4 s,
5,26 s et 6,76 s) du même cycle. Rejouer les images de la meilleure course derrière l'arrivée en
avance passe la porte (après 16 images d'attente : l'énergie) mais tombe aux plateformes : avec
6,1 s d'avance et des périodes de 6,8 et 7,4 s, la seule attente qui remet ces deux plateformes en
phase est l'avance entière. Gagner là demande un autre passage des plateformes (le faisceau, le
planificateur), plus tôt dans leur cycle — d'où les pistes de phase ci-dessous, qui donnent ce
départ au faisceau.

**Le CP2 « fixé » vers 30,1 s** des mesures d'avant ne l'était pas par le niveau : la meilleure
course d'alors perd environ 3 s à monter vers x ≈ 1870 (de 13,4 s à 17,9 s ; « fresh » y monte en
1,5 s) et 0,5 s arrêtée vers x ≈ 3665 ; mais toute arrivée au CP2 entre 22,6 s et 30,5 s environ
passe les plateformes de la section 3 au même cycle et arrive au CP3 vers 39,5 s : la fin mesurée ne
change pas, et le gagnant d'une section (classé par la fin mesurée) ne préférait jamais un CP2 plus tôt.

**Le réalignement** (`tools/tas/Phase.h`, générique : rien n'y connaît un niveau) :

* **Les éléments périodiques** du niveau (`periodicElements`) : une plateforme mobile (ni
  déclenchée ni à l'arrêt) a pour période l'aller et retour, attentes comprises
  (2 × (longueur / vitesse + attente)) ; une orbite 2πr / vitesse ; un laser allumé + éteint. La
  **période rencontrée** par une section (`periodMet`) : la plus longue de ceux dont la zone (élargie
  de 96 px) touche le chemin de la source dans la section.
* **Les attentes** : depuis l'arrivée en avance, les images de la source pour la section, jouées
  après k images sans rien appuyer, k de 0 à min(avance + 0,5 s, période rencontrée + 0,5 s, 8 s)
  (0,5 s au moins), toutes les 4 images puis image par image juste sous les meilleures ; depuis le
  début de la section de la source et depuis l'image où Nova y était la plus proche (après
  l'attente). k = l'avance remet tout le monde périodique en phase : la recherche ne perd jamais
  l'avance par l'attente, elle cherche **la plus petite** attente qui marche.
* **Hors de phase** : un essai à plus de 96 px du chemin de la source 40 images de suite s'arrête ;
  les deux qui sont allés le plus loin reçoivent **une seconde attente** au dernier endroit, avant
  leur sortie du chemin, où la source avait les pieds au sol (un arrêt propre), dans ce qui reste de
  l'avance. Un essai qui ne peut plus arriver avant les arrivées gardées (au rythme de la source,
  moins 0,5 s), ou plus de 1,5 s après le temps de la source pour la section, s'arrête aussi ; une
  mort n'est jamais gardée.
* **Plusieurs sources** : chaque section essaie chaque source qui l'a (la meilleure course, les
  guides, les `--phase-source`, la branche, les arrivées en avance du tour) : une course peut prendre
  la section d'une source après l'arrivée d'une autre, remise en phase — une somme des meilleurs
  segments qui se joue vraiment. La course entière est rejouée depuis le chargement, puis vérifiée
  par la lecture normale des replays comme toute course (source `phase`).
* **Les touches des pouvoirs suivent les pouvoirs** (`remapSlots`, `Sim.h`) : un replay appuie la
  touche d'un **emplacement** ; une source qui porte d'autres pouvoirs, ou les mêmes à d'autres
  emplacements (« dash, onde, impulsion » contre « dash, onde, bouclier, impulsion »), voit ses
  touches d'emplacement (et la réponse à une offre de pouvoir) déplacées vers l'emplacement qui porte
  le même pouvoir dans le monde joué ; un pouvoir que ce monde ne porte pas : sa touche est retirée.
  `RunStudy` garde pour chaque image la disposition des emplacements de la source.
* **Quelles arrivées continuent** (aux fins de section) : d'abord la suite **pure** de la source dont
  le préfixe est le début (ses sections suivantes, sans attente) — le réalignement ne fait jamais pire
  que cette source ; puis un **ensemble de Pareto** : les arrivées les plus tôt (un cœur perdu compte
  0,25 s), et une plus tardive seulement si elle garde plus d'énergie ou de santé que toutes celles
  déjà gardées (6 au plus). Mesuré : la section 4 de « fresh » derrière le CP3 de « main » arrive
  1,5 s plus tôt, mais avec moins d'énergie, et le boss ne se fait plus : sans ces deux règles, la
  seule suite qui finissait était perdue.
* **Pareto par phase** : une arrivée plus tôt qui attend atteint, debout, toutes les phases plus
  tardives du monde périodique ; les attentes jusqu'à l'avance couvrent donc les arrivées plus
  tardives qui ne diffèrent que par la phase. Ce qu'elles ne couvrent pas : la vitesse et l'énergie
  gardées (l'ensemble de Pareto ci-dessus) et les ennemis déplacés par Nova (la position d'un ennemi
  dépend d'elle).
* **Rien ne finit** : `--realign-out` écrit alors la voie réalignée la plus avancée (jusqu'à la fin
  de sa dernière section atteinte) : le point de départ d'une **branche** (ci-dessous), où le
  faisceau cherche ce que les images d'une source ne refont pas (un combat).

**Dans la recherche** :

* **Les pistes de phase** (`SearchOptions::phaseLeads`, `--no-phase-leads` pour comparer) : quand la
  course est en avance d'au moins 0,25 s sur la meilleure course (ou un guide) au début d'une section,
  les images de cette source pour la section sont aussi offertes au faisceau **après l'attente qui
  les mène le plus loin sur leur chemin** (`Worker::phaseWait` : k de 0 à l'avance + 0,5 s, 8 s au
  plus, toutes les 4 images ; la première qui arrive, sinon celle qui va le plus loin, 1 s au moins) :
  une piste comme les autres (place gardée dans le faisceau, qui en part) ; son arrivée est classée,
  raccordée et vérifiée comme toutes (source `phase`). Le journal le dit (« phase : la course en
  avance de 6,20 s sur un guide ; ses images de la section 3 après 0,133 s d'attente »).
* **Les arrivées en avance gardées** (`SearchOutcome::early`) : à la fin de chaque section, les
  arrivées au moins 0,25 s avant la meilleure course qui ne se raccordent pas (les deux plus tôt par
  section, aussi saines ou plus) ; un entraînement avec `--phase` les réaligne après le tour.

**Dans un entraînement** (`cosmic_tas`) :

| Option | Rôle |
|---|---|
| `--realign <replay>` | les premières sections du replay gardées (jusqu'à chacune de ses sections, ou `--realign-through <n>`), la suite réalignée sur la meilleure course (`--seed-replay`), le replay lui-même et chaque `--realign-source <replay>` ; une course plus rapide est vérifiée et écrite ; `--realign-out <fichier>` : la plus rapide trouvée, plus rapide ou non ; sans `--until-stopped`, rien d'autre |
| `--phase` | au début de chaque tour dont les sources ont changé, chaque préfixe d'une source qui, suivi des meilleurs segments des autres, pourrait battre la plus rapide (0,1 s au moins ; les plus prometteurs d'abord), réaligné sur toutes, une avance par fil (`--threads`) ; après chaque tour, ses arrivées en avance |
| `--phase-source <replay>` | (plusieurs) une course de ce niveau relue **à chaque tour** (le meilleur fichier d'un autre entraînement) : une source du réalignement et un guide du tour ; implique `--phase` |
| `--phase-budget <minutes>` | le temps d'un réalignement (3) |
| `--phase-wait <s>`, `--phase-keep <n>` | l'attente la plus longue (8 s) ; les arrivées gardées par section (3) |
| `--phase-trace` | chaque essai dans le journal (mesures) |
| `--phase-min-wait <s>` | l'attente tentée même sans élément périodique rencontré (0,5 s) |
| `--study <replay> [de à]` | le chemin de Nova toutes les 0,25 s du chrono (pieds, vitesse, énergie, santé, pouvoirs portés) et les fins de section, rien d'autre |

**Mesure** (`sylvanox_01` ANY%, 1 fil, la machine partagée avec deux entraînements sur 4 cœurs) :

| Essai | Résultat |
|---|---|
| `--realign` de la course « fresh » (1:19.275, CP2 à 23,925 s) jusqu'au CP2, sur la meilleure course « main » (1:26.025) et sur elle-même | **1:19.175** vérifiée et écrite en 28 s (2 706 essais) : la section 7 de « main » après le CP6 de « fresh », 2 images d'attente (3,175 s au lieu de 3,258 s) ; les sections 3 à 6 restent celles de « fresh » |
| Les images de « main » pour la section 3 derrière le CP2 de « fresh » (`--phase-trace`) | sans attente, bloquées à la porte o171 ; dès 16 images d'attente (l'énergie de l'onde), la porte passée vers 26,4 s, mais chute aux plateformes o186 à o188 pour toute attente sous l'avance (6,1 s) : leurs périodes (6,76 et 7,4 s) dépassent l'avance ; la seconde attente arrive au mieux à 40,04 s |
| Piste de phase dans une branche (préfixe de « fresh » jusqu'au CP2, un tour de 6 min) | la piste « 0,133 s d'attente » met le faisceau après la porte à 27,1 s (x = 4589 ; « fresh » y restait jusqu'à 32,0 s) ; le faisceau attend ensuite le même cycle des plateformes : CP3 à 39,267 s au mieux (0,25 s d'avance), CP5 à 61,833 s (0,73 s), sans course plus rapide |
| Réalignement des arrivées en avance de ce tour (CP3 39,267 s, CP4 47,858 s, CP5 61,833 s) | aucune ne finit avec la suite d'une source : l'arène de la section 4 et le boss (section 6) dépendent de l'état (ennemis réveillés autrement, énergie, santé), pas d'une phase |
| `--realign` de « main » (1:12.975, le boss en 5,3 s) jusqu'au CP3, sur « main » et « fresh » (1:18.767), touches remappées | la section 4 de « fresh » (dash, onde) arrive au CP4 en 49,900 s au lieu de 51,367 s ; la section 5 de « main » au CP5 en 64,325 s (0,24 s d'avance) ; mais le boss de « main » ne se refait pas : la course gardée est « main » elle-même (1:12.975, sa suite pure), la voie jusqu'au CP5 écrite pour une branche |

Ce que cela dit : la phase compte là où le niveau est **périodique** (plateformes, lasers : la section 7, la porte de la section 3) ; les arènes et le boss ne se « réalignent » pas, ils se cherchent. **Le CP3 vers 39,5 s est fixé par le cycle des plateformes o186 et o188** (une arrivée au CP2 vers 22,6 s, ou un autre passage, le précédent) — la course de NovaRider (version 2.88 du niveau) l'a pris au cycle précédent (CP3 à 33,04 s). Le boss se bat en **5,3 s** dans la course « main » de la nuit (1:12.975, trouvée par son faisceau : la porte o345 s'ouvre quand le Gardien tombe), contre 13,45 s dans « fresh » ; ses images ne refont pas ce combat derrière une autre arrivée (même 0,24 s plus tôt et avec plus d'énergie) : un combat se cherche, il ne se rejoue pas — d'où la branche engagée sur le CP5 de « fresh » (2 s devant « main ») avec « main » comme guide. **La fin est quantifiée** par l'ascenseur o364 de la dernière section (période 6,12 s) : des arrivées au CP6 jusqu'à 0,52 s plus tôt que la course 1:12.358 (une branche, tour 1) finissent toutes, réalignées, en 1:12.358 — une avance plus courte que la période est absorbée par l'attente, et le classement par la fin mesurée ne la voit pas ; de même le CP3 avec les plateformes de la section 3. Pour gagner là : une période entière d'avance (la branche garde une avance qui ne paie pas encore).

### La branche

`--branch <replay> [--branch-through <n>]` (`tools/tas/Branch.h`) : un entraînement qui **s'engage**
sur les `n` premières sections du replay (2 par défaut : une arrivée en avance), gardées exactement,
et entraîne **la suite**, tour après tour, fenêtres comprises (elles tournent sur les sections après
le préfixe seulement), depuis **sa propre meilleure course** :

* sa meilleure course est **gardée même plus lente que la meilleure course** (vérifiée par la
  lecture normale des replays, écrite dans son propre fichier : `--branch-out <dossier>`, par défaut
  `<--out>/branch` ; jamais `data/tas`) ; au redémarrage, la branche repart de ce fichier si sa course
  commence par le préfixe ;
* `--branch-share <f>` : la part des tours de la branche (0,5 : un sur deux) ; un tour de la branche
  commence par réaligner le préfixe sur toutes les sources (sa meilleure course, la meilleure course,
  les guides, les `--phase-source`), puis cherche sa fenêtre ; sans course encore, il cherche depuis
  la fin du préfixe ;
* **le retour** : une course de la branche plus rapide que la meilleure course (dès le départ aussi) **est** la meilleure
  course (vérifiée et écrite comme d'habitude) ; dans les tours de la meilleure course, la meilleure
  course de la branche est un guide (ses sections offertes, réalignées, raccordées, assemblées) et
  une source du réalignement de phase.

**Mesure** (`sylvanox_01` ANY%, 1 fil, tours de 5 min) : une branche engagée sur les 5 premières
sections de « fresh » (1:18.767 ; CP5 à 62,567 s, 2 s devant « main ») avec « main » (1:12.958 ; le
boss en 5,3 s) comme meilleure course, donc comme guide : au premier tour, la deuxième passe du
faisceau sur le boss arrive au CP6 en **69,025 s** (6,46 s de combat ; « fresh » : 13,45 s), puis la
piste de phase de la section 7 de « main » (0,167 s d'attente : l'ascenseur o364, période 6,12 s)
finit : **1:12.367**, vérifiée par la lecture normale des replays et écrite (branche et meilleure
course), 1,2 min après le départ — plus rapide que les deux entraînements de la nuit (1:12.958 et
1:18.767), là où ni le réalignement seul (le combat ne se rejoue pas) ni « fresh » seul (ses fenêtres
sur le boss partent de sa propre course lente) n'y arrivaient.

### Vérification au fil de l'eau (2.99)

Chaque course entière plus rapide que la recherche tient (raccord, assemblage, course trouvée,
affinage) est confiée **aussitôt** à un fil de vérification (`SearchOptions::onRun`) pendant que la
recherche continue : vérifiée par la lecture normale des replays (ci-dessous), **écrite** si elle
est plus rapide, sinon rien. Une plus lente en attente est oubliée au profit de la plus rapide. La
progression le dit en quelques secondes : `candidateMs` et `verifying` (« candidat 1:26.642 ·
vérification… »), puis `bestMs`, ses temps aux points de passage et l'origine du gain
(`bestSplits`, `bestSource` : `beam` la recherche, `guide`, `splice` l'assemblage, `polish`
l'affinage, `planner` le planificateur ; avec le portefeuille, `explore` et `timing` ; `phase` le
réalignement de phase, `branch` une branche qui part plus vite que la meilleure course). Le tour
suivant part de la meilleure course vérifiée. Jamais une course non vérifiée n'est écrite.

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
