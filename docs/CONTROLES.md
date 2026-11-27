[← Retour à l'accueil (README)](../README.md)

# Contrôles : inventaire des touches et des boutons

Toutes les touches du clavier, boutons de la souris et boutons de la manette auxquels le jeu
réagit. Ce qui se règle se change dans **Paramètres › Clavier / souris** et **Paramètres ›
Manette**, section par section ; « Par défaut » rend sa touche à une commande, « Réinitialiser »
rend toutes celles de l'onglet. Les touches par défaut sont celles d'avant (rien ne change pour
qui n'y touche pas) ; elles sont données **par leur place sur le clavier** (QWERTY : la touche
« , » d'un clavier AZERTY s'affiche « ; »), et chaque écran affiche la touche réellement choisie.

## Ce qui ne change jamais : les replays

Les réglages ne font que relier une touche physique à une commande. Une partie n'enregistre que
les commandes du jeu (déplacements, saut, attaque, interagir, touches des pouvoirs : bits de
l'`InputFrame`) et les choix faits pendant la partie (`InputFrame::choice`, drapeaux des
startpos) : jamais une touche. Les commandes ajoutées ici ne sont **jamais** des bits enregistrés
(`tests/test_controls.cpp`) ; un replay se rejoue à l'identique quels que soient les réglages.

## Conflits : une touche par écran

Chaque commande a les écrans où elle est lue (`actionScope`, `engine/input/InputTypes.cpp`). Deux
commandes **lues sur un même écran** ne peuvent pas avoir la même touche : en choisir une la
retire de l'autre (« Touche retirée de « … » »), comme avant. Deux commandes d'écrans différents
la partagent sans problème : R recommence une partie, recale un replay, revient au début d'un
film, tourne un objet dans l'éditeur, trace un rectangle dans le Studio. Un ancien fichier de
réglages (sans les nouvelles commandes) leur donne leur touche par défaut, sauf si une commande
du même écran l'utilise déjà dans ce fichier : elles restent alors sans touche.

Les commandes de « Partout » (plein écran, capture, debug) sont lues par l'application avant tout
écran : une touche du clavier seulement, jamais une touche des menus (refusée). Les touches des
pouvoirs (choisies pour chaque niveau) ne prennent jamais celle d'une commande lue pendant un
niveau, « Recommencer » et celles de « Partout » comprises.

## Inventaire

« Oui (2.93) » : réglable depuis 2.93. « Déjà » : réglable avant. « Non » : fixe (raison donnée).

### Partie, fantômes, entraînement, raids

| Action | Écran | Touche | Manette | Réglable |
|---|---|---|---|---|
| Gauche, droite, haut, bas | partie (seul, équipe, raid) | A D W S + flèches | stick gauche + croix | Déjà (les flèches et la croix restent en plus) |
| Saut, attaque, interagir | partie | Espace, J, E | A, X, Y | Déjà |
| Pause | partie | Échap | Menu / Start | Déjà |
| Mode concentré | partie | T | Partage (+ pavé tactile) | Déjà |
| **Recommencer** | contre-la-montre, entraînement, défis, entraînement de boss, niveau hardcore avec réapparitions, leurs écrans de fin | R | Vue | **Oui (2.93)** |
| Afficher / masquer les fantômes | course aux fantômes | G | L3 | Déjà |
| Startpos : poser, précédente, suivante ; point de mesure | entraînement (le point aussi en replay) | F5, F6, F7, F8 | bouton de « Recommencer » maintenu + croix | Déjà ; l'accord suit le bouton de « Recommencer » |
| Carte, menu du raid, ordres et pensées des joueurs simulés | raid | M, Tab, C, V | Vue, L3, R3 | Déjà |
| Touches des pouvoirs | niveau | par niveau | par niveau | Déjà, ailleurs : éditeur, choix du niveau › Touches…, pause › Touches de ce niveau |

### Replays et montage « Regarder »

| Action | Touche | Manette | Réglable |
|---|---|---|---|
| Lecture / pause | K | X | **Oui (2.93)** |
| Reculer / avancer de 5 s (montage : segment d'avant / suivant) | J / L | stick droit ← / → | **Oui (2.93)** |
| Image précédente / suivante | , / . | LT / RT (maintenues : répétées) | **Oui (2.93)** |
| Aller au début / à la fin | Home / End | — | **Oui (2.93)** |
| Ralentir / accélérer | - / = (+ pavé numérique) | — | **Oui (2.93)** |
| Afficher / masquer les infos | I | Vue (au relâchement) | **Oui (2.93)** |
| Caméra, trajectoires, recaler (comparaison) | C, T, R | Y, —, — | **Oui (2.93)** |
| Point de mesure | celle de l'entraînement (F8) | bouton des infos maintenu + haut | Déjà (partagée avec l'entraînement) |
| Point précédent / suivant | onglets des menus | LB / RB | Non : touches des menus |

### Films, cadeaux, vues

| Action | Écran | Touche | Manette | Réglable |
|---|---|---|---|---|
| Revoir depuis le début | films et histoires | R | Y | **Oui (2.93)** |
| Mode Film | films et histoires | F | bouton de « Revoir depuis le début » quand il n'y a rien à revoir | **Oui (2.93)** |
| Passer / Quitter | films et histoires | Espace, Entrée / Échap | A / B | Non : Valider et Retour des menus |
| Équiper la récompense | cadeau ouvert | Y | Y | **Oui (2.93)** |
| Zoom avant / arrière | toile des succès, parties des ennemis | = / - (+ pavé numérique) | gâchettes, stick droit (analogiques, fixes) | **Oui (2.93)** |
| Partie suivante / précédente | parties des ennemis | Tab / Maj+Tab | Y / X | **Oui (2.93)** |
| État réel du clan | fiche du cœur d'un raid | R | — | **Oui (2.93)** |

### Éditeur de niveaux et Studio

| Action | Touche | Réglable |
|---|---|---|
| Tester, liste des raccourcis | F5 (Maj : depuis la vue), F1 | **Oui (2.93)** |
| Outil sélection, outil main | V, H | **Oui (2.93)** |
| Rotation (Maj : sens inverse), miroir, aimanter, carte, zoom 100 % | R, F, G, M, Home | **Oui (2.93)** |
| Studio : crayon, gomme, pot, pipette, ligne, rectangle, ellipse, sélection, déplacer | B, E, G, I, L, R, O, M, V | **Oui (2.93)** |
| Studio : retourner (Maj : de haut en bas), symétrie, échanger les couleurs, jouer l'animation | H, S, X, P | **Oui (2.93)** |
| Studio : image précédente / suivante, renommer | , / ., F2 | **Oui (2.93)** |
| Studio : annuler / rétablir | — (Ctrl+Z / Ctrl+Y fixes) ; manette X / Y | **Oui (2.93)** |
| Raccourcis avec Ctrl (annuler, rétablir, enregistrer, copier, coller, dupliquer, grouper, tout choisir, nouveau, importer, fermer l'onglet, grille, pelure d'oignon) | Ctrl+… | Non : conventions de toutes les applications, avec un modificateur |
| Suppr / Retour arrière (supprimer), Échap (quitter la sélection) | | Non : touches d'édition universelles |
| Déplacer la vue | flèches, ZQSD / WASD, stick gauche ; Alt+flèches décale la sélection | Non : navigation, comme dans les menus |
| Souris : clic, clic droit, molette, glisser, Espace+glisser | | Non : le pointeur |
| Manette du Studio : LB / RB (outil), gâchettes (image), A (dessiner) | | Non : navigation et validation des menus |

### Partout

| Action | Touche | Réglable |
|---|---|---|
| Plein écran | F11 (+ Alt+Entrée, fixe) | **Oui (2.93)**, clavier seulement |
| Capture d'écran | F12 | **Oui (2.93)**, clavier seulement |
| Informations de debug (avec le mode debug) | F3 | **Oui (2.93)**, clavier seulement |

### Ce qui reste fixe, et pourquoi

| Touches | Raison |
|---|---|
| Menus : flèches et ZQSD / WASD, Entrée / Espace, Échap / Retour arrière, Q / E et Page préc. / suiv. ; manette : croix et stick gauche, A, B, Start, LB / RB | Navigation universelle : le joueur ne peut jamais se priver des menus, ni de l'écran des réglages lui-même |
| Échap pendant le choix d'une touche | Annule toujours (sauf pour « Pause », qui peut la prendre) |
| Champs de texte : la saisie, Retour arrière, Suppr, flèches, Origine / Fin, Entrée (envoyer), Tab et Échap (sortir), Ctrl+A / C / X / V | Saisie du texte ; aucune commande ne réagit pendant qu'on écrit |
| Raccourcis avec Ctrl, Alt+Entrée | Conventions du système et des applications |
| Souris : clic gauche (interface), molette (défiler, zoomer), boutons du milieu / droit en glisser (déplacer une vue) | Le pointeur ; tout bouton de la souris peut en plus servir à une commande |
| Sticks et gâchettes analogiques (zoom et déplacement continus) | Commandes analogiques, pas des boutons |
| Prendre une place à plusieurs sur un PC : A / Start d'une manette, Entrée du clavier | Un appareil s'annonce par sa touche de validation, avant qu'aucun réglage ne lui soit lié |
| Administration | Aucun raccourci : boutons de l'interface seulement |

---

[← Retour à l'accueil (README)](../README.md)
