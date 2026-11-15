#include "ui/screens/OnlineNav.h"

#include <string>

#include "game/GameServices.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

int onlineSocialBadge(GameServices& g) {
    if (!g.online.loggedIn()) return 0;
    return g.online.unreadMessages() + g.online.pendingFriendRequests();
}

float drawOnlineNav(GameServices& g, const Rect& area, OnlineSection current) {
    UIContext& ui = g.ui;
    constexpr int kItems = 6;
    Rect nav{area.x, area.y, area.w, ui.s(kItems * 58 + 20)};
    ui.r().fillRoundedRect(nav, ui.s(14), Color{6, 10, 22, 170});
    ui.pushId("online/nav");
    float y = nav.y + ui.s(10), h = ui.s(50);
    // `messages`: unread messages (red pill); `requests`: friend requests (amber pill).
    auto item = [&](const char* id, const char* label, bool here, int messages = 0, int requests = 0) {
        UIContext::ButtonStyle st;
        st.align = Align::Left;
        st.fontSize = 21;
        st.selected = here;
        st.primary = here;
        Rect r{nav.x + ui.s(10), y, nav.w - ui.s(20), h};
        // The counters take their place at the right edge first; the label keeps the room
        // left of them (it shrinks rather than run under a pill).
        float pills = r.right() - ui.s(12);
        float pillsW = 0.0f;
        if (messages > 0) pillsW += OnlineUi::countPillWidth(ui, messages, 26, 15) + ui.s(6);
        if (requests > 0) pillsW += OnlineUi::countPillWidth(ui, requests, 26, 15) + ui.s(6);
        const float textX = r.x + ui.s(20), room = pills - pillsW - textX - ui.s(4);
        const bool fits = ui.text().measure(label, FontFace::Bold, ui.s(st.fontSize)).x <= room;
        bool c = ui.button(id, r, fits ? std::string(label) : std::string(), st);
        if (!fits)
            ui.label(label, {textX, r.center().y}, ui.fitFont(label, FontFace::Bold, st.fontSize, room), here ? Color::white() : ui.theme().text,
                     Align::Left, FontFace::Bold, VAlign::Middle);
        if (here) ui.r().fillRect({nav.x + ui.s(4), y + ui.s(8), ui.s(4), h - ui.s(16)}, ui.theme().accent);
        if (messages > 0) pills = OnlineUi::countPill(ui, pills, r.center().y, messages, ui.theme().danger, Color::white(), 26, 15) - ui.s(6);
        if (requests > 0) OnlineUi::countPill(ui, pills, r.center().y, requests, ui.theme().warm, Color{20, 12, 4, 255}, 26, 15);
        y += h + ui.s(8);
        return c && !here;
    };
    // The levels are played from the level card ("Jouer ce niveau"); the campaign and the
    // settings are reached from the main menu. The WORKSHOP lives here only.
    if (item("levels", "NIVEAUX EN LIGNE", current == OnlineSection::Levels)) g.app.switchScene(makeOnlineLevels(g));
    if (item("ranking", "CLASSEMENTS", current == OnlineSection::Ranking)) g.app.switchScene(makeOnlineRanking(g));
    const bool in = g.online.loggedIn();
    if (item("social", "SOCIAL", current == OnlineSection::Social, in ? g.online.unreadMessages() : 0, in ? g.online.pendingFriendRequests() : 0))
        g.app.switchScene(makeSocial(g));
    if (item("projects", "PROJETS", current == OnlineSection::Projects)) g.app.switchScene(makeProjects(g));
    if (item("workshop", "ATELIER", current == OnlineSection::Workshop)) g.app.switchScene(makeContentManager(g));
    if (item("back", "RETOUR", false)) g.app.switchScene(makeMainMenu(g));
    ui.popId();
    return nav.h;
}

}  // namespace cosmic
