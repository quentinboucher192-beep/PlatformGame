#pragma once

#include <array>
#include <functional>
#include <future>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "engine/core/Application.h"
#include "game/achievements/Achievements.h"
#include "game/cosmetics/Collection.h"
#include "game/cosmetics/CreationImages.h"
#include "game/content/BossStories.h"
#include "game/content/ContentDatabase.h"
#include "game/ghost/GhostStore.h"
#include "game/ghost/ReplayLibrary.h"
#include "game/ghost/SegmentHistory.h"
#include "game/ghost/TrainingSegments.h"
#include "game/levels/RatingService.h"
#include "game/online/AccountMigration.h"
#include "game/online/CampaignRevisions.h"
#include "game/records/KeptRecords.h"
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
#include "game/records/PlayStats.h"
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
    /// Boss statistics (victories, phases and fights without being hit), and the giga bosses
    /// whose story has been told.
    BossStats bossStats;
    /// 2.54: the player's statistics (main menu → Statistiques; saves/stats.json). 2.55: in the copy of
    /// the progression and restored with it (merged without counting anything twice).
    PlayStatsStore playStats;
    /// « Histoire des giga-boss » (2.51): the stories told before the first fight against each
    /// giga boss (data/stories).
    BossStories stories;
    /// 2.63: the game's trailer (data/trailer/trailer.json), played by the main menu's attract mode.
    std::optional<BossStory> trailer;
    /// Ghost Racing: the replays kept on this computer (my runs, downloads, chosen files)
    /// and the history of my segments.
    GhostStore ghosts;
    SegmentHistory segments;
    /// Section training (2.30.2): the best training time of each segment and the powers last
    /// chosen for it (saves/training.json). Never a record nor a gold segment.
    TrainingRecords training;
    /// The keys the player chose for the powers of a level (2.31, saves/power_keys.json).
    PowerKeyOverrides powerKeyOverrides;
    /// 2.65: the power keys of players 2 and 3 on one computer (saves/power_keys_j2.json, _j3.json).
    std::array<PowerKeyOverrides, 2> coopPowerKeys;
    /// The power keys of local player `player` (0: player 1, `powerKeyOverrides`), and saving them.
    PowerKeyOverrides& powerKeysOf(int player) { return player <= 0 ? powerKeyOverrides : coopPowerKeys[static_cast<size_t>(std::min(player, 2) - 1)]; }
    void savePowerKeys(int player);
    /// 2.65: the players of one computer's cooperation (the lobby « Sur ce PC » and its game):
    /// the keys screens (Paramètres, « Touches de ce niveau ») show one tab each, with the keys of
    /// that player's device. Empty (or one player): the solo game, no tab.
    struct LocalPlayer {
        std::string name;
        Color color;
        LocalDevice device;
    };
    std::vector<LocalPlayer> localTeam;
    /// The glyphs of a local player's device: its pad's (connected or remembered), else those of
    /// the gamepad in use.
    GamepadStyle playerPadStyle(int player) const;
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
    /// The copy as the server keeps it: {format, game_version, summary, files}. `withStats`
    /// (2.55): with the statistics (stats.json), the largest part.
    Json progressCopyBody(bool withStats = true) const;
    /// 2.50 « Migrer vers un nouveau compte »: the files of a copy (`files` of the body above,
    /// sent by the old account's game) added to this computer's saves, the best of each side
    /// kept (game/online/ProgressMerge). The save files are copied first, as they are, to
    /// saves/avant_restauration_<date>/ (its name in `keptFolder`; nothing changes if that
    /// fails). `sha256` names the copy: a copy already restored here (saves/restored_copies.json,
    /// or merged earlier in this session) is not added again (AlreadyRestored). Failed (and
    /// `error`) when the saves could not be kept (`keptFolder` empty: nothing changed) or a save
    /// could not be written (merged in memory, written by the next save).
    enum class RestoreOutcome { Restored, AlreadyRestored, Failed };
    RestoreOutcome restoreProgressCopy(const Json& files, const std::string& sha256, std::string* error = nullptr,
                                       std::string* keptFolder = nullptr);
    /// Whether the server offers the migration (GET /players/me/migration answers, asked once per
    /// account and server: until then nothing about it is shown, no button that a server without
    /// it would fail) and the progression waiting to be restored here. Updated every frame.
    MigrationState migration;
    /// Sends a copy of the progression: its statistics are written out on a worker thread (the
    /// largest part), then the copy leaves from updateProgressCopy.
    void sendProgressCopy(std::function<void(const ApiResult&)> done = {});
    /// Every frame: sends a copy when one is due (connected, 10 minutes after the last one), and
    /// asks the server whether it offers the migration (MigrationState).
    void updateProgressCopy(float dt);
    void sendPreparedCopies();
    bool progressCopyDue = false;
    float progressCopyWait_ = 20.0f;
    bool progressCopyBusy_ = false;
    struct PreparedCopy {
        std::future<std::string> text;  // the body, written out off the main thread
        std::function<void(const ApiResult&)> done;
    };
    std::vector<PreparedCopy> preparedCopies_;
    std::set<std::string> copiesMergedThisSession_;  // 2.50: by SHA-256 (restoreProgressCopy)
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
    /// 2.59: the trails, frames and victory effects created there (the same list, kept in
    /// online/creation_looks.json).
    ServerLookBook serverLooks;
    /// The look of a trail, a frame or a victory effect created on Cosmic Online
    /// ("srvtrail:<n>"...): from the list kept on this computer, else from the rewards won;
    /// unknown yet: nullopt (the category's simple look is drawn meanwhile) and the list is read
    /// again soon. What the drawing code asks (setCreationLookResolver, set by startServerSuits).
    std::optional<CreationLook> creationLook(const std::string& ref);
    /// 2.61 Studio de Créations: the images of the creations (a suit's whole sheet, a trail's or
    /// a victory effect's particle, a frame's ring): the list (GET /creations/images) and their
    /// files kept on this computer (online/creation_images), read off the main thread; drawn
    /// instead of the parameters once ready (suitSheet(), CosmeticFx through
    /// resolveCreationImage()). Missing: the category's simple look.
    CreationImageBook creationImages;
    std::optional<CreationImageDraw> creationImage(const std::string& ref);
    /// The lists kept on disk at once; with `fetch`, read from the server now and every 30 minutes.
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
    /// Asks the server for a newer installer (no account needed, nothing personal sent); a
    /// version seen for the first time this session gets its toast at once.
    void checkForUpdate();
    /// Every frame: the check again when Cosmic Online becomes usable or comes back, after
    /// a login, then every 10 minutes (ReleaseWatch), so that a new version is announced live.
    void updateReleaseCheck(float dt);
    ReleaseWatch releaseWatch_;
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
    /// 2.50: earlier versions of campaign levels whose records stay valid (data/levels/kept_records.json).
    KeptRecords keptRecords;
    void startCampaignRevisions(bool fetch);
    /// Reads GET /campaign/revisions, downloads and checks what is missing, then plays the
    /// current revisions. `done(error)`: once everything is installed ("" when all went well).
    void refreshCampaignRevisions(std::function<void(const std::string&)> done = {});
    /// The levels follow the current revisions kept (content, fingerprints, ratings).
    void applyCampaignRevisions();
    /// 2.49.1: the best times set on a revision of a campaign level, checked against the
    /// revision played now: kept on a revision published with « Garder les records » (no major
    /// change: its rankings are the same), dropped otherwise (announced in the news).
    void checkRevisionRecords();
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
