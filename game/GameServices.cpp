#include "game/GameServices.h"

#include "engine/core/I18n.h"

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"

namespace cosmic {

void GameServices::loadAll() {
    settings = SettingsStore::load();
    // The language chosen in the Windows installer, when it is new (see Settings.h).
    if (applyInstallerLanguage(settings, Paths::installDir() / "language.ini")) saveSettings();
    content.reload();
    ratings.init(content);
    progression.load();
    records.load();
    bossStats.load();
    ghosts.open(Paths::userDir() / "ghosts");
    ghosts.trimCache(static_cast<uint64_t>(settings.ghosts.cacheMB) * 1024 * 1024);
    segments.load();
    if (std::string err; !achievementCatalog.loadDefault(&err)) Log::warn("Succès : {}", err);
    achievements.load();
    online.onAchievementEvent = [this](const std::string& key) {
        achievements.stats().add(key);
        checkAchievements();
        achievements.save();
    };
    checkAchievements(false);  // what the existing saves already prove (no toast at startup)
    if (std::string err; !cosmetics.loadDefault(&err)) Log::warn("Collection : {}", err);
    collection.load();
    if (collection.firstStart()) {
        // Rewards already earned before 2.19: equipped by nobody, announced once by a toast.
        auto p = cosmeticProgress();
        auto already = collection.fresh(cosmetics, p);
        for (const CosmeticItem* i : already) collection.markAnnounced(i->id);
        if (!already.empty())
            online.notify(OnlineService::Notification::Kind::Success,
                          std::to_string(already.size()) + (already.size() > 1 ? " récompenses débloquées" : " récompense débloquée"),
                          "Avec vos étoiles · menu principal → Collection");
    }
    discoverFromSaves();
    app.audio().onMusicStarted = [this](const std::string& path) {
        // The jukebox: a track becomes listenable once heard in the game.
        std::string rel = app.resources().relativeSoundPath(path);
        if (!rel.empty() && collection.heard.insert(rel).second) collection.save();
    };
    news.load();
    news.startScan(content);
    applySettings();
}

CosmeticProgress GameServices::cosmeticProgress() const {
    CosmeticProgress p;
    p.stars = achievements.stars(achievementCatalog);
    p.totalStars = achievementCatalog.totalStars();
    p.book = &achievements;
    return p;
}

std::string GameServices::menuMusic() const {
    // Only a track already heard, and still shipped with the game.
    if (!collection.menuMusic.empty() && collection.heard.count(collection.menuMusic) &&
        !app.resources().soundPath(collection.menuMusic).empty())
        return collection.menuMusic;
    return "music/menu_theme.ogg";
}

void GameServices::discoverFromSaves() {
    bool changed = false;
    auto hear = [&](const std::string& rel) {
        if (!rel.empty() && collection.heard.insert(rel).second) changed = true;
    };
    hear("music/menu_theme.ogg");
    for (const PlanetData* p : content.planets()) {
        if (!progression.planetUnlocked(*p, content)) continue;
        hear(p->music);
        for (const LevelEntry* e : content.levelsForPlanet(p->id)) {
            if (!progression.levelCompleted(e->id) || collection.met.count("@" + e->id)) continue;
            auto lv = content.loadLevel(e->id);
            if (!lv) continue;
            // A completed level: its music, ambience, boss music and creatures were met.
            hear(lv->music);
            hear(lv->ambience);
            hear(lv->bossMusic);
            hear("music/victory.ogg");
            for (const auto& o : lv->objects)
                if (o.type == "enemy")
                    if (std::string id = jstr(o.props, "enemy"); !id.empty()) collection.met.insert(id);
            collection.met.insert("@" + e->id);  // this level was scanned
            changed = true;
        }
    }
    if (changed) collection.save();
}

AchievementInputs GameServices::achievementInputs() const {
    AchievementInputs in;
    in.content = &content;
    in.progression = &progression;
    in.records = &records;
    in.bosses = &bossStats;
    in.stats = &achievements.stats();
    in.playTimeSeconds = progression.playTimeSeconds;
    for (const PlanetData* p : content.planets())
        for (const LevelEntry* e : content.levelsForPlanet(p->id)) in.campaignLevels.push_back(e->id);
    for (const LevelEntry* e : content.levels()) {
        if (online.communityInstall(e->id)) {
            if (const LevelRecord* r = records.get(e->id); r && r->completions > 0) ++in.communityLevelsFinished;
        } else if (e->source == ContentSource::User) {
            ++in.userLevels;
        }
    }
    std::error_code ec;
    if (fs::is_directory(Paths::userEnemiesDir(), ec))
        for (const auto& f : fs::directory_iterator(Paths::userEnemiesDir(), ec))
            if (f.path().extension() == ".json") ++in.userEnemies;
    if (fs::is_directory(Paths::userLevelsDir(), ec))
        for (const auto& f : fs::directory_iterator(Paths::userLevelsDir(), ec)) {
            std::string name = Paths::toUtf8(f.path().filename());
            if (name.size() > 16 && name.compare(name.size() - 16, 16, ".completion.json") == 0) ++in.completionProofs;
        }
    in.onlineAccount = online.loggedIn();
    in.friends = online.friendCount();
    return in;
}

void GameServices::checkAchievements(bool announce) {
    if (achievementCatalog.achievements().empty()) return;
    auto fresh = achievements.evaluate(achievementCatalog, achievementInputs(), FileUtil::nowString());
    // The profile shows them (declared by the game) when the player is connected.
    std::vector<std::string> ids;
    for (const auto& a : achievementCatalog.achievements())
        if (achievements.unlocked(a.id)) ids.push_back(a.id);
    online.setAchievements(std::move(ids));
    if (fresh.empty()) return;
    achievements.save();
    if (!announce) {
        achievements.takeAnnouncements();
        return;
    }
    if (fresh.size() > 3) {
        int stars = 0;
        for (const AchievementDef* a : fresh) stars += a->stars;
        online.notify(OnlineService::Notification::Kind::Success, std::to_string(fresh.size()) + " succès débloqués",
                      "+" + std::to_string(stars) + " étoiles · menu principal → Succès");
        return;
    }
    for (const AchievementDef* a : fresh)
        online.notify(OnlineService::Notification::Kind::Success, "Succès débloqué : " + a->name,
                      a->description + " (" + std::to_string(a->stars) + (a->stars > 1 ? " étoiles)" : " étoile)"));
}

void GameServices::checkForUpdate() {
    if (!online.usable()) return;
    online.get("/api/v1/client/latest?platform=windows", [this](const ApiResult& r) {
        if (!r.ok || !r.data.is_object() || !r.data.contains("release") || !r.data["release"].is_object()) return;
        const Json& rel = r.data["release"];
        std::string v = jstr(rel, "version");
        if (v.empty() || !versionNewer(v, COSMIC_VERSION_STRING)) return;
        news.offerRelease(v, jget<int64_t>(rel, "size", 0), jstr(rel, "sha256"), jstr(rel, "download"), jstr(rel, "notes"));
    }, false);
}

void GameServices::applySettings() {
    i18n::setLanguage(settingsLanguage(settings));
    app.input().setBindings(settings.input);
    app.audio().setVolumes(settings.audio);
    if (!app.headless()) {
        if (app.fullscreen() != settings.graphics.fullscreen) app.setFullscreen(settings.graphics.fullscreen);
        app.setVSync(settings.graphics.vsync);
    }
    app.debugOverlay = app.debugOverlay && settings.gameplay.debugMode;
    progression.unlockAll = settings.gameplay.unlockAll;
}

void GameServices::playMusic(const std::string& rel) {
    std::string p = app.resources().soundPath(rel);
    if (!p.empty()) app.audio().playMusic(p, 1200);
}

}  // namespace cosmic
