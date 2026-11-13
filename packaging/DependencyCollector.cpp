#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "engine/core/FileUtil.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/levels/DecorAtlas.h"
#include "game/levels/ObjectTypes.h"
#include "packaging/Package.h"

namespace cosmic {

namespace PackageFormat {

bool allowedExtension(const std::string& path) {
    std::string ext = FileUtil::extensionLower(Paths::fromUtf8(path));
    static const char* ok[] = {".json", ".png", ".jpg", ".jpeg", ".bmp", ".wav", ".ogg",
                               ".mp3",  ".flac", ".ttf", ".otf", ".txt", ".md"};
    for (const char* e : ok)
        if (ext == e) return true;
    return false;
}

int compareVersions(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& s) {
        std::vector<int> v;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, '.')) v.push_back(std::atoi(item.c_str()));
        while (v.size() < 3) v.push_back(0);
        return v;
    };
    auto pa = parts(a), pb = parts(b);
    for (size_t i = 0; i < 3; ++i)
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    return 0;
}

std::string engineVersion() { return COSMIC_VERSION_STRING; }

}  // namespace PackageFormat

std::vector<std::string> DependencyCollector::coreRuntimeAssets() {
    std::vector<std::string> v = {
        "animations/nova.json", "animations/projectiles.json",
        "textures/fx/glow.png", "textures/fx/particle.png", "textures/fx/spark.png", "textures/fx/star.png",
        "textures/fx/ring.png", "textures/fx/smoke.png", "textures/fx/dust.png", "textures/fx/slash.png",
        "textures/fx/hit.png", "textures/fx/explosion.png", "textures/fx/shield.png", "textures/fx/teleport.png",
        "textures/fx/warning.png",
        "ui/icons_powers.png", "ui/icons_medals.png", "ui/icons_hud.png",
        "ui/glyphs/xbox.png", "ui/glyphs/playstation.png", "ui/glyphs/generic.png",
        "fonts/Orbitron.ttf", "fonts/ChakraPetch-Regular.ttf", "fonts/ChakraPetch-SemiBold.ttf",
        "fonts/OFL-Orbitron.txt", "fonts/OFL-ChakraPetch.txt",
        "music/victory.ogg",
    };
    for (const char* s : {"jump", "double_jump", "land", "footstep_a", "footstep_b", "wall_jump", "slide", "slash",
                          "slash_hit", "player_hurt", "player_death", "respawn", "checkpoint", "collect_shard",
                          "collect_relic", "collect_orb", "power_unlock", "door_open", "lever", "crumble", "bounce",
                          "laser_charge", "laser_fire", "enemy_shoot", "enemy_alert", "enemy_telegraph", "enemy_hurt",
                          "enemy_death", "golem_slam", "explosion", "boss_roar", "boss_phase", "summon", "wisp_teleport",
                          "shield_block", "energy_low", "teleport_in", "wave_hit", "portal", "timer_tick", "timer_warning",
                          "dash", "spore_spit"})
        v.push_back(std::string("sounds/") + s + ".wav");
    return v;
}

void DependencyCollector::asset(const std::string& rel) {
    if (rel.empty()) return;
    std::string n = Paths::normalizeSlashes(rel);
    if (!Paths::isSafeRelative(n)) {
        out_.warnings.push_back("Chemin ignoré (non relatif) : " + rel);
        return;
    }
    out_.assets.insert(n);
}

void DependencyCollector::animation(const std::string& rel) {
    if (rel.empty() || out_.assets.count(rel)) return;
    asset(rel);
    if (auto j = res_.json(rel)) asset(jstr(*j, "texture"));
    else out_.warnings.push_back("Animation introuvable : " + rel);
}

void DependencyCollector::enemy(const std::string& id) {
    if (id.empty() || out_.enemies.count(id)) return;
    const EnemyDefinition* d = content_.enemy(id);
    if (!d) {
        out_.warnings.push_back("Ennemi inconnu : " + id);
        return;
    }
    out_.enemies.insert(id);
    if (!d->assetRoot.empty()) res_.pushMount(d->assetRoot);
    animation(d->animation);
    if (!d->assetRoot.empty()) res_.popMount();
    sound(d->hurtSound);
    sound(d->deathSound);
    sound(d->alertSound);
    for (const auto& a : d->attacks) {
        sound(a.sound);
        sound(a.telegraphSound);
        if (a.type == AttackType::Summon) enemy(a.summonId);
    }
    if (d->deathSpawnCount > 0) enemy(d->deathSpawnId);
}

void DependencyCollector::power(const std::string& id) {
    if (id.empty() || out_.powers.count(id)) return;
    const PowerDefinition* d = content_.power(id);
    if (!d) {
        out_.warnings.push_back("Pouvoir inconnu : " + id);
        return;
    }
    out_.powers.insert(id);
    sound(d->sound);
    sound(d->soundEnd);
}

DependencySet DependencyCollector::collect(const LevelData& level) {
    out_ = DependencySet{};
    out_.planet = level.planet;
    for (const auto& a : coreRuntimeAssets()) asset(a);
    asset(level.music);
    asset(level.bossMusic);
    asset(level.ambience);
    for (const auto& l : level.background) asset(l.texture);
    if (const PlanetData* p = content_.planet(level.planet)) {
        asset(p->illustration);
        asset(p->banner);
        asset(p->music);
    }
    for (const auto& pw : level.startPowers) power(pw);

    for (const auto& o : level.objects) {
        if (o.type == "power_gate") {
            // Only the sprites of this kind of obstacle.
            std::string pw = o.propStr("power", "dash");
            if (pw == "dash" || pw == "impulse") animation("animations/breakable_wall.json");
            else if (pw == "teleport") animation("animations/phase_wall.json");
            else if (pw == "ground_pound") animation("animations/cracked_floor.json");
            else asset("sprites/objects/laser_beam.png");
            asset("ui/icons_powers.png");
            continue;
        }
        if (const ObjectTypeInfo* t = ObjectTypes::find(o.type))
            for (const auto& a : t->assets) {
                if (a.size() > 5 && a.substr(a.size() - 5) == ".json" && a.rfind("animations/", 0) == 0) animation(a);
                else asset(a);
            }
        if (o.type == "solid") {
            std::string m = o.propStr("material", "ruin_stone");
            asset("textures/tiles/" + m + ".json");
            asset("textures/tiles/" + m + ".png");
        } else if (o.type == "crumble_platform") {
            std::string st = o.propStr("style", "ruins");
            asset(st == "ruins" ? std::string("sprites/objects/crumble_platform.png")
                                : "sprites/objects/crumble_platform_" + st + ".png");
        } else if (o.type == "liquid") {
            asset("sprites/objects/liquid_" + o.propStr("kind", "acid") + ".png");
        } else if (o.type == "enemy") {
            enemy(o.propStr("enemy"));
        } else if (o.type == "power_unlock") {
            power(o.propStr("power"));
        } else if (o.type == "combat_zone") {
            asset(o.propStr("music"));
        } else if (o.type == "decor") {
            if (const DecorSprite* s = DecorAtlas::shared(res_).find(o.propStr("sprite"))) {
                asset(s->atlas);
                asset(s->texturePath);
            } else {
                out_.warnings.push_back("Sprite de décor inconnu : " + o.propStr("sprite"));
            }
        } else if (o.type == "ladder") {
            if (const DecorSprite* s = DecorAtlas::shared(res_).find("hanging_vine_long")) {
                asset(s->atlas);
                asset(s->texturePath);
            }
        }
    }
    auto stepAssets = [&](const Json& p) {
        for (const char* k : {"sound", "music"})
            if (p.contains(k) && p[k].is_string()) asset(p[k].get<std::string>());
        if (p.contains("power") && p["power"].is_string()) power(p["power"].get<std::string>());
    };
    for (const auto& e : level.events)
        for (const auto& a : e.actions) stepAssets(a.params);
    for (const auto& s : level.intro.steps) stepAssets(s.params);
    for (const auto& s : level.outro.steps) stepAssets(s.params);
    return out_;
}

}  // namespace cosmic
