#!/usr/bin/env python3
"""The four guardians of Aethra (version 2.19), one per level of the planet of the
sanctuaries, each with three attacks of its own (game/enemies/GuardianAttacks.cpp) and two
classic ones:

  mycelia   Mycélia, la Mère-Racine          (aethra_01)  racine traqueuse, cage de ronces, inspiration
  aurorax   Aurorax, le Miroir boréal        (aethra_02)  reflets, lac gelé, prisme
  pyrogyre  Pyrogyre, l'Anneau solaire       (aethra_03)  couronne de feu, spirale solaire, marée de lave
  nyxara    Nyxara, la Dévoreuse d'étoiles   (aethra_04)  puits gravitationnel, pas de l'ombre, onde d'éclipse

Strength of a guardian at the end of a planet (the trials' guardians: 90 to 120 health,
contact 2, rage after a minute or so), Nyxara a notch above. Sprites, effects, sounds and
music are made by tools/assetgen (assets/sprites/enemies/<id>.png, animations/<id>.json,
music/<id>_boss.ogg); clips: idle, walk, telegraph, attack, telegraph_special, special,
summon, hurt, death.

Writes data/enemies/<id>.json (only these four: the other bosses are not rewritten).
Run from the repository root:  python3 tools/contentgen/guardians.py
then python3 tools/levelgen/stellar_bosses.py (their echoes).
"""
import json
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bosses import _atk, _boss  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA = os.path.join(ROOT, "data", "enemies")

# Clips of the sheets (tools/assetgen): idle, walk, move, telegraph, attack, telegraph_special,
# special (a loop: Mycélia's breath, Aurorax's charge, Pyrogyre's ray, Nyxara's pulse),
# summon, roar, hurt, death; Mycélia and Nyxara also teleport_out / teleport_in.
SPECIAL = ("telegraph_special", "special")
CLASSIC = ("telegraph", "attack")
RAISE = ("telegraph_special", "summon")  # walls, doubles, tide: the guardian raises its arms


def _phase(name, threshold, attacks, speed, cooldown, combo, telegraph, text="", summon=False):
    p = {"name": name, "hpThreshold": threshold, "attacks": attacks, "speedMultiplier": speed,
         "cooldownMultiplier": cooldown, "combo": combo, "telegraphMultiplier": telegraph}
    if text:
        p["enterText"] = text
    if summon:
        p["summonOnEnter"] = True
    return p


def _guardian(d, enrage, **boss):
    d["contactDamage"] = 2
    d["boss"]["enrageTime"] = enrage
    d["boss"].update(boss)
    return d


def mycelia():
    attacks = [
        _atk("tracker", "Racine traqueuse", "tracker", 2, 620, 0.8, 0.2, 0.5, 3.0, *CLASSIC,
             projectile="root_spike", projectileSpeed=105, duration=2.5, radius=16, height=44, hitEffect="eruption_spore",
             knockback=240),
        _atk("cage", "Cage de ronces", "cage", 1, 540, 0.9, 0.2, 0.8, 7.5, *RAISE,
             projectile="pollen", projectileSpeed=90, burstInterval=0.4, duration=4.0, radius=130, height=136,
             knockback=180),
        _atk("inhale", "Inspiration", "vortex", 2, 480, 0.9, 0.2, 0.9, 7.0, *SPECIAL,
             pull=80, duration=2.0, knockback=280),
        _atk("spore_rain", "Pluie de spores", "rain", 1, 540, 0.8, 0.3, 0.6, 3.6, *CLASSIC,
             projectile="pollen", projectileSpeed=150, count=6, spread=44, gravity=180, rainHeight=240,
             hitEffect="acid_burst", lead=0.8),
        # Like the v3 attacks of the other guardians: what reaches a player perched out of reach.
        _atk("spore_volley", "Salve de spores", "arc", 1, 520, 0.6, 0.25, 0.5, 2.6, *CLASSIC,
             projectile="pollen", projectileSpeed=160, count=3, spread=20, gravity=420, projectileRadius=6,
             hitEffect="acid_burst", lead=0.8),
        _atk("buds", "Bourgeons", "summon", 0, 600, 0.9, 0.4, 0.8, 11, "summon", "summon",
             summonId="gloop", summonCount=2, maxSummoned=3),
    ]
    phases = [
        _phase("Éveil", 1.0, ["tracker", "spore_rain", "spore_volley"], 1.0, 1.0, 1, 1.0),
        _phase("Forêt vivante", 0.66, ["tracker", "spore_rain", "spore_volley", "cage", "buds"], 1.1, 0.9, 1, 0.95,
               "Mycélia réveille la forêt !", True),
        _phase("Mère de tout", 0.33, ["tracker", "spore_rain", "spore_volley", "cage", "buds", "inhale"], 1.2, 0.8, 2, 0.9,
               "Mycélia aspire l'air du bosquet !"),
    ]
    d = _boss("mycelia", "Mycélia", "MYCÉLIA, LA MÈRE-RACINE",
              "Le champignon-mère qui relie toutes les racines de Sylvanox. Elle s'enfonce sous terre et ressurgit là où on ne l'attend pas.",
              [60, 64], 95, attacks, phases, speed=40, light="#b48cff",
              mult={"charge_shot": 1.2, "wave": 1.2}, score=5000)
    # She sinks into the ground and comes out elsewhere (teleport_out / teleport_in clips).
    d["powers"]["teleport"] = {"enabled": True, "cooldown": 7.0, "range": 170}
    return _guardian(d, 75)


def aurorax():
    prism = dict(bounces=2, laserLength=1100, color="#9fe8ff", beamTexture="prism", beamWidth=12, knockback=220)
    attacks = [
        _atk("prism", "Prisme", "prism", 2, 660, 1.0, 0.9, 0.6, 3.6, *CLASSIC, **prism),
        _atk("prism_night", "Prisme de la nuit polaire", "prism", 2, 660, 0.95, 0.9, 0.7, 4.0, *CLASSIC,
             **dict(prism, bounces=3)),
        _atk("charge", "Charge de cristal", "charge", 2, 440, 0.6, 2.0, 0.7, 3.4, *SPECIAL,
             minRange=70, lungeSpeed=280, knockback=270),
        _atk("shards", "Éclats", "spread", 1, 480, 0.6, 0.25, 0.45, 2.2, *CLASSIC,
             projectile="aurora_shard", projectileSpeed=210, count=5, spread=11, slow=0.5, slowDuration=1.4,
             hitEffect="ice_burst", lead=0.7),
        _atk("mirror", "Reflets", "mirror", 0, 800, 0.9, 0.4, 0.6, 9.0, *RAISE, duration=16),
        _atk("mirror_night", "Reflets éternels", "mirror", 0, 800, 0.8, 0.4, 0.6, 4.0, *RAISE, weight=1.5),
        _atk("frozen_lake", "Lac gelé", "frost_floor", 0, 800, 1.0, 0.4, 0.6, 12.0, "telegraph", "summon", duration=5.0,
             grip=0.15),
    ]
    phases = [
        _phase("Aurore", 1.0, ["prism", "charge", "shards"], 1.0, 1.0, 1, 1.0),
        _phase("Reflets", 0.66, ["prism", "charge", "shards", "mirror", "frozen_lake"], 1.1, 0.9, 2, 0.95,
               "Aurorax se dédouble sur le lac !"),
        _phase("Nuit polaire", 0.33, ["prism_night", "charge", "shards", "mirror_night", "frozen_lake"], 1.2, 0.8, 2, 0.9,
               "La nuit polaire tombe sur le lac !"),
    ]
    d = _boss("aurorax", "Aurorax", "AURORAX, LE MIROIR BORÉAL",
              "Esprit de l'aurore figé dans un corps de cristal. Sur le lac gelé, ses reflets se battent avec lui.",
              [42, 80], 110, attacks, phases, speed=40, light="#9fe8ff", roar="sounds/boss_roar_ice.wav",
              mult={"dash": 0.6, "ground_pound": 1.3}, score=5500)
    return _guardian(d, 75)


def pyrogyre():
    attacks = [
        _atk("crown", "Couronne de feu", "orbit", 2, 500, 0.8, 0.2, 0.45, 4.0, *CLASSIC,
             projectile="solar_flame", projectileRadius=8, count=6, radius=150, spread=110, duration=2.6, knockback=220),
        _atk("spiral", "Spirale solaire", "spiral", 1, 640, 0.7, 0.2, 0.4, 4.0, *CLASSIC,
             projectile="solar_bolt", projectileSpeed=125, arms=2, spread=13, burstInterval=0.14, duration=3.0,
             hitEffect="fire_burst"),
        _atk("ray", "Rayon solaire", "laser", 2, 500, 1.0, 1.4, 0.5, 3.6, *SPECIAL,
             laserLength=500, sweepDegrees=90, color="#ffb13b", beamTexture="flame", beamWidth=16),
        _atk("tide", "Marée de lave", "flood", 2, 1000, 1.4, 0.5, 0.6, 12.0, *RAISE, duration=4.0, height=64),
        _atk("high_tide", "Grande marée", "flood", 2, 1000, 1.4, 0.5, 0.6, 11.0, *RAISE, duration=4.0, height=120),
        _atk("eruptions", "Éruptions", "eruption", 2, 560, 0.9, 0.3, 0.7, 4.2, *CLASSIC,
             projectile="fireball", projectileSpeed=420, count=5, spread=60, gravity=700,
             warningEffect="eruption_warning_fire", eruptionEffect="eruption_fire", hitEffect="fire_burst", lead=0.7),
        # Like the v3 attacks of the other guardians: lobbed suns reach a player out of reach.
        _atk("flare_line", "Gerbe solaire", "arc", 1, 640, 0.6, 0.25, 0.4, 2.6, *CLASSIC,
             projectile="solar_bolt", projectileSpeed=180, count=4, spread=18, gravity=420, projectileRadius=6,
             hitEffect="fire_burst", lead=0.8, weight=1.4),
        _atk("supernova", "Supernova", "orbit", 2, 480, 0.9, 0.2, 0.8, 6.0, *CLASSIC,
             projectile="solar_flame", projectileRadius=8, projectileSpeed=120, count=6, radius=160, spread=95,
             duration=4.5, arms=2, burstInterval=0.24, knockback=220),
    ]
    phases = [
        _phase("Aube", 1.0, ["crown", "spiral", "ray", "flare_line"], 1.0, 1.0, 1, 1.0),
        _phase("Zénith", 0.66, ["crown", "spiral", "ray", "flare_line", "tide", "eruptions"], 1.1, 0.9, 2, 0.95,
               "La caldeira se remplit de lave !"),
        _phase("Supernova", 0.33, ["supernova", "spiral", "ray", "flare_line", "high_tide", "eruptions"], 1.2, 0.8, 2, 0.9,
               "Pyrogyre devient une étoile naissante !"),
    ]
    d = _boss("pyrogyre", "Pyrogyre", "PYROGYRE, L'ANNEAU SOLAIRE",
              "Un anneau de magma en orbite autour d'un œil solaire. Il fait tourner le feu autour de lui comme une étoile qui naît.",
              [52, 52], 115, attacks, phases, flying=True, movement="fly", reaction="keepDistance", speed=56,
              preferred=170, light="#ffb13b", roar="sounds/boss_roar_fire.wav",
              mult={"wave": 1.2, "charge_shot": 1.3}, score=5500)
    return _guardian(d, 75)


def nyxara():
    attacks = [
        _atk("shadow_step", "Pas de l'ombre", "blink", 2, 660, 0.6, 0.35, 0.6, 3.2, "teleport_out", "attack", radius=46,
             knockback=260),
        _atk("seekers", "Étoiles chercheuses", "projectile", 1, 620, 0.6, 0.25, 0.5, 3.2, *CLASSIC,
             projectile="void_star", projectileSpeed=140, count=3, spread=30, homing=2.2, homingTime=1.6,
             hitEffect="projectile_burst"),
        _atk("star_fall", "Chute d'étoiles", "rain", 1, 620, 0.7, 0.3, 0.5, 3.4, *CLASSIC,
             projectile="void_star", projectileSpeed=170, count=6, spread=46, gravity=200, rainHeight=250,
             hitEffect="projectile_burst", lead=0.7),
        _atk("well", "Puits gravitationnel", "well", 2, 540, 0.9, 0.2, 0.7, 9.0, *SPECIAL, pull=85, radius=22,
             duration=4.0, knockback=240),
        _atk("eclipse", "Onde d'éclipse", "ring", 2, 660, 0.8, 0.2, 0.6, 5.5, *SPECIAL, projectileSpeed=165,
             radius=600, spread=52, knockback=240),
        _atk("void_call", "Appel du vide", "summon", 0, 640, 0.9, 0.4, 0.8, 12, "summon", "summon",
             summonId="nightwing", summonCount=2, maxSummoned=3),
    ]
    phases = [
        _phase("Crépuscule", 1.0, ["shadow_step", "seekers", "star_fall"], 1.0, 1.0, 1, 1.0),
        _phase("Horizon", 0.66, ["shadow_step", "seekers", "star_fall", "well", "eclipse", "void_call"], 1.1, 0.9, 2, 0.95,
               "Nyxara ouvre l'horizon des événements !", True),
        _phase("Éclipse", 0.33, ["shadow_step", "seekers", "star_fall", "well", "eclipse", "void_call"], 1.2, 0.8, 3, 0.85,
               "Nyxara avale la lumière !"),
    ]
    d = _boss("nyxara", "Nyxara", "NYXARA, LA DÉVOREUSE D'ÉTOILES",
              "Née du vide entre les étoiles, elle avale la lumière. Plus elle est blessée, plus l'arène s'assombrit.",
              [52, 52], 140, attacks, phases, flying=True, movement="fly", reaction="keepDistance", speed=48,
              preferred=180, light="#ff7696", mult={"charge_shot": 1.2}, score=7000)
    return _guardian(d, 80, darkPhase=3)


def all_guardians():
    return [mycelia(), aurorax(), pyrogyre(), nyxara()]


def main():
    for d in all_guardians():
        with open(os.path.join(DATA, d["id"] + ".json"), "w", encoding="utf-8") as f:
            json.dump(d, f, ensure_ascii=False, indent=2)
            f.write("\n")
        print(d["id"], "PV", d["hp"], "attaques", len(d["attacks"]))


if __name__ == "__main__":
    main()
