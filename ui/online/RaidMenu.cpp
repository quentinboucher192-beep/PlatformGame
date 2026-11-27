#include "ui/online/RaidMenu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "game/levels/LevelTimer.h"
#include "ui/online/RaidWidgets.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::raidmenu {

using namespace raidui;

namespace {

const Color kNoticeFill{20, 14, 26, 232};
const Color kRuleColor{255, 138, 138, 255};

}  // namespace

const char* sectionLabel(Section s) {
    switch (s) {
        case Section::Remaining: return "Ce qu'il reste à faire";
        case Section::Zones: return "Zones terminées";
        case Section::Journal: return "Journal";
        case Section::Group: return "Groupe";
        case Section::Titan: return "Titan";
        default: return "";
    }
}

// ------------------------------------------------------------------------------ notifications

void Notices::push(Notice n) {
    n.shownAt = -1.0f;
    journal_.push_back(n);
    if (journal_.size() > kJournalMax) journal_.erase(journal_.begin());
    queue_.push_back(std::move(n));
    // Too many waiting (a long stay in the menu): the oldest not shown yet give way (the journal keeps them).
    while (queue_.size() > kMaxWaiting) {
        auto it = std::find_if(queue_.begin(), queue_.end(), [](const Notice& q) { return q.shownAt < 0.0f; });
        queue_.erase(it == queue_.end() ? queue_.begin() : it);
    }
}

void Notices::update(float clock) {
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [&](const Notice& q) { return q.shownAt >= 0.0f && clock - q.shownAt >= kShow; }),
                 queue_.end());
}

std::vector<const Notice*> Notices::front() const {
    std::vector<const Notice*> out;
    for (const Notice& q : queue_) {
        if (static_cast<int>(out.size()) >= kMaxShown) break;
        out.push_back(&q);
    }
    return out;
}

void Notices::drawn(size_t count, float clock) {
    for (size_t i = 0; i < count && i < queue_.size(); ++i)
        if (queue_[i].shownAt < 0.0f) queue_[i].shownAt = clock;
}

float Notices::alpha(const Notice& n, float clock) {
    const float t = n.shownAt < 0.0f ? 0.0f : std::max(0.0f, clock - n.shownAt);
    const float in = std::min(1.0f, 0.35f + t / 0.2f);
    const float out = std::clamp((kShow - t) / kFade, 0.0f, 1.0f);
    return std::min(in, out);
}

Section Notices::latestSection() const {
    Section s = Section::Count;
    for (const Notice& q : queue_)
        if (q.shownAt >= 0.0f) s = q.section;
    return s;
}

std::string raidTime(float raidSeconds) {
    const int total = std::max(0, static_cast<int>(raidSeconds));
    char b[24];
    if (total >= 3600) std::snprintf(b, sizeof b, "%d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60);
    else std::snprintf(b, sizeof b, "%02d:%02d", total / 60, total % 60);
    return b;
}

// 2.81.2: the key alone (« Tab », the gamepad's button): « MENU DU RAID » no more (the user's wish); « Menu » when unbound.
std::string buttonText(const std::string& key) { return key.empty() ? std::string("Menu") : key; }

std::string reminder(const Notice& n, const std::string& key) {
    const std::string k = key.empty() ? std::string("menu du raid") : key;
    switch (n.section) {
        case Section::Zones: return n.table ? "Tableau : " + k : "Zones : " + k;
        case Section::Group: return "Groupe : " + k;
        case Section::Titan: return "Titan : " + k;
        case Section::Remaining: return "À faire : " + k;
        default: return "Journal : " + k;
    }
}

NoticeLayout layoutNotices(UIContext& ui, const Rect& area, float k, const std::vector<const Notice*>& list, const std::string& text, bool button) {
    NoticeLayout out;
    const float pad = ui.s(8) * k, bar = ui.s(4) * k, gap = ui.s(6) * k;
    float y = area.y;
    if (button) {
        const float bh = ui.s(28) * k;
        // 2.81.2: the key alone, never narrower than a comfortable click zone.
        const float bw = std::min(area.w, std::max(ui.s(72) * k, textW(ui, text, 13 * k, FontFace::Bold) + ui.s(28) * k));
        if (bh <= area.h) {
            out.button = {area.center().x - bw * 0.5f, area.y, bw, bh};
            y = out.button.bottom() + gap;
        }
    }
    const float innerW = area.w - pad * 2 - bar;
    for (const Notice* n : list) {
        NoticeBox b;
        // Its text on two lines at most; one, then none, when the area has no more room.
        for (int maxLines = 2; maxLines >= 0; --maxLines) {
            b.lines = maxLines > 0 ? wrap(ui, n->text, FontFace::Body, 13 * k, innerW, maxLines) : std::vector<std::string>{};
            const float textH = b.lines.empty() ? 0.0f : ui.s(2) * k + static_cast<float>(b.lines.size()) * ui.s(17) * k;
            const float h = pad * 2 + ui.s(18) * k + textH;
            if (y + h <= area.bottom() + 0.5f) {
                b.rect = {area.x, y, area.w, h};
                break;
            }
        }
        if (b.rect.w <= 0.0f) break;  // no room: it waits
        y = b.rect.bottom() + gap;
        out.boxes.push_back(std::move(b));
    }
    return out;
}

bool drawNotices(UIContext& ui, const Rect& area, float k, const std::vector<const Notice*>& list, const std::vector<float>& alpha,
                 const std::string& key, bool button, size_t* drawnCount, Rect* buttonRect) {
    const std::string bt = buttonText(key);
    const NoticeLayout L = layoutNotices(ui, area, k, list, bt, button);
    if (buttonRect) *buttonRect = L.button;
    bool clicked = false;
    if (L.button.w > 0.0f) {
        // A mouse-only click zone (never a stop of the keyboard or the gamepad: no focus taken).
        bool hover = false;
        clicked = ui.mouseClick("raidmenu/button", L.button, &hover);
        ui.r().fillRoundedRect(L.button, L.button.h * 0.5f, hover ? Color{40, 52, 104, 240} : Color{14, 18, 40, 220});
        ui.r().drawRoundedRect(L.button, L.button.h * 0.5f, hover ? kGold : Color{255, 255, 255, 70}, ui.s(1.2f) * k);
        fit(ui, bt, L.button.center(), 13 * k, L.button.w - ui.s(16) * k, hover ? kGold : kText, FontFace::Bold, Align::Center);
    }
    const float pad = ui.s(8) * k, bar = ui.s(4) * k;
    for (size_t i = 0; i < L.boxes.size(); ++i) {
        const Notice& n = *list[i];
        const float a = i < alpha.size() ? std::clamp(alpha[i], 0.0f, 1.0f) : 1.0f;
        const Rect& r = L.boxes[i].rect;
        ui.r().fillRoundedRect(r, ui.s(8) * k, kNoticeFill.withAlphaF(a));
        ui.r().fillRect({r.x, r.y, bar, r.h}, n.color.withAlphaF(a));
        const float x = r.x + bar + pad, innerW = r.w - bar - pad * 2, rowY = r.y + pad + ui.s(9) * k;
        // The reminder (« Tableau : Tab ») measured first at the right, the title in what is left.
        const std::string rem = reminder(n, key);
        const float hw = std::min(textW(ui, rem, 12 * k, FontFace::Bold), innerW * 0.45f);
        fit(ui, rem, {r.right() - pad, rowY}, 12 * k, hw, kGold.withAlphaF(a), FontFace::Bold, Align::Right);
        fit(ui, n.title, {x, rowY}, 14 * k, std::max(0.0f, innerW - hw - ui.s(10) * k), n.color.withAlphaF(a), FontFace::Bold);
        if (!L.boxes[i].lines.empty())
            drawLines(ui, L.boxes[i].lines, {x, r.y + pad + ui.s(20) * k}, 13 * k, ui.s(17) * k, kText.withAlphaF(a));
    }
    if (drawnCount) *drawnCount = L.boxes.size();
    return clicked;
}

// ----------------------------------------------------------------------------------- the menu

void Menu::open(Section s) {
    open_ = true;
    if (s != Section::Count) section_ = s;
    keys_.reset();  // titan 13: what is held now (moving) waits for its release
    scrollBy = 0.0f;
    confirm = false;
}

bool Menu::handleKeys(const InputManager& in, bool allowed, Section openAt) {
    confirm = false;
    if (!open_) {
        if (allowed && in.menuPressed(Action::RaidMenu)) {
            open(openAt);
            keys_.update(in);  // titan 13: the directions held at this moment are ignored until released
        }
        return false;
    }
    if (!allowed) {
        open_ = false;
        return false;
    }
    if (in.menuPressed(Action::RaidMenu)) {
        open_ = false;
        return false;
    }
    if (in.menuPressed(Action::MenuBack)) {
        open_ = false;
        return true;
    }
    // Titan 13: a section changes on a fresh press only (never a held direction's repeat, never one held when it opened).
    keys_.update(in);
    int step = 0;
    if (keys_.fresh(in, Action::MenuLeft) || keys_.fresh(in, Action::MenuTabPrev)) step = -1;
    else if (keys_.fresh(in, Action::MenuRight) || keys_.fresh(in, Action::MenuTabNext)) step = 1;
    if (step != 0) section_ = static_cast<Section>((static_cast<int>(section_) + step + kSections) % kSections);
    if (keys_.pressed(in, Action::MenuUp)) scrollBy -= 90.0f;
    if (keys_.pressed(in, Action::MenuDown)) scrollBy += 90.0f;
    confirm = in.menuPressed(Action::MenuConfirm);
    return false;
}

InputFrame playerFrame(const PlayerInput& in, const Hold& h) { return h.any() ? InputFrame{} : InputFrame::fromPlayerInput(in); }

// ------------------------------------------------------------------------------ menu: layout

namespace {

const char* const kRule = "Le jeu continue : vous pouvez être touché. Tant que ce menu est ouvert, vos touches ne bougent pas votre personnage.";

std::string closeText(const std::string& key) { return key.empty() ? std::string("Fermer") : "Fermer · " + key; }

std::string keysText(const std::string& key) {
    if (key.empty()) return "Haut / Bas, stick ou molette : défiler · Gauche / Droite, Q / E ou LB / RB : sections · Échap ou B : fermer";
    return "Haut / Bas, stick ou molette : défiler · Gauche / Droite, Q / E ou LB / RB : sections · " + key + ", Échap ou B : fermer";
}

/// Writes (or only measures) a section's content from the top down: `y` grows by what each part takes.
struct Pen {
    UIContext& ui;
    float x, y, w;
    bool draw;
};

void heading(Pen& p, const std::string& t, Color c) {
    if (p.draw) fit(p.ui, t, {p.x, p.y + p.ui.s(12)}, 16, p.w, c, FontFace::Bold);
    p.y += p.ui.s(30);
}

void paragraph(Pen& p, const std::string& t, Color c, int maxLines = 6) {
    const auto lines = wrap(p.ui, t, FontFace::Body, 14, p.w, maxLines);
    if (p.draw) drawLines(p.ui, lines, {p.x, p.y}, 14, p.ui.s(20), c);
    p.y += static_cast<float>(lines.size()) * p.ui.s(20) + p.ui.s(8);
}

void lineList(Pen& p, const std::vector<raidhud::Line>& ls) {
    const float markW = p.ui.s(64);
    for (const raidhud::Line& l : ls) {
        if (l.mark.empty()) {  // a line without a mark: a sub-title (« BRANCHE · ce qu'il manque »)
            p.y += p.ui.s(4);
            heading(p, l.text, l.color);
            continue;
        }
        const auto lines = wrap(p.ui, l.text, FontFace::Body, 14, p.w - markW, 6);
        if (p.draw) {
            fit(p.ui, l.mark, {p.x, p.y + p.ui.s(10)}, 14, markW - p.ui.s(8), l.color, FontFace::Bold);
            drawLines(p.ui, lines, {p.x + markW, p.y}, 14, p.ui.s(20), kText);
        }
        p.y += static_cast<float>(std::max<size_t>(1, lines.size())) * p.ui.s(20) + p.ui.s(6);
    }
}

void entry(Pen& p, const Notice& n, const raidhud::RaidClock& clock) {
    const float timeW = p.ui.s(84), tx = p.x + timeW, tw = p.w - timeW;
    const auto lines = wrap(p.ui, n.text, FontFace::Body, 14, tw, 3);
    if (p.draw) {
        fit(p.ui, clock.at(n.raidSeconds), {p.x, p.y + p.ui.s(11)}, 14, timeW - p.ui.s(10), kDim, FontFace::Bold, Align::Left, true);
        fit(p.ui, n.title, {tx, p.y + p.ui.s(11)}, 15, tw, n.color, FontFace::Bold);
        drawLines(p.ui, lines, {tx, p.y + p.ui.s(24)}, 14, p.ui.s(19), kDim);
    }
    p.y += p.ui.s(24) + static_cast<float>(lines.size()) * p.ui.s(19) + p.ui.s(10);
}

void member(Pen& p, const Member& m) {
    const float h = p.ui.s(34);
    if (p.draw) {
        UIContext& ui = p.ui;
        const Rect row{p.x, p.y, p.w, h - ui.s(4)};
        ui.r().fillRoundedRect(row, ui.s(6), m.me ? Color{42, 42, 90, 240} : Color{22, 29, 66, 230});
        const float cy = row.center().y, pad = ui.s(10);
        const float nameW = p.w * 0.32f, barW = p.w * 0.24f, heartsW = p.w * 0.12f;
        const float stateX = row.right() - pad, stateW = p.w - nameW - barW - heartsW - pad * 4;
        fit(ui, m.vacant ? "—" : m.name, {row.x + pad, cy}, 15, nameW - pad, m.vacant ? kDim : m.color, FontFace::Bold, Align::Left, true);
        const float bx = row.x + pad + nameW, bh = ui.s(8);
        ui.r().fillRoundedRect({bx, cy - bh * 0.5f, barW, bh}, bh * 0.5f, Color{58, 42, 58, 255});
        const float hp = std::clamp(m.health, 0.0f, 1.0f);
        if (!m.vacant && hp > 0.0f)
            ui.r().fillRoundedRect({bx, cy - bh * 0.5f, barW * hp, bh}, bh * 0.5f, hp > 0.6f ? kGreen : hp > 0.3f ? kGold : kOrange);
        fit(ui, m.hearts, {bx + barW + pad, cy}, 13, heartsW, kDim, FontFace::Body, Align::Left, true);
        fit(ui, m.state, {stateX, cy}, 14, std::max(0.0f, stateW), m.stateColor, FontFace::Body, Align::Right);
    }
    p.y += h;
    // 2.79: what a test's simulated player thinks, under its row (wrapped to the width, measured as drawn).
    if (!m.thought.empty()) {
        const float indent = p.ui.s(18);
        const auto lines = wrap(p.ui, m.thought, FontFace::Body, 13, p.w - indent, 3);
        if (p.draw) drawLines(p.ui, lines, {p.x + indent, p.y}, 13, p.ui.s(18), kDim);
        p.y += static_cast<float>(lines.size()) * p.ui.s(18) + p.ui.s(4);
    }
}

/// A section's content (measured with `p.draw` false, drawn with it true); `results`: set when its
/// « Résultats du raid » button was clicked.
void sectionBody(Pen& p, const MenuData& d, Section s, bool* results, bool* tableAsked = nullptr, bool* chatAsked = nullptr) {
    switch (s) {
        case Section::Remaining:
            heading(p, d.remainingTitle, kGold);
            if (d.remaining.empty()) paragraph(p, "Rien à faire ici pour l'instant.", kDim);
            lineList(p, d.remaining);
            break;
        case Section::Zones:
            // 2.78: the full table (tabs « Zone » / « Raid entier », sort, tooltips) opens over the menu.
            if (d.tableAvailable) {
                const float bw = std::min(p.ui.s(420), p.w), bh = p.ui.s(48);
                if (p.draw) {
                    UIContext::ButtonStyle bs;
                    bs.primary = true;
                    if (p.ui.button("contribtable", {p.x, p.y, bw, bh}, "Tableau des contributions", bs) && tableAsked) *tableAsked = true;
                }
                p.y += bh + p.ui.s(8);
                paragraph(p, "Entrée ou A : le tableau de chaque zone finie et celui du raid entier (dégâts, soutien, mécanismes, objectifs, morts, réanimations).",
                          kDim, 3);
            }
            if (d.tableLines.empty())
                paragraph(p, "Aucune zone terminée pendant que votre instance y était : le tableau des contributions de chaque zone finie ici "
                             "s'ajoute ici.",
                          kDim);
            for (const std::string& l : d.tableLines) paragraph(p, l, kText, 2);
            if (!d.otherZones.empty()) {
                std::string names;
                for (const std::string& n : d.otherZones) names += (names.empty() ? "" : " · ") + n;
                heading(p, "AUTRES ZONES TERMINÉES DANS LA BRANCHE", kSky);
                paragraph(p, names, kText, 4);
                paragraph(p, "Terminées ailleurs ou avant votre arrivée : leurs tableaux, pour les groupes de votre clan, sont dans Résultats du raid.", kDim, 3);
            }
            break;
        case Section::Journal: {
            const size_t n = d.journal ? d.journal->size() : 0;
            heading(p, n == 0 ? std::string("JOURNAL") : "JOURNAL · " + std::to_string(n) + (n > 1 ? " notifications" : " notification"), kGold);
            if (n == 0) paragraph(p, "Aucune notification pour l'instant : chacune s'ajoute ici avec son heure dans le raid.", kDim);
            for (size_t i = n; i-- > 0;) entry(p, (*d.journal)[i], d.clock);
            break;
        }
        case Section::Group:
            // 2.95: the raid's chat, from here too (a gamepad without its own button: Entrée / A).
            if (d.chat) {
                const float bw = std::min(p.ui.s(380), p.w), bh = p.ui.s(48);
                if (p.draw) {
                    UIContext::ButtonStyle bs;
                    bs.primary = true;
                    if (p.ui.button("chat", {p.x, p.y, bw, bh}, "Chat du raid", bs) && chatAsked) *chatAsked = true;
                }
                p.y += bh + p.ui.s(8);
                paragraph(p, d.chatKey.empty() ? std::string("Entrée ou A : écrire à votre groupe ou à tout le raid.")
                                               : "Entrée ou A : écrire à votre groupe ou à tout le raid (aussi " + d.chatKey + " pendant le combat).",
                          kDim, 3);
            }
            heading(p, d.groupTitle, kText);
            for (const Member& m : d.group) member(p, m);
            p.y += p.ui.s(6);
            if (!d.groupFooter.empty()) paragraph(p, d.groupFooter, kGold, 3);
            for (const raidhud::Line& l : d.groupStates) {  // 2.77.2: what each state means, its keys
                p.y += p.ui.s(4);
                heading(p, l.mark, l.color);
                paragraph(p, l.text, kText, 6);
            }
            break;
        case Section::Titan: {
            if (d.results) {
                const float bw = std::min(p.ui.s(380), p.w), bh = p.ui.s(48);
                if (p.draw) {
                    UIContext::ButtonStyle bs;
                    bs.primary = true;
                    if (p.ui.button("results", {p.x, p.y, bw, bh}, "Résultats du raid", bs) && results) *results = true;
                }
                p.y += bh + p.ui.s(8);
                paragraph(p, "Entrée ou A : les résultats validés par le serveur, tous les groupes comptés.", kDim, 2);
            }
            heading(p, "LE TITAN", kGold);
            lineList(p, d.titan);
            p.y += p.ui.s(6);
            heading(p, "ÉVÉNEMENTS", kSky);
            bool any = false;
            if (d.journal)
                for (size_t i = d.journal->size(); i-- > 0;)
                    if ((*d.journal)[i].section == Section::Titan) {
                        entry(p, (*d.journal)[i], d.clock);
                        any = true;
                    }
            if (!any) paragraph(p, "Aucun événement du titan pour l'instant.", kDim);
            break;
        }
        default: break;
    }
}

}  // namespace

MenuLayout layoutMenu(UIContext& ui, const std::string& keyName) {
    MenuLayout L;
    const float W = ui.screenW(), H = ui.screenH();
    const float pw = std::min(ui.s(1080), W - ui.s(32)), ph = std::min(ui.s(820), H - ui.s(32));
    L.panel = {(W - pw) * 0.5f, (H - ph) * 0.5f, pw, ph};
    const float pad = ui.s(20), x = L.panel.x + pad, iw = pw - pad * 2;
    float y = L.panel.y + pad;
    // The title row: « Fermer · Tab » measured first at the right, the title in what is left.
    const float rowH = ui.s(38);
    const float cw = std::min(iw * 0.4f, textW(ui, closeText(keyName), 15, FontFace::Bold) + ui.s(28));
    L.close = {x + iw - cw, y, cw, rowH};
    L.title = {x, y, std::max(0.0f, iw - cw - ui.s(12)), rowH};
    y += rowH + ui.s(6);
    // The rule, under it: the game goes on.
    L.rule = wrap(ui, kRule, FontFace::Bold, 15, iw, 2);
    L.ruleY = y;
    y += static_cast<float>(L.rule.size()) * ui.s(20) + ui.s(10);
    // 2.81.2: the tabs all of ONE width (the row shared), their font smaller when the widest name (measured in the
    // language shown) does not fit: a section chosen never resizes them.
    const float th = ui.s(40), gap = ui.s(6), room = iw - gap * static_cast<float>(kSections - 1);
    float widest = 0.0f;
    for (int i = 0; i < kSections; ++i) widest = std::max(widest, textW(ui, sectionLabel(static_cast<Section>(i)), 16, FontFace::Bold) + ui.s(28));
    const float each = std::floor(room / static_cast<float>(kSections));
    L.tabFont = widest <= each ? 16.0f : std::max(10.0f, 16.0f * each / widest);
    L.tabsRow = {x, y, iw, th};
    for (int i = 0; i < kSections; ++i) L.tabs.push_back({x + static_cast<float>(i) * (each + gap), y, each, th});
    y += th + ui.s(10);
    // The keys at the bottom, then the section's view between the tabs and them.
    L.keyLines = wrap(ui, keysText(keyName), FontFace::Body, 13, iw, 2);
    const float kh = static_cast<float>(L.keyLines.size()) * ui.s(18);
    L.keys = {x, L.panel.bottom() - pad - kh, iw, kh};
    L.view = {x, y, iw, std::max(0.0f, L.keys.y - ui.s(8) - y)};
    return L;
}

float sectionHeight(UIContext& ui, const MenuData& d, Section s, float width) {
    Pen p{ui, 0.0f, 0.0f, width, false};
    sectionBody(p, d, s, nullptr);
    return p.y;
}

MenuAction drawMenu(UIContext& ui, Menu& m, const MenuData& d) {
    MenuAction act;
    ui.pushId("raidmenu");
    // Its keys move in the menu only: the focus never travels to its buttons (mouse only).
    ui.consumeNavigation(true, true);
    const MenuLayout L = layoutMenu(ui, d.keyName);
    ui.r().fillRect({0.0f, 0.0f, ui.screenW(), ui.screenH()}, Color{2, 4, 10, 110});
    // 2.79: opaque (the game goes on under it: nothing of it shows under its texts).
    ui.r().fillRoundedRect(L.panel, ui.s(16), Color{16, 22, 46, 255});
    ui.r().drawRoundedRect(L.panel, ui.s(16), ui.theme().accent.withAlpha(150), std::max(1.0f, ui.s(1.5f)));
    // The body faces (« · » is not in the titles' font).
    // Titan 13: the raid's countdown at the right of the title (measured first; the title in what is left).
    float titleW = L.title.w;
    {
        const std::string c = d.clock.deadline ? "Reste " + LevelTimer::format(d.clock.left(), false)
                                               : "Écoulé " + LevelTimer::format(std::max(0.0f, d.clock.now), false);
        const float cw = std::min(L.title.w * 0.45f, textW(ui, c, 18, FontFace::Bold) + ui.s(12));
        fit(ui, c, {L.title.right() - cw, L.title.center().y}, 18, cw, raidhud::clockColor(d.clock, ui.theme().text, ui.time()), FontFace::Bold);
        titleW = std::max(0.0f, L.title.w - cw - ui.s(12));
    }
    fit(ui, d.title, {L.title.x, L.title.center().y}, 22, titleW, kText, FontFace::Bold);
    UIContext::ButtonStyle cs;
    cs.small = true;
    cs.fontSize = ui.fitFont(closeText(d.keyName), FontFace::Bold, 15, std::max(ui.s(20), L.close.w - ui.s(16)));
    if (ui.button("close", L.close, closeText(d.keyName), cs)) act.close = true;
    drawLines(ui, L.rule, {L.title.x, L.ruleY}, 15, ui.s(20), kRuleColor, FontFace::Bold);
    // The tabs (the mouse picks one; the keys: Gauche / Droite, Q / E, LB / RB).
    for (int i = 0; i < kSections; ++i) {
        const Rect& tr = L.tabs[static_cast<size_t>(i)];
        const bool on = static_cast<int>(m.section()) == i;
        bool hover = false;
        if (ui.mouseClick("tab" + std::to_string(i), tr, &hover)) m.select(static_cast<Section>(i));
        ui.r().fillRoundedRect(tr, ui.s(6), on ? Color{28, 64, 84, 240} : hover ? Color{24, 40, 64, 230} : Color{16, 22, 42, 220});
        if (on) ui.r().fillRect({tr.x + ui.s(8), tr.bottom() - ui.s(3), tr.w - ui.s(16), ui.s(3)}, ui.theme().accent);
        fit(ui, sectionLabel(static_cast<Section>(i)), tr.center(), L.tabFont, tr.w - ui.s(12), on ? Color::white() : kDim, FontFace::Bold, Align::Center);
    }
    // The section, scrolled (keys: the offset asked, the wheel and the bar: the scroll region's own).
    const Section s = m.section();
    const int si = static_cast<int>(s);  // each section its own scroll
    const float pad = ui.s(8), cw = L.view.w - ui.s(20);
    const float contentH = sectionHeight(ui, d, s, cw) + pad * 2;
    ui.beginScroll("content" + std::to_string(si), L.view, contentH);
    const float off = ui.scrollOffset();
    if (m.scrollBy != 0.0f) ui.setScroll("content" + std::to_string(si), off + ui.s(m.scrollBy));  // the next frame clamps it
    m.scrollBy = 0.0f;
    Pen p{ui, L.view.x + pad, L.view.y + pad - off, cw, true};
    bool results = false, table = false, chat = false;
    sectionBody(p, d, s, &results, &table, &chat);
    ui.endScroll();
    if (results || (m.confirm && s == Section::Titan && d.results)) act.results = true;
    if (table || (m.confirm && s == Section::Zones && d.tableAvailable)) act.table = true;  // 2.78
    if (chat || (m.confirm && s == Section::Group && d.chat)) act.chat = true;              // 2.95
    m.confirm = false;
    drawLines(ui, L.keyLines, {L.keys.x, L.keys.y}, 13, ui.s(18), kDim);
    ui.popId();
    return act;
}

}  // namespace cosmic::raidmenu
