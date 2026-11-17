"""BOSSES-H entry point (called from boss.py `build()`): the phoenix giga boss.

  * phenix - Le Phénix Stellaire   256x192, pivot [128, 190]  (boss_h_phenix.py)

Eight sheets, one per phase (sprites/enemies/phenix.png for phase 1, phenix_p2..p8.png): the
same clips, the light (feather edges, eyes, chest star, tail plumes, sparks) recoloured per phase
(aube, zénith, aurore, nébuleuse, cendres, renaissance, comète, supernova), the cosmic plumage
never; phases 1-4 show the whole plumage (form I), phases 5-8 the reborn one (form II,
« Renaissance »: feathers of fire, bare chest star, longer torn tail); the phase-5 sheet's roar is
the rebirth (it bursts into flames and is reborn).  Deterministic."""
import importlib

BOSSES = ("phenix",)


def build(only=None):
    for name in BOSSES:
        if only and name not in only:
            continue
        importlib.import_module("boss_h_" + name).build()


if __name__ == "__main__":
    import os
    import sys
    sys.dont_write_bytecode = True
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path[:0] = [here, os.path.dirname(here)]
    build(sys.argv[1:] or None)
