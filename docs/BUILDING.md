# Compilation, tests et installateur

## Prérequis

| Plateforme | Outils |
|---|---|
| Windows | Visual Studio 2022 (charge de travail « Développement Desktop en C++ ») ou Build Tools 2022, CMake ≥ 3.24, Git ; Inno Setup 6 pour l'installateur |
| Linux | GCC ≥ 12 ou Clang ≥ 15, CMake ≥ 3.24, Ninja (conseillé), Git, paquets de développement X11 / Wayland / audio requis par SDL3 (voir `.github/workflows/build.yml`), `libcurl4-openssl-dev` pour Cosmic Online |
| Serveur Cosmic Online (facultatif) | Node.js 22 — voir [online/INSTALL.md](online/INSTALL.md) |

Les dépendances sont téléchargées par CMake (`FetchContent`) au premier configure, à des
versions épinglées avec empreinte SHA-256 (`cmake/Dependencies.cmake`) :
SDL 3.4, SDL_image 3.4 (décodeurs intégrés), SDL_ttf 3.2 (FreeType intégré), SDL_mixer 3.2
(décodeurs intégrés), nlohmann/json 3.12, miniz 3.1. Ensuite, aucune connexion n'est
nécessaire, ni pour compiler ni pour jouer.

Option : `-DCOSMIC_USE_SYSTEM_DEPS=ON` utilise des bibliothèques déjà installées
(`find_package`) au lieu de les télécharger.

Client HTTP de Cosmic Online : WinHTTP sous Windows (fourni par le système), libcurl ailleurs
(`find_package(CURL)`). Sans libcurl, ou avec `-DCOSMIC_ONLINE_HTTP=OFF`, le jeu se compile
sans réseau : Cosmic Online indique alors « Réseau indisponible ».

## Windows (Visual Studio)

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
build\bin\CosmicPlatformer.exe
```

Le runtime MSVC est lié statiquement : le jeu ne nécessite aucun redistribuable.
Le dossier `build/bin/` contient le jeu, les DLL SDL et les tests ; lancé depuis l'arbre de
compilation, le jeu trouve `assets/` et `data/` dans les sources.

On peut aussi ouvrir le dossier directement dans Visual Studio (« Ouvrir un dossier »,
prise en charge CMake native).

## Linux

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/bin/CosmicPlatformer
```

## Compilation croisée Windows depuis Linux (MinGW-w64)

```
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-win
```

Les exécutables produits ne dépendent que des DLL système et des DLL SDL3 fournies
(libstdc++, libgcc et winpthread sont liés statiquement). Les tests peuvent tourner sous
Wine : `wine build-win/bin/cosmic_tests.exe`.

## Tests

`cosmic_tests` regroupe :

* tests unitaires : maths, collisions SAT, physique (sol, plateformes traversables),
  chemins sûrs, entrées et sérialisation des contrôles, sauvegardes versionnées et
  migration v1 → v2, chrono indépendant de la fréquence d'images, records / médailles /
  score ;
* gameplay : hauteur de saut conforme aux paramètres, accélération / friction, double saut
  et Dash, ennemi qui détecte, attaque et meurt ;
* niveau : chargement et aller-retour JSON du premier niveau, puis **traversée simulée**
  des passages obligatoires (mur de double saut, gouffre du Dash, falaise à l'Impulsion,
  passage en glissade, barrière de téléportation, arène à vagues, mini-boss en 3 phases,
  sortie et cinématique de fin) ;
* packages : SHA-256, export complet, import sur un « autre PC » sans les assets
  d'origine, refus des archives corrompues et malveillantes (chemin `..`, bibliothèque
  `.dll`, empreinte modifiée, version future, bombe ZIP, fichier non-ZIP) ;
* Cosmic Online (`cosmic_tests online`, `cosmic_tests globe`) : enveloppes JSON, cache,
  session protégée, format et enregistrement des replays, file locale, renouvellement et perte
  de session, repli sur le cache hors ligne, synchronisation après coupure, non-blocage du
  thread principal, client HTTP réel face à un serveur TCP local défaillant (refus, délai,
  coupure, réponse illisible, réponse trop grosse, annulation), logique du globe. Le test de
  bout en bout contre un vrai serveur s'active avec `COSMIC_ONLINE_TEST_URL` et
  `COSMIC_ONLINE_TEST_ADMIN_TOKEN` (voir [online/INSTALL.md](online/INSTALL.md)).

Les tests utilisent un dossier utilisateur temporaire et ne touchent jamais aux vraies
sauvegardes. `cosmic_tests <filtre>` n'exécute que les tests dont le nom contient le filtre.

## Vérifications automatiques en jeu

L'exécutable accepte des options pour les vérifications sans intervention :

```
CosmicPlatformer --headless --level sylvanox_01 --frames 900 --shot 600:zone1.png
CosmicPlatformer --headless --level sylvanox_01 --spawn 3200,700 --input "0:right;60:right,jump;90:" --frames 300
```

`--headless` utilise une fenêtre cachée et des images de 1/60 s fixes ; `--shot <image>:<fichier>`
enregistre une capture PNG au chemin donné (F12 en jeu écrit dans `screenshots/` du dossier
utilisateur) ; `--input` rejoue des actions scriptées ; `--user-dir` isole les données.

## Installateur Windows

```
powershell -ExecutionPolicy Bypass -File installer\build_installer.ps1
```

Le script configure, compile en Release, exécute les tests, prépare l'installation avec
`cmake --install build --config Release --prefix build\stage`, puis compile
`installer\CosmicPlatformer.iss` avec Inno Setup. Résultat :
`installer\Output\CosmicPlatformer-Setup-2.1.0.exe`.

L'installateur :

* installe dans `C:\Program Files\Cosmic Platformer` (ou dans le profil si l'utilisateur
  n'est pas administrateur) ;
* crée les raccourcis du menu Démarrer (jeu, éditeur de niveaux, éditeur d'ennemis,
  documentation, dossier des sauvegardes, désinstallation) et, en option, sur le Bureau ;
* associe, en option, les fichiers `.cplanet` au jeu (double-clic = écran d'import) ;
* propose de lancer le jeu à la fin ;
* à la désinstallation, **conserve** les sauvegardes, records, créations et packages
  (`%APPDATA%\CosmicStudio\CosmicPlatformer`) sauf si l'utilisateur répond *Oui* à la
  question dédiée (réponse par défaut : *Non*).

Le workflow GitHub Actions `.github/workflows/build.yml` effectue la même chaîne sur
Windows (MSVC + tests + installateur publié en artefact) et compile / teste sous Linux.

## Régénérer le contenu

* Assets : `python3 tools/assetgen/generate_all.py` (Python 3, Pillow, NumPy ; ffmpeg pour
  l'encodage OGG). Déterministe (graines fixes), écrit dans `assets/`.
* Niveau officiel : `python3 tools/levelgen/sylvanox_01.py` réécrit
  `data/levels/sylvanox_01.json`.
