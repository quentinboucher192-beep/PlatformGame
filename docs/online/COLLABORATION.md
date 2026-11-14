# Projets collaboratifs — guide

Un **projet** est un niveau construit à plusieurs dans Cosmic Online (ONLINE → PROJETS). Ce
guide décrit ce que voit le joueur, ce que garantit le serveur, et comment tout cela est testé.
Référence des routes : [API.md](API.md) §8.9 quater à sexies. Données conservées :
[PRIVACY.md](PRIVACY.md).

Tout est facultatif : on crée et on joue sans compte et sans Internet ; un projet ne sert qu'à
partager un niveau avec d'autres joueurs.

## 1. Rôles

| Rôle | Peut |
|---|---|
| Lecteur | voir le projet, ses membres, son activité, son historique ; télécharger une copie ; regarder l'édition en direct |
| Testeur | en plus : jouer la dernière version |
| Éditeur | en plus : envoyer des révisions, éditer en direct, restaurer, nommer des versions |
| Administrateur | en plus : inviter, accepter les demandes, changer les rôles (sauf administrateurs), réglages |
| Propriétaire | en plus : nommer ou retirer les administrateurs, transférer, supprimer |

Le serveur vérifie le rôle à **chaque** requête (session authentifiée, jamais un identifiant de
joueur fourni par le client). Un joueur extérieur au projet reçoit `404` : il n'apprend même
pas que le projet existe. Les boutons du jeu se grisent selon le rôle, mais ce n'est qu'un
confort : le refus vient du serveur.

## 2. Entrer dans un projet

* **Visibilité** : privé, sur invitation, public avec demande, ouvert (rôle d'arrivée choisi).
* **Clés d'invitation** `CPJ-…` et liens `cosmic://rejoindre/<clé>` : 128 bits aléatoires,
  montrées une seule fois, stockées en SHA-256 ; nombre d'utilisations, durée, révocation. Une
  clé épuisée, expirée, révoquée ou inconnue reçoit la même réponse.
* **Invitation directe** d'un joueur (notifiée), **demandes** d'adhésion avec message.

## 3. Deux façons de travailler

**Copie de travail.** « Ouvrir dans l'éditeur » télécharge la dernière révision comme niveau
local, lié au projet. « Envoyer » crée une révision. Une copie faite d'après une révision
dépassée est refusée (`409 REVISION_CONFLICT`) : rien n'est écrasé, le jeu propose de récupérer
la dernière version en gardant ses modifications à part.

**Édition en direct.** Tous les membres présents dans le même niveau :

* barre de présence (avatars, couleur, état de synchronisation), curseurs et sélections des
  autres avec leur nom, « Suivre » un membre (sa vue) ;
* notifications regroupées en bas de la vue : arrivées, départs, et une ligne par membre pour
  ses modifications des dernières secondes (« Astrid : 6 modifications · place des éclats ») ;
* un objet sélectionné par un éditeur est **verrouillé** pour les autres tant qu'il le tient ;
* chaque modification devient une opération (`add`, `set`, `del`, `doc`) que la salle du projet
  numérote dans un seul ordre pour tous ;
* une opération faite d'après un état dépassé sur la même chose, sur un objet verrouillé ou
  supprimé, est **refusée** avec l'état actuel : le jeu montre le refus et propose de réappliquer
  sa modification ou de garder celle du projet. Rien n'est écrasé en silence ;
* « Annuler » ne défait que ses propres modifications (celles des autres sont appliquées à tout
  l'historique d'annulation) ;
* **hors ligne**, les opérations attendent sur le disque (`online/live_<projet>.json`) et partent
  au retour du réseau. Une réponse perdue est renvoyée avec le même identifiant : la salle la
  reconnaît et ne l'applique qu'une fois ;
* toutes les 200 opérations, et quand le dernier membre part, le document devient une révision ;
  si le dernier membre disparaît sans quitter (jeu fermé, réseau coupé), la salle le constate
  par une alarme (30 s environ) et écrit la révision elle-même, même si elle a été mise en veille
  entre-temps ;
* la salle traite les lots d'opérations un par un, chacun jusqu'à son écriture en base : l'ordre
  est le même pour tous, et rien n'est montré aux autres avant d'être enregistré.

La présence (curseur, sélection, activité) n'existe qu'en mémoire et disparaît 20 s après le
dernier signe de vie ; seules les opérations sont conservées.

## 4. Historique et versions

* **Révisions** : toutes, filtrées par auteur ou par mots du message, avec leur empreinte.
* **Comparer** deux révisions : objets ajoutés, supprimés, modifiés (et quels champs), réglages.
* **Restaurer** une révision en crée une **nouvelle** ; l'édition en direct en cours est d'abord
  enregistrée, puis tous les membres présents reçoivent le document restauré.
* **Versions nommées** (« Bêta 1 ») : définitives, nom unique, SHA-256 vérifié à la création et à
  chaque téléchargement ; les autres membres sont prévenus.
* **Télécharger une copie** d'une révision ou d'une version : elle arrive dans WORKSHOP → Mes
  niveaux comme niveau indépendant (la copie liée au projet n'est pas touchée).

## 5. Limites de charge

| Limite | Valeur |
|---|---|
| Opérations par requête | 50 |
| Opérations par membre | 300 par 10 s (au-delà : `429`, le jeu garde les opérations et les renvoie) |
| Présence | une réponse complète toutes les 200 ms au plus par membre (le jeu en envoie une toutes les 0,6 s) |
| Taille d'une opération | 64 Ko |
| Opérations gardées en mémoire par la salle | 1 000 (au-delà, un membre en retard rejoint la salle au lieu de tout relire) |
| Création de projets, invitations, adhésions, envois | 10, 30, 20, 120 par heure et par joueur |

## 6. Tests

* `server/test/projects.test.ts`, `live.test.ts`, `load.test.ts` (Workers + D1 + R2 simulés par
  Miniflare) : rôles sur chaque route, clés d'invitation, conflits de révision, présence,
  verrous, ordre des opérations, idempotence, historique, versions, restauration pendant le
  direct ; **8 comptes éditant en même temps** (ordre sans trou ni doublon, rejeu identique,
  révision automatique fidèle) ; afflux d'opérations refusé en `429` ; envois et adhésions
  concurrents (un seul envoi gagne, une clé à 3 utilisations n'en accepte que 3).
* `tests/test_live.cpp` (jeu, contre une salle simulée) : différence et application des
  opérations, confirmations, opérations des autres, conflit, hors ligne puis reprise,
  réponse perdue puis renvoyée (appliquée une fois), `429` puis renvoi, resynchronisation après
  une restauration, arrivées, départs et modifications regroupées par membre.
* Coupures côté serveur (`load.test.ts`) : un membre qui disparaît sans quitter, une salle mise
  en veille puis réveillée par son alarme, un membre en retard qui reçoit chaque opération telle
  qu'elle a été faite.
* `server/scripts/load-test.mjs` : charge contre un serveur **local** (`wrangler dev`) ; refuse
  tout autre serveur.

```bash
node scripts/load-test.mjs 30 15 4
```

Résultats sur le serveur local (Miniflare, une machine de développement) :

| Membres | Durée | Modifications envoyées | Temps de réponse (médiane / p95 / max) | Erreurs | Cohérence |
|---|---|---|---|---|---|
| 12 | 20 s | 472 (2 par membre et par seconde) | 20 / 72 / 244 ms | 0 | rejeu identique au document ; dernière révision identique |
| 30 | 15 s | 1 557 (4 par membre et par seconde) | 108 / 214 / 310 ms | 0 | dernière révision identique au document (rejeu complet impossible au-delà de 1 000 opérations : resynchronisation prévue) |

Les refus de ces essais sont des conflits voulus : chaque membre modifie aussi un objet commun.
Ces mesures valent pour le serveur local ; elles ne remplacent pas une mesure sur le réseau de
Cloudflare.
