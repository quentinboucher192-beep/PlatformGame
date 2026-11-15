"""BOSSES-D entry point (called from boss.py `build()`): the giga boss of the Noyau stellaire.

  * singularis - Singularis, le Coeur Effondre   192x192, pivot [96, 124]  (boss_d_singularis.py)

Its effects (boss_d_effects.py): plasma_flood, star_wall, crumble_platform_star; its projectiles
(boss_d_projectiles.py, rows of projectiles.png): star_shard, meteor.

Eight sheets, one per phase (sprites/enemies/singularis.png for phase 1, singularis_p2..p8.png):
the same clips, the plasma recoloured per phase; phases 1-4 show the whole shell (form I),
phases 5-8 the burst one (form II); the phase-5 sheet's roar is the shattering of the shell,
the phase-8 one the ignition of the supernova.  Deterministic."""
import importlib

BOSSES = ("singularis",)


def build(only=None):
    for name in BOSSES:
        if only and name not in only:
            continue
        importlib.import_module("boss_d_" + name).build()
    if not only or "effects" in only:
        importlib.import_module("boss_d_effects").build()  # plasma tide, starlight walls, crust floor


if __name__ == "__main__":
    import os
    import sys
    sys.dont_write_bytecode = True
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path[:0] = [here, os.path.dirname(here)]
    build(sys.argv[1:] or None)
