// 2.72 « Qui est en ligne » in the lists of a clan (ui/online/PresenceList): CLANS › Membres and
// the registrants of a raid's sheet. The pill and « X/Y en ligne » only from a fresh answer of
// the server (never the offline cache, never a count the server did not send), the members
// « en ligne » first, and the header measured: « X/Y en ligne » never overflows it nor runs into
// its title — at 1280×720 at its full size, in French and in English, whatever the numbers.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/online/Clans.h"
#include "tests/TestFramework.h"
#include "ui/online/PresenceList.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

TEST_CASE("presence list: « X/Y en ligne », the pills and the order come from the server's fresh answer only") {
    using namespace presencelist;
    CHECK(counterText(3, 12) == "3/12 en ligne");
    CHECK(counterText(0, 0) == "0/0 en ligne");
    CHECK(counterText(50, 50) == "50/50 en ligne");
    CHECK(counterText(-1, 12).empty());  // unknown: no counter, never a guess

    // The pill: one of the three states of SOCIAL; « hidden » (a server before 2.72) reads offline.
    CHECK(shown(Json{{"presence", "online"}}, false) == "online");
    CHECK(shown(Json{{"presence", "away"}}, false) == "away");
    CHECK(shown(Json{{"presence", "offline"}}, false) == "offline");
    CHECK(shown(Json{{"presence", "hidden"}}, false) == "offline");
    CHECK(shown(Json{{"presence", "online"}}, true).empty());  // the offline cache: no longer true
    CHECK(shown(Json{{"presence", "dancing"}}, false).empty());
    CHECK(shown(Json{{"presence", 1}}, false).empty());
    CHECK(shown(Json::object(), false).empty());  // not a member of the clan: nothing sent
    CHECK(shown(Json(), false).empty());

    // The count: the server's, between 0 and the list's size, from a fresh answer.
    const Json page{{"members_online", 2}};
    CHECK(onlineCount(page, "members_online", 5, false) == 2);
    CHECK(onlineCount(page, "members_online", 2, false) == 2);
    CHECK(onlineCount(page, "members_online", 1, false) == -1);  // more than the list: refused
    CHECK(onlineCount(page, "members_online", 5, true) == -1);   // the offline cache
    CHECK(onlineCount(page, "roster_online", 5, false) == -1);   // not sent (another clan, no account)
    CHECK(onlineCount(Json{{"members_online", -1}}, "members_online", 5, false) == -1);
    CHECK(onlineCount(Json{{"members_online", 1.5}}, "members_online", 5, false) == -1);
    CHECK(onlineCount(Json{{"members_online", "2"}}, "members_online", 5, false) == -1);
    CHECK(onlineCount(Json::array(), "members_online", 5, false) == -1);

    // The order: « en ligne », « absent », « hors ligne », then without presence; stable inside.
    const Json list = Json::array({Json{{"id", "a"}, {"presence", "offline"}}, Json{{"id", "b"}, {"presence", "away"}}, Json{{"id", "c"}},
                                   Json{{"id", "d"}, {"presence", "online"}}, Json{{"id", "e"}, {"presence", "offline"}},
                                   Json{{"id", "f"}, {"presence", "online"}}, Json{{"id", "g"}, {"presence", "hidden"}}});
    auto ids = [&](const std::vector<size_t>& order) {
        std::string s;
        for (size_t i : order) s += jstr(list[i], "id");
        return s;
    };
    CHECK(ids(orderByPresence(list, false)) == "dfbaegc");
    CHECK(ids(orderByPresence(list, true)) == "abcdefg");  // the cache: the server's order, untouched
    CHECK(orderByPresence(Json(), false).empty());
}

namespace {

bool within(const Rect& in, float x, float right) { return in.x >= x - 0.01f && in.right() <= right + 0.01f; }

}  // namespace

TEST_CASE("presence list: « X/Y en ligne » never overflows its header nor its title (1280×720, French and English)") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    Application app;
    AppConfig cfg;
    cfg.headless = true;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.vsync = false;
    CHECK(app.init(cfg));
    {
        GameServices g(app);
        g.loadAll();
        app.setWindowSize(1280, 720);
        app.renderer().refreshOutputSize();
        UIContext& ui = g.ui;
        ui.begin(app, 1.0f / 60.0f);
        CHECK(ui.screenW() == 1280.0f && ui.screenH() == 720.0f);
        const float W = ui.screenW();
        // The headers as the screens place them at this size:
        //  - CLANS › Membres (ClanScene::layout, drawPage, drawMembers): the zone right of the
        //    navigation (margins of 22, a column of 272, a gap of 18), its members column
        //    (32 % of it, between 340 and 520), the header 16 inside each side;
        //  - the registrants of a raid (RaidEventScene::render, drawAside, middle): the aside
        //    (34 % of the width, 520 at most), 16 inside, the list 8 wider, 14 for its scroll bar.
        const float zoneB = W - ui.s(22) - (ui.s(22) + ui.s(272)) - ui.s(18);
        const float membersW = std::clamp(zoneB * 0.32f, ui.s(340), ui.s(520));
        const float asideW = std::min(ui.s(520), W * 0.34f);
        struct Header {
            const char* what;
            float line;
            std::string title;
            FontFace titleFace;
            int most;  // the longest list: 50 members, 200 registrants (the server's limits)
        };
        const std::vector<Header> headers = {{"clan", membersW - ui.s(32), "MEMBRES", FontFace::Bold, clans::kMaxMembers},
                                             {"raid", (asideW - ui.s(32) + ui.s(8)) - ui.s(14), "INSCRITS DU CLAN", FontFace::Title, 200}};
        for (Language lang : {Language::French, Language::English}) {
            i18n::setLanguage(lang);
            if (lang == Language::English) CHECK(i18n::tr("12/50 en ligne") == "12/50 online");
            for (const Header& h : headers) {
                const float x = ui.s(100), right = x + h.line;
                for (int online : {0, 7, h.most}) {
                    const std::string counter = presencelist::counterText(online, h.most);
                    const auto L = presencelist::layoutHeader(ui, x, right, ui.s(40), h.title, 14, h.titleFace, counter, 14, FontFace::Bold);
                    // At 1280×720 both texts keep their full size, never shortened.
                    if (L.counterSize != 14.0f || L.counterText != counter || L.titleSize != 14.0f || L.titleText != h.title)
                        std::printf("  en-tête %s (%s) : « %s » réduit ou tronqué\n", h.what, i18n::code(lang), counter.c_str());
                    CHECK(L.counterSize == 14.0f);
                    CHECK(L.counterText == counter);
                    CHECK(L.titleSize == 14.0f);
                    CHECK(L.titleText == h.title);
                    // Each text within its room, the rooms on the line, one after the other.
                    CHECK(ui.text().measure(L.counterText, FontFace::Bold, ui.s(L.counterSize)).x <= L.counter.w + 0.5f);
                    CHECK(ui.text().measure(L.titleText, h.titleFace, ui.s(L.titleSize)).x <= L.title.w + 0.5f);
                    CHECK(within(L.counter, x, right));
                    CHECK(within(L.title, x, right));
                    CHECK(L.title.right() + ui.s(12) <= L.counter.x + 0.01f);
                }
            }
            // Narrower lines (any screen): smaller, then shortened, never over the line nor the title.
            for (float line = ui.s(140); line <= ui.s(700); line += ui.s(20)) {
                const float x = ui.s(50), right = x + line;
                const auto L = presencelist::layoutHeader(ui, x, right, ui.s(40), "INSCRITS DU CLAN", 14, FontFace::Title,
                                                          presencelist::counterText(200, 200), 14, FontFace::Bold);
                CHECK(ui.text().measure(L.counterText, FontFace::Bold, ui.s(L.counterSize)).x <= L.counter.w + 0.5f);
                CHECK(ui.text().measure(L.titleText, FontFace::Title, ui.s(L.titleSize)).x <= L.title.w + 0.5f);
                CHECK(within(L.counter, x, right));
                CHECK(within(L.title, x, right));
                CHECK(L.title.right() <= L.counter.x + 0.01f);
                CHECK(L.counterSize >= 14.0f * 0.75f - 0.01f && L.titleSize >= 14.0f * 0.75f - 0.01f);
            }
            // No counter (offline, no account, another clan): the title has the whole line.
            const auto none = presencelist::layoutHeader(ui, ui.s(50), ui.s(400), ui.s(40), "MEMBRES", 14, FontFace::Bold,
                                                         presencelist::counterText(-1, 12), 14, FontFace::Bold);
            CHECK(none.counterText.empty());
            CHECK(none.counter.w == 0.0f);
            CHECK(std::abs(none.title.w - ui.s(350)) < 0.01f);
        }
        i18n::setLanguage(Language::French);
    }
    app.shutdown();
}
