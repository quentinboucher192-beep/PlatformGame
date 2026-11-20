#pragma once
// 2.68: Asterion's mini-film (« ASTERION, LE DÉVOREUR DE MONDES », about two minutes in ten shots:
// data/raids/asterion_film.json, its scenes written by tools/assetgen/environment/story_asterion.py),
// played by the stories' scene (makeFilm) the first time the player would see the titan: before
// CLANS › RAIDS shows anything, before the sheet of a raid event, before any AsterionFigure is
// drawn. That first viewing cannot be skipped (no key, button or click; no hint); closing the
// game's window still closes the game. « Vu » is written in the progression (saves/progress.json,
// kept by the progression copies and the account migration: ProgressMerge) only when the film
// has played to its end: a first viewing interrupted plays again the next time. Afterwards,
// « Revoir la cinématique » on the RAIDS hero card plays it again, and that viewing may be left.
// Local content: offline as online. Display only (no simulation, no replay depends on it).

#include <memory>
#include <string>

#include "engine/core/Math.h"
#include "engine/core/Json.h"
#include "engine/core/Scene.h"
#include "game/raid/Aftermath.h"

namespace cosmic {

class GameServices;
class Progression;
class UIContext;
struct BossStory;

namespace asterionfilm {

/// The film, read once (nullptr when its files cannot be read: then it never holds a screen).
std::shared_ptr<const BossStory> film();
/// Whether its first viewing is due: never played to its end in this progression.
bool due(const Progression& p);
/// The guard of every screen that shows Asterion (one call, first thing): when the first viewing
/// is due, pushes the film over the screen (once) and returns true — the caller then draws
/// nothing of the titan this frame. False otherwise (seen, no film). Automated runs (headless)
/// never start it by themselves, unless `autoplayHeadless` (tests).
bool hold(GameServices& g);
extern bool autoplayHeadless;
/// The film: its first viewing (`firstViewing`: locked; « vu » written at its end) or a replay
/// (may be left). nullptr without a film.
std::unique_ptr<Scene> make(GameServices& g, bool firstViewing, float atSeconds = -1.0f);
/// « Revoir la cinématique »: the width it needs (pixels), then the button drawn in `r` (keyboard,
/// gamepad, mouse); pressed, it plays the replay over the screen. False when there is no film.
float replayButtonWidth(UIContext& ui);
bool replayButton(GameServices& g, const std::string& id, const Rect& r);

/// 2.74 (phase 6): the film of Asterion's fall (« LA CHUTE D'ASTERION »: data/raids/asterion_victoire.json,
/// its scenes written by tools/assetgen/environment/story_asterion_victoire.py): the titan collapsing, its
/// heart going supernova, the clans' banners. Played for everyone of an instance at the step the server
/// validates the shared victory (over the raid's combat, which keeps running: `overRaid`), then from the
/// sheet of the event (« Revoir la victoire »). It may be left. nullptr without its files.
std::shared_ptr<const BossStory> victory();
std::unique_ptr<Scene> makeVictory(GameServices& g, bool overRaid);
float victoryButtonWidth(UIContext& ui);
bool victoryButton(GameServices& g, const std::string& id, const Rect& r);

/// 2.81: « Après Asterion » (data/raids/asterion_apres.json, game/raid/Aftermath.h): what the fall changes in the world,
/// watched together. Who: every player of a Heart instance (right after the victory film: playFall) and, at the same
/// server time, the online members of the clans present at the fall (their « heart_film » notification, read every
/// minute: updateParty). Never over a level: there, a notification offers it (NOTIFICATIONS › « Regarder le film »); it
/// plays by itself only in the menus or after the raid's victory film. A late joiner starts mid-film (« Depuis le
/// début » offered). Watched again from the sheet of the event and the Collection (Galerie › Films), once unlocked:
/// for a player who took part in an event whose Heart fell, or for everyone once the world outcome « Asterion est
/// tombé » is official (the server says which; kept in the progression for offline viewing). nullptr without its files.
std::shared_ptr<const BossStory> aftermath();
bool aftermathUnlocked(const Progression& p);
/// What an answer of the server says (GET /raids/events/{id}: its `aftermath`; `eventId` its id): unlocks it, and
/// notes the start time and the clans of the fall (played by updateParty while it is live). `answerFresh`: not from
/// the cache (its server_time measures this computer's clock).
void noteEvent(GameServices& g, const Json& answer, const std::string& eventId, bool answerFresh = true);
/// 2.81: the overview of the raids (its events and their official results): an outcome « vaincu » unlocks it.
void noteOverview(GameServices& g, const Json& answer);
/// The film with the clans of event `eventId` when known (else the generic text), played at the server's time when it
/// is live and `together`, else from its start. `overRaid`: over a raid's combat. nullptr without its files.
std::unique_ptr<Scene> makeAftermath(GameServices& g, const std::string& eventId, bool together, bool overRaid = false);
/// An administrator's preview (ADMINISTRATION › Films) or a test: the generic text, from its start.
std::unique_ptr<Scene> makeAftermathPreview(GameServices& g, bool overRaid = false);
/// The validated fall (or a test's: `eventId` empty, nothing asked of the server): the victory film, then « Après
/// Asterion » at the server's time (a test: the generic text, from its start).
void playFall(GameServices& g, const std::string& eventId, bool overRaid);
/// From a notification or a button: plays it (together while it is live), reading the event first when needed.
void watch(GameServices& g, const std::string& eventId);
/// « Après Asterion » (sheet of the event): its width, then the button (keyboard, gamepad, mouse); pressed,
/// watch(). False without the film.
float aftermathButtonWidth(UIContext& ui);
bool aftermathButton(GameServices& g, const std::string& id, const Rect& r, const std::string& eventId);
/// The watch party, once an image (main loop): the « heart_film » notifications read, the film pushed in the menus at
/// its time, a toast in a level (never pushed over one).
void updateParty(GameServices& g, float dt);
/// Tests: forgets every party noted.
void resetParties();
/// Tests: the party noted for `eventId` (nullptr: none).
const aftermath::Party* partyOf(const std::string& eventId);

}  // namespace asterionfilm

}  // namespace cosmic
