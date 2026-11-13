# Format des packages `.cplanet`

Un fichier `.cplanet` est une archive ZIP autonome contenant un niveau et **tout ce dont il
a besoin** pour être joué sur une autre machine : niveau, planète, ennemis, pouvoirs,
effets, animations, textures, fonds, sons, musiques et polices utilisés.

## Structure

```
COSMIC_LEVEL/
├── manifest.json        identité, dépendances, liste des fichiers avec SHA-256
├── level.json           le niveau (format cosmic.level)
├── planet.json          la planète du niveau
├── enemies/*.json       définitions des ennemis utilisés
├── powers/*.json        pouvoirs utilisés (déblocages, pouvoirs de départ, barrières)
├── effects/effects.json préréglages de particules
├── metadata/info.json   informations d'affichage (auteur, date, statistiques)
└── assets/              animations/, sprites/, textures/, backgrounds/, music/,
                         sounds/, ui/, fonts/ (chemins identiques à ceux du jeu)
```

## Manifeste

```json
{
  "format": "cosmic.package",
  "formatVersion": 1,
  "packageId": "sylvanox_01-studio_test",
  "name": "Les Ruines Suspendues",
  "levelId": "sylvanox_01",
  "planet": "sylvanox",
  "author": "Studio Test",
  "version": "1.0",
  "description": "…",
  "createdAt": "2026-11-13 12:28",
  "engineVersion": "1.0.0",
  "minEngineVersion": "1.0.0",
  "dependencies": { "planet": "sylvanox", "enemies": ["golem", "…"], "powers": ["dash", "…"] },
  "files": [ { "path": "level.json", "kind": "level", "size": 144229, "sha256": "49f4…" }, … ],
  "totalSize": 8794329
}
```

## Export

*Gestion des contenus → Exporter un niveau* (ou `--export <niveau> <fichier>`) :

1. collecte des dépendances : chaque objet, ennemi, pouvoir, effet et animation est
   résolu jusqu'aux fichiers ; une dépendance introuvable **bloque** l'export et est
   listée ;
2. résumé avant écriture (nombre de fichiers par type, taille, avertissements) ;
3. écriture dans un fichier temporaire puis renommage (jamais d'archive à moitié écrite).

## Import et sécurité

*Gestion des contenus → Importer un package* (ou double-clic sur un `.cplanet`, ou
`--import <fichier>`). L'archive est **analysée avant toute écriture** et refusée si :

* ce n'est pas un ZIP valide, ou le manifeste manque / est invalide / a un format inconnu ;
* `minEngineVersion` est supérieure à la version du jeu ;
* une entrée a un chemin dangereux : absolu, `..`, lecteur Windows (`C:`), barre oblique
  inverse, caractères interdits, noms réservés (`CON`, `NUL`, `COM1`…), lien symbolique ;
* une entrée est hors de `COSMIC_LEVEL/`, apparaît en double, ou un fichier déclaré dans
  le manifeste manque (un fichier non déclaré est ignoré, avec un avertissement) ;
* une extension n'est pas dans la liste blanche (`.json .png .jpg .jpeg .bmp .wav .ogg
  .mp3 .flac .ttf .otf .txt .md`) — exécutables, scripts et bibliothèques sont toujours
  refusés ;
* une taille, un CRC ou une empreinte SHA-256 ne correspond pas au manifeste ;
* les limites sont dépassées : 128 Mo par fichier, 512 Mo au total, 4000 entrées, taux de
  compression > 250 (protection contre les « bombes ZIP ») ;
* le niveau, la planète ou une définition d'ennemi / de pouvoir ne se charge pas, ou
  l'identifiant du niveau ne correspond pas au manifeste.

**Aucun code n'est jamais exécuté** depuis un package : il ne contient que des données.

Ensuite :

* **dépendances** : chaque ennemi placé dans le niveau doit être fourni par le package ou
  déjà présent dans le jeu, sinon l'import est refusé en nommant l'ennemi manquant ;
* **conflits** : si le package est déjà installé, ou si un ennemi / pouvoir / niveau de
  même identifiant existe, l'écran d'import les liste et l'utilisateur choisit *Renommer*
  (nouveaux identifiants, références réécrites) ou *Remplacer* (ancienne version du même
  package) ; les niveaux officiels et vos propres niveaux ne sont jamais écrasés ;
* **installation atomique** dans `packages/<packageId>/` du dossier utilisateur (dossier
  temporaire, puis renommage) ;
* au lancement du niveau, les assets du package sont montés **au-dessus** des assets du
  jeu : le niveau s'affiche exactement comme chez son auteur, sans modifier
  l'installation.

Les packages installés se gèrent dans l'onglet *Packages installés* : *Jouer*, *Éditer*
(copie modifiable dans l'éditeur) et *Supprimer* (avec confirmation).
