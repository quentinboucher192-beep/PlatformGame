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

* **La pose fait partie de la simulation** : l'animation d'un boss qui se bat par parties avance dans le pas fixe
  (`Enemy::fixedUpdate`). C'est la même image au même pas sur chaque ordinateur et dans chaque replay.
* **Les coups du joueur** : tirs, coups d'épée, bombes, boomerang. Ils touchent **la partie** qu'ils atteignent,
  par priorité : point faible, puis partie normale, puis blindage. Les dégâts sont multipliés par sa valeur
  (`reaches`, `partHitBy`). Une partie qui ne fait que blesser n'est jamais touchée.
* **Le contact** : seules les parties qui blessent touchent le joueur (`hurtsWithParts`), ainsi que la tête pendant
  la morsure. La boîte entière ne compte plus ; les attaques gardent leurs zones.
* **La géométrie** : axes séparateurs entre un convexe et une boîte ou un cercle (`convexOverlapsRect`,
  `convexOverlapsCircle`). Les opérations sont faites dans un ordre fixe et le build interdit les contractions
  (`-ffp-contract=off`) : le résultat est le même partout. La boîte physique du boss (sol, murs, déplacements) ne
  change pas.
* **Les replays d'avant** : un replay de version 15 rejoue avec la boîte (`GameWorld::setSimulationVersion`). Son
  empreinte reste celle de la 2.75.2, et `tests/test_determinism.cpp` le vérifie sur le combat de Pyrrhax.
  `kRankingGeneration` reste 8 : les records sont gardés, comme le joueur l'a demandé.

2.76.1 : les 8 Gigaboss d'Asterion se battent par parties dès la version 17 (profil « regions », tableau dans
[RAIDS.md](RAIDS.md)). Les autres boss gardent leur boîte jusqu'à leur étape ; leurs fiches montrent déjà leurs parties.

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
  l'image de chaque pose ; leur combat se joue encore sur leur boîte.
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

---

[← Retour à l'accueil (README)](../README.md)
