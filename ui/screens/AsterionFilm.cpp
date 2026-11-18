#include "ui/screens/AsterionFilm.h"

#include <algorithm>

#include "engine/core/Log.h"
#include "game/GameServices.h"
#include "game/content/BossStories.h"
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
