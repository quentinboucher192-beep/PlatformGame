#!/usr/bin/env python3
"""La Liche des Catacombes (version 2.57): the fifth giga boss of Les Confins, at the end of
« La Crypte des Noms » (confins_05, tools/levelgen/confins.py).

An old keeper of the catacombs of the Confins who locked his soul in a crystal so as never to die
(data/stories/liche.json): a sorcerer-king of bone and tattered robes floating over the altars of
its crypt. It does not chase Nova: it floats over one altar, casts, then fades out and appears over
another one (the next altar flares before it appears there). Health 760 (95 per phase), contact 2,
combos of three. Eight phases, one every 12.5 % of its health, each with its own soul fire (the
sheets of tools/assetgen/characters/boss_i_liche.py: one per phase; phases 5-8 the broken form):

  1 Nécromancie       souls in torment in fans, the soul ray, its runes on the floor, a rain of
                      bones, homing souls
  2 Ossuaire          walls of bones close Nova in (bones falling between them), volleys of bones,
                      bones bursting out of the floor in a line, its runes, the rain of bones
  3 Malédiction       « Maléfice »: a circle of runes closes around Nova (still inside, her jumps
                      grow heavy for a while), cursed skulls, the funeral knell (a ring with one
                      opening), the embrace of the dead pulls her, a circle of runes around her
  4 Spectres          its spectres escort it, a round of souls circling it, a spiral of souls; at
                      the end of the phase it hides its soul in its phylactery (a crystal set on
                      one of its altars): it cannot be hurt until the crystal is broken
  5 Phylactère brisé  the crypt's floor gives way over the ossuaries; the blood ray, runes of blood,
                      shards of its phylactery, its curse, its walls of bones
  6 Âmes              lost souls escort it, the well of souls pulls Nova and bends her shots,
                      homing souls, the spiral, the knell, the circle of runes
  7 Crypte            the crypts open (souls burst out of the floor), walls of bones, the curse,
                      runes of blood, the ossuary, the soul ray
  8 Néant             everything, faster; rage after 40 s in the phase (the arena closes in), and
                      its last spell to survive when its health runs out.

Its own attack types (game/enemies/LicheAttacks.cpp): « Runes funestes » (glyph: runes traced on the
floor Nova stands on: armed, a rune bursts into a pillar of soul fire when she steps on it, and
every rune left bursts at the end of its time) and « Maléfice » (hex: a circle of runes closes
around her; still inside, she is cursed: her jumps reach only 70 % of their height for 5 s). Right
under it, or beside it, is no shelter (what comes down on its prey is preferred there).

Looks of its own (appearance only, not in the fingerprints): its souls and its bones (projectiles
« soul » and « bone », drawn by the game), its walls of bones (Cage `look` « bone »), the ossuaries
and the curtains of souls of its crypt (damage zones `style` « souls ») and its altars (level objects
« altar », tools/levelgen/confins.py).

Writes data/enemies/liche.json, data/enemies/spectre.json (its escort) and
data/enemies/phylactere.json (the crystal of its soul).
Run from the repository root:  python3 tools/contentgen/liche.py
"""
import copy
import json
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bosses import _atk  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA = os.path.join(ROOT, "data", "enemies")

# Clips of its sheets (boss_i_liche.py): telegraph/attack (it raises its staff, the runes flare,
# then thrusts it forward: a bolt leaves the crystal, `fire` on frame 1), telegraph_special/special
# (both hands raised, the eyes blaze, then the staff levelled: the beam), summon (arms spread, souls
# rise from the ground), roar (phase change; the 5th sheet's: the phylactery cracks), death.
CLASSIC = ("telegraph", "attack")
SPECIAL = ("telegraph_special", "special")
RAISE = ("telegraph_special", "summon")
CAST = ("telegraph", "summon")      # the staff raised, then the arms spread: runes, curses
CRYSTAL = [83, -162]  # the point of the phylactery crystal on the attack frame, from the feet (sprite pixels)
BEAM = [86, -138]     # the crystal of the levelled staff during the beam

# The soul fire of each phase (the P1 tone of each ramp of boss_i_liche: vert maladif, jaune d'os,
# violet, cyan pâle, rouge sang, turquoise, indigo, blanc incandescent).
LIGHTS = ["#b4f566", "#ffe882", "#cf8eff", "#aaf2ff", "#ff6658", "#68f4d4", "#9ea0ff", "#f2f0ff"]
SHEETS = ["animations/liche.json"] + ["animations/liche_p%d.json" % k for k in range(2, 9)]


def attacks():
    soul = dict(projectile="soul", projectileRadius=6, hitEffect="projectile_burst")
    bone = dict(projectile="bone", projectileRadius=6, hitEffect="projectile_burst")
    ring = dict(radius=900, knockback=240)
    runes = dict(radius=18, height=56, duration=4.0, knockback=260)
    hexed = dict(radius=72, duration=5.0, curse=0.7, knockback=160)
    walls = dict(projectileSpeed=120, burstInterval=0.34, duration=3.6, radius=104, height=150, knockback=200, look="bone",
                 **bone)
    return [
        # 1 Nécromancie
        _atk("ames", "Âmes en peine", "spread", 1, 900, 0.6, 0.3, 0.45, 2.4, *CLASSIC, projectileSpeed=220, count=5,
             spread=12, bursts=2, burstInterval=0.35, origin=CRYSTAL, lead=0.6, **soul),
        _atk("rayon", "Rayon d'âme", "laser", 2, 900, 0.8, 1.5, 0.6, 4.6, *SPECIAL, laserLength=900, sweepDegrees=100,
             origin=BEAM, color="#b4f566", beamTexture="thorn", beamWidth=20),
        # Runes traced on her floor, a row through her: armed after 0.7 s, they burst when she
        # steps on one, and all at the end of their 4 s.
        _atk("runes", "Runes funestes", "glyph", 1, 1400, 0.7, 0.3, 0.5, 6.0, *CAST, count=5, spread=72, burstInterval=0.7,
             stagger=0.1, lead=0.5, **runes),
        _atk("pluie_os", "Pluie d'ossements", "rain", 1, 1200, 0.7, 0.3, 0.5, 3.4, *RAISE, projectileSpeed=240, count=9,
             spread=44, gravity=220, rainHeight=360, lead=0.8, **bone),
        _atk("traits", "Traits d'âme", "projectile", 1, 1000, 0.55, 0.25, 0.45, 3.4, *CLASSIC, projectileSpeed=160,
             count=3, spread=30, homing=2.2, homingTime=1.8, origin=CRYSTAL, **soul),
        # 2 Ossuaire
        # Walls of bones rise on both sides of Nova; bones fall between them.
        _atk("murs_os", "Murs d'os", "cage", 1, 1100, 0.85, 0.2, 0.6, 7.0, *RAISE, **walls),
        _atk("volee_os", "Volée d'ossements", "arc", 1, 1000, 0.7, 0.25, 0.5, 3.2, *CLASSIC, projectileSpeed=300, count=4,
             spread=14, gravity=420, origin=CRYSTAL, lead=0.6, **bone),
        # Bones burst out of the floor in a line from it through Nova.
        _atk("ossuaire", "Ossuaire", "eruption", 2, 1200, 0.8, 0.3, 0.6, 4.2, *CAST, projectile="bone", projectileSpeed=440,
             count=6, spread=54, gravity=700, stagger=0.12, lead=0.8, projectileRadius=6,
             warningEffect="eruption_warning_ice", eruptionEffect="rock_shatter", hitEffect="projectile_burst"),
        # 3 Malédiction
        # A circle of runes closes around her: still inside, her jumps grow heavy for 5 s.
        _atk("malefice", "Maléfice", "hex", 1, 1200, 0.9, 0.6, 0.5, 6.5, *CAST, lead=0.4, **hexed),
        _atk("cranes", "Crânes maudits", "projectile", 1, 1000, 0.6, 0.25, 0.45, 3.6, *CLASSIC, projectileSpeed=150,
             count=4, spread=26, homing=2.6, homingTime=1.8, origin=CRYSTAL, **soul),
        _atk("glas", "Glas funèbre", "ring", 2, 1000, 0.75, 0.2, 0.6, 5.0, *SPECIAL, projectileSpeed=190, spread=54,
             **ring),
        _atk("etreinte", "Étreinte des morts", "vortex", 2, 900, 0.8, 0.2, 0.7, 7.0, *RAISE, pull=110, duration=2.4,
             knockback=280),
        # Runes around her: her spot and two on each side.
        _atk("cercle_runes", "Cercle de runes", "glyph", 1, 1400, 0.75, 0.3, 0.5, 6.5, *CAST, count=2, spread=78,
             burstInterval=0.75, stagger=0.06, arms=2, lead=0.4, **runes),
        # 4 Spectres
        _atk("spectres", "Spectres", "summon", 0, 1500, 0.9, 0.4, 0.8, 12.0, "summon", "summon", summonId="spectre",
             summonCount=2, maxSummoned=3),
        _atk("ronde", "Ronde des âmes", "orbit", 2, 400, 0.75, 0.2, 0.5, 5.0, *RAISE, projectile="soul", projectileRadius=7,
             count=8, radius=180, spread=95, duration=3.0, knockback=220),
        _atk("spirale", "Spirale d'âmes", "spiral", 1, 1000, 0.7, 0.2, 0.5, 4.5, *SPECIAL, projectileSpeed=130, arms=3,
             spread=14, burstInterval=0.12, duration=3.5, **soul),
        # 5 Phylactère brisé
        _atk("rayon_sang", "Rayon de sang", "laser", 2, 900, 0.75, 1.5, 0.6, 4.4, *SPECIAL, laserLength=900,
             sweepDegrees=130, origin=BEAM, color="#ff6658", beamTexture="flame", beamWidth=22),
        _atk("runes_sang", "Runes de sang", "glyph", 1, 1400, 0.7, 0.3, 0.5, 6.0, *CAST, count=7, spread=66,
             burstInterval=0.7, stagger=0.08, lead=0.5, **runes),
        _atk("eclats", "Éclats du phylactère", "spread", 1, 900, 0.6, 0.3, 0.45, 2.6, *CLASSIC, projectile="star_shard",
             projectileSpeed=240, count=7, spread=10, bursts=2, burstInterval=0.35, origin=CRYSTAL, lead=0.6,
             hitEffect="projectile_burst"),
        # 6 Âmes
        _atk("ames_errantes", "Âmes errantes", "summon", 0, 1500, 0.9, 0.4, 0.8, 12.0, "summon", "summon",
             summonId="spectre", summonCount=2, maxSummoned=4),
        _atk("puits", "Puits des âmes", "well", 2, 900, 0.8, 0.2, 0.6, 8.0, *RAISE, pull=95, radius=26, duration=4.5,
             knockback=240),
        # 7 Crypte
        # The crypts open: souls burst out of the floor in a line through her.
        _atk("cryptes", "Cryptes ouvertes", "eruption", 2, 1200, 0.8, 0.3, 0.6, 4.4, *CAST, projectile="soul",
             projectileSpeed=420, count=6, spread=60, gravity=600, stagger=0.13, lead=0.8, projectileRadius=7,
             warningEffect="eruption_warning_spore", eruptionEffect="eruption_spore", hitEffect="projectile_burst"),
        # Its last spell (never chosen: the last stand, EnemyDefinition::finalAttacks)
        _atk("dernier_glas", "Dernier glas", "ring", 2, 3000, 0.6, 0.2, 0.3, 0.0, *SPECIAL, projectileSpeed=320,
             spread=60, radius=880, knockback=260),
        _atk("dernieres_runes", "Dernières runes", "glyph", 1, 3000, 0.7, 0.3, 0.3, 0.0, *CAST, count=3, spread=74,
             burstInterval=0.7, stagger=0.05, arms=2, lead=0.4, **runes),
        _atk("derniere_pluie", "Dernière pluie d'os", "rain", 1, 3000, 0.6, 0.3, 0.3, 0.0, *RAISE, projectileSpeed=260,
             count=14, spread=48, gravity=300, rainHeight=360, lead=0.8, **bone),
        _atk("dernier_rayon", "Dernier rayon", "laser", 2, 3000, 0.8, 1.6, 0.3, 0.0, *SPECIAL, laserLength=1000,
             sweepDegrees=150, origin=BEAM, color="#f2f0ff", beamTexture="star", beamWidth=24),
    ]


def phase(i, name, attack_ids, speed, cooldown, telegraph, text, combo=3, **kw):
    p = {"name": name, "hpThreshold": round(1.0 - 0.125 * i, 3), "attacks": attack_ids, "speedMultiplier": speed,
         "cooldownMultiplier": cooldown, "combo": combo, "telegraphMultiplier": telegraph, "enterText": text,
         "animation": SHEETS[i], "lightColor": LIGHTS[i]}
    p.update(kw)
    return p


def phases():
    return [
        phase(0, "Nécromancie", ["ames", "rayon", "runes", "pluie_os", "traits"], 1.0, 1.0, 0.85,
              "La Liche s'éveille parmi ses morts."),
        phase(1, "Ossuaire", ["murs_os", "volee_os", "ossuaire", "runes", "pluie_os", "ames"], 1.05, 0.95, 0.82,
              "Les os des catacombes se lèvent !"),
        phase(2, "Malédiction", ["malefice", "cranes", "glas", "etreinte", "cercle_runes", "ames"], 1.1, 0.92, 0.8,
              "Elle trace ses malédictions : gardez vos distances !"),
        # « Spectres »: at its end it hides its soul in its phylactery (an altar of its crypt):
        # it cannot be hurt until the crystal is broken, then « Phylactère brisé » begins.
        phase(3, "Spectres", ["spectres", "ronde", "spirale", "rayon", "malefice", "runes"], 1.12, 0.9, 0.78,
              "Ses spectres se lèvent pour la servir !", summonOnEnter=True, phylactery="phylactere"),
        phase(4, "Phylactère brisé", ["rayon_sang", "runes_sang", "eclats", "malefice", "pluie_os", "murs_os"], 1.15, 0.88,
              0.75, "Son phylactère est brisé : la Liche hurle de rage !", enterSound="sounds/wyrm_roar.wav"),
        phase(5, "Âmes", ["ames_errantes", "puits", "traits", "spirale", "glas", "cercle_runes"], 1.2, 0.85, 0.72,
              "Les âmes perdues tourbillonnent autour d'elle !"),
        phase(6, "Crypte", ["cryptes", "murs_os", "malefice", "runes_sang", "ossuaire", "rayon"], 1.25, 0.82, 0.7,
              "Les cryptes s'ouvrent !", enterSound="sounds/boss_roar.wav"),
        phase(7, "Néant", ["ames", "rayon_sang", "runes_sang", "malefice", "murs_os", "cryptes", "pluie_os", "spirale",
                           "glas", "cranes", "ossuaire", "puits"], 1.3, 0.75, 0.62,
              "Le Néant ! La Liche ne retient plus rien !", enrageTime=40, enterSound="sounds/supernova.wav"),
    ]


def liche():
    return {
        "format": "cosmic.enemy", "id": "liche", "name": "Liche",
        "description": ("Un vieux gardien des catacombes des Confins, qui enferma son âme dans un cristal pour ne jamais "
                        "mourir. Huit phases, huit feux d'âme : il flotte d'autel en autel, cache son âme dans son "
                        "phylactère, et son dernier sort doit être survécu."),
        "family": "miniboss", "animation": SHEETS[0], "size": [70, 200], "scale": 1.25, "hp": 760, "contactDamage": 2,
        "speed": 70, "chaseSpeedMultiplier": 1.2, "flying": True, "knockbackResist": 1, "detectionRange": 1000,
        "attackRange": 900, "loseRange": 4000, "needsLineOfSight": False, "movement": "fly",
        "reaction": "keepDistance", "fleeHealthRatio": 0, "preferredDistance": 260, "hopVelocity": 0,
        "attacks": attacks(),
        "powers": {"teleport": {"enabled": False}, "defend": {"enabled": False}, "reinforcements": {"enabled": False}},
        "damageMultipliers": {"charge_shot": 1.2, "frost_nova": 1.25},
        "rewards": {"score": 25000, "drops": [{"type": "health_orb", "chance": 1.0}, {"type": "energy_orb", "chance": 1.0}]},
        "light": {"color": LIGHTS[0], "radius": 240},
        "sounds": {"hurt": "sounds/enemy_hurt.wav", "death": "sounds/supernova.wav", "alert": "sounds/wyrm_roar.wav"},
        "boss": {"title": "LA LICHE DES CATACOMBES", "phases": phases(), "barIcon": "skull", "barColor": "#b4f566",
                 "finalAttacks": ["dernier_glas", "dernieres_runes", "dernier_rayon", "derniere_pluie", "dernier_glas"],
                 "finalText": "Son dernier sort : survivez !"},
    }


def spectre():
    """Its escort (phases 4 and 6): a pale shade of the catacombs that throws souls in torment."""
    with open(os.path.join(DATA, "wisp.json"), encoding="utf-8") as f:
        d = copy.deepcopy(json.load(f))
    d["id"] = "spectre"
    d["name"] = "Spectre de la Liche"
    d["description"] = "Une ombre pâle des catacombes, liée à la Liche : elle flotte autour d'elle et lance des âmes en peine."
    d["hp"] = 7
    d["tint"] = "#c8f6ff"
    d["light"] = {"color": "#aaf2ff", "radius": 52}
    d["detectionRange"] = 900
    d["loseRange"] = 2000
    d["needsLineOfSight"] = False
    d["attacks"] = [a for a in d["attacks"] if a.get("type") != "summon"]
    for a in d["attacks"]:
        a["name"] = "Âme en peine"
        a["projectile"] = "soul"
        a["projectileRadius"] = 5
    d["powers"]["reinforcements"] = {"enabled": False}
    d["rewards"] = {"score": 340, "drops": [{"type": "energy_orb", "chance": 0.5}]}
    return d


def phylactere():
    """The crystal of its soul: set on an altar at the end of its 4th phase, the Liche cannot be
    hurt until it is broken. It does not attack."""
    return {
        "format": "cosmic.enemy", "id": "phylactere", "name": "Phylactère de la Liche",
        "description": ("Le cristal où la Liche a enfermé son âme, posé sur un autel de sa crypte. Tant qu'il tient, "
                        "rien ne peut la blesser : brisez-le."),
        "family": "turret", "animation": "animations/phylactere.json", "size": [26, 52], "scale": 1.0, "hp": 36,
        "contactDamage": 0, "speed": 0, "chaseSpeedMultiplier": 1.0, "flying": True, "knockbackResist": 1,
        "detectionRange": 2000, "attackRange": 0, "loseRange": 4000, "needsLineOfSight": False,
        "movement": "stationary", "reaction": "none", "fleeHealthRatio": 0, "preferredDistance": 0, "hopVelocity": 0,
        "attacks": [],
        "powers": {"teleport": {"enabled": False}, "defend": {"enabled": False}, "reinforcements": {"enabled": False}},
        "damageMultipliers": {},
        "rewards": {"score": 1500, "drops": []},
        "light": {"color": "#aaf2ff", "radius": 110},
        "sounds": {"hurt": "sounds/shield_block.wav", "death": "sounds/ice_shatter.wav", "alert": ""},
    }


def main():
    for d in (liche(), spectre(), phylactere()):
        with open(os.path.join(DATA, d["id"] + ".json"), "w", encoding="utf-8") as f:
            json.dump(d, f, ensure_ascii=False, indent=2)
            f.write("\n")
        print(d["id"], "PV", d["hp"], "attaques", len(d["attacks"]))


if __name__ == "__main__":
    main()
