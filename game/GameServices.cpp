#include "game/GameServices.h"

#include "game/online/ProgressMerge.h"

#include <memory>
#include <set>

#include <SDL3/SDL_timer.h>

#include "engine/core/I18n.h"

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/online/HttpTransport.h"

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
    replayLibrary.open(Paths::userDir() / "replays" / "downloaded");
    segments.load();
    training.load();
    powerKeyOverrides.load();
    if (std::string err; !achievementCatalog.loadDefault(&err)) Log::warn("Succès : {}", err);
    achievements.load();
    online.onAchievementEvent = [this](const std::string& key) {
        achievements.stats().add(key);
        checkAchievements();
        achievements.save();
    };
    challenges.onRewardsChanged = [this] { challengeRewardsChanged(); };
    // The Collection before the first check: its achievements read the tracks heard and the
    // creatures met, also those the saves prove (2.24).
    collection.load();
    discoverFromSaves();
    checkAchievements(false);  // what the existing saves already prove (no toast at startup)
    if (std::string err; !cosmetics.loadDefault(&err)) Log::warn("Collection : {}", err);
    if (collection.firstStart()) {
        // Rewards already earned before 2.19: equipped by nobody, announced once by a toast.
        auto p = cosmeticProgress();
        auto already = collection.fresh(cosmetics, p);
        for (const CosmeticItem* i : already) collection.markAnnounced(i->id);
        collection.save();
        if (!already.empty())
            online.notify(OnlineService::Notification::Kind::Success,
                          std::to_string(already.size()) + (already.size() > 1 ? " récompenses débloquées" : " récompense débloquée"),
                          "Avec vos étoiles · menu principal → Collection");
    }
    noteOutfit(false);  // what was already worn before 2.24 counts too
    syncCosmetics();
    app.audio().onMusicStarted = [this](const std::string& path) {
        // The jukebox: a track becomes listenable once heard in the game.
        std::string rel = app.resources().relativeSoundPath(path);
        if (!rel.empty() && collection.heard.insert(rel).second) collection.save();
    };
    news.load();
    // « NOUVEAU » pills: what came after the version the player had before this update.
    newBadges.loadCatalog(Paths::dataDir() / "news" / "new_features.json");
    newBadges.init(Paths::savesDir() / "new_badges.json", news.lastVersion(), COSMIC_VERSION_STRING);
    ui.isNewFeature = [this](const std::string& id) { return newBadges.isNew(id); };
    ui.featureSeen = [this](const std::string& id) {
        if (newBadges.markSeen(id)) newBadges.save();  // a step of a trail (a tab…) has nothing to save
    };
    news.startScan(content);
    applySettings();
}

// ------------------------------------------------- suits created on Cosmic Online

TexturePtr GameServices::suitSheet(const std::string& look, const SuitColors* colors) {
    if (look.empty() || look == "standard") return nullptr;
    if (!isServerSuitRef(look)) return app.resources().texture("sprites/player/suits/nova_" + look + ".png");
    std::optional<SuitColors> c;
    if (colors) c = *colors;
    else if (const ServerSuit* s = serverSuits.find(look)) c = s->colors;
    else if (const CosmeticItem* w = challenges.wonRewards().suit(look); w && w->suitColors) c = w->suitColors;
    if (!c) {
        // Not known yet (created since the last reading): the normal sheet meanwhile, and the
        // list read again (at most every two minutes).
        const uint64_t now = SDL_GetTicks();
        if (serverSuitsFetch_ && online.usable() && (serverSuitsAskedMs_ == 0 || now - serverSuitsAskedMs_ > 120000)) {
            serverSuitsAskedMs_ = now;
            refreshServerSuits();
        }
        return nullptr;
    }
    return suitTextures.get(app.resources(), app.renderer(), *c);
}

void GameServices::startServerSuits(bool fetch) {
    serverSuits.setFile(Paths::userDir() / "online" / "creations.json");
    serverSuits.load();  // drawn offline after one reading
    serverSuitsFetch_ = fetch;
    serverSuitsTimer_ = 0.0f;
    if (fetch) refreshServerSuits();
}

void GameServices::refreshServerSuits() {
    if (serverSuitsBusy_ || !online.usable()) return;
    serverSuitsBusy_ = true;
    serverSuitsTimer_ = 0.0f;
    online.get("/api/v1/creations", [this](const ApiResult& r) {
        serverSuitsBusy_ = false;
        // A cached answer (no network) changes nothing that the file does not already keep.
        if (!r.ok || r.fromCache) return;
        if (serverSuits.applyList(jfield(r.data, "creations", Json()))) serverSuits.save();
    });
}

void GameServices::updateServerSuits(float dt) {
    if (!serverSuitsFetch_ || serverSuitsBusy_ || !online.usable()) return;
    serverSuitsTimer_ += dt;
    if (serverSuitsTimer_ >= 30.0f * 60.0f) refreshServerSuits();
}

// ------------------------------------------------- campaign revisions (2.45)

void GameServices::startCampaignRevisions(bool fetch) {
    // 2.45: the enemy revisions first (the level revisions bound to them need them).
    enemyRevisions.setDir(Paths::userDir() / "online" / "enemies");
    enemyRevisions.load();
    enemyRevisions.applyTo(content, online.config().serverUrl, COSMIC_VERSION_STRING);
    campaignRevisions.setDir(Paths::userDir() / "online" / "campaign");
    campaignRevisions.load();
    applyCampaignRevisions();  // the revisions kept on this computer: offline too
    campaignFetch_ = fetch;
    campaignTimer_ = 0.0f;
    if (fetch) refreshCampaignRevisions();
}

void GameServices::updateCampaignRevisions(float dt) {
    if (campaignApplyPending_ && news.scanned()) applyCampaignRevisions();
    if (!campaignFetch_ || campaignRefreshing_ || !online.usable()) return;
    campaignTimer_ += dt;
    if (campaignTimer_ >= 30.0f * 60.0f) refreshCampaignRevisions();
}

void GameServices::refreshCampaignRevisions(std::function<void(const std::string&)> done) {
    if (campaignRefreshing_ || !online.usable()) {
        if (done) done(online.usable() ? std::string() : std::string("Cosmic Online est indisponible."));
        return;
    }
    campaignRefreshing_ = true;
    campaignTimer_ = 0.0f;
    // 2.45: the enemy revisions before the campaign's (a level revision is checked with the
    // definitions it is bound to). A server without them (404) has none: the campaign goes on.
    online.get(
        "/api/v1/enemies/revisions",
        [this, done](const ApiResult& r) {
            const std::string server = online.config().serverUrl;
            if (!r.ok) return fetchCampaignManifest(done, r.status == 404 ? std::string() : r.message);
            auto todo = enemyRevisions.applyManifest(r.data, server, content, COSMIC_VERSION_STRING);
            enemyRevisions.save();
            if (todo.empty()) {
                enemyRevisions.applyTo(content, server, COSMIC_VERSION_STRING);
                return fetchCampaignManifest(done, {});
            }
            auto left = std::make_shared<size_t>(todo.size());
            auto errors = std::make_shared<std::string>();
            for (const EnemyRevisionFile& f : todo)
                online.getBinary(
                    f.path,
                    [this, f, left, errors, done, server](const HttpResponse& h) {
                        std::string err;
                        if (h.networkFailure() || h.status != 200) err = "téléchargement impossible";
                        else enemyRevisions.install(content, f, h.body, &err);
                        if (!err.empty()) *errors = f.enemyId + " : " + err;
                        if (--*left > 0) return;
                        enemyRevisions.save();
                        enemyRevisions.applyTo(content, server, COSMIC_VERSION_STRING);
                        fetchCampaignManifest(done, *errors);
                    },
                    static_cast<size_t>(f.size) + 1024);
        },
        false);
}

void GameServices::fetchCampaignManifest(std::function<void(const std::string&)> done, std::string firstError) {
    online.get(
        "/api/v1/campaign/revisions",
        [this, done, firstError](const ApiResult& r) {
            if (!r.ok) {
                campaignRefreshing_ = false;
                campaignTimer_ = 28.0f * 60.0f;  // tried again in two minutes
                if (done) done(r.message);
                return;
            }
            auto todo = campaignRevisions.applyManifest(r.data, online.config().serverUrl, content, COSMIC_VERSION_STRING);
            campaignRevisions.save();
            auto left = std::make_shared<size_t>(todo.size());
            auto errors = std::make_shared<std::string>(firstError);
            auto finish = [this, done, errors]() {
                campaignRevisions.prune();
                campaignRevisions.save();
                applyCampaignRevisions();
                campaignRefreshing_ = false;
                if (done) done(*errors);
            };
            if (todo.empty()) return finish();
            for (const CampaignRevisionFile& f : todo)
                online.getBinary(
                    f.path,
                    [this, f, left, errors, finish](const HttpResponse& h) {
                        std::string err;
                        if (h.networkFailure() || h.status != 200) err = "téléchargement impossible";
                        else campaignRevisions.install(content, f, h.body, &err);
                        if (!err.empty()) *errors = f.levelId + " : " + err;
                        if (--*left == 0) finish();
                    },
                    static_cast<size_t>(f.size) + 1024);
        },
        false);
}

void GameServices::applyCampaignRevisions() {
    // The news compare the bundled levels on a worker thread at startup: the entries change
    // once it is done (a few frames later).
    if (!news.scanned()) {
        campaignApplyPending_ = true;
        return;
    }
    campaignApplyPending_ = false;
    auto files = campaignRevisions.currentFiles(content, online.config().serverUrl, COSMIC_VERSION_STRING);
    bool changed = false;
    for (const LevelEntry* e : content.levels()) {
        if (e->source != ContentSource::Builtin) continue;
        auto it = files.find(e->id);
        fs::path want = it == files.end() ? fs::path() : it->second;
        fs::path now = e->bundledPath.empty() ? fs::path() : e->path;
        if (want != now) {
            // A file that is not a level of that id: the bundled one stays.
            if (!content.setCampaignRevision(e->id, want)) content.setCampaignRevision(e->id, {});
            changed = true;
            Log::info("Campagne en ligne : {} -> {}", e->id, want.empty() ? std::string("niveau d'origine") : Paths::toUtf8(want));
        }
        std::optional<DifficultyRating> rating;
        if (!e->bundledPath.empty())
            if (const Json* r = campaignRevisions.ratingOf(e->id); r && jget<int>(*r, "version", 0) == DifficultyRating::kRatingVersion)
                rating = DifficultyRating::fromJson(*r);
        ratings.setCampaignRating(e->id, rating);
    }
    if (changed) online.fingerprints().clear();
}

CosmeticProgress GameServices::cosmeticProgress() const {
    CosmeticProgress p;
    p.stars = achievements.stars(achievementCatalog);
    p.totalStars = achievementCatalog.totalStars();
    p.book = &achievements;
    p.challenges = &challenges.rewardProgress();
    p.won = &challenges.wonRewards();
    return p;
}

void GameServices::challengeRewardsChanged(bool announce) {
    if (cosmetics.items().empty()) return;
    syncCosmetics();  // a reward of the challenges unlocked (or not any more) changes what is worn
    // Unlocked by this reading of the server (not before it): a toast; the card « Nouvelles
    // récompenses » (end of a challenge or of a level) lets the player equip them.
    const CosmeticProgress p = cosmeticProgress();
    std::set<std::string> now;
    for (const CosmeticItem& i : cosmetics.items())
        if ((i.challengeReward() || (p.won && p.won->has(i.id))) && cosmeticUnlocked(i, p)) now.insert(i.id);
    // 2.43: the server titles (and suits) won at a challenge.
    std::vector<const CosmeticItem*> all;
    for (const CosmeticItem& i : cosmetics.items()) all.push_back(&i);
    if (p.won)
        for (const auto* list : {&p.won->titles, &p.won->suits})
            for (const CosmeticItem& t : *list) {
                now.insert(t.id);
                all.push_back(&t);
            }
    if (announce)
        for (const CosmeticItem* i : all)
            if (now.count(i->id) && !challengeRewardsUnlocked_.count(i->id) && !collection.announced(i->id))
                online.notify(OnlineService::Notification::Kind::Success, "Récompense débloquée : " + i->name,
                              cosmeticCategorySingular(i->category) + " · défis · menu principal → Collection");
    challengeRewardsUnlocked_ = std::move(now);
}

void GameServices::syncCosmetics() {
    if (cosmetics.items().empty()) return;  // catalogue not loaded (yet)
    online.setCosmetics(collection.equippedIds(cosmetics, cosmeticProgress()));
}

std::vector<std::string> GameServices::shownBadges() const { return cosmic::shownBadges(collection.badges, achievementCatalog, achievements); }

void GameServices::syncBadges() {
    if (achievementCatalog.achievements().empty()) return;  // catalogue not loaded
    // Never chosen on this computer: the profile keeps the badges chosen elsewhere.
    if (!collection.badgesChosen) return;
    online.setBadges(shownBadges());
}

void GameServices::noteOutfit(bool announce) {
    if (cosmetics.items().empty()) return;
    const CosmeticProgress p = cosmeticProgress();
    AchievementStats& a = achievements.stats();
    int worn = 0;  // categories wearing an unlocked reward (not their default)
    bool changed = false;
    for (const auto& c : cosmetics.categories()) {
        const CosmeticItem* i = collection.equipped(cosmetics, c.id, p);
        if (!i || i->isDefault) continue;
        ++worn;
        if (!a.setHas("equipped_items", i->id)) {
            a.insert("equipped_items", i->id);
            changed = true;
        }
    }
    if (worn > a.maximum("outfit_categories")) {
        a.raise("outfit_categories", worn);
        changed = true;
    }
    if (!changed) return;
    achievements.save();
    checkAchievements(announce);
}

namespace {

/// Files of the jukebox's tracks (data/music/tracks.json), read once.
const std::vector<std::string>& jukeboxTracks() {
    static const std::vector<std::string> tracks = [] {
        std::vector<std::string> out;
        auto j = FileUtil::loadJson(Paths::dataDir() / "music" / "tracks.json");
        if (j && j->is_object())
            if (auto it = j->find("tracks"); it != j->end() && it->is_array())
                for (const auto& t : *it)
                    if (std::string f = jstr(t, "file"); !f.empty()) out.push_back(f);
        return out;
    }();
    return tracks;
}

}  // namespace

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
    // Collection (2.24): the jukebox's tracks heard, the bestiary's creatures met (the game's
    // own creatures, guardians and the small slimes born from a split aside: the gallery's
    // Bestiaire).
    for (const std::string& f : jukeboxTracks()) in.tracksHeard += collection.heard.count(f) ? 1 : 0;
    std::set<std::string> splitChildren;
    for (const EnemyDefinition* d : content.enemies())
        if (d->deathSpawnCount > 0 && d->deathSpawnId != d->id) splitChildren.insert(d->deathSpawnId);
    for (const std::string& id : collection.met)
        if (const EnemyDefinition* d = content.enemy(id); d && d->builtin && !d->isBoss && !splitChildren.count(id)) ++in.creaturesMet;
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
    syncCosmetics();  // an item locked again (or unlocked) changes what is worn
    syncBadges();     // and the badges (2.41: their slots come from the stars)
    if (announce) progressCopyDue = true;  // 2.47: after a run (sent at most every 10 minutes)
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

// ------------------------------------------------- local and online (2.47)

namespace {
fs::path localSnapshotPath() { return Paths::userDir() / "online" / "local_snapshot.json"; }
}  // namespace

Json GameServices::progressCopyBody() const {
    int bosses = 0;
    for (const auto& [id, b] : bossStats.all()) bosses += b.kills > 0 ? 1 : 0;
    Json summary = {{"levels", progression.completedCount()},
                    {"achievements", achievements.unlockedCount()},
                    {"stars", achievements.stars(achievementCatalog)},
                    {"play_seconds", static_cast<int64_t>(std::max(0.0, progression.playTimeSeconds))},
                    {"bosses", bosses}};
    Json files = {{"progress", progression.toJson()},
                  {"records", records.toJson()},
                  {"boss_stats", bossStats.toJson()},
                  {"achievements", achievements.toJson()},
                  {"collection", collection.toJson()}};
    return {{"format", "cosmic.progress_copy"}, {"game_version", std::string(COSMIC_VERSION_STRING)}, {"summary", summary}, {"files", files}};
}

bool GameServices::restoreProgressCopy(const Json& files, std::string* error) {
    if (!files.is_object()) {
        if (error) *error = "Copie de la progression illisible.";
        return false;
    }
    // The saves as they are, kept first (a folder per restoration).
    std::string stamp = FileUtil::nowString();
    for (char& c : stamp)
        if (c == ':' || c == ' ' || c == '/') c = '-';
    const fs::path keep = Paths::savesDir() / ("avant_restauration_" + stamp);
    std::error_code ec;
    fs::create_directories(keep, ec);
    const Json current = progressCopyBody()["files"];
    for (const auto& [name, body] : current.items())
        if (!FileUtil::writeTextAtomic(keep / (name + ".json"), body.dump(1))) {
            if (error) *error = "Impossible de garder une copie des sauvegardes actuelles.";
            return false;
        }
    auto part = [&](const char* key) -> const Json& {
        static const Json empty = Json::object();
        auto it = files.find(key);
        return it != files.end() && it->is_object() ? *it : empty;
    };
    progression.fromJson(mergeProgressJson(progression.toJson(), part("progress")));
    records.fromJson(mergeRecordsJson(records.toJson(), part("records")));
    bossStats.fromJson(mergeBossStatsJson(bossStats.toJson(), part("boss_stats")));
    achievements.fromJson(mergeAchievementsJson(achievements.toJson(), part("achievements")));
    collection.fromJson(mergeCollectionJson(collection.toJson(), part("collection")));
    bossStats.touch();
    const bool ok = progression.save() && records.save() && bossStats.save() && achievements.save() && collection.save();
    if (!ok && error) *error = "Une sauvegarde n'a pas pu être écrite (les anciennes sont dans " + Paths::toUtf8(keep) + ").";
    checkAchievements(false);
    progressCopyDue = true;
    return ok;
}

void GameServices::sendProgressCopy(std::function<void(const ApiResult&)> done) {
    if (!online.loggedIn() || !online.usable()) {
        if (done) done(ApiResult{});
        return;
    }
    progressCopyBusy_ = true;
    online.send("PUT", "/api/v1/players/me/progress-copy", progressCopyBody(), [this, done](const ApiResult& r) {
        progressCopyBusy_ = false;
        if (r.ok) {
            progressCopyDue = false;
            progressCopyWait_ = 600.0f;  // at most every 10 minutes
        } else {
            progressCopyWait_ = 120.0f;  // again in 2 minutes
        }
        if (done) done(r);
    }, 60000);
}

void GameServices::updateProgressCopy(float dt) {
    progressCopyWait_ = std::max(0.0f, progressCopyWait_ - dt);
    if (!progressCopyDue || progressCopyBusy_ || progressCopyWait_ > 0.0f) return;
    if (!online.loggedIn() || !online.usable() || online.state() != OnlineState::Online) return;
    sendProgressCopy();
}

void GameServices::noteLoggedOut() {
    // Local mode starts: what is played from now on is shown at the next login.
    Json snap = {{"levels", progression.completedCount()},
                 {"achievements", achievements.unlockedCount()},
                 {"stars", achievements.stars(achievementCatalog)},
                 {"play_seconds", static_cast<int64_t>(std::max(0.0, progression.playTimeSeconds))}};
    FileUtil::writeTextAtomic(localSnapshotPath(), snap.dump(1));
}

void GameServices::noteStartedLocal() {
    std::error_code ec;
    if (!online.loggedIn() && !online.device().boundPlayer.empty() && !fs::exists(localSnapshotPath(), ec)) noteLoggedOut();
}

void GameServices::noteSessionStarted() {
    progressCopyDue = true;
    progressCopyWait_ = std::min(progressCopyWait_, 8.0f);
    auto snap = FileUtil::loadJson(localSnapshotPath());
    std::error_code ec;
    fs::remove(localSnapshotPath(), ec);
    if (!snap || !snap->is_object()) return;
    const int levels = progression.completedCount() - jget<int>(*snap, "levels", 0);
    const int ach = achievements.unlockedCount() - jget<int>(*snap, "achievements", 0);
    const int stars = achievements.stars(achievementCatalog) - jget<int>(*snap, "stars", 0);
    const int64_t secs = static_cast<int64_t>(std::max(0.0, progression.playTimeSeconds)) - jget<int64_t>(*snap, "play_seconds", 0);
    const int runs = static_cast<int>(online.unsentCount());
    if (levels <= 0 && ach <= 0 && runs <= 0 && secs < 60) return;
    reconnectSummary = {{"levels", std::max(0, levels)},
                        {"runs", runs},
                        {"achievements", std::max(0, ach)},
                        {"stars", std::max(0, stars)},
                        {"minutes", static_cast<int>(std::max<int64_t>(0, secs) / 60)}};
}

void GameServices::updatePresence(float dt) {
    if (!online.loggedIn() || !online.usable()) {
        presenceSent_.clear();
        return;
    }
    uint64_t now = SDL_GetTicks(), last = app.input().lastActivityMs();
    const std::string state = last > 0 && now - last < 5u * 60u * 1000u ? "active" : "idle";
    presenceTimer_ -= dt;
    if (state == presenceSent_ && presenceTimer_ > 0.0f) return;
    presenceTimer_ = 60.0f;
    presenceSent_ = state;
    online.send("POST", "/api/v1/presence", {{"state", state}}, [](const ApiResult&) {});
}

void GameServices::presenceOffline() {
    if (!online.loggedIn() || !online.usable() || presenceSent_.empty()) return;
    presenceSent_.clear();
    online.send("POST", "/api/v1/presence", {{"state", "offline"}}, [](const ApiResult&) {});
}

}  // namespace cosmic
