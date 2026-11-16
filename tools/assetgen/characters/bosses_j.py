"""BOSSES-J entry point (called from boss.py `build()`): the war machine giga boss.

  * titan - Le Titan Mécanique   256x224, pivot [128, 212]  (boss_j_titan.py)

Eight sheets, one per phase (sprites/enemies/titan.png for phase 1, titan_p2..p8.png): the same
clips, the light (visor, reactor, muzzle, vents, cable lights, sparks) recoloured per phase
(Démarrage ambre, Vapeur blanc orangé, Surtension bleu électrique, Laser rouge, Surchauffe
orange-rouge, Plasma magenta, Arc vert, Cœur nucléaire blanc incandescent), the metal never;
phases 1-4 show the intact machine (form I), phases 5-8 the damaged one (form II, « Endommagé »:
torn chest showing the reactor, shoulder plate gone, smoke); the phase-5 sheet's roar is the chest
plate blowing off.  Deterministic."""
import importlib

BOSSES = ("titan",)


def build(only=None):
    for name in BOSSES:
        if only and name not in only:
            continue
        importlib.import_module("boss_j_" + name).build()


if __name__ == "__main__":
    import os
    import sys
    sys.dont_write_bytecode = True
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path[:0] = [here, os.path.dirname(here)]
    build(sys.argv[1:] or None)
