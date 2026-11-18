// 2.73: the sheets of Asterion and of its seven gigaboss (ui/screens/RaidSheetsScene.h).

#include "ui/screens/RaidSheetsScene.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/content/CreaturePlaces.h"
#include "game/enemies/BossAttackSheet.h"
#include "game/raid/RaidSheets.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/AsterionFilm.h"
#include "ui/screens/CreatureArt.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/RaidSheetSketch.h"
#include "ui/widgets/AsterionFigure.h"
#include "ui/widgets/BossSheetWidgets.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

namespace {

using namespace raidui;

/// Never below 11.5 pixels on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

/// `t` fitted to `r` (smaller down to `minScale`, never under 11.5 px, then shortened).
RaidSheetText fitted(UIContext& ui, const std::string& t, const Rect& r, FontFace face, float size, float minScale = 0.7f) {
    RaidSheetText out;
    out.r = r;
    out.size = std::max(ui.fitFont(t, face, fz(ui, size), r.w, minScale), 11.5f / ui.s(1.0f));
    out.text = ui.elide(t, face, out.size, r.w);
    out.px = ui.s(out.size);
    out.face = face;
    return out;
}

std::string num(float v, int decimals = 1) { return sheetNumber(v, decimals); }

constexpr const char* kTitan = "asterion";
const std::vector<std::string> kTitanTabs = {"Le titan", "Les sept parties", "Récompenses"};
const std::vector<std::string> kBossTabs = {"Combat", "Mécanismes", "Lieu et récompenses"};

std::vector<std::string> sheetIds(const raid::RaidSheets& s) {
    std::vector<std::string> ids = {kTitan};
    for (const raid::GigabossSheet& g : s.gigaboss) ids.push_back(g.id);
    return ids;
}

std::string rowName(const raid::RaidSheets& s, const std::string& id) {
    if (id == kTitan) return s.asterion.title;
    const raid::GigabossSheet* g = s.find(id);
    return g ? g->name : id;
}

std::string rowSub(const raid::RaidSheets& s, const std::string& id) {
    if (id == kTitan) return "Le titan · " + std::to_string(s.asterion.parts.size()) + " parties et son cœur";
    const raid::GigabossSheet* g = s.find(id);
    if (!g) return {};
    return g->stars > 0 ? g->partName + " · difficulté " + std::to_string(g->stars) + " / 10" : g->partName;
}

}  // namespace

RaidSheetsLayout layoutRaidSheets(UIContext& ui, const raid::RaidSheets& sheets, const std::string& id) {
    RaidSheetsLayout L;
    const float W = ui.screenW(), H = ui.screenH(), m = ui.s(28);
    L.screen = {0.0f, 0.0f, W, H};
    // The header: « ‹ Retour », then the title in the room left.
    const float bw = ui.text().measure("‹ Retour", FontFace::Bold, ui.s(fz(ui, 17))).x + ui.s(40);
    L.back = {m, ui.s(20), bw, ui.s(44)};
    const float tx = L.back.right() + ui.s(18);
    L.title = fitted(ui, "FICHES D'ASTERION", {tx, ui.s(20), W - m - tx, ui.s(44)}, FontFace::Title, 30, 0.6f);
    // The list of the sheets.
    const float top = ui.s(80);
    L.list = {m, top, std::min(ui.s(360), W * 0.27f), H - top - ui.s(24)};
    L.rowH = ui.s(76);
    const std::vector<std::string> ids = sheetIds(sheets);
    for (size_t i = 0; i < ids.size(); ++i) {
        RaidSheetsLayout::Row row;
        row.r = {L.list.x, L.list.y + static_cast<float>(i) * (L.rowH + ui.s(8)), L.list.w - ui.s(14), L.rowH};
        row.portrait = {row.r.x + ui.s(8), row.r.y + ui.s(8), L.rowH - ui.s(16), L.rowH - ui.s(16)};
        const float rx = row.portrait.right() + ui.s(12), room = row.r.right() - ui.s(12) - rx;
        row.name = fitted(ui, rowName(sheets, ids[i]), {rx, row.r.y + ui.s(10), room, ui.s(28)}, FontFace::Bold, 19, 0.75f);
        row.sub = fitted(ui, rowSub(sheets, ids[i]), {rx, row.r.y + ui.s(42), room, ui.s(24)}, FontFace::Body, 14, 0.8f);
        L.rows.push_back(std::move(row));
    }
    // The sheet shown.
    L.panel = {L.list.right() + ui.s(18), top, W - m - (L.list.right() + ui.s(18)), H - top - ui.s(24)};
    const float pad = ui.s(22), x = L.panel.x + pad, w = L.panel.w - 2.0f * pad, y = L.panel.y + pad;
    auto chips = [&](const std::vector<std::string>& texts, float cx, float cy, float right) {
        for (const std::string& t : texts) {
            const float size = fz(ui, 14);
            const float tw = ui.text().measure(t, FontFace::Bold, ui.s(size)).x;
            const Rect r{cx, cy, tw + ui.s(24), ui.s(30)};
            if (r.right() > right) break;  // the others are in the tabs
            RaidSheetText c;
            c.r = r;
            c.text = t;
            c.size = size;
            c.px = ui.s(size);
            c.face = FontFace::Bold;
            L.chips.push_back(c);
            cx = r.right() + ui.s(8);
        }
    };
    if (id == kTitan) {
        const raid::AsterionSheet& a = sheets.asterion;
        L.titan = true;
        const float fw = std::min(w * 0.45f, ui.s(520)), capH = ui.s(30);
        L.box = {x, y, fw, L.panel.bottom() - pad - capH - ui.s(10) - y};
        L.caption = fitted(ui, "Clic, Entrée ou A sur une partie : la fiche de son gigaboss", {x, L.box.bottom() + ui.s(10), fw, capH}, FontFace::Body, 14, 0.75f);
        const float rx = L.box.right() + ui.s(24), rw = x + w - rx;
        L.name = fitted(ui, a.title, {rx, y, rw, ui.s(50)}, FontFace::Title, 40, 0.6f);
        L.subtitle = fitted(ui, a.subtitle, {rx, y + ui.s(52), rw, ui.s(30)}, FontFace::Title, 20, 0.7f);
        chips({std::to_string(a.branches) + " branches", std::to_string(a.zones) + " zones", "Instances de " + std::to_string(a.players) + " joueurs"}, rx,
              y + ui.s(92), rx + rw);
        L.tabs = {rx, y + ui.s(136), std::min(rw, ui.s(600)), ui.s(40)};
        L.tabLabels = kTitanTabs;
        L.content = {rx, L.tabs.bottom() + ui.s(12), rw, L.panel.bottom() - pad - (L.tabs.bottom() + ui.s(12))};
        return L;
    }
    const raid::GigabossSheet* g = sheets.find(id);
    const float topH = std::clamp(L.panel.h * 0.3f, ui.s(170), ui.s(230));
    L.box = {x, y, std::min(ui.s(280), w * 0.28f), topH};
    const float rx = L.box.right() + ui.s(22), rw = x + w - rx;
    L.name = fitted(ui, g ? g->name : id, {rx, y, rw, ui.s(42)}, FontFace::Title, 34, 0.6f);
    L.subtitle = fitted(ui, g ? g->title : std::string(), {rx, y + ui.s(44), rw, ui.s(26)}, FontFace::Body, 17, 0.75f);
    if (g) {
        std::vector<std::string> facts = {"Partie : " + g->partName};
        if (!g->lairName.empty()) facts.push_back("Antre : " + g->lairName);
        if (!g->rewards.empty() && g->rewards.front().kind == "badge") facts.push_back("Badge : " + g->rewards.front().name);
        chips(facts, rx, y + ui.s(78), rx + rw);
    }
    L.difficulty = {rx, y + ui.s(120), rw, ui.s(40)};
    L.caption.r = {L.box.x + ui.s(10), L.box.bottom() - ui.s(36), L.box.w - ui.s(20), ui.s(32)};  // its phase, fitted as it is drawn
    L.tabs = {x, L.box.bottom() + ui.s(14), std::min(w, ui.s(640)), ui.s(40)};
    L.tabLabels = kBossTabs;
    L.content = {x, L.tabs.bottom() + ui.s(12), w, L.panel.bottom() - pad - (L.tabs.bottom() + ui.s(12))};
    return L;
}

namespace {

class RaidSheetsScene : public Scene {
public:
    RaidSheetsScene(GameServices& g, std::string sheet, int tab) : g_(g), wanted_(std::move(sheet)), wantedTab_(tab), attacks_(g) {}

    void onEnter() override {
        bg_.init(g_);
        sheets_ = raid::loadRaidSheets(g_.content, g_.achievementCatalog, g_.cosmetics);
        ids_ = sheetIds(sheets_);
        for (size_t i = 0; i < ids_.size(); ++i)
            if (ids_[i] == wanted_) selected_ = static_cast<int>(i);
        tabs_[ids_[static_cast<size_t>(selected_)]] = std::clamp(wantedTab_, 0, 2);
        g_.ui.setFocus("sheets/row" + std::to_string(selected_));
        // Automated screenshots (--raid-sheets <sheet>:<tab>:attaque=<id>): that attack (or mechanism
        // « m:<id> ») of the sheet unfolded with its example, at the top of the list.
        if (g_.onlineOpen.rfind("sheet-unfold:", 0) == 0) {
            const std::string item = g_.onlineOpen.substr(13);
            attacks_.fold(ids_[static_cast<size_t>(selected_)]).expand(item);
            attacks_.scrollTo(item);
            g_.onlineOpen.clear();
        }
        titan_.setOpenHint("Clic ou validation : la fiche de son gigaboss");
        atlas_ = officialCreatureAtlas(g_.content);  // the examples of the attacks (a neutral arena for a gigaboss)
        attacks_.setAtlas(atlas_);
        asterionfilm::hold(g_);  // Asterion's film first (its first viewing), as every screen of the titan
    }

    void fixedUpdate(float) override { attacks_.fixedUpdate(); }

    void update(float dt) override {
        t_ += dt;
        dt_ = dt;
        bg_.update(dt);
        if (!atlas_) {
            atlas_ = officialCreatureAtlas(g_.content);
            attacks_.setAtlas(atlas_);
        }
        attacks_.update(dt);
    }

    void render(float alpha) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.6f);
        const bool top = g_.app.topScene() == this;
        ui.inputEnabled = top;
        ui.pushId("sheets");
        const std::string id = ids_[static_cast<size_t>(selected_)];
        const RaidSheetsLayout L = layoutRaidSheets(ui, sheets_, id);
        UIContext::ButtonStyle bs;
        bs.small = true;
        bool back = ui.button("back", L.back, "‹ Retour", bs);
        ui.label(L.title.text, {L.title.r.x, L.title.r.center().y}, L.title.size, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        drawList(L);
        view_ = RaidSheetsView{};
        view_.valid = true;
        view_.sheet = id;  // the sheet drawn this frame (a row selected now shows on the next)
        view_.tab = tabs_[view_.sheet];
        ui.panel(L.panel, true, 16);
        if (L.titan) drawTitan(L);
        else if (const raid::GigabossSheet* g = sheets_.find(id)) drawGigaboss(L, *g, alpha);
        ui.popId();
        titan_.drawTooltip(g_);  // over everything (only when the titan was drawn this frame)
        if (!back && ui.backPressed()) back = true;
        ui.end();
        if (back) g_.app.popScene();
    }

    RaidSheetsView view() const { return view_; }

private:
    // ------------------------------------------------------------------ the list
    void drawList(const RaidSheetsLayout& L) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const float gap = ui.s(8);
        ui.beginScroll("list", L.list, static_cast<float>(L.rows.size()) * (L.rowH + gap));
        const float off = ui.scrollOffset();
        for (size_t i = 0; i < L.rows.size(); ++i) {
            const RaidSheetsLayout::Row& row0 = L.rows[i];
            const auto moved = [&](Rect rc) {
                rc.y -= off;
                return rc;
            };
            const Rect row = moved(row0.r);
            const std::string rowId = "row" + std::to_string(i);
            UIContext::ButtonStyle st;
            st.selected = static_cast<int>(i) == selected_;
            if (ui.button(rowId, row, "", st)) select(static_cast<int>(i));
            if (ui.focused(rowId) && static_cast<int>(i) != selected_) select(static_cast<int>(i));
            ui.newBadge(rowId, row, "raids.sheet." + ids_[i], true, true);  // « NOUVEAU » (2.73)
            if (row.bottom() < L.list.y || row.y > L.list.bottom()) continue;
            // Its portrait: the gigaboss (its first look), the titan's heart for Asterion.
            const Rect p = moved(row0.portrait);
            r.fillRoundedRect(p, ui.s(8), Color{10, 14, 32, 255});
            if (ids_[i] == kTitan) {
                const float beat = 0.5f + 0.5f * std::sin(t_ * 3.2f);
                r.drawGlow(p.center(), p.w * (0.42f + 0.08f * beat), Color{255, 140, 60, static_cast<uint8_t>(120 + 80 * beat)});
                r.fillCircle(p.center(), p.w * 0.16f, Color{255, 190, 120, 255});
                r.drawRing(p.center(), p.w * 0.34f, std::max(1.0f, ui.s(2)), kOrange.withAlpha(180));
            } else if (const raid::GigabossSheet* g = sheets_.find(ids_[i]); g && g->def) {
                r.pushClip(p);
                drawCreature(g_, *g->def, p.expanded(-ui.s(4)), t_, Color::white(), "idle");
                r.popClip();
            }
            const RaidSheetText& n = row0.name;
            const RaidSheetText& s = row0.sub;
            ui.label(n.text, {n.r.x, n.r.center().y - off}, n.size, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            ui.label(s.text, {s.r.x, s.r.center().y - off}, s.size, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        }
        ui.endScroll();
    }

    void select(int i) {
        if (i == selected_ || i < 0 || i >= static_cast<int>(ids_.size())) return;
        selected_ = i;
        attacks_.stop();
        phaseShown_ = 0;
        phaseT_ = 0.0f;
    }
    void open(const std::string& sheet) {
        for (size_t i = 0; i < ids_.size(); ++i)
            if (ids_[i] == sheet) {
                select(static_cast<int>(i));
                g_.ui.setFocus("sheets/row" + std::to_string(i));
            }
    }

    // ------------------------------------------------------------------ blocks of a tab
    struct Line {
        std::string text;
        Color color;
        FontFace face = FontFace::Body;
    };
    struct Card {
        std::string title, right;
        Color accent{255, 179, 71, 200};
        std::vector<Line> lines;
        std::vector<raid::SheetFact> facts;
        std::string button, buttonId;  // a button at the title's right ("" none)
    };

    /// A card from `y`, `w` wide: its title (and a right part, or a button), its lines wrapped, its
    /// facts (label at the left, value wrapped at the right). Returns its height; `pressed` when its
    /// button was activated.
    float card(const Rect& view, float y, float w, const Card& c, bool* pressed = nullptr) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const float pad = ui.s(12), inner = w - 2.0f * pad - ui.s(4);
        const float lineSize = fz(ui, 15), factSize = fz(ui, 14);
        const float titleH = c.title.empty() ? 0.0f : ui.s(32);
        float h = pad + titleH;
        std::vector<float> lineH;
        for (const Line& l : c.lines) {
            lineH.push_back(ui.text().measureWrapped(l.text, l.face, ui.s(lineSize), inner).y);
            h += lineH.back() + ui.s(6);
        }
        const float labelW = inner * 0.42f, valueW = inner - labelW - ui.s(10);
        std::vector<float> factH;
        for (const raid::SheetFact& f : c.facts) {
            factH.push_back(std::max(ui.s(26), ui.text().measureWrapped(f.value, FontFace::Body, ui.s(factSize), valueW).y + ui.s(8)));
            h += factH.back() + ui.s(2);
        }
        h += pad;
        const Rect b{view.x, y, w, h};
        // The button first (a focus stop even out of sight: the scroll follows the focus).
        float buttonW = 0.0f;
        if (!c.button.empty()) {
            UIContext::ButtonStyle st;
            st.small = true;
            const float bf = fz(ui, 15);
            buttonW = std::min(inner * 0.45f, ui.text().measure(c.button, FontFace::Bold, ui.s(bf)).x + ui.s(32));
            st.fontSize = std::max(ui.fitFont(c.button, FontFace::Bold, bf, buttonW - ui.s(16), 0.75f), 11.5f / ui.s(1.0f));
            const Rect br{b.right() - pad - buttonW, b.y + pad - ui.s(4), buttonW, ui.s(32)};
            if (ui.button(c.buttonId, br, c.button, st) && pressed) *pressed = true;
        }
        if (b.bottom() < view.y || b.y > view.bottom()) return h;
        ++view_.blocks;
        r.fillRoundedRect(b, ui.s(8), Color{12, 18, 38, 225});
        r.fillRect({b.x, b.y + ui.s(6), ui.s(3), b.h - ui.s(12)}, c.accent);
        const float x = b.x + pad + ui.s(4);
        float cy = b.y + pad;
        if (!c.title.empty()) {
            float room = inner - (buttonW > 0.0f ? buttonW + ui.s(12) : 0.0f);
            if (!c.right.empty() && buttonW == 0.0f) {
                const float rf = std::max(ui.fitFont(c.right, FontFace::Body, fz(ui, 14), inner * 0.45f, 0.75f), 11.5f / ui.s(1.0f));
                const std::string rt = ui.elide(c.right, FontFace::Body, rf, inner * 0.45f);
                const float rw = ui.text().measure(rt, FontFace::Body, ui.s(rf)).x;
                ui.label(rt, {b.right() - pad, cy + ui.s(12)}, rf, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
                room -= rw + ui.s(12);
            }
            const float tf = std::max(ui.fitFont(c.title, FontFace::Bold, fz(ui, 18), room, 0.75f), 11.5f / ui.s(1.0f));
            ui.label(ui.elide(c.title, FontFace::Bold, tf, room), {x, cy + ui.s(12)}, tf, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            cy += titleH;
        }
        for (size_t i = 0; i < c.lines.size(); ++i) {
            ui.labelWrapped(c.lines[i].text, {x, cy}, inner, lineSize, c.lines[i].color);
            cy += lineH[i] + ui.s(6);
        }
        for (size_t i = 0; i < c.facts.size(); ++i) {
            const Rect row{x, cy, inner, factH[i]};
            r.fillRoundedRect(row, ui.s(4), i % 2 ? Color{14, 20, 42, 160} : Color{20, 29, 56, 200});
            const float lf = std::max(ui.fitFont(c.facts[i].label, FontFace::Body, factSize, labelW - ui.s(8), 0.8f), 11.5f / ui.s(1.0f));
            ui.label(ui.elide(c.facts[i].label, FontFace::Body, lf, labelW - ui.s(8)), {row.x + ui.s(6), row.y + ui.s(13)}, lf, ui.theme().textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
            ui.labelWrapped(c.facts[i].value, {row.x + labelW + ui.s(4), row.y + ui.s(4)}, valueW, factSize, ui.theme().text);
            cy += factH[i] + ui.s(2);
        }
        return h;
    }

    /// The scroll region of a tab: its content measured on the previous frame; back at its top
    /// when another sheet or tab is shown.
    void beginContent(const Rect& view, const std::string& key) {
        UIContext& ui = g_.ui;
        if (contentOf_ != key) {
            contentOf_ = key;
            contentH_ = view.h;
            ui.setScroll("content", 0.0f);
        }
        ui.beginScroll("content", view, contentH_);
    }
    void endContent(float top, float y) {
        contentH_ = std::max(1.0f, y - top);
        g_.ui.endScroll();
    }

    void tabsOf(const RaidSheetsLayout& L, const std::string& sheet) {
        int& tab = tabs_[sheet];
        g_.ui.tabs("tabs", L.tabs, L.tabLabels, tab);
        view_.tab = tab;
    }

    // ------------------------------------------------------------------ Asterion
    void drawTitan(const RaidSheetsLayout& L) {
        UIContext& ui = g_.ui;
        const raid::AsterionSheet& a = sheets_.asterion;
        // The titan: a part activated opens its gigaboss's sheet.
        std::vector<std::string> openable;
        for (const raid::AsterionPart& p : a.parts)
            if (!p.sheet.empty()) openable.push_back(p.id);
        if (!a.heartSheet.empty()) openable.push_back(a.heart.id);
        ui.r().fillRoundedRect(L.box, ui.s(12), Color{10, 12, 30, 230});
        const std::string part = titan_.draw(g_, "asterion", L.box.expanded(-ui.s(10)), {}, openable);
        ui.label(L.caption.text, {L.caption.r.x, L.caption.r.center().y}, L.caption.size, kDim, Align::Left, FontFace::Body, VAlign::Middle);
        if (!part.empty()) {
            const std::string sheet = part == a.heart.id ? a.heartSheet : sheets_.ofPart(part) ? sheets_.ofPart(part)->id : std::string();
            if (!sheet.empty()) open(sheet);
        }
        ui.label(L.name.text, {L.name.r.x, L.name.r.center().y}, L.name.size, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        ui.label(L.subtitle.text, {L.subtitle.r.x, L.subtitle.r.center().y}, L.subtitle.size, kPeach, Align::Left, FontFace::Title, VAlign::Middle);
        drawChips(L);
        tabsOf(L, kTitan);
        const int tab = tabs_[kTitan];
        const Rect view = L.content;
        beginContent(view, std::string(kTitan) + "#" + std::to_string(tab));
        const float top = view.y - ui.scrollOffset();
        float y = top;
        const float w = view.w - ui.s(16);
        const Color text{216, 222, 240, 255};
        if (tab == 0) {
            Card lore{"Le Dévoreur de mondes", "", kOrange, {}, {}, {}, {}};
            for (const std::string& l : a.lore) lore.lines.push_back({l, text});
            y += card(view, y, w, lore) + ui.s(8);
            Card raid{"Le raid", "", kSky, {}, {}, {}, {}};
            raid.lines.push_back({std::to_string(a.branches) + " branches de " + std::to_string(a.branches > 0 ? a.zones / a.branches : 0) +
                                      " zones : une antre de gigaboss au bout de chacune, puis le cœur.",
                                  text});
            raid.lines.push_back({"Des instances de " + std::to_string(a.players) +
                                      " joueurs au plus : chaque groupe joue la sienne, et tout ce qu'il valide compte pour le même titan.",
                                  text});
            if (a.maxMinutes > 0.0f)
                raid.lines.push_back({"Une branche sans son gigaboss : environ " + num(a.minMinutes, 0) + " à " + num(a.maxMinutes, 0) +
                                          " min à huit joueurs (l'estimation de ses zones).",
                                      text});
            raid.lines.push_back({"Les parties détruites et leurs effets viennent des résultats validés par le serveur, pour tous les groupes.", kDim});
            y += card(view, y, w, raid) + ui.s(8);
            bool pressed = false;
            Card heart{a.heart.name.empty() ? std::string("Le cœur cosmique") : a.heart.name, "", Color{255, 120, 90, 230}, {}, {}, {}, {}};
            if (!a.heart.effect.empty()) heart.lines.push_back({a.heart.effect, kPeach, FontFace::Bold});
            for (const std::string& l : a.heartText) heart.lines.push_back({l, text});
            if (!a.heartSheet.empty()) {
                heart.button = "Sa fiche";
                heart.buttonId = "heart";
            }
            y += card(view, y, w, heart, &pressed) + ui.s(8);
            if (pressed) open(a.heartSheet);
        } else if (tab == 1) {
            for (size_t i = 0; i < a.parts.size(); ++i) {
                const raid::AsterionPart& p = a.parts[i];
                Card c{p.name, "", kGold, {}, {}, {}, {}};
                c.lines.push_back({"Gigaboss : " + p.boss, kPeach, FontFace::Bold});
                if (!p.effect.empty()) c.lines.push_back({p.effect, text});
                for (const std::string& l : p.links) c.lines.push_back({"Pendant le raid : " + l, kDim});
                if (!p.sheet.empty()) {
                    c.button = "Fiche de " + p.boss;
                    c.buttonId = "sheet" + std::to_string(i);
                }
                bool pressed = false;
                y += card(view, y, w, c, &pressed) + ui.s(8);
                if (pressed) open(p.sheet);
            }
            Card heart{a.heart.name, "", Color{255, 120, 90, 230}, {}, {}, {}, {}};
            if (!a.heart.effect.empty()) heart.lines.push_back({a.heart.effect, text});
            y += card(view, y, w, heart) + ui.s(8);
        } else {
            auto rewards = [&](const std::string& kind, const std::string& title, Color accent) {
                Card c{title, "", accent, {}, {}, {}, {}};
                for (const raid::SheetReward& rw : a.rewards)
                    if (rw.kind == kind) {
                        c.lines.push_back({rw.name, kGold, FontFace::Bold});
                        c.lines.push_back({rw.text, text});
                    }
                if (!c.lines.empty()) y += card(view, y, w, c) + ui.s(8);
            };
            rewards("badge", "Badges des gigaboss (présent à sa chute)", kGold);
            rewards("title", "Titres", kSky);
            rewards("suit", "Combinaisons des événements mondiaux", kGreen);
            Card c{"Ce qui compte dans les contributions", "", kOrange, {}, {}, {}, {}};
            for (const std::string& l : a.contributions) c.lines.push_back({l, text});
            c.lines.push_back({"Les récompenses sont accordées par le serveur, d'après les résultats validés ; elles se récupèrent dans CLANS › RAIDS.", kDim});
            y += card(view, y, w, c) + ui.s(8);
        }
        endContent(top, y);
    }

    void drawChips(const RaidSheetsLayout& L) {
        UIContext& ui = g_.ui;
        for (const RaidSheetText& c : L.chips) {
            ui.r().fillRoundedRect(c.r, c.r.h * 0.5f, Color{36, 44, 92, 255});
            ui.label(c.text, c.r.center(), c.size, kText, Align::Center, FontFace::Bold, VAlign::Middle);
        }
    }

    // ------------------------------------------------------------------ a gigaboss
    /// Its look in each phase (the definition's own sheet, then each phase's).
    const EnemyDefinition* phaseLook(const raid::GigabossSheet& g, int phase) {
        if (looksOf_ != g.id) {
            looksOf_ = g.id;
            looks_.clear();
            for (const BossPhase& ph : g.def->phases) {
                EnemyDefinition d = *g.def;
                if (!ph.animation.empty()) d.animation = ph.animation;
                looks_.push_back(std::move(d));
            }
        }
        return looks_.empty() ? g.def : &looks_[static_cast<size_t>(std::clamp(phase, 0, static_cast<int>(looks_.size()) - 1))];
    }

    void drawSprite(const RaidSheetsLayout& L, const raid::GigabossSheet& g) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const int phases = std::max(1, static_cast<int>(g.phases.size()));
        bool hovered = false;
        // Each phase's look in turn (2.4 s each); activated: the next one.
        if (ui.hotspot("sprite", L.box, &hovered)) {
            phaseShown_ = (phaseShown_ + 1) % phases;
            phaseT_ = 0.0f;
        }
        phaseT_ += dt_;
        if (phaseT_ >= 2.4f) {
            phaseT_ = 0.0f;
            phaseShown_ = (phaseShown_ + 1) % phases;
        }
        const bool focus = ui.focused("sprite");
        r.fillRoundedRect(L.box, ui.s(12), Color{11, 18, 38, 255});
        r.drawRoundedRect(L.box, ui.s(12), focus || hovered ? ui.theme().accent : ui.theme().accent.withAlpha(50), std::max(1.0f, ui.s(focus ? 2.0f : 1.2f)));
        const Rect stage{L.box.x + ui.s(10), L.box.y + ui.s(10), L.box.w - ui.s(20), L.box.h - ui.s(52)};
        if (g.def) {
            r.pushClip(L.box);
            drawCreature(g_, *phaseLook(g, phaseShown_), stage, t_, Color::white(), "idle");
            r.popClip();
        }
        const Rect cap{L.box.x + ui.s(1), L.box.bottom() - ui.s(40), L.box.w - ui.s(2), ui.s(39)};
        r.fillRect(cap, Color{6, 10, 22, 200});
        std::string text = "Phase " + std::to_string(phaseShown_ + 1) + " / " + std::to_string(phases);
        if (phaseShown_ < static_cast<int>(g.phases.size())) text += " · " + g.phases[static_cast<size_t>(phaseShown_)].name;
        const float room = cap.w - ui.s(20);
        const float f = std::max(ui.fitFont(text, FontFace::Body, fz(ui, 14), room, 0.75f), 11.5f / ui.s(1.0f));
        ui.label(ui.elide(text, FontFace::Body, f, room), cap.center(), f, ui.theme().text, Align::Center, FontFace::Body, VAlign::Middle);
    }

    Color tintOf(const raid::GigabossSheet& g) const {
        if (!g.def) return kOrange;
        if (!g.def->phases.empty() && g.def->phases.front().lightColor.a > 0) return g.def->phases.front().lightColor;
        return g.def->lightColor;
    }

    /// The rows of its attacks list: the mechanisms of its routine, then its engine attacks.
    const std::vector<AttackListItem>& items(const raid::GigabossSheet& g) {
        if (itemsOf_ == g.id) return items_;
        itemsOf_ = g.id;
        items_.clear();
        const int phases = std::max(1, static_cast<int>(g.phases.size()));
        for (const raid::SheetMechanic& m : g.mechanics) {
            AttackListItem it;
            it.id = "m:" + m.id;
            it.name = m.name;
            it.where = raid::phasesLabel(m.phases, phases);
            it.summary = m.summary;
            it.description = m.text;
            if (!m.players.empty()) it.description += "\n" + m.players;  // a line each (each translated)
            it.avoid = m.hint;
            for (const raid::SheetFact& f : m.facts) it.facts.push_back({f.label, f.value});
            for (const raid::SheetFact& f : m.byPlayers) it.facts.push_back({m.byPlayersLabel + " · " + f.label, f.value});
            it.showable = !m.sketch.kind.empty();
            it.unshown = "Pas d'exemple animé pour ce mécanisme : sa description dit tout.";
            it.caption = raidSketchCaption(m.sketch);
            items_.push_back(std::move(it));
        }
        if (g.def) {
            std::vector<AttackListItem> engine = attackListItems(bossAttackEntries(*g.def, [this](const std::string& id) {
                const EnemyDefinition* e = g_.content.enemy(id);
                return e ? e->name : id;
            }));
            for (AttackListItem& it : engine) {
                if (const AttackDef* a = g.def->attack(it.id)) it.avoid = sheets_.avoidOf(attackTypeId(a->type));
                items_.push_back(std::move(it));
            }
        }
        return items_;
    }

    void drawGigaboss(const RaidSheetsLayout& L, const raid::GigabossSheet& g, float alpha) {
        UIContext& ui = g_.ui;
        drawSprite(L, g);
        ui.label(L.name.text, {L.name.r.x, L.name.r.center().y}, L.name.size, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        ui.label(L.subtitle.text, {L.subtitle.r.x, L.subtitle.r.center().y}, L.subtitle.size, kPeach, Align::Left, FontFace::Body, VAlign::Middle);
        drawChips(L);
        if (g.stars > 0) drawBossDifficulty(ui, L.difficulty, g.stars, "d'après sa définition, pour un joueur : il grandit avec le groupe");
        tabsOf(L, g.id);
        const int tab = tabs_[g.id];
        if (tab != 0) attacks_.stop();
        const Rect view = L.content;
        beginContent(view, g.id + "#" + std::to_string(tab));
        const float top = view.y - ui.scrollOffset();
        float y = top;
        const float w = view.w - ui.s(16);
        const Color text{216, 222, 240, 255};
        const int phaseCount = std::max(1, static_cast<int>(g.phases.size()));
        if (tab == 0) {
            Card about{"", "", tintOf(g), {}, {}, {}, {}};
            if (!g.description.empty()) about.lines.push_back({g.description, text});
            if (!g.summary.empty()) about.lines.push_back({"En bref : " + g.summary, kGold, FontFace::Bold});
            y += card(view, y, w, about) + ui.s(8);
            for (const raid::SheetPhase& p : g.phases) {
                Card c{std::to_string(p.number) + ". " + p.name, p.from, Color{255, 179, 71, 200}, {}, {}, {}, {}};
                if (!p.enterText.empty()) c.lines.push_back({"« " + p.enterText + " »", kPeach});
                if (!p.attacks.empty()) c.lines.push_back({"Attaques : " + join(p.attacks), text});
                if (!p.mechanics.empty()) c.lines.push_back({"Mécanismes : " + join(p.mechanics), kSky});
                y += card(view, y, w, c) + ui.s(8);
            }
            if (!g.lastStand.empty()) {
                Card c{"Dernier souffle", "à 0 PV, invincible", ui.theme().danger.withAlpha(220), {}, {}, {}, {}};
                if (!g.lastStandText.empty()) c.lines.push_back({g.lastStandText, kPeach});
                c.lines.push_back({"Attaques : " + join(g.lastStand), text});
                y += card(view, y, w, c) + ui.s(8);
            }
            y += ui.s(6);
            AttackList::Options o;
            o.scroll = "content";
            o.boss = g.def;
            o.engineShown = true;  // visible without having met it
            o.alpha = alpha;
            const Color tint = tintOf(g);
            o.drawExample = [this, &g, tint](const Rect& frame, const AttackListItem& it, float t) {
                for (const raid::SheetMechanic& m : g.mechanics)
                    if ("m:" + m.id == it.id) drawRaidSketch(g_, frame, m.sketch, g.fx, g.def, tint, t);
            };
            y = attacks_.draw(view, top, y, w, g.id, items(g), o);
            view_.attackRows = attacks_.rows();
        } else if (tab == 1) {
            for (const raid::SheetMechanic& m : g.mechanics) {
                if (m.kind != "collective") continue;
                Card c{m.name, raid::phasesLabel(m.phases, phaseCount), kGold, {}, {}, {}, {}};
                c.lines.push_back({m.text, text});
                if (!m.players.empty()) c.lines.push_back({m.players, kSky});
                if (!m.hint.empty()) c.lines.push_back({"Comment s'y prendre : " + m.hint, Color{196, 240, 210, 255}});
                c.facts = m.facts;
                for (const raid::SheetFact& f : m.byPlayers) c.facts.push_back({m.byPlayersLabel + " · " + f.label, f.value});
                y += card(view, y, w, c) + ui.s(8);
            }
            Card weak{"Points faibles", "À nu " + num(g.exposed) + " s · dégâts × " + num(g.exposedMult, 2), Color{255, 120, 150, 230}, {}, {}, {}, {}};
            for (const raid::SheetWeakPoint& p : g.weak) {
                weak.lines.push_back({std::to_string(p.count) + " × " + p.name, kPeach, FontFace::Bold});
                if (!p.description.empty()) weak.lines.push_back({p.description, text});
            }
            if (g.weakShare > 0.0f)
                weak.lines.push_back({"Chacun brisé lui ôte " + num(g.weakShare * 100.0f) + " % de sa vie, et compte pour qui le brise.", kGold});
            y += card(view, y, w, weak) + ui.s(8);
            Card scale{"Sa force selon le groupe", "", kSky, {}, {}, {}, {}};
            for (const std::string& l : g.scaling) scale.lines.push_back({l, text});
            y += card(view, y, w, scale) + ui.s(8);
            Card counts{"Ce qui compte dans vos contributions", "", kGreen, {}, {}, {}, {}};
            for (const std::string& l : g.contributions) counts.lines.push_back({l, text});
            y += card(view, y, w, counts) + ui.s(8);
        } else {
            Card where{"Où il vit", "", kSky, {}, {}, {}, {}};
            where.facts.push_back({"Branche", g.branchName + " · " + std::to_string(g.zones) + " zones"});
            if (g.branchMinutes > 0.0f) where.facts.push_back({"Durée de la branche", "environ " + num(g.branchMinutes, 0) + " min à huit joueurs, hors gigaboss"});
            if (!g.expedition.empty())
                where.facts.push_back({"L'expédition", "« " + g.expedition + " », environ " + num(g.expeditionMinutes, 0) + " min à huit joueurs"});
            where.facts.push_back({"Son antre", "zone " + std::to_string(g.lairZone) + ", « " + g.lairName + " »"});
            if (!g.objective.empty()) where.facts.push_back({"Objectif", g.objective});
            if (!g.lairSub.empty()) where.lines.push_back({g.lairSub, kPeach});
            y += card(view, y, w, where) + ui.s(8);
            Card fall{"Sa chute et Asterion", "", Color{255, 120, 90, 230}, {}, {}, {}, {}};
            if (!g.fallText.empty()) fall.lines.push_back({g.fallText, kPeach, FontFace::Bold});
            if (!g.effect.empty()) fall.lines.push_back({"Sur Asterion : " + g.effect, text});
            fall.lines.push_back({"La partie est détruite pour tout l'événement dès que le serveur valide sa chute.", kDim});
            y += card(view, y, w, fall) + ui.s(8);
            if (!g.links.empty()) {
                Card links{"Liens avec le reste du titan", "", Color{255, 138, 184, 230}, {}, {}, {}, {}};
                for (const std::string& l : g.links) links.lines.push_back({l, text});
                y += card(view, y, w, links) + ui.s(8);
            }
            Card rewards{"Récompenses", "", kGold, {}, {}, {}, {}};
            for (const raid::SheetReward& rw : g.rewards) {
                if (rw.kind == "badge") rewards.lines.push_back({"Badge « " + rw.name + " »", kGold, FontFace::Bold});
                else rewards.lines.push_back({rw.name, kGold, FontFace::Bold});
                rewards.lines.push_back({rw.text, text});
            }
            rewards.lines.push_back({"Les récompenses communes à tout le raid sont sur la fiche d'Asterion.", kDim});
            rewards.button = "Fiche d'Asterion";
            rewards.buttonId = "asterion";
            bool pressed = false;
            y += card(view, y, w, rewards, &pressed) + ui.s(8);
            if (pressed) open(kTitan);
        }
        endContent(top, y);
    }

    static std::string join(const std::vector<std::string>& v) {
        std::string out;
        for (const std::string& s : v) out += (out.empty() ? "" : " · ") + s;
        return out;
    }

    GameServices& g_;
    std::string wanted_;
    int wantedTab_ = 0;
    MenuBackground bg_;
    raid::RaidSheets sheets_;
    std::vector<std::string> ids_;
    int selected_ = 0;
    std::map<std::string, int> tabs_;
    float t_ = 0.0f, dt_ = 0.0f;
    AsterionFigure titan_;
    AttackList attacks_;
    std::shared_ptr<const CreatureAtlas> atlas_;
    std::string itemsOf_;
    std::vector<AttackListItem> items_;
    std::string looksOf_;
    std::vector<EnemyDefinition> looks_;
    int phaseShown_ = 0;
    float phaseT_ = 0.0f;
    std::string contentOf_;
    float contentH_ = 0.0f;
    RaidSheetsView view_;
};

}  // namespace

RaidSheetsView raidSheetsView(const Scene* s) {
    const auto* r = dynamic_cast<const RaidSheetsScene*>(s);
    return r ? r->view() : RaidSheetsView{};
}

std::unique_ptr<Scene> makeRaidSheets(GameServices& g, const std::string& sheet, int tab) { return std::make_unique<RaidSheetsScene>(g, sheet, tab); }

}  // namespace cosmic
