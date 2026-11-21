// 2.75.5 (docs/RAIDS.md §21): no card opens by itself during a raid any more. A zone finished (and the
// group eliminated, a zone taken back, a gigaboss fallen…) is a short notification that takes no input:
// the player's frame is the same with or without it, and it goes by itself; the raid's menu is opened
// and closed by the player (its key, Échap / B), holds this player's inputs as the pause does, and every
// text of both is measured into its place.

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Application.h"
#include "engine/core/Scene.h"
#include "engine/input/InputManager.h"
#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidRun.h"
#include "saves/RaidHudLayout.h"
#include "tests/TestFramework.h"
#include "ui/online/ContribTable.h"
#include "ui/online/RaidHud.h"
#include "ui/online/RaidMenu.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

struct Svc {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    Svc() {
        content.reload();
        res.init(nullptr, &audio);
    }
    WorldServices ws() { return WorldServices{res, audio, content, nullptr, 0.0f, 1.0f}; }
};

const raid::Branch& bras() {
    const raid::Branch* b = raid::Branch::builtin();
    CHECK(b != nullptr);
    return *b;
}

uint32_t bit(const std::string& id) {
    const int i = bras().objectiveIndex(id);
    CHECK(i >= 0);
    return i >= 0 ? 1u << i : 0u;
}

/// What a player holds at step `t`: running right, jumping now and then, attacking.
PlayerInput held(uint32_t t) {
    PlayerInput in;
    in.held[static_cast<int>(Action::MoveRight)] = true;
    in.move = {1.0f, 0.0f};
    in.held[static_cast<int>(Action::Jump)] = t % 60 < 20;
    in.pressed[static_cast<int>(Action::Jump)] = t % 60 == 0;
    in.pressed[static_cast<int>(Action::Attack)] = t % 30 == 7;
    return in;
}

void press(InputManager& in, SDL_Scancode sc) {
    in.beginFrame();
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = sc;
    in.handleEvent(e);
}

/// A screen that draws nothing: the application's frames go on (the interface's scale follows the size).
struct Blank : Scene {
    void render(float) override {}
};

raidmenu::Notice notice(const std::string& title, const std::string& text, raidmenu::Section s) {
    raidmenu::Notice n;
    n.title = title;
    n.text = text;
    n.section = s;
    n.color = Color{124, 240, 168, 255};
    return n;
}

}  // namespace

TEST_CASE("raid notices (2.75.5): a zone finished neutralises no input: the player's frame and the run are the same with or without its notification") {
    Svc s;
    raid::RaidRun with(s.ws(), bras(), raid::kMaxInstancePlayers), without(s.ws(), bras(), raid::kMaxInstancePlayers);
    CHECK(with.begin(0, 2));
    CHECK(without.begin(0, 2));
    raidmenu::Overlay told, silent;  // the screen that tells it, and one that never does
    std::vector<InputFrame> fa(raid::kMaxInstancePlayers), fb(raid::kMaxInstancePlayers);
    for (int k = 1; k < raid::kMaxInstancePlayers; ++k) fa[static_cast<size_t>(k)].flags = fb[static_cast<size_t>(k)].flags = InputFrame::Vacant;
    int notified = 0, differing = 0, held_back = 0;
    float clock = 0.0f;
    for (uint32_t t = 0; t < 720; ++t) {
        clock += 1.0f / 120.0f;
        const PlayerInput in = held(t);
        fa[0] = told.frame(in, false, false, with.wiped());
        fb[0] = silent.frame(in, false, false, without.wiped());
        differing += !(fa[0] == fb[0]);
        // Only the group eliminated (a rule of the raid: the same on both screens) ever holds it.
        held_back += !with.wiped() && !(fa[0] == InputFrame::fromPlayerInput(in));
        // Another group finishes the zone at step 100 (the branch's validated state).
        const uint32_t state = t >= 100 ? bit("biceps_gardien") : 0u;
        with.step(fa.data(), static_cast<int>(fa.size()), state);
        without.step(fb.data(), static_cast<int>(fb.size()), state);
        if (with.zoneFinishedNow() == 2) {
            told.notices.push(notice("ZONE TERMINÉE · Biceps", "Par un autre groupe", raidmenu::Section::Zones));
            ++notified;
        }
        // The notification is on screen meanwhile (drawn, then gone by itself).
        told.notices.drawn(told.notices.front().size(), clock);
        told.notices.update(clock);
        CHECK(!told.hold(false, false, false).any());
        CHECK(!told.wantsPointer());
    }
    CHECK(notified == 1);
    CHECK(differing == 0);
    CHECK(held_back == 0);
    CHECK(with.hash() == without.hash());  // the same game on both screens
    CHECK(told.notices.journal().size() == 1);
    CHECK(!told.notices.any());  // gone by itself (shown from step 100, 6 s played: more than its 3.5 s)
    // The menu is the player's choice: its inputs held as in the pause, back once closed.
    const PlayerInput in = held(1);
    told.menu.open(raidmenu::Section::Zones);
    CHECK(told.frame(in, false, false, false) == InputFrame{});
    CHECK(told.wantsPointer());
    told.menu.close();
    CHECK(told.frame(in, false, false, false) == InputFrame::fromPlayerInput(in));
    CHECK(told.frame(in, true, false, false) == InputFrame{});  // the pause, as before
    CHECK(told.frame(in, false, false, true) == InputFrame{});  // the group eliminated: spectators
}

TEST_CASE("raid notices (2.75.5): each one goes by itself after its seconds; several wait their turn; the journal keeps them, timed in the raid") {
    CHECK(raidmenu::Notices::kShow >= 3.0f);
    CHECK(raidmenu::Notices::kShow <= 4.0f);
    raidmenu::Notices n;
    for (int i = 0; i < 5; ++i) {
        raidmenu::Notice x = notice("N" + std::to_string(i), "texte", i == 4 ? raidmenu::Section::Titan : raidmenu::Section::Zones);
        x.raidSeconds = 60.0f + static_cast<float>(i);
        n.push(x);
    }
    CHECK(n.front().size() == static_cast<size_t>(raidmenu::Notices::kMaxShown));
    CHECK(n.latestSection() == raidmenu::Section::Count);  // none on screen yet
    n.drawn(3, 10.0f);
    CHECK(n.latestSection() == raidmenu::Section::Zones);
    n.update(11.0f);
    CHECK(n.front().size() == 3);
    CHECK(n.front().front()->title == "N0");
    CHECK(raidmenu::Notices::alpha(*n.front().front(), 11.0f) > 0.99f);
    CHECK(raidmenu::Notices::alpha(*n.front().front(), 10.0f + raidmenu::Notices::kShow - 0.1f) < 0.5f);  // fading out
    n.update(10.0f + raidmenu::Notices::kShow + 0.01f);
    CHECK(n.front().size() == 2);  // the next ones in, not started yet (their time starts when drawn)
    CHECK(n.front().front()->title == "N3");
    CHECK(n.front().front()->shownAt < 0.0f);
    n.update(100.0f);
    CHECK(n.front().size() == 2);  // never drawn: still waiting
    n.drawn(2, 100.0f);
    CHECK(n.latestSection() == raidmenu::Section::Titan);
    n.update(100.0f + raidmenu::Notices::kShow + 0.01f);
    CHECK(!n.any());
    CHECK(n.journal().size() == 5);
    CHECK(raidmenu::raidTime(n.journal()[1].raidSeconds) == "01:01");
    CHECK(raidmenu::raidTime(3725.0f) == "1:02:05");
    // A long stay in the menu: the oldest waiting give way, the journal keeps every one.
    raidmenu::Notices many;
    for (int i = 0; i < 30; ++i) many.push(notice("M" + std::to_string(i), "", raidmenu::Section::Journal));
    CHECK(many.journal().size() == 30);
    CHECK(many.front().size() == 3);
    CHECK(many.front().back()->title != "M0");
    // The reminder names the menu's key and where it tells more.
    raidmenu::Notice z = notice("ZONE TERMINÉE · X", "", raidmenu::Section::Zones);
    z.table = true;
    CHECK(raidmenu::reminder(z, "Tab") == "Tableau : Tab");
    CHECK(raidmenu::reminder(notice("GROUPE ÉLIMINÉ", "", raidmenu::Section::Group), "L3") == "Groupe : L3");
}

TEST_CASE("raid menu (2.75.5): its key opens and closes it, Échap closes it without pausing, its sections follow the keys; its key is set in Commandes") {
    const InputBindings d = InputBindings::defaults();
    CHECK(d.keys[static_cast<int>(Action::RaidMenu)][0] == SDL_SCANCODE_TAB);
    CHECK(d.pad[static_cast<int>(Action::RaidMenu)][0] == PadBinding::button(SDL_GAMEPAD_BUTTON_LEFT_STICK));
    CHECK(actionIsRebindable(Action::RaidMenu));
    CHECK(static_cast<int>(Action::RaidMenu) >= InputFrame::kActions);  // never recorded
    CHECK(actionFromId("raid_menu") == Action::RaidMenu);
    CHECK(InputBindings::fromJson(d.toJson()).keys[static_cast<int>(Action::RaidMenu)][0] == SDL_SCANCODE_TAB);
    // A file written before it, where the player gave Tab to the jump and L3 to « Interagir »: no key does two things.
    const Json old = Json::parse(R"({"keyboard": {"jump": ["Tab", ""]}, "gamepad": {"interact": [{"button": "leftstick"}, null], "ghost_toggle": [null, null]}})");
    const InputBindings b = InputBindings::fromJson(old);
    CHECK(b.keys[static_cast<int>(Action::RaidMenu)][0] == SDL_SCANCODE_UNKNOWN);
    CHECK(b.pad[static_cast<int>(Action::RaidMenu)][0].kind == PadBinding::Kind::None);
    CHECK(InputBindings::fromJson(Json::parse(R"({"keyboard": {"jump": ["Space", ""]}})")).keys[static_cast<int>(Action::RaidMenu)][0] == SDL_SCANCODE_TAB);

    InputManager in;
    in.setBindings(d);
    raidmenu::Menu m;
    press(in, SDL_SCANCODE_TAB);
    CHECK(!m.handleKeys(in, false, raidmenu::Section::Count));  // in the pause, at the end: not now
    CHECK(!m.isOpen());
    CHECK(!m.handleKeys(in, true, raidmenu::Section::Zones));  // opened on the notification shown
    CHECK(m.isOpen());
    CHECK(m.section() == raidmenu::Section::Zones);
    press(in, SDL_SCANCODE_RIGHT);
    m.handleKeys(in, true, raidmenu::Section::Count);
    CHECK(m.section() == raidmenu::Section::Journal);
    press(in, SDL_SCANCODE_Q);  // the previous section (LB at the gamepad)
    m.handleKeys(in, true, raidmenu::Section::Count);
    CHECK(m.section() == raidmenu::Section::Zones);
    press(in, SDL_SCANCODE_LEFT);
    m.handleKeys(in, true, raidmenu::Section::Count);
    press(in, SDL_SCANCODE_LEFT);
    m.handleKeys(in, true, raidmenu::Section::Count);
    CHECK(m.section() == raidmenu::Section::Titan);  // round the tabs
    press(in, SDL_SCANCODE_DOWN);
    m.handleKeys(in, true, raidmenu::Section::Count);
    CHECK(m.scrollBy > 0.0f);
    press(in, SDL_SCANCODE_TAB);
    CHECK(!m.handleKeys(in, true, raidmenu::Section::Count));  // the same key closes it
    CHECK(!m.isOpen());
    press(in, SDL_SCANCODE_TAB);
    m.handleKeys(in, true, raidmenu::Section::Count);
    CHECK(m.isOpen());
    CHECK(m.section() == raidmenu::Section::Titan);  // where the player left it
    press(in, SDL_SCANCODE_ESCAPE);
    CHECK(in.menuPressed(Action::Pause));            // Échap is the pause's key too…
    CHECK(m.handleKeys(in, true, raidmenu::Section::Count));  // …used by the menu: the screen does not pause with it
    CHECK(!m.isOpen());
    // Open, a game action held does not reach the game; closed, the same input plays.
    press(in, SDL_SCANCODE_TAB);
    m.handleKeys(in, true, raidmenu::Section::Count);
    raidmenu::Overlay o;
    o.menu = m;
    press(in, SDL_SCANCODE_SPACE);  // « Valider » in the menu, the jump in the game
    const PlayerInput jump = in.consumePlayerInput();
    CHECK(jump.isPressed(Action::Jump));
    CHECK(o.frame(jump, false, false, false) == InputFrame{});
    o.menu.close();
    CHECK(o.frame(jump, false, false, false) == InputFrame::fromPlayerInput(jump));
}

TEST_CASE("raid menu (titan 13): Tab while moving never runs through its tabs; a fresh press moves it by exactly one") {
    // « Je fais Tab pour ouvrir le menu tout en avançant : ça change d'onglets à l'infini. » A held direction repeats
    // (InputManager's auto-repeat); the menu, its contributions and the test's panel read a fresh press only, and a
    // direction held when they open waits for its release.
    InputManager in;
    in.setBindings(InputBindings::defaults());
    int repeats = 0;
    auto frame = [&](bool right, bool tab = false) {
        in.beginFrame();
        if (tab) {
            SDL_Event e{};
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.scancode = SDL_SCANCODE_TAB;
            in.handleEvent(e);
        }
        in.simulateHeld(Action::MoveRight, right);  // the right arrow: the move and the menus' « Droite »
        in.simulateHeld(Action::MenuRight, right);
        in.update(1.0f / 60.0f);
        if (in.menuPressed(Action::MenuRight)) ++repeats;
    };
    raidmenu::Menu m;
    for (int t = 0; t < 30; ++t) frame(true);  // walking right
    frame(true, true);                         // Tab, still walking
    m.handleKeys(in, true, raidmenu::Section::Remaining);
    CHECK(m.isOpen());
    repeats = 0;
    for (int t = 0; t < 60; ++t) {
        frame(true);
        m.handleKeys(in, true, raidmenu::Section::Count);
        CHECK(m.section() == raidmenu::Section::Remaining);
    }
    CHECK(repeats > 3);  // the input does repeat: the menu ignores it
    frame(false);        // released…
    m.handleKeys(in, true, raidmenu::Section::Count);
    CHECK(m.section() == raidmenu::Section::Remaining);
    frame(true);  // …pressed again: one section
    m.handleKeys(in, true, raidmenu::Section::Count);
    CHECK(m.section() == raidmenu::Section::Zones);
    for (int t = 0; t < 60; ++t) {  // held on: still one
        frame(true);
        m.handleKeys(in, true, raidmenu::Section::Count);
    }
    CHECK(m.section() == raidmenu::Section::Zones);
    // The same for the contributions' table (its sort: Gauche / Droite), opened while the right arrow is held.
    contrib::View v;
    const contrib::Col sort0 = v.sort;
    for (int t = 0; t < 60; ++t) {
        frame(true);
        CHECK(!v.handleKeys(in, 2, 0));
        CHECK(v.sort == sort0);
    }
    frame(false);
    v.handleKeys(in, 2, 0);
    frame(true);
    v.handleKeys(in, 2, 0);
    CHECK(static_cast<int>(v.sort) == (static_cast<int>(sort0) + 1) % contrib::kCols);
    for (int t = 0; t < 60; ++t) {
        frame(true);
        v.handleKeys(in, 2, 0);
    }
    CHECK(static_cast<int>(v.sort) == (static_cast<int>(sort0) + 1) % contrib::kCols);
    // A short tap (pressed and released within a frame) still counts, once.
    raidmenu::Menu q;
    frame(false, true);
    q.handleKeys(in, true, raidmenu::Section::Remaining);
    in.beginFrame();
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = SDL_SCANCODE_RIGHT;
    in.handleEvent(e);
    in.simulateHeld(Action::MoveRight, false);
    in.simulateHeld(Action::MenuRight, false);
    in.update(1.0f / 60.0f);
    q.handleKeys(in, true, raidmenu::Section::Count);
    CHECK(q.section() == raidmenu::Section::Zones);
}

TEST_CASE("raid notices and menu (2.75.5): measured into their places, inside the screen, nothing over anything, no focus taken") {
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
        app.pushScene(std::make_unique<Blank>());
        CHECK(std::string(RaidHudLayout::id(RaidHudElement::Notices)) == "notices");
        CHECK(!RaidHudLayout::canHide(RaidHudElement::Notices));
        CHECK(RaidHudLayout::fromJson(RaidHudLayout{}.toJson())[RaidHudElement::Notices].visible);
        std::vector<raidmenu::Notice> ex;
        ex.push_back(notice("ZONE TERMINÉE · Forge-cœur du Coude", "Par un autre groupe (état validé par le serveur) : son tableau est dans le menu du raid.",
                            raidmenu::Section::Zones));
        ex.push_back(notice("GROUPE ÉLIMINÉ", "Tout le groupe est à terre : la zone attend un groupe de renfort. Vous devenez spectateurs.",
                            raidmenu::Section::Group));
        ex.push_back(notice("ZONE REPRISE · Poignet-verrou", "Terminée par le groupe de renfort [AUBE]·2.", raidmenu::Section::Zones));
        const std::vector<const raidmenu::Notice*> list{&ex[0], &ex[1], &ex[2]};
        raidmenu::MenuData data;
        data.title = "MENU DU RAID · BRAS DROIT";
        data.keyName = "Tab";
        data.remainingTitle = "CE QU'IL RESTE À FAIRE · Biceps";
        data.remaining = {{"OK", "Vaincre le golem gardien", Color::white()}, {"", "BRANCHE · ce qu'il manque", Color::white()},
                          {"■", "Poignet-verrou : Ouvrir l'accès : allumer les deux générateurs (1 / 2)", Color::white()}};
        raidhud::ZoneTable t;
        t.title = "TABLEAU DES CONTRIBUTIONS · Biceps";
        t.sub = "Zone terminée par votre groupe";
        for (int i = 0; i < 8; ++i) t.rows.push_back({"Joueur " + std::to_string(i + 1), Color::white(), i == 0, 10 * i, 20 * i, i, 1, 0});
        data.tableAvailable = true;  // 2.79: the zones' tables are in the one contributions table (ContribTable)
        data.tableLines = {"Zone 3 · Biceps · terminée par votre groupe · 12:34 · Dégâts au boss : vous (31 %)"};
        data.otherZones = {"Veine de magma"};
        data.journal = &ex;
        data.groupTitle = "GROUPE · [AUBE]·1 · 8 / 8";
        for (int i = 0; i < 8; ++i) data.group.push_back({"Joueur " + std::to_string(i + 1), i == 3 ? "à terre" : "en jeu", "3 / 5"});
        data.titan = {{"→", "Pyroclast est debout · Antre", Color::white()}};
        data.results = true;
        for (const Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}, Vec2{1366, 768}, Vec2{2560, 1440}}) {
            app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
            app.renderer().refreshOutputSize();
            app.runFrames(1);  // a new frame: the interface's scale follows the size
            app.renderer().refreshOutputSize();
            UIContext& ui = g.ui;
            ui.begin(app, 1.0f / 60.0f);
            const float W = ui.screenW(), H = ui.screenH();
            CHECK(std::abs(ui.scale() - std::max(0.5f, std::min(W / 1920.0f, H / 1080.0f))) < 0.001f);
            const RaidHudLayout l;
            const raidhud::HudAreas hud{};
            // Its default place: inside the screen, over no other element of the raids' HUD.
            const Rect area = raidhud::area(ui, l, RaidHudElement::Notices, hud);
            CHECK(area.w >= ui.s(160));
            CHECK(area.h >= ui.s(60));
            CHECK(area.x >= 0.0f && area.y >= 0.0f && area.right() <= W + 0.5f && area.bottom() <= H + 0.5f);
            CHECK(area.y < H * 0.5f);  // away from the middle, where the players are
            for (int i = 0; i < kRaidHudElements; ++i) {
                const auto e = static_cast<RaidHudElement>(i);
                if (e == RaidHudElement::Notices || RaidHudLayout::hudElement(e) != HudElement::Count) continue;
                const Rect o = raidhud::area(ui, l, e, hud);
                if (o.w <= 0.0f) continue;
                const bool clear = !area.intersects(o);
                if (!clear) std::printf("  « Notifications » touche « %s » en %.0f x %.0f\n", RaidHudLayout::label(e), size.x, size.y);
                CHECK(clear);
            }
            // Three at once (and the button): each inside the area, one under the other.
            const raidmenu::NoticeLayout nl = raidmenu::layoutNotices(ui, area, 1.0f, list, raidmenu::buttonText("Tab"), true);
            CHECK(nl.button.w > 0.0f);
            CHECK(area.containsRect(nl.button));
            CHECK(!nl.boxes.empty());
            for (size_t i = 0; i < nl.boxes.size(); ++i) {
                CHECK(area.containsRect(nl.boxes[i].rect));
                CHECK(!nl.boxes[i].rect.intersects(nl.button));
                CHECK(nl.boxes[i].lines.size() <= 2);
                for (size_t j = i + 1; j < nl.boxes.size(); ++j) CHECK(!nl.boxes[i].rect.intersects(nl.boxes[j].rect));
            }
            // A tiny area: fewer of them, never outside it.
            const Rect tiny{area.x, area.y, area.w, ui.s(70)};
            for (const raidmenu::NoticeBox& b : raidmenu::layoutNotices(ui, tiny, 1.0f, list, raidmenu::buttonText("Tab"), true).boxes)
                CHECK(tiny.containsRect(b.rect));
            // The menu: each part after the one before, inside its panel, inside the screen.
            const raidmenu::MenuLayout ml = raidmenu::layoutMenu(ui, "Tab");
            CHECK(ml.panel.x >= 0.0f && ml.panel.y >= 0.0f && ml.panel.right() <= W + 0.5f && ml.panel.bottom() <= H + 0.5f);
            for (const Rect& r : {ml.title, ml.close, ml.tabsRow, ml.view, ml.keys}) CHECK(ml.panel.containsRect(r));
            CHECK(!ml.title.intersects(ml.close));
            CHECK(ml.ruleY >= ml.title.bottom());
            CHECK(ml.ruleY + static_cast<float>(ml.rule.size()) * ui.s(20) <= ml.tabsRow.y + 0.5f);
            CHECK(ml.tabsRow.bottom() <= ml.view.y + 0.5f);
            CHECK(ml.view.bottom() <= ml.keys.y + 0.5f);
            CHECK(ml.view.h >= ui.s(200));
            CHECK(ml.tabs.size() == static_cast<size_t>(raidmenu::kSections));
            for (size_t i = 0; i < ml.tabs.size(); ++i) {
                CHECK(ml.tabsRow.containsRect(ml.tabs[i]));
                for (size_t j = i + 1; j < ml.tabs.size(); ++j) CHECK(!ml.tabs[i].intersects(ml.tabs[j]));
                // Each tab's name fits its tab (at worst at 75 %, the smallest raidui::fit draws).
                const std::string label = raidmenu::sectionLabel(static_cast<raidmenu::Section>(i));
                const float w = ui.text().measure(label, FontFace::Bold, ui.s(ml.tabFont) * 0.75f).x;
                if (w > ml.tabs[i].w - ui.s(12)) std::printf("  onglet « %s » trop large en %.0f x %.0f\n", label.c_str(), size.x, size.y);
                CHECK(w <= ml.tabs[i].w - ui.s(12) + 0.5f);
            }
            // Every section has its height (long ones scroll) and draws; a zone table has room for every row.
            for (int sct = 0; sct < raidmenu::kSections; ++sct)
                CHECK(raidmenu::sectionHeight(ui, data, static_cast<raidmenu::Section>(sct), ml.view.w - ui.s(20)) > ui.s(30));
            CHECK(raidhud::zoneTableHeight(ui, t, ml.view.w - ui.s(20), 1.0f) >= ui.s(28) * 8.0f + ui.s(46));
            ui.end();
        }
        // Drawn, the notifications take no focus (no stop for the keyboard or the gamepad), even with an
        // arrow pressed (a card's button took it: Espace, the jump, then pressed it).
        app.runFrames(1);
        press(app.input(), SDL_SCANCODE_RIGHT);
        UIContext& ui = g.ui;
        ui.begin(app, 1.0f / 60.0f);
        size_t drawn = 0;
        raidmenu::drawNotices(ui, raidhud::area(ui, RaidHudLayout{}, RaidHudElement::Notices, {}), 1.0f, list, {1.0f, 1.0f, 1.0f}, "Tab", true, &drawn);
        ui.end();
        CHECK(drawn >= 1);
        CHECK(!ui.hasFocus());
        // The menu draws every section (the player's own choice: its own keys).
        app.runFrames(1);
        ui.begin(app, 1.0f / 60.0f);
        raidmenu::Menu m;
        m.open(raidmenu::Section::Zones);
        for (int sct = 0; sct < raidmenu::kSections; ++sct) {
            m.select(static_cast<raidmenu::Section>(sct));
            const raidmenu::MenuAction a = raidmenu::drawMenu(ui, m, data);
            CHECK(!a.close);
            CHECK(!a.results);
        }
        ui.end();
    }
    app.shutdown();
}
