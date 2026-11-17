#!/usr/bin/env python3
"""Le Phénix Stellaire (version 2.55): the fourth giga boss of Les Confins, at the end of
« Le Nid de Cendres » (confins_04, tools/levelgen/confins.py).

A phoenix of starlight and plasma that burns itself to ashes and is born again from them. It
flies (the engine's flying movement, no teleport) and glides between its blows (« vol plané »:
wide swoops from one side of Nova to the other, above her), and it fights with its feathers, its
wings, the light of its sun and its ashes. Health 720 (90 per phase, and 90 more given back by its
rebirth), contact 2, combos of three. Eight phases, one every 12.5 % of its health, each with its
own light (the sheets of tools/assetgen/characters/boss_h_phenix.py: one per phase, the light of the
feathers recoloured; phases 5-8 the reborn form, feathers of fire):

  1 Aube         fans of feathers, a solar dive through Nova, the breath of a star, a march of
                 columns of sunlight, a blade of feathers across the arena (jump it or stay down)
  2 Zénith       the sun at its zenith: columns closing in from both walls, a rain of feathers, a
                 solar ring with one opening, its dives
  3 Aurore       blades two by two (low, then high), the beat of its wings pushes Nova away,
                 homing feathers, dives two by two, the breath of dawn pulls her in
  4 Nébuleuse    a crown of feathers circling it, a spiral of feathers, crossing beams of
                 nebula, the heart of its nebula pulls her and bends her shots
  5 Cendres      ash falls and veils the arena (around Nova the view stays clear), clouds of ash,
                 a rain of embers; at the end of the phase it burns to ashes on the floor of its
                 nest (it cannot be hurt, embers fly out of them), then is reborn with the health
                 of its phase: 90 health given back, its fire bursting around its ashes
  6 Renaissance  reborn: columns from both walls, blades two by two, homing feathers, dives
  7 Comète       sparks torn from its feathers escort it, comets three by three, a rain of
                 comets, its blades
  8 Supernova    everything, faster; rage after 40 s in the phase (the arena closes in), and its
                 last dawn to survive when its health runs out.

Its own attack types (game/enemies/PhoenixAttacks.cpp): « Colonnes solaires » (sunbeam: columns of
sunlight fall from the roof onto marks shown on the ground, a march through where Nova stands, or
pairs from both walls closing in on her) and « Lame d'ailes » (sweep: a blade of feathers across
the whole arena, low or high, then the other). Right under it, or beside it, is no shelter (the
dragon's rules, game/enemies/DragonAttacks.cpp: it prefers what comes down on its prey).

Looks of its own (appearance only, not in the fingerprints): its feathers (projectile « feather »,
drawn by the game), its clouds of ash (CloudShot `look` « ash »), the ash veil of its 5th phase
(BossPhase `ashes`), its arena's embers under the ash (damage zones `style` « embers ») and its
perches of embers that burn and are reborn (crumble platforms `style` « ember »,
tools/levelgen/confins.py).

Writes data/enemies/phenix.json and data/enemies/etincelle.json (its escort).
Run from the repository root:  python3 tools/contentgen/phenix.py
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

# Clips of its sheets (boss_h_phenix.py): telegraph/attack (the wings rise, light gathers at the
# beak and the wing tips, then the wing slash: `fire` on frame 1), telegraph_special/special (the
# chest star brightens, then starfire at the beak), summon (wings spread wide, a burst of feathers
# of light), move (it glides), roar (phase change; the 5th sheet's: it bursts into flames and is
# reborn), death (it turns to ash: its « Cendres » too).
CLASSIC = ("telegraph", "attack")
SPECIAL = ("telegraph_special", "special")
RAISE = ("telegraph_special", "summon")
WINGS = ("telegraph", "summon")      # the wings rise, then spread wide: the blade leaves them
DIVE = ("telegraph", "attack")       # wings raised, then the slash through
MOUTH = [87, -101]  # just in front of the beak on the attack frame, from the feet (sprite pixels)
BEAK = [80, -106]   # its beak during the starfire (the head does not move on the four frames)

# The light of each phase (the P1 tone of each ramp of boss_h_phenix: or, blanc-jaune, rose,
# violet, gris-orangé, cramoisi, cyan, blanc incandescent).
LIGHTS = ["#ffd04a", "#fff7a8", "#ff9ac6", "#c48eff", "#e89c6c", "#ff6e60", "#7aeeff", "#f3f4ff"]
SHEETS = ["animations/phenix.json"] + ["animations/phenix_p%d.json" % k for k in range(2, 9)]


def attacks():
    feather = dict(projectile="feather", projectileRadius=6, hitEffect="fire_burst")
    ember = dict(projectile="ember", hitEffect="fire_burst")
    ring = dict(radius=900, knockback=240)
    sun = dict(radius=22, duration=0.35, knockback=260)
    blade = dict(projectile="feather", projectileSpeed=330, height=30, projectileRadius=6, burstInterval=1.15,
                 knockback=240, hitEffect="fire_burst")
    dive = dict(radius=240, burstInterval=0.6, lead=0.6, knockback=340)
    return [
        # 1 Aube
        _atk("plumes", "Éventail de plumes", "spread", 1, 900, 0.6, 0.3, 0.45, 2.4, *CLASSIC, projectileSpeed=230,
             count=5, spread=12, bursts=2, burstInterval=0.35, origin=MOUTH, lead=0.6, **feather),
        _atk("pique", "Piqué solaire", "dive", 2, 1100, 0.7, 0.2, 0.6, 3.4, *DIVE, lungeSpeed=760, count=1, **dive),
        _atk("souffle", "Souffle d'étoile", "laser", 2, 900, 0.8, 1.6, 0.6, 4.6, *SPECIAL, laserLength=900,
             sweepDegrees=110, origin=BEAK, color="#ffe08a", beamTexture="star", beamWidth=22),
        # Columns of sunlight: a march from its side through where Nova will be, and beyond.
        _atk("colonnes", "Colonnes solaires", "sunbeam", 2, 1400, 0.7, 0.5, 0.5, 5.5, *RAISE, count=5, spread=92,
             stagger=0.2, burstInterval=0.6, arms=1, lead=0.6, **sun),
        # A blade of feathers across the whole arena: low (jump over it) or high (stay down).
        _atk("lame", "Lame d'ailes", "sweep", 2, 1600, 0.9, 0.4, 0.6, 6.5, *WINGS, count=1, **blade),
        # 2 Zénith
        # Pairs of columns from both walls closing in on her, the last one on her.
        _atk("midi", "Midi solaire", "sunbeam", 2, 1600, 0.8, 0.6, 0.5, 7.0, *RAISE, count=4, spread=110, stagger=0.24,
             burstInterval=0.7, arms=2, lead=0.5, **sun),
        _atk("pluie_plumes", "Pluie de plumes", "rain", 1, 1200, 0.7, 0.3, 0.5, 3.4, *RAISE, projectileSpeed=240,
             count=10, spread=42, gravity=200, rainHeight=360, lead=0.8, **feather),
        _atk("couronne", "Couronne solaire", "ring", 2, 1000, 0.75, 0.2, 0.6, 5.0, *SPECIAL, projectileSpeed=190,
             spread=54, **ring),
        # 3 Aurore
        _atk("lame_double", "Double lame d'ailes", "sweep", 2, 1600, 0.85, 0.4, 0.6, 7.5, *WINGS, count=2, **blade),
        # Its wings push Nova away (105 px/s, below her run), feathers in the wind.
        _atk("battement", "Battement d'ailes", "gust", 1, 1000, 0.8, 0.2, 0.6, 7.0, *RAISE, duration=2.2, pull=105,
             count=3, burstInterval=0.5, projectileSpeed=240, spread=14, **feather),
        _atk("traqueuses", "Plumes traqueuses", "projectile", 1, 1000, 0.55, 0.25, 0.45, 3.4, *CLASSIC,
             projectileSpeed=160, count=3, spread=30, homing=2.4, homingTime=1.8, origin=MOUTH, **feather),
        _atk("double_pique", "Double piqué", "dive", 2, 1100, 0.65, 0.2, 0.6, 5.0, *DIVE, lungeSpeed=820, count=2,
             **dive),
        _atk("aurore", "Souffle de l'aurore", "vortex", 2, 900, 0.8, 0.2, 0.7, 7.0, *RAISE, pull=110, duration=2.4,
             knockback=280),
        # 4 Nébuleuse
        _atk("couronne_plumes", "Couronne de plumes", "orbit", 2, 400, 0.75, 0.2, 0.5, 5.0, *SPECIAL,
             projectile="feather", projectileRadius=7, count=8, radius=180, spread=95, duration=3.0, knockback=220),
        _atk("spirale", "Spirale de plumes", "spiral", 1, 1000, 0.7, 0.2, 0.5, 4.5, *SPECIAL, projectileSpeed=130, arms=3,
             spread=14, burstInterval=0.12, duration=3.5, **feather),
        _atk("reflets", "Reflets de nébuleuse", "prism", 2, 1400, 1.0, 1.0, 0.6, 4.4, *SPECIAL, bounces=3,
             laserLength=1600, arms=2, origin=BEAK, color="#d8b8ff", beamTexture="prism", beamWidth=14, knockback=220),
        _atk("coeur", "Cœur de nébuleuse", "well", 2, 900, 0.8, 0.2, 0.6, 8.0, *SPECIAL, pull=95, radius=26, duration=4.5,
             knockback=240),
        # 5 Cendres
        # Embers lobbed onto the marks shown: a cloud of ash stays where each one bursts.
        _atk("nuees_cendres", "Nuées de cendres", "cloud_shot", 1, 1300, 0.7, 0.3, 0.5, 5.5, *CLASSIC,
             projectileSpeed=320, gravity=520, count=3, spread=110, radius=58, duration=3.4, projectileRadius=8,
             origin=MOUTH, lead=0.7, look="ash", **ember),
        _atk("pluie_cendres", "Pluie de cendres", "rain", 1, 1200, 0.7, 0.3, 0.5, 3.2, *RAISE, projectileSpeed=220,
             count=10, spread=40, gravity=260, rainHeight=360, explosionRadius=18, lead=0.8, **ember),
        # 6 Renaissance
        _atk("anneaux", "Anneaux de renaissance", "ring", 2, 1000, 0.7, 0.2, 0.5, 4.5, *SPECIAL, projectileSpeed=210,
             spread=50, **ring),
        # 7 Comète
        _atk("etincelles", "Étincelles", "summon", 0, 1500, 0.9, 0.4, 0.8, 12.0, "summon", "summon",
             summonId="etincelle", summonCount=2, maxSummoned=4),
        _atk("ronde_cometes", "Ronde des comètes", "dive", 2, 1200, 0.6, 0.2, 0.6, 6.5, *DIVE, lungeSpeed=880, count=3,
             radius=260, burstInterval=0.5, lead=0.65, knockback=340),
        _atk("pluie_cometes", "Pluie de comètes", "rain", 1, 1200, 0.75, 0.3, 0.5, 3.8, *RAISE, projectile="meteor",
             projectileSpeed=280, count=8, spread=56, gravity=300, rainHeight=380, explosionRadius=20, lead=0.8,
             projectileRadius=8, hitEffect="fire_burst"),
        # Its last dawn (never chosen: the last stand, EnemyDefinition::finalAttacks)
        _atk("derniere_aube", "Dernière aube", "ring", 2, 3000, 0.6, 0.2, 0.3, 0.0, *SPECIAL, projectileSpeed=320,
             spread=60, radius=880, knockback=260),
        _atk("dernier_zenith", "Dernier zénith", "sunbeam", 2, 3000, 0.7, 0.4, 0.3, 0.0, *RAISE, count=5, spread=100,
             stagger=0.18, burstInterval=0.6, arms=2, lead=0.5, **sun),
        _atk("dernier_envol", "Dernier envol", "dive", 2, 3000, 0.7, 0.2, 0.3, 0.0, *DIVE, lungeSpeed=900, count=3,
             radius=260, burstInterval=0.55, lead=0.6, knockback=340),
        _atk("dernieres_plumes", "Dernières plumes", "rain", 1, 3000, 0.6, 0.3, 0.3, 0.0, *RAISE, projectileSpeed=260,
             count=14, spread=48, gravity=300, rainHeight=360, lead=0.8, **feather),
    ]


def phase(i, name, attack_ids, speed, cooldown, telegraph, text, combo=3, **kw):
    p = {"name": name, "hpThreshold": round(1.0 - 0.125 * i, 3), "attacks": attack_ids, "speedMultiplier": speed,
         "cooldownMultiplier": cooldown, "combo": combo, "telegraphMultiplier": telegraph, "enterText": text,
         "animation": SHEETS[i], "lightColor": LIGHTS[i]}
    p.update(kw)
    return p


def phases():
    return [
        phase(0, "Aube", ["plumes", "pique", "souffle", "colonnes", "lame"], 1.0, 1.0, 0.85,
              "Le Phénix s'éveille avec l'aube."),
        phase(1, "Zénith", ["midi", "pluie_plumes", "couronne", "colonnes", "plumes", "pique"], 1.05, 0.95, 0.82,
              "Le soleil est au zénith : sa lumière tombe d'aplomb !"),
        phase(2, "Aurore", ["lame_double", "battement", "traqueuses", "double_pique", "aurore", "plumes"], 1.1, 0.92, 0.8,
              "Ses ailes déchirent l'aurore !"),
        phase(3, "Nébuleuse", ["couronne_plumes", "spirale", "reflets", "coeur", "pique", "colonnes"], 1.12, 0.9, 0.78,
              "Il plane au cœur de sa nébuleuse !"),
        # « Cendres »: ash veils the arena; at its end it burns to ashes and is reborn with the
        # health of its phase (its share at the start of it), then « Renaissance » begins.
        phase(4, "Cendres", ["nuees_cendres", "pluie_cendres", "colonnes", "battement", "lame", "pique", "plumes"], 1.15, 0.88,
              0.75, "Ses plumes s'embrasent : les cendres pleuvent !", enterSound="sounds/boss_roar_fire.wav",
              rebirth=0.5, ashes=True),
        phase(5, "Renaissance", ["midi", "lame_double", "traqueuses", "double_pique", "anneaux", "spirale"], 1.2, 0.85,
              0.72, "Le Phénix renaît de ses cendres !", enterSound="sounds/star_roar.wav"),
        phase(6, "Comète", ["etincelles", "ronde_cometes", "pluie_cometes", "lame", "colonnes", "plumes"], 1.25, 0.82, 0.7,
              "Il file comme une comète, ses étincelles avec lui !", summonOnEnter=True),
        phase(7, "Supernova", ["plumes", "ronde_cometes", "midi", "lame_double", "traqueuses", "pluie_cometes",
                               "nuees_cendres", "couronne_plumes", "spirale", "reflets", "souffle", "battement"], 1.3, 0.75,
              0.62, "Supernova ! Le Phénix brûle de toute sa lumière !", enrageTime=40, enterSound="sounds/supernova.wav"),
    ]


def phenix():
    return {
        "format": "cosmic.enemy", "id": "phenix", "name": "Phénix",
        "description": ("Un oiseau de lumière stellaire qui se consume et renaît de ses cendres, aux Confins du système. "
                        "Huit phases, huit lueurs : il se consume, renaît, et sa dernière aube doit être survécue."),
        "family": "miniboss", "animation": SHEETS[0], "size": [80, 120], "scale": 1.25, "hp": 720, "contactDamage": 2,
        "speed": 75, "chaseSpeedMultiplier": 1.3, "flying": True, "knockbackResist": 1, "detectionRange": 1000,
        "attackRange": 900, "loseRange": 4000, "needsLineOfSight": False, "movement": "fly",
        "reaction": "keepDistance", "fleeHealthRatio": 0, "preferredDistance": 260, "hopVelocity": 0,
        "attacks": attacks(),
        "powers": {"teleport": {"enabled": False}, "defend": {"enabled": False}, "reinforcements": {"enabled": False}},
        "damageMultipliers": {"charge_shot": 1.2, "frost_nova": 1.25},
        "rewards": {"score": 24000, "drops": [{"type": "health_orb", "chance": 1.0}, {"type": "energy_orb", "chance": 1.0}]},
        "light": {"color": LIGHTS[0], "radius": 240},
        "sounds": {"hurt": "sounds/enemy_hurt.wav", "death": "sounds/supernova.wav", "alert": "sounds/star_roar.wav"},
        "boss": {"title": "LE PHÉNIX STELLAIRE", "phases": phases(), "barIcon": "star", "barColor": "#ffd88a",
                 "finalAttacks": ["derniere_aube", "dernier_zenith", "dernier_envol", "dernieres_plumes", "derniere_aube"],
                 "finalText": "Sa dernière aube : survivez !"},
    }


def etincelle():
    """Its escort (phase 7): a spark torn from its feathers (feathers of fire, no calls)."""
    with open(os.path.join(DATA, "wisp.json"), encoding="utf-8") as f:
        d = copy.deepcopy(json.load(f))
    d["id"] = "etincelle"
    d["name"] = "Étincelle du Phénix"
    d["description"] = "Une étincelle arrachée aux plumes du Phénix, qui vole encore et lance de petites plumes de feu."
    d["hp"] = 7
    d["tint"] = "#ffd08a"
    d["light"] = {"color": "#ffb04a", "radius": 52}
    d["detectionRange"] = 900
    d["loseRange"] = 2000
    d["attacks"] = [a for a in d["attacks"] if a.get("type") != "summon"]
    for a in d["attacks"]:
        a["name"] = "Plumes de feu"
        a["projectile"] = "feather"
        a["projectileRadius"] = 5
        a["hitEffect"] = "fire_burst"
    d["rewards"] = {"score": 340, "drops": [{"type": "energy_orb", "chance": 0.5}]}
    return d


def main():
    for d in (phenix(), etincelle()):
        with open(os.path.join(DATA, d["id"] + ".json"), "w", encoding="utf-8") as f:
            json.dump(d, f, ensure_ascii=False, indent=2)
            f.write("\n")
        print(d["id"], "PV", d["hp"], "attaques", len(d["attacks"]))


if __name__ == "__main__":
    main()
