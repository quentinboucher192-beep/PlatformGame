// Gifts (« Cadeaux », game/online/Gifts, ui/screens/GiftsScene, GiftsUi, AdminGifts, server/src/gifts.ts): the
// client's model (the list as the server sends it, the content hidden until opened, a parcel's items one by one and no
// count before it is opened, the filters, the counts, the gift « Cadeau suivant » opens), the wraps' and
// parcels' sheets and their generator (tools/assetgen/environment/gift_wraps.py
// --check, the same ids and lights as the game's table), what may be given in ADMINISTRATION › Cadeaux, and the
// layouts measured at 1280 x 720 and 1920 x 1080, in French and in English: no text over another nor out of its
// room (the list, a row, the detail, the opening's texts and buttons, the admin tab's columns).

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "engine/core/Scene.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/online/Gifts.h"
#include "tests/TestFramework.h"
#include "ui/screens/AdminGifts.h"
#include "ui/screens/GiftsUi.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

#ifdef _WIN32
#define COSMIC_NULL_DEVICE "NUL"
#else
#define COSMIC_NULL_DEVICE "/dev/null"
#endif

namespace {

Json giftJson(int64_t id, bool opened, const std::string& wrap = "or", const std::string& message = "Merci !") {
    Json j = {{"id", id},
              {"from", {{"id", "p-admin"}, {"username", "NovaRider"}}},
              {"wrap", wrap},
              {"message", message},
              {"created_at", "2026-11-20T20:00:00.000Z"},
              {"opened", opened},
              {"opened_at", opened ? Json("2026-11-20T21:00:00.000Z") : Json()},
              {"reward", opened ? Json{{"kind", "suit"}, {"id", "suit_or"}, {"name", "Or stellaire"}} : Json()}};
    return j;
}

struct Blank : Scene {
    void render(float) override {}
};

bool inside(const Rect& outer, const Rect& in) { return in.w <= 0.0f || outer.expanded(0.5f).containsRect(in); }
bool apart(const Rect& a, const Rect& b) {
    if (a.w <= 0.0f || b.w <= 0.0f) return true;
    return !a.expanded(-0.25f).intersects(b.expanded(-0.25f));
}
/// A fitted text no wider than its room (measured as it is drawn: translated, or verbatim).
bool fits(UIContext& ui, const giftsui::Text& t, FontFace face, float room, bool verbatim = false) {
    i18n::Verbatim v(verbatim);
    return ui.text().measure(t.shown, face, ui.s(t.size)).x <= room + 0.5f;
}

}  // namespace

TEST_CASE("gifts: the list as the server sends it, the content hidden until opened") {
    Json data = {{"gifts", Json::array({giftJson(3, false, "givre", "  Bravo\x07 !  "), giftJson(2, true, "cosmos", ""), giftJson(1, false, "papier")})}};
    // A wrapped gift whose answer carries a reward anyway: the game never shows it before the server opened it.
    data["gifts"][0]["reward"] = Json{{"kind", "suit"}, {"id", "suit_or"}};
    data["gifts"].push_back(Json{{"id", 0}});  // malformed: ignored
    const auto list = gifts::parseList(data);
    CHECK(list.size() == 3);
    CHECK(list[0].id == 3 && !list[0].opened && !list[0].reward.has_value());
    CHECK(list[0].message == "Bravo !");  // control characters removed, trimmed
    CHECK(list[0].wrap == "givre");
    CHECK(list[0].from == "NovaRider");
    CHECK(list[1].opened && list[1].reward && list[1].reward->kind == "suit" && list[1].reward->id == "suit_or");
    CHECK(list[2].wrap == "or");  // an unknown wrap draws as « or »
    CHECK(gifts::unopenedCount(list) == 2);
    // The filters keep the list's order.
    const auto toOpen = gifts::filtered(list, gifts::Filter::ToOpen);
    CHECK(toOpen.size() == 2 && toOpen[0]->id == 3 && toOpen[1]->id == 1);
    const auto opened = gifts::filtered(list, gifts::Filter::Opened);
    CHECK(opened.size() == 1 && opened[0]->id == 2);
    CHECK(gifts::filtered(list, gifts::Filter::All).size() == 3);
    // « Cadeau suivant »: the next one still wrapped after the current one, never the current one.
    CHECK(gifts::nextToOpen(list, 3)->id == 1);
    CHECK(gifts::nextToOpen(list, 1)->id == 3);  // from the top again
    CHECK(gifts::nextToOpen(list, 2)->id == 1);
    std::vector<gifts::Gift> after = list;
    gifts::Gift g3 = *gifts::parseGift(giftJson(3, true, "givre"));
    gifts::applyOpened(after, g3);
    CHECK(gifts::unopenedCount(after) == 1);
    CHECK(gifts::nextToOpen(after, 1) == nullptr);  // the last one: « Voir mes cadeaux »
    CHECK(after[0].reward && after[0].reward->id == "suit_or");
    CHECK(gifts::parseList(Json::object()).empty());
}

TEST_CASE("gifts: a parcel (« Colis »): one wrapped entry, no count before it is opened, then its items one by one") {
    Json wrapped = giftJson(7, false, "rubis");
    wrapped["parcel"] = true;
    wrapped["reward"] = Json();
    // Even if an answer carried contents for a wrapped parcel, the game keeps none (nothing to count).
    wrapped["contents"] = Json::array({Json{{"kind", "suit"}, {"id", "suit_or"}}});
    Json opened = giftJson(3, true, "azur");
    opened["parcel"] = true;
    opened["reward"] = Json();
    opened["contents"] = Json::array({Json{{"kind", "suit"}, {"id", "suit_or"}, {"name", "Or stellaire"}}, Json{{"kind", "hat"}, {"id", "x"}},
                                      Json{{"kind", "trail"}, {"id", "trail_comet"}, {"name", "Comète"}}, Json{{"kind", "badge"}, {"id", "communaute_coeur"}}});
    // A gift with the same id as the opened parcel: they never mix (their keys differ).
    const Json data = {{"gifts", Json::array({wrapped, opened, giftJson(3, false, "or")})}};
    const auto list = gifts::parseList(data);
    CHECK(list.size() == 3);
    CHECK(list[0].parcel && !list[0].opened && list[0].contents.empty() && !list[0].reward);
    CHECK(gifts::revealed(list[0]).empty());
    CHECK(!gifts::isRevealed(list[0]));
    CHECK(gifts::openPath(list[0]) == "/api/v1/gifts/parcels/7/open");
    CHECK(gifts::openPath(list[2]) == "/api/v1/gifts/3/open");
    CHECK(list[1].parcel && list[1].opened && list[1].contents.size() == 3);  // the unknown kind left out
    CHECK(list[1].contents[1].id == "trail_comet" && list[1].contents[2].kind == "badge");
    CHECK(gifts::isRevealed(list[1]));
    CHECK(list[1].key() == -3 && list[2].key() == 3);
    CHECK(gifts::unopenedCount(list) == 2);  // a parcel counts as one
    CHECK(gifts::nextToOpen(list, list[0].key())->key() == 3);
    CHECK(gifts::nextToOpen(list, 3)->key() == -7);
    // Opening the parcel replaces it, never the gift with the same number.
    std::vector<gifts::Gift> after = list;
    gifts::Gift p7 = list[1];
    p7.id = 7;
    gifts::applyOpened(after, p7);
    CHECK(after[0].opened && after[0].contents.size() == 3);
    CHECK(!after[2].opened);
    gifts::Gift g3 = *gifts::parseGift(giftJson(3, true));
    gifts::applyOpened(after, g3);
    CHECK(after[2].opened && after[1].parcel && after[1].contents.size() == 3);
    // The items come out one by one: « Suivant » after each, then the empty box; never a count to show.
    gifts::Unwrapping u;
    u.items = gifts::revealed(list[1]);
    CHECK(u.current() && u.current()->id == "suit_or");
    CHECK(u.advance() && u.current()->id == "trail_comet");
    CHECK(u.advance() && u.current()->id == "communaute_coeur");
    CHECK(u.advance() && u.empty && u.current() == nullptr);
    CHECK(!u.advance());
    gifts::Unwrapping one;  // a parcel of one: the same steps
    one.items = {list[1].contents[0]};
    CHECK(one.current() && one.advance() && one.empty);
    CHECK(gifts::Unwrapping{}.current() == nullptr);
}

TEST_CASE("gifts: the message as the server keeps it, the names of the kinds and wraps") {
    CHECK(gifts::textLength("Merci d'avoir été là") == 20);
    CHECK(gifts::cleanMessage("  a\tb​c  ") == "abc");
    CHECK(gifts::cutText("éééé", 2) == "éé");
    CHECK(gifts::textLength(gifts::cutText(std::string(200, 'x'), gifts::kMaxMessage)) == 140);
    CHECK(gifts::wrapIds().size() == static_cast<size_t>(gifts::kWrapCount));
    CHECK(gifts::wrapIndex("asterion") == 10);
    CHECK(gifts::wrapIndex("papier") == -1);
    CHECK(gifts::wrapName("amethyste") == "Améthyste");
    CHECK(gifts::kindName("badge") == "Badge de profil");
    CHECK(gifts::rewardKinds().size() == 6);
}

TEST_CASE("gifts: the wraps' sheet is the generator's, with the game's ids and lights") {
    const fs::path manifest = Paths::assetsDir() / "ui" / "gifts" / "wraps.json";
    const auto loaded = FileUtil::loadJson(manifest);
    CHECK(loaded.has_value());
    const Json j = loaded.value_or(Json::object());
    CHECK(j.is_object());
    CHECK(jget<int>(j, "cell", 0) == gifts::kWrapCell);
    CHECK(jget<int>(j, "lid_dy", 0) == gifts::kLidDy);
    CHECK(jstr(j, "texture") == gifts::kWrapsTexture);
    const Json& wraps = jfield(j, "wraps", Json::array());
    CHECK(wraps.is_array() && wraps.size() == static_cast<size_t>(gifts::kWrapCount));
    for (size_t i = 0; i < wraps.size() && i < gifts::wrapIds().size(); ++i) {
        CHECK(jstr(wraps[i], "id") == gifts::wrapIds()[i]);
        const Json& rect = jfield(wraps[i], "body", Json::array());
        CHECK(rect.is_array() && rect.size() == 4 && rect[1].get<int>() == static_cast<int>(i) * gifts::kWrapCell);
        const Json& light = jfield(wraps[i], "light", Json::array());
        const Color c = gifts::wrapLight(gifts::wrapIds()[i]);
        CHECK(light.is_array() && light.size() == 3 && light[0].get<int>() == c.r && light[1].get<int>() == c.g && light[2].get<int>() == c.b);
    }
    // The parcels: the same wraps, cells and lid offset, the body's top edge where the items rise from.
    const auto parcels = FileUtil::loadJson(Paths::assetsDir() / "ui" / "gifts" / "parcels.json");
    CHECK(parcels.has_value());
    const Json pj = parcels.value_or(Json::object());
    CHECK(jstr(pj, "texture") == gifts::kParcelsTexture);
    CHECK(jget<int>(pj, "cell", 0) == gifts::kWrapCell && jget<int>(pj, "lid_dy", 0) == gifts::kLidDy);
    CHECK(jget<int>(pj, "top", 0) == gifts::kParcelTop);
    const Json& pw = jfield(pj, "wraps", Json::array());
    CHECK(pw.is_array() && pw.size() == static_cast<size_t>(gifts::kWrapCount));
    for (size_t i = 0; i < pw.size() && i < gifts::wrapIds().size(); ++i) CHECK(jstr(pw[i], "id") == gifts::wrapIds()[i]);
    if (std::system("python3 -c \"import PIL\" > " COSMIC_NULL_DEVICE " 2>&1") != 0) {
        std::printf("  (python3 + Pillow absents : vérification des emballages sautée)\n");
        return;
    }
    CHECK(std::system("python3 tools/assetgen/environment/gift_wraps.py --check > " COSMIC_NULL_DEVICE " 2>&1") == 0);
}

TEST_CASE("gifts: layouts measured, nothing overlaps (1280×720, 1920×1080, FR / EN)") {
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
        // What may be given: real items of the catalogue (never a default nor a souvenir), the badges the server grants.
        for (const std::string& kind : gifts::rewardKinds()) {
            const auto items = AdminGifts::catalogue(g, kind);
            CHECK(!items.empty());
            for (const auto& r : items) {
                if (kind == "badge") {
                    const AchievementDef* a = g.achievementCatalog.find(r.id);
                    CHECK(a && a->condition.type == "raid_reward");
                } else {
                    const CosmeticItem* it = g.cosmetics.find(r.id);
                    CHECK(it && it->category == kind && !it->isDefault && it->souvenir.empty());
                }
            }
        }
        const std::string longName = "Capitaine_Nébuleuse_du_Cœur";
        const std::string longMessage =
            "Merci d'avoir été là pour la chute d'Asterion, pendant les tests des raids et tous les soirs de la semaine, sans jamais rien lâcher !!";
        CHECK(gifts::textLength(longMessage) <= gifts::kMaxMessage);
        for (const Language lang : {Language::French, Language::English}) {
            i18n::setLanguage(lang);
            if (lang == Language::English) {
                CHECK(i18n::tr("Ouvrir le cadeau") == "Open the gift");
                CHECK(i18n::tr("Plus tard") == "Later");
                CHECK(i18n::tr("Vous avez reçu 3 cadeaux") == "You received 3 gifts");
            }
            for (const Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}}) {
                app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
                app.renderer().refreshOutputSize();
                app.runFrames(1);
                app.renderer().refreshOutputSize();
                UIContext& ui = g.ui;
                ui.begin(app, 1.0f / 60.0f);
                const float W = ui.screenW(), H = ui.screenH();
                CHECK(W == size.x && H == size.y);
                const Rect screen{0.0f, 0.0f, W, H};
                // The columns of COSMIC ONLINE › CADEAUX.
                const giftsui::ScreenLayout S = giftsui::screenLayout(ui);
                for (const Rect& r : {S.nav, S.list, S.detail, S.hint}) CHECK(inside(screen, r));
                CHECK(apart(S.nav, S.list));
                CHECK(apart(S.list, S.detail));
                CHECK(apart(S.list, S.hint) && apart(S.detail, S.hint));
                CHECK(S.list.w > ui.s(560));
                // The navigation with CADEAUX (8 items) above the bottom of its column.
                CHECK(S.nav.y + ui.s(8 * 58 + 20) <= S.nav.bottom());
                const giftsui::ListHead Hd = giftsui::listHead(ui, S.list, "12 à ouvrir · 128 reçus");
                CHECK(inside(S.list, Hd.title.box) && inside(S.list, Hd.count.box) && inside(S.list, Hd.tabs) && inside(S.list, Hd.rows));
                CHECK(apart(Hd.title.box, Hd.count.box));
                CHECK(fits(ui, Hd.title, FontFace::Bold, Hd.title.box.w));
                CHECK(fits(ui, Hd.count, FontFace::Body, Hd.count.box.w));
                CHECK(Hd.tabs.bottom() <= Hd.rows.y);
                CHECK(Hd.rows.h > ui.s(giftsui::kRowH) * 3.0f);
                // A row at its longest: the title, the line under it and the state never under one another.
                const Rect row{Hd.rows.x, Hd.rows.y, Hd.rows.w - ui.s(16), ui.s(giftsui::kRowH)};
                for (const int kind : {0, 1, 2, 3}) {  // a gift wrapped, opened; a parcel wrapped, opened
                    const bool opened = kind % 2 == 1, parcel = kind >= 2;
                    const std::string title = parcel ? "Colis de " + longName : opened ? "Couronne solaire d'Asterion le Dévoreur" : "Cadeau de " + longName;
                    const std::string sub = parcel   ? (opened ? "Colis ouvert · 20/11/2026" : "Contenu : surprise · 20/11/2026")
                                            : opened ? "Effet de victoire · de " + longName + " · 20/11/2026"
                                                     : "Contenu : surprise · 20/11/2026";
                    const giftsui::RowLayout R = giftsui::rowLayout(ui, row, title, sub, opened ? "Ouvert" : "À ouvrir", false);
                    for (const Rect& r : {R.icon, R.status, R.title.box, R.sub.box}) CHECK(inside(row, r));
                    CHECK(apart(R.icon, R.title.box) && apart(R.title.box, R.status) && apart(R.sub.box, R.status) && apart(R.title.box, R.sub.box));
                    CHECK(fits(ui, R.title, FontFace::Bold, R.title.box.w));
                    CHECK(fits(ui, R.sub, FontFace::Body, R.sub.box.w));
                    CHECK(fits(ui, R.statusText, FontFace::Bold, R.status.w - ui.s(10)));
                }
                // The detail: wrapped (one button) and opened (two), the longest message; a parcel wrapped and opened.
                for (const int kind : {0, 1, 2, 3}) {
                    const bool opened = kind % 2 == 1, parcel = kind >= 2;
                    giftsui::DetailTexts T;
                    T.kind = parcel ? "Colis · emballage Améthyste" : opened ? "Effet de victoire" : "Cadeau · emballage Améthyste";
                    T.name = parcel ? (opened ? "Colis de " + longName : "Que contient-il ?") : opened ? "Couronne solaire d'Asterion le Dévoreur" : "Que contient-il ?";
                    T.from = "De " + longName + " · 20/11/2026";
                    T.message = "« " + longMessage + " »";
                    T.actions = parcel   ? std::vector<std::string>{opened ? "Revoir l'ouverture" : "Ouvrir le colis"}
                                : opened ? std::vector<std::string>{"Badges du profil complets", "Revoir l'ouverture"}
                                         : std::vector<std::string>{"Ouvrir le cadeau"};
                    T.note = opened   ? "Retirez d'abord un badge du profil (Collection › Badges du profil)."
                             : parcel ? "Hors ligne : le colis vous attend, il s'ouvrira une fois connecté."
                                      : "Hors ligne : le cadeau vous attend, il s'ouvrira une fois connecté.";
                    const giftsui::DetailLayout D = giftsui::detailLayout(ui, S.detail, T);
                    std::vector<Rect> blocks = {D.kind.box, D.art, D.name.box, D.from.box, D.message, D.note.box};
                    for (const Rect& r : D.actions) blocks.push_back(r);
                    for (const Rect& r : blocks) CHECK(inside(S.detail, r));
                    for (size_t i = 0; i < blocks.size(); ++i)
                        for (size_t k = i + 1; k < blocks.size(); ++k) CHECK(apart(blocks[i], blocks[k]));
                    CHECK(D.art.h > ui.s(220));
                    CHECK(fits(ui, D.kind, FontFace::Bold, D.kind.box.w));
                    CHECK(fits(ui, D.name, FontFace::Bold, D.name.box.w));
                    CHECK(fits(ui, D.from, FontFace::Body, D.from.box.w));
                    CHECK(fits(ui, D.note, FontFace::Body, D.note.box.w));
                    CHECK(!D.messageLines.empty() && D.messageLines.size() <= 4);
                    {
                        i18n::Verbatim v(true);
                        for (const auto& l : D.messageLines) CHECK(ui.text().measure(l, FontFace::Body, ui.s(D.messageSize)).x <= D.message.w - ui.s(32) + 0.5f);
                    }
                    CHECK(static_cast<float>(D.messageLines.size()) * D.messageLineH <= D.message.h - ui.s(18));
                    for (size_t i = 0; i < D.actions.size(); ++i)
                        CHECK(ui.text().measure(T.actions[i], FontFace::Bold, ui.s(ui.fitFont(T.actions[i], FontFace::Bold, D.actionSize, D.actions[i].w - ui.s(20)))).x <=
                              D.actions[i].w - ui.s(20) + 0.5f);
                    if (!(parcel && opened)) continue;
                    // An opened parcel's contents in the art's room: at least two rows show, each row's icon, name,
                    // kind and « Équiper » apart and inside it (the longest names and labels).
                    CHECK(D.art.h >= 2.0f * ui.s(giftsui::kContentRowH) + ui.s(giftsui::kContentRowGap));
                    const Rect crow{D.art.x, D.art.y, D.art.w - ui.s(16), ui.s(giftsui::kContentRowH)};
                    for (const std::string& equip : {std::string("Badges du profil complets"), std::string("Équiper"), std::string("Équipé")}) {
                        const giftsui::ContentRowLayout C =
                            giftsui::contentRowLayout(ui, crow, "Couronne solaire d'Asterion le Dévoreur", "Effet de victoire", equip);
                        for (const Rect& r : {C.icon, C.equip, C.name.box, C.kind.box}) CHECK(inside(crow, r));
                        CHECK(apart(C.icon, C.name.box) && apart(C.name.box, C.equip) && apart(C.kind.box, C.equip) && apart(C.name.box, C.kind.box));
                        CHECK(fits(ui, C.name, FontFace::Bold, C.name.box.w));
                        CHECK(fits(ui, C.kind, FontFace::Body, C.kind.box.w));
                        CHECK(ui.text().measure(equip, FontFace::Bold, ui.s(ui.fitFont(equip, FontFace::Bold, C.equipSize, C.equip.w - ui.s(20)))).x <=
                              C.equip.w - ui.s(20) + 0.5f);
                    }
                }
                // The opening's last texts and its buttons, above the line of the keys: a gift, the preview, a
                // parcel's item (« Suivant »), its empty box, the preview of a parcel.
                for (const int step : {0, 1, 2, 3, 4}) {
                    const bool preview = step == 1 || step == 4, empty = step == 3;
                    giftsui::RevealTexts T;
                    T.kind = empty ? "Colis · emballage Améthyste" : preview ? "Effet de victoire · aperçu" : "Effet de victoire";
                    T.name = empty ? "Le colis est vide" : "Couronne solaire d'Asterion le Dévoreur";
                    T.from = preview ? "De " + longName + " · ce que verront les joueurs"
                             : empty ? "De " + longName + " · 20/11/2026 · tout est dans votre collection"
                                     : "De " + longName + " · 20/11/2026 · ajouté à votre collection";
                    T.message = "« " + longMessage + " »";
                    T.buttons = step == 0   ? std::vector<std::string>{"Badges du profil complets", "Cadeau suivant (12)", "Plus tard"}
                                : step == 1 ? std::vector<std::string>{"Retour à l'envoi"}
                                : step == 2 ? std::vector<std::string>{"Badges du profil complets", "Suivant", "Plus tard"}
                                : step == 3 ? std::vector<std::string>{"Cadeau suivant (12)", "Fermer"}
                                            : std::vector<std::string>{"Suivant", "Retour à l'envoi"};
                    const giftsui::RevealLayout R = giftsui::revealLayout(ui, T);
                    std::vector<Rect> blocks = {R.kind.box, R.name.box, R.from.box, R.hint};
                    if (!R.messageLines.empty())
                        blocks.push_back({R.name.box.x, R.messageY, R.name.box.w, static_cast<float>(R.messageLines.size()) * R.messageLineH});
                    for (const Rect& r : R.buttons) blocks.push_back(r);
                    for (const Rect& r : blocks) CHECK(inside(screen, r));
                    for (size_t i = 0; i < blocks.size(); ++i)
                        for (size_t k = i + 1; k < blocks.size(); ++k) CHECK(apart(blocks[i], blocks[k]));
                    CHECK(R.buttons.size() == T.buttons.size());
                    CHECK(fits(ui, R.kind, FontFace::Bold, R.kind.box.w));
                    CHECK(fits(ui, R.name, FontFace::Bold, R.name.box.w));
                    CHECK(fits(ui, R.from, FontFace::Body, R.from.box.w));
                    CHECK(R.messageLines.size() <= 2);
                    // The item rises above the texts: its halo's centre and the box stay over the first text.
                    CHECK(R.itemY < R.kind.box.y);
                    for (size_t i = 0; i < R.buttons.size(); ++i)
                        CHECK(ui.text().measure(T.buttons[i], FontFace::Bold, ui.s(ui.fitFont(T.buttons[i], FontFace::Bold, R.buttonSize, R.buttons[i].w - ui.s(20)))).x <=
                              R.buttons[i].w - ui.s(20) + 0.5f);
                }
                // ADMINISTRATION › Cadeaux: three columns side by side in the body the administration gives the tab.
                const float adminW = std::min(W - ui.s(96), ui.s(1760));
                const Rect inner{(W - adminW) * 0.5f + ui.s(22), ui.s(180), adminW - ui.s(44), H - ui.s(180) - ui.s(100)};
                const giftsui::AdminLayout A = giftsui::adminLayout(ui, inner);
                for (const Rect& r : {A.rewards, A.players, A.send}) CHECK(inside(inner, r));
                CHECK(apart(A.rewards, A.players) && apart(A.players, A.send));
                CHECK(A.rewards.w > ui.s(400) && A.players.w > ui.s(360) && A.send.w > ui.s(360));
                // The grid: three cells (or four) each wide enough for a preview; the wraps in two rows.
                const float cw = (A.rewards.w - ui.s(16) - ui.s(8) * static_cast<float>(A.itemColumns - 1)) / static_cast<float>(A.itemColumns);
                CHECK(cw > ui.s(110));
                const float ww = (A.rewards.w - ui.s(6) * 5.0f) / 6.0f;
                CHECK(ww > ui.s(70));
                CHECK(ui.text().measure("Améthyste", FontFace::Bold, ui.s(12)).x <= ww - ui.s(6));
                ui.end();
            }
        }
        i18n::setLanguage(Language::French);
    }
    app.shutdown();
}
