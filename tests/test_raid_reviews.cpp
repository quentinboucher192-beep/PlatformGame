// 2.77.4: the players' reviews of a raid event (game/online/RaidReviews, ui/online/RaidReviewsPanel,
// server/src/raidReviews.ts). The game offers « Noter ce raid » only on the server's verdict (a seat and
// a validated contribution: `can_review`), « Modifier mon avis » once reviewed, nothing to a player who
// did not play; the comment's length as the server counts it; and the layout measured at 1280×720 and
// 1920×1080, in French and in English: no text over another nor out of its room (the cards of the
// event's sheet at the width the sheet gives them, the « Noter ce raid » window, the summary of a card).

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "engine/core/Scene.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/online/RaidReviews.h"
#include "tests/TestFramework.h"
#include "ui/online/RaidReviewsPanel.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

Json reviewJson(const std::string& id, int stars, const std::string& body, bool mine = false) {
    return Json{{"id", id},
                {"author", {{"id", "p-" + id}, {"username", mine ? "Moi" : "Capitaine_Nébuleuse"}}},
                {"stars", stars},
                {"body", body},
                {"has_body", !body.empty()},
                {"hidden", false},
                {"hidden_reason", nullptr},
                {"created_at", "2026-11-21T20:00:00.000Z"},
                {"edited_at", nullptr},
                {"mine", mine},
                {"reported", false},
                {"can_report", !mine && !body.empty()}};
}

Json pageJson(bool signedIn, bool played, const Json& mine) {
    Json data = {{"summary", {{"average", 3.7}, {"count", 3}}},
                 {"reviews", Json::array({reviewJson("r2", 5, "Un très bon raid."), reviewJson("r3", 2, "")})},
                 {"total", 2}};
    data["me"] = signedIn ? Json{{"played", played}, {"can_review", played}, {"review", mine}} : Json();
    return data;
}

struct Blank : Scene {
    void render(float) override {}
};

bool inside(const Rect& outer, const Rect& in) { return in.w <= 0.0f || outer.expanded(0.5f).containsRect(in); }
bool apart(const Rect& a, const Rect& b) {
    if (a.w <= 0.0f || b.w <= 0.0f) return true;
    return !a.expanded(-0.25f).intersects(b.expanded(-0.25f));
}

}  // namespace

TEST_CASE("2.77.4 raid reviews: « Noter ce raid » only on the server's verdict, « Modifier mon avis » once reviewed") {
    using namespace raidreviews;
    // Not signed in, or signed in without having played: no button (the reason said instead).
    Page anon = parsePage(pageJson(false, false, Json()), Json());
    CHECK(anon.valid && !anon.signedIn);
    CHECK(offerOf(anon) == Offer::None);
    CHECK(noOfferNote(anon, false) == "Connectez-vous pour noter un raid que vous avez joué.");
    Page outsider = parsePage(pageJson(true, false, Json()), Json());
    CHECK(outsider.signedIn && !outsider.played);
    CHECK(offerOf(outsider) == Offer::None);
    CHECK(offerLabel(offerOf(outsider)).empty());
    CHECK(noOfferNote(outsider, true) == "Seuls les joueurs qui ont joué cet événement peuvent le noter.");
    // « played » without « can_review » (an inconsistent answer): still nothing; the server decides.
    Json odd = pageJson(true, true, Json());
    odd["me"]["can_review"] = false;
    CHECK(offerOf(parsePage(odd, Json())) == Offer::None);
    // Played, not reviewed yet: « Noter ce raid ».
    Page player = parsePage(pageJson(true, true, Json()), Json{{"next_offset", 20}});
    CHECK(offerOf(player) == Offer::Rate);
    CHECK(offerLabel(Offer::Rate) == "Noter ce raid");
    CHECK(noOfferNote(player, true).empty());
    CHECK(player.nextOffset == 20);
    CHECK(player.reviews.size() == 2);
    CHECK(player.reviews[0].canReport && !player.reviews[1].canReport);  // no comment: nothing to report
    // Already reviewed (« déjà noté »): « Modifier mon avis », the review apart and first.
    Page done = parsePage(pageJson(true, true, reviewJson("r1", 4, "Belle coordination.", true)), Json());
    CHECK(done.hasMine && done.mine.mine && done.mine.stars == 4 && done.mine.body == "Belle coordination.");
    CHECK(offerOf(done) == Offer::Edit);
    CHECK(offerLabel(Offer::Edit) == "Modifier mon avis");
    CHECK(done.reviews.size() == 2);
    // The administrators' list has no « me »: never an offer there.
    Json adminData = pageJson(false, false, Json());
    adminData.erase("me");
    CHECK(offerOf(parsePage(adminData, Json())) == Offer::None);
    // An answer without its summary (an older server): nothing shown, nothing offered.
    CHECK(!parsePage(Json{{"reviews", Json::array()}}, Json()).valid);
    CHECK(offerOf(parsePage(Json{{"me", {{"played", true}, {"can_review", true}}}}, Json())) == Offer::None);
    // A review the server could not have sent (no id, stars out of 1..5) is left out.
    CHECK(!parseReview(Json{{"id", "x"}, {"stars", 6}}).valid);
    CHECK(!parseReview(Json{{"stars", 3}}).valid);
    CHECK(parseReview(reviewJson("ok", 1, "")).valid);
}

TEST_CASE("2.77.4 raid reviews: the average, the count, the comment's length as the server counts it") {
    using namespace raidreviews;
    Summary s = parseSummary(Json{{"average", 4.2}, {"count", 12}});
    CHECK(s.known && s.count == 12 && std::abs(s.average - 4.2) < 1e-9);
    CHECK(summaryText(s) == "4,2 · 12 avis");
    CHECK(summaryText(parseSummary(Json{{"average", 5}, {"count", 1}})) == "5,0 · 1 avis");
    CHECK(summaryText(parseSummary(Json{{"average", nullptr}, {"count", 0}})) == "Aucun avis");
    CHECK(summaryText(parseSummary(Json())).empty());  // not sent: nothing
    CHECK(summaryText(parseSummary(Json{{"average", 3.0}, {"count", -1}})).empty());
    CHECK(averageText(3.75) == "3,8");
    CHECK(averageText(1.0) == "1,0");
    // UTF-16 units, as JavaScript's length: accents count one, an emoji two.
    CHECK(textLength("abc") == 3);
    CHECK(textLength("été") == 3);
    CHECK(textLength("\xF0\x9F\x9A\x80") == 2);  // 🚀
    CHECK(trimmed("  bonjour \n") == "bonjour");
    std::string e500;
    for (int i = 0; i < kMaxText; ++i) e500 += "é";
    CHECK(problemOf(4, e500).empty());
    CHECK(problemOf(4, std::string(kMaxText, 'a') + "  ").empty());  // the spaces at the ends are not kept
    CHECK(problemOf(4, std::string(kMaxText + 1, 'a')) == "Commentaire trop long : 501 caractères pour 500 au plus.");
    CHECK(problemOf(0, "") == "Choisissez une note de 1 à 5 étoiles.");
    CHECK(problemOf(6, "") == "Choisissez une note de 1 à 5 étoiles.");
    CHECK(counterText("  abc ") == "3 / 500");
    CHECK(requestBody(5, "  Super !  ") == Json({{"stars", 5}, {"body", "Super !"}}));
    CHECK(requestBody(3, "") == Json({{"stars", 3}, {"body", ""}}));
    CHECK(starsWord(1) == "Décevant" && starsWord(5) == "Excellent" && starsWord(0).empty());
    // A masked comment: its author is told why, the others only that it is masked.
    Review r = parseReview(reviewJson("h", 1, "", false));
    r.hidden = true;
    r.hiddenReason = "reports";
    CHECK(hiddenNote(r) == "Commentaire masqué après des signalements, en attendant la décision des modérateurs.");
    r.mine = true;
    r.hiddenReason = "moderation";
    CHECK(hiddenNote(r) == "Votre commentaire a été masqué par la modération (vous seul le voyez). Votre note compte toujours.");
}

TEST_CASE("2.77.4 raid reviews layout: cards, summary and « Noter ce raid » measured, nothing overlaps (1280×720, 1920×1080, FR / EN)") {
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
        // The reviews at their longest: 500 characters of words, one long word, a masked one, reports.
        std::string words;
        while (raidreviews::textLength(words) < 490) words += "Coordination parfaite du groupe trois, ";
        words = words.substr(0, 500);
        raidreviews::Review longOne = raidreviews::parseReview(reviewJson("a", 4, words));
        longOne.editedAt = longOne.createdAt + 3600;
        longOne.author = "Capitaine_Nébuleuse_du_Cœur";
        raidreviews::Review mine = raidreviews::parseReview(reviewJson("b", 2, std::string(120, 'W'), true));
        mine.hidden = true;
        mine.hiddenReason = "reports";
        raidreviews::Review starsOnly = raidreviews::parseReview(reviewJson("c", 5, ""));
        raidreviews::Review reported = raidreviews::parseReview(reviewJson("d", 1, "Texte signalé"));
        reported.reported = true;
        reported.canReport = false;
        raidreviews::Review flagged = reported;
        flagged.hidden = true;
        flagged.hiddenByReports = true;
        flagged.reportCount = 3;
        flagged.reasons = {"harassment", "harassment", "spam"};
        const std::vector<raidreviews::Review> all = {longOne, mine, starsOnly, reported, flagged};

        for (const Language lang : {Language::French, Language::English}) {
            i18n::setLanguage(lang);
            if (lang == Language::English) {
                CHECK(i18n::tr("4,2 · 12 avis") == "4.2 · 12 reviews");
                CHECK(i18n::tr("Noter ce raid") == "Rate this raid");
                CHECK(i18n::tr("1 avis") == "1 review");
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
                // The column RaidEventScene gives the reviews (drawLeft: 40 % of the sheet's left part),
                // the panel's list inside it (14 of margin, 6 more for the list, 14 for its scroll bar);
                // and the administrators' column (AdminRaids: what the form leaves).
                const float m = ui.s(28), asideW = std::min(ui.s(520), W * 0.34f);
                const float leftW = (W - m - asideW - ui.s(18)) - m;
                const float column = (leftW - 2 * ui.s(12)) * 0.4f;
                const float cardW = (column - ui.s(28) + ui.s(6)) - ui.s(14);
                const float adminIn = std::min(W - ui.s(96), ui.s(1760)) - ui.s(44);  // AdminScene: the body's inner rect
                const float adminW = (adminIn - std::min(ui.s(600), adminIn * 0.44f) - ui.s(18)) - ui.s(28) + ui.s(6) - ui.s(14);
                for (const bool admin : {false, true}) {
                    const float w = admin ? adminW : cardW;
                    CHECK(w > ui.s(300));
                    for (const raidreviews::Review& r : all) {
                        const raidreviewsui::ItemLayout L = raidreviewsui::layoutItem(ui, r, ui.s(40), ui.s(60), w, admin, true);
                        const Rect inner = L.card.expanded(-ui.s(12));
                        CHECK(L.h > ui.s(60));
                        CHECK(inside(inner, L.stars));
                        CHECK(inside(inner, L.name));
                        CHECK(apart(L.stars, L.name));
                        CHECK(inside(inner, L.action));
                        CHECK(inside(inner, L.date));
                        CHECK(apart(L.action, L.date));
                        // The author's name at its size or smaller, shortened, within its room (as raidui::fit draws it).
                        {
                            i18n::Verbatim name(L.nameText != "Votre avis");
                            const float f = ui.fitFont(L.nameText, FontFace::Bold, 15, L.name.w, 0.75f);
                            CHECK(ui.text().measure(ui.elide(L.nameText, FontFace::Bold, f, L.name.w), FontFace::Bold, ui.s(f)).x <= L.name.w + 0.5f);
                        }
                        // The text, the masked note, the reports: lines within the card, one block after the other.
                        float bottom = L.stars.bottom();
                        auto block = [&](const std::vector<std::string>& lines, float y, float lineH, float sizePt, bool verbatim) {
                            if (lines.empty()) return;
                            CHECK(y >= bottom - 0.5f);
                            i18n::Verbatim v(true);  // wrap() gave them translated (or as the player wrote them)
                            for (const std::string& l : lines) CHECK(ui.text().measure(l, FontFace::Body, ui.s(sizePt)).x <= inner.w + 0.5f);
                            bottom = y + static_cast<float>(lines.size()) * lineH;
                            CHECK(bottom <= inner.bottom() + 0.5f);
                            (void)verbatim;
                        };
                        block(L.body, L.bodyY, L.bodyLineH, 15, true);
                        block(L.note, L.noteY, L.noteLineH, 13, false);
                        block(L.admin, L.adminY, L.noteLineH, 13, false);
                        const float lastRow = std::max(L.action.w > 0 ? L.action.y : 0.0f, L.date.w > 0 ? L.date.y : 0.0f);
                        if (lastRow > 0.0f) CHECK(lastRow >= bottom - 0.5f);
                        if (r.mine && !admin) CHECK(L.actionKind == "edit");
                        if (admin) CHECK(L.actionKind == (r.hasBody ? (r.hidden ? "restore" : "hide") : ""));
                        if (!admin && !r.mine && r.reported) CHECK(L.actionKind == "reported");
                        if (!admin && r.id == "a") CHECK(L.actionKind == "report");
                        if (!admin && r.id == "c") CHECK(L.actionKind.empty());  // stars only: nothing to report
                        if (!admin && r.id == "a") CHECK(L.body.size() >= 4);       // the whole comment, wrapped
                    }
                    // Without an account: no action at all.
                    const raidreviewsui::ItemLayout anon = raidreviewsui::layoutItem(ui, longOne, 0, 0, w, false, false);
                    CHECK(anon.actionLabel.empty() && anon.action.w == 0.0f);
                }
                // The summary of the panel's header and of a card's line: within its room, the title before it.
                for (const raidreviews::Summary& s : {raidreviews::parseSummary(Json{{"average", 4.2}, {"count", 1234}}),
                                                      raidreviews::parseSummary(Json{{"average", nullptr}, {"count", 0}})}) {
                    const float x = ui.s(100), w = column - ui.s(28);
                    const raidreviewsui::SummaryLayout S = raidreviewsui::layoutSummary(ui, x + w, ui.s(40), s, 15, w * 0.55f, true);
                    CHECK(!S.shown.empty());
                    CHECK(S.text.right() <= x + w + 0.5f);
                    CHECK(S.star.x >= x + w - w * 0.55f - 0.5f);
                    CHECK(apart(S.star, S.text));
                    CHECK(ui.text().measure(S.shown, FontFace::Bold, ui.s(S.size)).x <= S.text.w + 0.5f);
                    const float titleRoom = w - S.width() - ui.s(12);
                    CHECK(ui.text().measure("AVIS DES JOUEURS", FontFace::Title, ui.s(15)).x * 0.75f <= titleRoom + 0.5f);
                    raidreviewsui::drawSummary(ui, S, Color{255, 210, 122, 255}, Color::white());
                    // A history row (CLANS › RAIDS): a narrow room, smaller then shortened, never out of it.
                    const raidreviewsui::SummaryLayout tight = raidreviewsui::layoutSummary(ui, ui.s(300), ui.s(80), s, 13, ui.s(120), true);
                    if (!tight.shown.empty()) CHECK(tight.star.x >= ui.s(180) - 0.5f && tight.text.right() <= ui.s(300) + 0.5f);
                }
                raidreviewsui::drawStars(ui, ui.s(40), ui.s(40), 16, 3.5, Color{255, 210, 122, 255}, Color{70, 78, 128, 255});
                // « Noter ce raid » / « Modifier mon avis »: inside the screen, each piece in its place.
                for (const bool editing : {false, true}) {
                    const raidreviewsui::EditorLayout E = raidreviewsui::layoutEditor(ui, editing, editing ? 4 : 0, words);
                    CHECK(inside(screen, E.box));
                    const Rect inner = E.box.expanded(-ui.s(20));
                    for (const Rect& r : {E.title, E.caption, E.word, E.field, E.counter, E.captionText, E.preview, E.problem, E.cancel, E.ok}) CHECK(inside(inner, r));
                    CHECK(E.stars.size() == 5);
                    for (size_t i = 0; i < E.stars.size(); ++i) {
                        CHECK(inside(inner, E.stars[i]));
                        CHECK(E.stars[i].w >= ui.s(44));  // a target for the mouse
                        if (i > 0) CHECK(apart(E.stars[i - 1], E.stars[i]));
                        CHECK(apart(E.stars[i], E.word));
                    }
                    CHECK(apart(E.captionText, E.counter));
                    CHECK(E.title.bottom() <= E.introY + 0.5f);
                    CHECK(E.introY + static_cast<float>(E.intro.size()) * E.introLineH <= E.caption.y + 0.5f);
                    CHECK(E.caption.bottom() <= E.stars[0].y + 0.5f);
                    CHECK(E.stars[0].bottom() <= E.captionText.y + 0.5f);
                    CHECK(E.captionText.bottom() <= E.field.y + 0.5f);
                    CHECK(E.field.bottom() <= E.preview.y + 0.5f);
                    CHECK(E.preview.bottom() <= E.problem.y + 0.5f);
                    CHECK(E.problem.bottom() <= E.ok.y + 0.5f);
                    CHECK(E.previewLines.size() <= 4);
                    {
                        i18n::Verbatim v(true);
                        for (const std::string& l : E.previewLines) CHECK(ui.text().measure(l, FontFace::Body, ui.s(15)).x <= E.preview.w - ui.s(24) + 0.5f);
                        for (const std::string& l : E.intro) CHECK(ui.text().measure(l, FontFace::Body, ui.s(16)).x <= inner.w + 0.5f);
                    }
                    CHECK(apart(E.cancel, E.ok));
                    if (editing) {
                        CHECK(inside(inner, E.remove));
                        CHECK(apart(E.remove, E.cancel));
                    } else CHECK(E.remove.w == 0.0f);
                    // The buttons' labels at their size, within the button (UIContext::button keeps 20 of margin).
                    CHECK(ui.text().measure(E.okLabel, FontFace::Bold, ui.s(17)).x <= E.ok.w - ui.s(20) + 0.5f);
                    CHECK(ui.text().measure("Annuler", FontFace::Bold, ui.s(17)).x <= E.cancel.w - ui.s(20) + 0.5f);
                    if (editing) CHECK(ui.text().measure("Supprimer mon avis", FontFace::Bold, ui.s(17)).x <= E.remove.w - ui.s(20) + 0.5f);
                }
                CHECK(!ui.hasFocus());
                ui.end();
            }
        }
        i18n::setLanguage(Language::French);
    }
    app.shutdown();
}
