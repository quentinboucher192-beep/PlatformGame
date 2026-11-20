#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Scene.h"
#include "game/gameplay/BossTraining.h"
#include "game/ghost/GhostStore.h"
#include "game/ghost/SobMontage.h"
#include "game/levels/LevelData.h"
#include "game/online/DailyChallenges.h"

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
    /// Automated screenshots (2.33): the active boss jumps to this phase (1 = the first) as
    /// soon as its fight starts; one more than its phases = its last stand.
    int forceBossPhase = 0;
    /// Ghost Racing: the ghosts chosen before the start (`ghostsChosen` false: the default
    /// ones, i.e. my best time if the setting asks for it). Kept when the run restarts.
    std::vector<GhostEntry> ghosts;
    bool ghostsChosen = false;
    /// Training (Ghost Racing): starts at this checkpoint (level object id), compares with
    /// the ghosts from there; never a record, never progression, never sent online.
    bool training = false;
    /// 2.48: a hardcore (EXTRÊME) level as a practice run: Nova comes back where she fell,
    /// invulnerable for a moment; never a record, never ranked (GameWorld::setPracticeRespawn).
    bool practiceRespawn = false;
    std::string trainingCheckpoint;
    std::string trainingEnd;                 // the checkpoint ending the segment ("" = the exit)
    std::vector<std::string> trainingPowers; // tablets picked up before the start, in route order
    /// The powers chosen in the training setup (2.30.2): exactly `trainingPowers` are carried
    /// (the level's start powers only if chosen too). False: the start powers plus the tablets.
    bool trainingPowersChosen = false;
    /// What the training setup showed for this segment (display only, -1 unknown): its best
    /// duration in full runs and its duration in the personal best run.
    float trainingBest = -1.0f;
    float trainingPbSegment = -1.0f;
    /// Boss training (boss statistics → Entraînement): set up with the world, never recorded.
    std::optional<BossTrainingSetup> bossTraining;
    /// A daily challenge (2.32): its rules apply to the world (GameWorld::setChallenge). A
    /// run of the day's challenge is kept as a local result and sent; `practice` (« S'entraîner »,
    /// « Rejouer » an archive) keeps nothing: hors classement. Never a level record either way.
    struct Challenge {
        DailyChallenge challenge;
        bool practice = false;
    };
    std::optional<Challenge> challenge;
    /// 2.64: the tutorial of the first launch (`level` holds it, game/tutorial/Tutorial.h): a
    /// training run (nothing of it counts), Bip by Nova, its explanation cards; its end card
    /// leads to the map of Sylvanox. Left from its pause menu, it is skipped for good.
    bool tutorial = false;
    /// 2.81: ADMINISTRATION › Films › « Rejouer le didacticiel »: the tutorial again from its start, for an
    /// administrator, that touches nothing: its state in the progression stays as it was (never « started »,
    /// « skipped » nor « finished »), no souvenir, no title worn, nothing counted (a training run, as always); left or
    /// finished, it goes back to ADMINISTRATION › Films.
    bool tutorialReplay = false;
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
    /// 2.60.3: the Sum of Best before this run, full runs and section trainings together (-1:
    /// unknown, the sum of `bestSegments` is shown).
    float sob = -1.0f;
    std::vector<float> pbSplits;          // my record's splits before this run
    float pbTime = 0.0f;
    struct Ghost {
        std::string name;
        std::string recordId;             // an online record (2.60.3: the world record's own column)
        Color color;
        std::vector<float> splits;
        float duration = 0.0f;
        float gap = 0.0f;                 // this run's time - the ghost's
        bool sameVersion = true;
    };
    std::vector<Ghost> ghosts;
    /// 2.60.3, online only: the level version (GhostStore::levelKey's hash), its leaderboard
    /// (GET path, empty: none) and where its records' verified replays are (« <base><id>/replay »).
    std::string levelHash;
    std::string wrBoard;
    std::string wrReplayBase;
};
std::unique_ptr<Scene> makeSegments(GameServices& g, std::shared_ptr<const SegmentSummary> s);

/// What the pre-race screen opens with: an online record to race against (preselected and
/// downloaded at once), and where the run came from.
struct GhostSetupRequest {
    std::string recordId;
    bool fromOnline = false;
    /// Back from a section training (2.31): the training on, at the same segment (its start
    /// checkpoint, "" = the level's start).
    bool training = false;
    std::string trainingCheckpoint;
};

// Factories of every screen (implemented next to each scene).
std::unique_ptr<Scene> makeSplashScene(GameServices& g);
/// 2.64: the tutorial of the first launch: its welcome (« Commencer » / « Passer », confirmed),
/// then the tutorial itself (nullptr from makeTutorialRun when its level cannot be read).
std::unique_ptr<Scene> makeTutorialWelcome(GameServices& g);
std::unique_ptr<Scene> makeTutorialRun(GameServices& g);
/// 2.81: the tutorial replayed by an administrator (PlayOptions::tutorialReplay): nothing of it is kept.
std::unique_ptr<Scene> makeTutorialReplay(GameServices& g);
std::unique_ptr<Scene> makeMainMenu(GameServices& g);
/// ADMINISTRATION (2.40, accounts with the admin role only). `open`: "log" for the Journal,
/// "players:<search>[:confirm]", "calendar[:<YYYY-MM>[:<challenge id>[:objectif]]]" (2.41,
/// « Calendrier des défis »), "campaign[:<level id>]" (2.45, « Campagne ») (automated screenshots);
/// 2.63: "assets[:<asset path>]" (« Assets », that asset selected: the way back from a sheet).
std::unique_ptr<Scene> makeAdmin(GameServices& g, const std::string& open = {});
std::unique_ptr<Scene> makePlanetMap(GameServices& g);
/// `levelId`: the level selected on arrival (else the last one played). `fromBoss`: opened
/// from the statistics of this boss (« Retour » goes back there instead of the planet map).
/// `fromAsset` (2.63): opened from ADMINISTRATION › Assets on this asset (« Retour » goes back there).
std::unique_ptr<Scene> makeLevelSelect(GameServices& g, const std::string& planetId, const std::string& levelId = {},
                                       const std::string& fromBoss = {}, const std::string& fromAsset = {});
std::unique_ptr<Scene> makeGameScene(GameServices& g, const std::string& levelId, PlayOptions opt = {});
/// `tab` (2.65): the tab opened first (-1: the first one); `player`: on one PC with a team
/// (GameServices::localTeam), whose controls the keyboard and gamepad tabs show first.
std::unique_ptr<Scene> makeSettingsScene(GameServices& g, bool overlay, int tab = -1, int player = 0);
/// HUD editor: place and size every HUD element; "Enregistrer" writes the layout into `target`.
std::unique_ptr<Scene> makeHudEditor(GameServices& g, GameplaySettings& target);
/// Notifications of the main menu (new content, records dropped, new installer); `tab` 1:
/// the Cosmic Online notification centre.
std::unique_ptr<Scene> makeNotifications(GameServices& g, int tab = 0);
/// Before a run: the ghosts to race against (mine, downloaded, online) and the training mode.
std::unique_ptr<Scene> makeGhostSetup(GameServices& g, const std::string& levelId, GhostSetupRequest req = {});
/// 2.60.3, section training › « Regarder »: the Sum of Best of a level version watched, its best
/// segments one after the other (full runs and section trainings), each from its exact replay.
struct SobMontageRequest {
    std::string levelId;
    std::string levelHash;  // the version every replay must be of (GhostStore::levelKey)
    std::string levelName;
    MontagePlan plan;
    std::vector<std::string> names;  // one per segment of the plan (« Départ du niveau → Seuil I »)
};
std::unique_ptr<Scene> makeSobMontage(GameServices& g, SobMontageRequest req);
class GameScene;
/// Ghost Racing options (opacity, colours, HUD): from the pause menu (`game`: the run, whose
/// ghosts follow the changes at once) or from the pre-race screen (nullptr).
std::unique_ptr<Scene> makeGhostOptions(GameServices& g, GameScene* game);
/// Main menu → Succès: every achievement on a spider web (categories, stars), or as a list.
std::unique_ptr<Scene> makeAchievements(GameServices& g);
/// 2.51: Succès over a paused run (pause › Succès suivis › Modifier), on its « Suggestions »
/// tab; « Retour » goes back to the pause menu.
std::unique_ptr<Scene> makeAchievementsFromPause(GameServices& g);
/// Main menu → Collection: Vestiaire, Route des étoiles, Musiques (jukebox), Galerie.
std::unique_ptr<Scene> makeCollection(GameServices& g, int tab = 0);
/// The Collection's Vestiaire on one reward (its category, the item selected), over the
/// screen it was opened from (2.35: a reward of the challenges).
std::unique_ptr<Scene> makeCollectionOn(GameServices& g, const std::string& itemId);
/// End of a level: the rewards just unlocked by the stars (equip now or later).
std::unique_ptr<Scene> makeNewRewards(GameServices& g);
/// Main menu → Statistiques (2.54): the player's own statistics, kept on this computer. `tab`:
/// 0 Vue d'ensemble, 1 Combat, 2 Boss, 3 Niveaux, 4 Passages difficiles, 5 Données et sauvegarde.
std::unique_ptr<Scene> makeStatistics(GameServices& g, int tab = 0);
/// Statistiques › Boss › « Fiches des boss »: the bosses (victories, without being hit, per
/// phase, how they look, where to fight them). `bossId`: the boss selected on arrival.
/// `fromAsset` (2.63): opened from ADMINISTRATION › Assets on this asset (« Retour » goes back there).
std::unique_ptr<Scene> makeBossStats(GameServices& g, const std::string& bossId = {}, const std::string& fromAsset = {});
/// Boss statistics → Entraînement: hearts, power slots, powers and phase, then the boss's arena.
/// `phase` (2.54, « Passages difficiles »): the phase chosen on arrival (0 = the first, -1 every
/// phase; -2: the last choice for this boss).
std::unique_ptr<Scene> makeBossTraining(GameServices& g, const std::string& bossId, int phase = -2);
/// Main menu → Défis (2.33): `tab` 0 « Jour », 1 « Semaine », 2 « Mois », 3 « Calendrier »
/// (`select`: the month "YYYY-MM" shown there, or the challenge whose card is open).
std::unique_ptr<Scene> makeChallenges(GameServices& g, int tab = 0, const std::string& select = {});
/// The Défis screen on a challenge: its kind's tab while it runs, else the calendar on its card.
std::unique_ptr<Scene> makeChallengesOn(GameServices& g, const std::string& challengeId);
/// Before a challenge run (pushed over the Défis screen): the level, what changes today,
/// the tiers, the ghost, the connection. `practice`: hors classement.
std::unique_ptr<Scene> makeChallengeStart(GameServices& g, const std::string& challengeId, bool practice);
/// A challenge run straight away, without ghost (automated checks: --challenge-play);
/// nullptr when the challenge or its level is not on this computer.
std::unique_ptr<Scene> makeChallengeRun(GameServices& g, const std::string& challengeId, bool practice);
/// Help: every power of the player with a live example (pushed over the current screen).
std::unique_ptr<Scene> makePowerGuide(GameServices& g, int page = 0);
std::unique_ptr<Scene> makeLevelEditor(GameServices& g, const std::string& levelId);
/// ADMINISTRATION › Campagne (2.45): a campaign level edited as a new revision, published online
/// (« Publier la révision… ») from the revision `baseVersion` that is current on the server.
std::unique_ptr<Scene> makeLevelEditorCampaign(GameServices& g, const std::string& levelId, int baseVersion, const std::string& baseHash);
/// Live editing of a collaborative project in the level editor (`levelId`: its local copy).
std::unique_ptr<Scene> makeLevelEditorLive(GameServices& g, const std::string& levelId, const std::string& projectId, const std::string& role);
/// Automated screenshots: the level editor with its help open.
std::unique_ptr<Scene> makeLevelEditorHelp(GameServices& g, const std::string& levelId);
std::unique_ptr<Scene> makeEnemyEditor(GameServices& g, const std::string& enemyId = {}, int tab = 0);
/// 2.45, ADMINISTRATION › Ennemis: the enemy editor on one native entity, whose definition is
/// edited and published online as revision `baseRevision` + 1 (0: the game's own definition),
/// with the campaign levels that use it. Pushed over the administration (« Retour » pops it).
std::unique_ptr<Scene> makeEnemyEditorAdmin(GameServices& g, const std::string& enemyId, int baseRevision);
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
/// ONLINE › CLANS (2.66): the player's clan, the search of the clans, their ranking, the creation
/// of a clan. `clanId`: that clan's page on arrival. `pushed`: opened over another screen (a
/// profile, SOCIAL): « Retour » pops it instead of going back to the ONLINE screens.
std::unique_ptr<Scene> makeClans(GameServices& g, const std::string& clanId = {}, bool pushed = false);
/// The sheet of a clan raid event (CLANS › RAIDS, phase 1 of « Asterion »), pushed over the current screen.
std::unique_ptr<Scene> makeRaidEvent(GameServices& g, const std::string& eventId);
/// 2.65, raids phase 2 (docs/RAIDS.md): the map of a branch of the titan (2.68: any of the seven,
/// by its part: « crane », « bras_droit »…) of an event (zones and their state, the groups and where
/// they are, the links of the titan, « Rejoindre » / « Aider »), pushed.
std::unique_ptr<Scene> makeRaidBranch(GameServices& g, const std::string& eventId, const std::string& part = "bras_droit");
/// 2.68, raids phase 4: the global map of the titan (its seven branches, their progress, the links
/// between them, the groups, the alerts; a branch opens its map), pushed.
std::unique_ptr<Scene> makeRaidTitan(GameServices& g, const std::string& eventId);
/// 2.68: where the players of my clan are placed (branch, group), moved and rebalanced by the raid
/// leader, the chief or an officer, pushed.
std::unique_ptr<Scene> makeRaidRoster(GameServices& g, const std::string& eventId);
/// 2.68, raids phase 3: the results of an event (validated tables only: clans, players in every
/// category, the gigaboss's fights) and my rewards to claim, pushed.
std::unique_ptr<Scene> makeRaidResults(GameServices& g, const std::string& eventId);
/// 2.69, raids phase 5: the world rankings of a world event (the world's progress and final result,
/// every clan, the groups, the players in every category, my clan; validated results only, what
/// waits for its majority apart; the official result once frozen) and my rewards of it, pushed.
std::unique_ptr<Scene> makeRaidWorld(GameServices& g, const std::string& eventId);
/// 2.73: the sheets of Asterion and of its seven gigaboss (CLANS › RAIDS › « Fiches », the sheet of an
/// event, a lair of a branch map), visible whether they were met or not, pushed. `sheet`: "asterion"
/// or a gigaboss's (« skullmaw »…); `tab`: the tab shown first.
std::unique_ptr<Scene> makeRaidSheets(GameServices& g, const std::string& sheet = "asterion", int tab = 0);
/// The instance a player enters (POST /raids/events/{id}/branches/{part}/join): its event, id, clan,
/// group and branch (its part).
struct RaidEntry {
    std::string eventId, instanceId, clanTag;
    int group = 1;
    std::string branch = "bras_droit";
    /// 2.75: the zone its games start in (a group taking a wiped zone again: that zone; 0: the
    /// branch's start), and whether this player only watches (their group was wiped: spectator).
    int startZone = 0;
    bool spectator = false;
};
/// 2.75: the instance a join answered (POST …/join, …/retake): its fields, as RaidEntry keeps them.
inline void readRaidEntry(RaidEntry& e, const Json& data) {
    const Json& i = data.value("instance", Json::object());
    e.instanceId = jstr(i, "id");
    e.clanTag = jstr(i, "clan_tag");
    e.group = jget<int>(i, "group", 1);
    e.branch = jstr(i, "part", e.branch);
    e.startZone = jget<int>(i, "start_zone", 0);
    e.spectator = jget<bool>(data, "spectator", false);
}
/// The combat of a raid instance (up to 8 players in lockstep), in place of the current screen.
std::unique_ptr<Scene> makeRaidGame(GameServices& g, RaidEntry entry);
/// 2.68: an administrator's test of a raid (ADMINISTRATION › Raids › « Tester Asterion »): a
/// branch of the catalog (raid::Branch::all) and one of its zones, solo and local, never reported
/// (no consensus, branch state, ranking, contribution nor reward), invincible when asked, the
/// gigaboss scaled for `players` and started at `startPhase`. Pushed over the administration.
struct RaidTestSetup {
    std::string branch;  // raid::Branch::id
    int zone = 1;
    int players = 2;
    int startPhase = 1;
    bool invincible = true;
    /// 2.74 (phase 6), the Cœur d'Asterion: the parts of the titan simulated as fallen (bit p = the part p
    /// of raid::AsterionHeartRoutine::Part: their « modifier » links on in the test's branch state) and the
    /// relics validated; ignored by the seven branches.
    uint32_t parts = 0x7Fu;
    int relics = 0;
    /// 2.70, captures for the documentation only (`--raid-test`, never without --headless:
    /// docs/BUILDING.md): no account needed (the test stays solo, local and never reported),
    /// each seat played by its script (`scripts[i]`, InputScript's format; "" leaves seat 0 to
    /// this computer and the other seats vacant), appearing at `spawns[i]` when given.
    bool capture = false;
    std::vector<std::string> scripts;
    std::vector<std::optional<Vec2>> spawns;
    /// 2.77: « Niveau des joueurs simulés » (0 Débutant, 1 Confirmé, 2 Expert: reaction and precision).
    int skill = 1;
    /// 2.78, captures (`--raid-contrib`): the « Tableau des contributions » opens over the raid's menu at the
    /// first zone finished (the real numbers of the test, nothing sent).
    bool openContrib = false;
};
std::unique_ptr<Scene> makeRaidTest(GameServices& g, RaidTestSetup setup);
/// 2.77: « Simuler un raid complet » (ADMINISTRATION › Raids › Tester Asterion, docs/RAIDS.md §21): the
/// whole clan raid played locally by simulated players (raid::RaidSimulation), the administrator free to
/// take a place in any group or to watch one; nothing is ever sent (no network, no report, no reward).
struct RaidSimSetup {
    int groups = 7;        // 1 to 14
    int bots = 4;          // simulated players per group, the administrator not counted (1 to 7)
    bool fullGroups = false;  // 2.77.3: « Joueurs par groupe »: the administrator's seat of the other groups played too
    int skill = 1;         // 0 Débutant, 1 Confirmé, 2 Expert
    int background = 1;    // the groups not watched: 1 (as the one watched), 2 or 4 steps each (« accéléré »)
    bool invincible = true;
    bool botsInvincible = false;  // the simulated players too (a test only: their survival is not what is tested)
    bool capture = false;  // captures for the documentation (headless only): no account needed
    int panel = -1;        // captures: the raid's panel open on that tab (0 groups … 3 titan's map)
};
std::unique_ptr<Scene> makeRaidSim(GameServices& g, RaidSimSetup setup);
/// 2.77, captures for the documentation (headless only): the settings of « Simuler un raid complet ».
std::unique_ptr<Scene> makeRaidSimMenu(GameServices& g);
/// 2.70, captures for the documentation only (`--coop-test`, never without --headless): a team
/// of `players` (2 or 3) on one PC playing the EXTRÊME level `levelId`, each seat played by its
/// script (`scripts[i]`, InputScript's format), appearing at `spawns[i]` when given; never
/// ranked (one PC), as any game on one PC.
struct CoopCapture {
    std::string levelId;
    int players = 2;
    std::vector<std::string> scripts;
    std::vector<std::optional<Vec2>> spawns;
};
std::unique_ptr<Scene> makeCoopCapture(GameServices& g, CoopCapture capture);
/// The sheet of the clan whose tag is `tag` (2.63.6: a « [TAG] » clicked before a name in a
/// ranking or in SOCIAL), pushed over the current screen; found by GET /clans?q=.
std::unique_ptr<Scene> makeClanByTag(GameServices& g, const std::string& tag);
/// SOCIAL › Amis › « Regarder » (2.36, pushed over SOCIAL): a friend's run, live, re-simulated
/// from the inputs their game streams (read only; the friend is never told).
std::unique_ptr<Scene> makeSpectate(GameServices& g, const std::string& playerId, const std::string& name, const std::string& avatar);
/// Profile of a player (pushed over the current screen): records, creator card and
/// creations, followers, time played, follow / friend / block.
std::unique_ptr<Scene> makePlayerProfile(GameServices& g, const std::string& playerId);
/// `tab`: 0 Préférences, 1 Compte et confidentialité, 2 Amis.
std::unique_ptr<Scene> makeOnlineSettings(GameServices& g, int tab = 0);
/// « Course à la vérification » of an EXTRÊME level (pushed over the current screen):
/// `kind` "official" (campaign level, `levelHash` optional) or "community" (online id,
/// `version` 0 = the current one). `size` (2.63.5): the board shown first, 1 Solo, 2 Duo, 3 Trio.
std::unique_ptr<Scene> makeRaceBoard(GameServices& g, const std::string& kind, const std::string& levelId, int version = 0,
                                     const std::string& levelHash = {}, int size = 1);
/// « Avis et commentaires » (pushed over ONLINE, 2.19): `thread` = "level:<official level
/// id>" (the players' rating and the level's comments) or "run:<record id>" (the comments of
/// a run of the leaderboards; `levelId` names its level until the record is read).
std::unique_ptr<Scene> makeOnlineThread(GameServices& g, const std::string& thread, const std::string& levelId = {});
/// Downloads and plays back the replay of a verified record.
std::unique_ptr<Scene> makeReplayViewer(GameServices& g, const std::string& recordId);
/// Plays a replay already in memory (the run just finished, a .cprz file).
/// 2.75.3: `adminNote`, « Vu en tant qu'admin (…) » shown all along (a replay an administrator watches through a lock).
std::unique_ptr<Scene> makeLocalReplayViewer(GameServices& g, ReplayData replay, const std::string& title, const std::string& adminNote = {});

}  // namespace cosmic
