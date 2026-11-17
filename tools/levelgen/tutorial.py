"""Builds data/tutorial/premier_envol.json — « Le premier envol de Nova » (2.64), the tutorial of the
first launch, in « la Pépinière d'étoiles », a small soft world outside the campaign (no planet
of the map lists it: it is played once, from the welcome of the very first launch).

Seven zones, about six minutes with Bip's explanation cards (event action « tutorial », each
card stops the run until the player has read it):
  1. the awakening: walking;
  2. round grass: jumping, a long press goes higher (a step, a low wall), the double jump;
  3. bouncing mushrooms (bounce_pad) and shards to catch in the air;
  4. a garden of sleeping creatures that never strike back (gloop_dormeur): attacking; the
     garden's gate opens once they are gone;
  5. a tablet gives the Ruée (the Dash), a crystal wall to break; carrying bubbles (bubble) take
     Nova over a stream;
  6. a lantern (checkpoint): back here at once after a fall of all her health, nothing lost;
  7. the easiest boss of the game by the bosses' rating (mycelia), slowed for the tutorial:
     « mycelia_tendre », a data variant with two slow, long-announced attacks.
No deadly fall (LevelData::tutorial): a fall brings Nova back on the last solid ground at once.
Not part of the campaign: no seals, no power stations nor rating are needed (one short route).

Run from the repository root:  python3 tools/levelgen/tutorial.py
"""
import os

from levelkit import Level, ROOT, T

W, H = 332, 38
F = 30                 # the floor's top row
WATER = 35             # the stream's surface row (below every floor)
LEVEL_ID = "premier_envol"

L = Level(LEVEL_ID, "Le premier envol de Nova", W, H)
d = L.data
d["description"] = ("La Pépinière d'étoiles : herbes rondes, champignons rebondissants et bulles qui portent Nova. "
                    "Bip, un petit robot, y présente chaque nouveauté avant l'aventure.")
d["difficulty"] = 1
d["planet"] = "sylvanox"
d["tutorial"] = True
d["music"] = "music/sylvanox_level.ogg"
d["bossMusic"] = "music/mycelia_boss.ogg"
d["ambience"] = "music/sylvanox_ambience.ogg"
# Pastel: the Sylvanox planes in pink, lilac and mint, a soft bright light.
d["ambientLight"] = "#e2d6f6"
d["backgroundColor"] = "#2b2a6a"
d["background"] = [
    {"texture": "backgrounds/sylvanox/sky.png", "factor": [0.0, 0.0], "fill": True, "tint": "#ffc8ec"},
    {"texture": "backgrounds/sylvanox/far.png", "factor": [0.1, 0.05], "offset": [0, -20], "tint": "#e4ccff"},
    {"texture": "backgrounds/sylvanox/mid.png", "factor": [0.25, 0.12], "offset": [0, 10], "tint": "#d4c4ff"},
    {"texture": "backgrounds/sylvanox/abyss.png", "factor": [0.4, 0.3], "offset": [0, 210], "fillBelow": "#8fcfb0",
     "tint": "#c8f0e0"},
    {"texture": "backgrounds/sylvanox/near.png", "factor": [0.5, 0.25], "offset": [0, 60], "tint": "#c8f4d4"},
]
# A fall is caught just above the stream (LevelData::tutorial: back on the last solid ground).
d["killY"] = WATER * T - 26
d["timer"] = {"mode": "unlimited", "limit": 3600, "onExpire": "continue", "referenceTime": 360, "challengeMaxDeaths": 0}
d["medals"] = {}
d["startPowers"] = []
d["maxPowers"] = 4

def card(source, trigger, name, delay=0.0, params=None):
    L.event(source, trigger, [{"type": "tutorial", "params": {"card": name}}], delay=delay, params=params)


def shard_cards():
    """Bip's word on the first shard caught: every shard placed so far asks for the card (once)."""
    for o in L.data["objects"]:
        if o["type"] == "shard" and o["id"] not in asked:
            asked.add(o["id"])
            card(o["id"], "collected", "shards", delay=0.2)


asked = set()


def ground(x0, x1, y=F):
    L.solid(x0, y, x1, H, "ruin_stone")


def stream(x0, x1):
    """A stream between two banks: soft water far below the floor (a fall is caught above it)."""
    L.liquid(x0, x1, WATER, H, "water")


def garden(x0, x1, y=F, step=5):
    """Round grass, flowers and small mushrooms along a floor (decor only)."""
    names = ["grass_tuft", "glow_flowers", "mushroom_small", "grass_tuft", "glow_fern", "mushroom_cluster"]
    for i, x in enumerate(range(x0 + 1, x1 - 1, step)):
        L.decor(names[(i + x0) % len(names)], x + 0.5, y, "bg_decor")


def light(x, y, color="#ffd0f0", radius=110):
    L.light(x, y, color, radius, flicker=0.05)


# --------------------------------------------------------------- 1. the awakening (walking)
L.solid(0, 0, 1, H, "ruin_stone", deco=False)
ground(1, 38)
L.start(5, F)
garden(2, 38)
L.decor("lantern_pod", 12, F - 4, "bg_decor")
L.decor("mushroom_tall", 24, F, "bg_decor")
light(8, F - 5)
light(26, F - 5, "#c8f0ff")
L.emitter(1, 18, 60, F, "fireflies", 1.2)
card("level", "levelStart", "move", delay=0.4)

# --------------------------------------------------------------- 2. round grass (jumping)
t_jump = L.trigger(31, 18, 33, F)
card(t_jump, "enter", "jump")
ground(38, 45, F - 1)       # a small step: a short press
ground(45, 52, F - 2)
ground(52, 61, F - 3)
L.solid(61, F - 6, 64, H, "ruin_stone")  # the low wall: a long press (or the double jump)
ground(64, 76, F - 3)
ground(76, 86)               # back down to the floor
garden(52, 61, F - 3, 4)
garden(64, 76, F - 3, 5)
garden(76, 86, F, 5)
light(62, F - 9, "#fff0b0")

# --------------------------------------------------------------- 3. bouncing mushrooms, shards in the air
t_mush = L.trigger(84, 18, 86, F)
card(t_mush, "enter", "mushroom")
ground(86, 134)
L.bounce(94, F)
L.oneway(98, 106, F - 7, "root_branch")
L.shards_line(99, F - 8.5, 105, F - 8.5, 4)
L.bounce(116, F)
L.shards_arc(113, 120, F - 4, 6, 6)        # an arc in the air, caught on the way up and down
L.shards_line(115, F - 11, 117, F - 11, 2)  # high: the bounce, then the double jump
L.shards_line(122, F - 1.2, 127, F - 1.2, 4)  # a trail at her height (no one misses the first)
L.decor("mushroom_tall", 90, F, "bg_decor")
L.decor("mushroom_cluster", 108, F, "bg_decor")
L.decor("mushroom_tall", 126, F, "bg_decor", flip=True)
light(94, F - 3, "#ffb3d9", 90)
light(116, F - 3, "#ffb3d9", 90)
garden(86, 134, F, 7)
shard_cards()  # the first shard caught (the third card)

# --------------------------------------------------------------- 4. the sleeping garden (attacking)
t_att = L.trigger(134, 18, 136, F)
card(t_att, "enter", "attack")
ground(134, 196)
sleepers = [L.enemy("gloop_dormeur", x, F, patrol_tiles=0, face_left=True) for x in (146, 154, 162)]
gate = L.door(171, F - 6, 6, open_=False)
L.solid(171, 0, 172, F - 6, "ruin_stone", deco=False)  # the gate goes up to the sky
garden_zone = L.combat(138, 14, 170, F, doors=gate, waves=",".join(sleepers), lock=False)
L.data["objects"][-1]["props"]["announce"] = False
L.decor("glow_flowers", 142, F, "bg_decor")
L.decor("glow_fern", 150, F, "bg_decor")
L.decor("glow_flowers", 158, F, "bg_decor")
L.decor("glow_fern", 166, F, "bg_decor")
light(154, F - 6, "#c8b8ff", 140)
L.event(garden_zone, "cleared", [{"type": "hint", "params": {"text": "Le jardin s'est rendormi : la porte s'ouvre.",
                                                              "duration": 3}}])

# --------------------------------------------------------------- 5. the Ruée, then the stream of bubbles
L.power(179, F - 1.5, "dash", "La Ruée (le Dash) : file d'un coup, même en l'air.")
card("dash", "unlocked", "power", delay=0.6)
L.solid(186, 0, 187, F - 6, "ruin_stone", deco=False)
L.gate(186, F - 6, 1, 6, "dash")   # a crystal wall: the Ruée breaks it
light(186, F - 4, "#9ad7ff", 90)
t_bub = L.trigger(190, 18, 192, F)
card(t_bub, "enter", "bubbles")
stream(196, 230)
L.emitter(196, WATER - 3, 230, WATER, "fireflies", 1.0)
# Two bubbles: one to a small floating island, the other to the far bank (each ends level with
# its ground: Nova walks off before it pops).
L.obj("bubble", [196 * T, F * T, 40, 16], props={"dx": 16 * T, "dy": -1 * T, "speed": 50, "respawn": 1.2})
L.solid(213, F - 1, 218, F + 1, "ruin_stone")
L.decor("glow_flowers", 215.5, F - 1, "bg_decor")
L.obj("bubble", [217 * T, (F - 1) * T, 40, 16], props={"dx": 12 * T, "dy": 1 * T, "speed": 50, "respawn": 1.2})
L.shards_line(204, F - 3, 210, F - 4, 3)
L.shards_line(221, F - 3.5, 226, F - 3, 3)
light(205, F - 4, "#9ad7ff", 120)
light(223, F - 4, "#9ad7ff", 120)

# --------------------------------------------------------------- 6. the lantern (checkpoint)
ground(230, 246)
cp = L.checkpoint(236, F, "Lanterne")
card(cp, "checkpoint", "lantern", delay=0.5)
L.decor("lantern_pod", 233, F - 4, "bg_decor")
light(236, F - 4, "#ffd27a", 140)
stream(246, 250)            # a small brook: an easy jump (a fall is caught)
ground(250, 264)
garden(230, 264, F, 6)

# --------------------------------------------------------------- 7. the cuddly guardian (the boss)
AX, AW, HEAD = 264, 48, 16
ground(AX, AX + AW + 20)
L.solid(AX, F - HEAD - 4, AX + 1, F - 7, "ruin_stone", deco=False)
L.solid(AX + AW - 1, 0, AX + AW, F - 7, "ruin_stone", deco=False)
left = L.door(AX, F - 7, 7, open_=True)
right = L.door(AX + AW - 1, F - 7, 7, open_=False)
L.oneway(AX + 7, AX + 13, F - 5, "root_branch")   # above the reach of the roots (3 tiles)
L.oneway(AX + 34, AX + 40, F - 5, "root_branch")
boss = L.enemy("mycelia_tendre", AX + AW - 12, F, patrol_tiles=6, face_left=True, dormant=True, boss=True)
L.combat(AX + 2, F - HEAD, AX + AW - 1, F, doors=left + "," + right, waves=boss, lock=False, music="music/mycelia_boss.ogg")
L.data["objects"][-1]["props"]["announce"] = False
card(boss, "bossStart", "boss", delay=0.4)
L.orb("health_orb", AX + 4, F - 1.5)
light(AX + AW // 2, F - HEAD + 3, "#ffc8e8", 170)
L.event(boss, "bossDefeated", [{"type": "hint", "params": {
    "text": "Mycélia s'endort, toute contente : le chemin s'ouvre !", "duration": 4}}], delay=1.2)
garden(AX + AW, AX + AW + 20, F, 5)
L.exit(AX + AW + 12, F)
light(AX + AW + 12, F - 5, "#ffd27a", 120)
L.solid(W - 1, 0, W, H, "ruin_stone", deco=False)

shard_cards()  # the shards of the stream too

# --------------------------------------------------------------- the opening and the end
d["intro"] = {"skippable": True, "letterbox": True, "steps": [
    {"type": "fade", "duration": 0.01, "params": {"to": 1}},
    {"type": "cut", "duration": 0.01, "params": {"x": 5 * T, "y": (F - 5) * T}},
    {"type": "fade", "duration": 1.2, "wait": False, "params": {"to": 0}},
    {"type": "text", "duration": 3.2, "wait": False, "params": {"text": "LE PREMIER ENVOL DE NOVA",
                                                                 "subtitle": "La Pépinière d'étoiles"}},
    {"type": "wait", "duration": 2.0},
    {"type": "spawnPlayer", "duration": 0.9},
]}
d["outro"] = {"skippable": True, "letterbox": True, "steps": [
    {"type": "zoom", "duration": 1.2, "wait": False, "params": {"zoom": 1.4}},
    {"type": "camera", "duration": 1.2, "params": {"player": True}},
    {"type": "text", "duration": 2.6, "wait": False, "params": {"text": "BRAVO, NOVA !", "subtitle": "Le premier envol"}},
    {"type": "playerAnim", "duration": 2.0, "params": {"anim": "victory"}},
    {"type": "fade", "duration": 0.8, "params": {"to": 1}},
]}
d["metadata"] = {"generator": "tools/levelgen/tutorial.py", "revision": 8}

out = os.path.join(ROOT, "data", "tutorial", LEVEL_ID + ".json")
os.makedirs(os.path.dirname(out), exist_ok=True)
L.save(out)
