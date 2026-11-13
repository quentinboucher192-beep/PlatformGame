"""All regular enemies (docs/ASSET_SPEC.md section 4); the mini-boss is in boss.py."""
import enemy_glowmite
import enemy_golem
import enemy_sentinel
import enemy_sporeflower
import enemy_stalker
import enemy_wisp


def build():
    for mod in (enemy_glowmite, enemy_sentinel, enemy_sporeflower, enemy_stalker, enemy_golem, enemy_wisp):
        mod.build()
