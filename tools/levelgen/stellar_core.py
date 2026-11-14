#!/usr/bin/env python3
"""Builds the two levels of the Stellar Core (Noyau stellaire, version 2.7), the star at the
centre of the system, opened once Sylvanox, Glacior and Pyralis are completed:

  data/levels/noyau_01.json  "L'Arène des Échos"     the twelve guardians in a row (boss rush):
                                                       no healing, no checkpoint, one life
  data/levels/noyau_02.json  "Le Cœur de l'Étoile"   the final level (plan: maquette
                                                       NiveauFinal): echoes of the three planets,
                                                       5 ambushes, 4 bosses, the Astrarque last

The rooms are the calibrated rooms of the trials (challenge.py), built with the materials and
the enemies of the planet each zone echoes. Run from the repository root after
stellar_bosses.py:  python3 tools/levelgen/stellar_core.py
"""
import json
import os

from challenge import Builder, H, HEADROOM, THEMES
from levelkit import Level, ROOT, T

ECHO_ORDER = [("sylvanox", ["thornwarden", "spore_queen", "prime_sentinel", "archon_catacombs"]),
              ("glacior", ["cryowyrm", "frost_hydra", "glaciarch", "howling_wyrm"]),
              ("pyralis", ["magmaroth", "ignivore", "forge_core", "crucible_heart"])]
SKULL = {"sylvanox": "#39f0d8", "glacior": "#9fe8ff", "pyralis": "#ff8a5c", "noyau": "#ffd98a"}


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

    def boss_arena(self, boss, w=46, head=18, health=True, movers=False, music=""):
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
    d["description"] = ("Les douze gardiens du système reviennent, l'un après l'autre, en échos de lumière. "
                        "Aucun soin, aucun checkpoint : une seule vie pour les vaincre tous.")
    d["difficulty"] = 5
    b.f = 40
    b.opening("L'Arène des Échos : douze gardiens à la suite. Ni soin ni checkpoint, une seule vie. "
              "Choisissez vos quatre pouvoirs.")
    # Combat powers to choose from (4 carried; the start set is in levelkit.START_POWERS).
    b.tablets(["time_slow", "plasma_bomb", "impulse", "ground_pound", "triple_jump", "glide"])
    n = 0
    for planet, bosses in ECHO_ORDER:
        b.use(planet)
        for boss in bosses:
            n += 1
            name = enemy_def(boss)["name"]
            b.zone_sign("Écho %d / 12 : %s." % (n, name))
            b.rooms.append({"type": "echo", "x": b.x, "floor": b.f, "boss": "echo_" + boss})
            b.boss_arena("echo_" + boss, health=False)
            # A breath between two fights: energy only.
            b.ground(b.x, b.x + 6, b.f)
            b.roof(b.x, b.x + 6, b.f - HEADROOM)
            L.orb("energy_orb", b.x + 3, b.f - 1.5)
            b.x += 6
    b.use("pyralis")
    width = b.finish("Les douze échos se sont tus.")
    # One life: the challenge mode ends the run at the first fall.
    ref = 12 * 75
    d["timer"] = {"mode": "challenge", "limit": 3600, "onExpire": "gameover", "referenceTime": ref, "challengeMaxDeaths": 0}
    d["medals"] = {"bronze": {}, "silver": {"maxTime": int(ref * 1.6)}, "gold": {"maxTime": int(ref * 1.2)},
                   "platinum": {"maxTime": ref, "maxDeaths": 0}}
    b.cinematics("L'ARÈNE DES ÉCHOS", "Noyau stellaire", "Douze gardiens, aucun soin, une seule vie.", "ÉCHOS VAINCUS")
    d["metadata"] = {"generator": "tools/levelgen/stellar_core.py", "revision": 7, "rooms": b.rooms, "noHealing": True}
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


if __name__ == "__main__":
    print("noyau_01 :", boss_rush(), "tuiles")
    print("noyau_02 :", final_level(), "tuiles")
