"""Small DSL used to author Cosmic Platformer levels from Python.

Coordinates are in tiles (16 px) unless noted; y grows downwards. The output is the
regular level JSON format, fully editable afterwards in the in-game level editor.
"""
import json
import os

T = 16
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

# Decor sprites of every planet atlas (names are unique across atlases).
DECOR = {}
for _atlas in ("sylvanox", "pyralis", "glacior"):
    _p = os.path.join(ROOT, "assets/sprites/decor/%s_decor.json" % _atlas)
    if os.path.exists(_p):
        with open(_p) as f:
            for _k, _v in json.load(f)["sprites"].items():
                DECOR.setdefault(_k, _v)


def enemy_size(kind):
    """Collision box of an enemy definition (data/enemies/<kind>.json)."""
    with open(os.path.join(ROOT, "data/enemies/%s.json" % kind), encoding="utf-8") as f:
        d = json.load(f)
    return d["size"][0] * 1.0, d["size"][1] * 1.0


# Per-planet presentation: music, light, parallax stack (see docs/ASSET_SPEC.md section 8).
PLANETS = {
    "sylvanox": {
        "music": "music/sylvanox_level.ogg", "bossMusic": "music/sylvanox_boss.ogg",
        "ambience": "music/sylvanox_ambience.ogg", "ambientLight": "#7480a8", "backgroundColor": "#0b0f1e",
        "background": [
            {"texture": "backgrounds/sylvanox/sky.png", "factor": [0.0, 0.0], "fill": True},
            {"texture": "backgrounds/sylvanox/far.png", "factor": [0.1, 0.05], "offset": [0, -20]},
            {"texture": "backgrounds/sylvanox/mid.png", "factor": [0.25, 0.12], "offset": [0, 10]},
            {"texture": "backgrounds/sylvanox/abyss.png", "factor": [0.4, 0.3], "offset": [0, 210], "fillBelow": "#2f7a1a"},
            {"texture": "backgrounds/sylvanox/near.png", "factor": [0.5, 0.25], "offset": [0, 60]},
            {"texture": "backgrounds/sylvanox/fg_vines.png", "factor": [1.15, 1.0], "offset": [0, -330],
             "foreground": True, "tint": "#ffffffc0"},
        ],
    },
    "pyralis": {
        "music": "music/pyralis_level.ogg", "bossMusic": "music/pyralis_boss.ogg",
        "ambience": "music/pyralis_ambience.ogg", "ambientLight": "#9a6a5a", "backgroundColor": "#14090b",
        "background": [
            {"texture": "backgrounds/pyralis/sky.png", "factor": [0.0, 0.0], "fill": True},
            {"texture": "backgrounds/pyralis/far.png", "factor": [0.1, 0.05], "offset": [0, -20]},
            {"texture": "backgrounds/pyralis/mid.png", "factor": [0.25, 0.12], "offset": [0, 10]},
            {"texture": "backgrounds/pyralis/abyss.png", "factor": [0.4, 0.3], "offset": [0, 210], "fillBelow": "#ff8a3d"},
            {"texture": "backgrounds/pyralis/near.png", "factor": [0.5, 0.25], "offset": [0, 60]},
            {"texture": "backgrounds/pyralis/fg_embers.png", "factor": [1.15, 1.0], "offset": [0, -330],
             "foreground": True, "tint": "#ffffffc0"},
        ],
    },
    "glacior": {
        "music": "music/glacior_level.ogg", "bossMusic": "music/glacior_boss.ogg",
        "ambience": "music/glacior_ambience.ogg", "ambientLight": "#8ca4c8", "backgroundColor": "#0a1428",
        "background": [
            {"texture": "backgrounds/glacior/sky.png", "factor": [0.0, 0.0], "fill": True},
            {"texture": "backgrounds/glacior/far.png", "factor": [0.1, 0.05], "offset": [0, -20]},
            {"texture": "backgrounds/glacior/mid.png", "factor": [0.25, 0.12], "offset": [0, 10]},
            {"texture": "backgrounds/glacior/abyss.png", "factor": [0.4, 0.3], "offset": [0, 210], "fillBelow": "#c2dbef"},
            {"texture": "backgrounds/glacior/near.png", "factor": [0.5, 0.25], "offset": [0, 60]},
            {"texture": "backgrounds/glacior/fg_icicles.png", "factor": [1.15, 1.0], "offset": [0, -330],
             "foreground": True, "tint": "#ffffffc0"},
        ],
    },
    # Stellar Core (2.7): the star at the centre of the system; its sky is the star itself and
    # the three planets come back as echoes in its light.
    "noyau": {
        "music": "music/sylvanox_depths.ogg", "bossMusic": "music/pyralis_boss.ogg",
        "ambience": "music/pyralis_ambience.ogg", "ambientLight": "#a890c8", "backgroundColor": "#0a0716",
        "background": [
            {"texture": "backgrounds/noyau/sky.png", "factor": [0.0, 0.0], "fill": True},
            {"texture": "backgrounds/noyau/far.png", "factor": [0.1, 0.05], "offset": [0, -20]},
            {"texture": "backgrounds/noyau/mid.png", "factor": [0.25, 0.12], "offset": [0, 10]},
            {"texture": "backgrounds/noyau/abyss.png", "factor": [0.4, 0.3], "offset": [0, 210], "fillBelow": "#ffd98a"},
            {"texture": "backgrounds/noyau/near.png", "factor": [0.5, 0.25], "offset": [0, 60]},
            {"texture": "backgrounds/noyau/fg_motes.png", "factor": [1.15, 1.0], "offset": [0, -330],
             "foreground": True, "tint": "#ffffffc0"},
        ],
    },
}



# Power slots (version 2.3): powers carried at once in every campaign level, and the
# start set of each level (the other powers come from tablets and power stations).
MAX_POWERS = 4
START_POWERS = {
    "sylvanox_01": [],
    "sylvanox_02": ["teleport", "wave", "dash", "impulse"],
    "sylvanox_03": ["wave", "dash", "glide", "teleport"],
    "sylvanox_04": ["dash", "wave", "grapple", "shield"],
    "glacior_01": ["ground_pound", "glide", "grapple", "dash"],
    "glacior_02": ["triple_jump", "grapple", "teleport", "dash"],
    "glacior_03": ["triple_jump", "grapple", "ground_pound", "glide"],
    "glacior_04": ["glide", "triple_jump", "dash", "shield"],
    "pyralis_01": ["glide", "grapple", "dash", "shield"],
    "pyralis_02": ["ground_pound", "wave", "glide", "grapple"],
    "pyralis_03": ["grapple", "shield", "glide", "teleport"],
    "pyralis_04": ["glide", "ground_pound", "charge_shot", "dash"],
    "noyau_01": ["dash", "wave", "shield", "charge_shot"],
    "noyau_02": ["dash", "wave", "shield", "grapple"],
}
POWER_NAMES = {
    "dash": "le Dash", "wave": "l'Onde", "shield": "le Bouclier", "impulse": "l'Impulsion", "teleport": "la Téléportation",
    "glide": "le Planeur", "grapple": "le Grappin", "ground_pound": "la Frappe sismique", "charge_shot": "le Tir chargé",
    "triple_jump": "le Triple saut", "time_slow": "le Ralenti", "magnet": "l'Aimant", "air_dash": "la Ruée aérienne",
    "plasma_bomb": "la Bombe plasma",
}

class Level:
    def __init__(self, level_id, name, width_tiles, height_tiles):
        self.data = {
            "format": "cosmic.level", "formatVersion": 1, "id": level_id, "name": name,
            "description": "", "planet": "sylvanox", "author": "Cosmic Studio", "version": "1.0",
            "difficulty": 2, "music": "", "bossMusic": "", "ambience": "",
            "bounds": [0, 0, width_tiles * T, height_tiles * T],
            "ambientLight": "#6e78a0", "backgroundColor": "#0b0f1e", "background": [],
            "timer": {}, "medals": {}, "startPowers": [], "cameraZoom": 1.0,
            "layers": [
                {"id": "bg_decor", "name": "Décor arrière", "order": 0, "visible": True, "locked": False},
                {"id": "terrain", "name": "Terrain", "order": 10, "visible": True, "locked": False},
                {"id": "gameplay", "name": "Gameplay", "order": 20, "visible": True, "locked": False},
                {"id": "fg_decor", "name": "Décor avant", "order": 30, "visible": True, "locked": False,
                 "foreground": True},
            ],
            "objects": [], "events": [], "intro": {}, "outro": {}, "metadata": {},
        }
        self._next = 1
        self._ev = 1
        self.crumble_style = "ruins"

    def planet(self, planet_id):
        """Applies the planet presentation preset (music, light, parallax background)."""
        import copy
        p = PLANETS[planet_id]
        d = self.data
        d["planet"] = planet_id
        for k in ("music", "bossMusic", "ambience", "ambientLight", "backgroundColor"):
            d[k] = p[k]
        d["background"] = copy.deepcopy(p["background"])
        self.crumble_style = {"pyralis": "basalt", "glacior": "ice"}.get(planet_id, "ruins")

    # ------------------------------------------------------------------ core
    def obj(self, type_, rect, layer="gameplay", props=None, rotation=0, flip=False, z=0, oid=None):
        if oid is None:
            oid = "o%d" % self._next
            self._next += 1
        o = {"id": oid, "type": type_, "layer": layer, "rect": [float(v) for v in rect]}
        if rotation:
            o["rotation"] = rotation
        if flip:
            o["flipX"] = True
        if z:
            o["z"] = z
        if props:
            o["props"] = props
        self.data["objects"].append(o)
        return oid

    def event(self, source, trigger, actions, once=True, delay=0.0, params=None, name=""):
        eid = "ev%d" % self._ev
        self._ev += 1
        e = {"id": eid, "source": source, "trigger": trigger, "once": once, "actions": actions}
        if delay:
            e["delay"] = delay
        if params:
            e["params"] = params
        if name:
            e["name"] = name
        self.data["events"].append(e)
        return eid

    # --------------------------------------------------------------- terrain
    island_bottom = None  # callable(y0) -> bottom tile for floating islands

    def solid(self, x0, y0, x1, y1, material="ruin_stone", deco=True, layer="terrain", collision=True):
        if y1 == "island" and self.island_bottom:
            y1 = self.island_bottom(x0, y0, x1)
        props = {"material": material}
        if not deco:
            props["decorTop"] = False
        if not collision:
            props["collision"] = False
        return self.obj("solid", [x0 * T, y0 * T, (x1 - x0) * T, (y1 - y0) * T], layer, props)

    def oneway(self, x0, x1, y, style="ruin_bridge"):
        return self.obj("oneway", [x0 * T, y * T, (x1 - x0) * T, T], "terrain", {"style": style})

    def moving(self, x, y, w_tiles, dx, dy, speed=60, wait=0.6, phase=0.0, active=True, triggered=False, oneway=False):
        return self.obj("moving_platform", [x * T, y * T, w_tiles * T, T], "gameplay",
                        {"dx": dx * T, "dy": dy * T, "speed": speed, "wait": wait, "phase": phase,
                         "active": active, "triggered": triggered, "oneway": oneway})

    def crumble(self, x, y, w_tiles=3, delay=0.45, respawn=3.0, style=None):
        props = {"delay": delay, "respawn": respawn}
        style = style or self.crumble_style
        if style != "ruins":
            props["style"] = style
        return self.obj("crumble_platform", [x * T, y * T, w_tiles * T, T], "gameplay", props)

    def gate(self, x, y, w_tiles, h_tiles, power, respawn=False):
        return self.obj("power_gate", [x * T, y * T, w_tiles * T, h_tiles * T], "gameplay",
                        {"power": power, "respawn": respawn})

    def bounce(self, x, y_ground, power=430):
        return self.obj("bounce_pad", [x * T - 16, y_ground * T - 16, 32, 16], "gameplay", {"power": power})

    def ladder(self, x, y0, y1, w_tiles=1):
        return self.obj("ladder", [x * T, y0 * T, w_tiles * T, (y1 - y0) * T], "gameplay")

    def spikes(self, x0, x1, y_ground, rotation=0, damage=1):
        return self.obj("spikes", [x0 * T, y_ground * T - T, (x1 - x0) * T, T], "gameplay",
                        {"damage": damage}, rotation=rotation)

    def liquid(self, x0, x1, y0, y1, kind="acid"):
        return self.obj("liquid", [x0 * T, y0 * T, (x1 - x0) * T, (y1 - y0) * T], "gameplay", {"kind": kind})

    def laser(self, x, y, length_tiles, rotation=0, on=1.5, off=1.5, warn=0.6, phase=0.0, active=True):
        # (x, y) = emitter centre in tiles; beam goes towards `rotation`.
        import math
        a = math.radians(rotation)
        L = length_tiles * T
        cx = x * T + math.cos(a) * (L / 2 - 8)
        cy = y * T + math.sin(a) * (L / 2 - 8)
        return self.obj("laser", [cx - L / 2, cy - 8, L, 16], "gameplay",
                        {"onTime": on, "offTime": off, "warnTime": warn, "phase": phase, "active": active},
                        rotation=rotation)

    # -------------------------------------------------------------- gameplay
    def start(self, x, y_ground):
        return self.obj("player_start", [x * T - 5, y_ground * T - 22, 10, 22])

    def checkpoint(self, x, y_ground, label="", optional=False):
        """optional: checkpoint of a detour or of an alternative route (the shortcut detector
        does not require the route to pass by it)."""
        props = {"label": label}
        if optional:
            props["optional"] = True
        return self.obj("checkpoint", [x * T - 12, y_ground * T - 48, 24, 48], props=props)

    def exit(self, x, y_ground, locked=False):
        return self.obj("exit", [x * T - 20, y_ground * T - 64, 40, 64], props={"locked": locked})

    def shard(self, x, y):
        return self.obj("shard", [x * T - 6, y * T - 6, 12, 12], props={"value": 50})

    def shards_line(self, x0, y0, x1, y1, n):
        for i in range(n):
            t = i / (n - 1) if n > 1 else 0
            self.shard(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t)

    def shards_arc(self, x0, x1, y, height, n):
        for i in range(n):
            t = i / (n - 1) if n > 1 else 0
            self.shard(x0 + (x1 - x0) * t, y - height * 4 * t * (1 - t))

    def relic(self, x, y, name):
        return self.obj("relic", [x * T - 8, y * T - 8, 16, 16], props={"value": 1000, "name": name})

    def orb(self, kind, x, y):
        return self.obj(kind, [x * T - 6, y * T - 6, 12, 12], props={"amount": 2 if kind == "health_orb" else 40})

    def power(self, x, y, power, message=""):
        return self.obj("power_unlock", [x * T - 12, y * T - 12, 24, 24], props={"power": power, "message": message})

    def door(self, x, y_top, h_tiles=4, open_=False):
        return self.obj("door", [x * T, y_top * T, 16, h_tiles * T], props={"open": open_})

    def lever(self, x, y_ground, targets, one_shot=True):
        return self.obj("lever", [x * T - 8, y_ground * T - 16, 16, 16], props={"targets": targets, "oneShot": one_shot})

    def receptor(self, x, y_ground, targets, duration=0.0):
        return self.obj("receptor", [x * T - 8, y_ground * T - 32, 16, 32],
                        props={"targets": targets, "duration": duration})

    def sign(self, x, y_ground, text, auto=True):
        return self.obj("sign", [x * T - 12, y_ground * T - 24, 24, 24], props={"text": text, "auto": auto})

    def trigger(self, x0, y0, x1, y1, once=True):
        return self.obj("trigger", [x0 * T, y0 * T, (x1 - x0) * T, (y1 - y0) * T], props={"once": once})

    def combat(self, x0, y0, x1, y1, doors="", waves="", lock=True, music=""):
        return self.obj("combat_zone", [x0 * T, y0 * T, (x1 - x0) * T, (y1 - y0) * T],
                        props={"doors": doors, "waves": waves, "lockCamera": lock, "music": music})

    def camzone(self, x0, y0, x1, y1, mode="clamp", zoom=0.0):
        return self.obj("camera_zone", [x0 * T, y0 * T, (x1 - x0) * T, (y1 - y0) * T],
                        props={"mode": mode, "zoom": zoom})

    def secret(self, x0, y0, x1, y1, message):
        return self.obj("secret_area", [x0 * T, y0 * T, (x1 - x0) * T, (y1 - y0) * T], props={"message": message})

    def enemy(self, kind, x, y_ground, patrol_tiles=6, face_left=False, dormant=False, boss=False):
        """(x, y_ground) in tiles = feet of the enemy (flying ones hover from there)."""
        w, h = enemy_size(kind)
        return self.obj("enemy", [x * T - w / 2, y_ground * T - h, w, h],
                        props={"enemy": kind, "faceLeft": face_left, "patrol": patrol_tiles * T,
                               "dormant": dormant, "boss": boss})

    # -------------------------------------------------------------- abilities
    def grapple(self, x, y):
        """Grapple anchor centred on (x, y) tiles; reachable from about 10 tiles."""
        return self.obj("grapple_point", [x * T - 12, y * T - 12, 24, 24])

    def updraft(self, x0, x1, y_top, y_bottom, strength=230, vent=True):
        return self.obj("updraft", [x0 * T, y_top * T, (x1 - x0) * T, (y_bottom - y_top) * T], "gameplay",
                        {"strength": strength, "vent": vent})

    def cracked_floor(self, x0, x1, y, h_tiles=1, respawn=False):
        """Floor slab broken by a ground pound (top at row y)."""
        return self.gate(x0, y, x1 - x0, h_tiles, "ground_pound", respawn)

    def heart(self, x, y, hearts=1):
        """Crystal heart (max health +1 for the level) centred on (x, y) tiles."""
        return self.obj("heart_container", [x * T - 8, y * T - 8, 16, 16], props={"hearts": hearts})

    # ----------------------------------------------------------------- decor
    def decor(self, name, x, y, layer="bg_decor", scale=1.0, flip=False, alpha=1.0, tint="#ffffff", sway=0.0,
              rotation=0, glow=True):
        """(x, y) in tiles = the sprite pivot position (bottom-centre for most sprites)."""
        s = DECOR[name]
        w, h = s["rect"][2] * scale, s["rect"][3] * scale
        px, py = s["pivot"][0] * scale, s["pivot"][1] * scale
        rect = [x * T - px, y * T - py, w, h]
        props = {"sprite": name, "alpha": alpha, "tint": tint, "animSpeed": 1.0, "glow": glow, "sway": sway}
        return self.obj("decor", rect, layer, props, rotation=rotation, flip=flip)

    def light(self, x, y, color="#39f0d8", radius=80, flicker=0.1, bloom=True):
        return self.obj("light", [x * T - 8, y * T - 8, 16, 16], "gameplay",
                        {"color": color, "radius": radius, "flicker": flicker, "bloom": bloom})

    def emitter(self, x0, y0, x1, y1, effect="fireflies", rate=1.5, layer="gameplay"):
        return self.obj("emitter", [x0 * T, y0 * T, (x1 - x0) * T, (y1 - y0) * T], layer,
                        {"effect": effect, "rate": rate})

    def apply_seals(self):
        """Gates computed by the shortcut detector (CosmicPlatformer --seal-levels
        tools/levelgen/seals): solid columns from the top of the level that force the route
        past the checkpoints."""
        seal = os.path.join(os.path.dirname(os.path.abspath(__file__)), "seals", self.data["id"] + ".json")
        if not os.path.exists(seal):
            return
        with open(seal, encoding="utf-8") as f:
            gates = json.load(f)
        for i, g in enumerate(gates):
            self.obj("solid", list(g["rect"]), "terrain", {"material": g.get("material", "ruin_stone"), "seal": True},
                     oid="seal_%d" % (i + 1))

    def needs(self, oid, power):
        """Marks an object as asking for a power (power stations planning): a hazard only
        the shield crosses, a receptor out of the wave's reach..."""
        for o in self.data["objects"]:
            if o["id"] == oid:
                o.setdefault("props", {})["needsPower"] = power
                return oid
        raise KeyError(oid)

    def apply_power_slots(self):
        """Power slots (version 2.3): a campaign level carries at most MAX_POWERS powers. The
        start set comes from START_POWERS; the power stations (tablets + sign) computed by
        CosmicPlatformer --plan-stations tools/levelgen/stations are added on the route."""
        lid = self.data["id"]
        if lid not in START_POWERS:
            return
        self.data["startPowers"] = list(START_POWERS[lid])
        self.data["maxPowers"] = MAX_POWERS
        plan = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stations", lid + ".json")
        if not os.path.exists(plan):
            return
        with open(plan, encoding="utf-8") as f:
            stations = json.load(f).get("stations", [])
        for i, st in enumerate(stations):
            for j, t in enumerate(st["tablets"]):
                cx, cy = t["center"]
                self.obj("power_unlock", [round(cx) - 12, round(cy) - 12, 24, 24],
                         props={"power": t["power"], "message": "", "station": True}, oid="station_%d_%d" % (i + 1, j + 1))
            names = ", ".join(POWER_NAMES.get(p, p) for p in st["upcoming"] if p)
            sx, sy = st["sign"]
            if st.get("mid"):
                text = "Station de pouvoirs : il faut maintenant %s. Le pouvoir que vous laissez reste ici." % names
            else:
                text = ("Station de pouvoirs. Pour la suite : %s. Vous portez %d pouvoirs au plus : "
                        "celui que vous laissez reste ici." % (names, MAX_POWERS))
            self.obj("sign", [round(sx) - 12, round(sy) - 24, 24, 24], props={"text": text, "auto": True},
                     oid="station_sign_%d" % (i + 1))

    def harden(self):
        """Campaign difficulty (version 2.1.3): one health orb out of two is removed (hearts
        and energy orbs stay)."""
        orbs = [o for o in self.data["objects"] if o["type"] == "health_orb"]
        drop = {id(o) for i, o in enumerate(orbs) if i % 2 == 1}
        self.data["objects"] = [o for o in self.data["objects"] if id(o) not in drop]

    def save(self, path):
        if self.data.get("metadata", {}).get("generator") not in ("tools/levelgen/challenge.py", "tools/levelgen/stellar_core.py"):
            self.harden()
        self.apply_seals()
        self.apply_power_slots()
        meta = self.data.setdefault("metadata", {})
        if meta.get("revision", 0) < 6:
            meta["revision"] = 6  # 2.5: energy 150, new powers (simulation 6)
        ids = [o["id"] for o in self.data["objects"]]
        assert len(ids) == len(set(ids)), "duplicate ids"
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self.data, f, ensure_ascii=False, indent=1)
            f.write("\n")
        print("wrote %s: %d objects, %d events" % (path, len(self.data["objects"]), len(self.data["events"])))
