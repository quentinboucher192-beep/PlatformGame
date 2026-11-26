"""2.69 (phase 4B): the lair of TITANUS, zone 8 of the JAMBE DROITE (docs/RAIDS.md §13), unsealed.

Called by raid_asterion.py (jambe_droite()) with that module itself (`A`), never run alone. The arena
keeps the format of the seven lairs (metadata.raid.arena) and adds the fight (metadata.raid.gigaboss,
its routine "titanus", game/raid/TitanusRoutine.cpp):

* a long floor for its shockwaves (jumped over; the grates of the five storeys are safe);
* the forge under the middle of the floor: its crust cracks at the 2nd phase (the floor gives way to
  the lava, grates bridge it);
* the vault its boulders fall from.
"""

ARENA_W, FLOOR, H8 = 200, 62, 72


def lair(A, spec):
    T, tr = A.T, A.tr
    z = A.Zone(spec, 8, "Le talon de Titanus, au bout de la jambe droite : un colosse de forge qui écrase tout. Trois phases : ses ondes de choc, "
                        "son talon sur chacun, puis la forge à nu ; ses deux entraves se brisent ensemble pour le mettre à genou.",
               "Titanus's heel, at the end of the right leg: a colossus of the forge that crushes everything. Three phases: its shockwaves, "
               "its heel on everyone, then the bare forge; its two restraints break together to bring it to its knee.", height=H8)
    L, th = z.L, spec.theme
    L.data["bossMusic"] = "music/titan_boss.ogg"
    z.west(low=7, y=FLOOR)
    x0 = z.flat(20)
    L.sign(x0 + 4, FLOOR, tr("Titanus, la jambe qui écrase tout. Sautez ses ondes de choc ; brisez ses deux entraves en même temps pour le mettre à genou.",
                             "Titanus, the leg that crushes everything. Jump its shockwaves; break its two restraints at the same time to bring it to its knee."))
    L.light(x0 + 10, FLOOR - 6, "#ffd27a", 140, flicker=0.3)
    ax0 = z.x
    ax1 = ax0 + ARENA_W
    # The floor, the forge under its middle (a crust that cracks at the 2nd phase).
    f0, f1 = ax0 + 82, ax0 + 118
    z.rock(ax0, FLOOR, f0)
    z.rock(f0, FLOOR + 4, f1)
    z.rock(f1, FLOOR, ax1 + 2)
    crust = []
    for cx in range(f0, f1, 4):
        oid = L.crumble(cx, FLOOR, min(4, f1 - cx), delay=0.3, respawn=0.0, style="basalt")
        L.data["objects"][-1]["props"]["onEvent"] = True
        crust.append(oid)
    lava = L.obj("damage_zone", [f0 * T, (FLOOR + 2) * T, (f1 - f0) * T, 2 * T], "gameplay",
                 {"damage": 2, "interval": 0.6, "visible": True, "shieldProtects": True, "active": False, "style": "embers", "bounce": True})
    z.ledge(f0 + 4, f0 + 14, FLOOR - 4)
    z.ledge(f1 - 14, f1 - 4, FLOOR - 4)
    z.slab(ax0, 4, ax1, 6)
    z.slab(ax1, 4, ax1 + 2, FLOOR)
    z.slab(ax0 - 2, 4, ax0, FLOOR - 12)
    for k, row in enumerate((FLOOR - 7, FLOOR - 13, FLOOR - 19, FLOOR - 25, FLOOR - 31)):
        for px in range(ax0 + 8 + (12 if k % 2 else 0), ax1 - 12, 26):
            z.ledge(px, px + 10, row)
    boss = L.enemy("titanus", ax0 + 140, FLOOR, patrol_tiles=40, face_left=True, dormant=True, boss=True)
    L.combat(ax0 + 1, 6, ax1, FLOOR, waves=boss, lock=False, music="music/titan_boss.ogg")
    L.data["objects"][-1]["props"]["announce"] = False
    z.objective("jd_titanus", boss, "killed")
    orbs = {}
    for ph, ox, oy in ((2, ax0 + 20, FLOOR - 9), (2, ax0 + 180, FLOOR - 9), (2, ax0 + 100, FLOOR - 15), (3, ax0 + 34, 29), (3, ax0 + 166, 29),
                       (3, ax0 + 60, FLOOR - 15), (3, ax0 + 140, FLOOR - 15)):
        L.orb("health_orb", ox, oy)
        L.data["objects"][-1]["props"] = {"amount": 4, "hidden": True}
        orbs.setdefault(ph, []).append(L.data["objects"][-1]["id"])
    L.event(boss, "bossPhase", [{"type": "activate", "target": ",".join(crust)}, {"type": "enable", "target": lava},
                                {"type": "spawn", "target": ",".join(orbs[2])}, {"type": "shake", "params": {"amount": 0.9}},
                                {"type": "hint", "params": {"text": tr("Son talon fend le sol : la forge s'ouvre sous l'arène, ses entraves se verrouillent !",
                                                                       "Its heel splits the floor: the forge opens under the arena, its restraints lock!"),
                                                            "duration": 3.5}}],
            params={"phase": 2}, name=tr("Les entraves", "The restraints"))
    L.event(boss, "bossPhase", [{"type": "spawn", "target": ",".join(orbs[3])}, {"type": "shake", "params": {"amount": 1.0}},
                                {"type": "hint", "params": {"text": tr("Son blindage se fend : ses ondes de choc vont par deux !",
                                                                       "Its armour splits: its shockwaves come in pairs!"),
                                                            "duration": 3.5}}],
            params={"phase": 3}, name=tr("La forge à nu", "The bare forge"))
    L.event(boss, "bossDefeated", [{"type": "hint", "params": {"text": tr("Titanus est vaincu : la jambe droite d'Asterion chancelle.",
                                                                          "Titanus is defeated: Asterion's right leg staggers."), "duration": 4}},
                                   {"type": "disable", "target": lava}, {"type": "shake", "params": {"amount": 0.8}}],
            delay=1.5, name=tr("Titanus vaincu", "Titanus defeated"))
    z.raid["arena"] = {"rect": [ax0 * T, 6 * T, ARENA_W * T, (FLOOR - 6) * T], "floor": FLOOR * T, "door": "",
                       "entry": [(ax0 + 6) * T, FLOOR * T], "players": 8}
    z.raid["gigaboss"] = {
        # Titan 13 (the nerf, docs/RAIDS.md §37): its definition's health × hpScale in the raid (3 / 5).
        # Titan 15 (« allège un peu », docs/RAIDS.md §38): 0.40 → 0.34, its blows 1 s further apart, 18 s on its knee.
        "hpScale": 0.34,
        "boss": boss, "name": "Titanus", "objective": "jd_titanus", "routine": "titanus", "carapace": [2, 4],
        "hold": 3.0, "reopen": 15.0, "exposed": 18.0, "exposedMult": 2.0,
        "fists": {"name": tr("Écrasement", "Crushing heel"), "periods": [9.5, 8.0, 6.5], "warn": 1.5, "width": 96.0, "damage": 2.0},
        "weakEnemy": "titanus_genou", "weakSpots": [[50.0, -260.0], [-60.0, -110.0], [70.0, -110.0]], "weakShare": 0.04,
        "hpPerPlayer": 0.55, "weakHpPerPlayer": 0.25, "pressurePerPlayer": 0.06,
        "waves": {"periods": [11.0, 9.0, 7.5], "warn": 0.9, "speed": 430.0, "height": 40.0, "width": 26.0, "damage": 1.0, "double": 3,
                  "first": 6.0, "floor": FLOOR * T, "left": ax0 * T, "right": ax1 * T},
        "boulders": {"periods": [0.0, 10.0, 8.0], "count": [0, 6, 9], "warn": 1.3, "damage": 1.0, "top": 6 * T},
        "restraints": {"enemy": "titanus_entrave", "spots": [[-70.0, -150.0], [70.0, -150.0]], "window": 3.0, "reactors": 1.5},
        "links": {"west": "jd_reacteur_ouest", "east": "jd_reacteur_est"},
        "fx": "animations/titanus_fx.json",
        "fallText": tr("La jambe droite d'Asterion chancelle : ses charges ralentissent.", "Asterion's right leg staggers: its charges slow down."),
        "goneText": tr("Le talon est à terre", "The heel is down"),
        "summary": tr("Sautez ses ondes de choc, brisez ses 2 entraves en même temps (moins de 1,5 s d'écart) pour le mettre à genou.",
                      "Jump its shockwaves, break its 2 restraints at the same time (less than 1.5 s apart) to bring it to its knee."),
    }
    for px in range(ax0 + 10, ax1, 30):
        L.light(px, FLOOR - 20, th["glow"], 160, flicker=0.2)
    L.emitter(f0, FLOOR - 2, f1, FLOOR, "embers_rising", 2.5)
    L.emitter(ax0 + 20, 8, ax1 - 20, FLOOR, "embers_rising", 1.5)
    L.decor(th["deco_tall"][0], x0 + 2, FLOOR)
    L.decor(th["deco_tall"][1], ax0 + 40, FLOOR, alpha=0.8)
    L.decor(th["deco_tall"][2], ax0 + 160, FLOOR, alpha=0.8)
    L.orb("health_orb", x0 + 14, FLOOR - 2)
    z.x = ax1 + 2
    # Titan 13: more footholds on every storey (none under the forge's ledges: its shock waves still run along the floor).
    z.L.footholds(ax0 + 2, ax1 - 2, [FLOOR - 1 - 6 * k for k in range(1, 6)], z.th["ledge"], min_gap=16)
    return z.save()
