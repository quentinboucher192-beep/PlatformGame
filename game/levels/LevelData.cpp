#include "game/levels/LevelData.h"

#include <algorithm>

namespace cosmic {

// ------------------------------------------------------------------ enums

const char* timerModeId(TimerMode m) {
    switch (m) {
        case TimerMode::Limited: return "limited";
        case TimerMode::Unlimited: return "unlimited";
        case TimerMode::TimeAttack: return "timeAttack";
        case TimerMode::Challenge: return "challenge";
    }
    return "limited";
}

const char* timerModeLabel(TimerMode m) {
    switch (m) {
        case TimerMode::Limited: return "Temps limité";
        case TimerMode::Unlimited: return "Temps illimité";
        case TimerMode::TimeAttack: return "Contre-la-montre";
        case TimerMode::Challenge: return "Mode défi";
    }
    return "";
}

TimerMode timerModeFromId(const std::string& s) {
    if (s == "unlimited") return TimerMode::Unlimited;
    if (s == "timeAttack") return TimerMode::TimeAttack;
    if (s == "challenge") return TimerMode::Challenge;
    return TimerMode::Limited;
}

const char* expireActionId(ExpireAction a) {
    switch (a) {
        case ExpireAction::GameOver: return "gameover";
        case ExpireAction::RestartCheckpoint: return "restartCheckpoint";
        case ExpireAction::Continue: return "continue";
    }
    return "gameover";
}

ExpireAction expireActionFromId(const std::string& s) {
    if (s == "restartCheckpoint") return ExpireAction::RestartCheckpoint;
    if (s == "continue") return ExpireAction::Continue;
    return ExpireAction::GameOver;
}

const char* medalName(Medal m) {
    switch (m) {
        case Medal::None: return "Aucune";
        case Medal::Bronze: return "Bronze";
        case Medal::Silver: return "Argent";
        case Medal::Gold: return "Or";
        case Medal::Platinum: return "Platine";
    }
    return "";
}

// ---------------------------------------------------------------- objects

Json LevelObject::toJson() const {
    Json j;
    j["id"] = id;
    j["type"] = type;
    j["layer"] = layer;
    j["rect"] = cosmic::toJson(rect);
    if (rotation != 0.0f) j["rotation"] = rotation;
    if (flipX) j["flipX"] = true;
    if (z != 0) j["z"] = z;
    if (!props.empty()) j["props"] = props;
    return j;
}

LevelObject LevelObject::fromJson(const Json& j) {
    LevelObject o;
    o.id = jstr(j, "id");
    o.type = jstr(j, "type");
    o.layer = jstr(j, "layer", "gameplay");
    o.rect = rectFromJson(j.value("rect", Json()), {0, 0, 16, 16});
    o.rotation = jget<float>(j, "rotation", 0.0f);
    o.flipX = jget<bool>(j, "flipX", false);
    o.z = jget<int>(j, "z", 0);
    if (j.contains("props") && j["props"].is_object()) o.props = j["props"];
    return o;
}

Json LevelLayer::toJson() const {
    Json j{{"id", id}, {"name", name}, {"order", order}, {"visible", visible}, {"locked", locked}};
    if (foreground) j["foreground"] = true;
    if (!(tint == Color::white())) j["tint"] = tint.toHex();
    return j;
}

LevelLayer LevelLayer::fromJson(const Json& j) {
    LevelLayer l;
    l.id = jstr(j, "id");
    l.name = jstr(j, "name", l.id);
    l.order = jget<int>(j, "order", 0);
    l.visible = jget<bool>(j, "visible", true);
    l.locked = jget<bool>(j, "locked", false);
    l.foreground = jget<bool>(j, "foreground", false);
    l.tint = colorFromJson(j.value("tint", Json()), Color::white());
    return l;
}

Json MedalRule::toJson() const {
    Json j = Json::object();
    if (maxTime > 0) j["maxTime"] = maxTime;
    if (maxDeaths >= 0) j["maxDeaths"] = maxDeaths;
    if (minCollectiblesPercent > 0) j["minCollectiblesPercent"] = minCollectiblesPercent;
    return j;
}

MedalRule MedalRule::fromJson(const Json& j) {
    MedalRule r;
    r.maxTime = jget<float>(j, "maxTime", 0.0f);
    r.maxDeaths = jget<int>(j, "maxDeaths", -1);
    r.minCollectiblesPercent = jget<int>(j, "minCollectiblesPercent", 0);
    return r;
}

Json EventAction::toJson() const {
    Json j{{"type", type}};
    if (!target.empty()) j["target"] = target;
    if (!params.empty()) j["params"] = params;
    return j;
}

EventAction EventAction::fromJson(const Json& j) {
    EventAction a;
    a.type = jstr(j, "type");
    a.target = jstr(j, "target");
    if (j.contains("params") && j["params"].is_object()) a.params = j["params"];
    return a;
}

Json LevelEvent::toJson() const {
    Json j{{"id", id}, {"source", source}, {"trigger", trigger}, {"once", once}};
    if (!name.empty()) j["name"] = name;
    if (delay > 0) j["delay"] = delay;
    if (!params.empty()) j["params"] = params;
    Json acts = Json::array();
    for (const auto& a : actions) acts.push_back(a.toJson());
    j["actions"] = acts;
    return j;
}

LevelEvent LevelEvent::fromJson(const Json& j) {
    LevelEvent e;
    e.id = jstr(j, "id");
    e.name = jstr(j, "name");
    e.source = jstr(j, "source");
    e.trigger = jstr(j, "trigger", "enter");
    e.once = jget<bool>(j, "once", true);
    e.delay = jget<float>(j, "delay", 0.0f);
    if (j.contains("params") && j["params"].is_object()) e.params = j["params"];
    if (j.contains("actions") && j["actions"].is_array())
        for (const auto& a : j["actions"]) e.actions.push_back(EventAction::fromJson(a));
    return e;
}

Json CinematicStep::toJson() const {
    Json j{{"type", type}, {"duration", duration}};
    if (!wait) j["wait"] = false;
    if (!params.empty()) j["params"] = params;
    return j;
}

CinematicStep CinematicStep::fromJson(const Json& j) {
    CinematicStep s;
    s.type = jstr(j, "type", "wait");
    s.duration = jget<float>(j, "duration", 1.0f);
    s.wait = jget<bool>(j, "wait", true);
    if (j.contains("params") && j["params"].is_object()) s.params = j["params"];
    return s;
}

Json CinematicDef::toJson() const {
    Json steps_j = Json::array();
    for (const auto& s : steps) steps_j.push_back(s.toJson());
    return {{"skippable", skippable}, {"letterbox", letterbox}, {"steps", steps_j}};
}

CinematicDef CinematicDef::fromJson(const Json& j) {
    CinematicDef c;
    c.skippable = jget<bool>(j, "skippable", true);
    c.letterbox = jget<bool>(j, "letterbox", true);
    if (j.contains("steps") && j["steps"].is_array())
        for (const auto& s : j["steps"]) c.steps.push_back(CinematicStep::fromJson(s));
    return c;
}

// ------------------------------------------------------------------- level

LevelObject* LevelData::findObject(const std::string& objId) {
    for (auto& o : objects)
        if (o.id == objId) return &o;
    return nullptr;
}

const LevelObject* LevelData::findObject(const std::string& objId) const {
    for (const auto& o : objects)
        if (o.id == objId) return &o;
    return nullptr;
}

const LevelLayer* LevelData::findLayer(const std::string& layerId) const {
    for (const auto& l : layers)
        if (l.id == layerId) return &l;
    return nullptr;
}

std::string LevelData::nextObjectId() const {
    int best = 0;
    for (const auto& o : objects) {
        if (o.id.size() > 1 && o.id[0] == 'o') {
            try {
                best = std::max(best, std::stoi(o.id.substr(1)));
            } catch (...) {
            }
        }
    }
    return "o" + std::to_string(best + 1);
}

std::string LevelData::nextEventId() const {
    int best = 0;
    for (const auto& e : events) {
        if (e.id.size() > 2 && e.id.rfind("ev", 0) == 0) {
            try {
                best = std::max(best, std::stoi(e.id.substr(2)));
            } catch (...) {
            }
        }
    }
    return "ev" + std::to_string(best + 1);
}

int LevelData::countObjects(const std::string& type) const {
    return static_cast<int>(std::count_if(objects.begin(), objects.end(), [&](const auto& o) { return o.type == type; }));
}

void LevelData::ensureDefaultLayers() {
    auto add = [&](const char* lid, const char* name, int order, bool fg) {
        if (!findLayer(lid)) {
            LevelLayer l;
            l.id = lid;
            l.name = name;
            l.order = order;
            l.foreground = fg;
            layers.push_back(l);
        }
    };
    add("bg_decor", "Décor arrière", 0, false);
    add("terrain", "Terrain", 10, false);
    add("gameplay", "Gameplay", 20, false);
    add("fg_decor", "Décor avant", 30, true);
    sortLayers();
}

void LevelData::sortLayers() {
    std::stable_sort(layers.begin(), layers.end(), [](const auto& a, const auto& b) { return a.order < b.order; });
}

Json LevelData::toJson() const {
    Json j;
    j["format"] = "cosmic.level";
    j["formatVersion"] = kFormatVersion;
    j["id"] = id;
    j["name"] = name;
    j["description"] = description;
    j["planet"] = planet;
    j["author"] = author;
    j["version"] = version;
    j["difficulty"] = difficulty;
    j["music"] = music;
    j["bossMusic"] = bossMusic;
    j["ambience"] = ambience;
    j["bounds"] = cosmic::toJson(bounds);
    if (killY != 0.0f) j["killY"] = killY;
    j["ambientLight"] = ambientLight.toHex();
    j["backgroundColor"] = backgroundColor.toHex();
    Json bg = Json::array();
    for (const auto& l : background) bg.push_back(l.toJson());
    j["background"] = bg;
    j["timer"] = {{"mode", timerModeId(timerMode)},
                  {"limit", timeLimit},
                  {"onExpire", expireActionId(onExpire)},
                  {"referenceTime", referenceTime},
                  {"challengeMaxDeaths", challengeMaxDeaths}};
    // Written only when set, so that the levels of before keep their fingerprints.
    if (damageFactor != 1.0f) j["damageFactor"] = damageFactor;
    if (lives > 0) j["lives"] = lives;
    j["medals"] = {{"bronze", medals[0].toJson()},
                   {"silver", medals[1].toJson()},
                   {"gold", medals[2].toJson()},
                   {"platinum", medals[3].toJson()}};
    j["startPowers"] = startPowers;
    j["maxPowers"] = maxPowers;
    j["cameraZoom"] = cameraZoom;
    Json ls = Json::array();
    for (const auto& l : layers) ls.push_back(l.toJson());
    j["layers"] = ls;
    Json objs = Json::array();
    for (const auto& o : objects) objs.push_back(o.toJson());
    j["objects"] = objs;
    Json evs = Json::array();
    for (const auto& e : events) evs.push_back(e.toJson());
    j["events"] = evs;
    j["intro"] = intro.toJson();
    j["outro"] = outro.toJson();
    if (!metadata.empty()) j["metadata"] = metadata;
    return j;
}

std::optional<LevelData> LevelData::fromJson(const Json& j, std::string* error) {
    auto fail = [&](const std::string& msg) -> std::optional<LevelData> {
        if (error) *error = msg;
        return std::nullopt;
    };
    if (!j.is_object()) return fail("le fichier n'est pas un objet JSON");
    if (jstr(j, "format") != "cosmic.level") return fail("format inconnu (attendu cosmic.level)");
    int fv = jget<int>(j, "formatVersion", 1);
    if (fv > kFormatVersion) return fail("niveau créé avec une version plus récente du jeu");
    LevelData d;
    d.id = jstr(j, "id");
    if (d.id.empty()) return fail("identifiant manquant");
    d.name = jstr(j, "name", d.id);
    d.description = jstr(j, "description");
    d.planet = jstr(j, "planet", "sylvanox");
    d.author = jstr(j, "author");
    d.version = jstr(j, "version", "1.0");
    d.difficulty = std::clamp(jget<int>(j, "difficulty", 1), 1, 5);
    d.music = jstr(j, "music");
    d.bossMusic = jstr(j, "bossMusic");
    d.ambience = jstr(j, "ambience");
    d.bounds = rectFromJson(j.value("bounds", Json()), d.bounds);
    if (d.bounds.w < 480 || d.bounds.h < 270 || d.bounds.w > 200000 || d.bounds.h > 50000)
        return fail("dimensions du niveau invalides");
    d.killY = jget<float>(j, "killY", 0.0f);
    d.ambientLight = colorFromJson(j.value("ambientLight", Json()), d.ambientLight);
    d.backgroundColor = colorFromJson(j.value("backgroundColor", Json()), d.backgroundColor);
    if (j.contains("background") && j["background"].is_array())
        for (const auto& l : j["background"]) d.background.push_back(ParallaxLayerDef::fromJson(l));
    if (j.contains("timer")) {
        const Json& t = j["timer"];
        d.timerMode = timerModeFromId(jstr(t, "mode", "limited"));
        d.timeLimit = std::clamp(jget<float>(t, "limit", 600.0f), 10.0f, 36000.0f);
        d.onExpire = expireActionFromId(jstr(t, "onExpire", "gameover"));
        d.referenceTime = std::max(1.0f, jget<float>(t, "referenceTime", 180.0f));
        d.challengeMaxDeaths = std::max(0, jget<int>(t, "challengeMaxDeaths", 0));
    }
    {
        float df = jget<float>(j, "damageFactor", 1.0f);
        d.damageFactor = std::isfinite(df) ? std::clamp(df, 0.25f, 5.0f) : 1.0f;
        d.lives = std::clamp(jget<int>(j, "lives", 0), 0, 99);
    }
    if (j.contains("medals")) {
        const Json& m = j["medals"];
        const char* names[4] = {"bronze", "silver", "gold", "platinum"};
        for (int i = 0; i < 4; ++i)
            if (m.contains(names[i])) d.medals[i] = MedalRule::fromJson(m[names[i]]);
    }
    if (j.contains("startPowers") && j["startPowers"].is_array())
        for (const auto& p : j["startPowers"])
            if (p.is_string()) d.startPowers.push_back(p.get<std::string>());
    d.maxPowers = std::clamp(jget<int>(j, "maxPowers", kDefaultMaxPowers), 0, kMaxPowerSlots);
    d.cameraZoom = std::clamp(jget<float>(j, "cameraZoom", 1.0f), 0.5f, 2.0f);
    if (j.contains("layers") && j["layers"].is_array())
        for (const auto& l : j["layers"]) d.layers.push_back(LevelLayer::fromJson(l));
    d.ensureDefaultLayers();
    if (j.contains("objects") && j["objects"].is_array()) {
        if (j["objects"].size() > 50000) return fail("trop d'objets");
        for (const auto& o : j["objects"]) {
            LevelObject obj = LevelObject::fromJson(o);
            if (obj.type.empty() || obj.id.empty()) continue;
            if (!d.findLayer(obj.layer)) obj.layer = "gameplay";
            d.objects.push_back(std::move(obj));
        }
    }
    if (j.contains("events") && j["events"].is_array())
        for (const auto& e : j["events"]) d.events.push_back(LevelEvent::fromJson(e));
    if (j.contains("intro")) d.intro = CinematicDef::fromJson(j["intro"]);
    if (j.contains("outro")) d.outro = CinematicDef::fromJson(j["outro"]);
    if (j.contains("metadata") && j["metadata"].is_object()) d.metadata = j["metadata"];
    return d;
}

LevelData LevelData::makeEmpty(const std::string& id, const std::string& name, const std::string& planet) {
    LevelData d;
    d.id = id;
    d.name = name;
    d.planet = planet;
    d.description = "Nouveau niveau créé avec l'éditeur.";
    d.bounds = {0, 0, 2400, 960};
    d.music = "music/sylvanox_level.ogg";
    d.ambience = "music/sylvanox_ambience.ogg";
    d.ensureDefaultLayers();
    d.background = {
        ParallaxLayerDef::fromJson({{"texture", "backgrounds/sylvanox/sky.png"}, {"factor", {0.0, 0.0}}, {"fill", true}}),
        ParallaxLayerDef::fromJson({{"texture", "backgrounds/sylvanox/far.png"}, {"factor", {0.12, 0.05}}}),
        ParallaxLayerDef::fromJson({{"texture", "backgrounds/sylvanox/mid.png"}, {"factor", {0.3, 0.12}}}),
        ParallaxLayerDef::fromJson({{"texture", "backgrounds/sylvanox/near.png"}, {"factor", {0.55, 0.25}}}),
    };
    d.medals[1].maxTime = 240;
    d.medals[2].maxTime = 150;
    d.medals[3].maxTime = 110;
    d.medals[3].maxDeaths = 0;
    d.timeLimit = 600;
    d.referenceTime = 120;
    LevelObject ground;
    ground.id = "o1";
    ground.type = "solid";
    ground.layer = "terrain";
    ground.rect = {0, 800, 2400, 160};
    ground.props = {{"material", "ruin_stone"}};
    d.objects.push_back(ground);
    LevelObject start;
    start.id = "o2";
    start.type = "player_start";
    start.layer = "gameplay";
    start.rect = {160, 778, 10, 22};
    d.objects.push_back(start);
    LevelObject exitObj;
    exitObj.id = "o3";
    exitObj.type = "exit";
    exitObj.layer = "gameplay";
    exitObj.rect = {2200, 736, 40, 64};
    d.objects.push_back(exitObj);
    return d;
}

}  // namespace cosmic
