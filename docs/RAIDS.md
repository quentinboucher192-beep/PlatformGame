[← Retour à l'accueil (README)](../README.md)

# Raids de clan « ASTERION » — phase 2 : la branche « BRAS DROIT » (2.65) ; phase 4 : le titan entier (2.68, §6) ;
# phase 3 : le gigaboss PYROCLAST (2.68, §7 à §12) ; phase 4B : VOIDWORM, GRAVITON, TITANUS (2.69, §13) ;
# phase 5 : les événements mondiaux (2.69, §14) ; phase 7 : fiabilité, charge et anti-triche (2.74, §15) ;
# les fiches d'Asterion et des Gigaboss (2.73, §16)

La phase 1 (événements, inscriptions, groupes de 8, rappels : `server/src/raids.ts`, migration 0141)
prépare le raid. La phase 2 le rend **jouable** sur une première branche du titan, le **Bras droit** :
8 sous-zones, des instances de 8 joueurs au plus en verrouillage par entrées, et un état de la
branche partagé par tous les groupes, que le serveur ne met à jour qu'avec le **consensus** des
joueurs de chaque instance. La phase 3 (2.68) ouvre la Main : le gigaboss **Pyroclast** (§7 à §12).

<p align="center"><img src="media/asterion_film.gif" width="600" alt="Film d'Asterion : le titan se lève et rugit, « Le Dévoreur de mondes »"></p>
<p align="center"><sub>Deux moments du mini-film d'Asterion (§4).</sub></p>

## 1. La branche (maquette « Branche »)

| Zone | Nom | Ce qu'on y fait | Passages |
|---|---|---|---|
| 1 | Épaule | entrée, **3 points d'apparition** ; reprendre l'arène (2 vagues) | 2, 3 |
| 2 | Biceps de basalte | plateformes qui « respirent » ; abattre le golem gardien | 1, 4, 6 |
| 3 | Veine de magma | terrasses à remonter, **coulée de magma** toutes les 60 s ; générateur ouest ; passage caché vers 5 ; raccourci (porte) vers 6 | 1, 4, 5, 6⇄ |
| 4 | Coude-forge | **deux soufflets, deux groupes** (un chacun) ; coulée toutes les 90 s ; l'Enclume vivante (facultatif) | 2, 3, 7🔒 |
| 5 | Grotte des scories | **zone secrète** ; la relique (facultatif) | 3 |
| 6 | Avant-bras | générateur est ; **levier du raccourci** vers la Veine (facultatif) | 2, 3⇄, 8 |
| 7 | Poignet-verrou | ouvert par les **2 générateurs** ; 2.68 : **la longue route** (§11) dont la dernière embuscade ouvre la Main | 4, 8 |
| 8 | Main de Pyroclast | le gigaboss **Pyroclast** (§6) ; objectif `main_pyroclast` | 6, 7 |

* Deux chemins vers le Coude-forge (Biceps / Veine), puis le Poignet-verrou et la Main ; 2.68 : la Main ne s'ouvre
  (aussi par l'Avant-bras) qu'une fois le verrou du Poignet brisé (`objective:poignet_verrou`).
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
partir de `first`, annoncée `warn` s avant), `gigaboss` (2.68, §7 : l'objet du boss, ses évents en pixels, les
phases de sa carapace, ses points faibles et les nombres de son combat). `revision` vaut 2 depuis la phase 3.

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

<p align="center"><img src="media/raid_branch_map.png" width="820" alt="Carte de la branche Bras droit : les huit zones, leurs passages, verrous et objectifs"></p>
<p align="center"><sub>La carte d'une branche au début d'un raid de clan (serveur lancé en local, comptes de démonstration).</sub></p>

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
* **Mini-film d'Asterion** (2.68, `ui/screens/AsterionFilm`) : avant le premier affichage du titan
  (CLANS › RAIDS, fiche d'un événement, tout `AsterionFigure` : un seul appel `asterionfilm::hold`),
  dix plans d'environ deux minutes joués par le moteur des histoires (`makeFilm`). Le script — légende,
  titres et durée de chaque plan — se modifie à la main dans `data/raids/asterion_film.json` (une durée
  changée étire la scène) ; les scènes (`asterion_film_scenes.json`), les planches du titan en calques et
  les décors sont écrits par `tools/assetgen/environment/story_asterion.py` (`--check`, `--storyboard`),
  la musique par `tools/assetgen/audio` (`asterion_film`). Première projection impossible à passer ;
  « vu » (`asterionFilmSeen` de la progression, uni par `ProgressMerge`) écrit seulement à la fin ;
  « Revoir la cinématique » sur la carte de l'événement (celle-là se quitte). Contenu local, hors ligne.

## 5. Limites de la phase 2

* (2.68 : la Main est ouverte, voir §7.)
* Les replays d'une instance ne sont pas encore conservés côté serveur (le relais garde les trames
  en mémoire, comme la coopération ; 2.74 : et dans le stockage de son objet durable tant que dure la
  partie, pour la retrouver après un redémarrage, §15).
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

<p align="center"><img src="media/raid_titan_map.png" width="820" alt="Carte du titan : Asterion et ses sept branches, les groupes du clan, la progression"></p>
<p align="center"><sub>La carte du titan au début d'un raid de clan (serveur lancé en local, comptes de démonstration).</sub></p>

* **Carte du titan** (fiche d'un raid en cours › « Carte du titan ») : le titan (`AsterionFigure`, une
  partie = sa branche), les groupes par branche, la progression, les effets, les renforts, les alertes,
  « Rejoindre » et « Répartition du clan ». Les sept branches sont aussi des arrêts du focus.
* **Carte d'une branche** (`RaidBranchScene`, pour les sept) : les passages ouverts par une autre
  branche (✦), la section « Liens avec le reste du titan ».
* **Répartition du clan** (`RaidRosterScene`) : membres, groupes, « Déplacer vers », « Équilibrer ».
* **Combat** : les verrous et dangers de chaque branche, les liens actifs de la zone (✦).

## 7. Phase 3 (2.68) : le gigaboss PYROCLAST

<p align="center"><img src="media/raid_pyroclast.gif" width="640" alt="Pyroclast : trois joueurs tiennent ses évents ensemble, sa carapace éclate"></p>
<p align="center"><sub>Filmé dans le test des administrateurs (en local, jamais classé), les autres joueurs pilotés par des scripts.</sub></p>

`data/enemies/pyroclast.json` : une main de magma qui marche sur ses doigts (le cerveau du golem : poing météore =
`skyfall`, secousse = `quake`), trois phases et une dernière éruption ; les **Poings de magma** (un par joueur en
jeu) s'y ajoutent à chaque phase :

| Phase | Seuil | Attaques | L'arène (événements `bossPhase` de la zone) |
|---|---|---|---|
| 1 « Poing de magma » | 100 % | coup de poing, pluie de magma, éruption, bombes de lave, secousse | — |
| 2 « Carapace de basalte » | 66 % | poing météore, secousse, pluie, éruption | la croûte des fosses sous les tours cède au magma, soins |
| 3 « Cœur de magma » | 33 % | marée de magma, double poing météore, secousse, éruption folle, pluie, poing | rideaux de lave aux deux murs, soins |

Le sprite (trois feuilles, une par phase : plaques nues, carapace et évents, cœur à nu), son point faible et ses
évents sont générés par `tools/assetgen/characters/boss_k_pyroclast.py` (`--check` : mêmes octets, testé ;
`--review` : planche contact).

`game/raid/Gigaboss` mène les règles du raid autour de lui, pas à pas, dans la simulation (le même sur chaque
ordinateur) :

* **Un géant pour 8** : 2,5 fois la taille du sprite (une boîte de 220 × 300 px), une arène de 183 tuiles (plus de
  six écrans de large), deux tours de corniches au-dessus de fosses, deux monte-charges vers une plateforme haute
  au-dessus de sa tête : ses trois évents sont sur les deux tours et la plateforme (le groupe doit se séparer).
* **Poings de magma** (toutes les phases, toutes les 9 / 7 / 5,5 s ÷ sa pression) : une marque sous **chaque joueur
  en jeu**, le même pas (8 joueurs : 8 poings), puis un poing de magma tombe sur chaque marque 1,4 s plus tard.
  Avec la pluie de magma, les éruptions, la marée de magma (seules les corniches restent sûres) et les poings
  météores, il attaque tout le groupe à la fois.
* **Mise à l'échelle** : sa vie × (1 + 0,55 × (sièges occupés − 1)) au début du combat (2 à 8 joueurs), et encore
  quand un joueur prend un siège pendant le combat (jamais à la baisse) ; ses temps de recharge ÷ (1 + 0,06 ×
  (joueurs en jeu − 1)).
* **Carapace** (phase 2) : rien ne le blesse (`Enemy::setRaidGuard`) tant qu'un de ses **3 évents** est ouvert. Un
  joueur debout 3 s sur un évent le ferme (chaque joueur dessus compte un « évent fermé ») ; un évent fermé se
  rallume au bout de 15 s (× 1,35 à deux joueurs, × 2 seul) : il faut les tenir **ensemble**. Chaque évent ouvert
  crache un jet de magma toutes les 6 s, à tour de rôle (annoncé 1,2 s avant) : une zone de danger.
* **Cœur à nu** : les trois fermés, la carapace éclate 10 s (dégâts × 2) et ses **3 points faibles**
  (`pyroclast_noyau`, collés à ses jointures) peuvent être brisés : chacun lui ôte 4 % de sa vie et compte pour qui
  l'a brisé. Puis les évents se rallument.
* **Sauvetages** : relever un allié à terre (la règle de la coopération, 2 par zone) compte pour qui était près de
  son âme.
* **Sa chute** : tous les joueurs en jeu sont « présents à la chute ». Un groupe entier à terre : la zone reprend, le
  boss aussi. Une Main dont l'objectif est déjà validé (un autre groupe l'a vaincu) est éteinte.

## 8. Contributions et consensus (`game/raid/Contributions`, `raidZones.ts`)

Calculées dans la simulation (l'écouteur du monde : les attaques de chaque joueur sont étiquetées), un
enregistrement par joueur ayant tenu un siège (`seat`, `from` = le pas où il l'a pris) : dégâts (dixièmes de vie),
dégâts au gigaboss, dégâts évités (ses coups visant ce joueur et esquivés, les coups arrêtés par son bouclier),
blocages, sauvetages, évents, points faibles, objectifs (une action d'un joueur : la sienne ; un combat gagné : celle
de tous les joueurs en jeu), pas en jeu, présence à la chute. L'empreinte de `RaidRun` les contient.

Deux rapports de plus, par le même consensus (majorité stricte des joueurs actifs, minorité journalisée, égalité
jamais acceptée) :

* `{kind: "boss", phase}` (1 le combat commence, 2 et 3 ses phases, 4 sa chute) → `raid_gigaboss_fights` ;
* `{kind: "result", records: [...]}` à chaque jalon (objectif, phase) → `raid_contributions` (une ligne par
  `seat`/`from`, attribuée au membre qui tenait ce siège depuis ce pas : `raid_instance_members.seat_from`, écrit
  par le relais ; le tableau le plus récent garde ses lignes). Avant toute majorité, les tableaux impossibles sont
  refusés (temps au-delà du pas, plus d'évents qu'un toutes les 3 s, plus de sauvetages qu'un toutes les 2 s…).

La valeur comparée est le texte canonique du tableau, reconstruit par le serveur à partir des nombres.

## 9. Corps d'Asterion, résultats et récompenses (`raidRewards.ts`)

* **Corps** (`body` de chaque événement, état validé seulement) : bras droit « attaque » dès qu'il y a une progression
  validée, « affaibli » quand un combat a atteint sa phase 2, « detruit » quand `main_pyroclast` est accepté ;
  `effects` : « Plus de pluies de lave » (seulement noté et affiché dans cette phase).
* **Résultats** `GET /raids/events/{id}/results` : clans (somme des scores), joueurs par catégorie, combats contre
  Pyroclast, mon dernier tableau « en attente de validation » s'il attend sa majorité ; « Classement final » une fois
  l'événement terminé. Le **score de contribution** mélange tout, chaque part plafonnée : dégâts 500, évités 300,
  soutien 400, progression (évents, points faibles, objectifs, chute) 1 880, présence 600.
* **Récompenses** (`raid_reward_grants`, clé unique joueur + récompense + événement) accordées par le serveur à
  partir des résultats validés : badge « Main de Pyroclast » (présent à une chute validée : le succès
  `communaute_pyroclast`, que le serveur n'accepte qu'une fois la récompense récupérée), titre « Assaillant
  d'Asterion » (5 min), « Briseur de magma » (score ≥ 1 500), « Fléau de Pyroclast » (3 meilleurs scores, à la fin
  de l'événement). `POST /raids/rewards/claim {id?}` est idempotent ; les titres vont dans `player_won_rewards`
  (`raid:<événement>`) : profil et vestiaire.

Migration **0143_raid_gigaboss.sql** : `raid_instance_members.seat_from`, `raid_contributions`,
`raid_gigaboss_fights`, `raid_reward_grants`.

## 10. Écrans (2.68) et test des administrateurs

* **Combat** : les évents dessinés sur l'arène (fermé, ouvert, annoncé, jet ; la tenue en anneau), la carapace,
  les objectifs du gigaboss (évents k / 3, cœur à nu et ses points faibles, allié à relever), mes contributions en
  bas à droite, la carte de sa chute (« Résultats du raid »).
* **Résultats du raid** (maquette « Résultats ») : depuis la fiche de l'événement et à la chute de Pyroclast.
* **CLANS › RAIDS** : « Récompenses à récupérer » avec « Récupérer ».
* **ADMINISTRATION › Raids › « Tester Asterion »** (administrateurs) : une branche du catalogue
  (`raid::Branch::all`, les branches ajoutées plus tard y apparaissent seules), une zone (le combat de Pyroclast par
  défaut), « Invincible (admin) · non classé », 2 à 8 joueurs simulés, phase de départ ; seul et en local
  (`RaidRun::setTest`) : aucun rapport n'est jamais produit, et le serveur refuse tout rapport marqué `test`.

## 11. La longue route du Poignet-verrou (2.68)

La zone 7 est un vrai niveau d'environ **15 minutes pour 8 joueurs** : des galeries à deux voies (sol et grilles) pour
que huit joueurs ne s'empilent pas, **quatre embuscades géantes sur plusieurs étages** (la Tour des tendons, la
Galerie des forges, la Fosse des scories, la Garde du verrou : des vagues sur chaque étage, les portes fermées
pendant le combat ; l'état, le compteur et les flèches violettes des embuscades du jeu), des **mécanismes de groupe**
(`metadata.raid.coop`, le format des six autres expéditions : la vanne double, 4 joueurs sur sa plaque ; le
monte-charge, 3 joueurs dessus ; les plaques jumelles, deux plaques d'un même `group` tenues au même pas, les joueurs
en jeu partagés entre elles, seul la première suffit ; chaque joueur sur une plaque compte un « mécanisme »),
des **points de contrôle** (le groupe entier à terre reprend au dernier atteint) et des réanimations.

L'estimation est celle des sept branches (`estimate_minutes` de `tools/levelgen/raid_asterion.py`, appelée par
`raid_bras_droit.py`, champ `minutes` du catalogue, recalculée par `tests/test_raid.cpp` depuis le niveau lui-même) :
**≈ 15,4 min** à 8 (13 à 17 exigés ; quatre embuscades de 4, 4, 4 et 5 vagues). Les autres zones restent courtes
(la branche entière ≈ 22 min sans le gigaboss : un raid standard de 45 à 60 min avec son combat et les attentes).

## 12. La caméra des arènes de gigaboss (`game/raid/ArenaCamera`, affichage seulement)

En entrant dans l'arène : un plan de **l'arène entière** et du boss (4 s), puis une vue qui suit le joueur de cet
ordinateur avec un zoom entre 300 et 620 px de monde en hauteur : assez large pour les marques qui le menacent, le
boss quand il est proche et les alliés voisins, jamais au point de rendre l'action minuscule. Des flèches au bord de
l'écran (hors du HUD mesuré) montrent le boss (ses points faibles quand il est à nu), les alliés (leurs couleurs) et
les ennemis d'une embuscade. Elle sert à toutes les arènes de gigaboss : elle lit `metadata.raid.arena` (`rect`,
`floor`, `door`, `entry`), le format des sept antres (`raid::ZoneRules::arena`). La simulation ne la voit jamais.

## 13. Phase 4B (2.69) : VOIDWORM, GRAVITON, TITANUS

<p align="center">
<img src="media/raid_graviton.gif" width="640" alt="Graviton : la gravité s'inverse dans un secteur de l'arène">
</p>
<p align="center"><sub>Graviton inverse la gravité d'un secteur. Filmé dans le test des administrateurs (en local, jamais classé), les autres joueurs pilotés par des scripts.</sub></p>

Les antres de l'ABDOMEN, de la JAMBE GAUCHE et de la JAMBE DROITE sont descellées (zone 8 de chaque branche, écrite par
`tools/levelgen/raid_voidworm.py`, `raid_graviton.py`, `raid_titanus.py`, appelés par `raid_asterion.py` ; `BRANCH_REVISION` 2).
Chaque catalogue gagne, à la fin, l'objectif de son gigaboss (rôle `gigaboss` : `abd_voidworm`, `jg_graviton`, `jd_titanus`) :
le serveur accepte alors ses rapports `boss` et détruit la partie du titan à sa chute, comme pour Pyroclast.

`metadata.raid.gigaboss` garde le format de Pyroclast (§7 : vie et pression selon les sièges, marques sous chaque joueur
`fists`, évents `vents`, carapace `carapace`, points faibles) et ajoute `routine`, les textes (`fallText`, `goneText`,
`summary`), la planche des effets (`fx`) et `links` (des objectifs de la branche, lus dans son état au même pas). La routine
(`game/raid/GigabossRoutine`, un fichier par gigaboss) mène le reste du combat dans la simulation, après les marques ; son
état entre dans l'empreinte du gigaboss ; ce qu'elle montre (zones, plaques, marques, ondes, lignes des objectifs) est
seulement affiché par l'écran de combat.

| Gigaboss | Phases | Sur chaque joueur | Mécanisme collectif (phases) | Liens de la phase 4A |
|---|---|---|---|---|
| Voidworm (ver du vide, au milieu de l'arène) | « Le ventre affamé », « Les anneaux de chitine », « Le reflux » | Fouissement (une gueule sort du sol sur la marque) ; sa gueule vise 1 joueur (2 à partir de 6) d'un anneau vert qui suit puis se fige : qui y reste est **avalé** | 3 crochets (deux au sol, un sur la couronne au-dessus de lui) tenus ensemble 3 s : épinglé, sa gueule s'ouvre 10 s (phase 2) | nid détruit : deux fois moins de parasites ; les deux valves fermées : l'acide monte moins haut |
| Graviton (jambe qui flotte) | « Le champ de gravité », « Les générateurs », « Le monde à l'envers » | Chute de débris ; un puits de gravité sous chacun (phases 2 et 3) | 4 générateurs (deux au sol aux deux bouts, deux sur des îlots en hauteur) : un joueur sur chacun **au même instant**, 2,5 s ensemble ; elle s'effondre au sol 12 s (phases 2 et 3) | générateurs de la jambe coupés : un générateur de moins |
| Titanus (jambe de forge) | « Le pas du colosse », « Les entraves », « La forge à nu » | Écrasement (son talon) ; rochers de la voûte (phases 2 et 3) | 2 entraves de cheville brisées à moins de 1,5 s d'écart (seule, une entrave se reforge) : à genou 11 s (phases 2 et 3) | réacteurs coupés : fenêtre × 1,5 |

* **Voidworm** : un avalé est gardé dans sa poche (une pièce fermée sous le sol, des sucs toutes les 3 s), libéré quand la
  poche qui pousse sur son cou est crevée (un **sauvetage** pour qui la brise) ou recraché au bout de 12 s ; jamais le dernier
  joueur dehors. Ses parasites sortent de quatre trous (1 + joueurs / 2 par vague). À sa 3e phase, une marée d'acide couvre
  toute l'arène toutes les 22 s (annoncée 3 s avant).
* **Graviton** : l'arène en quatre secteurs ; un secteur annoncé (2 s) voit sa gravité s'inverser (4,5 à 6 s) : les joueurs
  montent vers la voûte, dont le champ blesse, sauf sous une des deux dalles du secteur. Un secteur, puis deux, puis la moitié
  de l'arène à sa 3e phase.
* **Titanus** : ses ondes de choc partent de ses pieds vers les deux murs, le long du sol (40 px de haut : à sauter), par deux
  à sa 3e phase ; sa 2e phase fend le sol au-dessus de la forge (dalles qui cèdent, lave).
* Contributions : chaque joueur sur un crochet ou un générateur quand il cède, chaque briseur d'entrave quand elles cèdent
  ensemble, compte un « mécanisme » (jamais plus d'un toutes les 3 s) ; les points faibles (3 par exposition) comme pour Pyroclast.
* Mise à l'échelle : vie × (1 + 0,55 × (sièges − 1)), points faibles, poche et entraves × (1 + 0,25 × (sièges − 1)), temps ÷
  la pression ; moins de joueurs : plus de temps (× 1,35 à deux, × 2 seul) et moins de générateurs à tenir (un par joueur).
* **Récompenses** (`raidRewards.ts`, sans migration) : un badge par gigaboss (`GIGABOSS_BADGES` : `voidworm_badge`,
  `graviton_badge`, `titanus_badge`, succès `communaute_voidworm`…), accordé une fois à qui était présent à sa chute validée
  (`raid_gigaboss_fights.boss`) ; « Fléau de Pyroclast » reste réservé à la chute de Pyroclast. Corps : chaque partie
  combattue « affaiblie » puis « détruite » ; résultats : `gigaboss.defeated_bosses`, `fights[].boss`.
* Sprites (trois phases chacun, cibles, effets) : `tools/assetgen/characters/boss_m_voidworm.py`, `boss_m_graviton.py`,
  `boss_m_titanus.py` (`--check`, `--review`), aux couleurs de leur partie dans `asterion_sprites.png`.
* Tests : `tests/test_raid_gigaboss_b.cpp` (générateurs, taille, vie de 2 à 8, même empreinte à 2 et 8 joueurs et par le relais,
  marques sur chaque joueur, crochets, avalement, acide, parasites, générateurs, inversion, entraves, ondes, chute et test admin
  jamais envoyé), `server/test/raidGigabossB.test.ts` (chute, badge accordé une fois, partie détruite, résultats).

### 13 bis. SKULLMAW, DREADFANG et HEARTBREAKER (2.69)

Les antres du Crâne, du Bras gauche et du Torse sont ouverts eux aussi : `tools/levelgen/raid_skullmaw.py` (la coque
commune `shell()` de ces trois antres), `raid_dreadfang.py`, `raid_heartbreaker.py`, appelés par `raid_asterion.py`
(révision de branche 3) ; leurs objectifs de rôle `gigaboss` : `crane_skullmaw`, `bg_dreadfang`, `torse_heartbreaker`.
Leurs routines (`game/raid/SkullmawRoutine`, `DreadfangRoutine`, `HeartbreakerRoutine`) suivent le cadre de
`GigabossRoutine` : armure = étape « carapace » de `raid::Gigaboss`, exposition et points faibles du cadre
(`expose` / `exposedStep`, un ennemi par point quand `weakSpots` en nomme un : les yeux et le cristal de Skullmaw),
plaques tenues par `holdPlates` (les chaînes, les côtes), marques sous chaque joueur par les poings du cadre
(`fists`, `rise` : tirées du sol), affichage par `BossMark` (deux genres de plus : `Beam`, un regard ou une chaîne ;
`Cone`, la lumière de l'œil qui révèle les plateformes dans les ténèbres).

<p align="center"><img src="media/gigaboss_skullmaw.gif" width="49%" alt="Skullmaw dans ses trois phases et un miroir qui s'allume">
<img src="media/gigaboss_dreadfang.gif" width="49%" alt="Dreadfang dans ses trois phases et une griffe de givre"><br>
<img src="media/gigaboss_heartbreaker.gif" width="49%" alt="Heartbreaker dans ses trois phases et l'onde de ses battements"></p>

| Gigaboss | Phases | Sur chaque joueur | Mécanisme collectif | Points faibles |
|---|---|---|---|---|
| **Skullmaw** (300 × 300 px, volant) | « Le regard qui traque », « Le voile psychique » (66 %), « Les ténèbres » (33 %, arène sombre) | **Regards traqueurs** : un laser par joueur, qui suit 1,5 s, se fige 0,45 s puis tire jusqu'au premier mur | **Voile** (phases 2-3, son armure) : son **Regard de mort** se charge 3,5 s puis frappe ceux qu'il voit (une colonne les cache) ; assez de **miroirs** tenus à cet instant (1 seul, 2 de 2 à 4 joueurs, 3 à 5-6, 4 à 7-8) : aveuglé 10 s, dégâts × 2 | ses deux yeux et le cristal du front |
| **Dreadfang** (320 × 240 px, volant) | « Les griffes », « Le bras enchaîné » (66 %), « Griffes déchaînées » (33 %, plateformes tranchées) | **Griffes du sol** ; **balayages** d'un, deux puis trois étages entiers (sauter la lame ou changer d'étage) | **Trois chaînes** (phase 2) : 2,5 s sur une ancre la verrouille 12 s ; les trois ensemble : plaqué au sol 10 s, dégâts × 2. **Griffe clouante** (phases 2-3, dès 2 joueurs) : un joueur (deux dès 5) cloué jusqu'à ce qu'un autre brise la griffe (un sauvetage) ; épargné par les griffes du sol pendant ce temps | ses trois jointures |
| **Heartbreaker** (340 × 320 px, immobile) | « La cuirasse », « La cage thoracique » (66 %), « Le cœur affolé » (33 %) | **Côtes-pièges** ; **battements** en rythme (un fort sur 4, 3 puis 2) dont l'onde court au sol | **Quatre plaques** (phase 1, les plaques de la branche reformées) : à briser dans les 20 s après la première (× 1,2 à 2 avec peu de joueurs), sinon elles repoussent ; **côtes à écarter** (phase 2) : un joueur sur chaque côte, ensemble (2,5 s, une côte lâchée se referme en 4 s) → cœur à nu 8 s, dégâts × 2,5 | ses plaques, puis la lumière du cœur |

* Badges `skullmaw_badge`, `dreadfang_badge`, `heartbreaker_badge` (succès `communaute_*`) dans `GIGABOSS_BADGES`, parties
  `crane`, `bras_gauche`, `torse` détruites à la chute validée (`GIGABOSS_OBJECTIVES`, `PART_EFFECTS`), sans migration.
* Contributions : une marque, un regard ou un flash évités comptent en dégâts évités (`Contributions::dodged`) ; un point
  faible ou une cible surveillée brisés comptent pour qui les a brisés (la liste des brisures survit au pas : correctif).
* `GigabossRoutine::spared(seat)` : un joueur tenu par la routine (avalé par Voidworm, cloué par Dreadfang) ne reçoit
  aucune marque des poings du cadre (correctif : l'avalé était frappé dans l'estomac). `targets()` : les flèches montrent
  aussi les plaques, les griffes clouantes et la poche de Voidworm.
* `exposedStep` rend l'étape « combat » à la fin d'une exposition (correctif : `setArmour` ne passe que de « combat » à
  « carapace », le gigaboss restait exposé pour de bon au lieu de reformer sa carapace, ses chaînes, ses côtes ou son voile).
* Sprites : `tools/assetgen/characters/boss_l_skullmaw.py`, `boss_l_dreadfang.py`, `boss_l_heartbreaker.py` (aides de
  dessin `boss_l_gigaboss.py` sur `gigaboss_kit.py`) ; feuilles d'effets 48 × 144 (`closed` / `open` / `warn` / `jet` des
  ancres et des côtes, `fist`, `blade`, `wave`, les miroirs).

## 14. Phase 5 (2.69) : les événements mondiaux (`server/src/raidWorld.ts`, migration **0147**)

Un événement mondial (`kind = 'world'`, programmé dans ADMINISTRATION › Raids) est **partagé par tous les clans
inscrits** : mêmes heures UTC (début, verrouillage des inscriptions, fin ; chaque jeu les montre à son heure), et un
**seul titan** : toutes les instances de tous les clans agissent sur le même `raid_branch_objectives` (un objectif compte
une fois pour le monde, au premier groupe qui l'a validé). Le reste est par clan, calculé par le serveur.

### Agrégation (validé seulement, idempotente)

`aggregate(db, event)` recalcule tout à partir de tables à clé, seulement ce que le consensus de chaque instance a
accepté (§3, §8) :

| Niveau | Ce qui compte |
|---|---|
| Monde | objectifs validés (bits du titan), zones contrôlées, gigaboss vaincus, **progression mondiale** = objectifs non facultatifs faits / total, **résultat** : « Asterion est tombé » (7 gigaboss vaincus) ou « a survécu » |
| Clan | points (somme des scores mixtes de ses joueurs, §9), joueurs validés / inscrits, groupes, objectifs validés par ses groupes (une fois par clan ; le second objectif d'une paire refusé au même groupe exclu), dont **en premier**, chutes de gigaboss |
| Groupe (instance) | points de ses tableaux, joueurs, objectifs validés, phase atteinte contre son gigaboss |
| Joueur | chaque catégorie (contribution, dégâts, évités, soutien, objectifs, temps, sauvetages, progression) : jamais les seuls dégâts |

Les mêmes rapports, acceptés dans n'importe quel ordre et autant de fois qu'on veut, donnent le même agrégat : objectif
par sa clé, tableau de contributions par `(instance, siège, pas)` remplacé seulement par un plus récent, combat par
instance. Deux instances qui valident le même objectif au même instant : compté une fois, et le doublon est **journalisé**
(`raid_world_journal`, « … à 40 ms d'écart : compté une fois »). Ce qui attend sa majorité (`pendingOf` : tableaux plus
récents que le dernier accepté, objectifs pas encore validés) est rendu **à part**, jamais dans un classement.

### Classements mondiaux — `GET /raids/events/{id}/world`

Monde (et chaque branche), résultat, tous les clans (100 premiers + le mien), groupes (60 + le mien), joueurs (100 + moi),
`pending`, mes rangs, les titres de mon clan, mes récompenses de l'événement. Avant la clôture : « Classement en cours » ;
après : le **résultat officiel figé** (« Résultat officiel · figé le … »). Écran `RaidWorldScene` (« Classement mondial »,
fiche d'un événement mondial) : progression, résultat, cinq chiffres, onglets Clans / Groupes / Individuel (catégories) /
Mon clan, bandeau « en attente de validation » à part, « Tout récupérer ». Clavier, manette, souris ; textes mesurés.

### Clôture — résultat officiel, récompenses, notifications

Le cron (chaque minute, `worldTick`) journalise le début de chaque événement mondial (une fois) et clôt ceux terminés
**depuis 10 min** (la grâce des derniers rapports, comme `raidZones.report`) — la première lecture du classement le fait
aussi. `finalizeEvent` : 1) **fige** l'agrégat dans `raid_event_results` (clé primaire : une fois ; un déclencheur SQL
refuse toute modification) ; 2) accorde les récompenses du classement final (`rewards_at`) ; 3) prévient chaque inscrit et
les chefs et officiers de chaque clan (« Résultats officiels de … : Asterion a survécu · [AUBE] 2ᵉ sur 23 clans »,
`notified_at` réservé avant l'envoi : jamais deux fois). Ensuite, tout rapport de l'événement est refusé (« résultats
officiels figés »). L'historique de CLANS › RAIDS montre le résultat officiel des événements mondiaux terminés.

### Récompenses exclusives (`raidWorldRewards.ts`, système de §9)

| Récompense | Objet | Condition |
|---|---|---|
| Combinaison « Écorce d'Asterion » | `suit_ecorce_asterion` | 5 min de combat validées dans l'**événement officiel** (dès l'acceptation, et à la clôture) |
| Titre de clan « Dévoreurs dévorés » | `title_devoreurs_devores` + `raid_clan_titles` | clan du top 10 officiel (points > 0) : ses joueurs validés |
| Combinaison « Écorce d'Asterion » argentée | `suit_ecorce_argent` | 5 min validées dans une **session de rattrapage** |

Une fois par joueur, récompense et événement (`raid_reward_grants`), jamais pour un raid de clan. Combinaisons générées par
`tools/assetgen/characters/nova_suits.py ecorce ecorce_argent`.

### Sessions de rattrapage (spec §16)

`POST /admin/raids/{id}/catchup {days}` (1 à 30, après la fin de l'événement officiel, une ouverte à la fois) : un
événement mondial de plus (`catchup_of`), ouvert tout de suite, inscriptions jusqu'à sa fin (l'inscription d'un membre
inscrit son clan), ses propres instances et résultats, la version argentée seulement, jamais de récompense de classement.
Le résultat officiel ne change jamais (autre événement ; l'officiel est figé).

### Arrivée en retard, suspension

* `late_minutes` (formulaire « Arrivée en retard ») : un nouveau joueur peut encore entrer en jeu jusqu'à N min après le
  début (`null` : jusqu'à la fin) ; un joueur déjà placé ou déjà dans une instance revient toujours (`requireEntry`).
* « Suspendre » (`POST …/suspend {reason}`) : plus aucune entrée en jeu, aucun rapport accepté (refusé et journalisé),
  pas de rappel ; « Reprendre » rend à l'événement le temps suspendu pendant qu'il tournait (6 h au plus). Les inscrits sont
  prévenus.

### Administration — « En direct » (maquette « Administration »)

`GET /admin/raids/{id}/live`, chiffres réels seulement : joueurs connectés (écrits par les relais), instances (zone,
joueurs, latence moyenne **mesurée par les jeux** et demandée aux relais en jeu, « — » sinon), gigaboss vaincus,
**reconnexions** (comptées par la base : déclencheur sur `raid_instance_members.active` 0 → 1), rapports refusés, en
attente, doublons ; le journal mêle les actions des administrateurs, les jalons et les anomalies (rapports refusés,
objectifs comptés une fois). « Valider la carte » (`POST …/validate-map`) vérifie les catalogues du titan (`checkMap` : 7
branches, 56 zones, une antre par branche, zones atteignables, liens valides ; combat encore à venir = avertissement) ; la
publication exige une carte valide. Chaque action va au Journal (`admin_raid_suspended`, `…_resumed`, `…_map_validated`,
`…_catchup`) et au journal de l'événement.

Migration **0147_raid_world.sql** : `raid_events.suspended_at / suspended_reason / late_minutes / catchup_of / map_check`,
`raid_instances.reconnects` (+ déclencheur), `raid_world_journal`, `raid_event_results` (+ déclencheur de gel),
`raid_clan_titles`.

## 15. Phase 7 (2.74) : fiabilité, charge et anti-triche

La phase 7 met tout le raid sous charge, sur un réseau dégradé et face aux tricheurs, avec des tests automatisés qui
tournent à chaque compilation (serveur : vitest dans Miniflare, avec le vrai Worker, D1 et le `CoopRoom` ; jeu :
`cosmic_tests`). Chaque problème trouvé a été corrigé, et le test qui l'a montré échoue avec l'ancien code.

### Ce que les tests ont trouvé

| Problème trouvé | Effet | Correction |
|---|---|---|
| Le dernier joueur d'une instance part (un joueur seul qui ferme son jeu, un groupe dont la connexion tombe) | le relais donnait des trames de veille **à personne**, aussi vite que possible, jusqu'à la limite de 2 h (864 000 pas × 8 sièges en mémoire : l'objet durable épuisait sa mémoire et son temps de calcul), puis oubliait la partie | **personne en jeu : les pas s'arrêtent** (`CoopRoom` et `CoopRelay` du réseau local) |
| Tous les liens d'une instance se ferment | la partie était oubliée à l'instant : aucun retour possible dans les 60 s | en mode raid, la partie **attend ses joueurs 60 s** (sièges libérés à l'horloge quand personne ne joue), puis se termine |
| Un jeu se reconnecte avant que le relais n'ait vu la coupure (lien mort sans fermeture : 3 s de silence) | refusé (« plus de 60 s d'absence ») | une **seconde connexion du même compte** (celui de la session) reprend sa place ; l'ancienne est fermée |
| L'objet durable redémarre (déploiement, éviction, panne) | toute l'instance perdue | la partie est **stockée chaque seconde** et revient au démarrage de l'objet (ci-dessous) |
| Un siège libéré pendant l'écriture D1 du relais | le joueur arrivé entre-temps perdait son identité (rapports refusés, contributions sans joueur) | le siège oublie son joueur tout de suite ; les écritures du relais en D1 partent dans l'ordre |
| Consensus : des membres **absents** envoient un rapport | leurs votes comptaient contre une majorité calculée sur les seuls joueurs actifs : trois complices absents l'emportaient sur deux joueurs en jeu | **seuls les votes des joueurs en jeu comptent** ; les autres attendent, puis sont journalisés s'ils sont minoritaires |
| Un rapport minoritaire croise l'acceptation | le menteur recevait « accepted » et n'était pas toujours journalisé | refusé et **journalisé une fois** |
| La clôture croise une acceptation | un rapport pouvait être accepté juste après le gel du résultat officiel | l'acceptation est une seule instruction conditionnelle : **figé ou suspendu, refusé** |
| Deux rapports de zone acceptés dans l'autre ordre (requêtes croisées sur le réseau) | le groupe revenait en arrière sur la carte | `raid_instances.zone_tick` : **le pas le plus récent gagne** |
| Résultat officiel figé : `DELETE` ou `INSERT OR REPLACE` | contournaient le déclencheur de gel (qui ne voyait que les `UPDATE`) | déclencheurs `raid_event_results_kept` et `raid_event_results_once` |
| Un objectif accepté dans un événement mondial | réveillait les relais de **toutes** les instances l'un après l'autre, dans la requête | tous ensemble, **après la réponse** (`waitUntil`) : la requête fait le même travail avec 2 ou 30 instances |
| La récompense de fin d'événement (cron, chaque minute) | lisait tous les combats de gigaboss jamais livrés | part des événements qui viennent de finir (index `raid_events_end`) |
| Le jeu perd sa liaison avec le relais | l'instance s'arrêtait (« Retour à la carte ») | **reconnexion automatique** pendant 60 s (ci-dessous) |

### Persistance : le relais d'une instance survit à son objet

* Le `CoopRoom` d'une instance stocke sa partie (stockage de l'objet durable) **chaque seconde** : l'historique des
  trames par parts de 4 s (480 pas, écrites une fois chacune, la dernière réécrite tant qu'elle se remplit ; trames
  compactes : un octet par joueur quand rien ne change), les sièges, les états de la branche donnés. Une écriture est
  atomique : les pas stockés correspondent toujours à l'état stocké.
* Au démarrage de l'objet (`blockConcurrencyWhile`), la partie revient : plus personne n'est connecté, chaque joueur est
  **absent à partir du dernier pas stocké**, écrit ainsi en D1 (le consensus ne compte plus ses votes), et revient comme
  tout retour (historique, rattrapage hors du fil principal, `REJOIN`) ; les reconnexions sont comptées par la base
  (déclencheur 0 → 1 de la migration 0147). Au pire, la dernière seconde jouée est perdue : le groupe la rejoue.
* Une partie dont tous les sièges sont libres et personne connecté est effacée (le salon reste : le prochain joueur
  lance une nouvelle partie de l'instance).
* D1 reste la vérité de la branche et du monde : objectifs par leur clé, tableaux par `(instance, siège, pas)` remplacés
  seulement par un plus récent, combats par instance (`MAX(phase)`), zone par le pas le plus récent. Les crons
  (`worldTick`, `grantEndedTops`) et la première lecture du classement peuvent clore un événement en même temps :
  résultat figé une fois, récompenses une fois, notifications une fois (testé avec trois crons et deux lectures à la
  fois). Le résultat officiel ne change plus : ni `UPDATE`, ni `DELETE`, ni `INSERT OR REPLACE` (seule la suppression
  de l'événement lui-même l'emporte).

### Réseau : le verrouillage à 8 sous latence

* **Relais** (`server/test/raidRelay.test.ts`, vraies WebSockets) : 8 jeux, gigue de 0 à 25 ms par message, un lot
  d'entrées sur trois envoyé avant le précédent : les 8 jeux reçoivent les mêmes trames, chaque entrée à son pas ; le
  dernier joueur part et revient dans les 60 s ; 60 s de pas libèrent le siège (trames `Vacant`) qu'un membre arrivé en
  retard prend (son `seat_from` = son pas de retour) ; celui qui a lancé l'instance part, les autres continuent ; une
  seconde connexion du même compte reprend sa place ; un neuvième joueur est refusé ; l'objet redémarre au milieu
  (3 parts stockées) et les deux joueurs retrouvent exactement l'historique.
* **Jeu** (`tests/test_raid_net.cpp`, réseau simulé, 73 s de jeu) : 8 jeux à 120 Hz derrière des liaisons de 25 ms
  (+ 0 à 15 ms de gigue) dans chaque sens, où un message sur 5 000 n'arrive que 200 ms plus tard en retenant toute sa
  liaison (une retransmission TCP), des entrées envoyées dans le désordre, une liaison coupée (ce qui était en route est
  perdu) puis rétablie 2 s plus tard, le lanceur qui part pour de bon, son siège libéré au bout de 60 s de pas et repris
  par un retardataire. Résultat : tous les jeux en jeu reçoivent exactement les mêmes trames (≈ 8 650 pas), chaque
  entrée tombe sur son pas et son siège, l'absence est en veille puis libre (`Vacant`) au bout de 7 200 pas ; le relais
  choisit un délai d'entrée de 12 images (100 ms), un jeu attend sa trame sur ≈ 5,5 % de ses images (les
  retransmissions) et le verrouillage suit l'horloge à plus de 90 %. Au-delà du délai maximal (15 images, 125 ms :
  liaisons de 40 + 30 ms), rien ne se désynchronise mais le jeu ralentit (≈ 72 % de l'horloge, mesuré pendant la mise au
  point) : c'est la limite du verrouillage par entrées.
* **Reconnexion automatique** (`game/raid/Reconnect`, `RaidGameScene`) : une liaison qui meurt sans `END` du relais
  (`NetCoopLink::connectionLost`, distinct d'un refus du relais) déclenche des essais tout de suite, puis 1, 2, 4 et 8 s
  après, puis toutes les 8 s, pendant les 60 s où le relais garde la place. Le monde reste affiché, figé, sous la carte
  « LIAISON PERDUE » (message mesuré, compte à rebours, « Retour à la carte de la branche » au clavier, à la manette et à
  la souris) ; chaque essai est un retour (nouveau monde, historique rejoué hors du fil principal, « Liaison rétablie :
  rattrapage de la partie (n %) ») ; après 60 s : « La liaison avec le relais n'a pas pu être rétablie en 60 s : votre
  place a été libérée… ». Un refus du relais (place reprise, instance pleine) est dit tel quel.

### Charge (Miniflare, `server/test/raidLoad.test.ts`)

Quatre clans de 50 joueurs (200 comptes) :

| Opération, toutes en même temps | Résultat | Durée mesurée |
|---|---|---|
| 200 inscriptions pour 180 places (50 par clan au plus) | exactement 180, aucune en double, aucun clan au-delà de 50 | ≈ 4,2 s |
| 180 placements sur le titan | jamais un neuvième dans une instance ; 27 instances | ≈ 8,0 s (avec les nouveaux essais « réessayez ») |
| 180 rapports d'objectif (chaque instance son premier objectif) | un objectif par branche pour le monde, un par instance pour son consensus, chaque doublon journalisé, aucun refus | ≈ 4,1 s (≈ 23 ms par rapport, 60 à la fois) |
| 180 rapports de tableau, pendant deux agrégations | 180 lignes de contributions, agrégat identique après avoir tout renvoyé dans un autre ordre | ≈ 3,9 s |
| 360 demandes de récompenses (deux par joueur) | chaque récompense réclamée une fois, aucun objet en double | ≈ 4,5 s |
| clôture : trois crons et deux lectures du classement | résultat figé une fois, notifications une fois, titres des 4 clans | ≈ 0,4 s |

Travail par requête (allers-retours D1, **identiques avec 2 et 30 instances**) : rapport en attente 10,
rapport accepté 16, rapport répété 9, agrégation 1 (un seul lot), classement mondial 10,
récupération 4, carte du titan 6, place 14, « En direct » 6, cron du monde
4, clôture 16. Le plan de chaque instruction de ces chemins est vérifié (`EXPLAIN QUERY PLAN`) :
aucune ne lit une table de raid entière.

À l'échelle d'un événement mondial (50 clans de 50 joueurs, 350 instances, 2 500 tableaux validés) : agrégation
≈ 60 ms (un seul lot), classement mondial ≈ 55 ms (11 allers-retours), clôture (gel, 2 500 combinaisons, 2 500 notifications) ≈ 0,4 s (44 allers-retours). Les
durées sont celles de Miniflare sur la machine de test (les objets durables y partagent le fil des requêtes) : des
ordres de grandeur, pas des garanties.

### Anti-triche (`server/test/raidCheat.test.ts`)

* **Identité** : le serveur ne connaît le joueur que par sa session : le relais reçoit l'identifiant du Worker (un en-tête
  `x-player-id` envoyé par le jeu est écrasé, le compte et le siège dits dans `HELLO` sont ignorés) ; un rapport qui dit
  un joueur ou un siège est refusé (400) ; un non-membre de l'instance, ou un membre dont le siège a été libéré : 403.
* **Consensus** : majorité stricte des joueurs en jeu, égalité jamais acceptée, un joueur une valeur par clé, menteurs
  minoritaires journalisés (une fois), complices absents sans voix, rapports rejoués ou dupliqués sans effet (20 fois
  le même à la fois : un rapport, un objectif).
* **Vraisemblance** avant toute majorité : objectif inconnu, d'une autre zone ou d'une zone verrouillée, gigaboss hors de
  son antre, pas plus loin que l'horloge depuis le début de l'instance, tableaux impossibles (dégâts, évents…) : refusés
  et journalisés pour chaque joueur.
* **Temps** : rien n'est accepté pendant une suspension ni après le gel, même au milieu d'une requête.
* **Débit** : 600 rapports par joueur et par 10 min (429 avec `retry-after`), 200 messages par seconde et par
  connexion au relais (au-delà, la connexion est fermée).
* **Limite connue** : une instance dont **plus de la moitié** des joueurs en jeu mentent ensemble peut faire accepter des
  faits *vraisemblables* (le serveur ne simule pas la partie) ; ils restent bornés par les règles ci-dessus, ne valent
  que pour son instance (un objectif compte une fois pour le monde) et chaque refus est journalisé pour les
  administrateurs (« En direct »).

### Performance (`tests/test_raid_net.cpp`, sans affichage)

Un pas de simulation de l'instance (8 joueurs, `RaidRun::step`), 1 800 pas (15 s de jeu, les joueurs avancent depuis
l'entrée de la zone ; chaque ennemi est simulé à chaque pas, sans tri par distance), dans les zones les plus lourdes,
build Release (GCC 13, Linux, 4 cœurs) :

| Zone | Ennemis | Moyenne | 1 % le plus lent | Pire pas |
|---|---|---|---|---|
| Bras droit, zone 7 (Poignet-verrou) | 192 | 0,10 ms | 0,37 ms | 4,1 ms (le premier : les ennemis apparaissent) |
| Torse, zone 7 (magma) | 143 | 0,09 ms | 0,15 ms | 4,2 ms (le premier) |
| Jambe gauche, zone 7 (magma) | 143 | 0,10 ms | 0,16 ms | 4,2 ms (le premier) |

Seuil enregistré dans le test : en moyenne **moins de la moitié** du budget d'un pas à 120 Hz (8,33 ms : l'autre moitié
reste pour l'affichage), et le 1 % le plus lent **sous le budget entier**.

### Les tests

| Fichier | Ce qu'il vérifie |
|---|---|
| `server/test/raidRelay.test.ts` | le relais d'une instance : 8 jeux sous gigue, départ et retour, siège libéré et repris, sans hôte, seconde connexion, identité, débit, redémarrage de l'objet |
| `server/test/raidLoad.test.ts` | 4 clans de 50 en même temps, travail borné par requête, plans des requêtes, échelle mondiale chronométrée |
| `server/test/raidCheat.test.ts` | consensus face aux tricheurs, courses rendues exactes (rapport minoritaire, clôture), ordre, rejeux, identité, débit, gel |
| `tests/test_raid_net.cpp` | réseau simulé à 8, relais sans personne en jeu, liaison perdue ou refusée, calendrier de reconnexion, performance |

Migration **0149_raid_reliability.sql** : `raid_instances.zone_tick`, déclencheurs `raid_event_results_kept` /
`raid_event_results_once`, index `raid_report_rejections_key`, `raid_reward_grants_event`, `raid_events_end`.
Le serveur est à déployer avec la migration (le jeu reste compatible avec un serveur qui ne l'a pas encore : il ne dépend
d'aucune nouvelle route ; sans le serveur 2.74, il retrouve seulement les anciennes limites du relais).

## 16. Fiches d'Asterion et de ses Gigaboss (2.73)

`ui/screens/RaidSheetsScene` (modèle pur `game/raid/RaidSheets`, testé par `tests/test_raid_sheets.cpp`) : la fiche
d'Asterion et celle de chacun des sept Gigaboss, **visibles par tous**, même sans les avoir jamais rencontrés (aucun
verrou, aucun « ??? »), et hors ligne : ce sont les données du jeu.

* **Accès** : CLANS › RAIDS › « Fiches d'Asterion » (au-dessus du titan de la carte de l'événement, et sous le message
  quand l'onglet n'est pas chargé), la fiche d'un événement › « Fiches », l'antre de chaque carte de branche › « Fiche
  de … ». Pastilles « NOUVEAU » : `raids.sheets` et `raids.sheet.<id>` (`data/news/new_features.json`). Clavier,
  manette et souris (liste des fiches, parties du titan, onglets, attaques, boutons).
* **Asterion** : le titan en calques (`AsterionFigure` ; une partie survolée ou sélectionnée montre son infobulle,
  activée ouvre la fiche de son Gigaboss), son histoire, la structure du raid (branches, zones, antres, joueurs par
  instance, durée estimée d'une branche : catalogues des branches), son **cœur** (ce qui en est connu :
  `asterion.heart`), les sept parties (Gigaboss, effet de leur destruction de `data/raids/asterion.json`, liens du
  titan de `titan.json`) et les récompenses (badges : succès « raid_reward » ; titres et combinaisons : cosmétiques
  « raid »: "asterion").
* **Chaque Gigaboss**, en trois onglets : « Combat » (sa description, ses phases avec leurs attaques et leurs
  mécanismes, son dernier souffle, puis la liste dépliable : chaque mécanisme de sa routine avec un schéma animé aux
  sprites du combat — `ui/screens/RaidSheetSketch`, planche d'effets `fx` de ses règles —, chaque attaque de sa
  définition avec son exemple joué par le moteur — `BossAttackPreview`, la même liste que Statistiques des boss ›
  Fiche, `ui/widgets/BossSheetWidgets` — et « Comment l'éviter »), « Mécanismes » (ce que le groupe fait ensemble :
  combien de joueurs, combien de temps, la table par nombre de joueurs en jeu quand les règles en ont une ; points
  faibles ; sa force selon le groupe ; ce qui compte dans les contributions), « Lieu et récompenses » (branche,
  expédition, antre, objectif, sa chute et ce qu'elle fait à Asterion, liens du titan, badge).
* **Pas de nombre inventé** : `data/raids/asterion_sheets.json` ne contient que les mots ; chaque nombre est un
  emplacement (`{0}`…) rempli depuis les règles de l'antre (`metadata.raid.gigaboss` de la zone 8, celles que lisent
  les routines), ou depuis les constantes des routines nommées dans `game/raid/GigabossTunables.h` (`slack`,
  `plateSlack`, griffe clouante, gueule de Voidworm : mêmes valeurs, aucune règle changée). Les phases d'un mécanisme
  se lisent aussi dans les règles (`range:carapace`, `from:pin.from`, `nonzero:swallow.periods`…). Le test vérifie
  que tout se résout, que chaque phase a ses attaques et mécanismes, que chaque type d'attaque dit comment l'éviter et
  que les panneaux fixes tiennent leurs textes à 1280 × 720 et 1920 × 1080, en français et en anglais.
* **Ajouter une fiche par les données** (le cœur, phase 6) : une entrée de `gigaboss` avec sa partie (`"part":
  "coeur"`), son ennemi (`enemy`) et son antre (`lair`: `{"part": <dossier de branche>, "zone": <zone>}`) ; la fiche
  d'Asterion propose alors « Sa fiche » sur le cœur, et sa partie s'ouvre depuis le titan.
* **Capture sur demande** : `CosmicPlatformer --headless --raid-sheets <fiche>[:<onglet 0-2>][:attaque=<id>|:attaque=m:<mécanisme>]`
  (au-dessus du menu, l'attaque dépliée avec son exemple) ; `--clans --online-open raids` ouvre l'onglet RAIDS.
---

[← Retour à l'accueil (README)](../README.md)
