#!/usr/bin/env python3
"""Builds the branch « BRAS DROIT » of the clan raid « Asterion » (phase 2, docs/RAIDS.md).

Eight sub-zones on the titan's right arm, each a real level (data/raids/asterion/bras_droit/
zone_N.json), and the branch catalog (branch.json) the game and the server share (a copy goes to
server/src/raidBranches.json: the server never trusts the game's).

The graph (maquette « Branche »): Épaule (1) → Biceps de basalte (2) and Veine de magma (3), two
paths that meet at the Coude-forge (4, two groups: one bellows each). The Veine hides the secret
Grotte des scories (5) and the shortcut to the Avant-bras (6), opened by the lever of the
Avant-bras. The Poignet-verrou (7) opens once the two generators (Veine and Avant-bras) are on;
2.68 (phase 3): it is the long level before the gigaboss (about 15 minutes for 8 players: wide
lanes, co-op plates and a goods lift, checkpoints, four giant ambushes on several storeys; its
time is raid_asterion.estimate_minutes, the model of the six other branches), whose last ambush
opens the way to the Main de Pyroclast (8, from the Avant-bras too once it is broken): the arena
of the gigaboss
PYROCLAST (data/enemies/pyroclast.json, its fight run by game/raid/Gigaboss), its three vents to
close together while its carapace protects it, its floor of basalt that gives way to the magma.

Each zone's metadata.raid holds its spawn points (by the zone one comes from), its passages to its
neighbours (rect in pixels, lock), its objectives (an id of the catalog, the object and the event
that completes it), its magma flow (a damage zone switched on every period, announced before; 2.71:
style « magma », the rising magma drawn over the players and the enemies from the generated sheet of
tools/assetgen/environment/raid_visuals.py, rising from the bottom of its box and burning every enemy
caught in it like the players; three zones rise, more often) and,
for the Main, its arena (metadata.raid.arena: rect, floor, door, entry; the format of the seven
branches' lairs) and its gigaboss (the boss's object, its vents in pixels, its carapace's phases,
its weak points and the numbers of its fight); for the Poignet-verrou, its co-op plates
(metadata.raid.coop) and its checkpoints (metadata.raid.checkpoints, arrivals spawns["cp:<id>"]),
in the format of raid_asterion.py. Never edit the JSON by hand: run
    python3 tools/levelgen/raid_bras_droit.py
from the repository root (the output is stable: same bytes each run).
"""
import json
import os

from levelkit import Level, ROOT, T
from pyralis_common import Pyra, FORGE_GLOW, LAVA_GLOW
from raid_asterion import estimate_minutes

BRANCH_ID = "asterion_bras_droit"
# The revision of the branch: every game of an instance must have the same (the relay's GO
# carries it). Raise it whenever a zone or the catalog changes how the branch plays.
REVISION = 3  # 2.71: the rising magma (style « magma »), more often, in the Avant-bras too
# 2 (2.68, phase 3): the Main de Pyroclast is the gigaboss's arena, the Poignet-verrou its long way
OUT = os.path.join(ROOT, "data", "raids", "asterion", "bras_droit")
SERVER_COPY = os.path.join(ROOT, "server", "src", "raidBranches.json")
H, LAVA = 48, 44
START_POWERS = ["dash", "wave", "shield", "impulse"]

ZONES = [
    # id, name, subtitle (maquette), map position (maquette px), what is inside, magma period
    (1, "Épaule", "entrée · 3 points d'apparition", 110, 360,
     "Ennemis : acariens de braise, rôdeurs de lave · arène de la garde", ""),
    (2, "Biceps de basalte", "plateformes qui respirent", 330, 200,
     "Ennemis : golem de magma (gardien), acariens de braise", ""),
    (3, "Veine de magma", "courant de lave à remonter", 330, 520,
     "Ennemis : rôdeurs de lave, feux follets · générateur ouest", "Montée de magma toutes les 34 s (terrasses hautes sûres)"),
    (4, "Coude-forge", "2 soufflets à détruire", 560, 300,
     "Ennemis : forgerons de lave, drones-scories · mini-boss : l'Enclume vivante",
     "Montée de magma toutes les 42 s (grilles hautes sûres)"),
    (5, "Grotte des scories", "zone secrète ◆ relique", 560, 620,
     "Ennemis : gloops de magma, acariens de braise", ""),
    (6, "Avant-bras", "raccourci depuis la Veine", 790, 160,
     "Ennemis : drones de forge, ailes de cendre · générateur est", "Montée de magma toutes les 38 s (corniches sûres)"),
    (7, "Poignet-verrou", "s'ouvre : 2 générateurs · la longue route", 790, 470,
     "Quatre embuscades géantes sur plusieurs étages, plaques et monte-charge de groupe, points de contrôle",
     "Environ 15 min à 8 joueurs"),
    (8, "Main de Pyroclast", "GIGABOSS · Pyroclast", 1000, 320,
     "Le gigaboss Pyroclast : trois phases, sa carapace, ses trois évents à fermer ensemble",
     "Carapace : ses 3 évents à tenir 3 s, ensemble"),
]
NAMES = {z[0]: z[1] for z in ZONES}

# The objectives of the branch, in a FIXED order: the index is the bit of the branch state
# (game and server). Never reorder; add at the end.
OBJECTIVES = [
    # id, zone, label, optional, role, pair
    ("epaule_arene", 1, "Reprendre l'épaule : vaincre la garde de l'arène", False, "", ""),
    ("biceps_gardien", 2, "Abattre le golem gardien du biceps", False, "", ""),
    ("veine_generateur", 3, "Activer le générateur de la Veine (un pouvoir de tir)", False, "generator", ""),
    ("forge_soufflet_ouest", 4, "Détruire le soufflet ouest (un groupe)", False, "", "soufflets"),
    ("forge_soufflet_est", 4, "Détruire le soufflet est (un autre groupe)", False, "", "soufflets"),
    ("forge_enclume", 4, "Facultatif : vaincre l'Enclume vivante", True, "", ""),
    ("grotte_relique", 5, "Facultatif : emporter la relique des scories", True, "", ""),
    ("avantbras_generateur", 6, "Activer le générateur de l'avant-bras (un pouvoir de tir)", False, "generator", ""),
    ("avantbras_raccourci", 6, "Facultatif : ouvrir le raccourci vers la Veine (levier)", True, "shortcut", ""),
    ("poignet_verrou", 7, "Briser le verrou du poignet : vaincre sa garde", False, "", ""),
    # 2.68 (phase 3): the gigaboss of the branch; accepted once, it destroys the right arm.
    ("main_pyroclast", 8, "Vaincre le gigaboss Pyroclast", False, "gigaboss", ""),
]

EDGES = [
    # a, b, lock ("", "generators", "shortcut", "objective:<id>"), secret
    (1, 2, "", False), (1, 3, "", False), (2, 4, "", False), (3, 4, "", False), (3, 5, "", True),
    (2, 6, "", False), (3, 6, "shortcut", False), (4, 7, "generators", False),
    # 2.68: the Main only once the wrist is broken (its long level is the way to the gigaboss).
    (6, 8, "objective:poignet_verrou", False),
    (7, 8, "objective:poignet_verrou", False),
]


def lock_of(a, b):
    for x, y, lock, _ in EDGES:
        if {x, y} == {a, b}:
            return lock
    raise ValueError("no edge %d-%d" % (a, b))


class Zone:
    """One sub-zone: a Pyralis level plus its raid metadata."""

    def __init__(self, n, W, description):
        self.n = n
        self.L = Level("raid_bd_%d" % n, "Asterion · Bras droit · " + NAMES[n], W, H)
        self.P = Pyra(self.L, LAVA, seed=4200 + n)
        self.P.base(W, H, description, difficulty=4, start_powers=START_POWERS, limit=3600, ref=600,
                    medals=(None, 3000, 2400, 1800))
        d = self.L.data
        d["timer"] = {"mode": "unlimited", "limit": 3600, "onExpire": "continue", "referenceTime": 600,
                      "challengeMaxDeaths": 0}
        d["maxPowers"] = 4
        self.raid = {"branch": BRANCH_ID, "zone": n, "spawns": {}, "passages": [], "objectives": [], "magma": None,
                     "sealed": None}
        d["metadata"] = {"generator": "tools/levelgen/raid_bras_droit.py", "raid": self.raid}

    def rock(self, x0, top, x1):
        return self.P.rock(x0, top, x1)

    def start(self, points):
        """The default spawn points (feet, tiles): the level's start is the first."""
        self.L.start(points[0][0], points[0][1])
        self.raid["spawns"]["start"] = [[x * T, y * T] for x, y in points]

    def arrive(self, src, x, y):
        self.raid["spawns"][str(src)] = [[x * T, y * T]]

    def passage(self, to, x0, y_floor, door=None):
        """A way to zone `to`: 4 tiles wide, 4 high, standing on y_floor; a sign and a light."""
        rect = [x0 * T, (y_floor - 4) * T, 4 * T, 4 * T]
        p = {"id": "p%d_%d" % (self.n, to), "to": to, "rect": rect, "lock": lock_of(self.n, to), "door": door or ""}
        self.raid["passages"].append(p)
        self.L.sign(x0 + 2, y_floor, "Passage vers : %s" % NAMES[to])
        self.L.light(x0 + 2, y_floor - 2, "#9fd8ff", 70, flicker=0.05)
        self.L.emitter(x0, y_floor - 4, x0 + 4, y_floor, "fireflies", 1.2)
        return p

    def objective(self, oid, source, trigger):
        assert any(o[0] == oid and o[1] == self.n for o in OBJECTIVES), oid
        self.raid["objectives"].append({"id": oid, "source": source, "trigger": trigger})

    def magma(self, x0, x1, y0, y1, period, first, warn=4.0, duration=8.0):
        """2.71: the rising magma (style « magma »: it rises from the bottom of its box in `rise` s,
        drains in `fall` s, hurts the players and burns the enemies caught in it, drawn over them),
        announced `warn` s before each rise."""
        dz = self.L.obj("damage_zone", [x0 * T, y0 * T, (x1 - x0) * T, (y1 - y0) * T], "gameplay",
                        {"damage": 2, "interval": 0.6, "visible": True, "active": False, "style": "magma",
                         "rise": 1.6, "fall": 1.4, "bounce": True}, oid="magma_flow")
        self.raid["magma"] = {"zone": dz, "period": period, "first": first, "warn": warn, "duration": duration}
        return dz

    def coop(self, cid, x0, x1, y, players, targets, action, label, message, group=""):
        """2.68 (phase 3): a co-op plate (metadata.raid.coop, the format of raid_asterion.py): from
        x0 to x1 standing on y (tiles), `players` together on it (fewer when fewer are in play:
        game/raid/RaidRun) send `action` to each of `targets` (« open » a door, « activate » a lift).
        The plates of one `group` must be held at the same time (the players in play shared)."""
        c = {"id": cid, "rect": [x0 * T, (y - 4) * T, (x1 - x0) * T, 4 * T], "players": players, "targets": targets,
             "action": action, "label": label, "message": message}
        if group:
            c["group"] = group
        self.raid.setdefault("coop", []).append(c)
        return cid

    def checkpoint(self, x, y, label):
        """2.68: a checkpoint of the raid (the group starts again at the last one touched, the
        players down get up there)."""
        cp = self.L.checkpoint(x, y, label)
        self.raid.setdefault("checkpoints", []).append(cp)
        self.raid["spawns"]["cp:" + cp] = [[x * T, y * T]]
        return cp

    def save(self):
        ids = [o["id"] for o in self.L.data["objects"]]
        assert len(ids) == len(set(ids)), "duplicate ids"
        for o in self.raid["objectives"]:
            assert o["source"] in ids, o
        os.makedirs(OUT, exist_ok=True)
        path = os.path.join(OUT, "zone_%d.json" % self.n)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self.L.data, f, ensure_ascii=False, indent=1)
            f.write("\n")
        print("wrote %s: %d objects" % (os.path.relpath(path, ROOT), len(ids)))


# ======================================================================== 1 — Épaule
def zone_1():
    z = Zone(1, 120, "L'épaule du titan, porte du bras droit. Trois points d'apparition, une garde de lave à "
                     "disperser, puis deux chemins : le Biceps en hauteur, la Veine en contrebas.")
    L, P = z.L, z.P
    z.rock(0, 36, 34)
    z.start([(8, 36), (14, 36), (20, 36)])
    L.sign(26, 36, "Asterion, bras droit. Chaque zone a ses objectifs ; la carte de la branche montre où sont les groupes.")
    P.brazier(4, 36)
    P.scatter(2, 32, 36, 0.35)
    z.rock(34, 38, 40)
    z.rock(43, 38, 86)
    P.scatter(44, 85, 38, 0.3, forge=True)
    w1 = [L.enemy("ember_mite", 52, 38, 3, dormant=True), L.enemy("ember_mite", 60, 38, 3, True, dormant=True),
          L.enemy("lava_stalker", 72, 38, 5, True, dormant=True)]
    w2 = [L.enemy("forge_drone", 56, 30, 4, dormant=True), L.enemy("lava_stalker", 66, 38, 5, dormant=True),
          L.enemy("ember_mite", 78, 38, 3, True, dormant=True)]
    arena = L.combat(46, 24, 84, 38, waves=";".join(",".join(w) for w in (w1, w2)), lock=False)
    L.oneway(54, 60, 33, "basalt_ledge")
    L.oneway(70, 76, 33, "basalt_ledge")
    z.objective("epaule_arene", arena, "cleared")
    L.event(arena, "cleared", [{"type": "hint", "params": {"text": "L'épaule est à nous ! Biceps en haut, Veine en bas.",
                                                           "duration": 3.5}}], delay=0.6)
    z.rock(86, 35, 120)
    L.solid(106, 25, 120, 27, "basalt")
    L.oneway(92, 98, 32, "basalt_ledge")
    L.oneway(99, 104, 29, "basalt_ledge")
    L.oneway(100, 105, 26, "basalt_ledge")
    z.passage(2, 115, 25)
    z.passage(3, 115, 35)
    z.arrive(2, 109, 25)
    z.arrive(3, 108, 35)
    L.orb("health_orb", 90, 33)
    L.light(65, 30, FORGE_GLOW, 120, flicker=0.2)
    z.save()


# ============================================================ 2 — Biceps de basalte
def zone_2():
    z = Zone(2, 130, "Le biceps du titan se gonfle et se relâche : ses plateformes de basalte respirent au-dessus "
                     "de la lave. Un golem garde le muscle.")
    L, P = z.L, z.P
    z.rock(0, 34, 14)
    z.passage(1, 1, 34)
    z.arrive(1, 8, 34)
    z.start([(8, 34), (11, 34)])
    L.sign(12, 34, "Les plateformes montent et descendent avec la respiration du titan : sautez au bon moment.")
    for i, x in enumerate(range(17, 57, 7)):
        L.moving(x, 34, 4, 0, -3, speed=26, wait=1.0, phase=(i % 2) * 0.5)
    z.rock(59, 34, 100)
    P.scatter(60, 99, 34, 0.3)
    golem = L.enemy("magma_golem", 82, 34, 8, True)
    z.objective("biceps_gardien", golem, "killed")
    L.enemy("ember_mite", 66, 34, 3)
    L.enemy("ember_mite", 94, 34, 3, True)
    L.orb("health_orb", 62, 32)
    z.rock(100, 34, 130)
    L.oneway(88, 93, 31, "basalt_ledge")
    L.oneway(95, 100, 28, "basalt_ledge")
    L.oneway(102, 107, 25, "basalt_ledge")
    L.solid(110, 22, 130, 24, "basalt")
    z.passage(6, 124, 22)
    z.passage(4, 124, 34)
    z.arrive(6, 116, 22)
    z.arrive(4, 117, 34)
    L.light(80, 28, LAVA_GLOW, 110, flicker=0.15)
    z.save()


# ================================================================ 3 — Veine de magma
def zone_3():
    z = Zone(3, 140, "Une veine de magma remonte le bras : ses terrasses grimpent vers le coude pendant que la "
                     "lave déborde. Un générateur alimente le verrou du poignet ; un passage caché mène à une grotte.")
    L, P = z.L, z.P
    z.rock(0, 36, 14)
    z.passage(1, 1, 36)
    z.arrive(1, 8, 36)
    z.start([(8, 36), (11, 36)])
    z.rock(14, 38, 30)
    L.liquid(30, 34, 41, LAVA, "lava")
    z.rock(34, 35, 50)
    z.rock(50, 32, 66)
    z.rock(66, 29, 82)
    z.rock(82, 26, 140)
    L.sign(20, 38, "La veine déborde : quand la lave monte, grimpez sur les terrasses hautes.")
    z.magma(14, 50, 32, 38, period=34.0, first=16.0, duration=7.0)
    for x in (24, 42, 58, 74):
        P.lavafall(x, 4, 12, hazard=False, alpha=0.5)
    L.enemy("lava_stalker", 44, 35, 5)
    L.enemy("lava_stalker", 72, 29, 5, True)
    L.enemy("fire_wisp", 60, 24, 4)
    gen = L.receptor(92, 26, "")
    z.objective("veine_generateur", gen, "activate")
    L.sign(88, 26, "Générateur du verrou : frappez-le avec un pouvoir de tir (Onde, Tir chargé, Boomerang).")
    L.light(92, 23, "#39f0d8", 90, flicker=0.1)
    # The secret way to the Grotte des scories (an island high on the left).
    L.oneway(37, 42, 32, "basalt_ledge")
    L.oneway(31, 36, 29, "basalt_ledge")
    L.solid(20, 26, 30, 28, "basalt")
    L.secret(18, 18, 32, 28, "Un passage dans la roche : la Grotte des scories !")
    z.passage(5, 21, 26)
    z.arrive(5, 27, 26)
    # The shortcut to the Avant-bras (a door its lever opens) on a high ledge at the right.
    L.oneway(124, 129, 22, "basalt_ledge")
    L.solid(104, 18, 122, 20, "basalt")
    door = L.door(110, 14, 4)
    z.passage(6, 105, 18, door=door)
    z.arrive(6, 116, 18)
    z.passage(4, 134, 26)
    z.arrive(4, 129, 26)
    L.orb("health_orb", 86, 24)
    z.save()


# =================================================================== 4 — Coude-forge
def zone_4():
    z = Zone(4, 150, "Une articulation de basalte où le titan fond le métal des mondes avalés. Deux soufflets "
                     "attisent la forge : il faut deux groupes, un par soufflet. La coulée de magma revient sans cesse.")
    L, P = z.L, z.P
    z.rock(0, 36, 20)
    z.passage(3, 1, 36)
    z.arrive(3, 9, 36)
    L.solid(0, 24, 22, 26, "basalt")
    z.passage(2, 1, 24)
    z.arrive(2, 9, 24)
    z.start([(9, 36), (12, 36)])
    L.oneway(24, 29, 33, "basalt_ledge")
    L.oneway(25, 30, 30, "basalt_ledge")
    L.oneway(23, 27, 27, "basalt_ledge")
    z.rock(20, 36, 130)
    P.scatter(22, 128, 36, 0.25, forge=True)
    z.magma(22, 128, 33, 36, period=42.0, first=20.0)
    for x0 in (34, 54, 74, 94, 114):
        L.oneway(x0, x0 + 8, 30, "chain_grate")
    L.sign(30, 36, "Coude-forge : deux soufflets, deux groupes. Chaque groupe en détruit un (un pouvoir de tir).")
    west = L.receptor(46, 36, "")
    east = L.receptor(106, 36, "")
    z.objective("forge_soufflet_ouest", west, "activate")
    z.objective("forge_soufflet_est", east, "activate")
    L.decor("forge_anvil", 44, 36)
    L.decor("forge_anvil", 108, 36, flip=True)
    P.brazier(50, 36)
    P.brazier(102, 36)
    anvil = L.enemy("forge_bulwark", 76, 36, 6, True)
    z.objective("forge_enclume", anvil, "killed")
    L.enemy("lava_stalker", 40, 36, 5)
    L.enemy("lava_stalker", 66, 36, 5, True)
    L.enemy("lava_stalker", 90, 36, 5)
    L.enemy("forge_drone", 60, 26, 4)
    L.enemy("forge_drone", 100, 26, 4, True)
    L.orb("health_orb", 78, 28)
    z.rock(130, 36, 150)
    door = L.door(137, 32, 4)
    z.passage(7, 141, 36, door=door)
    z.arrive(7, 133, 36)
    L.sign(134, 36, "Poignet-verrou : il s'ouvre quand les deux générateurs (Veine, Avant-bras) tournent.")
    L.light(75, 30, FORGE_GLOW, 140, flicker=0.25)
    z.save()


# ============================================================ 5 — Grotte des scories
def zone_5():
    z = Zone(5, 72, "Une grotte secrète sous la Veine, pleine de scories. Une relique y attend qui ose la chercher.")
    L, P = z.L, z.P
    L.solid(0, 0, 72, 6, "basalt")
    z.rock(0, 34, 12)
    z.passage(3, 1, 34)
    z.arrive(3, 8, 34)
    z.start([(8, 34)])
    z.rock(12, 36, 30)
    z.rock(34, 33, 50)
    L.crumble(52, 33, 3)
    L.crumble(57, 33, 3)
    z.rock(62, 30, 72)
    relic = L.relic(67, 27, "Relique des scories")
    z.objective("grotte_relique", relic, "collected")
    L.enemy("magma_gloop", 22, 36, 4)
    L.enemy("ember_mite", 42, 33, 3, True)
    P.scatter(13, 29, 36, 0.4)
    L.light(67, 26, "#c9a8ff", 90, flicker=0.2)
    L.emitter(0, 6, 72, 20, "ash_fall", 1.5)
    z.save()


# ==================================================================== 6 — Avant-bras
def zone_6():
    z = Zone(6, 130, "L'avant-bras, couvert de câbles de forge. Son générateur alimente le verrou du poignet ; son "
                     "levier ouvre le raccourci vers la Veine. Au bout : la Main.")
    L, P = z.L, z.P
    z.rock(0, 30, 16)
    z.passage(2, 1, 30)
    z.arrive(2, 9, 30)
    z.start([(9, 30), (12, 30)])
    z.rock(16, 36, 40)
    door = L.door(24, 32, 4)
    z.passage(3, 18, 36, door=door)
    z.arrive(3, 33, 36)
    lever = L.lever(30, 36, door)
    z.objective("avantbras_raccourci", lever, "activate")
    L.sign(34, 36, "Raccourci vers la Veine : actionnez le levier pour l'ouvrir à tous les groupes.")
    L.oneway(34, 39, 33, "basalt_ledge")
    z.rock(40, 30, 90)
    gen = L.receptor(64, 30, "")
    z.objective("avantbras_generateur", gen, "activate")
    L.sign(60, 30, "Générateur du verrou : frappez-le avec un pouvoir de tir (Onde, Tir chargé, Boomerang).")
    L.enemy("forge_drone", 56, 24, 4)
    L.enemy("lava_stalker", 76, 30, 5, True)
    L.enemy("ash_wing", 84, 22, 4)
    P.scatter(41, 89, 30, 0.3, forge=True)
    # 2.71: the magma rises along the forearm too (its ledges stay above it).
    z.magma(42, 88, 27, 30, period=38.0, first=18.0, duration=6.0)
    for x0 in (44, 54, 70, 80):
        L.oneway(x0, x0 + 6, 25, "basalt_ledge")
    L.sign(42, 30, "Le magma monte dans l'avant-bras : à l'alerte, sautez sur les corniches.")
    z.rock(90, 27, 130)
    z.passage(8, 124, 27)
    z.arrive(8, 117, 27)
    L.orb("health_orb", 92, 25)
    L.light(64, 26, "#39f0d8", 90, flicker=0.1)
    z.save()


# ================================================================ 7 — Poignet-verrou
# 2.68 (phase 3): the long level before the gigaboss (about 15 minutes for 8 players, estimated by
# raid_asterion.estimate_minutes like the six other branches' expeditions): lanes wide enough for 8,
# co-op plates and a goods lift, checkpoints, four giant ambushes on several storeys (state kept
# when someone falls, counter and violet arrows: the game's ambushes).


def enemy_hp(kind):
    with open(os.path.join(ROOT, "data", "enemies", kind + ".json"), encoding="utf-8") as f:
        return float(json.load(f)["hp"])


def tower(z, x0, x1, floors, waves, name):
    """A giant ambush on several storeys: `floors` the rows from the ground up (grates the whole
    width, a gap at alternate ends, a ladder in each), `waves` lists of (kind, storey, x) placed
    on every storey; the doors at both ends close while it lasts. Returns (arena, the health of
    each wave)."""
    L = z.L
    ground = floors[0]
    z.rock(x0, ground, x1)
    door_l = L.door(x0, ground - 6, 6, open_=True)
    door_r = L.door(x1 - 1, ground - 6, 6)
    for k, fy in enumerate(floors[1:]):
        left_gap = k % 2 == 0
        a, b = (x0 + 6, x1 - 2) if left_gap else (x0 + 2, x1 - 6)
        for gx in range(a, b, 12):
            L.oneway(gx, min(b, gx + 11), fy, "chain_grate")
        lx = x0 + 3 if left_gap else x1 - 4
        L.ladder(lx, fy, floors[k], 1)
        L.ladder((x0 + x1) // 2, fy, floors[k], 1)
    hp, ids = [], []
    for wave in waves:
        wid, whp = [], 0.0
        for kind, storey, x in wave:
            wid.append(L.enemy(kind, x, floors[storey], 4, x > (x0 + x1) // 2, dormant=True))
            whp += enemy_hp(kind)
        ids.append(wid)
        hp.append(whp)
    arena = L.combat(x0 + 1, floors[-1] - 6, x1 - 1, ground, doors=door_l + "," + door_r, waves=";".join(",".join(w) for w in ids), lock=False)
    L.data["objects"][-1]["props"]["announce"] = True
    L.light((x0 + x1) // 2, floors[-1] - 2, FORGE_GLOW, 170, flicker=0.2)
    L.sign(x0 + 4, ground, name + " : une embuscade sur %d étages, toutes ses vagues avant d'avancer." % len(floors))
    return arena, hp


def wave_of(kinds, x0, x1, storeys, seed):
    """Enemies spread over the storeys and the width (deterministic)."""
    out = []
    span = x1 - x0 - 10
    for i, kind in enumerate(kinds):
        storey = (i + seed) % storeys
        x = x0 + 5 + (i * 37 + seed * 11) % span
        out.append((kind, storey, x))
    return out


def zone_7():
    z = Zone(7, 752, "Le poignet du titan, verrouillé par deux générateurs : la longue route vers la Main. Des galeries "
                     "assez larges pour huit, des plaques et un monte-charge qui demandent le groupe, quatre embuscades "
                     "géantes sur plusieurs étages, des points de contrôle.")
    L, P = z.L, z.P
    # The entrance from the Coude-forge, wide: three lanes from the start.
    z.rock(0, 36, 40)
    z.passage(4, 1, 36)
    z.arrive(4, 9, 36)
    z.start([(9, 36), (12, 36), (15, 36), (18, 36)])
    L.sign(22, 36, "Poignet-verrou : quatre embuscades, des plaques et un monte-charge de groupe. Restez ensemble.")
    for y in (31, 26):
        L.oneway(24, 38, y, "basalt_ledge")
    # 40-120 · Les Gaines: a low lane over the lava and an upper lane of grates (8 never pile up).
    for x0 in range(40, 120, 16):
        z.rock(x0, 38, x0 + 11)
    for x0 in range(42, 120, 14):
        L.oneway(x0, x0 + 9, 31, "chain_grate")
    for x0 in range(48, 116, 22):
        L.oneway(x0, x0 + 6, 25, "basalt_ledge")
    for k, x in enumerate((50, 70, 92, 110)):
        L.enemy("lava_stalker" if k % 2 == 0 else "ember_mite", x, 38, 4, k % 2 == 1)
    L.enemy("fire_wisp", 80, 24, 4)
    L.enemy("forge_drone", 100, 22, 4, True)
    z.rock(116, 36, 124)
    z.checkpoint(120, 36, "Les Gaines")
    # 124-210 · Ambush 1, « La Tour des tendons » (4 storeys, 4 waves).
    floors1 = [36, 31, 26, 21]
    waves1 = [wave_of(["lava_stalker", "lava_stalker", "ember_mite", "ember_mite", "forge_bulwark", "magma_golem", "forge_drone", "fire_wisp",
                       "lava_stalker", "magma_gloop"], 124, 210, 4, s) for s in range(4)]
    arena1, _ = tower(z, 124, 210, floors1, waves1, "La Tour des tendons")
    z.rock(210, 36, 230)
    z.checkpoint(214, 36, "Après la Tour des tendons")
    L.orb("health_orb", 218, 34)
    # 230-290 · La Vanne double: a door held shut until 4 players stand on its plate together.
    z.rock(230, 36, 290)
    door1 = L.door(278, 30, 6)
    z.coop("vanne_double", 248, 260, 36, 4, [door1], "open", "La vanne double", "La vanne double cède : la porte s'ouvre")
    L.sign(244, 36, "La vanne double : 4 joueurs ensemble sur la plaque ouvrent la porte.")
    L.enemy("forge_drone", 254, 28, 4)
    L.enemy("forge_drone", 266, 27, 4, True)
    L.enemy("forge_bomber", 260, 24, 4)
    # 290-330 · Le Monte-charge: a lift up to the high gallery, moving once 3 players stand on it.
    z.rock(290, 36, 330)
    lift = L.moving(300, 35, 8, 0, -20, speed=55, wait=1.6, active=False, oneway=False)
    z.coop("monte_charge", 300, 308, 35, 3, [lift], "activate", "Le monte-charge",
           "Le monte-charge démarre : il fait ensuite la navette tout seul")
    L.sign(296, 36, "Le monte-charge : il monte quand 3 joueurs sont dessus.")
    L.solid(312, 14, 330, 16, "basalt")
    z.checkpoint(320, 14, "La galerie haute")
    # 330-390 · La Galerie haute: crumbling basalt over the void, wisps.
    for k, x0 in enumerate(range(330, 384, 9)):
        L.crumble(x0, 15 + (k % 2) * 2, 4, delay=0.5, respawn=3.0, style="basalt")
        L.oneway(x0 + 4, x0 + 8, 19 + (k % 3), "chain_grate")
    L.enemy("fire_wisp", 350, 10, 4)
    L.enemy("ash_wing", 372, 12, 4, True)
    L.solid(384, 16, 390, 40, "basalt")
    L.ladder(390, 16, 36, 1)
    z.rock(390, 36, 392)
    # 392-472 · Ambush 2, « La Galerie des forges » (3 storeys, 4 waves).
    floors2 = [36, 30, 24]
    waves2 = [wave_of(["forge_bulwark", "forge_bulwark", "lava_stalker", "lava_stalker", "lava_burrower", "magma_golem", "forge_bomber", "forge_drone",
                       "ember_mite", "ash_wing", "magma_gloop"], 392, 472, 3, s + 1) for s in range(4)]
    arena2, _ = tower(z, 392, 472, floors2, waves2, "La Galerie des forges")
    z.rock(472, 36, 490)
    z.checkpoint(478, 36, "Après la Galerie des forges")
    L.orb("health_orb", 484, 34)
    # 490-540 · Les Plaques jumelles: two plates far apart, 2 players on each at once.
    z.rock(490, 36, 540)
    L.solid(500, 28, 506, 36, "basalt")
    L.oneway(506, 516, 28, "basalt_ledge")
    door2 = L.door(534, 30, 6)
    for cid, x0, x1, y, label in (("jumelle_basse", 492, 500, 36, "La plaque jumelle basse"),
                                  ("jumelle_haute", 506, 516, 28, "La plaque jumelle haute")):
        z.coop(cid, x0, x1, y, 2, [door2], "open", label, "Les plaques jumelles tiennent ensemble : la porte s'ouvre",
               group="jumelles")
    L.sign(518, 36, "Les plaques jumelles : 2 joueurs sur chacune, en même temps.")
    L.enemy("lava_stalker", 522, 36, 4, True)
    L.enemy("forge_drone", 512, 22, 4)
    # 540-620 · Ambush 3, « La Fosse des scories » (3 storeys, 4 waves).
    floors4 = [36, 30, 24]
    waves4 = [wave_of(["lava_burrower", "lava_burrower", "magma_gloop", "magma_gloop", "forge_bulwark", "ash_wing", "ash_wing", "forge_drone",
                       "magma_golem", "ember_mite"], 540, 620, 3, s + 3) for s in range(4)]
    arena4, _ = tower(z, 540, 620, floors4, waves4, "La Fosse des scories")
    z.rock(620, 36, 632)
    z.checkpoint(626, 36, "Avant la Garde du verrou")
    L.orb("health_orb", 629, 34)
    # 632-732 · Ambush 4, « La Garde du verrou » (4 storeys, 5 waves) — its end breaks the lock.
    floors3 = [36, 31, 26, 21]
    waves3 = [wave_of(["magma_golem", "magma_golem", "forge_bulwark", "forge_bulwark", "lava_stalker", "lava_stalker", "forge_drone", "fire_wisp",
                       "ember_mite", "ember_mite", "forge_bomber"], 632, 732, 4, s + 2) for s in range(5)]
    arena3, _ = tower(z, 632, 732, floors3, waves3, "La Garde du verrou")
    z.objective("poignet_verrou", arena3, "cleared")
    # The end: the door of the Main opens when the guard is beaten.
    z.rock(732, 36, 752)
    door = L.door(740, 30, 6)
    L.event(arena3, "cleared", [{"type": "open", "target": door},
                                {"type": "hint", "params": {"text": "Le verrou cède : la Main de Pyroclast est ouverte.",
                                                            "duration": 3.5}}], delay=0.5)
    z.passage(8, 745, 36, door=door)
    z.arrive(8, 736, 36)
    L.light(120, 30, FORGE_GLOW, 120, flicker=0.2)
    L.light(300, 28, LAVA_GLOW, 140, flicker=0.2)
    L.light(520, 28, FORGE_GLOW, 120, flicker=0.2)
    P.scatter(2, 38, 36, 0.25, forge=True)
    z.save()


# ============================================================ 8 — Main de Pyroclast
# The arena of the gigaboss (2.68), sized for 8 players and a giant: the west ledge (0-22, floor
# 32) with the ways back, the arena (23-205, floor 36): towers of ledges on both sides over pits
# whose crust gives way at the 2nd phase, two lifts up to a high platform over its head, its three
# vents (left tower, high platform, right tower: the group must split), curtains of magma at the 3rd.
VENTS = [("event_ouest", 42, 26), ("event_centre", 116, 14), ("event_est", 186, 26)]


def zone_8():
    z = Zone(8, 210, "La main du titan, où veille le gigaboss Pyroclast, assez grand pour huit joueurs. Trois phases : "
                     "son poing de magma, sa carapace de basalte (ses trois évents à fermer ensemble), son cœur à nu.")
    L, P = z.L, z.P
    L.data["bossMusic"] = "music/pyralis_boss.ogg"
    f = 36
    z.rock(0, 32, 23)
    z.passage(6, 1, 32)
    z.passage(7, 9, 32)
    z.arrive(6, 15, 32)
    z.arrive(7, 18, 32)
    z.start([(15, 32), (18, 32), (13, 32)])
    L.sign(20, 32, "Pyroclast, la main de magma. Ses évents nourrissent sa carapace : fermez-les ensemble.")
    P.brazier(5, 32)
    # The floor, the pits under the towers (their crust gives way at the 2nd phase).
    pits = [(30, 52), (176, 198)]
    edge = 23
    for a, b in pits:
        z.rock(edge, f, a)
        z.rock(a, f + 4, b)
        edge = b
    z.rock(edge, f, 210)
    crust, magma = [], []
    for a, b in pits:
        for cx in range(a, b, 4):
            oid = L.crumble(cx, f, min(4, b - cx), delay=0.3, respawn=0.0, style="basalt")
            L.data["objects"][-1]["props"]["onEvent"] = True
            crust.append(oid)
        magma.append(L.obj("damage_zone", [a * T, (f + 2) * T, (b - a) * T, 2 * T], "gameplay",
                           {"damage": 2, "interval": 0.6, "visible": True, "shieldProtects": True, "active": False, "style": "magma",
                            "rise": 1.2, "fall": 1.2, "bounce": True}))
    # The towers (ledges and ladders), the high platform and its two lifts.
    for a, b, y in ((30, 40, 31), (36, 48, 26), (28, 36, 21), (188, 198, 31), (180, 192, 26), (192, 200, 21)):
        L.oneway(a, b, y, "basalt_ledge")
    L.ladder(29, 26, 36, 1)
    L.ladder(199, 26, 36, 1)
    L.oneway(106, 126, 14, "basalt_ledge")
    lifts = [L.moving(90, 35, 6, 0, -21, speed=60, wait=1.2, oneway=True),
             L.moving(132, 35, 6, 0, -21, speed=60, wait=1.2, phase=0.5, oneway=True)]
    L.oneway(96, 104, 18, "basalt_ledge")
    L.oneway(128, 136, 18, "basalt_ledge")
    curtains = [L.obj("damage_zone", [23 * T, 2 * T, 5 * T, (f - 2) * T], "gameplay",
                      {"damage": 2, "interval": 0.5, "visible": True, "shieldProtects": True, "active": False, "style": "magma",
                       "rise": 1.0, "fall": 1.0, "bounce": False}),
                L.obj("damage_zone", [200 * T, 2 * T, 5 * T, (f - 2) * T], "gameplay",
                      {"damage": 2, "interval": 0.5, "visible": True, "shieldProtects": True, "active": False, "style": "magma",
                       "rise": 1.0, "fall": 1.0, "bounce": False})]
    orbs = {}
    for ph, ox, oy in ((2, 42, 24), (2, 186, 24), (2, 116, 12), (3, 34, 19), (3, 196, 19), (3, 100, 16), (3, 132, 16)):
        L.orb("health_orb", ox, oy)
        L.data["objects"][-1]["props"] = {"amount": 4, "hidden": True}
        orbs.setdefault(ph, []).append(L.data["objects"][-1]["id"])
    boss = L.enemy("pyroclast", 150, f, patrol_tiles=30, face_left=True, dormant=True, boss=True)
    L.combat(24, 2, 205, f, waves=boss, lock=False, music="music/pyralis_boss.ogg")
    L.data["objects"][-1]["props"]["announce"] = False
    z.objective("main_pyroclast", boss, "killed")
    L.event(boss, "bossPhase", [{"type": "activate", "target": ",".join(crust)},
                                {"type": "enable", "target": ",".join(magma)},
                                {"type": "spawn", "target": ",".join(orbs[2])},
                                {"type": "shake", "params": {"amount": 0.8}},
                                {"type": "hint", "params": {"text": "La main frappe le sol : le basalte cède au magma sous les tours !",
                                                            "duration": 3.5}}],
            params={"phase": 2}, name="La main se referme")
    L.event(boss, "bossPhase", [{"type": "enable", "target": ",".join(curtains)},
                                {"type": "spawn", "target": ",".join(orbs[3])},
                                {"type": "shake", "params": {"amount": 1.0}},
                                {"type": "hint", "params": {"text": "Le magma se referme sur l'arène : restez au centre !",
                                                            "duration": 3.5}}],
            params={"phase": 3}, name="Le cœur à nu")
    L.event(boss, "bossDefeated", [{"type": "hint", "params": {"text": "Pyroclast est vaincu : le bras droit d'Asterion s'éteint.",
                                                               "duration": 4}},
                                   {"type": "disable", "target": ",".join(magma + curtains)},
                                   {"type": "shake", "params": {"amount": 0.6}}], delay=1.5, name="Pyroclast vaincu")
    vents = []
    for vid, vx, vy in VENTS:
        vents.append({"id": vid, "rect": [(vx - 1.5) * T, (vy - 1) * T, 3 * T, 1 * T]})
        L.light(vx, vy - 1, LAVA_GLOW, 70, flicker=0.25)
        L.emitter(vx - 1, vy - 2, vx + 1, vy, "embers_rising", 1.5)
    # The arena (metadata.raid.arena, the format of the seven branches' lairs: the arena camera reads it).
    z.raid["arena"] = {"rect": [23 * T, 0, 183 * T, (f + 2) * T], "floor": f * T, "door": "", "entry": [30 * T, f * T], "players": 8}
    z.raid["gigaboss"] = {
        "boss": boss, "name": "Pyroclast", "objective": "main_pyroclast", "vents": vents, "carapace": [2, 3],
        "hold": 3.0, "reopen": 15.0, "exposed": 10.0, "exposedMult": 2.0,
        "jet": {"period": 6.0, "warn": 1.2, "duration": 1.0, "height": 112.0, "damage": 1.0},
        "fists": {"periods": [9.0, 7.0, 5.5], "warn": 1.4, "width": 64.0, "damage": 1.0},
        "weakEnemy": "pyroclast_noyau", "weakSpots": [[100.0, -262.0], [60.0, -300.0], [20.0, -310.0]], "weakShare": 0.04,
        "hpPerPlayer": 0.55, "weakHpPerPlayer": 0.25, "pressurePerPlayer": 0.06}
    P.scatter(53, 175, f, 0.25)
    L.decor("skull_rock", 60, f)
    L.decor("obsidian_spire", 170, f)
    L.decor("obsidian_spire", 205, f, flip=True)
    for lx in (42, 80, 116, 152, 186):
        L.light(lx, 18, "#ffd27a", 170, flicker=0.3)
    L.emitter(60, 10, 170, f, "embers_rising", 3.0)
    L.orb("health_orb", 20, 30)
    z.save()


def catalog(minutes):
    zones = []
    for n, name, sub, x, y, inside, event in ZONES:
        zones.append({"id": n, "name": name, "sub": sub, "x": x, "y": y, "level": "zone_%d.json" % n,
                      "inside": inside, "event": event, "secret": n == 5, "boss": n == 8,
                      "groups_required": 2 if n == 4 else 1})
        if n in minutes:
            zones[-1]["minutes"] = minutes[n]
    objectives = [{"id": oid, "index": i, "zone": zone, "label": label, "optional": opt, "role": role, "pair": pair}
                  for i, (oid, zone, label, opt, role, pair) in enumerate(OBJECTIVES)]
    edges = [{"a": a, "b": b, "lock": lock, "secret": secret} for a, b, lock, secret in EDGES]
    return {"id": BRANCH_ID, "revision": REVISION, "raid": "asterion", "branch": "bras_droit", "name": "BRAS DROIT", "gigaboss": "Pyroclast",
            "start_zone": 1, "generators_needed": 2, "zones": zones, "edges": edges, "objectives": objectives}


def main():
    for build in (zone_1, zone_2, zone_3, zone_4, zone_5, zone_6, zone_7, zone_8):
        build()
    # 2.68 (phase 3): how long each zone takes a group of 8 (the model of raid_asterion.py; the
    # gigaboss apart): the Poignet-verrou, the long way to the Main, about 15 minutes.
    minutes = {}
    for n in range(1, 8):
        with open(os.path.join(OUT, "zone_%d.json" % n), encoding="utf-8") as f:
            minutes[n] = round(estimate_minutes(json.load(f)), 1)
    assert 13.0 <= minutes[7] <= 17.0, ("expedition", minutes[7])
    print("bras_droit : %s min (8 joueurs ; Poignet-verrou : %.1f min ; branche : %.1f min sans le gigaboss)"
          % (minutes, minutes[7], sum(minutes.values())))
    cat = catalog(minutes)
    text = json.dumps(cat, ensure_ascii=False, indent=1) + "\n"
    for path in (os.path.join(OUT, "branch.json"), SERVER_COPY):
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        print("wrote %s" % os.path.relpath(path, ROOT))


if __name__ == "__main__":
    main()
