# Règles du projet COSMIC PLATFORMER

## Interface (à respecter sur chaque écran modifié)
- **Aucun chevauchement** : aucun texte ne doit en recouvrir un autre, déborder de son bouton,
  de son panneau ou de sa fenêtre, ni passer sous un autre élément. Mesurer avant d'écrire
  (`UIContext::fitFont`, `labelWrapped`, `text().measure`), placer les éléments les uns à la
  suite des autres, jamais à des positions « devinées ».
- Avant chaque livraison, vérifier **par capture d'écran** (rendu sans écran :
  `SDL_VIDEODRIVER=offscreen CosmicPlatformer --headless --shot N:fichier.png …`) chaque écran,
  fenêtre et panneau ajouté ou modifié, en 1600×900 au moins, et corriger tout chevauchement.
- Tout est en français, jouable au clavier, à la manette et à la souris.
- Pas de bouton décoratif ni de données fictives : une fonction annoncée fonctionne vraiment.

## Livraison
- Livrer l'installateur Windows (artefact de la CI) **à la fin de chaque étape**, avec des
  captures des écrans concernés.
- Pousser uniquement sur la branche de développement ; pas de pull request sans demande.

## Moteur et en ligne
- Simulation déterministe à 120 Hz : toute règle qui change le déroulement d'une partie fait
  passer `kSimulationVersion` et les empreintes de référence (`tests/test_determinism.cpp`).
- Les choix du joueur pendant une partie (pouvoir remplacé…) passent par les entrées
  (`InputFrame`), jamais par l'interface seule, pour que les replays restent exacts.
- Ne jamais faire confiance aux données du client ; aucun secret dans le dépôt ni dans le jeu ;
  le jeu reste jouable sans compte et sans Internet ; localisation seulement approximative.
- Les niveaux de la campagne sont générés par `tools/levelgen` (ne pas les éditer à la main) :
  sceaux (`--seal-levels`), stations de pouvoirs (`--plan-stations`), notes (`--rate-levels`).
