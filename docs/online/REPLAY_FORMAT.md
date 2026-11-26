[← Retour : Cosmic Online](README.md) · [Accueil (README)](../../README.md)

# Format des replays (CPRP v3 et v4 / CPRZ)

Depuis la version 2.1.2, un replay permet de **rejouer exactement** la partie : il contient la
graine du gameplay et l'**entrée de chaque pas de simulation** (120 par seconde), ce que le
jeu re-simule avec le même moteur (mêmes animations, mêmes ennemis, mêmes attaques, mêmes
blocs qui cassent). Une **empreinte de l'état** de la simulation toutes les 0,5 s permet de
détecter toute divergence. Le replay garde aussi la **trajectoire échantillonnée** de la
version 1 (positions à 30 Hz et événements), que le serveur analyse.

Ce rejeu exact repose sur un moteur déterministe (voir [Déterminisme](#déterminisme)).
Implémentations : `game/online/Replay.cpp` (écriture, lecture, enregistreur, `ReplaySimulator`)
et `server/src/replay.ts` (lecture, analyse).

## Conteneur CPRZ (ce qui est envoyé et stocké)

| Octets | Contenu |
|---|---|
| 0–3 | `"CPRZ"` |
| 4–7 | taille décompressée (u32, ≤ 4 Mio) |
| 8– | flux zlib (RFC 1950) du contenu CPRP |

## Contenu CPRP (little-endian)

| Champ | Type | Notes |
|---|---|---|
| magic | char[4] | `"CPRP"` |
| format_version | u16 | 4 pour une partie de défi du jour (jeu 2.32, voir « Règles d'un défi ») ; 3 pour toute autre partie depuis le jeu 2.24 (2 : jeu 2.1.2 à 2.23 ; 1 : trajectoire seule, anciens replays et enregistrements sans entrées) |
| sample_hz | u16 | 30 (10 à 120 acceptés) |
| frame_count | u32 | ≥ 1 |
| duration_ms | u32 | temps du chronomètre à la fin |
| event_count | u32 | |
| level_hash | u8[32] | SHA-256 brut de la révision |
| attempt_id | u8[16] | UUID v4 brut |
| level_id | u8 longueur + octets | ≤ 64 |
| game_version | u8 longueur + octets | ≤ 16 |
| frames | frame_count × 12 octets | voir ci-dessous |
| events | event_count × 8 octets | voir ci-dessous |

En version 1, le fichier se termine exactement après le dernier événement. En versions 2 et
3, la section des entrées suit (voir plus bas) et le fichier se termine exactement après elle ;
en version 4, la section des règles du défi suit encore la section des entrées.

### Échantillon (12 octets)

| Champ | Type | Notes |
|---|---|---|
| x, y | f32, f32 | position des pieds du joueur (pixels du monde) |
| state | u8 | `PlayerState` (0 Idle, 1 Run, 2 Jump, 3 Fall, 4 WallSlide, 5 Climb, 6 Slide, 7 Dash, 8 Swim, 9 Hurt, 10 Dead, 11 Respawn, 12 Victory, 13 Hidden) |
| flags | u8 | 1 regard à gauche, 2 au sol, 4 invulnérable, 8 planeur, 16 bouclier |
| health | u8 | points de vie |
| réservé | u8 | 0 |

Un échantillon est pris chaque fois que le chronomètre franchit `k / sample_hz` secondes ; le
dernier est pris à la fin du niveau.

### Événement (8 octets)

| Champ | Type |
|---|---|
| frame | u32 (index de l'échantillon suivant l'événement) |
| type | u8 |
| arg | u8 |
| value | u16 |

| Type | Nom | arg / value |
|---|---|---|
| 1 | saut | — |
| 2 | saut en l'air | — |
| 3 | mort | — |
| 4 | réapparition | — |
| 5 | objet ramassé | arg : 0 éclat, 1 relique, 2 soin, 3 énergie, 4 cœur, 5 secret |
| 6 | pouvoir | arg : 0 dash, 1 onde, 2 bouclier, 3 impulsion, 4 téléportation, 5 planeur, 6 frappe sismique, 7 tir chargé, 8 grappin, 9 triple saut |
| 7 | point de contrôle | — |
| 8 | dégâts | value : points perdus |
| 9 | ennemi vaincu | — |
| 10 | téléportation | — |
| 11 | fin du niveau | — (dernier échantillon) |
| 12 | boss touché (course à la vérification, 2.16) | arg : boss, value : vie retirée (pour mille) |
| 13 | vague d'embuscade (course à la vérification, 2.16) | arg : embuscade, value : vagues battues |
| 14 | temps de passage (Ghost Racing, 2.18) | arg : n° du point (255 au-delà), value : les 16 bits bas du temps en ms ; l'échantillon de l'événement donne le reste (le temps le plus proche ayant ces 16 bits) |
| 20 | version de la simulation (2.71) | value : `kSimulationVersion` du jeu qui a joué la partie (15 et plus). Toujours le **premier** événement, à l'échantillon 0, d'un replay exact (versions 2 à 5) ; le jeu le lit dans `ReplayData::simVersion` et ne le garde pas parmi les événements |

Un lecteur ignore les types qu'il ne connaît pas (le serveur ignore les types 14 et 20).

**Version de la simulation (2.71, simulation 15).** Un replay se rejoue avec les règles de la
version qui l'a enregistré (`ReplaySimulator` → `GameWorld::setSimulationVersion`) : un replay
exact sans événement 20 (enregistré avant la 2.71, simulation 14 ou avant) garde l'attaque
d'alors (direction, et rebond d'un coup vers le bas en l'air, lus au moment du coup) et se
rejoue donc à l'identique ; depuis la 2.71, l'attaque garde la visée tenue au début du coup (sa
direction, et le même rebond sur l'ennemi touché). Ni le format ni `input_flags` ne changent : le serveur et les jeux plus
anciens lisent ces replays comme avant.

**Intro regardée ou passée (2.92, simulation 19).** Les entrées commencent au chargement du niveau,
intro comprise : passer l'intro est le drapeau `SkipCinematic` (bit 0 des drapeaux d'une entrée) au
pas où le joueur l'a passée, une intro regardée n'en a aucun ; un replay rejoue donc la même intro de
la même façon. Depuis la simulation 19, un niveau qui s'ouvre sur son intro attend son chronomètre
(`GameWorld::startHeld`) : pendant l'intro, puis la matérialisation de Nova, rien du niveau ne bouge
(événements, objets, ennemis, dangers, tirs, temps de la simulation) ; il démarre au pas où le
chronomètre démarre, dans le même état pour tous, intro regardée ou passée. Un replay de la simulation
18 ou avant se rejoue avec ses règles (le niveau continuait pendant l'intro) : mêmes sommes de
contrôle, records et classements gardés (`kRankingGeneration` reste 8).

Les éclats et reliques (arg 0 et 1) forment le nombre d'objets comparé au record.

## Section des entrées (versions 2 et 3)

| Champ | Type | Notes |
|---|---|---|
| seed | u64 | graine du gameplay (identique pour tous les joueurs d'un niveau) |
| tick_hz | u16 | 120 |
| input_flags | u16 | version 3 : bit 0 = touches par emplacement de pouvoir (voir plus bas) ; tout autre bit refusé. Version 4 : bit 1 = règles de défi, obligatoire (voir « Règles d'un défi »). Version 2 : réservé, 0 |
| tick_count | u32 | nombre de pas simulés depuis le chargement du niveau (intro comprise), ≤ 864 000 (2 h) |
| run_count | u32 | nombre de plages, 1 à tick_count |
| runs | run_count × 18 octets (version 3) ou × 12 octets (version 2) | entrées compressées par plages (voir ci-dessous) |
| check_count | u32 | ≤ tick_count / 60 + 2 |
| checks | check_count × 12 octets | empreintes de l'état |

### Plage d'entrées (18 octets en version 3, 12 en version 2)

| Champ | Type | Notes |
|---|---|---|
| repeat | u16 | nombre de pas identiques consécutifs (≥ 1) ; la somme vaut tick_count |
| held | u32 (version 2 : u16) | actions maintenues (bit = `Action` : 0 gauche, 1 droite, 2 haut, 3 bas, 4 saut, 5 attaque, 6 dash, 7 onde, 8 bouclier, 9 impulsion, 10 téléportation, 11 interagir, 12 spécial — ralenti, bombe plasma — depuis la simulation 6 ; 13 à 20 : touches des emplacements de pouvoirs « Pouvoir 1 » à « Pouvoir 8 », version 3) |
| pressed | u32 (version 2 : u16) | actions enfoncées pendant ce pas |
| released | u32 (version 2 : u16) | actions relâchées pendant ce pas |
| move_x, move_y | i8, i8 | stick analogique quantifié (−127 à 127, soit ±1) |
| flags | u8 | 1 : saut de la cinématique en cours |
| choice | u8 | réponse à une offre de pouvoir (jeu 2.3) : 0 aucune, 1 à 8 remplace ce pouvoir porté, 255 laisse le nouveau pouvoir sur sa tablette (octet réservé, toujours 0, avant 2.3) |

### Touches par emplacement (version 3, simulation 10, jeu 2.24)

Depuis la 2.24, le joueur choisit une touche (et un bouton de manette) pour chaque
emplacement de pouvoir du HUD — *Paramètres › Clavier / souris* et *Manette*, « Pouvoir 1 »
à « Pouvoir 8 » — au lieu d'une touche par pouvoir fixée par le jeu. La touche d'un
emplacement utilise le pouvoir qui y est équipé, quel qu'il soit : le jeu enregistre la
**touche d'emplacement enfoncée** (bits 13 à 20) et la simulation, qui connaît l'ordre des
pouvoirs portés, en déduit le pouvoir. Deux pouvoirs de l'ancien bouton Spécial (Lame
boomerang et Nova de givre…) peuvent donc être portés ensemble, chacun sur son emplacement :
l'ancienne règle « même bouton, il faut remplacer » ne s'applique plus.

Les pouvoirs passifs (Aimant, Ruée aérienne) et ceux qui s'utilisent avec un mouvement du
personnage (Planeur, Triple saut, Frappe sismique, Tir chargé, Grappin) n'utilisent pas la
touche de leur emplacement ; ils ont leur cadre pointillé doré dans le HUD, le choix du
pouvoir à remplacer et le guide des pouvoirs.

Le bit 0 de `input_flags` dit quelles règles rejouer : 1 pour une partie jouée depuis la
2.24 (touches par emplacement), 0 pour un fichier de version 2 (un bouton par pouvoir, bits 6
à 10 et 12). `ReplaySimulator` donne ces règles au monde qu'il rejoue
(`GameWorld::setKeysPerSlot`) : un replay de version 2 se rejoue exactement comme avant,
avec les mêmes empreintes.

En jeu, la simulation ne reçoit **que** cette forme quantifiée : le rejeu lui donne donc
exactement les mêmes entrées.

Emplacements de pouvoirs (simulation 5) : quand le joueur touche une tablette alors que tous
ses emplacements sont pris (ou qu'un pouvoir porté utilise le même bouton), la simulation
s'arrête : les pas sans réponse (`choice` = 0) ne simulent rien et ne sont pas enregistrés en
jeu. Le premier pas suivant porte la réponse du joueur, puis la partie reprend avec les
entrées de ce pas. Un pouvoir remplacé reste sur la tablette.

### Empreinte (12 octets)

| Champ | Type | Notes |
|---|---|---|
| tick | u32 | pas après lequel l'empreinte est prise (strictement croissant) |
| hash | u64 | FNV-1a 64 bits de l'état (`simulationStateHash`) |

Une empreinte tous les 60 pas (0,5 s), plus une au dernier pas. L'état couvre l'état du monde,
le temps, le chronomètre, le générateur aléatoire du gameplay, le joueur (boîte, vitesse, vie,
état, morts), chaque ennemi (boîte, vitesse, vie, état), chaque projectile et chaque entité
(boîte, actif, retiré).

## Règles d'un défi (version 4, simulation 11, jeu 2.32)

Une partie d'un **défi du jour** (`docs/online/DAILY_CHALLENGES.md`) se joue avec les règles du
défi : cœurs, emplacements de pouvoirs, pouvoirs imposés, stations éteintes ou non, points de
passage éteints ou non. Son replay est écrit en **version 4** : la version 3, plus le bit 1
de `input_flags` (`2` : règles de défi) et une dernière section, après les empreintes :

| Champ | Type | Notes |
|---|---|---|
| rules_len | u16 | 1 à 1024 |
| rules | rules_len octets | forme canonique UTF-8 des règles, exactement (`ChallengeRules::canonical`) |

```
{"challenge":"daily-2026-11-16","hearts":2,"slots":3,"powers":["dash","glide","grapple"],"stations":true,"checkpoints":false}
```

Le fichier se termine exactement après elle. Règles de lecture (jeu et serveur) :

* version 4 ⇔ bit 1 de `input_flags` : une version 3 avec ce bit, ou une version 4 sans lui,
  est refusée ; aucun autre bit n'existe (bit 0 : touches par emplacement, toujours 1 en
  pratique) ;
* la section n'est acceptée que si elle est **exactement** la forme canonique de ce qu'elle
  contient (clés dans cet ordre, `powers` trié, sans espace, valeurs dans les bornes du
  contrat) ; le serveur vérifie en plus qu'elle est identique à celle du défi soumis ;
* seule une partie de défi est écrite en version 4 : toute autre partie reste en version 3
  (un serveur pas encore à jour continue de les accepter).

Au rejeu, `ReplaySimulator` donne ces règles au monde qu'il rejoue
(`GameWorld::setChallenge`), juste après le chargement du niveau, comme la partie l'a fait :
le lecteur (« Revoir mon meilleur replay », `--replay`) rejoue donc une partie de défi à
l'identique. Une partie sans défi se joue exactement comme avant (mêmes empreintes).

## Rejeu

`ReplaySimulator` charge le niveau, applique la graine puis les entrées pas à pas
(`stepWorld`, la même fonction qu'en jeu) et compare chaque empreinte. Le lecteur
(*Voir le replay*, *Revoir la partie* sur l'écran de résultats, `CosmicPlatformer --replay
fichier.cprz`) affiche « Partie rejouée à l'identique » avec le nombre de contrôles vérifiés.
Une empreinte différente signifie que la simulation locale n'est pas celle qui a enregistré
la partie : le lecteur l'indique (« Désynchronisé à … ») et propose la trajectoire enregistrée.
Le lecteur refuse d'emblée un replay dont l'empreinte de niveau (niveau, ennemis, pouvoirs,
configuration du joueur, version de la simulation) ne correspond pas au jeu installé.

Depuis la 2.19, le lecteur va à n'importe quel instant, en avant comme en arrière, image par
image (`ReplayPlayback`) : pour revenir en arrière, il repart d'un **point de reprise** (une
autre copie du niveau rejouée avec les mêmes entrées, gardée un peu derrière l'instant
montré) ou recharge le niveau et rejoue les entrées depuis le début. L'état montré est
toujours celui de la simulation à ce pas (aucune interpolation, aucune estimation). La
comparaison de deux replays (`docs/GHOST_RACING.md`) rejoue A ainsi et montre B en
fantôme.

## Déterminisme

* Pas fixe de 1/120 s ; tout ce qui modifie le gameplay se passe dans `fixedUpdate`. Les mises
  à jour à la fréquence de l'affichage (animations, particules, sons, HUD) ne touchent jamais
  la simulation (test : mêmes empreintes avec un affichage à 37, 60 ou 144 images/s).
* Deux générateurs aléatoires : `rng()` pour le gameplay (graine du niveau, utilisé seulement
  au pas fixe) et `fxRng()` pour le cosmétique.
* `sin`, `cos`, `atan2` et `exp` viennent de `engine/core/DetMath.h` (opérations IEEE de base
  en double) et non de la bibliothèque C, dont les arrondis diffèrent entre MSVC et GCC.
  Compilation sans *fast-math* ni contraction FMA (`-ffp-contract=off`, `/fp:precise`).
* Aucun état global : chaque partie a ses propres compteurs (identifiants d'ennemis, objets).
* Tris stables, aucun parcours de conteneur non ordonné qui influence le gameplay.
* Test `determinism: golden digests` : les compilations Linux (GCC), Windows (MinGW) et
  Windows (MSVC, CI) doivent produire exactement les mêmes empreintes après 60 s de jeu
  scripté sur un niveau de chaque planète.
* `kSimulationVersion` (`game/online/LevelFingerprint.h`) entre dans l'empreinte des niveaux
  classés : toute évolution du moteur qui change le déroulement d'une partie l'incrémente, ce
  qui crée de nouvelles révisions de niveaux ; records et replays de moteurs différents ne
  sont jamais mélangés.

## Compatibilité

`format_version` est vérifié par le jeu et par le serveur. Les replays v1 (jeu 2.1.0 et
2.1.1) restent lisibles : le lecteur affiche leur trajectoire, s'ils correspondent à la
révision installée du niveau. Les replays v2 (jeu 2.1.2 à 2.23) restent lisibles et se
rejouent à l'identique (un bouton par pouvoir). Le serveur analyse la trajectoire de toutes
les versions et vérifie en plus, en versions 2 et 3, la cohérence de la section des entrées
(nombre de pas compatible avec le temps, empreintes présentes, `input_flags` connus) : il
doit accepter la version 3 **avant** que les joueurs aient le jeu 2.24 (sinon leurs replays
seraient refusés). Il conserve les entrées pour une future re-simulation côté
serveur (pas encore faite : un replay v2 n'est donc pas encore « prouvé » par le serveur).

---

[← Retour : Cosmic Online](README.md) · [Accueil (README)](../../README.md)
