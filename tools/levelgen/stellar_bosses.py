#!/usr/bin/env python3
"""Bosses of the Stellar Core (Noyau stellaire, version 2.7):

* the echoes of the twelve guardians of the campaign (data/enemies/echo_<id>.json): the
  same fight, a spectral tint, and no health orb when they fall (the boss rush is played
  without any healing);
* the Astrarque (data/enemies/astrarque.json), the heart of the star: the Forgeron
  Primordial's body, bigger and gold-lit, with the attacks of the three other flying
  guardians (Sentinelle Prime, Cryowyrm, Ignivore), three phases and rage after 90 s.

Run from the repository root after trial_bosses.py:  python3 tools/levelgen/stellar_bosses.py
"""
import copy
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GUARDIANS = ["thornwarden", "spore_queen", "prime_sentinel", "archon_catacombs",
             "cryowyrm", "frost_hydra", "glaciarch", "howling_wyrm",
             "magmaroth", "ignivore", "forge_core", "crucible_heart"]
ECHO_TINT = "#a894ff"


def load(eid):
    with open(os.path.join(ROOT, "data", "enemies", eid + ".json"), encoding="utf-8") as f:
        return json.load(f)


def save(d):
    with open(os.path.join(ROOT, "data", "enemies", d["id"] + ".json"), "w", encoding="utf-8") as f:
        json.dump(d, f, ensure_ascii=False, indent=2)
        f.write("\n")


for gid in GUARDIANS:
    d = copy.deepcopy(load(gid))
    d["id"] = "echo_" + gid
    d["variantOf"] = gid
    d["name"] = "Écho : " + d["name"]
    d["description"] = "Reflet de %s, rappelé par le cœur de l'étoile. Il ne laisse aucun soin derrière lui." % load(gid)["name"]
    d["tint"] = ECHO_TINT
    d["rewards"]["drops"] = [{"type": "energy_orb", "chance": 1.0}]
    d["boss"]["title"] = "ÉCHO — " + d["boss"]["title"]
    save(d)
    print(d["id"], "PV", d["hp"])

# ------------------------------------------------------------------ Astrarque
base = load("forge_core")
d = copy.deepcopy(base)
d["id"], d["name"] = "astrarque", "Astrarque"
d["description"] = "Le cœur de l'étoile a pris conscience : il porte les échos de tous les gardiens du système."
d["hp"] = 190
d["scale"] = 1.6
d["size"] = [round(d["size"][0] * 1.6), round(d["size"][1] * 1.6)]
d["contactDamage"] = 2
d["tint"] = "#ffe7a8"
d["light"] = {"color": "#fff1c9", "radius": 150}
taken = {"prime_sentinel": ["debris"], "cryowyrm": ["breath", "meteor"], "ignivore": ["bolts", "dive"]}
extra = []
for src, ids in taken.items():
    s = load(src)
    for aid in ids:
        a = copy.deepcopy(next(x for x in s["attacks"] if x["id"] == aid))
        a["id"] = "a_" + src.split("_")[0] + "_" + aid
        same = [x for x in d["attacks"] if x["type"] == a["type"]] or d["attacks"]
        a["telegraphAnim"] = same[0].get("telegraphAnim", "telegraph")
        a["attackAnim"] = same[0].get("attackAnim", "attack")
        a["lead"] = max(a.get("lead", 0), 0.8)
        d["attacks"].append(a)
        extra.append(a["id"])
names = [("Aube stellaire", "L'étoile s'éveille."), ("Couronne ardente", "La couronne s'embrase !"),
         ("Supernova", "Le cœur de l'étoile se déchaîne !")]
for i, p in enumerate(d["boss"]["phases"]):
    p["name"], p["enterText"] = names[min(i, 2)]
    p["attacks"] = p["attacks"] + [x for x in extra[: 2 + 2 * i] if x not in p["attacks"]]
    p["combo"] = [2, 3, 4][min(i, 2)]
    p["telegraphMultiplier"] = [0.9, 0.8, 0.7][min(i, 2)]
    p["cooldownMultiplier"] = round(p.get("cooldownMultiplier", 1.0) * 0.85, 2)
d["boss"]["title"] = "L'ASTRARQUE"
d["boss"]["enrageTime"] = 90
d["boss"]["barIcon"] = "star"
d["boss"]["barColor"] = "#fff1c9"
d["rewards"] = {"score": 8000, "drops": [{"type": "energy_orb", "chance": 1.0}]}
save(d)
print("astrarque PV", d["hp"], "attaques", len(d["attacks"]))
