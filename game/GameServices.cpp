#include "game/GameServices.h"

#include "game/online/ProgressMerge.h"

#include <chrono>
#include <future>
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
    playStats.open(Paths::savesDir() / "stats.json");
    playStats.currentStars = [this] { return achievements.stars(achievementCatalog); };
    if (std::string err; !stories.loadDefault(&err)) Log::warn("Histoires des giga-boss : {}", err);
    if (std::string err; !(trailer = story::loadTrailer(&err))) Log::warn("Bande-annonce : {}", err);
    ghosts.open(Paths::userDir() / "ghosts");
    ghosts.trimCache(static_cast<uint64_t>(settings.ghosts.cacheMB) * 1024 * 1024);
    replayLibrary.open(Paths::userDir() / "replays" / "downloaded");
    segments.load();
    training.load();
    powerKeyOverrides.load();
    for (size_t i = 0; i < coopPowerKeys.size(); ++i) coopPowerKeys[i].loadFile(Paths::savesDir() / ("power_keys_j" + std::to_string(i + 2) + ".json"));
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
    // 2.69: the new assets of ADMINISTRATION › Assets (tools/assets/asset_versions.py).
    newBadges.loadAssetVersions(Paths::dataDir() / "assets" / "asset_versions.json", "admin.tab.assets");
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
    // 2.61: its own sheet, drawn in the Studio de Créations, once downloaded and read.
    if (TexturePtr t = creationImages.texture(look, app.renderer())) return t;
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

std::optional<CreationLook> GameServices::creationLook(const std::string& ref) {
    std::string kind;
    if (!isServerLookRef(ref, &kind)) return std::nullopt;
    if (const ServerLook* s = serverLooks.find(ref)) return s->look;
    if (const CosmeticItem* w = challenges.wonRewards().serverLook(ref); w && w->creationLook) return w->creationLook;
    // Not known yet (created since the last reading): the simple look meanwhile, and the list
    // read again (at most every two minutes, as for the suits).
    const uint64_t now = SDL_GetTicks();
    if (serverSuitsFetch_ && online.usable() && (serverSuitsAskedMs_ == 0 || now - serverSuitsAskedMs_ > 120000)) {
        serverSuitsAskedMs_ = now;
        refreshServerSuits();
    }
    return std::nullopt;
}

void GameServices::startServerSuits(bool fetch) {
    serverSuits.setFile(Paths::userDir() / "online" / "creations.json");
    serverSuits.load();  // drawn offline after one reading
    // 2.59: the trails, frames and victory effects, kept beside them (a game before 2.59 keeps
    // the suits' file as it knows it).
    serverLooks.setFile(Paths::userDir() / "online" / "creation_looks.json");
    serverLooks.load();
    setCreationLookResolver([this](const std::string& ref) { return creationLook(ref); });
    // 2.61: their images (kept beside them; downloaded with the session, checked by their hash).
    creationImages.setDir(Paths::userDir() / "online" / "creation_images");
    creationImages.load();
    creationImages.setFetch([this](const std::string& path, std::function<void(bool, std::vector<uint8_t>)> done) {
        online.getBinary(
            path,
            [done](const HttpResponse& r) {
                const bool ok = !r.networkFailure() && r.status == 200;
                done(ok, ok ? std::vector<uint8_t>(r.body.begin(), r.body.end()) : std::vector<uint8_t>());
            },
            kCreationImageMaxBytes + 4096);
    });
    setCreationImageResolver([this](const std::string& ref) { return creationImage(ref); });
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
        const Json list = jfield(r.data, "creations", Json());
        if (serverSuits.applyList(list)) serverSuits.save();
        if (serverLooks.applyList(list)) serverLooks.save();
    });
    // 2.61: the images those creations have (with the session: the ones won are listed too).
    // A server before 2.61 answers 404: nothing changes.
    online.get(
        "/api/v1/creations/images",
        [this](const ApiResult& r) {
            if (!r.ok || r.fromCache) return;
            if (creationImages.applyList(jfield(r.data, "images", Json()))) creationImages.save();
        },
        false);
}

std::optional<CreationImageDraw> GameServices::creationImage(const std::string& ref) {
    const CreationImageInfo* info = creationImages.find(ref);
    if (!info) return std::nullopt;
    TexturePtr t = creationImages.texture(ref, app.renderer());
    if (!t) return std::nullopt;
    return CreationImageDraw{t, *info};
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
    keptRecords.loadDefault();
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
    checkRevisionRecords();
}

void GameServices::checkRevisionRecords() {
    bool changed = false;
    for (const LevelEntry* e : content.levels()) {
        if (e->source != ContentSource::Builtin) continue;
        const LevelRecord* r = records.get(e->id);
        // A best time from before 2.49.1 has no revision: the news compare it with the bundled file.
        if (!r || r->bestTime <= 0.0f || r->rankedHash.empty()) continue;
        const LevelFingerprint* fp = online.fingerprints().get(content, e->id);
        if (!fp || fp->hash == r->rankedHash) continue;
        // Kept: published online with « Garder les records », or shipped with the game as an
        // earlier version without a major change (data/levels/kept_records.json, offline too).
        const bool kept = campaignRevisions.keptHashes(e->id).count(r->rankedHash) > 0 || keptRecords.ranked(e->id).count(r->rankedHash) > 0;
        std::optional<LevelRecord> old;
        if (kept) records.carryBest(e->id, fp->hash);
        else old = records.invalidateBest(e->id);
        std::string name = e->name;
        for (const PlanetData* p : content.planets())
            if (p->id == e->planet) name += " (" + p->name + ")";
        news.noteRevision(e->id, name, fp->hash, kept, old);
        Log::info("Campagne en ligne : record de {} {}", e->id, kept ? "gardé (révision sans changement majeur)" : "remis à zéro (nouvelle révision)");
        changed = true;
    }
    if (changed) {
        records.save();
        news.save();
    }
}

CosmeticProgress GameServices::cosmeticProgress() const {
    CosmeticProgress p;
    p.stars = achievements.stars(achievementCatalog);
    p.totalStars = achievementCatalog.totalStars();
    p.book = &achievements;
    p.challenges = &challenges.rewardProgress();
    p.won = &challenges.wonRewards();
    p.tutorialFinished = progression.tutorialFinished();
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
        for (const auto* list : {&p.won->titles, &p.won->suits, &p.won->looks})
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
    in.raidRewards = collection.raidRewards;  // 2.68
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
    // 2.62: `since` = this game's version: the notes of every installer published after it
    // (a player several versions behind sees all that changed, not only the newest).
    online.get(std::string("/api/v1/client/latest?platform=windows&since=") + COSMIC_VERSION_STRING, [this](const ApiResult& r) {
        if (!r.ok || !r.data.is_object() || !r.data.contains("release") || !r.data["release"].is_object()) return;
        const Json& rel = r.data["release"];
        std::string v = jstr(rel, "version");
        if (v.empty() || !versionNewer(v, COSMIC_VERSION_STRING)) return;
        // 2.76.2: the notes in English too (notes_en, written from the dictionary of their version):
        // shown when the game is in English, only when every release has them.
        std::string notes, notesEn;
        bool english = true;
        if (r.data.contains("changes") && r.data["changes"].is_array()) {
            for (const Json& c : r.data["changes"]) {
                const std::string cv = jstr(c, "version"), cn = jstr(c, "notes"), ce = jstr(c, "notes_en");
                if (cv.empty() || cn.empty() || !versionNewer(cv, COSMIC_VERSION_STRING)) continue;
                if (!notes.empty()) notes += "\n\n", notesEn += "\n\n";
                // Notes written from the changelog already start with « Version x.y.z : ».
                notes += cn.rfind("Version ", 0) == 0 ? cn : "Version " + cv + " :\n" + cn;
                notesEn += ce.rfind("Version ", 0) == 0 ? ce : "Version " + cv + ":\n" + ce;
                english = english && !ce.empty();
            }
        }
        if (notes.empty()) {  // an older server: the newest installer's notes
            notes = jstr(rel, "notes");
            notesEn = jstr(rel, "notes_en");
        }
        news.offerRelease(v, jget<int64_t>(rel, "size", 0), jstr(rel, "sha256"), jstr(rel, "download"), notes, english ? notesEn : std::string{});
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
    playStats.enabled = settings.gameplay.recordStats;
}

void GameServices::playMusic(const std::string& rel) {
    std::string p = app.resources().soundPath(rel);
    if (!p.empty()) app.audio().playMusic(p, 1200);
}

// ------------------------------------------------- local and online (2.47)

namespace {
fs::path localSnapshotPath() { return Paths::userDir() / "online" / "local_snapshot.json"; }
}  // namespace

Json GameServices::progressCopyBody(bool withStats) const {
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
    if (withStats) files["stats"] = playStats.data().toJson();  // 2.55: the « Statistiques » screen
    return {{"format", "cosmic.progress_copy"}, {"game_version", std::string(COSMIC_VERSION_STRING)}, {"summary", summary}, {"files", files}};
}

GameServices::RestoreOutcome GameServices::restoreProgressCopy(const Json& files, const std::string& sha256, std::string* error,
                                                               std::string* keptFolder) {
    if (keptFolder) keptFolder->clear();
    if (!files.is_object()) {
        if (error) *error = "Copie de la progression illisible.";
        return RestoreOutcome::Failed;
    }
    const fs::path registry = Paths::savesDir() / "restored_copies.json";
    if (copiesMergedThisSession_.count(sha256) || progressCopyRestored(registry, sha256)) return RestoreOutcome::AlreadyRestored;
    // The save files as they are on disk, kept first (a folder per restoration): nothing is
    // changed if they cannot be kept.
    fs::path kept;
    playStats.flush();  // stats.json as it is in memory (written on its worker thread)
    if (!backupSaveFiles(Paths::savesDir(), FileUtil::nowString(), &kept, error)) return RestoreOutcome::Failed;
    if (keptFolder) *keptFolder = Paths::toUtf8(kept.filename());
    const Json merged = mergeProgressCopyFiles(progressCopyBody()["files"], files, playStats.today());
    progression.fromJson(merged["progress"]);
    records.fromJson(merged["records"]);
    bossStats.fromJson(merged["boss_stats"]);
    achievements.fromJson(merged["achievements"]);
    collection.fromJson(merged["collection"]);
    bossStats.touch();
    // 2.55: the statistics (written on their worker thread, like after a run).
    if (files.contains("stats")) playStats.replace(PlayStatsData::fromJson(merged["stats"]));
    if (!sha256.empty()) copiesMergedThisSession_.insert(sha256);  // in memory now: never added twice
    // Every save is written, even after one failed (the next save writes what is in memory).
    std::string failed;
    auto save = [&](bool ok, const char* name) {
        if (!ok) failed += (failed.empty() ? "" : ", ") + std::string(name) + ".json";
    };
    save(progression.save(), "progress");
    save(records.save(), "records");
    save(bossStats.save(), "boss_stats");
    save(achievements.save(), "achievements");
    save(collection.save(), "collection");
    checkAchievements(false);
    progressCopyDue = true;
    if (!failed.empty()) {
        if (error)
            *error = "Progression ajoutée, mais une sauvegarde n'a pas pu être écrite (" + failed + ") : les précédentes sont dans saves › " +
                     Paths::toUtf8(kept.filename()) + ".";
        return RestoreOutcome::Failed;
    }
    // On disk: a later « Restaurer » of the same copy only tells the server.
    noteProgressCopyRestored(registry, sha256);
    return RestoreOutcome::Restored;
}

void GameServices::sendProgressCopy(std::function<void(const ApiResult&)> done) {
    if (!online.loggedIn() || !online.usable()) {
        if (done) done(ApiResult{});
        return;
    }
    progressCopyBusy_ = true;
    // 2.55: the statistics (up to 300 KB) are written out with the body on a worker thread, from
    // a copy of them; the copy leaves once ready (updateProgressCopy).
    Json body = progressCopyBody(false);
    PlayStatsData stats = playStats.data();
    preparedCopies_.push_back({std::async(std::launch::async,
                                          [body = std::move(body), stats = std::move(stats)]() mutable {
                                              body["files"]["stats"] = stats.toJson();
                                              return body.dump(-1, ' ', false, Json::error_handler_t::replace);
                                          }),
                               std::move(done)});
}

void GameServices::sendPreparedCopies() {
    for (auto it = preparedCopies_.begin(); it != preparedCopies_.end();) {
        if (it->text.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        std::string text = it->text.get();
        auto done = std::move(it->done);
        it = preparedCopies_.erase(it);
        if (!online.loggedIn() || !online.usable()) {
            progressCopyBusy_ = !preparedCopies_.empty();
            if (done) done(ApiResult{});
            continue;
        }
        online.sendJsonText("PUT", "/api/v1/players/me/progress-copy", std::move(text), [this, done](const ApiResult& r) {
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
}

void GameServices::updateProgressCopy(float dt) {
    sendPreparedCopies();
    migration.update(online, dt);  // 2.50: does the server offer « Migrer vers un nouveau compte » ?
    online.setAchievementsMerge(migration.pendingRestore().is_object());
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

void GameServices::savePowerKeys(int player) {
    if (player <= 0) powerKeyOverrides.save();
    else coopPowerKeys[static_cast<size_t>(std::min(player, 2) - 1)].saveFile(Paths::savesDir() / ("power_keys_j" + std::to_string(std::min(player, 2) + 1) + ".json"));
}

GamepadStyle GameServices::playerPadStyle(int player) const {
    if (player >= 0 && player < static_cast<int>(localTeam.size()) && localTeam[static_cast<size_t>(player)].device.isPad())
        if (const PadEntry* p = app.input().devices().byNumber(localTeam[static_cast<size_t>(player)].device.pad)) return p->style;
    return app.input().gamepadStyle();
}

}  // namespace cosmic
