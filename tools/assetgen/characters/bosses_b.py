"""BOSSES-B: Knight-set and Wyrm-set bosses of Pyralis and Glacior (v2 content contract, section 3).

    magmaroth  96x96,  collision 44x60, pivot [48, 96]  (knight rig, boss_b_knight*.py)
    glaciarch  96x96,  collision 44x60, pivot [48, 96]
    ignivore   128x96, collision 56x40, pivot [64, 84]  (wyrm rig, boss_b_wyrm*.py)
    cryowyrm   128x96, collision 56x40, pivot [64, 84]

`build()` is called by boss.py (characters/generate.py boss).  Deterministic."""
from sheet import Sheet


def _sheet(name, fw, fh, pivot, clips):
    s = Sheet(f"sprites/enemies/{name}.png", fw, fh, pivot)
    for clip, frames, kw in clips:
        s.add(clip, frames, **kw)
    s.save(f"animations/{name}.json")
    return s


def build(only=None):
    import boss_b_knight_clips as KC
    import boss_b_wyrm_clips as WC
    jobs = [
        ("magmaroth", 96, 96, [48, 96], KC.magmaroth_clips),
        ("glaciarch", 96, 96, [48, 96], KC.glaciarch_clips),
        ("ignivore", WC.W, WC.H, WC.PIVOT, WC.ignivore_clips),
        ("cryowyrm", WC.W, WC.H, WC.PIVOT, WC.cryowyrm_clips),
    ]
    for name, fw, fh, pivot, fn in jobs:
        if only and name not in only:
            continue
        _sheet(name, fw, fh, pivot, fn())
