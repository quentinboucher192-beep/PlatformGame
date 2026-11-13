# globegen : données du globe du classement en ligne

Ce dossier régénère les textures et les données utilisées par l'écran « Classement en
ligne » (globe terrestre 3D interactif dessiné par le jeu avec `SDL_RenderGeometry`).
Tout est produit de façon **déterministe** : deux exécutions donnent des fichiers
identiques octet pour octet.

## Régénérer

Prérequis : Node.js 22 et npm, Python 3 avec Pillow et NumPy.

```sh
cd tools/globegen
npm ci            # installe les versions exactes de package-lock.json
npm run build     # = python3 -I build.py
```

Depuis la racine du dépôt, `python3 -I tools/globegen/build.py` fait la même chose (il
lance `npm ci` tout seul si `node_modules/` est absent). Durée : environ 1 minute.

Étapes :

1. `extract.js` (Node) lit les paquets npm et écrit des fichiers intermédiaires dans
   `tools/globegen/build/` (ignoré par git) : géométries par pays (`geo.json`), codes et
   noms ISO (`codes.json`), villes (`cities.f32`), drapeaux rendus à 192x144 (`flags/`).
2. `build.py` enchaîne les modules Python :
   `countries.py`, `earth_day.py`, `earth_night.py`, `flags.py`, `avatars.py`
   (`geo_raster.py` contient les outils communs), puis **vérifie** le résultat (tailles,
   formats, positions et pixels d'index de FR, US, BR, JP, AU, ZA, CN, IN, RU, CA, GB, DE,
   présence des drapeaux) et s'arrête en erreur si un contrôle échoue.

Python est lancé avec `-I` (mode isolé) car il lit des données téléchargées.

## Fichiers produits

| Fichier | Format | Contenu |
|---|---|---|
| `assets/online/earth_day.png` | 2048x1024 RGB | Terre de jour stylisée : océans bleu nuit, hauts-fonds plus clairs près des côtes, biomes selon la latitude (forêts, savanes, déserts, toundra), reliefs principaux, calottes glaciaires (Antarctique, Groenland, banquise arctique), côtes cyan lumineuses, frontières fines semi-transparentes. |
| `assets/online/earth_night.png` | 2048x1024 RGBA | Lumières des villes (halos jaune-orangé dont la taille dépend de la population). Voir « Lumières de nuit ». |
| `assets/online/earth_countries.png` | 1024x512 RGB | Carte d'index des pays pour le clic (sans anticrénelage). |
| `data/online/countries.json` | JSON | Les pays présents sur la carte, avec leur index. |
| `data/online/country_codes.json` | JSON | Tous les codes ISO 3166-1 alpha-2 valides (+ XK), pour valider le pays choisi par un joueur. |
| `assets/online/flags.png` | 1536x288 RGBA | Atlas de drapeaux 48x36 (4:3), 32 colonnes. |
| `data/online/flags.json` | JSON | Code -> numéro de cellule de l'atlas. |
| `assets/online/avatars.png` | 512x512 RGBA | 16 avatars 128x128 (grille 4x4), badges ronds cosmiques. |
| `data/online/avatars.json` | JSON | Identifiants et noms des avatars. |

## Projection (toutes les cartes du globe)

Équirectangulaire, longitude -180 à gauche, nord en haut :

```
x = (lon + 180) / 360 * W
y = (90 - lat) / 180 * H
```

Le pixel (c, r) couvre lon ∈ [c·360/W − 180, (c+1)·360/W − 180[ et a son centre en
(c + 0,5, r + 0,5). La texture est périodique en x (le bord gauche continue le bord droit).

## Carte d'index `earth_countries.png`

Chaque pixel code l'index d'un pays : `index = R + 256 * G`, `B = 0`.
`(0, 0, 0)` = océan ou zone sans pays. Un index ≥ 1 renvoie à l'entrée de
`countries.json` qui porte ce même `index`. Pour tester un clic : convertir le point
de la sphère en (lat, lon), puis lire le pixel `(floor((lon+180)/360*1024),
floor((90-lat)/180*512))` (coordonnées bornées à l'image).

Le remplissage se fait au centre des pixels, règle pair-impair, sans anticrénelage ; deux
pays ne se partagent jamais un pixel. Les très petits pays absents de la grille (Monaco,
Vatican, Singapour, Hong Kong, petites îles…) reçoivent un pixel à leur point
représentatif pour rester cliquables (liste affichée pendant la génération).

## `countries.json` et `country_codes.json`

```json
{ "version": 1, "countries": [
  { "index": 74, "code": "FR", "alpha3": "FRA", "numeric": "250",
    "name_fr": "France", "name_en": "France", "lat": 46.5, "lon": 2.5 } ] }
```

* Trié par code ; `index` = rang dans cette liste (1, 2, …). Il change si les données
  sources changent : le jeu doit toujours passer par le code, pas par un index codé en dur.
* `lat`/`lon` (arrondis à 0,1°) : **seule position jamais affichée pour un joueur**
  (confidentialité). C'est le barycentre surfacique du **plus grand polygone** du pays
  (France métropolitaine, États-Unis contigus, Honshu…). S'il tombe hors du polygone
  (Norvège, Viêt Nam, Croatie…), on prend le pôle d'inaccessibilité (recherche sur
  grille). On vérifie ensuite que le pixel d'index à cette position est bien le pays.
* Particularités de Natural Earth / world-atlas traitées dans `extract.js` :
  Kosovo → `XK` ; Somaliland → `SO` et Chypre du Nord → `CY` (rattachements ISO) ;
  « Ashmore and Cartier Is. » → `AU` ; « Indian Ocean Ter. » séparé en `CX` et `CC` ;
  les DOM (`GF`, `GP`, `MQ`, `RE`, `YT`), Svalbard (`SJ`) et Bonaire (`BQ`) sont extraits
  des polygones de la France, de la Norvège et des Pays-Bas pour avoir leur propre index ;
  le glacier de Siachen (zone disputée) reste sans pays. Les frontières recousues sur
  l'antiméridien (Russie, Fidji, Antarctique) sont déroulées avant le remplissage.
* Noms : `i18n-iso-countries`, avec quelques formes courtes pour l'interface
  (`États-Unis`, `RD Congo`, `Tchéquie`, `Russia`, `China`…, voir `countries.py`).

`country_codes.json` (`"codes": [...]`) liste les 250 codes connus de
`i18n-iso-countries` avec les mêmes champs ; `index` vaut 0 si le territoire n'est pas
sur la carte. `lat`/`lon` sont présents quand on les connaît : point représentatif de la
carte, sinon plus grande localité GeoNames du territoire (Gibraltar, Tokelau, Tuvalu) ;
absents pour `BV` (île Bouvet) et `UM` (îles mineures des États-Unis).

## Lumières de nuit `earth_night.png`

`alpha` = intensité lumineuse, `RGB` = couleur de la lumière **non prémultipliée**
(noir là où alpha = 0). Rendu conseillé : dessiner la texture avec
`SDL_BLENDMODE_ADD` (résultat = couleur × alpha) par-dessus la face de nuit, en modulant
l'alpha des sommets par le facteur nuit. Chaque localité GeoNames (≥ 1000 habitants)
dépose une lumière ∝ population^0,72, étalée par quatre flous gaussiens qui tiennent compte
de la sphère : les métropoles ont naturellement des halos plus grands.

## Drapeaux `flags.png` / `flags.json`

```json
{ "version": 1, "cell_w": 48, "cell_h": 36, "columns": 32, "rows": 8, "unknown": 0,
  "cells": { "AD": 1, "AE": 2, "FR": 75 } }
```

Cellule `n` en `(n % 32 * 48, n / 32 * 36)`. La cellule 0 est le drapeau « inconnu »
(fond bleu nuit, petit globe cyan) ; tout code sans SVG y renvoie (aucun actuellement).
Les SVG 4x3 de flag-icons sont rendus à 4x avec resvg puis réduits (moyenne 4x4).

## Avatars `avatars.png` / `avatars.json`

16 portraits 128x128 (dessinés à 512 px puis réduits), cellule `i` en
`(i % 4 * 128, i / 4 * 128)`, ordre de `avatars.json` (`avatar_01` … `avatar_16`) :
4 astronautes (visières cyan, violet, ambre, rose), alien vert, alien à trois yeux,
poulpe cosmique, robot, planète à anneaux, comète, étoile, chat lunaire, fusée,
soucoupe volante, trou noir, galaxie spirale. Chaque entrée a `id`, `name_fr`,
`name_en` et `rim` (couleur du liseré, palette #39F0D8 #B04CFF #FFB347 #FF4D6D #78E68C
#4C8DFF). Hors du badge, l'image est transparente.

## Sources et licences

| Source | Licence | Utilisation | Fichier |
|---|---|---|---|
| Natural Earth 1:50m v4.1.0 via `world-atlas` 2.0.2 | domaine public / ISC | frontières, terres, index, positions | `licenses/NaturalEarth-world-atlas-LICENSE.txt` |
| `i18n-iso-countries` 7.14.0 | MIT | codes et noms | `licenses/i18n-iso-countries-LICENSE.txt` |
| `flag-icons` 7.6.0 | MIT | drapeaux | `licenses/flag-icons-LICENSE.txt` |
| GeoNames via `all-the-cities` 3.1.0 | **CC BY 4.0** (données) / MIT (paquet) | lumières des villes | `licenses/GeoNames-all-the-cities-LICENSE.txt` |

**Attribution obligatoire** dans les crédits du jeu pour GeoNames (CC BY 4.0), texte
proposé dans le fichier de licence. Outils de génération non redistribués :
`topojson-client` 3.1.0 (ISC) et `@resvg/resvg-js` 2.6.2 (MPL-2.0, n'affecte pas les
images produites). Les avatars et le drapeau « inconnu » sont dessinés par ce générateur.

## Limites connues

* Pas de lacs (Grands Lacs, Victoria, Baïkal…) : world-atlas n'en contient pas et les
  jeux de données trouvés sur npm (geo-maps, dérivés d'OpenStreetMap) sont sous ODbL.
* Biomes, déserts et chaînes de montagnes sont **stylisés** : latitude + bruit 3D +
  quelques foyers arides et tracés de reliefs placés à la main (`DESERTS`, `MOUNTAINS`
  dans `earth_day.py`) ; ce n'est pas une carte d'altitude réelle.
* La densité des lumières reflète le recensement GeoNames, plus détaillé en Europe
  qu'ailleurs.
