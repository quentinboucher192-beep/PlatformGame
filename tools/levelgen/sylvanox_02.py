#!/usr/bin/env python3
"""Builds data/levels/sylvanox_02.json — "La Canopée des Spores", second level of Sylvanox.

The giant bioluminescent canopy above the abyss: root bridges, mushroom caps, spore clouds,
updraft chimneys. New power: Glide (Planeur). Six zones:
  1. La Lisière sporale       — warm-up with the known powers, spore clouds, mushroom bounce
  2. Le Belvédère des Vents    — Glide tablet, safe tutorial basin (long drop + updraft), first gaps
  3. Le Tronc creux            — combat arena (3 waves) inside a hollow tree, heart behind plasma
  4. Les Cimes tourmentées     — glide + updrafts + dash + crumbling + webs, upper/lower routes
  5. La Chambre noyée          — boss: the Spore Queen (Reine Sporale), rooted in a flooded chamber
  6. La Couronne               — updraft chimney to the exit portal
Run from the repository root:  python3 tools/levelgen/sylvanox_02.py
"""
import os
import random

from levelkit import Level, ROOT
import sylvanox_common as C

random.seed(20202)
W, H = 530, 80
ACID = 72          # toxic abyss surface (tiles)
I = "island"
L = Level("sylvanox_02", "La Canopée des Spores", W, H)
L.planet("sylvanox")
d = L.data
d["description"] = ("Sous les ruines, la canopée géante de Sylvanox respire des nuages de spores au-dessus de "
                    "l'abîme. Apprenez à planer sur ses courants ascendants et affrontez la Reine Sporale "
                    "dans sa chambre noyée.")
d["difficulty"] = 3
if os.path.exists(os.path.join(ROOT, "assets/music/sylvanox_depths.ogg")):
    d["music"] = "music/sylvanox_depths.ogg"
d["ambientLight"] = "#7470a6"
d["backgroundColor"] = "#0c0b1e"
d["killY"] = H * 16 + 24
for layer in d["background"]:
    if layer["texture"].endswith(("mid.png", "near.png")):
        layer["tint"] = "#d8c8ff"           # spore haze: the canopy is more violet than the ruins
    if layer.get("foreground"):
        layer["offset"] = [0, -470]         # foreground canopy band above the highest branch route
d["timer"] = {"mode": "limited", "limit": 1080, "onExpire": "gameover", "referenceTime": 330,
              "challengeMaxDeaths": 0}
d["medals"] = {"bronze": {}, "silver": {"maxTime": 600}, "gold": {"maxTime": 420},
               "platinum": {"maxTime": 330, "maxDeaths": 0}}
d["startPowers"] = ["dash", "wave", "shield", "impulse", "teleport"]

L.liquid(0, W, ACID, H, "acid")
L.emitter(0, ACID - 8, W, ACID, "abyss_motes", 7)


def _island_bottom(x0, y0, x1):
    return min(y0 + 7 + int(random.random() * 5), ACID + 3)


L.island_bottom = _island_bottom
SPORES = dict(small=C.SPORE_SMALL, tall_kinds=C.SPORE_TALL)


def flora(x0, x1, y, density=0.4, layer="bg_decor", tall=True):
    C.flora(L, x0, x1, y, density, layer, tall, **SPORES)


# =====================================================================================
# ZONE 1 — La Lisière sporale (warm-up: spore clouds, mushroom bounce, wave barrier)
# =====================================================================================
L.solid(0, 12, 3, H, "root")                       # left trunk
L.solid(3, 58, 30, I, "root")                      # start terrace (top 58)
start = L.start(8, 58)
L.decor("ruin_arch", 18, 58)
L.decor("statue_head", 6, 58)
C.giant_mushroom(L, 26, 58, 2.2)
flora(3, 23, 58)
L.light(18, 53, "#39f0d8", 80)
L.sign(14, 58, "La Canopée des Spores... Les nuages violets engluent vos mouvements : contournez-les ou "
               "traversez-les vite.")
L.emitter(3, 44, 30, 57, "fireflies", 1.2)
L.shards_line(22, 55, 28, 55, 3)

# long root bridge with a spore cloud
L.solid(30, 58, 56, 60, "root")
C.spore_cloud(L, 38, 52, 46, 58)
L.enemy("glowmite", 34, 58, patrol_tiles=3)
L.enemy("sporeflower", 53, 58, face_left=True)
L.enemy("nightwing", 43, 48, patrol_tiles=3)
L.shards_arc(37, 47, 51, 1.5, 5)
L.decor("mushroom_cluster", 50, 58)
L.decor("glow_fern", 32, 58)

# under the bridge: a ledge (safety net) and the phase vault of relic 1
L.oneway(50, 61, 64, "root_branch")
L.solid(41, 64, 50, 67, "root")                    # vault floor
L.solid(41, 60, 44, 64, "root")                    # vault back wall
L.gate(49, 60, 1, 4, "teleport")
L.relic(46.5, 62.5, "Graine d'aube")
L.secret(44, 60, 49, 64, "Une graine cachée sous le pont de racines !")
L.light(46.5, 62, "#e08aff", 60)
L.shards_line(52, 62.5, 57, 62.5, 3)

# mushroom cap with a bounce pad, up to the next canopy layer
L.solid(61, 62, 68, I, "root")
L.solid(61, 61, 63, 62, "root", deco=False)          # root bump: step off it onto the pad
L.bounce(64.5, 62, power=600)
C.giant_mushroom(L, 63, 62, 1.6)
L.sign(62, 62, "Rebondissez sur le champignon pour atteindre la canopée.")
L.shards_line(64.5, 57, 64.5, 50, 3)

L.solid(68, 50, 74, 68, "root")                    # cliff above the mushroom cap
L.solid(74, 50, 88, I, "root")
L.checkpoint(71, 50, "Lisière sporale")
L.light(71, 45, "#39f0d8", 90)
L.enemy("gloop", 77, 50, patrol_tiles=3)
L.enemy("gloop", 82, 50, patrol_tiles=2, face_left=True)
C.spore_cloud(L, 79, 45, 85, 50)
L.enemy("sporeflower", 86.5, 50, face_left=True)
flora(72, 86, 50, 0.3)
L.orb("health_orb", 74, 47)
L.enemy("nightwing", 91, 45, patrol_tiles=3)
L.shards_arc(88, 95, 47, 3, 3)

L.solid(95, 47, 112, I, "root")
L.solid(112, 47, 128, 70, "root")                  # trunk: also the left wall of the glide basin
L.enemy("weaver", 101, 47, patrol_tiles=2, face_left=True)
L.enemy("glowmite", 98, 47, patrol_tiles=2)
L.solid(104, 33, 122, 40, "root")                  # overhanging branch
barrier = L.gate(110, 40, 1, 7, "wave")
L.sign(106, 47, "Une barrière d'énergie barre la canopée : dissolvez-la avec l'Onde.")
L.decor("tech_panel", 108, 47)
L.light(110, 43, "#39f0d8", 60)
flora(112, 126, 47, 0.3, tall=False)
C.vines(L, 30, 125, 26)
L.emitter(30, 30, 125, 46, "falling_leaves", 1.5)

# =====================================================================================
# ZONE 2 — Le Belvédère des Vents (Glide tablet, safe tutorial basin, first glide gaps)
# =====================================================================================
L.checkpoint(114, 47, "Belvédère des Vents")
L.light(114, 42, "#39f0d8", 90)
L.power(120, 45.7, "glide", "Planeur : maintenez Saut pendant une chute pour déployer les ailes d'énergie. "
                          "Les courants ascendants vous élèvent.")
L.light(120, 41, "#8ffff0", 110)
L.decor("ruin_arch", 120, 47)
L.sign(125, 47, "Planeur : maintenez Saut en tombant pour planer jusqu'à la corniche de racines.")

# safe basin: a long gentle drop onto a mushroom cap, nothing deadly below
L.solid(128, 62, 160, 70, "root")                  # basin floor
L.ladder(128, 45, 62)                               # vines back up the trunk (no softlock in the basin)
L.solid(141, 53, 149, 55, "root")                  # root ledge: the glide target
L.light(145, 51, "#8ffff0", 70)
L.shards_arc(130, 143, 49, 0.8, 5)
L.orb("energy_orb", 147, 51)
flora(128, 156, 62, 0.45)
L.sign(153, 62, "Courant ascendant : sautez dedans et maintenez Saut pour vous élever.")
L.updraft(156, 160, 28, 62)
L.shards_line(158, 56, 158, 34, 4)
L.emitter(128, 50, 160, 62, "fireflies", 1.5)

# the belvedere (top 34), with a secret chimney to a perch (relic 2)
L.solid(160, 34, 176, 70, "ruin_stone")
L.decor("ruin_pillar", 162, 34)
L.decor("ruin_pillar_broken", 170, 34)
L.decor("statue_head", 173, 34, flip=True)
L.decor("holo_glyph", 167, 34)
L.sign(168, 34, "Planez au-dessus du gouffre. Relâchez Saut pour replier les ailes.")
L.updraft(163, 166, 9, 34)                         # secret chimney (the wind gives it away)
L.solid(166, 13, 173, 15, "ruin_stone", deco=False)
L.relic(170, 11.5, "Plume des vents")
L.secret(166, 6, 174, 13, "Le perchoir des vents !")
L.light(170, 11, "#ffd98a", 60)
L.decor("lantern_pod", 168, 15, sway=1.5)
L.decor("lantern_pod", 172, 15, sway=1.5)
L.shards_line(164.5, 30, 164.5, 16, 4)

# first glide gap: 24 tiles over the abyss, 6 tiles lower
L.shards_arc(178, 198, 33, 2.5, 6)
L.solid(200, 40, 218, I, "root")
L.checkpoint(203, 40, "Premier envol")
L.light(203, 35, "#39f0d8", 90)
C.giant_mushroom(L, 209, 40, 2.4)
L.enemy("nightwing", 208, 35, patrol_tiles=3)
L.enemy("glowmite", 212, 40, patrol_tiles=3)
L.enemy("weaver", 216, 40, patrol_tiles=1, face_left=True)
L.orb("health_orb", 206, 37)
flora(200, 216, 40, 0.35, tall=False)
L.sign(215, 40, "Ce courant traverse le gouffre : planez dedans pour reprendre de l'altitude.")

# second gap: an updraft rising from the abyss lifts the glider to a higher ledge
C.abyss_updraft(L, 232, 236, 18, ACID)
L.light(234, ACID - 2, "#8ffff0", 120)
L.shards_line(222, 41, 230, 41, 4)
L.shards_line(234, 34, 234, 22, 3)
L.shards_arc(237, 251, 21, 1.5, 5)
L.enemy("nightwing", 243, 22, patrol_tiles=3)
L.solid(252, 30, 268, I, "root")
L.checkpoint(256, 30, "Cimes venteuses")
L.light(256, 25, "#39f0d8", 90)
flora(252, 266, 30, 0.35)
L.orb("health_orb", 262, 27)
L.enemy("nightwing", 270, 28, patrol_tiles=3)
C.vines(L, 130, 268, 14)
L.emitter(176, 18, 252, 40, "falling_leaves", 2)

# =====================================================================================
# ZONE 3 — Le Tronc creux (combat arena inside a hollow tree, heart behind plasma)
# =====================================================================================
# descent along the outer roots of the trunk
L.oneway(269, 275, 37, "root_branch")
L.oneway(262, 268, 44, "root_branch")
L.shards_line(271, 35, 273, 35, 2)
L.solid(256, 52, 276, I, "root")
L.enemy("sporeflower", 259, 52)
L.enemy("burrower", 266, 52, face_left=True)
flora(256, 274, 52, 0.4)
L.decor("roots", 273, 52)

# the trunk
TL, TR = 276, 328                                    # outer faces of the trunk
L.solid(TL, 52, TR, ACID + 2, "root")                # trunk base / arena floor (top 52)
L.solid(TL, 26, TR, 33, "root")                      # arena ceiling (19 tiles above the floor)
L.solid(TL, 33, 280, 44, "root")                     # left wall above the door
L.solid(324, 33, TR, 44, "root")                     # right wall above the door
left_door = L.door(279, 44, 8, open_=True)
right_door = L.door(324, 44, 8)
L.oneway(283, 290, 46, "root_branch")
L.oneway(314, 321, 46, "root_branch")
L.oneway(295, 309, 41, "root_branch")
L.oneway(284, 291, 37, "root_branch")
L.oneway(313, 320, 37, "root_branch")
w1 = [L.enemy("gloop", 288, 52, 3, dormant=True), L.enemy("gloop", 316, 52, 3, True, dormant=True),
      L.enemy("glowmite", 302, 52, 4, dormant=True), L.enemy("weaver", 302, 41, 3, dormant=True)]
w2 = [L.enemy("nightwing", 290, 41, 3, dormant=True), L.enemy("nightwing", 314, 41, 3, True, dormant=True),
      L.enemy("bulwark", 304, 52, 5, True, dormant=True), L.enemy("stalker", 292, 52, 5, dormant=True)]
w3 = [L.enemy("bomber", 296, 38, 4, dormant=True), L.enemy("bomber", 310, 37, 4, True, dormant=True),
      L.enemy("golem", 306, 52, 3, True, dormant=True), L.enemy("weaver", 287, 37, 2, dormant=True),
      L.enemy("stalker", 316, 52, 4, True, dormant=True)]
L.combat(280, 33, 324, 52, doors=left_door + "," + right_door,
         waves=";".join(",".join(w) for w in (w1, w2, w3)))
L.orb("health_orb", 287, 44)
L.orb("health_orb", 317, 44)
L.orb("health_orb", 302, 39)
L.orb("energy_orb", 287.5, 35)
L.orb("energy_orb", 316.5, 35)
L.light(302, 38, "#ff4d6d", 130, flicker=0.25)
L.light(287, 48, "#39f0d8", 60)
L.light(317, 48, "#39f0d8", 60)
for x in range(283, 322, 4):
    L.decor(random.choice(["lantern_pod", "hanging_vine_short", "hanging_vine_long", "roots"]), x + random.random(),
            33, "bg_decor", sway=1.0)
flora(281, 323, 52, 0.45)
L.decor("roots", 282, 52)
L.decor("roots", 322, 52, flip=True)
L.decor("mushroom_cluster", 299, 41)
L.decor("glow_fern", 306, 41)
L.decor("glow_flowers", 287, 37)
L.decor("mushroom_small", 316, 37)

# crown of the trunk and the heart chamber behind a plasma curtain
L.solid(274, 8, 331, 19, "root")
L.solid(TL, 19, 312, 26, "root")
L.gate(327, 19, 1, 7, "shield")
L.heart(316, 24.9)
L.orb("health_orb", 320, 24.5)
L.shards_line(314, 24.5, 324, 24.5, 5)
L.secret(312, 19, 327, 26, "Le cœur du grand arbre !")
L.light(316, 22, "#ff8aa0", 70)
L.decor("crystal_cluster", 313.5, 26)
L.decor("lantern_pod", 319, 19, sway=1.0)
L.oneway(328, 333, 26, "root_branch")              # bark ledge in front of the curtain
L.ladder(330, 26, 52)                               # vines hanging from it
L.sign(332, 52, "Des lianes grimpent le long de l'écorce... Un rideau de plasma luit tout là-haut.")
L.shards_line(330.5, 46, 330.5, 30, 3)

# =====================================================================================
# ZONE 4 — Les Cimes tourmentées (glide + updrafts + dash + crumbling + webs; 2 routes)
# =====================================================================================
L.solid(TR, 52, 346, I, "root")
L.checkpoint(336, 52, "Tronc creux")
L.light(336, 47, "#39f0d8", 90)
flora(330, 344, 52, 0.3, tall=False)
L.sign(343, 52, "Les cimes se déchaînent : brisez le cristal d'un Dash, planez entre les nuages.")
L.orb("health_orb", 339, 49)

# --- lower route: crumbling roots, web spinner, dash crystal, glide past a spore cloud
L.crumble(349, 52, 3)
L.crumble(354, 53, 3)
L.shards_line(350, 50, 356, 50, 3)
L.solid(360, 52, 365, I, "root")
L.enemy("weaver", 362.5, 52, patrol_tiles=1, face_left=True)
L.solid(360, 12, 400, 15, "root")                  # great branch (upper route walks on it)
L.solid(368, 15, 372, 44, "root")                  # hanging root
L.gate(369, 44, 2, 18, "dash")                     # crystal curtain under it: dash through
L.light(370, 50, "#b04cff", 90)
L.shards_line(367, 48, 373, 48, 3)
L.solid(375, 52, 383, I, "root")
L.enemy("bomber", 380, 44, patrol_tiles=3)
L.enemy("wisp", 394, 32, patrol_tiles=2)
L.orb("health_orb", 377, 49)
C.spore_cloud(L, 391, 36, 396, 52)                  # spore column across the gap: costs altitude
L.crumble(397, 55, 3)                               # catch net at the foot of the next stump
L.enemy("nightwing", 403, 44, patrol_tiles=2)
L.shards_arc(384, 390, 46, 1.0, 4)
L.shards_line(397, 48, 401, 50, 3)

# --- upper route: bounce, updraft from the crumbling roots to the great branch, relic 3
L.bounce(340.5, 52, power=580)
L.oneway(337, 345, 40, "root_branch")
L.shards_line(340.5, 47, 340.5, 42, 2)
C.abyss_updraft(L, 350, 353, 8, 51)
L.shards_line(351.5, 34, 351.5, 14, 4)
L.enemy("weaver", 371, 12, patrol_tiles=2)
C.spore_cloud(L, 376, 7, 382, 12)
L.enemy("weaver", 386, 12, patrol_tiles=2, face_left=True)
L.enemy("nightwing", 380, 5, patrol_tiles=3)
L.enemy("bulwark", 389, 12, patrol_tiles=1, face_left=True)
L.shards_line(362, 10, 374, 10, 5)
L.orb("energy_orb", 384, 10)
flora(360, 392, 12, 0.35, tall=False)
L.solid(392, 4, 400, 8, "crystal")                 # relic nook (crystal), dash through its wall
L.solid(399, 8, 400, 12, "crystal")
L.gate(393, 8, 1, 4, "dash")
L.relic(396.5, 10.5, "Sève de la Reine")
L.secret(394, 8, 399, 12, "Une niche de cristal au sommet des cimes !")
L.light(396.5, 10, "#e08aff", 60)

# --- merge
L.solid(400, 53, 418, I, "root")
L.checkpoint(407, 53, "Cimes tourmentées")
L.light(407, 48, "#39f0d8", 90)
L.enemy("bulwark", 414, 53, patrol_tiles=2, face_left=True)
L.enemy("glowmite", 410, 53, patrol_tiles=2)
L.orb("health_orb", 410, 50)
flora(404, 417, 53, 0.3, tall=False)
C.vines(L, 346, 420, 16)
L.emitter(346, 18, 420, 50, "spores", 2)

# --- the flooded gallery leading to the queen
L.solid(418, 30, 452, 40, "root")                  # gallery roof
L.solid(418, 64, 452, ACID + 2, "root")            # gallery bed
L.liquid(418, 444, 59, 64, "water")
L.oneway(421, 427, 56, "root_branch")
L.oneway(430, 436, 55, "root_branch")
L.oneway(438, 443, 56, "root_branch")
L.solid(444, 58, 452, 64, "root")
L.enemy("gloop", 425.5, 56, patrol_tiles=1)
L.enemy("sporeflower", 422, 56)
L.enemy("gloop", 441, 56, patrol_tiles=1, face_left=True)
L.enemy("weaver", 433, 55, patrol_tiles=2, face_left=True)
L.enemy("bomber", 437, 48, patrol_tiles=3)
C.spore_cloud(L, 426, 50, 432, 55)
L.shards_line(422, 54, 442, 53, 5)
for x in range(420, 451, 3):
    L.decor(random.choice(["roots", "hanging_vine_long", "lantern_pod", "hanging_vine_short"]), x + random.random(),
            40, "bg_decor", sway=1.2)
L.light(430, 58, "#4cc9ff", 120)
L.emitter(418, 56, 444, 60, "fireflies", 1)

# =====================================================================================
# ZONE 5 — La Chambre noyée (boss: Reine Sporale)
# =====================================================================================
L.checkpoint(449, 58, "Chambre noyée")
L.light(449, 53, "#39f0d8", 90)
L.orb("health_orb", 446, 56)
AL, AR = 452, 506
L.solid(AL, 26, AR, 32, "root")                    # chamber vault
L.solid(AL, 32, 455, 50, "root")
L.solid(502, 32, AR, 50, "root")
L.solid(AL, 58, 499, ACID + 2, "root")             # flat floor (44 tiles of fighting room)
L.solid(499, 61, AR, ACID + 2, "root")             # flooded pit behind the queen
L.solid(502, 58, AR, 61, "root")
L.liquid(499, 502, 58, 61, "water")
L.oneway(499, 502, 58, "root_branch")              # root bridge over the pool to the exit door
boss_l = L.door(455, 50, 8, open_=True)
boss_r = L.door(502, 50, 8)
L.oneway(461, 468, 51, "root_branch")
L.oneway(473, 482, 45, "root_branch")
L.oneway(484, 490, 51, "root_branch")
boss = L.enemy("spore_queen", 495, 58, patrol_tiles=2, face_left=True, dormant=True, boss=True)
C.boss_arena(L, boss, boss_l, boss_r, 456, 32, 502, 58, d["bossMusic"], zoom=0.75, cam=(AL, 26, AR, 62))
L.orb("health_orb", 464.5, 49)
L.orb("health_orb", 477.5, 43)
L.orb("energy_orb", 487, 49)
L.decor("roots", 495, 58, scale=2.0)
L.decor("crystal_big", 459, 58)
L.decor("mushroom_tall", 470, 58, scale=1.5)
L.decor("glow_flowers", 480, 58)
L.decor("mushroom_cluster", 466, 58)
L.decor("crystal_cluster", 489, 58)
L.light(478, 36, "#b04cff", 170, flicker=0.06)
L.light(495, 52, "#e08aff", 90)
for x in range(458, 501, 4):
    L.decor(random.choice(["roots", "hanging_vine_long", "lantern_pod"]), x + random.random(), 32, "bg_decor",
            sway=1.0)
L.emitter(456, 34, 502, 56, "spores", 2.5)
L.event(boss, "bossDefeated", [
    {"type": "hint", "params": {"text": "La Reine Sporale s'effondre... Le chemin de la couronne est libre.",
                                "duration": 4}},
], delay=1.5, name="Reine vaincue")
L.event(boss, "bossPhase", [{"type": "shake", "params": {"amount": 0.4}}], once=False)

# =====================================================================================
# ZONE 6 — La Couronne (updraft chimney to the exit)
# =====================================================================================
L.solid(AR, 58, W, ACID + 2, "root")
L.solid(AR, 20, 512, 50, "root")                   # chimney left wall
L.solid(516, 26, W, 58, "root")                    # crown (top 26), chimney right wall
L.checkpoint(509, 58, "Couronne")
L.light(509, 53, "#39f0d8", 90)
L.updraft(512, 516, 24, 58)
L.ladder(515, 27, 58)                              # vines too, for the patient climbers
L.shards_line(514, 54, 514, 30, 4)
L.decor("ruin_arch", 524, 26, "bg_decor", scale=1.25)
exit_id = L.exit(524, 26)
L.light(524, 22, "#ffd98a", 140)
L.decor("holo_glyph", 518, 26)
L.decor("mushroom_tall", 528, 26, scale=1.4)
L.emitter(516, 14, W, 26, "fireflies", 2)

# =====================================================================================
# Background dressing
# =====================================================================================
C.hanging_under(L, ACID - 1, skip=lambda x, b: 276 <= x < 331 or 418 <= x < 531)
C.bg_rocks(L, 20, W - 20, 14, 30, every=41)
for x in range(40, W - 40, 53):
    if not (270 <= x <= 335 or 410 <= x <= 520):
        L.decor("mushroom_tall", x + random.random() * 8, ACID - 1, "bg_decor", scale=3.0, alpha=0.55,
                tint="#8f7fc8")

# =====================================================================================
# Events and cinematics
# =====================================================================================
L.event("level", "levelStart", [{"type": "hint", "params": {
    "text": "Traversez la canopée jusqu'à sa couronne.", "duration": 4}}])
L.event("glide", "unlocked", [{"type": "hint", "params": {
    "text": "Un nouvel appui sur Saut replie les ailes (double saut), maintenez-le ensuite pour replaner.",
    "duration": 5}}], delay=6)
L.event("level", "time", [{"type": "hint", "params": {"text": "Plus que 5 minutes !", "duration": 3}}],
        params={"seconds": 780})

d["intro"] = {"skippable": True, "letterbox": True, "steps": [
    {"type": "fade", "duration": 0.01, "params": {"to": 1}},
    {"type": "cut", "duration": 0.01, "params": {"x": 2400, "y": 560}},
    {"type": "zoom", "duration": 0.01, "params": {"zoom": 0.8}},
    {"type": "fade", "duration": 1.6, "wait": False, "params": {"to": 0}},
    {"type": "text", "duration": 4.0, "wait": False, "params": {"text": "SYLVANOX", "subtitle": "La Canopée des Spores"}},
    {"type": "camera", "duration": 6.0, "params": {"x": 3500, "y": 500, "ease": "inOutSine"}},
    {"type": "text", "duration": 3.5, "wait": False, "params": {
        "text": "La canopée géante respire au-dessus de l'abîme... et ses spores ne dorment jamais.",
        "style": "caption"}},
    {"type": "camera", "duration": 3.0, "params": {"x": 3100, "y": 900, "ease": "inOutCubic"}},
    {"type": "wait", "duration": 0.5},
    {"type": "zoom", "duration": 3.0, "wait": False, "params": {"zoom": 1.0}},
    {"type": "camera", "duration": 3.0, "params": {"start": True, "ease": "inOutCubic"}},
    {"type": "spawnPlayer", "duration": 0.9},
    {"type": "dialogue", "duration": 3.6, "params": {"speaker": "Nova", "text":
        "Le signal descend sous la canopée. Ces spores... mieux vaut ne pas s'y attarder."}},
]}
d["outro"] = {"skippable": True, "letterbox": True, "steps": [
    {"type": "zoom", "duration": 1.4, "wait": False, "params": {"zoom": 1.4}},
    {"type": "camera", "duration": 1.4, "params": {"player": True}},
    {"type": "text", "duration": 3.0, "wait": False, "params": {"text": "CANOPÉE TRAVERSÉE",
                                                               "subtitle": "Sylvanox — La Canopée des Spores"}},
    {"type": "playerAnim", "duration": 2.2, "params": {"anim": "victory"}},
    {"type": "dialogue", "duration": 3.2, "params": {"speaker": "Nova", "text":
        "Le signal vient d'en dessous... des ruines. Il y a une machine là-dessous."}},
    {"type": "flash", "duration": 0.6, "params": {"color": "#e6fffb"}},
    {"type": "fade", "duration": 0.9, "params": {"to": 1}},
]}
d["metadata"] = {"generator": "tools/levelgen/sylvanox_02.py"}

L.save(os.path.join(ROOT, "data/levels/sylvanox_02.json"))
