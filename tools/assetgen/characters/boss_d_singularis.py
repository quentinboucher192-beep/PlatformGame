"""Singularis, le Coeur Effondre (giga boss of the Noyau stellaire).

A dead star become a consciousness.  A sphere of dead stellar crust (cracked tectonic plates of
slate-violet basalt) around a collapsed white-hot core; through an almond window in the shell the
core looks out like an eye whose pupil is a singularity (a black point in a photon ring).  A broken
crown of crust spikes, a corona of plasma tongues and two magnetic prominence loops behind, three
plasma wisps dripping below, crust shards in orbit, and two great claw-hands of floating crust held
by plasma tethers (their palms carry a small sun: the projectiles leave the right palm).

Two forms:
  * form I  (phases 1-4): the shell is whole (only its cracks glow);
  * form II (phases 5-8): "Effondrement" - the lower front plates are gone, the remaining ones
    float apart, the core shows, the corona doubles and the crown is broken.
The light of the plasma (cracks, core, corona, palms, tethers, wisps) changes colour at every
phase; the crust never does.  Every emissive pixel uses one of five tones P0..P4 of the phase
ramp (plus pure white), so each phase sheet is the phase-1 sheet with those five colours swapped
(exact LUT, `recolour`), see PHASES.

Frames 192x192, pivot [96, 124] = bottom-centre of the 72x72 collision box (x 60..132, y 52..124)
centred on the shell (96, 88).  Drawn at scale 1.25 in game (240 px frame, 90 px box).
Clips (packed several per row, `start` = first column): idle 8, walk 8 (= move, same frames),
telegraph 6, attack 5 (fire f1, right palm), telegraph_special 6 (shell opens: wells, rings,
cages, tides), special 4 (loop, pulse), summon 6, roar 8 (phase change; the phase-5 sheet's roar
is the shattering of the shell, the phase-8 one the ignition of the supernova), hurt 2, death 14
(implosion, supernova flash, black dwarf), teleport_out 5, teleport_in 5.
Deterministic (fixed seeds)."""
import math

import numpy as np

import effects as FX
from boss_a_common import C, mat, motes, ring, streak
from boss_c_common import TAU, TIMING, Pose, dash_arc, over, shake
from raster import TCanvas, put

PI = math.pi
W = H = 192
PIVOT = [96, 124]
CX, CY = 96.0, 88.0
R = 36.0
WHITE = "#ffffff"

# The eight phases: name, French colour name, ramp P0..P4 (light -> dark).
PHASES = [
    ("Éveil", "rouge braise", ["#ffe2c4", "#ff9455", "#e64b2c", "#a32436", "#561230"]),
    ("Pluie d'étoiles", "or ambré", ["#fff4c2", "#ffcf4a", "#f28b1c", "#a9531c", "#5c2a14"]),
    ("Gravité", "violet", ["#f2dcff", "#c08aff", "#8650f0", "#4f2aa6", "#2a145c"]),
    ("Miroirs", "cyan", ["#dcffff", "#7cf2ff", "#2bb6e8", "#1d6aa8", "#10335c"]),
    ("Effondrement", "magenta plasma", ["#ffdcf2", "#ff7ad2", "#e0329e", "#8e1a6a", "#4a0c3a"]),
    ("Cage stellaire", "émeraude", ["#dcffe8", "#6dfab0", "#1fc47e", "#127056", "#0a3830"]),
    ("Escorte", "azur", ["#dfe8ff", "#8fb0ff", "#4a6cf2", "#28379e", "#141a52"]),
    ("Supernova", "blanc incandescent", ["#fffbe8", "#e8f6ff", "#a6dcff", "#5f8cff", "#3a2fae"]),
]
P = PHASES[0][2]
PC = [C(h) for h in P]
WHITE_C = C(WHITE)

# Crust: slate-violet basalt (never recoloured).
CRUST = mat("sg_crust", ["#8a80a0", "#5c536f", "#3b3450", "#241e33", "#130f1b"], th=[0.88, 0.7, 0.48, 0.26])
CRUST_BK = mat("sg_crust_bk", ["#4f4766", "#332c47", "#1f1a2c", "#110e17"])
DEAD = mat("sg_dead", ["#6e6a78", "#4a4652", "#2e2b36", "#1a1820"])
# Plasma (recoloured per phase).
CORE = mat("sg_core", [WHITE] + P[:4], emissive=True, outline=None, th=[0.85, 0.68, 0.5, 0.3])
HOT = mat("sg_hot", P, emissive=True, outline=None, th=[0.86, 0.68, 0.48, 0.28])
VOID = mat("sg_void", ["#24123a", "#07040c"], th=[0.6])

# Shell plates: Voronoi seeds in shell units (|u| <= 1).  (seed, lid-plate?, lost in form II?)
SEEDS = [((-0.56, -0.74), False, False), ((0.06, -0.9), False, False), ((0.66, -0.62), False, False),
         ((-0.93, -0.12), False, False), ((0.95, 0.02), False, True), ((-0.12, -0.44), True, False),
         ((0.18, 0.47), True, True), ((-0.66, 0.6), False, False), ((0.04, 0.9), False, True),
         ((0.68, 0.62), False, True), ((-0.62, 0.12), False, False), ((0.62, -0.12), False, False)]
SEED_XY = np.array([s for s, _, _ in SEEDS], np.float32)
SEED_VAR = [0.04, -0.03, 0.06, -0.05, 0.02, 0.0, -0.02, 0.05, -0.04, 0.03, -0.06, 0.01]
# Craters of the dead crust (shell units: x, y, radius), pitted into the plates.
CRATERS = [(-0.5, -0.55, 0.09), (-0.32, -0.78, 0.06), (0.3, -0.7, 0.08), (0.72, -0.38, 0.07), (-0.82, 0.22, 0.08),
           (-0.48, 0.42, 0.1), (-0.2, 0.7, 0.06), (0.4, 0.74, 0.08), (0.78, 0.36, 0.06), (-0.7, -0.3, 0.05),
           (0.1, -0.5, 0.05), (0.55, 0.55, 0.05)]
# Crown spikes (angle deg, length, half-width at the base): a broken crown on top.
CROWN = [(-140, 12, 4.4), (-117, 19, 5.2), (-92, 26, 6.0), (-67, 18, 5.2), (-44, 11, 4.4)]
CROWN_LOST = {0, 3}  # broken off in form II
# Corona tongues (angle deg, length, curl, phase offset).
TONGUES = [(-176, 15, 0.5, 0.0), (-152, 20, -0.4, 1.1), (-128, 15, 0.45, 2.3), (-101, 12, -0.3, 3.0),
           (-80, 13, 0.35, 4.2), (-55, 16, -0.45, 5.0), (-30, 21, 0.4, 0.7), (-6, 15, -0.5, 1.9),
           (20, 13, 0.4, 2.8), (160, 13, -0.4, 3.6), (40, 9, 0.5, 4.6), (140, 9, -0.5, 5.5)]


class SPose(Pose):
    DEFAULTS = dict(dx=0.0, dy=0.0, t=0.0, spread=0.0, crack=1.0, glow=0.6, lid=1.0, look=(3.0, 0.0),
                    pupil=1.0, corona=1.0, rot=0.0, tail=1.0, sway=0.0, broken=False, scale=1.0, dead=0.0,
                    lh=(-60.0, 26.0, 1.95, 0.4, 0.4), rh=(60.0, 26.0, 1.2, 0.4, 0.4), tether=1.0, halo=0.0,
                    shards=1.0, eye_glow=0.0)
    # lh / rh: hand (dx, dy from the shell centre, finger angle, open 0..1, palm glow 0..1)


def arms(h, out=0.0, op=None, glow=None, lift=0.0):
    """Both hands: h = 0 at rest (low, fingers down) .. 1 raised (high, fingers up), out = flung
    wider (px), keeping the claws inside the 192 px frame.  Returns dict(lh=..., rh=...)."""
    op = 0.4 + 0.6 * h if op is None else op
    glow = 0.35 + 0.65 * h if glow is None else glow
    dx = 58.0 - 6.0 * h + out
    dy = 26.0 - 52.0 * h - lift
    ha = min(1.0, 1.5 * h)  # the claws turn up before the hands are high
    return dict(lh=(-dx, dy, 1.95 + 2.25 * ha, op, glow), rh=(dx, dy, 1.2 - 2.25 * ha, op, glow))


def _noise(ux, uy):
    """Smooth deterministic warp of the shell coordinates (irregular cracks)."""
    return (0.055 * np.sin(7.3 * uy + 1.1) + 0.035 * np.sin(13.1 * ux - 2.0 * uy + 0.4),
            0.055 * np.sin(6.7 * ux + 2.3) + 0.035 * np.sin(11.7 * uy + 1.7 * ux + 2.9))


class Singularis:
    id = "singularis"
    W, H, PIVOT = W, H, PIVOT
    seed = 33081
    IDLE_FPS, WALK_FPS, SPECIAL_FPS = 9, 10, 12
    ATTACK_EVENTS = [(1, "fire")]

    # ------------------------------------------------------------ geometry
    def centre(self, p):
        return (CX + p.dx, CY + p.dy)

    def radius(self, p):
        return R * p.scale

    def eye_centre(self, p):
        cx, cy = self.centre(p)
        k = self.radius(p)
        return (cx + (0.06 + 0.025 * p.look[0]) * k, cy + (0.0 + 0.03 * p.look[1]) * k)

    def palm(self, p, right=True):
        cx, cy = self.centre(p)
        hx, hy, ang, _, _ = p.rh if right else p.lh
        return (cx + hx, cy + hy)

    # ------------------------------------------------------------ drawing
    def draw(self, p):
        cv = TCanvas(W, H)
        if p.scale <= 0.04:
            return cv.render()
        dead = p.dead >= 0.5
        self._corona(cv, p, dead)
        self._tail(cv, p, dead)
        self._shards(cv, p, back=True, dead=dead)
        self._tethers(cv, p, dead)
        self._core(cv, p, dead)
        self._shell(cv, p, dead)
        self._eye(cv, p, dead)
        self._shards(cv, p, back=False, dead=dead)
        self._hand(cv, p, False, dead)
        self._hand(cv, p, True, dead)
        return cv.render()

    def _corona(self, cv, p, dead):
        """A flaming corona all around the shell (a star, even dead), two great flares and a drip
        of plasma below; every term is periodic in p.t (loops)."""
        if dead or p.corona <= 0.02:
            return
        cx, cy = self.centre(p)
        k = p.scale
        rad = self.radius(p)
        grow = p.corona * (1.3 if p.broken else 1.0)
        dx, dy = cv.px - cx, cv.py - cy
        r = np.hypot(dx, dy)
        th = np.arctan2(dy, dx)
        t = p.t

        def bump(a, w):  # a pointed flare (laplacian, not gaussian: flames end in a tip)
            d = np.angle(np.exp(1j * (th - a)))
            return np.exp(-np.abs(d) / w)

        def tri(x):  # pointed licks
            return 1.0 - 2.0 * np.abs(np.mod(x, 1.0) - 0.5)

        u = th / TAU
        n = (0.32 + 0.42 * tri(11 * u + t / TAU) ** 1.7 + 0.26 * tri(17 * u - 2 * t / TAU + 0.3) ** 2.2
             + 0.1 * np.sin(5 * th + t))
        n = n + 1.3 * bump(-2.42 + 0.05 * math.sin(t), 0.11) * (0.85 + 0.15 * math.sin(t))
        n = n + 1.0 * bump(-0.66 + 0.05 * math.sin(t + 2), 0.09) * (0.85 + 0.15 * math.sin(t + 2.0))
        n = n + (1.7 * p.tail) * bump(PI / 2 + 0.08 * math.sin(p.sway + t), 0.16)
        rout = rad - 1 + 12.0 * k * grow * np.maximum(0.2, n)
        mask = (r <= rout) & (r > rad - 3)
        s = np.clip((r - (rad - 1)) / np.maximum(1.0, rout - (rad - 1)), 0, 1)
        inten = 0.8 - 0.62 * s + 0.06 * (p.glow - 0.6)
        cv.part(line=False)
        cv.put(mask, inten, HOT)

    def _tail(self, cv, p, dead):
        """Plasma drips under the corona's lower lobe (idle loops show them falling)."""
        if dead:
            return
        cx, cy = self.centre(p)
        k = p.scale
        rad = self.radius(p)
        for j, (ox, ln, ph) in enumerate(((-6, 1.0, 0.0), (5, 0.7, 2.6))):
            s = ((p.t / TAU) + ph / TAU) % 1.0
            y = cy + rad + 8 * k + (10 + 22 * s) * k * p.tail * ln
            x = cx + ox * k + 1.5 * math.sin(p.sway + ph)
            cv.part(line=False)
            cv.ellipse((x, y), 1.6 * k * (1 - 0.4 * s), 2.4 * k * (1 - 0.3 * s), HOT, flat=0.7 - 0.4 * s)

    def _shard_pos(self, p, i, n):
        cx, cy = self.centre(p)
        th = p.rot + TAU * i / n
        rx, ry, tilt = 50.0 * p.scale, 11.0 * p.scale, -0.22
        x, y = math.cos(th) * rx, math.sin(th) * ry
        c, s = math.cos(tilt), math.sin(tilt)
        return (cx + x * c - y * s, cy - 2 + x * s + y * c), math.sin(th)

    def _shards(self, cv, p, back, dead):
        if p.shards <= 0.0:
            return
        n = 6 if p.broken else 4
        rng = np.random.default_rng(self.seed + 3)
        sizes = rng.uniform(3.4, 5.6, 8)
        for i in range(n):
            (x, y), depth = self._shard_pos(p, i, n)
            if (depth < 0) != back:
                continue
            s = sizes[i] * p.scale * (0.85 if back else 1.0) * p.shards
            ang = p.rot * 1.7 + i * 1.3
            pts = [(x + math.cos(ang + a) * s * r, y + math.sin(ang + a) * s * r)
                   for a, r in ((0, 1.0), (1.3, 0.8), (2.3, 1.05), (3.5, 0.7), (4.6, 0.95))]
            cv.part()
            cv.poly(pts, DEAD if dead else (CRUST_BK if back else CRUST), base=0.55)
            if not dead and not back:
                cv.part(line=False)
                cv.px_set(x, y, HOT, inten=0.75)

    def _tethers(self, cv, p, dead):
        if dead or p.tether <= 0.05:
            return
        cx, cy = self.centre(p)
        rad = self.radius(p)
        for right in (False, True):
            hx, hy, ang, _, _ = p.rh if right else p.lh
            wx, wy = cx + hx - math.cos(ang) * 9, cy + hy - math.sin(ang) * 9
            dx, dy = wx - cx, wy - cy
            dl = math.hypot(dx, dy)
            if dl < rad + 4:
                continue
            sx, sy = cx + dx / dl * (rad - 2), cy + dy / dl * (rad - 2)
            mx, my = (sx + wx) / 2, (sy + wy) / 2 + 6
            pts = []
            n = 12
            for i in range(n + 1):
                s = i / n
                u = 1 - s
                pts.append((u * u * sx + 2 * u * s * mx + s * s * wx, u * u * sy + 2 * u * s * my + s * s * wy))
            for i in range(n):
                if (i + int(p.t * 2)) % 3 == 2:
                    continue
                cv.part(line=False)
                cv.capsule(pts[i], pts[i + 1], 1.3 * p.tether, 1.3 * p.tether, HOT, flat=0.45)
            for i in (3, 7, 11):
                cv.part(line=False)
                cv.disc(pts[i], 1.0, HOT, flat=0.85)

    def _core(self, cv, p, dead):
        cx, cy = self.centre(p)
        rad = self.radius(p)
        cv.part(line=False)
        if dead:
            cv.disc((cx, cy), rad - 1, DEAD, flat=0.2)
            return
        d = np.hypot(cv.px - cx, cv.py - cy) / max(1.0, rad - 1)
        inten = np.clip(1.0 - 0.95 * d + 0.25 * (p.glow - 0.6) + (0.22 if p.broken else 0.0), 0, 1)
        mask = d <= 1.0
        cv.put(mask, inten, CORE)

    def _shell_masks(self, p):
        """Per plate: (mask, intensity) of the displaced plate; plus the crack-rim mask."""
        cx, cy = self.centre(p)
        rad = self.radius(p)
        out = []
        ex, ey = self.eye_centre(p)
        a_eye = 0.56
        b_eye = max(0.02, 0.3 * p.lid)
        r_lens = (a_eye * a_eye + b_eye * b_eye) / (2 * b_eye)
        c_lens = r_lens - b_eye
        crack_w = (0.034 + 0.022 * (p.crack - 1.0)) * (36.0 / max(8.0, rad))
        X = np.arange(W, dtype=np.float32)[None, :] + 0.5
        Y = np.arange(H, dtype=np.float32)[:, None] + 0.5
        for i, ((sx, sy), lidp, lost) in enumerate(SEEDS):
            if p.broken and lost:
                continue
            sl = math.hypot(sx, sy) or 1.0
            push = p.spread + (3.0 if p.broken else 0.0)
            ox, oy = sx / sl * push * p.scale, sy / sl * push * p.scale
            if lidp:  # the lids open and close vertically with the eye
                oy += (-1 if sy < 0 else 1) * (p.lid - 1.0) * 1.5
            ux = (X - cx - ox) / rad
            uy = (Y - cy - oy) / rad
            r2 = ux * ux + uy * uy
            inside = r2 <= 1.0
            nx, ny = _noise(ux, uy)
            wx, wy = ux + nx, uy + ny
            dist = np.sqrt((wx[..., None] - SEED_XY[:, 0]) ** 2 + (wy[..., None] - SEED_XY[:, 1]) ** 2)
            if p.broken:
                keep = [j for j, (_, _, l) in enumerate(SEEDS) if not l]
                dist_k = dist[..., keep]
                order = np.sort(dist_k, axis=-1)
                mine = dist[..., i] <= order[..., 0] + 1e-6
                gap = order[..., 1] - order[..., 0]
            else:
                order = np.sort(dist, axis=-1)
                mine = dist[..., i] <= order[..., 0] + 1e-6
                gap = order[..., 1] - order[..., 0]
            crack = gap < crack_w
            rim = (gap >= crack_w) & (gap < crack_w + 0.035) & (r2 < 0.8)
            # eye window (in shell units, moves with the plate)
            exu, eyu = (ex - cx) / rad, (ey - cy) / rad
            win = ((ux - exu) ** 2 + (uy - eyu - c_lens) ** 2 <= r_lens ** 2) & \
                  ((ux - exu) ** 2 + (uy - eyu + c_lens) ** 2 <= r_lens ** 2)
            if p.broken:
                # the front of the shell is gone: a wide ragged hole
                hole = ((ux - 0.12) ** 2 / 0.62 ** 2 + (uy - 0.32) ** 2 / 0.6 ** 2) <= 1.0 + 0.12 * np.sin(9 * np.arctan2(uy, ux))
                win = win | hole
            plate = inside & mine & ~crack & ~win
            nz = np.sqrt(np.clip(1 - r2, 0, 1))
            lit = TCanvas.light_from_normal(ux, uy, nz) + SEED_VAR[i]
            for (qx, qy, qr) in CRATERS:
                d = np.hypot(ux - qx, uy - qy)
                lit = np.where(d < qr, lit - 0.2, lit)
                rim_c = (d >= qr) & (d < qr + 0.045) & ((ux - qx) + (uy - qy) > 0.02)
                lit = np.where(rim_c, lit + 0.12, lit)
            # the lids: a dark rim around the window
            r2l = ((a_eye + 0.07) ** 2 + (b_eye + 0.07) ** 2) / (2 * (b_eye + 0.07))
            c2l = r2l - (b_eye + 0.07)
            lens2 = ((ux - exu) ** 2 + (uy - eyu - c2l) ** 2 <= r2l ** 2) & ((ux - exu) ** 2 + (uy - eyu + c2l) ** 2 <= r2l ** 2)
            lit = np.where(lens2 & ~win, 0.1, lit)
            out.append((plate, lit, plate & rim))
        return out

    def _shell(self, cv, p, dead):
        cx, cy = self.centre(p)
        rad = self.radius(p)
        # crown spikes (behind the plates, moving with the top plates)
        for j, (adeg, ln, hw) in enumerate(CROWN):
            if p.broken and j in CROWN_LOST:
                continue
            a = math.radians(adeg)
            push = p.spread + (3.0 if p.broken else 0.0)
            bx, by = cx + math.cos(a) * (rad - 3 + push), cy + math.sin(a) * (rad - 3 + push)
            t = (math.cos(a + PI / 2) * hw * p.scale, math.sin(a + PI / 2) * hw * p.scale)
            L = ln * p.scale * (0.6 if p.broken and j == 2 else 1.0)
            tip = (bx + math.cos(a) * L + t[0] * 0.15, by + math.sin(a) * L + t[1] * 0.15)
            pts = [(bx - t[0], by - t[1]), (bx - t[0] * 0.35 + math.cos(a) * L * 0.55, by - t[1] * 0.35 + math.sin(a) * L * 0.55),
                   tip, (bx + t[0] * 0.5 + math.cos(a) * L * 0.6, by + t[1] * 0.5 + math.sin(a) * L * 0.6),
                   (bx + t[0], by + t[1])]
            cv.part()
            cv.poly(pts, DEAD if dead else CRUST, bevel=1.4, base=0.56)
            if not dead and ln >= 13:
                cv.part(line=False)
                cv.line((bx, by), (bx + math.cos(a) * L * 0.7, by + math.sin(a) * L * 0.7), HOT, inten=0.5 + 0.3 * p.glow,
                        decal=True)
        m = DEAD if dead else CRUST
        for i, (plate, lit, rim) in enumerate(self._shell_masks(p)):
            cv.part(line=1)
            cv.put(plate, lit, m)
            if not dead:
                cv.put(rim, 0.18 + 0.12 * p.glow, HOT, decal=True)

    def _eye(self, cv, p, dead):
        if p.lid <= 0.12:
            return
        ex, ey = self.eye_centre(p)
        k = p.scale
        lx, ly = p.look
        px, py = ex + lx * 1.2 * k, ey + ly * 0.8 * k
        if dead:
            cv.part(line=False)
            cv.disc((px, py), 2.6 * k, VOID, flat=0.2, decal=True)
            return
        rr = min(1.0, p.lid) * k
        cv.part(line=False)
        # iris: an accretion ring around the singularity
        cv.disc((px, py), 9.0 * rr, CORE, flat=0.58 + 0.2 * p.eye_glow, decal=True)
        cv.disc((px, py), 7.0 * rr, CORE, flat=0.74 + 0.2 * p.eye_glow, decal=True)
        # photon ring + singularity
        cv.disc((px, py), (4.6 * p.pupil + 1.0) * rr, CORE, flat=1.0, decal=True)
        cv.disc((px, py), 4.6 * p.pupil * rr, VOID, flat=0.3, decal=True)
        cv.px_set(px - 1.8 * rr, py - 1.9 * rr, VOID, inten=0.9, decal=True)

    def _hand(self, cv, p, right, dead):
        cx, cy = self.centre(p)
        hx, hy, ang, open_, glow = p.rh if right else p.lh
        hx, hy = cx + hx, cy + hy
        k = p.scale * 1.3
        side = 1 if right else -1
        ca, sa = math.cos(ang), math.sin(ang)

        def L(x, y):  # local (x along the fingers, y across; +y = thumb side, upward on screen)
            yy = -y * side
            return (hx + (x * ca - yy * sa) * k, hy + (x * sa + yy * ca) * k)

        cm = DEAD if dead else CRUST
        # fingers: three talons of floating segments, joints of plasma
        spread = 0.18 + 0.42 * open_
        for f, (oy, base_len) in enumerate(((-5.0, 12.0), (0.0, 14.0), (5.0, 11.5))):
            fa = (f - 1) * spread
            curl = (1.0 - open_) * 0.55 * (1 if side > 0 else 1)
            x, y = 7.0, oy * 0.9
            d = fa
            pts = [(x, y)]
            for s in range(3):
                seg = base_len / 3 * (1.0 - 0.12 * s)
                d += curl * (0.5 + 0.3 * s)
                x += math.cos(d) * seg
                y += math.sin(d) * seg
                pts.append((x, y))
            for s in range(3):
                a0 = (pts[s][0] + (pts[s + 1][0] - pts[s][0]) * 0.1, pts[s][1] + (pts[s + 1][1] - pts[s][1]) * 0.1)
                a1 = (pts[s][0] + (pts[s + 1][0] - pts[s][0]) * 0.86, pts[s][1] + (pts[s + 1][1] - pts[s][1]) * 0.86)
                cv.part()
                cv.capsule(L(*a0), L(*a1), (2.9 - 0.7 * s) * k, (2.3 - 0.8 * s) * k, cm)
                if not dead and s < 2:
                    cv.part(line=False)
                    cv.disc(L(*pts[s + 1]), 1.0 * k, HOT, flat=0.55 + 0.35 * glow)
            # claw tip
            tip = pts[-1]
            dd = d + curl * 0.6
            cv.part()
            cv.capsule(L(*tip), L(tip[0] + math.cos(dd) * 3.5, tip[1] + math.sin(dd) * 3.5), 1.3 * k, 0.4 * k, cm)
        # thumb
        tpts = [(2.0, 7.0), (6.0, 11.5 - 3 * open_), (9.5, 13.0 - 4 * open_)]
        cv.part()
        cv.chain([L(*q) for q in tpts], [2.6 * k, 2.0 * k, 1.0 * k], cm)
        # palm plate
        palm = [(-8, -6), (1, -8.5), (8, -6.5), (9.5, 0), (8, 6.5), (1, 8.5), (-8, 6), (-10.5, 0)]
        cv.part()
        cv.poly([L(*q) for q in palm], cm, bevel=1.5, base=0.6)
        if not dead:
            # a glowing seam across the back of the hand and the palm sun
            cv.part(line=False)
            cv.line(L(-7, -1), L(6, 1.5), HOT, inten=0.42 + 0.3 * glow, decal=True)
            r_sun = (2.2 + 2.6 * glow) * k
            cv.part(line=False)
            cv.disc(L(1.5, 0), r_sun + 1.0, HOT, flat=0.55 + 0.25 * glow)
            cv.disc(L(1.5, 0), r_sun, CORE, flat=0.7 + 0.3 * glow)
        # wrist stump (the arm is plasma only)
        cv.part()
        cv.capsule(L(-9, 0), L(-13, 0), 3.4 * k, 2.4 * k, cm)

    # ------------------------------------------------------------- effects
    def sparkle(self, f, x, y, size, c0=None, c1=None):
        FX.sparkle(f, int(x), int(y), size, c0 or WHITE_C, c1 or PC[1])

    def charge(self, f, x, y, i, n, seed, r0=22, r1=4, count=10):
        """Motes converging onto (x, y)."""
        rng = np.random.default_rng(seed)
        t = (i + 1) / n
        for k in range(count):
            a = rng.random() * TAU
            d = r0 * (1 - t) + r1 + rng.random() * 6
            put_if_empty(f, x + math.cos(a) * d, y + math.sin(a) * d, [WHITE_C, PC[0], PC[1], PC[2]][k % 4])

    def embers(self, f, i, n, seed, count=8):
        """Plasma droplets falling from the wisps (periodic over n frames)."""
        rng = np.random.default_rng(seed)
        for k in range(count):
            x0 = CX - 16 + rng.random() * 32
            off = rng.random()
            t = (i / n + off) % 1.0
            y = CY + R + 18 + t * 46
            put_if_empty(f, x0 + math.sin(t * 6 + k) * 2, y, [PC[1], PC[2], PC[3]][min(2, int(t * 3))])

    # ================================================================= clips
    def base(self, **kw):
        return SPose(**kw)

    def idle(self, broken=False):
        out = []
        n = 8
        for i in range(n):
            ph = TAU * i / n
            p = SPose(t=ph, dy=2.0 * math.sin(ph), rot=TAU * i / n / 4, sway=ph, broken=broken,
                      glow=0.6 + 0.12 * math.sin(ph), look=(3.0, 0.4 * math.sin(ph)),
                      lid=0.3 if i == 5 else 1.0,
                      lh=(-60, 26 + 3 * math.sin(ph + 0.6), 1.95 + 0.06 * math.sin(ph), 0.4, 0.35),
                      rh=(60, 26 + 3 * math.sin(ph + 1.4), 1.2 - 0.06 * math.sin(ph), 0.4, 0.35))
            f = self.draw(p)
            self.embers(f, i, n, self.seed + 1)
            out.append(f)
        return out

    def walk(self, broken=False):
        out = []
        n = 8
        for i in range(n):
            ph = TAU * i / n
            p = SPose(t=ph * 1.5, dx=2.0, dy=1.5 * math.sin(ph), rot=TAU * i / n / 4, sway=ph * 1.5, broken=broken,
                      tail=1.15, look=(4.0, 0.3), glow=0.65,
                      lh=(-64, 22 + 2 * math.sin(ph), 2.4, 0.3, 0.3), rh=(56, 30 + 2 * math.sin(ph + 1), 0.9, 0.5, 0.35))
            f = self.draw(p)
            rng = np.random.default_rng(self.seed + 10 + i)
            for k in range(6):
                y = int(rng.integers(50, 140))
                x0 = int(rng.integers(4, 24))
                streak(f, (x0, y), (x0 + int(rng.integers(6, 14)), y), PC[2 + k % 2])
            out.append(f)
        return out

    def telegraph(self, broken=False):
        """The right hand draws back, its palm sun swells; the eye flares (volleys, rays, meteors)."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            p = SPose(t=0.6 * i, broken=broken, glow=0.6 + 0.4 * t, eye_glow=t, lid=1.0 + 0.15 * t, pupil=1.0 - 0.25 * t,
                      corona=1.0 + 0.25 * t, spread=0.6 * t, dy=-2 * t, look=(4.0, -0.5),
                      lh=(-60, 22 - 8 * t, 2.1 + 0.5 * t, 0.5 + 0.4 * t, 0.4 + 0.3 * t),
                      rh=(54 - 6 * t, 22 - 20 * t, 1.0 - 1.9 * t, 0.7, 0.35 + 0.65 * t))
            f = self.draw(p)
            x, y = self.palm(p, True)
            self.charge(f, x + 2, y, i, 6, self.seed + 20 + i, r0=26, r1=6, count=6 + 2 * i)
            if i >= 4:
                self.sparkle(f, x + 2, y, 3 + (i - 4) * 2)
            out.append(f)
        return out

    def attack(self, broken=False):
        """The right hand thrusts forward and the palm sun bursts (projectiles leave on f1)."""
        keys = [  # rh dx, dy, open, glow, lean
            (58, -2, 1.0, 1.0, 2), (64, 2, 1.0, 0.9, 4), (62, 6, 0.9, 0.6, 3), (60, 14, 0.6, 0.4, 1), (58, 22, 0.45, 0.35, 0)]
        out = []
        for i, (rx, ry, op, g, lean) in enumerate(keys):
            p = SPose(t=0.5 * i + 3, broken=broken, dx=lean * 0.5, glow=0.7 + 0.3 * g, eye_glow=g, look=(4.5, 0.0),
                      corona=1.0 + 0.2 * g, spread=0.6 * g,
                      lh=(-60, 18 + 2 * i, 2.3, 0.7, 0.5), rh=(rx, ry, 0.1 + 0.25 * (i / 4), op, g))
            f = self.draw(p)
            x, y = self.palm(p, True)
            if i == 1:
                FX.burst(f, x + 10, y, 8, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 40, jag=0.35, rays=7)
            if i == 2:
                for k in range(5):
                    a = -0.55 + 0.275 * k
                    self.sparkle(f, x + 12 + math.cos(a) * 18, y + math.sin(a) * 18, 2)
                ring(f, x + 6, y, 10, PC[1], -1.2, 1.2)
            if i == 3:
                motes(f, x + 18, y, 10, 8, 26, [WHITE_C, PC[0], PC[1]], seed=self.seed + 45)
            out.append(f)
        return out

    def telegraph_special(self, broken=False):
        """Both hands rise, the shell opens and the light of the core gathers (wells, rings, cages, tides)."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            p = SPose(t=0.7 * i, broken=broken, spread=2.5 * t, crack=1.0 + 1.4 * t, glow=0.6 + 0.4 * t, eye_glow=t,
                      lid=1.0 + 0.2 * t, pupil=1.0 + 0.3 * t, corona=1.0 + 0.4 * t, dy=-3 * t, tail=1.0 + 0.2 * t,
                      **arms(t))
            f = self.draw(p)
            cx, cy = self.centre(p)
            for k, r in enumerate((84 - 9 * i, 66 - 7 * i)):
                if r > R + 8:
                    dash_arc(f, cx, cy, r, r * 0.86, 0, TAU, PC[1 + k], dash=4, gap=3, phase=i)
            motes(f, cx, cy, 10 + 4 * i, 40, 86 - 20 * t, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 50 + i,
                  inward=0.25 * t)
            if i == 5:
                self.sparkle(f, cx, cy - R - 24, 6)
            out.append(f)
        return out

    def special(self, broken=False):
        """Pulse (loop): the open shell throbs, hands up, rings of light leave the core."""
        out = []
        for i in range(4):
            p = SPose(t=1.5 * i, broken=broken, spread=2.5 + 0.8 * (i % 2), crack=2.4, glow=1.0, eye_glow=1.0,
                      lid=1.15, pupil=1.25 - 0.1 * (i % 2), corona=1.4 + 0.1 * (i % 2), dy=-3, tail=1.2,
                      **arms(1.0, lift=2 * (i % 2)))
            f = self.draw(p)
            cx, cy = self.centre(p)
            for k in range(2):
                r = R + 10 + ((i * 7 + k * 14) % 28)
                ring(f, cx, cy, r, [PC[1], PC[2]][k], dotted=0 if r < R + 26 else 2)
            out.append(f)
        return out

    def _rift(self, f, x, y, s, seed):
        """A small star tearing out of space (summon)."""
        if s <= 0.05:
            return
        FX.burst(f, x, y, 2 + 4 * s, [WHITE_C, PC[0], PC[1], PC[2]], seed=seed, jag=0.5, rays=int(4 * s))
        ring(f, x, y, 5 + 6 * s, PC[2], dotted=2)
        motes(f, x, y, int(5 + 6 * s), 4, 10 * s + 6, [PC[0], PC[1], PC[2]], seed=seed + 1)

    def summon(self, broken=False):
        """Arms flung wide: small stars tear out of space on both sides (the escort)."""
        keys = [0.3, 0.8, 1.0, 1.0, 0.7, 0.25]
        out = []
        for i, h in enumerate(keys):
            p = SPose(t=0.6 * i, broken=broken, spread=1.5 * h, glow=0.6 + 0.4 * h, eye_glow=h, corona=1.0 + 0.3 * h,
                      dy=-2 * h, lid=1.0 + 0.15 * h,
                      **arms(0.45 * h, out=6 * h, op=0.4 + 0.6 * h))
            f = self.draw(p)
            s = [0, 0.4, 0.8, 1.0, 1.0, 0.6][i]
            self._rift(f, 20, 132, s * 1.3, self.seed + 70 + i)
            self._rift(f, 172, 132, s * 1.3, self.seed + 75 + i)
            out.append(f)
        return out

    def roar(self, broken=False):
        """Phase change: the shell heaves, the corona flares, the hands open wide."""
        keys = [(0.0, 0), (0.3, 0), (1.0, 1), (1.0, -1), (1.0, 1), (0.9, -1), (0.4, 0), (0.0, 0)]
        out = []
        for i, (h, sh) in enumerate(keys):
            p = SPose(t=0.8 * i, broken=broken, spread=3.0 * h, crack=1.0 + 1.6 * h, glow=0.6 + 0.4 * h, eye_glow=h,
                      lid=1.0 + 0.25 * h, pupil=1.0 - 0.3 * h, corona=1.0 + 0.6 * h, tail=1.0 + 0.3 * h,
                      **arms(0.75 * h, out=4 * h, op=0.4 + 0.6 * h))
            f = self.draw(p)
            if h > 0.8:
                cx, cy = self.centre(p)
                for k, r in enumerate((R + 16 + 3 * (i % 2), R + 30 + 3 * (i % 2))):
                    ring(f, cx, cy, r, [PC[1], PC[2]][k], dotted=0 if k == 0 else 2)
                motes(f, cx, cy, 22, 44, 90, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 80 + i, big=0.2)
                f = shake(f, sh, 0)
            out.append(f)
        return out

    def shatter(self):
        """Phase 5 roar: the shell bursts, plates fly, and form II settles (Effondrement)."""
        out = []
        seq = [(False, 1.0, 1.4, 0.3), (False, 2.0, 2.6, 0.7), (False, 3.5, 3.4, 1.0), (True, 7.0, 3.0, 1.0),
               (True, 4.5, 2.4, 0.9), (True, 2.5, 1.8, 0.6), (True, 1.0, 1.2, 0.3), (True, 0.0, 1.0, 0.0)]
        for i, (broken, spread, crack, h) in enumerate(seq):
            p = SPose(t=0.8 * i, broken=broken, spread=spread, crack=crack, glow=0.6 + 0.4 * h, eye_glow=h,
                      lid=1.0 + 0.2 * h, corona=1.0 + 0.7 * h, tail=1.0 + 0.3 * h,
                      **arms(0.75 * h, out=4 * h, op=0.4 + 0.6 * h))
            f = self.draw(p)
            cx, cy = self.centre(p)
            if i == 2:
                f = flashed(f, 0.35)
            if i == 3:
                FX.burst(f, cx + 4, cy + 10, 16, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 90, jag=0.4, rays=10)
            if 3 <= i <= 5:
                rng = np.random.default_rng(self.seed + 91 + i)
                for k in range(7):
                    a = rng.random() * TAU
                    d = 40 + 12 * (i - 3) + rng.random() * 10
                    FX.chunk(f, cx + math.cos(a) * d, cy + math.sin(a) * d * 0.8, 3 if k % 2 else 2, C("#8a80a0"),
                             C("#3b3450"))
                ring(f, cx, cy, 30 + 14 * (i - 3), PC[1 + (i - 3)], dotted=0 if i == 3 else 2)
            if i in (2, 3, 4):
                fl = getattr(f, "flash", 0.0)
                f = shake(np.asarray(f), [1, -2, 1][i - 2], 0)
                if fl:
                    f = flashed(f, fl)
            out.append(f)
        return out

    def ignite(self):
        """Phase 8 roar: the supernova ignites (white flash, the corona at its widest)."""
        out = []
        keys = [0.2, 0.6, 1.0, 1.0, 1.0, 0.8, 0.5, 0.2]
        for i, h in enumerate(keys):
            p = SPose(t=0.9 * i, broken=True, spread=3.5 * h, crack=1.0 + 2.0 * h, glow=0.7 + 0.3 * h, eye_glow=h,
                      lid=1.0 + 0.3 * h, pupil=1.0 - 0.4 * h, corona=1.1 + 0.55 * h, tail=1.0 + 0.15 * h,
                      **arms(0.85 * h, out=4 * h, op=0.4 + 0.6 * h))
            f = self.draw(p)
            cx, cy = self.centre(p)
            if i in (2, 3):
                f = flashed(f, 0.45 if i == 2 else 0.2)
            if h >= 1.0:
                for k, r in enumerate((R + 18 + 8 * (i - 2), R + 34 + 8 * (i - 2), R + 50 + 8 * (i - 2))):
                    ring(f, cx, cy, r, [WHITE_C, PC[0], PC[1]][k], dotted=0 if k < 2 else 2)
                motes(f, cx, cy, 30, 50, 94, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 100 + i, big=0.3)
                fl = getattr(f, "flash", 0.0)
                f = shake(np.asarray(f), [1, -1, 1, -1, 1][i - 2], 0)
                if fl:
                    f = flashed(f, fl)
            out.append(f)
        return out

    def hurt(self, broken=False):
        a = SPose(t=0.3, broken=broken, dx=-3, spread=1.2, crack=1.6, glow=0.9, lid=0.5, look=(1.0, 0.0),
                  lh=(-64, 30, 2.1, 0.7, 0.4), rh=(54, 30, 1.0, 0.7, 0.4))
        b = SPose(t=0.6, broken=broken, dx=-1, spread=0.4, glow=0.7, lid=0.9,
                  lh=(-62, 28, 2.0, 0.5, 0.4), rh=(57, 28, 1.1, 0.5, 0.4))
        fa = flashed(self.draw(a), 0.55)
        fb = self.draw(b)
        motes(fb, CX, CY, 10, 36, 52, [WHITE_C, PC[0], PC[1]], seed=self.seed + 110)
        return [fa, fb]

    def residue(self, t):
        """The black dwarf: a cold stone the size of a fist, smoking."""
        cv = TCanvas(W, H)
        cv.part()
        cv.disc((CX, CY), 4 + 7 * t, DEAD)
        cv.part()
        for (x, y, r) in ((CX - 9, CY + 6, 2.4), (CX + 10, CY - 4, 2.0), (CX + 4, CY + 10, 1.6)):
            cv.disc((x, y), r * t + 0.4, DEAD)
        return cv.render()

    def death(self, broken=False):
        out = []
        # 1-3: convulsions, the light flickers
        seq = [SPose(t=0.2, broken=broken, spread=2.0, crack=2.2, glow=1.0, eye_glow=1.0, lid=1.3, pupil=0.6, corona=1.5,
                     **arms(0.6, out=4)),
               SPose(t=0.9, broken=broken, spread=0.5, crack=1.4, glow=0.4, lid=0.5, pupil=1.4, corona=0.8, dx=2,
                     lh=(-56, 30, 1.6, 0.3, 0.2), rh=(54, 34, 1.4, 0.3, 0.2)),
               SPose(t=1.6, broken=broken, spread=3.0, crack=2.6, glow=1.0, eye_glow=1.0, lid=1.4, pupil=0.4, corona=1.8,
                     **arms(0.8, out=4))]
        for i, p in enumerate(seq):
            f = self.draw(p)
            f = shake(f, [1, -1, 2][i], 0)
            out.append(flashed(f, 0.4) if i == 0 else f)
        # 4-7: implosion - everything falls into the core
        fall = [(0.85, 0.2), (0.62, 0.3), (0.4, 0.5), (0.2, 0.7)]
        for i, (sc, dead) in enumerate(fall):
            p = SPose(t=2 + i, broken=broken, scale=sc, spread=0.0, glow=1.0, eye_glow=1.0, lid=0.8, pupil=1.6,
                      corona=1.0 - 0.25 * i, tail=0.5, tether=0.5, shards=sc,
                      lh=(-50 * sc - 6, 20 * sc, 2.2, 0.2, 0.8), rh=(50 * sc + 6, 20 * sc, 0.9, 0.2, 0.8))
            f = self.draw(p)
            cx, cy = self.centre(p)
            rng = np.random.default_rng(self.seed + 120 + i)
            for k in range(12):
                a = rng.random() * TAU
                d0 = 80 - 14 * i
                streak(f, (cx + math.cos(a) * d0, cy + math.sin(a) * d0 * 0.8),
                       (cx + math.cos(a) * (d0 - 14), cy + math.sin(a) * (d0 - 14) * 0.8), [WHITE_C, PC[0], PC[1]][k % 3])
            out.append(f)
        # 8-13: supernova flash, shockwave, then the black dwarf
        for k, t in enumerate((0.0, 0.2, 0.4, 0.6, 0.8, 0.95)):
            f = np.zeros((H, W, 4), np.uint8)
            r = 6 + 80 * t
            ring(f, CX, CY, r, [WHITE_C, PC[0], PC[1], PC[2], PC[3], PC[4]][k], dotted=0 if k < 3 else 2)
            if k < 3:
                FX.burst(f, CX, CY, 22 - 7 * k, [WHITE_C, WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 130 + k,
                         jag=0.3, rays=14 - 4 * k)
            motes(f, CX, CY, 30 - 3 * k, 10 + 50 * t, 20 + 70 * t, [WHITE_C, PC[0], PC[1], PC[2]],
                  seed=self.seed + 140 + k)
            FX.under(f, self.residue(min(1.0, 0.3 + t)))
            out.append(f)
        last = self.residue(1.0)
        motes(last, CX, CY - 4, 8, 6, 24, [PC[2], PC[3], PC[4]], seed=self.seed + 150, arc=(-2.8, -0.3))
        out.append(last)
        return out

    def teleport_out(self, broken=False):
        """Quantum leap out: the star collapses to a point."""
        out = []
        for i, sc in enumerate((0.85, 0.6, 0.35, 0.12, 0.0)):
            p = SPose(t=i, broken=broken, scale=sc, glow=1.0, eye_glow=1.0, pupil=1.6, corona=1.0 - 0.2 * i, tail=sc,
                      tether=sc, shards=sc, lh=(-60 * sc, 26 * sc, 2.0, 0.3, 0.8), rh=(60 * sc, 26 * sc, 1.1, 0.3, 0.8))
            f = self.draw(p)
            cx, cy = self.centre(p)
            rng = np.random.default_rng(self.seed + 160 + i)
            for k in range(10):
                a = rng.random() * TAU
                d0 = 64 * (0.4 + sc)
                streak(f, (cx + math.cos(a) * d0, cy + math.sin(a) * d0 * 0.8),
                       (cx + math.cos(a) * d0 * 0.6, cy + math.sin(a) * d0 * 0.48), PC[1 + k % 2])
            if i >= 3:
                self.sparkle(f, cx, cy, 8 - 2 * (i - 3))
            out.append(f)
        return out

    def teleport_in(self, broken=False):
        """Quantum leap in: a point of light tears open into the star."""
        out = []
        for i, sc in enumerate((0.0, 0.25, 0.55, 0.85, 1.0)):
            p = SPose(t=i, broken=broken, scale=sc, glow=1.0 - 0.3 * sc, eye_glow=1.0 - 0.5 * sc, corona=0.6 + 0.4 * sc,
                      tail=sc, tether=sc, shards=sc,
                      lh=(-60 * sc, 26 * sc, 1.95, 0.4, 0.6), rh=(60 * sc, 26 * sc, 1.2, 0.4, 0.6))
            f = self.draw(p)
            cx, cy = self.centre(p)
            if i == 0:
                self.sparkle(f, cx, cy, 8)
                ring(f, cx, cy, 8, PC[1])
            if 1 <= i <= 3:
                ring(f, cx, cy, R * sc + 10 + 6 * i, [PC[0], PC[1], PC[2]][i - 1], dotted=2)
            out.append(f)
        return out

    # ------------------------------------------------------------ the sheet
    def clips(self, form2=False, roar=None):
        b = form2
        walk = self.walk(b)
        return [
            ("idle", self.idle(b), dict(fps=self.IDLE_FPS, loop=True)),
            ("telegraph", self.telegraph(b), dict(durations=TIMING["telegraph"], loop=False)),
            ("walk", walk, dict(fps=self.WALK_FPS, loop=True)),
            ("attack", self.attack(b), dict(durations=TIMING["attack"], loop=False, events=self.ATTACK_EVENTS)),
            ("telegraph_special", self.telegraph_special(b), dict(durations=TIMING["telegraph_special"], loop=False)),
            ("special", self.special(b), dict(fps=self.SPECIAL_FPS, loop=True)),
            ("hurt", self.hurt(b), dict(durations=TIMING["hurt"], loop=False)),
            ("summon", self.summon(b), dict(durations=TIMING["summon"], loop=False)),
            ("roar", roar if roar is not None else self.roar(b), dict(durations=TIMING["roar"], loop=False)),
            ("death", self.death(b), dict(durations=TIMING["death"], loop=False)),
            ("teleport_out", self.teleport_out(b), dict(durations=[0.06, 0.06, 0.06, 0.08, 0.1], loop=False)),
            ("teleport_in", self.teleport_in(b), dict(durations=[0.08, 0.06, 0.06, 0.06, 0.1], loop=False)),
        ]


class Flashed(np.ndarray):
    """A frame to be flashed white once recoloured (a flash blends colours out of the phase
    ramps, so it is applied to each phase sheet after the LUT)."""
    flash = 0.0


def flashed(f, amount):
    g = np.asarray(f).copy().view(Flashed)
    g.flash = amount
    return g


def finish(f, phase):
    """Recolour a frame for `phase`, then apply its pending flash."""
    out = recolour(np.asarray(f), phase)
    amount = getattr(f, "flash", 0.0)
    return FX.flash(out, color=(255, 255, 255, 255), amount=amount) if amount else out


def put_if_empty(f, x, y, c):
    x, y = int(x), int(y)
    if 0 <= y < f.shape[0] and 0 <= x < f.shape[1] and f[y, x, 3] == 0:
        f[y, x] = c


# ------------------------------------------------------------------ recolour + packing
def recolour(img, phase):
    """The phase-1 sheet with the five plasma tones of `phase` (0-based): an exact LUT."""
    if phase == 0:
        return img.copy()
    out = img.copy()
    src = [C(h) for h in PHASES[0][2]]
    dst = [C(h) for h in PHASES[phase][2]]
    rgb = img[:, :, :3]
    for a, b in zip(src, dst):
        sel = (rgb[:, :, 0] == a[0]) & (rgb[:, :, 1] == a[1]) & (rgb[:, :, 2] == a[2]) & (img[:, :, 3] > 0)
        out[sel, :3] = b[:3]
    return out


COLUMNS = 14  # the longest clip (death) fills a row; the others are packed side by side


def pack(clips, columns=COLUMNS):
    """Clips placed left to right, several per row (never split).  `move` shares `walk`'s frames.
    Returns (image, meta clips)."""
    rows, meta = [], {}
    cur, used = [], 0
    for name, frames, kw in clips:
        if used + len(frames) > columns:
            rows.append(cur)
            cur, used = [], 0
        cur.append((name, frames, kw, used))
        used += len(frames)
    if cur:
        rows.append(cur)
    img = np.zeros((H * len(rows), W * columns, 4), np.uint8)
    for r, row in enumerate(rows):
        for name, frames, kw, start in row:
            for c, f in enumerate(frames):
                img[r * H:(r + 1) * H, (start + c) * W:(start + c + 1) * W] = np.asarray(f)
            m = {"row": r, "count": len(frames)}
            if start:
                m["start"] = start
            if kw.get("durations") is not None:
                assert len(kw["durations"]) == len(frames), name
                m["durations"] = [round(float(d), 3) for d in kw["durations"]]
            else:
                m["fps"] = kw.get("fps", 10)
            m["loop"] = bool(kw.get("loop", True))
            if kw.get("events"):
                m["events"] = [{"frame": int(fr), "name": n} for fr, n in kw["events"]]
            meta[name] = m
            if name == "walk":
                meta["move"] = dict(m)
    return img, meta


BOSS = Singularis()


def phase_clips(k, form1, form2, shat, ign):
    clips = form1 if k < 4 else (shat if k == 4 else (ign if k == 7 else form2))
    return [(name, [finish(f, k) for f in frames], kw) for name, frames, kw in clips]


def phase_sheets():
    """(phase index 0..7, image, clip meta) of the eight phase sheets."""
    form1 = BOSS.clips(False)
    form2 = BOSS.clips(True)
    shat = [c if c[0] != "roar" else ("roar", BOSS.shatter(), c[2]) for c in form2]
    ign = [c if c[0] != "roar" else ("roar", BOSS.ignite(), c[2]) for c in form2]
    out = []
    for k in range(8):
        img, meta = pack(phase_clips(k, form1, form2, shat, ign))
        out.append((k, img, meta))
    return out


def build():
    from sheet import save_json, save_png
    for k, img, meta in phase_sheets():
        name = "singularis" if k == 0 else "singularis_p%d" % (k + 1)
        tex = "sprites/enemies/%s.png" % name
        save_png(img, tex)
        save_json({"texture": tex, "frameWidth": W, "frameHeight": H, "pivot": PIVOT, "clips": meta},
                  "animations/%s.json" % name)

