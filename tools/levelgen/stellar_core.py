#!/usr/bin/env python3
"""Builds the three levels of the Stellar Core (Noyau stellaire, version 2.7), the star at the
centre of the system, opened once Sylvanox, Glacior and Pyralis are completed:

  data/levels/noyau_01.json  "L'Arène des Échos"     the sixteen guardians in a row (boss rush):
                                                       no healing, no checkpoint, one life
  data/levels/noyau_02.json  "Le Cœur de l'Étoile"   the final level (plan: maquette
                                                       NiveauFinal): echoes of the three planets,
                                                       5 ambushes, 4 bosses, the Astrarque last
  data/levels/noyau_03.json  "Le Cœur d'une Étoile Mourante" (2.33, EXTRÊME): very hard
                                                       platforming, one life, no checkpoint, then
                                                       the giant arena of Singularis (8 phases)

The rooms are the calibrated rooms of the trials (challenge.py), built with the materials and
the enemies of the planet each zone echoes. Run from the repository root after
stellar_bosses.py:  python3 tools/levelgen/stellar_core.py
"""
import json
import os

from challenge import Builder, H, HEADROOM, THEMES
from levelkit import Level, ROOT, T
from aethra import LEVELS as AETHRA_LEVELS, STONE as AETHRA_STONE, tile

# Aethra (2.19) in the star's light: the void island's theme, on the islands' rock.
THEMES["aethra"] = dict(AETHRA_LEVELS["aethra_04"], floor=tile(AETHRA_STONE, "obsidian"))

ECHO_ORDER = [("sylvanox", ["thornwarden", "spore_queen", "prime_sentinel", "archon_catacombs"]),
              ("glacior", ["cryowyrm", "frost_hydra", "glaciarch", "howling_wyrm"]),
              ("pyralis", ["magmaroth", "ignivore", "forge_core", "crucible_heart"]),
              ("aethra", ["mycelia", "aurorax", "pyrogyre", "nyxara"])]
ECHOES = sum(len(b) for _, b in ECHO_ORDER)
SKULL = {"sylvanox": "#39f0d8", "glacior": "#9fe8ff", "pyralis": "#ff8a5c", "noyau": "#ffd98a", "aethra": "#c9a2ff"}


def enemy_def(eid):
    with open(os.path.join(ROOT, "data", "enemies", eid + ".json"), encoding="utf-8") as f:
        return json.load(f)


class Star(Builder):
    """A Builder whose level is on the Stellar Core and whose theme changes per zone."""

    def __init__(self, level_id, name, first_theme="sylvanox", seed=7001):
        super().__init__(first_theme)
        self.L = Level(level_id, name, 1000, H)
        self.L.planet("noyau")
        self.rng.seed(seed)
        self.use(first_theme)

    CP_NAMES = []

    def checkpoint(self):
        """Checkpoints named after the zones of the plan (C1..C8)."""
        if self.cp < len(self.CP_NAMES):
            label = self.CP_NAMES[self.cp]
            self.cp += 1
            self.ground(self.x, self.x + 8, self.f)
            self.roof(self.x, self.x + 8, self.f - HEADROOM)
            self.L.checkpoint(self.x + 4, self.f, label)
            self.light(self.x + 4, self.f - 5)
            self.x += 8
        else:
            super().checkpoint()

    def use(self, planet):
        self.th = dict(THEMES[planet])
        self.planet = planet
        self.L.crumble_style = self.th["crumble"]

    def zone_sign(self, text):
        self.ground(self.x, self.x + 6, self.f)
        self.roof(self.x, self.x + 6, self.f - HEADROOM)
        self.L.sign(self.x + 3, self.f, text)
        self.L.light(self.x + 3, self.f - 5, SKULL.get(self.planet, "#ffd98a"), 90, flicker=0.1)
        self.x += 6

    def tablets(self, powers, height=4.2, spacing=3.5):
        """A row of power tablets high above the floor (taken on purpose, never by walking)."""
        w = int(len(powers) * spacing + 6)
        self.ground(self.x, self.x + w, self.f)
        self.roof(self.x, self.x + w, self.f - HEADROOM)
        for i, p in enumerate(powers):
            self.L.power(self.x + 3 + i * spacing, self.f - height, p)
        self.x += w

    def ambush(self, waves, pools, w=36, health=True):
        """Closed arena: doors on both sides, `waves` waves drawn from `pools` (kinds)."""
        x, f = self.x, self.f
        door_h = 6
        self.ground(x, x + w, f)
        self.roof(x, x + w, f - HEADROOM)
        self.L.solid(x, f - HEADROOM, x + 1, f - door_h, self.th["accent"], deco=False)
        self.L.solid(x + w - 1, f - HEADROOM, x + w, f - door_h, self.th["accent"], deco=False)
        left = self.L.door(x, f - door_h, door_h, open_=True)
        right = self.L.door(x + w - 1, f - door_h, door_h, open_=False)
        ids = []
        for k in range(waves):
            wave = []
            for j, kind in enumerate(pools[k % len(pools)]):
                gx = 8 + (j * 7 + k * 3) % (w - 14)
                air = enemy_def(kind).get("flying", False)
                wave.append(self.L.enemy(kind, x + gx, f - (6 if air else 0), patrol_tiles=3, face_left=j % 2 == 1,
                                         dormant=True))
            ids.append(",".join(wave))
        zid = self.L.combat(x + 1, f - HEADROOM, x + w - 1, f, doors=left + "," + right, waves=";".join(ids))
        self.L.data["objects"][-1]["props"]["skullColor"] = SKULL.get(self.planet, "#ffd98a")
        if health:
            self.L.orb("health_orb", x + w // 2, f - 1.5)
        self.x = x + w
        return zid

    def boss_arena(self, boss, w=46, head=18, health=True, movers=False, music="", ledges=False):
        x, f = self.x, self.f
        door_h = 8
        self.ground(x, x + w, f)
        self.roof(x, x + w, f - head)
        self.L.solid(x, f - head, x + 1, f - door_h, self.th["accent"], deco=False)
        self.L.solid(x + w - 1, f - head, x + w, f - door_h, self.th["accent"], deco=False)
        left = self.L.door(x, f - door_h, door_h, open_=True)
        right = self.L.door(x + w - 1, f - door_h, door_h, open_=False)
        air = enemy_def(boss).get("flying", False)
        bid = self.L.enemy(boss, x + w - 14, f - (6 if air else 0), patrol_tiles=6, face_left=True, dormant=True, boss=True)
        if movers:
            # The heart of the star: platforms circling around the arena (two pairs in
            # opposite phases), to dodge the Astrarque's lines of fire.
            for i, (px, py, dx, dy, ph) in enumerate([(10, f - 5, 8, 0, 0.0), (w - 18, f - 5, -8, 0, 0.0),
                                                      (16, f - 10, 0, 3, 0.5), (w - 22, f - 10, 0, -3, 0.5)]):
                self.L.moving(x + px, py, 4, dx, dy, speed=55, wait=0.5, phase=ph, oneway=True)
        if ledges:
            # The guardians of Aethra (2.19): ledges above the lava tide and the roots, high
            # ones above the great tide.
            for a, b in ((4, 11), (19, 27), (35, 42)):
                self.L.oneway(x + a, x + b, f - 5, self.th["oneway"])
            for a, b in ((12, 18), (28, 34)):
                self.L.oneway(x + a, x + b, f - 10, self.th["oneway"])
        self.L.combat(x + 1, f - head, x + w - 1, f, doors=left + "," + right, waves=bid, lock=False,
                      music=music or self.L.data.get("bossMusic", ""))
        if movers:
            # The whole heart in view: the Astrarque never strikes from off screen.
            self.L.camzone(x, f - head - 1, x + w, f + 5, "clamp", zoom=0.72)
        self.L.data["objects"][-1]["props"]["announce"] = False
        if health:
            self.L.orb("health_orb", x + 4, f - 1.5)
        self.L.light(x + w // 2, f - head + 4, "#fff1c9" if boss == "astrarque" else self.th["light"], 120, flicker=0.08)
        self.x = x + w
        return bid

    # The giant arena of Singularis (2.33): 96 x 36 tiles inside (about 3.2 x 2.1 screens:
    # the camera follows Nova at its widest instead of showing it all). Heights `h` are in
    # tiles above the floor. Phases 1-4: the whole arena (a crust floor over an empty basin,
    # ledges at h 5, 10, 15, 20, a crown at h 23, three debris platforms parked on a ring);
    # phase 3: the debris start orbiting; phase 5: the crust floor collapses into the basin of
    # plasma (2 damage and a bounce); phase 8: curtains of plasma close in from both sides,
    # then further at the boss's rage. Health orbs appear when phases 3, 5 and 7 start.
    GIANT_W, GIANT_H = 96, 36
    GIANT_BASIN = (22, 74)  # inside columns of the crust floor over the basin
    GIANT_LEDGES = {5: [(6, 8), (26, 8), (62, 8), (82, 8)], 10: [(14, 8), (74, 8)],
                    15: [(4, 8), (26, 8), (62, 8), (84, 8)], 20: [(14, 8), (74, 8)], 23: [(40, 16)]}
    GIANT_ORBIT = (48, 12, 10, 6)  # centre x, h, radii (tiles)
    GIANT_ORBS = [(3, 18, 10), (5, 48, 23), (7, 78, 10)]  # phase, x, h of the ledge it rests on

    def giant_arena(self, boss, music=""):
        L, f = self.L, self.f
        x0 = self.x
        X = x0 + 1                      # first inside column
        W, Hh = self.GIANT_W, self.GIANT_H
        door_h = 8
        ledge = "sanctum_ledge"
        b0, b1 = self.GIANT_BASIN
        # Shell of the arena: roof, walls with their doors, the side floors that stay, the basin.
        self.roof(x0, X + W + 1, f - Hh)
        L.solid(x0, f - Hh, x0 + 1, f - door_h, self.th["accent"], deco=False)
        L.solid(X + W, f - Hh, X + W + 1, f - door_h, self.th["accent"], deco=False)
        left = L.door(x0, f - door_h, door_h, open_=True)
        right = L.door(X + W, f - door_h, door_h, open_=False)
        self.ground(x0, X + b0, f)
        self.ground(X + b1, X + W + 1, f)
        self.ground(X + b0, X + b1, f + 4)
        # The crust floor over the basin: it gives way only to the fight (phase 5).
        crust = []
        for cx in range(b0, b1, 4):
            oid = L.crumble(X + cx, f, 4, delay=0.3, respawn=0.0, style="star")
            L.data["objects"][-1]["props"]["onEvent"] = True
            crust.append(oid)
        # Its plasma (off until the collapse): 2 damage and a bounce out of the basin.
        basin = L.obj("damage_zone", [(X + b0) * T, (f + 2) * T, (b1 - b0) * T, 2 * T], "gameplay",
                      {"damage": 2, "interval": 0.6, "visible": True, "shieldProtects": True, "active": False,
                       "style": "plasma", "bounce": True})
        for h, ls in self.GIANT_LEDGES.items():
            for (lx, lw) in ls:
                L.oneway(X + lx, X + lx + lw, f - h, ledge)
        # Debris on a ring around the middle: parked, then in orbit from phase 3.
        ocx, och, orx, ory = self.GIANT_ORBIT
        debris = []
        for k in range(3):
            oid = L.moving(X + ocx - 2, f - och - 0.5, 4, orx, ory, speed=46, wait=0.0, phase=round(0.25 + k / 3.0, 3),
                           active=False, oneway=True)
            L.data["objects"][-1]["props"]["orbit"] = True
            debris.append(oid)
        # Health orbs hidden on the ledges, one per phase 3, 5 and 7.
        orbs = {}
        for (ph, ox, oh) in self.GIANT_ORBS:
            L.orb("health_orb", X + ox, f - oh - 1.5)
            L.data["objects"][-1]["props"] = {"amount": 4, "hidden": True}
            orbs[ph] = L.data["objects"][-1]["id"]
        # Phase 8: curtains of plasma close in (then further at its rage).
        def curtain(a, b):
            return L.obj("damage_zone", [(X + a) * T, (f - Hh) * T, (b - a) * T, Hh * T], "gameplay",
                         {"damage": 2, "interval": 0.5, "visible": True, "shieldProtects": True, "active": False,
                          "style": "plasma", "bounce": False})
        curtain1 = [curtain(0, 12), curtain(W - 12, W)]
        curtain2 = [curtain(12, 22), curtain(W - 22, W - 12)]
        bid = L.enemy(boss, X + 70, f - 20, patrol_tiles=6, face_left=True, dormant=True, boss=True)
        L.combat(X, f - Hh, X + W, f + 4, doors=left + "," + right, waves=bid, lock=False,
                 music=music or L.data.get("bossMusic", ""))
        L.data["objects"][-1]["props"]["announce"] = False
        for (lx, lh, col) in ((10, 26, "#ffd98a"), (48, 30, "#fff1c9"), (86, 26, "#ffd98a"), (30, 8, "#ff7ad2"),
                              (66, 8, "#ff7ad2")):
            L.light(X + lx, f - lh, col, 160, flicker=0.08)
        # The arena answers the fight.
        plasma = [basin] + curtain1 + curtain2
        L.event(bid, "bossPhase", [{"type": "activate", "target": ",".join(debris)},
                                   {"type": "spawn", "target": orbs[3]},
                                   {"type": "hint", "params": {"text": "Les débris entrent en orbite !", "duration": 3}}],
                params={"phase": 3}, name="Gravité : les débris en orbite")
        L.event(bid, "bossPhase", [{"type": "activate", "target": ",".join(crust)},
                                   {"type": "enable", "target": basin},
                                   {"type": "spawn", "target": orbs[5]},
                                   {"type": "shake", "params": {"amount": 0.8}}],
                params={"phase": 5}, name="Effondrement : le sol cède")
        L.event(bid, "bossPhase", [{"type": "spawn", "target": orbs[7]}], params={"phase": 7}, name="Escorte : un soin")
        L.event(bid, "bossPhase", [{"type": "enable", "target": ",".join(curtain1)},
                                   {"type": "hint", "params": {"text": "Le plasma referme l'arène !", "duration": 3}}],
                params={"phase": 8}, name="Supernova : l'arène se resserre")
        L.event(bid, "bossEnrage", [{"type": "enable", "target": ",".join(curtain2)},
                                    {"type": "shake", "params": {"amount": 0.6}}], name="Rage : l'arène se resserre encore")
        L.event(bid, "bossDefeated", [{"type": "disable", "target": ",".join(plasma)},
                                      {"type": "hint", "params": {"text": "L'étoile morte s'éteint. La sortie est ouverte.",
                                                                 "duration": 4}}],
                delay=1.5, name="Singularis vaincu")
        self.x = X + W + 1
        return bid

    def rooms_of(self, rooms):
        rising = {"climb": 16, "updraft": 14, "glide": 0, "pistons": 12, "vents": 6, "wind": 4}
        falling = {"ice_slope": 8, "pound": 15}
        for room in rooms:
            if self.f - rising.get(room, 0) < 22:
                self.r_drop()
            if self.f + falling.get(room, 0) > 42:
                self.rooms.append({"type": "updraft", "x": self.x, "floor": self.f})
                self.r_updraft()
            self.rooms.append({"type": room, "x": self.x, "floor": self.f, "theme": self.planet})
            getattr(self, "r_" + room)()
        if self.f < 34:
            self.r_drop()

    def finish(self, exit_text):
        L = self.L
        self.ground(self.x, self.x + 16, self.f)
        self.roof(self.x, self.x + 16, self.f - HEADROOM)
        L.sign(self.x + 4, self.f, exit_text)
        L.exit(self.x + 10, self.f)
        self.light(self.x + 10, self.f - 5)
        L.solid(self.x + 16, 0, self.x + 17, H, self.th["floor"])
        width = self.x + 17
        L.data["bounds"] = [0, 0, width * T, H * T]
        L.data["killY"] = (H - 1) * T
        return width

    def opening(self, text):
        L = self.L
        self.start_floor = self.f
        self.ground(0, 14, self.f)
        self.roof(0, 14, self.f - HEADROOM)
        L.solid(0, 0, 1, H, self.th["floor"])
        L.start(4, self.f)
        L.sign(9, self.f, text)
        L.light(6, self.f - 5, "#ffd98a", 90, flicker=0.1)
        self.x = 14

    def cinematics(self, title, subtitle, motto, victory):
        d = self.L.data
        d["intro"] = {"skippable": True, "letterbox": True, "steps": [
            {"type": "fade", "duration": 0.01, "params": {"to": 1}},
            {"type": "cut", "duration": 0.01, "params": {"x": 6 * T, "y": (self.start_floor - 6) * T}},
            {"type": "fade", "duration": 1.4, "wait": False, "params": {"to": 0}},
            {"type": "text", "duration": 3.5, "wait": False, "params": {"text": title, "subtitle": subtitle}},
            {"type": "camera", "duration": 4.0, "params": {"x": 60 * T, "y": (self.start_floor - 6) * T, "ease": "inOutSine"}},
            {"type": "text", "duration": 3.0, "wait": False, "params": {"text": motto, "style": "caption"}},
            {"type": "camera", "duration": 2.5, "params": {"start": True, "ease": "inOutCubic"}},
        ]}
        d["outro"] = {"skippable": True, "letterbox": True, "steps": [
            {"type": "zoom", "duration": 1.4, "wait": False, "params": {"zoom": 1.4}},
            {"type": "camera", "duration": 1.4, "params": {"player": True}},
            {"type": "text", "duration": 3.5, "wait": False, "params": {"text": victory, "subtitle": "Noyau stellaire — " + d["name"]}},
            {"type": "playerAnim", "duration": 2.2, "params": {"anim": "victory"}},
            {"type": "fade", "duration": 0.9, "params": {"to": 1}},
        ]}


# ---------------------------------------------------------------- the boss rush
def boss_rush():
    b = Star("noyau_01", "L'Arène des Échos", "sylvanox", seed=7101)
    L, d = b.L, b.L.data
    d["description"] = ("Les seize gardiens du système reviennent, l'un après l'autre, en échos de lumière. "
                        "Aucun soin, aucun checkpoint : une seule vie pour les vaincre tous.")
    d["difficulty"] = 5
    # EXTRÊME: one life (the challenge mode below allows no death) and a race online.
    d["extreme"] = True
    b.f = 40
    b.opening("L'Arène des Échos : seize gardiens à la suite. Ni soin ni checkpoint, une seule vie. "
              "Choisissez vos quatre pouvoirs.")
    # Combat powers to choose from (4 carried; the start set is in levelkit.START_POWERS).
    b.tablets(["time_slow", "plasma_bomb", "impulse", "ground_pound", "triple_jump", "glide"])
    n = 0
    for planet, bosses in ECHO_ORDER:
        b.use(planet)
        for boss in bosses:
            n += 1
            name = enemy_def(boss)["name"]
            b.zone_sign("Écho %d / %d : %s." % (n, ECHOES, name))
            b.rooms.append({"type": "echo", "x": b.x, "floor": b.f, "boss": "echo_" + boss})
            b.boss_arena("echo_" + boss, health=False, ledges=planet == "aethra")
            # A breath between two fights: energy only.
            b.ground(b.x, b.x + 6, b.f)
            b.roof(b.x, b.x + 6, b.f - HEADROOM)
            L.orb("energy_orb", b.x + 3, b.f - 1.5)
            b.x += 6
    b.use("pyralis")
    width = b.finish("Les seize échos se sont tus.")
    # One life: the challenge mode ends the run at the first fall.
    ref = ECHOES * 75
    d["timer"] = {"mode": "challenge", "limit": 3600, "onExpire": "gameover", "referenceTime": ref, "challengeMaxDeaths": 0}
    d["medals"] = {"bronze": {}, "silver": {"maxTime": int(ref * 1.6)}, "gold": {"maxTime": int(ref * 1.2)},
                   "platinum": {"maxTime": ref, "maxDeaths": 0}}
    b.cinematics("L'ARÈNE DES ÉCHOS", "Noyau stellaire", "Seize gardiens, aucun soin, une seule vie.", "ÉCHOS VAINCUS")
    # 2.19: sixteen guardians (a new version of the level: its rankings start again).
    d["metadata"] = {"generator": "tools/levelgen/stellar_core.py", "revision": 9, "rooms": b.rooms, "noHealing": True}
    L.save(os.path.join(ROOT, "data/levels/noyau_01.json"))
    return width


# ---------------------------------------------------------------- the final level
def final_level():
    b = Star("noyau_02", "Le Cœur de l'Étoile", "sylvanox", seed=7201)
    b.CP_NAMES = ["Le Seuil", "Écho de Sylvanox : la Sentinelle", "Écho de Glacior", "Écho de Glacior : l'Hydre",
                  "Écho de Pyralis", "Écho de Pyralis : le Forgeron", "La Couronne", "Le Cœur"]
    L, d = b.L, b.L.data
    d["description"] = ("Au centre du système, l'étoile s'ouvre. Ses couloirs rejouent les trois planètes avant "
                        "son cœur, où l'Astrarque porte les échos de tous les gardiens.")
    d["difficulty"] = 5
    b.f = 40
    # Zone 0 · Le Seuil: choose four powers among eight.
    b.opening("Le Seuil de l'étoile. Huit pouvoirs, vous en gardez quatre : celui que vous laissez reste ici.")
    b.tablets(["impulse", "teleport", "glide", "triple_jump", "ground_pound", "charge_shot", "time_slow", "plasma_bomb"])
    b.checkpoint()
    # Zone 1 · Écho de Sylvanox
    b.use("sylvanox")
    b.zone_sign("Zone 1 — Écho de Sylvanox.")
    b.rooms_of(["metronome", "anchors", "dash_walls"])
    b.ambush(3, [["glowmite", "stalker", "glowmite"], ["nightwing", "wisp", "stalker"], ["sentinel", "bulwark", "golem"]])
    b.checkpoint()
    b.boss_arena("echo_prime_sentinel")
    # Zone 2 · Écho de Glacior
    b.use("glacior")
    b.checkpoint()
    b.zone_sign("Zone 2 — Écho de Glacior.")
    b.rooms_of(["ice_slope", "frail_bridges", "wind"])
    b.ambush(2, [["cryo_drone", "ice_golem", "frost_mite"], ["frost_wing", "cryo_drone", "snow_stalker"]])
    b.checkpoint()
    b.boss_arena("echo_frost_hydra")
    # Zone 3 · Écho de Pyralis
    b.use("pyralis")
    b.checkpoint()
    b.zone_sign("Zone 3 — Écho de Pyralis.")
    b.rooms_of(["rafts", "vents", "pistons"])
    b.ambush(2, [["forge_bomber", "magma_golem", "ember_mite"], ["ash_wing", "forge_bomber", "lava_stalker"]])
    b.checkpoint()
    b.boss_arena("echo_forge_core")
    # Zone 4 · La Couronne: every mechanic mixed, two ambushes in a row.
    b.use("glacior")
    b.checkpoint()
    b.zone_sign("Zone 4 — La Couronne : les trois mondes réunis.")
    b.rooms_of(["frail_bridges_fast"])
    b.use("pyralis")
    b.rooms_of(["foundry"])
    b.use("sylvanox")
    b.rooms_of(["metronome_fast"])
    b.use("pyralis")
    b.ambush(2, [["magma_golem", "forge_bulwark", "ember_mite"], ["golem", "ice_golem", "magma_golem"]], health=False)
    b.use("glacior")
    b.ambush(2, [["frost_wing", "nightwing", "ash_wing"], ["frost_bulwark", "bulwark", "forge_bulwark"]])
    # Zone 5 · Le Cœur
    b.use("pyralis")
    b.checkpoint()
    b.zone_sign("Zone 5 — Le Cœur. L'Astrarque attend.")
    b.rooms.append({"type": "boss", "x": b.x, "floor": b.f, "boss": "astrarque"})
    b.boss_arena("astrarque", w=56, head=17, movers=True)
    width = b.finish("L'étoile s'apaise. Le système Kepler-Voss est libre.")
    ref = int(round(width * T / 125.0 + 30 * 4))
    d["timer"] = {"mode": "unlimited", "referenceTime": ref}
    d["medals"] = {"bronze": {}, "silver": {"maxTime": ref * 2}, "gold": {"maxTime": int(ref * 1.35)},
                   "platinum": {"maxTime": ref, "maxDeaths": 0}}
    b.cinematics("LE CŒUR DE L'ÉTOILE", "Noyau stellaire", "Les trois mondes, une dernière fois.", "L'ÉTOILE EST LIBRE")
    d["metadata"] = {"generator": "tools/levelgen/stellar_core.py", "revision": 7, "rooms": b.rooms}
    L.save(os.path.join(ROOT, "data/levels/noyau_02.json"))
    return width


# ---------------------------------------------------------------- the dying star (2.33)
def dying_star():
    b = Star("noyau_03", "Le Cœur d'une Étoile Mourante", "pyralis", seed=7301)
    L, d = b.L, b.L.data
    d["description"] = ("Au fond de l'étoile, un cœur mort s'est éveillé. Deux minutes d'épreuves sans pardon, "
                        "puis Singularis et ses huit phases. Une seule vie, aucun checkpoint.")
    d["difficulty"] = 5
    # EXTRÊME: one life (the challenge mode below allows no death) and a race online.
    d["extreme"] = True
    b.f = 40
    b.opening("Le Cœur d'une Étoile Mourante : une seule vie, aucun checkpoint. Singularis attend au bout.")
    # The crust of the dying star: its hardest rooms, one after the other (dash, grapple, glide
    # and the charged shot are carried from the start: levelkit.START_POWERS).
    b.zone_sign("La croûte en fusion.")
    b.rooms_of(["metronome_fast", "dash_walls", "vents", "anchors"])
    b.zone_sign("Les veines de l'étoile.")
    b.rooms_of(["pistons", "frail_bridges_fast", "foundry", "lasers"])
    # Before the heart: the powers of the fight (four carried; what is left stays here).
    b.use("aethra")
    b.th.update(floor="obsidian", roof="obsidian", accent="obsidian")  # the dead star's crust
    b.zone_sign("Le seuil du cœur. Choisissez les pouvoirs du combat : celui que vous laissez reste ici.")
    b.tablets(["time_slow", "plasma_bomb", "impulse", "shield", "triple_jump", "wave"])
    platforming = b.x
    b.zone_sign("Singularis, le Cœur Effondré.")
    b.rooms.append({"type": "boss", "x": b.x, "floor": b.f, "boss": "singularis"})
    b.giant_arena("singularis", music="music/singularis_boss.ogg")
    width = b.finish("L'étoile morte s'est éteinte. Le système respire.")
    # Reference: the rooms at a good pace, then about six minutes of fight.
    ref = int(round(platforming * T / 125.0 + 360))
    d["timer"] = {"mode": "challenge", "limit": 3600, "onExpire": "gameover", "referenceTime": ref, "challengeMaxDeaths": 0}
    d["medals"] = {"bronze": {}, "silver": {"maxTime": int(ref * 1.6)}, "gold": {"maxTime": int(ref * 1.2)},
                   "platinum": {"maxTime": ref, "maxDeaths": 0}}
    b.cinematics("LE CŒUR D'UNE ÉTOILE MOURANTE", "Noyau stellaire", "Une seule vie. Une étoile morte. Huit phases.",
                 "L'ÉTOILE S'ÉTEINT")
    d["metadata"] = {"generator": "tools/levelgen/stellar_core.py", "revision": 1, "rooms": b.rooms, "noHealing": True}
    L.save(os.path.join(ROOT, "data/levels/noyau_03.json"))
    return width


if __name__ == "__main__":
    print("noyau_01 :", boss_rush(), "tuiles")
    print("noyau_02 :", final_level(), "tuiles")
    print("noyau_03 :", dying_star(), "tuiles")
