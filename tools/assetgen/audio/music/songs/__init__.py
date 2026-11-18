"""All songs of section 12 (+ the v2 planets, + the 2.19 sanctuary guardians, + the 2.33 giga boss,
+ the giga bosses of Les Confins, 2.48-2.53).
Each module exposes NAME and build() -> Song."""
from music.songs import (menu_theme, planet_map, sylvanox_level, sylvanox_boss, victory,
                         sylvanox_ambience)
from music.songs import (pyralis_level, pyralis_boss, pyralis_ambience, glacior_level,
                         glacior_boss, glacior_ambience, sylvanox_depths)
from music.songs import mycelia_boss, aurorax_boss, pyrogyre_boss, nyxara_boss, aethra_level, aethra_ambience
from music.songs import singularis_boss, pyrrhax_boss
from music.songs import monolithe_boss, kraken_boss, phenix_boss, liche_boss, titan_boss
from music.songs import asterion_film  # 2.68: the score of Asterion's film

SONGS = [menu_theme, planet_map, sylvanox_level, sylvanox_boss, victory, sylvanox_ambience,
         pyralis_level, pyralis_boss, pyralis_ambience, glacior_level, glacior_boss,
         glacior_ambience, sylvanox_depths, mycelia_boss, aurorax_boss, pyrogyre_boss, nyxara_boss,
         aethra_level, aethra_ambience, singularis_boss, pyrrhax_boss,
         monolithe_boss, kraken_boss, phenix_boss, liche_boss, titan_boss, asterion_film]
