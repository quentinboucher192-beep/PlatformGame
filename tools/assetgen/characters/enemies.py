"""All regular enemies (docs/ASSET_SPEC.md section 4 + CONTENT_CONTRACT section 2);
the mini-boss and bosses are in boss.py.

Order matters: the Pyralis / Glacior variants are palette swaps of the base
sheets, so every base species is generated first."""
import enemy_bomber
import enemy_bulwark
import enemy_burrower
import enemy_glowmite
import enemy_gloop
import enemy_golem
import enemy_nightwing
import enemy_sentinel
import enemy_sporeflower
import enemy_stalker
import enemy_weaver
import enemy_wisp
import variants

BASE = (enemy_glowmite, enemy_sentinel, enemy_sporeflower, enemy_stalker, enemy_golem, enemy_wisp)
NEW = (enemy_nightwing, enemy_gloop, enemy_weaver, enemy_bulwark, enemy_bomber, enemy_burrower)


def build():
    for mod in BASE + NEW:
        mod.build()
    variants.build()
