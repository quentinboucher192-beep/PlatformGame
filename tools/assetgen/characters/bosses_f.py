"""BOSSES-F entry point (called from boss.py `build()`): the golem giga boss.

  * monolithe - Monolithe, le Golem Primordial   224x224, pivot [112, 212]  (boss_f_monolithe.py)

Eight sheets, one per phase (sprites/enemies/monolithe.png for phase 1, monolithe_p2..p8.png):
the same clips, the light (runes, veins, cracks, eye, crystal core) recoloured per phase (ambre,
émeraude, cyan, améthyste, magma, or, azur, blanc incandescent); phases 1-4 show the whole golem
(form I), phases 5-8 the split one (form II, "Fendu": chest plates gone, crystal core, broken
shoulder); the phase-5 sheet's roar is the chest plates bursting off.  Deterministic."""
import importlib

BOSSES = ("monolithe",)


def build(only=None):
    for name in BOSSES:
        if only and name not in only:
            continue
        importlib.import_module("boss_f_" + name).build()


if __name__ == "__main__":
    import os
    import sys
    sys.dont_write_bytecode = True
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path[:0] = [here, os.path.dirname(here)]
    build(sys.argv[1:] or None)
