"""Knight rigs for the BOSSES-B sheets (96x96 frames, collision 44x60, pivot [48, 96]).

Same skeleton idea as the Thornwarden rig (boss_rig.py: hips + lean, two-bone
IK limbs, head frame), but each knight has its own body and parts:

* Magmaroth  - hunched colossal magma golem-knight: basalt plates with glowing
  lava seams, volcanic back hump with smoking vents, horned (obsidian) helm with
  a furnace maw, molten core behind a grate, giant maul held in the front fist.
* Glaciarch  - tall ice knight: pale steel armour encrusted with ice crystals,
  frost crown, long frozen cape with an icicle hem, diamond frost-heart, crystal
  greatsword fused to the front gauntlet.
"""
import math

import boss_b_palette as C
from raster import TCanvas, ik2

W = H = 96


class Pose:
    def __init__(self, **kw):
        self.hip = (47.0, 66.0)
        self.lean = 0.0            # torso rotation about the hips (+ = forward)
        self.head_tilt = 0.0       # + = looking down
        self.head_off = (0.0, 0.0)
        self.foot_f = (59.0, 95.0)
        self.foot_b = (35.0, 95.0)
        self.fist = (74.0, 68.0)   # front (weapon) hand
        self.wpn = math.pi / 2     # weapon direction (fist -> weapon end), canvas radians
        self.elbow_dir = 1.0
        self.hand_b = (30.0, 66.0)
        self.core = 0.6            # core glow 0..1.6
        self.eye = 0.6
        self.glow = 0.6            # seams / crystal glow 0..1.6
        self.wglow = 0.4           # weapon glow 0..1.6
        self.cape_wave = 0.0
        self.cape_lift = 0.0
        self.chest_open = 0.0
        self.jaw = 0.0
        self.cracks = 0
        self.dead = 0.0
        self.rot = 0.0
        self.rot_c = (48.0, 95.0)
        self.offset = (0.0, 0.0)
        self.t = 0.0               # animation phase 0..1 (flicker of small details)
        self.bristle = 0.0         # crystal / spike growth boost
        self.no_weapon = False
        self.broken_weapon = 0.0   # glaciarch: 0..1 shattered blade length lost
        self.__dict__.update(kw)

    def copy(self, **kw):
        d = dict(self.__dict__)
        d.update(kw)
        return Pose(**d)


def _frame(p, sh_f, sh_b, hip_f, hip_b, head):
    hx, hy = p.hip
    c, s = math.cos(p.lean), math.sin(p.lean)

    def B(x, y):
        return (hx + x * c - y * s, hy + x * s + y * c)

    hc = B(head[0] + p.head_off[0], head[1] + p.head_off[1])
    hl = p.lean + p.head_tilt
    hcs, hsn = math.cos(hl), math.sin(hl)

    def HD(x, y):
        return (hc[0] + x * hcs - y * hsn, hc[1] + x * hsn + y * hcs)
    return B, HD, B(*sh_f), B(*sh_b), B(*hip_f), B(*hip_b), hc


def spike(cv, base, ang, ln, wd, mat, bevel=0.8, base_i=0.6):
    tip = (base[0] + math.cos(ang) * ln, base[1] + math.sin(ang) * ln)
    nx, ny = -math.sin(ang) * wd, math.cos(ang) * wd
    cv.tpoly([(base[0] + nx, base[1] + ny), tip, (base[0] - nx, base[1] - ny)], mat, bevel=bevel, base=base_i)
    return tip


def crystal(cv, base, ang, ln, wd, mat):
    """Faceted crystal: hexagonal-ish prism with a pointed tip."""
    ca, sa = math.cos(ang), math.sin(ang)
    nx, ny = -sa, ca

    def Q(u, v):
        return (base[0] + ca * u + nx * v, base[1] + sa * u + ny * v)
    cv.tpoly([Q(0, -wd * 0.8), Q(ln * 0.62, -wd), Q(ln, 0), Q(ln * 0.62, wd), Q(0, wd * 0.8)], mat, bevel=1.0,
             base=0.62)
    return Q(ln, 0)


def limb(cv, a, j, b, r0, r1, r2, mat, knee=None):
    cv.tcapsule(a, j, r0, r1, mat)
    cv.tcapsule(j, b, r1, r2, mat)
    if knee:
        cv.tdisc(j, knee, mat, bias=0.1)


def boot(cv, f, mat, big=1.0):
    x, y = f
    k = big
    cv.tpoly([(x - 6 * k, y - 7.5), (x + 3 * k, y - 7.5), (x + 10 * k, y - 2.5), (x + 10 * k, y + 0.9),
              (x - 7 * k, y + 0.9), (x - 7 * k, y - 4)], mat, bevel=1.8, base=0.6)


def _axes(a, b):
    ax, ay = b[0] - a[0], b[1] - a[1]
    L = math.hypot(ax, ay) or 1.0
    ux, uy = ax / L, ay / L
    return ux, uy, -uy, ux, L


# =================================================================== MAGMAROTH
MAG_HEAD = (11.0, -37.0)
def hammer_geo(fist, ang, L=18.0):
    """Maul head centre and its local axes (u = along haft, v = across)."""
    u = (math.cos(ang), math.sin(ang))
    v = (-u[1], u[0])
    hc = (fist[0] + u[0] * L, fist[1] + u[1] * L)
    return hc, u, v


HAMMER_U = (-6.0, 6.5)
HAMMER_V = 10.5


def hammer_extent(fist, ang):
    hc, u, v = hammer_geo(fist, ang)
    pts = []
    for uu in HAMMER_U:
        for vv in (-HAMMER_V, HAMMER_V):
            pts.append((hc[0] + u[0] * uu + v[0] * vv, hc[1] + u[1] * uu + v[1] * vv))
    return pts


def draw_hammer(cv, p):
    fist, ang = p.fist, p.wpn
    hc, u, v = hammer_geo(fist, ang)
    alive = p.dead < 0.7

    def Q(uu, vv):
        return (hc[0] + u[0] * uu + v[0] * vv, hc[1] + u[1] * uu + v[1] * vv)
    # haft (passes through the fist), pommel knob
    cv.part()
    pom = (fist[0] - u[0] * 7, fist[1] - u[1] * 7)
    cv.tcapsule(pom, Q(-4, 0), 2.0, 2.0, C.RUST_DK)
    cv.tdisc(pom, 2.5, C.RUST_M, bias=0.05)
    # head: chamfered basalt block, rusted iron bands, lava striking faces
    cv.part()
    hv = HAMMER_V
    u0, u1 = HAMMER_U
    cv.tpoly([Q(u0, -hv + 2), Q(u0 + 2, -hv), Q(u1 - 2, -hv), Q(u1, -hv + 2), Q(u1, hv - 2), Q(u1 - 2, hv),
              Q(u0 + 2, hv), Q(u0, hv - 2)], C.BAS_DK, bevel=2.4, base=0.6)
    cv.part()
    cv.tpoly([Q(u0 - 0.6, -2.2), Q(u1 + 0.6, -2.2), Q(u1 + 0.6, 2.2), Q(u0 - 0.6, 2.2)], C.RUST_DK, bevel=0.9,
             base=0.62)
    for uu in (u0 + 1.2, u1 - 1.2):
        cv.tpx((Q(uu, 0)[0] - 0.5, Q(uu, 0)[1] - 0.5), C.RUST_M, inten=0.95, decal=True)
    cv.part(line=False)
    g = min(1.6, p.wglow) if alive else 0.0
    face = C.LAVA_M if alive else C.SEAM_DEAD
    for sgn in (-1, 1):
        cv.tpoly([Q(u0 + 1.0, sgn * (hv + 0.4)), Q(u1 - 1.0, sgn * (hv + 0.4)), Q(u1 - 1.0, sgn * (hv - 1.6)),
                  Q(u0 + 1.0, sgn * (hv - 1.6))], face, flat=0.3 + 0.42 * g)
    if alive and g > 0.85:
        sm = C.SEAM
        gi = 0.3 + 0.4 * g
        cv.tline(Q(u0 + 1.5, -hv + 2.5), Q(-1.0, -4.5), sm, inten=gi, decal=True)
        cv.tline(Q(-1.0, -4.5), Q(u1 - 2.0, -3.0), sm, inten=gi, decal=True)
        cv.tline(Q(u1 - 1.5, hv - 2.5), Q(1.0, 4.5), sm, inten=gi, decal=True)
        cv.tline(Q(1.0, 4.5), Q(u0 + 2.0, 3.5), sm, inten=gi, decal=True)
    return hc


def draw_magmaroth(p):
    cv = TCanvas(W, H, rot=p.rot, center=p.rot_c, offset=p.offset)
    B, HD, sh_f, sh_b, hip_f, hip_b, hc = _frame(p, (17.0, -24.0), (-12.0, -25.0), (7.0, 3.0), (-7.0, 3.0),
                                                MAG_HEAD)
    alive = p.dead < 0.7
    g = p.glow if alive else 0.0
    seam = C.SEAM if p.dead < 0.4 else (C.SEAM_DIM if alive else C.SEAM_DEAD)

    def SL(a, b, k=1.0):
        cv.tline(B(*a), B(*b), seam, inten=min(0.95, (0.28 + 0.45 * g) * k), decal=True)

    # ------------------------------------------------ volcanic back hump + vents
    cv.part()
    hump = [B(-3, -31), B(-8, -39), B(-16, -42), B(-24, -37), B(-27, -27), B(-24, -15), B(-14, -11)]
    cv.tpoly(hump, C.BAS_DK, bevel=2.2, base=0.56)
    cv.part()
    for (bx, by, a, ln) in ((-10, -40, -1.75, 5.5), (-18, -41, -2.15, 6.5), (-25, -32, -2.7, 5.0),
                            (-26, -22, -2.95, 3.5)):
        spike(cv, B(bx, by), a + p.lean, ln * (1 + 0.3 * p.bristle), 2.4, C.BAS_DK, base_i=0.55)
    for (vx, vy, va) in ((-12.5, -42.0, -0.25), (-21.5, -38.5, -0.65)):
        cv.part()
        cv.tpoly([B(vx - 3.6, vy + 2.5), B(vx - 2.6, vy - 1.2), B(vx + 2.6, vy - 1.2), B(vx + 3.6, vy + 2.5)],
                 C.BAS, bevel=1.0, base=0.6)
        cv.part(line=False)
        cv.tellipse(B(vx, vy - 1.0), 2.4, 1.0, C.LAVA_M if alive else C.SEAM_DEAD, ang=p.lean,
                    flat=0.45 + 0.3 * g)
    SL((-6, -36), (-12, -33))
    SL((-12, -33), (-15, -27))
    SL((-22, -30), (-19, -23), 0.8)

    # ---------------------------------------------------------------- back arm
    cv.part()
    el_b = ik2(sh_b, p.hand_b, 12.5, 12.5, -1.0)
    limb(cv, sh_b, el_b, p.hand_b, 5.4, 4.6, 4.2, C.BAS_BK, knee=4.8)
    cv.part()
    ux, uy, nx, ny, _ = _axes(el_b, p.hand_b)
    hb = p.hand_b
    cv.tpoly([(hb[0] - ux * 2 + nx * 5, hb[1] - uy * 2 + ny * 5),
              (hb[0] + ux * 6 + nx * 5.5, hb[1] + uy * 6 + ny * 5.5),
              (hb[0] + ux * 7 - nx * 5, hb[1] + uy * 7 - ny * 5),
              (hb[0] - ux * 2 - nx * 5, hb[1] - uy * 2 - ny * 5)], C.BAS_BK, bevel=1.6, base=0.55)
    # ---------------------------------------------------------------- back leg
    cv.part()
    ank_b = (p.foot_b[0], p.foot_b[1] - 4.5)
    kn_b = ik2(hip_b, ank_b, 13.5, 13.5, 1.0)
    limb(cv, hip_b, kn_b, ank_b, 6.4, 5.4, 4.4, C.BAS_BK, knee=5.2)
    cv.part()
    boot(cv, p.foot_b, C.BAS_BK, 1.05)

    # -------------------------------------------------------------------- torso
    cv.part()
    torso = [B(-13, 2), B(-18, -10), B(-18, -24), B(-10, -32), B(9, -33), B(19, -26), B(20, -11), B(13, 2)]
    cv.tpoly(torso, C.BAS_DK, bevel=2.8, base=0.6)
    cv.part(line=False)
    cv.tline(B(-16, -24), B(-9, -30), C.BAS_BK, inten=0.2, decal=True)
    # lava seams between the plates
    SL((-17, -13), (-10, -12))
    SL((-10, -12), (-7, -7))
    SL((-7, -7), (-11, -1))
    SL((9, -9), (17, -12))
    SL((12, -27), (16, -20), 0.85)
    # furnace core: iron ring, molten heart, grate bars
    cc = B(0.5, -17.0)
    cv.part()
    cv.tdisc(cc, 8.0, C.RUST_DK, bias=-0.02)
    cv.part(line=False)
    cv.tdisc(cc, 6.0, C.SLOT_P, flat=0.3)
    cm = C.LAVA_M if alive else C.SEAM_DEAD
    cr = 4.4 + 0.5 * min(1.5, p.core) + p.chest_open * 1.0
    cv.tdisc(cc, cr, cm, bias=-0.32 + 0.48 * p.core)
    if alive and p.core > 0.2:
        cv.tdisc((cc[0] - 1.2, cc[1] - 1.2), 1.3 + 0.5 * p.core, cm, flat=0.97)
    cv.part()
    for k in (-3.0, 0.0, 3.0):
        if p.chest_open > 0.5 and k == 0.0:
            continue
        cv.tcapsule(B(0.5 + k, -23.0), B(0.5 + k, -11.0), 0.8, 0.8, C.RUST_DK)
    # rivets on the ring
    for k in range(6):
        a = k * math.pi / 3 + 0.4
        cv.tpx((cc[0] + math.cos(a) * 7.0, cc[1] + math.sin(a) * 7.0), C.RUST_M, inten=0.9, decal=True)
    # belt + tassets + hanging chain
    cv.part()
    cv.tpoly([B(-14, -2), B(14, -2), B(15, 2), B(-15, 2)], C.RUST_DK, bevel=1.0, base=0.58)
    cv.part()
    cv.tpoly([B(-12, 1), B(13, 1), B(16, 10), B(4, 12), B(-3, 11), B(-15, 9)], C.BAS_DK, bevel=1.8, base=0.58)
    SL((1, 2), (0, 10), 0.8)
    cv.part()
    sw = math.sin(p.t * 2 * math.pi) * 1.2
    links = [B(-12, 1), B(-11 + sw * 0.3, 6), B(-8 + sw * 0.6, 10), B(-4 + sw, 11), B(1 + sw * 0.6, 9), B(4, 4)]
    for i, q in enumerate(links[1:-1]):
        cv.tellipse(q, 1.6 if i % 2 == 0 else 1.0, 1.0 if i % 2 == 0 else 1.6, C.RUST_M, ang=p.lean)

    # --------------------------------------------------------------- front leg
    cv.part()
    ank_f = (p.foot_f[0], p.foot_f[1] - 4.5)
    kn_f = ik2(hip_f, ank_f, 13.5, 13.5, 1.0)
    limb(cv, hip_f, kn_f, ank_f, 6.8, 5.6, 4.6, C.BAS_DK)
    cv.part()
    cv.tdisc(kn_f, 5.4, C.BAS, bias=0.05)
    spike(cv, (kn_f[0] + 3.5, kn_f[1] - 1.5), -0.35, 4.0, 1.8, C.BAS)
    cv.part(line=False)
    cv.tline((kn_f[0] - 2, kn_f[1] + 4), (kn_f[0] + 1, kn_f[1] + 9), seam, inten=0.3 + 0.4 * g, decal=True)
    cv.part()
    boot(cv, p.foot_f, C.BAS_DK, 1.1)

    # -------------------------------------------------------------------- head
    # far horn behind the helm
    cv.part()
    hf = [HD(2, -5), HD(-2, -10), HD(-3, -16), HD(1, -21), HD(6, -23)]
    cv.tchain(hf, [2.6, 2.4, 1.9, 1.2, 0.5], C.OBS_DK)
    cv.part()
    helm = [HD(-8, -6), HD(-3, -9), HD(6, -9), HD(11, -5), HD(12, 2), HD(9, 7.5), HD(-4, 8.5), HD(-9, 3)]
    cv.tpoly(helm, C.BAS, bevel=2.0, base=0.6)
    # brow ridge
    cv.part()
    cv.tpoly([HD(-1, -8), HD(7, -8.5), HD(13.5, -4.5), HD(13, -2), HD(6, -2.8), HD(0, -4)], C.BAS_DK, bevel=1.2,
             base=0.55)
    # deep eye socket + burning eye
    cv.part(line=False)
    cv.tpoly([HD(3, -2.6), HD(12, -2.4), HD(11.5, 0.6), HD(3.5, 0.2)], C.SLOT_P, flat=0.3)
    if alive:
        er = 0.9 + 0.45 * min(1.5, p.eye)
        cv.tcapsule(HD(6.0, -1.2), HD(11.0, -1.0), er, er * 0.75, C.EYE_FIRE, flat=0.45 + 0.35 * min(1.0, p.eye))
    # furnace maw (jaw): grille that opens
    j = p.jaw
    cv.part()
    cv.tpoly([HD(1, 2.5 + j * 2.4), HD(12, 2.5 + j * 3.6), HD(10, 8 + j * 4.6), HD(-2, 8 + j * 2.4)], C.BAS_DK,
             bevel=1.2, base=0.55)
    if alive and (j > 0.08 or g > 1.0):
        cv.part(line=False)
        jo = max(j, 0.25)
        cv.tpoly([HD(2, 1.2), HD(12, 1.2), HD(12, 1.6 + jo * 3.6), HD(2, 1.6 + jo * 2.4)], C.LAVA_M,
                 flat=0.45 + 0.35 * min(1.2, g))
    for k in range(3):
        cv.tline(HD(3.5 + 2.4 * k, 4.0 + j * 2.6), HD(3.0 + 2.4 * k, 7.0 + j * 3.0), C.SLOT_P, inten=0.3, decal=True)
    cv.part(line=False)
    cv.tline(HD(-6, -4), HD(-3, 3), seam, inten=0.3 + 0.4 * g, decal=True)
    # near horn: big obsidian crescent curving up and forward
    cv.part()
    hn = [HD(-3, -3), HD(-9, -7), HD(-12, -13), HD(-10, -20), HD(-5, -24.5), HD(1, -26)]
    cv.tchain(hn, [3.4, 3.2, 2.7, 2.0, 1.3, 0.6], C.OBS_M)
    cv.part()
    cv.tdisc(HD(-3.5, -3), 3.4, C.RUST_DK, bias=0.05)

    # ------------------------------------------------------------------ hammer
    if not p.no_weapon:
        draw_hammer(cv, p)

    # ---------------------------------------------------------- huge front arm
    el_f = ik2(sh_f, p.fist, 15.5, 15.5, -p.elbow_dir)
    cv.part()
    cv.tcapsule(sh_f, el_f, 7.0, 6.2, C.BAS)
    cv.part()
    cv.tdisc(el_f, 6.4, C.BAS_DK, bias=0.12)
    cv.part(line=False)
    cv.tline((el_f[0] - 3, el_f[1] - 2), (el_f[0] + 2, el_f[1] + 3), seam, inten=0.3 + 0.4 * g, decal=True)
    cv.part()
    fx, fy = p.fist
    ux, uy, nx, ny, _ = _axes(el_f, p.fist)
    g0 = (el_f[0] + ux * 2, el_f[1] + uy * 2)
    g1 = (fx - ux * 4, fy - uy * 4)
    cv.tpoly([(g0[0] + nx * 6.2, g0[1] + ny * 6.2), (g1[0] + nx * 8.0, g1[1] + ny * 8.0),
              (g1[0] - nx * 8.0, g1[1] - ny * 8.0), (g0[0] - nx * 6.2, g0[1] - ny * 6.2)], C.BAS, bevel=2.4, base=0.6)
    cv.part(line=False)
    mid = ((g0[0] + g1[0]) / 2, (g0[1] + g1[1]) / 2)
    cv.tline((mid[0] + nx * 4, mid[1] + ny * 4), (mid[0] - ux * 2 - nx * 1, mid[1] - uy * 2 - ny * 1), seam,
             inten=0.3 + 0.45 * g, decal=True)
    cv.tline((mid[0] - ux * 2 - nx * 1, mid[1] - uy * 2 - ny * 1), (g1[0] - nx * 5, g1[1] - ny * 5), seam,
             inten=0.3 + 0.45 * g, decal=True)
    # rusted cuff
    cv.part()
    cv.tcapsule((g1[0] + nx * 8.5, g1[1] + ny * 8.5), (g1[0] - nx * 8.5, g1[1] - ny * 8.5), 2.0, 2.0, C.RUST_M)
    # fist wrapped around the haft
    cv.part()

    def FQ(a, b):  # a along forearm, b across
        return (fx + ux * a + nx * b, fy + uy * a + ny * b)
    cv.tpoly([FQ(-4, -6.5), FQ(3, -7), FQ(6.5, -4), FQ(6.5, 4), FQ(3, 7), FQ(-4, 6.5)], C.BAS, bevel=2.0, base=0.6)
    for k in (-3.0, 0.0, 3.0):
        cv.tline(FQ(1.5, k), FQ(6.0, k), C.BAS_BK, inten=0.2, decal=True)
    # pauldron: craggy basalt boulder with a lava crack and spikes
    cv.part()
    pc = (sh_f[0] + 0.5, sh_f[1] + 0.5)
    cv.tellipse(pc, 11.5, 8.0, C.BAS, ang=p.lean * 0.6 - 0.12, bias=0.04)
    cv.part()
    cv.tellipse((pc[0] + 1.0, pc[1] + 5.5), 10.5, 3.6, C.BAS_DK, ang=p.lean * 0.6 - 0.08)
    cv.part()
    for ang, ln in ((-2.2, 6.0), (-1.6, 7.5), (-1.0, 5.5)):
        q = (pc[0] + math.cos(ang) * 9.5, pc[1] + math.sin(ang) * 6.5)
        spike(cv, q, ang + 0.25, ln * (1 + 0.35 * p.bristle), 2.6, C.BAS, base_i=0.58)
    cv.part(line=False)
    cv.tline((pc[0] - 8, pc[1] - 2), (pc[0] - 2, pc[1] + 1), seam, inten=0.3 + 0.45 * g, decal=True)
    cv.tline((pc[0] - 2, pc[1] + 1), (pc[0] + 3, pc[1] - 5), seam, inten=0.3 + 0.45 * g, decal=True)
    cv.tline((pc[0] + 3, pc[1] - 5), (pc[0] + 8, pc[1] - 3), seam, inten=0.3 + 0.45 * g, decal=True)

    # ------------------------------------------------------------------ cracks
    if p.cracks:
        cv.part(line=False)
        cm2 = C.SEAM if p.dead < 0.7 else C.SEAM_DEAD
        segs = [((3, -28), (-1, -24)), ((8, -10), (12, -5)), ((-6, -20), (-10, -17)), ((15, -18), (18, -15))]
        if p.cracks > 1:
            segs += [((-15, -20), (-11, -15)), ((-4, -30), (-8, -26)), ((-5, -4), (-1, 0)), ((10, -30), (14, -27))]
        for a, b in segs:
            cv.tline(B(*a), B(*b), cm2, inten=0.85, decal=True)
    return cv.render()


def magma_mouth(p):
    """Canvas position of the furnace maw (flame-breath origin)."""
    _, HD, *_ = _frame(p, (17.0, -24.0), (-12.0, -25.0), (7.0, 3.0), (-7.0, 3.0), MAG_HEAD)
    return HD(12.5, 2.5 + p.jaw * 2.0)


def magma_core(p):
    B, *_ = _frame(p, (17.0, -24.0), (-12.0, -25.0), (7.0, 3.0), (-7.0, 3.0), MAG_HEAD)
    return B(0.5, -17.0)


def magma_vents(p):
    B, *_ = _frame(p, (17.0, -24.0), (-12.0, -25.0), (7.0, 3.0), (-7.0, 3.0), MAG_HEAD)
    return [B(-13, -42.5), B(-21.5, -39.5)]


# =================================================================== GLACIARCH
GLA_HEAD = (3.5, -40.5)
SWORD_L = 31.0    # blade length from the crossguard


def sword_geo(fist, ang, lost=0.0):
    u = (math.cos(ang), math.sin(ang))
    guard = (fist[0] + u[0] * 4.5, fist[1] + u[1] * 4.5)
    ln = SWORD_L * (1 - lost)
    tip = (guard[0] + u[0] * ln, guard[1] + u[1] * ln)
    return guard, tip, u


def draw_sword(cv, p):
    fist, ang = p.fist, p.wpn
    guard, tip, u = sword_geo(fist, ang, p.broken_weapon)
    v = (-u[1], u[0])
    alive = p.dead < 0.7

    def Q(a, b, o=guard):
        return (o[0] + u[0] * a + v[0] * b, o[1] + u[1] * a + v[1] * b)
    # pommel + grip
    cv.part()
    pom = (fist[0] - u[0] * 6.5, fist[1] - u[1] * 6.5)
    cv.tcapsule(pom, guard, 1.4, 1.4, C.STL_DK)
    cv.tdisc(pom, 2.1, C.GEM_AUR if alive else C.ICE_DEAD, flat=0.6)
    # blade: broad crystal blade, central ridge, glowing edge
    cv.part()
    ln = SWORD_L * (1 - p.broken_weapon)
    wb = 3.8
    bm = C.CRYST if alive else C.ICE_DK
    if p.broken_weapon > 0.01:
        pts = [Q(0, -wb), Q(ln - 1, -wb * 0.82), Q(ln + 1.5, -1.0), Q(ln - 0.5, 0.8), Q(ln + 1, wb * 0.8), Q(0, wb)]
    else:
        pts = [Q(0, -wb), Q(ln - 7, -wb * 0.78), Q(ln, 0), Q(ln - 7, wb * 0.78), Q(0, wb)]
    cv.tpoly(pts, bm, bevel=1.3, base=0.52 + 0.12 * min(1.4, p.wglow))
    cv.part(line=False)
    cv.tline(Q(1.5, -0.6), Q(ln - 5, -0.4), C.FROST_GLOW if alive else C.ICE_M,
             inten=0.55 + 0.3 * min(1.0, p.wglow), decal=True)
    # crossguard with crystal quillons
    cv.part()
    cv.tcapsule(Q(-0.5, -6.0), Q(-0.5, 6.0), 1.6, 1.6, C.STL)
    for sgn in (-1, 1):
        crystal(cv, Q(-0.5, sgn * 6.0), math.atan2(u[1], u[0]) + sgn * 0.9, 4.5 + 2 * p.bristle, 1.6,
                C.CRYST if alive else C.ICE_DK)
    cv.part(line=False)
    cv.tdisc(Q(-0.3, 0), 1.6, C.GEM_AUR if alive else C.ICE_DEAD, flat=0.5 + 0.3 * min(1.0, p.wglow))
    return tip


def draw_glaciarch(p):
    cv = TCanvas(W, H, rot=p.rot, center=p.rot_c, offset=p.offset)
    B, HD, sh_f, sh_b, hip_f, hip_b, hc = _frame(p, (14.0, -26.0), (-9.0, -27.0), (5.5, 2.0), (-5.5, 2.0),
                                                GLA_HEAD)
    alive = p.dead < 0.7
    g = p.glow if alive else 0.0
    cm = C.CRYST if alive else C.ICE_DK

    # ------------------------------------------------------------ frozen cape
    cv.part()
    wv, lift = p.cape_wave, p.cape_lift
    cape = [B(-4, -33), B(-11, -31), B(-16, -18), B(-21 - 3 * lift, -2 - 4 * lift),
            B(-25 - 7 * lift, 14 - 10 * lift), B(-27 - 11 * lift + wv, 26 - 16 * lift)]
    hem = [(-27 - 11 * lift + wv, 26 - 16 * lift), (-22 - 9 * lift + wv * 0.8, 24 - 13 * lift),
           (-18 - 8 * lift + wv * 0.6, 28 - 12 * lift), (-13 - 6 * lift + wv * 0.4, 24 - 9 * lift),
           (-9 - 4 * lift + wv * 0.2, 27 - 8 * lift), (-5 - 2 * lift, 21 - 5 * lift)]
    cape += [B(*q) for q in hem[1:]] + [B(-4, 4), B(-2, -24)]
    cv.tpoly(cape, C.CAPE, bevel=2.2, base=0.56)
    # frost rim along the back edge + icicles on the hem
    cv.part(line=False)
    for a, b in zip(cape[1:5], cape[2:6]):
        cv.tline((a[0] + 1, a[1]), (b[0] + 1, b[1]), C.SNOW_M, inten=0.6, decal=True)
    cv.part()
    for i, q in enumerate(hem):
        if i % 2 == 0 or i == len(hem) - 1:
            bq = B(*q)
            ln = (3.5 + (i % 3)) * (1 - 0.5 * lift)
            spike(cv, (bq[0], bq[1] - 0.5), math.pi / 2 + p.lean + 0.3 * lift + 0.05 * wv, ln, 1.3, C.ICE_M)

    # ---------------------------------------------------------------- back arm
    cv.part()
    el_b = ik2(sh_b, p.hand_b, 12.0, 12.0, -1.0)
    limb(cv, sh_b, el_b, p.hand_b, 4.0, 3.4, 3.0, C.STL_BK, knee=3.6)
    cv.part()
    ux, uy, nx, ny, _ = _axes(el_b, p.hand_b)
    for k in (-0.45, 0.0, 0.45):
        a = math.atan2(uy, ux) + k
        cv.tcapsule(p.hand_b, (p.hand_b[0] + math.cos(a) * 4.5, p.hand_b[1] + math.sin(a) * 4.5), 1.3, 0.6,
                    C.STL_BK)
    # ---------------------------------------------------------------- back leg
    cv.part()
    ank_b = (p.foot_b[0], p.foot_b[1] - 4.5)
    kn_b = ik2(hip_b, ank_b, 14.5, 14.5, 1.0)
    limb(cv, hip_b, kn_b, ank_b, 4.8, 4.0, 3.2, C.STL_BK, knee=4.0)
    cv.part()
    boot(cv, p.foot_b, C.STL_BK, 0.9)

    # -------------------------------------------------------------------- torso
    cv.part()
    torso = [B(-9, 2), B(-12, -10), B(-13, -25), B(-7, -34), B(8, -35), B(15, -27), B(13, -12), B(8, 2)]
    cv.tpoly(torso, C.STL_DK, bevel=2.6, base=0.62)
    cv.part(line=False)
    cv.tline(B(-11, -12), B(-2, -11), C.STL_BK, inten=0.15, decal=True)
    cv.tline(B(5, -11), B(12, -13), C.STL_BK, inten=0.15, decal=True)
    cv.tline(B(-10, -26), B(-4, -31), C.STL, inten=0.95, decal=True)
    # frost heart: diamond crystal in a steel setting, crystal petals opening
    cc = B(0.5, -19.0)
    co = p.chest_open
    rot = p.lean
    cr_, sr_ = math.cos(rot), math.sin(rot)

    def D(x, y, o=cc):
        return (o[0] + x * cr_ - y * sr_, o[1] + x * sr_ + y * cr_)
    cv.part()
    cv.tpoly([D(0, -9.5), D(7, 0), D(0, 9.5), D(-7, 0)], C.STL, bevel=1.4, base=0.6)
    cv.part(line=False)
    cv.tpoly([D(0, -7.5), D(5.2, 0), D(0, 7.5), D(-5.2, 0)], C.SLOT_G, flat=0.3)
    hm = C.FROST_GLOW if alive else C.ICE_DEAD
    k = 1.0 + 0.12 * min(1.5, p.core) + 0.25 * co
    cv.tpoly([D(0, -6 * k), D(4 * k, 0), D(0, 6 * k), D(-4 * k, 0)], hm, bevel=1.5, base=0.25 + 0.42 * p.core)
    if alive and p.core > 0.3:
        cv.tpoly([D(-0.5, -3.5 * k), D(1.2 * k, -1), D(-0.5, 0.5), D(-2.0 * k, -1)], hm, flat=0.97)
    if co > 0.05:
        cv.part()
        for i in range(4):
            a = -math.pi / 2 + i * math.pi / 2 + math.pi / 4 + rot
            d = 6.0 + 4.0 * co
            q = (cc[0] + math.cos(a) * d, cc[1] + math.sin(a) * d)
            crystal(cv, q, a, 3.0 + 3.5 * co, 1.5, cm)
    # ice crust growing on the back of the chest
    cv.part()
    for (bx, by, a, ln) in ((-12, -21, -2.6, 6.0), (-13, -15, -2.9, 4.5), (-10, -29, -2.2, 5.0)):
        crystal(cv, B(bx, by), a + p.lean, ln * (1 + 0.4 * p.bristle), 1.8, cm)
    # belt with aurora gem + faulds with frost rim
    cv.part()
    cv.tpoly([B(-10, -3), B(10, -3), B(10, 1), B(-10, 1)], C.STL_BK, bevel=1.0, base=0.6)
    cv.part(line=False)
    cv.tdisc(B(1, -1), 1.7, C.GEM_AUR if alive else C.ICE_DEAD, flat=0.55 + 0.25 * min(1.0, g))
    cv.part()
    cv.tpoly([B(-10, 0), B(10, 0), B(13, 13), B(4, 15), B(-2, 14), B(-12, 12)], C.STL_DK, bevel=1.8, base=0.6)
    cv.part(line=False)
    cv.tline(B(1, 1), B(1, 14), C.STL_BK, inten=0.1, decal=True)
    cv.tline(B(-11, 12), B(-2, 13.5), C.SNOW_M, inten=0.75, decal=True)
    cv.tline(B(4, 14.5), B(12, 12.5), C.SNOW_M, inten=0.75, decal=True)

    # --------------------------------------------------------------- front leg
    cv.part()
    ank_f = (p.foot_f[0], p.foot_f[1] - 4.5)
    kn_f = ik2(hip_f, ank_f, 14.5, 14.5, 1.0)
    limb(cv, hip_f, kn_f, ank_f, 5.2, 4.2, 3.4, C.STL_DK)
    cv.part()
    cv.tdisc(kn_f, 4.2, C.STL, bias=0.05)
    crystal(cv, (kn_f[0] + 2.5, kn_f[1] - 1.5), -0.5, 5.0 + 2 * p.bristle, 1.6, cm)
    cv.part()
    boot(cv, p.foot_f, C.STL_DK, 0.95)
    cv.part()
    crystal(cv, (p.foot_f[0] - 4, p.foot_f[1] - 7.5), -1.9, 3.5, 1.3, C.ICE_M)

    # -------------------------------------------------------------------- head
    cv.part()
    # gorget / mantle of snow-white fur-frost around the neck
    cv.tellipse(B(2.5, -33.5), 9.5, 4.0, C.SNOW_M, ang=p.lean, bias=0.0)
    cv.part()
    helm = [HD(-6, -6), HD(-3, -9), HD(5, -9), HD(8.5, -5), HD(8.5, 3), HD(6.5, 8), HD(-3, 8.5), HD(-6.5, 3)]
    cv.tpoly(helm, C.STL, bevel=2.0, base=0.62)
    # T visor
    cv.part(line=False)
    cv.tpoly([HD(1.0, -3.0), HD(9, -3.2), HD(9, 0.2), HD(1.0, 0.4)], C.SLOT_G, flat=0.3)
    for k in range(3):
        cv.tline(HD(4.0 + 1.8 * k, 3.0), HD(4.0 + 1.8 * k, 5.5), C.SLOT_G, inten=0.3, decal=True)
    if alive:
        er = 0.75 + 0.4 * min(1.5, p.eye)
        cv.tcapsule(HD(3.0, -1.4), HD(8.6, -1.5), er, er * 0.8, C.EYE_ICE, flat=0.5 + 0.3 * min(1.0, p.eye))
    # breath / jaw: frost mist from the visor when roaring
    if p.jaw > 0.1 and alive:
        cv.part(line=False)
        cv.tpoly([HD(3.4, 2.4), HD(9.0, 2.4), HD(9.0, 3 + 4 * p.jaw), HD(3.4, 3 + 3 * p.jaw)], C.FROST_GLOW, flat=0.7)
    cv.part(line=False)
    cv.tline(HD(-5, -5), HD(-2, -8), C.STL, inten=0.95, decal=True)
    # frost crown: ring of crystal spikes, aurora gem
    cv.part()
    cv.tpoly([HD(-6.5, -6), HD(8.5, -6.5), HD(8.5, -9.5), HD(-6.5, -9.0)], C.STL_DK, bevel=1.0, base=0.6)
    cv.part()
    lens = (5.0, 8.0, 11.0, 8.5, 5.5)
    xs = (-5.5, -2.5, 1.0, 4.5, 7.5)
    angs = (-2.15, -1.9, -1.62, -1.35, -1.1)
    for x, ln, a in zip(xs, lens, angs):
        crystal(cv, HD(x, -9.0), a + p.lean + p.head_tilt, ln * (1 + 0.12 * p.bristle) * (1 - 0.6 * p.dead), 1.7,
                cm)
    cv.part(line=False)
    cv.tdisc(HD(1.0, -7.8), 1.8, C.GEM_AUR if alive else C.ICE_DEAD, flat=0.5 + 0.35 * min(1.0, g))

    # ------------------------------------------------------------------- sword
    if not p.no_weapon:
        draw_sword(cv, p)

    # ------------------------------------------------------------- front arm
    el_f = ik2(sh_f, p.fist, 14.0, 14.0, -p.elbow_dir)
    cv.part()
    cv.tcapsule(sh_f, el_f, 5.0, 4.4, C.STL)
    cv.part()
    cv.tdisc(el_f, 4.6, C.STL_DK, bias=0.12)
    cv.part()
    fx, fy = p.fist
    ux, uy, nx, ny, _ = _axes(el_f, p.fist)
    g0 = (el_f[0] + ux * 2, el_f[1] + uy * 2)
    g1 = (fx - ux * 3, fy - uy * 3)
    cv.tpoly([(g0[0] + nx * 4.6, g0[1] + ny * 4.6), (g1[0] + nx * 5.8, g1[1] + ny * 5.8),
              (g1[0] - nx * 5.8, g1[1] - ny * 5.8), (g0[0] - nx * 4.6, g0[1] - ny * 4.6)], C.STL, bevel=2.0, base=0.62)
    # ice crystals growing out of the forearm toward the blade
    cv.part()
    fa = math.atan2(uy, ux)
    for tpos, side, ln in ((0.25, -1, 5.0), (0.55, -1, 6.5), (0.8, 1, 4.0), (0.45, 1, 3.5)):
        q = (g0[0] + (g1[0] - g0[0]) * tpos + nx * side * 4.0, g0[1] + (g1[1] - g0[1]) * tpos + ny * side * 4.0)
        crystal(cv, q, fa + side * 1.1 + 0.25, ln * (1 + 0.4 * p.bristle), 1.5, cm)
    # gauntlet
    cv.part()

    def FQ(a, b):
        return (fx + ux * a + nx * b, fy + uy * a + ny * b)
    cv.tpoly([FQ(-3.5, -5), FQ(2.5, -5.5), FQ(5.5, -3), FQ(5.5, 3), FQ(2.5, 5.5), FQ(-3.5, 5)], C.STL, bevel=1.8,
             base=0.62)
    for k in (-2.5, 0.0, 2.5):
        cv.tline(FQ(1.5, k), FQ(5.0, k), C.STL_BK, inten=0.2, decal=True)
    # pauldron: layered steel with tall crystals
    cv.part()
    pc = (sh_f[0] + 0.5, sh_f[1] - 1.0)
    cv.tpoly([(pc[0] - 7, pc[1] + 2), (pc[0] - 4, pc[1] - 5), (pc[0] + 3, pc[1] - 6.5), (pc[0] + 8.5, pc[1] - 2),
              (pc[0] + 8.5, pc[1] + 3), (pc[0] + 2, pc[1] + 5.5), (pc[0] - 6, pc[1] + 5)], C.STL, bevel=1.8, base=0.62)
    cv.part()
    cv.tpoly([(pc[0] - 6, pc[1] + 3.5), (pc[0] + 8, pc[1] + 1.5), (pc[0] + 9.5, pc[1] + 6), (pc[0] + 2, pc[1] + 8.5),
              (pc[0] - 5, pc[1] + 7)], C.STL_DK, bevel=1.3, base=0.6)
    cv.part(line=False)
    cv.tline((pc[0] - 5, pc[1] + 6.5), (pc[0] + 2, pc[1] + 8), C.SNOW_M, inten=0.8, decal=True)
    cv.part()
    for (dx, dy, a, ln) in ((-4, -5, -1.95, 7.0), (0.5, -6.5, -1.55, 10.0), (5, -5, -1.2, 6.5)):
        crystal(cv, (pc[0] + dx, pc[1] + dy), a + p.lean * 0.6, ln * (1 + 0.35 * p.bristle) * (1 - 0.5 * p.dead),
                2.0, cm)

    # ------------------------------------------------------------------ cracks
    if p.cracks:
        cv.part(line=False)
        cm2 = C.FROST_GLOW if alive else C.SLOT_G
        segs = [((3, -30), (0, -26)), ((7, -9), (11, -5)), ((-6, -22), (-9, -18)), ((10, -22), (13, -18))]
        if p.cracks > 1:
            segs += [((-11, -14), (-8, -9)), ((-4, -32), (-7, -28)), ((-4, 4), (-1, 9)), ((9, -30), (12, -27))]
        for a, b in segs:
            cv.tline(B(*a), B(*b), cm2, inten=0.85, decal=True)
    return cv.render()


def glaciarch_core(p):
    B, *_ = _frame(p, (14.0, -26.0), (-9.0, -27.0), (5.5, 2.0), (-5.5, 2.0), GLA_HEAD)
    return B(0.5, -19.0)


def glaciarch_head(p):
    _, HD, *_ = _frame(p, (14.0, -26.0), (-9.0, -27.0), (5.5, 2.0), (-5.5, 2.0), GLA_HEAD)
    return HD(8.5, 0.0)


def glaciarch_crown(p):
    _, HD, *_ = _frame(p, (14.0, -26.0), (-9.0, -27.0), (5.5, 2.0), (-5.5, 2.0), GLA_HEAD)
    return HD(1.0, -8.0)
