#include "game/levels/ObjectTypes.h"

namespace cosmic {

const char* categoryLabel(ObjCategory c) {
    switch (c) {
        case ObjCategory::Terrain: return "Terrain";
        case ObjCategory::Platforms: return "Plateformes";
        case ObjCategory::Hazards: return "Pièges & dangers";
        case ObjCategory::Gameplay: return "Gameplay";
        case ObjCategory::Interactive: return "Interactifs";
        case ObjCategory::Enemies: return "Ennemis";
        case ObjCategory::Zones: return "Zones & événements";
        case ObjCategory::Decor: return "Décors";
        case ObjCategory::Lights: return "Lumières & effets";
    }
    return "";
}

Json ObjectTypeInfo::defaultProps() const {
    Json j = Json::object();
    for (const auto& p : props) j[p.key] = p.def;
    return j;
}

const PropDef* ObjectTypeInfo::prop(const std::string& key) const {
    for (const auto& p : props)
        if (p.key == key) return &p;
    return nullptr;
}

namespace {

PropDef pBool(const char* key, const char* label, bool def, const char* help = "") {
    PropDef p;
    p.key = key;
    p.label = label;
    p.type = PropType::Bool;
    p.def = def;
    p.help = help;
    return p;
}
PropDef pFloat(const char* key, const char* label, float def, float mn, float mx, float step, const char* help = "") {
    PropDef p;
    p.key = key;
    p.label = label;
    p.type = PropType::Float;
    p.def = def;
    p.min = mn;
    p.max = mx;
    p.step = step;
    p.help = help;
    return p;
}
PropDef pInt(const char* key, const char* label, int def, int mn, int mx, const char* help = "") {
    PropDef p;
    p.key = key;
    p.label = label;
    p.type = PropType::Int;
    p.def = def;
    p.min = static_cast<float>(mn);
    p.max = static_cast<float>(mx);
    p.help = help;
    return p;
}
PropDef pStr(const char* key, const char* label, const char* def, PropType t = PropType::String, const char* help = "") {
    PropDef p;
    p.key = key;
    p.label = label;
    p.type = t;
    p.def = def;
    p.help = help;
    return p;
}
PropDef pEnum(const char* key, const char* label, const char* def, std::vector<std::string> opts,
              std::vector<std::string> labels) {
    PropDef p;
    p.key = key;
    p.label = label;
    p.type = PropType::Enum;
    p.def = def;
    p.options = std::move(opts);
    p.optionLabels = std::move(labels);
    return p;
}

const std::vector<std::string> kMaterials = {"ruin_stone", "ancient_metal", "crystal",     "root",
                                             "basalt",     "obsidian",      "forge_metal", "glacier_ice",
                                             "snow_rock",  "frost_metal"};
const std::vector<std::string> kMaterialLabels = {"Pierre des ruines", "Métal ancien", "Cristal",
                                                  "Racines",           "Basalte",      "Obsidienne",
                                                  "Métal de forge",    "Glace (glissante)", "Roche enneigée",
                                                  "Métal givré"};
const std::vector<std::string> kPowers = {"dash",         "wave",        "shield",  "impulse", "teleport",
                                          "glide",        "ground_pound", "charge_shot", "grapple", "triple_jump"};
const std::vector<std::string> kPowerLabels = {"Dash",    "Onde énergétique", "Bouclier",   "Impulsion",
                                               "Téléportation", "Planeur",    "Frappe sismique", "Tir chargé",
                                               "Grappin", "Triple saut"};
// Obstacles that a power opens (power_gate).
const std::vector<std::string> kGatePowers = {"dash", "wave", "shield", "impulse", "teleport", "ground_pound"};
const std::vector<std::string> kGatePowerLabels = {"Dash (mur de cristal)",        "Onde (barrière d'énergie)",
                                                   "Bouclier (rideau de plasma)",  "Impulsion (rocher fissuré)",
                                                   "Téléportation (barrière de phase)", "Frappe sismique (sol fissuré)"};

std::vector<ObjectTypeInfo> build() {
    std::vector<ObjectTypeInfo> v;
    auto add = [&](ObjectTypeInfo t) { v.push_back(std::move(t)); };

    // ------------------------------------------------------------ terrain
    {
        ObjectTypeInfo t;
        t.id = "solid";
        t.name = "Bloc de terrain";
        t.category = ObjCategory::Terrain;
        t.defaultSize = {64, 32};
        t.editorColor = {90, 130, 150, 255};
        t.defaultLayer = "terrain";
        t.props = {pEnum("material", "Matériau", "ruin_stone", kMaterials, kMaterialLabels),
                   pBool("decorTop", "Végétation au sommet", true),
                   pBool("collision", "Collision", true, "Décocher pour un bloc purement décoratif")};
        t.help = "Terrain solide auto-raccordé (tuiles 16 px).";
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "oneway";
        t.name = "Plateforme traversable";
        t.category = ObjCategory::Platforms;
        t.defaultSize = {64, 16};
        t.editorColor = {150, 120, 80, 255};
        t.defaultLayer = "terrain";
        t.props = {pEnum("style", "Style", "ruin_bridge",
                         {"ruin_bridge", "tech_ledge", "root_branch", "basalt_ledge", "chain_grate", "ice_ledge",
                          "snow_bridge"},
                         {"Pont en ruine", "Corniche technologique", "Branche", "Corniche de basalte",
                          "Grille suspendue", "Corniche de glace", "Pont enneigé"})};
        t.assets = {"textures/tiles/platforms.json", "textures/tiles/platforms.png"};
        t.help = "On peut sauter au travers par dessous et descendre avec Bas + Saut.";
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "moving_platform";
        t.name = "Plateforme mobile";
        t.category = ObjCategory::Platforms;
        t.defaultSize = {48, 16};
        t.editorColor = {255, 179, 71, 255};
        t.props = {pFloat("dx", "Déplacement X", 128, -4000, 4000, 8),
                   pFloat("dy", "Déplacement Y", 0, -4000, 4000, 8),
                   pFloat("speed", "Vitesse (px/s)", 60, 5, 400, 5),
                   pFloat("wait", "Pause aux extrémités (s)", 0.6f, 0, 10, 0.1f),
                   pFloat("phase", "Décalage de départ (0-1)", 0, 0, 1, 0.05f),
                   pBool("active", "Actif au départ", true, "Peut être activé par un événement"),
                   pBool("oneway", "Traversable par dessous", false),
                   pBool("triggered", "Démarre quand on monte dessus", false)};
        t.assets = {"sprites/objects/moving_platform.png"};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "crumble_platform";
        t.name = "Plateforme fragile";
        t.category = ObjCategory::Platforms;
        t.defaultSize = {48, 16};
        t.editorColor = {170, 150, 110, 255};
        t.props = {pFloat("delay", "Délai avant chute (s)", 0.45f, 0.05f, 5, 0.05f),
                   pFloat("respawn", "Réapparition (s, 0 = jamais)", 3.0f, 0, 60, 0.5f)};
        t.assets = {"sprites/objects/crumble_platform.png"};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "power_gate";
        t.name = "Obstacle à pouvoir";
        t.category = ObjCategory::Terrain;
        t.defaultSize = {32, 64};
        t.editorColor = {176, 76, 255, 255};
        t.props = {pEnum("power", "Pouvoir requis", "dash", kGatePowers, kGatePowerLabels),
                   pBool("respawn", "Se reforme au checkpoint", false)};
        t.assets = {"animations/breakable_wall.json", "sprites/objects/breakable_wall.png", "animations/phase_wall.json",
                    "sprites/objects/phase_wall.png", "sprites/objects/laser_beam.png",
                    "animations/cracked_floor.json", "sprites/objects/cracked_floor.png"};
        t.help = "Dash : mur de cristal à briser. Téléportation : barrière de phase à traverser. "
                 "Onde / Impulsion : barrière détruite par le pouvoir (le Tir chargé dissout aussi les "
                 "barrières d'énergie). Bouclier : rideau de plasma franchissable seulement bouclier actif. "
                 "Frappe sismique : sol fissuré brisé en plongeant dessus.";
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "bounce_pad";
        t.name = "Champignon rebond";
        t.category = ObjCategory::Platforms;
        t.defaultSize = {32, 16};
        t.resizable = false;
        t.editorColor = {224, 138, 255, 255};
        t.props = {pFloat("power", "Force du rebond", 430, 100, 1200, 10)};
        t.assets = {"animations/bounce_pad.json", "sprites/objects/bounce_pad.png"};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "ladder";
        t.name = "Lianes grimpables";
        t.category = ObjCategory::Platforms;
        t.defaultSize = {16, 96};
        t.editorColor = {53, 181, 131, 255};
        t.help = "Zone d'escalade (Haut/Bas pour grimper).";
        add(t);
    }

    // ------------------------------------------------------------ hazards
    {
        ObjectTypeInfo t;
        t.id = "spikes";
        t.name = "Pics de cristal";
        t.category = ObjCategory::Hazards;
        t.defaultSize = {48, 16};
        t.rotatable = true;
        t.editorColor = {255, 77, 109, 255};
        t.props = {pInt("damage", "Dégâts", 1, 1, 10), pBool("instantKill", "Mort instantanée", false)};
        t.assets = {"sprites/objects/spikes.png"};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "liquid";
        t.name = "Liquide";
        t.category = ObjCategory::Hazards;
        t.defaultSize = {128, 48};
        t.editorColor = {157, 255, 58, 255};
        t.props = {pEnum("kind", "Type", "acid", {"acid", "water", "lava"}, {"Acide de l'abîme", "Eau", "Lave"}),
                   pFloat("waveHeight", "Hauteur des vagues", 1, 0, 4, 0.5f)};
        t.assets = {"sprites/objects/liquid_acid.png", "sprites/objects/liquid_water.png",
                    "sprites/objects/liquid_lava.png"};
        t.help = "Acide et lave sont mortels ; l'eau ralentit et permet de nager.";
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "damage_zone";
        t.name = "Zone de dégâts";
        t.category = ObjCategory::Hazards;
        t.defaultSize = {64, 64};
        t.editorColor = {255, 60, 60, 255};
        t.defaultLayer = "gameplay";
        t.props = {pInt("damage", "Dégâts", 1, 1, 10), pFloat("interval", "Intervalle (s)", 0.8f, 0.1f, 10, 0.1f),
                   pBool("visible", "Visible (brume toxique)", true),
                   pBool("shieldProtects", "Le bouclier protège", true)};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "slow_zone";
        t.name = "Zone de ralentissement";
        t.category = ObjCategory::Hazards;
        t.defaultSize = {96, 64};
        t.editorColor = {120, 120, 255, 255};
        t.props = {pFloat("factor", "Facteur de vitesse", 0.5f, 0.1f, 1.0f, 0.05f),
                   pBool("visible", "Visible (spores)", true)};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "updraft";
        t.name = "Courant ascendant";
        t.category = ObjCategory::Zones;
        t.defaultSize = {48, 192};
        t.editorColor = {143, 255, 240, 255};
        t.props = {pFloat("strength", "Vitesse de montée", 230.0f, 40.0f, 600.0f, 10.0f),
                   pBool("vent", "Grille au sol", true, "Dessine une bouche d'aération en bas de la zone")};
        t.assets = {"animations/updraft_vent.json", "sprites/objects/updraft_vent.png"};
        t.help = "Soulève le joueur qui plane (Planeur) et ralentit sa chute sinon.";
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "laser";
        t.name = "Laser ancien";
        t.category = ObjCategory::Hazards;
        t.defaultSize = {160, 16};
        t.rotatable = true;
        t.editorColor = {255, 77, 109, 255};
        t.props = {pFloat("onTime", "Durée active (s)", 1.5f, 0.1f, 20, 0.1f),
                   pFloat("offTime", "Durée inactive (s)", 1.5f, 0, 20, 0.1f),
                   pFloat("warnTime", "Avertissement (s)", 0.6f, 0, 5, 0.1f),
                   pFloat("phase", "Décalage (s)", 0, 0, 20, 0.1f), pInt("damage", "Dégâts", 1, 1, 10),
                   pBool("active", "Actif au départ", true)};
        t.assets = {"animations/laser_emitter.json", "sprites/objects/laser_emitter.png",
                    "sprites/objects/laser_beam.png"};
        add(t);
    }

    // ----------------------------------------------------------- gameplay
    {
        ObjectTypeInfo t;
        t.id = "player_start";
        t.name = "Départ du joueur";
        t.category = ObjCategory::Gameplay;
        t.defaultSize = {10, 22};
        t.resizable = false;
        t.unique = true;
        t.editorColor = {57, 240, 216, 255};
        t.props = {pBool("faceLeft", "Regarde à gauche", false)};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "checkpoint";
        t.name = "Point de sauvegarde";
        t.category = ObjCategory::Gameplay;
        t.defaultSize = {24, 48};
        t.resizable = false;
        t.editorColor = {57, 240, 216, 255};
        t.props = {pStr("label", "Nom (affiché)", "")};
        t.assets = {"animations/checkpoint.json", "sprites/objects/checkpoint.png"};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "exit";
        t.name = "Portail de sortie";
        t.category = ObjCategory::Gameplay;
        t.defaultSize = {40, 64};
        t.resizable = false;
        t.unique = true;
        t.editorColor = {255, 217, 138, 255};
        t.props = {pBool("locked", "Verrouillé (ouvert par événement)", false)};
        t.assets = {"animations/exit_portal.json", "sprites/objects/exit_portal.png"};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "shard";
        t.name = "Éclat d'énergie";
        t.category = ObjCategory::Gameplay;
        t.defaultSize = {12, 12};
        t.resizable = false;
        t.snap = 8;
        t.editorColor = {143, 255, 240, 255};
        t.props = {pInt("value", "Points", 50, 0, 10000)};
        t.assets = {"animations/shard.json", "sprites/objects/shard.png"};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "relic";
        t.name = "Relique secrète";
        t.category = ObjCategory::Gameplay;
        t.defaultSize = {16, 16};
        t.resizable = false;
        t.snap = 8;
        t.editorColor = {255, 217, 138, 255};
        t.props = {pInt("value", "Points", 1000, 0, 100000), pStr("name", "Nom", "Relique ancienne")};
        t.assets = {"animations/relic.json", "sprites/objects/relic.png"};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "health_orb";
        t.name = "Orbe de vie";
        t.category = ObjCategory::Gameplay;
        t.defaultSize = {12, 12};
        t.resizable = false;
        t.snap = 8;
        t.editorColor = {255, 77, 109, 255};
        t.props = {pInt("amount", "Soin", 2, 1, 10)};
        t.assets = {"animations/health_orb.json", "sprites/objects/health_orb.png"};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "energy_orb";
        t.name = "Orbe d'énergie";
        t.category = ObjCategory::Gameplay;
        t.defaultSize = {12, 12};
        t.resizable = false;
        t.snap = 8;
        t.editorColor = {57, 240, 216, 255};
        t.props = {pFloat("amount", "Énergie", 40, 1, 100, 1)};
        t.assets = {"animations/energy_orb.json", "sprites/objects/energy_orb.png"};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "power_unlock";
        t.name = "Tablette de pouvoir";
        t.category = ObjCategory::Gameplay;
        t.defaultSize = {24, 24};
        t.resizable = false;
        t.snap = 8;
        t.editorColor = {176, 76, 255, 255};
        t.props = {pEnum("power", "Pouvoir", "dash", kPowers, kPowerLabels),
                   pStr("message", "Message", "", PropType::Text)};
        t.assets = {"animations/power_unlock.json", "sprites/objects/power_unlock.png"};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "heart_container";
        t.name = "Cœur de cristal";
        t.category = ObjCategory::Gameplay;
        t.defaultSize = {16, 16};
        t.resizable = false;
        t.snap = 8;
        t.editorColor = {255, 120, 150, 255};
        t.props = {pInt("hearts", "Cœurs maximum en plus", 1, 1, 5)};
        t.assets = {"animations/heart_container.json", "sprites/objects/heart_container.png"};
        t.help = "Augmente la vie maximale pour le reste du niveau et soigne entièrement.";
        add(t);
    }

    // -------------------------------------------------------- interactive
    {
        ObjectTypeInfo t;
        t.id = "grapple_point";
        t.name = "Point d'ancrage";
        t.category = ObjCategory::Interactive;
        t.defaultSize = {24, 24};
        t.resizable = false;
        t.snap = 8;
        t.editorColor = {57, 240, 216, 255};
        t.assets = {"animations/grapple_point.json", "sprites/objects/grapple_point.png"};
        t.help = "Avec le Grappin, Interagir à portée (environ 170 px) propulse le joueur jusqu'ici.";
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "door";
        t.name = "Porte d'énergie";
        t.category = ObjCategory::Interactive;
        t.defaultSize = {16, 64};
        t.rotatable = true;
        t.editorColor = {255, 179, 71, 255};
        t.props = {pBool("open", "Ouverte au départ", false)};
        t.assets = {"animations/door.json", "sprites/objects/door.png"};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "lever";
        t.name = "Levier";
        t.category = ObjCategory::Interactive;
        t.defaultSize = {16, 16};
        t.resizable = false;
        t.editorColor = {255, 179, 71, 255};
        t.props = {pStr("targets", "Cibles (ids)", "", PropType::ObjectRef, "Objets basculés (porte, plateforme, laser...)"),
                   pBool("oneShot", "Usage unique", true)};
        t.assets = {"animations/lever.json", "sprites/objects/lever.png"};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "receptor";
        t.name = "Récepteur d'énergie";
        t.category = ObjCategory::Interactive;
        t.defaultSize = {16, 32};
        t.resizable = false;
        t.rotatable = true;
        t.editorColor = {57, 240, 216, 255};
        t.props = {pStr("targets", "Cibles (ids)", "", PropType::ObjectRef, "Activé par l'onde énergétique"),
                   pFloat("duration", "Durée d'activation (s, 0 = permanent)", 0, 0, 60, 0.5f)};
        t.assets = {"animations/receptor.json", "sprites/objects/receptor.png"};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "sign";
        t.name = "Tablette ancienne";
        t.category = ObjCategory::Interactive;
        t.defaultSize = {24, 24};
        t.resizable = false;
        t.editorColor = {200, 200, 255, 255};
        t.props = {pStr("text", "Texte", "Les anciens veillent.", PropType::Text),
                   pBool("auto", "Affichage automatique à proximité", true)};
        t.assets = {"animations/sign.json", "sprites/objects/sign.png"};
        add(t);
    }

    // ------------------------------------------------------------ enemies
    {
        ObjectTypeInfo t;
        t.id = "enemy";
        t.name = "Ennemi";
        t.category = ObjCategory::Enemies;
        t.defaultSize = {16, 16};
        t.resizable = false;
        t.editorColor = {255, 77, 109, 255};
        t.props = {pStr("enemy", "Type d'ennemi", "glowmite", PropType::EnemyRef),
                   pBool("faceLeft", "Regarde à gauche", false),
                   pFloat("patrol", "Distance de patrouille", 96, 0, 2000, 8),
                   pBool("dormant", "En attente (apparaît par événement)", false),
                   pBool("boss", "Boss du niveau", false)};
        add(t);
    }

    // -------------------------------------------------------------- zones
    {
        ObjectTypeInfo t;
        t.id = "trigger";
        t.name = "Déclencheur";
        t.category = ObjCategory::Zones;
        t.defaultSize = {64, 96};
        t.editorColor = {120, 255, 120, 255};
        t.props = {pBool("once", "Une seule fois", true)};
        t.help = "Zone invisible : utilisez l'éditeur d'événements pour définir ce qu'elle déclenche.";
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "combat_zone";
        t.name = "Arène de combat";
        t.category = ObjCategory::Zones;
        t.defaultSize = {320, 160};
        t.editorColor = {255, 120, 60, 255};
        t.props = {pStr("doors", "Portes verrouillées (ids)", "", PropType::ObjectRef),
                   pStr("waves", "Vagues (ids ennemis par vague, ';' sépare les vagues)", "", PropType::Text),
                   pBool("lockCamera", "Bloquer la caméra sur l'arène", true),
                   pBool("announce", "Annoncer le combat (titres)", true),
                   pStr("music", "Musique pendant le combat", "", PropType::SoundRef)};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "camera_zone";
        t.name = "Zone de caméra";
        t.category = ObjCategory::Zones;
        t.defaultSize = {480, 270};
        t.editorColor = {255, 255, 120, 255};
        t.props = {pEnum("mode", "Mode", "clamp", {"clamp", "lock"}, {"Limiter à la zone", "Caméra fixe"}),
                   pFloat("zoom", "Zoom (0 = inchangé)", 0, 0, 2, 0.05f)};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "secret_area";
        t.name = "Zone secrète";
        t.category = ObjCategory::Zones;
        t.defaultSize = {128, 96};
        t.editorColor = {255, 217, 138, 255};
        t.props = {pStr("message", "Message à la découverte", "Passage secret découvert !")};
        t.help = "Rend transparents les décors avant au-dessus de la zone et compte comme secret trouvé.";
        add(t);
    }

    // -------------------------------------------------------------- decor
    {
        ObjectTypeInfo t;
        t.id = "decor";
        t.name = "Décor";
        t.category = ObjCategory::Decor;
        t.defaultSize = {32, 48};
        t.resizable = true;
        t.rotatable = true;
        t.freeRotation = true;
        t.snap = 4;
        t.editorColor = {120, 200, 160, 255};
        t.defaultLayer = "bg_decor";
        t.props = {pStr("sprite", "Sprite", "mushroom_tall", PropType::DecorRef),
                   pFloat("alpha", "Opacité", 1, 0, 1, 0.05f), pStr("tint", "Teinte", "#ffffff", PropType::Color),
                   pFloat("animSpeed", "Vitesse d'animation", 1, 0, 4, 0.1f),
                   pBool("glow", "Lueur", true), pFloat("sway", "Balancement", 0, 0, 10, 0.5f)};
        // Atlas resolved per sprite by the exporter (one atlas per planet).
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "light";
        t.name = "Lumière";
        t.category = ObjCategory::Lights;
        t.defaultSize = {16, 16};
        t.resizable = false;
        t.snap = 4;
        t.editorColor = {255, 255, 200, 255};
        t.props = {pStr("color", "Couleur", "#39f0d8", PropType::Color), pFloat("radius", "Rayon", 80, 8, 600, 4),
                   pFloat("flicker", "Scintillement", 0.1f, 0, 1, 0.05f),
                   pBool("bloom", "Halo visible", true)};
        add(t);
    }
    {
        ObjectTypeInfo t;
        t.id = "emitter";
        t.name = "Émetteur de particules";
        t.category = ObjCategory::Lights;
        t.defaultSize = {64, 32};
        t.editorColor = {200, 255, 255, 255};
        t.props = {pStr("effect", "Effet", "fireflies", PropType::EffectRef),
                   pFloat("rate", "Émissions / s", 2, 0.1f, 60, 0.1f)};
        add(t);
    }
    return v;
}

std::vector<PaletteEntry> buildPalette() {
    std::vector<PaletteEntry> p;
    auto add = [&](const char* label, const char* type, Json props = Json::object(), Vec2 size = {}) {
        p.push_back({label, type, std::move(props), size});
    };
    add("Pierre des ruines", "solid", {{"material", "ruin_stone"}});
    add("Métal ancien", "solid", {{"material", "ancient_metal"}});
    add("Roche de cristal", "solid", {{"material", "crystal"}});
    add("Racines géantes", "solid", {{"material", "root"}});
    add("Mur de cristal (Dash)", "power_gate", {{"power", "dash"}});
    add("Barrière de phase (Téléport.)", "power_gate", {{"power", "teleport"}}, {16, 64});
    add("Barrière d'énergie (Onde)", "power_gate", {{"power", "wave"}}, {16, 64});
    add("Rocher fissuré (Impulsion)", "power_gate", {{"power", "impulse"}}, {64, 32});
    add("Rideau de plasma (Bouclier)", "power_gate", {{"power", "shield"}}, {16, 80});
    add("Sol fissuré (Frappe sismique)", "power_gate", {{"power", "ground_pound"}}, {64, 16});
    add("Basalte", "solid", {{"material", "basalt"}});
    add("Obsidienne", "solid", {{"material", "obsidian"}});
    add("Métal de forge", "solid", {{"material", "forge_metal"}});
    add("Glace (glissante)", "solid", {{"material", "glacier_ice"}});
    add("Roche enneigée", "solid", {{"material", "snow_rock"}});
    add("Métal givré", "solid", {{"material", "frost_metal"}});
    add("Pont en ruine", "oneway", {{"style", "ruin_bridge"}});
    add("Corniche technologique", "oneway", {{"style", "tech_ledge"}});
    add("Branche", "oneway", {{"style", "root_branch"}});
    add("Corniche de basalte", "oneway", {{"style", "basalt_ledge"}});
    add("Grille suspendue", "oneway", {{"style", "chain_grate"}});
    add("Corniche de glace", "oneway", {{"style", "ice_ledge"}});
    add("Pont enneigé", "oneway", {{"style", "snow_bridge"}});
    add("Plateforme mobile", "moving_platform");
    add("Plateforme fragile", "crumble_platform");
    add("Champignon rebond", "bounce_pad");
    add("Lianes grimpables", "ladder");
    add("Pics de cristal", "spikes");
    add("Acide de l'abîme", "liquid", {{"kind", "acid"}});
    add("Eau", "liquid", {{"kind", "water"}});
    add("Lave", "liquid", {{"kind", "lava"}});
    add("Zone de dégâts", "damage_zone");
    add("Zone de ralentissement", "slow_zone");
    add("Laser ancien", "laser");
    for (const char* t : {"player_start", "checkpoint", "exit", "shard", "relic", "health_orb", "energy_orb"}) add("", t);
    add("Tablette : Dash", "power_unlock", {{"power", "dash"}});
    add("Tablette : Onde", "power_unlock", {{"power", "wave"}});
    add("Tablette : Bouclier", "power_unlock", {{"power", "shield"}});
    add("Tablette : Impulsion", "power_unlock", {{"power", "impulse"}});
    add("Tablette : Téléportation", "power_unlock", {{"power", "teleport"}});
    add("Tablette : Planeur", "power_unlock", {{"power", "glide"}});
    add("Tablette : Frappe sismique", "power_unlock", {{"power", "ground_pound"}});
    add("Tablette : Tir chargé", "power_unlock", {{"power", "charge_shot"}});
    add("Tablette : Grappin", "power_unlock", {{"power", "grapple"}});
    add("Tablette : Triple saut", "power_unlock", {{"power", "triple_jump"}});
    add("", "heart_container");
    add("", "grapple_point");
    add("", "updraft");
    for (const char* t : {"door", "lever", "receptor", "sign", "trigger", "combat_zone", "camera_zone", "secret_area",
                          "light", "emitter"})
        add("", t);
    return p;
}

}  // namespace

namespace ObjectTypes {

const std::vector<ObjectTypeInfo>& all() {
    static const std::vector<ObjectTypeInfo> types = build();
    return types;
}

const ObjectTypeInfo* find(const std::string& id) {
    for (const auto& t : all())
        if (t.id == id) return &t;
    return nullptr;
}

const std::vector<PaletteEntry>& palette() {
    static const std::vector<PaletteEntry> p = [] {
        auto v = buildPalette();
        for (auto& e : v)
            if (e.label.empty())
                if (const auto* t = find(e.type)) e.label = t->name;
        return v;
    }();
    return p;
}

}  // namespace ObjectTypes

}  // namespace cosmic
