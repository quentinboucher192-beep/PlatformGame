#!/usr/bin/env python3
"""Builds data/levels/sylvanox_03.json — "Le Cœur de la Machine", third level of Sylvanox.

A descent into the ancient machine hanging under the ruins: metal and crystal halls, lasers,
lifts, receptors and doors. New power: Grapple (Grappin). Six zones:
  1. Les Portes englouties     — receptor door, laser hall, lift, glide slalom down a laser shaft
  2. La Salle des Ancrages     — Grapple tablet, safe tutorial over a shallow pit, rising anchor chain
  3. La Fonderie des Gardiens  — combat arena (3 waves) of machine guardians
  4. Le Labyrinthe de Phase    — upper route: anchors and lasers over an acid basin (heart);
                                 lower route: phase barriers, plasma curtains, coolant pits (relic);
                                 then the airlock: plasma curtain, anchor, phase barrier
  5. Le Cœur de la Machine     — boss: Prime Sentinel (Sentinelle Prime) in a tall machine hall
  6. L'Ascenseur des Anciens   — lift (or anchors) up to the exit portal
Run from the repository root:  python3 tools/levelgen/sylvanox_03.py
"""
import os
import random

from levelkit import Level, ROOT
import sylvanox_common as C

random.seed(30303)
W, H = 482, 90
ACID = 86          # the abyss far below the machine
L = Level("sylvanox_03", "Le Cœur de la Machine", W, H)
L.planet("sylvanox")
d = L.data
d["description"] = ("Sous les ruines suspendues dort une machine colossale. Descendez dans ses halls de métal et de "
                    "cristal, maîtrisez le Grappin et éteignez la Sentinelle Prime qui garde son cœur.")
d["difficulty"] = 3
d["ambientLight"] = "#56628c"
d["backgroundColor"] = "#06080f"
d["killY"] = H * 16 + 24
# Inside the machine: a colder, darker sky and no foreground vines.
TINTS = {"sky.png": "#252c44", "far.png": "#30395a", "mid.png": "#3a446a", "abyss.png": "#5c6890",
         "near.png": "#465280"}
d["background"] = [l for l in d["background"] if not l.get("foreground")]
for layer in d["background"]:
    for k, v in TINTS.items():
        if layer["texture"].endswith(k):
            layer["tint"] = v
d["timer"] = {"mode": "limited", "limit": 1080, "onExpire": "gameover", "referenceTime": 330,
              "challengeMaxDeaths": 0}
d["medals"] = {"bronze": {}, "silver": {"maxTime": 600}, "gold": {"maxTime": 420},
               "platinum": {"maxTime": 330, "maxDeaths": 0}}
d["startPowers"] = ["dash", "wave", "shield", "impulse", "teleport", "glide"]

L.liquid(0, W, ACID, H, "acid")
L.emitter(0, ACID - 10, W, ACID, "abyss_motes", 6)

M = "ancient_metal"
X = "crystal"
AMBER, RED, CYAN, VIOLET = "#ffb347", "#ff4d6d", "#39f0d8", "#b04cff"


def machine(x0, x1, y, density=0.3, layer="bg_decor"):
    C.flora(L, x0, x1, y, density, layer, True, small=C.MACHINE_SMALL, tall_kinds=C.MACHINE_TALL)


def ceiling_dressing(x0, x1, y, every=5):
    for x in range(int(x0), int(x1), every):
        r = random.random()
        if r < 0.35:
            L.decor("banner_torn", x + random.random(), y, "bg_decor", rotation=180)
        elif r < 0.6:
            L.decor("hanging_vine_short", x + random.random(), y, "bg_decor", sway=1.0)
        elif r < 0.75:
            L.decor("lantern_pod", x + random.random(), y, "bg_decor", sway=0.8)


# =====================================================================================
# ZONE 1 — Les Portes englouties (receptor door, laser hall, lift, laser shaft)
# =====================================================================================
L.solid(0, 0, 3, H, "ruin_stone")
L.solid(3, 22, 36, 32, "ruin_stone")                # last ruins above the machine
start = L.start(7, 22)
L.decor("statue_head", 4.5, 22)
L.decor("ruin_arch", 16, 22)
L.decor("ruin_pillar", 23, 22)
L.decor("ruin_pillar_broken", 27, 22, flip=True)
C.flora(L, 8, 30, 22, 0.35)
L.light(16, 17, CYAN, 80)
L.sign(11, 22, "Sous les ruines dort une machine colossale... Le signal vient de son cœur.")
L.emitter(3, 10, 36, 21, "fireflies", 1.0)
L.shards_line(18, 19, 22, 19, 3)
L.enemy("glowmite", 26, 22, patrol_tiles=2)

gate1 = L.door(37, 16, 6)
L.receptor(33, 22, gate1)
L.sign(30, 22, "Une porte des Anciens : réveillez son récepteur avec l'Onde.")
L.solid(36, 2, 40, 16, M)                           # facade
L.solid(36, 22, 58, 32, M)                          # hall 1 floor
L.solid(36, 2, 100, 10, M)                          # machine roof
L.decor("holo_glyph", 35, 16, "bg_decor")
L.light(38, 13, AMBER, 70)

# hall 1: first laser, sentinel
L.laser(47, 10, 12, rotation=90, on=1.2, off=1.6, warn=0.6)
L.sign(43, 22, "Les lasers des Anciens s'éteignent un instant : passez entre deux tirs.")
L.enemy("glowmite", 52, 22, patrol_tiles=2)
L.enemy("sentinel", 54, 17, patrol_tiles=2)
machine(40, 56, 22, 0.45)
L.decor("tech_panel", 41, 22)
L.light(52, 14, AMBER, 70)
L.shards_line(44, 20, 50, 20, 3)

# the lift shaft
L.solid(50, 32, 58, 52, M)
L.solid(62, 10, 66, 34, M)
L.solid(58, 44, 62, 52, M)                          # sump under the lift (no crushing)
lift1 = L.moving(58, 22, 4, 0, 18, speed=45, wait=1.2)
L.sign(56, 22, "Un ascenseur des Anciens : montez dessus pour descendre.")
L.light(60, 30, AMBER, 60)
L.enemy("bomber", 60, 28, patrol_tiles=1)

# hall 2
L.solid(62, 40, 94, 50, M)                          # hall 2 floor (top 40)
L.solid(66, 10, 108, 30, M)                         # hall 2 ceiling mass
L.checkpoint(68, 40, "Portes englouties")
L.light(68, 35, CYAN, 90)
L.enemy("bulwark", 80, 40, patrol_tiles=4, face_left=True)
L.enemy("sentinel", 87, 35, patrol_tiles=2)
L.enemy("glowmite", 74, 40, patrol_tiles=2)
L.enemy("stalker", 85, 40, patrol_tiles=3, face_left=True)
L.orb("health_orb", 72, 38)
machine(66, 92, 40, 0.45)
L.decor("holo_glyph", 71, 40)
L.decor("pipe_broken", 89, 40, flip=True)
ceiling_dressing(67, 93, 30)
L.sign(91, 40, "Puits laser : planez et glissez entre les faisceaux.")
L.light(80, 33, AMBER, 70)
L.shards_line(70, 37, 78, 37, 4)

# the laser shaft: glide slalom between static beams, then a cycling one
L.solid(86, 50, 94, 64, M)                          # shaft left wall
L.solid(102, 30, 108, 56, M)                        # shaft right wall (upper)
L.solid(102, 60, 108, 61, M)                        # floor of the crystal pocket (exit opening below)
L.solid(107, 56, 108, 60, X)
L.gate(102, 56, 1, 4, "dash")                       # crystal pocket (relic 1)
L.relic(105, 58.5, "Engrenage d'aube")
L.secret(103, 56, 107, 60, "Une poche de cristal dans le puits !")
L.light(105, 58, "#e08aff", 60)
L.laser(94, 45, 5, rotation=0, off=0)               # from the left wall, gap on the right
L.solid(96, 52, 97, 53, M)                          # emitter stub in the middle of the shaft
L.laser(97, 52.5, 5, rotation=0, off=0)             # from the stub to the right wall, gap on the left
L.laser(94, 61, 6, rotation=0, on=1.0, off=1.5, warn=0.5)
L.shards_line(100.5, 43, 100.5, 47, 2)
L.shards_line(95.5, 50, 95.5, 54, 2)
L.shards_line(100.8, 57, 100.8, 60, 2)
L.light(98, 48, RED, 60)
L.light(98, 59, RED, 60)

# =====================================================================================
# ZONE 2 — La Salle des Ancrages (Grapple tablet, tutorial over a shallow pit, rising chain)
# =====================================================================================
L.solid(86, 64, 124, 74, M)                         # hall 3 floor (top 64)
L.solid(108, 14, 212, 22, M)                        # hall 3 ceiling
L.solid(108, 22, 112, 58, M)                        # hall 3 left pillar (ends above the shaft exit)
L.checkpoint(104, 64, "Salle des ancrages")
L.light(104, 59, CYAN, 90)
L.power(117, 62.7, "grapple", "Grappin : près d'un point d'ancrage, appuyez sur Interagir pour vous y "
                               "propulser. Saut pour lâcher en avance.")
L.light(117, 61, "#8ffff0", 110)
L.decor("ruin_arch", 117, 64)
L.sign(121, 64, "Grappin : approchez d'un anneau lumineux et appuyez sur Interagir.")
machine(112, 116, 64, 0.4)
L.shards_line(112, 62, 115, 62, 3)

# tutorial: a shallow pit (falling is harmless) and two anchors up to a high ledge
L.solid(124, 67, 137, 74, M)                        # pit floor (3 below the hall)
L.grapple(129, 58)
L.grapple(135, 51)
L.solid(137, 50, 150, 74, X)                        # high crystal ledge (14 above the hall floor)
L.decor("crystal_big", 140, 50)
L.decor("crystal_cluster", 148, 50)
L.sign(126, 67, "Pas de danger ici : remontez à gauche et réessayez.")
L.shards_line(126, 61, 128, 59, 2)
L.shards_line(131, 56, 134, 53, 2)
L.light(129, 58, CYAN, 60)
L.light(135, 51, CYAN, 60)

# secret: a hidden anchor above the ledge, up to a crystal shelf (relic 2)
L.grapple(146, 37.4)
L.oneway(141, 151, 36, "tech_ledge")
L.solid(150, 30, 152, 36, X)
L.relic(144, 34.5, "Anneau des ancrages")
L.secret(140, 30, 151, 36, "Une étagère oubliée sous la voûte !")
L.shards_line(146, 47, 146, 41, 3)
L.decor("crystal_cluster", 148, 36)
L.light(144, 33, "#ffd98a", 60)

# rising anchor chain over the abyss
L.sign(148, 50, "Enchaînez les ancrages : chaque traction rend vos sauts aériens.")
chain = [(157, 46), (165, 42), (173, 38), (181, 34)]
for (gx, gy) in chain:
    L.grapple(gx, gy)
    L.light(gx, gy, CYAN, 50)
L.shards_arc(151, 186, 40, 4, 7)
L.enemy("wisp", 169, 47, patrol_tiles=3)
L.enemy("sentinel", 178, 28, patrol_tiles=3)

L.solid(186, 33, 202, 40, X)                        # crystal balcony (top 33)
L.decor("crystal_big", 200, 33, flip=True)
L.checkpoint(189, 33, "Ancrages")
L.light(189, 28, CYAN, 90)
L.enemy("burrower", 198, 33, face_left=True)
L.orb("health_orb", 194, 31)
L.shards_line(190, 31, 196, 31, 3)
L.shards_line(204, 36, 204, 48, 4)
machine(186, 201, 33, 0.3)
L.emitter(112, 30, 186, 60, "fireflies", 1.5)

# =====================================================================================
# ZONE 3 — La Fonderie des Gardiens (combat arena)
# =====================================================================================
L.solid(196, 52, 262, 62, M)                        # foundry floor (top 52)
L.orb("health_orb", 205, 50)
L.sign(204, 52, "La fonderie des gardiens... Ils s'éveillent.")
L.enemy("glowmite", 199, 52, patrol_tiles=2)
L.enemy("sentinel", 202, 45, patrol_tiles=2)
L.enemy("bomber", 207, 43, patrol_tiles=2)
L.solid(206, 26, 258, 32, M)                        # arena ceiling
L.solid(208, 32, 211, 44, M)
L.solid(255, 32, 258, 44, M)
a_left = L.door(210, 44, 8, open_=True)
a_right = L.door(255, 44, 8)
L.oneway(215, 222, 46, "tech_ledge")
L.oneway(244, 251, 46, "tech_ledge")
L.oneway(227, 239, 40, "tech_ledge")
L.moving(218, 35, 3, 26, 0, speed=40, wait=0.8)
w1 = [L.enemy("sentinel", 218, 40, 3, dormant=True), L.enemy("sentinel", 248, 40, 3, True, dormant=True),
      L.enemy("glowmite", 225, 52, 3, dormant=True), L.enemy("glowmite", 241, 52, 3, True, dormant=True),
      L.enemy("burrower", 233, 52, dormant=True)]
w2 = [L.enemy("bulwark", 222, 52, 4, dormant=True), L.enemy("bulwark", 246, 52, 4, True, dormant=True),
      L.enemy("bomber", 229, 36, 4, dormant=True), L.enemy("bomber", 240, 36, 4, True, dormant=True)]
w3 = [L.enemy("golem", 233, 52, 3, True, dormant=True), L.enemy("wisp", 219, 38, 3, dormant=True),
      L.enemy("wisp", 247, 38, 3, True, dormant=True), L.enemy("stalker", 215, 52, 4, dormant=True),
      L.enemy("stalker", 251, 52, 4, True, dormant=True)]
L.combat(211, 32, 255, 52, doors=a_left + "," + a_right, waves=";".join(",".join(w) for w in (w1, w2, w3)))
L.orb("health_orb", 218.5, 44)
L.orb("health_orb", 247.5, 44)
L.orb("health_orb", 233, 38)
L.orb("energy_orb", 230, 38)
L.orb("energy_orb", 236, 38)
L.light(233, 36, RED, 140, flicker=0.25)
L.light(214, 48, AMBER, 60)
L.light(252, 48, AMBER, 60)
machine(212, 254, 52, 0.35)
ceiling_dressing(212, 254, 32, every=4)
L.decor("pipe_broken", 225, 52)
L.decor("pipe_broken", 241, 52, flip=True)

# =====================================================================================
# ZONE 4 — Le Labyrinthe de Phase (two routes, then the airlock)
# =====================================================================================
L.solid(258, 52, 266, 62, M)                        # split platform (left part, top 52)
L.solid(270, 52, 278, 58, M)                        # split platform (right part); hatch between
L.checkpoint(261, 52, "Fonderie")
L.light(261, 47, CYAN, 90)
L.shards_line(258.5, 50, 264.5, 50, 3)
L.sign(272, 52, "Deux voies : les ancrages au-dessus de l'acide, ou la trappe vers les coursives de phase.")
L.light(268, 50, AMBER, 50)

# --- the acid basin under the upper gallery (roof of the lower corridor)
L.solid(270, 58, 350, 62, M)                        # corridor roof = basin bottom
L.liquid(278, 346, 54, 58, "acid")
L.solid(346, 52, 350, 58, M)                        # basin right wall
L.emitter(278, 50, 346, 54, "abyss_motes", 3)

# --- upper route: anchors and lasers over the acid, two walls to clear (heart in the vault)
L.solid(270, 22, 302, 28, M)                        # gallery ceiling (with a vault in it)
L.solid(310, 22, 352, 28, M)
L.solid(302, 18, 310, 24, M)
L.grapple(283, 45)
L.grapple(290, 40)
L.oneway(287, 292, 47, "tech_ledge")                # rest ledge under the second anchor
L.solid(296, 36, 300, 58, M)                        # wall 1
L.grapple(296, 32)
L.laser(298, 28, 8, rotation=90, on=1.0, off=1.8, warn=0.6)
L.oneway(304, 310, 44, "tech_ledge")                # rest ledge between the walls
L.checkpoint(308.5, 44, "Galerie des ancrages")
L.grapple(315, 39)
L.solid(322, 36, 326, 58, M)                        # wall 2
L.grapple(322, 32)
L.laser(324, 28, 8, rotation=90, on=1.0, off=1.8, warn=0.6, phase=1.4)
L.grapple(336, 44)
L.enemy("sentinel", 302, 38, patrol_tiles=2)
L.enemy("sentinel", 331, 40, patrol_tiles=3)
L.enemy("wisp", 342, 46, patrol_tiles=2)
L.shards_line(283, 48, 290, 43, 3)
L.shards_line(301, 34, 308, 41, 3)
L.shards_line(328, 36, 344, 48, 4)
L.light(298, 40, RED, 70)
L.light(324, 40, RED, 70)
# heart vault in the ceiling: hidden anchor, plasma curtain inside
L.grapple(304, 30.5)                                # face left at the ledge's end, jump, grapple
L.oneway(302, 310, 29, "tech_ledge")
L.gate(306, 24, 1, 5, "shield")
L.heart(308.5, 27.5)
L.secret(302, 24, 310, 29, "Un cœur de cristal dans la voûte !")
L.light(308.5, 27, "#ff8aa0", 60)

# --- lower route: hatch, phase barrier, coolant pit, plasma curtain, lasers, guards
L.solid(256, 62, 258, 72, M)
L.solid(256, 70, 284, 80, M)                        # corridor floor (top 70)
L.solid(296, 70, 364, 80, M)
L.liquid(284, 296, 71, 80, "acid")                  # coolant pit
L.gate(276, 62, 1, 8, "teleport")
L.moving(285, 70, 3, 8, 0, speed=45, wait=0.6)
L.enemy("sentinel", 290, 66, patrol_tiles=2)
L.gate(302, 62, 1, 8, "shield")
L.enemy("sentinel", 307, 66, patrol_tiles=1, face_left=True)
L.laser(312, 62, 8, rotation=90, on=1.1, off=1.5, warn=0.5)
L.laser(318, 62, 8, rotation=90, on=1.1, off=1.5, warn=0.5, phase=1.3)
L.enemy("bulwark", 326, 70, patrol_tiles=3, face_left=True)
L.enemy("burrower", 336, 70, face_left=True)
L.enemy("glowmite", 273, 70, patrol_tiles=2)
L.enemy("weaver", 331, 70, patrol_tiles=2, face_left=True)
L.enemy("glowmite", 344, 70, patrol_tiles=2, face_left=True)
L.orb("health_orb", 297.5, 68)
L.orb("energy_orb", 300, 68)
L.shards_line(279, 68, 282, 68, 2)
L.shards_line(305, 68, 309, 68, 2)
L.checkpoint(306, 70, "Coursives de phase")
L.light(306, 66, CYAN, 70)
L.shards_line(321, 68, 333, 68, 4)
L.shards_line(352, 66, 352, 56, 3)
for x in (280, 299, 315, 329, 343):
    L.light(x, 64, AMBER, 50)
L.decor("tech_panel", 272, 70)
L.decor("pipe_broken", 300, 70)
L.decor("tech_panel", 340, 70)
L.sign(270, 70, "Coursives de phase : téléportez-vous à travers la barrière.")
# exit shaft up to the merge (anchors) and the phase vault (relic 3) at the end of the corridor
L.grapple(352, 61)
L.grapple(352, 53)
L.gate(356, 62, 1, 8, "teleport")
L.solid(362, 62, 364, 70, M)
L.relic(360.8, 68.5, "Prisme de la machine")
L.secret(357, 62, 362, 70, "La chambre de phase !")
L.light(359.5, 67, "#e08aff", 60)

# --- merge and the airlock: plasma curtain, anchor over a pit, phase barrier
L.solid(354, 52, 372, 62, M)                        # merge platform (top 52)
L.solid(362, 30, 372, 44, M)                        # its ceiling (gliders land on its left part)
L.checkpoint(358, 52, "Labyrinthe de phase")
L.light(358, 47, CYAN, 90)
L.orb("health_orb", 363, 50)
L.shards_line(355, 50, 359, 50, 3)
L.shards_arc(372, 382, 47, 2, 4)
L.enemy("golem", 366, 52, patrol_tiles=2, face_left=True)
L.gate(371, 44, 1, 8, "shield")
L.grapple(377, 46)
L.solid(382, 52, 394, 58, X)                        # far platform (crystal)
L.decor("crystal_cluster", 384, 52)
L.solid(382, 36, 396, 44, M)                        # its ceiling
L.gate(389, 44, 1, 8, "teleport")
L.sign(384, 52, "Le sas du cœur : bouclier, grappin, téléportation.")
L.light(377, 50, CYAN, 60)
L.enemy("wisp", 378, 40, patrol_tiles=2)
machine(354, 370, 52, 0.25)

# =====================================================================================
# ZONE 5 — Le Cœur de la Machine (boss: Sentinelle Prime)
# =====================================================================================
L.solid(386, 58, 394, 76, M)                        # drop shaft left wall
L.solid(394, 74, 456, 84, M)                        # arena floor (top 74)
L.checkpoint(396, 74, "Cœur de la machine")
L.light(396, 69, CYAN, 90)
L.orb("health_orb", 395.5, 66)
L.shards_line(396, 56, 396, 68, 4)
AL, AR = 398, 454
L.solid(AL, 36, AR + 2, 44, M)                      # vault of the core hall (30 tiles above the floor)
L.solid(AL, 44, 401, 66, M)
L.solid(451, 44, AR, 66, M)
boss_l = L.door(399, 66, 8, open_=True)
boss_r = L.door(452, 66, 8)
L.oneway(407, 414, 67, "tech_ledge")
L.oneway(438, 445, 67, "tech_ledge")
L.oneway(420, 432, 61, "tech_ledge")
L.oneway(408, 414, 55, "tech_ledge")
L.oneway(438, 444, 55, "tech_ledge")
boss = L.enemy("prime_sentinel", 436, 64, patrol_tiles=8, face_left=True, dormant=True, boss=True)
C.boss_arena(L, boss, boss_l, boss_r, 401, 44, 451, 74, d["bossMusic"], zoom=0.72, cam=(AL, 36, AR + 2, 78))
L.orb("health_orb", 410.5, 65)
L.orb("health_orb", 441.5, 65)
L.orb("energy_orb", 426, 59)
L.orb("health_orb", 411, 53)
L.decor("ruin_arch", 426, 74, "bg_decor", scale=2.0)
L.decor("crystal_big", 404, 74)
L.decor("crystal_big", 448, 74, flip=True)
L.decor("tech_panel", 416, 74)
L.decor("tech_panel", 436, 74)
L.decor("holo_glyph", 421, 74)
L.decor("holo_glyph", 431, 74)
L.light(426, 52, AMBER, 180, flicker=0.05)
L.light(406, 70, RED, 70)
L.light(446, 70, RED, 70)
ceiling_dressing(402, 450, 44, every=4)
L.event(boss, "bossDefeated", [
    {"type": "hint", "params": {"text": "La Sentinelle Prime s'éteint... L'ascenseur des Anciens se réveille.",
                                "duration": 4}},
], delay=1.5, name="Sentinelle vaincue")
L.event(boss, "bossPhase", [{"type": "shake", "params": {"amount": 0.45}}], once=False)

# =====================================================================================
# ZONE 6 — L'Ascenseur des Anciens (lift or anchors up to the exit)
# =====================================================================================
L.solid(AR, 74, 459, 84, M)
L.solid(459, 75, W, 84, M)
L.checkpoint(456, 74, "Ascenseur des Anciens")
L.light(456, 69, CYAN, 90)
L.solid(AR, 44, 459, 66, M)
lift2 = L.moving(459, 74, 4, 0, -24, speed=55, wait=1.0, triggered=True)
L.grapple(461, 64)
L.grapple(461, 56)
L.grapple(461, 48)
L.solid(463, 50, W, 74, M)                          # exit terrace (top 50)
L.solid(456, 26, W, 36, M)                          # roof
L.sign(458, 74, "L'ascenseur démarre quand vous montez dessus.")
L.decor("ruin_arch", 472, 50, "bg_decor", scale=1.25)
exit_id = L.exit(472, 50)
L.light(472, 46, "#ffd98a", 140)
L.decor("holo_glyph", 466, 50)
L.decor("holo_glyph", 478, 50)
L.emitter(463, 38, W, 50, "fireflies", 2)
L.shards_line(461, 70, 461, 54, 4)

# =====================================================================================
# Background dressing
# =====================================================================================
C.bg_rocks(L, 30, W - 30, 34, 60, every=47, tint="#8890b8", alpha=0.6)
# ceilings: torn banners, hanging cables (vines) and lanterns of the overgrown machine
ceiling_dressing(40, 58, 10, every=4)
ceiling_dressing(112, 186, 22, every=6)
ceiling_dressing(278, 352, 28, every=6)
ceiling_dressing(262, 350, 62, every=7)
ceiling_dressing(362, 372, 44, every=3)
ceiling_dressing(382, 396, 44, every=3)
ceiling_dressing(456, W, 36, every=5)
# vines and roots hanging under the floating machine blocks
C.hanging_under(L, ACID - 1, kinds=["hanging_vine_short", "roots", "hanging_vine_long"], every=6,
                skip=lambda x, b: b < 40 and x > 250)
# ancient guardians, pillars and consoles along the halls
for (name, x, y, fl) in [("statue_head", 44, 22, False), ("ruin_pillar", 64, 40, False), ("statue_head", 92, 40, True),
                         ("ruin_pillar", 113, 64, False), ("holo_glyph", 120, 64, False), ("ruin_pillar_broken", 195, 33, False),
                         ("statue_head", 199, 52, True), ("ruin_pillar", 213, 52, False), ("ruin_pillar", 253, 52, False),
                         ("holo_glyph", 263, 52, False), ("tech_panel", 276, 52, False), ("crystal_big", 360, 52, False),
                         ("holo_glyph", 369, 52, False), ("ruin_pillar_broken", 391, 52, True), ("statue_head", 467, 50, False)]:
    L.decor(name, x, y, flip=fl)
for x in (60, 98, 230, 300, 352, 426, 461):
    L.emitter(x - 4, 30, x + 4, 60, "fireflies", 0.8)
L.emitter(296, 62, 340, 70, "laser_sparks", 0.6)
L.emitter(86, 20, 106, 40, "abyss_motes", 0.8)

# =====================================================================================
# Events and cinematics
# =====================================================================================
L.event("level", "levelStart", [{"type": "hint", "params": {
    "text": "Descendez jusqu'au cœur de la machine.", "duration": 4}}])
L.event("grapple", "unlocked", [{"type": "hint", "params": {
    "text": "Le Grappin rend vos sauts aériens : enchaînez traction, saut et Planeur.", "duration": 5}}], delay=6)
L.event(lift2, "activate", [{"type": "playSound", "params": {"sound": "sounds/door_open.wav"}}])
L.event("level", "time", [{"type": "hint", "params": {"text": "Plus que 5 minutes !", "duration": 3}}],
        params={"seconds": 780})

d["intro"] = {"skippable": True, "letterbox": True, "steps": [
    {"type": "fade", "duration": 0.01, "params": {"to": 1}},
    {"type": "cut", "duration": 0.01, "params": {"x": 300, "y": 220}},
    {"type": "zoom", "duration": 0.01, "params": {"zoom": 0.8}},
    {"type": "fade", "duration": 1.6, "wait": False, "params": {"to": 0}},
    {"type": "text", "duration": 4.0, "wait": False, "params": {"text": "SYLVANOX", "subtitle": "Le Cœur de la Machine"}},
    {"type": "camera", "duration": 4.5, "params": {"x": 1000, "y": 520, "ease": "inOutSine"}},
    {"type": "text", "duration": 3.5, "wait": False, "params": {
        "text": "Sous les ruines, la machine des Anciens n'a jamais cessé de battre.", "style": "caption"}},
    {"type": "camera", "duration": 3.5, "params": {"x": 2100, "y": 760, "ease": "inOutCubic"}},
    {"type": "wait", "duration": 0.5},
    {"type": "zoom", "duration": 3.0, "wait": False, "params": {"zoom": 1.0}},
    {"type": "camera", "duration": 3.0, "params": {"start": True, "ease": "inOutCubic"}},
    {"type": "spawnPlayer", "duration": 0.9},
    {"type": "dialogue", "duration": 3.6, "params": {"speaker": "Nova", "text":
        "Le signal bat comme un cœur... Il vient du fond de cette machine."}},
]}
d["outro"] = {"skippable": True, "letterbox": True, "steps": [
    {"type": "zoom", "duration": 1.4, "wait": False, "params": {"zoom": 1.4}},
    {"type": "camera", "duration": 1.4, "params": {"player": True}},
    {"type": "text", "duration": 3.0, "wait": False, "params": {"text": "CŒUR APAISÉ",
                                                               "subtitle": "Sylvanox — Le Cœur de la Machine"}},
    {"type": "playerAnim", "duration": 2.2, "params": {"anim": "victory"}},
    {"type": "dialogue", "duration": 3.2, "params": {"speaker": "Nova", "text":
        "La machine se tait. Le signal, lui, part vers les étoiles... vers Pyralis."}},
    {"type": "flash", "duration": 0.6, "params": {"color": "#e6fffb"}},
    {"type": "fade", "duration": 0.9, "params": {"to": 1}},
]}
d["metadata"] = {"generator": "tools/levelgen/sylvanox_03.py", "revision": 2}

L.save(os.path.join(ROOT, "data/levels/sylvanox_03.json"))
