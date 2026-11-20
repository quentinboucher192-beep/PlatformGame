[← Retour à l'accueil (README)](../README.md)

# Parties de frappe des boss (2.76.0)

Un boss n'est plus seulement une boîte : son dessin est découpé en **parties nommées** (tête, cou, corps, ailes,
queue, pattes, griffes, souffle pour un dragon ; le corps pour les autres), chacune une union de quelques
**polygones convexes** qui épousent les pixels de **chaque image** de chaque animation.

## Les données

* **Les formes** sont dans `assets/hitparts/<feuille>.json`. Elles sont générées par
  `python3 tools/assetgen/hitparts.py` pour les 49 boss (classiques, gigaboss de la campagne et d'Asterion, échos
  compris), une fois par feuille : la sienne et celle de chaque phase.
  * Pour chaque case de la feuille qu'un clip montre, l'outil donne la liste `[partie, [x0, y0, x1, y1, …]]`.
  * Les sommets sont des coins de pixels de l'image, en entiers, avec l'origine en haut à gauche.
  * Une feuille de phase qui a exactement les masques de la feuille du boss ne fait que la nommer (`"sameAs"`).
  * Une feuille dont les masques diffèrent reprend les parties de la même case, pixel par pixel (la partie la plus
    proche).
  * Chaque partie est coupée en convexes jusqu'à épouser le dessin : un polygone fait au plus 1,25 fois la surface
    de ses pixels. Au plus 6 morceaux par partie et 10 sommets par morceau.
  * La régénération donne les mêmes octets ; `--check` compare sans écrire (CI Linux).
* **Les profils**
  * Le profil « dragon » (Pyrrhax) trouve la tête et le cou à partir du museau, et la queue à partir de son bout,
    par des distances le long du dessin. Les ailes sont reconnues à la couleur de leurs membranes, le souffle au feu
    du sprite. Ce qui reste épais est le corps ; le reste mince fait les pattes, avec leurs griffes au bout.
  * Le profil « silhouette » (tous les autres boss) donne le corps : le dessin sans les étincelles ni la poussière
    autour.
* **Les règles** sont dans `data/enemies/<id>.json`, sous `"parts"`, avec `"partsFromVersion"` à côté : la version
  de simulation à partir de laquelle le combat utilise les parties (Pyrrhax : 16 ; sans ce champ, la fiche montre
  les parties et le combat garde la boîte). Brancher un boss : [HITBOX_PARTS.md](HITBOX_PARTS.md). Sous `"parts"` :
  * `profile` : le découpage à utiliser ;
  * `roles` : pour chaque partie, son rôle (`weak` point faible, `normal`, `armour` blindée, `hurt` blesse) et son
    multiplicateur de dégâts (`damage`) ;
  * `hurtDuring` : les attaques pendant lesquelles la partie blesse aussi (la tête pendant la morsure).

  Ces règles ne font pas partie de la définition de l'ennemi (`EnemyDefinition`), ni donc de l'empreinte des
  niveaux classés : aucun record n'est effacé.

## Le combat par parties (Pyrrhax, `partsFromVersion` 16)

Valeurs validées : tête × 2 (point faible), cou, corps, ailes et pattes × 1, queue × 0,5 (blindée). Les griffes et
le souffle blessent ; la tête blesse aussi pendant la morsure.

2.76.2 (version 17) : tout son corps blessant au contact, Pyrrhax **pique les ailes repliées**, le nez en avant le
long de sa ligne (clips `telegraph_dive` et `dive` de son générateur, ajoutés après les autres : les images d'avant
sont identiques à l'octet). Son corps tient dans le couloir que montre l'annonce, et on l'esquive en courant hors de
ce couloir (`tests/test_pyrrhax.cpp`). En version 16, ses replays piquent ailes déployées, comme en 2.76.0.

* **La pose fait partie de la simulation** : l'animation d'un boss qui se bat par parties avance dans le pas fixe
  (`Enemy::fixedUpdate`). C'est la même image au même pas sur chaque ordinateur et dans chaque replay.
* **Les coups du joueur** : tirs, coups d'épée, bombes, boomerang. Ils touchent **la partie** qu'ils atteignent,
  par priorité : point faible, puis partie normale, puis blindage. Les dégâts sont multipliés par sa valeur
  (`reaches`, `partHitBy`). Une partie qui ne fait que blesser n'est jamais touchée.
* **Le contact** : en version 16, seules les parties qui blessent touchent le joueur (`hurtsWithParts`), ainsi que la
  tête pendant la morsure. Depuis la 2.76.2 (version 17, la règle des joueurs), **tout le corps** blesse au contact,
  au pixel près (`touchesWithParts`), et les parties qui blessent frappent en plus pendant leurs attaques avec leurs
  dégâts. La boîte entière ne compte plus ; les attaques gardent leurs zones.
* **La géométrie** : axes séparateurs entre un convexe et une boîte ou un cercle (`convexOverlapsRect`,
  `convexOverlapsCircle`). Les opérations sont faites dans un ordre fixe et le build interdit les contractions
  (`-ffp-contract=off`) : le résultat est le même partout. La boîte physique du boss (sol, murs, déplacements) ne
  change pas.
* **Les replays d'avant** : un replay de version 15 rejoue avec la boîte (`GameWorld::setSimulationVersion`). Son
  empreinte reste celle de la 2.75.2, et `tests/test_determinism.cpp` le vérifie sur le combat de Pyrrhax.
  `kRankingGeneration` reste 8 : les records sont gardés, comme le joueur l'a demandé.

## Les Gigaboss d'Asterion (2.76.2, `partsFromVersion` 17)

Les 8 Gigaboss d'Asterion se battent par parties (profil « regions », tableau dans [RAIDS.md](RAIDS.md)). Leurs
fiches de raid montrent leurs parties, et leur ancienne boîte en cadre blanc (les replays d'avant).

## Les gigaboss de la campagne (2.76.2, `partsFromVersion` 17)

Découpés par le profil « regions » (des régions par pose et par feuille, [HITBOX_PARTS.md](HITBOX_PARTS.md)), sur le
modèle de Pyrrhax :

| Gigaboss | Point faible × 2 | Blindé × 0,5 | × 1 | Blesse | Blesse pendant |
|---|---|---|---|---|---|
| Singularis | œil (à nu dès la phase 5) | coquille | mains | épines, couronne de feu | coquille : chute |
| Monolithe | tête | carapace (la boule entière quand il roule) | torse, bras, poings, jambes | — | poings : frappe, séismes |
| Phénix | tête | queue | corps, ailes | serres | — |
| Liche | crâne, phylactère (le cristal de son bâton) | traîne | robe, bâton | flammes d'âme | — |
| Titan Mécanique | tête, réacteur | blindage (épaulière, phases 1 à 4) | torse, canon, poing, jambes | cheminées | poing : frappe |
| Kraken | yeux | — | manteau, tentacules | bec | tentacules : fouets, étreinte |
| Astrarque | cœur | anneaux, plaques | coque, bras, chaînes | creusets, pointes | coque, plaques : ruées |

* **L'Astrarque porte la feuille du Forgeron primordial** (`forge_core`) : ses parties ont les noms de celles de ce
  boss. Une feuille partagée prend le profil du boss dont elle porte le nom quand il en a un, sinon celui du
  premier qui en a un : ici le « rig » du Forgeron primordial (ses parties nommées dans son générateur). Chacun des
  deux boss garde ses propres rôles (`data/enemies/astrarque.json`, `data/enemies/forge_core.json`) ; les régions
  de l'Astrarque ne servent plus que si le Forgeron perdait son profil.

* **Leurs mécanismes restent**, et s'ajoutent aux parties : le Monolithe roulé reste blindé (× 0,35 de son attaque,
  en plus de sa carapace) et s'expose étourdi ; le Phénix est intouchable dans ses cendres ; la Liche tant que son âme
  est dans le cristal de l'autel ; le Titan arrêté tant que ses vannes ne sont pas tournées, et son réacteur à nu
  multiplie encore les coups par 1,8. Les vannes et le cristal de l'autel gardent leur boîte.
* **Les tirs** partent de leur dessin : l'œil et les mains de Singularis, le cristal de la Liche, le bec du Phénix et du
  Kraken, le canon du Titan, l'œil et les poings du Monolithe. Les deux attaques que l'Astrarque emprunte (souffle de
  Cryowyrm, éclairs d'Ignivore) partent de son cœur et de son bras par `parts.origins`.
* **La feuille de phase** est prise dans le pas fixe (`kPartsLookSimulation`), la pose gardée : à toute fréquence
  d'image, la même partie au même pas.
* **Les replays d'avant** (versions 16 et 15) rejouent avec la boîte ; `tests/test_determinism.cpp` le vérifie sur le
  combat de chacun.

## Les boss classiques et leurs échos (2.76.2, `partsFromVersion` 17)

Les 16 gardiens des planètes et d'Aethra, Mycélia la Câline et les 16 échos du Noyau se battent par parties.

* **Leurs formes** (profil « rig ») viennent de leur générateur de sprites : chaque partie y est nommée au moment où
  elle est dessinée, et chaque pixel de chaque image porte sa partie. Un bras levé, une épée qui s'abat, une tête qui
  plonge sont donc à leur place sur chaque image.
* **Leurs rôles** suivent le modèle validé : tête, yeux et cœur exposé × 2 ; carapace (bosse, épaulières, plaques,
  anneaux, chapeau, glace) et queue × 0,5 ; le reste × 1. Les ronces, pointes, flammes, armes, tentacules, réacteurs
  et lianes blessent ; le poing, le bras, la gueule ou les têtes blessent aussi pendant leur attaque (`hurtDuring`).
* **Leurs tirs** partent de leur dessin : les origines manquantes ou fausses sont données par `"origins"` dans leurs
  parties (le poing et le cœur du Thornwarden et de l'Archonte, la lance d'Aurorax, le chapeau de Mycélia, les yeux de
  Nyxara, l'œil de Pyrogyre, le marteau du Cœur du Creuset).
* **Un écho** prend les parties de son original (`PartRules::of`, `"variantOf"`).
* **Les replays d'avant** (15, 16) rejouent avec la boîte et les origines d'avant : `tests/test_determinism.cpp` le
  vérifie sur les combats de Magmaroth et du Cryowyrm ; `tests/test_hitparts_classics.cpp` vérifie les rôles, les
  images, les tirs et un échantillon de combats.

En 2.76.2, les 49 boss se battent tous par parties à partir de la version de simulation 17 : Pyrrhax depuis la 16,
les Gigaboss d'Asterion, les gigaboss de la campagne, les boss classiques et leurs échos depuis la 17.

## Les fiches « Où frapper »

* **Boss classiques et gigaboss de la campagne** : `STATISTIQUES › boss › Où frapper` (`ui/widgets/HitPartsView`).
  La fiche montre le vrai sprite, avec une puce par phase dont la feuille change et une puce par pose (repos,
  élan, attaque…). Sur l'image en cours, les parties sont remplies à la couleur de leur rôle et la boîte est tracée
  en cadre blanc. Une légende mesurée donne le rôle et le multiplicateur de chaque partie, et dit comment le combat
  les utilise.
* **Motifs (2.76.2)** : chaque partie a un contour épais sur un contour sombre et un motif propre à son rôle
  (`drawPartPattern`). Le point faible est quadrillé, la partie normale rayée en long, la partie blindée rayée en
  travers (ses plaques) et la partie qui blesse à grosses rayures obliques. Chaque trait est posé sur un trait
  sombre : la partie reste lisible sur un sprite de n'importe quelle couleur, comme la cuisse grise de Titanus. Les
  légendes gardent les couleurs et montrent le motif (`drawPartSwatch`).
* **Gigaboss d'Asterion** : `FICHES D'ASTERION › Mécanismes › Où frapper`. Leurs parties sont dessinées sur
  l'image de chaque pose, et leur ancienne boîte en cadre blanc.
* **Captures** :
  * `CosmicPlatformer --headless --boss-stats <boss>:frapper[=<clip>]` ;
  * `--raid-sheets <fiche>:1:frapper=<n>[:pose=<attaque>]`.

## Tests

* `tests/test_hitparts.cpp` :
  * parties de chaque image de chaque boss, convexes, dans leur image ;
  * règles de Pyrrhax ;
  * axes séparateurs ;
  * coups par parties en 16 et par boîte en 15 ;
  * contact par les seules parties qui blessent.
* `tests/test_determinism.cpp` : empreintes du combat de Pyrrhax en 15 et en 16.
* `tests/test_enemy_parts_editor.cpp` (2.76.2) :
  * fichiers réécrits à l'octet près ;
  * un changement ne touche que les rôles ;
  * verrou des natives ;
  * panneau sans chevauchement (1280 × 720 et 1920 × 1080, français et anglais).
* 2.76.2, gigaboss de la campagne :
  * `tests/test_hitparts.cpp` : leurs règles, un coup sur le point faible × 2 et sur le blindage × 0,5, une partie hors
    de l'ancienne boîte touchée en 17 et manquée en 16, les parties qui blessent (et pendant quelle attaque), leurs
    tirs partis de la bonne partie, la feuille de phase prise dans le pas fixe ;
  * `tests/test_determinism.cpp` : le combat de chacun en 15, 16 et 17, et le même combat à toute fréquence d'image.

---

[← Retour à l'accueil (README)](../README.md)
