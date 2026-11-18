# Raids de clan « ASTERION » — phases 2 et 3 : la branche « BRAS DROIT » et son gigaboss PYROCLAST (2.65 – 2.67)

La phase 1 (événements, inscriptions, groupes de 8, rappels : `server/src/raids.ts`, migration 0141)
prépare le raid. La phase 2 le rend **jouable** sur une première branche du titan, le **Bras droit** :
8 sous-zones, des instances de 8 joueurs au plus en verrouillage par entrées, et un état de la
branche partagé par tous les groupes, que le serveur ne met à jour qu'avec le **consensus** des
joueurs de chaque instance. La phase 3 (2.67) ouvre la Main : le gigaboss **Pyroclast** (§6 à §9).

## 1. La branche (maquette « Branche »)

| Zone | Nom | Ce qu'on y fait | Passages |
|---|---|---|---|
| 1 | Épaule | entrée, **3 points d'apparition** ; reprendre l'arène (2 vagues) | 2, 3 |
| 2 | Biceps de basalte | plateformes qui « respirent » ; abattre le golem gardien | 1, 4, 6 |
| 3 | Veine de magma | terrasses à remonter, **coulée de magma** toutes les 60 s ; générateur ouest ; passage caché vers 5 ; raccourci (porte) vers 6 | 1, 4, 5, 6⇄ |
| 4 | Coude-forge | **deux soufflets, deux groupes** (un chacun) ; coulée toutes les 90 s ; l'Enclume vivante (facultatif) | 2, 3, 7🔒 |
| 5 | Grotte des scories | **zone secrète** ; la relique (facultatif) | 3 |
| 6 | Avant-bras | générateur est ; **levier du raccourci** vers la Veine (facultatif) | 2, 3⇄, 8 |
| 7 | Poignet-verrou | ouvert par les **2 générateurs** ; 2.67 : **la longue route** (§10) dont la dernière embuscade ouvre la Main | 4, 8 |
| 8 | Main de Pyroclast | le gigaboss **Pyroclast** (§6) ; objectif `main_pyroclast` | 6, 7 |

* Deux chemins vers le Coude-forge (Biceps / Veine), puis le Poignet-verrou et la Main ; 2.67 : la Main ne s'ouvre
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
partir de `first`, annoncée `warn` s avant), `gigaboss` (2.67, §6 : l'objet du boss, ses évents en pixels, les
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

## 5. Limites de la phase 2

* (2.67 : la Main est ouverte, voir §6.)
* Les replays d'une instance ne sont pas encore conservés côté serveur (le relais garde les trames
  en mémoire, comme la coopération).
* Les soufflets demandent deux groupes, sans la contrainte « à 3 s près » de la maquette : deux
  instances ne partagent pas d'horloge commune assez précise.
* Le chat de groupe (« GROUPE · Lyra : … » de la maquette) n'est pas dans cette phase : les
  alertes viennent du jeu et du serveur seulement.

## 6. Phase 3 (2.67) : le gigaboss PYROCLAST

`data/enemies/pyroclast.json` : une main de magma qui marche sur ses doigts (le cerveau du golem : poing roulant
= `shell`, poing météore = `skyfall`, secousse = `quake`), trois phases et une dernière éruption :

| Phase | Seuil | Attaques | L'arène (événements `bossPhase` de la zone) |
|---|---|---|---|
| 1 « Poing de magma » | 100 % | coup de poing, poing roulant, pluie de magma, éruption, bombes de lave | — |
| 2 « Carapace de basalte » | 66 % | poing météore, secousse, pluie, poing roulant, éruption | la croûte du centre cède au magma, deux dalles mobiles, soins |
| 3 « Cœur de magma » | 33 % | marée de magma, double poing météore, secousse, éruption folle… | rideaux de lave aux deux murs, soins |

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

## 7. Contributions et consensus (`game/raid/Contributions`, `raidZones.ts`)

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

## 8. Corps d'Asterion, résultats et récompenses (`raidRewards.ts`)

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

## 9. Écrans (2.67) et test des administrateurs

* **Combat** : les évents dessinés sur l'arène (fermé, ouvert, annoncé, jet ; la tenue en anneau), la carapace,
  les objectifs du gigaboss (évents k / 3, cœur à nu et ses points faibles, allié à relever), mes contributions en
  bas à droite, la carte de sa chute (« Résultats du raid »).
* **Résultats du raid** (maquette « Résultats ») : depuis la fiche de l'événement et à la chute de Pyroclast.
* **CLANS › RAIDS** : « Récompenses à récupérer » avec « Récupérer ».
* **ADMINISTRATION › Raids › « Tester Asterion »** (administrateurs) : une branche du catalogue
  (`raid::Branch::all`, les branches ajoutées plus tard y apparaissent seules), une zone (le combat de Pyroclast par
  défaut), « Invincible (admin) · non classé », 2 à 8 joueurs simulés, phase de départ ; seul et en local
  (`RaidRun::setTest`) : aucun rapport n'est jamais produit, et le serveur refuse tout rapport marqué `test`.

## 10. La longue route du Poignet-verrou (2.67)

La zone 7 est un vrai niveau d'environ **15 minutes pour 8 joueurs** : des galeries à deux voies (sol et grilles) pour
que huit joueurs ne s'empilent pas, **quatre embuscades géantes sur plusieurs étages** (la Tour des tendons, la
Galerie des forges, la Fosse des scories, la Garde du verrou : des vagues sur chaque étage, les portes fermées
pendant le combat ; l'état, le compteur et les flèches violettes des embuscades du jeu), des **mécanismes de groupe**
(`groupSwitches` : la vanne double, 4 joueurs sur sa plaque ; le monte-charge, 3 joueurs dessus ; les plaques
jumelles, 2 joueurs sur chacune en même temps ; le besoin suit les sièges occupés, seul la première plaque suffit),
des **points de contrôle** (le groupe entier à terre reprend au dernier atteint) et des réanimations.

L'estimation (`estimate` du générateur, recalculée par `tests/test_raid.cpp` depuis le niveau lui-même) :
parcours à l'allure du groupe (70 px/s) + 3 s par tuile grimpée + pour chaque vague 12 s (elle apparaît, le groupe
se répartit) + sa vie ÷ (8 × 0,6 de dégâts par seconde) + 45 s par mécanisme de groupe : **≈ 14,5 min** (13 à 17
exigés). Les autres zones restent courtes (le raid standard dure 45 à 60 min).

## 11. La caméra des arènes de gigaboss (`game/raid/ArenaCamera`, affichage seulement)

En entrant dans l'arène : un plan de **l'arène entière** et du boss (4 s), puis une vue qui suit le joueur de cet
ordinateur avec un zoom entre 300 et 620 px de monde en hauteur : assez large pour les marques qui le menacent, le
boss quand il est proche et les alliés voisins, jamais au point de rendre l'action minuscule. Des flèches au bord de
l'écran (hors du HUD mesuré) montrent le boss (ses points faibles quand il est à nu), les alliés (leurs couleurs) et
les ennemis d'une embuscade. Elle sert à tous les gigaboss (`GigabossRules::arena`). La simulation ne la voit jamais.
