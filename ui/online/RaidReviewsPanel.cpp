#include "ui/online/RaidReviewsPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/online/Raids.h"
#include "ui/online/RaidWidgets.h"

namespace cosmic {

using namespace raidui;
using raidreviews::Review;

namespace {

const Color kStarOn{255, 210, 122, 255};
const Color kStarOff{70, 78, 128, 255};
const Color kMineCard{42, 42, 90, 250};
const Color kNote{255, 196, 92, 255};

// The comments' report reasons (server: COMMENT_REPORT_REASONS) and their labels (CommentsPanel's).
const char* const kReasons[] = {"spam", "harassment", "inappropriate", "spoiler", "other"};
const std::vector<std::string> kReasonLabels = {"Spam ou publicité", "Harcèlement ou insultes", "Contenu inapproprié", "Divulgue la solution", "Autre"};

std::string reasonWord(const std::string& code) {
    if (code == "spam") return "spam";
    if (code == "harassment") return "harcèlement";
    if (code == "inappropriate") return "contenu inapproprié";
    if (code == "spoiler") return "divulgation";
    return "autre";
}

/// « 3 signalements : harcèlement ×2, spam » (the administrators' list).
std::string reportsLine(const Review& r) {
    if (r.reportCount <= 0) return "Aucun signalement";
    std::map<std::string, int> by;
    for (const std::string& x : r.reasons) ++by[reasonWord(x)];
    std::string list;
    for (const auto& [word, n] : by) list += (list.empty() ? "" : ", ") + word + (n > 1 ? " ×" + std::to_string(n) : std::string());
    std::string head = std::to_string(r.reportCount) + (r.reportCount > 1 ? " signalements" : " signalement");
    return list.empty() ? head : head + " : " + list;
}

std::string adminNote(const Review& r) {
    if (!r.hidden) return "";
    return r.hiddenByReports ? "Masqué après des signalements : rétablissez-le ou laissez-le masqué." : "Masqué par un administrateur. Sa note compte toujours.";
}

std::string dateOf(const Review& r) {
    if (r.createdAt <= 0) return "";
    std::string d = raids::dateShort(raids::toSystemLocal(r.createdAt));
    if (r.editedAt > 0) d += " · modifié";
    return d;
}

}  // namespace

// ------------------------------------------------------------------ drawing pieces

namespace raidreviewsui {

void drawStar(UIContext& ui, Vec2 c, float r, float fill, Color on, Color off) {
    std::array<Vec2, 10> p;
    for (int k = 0; k < 10; ++k) {
        const float a = -kPi * 0.5f + static_cast<float>(k) * kPi / 5.0f;
        const float rr = k % 2 == 0 ? r : r * 0.45f;
        p[static_cast<size_t>(k)] = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
    }
    auto shape = [&](Color col) {
        for (size_t k = 0; k < 10; ++k) ui.r().fillTriangle(c, p[k], p[(k + 1) % 10], col);
    };
    fill = std::clamp(fill, 0.0f, 1.0f);
    if (fill < 1.0f) shape(off);
    if (fill <= 0.0f) return;
    if (fill >= 1.0f) {
        shape(on);
        return;
    }
    ui.r().pushClip({c.x - r - 1.0f, c.y - r - 1.0f, 2.0f * r * fill + 1.0f, 2.0f * r + 2.0f});
    shape(on);
    ui.r().popClip();
}

float starsWidth(UIContext& ui, float size) { return 5.0f * ui.s(size) + 4.0f * ui.s(size * 0.2f); }

float drawStars(UIContext& ui, float x, float cy, float size, double value, Color on, Color off) {
    const float d = ui.s(size), gap = ui.s(size * 0.2f);
    for (int i = 0; i < raidreviews::kMaxStars; ++i)
        drawStar(ui, {x + static_cast<float>(i) * (d + gap) + d * 0.5f, cy}, d * 0.5f, static_cast<float>(value - i), on, off);
    return starsWidth(ui, size);
}

SummaryLayout layoutSummary(UIContext& ui, float x, float cy, const raidreviews::Summary& s, float size, float maxW, bool alignRight) {
    SummaryLayout L;
    const std::string t = raidreviews::summaryText(s);
    const float star = ui.s(size), gap = ui.s(6);
    const float room = maxW - star - gap;
    if (t.empty() || room <= ui.s(24)) return L;
    L.size = ui.fitFont(t, FontFace::Bold, size, room, 0.75f);
    L.shown = ui.elide(t, FontFace::Bold, L.size, room);
    const float tw = std::min(ui.text().measure(L.shown, FontFace::Bold, ui.s(L.size)).x, room);
    const float total = star + gap + tw;
    const float left = alignRight ? x - total : x;
    L.filled = s.count > 0;
    L.star = {left, cy - star * 0.5f, star, star};
    L.text = {left + star + gap, cy - star * 0.5f, tw, star};
    return L;
}

void drawSummary(UIContext& ui, const SummaryLayout& L, Color starColor, Color textColor) {
    if (L.shown.empty()) return;
    drawStar(ui, L.star.center(), L.star.w * 0.5f, L.filled ? 1.0f : 0.0f, starColor, kStarOff);
    ui.label(L.shown, {L.text.x, L.text.center().y}, L.size, textColor, Align::Left, FontFace::Bold, VAlign::Middle);
}

ItemLayout layoutItem(UIContext& ui, const Review& r, float x, float y, float w, bool admin, bool loggedIn) {
    ItemLayout L;
    const float pad = ui.s(12), ix = x + pad, iw = w - 2 * pad;
    float cy = y + pad + ui.s(12);
    const float sw = starsWidth(ui, 16);
    L.stars = {ix, cy - ui.s(8), sw, ui.s(16)};
    L.nameText = r.mine && !admin ? "Votre avis" : r.author;
    L.name = {ix + sw + ui.s(10), cy - ui.s(12), std::max(0.0f, iw - sw - ui.s(10)), ui.s(24)};
    float yy = y + pad + ui.s(24) + ui.s(6);
    L.bodyLineH = ui.s(21);
    if (!r.body.empty()) {
        L.body = wrap(ui, r.body, FontFace::Body, 15, iw, 14, true);  // a player's words: never translated
        L.bodyY = yy;
        yy += static_cast<float>(L.body.size()) * L.bodyLineH + ui.s(6);
    }
    L.noteLineH = ui.s(18);
    const std::string note = admin ? adminNote(r) : raidreviews::hiddenNote(r);
    if (!note.empty()) {
        L.note = wrap(ui, note, FontFace::Body, 13, iw, 3);
        L.noteY = yy;
        yy += static_cast<float>(L.note.size()) * L.noteLineH + ui.s(6);
    }
    if (admin) {
        L.admin = wrap(ui, reportsLine(r), FontFace::Body, 13, iw, 2);
        L.adminY = yy;
        yy += static_cast<float>(L.admin.size()) * L.noteLineH + ui.s(6);
    }
    // The action at the left of the last line, the date in the room at its right.
    if (admin) {
        if (r.hasBody) {
            L.actionLabel = r.hidden ? "Rétablir le commentaire" : "Masquer le commentaire";
            L.actionKind = r.hidden ? "restore" : "hide";
        }
    } else if (r.mine) {
        L.actionLabel = "Modifier";
        L.actionKind = "edit";
    } else if (loggedIn && r.reported) {
        L.actionLabel = "Signalé";
        L.actionKind = "reported";
    } else if (loggedIn && r.canReport) {
        L.actionLabel = "Signaler";
        L.actionKind = "report";
    }
    const float rowH = ui.s(30);
    float dateRoom = iw;
    if (!L.actionLabel.empty()) {
        const float aw = std::min(textW(ui, L.actionLabel, 13, FontFace::Bold) + ui.s(24), iw * 0.6f);
        L.action = {ix, yy, aw, rowH};
        dateRoom = iw - aw - ui.s(12);
    }
    L.dateText = dateOf(r);
    if (!L.dateText.empty() && dateRoom > ui.s(24)) {
        const float dw = std::min(textW(ui, L.dateText, 13), dateRoom);
        L.date = {ix + iw - dw, yy, dw, rowH};
    }
    yy += rowH;
    L.h = yy + pad - y;
    L.card = {x, y, w, L.h};
    return L;
}

EditorLayout layoutEditor(UIContext& ui, bool editing, int stars, const std::string& text) {
    EditorLayout L;
    const float w = std::min(ui.screenW() - ui.s(40), ui.s(760)), tw = w - ui.s(60);
    L.titleText = editing ? "Modifier mon avis" : "Noter ce raid";
    L.okLabel = editing ? "Enregistrer" : "\x1F" "commentaire|Publier";
    L.intro = wrap(ui,
                   "Une note de 1 à 5 étoiles et, si vous le voulez, un commentaire. Un seul avis par événement : vous pourrez le modifier ou le "
                   "supprimer.",
                   FontFace::Body, 16, tw, 3);
    L.introLineH = ui.s(22);
    L.previewLineH = ui.s(21);
    // The heights first, to centre the window.
    const float introH = static_cast<float>(L.intro.size()) * L.introLineH;
    const float starD = ui.s(52), starGap = ui.s(10);
    const float previewH = 4.0f * L.previewLineH + ui.s(16);
    const float h = ui.s(26) + ui.s(36) + ui.s(10) + introH + ui.s(16) + ui.s(26) + starD + ui.s(18) + ui.s(26) + ui.s(44) + ui.s(10) + previewH + ui.s(8) +
                    ui.s(24) + ui.s(12) + ui.s(48) + ui.s(26);
    L.box = {(ui.screenW() - w) * 0.5f, std::max(ui.s(10), (ui.screenH() - h) * 0.5f), w, h};
    const float x = L.box.x + ui.s(30);
    float y = L.box.y + ui.s(26);
    L.title = {x, y, tw, ui.s(36)};
    y += ui.s(36) + ui.s(10);
    L.introY = y;
    y += introH + ui.s(16);
    L.caption = {x, y, tw, ui.s(26)};
    y += ui.s(26);
    for (int i = 0; i < raidreviews::kMaxStars; ++i) L.stars.push_back({x + static_cast<float>(i) * (starD + starGap), y, starD, starD});
    const float rowW = 5.0f * starD + 4.0f * starGap;
    L.word = {x + rowW + ui.s(16), y, std::max(0.0f, tw - rowW - ui.s(16)), starD};
    y += starD + ui.s(18);
    // The comment's caption and its counter (measured first, at the right).
    const float cw = textW(ui, "500 / 500", 14, FontFace::Bold) + ui.s(4);
    L.counter = {x + tw - cw, y, cw, ui.s(26)};
    L.captionText = {x, y, std::max(0.0f, tw - cw - ui.s(12)), ui.s(26)};
    y += ui.s(26);
    L.field = {x, y, tw, ui.s(44)};
    y += ui.s(44) + ui.s(10);
    L.preview = {x, y, tw, previewH};
    const std::string t = raidreviews::trimmed(text);
    L.previewLines = t.empty() ? std::vector<std::string>{} : wrap(ui, t, FontFace::Body, 15, tw - ui.s(24), 4, true);
    y += previewH + ui.s(8);
    L.problem = {x, y, tw, ui.s(24)};
    y += ui.s(24) + ui.s(12);
    // The buttons: « Publier » at the right, « Annuler » before it, « Supprimer mon avis » at the left.
    const float okW = textW(ui, L.okLabel, 17, FontFace::Bold) + ui.s(44);
    const float cancelW = textW(ui, "Annuler", 17, FontFace::Bold) + ui.s(44);
    const float removeW = editing ? textW(ui, "Supprimer mon avis", 17, FontFace::Bold) + ui.s(44) : 0.0f;
    const float need = okW + cancelW + ui.s(12) + (editing ? removeW + ui.s(12) : 0.0f);
    const float k = need > tw ? tw / need : 1.0f;  // a narrow window: every button narrower (its label fitted)
    L.ok = {x + tw - okW * k, y, okW * k, ui.s(48)};
    L.cancel = {L.ok.x - ui.s(12) * k - cancelW * k, y, cancelW * k, ui.s(48)};
    if (editing) L.remove = {x, y, removeW * k, ui.s(48)};
    (void)stars;
    return L;
}

}  // namespace raidreviewsui

// ------------------------------------------------------------------ the panel

RaidReviewsPanel::RaidReviewsPanel(GameServices& g, Mode mode) : g_(g), mode_(mode) {}

raidreviews::Offer RaidReviewsPanel::offer() const { return mode_ == Mode::Admin || !loaded_ ? raidreviews::Offer::None : raidreviews::offerOf(page_); }

void RaidReviewsPanel::open(const std::string& eventId, bool force) {
    if (eventId == eventId_ && !force) return;
    if (eventId != eventId_) {
        page_ = {};
        loaded_ = false;
        error_.clear();
        modal_ = Modal::None;
    }
    eventId_ = eventId;
    load(false);
}

void RaidReviewsPanel::reload() {
    if (!eventId_.empty()) load(false);
}

void RaidReviewsPanel::load(bool more) {
    if (eventId_.empty()) return;
    const int gen = ++gen_;
    const int offset = more ? std::max(0, page_.nextOffset) : 0;
    const std::string base = mode_ == Mode::Admin ? "/api/v1/admin/raids/" + eventId_ + "/reviews" : "/api/v1/raids/events/" + eventId_ + "/reviews";
    loading_ = true;
    g_.online.get(base + "?limit=20&offset=" + std::to_string(offset), OnlineService::guard(life_, [this, gen, more](const ApiResult& r) {
                      if (gen != gen_) return;
                      loading_ = false;
                      if (!r.ok) {
                          // A server before 2.77.4 has no such route: no reviews to show, said plainly.
                          if (!loaded_) error_ = r.code == "NOT_FOUND" ? std::string("Avis indisponibles.") : r.message;
                          return;
                      }
                      raidreviews::Page p = raidreviews::parsePage(r.data, r.meta);
                      if (!p.valid) {
                          if (!loaded_) error_ = "Avis indisponibles.";
                          return;
                      }
                      if (more) {
                          for (Review& x : p.reviews) page_.reviews.push_back(std::move(x));
                          page_.nextOffset = p.nextOffset;
                          page_.summary = p.summary;
                          page_.total = p.total;
                      } else page_ = std::move(p);
                      loaded_ = true;
                      error_.clear();
                  }),
                  mode_ == Mode::Player);
}

void RaidReviewsPanel::openEditor() {
    const raidreviews::Offer o = offer();
    if (o == raidreviews::Offer::None) return;
    stars_ = o == raidreviews::Offer::Edit ? page_.mine.stars : 0;
    text_ = o == raidreviews::Offer::Edit ? page_.mine.body : std::string();
    modalError_.clear();
    modal_ = Modal::Editor;
    prevStarFocus_ = -1;
    focusStars_ = true;  // given by drawModal, under its own ids
}

void RaidReviewsPanel::submit() {
    const std::string problem = raidreviews::problemOf(stars_, text_);
    if (!problem.empty()) {
        modalError_ = problem;
        return;
    }
    busy_ = true;
    modalError_.clear();
    g_.online.send("PUT", "/api/v1/raids/events/" + eventId_ + "/review", raidreviews::requestBody(stars_, text_),
                   OnlineService::guard(life_, [this](const ApiResult& r) {
                       busy_ = false;
                       if (!r.ok) {
                           modalError_ = r.message;
                           return;
                       }
                       modal_ = Modal::None;
                       if (say) say(jget<bool>(r.data, "created", false) ? "Merci : votre avis est publié." : "Votre avis est modifié.", false);
                       reload();
                       if (changed) changed();
                   }));
}

void RaidReviewsPanel::remove() {
    busy_ = true;
    g_.online.send("DELETE", "/api/v1/raids/events/" + eventId_ + "/review", Json::object(), OnlineService::guard(life_, [this](const ApiResult& r) {
                       busy_ = false;
                       if (!r.ok) {
                           modalError_ = r.message;
                           return;
                       }
                       modal_ = Modal::None;
                       if (say) say("Votre avis est supprimé.", false);
                       reload();
                       if (changed) changed();
                   }));
}

void RaidReviewsPanel::report() {
    const std::string id = reportId_;
    busy_ = true;
    g_.online.send("POST", "/api/v1/raids/reviews/" + id + "/report", {{"reason", kReasons[std::clamp(reportReason_, 0, 4)]}},
                   OnlineService::guard(life_, [this, id](const ApiResult& r) {
                       busy_ = false;
                       if (!r.ok) {
                           modalError_ = r.message;
                           return;
                       }
                       modal_ = Modal::None;
                       if (say) say("Commentaire signalé. Merci.", false);
                       for (Review& x : page_.reviews)
                           if (x.id == id) {
                               x.reported = true;
                               x.canReport = false;
                           }
                   }));
}

void RaidReviewsPanel::moderate(const std::string& id, bool hide) {
    if (busy_) return;
    busy_ = true;
    g_.online.send("POST", "/api/v1/admin/raids/" + eventId_ + "/reviews/" + id + "/moderate", {{"action", hide ? "hide" : "restore"}},
                   OnlineService::guard(life_, [this, hide](const ApiResult& r) {
                       busy_ = false;
                       if (!r.ok) {
                           if (say) say(r.message, true);
                           return;
                       }
                       if (say)
                           say(hide ? "Commentaire masqué : sa note compte toujours (inscrit au Journal)." : "Commentaire rétabli (inscrit au Journal).", false);
                       reload();
                       if (changed) changed();
                   }));
}

float RaidReviewsPanel::itemsHeight(float w, bool loggedIn) {
    UIContext& ui = g_.ui;
    const bool admin = mode_ == Mode::Admin;
    float h = 0.0f;
    if (page_.hasMine && !admin) h += raidreviewsui::layoutItem(ui, page_.mine, 0, 0, w, false, loggedIn).h + ui.s(10);
    for (const Review& r : page_.reviews) h += raidreviewsui::layoutItem(ui, r, 0, 0, w, admin, loggedIn).h + ui.s(10);
    if (page_.reviews.empty() && !(page_.hasMine && !admin)) h += ui.s(30);
    if (page_.nextOffset > 0) h += ui.s(52);
    return h;
}

void RaidReviewsPanel::drawItem(const Review& r, const raidreviewsui::ItemLayout& L) {
    UIContext& ui = g_.ui;
    const bool mine = r.mine && mode_ == Mode::Player;
    ui.r().fillRoundedRect(L.card, ui.s(10), mine ? kMineCard : kCard);
    if (mine) ui.r().drawRoundedRect(L.card, ui.s(10), kGold.withAlphaF(0.7f), ui.s(1.2f));
    raidreviewsui::drawStars(ui, L.stars.x, L.stars.center().y, 16, r.stars, kStarOn, kStarOff);
    fit(ui, L.nameText, {L.name.x, L.name.center().y}, 15, L.name.w, mine ? kGold : kText, FontFace::Bold, Align::Left, !mine);
    if (!L.body.empty()) drawLines(ui, L.body, {L.card.x + ui.s(12), L.bodyY}, 15, L.bodyLineH, kText);
    if (!L.note.empty()) drawLines(ui, L.note, {L.card.x + ui.s(12), L.noteY}, 13, L.noteLineH, kNote);
    if (!L.admin.empty()) drawLines(ui, L.admin, {L.card.x + ui.s(12), L.adminY}, 13, L.noteLineH, r.reportCount > 0 ? kNote : kDim);
    if (!L.actionLabel.empty()) {
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = 13;
        st.danger = L.actionKind == "report" || L.actionKind == "hide";
        st.enabled = !busy_ && L.actionKind != "reported";
        if (ui.button("act/" + (r.id.empty() ? std::string("mine") : r.id), L.action, L.actionLabel, st)) {
            if (L.actionKind == "edit") openEditor();
            else if (L.actionKind == "report") {
                reportId_ = r.id;
                reportReason_ = 0;
                modalError_.clear();
                modal_ = Modal::Report;
            } else if (L.actionKind == "hide" || L.actionKind == "restore")
                moderate(r.id, L.actionKind == "hide");
        }
    }
    if (L.date.w > 0.0f) fit(ui, L.dateText, {L.date.right(), L.date.center().y}, 13, L.date.w, kDim, FontFace::Body, Align::Right);
}

void RaidReviewsPanel::draw(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.pushId("reviews");
    ui.r().fillRoundedRect(r, ui.s(14), kPanel);
    ui.r().drawRoundedRect(r, ui.s(14), kBorder, ui.s(1.2f));
    const float x = r.x + ui.s(14), w = r.w - ui.s(28);
    const bool admin = mode_ == Mode::Admin;
    const bool logged = g_.online.loggedIn();
    // The header: the average and the count at the right (measured first), the title in the room left.
    raidreviewsui::SummaryLayout S;
    if (loaded_) S = raidreviewsui::layoutSummary(ui, x + w, r.y + ui.s(24), page_.summary, 15, w * 0.55f, true);
    const float sumW = S.shown.empty() ? 0.0f : S.width() + ui.s(12);
    fit(ui, "AVIS DES JOUEURS", {x, r.y + ui.s(24)}, 15, w - sumW, kSky, FontFace::Title);
    raidreviewsui::drawSummary(ui, S, kStarOn, kText);
    float y = r.y + ui.s(46);
    if (!loaded_) {
        drawLines(ui, wrap(ui, error_.empty() ? "Chargement des avis…" : error_, FontFace::Body, 15, w, 3), {x, y}, 15, ui.s(22),
                  error_.empty() ? kDim : ui.theme().danger);
        ui.popId();
        return;
    }
    // The offer (« Noter ce raid »), or why there is none; the administrators' note.
    const raidreviews::Offer o = offer();
    if (o == raidreviews::Offer::Rate && y + ui.s(44) <= r.bottom() - ui.s(12)) {
        UIContext::ButtonStyle st;
        st.primary = true;
        st.fontSize = 17;
        st.enabled = !busy_;
        if (ui.button("rate", {x, y, w, ui.s(44)}, raidreviews::offerLabel(o), st)) openEditor();
        y += ui.s(54);
    } else {
        const std::string note = admin ? "Tous les avis de l'événement, masqués compris. Un commentaire masqué garde sa note."
                                       : raidreviews::noOfferNote(page_, logged);
        const int lines = std::max(0, std::min(2, static_cast<int>((r.bottom() - ui.s(12) - y) / ui.s(18))));
        const auto nl = wrap(ui, note, FontFace::Body, 13, w, lines);
        if (!nl.empty()) y += drawLines(ui, nl, {x, y}, 13, ui.s(18), kDim) + ui.s(8);
    }
    // The list: my review first (with « Modifier »), the others, « Plus d'avis ».
    const Rect view{x, y, w + ui.s(6), r.bottom() - ui.s(12) - y};
    if (view.h < ui.s(40)) {
        ui.popId();
        return;
    }
    const float cw = view.w - ui.s(14);
    ui.beginScroll("list", view, itemsHeight(cw, logged));
    float cy = view.y - ui.scrollOffset();
    auto item = [&](const Review& rv) {
        const raidreviewsui::ItemLayout L = raidreviewsui::layoutItem(ui, rv, view.x, cy, cw, admin, logged);
        if (L.card.bottom() >= view.y && L.card.y <= view.bottom()) drawItem(rv, L);
        cy += L.h + ui.s(10);
    };
    if (page_.hasMine && !admin) item(page_.mine);
    for (const Review& rv : page_.reviews) item(rv);
    if (page_.reviews.empty() && !(page_.hasMine && !admin))
        fit(ui, "Aucun avis pour l'instant.", {view.x + ui.s(4), cy + ui.s(12)}, 15, cw - ui.s(8), kDim);
    if (page_.nextOffset > 0) {
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = 14;
        st.enabled = !loading_;
        const float bw = std::min(cw, textW(ui, "Plus d'avis", 14, FontFace::Bold) + ui.s(60));
        if (ui.button("more", {view.x + (cw - bw) * 0.5f, cy + ui.s(4), bw, ui.s(40)}, loading_ ? "Chargement…" : "Plus d'avis", st)) load(true);
    }
    ui.endScroll();
    ui.popId();
}

void RaidReviewsPanel::drawModal() {
    if (modal_ == Modal::None) return;
    UIContext& ui = g_.ui;
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 175});
    ui.pushId("reviews/editor");
    bool close = false;
    if (modal_ == Modal::Editor) {
        const bool editing = page_.hasMine;
        const raidreviewsui::EditorLayout L = raidreviewsui::layoutEditor(ui, editing, stars_, text_);
        if (focusStars_) {
            ui.setFocus("star/" + std::to_string(std::max(0, stars_ - 1)));
            focusStars_ = false;
        }
        ui.r().fillRoundedRect(L.box, ui.s(18), Color{9, 14, 32, 250});
        ui.r().drawRoundedRect(L.box, ui.s(18), kOrange.withAlphaF(0.7f), ui.s(1.5f));
        fit(ui, L.titleText, {L.title.x, L.title.center().y}, 26, L.title.w, kText, FontFace::Title);
        drawLines(ui, L.intro, {L.title.x, L.introY}, 16, L.introLineH, ui.theme().textDim);
        fit(ui, "Votre note · ← → ou stick pour choisir, Entrée ou A", {L.caption.x, L.caption.center().y}, 15, L.caption.w, ui.theme().textDim);
        // The stars: focus stops; moving along them (arrows, stick) chooses, a click or Entrée / A too.
        int focusedStar = -1, hoverStar = -1;
        for (int i = 0; i < raidreviews::kMaxStars; ++i) {
            const Rect& sr = L.stars[static_cast<size_t>(i)];
            bool hover = false;
            const std::string id = "star/" + std::to_string(i);
            if (ui.hotspot(id, sr, &hover)) stars_ = i + 1;
            if (hover && ui.usingPointer()) hoverStar = i;
            if (ui.focused(id)) focusedStar = i;
        }
        if (focusedStar >= 0 && prevStarFocus_ >= 0 && focusedStar != prevStarFocus_ && !ui.usingPointer()) stars_ = focusedStar + 1;
        prevStarFocus_ = focusedStar;
        const int shown = hoverStar >= 0 ? hoverStar + 1 : stars_;
        for (int i = 0; i < raidreviews::kMaxStars; ++i) {
            const Rect& sr = L.stars[static_cast<size_t>(i)];
            if (focusedStar == i && !ui.usingPointer()) ui.r().drawRoundedRect(sr.expanded(ui.s(2)), ui.s(10), ui.theme().accent, ui.s(2));
            raidreviewsui::drawStar(ui, sr.center(), sr.w * 0.42f, i < shown ? 1.0f : 0.0f, kStarOn, kStarOff);
        }
        const std::string word = shown > 0 ? std::to_string(shown) + " / 5 · " + raidreviews::starsWord(shown) : std::string("Choisissez une note");
        fit(ui, word, {L.word.x, L.word.center().y}, 18, L.word.w, shown > 0 ? kGold : kDim, FontFace::Bold);
        fit(ui, "Commentaire (facultatif, 500 caractères au plus)", {L.captionText.x, L.captionText.center().y}, 15, L.captionText.w, ui.theme().textDim);
        const bool tooLong = raidreviews::textLength(raidreviews::trimmed(text_)) > raidreviews::kMaxText;
        fit(ui, raidreviews::counterText(text_), {L.counter.right(), L.counter.center().y}, 14, L.counter.w, tooLong ? ui.theme().danger : kDim, FontFace::Bold,
            Align::Right);
        // Room for the longest comment in bytes (4 per character at most); the counter says when it is too long.
        ui.textField("text", L.field, text_, "Votre commentaire (facultatif)", static_cast<size_t>(raidreviews::kMaxText) * 4);
        ui.r().fillRoundedRect(L.preview, ui.s(10), Color{18, 24, 54, 235});
        if (!L.previewLines.empty())
            drawLines(ui, L.previewLines, {L.preview.x + ui.s(12), L.preview.y + ui.s(8)}, 15, L.previewLineH, kText);
        else
            fit(ui, "Aucun commentaire : seule votre note sera publiée.", {L.preview.x + ui.s(12), L.preview.y + ui.s(8) + L.previewLineH * 0.5f}, 14,
                L.preview.w - ui.s(24), kDim);
        const std::string problem = stars_ > 0 ? raidreviews::problemOf(stars_, text_) : std::string();
        fit(ui, !modalError_.empty() ? modalError_ : problem, {L.problem.x, L.problem.center().y}, 15, L.problem.w, ui.theme().danger);
        UIContext::ButtonStyle ok;
        ok.primary = true;
        ok.fontSize = 17;
        ok.enabled = !busy_ && stars_ > 0 && problem.empty();
        UIContext::ButtonStyle plain;
        plain.fontSize = 17;
        if (ui.button("ok", L.ok, L.okLabel, ok)) submit();
        if (ui.button("cancel", L.cancel, "Annuler", plain)) close = true;
        if (editing) {
            UIContext::ButtonStyle del = plain;
            del.danger = true;
            del.enabled = !busy_;
            if (ui.button("remove", L.remove, "Supprimer mon avis", del)) {
                modalError_.clear();
                modal_ = Modal::Delete;
                ui.setFocus("keep");
            }
        }
    } else {
        // Deleting my review, or reporting a comment: a text, (the reason), two buttons.
        const bool del = modal_ == Modal::Delete;
        const float w = std::min(ui.screenW() - ui.s(40), ui.s(640)), tw = w - ui.s(60);
        const auto lines = wrap(ui,
                                del ? "Supprimer votre avis sur cet événement ? Votre note et votre commentaire disparaissent ; vous pourrez en donner un "
                                      "nouveau."
                                    : "Signaler ce commentaire aux modérateurs ? Après plusieurs signalements, il est masqué en attendant leur décision.",
                                FontFace::Body, 17, tw, 4);
        const float textH = static_cast<float>(lines.size()) * ui.s(24);
        const float h = ui.s(30) + textH + ui.s(16) + (del ? 0.0f : ui.s(42) + ui.s(14)) + ui.s(24) + ui.s(10) + ui.s(46) + ui.s(26);
        const Rect box{(ui.screenW() - w) * 0.5f, (ui.screenH() - h) * 0.5f, w, h};
        ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
        ui.r().drawRoundedRect(box, ui.s(18), ui.theme().danger.withAlphaF(0.7f), ui.s(1.5f));
        const float x = box.x + ui.s(30);
        float y = box.y + ui.s(30);
        y += drawLines(ui, lines, {x, y}, 17, ui.s(24), kText) + ui.s(16);
        if (!del) {
            ui.cycle("reason", {x, y, tw, ui.s(42)}, "Motif", reportReason_, kReasonLabels);
            reportReason_ = std::clamp(reportReason_, 0, 4);
            y += ui.s(42) + ui.s(14);
        }
        fit(ui, modalError_, {x, y + ui.s(12)}, 15, tw, ui.theme().danger);
        y += ui.s(24) + ui.s(10);
        const std::string okLabel = del ? "Supprimer" : "Signaler", backLabel = del ? "Garder" : "Annuler";
        const float okW = std::min(textW(ui, okLabel, 17, FontFace::Bold) + ui.s(44), tw * 0.48f);
        const float cW = std::min(textW(ui, backLabel, 17, FontFace::Bold) + ui.s(44), tw * 0.48f);
        UIContext::ButtonStyle d;
        d.danger = true;
        d.fontSize = 17;
        d.enabled = !busy_;
        UIContext::ButtonStyle plain;
        plain.fontSize = 17;
        if (ui.button("go", {x + tw - okW, y, okW, ui.s(46)}, okLabel, d)) {
            if (del) remove();
            else report();
        }
        if (ui.button("keep", {x + tw - okW - ui.s(12) - cW, y, cW, ui.s(46)}, backLabel, plain)) close = true;
    }
    if (!close && ui.backPressed() && !ui.editingText()) close = true;
    ui.popId();
    if (close) {
        // Back from the deletion: to the editor; else closed.
        modal_ = modal_ == Modal::Delete ? Modal::Editor : Modal::None;
        modalError_.clear();
    }
}

}  // namespace cosmic
