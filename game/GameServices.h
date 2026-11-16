#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "engine/core/Application.h"
#include "game/achievements/Achievements.h"
#include "game/cosmetics/Collection.h"
#include "game/content/ContentDatabase.h"
#include "game/ghost/GhostStore.h"
#include "game/ghost/ReplayLibrary.h"
#include "game/ghost/SegmentHistory.h"
#include "game/ghost/TrainingSegments.h"
#include "game/levels/RatingService.h"
#include "game/online/CampaignRevisions.h"
#include "game/online/EnemyRevisions.h"
#include "game/online/DailyChallenges.h"
#include "game/online/OnlineService.h"
#include "game/online/ReplayDownloads.h"
#include "game/online/Spectate.h"
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

    // 2.47: an account is private to this computer (docs/online/DEVICES.md). One progression
    // on the computer, connected or not: what is played in local mode (logged out) goes to the
    // account at the next login, with a summary; copies of the progression are sent to the
    // server (after the runs, at most every 10 minutes, at each login, and on demand).
    /// Hooks of OnlineService (main.cpp).
    void noteSessionStarted();
    void noteLoggedOut();
    /// At startup: a computer bound to an account but logged out plays in local mode.
    void noteStartedLocal();
    /// What was played in local mode since the logout, set at the login (levels, runs to send,
    /// achievements, stars, minutes); null when nothing new. Shown once, then cleared.
    Json reconnectSummary;
    /// The copy as the server keeps it: {format, game_version, summary, files}.
    Json progressCopyBody() const;
    /// 2.50 « Migrer vers un nouveau compte »: the files of a copy (`files` of the body above,
    /// sent by the old account's game) added to this computer's saves, the best of each side
    /// kept (game/online/ProgressMerge). The saves are copied first to
    /// saves/avant_restauration_<date>/. False (and `error`) when a save could not be written.
    bool restoreProgressCopy(const Json& files, std::string* error = nullptr);
    /// The server offers the migration (GET /players/me/migration answers): checked at each
    /// login; until then, nothing about it is shown (no button that a server without it fails).
    bool migrationAvailable = false;
    void sendProgressCopy(std::function<void(const ApiResult&)> done = {});
    /// Every frame: sends a copy when one is due (connected, 10 minutes after the last one).
    void updateProgressCopy(float dt);
    bool progressCopyDue = false;
    float progressCopyWait_ = 20.0f;
    bool progressCopyBusy_ = false;
    /// Collection (2.19): cosmetic rewards unlocked by the stars, equipped items, and the
    /// discoveries of the jukebox and the gallery. Display only.
    CosmeticCatalog cosmetics;
    CollectionBook collection;
    CosmeticProgress cosmeticProgress() const;
    /// Look of the equipped item of a category (« givre », « comet »…).
    std::string cosmeticLook(const std::string& category) const {
        return collection.look(cosmetics, category, cosmeticProgress());
    }
    /// The sheet of Nova in the suit `look` (its animation set's frames), nullptr for the normal
    /// one (« standard », empty). A catalogue suit: its PNG (assets/sprites/player/suits). A suit
    /// created on Cosmic Online ("srvsuit:<n>"): Nova's sheet recoloured at run time from
    /// `colors` when given, else from the colours this game knows (the public list kept on
    /// disk, the suits won); unknown yet: the normal sheet, and the list is read again soon.
    /// Display only.
    TexturePtr suitSheet(const std::string& look, const SuitColors* colors = nullptr);
    /// The suits created on Cosmic Online that this game knows (GET /creations, kept in the user
    /// folder, online/creations.json), and their recoloured sheets.
    ServerSuitBook serverSuits;
    SuitTextureCache suitTextures;
    /// The list kept on disk at once; with `fetch`, read from the server now and every 30 minutes.
    void startServerSuits(bool fetch);
    void refreshServerSuits();
    void updateServerSuits(float dt);
    bool serverSuitsFetch_ = false;
    bool serverSuitsBusy_ = false;
    float serverSuitsTimer_ = 0.0f;
    uint64_t serverSuitsAskedMs_ = 0;  // the last reading asked by an unknown suit
    /// What the player wears goes to the online profile (2.23): at start, after each
    /// « Équiper » and when the achievements change what is unlocked.
    void syncCosmetics();
    /// Profile badges (2.41): the achievements chosen in the Collection that may be shown now
    /// (unlocked, as many as the stars open), in their order. Sent to the online profile when
    /// the player is connected (at start, after each choice, when the achievements change).
    std::vector<std::string> shownBadges() const;
    void syncBadges();
    /// After an « Équiper » (and at start): the achievements of the Collection count the
    /// rewards worn (2.24). Checks the achievements (`announce` false: silently, at start).
    void noteOutfit(bool announce = true);
    /// 2.34: Cosmic Online's verified challenge results changed (ChallengeService): what is worn
    /// is sent again and, `announce`, a reward of the challenges just unlocked gets its toast
    /// (false: at start, the results kept on this computer).
    void challengeRewardsChanged(bool announce = true);
    std::set<std::string> challengeRewardsUnlocked_;  // the rewards of the challenges unlocked at the last reading
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
    /// Presence for friends (2.32.3): « active » or « idle » (no input for 5 min), sent every
    /// minute and on each change while logged in; « offline » when the game closes.
    void updatePresence(float dt);
    void presenceOffline();
    float presenceTimer_ = 0.0f;
    std::string presenceSent_;
    /// Cosmic Online (rankings, globe, account). Fully optional: offline play never needs it.
    OnlineService online;
    /// « Replays téléchargés » (2.45): online replays kept on this computer to be watched
    /// without a connection (user folder, replays/downloaded/), and their downloads.
    ReplayLibrary replayLibrary;
    ReplayDownloads replayDownloads{online, replayLibrary};
    /// « Défis du jour » (2.32): the challenges (cached, playable offline until their end),
    /// local results and streak, runs waiting to be sent.
    ChallengeService challenges;
    /// Live spectating (2.36): the run being played, streamed for the friends who may watch
    /// (« Mes amis peuvent me regarder jouer »). Nothing of it is shown to the player.
    SpectateBroadcaster spectate{online};
    UpdateInstaller updates;  // the installer of a newer version (main menu alert, Notifications)
    /// 2.45: the campaign revisions published online by an administrator (ADMINISTRATION ›
    /// Campagne). Those kept on this computer are played at once, offline too; with `fetch`,
    /// the manifest is read (public, nothing personal sent) and the newer ones downloaded.
    CampaignRevisionStore campaignRevisions;
    void startCampaignRevisions(bool fetch);
    /// Reads GET /campaign/revisions, downloads and checks what is missing, then plays the
    /// current revisions. `done(error)`: once everything is installed ("" when all went well).
    void refreshCampaignRevisions(std::function<void(const std::string&)> done = {});
    /// The levels follow the current revisions kept (content, fingerprints, ratings).
    void applyCampaignRevisions();
    /// Every frame: the manifest is read again every 30 minutes (2 after a failure).
    void updateCampaignRevisions(float dt);
    /// 2.45: the revisions of the native enemies published online (ADMINISTRATION › Ennemis),
    /// read before the campaign revisions (a level revision names the enemy revisions it plays):
    /// GET /enemies/revisions, every revision missing downloaded and checked. Offline, those
    /// kept on this computer are given to the content at start.
    EnemyRevisionStore enemyRevisions;
    void fetchCampaignManifest(std::function<void(const std::string&)> done, std::string firstError);
    bool campaignRefreshing_ = false;
    bool campaignFetch_ = false;
    bool campaignApplyPending_ = false;  // waits for the news scan (it reads the level files)
    float campaignTimer_ = 0.0f;
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
