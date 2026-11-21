#include "ui/widgets/CosmicFrame.h"
#include "ui/screens/AdminGifts.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <random>

#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/online/OnlineSession.h"
#include "game/online/Raids.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/GiftArt.h"
#include "ui/screens/GiftsUi.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

namespace {

const Color kGold{255, 205, 110, 255};
const Color kMuted{140, 152, 186, 255};
const Color kCell{18, 26, 60, 255};
const Color kLine{44, 58, 104, 255};

/// « 20/11 · 21:58 » (local time).
std::string stamp(const std::string& iso) {
    auto t = parseIsoUtc(iso);
    if (!t) return "—";
    std::time_t tt = std::chrono::system_clock::to_time_t(*t);
    std::tm m{};
#ifdef _WIN32
    localtime_s(&m, &tt);
#else
    localtime_r(&tt, &m);
#endif
    char b[40];
    std::snprintf(b, sizeof b, "%02d/%02d · %02d:%02d", m.tm_mday, m.tm_mon + 1, m.tm_hour, m.tm_min);
    return b;
}

/// A column's title in gold; the y under it.
float title(UIContext& ui, const Rect& col, const std::string& t) {
    raidui::fit(ui, t, {col.x, col.y + ui.s(14)}, 18, col.w, kGold, FontFace::Bold);
    return col.y + ui.s(34);
}

/// The check of a chosen cell or player: a gold square with its tick drawn (no glyph needed).
void tick(Renderer& r, const Rect& box, bool on, float s) {
    r.fillRoundedRect(box, box.h * 0.2f, on ? kGold : Color{0, 0, 0, 0});
    r.drawRoundedRect(box, box.h * 0.2f, on ? kGold : kMuted, std::max(1.0f, s * 2.0f));
    if (!on) return;
    const Vec2 a{box.x + box.w * 0.24f, box.y + box.h * 0.54f}, b{box.x + box.w * 0.43f, box.y + box.h * 0.72f}, c{box.x + box.w * 0.78f, box.y + box.h * 0.3f};
    r.drawLine(a, b, Color{42, 26, 0, 255}, std::max(1.5f, box.h * 0.14f));
    r.drawLine(b, c, Color{42, 26, 0, 255}, std::max(1.5f, box.h * 0.14f));
}

std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

}  // namespace

AdminGifts::AdminGifts(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole)
    : g_(g), say_(std::move(say)), lostRole_(std::move(lostRole)), online_(std::make_unique<OnlineUi>(g)) {}

AdminGifts::~AdminGifts() = default;

std::vector<gifts::Reward> AdminGifts::catalogue(GameServices& g, const std::string& kind) {
    std::vector<gifts::Reward> out;
    if (kind == "badge") {
        // The badges Cosmic Online grants itself (the clan raids' achievements): never one the game unlocks alone.
        for (const auto& a : g.achievementCatalog.achievements())
            if (a.condition.type == "raid_reward") out.push_back({"badge", a.id, a.name});
        return out;
    }
    for (const CosmeticItem* it : g.cosmetics.items(kind))
        if (!it->isDefault && it->souvenir.empty() && !it->serverMade()) out.push_back({kind, it->id, it->name});
    return out;
}

void AdminGifts::enter(bool force) {
    if (!loaded_ || force) loadPlayers(false);
    if (!historyLoaded_ || force) loadHistory();
}

std::string AdminGifts::rewardsQuery() const {
    std::string q;
    for (const auto& r : rewards_) q += (q.empty() ? "" : ",") + r.key();
    return q;
}

void AdminGifts::loadPlayers(bool more) {
    static const char* kFilters[] = {"online", "all", "clan"};
    if (filter_ == 3) {  // « Choisis »: the players chosen, read with their tags
        loadChosenOwned();
        return;
    }
    lastQuery_ = query_;
    listRewards_ = rewardsQuery();
    const int offset = more ? static_cast<int>(players_.size()) : 0;
    std::string path = "/api/v1/admin/gifts/players?limit=40&offset=" + std::to_string(offset) + "&filter=" + kFilters[filter_];
    const std::string q = query_.substr(0, 20);
    if (!q.empty()) path += "&q=" + online::encodeQueryValue(q);
    if (!rewards_.empty()) path += "&rewards=" + online::encodeQueryValue(rewardsQuery());
    const int gen = ++gen_;
    busy_ = true;
    g_.online.get(path, OnlineService::guard(life_, [this, gen, more](const ApiResult& r) {
                      if (gen != gen_) return;
                      busy_ = false;
                      if (lostRole_(r)) return;
                      if (!r.ok) {
                          say_(r.message, true);
                          return;
                      }
                      loaded_ = true;
                      const Json& list = jfield(r.data, "players", Json::array());
                      if (!more) players_ = Json::array();
                      if (list.is_array())
                          for (const Json& p : list) {
                              players_.push_back(p);
                              owned_[jstr(p, "id")] = p;
                          }
                      more_ = jget<bool>(r.data, "has_more", false);
                      clan_ = jfield(r.data, "clan", Json());
                      if (r.data.contains("active_players")) activePlayers_ = jget<int>(r.data, "active_players", activePlayers_);
                  }),
                  false);
}

void AdminGifts::loadChosenOwned() {
    std::string ids;
    for (const auto& c : chosen_) ids += (ids.empty() ? "" : ",") + c.id;
    ownedKey_ = rewardsQuery() + "|" + ids;
    if (ids.empty()) return;
    std::string path = "/api/v1/admin/gifts/players?filter=ids&limit=100&ids=" + online::encodeQueryValue(ids);
    if (!rewards_.empty()) path += "&rewards=" + online::encodeQueryValue(rewardsQuery());
    const int gen = ++ownedGen_;
    g_.online.get(path, OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                      if (gen != ownedGen_ || lostRole_(r) || !r.ok) return;
                      const Json& list = jfield(r.data, "players", Json::array());
                      if (list.is_array())
                          for (const Json& p : list) owned_[jstr(p, "id")] = p;
                      if (clan_.is_null()) clan_ = jfield(r.data, "clan", Json());
                  }),
                  false);
}

void AdminGifts::loadActive() {
    g_.online.get("/api/v1/admin/gifts/players?filter=all&limit=1", OnlineService::guard(life_, [this](const ApiResult& r) {
                      if (lostRole_(r) || !r.ok) return;
                      activePlayers_ = jget<int>(r.data, "active_players", activePlayers_);
                  }),
                  false);
}

void AdminGifts::loadHistory() {
    g_.online.get("/api/v1/admin/gifts?limit=30", OnlineService::guard(life_, [this](const ApiResult& r) {
                      if (lostRole_(r) || !r.ok) return;
                      historyLoaded_ = true;
                      history_ = jfield(r.data, "sends", Json::array());
                      historyMore_ = jget<bool>(r.data, "has_more", false);
                  }),
                  false);
}

std::vector<std::string> AdminGifts::ownedOf(const std::string& playerId, bool pending) const {
    std::vector<std::string> out;
    auto it = owned_.find(playerId);
    if (it == owned_.end()) return out;
    const Json& list = jfield(it->second, pending ? "pending" : "owned", Json::array());
    if (!list.is_array()) return out;
    for (const Json& k : list)
        if (k.is_string())
            for (const auto& r : rewards_)
                if (r.key() == k.get<std::string>()) out.push_back(giftart::rewardName(g_, r));
    return out;
}

bool AdminGifts::isChosen(const std::string& id) const {
    return std::any_of(chosen_.begin(), chosen_.end(), [&](const Chosen& c) { return c.id == id; });
}

void AdminGifts::toggle(const Json& p) {
    const std::string id = jstr(p, "id");
    auto it = std::find_if(chosen_.begin(), chosen_.end(), [&](const Chosen& c) { return c.id == id; });
    if (it != chosen_.end()) chosen_.erase(it);
    else chosen_.push_back({id, jstr(p, "username")});
    confirm_ = false;
}

void AdminGifts::send() {
    if (sending_ || rewards_.empty() || (!allPlayers_ && chosen_.empty())) return;
    Json rewards = Json::array(), players = Json::array();
    for (const auto& r : rewards_) rewards.push_back({{"kind", r.kind}, {"id", r.id}});
    for (const auto& c : chosen_) players.push_back(c.id);
    const bool bundle = bundled();
    Json body{{"rewards", rewards}, {"wrap", wrap_}, {"message", gifts::cleanMessage(message_)}, {"bundle", bundle}};
    body["players"] = allPlayers_ ? Json("all") : players;
    // The same request (a send interrupted, its answer lost) keeps its key: the server goes on, never sends it twice.
    const std::string what = body.dump();
    if (requestKey_.empty() || what != requestBody_) {
        static const char* kHex = "0123456789abcdef";
        std::random_device rd;
        requestKey_ = "gift-";
        for (int i = 0; i < 24; ++i) requestKey_ += kHex[rd() % 16];
        requestBody_ = what;
    }
    body["request_key"] = requestKey_;
    sending_ = true;
    post(body, bundle);
}

void AdminGifts::post(const Json& body, bool bundle) {
    g_.online.send("POST", "/api/v1/admin/gifts", body, OnlineService::guard(life_, [this, body, bundle](const ApiResult& r) {
                       if (lostRole_(r)) {
                           sending_ = confirm_ = false;
                           return;
                       }
                       if (r.ok && jstr(r.data, "state") == "writing") {  // a large send: written in steps, it goes on
                           const int done = jget<int>(r.data, "done", -1), total = jget<int>(r.data, "total", -1);
                           say_(done >= 0 && total > 0 ? "Envoi en cours… " + raids::groupDigits(done) + " / " + raids::groupDigits(total) + " joueurs"
                                                       : std::string("Envoi en cours…"),
                                false);
                           post(body, bundle);
                           return;
                       }
                       sending_ = false;
                       confirm_ = false;
                       if (!r.ok) {
                           if (r.status != 0) requestKey_.clear();  // refused: the next send is a new one (a lost connection keeps it)
                           say_(r.message, true);
                           return;
                       }
                       requestKey_.clear();
                       const int n = jget<int>(r.data, "created", 0), parcels = jget<int>(r.data, "parcels", 0);
                       const Json& skipped = jfield(r.data, "skipped", Json::array());
                       const int s = jget<int>(r.data, "skipped_count", skipped.is_array() ? static_cast<int>(skipped.size()) : 0);
                       say_((bundle ? plural(parcels, "colis envoyé", "colis envoyés") : plural(n, "cadeau envoyé", "cadeaux envoyés")) +
                                (s > 0 ? " · " + plural(s, "ignoré", "ignorés") : std::string()) + " · inscrit au Journal",
                            false);
                       g_.uiSound("ui_confirm", 0.6f);
                       chosen_.clear();
                       owned_.clear();
                       allPlayers_ = false;
                       loadPlayers(false);
                       loadHistory();
                   }));
}

void AdminGifts::cancelSend(int64_t id, bool parcels) {
    g_.online.send("POST", "/api/v1/admin/gifts/" + std::to_string(id) + "/cancel", Json::object(), OnlineService::guard(life_, [this, parcels](const ApiResult& r) {
                       cancelAsk_ = 0;
                       if (lostRole_(r)) return;
                       if (!r.ok) {
                           say_(r.message, true);
                           return;
                       }
                       const int n = jget<int>(r.data, "cancelled", 0);
                       if (parcels)
                           say_(n > 0 ? plural(n, "colis retiré", "colis retirés") + " · inscrit au Journal" : std::string("Ces colis étaient déjà ouverts."), false);
                       else
                           say_(n > 0 ? plural(n, "cadeau retiré", "cadeaux retirés") + " · inscrit au Journal" : std::string("Ces cadeaux étaient déjà ouverts."),
                                false);
                       loadHistory();
                       loadPlayers(false);
                   }));
}

// ------------------------------------------------------------------ drawing

void AdminGifts::draw(const Rect& inner) {
    UIContext& ui = g_.ui;
    ui.pushId("gifts");
    L_ = giftsui::adminLayout(ui, inner);
    // The tags of the chosen players follow the rewards and the players chosen (read a moment after a change).
    std::string ids;
    for (const auto& c : chosen_) ids += (ids.empty() ? "" : ",") + c.id;
    if (rewardsQuery() + "|" + ids != ownedKey_) {
        if (!queryDirty_) queryT_ = ui.time();
        queryDirty_ = true;
    }
    if (queryDirty_ && ui.time() - queryT_ > 0.35f) {
        queryDirty_ = false;
        loadChosenOwned();
        // The list shown again only when what it depends on changed (the search, the rewards' tags).
        if (filter_ != 3 && (query_ != lastQuery_ || rewardsQuery() != listRewards_)) loadPlayers(false);
    }
    drawRewards(L_.rewards);
    drawPlayers(L_.players);
    drawSend(L_.send);
    ui.popId();
}

void AdminGifts::drawRewards(const Rect& col) {
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
    const giftsui::AdminLayout& L = L_;
    float y = title(ui, col, "1 · Les récompenses");
    // The categories, each with how many of its items are chosen.
    const auto& kinds = gifts::rewardKinds();
    std::vector<std::string> labels;
    std::vector<bool> on;
    for (size_t i = 0; i < kinds.size(); ++i) {
        const int n = static_cast<int>(std::count_if(rewards_.begin(), rewards_.end(), [&](const gifts::Reward& x) { return x.kind == kinds[i]; }));
        labels.push_back(gifts::kindName(kinds[i]) + (n > 0 ? " (" + std::to_string(n) + ")" : std::string()));
        on.push_back(static_cast<int>(i) == kind_);
    }
    float bottom = y;
    const int hit = raidui::chips(ui, "kinds", col.x, y, col.right(), labels, on, bottom);
    if (hit >= 0) kind_ = hit;
    // The wraps at the bottom, the grid of the category between.
    const int wc = L.wrapColumns;
    const float wgap = ui.s(6), wrapH = L.wrapH;
    const int wrows = (gifts::kWrapCount + wc - 1) / wc;
    const float wrapsH = static_cast<float>(wrows) * wrapH + static_cast<float>(wrows - 1) * wgap;
    const float wrapsY = col.bottom() - wrapsH;
    const float wtY = wrapsY - ui.s(34);
    const Rect grid{col.x, bottom + ui.s(10), col.w, wtY - ui.s(8) - (bottom + ui.s(10))};
    const auto items = catalogue(g_, kinds[static_cast<size_t>(kind_)]);
    const int cols = L.itemColumns;
    const float gap = ui.s(8), cw = (grid.w - ui.s(16) - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols), ch = L.itemH;
    const int rows = (static_cast<int>(items.size()) + cols - 1) / cols;
    ui.beginScroll("items", grid, static_cast<float>(rows) * (ch + gap));
    const float top = grid.y - ui.scrollOffset();
    const float t = ui.time();
    for (size_t i = 0; i < items.size(); ++i) {
        const gifts::Reward& it = items[i];
        const Rect cell{grid.x + static_cast<float>(i % static_cast<size_t>(cols)) * (cw + gap), top + static_cast<float>(i / static_cast<size_t>(cols)) * (ch + gap), cw, ch};
        bool hover = false;
        const bool clicked = ui.hotspot("item/" + it.key(), cell, &hover);
        const bool focused = ui.focused("item/" + it.key());
        auto at = std::find(rewards_.begin(), rewards_.end(), it);
        const bool chosen = at != rewards_.end();
        if (clicked) {
            if (chosen) rewards_.erase(at);
            else rewards_.push_back(it);
            confirm_ = false;
        }
        if (cell.bottom() < grid.y || cell.y > grid.bottom()) continue;
        r.fillRoundedRect(cell, ui.s(10), chosen ? Color{42, 36, 64, 255} : hover || focused ? Color{24, 36, 72, 255} : kCell);
        r.drawRoundedRect(cell, ui.s(10), chosen ? kGold : focused ? ui.theme().accent : kLine, ui.s(chosen || focused ? 2.0f : 1.0f));
        const Rect art{cell.x + ui.s(6), cell.y + ui.s(6), cell.w - ui.s(12), cell.h - ui.s(36)};
        r.pushClip(art);
        const float size = it.kind == "title" ? art.w / 3.4f : std::min(art.h * 1.05f, art.w * 0.8f);
        giftart::drawReward(g_, online_.get(), it, art.center(), size, t, false);
        r.popClip();
        raidui::fit(ui, giftart::rewardName(g_, it), {cell.center().x, cell.bottom() - ui.s(16)}, 13, cell.w - ui.s(12), ui.theme().text, FontFace::Bold,
                    Align::Center);
        if (chosen) tick(r, {cell.right() - ui.s(26), cell.y + ui.s(6), ui.s(20), ui.s(20)}, true, ui.scale());
    }
    ui.endScroll();
    // The eleven wraps.
    raidui::fit(ui, "Emballage · 11 au choix", {col.x, wtY + ui.s(14)}, 16, col.w, kGold, FontFace::Bold);
    const float ww = (col.w - wgap * static_cast<float>(wc - 1)) / static_cast<float>(wc);
    for (int i = 0; i < gifts::kWrapCount; ++i) {
        const std::string& id = gifts::wrapIds()[static_cast<size_t>(i)];
        const Rect cell{col.x + static_cast<float>(i % wc) * (ww + wgap), wrapsY + static_cast<float>(i / wc) * (wrapH + wgap), ww, wrapH};
        bool hover = false;
        if (ui.hotspot("wrap/" + id, cell, &hover)) {
            wrap_ = id;
            confirm_ = false;
        }
        const bool on = wrap_ == id, focused = ui.focused("wrap/" + id);
        r.fillRoundedRect(cell, ui.s(8), hover || focused ? Color{24, 36, 72, 255} : kCell);
        r.drawRoundedRect(cell, ui.s(8), on ? kGold : focused ? ui.theme().accent : kLine, ui.s(on || focused ? 2.0f : 1.0f));
        const float icon = std::min(cell.h - ui.s(26), cell.w - ui.s(8));
        r.pushClip(cell);
        giftart::drawWrapIcon(g_, id, {cell.center().x - icon * 0.5f, cell.y + ui.s(2), icon, icon}, t, static_cast<float>(i));
        r.popClip();
        raidui::fit(ui, gifts::wrapName(id), {cell.center().x, cell.bottom() - ui.s(12)}, 12, cell.w - ui.s(6), on ? kGold : ui.theme().text, FontFace::Bold,
                    Align::Center);
    }
}

void AdminGifts::drawPlayers(const Rect& col) {
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
    const giftsui::AdminLayout& L = L_;
    float y = title(ui, col, "2 · Les joueurs");
    const std::string before = query_;
    ui.textField("search", {col.x, y, col.w, ui.s(46)}, query_, "Rechercher un pseudonyme…", 20);
    if (query_ != before) {
        queryDirty_ = true;
        queryT_ = ui.time();
        if (filter_ == 3) filter_ = 1;
    }
    y += ui.s(56);
    std::vector<std::string> labels = {"En ligne", "Tous"};
    const std::string tag = clan_.is_object() ? jstr(clan_, "tag") : std::string();
    if (!tag.empty()) labels.push_back("Clan [" + tag + "]");
    labels.push_back("Choisis (" + std::to_string(chosen_.size()) + ")");
    std::vector<int> map = {0, 1};
    if (!tag.empty()) map.push_back(2);
    map.push_back(3);
    std::vector<bool> on;
    for (int m : map) on.push_back(m == filter_);
    float bottom = y;
    const int hit = raidui::chips(ui, "filters", col.x, y, col.right(), labels, on, bottom);
    if (hit >= 0 && map[static_cast<size_t>(hit)] != filter_) {
        filter_ = map[static_cast<size_t>(hit)];
        players_ = Json::array();
        loadPlayers(false);
    }
    // The list: those of the filter, or the chosen ones.
    Json shown = Json::array();
    if (filter_ == 3) {
        for (const auto& c : chosen_) {
            auto it = owned_.find(c.id);
            shown.push_back(it != owned_.end() ? it->second : Json{{"id", c.id}, {"username", c.username}});
        }
    } else {
        shown = players_;
    }
    const float moreH = more_ && filter_ != 3 ? ui.s(54) : 0.0f;
    const Rect list{col.x, bottom + ui.s(10), col.w, col.bottom() - moreH - (bottom + ui.s(10))};
    if (shown.empty()) {
        const std::string empty = busy_ ? "Chargement…" : filter_ == 3 ? "Aucun joueur choisi : cochez-en dans « En ligne » ou « Tous »." : "Aucun joueur trouvé.";
        const auto lines = raidui::wrap(ui, empty, FontFace::Body, 15, list.w - ui.s(16), 3);
        raidui::drawLines(ui, lines, {list.x + ui.s(8), list.y + ui.s(12)}, 15, ui.s(21), ui.theme().textDim);
    } else {
        const float rowH = L.playerH, gap = ui.s(6);
        ui.beginScroll("players", list, static_cast<float>(shown.size()) * (rowH + gap));
        float ry = list.y - ui.scrollOffset();
        for (const Json& p : shown) {
            const Rect row{list.x, ry, list.w - ui.s(16), rowH};
            ry += rowH + gap;
            const std::string id = jstr(p, "id");
            bool hover = false;
            if (ui.hotspot("p/" + id, row, &hover)) toggle(p);
            if (row.bottom() < list.y || row.y > list.bottom()) continue;
            const bool chosen = isChosen(id), focused = ui.focused("p/" + id);
            r.fillRoundedRect(row, ui.s(8), hover || focused ? Color{24, 36, 72, 255} : kCell);
            r.drawRoundedRect(row, ui.s(8), focused ? ui.theme().accent : chosen ? kGold.withAlpha(150) : kLine, ui.s(focused ? 2.0f : 1.0f));
            tick(r, {row.x + ui.s(10), row.center().y - ui.s(11), ui.s(22), ui.s(22)}, chosen, ui.scale());
            const bool online = jget<bool>(p, "online", false);
            r.fillCircle({row.x + ui.s(46), row.center().y}, ui.s(5), online ? ui.theme().success : Color{90, 100, 136, 255});
            // The tag at the right: what the player already owns (skipped) or has waiting in a gift.
            const auto owned = ownedOf(id, false), pending = ownedOf(id, true);
            std::string tagText;
            if (!owned.empty()) tagText = "possède déjà " + owned.front() + (owned.size() > 1 ? " (+" + std::to_string(owned.size() - 1) + ")" : "");
            else if (!pending.empty()) tagText = "l'attend déjà : " + pending.front();
            const float nameX = row.x + ui.s(60);
            float right = row.right() - ui.s(10);
            if (!tagText.empty()) {
                const float room = (right - nameX) * 0.6f;
                const float f = ui.fitFont(tagText, FontFace::Bold, 12, room - ui.s(14), 0.8f);
                const std::string t = ui.elide(tagText, FontFace::Bold, f, room - ui.s(14));
                const float tw = ui.text().measure(t, FontFace::Bold, ui.s(f)).x + ui.s(14);
                const Rect pill{right - tw, row.center().y - ui.s(12), tw, ui.s(24)};
                r.drawRoundedRect(pill, ui.s(5), kLine, ui.s(1));
                ui.label(t, pill.center(), f, kMuted, Align::Center, FontFace::Bold, VAlign::Middle);
                right = pill.x - ui.s(8);
            }
            std::string name = jstr(p, "username");
            if (const std::string ct = jstr(p, "clan_tag"); !ct.empty()) name += " [" + ct + "]";
            raidui::fit(ui, name, {nameX, row.center().y}, 16, right - nameX, ui.theme().text, FontFace::Bold, Align::Left, true);
        }
        ui.endScroll();
    }
    if (moreH > 0) {
        UIContext::ButtonStyle st;
        st.small = true;
        if (cframe::button(ui, "more", {col.x, col.bottom() - ui.s(46), col.w, ui.s(46)}, busy_ ? "Chargement…" : "Afficher plus", st) && !busy_) loadPlayers(true);
    }
}

void AdminGifts::drawSend(const Rect& col) {
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
    const giftsui::AdminLayout& L = L_;
    float y = title(ui, col, "3 · Message et envoi");
    const std::string before = message_;
    ui.textField("message", {col.x, y, col.w, ui.s(46)}, message_, "Message joint au cadeau (facultatif)", 600);
    if (gifts::textLength(message_) > gifts::kMaxMessage) message_ = gifts::cutText(message_, gifts::kMaxMessage);
    if (message_ != before) confirm_ = false;
    y += ui.s(50);
    const std::string counter = std::to_string(gifts::textLength(gifts::cleanMessage(message_))) + " / " + std::to_string(gifts::kMaxMessage);
    ui.label(counter, {col.right(), y + ui.s(10)}, 14, kMuted, Align::Right, FontFace::Body, VAlign::Middle);
    y += ui.s(26);
    // 2.85: the recipients — the players chosen, or every active player (as many as the server counts).
    {
        const std::string count = activePlayers_ >= 0 ? raids::groupDigits(activePlayers_) : std::string("…");
        const std::vector<std::string> labels = {"Joueurs choisis (" + std::to_string(chosen_.size()) + ")", "Tous les joueurs (" + count + ")"};
        float bottom = y;
        const int hit = raidui::chips(ui, "recipients", col.x, y, col.right(), labels, {!allPlayers_, allPlayers_}, bottom);
        if (hit >= 0 && (hit == 1) != allPlayers_) {
            allPlayers_ = hit == 1;
            confirm_ = false;
            if (allPlayers_) loadActive();
        }
        y = bottom + ui.s(10);
    }
    const int n = static_cast<int>(rewards_.size()), m = allPlayers_ ? std::max(0, activePlayers_) : static_cast<int>(chosen_.size());
    // Two rewards or more: N separate gifts, or one parcel per player holding them (its content and how many it
    // holds hidden from the player until opened).
    if (n >= 2) {
        const std::vector<std::string> labels = {std::to_string(n) + " cadeaux séparés", "1 colis de " + std::to_string(n) + " cadeaux"};
        float bottom = y;
        const int hit = raidui::chips(ui, "bundle", col.x, y, col.right(), labels, {!bundle_, bundle_}, bottom);
        if (hit >= 0 && (hit == 1) != bundle_) {
            bundle_ = hit == 1;
            confirm_ = false;
        }
        y = bottom + ui.s(10);
    }
    const bool bundle = bundled();
    // The summary: N rewards × M players = K gifts (or parcels), and who is skipped (as the server counted it).
    int skippedCount = 0, parcels = allPlayers_ ? m : 0;
    std::vector<std::string> skipped;
    for (const auto& c : allPlayers_ ? std::vector<Chosen>{} : chosen_) {
        auto it = owned_.find(c.id);
        int mine = 0;
        if (it != owned_.end())
            for (const char* k : {"owned", "pending"}) {
                const Json& list = jfield(it->second, k, Json::array());
                if (!list.is_array()) continue;
                for (const auto& rw : rewards_)
                    for (const Json& x : list)
                        if (x.is_string() && x.get<std::string>() == rw.key()) {
                            ++skippedCount;
                            ++mine;
                            if (skipped.size() < 4)
                                skipped.push_back(c.username + (std::string(k) == "owned" ? " possède déjà " : " l'attend déjà : ") + giftart::rewardName(g_, rw));
                        }
            }
        if (mine < n) ++parcels;  // a parcel for this player: at least one reward left for them
    }
    const int total = bundle ? parcels : std::max(0, n * m - skippedCount);
    const std::string units = bundle ? plural(total, "colis", "colis") : plural(total, "cadeau", "cadeaux");
    std::string sum, sub;
    if (n == 0 || m == 0) {
        sum = "Choisissez au moins une récompense et un joueur.";
    } else {
        std::string names;
        for (size_t i = 0; i < rewards_.size() && i < 3; ++i) names += (i ? ", " : "") + giftart::rewardName(g_, rewards_[i]);
        if (rewards_.size() > 3) names += "…";
        if (allPlayers_)  // the skipped ones are counted by the server when it sends: at most this many
            sum = plural(n, "récompense", "récompenses") + " (" + names + ") × tous les joueurs (" + raids::groupDigits(m) + ") = jusqu'à " +
                  (bundle ? raids::groupDigits(total) + " colis" : raids::groupDigits(total) + (total > 1 ? " cadeaux" : " cadeau"));
        else
            sum = plural(n, "récompense", "récompenses") + " (" + names + ") × " + plural(m, "joueur", "joueurs") + " = " + units;
        if (allPlayers_) {
            sub = "Chaque joueur actif · ceux qui les possèdent déjà ou les attendent sont ignorés par le serveur";
        } else if (!skipped.empty()) {
            sub = "Ignoré : ";
            for (size_t i = 0; i < skipped.size(); ++i) sub += (i ? " · " : "") + skipped[i];
            if (skippedCount > static_cast<int>(skipped.size())) sub += " · …";
        } else if (bundle) {
            sub = "Un colis par joueur · emballage " + gifts::wrapName(wrap_) + " · son contenu reste une surprise";
        } else {
            sub = "Emballage " + gifts::wrapName(wrap_) + " · aucun doublon";
        }
    }
    const float sw = col.w - ui.s(24);
    const auto lines = raidui::wrap(ui, sum, FontFace::Body, 15, sw, 3);
    const auto subLines = sub.empty() ? std::vector<std::string>{} : raidui::wrap(ui, sub, FontFace::Body, 14, sw, 3);
    const float lh = ui.s(21);
    const Rect box{col.x, y, col.w, static_cast<float>(lines.size() + subLines.size()) * lh + ui.s(20)};
    r.fillRoundedRect(box, ui.s(8), kCell);
    r.drawRoundedRect(box, ui.s(8), kLine, ui.s(1));
    float ty = box.y + ui.s(10);
    ty += raidui::drawLines(ui, lines, {box.x + ui.s(12), ty}, 15, lh, ui.theme().text);
    raidui::drawLines(ui, subLines, {box.x + ui.s(12), ty}, 14, lh, kMuted);
    y = box.bottom() + ui.s(12);
    // « Aperçu de l'ouverture », « Envoyer K cadeaux »; then the confirmation on the same line.
    const float bh = ui.s(48);
    const std::string sendLabel = allPlayers_ ? std::string("Envoyer à tous") : total > 0 ? "Envoyer " + units : std::string("Envoyer");
    if (!confirm_) {
        const auto rects = std::vector<std::string>{"Aperçu de l'ouverture", sendLabel};
        const float half = (col.w - ui.s(10)) * 0.5f;
        UIContext::ButtonStyle pv;
        pv.enabled = !rewards_.empty();
        pv.fontSize = ui.fitFont(rects[0], FontFace::Bold, 18, half - ui.s(24), 0.6f);
        if (cframe::button(ui, "preview", {col.x, y, half, bh}, rects[0], pv)) {
            GiftOpening o;
            o.preview = true;
            o.gift.id = 1;
            o.gift.opened = true;
            o.gift.wrap = wrap_;
            o.gift.from = g_.online.username();
            o.gift.message = gifts::cleanMessage(message_);
            if (bundle) {  // the parcel: each reward after « Suivant », then the empty box
                o.gift.parcel = true;
                o.gift.contents = rewards_;
            } else {
                o.gift.reward = rewards_.front();
            }
            g_.app.pushScene(makeGiftOpening(g_, o));
        }
        UIContext::ButtonStyle sd;
        sd.primary = true;
        sd.enabled = total > 0 && !sending_;
        sd.fontSize = ui.fitFont(sendLabel, FontFace::Bold, 18, half - ui.s(24), 0.6f);
        if (cframe::button(ui, "send", {col.x + half + ui.s(10), y, half, bh}, sendLabel, sd)) {
            confirm_ = true;
            ui.setFocus("ok");
            if (allPlayers_) loadActive();  // the confirmation says the real count, read again now
        }
    } else {
        const std::string ask = allPlayers_ ? "Envoyer à " + raids::groupDigits(m) + (m > 1 ? " joueurs ?" : " joueur ?") : "Envoyer " + units + " ?";
        const float bw = std::min(ui.s(140), col.w * 0.28f);
        const float room = col.w - 2 * bw - ui.s(20);
        raidui::fit(ui, ask, {col.x, y + bh * 0.5f}, 16, room, kGold, FontFace::Bold);
        UIContext::ButtonStyle ok;
        ok.primary = true;
        ok.enabled = !sending_;
        ok.fontSize = 17;
        if (cframe::button(ui, "ok", {col.right() - 2 * bw - ui.s(10), y, bw, bh}, "Confirmer", ok)) send();
        UIContext::ButtonStyle ko;
        ko.fontSize = 17;
        if (cframe::button(ui, "ko", {col.right() - bw, y, bw, bh}, "Annuler", ko)) confirm_ = false;
    }
    y += bh + ui.s(16);
    // « Derniers envois »: each send, its gifts opened, the unopened ones withdrawn on demand.
    raidui::fit(ui, "Derniers envois", {col.x, y + ui.s(12)}, 16, col.w, kGold, FontFace::Bold);
    y += ui.s(30);
    const Rect list{col.x, y, col.w, col.bottom() - y};
    if (history_.empty()) {
        raidui::fit(ui, historyLoaded_ ? "Aucun envoi pour l'instant." : "Chargement…", {list.x, list.y + ui.s(14)}, 15, list.w, ui.theme().textDim);
        return;
    }
    const float rowH = L.historyH, gap = ui.s(6);
    ui.beginScroll("history", list, static_cast<float>(history_.size()) * (rowH + gap));
    float ry = list.y - ui.scrollOffset();
    for (const Json& s : history_) {
        const Rect row{list.x, ry, list.w - ui.s(16), rowH};
        ry += rowH + gap;
        const int64_t id = jget<int64_t>(s, "id", 0);
        const bool parcelSend = jget<bool>(s, "bundle", false);
        const int waiting = jget<int>(s, "waiting", 0), opened = jget<int>(s, "opened", 0), cancelled = jget<int>(s, "cancelled", 0);
        const int total2 = jget<int>(s, "total", 0) - cancelled;
        const bool visible = !(row.bottom() < list.y || row.y > list.bottom());
        if (visible) {
            r.fillRoundedRect(row, ui.s(8), kCell);
            r.drawRoundedRect(row, ui.s(8), kLine, ui.s(1));
        }
        float right = row.right() - ui.s(10);
        if (waiting > 0) {
            const bool asking = cancelAsk_ == id;
            const std::string label = asking ? "Confirmer" : "Retirer";
            UIContext::ButtonStyle st;
            st.small = true;
            st.danger = asking;
            st.fontSize = 14;
            const float bw = std::min(ui.s(118), row.w * 0.3f);
            if (cframe::button(ui, "cancel/" + std::to_string(id), {right - bw, row.y + ui.s(8), bw, rowH - ui.s(16)}, label, st)) {
                if (asking) cancelSend(id, parcelSend);
                else cancelAsk_ = id;
            }
            right -= bw + ui.s(8);
        }
        if (!visible) continue;
        std::string what;
        const Json& rw = jfield(s, "rewards", Json::array());
        if (rw.is_array())
            for (const Json& x : rw) {
                const gifts::Reward one{jstr(x, "kind"), jstr(x, "id"), jstr(x, "name")};
                what += (what.empty() ? "" : ", ") + giftart::rewardName(g_, one);
            }
        // A send of parcels: « Colis (N) », then what each held (its counts are parcels).
        if (parcelSend) what = "Colis (" + std::to_string(rw.is_array() ? rw.size() : 0) + ") · " + what;
        std::string who;
        const Json& names = jfield(s, "names", Json::array());
        const int recipients = jget<int>(s, "recipients", 0);
        if (names.is_array())
            for (const Json& x : names)
                if (x.is_string()) who += (who.empty() ? "" : ", ") + x.get<std::string>();
        if (recipients > static_cast<int>(names.size()) && !who.empty()) who += "… (" + raids::groupDigits(recipients) + " joueurs)";
        const std::string info = stamp(jstr(s, "at")) + " · ouverts " + std::to_string(opened) + " / " + std::to_string(total2) +
                                 (cancelled > 0 ? " · " + plural(cancelled, "retiré", "retirés") : std::string());
        const float x0 = row.x + ui.s(12), room = right - x0;
        raidui::fit(ui, what, {x0, row.y + rowH * 0.32f}, 15, room, ui.theme().text, FontFace::Bold);
        {
            const float iw = std::min(raidui::textW(ui, info, 13), room * 0.62f);
            raidui::fit(ui, info, {right, row.y + rowH * 0.72f}, 13, iw, kMuted, FontFace::Body, Align::Right);
            raidui::fit(ui, who, {x0, row.y + rowH * 0.72f}, 13, room - iw - ui.s(10), kMuted, FontFace::Body, Align::Left, true);
        }
    }
    ui.endScroll();
}

}  // namespace cosmic
