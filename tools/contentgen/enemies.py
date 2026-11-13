#!/usr/bin/env python3
"""Generates the v2 enemy definitions (data/enemies/*.json).

* 6 new Sylvanox species (nightwing, gloop, weaver, bulwark, bomber, burrower) + the small
  slimes they split into,
* themed variants of the 12 regular species for Pyralis (fire) and Glacior (ice),
* the 8 v2 bosses (bosses.py).

Run from the repository root:  python3 tools/contentgen/enemies.py
The 7 v1 definitions (glowmite ... thornwarden) are read, never rewritten.
Every file stays editable in the in-game enemy editor.
"""
import copy
import json
import os
import sys

sys.dont_write_bytecode = True
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA = os.path.join(ROOT, "data", "enemies")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

SOUNDS = {"hurt": "sounds/enemy_hurt.wav", "death": "sounds/enemy_death.wav", "alert": "sounds/enemy_alert.wav"}
NO_POWERS = {"teleport": {"enabled": False}, "defend": {"enabled": False}, "reinforcements": {"enabled": False}}


def enemy(id, name, desc, family, size, hp, speed, movement, reaction, attacks, *, flying=False, contact=1,
          chase=1.6, detection=150, attack_range=28, lose=260, los=True, scale=1.0, kb_resist=0.0, flee=0.0,
          preferred=110, hop=220, powers=None, mult=None, score=100, drops=None, light=("#39f0d8", 26),
          sounds=None, animation=None, death_spawn=None):
    d = {
        "format": "cosmic.enemy", "id": id, "name": name, "description": desc, "family": family,
        "animation": animation or f"animations/{id}.json", "size": size, "scale": scale,
        "hp": hp, "contactDamage": contact, "speed": speed, "chaseSpeedMultiplier": chase, "flying": flying,
        "knockbackResist": kb_resist, "detectionRange": detection, "attackRange": attack_range, "loseRange": lose,
        "needsLineOfSight": los, "movement": movement, "reaction": reaction, "fleeHealthRatio": flee,
        "preferredDistance": preferred, "hopVelocity": hop, "attacks": attacks,
        "powers": copy.deepcopy(powers or NO_POWERS), "damageMultipliers": mult or {},
        "rewards": {"score": score, "drops": drops or [{"type": "energy_orb", "chance": 0.3}]},
        "light": {"color": light[0], "radius": light[1]}, "sounds": dict(sounds or SOUNDS),
    }
    if death_spawn:
        d["deathSpawn"] = {"id": death_spawn[0], "count": death_spawn[1]}
    return d


def atk(id, name, type, damage, range, telegraph, active, recover, cooldown, tanim="telegraph", aanim="attack", **kw):
    a = {"id": id, "name": name, "type": type, "damage": damage, "range": range, "telegraph": telegraph,
         "active": active, "recover": recover, "cooldown": cooldown, "telegraphAnim": tanim, "attackAnim": aanim}
    a.update(kw)
    return a


# ---------------------------------------------------------------- new species
def new_species():
    out = []
    out.append(enemy(
        "nightwing", "Aile-de-nuit", "Chauve-souris des ruines aux ailes veinées de lumière. Tournoie puis plonge sur sa proie.",
        "flying", [14, 10], 2, 55, "fly", "chase",
        [atk("dive", "Piqué", "lunge", 1, 130, 0.45, 0.55, 0.5, 1.8, minRange=24, lungeSpeed=235, knockback=160)],
        flying=True, chase=1.7, detection=180, attack_range=120, lose=320, los=False, score=120,
        light=("#39f0d8", 22), sounds={**SOUNDS, "alert": "sounds/bat_screech.wav"}))
    out.append(enemy(
        "gloop", "Gloop", "Masse gélatineuse qui bondit en crachant de l'acide. Se divise en deux quand on le frappe à mort.",
        "ground", [14, 10], 3, 30, "hop", "chase",
        [atk("spit", "Crachat acide", "arc", 1, 150, 0.5, 0.2, 0.6, 2.2, projectile="acid_glob", projectileSpeed=150,
             gravity=520, count=1, hitEffect="acid_burst")],
        chase=1.3, detection=150, attack_range=140, hop=210, score=110,
        mult={"ground_pound": 1.5}, light=("#8ff26a", 24), death_spawn=("gloop_small", 2)))
    small = enemy(
        "gloop_small", "Petit Gloop", "Fragment d'un Gloop. Plus rapide, mais fragile.",
        "ground", [9, 7], 1, 42, "hop", "chase", [], chase=1.5, detection=160, hop=190, score=40, scale=0.65,
        animation="animations/gloop.json", light=("#8ff26a", 16), drops=[{"type": "energy_orb", "chance": 0.15}])
    out.append(small)
    out.append(enemy(
        "weaver", "Tisseuse", "Araignée à l'abdomen de cristal. Sa toile colle aux bottes et ralentit.",
        "ground", [16, 12], 3, 40, "patrol", "keepDistance",
        [atk("web", "Toile", "projectile", 1, 170, 0.45, 0.2, 0.5, 2.0, projectile="web", projectileSpeed=170,
             slow=0.5, slowDuration=1.8, hitEffect="web_burst"),
         atk("bite", "Morsure", "melee", 1, 22, 0.3, 0.15, 0.4, 1.2, weight=0.6)],
        chase=1.5, detection=170, attack_range=160, preferred=100, score=130, light=("#b04cff", 26)))
    out.append(enemy(
        "bulwark", "Rempart", "Chevalier-automate au bouclier de glyphes. Prenez-le à revers, ou brisez sa garde d'une Frappe sismique.",
        "heavy", [16, 24], 6, 26, "patrol", "chase",
        [atk("thrust", "Coup de lance", "melee", 2, 34, 0.55, 0.2, 0.6, 1.6, knockback=230)],
        chase=1.4, detection=150, attack_range=34, kb_resist=0.7, score=220,
        powers={"teleport": {"enabled": False}, "defend": {"enabled": True, "duration": 1.4, "cooldown": 2.4},
                "reinforcements": {"enabled": False}},
        mult={"ground_pound": 2.0, "charge_shot": 1.5, "impulse": 1.5},
        drops=[{"type": "health_orb", "chance": 0.25}, {"type": "energy_orb", "chance": 0.4}], light=("#39f0d8", 30)))
    out.append(enemy(
        "bomber", "Bombardier", "Drone de soute qui survole sa cible et largue des charges explosives.",
        "robot", [18, 12], 3, 45, "fly", "chase",
        [atk("bomb", "Largage", "arc", 2, 210, 0.6, 0.2, 0.6, 2.4, projectile="bomb", projectileSpeed=120,
             gravity=520, explosionRadius=26, projectileRadius=5)],
        flying=True, chase=1.3, detection=210, attack_range=200, lose=340, los=False, score=150,
        mult={"wave": 1.5, "charge_shot": 1.5}, light=("#ffb347", 30)))
    out.append(enemy(
        "burrower", "Fouisseur", "Ver géant qui disparaît sous terre et resurgit près de sa proie.",
        "ground", [18, 16], 4, 0, "stationary", "chase",
        [atk("spit", "Crachat de pierres", "arc", 1, 190, 0.5, 0.2, 0.6, 2.2, projectile="rock", projectileSpeed=160,
             gravity=520, count=2, spread=12, hitEffect="slam_dust"),
         atk("bite", "Morsure", "melee", 2, 28, 0.4, 0.2, 0.6, 1.4)],
        detection=190, attack_range=180, los=False, score=170,
        powers={"teleport": {"enabled": True, "cooldown": 3.5, "range": 120}, "defend": {"enabled": False},
                "reinforcements": {"enabled": False}},
        mult={"ground_pound": 2.0}, light=("#ffb347", 24), sounds={**SOUNDS, "alert": "sounds/burrow.wav"}))
    return out


# ---------------------------------------------------------------- variants
THEMES = {
    "fire": {
        "hp": 1.3, "speed": 1.1, "light": "#ff8a3d", "hit": "fire_burst",
        "projectiles": {"drone_bolt": "fireball", "spore": "magma_glob", "wisp_orb": "fireball", "acid_glob": "magma_glob",
                        "web": "ember", "rock": "magma_glob", "bomb": "bomb"},
        "slow": None, "mult": {"wave": 0.8},
    },
    "ice": {
        "hp": 1.2, "speed": 0.95, "light": "#7ff6ff", "hit": "ice_burst",
        "projectiles": {"drone_bolt": "ice_shard", "spore": "frost_orb", "wisp_orb": "frost_orb", "acid_glob": "frost_orb",
                        "web": "web", "rock": "ice_shard", "bomb": "bomb"},
        "slow": (0.55, 1.5), "mult": {"charge_shot": 1.3},
    },
}
VARIANTS = {  # base: (fire id, fire name, ice id, ice name)
    "glowmite": ("ember_mite", "Braisite", "frost_mite", "Givrite"),
    "sentinel": ("forge_drone", "Drone de forge", "cryo_drone", "Drone cryogénique"),
    "sporeflower": ("cinder_bloom", "Fleur de cendre", "frost_bloom", "Fleur de givre"),
    "stalker": ("lava_stalker", "Traqueur de lave", "snow_stalker", "Traqueur des neiges"),
    "golem": ("magma_golem", "Golem de magma", "ice_golem", "Golem de glace"),
    "wisp": ("fire_wisp", "Feu follet ardent", "frost_wisp", "Feu follet gelé"),
    "nightwing": ("ash_wing", "Aile de cendre", "frost_wing", "Aile de givre"),
    "gloop": ("magma_gloop", "Gloop de magma", "ice_gloop", "Gloop de glace"),
    "weaver": ("ember_weaver", "Tisseuse ardente", "frost_weaver", "Tisseuse de givre"),
    "bulwark": ("forge_bulwark", "Rempart de forge", "frost_bulwark", "Rempart gelé"),
    "bomber": ("forge_bomber", "Bombardier de forge", "cryo_bomber", "Bombardier cryogénique"),
    "burrower": ("lava_burrower", "Fouisseur de lave", "ice_burrower", "Fouisseur de glace"),
}
DESC = {
    "fire": "Variante de Pyralis forgée dans la lave : plus résistante et plus rapide.",
    "ice": "Variante de Glacior : ses attaques glacées ralentissent.",
}


def variant(base, theme, vid, vname):
    t = THEMES[theme]
    d = copy.deepcopy(base)
    d["id"] = vid
    d["name"] = vname
    d["description"] = base["description"] + " " + DESC[theme]
    d["animation"] = f"animations/{vid}.json"
    d["hp"] = max(1, round(base["hp"] * t["hp"]))
    d["speed"] = round(base["speed"] * t["speed"], 1)
    d["light"]["color"] = t["light"]
    d["rewards"]["score"] = int(base["rewards"]["score"] * 1.25)
    for k, v in t["mult"].items():
        d["damageMultipliers"].setdefault(k, v)
    for a in d["attacks"]:
        if "projectile" in a:
            a["projectile"] = t["projectiles"].get(a["projectile"], a["projectile"])
            a["hitEffect"] = "fire_burst" if theme == "fire" and a["projectile"] != "bomb" else a.get("hitEffect", t["hit"])
            if theme == "ice":
                a["hitEffect"] = "ice_burst" if a["projectile"] != "web" else "web_burst"
        if theme == "ice" and t["slow"] and a["type"] in ("projectile", "arc", "spread", "slam", "melee", "lunge"):
            a.setdefault("slow", t["slow"][0])
            a.setdefault("slowDuration", t["slow"][1])
        if theme == "fire" and a["type"] in ("melee", "slam", "lunge"):
            a["damage"] = a["damage"] + (1 if base["family"] in ("heavy",) else 0)
        if a["type"] == "summon":
            a["summonId"] = themed_id(a.get("summonId", "glowmite"), theme)
    if "deathSpawn" in d:
        d["deathSpawn"]["id"] = themed_id(d["deathSpawn"]["id"], theme)
    return d


def themed_id(base_id, theme):
    if base_id.endswith("_small"):
        root = base_id[:-6]
        return themed_id(root, theme) + "_small"
    if base_id in VARIANTS:
        f, _, i, _ = VARIANTS[base_id]
        return f if theme == "fire" else i
    return base_id


def save(d):
    path = os.path.join(DATA, d["id"] + ".json")
    with open(path, "w", encoding="utf-8") as f:
        f.write(json.dumps(d, ensure_ascii=False, indent=2) + "\n")
    return path


def main():
    bases = {}
    for b in ("glowmite", "sentinel", "sporeflower", "stalker", "golem", "wisp"):
        with open(os.path.join(DATA, b + ".json"), encoding="utf-8") as f:
            bases[b] = json.load(f)
    written = []
    for d in new_species():
        bases.setdefault(d["id"], d)
        written.append(save(d))
    for base_id, (fid, fname, iid, iname) in VARIANTS.items():
        written.append(save(variant(bases[base_id], "fire", fid, fname)))
        written.append(save(variant(bases[base_id], "ice", iid, iname)))
    # small slimes of the themed gloops
    for theme, gid in (("fire", "magma_gloop"), ("ice", "ice_gloop")):
        s = variant(bases["gloop_small"], theme, gid + "_small", "Petit " + ("Gloop de magma" if theme == "fire" else "Gloop de glace"))
        s["animation"] = f"animations/{gid}.json"
        written.append(save(s))
    import bosses
    for d in bosses.all_bosses():
        written.append(save(d))
    print(f"{len(written)} enemy definitions written")


if __name__ == "__main__":
    main()
