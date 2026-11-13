"""Shared helpers of the Glacior levels (glacior_01..03).

Decoration helpers silently skip sprites that are not in the decor atlases yet, so the
gameplay layout can be generated before `assets/sprites/decor/glacior_decor.json` exists.
Every helper takes the `Level` and a `random.Random` so generation stays deterministic.
"""
from levelkit import DECOR, T

# Palette (CONTENT_CONTRACT section 1).
AURORA_GREEN = "#5dffb0"
AURORA_VIOLET = "#b48cff"
FROST_CYAN = "#7ff6ff"
ICE_BLUE = "#a8e4ff"
ICE_DEEP = "#5fb0ff"
WARM = "#ffd98a"
DANGER = "#ff6d8a"

SMALL = ["snow_rock_small", "frost_flower", "ice_crystal", "snow_drift", "frost_flower", "snow_rock_small"]
TALL = ["snow_pine", "frozen_tree", "ice_spire", "snow_pine", "snow_rock_big", "ice_crystal_big"]
RUIN_TALL = ["frozen_statue", "frozen_machine", "frost_banner", "ice_arch"]


def has(name):
    return name in DECOR


def deco(L, name, x, y, layer="bg_decor", **kw):
    """Decor sprite at (x, y) tiles (its pivot), skipped while the atlas is missing."""
    if name not in DECOR:
        return None
    return L.decor(name, x, y, layer, **kw)


def flora(L, rng, x0, x1, y, density=0.35, layer="bg_decor", tall=True, ruins=False, avoid=()):
    """Scatter snowy vegetation, rocks and crystals on a ground strip (tiles).

    `avoid` is a list of (xa, xb) ranges kept free (signs, checkpoints, tablets)."""
    x = x0 + 1
    while x < x1 - 1:
        if any(a - 1 <= x <= b + 1 for a, b in avoid):
            x += 1
            continue
        r = rng.random()
        if r < density * 0.32 and tall:
            pool = TALL + (RUIN_TALL if ruins else [])
            deco(L, rng.choice(pool), x + rng.random() * 0.6, y, layer, flip=rng.random() < 0.5)
            x += 3 + rng.random() * 2
        elif r < density:
            deco(L, rng.choice(SMALL), x + rng.random() * 0.6, y, layer, flip=rng.random() < 0.5)
            x += 2
        else:
            x += 1


def icicles(L, rng, x0, x1, y_bottom, every=3.0, layer="bg_decor", long_ratio=0.35):
    """Icicles hanging under a ceiling / platform whose bottom edge is at row `y_bottom`."""
    x = x0 + 0.6 + rng.random()
    while x < x1 - 0.6:
        deco(L, "icicles_long" if rng.random() < long_ratio else "icicles", x, y_bottom, layer,
             flip=rng.random() < 0.5)
        x += every * (0.7 + rng.random() * 0.6)


def hang_icicles_under_solids(L, rng, max_bottom, min_width=4, skip=()):
    """Icicles under every terrain solid whose bottom is above `max_bottom` (floating ice)."""
    for o in list(L.data["objects"]):
        if o["type"] != "solid" or o["layer"] != "terrain":
            continue
        x, y, w, h = o["rect"]
        bottom = (y + h) / T
        if bottom >= max_bottom or w < min_width * T:
            continue
        if any(a <= x / T <= b for a, b in skip):
            continue
        icicles(L, rng, x / T, (x + w) / T, bottom, every=3.2)


def lamp(L, x, y_ground, color=AURORA_GREEN, radius=90, layer="bg_decor"):
    """Aurora lamp decor + matching light."""
    deco(L, "aurora_lamp", x, y_ground, layer)
    L.light(x, y_ground - 2.2, color, radius, flicker=0.12)


def crystal_light(L, x, y_ground, big=False, color=FROST_CYAN, radius=60):
    deco(L, "ice_crystal_big" if big else "ice_crystal", x, y_ground)
    L.light(x, y_ground - (1.6 if big else 0.8), color, radius, flicker=0.08)


def floating_ice(L, rng, x0, x1, y0, y1, step=34, alpha=0.75):
    """Distant floating ice shards drifting in the sky (pure background)."""
    x = x0 + rng.random() * 8
    while x < x1:
        deco(L, "ice_floating", x, y0 + rng.random() * (y1 - y0), "bg_decor", alpha=alpha,
             tint="#c8dcff", scale=0.75 + rng.random() * 0.5, flip=rng.random() < 0.5)
        x += step * (0.7 + rng.random() * 0.6)


def freezing_water(L, x0, x1, y0, y1, interval=1.4, mist=True):
    """Icy water: swimmable, but the cold bites (1 damage every `interval` s, shield protects)."""
    wid = L.liquid(x0, x1, y0, y1, "water")
    L.obj("damage_zone", [x0 * T, (y0 + 0.4) * T, (x1 - x0) * T, (y1 - y0 - 0.4) * T], "gameplay",
          {"damage": 1, "interval": interval, "visible": False, "shieldProtects": True})
    if mist:
        L.emitter(x0, y0 - 3, x1, y0, "frost_mist", max(1.0, (x1 - x0) / 10.0))
    return wid


def ice_spikes_wall(L, x, y0, y1, facing_left=True):
    """Icicle spikes covering a vertical cliff face (rows y0..y1). The face is at column x:
    facing_left = spikes on the left side of a wall starting at x (tips point left)."""
    if facing_left:
        return L.obj("spikes", [(x - 1) * T, y0 * T, T, (y1 - y0) * T], "gameplay", {"damage": 1}, rotation=270)
    return L.obj("spikes", [x * T, y0 * T, T, (y1 - y0) * T], "gameplay", {"damage": 1}, rotation=90)


def ceiling_spikes(L, x0, x1, y_ceiling, damage=1):
    """Icicle spikes hanging under a ceiling whose bottom edge is row `y_ceiling` (tips down)."""
    return L.obj("spikes", [x0 * T, y_ceiling * T, (x1 - x0) * T, T], "gameplay", {"damage": damage}, rotation=180)


def boss_arena(L, x0, x1, floor_y, ceil_y, boss_kind, boss_x, boss_y, platforms, music, materials,
               door_h=8, face_left=True, cam_zoom=0.92, ceiling_material="frost_metal"):
    """Boss arena in the sylvanox_01 zone-5 pattern. Returns (boss_id, left_door, right_door).

    x0..x1: outer walls (3 tiles thick each); floor top at `floor_y`; ceiling bottom at `ceil_y`.
    `materials` = list of (xa, xb, material) floor strips; `platforms` = (xa, xb, y, style)."""
    for xa, xb, mat in materials:
        L.solid(xa, floor_y, xb, L.data["bounds"][3] // T, mat)
    L.solid(x0 - 4, ceil_y - 6, x1 + 4, ceil_y, ceiling_material)
    L.solid(x0, ceil_y, x0 + 3, floor_y - door_h, ceiling_material)
    L.solid(x1 - 3, ceil_y, x1, floor_y - door_h, ceiling_material)
    left = L.door(x0 + 1, floor_y - door_h, door_h, open_=True)
    right = L.door(x1 - 2, floor_y - door_h, door_h)
    for xa, xb, y, style in platforms:
        L.oneway(xa, xb, y, style)
    boss = L.enemy(boss_kind, boss_x, boss_y, patrol_tiles=12, face_left=face_left, dormant=True, boss=True)
    L.camzone(x0, ceil_y - 4, x1, floor_y + 4, "clamp", cam_zoom)
    L.combat(x0 + 5, ceil_y, x1 - 5, floor_y, doors=left + "," + right, waves=boss, lock=False, music=music)
    L.data["objects"][-1]["props"]["announce"] = False
    L.event(boss, "bossPhase", [{"type": "shake", "params": {"amount": 0.4}}], once=False)
    return boss, left, right
