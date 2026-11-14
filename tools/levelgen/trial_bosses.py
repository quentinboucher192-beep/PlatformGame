#!/usr/bin/env python3
"""Guardians of the three trials (the fourth level of each planet): elite versions of a
planet boss, bigger, tougher, with the attacks of two bosses of the planet and fiercer
phases (longer combos, shorter wind-ups, earlier rage). Writes data/enemies/<id>.json.
Run from the repository root after the bosses themselves:  python3 tools/levelgen/trial_bosses.py
"""
import copy
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TRIALS = [
    {"id": "archon_catacombs", "base": "thornwarden", "extra": "spore_queen", "extra_attacks": ["spore_rain", "vines"],
     "name": "Archonte des Catacombes", "title": "L'ARCHONTE DES CATACOMBES",
     "description": "Gardien ancestral réveillé au cœur de la machinerie des Anciens.",
     "phases": ["Réveil de l'Archonte", "Engrenages", "Mécanisme ultime"]},
    {"id": "crucible_heart", "base": "magmaroth", "extra": "forge_core", "extra_attacks": ["embers", "volley"],
     "name": "Cœur du Creuset", "title": "LE CŒUR DU CREUSET",
     "description": "Le magma du creuset ancestral a pris forme et défend sa forge.",
     "phases": ["Ébullition", "Coulée ardente", "Fusion du cœur"]},
    {"id": "howling_wyrm", "base": "cryowyrm", "extra": "frost_hydra", "extra_attacks": ["spikes", "icicles"],
     "name": "Wyrm Hurlante", "title": "LA WYRM HURLANTE",
     "description": "La reine des vents de la crevasse, plus vaste et plus rapide que ses sœurs.",
     "phases": ["Hurlement", "Tempête de crevasse", "Blizzard éternel"]},
]


def load(eid):
    with open(os.path.join(ROOT, "data", "enemies", eid + ".json"), encoding="utf-8") as f:
        return json.load(f)


for t in TRIALS:
    d = copy.deepcopy(load(t["base"]))
    extra = load(t["extra"])
    d["id"], d["name"], d["description"] = t["id"], t["name"], t["description"]
    d["hp"] = int(round(d["hp"] * 1.5))
    d["scale"] = round(d.get("scale", 1.0) * 1.25, 3)
    d["size"] = [round(d["size"][0] * 1.25), round(d["size"][1] * 1.25)]
    d["contactDamage"] = 2
    for aid in t["extra_attacks"]:
        a = copy.deepcopy(next(x for x in extra["attacks"] if x["id"] == aid))
        a["id"] = "x_" + aid
        # Animation clips of the guardian's own sheet (a similar attack of the base boss).
        ranged = ("projectile", "spread", "arc", "rain", "eruption")
        same = [x for x in d["attacks"] if x["type"] == a["type"]] or \
               [x for x in d["attacks"] if (x["type"] in ranged) == (a["type"] in ranged)] or d["attacks"]
        a["telegraphAnim"] = same[0].get("telegraphAnim", "telegraph")
        a["attackAnim"] = same[0].get("attackAnim", "attack")
        a["lead"] = max(a.get("lead", 0), 0.8)
        d["attacks"].append(a)
    for a in d["attacks"]:
        if a["type"] != "summon":
            a["damage"] = max(a["damage"], 2) if a["damage"] >= 2 else a["damage"]
    phases = d["boss"]["phases"]
    extra_ids = ["x_" + a for a in t["extra_attacks"]]
    for i, p in enumerate(phases):
        p["name"] = t["phases"][min(i, 2)]
        if i >= 1:
            p["attacks"] = p["attacks"] + [x for x in extra_ids[: i] if x not in p["attacks"]]
        p["combo"] = [2, 3, 3][min(i, 2)]
        p["telegraphMultiplier"] = [0.9, 0.8, 0.7][min(i, 2)]
        p["cooldownMultiplier"] = round(p.get("cooldownMultiplier", 1.0) * 0.85, 2)
    d["boss"]["title"] = t["title"]
    d["boss"]["enrageTime"] = 60
    with open(os.path.join(ROOT, "data", "enemies", t["id"] + ".json"), "w", encoding="utf-8") as f:
        json.dump(d, f, ensure_ascii=False, indent=2)
        f.write("\n")
    print(t["id"], "PV", d["hp"], "attaques", len(d["attacks"]))
