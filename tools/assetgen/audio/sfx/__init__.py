"""Sound effects (docs/ASSET_SPEC.md section 12). Importing this package registers them all."""
from sfx.core import REGISTRY, SoundDef, rng_for  # noqa: F401
from sfx import movement, powers, enemies, player, world, ui  # noqa: F401  (registration)
