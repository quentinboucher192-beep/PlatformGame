#!/usr/bin/env python3
"""Builds data/levels/pyralis_02.json — "Le Fleuve de Lave", second level of Pyralis.

Six zones: the scorched banks (intro, first forge raft), the ferryman's ford (Charged Shot tablet:
a receptor at the far end of a low conduit, an energy barrier, shield-bearers to shoot from afar),
the ferrymen's bastion (combat arena with bombers and ash wings), the lava river (rafts, lifts,
collapsing obsidian bridges and an eruption), Ignivore's lava lake (boss) and the far shore.
Run from the repository root:  python3 tools/levelgen/pyralis_02.py
"""
import os

from levelkit import Level, ROOT, T
from pyralis_common import Pyra, FORGE_GLOW, LAVA_GLOW, EMBER_GLOW

W, H = 560, 80
LAVA = 70
L = Level("pyralis_02", "Le Fleuve de Lave", W, H)
P = Pyra(L, LAVA, seed=4207)
R = P.rock
P.base(W, H,
       "Un fleuve de lave traverse les forges de Pyralis. Empruntez les radeaux des passeurs, apprenez le "
       "Tir chargé et affrontez Ignivore, le serpent qui règne sur le lac de feu.",
       difficulty=4,
       start_powers=["dash", "wave", "shield", "impulse", "teleport", "glide", "grapple", "ground_pound"],
       limit=1140, ref=360, medals=(None, 660, 460, 360))
d = L.data

# =====================================================================================
# ZONE 1 — Les Berges Calcinées (introduction: the first forge raft)
# =====================================================================================
R(0, 10, 3)
L.solid(3, 50, 6, 51, "basalt")
L.cracked_floor(6, 10, 50)                    # relic 1: cache under the start cliff
L.solid(10, 50, 24, 51, "basalt")
L.solid(3, 51, 4, 56, "basalt")
L.solid(12, 51, 24, 56, "basalt")
L.solid(3, 56, 24, LAVA + 3, "basalt")
L.relic(5.5, 54.5, "Écaille de salamandre")
L.shards_line(8, 54.5, 11, 54.5, 3)
L.secret(4, 51, 12, 56, "Une cache sous la falaise !")
L.light(7, 54, "#ffd98a", 50)
for rx in (6, 10):                            # rubble in front of the cache (fades once inside)
    L.decor("ash_rock_big", rx, 56.3, "fg_decor", scale=2.0, tint="#b08878", flip=rx > 8)
start = L.start(16, 50)
P.banner(13, 50)
L.decor("basalt_column", 21, 50)
P.brazier(18, 50)
L.decor("skull_rock", 4.5, 50, scale=0.8)
L.sign(11, 50, "Le fleuve de lave... Les passeurs y faisaient dériver leurs radeaux de forge.")
R(24, 54, 37)                                 # bank (top 54)
P.scatter(25, 36, 54, 0.45)
L.sign(34, 54, "Montez sur le radeau : il vous fera traverser.")
raft1 = L.moving(37, 54, 4, 16, 0, speed=48, wait=0.8)
L.liquid(37, 57, 54.5, LAVA, "lava")
L.shards_line(40, 51, 54, 51, 5)
L.enemy("ash_wing", 48, 47, patrol_tiles=6)
R(57, 54, 86)                                 # far bank (top 54)
L.checkpoint(61, 54, "Berges calcinées")
L.light(61, 49, FORGE_GLOW, 90)
P.scatter(64, 80, 54, 0.4)
L.enemy("ember_mite", 67, 54, patrol_tiles=3)
L.enemy("ember_mite", 72, 54, patrol_tiles=3, face_left=True)
R(80, 51, 86)                                 # rock (top 51)
L.enemy("cinder_bloom", 83, 51, face_left=True)
L.orb("health_orb", 70, 52)

# =====================================================================================
# ZONE 2 — Le Gué du Passeur (Charged Shot tablet and tutorial)
# =====================================================================================
R(86, 54, 100)                                # dais before the conduit (top 54)
L.power(91, 52.8, "charge_shot",
        "Maintenez Attaque pour charger, relâchez : la sphère de plasma porte loin, perce et active les récepteurs.")
L.light(91, 51, "#8ffff0", 110)
L.decor("forge_anvil", 87.5, 54)
# the conduit: a low forge hood over a lava basin, the receptor at its far end calls the raft
L.solid(98, 30, 124, 52, "forge_metal")
L.liquid(100, 124, 54.5, 58, "lava")
L.solid(100, 58, 124, LAVA + 3, "basalt", deco=False)
raft2 = L.moving(120, 54, 4, -20, 0, speed=55, wait=1.2, active=False)
R(124, 54, 150)                               # far quay (top 54)
L.receptor(125, 54, raft2)
L.light(125, 51, "#8ffff0", 50)
L.sign(96, 54, "Trop loin pour l'Onde ! Maintenez Attaque pour charger, relâchez pour toucher le récepteur.")
P.chains(99, 123, 52, every=4, layer="bg_decor", long_ratio=0.0)
L.light(112, 53, LAVA_GLOW, 90, flicker=0.2)
# energy barrier in a doorway
L.solid(138, 26, 146, 46, "basalt")
L.gate(141, 46, 2, 8, "wave")
L.sign(135, 54, "Une barrière d'énergie : le Tir chargé la dissout aussi.")
P.scatter(127, 137, 54, 0.4)
L.enemy("fire_wisp", 148, 46, patrol_tiles=3)
# shield-bearers' bridge over the lava
L.solid(150, 54, 177, 56, "forge_metal")
L.liquid(150, 156, 59, LAVA, "lava")
L.liquid(158, 169, 59, LAVA, "lava")
L.liquid(171, 177, 59, LAVA, "lava")
L.solid(156, 56, 158, LAVA + 3, "forge_metal")
L.solid(169, 56, 171, LAVA + 3, "forge_metal")
L.sign(148.5, 54, "Les Remparts parent de près : abattez-les de loin au Tir chargé, il perce leur garde !")
L.enemy("forge_bulwark", 162, 54, patrol_tiles=3, face_left=True)
L.enemy("forge_bulwark", 172, 54, patrol_tiles=3, face_left=True)
L.enemy("forge_drone", 166, 47, patrol_tiles=4)
P.chains(150, 177, 56, every=4, layer="bg_decor", long_ratio=0.3)
R(177, 54, 200)
L.checkpoint(181, 54, "Gué du passeur")
L.light(181, 49, FORGE_GLOW, 90)
L.orb("health_orb", 186, 52)
L.orb("energy_orb", 189, 52)
P.scatter(184, 198, 54, 0.35, forge=True)

# =====================================================================================
# ZONE 3 — Le Bastion des Passeurs (combat arena: ash wings, bombers)
# =====================================================================================
L.solid(200, 54, 252, LAVA + 3, "basalt")
L.solid(198, 26, 254, 32, "forge_metal")
L.solid(200, 32, 203, 46, "forge_metal")
L.solid(249, 32, 252, 46, "forge_metal")
a_l = L.door(201, 46, 8, open_=True)
a_r = L.door(250, 46, 8)
L.oneway(209, 216, 48, "basalt_ledge")
L.oneway(236, 243, 48, "basalt_ledge")
L.oneway(221, 231, 42, "chain_grate")
w1 = [L.enemy("ash_wing", 214, 44, 4, dormant=True), L.enemy("ash_wing", 238, 44, 4, True, dormant=True),
      L.enemy("ash_wing", 226, 38, 4, dormant=True), L.enemy("ember_mite", 220, 54, 3, dormant=True),
      L.enemy("ember_mite", 232, 54, 3, True, dormant=True)]
w2 = [L.enemy("forge_bomber", 214, 40, 4, dormant=True), L.enemy("forge_bomber", 240, 40, 4, True, dormant=True),
      L.enemy("forge_bulwark", 233, 54, 3, True, dormant=True), L.enemy("lava_stalker", 218, 54, 5, dormant=True),
      L.enemy("magma_gloop", 244, 54, 3, True, dormant=True)]
w3 = [L.enemy("magma_golem", 237, 54, 4, True, dormant=True), L.enemy("forge_bomber", 226, 38, 4, dormant=True),
      L.enemy("fire_wisp", 212, 40, 3, dormant=True), L.enemy("lava_stalker", 222, 54, 5, dormant=True),
      L.enemy("ash_wing", 242, 42, 4, True, dormant=True)]
arena = L.combat(206, 32, 246, 54, doors=a_l + "," + a_r, waves=";".join(",".join(w) for w in (w1, w2, w3)))
L.event(arena, "cleared", [{"type": "hint", "params": {"text": "Le bastion est tombé. Le fleuve vous attend.",
                                                       "duration": 3.5}}], delay=0.8)
L.decor("burnt_banner", 207, 32, rotation=180)
L.decor("burnt_banner", 245, 32, rotation=180, flip=True)
P.brazier(208, 54)
P.brazier(244, 54)
L.decor("gear_wreck", 226, 54, alpha=0.9)
P.chains(206, 246, 32, every=7, layer="bg_decor")
L.light(226, 38, "#ff4d1e", 140, flicker=0.25)
L.orb("health_orb", 212.5, 46)
L.orb("health_orb", 239.5, 46)
L.orb("energy_orb", 226, 40)

# =====================================================================================
# ZONE 4 — Le Fleuve de Lave (advanced: raft chain, lift, receptor across the river,
#          collapsing obsidian bridges with an eruption, geyser cliff)
# =====================================================================================
# 4a. the raft chain under bombers
R(252, 54, 262)
L.checkpoint(256, 54, "Fleuve de lave")
L.light(256, 49, FORGE_GLOW, 90)
L.moving(262, 54, 4, 14, 0, speed=52, wait=0.7)                  # raft A: 262 -> 276
L.liquid(262, 280, 54.5, LAVA, "lava")
R(280, 50, 284)                                                   # basalt pile (top 50)
L.enemy("cinder_bloom", 282, 50, face_left=True)
L.moving(284, 54, 4, 14, 0, speed=52, wait=0.7, phase=0.5)        # raft B: 284 -> 298
L.liquid(284, 302, 54.5, LAVA, "lava")
L.enemy("forge_bomber", 278, 42, patrol_tiles=8)
L.enemy("ash_wing", 294, 46, patrol_tiles=5)
L.solid(288, 44, 294, 48, "basalt")                             # floating slag outcrop
P.lavafall(291, 48, 54, hazard=True)                             # falls on raft B's route: shield or time it
L.sign(259, 54, "Une chute de lave barre la route du second radeau : passez-la sous Bouclier.")
L.shards_line(264, 51, 296, 51, 8)
R(302, 54, 312)                                                   # islet (top 54)
L.enemy("lava_burrower", 306, 54)
# ash cloud over the islet
L.obj("slow_zone", [303 * T, 48 * T, 8 * T, 6 * T], "gameplay", {"factor": 0.7, "visible": True, "style": "ash"})
L.orb("health_orb", 304, 52)
# lift up the cliff (vertical raft)
L.moving(312, 54, 3, 0, -12, speed=45, wait=1.0)                 # lift 54 -> 42
L.liquid(312, 315, 54.5, LAVA, "lava")
R(315, 42, 326)                                                   # cliff top (top 42)
L.orb("energy_orb", 318, 40)
L.enemy("ember_weaver", 320, 42, patrol_tiles=2, face_left=True)
# 4a'. the lava aqueduct: low obsidian vault, receptor at its far end (Charged Shot required)
L.solid(323, 24, 351, 40, "obsidian")
L.liquid(326, 348, 42.5, 46, "lava")
L.solid(326, 46, 348, LAVA + 3, "basalt", deco=False)
raft_c = L.moving(326, 42, 4, 18, 0, speed=60, wait=1.2, active=False)
R(348, 42, 357)                                                   # far landing (top 42)
L.receptor(349.5, 42, raft_c)
L.light(349.5, 39, "#8ffff0", 50)
L.sign(324, 42, "L'aqueduc de lave : réveillez le radeau d'un Tir chargé sur le récepteur, tout au bout.")
P.chains(326, 348, 40, every=4, layer="bg_decor", long_ratio=0.0)
L.light(337, 41, LAVA_GLOW, 90, flicker=0.2)
L.checkpoint(353, 42, "Rive des radeaux")
L.light(353, 37, FORGE_GLOW, 90)
# 4b. collapsing obsidian bridges and the eruption
L.sign(355.5, 42, "Les ponts d'obsidienne s'effondrent sous vos pas. Courez !")
L.liquid(357, 392, 46.5, LAVA, "lava")
xs = [358, 363, 368, 373, 378, 383, 388]
ys = [43, 43, 42, 42, 43, 43, 42]
for x, y in zip(xs, ys):
    L.crumble(x, y, 3, delay=0.35, respawn=3.5)
L.shards_arc(358, 394, 40, 3, 9)
erupt = L.trigger(370, 28, 376, 44)
erupt_fx = [L.light(366, LAVA - 1, LAVA_GLOW, 60), L.light(381, LAVA - 1, LAVA_GLOW, 60)]
wings = [L.enemy("ash_wing", 366, 36, 4, dormant=True), L.enemy("ash_wing", 386, 34, 4, True, dormant=True)]
L.event(erupt, "enter", [
    {"type": "shake", "params": {"amount": 0.6}},
    {"type": "effect", "target": erupt_fx[0], "params": {"effect": "eruption_fire", "scale": 2.5}},
    {"type": "effect", "target": erupt_fx[1], "params": {"effect": "eruption_fire", "scale": 2.5}},
    {"type": "flash", "params": {"color": "#ff6a2a", "duration": 0.25}},
    {"type": "spawn", "target": ",".join(wings)},
    {"type": "hint", "params": {"text": "Éruption ! Des Ailes de cendre jaillissent du fleuve.", "duration": 2.5}},
], name="Éruption du fleuve")
L.grapple(376, 34)                                                # rescue anchor over the bridges
# island with the relic vault (cracked floor, updraft to climb back out)
L.solid(392, 44, 399, 45, "basalt")
L.cracked_floor(399, 402, 44)
L.solid(402, 44, 408, 45, "basalt")
L.solid(392, 45, 397, 51, "basalt")
L.solid(405, 45, 408, 51, "basalt")
L.solid(392, 51, 408, LAVA + 3, "basalt")
L.relic(403.5, 49.5, "Rame du passeur")
L.shards_line(398, 49.5, 401, 49.5, 3)
P.vent(399, 402, 38, 51)
L.secret(397, 45, 405, 51, "Une crypte sous l'îlot !")
L.light(403.5, 49, "#ffd98a", 50)
for rx in (399.5, 403.5):
    L.decor("ash_rock_big", rx, 51.3, "fg_decor", scale=2.0, tint="#b08878", flip=rx > 401)
L.decor("ash_rock_big", 395, 44, "bg_decor")
L.enemy("lava_burrower", 406, 44)
L.orb("health_orb", 394, 42)
# grapple chain with crumbling relays
L.liquid(408, 430, 45.5, LAVA, "lava")
L.grapple(412, 38)
L.crumble(415, 42, 3, delay=0.35, respawn=3.5)
L.grapple(421.5, 36.5)
L.crumble(425, 40, 5, delay=0.4, respawn=3.5)
L.enemy("forge_bomber", 420, 30, patrol_tiles=6)
R(430, 40, 444)                                                   # cataract foot (top 40)
L.checkpoint(434, 40, "Pied de la cataracte")
L.light(434, 35, FORGE_GLOW, 90)
L.orb("health_orb", 438, 38)
L.sign(441, 40, "Les geysers de la cataracte soulèvent ceux qui planent.")
# 4c. the cataract: geysers lift the gliding player up the lava cliff
P.vent(444, 448, 24, 40)
R(444, 40, 448)
R(448, 32, 455)                                                   # ledge (top 32)
L.enemy("forge_bulwark", 452, 32, patrol_tiles=1, face_left=True)
P.vent(455, 459, 12, 38)
R(455, 38, 459)
R(459, 30, 476)                                                   # top of the cataract (top 30)
L.enemy("ember_weaver", 466, 30, patrol_tiles=2, face_left=True)
# heart: alcove behind a burning cascade, entered from the second geyser under Shield
L.solid(447, 10, 454, 14, "obsidian")
L.solid(447, 19, 454, 20, "obsidian")
L.solid(447, 14, 448, 19, "obsidian")
P.lavafall(454.3, 12, 20, hazard=True)
L.heart(450.5, 17.5)
L.secret(448, 14, 454, 19, "Un cœur de cristal derrière la cascade !")
L.light(450.5, 17, "#ff8aa0", 50)

# =====================================================================================
# ZONE 5 — Le Lac de Feu (Ignivore)
# =====================================================================================
L.checkpoint(472, 30, "Lac de feu")
L.light(472, 25, FORGE_GLOW, 90)
L.orb("health_orb", 469, 28)
L.solid(476, 33, 478, LAVA + 3, "basalt")                       # step down (top 33)
boss, boss_l, boss_r = P.boss_arena(
    478, 530, 36, 10, "ignivore", 516, boss_y=26,
    ledges=[(486, 493, 30), (515, 522, 30), (499, 509, 23)],
    name="Ignivore", defeated_text="Ignivore sombre dans le lac de feu. La rive lointaine vous attend.",
    floor_mat="basalt", mat="obsidian", ledge_style="basalt_ledge")
P.brazier(484, 36)
P.brazier(524, 36)
L.decor("skull_rock", 504, 36, scale=1.5)
L.light(504, 20, LAVA_GLOW, 180, flicker=0.08)
L.orb("health_orb", 489.5, 28)
L.orb("health_orb", 518.5, 28)
L.event(boss, "bossStart", [{"type": "dialogue", "params": {
    "speaker": "Nova", "text": "Il vole trop haut... Tirs chargés quand il plonge !", "duration": 3.5}}])

# =====================================================================================
# ZONE 6 — La Rive Lointaine (conclusion, relic 3 behind a phase barrier)
# =====================================================================================
R(530, 36, 540)
L.checkpoint(534, 36, "Rive lointaine")
L.light(534, 31, FORGE_GLOW, 90)
L.solid(540, 32, W, 33, "basalt")
L.solid(548, 33, W, 36, "basalt")
L.solid(540, 36, W, LAVA + 3, "basalt")
L.gate(540, 33, 1, 3, "teleport")
L.relic(545, 34.5, "Œil d'Ignivore")
L.secret(541, 33, 548, 36, "La cache de la rive !")
L.light(545, 34, "#e08aff", 50)
L.solid(W - 2, 0, W, 32, "basalt")
exit_id = L.exit(552, 32)
L.light(552, 28, "#ffd98a", 140)
P.brazier(545, 32)
P.brazier(557, 32)
P.scatter(531, 539, 36, 0.4)

# =====================================================================================
# Extra content: shard trails, more monsters, props
# =====================================================================================
L.checkpoint(129, 54, "Quai du conduit")
L.light(129, 49, FORGE_GLOW, 90)
L.checkpoint(317, 42, "Falaise de l'aqueduc")
L.light(317, 37, FORGE_GLOW, 90)
L.shards_arc(25, 35, 52, 1.5, 3)
L.shards_line(63, 52, 77, 52, 4)
L.shards_line(104, 53, 119, 53, 5)                 # collected while riding the conduit raft
L.shards_line(131, 52, 136, 52, 3)
L.shards_line(152, 51.5, 176, 51.5, 6)
L.shards_line(185, 52, 196, 52, 3)
L.shards_line(303, 52, 309, 52, 3)
L.shards_line(313.5, 51, 313.5, 44, 4)
L.shards_line(329, 41, 345, 41, 5)                 # under the aqueduct vault
L.shards_line(410, 36, 428, 36, 4)
L.shards_line(446, 37, 446, 26, 4)
L.shards_line(457, 35, 457, 18, 5)
L.shards_line(462, 28, 474, 28, 3)
L.shards_line(531, 34, 538, 34, 3)
L.enemy("forge_drone", 47, 45, patrol_tiles=4)
L.enemy("magma_gloop", 76, 54, patrol_tiles=3, face_left=True)
L.enemy("ember_mite", 131, 54, patrol_tiles=2)
L.enemy("ember_mite", 135, 54, patrol_tiles=2, face_left=True)
L.enemy("lava_stalker", 192, 54, patrol_tiles=4, face_left=True)
L.enemy("ash_wing", 270, 46, patrol_tiles=6)
L.enemy("forge_drone", 321, 35, patrol_tiles=3)
L.enemy("fire_wisp", 400, 36, patrol_tiles=3)
L.enemy("forge_drone", 468, 22, patrol_tiles=4)
L.enemy("magma_gloop", 463, 30, patrol_tiles=2)
L.enemy("lava_stalker", 537, 36, patrol_tiles=2, face_left=True)
P.brazier(127, 54)
P.banner(133, 54)
P.brazier(152, 54)
P.banner(175, 54, flip=True)
L.decor("obsidian_spire", 324.5, 24, scale=1.2)
L.decor("obsidian_spire", 349, 24, scale=1.4, flip=True)
L.decor("lava_vent", 306, 54)
L.decor("lava_vent", 397, 44)
L.decor("skull_rock", 470, 30)
L.decor("gear_wreck", 196, 54, alpha=0.9)
P.chains(152, 176, 56, every=3, layer="fg_decor", long_ratio=0.2)
P.chains(326, 348, 40, every=6, layer="fg_decor", long_ratio=0.0)

# =====================================================================================
# Ambience
# =====================================================================================
P.glow_line(0, W, LAVA - 2, every=24)
P.autodecorate(density=0.45, fg_every=1)
for x, y in ((28, 54), (66, 54), (134, 54), (186, 54), (256, 54), (309, 54), (323, 42), (436, 40), (472, 30)):
    L.decor(P.rng.choice(["lava_vent", "slag_pile", "ash_rock_big"]), x + 0.5, y, "bg_decor", flip=P.rng.random() < 0.5)
for x in (40, 52, 270, 296, 366, 384, 416, 426):     # sunken forge wrecks in the river
    L.decor(P.rng.choice(["gear_wreck", "basalt_column_broken", "obsidian_spire"]), x, LAVA + 0.6, "bg_decor",
            alpha=0.85, scale=1.3)
for x in range(10, W, 43):
    L.decor("obsidian_spire", x + P.rng.random() * 10, 24 + P.rng.random() * 6, "bg_decor", alpha=0.35,
            tint="#ff9a7a", scale=1.5)

# =====================================================================================
# Events and cinematics
# =====================================================================================
L.event("level", "levelStart", [{"type": "hint", "params": {
    "text": "Descendez le fleuve de lave jusqu'au lac de feu.", "duration": 4}}])
L.event("charge_shot", "unlocked", [{"type": "hint", "params": {
    "text": "Le Tir chargé coûte de l'énergie : ramassez les orbes d'énergie.", "duration": 4}}], delay=6)
L.event("level", "time", [{"type": "hint", "params": {"text": "Plus que 5 minutes !", "duration": 3}}],
        params={"seconds": 840})
P.intro("PYRALIS", "Le Fleuve de Lave",
        "Le fleuve emporte les forges des Anciens vers le lac de feu... et ce qui y dort.",
        "Le fleuve est trop large pour sauter. Il me faut un radeau.",
        cut=(200, 40), pan=(380, 38), low=(504, 24))
P.outro("FLEUVE TRAVERSÉ", "Pyralis — Le Fleuve de Lave")
d["metadata"] = {"generator": "tools/levelgen/pyralis_02.py", "revision": 4}

L.save(os.path.join(ROOT, "data/levels/pyralis_02.json"))
