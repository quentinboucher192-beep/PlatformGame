# Créations d'un admin : combinaisons recolorées (2.45)

ADMINISTRATION › **Créations** : un compte admin crée des combinaisons sans mise à jour du jeu.
Une combinaison créée = un nom (32 caractères au plus) et trois couleurs : la combinaison, son
accent (visière, énergie) et son écharpe. Le jeu les applique à la feuille de Nova
(`assets/sprites/player/nova.png`) comme `tools/assetgen/characters/nova_suits.py` fait les
combinaisons du catalogue (`game/cosmetics/SuitRecolor.*`, même classement des pixels, même
calcul ; `tests/test_suit_recolor.cpp` le compare aux feuilles livrées).

Code : `server/src/creations.ts` (routes), `server/migrations/0087_creations.sql` (table
`creations`, récompenses de type `suit`), `ui/screens/AdminCreations.*` (onglet),
`GameServices::suitSheet` / `serverSuits` (liste gardée par le jeu).

## Règles

- Le serveur vérifie tout (rôle admin par la session, `requireAdminRole` : `404` pour un autre
  compte ; nom, couleurs `#rrggbb`) ; deux combinaisons actives ne portent pas le même nom
  (accents et casse ignorés). Chaque création, modification, archivage, restauration et chaque
  refus est inscrit au Journal (filtre « Créations »).
- Une création n'est jamais supprimée : archivée, elle n'est plus proposée comme récompense ;
  les joueurs qui l'ont gagnée la gardent.
- Une combinaison créée se gagne comme **récompense d'un défi** (éditeur des récompenses du
  calendrier : type `suit`, référence `srvsuit:<n>`), accordée comme les autres récompenses une
  heure après la fin du défi. Elle apparaît alors dans Collection › Combinaisons et s'équipe.
  Le serveur n'accepte de l'équiper que pour un joueur qui l'a gagnée.
- Affichage seulement : la simulation, les replays et les records ne changent pas.

## Le jeu

Au démarrage puis toutes les 30 minutes (Cosmic Online actif), le jeu lit `GET /creations` et
garde la liste dans `<dossier utilisateur>/online/creations.json` : les combinaisons créées se
dessinent ensuite hors ligne (son personnage, profils et classements des autres joueurs,
fantômes, récompenses). Une combinaison encore inconnue est dessinée en combinaison standard et
la liste est relue.
