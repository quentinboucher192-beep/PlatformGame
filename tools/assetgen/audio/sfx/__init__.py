"""Sound effects (docs/ASSET_SPEC.md section 12 + the v2 content). Importing this package
registers them all."""
from sfx.core import REGISTRY, SoundDef, rng_for  # noqa: F401
from sfx import movement, powers, enemies, player, world, ui  # noqa: F401  (registration)
from sfx import abilities, hazards, creatures  # noqa: F401  (v2 registration)
