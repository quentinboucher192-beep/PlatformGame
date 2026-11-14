# Ghost Racing, succès et mode concentré (2.18.0)

## Ghost Racing

Courir contre des **fantômes** : des courses déjà faites, rejouées en même temps que la
vôtre, sur le chronomètre du niveau.

### D'où viennent les fantômes

| Fantôme | Origine | Stockage |
|---|---|---|
| Mon meilleur temps | votre course la plus rapide sur **cette version** du niveau | `ghosts/local/<niveau>-<version>/pb.cprz` |
| Mon record précédent | le meilleur temps que le dernier a remplacé | `…/prev.cprz` |
| Ma dernière course | la dernière course terminée | `…/last.cprz` |
| Record mondial | 1re place du classement (officiel : Any%, classement « historique » ; niveau de joueur : version en cours) | `ghosts/cache/<record>.cprz` |
| Record d'un ami | classement des amis (officiel) ou amis présents au classement (niveau de joueur) | idem |
| Record du classement | autres records avec replay (officiel, 10 premiers) | idem |
| Replay choisi | « Importer un replay… » (fichier `.cprz`, vérifié puis copié) | `ghosts/files/` |

Chaque replay garde à côté de lui une description `.json` (joueur, niveau, empreinte exacte
de la version, record, catégorie, durée, date, temps de passage, version du jeu, vérifié en
ligne ou non). Le replay lui-même n'est jamais modifié.

Les replays téléchargés sont gardés dans un **cache** (64 Mo par défaut, réglable dans
`saves/settings.json` → `ghosts.cacheMB`) ; les plus anciens partent en premier.

### Ce qu'est un fantôme (et ce qu'il n'est pas)

* Un fantôme montre la **trajectoire enregistrée** de la course (position des pieds,
  orientation, état d'animation, planeur, bouclier, 30 fois par seconde, interpolée entre
  deux échantillons). Ce n'est **pas** une re-simulation : le monde n'est pas rejoué pour lui.
* Il est **purement visuel** : aucune collision, aucun projectile, aucun ennemi, aucun
  mécanisme, aucun point de sauvegarde. Il n'entre jamais dans la simulation : une partie
  avec ou sans fantôme donne exactement la même simulation et le même replay.
* Un fantôme n'est **jamais une preuve** : seules les vérifications du serveur (inchangées)
  décident des records en ligne. Un fantôme « vérifié en ligne » vient d'un record que le
  serveur a vérifié ; un fantôme « local » n'a été vérifié par personne.

### Écarts

* **En direct** : le temps qu'il vous a fallu pour atteindre le point le plus avancé de la
  route du niveau (la même que celle de la barre de progression) moins le temps qu'il a
  fallu au fantôme pour le même point. « +2,450 s » : retard ; « -1,230 s » : avance ;
  « 0,000 s » : égalité. Entre deux points de sauvegarde, c'est une estimation (la route est
  une ligne, le joueur peut passer ailleurs).
* **Aux points de sauvegarde** : exact. Depuis la 2.18, chaque replay contient l'événement
  `Split` (temps de passage à la milliseconde) ; pour un replay plus ancien, le temps de
  passage est lu sur l'échantillon (environ 1/30 s près) et marqué comme approximatif.
  Les points de sauvegarde sont appariés **par position** (un point sauté ou repris ne décale
  pas la comparaison).
* Les écarts se comparent **à la milliseconde** : une égalité reste une égalité.
* **Segments** : la durée entre deux points (pas le temps cumulé) ; un segment plus court que
  votre meilleur est en **or**. L'écran « Segments » de fin de course sépare toujours les
  durées et les temps cumulés.

### Versions du niveau

Un fantôme est lié à l'**empreinte exacte** de la version du niveau sur laquelle il a été
fait. Un fantôme d'une autre version (niveau modifié après publication, règles du jeu
changées) reste utilisable mais est affiché « **autre version : comparaison non officielle** »
et n'est jamais compté comme une victoire dans les succès.

### Vérifications d'un replay avant la course

Lecture sûre (taille limitée à 4 Mo, conteneur vérifié), trajectoire présente, durée possible
(2 h au plus), nombre d'échantillons cohérent avec la durée, course terminée, positions dans
le niveau, aucun déplacement impossible hors mort, réapparition ou téléportation (3
tolérés : téléporteurs du niveau), temps de passage dans l'ordre. Un replay refusé est
annoncé (« Fantôme ignoré : … ») et la course continue sans lui.

### Mode entraînement

Départ à un point de sauvegarde choisi, arrivée au suivant (ou à la sortie). Les pouvoirs des
tablettes placées avant le départ sur la route sont donnés (les derniers si les
emplacements manquent). Le fantôme part du moment où il est passé à ce point. Le segment
recommence tout seul ; R (ou Vue à la manette) recommence à tout moment.
**Aucun record, aucune progression, rien n'est envoyé en ligne.**

### Commandes

| Action | Clavier | Manette |
|---|---|---|
| Afficher / masquer les fantômes | G | clic du stick gauche |
| Mode concentré | T | Partage (Micro sur PS5) ou pavé tactile |
| Recommencer (contre-la-montre, entraînement) | R | Vue |

Toutes se changent dans *Paramètres → Contrôles*. Les options des fantômes (nombre à la fois,
opacité, couleurs, noms, écart, panneau) sont dans la pause (*Fantômes*) et sur l'écran
d'avant le départ.

### Performances

Les replays sont lus, vérifiés et placés sur la route **hors du fil principal** pendant le
chargement du niveau ; la mise à jour de 8 fantômes coûte environ 0,001 ms par image (test
`ghost: several ghosts at once`). Aucune requête réseau pendant la course : les
téléchargements se font avant le départ.

### Limites connues

* Le fantôme est la trajectoire enregistrée, pas une re-simulation : les ennemis, blocs et
  portes du monde ne sont pas montrés dans l'état où ils étaient pour lui.
* L'état des pouvoirs montré se limite au planeur et au bouclier (et à l'animation).
* En entraînement, l'état du niveau n'est pas celui d'une course complète arrivée à ce point
  (ennemis déjà vaincus, leviers, portes) : seuls les pouvoirs sont reconstitués.
* L'écart en direct est une estimation entre deux points de sauvegarde.
* Les classements en ligne ne gardent un replay que pour les records vérifiés : un joueur
  sans record vérifié n'a pas de fantôme en ligne.

## Succès

134 succès en 12 catégories (`data/achievements/achievements.json`), chacun avec 1 à 5 étoiles
de difficulté (344 étoiles). Ils se débloquent d'après les vraies sauvegardes
(progression, records, statistiques des boss) — y compris rétroactivement — et des compteurs
de partie gardés dans `saves/achievements.json` (ennemis vaincus, sauts, chutes, pouvoirs
utilisés, segments en or…). Certains succès en suivent d'autres (les fils de la toile) : un
succès ne se débloque qu'une fois les précédents obtenus.

* Les succès « niveau » (sans mourir, sans dégât, tout ramasser, tous les secrets, tous les
  ennemis) comptent les niveaux de la **campagne** joués depuis le départ.
* Les pouvoirs passifs (Triple saut, Ruée aérienne, Aimant) se comptent en niveaux terminés
  en les possédant ; le Planeur à chaque ouverture.
* Les succès de la catégorie Communauté demandent Cosmic Online ; aucun n'est nécessaire au
  reste du jeu.

**Profil en ligne** : quand vous êtes connecté, la liste des succès débloqués est envoyée
(`PUT /api/v1/players/me/achievements`). Le serveur ne garde que les identifiants qu'il
connaît, recompte lui-même les étoiles avec sa copie du catalogue
(`server/src/achievementCatalog.json`, régénérée par `node scripts/achievements-catalog.mjs`)
et les affiche sur le profil comme **déclarées par le jeu, non vérifiées**.

## Mode concentré

Un HUD réduit : par défaut la vie, l'énergie, les pouvoirs et la vie du boss. Les éléments
gardés se choisissent dans l'éditeur du HUD (onglet « Mode concentré ») ; places et tailles
sont communes aux deux modes (deux éléments ne se chevauchent dans aucun des deux). Les
**messages du niveau** (indications, titres de pouvoirs et de boss) s'affichent ou non dans
chaque mode.
