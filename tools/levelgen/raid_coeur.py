"""2.72 (phase 6): LE CŒUR D'ASTERION, the final fight of the clan raid (docs/RAIDS.md §15).

Called by raid_asterion.py (main()) with that module itself (`A`: its Zone, Expedition, tr, T…), never
run alone. The heart is not one of the seven branches of the titan's body: it is the « heart » of
titan.json, its own small catalog (data/raids/asterion/coeur/branch.json, server copy in
raidTitan.json's `heart_catalog`), opened by the server once the seven gigaboss fell (validated):

* zone 1, « Aorte cosmique »: the long expedition to the heart (three floors, giant ambushes, locks
  and a goods lift for several players, checkpoints; about 15 min even with 8, estimate_minutes), its
  last ambush, the heart's guard, opens the door of zone 2;
* zone 2, « Le Cœur d'Asterion »: an antechamber, then a gigantic arena (260 tiles: nine screens
  wide, seven storeys of ledges, vertical shuttles and ferries that carry the players around the
  heart, four artery plates far apart: two on the floor at the walls, two on high balconies), the
  heart floating in the middle (metadata.raid.gigaboss, routine "coeur", game/raid/AsterionHeartRoutine).

Every group of every clan fights the heart at the same time, each instance in its own simulation;
its health is shared (the server sums the damage each instance's consensus validated, raidHeart.ts,
and every relay gives the others' part to its games at a step, RAID_STATE). The seven destroyed parts
act on the fight through the titan's links of kind « modifier » (raid_asterion.LINKS → coeur).
"""

import raid_skullmaw as K

FLOOR, H2, ARENA_W = 76, 84, 260
THEME = dict(planet="noyau", ground="ancient_metal", alt="crystal", ledge="tech_ledge", crumble="ruins", cross="ribs", sea=None,
             glow="#ffd27a", style="plasma",
             hazard=("Pulsation cosmique", "Cosmic pulse"), advice=("montez sur les passerelles", "climb onto the walkways"),
             alarm=("Le cœur pulse ! Restez sur les passerelles hautes", "The heart pulses! Stay on the high walkways"),
             small="glowmite", mid="stalker", flyer="sentinel", flyer2="etincelle", heavy="bulwark", guardian="eclat_de_roche",
             deco_small=["crystal_cluster", "rock_small", "pipe_broken"], deco_tall=["crystal_big", "tech_panel", "ruin_pillar"],
             dust="aurora_motes")

# The parts of the titan and what their fall does to the heart (the titan's links of kind « modifier »:
# their ids are read by the routine, their labels shown on the heart's HUD and the titan's map).
MODIFIERS = [
    ("crane", "crane_skullmaw", "Le Crâne est tombé : le Cœur est aveuglé, ses lances stellaires perdent leur précision",
     "The Skull fell: the Heart is blinded, its star lances lose their precision"),
    ("bras_gauche", "bg_dreadfang", "Le Bras gauche est tombé : plus de balayages des artères dans l'arène du Cœur",
     "The Left arm fell: no more artery sweeps in the Heart's arena"),
    ("bras_droit", "main_pyroclast", "Le Bras droit est tombé : plus de pluie de magma sur l'arène du Cœur",
     "The Right arm fell: no more magma rain over the Heart's arena"),
    ("torse", "torse_heartbreaker", "Le Torse est tombé : la cage du Cœur est brisée, la voie de ses ventricules s'ouvre",
     "The Torso fell: the Heart's cage is broken, the way to its ventricles opens"),
    ("abdomen", "abd_voidworm", "L'Abdomen est tombé : plus d'engeances en renfort autour du Cœur",
     "The Abdomen fell: no more spawn reinforcing the Heart"),
    ("jambe_gauche", "jg_graviton", "La Jambe gauche est tombée : plus de pièges de gravité sous les joueurs",
     "The Left leg fell: no more gravity traps under the players"),
    ("jambe_droite", "jd_titanus", "La Jambe droite est tombée : le Cœur chancelle, ses charges stellaires ralentissent",
     "The Right leg fell: the Heart staggers, its star charges slow down"),
]


def spec(A):
    A.THEMES["coeur"] = THEME
    return A.Spec(
        "coeur", "Cœur", "Heart", "co", "Cœur d'Asterion", 0,
        [(1, "Aorte cosmique", "Cosmic aorta", "entrée · longue expédition à 8", 300, 380,
          "Longue expédition sur trois étages : embuscades géantes, écluses et monte-charge à plusieurs, points de contrôle · la garde du cœur",
          "Pulsation cosmique toutes les 75 s dans les galeries basses (étages sûrs)"),
         (2, "Le Cœur d'Asterion", "Asterion's Heart", "COMBAT FINAL · toutes les instances ensemble", 700, 380,
          "Le Cœur d'Asterion : vie partagée par toutes les instances, quatre artères à tenir ensemble, plateformes mobiles, marée de plasma, supernova à contenir",
          "Lances stellaires sur chaque joueur, marée de plasma (phases 3 et 4), supernova (phase 4)")],
        [("coeur_aorte", 1, "Franchir l'Aorte cosmique : vaincre la garde du cœur", "Cross the Cosmic aorta: defeat the heart's guard", False, "", ""),
         ("coeur_asterion", 2, "Vaincre le Cœur d'Asterion (sa vie est partagée par toutes les instances)",
          "Defeat Asterion's Heart (its health is shared by every instance)", False, "gigaboss", "")],
        [(1, 2, "objective:coeur_aorte", False)],
        {})


def aorta(A, s):
    """Zone 1: the long expedition to the heart (the format of the lairs' zones 7)."""
    tr = A.tr
    LOW, MID = A.LOW, A.MID
    z = A.Zone(s, 1, "L'aorte cosmique d'Asterion, qui mène à son cœur : une longue expédition sur trois étages, embuscades géantes, écluses et "
                     "monte-charge à plusieurs.",
               "Asterion's cosmic aorta, which leads to its heart: a long expedition over three floors, giant ambushes, locks and a goods lift "
               "for several players.", height=A.H7)
    e = A.Expedition(z)
    z.rock(0, LOW, 24)
    z.slab(0, MID, 16, MID + 2)
    z.start([(6, LOW), (10, LOW), (14, LOW)])
    for i, y in enumerate((57, 54, 51, 48)):
        z.ledge(16 if i % 2 == 0 else 20, (16 if i % 2 == 0 else 20) + 4, y)
    z.L.sign(3, LOW, tr("Les sept gigaboss sont tombés : l'aorte mène au Cœur d'Asterion. Restez répartis sur les trois étages.",
                        "The seven gigaboss fell: the aorta leads to Asterion's Heart. Stay spread over the three floors."))
    z.L.light(8, LOW - 4, "#ffd27a", 140, flicker=0.25)
    x = 24
    x = e.hall(x)
    x = e.checkpoint(x, 1)
    x = e.lock(x, 3, 1)
    x, _ = e.ambush(x, 4, text=("Première vague du cœur repoussée !", "First wave of the heart driven back!"))
    x = e.hall(x, hazard=True)
    x = e.lift(x, 4)
    x = e.checkpoint(x, 2)
    x, _ = e.ambush(x, 4, text=("Deuxième vague repoussée : le cœur bat plus fort !", "Second wave driven back: the heart beats louder!"))
    x = e.hall(x)
    x = e.lock(x, 3, 2)
    x = e.hall(x)
    x = e.checkpoint(x, 3)
    x, _ = e.ambush(x, 4, text=("Troisième vague repoussée : la garde du cœur approche !", "Third wave driven back: the heart's guard draws near!"))
    x = e.hall(x)
    x = e.checkpoint(x, 4)
    x, _ = e.ambush(x, 5, objective="coeur_aorte", w=110)
    z.rock(x, LOW, x + 4)
    z.x, z.y = x + 4, LOW
    z.east(2)
    door = z.doors[2]
    z.L.event(z.raid["objectives"][0]["source"], "cleared",
              [{"type": "open", "target": door},
               {"type": "hint", "params": {"text": tr("La garde du cœur est tombée : la porte du Cœur d'Asterion s'ouvre !",
                                                      "The heart's guard fell: the door of Asterion's Heart opens!"), "duration": 3.5}}], delay=0.5)
    z.raid["coop"] = e.coops
    z.raid["checkpoints"] = e.checkpoints
    return z.save()


def arena(A, s):
    """Zone 2: the antechamber, then the heart's gigantic arena and its fight."""
    tr, T = A.tr, A.T
    F = FLOOR
    z = A.Zone(s, 2, "Le Cœur d'Asterion, une étoile captive au milieu d'une arène immense : tous les groupes le combattent en même temps.",
               "Asterion's Heart, a captive star in the middle of an immense arena: every group fights it at the same time.", height=H2)
    L = z.L
    L.data["bossMusic"] = "music/singularis_boss.ogg"
    z.west(low=1, y=F)
    x0 = z.flat(30)
    L.sign(x0 + 5, F, tr("Le Cœur d'Asterion. Sa vie est partagée par toutes les instances : chaque coup compte pour tous les clans.",
                         "Asterion's Heart. Its health is shared by every instance: every blow counts for every clan."))
    L.light(x0 + 14, F - 6, "#ffd27a", 160, flicker=0.3)
    L.decor(THEME["deco_tall"][0], x0 + 2, F)
    L.decor(THEME["deco_tall"][1], x0 + 26, F, flip=True)
    L.emitter(x0 + 10, F - 16, x0 + 28, F, "aurora_motes", 2.5)
    dx = z.x
    z.slab(dx, 6, dx + 2, F - 14)  # the arch over the way in
    ax0, ax1 = dx + 2, dx + 2 + ARENA_W
    mid = ax0 + ARENA_W // 2
    z.rock(dx, F, ax1 + 2)
    z.slab(ax0, 4, ax1, 6)          # the vault
    z.slab(ax1, 4, ax1 + 2, F)      # the far wall
    # Seven storeys of ledges, staggered; none in the heart's room (its box floats over the middle).
    heart_x0, heart_x1, heart_top, heart_bottom = mid - 16, mid + 16, F - 42, F - 12
    rows = (F - 6, F - 12, F - 18, F - 24, F - 30, F - 36, F - 48)
    for k, row in enumerate(rows):
        for px in range(ax0 + 6 + (11 if k % 2 else 0), ax1 - 12, 24):
            if heart_top - 1 <= row <= heart_bottom + 1 and px + 10 > heart_x0 and px < heart_x1:
                continue
            z.ledge(px, px + 10, row)
    # The balconies of the two high artery plates (wide, under the shuttles' stops).
    z.ledge(ax0 + 56, ax0 + 74, F - 30)
    z.ledge(ax1 - 74, ax1 - 56, F - 30)
    # « Les artères »: vertical shuttles from the floor to the high storeys, ferries over the heart, and
    # two lifts beside it that carry melee fighters to its lower ventricles.
    for k, px in enumerate((ax0 + 40, ax0 + 90, ax1 - 96, ax1 - 46)):
        L.moving(px, F - 2, 6, 0, -36, speed=70, wait=1.2, phase=0.25 * k)
        L.light(px + 3, F - 4, "#ffd27a", 70, flicker=0.2)
    L.moving(mid - 34, F - 52, 8, 26, 0, speed=60, wait=1.0, phase=0.0)
    L.moving(mid + 8, F - 52, 8, 26, 0, speed=60, wait=1.0, phase=0.5)
    L.moving(mid - 22, F - 3, 5, 0, -18, speed=55, wait=1.4, phase=0.0)
    L.moving(mid + 17, F - 3, 5, 0, -18, speed=55, wait=1.4, phase=0.5)
    plates = K.places(A, [("artere_ouest", ax0 + 12, F), ("artere_haute_ouest", ax0 + 65, F - 30),
                          ("artere_haute_est", ax1 - 65, F - 30), ("artere_est", ax1 - 12, F)], w=4)
    for p in plates:
        cx, cy = (p["rect"][0] + p["rect"][2] / 2) / T, p["rect"][1] / T
        L.light(cx, cy - 1, "#ff9a5a", 90, flicker=0.25)
        L.emitter(cx - 2, cy - 3, cx + 2, cy, "embers_rising", 1.5)
    orbs = K.hidden_orbs(z, {2: [(ax0 + 65, F - 32), (ax1 - 65, F - 32), (mid - 40, F - 8), (mid + 40, F - 8)],
                             3: [(ax0 + 20, F - 8), (ax1 - 20, F - 8), (mid, F - 54), (ax0 + 100, F - 26), (ax1 - 100, F - 26)],
                             4: [(ax0 + 30, F - 38), (ax1 - 30, F - 38), (mid - 60, F - 20), (mid + 60, F - 20), (mid, F - 54)]})
    boss = L.enemy("asterion_coeur", mid, F - 14, patrol_tiles=0, face_left=True, dormant=True, boss=True)
    L.combat(ax0, 6, ax1, F, waves=boss, lock=False, music="music/singularis_boss.ogg")
    L.data["objects"][-1]["props"]["announce"] = False
    z.objective("coeur_asterion", boss, "killed")
    texts = {2: ("Les artères du cœur se gonflent : tenez les quatre artères ensemble !", "The heart's arteries swell: hold the four arteries together!"),
             3: ("Une couronne d'étoiles se lève : la marée de plasma va monter, restez sur les plateformes !",
                 "A crown of stars rises: the plasma tide will rise, stay on the platforms!"),
             4: ("SUPERNOVA : le cœur s'effondre ! Contenez-la sur les ancres, tous ensemble !",
                 "SUPERNOVA: the heart collapses! Contain it on the anchors, all together!")}
    for ph in (2, 3, 4):
        L.event(boss, "bossPhase", [{"type": "spawn", "target": ",".join(orbs[ph])},
                                    {"type": "shake", "params": {"amount": 0.6 + 0.25 * (ph - 2)}},
                                    {"type": "hint", "params": {"text": tr(*texts[ph]), "duration": 3.5}}],
                params={"phase": ph}, name=tr("Phase %d" % ph, "Phase %d" % ph))
    L.event(boss, "bossDefeated", [{"type": "hint", "params": {"text": tr("Le Cœur d'Asterion cède : le titan s'effondre !",
                                                                          "Asterion's Heart gives way: the titan collapses!"), "duration": 4}},
                                   {"type": "shake", "params": {"amount": 1.0}}], delay=1.5, name=tr("Cœur vaincu", "Heart defeated"))
    span = [ax0 * T, ax1 * T]
    z.raid["arena"] = {"rect": [ax0 * T, 6 * T, ARENA_W * T, (F - 6) * T], "floor": F * T, "door": "",
                       "entry": [(ax0 + 6) * T, F * T], "players": 8}
    z.raid["gigaboss"] = dict(K.SCALING, **{
        "boss": boss, "name": "Cœur d'Asterion", "objective": "coeur_asterion", "routine": "coeur",
        # Its health is shared by every instance of the event (raidHeart.ts): each one reports its own part
        # (ten-thousandths of its health), the others' part comes with RAID_STATE. Each relic of the seven
        # branches validated takes `relicShare` of its health at the start of the fight.
        "heart": {"relicShare": 0.03, "reportEvery": 15.0},
        # « Les artères » (phases 2 and 3, its armour): its four artery plates held together bare its
        # ventricles (the rules' exposure). « La cage » (the Torso standing): its armour from its first phase,
        # each artery held longer, its ventricles bare less long.
        "carapace": [2, 4], "vents": plates, "hold": 2.5, "reopen": 14.0, "exposed": 10.0, "exposedMult": 2.0,
        "jet": {"name": tr("Jet d'artère", "Artery jet"), "period": 9.0, "warn": 1.2, "duration": 0.8, "height": 120.0, "damage": 1.0},
        "cage": {"hold": 3.5, "exposed": 0.6},
        "weakEnemy": "asterion_ventricule", "weakSpots": [[-150.0, -96.0], [150.0, -96.0], [0.0, -372.0]], "weakShare": 0.05,
        # « Lances stellaires » (every phase, on each player in play): a mark that follows its player, then
        # locks and a lance of light falls on it. The Skull down: no tracking, the mark lands beside them.
        "lances": {"name": tr("Lances stellaires", "Star lances"), "periods": [8.0, 7.0, 6.0, 5.0], "track": 1.2, "warn": 1.3,
                   "width": 56.0, "damage": 1.0, "miss": [48.0, 120.0]},
        # « Balayages des artères » (Dreadfang standing): blades from the heart to the walls along storeys.
        "sweeps": {"name": tr("Balayage des artères", "Artery sweep"), "periods": [0.0, 11.0, 9.0, 7.0], "warn": 1.4, "speed": 760.0,
                   "height": 52.0, "damage": 1.0, "rows": [F * T, (F - 12) * T, (F - 24) * T]},
        # « Pluie de magma » (Pyroclast standing): columns of magma on deterministic spots of the arena.
        "rain": {"name": tr("Pluie de magma", "Magma rain"), "periods": [10.0, 9.0, 8.0, 7.0], "count": [6, 7, 8, 10], "warn": 1.2,
                 "width": 64.0, "damage": 1.0},
        # « Pièges de gravité » (Graviton standing): a well under each player in play.
        "wells": {"name": tr("Piège de gravité", "Gravity trap"), "periods": [0.0, 12.0, 10.0, 9.0], "warn": 1.2, "time": 3.0, "speed": 95.0,
                  "range": 220.0, "core": 30.0, "damage": 1.0},
        # « Charges stellaires » (every phase from the 2nd): a comet runs a storey from wall to wall. The
        # Right leg down: slower (`slow`), announced longer.
        "charges": {"name": tr("Charge stellaire", "Star charge"), "periods": [0.0, 13.0, 11.0, 9.0], "warn": 1.5, "speed": 900.0,
                    "height": 64.0, "damage": 2.0, "slow": 0.55, "rows": [F * T, (F - 18) * T, (F - 30) * T]},
        # « Engeances du cœur » (Voidworm standing): reinforcements out of the heart.
        "spawns": {"periods": [0.0, 22.0, 20.0, 18.0], "kinds": ["etincelle", "glowmite", "sentinel"], "max": 8},
        # « Marée de plasma » (phases 3 and 4, the environment): the floor burns, the platforms are safe.
        "tide": {"name": tr("Marée de plasma", "Plasma tide"), "periods": [0.0, 0.0, 24.0, 18.0], "warn": 3.0, "duration": 6.0,
                 "height": 3 * T, "damage": 1.0},
        # « Supernova » (phase 4, the hardest): the heart charges; enough players on the anchors (the four
        # artery plates, `need` by the players in play) contain it (its ventricles bare), else it burns all.
        "supernova": {"name": tr("Supernova", "Supernova"), "period": 26.0, "charge": 6.0, "need": [1, 2, 2, 2, 3, 3, 4, 4],
                      "damage": 3.0},
        "span": span, "floor": F * T,
        "modifiers": {part: "coeur_" + part for part, _, _, _ in MODIFIERS},
        "fx": "animations/asterion_coeur_fx.json",
        "fallText": tr("Le Cœur d'Asterion s'est éteint : le titan s'effondre.", "Asterion's Heart went out: the titan collapses."),
        "goneText": tr("Le Cœur est éteint", "The Heart is out"),
        "summary": tr("Vie partagée par toutes les instances. Tenez les 4 artères ensemble pour mettre ses ventricules à nu ; en phase 4, "
                      "contenez la supernova sur les ancres.",
                      "Health shared by every instance. Hold the 4 arteries together to bare its ventricles; in phase 4, contain the "
                      "supernova on the anchors."),
    })
    for px in range(ax0 + 10, ax1, 26):
        L.light(px, F - 22, "#ffd27a" if (px // 26) % 2 else "#b46cff", 170, flicker=0.25)
    L.light(mid, F - 27, "#ffffff", 320, flicker=0.35)
    L.emitter(ax0 + 20, 10, ax1 - 20, F, "aurora_motes", 3.0)
    L.emitter(mid - 30, F - 50, mid + 30, F - 6, "embers_rising", 3.0)
    z.x = ax1 + 2
    return z.save()


def build(A):
    """The heart's two zones and its catalog; returns (catalog, levels)."""
    s = spec(A)
    aorta(A, s)
    arena(A, s)
    levels = {n: A.load_json(A.os.path.join(A.RAID_DIR, "coeur", "zone_%d.json" % n)) for n in (1, 2)}
    minutes = {n: round(A.estimate_minutes(levels[n]), 1) for n in (1, 2)}
    assert 13.0 <= minutes[1] <= 17.0, ("coeur", "aorte", minutes[1])
    print("coeur : %s min (8 joueurs ; l'aorte : %.1f min, puis le combat final)" % (minutes, minutes[1]))
    cat = s.catalog(minutes)
    for zone in cat["zones"]:  # Spec.catalog marks the lairs' zone 8: here the heart's arena is zone 2
        zone["boss"] = zone["id"] == 2
        zone["secret"] = False
    cat["revision"] = 1
    A.check_branch(cat, levels)
    A.write_json(A.os.path.join(A.RAID_DIR, "coeur", "branch.json"), cat)
    return cat, levels


def links(A):
    """The titan's links of kind « modifier » to the heart: one per part, from its gigaboss's fall."""
    out = []
    for part, objective, label, label_en in MODIFIERS:
        out.append(("coeur_" + part, label, label_en, [(part, objective)], "coeur", "modifier", [2], []))
    return out
