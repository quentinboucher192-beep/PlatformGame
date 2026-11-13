# Format des replays (CPRP v2 / CPRZ)

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
| format_version | u16 | 2 (1 : trajectoire seule, anciens replays et enregistrements sans entrées) |
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

En version 1, le fichier se termine exactement après le dernier événement. En version 2, la
section des entrées suit (voir plus bas) et le fichier se termine exactement après elle.

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

Les éclats et reliques (arg 0 et 1) forment le nombre d'objets comparé au record.

## Section des entrées (version 2)

| Champ | Type | Notes |
|---|---|---|
| seed | u64 | graine du gameplay (identique pour tous les joueurs d'un niveau) |
| tick_hz | u16 | 120 |
| réservé | u16 | 0 |
| tick_count | u32 | nombre de pas simulés depuis le chargement du niveau (intro comprise), ≤ 864 000 (2 h) |
| run_count | u32 | nombre de plages, 1 à tick_count |
| runs | run_count × 12 octets | entrées compressées par plages (voir ci-dessous) |
| check_count | u32 | ≤ tick_count / 60 + 2 |
| checks | check_count × 12 octets | empreintes de l'état |

### Plage d'entrées (12 octets)

| Champ | Type | Notes |
|---|---|---|
| repeat | u16 | nombre de pas identiques consécutifs (≥ 1) ; la somme vaut tick_count |
| held | u16 | actions maintenues (bit = `Action` : 0 gauche, 1 droite, 2 haut, 3 bas, 4 saut, 5 attaque, 6 dash, 7 onde, 8 bouclier, 9 impulsion, 10 téléportation, 11 interagir) |
| pressed | u16 | actions enfoncées pendant ce pas |
| released | u16 | actions relâchées pendant ce pas |
| move_x, move_y | i8, i8 | stick analogique quantifié (−127 à 127, soit ±1) |
| flags | u8 | 1 : saut de la cinématique en cours |
| réservé | u8 | 0 |

En jeu, la simulation ne reçoit **que** cette forme quantifiée : le rejeu lui donne donc
exactement les mêmes entrées.

### Empreinte (12 octets)

| Champ | Type | Notes |
|---|---|---|
| tick | u32 | pas après lequel l'empreinte est prise (strictement croissant) |
| hash | u64 | FNV-1a 64 bits de l'état (`simulationStateHash`) |

Une empreinte tous les 60 pas (0,5 s), plus une au dernier pas. L'état couvre l'état du monde,
le temps, le chronomètre, le générateur aléatoire du gameplay, le joueur (boîte, vitesse, vie,
état, morts), chaque ennemi (boîte, vitesse, vie, état), chaque projectile et chaque entité
(boîte, actif, retiré).

## Rejeu

`ReplaySimulator` charge le niveau, applique la graine puis les entrées pas à pas
(`stepWorld`, la même fonction qu'en jeu) et compare chaque empreinte. Le lecteur
(*VIEW REPLAY*, *Revoir la partie* sur l'écran de résultats, `CosmicPlatformer --replay
fichier.cprz`) affiche « Partie rejouée à l'identique » avec le nombre de contrôles vérifiés.
Une empreinte différente signifie que la simulation locale n'est pas celle qui a enregistré
la partie : le lecteur l'indique (« Désynchronisé à … ») et propose la trajectoire enregistrée.
Le lecteur refuse d'emblée un replay dont l'empreinte de niveau (niveau, ennemis, pouvoirs,
configuration du joueur, version de la simulation) ne correspond pas au jeu installé.

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
révision installée du niveau. Le serveur analyse la trajectoire des deux versions et vérifie
en plus, en version 2, la cohérence de la section des entrées (nombre de pas compatible avec
le temps, empreintes présentes). Il conserve les entrées pour une future re-simulation côté
serveur (pas encore faite : un replay v2 n'est donc pas encore « prouvé » par le serveur).
