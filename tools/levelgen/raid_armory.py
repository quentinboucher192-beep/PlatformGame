#!/usr/bin/env python3
"""2.75: the armory (« vestiaire ») of each branch of the raid « Asterion » (docs/RAIDS.md §20).

Before the real zones of a branch, the group appears in its armory (data/raids/asterion/<part>/armory.json,
zone 0 of the branch for the game: game/raid/RaidBranch, never in the server's catalog): a rack of power
tablets, one per power of the game, where each player takes the powers they want for the raid (four at
most, the existing power slots: a tablet touched with every slot taken asks which power to leave, a choice
that goes through InputFrame like every choice, so the replays stay exact; an armory tablet keeps its own
power for everyone). Then the whole group on the « Prêts » plate (every player in play) opens the door of
the route: its passage leads to the branch's start zone, the powers chosen carried there.

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


def build(part):
    with open(os.path.join(A.RAID_DIR, part, "branch.json"), encoding="utf-8") as f:
        cat = json.load(f)
    th = theme(part)
    L = Level("raid_%s_vestiaire" % part, "Asterion · %s · Vestiaire" % cat["name"].capitalize(), W, H)
    L.planet(th["planet"])
    d = L.data
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
    starts = [(4 + k, F) for k in range(0, 16, 2)]
    L.start(*starts[0])
    L.sign(11, F, A.tr("Vestiaire : touchez une tablette pour prendre son pouvoir. Quatre au plus : choisissez celui que vous laissez.",
                       "Armory: touch a tablet to take its power. Four at most: choose the one you leave."))
    # The rack: one tablet per power, each lit.
    x = 16
    for p in powers():
        oid = L.power(x, F - 1, p)
        L.data["objects"][-1]["props"].update({"armory": True, "station": True})
        L.light(x, F - 3, th["glow"], 46, flicker=0.05)
        x += 5
    assert x <= W - 24, x
    # « Prêts » : every player in play on the plate opens the door of the route.
    door = L.door(W - 10, F - 4, 4)
    plate = {"id": "vestiaire_prets", "rect": [(W - 20) * T, (F - 4) * T, 8 * T, 4 * T], "players": 8, "targets": [door],
             "action": "open", "label": A.tr("Prêts", "Ready"),
             "message": A.tr("Tout le groupe est prêt : la route s'ouvre !", "The whole group is ready: the route opens!")}
    L.sign(W - 22, F, A.tr("Plaque « Prêts » : tout le groupe dessus ouvre la porte de la route.",
                           "« Ready » plate: the whole group on it opens the route's door."))
    L.light(W - 16, F - 3, "#ffd27a", 80, flicker=0.1)
    passage = {"id": "p0_%d" % cat["start_zone"], "to": cat["start_zone"], "rect": [(W - 6) * T, (F - 4) * T, 4 * T, 4 * T], "lock": "", "door": ""}  # the plate opens the door, not a lock
    start_name = next(z["name"] for z in cat["zones"] if z["id"] == cat["start_zone"])
    L.sign(W - 4, F, A.tr("Passage vers : %s" % start_name, "Way to: %s" % start_name))
    d["metadata"] = {"generator": "tools/levelgen/raid_armory.py",
                     "raid": {"branch": cat["id"], "zone": 0, "spawns": {"start": [[sx * T, sy * T] for sx, sy in starts]},
                              "passages": [passage], "objectives": [], "magma": None, "sealed": None, "coop": [plate]}}
    ids = [o["id"] for o in d["objects"]]
    assert len(ids) == len(set(ids)), part
    A.write_json(os.path.join(A.RAID_DIR, part, "armory.json"), d)
    return d


def main():
    for part in PARTS:
        build(part)
        print("%s : vestiaire (%d pouvoirs)" % (part, len(powers())))
    if "--en" in sys.argv and sys.argv.index("--en") + 1 < len(sys.argv):
        A.write_english(sys.argv[sys.argv.index("--en") + 1])


if __name__ == "__main__":
    main()
