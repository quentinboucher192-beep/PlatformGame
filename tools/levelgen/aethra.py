#!/usr/bin/env python3
"""Builds the four levels of Aethra (version 2.19), the planet of the sanctuaries: islands
floating over an ocean of nebula, where roots, frost, magma and the void mingle. It opens
once Sylvanox, Glacior and Pyralis are completed. Each level is a whole level (rooms of
platforming built from the calibrated rooms of the trials, enemies of the element it echoes,
shards, a secret with a relic, checkpoints, medals) ending with one of the new guardians;
the tablet of a new power waits at the start:

  data/levels/aethra_01.json  "Le Bosquet Englouti"         roots and spores, Lame boomerang, Mycélia
  data/levels/aethra_02.json  "Le Lac aux Reflets"          frost, Nova de givre, Aurorax
  data/levels/aethra_03.json  "La Caldeira"                 magma, Leurre holographique, Pyrogyre
  data/levels/aethra_04.json  "L'Horizon des événements"   the void, Balise de rappel, Nyxara

Every boss arena is shaped for its guardian's attacks (ledges above the lava tide, room for
the cage and for the doubles, open sky for the eclipse ring). The tiles, backgrounds and
music of Aethra come from tools/assetgen: a tile set missing when this runs is replaced by
the element's own tiles (run it again once the art is there), then
  CosmicPlatformer --seal-levels tools/levelgen/seals, --plan-stations tools/levelgen/stations,
  python3 tools/levelgen/aethra.py again, and --rate-levels data/levels/ratings.json.
Run from the repository root:  python3 tools/levelgen/aethra.py
"""
import os
import random
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from challenge import Builder, H, HEADROOM  # noqa: E402
from levelkit import Level, ROOT, T  # noqa: E402


def tile(name, fallback):
    """A tile set of Aethra if the art provides it, else the element's own."""
    if os.path.exists(os.path.join(ROOT, "assets", "textures", "tiles", name + ".json")):
        return name
    print("  (tuiles %s absentes : %s à la place)" % (name, fallback))
    return fallback


STONE = "aethra_stone"  # the islands' rock (tools/assetgen/environment)

LEVELS = {
    "aethra_01": {
        "id": "aethra_01", "name": "Le Bosquet Englouti", "seed": 1901, "element": "sylvanox",
        "power": "boomerang", "boss": "mycelia", "title": "AETHRA", "light": "#b48cff",
        "description": ("Sur la première île-sanctuaire, un bosquet de Sylvanox s'est enfoncé dans la nébuleuse. "
                        "Ses racines vivent encore, et la Mère-Racine les entend toutes."),
        "motto": "Les racines se souviennent de chaque pas.",
        "sign": "Une tablette des sanctuaires : la Lame boomerang part, frappe et revient.",
        "floor": ("ruin_stone",), "roof": "root", "accent": "ancient_metal", "oneway": "root_branch", "crumble": "ruins",
        "hazard": "acid",
        "ground": ["glowmite", "stalker", "gloop", "bulwark"], "air": ["nightwing", "wisp"],
        "turret": ["sporeflower", "sentinel"],
        "decor_ground": ["mushroom_cluster", "glow_fern", "glow_flowers", "mushroom_small"],
        "decor_hang": ["hanging_vine_short", "hanging_vine_long", "roots"],
        "rooms": ["gaps", "garden", "secret", "cp", "anchors", "gauntlet", "cp", "crumble", "climb", "garden", "cp",
                  "dash_walls", "secret_heart", "cp"],
        "relics": ["Graine de la Mère-Racine", "Spore d'or"], "victory": "LE BOSQUET RESPIRE",
    },
    "aethra_02": {
        "id": "aethra_02", "name": "Le Lac aux Reflets", "seed": 1902, "element": "glacior",
        "power": "frost_nova", "boss": "aurorax", "title": "AETHRA", "light": "#9fe8ff",
        "description": ("Un lac de Glacior flotte sous l'aurore d'Aethra. Ses rives de cristal renvoient chaque geste, "
                        "et sur la glace, Aurorax se bat avec ses propres reflets."),
        "motto": "Sur le lac, chaque reflet peut mordre.",
        "sign": "Une tablette des sanctuaires : la Nova de givre fige tout ce qui vous entoure.",
        "floor": ("snow_rock",), "roof": "glacier_ice", "accent": "frost_metal", "oneway": "ice_ledge", "crumble": "ice",
        "hazard": None,
        "ground": ["frost_mite", "snow_stalker", "ice_golem", "ice_gloop"], "air": ["frost_wisp", "frost_wing"],
        "turret": ["cryo_drone", "frost_bloom"],
        "decor_ground": ["ice_crystal", "frost_flower", "snow_rock_small", "ice_crystal_big"],
        "decor_hang": ["icicles", "icicles_long"],
        "rooms": ["garden", "ice_slope", "frail_bridges", "cp", "under_ice", "secret", "gauntlet", "cp", "garden", "wind",
                  "cp", "ice_peaks", "secret_heart", "cp"],
        "relics": ["Éclat du premier reflet", "Larme d'aurore"], "victory": "LE LAC S'APAISE",
    },
    "aethra_03": {
        "id": "aethra_03", "name": "La Caldeira", "seed": 1903, "element": "pyralis",
        "power": "decoy", "boss": "pyrogyre", "title": "AETHRA", "light": "#ffb13b",
        "description": ("Au cœur d'une île de basalte, une caldeira bouillonne de magma venu de Pyralis. "
                        "Au-dessus d'elle tourne l'Anneau solaire, et la lave monte quand il le veut."),
        "motto": "Ici, la lave obéit à un soleil.",
        "sign": "Une tablette des sanctuaires : le Leurre holographique attire les coups à votre place.",
        "floor": ("basalt",), "roof": "basalt", "accent": "forge_metal", "oneway": "chain_grate", "crumble": "basalt",
        "hazard": "lava",
        "ground": ["ember_mite", "lava_stalker", "magma_golem", "magma_gloop"], "air": ["fire_wisp", "ash_wing"],
        "turret": ["forge_drone", "cinder_bloom"],
        "decor_ground": ["slag_pile", "ember_crystal", "ash_rock_small", "forge_brazier"],
        "decor_hang": ["hanging_chain", "hanging_chain_long"],
        "rooms": ["garden", "rafts", "cp", "vents", "gauntlet", "secret", "cp", "garden", "pistons", "cp", "foundry",
                  "secret_heart", "cp"],
        "relics": ["Cendre de l'œil solaire", "Perle de basalte"], "victory": "LA CALDEIRA S'ÉTEINT",
    },
    "aethra_04": {
        "id": "aethra_04", "name": "L'Horizon des événements", "seed": 1904, "element": "noyau",
        "power": "recall", "boss": "nyxara", "title": "AETHRA", "light": "#ff7696",
        "description": ("Au bord d'Aethra, l'île la plus lointaine glisse vers un trou noir. Lasers, ancrages et "
                        "leviers des Anciens tiennent encore le passage : au bout attend la Dévoreuse d'étoiles."),
        "motto": "Au bord du vide, la lumière ne revient pas.",
        "sign": "Une tablette des sanctuaires : la Balise de rappel vous ramène là où vous l'avez posée.",
        "floor": ("obsidian",), "roof": "ancient_metal", "accent": "crystal", "oneway": "tech_ledge", "crumble": "ruins",
        "hazard": None,
        "ground": ["stalker", "bulwark", "golem", "glowmite"], "air": ["wisp", "nightwing", "bomber"],
        "turret": ["sentinel", "cryo_drone"],
        "decor_ground": ["crystal_cluster", "holo_glyph", "rock_small", "crystal_big"],
        "decor_hang": ["hanging_chain", "hanging_vine_short"],
        "rooms": ["garden", "metronome", "cp", "movers", "lasers", "secret", "grapple", "cp", "garden", "lever_hall", "cp",
                  "dash_gap", "gauntlet", "secret_heart", "cp"],
        "relics": ["Fragment d'horizon", "Poussière d'étoile éteinte"], "victory": "L'HORIZON SE REFERME",
    },
}


class Sanctuary(Builder):
    """A Builder whose level is on Aethra, themed by one element."""

    def __init__(self, spec):  # noqa: the Builder's own set-up is for the planets of the trials
        th = dict(spec)
        th["floor"] = tile(STONE, spec["floor"][0])
        self.th = th
        self.planet = "aethra"
        self.rng = random.Random(th["seed"])
        self.L = Level(th["id"], th["name"], 1000, H)
        self.L.planet("aethra")
        self.L.crumble_style = th["crumble"]
        self.L.data["bossMusic"] = "music/%s_boss.ogg" % th["boss"]
        self.x = 0
        self.f = 38
        self.cp = 0
        self.waves = 0
        self.rooms = []
        self.secrets = 0

    def checkpoint(self):
        names = {"aethra_01": ["Racines", "Clairière", "Sous-bois", "Porte du bosquet"],
                 "aethra_02": ["Rive", "Miroir", "Vents", "Porte du lac"],
                 "aethra_03": ["Basalte", "Cheminées", "Pistons", "Porte de la caldeira"],
                 "aethra_04": ["Seuil", "Ancrages", "Leviers", "Porte de l'horizon"]}
        labels = names.get(self.th["id"], [])
        self.L.shards_arc(self.x + 1, self.x + 7, self.f - 2, 2, 4)
        if self.cp < len(labels):
            label = labels[self.cp]
            self.cp += 1
            self.ground(self.x, self.x + 8, self.f)
            self.roof(self.x, self.x + 8, self.f - HEADROOM)
            self.L.checkpoint(self.x + 4, self.f, label)
            self.light(self.x + 4, self.f - 5)
            self.x += 8
        else:
            super().checkpoint()

    # ------------------------------------------------------------- rooms
    def r_garden(self):
        """A breath between two trials: ledges and shards above a quiet stretch, two guards."""
        x, f = self.x, self.f
        w = 22
        self.ground(x, x + w, f)
        self.roof(x, x + w, f - HEADROOM)
        self.L.oneway(x + 4, x + 9, f - 4, self.th["oneway"])
        self.L.oneway(x + 13, x + 18, f - 7, self.th["oneway"])
        self.L.shards_arc(x + 4, x + 9, f - 6, 2, 4)
        self.L.shards_arc(x + 13, x + 18, f - 9, 2, 4)
        self.L.shards_line(x + 2, f - 1.5, x + 20, f - 1.5, 5)
        self.foe(x + 11, f, "ground", 3)
        self.foe(x + 16, f - 11, "air", 3)
        self.L.orb("energy_orb", x + 15.5, f - 8.5)
        self.flora(x, x + w, f)
        self.x = x + w

    def r_secret_heart(self):
        self.r_secret(heart=True)

    def r_secret(self, heart=False):
        """A hollow in the vault, hidden behind hanging roots: two ledges climb to it, a relic
        (and, in the second one, a crystal heart) waits inside."""
        x, f = self.x, self.f
        w = 16
        top = f - HEADROOM
        self.ground(x, x + w, f)
        self.roof(x, x + 5, top)
        self.roof(x + 11, x + w, top)
        self.L.solid(x + 5, 0, x + 11, top - 5, self.th["roof"], deco=False)
        self.L.oneway(x + 3, x + 7, f - 5, self.th["oneway"])
        self.L.oneway(x + 7, x + 11, f - 10, self.th["oneway"])
        self.L.oneway(x + 6, x + 10, top - 1, self.th["oneway"])
        self.L.relic(x + (7 if heart else 8), top - 2.5, self.th["relics"][min(self.secrets, 1)])
        if heart:
            self.L.heart(x + 9, top - 2.5)
        self.L.shards_line(x + 4, f - 7, x + 10, f - 12, 3)
        self.L.secret(x + 5, top - 5, x + 11, top, "Un sanctuaire caché dans la voûte !" if not heart
                      else "Un cœur de cristal dormait dans la voûte !")
        for gx in (5.6, 7.2, 8.8, 10.4):
            self.L.decor(self.rng.choice(self.th["decor_hang"]), gx, top - 5, layer="fg_decor", sway=1.0)
        self.foe(x + 13, f, "ground", 2)
        self.flora(x, x + w, f)
        self.secrets += 1
        self.x = x + w

    def opening(self):
        """Start: the tablet of the sanctuary's new power, then the way on."""
        L, th = self.L, self.th
        self.start_floor = self.f
        self.ground(0, 18, self.f)
        self.roof(0, 18, self.f - HEADROOM)
        L.solid(0, 0, 1, H, th["floor"])
        L.start(4, self.f)
        L.sign(8, self.f, th["sign"])
        L.power(12, self.f - 1.5, th["power"], th["sign"])
        L.light(12, self.f - 4, "#ffd65a", 90, flicker=0.08)
        self.light(6, self.f - 5)
        self.x = 18

    # ----------------------------------------------------------- arenas
    def arena(self):
        """The guardian's arena, shaped for its attacks."""
        boss = self.th["boss"]
        x, f = self.x, self.f
        w, head, door_h = {"mycelia": (48, 18, 8), "aurorax": (52, 18, 8), "pyrogyre": (50, 20, 8), "nyxara": (52, 20, 8)}[boss]
        L = self.L
        self.ground(x, x + w, f)
        self.roof(x, x + w, f - head)
        L.solid(x, f - head, x + 1, f - door_h, self.th["accent"], deco=False)
        L.solid(x + w - 1, f - head, x + w, f - door_h, self.th["accent"], deco=False)
        left = L.door(x, f - door_h, door_h, open_=True)
        right = L.door(x + w - 1, f - door_h, door_h, open_=False)
        ow = self.th["oneway"]
        if boss == "mycelia":
            # Two branches above the roots' reach (the spike rises 3 tiles).
            L.oneway(x + 8, x + 14, f - 5, ow)
            L.oneway(x + 34, x + 40, f - 5, ow)
        elif boss == "aurorax":
            # The lake: wide and flat for the doubles; two crystal ledges to wait out the ice.
            L.oneway(x + 6, x + 12, f - 6, ow)
            L.oneway(x + 40, x + 46, f - 6, ow)
            L.oneway(x + 22, x + 30, f - 10, ow)
        elif boss == "pyrogyre":
            # Low ledges above the tide (4 tiles), high ones above the great tide (7.5 tiles).
            for a, b in ((4, 11), (21, 29), (39, 46)):
                L.oneway(x + a, x + b, f - 5, ow)
            for a, b in ((12, 19), (31, 38)):
                L.oneway(x + a, x + b, f - 10, ow)
        else:
            # Floating slabs at the event horizon: room to see the ring's opening.
            L.oneway(x + 7, x + 13, f - 6, ow)
            L.oneway(x + 39, x + 45, f - 6, ow)
            L.oneway(x + 22, x + 30, f - 11, ow)
        flying = boss in ("pyrogyre", "nyxara")
        bid = L.enemy(boss, x + w - 14, f - (7 if flying else 0), patrol_tiles=6, face_left=True, dormant=True, boss=True)
        L.combat(x + 1, f - head, x + w - 1, f, doors=left + "," + right, waves=bid, lock=False, music=L.data["bossMusic"])
        L.data["objects"][-1]["props"]["announce"] = False
        L.orb("health_orb", x + 4, f - 1.5)
        L.orb("energy_orb", x + w // 2, f - 1.5)
        L.light(x + w // 2, f - head + 4, self.th["light"], 140, flicker=0.06)
        L.event(bid, "bossPhase", [{"type": "shake", "params": {"amount": 0.45}}], once=False)
        self.x = x + w
        return bid

    def finish(self):
        L, th = self.L, self.th
        self.ground(self.x, self.x + 16, self.f)
        self.roof(self.x, self.x + 16, self.f - HEADROOM)
        L.exit(self.x + 10, self.f)
        self.light(self.x + 10, self.f - 5)
        L.solid(self.x + 16, 0, self.x + 17, H, th["floor"])
        width = self.x + 17
        L.data["bounds"] = [0, 0, width * T, H * T]
        return width

    def cinematics(self, width):
        th, d = self.th, self.L.data
        d["intro"] = {"skippable": True, "letterbox": True, "steps": [
            {"type": "fade", "duration": 0.01, "params": {"to": 1}},
            {"type": "cut", "duration": 0.01, "params": {"x": 6 * T, "y": (self.start_floor - 6) * T}},
            {"type": "fade", "duration": 1.4, "wait": False, "params": {"to": 0}},
            {"type": "text", "duration": 3.5, "wait": False, "params": {"text": th["title"], "subtitle": th["name"]}},
            {"type": "camera", "duration": 4.0, "params": {"x": 60 * T, "y": (self.start_floor - 6) * T, "ease": "inOutSine"}},
            {"type": "text", "duration": 3.0, "wait": False, "params": {"text": th["motto"], "style": "caption"}},
            {"type": "camera", "duration": 2.5, "params": {"start": True, "ease": "inOutCubic"}},
        ]}
        d["outro"] = {"skippable": True, "letterbox": True, "steps": [
            {"type": "zoom", "duration": 1.4, "wait": False, "params": {"zoom": 1.4}},
            {"type": "camera", "duration": 1.4, "params": {"player": True}},
            {"type": "text", "duration": 3.5, "wait": False, "params": {"text": th["victory"], "subtitle": "Aethra — " + th["name"]}},
            {"type": "playerAnim", "duration": 2.2, "params": {"anim": "victory"}},
            {"type": "fade", "duration": 0.9, "params": {"to": 1}},
        ]}

    def build(self):
        th, L = self.th, self.L
        d = L.data
        d["description"] = th["description"]
        d["difficulty"] = 4 if th["id"] != "aethra_04" else 5
        d["killY"] = (H - 1) * T
        d["cameraZoom"] = 1.0
        self.f = 38
        self.opening()
        rising = {"climb": 16, "updraft": 14, "glide": 0, "pistons": 12, "vents": 6, "wind": 4}
        falling = {"ice_slope": 8, "pound": 15}
        for room in th["rooms"]:
            if room == "cp":
                self.checkpoint()
                continue
            if self.f - rising.get(room, 0) < 22:
                self.r_drop()
            if self.f + falling.get(room, 0) > 42:
                self.rooms.append({"type": "updraft", "x": self.x, "floor": self.f})
                self.r_updraft()
            self.rooms.append({"type": room, "x": self.x, "floor": self.f})
            getattr(self, "r_" + room)()
        if self.f < 30:
            self.r_drop()
        self.rooms.append({"type": "boss", "x": self.x, "floor": self.f, "boss": th["boss"]})
        bid = self.arena()
        L.event(bid, "bossDefeated", [{"type": "hint", "params": {
            "text": "Le gardien du sanctuaire est vaincu : le portail s'éveille.", "duration": 4}}], delay=1.5)
        width = self.finish()
        rooms = [r for r in th["rooms"] if r != "cp"]
        ref = int(round(width * T / 125.0 + len(rooms) * 4 + 60))
        d["timer"] = {"mode": "limited", "limit": max(900, ref * 4), "onExpire": "gameover", "referenceTime": ref,
                      "challengeMaxDeaths": 0}
        d["medals"] = {"bronze": {}, "silver": {"maxTime": ref * 2}, "gold": {"maxTime": int(ref * 1.35)},
                       "platinum": {"maxTime": ref, "maxDeaths": 0}}
        L.event("level", "levelStart", [{"type": "hint", "params": {"text": "Prenez la tablette du sanctuaire.", "duration": 4}}])
        self.cinematics(width)
        d["metadata"] = {"generator": "tools/levelgen/aethra.py", "revision": 8, "rooms": self.rooms}
        L.save(os.path.join(ROOT, "data/levels/%s.json" % th["id"]))
        return width


def main():
    for lid in sorted(LEVELS):
        print(lid, ":", Sanctuary(LEVELS[lid]).build(), "tuiles")


if __name__ == "__main__":
    main()
