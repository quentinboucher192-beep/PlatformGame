#include "ui/screens/CreatureArt.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>

#include "game/GameServices.h"

namespace cosmic {

namespace {

struct BackdropLayer {
    std::string texture;
    Color tint = Color::white();
};

/// The four layers of a planet's backdrop: its own folder (backgrounds/<planet>/), or, for a
/// planet drawn with another one's layers (2.48 Les Confins: Pyralis' layers in ash and violet),
/// the sky, far, mid and near layers of its first level, with their tints. Read once per planet.
const std::vector<BackdropLayer>& backdropOf(GameServices& g, const std::string& planet) {
    static std::map<std::string, std::vector<BackdropLayer>> cache;
    auto it = cache.find(planet);
    if (it != cache.end()) return it->second;
    std::vector<BackdropLayer> layers;
    for (const char* layer : {"sky", "far", "mid", "near"}) {
        const std::string path = "backgrounds/" + planet + "/" + layer + ".png";
        if (g.app.resources().texture(path)) layers.push_back({path});
    }
    if (layers.empty())
        for (const LevelEntry* e : g.content.levelsForPlanet(planet)) {
            auto level = g.content.loadLevel(e->id);
            if (!level) continue;
            for (const auto& l : level->background)
                for (const char* name : {"/sky.png", "/far.png", "/mid.png", "/near.png"})
                    if (l.texture.size() > std::strlen(name) && l.texture.compare(l.texture.size() - std::strlen(name), std::string::npos, name) == 0)
                        layers.push_back({l.texture, l.tint});
            break;
        }
    return cache.emplace(planet, std::move(layers)).first->second;
}

Color times(Color a, Color b) {
    return {static_cast<uint8_t>(a.r * b.r / 255), static_cast<uint8_t>(a.g * b.g / 255), static_cast<uint8_t>(a.b * b.b / 255),
            static_cast<uint8_t>(a.a * b.a / 255)};
}

}  // namespace

void drawPlanetBackdrop(GameServices& g, const std::string& planet, const Rect& dst, Color tint) {
    Renderer& r = g.app.renderer();
    r.fillRect(dst, Color{8, 11, 24, 255});
    for (const BackdropLayer& l : backdropOf(g, planet))
        if (TexturePtr t = g.app.resources().texture(l.texture)) r.drawTexture(*t, nullptr, dst, times(tint, l.tint));
}

void drawCreature(GameServices& g, const EnemyDefinition& def, const Rect& box, float t, Color tintMul, const std::string& clipName) {
    AnimationSetPtr set = g.app.resources().animation(def.animation);
    if (!set || !set->texture) return;
    const AnimationClip* c = set->clip(clipName);
    if (!c) c = set->clip("idle");
    if (!c && !set->clips.empty()) c = &set->clips.begin()->second;
    if (!c || c->frameCount() == 0) return;
    float total = std::max(0.01f, c->totalDuration());
    float local = std::fmod(std::max(0.0f, t), total), acc = 0.0f;
    int frame = 0;
    for (int i = 0; i < c->frameCount(); ++i) {
        acc += c->durations[static_cast<size_t>(i)];
        if (local < acc) {
            frame = i;
            break;
        }
    }
    Rect src = set->frameRect(*c, frame);
    float k = std::min(box.w / src.w, box.h / src.h);
    if (k >= 1.0f) k = std::floor(k);
    Vec2 size{src.w * k, src.h * k};
    Rect dst = Rect::fromCenter(box.center(), size);
    Color tint{static_cast<uint8_t>(def.tint.r * tintMul.r / 255), static_cast<uint8_t>(def.tint.g * tintMul.g / 255),
               static_cast<uint8_t>(def.tint.b * tintMul.b / 255), tintMul.a};
    g.app.renderer().drawTexture(*set->texture, &src, dst, tint);
}

namespace {

const char* kTelegraph = "telegraph_";

/// Group of a clip in the showing order (attacks keep their wind-up just before them).
int clipGroup(const std::string& c) {
    if (c == "idle") return 0;
    if (c == "walk" || c == "move" || c == "run" || c == "fly") return 1;
    if (c == "summon") return 3;
    if (c == "roar") return 4;
    if (c == "teleport_out" || c == "teleport_in") return 5;
    if (c == "hurt") return 6;
    if (c == "death") return 7;
    return 2;  // attacks and their wind-ups
}

/// The attack a clip belongs to ("charge" for "telegraph_charge", "attack" for "telegraph").
std::string attackOf(const std::string& c) {
    if (c == "telegraph") return "attack";
    if (c.rfind(kTelegraph, 0) == 0) return c.substr(std::char_traits<char>::length(kTelegraph));
    return c;
}

const std::map<std::string, std::string>& clipWords() {
    static const std::map<std::string, std::string> words = {
        {"idle", "Repos"},          {"walk", "Déplacement"},       {"move", "Déplacement"},     {"run", "Course"},
        {"fly", "Vol"},             {"attack", "Attaque"},         {"special", "Attaque spéciale"},
        {"slam", "Frappe"},         {"charge", "Charge"},          {"barrage", "Barrage"},      {"laser", "Laser"},
        {"beam", "Rayon"},          {"bite", "Morsure"},           {"breath", "Souffle"},       {"dive", "Plongeon"},
        {"meteor", "Météores"},     {"spores", "Spores"},          {"vines", "Lianes"},         {"volley", "Salve"},
        {"summon", "Invocation"},   {"roar", "Rugissement"},       {"hurt", "Touché"},          {"death", "Défaite"},
        {"teleport_out", "Disparition"}, {"teleport_in", "Apparition"},
    };
    return words;
}

}  // namespace

std::vector<std::string> creatureClips(GameServices& g, const EnemyDefinition& def) {
    std::vector<std::string> clips;
    if (AnimationSetPtr set = g.app.resources().animation(def.animation))
        for (const auto& [name, c] : set->clips)
            if (c.frameCount() > 0) clips.push_back(name);
    std::sort(clips.begin(), clips.end(), [](const std::string& a, const std::string& b) {
        int ga = clipGroup(a), gb = clipGroup(b);
        if (ga != gb) return ga < gb;
        std::string ka = attackOf(a), kb = attackOf(b);
        if (ka != kb) return ka < kb;
        bool ta = ka != a, tb = kb != b;  // the wind-up first
        if (ta != tb) return ta;
        return a < b;
    });
    return clips;
}

float creatureClipDuration(GameServices& g, const EnemyDefinition& def, const std::string& clip) {
    AnimationSetPtr set = g.app.resources().animation(def.animation);
    if (!set) return 0.0f;
    const AnimationClip* c = set->clip(clip);
    return c ? c->totalDuration() : 0.0f;
}

std::string creatureClipLabel(const EnemyDefinition& def, const std::string& clip) {
    const auto& words = clipWords();
    bool windUp = attackOf(clip) != clip;
    std::string base = windUp ? attackOf(clip) : clip;
    std::string name;
    if (auto it = words.find(base); it != words.end()) name = it->second;
    if (name.empty() || base == "attack" || base == "special") {
        // The attack using this clip, by its own name (« Prisme »).
        for (const auto& a : def.attacks)
            if ((windUp ? a.telegraphAnim : a.attackAnim) == clip && !a.name.empty()) {
                name = name.empty() ? a.name : name + " (" + a.name + ")";
                break;
            }
    }
    if (name.empty()) name = "Animation";
    return windUp ? "Préparation : " + name : name;
}

}  // namespace cosmic
