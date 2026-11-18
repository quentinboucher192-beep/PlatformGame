#!/usr/bin/env python3
"""Builds the six other branches of the clan raid « Asterion » and the titan's catalog (phase 4,
docs/RAIDS.md): CRÂNE (Skullmaw), BRAS GAUCHE (Dreadfang), TORSE (Heartbreaker), ABDOMEN
(Voidworm), JAMBE GAUCHE (Graviton) and JAMBE DROITE (Titanus). The BRAS DROIT stays the work of
raid_bras_droit.py (it is only read here, never written).

Each branch: eight sub-zones, each a real level (data/raids/asterion/<branch>/zone_N.json, with
its metadata.raid as in raid_bras_droit.py), and its catalog (branch.json): two paths from the
entry, a secret zone, a shortcut opened by a lever, a zone that needs two groups (a pair of
objectives), a lock that waits for objectives of several zones (« generators »: the eyes of the
skull, the frost hearts, the four plates of the torso…), an arena that opens the gigaboss' lair,
whose door stays sealed (« <Gigaboss> — prochaine version »). Each branch has its own world
(materials, enemies, crossings: floating vertebrae, brittle ice, moving ribs, breathing gut,
gravity wells, pistons) and its own periodic danger (psychic wave, claw sweep, heart pulse, acid
reflux, gravity well, seismic wave).

The titan (data/raids/asterion/titan.json, server copy server/src/raidTitan.json with the six
catalogs) links the branches: an objective done in one branch opens a passage, switches off a
danger, defences or reinforcements in another (spec §7). A link is a bit of the target branch's
state (24 and up; the objectives are the bits below): the server sets it when the source
objectives are accepted, the relay gives it at a step (RAID_STATE), every game of an instance
applies it at that step.

2.69 (phase 4B): the lairs of Voidworm, Graviton and Titanus are unsealed: their arenas and fights are
written by raid_voidworm.py, raid_graviton.py and raid_titanus.py (called from here with this module),
and those of Skullmaw, Dreadfang and Heartbreaker by raid_skullmaw.py, raid_dreadfang.py and raid_heartbreaker.py,
each branch gains its gigaboss objective (role "gigaboss", at the end of its catalog).

Never edit the JSON by hand: run
    python3 tools/levelgen/raid_asterion.py          (the levels, the catalogs, the titan)
    python3 tools/levelgen/raid_asterion.py --en raid_en.json   (also: the English of every
        text, then: python3 tools/i18n/extract.py --merge raid_en.json)
from the repository root (the output is stable: same bytes each run).
"""
import json
import math
import os
import random
import sys

from levelkit import Level, ROOT, T

# Raise when a zone, a catalog or a link changes how the raid plays: every game of an instance
# must have the same (the relay's GO carries "<branch revision>.<titan revision>").
TITAN_REVISION = 1
BRANCH_REVISION = 3  # 2.69 (phase 4B): lairs unsealed, gigaboss objectives (3: Skullmaw, Dreadfang, Heartbreaker too)
RAID_DIR = os.path.join(ROOT, "data", "raids", "asterion")
SERVER_COPY = os.path.join(ROOT, "server", "src", "raidTitan.json")
H = 48
START_POWERS = ["dash", "wave", "shield", "impulse"]
LINK_BIT0 = 24  # the links' bits in a branch's state (the objectives are below)

EN = {}


def tr(fr, en):
    """A French text of the raid, with its English (merged by --en)."""
    if fr in EN and EN[fr] != en:
        raise ValueError("two translations for %r" % fr)
    EN[fr] = en
    return fr


# ------------------------------------------------------------------ the worlds of the branches
THEMES = {
    "crane": dict(planet="confins", ground="sanctum_stone", alt="obsidian", ledge="sanctum_ledge", crumble="ruins",
                  cross="vertebrae", sea=None, glow="#c9a8ff", style="souls",
                  hazard=("Onde psychique", "Psychic wave"), advice=("montez sur les corniches", "climb onto the ledges"),
                  alarm=("Onde psychique ! Restez sur les corniches hautes", "Psychic wave! Stay on the high ledges"),
                  small="glowmite", mid="stalker", flyer="spectre", flyer2="nightwing", heavy="bulwark", guardian="golem",
                  deco_small=["frozen_bones", "skull_rock", "bones_ancient", "rock_small"],
                  deco_tall=["obsidian_spire", "statue_head", "ruin_pillar_broken"], dust="abyss_motes"),
    "bras_gauche": dict(planet="glacior", ground="snow_rock", alt="glacier_ice", ledge="ice_ledge", crumble="ice",
                        cross="ice", sea=None, glow="#7ff6ff", style="plasma",
                        hazard=("Balayage des griffes", "Claw sweep"), advice=("sautez sur les corniches", "jump onto the ledges"),
                        alarm=("Les griffes balaient le sol ! Restez en hauteur", "The claws sweep the floor! Stay up high"),
                        small="frost_mite", mid="snow_stalker", flyer="frost_wing", flyer2="cryo_drone", heavy="frost_bulwark",
                        guardian="ice_golem", deco_small=["snow_rock_small", "ice_crystal", "frost_flower"],
                        deco_tall=["ice_spire", "frozen_statue", "ice_crystal_big", "frost_banner"], dust="snowfall"),
    "torse": dict(planet="noyau", ground="ancient_metal", alt="crystal", ledge="tech_ledge", crumble="ruins", cross="ribs",
                  sea=None, glow="#ff5d8f", style="plasma",
                  hazard=("Pulsation du cœur", "Heart pulse"), advice=("grimpez sur les passerelles", "climb onto the walkways"),
                  alarm=("Le cœur pulse ! Restez sur les passerelles", "The heart pulses! Stay on the walkways"),
                  small="glowmite", mid="stalker", flyer="sentinel", flyer2="etincelle", heavy="bulwark",
                  guardian="eclat_de_roche", deco_small=["crystal_cluster", "rock_small", "pipe_broken"],
                  deco_tall=["crystal_big", "tech_panel", "ruin_pillar"], dust="fireflies"),
    "abdomen": dict(planet="sylvanox", ground="root", alt="root", ledge="root_branch", crumble="ruins", cross="breath",
                    sea="acid", glow="#9dff3a", style="toxic",
                    hazard=("Reflux d'acide", "Acid reflux"), advice=("montez sur les branches", "climb onto the branches"),
                    alarm=("Reflux d'acide ! Restez sur les branches hautes", "Acid reflux! Stay on the high branches"),
                    small="gloop_small", mid="gloop", flyer="wisp", flyer2="bomber", heavy="weaver", guardian="golem",
                    deco_small=["mushroom_cluster", "glow_fern", "glow_flowers", "mushroom_small"],
                    deco_tall=["mushroom_tall", "crystal_big"], dust="spores"),
    "jambe_gauche": dict(planet="aethra", ground="ruin_stone", alt="crystal", ledge="ruin_bridge", crumble="ruins",
                         cross="gravity", sea=None, glow="#9fd8ff", style="abyss",
                         hazard=("Puits de gravité", "Gravity well"), advice=("restez sur les îlots hauts", "stay on the high islets"),
                         alarm=("Puits de gravité ! Restez sur les îlots hauts", "Gravity well! Stay on the high islets"),
                         small="glowmite", mid="stalker", flyer="dragonnet", flyer2="nightwing", heavy="bulwark",
                         guardian="eclat_de_roche", deco_small=["rock_small", "crystal_cluster", "glow_flowers"],
                         deco_tall=["ruin_pillar", "ruin_arch", "ruin_pillar_broken"], dust="aurora_motes"),
    "jambe_droite": dict(planet="pyralis", ground="forge_metal", alt="obsidian", ledge="chain_grate", crumble="basalt",
                         cross="pistons", sea="lava", glow="#ff8a3d", style="embers",
                         hazard=("Onde sismique", "Seismic wave"), advice=("sautez sur les grilles", "jump onto the grates"),
                         alarm=("Onde sismique ! Restez sur les grilles hautes", "Seismic wave! Stay on the high grates"),
                         small="ember_mite", mid="lava_stalker", flyer="forge_drone", flyer2="bomber", heavy="forge_bulwark",
                         guardian="magma_golem", deco_small=["slag_pile", "gear_wreck", "ash_rock_small"],
                         deco_tall=["basalt_column", "obsidian_spire", "steam_pipe"], dust="embers_rising"),
}


# ------------------------------------------------------------------ the branches (catalogs)
class Spec:
    """A branch: its names, zones, objectives, edges and the texts of its locks."""

    def __init__(self, part, title, title_en, short, gigaboss, generators, zones, objectives, edges, locks):
        self.part = part
        self.id = "asterion_" + part
        self.title = title          # « Crâne » (level names)
        self.title_en = title_en
        self.short = short          # level ids: raid_<short>_<n>
        self.gigaboss = gigaboss
        self.generators = generators
        self.zones = zones          # [(id, name, name_en, sub, x, y, inside, event)]
        self.objectives = objectives  # [(id, zone, label, label_en, optional, role, pair)]
        self.edges = edges          # [(a, b, lock, secret)]
        self.locks = locks          # {lock: (hint, hint_en, message, message_en, objective, objective_en)}
        self.theme = THEMES[part]
        self.zone_names = {z[0]: tr(z[1], z[2]) for z in zones}
        self.zone_en = {z[0]: z[2] for z in zones}
        for o in objectives:
            tr(o[2], o[3])

    def lock_of(self, a, b):
        for x, y, lock, _ in self.edges:
            if {x, y} == {a, b}:
                return lock
        raise ValueError("%s: no edge %d-%d" % (self.part, a, b))

    def catalog(self, minutes=None):
        minutes = minutes or {}
        zones = []
        for n, name, _, sub, x, y, inside, event in self.zones:
            if n == 7:  # the long expedition to the lair (lock_arena)
                inside = "Longue expédition sur trois étages : embuscades géantes, écluses et monte-charge à plusieurs, points de contrôle · " + inside
                event = "%s toutes les 75 s dans les galeries basses (étages sûrs)" % self.theme["hazard"][0]
            zones.append({"id": n, "name": name, "sub": sub, "x": x, "y": y, "level": "zone_%d.json" % n, "inside": inside,
                          "event": event, "secret": n == 5, "boss": n == 8,
                          "groups_required": 2 if any(o[1] == n and o[6] for o in self.objectives) else 1,
                          "minutes": minutes.get(n, 0)})
        objectives = [{"id": oid, "index": i, "zone": zone, "label": label, "optional": opt, "role": role, "pair": pair}
                      for i, (oid, zone, label, _, opt, role, pair) in enumerate(self.objectives)]
        edges = [{"a": a, "b": b, "lock": lock, "secret": secret} for a, b, lock, secret in self.edges]
        locks = {k: {"hint": tr(v[0], v[1]), "message": tr(v[2], v[3]), "objective": tr(v[4], v[5])}
                 for k, v in sorted(self.locks.items())}
        return {"id": self.id, "revision": BRANCH_REVISION, "raid": "asterion", "branch": self.part,
                "name": self.title.upper(), "gigaboss": self.gigaboss, "start_zone": 1,
                "generators_needed": self.generators, "lock_texts": locks, "zones": zones, "edges": edges,
                "objectives": objectives}


SEALED_SUB = "GIGABOSS · prochaine version"
ENTRY_SUB = "entrée · 3 points d'apparition"
SECRET_SUB = "zone secrète ◆ relique"

CRANE = Spec(
    "crane", "Crâne", "Skull", "cr", "Skullmaw", 2,
    [(1, "Mâchoire", "Jaw", ENTRY_SUB, 110, 380, "Ennemis : spectres, rôdeurs · arène de la garde", ""),
     (2, "Orbite gauche", "Left orbit", "l'œil gauche à crever", 330, 200,
      "Ennemis : spectres, lucioles d'os · l'œil gauche (un pouvoir de tir)", "Onde psychique toutes les 70 s (corniches hautes sûres)"),
     (3, "Orbite droite", "Right orbit", "l'œil droit · regard laser", 330, 560,
      "Ennemis : rôdeurs, ailes de nuit · regards lasers · l'œil droit", ""),
     (4, "Nerfs optiques", "Optic nerves", "2 nerfs à trancher : 2 groupes", 560, 380,
      "Ennemis : spectres en renfort, rempart · deux nerfs, un par groupe", "Onde psychique toutes les 80 s (corniches hautes sûres)"),
     (5, "Ossuaire oublié", "Forgotten ossuary", SECRET_SUB, 560, 180, "Ennemis : lucioles d'os · la relique de l'ossuaire", ""),
     (6, "Tempe", "Temple", "levier du raccourci vers les nerfs", 790, 560,
      "Ennemis : le golem de la tempe (gardien), spectres en renfort", ""),
     (7, "Voûte crânienne", "Cranial vault", "s'ouvre : les deux yeux crevés", 790, 250,
      "Ennemis : la garde de la voûte (arène en trois vagues)", ""),
     (8, "Antre de Skullmaw", "Skullmaw's lair", "GIGABOSS · miroirs à tenir ensemble", 1000, 400,
      "Le gigaboss Skullmaw : regards traqueurs, flash aveuglant (cachez-vous derrière les colonnes), miroirs à tenir ensemble, ténèbres",
      "Regards lasers sur chaque joueur, flash toutes les 13 à 16 s en phases 2 et 3")],
    [("crane_machoire", 1, "Briser la garde de la mâchoire : vaincre l'arène", "Break the jaw's guard: clear the arena", False, "", ""),
     ("crane_oeil_gauche", 2, "Crever l'œil gauche (un pouvoir de tir)", "Put out the left eye (a shooting power)", False, "generator", ""),
     ("crane_oeil_droit", 3, "Crever l'œil droit (un pouvoir de tir)", "Put out the right eye (a shooting power)", False, "generator", ""),
     ("crane_nerf_gauche", 4, "Trancher le nerf gauche (un groupe)", "Sever the left nerve (one group)", False, "", "nerfs"),
     ("crane_nerf_droit", 4, "Trancher le nerf droit (un autre groupe)", "Sever the right nerve (another group)", False, "", "nerfs"),
     ("crane_ossuaire", 5, "Facultatif : emporter la relique de l'ossuaire", "Optional: take the ossuary's relic", True, "", ""),
     ("crane_tempe", 6, "Abattre le golem de la tempe", "Bring down the temple's golem", False, "", ""),
     ("crane_levier", 6, "Facultatif : ouvrir le passage vers les nerfs (levier)", "Optional: open the way to the nerves (lever)", True,
      "shortcut", ""),
     ("crane_voute", 7, "Faire taire la voûte : vaincre sa garde", "Silence the vault: defeat its guard", False, "", ""),
     # 2.69 (phase 4B): the gigaboss of the branch; accepted once, it destroys the skull.
     ("crane_skullmaw", 8, "Vaincre le gigaboss Skullmaw", "Defeat the gigaboss Skullmaw", False, "gigaboss", "")],
    [(1, 2, "", False), (1, 3, "", False), (2, 4, "", False), (3, 4, "", False), (2, 5, "", True), (3, 6, "", False),
     (4, 6, "shortcut", False), (4, 7, "generators", False), (6, 7, "link:bd_poignet", False),
     (7, 8, "objective:crane_voute", False)],
    {"generators": ("il faut crever les deux yeux", "both eyes must be put out",
                    "Les deux yeux sont crevés : la Voûte crânienne s'ouvre", "Both eyes are out: the Cranial vault opens",
                    "crever les deux yeux (Orbites gauche et droite)", "put out both eyes (left and right Orbits)"),
     "shortcut": ("le levier de la Tempe l'ouvre", "the Temple's lever opens it",
                  "Le passage Nerfs optiques – Tempe est ouvert", "The Optic nerves – Temple way is open",
                  "passage fermé (le levier est dans la Tempe)", "way closed (the lever is in the Temple)")})

BRAS_GAUCHE = Spec(
    "bras_gauche", "Bras gauche", "Left arm", "bg", "Dreadfang", 2,
    [(1, "Épaule gelée", "Frozen shoulder", ENTRY_SUB, 110, 360, "Ennemis : mites de givre, rôdeurs des neiges · arène de la garde", ""),
     (2, "Biceps de givre", "Frost biceps", "plateformes de glace fragiles", 330, 200,
      "Ennemis : golem de glace (gardien), ailes de givre", ""),
     (3, "Tendon glacé", "Frozen tendon", "tourelles-regards · cœur de givre ouest", 330, 520,
      "Ennemis : rôdeurs des neiges, drones cryo · tourelles · cœur de givre ouest",
      "Balayage des griffes toutes les 60 s (terrasses hautes sûres)"),
     (4, "Coude des lames", "Elbow of blades", "2 lames à briser : 2 groupes", 560, 300,
      "Ennemis : rempart de givre, ailes de givre · deux lames, un groupe chacune",
      "Balayage des griffes toutes les 75 s (corniches hautes sûres)"),
     (5, "Crevasse cachée", "Hidden crevasse", SECRET_SUB, 560, 620, "Ennemis : mites de givre · la relique de la crevasse", ""),
     (6, "Avant-bras de givre", "Frost forearm", "cœur de givre est · levier du raccourci", 790, 160,
      "Ennemis : drones cryo, rôdeurs des neiges · cœur de givre est", ""),
     (7, "Poignet de glace", "Ice wrist", "s'ouvre : 2 cœurs de givre", 790, 470,
      "Ennemis : la garde du poignet (arène en trois vagues)", ""),
     (8, "Griffes de Dreadfang", "Dreadfang's claws", "GIGABOSS · trois chaînes à tenir", 1000, 320,
      "Le gigaboss Dreadfang : balayages d'étages entiers, griffes du sol, un joueur cloué à libérer, trois chaînes à tenir ensemble",
      "Balayage des griffes toutes les 7 à 11 s (sautez par-dessus ou changez d'étage)")],
    [("bg_epaule", 1, "Reprendre l'épaule gelée : vaincre sa garde", "Retake the frozen shoulder: defeat its guard", False, "", ""),
     ("bg_golem", 2, "Abattre le golem de glace du biceps", "Bring down the biceps' ice golem", False, "", ""),
     ("bg_coeur_ouest", 3, "Briser le cœur de givre ouest (un pouvoir de tir)", "Break the west frost heart (a shooting power)", False,
      "generator", ""),
     ("bg_lame_haute", 4, "Briser la lame haute (un groupe)", "Break the high blade (one group)", False, "", "lames"),
     ("bg_lame_basse", 4, "Briser la lame basse (un autre groupe)", "Break the low blade (another group)", False, "", "lames"),
     ("bg_relique", 5, "Facultatif : emporter la relique de la crevasse", "Optional: take the crevasse's relic", True, "", ""),
     ("bg_coeur_est", 6, "Briser le cœur de givre est (un pouvoir de tir)", "Break the east frost heart (a shooting power)", False,
      "generator", ""),
     ("bg_levier", 6, "Facultatif : ouvrir le raccourci vers le Tendon (levier)", "Optional: open the shortcut to the Tendon (lever)",
      True, "shortcut", ""),
     ("bg_poignet", 7, "Briser le poignet de glace : vaincre sa garde", "Break the ice wrist: defeat its guard", False, "", ""),
     # 2.69 (phase 4B): the gigaboss of the branch; accepted once, it destroys the left arm.
     ("bg_dreadfang", 8, "Vaincre le gigaboss Dreadfang", "Defeat the gigaboss Dreadfang", False, "gigaboss", "")],
    [(1, 2, "", False), (1, 3, "", False), (2, 4, "", False), (3, 4, "", False), (3, 5, "", True), (2, 6, "", False),
     (3, 6, "shortcut", False), (4, 7, "generators", False), (5, 7, "link:jg_cheville", False),
     (7, 8, "objective:bg_poignet", False)],
    {"generators": ("il faut briser les deux cœurs de givre", "both frost hearts must be broken",
                    "Les deux cœurs de givre sont brisés : le Poignet de glace s'ouvre", "Both frost hearts are broken: the Ice wrist opens",
                    "briser les deux cœurs de givre (Tendon, Avant-bras)", "break both frost hearts (Tendon, Forearm)"),
     "shortcut": ("le levier de l'Avant-bras de givre l'ouvre", "the Frost forearm's lever opens it",
                  "Le raccourci Tendon – Avant-bras de givre est ouvert", "The Tendon – Frost forearm shortcut is open",
                  "raccourci fermé (le levier est dans l'Avant-bras de givre)", "shortcut closed (the lever is in the Frost forearm)")})

TORSE = Spec(
    "torse", "Torse", "Torso", "to", "Heartbreaker", 4,
    [(1, "Clavicule", "Collarbone", ENTRY_SUB, 110, 380, "Ennemis : rôdeurs, mites lumineuses · arène de la garde", ""),
     (2, "Côtes hautes", "Upper ribs", "côtes mouvantes · barrière d'énergie", 330, 200,
      "Ennemis : l'éclat de roche (gardien), sentinelles · barrière d'énergie", ""),
     (3, "Poumon gauche", "Left lung", "le poumon à crever · levier du raccourci", 330, 560,
      "Ennemis : parasites en renfort, étincelles · le poumon (un pouvoir de tir)", ""),
     (4, "Sternum", "Sternum", "2 plaques : 2 groupes", 560, 250,
      "Ennemis : rempart, sentinelles · barrières d'énergie · deux plaques, un groupe chacune",
      "Pulsation du cœur toutes les 45 s (passerelles sûres)"),
     (5, "Reliquaire du cœur", "Heart reliquary", SECRET_SUB, 330, 380, "Ennemis : mites lumineuses · la relique du reliquaire", ""),
     (6, "Côtes basses", "Lower ribs", "2 plaques : 2 groupes", 560, 560,
      "Ennemis : parasites en renfort, rôdeurs · deux plaques, un groupe chacune", "Pulsation du cœur toutes les 50 s (passerelles sûres)"),
     (7, "Cage du cœur", "Heart cage", "s'ouvre : les 4 plaques brisées", 790, 400,
      "Ennemis : la garde de la cage (arène en trois vagues)", ""),
     (8, "Antre de Heartbreaker", "Heartbreaker's lair", "GIGABOSS · 4 plaques, côtes à écarter", 1000, 400,
      "Le gigaboss Heartbreaker : quatre plaques à briser ensemble, battements en rythme, côtes-pièges, côtes à écarter des deux côtés",
      "Ondes du cœur au sol à chaque battement fort (sautez-les)")],
    [("torse_clavicule", 1, "Reprendre la clavicule : vaincre sa garde", "Retake the collarbone: defeat its guard", False, "", ""),
     ("torse_cotes", 2, "Abattre l'éclat de roche des côtes hautes", "Bring down the upper ribs' rock shard", False, "", ""),
     ("torse_poumon", 3, "Crever le poumon gauche (un pouvoir de tir)", "Burst the left lung (a shooting power)", False, "", ""),
     ("torse_levier", 3, "Facultatif : ouvrir le raccourci vers le Sternum (levier)", "Optional: open the shortcut to the Sternum (lever)",
      True, "shortcut", ""),
     ("torse_plaque_nord", 4, "Briser la plaque nord (un groupe)", "Break the north plate (one group)", False, "generator", "plaques_sternum"),
     ("torse_plaque_sud", 4, "Briser la plaque sud (un autre groupe)", "Break the south plate (another group)", False, "generator",
      "plaques_sternum"),
     ("torse_reliquaire", 5, "Facultatif : emporter la relique du reliquaire", "Optional: take the reliquary's relic", True, "", ""),
     ("torse_plaque_est", 6, "Briser la plaque est (un groupe)", "Break the east plate (one group)", False, "generator", "plaques_cotes"),
     ("torse_plaque_ouest", 6, "Briser la plaque ouest (un autre groupe)", "Break the west plate (another group)", False, "generator",
      "plaques_cotes"),
     ("torse_cage", 7, "Ouvrir la cage du cœur : vaincre sa garde", "Open the heart cage: defeat its guard", False, "", ""),
     # 2.69 (phase 4B): the gigaboss of the branch; accepted once, it destroys the torso.
     ("torse_heartbreaker", 8, "Vaincre le gigaboss Heartbreaker", "Defeat the gigaboss Heartbreaker", False, "gigaboss", "")],
    [(1, 2, "", False), (1, 3, "", False), (2, 4, "", False), (3, 4, "shortcut", False), (3, 6, "", False), (2, 5, "", True),
     (4, 7, "generators", False), (6, 7, "generators", False), (7, 8, "objective:torse_cage", False)],
    {"generators": ("il faut briser les quatre plaques du torse", "the torso's four plates must be broken",
                    "Les quatre plaques sont brisées : la Cage du cœur s'ouvre", "The four plates are broken: the Heart cage opens",
                    "briser les quatre plaques (Sternum, Côtes basses)", "break the four plates (Sternum, Lower ribs)"),
     "shortcut": ("le levier du Poumon gauche l'ouvre", "the Left lung's lever opens it",
                  "Le raccourci Poumon gauche – Sternum est ouvert", "The Left lung – Sternum shortcut is open",
                  "raccourci fermé (le levier est dans le Poumon gauche)", "shortcut closed (the lever is in the Left lung)")})

ABDOMEN = Spec(
    "abdomen", "Abdomen", "Abdomen", "ab", "Voidworm", 2,
    [(1, "Diaphragme", "Diaphragm", ENTRY_SUB, 110, 360, "Ennemis : gloops, tisseuses · arène de la garde", ""),
     (2, "Estomac", "Stomach", "bassins d'acide · glande nord", 330, 200,
      "Ennemis : gloops, feux follets · glande nord (un pouvoir de tir)", "Reflux d'acide toutes les 65 s (branches hautes sûres)"),
     (3, "Nid des parasites", "Parasite nest", "le nid à détruire", 330, 520, "Ennemis : la garde du nid (arène en deux vagues)", ""),
     (4, "Intestin", "Intestine", "2 valves : 2 groupes · reflux d'acide", 560, 380,
      "Ennemis : tisseuses, bombardiers · deux valves, un groupe chacune", "Reflux d'acide toutes les 50 s (branches hautes sûres)"),
     (5, "Poche oubliée", "Forgotten pouch", SECRET_SUB, 560, 640, "Ennemis : petits gloops · la relique de la poche", ""),
     (6, "Foie noir", "Black liver", "glande sud · levier du raccourci", 790, 200,
      "Ennemis : tisseuses, feux follets · glande sud (un pouvoir de tir)", ""),
     (7, "Gosier de Voidworm", "Voidworm's gullet", "s'ouvre : 2 glandes", 790, 520,
      "Ennemis : la garde du gosier (arène en trois vagues)", ""),
     (8, "Antre de Voidworm", "Voidworm's lair", "GIGABOSS · Voidworm, 3 phases", 1000, 360,
      "Le gigaboss Voidworm : épingler ses anneaux, libérer les avalés, fuir l'acide", "Reflux d'acide à sa 3e phase (racines hautes sûres)")],
    [("abd_diaphragme", 1, "Reprendre le diaphragme : vaincre sa garde", "Retake the diaphragm: defeat its guard", False, "", ""),
     ("abd_glande_nord", 2, "Crever la glande nord (un pouvoir de tir)", "Burst the north gland (a shooting power)", False, "generator", ""),
     ("abd_nid", 3, "Détruire le nid des parasites : vaincre ses gardes", "Destroy the parasite nest: defeat its guards", False, "", ""),
     ("abd_valve_haute", 4, "Fermer la valve haute (un groupe)", "Close the high valve (one group)", False, "", "valves"),
     ("abd_valve_basse", 4, "Fermer la valve basse (un autre groupe)", "Close the low valve (another group)", False, "", "valves"),
     ("abd_relique", 5, "Facultatif : emporter la relique de la poche", "Optional: take the pouch's relic", True, "", ""),
     ("abd_glande_sud", 6, "Crever la glande sud (un pouvoir de tir)", "Burst the south gland (a shooting power)", False, "generator", ""),
     ("abd_levier", 6, "Facultatif : ouvrir le raccourci vers l'Intestin (levier)", "Optional: open the shortcut to the Intestine (lever)",
      True, "shortcut", ""),
     ("abd_gosier", 7, "Forcer le gosier : vaincre sa garde", "Force the gullet: defeat its guard", False, "", ""),
     ("abd_voidworm", 8, "Vaincre le gigaboss Voidworm", "Defeat the gigaboss Voidworm", False, "gigaboss", "")],
    [(1, 2, "", False), (1, 3, "", False), (2, 4, "", False), (3, 4, "", False), (3, 5, "", True), (2, 6, "", False),
     (4, 6, "shortcut", False), (6, 7, "generators", False), (4, 7, "link:torse_plaques", False),
     (7, 8, "objective:abd_gosier", False)],
    {"generators": ("il faut crever les deux glandes", "both glands must be burst",
                    "Les deux glandes sont crevées : le Gosier de Voidworm s'ouvre", "Both glands are burst: Voidworm's gullet opens",
                    "crever les deux glandes (Estomac, Foie noir)", "burst both glands (Stomach, Black liver)"),
     "shortcut": ("le levier du Foie noir l'ouvre", "the Black liver's lever opens it",
                  "Le raccourci Intestin – Foie noir est ouvert", "The Intestine – Black liver shortcut is open",
                  "raccourci fermé (le levier est dans le Foie noir)", "shortcut closed (the lever is in the Black liver)")})

JAMBE_GAUCHE = Spec(
    "jambe_gauche", "Jambe gauche", "Left leg", "jg", "Graviton", 2,
    [(1, "Hanche", "Hip", ENTRY_SUB, 110, 300, "Ennemis : rôdeurs, mites lumineuses · arène de la garde", ""),
     (2, "Cuisse flottante", "Floating thigh", "puits d'apesanteur · générateur ouest", 330, 180,
      "Ennemis : dragonnets, ailes de nuit · générateur de gravité ouest", "Puits de gravité toutes les 70 s (îlots hauts sûrs)"),
     (3, "Muscle suspendu", "Hanging muscle", "îlots mouvants · gardien", 330, 480,
      "Ennemis : l'éclat de roche (gardien), rôdeurs · îlots mouvants", ""),
     (4, "Genou", "Knee", "2 rotules : 2 groupes", 560, 330,
      "Ennemis : rempart, dragonnets · deux rotules, un groupe chacune", "Puits de gravité toutes les 60 s (îlots hauts sûrs)"),
     (5, "Sanctuaire des poids", "Sanctuary of weights", SECRET_SUB, 560, 620, "Ennemis : mites lumineuses · la relique du sanctuaire", ""),
     (6, "Tibia", "Shin", "générateur est · levier du raccourci", 790, 180,
      "Ennemis : ailes de nuit, rôdeurs · générateur de gravité est", ""),
     (7, "Cheville", "Ankle", "s'ouvre : 2 générateurs de gravité", 790, 480,
      "Ennemis : la garde de la cheville (arène en trois vagues)", ""),
     (8, "Pied de Graviton", "Graviton's foot", "GIGABOSS · Graviton, 3 phases", 1000, 330,
      "Le gigaboss Graviton : couper ses générateurs ensemble, s'abriter quand la gravité s'inverse",
      "Gravité inversée par secteurs (les dalles hautes abritent)")],
    [("jg_hanche", 1, "Reprendre la hanche : vaincre sa garde", "Retake the hip: defeat its guard", False, "", ""),
     ("jg_generateur_ouest", 2, "Couper le générateur de gravité ouest (un pouvoir de tir)",
      "Cut the west gravity generator (a shooting power)", False, "generator", ""),
     ("jg_gardien", 3, "Abattre l'éclat de roche du muscle suspendu", "Bring down the hanging muscle's rock shard", False, "", ""),
     ("jg_rotule_interieure", 4, "Briser la rotule intérieure (un groupe)", "Break the inner kneecap (one group)", False, "", "rotules"),
     ("jg_rotule_exterieure", 4, "Briser la rotule extérieure (un autre groupe)", "Break the outer kneecap (another group)", False, "",
      "rotules"),
     ("jg_relique", 5, "Facultatif : emporter la relique du sanctuaire", "Optional: take the sanctuary's relic", True, "", ""),
     ("jg_generateur_est", 6, "Couper le générateur de gravité est (un pouvoir de tir)",
      "Cut the east gravity generator (a shooting power)", False, "generator", ""),
     ("jg_levier", 6, "Facultatif : ouvrir le raccourci vers le Genou (levier)", "Optional: open the shortcut to the Knee (lever)", True,
      "shortcut", ""),
     ("jg_cheville", 7, "Briser la cheville : vaincre sa garde", "Break the ankle: defeat its guard", False, "", ""),
     ("jg_graviton", 8, "Vaincre le gigaboss Graviton", "Defeat the gigaboss Graviton", False, "gigaboss", "")],
    [(1, 2, "", False), (1, 3, "", False), (2, 4, "", False), (3, 4, "", False), (3, 5, "", True), (2, 6, "", False),
     (4, 6, "shortcut", False), (4, 7, "generators", False), (6, 7, "link:jd_cheville", False),
     (7, 8, "objective:jg_cheville", False)],
    {"generators": ("il faut couper les deux générateurs de gravité", "both gravity generators must be cut",
                    "Les deux générateurs de gravité sont coupés : la Cheville s'ouvre", "Both gravity generators are cut: the Ankle opens",
                    "couper les deux générateurs de gravité (Cuisse, Tibia)", "cut both gravity generators (Thigh, Shin)"),
     "shortcut": ("le levier du Tibia l'ouvre", "the Shin's lever opens it",
                  "Le raccourci Genou – Tibia est ouvert", "The Knee – Shin shortcut is open",
                  "raccourci fermé (le levier est dans le Tibia)", "shortcut closed (the lever is in the Shin)")})

JAMBE_DROITE = Spec(
    "jambe_droite", "Jambe droite", "Right leg", "jd", "Titanus", 2,
    [(1, "Hanche de fer", "Iron hip", ENTRY_SUB, 110, 380, "Ennemis : acariens de braise, rôdeurs de lave · arène de la garde", ""),
     (2, "Cuisse-piston", "Piston thigh", "pistons géants · gardien", 330, 200,
      "Ennemis : golem de magma (gardien), drones de forge · pistons", ""),
     (3, "Fémur fendu", "Split femur", "onde sismique · réacteur ouest", 330, 560,
      "Ennemis : rôdeurs de lave, bombardiers · réacteur ouest (un pouvoir de tir)",
      "Onde sismique toutes les 60 s (grilles hautes sûres)"),
     (4, "Genou-forge", "Forge knee", "2 pistons à bloquer : 2 groupes", 560, 250,
      "Ennemis : rempart de forge, drones de forge · deux pistons, un groupe chacun",
      "Onde sismique toutes les 75 s (grilles hautes sûres)"),
     (5, "Forge oubliée", "Forgotten forge", SECRET_SUB, 560, 640, "Ennemis : acariens de braise · la relique de la forge", ""),
     (6, "Mollet blindé", "Armoured calf", "réacteur est · levier du raccourci", 560, 470,
      "Ennemis : drones de forge, rôdeurs de lave · réacteur est (un pouvoir de tir)", ""),
     (7, "Cheville d'acier", "Steel ankle", "s'ouvre : 2 réacteurs", 790, 360,
      "Ennemis : la garde de la cheville (arène en trois vagues)", ""),
     (8, "Talon de Titanus", "Titanus's heel", "GIGABOSS · Titanus, 3 phases", 1000, 360,
      "Le gigaboss Titanus : sauter ses ondes de choc, briser ses deux entraves ensemble", "Ondes de choc au sol (les grilles sont sûres)")],
    [("jd_hanche", 1, "Reprendre la hanche de fer : vaincre sa garde", "Retake the iron hip: defeat its guard", False, "", ""),
     ("jd_golem", 2, "Abattre le golem de la cuisse-piston", "Bring down the piston thigh's golem", False, "", ""),
     ("jd_reacteur_ouest", 3, "Couper le réacteur ouest (un pouvoir de tir)", "Cut the west reactor (a shooting power)", False,
      "generator", ""),
     ("jd_piston_haut", 4, "Bloquer le piston haut (un groupe)", "Jam the high piston (one group)", False, "", "pistons"),
     ("jd_piston_bas", 4, "Bloquer le piston bas (un autre groupe)", "Jam the low piston (another group)", False, "", "pistons"),
     ("jd_relique", 5, "Facultatif : emporter la relique de la forge", "Optional: take the forge's relic", True, "", ""),
     ("jd_reacteur_est", 6, "Couper le réacteur est (un pouvoir de tir)", "Cut the east reactor (a shooting power)", False, "generator", ""),
     ("jd_levier", 6, "Facultatif : ouvrir le raccourci vers le Genou-forge (levier)",
      "Optional: open the shortcut to the Forge knee (lever)", True, "shortcut", ""),
     ("jd_cheville", 7, "Briser la cheville d'acier : vaincre sa garde", "Break the steel ankle: defeat its guard", False, "", ""),
     ("jd_titanus", 8, "Vaincre le gigaboss Titanus", "Defeat the gigaboss Titanus", False, "gigaboss", "")],
    [(1, 2, "", False), (1, 3, "", False), (2, 4, "", False), (3, 4, "", False), (3, 5, "", True), (3, 6, "", False),
     (4, 6, "shortcut", False), (4, 7, "generators", False), (6, 7, "generators", False), (7, 8, "objective:jd_cheville", False)],
    {"generators": ("il faut couper les deux réacteurs", "both reactors must be cut",
                    "Les deux réacteurs sont coupés : la Cheville d'acier s'ouvre", "Both reactors are cut: the Steel ankle opens",
                    "couper les deux réacteurs (Fémur, Mollet)", "cut both reactors (Femur, Calf)"),
     "shortcut": ("le levier du Mollet blindé l'ouvre", "the Armoured calf's lever opens it",
                  "Le raccourci Genou-forge – Mollet blindé est ouvert", "The Forge knee – Armoured calf shortcut is open",
                  "raccourci fermé (le levier est dans le Mollet blindé)", "shortcut closed (the lever is in the Armoured calf)")})

SPECS = [CRANE, BRAS_GAUCHE, TORSE, ABDOMEN, JAMBE_GAUCHE, JAMBE_DROITE]
# The titan's branches in the order of its body (data/raids/asterion.json).
ORDER = ["crane", "bras_gauche", "bras_droit", "torse", "abdomen", "jambe_gauche", "jambe_droite"]

# ------------------------------------------------------------------ the links between branches (spec §7)
# id, label (French, English), source objectives (branch part, objective), target part, kind, zones, objects
#   open:       a passage of the target whose lock is "link:<id>" opens;
#   disable:    the objects (lasers) of the target's zones are switched off;
#   remove:     the enemies (objects) of the target's zones are gone (silently);
#   hazard_off: the periodic danger of the target's zones stops.
LINKS = [
    ("crane_yeux", "Les yeux du Crâne sont crevés : les tourelles-regards du Tendon glacé (Bras gauche) s'éteignent",
     "The Skull's eyes are out: the Frozen tendon's watcher turrets (Left arm) go dark",
     [("crane", "crane_oeil_gauche"), ("crane", "crane_oeil_droit")], "bras_gauche", "disable", [3], ["tourelle_1", "tourelle_2"]),
    ("crane_nerfs", "Les nerfs optiques sont tranchés : Titanus ne frappe plus le sol, plus d'onde sismique dans la Jambe droite",
     "The optic nerves are severed: Titanus no longer strikes the ground, no more seismic waves in the Right leg",
     [("crane", "crane_nerf_gauche"), ("crane", "crane_nerf_droit")], "jambe_droite", "hazard_off", [3, 4], []),
    ("jg_generateurs", "Les générateurs de gravité sont coupés : les barrières d'énergie du Torse tombent",
     "The gravity generators are cut: the Torso's energy barriers fall",
     [("jambe_gauche", "jg_generateur_ouest"), ("jambe_gauche", "jg_generateur_est")], "torse", "disable", [2, 4],
     ["barriere_1", "barriere_2", "barriere_3"]),
    ("abd_nid", "Le nid de l'Abdomen est détruit : plus de parasites en renfort dans le Torse",
     "The Abdomen's nest is destroyed: no more parasite reinforcements in the Torso",
     [("abdomen", "abd_nid")], "torse", "remove", [3, 6], ["parasite_1", "parasite_2", "parasite_3", "parasite_4"]),
    ("torse_plaques", "Les plaques du Torse sont brisées : l'Intestin de l'Abdomen s'ouvre vers le Gosier",
     "The Torso's plates are broken: the Abdomen's Intestine opens onto the Gullet",
     [("torse", "torse_plaque_nord"), ("torse", "torse_plaque_sud"), ("torse", "torse_plaque_est"), ("torse", "torse_plaque_ouest")],
     "abdomen", "open", [4, 7], []),
    ("jg_cheville", "La cheville gauche est brisée : le titan bascule, la Crevasse cachée du Bras gauche s'ouvre vers le Poignet",
     "The left ankle is broken: the titan tilts, the Left arm's Hidden crevasse opens onto the Wrist",
     [("jambe_gauche", "jg_cheville")], "bras_gauche", "open", [5, 7], []),
    ("jd_reacteurs", "Les réacteurs de la Jambe droite sont coupés : plus de coulée de magma dans le Bras droit",
     "The Right leg's reactors are cut: no more magma flows in the Right arm",
     [("jambe_droite", "jd_reacteur_ouest"), ("jambe_droite", "jd_reacteur_est")], "bras_droit", "hazard_off", [3, 4], []),
    ("bd_poignet", "Le poignet du Bras droit est brisé : la Tempe du Crâne s'ouvre vers la Voûte",
     "The Right arm's wrist is broken: the Skull's Temple opens onto the Vault",
     [("bras_droit", "poignet_verrou")], "crane", "open", [6, 7], []),
    ("jd_cheville", "Titanus fléchit : le Tibia de la Jambe gauche s'ouvre vers la Cheville",
     "Titanus buckles: the Left leg's Shin opens onto the Ankle",
     [("jambe_droite", "jd_cheville")], "jambe_gauche", "open", [6, 7], []),
    ("abd_valves", "Les valves de l'Abdomen sont fermées : les puits de gravité de la Jambe gauche s'éteignent",
     "The Abdomen's valves are closed: the Left leg's gravity wells die out",
     [("abdomen", "abd_valve_haute"), ("abdomen", "abd_valve_basse")], "jambe_gauche", "hazard_off", [2, 4], []),
    ("bg_lames", "Les lames de Dreadfang sont brisées : les spectres en renfort du Crâne se dissipent",
     "Dreadfang's blades are broken: the Skull's reinforcing spectres fade away",
     [("bras_gauche", "bg_lame_haute"), ("bras_gauche", "bg_lame_basse")], "crane", "remove", [4, 6],
     ["renfort_1", "renfort_2", "renfort_3", "renfort_4", "renfort_5"]),
]


# ------------------------------------------------------------------ a zone
class Zone:
    """One sub-zone, built from left to right: a cursor (x, ground row y) moves on as the terrain is laid."""

    def __init__(self, spec, n, description, description_en, height=H):
        self.spec, self.n, self.th = spec, n, spec.theme
        self.H = height
        name = spec.zone_names[n]
        self.L = Level("raid_%s_%d" % (spec.short, n),
                       tr("Asterion · %s · %s" % (spec.title, name), "Asterion · %s · %s" % (spec.title_en, spec.zone_en[n])), 100, height)
        L = self.L
        L.planet(self.th["planet"])
        if self.th["crumble"] != "ruins":
            L.crumble_style = self.th["crumble"]
        d = L.data
        d["description"] = tr(description, description_en)
        d["difficulty"] = 4
        d["startPowers"] = list(START_POWERS)
        d["killY"] = (height - 1) * T
        d["timer"] = {"mode": "unlimited", "limit": 3600, "onExpire": "continue", "referenceTime": 600, "challengeMaxDeaths": 0}
        d["medals"] = {"bronze": {}, "silver": {"maxTime": 3000}, "gold": {"maxTime": 2400},
                       "platinum": {"maxTime": 1800, "maxDeaths": 0}}
        d["maxPowers"] = 4
        self.raid = {"branch": spec.id, "zone": n, "spawns": {}, "passages": [], "objectives": [], "magma": None, "sealed": None}
        d["metadata"] = {"generator": "tools/levelgen/raid_asterion.py", "raid": self.raid}
        self.rng = random.Random(hash_seed(spec.part, n))
        self.x, self.y = 0, 36
        self.doors = {}
        self.ground = {}  # column -> top row of the ground (for the decor)

    # ------------------------------------------------------------ terrain
    def rock(self, x0, top, x1, mat=None):
        for x in range(x0, x1):
            self.ground[x] = min(self.ground.get(x, self.H), top)
        return self.L.solid(x0, top, x1, self.H, mat or self.th["ground"])

    def slab(self, x0, y0, x1, y1, mat=None):
        return self.L.solid(x0, y0, x1, y1, mat or self.th["alt"])

    def ledge(self, x0, x1, y):
        return self.L.oneway(x0, x1, y, self.th["ledge"])

    def rename(self, oid, new):
        for o in self.L.data["objects"]:
            if o["id"] == oid:
                o["id"] = new
                return new
        raise KeyError(oid)

    def enemy(self, kind, x, y=None, patrol=6, flip=False, dormant=False, name=None):
        k = self.th.get(kind, kind)
        oid = self.L.enemy(k, x, self.y if y is None else y, patrol, flip, dormant=dormant)
        return self.rename(oid, name) if name else oid

    def flat(self, n, dy=0):
        """Ground from the cursor, n tiles wide, dy rows higher (negative) or lower; returns its x."""
        self.y += dy
        x0 = self.x
        self.rock(x0, self.y, x0 + n)
        self.x += n
        return x0

    def up(self, steps, w=10):
        """Terraces climbing 3 rows each."""
        for _ in range(steps):
            self.flat(w, -3)

    def down(self, steps, w=10):
        for _ in range(steps):
            self.flat(w, 3)

    def cross(self, n):
        """The branch's way over a gap of n tiles (the void, or its acid / lava, under it)."""
        x0, x1, y, kind = self.x, self.x + n, self.y, self.th["cross"]

        def spaced(w, gap):
            k = max(1, round((n - gap) / (w + gap)))
            step = (n - gap) / k
            return [int(round(x0 + gap + i * step)) for i in range(k)], int(round(step))

        if kind == "vertebrae":
            xs, _ = spaced(4, 3)
            for i, x in enumerate(xs):
                self.ledge(x, x + 4, y - (3 if i % 2 else 0))
        elif kind == "ice":
            xs, _ = spaced(3, 3)
            for x in xs:
                self.L.crumble(x, y, 3, delay=0.5, respawn=3.0, style="ice")
        elif kind == "ribs":
            xs, _ = spaced(4, 4)
            for x in xs:
                self.L.moving(x, y, 4, 2, 0, speed=28, wait=0.6, phase=0.0)
        elif kind == "breath":
            xs, _ = spaced(4, 3)
            for i, x in enumerate(xs):
                self.L.moving(x, y, 4, 0, -3, speed=26, wait=1.0, phase=(i % 2) * 0.5)
        elif kind == "gravity":
            xs, step = spaced(4, 4)
            for x in xs:
                self.ledge(x, x + 4, y - 1)
            for x in [x0] + [x + 4 for x in xs]:
                if min(x + 4, x1) - x >= 2:
                    self.L.updraft(x, min(x + 4, x1), y - 8, self.H, strength=240, vent=False)
        elif kind == "pistons":
            xs, _ = spaced(5, 3)
            for i, x in enumerate(xs):
                self.L.moving(x, y + 2, 5, 0, -5, speed=34, wait=0.8, phase=(i % 3) / 3.0)
        self.x = x1
        return x0

    def scatter(self, x0, x1, density=0.3):
        """Decor of the branch's world along the ground row of the cursor."""
        for x in range(x0 + 1, x1 - 1, 3):
            if self.rng.random() < density and x in self.ground and x + 1 in self.ground and self.ground[x] == self.ground[x + 1]:
                pool = self.th["deco_tall"] if self.rng.random() < 0.35 else self.th["deco_small"]
                self.L.decor(self.rng.choice(pool), x + 1, self.ground[x], flip=self.rng.random() < 0.5, alpha=0.9)

    def lights(self, x0, x1, y, every=26):
        for x in range(x0 + 6, x1, every):
            self.L.light(x, y, self.th["glow"], 110, flicker=0.15)

    # ------------------------------------------------------------ raid metadata
    def start(self, points):
        self.L.start(points[0][0], points[0][1])
        self.raid["spawns"]["start"] = [[x * T, y * T] for x, y in points]

    def arrive(self, src, x, y):
        self.raid["spawns"][str(src)] = [[x * T, y * T]]

    def lockdoor(self, to, x, y_floor):
        lock = self.spec.lock_of(self.n, to)
        if not lock or self.n == 8:
            return None
        door = self.L.door(x, y_floor - 4, 4)
        self.doors[to] = door
        return door

    def passage(self, to, x0, y_floor, door=None):
        rect = [x0 * T, (y_floor - 4) * T, 4 * T, 4 * T]
        name, name_en = self.spec.zone_names[to], self.spec.zone_en[to]
        p = {"id": "p%d_%d" % (self.n, to), "to": to, "rect": rect, "lock": self.spec.lock_of(self.n, to), "door": door or ""}
        self.raid["passages"].append(p)
        self.L.sign(x0 + 2, y_floor, tr("Passage vers : %s" % name, "Way to: %s" % name_en))
        self.L.light(x0 + 2, y_floor - 2, "#9fd8ff", 70, flicker=0.05)
        self.L.emitter(x0, y_floor - 4, x0 + 4, y_floor, "fireflies", 1.2)
        return p

    def objective(self, oid, source, trigger):
        assert any(o[0] == oid and o[1] == self.n for o in self.spec.objectives), oid
        self.raid["objectives"].append({"id": oid, "source": source, "trigger": trigger})

    def hazard(self, x0, x1, period, first, warn=3.0, duration=6.0):
        """The branch's periodic danger over the ground rows [y-2, y) from x0 to x1 (the ledges above are safe)."""
        th = self.th
        props = {"damage": 2, "interval": 0.6, "visible": True, "active": False, "bounce": True}
        if th["style"] != "toxic":
            props["style"] = th["style"]
        dz = self.L.obj("damage_zone", [x0 * T, (self.y - 2) * T, (x1 - x0) * T, 2 * T], "gameplay", props, oid="magma_flow")
        self.raid["magma"] = {"zone": dz, "period": period, "first": first, "warn": warn, "duration": duration,
                              "name": tr(*th["hazard"]), "hint": tr(*th["advice"]), "message": tr(*th["alarm"])}
        return dz

    def safe_ledges(self, x0, x1, every=14):
        """Ledges 4 rows up and walkways 7 rows up along a hazard: the safe places."""
        for x in range(x0 + 3, x1 - 6, every):
            self.ledge(x, x + 5, self.y - 4)
            if x + every // 2 + 5 < x1:
                self.ledge(x + every // 2, x + every // 2 + 5, self.y - 7)

    def receptor(self, oid, x, y=None, sign=None):
        r = self.L.receptor(x, self.y if y is None else y, "")
        self.objective(oid, r, "activate")
        if sign:
            self.L.sign(x - 4, self.y if y is None else y, sign)
        self.L.light(x, (self.y if y is None else y) - 3, self.th["glow"], 90, flicker=0.1)
        return r

    def lasers(self, xs, names, top_gap=9):
        """Vertical beams from a ceiling block, crossing the ground row (on 1.6 s, off 1.6 s)."""
        out = []
        for i, (x, name) in enumerate(zip(xs, names)):
            self.slab(x - 1, self.y - top_gap - 1, x + 2, self.y - top_gap)
            oid = self.L.laser(x + 0.5, self.y - top_gap, top_gap, rotation=90, on=1.6, off=1.6, warn=0.6, phase=0.5 * i)
            out.append(self.rename(oid, name))
        return out

    def arena(self, x0, x1, waves, oid, text, text_en, top=14):
        ws = []
        for wave in waves:
            ws.append([self.enemy(kind, x, None if dy is None else self.y - dy, patrol=5, flip=flip, dormant=True)
                       for kind, x, dy, flip in wave])
        a = self.L.combat(x0, self.y - top, x1, self.y, waves=";".join(",".join(w) for w in ws), lock=False)
        self.objective(oid, a, "cleared")
        self.L.event(a, "cleared", [{"type": "hint", "params": {"text": tr(text, text_en), "duration": 3.5}}], delay=0.6)
        return a

    # ------------------------------------------------------------ the two ends
    def west(self, low=None, high=None, y=36, start=True):
        self.y = y
        width = 32 if high else 16
        self.rock(0, y, width)
        if low:
            self.passage(low, 1, y, self.lockdoor(low, 7, y))
            self.arrive(low, 9, y)
        if high:
            self.slab(0, y - 12, 22, y - 10)
            self.ledge(24, 29, y - 3)
            self.ledge(25, 30, y - 6)
            self.ledge(23, 27, y - 9)
            self.passage(high, 1, y - 12, self.lockdoor(high, 7, y - 12))
            self.arrive(high, 9, y - 12)
        if start:
            self.start([(9, y), (12, y)])
        self.x = width

    def east(self, low=None, high=None):
        y = self.y
        width = 34 if high else 16
        x0 = self.x
        W = x0 + width
        self.rock(x0, y, W)
        if low:
            self.passage(low, W - 6, y, self.lockdoor(low, W - 9, y))
            self.arrive(low, W - 13, y)
        if high:
            self.slab(W - 22, y - 12, W, y - 10)
            self.ledge(W - 30, W - 25, y - 3)
            self.ledge(W - 31, W - 26, y - 6)
            self.ledge(W - 29, W - 24, y - 9)
            self.passage(high, W - 6, y - 12, self.lockdoor(high, W - 9, y - 12))
            self.arrive(high, W - 14, y - 12)
        if not low and not high:
            self.L.decor(self.th["deco_tall"][0], W - 4, y)
        self.x = W

    def secret_island(self, to, x, y, message, message_en):
        """A hidden ledge with its passage (x, y: the island's left end and its top row)."""
        self.slab(x, y, x + 12, y + 2)
        self.L.secret(x - 2, y - 8, x + 14, y + 2, tr(message, message_en))
        self.passage(to, x + 1, y)
        self.arrive(to, x + 8, y)

    def save(self):
        W = self.x
        d = self.L.data
        d["bounds"] = [0, 0, W * T, self.H * T]
        sea = self.th["sea"]
        if sea:
            self.L.liquid(0, W, self.H - 3, self.H, sea)
            if sea == "lava":
                for x in range(0, W, 70):
                    self.L.emitter(x, self.H - 6, min(W, x + 70), self.H - 3, "lava_bubbles", 2)
        for x in range(0, W, 70):
            self.L.emitter(x, 4, min(W, x + 70), 26, self.th["dust"], 1.2)
        ids = [o["id"] for o in d["objects"]]
        assert len(ids) == len(set(ids)), "duplicate ids in %s zone %d" % (self.spec.part, self.n)
        for o in self.raid["objectives"]:
            assert o["source"] in ids, o
        for p in self.raid["passages"]:
            assert p["rect"][0] + p["rect"][2] <= W * T, p
        out = os.path.join(RAID_DIR, self.spec.part)
        os.makedirs(out, exist_ok=True)
        path = os.path.join(out, "zone_%d.json" % self.n)
        write_json(path, d)
        return d


def hash_seed(part, n):
    h = 2166136261
    for c in ("%s:%d" % (part, n)).encode():
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h


def write_json(path, d):
    text = json.dumps(d, ensure_ascii=False, indent=1) + "\n"
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote %s" % os.path.relpath(path, ROOT))


# ------------------------------------------------------------------ shared zone shapes
def hub(spec, desc, desc_en, objective, guard_text, east_low, east_high, sign, sign_en):
    """Zone 1: the entry, three spawn points, the arena of the guard, two ways on."""
    z = Zone(spec, 1, desc, desc_en)
    z.y = 36
    z.rock(0, 36, 34)
    z.x = 34
    z.start([(8, 36), (14, 36), (20, 36)])
    z.L.sign(26, 36, tr(sign, sign_en))
    z.scatter(2, 32, 0.35)
    z.flat(6, 2)
    x0 = z.flat(44)
    z.arena(x0 + 3, x0 + 41, [[("small", x0 + 9, None, False), ("small", x0 + 17, None, True), ("mid", x0 + 29, None, True)],
                              [("flyer", x0 + 13, 8, False), ("mid", x0 + 23, None, False), ("heavy", x0 + 35, None, True)]],
            objective, guard_text[0], guard_text[1])
    z.ledge(x0 + 11, x0 + 17, z.y - 5)
    z.ledge(x0 + 27, x0 + 33, z.y - 5)
    z.scatter(x0 + 1, x0 + 43, 0.25)
    z.L.orb("health_orb", x0 + 46, z.y - 5)
    z.flat(3, -3)
    z.east(east_low, east_high)
    z.lights(0, z.x, 30)
    return z.save()


def secret(spec, back, relic, relic_name, relic_name_en, desc, desc_en, east_low=None):
    """Zone 5: the secret zone, its relic high at the end (and maybe a way the titan opens)."""
    z = Zone(spec, 5, desc, desc_en)
    z.L.solid(0, 0, 100 if east_low else 80, 6, spec.theme["alt"])
    z.west(low=back, y=34)
    z.flat(8, 2)
    z.enemy("small", z.x - 4)
    z.cross(14)
    z.flat(12, -3)
    z.enemy("mid", z.x - 6, flip=True)
    z.L.crumble(z.x + 2, z.y, 3)
    z.L.crumble(z.x + 7, z.y, 3)
    z.x += 12
    x0 = z.flat(14, -3)
    r = z.L.relic(x0 + 7, z.y - 3, tr(relic_name, relic_name_en))
    z.objective(relic, r, "collected")
    z.L.light(x0 + 7, z.y - 4, "#c9a8ff", 90, flicker=0.2)
    if east_low:
        z.flat(4)
        z.east(east_low)
    else:
        z.flat(4)
    z.scatter(2, z.x - 2, 0.35)
    return z.save()


# ------------------------------------------------------------------ zone 7: the long expedition to the lair
# The route before each gigaboss (spec of 2.68): a long platforming level for 8 players (about
# 15 minutes even with 8, see estimate_minutes), on THREE FLOORS so 8 players never pile up (low
# galleries with the branch's crossings and danger, a middle storey, a high storey; wells of ledges
# join them at every sector), co-op mechanisms (locks whose doors open on every floor when several
# players stand together on their plate, a goods lift that starts with several players on it),
# checkpoints on every floor (the whole group down: the zone starts again at the last one, the
# players down get up there), and giant ambushes on the three floors at once (the ambush system:
# kept as it is when the group falls, enemy counter, arrows to the enemies off screen).
LOW, MID, HIGH = 60, 46, 32  # the floors' top rows (the middle and high ones are slabs 2 rows thick)
H7 = 66


class Expedition:
    def __init__(self, z):
        self.z, self.L, self.th = z, z.L, z.th
        self.coops, self.checkpoints = [], []

    def slabs(self, x0, x1, mid=True, high=True):
        if x1 <= x0:
            return
        if mid:
            self.z.slab(x0, MID, x1, MID + 2)
        if high:
            self.z.slab(x0, HIGH, x1, HIGH + 2)

    def well(self, x):
        """An 8-wide well of ledges joining the three floors (the slabs leave it open)."""
        for i, y in enumerate((57, 54, 51, 48, 43, 40, 37, 34)):
            a = x if i % 2 == 0 else x + 4
            self.z.ledge(a, a + 4, y)

    def enemy(self, kind, x, floor, flyer=False, dormant=False, flip=False):
        y = floor - (6 if flyer else 0)
        return self.z.enemy(kind, x, y, patrol=5, flip=flip, dormant=dormant)

    def lights(self, x0, x1):
        for f in (LOW, MID, HIGH):
            self.L.light((x0 + x1) // 2, f - 4, self.th["glow"], 120, flicker=0.15)

    def hall(self, x, hazard=False):
        """Three lanes, 90 wide: the low one crosses two gaps the branch's way, the middle one is
        held by its walkers, the high one by flyers, over brittle platforms."""
        z, w = self.z, 90
        self.well(x)
        self.slabs(x + 8, x + 40)
        self.slabs(x + 40, x + 52, high=False)
        self.slabs(x + 52, x + w)
        self.L.crumble(x + 42, HIGH, 3)
        self.L.crumble(x + 47, HIGH, 3)
        z.rock(x, LOW, x + 20)
        z.x, z.y = x + 20, LOW
        z.cross(22)
        z.rock(x + 42, LOW, x + 58)
        z.x, z.y = x + 58, LOW
        z.cross(20)
        z.rock(x + 78, LOW, x + w)
        if hazard:
            z.y = LOW
            z.hazard(x, x + w, period=75.0, first=40.0)
        self.enemy("small", x + 10, LOW)
        self.enemy("small", x + 50, LOW, flip=True)
        self.enemy("mid", x + 84, LOW)
        self.enemy("mid", x + 30, MID)
        self.enemy("heavy", x + 66, MID, flip=True)
        self.enemy("flyer", x + 24, HIGH, flyer=True)
        self.enemy("flyer2", x + 70, HIGH, flyer=True, flip=True)
        self.L.orb("health_orb", x + 45, MID - 2)
        z.scatter(x + 1, x + 19, 0.3)
        self.lights(x, x + w)
        return x + w

    def ambush(self, x, waves_n, objective=None, text=None, w=100):
        """A giant ambush on the three floors at once: each wave comes on every floor."""
        z, mid_well = self.z, x + w // 2 - 4
        self.well(x)
        self.well(mid_well)
        self.slabs(x + 8, mid_well)
        self.slabs(mid_well + 8, x + w)
        z.rock(x, LOW, x + w)
        # Where walkers stand (never in a well), where flyers hover.
        spots = [x + 12 + k * 9 for k in range((w - 20) // 9) if not (mid_well - 2 <= x + 12 + k * 9 <= mid_well + 9)]
        plan = [  # (floor, kinds) per wave, the waves growing
            [(LOW, ["small", "small", "mid"]), (MID, ["small", "mid"]), (HIGH, ["flyer", "small"])],
            [(LOW, ["mid", "mid", "small"]), (MID, ["heavy", "small"]), (HIGH, ["flyer2", "mid"])],
            [(LOW, ["heavy", "mid", "small"]), (MID, ["mid", "mid"]), (HIGH, ["flyer", "flyer2"])],
            [(LOW, ["guardian", "small"]), (MID, ["heavy", "mid"]), (HIGH, ["flyer", "heavy"])],
        ]
        waves = []
        for k in range(waves_n):
            wave = []
            for f, (floor, kinds) in enumerate(plan[k % len(plan)]):
                for i, kind in enumerate(kinds):
                    sx = spots[(k * 5 + f * 3 + i * 4) % len(spots)]
                    fly = kind.startswith("flyer")
                    wave.append(self.enemy(kind, sx, floor, flyer=fly, dormant=True, flip=(i + k) % 2 == 1))
            waves.append(wave)
        a = self.L.combat(x + 2, HIGH - 12, x + w - 2, LOW, waves=";".join(",".join(v) for v in waves), lock=False)
        if objective:
            z.objective(objective, a, "cleared")
        if text:
            self.L.event(a, "cleared", [{"type": "hint", "params": {"text": tr(*text), "duration": 3.5}}], delay=0.6)
        self.L.orb("health_orb", x + w // 2, LOW - 2)
        self.L.orb("health_orb", x + w // 2 - 20, MID - 2)
        self.lights(x, x + w)
        return x + w, a

    def lock(self, x, need, n):
        """A lock: a wall across the three floors, a door on each; they open together when `need`
        players (fewer when fewer are in play) stand together on its plate."""
        z, w = self.z, 30
        z.rock(x, LOW, x + w)
        self.slabs(x, x + w)
        wx = x + 20
        for y0, y1 in ((18, HIGH - 4), (HIGH, MID - 4), (MID, LOW - 4)):
            z.slab(wx, y0, wx + 1, y1)
        doors = [self.L.door(wx, f - 4, 4) for f in (HIGH, MID, LOW)]
        rect = [(x + 6) * T, (LOW - 4) * T, 10 * T, 4 * T]
        self.coops.append({"id": "ecluse_%d" % n, "rect": rect, "players": need, "targets": doors, "action": "open",
                           "label": tr("Écluse", "Lock"),
                           "message": tr("L'écluse s'ouvre : ses portes cèdent à tous les étages", "The lock opens: its doors give way on every floor")})
        self.L.sign(x + 3, LOW, tr("Écluse : il faut %d joueurs ensemble sur la plaque pour ouvrir ses portes à tous les étages." % need,
                                    "Lock: %d players must stand together on the plate to open its doors on every floor." % need))
        self.L.emitter(x + 6, LOW - 2, x + 16, LOW, "fireflies", 3)
        self.L.light(x + 11, LOW - 2, "#ffd27a", 90, flicker=0.2)
        return x + w

    def lift(self, x, need):
        """A goods lift up a wall that bars the three floors: it starts with `need` players on it,
        then goes up and down by itself for the others."""
        z, w = self.z, 40
        z.rock(x, LOW, x + 21)
        z.rock(x + 21, LOW + 3, x + 31)  # a shallow pit under the lift
        z.rock(x + 31, LOW, x + w)
        self.slabs(x, x + 18)
        z.slab(x + 31, 18, x + 33, LOW)  # the wall
        self.slabs(x + 33, x + w)
        lift = self.L.moving(x + 21, LOW, 9, 0, -(LOW - 16), speed=60, wait=2.0, active=False, triggered=True)
        for i, y in enumerate((21, 24, 27, 30)):  # down from the wall's top to the high floor
            a = x + 33 if i % 2 == 0 else x + 36
            z.ledge(a, a + 4, y)
        rect = [(x + 21) * T, (LOW - 4) * T, 9 * T, 4 * T]
        self.coops.append({"id": "monte_charge", "rect": rect, "players": need, "targets": [lift], "action": "activate",
                           "label": tr("Monte-charge", "Goods lift"),
                           "message": tr("Le monte-charge démarre : il fait ensuite la navette tout seul",
                                         "The goods lift starts: it then shuttles up and down by itself")})
        self.L.sign(x + 14, LOW, tr("Monte-charge : il démarre quand %d joueurs sont dessus, puis fait la navette tout seul." % need,
                                    "Goods lift: it starts when %d players stand on it, then shuttles by itself." % need))
        self.L.light(x + 25, LOW - 3, "#ffd27a", 100, flicker=0.2)
        return x + w

    def checkpoint(self, x, n):
        """Checkpoints on the three floors (the one touched last is where the group starts again)."""
        z, w = self.z, 12
        z.rock(x, LOW, x + w)
        self.slabs(x, x + w)
        for f in (LOW, MID, HIGH):
            cp = self.L.checkpoint(x + 6, f, tr("Expédition, étape %d" % n, "Expedition, stage %d" % n))
            self.checkpoints.append(cp)
            z.raid["spawns"]["cp:" + cp] = [[(x + 6) * T, f * T]]
        return x + w


def lock_arena(spec, west_low, west_high, objective, waves_kinds, desc, desc_en, text, text_en):
    """Zone 7: the long expedition (three floors, ambushes, locks, a lift, checkpoints) whose last
    giant ambush, the lair's guard, opens the gigaboss' door."""
    del waves_kinds  # the expedition's ambushes use the branch's whole roster
    z = Zone(spec, 7, desc + " Une longue expédition sur trois étages : embuscades géantes, écluses et monte-charge à plusieurs.",
             desc_en + " A long expedition over three floors: giant ambushes, locks and a goods lift for several players.", height=H7)
    e = Expedition(z)
    # The west end: the low gallery and the middle storey, their passages.
    z.rock(0, LOW, 24)
    z.slab(0, MID, 16, MID + 2)
    z.passage(west_low, 1, LOW, z.lockdoor(west_low, 7, LOW))
    z.arrive(west_low, 9, LOW)
    z.passage(west_high, 1, MID, z.lockdoor(west_high, 7, MID))
    z.arrive(west_high, 9, MID)
    z.start([(9, LOW), (12, LOW)])
    for i, y in enumerate((57, 54, 51, 48)):
        z.ledge(16 if i % 2 == 0 else 20, (16 if i % 2 == 0 else 20) + 4, y)
    z.L.sign(13, LOW, tr("L'expédition vers l'antre : trois étages, restez répartis. Les points de contrôle relèvent les joueurs à terre.",
                         "The expedition to the lair: three floors, stay spread out. The checkpoints get the players who are down back up."))
    x = 24
    x = e.hall(x)
    x = e.checkpoint(x, 1)
    x = e.lock(x, 3, 1)
    x, _ = e.ambush(x, 4, text=("Première embuscade repoussée !", "First ambush driven back!"))
    x = e.hall(x, hazard=True)
    x = e.lift(x, 4)
    x = e.checkpoint(x, 2)
    x, _ = e.ambush(x, 4, text=("Deuxième embuscade repoussée !", "Second ambush driven back!"))
    x = e.hall(x)
    x = e.lock(x, 3, 2)
    x = e.hall(x)
    x = e.checkpoint(x, 3)
    x, _ = e.ambush(x, 4, text=("Troisième embuscade repoussée : la garde de l'antre approche !",
                                "Third ambush driven back: the lair's guard draws near!"))
    x = e.hall(x)
    x = e.checkpoint(x, 4)
    x, _ = e.ambush(x, 4, objective=objective, w=110)
    z.rock(x, LOW, x + 4)
    z.x, z.y = x + 4, LOW
    z.east(8)
    door = z.doors[8]
    z.L.event(z.raid["objectives"][0]["source"], "cleared",
              [{"type": "open", "target": door},
               {"type": "hint", "params": {"text": tr(text, text_en), "duration": 3.5}}], delay=0.5)
    z.raid["coop"] = e.coops
    z.raid["checkpoints"] = e.checkpoints
    return z.save()


# ------------------------------------------------------------------ zone 8: the gigaboss' arena (sealed)
ARENA_W, ARENA_FLOOR = 200, 62
H8 = 72


def lair(spec, desc, desc_en):
    """Zone 8: an antechamber, then the gigaboss' arena behind its sealed door: enormous (more than
    two screens wide, five storeys of platforms, a floor for 8 players), its bounds declared in
    metadata.raid.arena for the fight that comes in a next version."""
    z = Zone(spec, 8, desc, desc_en, height=H8)
    th = spec.theme
    z.west(low=7, y=ARENA_FLOOR)
    x0 = z.flat(28)
    z.L.sign(x0 + 6, ARENA_FLOOR, tr("%s — prochaine version. La porte de l'antre restera scellée jusque-là." % spec.gigaboss,
                                     "%s — next version. The lair's door stays sealed until then." % spec.gigaboss))
    dx = z.x  # the sealed door, in the wall that closes the arena
    door = z.L.door(dx, ARENA_FLOOR - 12, 12)
    z.slab(dx, 6, dx + 2, ARENA_FLOOR - 12)
    ax0, ax1 = dx + 2, dx + 2 + ARENA_W
    z.rock(dx, ARENA_FLOOR, ax1 + 2)
    z.slab(ax0, 4, ax1, 6)          # the ceiling
    z.slab(ax1, 4, ax1 + 2, ARENA_FLOOR)  # the far wall
    # Five storeys of platforms, staggered, and two low pillars to take cover behind.
    for k, row in enumerate((ARENA_FLOOR - 6, ARENA_FLOOR - 12, ARENA_FLOOR - 18, ARENA_FLOOR - 24, ARENA_FLOOR - 30)):
        for px in range(ax0 + 8 + (12 if k % 2 else 0), ax1 - 12, 26):
            z.ledge(px, px + 10, row)
    for px in (ax0 + ARENA_W // 3, ax0 + 2 * ARENA_W // 3):
        z.slab(px, ARENA_FLOOR - 4, px + 4, ARENA_FLOOR)
    for px in range(ax0 + 10, ax1, 30):
        z.L.light(px, ARENA_FLOOR - 20, th["glow"], 160, flicker=0.2)
    z.x = ax1 + 2
    label = tr("%s — prochaine version" % spec.gigaboss, "%s — next version" % spec.gigaboss)
    z.raid["sealed"] = {"door": door, "label": label}
    z.raid["arena"] = {"rect": [ax0 * T, 6 * T, ARENA_W * T, (ARENA_FLOOR - 6) * T], "floor": ARENA_FLOOR * T, "door": door,
                       "entry": [(ax0 + 6) * T, ARENA_FLOOR * T], "players": 8}
    z.L.decor(th["deco_tall"][0], x0 + 2, ARENA_FLOOR)
    z.L.decor(th["deco_tall"][-1], dx - 3, ARENA_FLOOR, flip=True)
    z.L.light(dx - 2, ARENA_FLOOR - 6, "#ffd27a", 160, flicker=0.3)
    z.L.emitter(dx - 14, ARENA_FLOOR - 16, dx, ARENA_FLOOR, "embers_rising" if spec.part == "jambe_droite" else "fireflies", 2.5)
    return z.save()


# ------------------------------------------------------------------ how long a zone takes
# A group of 8 moves at the pace of its slowest (GROUP_SPEED tiles a second, the mechanisms and
# the regrouping counted apart), deals GROUP_DPS hit points a second to enemies spread over the
# floors, loses WAVE_TIME at each wave of an ambush (it appears, telegraphs) and COOP_TIME at each
# co-op mechanism (gathering the players; a lift adds its ride). With one player (SOLO_DPS), the
# same model gives the campaign's reference times within ±40 % (calibrate()).
GROUP_SPEED, GROUP_DPS, SOLO_DPS, WAVE_TIME, COOP_TIME = 2.0, 4.0, 1.5, 8.0, 30.0
_HP = {}


def enemy_hp(kind):
    if kind not in _HP:
        with open(os.path.join(ROOT, "data", "enemies", kind + ".json"), encoding="utf-8") as f:
            _HP[kind] = json.load(f).get("hp", 3)
    return _HP[kind]


def estimate_minutes(level, dps=GROUP_DPS):
    width = level["bounds"][2] / T
    hp = sum(enemy_hp(o["props"]["enemy"]) for o in level["objects"] if o["type"] == "enemy" and not o["props"].get("boss"))
    waves = sum(len([w for w in o["props"].get("waves", "").split(";") if w]) for o in level["objects"] if o["type"] == "combat_zone")
    t = width / GROUP_SPEED + hp / dps + waves * WAVE_TIME
    for c in level["metadata"].get("raid", {}).get("coop", []):
        t += COOP_TIME
        for o in level["objects"]:
            if o["id"] in c["targets"] and o["type"] == "moving_platform":
                t += 2 * abs(o["props"]["dy"]) / max(1.0, o["props"]["speed"])  # up and back down for the others
    return t / 60.0


def calibrate():
    """The model, with one player, against the campaign's reference times (ground levels)."""
    for lid in ("pyralis_01", "pyralis_02", "pyralis_03", "glacior_01", "glacior_02", "glacior_03", "sylvanox_02", "sylvanox_03"):
        with open(os.path.join(ROOT, "data", "levels", lid + ".json"), encoding="utf-8") as f:
            d = json.load(f)
        ratio = estimate_minutes(d, SOLO_DPS) * 60.0 / d["timer"]["referenceTime"]
        assert 0.6 <= ratio <= 1.4, (lid, ratio)


# ======================================================================== CRÂNE (Skullmaw)
def crane():
    s = CRANE
    hub(s, "La mâchoire du titan, porte du crâne. Trois points d'apparition, une garde d'os à disperser, puis deux orbites.",
        "The titan's jaw, gate of the skull. Three spawn points, a bone guard to scatter, then two orbits.",
        "crane_machoire", ("La mâchoire est à nous ! Orbite gauche en haut, orbite droite en bas.",
                           "The jaw is ours! Left orbit up high, right orbit down below."), 3, 2,
        "Asterion, crâne. Les deux yeux crevés ouvrent la voûte ; la carte de la branche montre où sont les groupes.",
        "Asterion, skull. Putting out both eyes opens the vault; the branch map shows where the groups are.")

    z = Zone(s, 2, "L'orbite gauche, où l'œil du titan fixe les intrus. Une onde psychique balaie le sol ; une fente cache un ossuaire.",
             "The left orbit, where the titan's eye stares at intruders. A psychic wave sweeps the floor; a crack hides an ossuary.")
    z.west(low=1)
    z.flat(12)
    z.enemy("small", z.x - 5)
    z.cross(30)
    x0 = z.flat(48)
    z.hazard(x0, x0 + 48, period=70.0, first=35.0)
    z.safe_ledges(x0, x0 + 48)
    z.enemy("flyer", x0 + 20, z.y - 8)
    z.enemy("mid", x0 + 34, flip=True)
    z.receptor("crane_oeil_gauche", x0 + 42, sign=tr("L'œil gauche : frappez-le avec un pouvoir de tir (Onde, Tir chargé, Boomerang).",
                                                     "The left eye: hit it with a shooting power (Wave, Charged shot, Boomerang)."))
    # The ossuary, hidden above the orbit (ledges from the walkways).
    z.ledge(x0 + 10, x0 + 15, z.y - 10)
    z.ledge(x0 + 4, x0 + 9, z.y - 13)
    z.secret_island(5, x0 - 12, z.y - 16, "Une fente dans l'os : l'Ossuaire oublié !", "A crack in the bone: the Forgotten ossuary!")
    z.flat(6, -3)
    z.east(low=4)
    z.scatter(x0, x0 + 48, 0.2)
    z.save()

    z = Zone(s, 3, "L'orbite droite : son regard tire des rayons. L'œil droit attend au bout ; un passage descend vers la tempe.",
             "The right orbit: its gaze fires beams. The right eye waits at the end; a way leads down to the temple.")
    z.west(low=1)
    z.flat(12)
    z.up(2, w=10)
    x0 = z.flat(34)
    z.lasers([x0 + 8, x0 + 20], ["regard_1", "regard_2"])
    z.L.sign(x0 + 3, z.y, tr("Le regard de l'orbite : passez entre deux rayons.", "The orbit's gaze: pass between two beams."))
    z.enemy("mid", x0 + 14)
    z.enemy("flyer2", x0 + 26, z.y - 7)
    z.receptor("crane_oeil_droit", x0 + 30, sign=tr("L'œil droit : frappez-le avec un pouvoir de tir (Onde, Tir chargé, Boomerang).",
                                                    "The right eye: hit it with a shooting power (Wave, Charged shot, Boomerang)."))
    z.cross(26)
    z.flat(4)
    z.east(low=6, high=4)
    z.scatter(0, z.x, 0.2)
    z.lights(0, z.x, 26)
    z.save()

    z = Zone(s, 4, "Les nerfs optiques relient les yeux au cerveau. Deux nerfs, deux groupes ; l'onde psychique y revient sans cesse.",
             "The optic nerves link the eyes to the brain. Two nerves, two groups; the psychic wave keeps coming back.")
    z.west(low=3, high=2)
    x0 = z.flat(72)
    z.hazard(x0, x0 + 72, period=80.0, first=40.0)
    z.safe_ledges(x0, x0 + 72, every=16)
    z.L.sign(x0 + 3, z.y, tr("Deux nerfs, deux groupes : chaque groupe en tranche un (un pouvoir de tir).",
                             "Two nerves, two groups: each group severs one (a shooting power)."))
    z.receptor("crane_nerf_gauche", x0 + 18)
    z.receptor("crane_nerf_droit", x0 + 54)
    z.enemy("flyer", x0 + 30, z.y - 9, name="renfort_1")
    z.enemy("flyer", x0 + 44, z.y - 9, flip=True, name="renfort_2")
    z.enemy("mid", x0 + 36, name="renfort_3")
    z.enemy("heavy", x0 + 62, flip=True)
    z.L.orb("health_orb", x0 + 36, z.y - 8)
    z.east(low=7, high=6)
    z.save()

    secret(s, 2, "crane_ossuaire", "Relique de l'ossuaire", "Ossuary relic",
           "Un ossuaire oublié dans l'épaisseur du crâne. Une relique y repose.",
           "An ossuary forgotten deep in the skull. A relic rests there.")

    z = Zone(s, 6, "La tempe du titan. Un golem la garde ; son levier ouvre le passage vers les nerfs. Une paroi mène à la voûte.",
             "The titan's temple. A golem guards it; its lever opens the way to the nerves. A wall leads to the vault.")
    z.west(low=3, high=4)
    lever = z.L.lever(14, z.y - 12, z.doors[4])
    z.objective("crane_levier", lever, "activate")
    z.L.sign(17, z.y - 12, tr("Levier : il ouvre le passage vers les Nerfs optiques à tous les groupes.",
                              "Lever: it opens the way to the Optic nerves for every group."))
    z.flat(10)
    z.cross(28)
    x0 = z.flat(40)
    golem = z.enemy("guardian", x0 + 22, flip=True)
    z.objective("crane_tempe", golem, "killed")
    z.enemy("flyer", x0 + 10, z.y - 8, name="renfort_4")
    z.enemy("flyer", x0 + 32, z.y - 9, flip=True, name="renfort_5")
    z.L.orb("health_orb", x0 + 4, z.y - 3)
    z.flat(6, -3)
    z.L.sign(z.x - 3, z.y, tr("La paroi vers la Voûte ne s'ouvre que si une autre branche du titan cède.",
                              "The wall to the Vault only opens if another branch of the titan gives way."))
    z.east(low=7)
    z.scatter(0, z.x, 0.25)
    z.save()

    lock_arena(s, 4, 6, "crane_voute", ["mid", "small", "flyer", "heavy", "guardian"],
               "La voûte crânienne, verrouillée par les deux yeux. Sa garde défend l'antre de Skullmaw.",
               "The cranial vault, locked by both eyes. Its guard defends Skullmaw's lair.",
               "La voûte se tait ! L'antre de Skullmaw est ouvert.", "The vault falls silent! Skullmaw's lair is open.")
    import raid_skullmaw  # 2.69 (phase 4B): its lair unsealed, its fight
    raid_skullmaw.lair(sys.modules[__name__], s)


# ======================================================================== BRAS GAUCHE (Dreadfang)
def bras_gauche():
    s = BRAS_GAUCHE
    hub(s, "L'épaule gelée du titan, porte du bras gauche. Trois points d'apparition, une garde de givre, puis deux chemins.",
        "The titan's frozen shoulder, gate of the left arm. Three spawn points, a frost guard, then two ways.",
        "bg_epaule", ("L'épaule gelée est à nous ! Biceps en haut, Tendon en bas.", "The frozen shoulder is ours! Biceps up high, Tendon below."),
        3, 2, "Asterion, bras gauche. Les deux cœurs de givre ouvrent le poignet ; la carte de la branche montre où sont les groupes.",
        "Asterion, left arm. The two frost hearts open the wrist; the branch map shows where the groups are.")

    z = Zone(s, 2, "Le biceps de givre : sa glace se brise sous les pas. Un golem de glace garde le muscle.",
             "The frost biceps: its ice breaks underfoot. An ice golem guards the muscle.")
    z.west(low=1)
    z.flat(10)
    z.L.sign(12, z.y, tr("La glace du biceps cède sous les pas : ne vous arrêtez pas.", "The biceps' ice gives way underfoot: don't stop."))
    z.cross(36)
    x0 = z.flat(42)
    golem = z.enemy("guardian", x0 + 24, flip=True)
    z.objective("bg_golem", golem, "killed")
    z.enemy("flyer", x0 + 12, z.y - 8)
    z.enemy("small", x0 + 34, flip=True)
    z.L.orb("health_orb", x0 + 4, z.y - 3)
    z.east(low=4, high=6)
    z.scatter(0, z.x, 0.25)
    z.save()

    z = Zone(s, 3, "Le tendon glacé grimpe vers le coude. Des tourelles le surveillent, les griffes balaient le bas ; un cœur de givre bat en haut.",
             "The frozen tendon climbs to the elbow. Turrets watch it, the claws sweep the bottom; a frost heart beats at the top.")
    z.west(low=1)
    x0 = z.flat(30)
    z.hazard(x0, x0 + 30, period=60.0, first=30.0)
    z.safe_ledges(x0, x0 + 30, every=14)
    z.enemy("mid", x0 + 12)
    z.up(3, w=12)
    x1 = z.flat(36)
    z.lasers([x1 + 8, x1 + 22], ["tourelle_1", "tourelle_2"])
    z.L.sign(x1 + 3, z.y, tr("Les tourelles-regards du tendon : passez entre deux tirs.", "The tendon's watcher turrets: pass between two shots."))
    z.enemy("flyer2", x1 + 16, z.y - 7)
    z.receptor("bg_coeur_ouest", x1 + 32, sign=tr("Cœur de givre ouest : frappez-le avec un pouvoir de tir.",
                                                   "West frost heart: hit it with a shooting power."))
    # The crevasse, hidden above the low hall.
    z.ledge(x0 + 16, x0 + 21, 36 - 10)
    z.ledge(x0 + 8, x0 + 13, 36 - 13)
    z.secret_island(5, x0 - 6, 36 - 16, "Une faille dans la glace : la Crevasse cachée !", "A rift in the ice: the Hidden crevasse!")
    z.east(low=4, high=6)
    z.save()

    z = Zone(s, 4, "Le coude des lames, où Dreadfang aiguise ses griffes. Deux lames : il faut deux groupes, un par lame. Les griffes balaient le sol.",
             "The elbow of blades, where Dreadfang sharpens its claws. Two blades: two groups are needed, one per blade. The claws sweep the floor.")
    z.west(low=3, high=2)
    x0 = z.flat(70)
    z.hazard(x0, x0 + 70, period=75.0, first=40.0)
    z.safe_ledges(x0, x0 + 70, every=16)
    z.L.sign(x0 + 3, z.y, tr("Deux lames, deux groupes : chaque groupe en brise une (un pouvoir de tir).",
                             "Two blades, two groups: each group breaks one (a shooting power)."))
    z.slab(x0 + 40, z.y - 11, x0 + 52, z.y - 9)
    z.receptor("bg_lame_haute", x0 + 46, y=z.y - 11)
    z.receptor("bg_lame_basse", x0 + 24)
    z.enemy("heavy", x0 + 34, flip=True)
    z.enemy("flyer", x0 + 56, z.y - 9)
    z.enemy("flyer", x0 + 14, z.y - 9, flip=True)
    z.L.orb("health_orb", x0 + 62, z.y - 5)
    z.east(low=7)
    z.L.sign(z.x - 12, z.y, tr("Poignet de glace : il s'ouvre quand les deux cœurs de givre (Tendon, Avant-bras) sont brisés.",
                               "Ice wrist: it opens once both frost hearts (Tendon, Forearm) are broken."))
    z.save()

    secret(s, 3, "bg_relique", "Relique de la crevasse", "Crevasse relic",
           "Une crevasse cachée dans la glace du bras. Une relique y dort ; une paroi gelée donne sur le poignet.",
           "A crevasse hidden in the arm's ice. A relic sleeps there; a frozen wall faces the wrist.", east_low=7)

    z = Zone(s, 6, "L'avant-bras de givre. Son cœur de givre alimente le verrou du poignet ; son levier ouvre le raccourci vers le tendon.",
             "The frost forearm. Its frost heart feeds the wrist's lock; its lever opens the shortcut to the tendon.")
    z.west(low=2, high=3)
    lever = z.L.lever(14, z.y - 12, z.doors[3])
    z.objective("bg_levier", lever, "activate")
    z.L.sign(17, z.y - 12, tr("Levier : il ouvre le raccourci vers le Tendon glacé à tous les groupes.",
                              "Lever: it opens the shortcut to the Frozen tendon for every group."))
    z.flat(8)
    z.cross(24)
    x0 = z.flat(40)
    z.enemy("flyer2", x0 + 10, z.y - 7)
    z.enemy("mid", x0 + 22, flip=True)
    z.receptor("bg_coeur_est", x0 + 32, sign=tr("Cœur de givre est : frappez-le avec un pouvoir de tir.",
                                                 "East frost heart: hit it with a shooting power."))
    z.L.orb("health_orb", x0 + 4, z.y - 3)
    z.east()
    z.scatter(0, z.x, 0.3)
    z.save()

    lock_arena(s, 4, 5, "bg_poignet", ["mid", "small", "flyer", "heavy", "guardian"],
               "Le poignet de glace, verrouillé par deux cœurs de givre. Sa garde défend les griffes de Dreadfang.",
               "The ice wrist, locked by two frost hearts. Its guard defends Dreadfang's claws.",
               "Le poignet cède ! Les griffes de Dreadfang sont ouvertes.", "The wrist gives way! Dreadfang's claws are open.")
    import raid_dreadfang  # 2.69 (phase 4B): its lair unsealed, its fight
    raid_dreadfang.lair(sys.modules[__name__], s)


# ======================================================================== TORSE (Heartbreaker)
def torse():
    s = TORSE
    hub(s, "La clavicule du titan, porte du torse. Trois points d'apparition, une garde à disperser, puis deux chemins.",
        "The titan's collarbone, gate of the torso. Three spawn points, a guard to scatter, then two ways.",
        "torse_clavicule", ("La clavicule est à nous ! Côtes hautes en haut, poumon en bas.", "The collarbone is ours! Upper ribs up high, lung below."),
        3, 2, "Asterion, torse. Ses quatre plaques brisées ouvrent la cage du cœur ; il faut plusieurs groupes.",
        "Asterion, torso. Its four plates broken open the heart cage; several groups are needed.")

    z = Zone(s, 2, "Les côtes hautes bougent avec le souffle du titan. Une barrière d'énergie les traverse ; un éclat de roche les garde.",
             "The upper ribs move with the titan's breath. An energy barrier crosses them; a rock shard guards them.")
    z.west(low=1)
    z.flat(10)
    z.cross(36)
    x0 = z.flat(46)
    z.lasers([x0 + 8], ["barriere_1"])
    z.L.sign(x0 + 3, z.y, tr("Barrière d'énergie : passez quand elle faiblit.", "Energy barrier: pass when it weakens."))
    shard = z.enemy("guardian", x0 + 28, flip=True)
    z.objective("torse_cotes", shard, "killed")
    z.enemy("flyer", x0 + 18, z.y - 8)
    z.enemy("flyer", x0 + 50, z.y - 9, flip=True)
    z.ledge(x0 + 44, x0 + 49, z.y - 4)
    z.ledge(x0 + 41, x0 + 46, z.y - 8)
    z.secret_island(5, x0 + 27, z.y - 12, "Une fissure entre deux côtes : le Reliquaire du cœur !",
                    "A crack between two ribs: the Heart reliquary!")
    z.L.orb("health_orb", x0 + 4, z.y - 3)
    z.east(low=4)
    z.save()

    z = Zone(s, 3, "Le poumon gauche se gonfle et siffle. Il faut le crever ; des parasites y affluent. Son levier ouvre le raccourci vers le sternum.",
             "The left lung swells and hisses. It must be burst; parasites flock there. Its lever opens the shortcut to the sternum.")
    z.west(low=1)
    z.flat(14)
    z.cross(28)
    x0 = z.flat(44)
    z.enemy("small", x0 + 8, name="parasite_1")
    z.enemy("flyer2", x0 + 18, z.y - 7, name="parasite_2")
    z.enemy("mid", x0 + 30, flip=True)
    z.receptor("torse_poumon", x0 + 38, sign=tr("Le poumon gauche : crevez-le avec un pouvoir de tir.",
                                                 "The left lung: burst it with a shooting power."))
    z.east(low=6, high=4)
    lever = z.L.lever(z.x - 16, z.y - 12, z.doors[4])
    z.objective("torse_levier", lever, "activate")
    z.L.sign(z.x - 19, z.y - 12, tr("Levier : il ouvre le raccourci vers le Sternum à tous les groupes.",
                                    "Lever: it opens the shortcut to the Sternum for every group."))
    z.scatter(0, z.x, 0.25)
    z.lights(0, z.x, 26)
    z.save()

    z = Zone(s, 4, "Le sternum, cuirassé de deux plaques. Deux groupes, une plaque chacun ; le cœur pulse sous le métal et des barrières le défendent.",
             "The sternum, armoured with two plates. Two groups, one plate each; the heart pulses under the metal and barriers defend it.")
    z.west(low=2, high=3)
    x0 = z.flat(72)
    z.hazard(x0, x0 + 72, period=45.0, first=25.0, duration=4.0)
    z.safe_ledges(x0, x0 + 72, every=16)
    z.lasers([x0 + 12, x0 + 60], ["barriere_2", "barriere_3"], top_gap=10)
    z.L.sign(x0 + 3, z.y, tr("Deux plaques, deux groupes : chaque groupe en brise une (un pouvoir de tir).",
                             "Two plates, two groups: each group breaks one (a shooting power)."))
    z.slab(x0 + 30, z.y - 11, x0 + 42, z.y - 9)
    z.receptor("torse_plaque_nord", x0 + 36, y=z.y - 11)
    z.receptor("torse_plaque_sud", x0 + 48)
    z.enemy("heavy", x0 + 24, flip=True)
    z.enemy("flyer", x0 + 54, z.y - 9)
    z.L.orb("health_orb", x0 + 66, z.y - 5)
    z.east(low=7)
    z.save()

    secret(s, 2, "torse_reliquaire", "Relique du reliquaire", "Reliquary relic",
           "Un reliquaire caché entre les côtes, tout près du cœur. Une relique y brille.",
           "A reliquary hidden between the ribs, right by the heart. A relic shines there.")

    z = Zone(s, 6, "Les côtes basses, cuirassées de deux plaques. Deux groupes, une plaque chacun ; des parasites rampent entre les os.",
             "The lower ribs, armoured with two plates. Two groups, one plate each; parasites crawl between the bones.")
    z.west(low=3)
    z.flat(10)
    z.cross(30)
    x0 = z.flat(60)
    z.hazard(x0, x0 + 60, period=50.0, first=30.0, duration=4.0)
    z.safe_ledges(x0, x0 + 60, every=15)
    z.receptor("torse_plaque_ouest", x0 + 14)
    z.receptor("torse_plaque_est", x0 + 46)
    z.L.sign(x0 + 3, z.y, tr("Deux plaques, deux groupes : chaque groupe en brise une (un pouvoir de tir).",
                             "Two plates, two groups: each group breaks one (a shooting power)."))
    z.enemy("small", x0 + 24, name="parasite_3")
    z.enemy("flyer2", x0 + 36, z.y - 8, name="parasite_4")
    z.enemy("mid", x0 + 52, flip=True)
    z.east(low=7)
    z.save()

    lock_arena(s, 4, 6, "torse_cage", ["mid", "small", "flyer", "heavy", "guardian"],
               "La cage du cœur, fermée par quatre plaques. Sa garde défend l'antre de Heartbreaker.",
               "The heart cage, closed by four plates. Its guard defends Heartbreaker's lair.",
               "La cage s'ouvre ! L'antre de Heartbreaker est ouvert.", "The cage opens! Heartbreaker's lair is open.")
    import raid_heartbreaker  # 2.69 (phase 4B): its lair unsealed, its fight
    raid_heartbreaker.lair(sys.modules[__name__], s)


# ======================================================================== ABDOMEN (Voidworm)
def abdomen():
    s = ABDOMEN
    hub(s, "Le diaphragme du titan, porte de l'abdomen. Trois points d'apparition, une garde gluante, puis deux chemins.",
        "The titan's diaphragm, gate of the abdomen. Three spawn points, a slimy guard, then two ways.",
        "abd_diaphragme", ("Le diaphragme est à nous ! Estomac en haut, nid des parasites en bas.",
                           "The diaphragm is ours! Stomach up high, parasite nest below."),
        3, 2, "Asterion, abdomen. Les deux glandes crevées ouvrent le gosier ; le nid des parasites nourrit tout le titan.",
        "Asterion, abdomen. Bursting both glands opens the gullet; the parasite nest feeds the whole titan.")

    z = Zone(s, 2, "L'estomac du titan : des bassins d'acide, des parois qui respirent et une glande à crever. L'acide reflue sans prévenir.",
             "The titan's stomach: acid pools, breathing walls and a gland to burst. The acid flows back without warning.")
    z.west(low=1)
    x0 = z.flat(24)
    z.hazard(x0, x0 + 24, period=65.0, first=35.0)
    z.safe_ledges(x0, x0 + 24, every=12)
    z.cross(34)
    x1 = z.flat(40)
    z.enemy("mid", x1 + 10)
    z.enemy("flyer", x1 + 22, z.y - 7)
    z.receptor("abd_glande_nord", x1 + 32, sign=tr("La glande nord : crevez-la avec un pouvoir de tir.",
                                                    "The north gland: burst it with a shooting power."))
    z.east(low=4, high=6)
    z.scatter(0, z.x, 0.3)
    z.save()

    z = Zone(s, 3, "Le nid des parasites, qui nourrit tout le titan. Sa garde le défend en deux vagues ; un repli cache une poche oubliée.",
             "The parasite nest that feeds the whole titan. Its guard defends it in two waves; a fold hides a forgotten pouch.")
    z.west(low=1)
    z.flat(10)
    z.flat(4, 2)
    x0 = z.flat(50)
    z.arena(x0 + 3, x0 + 47, [[("small", x0 + 10, None, False), ("mid", x0 + 22, None, True), ("small", x0 + 34, None, True)],
                              [("flyer", x0 + 10, 7, False), ("heavy", x0 + 30, None, True), ("flyer2", x0 + 40, 9, True)]],
            "abd_nid", "Le nid des parasites est détruit !", "The parasite nest is destroyed!")
    z.ledge(x0 + 12, x0 + 18, z.y - 5)
    z.ledge(x0 + 30, x0 + 36, z.y - 5)
    z.ledge(x0 + 15, x0 + 20, z.y - 9)
    z.secret_island(5, x0 + 23, z.y - 13, "Un repli de chair : la Poche oubliée !", "A fold of flesh: the Forgotten pouch!")
    z.flat(4, -2)
    z.east(low=4)
    z.scatter(0, z.x, 0.3)
    z.save()

    z = Zone(s, 4, "L'intestin du titan, long et mouvant. Deux valves : deux groupes, une chacun. L'acide y reflue souvent.",
             "The titan's intestine, long and shifting. Two valves: two groups, one each. Acid flows back often.")
    z.west(low=3, high=2)
    z.cross(26)
    x0 = z.flat(66)
    z.hazard(x0, x0 + 66, period=50.0, first=25.0)
    z.safe_ledges(x0, x0 + 66, every=16)
    z.L.sign(x0 + 3, z.y, tr("Deux valves, deux groupes : chaque groupe en ferme une (un pouvoir de tir).",
                             "Two valves, two groups: each group closes one (a shooting power)."))
    z.slab(x0 + 36, z.y - 11, x0 + 48, z.y - 9)
    z.receptor("abd_valve_haute", x0 + 42, y=z.y - 11)
    z.receptor("abd_valve_basse", x0 + 20)
    z.enemy("heavy", x0 + 30, flip=True)
    z.enemy("flyer2", x0 + 54, z.y - 9)
    z.L.orb("health_orb", x0 + 60, z.y - 5)
    z.L.sign(x0 + 62, z.y, tr("Vers le Gosier : la paroi ne cède que si le Torse perd ses plaques.",
                              "To the Gullet: the wall only gives way if the Torso loses its plates."))
    z.east(low=7, high=6)
    z.save()

    secret(s, 3, "abd_relique", "Relique de la poche", "Pouch relic",
           "Une poche oubliée dans les replis de l'abdomen. Une relique y flotte.",
           "A pouch forgotten in the abdomen's folds. A relic floats there.")

    z = Zone(s, 6, "Le foie noir du titan. Sa glande alimente le verrou du gosier ; son levier ouvre le raccourci vers l'intestin.",
             "The titan's black liver. Its gland feeds the gullet's lock; its lever opens the shortcut to the intestine.")
    z.west(low=2, high=4)
    lever = z.L.lever(14, z.y - 12, z.doors[4])
    z.objective("abd_levier", lever, "activate")
    z.L.sign(17, z.y - 12, tr("Levier : il ouvre le raccourci vers l'Intestin à tous les groupes.",
                              "Lever: it opens the shortcut to the Intestine for every group."))
    z.flat(8)
    z.cross(26)
    x0 = z.flat(40)
    z.enemy("heavy", x0 + 12)
    z.enemy("flyer", x0 + 24, z.y - 8)
    z.receptor("abd_glande_sud", x0 + 34, sign=tr("La glande sud : crevez-la avec un pouvoir de tir.",
                                                   "The south gland: burst it with a shooting power."))
    z.L.orb("health_orb", x0 + 4, z.y - 3)
    z.east(low=7)
    z.scatter(0, z.x, 0.3)
    z.save()

    lock_arena(s, 4, 6, "abd_gosier", ["mid", "small", "flyer", "heavy", "guardian"],
               "Le gosier de Voidworm, fermé par deux glandes. Sa garde défend l'antre.",
               "Voidworm's gullet, closed by two glands. Its guard defends the lair.",
               "Le gosier s'ouvre ! L'antre de Voidworm est ouvert.", "The gullet opens! Voidworm's lair is open.")
    import raid_voidworm  # 2.69 (phase 4B): its lair unsealed, its fight
    raid_voidworm.lair(sys.modules[__name__], s)


# ======================================================================== JAMBE GAUCHE (Graviton)
def jambe_gauche():
    s = JAMBE_GAUCHE
    hub(s, "La hanche gauche du titan, porte de la jambe. Trois points d'apparition, une garde à disperser, puis deux chemins.",
        "The titan's left hip, gate of the leg. Three spawn points, a guard to scatter, then two ways.",
        "jg_hanche", ("La hanche est à nous ! Cuisse flottante en haut, muscle suspendu en bas.",
                      "The hip is ours! Floating thigh up high, hanging muscle below."),
        3, 2, "Asterion, jambe gauche. Graviton tord la pesanteur : les puits d'apesanteur portent, les puits de gravité écrasent.",
        "Asterion, left leg. Graviton bends gravity: the weightless wells carry you, the gravity wells crush you.")

    z = Zone(s, 2, "La cuisse flotte dans la gravité tordue de Graviton. Des puits d'apesanteur portent les sauts ; un générateur de gravité tourne au bout.",
             "The thigh floats in Graviton's twisted gravity. Weightless wells carry the jumps; a gravity generator spins at the end.")
    z.west(low=1)
    z.flat(10)
    z.L.sign(12, z.y, tr("Les puits d'apesanteur vous portent : laissez-vous soulever.", "The weightless wells carry you: let them lift you."))
    z.cross(36)
    x0 = z.flat(44)
    z.hazard(x0, x0 + 44, period=70.0, first=35.0)
    z.safe_ledges(x0, x0 + 44, every=14)
    z.enemy("flyer", x0 + 14, z.y - 8)
    z.enemy("flyer2", x0 + 26, z.y - 9, flip=True)
    z.receptor("jg_generateur_ouest", x0 + 38, sign=tr("Générateur de gravité ouest : coupez-le avec un pouvoir de tir.",
                                                        "West gravity generator: cut it with a shooting power."))
    z.east(low=4, high=6)
    z.save()

    z = Zone(s, 3, "Le muscle suspendu : des îlots mouvants au-dessus du vide. Un éclat de roche le garde ; un sanctuaire se cache en hauteur.",
             "The hanging muscle: shifting islets over the void. A rock shard guards it; a sanctuary hides up high.")
    z.west(low=1)
    z.flat(12)
    x = z.x
    for i in range(4):
        z.L.moving(x + 3 + i * 9, z.y - (2 if i % 2 else 0), 5, 3, 0, speed=24, wait=0.8, phase=0.0)
    z.x = x + 40
    x0 = z.flat(44)
    shard = z.enemy("guardian", x0 + 26, flip=True)
    z.objective("jg_gardien", shard, "killed")
    z.enemy("mid", x0 + 12)
    z.enemy("flyer", x0 + 36, z.y - 8)
    z.ledge(x0 + 18, x0 + 23, z.y - 4)
    z.ledge(x0 + 17, x0 + 22, z.y - 8)
    z.secret_island(5, x0 + 4, z.y - 12, "Des ruines suspendues : le Sanctuaire des poids !", "Hanging ruins: the Sanctuary of weights!")
    z.L.orb("health_orb", x0 + 40, z.y - 3)
    z.east(low=4)
    z.scatter(0, z.x, 0.25)
    z.save()

    z = Zone(s, 4, "Le genou du titan, tenu par deux rotules. Deux groupes, une rotule chacun ; des puits de gravité y écrasent tout.",
             "The titan's knee, held by two kneecaps. Two groups, one kneecap each; gravity wells crush everything there.")
    z.west(low=3, high=2)
    x0 = z.flat(70)
    z.hazard(x0, x0 + 70, period=60.0, first=30.0)
    z.safe_ledges(x0, x0 + 70, every=16)
    z.L.sign(x0 + 3, z.y, tr("Deux rotules, deux groupes : chaque groupe en brise une (un pouvoir de tir).",
                             "Two kneecaps, two groups: each group breaks one (a shooting power)."))
    z.slab(x0 + 44, z.y - 11, x0 + 56, z.y - 9)
    z.receptor("jg_rotule_exterieure", x0 + 50, y=z.y - 11)
    z.receptor("jg_rotule_interieure", x0 + 22)
    z.enemy("heavy", x0 + 34, flip=True)
    z.enemy("flyer", x0 + 60, z.y - 9)
    z.L.orb("health_orb", x0 + 64, z.y - 5)
    z.east(low=7, high=6)
    z.save()

    secret(s, 3, "jg_relique", "Relique du sanctuaire", "Sanctuary relic",
           "Le sanctuaire des poids, suspendu dans la jambe. Une relique y défie la gravité.",
           "The sanctuary of weights, hanging inside the leg. A relic defies gravity there.")

    z = Zone(s, 6, "Le tibia du titan. Son générateur de gravité alimente le verrou de la cheville ; son levier ouvre le raccourci vers le genou.",
             "The titan's shin. Its gravity generator feeds the ankle's lock; its lever opens the shortcut to the knee.")
    z.west(low=2, high=4)
    lever = z.L.lever(14, z.y - 12, z.doors[4])
    z.objective("jg_levier", lever, "activate")
    z.L.sign(17, z.y - 12, tr("Levier : il ouvre le raccourci vers le Genou à tous les groupes.",
                              "Lever: it opens the shortcut to the Knee for every group."))
    z.flat(8)
    z.cross(30)
    x0 = z.flat(40)
    z.enemy("flyer2", x0 + 10, z.y - 8)
    z.enemy("mid", x0 + 22, flip=True)
    z.receptor("jg_generateur_est", x0 + 32, sign=tr("Générateur de gravité est : coupez-le avec un pouvoir de tir.",
                                                      "East gravity generator: cut it with a shooting power."))
    z.flat(6, -3)
    z.L.sign(z.x - 3, z.y, tr("La paroi vers la Cheville ne s'ouvre que si une autre branche du titan cède.",
                              "The wall to the Ankle only opens if another branch of the titan gives way."))
    z.east(low=7)
    z.scatter(0, z.x, 0.25)
    z.save()

    lock_arena(s, 4, 6, "jg_cheville", ["mid", "small", "flyer", "heavy", "guardian"],
               "La cheville gauche, verrouillée par deux générateurs de gravité. Sa garde défend le pied de Graviton.",
               "The left ankle, locked by two gravity generators. Its guard defends Graviton's foot.",
               "La cheville cède ! Le pied de Graviton est ouvert.", "The ankle gives way! Graviton's foot is open.")
    import raid_graviton  # 2.69 (phase 4B): its lair unsealed, its fight
    raid_graviton.lair(sys.modules[__name__], s)


# ======================================================================== JAMBE DROITE (Titanus)
def jambe_droite():
    s = JAMBE_DROITE
    hub(s, "La hanche de fer du titan, porte de la jambe droite. Trois points d'apparition, une garde de forge, puis deux chemins.",
        "The titan's iron hip, gate of the right leg. Three spawn points, a forge guard, then two ways.",
        "jd_hanche", ("La hanche de fer est à nous ! Cuisse-piston en haut, fémur en bas.", "The iron hip is ours! Piston thigh up high, femur below."),
        3, 2, "Asterion, jambe droite. Titanus frappe le sol : quand l'onde arrive, montez sur les grilles.",
        "Asterion, right leg. Titanus strikes the ground: when the wave comes, climb onto the grates.")

    z = Zone(s, 2, "La cuisse-piston : des pistons géants montent et descendent au-dessus de la lave. Un golem de magma garde le muscle.",
             "The piston thigh: giant pistons rise and fall over the lava. A magma golem guards the muscle.")
    z.west(low=1)
    z.flat(10)
    z.L.sign(12, z.y, tr("Les pistons montent et descendent : sautez au sommet de leur course.", "The pistons rise and fall: jump at the top of their stroke."))
    z.cross(40)
    x0 = z.flat(44)
    golem = z.enemy("guardian", x0 + 24, flip=True)
    z.objective("jd_golem", golem, "killed")
    z.enemy("flyer", x0 + 12, z.y - 8)
    z.enemy("small", x0 + 36, flip=True)
    z.L.orb("health_orb", x0 + 4, z.y - 3)
    z.east(low=4)
    z.scatter(0, z.x, 0.25)
    z.save()

    z = Zone(s, 3, "Le fémur fendu : la lave affleure dans ses fissures et l'onde sismique de Titanus y frappe. Un réacteur tourne au fond.",
             "The split femur: lava rises in its cracks and Titanus' seismic wave strikes there. A reactor spins at the back.")
    z.west(low=1)
    x0 = z.flat(46)
    z.hazard(x0, x0 + 46, period=60.0, first=30.0)
    z.safe_ledges(x0, x0 + 46, every=14)
    z.enemy("mid", x0 + 16)
    z.enemy("flyer2", x0 + 30, z.y - 9)
    z.ledge(x0 + 34, x0 + 39, z.y - 10)
    z.secret_island(5, x0 + 40, z.y - 13, "Une forge cachée dans l'os : la Forge oubliée !", "A forge hidden in the bone: the Forgotten forge!")
    z.cross(18)
    x1 = z.flat(30)
    z.receptor("jd_reacteur_ouest", x1 + 20, sign=tr("Réacteur ouest : coupez-le avec un pouvoir de tir.",
                                                      "West reactor: cut it with a shooting power."))
    z.enemy("mid", x1 + 10, flip=True)
    z.east(low=6, high=4)
    z.save()

    z = Zone(s, 4, "Le genou-forge, actionné par deux pistons. Deux groupes, un piston chacun ; l'onde sismique y revient souvent.",
             "The forge knee, driven by two pistons. Two groups, one piston each; the seismic wave comes back often.")
    z.west(low=3, high=2)
    x0 = z.flat(72)
    z.hazard(x0, x0 + 72, period=75.0, first=40.0)
    z.safe_ledges(x0, x0 + 72, every=16)
    z.L.sign(x0 + 3, z.y, tr("Deux pistons, deux groupes : chaque groupe en bloque un (un pouvoir de tir).",
                             "Two pistons, two groups: each group jams one (a shooting power)."))
    z.slab(x0 + 40, z.y - 11, x0 + 52, z.y - 9)
    z.receptor("jd_piston_haut", x0 + 46, y=z.y - 11)
    z.receptor("jd_piston_bas", x0 + 22)
    z.enemy("heavy", x0 + 32, flip=True)
    z.enemy("flyer", x0 + 58, z.y - 9)
    z.L.orb("health_orb", x0 + 66, z.y - 5)
    z.east(low=7, high=6)
    z.save()

    secret(s, 3, "jd_relique", "Relique de la forge", "Forge relic",
           "Une forge oubliée dans la jambe du titan. Une relique y refroidit.",
           "A forge forgotten in the titan's leg. A relic cools there.")

    z = Zone(s, 6, "Le mollet blindé. Son réacteur alimente le verrou de la cheville ; son levier ouvre le raccourci vers le genou-forge.",
             "The armoured calf. Its reactor feeds the ankle's lock; its lever opens the shortcut to the forge knee.")
    z.west(low=3, high=4)
    lever = z.L.lever(14, z.y - 12, z.doors[4])
    z.objective("jd_levier", lever, "activate")
    z.L.sign(17, z.y - 12, tr("Levier : il ouvre le raccourci vers le Genou-forge à tous les groupes.",
                              "Lever: it opens the shortcut to the Forge knee for every group."))
    z.flat(8)
    z.cross(32)
    x0 = z.flat(40)
    z.enemy("flyer", x0 + 10, z.y - 8)
    z.enemy("mid", x0 + 22, flip=True)
    z.receptor("jd_reacteur_est", x0 + 32, sign=tr("Réacteur est : coupez-le avec un pouvoir de tir.",
                                                    "East reactor: cut it with a shooting power."))
    z.L.orb("health_orb", x0 + 4, z.y - 3)
    z.east(low=7)
    z.scatter(0, z.x, 0.25)
    z.save()

    lock_arena(s, 4, 6, "jd_cheville", ["mid", "small", "flyer", "heavy", "guardian"],
               "La cheville d'acier, verrouillée par deux réacteurs. Sa garde défend le talon de Titanus.",
               "The steel ankle, locked by two reactors. Its guard defends Titanus's heel.",
               "La cheville d'acier cède ! Le talon de Titanus est ouvert.", "The steel ankle gives way! Titanus's heel is open.")
    import raid_titanus  # 2.69 (phase 4B): its lair unsealed, its fight
    raid_titanus.lair(sys.modules[__name__], s)


# ------------------------------------------------------------------ catalogs, links, checks
def load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def check_branch(cat, levels):
    """Every edge has its two passages (same lock), every passage an edge, every objective one object."""
    for e in cat["edges"]:
        for a, b in ((e["a"], e["b"]), (e["b"], e["a"])):
            ps = [p for p in levels[a]["metadata"]["raid"]["passages"] if p["to"] == b]
            assert len(ps) == 1 and ps[0]["lock"] == e["lock"], (cat["id"], a, b)
            assert str(a) in levels[b]["metadata"]["raid"]["spawns"], (cat["id"], "arrival", a, b)
    for n, lv in levels.items():
        for p in lv["metadata"]["raid"]["passages"]:
            assert any({e["a"], e["b"]} == {n, p["to"]} for e in cat["edges"]), (cat["id"], n, p["to"])
    for o in cat["objectives"]:
        found = [x for x in levels[o["zone"]]["metadata"]["raid"]["objectives"] if x["id"] == o["id"]]
        assert len(found) == 1, (cat["id"], o["id"])


def links(catalogs, levels):
    """The links of the titan, their bits in the target's state, checked against the zones."""
    by_part = {c["branch"]: c for c in catalogs}
    out, bits = [], {}
    for lid, label, label_en, sources, to, kind, zones, objects in LINKS:
        tr(label, label_en)
        target = by_part[to]
        bit = LINK_BIT0 + bits.get(to, 0)
        bits[to] = bits.get(to, 0) + 1
        assert bit < 32 and len(target["objectives"]) <= LINK_BIT0, lid
        for part, oid in sources:
            assert any(o["id"] == oid for o in by_part[part]["objectives"]), (lid, oid)
        if kind == "open":
            assert any(e["lock"] == "link:" + lid for e in target["edges"]), lid
        for z in zones:
            lv = levels[to][z]
            ids = {o["id"] for o in lv["objects"]}
            if kind == "hazard_off":
                assert lv["metadata"]["raid"].get("magma"), (lid, z)
            if kind in ("disable", "remove"):
                assert any(oid in ids for oid in objects), (lid, z)
        for oid in objects:
            assert any(oid in {o["id"] for o in levels[to][z]["objects"]} for z in zones), (lid, oid)
        out.append({"id": lid, "label": label, "from": [{"branch": by_part[p]["id"], "objective_id": o} for p, o in sources],
                    "to": target["id"], "bit": bit, "kind": kind, "zones": zones, "objects": objects})
    # Every "link:" lock of a branch is a link to that branch.
    for c in catalogs:
        for e in c["edges"]:
            if e["lock"].startswith("link:"):
                assert any(l["id"] == e["lock"][5:] and l["to"] == c["id"] for l in out), (c["id"], e["lock"])
    return out


def write_english(path):
    """The English of every text of the raid, for `python3 tools/i18n/extract.py --merge <path>`."""
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(dict(sorted(EN.items())), f, ensure_ascii=False, indent=1)
        f.write("\n")
    print("%s : %d textes et leur anglais" % (path, len(EN)))


def main():
    for build in (crane, bras_gauche, torse, abdomen, jambe_gauche, jambe_droite):
        build()
    calibrate()
    catalogs, levels = [], {}
    for s in SPECS:
        lv = {n: load_json(os.path.join(RAID_DIR, s.part, "zone_%d.json" % n)) for n in range(1, 9)}
        minutes = {n: round(estimate_minutes(lv[n]), 1) for n in range(1, 8)}
        assert 13.0 <= minutes[7] <= 17.0, (s.part, "expedition", minutes[7])
        # The whole branch for one group (every zone once, the gigaboss apart): with the gigaboss
        # fight and the waits for the other groups, a standard raid of 45 to 60 minutes.
        route = round(sum(minutes.values()), 1)
        assert 18.0 <= route <= 35.0, (s.part, "route", route)
        print("%s : %s min (8 joueurs ; expédition : %.1f min ; branche : %.1f min sans le gigaboss)" % (s.part, minutes, minutes[7], route))
        cat = s.catalog(minutes)
        check_branch(cat, lv)
        write_json(os.path.join(RAID_DIR, s.part, "branch.json"), cat)
        catalogs.append(cat)
        levels[s.part] = lv
    # The BRAS DROIT (raid_bras_droit.py): read only.
    bd = load_json(os.path.join(RAID_DIR, "bras_droit", "branch.json"))
    levels["bras_droit"] = {n: load_json(os.path.join(RAID_DIR, "bras_droit", "zone_%d.json" % n)) for n in range(1, 9)}
    all_cats = catalogs + [bd]
    lks = links(all_cats, levels)
    by_part = {c["branch"]: c for c in all_cats}
    branches = [{"part": p, "id": by_part[p]["id"], "dir": p} for p in ORDER]
    titan = {"format": "cosmic.raid_titan", "revision": TITAN_REVISION, "raid": "asterion",
             "comment": "Written by tools/levelgen/raid_asterion.py: the seven branches of Asterion and the links between them "
                        "(an objective of one branch acts on another). Never edit by hand.",
             "branches": branches, "links": lks}
    write_json(os.path.join(RAID_DIR, "titan.json"), titan)
    server = dict(titan)
    server["catalogs"] = catalogs
    write_json(SERVER_COPY, server)
    if "--en" in sys.argv and sys.argv.index("--en") + 1 < len(sys.argv):
        write_english(sys.argv[sys.argv.index("--en") + 1])


if __name__ == "__main__":
    main()
