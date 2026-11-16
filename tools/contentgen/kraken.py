#!/usr/bin/env python3
"""Le Kraken des Abysses (version 2.52): the third giga boss of Les Confins, at the end of
« La Fosse des Abysses » (confins_03, tools/levelgen/confins.py).

A colossal kraken of the black seas buried under the crust of the Confins. It swims (the engine's
flying movement, no teleport) and fights with its eight arms, its ink and the currents it stirs.
Health 800 (100 per phase), contact 2, combos of three. Eight phases, one every 12.5 % of its
health, each with its own light (the sheets of tools/assetgen/characters/boss_g_kraken.py: one per
phase, the bioluminescence recoloured; phases 5-8 « Blessé », the mantle torn on its glowing core
and one arm severed):

  1 Abysses        a lash that dives through Nova, ink clouds lobbed where she stands, tentacles
                   bursting from the floor on both sides of her (ink dripping between them), the
                   jet of its beak, a sonar ring
  2 Marée          a low tide floods the floor (the lowest ledges stay dry; a tide rises from the
                   bottom of the arena, its trenches 4 tiles below the floor), a whirlpool pulls
                   Nova in, geysers burst from the floor under her, it lunges arms first
  3 Encre          it comes down where she stands and spreads a sheet of ink along the floor,
                   clouds of ink three by three, the current of its jet pushes her away, a spiral
  4 Foudre marine  a lightning jet reflected by the walls, electric jellies that home in, a crown
                   of photophores circling it, sonar rings
  5 Brisé          its mantle tears: the floor gives way over the abyss, the great tide rises,
                   geysers, sheets of ink
  6 Nacre          crossing beams of mother-of-pearl, rings, tentacles, clouds, lashes two by two
  7 Tourbillon     lights torn from its arms escort it, a maelstrom pulls Nova and bends her shots,
                   whirlpools, lashes two by two
  8 Léviathan      everything, faster: lashes three by three, ink on both sides of its landing,
                   the great tide; rage after 40 s in the phase (the arena closes in), and its last
                   wave to survive when its health runs out.

Right under it is no shelter (the dragon's rules of 2.50, game/enemies/DragonAttacks.cpp): it
prefers what comes down on its prey there (its lashes, its landings, its ink).

Looks of its own (appearance only, not in the fingerprints): its tentacles rise out of the floor
(Cage `look` « tentacle »), its tides are dark water (Flood `look` « tide »), its clouds are ink
lit by its phase (CloudShot / Perch `look` « ink »); its arena's trenches and curtains are dark
water too (damage zones `style` « abyss », tools/levelgen/confins.py).

Writes data/enemies/kraken.json and data/enemies/photophore.json (its escort).
Run from the repository root:  python3 tools/contentgen/kraken.py
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

# Clips of its sheets (boss_g_kraken.py): telegraph/attack (the front arms rear up, hooked, then
# the lash: `fire` on frame 1), telegraph_special/special (it inhales, the light gathers in its
# eyes and beak, then the jet), summon (its arms spread: a pulse of light, two whirlpools), move
# (it swims), roar (phase change).
CLASSIC = ("telegraph", "attack")
SPECIAL = ("telegraph_special", "special")
RAISE = ("telegraph_special", "summon")
LASH = ("telegraph", "attack")          # arms reared, then the lash dives through
PERCH = ("telegraph_special", "move")   # it inhales, then swims down onto its mark
TIP = [109, -9]   # the tip of the lashing arm on the attack frame, from the feet (sprite pixels)
BEAK = [48, -11]  # its beak during the jet (the head does not move on the four frames)

# The light of each phase (the P1 tone of each ramp of boss_g_kraken: cyan, turquoise, indigo,
# jaune électrique, rouge corail, rose nacré, émeraude, blanc incandescent).
LIGHTS = ["#7ae6ff", "#74f6d6", "#a69aff", "#fff060", "#ff8a70", "#ffbede", "#6af59a", "#f6f8ff"]
SHEETS = ["animations/kraken.json"] + ["animations/kraken_p%d.json" % k for k in range(2, 9)]


def attacks():
    ink = dict(projectile="wisp_orb", hitEffect="projectile_burst")
    glow = dict(projectile="aurora_shard", hitEffect="projectile_burst")
    ring = dict(radius=900, knockback=240)
    wet = "sounds/slime_split.wav"
    return [
        # 1 Abysses
        # Coup de fouet: the line follows Nova, holds still, then the lash goes through and beyond.
        _atk("fouet", "Fouet des abysses", "dive", 2, 1100, 0.7, 0.2, 0.6, 3.2, *LASH, lungeSpeed=760, count=1,
             radius=240, burstInterval=0.6, lead=0.6, knockback=340, sound="sounds/wyrm_roar.wav"),
        # Ink lobbed onto the marks shown: a cloud of ink stays where it bursts.
        _atk("encre", "Nuage d'encre", "cloud_shot", 1, 1200, 0.7, 0.3, 0.5, 3.6, *CLASSIC, projectileSpeed=300,
             gravity=520, count=1, spread=0, radius=60, duration=3.2, projectileRadius=8, origin=TIP, lead=0.7,
             sound=wet, look="ink", **ink),
        # Two tentacles burst out of the floor on both sides of Nova; ink drips between them.
        _atk("etau", "Étau de tentacules", "cage", 1, 1100, 0.8, 0.2, 0.6, 6.5, *RAISE, projectileSpeed=120,
             burstInterval=0.32, duration=3.2, radius=100, height=150, knockback=200, look="tentacle", **ink),
        _atk("jet", "Jet abyssal", "laser", 2, 900, 0.8, 1.6, 0.6, 4.6, *SPECIAL, laserLength=900, sweepDegrees=100,
             origin=BEAK, color="#9ae8ff", beamTexture="frost", beamWidth=22),
        _atk("sonar", "Onde sonar", "ring", 2, 1000, 0.75, 0.2, 0.6, 5.0, *SPECIAL, projectileSpeed=185, spread=54,
             **ring),
        # 2 Marée
        _atk("maree", "Marée montante", "flood", 2, 2000, 1.1, 0.5, 0.6, 10.0, *RAISE, duration=3.5, height=132,
             look="tide", sound="sounds/lava_rise.wav"),
        _atk("tourbillon", "Tourbillon", "vortex", 2, 900, 0.8, 0.2, 0.7, 7.0, *RAISE, pull=110, duration=2.4,
             knockback=280),
        _atk("geysers", "Geysers des abysses", "eruption", 2, 1200, 0.8, 0.3, 0.6, 4.0, *CLASSIC, projectile="ice_shard",
             projectileSpeed=460, count=6, spread=50, gravity=700, stagger=0.12, lead=0.8,
             warningEffect="eruption_warning_ice", eruptionEffect="splash_water", hitEffect="projectile_burst"),
        _atk("etreinte", "Étreinte", "lunge", 2, 600, 0.6, 0.8, 0.6, 3.0, *CLASSIC, lungeSpeed=400, minRange=60,
             knockback=300, lead=0.8),
        # 3 Encre
        # It comes down where Nova stands and spreads a sheet of ink along the floor; landed, it
        # takes more damage.
        _atk("nappe", "Nappe d'encre", "perch", 1, 1400, 0.85, 1.3, 0.5, 7.5, *PERCH, lungeSpeed=640, radius=260,
             height=44, duration=3.0, arms=1, vulnerability=1.5, lead=0.5, knockback=280, sound=wet, look="ink"),
        _atk("nuees", "Nuées d'encre", "cloud_shot", 1, 1300, 0.7, 0.3, 0.5, 5.5, *CLASSIC, projectileSpeed=320,
             gravity=520, count=3, spread=110, radius=54, duration=3.0, projectileRadius=8, origin=TIP, lead=0.7,
             sound=wet, look="ink", **ink),
        # The current of its jet pushes Nova away (105 px/s, below her run), bubbles of light in it.
        _atk("courant", "Courant des abysses", "gust", 1, 1000, 0.8, 0.2, 0.6, 7.0, *SPECIAL, duration=2.2, pull=105,
             count=2, burstInterval=0.55, projectileSpeed=230, spread=12, knockback=200, sound="sounds/inhale.wav",
             **glow),
        _atk("spirale", "Spirale d'encre", "spiral", 1, 1000, 0.7, 0.2, 0.5, 4.5, *SPECIAL, projectileSpeed=130, arms=3,
             spread=14, burstInterval=0.12, duration=3.5, **ink),
        # 4 Foudre marine
        _atk("eclair", "Éclair réfléchi", "prism", 2, 1400, 1.0, 1.0, 0.6, 4.4, *SPECIAL, bounces=3, laserLength=1600,
             arms=1, origin=BEAK, color="#fff27a", beamTexture="plasma", beamWidth=12, knockback=220),
        _atk("meduses", "Méduses électriques", "projectile", 1, 1000, 0.55, 0.25, 0.45, 3.4, *CLASSIC,
             projectileSpeed=150, count=3, spread=30, homing=2.4, homingTime=1.8, origin=TIP, projectile="solar_bolt",
             hitEffect="projectile_burst"),
        _atk("couronne", "Couronne de photophores", "orbit", 2, 400, 0.75, 0.2, 0.5, 5.0, *RAISE, projectile="aurora_shard",
             projectileRadius=8, count=8, radius=180, spread=95, duration=3.0, knockback=220),
        # 5 Brisé
        _atk("grande_maree", "Grande marée", "flood", 2, 2000, 1.2, 0.5, 0.6, 11.0, *RAISE, duration=4.5, height=200,
             look="tide", sound="sounds/lava_rise.wav"),
        # 6 Nacre
        _atk("reflets", "Reflets de nacre", "prism", 2, 1400, 1.0, 1.0, 0.6, 4.2, *SPECIAL, bounces=3, laserLength=1600,
             arms=2, origin=BEAK, color="#ffd6ee", beamTexture="prism", beamWidth=14, knockback=220),
        _atk("anneaux", "Anneaux de nacre", "ring", 2, 1000, 0.7, 0.2, 0.5, 4.5, *SPECIAL, projectileSpeed=210, spread=50,
             **ring),
        _atk("double_fouet", "Double fouet", "dive", 2, 1100, 0.65, 0.2, 0.6, 5.0, *LASH, lungeSpeed=820, count=2,
             radius=240, burstInterval=0.6, lead=0.6, knockback=340, sound="sounds/wyrm_roar.wav"),
        # 7 Tourbillon
        _atk("lueurs", "Lueurs détachées", "summon", 0, 1500, 0.9, 0.4, 0.8, 12.0, "summon", "summon",
             summonId="photophore", summonCount=2, maxSummoned=4),
        _atk("maelstrom", "Maelström", "well", 2, 900, 0.8, 0.2, 0.6, 8.0, *RAISE, pull=95, radius=26, duration=4.5,
             knockback=240),
        # 8 Léviathan
        _atk("ronde_fouets", "Ronde des fouets", "dive", 2, 1200, 0.6, 0.2, 0.6, 6.5, *LASH, lungeSpeed=880, count=3,
             radius=260, burstInterval=0.5, lead=0.65, knockback=340, sound="sounds/wyrm_roar.wav"),
        _atk("nappe_double", "Déferlante d'encre", "perch", 1, 1400, 0.8, 1.2, 0.5, 7.0, *PERCH, lungeSpeed=700,
             radius=300, height=48, duration=3.5, arms=2, vulnerability=1.5, lead=0.5, knockback=280, sound=wet, look="ink"),
        # Its last wave (never chosen: the last stand, EnemyDefinition::finalAttacks)
        _atk("derniere_vague", "Dernière vague", "ring", 2, 3000, 0.6, 0.2, 0.3, 0.0, *SPECIAL, projectileSpeed=320,
             spread=60, radius=880, knockback=260),
        _atk("dernier_fouet", "Dernier fouet", "dive", 2, 3000, 0.7, 0.2, 0.3, 0.0, *LASH, lungeSpeed=900, count=3,
             radius=260, burstInterval=0.55, lead=0.6, knockback=340, sound="sounds/wyrm_roar.wav"),
        _atk("pluie_abyssale", "Pluie abyssale", "rain", 1, 3000, 0.6, 0.3, 0.3, 0.0, *RAISE, projectileSpeed=260,
             count=14, spread=48, gravity=300, rainHeight=360, lead=0.8, **ink),
    ]


def phase(i, name, attack_ids, speed, cooldown, telegraph, text, combo=3, **kw):
    p = {"name": name, "hpThreshold": round(1.0 - 0.125 * i, 3), "attacks": attack_ids, "speedMultiplier": speed,
         "cooldownMultiplier": cooldown, "combo": combo, "telegraphMultiplier": telegraph, "enterText": text,
         "animation": SHEETS[i], "lightColor": LIGHTS[i]}
    p.update(kw)
    return p


def phases():
    return [
        phase(0, "Abysses", ["fouet", "encre", "etau", "jet", "sonar"], 1.0, 1.0, 0.85,
              "Le Kraken remonte des abysses."),
        phase(1, "Marée", ["maree", "tourbillon", "geysers", "etreinte", "fouet", "encre"], 1.05, 0.95, 0.82,
              "La marée monte dans l'arène !"),
        phase(2, "Encre", ["nappe", "nuees", "courant", "spirale", "fouet"], 1.1, 0.92, 0.8,
              "Il crache son encre !"),
        phase(3, "Foudre marine", ["eclair", "meduses", "couronne", "sonar", "fouet"], 1.12, 0.9, 0.78,
              "Ses photophores crépitent d'éclairs !"),
        phase(4, "Brisé", ["grande_maree", "geysers", "nappe", "etreinte", "encre", "etau"], 1.15, 0.88, 0.75,
              "Son manteau se déchire : les abysses s'ouvrent !", enterSound="sounds/shell_shatter.wav"),
        phase(5, "Nacre", ["reflets", "anneaux", "etau", "nuees", "double_fouet", "spirale"], 1.2, 0.85, 0.72,
              "Une lueur de nacre monte de son cœur !"),
        phase(6, "Tourbillon", ["lueurs", "maelstrom", "tourbillon", "double_fouet", "nappe", "geysers"], 1.25, 0.82,
              0.7, "Ses lueurs se détachent et tournoient !", summonOnEnter=True),
        phase(7, "Léviathan", ["fouet", "ronde_fouets", "nappe_double", "grande_maree", "geysers", "nuees", "courant",
                               "etau", "spirale", "reflets", "jet", "maelstrom"], 1.3, 0.75, 0.62,
              "Léviathan ! Les abysses se déchaînent !", enrageTime=40, enterSound="sounds/supernova.wav"),
    ]


def kraken():
    return {
        "format": "cosmic.enemy", "id": "kraken", "name": "Kraken",
        "description": ("Un kraken colossal des mers noires enfouies sous la croûte des Confins. Huit phases, huit "
                        "lueurs : son manteau se déchire, la marée monte, et sa dernière vague doit être survécue."),
        "family": "miniboss", "animation": SHEETS[0], "size": [130, 135], "scale": 1.25, "hp": 800, "contactDamage": 2,
        "speed": 60, "chaseSpeedMultiplier": 1.3, "flying": True, "knockbackResist": 1, "detectionRange": 1000,
        "attackRange": 900, "loseRange": 4000, "needsLineOfSight": False, "movement": "fly",
        "reaction": "keepDistance", "fleeHealthRatio": 0, "preferredDistance": 240, "hopVelocity": 0,
        "attacks": attacks(),
        "powers": {"teleport": {"enabled": False}, "defend": {"enabled": False}, "reinforcements": {"enabled": False}},
        "damageMultipliers": {"charge_shot": 1.2, "wave": 1.25},
        "rewards": {"score": 23000, "drops": [{"type": "health_orb", "chance": 1.0}, {"type": "energy_orb", "chance": 1.0}]},
        "light": {"color": LIGHTS[0], "radius": 240},
        "sounds": {"hurt": "sounds/enemy_hurt.wav", "death": "sounds/supernova.wav", "alert": "sounds/wyrm_roar.wav"},
        "boss": {"title": "LE KRAKEN DES ABYSSES", "phases": phases(), "barIcon": "eye", "barColor": "#9ae8ff",
                 "finalAttacks": ["derniere_vague", "dernier_fouet", "pluie_abyssale", "derniere_vague"],
                 "finalText": "Sa dernière vague : survivez !"},
    }


def photophore():
    """Its escort (phase 7): a light torn from one of its arms (bubbles of light, no calls)."""
    with open(os.path.join(DATA, "wisp.json"), encoding="utf-8") as f:
        d = copy.deepcopy(json.load(f))
    d["id"] = "photophore"
    d["name"] = "Photophore abyssal"
    d["description"] = ("Une lueur arrachée à un bras du Kraken, qui flotte encore et crache des bulles de lumière.")
    d["hp"] = 7
    d["tint"] = "#8af0ff"
    d["light"] = {"color": "#7ae6ff", "radius": 52}
    d["detectionRange"] = 900
    d["loseRange"] = 2000
    d["attacks"] = [a for a in d["attacks"] if a.get("type") != "summon"]
    for a in d["attacks"]:
        a["name"] = "Bulles de lumière"
        a["projectile"] = "aurora_shard"
        a["hitEffect"] = "projectile_burst"
    d["rewards"] = {"score": 330, "drops": [{"type": "energy_orb", "chance": 0.5}]}
    return d


def main():
    for d in (kraken(), photophore()):
        with open(os.path.join(DATA, d["id"] + ".json"), "w", encoding="utf-8") as f:
            json.dump(d, f, ensure_ascii=False, indent=2)
            f.write("\n")
        print(d["id"], "PV", d["hp"], "attaques", len(d["attacks"]))


if __name__ == "__main__":
    main()
