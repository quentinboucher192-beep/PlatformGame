#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Scene.h"
#include "game/ghost/GhostStore.h"
#include "game/levels/LevelData.h"

namespace cosmic {

struct ReplayData;
struct GameplaySettings;

class GameServices;

/// Options for starting a level.
struct PlayOptions {
    /// Play this level data directly (editor test) instead of loading `levelId`.
    std::optional<LevelData> level;
    bool editorTest = false;
    /// Asset root to mount (package levels being edited).
    std::string assetRoot;
    /// Start at this position (feet, world pixels) and skip the intro.
    std::optional<Vec2> spawn;
    /// Launched from ONLINE ("Jouer ce niveau"): leaving the level goes back to ONLINE on
    /// this level, never to the campaign; no "next level".
    bool fromOnline = false;
    /// Automated screenshots: open the pause menu as soon as the level is playing.
    bool openPause = false;
    float pauseAfter = 0.0f;  // screenshots: open the pause menu after this many seconds of play
    /// Automated screenshots: the active boss uses this attack whenever it can.
    std::string forceBossAttack;
    /// Ghost Racing: the ghosts chosen before the start (`ghostsChosen` false: the default
    /// ones, i.e. my best time if the setting asks for it). Kept when the run restarts.
    std::vector<GhostEntry> ghosts;
    bool ghostsChosen = false;
    /// Training (Ghost Racing): starts at this checkpoint (level object id), compares with
    /// the ghosts from there; never a record, never progression, never sent online.
    bool training = false;
    std::string trainingCheckpoint;
    std::string trainingEnd;                 // the checkpoint ending the segment ("" = the exit)
    std::vector<std::string> trainingPowers; // tablets picked up before the start, in route order
};

/// A finished run's segments (results screen → Segments): this run, my best segments, my
/// record's segments, the ghosts raced, the history of my runs of this level version.
struct SegmentSummary {
    std::string levelName;
    std::string levelKey;                 // GhostStore::levelKey (history)
    std::string levelId;                  // the level played (« Comparer des replays »)
    std::vector<std::string> names;       // checkpoint names, in the order reached
    std::vector<float> splits;            // this run (cumulative)
    float time = 0.0f;
    std::vector<float> bestSegments;      // my best ones before this run (-1 unknown)
    std::vector<float> pbSplits;          // my record's splits before this run
    float pbTime = 0.0f;
    struct Ghost {
        std::string name;
        Color color;
        std::vector<float> splits;
        float duration = 0.0f;
        float gap = 0.0f;                 // this run's time - the ghost's
        bool sameVersion = true;
    };
    std::vector<Ghost> ghosts;
};
std::unique_ptr<Scene> makeSegments(GameServices& g, std::shared_ptr<const SegmentSummary> s);

/// What the pre-race screen opens with: an online record to race against (preselected and
/// downloaded at once), and where the run came from.
struct GhostSetupRequest {
    std::string recordId;
    bool fromOnline = false;
};

// Factories of every screen (implemented next to each scene).
std::unique_ptr<Scene> makeSplashScene(GameServices& g);
std::unique_ptr<Scene> makeMainMenu(GameServices& g);
std::unique_ptr<Scene> makePlanetMap(GameServices& g);
std::unique_ptr<Scene> makeLevelSelect(GameServices& g, const std::string& planetId);
std::unique_ptr<Scene> makeGameScene(GameServices& g, const std::string& levelId, PlayOptions opt = {});
std::unique_ptr<Scene> makeSettingsScene(GameServices& g, bool overlay);
/// HUD editor: place and size every HUD element; "Enregistrer" writes the layout into `target`.
std::unique_ptr<Scene> makeHudEditor(GameServices& g, GameplaySettings& target);
/// Notifications of the main menu (new content, records dropped, new installer); `tab` 1:
/// the Cosmic Online notification centre.
std::unique_ptr<Scene> makeNotifications(GameServices& g, int tab = 0);
/// Before a run: the ghosts to race against (mine, downloaded, online) and the training mode.
std::unique_ptr<Scene> makeGhostSetup(GameServices& g, const std::string& levelId, GhostSetupRequest req = {});
class GameScene;
/// Ghost Racing options (opacity, colours, HUD): from the pause menu (`game`: the run, whose
/// ghosts follow the changes at once) or from the pre-race screen (nullptr).
std::unique_ptr<Scene> makeGhostOptions(GameServices& g, GameScene* game);
/// Main menu → Succès: every achievement on a spider web (categories, stars), or as a list.
std::unique_ptr<Scene> makeAchievements(GameServices& g);
/// Main menu → Collection: Vestiaire, Route des étoiles, Musiques (jukebox), Galerie.
std::unique_ptr<Scene> makeCollection(GameServices& g, int tab = 0);
/// End of a level: the rewards just unlocked by the stars (equip now or later).
std::unique_ptr<Scene> makeNewRewards(GameServices& g);
/// Main menu → Statistiques: the bosses (victories, without being hit, per phase).
std::unique_ptr<Scene> makeBossStats(GameServices& g);
/// Help: every power of the player with a live example (pushed over the current screen).
std::unique_ptr<Scene> makePowerGuide(GameServices& g, int page = 0);
std::unique_ptr<Scene> makeLevelEditor(GameServices& g, const std::string& levelId);
/// Live editing of a collaborative project in the level editor (`levelId`: its local copy).
std::unique_ptr<Scene> makeLevelEditorLive(GameServices& g, const std::string& levelId, const std::string& projectId, const std::string& role);
/// Automated screenshots: the level editor with its help open.
std::unique_ptr<Scene> makeLevelEditorHelp(GameServices& g, const std::string& levelId);
std::unique_ptr<Scene> makeEnemyEditor(GameServices& g, const std::string& enemyId = {}, int tab = 0);
/// The WORKSHOP (reached from ONLINE): the player's levels, imported levels, .cplanet
/// export/import, enemy editor.
/// `importFile`: a .cplanet file to analyse immediately (double-click / command line).
/// `tab`: 0 Mes niveaux, 1 Niveaux importés, 2 Exporter, 3 Importer, 4 Éditeur d'ennemis.
std::unique_ptr<Scene> makeContentManager(GameServices& g, const std::string& importFile = {}, int tab = 0);
// Cosmic Online
std::unique_ptr<Scene> makeOnlineRanking(GameServices& g, const std::string& levelId = {});
/// Account overlay: create an account or log in. `onboarding` shows the first-visit
/// explanation (what is shared, location consent). `required`: the ONLINE section needs
/// an account, so leaving without one returns to the main menu (the game itself never
/// needs an account).
std::unique_ptr<Scene> makeOnlineAccount(GameServices& g, bool onboarding, bool required = false);
/// ONLINE LEVELS: the players' published levels (popular, hardest, recent, top rated,
/// friends', search, mine), download and play, ratings, reports, per-level leaderboards.
std::unique_ptr<Scene> makeOnlineLevels(GameServices& g, const std::string& communityId = {});
/// ONLINE → PROJETS: collaborative projects (roles, invitations, revisions); `projectId`
/// opens that project.
std::unique_ptr<Scene> makeProjects(GameServices& g, const std::string& projectId = {});
/// Where the level editor goes when it closes: back to PROJETS for the working copy of a
/// project, to the WORKSHOP otherwise.
std::unique_ptr<Scene> makeEditorExit(GameServices& g, const std::string& levelId);
/// WORKSHOP → Mes niveaux → Publier en ligne.
std::unique_ptr<Scene> makeCommunityPublish(GameServices& g, const std::string& levelId);
/// SOCIAL (in ONLINE): conversations with friends, groups, friends, blocked players.
/// `conversationId`: open this conversation; `tab`: 0 Discussions, 1 Amis, 2 Bloqués.
std::unique_ptr<Scene> makeSocial(GameServices& g, const std::string& conversationId = {}, int tab = 0);
/// Profile of a player (pushed over the current screen): records, creator card and
/// creations, followers, time played, follow / friend / block.
std::unique_ptr<Scene> makePlayerProfile(GameServices& g, const std::string& playerId);
/// `tab`: 0 Préférences, 1 Compte et confidentialité, 2 Amis.
std::unique_ptr<Scene> makeOnlineSettings(GameServices& g, int tab = 0);
/// « Course à la vérification » of an EXTRÊME level (pushed over the current screen):
/// `kind` "official" (campaign level, `levelHash` optional) or "community" (online id,
/// `version` 0 = the current one).
std::unique_ptr<Scene> makeRaceBoard(GameServices& g, const std::string& kind, const std::string& levelId, int version = 0,
                                     const std::string& levelHash = {});
/// « Avis et commentaires » (pushed over ONLINE, 2.19): `thread` = "level:<official level
/// id>" (the players' rating and the level's comments) or "run:<record id>" (the comments of
/// a run of the leaderboards; `levelId` names its level until the record is read).
std::unique_ptr<Scene> makeOnlineThread(GameServices& g, const std::string& thread, const std::string& levelId = {});
/// Downloads and plays back the replay of a verified record.
std::unique_ptr<Scene> makeReplayViewer(GameServices& g, const std::string& recordId);
/// Plays a replay already in memory (the run just finished, a .cprz file).
std::unique_ptr<Scene> makeLocalReplayViewer(GameServices& g, ReplayData replay, const std::string& title);

}  // namespace cosmic
