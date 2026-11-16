#!/usr/bin/env python3
"""Pyrrhax, le Dragon d'Obsidienne (version 2.48): the giga boss of Les Confins, at the end of
« La Gueule du Dragon » (confins_01, tools/levelgen/confins.py).

An obsidian dragon whose seams hold a living fire. About Singularis' health (760, 95 per phase),
contact 2, shorter wind-ups and combos of three. Eight phases, one every 12.5 % of its health, each
with its own mechanic and its own fire (the sheets of tools/assetgen/characters/boss_e_pyrrhax.py:
one per phase, phases 5-8 « Brisé », the chest scales torn off over a magma heart):

  1 Braise          fans of fireballs, a sweeping breath, a bite from the air (learn to dodge)
  2 Pluie de cendres magma rains, geysers from the floor, a trail of fire under Nova
  3 Tempête d'ailes  its wings pull Nova in, a shock ring with one opening, a dive
  4 Flamme bleue    homing blue flames, a spiral of embers, flames circling around it
  5 Écailles brisées its scales burst: the floor of the arena gives way, lava rises
  6 Feu vert        a breath reflected by the walls, rings, a spiral
  7 Couvée          two whelps of its brood escort it, dives behind Nova
  8 Soleil noir     everything (no brood), faster; rage after 40 s in the phase (the arena
                    closes in), and its last fire to survive when its health runs out.

Writes data/enemies/pyrrhax.json and data/enemies/dragonnet.json (its brood).
Run from the repository root:  python3 tools/contentgen/pyrrhax.py
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

# Clips of its sheets (boss_e_pyrrhax.py): telegraph/attack (a bite and a claw, `fire` on frame 1),
# telegraph_special/special (it inhales, then breathes fire), summon (wings spread, it roars).
CLASSIC = ("telegraph", "attack")
SPECIAL = ("telegraph_special", "special")
RAISE = ("telegraph_special", "summon")
JAWS = [110, -52]    # the mouth on the attack frame, from the feet (sprite pixels)
BREATH = [100, -57]  # the mouth during the breath (the head does not move)

# The fire of each phase (the P1 tone of each ramp of boss_e_pyrrhax).
LIGHTS = ["#ff7a2e", "#ffc84a", "#5ab4ff", "#b583ff", "#ff5ad2", "#4af0a0", "#7ad8ff", "#fff0e0"]
SHEETS = ["animations/pyrrhax.json"] + ["animations/pyrrhax_p%d.json" % k for k in range(2, 9)]


def attacks():
    fire = dict(projectile="fireball", hitEffect="fire_burst")
    magma = dict(projectile="magma_glob", hitEffect="fire_burst")
    ember = dict(projectile="ember", hitEffect="projectile_burst")
    ring = dict(radius=900, knockback=240)
    return [
        # 1 Braise
        _atk("crachat", "Crachat de braise", "spread", 1, 900, 0.6, 0.3, 0.45, 2.2, *CLASSIC, projectileSpeed=210,
             count=5, spread=13, bursts=2, burstInterval=0.35, origin=JAWS, lead=0.6, **fire),
        _atk("souffle", "Souffle d'obsidienne", "laser", 2, 900, 0.8, 1.8, 0.6, 4.5, *SPECIAL, laserLength=900,
             sweepDegrees=110, origin=BREATH, color="#ffb070", beamTexture="flame", beamWidth=26),
        _atk("morsure", "Morsure plongeante", "lunge", 2, 600, 0.6, 0.8, 0.6, 3.0, *CLASSIC, lungeSpeed=400, minRange=60,
             knockback=300, lead=0.8),
        # 2 Pluie de cendres
        _atk("cendres", "Pluie de cendres", "rain", 1, 1200, 0.7, 0.3, 0.5, 3.2, *CLASSIC, projectileSpeed=220, count=10,
             spread=40, gravity=260, rainHeight=360, explosionRadius=18, lead=0.8, **magma),
        _atk("geysers", "Geysers de magma", "eruption", 2, 1200, 0.8, 0.3, 0.6, 4.0, *CLASSIC, projectile="magma_glob",
             projectileSpeed=460, count=6, spread=50, gravity=700, stagger=0.12, lead=0.8,
             warningEffect="eruption_warning_fire", eruptionEffect="eruption_fire", hitEffect="fire_burst"),
        _atk("sillage", "Sillage de feu", "tracker", 2, 1000, 0.7, 0.2, 0.5, 4.0, *CLASSIC, projectile="fireball",
             projectileSpeed=130, duration=2.8, radius=18, height=64, hitEffect="eruption_fire", knockback=260),
        # 3 Tempête d'ailes
        _atk("rafale", "Rafale d'ailes", "vortex", 2, 900, 0.8, 0.2, 0.7, 7.0, *RAISE, pull=110, duration=2.4,
             knockback=280),
        _atk("onde", "Onde de choc", "ring", 2, 1000, 0.75, 0.2, 0.6, 5.0, *SPECIAL, projectileSpeed=185, spread=54,
             **ring),
        _atk("pique", "Piqué du dragon", "lunge", 2, 700, 0.55, 0.9, 0.6, 3.4, *CLASSIC, lungeSpeed=460, minRange=80,
             knockback=320, lead=0.9),
        # 4 Flamme bleue
        _atk("traqueuses", "Flammes bleues", "projectile", 1, 1000, 0.55, 0.25, 0.45, 3.4, *CLASSIC,
             projectileSpeed=150, count=3, spread=30, homing=2.4, homingTime=1.8, origin=JAWS, projectile="solar_flame",
             hitEffect="fire_burst"),
        _atk("spirale", "Spirale de braises", "spiral", 1, 1000, 0.7, 0.2, 0.5, 4.5, *SPECIAL, projectileSpeed=130,
             arms=3, spread=14, burstInterval=0.12, duration=3.5, **ember),
        _atk("orbite", "Couronne de flammes", "orbit", 2, 400, 0.75, 0.2, 0.5, 5.0, *CLASSIC, projectile="fireball",
             projectileRadius=8, count=8, radius=170, spread=95, duration=3.0, knockback=220),
        _atk("souffle_bleu", "Souffle bleu", "laser", 2, 900, 0.75, 1.6, 0.6, 4.8, *SPECIAL, laserLength=900,
             sweepDegrees=130, origin=BREATH, color="#8fd0ff", beamTexture="flame", beamWidth=24),
        # 5 Écailles brisées
        _atk("maree", "Marée de lave", "flood", 2, 2000, 1.2, 0.5, 0.6, 11.0, *RAISE, duration=4.5, height=200),
        # 6 Feu vert
        _atk("reflet", "Souffle réfléchi", "prism", 2, 1400, 1.0, 1.0, 0.6, 4.2, *SPECIAL, bounces=3, laserLength=1600,
             arms=2, origin=BREATH, color="#8affc4", beamTexture="flame", beamWidth=14, knockback=220),
        _atk("anneaux", "Anneaux de feu vert", "ring", 2, 1000, 0.7, 0.2, 0.5, 4.5, *SPECIAL, projectileSpeed=210,
             spread=50, **ring),
        # 7 Couvée
        _atk("couvee", "Couvée", "summon", 0, 1500, 0.9, 0.4, 0.8, 12.0, "summon", "summon", summonId="dragonnet",
             summonCount=2, maxSummoned=4),
        _atk("plongeon", "Plongeon dans le dos", "blink", 2, 1200, 0.6, 0.35, 0.6, 3.8, *CLASSIC, radius=60,
             knockback=280),
        # 8 Soleil noir: its last fire (never chosen: the last stand, EnemyDefinition::finalAttacks)
        _atk("dernier_souffle", "Dernier souffle", "ring", 2, 3000, 0.6, 0.2, 0.3, 0.0, *SPECIAL, projectileSpeed=320,
             spread=60, radius=880, knockback=260),
        _atk("derniere_pluie", "Pluie d'obsidienne", "rain", 1, 3000, 0.6, 0.3, 0.3, 0.0, *CLASSIC,
             projectileSpeed=260, count=14, spread=48, gravity=300, rainHeight=360, lead=0.8, **magma),
    ]


def phase(i, name, attack_ids, speed, cooldown, telegraph, text, combo=3, **kw):
    p = {"name": name, "hpThreshold": round(1.0 - 0.125 * i, 3), "attacks": attack_ids, "speedMultiplier": speed,
         "cooldownMultiplier": cooldown, "combo": combo, "telegraphMultiplier": telegraph, "enterText": text,
         "animation": SHEETS[i], "lightColor": LIGHTS[i]}
    p.update(kw)
    return p


def phases():
    return [
        phase(0, "Braise", ["crachat", "souffle", "morsure"], 1.0, 1.0, 0.85, "Le dragon d'obsidienne s'éveille."),
        phase(1, "Pluie de cendres", ["crachat", "cendres", "geysers", "sillage", "souffle"], 1.05, 0.95, 0.82,
              "Le ciel des Confins pleut des cendres !"),
        phase(2, "Tempête d'ailes", ["rafale", "onde", "pique", "crachat"], 1.1, 0.92, 0.8,
              "Ses ailes déchaînent la tempête !"),
        phase(3, "Flamme bleue", ["traqueuses", "spirale", "orbite", "souffle_bleu"], 1.12, 0.9, 0.78,
              "Sa flamme vire au bleu !"),
        phase(4, "Écailles brisées", ["maree", "geysers", "cendres", "morsure", "crachat"], 1.15, 0.88, 0.75,
              "Ses écailles éclatent : le sol cède !", enterSound="sounds/shell_shatter.wav"),
        phase(5, "Feu vert", ["reflet", "anneaux", "spirale", "traqueuses", "orbite"], 1.2, 0.85, 0.72,
              "Un feu vert monte de son cœur de magma !"),
        phase(6, "Couvée", ["couvee", "plongeon", "souffle", "traqueuses", "cendres"], 1.25, 0.82, 0.7,
              "Pyrrhax appelle sa couvée !", summonOnEnter=True),
        phase(7, "Soleil noir", ["crachat", "cendres", "traqueuses", "rafale", "pique", "plongeon", "maree", "geysers",
                                 "spirale", "reflet", "souffle"], 1.3, 0.75, 0.62,
              "Soleil noir ! Le dragon brûle tout !", enrageTime=40, enterSound="sounds/supernova.wav"),
    ]


def pyrrhax():
    return {
        "format": "cosmic.enemy", "id": "pyrrhax", "name": "Pyrrhax",
        "description": ("Un dragon d'obsidienne au feu vivant, gardien des Confins. Huit phases, huit flammes : ses "
                        "écailles éclatent, le sol cède, et son dernier feu doit être survécu."),
        "family": "miniboss", "animation": SHEETS[0], "size": [110, 75], "scale": 1.25, "hp": 760, "contactDamage": 2,
        "speed": 66, "chaseSpeedMultiplier": 1.3, "flying": True, "knockbackResist": 1, "detectionRange": 1000,
        "attackRange": 900, "loseRange": 4000, "needsLineOfSight": False, "movement": "fly",
        "reaction": "keepDistance", "fleeHealthRatio": 0, "preferredDistance": 250, "hopVelocity": 0,
        "attacks": attacks(),
        "powers": {"teleport": {"enabled": False}, "defend": {"enabled": False}, "reinforcements": {"enabled": False}},
        "damageMultipliers": {"charge_shot": 1.2, "frost_nova": 1.25},
        "rewards": {"score": 22000, "drops": [{"type": "health_orb", "chance": 1.0}, {"type": "energy_orb", "chance": 1.0}]},
        "light": {"color": LIGHTS[0], "radius": 230},
        "sounds": {"hurt": "sounds/enemy_hurt.wav", "death": "sounds/supernova.wav", "alert": "sounds/boss_roar.wav"},
        "boss": {"title": "PYRRHAX, LE DRAGON D'OBSIDIENNE", "phases": phases(), "barIcon": "flame", "barColor": "#ffc49a",
                 "finalAttacks": ["dernier_souffle", "derniere_pluie", "dernier_souffle"],
                 "finalText": "Son dernier feu : survivez !"},
    }


def dragonnet():
    """Its brood (phase 7): a whelp of embers spitting fireballs (no calls of its own)."""
    with open(os.path.join(DATA, "wisp.json"), encoding="utf-8") as f:
        d = copy.deepcopy(json.load(f))
    d["id"] = "dragonnet"
    d["name"] = "Dragonnet de braise"
    d["description"] = "Un petit de la couvée de Pyrrhax, né d'une écaille brûlante. Il crache des boules de feu."
    d["hp"] = 7
    d["tint"] = "#ff9a5a"
    d["light"] = {"color": "#ff9a5a", "radius": 52}
    d["detectionRange"] = 900
    d["loseRange"] = 2000
    d["attacks"] = [a for a in d["attacks"] if a.get("type") != "summon"]
    for a in d["attacks"]:
        a["projectile"] = "fireball"
        a["hitEffect"] = "fire_burst"
    d["rewards"] = {"score": 320, "drops": [{"type": "energy_orb", "chance": 0.5}]}
    return d


def main():
    for d in (pyrrhax(), dragonnet()):
        with open(os.path.join(DATA, d["id"] + ".json"), "w", encoding="utf-8") as f:
            json.dump(d, f, ensure_ascii=False, indent=2)
            f.write("\n")
        print(d["id"], "PV", d["hp"], "attaques", len(d["attacks"]))


if __name__ == "__main__":
    main()
