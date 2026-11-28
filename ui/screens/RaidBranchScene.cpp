// 2.65: the map of a branch of the raid (docs/RAIDS.md, maquette « Branche »): 2.68, any of the
// seven branches of Asterion (« BRAS DROIT » first), its 8 sub-zones linked as on the titan's body
// (two paths, the secret zone, the lock of several objectives, the shortcut, the zone of a pair of
// objectives, the passages that another branch opens, the sealed lair of the gigaboss; 2.68,
// phase 3: the hand of the BRAS DROIT is the arena of its gigaboss Pyroclast).
// Everything shown comes from the server (GET /raids/events/{id}/branches/{part}): the zones
// controlled, the objectives done and by which group, the passages open, the links of the titan
// received and given, the groups and where they are. At the right, the zone selected: its
// objectives, what is inside, the links, the groups present and « Rejoindre » (§41, titan 16: only on
// my group's branch — group k plays branch k, never another, and nobody helps another group), and, my
// group waiting (its Gigaboss fallen), « Remplacer le groupe éliminé » on a zone of my clan to retake.
//
// Keyboard, gamepad and mouse: the zones are focusable (arrows / stick, Entrée / A to select), the
// buttons too; Échap / B goes back. Every text is measured into its room (raidui::fit, wrap).

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidSheets.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

using namespace raidui;

const Color kViolet{201, 168, 255, 255};
const Color kLock{106, 90, 106, 255};
const Color kLink{255, 138, 184, 255};  // 2.68: a passage another branch of the titan opens

struct NodeLook {
    Color fill, edge, tag;
    bool dashed = false;
};

/// A dashed rectangle (locked zones, the secret one): short segments along its sides.
void dashedRect(Renderer& r, const Rect& b, Color c, float t, float dash) {
    auto line = [&](Vec2 a, Vec2 z) {
        const Vec2 d = z - a;
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len <= 0.0f) return;
        const Vec2 u{d.x / len, d.y / len};
        for (float s = 0.0f; s < len; s += dash * 2.0f) {
            const float e = std::min(len, s + dash);
            r.drawLine({a.x + u.x * s, a.y + u.y * s}, {a.x + u.x * e, a.y + u.y * e}, c, t);
        }
    };
    line({b.x, b.y}, {b.right(), b.y});
    line({b.right(), b.y}, {b.right(), b.bottom()});
    line({b.right(), b.bottom()}, {b.x, b.bottom()});
    line({b.x, b.bottom()}, {b.x, b.y});
}

class RaidBranchScene : public Scene {
public:
    RaidBranchScene(GameServices& g, std::string eventId, std::string part) : g_(g), eventId_(std::move(eventId)), part_(std::move(part)) {}

    void onEnter() override {
        bg_.init(g_);
        g_.ui.setFocus("branch/zone" + std::to_string(selected_));
        load();
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
        g_.online.get("/api/v1/raids/events/" + eventId_ + "/branches/" + part_, OnlineService::guard(life_, [this, gen](const ApiResult& r) {
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

    const Json& zoneJson(int id) const {
        static const Json none;
        const Json& zones = data_.contains("zones") ? data_["zones"] : none;
        if (zones.is_array())
            for (const auto& z : zones)
                if (jget<int>(z, "id", 0) == id) return z;
        return none;
    }

    std::vector<Json> groupsIn(int zone) const {
        // 2.78: each player changes zone alone: a group is in every zone where one of its players is (`zones`,
        // a server of before: its `zone`).
        std::vector<Json> out;
        for (const auto& i : data_.value("instances", Json::array())) {
            if (jget<int>(i, "players", 0) <= 0) continue;
            bool here = jget<int>(i, "zone", 0) == zone;
            if (const Json zs = i.value("zones", Json()); zs.is_array() && !zs.empty()) {
                here = false;
                for (const Json& z : zs) here = here || (z.is_number_integer() && z.get<int>() == zone);
            }
            if (here) out.push_back(i);
        }
        return out;
    }

    NodeLook look(const Json& z) const {
        const int id = jget<int>(z, "id", 0);
        if (jget<bool>(z, "boss", false)) return {{90, 18, 14, 245}, kGold, kGold, false};
        if (jget<bool>(z, "controlled", false)) return {{24, 58, 42, 245}, kGreen, kGreen, false};
        if (!jget<bool>(z, "reachable", false)) return {{30, 24, 32, 245}, kLock, Color{184, 168, 184, 255}, true};
        if (jget<bool>(z, "secret", false)) return {{36, 24, 58, 245}, kViolet, Color{216, 192, 255, 255}, true};
        if (!groupsIn(id).empty()) return {{74, 26, 18, 245}, kOrange, Color{255, 176, 138, 255}, false};
        return {{42, 26, 36, 245}, Color{184, 138, 122, 255}, kPeach, false};
    }

    std::string tagOf(const Json& z) const {
        const int id = jget<int>(z, "id", 0);
        const int groups = static_cast<int>(groupsIn(id).size());
        const int required = jget<int>(z, "groups_required", 1);
        if (jget<bool>(z, "boss", false)) {
            // 2.68 (phase 3): the gigaboss's arena: fallen once its objective is accepted; 2.69: sealed only
            // while its gigaboss is not fought (the lair's zone has none in the game's catalog).
            const raid::Branch* local = raid::Branch::builtin(part_);
            if (!(local && local->zoneRules(id) && local->zoneRules(id)->gigaboss.present)) return "scellée · prochaine version";
            if (jget<bool>(z, "controlled", false)) return jstr(data_.value("branch", Json::object()), "gigaboss") + " vaincu";
            if (!jget<bool>(z, "reachable", false)) return "gigaboss · verrouillée";
            return groups > 0 ? "gigaboss · " + std::to_string(groups) + (groups > 1 ? " groupes au combat" : " groupe au combat") : "gigaboss · à affronter";
        }
        if (jget<bool>(z, "controlled", false)) return "contrôlée";
        if (!jget<bool>(z, "reachable", false)) {
            const Json gens = data_.value("generators", Json::object());
            for (const auto& e : data_.value("edges", Json::array()))
                if ((jget<int>(e, "a", 0) == id || jget<int>(e, "b", 0) == id) && jstr(e, "lock") == "generators")
                    return "verrouillé (" + std::to_string(jget<int>(gens, "done", 0)) + " / " + std::to_string(jget<int>(gens, "needed", 2)) + ")";
            return "verrouillé";
        }
        if (jget<bool>(z, "secret", false)) {
            for (const auto& o : z.value("objectives", Json::array()))
                if (jget<bool>(o, "done", false)) return "relique emportée par " + jstr(o, "by");
            return groups > 0 ? "découverte · " + std::to_string(groups) + (groups > 1 ? " groupes" : " groupe") : "zone secrète";
        }
        std::string t = groups > 0 ? "combat · " + std::to_string(groups) + (groups > 1 ? " groupes" : " groupe") : "à prendre";
        if (required > 1) t += " · " + std::to_string(required) + " groupes requis";
        return t;
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.6f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{20, 8, 12, 150});
        const bool top = g_.app.topScene() == this;
        ui.inputEnabled = top;
        ui.pushId("branch");
        bool back = false;
        const float W = ui.screenW(), H = ui.screenH(), m = ui.s(26);
        const float asideW = std::min(ui.s(420), W * 0.3f);
        const Rect aside{W - m - asideW, ui.s(20), asideW, H - ui.s(40)};
        const Rect left{m, ui.s(20), aside.x - ui.s(16) - m, H - ui.s(40)};
        // The header: « Retour », the branch, the gigaboss, the count at the right (measured first).
        const float bw = textW(ui, "‹ Retour", 16) + ui.s(36);
        if (ui.button("back", {left.x, left.y, bw, ui.s(44)}, "‹ Retour")) back = true;
        float hx = left.x + bw + ui.s(14);
        if (!loaded_) {
            fit(ui, error_.empty() ? "Chargement de la branche…" : error_, {hx, left.y + ui.s(22)}, 17, left.right() - hx, error_.empty() ? kDim : ui.theme().danger);
        } else {
            int controlled = 0, groups = 0;
            for (const auto& z : data_.value("zones", Json::array())) controlled += jget<bool>(z, "controlled", false) ? 1 : 0;
            for (const auto& i : data_.value("instances", Json::array())) groups += jget<int>(i, "players", 0) > 0 ? 1 : 0;
            const std::string count = std::to_string(controlled) + " zones sur " + std::to_string(data_.value("zones", Json::array()).size()) + " contrôlées · " +
                                      std::to_string(groups) + (groups > 1 ? " groupes en jeu" : " groupe en jeu");
            const float cw = std::min(textW(ui, count, 14), (left.right() - hx) * 0.4f);
            fit(ui, count, {left.right(), left.y + ui.s(22)}, 14, cw, Color{200, 176, 168, 255}, FontFace::Body, Align::Right);
            const Json branch = data_.value("branch", Json::object());
            const float tw = fit(ui, jstr(branch, "name"), {hx, left.y + ui.s(22)}, 28, (left.right() - cw - ui.s(16) - hx) * 0.5f, kText, FontFace::Title).x;
            const float gx = hx + tw + ui.s(14);
            // 2.68 (phase 3): Pyroclast is fought; 2.69 (phase 4B): Voidworm, Graviton, Titanus too (a lair
            // whose zone has its gigaboss in the game's catalog); the others later.
            const raid::Branch* local = raid::Branch::builtin(part_);
            const bool fought = local && local->zoneRules(8) && local->zoneRules(8)->gigaboss.present;
            fit(ui, "Gigaboss : " + jstr(branch, "gigaboss") +
                        (!fought ? " · prochaine version" : zoneJson(8).value("controlled", false) ? " · vaincu" : part_ == "bras_droit" ? " · dans la Main" : " · dans son antre"),
                {gx, left.y + ui.s(22)}, 15, left.right() - cw - ui.s(16) - gx, kPeach);
            drawMap({left.x, left.y + ui.s(58), left.w, left.h - ui.s(58) - ui.s(34)});
            drawLegend({left.x, left.bottom() - ui.s(24), left.w, ui.s(24)});
            drawAside(aside);
        }
        ui.popId();
        if (!back && ui.backPressed()) back = true;
        ui.end();
        if (back) g_.app.popScene();
    }

    void drawMap(const Rect& panel) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        r.fillRoundedRect(panel, ui.s(18), Color{26, 12, 18, 235});
        r.drawRoundedRect(panel, ui.s(18), Color{90, 42, 34, 255}, ui.s(1));
        // The maquette's coordinates (nodes 170 × 92 around x 110..1000, y 160..620): scaled to fit.
        const float k = std::min(panel.w / 1130.0f, panel.h / 760.0f);
        const Vec2 o{panel.x + (panel.w - 1130.0f * k) * 0.5f + 10.0f * k, panel.y + (panel.h - 760.0f * k) * 0.5f - 60.0f * k};
        auto pos = [&](const Json& z) { return Vec2{o.x + jget<float>(z, "x", 0.0f) * k, o.y + jget<float>(z, "y", 0.0f) * k}; };
        // The passages first (under the nodes).
        for (const auto& e : data_.value("edges", Json::array())) {
            const Json& a = zoneJson(jget<int>(e, "a", 0));
            const Json& b = zoneJson(jget<int>(e, "b", 0));
            if (!a.is_object() || !b.is_object()) continue;
            const bool open = jget<bool>(e, "open", false);
            const std::string lock = jstr(e, "lock");
            Color c = open ? Color{124, 240, 168, 170} : kLock;
            if (jget<bool>(e, "secret", false)) c = kViolet;
            else if (lock == "shortcut") c = kSky;
            else if (lock.rfind("link:", 0) == 0) c = open ? kLink : kLink.withAlpha(150);
            else if (open && (!groupsIn(jget<int>(a, "id", 0)).empty() || !groupsIn(jget<int>(b, "id", 0)).empty())) c = kOrange;
            const Vec2 pa = pos(a), pb = pos(b);
            if (open) {
                r.drawLine(pa, pb, c, ui.s(4));
            } else {
                const Vec2 d = pb - pa;
                const float len = std::sqrt(d.x * d.x + d.y * d.y);
                for (float s = 0.0f; s < len; s += ui.s(16)) {
                    const float e2 = std::min(len, s + ui.s(8));
                    r.drawLine({pa.x + d.x * s / len, pa.y + d.y * s / len}, {pa.x + d.x * e2 / len, pa.y + d.y * e2 / len}, c, ui.s(4));
                }
            }
        }
        // The zones: focusable, their name and number, subtitle, state.
        const float nw = 170.0f * k, nh = 92.0f * k;
        for (const auto& z : data_.value("zones", Json::array())) {
            const int id = jget<int>(z, "id", 0);
            const Vec2 c = pos(z);
            const Rect box{c.x - nw * 0.5f, c.y - nh * 0.5f, nw, nh};
            const NodeLook l = look(z);
            bool hovered = false;
            if (ui.hotspot("zone" + std::to_string(id), box, &hovered)) selected_ = id;
            if (ui.focused("zone" + std::to_string(id))) selected_ = id;
            r.fillRoundedRect(box.expanded(ui.s(4)), ui.s(18), l.edge.withAlpha(selected_ == id ? 90 : 40));
            r.fillRoundedRect(box, ui.s(14), l.fill);
            if (l.dashed) dashedRect(r, box, l.edge, ui.s(2.5f), ui.s(7));
            else r.drawRoundedRect(box, ui.s(14), l.edge, ui.s(selected_ == id || hovered ? 3.5f : 2.5f));
            const float pad = ui.s(9), iw = box.w - pad * 2;
            const float size = std::max(10.0f, 15.0f * std::min(1.0f, k / ui.s(1.0f)));
            const float nwid = fit(ui, std::to_string(id), {box.right() - pad, box.y + pad + ui.s(8)}, size * 0.8f, iw * 0.2f, kGold, FontFace::Bold, Align::Right).x;
            fit(ui, jstr(z, "name"), {box.x + pad, box.y + pad + ui.s(8)}, size, iw - nwid - ui.s(6), kText, FontFace::Bold);
            fit(ui, jstr(z, "sub"), {box.x + pad, box.y + pad + ui.s(8) + nh * 0.3f}, size * 0.8f, iw, Color{240, 216, 208, 255});
            fit(ui, tagOf(z), {box.x + pad, box.y + pad + ui.s(8) + nh * 0.58f}, size * 0.8f, iw, l.tag, FontFace::Bold);
            // The groups there: chips above the zone, side by side, « +n » when the room ends.
            const auto groups = groupsIn(id);
            float gx = box.x;
            for (size_t i = 0; i < groups.size(); ++i) {
                const Json& gi = groups[i];
                const std::string t = jstr(gi, "clan_tag") + "·" + std::to_string(jget<int>(gi, "group", 1));
                const bool mine = jget<bool>(gi, "mine", false);
                const float wpill = pill(ui, gx, box.y - ui.s(14), t, mine ? kGold : kSky, kInk, box.right() - ui.s(30), 12);
                if (wpill == 0.0f) {
                    pill(ui, gx, box.y - ui.s(14), "+" + std::to_string(groups.size() - i), kSky, kInk, box.right() + ui.s(40), 12);
                    break;
                }
                gx += wpill + ui.s(4);
            }
        }
    }

    void drawLegend(const Rect& r) {
        UIContext& ui = g_.ui;
        const std::vector<std::pair<std::string, Color>> items = {{"— chemin ouvert", Color{216, 200, 192, 255}},
                                                                  {"— combat en cours", kOrange},
                                                                  {"- - - verrouillé", Color{154, 138, 138, 255}},
                                                                  {"■ zone secrète (pointillés)", kViolet},
                                                                  {"◀▶ raccourci déblocable", kSky},
                                                                  {"◊ ouvert par une autre branche", kLink},
                                                                  {"◆ objectif facultatif", kGold}};
        // One line: the size that fits them all, never below 75 %.
        float total = 0.0f;
        for (const auto& [t, c] : items) total += textW(ui, t, 13) + ui.s(16);
        const float size = total > r.w ? std::max(10.0f, 13.0f * r.w / total) : 13.0f;
        float x = r.x;
        for (const auto& [t, c] : items) {
            const float w = textW(ui, t, size);
            if (x + w > r.right()) break;
            fit(ui, t, {x, r.center().y}, size, w + ui.s(2), c);
            x += w + ui.s(16) * size / 13.0f;
        }
    }

    void drawAside(const Rect& r) {
        UIContext& ui = g_.ui;
        Renderer& rr = ui.r();
        rr.fillRoundedRect(r, ui.s(16), Color{22, 14, 24, 245});
        rr.drawRoundedRect(r, ui.s(16), Color{90, 42, 34, 255}, ui.s(1));
        const Json& z = zoneJson(selected_);
        if (!z.is_object()) return;
        const float pad = ui.s(16), x = r.x + pad, w = r.w - pad * 2;
        // The buttons at the bottom first: what they offer decides the room above.
        const std::string myInstance = jstr(data_.value("my", Json::object()), "instance_id");
        int myFree = 8;
        int myZone = 0;
        const Json my = data_.value("my", Json::object());
        for (const auto& i : data_.value("instances", Json::array()))
            if (jstr(i, "id") == myInstance) {
                myFree = jget<int>(i, "free", 8);
                myZone = jget<int>(i, "zone", 0);
            }
        const bool registered = jget<bool>(my, "registered", false);
        const bool running = jstr(data_.value("event", Json::object()), "state") == "en_cours";
        const float bh = ui.s(50), by = r.bottom() - pad - bh;
        // §41: my group plays one branch: « Rejoindre » only there (the server refuses any other); a spectator watches.
        const std::string groupBranch = jstr(my, "group_branch");
        const bool spectator = my.value("spectator", Json()).is_object();
        const bool mineHere = spectator || groupBranch.empty() || groupBranch == part_;
        UIContext::ButtonStyle js;
        js.primary = true;
        js.enabled = registered && running && !busy_ && mineHere;
        std::string joinLabel = myInstance.empty() ? "Rejoindre cette branche" : "Rejoindre · " + std::to_string(myFree) + (myFree > 1 ? " places" : " place");
        // 2.75: a player whose group was wiped only watches (the server gives a spectator link).
        if (spectator) joinLabel = "Regarder (groupe éliminé)";
        if (mineHere && ui.button("join", {x, by, w, bh}, joinLabel, js)) join("");  // another branch: no button, the note says why
        float bottom = by - ui.s(10);
        // 2.73: the lair: the sheet of its gigaboss (visible whether it was met or not).
        if (jget<bool>(z, "boss", false)) {
            const std::string boss = jstr(data_.value("branch", Json::object()), "gigaboss");
            if (const std::string sheet = gigabossSheet(); !sheet.empty()) {
                const std::string label = "Fiche de " + boss;
                UIContext::ButtonStyle ss;
                ss.small = true;
                ss.fontSize = std::max(ui.fitFont(label, FontFace::Bold, 16, w - ui.s(20), 0.7f), 11.5f / ui.s(1.0f));
                const Rect sr{x, by - ui.s(10) - ui.s(44), w, ui.s(44)};
                if (ui.button("sheet", sr, label, ss)) g_.app.pushScene(makeRaidSheets(g_, sheet));
                ui.newBadge("sheet", sr, "raids.sheets", true, true);  // « NOUVEAU »
                bottom = sr.y - ui.s(10);
            }
        }
        // 2.75, §41: this zone is « à reprendre » (a group of my clan was wiped there) and my group waits (its Gigaboss
        // fell): it replaces that group there, the whole group (a fresh instance of the zone, the branch's state kept).
        for (const auto& o : my.value("retake_offers", Json::array())) {
            if (jget<int>(o, "zone", 0) != selected_) continue;
            UIContext::ButtonStyle rs;
            rs.primary = true;
            rs.enabled = registered && running && !busy_;
            const std::string label = "Remplacer le groupe éliminé ici (tout votre groupe y va)";
            rs.fontSize = std::max(ui.fitFont(label, FontFace::Bold, 16, w - ui.s(20), 0.7f), 11.5f / ui.s(1.0f));
            const Rect rr{x, bottom - ui.s(44), w, ui.s(44)};
            if (ui.button("retake", rr, label, rs)) retake(selected_);
            bottom = rr.y - ui.s(10);
            break;
        }
        const std::string elsewhere = jstr(my, "placed_branch");
        std::string note = !running ? "Le raid n'est pas en cours : les combats se jouent pendant l'événement."
                           : !registered ? "Inscrivez-vous au raid (fiche de l'événement) pour combattre."
                           : !myInstance.empty() && myZone > 0 && branchZoneName(myZone).size() ? "Votre groupe est dans : " + branchZoneName(myZone) + "."
                           : !mineHere ? "Votre groupe joue une autre branche : un groupe ne joue que la sienne, et n'en part que pour remplacer un groupe éliminé."
                           : !elsewhere.empty() && elsewhere != part_ ? "Votre groupe joue une autre branche : un groupe ne joue que la sienne."
                                                                       : "Vous jouez dans le groupe de votre clan sur cette branche ; il entre par " + branchZoneName(1) + ".";
        if (!status_.empty()) note = status_;
        const auto nl = wrap(ui, note, FontFace::Body, 13, w, 2, true);
        bottom -= static_cast<float>(nl.size()) * ui.s(18);
        drawLines(ui, nl, {x, bottom}, 13, ui.s(18), statusError_ ? ui.theme().danger : kDim);
        bottom -= ui.s(10);

        float y = r.y + pad;
        fit(ui, "Zone " + std::to_string(selected_) + " sur 8 · sélectionnée", {x, y + ui.s(9)}, 13, w, Color{200, 176, 168, 255});
        y += ui.s(22);
        fit(ui, jstr(z, "name"), {x, y + ui.s(14)}, 24, w, kText, FontFace::Title);
        y += ui.s(32);
        const auto sub = wrap(ui, jstr(z, "sub"), FontFace::Body, 14, w, 2);
        y += drawLines(ui, sub, {x, y}, 14, ui.s(20), kPeach) + ui.s(10);
        // Three sections, each measured, cut at the room left above the buttons.
        auto section = [&](const std::string& title, const std::vector<std::pair<std::string, std::pair<std::string, Color>>>& rows) {
            if (y + ui.s(60) > bottom) return;
            std::vector<std::vector<std::string>> wrapped;
            const float markW = ui.s(46);
            float h = ui.s(12) * 2 + ui.s(22);
            for (const auto& row : rows) {
                wrapped.push_back(wrap(ui, row.first, FontFace::Body, 14, w - ui.s(24) - (row.second.first.empty() ? 0.0f : markW), 3));
                h += static_cast<float>(wrapped.back().size()) * ui.s(20) + ui.s(4);
            }
            h = std::min(h, bottom - y);
            const Rect box{x, y, w, h};
            rr.fillRoundedRect(box, ui.s(12), Color{36, 20, 26, 255});
            fit(ui, title, {x + ui.s(12), y + ui.s(12) + ui.s(9)}, 14, w - ui.s(24), kGold, FontFace::Bold);
            float ry = y + ui.s(12) + ui.s(24);
            for (size_t i = 0; i < rows.size(); ++i) {
                const float rh = static_cast<float>(wrapped[i].size()) * ui.s(20);
                if (ry + rh > box.bottom() - ui.s(6)) break;
                float tx = x + ui.s(12);
                if (!rows[i].second.first.empty()) {
                    fit(ui, rows[i].second.first, {tx, ry + ui.s(10)}, 14, markW - ui.s(6), rows[i].second.second, FontFace::Bold);
                    tx += markW;
                }
                drawLines(ui, wrapped[i], {tx, ry}, 14, ui.s(20), kText);
                ry += rh + ui.s(4);
            }
            y += h + ui.s(10);
        };
        std::vector<std::pair<std::string, std::pair<std::string, Color>>> goals;
        for (const auto& o : z.value("objectives", Json::array())) {
            const bool done = jget<bool>(o, "done", false), opt = jget<bool>(o, "optional", false);
            std::string t = jstr(o, "label");
            if (done && !jstr(o, "by").empty()) t += " — " + jstr(o, "by");
            goals.push_back({t, {done ? "OK" : opt ? "◆" : "→", done ? kGreen : opt ? kViolet : kGold}});
        }
        if (jget<bool>(z, "boss", false)) {
            const raid::Branch* local = raid::Branch::builtin(part_);
            if (const raid::ZoneRules* lair = local ? local->zoneRules(8) : nullptr; lair && lair->gigaboss.present)  // 2.68, 2.69: its arena
                goals.push_back({lair->gigaboss.summary, {"◆", kGold}});
            else
                goals.push_back({"Le gigaboss " + jstr(data_.value("branch", Json::object()), "gigaboss") + " attend derrière sa porte scellée : prochaine version.", {"■", kGold}});
        }
        if (goals.empty()) goals.push_back({"Aucun objectif dans cette zone.", {"·", kDim}});
        section("Objectifs", goals);
        std::vector<std::pair<std::string, std::pair<std::string, Color>>> inside;
        if (!jstr(z, "inside").empty()) inside.push_back({jstr(z, "inside"), {"", kText}});
        if (!jstr(z, "event").empty()) inside.push_back({"Événement : " + jstr(z, "event"), {"", kText}});
        if (const float minutes = jget<float>(z, "minutes", 0.0f); minutes > 0.0f)  // 2.68: the generator's estimate
            inside.push_back({"Durée estimée à 8 joueurs : ≈ " + std::to_string(std::max(1L, std::lround(minutes))) + " min", {"", kDim}});
        if (!inside.empty()) section("Dans la zone", inside);
        // 2.68: the links of the titan: received here (another branch acts on this zone), given
        // by this zone's objectives (they act on another branch).
        std::vector<std::pair<std::string, std::pair<std::string, Color>>> links;
        for (const auto& e : z.value("effects", Json::array()))
            links.push_back({jstr(e, "label"), {jget<bool>(e, "on", false) ? "OK" : "◊", jget<bool>(e, "on", false) ? kGreen : kLink}});
        for (const auto& l : data_.value("effects_out", Json::array())) {
            bool fromHere = false;
            for (const auto& src : l.value("sources", Json::array()))
                for (const auto& o : z.value("objectives", Json::array()))
                    fromHere = fromHere || (jstr(src, "branch") == part_ && jstr(src, "objective") == jstr(o, "id"));
            if (!fromHere) continue;
            const Json pr = l.value("progress", Json::object());
            const bool on = jget<bool>(l, "on", false);
            links.push_back({jstr(l, "label"),
                             {on ? "OK" : std::to_string(jget<int>(pr, "done", 0)) + " / " + std::to_string(jget<int>(pr, "needed", 1)), on ? kGreen : kGold}});
        }
        if (!links.empty()) section("Liens avec le reste du titan", links);
        std::vector<std::pair<std::string, std::pair<std::string, Color>>> present;
        for (const auto& gi : groupsIn(selected_))
            present.push_back({"[" + jstr(gi, "clan_tag") + "]·" + std::to_string(jget<int>(gi, "group", 1)) + " · " + std::to_string(jget<int>(gi, "players", 0)) + " / 8" +
                                   (jget<bool>(gi, "mine", false) ? " · votre clan" : ""),
                               {"", kText}});
        if (present.empty()) present.push_back({"Aucun groupe ici pour l'instant.", {"", kDim}});
        // 2.78: the players of my clan in this zone (each one changes zone alone; the server names them).
        std::string names;
        for (const auto& n : z.value("players_here", Json::array()))
            if (n.is_string()) names += (names.empty() ? "" : ", ") + n.get<std::string>();
        if (!names.empty()) present.push_back({i18n::keep(i18n::tr("Ici, de votre clan") + (i18n::language() == Language::English ? ": " : " : ") + names), {"", kSky}});
        const int required = jget<int>(z, "groups_required", 1);
        section(required > 1 ? "Groupes présents · " + std::to_string(required) + " groupes requis" : "Groupes présents", present);
    }

    std::string branchZoneName(int id) const { return jstr(zoneJson(id), "name"); }

    /// 2.73: the sheet of this branch's gigaboss (game/raid/RaidSheets), "" when there is none.
    std::string gigabossSheet() {
        if (!sheetRead_) {
            sheetRead_ = true;
            const raid::RaidSheets sheets = raid::loadRaidSheets(g_.content, g_.achievementCatalog, g_.cosmetics);
            if (const raid::GigabossSheet* s = sheets.ofPart(part_)) sheet_ = s->id;
        }
        return sheet_;
    }

    void join(const std::string& instanceId) {
        busy_ = true;
        status_ = "Connexion à l'instance…";
        statusError_ = false;
        Json body = Json::object();
        if (!instanceId.empty()) body["instance_id"] = instanceId;
        g_.online.send("POST", "/api/v1/raids/events/" + eventId_ + "/branches/" + part_ + "/join", body, OnlineService::guard(life_, [this](const ApiResult& r) {
                           busy_ = false;
                           if (!r.ok) {
                               status_ = r.message;
                               statusError_ = true;
                               return;
                           }
                           RaidEntry e;
                           e.eventId = eventId_;
                           e.branch = part_;
                           readRaidEntry(e, r.data);  // 2.75: its start zone, a spectator
                           status_.clear();
                           g_.app.switchScene(makeRaidGame(g_, e), 0.35f);
                       }));
    }

    /// 2.75, §41: my group waits (its Gigaboss fell): it replaces a wiped group of my clan in a zone of this branch (a
    /// fresh instance of that zone, the branch's state kept; the whole group goes), POST /raids/instances/{id}/retake
    /// from my group's instance (in its own branch).
    void retake(int zone) {
        const std::string mine = jstr(data_.value("my", Json::object()), "group_instance_id");
        if (mine.empty() || busy_) return;
        busy_ = true;
        status_ = "Remplacement du groupe éliminé…";
        statusError_ = false;
        g_.online.send("POST", "/api/v1/raids/instances/" + mine + "/retake", Json{{"zone", zone}, {"branch", part_}}, OnlineService::guard(life_, [this](const ApiResult& r) {
                           busy_ = false;
                           if (!r.ok) {
                               status_ = r.message;
                               statusError_ = true;
                               return;
                           }
                           RaidEntry e;
                           e.eventId = eventId_;
                           e.branch = part_;
                           readRaidEntry(e, r.data);
                           status_.clear();
                           g_.app.switchScene(makeRaidGame(g_, e), 0.35f);
                       }));
    }

    GameServices& g_;
    std::string eventId_, part_;
    MenuBackground bg_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    Json data_;
    bool loaded_ = false, busy_ = false, statusError_ = false;
    int gen_ = 0;
    int selected_ = 1;
    float timer_ = 0.0f;
    std::string error_, status_;
    bool sheetRead_ = false;
    std::string sheet_;  // 2.73: its gigaboss's sheet
};

}  // namespace

std::unique_ptr<Scene> makeRaidBranch(GameServices& g, const std::string& eventId, const std::string& part) {
    return std::make_unique<RaidBranchScene>(g, eventId, part);
}

}  // namespace cosmic
