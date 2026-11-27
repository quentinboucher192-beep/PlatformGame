[← Retour à l'accueil (README)](../README.md)

# Courses de la machine (TAS)

Pour chaque niveau de la campagne et chaque catégorie des classements (ANY%, 100 %, Sans mort,
Sans dégât), une **course calculée par la machine** : un programme joue le moteur du jeu, qui est
déterministe, et garde la course la plus rapide qu'il trouve. C'est le **meilleur temps trouvé par
la machine**, jamais un optimum prouvé : une recherche plus longue, ou meilleure, peut trouver
plus vite.

* Un seul algorithme, le même pour tous les niveaux et toutes les catégories : aucun niveau n'est
  scripté, rien dans la recherche ne connaît un niveau.
* La recherche tourne **hors ligne, sur nos machines** (`cosmic_tas`, jamais installé), jamais
  sur l'ordinateur d'un joueur.
* Le résultat est un **replay ordinaire**, au format du jeu, que le vrai moteur rejoue exactement.
* Il est rangé avec les niveaux (`data/tas/`) et n'entre **jamais** dans les classements
  officiels : rien de ce qui suit ne passe par Cosmic Online, les records ou les classements.

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

La course trouvée est **vérifiée** avant d'être écrite ; un fichier qui garde une course plus
rapide (ou égale) du même niveau et de la même simulation n'est pas remplacé.

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
course à battre, ses états rejoignent les faisceaux de chaque section tant que la recherche la
suit, et l'affinage part d'elle si rien n'est plus rapide.

## Vérification

Avant d'écrire quoi que ce soit (`tools/tas/Output.cpp`) :

1. les entrées sont jouées dans un monde ordinaire (tas normal, réglages d'une vraie partie),
   enregistrées par l'enregistreur du jeu (`ReplayRecorder`, comme `GameScene` enregistre une
   course officielle), l'outro jouée jusqu'au bout comme un joueur ;
2. le replay obtenu, tel qu'il est écrit, est rejoué par la lecture normale des replays
   (`ReplaySimulator` sur un monde neuf, comme le lecteur de replays) : aucune désynchronisation,
   le niveau terminé, le **temps annoncé** au chronomètre ;
3. la catégorie est vérifiée avec les règles des records (`eligibleCategories`).

Sinon la course est refusée et rien n'est écrit.

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
combat de boss (le giga-boss du Noyau en phase 5), N pas, un état sauvegardé, M pas ; restauré, les
mêmes M pas redonnent les mêmes empreintes **et les mêmes octets de toute l'arène** (une mémoire
d'état extérieure au monde qui influerait sur la course se verrait) ; après d'autres entrées puis
une nouvelle restauration, la même course encore, la mémoire libérée par la restauration écrasée
(rien ne doit encore s'en servir). Un monde dans une arène joue exactement comme un monde sur le
tas normal.

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
