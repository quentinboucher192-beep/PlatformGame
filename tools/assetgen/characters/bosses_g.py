"""BOSSES-G entry point (called from boss.py `build()`): the abyssal giga boss.

  * kraken - le Kraken des Abysses   256x224, pivot [128, 136]  (boss_g_kraken.py)

Eight sheets, one per phase (sprites/enemies/kraken.png for phase 1, kraken_p2..p8.png): the same
clips, the bioluminescence (bands, spots, eyes, suckers, inner core, jet) recoloured per phase
(cyan, turquoise, indigo, jaune électrique, rouge corail, rose nacré, émeraude, blanc
incandescent), the skin never; phases 1-4 show the whole kraken (form I), phases 5-8 the wounded
one (form II, « Blessé »: torn mantle showing the glowing core, one arm severed); the phase-5
sheet's roar is the mantle tearing open.  Deterministic."""
import importlib

BOSSES = ("kraken",)


def build(only=None):
    for name in BOSSES:
        if only and name not in only:
            continue
        importlib.import_module("boss_g_" + name).build()


if __name__ == "__main__":
    import os
    import sys
    sys.dont_write_bytecode = True
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path[:0] = [here, os.path.dirname(here)]
    build(sys.argv[1:] or None)
