# Collection (2.19.0)

Menu principal → **Collection** : quatre onglets, *Vestiaire*, *Route des étoiles*,
*Musiques* et *Galerie*. Tout y est **visuel** : rien ne change dans une partie, un replay,
un record ou la simulation (aucune empreinte de niveau ni version de simulation touchée).

## Récompenses (Vestiaire, Route des étoiles)

Catalogue : `data/cosmetics/cosmetics.json` (catégories, éléments, paliers).

| Catégorie | Éléments | Ce qui change |
|---|---|---|
| Combinaisons (12) | Standard, Givre (20), Braise (40), Sève (60), Noyau (100), Spectre (160), Or stellaire (300), Nébuleuse (350), Abysse (440), Éruption (520), Écho (succès « Silence des échos »), Sanctuaire (succès « Or des sanctuaires ») | la feuille de Nova, recolorée par `tools/assetgen/characters/nova_suits.py` (`assets/sprites/player/suits/` ; `nova_suits.py nebuleuse abysse` ne refait que ces combinaisons) |
| Traînées (8) | Aucune, Poussière d'étoiles (30), Comète (100), Aurore (180), Éclipse (250), Flammèches (290), Prisme (410), Runes d'Aethra (succès « Secrets des sanctuaires ») | effet dessiné derrière Nova quand elle court vite |
| Cadres d'avatar (10) | Simple, Bronze (10), Argent (50), Or (120), Platine (200), Émeraude (270), Orbite (380), Nébuleuse (500), Lauriers (succès « Chasseur de légendes »), Constellation (toutes les étoiles) | cadre de l'avatar |
| Titres (16) | Sans titre, puis un titre par succès précis (Explorateur, Gardien de Sylvanox, Mélomane, Encyclopédiste, Légende du cosmos…) | titre sous le nom |
| Effets de victoire (7) | Classique, Feu d'artifice (70), Pluie d'étoiles (140), Supernova (220), Vortex (320), Aurore boréale (470), Couronne solaire (540) | effet à l'arrivée |

53 récompenses ; la *Route des étoiles* compte 28 paliers, de 10 étoiles à toutes les étoiles
(597). Les paliers en nombre d'étoiles restent à la portée d'un joueur sans compte en ligne
(au plus 540 ; les succès qui demandent Cosmic Online en valent 50) ; seule *Constellation*
demande toutes les étoiles. La route défile (molette, clavier, manette) ; une récompense choisie s'ouvre dans le
*Vestiaire*.

* Un élément se débloque quand le nombre d'**étoiles des succès** atteint son palier, ou
  quand son **succès** est obtenu ; les étoiles déjà gagnées comptent (déblocage rétroactif).
* Ce qui est équipé est gardé dans `saves/collection.json` ; un élément qui ne serait plus
  débloqué (sauvegarde des succès effacée) est remplacé par l'élément de base.
* À la fin d'un niveau, si de nouvelles récompenses sont débloquées, une carte les présente :
  on coche ce qu'on équipe (une seule par catégorie), *Équiper* ou *Plus tard*.

### En ligne (2.23)

Connecté à Cosmic Online, le jeu envoie ce qui est équipé (`PUT /api/v1/players/me/cosmetics`,
les identifiants des cinq éléments) après la liste des succès, puis à chaque *Équiper* ; en cas
d'échec il réessaie une minute plus tard ; hors ligne ou sans compte, rien n'est envoyé et le
jeu reste entièrement jouable. Le serveur revérifie chaque élément avec sa copie du catalogue
(`server/src/cosmeticCatalog.json`, régénérée par `node scripts/cosmetics-catalog.mjs`) et les
succès déclarés : un élément inconnu ou verrouillé devient l'élément de base.

* **Classements** : le cadre autour de l'avatar et le titre sous le pseudonyme (plus petit et
  atténué). Un profil masqué garde le cadre simple, sans titre.
* **Profil** : le cadre, le titre sous le nom et un bloc *Tenue* (Nova dans sa combinaison,
  noms de la combinaison, de la traînée, du cadre et de l'effet de victoire). Rien de cela pour
  un profil masqué ; son propre profil montre ce qu'on porte en ligne.
* **Fantômes** : un fantôme porte la combinaison de son joueur, toujours teintée de sa couleur
  de fantôme. Les siens (meilleur temps, précédent, dernière course) portent la combinaison
  équipée ; un fantôme lancé depuis un classement, celle de l'entrée du classement (une
  apparence que le jeu connaît, sinon la combinaison standard). Affichage seulement : ni la
  simulation ni le format des replays ne changent (la description `.json` du fantôme garde
  `suit`).

## Musiques

Liste : `data/music/tracks.json`. Un morceau devient écoutable la **première fois qu'il est
entendu en jeu** (menu, carte, niveau, ambiance, combat) ; les morceaux des niveaux déjà
terminés sont comptés d'après les sauvegardes. Lecture / pause, position (clic sur la barre),
précédent / suivant, répéter, ordre aléatoire. *Jouer ce morceau au menu principal* le garde
comme musique du menu. Le volume est celui de la musique (Paramètres → Audio).

## Galerie

* **Planètes** : ouvertes quand elles sont atteintes ; *Enregistrer en image (PNG)* compose
  leurs couches de fond en 1280×720 dans le dossier utilisateur `Galerie/`.
* **Gardiens** : un gardien (ou un écho) entre dans la galerie quand il est battu ; ses
  animations défilent et ses statistiques (victoires, sans dégât) sont celles de *Statistiques*.
* **Bestiaire** : une créature y entre dès qu'elle a été vue éveillée à l'écran dans un
  niveau ; celles des niveaux déjà terminés sont comptées d'après les sauvegardes.

## Succès de la Collection (2.24)

La catégorie **Collection** de l'écran *Succès* (10 succès, 18 étoiles) suit ce que l'on fait
ici : récompenses différentes équipées (*Équiper* du Vestiaire ou de la carte de fin de
niveau ; ce qui était déjà porté compte au démarrage), une tenue complète (une récompense
débloquée dans chacune des cinq catégories en même temps), les morceaux du juke-box entendus
(10, puis les 19), la musique du menu choisie, les créatures du bestiaire rencontrées (15,
puis 30 : les créatures de la campagne, sans les gardiens ni les petites gelées nées d'une
division) et les images des planètes enregistrées (une, puis les 5). Compteurs gardés dans
`saves/achievements.json` (`equipped_items`, `outfit_categories`, `menu_music`,
`photographed_planets`) ; les morceaux et les créatures sont lus dans `saves/collection.json`.
