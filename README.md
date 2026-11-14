# Cosmic Platformer

Jeu de plateforme 2D en C++20 / SDL3, avec éditeur de niveaux, éditeur d'ennemis,
système de planètes, records et partage de niveaux par fichiers `.cplanet`.
Fonctionne entièrement hors ligne ; **Cosmic Online** (facultatif) ajoute les classements
mondiaux, les records vérifiés et un globe terrestre interactif.

**Contenu de la version 2.2.0** : 3 planètes et 12 niveaux, chacun avec son boss. Le
quatrième niveau de chaque planète est une **épreuve** bâtie sur une mécanique propre à la
planète, avec un gardien d'élite qui n'apparaît nulle part ailleurs :

* *Les Catacombes des Anciens* (Sylvanox) — **la machinerie des Anciens** : salles des
  métronomes (lasers qui battent la mesure au-dessus de dalles qui s'effritent : un saut par
  temps), porte du gardien du temps (Onde sur un récepteur, puis course avant qu'elle se
  referme), ancrages entre deux tirs de laser, murs de cristal brisés au Dash en plein saut,
  salle des leviers (chaque levier ouvre le rideau laser devant vous et referme celui de
  derrière). Gardien : l'**Archonte des Catacombes**.
* *Le Creuset Profond* (Pyralis) — **la lave et la forge** : radeaux de basalte (le premier
  part quand vous montez : choisissez le moment, un laser coupe sa route), cheminées de
  chaleur à remonter en planant sous une voûte hérissée, pistons de forge, fonderie (tir
  chargé sur le récepteur, puis course sur des dalles qui s'effondrent), descente du
  creuset en brisant trois dalles au martèlement. Gardien : le **Cœur du Creuset**.
* *La Crevasse Hurlante* (Glacior) — **la glace et le vent** : pentes de glace vive, ponts de
  givre qui cèdent au premier pas, lac gelé à traverser à la nage sous la banquise, couloir
  des vents et rayons de givre, pics glissants au-dessus des pointes. Gardienne : la **Wyrm
  Hurlante**.

Chaque épreuve ne donne que les pouvoirs dont elle a besoin, et ses fosses sont peu profondes
(le vol plané prolonge un saut, il ne survole plus une salle). Les salles clés sont jouées par
des tests avec le vrai moteur (bots scriptés). La **note de difficulté** affichée est calculée
à partir du niveau (voir [docs/LEVEL_FORMAT.md](docs/LEVEL_FORMAT.md)) : 2,5 à 4/5 pour la
campagne, les épreuves en tête.

| Planète | Niveaux (boss) |
|---|---|
| **Sylvanox**, le monde-jardin | Les Ruines Suspendues (Thornwarden) · La Canopée des Spores (Reine Sporale) · Le Cœur de la Machine (Sentinelle Prime) · *Les Catacombes des Anciens* (Archonte des Catacombes) |
| **Pyralis**, la forge en fusion | Les Forges Englouties (Magmaroth) · Le Fleuve de Lave (Ignivore) · La Fonderie Primordiale (Forgeron Primordial) · *Le Creuset Profond* (Cœur du Creuset) |
| **Glacior**, le sanctuaire gelé | Les Plaines de Givre (Cryowyrm) · Les Grottes de Cristal (Hydre de Givre) · Le Bastion Gelé (Glaciarque) · *La Crevasse Hurlante* (Wyrm Hurlante) |

Aucun niveau ne se contourne : chaque niveau a un plafond (on ne passe plus par le haut) et
le **détecteur de raccourcis** (`CosmicPlatformer --analyze-levels <dossier>`) vérifie, avec
les capacités du joueur dans chaque niveau, qu'aucun point de contrôle ni aucune arène ne peut
être évité ; un test l'exécute sur toute la campagne. Les niveaux classiques ont deux fois
moins d'orbes de soin qu'en 2.1.2.

**Boss** (version 2.2) : ils lisent le combat — distance, joueur en l'air ou perché,
attaques déjà utilisées —, visent là où le joueur va atterrir, enchaînent des combos dans
les dernières phases, punissent le joueur qui reste hors de portée et entrent en rage si le
combat s'éternise ; chacun a 4 nouvelles attaques (saut balistique, lignes d'éruptions
successives, projectiles à tête chercheuse, salves re-visées) et des annonces plus courtes.

51 sortes d'ennemis (12 espèces, chacune en version Sylvanox, feu et glace, plus les boss),
10 pouvoirs, 10 cœurs de vie (plus des cœurs de cristal cachés), 3 reliques et un cœur de
cristal secrets par niveau. Par défaut, l'option *Paramètres → Jeu → Tout débloquer* rend
toutes les planètes et tous les niveaux accessibles ; sans elle, ils se débloquent dans
l'ordre.

## Jouer

| Action | Clavier AZERTY (QWERTY) – principal / secondaire | Manette (Xbox / PlayStation) |
|---|---|---|
| Se déplacer | Q D (A D) / ← → | Stick gauche / croix |
| Haut / bas, grimper | Z S (W S) / ↑ ↓ | Stick gauche |
| Sauter / double saut | Espace / W (Z) | A / Croix |
| Glissade | Bas en courant au sol | Bas (stick / croix) en courant |
| Attaque | J / X | X / Carré |
| Dash | K / Maj gauche | B / Rond |
| Onde d'énergie | L / C | RT / R2 |
| Bouclier | O / V | LB / L1 |
| Impulsion | I / B | RB / R1 |
| Téléportation courte | U / N | LT / L2 |
| Planeur | maintenir Saut en tombant | maintenir A / Croix |
| Frappe sismique | Bas + Attaque en l'air | Bas + X / Carré |
| Tir chargé | maintenir Attaque, relâcher | maintenir X / Carré |
| Grappin | Interagir près d'un point d'ancrage | Y / Triangle |
| Triple saut | troisième Saut en l'air (passif) | A / Croix |
| Interagir | E / Entrée | Y / Triangle |
| Pause | Échap / P | Menu / Options |

Les touches sont des **positions physiques** (scancodes) : les déplacements tombent sur ZQSD
en AZERTY et sur WASD en QWERTY, et le jeu affiche toujours le nom réel de la touche. Toutes les commandes se reconfigurent dans
*Paramètres → Clavier* et *Paramètres → Manette* (profils indépendants, zones mortes,
sensibilité, inversion des axes, vibrations, réinitialisation). Les icônes affichées
s'adaptent à la manette utilisée (Xbox, PlayStation ou générique) et basculent
automatiquement entre clavier et manette.

Touches globales : **F3** mode debug (FPS, dt, colliders, hitboxes, position/vitesse du
joueur, états des ennemis, chrono, particules), **F11** plein écran, **F12** capture d'écran.

### Les pouvoirs

Les cinq premiers (Dash, Onde, Bouclier, Impulsion, Téléportation) s'obtiennent dans
« Les Ruines Suspendues » ; les suivants s'apprennent chacun dans un niveau : Planeur
(La Canopée des Spores, courants ascendants), Grappin (Le Cœur de la Machine), Frappe
sismique (Les Forges Englouties, sols fissurés), Tir chargé (Le Fleuve de Lave, récepteurs
lointains, boucliers des Remparts) et Triple saut (Les Plaines de Givre). Les niveaux joués
hors de l'ordre donnent d'office les pouvoirs des niveaux précédents.

Sur Glacior, la glace est glissante et les attaques gelées ralentissent ; sur Pyralis, la
lave, les éruptions et les bombes sont mortelles.

Chaque niveau a un chrono (limite de 15 à 20 min), des médailles bronze / argent / or /
platine, records locaux (meilleur temps, morts, collectibles, ennemis vaincus, score,
pouvoirs utilisés, date) avec comparaison au meilleur essai.

## Cosmic Online (classements en ligne)

Menu principal → **Online** (compte obligatoire pour cette section, jamais pour jouer) : globe 3D interactif (rotation à la souris ou au stick
droit, zoom molette / gâchettes, clic sur un marqueur ou un pays), classements par niveau,
pays, amis, planète et points cosmiques, catégories ANY% / 100% / NO DEATH / NO DAMAGE /
SPEEDRUN, période historique ou hebdomadaire, profils publics, amis (avec notifications des
demandes) et **replays exacts** : le moteur est déterministe (pas fixe de 1/120 s, hasard du
gameplay à graine fixe par niveau, mathématiques identiques sous Windows et Linux), un replay
contient les commandes de chaque pas et le jeu rejoue la partie à l'identique (mêmes
animations, mêmes ennemis, mêmes attaques), avec vérification de l'état toutes les 0,5 s
(`CosmicPlatformer --replay fichier.cprz`, *Revoir la partie* à la fin d'un niveau). Le jeu reste entièrement jouable sans compte ; hors ligne, les
performances sont gardées et envoyées au retour de la connexion. La localisation se limite au pays choisi (affiché au centre du pays), jamais de
position précise.

Le serveur (Cloudflare Workers, D1, R2) est dans [`server/`](server/README.md) ; il se lance en
local sans compte Cloudflare. Documentation complète : [docs/online/](docs/online/README.md).

## Menu principal

*Continuer* (dernier niveau joué) · *Campagne* (planètes et niveaux) · *Online* ·
*Paramètres* · *Quitter*.

**Online** (compte requis) : **ONLINE LEVELS** (les niveaux publiés par les joueurs : Populaires,
Plus durs, Récents, Mieux notés, Amis, Recherche, Les miens ; télécharger et jouer, noter,
signaler, classement par niveau ; *WORKSHOP → Mes niveaux → Publier* après avoir terminé son
niveau depuis le départ dans l'éditeur), **ONLINE RANKING** (classements, globe, profils), **SOCIAL**
(messagerie privée avec vos amis, groupes de discussion jusqu'à 20 membres, amis et demandes,
joueurs bloqués, signalement de messages ; notifications des nouveaux messages et demandes,
gardées pendant une partie et affichées ensuite) et le **WORKSHOP** (vos niveaux,
niveaux importés, export / import `.cplanet`, éditeur d'ennemis). Dans ONLINE, un niveau se
joue depuis sa fiche (*Jouer ce niveau*) ; à la fin (ou en quittant), on revient à ONLINE sur
ce niveau.

## Éditeurs

Les **entités natives** (ennemis, boss, pouvoirs, effets et graphismes de la campagne) sont
utilisables dans tous les niveaux des joueurs mais ne sont jamais modifiables : l'éditeur
d'ennemis enregistre vos réglages dans une copie, et un fichier utilisateur ou un package qui
réutilise l'identifiant d'une entité native est ignoré (renommé à l'import).

* **Éditeur de niveaux** (*Online → Workshop → Mes niveaux* ou `CosmicPlatformer --editor [id]`) :
  calques, bibliothèque d'objets, propriétés, grille magnétique, zoom, déplacement de vue,
  sélection multiple, déplacement / redimensionnement / rotation / miroir, copier-coller,
  annuler / rétablir, événements et déclencheurs, cinématiques d'intro / outro, chrono et
  médailles, test immédiat (F5) avec retour à l'éditeur, sauvegarde automatique.
* **Éditeur d'ennemis** (*Online → Workshop → Éditeur d'ennemis* ou `--enemy-editor`) : aperçu animé, statistiques, comportements IA,
  attaques, phases de boss, arène de test où l'on peut contrôler le joueur.

Voir [docs/EDITORS.md](docs/EDITORS.md).

## Partager un niveau

*Online → Workshop → Exporter* crée un fichier `.cplanet` autonome contenant le niveau,
la planète, les ennemis, les pouvoirs, les effets et **tous** les fichiers graphiques et
audio utilisés, avec un manifeste signé par empreintes SHA-256. *Importer* vérifie
l'archive (empreintes, chemins, tailles, version, dépendances, conflits) avant de
l'installer ; aucun code n'est jamais exécuté depuis un package. Avec l'installateur
Windows, un double-clic sur un `.cplanet` ouvre directement l'écran d'import.

En ligne de commande :

```
CosmicPlatformer --export sylvanox_01 MonNiveau.cplanet --author "Mon nom"
CosmicPlatformer --import MonNiveau.cplanet
```

Voir [docs/PACKAGE_FORMAT.md](docs/PACKAGE_FORMAT.md).

## Données utilisateur

Sauvegardes, paramètres, records, niveaux créés, packages importés, exports, captures et
journal (`cosmic.log`) sont stockés dans :

* Windows : `%APPDATA%\CosmicStudio\CosmicPlatformer\`
* Linux : `~/.local/share/CosmicStudio/CosmicPlatformer/`

Les fichiers sont versionnés et migrés automatiquement ; un fichier corrompu est mis de
côté (`.corrupt`) au lieu de faire planter le jeu. La désinstallation les conserve, sauf
demande explicite.

## Compiler

Prérequis : CMake ≥ 3.24, un compilateur C++20 (Visual Studio 2022, GCC 12+ ou Clang 15+),
Git, et une connexion Internet **uniquement** au premier configure (téléchargement de SDL3,
SDL3_image, SDL3_ttf, SDL3_mixer, nlohmann/json, miniz, aux versions épinglées).

```
cmake -S . -B build
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

L'exécutable est dans `build/bin/`. Installateur Windows : `installer\build_installer.ps1`
(Inno Setup 6). Détails, options et compilation croisée : [docs/BUILDING.md](docs/BUILDING.md).

## Documentation

* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) – modules, boucle de jeu, flux de données
* [docs/LEVEL_FORMAT.md](docs/LEVEL_FORMAT.md) – format JSON des niveaux, objets, événements
* [docs/PACKAGE_FORMAT.md](docs/PACKAGE_FORMAT.md) – format `.cplanet` et vérifications à l'import
* [docs/EDITORS.md](docs/EDITORS.md) – guide des éditeurs
* [docs/BUILDING.md](docs/BUILDING.md) – compilation, tests, installateur
* [docs/ASSET_SPEC.md](docs/ASSET_SPEC.md) – contrat des assets (sprites, animations, tuiles, audio)
* [docs/online/](docs/online/README.md) – Cosmic Online : installation, déploiement, secrets, API, règles, confidentialité, dépannage

## Licences

Code et assets du jeu : Cosmic Studio. Les assets (sprites, décors, sons, musiques) sont
générés par les scripts de `tools/assetgen`. Bibliothèques et polices tierces : voir
[licenses/](licenses/). Le globe utilise des données Natural Earth (domaine public), les
drapeaux flag-icons (MIT), les noms de pays i18n-iso-countries (MIT) et les lumières des villes
de GeoNames (CC BY 4.0, © GeoNames), générés par `tools/globegen`.
