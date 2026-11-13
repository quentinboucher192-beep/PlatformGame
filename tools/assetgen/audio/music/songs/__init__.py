"""All songs of section 12 (+ the v2 planets). Each module exposes NAME and build() -> Song."""
from music.songs import (menu_theme, planet_map, sylvanox_level, sylvanox_boss, victory,
                         sylvanox_ambience)
from music.songs import (pyralis_level, pyralis_boss, pyralis_ambience, glacior_level,
                         glacior_boss, glacior_ambience, sylvanox_depths)

SONGS = [menu_theme, planet_map, sylvanox_level, sylvanox_boss, victory, sylvanox_ambience,
         pyralis_level, pyralis_boss, pyralis_ambience, glacior_level, glacior_boss,
         glacior_ambience, sylvanox_depths]
