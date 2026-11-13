#!/usr/bin/env python3
"""Builds data/levels/sylvanox_01.json — "Les Ruines Suspendues", first level of Sylvanox.

Six zones: introduction, exploration, combat, advanced platforming, mini-boss and
conclusion. Run from the repository root:  python3 tools/levelgen/sylvanox_01.py
The result is a normal level file that can be edited in the in-game level editor.
"""
import os
import random

from levelkit import Level, ROOT

random.seed(7)
W, H = 660, 76
ACID = 64          # toxic lake surface (tiles)
I = "island"       # floating island bottom (computed)
L = Level("sylvanox_01", "Les Ruines Suspendues", W, H)
d = L.data
d["description"] = ("Au-dessus de l'abîme toxique de Sylvanox, les ruines des Anciens dérivent dans la nuit "
                    "bioluminescente. Traversez la canopée brisée, récupérez les pouvoirs oubliés et affrontez "
                    "le Gardien qui protège le relais.")
d["difficulty"] = 2
d["music"] = "music/sylvanox_level.ogg"
d["bossMusic"] = "music/sylvanox_boss.ogg"
d["ambience"] = "music/sylvanox_ambience.ogg"
d["ambientLight"] = "#7480a8"
d["backgroundColor"] = "#0b0f1e"
d["killY"] = 1240
d["background"] = [
    {"texture": "backgrounds/sylvanox/sky.png", "factor": [0.0, 0.0], "fill": True},
    {"texture": "backgrounds/sylvanox/far.png", "factor": [0.1, 0.05], "offset": [0, -20]},
    {"texture": "backgrounds/sylvanox/mid.png", "factor": [0.25, 0.12], "offset": [0, 10]},
    {"texture": "backgrounds/sylvanox/abyss.png", "factor": [0.4, 0.3], "offset": [0, 210], "fillBelow": "#2f7a1a"},
    {"texture": "backgrounds/sylvanox/near.png", "factor": [0.5, 0.25], "offset": [0, 60]},
    {"texture": "backgrounds/sylvanox/fg_vines.png", "factor": [1.15, 1.0], "offset": [0, -330], "foreground": True,
     "tint": "#ffffffc0"},
]
d["timer"] = {"mode": "limited", "limit": 900, "onExpire": "gameover", "referenceTime": 210, "challengeMaxDeaths": 0}
d["medals"] = {"bronze": {}, "silver": {"maxTime": 420}, "gold": {"maxTime": 270},
               "platinum": {"maxTime": 200, "maxDeaths": 0}}
d["startPowers"] = []

# The toxic abyss under everything.
L.liquid(0, W, ACID, H, "acid")
L.emitter(0, ACID - 8, W, ACID, "abyss_motes", 6)


def _island_bottom(x0, y0, x1):
    # Floating ruins: 7-11 tiles thick, never deeper than the lake.
    return min(y0 + 7 + int(random.random() * 5), ACID + 3)


L.island_bottom = _island_bottom


def flora(x0, x1, y, density=0.35, layer="bg_decor", tall=True):
    """Scatter bioluminescent vegetation on a ground strip (tiles)."""
    x = x0 + 1
    while x < x1 - 1:
        r = random.random()
        if r < density * 0.35 and tall:
            L.decor(random.choice(["mushroom_tall", "crystal_big", "ruin_pillar_broken"]), x, y, layer)
            x += 3
        elif r < density:
            L.decor(random.choice(["glow_fern", "mushroom_small", "glow_flowers", "grass_tuft", "mushroom_cluster",
                                   "rock_small", "crystal_cluster"]), x, y, layer,
                    flip=random.random() < 0.5)
            x += 2
        else:
            x += 1


def vines(x0, x1, y, layer="fg_decor", every=5):
    for x in range(x0, x1, every):
        name = random.choice(["hanging_vine_short", "hanging_vine_long", "lantern_pod"])
        L.decor(name, x + random.random(), y, layer, sway=2.0)


# =====================================================================================
# ZONE 1 — Le Seuil des Anciens (introduction, movement tutorial)
# =====================================================================================
L.solid(0, 20, 3, H)                       # left cliff
L.solid(3, 58, 30, I)                      # start terrace (top y=58)
L.solid(22, 55, 30, 58)                    # step
start = L.start(9, 58)
L.decor("ruin_arch", 15, 58)
L.decor("statue_head", 5, 58)
L.decor("ruin_pillar", 21, 58)
L.decor("mushroom_tall", 26, 55)
flora(3, 22, 58)
L.light(15, 53, "#39f0d8", 70)
L.sign(12, 58, "Les ruines des Anciens... Courez et sautez pour avancer.")
L.emitter(3, 44, 30, 57, "fireflies", 1.2)
# hidden alcove above the start: wall-jump between the cliff and the pillar
L.solid(6, 40, 8, 55)                      # floating pillar forming a wall-jump shaft with the cliff
L.shards_line(4.5, 52, 4.5, 43, 4)
L.secret(5, 34, 9, 40, "Cache des explorateurs découverte !")
L.orb("energy_orb", 7, 38)
L.decor("hanging_vine_long", 4, 40, "fg_decor", sway=1.5)
L.decor("hanging_vine_long", 6.5, 40, "fg_decor", sway=1.5)

# first gap over the abyss
L.shards_arc(30, 36, 54, 4, 4)
L.solid(36, 57, 52, I)
L.sign(40, 57, "Sautez puis appuyez à nouveau sur Saut en l'air : double saut !")
flora(36, 50, 57)
L.decor("ruin_pillar", 49, 57)
L.solid(52, 52, 58, I, "ancient_metal")    # wall: double jump required
L.decor("tech_panel", 55, 52)
L.light(55, 50, "#ffb347", 60)
L.shards_line(50.5, 55, 50.5, 49, 3)
# descending floating platforms
L.oneway(61, 66, 54)
L.shards_arc(58, 68, 52, 2, 4)
L.solid(69, 56, 76, 61, "root")
L.decor("glow_fern", 72, 56)
L.oneway(79, 84, 53, "tech_ledge")
L.sign(82, 53, "Bas + Saut pour descendre d'une plateforme traversable.", auto=True)
L.solid(87, 55, 124, I)
flora(87, 118, 55)
L.checkpoint(96, 55, "Seuil des Anciens")
L.light(96, 50, "#39f0d8", 90)
L.enemy("glowmite", 110, 55, patrol_tiles=5)
vines(60, 90, 30)

# =====================================================================================
# ZONE 2 — La Canopée Brisée (exploration, first enemies, Dash)
# =====================================================================================
L.sign(116, 55, "Grimpez aux lianes (Haut) ou rebondissez entre les murs avec le saut mural.")
L.solid(119, 42, 121, 55, "root")           # pillar forming a wall-jump shaft
L.solid(124, 40, 134, I)                   # cliff, top y=40
L.ladder(122, 41, 55)                      # climbable vines on the cliff face
L.decor("roots", 129, 40)
L.shards_line(122.5, 52, 122.5, 44, 4)
flora(124, 134, 40, 0.5)
# crumbling bridge over the abyss
L.crumble(136, 42, 3)
L.crumble(141, 44, 3)
L.shards_arc(134, 146, 41, 2, 5)
L.solid(146, 45, 157, 51, "root")
L.decor("mushroom_tall", 152, 45)
L.enemy("glowmite", 151, 45, patrol_tiles=3)
L.oneway(159, 164, 47, "root_branch")
L.solid(166, 48, 186, I)
flora(166, 186, 48)
L.enemy("glowmite", 175, 48, patrol_tiles=5)
L.enemy("sporeflower", 184, 48, face_left=True)
L.bounce(169, 48, power=560)
L.sign(167, 48, "Ces champignons rebondissent... que cache la canopée ?", auto=True)
L.shards_line(169, 44, 169, 38, 3)
# canopy (secret, relic 1)
L.solid(172, 36, 186, 38, "root")
L.relic(181, 34, "Sceau de la Canopée")
L.shards_line(174, 34, 178, 34, 4)
L.secret(171, 30, 187, 36, "La canopée secrète !")
for x in range(172, 186, 3):
    L.decor(random.choice(["hanging_vine_short", "hanging_vine_long"]), x + 0.5, 38, "fg_decor", sway=2.5)
L.decor("glow_flowers", 184, 36)
L.light(181, 33, "#ffd98a", 60)
# dash dais
L.solid(186, 50, 216, I)
L.decor("statue_head", 189, 50)
L.decor("ruin_arch", 196, 50)
L.power(196, 46, "dash", "Traversez les gouffres et brisez les cristaux en fonçant (invulnérable pendant le Dash).")
L.light(196, 44, "#b04cff", 110)
flora(200, 214, 50, 0.3)
L.solid(200, 30, 216, 42, "crystal")       # corridor ceiling
L.gate(206, 42, 2, 8, "dash")              # crystal wall
L.sign(203, 50, "Un mur de cristal... Foncez dedans avec le Dash !")
L.sign(214, 50, "Gouffre ! Sautez, double sautez, puis Dash en plein vol.")
# dash-required gap 216..230
L.shards_line(218, 46, 228, 46, 5)
L.solid(230, 50, 270, I)
L.checkpoint(240, 50, "Canopée brisée")
L.light(240, 45, "#39f0d8", 90)
flora(230, 262, 50)
vines(140, 230, 28)
L.emitter(140, 30, 230, 48, "falling_leaves", 2)

# =====================================================================================
# ZONE 3 — Les Jardins Gardés (combat, Wave, Shield, arena)
# =====================================================================================
L.power(246, 46, "wave", "Projetez l'onde sur les récepteurs d'énergie et sur vos ennemis.")
L.light(246, 44, "#39f0d8", 100)
L.solid(258, 30, 278, 42, "ancient_metal")  # ceiling over the door corridor
door1 = L.door(270, 42, 8)
L.receptor(264, 50, door1)
L.sign(259, 50, "Une porte scellée. Touchez le récepteur avec l'Onde énergétique.")
L.decor("tech_panel", 262, 46, "bg_decor")
L.solid(270, 50, 302, I)
flora(278, 300, 50)
L.enemy("sentinel", 288, 44, patrol_tiles=4)
L.enemy("glowmite", 296, 50, patrol_tiles=3)
L.orb("energy_orb", 284, 47)
# moving platforms + laser over the abyss
L.solid(306, 24, 334, 28, "ancient_metal")
L.laser(320, 28, 22, rotation=90, on=1.4, off=1.6, warn=0.6)
L.moving(304, 48, 3, 9, 0, speed=45, wait=0.5)
L.moving(319, 46, 3, 0, -4, speed=35, wait=0.8)
L.moving(326, 44, 3, 9, 0, speed=50, wait=0.5, phase=0.5)
L.shards_line(306, 45, 336, 42, 7)
L.solid(340, 48, 368, I)
flora(340, 366, 48)
L.enemy("stalker", 352, 48, patrol_tiles=6, face_left=True)
L.enemy("sporeflower", 362, 48, face_left=True)
L.decor("ruin_pillar", 344, 48)
L.power(358, 43, "shield", "Le bouclier absorbe les coups, renvoie les projectiles et traverse les rideaux de plasma.")
L.checkpoint(366, 48, "Jardins gardés")
L.light(366, 43, "#39f0d8", 90)
# combat arena
L.solid(368, 30, 416, 34, "ancient_metal")  # arena ceiling
L.solid(368, 50, 416, H)
left_door = L.door(371, 42, 8, open_=True)
right_door = L.door(413, 42, 8)
L.solid(368, 34, 371, 42, "ancient_metal")
L.solid(413, 34, 416, 42, "ancient_metal")
L.oneway(379, 386, 44, "tech_ledge")
L.oneway(398, 405, 44, "tech_ledge")
w1 = [L.enemy("glowmite", 382, 50, 4, dormant=True), L.enemy("glowmite", 400, 50, 4, True, dormant=True),
      L.enemy("stalker", 406, 50, 6, True, dormant=True)]
w2 = [L.enemy("sentinel", 384, 40, 4, dormant=True), L.enemy("sentinel", 402, 40, 4, True, dormant=True),
      L.enemy("golem", 396, 50, 3, True, dormant=True)]
L.combat(372, 34, 412, 50, doors=left_door + "," + right_door, waves=",".join(w1) + ";" + ",".join(w2))
L.decor("banner_torn", 375, 34, "bg_decor", rotation=180)
L.decor("banner_torn", 409, 34, "bg_decor", rotation=180)
L.light(392, 38, "#ff4d6d", 120, flicker=0.25)
L.orb("health_orb", 382, 42)
vines(250, 370, 26)

# =====================================================================================
# ZONE 4 — Les Ponts Suspendus (advanced platforming, Impulse, Teleport, secrets)
# =====================================================================================
L.solid(416, 50, 430, I)
L.checkpoint(420, 50, "Ponts suspendus")
L.power(426, 46, "impulse", "Une poussée verticale avec onde de choc. Brise la roche fissurée et garde des golems.")
L.light(426, 44, "#ffb347", 100)
L.sign(428, 50, "Saut + double saut + Impulsion pour gravir la falaise.")
L.solid(430, 40, 440, I, "ancient_metal")  # cliff (160 px)
L.decor("pipe_broken", 435, 40)
# cracked rock covering a shortcut pit with shards
L.gate(440, 40, 4, 1, "impulse")
L.solid(440, 44, 444, I)
L.shards_line(441, 42.5, 443, 42.5, 3)
L.solid(444, 40, 446, I)
# crumbling + lift
L.crumble(448, 40, 3)
L.crumble(453, 41, 3)
L.moving(458, 42, 3, 0, -6, speed=40, wait=0.5)
L.shards_line(449, 37, 460, 37, 5)
L.solid(463, 36, 478, 42, "ancient_metal")
L.solid(467, 27, 472, 35, "ancient_metal")   # low passage: slide (crawl) underneath
L.laser(465, 27, 9, rotation=90, on=1.2, off=1.4, warn=0.5)
L.solid(463, 22, 467, 27, "ancient_metal")   # laser housing
L.sign(463, 36, "Passage étroit : glissez (Bas en pleine course) pour vous faufiler.")
L.power(475, 33, "teleport", "Traversez les barrières de phase en vous téléportant sur une courte distance.")
L.light(475, 31, "#b04cff", 100)
# phase barrier on the main path (crystal corridor)
L.solid(478, 36, 500, 42, "crystal")
L.solid(478, 22, 506, 26, "crystal")       # corridor roof (walkable on top: upper route)
L.gate(488, 26, 1, 10, "teleport")
L.sign(484, 36, "Barrière de phase : visez et téléportez-vous au travers.")
L.enemy("wisp", 494, 31, patrol_tiles=3)
# upper alternative route: bounce + double jump + impulse onto the roof
L.bounce(476, 36, power=520)
L.shards_line(476, 31, 476, 23, 4)
L.shards_line(482, 20, 504, 20, 6)
# phase vault (secret, relic 2), entered by teleporting from the drop shaft
L.solid(506, 22, 516, 29, "crystal")
L.gate(506, 29, 2, 7, "teleport")
L.solid(506, 36, 516, 46, "crystal")
L.solid(514, 29, 516, 36, "crystal")
L.relic(511, 33, "Prisme de phase")
L.secret(508, 29, 514, 36, "Le coffre de phase !")
L.light(511, 32, "#e08aff", 70)
L.oneway(518, 523, 27, "tech_ledge")
# spiky floor below
L.solid(500, 46, 540, I)
L.spikes(522, 526, 46)
L.spikes(530, 534, 46)
L.oneway(521, 527, 42)
L.oneway(529, 535, 42)
L.enemy("sentinel", 528, 36, patrol_tiles=5)
L.enemy("glowmite", 538, 46, patrol_tiles=1)
L.solid(540, 44, 552, I)
L.checkpoint(546, 44, "Sanctuaire")
L.light(546, 39, "#39f0d8", 90)
L.orb("health_orb", 543, 42)
flora(540, 551, 44, 0.3)
vines(430, 540, 22)
L.emitter(440, 20, 540, 44, "spores", 2)

# =====================================================================================
# ZONE 5 — Le Sanctuaire du Gardien (mini-boss)
# =====================================================================================
L.solid(552, 48, 604, H, "ancient_metal")
L.solid(548, 18, 608, 24, "ancient_metal")      # arena ceiling
L.solid(552, 24, 555, 40, "ancient_metal")
L.solid(601, 24, 604, 40, "ancient_metal")
L.solid(550, 44, 552, 48)
boss_l = L.door(553, 40, 8, open_=True)
boss_r = L.door(602, 40, 8)
L.oneway(560, 567, 40, "tech_ledge")
L.oneway(588, 595, 40, "tech_ledge")
L.oneway(573, 582, 33, "tech_ledge")
boss = L.enemy("thornwarden", 586, 48, patrol_tiles=12, face_left=True, dormant=True, boss=True)
L.camzone(552, 18, 604, 52, "clamp", 0.92)
L.combat(557, 24, 601, 48, doors=boss_l + "," + boss_r, waves=boss, lock=False, music="music/sylvanox_boss.ogg")
d["objects"][-1]["props"]["announce"] = False
L.decor("ruin_arch", 578, 48, "bg_decor", scale=1.5)
L.decor("statue_head", 562, 48)
L.decor("statue_head", 594, 48, flip=True)
L.light(578, 30, "#39f0d8", 160, flicker=0.05)
L.orb("health_orb", 563, 38)
L.orb("health_orb", 591, 38)
L.event(boss, "bossDefeated", [
    {"type": "hint", "params": {"text": "Le passage vers le relais est ouvert.", "duration": 4}},
], delay=1.5, name="Gardien vaincu")
L.event(boss, "bossPhase", [{"type": "shake", "params": {"amount": 0.4}}], once=False)

# =====================================================================================
# ZONE 6 — L'Ascension finale (conclusion)
# =====================================================================================
L.solid(604, 48, 614, I)
L.checkpoint(608, 48, "Dernière ascension")
L.light(608, 43, "#39f0d8", 90)
# route A: crumbling stairs and a lift
L.crumble(616, 45, 3, delay=0.35)
L.crumble(621, 42, 3, delay=0.35)
L.crumble(626, 39, 3, delay=0.35)
L.moving(631, 38, 3, 0, -8, speed=50, wait=0.5)
L.shards_line(617, 42, 632, 35, 6)
# route B: bounce pad, then high ledges
L.bounce(611, 48, power=520)
L.oneway(614, 620, 33, "root_branch")
L.oneway(624, 630, 31, "root_branch")
L.shards_line(615, 31, 629, 29, 5)
# relic 3 behind a plasma curtain under the bridge
L.solid(614, 54, 624, 60)
L.solid(618, 47, 624, 49, "ancient_metal")
L.solid(623, 49, 624, 54, "ancient_metal")
L.gate(618, 49, 1, 5, "shield")
L.relic(621, 52, "Cœur du relais")
L.secret(619, 49, 623, 54, "Une alcôve protégée par le plasma !")
L.light(621, 52, "#ff8aa0", 50)
# final tower and exit
L.solid(636, 30, W, H, "ancient_metal")
L.decor("ruin_arch", 648, 30, "bg_decor", scale=1.25)
exit_id = L.exit(648, 30)
L.light(648, 26, "#ffd98a", 140)
L.decor("holo_glyph", 641, 30)
L.decor("holo_glyph", 655, 30)
L.emitter(636, 18, W, 30, "fireflies", 2)
vines(604, 660, 20)

# Hanging roots and vines under the floating islands
for o in list(d["objects"]):
    if o["type"] == "solid" and o["layer"] == "terrain":
        x, y, w, h = o["rect"]
        bottom = (y + h) / 16
        if bottom < ACID - 1 and w >= 64:
            for k in range(int(w / 16) // 4):
                tx = x / 16 + 1 + k * 4 + random.random() * 2
                L.decor(random.choice(["hanging_vine_short", "hanging_vine_long", "roots"]) if random.random() < 0.8
                        else "lantern_pod", tx, bottom if random.random() < 0.85 else bottom, "bg_decor", sway=1.5)

# =====================================================================================
# Background decor and lights along the level
# =====================================================================================
for x in range(20, W, 37):
    L.decor("floating_rock", x + random.random() * 10, 20 + random.random() * 14, "bg_decor",
            alpha=0.8, tint="#c0c8ff")

# =====================================================================================
# Events and cinematics
# =====================================================================================
L.event("level", "levelStart", [{"type": "hint", "params": {
    "text": "Atteignez le relais au sommet des ruines.", "duration": 4}}])
L.event("dash", "unlocked", [{"type": "hint", "params": {"text": "Le Dash fonctionne aussi en diagonale.", "duration": 4}}],
        delay=5)
L.event("level", "time", [{"type": "hint", "params": {"text": "Plus que 5 minutes !", "duration": 3}}],
        params={"seconds": 600})

d["intro"] = {"skippable": True, "letterbox": True, "steps": [
    {"type": "fade", "duration": 0.01, "params": {"to": 1}},
    {"type": "cut", "duration": 0.01, "params": {"x": 2000, "y": 640}},
    {"type": "zoom", "duration": 0.01, "params": {"zoom": 0.8}},
    {"type": "fade", "duration": 1.6, "wait": False, "params": {"to": 0}},
    {"type": "text", "duration": 4.0, "wait": False, "params": {"text": "SYLVANOX", "subtitle": "Les Ruines Suspendues"}},
    {"type": "camera", "duration": 6.0, "params": {"x": 3300, "y": 620, "ease": "inOutSine"}},
    {"type": "text", "duration": 3.5, "wait": False, "params": {
        "text": "Les Anciens ont disparu... mais leurs gardiens veillent toujours.", "style": "caption"}},
    {"type": "camera", "duration": 3.0, "params": {"x": 3300, "y": 1150, "ease": "inOutCubic"}},
    {"type": "wait", "duration": 0.6},
    {"type": "zoom", "duration": 3.0, "wait": False, "params": {"zoom": 1.0}},
    {"type": "camera", "duration": 3.0, "params": {"start": True, "ease": "inOutCubic"}},
    {"type": "spawnPlayer", "duration": 0.9},
    {"type": "dialogue", "duration": 3.4, "params": {"speaker": "Nova",
                                                    "text": "Le signal du relais vient d'au-delà des ruines. En route !"}},
]}
d["outro"] = {"skippable": True, "letterbox": True, "steps": [
    {"type": "zoom", "duration": 1.4, "wait": False, "params": {"zoom": 1.4}},
    {"type": "camera", "duration": 1.4, "params": {"player": True}},
    {"type": "text", "duration": 3.0, "wait": False, "params": {"text": "RELAIS ACTIVÉ", "subtitle": "Sylvanox — Les Ruines Suspendues"}},
    {"type": "playerAnim", "duration": 2.2, "params": {"anim": "victory"}},
    {"type": "flash", "duration": 0.6, "params": {"color": "#e6fffb"}},
    {"type": "fade", "duration": 0.9, "params": {"to": 1}},
]}
d["metadata"] = {"generator": "tools/levelgen/sylvanox_01.py"}

L.save(os.path.join(ROOT, "data/levels/sylvanox_01.json"))
