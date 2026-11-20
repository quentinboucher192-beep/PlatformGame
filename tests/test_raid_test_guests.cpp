// 2.80 (docs/RAIDS.md « Tester avec des joueurs invités »): the window of the players invited to an administrator's test of Asterion
// (ui/online/RaidTestGuests): its places measured at 1280 × 720 and 1920 × 1080, in French and in English —
// each part inside the window and after the one before, the list and the picker side by side (or one under
// the other) without overlapping, every row's texts and controls inside their row and apart, and every label
// of a control (« Invincible », the group's names up to the reinforcements, the actions, « Inviter »,
// « Fermer ») at a size that fits its room. Then the window drawn with guests and candidates: no crash.

#include <SDL3/SDL.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "engine/core/Scene.h"
#include "game/GameServices.h"
#include "tests/TestFramework.h"
#include "ui/online/RaidTestGuests.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

struct Blank : Scene {
    void render(float) override {}
};

/// `r` within `outer` (half a pixel of rounding allowed: rooms are computed by subtraction).
bool inside(const Rect& outer, const Rect& r) {
    return r.empty() || (r.x >= outer.x - 0.5f && r.y >= outer.y - 0.5f && r.right() <= outer.right() + 0.5f && r.bottom() <= outer.bottom() + 0.5f);
}
bool apart(const Rect& a, const Rect& b) { return a.empty() || b.empty() || !a.intersects(b); }

void report(bool ok, const char* what, const Vec2& size) {
    if (!ok) std::printf("  %s en %.0f x %.0f (%s)\n", what, size.x, size.y, i18n::language() == Language::English ? "EN" : "FR");
    CHECK(ok);
}

}  // namespace

TEST_CASE("raid test guests (2.80): the invited players' window measured — 1280 × 720, 1920 × 1080, French and English") {
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
        // The groups as the whole raid names them (its groups, then its reinforcements).
        std::vector<std::string> groups;
        for (int k = 1; k <= 14; ++k) groups.push_back("Groupe " + std::to_string(k));
        for (int k = 1; k <= 8; ++k) groups.push_back("Renfort " + std::to_string(k));
        // Every state a guest's row may show, with the longest name.
        std::vector<RaidTestGuests::Guest> guests;
        for (const char* st : {"", "pending", "accepted", "declined", "expired", "cancelled"}) {
            RaidTestGuests::Guest x;
            x.name = "Capitaine_Nebuleuse";
            x.status = st;
            x.expires = 4000000000LL;
            x.playing = true;
            guests.push_back(x);
        }
        guests[2].connected = false;
        for (const Language lang : {Language::French, Language::English}) {
            i18n::setLanguage(lang);
            for (const Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}}) {
                app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
                app.renderer().refreshOutputSize();
                app.runFrames(1);
                app.renderer().refreshOutputSize();
                UIContext& ui = g.ui;
                ui.begin(app, 1.0f / 60.0f);
                const Rect screen{0.0f, 0.0f, ui.screenW(), ui.screenH()};
                auto width = [&](const std::string& t, FontFace f, float px) { return ui.text().measure(t, f, ui.s(px)).x; };
                for (const bool attached : {false, true}) {
                    const RaidTestGuests::Layout L = RaidTestGuests::layout(ui, attached);
                    report(inside(screen, L.box), "fenêtre hors de l'écran", size);
                    for (const Rect& r : {L.title, L.close, L.list, L.pick, L.search, L.pickList, L.status, L.listHead, L.pickHead})
                        report(inside(L.box, r), "partie hors de la fenêtre", size);
                    report(apart(L.title, L.close), "titre sous « Fermer »", size);
                    report(width("Fermer", FontFace::Bold, L.closeFont) <= L.close.w - ui.s(20) + 0.5f, "« Fermer » déborde", size);
                    float y = std::max(L.title.bottom(), L.close.bottom()) - ui.s(10);
                    for (size_t i = 0; i < L.intro.size(); ++i) {
                        report(inside(L.box, L.intro[i]) && L.intro[i].y >= y - 0.5f, "introduction mal placée", size);
                        report(width(L.introLines[i], FontFace::Body, 14) <= L.intro[i].w + 0.5f, "introduction trop large", size);
                        y = L.intro[i].bottom();
                    }
                    report(L.list.y >= y - 0.5f && L.pick.y >= y - 0.5f, "listes sur l'introduction", size);
                    report(apart(L.list, L.pick), "la liste des invités sur celle des joueurs connectés", size);
                    report(L.status.y >= L.list.bottom() && L.status.y >= L.pick.bottom(), "ligne d'état sur les listes", size);
                    report(inside(L.pick, L.search) && inside(L.pick, L.pickList) && L.pickList.y >= L.search.bottom(), "recherche mal placée", size);
                    report(L.list.h > L.rowH * 2.0f && L.pickList.h > L.pickRowH * 2.0f, "listes trop basses", size);
                    // A guest's row, as the list draws it (its width: the list's, its scroll bar aside).
                    const Rect row{L.list.x, L.list.y + ui.s(40), L.list.w - ui.s(14), L.rowH};
                    for (const bool withGroups : {false, true})
                        for (const RaidTestGuests::Guest& x : guests) {
                            const std::string state = RaidTestGuests::stateText(x, 1000);
                            const std::string action = RaidTestGuests::actionText(x);
                            for (const std::string role : {"ADMIN", "JOUEUR"}) {
                                const RaidTestGuests::RowLayout R = RaidTestGuests::rowLayout(ui, row, withGroups, role, state, action, groups);
                                for (const Rect& r : {R.name, R.role, R.state, R.group, R.invincible, R.action}) report(inside(row, r), "élément hors de sa ligne", size);
                                report(apart(R.name, R.role) && apart(R.role, R.state) && apart(R.name, R.state), "première ligne : chevauchement", size);
                                report(apart(R.group, R.invincible) && apart(R.invincible, R.action) && apart(R.group, R.action), "seconde ligne : chevauchement", size);
                                report(R.invincible.y >= R.state.bottom() && R.action.y >= R.state.bottom(), "seconde ligne sur la première", size);
                                report(R.name.w >= ui.s(60), "nom sans place", size);
                                // Each pill's text at 75 % at least (raidui::fit), the controls' labels at their least size.
                                report(width(state, FontFace::Bold, 13 * 0.75f) <= R.state.w - ui.s(14) + 0.5f, "état trop long pour sa pastille", size);
                                report(width(role, FontFace::Bold, 13 * 0.75f) <= R.role.w - ui.s(14) + 0.5f, "rôle trop long", size);
                                report(ui.s(116) + width("Invincible", FontFace::Body, 22 * 0.6f) <= R.invincible.w + 0.5f, "« Invincible » déborde", size);
                                report(width(action, FontFace::Bold, R.actionFont) <= R.action.w - ui.s(20) + 0.5f, "action trop longue", size);
                                if (withGroups) {
                                    report(!R.group.empty(), "choix du groupe absent", size);
                                    for (const std::string& gname : groups)
                                        report(width(gname, FontFace::Bold, 22 * 0.5f) <= R.group.w - ui.s(94) + 0.5f, "nom de groupe trop long", size);
                                } else {
                                    report(R.group.empty(), "choix du groupe pour une zone", size);
                                }
                            }
                        }
                    // A connected player's row of the picker.
                    const Rect prow{L.pickList.x, L.pickList.y, L.pickList.w - ui.s(14), L.pickRowH};
                    for (const std::string role : {"ADMIN", "JOUEUR"}) {
                        const RaidTestGuests::PickRowLayout P = RaidTestGuests::pickRowLayout(ui, prow, role);
                        report(inside(prow, P.name) && inside(prow, P.role) && inside(prow, P.invite), "joueur connecté hors de sa ligne", size);
                        report(apart(P.name, P.role) && apart(P.role, P.invite) && apart(P.name, P.invite), "joueur connecté : chevauchement", size);
                        report(width("Inviter", FontFace::Bold, P.inviteFont) <= P.invite.w - ui.s(20) + 0.5f, "« Inviter » déborde", size);
                        report(width(role, FontFace::Bold, 13 * 0.75f) <= P.role.w - ui.s(14) + 0.5f, "rôle trop long", size);
                        report(P.name.w >= ui.s(60), "nom sans place", size);
                    }
                }
                ui.end();
            }
        }
        i18n::setLanguage(Language::French);
        // Drawn with players chosen (the setup) and the whole raid's groups: nothing breaks, nothing is sent.
        RaidTestGuests w(g);
        w.setShape(groups, 4);
        w.setChosen({RaidTestGuest{"11111111-1111-4111-8111-111111111111", "Alpha", false, 2, false},
                     RaidTestGuest{"22222222-2222-4222-8222-222222222222", "AdminB", true, 0, true}});
        CHECK(w.active() == 2);
        CHECK(w.chosen().size() == 2u);
        CHECK(!w.summary().empty());
        w.open();
        app.runFrames(1);
        UIContext& ui = g.ui;
        ui.begin(app, 1.0f / 60.0f);
        w.draw();
        ui.end();
        CHECK(w.isOpen());
        w.close();
        CHECK(!w.isOpen());
    }
    app.shutdown();
}
