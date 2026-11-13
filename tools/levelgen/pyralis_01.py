#!/usr/bin/env python3
"""Builds data/levels/pyralis_01.json — "Les Forges Englouties", first level of Pyralis.

Six zones: the ash quay (intro, updraft + grapple refresher), the sunken foundry (Ground Pound
tablet and tutorial), the anvil hall (combat arena), the lava rivers (tower of cracked floors,
underground river, chimneys and forge corridor), Magmaroth's forge (boss) and the final ascent.
Run from the repository root:  python3 tools/levelgen/pyralis_01.py
"""
import os

from levelkit import Level, ROOT, T
from pyralis_common import Pyra, FORGE_GLOW, LAVA_GLOW, EMBER_GLOW

W, H = 524, 76
LAVA = 68
L = Level("pyralis_01", "Les Forges Englouties", W, H)
P = Pyra(L, LAVA, seed=311)
R = P.rock
P.base(W, H,
       "Sous les cendres de Pyralis, les forges des Anciens sombrent lentement dans la lave. Apprenez la "
       "Frappe sismique, brisez les sols fissurés et faites taire le Marteau des Profondeurs.",
       difficulty=3,
       start_powers=["dash", "wave", "shield", "impulse", "teleport", "glide", "grapple"],
       limit=1080, ref=330, medals=(None, 600, 420, 330))
d = L.data

# =====================================================================================
# ZONE 1 — Le Quai des Cendres (introduction: updraft and grapple refresher)
# =====================================================================================
R(0, 10, 3)                                   # left cliff
R(3, 56, 31)                                  # start terrace (top 56)
start = L.start(8, 56)
L.decor("basalt_column", 5, 56)
P.banner(11, 56)
P.brazier(15, 56)
L.decor("skull_rock", 24, 56)
L.decor("obsidian_spire", 28.5, 56)
P.scatter(3, 30, 56, 0.4, tall=False)
L.sign(18, 56, "Pyralis, la forge en fusion. La lave est mortelle : restez sur la roche !")
L.sign(27, 56, "Un courant ascendant ! Sautez au-dessus, puis maintenez Saut pour planer : la chaleur vous soulève.")
L.emitter(3, 44, 31, 55, "embers_rising", 1.0)
# vent rock in the lava, its updraft lifts the gliding player to the high ledge
R(35, 62, 40)
L.liquid(31, 35, 63, LAVA, "lava")
L.liquid(40, 45, 63, LAVA, "lava")
P.vent(35, 40, 39, 62)
L.decor("lava_vent", 34, 62)
L.decor("lava_vent", 41, 68, flip=True)
L.shards_line(37.5, 58, 37.5, 44, 5)
L.shards_arc(40, 46, 43, 2, 3)
R(45, 50, 62)                                 # high ledge (top 50)
L.liquid(62, 76, 59, LAVA, "lava")
L.decor("basalt_column_broken", 47, 50)
P.scatter(48, 61, 50, 0.4)
L.enemy("ember_mite", 52, 50, patrol_tiles=3)
L.enemy("ember_mite", 57, 50, patrol_tiles=3, face_left=True)
L.orb("health_orb", 49, 48)
L.sign(58, 50, "Près d'un point d'ancrage, appuyez sur Interagir : le Grappin vous y projette.")
# grapple over the lava gap
L.grapple(69, 45)
L.shards_line(63, 48, 68, 46, 3)
L.shards_arc(70, 78, 46, 2, 4)
R(76, 52, 98)                                 # quay (top 52)
L.checkpoint(81, 52, "Quai des cendres")
L.light(81, 47, FORGE_GLOW, 90)
P.scatter(84, 96, 52, 0.4, forge=True)
L.enemy("forge_drone", 91, 46, patrol_tiles=4)
R(94, 49, 100)                                # step at the foundry door (top 49)
L.enemy("cinder_bloom", 97, 49, face_left=True)

# =====================================================================================
# ZONE 2 — La Fonderie Noyée (Ground Pound tablet, tutorial over a safe drop, secret)
# =====================================================================================
L.solid(98, 28, 178, 34, "forge_metal")       # foundry roof
P.chains(100, 176, 34, every=6, layer="bg_decor")
L.solid(100, 52, 112, 57, "forge_metal")      # upper floor (left part)
power_gp = L.power(106, 50.8, "ground_pound",
                   "En l'air, Bas + Attaque : plongeon sismique. Brise les sols fissurés et écrase les ennemis.")
L.light(106, 46, "#8ffff0", 110)
L.decor("forge_anvil", 102.5, 52)
L.cracked_floor(112, 117, 52)                 # TUTORIAL: the only way on, over a safe drop
L.solid(117, 52, 168, 57, "forge_metal")      # upper floor / gallery ceiling
L.solid(120, 34, 124, 52, "forge_metal")      # machinery wall: the upper floor is a dead end
L.decor("gear_wreck", 122, 52, scale=1.0)
L.decor("steam_pipe", 118.5, 52)
L.sign(109.5, 52, "Frappe sismique : sautez, puis Bas + Attaque en l'air. Brisez ce sol fissuré !")
L.light(114.5, 50, EMBER_GLOW, 50)
# lower gallery (floor top 63): ember-mite pack, cracked-floor secret, bulwark, updraft chimney
L.solid(100, 63, 138, LAVA + 3, "basalt")
L.solid(138, 67, 150, LAVA + 3, "basalt")     # secret chamber floor
L.solid(138, 63, 141, 64, "basalt")
L.cracked_floor(141, 145, 63)                 # hides the first relic
L.solid(145, 63, 150, 64, "basalt")
L.solid(150, 63, 174, LAVA + 3, "basalt")
L.relic(147.5, 65.5, "Sceau du Forgeron")
L.shards_line(139.5, 65.5, 144, 65.5, 3)
L.secret(138, 64, 150, 67, "Une cache sous le sol fissuré !")
L.light(146, 65, "#ffd98a", 50)
for rx in (140.5, 144.5, 148):                # rubble in front of the cache (fades once inside)
    L.decor("ash_rock_big", rx, 67.3, "fg_decor", scale=1.7, tint="#b08878", flip=rx > 144)
L.sign(118, 63, "Une meute de Braisites : écrasez-les d'une Frappe sismique au milieu du groupe !")
for i, x in enumerate((125, 127, 129.5, 132)):
    L.enemy("ember_mite", x, 63, patrol_tiles=2, face_left=i % 2 == 1)
L.checkpoint(135, 63, "Galerie engloutie")
L.light(135, 59, FORGE_GLOW, 80)
L.orb("health_orb", 136.5, 61)
P.scatter(101, 122, 63, 0.35, tall=False)
P.scatter(151, 166, 63, 0.3, tall=False)
L.sign(153, 63, "Un Rempart de forge pare de face : sautez par-dessus et frappez son dos d'une Frappe sismique.")
L.enemy("forge_bulwark", 161, 63, patrol_tiles=4, face_left=True)
L.decor("forge_brazier", 156, 63)
L.decor("slag_pile", 165, 63)
L.sign(166.5, 63, "Planez dans le courant ascendant pour remonter vers la sortie.")
P.vent(168, 174, 36, 63)
L.shards_line(171, 58, 171, 40, 5)
# upper hall above the gallery (optional loop from the top of the chimney)
L.enemy("magma_gloop", 146, 52, patrol_tiles=5)
L.enemy("fire_wisp", 136, 44, patrol_tiles=4)
L.orb("health_orb", 128, 50)
L.orb("energy_orb", 160, 50)
# smoke of the dead furnaces in the upper hall
L.obj("slow_zone", [140 * T, 45 * T, 12 * T, 7 * T], "gameplay", {"factor": 0.65, "visible": True, "style": "ash"})
L.shards_line(128, 48, 162, 48, 7)
P.scatter(125, 166, 52, 0.4, forge=True)
L.decor("forge_anvil", 140, 52)
P.brazier(154, 52)
P.banner(131, 52)
# out of the foundry
R(174, 50, 200)                               # ground after the chimney (top 50)
P.scatter(178, 196, 50, 0.35)
L.enemy("lava_stalker", 188, 50, patrol_tiles=5, face_left=True)
L.checkpoint(195, 50, "Salle des enclumes")
L.light(195, 45, FORGE_GLOW, 90)
L.orb("health_orb", 191, 48)

# =====================================================================================
# ZONE 3 — La Salle des Enclumes (combat arena, three waves)
# =====================================================================================
L.solid(200, 50, 254, LAVA + 3, "basalt")
L.solid(198, 22, 256, 28, "forge_metal")      # ceiling
L.solid(200, 28, 203, 42, "forge_metal")
L.solid(251, 28, 254, 42, "forge_metal")
arena_l = L.door(201, 42, 8, open_=True)
arena_r = L.door(252, 42, 8)
L.oneway(210, 217, 44, "chain_grate")
L.oneway(237, 244, 44, "chain_grate")
L.oneway(222, 232, 38, "chain_grate")
w1 = [L.enemy("ember_mite", 212, 50, 3, dormant=True), L.enemy("ember_mite", 216, 50, 3, True, dormant=True),
      L.enemy("ember_mite", 236, 50, 3, dormant=True), L.enemy("ember_mite", 240, 50, 3, True, dormant=True),
      L.enemy("magma_gloop", 227, 50, 4, True, dormant=True)]
w2 = [L.enemy("forge_drone", 214, 40, 4, dormant=True), L.enemy("forge_drone", 240, 40, 4, True, dormant=True),
      L.enemy("lava_stalker", 220, 50, 5, dormant=True), L.enemy("lava_stalker", 246, 50, 5, True, dormant=True),
      L.enemy("forge_bulwark", 232, 50, 4, True, dormant=True)]
w3 = [L.enemy("magma_golem", 238, 50, 4, True, dormant=True), L.enemy("fire_wisp", 214, 36, 4, dormant=True),
      L.enemy("ember_mite", 222, 50, 3, dormant=True), L.enemy("ember_mite", 230, 50, 3, True, dormant=True),
      L.enemy("ash_wing", 228, 34, 4, dormant=True)]
arena = L.combat(206, 28, 248, 50, doors=arena_l + "," + arena_r,
                 waves=";".join(",".join(w) for w in (w1, w2, w3)))
L.event(arena, "cleared", [{"type": "hint", "params": {"text": "La salle des enclumes se tait. En avant !",
                                                       "duration": 3.5}}], delay=0.8)
L.decor("burnt_banner", 207, 28, rotation=180)
L.decor("burnt_banner", 247, 28, rotation=180, flip=True)
L.decor("forge_anvil", 226, 50)
L.decor("gear_wreck", 245, 50, alpha=0.9)
P.brazier(208, 50)
P.brazier(244, 50)
P.chains(206, 248, 28, every=7, layer="bg_decor")
L.light(227, 34, "#ff4d1e", 140, flicker=0.25)
L.orb("health_orb", 213.5, 42)
L.orb("health_orb", 240.5, 42)
L.orb("energy_orb", 227, 36)

# =====================================================================================
# ZONE 4 — Les Rivières de Lave (advanced: cracked-floor tower, underground river,
#          chimney, crumbling bridge, bulwarks, forge corridor with an alternate duct)
# =====================================================================================
# 4a. tower of cracked floors (Ground Pound required)
R(254, 50, 262)
L.checkpoint(257, 50, "Puits des cuves")
L.light(257, 45, FORGE_GLOW, 90)
L.solid(260, 30, 288, 36, "forge_metal")      # tower roof
L.solid(260, 36, 262, 44, "forge_metal")      # lintel over the entrance
L.solid(262, 50, 266, 51, "forge_metal")
L.cracked_floor(266, 272, 50)
L.solid(272, 50, 284, 51, "forge_metal")
L.solid(284, 36, 288, 58, "forge_metal")      # right wall: the way on is down
L.sign(264, 50, "La tour des cuves s'enfonce sous la lave... frappez le sol pour descendre.")
for x in (270, 275, 279):
    L.enemy("ember_mite", x, 57, patrol_tiles=2)
L.enemy("forge_bulwark", 266, 57, patrol_tiles=2)
L.solid(262, 57, 274, 58, "forge_metal")
L.cracked_floor(274, 281, 57)
L.solid(281, 57, 284, 58, "forge_metal")
L.light(273, 54, EMBER_GLOW, 60)
L.decor("hanging_chain", 268, 51, "bg_decor")
L.decor("hanging_chain_long", 279, 51, "bg_decor")
L.orb("health_orb", 263.5, 55)
# 4b. underground river: landing, lava falls, grapple chain over open lava
R(262, 63, 286)                               # landing under the tower (top 63)
L.liquid(286, 328, 63.5, LAVA, "lava")             # the underground river
L.solid(288, 34, 346, 44, "basalt")           # cave roof (its top is a secret path)
P.scatter(263, 285, 63, 0.35)
L.enemy("ember_weaver", 282, 63, patrol_tiles=2, face_left=True)
L.oneway(289, 293, 60, "basalt_ledge")
L.oneway(296, 301, 57, "basalt_ledge")
L.solid(293, 44, 296, 51, "basalt")           # stalactite spout
P.lavafall(294.5, 51, 57, hazard=True)        # the lava curtain over the second ledge
L.sign(287, 63, "Les chutes de lave brûlent : traversez-les sous Bouclier.")
L.grapple(306, 52)
L.grapple(314.5, 50)
L.grapple(322.5, 51)
L.shards_line(306, 54, 322, 54, 5)
L.enemy("ash_wing", 316, 58, patrol_tiles=5)
R(328, 55, 346)                               # river landing (top 55)
L.checkpoint(333, 55, "Fleuve souterrain")
L.light(333, 50, FORGE_GLOW, 90)
L.orb("health_orb", 337, 53)
L.enemy("ember_weaver", 343, 55, patrol_tiles=1, face_left=True)
P.scatter(328, 345, 55, 0.35, tall=False)
P.glow_line(288, 346, 66, every=14)
# chimney up to the surface (updraft over a vent rock)
R(347, 63, 352)
L.liquid(346, 347, 64, LAVA, "lava")
P.vent(346, 352, 22, 63)
L.decor("lava_vent", 349, 63)
L.shards_line(349, 58, 349, 30, 6)
L.sign(344.5, 55, "Le courant de la cheminée remonte jusqu'à la surface.")
# secret: roof of the river (glide left from the top of the chimney), crystal wall, relic 2
P.scatter(292, 344, 34, 0.45)
L.shards_line(296, 32, 340, 32, 8)
L.enemy("magma_gloop", 318, 34, patrol_tiles=6)
L.enemy("cinder_bloom", 300, 34)
L.solid(286, 26, 288, 34, "obsidian")
L.solid(288, 26, 292, 28, "obsidian")
L.gate(291, 28, 1, 6, "dash")
L.relic(289.5, 32, "Lingot d'étoile")
L.secret(288, 28, 291, 34, "Le lingot caché sur le toit du fleuve !")
L.light(289.5, 31, "#ffd98a", 60)
# 4c. surface: crumbling obsidian bridge, bulwark bridge with the heart vault, forge corridor
R(352, 36, 366)                               # landing at the top of the chimney (top 36)
P.scatter(353, 365, 36, 0.4)
L.enemy("fire_wisp", 360, 30, patrol_tiles=3)
L.sign(364, 36, "Les ponts d'obsidienne s'effritent : ne traînez pas !")
L.liquid(366, 381, 44, LAVA, "lava")
L.crumble(367, 37, 3, delay=0.4, respawn=3)
L.crumble(372, 38, 3, delay=0.4, respawn=3)
L.crumble(377, 38, 3, delay=0.4, respawn=3)
L.shards_arc(366, 382, 35, 2, 6)
L.solid(381, 40, 398, 44, "forge_metal")      # forge bridge (top 40)
L.solid(383, 44, 386, LAVA + 3, "forge_metal")  # pier
L.enemy("forge_bulwark", 388, 40, patrol_tiles=2, face_left=True)
L.enemy("forge_bulwark", 393, 40, patrol_tiles=2, face_left=True)
# heart vault under the bridge: cracked slab, grapple anchor to climb back out
L.cracked_floor(398, 402, 40)
L.solid(397, 41, 398, 50, "obsidian")
L.solid(402, 44, 404, 50, "obsidian")
L.solid(397, 50, 404, LAVA + 3, "obsidian")
L.grapple(400, 41.5)
L.heart(400, 48)
L.secret(398, 41, 402, 50, "Un cœur de cristal scellé sous le pont !")
L.light(400, 47, "#ff8aa0", 60)
# forge corridor (main route: dash crystal + phase barrier; alternate: cracked floor into the duct)
L.solid(402, 40, 404, 44, "forge_metal")
L.solid(400, 28, 430, 32, "forge_metal")      # corridor roof
L.solid(404, 40, 406, 44, "forge_metal")
L.cracked_floor(406, 410, 40)                  # shortcut into the duct
L.solid(410, 40, 424, 44, "forge_metal")
L.oneway(424, 428, 40, "chain_grate")          # duct exit (updraft below)
L.solid(428, 40, 432, 44, "forge_metal")
L.gate(413, 32, 2, 8, "dash")
L.gate(420, 32, 1, 8, "teleport")
L.sign(411, 40, "Mur de cristal : Dash. Barrière de phase : Téléportation.", auto=True)
L.decor("steam_pipe", 418, 40)
P.chains(402, 430, 32, every=5, layer="bg_decor")
# the duct under the corridor (rows 44-49)
L.solid(404, 49, 430, LAVA + 3, "basalt")
L.solid(430, 40, 432, 49, "forge_metal")
for x in (412, 415, 418):
    L.enemy("ember_mite", x, 49, patrol_tiles=2)
L.shards_line(408, 47, 422, 47, 5)
P.vent(424, 428, 38, 49)
L.light(416, 46, EMBER_GLOW, 60)

# =====================================================================================
# ZONE 5 — La Forge du Marteau (Magmaroth)
# =====================================================================================
R(432, 44, 438)
L.checkpoint(435, 44, "Forge du Marteau")
L.light(435, 39, FORGE_GLOW, 90)
L.orb("health_orb", 437, 42)
L.solid(438, 46, 440, LAVA + 3, "basalt")      # step down to the arena (top 46)
boss, boss_l, boss_r = P.boss_arena(
    440, 490, 50, 26, "magmaroth", 480, ledges=[(448, 455, 44), (475, 482, 44), (460, 470, 37)],
    name="Magmaroth", defeated_text="Le Marteau des Profondeurs s'est tu. La forge s'ouvre.",
    floor_mat="basalt", mat="forge_metal")
L.decor("forge_anvil", 465, 50, scale=1.5)
P.brazier(446, 50)
P.brazier(484, 50)
L.decor("burnt_banner", 452, 26, rotation=180)
L.decor("burnt_banner", 478, 26, rotation=180, flip=True)
P.chains(444, 488, 26, every=6, layer="bg_decor")
L.light(465, 34, LAVA_GLOW, 170, flicker=0.08)
L.orb("health_orb", 451.5, 42)
L.orb("health_orb", 478.5, 42)
L.event(boss, "bossStart", [{"type": "dialogue", "params": {
    "speaker": "Nova", "text": "Un colosse de basalte... Frappez-le après ses coups de marteau !", "duration": 3.5}}])

# =====================================================================================
# ZONE 6 — L'Ascension du Creuset (conclusion: updrafts to the exit, relic 3)
# =====================================================================================
R(490, 50, 500)
L.checkpoint(494, 50, "Ascension du creuset")
L.light(494, 45, FORGE_GLOW, 90)
P.vent(500, 504, 30, 52)
R(500, 52, 504)
R(505, 36, 511)                               # ledge (top 36)
P.vent(511, 515, 20, 40)
R(511, 40, 515)
L.solid(515, 26, W, 28, "basalt")             # exit balcony (top 26)
L.solid(515, 34, W, LAVA + 3, "basalt")
L.solid(W - 2, 0, W, 26, "basalt")
exit_id = L.exit(519, 26)
L.light(519, 22, "#ffd98a", 140)
P.brazier(516.5, 26)
L.shards_line(502, 48, 502, 34, 4)
L.shards_line(513, 38, 513, 24, 4)
# relic 3: alcove behind a plasma curtain in the balcony cliff
L.gate(515, 28, 1, 6, "shield")
L.relic(519.5, 32, "Marteau des Anciens")
L.secret(516, 28, 523, 34, "L'alcôve du marteau !")
L.light(519.5, 31, "#ff8aa0", 50)

# =====================================================================================
# Extra content: checkpoint on the surface, more monsters, props
# =====================================================================================
L.checkpoint(357, 36, "Sortie des cheminées")
L.light(357, 31, FORGE_GLOW, 90)
L.enemy("magma_gloop", 86, 52, patrol_tiles=3, face_left=True)
L.enemy("ember_weaver", 158, 52, patrol_tiles=3, face_left=True)
L.enemy("lava_stalker", 363, 36, patrol_tiles=2, face_left=True)
L.enemy("forge_bomber", 374, 28, patrol_tiles=5)
L.enemy("ash_wing", 507, 30, patrol_tiles=3)
L.enemy("forge_drone", 182, 43, patrol_tiles=4)
L.enemy("cinder_bloom", 340, 55, face_left=True)
P.brazier(258.5, 50)
P.banner(254.5, 50)
L.decor("steam_pipe", 433, 44)
P.chains(262, 284, 51, every=5, layer="fg_decor", long_ratio=0.0)
P.chains(288, 346, 44, every=7, layer="fg_decor", long_ratio=0.3)

# =====================================================================================
# Ambience: lava glow, chains under forge metal, background columns
# =====================================================================================
P.glow_line(0, W, LAVA - 2, every=26)
P.autodecorate(density=0.32, fg_every=1)
for x in range(14, W, 41):
    L.decor("obsidian_spire", x + P.rng.random() * 10, 20 + P.rng.random() * 6, "bg_decor", alpha=0.35,
            tint="#ff9a7a", scale=1.5)

# =====================================================================================
# Events and cinematics
# =====================================================================================
L.event("level", "levelStart", [{"type": "hint", "params": {
    "text": "Traversez les forges englouties jusqu'au creuset.", "duration": 4}}])
L.event("ground_pound", "unlocked", [{"type": "hint", "params": {
    "text": "Plus la chute est longue, plus la Frappe sismique est puissante.", "duration": 4}}], delay=6)
L.event("level", "time", [{"type": "hint", "params": {"text": "Plus que 5 minutes !", "duration": 3}}],
        params={"seconds": 780})
P.intro("PYRALIS", "Les Forges Englouties",
        "Les forges des Anciens sombrent dans la lave... mais leurs marteaux battent encore.",
        "Quelle chaleur ! Le signal vient du fond des forges. Restons loin de la lave.",
        cut=(130, 40), pan=(260, 44), low=(330, 58))
P.outro("CREUSET ATTEINT", "Pyralis — Les Forges Englouties")
d["metadata"] = {"generator": "tools/levelgen/pyralis_01.py"}

L.save(os.path.join(ROOT, "data/levels/pyralis_01.json"))
