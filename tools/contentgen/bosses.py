"""The 8 v2 bosses (see docs/ASSET_SPEC.md section 4b for their sprite sheets).

Clip sets: knight (Thornwarden rig), hydra (rooted), core (floating machine), wyrm (flying serpent).
`origin` = projectile / beam origin in sprite pixels from the feet (x forward, y up negative).
"""


def _atk(id, name, type, damage, range, telegraph, active, recover, cooldown, tanim, aanim, **kw):
    a = {"id": id, "name": name, "type": type, "damage": damage, "range": range, "telegraph": telegraph,
         "active": active, "recover": recover, "cooldown": cooldown, "telegraphAnim": tanim, "attackAnim": aanim}
    a.update(kw)
    return a


def _phase(name, threshold, attacks, speed=1.0, cooldown=1.0, text="", summon=False):
    p = {"name": name, "hpThreshold": threshold, "attacks": attacks, "speedMultiplier": speed,
         "cooldownMultiplier": cooldown}
    if text:
        p["enterText"] = text
    if summon:
        p["summonOnEnter"] = True
    return p


def _boss(id, name, title, desc, size, hp, attacks, phases, *, flying=False, movement="guard", reaction="chase",
          speed=38, preferred=150, light="#39f0d8", roar="sounds/boss_roar.wav", mult=None, score=3500):
    return {
        "format": "cosmic.enemy", "id": id, "name": name, "description": desc, "family": "miniboss",
        "animation": f"animations/{id}.json", "size": size, "scale": 1.0, "hp": hp, "contactDamage": 1,
        "speed": speed, "chaseSpeedMultiplier": 1.2, "flying": flying, "knockbackResist": 1,
        "detectionRange": 360, "attackRange": 200, "loseRange": 2000, "needsLineOfSight": False,
        "movement": movement, "reaction": reaction, "fleeHealthRatio": 0, "preferredDistance": preferred,
        "hopVelocity": 0, "attacks": attacks,
        "powers": {"teleport": {"enabled": False}, "defend": {"enabled": False}, "reinforcements": {"enabled": False}},
        "damageMultipliers": mult or {},
        "rewards": {"score": score, "drops": [{"type": "health_orb", "chance": 1.0}, {"type": "energy_orb", "chance": 1.0}]},
        "light": {"color": light, "radius": 100},
        "sounds": {"hurt": "sounds/enemy_hurt.wav", "death": roar, "alert": roar},
        "boss": {"title": title, "phases": phases},
    }


FIRE = dict(hitEffect="fire_burst")
ICE_SLOW = dict(hitEffect="ice_burst", slow=0.5, slowDuration=1.6)


def spore_queen():
    o = [0, -78]
    attacks = [
        _atk("bite", "Morsure des trois têtes", "melee", 2, 96, 0.75, 0.3, 0.6, 2.4, "telegraph_bite", "bite",
             knockback=260),
        _atk("spores", "Salve de spores", "arc", 1, 420, 0.8, 0.25, 0.7, 3.0, "telegraph_spores", "spores",
             projectile="spore_big", projectileSpeed=150, count=3, spread=22, gravity=420, origin=o, projectileRadius=6,
             hitEffect="acid_burst"),
        _atk("vines", "Ronces jaillissantes", "eruption", 2, 420, 1.0, 0.3, 0.8, 4.2, "telegraph_vines", "vines",
             projectile="spore", projectileSpeed=380, count=3, spread=72, gravity=700,
             warningEffect="eruption_warning_spore", eruptionEffect="eruption_spore"),
        _atk("spore_rain", "Pluie de spores", "rain", 1, 500, 1.0, 0.3, 0.8, 4.0, "telegraph_spores", "spores",
             projectile="spore", projectileSpeed=150, count=6, spread=52, gravity=180, rainHeight=240),
        _atk("summon", "Bourgeons gluants", "summon", 0, 500, 0.9, 0.4, 0.8, 11, "summon", "summon",
             summonId="gloop", summonCount=2, maxSummoned=3),
    ]
    phases = [
        _phase("Floraison", 1.0, ["bite", "spores", "vines"]),
        _phase("Sève bouillonnante", 0.66, ["bite", "spores", "vines", "summon"], 1.1, 0.9,
               "La Reine Sporale répand ses bourgeons !", True),
        _phase("Tempête de spores", 0.33, ["bite", "spore_rain", "vines", "spores"], 1.2, 0.75,
               "Le ciel se couvre de spores !"),
    ]
    return _boss("spore_queen", "Reine Sporale", "REINE SPORALE, CŒUR DE LA CANOPÉE",
                 "Plante carnivore colossale enracinée au cœur de la canopée. Ses trois têtes mordent, ses sacs crachent des spores.",
                 [56, 64], 60, attacks, phases, movement="stationary", speed=0, light="#b04cff",
                 mult={"charge_shot": 1.2, "wave": 1.2})


def prime_sentinel():
    eye = [8, -24]
    attacks = [
        _atk("volley", "Salve de disques", "spread", 1, 460, 0.7, 0.25, 0.6, 2.6, "telegraph_volley", "volley",
             projectile="energy_disc", projectileSpeed=190, count=5, spread=14, origin=eye, projectileRadius=5),
        _atk("beam", "Rayon de l'œil", "laser", 2, 460, 1.1, 1.5, 1.0, 5.5, "telegraph_beam", "beam",
             laserLength=440, sweepDegrees=80, origin=eye, color="#ffb347"),
        _atk("slam", "Écrasement", "lunge", 2, 300, 0.8, 0.6, 0.9, 4.0, "telegraph_slam", "slam",
             lungeSpeed=300, minRange=40, knockback=280),
        _atk("debris", "Pluie de débris", "rain", 1, 520, 1.0, 0.3, 0.8, 4.5, "telegraph_volley", "volley",
             projectile="rock", projectileSpeed=160, count=7, spread=46, gravity=240, rainHeight=250, hitEffect="slam_dust"),
        _atk("summon", "Escorte", "summon", 0, 500, 0.9, 0.4, 0.8, 12, "summon", "summon",
             summonId="sentinel", summonCount=2, maxSummoned=3),
    ]
    phases = [
        _phase("Activation", 1.0, ["volley", "beam", "slam"]),
        _phase("Protocole de défense", 0.6, ["volley", "beam", "slam", "summon"], 1.1, 0.9,
               "Sentinelle Prime appelle son escorte !", True),
        _phase("Surcharge", 0.3, ["volley", "beam", "slam", "debris"], 1.25, 0.75, "Le plafond s'effondre !"),
    ]
    return _boss("prime_sentinel", "Sentinelle Prime", "SENTINELLE PRIME, ŒIL DES ANCIENS",
                 "Gardien mécanique flottant qui protège le cœur de la machine. Ses plaques blindées tournent autour de son œil.",
                 [48, 48], 64, attacks, phases, flying=True, movement="fly", reaction="keepDistance", speed=46,
                 preferred=160, light="#ffb347", roar="sounds/mech_alarm.wav", mult={"wave": 1.3, "charge_shot": 1.3})


def magmaroth():
    attacks = [
        _atk("slam", "Marteau tellurique", "slam", 2, 180, 0.85, 0.35, 1.0, 3.0, "telegraph_slam", "slam",
             projectileSpeed=220, knockback=260),
        _atk("charge", "Ruée de magma", "charge", 2, 420, 0.8, 2.2, 1.1, 4.0, "telegraph_charge", "charge",
             minRange=70, lungeSpeed=260, knockback=270),
        _atk("barrage", "Gerbe de magma", "arc", 1, 420, 0.8, 0.25, 0.8, 3.2, "telegraph_barrage", "barrage",
             projectile="magma_glob", projectileSpeed=160, count=4, spread=18, gravity=420, **FIRE),
        _atk("eruption", "Éruption", "eruption", 2, 460, 1.0, 0.3, 0.9, 4.5, "telegraph_slam", "slam",
             projectile="fireball", projectileSpeed=420, count=4, spread=64, gravity=700,
             warningEffect="eruption_warning_fire", eruptionEffect="eruption_fire", **FIRE),
        _atk("breath", "Souffle de forge", "laser", 2, 360, 1.1, 1.4, 1.1, 6.0, "telegraph_laser", "laser",
             laserLength=320, sweepDegrees=60, color="#ff8a3d"),
        _atk("summon", "Braises vivantes", "summon", 0, 500, 0.9, 0.4, 0.8, 11, "summon", "summon",
             summonId="ember_mite", summonCount=2, maxSummoned=3),
    ]
    phases = [
        _phase("Réveil du magma", 1.0, ["slam", "barrage", "charge"]),
        _phase("Fusion", 0.66, ["slam", "barrage", "charge", "eruption", "summon"], 1.1, 0.9,
               "Le sol se fissure sous Magmaroth !", True),
        _phase("Cœur en fusion", 0.33, ["slam", "eruption", "breath", "charge", "barrage"], 1.2, 0.8,
               "Magmaroth crache le feu de la forge !"),
    ]
    return _boss("magmaroth", "Magmaroth", "MAGMAROTH, LE MARTEAU DES PROFONDEURS",
                 "Colosse de basalte animé par un cœur de magma. Son marteau fait jaillir la lave du sol.",
                 [44, 60], 64, attacks, phases, light="#ff8a3d", roar="sounds/boss_roar_fire.wav",
                 mult={"dash": 0.5, "ground_pound": 1.3, "wave": 0.8})


def _wyrm(id, name, title, desc, theme):
    mouth = [36, -26]
    fire = theme == "fire"
    proj = "fireball" if fire else "ice_shard"
    eff = FIRE if fire else ICE_SLOW
    attacks = [
        _atk("bolts", "Crachats" if fire else "Éclats de givre", "spread", 1, 460, 0.7, 0.25, 0.6, 2.6,
             "telegraph_meteor", "meteor", projectile=proj, projectileSpeed=200, count=3 if fire else 4,
             spread=15 if fire else 12, origin=mouth, **eff),
        _atk("dive", "Piqué", "lunge", 2, 320, 0.85, 0.7, 1.0, 3.6, "telegraph_dive", "dive", lungeSpeed=320,
             minRange=40, knockback=280, **({} if fire else {"slow": 0.5, "slowDuration": 1.4})),
        _atk("breath", "Souffle ardent" if fire else "Souffle glacial", "laser", 2, 380, 1.1, 1.5, 1.1, 5.5,
             "telegraph_breath", "breath", laserLength=340, sweepDegrees=90, origin=mouth,
             color="#ff8a3d" if fire else "#7ff6ff", **({} if fire else {"slow": 0.5, "slowDuration": 1.6})),
        _atk("meteor", "Pluie de météores" if fire else "Grêle de cristaux", "rain", 1, 520, 1.0, 0.3, 0.8, 4.2,
             "telegraph_meteor", "meteor", projectile=proj, projectileSpeed=170, count=5 if fire else 6,
             spread=56 if fire else 48, gravity=300, rainHeight=260, **eff),
        _atk("summon", "Nuée", "summon", 0, 500, 0.9, 0.4, 0.8, 12, "summon", "summon",
             summonId="ash_wing" if fire else "frost_wing", summonCount=2, maxSummoned=3),
    ]
    phases = [
        _phase("Envol", 1.0, ["bolts", "dive", "breath"]),
        _phase("Tourbillon", 0.6, ["meteor", "dive", "breath", "summon"], 1.15, 0.9,
               (name + " appelle sa nuée !"), True),
        _phase("Fureur", 0.3, ["meteor", "dive", "breath", "bolts"], 1.25, 0.75,
               (name + " s'embrase !") if fire else (name + " déchaîne le blizzard !")),
    ]
    return _boss(id, name, title, desc, [56, 40], 60 if fire else 64, attacks, phases, flying=True, movement="fly",
                 reaction="keepDistance", speed=60, preferred=150, light="#ff8a3d" if fire else "#7ff6ff",
                 roar="sounds/wyrm_roar.wav", mult={"charge_shot": 1.25} if fire else {"wave": 1.2, "charge_shot": 1.25})


def forge_core():
    heart = [6, -24]
    attacks = [
        _atk("volley", "Gerbe de scories", "arc", 1, 460, 0.75, 0.25, 0.6, 2.8, "telegraph_volley", "volley",
             projectile="magma_glob", projectileSpeed=170, count=5, spread=16, gravity=380, origin=heart, **FIRE),
        _atk("beam", "Rayon de fusion", "laser", 2, 460, 1.1, 1.6, 1.0, 5.5, "telegraph_beam", "beam",
             laserLength=440, sweepDegrees=100, origin=heart, color="#ff8a3d"),
        _atk("slam", "Chute du creuset", "lunge", 2, 300, 0.8, 0.6, 0.9, 4.0, "telegraph_slam", "slam",
             lungeSpeed=310, minRange=40, knockback=280),
        _atk("eruption", "Coulée de lave", "eruption", 2, 480, 1.0, 0.3, 0.9, 4.5, "telegraph_slam", "slam",
             projectile="fireball", projectileSpeed=420, count=5, spread=60, gravity=700,
             warningEffect="eruption_warning_fire", eruptionEffect="eruption_fire", **FIRE),
        _atk("embers", "Pluie de braises", "rain", 1, 520, 1.0, 0.3, 0.8, 4.0, "telegraph_volley", "volley",
             projectile="ember", projectileSpeed=180, count=8, spread=36, gravity=200, rainHeight=250, **FIRE),
        _atk("summon", "Forge vivante", "summon", 0, 500, 0.9, 0.4, 0.8, 12, "summon", "summon",
             summonId="forge_bomber", summonCount=1, maxSummoned=2),
    ]
    phases = [
        _phase("Allumage", 1.0, ["volley", "beam", "slam"]),
        _phase("Coulée", 0.66, ["volley", "beam", "slam", "eruption", "summon"], 1.1, 0.9,
               "Le Forgeron Primordial ouvre ses vannes !", True),
        _phase("Fusion totale", 0.33, ["embers", "beam", "eruption", "slam", "volley"], 1.25, 0.75,
               "La fonderie entière s'embrase !"),
    ]
    return _boss("forge_core", "Forgeron Primordial", "LE FORGERON PRIMORDIAL",
                 "Cœur de forge vivant enchaîné dans des anneaux de fer. Il fond la roche et façonne des machines.",
                 [48, 48], 72, attacks, phases, flying=True, movement="fly", reaction="keepDistance", speed=44,
                 preferred=170, light="#ff8a3d", roar="sounds/boss_roar_fire.wav",
                 mult={"wave": 1.2, "charge_shot": 1.3}, score=4500)


def frost_hydra():
    o = [0, -78]
    attacks = [
        _atk("bite", "Morsure gelée", "melee", 2, 96, 0.75, 0.3, 0.6, 2.4, "telegraph_bite", "bite", knockback=260,
             slow=0.5, slowDuration=1.4),
        _atk("orbs", "Orbes de givre", "arc", 1, 420, 0.8, 0.25, 0.7, 3.0, "telegraph_spores", "spores",
             projectile="frost_orb", projectileSpeed=150, count=3, spread=22, gravity=420, origin=o, projectileRadius=6,
             **ICE_SLOW),
        _atk("spikes", "Pics de glace", "eruption", 2, 440, 1.0, 0.3, 0.8, 4.2, "telegraph_vines", "vines",
             projectile="ice_shard", projectileSpeed=400, count=4, spread=64, gravity=700,
             warningEffect="eruption_warning_ice", eruptionEffect="eruption_ice", **ICE_SLOW),
        _atk("icicles", "Chute de stalactites", "rain", 1, 520, 1.0, 0.3, 0.8, 4.0, "telegraph_spores", "spores",
             projectile="ice_shard", projectileSpeed=180, count=6, spread=50, gravity=260, rainHeight=250, **ICE_SLOW),
        _atk("summon", "Bourgeons gelés", "summon", 0, 500, 0.9, 0.4, 0.8, 11, "summon", "summon",
             summonId="ice_gloop", summonCount=2, maxSummoned=3),
    ]
    phases = [
        _phase("Dégel", 1.0, ["bite", "orbs", "spikes"]),
        _phase("Racines de givre", 0.66, ["bite", "orbs", "spikes", "summon"], 1.1, 0.9,
               "L'Hydre de Givre réveille la grotte !", True),
        _phase("Blizzard", 0.33, ["bite", "icicles", "spikes", "orbs"], 1.2, 0.75, "Les stalactites tremblent !"),
    ]
    return _boss("frost_hydra", "Hydre de Givre", "L'HYDRE DE GIVRE",
                 "Arbre-hydre pris dans la glace éternelle. Ses trois têtes de cristal gardent les grottes de Glacior.",
                 [56, 64], 70, attacks, phases, movement="stationary", speed=0, light="#7ff6ff",
                 roar="sounds/boss_roar_ice.wav", mult={"charge_shot": 1.2, "ground_pound": 1.2})


def glaciarch():
    attacks = [
        _atk("slam", "Lame sismique", "slam", 2, 190, 0.85, 0.35, 1.0, 3.0, "telegraph_slam", "slam",
             projectileSpeed=230, knockback=260, slow=0.5, slowDuration=1.2),
        _atk("charge", "Charge du blizzard", "charge", 2, 440, 0.8, 2.2, 1.1, 4.0, "telegraph_charge", "charge",
             minRange=70, lungeSpeed=270, knockback=270),
        _atk("barrage", "Éclats de cristal", "spread", 1, 440, 0.75, 0.25, 0.8, 3.0, "telegraph_barrage", "barrage",
             projectile="ice_shard", projectileSpeed=210, count=5, spread=10, **ICE_SLOW),
        _atk("spikes", "Forêt de glace", "eruption", 2, 480, 1.0, 0.3, 0.9, 4.5, "telegraph_slam", "slam",
             projectile="ice_shard", projectileSpeed=420, count=5, spread=58, gravity=700,
             warningEffect="eruption_warning_ice", eruptionEffect="eruption_ice", **ICE_SLOW),
        _atk("beam", "Rayon glacial", "laser", 2, 380, 1.1, 1.5, 1.1, 6.0, "telegraph_laser", "laser",
             laserLength=340, sweepDegrees=70, color="#7ff6ff", slow=0.5, slowDuration=1.6),
        _atk("summon", "Garde gelée", "summon", 0, 500, 0.9, 0.4, 0.8, 12, "summon", "summon",
             summonId="frost_mite", summonCount=2, maxSummoned=3),
    ]
    phases = [
        _phase("Garde éternelle", 1.0, ["slam", "barrage", "charge"]),
        _phase("Mur de glace", 0.66, ["slam", "barrage", "charge", "spikes", "summon"], 1.1, 0.9,
               "Le Glaciarque appelle sa garde !", True),
        _phase("Hiver absolu", 0.33, ["slam", "spikes", "beam", "charge", "barrage"], 1.25, 0.75,
               "Le Glaciarque libère l'hiver absolu !"),
    ]
    return _boss("glaciarch", "Glaciarque", "LE GLACIARQUE, SEIGNEUR DE GLACIOR",
                 "Chevalier de glace millénaire, gardien du bastion gelé. Sa lame de cristal fige tout ce qu'elle touche.",
                 [44, 60], 80, attacks, phases, light="#7ff6ff", roar="sounds/boss_roar_ice.wav",
                 mult={"dash": 0.5, "ground_pound": 1.3}, score=5000)


def all_bosses():
    return [
        spore_queen(), prime_sentinel(), magmaroth(),
        _wyrm("ignivore", "Ignivore", "IGNIVORE, LE SERPENT DE FEU",
              "Serpent ailé né dans les rivières de lave. Il plonge sur sa proie et crache des météores.", "fire"),
        forge_core(),
        _wyrm("cryowyrm", "Cryowyrm", "CRYOWYRM, LE SERPENT DES GLACES",
              "Serpent de cristal des plaines gelées. Son souffle fige le sang.", "ice"),
        frost_hydra(), glaciarch(),
    ]
