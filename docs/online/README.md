# Cosmic Online — classements mondiaux, records vérifiés et globe interactif

Cosmic Online est la partie en ligne, **facultative**, de Cosmic Platformer : comptes,
envoi et vérification des records, classements (niveau, pays, amis, semaine, planète,
points cosmiques), globe terrestre interactif, profils publics, amis et replays.
Le jeu reste entièrement jouable sans compte et sans Internet.

| Document | Contenu |
|---|---|
| [INSTALL.md](INSTALL.md) | Environnement local complet (serveur + jeu) sans compte Cloudflare |
| [DEPLOYMENT.md](DEPLOYMENT.md) | Déploiement Cloudflare : staging et production |
| [SECRETS.md](SECRETS.md) | Secrets : lesquels, où, comment les changer |
| [API.md](API.md) | Référence de l'API REST (et [`server/openapi.yaml`](../../server/openapi.yaml)) |
| [RULES.md](RULES.md) | Catégories, validation des records, statuts, niveaux de confiance, limites de l'anti-triche |
| [REPLAY_FORMAT.md](REPLAY_FORMAT.md) | Format binaire des replays (CPRP / CPRZ) et déterminisme du moteur |
| [PRIVACY.md](PRIVACY.md) | Données personnelles, localisation, RGPD |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | Dépannage |

## Architecture

```
 Jeu (C++20 / SDL3)                                    Cloudflare
┌──────────────────────────────────────────┐         ┌───────────────────────────────┐
│ Écrans : ONLINE RANKING, compte,          │         │ Worker TypeScript (server/)   │
│ paramètres en ligne, replay, résultats    │         │  /api/v1/... REST + JSON      │
│        │ callbacks (thread principal)      │  HTTPS  │  auth, records, classements,  │
│ OnlineService ── OnlineClient ── thread ──┼────────►│  globe, amis, RGPD, modération │
│  session · cache · file d'attente         │         │      │               │         │
│  replays · empreintes de niveaux          │         │   D1 (SQL)       R2 (privé)    │
│ Transport : WinHTTP (Windows) / libcurl   │         │   joueurs,       replays,      │
└──────────────────────────────────────────┘         │   records...     avatars       │
                                                      └───────────────────────────────┘
```

* **Serveur** (`server/`, indépendant du C++) : Worker TypeScript sans dépendance d'exécution,
  base **D1** (migrations versionnées dans `server/migrations/`), stockage **R2** privé pour les
  replays et avatars (servis uniquement à travers l'API, après contrôle d'accès), configuration
  `wrangler.jsonc` avec trois environnements (`dev`, `staging`, production).
* **Client** (`game/online/`) : `OnlineService` (point d'entrée), `OnlineClient` (thread réseau
  dédié, file de requêtes, annulation), transports `HttpTransportWinHttp.cpp` /
  `HttpTransportCurl.cpp` (rien d'autre à installer sous Windows ; `libcurl4-openssl-dev` sous
  Linux), `OnlineSession` (jetons, configuration des serveurs), `OnlineCache` (dernières
  réponses sur disque), `OnlineRecord` (file locale des performances), `Replay` (format et
  enregistreur), `LevelFingerprint` (empreinte des niveaux), `OnlineGlobe` (logique du globe).
* **Interface** (`ui/screens/OnlineRankingScene.cpp`, `OnlineAccountScene.cpp`,
  `OnlineSettingsScene.cpp`, `ReplayScene.cpp`, `ui/online/`) : rendu du globe avec
  `SDL_RenderGeometry`, aucun moteur 3D ajouté.

Le thread principal n'attend jamais le réseau : chaque appel retourne immédiatement et le
résultat arrive par un callback exécuté au début de l'image suivante. Les écrans qui
disparaissent avant la réponse ne sont jamais rappelés (`OnlineService::guard`).

## Parcours d'un record

1. Pendant la partie, `ReplayRecorder` enregistre l'entrée de chaque pas de simulation (120 par
   seconde) avec une empreinte de l'état toutes les 0,5 s, ce qui permet de rejouer la partie
   exactement ; il échantillonne aussi la position, l'état et la vie du joueur (30 fois par
   seconde de chrono) et note les événements (sauts, morts, objets, pouvoirs, points de
   contrôle). Seuls les niveaux officiels lancés depuis leur début sont concernés
   ([REPLAY_FORMAT.md](REPLAY_FORMAT.md)).
2. À la fin du niveau, `OnlineService::recordRun` garde la performance localement (statut
   **LOCAL**) si elle améliore le meilleur temps local dans au moins une catégorie, avec son
   replay compressé, sous un identifiant de tentative unique (UUID v4).
3. Connecté et synchronisation automatique active : la file est envoyée
   (`POST /records/submit`, une fois par catégorie), puis le replay (`PUT /records/{id}/replay`).
   Hors ligne, rien n'est perdu : envoi automatique au retour de la connexion (reconnexion
   avec temporisation exponentielle) ou bouton *Synchroniser* / *SUBMIT RECORD*.
4. Le serveur valide (schéma, niveau et révision exacts, version du jeu, règles de catégorie,
   cohérence, doublons, temps impossibles, analyse du replay, niveau de confiance) et répond
   **PENDING**, **VERIFIED** ou **REJECTED**. Seuls les records **VERIFIED** entrent dans les
   classements et sur le globe.
5. Notifications en jeu (record vérifié / refusé, record battu, synchronisation terminée),
   animation de progression du rang et halo sur le globe.

## Lancer et essayer

```
cd server && ./scripts/dev-setup.sh --levels      # serveur local + niveaux enregistrés
CosmicPlatformer --online-server http://127.0.0.1:8787
```

Détails : [INSTALL.md](INSTALL.md). Pour peupler un serveur **local** avec de vrais runs
(simulés par le moteur, vérifiés par le serveur) afin d'essayer le globe : voir la section
« Données d'essai » de [INSTALL.md](INSTALL.md).

## État de la fonctionnalité

Terminé et testé :

* API complète (santé, méta, authentification, joueurs, classements, records, replays, globe,
  amis, RGPD, modération, enregistrement des versions de niveaux), 55 tests serveur
  (Miniflare : D1 et R2 locaux) dont de nombreux cas négatifs ;
* client C++ (WinHTTP / libcurl), sessions avec renouvellement et révocation, cache hors ligne,
  file de synchronisation, replays, tests unitaires, tests sur sockets réelles (refus, délai,
  coupure, réponse illisible, annulation) et test d'intégration de bout en bout contre un
  vrai serveur (Linux / libcurl et Windows / WinHTTP sous Wine) ;
* écrans : ONLINE LEVELS (niveaux de la communauté : listes, recherche, fiche, téléchargement
  vérifié, notes, signalements, classement par niveau ; publication depuis le WORKSHOP avec le
  replay de fin de l'auteur), ONLINE RANKING (navigation, globe, classements avec onglets, filtres et recherche,
  niveau, carte du joueur), SOCIAL (conversations privées entre amis, groupes, amis, joueurs
  bloqués, signalements), compte et consentement, paramètres en ligne (confidentialité, amis,
  export et suppression des données), replays, statut en ligne sur l'écran de résultats,
  notifications (records, demandes d'amis, nouveaux messages).

Limites connues (non présentées comme faites) :

* déploiement : l'environnement **staging** est en ligne sur Cloudflare
  (`https://cosmic-online-staging.xpg-licences-workersdev.workers.dev`, D1 + R2, 9 niveaux
  enregistrés) et c'est le serveur utilisé par défaut (`data/online/config.json`,
  `environment: staging`). La **production** n'est pas encore déployée (voir
  [DEPLOYMENT.md](DEPLOYMENT.md)) ; le jeton d'administration a été retiré du staging après
  l'enregistrement des niveaux : définissez le vôtre (`wrangler secret put ADMIN_TOKEN --env
  staging`) pour modérer ou enregistrer de nouvelles révisions ;
* pas de récupération de mot de passe (les comptes n'ont pas d'adresse e-mail) ;
* la vérification des replays augmente fortement le coût de la triche mais ne la rend pas
  impossible (voir [RULES.md](RULES.md)) ; les records mondiaux suspects passent en revue
  manuelle via les routes d'administration ;
* pas d'outil graphique de modération (routes `/api/v1/admin/*` uniquement) ;
* les régions (ISO 3166-2) sont stockées et affichées sur le profil mais le globe utilise
  uniquement le centre du pays.
