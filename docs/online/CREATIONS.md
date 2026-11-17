# Créations d'un admin : combinaisons, traînées, cadres, effets de victoire (2.45, 2.59)

ADMINISTRATION › **Créations** : un compte admin crée des éléments du Vestiaire sans mise à jour
du jeu. Quatre types, choisis au-dessus de la liste (« Combinaisons », « Traînées », « Cadres »,
« Effets de victoire »), chacun avec un aperçu animé dans l'éditeur :

| Type | Référence | Ce qu'on choisit | Aperçu |
|---|---|---|---|
| Combinaison (2.45) | `srvsuit:<n>` | un nom, trois couleurs : la combinaison, son accent (visière, énergie), son écharpe | Nova dans la combinaison, pose après pose |
| Traînée (2.59) | `srvtrail:<n>` | un nom, une **forme** (`comet`, `stardust`, `prism`, `flame`, `aurora`, `bubbles`), deux couleurs (début : près de Nova, fin : le bout), largeur 50 à 200 %, durée 200 à 1000 ms, étincelles oui / non | Nova qui court et fonce à travers l'aperçu, la vraie traînée du jeu derrière elle |
| Cadre (2.59) | `srvframe:<n>` | un nom, un **style** (`metal`, `emerald`, `orbit`, `nebula`, `constellation`, `runic`), deux couleurs (bord, lueur) | l'avatar de l'admin en grand (profil) et une ligne de classement |
| Effet de victoire (2.59) | `srvvictory:<n>` | un nom, un **effet** (`fireworks`, `starfall`, `supernova`, `vortex`, `borealis`, `corona`), deux couleurs (principale, secondaire) | Nova à la sortie d'un niveau, l'effet rejoué |

Les combinaisons sont appliquées à la feuille de Nova (`assets/sprites/player/nova.png`) comme
`tools/assetgen/characters/nova_suits.py` fait celles du catalogue (`game/cosmetics/SuitRecolor.*`,
même classement des pixels, même calcul ; `tests/test_suit_recolor.cpp` le compare aux feuilles
livrées). Les traînées, cadres et effets reprennent chacun la forme d'un élément du catalogue que
le jeu sait déjà dessiner (`ui/screens/CosmeticFx.cpp` : comète, poussière d'étoiles, prisme en
ruban, flammèches, aurore, bulles ; bronze/argent/or pour « metal », émeraude, orbite, nébuleuse,
constellation, cercle runique ; feu d'artifice, pluie d'étoiles, supernova, vortex, aurore boréale,
couronne solaire), dessinée dans leurs deux couleurs (une couleur plus claire et une plus sombre en
sont tirées pour les reflets). La liste des styles est la même des deux côtés
(`server/src/creations.ts`, `game/cosmetics/CreationLooks.cpp` ; `tests/test_creation_looks.cpp`
vérifie qu'elles ne divergent pas) : un style inconnu est refusé par le serveur et laissé de côté
par le jeu.

Code : `server/src/creations.ts` (routes), `server/migrations/0087_creations.sql` (table
`creations`, récompenses de type `suit`), `server/migrations/0115_creations_kinds.sql` (2.59 :
types `trail`, `frame`, `victory`, colonnes `color_a`, `color_b`, `style`, `width`, `life_ms`,
`particles`, récompenses de ces types ; les tables sont reconstruites, toutes les lignes gardées
avec leurs identifiants), `ui/screens/AdminCreations.*` (onglet), `GameServices::suitSheet` /
`creationLook`, `ServerSuitBook` / `ServerLookBook` (listes gardées par le jeu).

## La réserve du jeu (2.60)

Le jeu apporte sa propre réserve de créations, prêtes à donner : **40 combinaisons, 30 traînées,
30 cadres et 30 effets de victoire**, chacune avec un nom français (unique dans son type) et une
palette (planètes du jeu, saisons, pierres précieuses, nébuleuses, métaux, ciel…). Migration
`server/migrations/0118_creations_reserve.sql`, générée par
`node server/scripts/creations-reserve.mjs` (sortie identique d'une exécution à l'autre ;
`--check <fichier>` la compare) : le script vérifie chaque création comme le serveur (nom de
32 caractères au plus, couleurs `#rrggbb`, style présent dans la liste du serveur **et** dans celle
du jeu, bornes d'une traînée), et `server/test/creationsReserve.test.ts` envoie chacune à
`POST /admin/creations` comme si un admin l'avait créée.

- Colonne `game_reserve` (1 pour la réserve) ; `created_by` `NULL` : l'administration affiche
  « Réserve du jeu » au lieu d'un auteur. Un admin peut ensuite les renommer, les redessiner, les
  archiver ou les donner comme les autres.
- Chaque `INSERT … WHERE NOT EXISTS` porte sur (`kind`, `name_key`), archivées comprises : une
  création d'un admin n'est jamais remplacée, et appliquer la migration deux fois n'ajoute rien.
- **Invisibles des joueurs** tant qu'aucun défi commencé ne les donne et que personne ne les a
  gagnées : `GET /creations` ne liste que les créations gagnées par un joueur ou données par un
  défi commencé (les défis à venir ne sont jamais montrés). Cela vaut pour toute création, pas
  seulement la réserve.
- ADMINISTRATION › Créations filtre et étiquette chaque création : « Réserve (jamais donnée) »,
  « Donnée en récompense » (un défi la donne, ou un joueur l'a gagnée), « Archivée ».

## Objets exclusifs et leur aura (2.60)

Un objet **exclusif** ne s'obtient qu'en récompense de défi : toute création du serveur (et les
titres créés sur le serveur), et les objets du catalogue dont la condition est un résultat de défis
(`CosmeticItem::challengeReward()`). Partout où il est dessiné comme objet (Vestiaire, écran des
nouvelles récompenses, récompenses d'un défi dans Défis et à la fin d'un défi, Calendrier des défis
et Créations de l'administration), sa vignette porte une **aura** dorée qui pulse doucement, tenue
dans la marge de la vignette (jamais sur le texte ni sur les voisines ; pas de réglage d'animations
réduites dans le jeu, donc toujours affichée). Au survol ou au focus (clavier, manette), une
infobulle dit d'où il vient : « Exclusif : défi du jour du 10/11/2026 · Or atteint » (défi de la
semaine du …, défi du mois de …), d'après les récompenses gagnées (`GET /players/me/rewards` :
`challenge`, `granted_at`, 2.60 `condition`, `value`) ou le défi dont il est la récompense ;
« Exclusif aux défis : Réussir … » pour une récompense du catalogue ; dans l'administration,
« Réserve : pas encore donné en récompense » ou le premier défi qui le donne (`given_by`). Hors
ligne, le jeu garde ce que son cache connaît, sinon « Exclusif aux défis ».

## Règles

- Le serveur vérifie tout (rôle admin par la session, `requireAdminRole` : `404` pour un autre
  compte, jamais un identifiant envoyé par le jeu ; nom de 32 caractères au plus ; couleurs
  `#rrggbb` avec exactement les clés du type ; style dans la liste de son type ; réglages d'une
  traînée dans leurs bornes ; tout champ inconnu refusé). Deux créations actives du même type ne
  portent pas le même nom (accents et casse ignorés). La table refuse elle aussi un style mal formé
  et les colonnes d'un autre type. Chaque création, modification, archivage, restauration et
  chaque refus est inscrit au Journal (filtre « Créations ») avec les couleurs et le style.
- Une création n'est jamais supprimée : archivée, elle n'est plus proposée comme récompense ;
  les joueurs qui l'ont gagnée la gardent. Son type ne change jamais.
- Une création se gagne comme **récompense d'un défi** (Calendrier des défis › Récompenses ›
  « + Récompense du jeu », dans la catégorie de son type ; `reward_kind` son type, `reward_ref`
  sa référence), accordée comme les autres récompenses une heure après la fin du défi. Elle
  apparaît alors dans sa catégorie du Vestiaire et s'équipe. Le serveur n'accepte de l'équiper
  que pour un joueur qui l'a gagnée, et seulement dans sa catégorie.
- Affichage seulement : la simulation, les replays et les records ne changent pas
  (`kSimulationVersion` et `tests/test_determinism.cpp` inchangés).

## Le jeu

Au démarrage puis toutes les 30 minutes (Cosmic Online actif), le jeu lit `GET /creations` et
garde la liste sur l'ordinateur : les combinaisons dans `<dossier utilisateur>/online/creations.json`
(le fichier des versions 2.45 à 2.58, qui l'ignorent pour le reste), les traînées, cadres et effets
dans `online/creation_looks.json`. Ils se dessinent ensuite hors ligne : son personnage et sa
traînée en jeu, la traînée derrière les fantômes (les siens : celle équipée ; ceux des autres :
celle de leur ligne de classement), le cadre sur les profils et dans les classements, l'effet de
victoire à la sortie d'un niveau, les récompenses des défis. Les récompenses gagnées
(`saves/challenges/state.json`) portent aussi leur apparence. Une création encore inconnue est
dessinée comme l'élément simple de sa catégorie (combinaison standard, pas de traînée, cadre
simple, fin classique) et la liste est relue.

## Le Studio et les images des créations (2.62)

*Administration › Créations › « Studio »* (à côté de « Bibliothèque », la liste et l'éditeur
paramétrique ci-dessus) : un éditeur d'images complet, pour les admins (`ui/studio/`).

- **Explorateur** (à gauche) : les combinaisons, traînées, cadres et effets de victoire du serveur
  et les brouillons de ce PC, une recherche (nom, type, couleur `#rrggbb`), « + Nouvelle »,
  « Importer… », « Dupliquer », « Renommer » (F2 ; le nom part avec « Envoyer au serveur »),
  « Archiver » / « Restaurer » (sur le serveur pour une création envoyée, dans
  `studio/archives` pour un brouillon) ; la **bibliothèque** : les sprites du jeu comme modèles
  (planche de Nova, combinaisons du catalogue, particules de `textures/fx`, anneau et cadres du
  jeu dessinés par le jeu lui-même), les **palettes** (Nova, chaque planète d'après ses paysages,
  couleurs de base) et les images importées.
- **Onglets** : plusieurs créations ouvertes ; « ● » : non enregistrée ; fermer une création
  modifiée demande « Enregistrer et fermer », « Fermer sans enregistrer » ou « Annuler ».
  En quittant l'écran, une création non enregistrée devient son brouillon (rien ne se perd).
- **Toile** : zoom (molette, − +), grille (Ctrl+G), pelure d'oignon (Ctrl+P : l'image d'avant
  bleutée, celle d'après rosée), déplacement (bouton du milieu, espace + glisser, stick droit),
  coordonnées et taille de l'image. **Outils** (rail, infobulles avec leurs touches) : crayon (B ;
  clic droit : couleur 2 ; Maj : ligne droite), gomme (E), pot de peinture (G), pipette (I),
  ligne (L), rectangle (R), ellipse (O ; Maj en lâchant : plein), sélection (M ; Suppr l'efface),
  déplacer (V ; flèches : 1 pixel), miroirs (H, Maj+H) et symétrie en dessinant (S). Annuler /
  rétablir : Ctrl+Z / Ctrl+Y (ou Ctrl+Maj+Z), par différence du rectangle changé (un import
  entier s'annule d'un coup). **Manette** : la toile prend le focus comme un bouton ; un curseur
  s'y déplace (croix, stick), A applique l'outil (maintenu : un trait), LB / RB changent
  d'outil, les gâchettes d'image, X / Y annulent / rétablissent ; au bord, le focus sort de la
  toile.
- **Palette** (à droite) : celle de la création (« + » y ajoute la couleur 1, Maj+clic en
  retire une), ou une palette de la bibliothèque ; sélecteur TSV (carré et barre de teinte, au
  clavier et à la manette), code `#rrggbb`, couleurs 1 et 2 (X les échange). « Recolorer toutes
  les images… » : une couleur par une autre, ou les trois couleurs d'une combinaison (comme
  `nova_suits.py`, ses paramètres suivent), dans toutes les images, une animation ou une image.
- **Frise** (en bas) : les animations du jeu concernées (pour une combinaison, celles de Nova
  d'après `animations/nova.json`, avec leur nombre d'images ; « à faire » quand des images sont
  vides, « Compléter avec Nova » les remplit), leurs images, « + Image », « Dupliquer »,
  « Supprimer », « Lecture » (P) au rythme du jeu ; images par seconde d'une particule ou d'un
  cadre animé.
- **Aperçu en direct** : Nova qui court dans une petite scène (la combinaison, ou la traînée
  derrière elle), son fantôme translucide, le profil, le cadre autour de l'avatar et dans une
  ligne de classement, l'effet de victoire rejoué à la sortie.
- **Importer une image** : un PNG glissé dans la fenêtre (SDL_EVENT_DROP_FILE), choisi
  (« Choisir un autre fichier… », boîte de dialogue de SDL3), ou déposé dans le dossier
  `<dossier utilisateur>/Images à importer` : le Studio le surveille (toutes les 3 s) et
  compte les nouvelles images dans l'explorateur ; un fichier refusé dit pourquoi (pas un PNG,
  PNG abîmé, plus de 8 Mo, plus de 2048 pixels de côté, entièrement transparent). Découpe :
  **détection automatique** (groupes de pixels non transparents, seuil de transparence, marge
  qui réunit les morceaux proches ; un clic retire ou reprend une zone ; un fond uni sans
  transparence est rendu transparent ; au clavier et à la manette, un curseur et A / Entrée sur une
  zone), **grille régulière** (taille, décalage, espacement, cases
  vides ignorées) ou **à la main** (glisser pour tracer une zone, ou A sur deux coins opposés ; clic
  ou A pour la retirer). Les zones
  sont numérotées par rangée ; **chaque rangée va dans** une animation du jeu (rangée *n* →
  *n*-ième animation de Nova par défaut), avec un avertissement quand le nombre d'images ne
  correspond pas ; chaque zone se pose dans son image (sous les pieds ou au centre, à 50, 100 ou
  200 %, réduite si elle est trop grande) ; **aperçu animé** avant d'importer. Rien n'est remplacé
  avant « Importer N images dans … », que Ctrl+Z annule en entier.
- **Enregistrer** (Ctrl+S) : un brouillon dans `<dossier utilisateur>/studio/brouillons/<id>/`
  (`doc.json`, `image.png`) ; les onglets ouverts sont rouverts la fois suivante
  (`studio/session.json`). **Envoyer au serveur** : la création (ses paramètres, comme
  « Bibliothèque ») puis son image ; **Donner en récompense…** ouvre le Calendrier des défis.

**Images** (`server/src/creationAssets.ts`, migration **0119**, table `creation_images`, une
image par création) :

| Type | Image | Réglages |
| --- | --- | --- |
| Combinaison | la planche entière de Nova, 320 × 864 (images de 32 × 32) | — |
| Traînée, effet de victoire | une particule : 1 à 16 images de 4 à 64 pixels, côte à côte, jouées sur la vie de chaque particule | `fps` (1 à 30) et l'émetteur : `rate` 1–120 /s, `life_ms` 100–1000, `size` 25–400 %, `speed` 0–400 px/s, `spread` 0–360°, `gravity` −600–600, `spin` −720–720 °/s, `fade` 0 aucun / 1 à la fin / 2 progressif, `sparkles` |
| Cadre | un anneau : 1 à 16 images carrées de 32 à 128 pixels, en boucle | `fps` |

- `PUT /admin/creations/{id}/image?frame_w=&frame_h=&frames=&fps=&rate=…` (le PNG en corps,
  `image/png` ou `application/octet-stream`) : admin par la session seulement (404 sinon) ; le
  serveur vérifie la signature PNG, l'en-tête IHDR (son CRC), chaque bloc jusqu'à IEND, 512 Ko
  au plus (413 `IMAGE_TOO_LARGE`), les dimensions de son type (400 `IMAGE_BAD_SIZE`), chaque
  réglage (400 `VALIDATION_ERROR`), une création non archivée (409 `CREATION_ARCHIVED`) ; il
  calcule son SHA-256 et la garde dans R2 sous `creation-images/<id>/<sha256>.png` (l'ancienne
  est supprimée). Chaque envoi (`admin_creation_image`) et chaque refus (`admin_refused`, avec sa
  raison) vont au Journal.
- `GET /admin/creations/images` : toutes les images (le Studio).
- `GET /creations/images` (session facultative) et `GET /creations/{id}/image/{sha256}` : un
  joueur ne lit que l'image d'une création qu'il a gagnée, qu'un joueur porte (elle apparaît dans
  un classement, un profil, un fantôme) ou que donne un défi commencé ; jamais une création que
  personne n'a reçue (la réserve non donnée) — la même réponse 404 qu'une image inconnue. Un
  admin les lit toutes. Réponse `image/png`, `ETag: "<sha256>"`, `Cache-Control: private,
  max-age=31536000, immutable` ; `If-None-Match` répond 304. Une nouvelle image a une nouvelle
  adresse.
- **Le jeu** lit `GET /creations/images` avec la liste des créations et garde
  `online/creation_images/list.json` ; chaque image est téléchargée une fois (vérifiée : SHA-256,
  en-tête PNG, dimensions), gardée dans `online/creation_images/<sha256>.png` et lue hors du fil
  principal ; elle se dessine ensuite hors ligne (la planche d'une combinaison, les particules
  d'une traînée ou d'un effet, l'anneau d'un cadre). Tant qu'elle manque, l'apparence simple de
  la création (ses paramètres) est dessinée. Affichage seulement : `tests/test_determinism.cpp`
  inchangé.
