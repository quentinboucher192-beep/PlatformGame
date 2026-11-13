#!/usr/bin/env python3
"""Builds data/levels/glacior_01.json — "Les Plaines de Givre", first level of Glacior.

Six zones: the frozen edge (intro), the aurora cliffs (Triple jump tutorial + windy gorge), the
sunken lake and the frozen outpost (combat arena), the blizzard pass (advanced platforming), the
aurora lake of the Cryowyrm (boss) and the aurora sanctuary (exit). Run from the repository root:
    python3 tools/levelgen/glacior_01.py

Design rule of Glacior: cliff faces bristling with ice ("no-grip" spiky faces) cannot be climbed
with wall jumps; the ice ledges sticking out of them are reached with the Triple jump.
"""
import os
import random

from levelkit import Level, ROOT
from glacior_common import (auto_flora, frost_cloud, AURORA_GREEN, AURORA_VIOLET, FROST_CYAN, ICE_BLUE, WARM, DANGER, deco, flora, icicles,
                            lamp, crystal_light, floating_ice, freezing_water, ice_spikes_wall, boss_arena)

rng = random.Random(4101)
W, H = 530, 72
L = Level("glacior_01", "Les Plaines de Givre", W, H)
L.planet("glacior")
d = L.data
d["description"] = ("Sous l'aurore éternelle de Glacior, des plaines de givre s'étendent jusqu'au lac gelé où "
                    "rôde le Cryowyrm. Gravissez les falaises de glace grâce au triple saut et bravez le blizzard.")
d["difficulty"] = 4
d["killY"] = H * 16 + 24
d["timer"] = {"mode": "limited", "limit": 1000, "onExpire": "gameover", "referenceTime": 300, "challengeMaxDeaths": 0}
d["medals"] = {"bronze": {}, "silver": {"maxTime": 600}, "gold": {"maxTime": 400},
               "platinum": {"maxTime": 300, "maxDeaths": 0}}
d["startPowers"] = ["dash", "wave", "shield", "impulse", "teleport", "glide", "grapple", "ground_pound", "charge_shot"]

SNOW, ICE, METAL = "snow_rock", "glacier_ice", "frost_metal"


def icecap(x0, y, x1, depth=2, bottom=None):
    """Snow rock mass capped with a slippery glacier-ice layer `depth` tiles thick."""
    L.solid(x0, y, x1, y + depth, ICE)
    return L.solid(x0, y + depth, x1, bottom or H, SNOW, deco=False)


def ledge(x0, x1, y, style="ice_ledge", hang=True):
    """One-way ice ledge with icicles underneath."""
    oid = L.oneway(x0, x1, y, style)
    if hang:
        icicles(L, rng, x0, x1, y + 1, every=3.6, long_ratio=0.0)
    return oid


# Snow everywhere; aurora motes high in the sky.
L.emitter(0, 0, W, 26, "snowfall", 12)
floating_ice(L, rng, 10, W - 10, 8, 22)

# =====================================================================================
# ZONE 1 — La Lisière Gelée (introduction: snow, slippery ice, first enemies)
# =====================================================================================
L.solid(0, 4, 3, H, SNOW)                      # left cliff
L.solid(3, 50, 16, H, SNOW)                    # start terrace
icecap(16, 50, 28)                             # flat slippery patch (safe)
L.solid(28, 50, 34, H, SNOW)
start = L.start(8, 50)
deco(L, "frozen_statue", 4.8, 50)
deco(L, "snow_pine", 2.5, 50, scale=1.2)
deco(L, "ice_arch", 22, 50)
deco(L, "snow_drift", 31, 50)
deco(L, "frozen_tree", 26.5, 50, flip=True)
lamp(L, 13.5, 50)
crystal_light(L, 18, 50)
L.sign(10.5, 50, "Glacior... Le dernier relais émet depuis ces terres gelées. Avancez vers l'est.")
L.sign(15, 50, "Glace vive ! Elle glisse : relâchez la direction tôt pour vous arrêter.")
L.emitter(3, 38, 34, 49, "aurora_motes", 1.2)
L.shards_line(18, 47, 26, 47, 5)

# first chasm, then an island hiding a frozen cave (secret 1, ground pound)
L.shards_arc(34, 38, 47, 3, 3)
L.solid(38, 50, 44, H, SNOW)
L.solid(44, 50, 50, 52, SNOW)
cracked1 = L.cracked_floor(50, 52, 50, 2)
L.solid(52, 50, 62, 52, SNOW)
L.solid(62, 50, 65, H, SNOW)
L.solid(44, 55, 62, H, SNOW)                   # cave floor
L.relic(59.5, 53.5, "Fragment d'aurore")
L.shards_line(46, 53.5, 56, 53.5, 5)
L.secret(45, 52, 62, 55, "Une grotte sous la neige !")
L.light(58, 53.5, AURORA_VIOLET, 60)
deco(L, "frost_flower", 47, 55)
deco(L, "ice_crystal", 61, 55)
deco(L, "frozen_bones", 53.5, 55)
icicles(L, rng, 44, 62, 52, every=2.5, long_ratio=0.0)
L.sign(41, 50, "Le sol craque par endroits... Une Frappe sismique (Bas + Attaque en l'air) le briserait.")
L.enemy("frost_mite", 45, 50, patrol_tiles=2)
L.enemy("frost_mite", 58, 50, patrol_tiles=4, face_left=True)
flora(L, rng, 38, 65, 50, 0.3, avoid=[(40, 42), (49, 53)])

# step up, frost bloom turret, slippery floe over a chasm (wide enough to stop on)
L.solid(65, 47, 76, H, SNOW)
L.enemy("frost_bloom", 73, 47, face_left=True)
L.orb("health_orb", 67, 45)
flora(L, rng, 65, 76, 47, 0.35, avoid=[(72, 74)])
L.solid(80, 48, 85, 49, ICE)
icicles(L, rng, 80, 85, 49, every=1.6)
L.shards_arc(76, 80, 45, 2, 3)
L.shards_arc(85, 89, 45, 2, 3)
L.enemy("frost_wing", 83, 42, patrol_tiles=4)
L.emitter(76, 60, 89, 70, "frost_mist", 1.5)
L.solid(89, 48, 110, H, SNOW)
cp1 = L.checkpoint(95, 48, "Lisière gelée")
L.light(95, 43, FROST_CYAN, 90)
L.enemy("snow_stalker", 105, 48, patrol_tiles=3, face_left=True)
L.enemy("frost_mite", 100, 48, patrol_tiles=2)
flora(L, rng, 89, 110, 48, 0.4, avoid=[(94, 96)])

# =====================================================================================
# ZONE 2 — Les Falaises d'Aurore (Triple jump tutorial, then the windy gorge)
# =====================================================================================
# Sheltered frozen ruin with the tablet: no enemy around.
L.solid(110, 48, 136, H, SNOW)
L.solid(112, 36, 129, 37, METAL)               # broken roof of the shrine
icicles(L, rng, 112, 129, 37, every=2.0)
deco(L, "frost_banner", 113, 48)
deco(L, "frozen_machine", 116, 48)
deco(L, "frost_banner", 127, 48, flip=True)
lamp(L, 123.5, 48, AURORA_VIOLET)
L.power(120, 46.2, "triple_jump", "Un troisième élan en plein vol : le Triple saut vous porte bien plus haut.")
L.light(120, 43, AURORA_GREEN, 120)
L.emitter(112, 37, 129, 47, "aurora_motes", 1.5)
L.sign(130.5, 48, "Ces parois hérissées de givre n'offrent aucune prise. Sautez trois fois pour atteindre la corniche !")
# Tutorial cliff A: an ice ledge 8 tiles up (jump + double jump fall short); safe floor below.
icecap(136, 40, 150)
ledgeA = ledge(132, 136, 40)
ice_spikes_wall(L, 136, 41, 46, facing_left=True)
L.shards_line(134, 46, 134, 42, 3)
L.orb("energy_orb", 143, 38)
flora(L, rng, 136, 150, 40, 0.3)
# Cliff B over a safe pit: same move, now while crossing (recovery ledge back up on the left).
L.solid(150, 48, 157, H, SNOW)                 # safe pit floor
ledge(150, 152, 40)                            # recovery ledge (8 up from the pit)
icecap(157, 32, 172)
ledgeB = ledge(153, 157, 32)
ice_spikes_wall(L, 157, 33, 46, facing_left=True)
L.shards_arc(150, 156, 34, 3, 4)
L.orb("health_orb", 153.5, 46)
deco(L, "frozen_bones", 154, 48)
cp2 = L.checkpoint(161, 32, "Falaises d'aurore")
L.light(161, 27, FROST_CYAN, 90)
L.enemy("frost_bloom", 170, 32, face_left=True)
L.sign(165, 32, "Planez (maintenez Saut en tombant) : les colonnes de vent vous soulèvent.")
flora(L, rng, 157, 172, 32, 0.35, avoid=[(160, 166), (169, 171)])
# The windy gorge: rising wind columns over a bottomless chasm.
L.updraft(178, 181, 6, 66, strength=230, vent=False)
L.updraft(192, 195, 20, 66, strength=230, vent=False)
L.emitter(178, 40, 181, 66, "blizzard", 3)
L.emitter(192, 40, 195, 66, "blizzard", 3)
L.emitter(172, 56, 207, 70, "frost_mist", 3)
L.enemy("frost_wing", 186, 29, patrol_tiles=5)
L.enemy("frost_wing", 200, 34, patrol_tiles=4)
L.shards_line(173, 33, 177, 35, 3)
L.shards_line(179.5, 30, 179.5, 18, 4)
L.shards_line(184, 31, 191, 33, 4)
L.shards_line(196, 30, 202, 35, 3)
# floating ice islands: triple-jump stepping stones (alternative to gliding)
L.solid(183, 37, 187, 38, ICE)
L.solid(197, 35, 201, 36, ICE)
icicles(L, rng, 183, 187, 38, every=1.4)
L.enemy("frost_mite", 185, 37, patrol_tiles=1)
icicles(L, rng, 197, 201, 36, every=1.4)
# Secret 2: a ledge on top of the first wind column (relic 2).
L.solid(171, 12, 177, 13, ICE)
icicles(L, rng, 171, 177, 13, every=1.5)
L.relic(172.5, 10.5, "Larme d'aurore")
L.shards_line(174, 10.5, 175.5, 10.5, 2)
L.secret(170, 7, 178, 12, "Au sommet du vent !")
L.light(172.5, 10, AURORA_VIOLET, 60)
deco(L, "ice_crystal", 176, 12)
# landing plateau, then down to the lake shore
L.solid(207, 40, 216, H, SNOW)
L.enemy("ice_burrower", 213, 40)
L.orb("health_orb", 209, 38)
flora(L, rng, 207, 216, 40, 0.4)
L.solid(216, 46, 224, H, SNOW)
cp3 = L.checkpoint(220, 46, "Rive du lac")
L.light(220, 41, FROST_CYAN, 90)

# =====================================================================================
# ZONE 3 — Le Lac Englouti (icy water, ice floes) and the frozen outpost (combat arena)
# =====================================================================================
LAKE = 52
L.solid(224, 62, 268, H, SNOW)                 # lake bed
freezing_water(L, 224, 264, LAKE, 62, interval=1.8)
L.solid(264, LAKE, 268, H, SNOW)               # beach flush with the surface (swimmers climb out)
L.sign(223, 46, "L'eau du lac est glaciale et mord la chair ! Passez par les blocs de glace ou au grappin.")
L.crumble(227, LAKE - 1, 3, delay=0.5, respawn=3.0)
L.crumble(233, LAKE - 1, 3, delay=0.5, respawn=3.0)
L.solid(239, LAKE - 1, 245, LAKE + 1, ICE)     # big floe (slippery)
L.moving(247, LAKE - 1, 3, 9, 0, speed=40, wait=0.6)
L.grapple(230, 43)
L.grapple(238, 42)
L.grapple(246, 42)
L.grapple(254, 43)
L.shards_line(228, 48, 236, 48, 4)
L.shards_line(246, 47, 260, 47, 5)
L.enemy("cryo_drone", 238, 42, patrol_tiles=5)
L.enemy("cryo_drone", 257, 40, patrol_tiles=5)
# sunken ruin under the ice: crystal heart (the cold bites, the heart heals completely)
L.solid(239, 57, 248, 58, METAL)
L.solid(239, 58, 240, 62, METAL)
L.heart(244, 60)
L.shards_line(250, 55, 250, 60, 3)
L.secret(240, 58, 248, 62, "Un cœur de cristal pris dans les glaces !")
L.light(244, 60, DANGER, 50)
deco(L, "frozen_machine", 228, 62, alpha=0.8)
deco(L, "frozen_bones", 258, 62)
deco(L, "ice_crystal", 262, 62)
# east shore and outpost gate
L.solid(268, 50, 278, H, SNOW)
cp4 = L.checkpoint(272, 50, "Avant-poste gelé")
L.light(272, 45, FROST_CYAN, 90)
L.orb("health_orb", 275, 48)
deco(L, "snow_drift", 266, 52)
# combat arena (frozen outpost courtyard)
AX0, AX1 = 278, 322
L.solid(AX0, 50, AX1, H, METAL)
L.solid(AX0 - 2, 32, AX1 + 2, 36, METAL)       # roof
L.solid(AX0, 36, AX0 + 3, 42, METAL)
L.solid(AX1 - 3, 36, AX1, 42, METAL)
a_left = L.door(AX0 + 1, 42, 8, open_=True)
a_right = L.door(AX1 - 2, 42, 8)
ledge(285, 292, 45)
ledge(308, 315, 45)
ledge(296, 304, 40)
w1 = [L.enemy("frost_mite", 290, 50, 4, dormant=True), L.enemy("frost_mite", 302, 50, 4, True, dormant=True),
      L.enemy("frost_mite", 311, 50, 4, True, dormant=True), L.enemy("snow_stalker", 314, 50, 5, True, dormant=True)]
w2 = [L.enemy("ice_gloop", 292, 50, 4, dormant=True), L.enemy("ice_gloop", 310, 50, 4, True, dormant=True),
      L.enemy("cryo_drone", 289, 40, 4, dormant=True), L.enemy("cryo_drone", 311, 40, 4, True, dormant=True)]
w3 = [L.enemy("ice_golem", 306, 50, 3, True, dormant=True), L.enemy("frost_bulwark", 294, 50, 4, dormant=True),
      L.enemy("frost_wisp", 300, 37, 4, True, dormant=True)]
arena = L.combat(AX0 + 4, 36, AX1 - 4, 50, doors=a_left + "," + a_right,
                 waves=";".join(",".join(w) for w in (w1, w2, w3)))
L.orb("health_orb", 288, 43)
L.orb("health_orb", 312, 43)
L.orb("energy_orb", 300, 38)
L.light(300, 39, DANGER, 140, flicker=0.25)
L.sign(283, 50, "Le Rempart gelé se protège de face : frappez-le de dos, en Frappe sismique ou au Tir chargé.")
for x in (283, 317):
    deco(L, "frost_banner", x, 50)
deco(L, "frozen_machine", 300, 50)
icicles(L, rng, AX0 + 3, AX1 - 3, 36, every=4.0, layer="bg_decor", long_ratio=0.0)
L.event(arena, "cleared", [{"type": "hint", "params": {"text": "L'avant-poste est libéré. Le col vous attend.",
                                                      "duration": 3}}], delay=0.8)

# =====================================================================================
# ZONE 4 — Le Col des Blizzards (advanced: triple jump + grapple, glide, teleport, dash)
# =====================================================================================
L.emitter(322, 4, 452, 46, "blizzard", 6)
L.solid(AX1, 50, 348, H, SNOW)
cp5 = L.checkpoint(328, 50, "Col des blizzards")
L.light(328, 45, FROST_CYAN, 90)
L.orb("health_orb", 331, 48)
flora(L, rng, 322, 340, 50, 0.35, avoid=[(327, 329)])
# 4a. L'Escalier de givre: three triple jumps up the spiky face of the ridge.
RX0, RX1, RT = 348, 370, 26
icecap(RX0, RT, RX1)
ice_spikes_wall(L, RX0, RT + 1, 48, facing_left=True)
ledge(342, 348, 42)
ledge(340, 348, 34)
L.sign(336, 50, "L'Escalier de givre : trois corniches, trois triples sauts. Ne touchez pas la paroi !")
L.shards_line(344, 47, 344, 43, 3)
L.shards_line(343, 39, 343, 35, 3)
L.shards_line(345, 31, 345, 27, 3)
L.enemy("frost_wing", 338, 36, patrol_tiles=4)
L.orb("health_orb", 341, 32)
# 4b. La Crête: the ridge top, then a grapple chain over the gorge (or a long glide).
flora(L, rng, RX0, RX1, RT, 0.4, avoid=[(354, 356), (367, 369)])
L.enemy("frost_bloom", 368, RT, face_left=True)
L.enemy("ice_burrower", 359, RT)
L.enemy("snow_stalker", 364, RT, patrol_tiles=2, face_left=True)
deco(L, "frozen_statue", 352, RT)
frost_cloud(L, 356, RT - 6, 366, RT, 0.65)                 # freezing mist on the ridge
L.sign(355, RT, "Le gouffre est trop large : enchaînez les ancrages au grappin (Interagir), ou planez.")
L.grapple(376, 20)
L.grapple(384, 19)
L.grapple(392, 20)
L.enemy("cryo_bomber", 384, 11, patrol_tiles=6)
L.shards_line(373, 24, 395, 24, 7)
L.emitter(370, 56, 398, 70, "frost_mist", 3)
# 4c. Le Bastion de phase: a frozen ruin closed by a phase barrier, guarded by a bulwark.
L.solid(398, 32, 420, H, SNOW)
L.solid(402, 22, 416, 28, METAL)
icicles(L, rng, 402, 416, 28, every=2.0, long_ratio=0.0)
phase = L.gate(410, 28, 1, 4, "teleport")
L.enemy("frost_bulwark", 407, 32, patrol_tiles=1, face_left=True)
L.enemy("frost_mite", 414, 32, patrol_tiles=2)
L.sign(402.5, 32, "Barrière de phase : téléportez-vous au travers (Téléportation).")
cp_bastion = L.checkpoint(399.5, 32, "Bastion de phase")
L.light(399.5, 27, FROST_CYAN, 90)
deco(L, "ice_arch", 400, 32, alpha=0.9)
deco(L, "frost_banner", 417, 28 + 4)
L.orb("health_orb", 418, 30)
L.light(410, 30, AURORA_VIOLET, 80)
# Secret 3: ride the wind column high above the bastion (relic 3), then glide down.
L.updraft(420, 423, 1, 66, strength=240, vent=False)
L.emitter(420, 30, 423, 66, "blizzard", 3)
L.shards_line(421.5, 26, 421.5, 14, 4)
L.solid(425, 8, 433, 9, ICE)
icicles(L, rng, 425, 433, 9, every=1.6)
L.relic(431, 6.5, "Couronne de givre")
L.shards_line(426, 6.5, 429, 6.5, 3)
L.secret(424, 3, 434, 8, "Le nid du blizzard !")
L.light(431, 6, AURORA_VIOLET, 60)
# 4d. Slippery ice steps down toward the lake of the aurora (wide steps, bombs from above).
icecap(423, 35, 429)
icecap(429, 38, 435)
icecap(435, 41, 441)
L.solid(441, 44, 446, H, SNOW)
L.enemy("frost_wing", 433, 30, patrol_tiles=5)
L.enemy("snow_stalker", 432, 38, patrol_tiles=2, face_left=True)
L.enemy("frost_wisp", 426, 18, patrol_tiles=3)
L.enemy("cryo_bomber", 438, 28, patrol_tiles=5)
L.enemy("ice_golem", 444, 44, patrol_tiles=1, face_left=True)
L.shards_line(425, 33, 444, 42, 6)
L.solid(446, 52, 452, H, SNOW)
cp6 = L.checkpoint(449.5, 52, "Lac de l'aurore")
L.light(449.5, 47, FROST_CYAN, 90)
L.sign(447, 52, "Le lac de l'aurore... Le Cryowyrm y dort. Reprenez votre souffle.")
L.orb("health_orb", 448, 50)
L.orb("health_orb", 451, 50)
flora(L, rng, 441, 446, 44, 0.5, tall=False)

# =====================================================================================
# ZONE 5 — Le Lac de l'Aurore (boss: Cryowyrm)
# =====================================================================================
BX0, BX1, BF, BC = 452, 504, 52, 26
boss, b_left, b_right = boss_arena(
    L, BX0, BX1, BF, BC, "cryowyrm", 488, 44,
    platforms=[(460, 467, 45, "ice_ledge"), (489, 496, 45, "ice_ledge"), (473, 483, 38, "ice_ledge")],
    music="music/glacior_boss.ogg",
    materials=[(BX0, 464, SNOW), (464, 492, ICE), (492, BX1, SNOW)])
L.orb("health_orb", 463, 43)
L.orb("health_orb", 493, 43)
L.light(478, 34, AURORA_GREEN, 170, flicker=0.05)
for x, name in ((458, "ice_spire"), (461, "frozen_statue"), (495, "frozen_statue"), (498, "ice_spire")):
    deco(L, name, x, BF, flip=x > 478)
crystal_light(L, 470, BF, big=True)
crystal_light(L, 486, BF, big=True)
icicles(L, rng, BX0 + 3, BX1 - 3, BC, every=3.0, layer="bg_decor")
L.emitter(BX0 + 3, BC, BX1 - 3, BF - 2, "aurora_motes", 2)
L.event(boss, "bossDefeated", [
    {"type": "hint", "params": {"text": "Le Cryowyrm s'effondre. Le sanctuaire de l'aurore est ouvert.", "duration": 4}},
], delay=1.5, name="Cryowyrm vaincu")
L.event(boss, "bossStart", [
    {"type": "dialogue", "params": {"speaker": "Nova", "text": "Un serpent de glace ! Gardons nos distances.",
                                    "duration": 3}}])

# =====================================================================================
# ZONE 6 — Le Sanctuaire de l'Aurore (conclusion)
# =====================================================================================
L.solid(BX1, 52, 514, H, SNOW)
L.checkpoint(508, 52, "Sanctuaire")
L.light(508, 47, FROST_CYAN, 90)
L.solid(514, 46, W, H, SNOW)
L.shards_line(510, 49, 513, 47, 3)
deco(L, "ice_arch", 522, 46, scale=1.2)
exit_id = L.exit(522, 46)
L.light(522, 41, AURORA_GREEN, 150)
lamp(L, 516.5, 46)
lamp(L, 527.5, 46, AURORA_VIOLET)
L.emitter(504, 30, W, 46, "aurora_motes", 2)
flora(L, rng, BX1, 514, 52, 0.4, tall=False, avoid=[(507, 509)])

# Icicles under floating ice / snow masses (above the lake level).
for o in list(d["objects"]):
    if o["type"] == "solid" and o["layer"] == "terrain" and o["props"]["material"] == SNOW:
        x, y, w, h = o["rect"]
        if (y + h) / 16 < 50 and w >= 64:
            icicles(L, rng, x / 16, (x + w) / 16, (y + h) / 16, every=3.0)

# Dense snowy flora on every exposed ground (keeps clear of interactive objects).
auto_flora(L, rng, density=0.6, skip=[(110, 136)])

# =====================================================================================
# Events and cinematics
# =====================================================================================
L.event("level", "levelStart", [{"type": "hint", "params": {
    "text": "Traversez les plaines jusqu'au lac de l'aurore.", "duration": 4}}])
L.event("triple_jump", "unlocked", [{"type": "hint", "params": {
    "text": "Saut, double saut, puis un troisième élan : visez la corniche.", "duration": 4}}], delay=4)
L.event("level", "time", [{"type": "hint", "params": {"text": "Plus que 5 minutes !", "duration": 3}}],
        params={"seconds": 700})

d["intro"] = {"skippable": True, "letterbox": True, "steps": [
    {"type": "fade", "duration": 0.01, "params": {"to": 1}},
    {"type": "cut", "duration": 0.01, "params": {"x": 7650, "y": 760}},
    {"type": "zoom", "duration": 0.01, "params": {"zoom": 0.8}},
    {"type": "fade", "duration": 1.6, "wait": False, "params": {"to": 0}},
    {"type": "text", "duration": 4.0, "wait": False, "params": {"text": "GLACIOR", "subtitle": "Les Plaines de Givre"}},
    {"type": "wait", "duration": 3.0},
    {"type": "text", "duration": 3.5, "wait": False, "params": {
        "text": "Sous l'aurore éternelle, quelque chose ondule sous la glace du lac...", "style": "caption"}},
    {"type": "wait", "duration": 2.0},
    {"type": "camera", "duration": 4.5, "params": {"x": 2600, "y": 520, "ease": "inOutSine"}},
    {"type": "wait", "duration": 0.4},
    {"type": "zoom", "duration": 3.0, "wait": False, "params": {"zoom": 1.0}},
    {"type": "camera", "duration": 3.0, "params": {"start": True, "ease": "inOutCubic"}},
    {"type": "spawnPlayer", "duration": 0.9},
    {"type": "dialogue", "duration": 3.4, "params": {"speaker": "Nova",
                                                    "text": "Brr... Le dernier relais émet d'ici. Restons en mouvement !"}},
]}
d["outro"] = {"skippable": True, "letterbox": True, "steps": [
    {"type": "zoom", "duration": 1.4, "wait": False, "params": {"zoom": 1.4}},
    {"type": "camera", "duration": 1.4, "params": {"player": True}},
    {"type": "text", "duration": 3.0, "wait": False, "params": {"text": "AURORE RALLUMÉE",
                                                               "subtitle": "Glacior — Les Plaines de Givre"}},
    {"type": "playerAnim", "duration": 2.2, "params": {"anim": "victory"}},
    {"type": "dialogue", "duration": 3.0, "params": {"speaker": "Nova",
                                                    "text": "Le signal descend sous la glace... vers les grottes de cristal."}},
    {"type": "flash", "duration": 0.6, "params": {"color": "#e6fbff"}},
    {"type": "fade", "duration": 0.9, "params": {"to": 1}},
]}
d["metadata"] = {"generator": "tools/levelgen/glacior_01.py"}

L.save(os.path.join(ROOT, "data/levels/glacior_01.json"))
