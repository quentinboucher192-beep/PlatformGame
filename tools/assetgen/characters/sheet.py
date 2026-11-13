"""Sprite-sheet assembly + animation JSON writer (docs/ASSET_SPEC.md section 2)."""
import json
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
ASSETS = os.path.join(REPO, "assets")


def asset_path(rel):
    return os.path.join(ASSETS, rel)


def save_png(img, rel):
    path = asset_path(rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if isinstance(img, np.ndarray):
        img = Image.fromarray(img, "RGBA")
    img.save(path, optimize=True)
    return path


def save_json(obj, rel):
    path = asset_path(rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(dumps(obj))
        f.write("\n")
    return path


def dumps(obj):
    """Readable JSON: one line per clip."""
    if "clips" not in obj:
        return json.dumps(obj, indent=2)
    head = {k: v for k, v in obj.items() if k != "clips"}
    lines = ["{"]
    for k, v in head.items():
        lines.append(f'  {json.dumps(k)}: {json.dumps(v)},')
    lines.append('  "clips": {')
    items = list(obj["clips"].items())
    for i, (name, clip) in enumerate(items):
        comma = "," if i < len(items) - 1 else ""
        lines.append(f'    {json.dumps(name)}: {json.dumps(clip)}{comma}')
    lines.append("  }")
    lines.append("}")
    return "\n".join(lines)


class Sheet:
    """Collects clips (one per row) and writes `<png>` + `<json>`."""

    def __init__(self, texture, fw, fh, pivot):
        self.texture = texture
        self.fw, self.fh = fw, fh
        self.pivot = list(pivot)
        self.rows = []  # (name, frames, meta)

    def add(self, name, frames, fps=None, durations=None, loop=True, next=None, events=None):
        frames = list(frames)
        for f in frames:
            assert f.shape[0] == self.fh and f.shape[1] == self.fw, (name, f.shape)
        meta = {"row": len(self.rows), "count": len(frames)}
        if durations is not None:
            assert len(durations) == len(frames), name
            meta["durations"] = [round(float(d), 3) for d in durations]
        else:
            meta["fps"] = fps if fps is not None else 10
        meta["loop"] = bool(loop)
        if next:
            meta["next"] = next
        if events:
            meta["events"] = [{"frame": int(f), "name": n} for f, n in events]
            for f, _ in events:
                assert 0 <= f < len(frames), (name, f)
        self.rows.append((name, frames, meta))
        return self

    def image(self):
        cols = max(len(fr) for _, fr, _ in self.rows)
        img = np.zeros((self.fh * len(self.rows), self.fw * cols, 4), np.uint8)
        for r, (_, frames, _) in enumerate(self.rows):
            for c, f in enumerate(frames):
                img[r * self.fh:(r + 1) * self.fh, c * self.fw:(c + 1) * self.fw] = f
        return img

    def meta(self):
        return {
            "texture": self.texture,
            "frameWidth": self.fw,
            "frameHeight": self.fh,
            "pivot": self.pivot,
            "clips": {name: meta for name, _, meta in self.rows},
        }

    def save(self, json_rel):
        save_png(self.image(), self.texture)
        save_json(self.meta(), json_rel)
        return self


def strip(frames):
    """Horizontal strip of equally sized frames."""
    h, w = frames[0].shape[:2]
    img = np.zeros((h, w * len(frames), 4), np.uint8)
    for i, f in enumerate(frames):
        img[:, i * w:(i + 1) * w] = f
    return img
