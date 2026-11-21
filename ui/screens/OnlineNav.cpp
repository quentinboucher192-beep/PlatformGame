#include "ui/screens/OnlineNav.h"

#include <string>

#include "game/GameServices.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/CosmicStyle2.h"

namespace cosmic {

int onlineSocialBadge(GameServices& g) {
    if (!g.online.loggedIn()) return 0;
    return g.online.unreadMessages() + g.online.pendingFriendRequests();
}

namespace {
cstyle::Motion navMotion(GameServices& g) { return cstyle::Motion::from(g.settings.graphics.particleQuality, g.settings.graphics.screenShake); }
}  // namespace

Vec2 drawOnlineTitle(GameServices& g, Vec2 leftMid, float maxW) {
    UIContext& ui = g.ui;
    const float u = cstyle::CosmicLayout::of(ui.screenW(), ui.screenH()).unit;
    return cstyle::neonText(ui, "COSMIC ONLINE", leftMid, ui.s(34), maxW, Align::Left, ui.time(), navMotion(g), u);
}

float drawOnlineNav(GameServices& g, const Rect& area, OnlineSection current) {
    UIContext& ui = g.ui;
    // 2.84: cockpit plates (Cosmic kit), the same places as before. One set of focus animations
    // for every screen of ONLINE (each item keeps its own).
    static cstyle::Anims anims;
    const cstyle::Motion mo = navMotion(g);
    const float t = ui.time(), dt = g.app.frameTime() / 1000.0f;
    // CADEAUX only once the player received a gift (its pill: the gifts still wrapped).
    const bool gifts = g.online.hasGifts() || current == OnlineSection::Gifts;
    const int kItems = gifts ? 8 : 7;
    Rect nav{area.x, area.y, area.w, ui.s(static_cast<float>(kItems * 58 + 20))};
    ui.r().fillRoundedRect(nav, ui.s(14), Color{6, 10, 22, 150});
    ui.pushId("online/nav");
    float y = nav.y + ui.s(10), h = ui.s(50);
    // `messages`: unread messages (red pill); `requests`: friend requests (amber pill).
    auto item = [&](const char* id, const char* label, const char* icon, Color accent, bool here, int messages = 0, int requests = 0) {
        // The plate slides right when focused: room kept for it inside the column.
        Rect r{nav.x + ui.s(6), y, nav.w - ui.s(18), h};
        const float pu = h / 64.0f;
        cstyle::PlateEx p;
        p.plate = {label, "", icon, accent};
        p.plate.feature = std::string("online.nav.") + id;  // « NOUVEAU » (data/news/new_features.json)
        p.selected = here;
        p.chevron = !here && messages <= 0 && requests <= 0;  // the pills take its place
        // The counters take their place at the right edge first; the label keeps the room
        // left of them (it shrinks rather than run under a pill).
        float pillsW = 0.0f;
        if (messages > 0) pillsW += OnlineUi::countPillWidth(ui, messages, 26, 15) + ui.s(6);
        if (requests > 0) pillsW += OnlineUi::countPillWidth(ui, requests, 26, 15) + ui.s(6);
        if (pillsW > 0.0f) {
            p.rightW = pillsW;
            p.drawRight = [&ui, messages, requests](const Rect& room, float) {
                float right = room.right();
                if (messages > 0) right = OnlineUi::countPill(ui, right, room.center().y, messages, ui.theme().danger, Color::white(), 26, 15) - ui.s(6);
                if (requests > 0) OnlineUi::countPill(ui, right, room.center().y, requests, ui.theme().warm, Color{20, 12, 4, 255}, 26, 15);
            };
        }
        const bool c = cstyle::plateButtonEx(ui, anims, id, r, p, pu, 1.0f, t, dt, mo);
        y += h + ui.s(8);
        return c && !here;
    };
    const Color cyan{94, 224, 255, 255}, green{110, 225, 150, 255}, violet{190, 150, 255, 255}, gold{255, 205, 110, 255};
    // The levels are played from the level card ("Jouer ce niveau"); the campaign and the
    // settings are reached from the main menu. The WORKSHOP lives here only.
    if (item("levels", "NIVEAUX EN LIGNE", "campaign", cyan, current == OnlineSection::Levels)) g.app.switchScene(makeOnlineLevels(g));
    if (item("ranking", "CLASSEMENTS", "stats", gold, current == OnlineSection::Ranking)) g.app.switchScene(makeOnlineRanking(g));
    const bool in = g.online.loggedIn();
    if (item("social", "SOCIAL", "online", green, current == OnlineSection::Social, in ? g.online.unreadMessages() : 0,
             in ? g.online.pendingFriendRequests() : 0))
        g.app.switchScene(makeSocial(g));
    // 2.66: the clans (the invitations received in an amber pill, like the friend requests).
    if (item("clans", "CLANS", "coop", violet, current == OnlineSection::Clans, 0, in ? g.online.clanInvitations() : 0)) g.app.switchScene(makeClans(g));
    if (gifts && item("gifts", "CADEAUX", "collection", gold, current == OnlineSection::Gifts, 0, in ? g.online.giftsUnopened() : 0))
        g.app.switchScene(makeGifts(g));
    if (item("projects", "PROJETS", "challenges", Color{255, 112, 136, 255}, current == OnlineSection::Projects)) g.app.switchScene(makeProjects(g));
    if (item("workshop", "ATELIER", "settings", cyan, current == OnlineSection::Workshop)) g.app.switchScene(makeContentManager(g));
    if (item("back", "RETOUR", "quit", Color{140, 152, 186, 255}, false)) g.app.switchScene(makeMainMenu(g));
    ui.popId();
    return nav.h;
}

}  // namespace cosmic
