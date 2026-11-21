"""2.69 (phase 4B): the lair of DREADFANG, zone 8 of the BRAS GAUCHE (docs/RAIDS.md §13), unsealed.

Called by raid_asterion.py (bras_gauche()) with that module itself (`A`), never run alone. The arena
(raid_skullmaw.shell: the format of the seven lairs) and the fight (metadata.raid.gigaboss, its routine
"dreadfang", game/raid/DreadfangRoutine):

* six storeys of long ledges (its claws sweep whole storeys: jump over the blade or change storey),
  some of them shredded at its 3rd phase (crumbling platforms set off by its « bossPhase »);
* the three anchors of its chains (the rules' vents): the two ends of the floor and a high platform in
  the middle, so that the group must split to hold them together.
"""

import raid_skullmaw as K

FLOOR = K.FLOOR


def lair(A, spec):
    T, tr = A.T, A.tr
    F = FLOOR
    z, ax0, ax1 = K.shell(A, spec,
                          "Les griffes de Dreadfang, au bout du bras gauche, assez grandes pour huit joueurs : elles balaient des étages entiers, "
                          "clouent un joueur au sol, et ses trois chaînes se tiennent ensemble.",
                          "Dreadfang's claws, at the end of the left arm, big enough for eight players: they sweep whole storeys, nail a player "
                          "to the ground, and its three chains are held together.",
                          "Dreadfang, les griffes qui déchirent. Sautez ses lames, libérez qui est cloué, tenez ses trois chaînes ensemble.",
                          "Dreadfang, the tearing claws. Jump its blades, free whoever is nailed, hold its three chains together.",
                          "music/glacior_boss.ogg")
    L = z.L
    shred = []
    storeys = {F - 6: (8, 42, 76, 110, 144, 178), F - 12: (24, 58, 94, 128, 162), F - 18: (8, 42, 76, 110, 144, 178),
               F - 24: (24, 58, 128, 162)}
    for row, xs in sorted(storeys.items()):
        for px in xs:
            w = 14 if px + 14 <= 196 else 196 - px
            if (row, px) in ((F - 12, 58), (F - 12, 128), (F - 18, 42), (F - 18, 144)):
                for cx in range(ax0 + px, ax0 + px + w, 4):
                    shred.append(L.crumble(cx, row, min(4, ax0 + px + w - cx), delay=0.3, respawn=0.0, style="ice"))
                    L.data["objects"][-1]["props"]["onEvent"] = True
            else:
                z.ledge(ax0 + px, ax0 + px + w, row)
    z.ledge(ax0 + 92, ax0 + 108, F - 24)   # the platform of the high anchor
    z.ledge(ax0 + 94, ax0 + 106, F - 30)
    for row, xs in ((F - 30, (10, 44, 130, 166)), (F - 36, (26, 60, 120, 154))):  # the high storeys, out of the sweeps' reach
        for px in xs:
            z.ledge(ax0 + px, ax0 + px + 10, row)
    anchors = K.places(A, [("ancre_ouest", ax0 + 12, F), ("ancre_haute", ax0 + 100, F - 24), ("ancre_est", ax0 + 188, F)])
    for a in anchors:
        cx, cy = (a["rect"][0] + a["rect"][2] / 2) / T, a["rect"][1] / T
        L.light(cx, cy - 1, "#7ff6ff", 80, flicker=0.15)
    orbs = K.hidden_orbs(z, {2: [(ax0 + 12, F - 3), (ax0 + 188, F - 3), (ax0 + 100, F - 27)],
                             3: [(ax0 + 30, F - 9), (ax0 + 170, F - 9), (ax0 + 100, F - 33), (ax0 + 66, F - 21)]})
    boss = L.enemy("dreadfang", ax0 + 150, F - 16, patrol_tiles=40, face_left=True, dormant=True, boss=True)
    L.combat(ax0, 6, ax1, F, waves=boss, lock=False, music="music/glacior_boss.ogg")
    L.data["objects"][-1]["props"]["announce"] = False
    z.objective("bg_dreadfang", boss, "killed")
    K.boss_events(A, z, boss, orbs,
                  ("Une armure de givre le couvre : tenez ses trois chaînes ensemble pour l'enchaîner !",
                   "Frost armour covers it: hold its three chains together to chain it down!"),
                  ("Ses griffes déchaînées tranchent les plateformes : changez d'étage !",
                   "Its unleashed claws shred the platforms: change storeys!"),
                  ("Dreadfang est vaincu : le bras gauche d'Asterion retombe, ses griffes arrachées.",
                   "Dreadfang is defeated: Asterion's left arm falls, its claws torn off."),
                  ("Dreadfang vaincu", "Dreadfang defeated"),
                  extra3=[{"type": "activate", "target": ",".join(shred)}])
    z.raid["gigaboss"] = dict(K.SCALING, **{
        # Titan 13 (the nerf, docs/RAIDS.md §37): its definition's health × hpScale in the raid (3 / 5).
        "hpScale": 0.85,
        "boss": boss, "name": "Dreadfang", "objective": "bg_dreadfang", "routine": "dreadfang",
        # « Griffes du sol »: under each player in play, claws of ice burst out of the ground (the rules' fists).
        "fists": {"name": tr("Griffes du sol", "Ground claws"), "periods": [8.0, 6.5, 5.0], "warn": 1.2, "width": 56.0, "damage": 1.0, "rise": True},
        # « Balayage des griffes »: a blade across whole storeys (their surfaces in pixels), low: jump over it.
        "sweep": {"periods": [11.0, 9.0, 7.5], "warn": 1.6, "speed": 1500.0, "height": 34.0, "damage": 2.0,
                  "storeys": [F * T, (F - 6) * T, (F - 12) * T, (F - 18) * T, (F - 24) * T], "count": [1, 2, 3],
                  "span": [ax0 * T, ax1 * T]},
        # « Griffe clouante » (phases 2 and 3): it nails a player to the ground; the others break the claw.
        "pin": {"from": 2, "period": 15.0, "warn": 1.2, "reach": 56.0, "hold": 8.0, "every": 2.5, "damage": 1.0, "enemy": "dreadfang_griffe"},
        # « Le bras enchaîné » (phase 2): its frost armour holds until its three chains are held together.
        "carapace": [2, 3], "vents": anchors, "hold": 2.5, "reopen": 18.0, "exposed": 10.0, "exposedMult": 2.0,
        "jet": {"name": tr("Éclats de givre", "Frost shards"), "period": 7.0, "warn": 1.2, "duration": 0.8, "height": 112.0, "damage": 1.0},
        "weakEnemy": "dreadfang_jointure", "weakSpots": [[70.0, -151.0], [100.0, -156.0], [130.0, -141.0]], "weakShare": 0.04,
        "fx": "animations/dreadfang_fx.json",
        "fallText": tr("Le bras gauche d'Asterion retombe, ses griffes arrachées.", "Asterion's left arm falls, its claws torn off."),
        "goneText": tr("Les griffes sont froides", "The claws are cold"),
        "summary": tr("Son armure de givre : tenez ses 3 chaînes ensemble pour le plaquer au sol, brisez ses jointures, libérez les joueurs cloués.",
                      "Its frost armour: hold its 3 chains together to pin it to the floor, break its joints, free the nailed players."),
    })
    L.emitter(ax0 + 20, 10, ax1 - 20, F, spec.theme["dust"], 2.0)
    # Titan 13: more footholds on every storey (the sweeps still cross whole storeys: jump the blade).
    z.L.footholds(ax0 + 2, ax1 - 2, [F - 6 * k for k in range(1, 7)], z.th["ledge"])
    return z.save()
