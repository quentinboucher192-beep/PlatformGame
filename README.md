# Cosmic Platformer

Jeu de plateforme 2D en C++20 / SDL3, avec éditeur de niveaux, éditeur d'ennemis,
système de planètes, records et partage de niveaux par fichiers `.cplanet`.
Fonctionne entièrement hors ligne ; **Cosmic Online** (facultatif) ajoute les classements
mondiaux, les records vérifiés et un globe terrestre interactif.

**Nouveau dans la version 2.49.0** :
* **« Le Cœur de Pierre »** (EXTRÊME, Les Confins) : les galeries de granit, puis **le Monolithe,
  le Golem Primordial**, boss géant à huit phases qui combat au sol.
* Succès et récompenses du Vestiaire du Monolithe (combinaison Granit, Éclats de roche, Séisme
  runique, Cercle runique, titre Briseur de pierre).
* Sa musique, **« Le Golem Primordial »**, au juke-box (22 morceaux).

**Nouveau dans la version 2.48.0** :
* **Les Confins**, sixième planète, au bord du système (ouverte dès le départ), et son niveau EXTRÊME **« La Gueule du Dragon »** : la croûte brûlée des Confins, une
  seule vie, aucun checkpoint, puis **Pyrrhax, le Dragon d'Obsidienne**, boss géant à huit phases.
* **Réessayer en entraînement** après une chute dans un niveau EXTRÊME : réapparition sur place,
  hors classements, avec un bandeau permanent.

**Nouveau dans la version 2.47.0** :
* **Un compte, un ordinateur** : le compte se lie à son ordinateur ; une connexion depuis un autre
  est refusée et signalée (alerte « Sécurité »), un seul compte par ordinateur.
* **Local et en ligne** : déconnecté, on joue en local ; à la reconnexion, « De retour en ligne »
  montre ce qui part au compte. Copie en ligne de la progression (EN LIGNE › PARAMÈTRES › Cet ordinateur).
* **Administration** : délier un compte ou le lier au nouvel ordinateur d'un joueur.
* Éditeur d'entités : le bandeau « lecture seule » des entités natives reprend celui de l'éditeur de niveaux.
* Correctifs : « Garder les records » garde aussi la course à la vérification ; présence masquée
  affichée par un rond barré ; chrono indépendant de l'intro.

**Nouveau dans la version 2.46.0** :
* **Entités natives en lecture seule** dans l'éditeur d'ennemis (EN LIGNE › ATELIER), avec
  « Dupliquer pour modifier ».
* **Administration › Ennemis** : un admin modifie une entité native et la publie ; chaque niveau
  de la campagne qui l'utilise reçoit une nouvelle révision, avec une case par niveau pour
  réinitialiser ou garder ses records.

**Nouveau dans la version 2.45.0** :
* **Créations** (Administration) : combinaisons recolorées créées par un admin (nom, trois
  couleurs, aperçu animé), données en récompense des défis et portées partout sans mise à jour.

**Nouveau dans la version 2.44.0** :
* **Replays téléchargés** : gardés sur le PC depuis les classements, défis et profils, et regardés
  sans connexion dans Collection › Replays.
* **Campagne modifiable** (Administration) : publication d'une nouvelle révision d'un niveau, avec le
  choix de réinitialiser ou garder les records.

**Nouveau dans la version 2.43.0** :
* **Récompenses par défi** (Administration) : éléments de la Collection ou titres créés sur le
  serveur, selon le palier, le Top N ou le niveau fini ; accordés par le serveur à la fin du défi.
* **Sauvegardes** (Administration) : copies de toutes les données, téléchargement chiffré, import.

**Nouveau dans la version 2.42.0** :
* **Calendrier des défis** dans l'écran Administration (niveau, règles, objectif « Vitesse » ou
  « Finir le niveau » avec paliers en % du parcours, retour au tirage).
* **Entraînement de boss** : choix du lieu d'apparition sur la carte de l'arène.

**Nouveau dans la version 2.41.0** :
* **Badges du profil** : dès 100 étoiles, 2 succès à afficher sur le profil Cosmic Online (3 dès
  250 étoiles), choisis dans Collection › Vestiaire ; le serveur vérifie succès et étoiles.

**Nouveau dans la version 2.40.1** :
* **Plus de catégorie SPEEDRUN** dans les classements : elle doublait ANY% (et ses notifications).

**Nouveau dans la version 2.40.0** :
* **Rôle administrateur** : un écran Administration réservé aux comptes admin (joueurs, rôles,
  journal des actions) ; le serveur vérifie le rôle à chaque demande, aucune clé dans le jeu.

**Nouveau dans la version 2.39.0** :
* **Barres de défilement à la souris** : attraper et faire glisser chaque barre, ou cliquer dans
  son couloir pour y sauter ; une liste déplacée reste où on l'a laissée.

**Nouveau dans la version 2.38.0** :
* **Calendrier des défis illustré** : l'image du niveau dans chaque case, un ruban par type de
  défi et des balises (EXTRÊME, communauté, cœurs, sans checkpoint, sans station).

**Nouveau dans la version 2.37.0** :
* **Fiche de chaque boss** (Statistiques des boss) : PV, phases, attaques, faiblesses, limite de
  temps, et une difficulté de 1 à 10 étoiles calculée d'après ses données.

**Nouveau dans la version 2.36.0** :
* **Regarder un ami jouer** : « Regarder » dans SOCIAL › Amis rejoue en direct la partie d'un
  ami (amis acceptés seulement, sans notification ; réglage « Mes amis peuvent me regarder jouer »).

**Nouveau dans la version 2.35.0** :
* **Récompenses des défis** : cinq cosmétiques (titre, cadre, traînée, combinaison, victoire)
  gagnés par les parties de défi vérifiées, montrés en icônes avec infobulle avant de jouer.

**Nouveau dans la version 2.34.1** :
* **« Le Cœur d'une Étoile Mourante »** devient le combat seul contre Singularis (choix des
  pouvoirs puis l'arène géante), comme L'Arène des Échos.

**Nouveau dans la version 2.34.0** :
* **Singularis, le Cœur Effondré** : gardien à huit phases dans une arène géante, au cœur du
  niveau EXTRÊME **« Le Cœur d'une Étoile Mourante »** (Noyau stellaire, une seule vie).

**Nouveau dans la version 2.33.1** :
* Présence des amis aussi dans les discussions privées et dans le profil d'un ami.

**Nouveau dans la version 2.33.0** :
* **Défis de la semaine et du mois**, **calendrier** des défis (anciens défis rejouables hors
  délai), classement **« Plus loin »** des essais notés par le serveur.
* **Présence des amis** dans SOCIAL (en ligne, absent, hors ligne) ; un clic sur un avatar ouvre
  le profil du joueur.

**Nouveau dans la version 2.32.2** :
* **Replays partout** : chaque joueur classé a son replay (CLASSEMENTS, NIVEAUX EN LIGNE, course à
  la vérification EXTRÊME). Profil : onglet « Meilleures progressions », recherche et filtres.
* **Niveaux EXTRÊME** : un encadrement qui fait peur (rouge sang, flammes, sang qui coule, griffes).

**Nouveau dans la version 2.32.1** :
* **Alerte de mise à jour** en bas du menu principal : un clic télécharge l'installateur (vérifié
  par SHA-256) et lance l'installation. « Touches… » avant un défi ; boutons de CLASSEMENTS
  uniformes.

**Nouveau dans la version 2.32.0** :
* **Défis du jour** (menu « Défis ») : un niveau par jour avec ses règles et trois paliers, niveau de
  la campagne ou niveau ONLINE LEVELS choisi par l'administrateur, classement du jour des temps
  vérifiés, série, archives, fantôme du 1er. Jouable sans compte et hors ligne (résultat gardé sur
  le PC).

**Nouveau dans la version 2.31.2** :
* Les **boss du niveau** s'affichent dans le choix du niveau, à côté du titre (« +N » quand il y en a
  beaucoup) ; « Mode concentré » de la pause aligné sur les autres boutons.

**Nouveau dans la version 2.31.1** :
* Quitter un entraînement par segment ramène à l'écran d'entraînement, sur le même segment.

**Nouveau dans la version 2.31.0** :
* **Touches des pouvoirs par niveau** : chaque pouvoir garde sa touche, quel que soit l'ordre de
  ramassage. Le créateur les choisit dans l'éditeur ; le joueur les change dans le choix du niveau
  (« Touches… ») ou dans la pause. Plus de touches d'emplacement dans les paramètres ; records
  conservés.

**Nouveau dans la version 2.30.2** :
* **Entraînement par segment** : aperçu du segment, meilleur segment, temps dans le record,
  meilleur temps à l'entraînement (gardé), **Sum of Best** et gain possible, choix des pouvoirs.
* **Boutons de la souris et molette** liables aux commandes (*Clavier / souris*), toutes souris.
* Notification **« Record battu »** quand un temps vérifié vous prend un record mondial.

**Nouveau dans la version 2.30.1** :
* **Replays recadrés** : la partie tient dans la place que laissent le panneau et les commandes,
  rien n'est caché dessous ; **« Masquer les infos »** (touche I, Select à la manette) lui donne
  toute la largeur. Les noms A et B ne s'écrivent plus sur la jauge du boss ni sur le HUD.

**Nouveau dans la version 2.30.0** :
* **Pastilles « NOUVEAU » sur tout le chemin** jusqu'à chaque nouveauté, même très profonde : menu
  principal, onglets, catégories des succès et de la Collection, cartes, fiches du Guide des
  pouvoirs. La pastille d'un parent ne part que quand tout ce qui est nouveau dessous a été vu.
* Sur la toile des succès, un point doré marque chaque nouveau succès (pastille près du nom en
  zoomant) ; le cadre d'animation des statistiques des boss se choisit aussi au clavier/manette.

**Nouveau dans la version 2.29.1** :
* **Une touche par commande** (*Paramètres › Clavier / souris* et *Manette*, une seule colonne) :
  plus de seconde touche cachée ; les flèches et la croix déplacent toujours Nova. Un bouton
  **« Par défaut »** par ligne remet cette commande seule à sa touche d'origine.

**Nouveau dans la version 2.29.0** :
* **72 nouveaux succès** (218 en tout, **597 étoiles**, 13 catégories dont « Collection ») :
  médailles par planète, chaque phase d'un boss sans être touché, ennemis par famille,
  comparaisons de replays, musiques et créatures, tenues équipées, notes et commentaires.
* **22 nouvelles récompenses** dans la Collection (53 en tout) : 4 combinaisons, 3 traînées,
  4 cadres, 8 titres, 3 effets de victoire ; Route des étoiles à 28 paliers, toutes les
  récompenses par étoiles accessibles sans compte.
* Corrigé : « Inventaire complet » et « Cartographe des ombres » étaient impossibles (18 niveaux
  demandés, 13 en ont).
* Corrigé : changer une touche ou un bouton dans les paramètres ne déclenche plus d'autre action
  (la touche choisie ne sert qu'à la nouvelle commande, jusqu'à ce qu'elle soit relâchée).
* Claviers AZERTY / QWERTY : touches gardées par leur place sur le clavier et toujours affichées
  avec le nom que leur donne le clavier du joueur (ZQSD ou WASD), aides comprises.

**Nouveau dans la version 2.28.0** :
* **Entraînement contre les boss** (*Statistiques › un boss › Entraînement*) : cœurs, nombre
  d'emplacements et pouvoirs au choix, toutes les phases ou une seule, dans le vrai niveau du
  boss juste avant son arène ; fin d'entraînement avec temps, dégâts et phase atteinte.
* **Ne compte jamais** : ni succès, ni statistiques des boss, ni records, ni progression, ni
  récompenses, rien en ligne (`GameWorld::training()`, vérifié par chaque enregistrement ;
  tests dédiés). Les derniers réglages sont gardés pour chaque boss (`saves/boss_training.json`).

**Nouveau dans la version 2.27.0** :
* **Statistiques des boss** : le boss choisi s'anime avec ses vraies animations, l'une après
  l'autre (attaques et préparations comprises) ; avant de l'avoir battu, une silhouette.
* **« Où l'affronter »** : tous les lieux où combattre chaque boss, lus dans les niveaux du jeu
  (`game/content/CreaturePlaces`, sur un fil à part) : niveau de la campagne, dernier point de
  sauvegarde avant lui, pouvoirs trouvés en route, et son écho dans L'Arène des Échos. Un lieu
  ouvre la page du niveau ; un lieu verrouillé dit pourquoi.

**Nouveau dans la version 2.26.0** :
* **Globe** : les points des pays sont bien visibles (taille selon l'écran et le zoom,
  contour, nombre de joueurs dans une pastille). **SOCIAL** montre un globe de tous les
  joueurs par pays (centre du pays seulement ; uniquement ceux qui ont choisi d'apparaître sur
  le globe avec un profil public ; route publique `GET /globe/players`, sans identifiant).
* **Messages non lus** : un compteur sur SOCIAL, sur « En ligne » au menu principal, sur
  l'onglet Discussions et sur chaque conversation ou groupe (`GET /social/unread` détaillé).
* En anglais, les noms de groupes et les messages des joueurs restent tels qu'ils ont été écrits.

**Nouveau dans la version 2.25.0** :
* **Touches des pouvoirs par emplacement** : « Pouvoir 1 » à « Pouvoir 8 » se règlent dans
  *Paramètres › Clavier / souris* et *Manette* ; la touche d'un emplacement utilise le pouvoir
  qui y est équipé. Les anciennes touches de pouvoirs deviennent celles des emplacements 1 à 6.
* Les pouvoirs qui n'utilisent pas la touche de leur emplacement (passifs, ou liés au saut, à
  l'attaque, à Bas + Attaque, à Interagir) ont un **cadre pointillé doré** dans le HUD, au choix
  d'un pouvoir et dans le guide, avec la touche concernée ; les fiches des autres pouvoirs
  n'affichent plus de touche. Simulation version 10 : les anciens replays et records restent
  valides et rejouables à l'identique (format de replay 3).
* Pastille **« NOUVEAU »** sur chaque nouveauté d'une mise à jour et sur le menu qui y mène
  (`data/news/new_features.json`) ; elle part dès que l'élément a le focus ou un clic.

**Nouveau dans la version 2.24.0** :
* **Version française entièrement en français** : les boutons et titres restés en anglais
  (NIVEAUX EN LIGNE, CLASSEMENTS, ATELIER, VOIR LE REPLAY, ENVOYER LE RECORD, SANS MORT,
  SANS DÉGÂT, En ligne…) sont traduits ; la version anglaise ne change pas.
* **Corrigé** : « Retour » dans la Collection fermait le jeu. Il ramène au menu principal, et
  aucun « Retour » ne peut plus fermer le jeu (seul « Quitter » le fait).
* **Replay de comparaison** : la barre de progression du niveau montre les deux personnages
  (A sur la barre, B dessous, avec leur lettre et leur couleur).

**Nouveau dans la version 2.23.0** :
* **Collection en ligne** : le cadre d'avatar et le titre équipés suivent le joueur dans les
  CLASSEMENTS et sur son profil public, qui montre aussi sa tenue
  (combinaison, traînée, cadre, effet de victoire). Les fantômes portent la combinaison de
  leur joueur, teintés de leur couleur.
* Le serveur n'accepte que les récompenses débloquées par les succès déclarés par le jeu
  (`PUT /players/me/cosmetics`) ; un profil privé ne montre rien de tout cela. Affichage
  seulement : rien n'entre dans la vérification des records. Voir
  [docs/COLLECTION.md](docs/COLLECTION.md).

**Nouveau dans la version 2.22.0** :
* **Français ou anglais** : l'installateur Windows demande la langue (il s'affiche lui-même
  dans la langue choisie) et le jeu démarre dans celle-ci ; *Paramètres › Jeu › « Langue /
  Language »* la change à tout moment, avec effet immédiat. Tout est traduit : menus,
  écrans, HUD, niveaux et messages du monde, ennemis et gardiens, pouvoirs, succès,
  Collection, actualités, éditeurs et messages connus du serveur Cosmic Online. Les
  contenus des joueurs (niveaux de la communauté, pseudonymes, commentaires, messages)
  restent tels que leurs auteurs les ont écrits. En anglais, le badge EXTREME, les nombres
  (2.45 ; 12,345) et les pluriels suivent l'usage anglais.
* Le français reste la source de tous les textes ; l'anglais vient du dictionnaire
  `data/i18n/en.json`, appliqué à l'affichage (`tools/i18n/extract.py` liste ce qui reste à
  traduire). Voir [docs/I18N.md](docs/I18N.md).

**Nouveau dans la version 2.21.0** :
* **Notes des joueurs** pour les niveaux de la campagne : un niveau terminé avec un temps
  **vérifié** en ligne peut être noté de 1 à 5 étoiles (écran de fin ou la section En ligne) ; la moyenne
  (« 4,3 ★ · 128 notes »), calculée par le serveur, s'affiche sur la fiche du niveau et dans
  ONLINE, à côté de la difficulté calculée (qui ne change pas). Hors ligne : la dernière
  moyenne reçue, marquée comme telle.
* **Commentaires des niveaux de la campagne**, visibles **uniquement dans la section En ligne** (bouton
  *Avis et commentaires*), et **commentaires sur les runs** des classements (une bulle sur
  chaque ligne ; l'auteur du run est notifié). Mêmes règles que les commentaires des niveaux
  des joueurs : réponses, réactions, signalement, modération, profils masqués, export et
  suppression des données.

**Nouveau dans la version 2.20.0** :
* **Aethra**, cinquième planète (ouverte avec le Noyau, après Sylvanox, Glacior et Pyralis) :
  la planète des sanctuaires, des îles flottant sur un océan de nébuleuse. **4 niveaux
  complets** (secrets, reliques, médailles), chacun avec la tablette d'un nouveau pouvoir au
  départ et un **nouveau gardien** à la fin.
* **4 gardiens** et **12 attaques inédites**, toutes annoncées avant de frapper :
  *Mycélia, la Mère-Racine* (racine traqueuse, cage de ronces, inspiration), *Aurorax, le Miroir
  boréal* (reflets, lac gelé, prisme qui rebondit), *Pyrogyre, l'Anneau solaire* (couronne de
  feu, spirale, marée de lave) et *Nyxara, la Dévoreuse d'étoiles* (puits gravitationnel, pas de
  l'ombre, onde d'éclipse ; l'arène s'assombrit en phase 3). Les 12 gardiens classiques ne
  changent pas. **L'Arène des Échos** enchaîne maintenant les **16** gardiens.
* **4 pouvoirs** (bouton *Spécial*) : **Lame boomerang** (touche à l'aller et au retour,
  rapporte éclats et orbes), **Nova de givre** (gèle les ennemis proches, qui deviennent des
  plateformes ; ralentit les boss), **Leurre holographique** (ennemis et boss le visent 3 s),
  **Balise de rappel** (retour instantané à la balise dans les 5 s).
* **12 nouveaux succès** (146 au total, 383 étoiles), nouveaux gardiens dans les
  *Statistiques* et la *Galerie*, et 6 nouvelles musiques dans le juke-box.

**Nouveau dans la version 2.19.0** :
* **Collection** (menu principal → *Collection*) : des **récompenses visuelles** débloquées par
  les étoiles des succès — 7 **combinaisons** de Nova, 4 **traînées**, 5 **cadres d'avatar**,
  des **titres** tirés des succès et 3 **effets de victoire**. Le *Vestiaire* les montre sur
  Nova et les équipe ; la *Route des étoiles* présente les 17 paliers et les récompenses de
  succès précis. Rien ne s'achète, tout se débloque aussi après coup, et rien ne change dans la
  partie (ni simulation, ni replay, ni record). À la fin d'un niveau, une carte présente les
  nouvelles récompenses : *Équiper* ou *Plus tard*.
* **Musiques** : un juke-box des morceaux déjà entendus en jeu (lecture, pause, position,
  précédent / suivant, répéter, aléatoire) ; un morceau peut devenir la musique du menu.
* **Galerie** : les planètes atteintes (enregistrables en PNG dans le dossier *Galerie*),
  les gardiens battus (animations, victoires et combats sans dégât) et les créatures
  rencontrées.
* **Lecteur de replay avancé** : vitesses ×0,25 à ×4, image par image (avant et arrière),
  barre de temps, sauts aux points de sauvegarde, entrées du joueur ; **comparer deux
  replays** avec un **décalage de temps** (±10 ms à ±1 s, saisie directe) ou un **recalage sur
  un point de sauvegarde**, tableau des segments (voir `docs/GHOST_RACING.md`).
* Corrigé : mourir contre un boss puis le finir sans être touché n'est plus annoncé « sans
  dégât » (ni la phase reprise). Champs de texte : curseur, sélection, **Ctrl+A / C / X / V**.

**Nouveau dans la version 2.18.0** :
* **Ghost Racing** : courez contre des **fantômes** : vos propres courses (meilleur temps,
  record précédent, dernière course de cette version du niveau), le **record mondial**, les
  records de vos **amis**, un replay **téléchargé** ou **importé** — jusqu'à 8 à la fois. Les
  fantômes sont translucides, colorés, entourés d'une lueur, avec leur nom et leur temps ; ils
  ne touchent à rien (aucune collision, aucun mécanisme, aucun point de sauvegarde). Touche
  **G** (clic du stick gauche) pour les masquer, options dans la pause.
* **Écarts en direct** sous le chronomètre (« +2,450 s » de retard, « -1,230 s » d'avance),
  temps du fantôme au même endroit, fin estimée, et un **panneau des segments** façon
  LiveSplit (durée et écart de chaque segment, temps cumulé et écart cumulé, meilleurs
  segments en or, animation de nouveau record) — un élément « Fantômes » de l'éditeur du HUD.
* **Avant le départ** (« Fantômes… » sur la fiche d'un niveau, « Défier ce temps »
  dans CLASSEMENTS, « Course contre un fantôme… » dans NIVEAUX EN LIGNE) : choix des
  fantômes, version et type de record affichés, options d'affichage, et le **mode
  entraînement** (un segment depuis un point de sauvegarde, répété contre les fantômes, sans
  aucun record). En fin de course : écart sur chaque fantôme et écran **Segments** (durées,
  temps cumulés, meilleurs segments, record, fantômes, historique).
* **Succès** (menu principal → *Succès*) : **134 succès** (146 depuis la 2.20) en 12 catégories, avec 1 à 5
  **étoiles** de difficulté, présentés sur une **toile d'araignée** (un secteur par catégorie,
  les étoiles du dehors vers le cœur, des fils entre les étapes, zoom et déplacement) ou en
  liste. Ils se débloquent d'après vos vraies sauvegardes ; vos étoiles s'affichent sur votre
  profil en ligne, comme **déclarées par le jeu** (non vérifiées par le serveur).
* **Mode concentré** : un HUD réduit (vie, pouvoirs, boss par défaut), choisi dans l'onglet
  « Mode concentré » de l'éditeur du HUD ; touche **T** (Partage à la manette) ou menu pause.
  Les **messages du niveau** (indications, titres) s'affichent ou non dans chaque mode.

**Nouveau dans la version 2.17.0** :
* **S'accroupir** : Bas seul au sol, même sur place, baisse le personnage pour laisser passer
  les attaques au-dessus (Bas en courant reste une glissade, une diagonale fait marcher).
* **Combats de boss** : la caméra montre **toute l'arène** pendant le combat (vérifié sur les
  24 combats de la campagne) ; une **flèche lumineuse qui clignote** sur le bord de l'écran
  indique un boss hors de vue ; les **créatures invoquées disparaissent avec leur boss**.
* **Statistiques des boss** (menu principal → *Statistiques*) : combats, victoires, défaites,
  victoires sans dégât, et pour chaque phase le nombre de fois battue et battue **sans
  dégât**, meilleur combat, dernière victoire. Un petit son salue chaque phase battue sans
  dégât ; l'écran de fin résume les boss vaincus. Gardées sur l'ordinateur, jamais envoyées.
* **Pouvoirs** : coûts d'énergie selon leur force (référence : le tir chargé à 20, une attaque
  moins forte coûte moins). Leurs **caractéristiques** (énergie, dégâts, rayon, portée,
  vitesse, recharge…) sont lues dans leur définition et affichées sur les cartes du guide,
  dans la **fiche** de chaque pouvoir (une carte du guide l'ouvre) et quand on les ramasse.
* **Nouvelle génération de classement** : les règles changent (coûts, créatures des boss), les
  classements en ligne de la campagne repartent à zéro (anciens records archivés).
* Correction : un profil ouvert depuis un classement ne se recharge plus quand on clique sur
  ses onglets (un écran recouvert par un autre ne reçoit plus aucun clic).

**Depuis la version 2.9** (résumé) : **NIVEAUX EN LIGNE** s'enrichit de favoris, commentaires
avec réponses et réactions, profils de créateurs, abonnés et notifications en ligne, et du
temps joué par niveau ; **PROJETS** permet de créer un niveau à plusieurs (rôles, invitations
à clé, demandes d'adhésion, **édition en direct** avec présence des autres, verrous et
discussion, historique des révisions et versions nommées) ; la pause montre une **carte des
zones explorées** ; les **niveaux EXTRÊME** (une seule vie) ont une **course à la
vérification** : chaque tentative est classée selon sa progression (parcours, vie retirée aux
boss, vagues d'embuscade) et le premier à finir le niveau en devient le vérificateur.

**Nouveau dans la version 2.8.0** : les **rayons des boss** ne sont plus des traits : ils sont
dessinés avec des textures animées (`animations/beams.json`, générées par
`tools/assetgen/characters/beams.py` dans les palettes des souffles des boss) — flamme pour
Pyralis, givre pour Glacior, plasma et sève pour Sylvanox, lumière d'étoile pour l'Astrarque
— avec une embouchure, un corps répété sur toute la longueur et une extrémité effilochée.
Dans l'éditeur d'ennemis (onglet Attaques), la *texture du rayon* et son *épaisseur* se
choisissent avec un aperçu animé, à côté des sprites, animations, projectiles, effets et sons
déjà paramétrables. L'aspect d'un rayon ne fait pas partie de l'empreinte des niveaux : le
changer ne fait perdre aucun record. Le faisceau n'est plus peint dans les sprites d'attaque
des boss (`tools/assetgen/characters`, seuls les clips de rayon et de télégraphe changent) :
un seul rendu, sans double trait.

**Règles de niveau** (paramètres du niveau dans l'éditeur, affichées sur la fiche du niveau
avant de jouer) : *facteur de dégâts reçus* (×0,25 à ×5) et *nombre de vies* (illimitées par
défaut ; « Plus de vies » met fin à la partie). Le mode défi les affiche aussi (l'Arène des
Échos : « Vies : 1 »). Les niveaux existants gardent leurs règles et leurs empreintes.

**Nouveau dans la version 2.7.0** : le **Noyau stellaire**, l'étoile au centre du système,
s'ouvre quand Sylvanox, Glacior et Pyralis sont terminées (décors propres : l'étoile et les
trois mondes revus dans sa lumière). Deux niveaux :
* **L'Arène des Échos** (boss rush) : les douze gardiens de la campagne, l'un après l'autre,
  en *échos* spectraux — même combat, mais aucun orbe de vie à leur mort. Aucun soin, aucun
  checkpoint, **une seule vie** (mode défi) ; on choisit ses quatre pouvoirs au départ.
* **Le Cœur de l'Étoile** (niveau final, selon la maquette) : le Seuil (huit pouvoirs, on en
  garde quatre), les échos de Sylvanox, de Glacior et de Pyralis (salles calibrées des
  épreuves, embuscades, Sentinelle Prime, Hydre de Givre et Forgeron Primordial en échos),
  la Couronne (les trois mondes mêlés, deux embuscades à la suite) et le Cœur : l'**Astrarque**
  (190 PV, trois phases, attaques de quatre gardiens, rage après 90 s), sur des plateformes
  qui tournent, la caméra élargie pour qu'il ne frappe jamais hors de l'écran. L'Astrarque
  reprend la silhouette du Forgeron Primordial, agrandie et dorée.
* Barre de progression : une seule icône par boss ; compteur d'éclats masqué quand le niveau
  n'en a pas. Éditeur d'ennemis : *teinte du sprite*.

**Nouveau dans la version 2.6.0** : **éditeur du HUD** (Paramètres › Jeu › « Personnaliser le
HUD », ou `--hud-editor`). Sur un aperçu du vrai HUD posé sur un vrai niveau, chaque élément —
vie et énergie, pouvoirs, éclats et reliques, chronomètre, barre de progression, vie du boss —
se déplace (glisser à la souris, ou curseurs de position au clavier et à la manette), change de
taille (50 à 200 %, molette ou curseur) et peut être masqué. Deux éléments ne peuvent jamais se
chevaucher ni sortir de l'écran : un déplacement qui le ferait est bloqué (l'élément glisse le
long de l'obstacle) et un message dit pourquoi ; une disposition en conflit ne s'enregistre pas.
Les positions sont relatives à l'écran : elles suivent la résolution. « Tout réinitialiser »
revient à la disposition d'origine.

**Nouveau dans la version 2.5.0** :
* **4 nouveaux pouvoirs** (14 au total) : **Ralenti temporel** (les ennemis et leurs tirs
  ralentissent quelques secondes) et **Bombe plasma** (rebondit puis explose : dégâts autour
  d'elle, brise la roche fissurée) sur la nouvelle touche **Spécial** (F, manette R3) ;
  **Aimant** (éclats, reliques et orbes proches filent vers vous) et **Ruée aérienne** (un Dash
  de plus avant de toucher le sol), passifs. Réserve d'énergie portée à 150, barre agrandie.
* **Guide des pouvoirs** (menu Pause et aide de l'éditeur, ou `--power-guide`) : chaque
  pouvoir avec un exemple animé joué en boucle par le moteur du jeu, sa touche et ce qu'il
  fait. Les tests vérifient que chaque exemple montre réellement le pouvoir réussir.
* **Les boss gardent leurs blessures** quand vous mourez dans leur arène : à votre retour, ils
  ont la vie et la phase où vous les avez laissés (réglable par arène dans l'éditeur).
* Barre de progression **sans encadré** : rail lumineux, repères et pourcentage seulement.

**Nouveau dans la version 2.4.0** :
* **Barre de progression** du niveau (Paramètres › Jeu : en haut, en bas ou masquée) : une
  capsule lumineuse avec le pourcentage parcouru, la section en cours, les checkpoints, les
  **embuscades** (tête de mort à la couleur choisie dans l'éditeur, champ « Aspect » de la zone ;
  par défaut la couleur de la planète), les **boss** (leur portrait, ou l'icône et la couleur
  choisies dans l'éditeur d'ennemis, onglet Boss, avec aperçu) et le **portail** de fin.
* **Notifications** dans le menu principal : nouveaux niveaux de la campagne, nouveaux pouvoirs,
  monstres et boss, niveaux modifiés, **records perdus** (un temps établi sur une autre version
  d'un niveau ne compte plus : il est retiré et c'est dit), version installée avec ses
  nouveautés, et **nouvel installateur** disponible : téléchargement depuis le serveur,
  vérification SHA-256, puis « Lancer l'installateur ».

**Nouveau dans la version 2.3.0** : **4 pouvoirs portés au plus** dans chaque niveau (réglable
par niveau dans l'éditeur). Toucher la tablette d'un nouveau pouvoir quand tout est pris
ouvre un choix : remplacer un pouvoir porté (il reste sur la tablette, à reprendre quand vous
voulez) ou laisser le nouveau. Les niveaux de la campagne partent avec 4 pouvoirs au plus et
des **stations de pouvoirs** (tablettes en hauteur après certains checkpoints, avec un
panneau) proposent ceux dont la suite a besoin : on ne peut jamais rester bloqué. Le choix
fait partie du replay. Les niveaux créés ou importés restent dans l'Atelier, jamais dans la
campagne.

**Contenu de la version 2.2.0** : 3 planètes et 12 niveaux, chacun avec son boss. Le
quatrième niveau de chaque planète est une **épreuve** bâtie sur une mécanique propre à la
planète, avec un gardien d'élite qui n'apparaît nulle part ailleurs :

* *Les Catacombes des Anciens* (Sylvanox) — **la machinerie des Anciens** : salles des
  métronomes (lasers qui battent la mesure au-dessus de dalles qui s'effritent : un saut par
  temps), porte du gardien du temps (Onde sur un récepteur, puis course avant qu'elle se
  referme), ancrages entre deux tirs de laser, murs de cristal brisés au Dash en plein saut,
  salle des leviers (chaque levier ouvre le rideau laser devant vous et referme celui de
  derrière). Gardien : l'**Archonte des Catacombes**.
* *Le Creuset Profond* (Pyralis) — **la lave et la forge** : radeaux de basalte (le premier
  part quand vous montez : choisissez le moment, un laser coupe sa route), cheminées de
  chaleur à remonter en planant sous une voûte hérissée, pistons de forge, fonderie (tir
  chargé sur le récepteur, puis course sur des dalles qui s'effondrent), descente du
  creuset en brisant trois dalles au martèlement. Gardien : le **Cœur du Creuset**.
* *La Crevasse Hurlante* (Glacior) — **la glace et le vent** : pentes de glace vive, ponts de
  givre qui cèdent au premier pas, lac gelé à traverser à la nage sous la banquise, couloir
  des vents et rayons de givre, pics glissants au-dessus des pointes. Gardienne : la **Wyrm
  Hurlante**.

Chaque épreuve ne donne que les pouvoirs dont elle a besoin, et ses fosses sont peu profondes
(le vol plané prolonge un saut, il ne survole plus une salle). Les salles clés sont jouées par
des tests avec le vrai moteur (bots scriptés). La **note de difficulté** affichée est calculée
à partir du niveau (voir [docs/LEVEL_FORMAT.md](docs/LEVEL_FORMAT.md)) : 2,5 à 4/5 pour la
campagne, les épreuves en tête.

| Planète | Niveaux (boss) |
|---|---|
| **Sylvanox**, le monde-jardin | Les Ruines Suspendues (Thornwarden) · La Canopée des Spores (Reine Sporale) · Le Cœur de la Machine (Sentinelle Prime) · *Les Catacombes des Anciens* (Archonte des Catacombes) |
| **Pyralis**, la forge en fusion | Les Forges Englouties (Magmaroth) · Le Fleuve de Lave (Ignivore) · La Fonderie Primordiale (Forgeron Primordial) · *Le Creuset Profond* (Cœur du Creuset) |
| **Glacior**, le sanctuaire gelé | Les Plaines de Givre (Cryowyrm) · Les Grottes de Cristal (Hydre de Givre) · Le Bastion Gelé (Glaciarque) · *La Crevasse Hurlante* (Wyrm Hurlante) |

Aucun niveau ne se contourne : chaque niveau a un plafond (on ne passe plus par le haut) et
le **détecteur de raccourcis** (`CosmicPlatformer --analyze-levels <dossier>`) vérifie, avec
les capacités du joueur dans chaque niveau, qu'aucun point de contrôle ni aucune arène ne peut
être évité ; un test l'exécute sur toute la campagne. Les niveaux classiques ont deux fois
moins d'orbes de soin qu'en 2.1.2.

**Boss** (version 2.2) : ils lisent le combat — distance, joueur en l'air ou perché,
attaques déjà utilisées —, visent là où le joueur va atterrir, enchaînent des combos dans
les dernières phases, punissent le joueur qui reste hors de portée et entrent en rage si le
combat s'éternise ; chacun a 4 nouvelles attaques (saut balistique, lignes d'éruptions
successives, projectiles à tête chercheuse, salves re-visées) et des annonces plus courtes.

51 sortes d'ennemis (12 espèces, chacune en version Sylvanox, feu et glace, plus les boss),
10 pouvoirs, 10 cœurs de vie (plus des cœurs de cristal cachés), 3 reliques et un cœur de
cristal secrets par niveau. Par défaut, l'option *Paramètres → Jeu → Tout débloquer* rend
toutes les planètes et tous les niveaux accessibles ; sans elle, ils se débloquent dans
l'ordre.

## Jouer

| Action | Clavier AZERTY (QWERTY) – principal / secondaire | Manette (Xbox / PlayStation) |
|---|---|---|
| Se déplacer | Q D (A D) / ← → | Stick gauche / croix |
| Haut / bas, grimper | Z S (W S) / ↑ ↓ | Stick gauche |
| Sauter / double saut | Espace / W (Z) | A / Croix |
| Glissade | Bas en courant au sol | Bas (stick / croix) en courant |
| S'accroupir | Bas seul au sol (même sur place) | Bas (stick / croix) sans direction |
| Attaque | J / X | X / Carré |
| Pouvoir 1 (1er emplacement du HUD) | K / Maj gauche | B / Rond |
| Pouvoir 2 | L / C | RT / R2 |
| Pouvoir 3 | O / V | LB / L1 |
| Pouvoir 4 | I / B | RB / R1 |
| Pouvoir 5 et 6 (niveaux à plus de 4 pouvoirs) | U / N, F / H | LT / L2, R3 |
| Pouvoir 7 et 8 | à choisir | à choisir |
| Planeur | maintenir Saut en tombant | maintenir A / Croix |
| Frappe sismique | Bas + Attaque en l'air | Bas + X / Carré |
| Tir chargé | maintenir Attaque, relâcher | maintenir X / Carré |
| Grappin | Interagir près d'un point d'ancrage | Y / Triangle |
| Triple saut | troisième Saut en l'air (passif) | A / Croix |
| Interagir | E / Entrée | Y / Triangle |
| Pause | Échap / P | Menu / Options |

**Touches des pouvoirs (2.24)** : chaque emplacement de pouvoir du HUD a sa touche, que le
joueur choisit (*Paramètres → Clavier / souris* et *Manette*, « Pouvoir 1 » à « Pouvoir 8 ») ;
elle utilise le pouvoir équipé dans cet emplacement, quel qu'il soit (Dash, Onde, Bouclier,
Impulsion, Téléportation, Ralenti, Bombe plasma, Lame boomerang, Nova de givre, Leurre,
Balise de rappel). Deux pouvoirs de l'ancien bouton Spécial se portent donc ensemble. Les
pouvoirs passifs (Aimant, Ruée aérienne) et ceux qui s'utilisent avec un mouvement (Planeur,
Triple saut, Frappe sismique, Tir chargé, Grappin) n'utilisent pas la touche de leur
emplacement : un **cadre pointillé doré** les distingue dans le HUD, le choix du pouvoir à
remplacer et le guide des pouvoirs, avec leur mouvement (ou « passif ») à la place de la
touche. Les paramètres d'avant la 2.24 sont repris : les touches choisies pour les six
pouvoirs d'alors (Dash, Onde, Bouclier, Impulsion, Téléportation, Spécial) deviennent celles
des emplacements 1 à 6.

Les touches sont des **positions physiques** (scancodes) : les déplacements tombent sur ZQSD
en AZERTY et sur WASD en QWERTY, et le jeu affiche toujours le nom réel de la touche. Toutes les commandes se reconfigurent dans
*Paramètres → Clavier / souris* et *Paramètres → Manette* (profils indépendants, zones mortes,
sensibilité, inversion des axes, vibrations, réinitialisation). Les icônes affichées
s'adaptent à la manette utilisée (Xbox, PlayStation ou générique) et basculent
automatiquement entre clavier et manette.

Touches globales : **F3** mode debug (FPS, dt, colliders, hitboxes, position/vitesse du
joueur, états des ennemis, chrono, particules), **F11** plein écran, **F12** capture d'écran.

### Les pouvoirs

Les cinq premiers (Dash, Onde, Bouclier, Impulsion, Téléportation) s'obtiennent dans
« Les Ruines Suspendues » ; les suivants s'apprennent chacun dans un niveau : Planeur
(La Canopée des Spores, courants ascendants), Grappin (Le Cœur de la Machine), Frappe
sismique (Les Forges Englouties, sols fissurés), Tir chargé (Le Fleuve de Lave, récepteurs
lointains, boucliers des Remparts) et Triple saut (Les Plaines de Givre). Les niveaux joués
hors de l'ordre donnent d'office les pouvoirs des niveaux précédents.

Sur Glacior, la glace est glissante et les attaques gelées ralentissent ; sur Pyralis, la
lave, les éruptions et les bombes sont mortelles.

Chaque pouvoir actif coûte de l'énergie selon sa force (jauge de 150) : Dash 5, Impulsion 8,
Onde 10, Frappe sismique 10, Lame boomerang 12, Balise de rappel 15 (pour revenir ; la poser est
gratuit), Tir chargé 20 (la référence), Bombe plasma 20, Bouclier 20, Téléportation 25, Leurre
holographique 25, Nova de givre 30, Ralenti 60 ; les pouvoirs passifs sont gratuits. Le *Guide des pouvoirs*
(menu pause) montre chaque pouvoir joué en boucle par le moteur avec son coût ; sa fiche donne
toutes ses caractéristiques.

Chaque niveau a un chrono (limite de 15 à 20 min), des médailles bronze / argent / or /
platine, records locaux (meilleur temps, morts, collectibles, ennemis vaincus, score,
pouvoirs utilisés, date) avec comparaison au meilleur essai.

## Cosmic Online (classements en ligne)

Menu principal → **En ligne** (compte obligatoire pour cette section, jamais pour jouer) : globe 3D interactif (rotation à la souris ou au stick
droit, zoom molette / gâchettes, clic sur un marqueur ou un pays), classements par niveau,
pays, amis, planète et points cosmiques, catégories ANY% / 100% / SANS MORT / SANS DÉGÂT,
période historique ou hebdomadaire, profils publics, amis (avec notifications des
demandes) et **replays exacts** : le moteur est déterministe (pas fixe de 1/120 s, hasard du
gameplay à graine fixe par niveau, mathématiques identiques sous Windows et Linux), un replay
contient les commandes de chaque pas et le jeu rejoue la partie à l'identique (mêmes
animations, mêmes ennemis, mêmes attaques), avec vérification de l'état toutes les 0,5 s
(`CosmicPlatformer --replay fichier.cprz`, *Revoir la partie* à la fin d'un niveau). Le jeu reste entièrement jouable sans compte ; hors ligne, les
performances sont gardées et envoyées au retour de la connexion. La localisation se limite au pays choisi (affiché au centre du pays), jamais de
position précise.

Le serveur (Cloudflare Workers, D1, R2) est dans [`server/`](server/README.md) ; il se lance en
local sans compte Cloudflare. Documentation complète : [docs/online/](docs/online/README.md).

## Menu principal

*Continuer* (dernier niveau joué) · *Campagne* (planètes et niveaux) · *En ligne* ·
*Notifications* · *Succès* (toile des 218 succès et de leurs 597 étoiles) · *Collection*
(vestiaire, route des étoiles, musiques, galerie) · *Statistiques* (les boss : victoires,
combats et phases sans dégât) · *Paramètres* · *Quitter*.

**En ligne** (compte requis) : **NIVEAUX EN LIGNE** (les niveaux publiés par les joueurs : Populaires,
Plus durs, Récents, Mieux notés, Amis, Recherche, Les miens ; télécharger et jouer, noter,
signaler, classement par niveau ; *ATELIER → Mes niveaux → Publier* après avoir terminé son
niveau depuis le départ dans l'éditeur), **CLASSEMENTS** (classements, globe, profils), **SOCIAL**
(messagerie privée avec vos amis, groupes de discussion jusqu'à 20 membres, amis et demandes,
joueurs bloqués, signalement de messages ; notifications des nouveaux messages et demandes,
gardées pendant une partie et affichées ensuite) et l'**ATELIER** (vos niveaux,
niveaux importés, export / import `.cplanet`, éditeur d'ennemis). Dans ONLINE, un niveau se
joue depuis sa fiche (*Jouer ce niveau*) ; à la fin (ou en quittant), on revient à ONLINE sur
ce niveau.

## Éditeurs

Les **entités natives** (ennemis, boss, pouvoirs, effets et graphismes de la campagne) sont
utilisables dans tous les niveaux des joueurs mais ne sont jamais modifiables : l'éditeur
d'ennemis enregistre vos réglages dans une copie, et un fichier utilisateur ou un package qui
réutilise l'identifiant d'une entité native est ignoré (renommé à l'import).

* **Éditeur de niveaux** (*Online → Workshop → Mes niveaux* ou `CosmicPlatformer --editor [id]`) :
  calques, bibliothèque d'objets, propriétés, grille magnétique, zoom, déplacement de vue,
  sélection multiple, déplacement / redimensionnement / rotation / miroir, copier-coller,
  annuler / rétablir, événements et déclencheurs, cinématiques d'intro / outro, chrono et
  médailles, test immédiat (F5) avec retour à l'éditeur, sauvegarde automatique.
* **Éditeur d'ennemis** (*Online → Workshop → Éditeur d'ennemis* ou `--enemy-editor`) : aperçu animé, statistiques, comportements IA,
  attaques, phases de boss, arène de test où l'on peut contrôler le joueur.

Voir [docs/EDITORS.md](docs/EDITORS.md).

## Partager un niveau

*Online → Workshop → Exporter* crée un fichier `.cplanet` autonome contenant le niveau,
la planète, les ennemis, les pouvoirs, les effets et **tous** les fichiers graphiques et
audio utilisés, avec un manifeste signé par empreintes SHA-256. *Importer* vérifie
l'archive (empreintes, chemins, tailles, version, dépendances, conflits) avant de
l'installer ; aucun code n'est jamais exécuté depuis un package. Avec l'installateur
Windows, un double-clic sur un `.cplanet` ouvre directement l'écran d'import.

En ligne de commande :

```
CosmicPlatformer --export sylvanox_01 MonNiveau.cplanet --author "Mon nom"
CosmicPlatformer --import MonNiveau.cplanet
```

Voir [docs/PACKAGE_FORMAT.md](docs/PACKAGE_FORMAT.md).

## Langue / Language

Le jeu est **en français ou en anglais**. L'installateur Windows demande la langue (il
s'affiche lui-même dans cette langue) et le jeu démarre dans celle-ci ; elle se change à
tout moment dans *Paramètres › Jeu › « Langue / Language »*, avec effet immédiat (ou
`--lang en|fr` en ligne de commande). Le français reste la source de tous les textes ;
l'anglais vient du dictionnaire `data/i18n/en.json`, appliqué à l'affichage. Les contenus
des joueurs (niveaux de la communauté, pseudonymes, commentaires, messages) ne sont jamais
traduits. Ajouter ou compléter une traduction : [docs/I18N.md](docs/I18N.md).

## Données utilisateur

Sauvegardes, paramètres, records, niveaux créés, packages importés, exports, captures et
journal (`cosmic.log`) sont stockés dans :

* Windows : `%APPDATA%\CosmicStudio\CosmicPlatformer\`
* Linux : `~/.local/share/CosmicStudio/CosmicPlatformer/`

Les statistiques des boss sont dans `saves/boss_stats.json` (jamais envoyées au serveur).
Les fichiers sont versionnés et migrés automatiquement ; un fichier corrompu est mis de
côté (`.corrupt`) au lieu de faire planter le jeu. La désinstallation les conserve, sauf
demande explicite.

## Compiler

Prérequis : CMake ≥ 3.24, un compilateur C++20 (Visual Studio 2022, GCC 12+ ou Clang 15+),
Git, et une connexion Internet **uniquement** au premier configure (téléchargement de SDL3,
SDL3_image, SDL3_ttf, SDL3_mixer, nlohmann/json, miniz, aux versions épinglées).

```
cmake -S . -B build
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

L'exécutable est dans `build/bin/`. Installateur Windows : `installer\build_installer.ps1`
(Inno Setup 6). Détails, options et compilation croisée : [docs/BUILDING.md](docs/BUILDING.md).

## Documentation

* [docs/COLLECTION.md](docs/COLLECTION.md) – récompenses de la collection, juke-box et galerie (2.19)
* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) – modules, boucle de jeu, flux de données
* [docs/LEVEL_FORMAT.md](docs/LEVEL_FORMAT.md) – format JSON des niveaux, objets, événements
* [docs/PACKAGE_FORMAT.md](docs/PACKAGE_FORMAT.md) – format `.cplanet` et vérifications à l'import
* [docs/EDITORS.md](docs/EDITORS.md) – guide des éditeurs
* [docs/BUILDING.md](docs/BUILDING.md) – compilation, tests, installateur
* [docs/ASSET_SPEC.md](docs/ASSET_SPEC.md) – contrat des assets (sprites, animations, tuiles, audio)
* [docs/I18N.md](docs/I18N.md) – langues (français / anglais) : choix, fonctionnement, ajouter une traduction
* [docs/GHOST_RACING.md](docs/GHOST_RACING.md) – fantômes, écarts, entraînement, succès, mode concentré (et leurs limites)
* [docs/online/](docs/online/README.md) – Cosmic Online : installation, déploiement, secrets, API, règles, confidentialité, dépannage

## Licences

Code et assets du jeu : Cosmic Studio. Les assets (sprites, décors, sons, musiques) sont
générés par les scripts de `tools/assetgen`. Bibliothèques et polices tierces : voir
[licenses/](licenses/). Le globe utilise des données Natural Earth (domaine public), les
drapeaux flag-icons (MIT), les noms de pays i18n-iso-countries (MIT) et les lumières des villes
de GeoNames (CC BY 4.0, © GeoNames), générés par `tools/globegen`.
