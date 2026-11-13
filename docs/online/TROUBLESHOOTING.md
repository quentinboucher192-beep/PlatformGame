# Dépannage de Cosmic Online

Le journal du jeu (`cosmic.log` dans le dossier utilisateur) indique au démarrage le serveur
utilisé, l'origine de l'adresse et le client HTTP :
`Cosmic Online : serveur https://... (config.json (production)), client HTTP WinHTTP`.
*Paramètres en ligne → Préférences* affiche les mêmes informations.

## Le jeu

| Symptôme | Cause / solution |
|---|---|
| « Serveur non configuré » | `data/online/config.json` n'a pas d'adresse pour l'environnement choisi. Déployez le serveur ([DEPLOYMENT.md](DEPLOYMENT.md)) et renseignez l'adresse, ou lancez avec `--online-server`. Une adresse `http://` autre que la machine locale est refusée volontairement. |
| « Réseau indisponible » (Linux) | le jeu a été compilé sans libcurl : installez `libcurl4-openssl-dev` et reconfigurez CMake (un avertissement l'indique). |
| « Hors ligne » | dernier appel en échec réseau (DNS, connexion, délai, TLS). Le jeu réessaie seul (15 s, puis jusqu'à 5 min) ; les classements affichés viennent du cache avec leur âge. Vérifiez la connexion, un pare-feu ou un proxy (WinHTTP utilise le proxy système ; libcurl les variables `HTTPS_PROXY` / `NO_PROXY`). |
| « Réponse du serveur illisible » | un portail captif ou un proxy renvoie autre chose que l'API : ce n'est jamais traité comme un succès. Connectez-vous au réseau puis *Actualiser*. |
| Déconnecté tout seul | session expirée (30 jours sans activité), révoquée (déconnexion de tous les appareils) ou jeton de renouvellement réutilisé (sécurité) : reconnectez-vous. |
| « Trop d'échecs de connexion » | 10 mauvais mots de passe : compte verrouillé 15 minutes. |
| Record « LOCAL » qui ne part pas | pas connecté (*SUBMIT RECORD* l'attribue au compte connecté), synchronisation automatique désactivée, ou serveur injoignable. |
| « Version du niveau différente de celle du serveur » | votre niveau n'a pas la même empreinte que la révision classée (jeu modifié, version différente) : la performance reste locale, elle n'est jamais mélangée. |
| Record « EN ATTENTE » | replay pas encore envoyé, catégorie SPEEDRUN sans replay, ou record mondial en revue manuelle sur un classement établi ([RULES.md](RULES.md)). La revue manuelle demande un `ADMIN_TOKEN` sur le serveur (routes `/api/v1/admin/*`) : sans lui, ces records restent en attente. |
| Pas de notification de demande d'ami | les demandes sont relevées une minute après la connexion puis toutes les minutes ; pendant une partie, les notifications attendent la fin du niveau. |
| Record « REFUSÉ » | temps impossible, au-delà de la limite, ou replay incohérent : la raison s'affiche dans la notification et sur la fiche du record. |
| Pas de marqueur à mon nom sur le globe | visibilité réglée sur *Masqué* ou *Classements* : choisissez *Classements et globe* (avec un pays). Seuls les records vérifiés apparaissent. |
| VIEW REPLAY grisé | aucun record du classement affiché n'a de replay vérifié, ou le niveau n'est pas installé. |

## Le serveur

| Symptôme | Cause / solution |
|---|---|
| `wrangler dev` : « Incorrect type for map entry » | le module principal ne doit exporter que le gestionnaire par défaut. |
| Routes `/api/v1/admin/*` en 404 | `ADMIN_TOKEN` absent ou trop court (< 32 caractères) ; en local, vérifiez `.dev.vars`. |
| 429 `RATE_LIMITED` en local | essais répétés depuis 127.0.0.1 : `RATE_LIMIT_SCALE` (dev) multiplie les limites ; sinon attendez la fenêtre indiquée par `Retry-After`. |
| 409 `LEVEL_VERSION_MISMATCH` pour tous les records | les niveaux de cette version du jeu ne sont pas enregistrés : `--online-manifest` puis `scripts/register-levels.mjs`. |
| 409 `REVISION_CONFLICT` à l'enregistrement | un niveau a changé sans changer `metadata.revision` : incrémentez-la. |
| Erreur 1102 / dépassement CPU à l'inscription (offre gratuite) | PBKDF2 : passez à l'offre Workers Paid ou réduisez `PBKDF2_ITERATIONS` ([DEPLOYMENT.md](DEPLOYMENT.md)). |
| Tables absentes (`no such table`) | migrations non appliquées sur cet environnement : `npm run db:migrate:staging` / `:production`. |
| `npm install` échoue (« edgesOut ») | utilisez `npm ci` (le fichier `.npmrc` active `legacy-peer-deps`, nécessaire pour l'intégration Vitest). |

Les erreurs internes ne renvoient au client qu'un message générique (`INTERNAL_ERROR`) avec un
`request_id` ; le détail est dans les journaux du Worker (`npx wrangler tail`), retrouvable
avec ce `request_id`.
