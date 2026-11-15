"""BOSSES-C entry point (called from boss.py `build()`): the four sanctuary guardians of 2.19
and the visuals of their new attacks.

  * mycelia   - Mycelia, la Mere-Racine      128x112, pivot [64, 112]  (boss_c_mycelia.py)
  * aurorax   - Aurorax, le Miroir boreal    128x128, pivot [64, 128]  (boss_c_aurorax.py)
  * pyrogyre  - Pyrogyre, l'Anneau solaire   128x128, pivot [64, 92]   (boss_c_pyrogyre.py)
  * nyxara    - Nyxara, la Devoreuse d'etoiles 128x128, pivot [64, 92] (boss_c_nyxara.py)

Common clips: idle, walk, move, telegraph, attack, telegraph_special, special, summon, roar,
hurt, death (+ teleport_out / teleport_in for Mycelia and Nyxara).
Attack effects (root_spike, thorn_wall, void_well, eclipse_ring, shadow_mark, frost_floor,
lava_flood) come from boss_c_effects.py; the 16x16 projectiles from boss_c_projectiles.py
(rows of projectiles.png, see projectiles.py) and the `prism` beam from beams.py.
Outputs assets/sprites/enemies/<id>.png + assets/animations/<id>.json.  Deterministic."""
import importlib
import importlib.util

BOSSES = ("mycelia", "aurorax", "pyrogyre", "nyxara")


def build(only=None):
    for name in BOSSES:
        if only and name not in only:
            continue
        importlib.import_module("boss_c_" + name).build()
    if (not only or "effects" in only) and importlib.util.find_spec("boss_c_effects") is not None:
        importlib.import_module("boss_c_effects").build()


if __name__ == "__main__":
    import os
    import sys
    sys.dont_write_bytecode = True
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path[:0] = [here, os.path.dirname(here)]
    build(sys.argv[1:] or None)
