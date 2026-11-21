"""2.69 (phase 4B): the lair of HEARTBREAKER, zone 8 of the TORSE (docs/RAIDS.md §13), unsealed.

Called by raid_asterion.py (torse()) with that module itself (`A`), never run alone. The arena
(raid_skullmaw.shell: the format of the seven lairs) and the fight (metadata.raid.gigaboss, its routine
"heartbreaker", game/raid/HeartbreakerRoutine):

* the torso in the middle of the arena, two flank towers of ledges against its ribs: its high plates
  are shot from there, its two ribs (the rules' vents) are pried open from both at once;
* five storeys of ledges over a floor its strong heartbeats run along (jump over their waves).
"""

import raid_skullmaw as K

FLOOR = K.FLOOR


def lair(A, spec):
    T, tr = A.T, A.tr
    F = FLOOR
    z, ax0, ax1 = K.shell(A, spec,
                          "L'antre de Heartbreaker, contre le cœur du titan, assez grand pour huit joueurs : quatre plaques à briser ensemble, des "
                          "battements en rythme, des côtes à écarter des deux côtés à la fois.",
                          "Heartbreaker's lair, against the titan's heart, big enough for eight players: four plates to break together, beats in "
                          "rhythm, ribs to pry open from both sides at once.",
                          "Heartbreaker, la cage qui garde le cœur. Brisez ses plaques ensemble, sautez ses ondes, écartez ses côtes des deux côtés.",
                          "Heartbreaker, the cage that guards the heart. Break its plates together, jump its waves, pry its ribs from both sides.",
                          "music/singularis_boss.ogg")
    L = z.L
    for a, b in ((76, 88), (112, 124)):  # the flank towers, against its ribs
        for row in (F - 6, F - 12, F - 18):
            z.ledge(ax0 + a, ax0 + b, row)
    rows = {F - 6: (8, 34, 56, 136, 158, 182), F - 12: (20, 46, 146, 172), F - 18: (8, 34, 56, 136, 158, 182),
            F - 24: (20, 46, 92, 146, 172), F - 30: (34, 72, 118, 156)}
    for row, xs in sorted(rows.items()):
        for px in xs:
            z.ledge(ax0 + px, ax0 + px + 10, row)
    ribs = K.places(A, [("cote_ouest", ax0 + 85, F - 12), ("cote_est", ax0 + 115, F - 12)])
    for r in ribs:
        cx, cy = (r["rect"][0] + r["rect"][2] / 2) / T, r["rect"][1] / T
        L.light(cx, cy - 1, "#ff5d8f", 80, flicker=0.2)
    orbs = K.hidden_orbs(z, {2: [(ax0 + 20, F - 8), (ax0 + 180, F - 8), (ax0 + 100, F - 26)],
                             3: [(ax0 + 40, F - 20), (ax0 + 160, F - 20), (ax0 + 82, F - 20), (ax0 + 118, F - 20)]})
    boss = L.enemy("heartbreaker", ax0 + 100, F, patrol_tiles=0, face_left=True, dormant=True, boss=True)
    L.combat(ax0, 6, ax1, F, waves=boss, lock=False, music="music/singularis_boss.ogg")
    L.data["objects"][-1]["props"]["announce"] = False
    z.objective("torse_heartbreaker", boss, "killed")
    K.boss_events(A, z, boss, orbs,
                  ("Les côtes se referment sur le cœur : écartez-les des deux côtés à la fois !",
                   "The ribs close over the heart: pry them open from both sides at once!"),
                  ("Le cœur s'affole : ses battements s'accélèrent, la cage se brise !",
                   "The heart races: its beats quicken, the cage breaks!"),
                  ("Heartbreaker est vaincu : la cage du torse s'ouvre, la voie du cœur d'Asterion est libre.",
                   "Heartbreaker is defeated: the torso's cage opens, the way to Asterion's heart is free."),
                  ("Heartbreaker vaincu", "Heartbreaker defeated"))
    z.raid["gigaboss"] = dict(K.SCALING, **{
        # Titan 13 (the nerf, docs/RAIDS.md §37): its definition's health × hpScale in the raid (3 / 5).
        "hpScale": 0.20,
        "boss": boss, "name": "Heartbreaker", "objective": "torse_heartbreaker", "routine": "heartbreaker",
        # « Côtes-pièges »: ribs rise around each player in play and snap shut (the rules' fists).
        "fists": {"name": tr("Côtes-pièges", "Rib traps"), "periods": [9.0, 7.5, 6.0], "warn": 1.3, "width": 112.0, "damage": 1.0, "rise": True},
        # « La cuirasse » (phase 1): its four plates, to break within `window` seconds of the first (else they grow back).
        "plates": {"enemy": "heartbreaker_plaque", "spots": [[-150.0, -248.0], [150.0, -248.0], [-160.0, -78.0], [160.0, -78.0]],
                   "window": 30.0, "share": 0.05},
        # « Battements »: a beat every period (by phase); the strong ones (every Nth) send a wave along the floor.
        "beats": {"periods": [2.0, 1.6, 1.25], "strong": [4, 3, 2], "speed": 520.0, "height": 40.0, "damage": 1.0,
                  "floor": F * T, "from": [-170.0, 170.0], "span": [ax0 * T, ax1 * T]},
        # « La cage thoracique » (phase 2): its two ribs held open together bare the light of its heart.
        "carapace": [2, 3], "vents": ribs, "hold": 2.5, "reopen": 12.0, "exposed": 16.0, "exposedMult": 3.0,
        "jet": {"name": tr("Jet de plasma", "Plasma jet"), "period": 8.0, "warn": 1.2, "duration": 0.8, "height": 96.0, "damage": 1.0},
        "weakEnemy": "heartbreaker_coeur", "weakSpots": [[0.0, -148.0]], "weakShare": 0.08,
        "fx": "animations/heartbreaker_fx.json",
        "fallText": tr("La cage du torse s'ouvre : la voie du cœur d'Asterion est libre.", "The torso's cage opens: the way to Asterion's heart is free."),
        "goneText": tr("La cage est ouverte", "The cage is open"),
        "summary": tr("Brisez ses 4 plaques ensemble (plusieurs groupes), puis écartez ses côtes des deux côtés à la fois pour frapper son cœur.",
                      "Break its 4 plates together (several groups), then pry its ribs open from both sides at once to strike its heart."),
    })
    L.emitter(ax0 + 20, 10, ax1 - 20, F, spec.theme["dust"], 2.0)
    # Titan 13: more footholds on every storey, none against the torso (its towers stay the way to its ribs and plates).
    z.L.footholds(ax0 + 2, ax1 - 2, [F - 6 * k for k in range(1, 6)], z.th["ledge"], min_gap=16,
                  keep=[(ax0 + 66, 6, ax0 + 134, F)])
    return z.save()
