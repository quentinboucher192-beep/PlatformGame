"""The tooling shared by the stories told in animated scenes (version 2 of data/stories/*.json, 2.60).

Pyrrhax's story was the first (story_pyrrhax_scene.py); the six other giga bosses' stories follow
it (2.63: story_<boss>_scene.py).  Each one has its sprites (characters/story_<boss>_sprites.py),
its layers of scenery (animated sheets, story_scenery.py) and its six shots; this module holds what
they share:

  * the helpers that write a scene (keys, layers, sprites, glows, particles, shadows) — `Kit`;
  * the renderer of a stage at a time (the rule of BossStories::elementState and of the game's
    drawing: parallax, camera kept inside the stage, pulses, shadows, flashes), used for the stills
    (stories/<boss>_<n>.png: the shot's image when its scene cannot be played, the thumbnails);
  * the writer of a story file (its lines of narration kept as written) and of its sheets
    (assets/stories/<boss>/<name>.png, assets/animations/stories/<boss>_<name>.json).

The format (BossStories.h): a shot's « stage » has a size (960x540 units), a backdrop colour, camera
keys {t, pos, zoom}, shakes {from, to, amount}, flashes {t, in, out, color, alpha} and elements
drawn in order.  An element is a sprite (an animation file), a glow (a colour) or particles (a kind
of the story's particles in an area); it has a depth (parallax: 0 never moves, 1 moves with the
camera, more is in front), a blend, a tint and keys {t, pos, scale, alpha, rot, flip, clip, offset,
ease}.  One sheet pixel is two units (scale 1).  The times are seconds of the shot as written in
French (max(4.5 s, 1.2 s + characters / 16)).  Deterministic (fixed seeds)."""
import json
import math
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
ASSETS = os.path.join(REPO, "assets")
W, H = 960, 540
PIX = 2.0  # units per image pixel


def chars_path():
    """Lets the scenes import the sprite modules (tools/assetgen/characters)."""
    chars = os.path.join(os.path.dirname(HERE), "characters")
    if chars not in sys.path:
        sys.path[:0] = [chars, os.path.dirname(HERE)]


def shot_duration(text):
    return max(4.5, 1.2 + len(text) / 16.0)


# =================================================================== helpers to write scenes
def K(t, **kw):
    k = {"t": round(float(t), 3)}
    for name, v in kw.items():
        if v is None:
            continue
        if name == "pos":
            v = [round(float(v[0]), 1), round(float(v[1]), 1)]
        elif isinstance(v, float):
            v = round(v, 3)
        k[name] = v
    return k


def shade(width, light=None, alpha=0.45):
    """A soft shadow on the ground under a sprite, pushed away from `light`."""
    sh = {"alpha": alpha, "size": [float(width), round(width * 0.16, 1)]}
    if light:
        sh["light"] = [float(light[0]), float(light[1])]
    return sh


def glow(id_, color, radius, keys, depth=1.0, pulse=None):
    e = {"id": id_, "glow": color, "radius": float(radius), "depth": depth, "keys": keys}
    if pulse:
        e["pulse"] = [float(v) for v in pulse]
    return e


def particles(id_, kind, color, area, keys, depth=1.0):
    return {"id": id_, "particles": kind, "color": color, "area": [float(v) for v in area], "depth": depth, "keys": keys}


def sfx(sound, t, volume=None, pan=None, at=None, loop=False, to=None, fade=None, duck=False, pitch=None):
    """A sound of a scene (2.63): `sound` a name of assets/sounds (or a whole path), heard at `t`
    (seconds of the French line); a loop plays from `t` to `to` (the shot's end when None), fading
    in and out; `at` a point of the stage (panned by its place on the screen); `duck`: a big
    sound (the music lowers a little)."""
    s = {"sound": sound if "/" in sound else f"sounds/{sound}.wav", "t": round(float(t), 3)}
    for k, v in (("volume", volume), ("pitch", pitch), ("pan", pan), ("to", to), ("fade", fade)):
        if v is not None:
            s[k] = round(float(v), 3)
    if at is not None:
        s["at"] = [round(float(at[0]), 1), round(float(at[1]), 1)]
    if loop:
        s["loop"] = True
    if duck:
        s["duck"] = True
    return s


def amb(sound, volume=0.45, t=0.0, to=None, fade=0.6):
    """An ambience under a shot (a loop of story_amb_*)."""
    return sfx(sound, t, volume=volume, loop=True, to=to, fade=fade)


def appear(t, dur=0.01):
    """Keys that make an element pop in at t (alpha 0 -> 1, a step)."""
    return [K(max(0.0, t - dur), alpha=0.0, ease="step"), K(t, alpha=1.0, ease="step")]


class Kit:
    """The helpers that name a boss's sheets (animations/stories/<boss>_<name>.json)."""

    def __init__(self, boss):
        self.boss = boss
        self.A = f"animations/stories/{boss}_"

    def layer(self, id_, image, depth, pos, alpha=1.0, drift=None, repeat=False, tint=None, keys=None, blend=None,
              offset=0.0, pulse=None):
        """A layer of scenery: an animated sheet (its point at its top-left)."""
        e = {"id": id_, "sprite": self.A + image + ".json", "depth": depth}
        if drift:
            e["drift"] = [float(drift[0]), float(drift[1])]
        if repeat:
            e["repeat"] = True
        if tint:
            e["tint"] = tint
        if blend:
            e["blend"] = blend
        if pulse:
            e["pulse"] = [float(v) for v in pulse]
        if keys:
            keys[0].setdefault("clip", "idle")
            if offset:
                keys[0]["offset"] = round(float(offset), 3)
        e["keys"] = keys or [K(0, pos=pos, alpha=alpha if alpha != 1.0 else None, clip="idle", offset=offset or None)]
        return e

    def sprite(self, id_, anim, keys, depth=1.0, blend=None, tint=None, shadow=None, pulse=None, drift=None):
        e = {"id": id_, "sprite": anim if anim.endswith(".json") else self.A + anim + ".json", "depth": depth}
        if blend:
            e["blend"] = blend
        if tint:
            e["tint"] = tint
        if shadow:
            e["shadow"] = shadow
        if pulse:
            e["pulse"] = [float(v) for v in pulse]
        if drift:
            e["drift"] = [float(drift[0]), float(drift[1])]
        e["keys"] = keys
        return e

    def twinkles(self, seed, n, area, depth=0.08, name="astre", prefix="astre"):
        rng = np.random.default_rng(seed)
        out = []
        for i in range(n):
            x = rng.uniform(area[0], area[0] + area[2])
            y = rng.uniform(area[1], area[1] + area[3])
            out.append(self.sprite(f"{prefix}{i + 1}", name, [K(0, pos=(x, y), clip="twinkle", offset=float(rng.uniform(0, 1.2)),
                                                                scale=float(rng.choice([1.0, 1.0, 1.5])))], depth=depth))
        return out


# =================================================================== the story file
def stages(story, shots, sounds=None):
    out = []
    for shot, fn in zip(story["shots"], shots):
        D = shot_duration(shot["text"])
        Dt = math.floor(D * 1000) / 1000
        st = fn(Dt)
        if sounds and fn.__name__ in sounds:  # 2.63: its sounds
            st["sfx"] = sounds[fn.__name__](Dt)
        still = st.pop("still")
        stage = {"size": [W, H]}
        stage.update(st)
        out.append((stage, still))
    return out


def story_path(boss):
    return os.path.join(REPO, "data", "stories", f"{boss}.json")


def write_story(boss, built, module):
    path = story_path(boss)
    story = json.load(open(path, encoding="utf-8"))
    story["version"] = 2
    story["comment"] = ("Story of a giga boss (game/content/BossStories.h), told before the first fight against it. Version 2: "
                        "each shot is an animated scene (« stage »: parallax layers, sprites moved by keyframes, glows, "
                        f"particles, camera, shakes, flashes), written by tools/assetgen/environment/{module}.py "
                        "(edit it there); its image is the still rendered from the scene. A shot lasts max(4.5 s, 1.2 s + "
                        "characters / 16); then the title card (3 s).")
    for shot, (stage, _) in zip(story["shots"], built):
        shot["particles"] = "none"
        shot["zoom"] = 1.04
        shot["stage"] = stage
    with open(path, "w", encoding="utf-8") as f:
        json.dump(story, f, ensure_ascii=False, indent=1)
        f.write("\n")
    return story


# =================================================================== sheets
def sheet_from(texture, fw, fh, pivot, clips):
    chars_path()
    from sheet import Sheet
    sh = Sheet(texture, fw, fh, pivot)
    for name, frames, meta in clips:
        meta = dict(meta)
        loop = meta.pop("loop", True)
        sh.add(name, frames, fps=meta.get("fps"), durations=meta.get("durations"), loop=loop, next=meta.get("next"))
    return sh


def build_sheets(boss, sprites, layers):
    """Writes a story's sheets: `sprites` {name: (fw, fh, pivot, clips function)}, `layers` {name: (w, h,
    frames, fps, frame function (k, n) -> RGBA)} (a looping clip « idle », its point at the top-left
    of a wide layer, at the bottom-centre of a small one).  Returns ({name: Sheet}, files)."""
    built = {}
    for name, (fw, fh, pivot, fn) in sprites.items():
        built[name] = sheet_from(f"stories/{boss}/{name}.png", fw, fh, pivot, fn())
    for name, (w, h, n, fps, fn) in layers.items():
        built[name] = sheet_from(f"stories/{boss}/{name}.png", w, h, [0, 0] if w > 100 else [w // 2, h],
                                 [("idle", [fn(k, n) for k in range(n)], dict(fps=fps, loop=True))])
    files = []
    for name, sh in built.items():
        rel = f"animations/stories/{boss}_{name}.json"
        sh.save(rel)
        files += [os.path.join(ASSETS, sh.texture), os.path.join(ASSETS, rel)]
    return built, files


def review_sheets(built, path, scale=2):
    """A contact sheet of every clip: its frames side by side, one row per clip."""
    from PIL import ImageDraw
    rows = []
    for name, sh in built.items():
        for cname, frames, meta in sh.rows:
            if sh.fw <= 260:
                rows.append((f"{name}.{cname}", frames))
    label_w = 150
    width = label_w + max(sum(f.shape[1] * scale + 4 for f in frames) for _, frames in rows)
    height = sum(max(f.shape[0] for f in frames) * scale + 8 for _, frames in rows)
    img = Image.new("RGBA", (width, height), (34, 24, 40, 255))
    d = ImageDraw.Draw(img)
    y = 0
    for label, frames in rows:
        fh = max(f.shape[0] for f in frames) * scale
        d.text((6, y + fh // 2 - 6), label, fill=(240, 230, 220, 255))
        x = label_w
        for f in frames:
            im = Image.fromarray(f, "RGBA").resize((f.shape[1] * scale, f.shape[0] * scale), Image.NEAREST)
            d.rectangle([x - 1, y, x + im.size[0], y + fh + 1], outline=(70, 56, 80, 255))
            img.alpha_composite(im, (x, y))
            x += im.size[0] + 4
        y += fh + 8
    img.save(path)
    return path


def generate(boss, module, shots, sprites, layers, review=None, sounds=None):
    """A boss's sheets, its story file and its stills; returns the files written."""
    built, files = build_sheets(boss, sprites, layers)
    story = json.load(open(story_path(boss), encoding="utf-8"))
    st = stages(story, shots, sounds)
    write_story(boss, st, module)
    Anim._cache.clear()
    for i, (stage, still) in enumerate(st, 1):
        p = os.path.join(ASSETS, "stories", f"{boss}_{i}.png")
        render(stage, still).save(p, optimize=True)
        files.append(p)
    if review:
        os.makedirs(review, exist_ok=True)
        review_sheets(built, os.path.join(review, f"sprites_{boss}.png"))
    return files


# =================================================================== rendering (stills, previews)
EASE = {
    "linear": lambda u: u,
    "in": lambda u: u * u,
    "out": lambda u: 1 - (1 - u) * (1 - u),
    "inout": lambda u: u * u * (3 - 2 * u),
    "step": lambda u: 1.0 if u >= 1.0 else 0.0,
}


def state(el, t):
    """The resolved state of an element at time t (the rule of BossStories::elementState)."""
    st = dict(pos=(0.0, 0.0), scale=1.0, alpha=1.0, rot=0.0, flip=False, clip="", clip_t=0.0)
    prev = None
    cur = dict(st)
    clip, start, offset = "", 0.0, 0.0
    for k in el["keys"]:
        nxt = dict(cur)
        for f in ("scale", "alpha", "rot"):
            if f in k:
                nxt[f] = float(k[f])
        if "pos" in k:
            nxt["pos"] = (float(k["pos"][0]), float(k["pos"][1]))
        if "flip" in k:
            nxt["flip"] = bool(k["flip"])
        if k["t"] <= t:
            if "clip" in k:
                clip, start, offset = k["clip"], k["t"], float(k.get("offset", 0.0))
            cur = nxt
            prev = k
            continue
        # between prev and k: interpolate the continuous values
        if prev is not None:
            u = (t - prev["t"]) / max(1e-6, k["t"] - prev["t"])
            e = EASE.get(k.get("ease", "inout"), EASE["inout"])(min(1.0, max(0.0, u)))
            out = dict(cur)
            for f in ("scale", "alpha", "rot"):
                out[f] = cur[f] + (nxt[f] - cur[f]) * e
            out["pos"] = (cur["pos"][0] + (nxt["pos"][0] - cur["pos"][0]) * e,
                          cur["pos"][1] + (nxt["pos"][1] - cur["pos"][1]) * e)
            cur = out
        else:
            cur = nxt  # before the first key: its values
        break
    if not clip:  # before its first clip starts: that clip's first frame
        first = next((k for k in el["keys"] if "clip" in k), None)
        if first is not None:
            clip, start, offset = first["clip"], t, float(first.get("offset", 0.0))
    cur["clip"] = clip
    cur["clip_t"] = max(0.0, t - start + offset)
    return cur


def held(el, t):
    """2.82: an element's own time (StoryElement::localTime): frozen from `hold[0]` to `hold[1]` (for ever without it)."""
    h = el.get("hold")
    if not h or t <= float(h[0]):
        return t
    a, b = float(h[0]), float(h[1]) if len(h) > 1 else -1.0
    if b < a or t < b:
        return a
    return t - (b - a)


def camera(stage, t):
    keys = stage.get("camera") or [K(0, pos=(W / 2, H / 2), zoom=1.0)]
    el = {"keys": [dict(k, scale=k.get("zoom", 1.0)) for k in keys]}
    s = state(el, t)
    return s["pos"], s["scale"]


class Anim:
    _cache = {}

    def __init__(self, rel):
        meta = json.load(open(os.path.join(ASSETS, rel), encoding="utf-8"))
        self.meta = meta
        self.img = Image.open(os.path.join(ASSETS, meta["texture"])).convert("RGBA")
        self.fw, self.fh = meta["frameWidth"], meta["frameHeight"]
        self.pivot = meta["pivot"]

    @classmethod
    def get(cls, rel):
        if rel not in cls._cache:
            cls._cache[rel] = Anim(rel)
        return cls._cache[rel]

    def frame(self, clip, t):
        clips = self.meta["clips"]
        c = clips.get(clip) or clips.get("idle") or next(iter(clips.values()))
        for _ in range(8):
            durs = c.get("durations") or [1.0 / c.get("fps", 10)] * c["count"]
            total = sum(durs)
            if c.get("loop", True):
                t = t % total
            elif t >= total:
                if c.get("next") and c["next"] in clips:
                    t -= total
                    c = clips[c["next"]]
                    continue
                t = total - 1e-6
            acc = 0.0
            fi = len(durs) - 1
            for i, d in enumerate(durs):
                acc += d
                if t < acc:
                    fi = i
                    break
            x = (c.get("start", 0) + fi) * self.fw
            y = c["row"] * self.fh
            return self.img.crop((x, y, x + self.fw, y + self.fh))
        return None


def _blend(canvas, img, x, y, alpha, add=False, tint=None):
    """Composites an RGBA PIL image onto the float canvas at (x, y)."""
    a = np.asarray(img, np.float32) / 255.0
    h, w = a.shape[:2]
    H_, W_ = canvas.shape[:2]
    x0, y0 = int(round(x)), int(round(y))
    sx0, sy0 = max(0, -x0), max(0, -y0)
    dx0, dy0 = max(0, x0), max(0, y0)
    dx1, dy1 = min(W_, x0 + w), min(H_, y0 + h)
    if dx1 <= dx0 or dy1 <= dy0:
        return
    src = a[sy0:sy0 + dy1 - dy0, sx0:sx0 + dx1 - dx0]
    rgb = src[..., :3]
    if tint is not None:
        rgb = rgb * tint
    al = src[..., 3:4] * alpha
    dst = canvas[dy0:dy1, dx0:dx1]
    if add:
        dst += rgb * al
    else:
        dst *= 1 - al
        dst += rgb * al


def _hex(h):
    h = h.lstrip("#")
    return np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)], np.float32)


def render(stage, t, size=(W, H), particles_on=False):
    """The stage at time t, as the game draws it in a frame of `size` (cover: the stage fills it)."""
    iw, ih = size
    base = max(iw / W, ih / H)
    (cx, cy), z = camera(stage, t)
    # clamp: the view stays inside the stage
    hw, hh = iw / (2 * base * z), ih / (2 * base * z)
    cx = W / 2 if hw >= W / 2 else min(max(cx, hw), W - hw)
    cy = H / 2 if hh >= H / 2 else min(max(cy, hh), H - hh)
    canvas = np.zeros((ih, iw, 3), np.float32)
    canvas[:] = _hex(stage.get("backdrop", "#000000"))
    C0 = (W / 2, H / 2)
    for el in stage["elements"]:
        d = el.get("depth", 1.0)
        zd = 1 + (z - 1) * d
        k = base * zd
        cdx, cdy = C0[0] + (cx - C0[0]) * d, C0[1] + (cy - C0[1]) * d

        def scr(p):
            return (iw / 2 + (p[0] - cdx) * k, ih / 2 + (p[1] - cdy) * k)

        te = held(el, t)  # 2.82: its own time (a hold freezes it)
        s = state(el, te)
        if el.get("pulse"):  # a breathing light
            amt, per = el["pulse"][0], el["pulse"][1]
            ph = el["pulse"][2] if len(el["pulse"]) > 2 else 0.0
            s["alpha"] *= 1 - amt * (0.5 - 0.5 * math.cos(2 * math.pi * (te / per + ph)))
        if s["alpha"] <= 0.003:
            continue
        tint = _hex(el["tint"]) if el.get("tint") else None
        add = el.get("blend") == "add"
        if "layer" in el:
            img = Image.open(os.path.join(ASSETS, el["layer"])).convert("RGBA")
            sc = PIX * s["scale"] * k
            w2, h2 = max(1, int(round(img.width * sc))), max(1, int(round(img.height * sc)))
            im = img.resize((w2, h2), Image.NEAREST)
            dr = el.get("drift", [0, 0])
            p = (s["pos"][0] + dr[0] * te, s["pos"][1] + dr[1] * te)
            x, y = scr(p)
            if el.get("repeat"):
                x = (x % w2) - w2
                while x < iw:
                    _blend(canvas, im, x, y, s["alpha"], add, tint)
                    x += w2
            else:
                _blend(canvas, im, x, y, s["alpha"], add, tint)
        elif "sprite" in el:
            an = Anim.get(el["sprite"])
            fr = an.frame(s["clip"], s["clip_t"])
            if fr is None:
                continue
            sc = PIX * s["scale"] * k
            px, py = an.pivot
            if s["flip"]:
                fr = fr.transpose(Image.FLIP_LEFT_RIGHT)
                px = an.fw - px
            if abs(s["rot"]) > 0.01:
                pad = max(an.fw, an.fh)
                big = Image.new("RGBA", (an.fw + 2 * pad, an.fh + 2 * pad))
                big.paste(fr, (pad, pad))
                fr = big.rotate(-s["rot"], resample=Image.NEAREST, center=(px + pad, py + pad))
                px, py = px + pad, py + pad
            w2, h2 = max(1, int(round(fr.width * sc))), max(1, int(round(fr.height * sc)))
            im = fr.resize((w2, h2), Image.NEAREST)
            dr = el.get("drift", [0, 0])
            x, y = scr((s["pos"][0] + dr[0] * te, s["pos"][1] + dr[1] * te))
            sh = el.get("shadow")
            if sh:  # its soft shadow on the ground, pushed away from its light
                off = 0.0
                if sh.get("light"):
                    off = min(40.0, max(-40.0, (s["pos"][0] - sh["light"][0]) * 0.12))
                cx_, cy_ = scr((s["pos"][0] + off, s["pos"][1]))
                rw, rh = sh["size"][0] * s["scale"] * k / 2, sh["size"][1] * s["scale"] * k / 2
                x0, x1 = max(0, int(cx_ - rw)), min(iw, int(cx_ + rw) + 1)
                y0, y1 = max(0, int(cy_ - rh)), min(ih, int(cy_ + rh) + 1)
                if x1 > x0 and y1 > y0:
                    yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float32)
                    dd = np.clip(1 - np.hypot((xx + 0.5 - cx_) / rw, (yy + 0.5 - cy_) / rh), 0, 1)
                    a = dd * dd * (3 - 2 * dd) * s["alpha"] * sh.get("alpha", 0.4)
                    canvas[y0:y1, x0:x1] *= (1 - a)[..., None]
            if el.get("repeat"):
                x = ((x - px * sc) % w2) - w2
                while x < iw:
                    _blend(canvas, im, x, y - py * sc, s["alpha"], add, tint)
                    x += w2
            else:
                _blend(canvas, im, x - px * sc, y - py * sc, s["alpha"], add, tint)
        elif "glow" in el:
            r = el["radius"] * s["scale"] * k
            x, y = scr(s["pos"])
            col = _hex(el["glow"])
            x0, x1 = max(0, int(x - r)), min(iw, int(x + r) + 1)
            y0, y1 = max(0, int(y - r)), min(ih, int(y + r) + 1)
            if x1 > x0 and y1 > y0:
                yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float32)
                dd = np.clip(1 - np.hypot(xx + 0.5 - x, yy + 0.5 - y) / max(r, 1e-3), 0, 1)
                a = dd * dd * (3 - 2 * dd) * s["alpha"]
                canvas[y0:y1, x0:x1] += col * a[..., None]
        elif "particles" in el and particles_on:
            pass
    # flashes
    for f in stage.get("flash", []):
        u = t - f["t"]
        a = 0.0
        if -f.get("in", 0.1) <= u < 0:
            a = 1 + u / max(1e-3, f.get("in", 0.1))
        elif 0 <= u <= f.get("out", 0.5):
            a = 1 - u / max(1e-3, f.get("out", 0.5))
        if a > 0:
            canvas += _hex(f.get("color", "#ffffff")) * a * f.get("alpha", 1.0)
    out = np.clip(canvas * 255 + 0.5, 0, 255).astype(np.uint8)
    return Image.fromarray(out, "RGB")
