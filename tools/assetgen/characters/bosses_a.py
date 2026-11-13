"""BOSSES-A entry point (called from boss.py `build()` when present).

Builds the v2 bosses of the hydra and core rigs (content contract section 3):

  * spore_queen    — Reine Sporale, Sylvanox, Hydra set, 128x112, pivot [64, 112]
  * frost_hydra    — Hydre de Givre, Glacior, Hydra set, 128x112, pivot [64, 112]
  * prime_sentinel — Sentinelle Prime, Sylvanox, Core set, 112x112, pivot [56, 80]
  * forge_core     — Forgeron Primordial, Pyralis, Core set, 112x112, pivot [56, 80]

Outputs assets/sprites/enemies/<id>.png + assets/animations/<id>.json.
Rigs: boss_a_hydra.py (+ themes boss_a_spore.py, boss_a_frost.py) and
boss_a_core.py (+ bosses boss_a_sentinel.py, boss_a_forge.py).
Everything is deterministic (fixed seeds, no global RNG).
"""
import boss_a_core
import boss_a_frost
import boss_a_hydra
import boss_a_spore

HYDRAS = (boss_a_spore.THEME, boss_a_frost.THEME)
CORES = ("prime_sentinel", "forge_core")


def build():
    for theme in HYDRAS:
        boss_a_hydra.build_sheet(theme)
    for name in CORES:
        boss_a_core.build_one(name)


if __name__ == "__main__":
    import os
    import sys
    sys.dont_write_bytecode = True
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path[:0] = [here, os.path.dirname(here)]
    build()
