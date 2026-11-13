# Architecture

Cosmic Platformer est écrit en C++20 sur SDL3 (rendu 2D `SDL_Renderer`, entrées, audio via
SDL3_mixer, images via SDL3_image, texte via SDL3_ttf). Le contenu (niveaux, planètes,
ennemis, pouvoirs, effets, joueur) est **entièrement décrit en JSON** : le code fournit des
comportements, les données les paramètrent.

```
main.cpp                    options de ligne de commande, création de l'Application
engine/                     → bibliothèque cosmic_engine (aucune règle de jeu)
game/ ui/ editors/          → bibliothèque cosmic_game
packaging/ saves/
data/                       contenu JSON officiel
assets/                     sprites, tuiles, fonds, sons, musiques, polices
tools/                      générateurs d'assets (Python) et de niveaux (DSL Python)
tests/                      tests unitaires et de jouabilité (cosmic_tests)
installer/                  installateur Windows (Inno Setup), icône, ressources
server/                     Cosmic Online : API Cloudflare Workers + D1 + R2 (TypeScript, indépendant du C++)
```

## Moteur (`engine/`)

| Module | Rôle |
|---|---|
| `core/Application` | Fenêtre, pile de scènes avec fondus, boucle **à pas fixe 120 Hz** + interpolation de rendu, F3 / F11 / F12, mode headless (tests, captures automatiques). |
| `core/Paths`, `FileUtil`, `Json`, `Log`, `Random`, `Math`, `Easing` | Dossiers d'installation et utilisateur, écritures atomiques, validation de chemins relatifs, utilitaires. |
| `rendering/` | `Renderer` (caméra monde → écran, primitives, textures), `Text` (cache de glyphes TTF), `ParticleSystem` (préréglages JSON), `Parallax` (couches de fond) et `LightMap` (éclairage dynamique par cible de rendu en mode multiplicatif). |
| `animation/` | Feuilles de sprites JSON : clips, durées par image, boucles, enchaînements, **événements** (pas, impacts, sons, VFX). |
| `physics/` | `PhysicsWorld` : AABB, grille spatiale uniforme, résolution par axes séparés, plateformes traversables, étiquettes (phase, friable, mobile, porte, rebond, ennemi seul, passage bouclier), SAT pour les boîtes orientées. |
| `camera/` | Suivi avec anticipation, zones de caméra (verrouillage, bornes, zoom), scripts (déplacement, zoom), tremblement par traumatisme. |
| `input/` | Actions abstraites, profils clavier et manette indépendants, zones mortes, sensibilité, inversion, vibrations, réaffectation, glyphes dynamiques, bascule automatique clavier ↔ manette, scripts d'entrée pour les tests. |
| `audio/` | Mixeur : 32 pistes d'effets, musique en fondu enchaîné sur 2 pistes, ambiance, volumes indépendants (général, musique, effets, ambiance), atténuation (« ducking »). |
| `resources/` | Cache de textures / animations / JSON / sons avec **pile de points de montage** : assets de base, assets utilisateur, assets d'un package importé. |

## Jeu (`game/`)

* `levels/` – `LevelData` (format JSON, voir [LEVEL_FORMAT.md](LEVEL_FORMAT.md)),
  `ObjectTypes` (registre des 32 types d'objets avec leur **schéma de propriétés**, partagé
  par le jeu, l'éditeur et l'exporteur), `EventSystem` (déclencheurs → actions),
  `Cinematic` (intro / outro), `LevelTimer`, `TerrainRenderer` (autotuiles 4 bits mises en
  cache par morceaux de 256 px), `DecorAtlas`.
* `gameplay/` – `GameWorld` (simulation : entités, physique, combats, arènes, boss,
  checkpoints, chrono, statistiques), `EntityFactory` (objet JSON → entité), entités
  (plateformes, portes, leviers, lasers, collectibles…), projectiles, dégâts, `Hud`,
  `GameRenderer`.
* `player/` – machine à états (Idle, Run, Jump, Fall, WallSlide, Climb, Slide, Dash, Swim,
  Hurt, Dead, Respawn, Victory, Hidden ; double saut, atterrissage, attaque et interaction
  sont des actions superposées à ces états) entièrement paramétrée par `data/player.json`
  (vitesses, accélérations, décélérations, gravité, temps de coyote, tampon de saut,
  hauteur variable, glissade, saut mural…).
* `powers/` – `PowerRegistry` : chaque pouvoir (Dash, Onde, Bouclier, Impulsion,
  Téléportation) est une classe de comportement configurée par `data/powers/*.json`
  (durée, recharge, énergie, VFX, sons, paramètres). Ajouter un pouvoir = une classe + un
  JSON.
* `enemies/` – `EnemyDefinition` (JSON), `Enemy` (IA : déplacement × réaction × attaques
  télégraphiées, phases de boss), `EnemyAttacks`.
* `planets/`, `progression/`, `records/` – planètes data-driven, déverrouillages, records
  et médailles, classement local avec interface `ILeaderboardProvider` prête pour un
  classement en ligne.
* `content/ContentDatabase` – catalogue unique des contenus officiels, utilisateur et
  importés (sources distinctes, contenus officiels en lecture seule).

## Interface (`ui/`) et éditeurs (`editors/`)

`UIContext` est une interface en mode immédiat avec focus navigable (clavier, manette,
souris) ; les écrans (`ui/screens/`) sont des `Scene` empilées par l'`Application`.
Les éditeurs réutilisent le moteur de rendu et `GameWorld` : le bouton *Tester* lance le
vrai jeu sur une copie du niveau en mémoire. L'historique (`EditorHistory`) stocke des
instantanés JSON pour annuler / rétablir.

## Sauvegardes (`saves/`)

Chaque fichier persistant est une enveloppe versionnée :

```json
{ "format": "cosmic.settings", "version": 2, "savedAt": "…", "engine": "1.0.0", "data": { … } }
```

Au chargement, les migrations successives (v1 → v2 …) sont appliquées et l'ancien fichier
est conservé (`.v1.bak`). Un fichier illisible est renommé `.corrupt` et remplacé par des
valeurs par défaut. Les écritures sont atomiques (fichier temporaire puis renommage).

## Packages (`packaging/`)

Voir [PACKAGE_FORMAT.md](PACKAGE_FORMAT.md). `DependencyCollector` parcourt un niveau
(objets, ennemis, pouvoirs, effets, animations → textures, sons, musiques, fonds, polices)
et produit la liste exhaustive des fichiers à embarquer ; `PackageImporter` vérifie puis
installe de façon atomique.

## Cosmic Online (`game/online/`, `ui/online/`, `server/`)

Voir [online/README.md](online/README.md). `OnlineService` (membre de `GameServices`) est le
seul point d'entrée : il possède le thread réseau (`OnlineClient`, transports WinHTTP /
libcurl), la session, le cache, la file des performances et les notifications. Il est mis à
jour par le crochet `Application::onUpdate` (après les scènes) ; les notifications sont
dessinées par `Application::onOverlay`. `GameScene` enregistre le replay (`ReplayRecorder`,
alimenté par `GameWorld::eventObserver`) et confie la performance à `recordRun` à la fin du
niveau, sans modifier les records locaux existants (`RecordsManager`). La logique du globe
(`OnlineGlobe`) est séparée de son rendu (`ui/online/GlobeRenderer`).

## Boucle de jeu

```
événements SDL → InputManager
tant que accumulateur ≥ 1/120 s :
    scène.fixedUpdate(1/120)       (physique, IA, pouvoirs, événements, chrono)
scène.update(dt)                   (UI, audio, transitions)
onUpdate(dt)                       (Cosmic Online : réponses réseau, synchronisation)
scène.render(alpha)                (interpolation entre deux pas fixes)
onOverlay()                        (notifications en jeu)
```

Le chrono et toute la simulation utilisent le pas fixe : les résultats sont identiques
quelle que soit la fréquence d'affichage.
