"""BOSSES-E entry point (called from boss.py `build()`): the obsidian dragon giga boss.

  * pyrrhax - Pyrrhax, le Dragon d'Obsidienne   256x192, pivot [128, 152]  (boss_e_pyrrhax.py)

Eight sheets, one per phase (sprites/enemies/pyrrhax.png for phase 1, pyrrhax_p2..p8.png): the
same clips, the fire (seams, eyes, mouth, throat, heart, breath) recoloured per phase, the obsidian
never; phases 1-4 show the whole armour (form I), phases 5-8 the broken one (form II: torn chest
showing the magma heart, torn wing); the phase-5 sheet's roar is the scales bursting off.
Deterministic."""
import importlib

BOSSES = ("pyrrhax",)


def build(only=None):
    for name in BOSSES:
        if only and name not in only:
            continue
        importlib.import_module("boss_e_" + name).build()


if __name__ == "__main__":
    import os
    import sys
    sys.dont_write_bytecode = True
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path[:0] = [here, os.path.dirname(here)]
    build(sys.argv[1:] or None)
