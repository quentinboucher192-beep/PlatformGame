"""Sound effects (docs/ASSET_SPEC.md section 12 + the v2 content). Importing this package
registers them all."""
from sfx.core import REGISTRY, SoundDef, rng_for  # noqa: F401
from sfx import movement, powers, enemies, player, world, ui  # noqa: F401  (registration)
from sfx import abilities, hazards, creatures  # noqa: F401  (v2 registration)
from sfx import guardians  # noqa: F401  (2.19: sanctuary guardians)
from sfx import powers_219  # noqa: F401  (2.19: powers of the sanctuaries)
from sfx import giga  # noqa: F401  (2.33: Singularis, the giga boss of the Noyau)
from sfx import stories  # noqa: F401  (2.63: the stories' and the trailer's sounds)
