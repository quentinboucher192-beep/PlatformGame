# Format des replays (CPRP v1 / CPRZ)

Un replay décrit un run par des **états échantillonnés** et des **événements**, pas par une
vidéo : quelques dizaines de kilo-octets pour plusieurs minutes. Le moteur n'étant pas
déterministe d'un compilateur à l'autre (flottants), on enregistre les états nécessaires
plutôt que les entrées du joueur. Implémentations : `game/online/Replay.cpp` (écriture,
lecture, enregistreur) et `server/src/replay.ts` (lecture, analyse).

## Conteneur CPRZ (ce qui est envoyé et stocké)

| Octets | Contenu |
|---|---|
| 0–3 | `"CPRZ"` |
| 4–7 | taille décompressée (u32, ≤ 4 Mio) |
| 8– | flux zlib (RFC 1950) du contenu CPRP |

## Contenu CPRP v1 (little-endian)

| Champ | Type | Notes |
|---|---|---|
| magic | char[4] | `"CPRP"` |
| format_version | u16 | 1 |
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

Le fichier doit se terminer exactement après le dernier événement.

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

## Compatibilité

`format_version` est vérifié par le jeu et par le serveur ; toute évolution (par exemple pour
une re-simulation côté serveur) crée une version 2, et les replays v1 restent lisibles par le
lecteur du jeu.
