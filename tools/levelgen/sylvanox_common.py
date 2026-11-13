"""Shared decoration and layout helpers for the Sylvanox levels (02 and 03).

Everything here only emits regular level objects through a levelkit.Level; randomness comes
from the global `random` module, which each level script seeds for deterministic output.
"""
import random

T = 16

FLORA_SMALL = ["glow_fern", "mushroom_small", "glow_flowers", "grass_tuft", "mushroom_cluster", "rock_small",
               "crystal_cluster"]
FLORA_TALL = ["mushroom_tall", "crystal_big", "ruin_pillar_broken"]
SPORE_SMALL = ["mushroom_small", "mushroom_cluster", "glow_flowers", "glow_fern", "mushroom_cluster", "grass_tuft"]
SPORE_TALL = ["mushroom_tall", "mushroom_tall", "crystal_big"]
MACHINE_SMALL = ["rock_small", "crystal_cluster", "rock_big", "bones_ancient", "crystal_cluster", "rock_small"]
MACHINE_TALL = ["ruin_pillar", "ruin_pillar_broken", "crystal_big", "tech_panel"]


def flora(L, x0, x1, y, density=0.35, layer="bg_decor", tall=True, small=None, tall_kinds=None):
    """Scatters vegetation on a ground strip (tiles)."""
    small = small or FLORA_SMALL
    tall_kinds = tall_kinds or FLORA_TALL
    x = x0 + 1
    while x < x1 - 1:
        r = random.random()
        if r < density * 0.35 and tall:
            L.decor(random.choice(tall_kinds), x, y, layer, flip=random.random() < 0.5)
            x += 3
        elif r < density:
            L.decor(random.choice(small), x, y, layer, flip=random.random() < 0.5)
            x += 2
        else:
            x += 1


def vines(L, x0, x1, y, layer="fg_decor", every=5, kinds=None):
    kinds = kinds or ["hanging_vine_short", "hanging_vine_long", "lantern_pod"]
    for x in range(int(x0), int(x1), every):
        L.decor(random.choice(kinds), x + random.random(), y, layer, sway=2.0)


def giant_mushroom(L, x, y_ground, scale=2.0, layer="bg_decor", tint="#ffffff", flip=False):
    """A huge bioluminescent mushroom (decor only) with a soft light under its cap."""
    L.decor("mushroom_tall", x, y_ground, layer, scale=scale, tint=tint, flip=flip)
    L.light(x, y_ground - 2.3 * scale, "#39f0d8", int(34 * scale), flicker=0.08)


def spore_cloud(L, x0, y0, x1, y1, factor=0.5, rate=None):
    """Violet spore cloud: slows the player (slow_zone), dense spore particles and violet glow."""
    oid = L.obj("slow_zone", [x0 * T, y0 * T, (x1 - x0) * T, (y1 - y0) * T], "gameplay",
                {"factor": factor, "visible": True})
    area = (x1 - x0) * (y1 - y0)
    # two purely visual haze layers (factor 1 = no effect) inset inside the cloud: the additive
    # violet fills stack into a soft, denser core so the hazard reads at a glance
    for inset in (0.6, 1.4):
        if x1 - x0 > 2 * inset + 1 and y1 - y0 > 2 * inset + 1:
            L.obj("slow_zone", [(x0 + inset) * T, (y0 + inset) * T, (x1 - x0 - 2 * inset) * T,
                                (y1 - y0 - 2 * inset) * T], "gameplay", {"factor": 1.0, "visible": True})
    L.emitter(x0, y0, x1, y1, "spores", rate or max(8.0, area * 0.6))
    L.emitter(x0, y1 - 1, x1, y1, "bounce_spores", max(0.6, (x1 - x0) * 0.12))
    # a few glowing puffs so the cloud reads as a volume, not just as particles
    n = max(1, int(round(area / 30.0)))
    for i in range(n):
        lx = x0 + (x1 - x0) * (i + 0.5) / n
        ly = y0 + (y1 - y0) * (0.35 + 0.3 * ((i * 7) % 3) / 2.0)
        L.light(lx, ly, "#b04cff", int(min(90, 26 + max(x1 - x0, y1 - y0) * 5)), flicker=0.2)
    return oid


def hanging_under(L, max_bottom, kinds=None, every=4, skip=None, layer="bg_decor"):
    """Hanging roots / vines under every floating terrain block narrower than the level."""
    kinds = kinds or ["hanging_vine_short", "hanging_vine_long", "roots"]
    for o in list(L.data["objects"]):
        if o["type"] != "solid" or o["layer"] != "terrain":
            continue
        if not o.get("props", {}).get("collision", True):
            continue
        x, y, w, h = o["rect"]
        bottom = (y + h) / T
        if bottom >= max_bottom or w < 64:
            continue
        if skip and skip(x / T, bottom):
            continue
        for k in range(int(w / T) // every):
            tx = x / T + 1 + k * every + random.random() * 2
            if tx > (x + w) / T - 1:
                break
            name = random.choice(kinds) if random.random() < 0.82 else "lantern_pod"
            L.decor(name, tx, bottom, layer, sway=1.5)


def bg_rocks(L, x0, x1, y0, y1, every=37, tint="#c0c8ff", alpha=0.8):
    x = x0
    while x < x1:
        L.decor("floating_rock", x + random.random() * 10, y0 + random.random() * (y1 - y0), "bg_decor",
                alpha=alpha, tint=tint)
        x += every


def lamp(L, x, y, color="#39f0d8", radius=80, flicker=0.1):
    return L.light(x, y, color, radius, flicker)


def slow_zone(L, x0, y0, x1, y1, factor=0.5, visible=True):
    return L.obj("slow_zone", [x0 * T, y0 * T, (x1 - x0) * T, (y1 - y0) * T], "gameplay",
                 {"factor": factor, "visible": visible})


def abyss_updraft(L, x0, x1, y_top, y_bottom, strength=230):
    """Updraft rising from the void (no vent): wind streaks and soft lights make the column readable."""
    oid = L.updraft(x0, x1, y_top, y_bottom, strength=strength, vent=False)
    h = y_bottom - y_top
    L.emitter(x0, y_top + h * 0.15, x1, y_bottom, "updraft_wind", max(3.0, h * 0.35))
    y = y_bottom - 4
    while y > y_top + 3:
        L.light((x0 + x1) / 2, y, "#8ffff0", 46, flicker=0.12)
        y -= 11
    return oid


def damage_zone(L, x0, y0, x1, y1, damage=1, interval=0.8, visible=True):
    return L.obj("damage_zone", [x0 * T, y0 * T, (x1 - x0) * T, (y1 - y0) * T], "gameplay",
                 {"damage": damage, "interval": interval, "visible": visible, "shieldProtects": True})


def boss_arena(L, boss_id, left_door, right_door, x0, y0, x1, y1, music, zoom=0.78, cam=None):
    """Boss arena wiring in the reference pattern (sylvanox_01 zone 5)."""
    if cam:
        L.camzone(*cam, "clamp", zoom)
    zid = L.combat(x0, y0, x1, y1, doors=left_door + "," + right_door, waves=boss_id, lock=False, music=music)
    L.data["objects"][-1]["props"]["announce"] = False
    return zid
