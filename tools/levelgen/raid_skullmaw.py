"""2.69 (phase 4B): the lair of SKULLMAW, zone 8 of the CRÂNE (docs/RAIDS.md §13), unsealed.

Called by raid_asterion.py (crane()) with that module itself (`A`: its Zone, tr, T…), never run alone.
The arena keeps the format of the seven lairs (metadata.raid.arena: 200 tiles wide, floor at row 62)
and adds the fight (metadata.raid.gigaboss, its routine "skullmaw", game/raid/SkullmawRoutine):

* four columns on the floor: the cover from its blinding flash (hide behind them, away from it);
* six storeys of ledges, the skull hovering over the middle;
* four obsidian mirrors, two on the floor at the walls, two on high balconies: the group must split to
  hold enough of them (`need`, by the players in play) when its gaze charges.

`shell()` is the antechamber and the empty arena shared by the lairs of phase 4B written here
(raid_dreadfang.py and raid_heartbreaker.py use it too).
"""

ARENA_W, FLOOR, H8 = 200, 62, 72
SCALING = {"hpPerPlayer": 0.55, "weakHpPerPlayer": 0.25, "pressurePerPlayer": 0.06}


def shell(A, spec, desc, desc_en, sign, sign_en, music):
    """The antechamber (the way back to zone 7, a sign), the arena's vault and walls, its way in open.
    Returns (zone, ax0, ax1)."""
    z = A.Zone(spec, 8, desc, desc_en, height=H8)
    L, th = z.L, spec.theme
    L.data["bossMusic"] = music
    z.west(low=7, y=FLOOR)
    x0 = z.flat(28)
    L.sign(x0 + 6, FLOOR, A.tr(sign, sign_en))
    L.light(x0 + 14, FLOOR - 6, "#ffd27a", 140, flicker=0.3)
    L.decor(th["deco_tall"][0], x0 + 2, FLOOR)
    L.emitter(x0 + 14, FLOOR - 16, x0 + 28, FLOOR, "fireflies", 2.5)
    dx = z.x
    z.slab(dx, 6, dx + 2, FLOOR - 12)  # the arch over the way in
    ax0, ax1 = dx + 2, dx + 2 + ARENA_W
    z.rock(dx, FLOOR, ax1 + 2)
    z.slab(ax0, 4, ax1, 6)            # the vault
    z.slab(ax1, 4, ax1 + 2, FLOOR)    # the far wall
    for px in range(ax0 + 10, ax1, 30):
        L.light(px, FLOOR - 20, th["glow"], 160, flicker=0.2)
    z.raid["arena"] = {"rect": [ax0 * A.T, 6 * A.T, ARENA_W * A.T, (FLOOR - 6) * A.T], "floor": FLOOR * A.T, "door": "",
                       "entry": [(ax0 + 6) * A.T, FLOOR * A.T], "players": 8}
    z.x = ax1 + 2
    return z, ax0, ax1


def places(A, points, w=3):
    """Places to stand on (mirrors, anchors, ribs): (id, x, surface row) -> {id, rect} in pixels."""
    T = A.T
    return [{"id": pid, "rect": [(x - w / 2.0) * T, (y - 1) * T, w * T, T]} for pid, x, y in points]


def hidden_orbs(z, spots):
    """Health orbs that appear at a phase: {phase: [(x, y)]} -> {phase: [ids]}."""
    out = {}
    for ph, pts in spots.items():
        for ox, oy in pts:
            z.L.orb("health_orb", ox, oy)
            z.L.data["objects"][-1]["props"] = {"amount": 4, "hidden": True}
            out.setdefault(ph, []).append(z.L.data["objects"][-1]["id"])
    return out


def boss_events(A, z, boss, orbs, phase2, phase3, defeated, name, extra3=()):
    """The arena's answers to the gigaboss's phases (events « bossPhase »: orbs, a shake, a hint), its fall."""
    L, tr = z.L, A.tr
    for ph, text, extra in ((2, phase2, ()), (3, phase3, extra3)):
        L.event(boss, "bossPhase", list(extra) + [{"type": "spawn", "target": ",".join(orbs[ph])},
                                                  {"type": "shake", "params": {"amount": 0.8 + 0.2 * (ph - 2)}},
                                                  {"type": "hint", "params": {"text": tr(*text), "duration": 3.5}}],
                params={"phase": ph}, name=tr("Phase %d" % ph, "Phase %d" % ph))
    L.event(boss, "bossDefeated", [{"type": "hint", "params": {"text": tr(*defeated), "duration": 4}},
                                   {"type": "shake", "params": {"amount": 0.6}}], delay=1.5, name=tr(*name))


def lair(A, spec):
    T, tr = A.T, A.tr
    F = FLOOR
    z, ax0, ax1 = shell(A, spec,
                        "L'antre de Skullmaw, au cœur du crâne, assez grand pour huit joueurs : son regard traque chacun, son flash aveugle ceux "
                        "qu'aucune colonne ne cache, ses miroirs se tiennent ensemble.",
                        "Skullmaw's lair, deep in the skull, big enough for eight players: its gaze tracks each one, its flash blinds those no "
                        "column hides, its mirrors are held together.",
                        "Skullmaw, le crâne qui voit tout. Quand son regard se charge : tenez les miroirs, ou cachez-vous derrière une colonne.",
                        "Skullmaw, the all-seeing skull. When its gaze charges: hold the mirrors, or hide behind a column.",
                        "music/liche_boss.ogg")
    L = z.L
    for px in (ax0 + 34, ax0 + 76, ax0 + 122, ax0 + 164):
        z.slab(px, F - 12, px + 3, F)  # the columns (their tops are walkable)
        L.light(px + 1, F - 13, "#c9a8ff", 70, flicker=0.15)
    rows = {F - 6: (10, 50, 92, 138, 178), F - 12: (18, 58, 104, 146, 180), F - 18: (6, 52, 92, 140, 184), F - 30: (20, 70, 120, 170),
            F - 36: (10, 46, 92, 138, 176)}
    for row, xs in sorted(rows.items()):
        for px in xs:
            z.ledge(ax0 + px, ax0 + px + (8 if row == F - 12 else 10), row)
    for a, b in ((40, 55), (145, 160), (92, 108)):  # the balconies of the mirrors (and the middle one)
        z.ledge(ax0 + a, ax0 + b, F - 24)
    mirrors = places(A, [("miroir_ouest", ax0 + 8, F), ("miroir_haut_ouest", ax0 + 47, F - 24),
                         ("miroir_haut_est", ax0 + 153, F - 24), ("miroir_est", ax0 + 192, F)])
    for m in mirrors:
        cx, cy = (m["rect"][0] + m["rect"][2] / 2) / T, m["rect"][1] / T
        L.light(cx, cy - 1, "#ff6ad5", 80, flicker=0.2)
        L.emitter(cx - 1, cy - 2, cx + 1, cy, "aurora_motes", 1.2)
    orbs = hidden_orbs(z, {2: [(ax0 + 47, F - 26), (ax0 + 153, F - 26), (ax0 + 100, F - 26)],
                           3: [(ax0 + 20, F - 8), (ax0 + 180, F - 8), (ax0 + 98, F - 22), (ax0 + 60, F - 16)]})
    boss = L.enemy("skullmaw", ax0 + 100, F - 22, patrol_tiles=40, face_left=True, dormant=True, boss=True)
    L.combat(ax0, 6, ax1, F, waves=boss, lock=False, music="music/liche_boss.ogg")
    L.data["objects"][-1]["props"]["announce"] = False
    z.objective("crane_skullmaw", boss, "killed")
    boss_events(A, z, boss, orbs,
                ("Un voile psychique enveloppe le crâne : tenez les miroirs quand son regard se charge !",
                 "A psychic veil wraps the skull: hold the mirrors when its gaze charges!"),
                ("Les ténèbres tombent : seule la lumière de son œil révèle les plateformes !",
                 "Darkness falls: only the light of its eye reveals the platforms!"),
                ("Skullmaw est vaincu : le crâne d'Asterion s'éteint, le titan est aveuglé.",
                 "Skullmaw is defeated: Asterion's skull goes dark, the titan is blinded."),
                ("Skullmaw vaincu", "Skullmaw defeated"))
    z.raid["gigaboss"] = dict(SCALING, **{
        # Titan 13 (the nerf, docs/RAIDS.md §37): its definition's health × hpScale in the raid (3 / 5).
        "hpScale": 0.25,
        "boss": boss, "name": "Skullmaw", "objective": "crane_skullmaw", "routine": "skullmaw",
        # Its eyes (from its feet, x toward where it faces): where its gazes and its flash leave from.
        "eyes": [[-55.0, -205.0], [55.0, -205.0]],
        # « Regards traqueurs »: a beam per player in play, following, frozen, fired (every phase).
        "gaze": {"periods": [8.0, 6.5, 5.0], "track": 1.5, "lock": 0.45, "fire": 0.35, "width": 18.0, "reach": 2400.0, "damage": 1.0},
        # « Voile psychique » (phases 2 and 3): its « Regard de mort » charges, then flashes on those it sees.
        "veil": [2, 4], "mirrors": mirrors,
        "flash": {"periods": [16.0, 13.0], "charge": 3.5, "damage": 2.0},
        # Mirrors held at the flash, by the players in play (1 to 8).
        "need": [1, 2, 2, 2, 3, 3, 4, 4],
        # Blind: its eyes and its crystal bare (the rules' exposure).
        "exposed": 14.0, "exposedMult": 3.0,
        "weakEnemy": "skullmaw_oeil",
        "weakSpots": [[-55.0, -181.0, "skullmaw_oeil"], [55.0, -181.0, "skullmaw_oeil"], [0.0, -256.0, "skullmaw_cristal"]],
        "weakShare": 0.05,
        # « Les ténèbres » (phase 3, display): its eye-light sweeps the arena (seconds a sweep, half-angle).
        "searchlight": {"period": 7.0, "spread": 0.32},
        "fx": "animations/skullmaw_fx.json",
        "fallText": tr("Le crâne d'Asterion s'éteint : le titan est aveuglé.", "Asterion's skull goes dark: the titan is blinded."),
        "goneText": tr("L'antre est silencieux", "The lair is silent"),
        "summary": tr("Son voile : à son flash, tenez des miroirs ensemble (jusqu'à 4) pour l'aveugler ; les autres se cachent derrière les colonnes.",
                      "Its veil: at its flash, hold mirrors together (up to 4) to blind it; the others hide behind the columns."),
    })
    L.emitter(ax0 + 20, 10, ax1 - 20, F, spec.theme["dust"], 2.0)
    # Titan 13: more footholds on every storey (the balconies' row a whole storey now), out of the columns.
    z.L.footholds(ax0 + 2, ax1 - 2, [F - 6 * k for k in range(1, 7)], z.th["ledge"])
    return z.save()
