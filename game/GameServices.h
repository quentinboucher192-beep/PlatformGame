#pragma once

#include <map>
#include <string>
#include <vector>

#include "engine/core/Application.h"
#include "game/achievements/Achievements.h"
#include "game/cosmetics/Collection.h"
#include "game/content/ContentDatabase.h"
#include "game/ghost/GhostStore.h"
#include "game/ghost/SegmentHistory.h"
#include "game/ghost/TrainingSegments.h"
#include "game/levels/RatingService.h"
#include "game/online/DailyChallenges.h"
#include "game/online/OnlineService.h"
#include "game/online/UpdateInstaller.h"
#include "game/progression/NewBadges.h"
#include "game/progression/NewsCenter.h"
#include "game/powers/PowerKeys.h"
#include "game/progression/Progression.h"
#include "game/records/BossStats.h"
#include "game/records/Records.h"
#include "saves/Settings.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

/// Long-lived game state shared by every scene: engine services, content registry,
/// settings, progression and records.
class GameServices {
public:
    explicit GameServices(Application& app) : app(app) {}

    void loadAll();
    void applySettings();
    void saveSettings() { SettingsStore::save(settings); }
    void uiSound(const std::string& name, float volume = 0.6f) {
        app.resources().playSound("sounds/" + name + ".wav", volume);
    }
    /// Plays a music by asset path with a cross-fade.
    void playMusic(const std::string& rel);

    Application& app;
    ContentDatabase content;
    /// Difficulty ratings (campaign precomputed, other levels on a worker thread).
    RatingService ratings;
    Settings settings;
    Progression progression;
    RecordsManager records;
    /// Boss statistics (victories, phases and fights without being hit).
    BossStats bossStats;
    /// Ghost Racing: the replays kept on this computer (my runs, downloads, chosen files)
    /// and the history of my segments.
    GhostStore ghosts;
    SegmentHistory segments;
    /// Section training (2.30.2): the best training time of each segment and the powers last
    /// chosen for it (saves/training.json). Never a record nor a gold segment.
    TrainingRecords training;
    /// The keys the player chose for the powers of a level (2.31, saves/power_keys.json).
    PowerKeyOverrides powerKeyOverrides;
    /// Achievements: the catalogue (data) and what this player has unlocked (saves).
    /// Training attempts of this session (per level and starting checkpoint), never saved.
    std::map<std::string, std::vector<float>> trainingAttempts;
    AchievementCatalog achievementCatalog;
    AchievementBook achievements;
    /// What the achievements' conditions read now.
    AchievementInputs achievementInputs() const;
    /// Unlocks what is done, saves, and announces the new ones (`announce` false: silently,
    /// e.g. the first check of saves made before achievements existed... still listed).
    void checkAchievements(bool announce = true);
    /// Collection (2.19): cosmetic rewards unlocked by the stars, equipped items, and the
    /// discoveries of the jukebox and the gallery. Display only.
    CosmeticCatalog cosmetics;
    CollectionBook collection;
    CosmeticProgress cosmeticProgress() const;
    /// Look of the equipped item of a category (« givre », « comet »…).
    std::string cosmeticLook(const std::string& category) const {
        return collection.look(cosmetics, category, cosmeticProgress());
    }
    /// What the player wears goes to the online profile (2.23): at start, after each
    /// « Équiper » and when the achievements change what is unlocked.
    void syncCosmetics();
    /// After an « Équiper » (and at start): the achievements of the Collection count the
    /// rewards worn (2.24). Checks the achievements (`announce` false: silently, at start).
    void noteOutfit(bool announce = true);
    /// Discoveries proven by the saves (tracks and creatures of the completed levels).
    void discoverFromSaves();
    /// Music of the main menu (the jukebox's choice, else the theme).
    std::string menuMusic() const;
    /// Notifications of the main menu (new content, records dropped, new installer).
    NewsCenter news;
    /// « NOUVEAU » pills on the novelties of the updates (data/news/new_features.json).
    NewBadges newBadges;
    /// Asks the server once for a newer installer (no account needed, nothing personal sent).
    void checkForUpdate();
    /// Cosmic Online (rankings, globe, account). Fully optional: offline play never needs it.
    OnlineService online;
    /// « Défis du jour » (2.32): the challenges (cached, playable offline until their end),
    /// local results and streak, runs waiting to be sent.
    ChallengeService challenges;
    UpdateInstaller updates;  // the installer of a newer version (main menu alert, Notifications)
    UIContext ui;
    /// Scripted input for automated runs (see InputScript), applied to the next level started.
    std::string inputScript;
    /// --save-replay: file where the replay of a completed run is written (tests, sharing).
    std::string saveReplayPath;
    /// Automated checks: pseudonym whose globe marker the ranking screen selects.
    std::string onlineFocus;
    /// Automated checks: overlay the ranking screen opens ("register", "login", "settings",
    /// "profile:<pseudonym>", "replay:<pseudonym>").
    std::string onlineOpen;
};

}  // namespace cosmic
