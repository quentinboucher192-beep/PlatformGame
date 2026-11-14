# Guide des éditeurs

Les deux éditeurs sont dans le **WORKSHOP** (menu *Online*), ou directement :
`CosmicPlatformer --editor [id_du_niveau]` et `CosmicPlatformer --enemy-editor`.
Ils se pilotent à la souris et au clavier ; les menus et boîtes de dialogue sont aussi
navigables à la manette.

Les créations sont enregistrées dans le dossier utilisateur (`levels/`, `enemies/`).
Les contenus officiels et importés sont protégés : les ouvrir dans un éditeur crée une
copie modifiable, l'original n'est jamais écrasé.

**Entités natives** : les ennemis, boss, pouvoirs, effets et graphismes livrés avec le jeu
sont ceux de la campagne. Ils sont utilisables dans tous vos niveaux mais jamais modifiables :

* dans l'éditeur d'ennemis, une entité native porte la mention « natif » et un bandeau
  l'indique ; vous pouvez essayer des réglages dans l'arène, et *Enregistrer une copie* crée
  votre version (`<id>_perso`, avec sa propre animation `animations/user/<id>.json` si vous
  avez changé le rythme des images) ;
* un fichier du dossier utilisateur qui reprend l'identifiant d'une entité native est ignoré
  au chargement, et un fichier graphique ou sonore du dossier utilisateur ou d'un package ne
  remplace jamais un fichier de l'installation (il peut seulement en ajouter de nouveaux) ;
* un package `.cplanet` dont un ennemi ou un pouvoir diffère d'une entité native de même
  identifiant l'installe toujours sous un nouvel identifiant, même avec l'option *Remplacer*.

La campagne et les records restent ainsi identiques pour tous les joueurs.

## Éditeur de niveaux

### Interface

* **Niveaux de la campagne et niveaux importés : lecture seule.** On peut les ouvrir, les
  explorer et les tester ; toute modification est refusée et un bandeau propose
  **Dupliquer pour modifier** : la copie (`<id>_copie`, révision 1, auteur = votre
  pseudonyme si vous êtes connecté, `duplicatedFrom` dans les métadonnées) devient un de vos
  niveaux, modifiable et enregistrable.
* **Barre d'outils** : Ouvrir, Nouveau, Enregistrer (ou *Dupliquer* en lecture seule),
  Annuler, Rétablir, outil Sélection, outil Main, grille (8 / 16 / 32 px ou désactivée),
  zoom − / +, *Niveau* (paramètres, difficulté calculée, vignette), *Événements*,
  *Cinématiques*, *Tester (F5)*, *Tester (vue)*, aide, Quitter.
* **Panneau gauche** :
  * *Biblio.* – onglets Terrain, Jeu, Ennemis, Décors, **recherche** (sans accents ni
    majuscules), **vignettes** (texture de l'objet, première image de l'animation de
    l'ennemi, sprite du décor) ; variantes prêtes à l'emploi (matériaux, barrières de
    pouvoir, liquides…).
  * *Calques* – ordre, visibilité, verrouillage, renommage, création, suppression,
    déplacement de la sélection vers un calque.
  * *Objets* – liste de tous les objets avec **filtre** (identifiant, type, ennemi, groupe,
    calque, nom de point de passage) ; clic : sélection et cadrage, Maj+clic : ajout.
  * *Histor.* – toutes les étapes de l'historique (annuler / rétablir) : un clic revient
    directement à une étape.
  * *Vérifier* – contrôles immédiats (départ, sortie, points de passage, objets hors
    limites, ennemis introuvables, références de leviers / récepteurs / arènes / événements
    vers des objets inexistants, plateformes mobiles immobiles) puis, sur un fil de travail,
    le **détecteur de raccourcis** (sortie atteignable, points de passage contournables) et
    la **note de difficulté** calculée ; un clic sur un problème sélectionne l'objet.
* **Vue centrale** – le niveau rendu avec le moteur du jeu (parallaxe, terrain, éclairage),
  et la **carte** (M) : tout le niveau en miniature (terrain, dangers, ennemis, boss,
  départ, sortie, points de passage, sélection), le cadre de la vue ; clic ou glisser pour
  s'y rendre.
* **Panneau droit** – propriétés de l'objet sélectionné (position, taille, rotation,
  miroir, calque et propriétés propres au type) et **Tester ici** (partie lancée depuis cet
  objet) ; pour une sélection multiple : **aligner** (gauche, centre, droite, haut, centre,
  bas), **répartir** à intervalles égaux (horizontalement, verticalement), **grouper /
  dégrouper** (un clic sur un objet groupé sélectionne tout le groupe, Alt+clic l'objet
  seul), supprimer, dupliquer.
* **Vignette** (*Niveau* → *Capturer la vignette*) : la vue actuelle de l'éditeur devient
  l'image du niveau (`<id>.thumb.png` à côté du fichier, point de vue gardé dans
  `metadata.thumbnailFocus`).

### Raccourcis

| Action | Raccourci |
|---|---|
| Sélectionner / placer | Clic gauche |
| Sélection multiple | Maj + clic, ou rectangle tracé dans le vide |
| Déplacer | Glisser un objet sélectionné (aimanté à la grille) |
| Redimensionner | Poignées de l'objet |
| Créer terrain, liquide, zone | Tracer un rectangle avec l'outil choisi |
| Déplacer la vue | Clic droit, molette enfoncée, Espace + glisser, ZQSD / WASD / flèches (Maj : plus vite) |
| Zoom | Molette (Origine : 100 %) |
| Décaler la sélection | Alt + flèches |
| Annuler / Rétablir | Ctrl+Z / Ctrl+Y |
| Copier / Coller / Dupliquer / Tout sélectionner | Ctrl+C / Ctrl+V / Ctrl+D / Ctrl+A |
| Supprimer | Suppr |
| Rotation | R / Maj+R (pas de 15°) |
| Miroir | F |
| Grille | G |
| Outils Sélection / Main | V / H |
| Enregistrer | Ctrl+S |
| Tester depuis le départ / depuis la vue | F5 / Maj+F5 |
| Grouper / dégrouper | Ctrl+G / Ctrl+Maj+G |
| Carte | M |
| Aide | F1 |

### Paramètres du niveau (bouton *Niveau*)

Nom, description, planète, auteur, difficulté, musiques (niveau, boss, ambiance), couleur
d'ambiance et de fond, limites, altitude de chute mortelle, zoom de caméra, pouvoirs de
départ, **chronomètre** (illimité, limité, contre-la-montre, défi ; durée, conséquence à
expiration, temps de référence, morts maximum en défi) et **médailles** (seuils de temps
bronze / argent / or / platine, morts maximum pour le platine).

### Événements

Un événement relie un **déclencheur** (entrée/sortie de zone, levier activé, arène
nettoyée, ennemi vaincu, boss commencé / nouvelle phase / vaincu, objet ramassé,
checkpoint, pouvoir utilisé ou débloqué, début du niveau, temps écoulé) à une liste
d'**actions** (ouvrir / fermer / basculer une porte, activer / désactiver un objet, faire
apparaître un ennemi, démarrer le boss, titre, indication, dialogue, travelling caméra,
verrouiller / libérer la caméra, tremblement, flash, musique, son, effet de particules,
temps bonus, donner un pouvoir, soigner, tuer le joueur, terminer le niveau), avec délai
et option « une seule fois ». La source et les cibles se choisissent par pipette dans la
vue.

### Cinématiques

Intro et outro sont des listes d'étapes ordonnées : caméra (déplacement, cadrage sur un
objet, zoom, coupe), attente, titre, dialogue, fondu, flash, tremblement, son, musique,
effet, bandes cinéma, apparition / masquage / marche / animation du joueur, et action
d'événement. Chaque étape a une durée et peut attendre ou non la suivante ; la
cinématique peut être passable. Le bouton *Prévisualiser* lance la cinématique
dans le test.

### Test et sauvegarde

*Tester (F5)* lance le niveau en cours (sans l'enregistrer) depuis le départ ; *Tester ici*
fait apparaître le joueur au centre de la vue. Échap → *Retour à l'éditeur* revient
exactement à l'état précédent. Une sauvegarde automatique est écrite régulièrement dans
`levels/autosave/` ; l'éditeur propose de la récupérer après un arrêt inattendu.

## Éditeur d'ennemis

* **Liste** des ennemis (officiels, utilisateur, importés) avec Nouveau, Dupliquer,
  Supprimer (créations utilisateur uniquement, avec confirmation).
* **Aperçu animé** : choix de l'animation, lecture / pause, rejouer ; option
  « Afficher hitbox et portées » (détection, attaque, abandon de poursuite).
* **Propriétés** en onglets :
  * *Général* – nom, identifiant, description, famille (créature terrestre, robot,
    volante, tourelle, rapide, lourde, utilisateur de pouvoirs, mini-boss), feuille
    d'animation, taille, échelle du sprite, PV, dégâts de contact, vitesse,
    multiplicateur de poursuite.
  * *IA* – déplacement (immobile, patrouille, surveillance, vol, sauts, fixe) et réaction
    au joueur (aucune, poursuite, garde ses distances, fuite, protège sa zone), portées de
    détection / d'attaque / d'abandon, ligne de vue, seuil de fuite, distance de tir,
    force des sauts.
  * *Attaques* – liste d'attaques (contact, corps à corps, bond, projectile visé, salve en
    éventail, tir en cloche, onde de choc, charge, rayon laser, appel de renforts, pluie de
    projectiles venue d'en haut, éruption au sol) avec télégraphe, durée, recharge, dégâts,
    portée, projectile, rayon d'explosion (bombes), ralentissement infligé (glace, toiles),
    point d'origine (bouche, arme) et couleur du rayon. La pluie et l'éruption marquent au
    sol, pendant le télégraphe, les endroits où elles vont frapper. Attaques avancées :
    *saut sur le joueur* (trajectoire balistique qui franchit les rebords, onde de choc à
    l'atterrissage), *anticipation* (0 = vise le joueur, 1 = vise l'endroit où il sera, point
    d'atterrissage compris s'il saute), *salves* (plusieurs volées re-visées pendant
    l'attaque), *tête chercheuse* (vitesse de virage et durée du guidage), *impacts
    successifs en ligne* (éruptions ou pluie qui partent du boss et traversent la position
    du joueur, une à une).
  * *Pouvoirs* – téléportation (portée, recharge) et défense frontale (garde).
  * *Résist.* – multiplicateur de dégâts par source (0 = immunisé, < 1 = résistance,
    > 1 = faiblesse).
  * *Butin* – points rapportés et objets lâchés (orbes de vie / d’énergie, éclats) avec
    leur probabilité ; division à la mort (un Gloop se sépare en deux petits).
  * *Boss* – mode mini-boss (jauge de vie, titre affiché), *rage* (après N secondes de
    combat : plus rapide, recharges plus courtes, annonces raccourcies) et phases : nom,
    seuil de PV, multiplicateurs de vitesse, de recharge et de durée d'annonce, nombre
    d'attaques enchaînées (combo), texte d'entrée, renforts, attaques disponibles.
    Un boss ne suit pas de cycle fixe : il choisit selon la situation (distance, joueur en
    l'air ou perché au-dessus de lui, attaques déjà utilisées — jamais deux fois de suite la
    même), recule pour trouver un angle si le joueur est sur une corniche, et punit un joueur
    resté hors de portée (saut, pluie, éruptions, tirs en cloche). Tout passe par le hasard
    déterministe du moteur : un replay rejoue exactement le même combat.
* **Arène de test** : l'ennemi évolue avec sa vraie IA ; *Jouer dans l'arène* donne le
  contrôle du joueur (Échap pour le rendre), *Réinitialiser*, *Tester : <attaque>* force
  l'attaque sélectionnée.
* Enregistrer : Ctrl+S ; Annuler / Rétablir : Ctrl+Z / Ctrl+Y.

Les ennemis créés apparaissent immédiatement dans la bibliothèque de l'éditeur de niveaux.

## Éditeur du HUD

*Paramètres › Jeu › « Personnaliser le HUD »* (ou `CosmicPlatformer --hud-editor`).

L'écran montre, réduit, le vrai HUD du jeu dessiné sur un vrai niveau (quatre pouvoirs portés
et un combat de boss en cours, pour que tout soit visible). À droite, la liste des éléments :
vie et énergie, pouvoirs, éclats et reliques, chronomètre, barre de progression, vie du boss.

* **Déplacer** : glisser l'élément dans l'aperçu, ou ses curseurs « Position horizontale » et
  « Position verticale » (clavier, manette). 0 % colle l'élément au bord gauche (ou haut),
  100 % au bord droit (ou bas).
* **Taille** : molette sur l'aperçu, ou le curseur « Taille » (50 à 200 %) ; l'élément grandit
  autour de son centre.
* **Afficher** : masque ou montre l'élément (le chronomètre et la barre de progression
  reprennent les réglages « Afficher le chronomètre » et « Barre de progression »).
* **Place et taille par défaut**, **Tout réinitialiser** : reviennent à la disposition d'origine.

Deux éléments ne se chevauchent jamais et restent dans l'écran : un changement qui le ferait
est refusé (en glissant, l'élément suit l'obstacle sur l'autre axe) et le message sous
l'aperçu dit ce qui gêne. Une disposition qui se chevauche déjà (fichier modifié à la main,
écran d'un autre format) est montrée en rouge et ne peut pas être enregistrée tant que le
conflit n'est pas réglé. La disposition est enregistrée dans `saves/settings.json`
(`gameplay.hud` : pour chaque élément `scale`, `visible`, et `x`, `y` en fraction de l'écran
quand il a été déplacé ; `progressBar` vaut 3 quand la barre a été placée dans l'éditeur).
