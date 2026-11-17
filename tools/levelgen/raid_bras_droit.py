#!/usr/bin/env python3
"""Builds the branch « BRAS DROIT » of the clan raid « Asterion » (phase 2, docs/RAIDS.md).

Eight sub-zones on the titan's right arm, each a real level (data/raids/asterion/bras_droit/
zone_N.json), and the branch catalog (branch.json) the game and the server share (a copy goes to
server/src/raidBranches.json: the server never trusts the game's).

The graph (maquette « Branche »): Épaule (1) → Biceps de basalte (2) and Veine de magma (3), two
paths that meet at the Coude-forge (4, two groups: one bellows each). The Veine hides the secret
Grotte des scories (5) and the shortcut to the Avant-bras (6), opened by the lever of the
Avant-bras. The Poignet-verrou (7) opens once the two generators (Veine and Avant-bras) are on;
its arena opens the way to the Main de Pyroclast (8), whose sealed door waits for phase 3.

Each zone's metadata.raid holds its spawn points (by the zone one comes from), its passages to its
neighbours (rect in pixels, lock), its objectives (an id of the catalog, the object and the event
that completes it), its magma flow (a damage zone switched on every period, announced before) and,
for the Main, its sealed door. Never edit the JSON by hand: run
    python3 tools/levelgen/raid_bras_droit.py
from the repository root (the output is stable: same bytes each run).
"""
import json
import os

from levelkit import Level, ROOT, T
from pyralis_common import Pyra, FORGE_GLOW, LAVA_GLOW

BRANCH_ID = "asterion_bras_droit"
# The revision of the branch: every game of an instance must have the same (the relay's GO
# carries it). Raise it whenever a zone or the catalog changes how the branch plays.
REVISION = 1
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
     "Ennemis : rôdeurs de lave, feux follets · générateur ouest", "Coulée de magma toutes les 60 s (terrasses hautes sûres)"),
    (4, "Coude-forge", "2 soufflets à détruire", 560, 300,
     "Ennemis : forgerons de lave, drones-scories · mini-boss : l'Enclume vivante",
     "Coulée de magma toutes les 90 s (plateformes hautes sûres)"),
    (5, "Grotte des scories", "zone secrète ◆ relique", 560, 620,
     "Ennemis : gloops de magma, acariens de braise", ""),
    (6, "Avant-bras", "raccourci ⇄ depuis la Veine", 790, 160,
     "Ennemis : drones de forge, ailes de cendre · générateur est", ""),
    (7, "Poignet-verrou", "s'ouvre : 2 générateurs", 790, 470,
     "Ennemis : la garde du verrou (arène en trois vagues)", ""),
    (8, "Main de Pyroclast", "GIGABOSS · prochaine version", 1000, 320,
     "Le gigaboss Pyroclast attend derrière sa porte scellée", ""),
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
]

EDGES = [
    # a, b, lock ("", "generators", "shortcut", "objective:<id>"), secret
    (1, 2, "", False), (1, 3, "", False), (2, 4, "", False), (3, 4, "", False), (3, 5, "", True),
    (2, 6, "", False), (3, 6, "shortcut", False), (4, 7, "generators", False), (6, 8, "", False),
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

    def magma(self, x0, x1, y0, y1, period, first, warn=3.0, duration=8.0):
        dz = self.L.obj("damage_zone", [x0 * T, y0 * T, (x1 - x0) * T, (y1 - y0) * T], "gameplay",
                        {"damage": 2, "interval": 0.6, "visible": True, "active": False, "style": "embers",
                         "bounce": True}, oid="magma_flow")
        self.raid["magma"] = {"zone": dz, "period": period, "first": first, "warn": warn, "duration": duration}
        return dz

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
    z.magma(14, 50, 32, 38, period=60.0, first=30.0)
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
    z.magma(22, 128, 33, 36, period=90.0, first=40.0)
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
    z.rock(90, 27, 130)
    z.passage(8, 124, 27)
    z.arrive(8, 117, 27)
    L.orb("health_orb", 92, 25)
    L.light(64, 26, "#39f0d8", 90, flicker=0.1)
    z.save()


# ================================================================ 7 — Poignet-verrou
def zone_7():
    z = Zone(7, 110, "Le poignet, verrouillé par deux générateurs. Sa garde défend le passage vers la Main de Pyroclast.")
    L, P = z.L, z.P
    z.rock(0, 34, 16)
    z.passage(4, 1, 34)
    z.arrive(4, 9, 34)
    z.start([(9, 34), (12, 34)])
    z.rock(16, 36, 90)
    P.scatter(17, 89, 36, 0.25, forge=True)
    w1 = [L.enemy("lava_stalker", 34, 36, 5, dormant=True), L.enemy("lava_stalker", 70, 36, 5, True, dormant=True),
          L.enemy("ember_mite", 52, 36, 3, dormant=True)]
    w2 = [L.enemy("forge_drone", 40, 28, 4, dormant=True), L.enemy("forge_drone", 64, 28, 4, True, dormant=True),
          L.enemy("forge_bulwark", 56, 36, 5, True, dormant=True)]
    w3 = [L.enemy("magma_golem", 60, 36, 6, True, dormant=True), L.enemy("fire_wisp", 30, 26, 4, dormant=True)]
    arena = L.combat(20, 22, 88, 36, waves=";".join(",".join(w) for w in (w1, w2, w3)), lock=False)
    z.objective("poignet_verrou", arena, "cleared")
    L.oneway(30, 38, 31, "chain_grate")
    L.oneway(64, 72, 31, "chain_grate")
    z.rock(90, 33, 110)
    door = L.door(98, 29, 4)
    L.event(arena, "cleared", [{"type": "open", "target": door},
                               {"type": "hint", "params": {"text": "Le verrou cède : la Main de Pyroclast est ouverte.",
                                                           "duration": 3.5}}], delay=0.5)
    z.passage(8, 103, 33, door=door)
    z.arrive(8, 94, 33)
    L.orb("health_orb", 18, 34)
    L.light(55, 28, FORGE_GLOW, 130, flicker=0.2)
    z.save()


# ============================================================ 8 — Main de Pyroclast
def zone_8():
    z = Zone(8, 90, "La main du titan, où veille le gigaboss Pyroclast. Sa porte est scellée : le combat arrive "
                    "dans la prochaine version.")
    L, P = z.L, z.P
    z.rock(0, 32, 40)
    z.passage(6, 1, 32)
    z.arrive(6, 9, 32)
    z.start([(9, 32), (12, 32)])
    z.rock(40, 36, 90)
    z.passage(7, 44, 36)
    z.arrive(7, 52, 36)
    door = L.door(80, 24, 12)
    L.sign(72, 36, "Pyroclast — prochaine version. La porte de la Main restera scellée jusque-là.")
    z.raid["sealed"] = {"door": door, "label": "Pyroclast — prochaine version"}
    L.decor("skull_rock", 64, 36)
    L.decor("obsidian_spire", 76, 36)
    L.decor("obsidian_spire", 86, 36, flip=True)
    P.scatter(41, 70, 36, 0.3)
    L.light(80, 30, "#ffd27a", 160, flicker=0.3)
    L.emitter(70, 20, 90, 36, "embers_rising", 2.5)
    z.save()


def catalog():
    zones = []
    for n, name, sub, x, y, inside, event in ZONES:
        zones.append({"id": n, "name": name, "sub": sub, "x": x, "y": y, "level": "zone_%d.json" % n,
                      "inside": inside, "event": event, "secret": n == 5, "boss": n == 8,
                      "groups_required": 2 if n == 4 else 1})
    objectives = [{"id": oid, "index": i, "zone": zone, "label": label, "optional": opt, "role": role, "pair": pair}
                  for i, (oid, zone, label, opt, role, pair) in enumerate(OBJECTIVES)]
    edges = [{"a": a, "b": b, "lock": lock, "secret": secret} for a, b, lock, secret in EDGES]
    return {"id": BRANCH_ID, "revision": REVISION, "raid": "asterion", "branch": "bras_droit", "name": "BRAS DROIT", "gigaboss": "Pyroclast",
            "start_zone": 1, "generators_needed": 2, "zones": zones, "edges": edges, "objectives": objectives}


def main():
    for build in (zone_1, zone_2, zone_3, zone_4, zone_5, zone_6, zone_7, zone_8):
        build()
    cat = catalog()
    text = json.dumps(cat, ensure_ascii=False, indent=1) + "\n"
    for path in (os.path.join(OUT, "branch.json"), SERVER_COPY):
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        print("wrote %s" % os.path.relpath(path, ROOT))


if __name__ == "__main__":
    main()
