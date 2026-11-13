#!/usr/bin/env python3
"""Builds data/levels/glacior_02.json — "Les Grottes de Cristal", second level of Glacior.

Six zones under the ice: the cave mouth (intro, icicle ceilings, first dive), the underground
lake (swimming, islands, sunken heart), the grapple wells (vertical shaft + combat arena), the
shimmering gallery (advanced platforming with every power, two routes), the cavern of the Frost
Hydra (boss) and the climb back to the aurora (exit). Run from the repository root:
    python3 tools/levelgen/glacior_02.py
"""
import os
import random

from levelkit import Level, ROOT
from glacior_common import (auto_flora, frost_cloud, AURORA_GREEN, AURORA_VIOLET, FROST_CYAN, ICE_BLUE, DANGER, deco, flora, icicles,
                            lamp, crystal_light, ice_spikes_wall, ceiling_spikes, boss_arena)

rng = random.Random(4202)
W, H = 486, 84
L = Level("glacior_02", "Les Grottes de Cristal", W, H)
L.planet("glacior")
d = L.data
d["description"] = ("Sous les plaines gelées s'ouvrent des grottes de cristal inondées. Nagez, grimpez les puits au "
                    "grappin et affrontez l'Hydre de Givre qui garde les profondeurs.")
d["difficulty"] = 5
d["killY"] = H * 16 + 24
d["ambientLight"] = "#7488b8"
d["backgroundColor"] = "#060c1a"
# Cave backdrop: the Glacior parallax darkened into distant crystal walls (no sky, no moon).
d["background"] = [
    {"texture": "backgrounds/glacior/far.png", "factor": [0.1, 0.05], "offset": [0, -40], "tint": "#3a4c78"},
    {"texture": "backgrounds/glacior/mid.png", "factor": [0.25, 0.12], "offset": [0, 0], "tint": "#2a3a62"},
    {"texture": "backgrounds/glacior/abyss.png", "factor": [0.4, 0.3], "offset": [0, 230], "fillBelow": "#24406a",
     "tint": "#7a90c8"},
    {"texture": "backgrounds/glacior/fg_icicles.png", "factor": [1.15, 1.0], "offset": [0, -330], "foreground": True,
     "tint": "#ffffffb0"},
]
d["timer"] = {"mode": "limited", "limit": 1100, "onExpire": "gameover", "referenceTime": 360, "challengeMaxDeaths": 0}
d["medals"] = {"bronze": {}, "silver": {"maxTime": 720}, "gold": {"maxTime": 480},
               "platinum": {"maxTime": 360, "maxDeaths": 0}}
d["startPowers"] = ["dash", "wave", "shield", "impulse", "teleport", "glide", "grapple", "ground_pound", "charge_shot",
                    "triple_jump"]

SNOW, ICE, METAL = "snow_rock", "glacier_ice", "frost_metal"


def rock(x0, y0, x1, y1, mat=SNOW, deco_top=True):
    return L.solid(x0, y0, x1, y1, mat, deco=deco_top)


def ceiling(x0, x1, bottom, top=0, mat=SNOW, hang=True):
    """Cave ceiling mass whose bottom edge is row `bottom`, with hanging icicles."""
    oid = L.solid(x0, top, x1, bottom, mat, deco=False)
    if hang:
        icicles(L, rng, x0, x1, bottom, every=3.4, long_ratio=0.3)
    return oid


def water(x0, x1, y0, y1):
    return L.liquid(x0, x1, y0, y1, "water")


def ledge(x0, x1, y, style="ice_ledge"):
    oid = L.oneway(x0, x1, y, style)
    icicles(L, rng, x0, x1, y + 1, every=3.6, long_ratio=0.0)
    return oid


def glow(x, y_ground, big=False, color=FROST_CYAN):
    crystal_light(L, x, y_ground, big=big, color=color, radius=70 if big else 50)


L.emitter(0, 0, W, H, "frost_mist", 4)

# =====================================================================================
# ZONE 1 — La Bouche des Grottes (intro: snowy surface, icicle ceilings, first dive)
# =====================================================================================
rock(0, 0, 3, H)
rock(3, 40, 30, H)
start = L.start(7, 40)
L.emitter(3, 10, 28, 38, "snowfall", 3)
L.emitter(3, 20, 28, 38, "aurora_motes", 1)
deco(L, "snow_pine", 4.5, 40, scale=1.2)
deco(L, "frozen_tree", 11, 40)
deco(L, "frozen_statue", 21, 40)
lamp(L, 15, 40)
L.sign(13, 40, "Les grottes de cristal... Le signal du relais résonne sous la glace.")
flora(L, rng, 3, 30, 40, 0.3, tall=False, avoid=[(12, 16), (6, 8)])
# cave mouth
ceiling(24, 46, 28)
ceiling(46, 62, 34)
ceiling(62, 100, 30, hang=False)
rock(30, 44, 46, H)
deco(L, "ice_arch", 27, 40, alpha=0.9)
L.enemy("frost_mite", 38, 44, patrol_tiles=4)
L.enemy("frost_wing", 54, 38, patrol_tiles=4)
glow(33, 44)
flora(L, rng, 30, 46, 44, 0.35, avoid=[(41, 43)], cave=True)
# pit under an icicle ceiling: a single jump only
L.sign(42, 44, "Stalactites ! Un simple saut suffit : gardez la tête basse.")
ceiling_spikes(L, 46, 52, 37)
L.solid(46, 34, 52, 37, SNOW, deco=False)
L.shards_arc(46, 50, 41.5, 1.5, 3)
rock(50, 44, 62, H)
L.enemy("ice_gloop", 57, 44, patrol_tiles=3, face_left=True)
L.emitter(46, 70, 50, 82, "abyss_motes", 1)
flora(L, rng, 50, 62, 44, 0.3, cave=True)
# first dive under a rock curtain
rock(62, 58, 82, H)                            # pool floor
water(62, 82, 48, 58)
rock(68, 30, 72, 53, SNOW, deco_top=False)      # rock curtain dipping into the water
L.sign(60, 44, "Plongez sous la roche : maintenez Saut pour remonter à la surface.")
L.shards_line(67, 55.5, 73, 55.5, 4)
glow(76, 58)
rock(82, 48, 100, H)
cp1 = L.checkpoint(88, 48, "Bouche des grottes")
L.light(88, 43, FROST_CYAN, 90)
L.orb("health_orb", 85, 46)
flora(L, rng, 82, 100, 48, 0.4, avoid=[(87, 89)], cave=True)
icicles(L, rng, 62, 100, 30, every=3.0, long_ratio=0.3)

# =====================================================================================
# ZONE 2 — Le Lac Souterrain (swimming, low islands, frost weavers, sunken heart)
# =====================================================================================
LS = 50                                         # lake surface
ceiling(100, 180, 32)
rock(100, 72, 180, H)                           # lake bed
# water split around the low islands (islands are flush with the surface: swimmers climb out)
islands = [(108, 114), (124, 131), (142, 148), (158, 164)]
xs = [100] + [v for a, b in islands for v in (a, b)] + [172]
for i in range(0, len(xs), 2):
    water(xs[i], xs[i + 1], LS, 72)
for a, b in islands:
    L.solid(a, LS, b, LS + 3, SNOW)
    water(a, b, LS + 3, 72)
rock(172, LS, 180, H)                           # east shore
L.sign(98.5, 48, "Le lac souterrain. Les îlots affleurent : nagez, sautez, ou passez au grappin.")
# upper route: grapple anchors under the crystal ceiling
for gx, gy in ((105, 39), (113, 38), (121, 39), (129, 38), (137, 39), (145, 38), (153, 39), (161, 38), (168, 39)):
    L.grapple(gx, gy)
L.shards_line(104, 36, 168, 36, 12)
# enemies on the islands
L.enemy("ice_gloop", 111, LS, patrol_tiles=2)
L.enemy("frost_weaver", 129.5, LS, patrol_tiles=1, face_left=True)
L.enemy("ice_burrower", 145, LS)
L.enemy("frost_weaver", 161, LS, patrol_tiles=2, face_left=True)
L.enemy("cryo_drone", 136, 42, patrol_tiles=6)
L.enemy("cryo_drone", 118, 43, patrol_tiles=4)
L.enemy("frost_wing", 152, 42, patrol_tiles=5)
for a, b in islands:
    glow((a + b) / 2 + 1.5, LS)
L.orb("health_orb", 127.5, LS - 2)
# crystal formations hanging from the ceiling
for x in (110, 126, 140, 156):
    L.solid(x, 32, x + 3, 35, ICE, deco=False)
# underwater: the sunken heart behind a phase barrier, shards in the depths
L.solid(114, 63, 124, 64, METAL)
L.solid(114, 64, 115, 72, METAL)
L.gate(123, 64, 1, 8, "teleport")
L.heart(119, 70.5)
L.secret(115, 64, 123, 72, "Un cœur de cristal dans l'épave engloutie !")
L.light(119, 70, DANGER, 50)
L.shards_line(126, 66, 140, 66, 5)
L.shards_line(146, 60, 156, 66, 4)
deco(L, "frozen_machine", 150, 72, alpha=0.8)
deco(L, "frozen_bones", 133, 72)
glow(166, 72, big=True)
L.sign(125.5, LS, "Une épave gît sous l'eau... une barrière de phase en garde l'entrée.")
# east shore
cp2 = L.checkpoint(175, LS, "Lac souterrain")
L.light(175, LS - 5, FROST_CYAN, 90)
L.orb("health_orb", 177, LS - 2)

# =====================================================================================
# ZONE 3 — Les Puits de Grappin (vertical shaft with anchors, arena at the top, drop)
# =====================================================================================
ceiling(180, 188, 42, hang=False)               # corridor roof
rock(180, LS, 188, H)                           # corridor floor (= shaft lower-left wall)
SX0, SX1, SB, ST = 188, 204, 64, 14             # shaft interior
rock(SX0, SB, SX1, H)                           # shaft floor
L.solid(180, 0, SX0, 42, SNOW, deco=False)      # shaft upper-left wall
L.solid(SX0 - 4, 0, SX1 + 4, ST, SNOW, deco=False)  # shaft roof
L.solid(SX1, 22, SX1 + 4, H, SNOW, deco=False)  # shaft right wall (top = arena floor level)
L.sign(185, LS, "Le puits monte vers la Salle des Échos. Visez les ancrages au grappin (Interagir).")
anchors = [(194, 57), (200, 50), (193, 43), (199, 36), (193, 29), (198, 25)]
ledge(198, SX1, 22)                             # landing lip at the top of the well
for gx, gy in anchors:
    L.grapple(gx, gy)
L.shards_line(196, 60, 196, 26, 9)
ledge(SX0, SX0 + 3, 48)                         # rest ledges
ledge(SX1 - 3, SX1, 40)
ledge(SX0, SX0 + 3, 33)
L.orb("health_orb", SX0 + 1.5, 46)
L.orb("health_orb", SX1 - 1.5, 38)
ice_spikes_wall(L, SX0, 16, 31, facing_left=False)   # icicle bands: no wall climbing to the top
ice_spikes_wall(L, SX1, 24, 39, facing_left=True)
ice_spikes_wall(L, SX1, 41, 62, facing_left=True)
L.enemy("frost_wing", 196, 40, patrol_tiles=3)
L.enemy("frost_weaver", 192, SB, patrol_tiles=2)
glow(201, SB)
glow(190, SB, big=True)
L.emitter(SX0, ST, SX1, SB, "aurora_motes", 1.5)
# arena: la Salle des Échos
AX0, AX1, AF, AC = 204, 248, 22, 8
rock(AX0 + 4, AF, AX1, 30, METAL)
L.solid(AX0 + 4, 30, AX1, H, SNOW, deco=False)
L.solid(AX0 - 4, 0, AX1 + 4, AC, METAL, deco=False)
L.solid(AX0 + 4, AC, AX0 + 7, AF - 8, METAL)
L.solid(AX1 - 3, AC, AX1, AF - 8, METAL)
a_left = L.door(AX0 + 5, AF - 8, 8, open_=True)
a_right = L.door(AX1 - 2, AF - 8, 8)
ledge(213, 220, 17)
ledge(232, 239, 17)
ledge(222, 230, 13)
w1 = [L.enemy("ice_gloop", 216, AF, 3, dormant=True), L.enemy("ice_gloop", 236, AF, 3, True, dormant=True),
      L.enemy("frost_weaver", 222, AF, 3, dormant=True), L.enemy("frost_weaver", 240, AF, 3, True, dormant=True)]
w2 = [L.enemy("ice_burrower", 219, AF, dormant=True), L.enemy("ice_burrower", 237, AF, dormant=True),
      L.enemy("cryo_drone", 214, 14, 3, dormant=True), L.enemy("cryo_drone", 238, 14, 3, True, dormant=True),
      L.enemy("frost_mite", 226, AF, 4, dormant=True), L.enemy("frost_mite", 230, AF, 4, True, dormant=True)]
w3 = [L.enemy("frost_bulwark", 218, AF, 3, dormant=True), L.enemy("ice_golem", 234, AF, 3, True, dormant=True),
      L.enemy("frost_wisp", 226, 11, 3, dormant=True)]
arena = L.combat(AX0 + 8, AC, AX1 - 4, AF, doors=a_left + "," + a_right,
                 waves=";".join(",".join(w) for w in (w1, w2, w3)))
L.orb("health_orb", 216.5, 15)
L.orb("health_orb", 235.5, 15)
L.orb("energy_orb", 226, 11)
L.light(226, 14, DANGER, 140, flicker=0.25)
for x in (212, 244):
    deco(L, "frost_banner", x, AF)
deco(L, "frozen_machine", 226, AF)
glow(209.5, AF)
L.sign(210, AF, "Les Tisseuses crachent des toiles qui ralentissent : gardez vos distances.")
L.event(arena, "cleared", [{"type": "hint", "params": {"text": "La salle se tait. Le passage s'ouvre vers les profondeurs.",
                                                      "duration": 3}}], delay=0.8)
# drop shaft to the gallery
rock(AX1, AF, 252, 34)
L.solid(AX1, 38, 252, H, SNOW, deco=False)
L.solid(AX1, 0, 264, AC, METAL, deco=False)
L.solid(252, AC, 264, 12, SNOW, deco=False)
L.solid(262, 12, 266, 44, SNOW, deco=False)     # drop shaft right wall
L.relic(250, 36.5, "Plume du puits")            # hidden niche in the left wall (glide into it)
L.secret(248, 34, 252, 38, "Une niche cachée dans le puits !")
L.light(250, 36.5, AURORA_VIOLET, 50)
L.shards_line(257, 26, 257, 46, 6)
L.sign(250, AF, "Un puits plonge vers la galerie. Planez pour adoucir la chute.")
L.emitter(252, 22, 262, 52, "aurora_motes", 1.5)

# =====================================================================================
# ZONE 4 — La Galerie Scintillante (advanced platforming: every power, two routes)
# =====================================================================================
GF = 56                                         # gallery floor
rock(252, GF, 270, H)
cp3 = L.checkpoint(258, GF, "Galerie scintillante")
L.light(258, GF - 5, FROST_CYAN, 90)
L.orb("health_orb", 261, GF - 2)
ceiling(264, 300, 44)
# 4a. crumbling ice bridge over the abyss under an icicle ceiling
for x in (272, 280):
    L.crumble(x, GF, 5, delay=0.5, respawn=2.5)
ceiling_spikes(L, 270, 287, 49)
L.solid(270, 44, 287, 49, SNOW, deco=False)
L.sign(266, GF, "Pont de glace fragile sous les stalactites : petits sauts, et ne vous arrêtez pas !")
L.shards_line(272, 53.5, 287, 53.5, 6)
L.emitter(270, 70, 289, 82, "abyss_motes", 2)
rock(287, GF, 304, H)
L.enemy("ice_burrower", 298, GF)
L.enemy("frost_mite", 295, GF, patrol_tiles=2, face_left=True)
# dash crystal wall closing the gallery
L.gate(302, 46, 2, 10, "dash")
L.solid(300, 0, 306, 46, SNOW, deco=False)
glow(296, GF, big=True)
# 4b. the hall of reflections: a door opened by a receptor across the abyss
rock(304, GF, 310, H)
ceiling(306, 316, 34)
ceiling(334, 340, 34)
L.solid(316, 0, 334, 26, SNOW, deco=False)
L.grapple(314, 49)
L.grapple(322, 46)
L.grapple(330, 46)
rock(336, GF, 344, H)
hall_door = L.door(343, GF - 8, 8)
L.solid(342, 34, 346, GF - 8, SNOW, deco=False)
L.receptor(339, GF, hall_door)
L.sign(308, GF, "Franchissez le gouffre au grappin. Le récepteur, frappé d'une Onde ou d'un Tir chargé, ouvre la porte.")
L.enemy("frost_bulwark", 340, GF, patrol_tiles=1, face_left=True)
L.enemy("frost_wing", 322, 46, patrol_tiles=5)
L.shards_line(312, 45, 333, 45, 6)
L.emitter(310, 70, 336, 82, "abyss_motes", 2)
# relic 1: a crystal alcove above the hall, behind a plasma curtain (shield)
L.solid(316, 34, 330, 35, ICE)                  # alcove floor
L.solid(316, 26, 317, 34, SNOW, deco=False)
L.solid(329, 26, 330, 30, SNOW, deco=False)
L.gate(329, 30, 1, 4, "shield")
L.oneway(330, 334, 34, "ice_ledge")
L.relic(320, 32.5, "Prisme du lac")
L.shards_line(322, 32.5, 326, 32.5, 3)
L.secret(317, 27, 329, 34, "Une alcôve de cristal derrière le plasma !")
L.light(320, 32, AURORA_VIOLET, 60)
L.grapple(332, 39)
# lower route entrance: cracked floor beyond the door
GX = 346
rock(GX, GF, 352, H)
cracked = L.cracked_floor(352, 356, GF, 2)
ceiling(346, 400, 44)
L.sign(348, GF, "Deux chemins : la galerie haute, ou le sol fissuré vers le canal englouti.")
cp4 = L.checkpoint(349, GF, "Salle des reflets")
L.light(349, GF - 5, FROST_CYAN, 90)
# 4c upper route: teleport barriers and a frost weaver corridor (floor 56)
rock(356, GF, 394, GF + 4, SNOW)                  # corridor floor = channel roof
L.gate(364, 44, 1, 12, "teleport")
L.enemy("frost_weaver", 372, GF, patrol_tiles=3, face_left=True)
L.enemy("ice_gloop", 380, GF, patrol_tiles=3, face_left=True)
L.enemy("snow_stalker", 388, GF, patrol_tiles=2, face_left=True)
ceiling_spikes(L, 384, 392, 44)
frost_cloud(L, 370, 48, 384, GF, 0.6)                     # freezing mist in the upper gallery
L.gate(392, 44, 1, 12, "teleport")
L.shards_line(358, 53, 398, 53, 8)
# 4c lower route: the sunken channel (cracked floor -> water channel -> rejoin at 400)
CH0, CH1 = 352, 400
water(CH0, 356, GF + 2, GF + 4)                   # under the cracked floor
water(CH0, CH1, GF + 4, 70)
rock(CH0, 70, CH1 + 4, H)
water(394, CH1, GF + 0.5, GF + 4)                  # exit well (surface just under the floor)
L.oneway(394, CH1, GF, "ice_ledge")                # grate over the well (swimmers pass through)
L.gate(368, GF + 4, 1, 10, "dash")
L.gate(384, GF + 4, 1, 10, "dash")
L.sign(358, GF, "Sous vos pieds, le canal englouti : Dash à travers les murs de cristal immergés.")
L.shards_line(358, 66, 396, 66, 8)
glow(376, 70)
glow(392, 70)
L.orb("health_orb", 372, 67)
# both routes reach the antechamber
rock(400, GF, 406, H)
rock(406, 60, 410, H)
ceiling(400, 410, 44)
L.solid(406, 0, 462, 30, SNOW, deco=False)
L.sign(401.5, GF, "La caverne de l'Hydre... Le froid y est vivant.")
cp5 = L.checkpoint(404, GF, "Caverne de l'Hydre")
L.light(404, GF - 5, FROST_CYAN, 90)
L.orb("health_orb", 402, GF - 2)
L.orb("health_orb", 408, 58)

# =====================================================================================
# ZONE 5 — La Caverne de l'Hydre (boss: Frost Hydra, rooted at the east end)
# =====================================================================================
BX0, BX1, BF, BC = 410, 462, 60, 36
boss, b_left, b_right = boss_arena(
    L, BX0, BX1, BF, BC, "frost_hydra", 452, BF,
    platforms=[(418, 425, 53, "ice_ledge"), (436, 444, 53, "ice_ledge"), (426, 434, 46, "ice_ledge")],
    music="music/glacior_boss.ogg",
    materials=[(BX0, 430, SNOW), (430, 446, ICE), (446, BX1, SNOW)], cam_zoom=0.92, ceiling_material=SNOW)
L.orb("health_orb", 421, 51)
L.orb("health_orb", 440, 51)
L.light(436, 42, AURORA_VIOLET, 170, flicker=0.05)
for x in (418, 428, 444):
    glow(x, BF, big=x == 428)
icicles(L, rng, BX0 + 3, BX1 - 3, BC, every=3.0)
L.emitter(BX0 + 3, BC, BX1 - 3, BF - 2, "aurora_motes", 2)
L.event(boss, "bossDefeated", [
    {"type": "hint", "params": {"text": "L'Hydre de Givre se fige à jamais. Le chemin vers la surface est libre.",
                                "duration": 4}}], delay=1.5, name="Hydre vaincue")
L.event(boss, "bossStart", [
    {"type": "dialogue", "params": {"speaker": "Nova", "text": "Trois têtes... et toutes affamées. Restons mobiles !",
                                    "duration": 3}}])

# =====================================================================================
# ZONE 6 — La Remontée vers l'Aurore (wind shaft up to the exit)
# =====================================================================================
rock(BX1, BF, 476, H)
L.checkpoint(466, BF, "Remontée")
L.light(466, BF - 5, FROST_CYAN, 90)
L.solid(BX1, 0, W, 22, SNOW, deco=False)
L.solid(476, 22, W, 36, SNOW, deco=False)
L.solid(476, 40, W, H, SNOW, deco=False)
L.solid(483, 36, W, 40, SNOW, deco=False)
L.updraft(468, 474, 26, BF, strength=260)
L.shards_line(471, 54, 471, 32, 6)
L.sign(464, BF, "Un courant ascendant ! Planez (maintenez Saut) pour remonter vers la surface.")
ledge(462, 467, 30)
ledge(470, 476, 26)
# relic 3: a phase vault in the shaft wall (teleport through while riding the wind)
L.gate(476, 36, 1, 4, "teleport")
L.relic(480, 38.5, "Œil de l'Hydre")
L.secret(477, 36, 483, 40, "La chambre secrète de l'Hydre !")
L.light(480, 38, AURORA_VIOLET, 50)
exit_id = L.exit(473, 26)
L.light(473, 22, AURORA_GREEN, 150)
lamp(L, 471, 26)
L.emitter(462, 14, 476, 26, "aurora_motes", 2)

# Crystal flora on the cave floors (bg layer), avoiding signs / checkpoints / doors.
for x0, x1, y, av in ((30, 46, 44, [(41, 43)]), (50, 62, 44, [(59, 61)]), (82, 100, 48, [(84, 90), (97, 100)]),
                      (172, 188, LS, [(174, 178), (184, 186)]), (SX0, SX1, SB, []), (252, 270, GF, [(257, 262)]),
                      (287, 300, GF, []), (304, 310, GF, [(307, 309)]), (346, 352, GF, [(347, 350)]),
                      (356, 394, GF, [(357, 359), (363, 365), (391, 393)]), (400, 406, GF, [(401, 405)]),
                      (BX1, 476, BF, [(463, 467)])):
    flora(L, rng, x0, x1, y, 0.45, cave=True, avoid=av)
for x0, x1, y in ((100, 172, 72), (CH0, CH1, 70)):
    flora(L, rng, x0, x1, y, 0.3, cave=True, tall=False)

# Dense snowy flora on every exposed ground (keeps clear of interactive objects).
auto_flora(L, rng, density=0.45, cave=True)

# =====================================================================================
# Events and cinematics
# =====================================================================================
L.event("level", "levelStart", [{"type": "hint", "params": {
    "text": "Descendez dans les grottes et suivez le signal.", "duration": 4}}])
L.event("level", "time", [{"type": "hint", "params": {"text": "Plus que 5 minutes !", "duration": 3}}],
        params={"seconds": 800})

d["intro"] = {"skippable": True, "letterbox": True, "steps": [
    {"type": "fade", "duration": 0.01, "params": {"to": 1}},
    {"type": "cut", "duration": 0.01, "params": {"x": 2190, "y": 800}},
    {"type": "zoom", "duration": 0.01, "params": {"zoom": 0.85}},
    {"type": "fade", "duration": 1.6, "wait": False, "params": {"to": 0}},
    {"type": "text", "duration": 4.0, "wait": False, "params": {"text": "GLACIOR", "subtitle": "Les Grottes de Cristal"}},
    {"type": "wait", "duration": 3.0},
    {"type": "text", "duration": 3.5, "wait": False, "params": {
        "text": "Sous la glace, la lumière du cristal ne s'éteint jamais.", "style": "caption"}},
    {"type": "wait", "duration": 1.5},
    {"type": "camera", "duration": 4.0, "params": {"x": 700, "y": 640, "ease": "inOutCubic"}},
    {"type": "wait", "duration": 0.4},
    {"type": "zoom", "duration": 2.5, "wait": False, "params": {"zoom": 1.0}},
    {"type": "camera", "duration": 2.5, "params": {"start": True, "ease": "inOutCubic"}},
    {"type": "spawnPlayer", "duration": 0.9},
    {"type": "dialogue", "duration": 3.4, "params": {"speaker": "Nova",
                                                    "text": "Le signal vient d'en bas. Allons voir ce que la glace cache."}},
]}
d["outro"] = {"skippable": True, "letterbox": True, "steps": [
    {"type": "zoom", "duration": 1.4, "wait": False, "params": {"zoom": 1.4}},
    {"type": "camera", "duration": 1.4, "params": {"player": True}},
    {"type": "text", "duration": 3.0, "wait": False, "params": {"text": "CRISTAUX LIBÉRÉS",
                                                               "subtitle": "Glacior — Les Grottes de Cristal"}},
    {"type": "playerAnim", "duration": 2.2, "params": {"anim": "victory"}},
    {"type": "dialogue", "duration": 3.2, "params": {"speaker": "Nova",
                                                    "text": "Le signal vient du bastion, là-haut. Le dernier gardien m'attend."}},
    {"type": "flash", "duration": 0.6, "params": {"color": "#e6fbff"}},
    {"type": "fade", "duration": 0.9, "params": {"to": 1}},
]}
d["metadata"] = {"generator": "tools/levelgen/glacior_02.py"}

L.save(os.path.join(ROOT, "data/levels/glacior_02.json"))
