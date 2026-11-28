// 2.68: where the players of my clan are in the raid (docs/RAIDS.md, spec §8): each registered
// member and their place (branch, group, zone, in play or not). §41 (titan 16): ALWAYS seven groups, one
// per branch, 2 to 8 players each. The raid leader, the chief and the officers move the member selected
// to another group (one of the seven, by the branch it plays now) and even out the seven groups (sizes
// that differ by one at most). Everything is checked by the server (GET / POST /raids/events/{id}/
// placements): never fewer than 2 nor more than 8 in a group; the buttons that would break it are off.
//
// Keyboard, gamepad and mouse: the members are focus stops (a scrolling list), the branch buttons
// too; Échap / B goes back. Every text is measured into its room (raidui::fit, wrap).

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/online/Raids.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

using namespace raidui;

class RaidRosterScene : public Scene {
public:
    RaidRosterScene(GameServices& g, std::string eventId) : g_(g), eventId_(std::move(eventId)) {}

    void onEnter() override {
        bg_.init(g_);
        g_.ui.setFocus("roster/m0");
        load();
    }

    void update(float dt) override {
        bg_.update(dt);
        timer_ += dt;
        if (g_.app.topScene() == this && timer_ >= 15.0f) load();
    }

    void load() {
        timer_ = 0.0f;
        const int gen = ++gen_;
        g_.online.get("/api/v1/raids/events/" + eventId_ + "/placements", OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                          if (gen != gen_) return;
                          if (!r.ok) {
                              error_ = r.message;
                              return;
                          }
                          data_ = r.data;
                          loaded_ = true;
                          error_.clear();
                          const Json members = data_.value("members", Json::array());
                          if (selected_.empty() && !members.empty()) selected_ = jstr(members[0], "id");
                      }),
                      false);
    }

    std::string branchName(const std::string& part) const {
        for (const auto& b : data_.value("branches", Json::array()))
            if (jstr(b, "part") == part) return jstr(b, "name");
        return part;
    }

    Json member(const std::string& id) const {
        for (const auto& m : data_.value("members", Json::array()))
            if (jstr(m, "id") == id) return m;
        return Json();
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.6f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{12, 8, 22, 170});
        ui.inputEnabled = g_.app.topScene() == this;
        ui.pushId("roster");
        bool back = false;
        const float W = ui.screenW(), H = ui.screenH(), m = ui.s(24);
        const float bw = textW(ui, "‹ Retour", 16) + ui.s(36);
        if (ui.button("back", {m, ui.s(18), bw, ui.s(44)}, "‹ Retour")) back = true;
        const float hx = m + bw + ui.s(16), hy = ui.s(40);
        if (!loaded_) {
            fit(ui, error_.empty() ? "Chargement de la répartition…" : error_, {hx, hy}, 17, W - m - hx, error_.empty() ? kDim : ui.theme().danger);
        } else {
            const Json members = data_.value("members", Json::array());
            int placed = 0;
            for (const auto& mb : members) placed += !jstr(mb, "branch").empty();
            const std::string count = std::to_string(placed) + " placés sur " + std::to_string(members.size()) + " inscrits";
            const float cw = std::min(textW(ui, count, 14), (W - m - hx) * 0.35f);
            fit(ui, count, {W - m, hy}, 14, cw, kDim, FontFace::Body, Align::Right);
            const float tw = fit(ui, "RÉPARTITION DU CLAN", {hx, hy}, 26, (W - m - cw - hx) * 0.6f, kText, FontFace::Title).x;
            fit(ui, "[" + jstr(data_, "clan_tag") + "] · 7 groupes de 2 à 8 joueurs, un par branche", {hx + tw + ui.s(14), hy}, 15,
                W - m - cw - ui.s(16) - hx - tw - ui.s(14), kPeach, FontFace::Body, Align::Left, false);
            const float top = ui.s(78), bottom = H - ui.s(24), gap = ui.s(16);
            const float leftW = std::clamp(W * 0.36f, ui.s(300), ui.s(520));
            drawGroups({m, top, leftW, bottom - top});
            drawMembers({m + leftW + gap, top, W - 2 * m - leftW - gap, bottom - top});
        }
        ui.popId();
        if (!back && ui.backPressed()) back = true;
        ui.end();
        if (back) g_.app.popScene();
    }

    /// §41: the instance of a group in play (its zone, its players in play), null.
    Json instanceOf(const std::string& id) const {
        for (const auto& i : data_.value("instances", Json::array()))
            if (jstr(i, "id") == id) return i;
        return Json();
    }

    /// §41: the clan's seven groups: their branch now, placed out of 8, in play, the zone; waiting, eliminated.
    void drawGroups(const Rect& r) {
        UIContext& ui = g_.ui;
        float y = section(ui, r, "GROUPES DU CLAN", kGold);
        const Json groups = data_.value("groups", Json::array());
        const float rowH = ui.s(48), x = r.x + ui.s(14), w = r.w - ui.s(28);
        if (groups.empty()) {
            const int n = jget<int>(data_, "registered", 0);
            const std::string t = jget<bool>(data_, "too_few", false)
                                      ? "Votre clan n'a que " + std::to_string(n) + " inscrit(s) : il en faut au moins 14 pour former les sept groupes."
                                      : std::string("Les sept groupes se forment au début du raid, un par branche du titan.");
            const auto ls = wrap(ui, t, FontFace::Body, 14, w, 4);
            drawLines(ui, ls, {x, y}, 14, ui.s(20), kDim);
            return;
        }
        int shown = 0;
        for (const auto& g : groups) {
            if (y + rowH > r.bottom() - ui.s(26)) break;
            const bool out = jget<bool>(g, "out", false), waiting = jget<bool>(g, "waiting", false);
            const std::string head = "Groupe " + std::to_string(jget<int>(g, "group", 1)) + " · " + jstr(g, "branch_name");
            const int pl = jget<int>(g, "placed", 0);
            const Json inst = instanceOf(jstr(g, "instance_id"));
            std::string sub;
            if (out) sub = "éliminé : ses joueurs regardent";
            else {
                sub = std::to_string(pl) + " / 8 placés · " + std::to_string(jget<int>(inst, "players", 0)) + " en jeu";
                if (!jstr(inst, "zone_name").empty()) sub += " · zone : " + jstr(inst, "zone_name");
                if (waiting) sub += " · en attente";
            }
            ui.r().fillRoundedRect({x - ui.s(4), y, w + ui.s(8), rowH - ui.s(6)}, ui.s(8), Color{30, 26, 58, 255});
            ui.r().fillRoundedRect({x - ui.s(4), y, ui.s(5), rowH - ui.s(6)}, ui.s(2), out ? kDim : pl >= 8 || pl <= 2 ? kOrange : kGreen);
            fit(ui, head, {x + ui.s(8), y + ui.s(13)}, 14, w - ui.s(12), out ? kDim : kText, FontFace::Bold);
            fit(ui, sub, {x + ui.s(8), y + ui.s(30)}, 12, w - ui.s(12), out ? Color{255, 140, 140, 255} : kDim);
            y += rowH;
            ++shown;
        }
        const int rest = static_cast<int>(groups.size()) - shown;
        if (rest > 0) fit(ui, "+ " + std::to_string(rest) + (rest > 1 ? " autres groupes" : " autre groupe"), {x, r.bottom() - ui.s(16)}, 12, w, kDim);
    }

    /// §41: the groups still in play (a member moves to one of them), in their order.
    std::vector<Json> liveGroups() const {
        std::vector<Json> out;
        for (const auto& g : data_.value("groups", Json::array()))
            if (!jget<bool>(g, "out", false) && !jstr(g, "instance_id").empty()) out.push_back(g);
        return out;
    }

    /// The members (a scrolling list), then the tools of the raid leader for the one selected.
    void drawMembers(const Rect& r) {
        UIContext& ui = g_.ui;
        Renderer& rr = ui.r();
        const bool manage = jget<bool>(data_, "can_manage", false);
        const std::vector<Json> live = liveGroups();  // §41: the groups in play (one per branch)
        const int n = static_cast<int>(live.size());
        const int perRow = 4;
        const float bh = ui.s(44), bgap = ui.s(8);
        const int rows = (n + perRow - 1) / perRow;
        // The tools below first (their height decides the list's room).
        const float toolsH = manage ? ui.s(34) + static_cast<float>(rows + 1) * (bh + bgap) + ui.s(30) : ui.s(70);
        const Rect list{r.x, r.y, r.w, std::max(ui.s(120), r.h - toolsH - ui.s(12))};
        float y = section(ui, list, "JOUEURS INSCRITS", kGold);
        const Json members = data_.value("members", Json::array());
        const Rect view{list.x + ui.s(10), y, list.w - ui.s(20), list.bottom() - y - ui.s(10)};
        const float pitch = ui.s(46);
        ui.beginScroll("members", view, static_cast<float>(members.size()) * pitch);
        const float off = ui.scrollOffset();
        for (size_t k = 0; k < members.size(); ++k) {
            const Json& mb = members[k];
            const Rect row{view.x, view.y - off + static_cast<float>(k) * pitch, view.w - ui.s(10), pitch - ui.s(6)};
            const std::string name = "m" + std::to_string(k);
            bool hovered = false;
            if (ui.hotspot(name, row, &hovered)) selected_ = jstr(mb, "id");
            const bool focus = ui.focused(name);
            if (focus && !ui.usingPointer()) selected_ = jstr(mb, "id");
            if (row.bottom() < view.y || row.y > view.bottom()) continue;
            const bool sel = selected_ == jstr(mb, "id");
            rr.fillRoundedRect(row, ui.s(8), sel ? Color{58, 40, 96, 255} : hovered || focus ? Color{40, 32, 74, 255} : Color{28, 24, 54, 255});
            if (sel || focus) rr.drawRoundedRect(row, ui.s(8), kGold.withAlphaF(sel ? 0.9f : 0.6f), ui.s(focus ? 2.0f : 1.0f));
            const std::string part = jstr(mb, "branch");
            const bool playing = jget<bool>(mb, "playing", false);
            const int rank = jget<int>(mb, "waiting_rank", 0);  // §41: beyond the 56 in play
            const std::string where = rank > 0       ? "liste d'attente · rang " + std::to_string(rank)
                                      : part.empty() ? "pas encore placé"
                                                     : branchName(part) + " · " + jstr(mb, "group") + (jstr(mb, "zone_name").empty() ? "" : " · " + jstr(mb, "zone_name"));
            const std::string state = playing ? "en jeu" : "hors combat";
            const float sw = std::min(textW(ui, state, 12, FontFace::Bold), row.w * 0.25f);
            fit(ui, state, {row.right() - ui.s(12), row.center().y}, 12, sw, playing ? kGreen : kDim, FontFace::Bold, Align::Right);
            const float nw = (row.w - sw - ui.s(36)) * 0.42f;
            const float used = fit(ui, jstr(mb, "username"), {row.x + ui.s(12), row.center().y}, 15, nw, jget<bool>(mb, "me", false) ? kGold : kText,
                                   FontFace::Bold, Align::Left, true)
                                   .x;
            fit(ui, where, {row.x + ui.s(24) + used, row.center().y}, 13, row.w - sw - ui.s(36) - used - ui.s(12), part.empty() ? kDim : kPeach);
        }
        ui.endScroll();

        // The tools: move the member selected to another group, even out the seven groups (raid leader, chief, officers).
        const Rect tools{r.x, list.bottom() + ui.s(12), r.w, r.bottom() - list.bottom() - ui.s(12)};
        const Json sel = member(selected_);
        const float x = tools.x, w = tools.w;
        float ty = tools.y;
        if (!manage) {
            const auto ls = wrap(ui, status_.empty() ? "Seuls le chef, les officiers et le chef de raid du clan déplacent les joueurs d'un groupe à l'autre ; chaque groupe joue sa branche."
                                                      : status_,
                                 FontFace::Body, 14, w, 3);
            drawLines(ui, ls, {x, ty}, 14, ui.s(20), statusError_ ? ui.theme().danger : kDim);
            return;
        }
        const std::string who = sel.is_object() ? jstr(sel, "username") : "";
        fit(ui, who.empty() ? "Choisissez un joueur dans la liste." : "Déplacer " + who + " vers le groupe de :", {x, ty + ui.s(12)}, 15, w, kText, FontFace::Bold,
            Align::Left, false);
        ty += ui.s(34);
        const float cellW = (w - static_cast<float>(perRow - 1) * bgap) / static_cast<float>(perRow);
        // §41: every group keeps 2 to 8 players: a move that would break it is off (the server checks it again).
        const std::string from = sel.is_object() ? jstr(sel, "instance_id") : "";
        int fromSize = 0;
        for (const Json& g : live)
            if (jstr(g, "instance_id") == from) fromSize = jget<int>(g, "placed", 0);
        UIContext::ButtonStyle bs;
        bs.enabled = sel.is_object() && !busy_ && jget<int>(sel, "waiting_rank", 0) == 0 && !from.empty();
        for (int i = 0; i < n; ++i) {
            const Json& g = live[static_cast<size_t>(i)];
            const Rect cell{x + static_cast<float>(i % perRow) * (cellW + bgap), ty + static_cast<float>(i / perRow) * (bh + bgap), cellW, bh};
            UIContext::ButtonStyle s = bs;
            s.selected = !from.empty() && jstr(g, "instance_id") == from;
            if (!s.selected) s.enabled = s.enabled && jget<int>(g, "placed", 0) < raids::kGroupSize && fromSize > raids::kMinGroup;
            if (ui.button("to" + std::to_string(i), cell, jstr(g, "branch_name"), s) && !s.selected) move(jstr(sel, "id"), jstr(g, "instance_id"));
        }
        ty += static_cast<float>(rows) * (bh + bgap);
        // §41: even out the seven groups (every one at once).
        const std::string label = "Équilibrer les sept groupes";
        const float rbw = std::min(textW(ui, label, 15, FontFace::Bold) + ui.s(40), w * 0.6f);
        UIContext::ButtonStyle rs;
        rs.primary = true;
        rs.enabled = n > 0 && !busy_;
        if (ui.button("rebalance", {x, ty, rbw, bh}, label, rs)) rebalance();
        const std::string note = status_.empty() ? "Les joueurs en combat gardent leur partie : leur nouvelle place compte à leur prochaine entrée." : status_;
        const auto ls = wrap(ui, note, FontFace::Body, 13, w - rbw - ui.s(16), 2);
        drawLines(ui, ls, {x + rbw + ui.s(16), ty + (bh - static_cast<float>(ls.size()) * ui.s(18)) * 0.5f}, 13, ui.s(18), statusError_ ? ui.theme().danger : kDim);
    }

    void move(const std::string& player, const std::string& instance) {
        busy_ = true;
        status_ = "Déplacement…";
        statusError_ = false;
        g_.online.send("POST", "/api/v1/raids/events/" + eventId_ + "/placements", Json{{"player_id", player}, {"instance_id", instance}},
                       OnlineService::guard(life_, [this](const ApiResult& r) {
                           busy_ = false;
                           statusError_ = !r.ok;
                           status_ = r.ok ? "Joueur placé." : r.message;
                           load();
                       }));
    }

    void rebalance() {
        busy_ = true;
        status_ = "Équilibrage…";
        statusError_ = false;
        g_.online.send("POST", "/api/v1/raids/events/" + eventId_ + "/placements/rebalance", Json::object(),
                       OnlineService::guard(life_, [this](const ApiResult& r) {
                           busy_ = false;
                           statusError_ = !r.ok;
                           const int moved = r.ok ? jget<int>(r.data, "moved", 0) : 0;
                           status_ = !r.ok ? r.message : moved == 0 ? "Les groupes sont déjà équilibrés." : std::to_string(moved) + (moved > 1 ? " joueurs déplacés." : " joueur déplacé.");
                           load();
                       }));
    }

    GameServices& g_;
    std::string eventId_;
    MenuBackground bg_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    Json data_;
    bool loaded_ = false, busy_ = false, statusError_ = false;
    int gen_ = 0;
    float timer_ = 0.0f;
    std::string error_, status_, selected_;
};

}  // namespace

std::unique_ptr<Scene> makeRaidRoster(GameServices& g, const std::string& eventId) { return std::make_unique<RaidRosterScene>(g, eventId); }

}  // namespace cosmic
