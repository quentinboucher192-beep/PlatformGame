# Données personnelles et localisation (RGPD)

## Principes appliqués

* **Facultatif** : aucun compte n'est nécessaire pour jouer ; sans connexion, le jeu
  n'envoie rien. Même avec un compte, rien n'est envoyé au démarrage si la connexion
  automatique est désactivée.
* **Minimisation** : pas d'adresse e-mail, pas de nom réel, pas de position GPS, pas de
  données de l'ordinateur.
* **Localisation** : uniquement un pays (et éventuellement une grande région) choisi par le
  joueur, jamais déduit. Trois niveaux : *masqué* (par défaut), *classements* (drapeau visible),
  *classements et globe*. Sur le globe, le joueur apparaît au **centre approximatif du pays**
  (table `countries`, calculée à partir de Natural Earth) ; plusieurs joueurs d'un même pays
  sont écartés sur une petite spirale purement visuelle. Aucune adresse n'est jamais déduite.
  Un joueur masqué reste dans les classements sous son pseudonyme, sans drapeau ni marqueur.
* **Globe des joueurs (SOCIAL)** : un second globe montre l'ensemble des joueurs de Cosmic
  Online **sous forme de nombres par pays** (`GET /api/v1/globe/players`), toujours au centre
  approximatif du pays, jamais plus précis. Seuls comptent les joueurs qui ont choisi
  *classements et globe* **et** gardent un profil public : un profil masqué n'y est jamais
  compté, même avec la présence sur le globe activée. La réponse ne contient ni identifiant,
  ni pseudonyme, ni record — uniquement le pays, ses coordonnées approximatives et un
  nombre ; elle est identique pour tous et peut être mise en cache quelques secondes.
  Masquer sa position ou son profil retire le joueur du compte dès la réponse suivante. Hors
  ligne ou sans compte, l'écran l'indique et n'affiche aucune donnée inventée.
* **Regarder ses amis jouer (2.36)** : vos amis peuvent regarder vos parties en direct ;
  vous n'êtes pas prévenu quand quelqu'un regarde (ni notification, ni nombre de
  spectateurs, ni liste : le serveur ne garde aucune trace de qui regarde, la salle de la
  partie ne sait même pas qui lit). Réglage *Paramètres en ligne › Compte et confidentialité ›
  « Mes amis peuvent me regarder jouer »*, **activé par défaut** ; décoché, personne ne peut
  regarder et la partie en cours est oubliée aussitôt. Seuls les **amis acceptés** le
  peuvent, jamais un joueur bloqué (dans un sens ou dans l'autre), et jamais quand vous
  masquez votre présence (« Montrer ma présence à mes amis ») : sinon « Regarder » trahirait
  que vous êtes en ligne. Ce qui est envoyé : le niveau joué (campagne, communauté ou défi)
  et sa version, la graine de la partie et les commandes de jeu de chaque pas de simulation
  (ce qu'un replay contient déjà), rien d'autre ; le spectateur, lui, n'envoie rien à votre
  jeu. Seuls les niveaux de la campagne, de la communauté installés et les défis sont
  diffusés, jamais un test de l'éditeur, un entraînement ou un niveau de l'atelier.
* **Transparence et consentement** : écran d'accueil de Cosmic Online, conditions
  d'utilisation (`data/online/terms_fr.txt`, version `2026-11`) acceptées explicitement à
  l'inscription.
* **Sécurité** : mots de passe hachés (PBKDF2-SHA-256, sel aléatoire, 100 000 itérations),
  jetons stockés sous forme d'empreinte, IP uniquement sous forme d'empreinte salée, buckets R2
  privés, limitation de débit, journal des opérations sensibles.

## Données stockées

| Donnée | Où | Visible par | Conservation |
|---|---|---|---|
| Pseudonyme, avatar, date d'inscription | D1 `players` | tous (profil masqué : pseudonyme et avatar seulement) | vie du compte |
| Pays / région, réglages de visibilité | D1 `players` | selon les réglages | vie du compte |
| Présence (2.32.3) : dernier état « actif » ou « inactif » envoyé par le jeu et son heure (aucun historique) | D1 `players` | les amis seulement (« en ligne », « absent », « hors ligne ») ; jamais si le joueur décoche « Montrer ma présence à mes amis » | remplacée à chaque envoi ; effacée quand le jeu se ferme |
| Empreinte du mot de passe | D1 `player_credentials` | personne | vie du compte |
| Sessions (empreintes, dates, client) | D1 `player_sessions` | le joueur (export) | 30 jours après expiration / révocation |
| Records (temps, catégorie, morts, dégâts, objets, version) | D1 `records` | records vérifiés : tous ; autres : le joueur | vie du compte |
| Replays (commandes de jeu pendant le niveau, trajectoire, événements) | R2 `replays/` + D1 `replay_metadata` | records vérifiés des profils publics | vie du compte |
| Avatar personnalisé (PNG contrôlé) | R2 `avatars/` | tous | jusqu'au changement d'avatar |
| Amis et demandes | D1 `player_friends` | les deux joueurs concernés | jusqu'au retrait |
| Messages (texte, date, auteur) et conversations | D1 `messages`, `conversations`, `conversation_members` | les membres de la conversation | vie du compte de l'auteur (le texte d'un message effacé par son auteur est supprimé) |
| Marque de lecture d'une conversation (date jusqu'à laquelle le joueur a lu ; donne les compteurs de messages non lus) | D1 `conversation_members.last_read_at` | le joueur seul (les autres membres ne voient pas si vous avez lu) | tant que le joueur est membre de la conversation |
| Joueurs bloqués | D1 `player_blocks` | le joueur qui bloque | jusqu'au déblocage |
| Niveaux publiés (package `.cplanet`, vignette, replay de fin de l'auteur, titre, description, note calculée) | R2 `community/` + D1 `community_levels`, `community_level_versions` | tous (niveaux publiés) | jusqu'au retrait par l'auteur ou la suppression du compte |
| Temps sur les niveaux de la communauté (et leurs replays) | D1 `community_records` + R2 | temps acceptés : tous | vie du compte |
| Notes et signalements de niveaux | D1 `community_ratings`, `community_reports` | notes : agrégées ; signalements : opérateurs | vie du compte |
| Signalements de messages | D1 `message_reports` + journal d'audit | opérateurs | tant que le message existe |
| Favoris (niveau, titre et auteur au moment de l'ajout) | D1 `community_favorites` | le joueur ; nombre agrégé sur le niveau | jusqu'au retrait |
| Commentaires et réponses (texte, dates, empreinte du texte contre le spam) | D1 `community_comments` | tous (sauf blocages et commentaires masqués) | jusqu'à leur suppression par l'auteur, la suppression du niveau ou du compte |
| Réactions aux commentaires | D1 `community_comment_reactions` | nombres agrégés ; la sienne pour le joueur | jusqu'au retrait |
| Signalements de commentaires | D1 `community_comment_reports` | opérateurs | tant que le commentaire existe |
| Notes des niveaux de la campagne (étoiles, dates ; seulement après un temps vérifié du niveau) | D1 `level_ratings` (+ moyennes `level_rating_stats`, recalculées par le serveur) | moyenne et nombre agrégés ; la sienne pour le joueur | jusqu'au retrait de la note ou la suppression du compte (moyennes alors recalculées) ; incluses dans l'export (`level_ratings`) |
| Commentaires des niveaux de la campagne (affichés dans la section En ligne seulement) et des courses des classements (texte, dates, empreinte du texte contre le spam, fil, réactions, signalements) | D1 `campaign_comments`, `campaign_comment_reactions`, `campaign_comment_reports` | tous (sauf blocages et commentaires masqués) ; les courses d'un profil masqué ne sont ni lisibles ni commentables par les autres | jusqu'à leur suppression par l'auteur ou la modération ; supprimés avec le compte de leur auteur, et le fil d'une course avec cette course (compte de son auteur supprimé) ; inclus dans l'export (`campaign_comments`, `campaign_comment_reactions`) |
| Joueurs d'un niveau (téléchargement ou temps envoyé) | D1 `community_level_players` | nombre agrégé (« joueurs uniques » du créateur) | tant que le niveau existe |
| Abonnements aux créateurs | D1 `player_follows` | listes publiques ou privées (réglage `follows_visibility`) | jusqu'au désabonnement ou à un blocage |
| Notifications (type, acteur, niveau, date, lu) et catégories désactivées | D1 `notifications`, `players.notifications_off` | le joueur | lues : 60 jours ; 500 au plus |
| Temps joué par niveau (secondes, date de la dernière partie) | D1 `player_playtime` | profils visibles | vie du compte |
| Succès débloqués (identifiants ; étoiles recomptées par le serveur), déclarés par le jeu | D1 `player_achievements` | profils visibles (masqués avec le profil) | vie du compte |
| Tenue portée en ligne (combinaison, traînée, cadre, titre, effet de victoire : identifiants du catalogue, vérifiés par le serveur avec les succès déclarés) | D1 `player_cosmetics` | profils visibles et classements (cadre, titre, combinaison ; masqués avec le profil) | vie du compte ; incluse dans l'export (`cosmetics`) |
| Badges du profil (2.41 : identifiants de succès choisis, dans leur ordre, vérifiés par le serveur avec les succès déclarés et leurs étoiles) | D1 `player_badges` | profils visibles (masqués avec le profil ; jamais dans les classements) | vie du compte ; inclus dans l'export (`badges`) |
| Projets collaboratifs (nom, description, réglages), membres et rôles | D1 `projects`, `project_members` | les membres (projets listés : nom, propriétaire, nombre de membres) | jusqu'à la suppression du projet ou du compte du propriétaire |
| Révisions des projets (niveau JSON, auteur, message, empreinte) | R2 `projects/` + D1 `project_revisions` | les membres | jusqu'à la suppression du projet |
| Invitations (empreinte SHA-256 de la clé, 4 derniers caractères, rôle, limites) et demandes d'adhésion (message) | D1 `project_invites`, `project_join_requests` | administrateurs du projet | jusqu'à la suppression du projet |
| Journal d'un projet (qui a fait quoi, quand) | D1 `project_activity` | les membres | jusqu'à la suppression du projet |
| Opérations de l'édition en direct (modification, auteur, date) | D1 `project_ops` | les membres | jusqu'à la suppression du projet |
| Versions nommées des projets (nom, notes, révision, empreinte, auteur) | D1 `project_versions` | les membres | jusqu'à la suppression du projet (une version ne se modifie pas ; l'auteur devient anonyme si son compte est supprimé) |
| Discussion des projets (message, réponse à, date, auteur) | D1 `project_messages` | les membres du projet | jusqu'à la suppression du projet ; supprimés avec le compte de leur auteur ; inclus dans l'export de vos données (avec vos projets et vos rôles) |
| Tentatives de la course à la vérification (niveaux EXTRÊME : score, parcours, boss, vagues, temps, date) et leurs replays | D1 `race_runs`, `race_victors` + R2 `races/` | classement public (pseudonyme, score, détail, date) | supprimées avec votre compte (replays compris) ou avec le niveau de la communauté ; incluses dans l'export de vos données (`race_attempts`) ; le titre de vérificateur d'un compte supprimé devient anonyme |
| Parties des défis du jour, de la semaine et du mois (temps, morts, dégâts, palier, statut, date, envoi hors délai ou non) et leurs replays | D1 `challenge_runs` + R2 `challenges/` | classement du défi (pseudonyme, meilleur temps vérifié, palier, hors délai ; pays selon la confidentialité) ; replay d'un temps vérifié (pas pour un profil masqué) | supprimées avec votre compte (replays compris) ; incluses dans l'export de vos données (`challenge_runs`) ; un défi qui met votre niveau à l'honneur perd ce lien |
| Essais « Plus loin » des défis (2.34 : temps, boss et vagues déclarés, progression calculée par le serveur, statut, raisons d'un refus, envoi hors délai ou non) et le replay du meilleur essai par défi | D1 `challenge_attempts` + R2 `challenges/` | classement « Plus loin » du défi (pseudonyme, meilleure progression vérifiée, temps, hors délai ; pays selon la confidentialité) ; replay du meilleur essai vérifié, regardable depuis ce classement (pas pour un profil masqué) | supprimés avec votre compte (replay compris) ; inclus dans l'export de vos données (`challenge_attempts`) ; un essai moins bon que votre meilleur ne garde pas son replay |
| Présence en direct (curseur et vue dans le niveau, sélection, activité) | mémoire de la salle du projet, jamais écrite | les membres présents | 20 secondes après le dernier signe de vie |
| Réglage « Mes amis peuvent me regarder jouer » (2.36, activé par défaut) | D1 `players.spectate_allowed` | le joueur (profil privé, export) | vie du compte |
| Partie en cours regardable par les amis (2.36) : niveau et version (titre fourni par le serveur), graine, commandes de jeu de chaque pas de simulation, empreintes d'état toutes les 0,5 s, heure de début | mémoire de la salle du joueur (Durable Object `SpectateRoom`), **jamais écrite** (ni D1, ni R2, ni stockage de l'objet) ; rien sur ceux qui regardent | les amis acceptés, si le réglage est coché et la présence montrée ; jamais un joueur bloqué | la partie en cours seulement (30 minutes de jeu au plus) ; oubliée 1 minute après sa fin, 2 minutes après le dernier envoi du jeu, dès qu'une nouvelle partie commence, que le réglage est décoché, la présence masquée ou le compte supprimé |
| Journal de sécurité (action, empreinte d'IP) | D1 `audit_log` | opérateurs | 180 jours |
| Compteurs de limitation (empreintes) | D1 `rate_limits` | personne | 24 heures |
| Copies de sauvegarde de toutes les données ci-dessus, sauf les sessions et les compteurs (voir [BACKUP.md](BACKUP.md)) | R2 privé `backups/` ; fichier `.cpbk` chiffré quand un admin le télécharge | comptes admin | automatiques : 14 jours ; avant un import : 30 jours ; manuelles : jusqu'à ce que 10 plus récentes existent |

## Droits du joueur (Paramètres en ligne)

* **Accès / portabilité** : *Exporter mes données* → `GET /api/v1/players/me/export`, enregistré
  en JSON dans le dossier `exports/` du joueur (dont les conversations, les messages envoyés,
  les joueurs bloqués, les favoris, commentaires, réactions, abonnements, notifications, le
  temps joué et, depuis 2.19, les notes des niveaux de la campagne et les commentaires des
  niveaux officiels et des courses).
* **Rectification** : pseudonyme, avatar, pays, visibilités (`PATCH /api/v1/players/me`).
* **Opposition / limitation** : masquer le profil, le pays, la présence sur le globe, refuser
  les demandes d'amis, empêcher ses amis de regarder ses parties en direct, rendre privées ses listes d'abonnés et d'abonnements, désactiver des
  catégories de notifications, désactiver Cosmic Online ou la connexion automatique.
* **Effacement** : *Supprimer mon compte* (mot de passe + confirmation) →
  `DELETE /api/v1/players/me` : suppression des fichiers R2 (replays, avatar) puis du compte et,
  en cascade, des sessions, records, meilleurs temps, replays, amis, messages envoyés,
  appartenances aux groupes, blocages, niveaux publiés (packages, vignettes, replays des
  joueurs sur ces niveaux), temps, notes et signalements, notes des niveaux de la campagne
  (moyennes recalculées sans elles), commentaires et réactions de la campagne (compteurs
  recalculés ; les fils des courses du joueur partent avec elles), et du journal d'audit
  rattaché ; une seule trace anonyme « compte supprimé » est conservée. Les records locaux
  restent sur l'ordinateur du joueur.
* **Déconnexion de tous les appareils** : révocation de toutes les sessions.

## Vérification des mises à jour

Au démarrage (Cosmic Online activé) et à l'ouverture des Notifications, le jeu demande
`GET /client/latest` : aucune donnée de compte, aucun identifiant ni numéro de version n'est
envoyé (le serveur voit seulement l'adresse IP de la requête, comme pour toute requête, et n'en
garde qu'une empreinte salée pour la limitation de débit). Le téléchargement de l'installateur
n'a lieu que sur demande du joueur, et le fichier n'est proposé à l'exécution qu'après
vérification de son empreinte SHA-256.

## Côté jeu (ordinateur du joueur)

| Fichier (dossier utilisateur) | Contenu |
|---|---|
| `online/session.json` | jeton de renouvellement (DPAPI sous Windows, fichier `0600` sous Linux), seulement avec la connexion automatique |
| `online/cache/` | dernières réponses du serveur (classements, globe, profils) ; *Effacer le cache en ligne* |
| `saves/online_attempts.json`, `online/replays/` | performances en attente ou suivies et leurs replays |
| `online/known_ranks.json` | derniers rangs vus (notifications « record battu ») |
| `online/level_ratings.json` | dernières moyennes reçues des notes des niveaux de la campagne (affichées « reçues il y a … » hors ligne) et vos notes ; lues seulement quand vous êtes connecté |
| `online/playtime.json` | temps joué pas encore envoyé (seulement pour le compte connecté) |
| `online/projects.json` | liens entre les copies locales (ATELIER → Mes niveaux) et les projets, révision de départ |
| `online/live_<projet>.json` | modifications faites en direct pas encore confirmées par le serveur (supprimé une fois vide) |
| `saves/progress.json` | temps joué par niveau (affiché sur la fiche de chaque niveau, même sans compte) |
| `saves/boss_stats.json` | statistiques des boss (victoires, défaites, combats et phases sans dégât) : jamais envoyées au serveur |
| `saves/achievements.json` | succès débloqués et compteurs de partie (sauts, ennemis vaincus…) : seule la liste des succès est envoyée, au compte connecté |
| `saves/segments.json` | temps de passage de vos dernières courses et meilleurs segments : jamais envoyés |
| `ghosts/` | fantômes : vos courses (`local/`), replays téléchargés (`cache/`, limité en taille), replays importés (`files/`) : jamais envoyés |

À la suppression du compte, les performances et replays locaux rattachés à ce compte sont
également effacés.
