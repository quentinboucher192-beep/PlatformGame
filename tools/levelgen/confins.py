#!/usr/bin/env python3
"""Builds the levels of Les Confins (version 2.48), the world beyond the dying star, open from the
start (data/planets/confins.json):

  data/levels/confins_01.json  "La Gueule du Dragon" (EXTRÊME): one life, no checkpoint. The
                                choice of the powers, then the giant arena of Pyrrhax, le Dragon
                                d'Obsidienne (8 phases, tools/contentgen/pyrrhax.py)
  data/levels/confins_02.json  "Le Cœur de Pierre" (2.49, EXTRÊME): the same for the Monolithe,
                                le Golem Primordial (a walker: the crust gives way on the two
                                sides of its arena)
  data/levels/confins_03.json  "La Fosse des Abysses" (2.53, EXTRÊME): the same for the Kraken des
                                Abysses (tools/contentgen/kraken.py; it swims: the crust gives way
                                over two trenches of dark water, the middle stays)
  data/levels/confins_04.json  "Le Nid de Cendres" (2.56, EXTRÊME): the same for the Phénix
                                Stellaire (tools/contentgen/phenix.py; it flies: the crust gives
                                way over two beds of embers at the ends, its nest in the middle
                                stays; its perches of embers burn under Nova and are reborn)
  data/levels/confins_05.json  "La Crypte des Noms" (2.57, EXTRÊME): the same for the Liche des
                                Catacombes (tools/contentgen/liche.py; it floats over four altars
                                of its crypt and teleports between them; the crust gives way over two
                                ossuaries at the ends, the floor between its two floor altars stays)

2.49: each level of Les Confins is its giga boss's fight alone (no platforming before it).

The rooms are the calibrated rooms of the trials (challenge.py), with Pyralis' materials and the
obsidian of the dead star; the arena is the giant arena of Singularis (stellar_core.Star) answering
the dragon's phases. Run from the repository root after tools/contentgen/pyrrhax.py:
  python3 tools/levelgen/confins.py
"""
import os

from challenge import HEADROOM
from levelkit import ROOT, T, giga_powers
from stellar_core import Star


class Edge(Star):
    """The builder of Star on the planet of Les Confins."""

    def __init__(self, level_id, name, first_theme="pyralis", seed=8101):
        super().__init__(level_id, name, first_theme, seed)
        self.L.planet("confins")

    DRAGON_TEXTS = {
        3: ("Ses ailes arrachent la roche : les blocs tournoient !", "Tempête d'ailes : les blocs en orbite"),
        5: "Écailles brisées : le sol cède",
        7: "Couvée : un soin",
        8: ("Le feu referme l'arène !", "Soleil noir : l'arène se resserre"),
        "won": ("Le dragon retourne à la pierre. La sortie est ouverte.", "Pyrrhax vaincu"),
    }

    def dragon_arena(self, boss, music=""):
        """The giant arena of Pyrrhax: the crust over the middle gives way at the 5th phase."""
        return self.giga_arena(boss, music, pits=[self.GIANT_BASIN], flying=True, texts=self.DRAGON_TEXTS)

    FIRE_LIGHTS = ((10, 26, "#ff7a2e"), (48, 30, "#ffc49a"), (86, 26, "#ff7a2e"), (30, 8, "#ff5ad2"), (66, 8, "#ff5ad2"))

    def giga_arena(self, boss, music, pits, flying, texts, style="plasma", lights=FIRE_LIGHTS, flicker=0.12,
                   perches=(), altars=()):
        """The giant arena (Star.GIANT_*) of a giga boss: the crust over each pit gives way at
        the 5th phase (magma below), debris orbit from the 3rd, fire closes in at the 8th and at
        the boss's rage. `pits`: (first, last) inside columns of each crust; `flying`: where the
        boss waits (in the air, or on the floor). 2.53: `style` of what fills the pits and closes
        in (« plasma », « abyss »: the dark water of the Kraken, or 2.56 « embers »: the Phénix's),
        the arena's `lights`. 2.56: the ledges at the heights `perches` are perches of embers
        instead (crumble platforms « ember »: they burn under Nova and are reborn). 2.57: `altars`
        (x, h): the altars of the Liche's crypt (objects « altar », 3 tiles wide, 2.5 tall, standing at
        the height h: 0 the floor, else a ledge), over which it floats and between which it teleports."""
        L, f = self.L, self.f
        x0 = self.x
        X = x0 + 1
        W, Hh = self.GIANT_W, self.GIANT_H
        door_h = 8
        ledge = "sanctum_ledge"
        self.roof(x0, X + W + 1, f - Hh)
        L.solid(x0, f - Hh, x0 + 1, f - door_h, self.th["accent"], deco=False)
        L.solid(X + W, f - Hh, X + W + 1, f - door_h, self.th["accent"], deco=False)
        left = L.door(x0, f - door_h, door_h, open_=True)
        right = L.door(X + W, f - door_h, door_h, open_=False)
        # The floor between the pits, the pits 4 tiles deep.
        edge = None
        for (b0, b1) in pits:
            self.ground(x0 if edge is None else X + edge, X + b0, f)
            edge = b1
        self.ground(X + edge, X + W + 1, f)
        for (b0, b1) in pits:
            self.ground(X + b0, X + b1, f + 4)
        # The obsidian crust over the magma: it gives way at the 5th phase.
        crust, magma = [], []
        for (b0, b1) in pits:
            for cx in range(b0, b1, 4):
                oid = L.crumble(X + cx, f, 4, delay=0.3, respawn=0.0, style="basalt")
                L.data["objects"][-1]["props"]["onEvent"] = True
                crust.append(oid)
            magma.append(L.obj("damage_zone", [(X + b0) * T, (f + 2) * T, (b1 - b0) * T, 2 * T], "gameplay",
                               {"damage": 2, "interval": 0.6, "visible": True, "shieldProtects": True, "active": False,
                                "style": style, "bounce": True}))
        for h, ls in self.GIANT_LEDGES.items():
            for (lx, lw) in ls:
                if h in perches:
                    L.crumble(X + lx, f - h, lw, delay=0.6, respawn=3.5, style="ember")
                else:
                    L.oneway(X + lx, X + lx + lw, f - h, ledge)
        for (ax, ah) in altars:
            L.obj("altar", [(X + ax - 1.5) * T, (f - ah - 2.5) * T, 3 * T, 2.5 * T], "terrain", {})
        ocx, och, orx, ory = self.GIANT_ORBIT
        debris = []
        for k in range(3):
            oid = L.moving(X + ocx - 2, f - och - 0.5, 4, orx, ory, speed=50, wait=0.0, phase=round(0.25 + k / 3.0, 3),
                           active=False, oneway=True)
            L.data["objects"][-1]["props"]["orbit"] = True
            debris.append(oid)
        orbs = {}
        for (ph, ox, oh) in self.GIANT_ORBS:
            L.orb("health_orb", X + ox, f - oh - 1.5)
            L.data["objects"][-1]["props"] = {"amount": 4, "hidden": True}
            orbs[ph] = L.data["objects"][-1]["id"]

        def curtain(a, b):
            return L.obj("damage_zone", [(X + a) * T, (f - Hh) * T, (b - a) * T, Hh * T], "gameplay",
                         {"damage": 2, "interval": 0.5, "visible": True, "shieldProtects": True, "active": False,
                          "style": style, "bounce": False})
        curtain1 = [curtain(0, 12), curtain(W - 12, W)]
        curtain2 = [curtain(12, 22), curtain(W - 22, W - 12)]
        bid = L.enemy(boss, X + 70, f - 20 if flying else f, patrol_tiles=6, face_left=True, dormant=True, boss=True)
        L.combat(X, f - Hh, X + W, f + 4, doors=left + "," + right, waves=bid, lock=False,
                 music=music or L.data.get("bossMusic", ""))
        L.data["objects"][-1]["props"]["announce"] = False
        for (lx, lh, col) in lights:
            L.light(X + lx, f - lh, col, 160, flicker=flicker)
        fire = magma + curtain1 + curtain2
        L.event(bid, "bossPhase", [{"type": "activate", "target": ",".join(debris)},
                                   {"type": "spawn", "target": orbs[3]},
                                   {"type": "hint", "params": {"text": texts[3][0], "duration": 3}}],
                params={"phase": 3}, name=texts[3][1])
        L.event(bid, "bossPhase", [{"type": "activate", "target": ",".join(crust)},
                                   {"type": "enable", "target": ",".join(magma)},
                                   {"type": "spawn", "target": orbs[5]},
                                   {"type": "shake", "params": {"amount": 0.8}}],
                params={"phase": 5}, name=texts[5])
        L.event(bid, "bossPhase", [{"type": "spawn", "target": orbs[7]}], params={"phase": 7}, name=texts[7])
        L.event(bid, "bossPhase", [{"type": "enable", "target": ",".join(curtain1)},
                                   {"type": "hint", "params": {"text": texts[8][0], "duration": 3}}],
                params={"phase": 8}, name=texts[8][1])
        L.event(bid, "bossEnrage", [{"type": "enable", "target": ",".join(curtain2)},
                                    {"type": "shake", "params": {"amount": 0.6}}], name="Rage : l'arène se resserre encore")
        L.event(bid, "bossDefeated", [{"type": "disable", "target": ",".join(fire)},
                                      {"type": "hint", "params": {"text": texts["won"][0], "duration": 4}}],
                delay=1.5, name=texts["won"][1])
        self.x = X + W + 1
        return bid

    def cinematics(self, title, subtitle, motto, victory):
        super().cinematics(title, subtitle, motto, victory)
        # The outro names the world of this level.
        for st in self.L.data["outro"]["steps"]:
            if st["type"] == "text":
                st["params"]["subtitle"] = "Les Confins — " + self.L.data["name"]


# ---------------------------------------------------------------- the dragon's maw (2.48)
def dragon_maw():
    b = Edge("confins_01", "La Gueule du Dragon", "pyralis", seed=8101)
    L, d = b.L, b.L.data
    d["description"] = ("Aux Confins du système, une croûte d'obsidienne sur un feu vivant : Pyrrhax et ses huit phases, "
                        "dans une arène géante. Une seule vie, aucun checkpoint.")
    d["difficulty"] = 5
    # EXTRÊME: one life (the challenge mode below allows no death) and a race online.
    d["extreme"] = True
    b.f = 40
    # 2.49: only the fight, as Singularis' level: the choice of the powers, then the giant arena
    # (no platforming before it; dash, grapple, glide and the charged shot are carried from the
    # start: levelkit.START_POWERS; four carried, what is left stays here).
    b.th.update(floor="obsidian", roof="obsidian", accent="obsidian")
    b.opening("La Gueule du Dragon : Pyrrhax seul, une seule vie, aucun checkpoint. Choisissez vos quatre pouvoirs.")
    b.tablets(giga_powers("confins_01"))  # 2.50: every power of the game
    b.zone_sign("Pyrrhax, le Dragon d'Obsidienne.")
    b.rooms.append({"type": "boss", "x": b.x, "floor": b.f, "boss": "pyrrhax"})
    b.dragon_arena("pyrrhax", music="music/pyrrhax_boss.ogg")
    width = b.finish("Le dragon s'est tu. Les Confins s'éteignent.")
    # Reference: about six minutes of fight.
    ref = 360
    d["timer"] = {"mode": "challenge", "limit": 3600, "onExpire": "gameover", "referenceTime": ref, "challengeMaxDeaths": 0}
    d["medals"] = {"bronze": {}, "silver": {"maxTime": int(ref * 1.6)}, "gold": {"maxTime": int(ref * 1.2)},
                   "platinum": {"maxTime": ref, "maxDeaths": 0}}
    b.cinematics("LA GUEULE DU DRAGON", "Les Confins", "Une seule vie. Un dragon d'obsidienne. Huit phases.",
                 "LE DRAGON S'ÉTEINT")
    # 2.49: the fight alone (a new version of the level: its rankings start again). 2.50: every
    # power (revision 10), the records of revision 9 kept (data/levels/kept_records.json).
    d["metadata"] = {"generator": "tools/levelgen/confins.py", "revision": 10, "rooms": b.rooms, "noHealing": True}
    L.save(os.path.join(ROOT, "data/levels/confins_01.json"))
    return width


# ---------------------------------------------------------------- the heart of stone (2.49)
GOLEM_TEXTS = {
    3: ("Ses pas arrachent la roche : les blocs tournoient !", "Séisme : les blocs en orbite"),
    5: "Fendu : les fissures s'ouvrent",
    7: "Azur : un soin",
    8: ("Le magma referme l'arène !", "Incandescent : l'arène se resserre"),
    "won": ("Le Monolithe s'effondre en un tas de pierres. La sortie est ouverte.", "Monolithe vaincu"),
}


def stone_heart():
    b = Edge("confins_02", "Le Cœur de Pierre", "pyralis", seed=8201)
    L, d = b.L, b.L.data
    d["description"] = ("Sous la croûte des Confins, une caverne de granit gravé de runes : le Monolithe et ses huit "
                        "phases, dans une arène géante. Une seule vie, aucun checkpoint.")
    d["difficulty"] = 5
    d["extreme"] = True
    # 2.52: its own music when its fight starts (the theme of Les Confins gives Pyrrhax's).
    d["bossMusic"] = "music/monolithe_boss.ogg"
    b.f = 40
    # Only the fight, as Pyrrhax' level: the choice of the powers, then the giant arena.
    b.th.update(floor="obsidian", roof="obsidian", accent="obsidian")
    b.opening("Le Cœur de Pierre : le Monolithe seul, une seule vie, aucun checkpoint. Choisissez vos quatre pouvoirs.")
    b.tablets(giga_powers("confins_02"))  # 2.50: every power of the game
    b.zone_sign("Le Monolithe, le Golem Primordial.")
    b.rooms.append({"type": "boss", "x": b.x, "floor": b.f, "boss": "monolithe"})
    # A walker: the crust gives way on the two sides only (the golem fights in the middle).
    b.giga_arena("monolithe", "music/monolithe_boss.ogg", pits=[(4, 16), (80, 92)], flying=False, texts=GOLEM_TEXTS)
    width = b.finish("Le Monolithe s'est tu. Le cœur des Confins se refroidit.")
    ref = 360  # about six minutes of fight
    d["timer"] = {"mode": "challenge", "limit": 3600, "onExpire": "gameover", "referenceTime": ref, "challengeMaxDeaths": 0}
    d["medals"] = {"bronze": {}, "silver": {"maxTime": int(ref * 1.6)}, "gold": {"maxTime": int(ref * 1.2)},
                   "platinum": {"maxTime": ref, "maxDeaths": 0}}
    b.cinematics("LE CŒUR DE PIERRE", "Les Confins", "Une seule vie. Un golem plus ancien que les planètes. Huit phases.",
                 "LE GOLEM S'EFFONDRE")
    # 2.50: every power (revision 9), the records of revision 8 kept (data/levels/kept_records.json).
    # 2.52: the Monolithe reworked (its brain, its shell, its quake, its titanic leap): revision 10,
    # the records of revisions 8 and 9 kept.
    d["metadata"] = {"generator": "tools/levelgen/confins.py", "revision": 10, "rooms": b.rooms, "noHealing": True}
    L.save(os.path.join(ROOT, "data/levels/confins_02.json"))
    return width


# ---------------------------------------------------------------- the abyssal trench (2.53)
KRAKEN_TEXTS = {
    3: ("Ses tentacules arrachent la roche : les blocs tournoient !", "Encre : les blocs en orbite"),
    5: "Brisé : les abysses s'ouvrent",
    7: "Tourbillon : un soin",
    8: ("Les eaux noires referment l'arène !", "Léviathan : l'arène se resserre"),
    "won": ("Le Kraken sombre dans les abysses. La sortie est ouverte.", "Kraken vaincu"),
}
# The lights of the abyss: cyan photophores, indigo glimmers low over the pits.
KRAKEN_LIGHTS = ((10, 26, "#7ae6ff"), (48, 30, "#b8f2ff"), (86, 26, "#7ae6ff"), (30, 8, "#6a58f0"), (66, 8, "#6a58f0"))
# The sky of the Confins seen through dark water (display only).
ABYSS_TINTS = {"sky.png": "#5a6ab0", "far.png": "#465886", "mid.png": "#38466c", "abyss.png": "#4a6aa0",
               "near.png": "#303c58", "fg_embers.png": "#8ad8ffc0"}


def abyss_trench():
    b = Edge("confins_03", "La Fosse des Abysses", "pyralis", seed=8301)
    L, d = b.L, b.L.data
    d["description"] = ("Sous la croûte des Confins, une mer noire sans fond : le Kraken des Abysses et ses huit "
                        "phases, dans une arène géante. Une seule vie, aucun checkpoint.")
    d["difficulty"] = 5
    d["extreme"] = True
    d["bossMusic"] = "music/kraken_boss.ogg"
    d["ambientLight"] = "#6a7ab4"
    d["backgroundColor"] = "#040a1c"
    for layer in d["background"]:
        name = layer["texture"].rsplit("/", 1)[-1]
        if name in ABYSS_TINTS:
            layer["tint"] = ABYSS_TINTS[name]
        if "fillBelow" in layer:
            layer["fillBelow"] = "#1a3a6a"
    b.f = 40
    # Only the fight, as the other giga bosses' levels: the choice of the powers, then the arena.
    b.th.update(floor="obsidian", roof="obsidian", accent="obsidian")
    b.opening("La Fosse des Abysses : le Kraken seul, une seule vie, aucun checkpoint. Choisissez vos quatre pouvoirs.")
    b.tablets(giga_powers("confins_03"))  # every power of the game
    b.zone_sign("Le Kraken des Abysses.")
    b.rooms.append({"type": "boss", "x": b.x, "floor": b.f, "boss": "kraken"})
    # It swims (it waits in the air): the crust gives way over two trenches of dark water, the
    # middle of the arena stays (and its two ends).
    b.giga_arena("kraken", "music/kraken_boss.ogg", pits=[(10, 38), (58, 86)], flying=True, texts=KRAKEN_TEXTS,
                 style="abyss", lights=KRAKEN_LIGHTS, flicker=0.08)
    width = b.finish("Le Kraken a sombré. Les abysses se taisent.")
    ref = 360  # about six minutes of fight
    d["timer"] = {"mode": "challenge", "limit": 3600, "onExpire": "gameover", "referenceTime": ref, "challengeMaxDeaths": 0}
    d["medals"] = {"bronze": {}, "silver": {"maxTime": int(ref * 1.6)}, "gold": {"maxTime": int(ref * 1.2)},
                   "platinum": {"maxTime": ref, "maxDeaths": 0}}
    b.cinematics("LA FOSSE DES ABYSSES", "Les Confins", "Une seule vie. Un kraken venu des mers enfouies. Huit phases.",
                 "LE KRAKEN SOMBRE")
    d["metadata"] = {"generator": "tools/levelgen/confins.py", "revision": 1, "rooms": b.rooms, "noHealing": True}
    L.save(os.path.join(ROOT, "data/levels/confins_03.json"))
    return width


# ---------------------------------------------------------------- the nest of ashes (2.56)
PHENIX_TEXTS = {
    3: ("Ses ailes arrachent la roche : les blocs tournoient !", "Aurore : les blocs en orbite"),
    5: "Cendres : le nid s'effondre sur les braises",
    7: "Comète : un soin",
    8: ("Le feu solaire referme l'arène !", "Supernova : l'arène se resserre"),
    "won": ("Le Phénix retombe en cendres, apaisé. La sortie est ouverte.", "Phénix vaincu"),
}
# The lights of the nest: gold of dawn high up, embers glowing low over the beds of ash.
PHENIX_LIGHTS = ((10, 26, "#ffd04a"), (48, 30, "#fff3c8"), (86, 26, "#ffd04a"), (30, 8, "#ff8a3a"), (66, 8, "#ff8a3a"))
# The sky of the Confins at dawn, gold and rose through the smoke (display only).
DAWN_TINTS = {"sky.png": "#ffc89a", "far.png": "#d8a088", "mid.png": "#b07a6a", "near.png": "#7a5450",
              "fg_embers.png": "#ffd08ac0"}


def phoenix_nest():
    b = Edge("confins_04", "Le Nid de Cendres", "pyralis", seed=8401)
    L, d = b.L, b.L.data
    d["description"] = ("Au sommet des Confins, un nid de braises sous un ciel d'aube : le Phénix Stellaire et ses huit "
                        "phases, dans une arène géante. Une seule vie, aucun checkpoint.")
    d["difficulty"] = 5
    d["extreme"] = True
    d["bossMusic"] = "music/phenix_boss.ogg"
    d["ambientLight"] = "#b49a8a"
    d["backgroundColor"] = "#1c0e10"
    for layer in d["background"]:
        name = layer["texture"].rsplit("/", 1)[-1]
        if name in DAWN_TINTS:
            layer["tint"] = DAWN_TINTS[name]
        if "fillBelow" in layer:
            layer["fillBelow"] = "#2a1210"
    b.f = 40
    # Only the fight, as the other giga bosses' levels: the choice of the powers, then the arena.
    b.th.update(floor="obsidian", roof="obsidian", accent="obsidian")
    b.opening("Le Nid de Cendres : le Phénix seul, une seule vie, aucun checkpoint. Choisissez vos quatre pouvoirs.")
    b.tablets(giga_powers("confins_04"))  # every power of the game
    b.zone_sign("Le Phénix Stellaire.")
    b.rooms.append({"type": "boss", "x": b.x, "floor": b.f, "boss": "phenix"})
    # It flies (it waits in the air): the crust gives way over two beds of embers at the ends, the
    # nest in the middle stays (where it burns to ashes and is reborn); the ledges at 10 and 15
    # tiles are perches of embers.
    b.giga_arena("phenix", "music/phenix_boss.ogg", pits=[(6, 22), (74, 90)], flying=True, texts=PHENIX_TEXTS,
                 style="embers", lights=PHENIX_LIGHTS, flicker=0.14, perches=(10, 15))
    width = b.finish("Le Phénix s'est endormi dans ses cendres. Une aube se lève sur les Confins.")
    ref = 360  # about six minutes of fight
    d["timer"] = {"mode": "challenge", "limit": 3600, "onExpire": "gameover", "referenceTime": ref, "challengeMaxDeaths": 0}
    d["medals"] = {"bronze": {}, "silver": {"maxTime": int(ref * 1.6)}, "gold": {"maxTime": int(ref * 1.2)},
                   "platinum": {"maxTime": ref, "maxDeaths": 0}}
    b.cinematics("LE NID DE CENDRES", "Les Confins", "Une seule vie. Un oiseau d'étoiles qui renaît de ses cendres. Huit phases.",
                 "LE PHÉNIX S'APAISE")
    d["metadata"] = {"generator": "tools/levelgen/confins.py", "revision": 1, "rooms": b.rooms, "noHealing": True}
    L.save(os.path.join(ROOT, "data/levels/confins_04.json"))
    return width


# ---------------------------------------------------------------- the crypt of names (2.57)
LICHE_TEXTS = {
    3: ("Ses sorts arrachent la roche : les blocs tournoient !", "Malédiction : les blocs en orbite"),
    5: "Phylactère brisé : la crypte s'effondre sur les ossuaires",
    7: "Crypte : un soin",
    8: ("Les âmes des morts referment l'arène !", "Néant : l'arène se resserre"),
    "won": ("La Liche retombe en poussière, enfin en paix. La sortie est ouverte.", "Liche vaincue"),
}
# The lights of the crypt: soul fire, green high up, violet low over the ossuaries.
LICHE_LIGHTS = ((10, 26, "#b4f566"), (48, 30, "#d8d0ff"), (86, 26, "#b4f566"), (30, 8, "#9a52f0"), (66, 8, "#9a52f0"))
# The sky of the Confins seen from the catacombs, grey-violet and cold (display only).
CRYPT_TINTS = {"sky.png": "#7a6e9a", "far.png": "#5a527a", "mid.png": "#443e60", "near.png": "#2e2a42",
               "fg_embers.png": "#b4f566a0"}
# The altars of the crypt (x, height above the floor): two on the ledges at 10 tiles (over the
# ossuaries), two on the floor between the ledges at 5 tiles (where its phylactery is set).
LICHE_ALTARS = ((18, 10), (37, 0), (59, 0), (78, 10))


def crypt_of_names():
    b = Edge("confins_05", "La Crypte des Noms", "pyralis", seed=8501)
    L, d = b.L, b.L.data
    d["description"] = ("Sous les Confins, des catacombes de verre noir où brûlent des bougies d'âme : la Liche des "
                        "Catacombes et ses huit phases, dans une arène géante. Une seule vie, aucun checkpoint.")
    d["difficulty"] = 5
    d["extreme"] = True
    d["bossMusic"] = "music/liche_boss.ogg"
    d["ambientLight"] = "#8a84a8"
    d["backgroundColor"] = "#0a0812"
    for layer in d["background"]:
        name = layer["texture"].rsplit("/", 1)[-1]
        if name in CRYPT_TINTS:
            layer["tint"] = CRYPT_TINTS[name]
        if "fillBelow" in layer:
            layer["fillBelow"] = "#14101e"
    b.f = 40
    # Only the fight, as the other giga bosses' levels: the choice of the powers, then the arena.
    b.th.update(floor="obsidian", roof="obsidian", accent="obsidian")
    b.opening("La Crypte des Noms : la Liche seule, une seule vie, aucun checkpoint. Choisissez vos quatre pouvoirs.")
    b.tablets(giga_powers("confins_05"))  # every power of the game
    b.zone_sign("La Liche des Catacombes.")
    b.rooms.append({"type": "boss", "x": b.x, "floor": b.f, "boss": "liche"})
    # It floats (it waits in the air): the crust gives way over two ossuaries at the ends, the
    # floor between its two floor altars stays; its altars, two on the floor, two on the ledges.
    b.giga_arena("liche", "music/liche_boss.ogg", pits=[(6, 22), (74, 90)], flying=True, texts=LICHE_TEXTS,
                 style="souls", lights=LICHE_LIGHTS, flicker=0.1, altars=LICHE_ALTARS)
    width = b.finish("La Liche s'est tue. Dans la crypte, les bougies brûlent encore, pour chaque nom.")
    ref = 360  # about six minutes of fight
    d["timer"] = {"mode": "challenge", "limit": 3600, "onExpire": "gameover", "referenceTime": ref, "challengeMaxDeaths": 0}
    d["medals"] = {"bronze": {}, "silver": {"maxTime": int(ref * 1.6)}, "gold": {"maxTime": int(ref * 1.2)},
                   "platinum": {"maxTime": ref, "maxDeaths": 0}}
    b.cinematics("LA CRYPTE DES NOMS", "Les Confins", "Une seule vie. Une liche qui n'a oublié aucun nom. Huit phases.",
                 "LA LICHE S'APAISE")
    d["metadata"] = {"generator": "tools/levelgen/confins.py", "revision": 1, "rooms": b.rooms, "noHealing": True}
    L.save(os.path.join(ROOT, "data/levels/confins_05.json"))
    return width


if __name__ == "__main__":
    print("confins_01 :", dragon_maw(), "tuiles")
    print("confins_02 :", stone_heart(), "tuiles")  # 2.49
    print("confins_03 :", abyss_trench(), "tuiles")  # 2.53
    print("confins_04 :", phoenix_nest(), "tuiles")  # 2.56
    print("confins_05 :", crypt_of_names(), "tuiles")  # 2.57
