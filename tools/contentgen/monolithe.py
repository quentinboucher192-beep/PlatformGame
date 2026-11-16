#!/usr/bin/env python3
"""Monolithe, le Golem Primordial (version 2.49, its brain and its attacks reworked in 2.51): the
second giga boss of Les Confins, at the end of « Le Cœur de Pierre » (confins_02, tools/levelgen/confins.py).

A golem of rune-carved granite older than the planets, walking on the ground (the other giga
bosses fly). Its health is the heaviest (840, 105 per phase), contact 2, and it fights with its
fists, the ground and the glare of its single eye. Eight phases, one every 12.5 % of its health,
each with its own light (the sheets of tools/assetgen/characters/boss_f_monolithe.py: one per phase,
phases 5-8 « Fendu », its chest plates gone over a crystal core):

2.51, its own attacks (game/enemies/GolemAttacks.cpp): « Carapace roulante » (it curls into its
rune shell and rolls at Nova, bouncing off the walls; armoured curled, dizzy after), « Tremblement de
terre » (cracks burst under Nova, rocks fall from the roof on marked spots) and « Bond titanesque » (it
leaps out of sight, its shadow hunts Nova, it falls on her: shockwaves along the floor). Its brain
of a walker keeps it on the firm floor between the pits, at its fighting distance (never in a corner).

  1 Ambre        its shell and its titanic leap from the start, double fist slams, rocks in arcs, the glare
  2 Éboulement   the quake, rocks rain from the roof, spikes of stone run under Nova, its shell
  3 Séisme       the quake, its titanic leap, rings of shock, the floor freezes into glass, charges
  4 Runes        runes turn around it, a spiral of rune discs, a reflected glare, its shell, its leap
  5 Fendu        its plates burst: magma rises, geysers, the quake, a wilder shell (two bounces)
  6 Or           the gravity of its core, rings, cages of rune stones, a double titanic leap
  7 Azur         rock creatures of its body escort it, the wild shell, the quake, the glare
  8 Incandescent everything, faster; rage after 40 s in the phase, and its last tremor to
                 survive when its health runs out (a ring, a last quake, the vault falling).

Writes data/enemies/monolithe.json and data/enemies/eclat_de_roche.json (its escort).
Run from the repository root:  python3 tools/contentgen/monolithe.py
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

# Clips of its sheets (boss_f_monolithe.py): telegraph/attack (both fists raised, then the slam,
# `impact` on frame 2), telegraph_special/special (runes flare, then the eye glares), summon (fists
# into the ground, rune rocks rise), roar (phase change); 2.51: curl/roll (the shell; uncurl and
# dizzy after it), telegraph_special/quake (fists pounding the ground), crouch/leap (fall and land
# after it).
CLASSIC = ("telegraph", "attack")
SPECIAL = ("telegraph_special", "special")
RAISE = ("telegraph_special", "summon")
SHELL = ("curl", "roll")
QUAKE = ("telegraph_special", "quake")
LEAP = ("crouch", "leap")
FIST = [65, -20]  # the near fist on the impact frame, from the feet (sprite pixels)
EYE = [63, -78]   # the eye during the glare (the same on the four frames)

# The light of its runes per phase (boss_f_monolithe: ambre, émeraude, cyan, améthyste, magma, or,
# azur, blanc incandescent).
LIGHTS = ["#ffb347", "#4ae08a", "#5ae0f0", "#b583ff", "#ff6a3a", "#ffd84a", "#6aa8ff", "#fff4e0"]
SHEETS = ["animations/monolithe.json"] + ["animations/monolithe_p%d.json" % k for k in range(2, 9)]


def attacks():
    rock = dict(projectile="rock", hitEffect="projectile_burst")
    disc = dict(projectile="energy_disc", hitEffect="projectile_burst")
    ring = dict(radius=900, knockback=260)
    # 2.51: its own attacks (game/enemies/GolemAttacks.cpp).
    shell = dict(bounces=1, duration=1.6, vulnerability=1.5, armor=0.35, knockback=320)
    quake = dict(projectile="rock", count=3, spread=150, radius=22, height=46, stagger=0.75, arms=2, projectileRadius=13,
                 gravity=900, projectileSpeed=0, knockback=240)
    leap = dict(duration=1.1, radius=26, projectileSpeed=300, projectileRadius=10, burstInterval=0.4, knockback=300)
    return [
        # Its own (2.51)
        _atk("carapace", "Carapace roulante", "shell", 2, 1600, 0.8, 0.3, 0.5, 6.0, *SHELL, lungeSpeed=360, **shell),
        _atk("carapace_folle", "Carapace folle", "shell", 2, 1600, 0.75, 0.3, 0.5, 6.0, *SHELL, lungeSpeed=430,
             **dict(shell, bounces=2, duration=1.3)),
        _atk("seisme", "Tremblement de terre", "quake", 2, 1600, 0.8, 0.2, 0.6, 8.0, *QUAKE, duration=3.4, burstInterval=0.85,
             **quake),
        _atk("bond_titan", "Bond titanesque", "skyfall", 2, 1800, 0.6, 0.6, 0.6, 6.5, *LEAP, count=1, **leap),
        _atk("bond_titan_double", "Double bond titanesque", "skyfall", 2, 1800, 0.55, 0.7, 0.6, 6.5, *LEAP, count=2,
             **dict(leap, duration=0.95)),
        # 1 Ambre
        _atk("poings", "Double frappe", "slam", 2, 200, 0.7, 0.35, 0.7, 2.0, *CLASSIC, projectileSpeed=230, knockback=280),
        _atk("charge", "Charge du colosse", "charge", 2, 700, 0.65, 2.2, 0.7, 3.4, *CLASSIC, minRange=90, lungeSpeed=300,
             knockback=320),
        _atk("regard", "Regard primordial", "laser", 2, 900, 0.85, 1.6, 0.6, 4.6, *SPECIAL, laserLength=900,
             sweepDegrees=90, origin=EYE, color="#ffd2a0", beamTexture="plasma", beamWidth=20),
        # 2 Éboulement
        _atk("eboulement", "Éboulement", "rain", 1, 1200, 0.75, 0.3, 0.5, 3.4, *RAISE, projectileSpeed=220, count=10,
             spread=42, gravity=300, rainHeight=360, explosionRadius=18, lead=0.8, **rock),
        _atk("pics", "Pics de pierre", "tracker", 2, 1000, 0.7, 0.2, 0.5, 4.0, *CLASSIC, projectile="rock",
             projectileSpeed=140, duration=2.8, radius=18, height=72, hitEffect="projectile_burst", knockback=260),
        _atk("jet", "Jet de rochers", "arc", 1, 900, 0.6, 0.25, 0.5, 2.6, *CLASSIC, count=3, spread=24, gravity=380,
             projectileSpeed=210, lead=0.7, origin=FIST, **rock),
        # 3 Séisme
        _atk("onde", "Onde tellurique", "ring", 2, 1000, 0.75, 0.2, 0.6, 5.0, *SPECIAL, projectileSpeed=180, spread=54,
             **ring),
        _atk("vitrifie", "Sol vitrifié", "frost_floor", 0, 1200, 0.8, 0.3, 0.6, 12.0, *RAISE, duration=6.0),
        # 4 Runes
        _atk("orbite", "Cercle de runes", "orbit", 2, 400, 0.75, 0.2, 0.5, 5.0, *SPECIAL, projectile="energy_disc",
             projectileRadius=8, count=8, radius=180, spread=95, duration=3.0, knockback=220),
        _atk("spirale", "Spirale runique", "spiral", 1, 1000, 0.7, 0.2, 0.5, 4.5, *SPECIAL, projectileSpeed=130, arms=3,
             spread=14, burstInterval=0.12, duration=3.5, **disc),
        _atk("reflet", "Regard réfléchi", "prism", 2, 1400, 1.0, 1.0, 0.6, 4.4, *SPECIAL, bounces=3, laserLength=1600,
             arms=2, origin=EYE, color="#d8b8ff", beamTexture="prism", beamWidth=12, knockback=220),
        # 5 Fendu
        _atk("magma", "Magma des fissures", "flood", 2, 2000, 1.2, 0.5, 0.6, 11.0, *RAISE, duration=4.5, height=200),
        _atk("geysers", "Geysers de roche", "eruption", 2, 1200, 0.8, 0.3, 0.6, 4.0, *CLASSIC, projectile="rock",
             projectileSpeed=460, count=6, spread=50, gravity=700, stagger=0.12, lead=0.8,
             warningEffect="eruption_warning_fire", eruptionEffect="eruption_fire", hitEffect="projectile_burst"),
        # 6 Or
        _atk("noyau", "Gravité du noyau", "well", 2, 900, 0.8, 0.2, 0.6, 8.0, *SPECIAL, pull=95, radius=26, duration=4.5,
             knockback=240),
        _atk("anneaux", "Anneaux d'or", "ring", 2, 1000, 0.7, 0.2, 0.5, 4.5, *SPECIAL, projectileSpeed=210, spread=50,
             **ring),
        _atk("cage", "Cage de menhirs", "cage", 1, 1000, 0.9, 0.2, 0.7, 8.0, *RAISE, projectile="rock",
             projectileSpeed=110, burstInterval=0.35, duration=4.5, radius=110, height=160, knockback=180),
        # 7 Azur
        _atk("eclats", "Éclats de son corps", "summon", 0, 1500, 0.9, 0.4, 0.8, 12.0, "summon", "summon",
             summonId="eclat_de_roche", summonCount=2, maxSummoned=4),
        # 8 Incandescent: its last tremor (never chosen: the last stand, EnemyDefinition::finalAttacks)
        _atk("dernier_seisme", "Dernier séisme", "ring", 2, 3000, 0.6, 0.2, 0.3, 0.0, *SPECIAL, projectileSpeed=320,
             spread=60, radius=880, knockback=260),
        _atk("derniere_chute", "Chute de la voûte", "rain", 1, 3000, 0.6, 0.3, 0.3, 0.0, *RAISE, projectileSpeed=260,
             count=14, spread=48, gravity=300, rainHeight=360, lead=0.8, **rock),
        _atk("derniere_secousse", "Dernière secousse", "quake", 2, 3000, 0.6, 0.2, 0.3, 0.0, *QUAKE, duration=4.0,
             burstInterval=0.7, **dict(quake, count=5, arms=3)),
    ]


def phase(i, name, attack_ids, speed, cooldown, telegraph, text, combo=3, **kw):
    p = {"name": name, "hpThreshold": round(1.0 - 0.125 * i, 3), "attacks": attack_ids, "speedMultiplier": speed,
         "cooldownMultiplier": cooldown, "combo": combo, "telegraphMultiplier": telegraph, "enterText": text,
         "animation": SHEETS[i], "lightColor": LIGHTS[i]}
    p.update(kw)
    return p


def phases():
    return [
        phase(0, "Ambre", ["carapace", "bond_titan", "poings", "jet", "regard"], 1.0, 0.9, 0.85, "Le Monolithe ouvre son œil."),
        phase(1, "Éboulement", ["seisme", "poings", "eboulement", "pics", "jet", "carapace"], 1.05, 0.95, 0.82,
              "La voûte s'effondre sur vous !"),
        phase(2, "Séisme", ["seisme", "bond_titan", "onde", "vitrifie", "poings", "charge"], 1.1, 0.92, 0.8,
              "Chacun de ses pas fait trembler les Confins !"),
        phase(3, "Runes", ["orbite", "spirale", "reflet", "carapace", "bond_titan", "jet"], 1.12, 0.9, 0.78,
              "Ses runes s'embrasent !"),
        phase(4, "Fendu", ["magma", "geysers", "seisme", "carapace_folle", "poings"], 1.15, 0.88, 0.75,
              "Sa cuirasse éclate : le magma monte des fissures !", enterSound="sounds/shell_shatter.wav"),
        phase(5, "Or", ["noyau", "anneaux", "cage", "spirale", "bond_titan_double"], 1.2, 0.85, 0.72,
              "Son cœur de cristal vous attire !"),
        phase(6, "Azur", ["eclats", "carapace_folle", "seisme", "charge", "regard"], 1.25, 0.82, 0.7,
              "Des éclats de son corps prennent vie !", summonOnEnter=True),
        phase(7, "Incandescent", ["carapace_folle", "bond_titan_double", "seisme", "poings", "regard", "eboulement", "pics",
                                  "onde", "magma", "geysers", "spirale", "noyau", "reflet"], 1.3, 0.75, 0.62,
              "Incandescent ! Le Monolithe se consume !", enrageTime=40, enterSound="sounds/supernova.wav"),
    ]


def monolithe():
    return {
        "format": "cosmic.enemy", "id": "monolithe", "name": "Monolithe",
        "description": ("Un golem de granit gravé de runes, plus ancien que les planètes, qui garde le cœur des Confins. "
                        "Huit phases, huit lumières : sa cuirasse éclate, le magma monte, et son dernier séisme doit "
                        "être survécu."),
        "family": "miniboss", "animation": SHEETS[0], "size": [100, 160], "scale": 1.25, "hp": 840, "contactDamage": 2,
        "speed": 60, "chaseSpeedMultiplier": 1.35, "flying": False, "knockbackResist": 1, "detectionRange": 1000,
        "attackRange": 900, "loseRange": 4000, "needsLineOfSight": False, "movement": "guard",
        "reaction": "chase", "fleeHealthRatio": 0, "preferredDistance": 170, "hopVelocity": 0,
        "attacks": attacks(),
        "powers": {"teleport": {"enabled": False}, "defend": {"enabled": False}, "reinforcements": {"enabled": False}},
        "damageMultipliers": {"charge_shot": 1.2, "ground_pound": 1.3, "dash": 0.5},
        "rewards": {"score": 24000, "drops": [{"type": "health_orb", "chance": 1.0}, {"type": "energy_orb", "chance": 1.0}]},
        "light": {"color": LIGHTS[0], "radius": 220},
        "sounds": {"hurt": "sounds/enemy_hurt.wav", "death": "sounds/supernova.wav", "alert": "sounds/boss_roar.wav"},
        "boss": {"title": "MONOLITHE, LE GOLEM PRIMORDIAL", "phases": phases(), "barIcon": "skull", "barColor": "#ffd9a8",
                 "finalAttacks": ["dernier_seisme", "derniere_secousse", "derniere_chute"],
                 "finalText": "Son dernier séisme : survivez !"},
    }


def eclat_de_roche():
    """Its escort (phase 7): a small golem broken off its body (one slam, no calls)."""
    with open(os.path.join(DATA, "golem.json"), encoding="utf-8") as f:
        d = copy.deepcopy(json.load(f))
    d["id"] = "eclat_de_roche"
    d["name"] = "Éclat de roche"
    d["description"] = "Un bloc détaché du corps du Monolithe, animé par ses runes. Il frappe le sol en s'approchant."
    d["hp"] = 10
    d["tint"] = "#bcd6ff"
    d["light"] = {"color": "#6aa8ff", "radius": 46}
    d["detectionRange"] = 900
    d["loseRange"] = 2000
    d["needsLineOfSight"] = False
    d["rewards"] = {"score": 340, "drops": [{"type": "energy_orb", "chance": 0.5}]}
    return d


def main():
    for d in (monolithe(), eclat_de_roche()):
        with open(os.path.join(DATA, d["id"] + ".json"), "w", encoding="utf-8") as f:
            json.dump(d, f, ensure_ascii=False, indent=2)
            f.write("\n")
        print(d["id"], "PV", d["hp"], "attaques", len(d["attacks"]))


if __name__ == "__main__":
    main()
