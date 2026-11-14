# Bilan — cahier des charges « COSMIC PLATFORMER — ONLINE 2.8 »

État au 14 novembre 2026, version **2.14.0**. Les six phases ont été traitées dans l'ordre
imposé ; chaque fonction listée ici est reliée au serveur, persistante quand elle doit l'être, et
testée. Ce qui n'est pas fait ou reste limité est dit en §4 et §5.

## 1. Livraisons

| Version | Contenu | Commit |
|---|---|---|
| 2.9.0 | Phase 1 — Workshop : favoris, commentaires, réactions, créateurs, abonnés, notifications ; temps joué (heures et minutes, par niveau et dans les profils) | `d17979f` |
| 2.10.0 | Phase 2 — projets collaboratifs : rôles, invitations, demandes, révisions | `804ce00` |
| 2.11.0 | Refonte de l'éditeur de niveaux selon la maquette approuvée (préalable à la phase 3) | `f29c2b3` |
| 2.12.0 | Phases 3 et 4 — édition en direct : présence, verrous, opérations synchronisées | `6d75b3c` |
| 2.13.0 | Phase 5 — historique, comparaison, restauration, versions nommées | `359e255` |
| 2.14.0 | Phase 6 — finitions : notifications regroupées, robustesse aux coupures et à la charge, tests multi-comptes et de charge, documentation, ce bilan | `5995dec`, `eb3380f` |

Chaque version a son installateur Windows (artefact de la CI) ; les versions 2.9.0 à 2.12.0
et 2.14.0 sont aussi proposées dans le jeu par le serveur de staging (notification « nouvelle version »,
téléchargement dans le jeu, lancement par le joueur ; aucune mise à jour automatique).

## 2. Fonctionnalités

**Phase 1 — Workshop.** Favoris (onglet trié et filtré ; niveaux retirés gardés comme
indisponibles) ; commentaires avec réponses, cinq réactions, épinglage par le créateur,
modification, suppression, signalement et modération ; profils de créateur (badge,
statistiques, créations), abonnés et abonnements, blocage ; recherche de créateurs ;
notifications en ligne (nouveaux niveaux suivis, réponses, réactions, abonnés…) avec filtres,
lu / non lu, suppression, catégories désactivables, **regroupement** des événements répétés tant
qu'ils ne sont pas lus ; temps joué envoyé au serveur (heures et minutes).

**Phase 2 — Projets.** Visibilités privé, sur invitation, public avec demande, ouvert ; rôles
propriétaire, administrateur, éditeur, testeur, lecteur, **vérifiés par le serveur à chaque
requête** (session authentifiée, jamais un identifiant fourni par le client) ; clés d'invitation
`CPJ-…` de 128 bits **stockées en SHA-256**, liens `cosmic://rejoindre/…`, nombre
d'utilisations compté atomiquement, expiration, révocation ; invitations directes, demandes avec
message ; transfert ; journal d'activité ; copie de travail locale, envoi = nouvelle révision,
copie dépassée refusée sans rien écraser.

**Phase 3 — Présence.** Salle par projet (Durable Object) ; barre de présence (avatars,
couleurs, état de synchronisation), curseurs, sélections, activité de chacun, « Suivre » un
membre, **verrous temporaires** (objets sélectionnés par un éditeur), **notifications
regroupées** dans la vue (arrivées, départs, une ligne par membre pour ses modifications des
dernières secondes) ; heartbeat toutes les 0,6 s ; présence uniquement en mémoire, séparée des
opérations persistantes.

**Phase 4 — Synchronisation.** Journal d'opérations persistant (`project_ops`), numérotées dans
un seul ordre, idempotentes par identifiant ; refus explicites (conflit, verrou, objet supprimé,
identifiant pris, invalide) montrés au joueur avec le choix de réappliquer ; **jamais d'écrasement
silencieux** ; file locale sur disque hors ligne ; révision écrite toutes les 200 opérations, au
départ du dernier membre, ou par l'alarme de la salle si le dernier membre disparaît sans
quitter ; un envoi classique ne peut pas cacher des opérations en direct (`409 LIVE_CHANGES`).

**Phase 5 — Historique.** Révisions filtrées par auteur et par texte ; comparaison de deux
révisions (objets ajoutés, supprimés, modifiés avec leurs champs ; réglages) ; restauration =
**nouvelle révision** (l'édition en direct en cours est d'abord enregistrée, puis tous les
membres présents reçoivent le document restauré) ; **versions nommées immuables** avec SHA-256
vérifié à la création et à chaque lecture ; téléchargement d'une révision ou d'une version comme
niveau indépendant.

**Phase 6 — Finitions.** Tests multi-comptes, coupures et charge (§3) ; limite de 300
opérations par membre et par 10 s (`429`, le jeu garde et renvoie) ; trois défauts trouvés par
ces tests et corrigés (§3) ; documentation ([COLLABORATION.md](COLLABORATION.md), API, RGPD).

## 3. Tests

| Suite | Nombre | Ce qui est vérifié |
|---|---|---|
| Serveur (`server/test`, Workers + D1 + R2 simulés) | 97 tests, 11 fichiers | dont `workshop` (6), `projects` (7), `live` (3), `load` (7) |
| Jeu (`cosmic_tests`) | 263 tests, 4 676 vérifications | dont `test_live` (6) contre une salle simulée, `test_online` |
| Captures 1600×900 | tous les écrans ajoutés ou modifiés | aucun chevauchement de texte |
| Charge (`server/scripts/load-test.mjs`, serveur local) | 12 et 30 membres | 0 erreur ; ordre, rejeu et enregistrement identiques |

Tests **multi-comptes** : 8 comptes éditant en même temps (ordre sans trou ni doublon, rejeu
identique au document, révision automatique fidèle) ; 5 envois concurrents (un seul gagne) ;
une clé à 3 utilisations essayée par 6 joueurs (3 entrent) ; deux versions du même nom (une seule) ;
matrice des rôles (inconnu `404`, sans session `401`, rôle insuffisant `403`) sur le direct,
l'historique et les versions.

Tests de **coupures** : réponse perdue puis renvoyée (appliquée une fois) ; hors ligne puis
reprise ; `429` puis renvoi ; membre disparu sans quitter ; salle mise en veille puis réveillée
par son alarme ; restauration pendant le direct (resynchronisation).

**Défauts trouvés par la phase 6 et corrigés** :

1. Une opération « ajouter » partageait son objet avec le document : un membre qui rattrapait
   son retard recevait l'objet déjà modifié par les opérations suivantes (état final juste,
   étapes fausses).
2. Après une mise en veille de la salle, l'alarme se réveillait sans savoir de quel projet il
   s'agissait : les modifications d'un membre parti sans quitter restaient enregistrées comme
   opérations, mais aucune révision n'était écrite (et un envoi classique restait refusé).
3. Deux lots d'opérations pouvaient s'entrelacer pendant l'écriture en base ; ils sont désormais
   traités un par un, et une écriture échouée fait recharger la salle depuis la base.

Mesures de charge (serveur local, voir [COLLABORATION.md](COLLABORATION.md) §6) : 12 membres,
médiane 20 ms, p95 72 ms ; 30 membres à 4 modifications par seconde chacun, médiane 108 ms,
p95 214 ms.

## 4. Limites connues

* **Interrogation, pas de WebSocket** : la présence et les opérations des autres arrivent par
  un heartbeat toutes les 0,6 s (délai perçu de 0,6 à 1 s ; une requête par membre et par
  0,6 s).
* **Conflits à l'échelle de l'objet** : deux membres qui modifient des champs différents du même
  objet à partir d'un état dépassé — le second est refusé (avec le choix de réappliquer). Pas de
  fusion champ par champ.
* **Verrous** : liés à la sélection d'un éditeur ; levés 20 s après sa disparition, sans durée
  maximale tant qu'il reste présent.
* La salle garde **1 000 opérations** en mémoire ; un membre plus en retard rejoint la salle
  (document complet) au lieu de tout relire.
* **Comparaison** : objets par identifiant ; événements, cinématiques et calques comparés en
  bloc (« événements » modifiés, sans détail) ; 200 entrées par liste au plus.
* **Restauration** du niveau entier seulement (pas d'un objet isolé). Les versions ne se
  suppriment pas (seulement avec leur projet) : c'est voulu.
* **Notifications en ligne** relevées toutes les 60 s (pas de push).
* **Mesures de charge** faites sur le serveur local (Miniflare) : elles ne remplacent pas une
  mesure sur le réseau de Cloudflare.

## 5. Ce qui n'est pas terminé

* **Production** : le serveur de production n'a pas été mis à jour (déploiement refusé ; à faire
  par vous) — Worker, migrations 0014 à 0023 et installateurs dans R2. Tout fonctionne sur le
  **staging**, que le jeu utilise par défaut.
* **Secret `ADMIN_TOKEN` du staging** non posé (écriture refusée) : les routes d'administration
  (`/admin/…` : modération des commentaires signalés, revue des records, enregistrement des
  niveaux officiels) ne sont pas utilisables sur le staging tant qu'il manque.
* **Mesure de charge sur Cloudflare** (staging) non faite : elle créerait des comptes de test sur
  un serveur partagé.
* Améliorations possibles, non promises : WebSocket (API d'hibernation des Durable Objects) à la
  place de l'interrogation ; fusion champ par champ ; détail des différences d'événements.

## 6. Migrations (D1)

| Migration | Contenu | Staging |
|---|---|---|
| 0014 | release 2.8.0 | appliquée |
| 0015 | Workshop : favoris, commentaires, réactions, signalements, abonnements, notifications, temps joué | appliquée |
| 0016 | release 2.9.0 | appliquée |
| 0017 | projets : `projects`, `project_members`, `project_invites`, `project_join_requests`, `project_revisions`, `project_activity` | appliquée |
| 0018, 0019 | releases 2.10.0, 2.11.0 | appliquées |
| 0020 | `project_ops`, colonnes `ops_seq` | appliquée |
| 0021 | `project_versions` | appliquée |
| 0022 | release 2.12.0 | appliquée |
| 0023 | release 2.14.0 | appliquée |

Toutes les migrations ne font qu'ajouter (tables, colonnes, lignes) : niveaux, classements,
replays et publications existants restent compatibles. Le Durable Object `ProjectRoom` est
déclaré dans `wrangler.jsonc` (migration de classe `v1`).

## 7. Sécurité et vie privée (rappel des engagements tenus)

Joueur identifié par sa session authentifiée, jamais par un identifiant fourni ; rôles vérifiés
par le serveur sur chaque route ; clés d'invitation imprévisibles stockées en hash, jamais
réaffichées ; mots de passe hachés (PBKDF2) ; fichiers R2 privés servis seulement après contrôle
d'accès ; aucun secret dans le dépôt ni dans le jeu ; localisation seulement par pays (région
facultative), masquable ; le jeu reste entièrement jouable sans compte et sans Internet ; aucune
donnée de démonstration sur le staging (elles ne vont que sur le serveur local de
développement).
