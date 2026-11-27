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

`phase` : `starting`, `searching`, `verifying`, `stopped`, `failed` ; `bestMs` : la meilleure course
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
course à battre, ses états rejoignent les faisceaux de chaque section tant que la recherche la
suit, et l'affinage part d'elle si rien n'est plus rapide.

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
  `saves/tas_local/`), et le verrou est rendu **aussitôt** : les autres voient la paire libre, et le
  temps de la session s'ajoute au total. **Fermer le jeu** fait de même pour chaque entraînement
  (les verrous rendus avant de quitter ; un plantage ferme l'entrée standard des enfants, qui
  s'arrêtent seuls ; le verrou expire alors en trois minutes).
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
