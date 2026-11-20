#include "ui/screens/AsterionFilm.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <set>

#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "game/content/BossStories.h"
#include "game/online/OnlineService.h"
#include "game/progression/Progression.h"
#include "ui/screens/BossStoryScene.h"

namespace cosmic::asterionfilm {

bool autoplayHeadless = false;

namespace {

/// First viewings alive (pushed, not yet destroyed): the guard pushes only one.
int g_firstViewings = 0;

struct Alive {
    Alive() { ++g_firstViewings; }
    ~Alive() { --g_firstViewings; }
    Alive(const Alive&) = delete;
    Alive& operator=(const Alive&) = delete;
};

constexpr const char* kLabel = "Revoir la cinématique";

}  // namespace

std::shared_ptr<const BossStory> film() {
    static std::shared_ptr<const BossStory> cached;
    static bool tried = false;
    if (!tried) {
        tried = true;
        std::string err;
        if (auto f = story::loadAsterionFilm(&err)) cached = std::make_shared<const BossStory>(std::move(*f));
        else Log::warn("Film d'Asterion : {}", err);
    }
    return cached;
}

bool due(const Progression& p) { return !p.asterionFilmSeen; }

std::unique_ptr<Scene> make(GameServices& g, bool firstViewing, float atSeconds) {
    std::shared_ptr<const BossStory> f = film();
    if (!f) return nullptr;
    if (!firstViewing) return makeFilm(g, f, false, {}, atSeconds);
    // « Vu » only at its end; the token counts the first viewing alive while its scene lives.
    auto alive = std::make_shared<Alive>();
    return makeFilm(
        g, f, true,
        [&g, alive] {
            if (g.progression.asterionFilmSeen) return;
            g.progression.asterionFilmSeen = true;
            g.progression.save();
        },
        atSeconds);
}

bool hold(GameServices& g) {
    if (!due(g.progression)) return false;
    if (g.app.headless() && !autoplayHeadless) return false;  // automated screenshots keep their screens
    if (!film()) return false;                                 // no film: nothing is held
    if (g_firstViewings == 0)
        if (auto s = make(g, true)) g.app.pushScene(std::move(s));
    return true;
}

float replayButtonWidth(UIContext& ui) { return ui.text().measure(kLabel, FontFace::Bold, ui.s(17)).x + ui.s(40); }

std::shared_ptr<const BossStory> victory() {
    static std::shared_ptr<const BossStory> cached;
    static bool tried = false;
    if (!tried) {
        tried = true;
        std::string err;
        if (auto f = story::loadAsterionVictory(&err)) cached = std::make_shared<const BossStory>(std::move(*f));
        else Log::warn("Film de la chute d'Asterion : {}", err);
    }
    return cached;
}

std::unique_ptr<Scene> makeVictory(GameServices& g, bool overRaid) { return makeFilm(g, victory(), false, {}, -1.0f, overRaid); }

// ===================================================================== 2.81: « Après Asterion »

std::shared_ptr<const BossStory> aftermath() {
    static std::shared_ptr<const BossStory> cached;
    static bool tried = false;
    if (!tried) {
        tried = true;
        std::string err;
        if (auto f = story::loadAsterionAftermath(&err)) cached = std::make_shared<const BossStory>(std::move(*f));
        else Log::warn("Film « Après Asterion » : {}", err);
    }
    return cached;
}

bool aftermathUnlocked(const Progression& p) { return p.asterionAftermath; }

namespace {
std::shared_ptr<const BossStory> loadTeaser(const char* file) {
    std::string err;
    if (auto f = story::loadFilm(Paths::dataDir() / "raids" / file, &err)) return std::make_shared<const BossStory>(std::move(*f));
    Log::warn("Film {} : {}", file, err);
    return nullptr;
}
}  // namespace

std::shared_ptr<const BossStory> balancier() {
    static std::shared_ptr<const BossStory> cached;
    static bool tried = false;
    if (!tried) {
        tried = true;
        cached = loadTeaser("asterion_balancier.json");
    }
    return cached;
}

std::shared_ptr<const BossStory> tour() {
    static std::shared_ptr<const BossStory> cached;
    static bool tried = false;
    if (!tried) {
        tried = true;
        cached = loadTeaser("asterion_tour.json");
    }
    return cached;
}

std::unique_ptr<Scene> makeTeaser(GameServices& g, Teaser which, float atSeconds) {
    FilmPlay play;
    play.startAt = atSeconds;
    return makeFilmPlay(g, which == Teaser::Tour ? tour() : balancier(), std::move(play));
}

namespace {

int64_t localNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

/// A party noted: what the server said, this computer's clock against the server's, what was done with it.
struct Known {
    aftermath::Party party;
    int64_t skew = 0;
    bool skewKnown = false;
    bool played = false;     // pushed once (or chained after the victory film): never again by itself
    bool announced = false;  // its toast (in a level, or before its countdown) shown
};
std::map<std::string, Known> g_parties;
std::set<std::string> g_fetching;
GameServices* g_owner = nullptr;  // the services the parties were noted with (tests make several)

double filmLength() {
    const auto f = aftermath();
    return f ? static_cast<double>(story::duration(*f)) : 0.0;
}

/// Before its time, a party is played this long ahead (its countdown) in the menus.
constexpr double kAhead = 15.0;

int64_t serverNow(const Known& k) { return localNowMs() + k.skew; }

FilmPlay playFor(const Known* k, bool together, bool overRaid) {
    FilmPlay play;
    play.keepBelow = overRaid;
    if (k) play.names = aftermath::rosterNames(k->party, &play.own);
    if (k && together && k->skewKnown) {
        const double at = aftermath::positionAt(serverNow(*k), k->party.filmAtMs);
        if (at < filmLength()) {
            play.synced = true;
            play.startAt = static_cast<float>(std::max(at, -kAhead * 8.0));
        }
    }
    return play;
}

void fetchEvent(GameServices& g, const std::string& eventId, std::function<void()> then = {}) {
    if (eventId.empty() || !g.online.loggedIn()) {
        if (then) then();
        return;
    }
    if (!g_fetching.insert(eventId).second && !then) return;
    GameServices* gp = &g;
    g.online.get(
        "/api/v1/raids/events/" + eventId,
        [gp, eventId, then](const ApiResult& r) {
            g_fetching.erase(eventId);
            if (g_owner != gp) return;  // other services since (tests)
            if (r.ok) noteEvent(*gp, r.data, eventId, !r.fromCache);
            if (then) then();
        },
        false);
}

}  // namespace

void noteEvent(GameServices& g, const Json& answer, const std::string& eventId, bool answerFresh) {
    if (g_owner != &g) {
        g_parties.clear();
        g_owner = &g;
    }
    if (!answer.is_object() || eventId.empty()) return;
    const auto party = aftermath::partyFromJson(answer.value("aftermath", Json()), eventId);
    if (!party) return;
    if (party->unlocked && !g.progression.asterionAftermath) {
        g.progression.asterionAftermath = true;
        g.progression.save();
    }
    Known& k = g_parties[eventId];
    k.party = *party;
    if (answerFresh && answer.contains("server_time")) {
        k.skew = aftermath::skewMs(answer, localNowMs());
        k.skewKnown = true;
    }
}

void noteOverview(GameServices& g, const Json& answer) {
    if (g.progression.asterionAftermath || !answer.is_object()) return;
    for (const char* list : {"world", "clan_events", "history"}) {
        auto it = answer.find(list);
        if (it == answer.end() || !it->is_array()) continue;
        for (const auto& e : *it) {
            const Json res = e.is_object() ? e.value("result", Json()) : Json();
            if (res.is_object() && jstr(res, "outcome") == "vaincu") {  // the world outcome « Asterion est tombé », official
                g.progression.asterionAftermath = true;
                g.progression.save();
                return;
            }
        }
    }
}

std::unique_ptr<Scene> makeAftermath(GameServices& g, const std::string& eventId, bool together, bool overRaid) {
    if (g_owner != &g) {
        g_parties.clear();
        g_owner = &g;
    }
    auto it = g_parties.find(eventId);
    Known* k = it == g_parties.end() ? nullptr : &it->second;
    if (k && together) k->played = true;
    return makeFilmPlay(g, aftermath(), playFor(k, together, overRaid));
}

std::unique_ptr<Scene> makeAftermathPreview(GameServices& g, bool overRaid) {
    FilmPlay play;
    play.keepBelow = overRaid;
    return makeFilmPlay(g, aftermath(), std::move(play));
}

void playFall(GameServices& g, const std::string& eventId, bool overRaid) {
    if (g_owner != &g) {
        g_parties.clear();
        g_owner = &g;
    }
    if (!eventId.empty()) {
        g_parties[eventId].played = true;  // chained here: the party never pushes it a second time
        fetchEvent(g, eventId);            // its start time and its clans, read while the victory film plays
    }
    FilmPlay play;
    play.keepBelow = overRaid;
    GameServices* gp = &g;
    play.onClose = [gp, eventId, overRaid](bool) {
        if (g_owner != gp) return;
        // right after the victory film (ended or left): « Après Asterion », together when its time is known
        std::unique_ptr<Scene> next = eventId.empty() ? makeAftermathPreview(*gp, overRaid) : makeAftermath(*gp, eventId, true, overRaid);
        if (next) gp->app.pushScene(std::move(next));
    };
    if (auto s = makeFilmPlay(g, victory(), std::move(play))) g.app.pushScene(std::move(s));
}

void watch(GameServices& g, const std::string& eventId) {
    if (!aftermath()) return;
    if (g_owner != &g) {
        g_parties.clear();
        g_owner = &g;
    }
    auto play = [&g, eventId] {
        if (g_owner != &g) return;
        if (auto s = makeAftermath(g, eventId, true)) g.app.pushScene(std::move(s));
    };
    if (eventId.empty() || g_parties.count(eventId)) play();
    else fetchEvent(g, eventId, play);  // from a notification: the event first (its clans, its time); offline: the generic text
}

namespace {
constexpr const char* kAftermathLabel = "Après Asterion";
}

float aftermathButtonWidth(UIContext& ui) { return ui.text().measure(kAftermathLabel, FontFace::Bold, ui.s(17)).x + ui.s(40); }

bool aftermathButton(GameServices& g, const std::string& id, const Rect& r, const std::string& eventId) {
    if (!aftermath() || r.w <= 0.0f || r.h <= 0.0f) return false;
    UIContext::ButtonStyle st;
    st.small = true;
    st.fontSize = 17.0f;
    if (!g.ui.button(id, r, kAftermathLabel, st)) return false;
    watch(g, eventId);
    return true;
}

void updateParty(GameServices& g, float) {
    if (g_owner != &g) {
        g_parties.clear();
        g_owner = &g;
    }
    for (const std::string& id : g.online.takeFilmNotices()) fetchEvent(g, id);
    if (g_parties.empty() || !aftermath()) return;
    if (g.app.headless() && !autoplayHeadless) return;  // automated screenshots keep their screens
    Scene* top = g.app.topScene();
    if (!top) return;
    const double len = filmLength();
    for (auto& [id, k] : g_parties) {
        if (k.played || !k.skewKnown || k.party.filmAtMs <= 0) continue;
        const double at = aftermath::positionAt(serverNow(k), k.party.filmAtMs);
        if (at >= len) {  // over: watched again from the sheet of the event
            k.played = true;
            continue;
        }
        if (storyShot(top) >= 0) continue;  // a film or a story already on the screen
        if (g.app.anySceneHoldsNotifications()) {
            // In a level (a run, a raid, coop, even under its pause menu): never pushed over it; a notification offers it
            // (shown after the run).
            if (!k.announced) {
                k.announced = true;
                g.online.notify(OnlineService::Notification::Kind::Info, "Asterion est tombé — regarder le film avec votre clan",
                                "« Après Asterion » : NOTIFICATIONS, ou la fiche de l'événement.");
            }
            continue;
        }
        if (g.app.input().textInputActive() || g.app.transitioning()) continue;  // never while something is typed
        if (at < -kAhead) {  // in the menus, before its time: said once, then its countdown
            if (!k.announced) {
                k.announced = true;
                g.online.notify(OnlineService::Notification::Kind::Info, "Asterion est tombé — regarder le film avec votre clan",
                                "« Après Asterion » commence bientôt, avec les clans de la chute.");
            }
            continue;
        }
        k.played = true;
        if (auto s = makeFilmPlay(g, aftermath(), playFor(&k, true, false))) g.app.pushScene(std::move(s));
        return;  // one at a time
    }
}

void resetParties() {
    g_parties.clear();
    g_fetching.clear();
    g_owner = nullptr;
}

const aftermath::Party* partyOf(const std::string& eventId) {
    auto it = g_parties.find(eventId);
    return it == g_parties.end() ? nullptr : &it->second.party;
}

namespace {
constexpr const char* kVictoryLabel = "Revoir la victoire";
}

float victoryButtonWidth(UIContext& ui) { return ui.text().measure(kVictoryLabel, FontFace::Bold, ui.s(17)).x + ui.s(40); }

bool victoryButton(GameServices& g, const std::string& id, const Rect& r) {
    if (!victory() || r.w <= 0.0f || r.h <= 0.0f) return false;
    UIContext::ButtonStyle st;
    st.small = true;
    st.fontSize = 17.0f;
    if (!g.ui.button(id, r, kVictoryLabel, st)) return false;
    if (auto s = makeVictory(g, false)) g.app.pushScene(std::move(s));
    return true;
}

bool replayButton(GameServices& g, const std::string& id, const Rect& r) {
    if (!film() || r.w <= 0.0f || r.h <= 0.0f) return false;
    UIContext::ButtonStyle st;
    st.small = true;
    st.fontSize = 17.0f;  // fitted to its width by the button if it must
    if (!g.ui.button(id, r, kLabel, st)) return false;
    if (auto s = make(g, false)) g.app.pushScene(std::move(s));
    return true;
}

}  // namespace cosmic::asterionfilm
