"""Shared materials (colour ramps light -> dark) built from the Sylvanox palette."""
from common.palette import (ACID, AMBER, CYAN, DANGER, MOSS, OUTLINE, SCARF, SKY, STONE,
                            SUIT, VIOLET, VISOR, hex_rgba)

from raster import Mat

H = hex_rgba

# --- hero -------------------------------------------------------------------
SUIT_M = Mat([SUIT[0], SUIT[1], SUIT[2], SUIT[3]], name="suit", thresholds=[0.80, 0.56, 0.33])
SUIT_DARK = Mat([SUIT[1], SUIT[2], SUIT[3], STONE[3]], name="suit_dark", thresholds=[0.80, 0.56, 0.33])
BOOT = Mat([STONE[4], STONE[3], STONE[2]], name="boot", thresholds=[0.78, 0.45])
GLOVE = Mat([SUIT[2], SUIT[3], STONE[2]], name="glove", thresholds=[0.75, 0.45])
VISOR_M = Mat([CYAN[3], VISOR[1], VISOR[0], CYAN[0]], emissive=True, name="visor",
              thresholds=[0.93, 0.76, 0.42])
SCARF_M = Mat([SCARF[2], SCARF[1], SCARF[0]], name="scarf", thresholds=[0.80, 0.47])
CORE = Mat([CYAN[3], CYAN[2], CYAN[1]], emissive=True, name="core", thresholds=[0.8, 0.5])
BLADE = Mat([CYAN[3], CYAN[2], CYAN[1], CYAN[0]], emissive=True, name="blade", outline=None,
            thresholds=[0.85, 0.6, 0.35])
GLOW_CYAN = Mat([CYAN[3], CYAN[2], CYAN[1], CYAN[0]], emissive=True, name="glow_cyan",
                 thresholds=[0.85, 0.6, 0.35])
GLOW_CYAN_NO = Mat([CYAN[3], CYAN[2], CYAN[1], CYAN[0]], emissive=True, outline=None,
                   name="glow_cyan_no", thresholds=[0.85, 0.6, 0.35])

# --- generic world materials -------------------------------------------------
STONE_M = Mat([STONE[4], STONE[3], STONE[2], STONE[1]], name="stone", thresholds=[0.82, 0.58, 0.36])
STONE_DARK = Mat([STONE[3], STONE[2], STONE[1], STONE[0]], name="stone_dark", thresholds=[0.82, 0.58, 0.36])
METAL = Mat([SUIT[2], SUIT[3], STONE[2], STONE[1]], name="metal", thresholds=[0.8, 0.56, 0.34])
MOSS_M = Mat([MOSS[3], MOSS[2], MOSS[1], MOSS[0]], name="moss", thresholds=[0.8, 0.56, 0.34])
DARK = Mat([SKY[4], SKY[3], SKY[2], SKY[1]], name="dark", thresholds=[0.8, 0.56, 0.34])

AMBER_GLOW = Mat([AMBER[2], AMBER[1], AMBER[0]], emissive=True, name="amber", thresholds=[0.75, 0.45])
RED_GLOW = Mat([H('#ffd0c0'), DANGER[2], DANGER[1], DANGER[0]], emissive=True, name="red",
               thresholds=[0.88, 0.66, 0.4])
EYE_RED = Mat([H('#ffe0e6'), DANGER[1], DANGER[0]], emissive=True, name="eye_red", thresholds=[0.8, 0.45])
VIOLET_GLOW = Mat([H('#f6dcff'), VIOLET[2], VIOLET[1], VIOLET[0]], emissive=True, name="violet",
                  thresholds=[0.88, 0.66, 0.4])
ACID_GLOW = Mat([ACID[3], ACID[2], ACID[1], ACID[0]], emissive=True, name="acid",
                thresholds=[0.85, 0.6, 0.35])

ALL = dict(OUTLINE=OUTLINE)
