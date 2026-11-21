"""2.69 (phase 4B): the lair of GRAVITON, zone 8 of the JAMBE GAUCHE (docs/RAIDS.md §13), unsealed.

Called by raid_asterion.py (jambe_gauche()) with that module itself (`A`), never run alone. The arena
keeps the format of the seven lairs (metadata.raid.arena) and adds the fight (metadata.raid.gigaboss,
its routine "graviton", game/raid/GravitonRoutine.cpp):

* four sectors of 50 tiles (their edges in metadata: gravity turns over sector by sector, half the
  arena in its 3rd phase); in each, two slabs high up shelter whoever falls toward the vault;
* its four gravity generators: two on the floor at both ends, two on high islands of the middle
  sectors (reached by the ledges): one player on each, all at the same time;
* ruins and ledges on five storeys, for the debris falling on each player and its wells.
"""

ARENA_W, FLOOR, H8 = 200, 62, 72


def lair(A, spec):
    T, tr = A.T, A.tr
    z = A.Zone(spec, 8, "Le pied de Graviton, au bout de la jambe gauche : la jambe flotte dans ses générateurs de gravité. Trois phases : elle plie "
                        "l'espace par secteurs, ouvre un puits sous chacun, puis retourne la moitié de l'arène ; ses générateurs se coupent tous ensemble.",
               "Graviton's foot, at the end of the left leg: the leg floats in its gravity generators. Three phases: it bends space by sectors, "
               "opens a well under everyone, then turns half the arena over; its generators are cut all together.", height=H8)
    L, th = z.L, spec.theme
    L.data["bossMusic"] = "music/singularis_boss.ogg"
    z.west(low=7, y=FLOOR)
    x0 = z.flat(20)
    L.sign(x0 + 4, FLOOR, tr("Graviton, la jambe qui plie l'espace. Ses quatre générateurs se coupent ensemble : un joueur sur chacun, au même moment.",
                             "Graviton, the leg that bends space. Its four generators are cut together: one player on each, at the same moment."))
    L.light(x0 + 10, FLOOR - 6, "#ffd27a", 140, flicker=0.3)
    ax0 = z.x
    ax1 = ax0 + ARENA_W
    z.rock(ax0, FLOOR, ax1 + 2)
    z.slab(ax0, 4, ax1, 6)
    z.slab(ax1, 4, ax1 + 2, FLOOR)
    z.slab(ax0 - 2, 4, ax0, FLOOR - 12)
    edges = [ax0 + 50 * k for k in range(5)]
    # Five storeys of ruined bridges, staggered.
    for k, row in enumerate((FLOOR - 6, FLOOR - 12, FLOOR - 18, FLOOR - 24, FLOOR - 30)):
        for px in range(ax0 + 6 + (13 if k % 2 else 0), ax1 - 10, 26):
            z.ledge(px, px + 10, row)
    # The shelters under the vault (two slabs a sector) and the islands of the high generators.
    for k in range(4):
        e = edges[k]
        z.slab(e + 8, 20, e + 18, 22, th["alt"])
        z.slab(e + 32, 20, e + 42, 22, th["alt"])
    gens = [("generateur_ouest", ax0 + 25, FLOOR), ("generateur_haut_ouest", ax0 + 75, 28), ("generateur_haut_est", ax0 + 125, 28),
            ("generateur_est", ax0 + 175, FLOOR)]
    for _, gx, gy in gens:
        if gy != FLOOR:
            z.slab(gx - 6, gy, gx + 6, gy + 2, th["ground"])
            z.ledge(gx - 14, gx - 6, gy + 4)
            z.ledge(gx + 6, gx + 14, gy + 4)
        L.light(gx, gy - 3, th["glow"], 100, flicker=0.2)
        L.emitter(gx - 2, gy - 4, gx + 2, gy, "aurora_motes", 1.5)
    boss = L.enemy("graviton", ax0 + 120, FLOOR - 16, patrol_tiles=30, face_left=True, dormant=True, boss=True)
    L.combat(ax0 + 1, 6, ax1, FLOOR, waves=boss, lock=False, music="music/singularis_boss.ogg")
    L.data["objects"][-1]["props"]["announce"] = False
    z.objective("jg_graviton", boss, "killed")
    orbs = {}
    for ph, ox, oy in ((2, ax0 + 25, FLOOR - 8), (2, ax0 + 175, FLOOR - 8), (2, ax0 + 100, 36), (3, ax0 + 75, 26), (3, ax0 + 125, 26),
                       (3, ax0 + 13, 18), (3, ax0 + 187, 18)):
        L.orb("health_orb", ox, oy)
        L.data["objects"][-1]["props"] = {"amount": 4, "hidden": True}
        orbs.setdefault(ph, []).append(L.data["objects"][-1]["id"])
    L.event(boss, "bossPhase", [{"type": "spawn", "target": ",".join(orbs[2])}, {"type": "shake", "params": {"amount": 0.8}},
                                {"type": "hint", "params": {"text": tr("Ses générateurs s'allument : un joueur sur chacun, tous en même temps !",
                                                                       "Its generators light up: one player on each, all at the same time!"),
                                                            "duration": 3.5}}],
            params={"phase": 2}, name=tr("Les générateurs", "The generators"))
    L.event(boss, "bossPhase", [{"type": "spawn", "target": ",".join(orbs[3])}, {"type": "shake", "params": {"amount": 1.0}},
                                {"type": "hint", "params": {"text": tr("Le haut et le bas s'échangent : la moitié de l'arène va tomber vers la voûte !",
                                                                       "Up and down swap: half the arena is about to fall toward the vault!"),
                                                            "duration": 3.5}}],
            params={"phase": 3}, name=tr("Le monde à l'envers", "The world upside down"))
    L.event(boss, "bossDefeated", [{"type": "hint", "params": {"text": tr("Graviton est vaincu : la jambe gauche d'Asterion retombe.",
                                                                          "Graviton is defeated: Asterion's left leg falls."), "duration": 4}},
                                   {"type": "shake", "params": {"amount": 0.6}}], delay=1.5, name=tr("Graviton vaincu", "Graviton defeated"))
    z.raid["arena"] = {"rect": [ax0 * T, 6 * T, ARENA_W * T, (FLOOR - 6) * T], "floor": FLOOR * T, "door": "",
                       "entry": [(ax0 + 6) * T, FLOOR * T], "players": 8}
    z.raid["gigaboss"] = {
        # Titan 13 (the nerf, docs/RAIDS.md §37): its definition's health × hpScale in the raid (3 / 5).
        "hpScale": 0.48,
        "boss": boss, "name": "Graviton", "objective": "jg_graviton", "routine": "graviton", "carapace": [2, 4],
        "hold": 2.5, "reopen": 15.0, "exposed": 12.0, "exposedMult": 2.0,
        "fists": {"name": tr("Chute de débris", "Falling debris"), "periods": [8.5, 7.5, 6.0], "warn": 1.4, "width": 80.0, "damage": 1.0},
        "weakEnemy": "graviton_coeur", "weakSpots": [[-30.0, -70.0], [40.0, -170.0], [-10.0, -280.0]], "weakShare": 0.04,
        "hpPerPlayer": 0.55, "weakHpPerPlayer": 0.25, "pressurePerPlayer": 0.06,
        "generators": [{"id": gid, "rect": [(gx - 1.5) * T, (gy - 1) * T, 3 * T, 1 * T]} for gid, gx, gy in gens],
        "sectors": {"edges": [e * T for e in edges], "top": 6 * T, "floor": FLOOR * T, "band": 40.0, "lift": 300.0,
                    "periods": [12.0, 10.5, 9.0], "time": [4.5, 5.0, 6.0], "warn": 2.0, "first": 10.0},
        "wells": {"periods": [0.0, 11.0, 9.0], "warn": 1.2, "time": 3.0, "speed": 90.0, "range": 240.0, "core": 30.0, "damage": 1.0},
        "fall": FLOOR * T,
        "links": {"west": "jg_generateur_ouest", "east": "jg_generateur_est"},
        "fx": "animations/graviton_fx.json",
        "fallText": tr("La jambe gauche d'Asterion retombe : plus de pièges de gravité.", "Asterion's left leg falls: no more gravity traps."),
        "goneText": tr("Le pied est retombé", "The foot has fallen"),
        "summary": tr("Coupez ses générateurs tous en même temps (un joueur sur chacun) ; abritez-vous sous une dalle quand la gravité s'inverse.",
                      "Cut its generators all at the same time (one player on each); shelter under a slab when gravity turns over."),
    }
    for px in range(ax0 + 10, ax1, 30):
        L.light(px, FLOOR - 20, th["glow"], 160, flicker=0.2)
    for e in edges[1:-1]:
        L.decor(th["deco_tall"][0], e, FLOOR, alpha=0.8)
    L.emitter(ax0 + 20, 8, ax1 - 20, FLOOR, "aurora_motes", 2.0)
    L.decor(th["deco_tall"][0], x0 + 2, FLOOR)
    L.orb("health_orb", x0 + 14, FLOOR - 2)
    z.x = ax1 + 2
    # Titan 13: more footholds on every storey of bridges, none by the islands of the high generators.
    z.L.footholds(ax0 + 2, ax1 - 2, [FLOOR - 6 * k for k in range(1, 6)], z.th["ledge"], min_gap=16,
                  keep=[(gx - 18, gy - 4, gx + 18, gy + 7) for _, gx, gy in gens if gy != FLOOR])
    return z.save()
