#!/usr/bin/env python3
"""Singularis, le Cœur Effondré (version 2.33): the giga boss of the Noyau stellaire, at the end
of « Le Cœur d'une Étoile Mourante » (noyau_03, tools/levelgen/stellar_core.py).

A dead star become a consciousness. About six guardians' health (720, 90 per phase), contact 2,
shorter wind-ups and combos of three. Eight phases, one every 12.5 % of its health, each with its
own mechanic and its own colour (the sheets of tools/assetgen/characters/boss_d_singularis.py: one
per phase, phases 5-8 with the shell burst):

  1 Éveil            fans of star shards, a sweeping ray, a falling lunge (learn to dodge)
  2 Pluie d'étoiles  meteor rains (one in a line along the floor), homing shards, a plasma trail
  3 Gravité          a gravity well, a pull toward it (dash out), rings with one opening
  4 Miroirs          two mirror doubles (find the real one), quantum leaps behind Nova
  5 Effondrement     the floor collapses (the level's events), plasma tides, geysers on the ledges
  6 Cage stellaire   cages of starlight, rings, spirals of shards, a belt of meteors
  7 Escorte          stellar shards escort it, two prisms fired at once that cross
  8 Supernova        everything (no mirrors, no escort), faster; rage after 40 s in the phase (the
                     arena closes in), and its last light to survive when its health runs out.

No echo in L'Arène des Échos (tools/levelgen/stellar_bosses.py leaves it out).
Writes data/enemies/singularis.json and data/enemies/eclat_stellaire.json (its escort).
Run from the repository root:  python3 tools/contentgen/singularis.py
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

# Clips of its sheets: telegraph/attack (a sun in the right palm), telegraph_special/special (the
# shell opens), summon, teleport_out (a quantum leap).
CLASSIC = ("telegraph", "attack")
SPECIAL = ("telegraph_special", "special")
RAISE = ("telegraph_special", "summon")
PALM = [64, -34]  # the right palm on the attack frame, from the feet (sprite pixels)
EYE = [10, -36]   # the singularity eye

# The light of each phase (P1 of its ramp in boss_d_singularis.PHASES).
LIGHTS = ["#ff9455", "#ffcf4a", "#c08aff", "#7cf2ff", "#ff7ad2", "#6dfab0", "#8fb0ff", "#e8f6ff"]
SHEETS = ["animations/singularis.json"] + ["animations/singularis_p%d.json" % k for k in range(2, 9)]


def attacks():
    shard = dict(projectile="star_shard", hitEffect="projectile_burst")
    meteor = dict(projectile="meteor", hitEffect="fire_burst")
    ring = dict(radius=900, knockback=240)
    return [
        # 1 Éveil
        _atk("eventail", "Éventail d'étoiles", "spread", 1, 900, 0.6, 0.3, 0.45, 2.2, *CLASSIC, projectileSpeed=200,
             count=7, spread=11, bursts=2, burstInterval=0.35, origin=PALM, lead=0.6, **shard),
        _atk("rayon", "Rayon de la naine rouge", "laser", 2, 900, 0.8, 1.8, 0.6, 4.5, *SPECIAL, laserLength=900,
             sweepDegrees=120, origin=EYE, color="#ffd2b0", beamTexture="star", beamWidth=22),
        _atk("chute", "Chute d'astre", "lunge", 2, 600, 0.6, 0.8, 0.6, 3.0, *CLASSIC, lungeSpeed=380, minRange=60,
             knockback=300, lead=0.8),
        # 2 Pluie d'étoiles
        _atk("meteores", "Pluie d'étoiles", "rain", 1, 1200, 0.7, 0.3, 0.5, 3.2, *CLASSIC, projectileSpeed=220, count=10,
             spread=40, gravity=260, rainHeight=360, explosionRadius=18, lead=0.8, **meteor),
        _atk("filantes", "Étoiles filantes", "rain", 1, 1200, 0.7, 0.3, 0.5, 4.5, *CLASSIC, projectileSpeed=240,
             count=12, spread=44, gravity=300, rainHeight=360, stagger=0.1, lead=0.9, **meteor),
        _atk("traqueuses", "Étoiles traqueuses", "projectile", 1, 1000, 0.55, 0.25, 0.45, 3.4, *CLASSIC,
             projectileSpeed=150, count=3, spread=30, homing=2.4, homingTime=1.8, origin=PALM, **shard),
        _atk("sillage", "Sillage de plasma", "tracker", 2, 1000, 0.7, 0.2, 0.5, 4.0, *CLASSIC, projectile="meteor",
             projectileSpeed=125, duration=2.8, radius=18, height=64, hitEffect="eruption_fire", knockback=260),
        # 3 Gravité
        _atk("puits", "Puits d'effondrement", "well", 2, 900, 0.8, 0.2, 0.6, 8.0, *SPECIAL, pull=95, radius=26,
             duration=4.5, knockback=240),
        _atk("aspiration", "Gravité", "vortex", 2, 900, 0.8, 0.2, 0.7, 7.0, *SPECIAL, pull=105, duration=2.4,
             knockback=280),
        _atk("onde", "Onde de marée", "ring", 2, 1000, 0.75, 0.2, 0.6, 5.0, *SPECIAL, projectileSpeed=180, spread=54,
             **ring),
        # 4 Miroirs
        _atk("reflets", "Reflets d'étoile", "mirror", 0, 1200, 0.85, 0.4, 0.6, 10.0, *RAISE, duration=14),
        _atk("saut", "Saut quantique", "blink", 2, 1200, 0.55, 0.35, 0.6, 3.6, "teleport_out", "attack", radius=56,
             knockback=260),
        # 5 Effondrement
        _atk("maree", "Marée de plasma", "flood", 2, 2000, 1.2, 0.5, 0.6, 11.0, *RAISE, duration=4.5, height=200,
             look="plasma"),
        _atk("geysers", "Geysers stellaires", "eruption", 2, 1200, 0.8, 0.3, 0.6, 4.0, *CLASSIC, projectile="star_shard",
             projectileSpeed=460, count=6, spread=50, gravity=700, stagger=0.12, lead=0.8,
             warningEffect="eruption_warning_fire", eruptionEffect="eruption_fire", hitEffect="fire_burst"),
        # 6 Cage stellaire
        _atk("cage", "Cage stellaire", "cage", 1, 1000, 0.9, 0.2, 0.7, 8.0, *RAISE, projectile="star_shard",
             projectileSpeed=110, burstInterval=0.35, duration=4.5, radius=110, height=160, knockback=180, look="star"),
        _atk("couronne", "Anneaux de la couronne", "ring", 2, 1000, 0.7, 0.2, 0.5, 4.5, *SPECIAL, projectileSpeed=210,
             spread=50, **ring),
        _atk("spirale", "Spirale de naine blanche", "spiral", 1, 1000, 0.7, 0.2, 0.5, 4.5, *CLASSIC, projectileSpeed=130,
             arms=3, spread=14, burstInterval=0.12, duration=3.5, **shard),
        _atk("ceinture", "Ceinture d'astéroïdes", "orbit", 2, 400, 0.75, 0.2, 0.5, 5.0, *CLASSIC, projectile="meteor",
             projectileRadius=8, count=8, radius=170, spread=95, duration=3.0, knockback=220),
        # 7 Escorte
        _atk("escorte", "Escorte d'astres", "summon", 0, 1500, 0.9, 0.4, 0.8, 12.0, "summon", "summon",
             summonId="eclat_stellaire", summonCount=2, maxSummoned=4),
        _atk("prismes", "Prismes croisés", "prism", 2, 1400, 1.0, 1.0, 0.6, 4.2, *SPECIAL, bounces=3, laserLength=1600,
             arms=2, origin=EYE, color="#bcd2ff", beamTexture="prism", beamWidth=12, knockback=220),
        # 8 Supernova: its last light (never chosen: the last stand, EnemyDefinition::finalAttacks)
        _atk("derniere_lumiere", "Dernière lumière", "ring", 2, 3000, 0.6, 0.2, 0.3, 0.0, *SPECIAL, projectileSpeed=320,
             spread=60, radius=880, knockback=260),
        _atk("derniere_pluie", "Pluie de cendres stellaires", "rain", 1, 3000, 0.6, 0.3, 0.3, 0.0, *CLASSIC,
             projectileSpeed=260, count=14, spread=48, gravity=300, rainHeight=360, lead=0.8, **meteor),
    ]


def phase(i, name, attack_ids, speed, cooldown, telegraph, text, combo=3, **kw):
    p = {"name": name, "hpThreshold": round(1.0 - 0.125 * i, 3), "attacks": attack_ids, "speedMultiplier": speed,
         "cooldownMultiplier": cooldown, "combo": combo, "telegraphMultiplier": telegraph, "enterText": text,
         "animation": SHEETS[i], "lightColor": LIGHTS[i]}
    p.update(kw)
    return p


def phases():
    return [
        phase(0, "Éveil", ["eventail", "rayon", "chute"], 1.0, 1.0, 0.85, "Une étoile morte ouvre son œil."),
        phase(1, "Pluie d'étoiles", ["eventail", "meteores", "filantes", "traqueuses", "sillage", "rayon"], 1.05, 0.95,
              0.82, "Les étoiles tombent du ciel mort !"),
        phase(2, "Gravité", ["puits", "aspiration", "onde", "traqueuses", "chute"], 1.1, 0.92, 0.8,
              "La gravité de Singularis vous attire !"),
        phase(3, "Miroirs", ["reflets", "saut", "eventail", "rayon"], 1.12, 0.9, 0.78,
              "Singularis se multiplie dans ses reflets !"),
        phase(4, "Effondrement", ["maree", "geysers", "meteores", "chute", "eventail"], 1.15, 0.88, 0.75,
              "La coquille éclate : le sol s'effondre !", enterSound="sounds/shell_shatter.wav"),
        phase(5, "Cage stellaire", ["cage", "couronne", "spirale", "ceinture", "traqueuses"], 1.2, 0.85, 0.72,
              "Une cage de lumière se referme !"),
        phase(6, "Escorte", ["escorte", "prismes", "rayon", "traqueuses", "meteores"], 1.25, 0.82, 0.7,
              "Singularis appelle son escorte d'astres !", summonOnEnter=True),
        phase(7, "Supernova", ["eventail", "meteores", "traqueuses", "puits", "aspiration", "saut", "maree", "geysers",
                               "cage", "spirale", "prismes"], 1.3, 0.75, 0.62,
              "Supernova ! L'étoile va exploser !", enrageTime=40, enterSound="sounds/supernova.wav"),
    ]


def singularis():
    return {
        "format": "cosmic.enemy", "id": "singularis", "name": "Singularis",
        "description": ("Une étoile morte devenue conscience, au cœur du Noyau. Huit phases, huit couleurs : sa "
                        "coquille éclate, le sol s'effondre, et sa dernière lumière doit être survécue."),
        "family": "miniboss", "animation": SHEETS[0], "size": [90, 90], "scale": 1.25, "hp": 720, "contactDamage": 2,
        "speed": 62, "chaseSpeedMultiplier": 1.3, "flying": True, "knockbackResist": 1, "detectionRange": 1000,
        "attackRange": 900, "loseRange": 4000, "needsLineOfSight": False, "movement": "fly",
        "reaction": "keepDistance", "fleeHealthRatio": 0, "preferredDistance": 240, "hopVelocity": 0,
        "attacks": attacks(),
        "powers": {"teleport": {"enabled": False}, "defend": {"enabled": False}, "reinforcements": {"enabled": False}},
        "damageMultipliers": {"charge_shot": 1.2},
        "rewards": {"score": 20000, "drops": [{"type": "health_orb", "chance": 1.0}, {"type": "energy_orb", "chance": 1.0}]},
        "light": {"color": LIGHTS[0], "radius": 220},
        "sounds": {"hurt": "sounds/enemy_hurt.wav", "death": "sounds/supernova.wav", "alert": "sounds/star_roar.wav"},
        "boss": {"title": "SINGULARIS, LE CŒUR EFFONDRÉ", "phases": phases(), "barIcon": "eye", "barColor": "#ffe2c4",
                 "finalAttacks": ["derniere_lumiere", "derniere_pluie", "derniere_lumiere"],
                 "finalText": "Sa dernière lumière : survivez !"},
    }


def eclat_stellaire():
    """Its escort (phase 7): a wisp of starlight shooting star shards (no calls of its own)."""
    with open(os.path.join(DATA, "wisp.json"), encoding="utf-8") as f:
        d = copy.deepcopy(json.load(f))
    d["id"] = "eclat_stellaire"
    d["name"] = "Éclat stellaire"
    d["description"] = "Un éclat de l'étoile morte, détaché par Singularis pour l'escorter. Il tire des éclats de lumière."
    d["hp"] = 6
    d["tint"] = "#fff1c9"
    d["light"] = {"color": "#fff1c9", "radius": 48}
    d["detectionRange"] = 900
    d["loseRange"] = 2000
    d["attacks"] = [a for a in d["attacks"] if a.get("type") != "summon"]
    for a in d["attacks"]:
        a["projectile"] = "star_shard"
        a["hitEffect"] = "projectile_burst"
    d["rewards"] = {"score": 300, "drops": [{"type": "energy_orb", "chance": 0.5}]}
    return d


def main():
    for d in (singularis(), eclat_stellaire()):
        with open(os.path.join(DATA, d["id"] + ".json"), "w", encoding="utf-8") as f:
            json.dump(d, f, ensure_ascii=False, indent=2)
            f.write("\n")
        print(d["id"], "PV", d["hp"], "attaques", len(d["attacks"]))


if __name__ == "__main__":
    main()
