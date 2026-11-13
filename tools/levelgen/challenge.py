#!/usr/bin/env python3
"""Builds the fourth level of each planet: the "Épreuves" (trials), harder levels made of
calibrated rooms in a closed cave (no route over the top):

  data/levels/sylvanox_04.json  "Les Catacombes des Anciens"
  data/levels/pyralis_04.json   "Le Creuset Profond"
  data/levels/glacior_04.json   "La Crevasse Hurlante"

Every room is sized from the player's physics (data/player.json): a single jump rises 58 px
(3.6 tiles) and carries about 6 tiles at full run, a double jump 6.4 tiles high / 10 tiles
long, the dash adds 4 tiles; ledges, gaps and shafts keep a margin below those values. The
shortcut detector (CosmicPlatformer --analyze-levels) checks the result.
Run from the repository root:  python3 tools/levelgen/challenge.py
"""
import os
import random

from levelkit import Level, ROOT, T

H = 56          # level height (tiles)
HEADROOM = 14   # corridor height above the floor (tiles)

THEMES = {
    "sylvanox": {
        "id": "sylvanox_04", "name": "Les Catacombes des Anciens", "seed": 4401,
        "description": ("Sous les ruines suspendues, les catacombes des Anciens s'enfoncent dans la roche : "
                        "puits, ponts effondrés et gardiens endormis. L'épreuve de Sylvanox, sans raccourci."),
        "floor": "ruin_stone", "roof": "root", "accent": "ancient_metal", "oneway": "root_branch", "crumble": "ruins",
        "hazard": "acid",
        "boss": "thornwarden", "title": "SYLVANOX", "light": "#39f0d8",
        "ground": ["glowmite", "stalker", "bulwark", "golem", "gloop"], "air": ["wisp", "nightwing", "bomber"],
        "turret": ["sentinel", "sporeflower"],
        "decor_ground": ["mushroom_cluster", "glow_fern", "glow_flowers", "ruin_pillar_broken"],
        "decor_hang": ["hanging_vine_short", "hanging_vine_long", "roots"],
        "powers": ["dash", "wave", "shield", "impulse", "teleport", "glide", "grapple"],
        "rooms": ["gaps", "gauntlet", "climb", "crumble", "lasers", "dash_gap", "arena", "updraft",
                  "grapple", "spikes", "movers", "drop", "glide", "gauntlet"],
    },
    "pyralis": {
        "id": "pyralis_04", "name": "Le Creuset Profond", "seed": 4402,
        "description": ("Au fond des forges, le creuset ancestral bouillonne encore : radeaux, cheminées, lasers "
                        "et gardiens de métal. L'épreuve de Pyralis."),
        "floor": "basalt", "roof": "basalt", "accent": "forge_metal", "oneway": "chain_grate", "crumble": "basalt",
        "hazard": "lava",
        "boss": "magmaroth", "title": "PYRALIS", "light": "#ff9a3c",
        "ground": ["ember_mite", "lava_stalker", "forge_bulwark", "magma_golem", "magma_gloop"],
        "air": ["fire_wisp", "ash_wing", "forge_bomber"], "turret": ["forge_drone", "cinder_bloom"],
        "decor_ground": ["slag_pile", "ember_crystal", "ash_rock_small", "forge_brazier"],
        "decor_hang": ["hanging_chain", "hanging_chain_long"],
        "powers": ["dash", "wave", "shield", "impulse", "teleport", "glide", "grapple", "ground_pound", "charge_shot"],
        "rooms": ["gauntlet", "gaps", "movers", "lasers", "climb", "arena", "crumble", "dash_gap", "updraft",
                  "spikes", "grapple", "drop", "gaps", "gauntlet"],
    },
    "glacior": {
        "id": "glacior_04", "name": "La Crevasse Hurlante", "seed": 4403,
        "description": ("Une crevasse sans fond fend le glacier : ponts de glace fragiles, vents ascendants et "
                        "sentinelles du froid. L'épreuve de Glacior, la plus dure de la campagne."),
        "floor": "snow_rock", "roof": "glacier_ice", "accent": "frost_metal", "oneway": "ice_ledge", "crumble": "ice",
        "hazard": None,
        "boss": "cryowyrm", "title": "GLACIOR", "light": "#9fe8ff",
        "ground": ["frost_mite", "snow_stalker", "frost_bulwark", "ice_golem", "ice_gloop"],
        "air": ["frost_wisp", "frost_wing", "cryo_bomber"], "turret": ["cryo_drone", "frost_bloom"],
        "decor_ground": ["ice_crystal", "frost_flower", "snow_rock_small", "ice_crystal_big"],
        "decor_hang": ["icicles", "icicles_long"],
        "powers": ["dash", "wave", "shield", "impulse", "teleport", "glide", "grapple", "ground_pound", "charge_shot",
                   "triple_jump"],
        "rooms": ["crumble", "gaps", "gauntlet", "updraft", "lasers", "climb", "arena", "dash_gap", "grapple",
                  "spikes", "movers", "crumble", "drop", "glide", "gauntlet"],
    },
}


class Builder:
    def __init__(self, planet):
        self.th = THEMES[planet]
        self.planet = planet
        self.rng = random.Random(self.th["seed"])
        # Width is fixed afterwards (bounds rewritten in finish()).
        self.L = Level(self.th["id"], self.th["name"], 1000, H)
        self.L.planet(planet)
        self.L.crumble_style = self.th["crumble"]
        self.x = 0
        self.f = 40          # current floor (tile row of the floor top)
        self.cp = 0
        self.waves = 0
        self.rooms = []  # (type, x, floor) of every room, kept in the metadata for the tests

    # ------------------------------------------------------------- primitives
    def ground(self, x0, x1, top):
        self.L.solid(x0, top, x1, H, self.th["floor"])

    def roof(self, x0, x1, bottom):
        self.L.solid(x0, 0, x1, bottom, self.th["roof"], deco=False)
        for gx in range(x0 + 1, x1 - 1, 5):
            if self.rng.random() < 0.55:
                self.L.decor(self.rng.choice(self.th["decor_hang"]), gx + 0.5, bottom, layer="bg_decor")

    def pit(self, x0, x1):
        """Bottomless (to the kill line) with the planet's hazard drawn at the bottom."""
        if self.th["hazard"]:
            self.L.liquid(x0, x1, H - 4, H, self.th["hazard"])
        else:
            self.L.spikes(x0, x1, H - 1)

    def flora(self, x0, x1, top):
        for gx in range(x0 + 1, x1 - 1, 4):
            if self.rng.random() < 0.45:
                self.L.decor(self.rng.choice(self.th["decor_ground"]), gx + 0.5, top, layer="bg_decor")

    def foe(self, x, y, kind="ground", patrol=4, dormant=False):
        pool = self.th[kind]
        return self.L.enemy(self.rng.choice(pool), x, y, patrol_tiles=patrol, face_left=self.rng.random() < 0.5,
                            dormant=dormant)

    def light(self, x, y):
        self.L.light(x, y, self.th["light"], 80, flicker=0.12)

    def checkpoint(self):
        self.cp += 1
        names = ["Seuil", "Galerie", "Croisée", "Antre", "Passage", "Abîme", "Sanctuaire", "Porte", "Faille"]
        label = "%s %s" % (names[(self.cp - 1) % len(names)], ["I", "II", "III", "IV", "V", "VI", "VII", "VIII"][min(self.cp - 1, 7)])
        self.ground(self.x, self.x + 8, self.f)
        self.roof(self.x, self.x + 8, self.f - HEADROOM)
        self.L.checkpoint(self.x + 4, self.f, label)
        self.light(self.x + 4, self.f - 5)
        self.x += 8

    # ------------------------------------------------------------------ rooms
    def r_flat(self, w=12):
        self.ground(self.x, self.x + w, self.f)
        self.roof(self.x, self.x + w, self.f - HEADROOM)
        self.flora(self.x, self.x + w, self.f)
        self.x += w

    def r_gaps(self):
        """Platforms over pits: single-jump gaps (4-5) and double-jump gaps (7-8), small steps."""
        x, f = self.x, self.f
        plan = [(6, 4, 0), (5, 5, -2), (5, 7, 0), (4, 5, 2), (5, 8, -1), (6, 4, 1)]
        top_min = f
        for (pw, gw, dy) in plan:
            self.ground(x, x + pw, f)
            self.L.shards_line(x + 1, f - 2, x + pw - 1, f - 2, 2)
            top_min = min(top_min, f)
            self.roof(x, x + pw + gw, min(f, top_min) - HEADROOM)
            self.pit(x + pw, x + pw + gw)
            x += pw + gw
            f = max(20, min(44, f + dy))
        self.foe(x - 14, f - 6, "air", 3)
        self.x, self.f = x, f
        self.r_flat(4)

    def r_gauntlet(self):
        w = 34
        self.ground(self.x, self.x + w, self.f)
        self.roof(self.x, self.x + w, self.f - HEADROOM)
        for i, gx in enumerate((8, 15, 22, 29)):
            self.foe(self.x + gx, self.f, "ground", 3)
        self.foe(self.x + 18, self.f - 7, "air", 4)
        self.L.orb("health_orb", self.x + 2, self.f - 1.5)
        self.flora(self.x, self.x + w, self.f)
        self.x += w

    def r_climb(self):
        """Zigzag shaft: one-way ledges every 4 tiles (double jump), 16 tiles up."""
        x, f = self.x, self.f
        w = 12
        self.ground(x, x + w, f)
        top = f - 16
        self.roof(x, x + w + 6, top - 10)
        for i in range(1, 5):
            y = f - 4 * i
            if i % 2:
                self.L.oneway(x + 1, x + 6, y, self.th["oneway"])
            else:
                self.L.oneway(x + 6, x + 12, y, self.th["oneway"])  # touches the right wall (top exit)
            self.L.shard(x + (3.5 if i % 2 else 8.5), y - 1.5)
        self.foe(x + 6, f - 9, "air", 2)
        # top exit: ledge to the right at the new floor level
        self.ground(x + w, x + w + 6, top)
        self.x, self.f = x + w + 6, top
        self.r_flat(4)

    def r_drop(self):
        """Descending ledges back towards the lower floor (falls are free, enemies are not)."""
        x, f = self.x, self.f
        steps = max(0, min(4, (40 - f) // 4))
        for i in range(steps):
            self.ground(x, x + 6, f)
            self.roof(x, x + 6 + 3, f - HEADROOM)
            self.pit(x + 6, x + 9)
            x += 9
            f += 4
        self.ground(x, x + 8, f)
        self.roof(x, x + 8, f - HEADROOM)
        self.foe(x + 5, f, "ground", 2)
        self.x, self.f = x + 8, f

    def r_crumble(self):
        """Bridge of crumbling slabs (3 wide, gaps of 3) over a 27-tile pit."""
        x, f = self.x, self.f
        self.ground(x, x + 3, f)
        span = 27
        self.roof(x, x + span + 6, f - HEADROOM)
        self.pit(x + 3, x + 3 + span)
        for i in range(5):
            cx = x + 3 + 2 + i * 5
            self.L.crumble(cx, f, 3, delay=0.38, respawn=2.5)
            self.L.shard(cx + 1.5, f - 2)
        self.foe(x + 3 + span // 2, f - 7, "air", 6)
        self.ground(x + 3 + span, x + span + 6, f)
        self.x = x + span + 6

    def r_movers(self):
        """Two moving platforms across a 26-tile pit (timing)."""
        x, f = self.x, self.f
        self.ground(x, x + 3, f)
        span = 26
        self.roof(x, x + span + 6, f - HEADROOM)
        self.pit(x + 3, x + 3 + span)
        self.L.moving(x + 4, f, 4, 7, 0, speed=70, wait=0.5)
        self.L.moving(x + 17, f - 2, 4, 5, 0, speed=80, wait=0.4, phase=0.5)
        self.L.shards_line(x + 8, f - 3, x + 20, f - 4, 4)
        self.ground(x + 3 + span, x + span + 6, f)
        self.x = x + span + 6

    def r_spikes(self):
        """Low corridor (7 tiles high) with spike strips: no room to float over them."""
        x, f = self.x, self.f
        w = 30
        self.ground(x, x + w, f)
        self.roof(x, x + w, f - 7)
        for gx in (4, 11, 18, 25):
            self.L.spikes(x + gx, x + gx + 3, f)
        self.foe(x + 15, f, "ground", 2)
        self.x = x + w
        # back to the normal headroom
        self.r_flat(3)

    def r_lasers(self):
        """Corridor crossed by timed vertical lasers (staggered)."""
        x, f = self.x, self.f
        w = 28
        self.ground(x, x + w, f)
        top = f - HEADROOM
        self.roof(x, x + w, top)
        for i, gx in enumerate((7, 14, 21)):
            self.L.laser(x + gx, top + 0.5, HEADROOM, rotation=90, on=1.3, off=1.5, warn=0.5, phase=0.9 * i)
        self.foe(x + 24, f, "turret", 1)
        self.flora(x, x + w, f)
        self.x = x + w

    def r_dash_gap(self):
        """A 12-tile gap: double jump plus dash."""
        x, f = self.x, self.f
        self.ground(x, x + 6, f)
        self.roof(x, x + 6 + 12 + 6, f - HEADROOM)
        self.pit(x + 6, x + 18)
        self.L.shards_line(x + 8, f - 4, x + 16, f - 4, 3)
        self.L.sign(x + 3, f, "Saut, double saut, puis Dash au sommet.")
        self.ground(x + 18, x + 24, f)
        self.x = x + 24

    def r_updraft(self):
        """Ride a rising current from a pit to a ledge 14 tiles higher."""
        x, f = self.x, self.f
        self.ground(x, x + 4, f)
        rise = 14
        top = f - rise
        self.roof(x, x + 4 + 7 + 8, top - 8)
        self.pit(x + 4, x + 11)
        self.L.updraft(x + 5, x + 10, top - 6, H - 4, strength=240)
        self.ground(x + 11, x + 19, top)
        self.foe(x + 15, top, "ground", 2)
        self.x, self.f = x + 19, top

    def r_grapple(self):
        """A 30-tile pit crossed with anchors (every 7 tiles, 7 tiles above the floor)."""
        x, f = self.x, self.f
        self.ground(x, x + 4, f)
        span = 30
        self.roof(x, x + span + 8, f - HEADROOM - 2)
        self.pit(x + 4, x + 4 + span)
        for i in range(4):
            gx = x + 4 + 4 + i * 7
            self.L.grapple(gx, f - 7)
            self.L.light(gx, f - 7, self.th["light"], 50)
        self.L.sign(x + 2, f, "Grappin : enchaînez les ancrages.")
        self.ground(x + 4 + span, x + span + 8, f)
        self.x = x + span + 8

    def r_glide(self):
        """From a high ledge, glide over a 34-tile chasm to a lower shore."""
        x, f = self.x, self.f
        high = max(20, f - 10)
        # stairs up to the high ledge
        self.ground(x, x + 5, f)
        self.ground(x + 5, x + 9, f - 4)
        self.ground(x + 9, x + 14, high)
        self.roof(x, x + 14 + 34 + 6, high - 8)
        self.pit(x + 14, x + 48)
        self.L.shards_line(x + 18, high - 1, x + 44, f - 3, 6)
        self.foe(x + 30, high + 2, "air", 6)
        self.ground(x + 48, x + 54, f)
        self.x = x + 54

    def r_arena(self):
        """Closed arena: doors on both sides, two waves of guardians."""
        x, f = self.x, self.f
        w = 34
        door_h = 6
        self.ground(x, x + w, f)
        self.roof(x, x + w, f - HEADROOM)
        # walls above the doors, from the roof down to the door top
        self.L.solid(x, f - HEADROOM, x + 1, f - door_h, self.th["accent"], deco=False)
        self.L.solid(x + w - 1, f - HEADROOM, x + w, f - door_h, self.th["accent"], deco=False)
        left = self.L.door(x, f - door_h, door_h, open_=True)
        right = self.L.door(x + w - 1, f - door_h, door_h, open_=False)
        w1 = [self.foe(x + gx, f, "ground", 3, dormant=True) for gx in (9, 17, 25)]
        w2 = [self.foe(x + gx, f - 6, "air", 3, dormant=True) for gx in (11, 23)] + \
             [self.foe(x + 17, f, "ground", 4, dormant=True)]
        self.L.combat(x + 1, f - HEADROOM, x + w - 1, f, doors=left + "," + right,
                      waves=",".join(w1) + ";" + ",".join(w2))
        self.L.orb("health_orb", x + 17, f - 1.5)
        self.x = x + w

    def r_boss(self):
        """Final arena: the planet's guardian returns (doors close until it falls)."""
        x, f = self.x, self.f
        w, head, door_h = 46, 18, 8
        self.ground(x, x + w, f)
        self.roof(x, x + w, f - head)
        self.L.solid(x, f - head, x + 1, f - door_h, self.th["accent"], deco=False)
        self.L.solid(x + w - 1, f - head, x + w, f - door_h, self.th["accent"], deco=False)
        left = self.L.door(x, f - door_h, door_h, open_=True)
        right = self.L.door(x + w - 1, f - door_h, door_h, open_=False)
        boss = self.L.enemy(self.th["boss"], x + 32, f - (6 if self.th["boss"] == "cryowyrm" else 0), patrol_tiles=6,
                            face_left=True, dormant=True, boss=True)
        zid = self.L.combat(x + 1, f - head, x + w - 1, f, doors=left + "," + right, waves=boss, lock=False,
                            music=self.L.data.get("bossMusic", ""))
        self.L.data["objects"][-1]["props"]["announce"] = False
        self.L.orb("health_orb", x + 4, f - 1.5)
        self.light(x + 23, f - 10)
        self.x = x + w
        return zid

    def cinematics(self, width):
        th, d = self.th, self.L.data
        d["intro"] = {"skippable": True, "letterbox": True, "steps": [
            {"type": "fade", "duration": 0.01, "params": {"to": 1}},
            {"type": "cut", "duration": 0.01, "params": {"x": 6 * T, "y": (self.start_floor - 6) * T}},
            {"type": "fade", "duration": 1.4, "wait": False, "params": {"to": 0}},
            {"type": "text", "duration": 3.5, "wait": False, "params": {"text": th["title"], "subtitle": th["name"]}},
            {"type": "camera", "duration": 4.0, "params": {"x": 60 * T, "y": (self.start_floor - 6) * T, "ease": "inOutSine"}},
            {"type": "text", "duration": 3.0, "wait": False, "params": {
                "text": "L'épreuve : pas de raccourci, pas de répit.", "style": "caption"}},
            {"type": "camera", "duration": 2.5, "params": {"start": True, "ease": "inOutCubic"}},
        ]}
        d["outro"] = {"skippable": True, "letterbox": True, "steps": [
            {"type": "zoom", "duration": 1.4, "wait": False, "params": {"zoom": 1.4}},
            {"type": "camera", "duration": 1.4, "params": {"player": True}},
            {"type": "text", "duration": 3.0, "wait": False, "params": {"text": "ÉPREUVE RÉUSSIE", "subtitle": th["title"].capitalize() + " — " + th["name"]}},
            {"type": "playerAnim", "duration": 2.2, "params": {"anim": "victory"}},
            {"type": "fade", "duration": 0.9, "params": {"to": 1}},
        ]}

    # ------------------------------------------------------------------ level
    def build(self):
        th, L = self.th, self.L
        d = L.data
        d["description"] = th["description"]
        d["difficulty"] = 5
        d["startPowers"] = th["powers"]
        d["killY"] = (H - 1) * T
        d["cameraZoom"] = 1.0
        # start
        self.start_floor = self.f
        self.ground(0, 14, self.f)
        self.roof(0, 14, self.f - HEADROOM)
        L.solid(0, 0, 1, H, th["floor"])
        L.start(4, self.f)
        L.sign(8, self.f, "L'épreuve commence. Aucun raccourci : seulement vous et la grotte.")
        self.light(6, self.f - 5)
        self.x = 14
        rooms = th["rooms"]
        rising = {"climb": 16, "updraft": 14, "glide": 0}
        for i, room in enumerate(rooms):
            # Rooms that go up need room above: come back down first if the floor is high.
            if self.f - rising.get(room, 0) < 22:
                self.r_drop()
            self.rooms.append({"type": room, "x": self.x, "floor": self.f})
            getattr(self, "r_" + room)()
            if i % 2 == 1 and i < len(rooms) - 1:
                self.checkpoint()
        # back down to a comfortable floor for the guardian and the exit
        if self.f < 34:
            self.r_drop()
        self.checkpoint()
        self.rooms.append({"type": "boss", "x": self.x, "floor": self.f})
        self.r_boss()
        self.ground(self.x, self.x + 16, self.f)
        self.roof(self.x, self.x + 16, self.f - HEADROOM)
        L.exit(self.x + 10, self.f)
        self.light(self.x + 10, self.f - 5)
        L.solid(self.x + 16, 0, self.x + 17, H, th["floor"])
        width = self.x + 17
        d["bounds"] = [0, 0, width * T, H * T]
        # timer and medals from the length (about 2.2 s per 10 tiles plus the rooms)
        ref = int(round(width * T / 125.0 + len(rooms) * 4))
        d["timer"] = {"mode": "limited", "limit": max(600, ref * 4), "onExpire": "gameover", "referenceTime": ref,
                      "challengeMaxDeaths": 0}
        d["medals"] = {"bronze": {}, "silver": {"maxTime": ref * 2}, "gold": {"maxTime": int(ref * 1.35)},
                       "platinum": {"maxTime": ref, "maxDeaths": 0}}
        self.cinematics(width)
        d["metadata"] = {"generator": "tools/levelgen/challenge.py", "revision": 3, "rooms": self.rooms}
        L.save(os.path.join(ROOT, "data/levels/%s.json" % th["id"]))


if __name__ == "__main__":
    for planet in ("sylvanox", "pyralis", "glacior"):
        Builder(planet).build()
