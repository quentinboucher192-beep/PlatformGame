[← Retour à l'accueil (README)](../README.md)

# Brancher un boss sur le combat par parties

Guide pratique (2.76) ; le système lui-même est décrit dans [HITPARTS.md](HITPARTS.md). Pyrrhax est l'exemple
complet (`data/enemies/pyrrhax.json`, `tests/test_hitparts.cpp`).

## 1. Ses formes : `assets/hitparts`

* `python3 tools/assetgen/hitparts.py --only <id>` régénère les feuilles d'un boss (sans `--only` : les 49 boss).
* Vérifiez avec `python3 tools/assetgen/hitparts.py --check` : il ne doit rien rester à régénérer, et c'est aussi
  vérifié en CI.
* **Profil** : `"parts": {"profile": "dragon"}` donne le découpage en tête, cou, corps, ailes, queue, pattes,
  griffes et souffle (un dragon de profil, tourné vers la droite). Sans profil, le découpage est « silhouette » :
  une seule partie, `corps`, en plusieurs convexes.
* **Profil « regions » (le plus rapide pour un boss)** : nommez quelques régions par des polygones dessinés sur
  son image de repos, en pixels de l'image (origine en haut à gauche) :
  `"parts": {"profile": "regions", "regions": [{"name": "crâne", "poly": [x0, y0, x1, y1, …]}, …], "roles": {…}}`.
  * Chaque pixel du dessin, sur chaque image, va à la première région qui le contient, sinon à la région dont le
    centre est le plus proche.
  * Ce profil convient aux boss qui gardent leur forme. Les parties qui bougent beaucoup (un bras levé) suivent
    moins bien : vérifiez-les sur les images d'attaque.
  * 2.76.2, pour les parties qui bougent ou changent (exemples : les gigaboss de la campagne) :
    * `"clips": ["attack", "telegraph:3-5", "roll:0"]` : la région ne vaut que pour ces poses, un clip entier, une
      image (`attack:2`) ou des images (`attack:1-3`). Dessinez-la sur l'image de cette pose (poings levés, boule) ;
    * `"sheets": ["singularis_p5", …]` : seulement pour ces feuilles, par exemple une phase redessinée (œil à nu) ;
    * `"rest": true` : ce qu'aucune région ne contient va à celle-ci, au lieu du centre le plus proche ;
    * `"drop": true` : ce qu'elle contient n'est aucune partie, par exemple un anneau ou un tourbillon dessiné autour
      du boss ; avec `rest`, tout ce qu'aucune région ne contient est écarté ;
    * plusieurs régions de même nom font une seule partie. L'ordre compte : la première qui contient un pixel le
      prend, donc la pose particulière passe avant la région générale.
* **Nouveau profil** : pour un découpage qui suit vraiment l'anatomie, ajoutez une fonction dans l'outil, sur le
  modèle de `dragon()`, et nommez ses parties dans une liste comme `DRAGON`. Elle doit rester déterministe : des
  entiers seulement pour les décisions, aucun hasard, des ordres fixes.
* **Contrôle visuel** : vérifiez sur quelques images (repos, élan, attaque) que les formes suivent le dessin.
  `CosmicPlatformer --headless --boss-stats <id>:frapper=<clip> --shot …` montre l'onglet « Où frapper ».

## 2. Ses règles : `data/enemies/<id>.json`

```json
"parts": {
  "profile": "dragon",
  "roles": {
    "tête":    {"role": "weak",   "damage": 2, "hurtDuring": ["morsure"]},
    "cou":     {"role": "normal", "damage": 1},
    "queue":   {"role": "armour", "damage": 0.5},
    "griffes": {"role": "hurt"},
    "souffle": {"role": "hurt"}
  }
},
"partsFromVersion": 17
```

* **Rôles** :
  * `weak` est un point faible ;
  * `normal` est une partie normale (rôle par défaut d'une partie sans règle, × 1) ;
  * `armour` est une partie blindée ;
  * `hurt` est une partie qui ne fait que blesser : elle n'est jamais touchée, elle blesse au contact.
* **Modèle validé par le joueur** : tête ou yeux × 2, carapace ou queue × 0,5, le reste × 1, et les griffes, le
  souffle, les dards ou les pointes qui blessent. Adaptez-le à chaque boss.
* `hurtDuring` : les identifiants des attaques pendant lesquelles une partie blesse aussi. Exemple : la tête pendant
  `morsure`. Une ruée (`lunge`) d'un boss par parties ne blesse que par ces parties.
* `partsFromVersion` : la version de simulation à partir de laquelle son combat utilise les parties. Les boss
  des étapes c, d et e utilisent 17. Un replay d'une version plus ancienne rejoue avec la boîte. Sans ce champ, la
  fiche montre les parties et le combat garde la boîte.
* Les parties ne figurent ni dans `EnemyDefinition` ni dans l'empreinte des niveaux classés : n'y mettez rien, les
  records restent comparables (`kRankingGeneration` reste 8).
* `"origins": {"<attaque>": [x, y]}` (2.76.2, sous `"parts"`) : l'origine des tirs d'une attaque quand le boss se
  bat par parties, en pixels du sprite depuis les pieds (comme `origin`). Elle corrige une origine hors du dessin
  sans toucher à la définition de l'attaque, donc ni à l'empreinte de ses niveaux ni aux replays d'avant (exemple :
  l'Astrarque, dont deux attaques empruntées partaient du vide).

### Les régler dans l'éditeur d'ennemis (2.76.2)

* **Où** : `Éditeur d'ennemis › Boss › Parties (où frapper)` (`editors/enemy_editor/EnemyEditorParts.cpp`). Le
  panneau montre le sprite du boss avec la planche, la pose et l'image choisies (ou la pose animée), et ses parties
  à la couleur et au motif de leur rôle. La partie choisie est cerclée de blanc ; on la choisit d'un clic sur le
  sprite, ou dans la liste au clavier et à la manette.
* **Ce qui se règle**, pour la partie choisie :
  * son rôle (point faible, partie normale, blindée, qui blesse) ;
  * son multiplicateur de dégâts (0 à 4, par quarts) ;
  * ses `hurtDuring`, une case par attaque du boss.
* **Qui** : comme pour les entités natives (2.45), les parties d'un boss du jeu sont en lecture seule pour le
  joueur. Seul un administrateur (`ADMINISTRATION › Ennemis`) les règle. Les entités du joueur se battent avec leur
  boîte : leurs parties sont montrées, sans règles.
* **Où ça va** : « Enregistrer les parties » (ou Ctrl+S) réécrit le seul bloc `roles` de `data/enemies/<id>.json`
  (`savePartRules`). Les autres clés, leur ordre, l'ordre des rôles et l'indentation sont gardés : un fichier
  réécrit avec ses propres règles revient à l'octet près. Le combat de l'arène repart avec les nouvelles règles.
  « Publier… » n'envoie pas les parties : elles partent avec la prochaine version du jeu.
* **Attention** : changer les règles d'un boss qui se bat déjà par parties change ses combats, et les replays
  enregistrés ne se rejouent plus à l'identique. Le panneau le rappelle. Une telle modification demande une nouvelle
  version de simulation (section 6).
* **Capture** : `CosmicPlatformer --enemy-editor <boss> --enemy-parts`.

## 3. Ce que le moteur fait (rien à coder pour un boss ordinaire)

* **La pose** : l'animation d'un boss qui se bat par parties avance dans `Enemy::fixedUpdate`, ce qui en fait une
  donnée de simulation (`fightsByParts`). À partir de la version 17 (`kPartsLookSimulation`), il prend aussi la
  feuille de sa phase dans le pas fixe, sa pose gardée (`Animator::swapSet`), jamais dans une mise à jour d'affichage
  dont le rythme varie : ses parties sont les mêmes à toute fréquence d'image.
* **Les coups du joueur** : `GameWorld` (tirs, `damageEnemies`) et les pouvoirs à rayon (boomerang) passent par
  `reaches()`, qui touche la partie atteinte, par priorité point faible, puis normale, puis blindée. Les dégâts
  sont multipliés par son `damage`.
  * **Nouveau code qui frappe un ennemi** : utilisez `reaches(e, w, zone, &mult)` (boîte ou cercle), jamais
    `e.box.intersects`. Sinon le boss serait touché par sa boîte.
* **Le contact** : `Enemy::contactDamage` ne blesse que par les parties `hurt` et celles de `hurtDuring`
  (`hurtsWithParts`).
* **Les attaques à zone propre** (lasers, ondes, plongeons, routines des Gigaboss) gardent leurs zones. Si une
  attaque doit partir d'une partie, réglez son `origin`.

## 4. L'origine des tirs

`origin` est en **pixels du sprite** depuis les pieds (le pivot), multiplié par `scale` par le moteur
(`attackOriginOf`). Il ne s'écrit jamais en unités du monde, faute de quoi les tirs partent loin du dessin, comme
pour les Gigaboss avant la 2.75.2. Mesurez sur l'image de l'attaque (clip `attackAnim`) : la bouche, les yeux,
le canon.

## 5. Les tests à écrire

* **Rôles du boss** : les rôles et `partsFromVersion` lus par `PartRules::of(id)`, sur le modèle du test
  « Pyrrhax's rules ».
* **Combat par parties** : dans une arène vide avec `setSimulationVersion(17)`, puis avec 16, sur le modèle de
  `Lair` :
  * la tête (ou les yeux) prend × 2 et la queue × 0,5 ;
  * une partie hors de l'ancienne boîte est touchée en 17 et manquée en 16 ;
  * seules les parties qui blessent blessent (`hurtsWithParts`).
* **Origines** : elles tombent sur le dessin de leur attaque, avec le même test de pixels que
  `tests/test_raid_hitbox_sprites.cpp`.
* **Couverture** : `tests/test_hitparts.cpp` vérifie déjà les parties de chaque image de chaque boss (49 boss).

## 6. Les empreintes à mettre à jour

* **Solo** : un boss d'un niveau à empreinte de référence (`tests/test_determinism.cpp`) change son empreinte en
  version 17. Gardez l'ancienne valeur pour une course en 16, comme le test de Pyrrhax le fait pour 15 contre 16,
  et notez la nouvelle.
* **Raids** : un Gigaboss d'Asterion change le combat de son antre.
  * Mettez à jour les empreintes de raid de vos combats (`tests/test_raid*.cpp`), et elles seules.
  * Montez `TITAN_REVISION` au numéro libre suivant (`tools/levelgen/raid_asterion.py`, puis `titan.json` et
    `server/src/raidTitan.json` régénérés), après avoir fusionné la dernière branche de développement. Une instance
    ne mélange jamais deux versions.
* **Coopération** : `kCoopSimulationVersion` ne bouge pas. Les replays d'équipe portent la version de simulation,
  ils rejouent avec leurs propres règles.
* **Journal** : sous la clé de la version (`"2.76.1"`) ; les autres agents écrivent sous la même clé, gardez les
  deux côtés à la fusion.

---

[← Retour à l'accueil (README)](../README.md)
