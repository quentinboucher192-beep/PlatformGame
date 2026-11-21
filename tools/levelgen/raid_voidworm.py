"""2.69 (phase 4B): the lair of VOIDWORM, zone 8 of the ABDOMEN (docs/RAIDS.md §13), unsealed.

Called by raid_asterion.py (abdomen()) with that module itself (`A`: its Zone, tr, T…), never run
alone. The arena keeps the format of the seven lairs (metadata.raid.arena: 200 tiles wide, floor at
row 62, five storeys of ledges) and adds the fight (metadata.raid.gigaboss, its routine "voidworm",
game/raid/VoidwormRoutine.cpp):

* the worm rears out of the middle of the arena (it splits the group in two halves);
* its three hooks: one on each side of the floor, one on the high crown over its head (reached by
  the ledges): three players must hold them together to pin it;
* its stomach: a closed pocket under the floor, where the players it swallows wait to be freed;
* its parasites come out of four holes along the floor; its acid tide spares only the ledges from
  the second storey up (the first one too once the Intestine's two valves are closed).
"""

ARENA_W, FLOOR, H8 = 200, 62, 72


def lair(A, spec):
    T, tr = A.T, A.tr
    z = A.Zone(spec, 8, "L'antre de Voidworm, au fond de l'abdomen : le ver du vide surgit de son gouffre. Trois phases : il fouit sous chacun, "
                        "avale ceux qu'il vise, puis l'acide monte ; dans sa chitine, ses trois crochets doivent être tenus ensemble.",
               "Voidworm's lair, at the bottom of the abdomen: the worm of the void bursts out of its pit. Three phases: it burrows under "
               "everyone, swallows those it aims at, then the acid rises; in its chitin, its three hooks must be held together.", height=H8)
    L, th = z.L, spec.theme
    L.data["bossMusic"] = "music/mycelia_boss.ogg"
    z.west(low=7, y=FLOOR)
    x0 = z.flat(20)
    L.sign(x0 + 4, FLOOR, tr("Voidworm, le ver du vide. Tenez ses trois crochets ensemble pour l'épingler ; crevez sa poche pour libérer un avalé.",
                             "Voidworm, the worm of the void. Hold its three hooks together to pin it; burst its stomach to free a swallowed ally."))
    L.light(x0 + 10, FLOOR - 6, "#ffd27a", 140, flicker=0.3)
    ax0 = z.x
    ax1 = ax0 + ARENA_W
    # The floor, and its stomach under it (a closed pocket: roof, walls and bottom of rock).
    s0, s1 = ax0 + 58, ax0 + 74
    z.rock(ax0, FLOOR, s0)
    z.slab(s0, FLOOR, s1, FLOOR + 2, th["ground"])
    z.slab(s0, FLOOR + 7, s1, H8, th["ground"])
    z.rock(s1, FLOOR, ax1 + 2)
    L.emitter(s0, FLOOR + 3, s1, FLOOR + 7, "acid_bubbles", 3.0)
    L.light((s0 + s1) // 2, FLOOR + 5, th["glow"], 90, flicker=0.3)
    z.slab(ax0, 4, ax1, 6)                # the vault
    z.slab(ax1, 4, ax1 + 2, FLOOR)        # the far wall
    z.slab(ax0 - 2, 4, ax0, FLOOR - 12)   # the arch over the way in
    # Five storeys of roots, staggered (the high ones stay dry when the acid rises).
    for k, row in enumerate((FLOOR - 6, FLOOR - 12, FLOOR - 18, FLOOR - 24, FLOOR - 30)):
        for px in range(ax0 + 8 + (12 if k % 2 else 0), ax1 - 12, 26):
            if ax0 + 88 <= px <= ax0 + 106 and k < 3:
                continue  # never inside the worm
            z.ledge(px, px + 10, row)
    # The crown over its head, and its hook. Titan 13: over its body (its top at row 25), no longer in it: a player
    # holds the hook out of its contact, reached by two steps a side from the high roots (plain jumps).
    cx = ax0 + 100
    z.ledge(cx - 10, cx + 10, 22)
    z.ledge(cx - 22, cx - 12, 25)
    z.ledge(cx + 12, cx + 22, 25)
    z.ledge(cx - 30, cx - 22, 28)
    z.ledge(cx + 22, cx + 30, 28)
    boss = L.enemy("voidworm", cx, FLOOR, patrol_tiles=0, face_left=True, dormant=True, boss=True)
    L.combat(ax0 + 1, 6, ax1, FLOOR, waves=boss, lock=False, music="music/mycelia_boss.ogg")
    L.data["objects"][-1]["props"]["announce"] = False
    z.objective("abd_voidworm", boss, "killed")
    hooks = [("crochet_ouest", ax0 + 30, FLOOR), ("crochet_couronne", cx, 22), ("crochet_est", ax0 + 170, FLOOR)]
    vents = []
    for vid, vx, vy in hooks:
        vents.append({"id": vid, "rect": [(vx - 1.5) * T, (vy - 1) * T, 3 * T, 1 * T]})
        L.light(vx, vy - 2, th["glow"], 80, flicker=0.25)
    holes = [ax0 + 16, ax0 + 50, ax0 + 150, ax0 + 186]
    for hx in holes:
        L.emitter(hx - 2, FLOOR - 2, hx + 2, FLOOR, "spores", 1.5)
    orbs = {}
    for ph, ox, oy in ((2, ax0 + 30, FLOOR - 8), (2, ax0 + 170, FLOOR - 8), (2, cx, 20), (3, ax0 + 20, 36), (3, ax0 + 180, 36), (3, cx - 16, 23),
                       (3, cx + 16, 23)):
        L.orb("health_orb", ox, oy)
        L.data["objects"][-1]["props"] = {"amount": 4, "hidden": True}
        orbs.setdefault(ph, []).append(L.data["objects"][-1]["id"])
    L.event(boss, "bossPhase", [{"type": "spawn", "target": ",".join(orbs[2])}, {"type": "shake", "params": {"amount": 0.8}},
                                {"type": "hint", "params": {"text": tr("Voidworm se love dans sa chitine : tenez ses trois crochets ensemble !",
                                                                       "Voidworm coils in its chitin: hold its three hooks together!"),
                                                            "duration": 3.5}}],
            params={"phase": 2}, name=tr("Les anneaux de chitine", "The rings of chitin"))
    L.event(boss, "bossPhase", [{"type": "spawn", "target": ",".join(orbs[3])}, {"type": "shake", "params": {"amount": 1.0}},
                                {"type": "hint", "params": {"text": tr("Le ventre se retourne : l'acide va monter, grimpez sur les racines !",
                                                                       "The belly turns over: the acid is about to rise, climb the roots!"),
                                                            "duration": 3.5}}],
            params={"phase": 3}, name=tr("Le reflux", "The reflux"))
    L.event(boss, "bossDefeated", [{"type": "hint", "params": {"text": tr("Voidworm est vaincu : le ventre d'Asterion se tait.",
                                                                          "Voidworm is defeated: Asterion's belly falls silent."), "duration": 4}},
                                   {"type": "shake", "params": {"amount": 0.6}}], delay=1.5, name=tr("Voidworm vaincu", "Voidworm defeated"))
    # The arena (the format of the seven lairs: the arena camera reads it) and the fight.
    z.raid["arena"] = {"rect": [ax0 * T, 6 * T, ARENA_W * T, (FLOOR - 6) * T], "floor": FLOOR * T, "door": "",
                       "entry": [(ax0 + 6) * T, FLOOR * T], "players": 8}
    z.raid["gigaboss"] = {
        # Titan 13 (the nerf, docs/RAIDS.md §37): its definition's health × hpScale in the raid (3 / 5).
        "hpScale": 0.35,
        "boss": boss, "name": "Voidworm", "objective": "abd_voidworm", "routine": "voidworm", "vents": vents, "carapace": [2, 3],
        "hold": 2.0, "reopen": 30.0, "exposed": 14.0, "exposedMult": 2.5,
        "jet": {"name": tr("Geyser d'acide", "Acid geyser"), "period": 6.0, "warn": 1.2, "duration": 1.0, "height": 128.0, "damage": 1.0},
        "fists": {"name": tr("Fouissement", "Burrowing"), "periods": [9.5, 8.0, 6.5], "warn": 1.5, "width": 72.0, "damage": 1.0},
        "weakEnemy": "voidworm_anneau", "weakSpots": [[-40.0, -120.0], [60.0, -210.0], [0.0, -300.0]], "weakShare": 0.04,
        "hpPerPlayer": 0.55, "weakHpPerPlayer": 0.25, "pressurePerPlayer": 0.06,
        "swallow": {"periods": [0.0, 20.0, 15.0], "track": 1.0, "lock": 0.8, "width": 96.0, "height": 150.0, "digest": 12.0, "tick": 3.0,
                    "stomach": [s0 * T, (FLOOR + 2) * T, (s1 - s0) * T, 5 * T], "exits": [[(cx - 16) * T, FLOOR * T], [(cx + 16) * T, FLOOR * T]],
                    "poche": "voidworm_poche", "pocheSpot": [-70.0, -150.0]},
        "parasites": {"periods": [24.0, 22.0, 18.0], "first": 14.0, "kinds": ["gloop_small", "wisp"],
                      "spawns": [[hx * T, FLOOR * T] for hx in holes]},
        "acid": {"from": 3, "period": 22.0, "first": 5.0, "warn": 3.0, "rise": 2.5, "hold": 6.0, "fall": 2.0, "height": 150.0, "damage": 2.0,
                 "valves": 0.6, "left": ax0 * T, "right": ax1 * T, "floor": FLOOR * T},
        "links": {"nest": "abd_nid", "valve_high": "abd_valve_haute", "valve_low": "abd_valve_basse"},
        "fx": "animations/voidworm_fx.json",
        "fallText": tr("Le ventre d'Asterion se tait : plus de parasites en renfort.", "Asterion's belly falls silent: no more parasite reinforcements."),
        "goneText": tr("Le gouffre est vide", "The pit is empty"),
        "summary": tr("Épinglez ses 3 anneaux (ses crochets tenus ensemble), crevez sa poche pour libérer les avalés, fuyez l'acide sur les racines hautes.",
                      "Pin its 3 rings (its hooks held together), burst its stomach to free the swallowed, flee the acid onto the high roots."),
    }
    for k, hx in enumerate(holes):
        L.decor(th["deco_small"][k % len(th["deco_small"])], hx + 3, FLOOR)
    for px in range(ax0 + 10, ax1, 30):
        L.light(px, FLOOR - 20, th["glow"], 160, flicker=0.2)
    L.emitter(ax0 + 20, 10, ax1 - 20, FLOOR, "toxic_mist", 2.0)
    L.decor(th["deco_tall"][0], x0 + 2, FLOOR)
    L.orb("health_orb", x0 + 14, FLOOR - 2)
    z.x = ax1 + 2
    # Titan 13: more footholds on every storey of roots, none by the crown (its hook stays up there).
    z.L.footholds(ax0 + 2, ax1 - 2, [FLOOR - 6 * k for k in range(1, 6)], z.th["ledge"], min_gap=16, keep=[(cx - 32, 18, cx + 32, 34)])
    return z.save()
