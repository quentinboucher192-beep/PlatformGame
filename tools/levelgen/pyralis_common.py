"""Shared helpers for the Pyralis levels (volcanic forge world).

Builds on levelkit: terrain shortcuts sunk into the lava sea, dense volcanic/forge decoration,
lava falls (decor + hazard), updraft vents, the standard boss arena and the cinematics.
Coordinates are in tiles (16 px), y grows downwards.
"""
import random

from levelkit import Level, T  # noqa: F401  (re-exported for the level scripts)

LAVA_GLOW = "#ff6a2a"
FORGE_GLOW = "#ff8a3d"
EMBER_GLOW = "#ffb347"
OBSIDIAN_GLOW = "#b04cff"

ROCK_SMALL = ["ash_rock_small", "ash_rock_small", "fire_plant", "ember_crystal", "slag_pile", "ash_rock_big"]
ROCK_TALL = ["basalt_column", "basalt_column_broken", "obsidian_spire", "skull_rock"]
FORGE_SMALL = ["forge_anvil", "slag_pile", "ash_rock_small", "steam_pipe", "gear_wreck"]


class Pyra:
    """Per-level helper: keeps the level, the lava height and a deterministic RNG."""

    def __init__(self, L, lava_y, seed):
        self.L = L
        self.lava = lava_y
        self.rng = random.Random(seed)
        random.seed(seed)

    # ----------------------------------------------------------------- setup
    def base(self, W, H, description, difficulty, start_powers, limit, ref, medals):
        L = self.L
        L.planet("pyralis")
        d = L.data
        d["description"] = description
        d["difficulty"] = difficulty
        d["startPowers"] = list(start_powers)
        d["killY"] = (self.lava + 3) * T
        d["timer"] = {"mode": "limited", "limit": limit, "onExpire": "gameover", "referenceTime": ref,
                      "challengeMaxDeaths": 0}
        bronze, silver, gold, plat = medals
        d["medals"] = {"bronze": {}, "silver": {"maxTime": silver}, "gold": {"maxTime": gold},
                       "platinum": {"maxTime": plat, "maxDeaths": 0}}
        # The lava sea under everything.
        L.liquid(0, W, self.lava, H, "lava")
        for x in range(0, W, 60):
            L.emitter(x, self.lava - 6, min(W, x + 60), self.lava, "embers_rising", 3)
        for x in range(0, W, 80):
            L.emitter(x, self.lava - 1, min(W, x + 80), self.lava + 1, "lava_bubbles", 3)
        for x in range(0, W, 70):
            L.emitter(x, 4, min(W, x + 70), 30, "ash_fall", 1.5)

    # --------------------------------------------------------------- terrain
    def rock(self, x0, y0, x1, y1=None, mat="basalt", deco=True):
        """Solid block; without y1 the block is a column sunk into the lava."""
        if y1 is None:
            y1 = self.lava + 3
        return self.L.solid(x0, y0, x1, y1, mat, deco=deco)

    # ----------------------------------------------------------------- decor
    def scatter(self, x0, x1, y, density=0.35, layer="bg_decor", tall=True, forge=False):
        """Volcanic debris on a ground strip (pivots on the ground line y)."""
        rnd = self.rng
        x = x0 + 1 + rnd.random()
        while x < x1 - 1:
            r = rnd.random()
            if r < density * 0.3 and tall and x < x1 - 2:
                name = rnd.choice(FORGE_SMALL if forge and rnd.random() < 0.5 else ROCK_TALL)
                self.L.decor(name, x + 0.5, y, layer, flip=rnd.random() < 0.5)
                x += 3 + rnd.random() * 2
            elif r < density:
                name = rnd.choice(FORGE_SMALL if forge and rnd.random() < 0.4 else ROCK_SMALL)
                self.L.decor(name, x, y, layer, flip=rnd.random() < 0.5)
                x += 1.8 + rnd.random() * 1.5
            else:
                x += 1 + rnd.random()

    def chains(self, x0, x1, y, every=5, layer="fg_decor", long_ratio=0.5):
        """Chains hanging from a ceiling line y."""
        rnd = self.rng
        x = x0 + rnd.random() * every * 0.5
        while x < x1:
            name = "hanging_chain_long" if rnd.random() < long_ratio else "hanging_chain"
            self.L.decor(name, x, y, layer, sway=1.0 + rnd.random())
            x += every * (0.7 + rnd.random() * 0.6)

    def brazier(self, x, y, light=True, layer="bg_decor"):
        self.L.decor("forge_brazier", x, y, layer)
        if light:
            self.L.light(x, y - 2, FORGE_GLOW, 70, flicker=0.25)

    def banner(self, x, y, layer="bg_decor", flip=False):
        self.L.decor("burnt_banner", x, y, layer, flip=flip)

    def lavafall(self, x, y_top, y_bottom, hazard=True, layer="bg_decor", damage=2, alpha=1.0):
        """Molten fall from y_top to y_bottom (tiles). With hazard it burns unless shielded;
        purely decorative falls are drawn faded (alpha) so they read as background."""
        h = y_bottom - y_top
        tint = "#ffffff" if alpha >= 1.0 else "#ffb090"
        # One sprite (lip on top, splash at the bottom) scaled up to x2; longer falls are stacked.
        n = max(1, int(-(-h // 8)))
        seg = h / n
        for i in range(n):
            self.L.decor("lava_fall", x, y_top + i * seg, layer, scale=max(0.5, seg / 4.0), alpha=alpha, tint=tint)
        if alpha >= 0.8:
            self.L.light(x, (y_top + y_bottom) / 2, LAVA_GLOW, max(60, h * 10), flicker=0.2)
        if hazard:
            self.L.obj("damage_zone", [(x - 0.55) * T, y_top * T, 1.1 * T, h * T], "gameplay",
                       {"damage": damage, "interval": 0.6, "visible": False, "shieldProtects": True})

    def vent(self, x0, x1, y_top, y_floor, strength=230, grate=True, embers=True):
        """Updraft column (x0..x1, y_top..y_floor) with volcanic vents around its base."""
        oid = self.L.updraft(x0, x1, y_top, y_floor, strength, vent=grate)
        if embers:
            self.L.emitter(x0, y_top, x1, y_floor, "embers_rising", 2.5)
        self.L.light((x0 + x1) / 2, y_floor - 1, "#ffb070", 70, flicker=0.15)
        return oid

    def glow_line(self, x0, x1, y, every=22, color=LAVA_GLOW, radius=120):
        for x in range(int(x0), int(x1), every):
            self.L.light(x + self.rng.random() * 6, y, color, radius, flicker=0.12, bloom=False)

    def under_glow(self, objects, lava=None):
        """Lava light under every floating block, chains under forge metal."""
        lava = self.lava if lava is None else lava
        for o in list(objects):
            if o["type"] != "solid" or o["layer"] != "terrain":
                continue
            x, y, w, h = o["rect"]
            bottom = (y + h) / T
            if bottom >= lava or w < 48:
                continue
            if o.get("props", {}).get("material") == "forge_metal":
                for k in range(int(w / T) // 5):
                    tx = x / T + 1.5 + k * 5 + self.rng.random() * 2
                    self.L.decor("hanging_chain" if self.rng.random() < 0.6 else "hanging_chain_long", tx, bottom,
                                 "bg_decor", sway=1.2)

    # ------------------------------------------------------- automatic decor
    def autodecorate(self, density=0.3, skip=(), fg_every=0, chain_ceilings=True):
        """Scatters debris on every exposed terrain top that is not decorated yet, hangs chains
        under exposed forge-metal ceilings and adds lava bubbles on every lava pool.
        skip: list of (x0, x1) tile ranges left untouched (arenas dressed by hand...)."""
        objs = self.L.data["objects"]
        solids = [o for o in objs if o["type"] == "solid" and o["layer"] == "terrain"
                  and o.get("props", {}).get("collision", True)]
        blockers = solids + [o for o in objs if o["type"] in ("power_gate", "door", "oneway")]
        occupied = {}
        for o in objs:
            if o["type"] == "decor":
                x, y, w, h = o["rect"]
                gy = round((y + h) / T)
                occupied.setdefault(gy, []).append((x + w / 2) / T)

        def covered(x, y):  # tile column x just above row y has a blocker?
            for b in blockers:
                bx, by, bw, bh = b["rect"]
                if bx / T <= x < (bx + bw) / T and by / T <= y - 0.5 < (by + bh) / T:
                    return True
            return False

        def busy(x, y):
            return any(abs(x - ox) < 1.6 for ox in occupied.get(int(round(y)), []))

        rnd = self.rng
        for o in solids:
            x0, y0, w, h = (v / T for v in o["rect"])
            if w < 3 or y0 >= self.lava - 1:
                continue
            if any(a <= x0 + w / 2 < b for a, b in skip):
                continue
            mat = o.get("props", {}).get("material", "basalt")
            forge = mat == "forge_metal"
            x = x0 + 0.8 + rnd.random()
            while x < x0 + w - 0.8:
                if covered(x, y0) or busy(x, y0):
                    x += 1.0
                    continue
                r = rnd.random()
                if r < density * 0.25 and x < x0 + w - 2:
                    name = rnd.choice(FORGE_SMALL if forge else ROCK_TALL + ["ember_crystal", "fire_plant"])
                    self.L.decor(name, x + 0.5, y0, "bg_decor", flip=rnd.random() < 0.5)
                    occupied.setdefault(int(round(y0)), []).append(x + 0.5)
                    x += 3 + rnd.random() * 2
                elif r < density:
                    name = rnd.choice(FORGE_SMALL[:3] + ROCK_SMALL if forge else ROCK_SMALL)
                    self.L.decor(name, x, y0, "bg_decor", flip=rnd.random() < 0.5)
                    occupied.setdefault(int(round(y0)), []).append(x)
                    x += 2 + rnd.random() * 1.5
                else:
                    x += 1 + rnd.random()
            if fg_every and w >= 10 and rnd.random() < 0.5:
                fx = x0 + 2 + rnd.random() * (w - 4)
                if not covered(fx, y0):
                    self.L.decor(rnd.choice(["ash_rock_small", "slag_pile", "ash_rock_small"]), fx, y0 + 0.15,
                                 "fg_decor", alpha=0.95)
        # chains under exposed forge-metal ceilings
        if chain_ceilings:
            for o in solids:
                if o.get("props", {}).get("material") != "forge_metal":
                    continue
                x0, y0, w, h = (v / T for v in o["rect"])
                bottom = y0 + h
                if w < 6 or bottom >= self.lava - 2:
                    continue
                x = x0 + 1.5 + rnd.random() * 2
                while x < x0 + w - 1:
                    below_free = not any(
                        b["rect"][0] / T <= x < (b["rect"][0] + b["rect"][2]) / T
                        and b["rect"][1] / T <= bottom + 0.5 < (b["rect"][1] + b["rect"][3]) / T for b in blockers)
                    if below_free and rnd.random() < 0.35:
                        self.L.decor("hanging_chain" if rnd.random() < 0.6 else "hanging_chain_long", x, bottom,
                                     "bg_decor", sway=1.0 + rnd.random())
                    x += 3 + rnd.random() * 3
        # bubbling lava pools
        for o in [o for o in objs if o["type"] == "liquid"]:
            x, y, w, h = (v / T for v in o["rect"])
            if y >= self.lava - 0.5:
                continue
            self.L.emitter(x, y - 0.5, x + w, y + 0.5, "lava_bubbles", max(1.0, w / 6))
            self.L.emitter(x, y - 4, x + w, y, "embers_rising", max(0.8, w / 8))

    # ------------------------------------------------------------ cinematics
    def intro(self, title, subtitle, caption, nova, cut, pan, low, zoom=0.8):
        """Reference-style intro: title, camera travelling, caption, back to the start, Nova line."""
        self.L.data["intro"] = {"skippable": True, "letterbox": True, "steps": [
            {"type": "fade", "duration": 0.01, "params": {"to": 1}},
            {"type": "cut", "duration": 0.01, "params": {"x": cut[0] * T, "y": cut[1] * T}},
            {"type": "zoom", "duration": 0.01, "params": {"zoom": zoom}},
            {"type": "fade", "duration": 1.6, "wait": False, "params": {"to": 0}},
            {"type": "text", "duration": 4.0, "wait": False, "params": {"text": title, "subtitle": subtitle}},
            {"type": "camera", "duration": 6.0, "params": {"x": pan[0] * T, "y": pan[1] * T, "ease": "inOutSine"}},
            {"type": "text", "duration": 3.5, "wait": False, "params": {"text": caption, "style": "caption"}},
            {"type": "camera", "duration": 3.0, "params": {"x": low[0] * T, "y": low[1] * T, "ease": "inOutCubic"}},
            {"type": "shake", "duration": 0.6, "wait": False, "params": {"amount": 0.35}},
            {"type": "wait", "duration": 0.6},
            {"type": "zoom", "duration": 3.0, "wait": False, "params": {"zoom": 1.0}},
            {"type": "camera", "duration": 3.0, "params": {"start": True, "ease": "inOutCubic"}},
            {"type": "spawnPlayer", "duration": 0.9},
            {"type": "dialogue", "duration": 3.6, "params": {"speaker": "Nova", "text": nova}},
        ]}

    def outro(self, title, subtitle, flash="#ffe0b0"):
        self.L.data["outro"] = {"skippable": True, "letterbox": True, "steps": [
            {"type": "zoom", "duration": 1.4, "wait": False, "params": {"zoom": 1.4}},
            {"type": "camera", "duration": 1.4, "params": {"player": True}},
            {"type": "text", "duration": 3.0, "wait": False, "params": {"text": title, "subtitle": subtitle}},
            {"type": "playerAnim", "duration": 2.2, "params": {"anim": "victory"}},
            {"type": "flash", "duration": 0.6, "params": {"color": flash}},
            {"type": "fade", "duration": 0.9, "params": {"to": 1}},
        ]}

    # ------------------------------------------------------------ boss arena
    def boss_arena(self, x0, x1, floor, ceil, boss_kind, boss_x, boss_y=None, mat="forge_metal",
                   ledges=None, wall_mat=None, name="Boss", defeated_text="", music="music/pyralis_boss.ogg",
                   zoom=0.92, floor_mat="basalt", ledge_style="chain_grate"):
        """sylvanox_01 zone-5 pattern: dormant boss, entry door open, exit door closed, combat
        zone with waves=<boss>, lock=False, boss music, announce=False, camera zone, defeat hint.
        The floor is flat from x0 to x1; walls are 3 tiles thick on both sides."""
        L = self.L
        wall_mat = wall_mat or mat
        L.solid(x0, floor, x1, self.lava + 3, floor_mat)
        L.solid(x0 - 4, ceil - 6, x1 + 4, ceil, wall_mat)              # ceiling
        L.solid(x0, ceil, x0 + 3, floor - 8, wall_mat)                 # left wall above the door
        L.solid(x1 - 3, ceil, x1, floor - 8, wall_mat)                 # right wall above the door
        door_l = L.door(x0 + 1, floor - 8, 8, open_=True)
        door_r = L.door(x1 - 2, floor - 8, 8)
        for (lx0, lx1, ly) in (ledges or []):
            L.oneway(lx0, lx1, ly, ledge_style)
        by = floor if boss_y is None else boss_y
        boss = L.enemy(boss_kind, boss_x, by, patrol_tiles=12, face_left=True, dormant=True, boss=True)
        L.camzone(x0, ceil - 6, x1, floor + 4, "clamp", zoom)
        L.combat(x0 + 5, ceil, x1 - 5, floor, doors=door_l + "," + door_r, waves=boss, lock=False, music=music)
        L.data["objects"][-1]["props"]["announce"] = False
        L.event(boss, "bossDefeated", [
            {"type": "hint", "params": {"text": defeated_text, "duration": 4}},
            {"type": "shake", "params": {"amount": 0.5}},
        ], delay=1.5, name=name + " vaincu")
        L.event(boss, "bossPhase", [{"type": "shake", "params": {"amount": 0.4}}], once=False)
        return boss, door_l, door_r
