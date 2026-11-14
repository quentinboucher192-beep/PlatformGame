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
| Empreinte du mot de passe | D1 `player_credentials` | personne | vie du compte |
| Sessions (empreintes, dates, client) | D1 `player_sessions` | le joueur (export) | 30 jours après expiration / révocation |
| Records (temps, catégorie, morts, dégâts, objets, version) | D1 `records` | records vérifiés : tous ; autres : le joueur | vie du compte |
| Replays (commandes de jeu pendant le niveau, trajectoire, événements) | R2 `replays/` + D1 `replay_metadata` | records vérifiés des profils publics | vie du compte |
| Avatar personnalisé (PNG contrôlé) | R2 `avatars/` | tous | jusqu'au changement d'avatar |
| Amis et demandes | D1 `player_friends` | les deux joueurs concernés | jusqu'au retrait |
| Messages (texte, date, auteur) et conversations | D1 `messages`, `conversations`, `conversation_members` | les membres de la conversation | vie du compte de l'auteur (le texte d'un message effacé par son auteur est supprimé) |
| Joueurs bloqués | D1 `player_blocks` | le joueur qui bloque | jusqu'au déblocage |
| Niveaux publiés (package `.cplanet`, vignette, replay de fin de l'auteur, titre, description, note calculée) | R2 `community/` + D1 `community_levels`, `community_level_versions` | tous (niveaux publiés) | jusqu'au retrait par l'auteur ou la suppression du compte |
| Temps sur les niveaux de la communauté (et leurs replays) | D1 `community_records` + R2 | temps acceptés : tous | vie du compte |
| Notes et signalements de niveaux | D1 `community_ratings`, `community_reports` | notes : agrégées ; signalements : opérateurs | vie du compte |
| Signalements de messages | D1 `message_reports` + journal d'audit | opérateurs | tant que le message existe |
| Journal de sécurité (action, empreinte d'IP) | D1 `audit_log` | opérateurs | 180 jours |
| Compteurs de limitation (empreintes) | D1 `rate_limits` | personne | 24 heures |

## Droits du joueur (Paramètres en ligne)

* **Accès / portabilité** : *Exporter mes données* → `GET /api/v1/players/me/export`, enregistré
  en JSON dans le dossier `exports/` du joueur (dont les conversations, les messages envoyés et
  les joueurs bloqués).
* **Rectification** : pseudonyme, avatar, pays, visibilités (`PATCH /api/v1/players/me`).
* **Opposition / limitation** : masquer le profil, le pays, la présence sur le globe, refuser
  les demandes d'amis, désactiver Cosmic Online ou la connexion automatique.
* **Effacement** : *Supprimer mon compte* (mot de passe + confirmation) →
  `DELETE /api/v1/players/me` : suppression des fichiers R2 (replays, avatar) puis du compte et,
  en cascade, des sessions, records, meilleurs temps, replays, amis, messages envoyés,
  appartenances aux groupes, blocages, niveaux publiés (packages, vignettes, replays des
  joueurs sur ces niveaux), temps, notes et signalements, et du journal d'audit rattaché ; une seule trace anonyme « compte supprimé » est conservée. Les records locaux
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

À la suppression du compte, les performances et replays locaux rattachés à ce compte sont
également effacés.
