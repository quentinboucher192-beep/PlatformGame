#!/usr/bin/env python3
"""Builds the levels of Les Confins (version 2.48), the world beyond the dying star, opened once
« Le Cœur d'une Étoile Mourante » (noyau_03) is completed:

  data/levels/confins_01.json  "La Gueule du Dragon" (EXTRÊME): one life, no checkpoint. The
                                scorched crust of the Confins (its hardest rooms, one after the
                                other), the choice of the powers, then the giant arena of Pyrrhax,
                                le Dragon d'Obsidienne (8 phases, tools/contentgen/pyrrhax.py)

The rooms are the calibrated rooms of the trials (challenge.py), with Pyralis' materials and the
obsidian of the dead star; the arena is the giant arena of Singularis (stellar_core.Star) answering
the dragon's phases. Run from the repository root after tools/contentgen/pyrrhax.py:
  python3 tools/levelgen/confins.py
"""
import os

from challenge import HEADROOM
from levelkit import ROOT, T
from stellar_core import Star


class Edge(Star):
    """The builder of Star on the planet of Les Confins."""

    def __init__(self, level_id, name, first_theme="pyralis", seed=8101):
        super().__init__(level_id, name, first_theme, seed)
        self.L.planet("confins")

    def dragon_arena(self, boss, music=""):
        """The giant arena (Star.GIANT_*): its floor gives way at the 5th phase, debris torn off
        by the wings orbit from the 3rd, fire closes in at the 8th and at the dragon's rage."""
        L, f = self.L, self.f
        x0 = self.x
        X = x0 + 1
        W, Hh = self.GIANT_W, self.GIANT_H
        door_h = 8
        ledge = "sanctum_ledge"
        b0, b1 = self.GIANT_BASIN
        self.roof(x0, X + W + 1, f - Hh)
        L.solid(x0, f - Hh, x0 + 1, f - door_h, self.th["accent"], deco=False)
        L.solid(X + W, f - Hh, X + W + 1, f - door_h, self.th["accent"], deco=False)
        left = L.door(x0, f - door_h, door_h, open_=True)
        right = L.door(X + W, f - door_h, door_h, open_=False)
        self.ground(x0, X + b0, f)
        self.ground(X + b1, X + W + 1, f)
        self.ground(X + b0, X + b1, f + 4)
        # The obsidian crust over the magma: it gives way when the dragon's scales burst (phase 5).
        crust = []
        for cx in range(b0, b1, 4):
            oid = L.crumble(X + cx, f, 4, delay=0.3, respawn=0.0, style="basalt")
            L.data["objects"][-1]["props"]["onEvent"] = True
            crust.append(oid)
        magma = L.obj("damage_zone", [(X + b0) * T, (f + 2) * T, (b1 - b0) * T, 2 * T], "gameplay",
                      {"damage": 2, "interval": 0.6, "visible": True, "shieldProtects": True, "active": False,
                       "style": "plasma", "bounce": True})
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
                          "style": "plasma", "bounce": False})
        curtain1 = [curtain(0, 12), curtain(W - 12, W)]
        curtain2 = [curtain(12, 22), curtain(W - 22, W - 12)]
        bid = L.enemy(boss, X + 70, f - 20, patrol_tiles=6, face_left=True, dormant=True, boss=True)
        L.combat(X, f - Hh, X + W, f + 4, doors=left + "," + right, waves=bid, lock=False,
                 music=music or L.data.get("bossMusic", ""))
        L.data["objects"][-1]["props"]["announce"] = False
        for (lx, lh, col) in ((10, 26, "#ff7a2e"), (48, 30, "#ffc49a"), (86, 26, "#ff7a2e"), (30, 8, "#ff5ad2"),
                              (66, 8, "#ff5ad2")):
            L.light(X + lx, f - lh, col, 160, flicker=0.12)
        fire = [magma] + curtain1 + curtain2
        L.event(bid, "bossPhase", [{"type": "activate", "target": ",".join(debris)},
                                   {"type": "spawn", "target": orbs[3]},
                                   {"type": "hint", "params": {"text": "Ses ailes arrachent la roche : les blocs tournoient !",
                                                              "duration": 3}}],
                params={"phase": 3}, name="Tempête d'ailes : les blocs en orbite")
        L.event(bid, "bossPhase", [{"type": "activate", "target": ",".join(crust)},
                                   {"type": "enable", "target": magma},
                                   {"type": "spawn", "target": orbs[5]},
                                   {"type": "shake", "params": {"amount": 0.8}}],
                params={"phase": 5}, name="Écailles brisées : le sol cède")
        L.event(bid, "bossPhase", [{"type": "spawn", "target": orbs[7]}], params={"phase": 7}, name="Couvée : un soin")
        L.event(bid, "bossPhase", [{"type": "enable", "target": ",".join(curtain1)},
                                   {"type": "hint", "params": {"text": "Le feu referme l'arène !", "duration": 3}}],
                params={"phase": 8}, name="Soleil noir : l'arène se resserre")
        L.event(bid, "bossEnrage", [{"type": "enable", "target": ",".join(curtain2)},
                                    {"type": "shake", "params": {"amount": 0.6}}], name="Rage : l'arène se resserre encore")
        L.event(bid, "bossDefeated", [{"type": "disable", "target": ",".join(fire)},
                                      {"type": "hint", "params": {"text": "Le dragon retourne à la pierre. La sortie est ouverte.",
                                                                 "duration": 4}}],
                delay=1.5, name="Pyrrhax vaincu")
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
    d["description"] = ("Aux Confins du système, une croûte d'obsidienne sur un feu vivant. Ses épreuves les plus "
                        "dures sans pardon, puis Pyrrhax et ses huit phases. Une seule vie, aucun checkpoint.")
    d["difficulty"] = 5
    # EXTRÊME: one life (the challenge mode below allows no death) and a race online.
    d["extreme"] = True
    b.f = 40
    b.opening("La Gueule du Dragon : une seule vie, aucun checkpoint. Pyrrhax attend au bout.")
    # The scorched crust: hard rooms, one after the other (dash, grapple, glide and the charged
    # shot are carried from the start: levelkit.START_POWERS).
    b.zone_sign("La croûte brûlée des Confins.")
    b.rooms_of(["metronome_fast", "dash_walls", "vents", "anchors"])
    b.zone_sign("Les veines de feu.")
    b.rooms_of(["pistons", "frail_bridges_fast", "foundry", "lasers"])
    # Before the maw: the powers of the fight (four carried; what is left stays here).
    b.th.update(floor="obsidian", roof="obsidian", accent="obsidian")
    b.zone_sign("Le seuil de la gueule. Choisissez les pouvoirs du combat : celui que vous laissez reste ici.")
    b.tablets(["frost_nova", "plasma_bomb", "wave", "shield", "triple_jump", "time_slow"])
    platforming = b.x
    b.zone_sign("Pyrrhax, le Dragon d'Obsidienne.")
    b.rooms.append({"type": "boss", "x": b.x, "floor": b.f, "boss": "pyrrhax"})
    b.dragon_arena("pyrrhax", music="music/pyrrhax_boss.ogg")
    width = b.finish("Le dragon s'est tu. Les Confins s'éteignent.")
    # Reference: the rooms at a good pace, then about six minutes of fight.
    ref = int(round(platforming * T / 125.0 + 360))
    d["timer"] = {"mode": "challenge", "limit": 3600, "onExpire": "gameover", "referenceTime": ref, "challengeMaxDeaths": 0}
    d["medals"] = {"bronze": {}, "silver": {"maxTime": int(ref * 1.6)}, "gold": {"maxTime": int(ref * 1.2)},
                   "platinum": {"maxTime": ref, "maxDeaths": 0}}
    b.cinematics("LA GUEULE DU DRAGON", "Les Confins", "Une seule vie. Un dragon d'obsidienne. Huit phases.",
                 "LE DRAGON S'ÉTEINT")
    d["metadata"] = {"generator": "tools/levelgen/confins.py", "revision": 1, "rooms": b.rooms, "noHealing": True}
    L.save(os.path.join(ROOT, "data/levels/confins_01.json"))
    return width


if __name__ == "__main__":
    print("confins_01 :", dragon_maw(), "tuiles")
