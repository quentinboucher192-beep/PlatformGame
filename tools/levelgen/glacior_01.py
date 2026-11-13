#!/usr/bin/env python3
"""Builds data/levels/glacior_01.json — "Les Plaines de Givre", first level of Glacior.

Six zones: snowy plains (intro), the aurora cliffs (Triple jump tutorial), the sunken lake and
the frozen outpost (combat arena), the blizzard pass (advanced platforming), the frozen lake of
the Cryowyrm (boss) and the aurora sanctuary (exit). Run from the repository root:
    python3 tools/levelgen/glacior_01.py
"""
import os
import random

from levelkit import Level, ROOT
from glacior_common import (AURORA_GREEN, AURORA_VIOLET, FROST_CYAN, ICE_BLUE, WARM, DANGER, deco, flora, icicles,
                            hang_icicles_under_solids, lamp, crystal_light, floating_ice, freezing_water,
                            boss_arena)

rng = random.Random(4101)
W, H = 520, 72
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

# Snow everywhere, blizzard gusts in the pass.
L.emitter(0, 2, W, 30, "snowfall", 10)

# =====================================================================================
# ZONE 1 — La Lisière Gelée (introduction: snow, slippery ice, first enemies)
# =====================================================================================
L.solid(0, 6, 3, H, SNOW)                      # left cliff
L.solid(3, 50, 16, H, SNOW)                    # start terrace
L.solid(16, 50, 28, H, ICE)                    # flat slippery patch (safe)
L.solid(28, 50, 33, H, SNOW)
start = L.start(8, 50)
deco(L, "frozen_statue", 4.5, 50)
deco(L, "snow_pine", 11, 50)
deco(L, "ice_arch", 22, 50)
deco(L, "snow_drift", 30, 50)
lamp(L, 13.5, 50)
L.sign(10, 50, "Glacior... Le dernier relais émet depuis ces terres gelées. Avancez vers l'est.")
L.sign(15, 50, "Glace vive ! Elle glisse : relâchez la direction tôt pour vous arrêter.")
L.emitter(3, 38, 33, 49, "aurora_motes", 1.2)
L.shards_line(18, 47, 26, 47, 5)

# first chasm
L.shards_arc(33, 38, 47, 3, 4)
L.solid(38, 50, 50, H, SNOW)
# secret 1: cracked slab -> frozen cave under the island (relic 1)
L.solid(50, 50, 62, 52, SNOW)
L.cracked_floor(50, 52, 50, 2)
L.solid(52, 50, 62, 52, SNOW)
L.solid(62, 50, 64, H, SNOW)
L.solid(44, 55, 62, H, SNOW)                   # cave floor
L.solid(38, 50, 44, H, SNOW)
L.relic(59, 53.5, "Fragment d'aurore")
L.shards_line(46, 53.5, 56, 53.5, 5)
L.secret(45, 52, 62, 55, "Une grotte sous la neige !")
L.light(58, 53, AURORA_VIOLET, 60)
deco(L, "frost_flower", 47, 55)
deco(L, "ice_crystal", 61, 55)
deco(L, "frozen_bones", 53, 55)
L.enemy("frost_mite", 44, 50, patrol_tiles=4)
L.enemy("frost_mite", 58, 50, patrol_tiles=4, face_left=True)
L.sign(41, 50, "Le sol craque par endroits... Une Frappe sismique (Bas + Attaque en l'air) le briserait.")

# step up and a frost bloom turret
L.solid(64, 47, 74, H, SNOW)
L.enemy("frost_bloom", 71, 47, face_left=True)
L.orb("health_orb", 66, 45)
# chasm with a slippery ice floe in the middle (wide enough to stop on)
L.solid(78, 48, 83, 49, ICE)
L.shards_arc(74, 78, 46, 2, 3)
L.shards_arc(83, 88, 46, 2, 3)
L.enemy("frost_wing", 81, 42, patrol_tiles=4)
L.solid(88, 48, 108, H, SNOW)
cp1 = L.checkpoint(94, 48, "Lisière gelée")
L.light(94, 43, FROST_CYAN, 90)
L.enemy("snow_stalker", 104, 48, patrol_tiles=3, face_left=True)

# =====================================================================================
# ZONE 2 — Les Falaises d'Aurore (Triple jump tutorial, glide over the windy gorge)
# =====================================================================================
# Sheltered frozen ruin with the tablet (no enemies around).
L.solid(108, 48, 132, H, SNOW)
L.solid(110, 36, 128, 37.5, METAL, deco=False)  # broken roof
L.solid(110, 37.5, 111.5, 48, METAL, deco=False, collision=False, layer="bg_decor")
L.power(119, 44.5, "triple_jump", "Un troisième élan en plein vol : le Triple saut vous porte bien plus haut.")
L.light(119, 43, AURORA_GREEN, 120)
lamp(L, 113, 48, AURORA_VIOLET)
lamp(L, 125, 48)
deco(L, "frozen_machine", 116, 48)
deco(L, "frost_banner", 122, 48)
L.sign(129, 48, "Triple saut : en l'air, appuyez une 3e fois sur Saut. Seul lui permet de gravir ces falaises de glace.")
# Tutorial cliff A: 8 tiles of ice (jump + double jump fall short).
L.solid(132, 40, 148, H, ICE)
L.shards_line(131, 46, 131, 41, 4)
L.orb("energy_orb", 140, 38)
# Cliff B across a pit: triple jump while moving (the pit floor is safe, try again).
L.solid(148, 48, 154, H, SNOW)                 # safe pit floor
L.solid(154, 33, 168, H, ICE)
L.shards_arc(148, 154, 38, 4, 4)
cp2 = L.checkpoint(158, 33, "Falaises d'aurore")
L.light(158, 28, FROST_CYAN, 90)
L.enemy("frost_bloom", 166, 33, face_left=True)
L.sign(162, 33, "Planez (maintenez Saut en tombant) : les colonnes de vent vous soulèvent.")
# The windy gorge: rising wind columns over a bottomless chasm.
L.updraft(176, 179, 14, 66, strength=230)
L.updraft(190, 193, 22, 66, strength=230)
L.enemy("frost_wing", 184, 30, patrol_tiles=5)
L.enemy("frost_wing", 198, 36, patrol_tiles=4)
L.shards_line(170, 34, 175, 36, 3)
L.shards_line(177.5, 30, 177.5, 18, 4)
L.shards_line(182, 31, 189, 33, 4)
# floating ice islands (triple-jump alternative to gliding)
L.solid(181, 37, 185, 38, ICE)
L.solid(195, 35, 199, 36, ICE)
# Secret 2: a ledge high above the first wind column (relic 2).
L.solid(170, 14, 176, 15.5, ICE)
L.relic(172, 12.5, "Larme d'aurore")
L.shards_line(170.5, 12.5, 174.5, 12.5, 2)
L.secret(169, 9, 177, 14, "Au sommet du vent !")
L.light(172, 12, AURORA_VIOLET, 60)
# landing plateau, then down to the lake
L.solid(203, 40, 214, H, SNOW)
L.enemy("ice_burrower", 210, 40)
L.solid(214, 46, 222, H, SNOW)
L.orb("health_orb", 205, 38)
L.checkpoint(219, 46, "Rive du lac")
L.light(219, 41, FROST_CYAN, 90)

# =====================================================================================
# ZONE 3 — Le Lac Englouti (icy water, ice floes) and the frozen outpost (combat arena)
# =====================================================================================
LAKE = 52
L.solid(222, 62, 266, H, SNOW)                 # lake bed
freezing_water(L, 222, 266, LAKE, 62)
L.sign(221, 46, "L'eau du lac est glaciale et mord la chair : passez par les blocs de glace ou le grappin.")
L.crumble(225, LAKE - 1, 3, delay=0.5, respawn=3.0)
L.crumble(231, LAKE - 1, 3, delay=0.5, respawn=3.0)
L.solid(237, LAKE - 1, 243, LAKE + 1, ICE)      # big floe (slippery)
L.moving(245, LAKE - 1, 3, 8, 0, speed=40, wait=0.6)
L.crumble(258, LAKE - 1, 3, delay=0.5, respawn=3.0)
L.grapple(229, 43)
L.grapple(240, 42)
L.grapple(252, 43)
L.shards_line(226, 48, 234, 48, 4)
L.shards_line(244, 47, 256, 47, 5)
L.enemy("cryo_drone", 236, 42, patrol_tiles=5)
L.enemy("cryo_drone", 254, 40, patrol_tiles=5)
# sunken ruin under the ice: crystal heart (the cold bites, the heart heals completely)
L.solid(238, 57, 246, 58, METAL)
L.solid(238, 58, 239, 62, METAL)
L.heart(243, 60)
L.shards_line(232, 58, 236, 60, 3)
L.secret(239, 58, 246, 62, "Un cœur de cristal pris dans les glaces !")
L.light(243, 60, DANGER, 50)
deco(L, "frozen_bones", 241, 62)
# east shore and outpost gate
L.solid(266, 50, 276, H, SNOW)
L.checkpoint(270, 50, "Avant-poste gelé")
L.light(270, 45, FROST_CYAN, 90)
L.orb("health_orb", 273, 48)
# combat arena
AX0, AX1 = 276, 320
L.solid(AX0, 50, AX1, H, METAL)
L.solid(AX0 - 2, 32, AX1 + 2, 36, METAL)       # roof
L.solid(AX0, 36, AX0 + 3, 42, METAL)
L.solid(AX1 - 3, 36, AX1, 42, METAL)
a_left = L.door(AX0 + 1, 42, 8, open_=True)
a_right = L.door(AX1 - 2, 42, 8)
L.oneway(283, 290, 45, "ice_ledge")
L.oneway(306, 313, 45, "ice_ledge")
L.oneway(294, 302, 40, "ice_ledge")
w1 = [L.enemy("frost_mite", 288, 50, 4, dormant=True), L.enemy("frost_mite", 300, 50, 4, True, dormant=True),
      L.enemy("frost_mite", 309, 50, 4, True, dormant=True), L.enemy("snow_stalker", 312, 50, 5, True, dormant=True)]
w2 = [L.enemy("ice_gloop", 290, 50, 4, dormant=True), L.enemy("ice_gloop", 308, 50, 4, True, dormant=True),
      L.enemy("cryo_drone", 287, 40, 4, dormant=True), L.enemy("cryo_drone", 309, 40, 4, True, dormant=True)]
w3 = [L.enemy("ice_golem", 304, 50, 3, True, dormant=True), L.enemy("frost_bulwark", 292, 50, 4, dormant=True),
      L.enemy("frost_wisp", 298, 38, 4, True, dormant=True)]
L.combat(AX0 + 4, 36, AX1 - 4, 50, doors=a_left + "," + a_right,
         waves=";".join(",".join(w) for w in (w1, w2, w3)))
L.orb("health_orb", 286, 43)
L.orb("health_orb", 310, 43)
L.orb("energy_orb", 298, 38)
L.light(298, 38, DANGER, 130, flicker=0.25)
L.sign(282, 50, "Le Rempart gelé se protège de face : frappez-le de dos, en Frappe sismique ou au Tir chargé.")

# =====================================================================================
# ZONE 4 — Le Col des Blizzards (advanced: triple jump + dash, grapple, glide, teleport)
# =====================================================================================
L.solid(AX1, 50, 332, H, SNOW)
cp4 = L.checkpoint(326, 50, "Col des blizzards")
L.light(326, 45, FROST_CYAN, 90)
L.orb("health_orb", 329, 48)
L.emitter(332, 10, 430, 48, "blizzard", 5)
# P1: ice pillar 8 tiles up across a gap -> triple jump
L.solid(336, 42, 341, H, ICE)
L.shards_line(333, 46, 335, 42, 3)
# P2: 7 tiles away, 4 tiles up -> triple jump or double jump + dash. A crystal wall blocks the top.
L.solid(348, 38, 353, H, SNOW)
L.gate(352, 34, 1, 4, "dash")
L.solid(352, 28, 353, 34, SNOW)
L.solid(353, 28, 356, 38, SNOW)
L.enemy("frost_bloom", 350, 38, face_left=True)
# Wide gorge: grapple chain under a cryo bomber
L.grapple(362, 31)
L.grapple(371, 30)
L.grapple(380, 31)
L.enemy("cryo_bomber", 371, 22, patrol_tiles=6)
L.shards_line(358, 33, 384, 33, 7)
# P3: snowy ledge with a phase barrier wall
L.solid(386, 36, 398, H, SNOW)
L.solid(392, 22, 398, 32, METAL)
L.gate(394, 32, 1, 4, "teleport")
L.enemy("snow_stalker", 389, 36, patrol_tiles=2)
L.sign(388, 36, "Barrière de phase : téléportez-vous au travers.")
# High route (optional): a wind column up to an ice ledge with relic 3, then glide down.
L.updraft(399, 402, 8, 66, strength=240)
L.solid(404, 12, 412, 13.5, ICE)
L.relic(410, 10.5, "Couronne de givre")
L.shards_line(405, 10.5, 408, 10.5, 3)
L.secret(403, 7, 413, 12, "Le nid du blizzard !")
L.light(410, 10, AURORA_VIOLET, 60)
# Low route: slippery descending ice steps toward the boss lake (tension, wide steps).
L.solid(398, 36, 404, H, SNOW)
L.solid(404, 39, 410, H, ICE)
L.solid(410, 42, 417, H, ICE)
L.solid(417, 45, 423, H, SNOW)
L.enemy("frost_wing", 412, 34, patrol_tiles=5)
L.enemy("cryo_bomber", 418, 30, patrol_tiles=5)
L.orb("health_orb", 420, 43)
L.shards_line(405, 37, 421, 43, 5)
# P4: last triple jump onto the high cliff over the lake
L.solid(427, 37, 433, H, ICE)
L.solid(423, 52, 427, H, SNOW)
L.shards_arc(423, 427, 41, 3, 3)
L.solid(433, 44, 440, H, SNOW)
L.enemy("ice_golem", 437, 44, patrol_tiles=2, face_left=True)
L.solid(440, 52, 446, H, SNOW)
cp6 = L.checkpoint(443, 52, "Lac de l'aurore")
L.light(443, 47, FROST_CYAN, 90)
L.orb("health_orb", 441, 50)
L.orb("health_orb", 445, 50)

# =====================================================================================
# ZONE 5 — Le Lac de l'Aurore (boss: Cryowyrm)
# =====================================================================================
BX0, BX1, BF, BC = 446, 498, 52, 26
boss, b_left, b_right = boss_arena(
    L, BX0, BX1, BF, BC, "cryowyrm", 482, 44,
    platforms=[(454, 461, 45, "ice_ledge"), (483, 490, 45, "ice_ledge"), (467, 477, 38, "ice_ledge")],
    music="music/glacior_boss.ogg",
    materials=[(BX0, 458, SNOW), (458, 486, ICE), (486, BX1, SNOW)])
L.orb("health_orb", 457, 43)
L.orb("health_orb", 487, 43)
L.light(472, 34, AURORA_GREEN, 170, flicker=0.05)
L.event(boss, "bossDefeated", [
    {"type": "hint", "params": {"text": "Le Cryowyrm s'effondre. Le sanctuaire de l'aurore est ouvert.", "duration": 4}},
], delay=1.5, name="Cryowyrm vaincu")
L.event(boss, "bossStart", [
    {"type": "dialogue", "params": {"speaker": "Nova", "text": "Un serpent de glace ! Gardons nos distances.",
                                    "duration": 3}}])

# =====================================================================================
# ZONE 6 — Le Sanctuaire de l'Aurore (conclusion)
# =====================================================================================
L.solid(BX1, 52, 508, H, SNOW)
L.checkpoint(503, 52, "Sanctuaire")
L.light(503, 47, FROST_CYAN, 90)
L.solid(508, 44, W, H, SNOW)
exit_id = L.exit(514, 44)
deco(L, "ice_arch", 514, 44, scale=1.25)
L.light(514, 40, AURORA_GREEN, 150)
L.emitter(500, 30, W, 44, "aurora_motes", 2)
L.shards_line(505, 49, 507, 46, 3)

# =====================================================================================
# Events and cinematics
# =====================================================================================
L.event("level", "levelStart", [{"type": "hint", "params": {
    "text": "Traversez les plaines jusqu'au lac de l'aurore.", "duration": 4}}])
L.event("triple_jump", "unlocked", [{"type": "hint", "params": {
    "text": "Saut, double saut, puis un troisième élan : visez le haut de la falaise.", "duration": 4}}], delay=4)
L.event("level", "time", [{"type": "hint", "params": {"text": "Plus que 5 minutes !", "duration": 3}}],
        params={"seconds": 700})

d["intro"] = {"skippable": True, "letterbox": True, "steps": [
    {"type": "fade", "duration": 0.01, "params": {"to": 1}},
    {"type": "cut", "duration": 0.01, "params": {"x": 2700, "y": 520}},
    {"type": "zoom", "duration": 0.01, "params": {"zoom": 0.8}},
    {"type": "fade", "duration": 1.6, "wait": False, "params": {"to": 0}},
    {"type": "text", "duration": 4.0, "wait": False, "params": {"text": "GLACIOR", "subtitle": "Les Plaines de Givre"}},
    {"type": "camera", "duration": 6.0, "params": {"x": 3800, "y": 700, "ease": "inOutSine"}},
    {"type": "text", "duration": 3.5, "wait": False, "params": {
        "text": "Sous l'aurore éternelle, quelque chose ondule sous la glace du lac...", "style": "caption"}},
    {"type": "camera", "duration": 2.5, "params": {"x": 3750, "y": 900, "ease": "inOutCubic"}},
    {"type": "wait", "duration": 0.6},
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
                                                    "text": "Le signal vient des grottes de cristal, plus bas."}},
    {"type": "flash", "duration": 0.6, "params": {"color": "#e6fbff"}},
    {"type": "fade", "duration": 0.9, "params": {"to": 1}},
]}
d["metadata"] = {"generator": "tools/levelgen/glacior_01.py"}

L.save(os.path.join(ROOT, "data/levels/glacior_01.json"))
