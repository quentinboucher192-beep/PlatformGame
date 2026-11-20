#!/usr/bin/env python3
"""2.75: the armory (« vestiaire ») of each branch of the raid « Asterion » (docs/RAIDS.md §20).

Before the real zones of a branch, the group appears in its armory (data/raids/asterion/<part>/armory.json,
zone 0 of the branch for the game: game/raid/RaidBranch, never in the server's catalog): a rack of power
tablets, one per power of the game, where each player takes the powers they want for the raid (four at
most, the existing power slots: a tablet touched with every slot taken asks which power to leave, a choice
that goes through InputFrame like every choice, so the replays stay exact; an armory tablet keeps its own
power for everyone). 2.77: the tablets are UP, each on its own small ledge (one-way), on two storeys — the
lower ledges 3 tiles over the floor, the upper ones 6 —, so one takes a power only by jumping onto it on
purpose: walking along the floor to the « Prêts » plate takes nothing (tests/test_raid_bots.cpp). Each
tablet is reached by a normal jump (from the floor for the lower ones, from a lower ledge for the upper
ones): checked below. The game writes each power's name over its tablet (display only). Then the whole group on the « Prêts » plate (every player in play) opens the door of
the route: its passage leads to the branch's start zone, the powers chosen carried there.

2.78: a second armory before each lair (« Vestiaire de l'antre », data/raids/asterion/<part>/armory_lair.json, zone 9
of the game, never in the server's catalog): the passage of the zone before the lair (its ambushes all beaten) leads
there; the same tablets to adjust one's powers for the gigaboss, and the « Prêts » plate that wants every player
present: those standing on it go to the lair together (a passage of the group, « plate »), the others stay. A way
back leads to the zone before. No enemy, no danger: a safe place.

Generated, never edited by hand:  python3 tools/levelgen/raid_armory.py
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import raid_asterion as A  # noqa: E402
import raid_coeur  # noqa: E402
from levelkit import Level, ROOT, T  # noqa: E402

PARTS = ["crane", "bras_gauche", "bras_droit", "torse", "abdomen", "jambe_gauche", "jambe_droite", "coeur"]
POWERS_DIR = os.path.join(ROOT, "data", "powers")
W, H, F = 136, 30, 24  # width, height (tiles), the floor's row
LOW, HIGH = 3, 6  # 2.77: the ledges of the tablets, tiles over the floor (the lower and the upper storey)
# data/player.json: a jump rises 58 px, the body is 22 px tall.
JUMP, BODY = 58, 22
BRAS_DROIT = dict(planet="pyralis", ground="basalt", ledge="basalt_ledge", glow="#ff8a3d")


def theme(part):
    if part == "bras_droit":
        return BRAS_DROIT
    if part == "coeur":
        return raid_coeur.THEME
    return A.THEMES[part]


def powers():
    """Every power of the game (data/powers), in a fixed order."""
    out = []
    for name in sorted(os.listdir(POWERS_DIR)):
        if name.endswith(".json"):
            with open(os.path.join(POWERS_DIR, name), encoding="utf-8") as f:
                out.append(json.load(f)["id"])
    return out


def check_reach(L):
    """2.77: walking on the floor touches no tablet; each one is touched by a normal jump (one press, no
    double jump): from the floor for the lower storey, from a lower ledge for the upper one."""
    floor = F * T
    tablets = [o for o in L.data["objects"] if o["type"] == "power_unlock"]
    ledges = [o for o in L.data["objects"] if o["type"] == "oneway"]
    for t in tablets:
        x, y, w, h = t["rect"]
        assert y + h < floor - BODY, ("walking touches", t["props"]["power"])
        under = [g for g in ledges if g["rect"][0] <= x + w / 2 <= g["rect"][0] + g["rect"][2] and abs(g["rect"][1] - (y + h + 4)) < 1]
        assert under, ("no ledge", t["props"]["power"])
        top = under[0]["rect"][1]
        # Where one jumps from: the floor for the lower storey, the top of a lower ledge for the upper one.
        stand = floor if top == floor - LOW * T else floor - LOW * T
        assert stand - BODY - JUMP <= y + h, ("out of reach", t["props"]["power"])
        assert stand - JUMP <= top, ("its ledge out of reach", t["props"]["power"])


def build(part, lair=False):
    """The armory of a branch (zone 0); 2.78, `lair`: the one before its lair (zone A.LAIR_ARMORY)."""
    with open(os.path.join(A.RAID_DIR, part, "branch.json"), encoding="utf-8") as f:
        cat = json.load(f)
    th = theme(part)
    lair_zone = next(z for z in cat["zones"] if z["boss"])
    before = next(z for z in cat["zones"] if z["id"] == lair_zone["id"] - 1)
    if lair:
        L = Level("raid_%s_vestiaire_antre" % part, "Asterion · %s · Vestiaire de l'antre" % cat["name"].capitalize(), W, H)
    else:
        L = Level("raid_%s_vestiaire" % part, "Asterion · %s · Vestiaire" % cat["name"].capitalize(), W, H)
    L.planet(th["planet"])
    d = L.data
    if lair:
        d["description"] = A.tr("Le vestiaire de l'antre : chacun ajuste ses pouvoirs pour le gigaboss, puis tous les joueurs présents sur la plaque « Prêts ».",
                                "The lair's armory: everyone adjusts their powers for the gigaboss, then every player present on the « Ready » plate.")
    else:
        d["description"] = A.tr("Le vestiaire du raid : chacun choisit ses pouvoirs avant la route, puis tout le groupe sur la plaque « Prêts ».",
                                "The raid's armory: everyone picks their powers before the route, then the whole group on the « Ready » plate.")
    d["difficulty"] = 1
    d["startPowers"] = list(A.START_POWERS)
    d["maxPowers"] = 4
    d["killY"] = (H - 1) * T
    d["timer"] = {"mode": "unlimited", "limit": 3600, "onExpire": "continue", "referenceTime": 600, "challengeMaxDeaths": 0}
    d["medals"] = {"bronze": {}, "silver": {"maxTime": 3000}, "gold": {"maxTime": 2400}, "platinum": {"maxTime": 1800, "maxDeaths": 0}}
    # The room: a floor, a left wall, a ceiling of rock (nothing to fall into, nothing to fight).
    L.solid(0, F, W, H, th["ground"])
    L.solid(0, 0, 2, F, th["ground"])
    L.solid(0, 0, W, 2, th["ground"])
    # 2.78: the lair's armory has a way back on its west wall: its players appear clear of it.
    starts = [((8 if lair else 4) + k, F) for k in range(0, 16, 2)]
    L.start(*starts[0])
    if lair:
        L.sign(11, F, A.tr("Vestiaire de l'antre : ajustez vos pouvoirs pour %s. Sautez sur une tablette ; quatre au plus." % cat["gigaboss"],
                           "Lair's armory: adjust your powers for %s. Jump onto a tablet; four at most." % cat["gigaboss"]))
    else:
        L.sign(11, F, A.tr("Vestiaire : sautez sur une tablette pour prendre son pouvoir. Quatre au plus : choisissez celui que vous laissez.",
                           "Armory: jump onto a tablet to take its power. Four at most: choose the one you leave."))
    # The rack: one tablet per power, each lit, UP on its own ledge (2.77): the even ones 3 tiles over the
    # floor, the odd ones 6 (two storeys), 5 tiles apart; the floor stays free to walk to the plate.
    x = 16
    for k, p in enumerate(powers()):
        ledge = F - (LOW if k % 2 == 0 else HIGH)
        L.oneway(x - 1, x + 1, ledge, th.get("ledge", "basalt_ledge"))
        L.power(x, ledge - 1, p)
        L.data["objects"][-1]["props"].update({"armory": True, "station": True})
        L.light(x, ledge - 3, th["glow"], 46, flicker=0.05)
        x += 5
    assert x <= W - 24, x
    check_reach(L)
    # « Prêts » : every player in play on the plate opens the door of the route; 2.78: the players standing on
    # it take its passage together at that step (« plate »: a passage of the group), nobody else.
    door = L.door(W - 10, F - 4, 4)
    if lair:
        plate = {"id": "vestiaire_prets", "rect": [(W - 20) * T, (F - 4) * T, 8 * T, 4 * T], "players": 8, "targets": [door],
                 "action": "open", "label": A.tr("Prêts", "Ready"),
                 "message": A.tr("Tous les joueurs présents sont prêts : en route vers l'antre !", "Every player present is ready: on to the lair!")}
        L.sign(W - 22, F, A.tr("Plaque « Prêts » : tous les joueurs présents dessus partent ensemble vers l'antre.",
                               "« Ready » plate: every player present on it leaves for the lair together."))
    else:
        plate = {"id": "vestiaire_prets", "rect": [(W - 20) * T, (F - 4) * T, 8 * T, 4 * T], "players": 8, "targets": [door],
                 "action": "open", "label": A.tr("Prêts", "Ready"),
                 "message": A.tr("Tout le groupe est prêt : la route s'ouvre !", "The whole group is ready: the route opens!")}
        L.sign(W - 22, F, A.tr("Plaque « Prêts » : tout le groupe dessus ouvre la porte de la route.",
                               "« Ready » plate: the whole group on it opens the route's door."))
    L.light(W - 16, F - 3, "#ffd27a", 80, flicker=0.1)
    ahead = lair_zone["id"] if lair else cat["start_zone"]
    zone_id = A.LAIR_ARMORY if lair else 0
    passage = {"id": "p%d_%d" % (zone_id, ahead), "to": ahead, "rect": [(W - 6) * T, (F - 4) * T, 4 * T, 4 * T], "lock": "", "door": "",
               "plate": "vestiaire_prets"}  # the plate opens the door, not a lock (2.78: and takes those on it)
    ahead_name = next(z["name"] for z in cat["zones"] if z["id"] == ahead)
    L.sign(W - 4, F, A.tr("Passage vers : %s" % ahead_name, "Way to: %s" % ahead_name))
    passages = [passage]
    spawns = {"start": [[sx * T, sy * T] for sx, sy in starts]}
    if lair:
        # The way back to the zone before the lair (its passage to the lair arrives here).
        passages.append({"id": "p%d_%d" % (zone_id, before["id"]), "to": before["id"], "rect": [2 * T, (F - 4) * T, 2 * T, 4 * T], "lock": "", "door": ""})
        L.sign(5, F, A.tr("Retour vers : %s" % before["name"], "Back to: %s" % before["name"]))
        L.light(3, F - 2, "#9fd8ff", 70, flicker=0.05)
        spawns[str(before["id"])] = spawns["start"]
    d["metadata"] = {"generator": "tools/levelgen/raid_armory.py",
                     "raid": {"branch": cat["id"], "zone": zone_id, "spawns": spawns,
                              "passages": passages, "objectives": [], "magma": None, "sealed": None, "coop": [plate]}}
    ids = [o["id"] for o in d["objects"]]
    assert len(ids) == len(set(ids)), part
    A.write_json(os.path.join(A.RAID_DIR, part, "armory_lair.json" if lair else "armory.json"), d)
    return d


def main():
    for part in PARTS:
        build(part)
        build(part, lair=True)  # 2.78
        print("%s : vestiaire et vestiaire de l'antre (%d pouvoirs)" % (part, len(powers())))
    if "--en" in sys.argv and sys.argv.index("--en") + 1 < len(sys.argv):
        A.write_english(sys.argv[sys.argv.index("--en") + 1])


if __name__ == "__main__":
    main()
