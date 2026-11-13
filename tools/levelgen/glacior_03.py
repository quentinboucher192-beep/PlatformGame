#!/usr/bin/env python3
"""Builds data/levels/glacior_03.json — "Le Bastion Gelé", final level of the game.

Six zones: the ice bridge and the gate (intro, lever), the ramparts and the court of honour
(combat arena), the tower of winds (vertical climb), the guardian's galleries (the hardest
platforming of the game, every power), the throne hall of the Glaciarch (final boss) and the
summit of the aurora (ending). Run from the repository root:
    python3 tools/levelgen/glacior_03.py
"""
import os
import random

from levelkit import Level, ROOT
from glacior_common import (AURORA_GREEN, AURORA_VIOLET, FROST_CYAN, ICE_BLUE, WARM, DANGER, deco, flora, icicles,
                            lamp, crystal_light, floating_ice, freezing_water, ice_spikes_wall, ceiling_spikes,
                            boss_arena)

rng = random.Random(4303)
W, H = 500, 80
L = Level("glacior_03", "Le Bastion Gelé", W, H)
L.planet("glacior")
d = L.data
d["description"] = ("Au sommet de Glacior se dresse le Bastion Gelé, forteresse du dernier gardien des Anciens. "
                    "Franchissez remparts, tours et galeries avec tous vos pouvoirs, et affrontez le Glaciarque "
                    "pour rallumer le dernier relais.")
d["difficulty"] = 5
d["killY"] = H * 16 + 24
d["ambientLight"] = "#8496c0"
d["timer"] = {"mode": "limited", "limit": 1200, "onExpire": "gameover", "referenceTime": 420, "challengeMaxDeaths": 0}
d["medals"] = {"bronze": {}, "silver": {"maxTime": 840}, "gold": {"maxTime": 570},
               "platinum": {"maxTime": 430, "maxDeaths": 0}}
d["startPowers"] = ["dash", "wave", "shield", "impulse", "teleport", "glide", "grapple", "ground_pound", "charge_shot",
                    "triple_jump"]

SNOW, ICE, METAL = "snow_rock", "glacier_ice", "frost_metal"


def wall(x0, y0, x1, y1, mat=METAL, top=True):
    return L.solid(x0, y0, x1, y1, mat, deco=top)


def ledge(x0, x1, y, style="ice_ledge", hang=True):
    oid = L.oneway(x0, x1, y, style)
    if hang:
        icicles(L, rng, x0, x1, y + 1, every=3.6, long_ratio=0.0)
    return oid


def banners(xs, y):
    for i, x in enumerate(xs):
        deco(L, "frost_banner", x, y, flip=i % 2 == 1)


def guardian_statue(x, y, flip=False):
    deco(L, "frozen_statue", x, y, flip=flip, scale=1.25)
    L.light(x, y - 3.5, FROST_CYAN, 40, flicker=0.05)


L.emitter(0, 0, W, 24, "snowfall", 10)
floating_ice(L, rng, 10, W - 10, 4, 18)

# =====================================================================================
# ZONE 1 — Le Pont des Glaces (intro: the broken ice bridge, the gate and its lever)
# =====================================================================================
L.solid(0, 0, 3, H, SNOW)
L.solid(3, 56, 26, H, SNOW)
start = L.start(8, 56)
deco(L, "snow_pine", 5, 56, scale=1.2)
deco(L, "frozen_tree", 13, 56)
deco(L, "snow_drift", 20, 56)
lamp(L, 23, 56)
L.sign(11, 56, "Le Bastion Gelé... Le dernier gardien des Anciens y veille sur le relais. Courage, Nova.")
L.emitter(3, 40, 26, 55, "aurora_motes", 1)
# the bridge: snow-bridge spans on frost-metal pylons, some of them crumbling
for x0, x1 in ((26, 32), (35, 44), (55, 63), (69, 76)):
    ledge(x0, x1, 56, "snow_bridge")
for x in (47, 50):
    L.crumble(x, 56, 3, delay=0.5, respawn=3.0)
for x in (38, 58, 72):
    wall(x, 57, x + 2, H, METAL, top=False)
    icicles(L, rng, x, x + 2, 57, every=1.5)
L.shards_line(28, 53.5, 74, 53.5, 12)
L.enemy("frost_mite", 40, 56, patrol_tiles=3)
L.enemy("cryo_drone", 50, 46, patrol_tiles=5)
L.enemy("frost_wing", 66, 47, patrol_tiles=4)
L.emitter(26, 66, 76, 78, "frost_mist", 4)
L.sign(29, 56, "Le pont est fragile : certaines dalles s'effondrent sous vos pas.")
# landing before the gate
L.solid(76, 56, 104, H, SNOW)
cp1 = L.checkpoint(80, 56, "Pont des glaces")
L.light(80, 51, FROST_CYAN, 90)
L.orb("health_orb", 83, 54)
# outer wall and its portcullis (door), opened by a lever on the turret ledge
wall(90, 18, 98, 48)
gate = L.door(93, 48, 8)
ledge(84, 90, 48)                               # turret ledge, 8 tiles up: triple jump
L.lever(86, 48, gate)
L.sign(87.5, 56, "La herse est close. Son levier est là-haut, sur la tourelle : triple saut, puis Interagir.")
deco(L, "ice_arch", 94, 56, alpha=0.6, scale=1.1)
banners((91, 97), 48)
guardian_statue(101, 56, flip=True)

# =====================================================================================
# ZONE 2 — Les Remparts (ramparts, drawbridge lever, bombers) and la Cour d'Honneur (arena)
# =====================================================================================
L.solid(104, 52, 110, H, METAL)
L.solid(110, 48, 116, H, METAL)
L.solid(116, 44, 136, 46, METAL)
L.solid(140, 44, 150, 46, METAL)
L.solid(116, 46, 132, H, METAL, deco=False)
L.solid(146, 46, 150, H, METAL, deco=False)
L.solid(132, 52, 146, H, METAL, deco=False)
L.enemy("frost_bulwark", 128, 44, patrol_tiles=3, face_left=True)
L.enemy("snow_stalker", 142, 44, patrol_tiles=3, face_left=True)
L.enemy("cryo_bomber", 134, 34, patrol_tiles=8)
L.shards_line(106, 49, 116, 41, 5)
banners((119, 131, 143), 44)
for x in range(118, 150, 4):                    # crenellations (background)
    deco(L, "snow_rock_small", x, 44, alpha=0.9)
lamp(L, 124, 44)
lamp(L, 146, 44, AURORA_VIOLET)
L.sign(121, 44, "Les Remparts gelés se protègent de face : passez-leur dans le dos, ou Frappe sismique !")
# drawbridge: a lever starts a moving platform over the gap
L.solid(162, 44, 166, H, METAL)
bridge = L.moving(150, 44, 4, 8, 0, speed=50, wait=0.8, active=False)
L.lever(148, 44, bridge, one_shot=True)
L.sign(145, 44, "Le pont-levis est relevé. Le levier le remet en marche (Interagir).")
L.shards_line(152, 41, 160, 41, 4)
# Crystal heart in the cellar under the ramparts (ground pound through the cracked slab)
L.cracked_floor(136, 140, 44, 2)               # Crystal heart in the cellar under the ramparts
L.heart(143, 51)
L.orb("energy_orb", 134, 51)
L.secret(132, 46, 146, 52, "La cave du rempart : un cœur de cristal !")
L.light(143, 50, DANGER, 50)
L.grapple(138, 40)
cp2 = L.checkpoint(164, 44, "Cour d'honneur")
L.light(164, 39, FROST_CYAN, 90)
L.orb("health_orb", 165.5, 42)
# arena: la Cour d'Honneur
AX0, AX1, AF, AC = 166, 214, 44, 28
L.solid(AX0, AF, AX1, H, METAL)
L.solid(AX0 - 2, AC - 6, AX1 + 2, AC, METAL)
L.solid(AX0, AC, AX0 + 3, AF - 8, METAL)
L.solid(AX1 - 3, AC, AX1, AF - 8, METAL)
a_left = L.door(AX0 + 1, AF - 8, 8, open_=True)
a_right = L.door(AX1 - 2, AF - 8, 8)
ledge(174, 181, 39)
ledge(199, 206, 39)
ledge(185, 195, 34)
w1 = [L.enemy("frost_bulwark", 180, AF, 4, dormant=True), L.enemy("frost_bulwark", 202, AF, 4, True, dormant=True),
      L.enemy("frost_mite", 186, AF, 3, dormant=True), L.enemy("frost_mite", 192, AF, 3, True, dormant=True),
      L.enemy("frost_mite", 197, AF, 3, True, dormant=True)]
w2 = [L.enemy("cryo_bomber", 180, 33, 5, dormant=True), L.enemy("cryo_bomber", 200, 33, 5, True, dormant=True),
      L.enemy("cryo_drone", 176, 36, 3, dormant=True), L.enemy("cryo_drone", 204, 36, 3, True, dormant=True),
      L.enemy("snow_stalker", 190, AF, 5, dormant=True)]
w3 = [L.enemy("ice_golem", 182, AF, 3, dormant=True), L.enemy("ice_golem", 200, AF, 3, True, dormant=True),
      L.enemy("frost_bulwark", 191, AF, 3, dormant=True), L.enemy("frost_wisp", 190, 31, 4, dormant=True)]
arena = L.combat(AX0 + 4, AC, AX1 - 4, AF, doors=a_left + "," + a_right,
                 waves=";".join(",".join(w) for w in (w1, w2, w3)))
for x in (177.5, 202.5, 190):
    L.orb("health_orb", x, 37 if x != 190 else 32)
L.light(190, 32, DANGER, 150, flicker=0.25)
banners((172, 208), AF)
guardian_statue(186, AF)
guardian_statue(194, AF, flip=True)
icicles(L, rng, AX0 + 3, AX1 - 3, AC, every=4.0, long_ratio=0.0)
L.event(arena, "cleared", [{"type": "hint", "params": {"text": "La cour est à vous. La Tour des Vents vous attend.",
                                                      "duration": 3}}], delay=0.8)

# =====================================================================================
# ZONE 3 — La Tour des Vents (vertical climb: wind column, lift, anchors, triple jumps)
# =====================================================================================
TX0, TX1, TF, TT = 214, 238, 44, 8              # tower interior, floor, roof
L.solid(TX0, TF, TX1, H, METAL)
cp3 = L.checkpoint(217.5, TF, "Tour des vents")
L.light(217.5, TF - 5, FROST_CYAN, 90)
L.orb("health_orb", 220, TF - 2)
L.solid(TX0 - 2, 0, TX1 + 4, TT, METAL)          # roof
L.solid(TX0, TT, TX0 + 2, 10, METAL)             # left wall (phase barrier to the vault at rows 10..14)
L.solid(TX0, 14, TX0 + 2, TF - 8, METAL)
L.solid(TX1, 16, TX1 + 4, H, METAL)              # right wall; exit opening rows TT..16
L.sign(222, TF, "La Tour des Vents : planez dans le courant, ou grimpez de corniche en ancrage.")
# route A: the wind column on the left
L.updraft(217, 220, 14, TF, strength=250)
L.shards_line(218.5, 40, 218.5, 18, 6)
ledge(216, 222, 14)
# route B: triple-jump ledge, lift and anchors on the right
ledge(230, 238, 36)
lift = L.moving(227, 36, 3, 0, -12, speed=45, wait=1.0)
L.grapple(232, 22)
ledge(228, 238, 16)
ice_spikes_wall(L, TX1, 20, 30, facing_left=True)
L.enemy("frost_wing", 226, 26, patrol_tiles=4)
L.enemy("cryo_drone", 231, 30, patrol_tiles=3)
L.orb("health_orb", 236, 34)
L.shards_line(231, 33, 237, 33, 3)
# relic 1: the tower vault behind a phase barrier, at the top of the wind column
L.solid(TX0 - 10, 0, TX0, 10, METAL)
L.solid(TX0 - 10, 14, TX0, 22, METAL)
L.solid(TX0 - 10, 10, TX0 - 8, 14, METAL)
L.gate(TX0, 10, 2, 4, "teleport")
L.relic(TX0 - 4, 13, "Sceau du bastion")
L.secret(TX0 - 8, 10, TX0, 14, "La chambre forte de la tour !")
L.light(TX0 - 4, 12, AURORA_VIOLET, 60)
L.emitter(TX0, TT, TX1, TF, "aurora_motes", 1.5)
banners((224, 234), TF)

# =====================================================================================
# ZONE 4 — Les Galeries du Gardien (the hardest platforming: every power in sequence)
# =====================================================================================
# 4a. Le Chemin de Ronde: a crumbling icy walkway under bomb fire, crystal walls, a bulwark.
RW = 16                                          # walkway level
L.solid(TX1 + 4, RW, 254, H, METAL)
cp4 = L.checkpoint(248, RW, "Chemin de ronde")
L.light(248, RW - 5, FROST_CYAN, 90)
L.orb("health_orb", 251, RW - 2)
for x in (256, 263.5):
    L.crumble(x, RW, 5, delay=0.45, respawn=2.5)
L.solid(273, RW, 286, H, ICE)                    # slippery stretch
L.gate(279, RW - 6, 1, 6, "dash")
L.solid(278, 0, 281, RW - 6, METAL)
L.enemy("cryo_bomber", 264, 6, patrol_tiles=6)
L.enemy("cryo_bomber", 282, 5, patrol_tiles=4)
L.shards_line(257, 13.5, 270, 13.5, 6)
L.sign(252, RW, "Le chemin de ronde craque sous le givre. Ne traînez pas sous les bombes !")
# narrow rampart guarded by a bulwark, then a cracked slab down into the lever hall
L.solid(286, RW, 296, H, METAL)
L.enemy("frost_bulwark", 291, RW, patrol_tiles=1, face_left=True)
L.cracked_floor(296, 300, RW, 2)
L.solid(300, RW, 304, 26, METAL)
L.sign(294, RW, "Brisez la dalle fissurée (Frappe sismique) pour descendre dans la salle des leviers.")
# 4b. La Salle des Leviers: timed receptor door, plasma curtain under drone fire, phase barrier.
HF = 34                                          # hall floor
L.solid(286, HF, 352, H, METAL)                   # hall floor (the slab shaft opens into the hall)
L.solid(304, 0, 320, 22, METAL)                   # hall ceiling up to the sky, with a niche (relic 2)
L.solid(326, 0, 352, 22, METAL)
L.solid(320, 0, 326, 15, METAL)
cp5 = L.checkpoint(302, HF, "Salle des leviers")
L.light(302, HF - 5, FROST_CYAN, 90)
timed = L.door(316, HF - 8, 8)
L.solid(315, 22, 317, HF - 8, METAL)
L.receptor(307, HF, timed, duration=4.0)
L.sign(305, HF, "Le récepteur n'ouvre la porte que quelques secondes : frappez-le, puis foncez (Dash) !")
curtain = L.gate(330, 22, 1, HF - 22, "shield")
L.enemy("cryo_drone", 324, 27, patrol_tiles=3)
L.enemy("cryo_drone", 336, 26, patrol_tiles=3)
L.sign(320, HF, "Rideau de plasma : traversez-le bouclier levé — il renvoie aussi les tirs des drones.")
L.enemy("ice_golem", 342, HF, patrol_tiles=2, face_left=True)
L.gate(348, 22, 1, HF - 22, "teleport")
L.shards_line(318, 31, 346, 31, 8)
L.orb("health_orb", 322, HF - 2)
banners((310, 326, 340), HF)
# relic 2: a niche in the hall ceiling, reached with the grapple and a triple jump
L.grapple(323, 26)
ledge(320, 326, 20, hang=False)
L.relic(323, 18.5, "Lame de givre")
L.secret(320, 15, 326, 22, "La niche du gardien !")
L.light(323, 17, AURORA_VIOLET, 60)
# 4c. Le Gouffre du Gardien: anchors, wind, crystal wall in the air, triple-jump ledges.
L.solid(352, HF, 358, H, METAL)
cp6 = L.checkpoint(355, HF, "Gouffre du gardien")
L.light(355, HF - 5, FROST_CYAN, 90)
L.orb("health_orb", 357, HF - 2)
L.sign(353.5, HF, "Le gouffre du gardien : grappin, vent, cristal et triple saut. Tout ce que vous avez appris.")
L.grapple(363, 28)
L.grapple(371, 27)
L.updraft(376, 380, 2, 76, strength=240, vent=False)
L.emitter(376, 40, 380, 76, "blizzard", 3)
L.gate(384, 14, 1, 10, "dash")                  # crystal wall hanging in the air
L.solid(383, 8, 386, 14, METAL)
L.solid(383, 24, 386, 44, METAL)
L.solid(390, 26, 396, 27, ICE)                  # floating ice floe
icicles(L, rng, 390, 396, 27, every=1.6)
ledge(398, 403, 19)                             # triple jump ledge (8 up)
L.grapple(408, 21)
L.enemy("frost_wing", 369, 34, patrol_tiles=4)
L.enemy("frost_wing", 393, 20, patrol_tiles=4)
L.shards_line(360, 31, 372, 31, 4)
L.shards_line(378, 30, 378, 20, 4)
L.shards_line(386, 22, 395, 24, 4)
L.emitter(358, 60, 420, 76, "frost_mist", 4)
# relic 3: the high road over the gulf (ride the wind to the top, glide right)
L.solid(386, 6, 396, 7, ICE)
L.relic(394, 4.5, "Couronne du gardien")
L.shards_line(388, 4.5, 392, 4.5, 3)
L.secret(386, 2, 396, 6, "Au-dessus du gouffre !")
L.light(394, 4, AURORA_VIOLET, 60)
# landing: the throne antechamber
L.solid(412, 24, 424, H, METAL)
L.solid(424, 50, 432, H, METAL)
L.sign(414, 24, "La salle du trône est en contrebas. Le Glaciarque attend.")
cp7 = L.checkpoint(428, 50, "Antichambre du trône")
L.light(428, 45, FROST_CYAN, 90)
L.orb("health_orb", 426, 48)
L.orb("health_orb", 430.5, 48)

# =====================================================================================
# ZONE 5 — La Salle du Trône (final boss: the Glaciarch)
# =====================================================================================
BX0, BX1, BF, BC = 432, 484, 50, 26
boss, b_left, b_right = boss_arena(
    L, BX0, BX1, BF, BC, "glaciarch", 470, BF,
    platforms=[(440, 447, 43, "ice_ledge"), (469, 476, 43, "ice_ledge"), (453, 463, 36, "ice_ledge")],
    music="music/glacior_boss.ogg",
    materials=[(BX0, 448, METAL), (448, 468, SNOW), (468, BX1, METAL)], cam_zoom=0.9)
L.orb("health_orb", 443.5, 41)
L.orb("health_orb", 472.5, 41)
L.orb("energy_orb", 458, 34)
L.light(458, 32, AURORA_VIOLET, 190, flicker=0.05)
deco(L, "ice_arch", 458, BF, scale=1.5, alpha=0.7)
guardian_statue(439, BF)
guardian_statue(477, BF, flip=True)
banners((445, 451, 465, 471), BF)
crystal_light(L, 454, BF, big=True)
crystal_light(L, 462, BF, big=True)
icicles(L, rng, BX0 + 3, BX1 - 3, BC, every=3.0)
L.emitter(BX0 + 3, BC, BX1 - 3, BF - 2, "aurora_motes", 2.5)
L.event(boss, "bossStart", [
    {"type": "dialogue", "params": {"speaker": "Glaciarque",
                                    "text": "Nul ne rallumera le relais. L'hiver des Anciens est éternel.", "duration": 3.5}}])
L.event(boss, "bossPhase", [
    {"type": "dialogue", "params": {"speaker": "Nova", "text": "Il faiblit ! Encore un effort !", "duration": 2.5}}],
    once=True)
L.event(boss, "bossDefeated", [
    {"type": "hint", "params": {"text": "Le Glaciarque tombe. Le chemin du dernier relais est libre.", "duration": 4}}],
    delay=1.5, name="Glaciarque vaincu")

# =====================================================================================
# ZONE 6 — Le Sommet de l'Aurore (the last relay)
# =====================================================================================
L.solid(BX1, BF, 490, H, METAL)
L.checkpoint(487, BF, "Sommet de l'aurore")
L.light(487, BF - 5, FROST_CYAN, 90)
L.solid(490, 46, W, H, SNOW)
deco(L, "ice_arch", 495, 46, scale=1.25)
exit_id = L.exit(495, 46)
L.light(495, 41, AURORA_GREEN, 170)
lamp(L, 491, 46)
L.emitter(482, 20, W, 46, "aurora_motes", 3)
L.shards_line(488.5, 48, 490.5, 44.5, 3)

# Background frost and ice on the masses (icicles under floating structures).
for o in list(d["objects"]):
    if o["type"] == "solid" and o["layer"] == "terrain":
        x, y, w, h = o["rect"]
        bottom = (y + h) / 16
        if bottom < 60 and w >= 64 and (y + h) < H * 16:
            icicles(L, rng, x / 16, (x + w) / 16, bottom, every=3.4, long_ratio=0.15)

# =====================================================================================
# Events and cinematics
# =====================================================================================
L.event("level", "levelStart", [{"type": "hint", "params": {
    "text": "Gravissez le Bastion Gelé et rallumez le dernier relais.", "duration": 4}}])
L.event("level", "time", [{"type": "hint", "params": {"text": "Plus que 5 minutes !", "duration": 3}}],
        params={"seconds": 900})

d["intro"] = {"skippable": True, "letterbox": True, "steps": [
    {"type": "fade", "duration": 0.01, "params": {"to": 1}},
    {"type": "cut", "duration": 0.01, "params": {"x": 7290, "y": 640}},
    {"type": "zoom", "duration": 0.01, "params": {"zoom": 0.75}},
    {"type": "fade", "duration": 1.8, "wait": False, "params": {"to": 0}},
    {"type": "text", "duration": 4.5, "wait": False, "params": {"text": "GLACIOR", "subtitle": "Le Bastion Gelé"}},
    {"type": "camera", "duration": 5.0, "params": {"x": 3600, "y": 300, "ease": "inOutSine"}},
    {"type": "text", "duration": 3.8, "wait": False, "params": {
        "text": "Le dernier gardien des Anciens n'a jamais quitté son trône de glace.", "style": "caption"}},
    {"type": "camera", "duration": 4.0, "params": {"x": 1400, "y": 760, "ease": "inOutCubic"}},
    {"type": "wait", "duration": 0.4},
    {"type": "zoom", "duration": 2.5, "wait": False, "params": {"zoom": 1.0}},
    {"type": "camera", "duration": 2.5, "params": {"start": True, "ease": "inOutCubic"}},
    {"type": "spawnPlayer", "duration": 0.9},
    {"type": "dialogue", "duration": 3.6, "params": {"speaker": "Nova",
                                                    "text": "Le dernier relais... Après lui, toute la galaxie sera reliée. On y va."}},
]}
d["outro"] = {"skippable": True, "letterbox": True, "steps": [
    {"type": "zoom", "duration": 1.6, "wait": False, "params": {"zoom": 1.35}},
    {"type": "camera", "duration": 1.6, "params": {"player": True}},
    {"type": "playerAnim", "duration": 2.0, "wait": False, "params": {"anim": "victory"}},
    {"type": "flash", "duration": 0.8, "params": {"color": "#e6fffb"}},
    {"type": "text", "duration": 3.5, "wait": False, "params": {"text": "LE DERNIER RELAIS EST RALLUMÉ",
                                                               "subtitle": "Glacior — Le Bastion Gelé"}},
    {"type": "shake", "duration": 0.8, "params": {"amount": 0.3}},
    {"type": "wait", "duration": 2.0},
    {"type": "dialogue", "duration": 4.0, "params": {"speaker": "Nova",
                                                    "text": "Sylvanox, Pyralis, Glacior... Les relais chantent à nouveau. La galaxie n'est plus seule."}},
    {"type": "zoom", "duration": 4.0, "wait": False, "params": {"zoom": 0.7}},
    {"type": "camera", "duration": 4.0, "wait": False, "params": {"x": 7700, "y": 300, "ease": "inOutSine"}},
    {"type": "text", "duration": 4.0, "params": {"text": "MERCI D'AVOIR JOUÉ", "subtitle": "Cosmic Platformer",
                                                 "style": "caption"}},
    {"type": "text", "duration": 4.0, "params": {"text": "Conception, programmation, graphismes et musique",
                                                 "subtitle": "Cosmic Studio", "style": "caption"}},
    {"type": "text", "duration": 4.0, "params": {"text": "Et Nova reprit la route des étoiles...",
                                                 "style": "caption"}},
    {"type": "fade", "duration": 1.5, "params": {"to": 1}},
]}
d["metadata"] = {"generator": "tools/levelgen/glacior_03.py"}

L.save(os.path.join(ROOT, "data/levels/glacior_03.json"))
