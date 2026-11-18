# Raids de clan « ASTERION » — phase 2 : la branche « BRAS DROIT » (2.65) ; phase 4 : le titan entier (2.68, §6)

La phase 1 (événements, inscriptions, groupes de 8, rappels : `server/src/raids.ts`, migration 0141)
prépare le raid. La phase 2 le rend **jouable** sur une première branche du titan, le **Bras droit** :
8 sous-zones, des instances de 8 joueurs au plus en verrouillage par entrées, et un état de la
branche partagé par tous les groupes, que le serveur ne met à jour qu'avec le **consensus** des
joueurs de chaque instance. Le gigaboss **Pyroclast** est la phase 3 : sa porte est scellée.

## 1. La branche (maquette « Branche »)

| Zone | Nom | Ce qu'on y fait | Passages |
|---|---|---|---|
| 1 | Épaule | entrée, **3 points d'apparition** ; reprendre l'arène (2 vagues) | 2, 3 |
| 2 | Biceps de basalte | plateformes qui « respirent » ; abattre le golem gardien | 1, 4, 6 |
| 3 | Veine de magma | terrasses à remonter, **coulée de magma** toutes les 60 s ; générateur ouest ; passage caché vers 5 ; raccourci (porte) vers 6 | 1, 4, 5, 6⇄ |
| 4 | Coude-forge | **deux soufflets, deux groupes** (un chacun) ; coulée toutes les 90 s ; l'Enclume vivante (facultatif) | 2, 3, 7🔒 |
| 5 | Grotte des scories | **zone secrète** ; la relique (facultatif) | 3 |
| 6 | Avant-bras | générateur est ; **levier du raccourci** vers la Veine (facultatif) | 2, 3⇄, 8 |
| 7 | Poignet-verrou | ouvert par les **2 générateurs** ; arène (3 vagues) qui ouvre la Main | 4, 8 |
| 8 | Main de Pyroclast | porte scellée « Pyroclast — prochaine version » | 6, 7 |

* Deux chemins : Épaule → Biceps → Avant-bras → Main, et Épaule → Veine → Coude-forge → Poignet → Main.
* **Une zone est contrôlée** quand tous ses objectifs non facultatifs sont faits. Les verrous :
  `generators` (les 2 générateurs), `shortcut` (le levier de l'Avant-bras), `objective:<id>`.
* Ennemis du bestiaire existant (acariens de braise, rôdeurs de lave, drones de forge, golem de
  magma, rempart de forge, ailes de cendre, gloops de magma, feux follets).

### Génération (jamais à la main)

`python3 tools/levelgen/raid_bras_droit.py` écrit `data/raids/asterion/bras_droit/zone_1.json …
zone_8.json` (des niveaux ordinaires de Pyralis, avec `metadata.raid`), le catalogue
`branch.json` et **sa copie serveur** `server/src/raidBranches.json`. La sortie est stable (mêmes
octets à chaque exécution) ; `tests/test_raid.cpp` vérifie les deux copies du catalogue et
l'empreinte des zones. `revision` (catalogue) change dès qu'une zone change de jeu : toutes les
instances doivent avoir la même (le `GO` du relais la porte, `hash` = révision).

`metadata.raid` d'une zone : `spawns` (`start` : les points de départ ; `"<zone>"` : l'arrivée depuis
cette zone), `passages` (`to`, `rect` en pixels, `lock`, `door`), `objectives` (`id` du catalogue,
`source` = objet du niveau, `trigger` = l'événement qui l'accomplit : `cleared`, `killed`,
`activate`, `collected`), `magma` (`zone` de dégâts allumée `duration` s toutes les `period` s à
partir de `first`, annoncée `warn` s avant), `sealed` (la porte de Pyroclast).

L'**état de la branche** est l'ensemble des objectifs faits : le bit *i* = l'objectif *i* du
catalogue (ordre fixe, on n'ajoute qu'à la fin). Générateurs, raccourci, passages ouverts, zones
atteignables et contrôlées s'en déduisent, de la même façon dans le jeu (`game/raid/RaidBranch`) et
sur le serveur (`raidZones.ts`).

## 2. L'instance : 8 joueurs en verrouillage par entrées

Une instance = un groupe de 8 d'un clan (les groupes de la phase 1) sur la branche. Elle réutilise
**toute la pile de la coopération** (docs/COOP.md) : le même protocole, le même `CoopRoom`
(Durable Object) **en mode raid**, `GameWorld` à N joueurs, `stepTeam`.

* **8 sièges** (`coopnet::kMaxPlayers = 8`, `GameWorld::kMaxTeam = 8`) ; la coopération des niveaux
  EXTRÊME reste à 2 ou 3 (`kMaxCoopPlayers`), avec ses règles et ses empreintes :
  `kCoopSimulationVersion` ne change pas, `kSimulationVersion` et `kRankingGeneration` non plus.
* **N'importe quel siège lance** l'instance (un joueur suffit) ; `GO` annonce toujours 8 joueurs.
* **Siège libre** : drapeau `InputFrame::Vacant` (4), posé par le relais seulement (jamais accepté
  d'un jeu), sur les trames d'un siège que personne ne tient. Le joueur est `Presence::Vacant` :
  hors jeu, invisible, ignoré de tous.
* **Arrivée en cours de partie** : un joueur inscrit prend le premier siège libre ; il reçoit
  l'historique (états de la branche d'abord, puis trames), le rejoue hors du fil principal,
  annonce `CATCHUP_DONE`, et joue à partir du pas `REJOIN` choisi par le relais ; il apparaît près
  de l'allié en jeu le plus proche, tous cœurs, pouvoirs de départ de la zone.
* **Déconnexion** : la règle des 60 s de la coopération (veille, retour à l'identique). Au-delà, le
  siège est **libéré** (trames `Vacant`) : n'importe quel inscrit peut le prendre.
* **Caméra** : chacun suit son propre joueur (affichage seulement). Pas de « cadre d'équipe » en
  raid : personne n'est ramené. Le groupe entier à terre : la zone reprend là où il y est entré
  (objectifs gardés, tous debout, tous cœurs).
* **Changer de zone** : le premier joueur en jeu qui **entre** dans un passage ouvert (plus petit
  numéro d'abord) emmène toute l'instance. Chacun garde **ses cœurs, son énergie, ses pouvoirs et
  sa présence** (`GameWorld::carryOf` / `restoreMember`) et apparaît au point de la zone pour
  l'endroit d'où il vient (un joueur à terre revient debout avec la moitié de ses cœurs). Un
  passage où l'on se trouve à l'arrivée ne ramène personne : il faut y entrer.
* **Paires** (les soufflets) : une instance n'en fait qu'un ; l'autre doit venir d'un autre groupe.

### Message `RAID_STATE` (18, relais → jeu)

`u32 pas`, `u32 objectifs faits` : l'état de la branche **à partir de ce pas**. Le relais le lit en
D1 (toutes les 3 s, et dès qu'un rapport est accepté) et l'envoie au premier pas non encore
diffusé ; tous les jeux de l'instance l'appliquent donc au même pas (portes des passages ouvertes,
alertes). Un jeu qui arrive les reçoit tous avant les trames. L'instance compte aussi tout de
suite ce qu'elle a fait elle-même (`RaidRun::mask()` = branche | instance).

### `raid::RaidRun` (déterministe)

Il n'y lit ni horloge, ni caméra, ni réseau : seulement les trames et l'état donné avec elles.
Objectifs (événements du monde), passages, coulée de magma (pas entiers), reprise de zone,
rapports (`objective`, `zone`) avec le pas et l'empreinte de l'état (`RaidRun::hash`).

## 3. Le consensus (serveur)

`POST /raids/instances/{id}/reports {kind: "objective"|"zone", objective?, zone, tick, hash}` :
chaque jeu rapporte ce qu'il a vu. Un rapport est **accepté** quand la **majorité des joueurs
actifs** de l'instance (`raid_instance_members.active`, écrit par le relais : plus de la moitié,
au moins 1) a envoyé **la même valeur** (`zone:pas:empreinte`). Avant cela :

* **vraisemblance** : le raid est en cours, l'objectif est bien dans cette zone, la zone est
  atteignable avec l'état de la branche, le pas est possible depuis le début de l'instance ;
* un joueur ne rapporte qu'une valeur par clé ; une autre est refusée.

Les rapports minoritaires (un menteur, un jeu désynchronisé) sont **refusés et journalisés**
(`raid_report_rejections`). Un objectif accepté entre dans `raid_branch_objectives` une seule fois
(clé primaire par objectif : deux instances, ou deux acceptations simultanées, le comptent une fois),
sauf le second soufflet d'une même instance (journalisé). Un rapport de zone accepté place le
groupe sur la carte (`raid_instances.zone`).

### Routes

| Route | Rôle |
|---|---|
| `GET /raids/events/{id}/branch` | la carte : zones (atteignable, contrôlée, objectifs et par quel groupe), passages, générateurs, raccourci, instances (clan, groupe, zone, joueurs, places), la mienne, le journal des derniers objectifs |
| `POST /raids/events/{id}/instance {instance_id?}` | l'instance de mon groupe (créée une fois), ou celle d'un autre groupe que j'aide ; inscrit et raid en cours seulement |
| `GET /raids/instances/{id}/socket` | la WebSocket du relais (inscrit à l'événement) |
| `POST /raids/instances/{id}/reports` | un rapport (ci-dessus) |

Tables (migration **0142_raid_zones.sql**) : `raid_instances`, `raid_instance_members`,
`raid_reports`, `raid_accepted`, `raid_branch_objectives`, `raid_report_rejections`.

## 4. Écrans

* **Fiche de l'événement** (en cours) : « Carte du Bras droit ».
* **Carte de la branche** (maquette « Branche ») : les 8 zones reliées (ouvert, combat en cours,
  verrouillé en pointillés, zone secrète, raccourci), leur état et les groupes présents (le mien en
  or) ; à droite la zone choisie : objectifs (✓, ◆ facultatif, et par quel groupe), ce qu'il y a
  dedans, les groupes présents, « Rejoindre · N places » et « Aider [TAG]·n ». Clavier, manette
  (zones focalisables) et souris ; rafraîchie toutes les 10 s.
* **Combat** (maquette « Combat ») : minimap permanente en haut (vous, le groupe, ennemis,
  objectifs, passages et porte scellée, coulée), panneau du groupe (8 lignes : cœurs, état,
  réanimations de la zone), objectifs de la zone avec leur progression (générateurs 1 / 2,
  soufflets 1 / 2), alertes du groupe, de la branche et du serveur (objectif validé ou refusé),
  bandeau de danger (« Coulée dans 3 s · montez ! »), état du réseau. La pause ne s'arrête pas : le
  groupe continue.

## 5. Limites de cette phase

* Pyroclast (phase 3) : la porte de la Main reste scellée ; pas de récompense.
* Les replays d'une instance ne sont pas encore conservés côté serveur (le relais garde les trames
  en mémoire, comme la coopération).
* Les soufflets demandent deux groupes, sans la contrainte « à 3 s près » de la maquette : deux
  instances ne partagent pas d'horloge commune assez précise.
* Le chat de groupe (« GROUPE · Lyra : … » de la maquette) n'est pas dans cette phase : les
  alertes viennent du jeu et du serveur seulement.

## 6. Phase 4 (2.68) : le titan entier — sept branches, carte globale, liens, répartition

### Les sept branches (`tools/levelgen/raid_asterion.py`)

Six branches de plus, écrites par `python3 tools/levelgen/raid_asterion.py` (sortie stable, jamais à
la main ; `--en fichier.json` donne l'anglais de chaque texte pour `tools/i18n/extract.py --merge`) :
`data/raids/asterion/<branche>/zone_1.json … zone_8.json` et `branch.json`, le catalogue du titan
`data/raids/asterion/titan.json` et **sa copie serveur** `server/src/raidTitan.json` (titan + les six
catalogues ; le Bras droit reste à `raid_bras_droit.py` et `raidBranches.json`, lus seulement).

| Branche | Gigaboss | Monde | Traversée | Danger périodique | Deux groupes | Verrou de la zone 7 |
|---|---|---|---|---|---|---|
| Crâne | Skullmaw | Confins (os, obsidienne) | vertèbres flottantes, regards lasers | onde psychique | 2 nerfs optiques | les deux yeux |
| Bras gauche | Dreadfang | Glacior | glace qui cède, tourelles | balayage des griffes | 2 lames | 2 cœurs de givre |
| Torse | Heartbreaker | Noyau | côtes mouvantes, barrières d'énergie | pulsation du cœur | 2 × 2 plaques | les 4 plaques |
| Abdomen | Voidworm | Sylvanox (racines, acide) | parois qui respirent | reflux d'acide | 2 valves | 2 glandes |
| Jambe gauche | Graviton | Aethra (ruines flottantes) | puits d'apesanteur, îlots mouvants | puits de gravité | 2 rotules | 2 générateurs de gravité |
| Jambe droite | Titanus | Pyralis (forge, lave) | pistons géants | onde sismique | 2 pistons | 2 réacteurs |

Chaque branche : entrée à 3 points d'apparition et son arène, deux chemins, une **zone secrète** (5) et
sa relique, un **raccourci** ouvert au levier, une **zone à deux groupes** (une paire d'objectifs), un
verrou `generators` sur plusieurs zones (textes propres dans `lock_texts` du catalogue), l'arène de
la zone 7 qui ouvre l'antre (8), dont la **porte reste scellée** (« <Gigaboss> — prochaine version » ;
les six combats viennent ensuite). Les dangers réutilisent `metadata.raid.magma` avec `name`, `hint`,
`message` (le Bras droit garde ses textes par défaut).

### L'expédition avant l'antre (zone 7) et l'arène du gigaboss (zone 8)

La zone 7 de chaque nouvelle branche est **une longue expédition pour 8 joueurs** (≈ 15 min même à 8) :
trois étages (galeries basses avec les traversées et le danger de la branche, étage du milieu, étage
haut ; des puits de corniches les relient à chaque secteur) pour que 8 joueurs ne s'entassent pas ;
**deux écluses** (un mur sur les trois étages, une porte à chaque étage : elles s'ouvrent ensemble quand
3 joueurs se tiennent sur la plaque) et **un monte-charge** (il démarre avec 4 joueurs dessus, puis fait
la navette seul) — `metadata.raid.coop` (`rect`, `players`, `targets`, `action`), moins de joueurs
demandés quand moins sont en jeu (jamais de blocage à 1 ou 2) ; **points de contrôle** à chaque étage
(`metadata.raid.checkpoints`, arrivée `spawns["cp:<id>"]`) : le groupe entier à terre reprend au dernier
atteint, et chaque nouveau point relève les joueurs à terre ; **quatre embuscades géantes** (une zone de
combat sur les trois étages, quatre vagues, des ennemis à chaque étage : le système d'embuscade, état
gardé à la chute, compteur, flèches hors écran) — la dernière, la garde de l'antre, est l'objectif qui
ouvre la porte de la zone 8. Tout se joue dans `raid::RaidRun` (plaques, points de contrôle : au même
pas sur tous les jeux, dans l'empreinte).

**Durée estimée** (`estimate_minutes` du générateur, champ `minutes` de chaque zone du catalogue) : la
largeur à 2 tuiles/s, les PV des ennemis à 4 PV/s pour 8 joueurs, 8 s par vague, 30 s par mécanisme à
plusieurs (plus le trajet du monte-charge). Avec un joueur (1,5 PV/s), le modèle retrouve les temps de
référence de la campagne à ±40 % (`calibrate()`). Le générateur exige 13 à 17 min pour l'expédition et
18 à 35 min pour toute la branche hors gigaboss (un raid standard de 45 à 60 min avec le combat et les
attentes des autres groupes) ; `tests/test_raid_titan.cpp` le revérifie.

La zone 8 : une antichambre, la porte scellée, puis **l'arène du gigaboss**, immense pour 8 joueurs
attaqués à la fois (200 tuiles de large, plus de deux écrans ; cinq étages de plateformes ; deux piliers),
ses bornes déclarées dans `metadata.raid.arena` (`rect`, `floor`, `door`, `entry`, en pixels) pour les
combats à venir.

### Les liens entre branches (spec §7)

`titan.json` → `links` : `from` (objectifs d'autres branches, tous requis), `to` (la branche cible),
`bit` (24 et plus : un bit de **l'état de la branche cible**, sous lequel restent ses objectifs), `kind`,
`zones`, `objects` :

| `kind` | Effet dans la simulation (`raid::RaidRun::applyEffect`) |
|---|---|
| `open` | les passages verrouillés `link:<id>` s'ouvrent (portes comprises) |
| `disable` | les objets des zones (lasers, tourelles, barrières) sont éteints |
| `remove` | les ennemis des zones (leurs objets d'apparition) disparaissent |
| `hazard_off` | le danger périodique des zones s'arrête |

Onze liens : les yeux du Crâne → tourelles du Bras gauche ; ses nerfs → onde sismique de la Jambe
droite ; les générateurs de la Jambe gauche → barrières du Torse ; le nid de l'Abdomen → parasites
du Torse ; les plaques du Torse → passage Intestin ⇄ Gosier de l'Abdomen ; la cheville gauche →
passage Crevasse ⇄ Poignet du Bras gauche ; les réacteurs de la Jambe droite → coulées du Bras droit ;
le poignet du Bras droit → passage Tempe ⇄ Voûte du Crâne ; la cheville de Titanus → passage Tibia ⇄
Cheville de la Jambe gauche ; les valves de l'Abdomen → puits de gravité de la Jambe gauche ; les lames
de Dreadfang → spectres en renfort du Crâne.

Le serveur calcule l'état d'une branche (`raidMap.stateMaskOf` : ses objectifs acceptés + les liens
dont toutes les sources sont acceptées) ; `raidZones.branchMaskOf` (lu par le relais toutes les 3 s et
à chaque objectif accepté, **toutes branches confondues**) le donne aux jeux par `RAID_STATE` : chaque
jeu de l'instance applique le lien **au même pas** (et de nouveau à chaque chargement de zone), avec
une alerte « TITAN · … ». Rien ne vient d'un jeu ni de l'interface. Le hash du `GO` devient
`<révision de la branche>.<révision du titan>`.

### Progression collective

Le consensus de la phase 2 (`POST /raids/instances/{id}/reports`) vaut pour les sept branches : le
catalogue de la branche de l'instance, sa joignabilité avec les liens, l'objectif compté une fois par
événement, le menteur minoritaire journalisé, l'égalité refusée.

### Répartition des joueurs (spec §8, migration **0144**)

`raid_placements` (une place par joueur et événement : instance, branche, par qui, pourquoi) et
`raid_placement_log` (chaque changement). **8 joueurs au plus par instance** : une seule instruction
conditionnelle place un joueur (deux placements simultanés ne font jamais un neuvième) ; une instance
s'ouvre seulement quand toutes celles du clan sur la branche sont pleines.

| Route | Rôle |
|---|---|
| `GET /raids/events/{id}/titan` | la carte globale : branches (état, zones contrôlées, objectifs, groupes, joueurs), `body` (état de chaque partie pour la figure), liens, renforts demandés, clans par objectifs, alertes, ma place |
| `GET /raids/events/{id}/branches/{partie}` | la carte d'une branche (comme `/branch`, pour les sept) avec ses liens reçus et donnés |
| `POST /raids/events/{id}/branches/{partie}/join {instance_id?}` | ma place sur cette branche (groupe de ma liste d'abord, puis les groupes en combat qui ont de la place, puis une nouvelle instance) ; avec `instance_id` : aider ce groupe s'il a de la place |
| `POST /raids/events/{id}/join` | ma place, ou là où le clan a besoin de moi (un groupe en combat qui n'est pas plein, sinon la branche où le clan a le moins de joueurs) |
| `GET /raids/events/{id}/placements` | les joueurs de mon clan et leur place, les groupes, `can_manage` |
| `POST /raids/events/{id}/placements {player_id, branch, instance_id?}` | le chef, un officier ou le chef de raid déplace un membre |
| `POST /raids/events/{id}/placements/rebalance {branch}` | répartit les joueurs d'une branche sur le moins de groupes possible, à parts égales (ceux qui ne jouent pas bougent d'abord) |

Un joueur en combat garde sa partie : sa nouvelle place compte à sa prochaine entrée.

### Écrans

* **Carte du titan** (fiche d'un raid en cours › « Carte du titan ») : le titan (`AsterionFigure`, une
  partie = sa branche), les groupes par branche, la progression, les effets, les renforts, les alertes,
  « Rejoindre » et « Répartition du clan ». Les sept branches sont aussi des arrêts du focus.
* **Carte d'une branche** (`RaidBranchScene`, pour les sept) : les passages ouverts par une autre
  branche (✦), la section « Liens avec le reste du titan ».
* **Répartition du clan** (`RaidRosterScene`) : membres, groupes, « Déplacer vers », « Équilibrer ».
* **Combat** : les verrous et dangers de chaque branche, les liens actifs de la zone (✦).
