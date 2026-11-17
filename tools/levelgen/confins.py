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

    def giga_arena(self, boss, music, pits, flying, texts, style="plasma", lights=FIRE_LIGHTS, flicker=0.12):
        """The giant arena (Star.GIANT_*) of a giga boss: the crust over each pit gives way at
        the 5th phase (magma below), debris orbit from the 3rd, fire closes in at the 8th and at
        the boss's rage. `pits`: (first, last) inside columns of each crust; `flying`: where the
        boss waits (in the air, or on the floor). 2.53: `style` of what fills the pits and closes
        in (« plasma », or « abyss »: the dark water of the Kraken), the arena's `lights`."""
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
                L.oneway(X + lx, X + lx + lw, f - h, ledge)
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


if __name__ == "__main__":
    print("confins_01 :", dragon_maw(), "tuiles")
    print("confins_02 :", stone_heart(), "tuiles")  # 2.49
    print("confins_03 :", abyss_trench(), "tuiles")  # 2.53
