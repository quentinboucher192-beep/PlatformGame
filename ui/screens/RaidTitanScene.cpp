// 2.68: the global map of the titan (docs/RAIDS.md, maquette « Carte du titan »): the top level of
// the raid's three maps. The whole Asterion with its seven branches in the state the server
// gives (unknown until a group goes there, under attack while groups fight there, weakened once
// half of its zones are taken), and around it:
//
// * at the left, the titan's progress (zones controlled out of 56, objectives, gigaboss still
//   sealed, groups, players and clans in combat) and its links between branches (« Effets sur
//   Asterion »: what each branch did to the others, or how far it is);
// * at the right, the seven branches (state, zones, groups there; one focus stop each: Entrée /
//   A / a click opens the branch's map), the zones that ask for another group, the alerts of the
//   whole titan (objectives accepted, links on);
// * at the bottom, « Rejoindre » (my place, or where my clan needs me: the server places) and
//   « Répartition du clan » (where my clan's players are; the raid leader moves them).
//
// The titan itself (ui/widgets/AsterionFigure) answers the mouse and the focus part by part: a
// branch opens its map too. Everything comes from GET /raids/events/{id}/titan, every 10 s. Every
// text is measured into its room (raidui::fit, wrap): nothing overlaps.

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/online/AsterionBody.h"
#include "game/online/Raids.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/AsterionFilm.h"
#include "ui/widgets/AsterionFigure.h"

namespace cosmic {

namespace {

using namespace raidui;

const Color kFog{122, 106, 200, 255};
const Color kRed{255, 138, 138, 255};

/// The words of a branch's state on the map.
std::string stateWord(const std::string& s) {
    if (s == "attaque") return "combat en cours";
    if (s == "affaibli") return "affaiblie";
    if (s == "detruit") return "détruite";
    if (s == "inconnu") return "pas encore découverte";
    return "découverte";
}

Color stateColor(const std::string& s) {
    if (s == "attaque") return kOrange;
    if (s == "affaibli") return kGreen;
    if (s == "inconnu") return kFog;
    return kPeach;
}

/// « 21:07 » at this computer's hour, from a time of the server.
std::string clockOf(const std::string& iso) {
    const auto t = raids::parseUtc(iso);
    return t ? raids::hhmm(raids::toSystemLocal(*t)) : std::string();
}

class RaidTitanScene : public Scene {
public:
    RaidTitanScene(GameServices& g, std::string eventId) : g_(g), eventId_(std::move(eventId)) {}

    void onEnter() override {
        bg_.init(g_);
        g_.ui.setFocus("titan/branch0");
        load();
        asterionfilm::hold(g_);  // 2.68: Asterion's film first (its first viewing), over the map from its first frame
    }
    void onResume() override { load(); }

    void update(float dt) override {
        bg_.update(dt);
        timer_ += dt;
        if (g_.app.topScene() == this && timer_ >= 10.0f) load();  // the groups move: every 10 s
    }

    void load() {
        timer_ = 0.0f;
        const int gen = ++gen_;
        g_.online.get("/api/v1/raids/events/" + eventId_ + "/titan", OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                          if (gen != gen_) return;
                          if (!r.ok) {
                              error_ = r.message;
                              return;
                          }
                          data_ = r.data;
                          loaded_ = true;
                          error_.clear();
                      }),
                      false);
    }

    Json branch(const std::string& part) const {
        for (const auto& b : data_.value("branches", Json::array()))
            if (jstr(b, "part") == part) return b;
        return Json::object();
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.6f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{12, 8, 22, 160});
        ui.inputEnabled = g_.app.topScene() == this;
        ui.pushId("titan");
        bool back = false;
        const float W = ui.screenW(), H = ui.screenH(), m = ui.s(24);
        // The header: « Retour », the title, the event's state at the right (measured first).
        const float bw = textW(ui, "‹ Retour", 16) + ui.s(36);
        if (ui.button("back", {m, ui.s(18), bw, ui.s(44)}, "‹ Retour")) back = true;
        const float hx = m + bw + ui.s(16), hy = ui.s(40);
        std::string open;
        if (!loaded_) {
            fit(ui, error_.empty() ? "Chargement du titan…" : error_, {hx, hy}, 17, W - m - hx, error_.empty() ? kDim : ui.theme().danger);
        } else {
            const std::string st = jstr(data_.value("event", Json::object()), "state");
            const std::string state = st == "en_cours" ? "Événement en cours" : st == "termine" ? "Événement terminé" : "Événement à venir";
            const float sw = std::min(textW(ui, state, 14, FontFace::Bold) + ui.s(22), (W - m - hx) * 0.3f);
            pill(ui, W - m - sw, hy, state, st == "en_cours" ? kOrange : Color{42, 58, 122, 255}, st == "en_cours" ? kInk : kText, W - m + ui.s(1), 14);
            const float tw = fit(ui, "ASTERION", {hx, hy}, 30, (W - m - sw - hx) * 0.45f, kText, FontFace::Title).x;
            fit(ui, "Carte globale du titan · cliquez une branche pour voir ses 8 zones", {hx + tw + ui.s(16), hy}, 15, W - m - sw - ui.s(16) - hx - tw - ui.s(16),
                kPeach);
            // The columns: the progress at the left, the branches at the right, the titan between.
            const float top = ui.s(78), bottom = H - ui.s(84), gap = ui.s(16);
            const float leftW = std::clamp(W * 0.25f, ui.s(280), ui.s(400));
            float rightW = std::clamp(W * 0.28f, ui.s(320), ui.s(450));
            float centerW = W - 2 * m - leftW - rightW - 2 * gap;
            if (centerW < ui.s(240)) {  // narrow screen: no titan, the lists take its room
                rightW += std::max(0.0f, centerW) + gap;
                centerW = 0.0f;
            }
            const Rect left{m, top, leftW, bottom - top};
            const Rect right{W - m - rightW, top, rightW, bottom - top};
            drawLeft(left);
            open = drawRight(right);
            if (centerW > 0.0f) {
                const std::string clicked = drawTitan({left.right() + gap, top, centerW, bottom - top});
                if (!clicked.empty()) open = clicked;
            }
            drawBottom({m, bottom + ui.s(12), W - 2 * m, H - bottom - ui.s(24)});
        }
        titan_.drawTooltip(g_);
        ui.popId();
        if (!back && ui.backPressed()) back = true;
        ui.end();
        if (back) {
            g_.app.popScene();
            return;
        }
        if (!open.empty()) g_.app.pushScene(makeRaidBranch(g_, eventId_, open));
    }

    /// The titan's progress and its links between branches.
    void drawLeft(const Rect& r) {
        UIContext& ui = g_.ui;
        Renderer& rr = ui.r();
        const Json t = data_.value("totals", Json::object());
        const float pad = ui.s(14), x = r.x + pad, w = r.w - pad * 2;
        // Its height first: the big count, the bar, four lines.
        const float progH = ui.s(46) + ui.s(44) + ui.s(16) + 4 * ui.s(22) + ui.s(8);
        const Rect prog{r.x, r.y, r.w, std::min(progH, r.h)};
        float y = section(ui, prog, "PROGRESSION DU TITAN", kGold);
        const int zc = jget<int>(t, "zones_controlled", 0), zn = std::max(1, jget<int>(t, "zones", 56));
        const float big = fit(ui, std::to_string(zc) + " / " + std::to_string(zn), {x, y + ui.s(18)}, 34, w * 0.45f, kText, FontFace::Title).x;
        fit(ui, "zones contrôlées", {x + big + ui.s(10), y + ui.s(20)}, 15, w - big - ui.s(10), kDim);
        y += ui.s(44);
        rr.fillRoundedRect({x, y, w, ui.s(8)}, ui.s(4), Color{40, 34, 66, 255});
        rr.fillRoundedRect({x, y, w * std::clamp(static_cast<float>(zc) / static_cast<float>(zn), 0.0f, 1.0f), ui.s(8)}, ui.s(4), kGreen);
        y += ui.s(16);
        // 2.69 (phase 4B): the lairs open (their gigaboss fought), the others still sealed.
        int open = 0;
        for (const auto& b : data_.value("branches", Json::array())) open += jget<bool>(b, "sealed", true) ? 0 : 1;
        const std::vector<std::pair<std::string, Color>> lines = {
            {"Gigaboss vaincus : " + std::to_string(jget<int>(t, "gigaboss_defeated", 0)) + " / " + std::to_string(jget<int>(t, "gigaboss", 7)) + " · " +
                 std::to_string(open) + (open > 1 ? " antres ouvertes" : " antre ouverte") +
                 (open < jget<int>(t, "gigaboss", 7) ? ", les autres portes restent scellées (prochaine version)" : ""),
             kPeach},
            {"Objectifs accomplis : " + std::to_string(jget<int>(t, "objectives_done", 0)) + " / " + std::to_string(jget<int>(t, "objectives", 0)), kText},
            {"En combat : " + std::to_string(jget<int>(t, "groups", 0)) + " groupes · " + std::to_string(jget<int>(t, "players", 0)) + " joueurs · " +
                 std::to_string(jget<int>(t, "clans", 0)) + " clans",
             kText},
            {"Liens du titan actifs : " + std::to_string(jget<int>(t, "links_on", 0)) + " / " + std::to_string(jget<int>(t, "links", 0)), kGreen}};
        for (const auto& [text, c] : lines) {
            if (y + ui.s(22) > prog.bottom()) break;
            fit(ui, text, {x, y + ui.s(11)}, 14, w, c);
            y += ui.s(22);
        }
        // The links: those on first, then how far the others are.
        const Rect fx{r.x, prog.bottom() + ui.s(12), r.w, r.bottom() - prog.bottom() - ui.s(12)};
        if (fx.h < ui.s(90)) return;
        y = section(ui, fx, "EFFETS SUR ASTERION", kGold);
        std::vector<Json> links;
        for (const auto& l : data_.value("links", Json::array())) links.push_back(l);
        std::stable_sort(links.begin(), links.end(), [](const Json& a, const Json& b) { return jget<bool>(a, "on", false) > jget<bool>(b, "on", false); });
        const float markW = ui.s(42);
        int shown = 0;
        for (const auto& l : links) {
            const bool on = jget<bool>(l, "on", false);
            const auto ls = wrap(ui, jstr(l, "label"), FontFace::Body, 13, w - markW, 3);
            const float h = static_cast<float>(ls.size()) * ui.s(18) + ui.s(8);
            if (y + h > fx.bottom() - ui.s(26)) break;
            const Json pr = l.value("progress", Json::object());
            const std::string mark = on ? "✓" : std::to_string(jget<int>(pr, "done", 0)) + " / " + std::to_string(jget<int>(pr, "needed", 1));
            fit(ui, mark, {x, y + ui.s(9)}, 13, markW - ui.s(6), on ? kGreen : kGold, FontFace::Bold);
            drawLines(ui, ls, {x + markW, y}, 13, ui.s(18), on ? kText : kDim);
            y += h;
            ++shown;
        }
        const int rest = static_cast<int>(links.size()) - shown;
        if (rest > 0) fit(ui, "+ " + std::to_string(rest) + (rest > 1 ? " autres liens (cartes des branches)" : " autre lien (cartes des branches)"), {x, fx.bottom() - ui.s(16)}, 12, w, kDim);
    }

    /// The titan in the middle, the groups of each branch as badges by their part, the legend.
    std::string drawTitan(const Rect& r) {
        UIContext& ui = g_.ui;
        const float legendH = ui.s(26);
        std::map<std::string, asterion::State> states;
        const Json body = data_.value("body", Json::object());
        for (const auto& it : body.items())
            if (it.value().is_string()) states[it.key()] = asterion::stateFromString(it.value().get<std::string>());
        std::vector<std::string> parts;
        for (const auto& b : data_.value("branches", Json::array())) parts.push_back(jstr(b, "part"));
        const std::string clicked = titan_.draw(g_, "figure", {r.x, r.y, r.w, r.h - legendH - ui.s(8)}, states, parts);
        // The groups fighting on each part (gold: my clan's there), never over one another.
        std::vector<Rect> placed;
        const Rect fig = titan_.bounds();
        for (const auto& b : data_.value("branches", Json::array())) {
            const int groups = jget<int>(b, "groups", 0);
            Vec2 a;
            if (groups <= 0 || !titan_.anchorOf(jstr(b, "part"), a)) continue;
            const bool mine = !b.value("my_groups", Json::array()).empty();
            const std::string t = std::to_string(groups) + (groups > 1 ? " groupes" : " groupe");
            const float w = textW(ui, t, 12, FontFace::Bold) + ui.s(16), h = ui.s(22);
            Rect box{a.x - w * 0.5f, a.y + ui.s(10), w, h};
            box.x = std::clamp(box.x, fig.x, std::max(fig.x, fig.right() - w));
            bool free = false;
            for (int k = 0; k < 6 && !free; ++k) {
                free = std::none_of(placed.begin(), placed.end(), [&](const Rect& p) { return p.intersects(box.expanded(ui.s(2))); });
                if (!free) box.y += h + ui.s(4);
            }
            if (!free || box.bottom() > fig.bottom()) continue;
            placed.push_back(box);
            ui.r().fillRoundedRect(box, ui.s(8), mine ? kGold : kSky);
            fit(ui, t, box.center(), 12, box.w - ui.s(8), kInk, FontFace::Bold, Align::Center);
        }
        // The legend under the titan: one line, the size that fits it (the items that do not fit wait).
        const std::vector<std::pair<std::string, Color>> items = {{"● combat en cours", kOrange},
                                                                  {"● affaiblie", kGreen},
                                                                  {"● découverte", kPeach},
                                                                  {"● pas encore découverte", kFog},
                                                                  {"■ groupes (votre clan en or)", kGold}};
        float total = 0.0f;
        for (const auto& [t, c] : items) total += textW(ui, t, 13) + ui.s(14);
        const float size = total > r.w ? std::max(10.0f, 13.0f * r.w / total) : 13.0f;
        float x = r.x + std::max(0.0f, (r.w - total * size / 13.0f) * 0.5f);
        for (const auto& [t, c] : items) {
            const float w = textW(ui, t, size);
            if (x + w > r.right()) break;
            fit(ui, t, {x, r.bottom() - legendH * 0.5f}, size, w + ui.s(2), c);
            x += w + ui.s(14) * size / 13.0f;
        }
        return clicked;
    }

    /// The seven branches (focus stops), the zones that ask for help, the titan's alerts.
    std::string drawRight(const Rect& r) {
        UIContext& ui = g_.ui;
        Renderer& rr = ui.r();
        std::string open;
        const Json branches = data_.value("branches", Json::array());
        const Json heart = data_.value("heart", Json());  // 2.72 (phase 6): the Cœur d'Asterion, its own row
        const float rowH = ui.s(50), pad = ui.s(12);
        const float listH = std::min(r.h, ui.s(46) + rowH * static_cast<float>(branches.size() + (heart.is_object() ? 1 : 0)) + ui.s(10));
        const Rect list{r.x, r.y, r.w, listH};
        float y = section(ui, list, "LES SEPT BRANCHES", kGold);
        int i = 0;
        for (const auto& b : branches) {
            if (y + rowH > list.bottom()) break;
            const Rect row{r.x + ui.s(8), y, r.w - ui.s(16), rowH - ui.s(4)};
            bool hovered = false;
            const std::string id = "branch" + std::to_string(i++);
            if (ui.hotspot(id, row, &hovered)) open = jstr(b, "part");
            const bool focus = ui.focused(id);
            rr.fillRoundedRect(row, ui.s(10), focus || hovered ? Color{58, 40, 96, 255} : Color{30, 26, 58, 255});
            if (focus) rr.drawRoundedRect(row, ui.s(10), kGold, ui.s(2));
            const std::string st = jstr(b, "state");
            rr.fillRoundedRect({row.x + ui.s(6), row.y + ui.s(8), ui.s(5), row.h - ui.s(16)}, ui.s(2), stateColor(st));
            const float ix = row.x + ui.s(18), iw = row.w - ui.s(18) - pad;
            // The right column first (state, zones and groups), the name in the room left.
            const int groups = jget<int>(b, "groups", 0);
            const std::string r1 = stateWord(st);
            const std::string r2 = std::to_string(jget<int>(b, "zones_controlled", 0)) + " / " + std::to_string(jget<int>(b, "zones", 8)) + " zones · " +
                                   std::to_string(groups) + (groups > 1 ? " groupes" : " groupe");
            const float rw = std::min(std::max(textW(ui, r1, 12, FontFace::Bold), textW(ui, r2, 12)), iw * 0.5f);
            fit(ui, r1, {row.right() - pad, row.y + ui.s(15)}, 12, rw, stateColor(st), FontFace::Bold, Align::Right);
            fit(ui, r2, {row.right() - pad, row.y + ui.s(32)}, 12, rw, kDim, FontFace::Body, Align::Right);
            fit(ui, jstr(b, "name"), {ix, row.y + ui.s(15)}, 15, iw - rw - ui.s(10), kText, FontFace::Bold);
            // 2.68 (phase 3): Pyroclast (the Bras droit) is fought; the six others stay sealed.
            const std::string gstate = jget<bool>(b, "gigaboss_down", false) ? " · vaincu" : jget<bool>(b, "sealed", true) ? " · scellé" : " · dans son antre";
            fit(ui, "Gigaboss : " + jstr(b, "gigaboss") + gstate, {ix, row.y + ui.s(32)}, 12, iw - rw - ui.s(10), kPeach);
            y += rowH;
        }
        if (heart.is_object() && y + rowH <= list.bottom() + ui.s(1)) drawHeartRow(heart, {r.x + ui.s(8), y, r.w - ui.s(16), rowH - ui.s(4)});
        float below = list.bottom() + ui.s(12);
        // The zones where a group waits for another one (two groups required).
        const Json needs = data_.value("needs", Json::array());
        if (!needs.empty() && below + ui.s(110) < r.bottom()) {
            std::vector<std::vector<std::string>> lines;
            float h = ui.s(46) + ui.s(10);
            for (const auto& n : needs) {
                const std::string name = jstr(branch(jstr(n, "branch")), "name");
                lines.push_back(wrap(ui,
                                     name + " · " + jstr(n, "name") + " : " + std::to_string(jget<int>(n, "groups", 1)) + " groupe sur " +
                                         std::to_string(jget<int>(n, "required", 2)) + ", un renfort est attendu",
                                     FontFace::Body, 13, r.w - ui.s(28), 2));
                h += static_cast<float>(lines.back().size()) * ui.s(18) + ui.s(4);
            }
            const Rect box{r.x, below, r.w, std::min(h, (r.bottom() - below) * 0.45f)};
            float ny = section(ui, box, "RENFORTS DEMANDÉS", kRed);
            for (const auto& l : lines) {
                const float lh = static_cast<float>(l.size()) * ui.s(18);
                if (ny + lh > box.bottom() - ui.s(6)) break;
                ny += drawLines(ui, l, {r.x + ui.s(14), ny}, 13, ui.s(18), kText) + ui.s(4);
            }
            below = box.bottom() + ui.s(12);
        }
        // The alerts of the whole titan.
        const Rect feed{r.x, below, r.w, r.bottom() - below};
        if (feed.h < ui.s(90)) return open;
        float fy = section(ui, feed, "ALERTES DU TITAN", kGold);
        const float timeW = textW(ui, "00:00", 12, FontFace::Bold) + ui.s(10);
        const Json items = data_.value("feed", Json::array());
        if (items.empty()) fit(ui, "Aucun objectif accompli pour l'instant.", {r.x + ui.s(14), fy + ui.s(9)}, 13, r.w - ui.s(28), kDim);
        for (const auto& f : items) {
            const bool link = jstr(f, "kind") == "link";
            const std::string by = jstr(f, "by");
            const std::string text = link ? "TITAN · " + jstr(f, "text")
                                          : (by.empty() ? "" : by + " · ") + jstr(f, "branch_name") + " : " + jstr(f, "text");
            const auto ls = wrap(ui, text, FontFace::Body, 13, r.w - ui.s(28) - timeW, 2);
            const float lh = static_cast<float>(ls.size()) * ui.s(18);
            if (fy + lh > feed.bottom() - ui.s(8)) break;
            fit(ui, clockOf(jstr(f, "at")), {r.x + ui.s(14), fy + ui.s(9)}, 12, timeW - ui.s(6), kDim, FontFace::Bold, Align::Left, true);
            drawLines(ui, ls, {r.x + ui.s(14) + timeW, fy}, 13, ui.s(18), link ? kGreen : kText);
            fy += lh + ui.s(6);
        }
        return open;
    }

    /// 2.72 (phase 6): the Cœur d'Asterion's row: sealed until the seven gigaboss fell (k / 7), then its
    /// shared health (every group's damage, validated) and its phase; Entrée / A / a click joins it.
    void drawHeartRow(const Json& h, const Rect& row) {
        UIContext& ui = g_.ui;
        Renderer& rr = ui.r();
        bool hovered = false;
        const std::string state = jstr(h, "state");
        const bool open = state == "ouvert" || state == "combat";
        if (ui.hotspot("heart", row, &hovered) && open) joinHeart();
        const bool focus = ui.focused("heart");
        rr.fillRoundedRect(row, ui.s(10), focus || hovered ? Color{74, 52, 30, 255} : Color{44, 32, 26, 255});
        rr.drawRoundedRect(row, ui.s(10), focus ? kGold : Color{255, 210, 122, 120}, ui.s(focus ? 2.0f : 1.0f));
        const float pad = ui.s(12), ix = row.x + ui.s(18), iw = row.w - ui.s(18) - pad;
        const Color sc = state == "tombe" ? kGreen : open ? kOrange : kFog;
        rr.fillRoundedRect({row.x + ui.s(6), row.y + ui.s(8), ui.s(5), row.h - ui.s(16)}, ui.s(2), sc);
        const int left = std::clamp(jget<int>(h, "health", 10000), 0, 10000), done = jget<int>(h, "gigaboss_defeated", 0);
        const std::string r1 = state == "tombe" ? "tombé" : open ? std::to_string(left / 100) + "," + std::to_string((left % 100) / 10) + " %" : "scellé";
        const std::string r2 = open ? "phase " + std::to_string(std::clamp(jget<int>(h, "phase", 1), 1, 4)) + " / 4 · " + std::to_string(jget<int>(h, "groups", 0)) +
                                          (jget<int>(h, "groups", 0) > 1 ? " groupes" : " groupe")
                                    : std::to_string(done) + " / 7 gigaboss";
        const float rw = std::min(std::max(textW(ui, r1, 12, FontFace::Bold), textW(ui, r2, 12)), iw * 0.5f);
        fit(ui, r1, {row.right() - pad, row.y + ui.s(15)}, 12, rw, sc, FontFace::Bold, Align::Right);
        fit(ui, r2, {row.right() - pad, row.y + ui.s(32)}, 12, rw, kDim, FontFace::Body, Align::Right);
        fit(ui, "CŒUR D'ASTERION", {ix, row.y + ui.s(15)}, 15, iw - rw - ui.s(10), kGold, FontFace::Bold);
        const std::string sub = state == "tombe" ? "Asterion est tombé : victoire de tous les clans"
                                : open           ? "Combat final : vie partagée par tous les groupes"
                                                 : "S'ouvre quand les sept gigaboss sont tombés";
        fit(ui, sub, {ix, row.y + ui.s(32)}, 12, iw - rw - ui.s(10), kPeach);
        if (open) {  // its shared health, under the row's texts
            const Rect bar{ix, row.bottom() - ui.s(7), iw, ui.s(4)};
            rr.fillRect(bar, Color{60, 44, 40, 255});
            rr.fillRect({bar.x, bar.y, bar.w * static_cast<float>(left) / 10000.0f, bar.h}, kGold);
        }
    }

    void joinHeart() {
        if (busy_) return;
        const Json my = data_.value("my", Json::object());
        if (!jget<bool>(my, "registered", false) || jstr(data_.value("event", Json::object()), "state") != "en_cours") {
            status_ = "Le combat final se joue pendant l'événement, pour les inscrits.";
            statusError_ = true;
            return;
        }
        busy_ = true;
        status_ = "Placement dans un groupe du Cœur d'Asterion…";
        statusError_ = false;
        g_.online.send("POST", "/api/v1/raids/events/" + eventId_ + "/branches/coeur/join", Json::object(),
                       OnlineService::guard(life_, [this](const ApiResult& r) {
                           busy_ = false;
                           if (!r.ok) {
                               status_ = r.message;
                               statusError_ = true;
                               return;
                           }
                           const Json i = r.data.value("instance", Json::object());
                           RaidEntry e;
                           e.eventId = eventId_;
                           e.instanceId = jstr(i, "id");
                           e.clanTag = jstr(i, "clan_tag");
                           e.group = jget<int>(i, "group", 1);
                           e.branch = jstr(i, "part", "coeur");
                           status_.clear();
                           g_.app.switchScene(makeRaidGame(g_, e), 0.35f);
                       }));
    }

    /// « Rejoindre » (my place, or where the clan needs me), « Répartition du clan », the note.
    void drawBottom(const Rect& r) {
        UIContext& ui = g_.ui;
        const Json my = data_.value("my", Json::object());
        const bool registered = jget<bool>(my, "registered", false);
        const bool running = jstr(data_.value("event", Json::object()), "state") == "en_cours";
        const Json place = my.value("placement", Json());
        const std::string placeName = place.is_object() ? jstr(branch(jstr(place, "branch")), "name") : "";
        const std::string joinLabel = !placeName.empty() ? "Rejoindre ma place · " + placeName : "Rejoindre là où le clan a besoin de moi";
        const std::string rosterLabel = "Répartition du clan";
        const float bh = std::min(r.h, ui.s(52));
        const float jw = std::min(textW(ui, joinLabel, 16, FontFace::Bold) + ui.s(44), r.w * 0.42f);
        const float rw = std::min(textW(ui, rosterLabel, 16, FontFace::Bold) + ui.s(44), r.w * 0.25f);
        UIContext::ButtonStyle js;
        js.primary = true;
        js.enabled = registered && running && !busy_;
        if (ui.button("join", {r.x, r.y, jw, bh}, joinLabel, js)) join();
        UIContext::ButtonStyle rs;
        rs.enabled = registered;
        if (ui.button("roster", {r.x + jw + ui.s(12), r.y, rw, bh}, rosterLabel, rs)) g_.app.pushScene(makeRaidRoster(g_, eventId_));
        std::string note = status_;
        if (note.empty()) {
            if (!running) note = "Le raid n'est pas en cours : les combats se jouent pendant l'événement.";
            else if (!registered) note = "Inscrivez-vous au raid (fiche de l'événement) pour combattre.";
            else if (place.is_object())
                note = "Votre place : " + placeName + " · " + jstr(place, "group") + ". Chaque groupe compte 8 joueurs au plus.";
            else note = "Choisissez une branche sur le titan, ou laissez le serveur vous placer là où le clan a besoin de vous.";
        }
        const float nx = r.x + jw + rw + ui.s(28);
        const auto lines = wrap(ui, note, FontFace::Body, 14, r.right() - nx, 2);
        drawLines(ui, lines, {nx, r.y + (bh - static_cast<float>(lines.size()) * ui.s(20)) * 0.5f}, 14, ui.s(20), statusError_ ? ui.theme().danger : kDim);
    }

    void join() {
        busy_ = true;
        status_ = "Placement dans un groupe…";
        statusError_ = false;
        g_.online.send("POST", "/api/v1/raids/events/" + eventId_ + "/join", Json::object(), OnlineService::guard(life_, [this](const ApiResult& r) {
                           busy_ = false;
                           if (!r.ok) {
                               status_ = r.message;
                               statusError_ = true;
                               return;
                           }
                           const Json i = r.data.value("instance", Json::object());
                           RaidEntry e;
                           e.eventId = eventId_;
                           e.instanceId = jstr(i, "id");
                           e.clanTag = jstr(i, "clan_tag");
                           e.group = jget<int>(i, "group", 1);
                           e.branch = jstr(i, "part", "bras_droit");
                           status_.clear();
                           g_.app.switchScene(makeRaidGame(g_, e), 0.35f);
                       }));
    }

    GameServices& g_;
    std::string eventId_;
    MenuBackground bg_;
    AsterionFigure titan_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    Json data_;
    bool loaded_ = false, busy_ = false, statusError_ = false;
    int gen_ = 0;
    float timer_ = 0.0f;
    std::string error_, status_;
};

}  // namespace

std::unique_ptr<Scene> makeRaidTitan(GameServices& g, const std::string& eventId) { return std::make_unique<RaidTitanScene>(g, eventId); }

}  // namespace cosmic
