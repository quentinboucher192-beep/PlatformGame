#include "ui/screens/OnlineNav.h"

#include <string>

#include "game/GameServices.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

int onlineSocialBadge(GameServices& g) {
    if (!g.online.loggedIn()) return 0;
    return g.online.unreadMessages() + g.online.pendingFriendRequests();
}

float drawOnlineNav(GameServices& g, const Rect& area, OnlineSection current) {
    UIContext& ui = g.ui;
    constexpr int kItems = 5;
    Rect nav{area.x, area.y, area.w, ui.s(kItems * 58 + 20)};
    ui.r().fillRoundedRect(nav, ui.s(14), Color{6, 10, 22, 170});
    ui.pushId("online/nav");
    float y = nav.y + ui.s(10), h = ui.s(50);
    auto item = [&](const char* id, const char* label, bool here, int badge = 0) {
        UIContext::ButtonStyle st;
        st.align = Align::Left;
        st.fontSize = 21;
        st.selected = here;
        st.primary = here;
        Rect r{nav.x + ui.s(10), y, nav.w - ui.s(20), h};
        bool c = ui.button(id, r, label, st);
        if (here) ui.r().fillRect({nav.x + ui.s(4), y + ui.s(8), ui.s(4), h - ui.s(16)}, ui.theme().accent);
        if (badge > 0) {
            std::string t = badge > 99 ? "99+" : std::to_string(badge);
            float bw = std::max(ui.s(28), ui.text().measure(t, FontFace::Bold, ui.s(15)).x + ui.s(14));
            Rect b{r.right() - ui.s(12) - bw, r.center().y - ui.s(13), bw, ui.s(26)};
            ui.r().fillRoundedRect(b, ui.s(13), ui.theme().danger);
            ui.label(t, b.center(), 15, Color::white(), Align::Center, FontFace::Bold, VAlign::Middle);
        }
        y += h + ui.s(8);
        return c && !here;
    };
    // The levels are played from the level card ("Jouer ce niveau"); the campaign and the
    // settings are reached from the main menu. The WORKSHOP lives here only.
    if (item("levels", "ONLINE LEVELS", current == OnlineSection::Levels)) g.app.switchScene(makeOnlineLevels(g));
    if (item("ranking", "ONLINE RANKING", current == OnlineSection::Ranking)) g.app.switchScene(makeOnlineRanking(g));
    if (item("social", "SOCIAL", current == OnlineSection::Social, onlineSocialBadge(g))) g.app.switchScene(makeSocial(g));
    if (item("workshop", "WORKSHOP", current == OnlineSection::Workshop)) g.app.switchScene(makeContentManager(g));
    if (item("back", "RETOUR", false)) g.app.switchScene(makeMainMenu(g));
    ui.popId();
    return nav.h;
}

}  // namespace cosmic
