#!/usr/bin/env python3
"""Builds data/levels/pyralis_03.json — "La Fonderie Primordiale", last level of Pyralis.

No new power: the hardest Pyralis platforming, combining every ability. Six zones: the foundry
gates (crucibles, first lasers), the casting chain (moving chain under timed lasers, secret on
the housing), the crucible hall (combat arena), the mechanical heart (plasma curtains and phase
barriers, burning chimneys, hanging crucibles, the press and its lava conduit, the vertical forge),
the furnace hall (Forgeron Primordial) and the exit cliff with the hidden second heart.
Run from the repository root:  python3 tools/levelgen/pyralis_03.py
"""
import os

from levelkit import Level, ROOT, T
from pyralis_common import Pyra, FORGE_GLOW, LAVA_GLOW, EMBER_GLOW

W, H = 584, 84
LAVA = 76
L = Level("pyralis_03", "La Fonderie Primordiale", W, H)
P = Pyra(L, LAVA, seed=9031)
R = P.rock
M = "forge_metal"
P.base(W, H,
       "Au cœur de Pyralis, la fonderie primordiale forge encore des machines sans maître. Réunissez tous vos "
       "pouvoirs pour atteindre le fourneau et éteindre le Forgeron Primordial.",
       difficulty=4,
       start_powers=["dash", "wave", "shield", "impulse", "teleport", "glide", "grapple", "ground_pound",
                     "charge_shot"],
       limit=1200, ref=450, medals=(None, 780, 570, 450))
d = L.data

# =====================================================================================
# ZONE 1 — Les Portes de la Fonderie (introduction: crucibles and the first lasers)
# =====================================================================================
R(0, 8, 3)
R(3, 56, 25, mat=M)                               # gantry (top 56)
start = L.start(9, 56)
P.brazier(5, 56)
P.banner(12, 56)
L.decor("gear_wreck", 18, 56)
L.sign(15, 56, "La fonderie primordiale... Chaque pouvoir comptera. Les creusets débordent de lave.")
R(25, 53, 30, mat=M)                              # step (top 53)
# crucible 1 (rims at 50, lava inside), grapple anchor above it
L.solid(30, 50, 32, 64, M)
L.solid(42, 50, 44, 64, M)
L.solid(30, 64, 44, LAVA + 3, M)
L.liquid(32, 42, 53, 64, "lava")
L.grapple(37, 44)
L.shards_arc(32, 42, 47, 2, 5)
L.decor("lava_vent", 37, 53, "bg_decor", alpha=0.8)
# relic 1 cache above the start, reached by two anchors
L.solid(4, 42, 12, 44, M)
L.grapple(17, 48)
L.grapple(14, 43)
L.relic(6, 40.5, "Creuset miniature")
L.shards_line(8, 40.5, 11, 40.5, 3)
L.secret(4, 36, 12, 42, "La niche du fondeur !")
L.light(7, 39, "#ffd98a", 50)
L.enemy("forge_drone", 8, 34, patrol_tiles=3)
# floor with the laser gantry
R(44, 56, 72, mat=M)
L.solid(50, 38, 64, 41, M)                    # laser gantry
L.laser(54, 41, 15, rotation=90, on=1.2, off=1.4, warn=0.5)
L.laser(60, 41, 15, rotation=90, on=1.2, off=1.4, warn=0.5, phase=1.3)
L.sign(47, 56, "Les lasers de la fonderie s'allument par cycles : passez quand ils s'éteignent.")
L.enemy("ember_mite", 49, 56, patrol_tiles=3)
L.enemy("ember_mite", 57, 56, patrol_tiles=3, face_left=True)
L.enemy("forge_drone", 62, 49, patrol_tiles=4)
L.enemy("forge_bulwark", 68, 56, patrol_tiles=3, face_left=True)
P.scatter(45, 70, 56, 0.3, forge=True)
R(72, 56, 90, mat=M)
L.checkpoint(76, 56, "Portes de la fonderie")
L.light(76, 51, FORGE_GLOW, 90)
L.orb("health_orb", 80, 54)
P.scatter(78, 88, 56, 0.35, forge=True)

# =====================================================================================
# ZONE 2 — La Chaîne de Coulée (a moving chain of grates under timed lasers)
# =====================================================================================
L.solid(88, 34, 152, 42, M)                   # housing (its top hides relic 3)
P.chains(89, 151, 42, every=5, layer="bg_decor")
for i, x in enumerate((93, 99, 105, 111, 117, 123, 129, 135)):
    L.moving(x, 56, 3, 5, 0, speed=36, wait=0.6)
L.liquid(90, 146, 62, LAVA, "lava")
for i, x in enumerate((102, 114, 126, 138)):
    L.laser(x, 42, 20, rotation=90, on=1.0, off=1.6, warn=0.5, phase=i * 0.65)
L.sign(86, 56, "La chaîne de coulée avance et recule d'un seul bloc : sautez d'un plateau à l'autre entre deux tirs.")
L.shards_line(95, 53, 141, 53, 9)
L.enemy("ash_wing", 120, 50, patrol_tiles=8)
R(146, 56, 178, mat=M)                            # end of the chain (top 56)
L.orb("health_orb", 149, 54)
L.enemy("lava_stalker", 160, 56, patrol_tiles=5, face_left=True)
L.enemy("ember_weaver", 167, 56, patrol_tiles=3, face_left=True)
P.scatter(150, 176, 56, 0.35, forge=True)
L.checkpoint(150, 56, "Bout de la chaîne")
L.light(150, 51, FORGE_GLOW, 90)
# relic 3: the housing roof, reached by two anchors from the end of the chain
L.grapple(149, 48)
L.grapple(153, 41)
L.grapple(155, 35)
L.relic(90.5, 32.5, "Lingot du Forgeron")
L.shards_line(96, 32, 146, 32, 10)
L.secret(88, 28, 100, 34, "Le lingot oublié sur la chaîne !")
L.light(90.5, 31, "#ffd98a", 60)
L.enemy("forge_drone", 120, 28, patrol_tiles=6)
L.enemy("cinder_bloom", 108, 34)
P.scatter(100, 150, 34, 0.35)

# =====================================================================================
# ZONE 3 — La Salle des Creusets (combat arena, three heavy waves)
# =====================================================================================
L.solid(178, 56, 232, LAVA + 3, "basalt")
L.solid(176, 28, 234, 34, M)
L.solid(178, 34, 181, 48, M)
L.solid(229, 34, 232, 48, M)
a_l = L.door(179, 48, 8, open_=True)
a_r = L.door(230, 48, 8)
L.oneway(187, 194, 50, "chain_grate")
L.oneway(216, 223, 50, "chain_grate")
L.oneway(200, 210, 43, "chain_grate")
w1 = [L.enemy("forge_drone", 190, 46, 4, dormant=True), L.enemy("forge_drone", 220, 46, 4, True, dormant=True),
      L.enemy("ember_mite", 196, 56, 3, dormant=True), L.enemy("ember_mite", 200, 56, 3, True, dormant=True),
      L.enemy("ember_mite", 212, 56, 3, dormant=True), L.enemy("magma_gloop", 216, 56, 4, True, dormant=True)]
w2 = [L.enemy("forge_bulwark", 192, 56, 3, dormant=True), L.enemy("forge_bulwark", 220, 56, 3, True, dormant=True),
      L.enemy("lava_stalker", 205, 56, 5, dormant=True), L.enemy("forge_bomber", 198, 40, 4, dormant=True),
      L.enemy("forge_bomber", 214, 40, 4, True, dormant=True)]
w3 = [L.enemy("magma_golem", 214, 56, 4, True, dormant=True), L.enemy("fire_wisp", 190, 42, 3, dormant=True),
      L.enemy("ember_weaver", 196, 56, 3, dormant=True), L.enemy("ember_weaver", 222, 56, 3, True, dormant=True),
      L.enemy("ash_wing", 205, 38, 4, dormant=True), L.enemy("lava_burrower", 202, 56, dormant=True)]
arena = L.combat(184, 34, 226, 56, doors=a_l + "," + a_r, waves=";".join(",".join(w) for w in (w1, w2, w3)))
L.event(arena, "cleared", [{"type": "hint", "params": {"text": "Les creusets refroidissent. Le cœur mécanique est ouvert.",
                                                       "duration": 3.5}}], delay=0.8)
L.decor("burnt_banner", 185, 34, rotation=180)
L.decor("burnt_banner", 225, 34, rotation=180, flip=True)
P.brazier(186, 56)
P.brazier(224, 56)
L.decor("forge_anvil", 205, 56, scale=1.5)
P.chains(184, 226, 34, every=6, layer="bg_decor")
L.light(205, 40, "#ff4d1e", 140, flicker=0.25)
L.orb("health_orb", 190.5, 48)
L.orb("health_orb", 219.5, 48)
L.orb("energy_orb", 205, 41)

# =====================================================================================
# ZONE 4 — Le Cœur Mécanique (all abilities)
# =====================================================================================
# 4a. plasma curtains and phase barrier; heart 1 in the burning alcove under the floor
R(232, 56, 250, mat=M)
L.checkpoint(235, 56, "Rideaux de plasma")
L.light(235, 51, FORGE_GLOW, 90)
L.solid(240, 40, 284, 46, M)                  # corridor roof
L.gate(244, 46, 1, 10, "shield")
L.sign(241, 56, "Rideau de plasma : Bouclier. Barrière de phase : Téléportation. Le Bouclier se recharge en 5 s.")
L.gate(251.5, 46, 1, 10, "teleport")           # over the pit 250-254
L.liquid(250, 254, 66, LAVA, "lava")
L.solid(254, 56, 266, 58, M)
L.oneway(266, 270, 56, "chain_grate")          # exit of the heart alcove (updraft below)
R(270, 56, 284, mat=M)
L.laser(258, 55.2, 9, rotation=0, on=1.0, off=1.4, warn=0.5)   # ankle laser: jump it
L.laser(262, 46, 10, rotation=90, on=1.1, off=1.5, warn=0.5, phase=0.8)
L.gate(276, 46, 1, 10, "shield")
L.enemy("forge_drone", 268, 50, patrol_tiles=3)
L.orb("energy_orb", 272, 54)
# heart alcove (rows 58-64): glide down the pit, shield through the curtain in its wall
L.gate(254, 58, 1, 6, "shield")
L.solid(254, 64, 270, LAVA + 3, M)
L.heart(259, 62)
L.shards_line(261, 62, 264, 62, 3)
P.vent(266, 270, 50, 64)
L.secret(255, 58, 266, 64, "Un cœur de cristal sous la fonderie !")
L.light(259, 61, "#ff8aa0", 60)
# 4b. burning chimneys: updraft gauntlet crossed by timed lasers, relic 2 behind a crystal wall
R(284, 56, 290, mat=M)
L.orb("health_orb", 287, 54)
R(290, 60, 294)
P.vent(290, 294, 30, 60)
L.laser(286, 44, 9, rotation=0, on=1.2, off=1.3, warn=0.5)
R(294, 34, 300)                               # ledge (top 34)
L.enemy("forge_drone", 297, 28, patrol_tiles=2)
R(300, 44, 304)
P.vent(300, 304, 12, 44)
L.laser(296, 26, 9, rotation=0, on=1.2, off=1.3, warn=0.5, phase=1.2)
R(304, 22, 312)                               # top of the chimneys (top 22)
L.sign(305, 22, "Les creusets suspendus : enchaînez les ancrages du Grappin.")
L.sign(288, 56, "Les cheminées ardentes : planez dans les courants, attention aux lasers.")
L.solid(294, 7, 300, 9, "obsidian")
L.solid(294, 9, 295, 13, "obsidian")
L.solid(294, 13, 299, 14, "obsidian")
L.gate(299, 9, 1, 4, "dash")
L.relic(296.5, 11.5, "Rouage primordial")
L.secret(295, 9, 299, 13, "Le rouage caché au sommet des cheminées !")
L.light(296.5, 11, "#ffd98a", 50)
# 4c. hanging crucible (grapple chain), the press (ground pound twice), the lava conduit (charged shot)
L.solid(316, 32, 318, 38, M)
L.solid(338, 32, 340, 38, M)
L.solid(316, 38, 340, 40, M)
L.liquid(312, 340, 52, LAVA, "lava")
L.liquid(318, 338, 34, 38, "lava")
P.chains(317, 339, 40, every=4, layer="bg_decor", long_ratio=0.8)
L.grapple(318, 21)
L.grapple(326, 20)
L.grapple(334, 20)
L.shards_line(318, 25, 336, 25, 5)
L.enemy("ash_wing", 330, 28, patrol_tiles=5)
L.solid(340, 26, 342, 50, M)                  # press: left wall
L.solid(342, 26, 348, 27, M)
L.cracked_floor(348, 352, 26)
L.solid(352, 26, 358, 27, M)
L.solid(358, 18, 362, 38, M)                  # press: right wall (the way on is down)
L.sign(345, 26, "La presse : frappez le sol, deux fois.")
for x in (344, 347, 350):
    L.enemy("ember_mite", x, 36, patrol_tiles=1)
L.enemy("forge_bulwark", 355, 36, patrol_tiles=1, face_left=True)
L.solid(342, 36, 350, 37, M)
# press smoke: slows the fight in the press chamber
L.obj("slow_zone", [342 * T, 29 * T, 16 * T, 7 * T], "gameplay", {"factor": 0.65, "visible": True, "style": "ash"})
L.cracked_floor(350, 354, 36)
L.solid(354, 36, 358, 37, M)
L.orb("health_orb", 343.5, 34)
R(342, 46, 354, mat=M)                            # conduit landing (top 46)
L.solid(354, 37, 380, 44, M)                  # conduit hood
L.solid(354, 0, 358, 37, M)                   # forge wall up to the vault: the conduit cannot be skipped over the hood
L.liquid(354, 378, 46.5, 50, "lava")
L.solid(354, 50, 378, LAVA + 3, "basalt", deco=False)
raft = L.moving(374, 46, 4, -20, 0, speed=60, wait=1.2, active=False)
R(378, 46, 392, mat=M)
L.receptor(379.5, 46, raft)
L.light(379.5, 43, "#8ffff0", 50)
L.sign(348, 46, "Le conduit de la presse : un Tir chargé sur le récepteur appellera le radeau.")
P.chains(355, 377, 44, every=4, layer="bg_decor", long_ratio=0.0)
L.light(366, 45, LAVA_GLOW, 90, flicker=0.2)
L.checkpoint(383, 46, "Conduit de la presse")
L.light(383, 41, FORGE_GLOW, 90)
P.vent(388, 392, 20, 46)
L.sign(386, 46, "Remontez par la cheminée.")
# 4d. the vertical forge: crystal wall, phase barrier over a pit, laser, grapple gap
R(392, 30, 410, mat=M)
L.solid(399, 14, 423, 22, M)                  # corridor roof
L.gate(401, 22, 2, 8, "dash")
L.gate(411.5, 22, 1, 8, "teleport")            # over the pit 410-414
L.liquid(410, 414, 34, LAVA, "lava")
R(414, 30, 423, mat=M)
L.laser(418, 22, 8, rotation=90, on=1.0, off=1.3, warn=0.5)
L.enemy("forge_bulwark", 421, 30, patrol_tiles=1, face_left=True)
L.enemy("fire_wisp", 396, 24, patrol_tiles=3)
L.sign(395, 30, "Mur de cristal, barrière de phase, laser... puis le vide : gardez le Grappin prêt.")
L.liquid(423, 438, 40, LAVA, "lava")
L.grapple(427, 25)
L.grapple(434, 24)
L.shards_line(425, 28, 436, 28, 4)
L.enemy("forge_bomber", 430, 16, patrol_tiles=5)
R(438, 34, 444, mat=M)                            # antechamber (top 34)
L.checkpoint(441, 34, "Antichambre du fourneau")
L.light(441, 29, FORGE_GLOW, 90)
L.orb("health_orb", 439, 32)
L.orb("health_orb", 443, 32)
L.solid(444, 37, 446, LAVA + 3, M)            # step (top 37)

# =====================================================================================
# ZONE 5 — La Salle du Fourneau (Forgeron Primordial)
# =====================================================================================
boss, boss_l, boss_r = P.boss_arena(
    446, 498, 40, 14, "forge_core", 484, boss_y=32,
    ledges=[(454, 461, 34), (483, 490, 34), (467, 477, 27)],
    name="Forgeron Primordial",
    defeated_text="Le Forgeron Primordial s'éteint. La fonderie se tait enfin.",
    floor_mat="forge_metal", mat="forge_metal")
P.brazier(452, 40)
P.brazier(492, 40)
L.decor("forge_anvil", 472, 40, scale=1.5)
L.decor("gear_wreck", 460, 40, alpha=0.9)
L.decor("gear_wreck", 486, 40, alpha=0.9, flip=True)
L.light(472, 24, LAVA_GLOW, 180, flicker=0.08)
L.orb("health_orb", 457.5, 32)
L.orb("health_orb", 486.5, 32)
L.event(boss, "bossStart", [{"type": "dialogue", "params": {
    "speaker": "Nova", "text": "Le cœur de la fonderie... Il faut l'éteindre !", "duration": 3.5}}])

# =====================================================================================
# ZONE 6 — La Falaise du Creuset (exit; the hidden second heart under the cliff)
# =====================================================================================
R(498, 40, 512, mat=M)
L.checkpoint(502, 40, "Falaise du creuset")
L.light(502, 35, FORGE_GLOW, 90)
R(512, 44, 516)
P.vent(512, 516, 18, 44)
R(516, 28, 524, mat=M)                            # ledge (top 28)
L.liquid(524, 532, 34, LAVA, "lava")
L.enemy("forge_drone", 527, 22, patrol_tiles=3)
# exit cliff: top slab with a cracked floor (secret) and a grate (way back up)
L.solid(532, 24, 538, 25, "basalt")
L.cracked_floor(538, 541, 24)
L.solid(541, 24, 556, 25, "basalt")
L.oneway(556, 560, 24, "chain_grate")
L.solid(560, 24, W, 25, "basalt")
L.solid(532, 25, 536, LAVA + 3, "basalt")
L.solid(536, 33, 562, LAVA + 3, "basalt")
L.solid(562, 25, W, LAVA + 3, "basalt")
L.solid(W - 2, 0, W, 24, "basalt")
exit_id = L.exit(572, 24)
L.light(572, 20, "#ffd98a", 140)
P.brazier(566, 24)
P.brazier(579, 24)
# hidden gauntlet under the cliff: energy barrier, plasma curtain, phase barrier, heart
L.gate(544, 25, 1, 8, "wave")
L.gate(548, 25, 1, 8, "shield")
L.gate(552, 25, 1, 8, "teleport")
L.heart(555, 31)
P.vent(556, 560, 18, 33)
L.secret(537, 25, 562, 33, "Le second cœur de cristal, au cœur de la falaise !")
L.light(555, 30, "#ff8aa0", 60)
L.shards_line(539, 31, 542, 31, 3)

# =====================================================================================
# Extra content: checkpoints, shard trails, more monsters, props
# =====================================================================================
L.checkpoint(286, 56, "Cheminées ardentes")
L.light(286, 51, FORGE_GLOW, 90)
L.checkpoint(344, 26, "La presse")
L.light(344, 21, FORGE_GLOW, 90)
L.shards_line(13, 54, 23, 54, 4)
L.shards_line(47, 53, 69, 53, 5)
L.shards_line(188, 48, 193, 48, 2)
L.shards_line(217, 48, 222, 48, 2)
L.shards_line(255, 54, 265, 54, 4)
L.shards_line(292, 56, 292, 37, 4)
L.shards_line(302, 41, 302, 25, 4)
L.shards_line(343, 34, 357, 34, 4)
L.shards_line(357, 45, 373, 45, 5)
L.shards_line(393, 28, 399, 28, 3)
L.shards_line(414, 28, 420, 28, 3)
L.shards_line(514, 42, 514, 26, 4)
L.shards_line(517, 26, 523, 26, 3)
L.enemy("ember_mite", 22, 56, patrol_tiles=2, face_left=True)
L.enemy("forge_bulwark", 157, 56, patrol_tiles=2, face_left=True)
L.enemy("magma_gloop", 165, 56, patrol_tiles=3)
L.enemy("lava_stalker", 274, 56, patrol_tiles=2, face_left=True)
L.enemy("ash_wing", 298, 20, patrol_tiles=4)
L.enemy("fire_wisp", 309, 14, patrol_tiles=3)
L.enemy("ash_wing", 430, 30, patrol_tiles=4)
L.enemy("forge_bomber", 520, 14, patrol_tiles=4)
L.enemy("cinder_bloom", 522.5, 28, face_left=True)
L.enemy("ember_mite", 506, 40, patrol_tiles=2)
P.brazier(234, 56)
P.banner(239, 56)
P.brazier(283, 56)
L.decor("steam_pipe", 289, 56)
L.decor("forge_anvil", 386, 46)
P.brazier(392.5, 30)
L.decor("steam_pipe", 437.5, 34)
P.chains(240, 284, 46, every=5, layer="fg_decor", long_ratio=0.0)
P.chains(399, 423, 22, every=5, layer="fg_decor", long_ratio=0.0)

# =====================================================================================
# Ambience
# =====================================================================================
P.glow_line(0, W, LAVA - 2, every=24)
P.autodecorate(density=0.32, fg_every=1)
for x in range(12, W, 39):
    L.decor("obsidian_spire", x + P.rng.random() * 10, 20 + P.rng.random() * 6, "bg_decor", alpha=0.35,
            tint="#ff9a7a", scale=1.5)

# =====================================================================================
# Events and cinematics
# =====================================================================================
L.event("level", "levelStart", [{"type": "hint", "params": {
    "text": "Atteignez le fourneau au cœur de la fonderie.", "duration": 4}}])
L.event("level", "time", [{"type": "hint", "params": {"text": "Plus que 5 minutes !", "duration": 3}}],
        params={"seconds": 900})
P.intro("PYRALIS", "La Fonderie Primordiale",
        "Ici, les Anciens forgeaient des étoiles. La fonderie, elle, ne s'est jamais arrêtée.",
        "Toute la fonderie tremble... Le cœur bat encore, là-bas. Allons-y.",
        cut=(240, 30), pan=(470, 26), low=(366, 44))
P.outro("FONDERIE ÉTEINTE", "Pyralis — La Fonderie Primordiale")
d["metadata"] = {"generator": "tools/levelgen/pyralis_03.py", "revision": 3}

L.save(os.path.join(ROOT, "data/levels/pyralis_03.json"))
