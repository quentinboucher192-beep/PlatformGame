#!/usr/bin/env python3
"""Le Titan Mécanique (version 2.58): the sixth and last giga boss of Les Confins, at the end of
« La Fonderie Engloutie » (confins_06, tools/levelgen/confins.py).

A war machine left running in a drowned foundry under the Confins (data/stories/titan.json): a
colossus of gunmetal and brass walking on two heavy legs, a piston fist, a rotary cannon, a reactor
in its chest, a missile pod in its back. It walks (the golem's steps: game/enemies/GolemAttacks.cpp)
and fights with the machinery of its foundry. Health 800 (100 per phase), contact 2, combos of
three. Eight phases, one every 12.5 % of its health, each with its own light (the sheets of
tools/assetgen/characters/boss_j_titan.py: one per phase; phases 5-8 the damaged form):

  1 Démarrage       its piston fist (shockwaves along the floor), its rotary cannon, the presses of
                    its foundry (every other one), shells lobbed
  2 Vapeur          its conveyor belts carry Nova toward it, the presses in a march, its fist, its
                    cannon, geysers of steam out of the floor
  3 Surtension      its missiles lock onto Nova one after another, electric waves (a ring with one
                    opening), arcs bouncing off the walls, its belts, its presses
  4 Laser           the laser of its cannon sweeps the arena, the presses all but one (the column
                    spared lit green), its missiles; at the end of the phase it stalls (« Calage »):
                    it cannot be hurt and three pressure valves appear in its foundry: all turned
                    (broken) before it restarts (14 s), its boiler bursts; otherwise it restarts
                    with a pressure wave along the floor
  5 Surchauffe      damaged, it heats faster: steam geysers, presses in a march, its missiles, its
                    belts carry Nova away from it (toward the magma of the broken crust)
  6 Plasma          its drones escort it, a crown of plasma circling it, missiles, presses
  7 Arc             arcs, electric waves, missiles, the presses all but one, its belts, its drones
  8 Cœur nucléaire  everything, faster; rage after 40 s in the phase (the arena closes in), and
                    its last salvo to survive when its health runs out.

Its heat (« Surchauffe », BossPhase `heat`): each attack it ends fills its gauge (a quarter, a
third...); full, it vents its steam: low jets along the ground on both sides, then its core laid
bare: it takes 1.8 times the damage for 3.6 s. The HUD shows the gauge.

Its own attack types (game/enemies/TitanAttacks.cpp): « Presses hydrauliques » (press), « Missiles à
verrouillage » (missile) and « Tapis roulants » (belt).

Looks of its own (appearance only, not in the fingerprints): its foundry's presses, conveyor belts
and valve spots (level objects « press », « conveyor », « valve_spot », tools/levelgen/confins.py),
the steam of its vents (effects « steam_puff », « steam_burst »).

Writes data/enemies/titan.json, data/enemies/drone_titan.json (its escort) and data/enemies/vanne.json
(its pressure valves).
Run from the repository root:  python3 tools/contentgen/titan.py
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

# Clips of its sheets (boss_j_titan.py): telegraph/attack (the piston fist winds back over its
# shoulder, then the punch slams the ground: `impact` on frame 2), telegraph_special/special (the
# reactor brightens, the vents open, the cannon levels, then the beam loop: the rotary cannon
# fires), summon (its back hatch opens: drones, missiles), roar (steam blasts and a horn; the
# phase-5 sheet's: the chest plate blows off), death.
PUNCH = ("telegraph", "attack")
CANNON = ("telegraph_special", "special")
POD = ("telegraph", "summon")        # its back hatch opens: missiles, drones
HORN = ("telegraph_special", "roar")  # steam and its horn: the presses, the belts
MUZZLE = [103, -106]  # the cannon's muzzle during `special` (it does not move on its four frames)
FIST = [57, -22]      # the fist on the ground at the impact of the punch

# The light of each phase (the P1 tone of each ramp of boss_j_titan: ambre, blanc orangé, bleu
# électrique, rouge, orange-rouge, magenta, vert, blanc incandescent).
LIGHTS = ["#ffc23c", "#ffdcb0", "#78d6ff", "#ff6c6c", "#ff7c3c", "#ff72de", "#7eff8c", "#f0fcff"]
SHEETS = ["animations/titan.json"] + ["animations/titan_p%d.json" % k for k in range(2, 9)]
# The share of its heat gauge each attack fills: it vents after 4 attacks, then 3 (phases 3-4),
# then every 3 (its damaged form, faster).
HEAT = [0.25, 0.25, 0.34, 0.34, 0.34, 0.34, 0.34, 0.34]


def attacks():
    bolt = dict(projectile="energy_disc", projectileRadius=5, hitEffect="projectile_burst")
    shell = dict(projectile="bomb", projectileRadius=7, explosionRadius=26, hitEffect="fire_burst")
    ring = dict(radius=900, knockback=240)
    press = dict(burstInterval=0.95, duration=0.6, knockback=320)
    missile = dict(burstInterval=0.9, stagger=0.32, duration=0.8, radius=34, knockback=260)
    return [
        # 1 Démarrage
        # Its piston fist slams the ground: shockwaves run along the floor both ways.
        _atk("poing", "Poing-piston", "slam", 2, 240, 0.75, 0.35, 0.7, 2.4, *PUNCH, projectileSpeed=260, knockback=300),
        _atk("mitraille", "Canon rotatif", "spread", 1, 900, 0.6, 0.6, 0.5, 2.8, *CANNON, projectileSpeed=260, count=3,
             spread=9, bursts=3, burstInterval=0.18, origin=MUZZLE, lead=0.6, **bolt),
        # Every other press, the half over Nova first, then the other half.
        _atk("presses", "Presses hydrauliques", "press", 2, 1500, 0.7, 0.3, 0.5, 6.5, *HORN, count=2, arms=1, **press),
        _atk("obus", "Obus", "arc", 1, 900, 0.65, 0.25, 0.5, 3.4, *CANNON, projectileSpeed=300, count=3, spread=16,
             gravity=420, origin=MUZZLE, lead=0.7, **shell),
        # 2 Vapeur
        # Its belts carry her toward it (90 px/s, she runs at 150).
        _atk("tapis", "Tapis roulants", "belt", 0, 1600, 0.6, 0.3, 0.4, 9.0, *HORN, pull=90, burstInterval=0.9, duration=5.0),
        # The presses one after the other from its side across the foundry.
        _atk("marche", "Marche des presses", "press", 2, 1600, 0.7, 0.3, 0.5, 7.5, *HORN, arms=3, stagger=0.75,
             burstInterval=0.9, duration=0.4, knockback=320),
        # Geysers of steam burst out of the floor in a line through her.
        _atk("geysers", "Geysers de vapeur", "eruption", 2, 1200, 0.8, 0.3, 0.6, 4.4, *PUNCH, projectile="ember",
             projectileSpeed=420, count=6, spread=56, gravity=700, stagger=0.12, lead=0.8, projectileRadius=6,
             warningEffect="eruption_warning_ice", eruptionEffect="steam_burst", hitEffect="projectile_burst"),
        # 3 Surtension
        # Three reticles lock onto her one after another; a missile falls on each.
        _atk("missiles", "Missiles à verrouillage", "missile", 2, 1600, 0.7, 0.3, 0.5, 6.0, *POD, count=3, **missile),
        _atk("onde", "Onde électrique", "ring", 2, 1000, 0.75, 0.2, 0.6, 5.0, *CANNON, projectileSpeed=190, spread=54,
             **ring),
        _atk("arcs", "Arcs électriques", "prism", 2, 1400, 1.0, 1.0, 0.6, 4.6, *CANNON, bounces=3, laserLength=1600,
             arms=2, origin=MUZZLE, color="#78d6ff", beamTexture="plasma", beamWidth=12, knockback=220),
        # 4 Laser
        _atk("laser", "Laser du canon", "laser", 2, 900, 0.85, 1.6, 0.6, 4.6, *CANNON, laserLength=950, sweepDegrees=110,
             origin=MUZZLE, color="#ff6c6c", beamTexture="flame", beamWidth=20),
        # Every press but one: the column spared is lit green, never far from her.
        _atk("sauf_une", "Toutes les presses sauf une", "press", 2, 1600, 0.75, 0.3, 0.5, 8.0, *HORN, arms=2,
             burstInterval=1.3, duration=0.6, knockback=320),
        _atk("missiles_4", "Salve de missiles", "missile", 2, 1600, 0.7, 0.3, 0.5, 6.5, *POD, count=4, **missile),
        # 5 Surchauffe
        # Its belts carry her away from it, toward the magma of the broken crust.
        _atk("reflux", "Tapis en reflux", "belt", 0, 1600, 0.6, 0.3, 0.4, 9.0, *HORN, pull=95, arms=1, burstInterval=0.9,
             duration=4.5),
        # 6 Plasma
        _atk("drones", "Drones de combat", "summon", 0, 1500, 0.9, 0.4, 0.8, 12.0, *POD, summonId="drone_titan",
             summonCount=2, maxSummoned=4),
        _atk("couronne", "Couronne de plasma", "orbit", 2, 400, 0.75, 0.2, 0.5, 5.0, *CANNON, projectile="energy_disc",
             projectileRadius=8, count=8, radius=180, spread=95, duration=3.0, knockback=220),
        _atk("missiles_5", "Pluie de missiles", "missile", 2, 1600, 0.7, 0.3, 0.5, 6.5, *POD, count=5,
             burstInterval=0.85, stagger=0.28, duration=0.8, radius=34, knockback=260),
        # Its last salvo (never chosen: the last stand, EnemyDefinition::finalAttacks)
        _atk("derniere_onde", "Dernière onde", "ring", 2, 3000, 0.6, 0.2, 0.3, 0.0, *CANNON, projectileSpeed=320,
             spread=60, radius=880, knockback=260),
        _atk("derniere_salve", "Dernière salve", "missile", 2, 3000, 0.6, 0.3, 0.3, 0.0, *POD, count=8, burstInterval=0.8,
             stagger=0.25, duration=0.8, radius=34, knockback=260),
        _atk("dernieres_presses", "Dernières presses", "press", 2, 3000, 0.6, 0.3, 0.3, 0.0, *HORN, arms=2,
             burstInterval=1.3, duration=0.6, knockback=320),
        _atk("dernier_laser", "Dernier laser", "laser", 2, 3000, 0.8, 1.6, 0.3, 0.0, *CANNON, laserLength=1000,
             sweepDegrees=150, origin=MUZZLE, color="#f0fcff", beamTexture="star", beamWidth=24),
    ]


def phase(i, name, attack_ids, speed, cooldown, telegraph, text, combo=3, **kw):
    p = {"name": name, "hpThreshold": round(1.0 - 0.125 * i, 3), "attacks": attack_ids, "speedMultiplier": speed,
         "cooldownMultiplier": cooldown, "combo": combo, "telegraphMultiplier": telegraph, "enterText": text,
         "animation": SHEETS[i], "lightColor": LIGHTS[i], "heat": HEAT[i]}
    p.update(kw)
    return p


def phases():
    return [
        phase(0, "Démarrage", ["poing", "mitraille", "presses", "obus"], 1.0, 1.0, 0.85,
              "Le Titan Mécanique se met en marche."),
        phase(1, "Vapeur", ["tapis", "marche", "geysers", "poing", "mitraille", "presses"], 1.05, 0.95, 0.82,
              "La vapeur monte : la fonderie tourne à plein !"),
        phase(2, "Surtension", ["missiles", "onde", "arcs", "tapis", "presses", "poing"], 1.1, 0.92, 0.8,
              "Surtension ! Ses missiles vous verrouillent !"),
        # « Laser »: at its end the machine stalls (« Calage »): its three valves to turn before it
        # restarts, then « Surchauffe » begins.
        phase(3, "Laser", ["laser", "sauf_une", "missiles_4", "tapis", "poing", "obus"], 1.12, 0.9, 0.78,
              "Son canon chauffe au rouge : gare au laser !", stall="vanne"),
        phase(4, "Surchauffe", ["geysers", "marche", "missiles_4", "reflux", "laser", "obus"], 1.15, 0.88, 0.75,
              "Sa cuirasse saute : le Titan surchauffe !", enterSound="sounds/boss_roar_fire.wav"),
        phase(5, "Plasma", ["drones", "couronne", "missiles_5", "presses", "mitraille", "onde"], 1.2, 0.85, 0.72,
              "Son cœur de plasma s'emballe !"),
        phase(6, "Arc", ["arcs", "onde", "missiles_5", "sauf_une", "tapis", "poing", "drones"], 1.25, 0.82, 0.7,
              "Des arcs électriques courent sur sa carcasse !", enterSound="sounds/boss_roar.wav"),
        phase(7, "Cœur nucléaire", ["poing", "mitraille", "laser", "missiles_5", "sauf_une", "marche", "reflux", "arcs",
                                     "couronne", "geysers", "obus", "drones"], 1.3, 0.75, 0.62,
              "Cœur nucléaire ! Le Titan donne tout !", enrageTime=40, enterSound="sounds/supernova.wav"),
    ]


def titan():
    return {
        "format": "cosmic.enemy", "id": "titan", "name": "Titan Mécanique",
        "description": ("Une machine de guerre restée en marche au fond d'une fonderie engloutie des Confins. Huit "
                        "phases, huit lumières : ses presses, ses tapis, ses missiles, sa surchauffe et son calage, "
                        "et sa dernière salve à survivre."),
        "family": "miniboss", "animation": SHEETS[0], "size": [95, 185], "scale": 1.25, "hp": 800, "contactDamage": 2,
        "speed": 55, "chaseSpeedMultiplier": 1.35, "flying": False, "knockbackResist": 1, "detectionRange": 1000,
        "attackRange": 900, "loseRange": 4000, "needsLineOfSight": False, "movement": "guard",
        "reaction": "chase", "fleeHealthRatio": 0, "preferredDistance": 190, "hopVelocity": 0,
        "attacks": attacks(),
        "powers": {"teleport": {"enabled": False}, "defend": {"enabled": False}, "reinforcements": {"enabled": False}},
        "damageMultipliers": {"charge_shot": 1.2, "ground_pound": 1.25, "dash": 0.5},
        "rewards": {"score": 26000, "drops": [{"type": "health_orb", "chance": 1.0}, {"type": "energy_orb", "chance": 1.0}]},
        "light": {"color": LIGHTS[0], "radius": 230},
        "sounds": {"hurt": "sounds/shield_block.wav", "death": "sounds/supernova.wav", "alert": "sounds/mech_alarm.wav"},
        "boss": {"title": "LE TITAN MÉCANIQUE", "phases": phases(), "barIcon": "portrait", "barColor": "#ffc23c",
                 "finalAttacks": ["derniere_onde", "derniere_salve", "dernieres_presses", "dernier_laser", "derniere_onde"],
                 "finalText": "Sa dernière salve : survivez !"},
    }


def drone_titan():
    """Its escort (phases 6 to 8): a combat drone launched from its back, which shoots bolts."""
    with open(os.path.join(DATA, "forge_drone.json"), encoding="utf-8") as f:
        d = copy.deepcopy(json.load(f))
    d["id"] = "drone_titan"
    d["name"] = "Drone du Titan"
    d["description"] = "Un drone de combat lancé du dos du Titan Mécanique : il garde ses distances et tire des éclairs."
    d["hp"] = 6
    d["tint"] = "#ffe2b8"
    d["light"] = {"color": "#ffc23c", "radius": 56}
    d["detectionRange"] = 900
    d["loseRange"] = 2000
    d["needsLineOfSight"] = False
    for a in d["attacks"]:
        a["name"] = "Éclair de drone"
        a["projectile"] = "energy_disc"
        a["projectileRadius"] = 4
        a["hitEffect"] = "projectile_burst"
    d["powers"]["reinforcements"] = {"enabled": False}
    d["rewards"] = {"score": 360, "drops": [{"type": "energy_orb", "chance": 0.5}]}
    return d


def vanne():
    """A pressure valve of its foundry: when the Titan stalls (the end of its 4th phase), one appears
    on each valve spot; turned (broken) all of them before it restarts, its boiler bursts. It does
    not attack."""
    return {
        "format": "cosmic.enemy", "id": "vanne", "name": "Vanne de pression",
        "description": ("Une vanne de la fonderie du Titan, qui apparaît quand il cale. Tournez-les toutes (en les "
                        "frappant) avant qu'il redémarre : sa chaudière explose."),
        "family": "turret", "animation": "animations/vanne.json", "size": [30, 30], "scale": 1.0, "hp": 12,
        "contactDamage": 0, "speed": 0, "chaseSpeedMultiplier": 1.0, "flying": True, "knockbackResist": 1,
        "detectionRange": 2000, "attackRange": 0, "loseRange": 4000, "needsLineOfSight": False,
        "movement": "stationary", "reaction": "none", "fleeHealthRatio": 0, "preferredDistance": 0, "hopVelocity": 0,
        "attacks": [],
        "powers": {"teleport": {"enabled": False}, "defend": {"enabled": False}, "reinforcements": {"enabled": False}},
        "damageMultipliers": {},
        "rewards": {"score": 800, "drops": []},
        "light": {"color": "#ff8a3c", "radius": 70},
        "sounds": {"hurt": "sounds/lever.wav", "death": "sounds/lava_rise.wav", "alert": ""},
    }


def main():
    for d in (titan(), drone_titan(), vanne()):
        with open(os.path.join(DATA, d["id"] + ".json"), "w", encoding="utf-8") as f:
            json.dump(d, f, ensure_ascii=False, indent=2)
            f.write("\n")
        print(d["id"], "PV", d["hp"], "attaques", len(d["attacks"]))


if __name__ == "__main__":
    main()
