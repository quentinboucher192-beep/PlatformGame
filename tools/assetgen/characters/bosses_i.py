"""BOSSES-I entry point (called from boss.py `build()`): the lich giga boss.

  * liche - La Liche des Catacombes   224x224, pivot [112, 218]  (boss_i_liche.py)

Eight sheets, one per phase (sprites/enemies/liche.png for phase 1, liche_p2..p8.png): the same
clips, the soul fire (eyes, phylactery, runes, gems, spells, wisps) recoloured per phase
(Nécromancie, Ossuaire, Malédiction, Spectres, Phylactère brisé, Âmes, Crypte, Néant), the bone,
the robe and the crown never; phases 1-4 show the whole lich (form I), phases 5-8 the broken one
(form II, "Brisée": crown split, cracked phylactery, robe in shreds over the ribcage and its burning
core, the bones of one arm floating apart); the phase-5 sheet's roar is the phylactery cracking and
the crown splitting.  Deterministic."""
import importlib

BOSSES = ("liche",)


def build(only=None):
    for name in BOSSES:
        if only and name not in only:
            continue
        importlib.import_module("boss_i_" + name).build()


if __name__ == "__main__":
    import os
    import sys
    sys.dont_write_bytecode = True
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path[:0] = [here, os.path.dirname(here)]
    build(sys.argv[1:] or None)
