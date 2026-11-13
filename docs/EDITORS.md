# Guide des éditeurs

Les deux éditeurs sont accessibles depuis le menu principal, ou directement :
`CosmicPlatformer --editor [id_du_niveau]` et `CosmicPlatformer --enemy-editor`.
Ils se pilotent à la souris et au clavier ; les menus et boîtes de dialogue sont aussi
navigables à la manette.

Les créations sont enregistrées dans le dossier utilisateur (`levels/`, `enemies/`).
Les contenus officiels et importés sont protégés : les ouvrir dans un éditeur crée une
copie modifiable, l'original n'est jamais écrasé.

## Éditeur de niveaux

### Interface

* **Barre d'outils** : Ouvrir, Nouveau, Enregistrer, Annuler, Rétablir, outil Sélection,
  outil Main, grille (8 / 16 / 32 px ou désactivée), zoom − / +, *Niveau* (paramètres),
  *Événements*, *Cinématiques*, *Tester (F5)*, *Tester ici*, aide, Quitter.
* **Panneau gauche** :
  * *Bibliothèque* – tous les objets par catégorie (terrain, plateformes, pièges & dangers,
    gameplay, interactifs, ennemis, zones & événements, décors, lumières & effets), avec
    variantes prêtes à l'emploi (matériaux, barrières de pouvoir, liquides…), tous les
    ennemis disponibles et les éléments de décor.
  * *Calques* – ordre, visibilité, verrouillage, renommage, création, suppression,
    déplacement de la sélection vers un calque.
  * *Objets* – liste de tous les objets du niveau (clic : sélection et cadrage).
* **Vue centrale** – le niveau rendu avec le moteur du jeu (parallaxe, terrain, éclairage).
* **Panneau droit** – propriétés de l'objet sélectionné (position, taille, rotation,
  miroir, calque et propriétés propres au type), ou actions groupées pour une sélection
  multiple.

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
    éventail, tir en cloche, onde de choc, charge, rayon laser, appel de renforts) avec
    télégraphe, durée, recharge, dégâts, portée et projectile.
  * *Pouvoirs* – téléportation (portée, recharge) et défense frontale (garde).
  * *Résist.* – multiplicateur de dégâts par source (0 = immunisé, < 1 = résistance,
    > 1 = faiblesse).
  * *Butin* – points rapportés et objets lâchés (orbes de vie / d’énergie, éclats) avec
    leur probabilité.
  * *Boss* – mode mini-boss (jauge de vie, titre affiché) et phases : nom, seuil de PV,
    multiplicateurs de vitesse et de recharge, texte d'entrée, renforts, attaques
    actives dans l'ordre du cycle.
* **Arène de test** : l'ennemi évolue avec sa vraie IA ; *Jouer dans l'arène* donne le
  contrôle du joueur (Échap pour le rendre), *Réinitialiser*, *Tester : <attaque>* force
  l'attaque sélectionnée.
* Enregistrer : Ctrl+S ; Annuler / Rétablir : Ctrl+Z / Ctrl+Y.

Les ennemis créés apparaissent immédiatement dans la bibliothèque de l'éditeur de niveaux.
