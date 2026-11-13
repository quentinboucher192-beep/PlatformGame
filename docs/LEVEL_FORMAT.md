# Format des niveaux (`cosmic.level`, version 1)

Un niveau est un fichier JSON unique. Les niveaux officiels sont dans `data/levels/`, les
créations dans `levels/` du dossier utilisateur, les niveaux importés dans
`packages/<id>/`. Toutes les coordonnées sont en pixels (tuile = 16 px), origine en haut à
gauche, y vers le bas. Les chemins d'assets sont relatifs au dossier `assets/`.

L'éditeur de niveaux lit et écrit ce format ; le niveau officiel
`data/levels/sylvanox_01.json` est généré par `tools/levelgen/sylvanox_01.py`.

## En-tête

```json
{
  "format": "cosmic.level", "formatVersion": 1,
  "id": "sylvanox_01", "name": "Les Ruines Suspendues", "description": "…",
  "planet": "sylvanox", "author": "Cosmic Studio", "version": "1.0", "difficulty": 2,
  "music": "music/sylvanox_level.ogg", "bossMusic": "music/sylvanox_boss.ogg",
  "ambience": "music/sylvanox_ambience.ogg",
  "bounds": [0, 0, 10560, 1216], "killY": 1240, "cameraZoom": 1.0,
  "ambientLight": "#7480a8", "backgroundColor": "#0b0f1e",
  "background": [ { "texture": "backgrounds/sylvanox/sky.png", "factor": [0, 0], "fill": true }, … ],
  "timer":  { "mode": "limited", "limit": 900, "onExpire": "gameover", "referenceTime": 210, "challengeMaxDeaths": 0 },
  "medals": { "bronze": {}, "silver": { "maxTime": 420 }, "gold": { "maxTime": 270 },
              "platinum": { "maxTime": 200, "maxDeaths": 0 } },
  "startPowers": [],
  "layers": [ … ], "objects": [ … ], "events": [ … ],
  "intro": { … }, "outro": { … }, "metadata": { … }
}
```

* `background` : couches de parallaxe, de la plus lointaine à la plus proche (`factor` =
  vitesse relative à la caméra, `fill`, `offset`, `repeatX`, `tint`…).
* `timer.mode` : `unlimited`, `limited` (temps maximum), `timeAttack` (chrono mis en avant,
  pas de limite), `challenge` (limite + nombre de morts maximum).
  `onExpire` : `gameover`, `restartCheckpoint` ou `continue`. `referenceTime` sert au score.
* `medals` : pour chaque médaille, `maxTime` (secondes) et/ou `maxDeaths` ; un objet vide
  = obtenue en terminant le niveau.
* `startPowers` : identifiants des pouvoirs disponibles dès le départ.

## Calques

```json
{ "id": "terrain", "name": "Terrain", "order": 10, "visible": true, "locked": false }
```

`order` fixe l'ordre de rendu ; `"foreground": true` dessine le calque devant le joueur.
Le niveau officiel utilise `bg_decor`, `terrain`, `gameplay`, `fg_decor`.

## Objets

```json
{ "id": "o186", "type": "moving_platform", "layer": "gameplay",
  "rect": [4864, 768, 48, 16], "rotation": 0, "flipX": false, "z": 0,
  "props": { "dx": 144, "dy": 0, "speed": 45, "wait": 0.5, "phase": 0, "active": true,
             "triggered": false, "oneway": false } }
```

`props` dépend du type ; le schéma de chaque type (clé, libellé, type, valeur par défaut,
bornes, choix possibles) est défini dans `game/levels/ObjectTypes.cpp` et sert à la fois au
jeu, à l'éditeur (panneau de propriétés) et à l'exporteur (dépendances). Une propriété
absente prend sa valeur par défaut.

| Catégorie | Types |
|---|---|
| Terrain | `solid` — `material` : `ruin_stone`, `ancient_metal`, `crystal`, `root` (Sylvanox), `basalt`, `obsidian`, `forge_metal` (Pyralis), `glacier_ice` (glissante), `snow_rock`, `frost_metal` (Glacior) |
| Plateformes | `oneway` (`style` : `ruin_bridge`, `tech_ledge`, `root_branch`, `basalt_ledge`, `chain_grate`, `ice_ledge`, `snow_bridge`), `moving_platform`, `crumble_platform` (`style` : `ruins`, `basalt`, `ice`), `power_gate` (`power` : `dash` mur de cristal, `teleport` barrière de phase, `wave` barrière d'énergie — aussi dissoute par le Tir chargé —, `impulse` rocher fissuré, `shield` rideau de plasma, `ground_pound` sol fissuré), `bounce_pad`, `ladder` |
| Dangers | `spikes`, `liquid` (`kind` : `acid`, `water`, `lava`, `icewater` — l'eau et l'eau glacée se nagent), `damage_zone`, `slow_zone` (`style` du nuage : `spores`, `frost`, `ash`), `laser` |
| Gameplay | `player_start`, `checkpoint` (`label` ; `optional` : point d'une route alternative ou d'un détour, que le détecteur de raccourcis n'exige pas), `exit`, `shard`, `relic`, `health_orb` (`amount`, 2 par défaut), `energy_orb`, `power_unlock` (`power` parmi les 10 pouvoirs), `heart_container` (`hearts` : cœurs maximum en plus pour le niveau) |
| Interactifs | `door`, `lever`, `receptor` (activé par l'Onde ou le Tir chargé), `sign`, `grapple_point` (point d'ancrage du Grappin) |
| Ennemis | `enemy` (`enemy` = identifiant de définition, `patrol`, `faceLeft`, `dormant`, `boss`) |
| Zones | `trigger`, `combat_zone` (`doors`, `waves` = vagues d'ennemis séparées par `;`, `lockCamera`, `music`, `announce`), `camera_zone`, `secret_area`, `updraft` (courant ascendant : `strength` vitesse de montée, `vent` grille au sol ; activable par événements) |
| Décor | `decor` (élément d'un atlas de décor : `sprites/decor/<planète>_decor.json`, noms uniques), `light`, `emitter` (particules) |

Les références entre objets (portes d'une arène, cibles d'un levier, vagues…) utilisent
les `id` d'objets.

## Événements

```json
{ "id": "ev1", "name": "Gardien vaincu", "source": "o349", "trigger": "bossDefeated",
  "once": true, "delay": 1.5,
  "actions": [ { "type": "hint", "target": "", "params": { "text": "Le passage est ouvert.", "duration": 4 } } ] }
```

* `source` : `id` de l'objet surveillé, `level` pour les déclencheurs globaux, ou
  identifiant de pouvoir pour `powerUsed` / `unlocked`.
* `trigger` : `enter`, `exit`, `activate`, `cleared`, `start`, `killed`, `bossStart`,
  `bossPhase`, `bossDefeated`, `collected`, `checkpoint`, `powerUsed`, `unlocked`,
  `levelStart`, `time` (avec `params.seconds`).
* `actions[].type` : `open`, `close`, `toggle`, `activate`, `disable`, `enable`, `spawn`,
  `startBoss`, `showText`, `hint`, `dialogue`, `cameraPan`, `lockCamera`, `unlockCamera`,
  `shake`, `flash`, `playMusic`, `playSound`, `effect`, `addTime`, `givePower`, `heal`,
  `killPlayer`, `completeLevel` ; `target` = `id` de l'objet visé.

Les événements sont interprétés par `EventSystem` : ce sont des **données**, jamais du code.

## Cinématiques

```json
"intro": { "skippable": true, "letterbox": true, "steps": [
  { "type": "fade", "duration": 0.01, "params": { "to": 1 } },
  { "type": "cut", "duration": 0.01, "params": { "x": 2000, "y": 640 } },
  { "type": "camera", "duration": 3.0, "wait": true, "params": { "x": 600, "y": 700, "ease": "inOutSine" } },
  { "type": "text", "duration": 4.0, "wait": false, "params": { "text": "SYLVANOX", "subtitle": "Les Ruines Suspendues" } }
] }
```

Types d'étapes : `camera`, `focus`, `cut`, `zoom`, `fade`, `flash`, `shake`, `text`,
`dialogue`, `sound`, `music`, `effect`, `letterbox`, `spawnPlayer`, `hidePlayer`,
`playerWalk`, `playerAnim`, `action` (exécute une action d'événement), `wait`.
`wait: false` lance l'étape suivante sans attendre la fin de celle-ci.

## Compatibilité

`formatVersion` est vérifié au chargement ; un niveau d'une version future est refusé avec
un message clair. Les champs inconnus sont ignorés, les champs absents prennent leur valeur
par défaut, ce qui permet d'ajouter des propriétés sans casser les anciens niveaux.

## Plafond et détecteur de raccourcis

Chaque niveau a un plafond invisible 48 px au-dessus du haut de ses limites (`bounds`), et
un bloc `solid` qui touche le haut du niveau monte jusqu'à ce plafond : on ne sort jamais par
le haut et on ne passe jamais au-dessus d'un mur qui atteint le haut du niveau.

`CosmicPlatformer --analyze-levels <dossier>` analyse les niveaux de la campagne
(`game/levels/LevelAnalyzer`) : espace où tient le joueur, hauteur et distance qu'il peut
franchir avec ses pouvoirs dans ce niveau, route la plus courte comparée à la route qui passe
par tous les points de contrôle (non optionnels), portes fermées au départ (arènes, leviers)
contournables. Il écrit une carte PPM par niveau (jaune : route la plus courte, bleu : route
prévue, magenta : contournement d'une porte). `--seal-levels tools/levelgen/seals` ajoute des
colonnes depuis le plafond aux endroits contournés (sans toucher aux secrets, reliques, cœurs,
lianes, courants ni ancrages) ; `tools/levelgen/levelkit.py` les intègre aux niveaux générés.

## Note de difficulté (calculée)

La difficulté affichée (carte des planètes, fiche du niveau, éditeur) n'est **pas saisie** :
elle est calculée à partir du niveau par `game/levels/DifficultyRating.cpp` (formule versionnée,
`kRatingVersion`). Le champ `difficulty` du fichier n'est plus qu'un arrondi en étoiles de cette
note, gardé pour la compatibilité.

Le parcours prévu (départ → chaque point de passage dans l'ordre → sortie, donné par le
détecteur de raccourcis) est parcouru et on mesure ce que le joueur y rencontre :

| Composante | Mesure |
|---|---|
| Sauts | chaque passage en l'air comparé au saut complet du joueur (double saut compris : ~165 px de portée, ~100 px de hauteur avec `data/player.json`) ; au-delà de 55 % le saut compte, d'autant plus qu'il est long ou haut, **×2 si un raté est mortel** (lave, acide, pics, vide) |
| Pièges | pics (longueur, dégâts, mort instantanée), lasers (part du temps allumés), liquides mortels traversés, zones de dégâts, plateformes fragiles (délai court, sans réapparition), plateformes mobiles, glace glissante sous les pieds, points d'ancrage |
| Ennemis | ennemis à moins de 192 px du parcours : santé, dégâts, rapidité des attaques (temps d'annonce), attaques à distance, vol, téléportation, garde, renforts |
| Boss | temps qu'il faut au joueur pour le vaincre comparé au temps qu'il lui faut pour vaincre le joueur (dégâts par seconde de ses attaques, pondérés par la difficulté à les esquiver selon leur temps d'annonce, phases) |
| Points de passage | distance moyenne et maximale entre deux points de passage (ce que coûte une erreur) |
| Soins | orbes de santé et cœurs par 100 tuiles (fait **baisser** la note) |
| Temps | temps limité serré par rapport à la longueur du parcours |

Sauts, pièges et ennemis sont des **densités par 100 tuiles** de parcours. La somme pondérée
passe par une courbe saturante `5 × (1 − exp(−(brut − 6) / 8))`, arrondie à la demi-étoile.

* Campagne : `CosmicPlatformer --rate-levels data/levels/ratings.json` écrit les notes (avec le
  détail de chaque composante) ; un test vérifie que ce fichier correspond à la formule.
  `CosmicPlatformer --rate-levels` seul affiche le détail.
* Niveaux des joueurs et importés : notés en arrière-plan par le jeu (le fil principal
  n'attend jamais) ; l'éditeur affiche la note et son résumé dans *Niveau* (bouton
  *Recalculer*).

Limite connue : c'est une estimation à partir de la géométrie et des données ; elle ne joue
pas le niveau. Le détecteur suppose un joueur très habile (il prend la route la plus courte
possible) : les sauts mesurés sont ceux de cette route.
