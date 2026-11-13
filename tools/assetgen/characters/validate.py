"""Checks every file produced by the characters generator against the
contract in docs/ASSET_SPEC.md (names, frame sizes, pivots, clip names and
frame counts, events, image sizes).

    python3 tools/assetgen/characters/validate.py
"""
import json
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from PIL import Image  # noqa: E402

from sheet import ASSETS, REPO  # noqa: E402

# name: (frameW, frameH, pivot, [(clip, count), ...] in row order, required events {clip: [(frame, name)]},
#        required next {clip: next})
ANIMS = {
    "nova": (32, 32, [16, 32], [
        ("idle", 6), ("walk", 8), ("run", 8), ("jump", 3), ("double_jump", 6), ("fall", 3), ("land", 3),
        ("slide", 3), ("wall_slide", 2), ("climb", 4), ("attack", 5), ("air_attack", 4), ("cast", 4), ("dash", 4),
        ("hurt", 3), ("death", 10), ("respawn", 10), ("victory", 10), ("interact", 4)],
        {"attack": [(2, "hit")], "air_attack": [(1, "hit")], "cast": [(1, "fire")]},
        {"land": "idle"}),
    "glowmite": (24, 16, [12, 16], [("idle", 4), ("walk", 6), ("telegraph", 3), ("attack", 4), ("hurt", 2),
                                    ("death", 6)], {"attack": [(2, "hit")]}, {}),
    "sentinel": (32, 32, [16, 32], [("idle", 6), ("move", 6), ("telegraph", 6), ("attack", 3), ("hurt", 2),
                                    ("death", 8)], {"attack": [(0, "fire")]}, {}),
    "sporeflower": (32, 32, [16, 32], [("idle", 6), ("telegraph", 5), ("attack", 4), ("hurt", 2), ("death", 7)],
                    {"attack": [(1, "fire")]}, {}),
    "stalker": (32, 24, [16, 24], [("idle", 4), ("run", 6), ("telegraph", 4), ("attack", 4), ("recover", 3),
                                   ("hurt", 2), ("death", 7)], {}, {}),
    "golem": (48, 48, [24, 48], [("idle", 4), ("walk", 6), ("defend", 3), ("telegraph", 5), ("attack", 5),
                                 ("hurt", 2), ("death", 9)], {"attack": [(3, "impact")]}, {}),
    "wisp": (24, 24, [12, 24], [("idle", 6), ("move", 6), ("telegraph", 5), ("attack", 4), ("teleport_out", 5),
                                ("teleport_in", 5), ("summon", 6), ("hurt", 2), ("death", 7)],
             {"attack": [(2, "fire")]}, {}),
    "thornwarden": (96, 96, [48, 96], [
        ("idle", 6), ("walk", 8), ("telegraph_slam", 6), ("slam", 6), ("telegraph_charge", 4), ("charge", 6),
        ("telegraph_barrage", 6), ("barrage", 4), ("summon", 6), ("roar", 8), ("telegraph_laser", 6), ("laser", 4),
        ("hurt", 2), ("death", 14)], {"slam": [(3, "impact")], "barrage": [(1, "fire")]}, {}),
    "projectiles": (16, 16, [8, 8], [("player_wave", 4), ("drone_bolt", 4), ("spore", 4), ("wisp_orb", 4),
                                     ("boss_thorn", 4), ("boss_orb", 4)], {}, {}),
    "checkpoint": (24, 48, [12, 48], [("inactive", 1), ("activate", 8), ("active", 6)], {}, {"activate": "active"}),
    "exit_portal": (64, 64, [32, 64], [("idle", 8), ("active", 8)], {}, {}),
    "shard": (12, 12, [6, 6], [("spin", 8)], {}, {}),
    "relic": (16, 16, [8, 8], [("idle", 8)], {}, {}),
    "health_orb": (12, 12, [6, 6], [("idle", 6)], {}, {}),
    "energy_orb": (12, 12, [6, 6], [("idle", 6)], {}, {}),
    "power_unlock": (32, 32, [16, 16], [("dash", 8), ("wave", 8), ("shield", 8), ("impulse", 8), ("teleport", 8)],
                     {}, {}),
    "door": (16, 64, [8, 64], [("closed", 1), ("opening", 6), ("open", 1), ("closing", 6)], {},
             {"opening": "open", "closing": "closed"}),
    "lever": (16, 16, [8, 16], [("off", 1), ("switching", 4), ("on", 1)], {}, {"switching": "on"}),
    "receptor": (16, 32, [8, 32], [("off", 1), ("on", 6)], {}, {}),
    "bounce_pad": (32, 16, [16, 16], [("idle", 1), ("bounce", 5)], {}, {"bounce": "idle"}),
    "breakable_wall": (32, 64, [16, 64], [("intact", 1), ("cracked", 1)], {}, {}),
    "phase_wall": (16, 64, [8, 64], [("idle", 6)], {}, {}),
    "sign": (24, 24, [12, 24], [("idle", 1)], {}, {}),
    "laser_emitter": (16, 16, [8, 16], [("off", 1), ("charging", 4), ("on", 4)], {}, {}),
}

LOOPING = {
    "nova": {"idle", "walk", "run", "fall", "wall_slide", "climb", "dash"},
    "projectiles": {"player_wave", "drone_bolt", "spore", "wisp_orb", "boss_thorn", "boss_orb"},
    "checkpoint": {"active"}, "exit_portal": {"idle", "active"}, "shard": {"spin"}, "relic": {"idle"},
    "health_orb": {"idle"}, "energy_orb": {"idle"},
    "power_unlock": {"dash", "wave", "shield", "impulse", "teleport"}, "receptor": {"on"}, "phase_wall": {"idle"},
    "laser_emitter": {"on"}, "thornwarden": {"charge", "laser", "idle", "walk"},
}

TEXTURES = {
    "nova": "sprites/player/nova.png", "projectiles": "sprites/projectiles/projectiles.png",
}
for e in ("glowmite", "sentinel", "sporeflower", "stalker", "golem", "wisp", "thornwarden"):
    TEXTURES[e] = f"sprites/enemies/{e}.png"
for o in ("checkpoint", "exit_portal", "shard", "relic", "health_orb", "energy_orb", "power_unlock", "door", "lever",
          "receptor", "bounce_pad", "breakable_wall", "phase_wall", "sign", "laser_emitter"):
    TEXTURES[o] = f"sprites/objects/{o}.png"

STATIC = {
    "sprites/objects/moving_platform.png": (48, 32),
    "sprites/objects/crumble_platform.png": (48, 48),
    "sprites/objects/spikes.png": (32, 16),
    "sprites/objects/laser_beam.png": (64, 8),
    "sprites/objects/liquid_acid.png": (128, 32),
    "sprites/objects/liquid_water.png": (128, 32),
    "sprites/objects/liquid_lava.png": (128, 32),
    "textures/fx/glow.png": (64, 64), "textures/fx/particle.png": (8, 8), "textures/fx/spark.png": (8, 8),
    "textures/fx/star.png": (8, 8), "textures/fx/ring.png": (64, 64), "textures/fx/smoke.png": (64, 16),
    "textures/fx/dust.png": (80, 16), "textures/fx/slash.png": (192, 32), "textures/fx/hit.png": (120, 24),
    "textures/fx/explosion.png": (384, 48), "textures/fx/shield.png": (288, 48),
    "textures/fx/teleport.png": (256, 32), "textures/fx/warning.png": (16, 16),
    "ui/app_icon.png": (256, 256),
}


def check_anim(name, errors):
    fw, fh, pivot, clips, events, nexts = ANIMS[name]
    jpath = os.path.join(ASSETS, "animations", name + ".json")
    if not os.path.exists(jpath):
        errors.append(f"{name}: missing {jpath}")
        return
    meta = json.load(open(jpath))
    if meta.get("texture") != TEXTURES[name]:
        errors.append(f"{name}: texture {meta.get('texture')} != {TEXTURES[name]}")
    tpath = os.path.join(ASSETS, meta["texture"])
    if not os.path.exists(tpath):
        errors.append(f"{name}: texture file missing {tpath}")
        return
    if (meta["frameWidth"], meta["frameHeight"]) != (fw, fh):
        errors.append(f"{name}: frame {meta['frameWidth']}x{meta['frameHeight']} != {fw}x{fh}")
    if meta["pivot"] != pivot:
        errors.append(f"{name}: pivot {meta['pivot']} != {pivot}")
    got = meta["clips"]
    if list(got.keys()) != [c for c, _ in clips]:
        errors.append(f"{name}: clips {list(got.keys())} != {[c for c, _ in clips]}")
    rows = set()
    max_col, max_row = 0, 0
    for cname, count in clips:
        c = got.get(cname)
        if c is None:
            continue
        if c["count"] != count:
            errors.append(f"{name}.{cname}: count {c['count']} != {count}")
        start = c.get("start", 0)
        max_col = max(max_col, start + c["count"])
        max_row = max(max_row, c["row"])
        if c["row"] in rows:
            errors.append(f"{name}.{cname}: duplicate row {c['row']}")
        rows.add(c["row"])
        if "durations" in c:
            if len(c["durations"]) != c["count"] or any(d <= 0 for d in c["durations"]):
                errors.append(f"{name}.{cname}: bad durations")
        elif not c.get("fps", 0) > 0:
            errors.append(f"{name}.{cname}: no fps/durations")
        for ev in c.get("events", []):
            if not 0 <= ev["frame"] < c["count"]:
                errors.append(f"{name}.{cname}: event frame {ev['frame']} out of range")
            n = ev["name"]
            if n not in ("footstep", "hit", "fire", "impact") and not n.startswith("sfx:"):
                errors.append(f"{name}.{cname}: unknown event {n}")
        for (fr, ev) in events.get(cname, []):
            if {"frame": fr, "name": ev} not in c.get("events", []):
                errors.append(f"{name}.{cname}: missing event {ev}@{fr}")
        if cname in nexts and c.get("next") != nexts[cname]:
            errors.append(f"{name}.{cname}: next {c.get('next')} != {nexts[cname]}")
        if c.get("next") and c["next"] not in got:
            errors.append(f"{name}.{cname}: next -> unknown clip {c['next']}")
        if cname in LOOPING.get(name, set()) and not c["loop"]:
            errors.append(f"{name}.{cname}: should loop")
    img = Image.open(tpath)
    need = (max_col * fw, (max_row + 1) * fh)
    if img.size[0] < need[0] or img.size[1] < need[1]:
        errors.append(f"{name}: texture {img.size} smaller than required {need}")
    if img.mode != "RGBA":
        errors.append(f"{name}: texture mode {img.mode} (expected RGBA)")
    # every frame must have some content except intentionally empty last frames
    import numpy as np
    a = np.array(img.convert("RGBA"))
    for cname, count in clips:
        c = got.get(cname)
        if not c:
            continue
        for f in range(count):
            x0, y0 = (c.get("start", 0) + f) * fw, c["row"] * fh
            if a[y0:y0 + fh, x0:x0 + fw, 3].max() == 0:
                errors.append(f"{name}.{cname}: frame {f} is empty")


def run():
    errors = []
    for name in ANIMS:
        check_anim(name, errors)
    for rel, size in STATIC.items():
        p = os.path.join(ASSETS, rel)
        if not os.path.exists(p):
            errors.append(f"missing {rel}")
            continue
        im = Image.open(p)
        if im.size != size:
            errors.append(f"{rel}: size {im.size} != {size}")
    ico = os.path.join(REPO, "installer", "app.ico")
    if not os.path.exists(ico):
        errors.append("missing installer/app.ico")
    else:
        sizes = set(Image.open(ico).ico.sizes())
        want = {(16, 16), (32, 32), (48, 48), (256, 256)}
        if not want <= sizes:
            errors.append(f"app.ico sizes {sorted(sizes)} missing {sorted(want - sizes)}")
    return errors


if __name__ == "__main__":
    errs = run()
    for e in errs:
        print("ERROR:", e)
    print(f"{len(ANIMS)} animation sheets, {len(STATIC)} static images, app.ico checked: "
          f"{'OK' if not errs else str(len(errs)) + ' problem(s)'}")
    sys.exit(1 if errs else 0)
