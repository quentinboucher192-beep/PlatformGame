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
        "description": ("Sous les ruines suspendues, la machinerie des Anciens bat encore : lasers en rythme, "
                        "portes à remonter le temps, leviers et ancrages au-dessus de l'acide. L'Archonte veille."),
        "motto": "Le mécanisme des Anciens bat encore : suivez son rythme.",
        "floor": "ruin_stone", "roof": "root", "accent": "ancient_metal", "oneway": "root_branch", "crumble": "ruins",
        "hazard": "acid",
        "boss": "archon_catacombs", "title": "SYLVANOX", "light": "#39f0d8",
        "ground": ["glowmite", "stalker", "bulwark", "golem", "gloop"], "air": ["wisp", "nightwing", "bomber"],
        "turret": ["sentinel", "sporeflower"],
        "decor_ground": ["mushroom_cluster", "glow_fern", "glow_flowers", "ruin_pillar_broken"],
        "decor_hang": ["hanging_vine_short", "hanging_vine_long", "roots"],
        "powers": ["dash", "wave", "shield", "impulse", "grapple"],
        # Signature: the Ancients' machinery (beat-synchronised lasers, timed doors, levers,
        # crystal walls broken mid-air, anchors between lasers).
        "rooms": ["metronome", "timed_door", "cp", "anchors", "dash_walls", "cp", "lever_hall", "metronome_fast", "cp",
                  "arena"],
    },
    "pyralis": {
        "id": "pyralis_04", "name": "Le Creuset Profond", "seed": 4402,
        "description": ("Au fond des forges, le creuset bouillonne : radeaux de basalte sur la lave, cheminées "
                        "brûlantes, pistons, fonderie à franchir à temps. Son cœur vivant vous attend."),
        "motto": "Le creuset ne pardonne rien : la lave attend la moindre hésitation.",
        "floor": "basalt", "roof": "basalt", "accent": "forge_metal", "oneway": "chain_grate", "crumble": "basalt",
        "hazard": "lava",
        "boss": "crucible_heart", "title": "PYRALIS", "light": "#ff9a3c",
        "ground": ["ember_mite", "lava_stalker", "forge_bulwark", "magma_golem", "magma_gloop"],
        "air": ["fire_wisp", "ash_wing", "forge_bomber"], "turret": ["forge_drone", "cinder_bloom"],
        "decor_ground": ["slag_pile", "ember_crystal", "ash_rock_small", "forge_brazier"],
        "decor_hang": ["hanging_chain", "hanging_chain_long"],
        "powers": ["dash", "wave", "shield", "glide", "ground_pound", "charge_shot"],
        # Signature: the crucible (rafts over lava, heat chimneys, forge pistons, a foundry
        # door to race through, floors broken with ground pounds).
        "rooms": ["rafts", "vents", "cp", "pistons", "foundry", "cp", "pound", "rafts_long", "cp", "arena"],
    },
    "glacior": {
        "id": "glacior_04", "name": "La Crevasse Hurlante", "seed": 4403,
        "description": ("Une crevasse sans fond fend le glacier : pentes de glace vive, ponts qui cèdent au "
                        "premier pas, lac gelé à traverser par-dessous, couloir des vents. La Wyrm hurle au fond."),
        "motto": "La glace ne retient personne : le vent décide.",
        "floor": "snow_rock", "roof": "glacier_ice", "accent": "frost_metal", "oneway": "ice_ledge", "crumble": "ice",
        "hazard": None,
        "boss": "howling_wyrm", "title": "GLACIOR", "light": "#9fe8ff",
        "ground": ["frost_mite", "snow_stalker", "frost_bulwark", "ice_golem", "ice_gloop"],
        "air": ["frost_wisp", "frost_wing", "cryo_bomber"], "turret": ["cryo_drone", "frost_bloom"],
        "decor_ground": ["ice_crystal", "frost_flower", "snow_rock_small", "ice_crystal_big"],
        "decor_hang": ["icicles", "icicles_long"],
        "powers": ["dash", "wave", "shield", "glide", "triple_jump"],
        # Signature: ice and wind (slippery slopes, bridges that break at once, swimming under
        # the ice sheet, wind corridors, slippery peaks).
        "rooms": ["ice_slope", "frail_bridges", "cp", "under_ice", "wind", "cp", "ice_peaks", "frail_bridges_fast", "cp",
                  "arena"],
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

    def pit(self, x0, x1, floor=None, depth=3):
        """The planet's hazard `depth` tiles under the floor: a miss is fatal and the pit is too
        shallow to glide far over it (gliding only stretches a jump)."""
        top = (self.f if floor is None else floor) + depth
        if self.th["hazard"]:
            self.L.liquid(x0, x1, top, H, self.th["hazard"])
        else:
            self.L.solid(x0, top + 1, x1, H, self.th["floor"], deco=False)
            self.L.spikes(x0, x1, top + 1, damage=3)

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
            self.pit(x + pw, x + pw + gw, floor=max(f, max(20, min(44, f + dy))))
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
            self.pit(x + 6, x + 9, floor=f + 4)
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
        self.L.updraft(x + 5, x + 10, top - 6, f + 3, strength=240)
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


    # ------------------------------------------------------------- helpers
    def laser(self, x, y, length, rotation=0, on=1.5, off=1.5, warn=0.5, phase=0.0, active=True, damage=3):
        oid = self.L.laser(x, y, length, rotation=rotation, on=on, off=off, warn=warn, phase=phase, active=active)
        self.L.data["objects"][-1]["props"]["damage"] = damage
        return oid

    def wall_above(self, x, top, y):
        self.L.solid(x, top, x + 1, y, self.th["accent"], deco=False)

    # ================================================ Sylvanox: the machinery
    def r_metronome(self, beat=1.0, delay=0.55, slab=3):
        """Rhythm hall: crumbling slabs over acid, a vertical laser over every gap. Lasers fire
        on alternate beats: gap i is open on the beats where gap i+1 is closed, so the crossing
        is one gap per beat (cross late, run the slab, cross early) - no waiting on a slab."""
        x, f = self.x, self.f
        n = 6
        span = n * (slab + 2) + 3
        top = f - HEADROOM
        self.ground(x, x + 2, f)
        self.roof(x, x + span + 6, top)
        self.pit(x + 2, x + 3 + span)
        for i in range(n):
            cx = x + 3 + 1 + i * (slab + 2)
            self.L.crumble(cx, f, slab, delay=delay, respawn=2.5)
            gx = cx - 1  # middle of the 2-tile gap before this slab
            self.laser(gx, top + 0.5, HEADROOM + 3, rotation=90, on=beat, off=beat, warn=0.35, phase=(i % 2) * beat)
        self.foe(x + span // 2, f - 9, "air", 6)
        self.ground(x + 3 + span, x + span + 6, f)
        self.foe(x + span + 4, f, "turret", 1)
        self.L.sign(x + 1, f, "Les lasers battent la mesure : un saut par temps, sans s'arrêter.")
        self.x = x + span + 6

    def r_metronome_fast(self):
        self.r_metronome(beat=0.8, delay=0.45, slab=2)

    def r_timed_door(self, seconds=6.5):
        """Hit the receptor with the Wave: the door at the far end opens for a few seconds."""
        x, f = self.x, self.f
        w = 36
        top = f - HEADROOM
        door_h = 6
        self.ground(x, x + 15, f)
        self.pit(x + 15, x + 20)
        self.ground(x + 20, x + w, f)
        self.roof(x, x + w, top)
        door_x = x + w - 3
        self.wall_above(door_x, top, f - door_h)
        door = self.L.door(door_x, f - door_h, door_h, open_=False)
        self.L.receptor(x + 6, f, door, duration=seconds)
        self.L.sign(x + 2, f, "Onde sur le récepteur : la porte s'ouvre... quelques secondes.")
        self.L.spikes(x + 10, x + 13, f, damage=2)
        self.L.spikes(x + 24, x + 27, f, damage=2)
        self.foe(x + 30, f, "ground", 2)
        self.foe(x + 22, f, "ground", 2)
        self.foe(x + 17, f - 6, "air", 3)
        self.flora(x, x + 10, f)
        self.x = x + w

    def r_anchors(self):
        """Acid shaft crossed from anchor to anchor; timed lasers fall between the anchors."""
        x, f = self.x, self.f
        span = 33
        top = f - HEADROOM - 2
        self.ground(x, x + 4, f)
        self.roof(x, x + span + 8, top)
        self.pit(x + 4, x + 4 + span)
        for i in range(4):
            gx = x + 4 + 4 + i * 7
            self.L.grapple(gx, f - 7)
            self.L.light(gx, f - 7, self.th["light"], 50)
            if i < 3:
                self.laser(gx + 3.5, top + 0.5, HEADROOM + 5, rotation=90, on=1.0, off=1.6, warn=0.4, phase=0.85 * i)
        self.foe(x + 15, f - 3, "air", 3)
        self.foe(x + 29, f - 3, "air", 3)
        self.L.sign(x + 2, f, "Grappin : d'ancrage en ancrage, entre deux tirs.")
        self.ground(x + 4 + span, x + span + 8, f)
        self.x = x + span + 8

    def r_dash_walls(self):
        """Three acid gaps closed by crystal walls on the far side: break them with a dash in
        mid-air (they grow back)."""
        x, f = self.x, self.f
        top = f - HEADROOM
        x0 = x
        self.ground(x, x + 5, f)
        x += 5
        for i, gap in enumerate((8, 9, 9)):
            self.pit(x, x + gap)
            self.L.gate(x + gap - 1, top, 1, HEADROOM + 3, "dash", respawn=True)
            x += gap
            self.ground(x, x + 6, f)
            self.foe(x + 4, f, "ground", 1)
            if i < 2:
                self.foe(x + 3, f - 5, "air", 2)
            x += 6
        self.roof(x0, x, top)
        self.L.sign(x0 + 2, f, "Murs de cristal : sautez, puis Dash à travers.")
        self.x = x

    def r_lever_hall(self):
        """Hall of levers: laser curtains, always on, in two alternating sets; the lever between
        two curtains swaps the sets - the one ahead opens, the one behind closes. The floor
        between the curtains is made of slabs that crumble under your feet."""
        x, f = self.x, self.f
        n = 6
        seg = 6
        w = 3 + n * seg + 4
        top = f - HEADROOM
        self.ground(x, x + 3, f)
        self.roof(x, x + w, top)
        self.pit(x + 3, x + 3 + n * seg)
        sets = [[], []]
        for i in range(n):
            cx = x + 3 + i * seg
            sets[i % 2].append(self.laser(cx + 0.5, top + 0.5, HEADROOM + 3, rotation=90, on=1.0, off=0.0, warn=0.0,
                                          active=(i % 2 == 1)))
            self.L.crumble(cx + 1, f, seg - 1, delay=1.1, respawn=2.0)
        targets = ",".join(sets[0] + sets[1])
        for i in range(n - 1):
            self.L.lever(x + 3 + i * seg + 4, f, targets, one_shot=False)
        self.ground(x + 3 + n * seg, x + w, f)
        self.foe(x + w - 2, f, "turret", 1)
        self.foe(x + 3 + n * seg // 2, f - 8, "air", 5)
        self.L.sign(x + 1, f, "Chaque levier ouvre le rideau devant vous... et referme celui de derrière.")
        self.x = x + w

    # ================================================== Pyralis: the crucible
    def r_rafts(self, span=44):
        """Lava lake crossed on basalt rafts: the first one leaves when you step on it (choose
        your moment: a laser crosses its route), a lift, then a last raft under a laser."""
        x, f = self.x, self.f
        top = f - HEADROOM
        self.ground(x, x + 3, f)
        self.roof(x, x + span + 6, top)
        self.pit(x + 3, x + 3 + span)
        self.L.moving(x + 3, f, 4, 14, 0, speed=70, wait=1.0, triggered=True)
        self.laser(x + 12, top + 0.5, HEADROOM + 3, rotation=90, on=1.2, off=1.8, warn=0.4)
        self.L.moving(x + 24, f - 1, 3, 0, -5, speed=55, wait=0.5, phase=0.3)
        last = span - 14
        self.L.moving(x + 30, f, 4, last - 16, 0, speed=80, wait=0.6, phase=0.5)
        self.laser(x + 30 + (last - 16) // 2 + 2, top + 0.5, HEADROOM + 3, rotation=90, on=1.0, off=1.6, warn=0.4, phase=0.7)
        self.foe(x + 20, f - 8, "air", 5)
        self.foe(x + span - 6, f - 7, "air", 4)
        self.foe(x + span + 4, f, "turret", 1)
        self.L.sign(x + 1, f, "Le radeau part quand vous montez : choisissez le bon moment.")
        self.ground(x + 3 + span, x + span + 6, f)
        self.x = x + span + 6

    def r_rafts_long(self):
        self.r_rafts(span=52)

    def r_vents(self):
        """Heat chimneys over lava: glide in the rising air from vent to vent; the vault above
        is lined with spikes - leave each column before it throws you against them."""
        x, f = self.x, self.f
        top = f - HEADROOM - 2
        self.ground(x, x + 4, f)
        self.roof(x, x + 44, top)
        self.pit(x + 4, x + 38)
        for i, cx in enumerate((7, 17, 27)):
            self.L.updraft(x + cx, x + cx + 4, top + 1, f + 3, strength=230)
            self.L.spikes(x + cx - 1, x + cx + 5, top + 2, rotation=180, damage=2)
        self.foe(x + 22, f - 9, "air", 4)
        self.foe(x + 12, f - 4, "air", 3)
        self.ground(x + 38, x + 44, f - 6)
        self.foe(x + 42, f - 6, "ground", 1)
        self.L.sign(x + 1, f, "Planeur dans les cheminées. Attention à la voûte.")
        self.x, self.f = x + 44, f - 6

    def r_pistons(self):
        """Forge pistons over lava: four platforms going up and down out of phase, a moving
        staircase to a gallery twelve tiles higher."""
        x, f = self.x, self.f
        top = f - 22
        self.ground(x, x + 4, f)
        self.roof(x, x + 34, top)
        self.pit(x + 4, x + 30)
        for i in range(4):
            px = x + 6 + 6 * i
            self.L.moving(px, f - 2 * i, 3, 0, -6, speed=60, wait=0.5, phase=0.25 * i)
        self.foe(x + 18, f - 14, "air", 4)
        self.foe(x + 10, f - 8, "air", 3)
        self.ground(x + 30, x + 34, f - 12)
        self.x, self.f = x + 34, f - 12
        self.r_flat(4)

    def r_foundry(self, seconds=5.5):
        """Charged shot (or Wave) on the receptor: the foundry door opens for a few seconds,
        across a floor of crumbling basalt over lava."""
        x, f = self.x, self.f
        w = 30
        top = f - HEADROOM
        door_h = 6
        self.ground(x, x + 6, f)
        self.pit(x + 6, x + 22)
        for i in range(4):
            self.L.crumble(x + 6 + i * 4, f, 3, delay=0.35, respawn=2.0)
        self.ground(x + 22, x + w, f)
        self.roof(x, x + w, top)
        door_x = x + w - 3
        self.wall_above(door_x, top, f - door_h)
        door = self.L.door(door_x, f - door_h, door_h, open_=False)
        self.L.receptor(x + 4, f, door, duration=seconds)
        self.foe(x + 25, f, "ground", 1)
        self.foe(x + 14, f - 6, "air", 3)
        self.L.sign(x + 1, f, "Tir chargé sur le récepteur, puis courez.")
        self.x = x + w

    def r_pound(self):
        """Down through the crucible: three cracked floors to break with ground pounds, each
        one on the other side of the room from the last (walk past a lava pocket and a guard
        between two pounds)."""
        x, f = self.x, self.f
        w = 22
        top = f - HEADROOM
        self.roof(x, x + w, top)
        cracks = [(5, 11), (15, 21), (1, 7)]
        for k, (a, b) in enumerate(cracks):
            y = f + 5 * k
            bottom = y + 1
            if a > 0:
                self.L.solid(x, y, x + a, bottom, self.th["floor"], deco=False)
            self.L.cracked_floor(x + a, x + b, y, 1, respawn=True)
            if b < w:
                self.L.solid(x + b, y, x + w, bottom, self.th["floor"], deco=False)
            if k == 1:
                self.L.liquid(x + 12, x + 14, y - 1, y, "lava")
                self.foe(x + 4, y, "ground", 2)
            if k == 2:
                self.foe(x + 14, y, "ground", 2)
        # Right wall down to the last cracked floor: the way on is at the bottom only.
        self.L.solid(x + w - 1, top, x + w, f + 11, self.th["floor"], deco=False)
        floor = f + 15
        self.ground(x, x + w, floor)
        self.L.sign(x + 2, f, "Martèlement au sol : brisez les dalles fissurées, une par une.")
        self.x, self.f = x + w, floor
        self.ground(self.x, self.x + 4, self.f)
        self.roof(self.x, self.x + 4, self.f - HEADROOM)
        self.x += 4

    # ====================================================== Glacior: ice and wind
    def r_ice_slope(self):
        """Slippery steps of glacier ice down to a gap over spikes: keep your momentum, jump at
        the very edge."""
        x, f = self.x, self.f
        top = f - HEADROOM
        for i in range(5):
            self.L.solid(x + 4 * i, f + 2 * i, x + 4 * i + 4, H, "glacier_ice")
        self.roof(x, x + 40, top)
        self.pit(x + 20, x + 30, floor=f + 8)
        self.foe(x + 25, f + 2, "air", 3)
        self.ground(x + 30, x + 40, f + 8)
        self.foe(x + 36, f + 8, "ground", 2)
        self.L.sign(x + 1, f, "Glace vive : impossible de s'arrêter net.")
        self.x, self.f = x + 40, f + 8

    def r_frail_bridges(self, delay=0.3, slab=2):
        """Ice slabs that crack the instant you touch them, over a spiked chasm."""
        x, f = self.x, self.f
        self.ground(x, x + 3, f)
        n = 7
        span = n * (slab + 4) + 3
        self.roof(x, x + span + 6, f - HEADROOM)
        self.pit(x + 3, x + 3 + span)
        for i in range(n):
            cx = x + 3 + 4 + i * (slab + 4)
            self.L.crumble(cx, f - (i % 2), slab, delay=delay, respawn=2.0)
        self.foe(x + span // 2, f - 8, "air", 6)
        self.foe(x + span // 2 + 8, f - 6, "air", 4)
        self.ground(x + 3 + span, x + span + 6, f)
        self.x = x + span + 6

    def r_frail_bridges_fast(self):
        self.r_frail_bridges(delay=0.22, slab=2)

    def r_under_ice(self):
        """A frozen lake: dive through a hole in the ice sheet, swim under it between icicles
        and spikes, come out through the hole at the far end."""
        x, f = self.x, self.f
        w = 36
        top = f - HEADROOM
        self.ground(x, x + 4, f)
        self.roof(x, x + w + 4, top)
        # Pool: water from f to f + 7, bottom at f + 7.
        self.L.liquid(x + 4, x + w, f, f + 7, "icewater")
        self.L.solid(x + 4, f + 7, x + w, H, self.th["floor"])
        # Ice sheet with a hole at each end (3 tiles) and an air pocket in the middle.
        self.L.solid(x + 7, f - 1, x + 18, f + 1, "glacier_ice")
        self.L.solid(x + 21, f - 1, x + w - 3, f + 1, "glacier_ice")
        for a, b in ((9, 12), (24, 27)):
            self.L.spikes(x + a, x + b, f + 7, damage=2)
        for a, b in ((13, 16), (28, 31)):
            self.L.spikes(x + a, x + b, f + 2, rotation=180, damage=2)
        self.foe(x + 20, f - 5, "air", 3)
        self.foe(x + 30, f - 5, "air", 3)
        self.ground(x + w, x + w + 4, f)
        self.L.sign(x + 1, f, "Le lac gelé se traverse par-dessous.")
        self.x = x + w + 4

    def r_wind(self):
        """Wind corridor: rising columns over a spiked chasm, crossed by horizontal frost beams
        on a timer. Glide from column to column."""
        x, f = self.x, self.f
        top = f - HEADROOM - 4
        self.ground(x, x + 4, f)
        self.roof(x, x + 46, top)
        self.pit(x + 4, x + 40)
        for i, cx in enumerate((7, 18, 29)):
            self.L.updraft(x + cx, x + cx + 4, top + 1, f + 3, strength=210)
        for i, y in enumerate((f - 6, f - 12)):
            self.laser(x + 4.5, y, 35, rotation=0, on=1.0, off=1.8, warn=0.45, phase=1.4 * i, damage=2)
        self.foe(x + 24, f - 9, "air", 5)
        self.foe(x + 13, f - 3, "air", 3)
        self.ground(x + 40, x + 46, f - 4)
        self.L.sign(x + 1, f, "Couloir des vents : planez, et surveillez les rayons de givre.")
        self.x, self.f = x + 46, f - 4

    def r_ice_peaks(self):
        """Slippery ice peaks over spikes: land, steady yourself (or jump again at once)."""
        x, f = self.x, self.f
        top = f - HEADROOM - 2
        self.ground(x, x + 4, f)
        xs = [4]
        self.roof(x, x + 52, top)
        self.pit(x + 4, x + 46)
        heights = [0, -2, 1, -3, 0, -2]
        px = x + 4
        for i, dh in enumerate(heights):
            px += 5 if i == 0 else 6 + (i % 2)
            self.L.solid(px, f + dh, px + 2, H, "glacier_ice")
            if i in (1, 4):
                self.foe(px + 1, f + dh - 5, "air", 2)
        self.ground(x + 46, x + 52, f)
        self.x = x + 52

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
        boss = self.L.enemy(self.th["boss"], x + 32, f - (6 if self.th["boss"] == "howling_wyrm" else 0), patrol_tiles=6,
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
                "text": th["motto"], "style": "caption"}},
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
        rising = {"climb": 16, "updraft": 14, "glide": 0, "pistons": 12, "vents": 6, "wind": 4}
        falling = {"ice_slope": 8, "pound": 15}
        for room in rooms:
            if room == "cp":
                self.checkpoint()
                continue
            # Rooms that go up need room above: come back down first if the floor is high;
            # rooms that go down need depth: ride an updraft up first.
            if self.f - rising.get(room, 0) < 22:
                self.r_drop()
            if self.f + falling.get(room, 0) > 42:
                self.rooms.append({"type": "updraft", "x": self.x, "floor": self.f})
                self.r_updraft()
            self.rooms.append({"type": room, "x": self.x, "floor": self.f})
            getattr(self, "r_" + room)()
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
        d["metadata"] = {"generator": "tools/levelgen/challenge.py", "revision": 4, "rooms": self.rooms}
        L.save(os.path.join(ROOT, "data/levels/%s.json" % th["id"]))


if __name__ == "__main__":
    for planet in ("sylvanox", "pyralis", "glacior"):
        Builder(planet).build()
