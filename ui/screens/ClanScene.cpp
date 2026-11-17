// COSMIC ONLINE › CLANS (2.66): the player's clan (its page), the search of the clans, their
// ranking, and the creation of a clan. The page of a clan (maquette « Page d'un clan »): its
// emblem, name, [TAG], motto and pills; its members and their role (their presence only for the
// clan's members, and only as each player shares it); its best team on every EXTRÊME level
// (Solo today: the server already says `mode` and `team_size` for Duo and Trio); the pending
// applications (chief and officers only); its discussion (a group conversation of SOCIAL).
// 2.63.0: the page is the clan's public sheet, opened from search, the ranking, a profile or a
// « [TAG] » clicked anywhere (makeClanByTag); the tab « Gestion » (chief and officers): the
// applications received, the invitations sent, the members and their roles, the settings.
//
// Every rule is the server's (server/src/clans.ts): the screen only offers what the player's
// role allows and shows what the server answers. Nothing here needs an account to be looked at:
// the clans, their pages and their ranking are public; joining or creating one needs it. Every
// read goes through OnlineService (never blocking, kept in the cache to be shown offline).
//
// Placement: every text is measured into the room left by what is placed before it (buttons
// first, then the labels between them), shortened with « … » or made smaller, never drawn over
// another element.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include "engine/core/I18n.h"
#include "engine/input/InputManager.h"
#include "game/GameServices.h"
#include "game/online/Clans.h"
#include "game/online/OnlineSession.h"
#include "ui/online/ClanWidgets.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/OnlineNav.h"

namespace cosmic {

namespace {

constexpr float kPagePoll = 30.0f;  // the page (members, presence, requests) while it is shown
constexpr float kChatPoll = 8.0f;   // the discussion while it is shown
constexpr size_t kMaxChat = 500;    // a message (server: MAX_MESSAGE_LENGTH)

const Color kGold{255, 210, 122, 255};
const Color kViolet{166, 107, 255, 255};
const Color kPanel{15, 23, 48, 235};
const Color kRow{10, 17, 36, 230};

/// A field of a JSON object without copying it (a null value when missing).
const Json& at(const Json& j, const char* key) {
    static const Json kNull;
    if (!j.is_object()) return kNull;
    auto it = j.find(key);
    return it == j.end() ? kNull : *it;
}

std::tm localOf(const std::string& iso, bool* ok) {
    std::tm m{};
    auto t = parseIsoUtc(iso);
    *ok = t.has_value();
    if (!t) return m;
    std::time_t tt = std::chrono::system_clock::to_time_t(*t);
#ifdef _WIN32
    localtime_s(&m, &tt);
#else
    localtime_r(&tt, &m);
#endif
    return m;
}

/// "02/11/2026" (local date of this computer), "" if unreadable.
std::string dayText(const std::string& iso) {
    bool ok = false;
    std::tm m = localOf(iso, &ok);
    if (!ok) return {};
    char b[48];
    std::snprintf(b, sizeof b, "%02d/%02d/%04d", m.tm_mday, m.tm_mon + 1, m.tm_year + 1900);
    return b;
}

/// "21:04" today, else "16/11 21:04".
std::string clockText(const std::string& iso) {
    bool ok = false;
    std::tm m = localOf(iso, &ok);
    if (!ok) return {};
    std::time_t now = std::time(nullptr);
    std::tm n{};
#ifdef _WIN32
    localtime_s(&n, &now);
#else
    localtime_r(&now, &n);
#endif
    char b[48];
    if (m.tm_year == n.tm_year && m.tm_yday == n.tm_yday) std::snprintf(b, sizeof b, "%02d:%02d", m.tm_hour, m.tm_min);
    else std::snprintf(b, sizeof b, "%02d/%02d %02d:%02d", m.tm_mday, m.tm_mon + 1, m.tm_hour, m.tm_min);
    return b;
}

/// `t` at `size` when it fits `room`, else smaller (down to 75 %), then shortened with « … ».
/// `verbatim`: a player's words (names, motto, messages) are never translated.
Vec2 fitLabel(UIContext& ui, const std::string& t, Vec2 pos, float size, float room, Color c, FontFace face = FontFace::Body,
              Align a = Align::Left, bool verbatim = false) {
    if (t.empty() || room <= ui.s(10)) return {};
    i18n::Verbatim v(verbatim);
    const float f = ui.fitFont(t, face, size, room, 0.75f);
    return ui.label(ui.elide(t, face, f, room), pos, f, c, a, face, VAlign::Middle);
}

/// Width of `t` at `size` (unscaled), translated as it will be drawn.
float textW(UIContext& ui, const std::string& t, float size, FontFace face = FontFace::Body, bool verbatim = false) {
    i18n::Verbatim v(verbatim);
    return ui.text().measure(t, face, ui.s(size)).x;
}

void panel(UIContext& ui, const Rect& r, Color border) {
    ui.r().fillRoundedRect(r, ui.s(14), kPanel);
    ui.r().drawRoundedRect(r, ui.s(14), border, ui.s(1.2f));
}

/// A section's title in small capitals at the top of `r` (and `right`, a value at its right
/// edge, measured first); the y under it.
float sectionTitle(UIContext& ui, const Rect& r, const std::string& t, const std::string& right = {}, Color rightColor = {}) {
    const float cy = r.y + ui.s(24), x = r.x + ui.s(16), xr = r.right() - ui.s(16);
    float rw = right.empty() ? 0.0f : std::min(textW(ui, right, 14, FontFace::Bold) + ui.s(4), (xr - x) * 0.45f);
    if (rw > 0.0f) fitLabel(ui, right, {xr, cy}, 14, rw, rightColor, FontFace::Bold, Align::Right);
    fitLabel(ui, t, {x, cy}, 14, xr - x - rw - (rw > 0.0f ? ui.s(12) : 0.0f), ui.theme().textDim, FontFace::Bold);
    return r.y + ui.s(44);
}

/// A small pill (text in a rounded frame) whose left edge is `x`; returns its width (0 when it
/// does not fit before `maxRight`).
float pill(UIContext& ui, float x, float cy, const std::string& t, Color border, Color text, float maxRight) {
    const float w = textW(ui, t, 13) + ui.s(20);
    if (x + w > maxRight) return 0.0f;
    Rect r{x, cy - ui.s(13), w, ui.s(26)};
    ui.r().drawRoundedRect(r, ui.s(10), border, ui.s(1.2f));
    ui.label(t, r.center(), 13, text, Align::Center, FontFace::Body, VAlign::Middle);
    return w;
}

class ClanScene : public Scene {
public:
    ClanScene(GameServices& g, std::string clanId, bool pushed, std::string tag = {})
        : g_(g), ui_(g), wanted_(std::move(clanId)), wantedTag_(std::move(tag)), pushed_(pushed) {}

    void onEnter() override {
        g_.playMusic("music/menu_theme.ogg");
        bg_.init(g_);
        g_.ui.setFocus(pushed_ ? "clans/tabs" : "online/nav/clans");
        wasLoggedIn_ = g_.online.loggedIn();
        if (!wanted_.empty()) {
            entryId_ = wanted_;
            openPage(wanted_, true);
        } else if (!wantedTag_.empty()) {
            resolveTag();
        }
        reloadAll();
    }

    void onResume() override { reloadAll(); }

    void onExit() override { g_.online.setOpenConversation({}); }

    void update(float dt) override {
        bg_.update(dt);
        const bool top = g_.app.topScene() == this;
        // Logged in or out meanwhile (the account overlay): everything is read again.
        if (g_.online.loggedIn() != wasLoggedIn_) {
            wasLoggedIn_ = g_.online.loggedIn();
            if (!pageFromList_) closePage();
            reloadAll();
        }
        pageTimer_ += dt;
        chatTimer_ += dt;
        if (top && (pageShown() || manageShown()) && pageTimer_ >= kPagePoll) loadPage();
        // The discussion only while it is on screen (reading it marks it read).
        if (top && pageShown() && !convId_.empty() && (!chatLoaded_ || chatTimer_ >= kChatPoll)) loadChat();
        if (searchDirty_ && g_.ui.time() - searchT_ > 0.35f) {
            searchDirty_ = false;
            loadSearch(false);
        }
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        layout();
        bg_.render(g_, 0.5f);
        const bool top = g_.app.topScene() == this;
        ui.inputEnabled = top && modal_ == Modal::None;
        drawHeader();
        const float navH = drawOnlineNav(g_, zoneA_, OnlineSection::Clans);
        drawInfoCard(navH);
        ui.pushId("clans");
        drawBody();
        ui.popId();
        ui.inputEnabled = top;
        if (modal_ != Modal::None) drawModal();
        else if (ui.backPressed() && !ui.editingText()) back();
        ui.end();
    }

private:
    enum class Modal { None, Form, Invite, Apply, Member, Confirm, Transfer };
    enum class TagState { None, Busy, Failed };  // a sheet asked for by its tag (makeClanByTag)
    static constexpr int kTabMine = 0, kTabSearch = 1, kTabRanking = 2, kTabManage = 3;

    // ------------------------------------------------------------ helpers
    void say(const std::string& m, bool error) {
        status_ = m;
        statusError_ = error;
        statusT_ = g_.ui.time();
    }

    std::string myClanId() const {
        const Json& c = at(mine_, "clan");
        return c.is_object() ? jstr(c, "id") : std::string();
    }
    std::string myRole() const { return jstr(at(page_, "me"), "role"); }
    /// The player's role in their clan, as GET /clans/me last said ("" without a clan).
    std::string myClanRole() const { return jstr(at(mine_, "clan"), "role"); }
    /// A clan's page is on screen (« Mon clan », or opened from a list).
    bool pageShown() const { return !pageId_.empty() && (pageFromList_ || tab_ == kTabMine); }
    /// The tab « Gestion » shows the page of the player's clan (its requests, members, settings).
    bool manageShown() const { return tab_ == kTabManage && !pageFromList_ && !pageId_.empty() && pageId_ == myClanId(); }
    /// The player left their clan (or dissolved it): « Mon clan » shows no page until GET /clans/me answers.
    void forgetMyClan() {
        if (mine_.is_object()) mine_["clan"] = nullptr;
        pageFromList_ = false;
        closePage();
    }

    void back() {
        // Opened over another screen on a clan's sheet: back there at once.
        if (pushed_ && pageFromList_ && !entryId_.empty() && pageId_ == entryId_) g_.app.popScene();
        else if (!pageId_.empty() && pageFromList_) closePage();
        else if (pushed_) g_.app.popScene();
        else g_.app.switchScene(makeOnlineRanking(g_));
    }

    void askAccount() { g_.app.pushScene(makeOnlineAccount(g_, !g_.settings.online.onboardingDone, false)); }

    /// The tab « Gestion » on the page of the player's clan.
    void goManage() {
        tab_ = kTabManage;
        tagState_ = TagState::None;
        if (pageFromList_ && pageId_ == myClanId()) pageFromList_ = false;
        else if (pageFromList_) closePage();  // reopens the player's clan (tab « Gestion »)
        if (!myClanId().empty() && pageId_ != myClanId()) openPage(myClanId(), false);
    }

    /// makeClanByTag: the clan whose tag was clicked (GET /clans?q=, the exact tag first).
    void resolveTag() {
        tagState_ = TagState::Busy;
        tagError_.clear();
        const std::string tag = wantedTag_;
        g_.online.get("/api/v1/clans?limit=10&q=" + online::encodeQueryValue(tag), OnlineService::guard(life_, [this, tag](const ApiResult& r) {
                          if (tagState_ != TagState::Busy) return;  // a tab chosen meanwhile
                          if (!r.ok) {
                              tagState_ = TagState::Failed;
                              tagError_ = r.message;
                              return;
                          }
                          const std::string id = clans::clanIdForTag(at(r.data, "clans"), tag);
                          if (id.empty()) {
                              tagState_ = TagState::Failed;
                              tagError_ = "Aucun clan ne porte le tag " + clans::bracket(tag) + " : il a peut-être changé ou été dissous.";
                              return;
                          }
                          tagState_ = TagState::None;
                          entryId_ = id;
                          openPage(id, true);
                      }));
    }

    // ------------------------------------------------------------- data
    void reloadAll() {
        loadMine();
        if (!pageId_.empty()) loadPage();
        if (tab_ == kTabSearch) loadSearch(false);
        if (tab_ == kTabRanking) loadRanking(false);
    }

    void loadMine() {
        if (!g_.online.loggedIn()) {
            mine_ = Json();
            mineLoaded_ = true;
            return;
        }
        int gen = ++mineGen_;
        g_.online.get("/api/v1/clans/me", OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                          if (gen != mineGen_) return;
                          mineLoaded_ = true;
                          if (!r.ok) return say(r.message, true);
                          mine_ = r.data;
                          mineCache_ = r.fromCache;
                          g_.online.noteMyClan(r.data);
                          // « Mon clan » (and « Gestion »): the page of the player's clan.
                          const std::string id = myClanId();
                          if (tab_ == kTabManage && !clans::canManage(myClanRole())) tab_ = kTabMine;  // no longer chief or officer
                          if ((tab_ == kTabMine || tab_ == kTabManage) && !pageFromList_) {
                              if (!id.empty() && pageId_ != id) openPage(id, false);
                              else if (id.empty() && !pageId_.empty()) closePage();
                          }
                      }));
    }

    void openPage(const std::string& id, bool fromList) {
        if (pageId_ != id) {
            page_ = Json();
            pageLoaded_ = false;
            pageError_.clear();
            messages_ = Json::array();
            chatLoaded_ = false;
            convId_.clear();
            g_.online.setOpenConversation({});
        }
        pageId_ = id;
        pageFromList_ = fromList;
        loadPage();
    }

    void closePage() {
        pageId_.clear();
        pageFromList_ = false;
        page_ = Json();
        pageLoaded_ = false;
        convId_.clear();
        messages_ = Json::array();
        g_.online.setOpenConversation({});
        // Back on « Mon clan » (or « Gestion »): the player's own page, if they have a clan.
        if ((tab_ == kTabMine || tab_ == kTabManage) && !myClanId().empty()) openPage(myClanId(), false);
    }

    void loadPage() {
        pageTimer_ = 0.0f;
        if (pageId_.empty()) return;
        const std::string id = pageId_;
        int gen = ++pageGen_;
        g_.online.get("/api/v1/clans/" + id, OnlineService::guard(life_, [this, gen, id](const ApiResult& r) {
                          if (gen != pageGen_ || id != pageId_) return;
                          pageLoaded_ = true;
                          if (!r.ok) {
                              pageError_ = r.message;
                              if (r.status == 404) page_ = Json();
                              return;
                          }
                          pageError_.clear();
                          page_ = r.data;
                          pageCache_ = r.fromCache;
                          // The applications waiting in the player's clan: the badge of « Gestion ».
                          if (id == myClanId()) myApplications_ = static_cast<int>(clans::pendingRequests(at(page_, "requests"), "apply").size());
                          const std::string conv = jstr(page_, "conversation_id");
                          if (conv != convId_) {
                              convId_ = conv;
                              messages_ = Json::array();
                              chatLoaded_ = false;
                              if (pageShown()) {
                                  g_.online.setOpenConversation(conv);
                                  if (!conv.empty()) loadChat();
                              }
                          }
                      }));
    }

    void loadChat() {
        chatTimer_ = 0.0f;
        if (convId_.empty() || chatBusy_) return;
        chatBusy_ = true;
        const std::string conv = convId_;
        g_.online.get("/api/v1/social/conversations/" + conv + "/messages?limit=40", OnlineService::guard(life_, [this, conv](const ApiResult& r) {
                          chatBusy_ = false;
                          if (conv != convId_) return;
                          chatLoaded_ = true;
                          if (!r.ok) return;
                          const Json& list = at(r.data, "messages");
                          const std::string newest = list.is_array() && !list.empty() ? jstr(list.back(), "id") : std::string();
                          if (newest != newestSeen_) {
                              if (!newestSeen_.empty() || messages_.empty()) stickBottom_ = true;
                              newestSeen_ = newest;
                              // Read on screen: the counters of SOCIAL follow at once.
                              if (!r.fromCache && !newest.empty()) {
                                  g_.online.send("POST", "/api/v1/social/conversations/" + conv + "/read", Json::object(), {});
                                  g_.online.noteConversationRead(conv);
                              }
                          }
                          messages_ = list.is_array() ? list : Json::array();
                      }));
    }

    void sendChat() {
        if (convId_.empty() || sending_) return;
        std::string text = draft_;
        sending_ = true;
        const std::string conv = convId_;
        g_.online.send("POST", "/api/v1/social/conversations/" + conv + "/messages", {{"body", text}}, OnlineService::guard(life_, [this](const ApiResult& r) {
                           sending_ = false;
                           if (!r.ok) return say(r.message, true);
                           draft_.clear();
                           stickBottom_ = true;
                           loadChat();
                       }));
    }

    void loadSearch(bool more) {
        int offset = more ? static_cast<int>(search_.size()) : 0;
        std::string path = "/api/v1/clans?limit=20&offset=" + std::to_string(offset);
        const std::string q = clans::cleanName(query_);
        if (!q.empty()) path += "&q=" + online::encodeQueryValue(q);
        int gen = ++searchGen_;
        searchBusy_ = true;
        g_.online.get(path, OnlineService::guard(life_, [this, gen, more](const ApiResult& r) {
                          if (gen != searchGen_) return;
                          searchBusy_ = false;
                          searchLoaded_ = true;
                          if (!r.ok) return say(r.message, true);
                          if (!more) search_ = Json::array();
                          for (const auto& c : at(r.data, "clans")) search_.push_back(c);
                          searchMore_ = jget<bool>(r.data, "has_more", false);
                          searchCache_ = r.fromCache;
                      }));
    }

    void loadRanking(bool more) {
        int offset = more ? static_cast<int>(ranking_.size()) : 0;
        int gen = ++rankingGen_;
        rankingBusy_ = true;
        g_.online.get("/api/v1/clans/ranking?limit=25&offset=" + std::to_string(offset), OnlineService::guard(life_, [this, gen, more](const ApiResult& r) {
                          if (gen != rankingGen_) return;
                          rankingBusy_ = false;
                          rankingLoaded_ = true;
                          if (!r.ok) return say(r.message, true);
                          if (!more) ranking_ = Json::array();
                          for (const auto& e : at(r.data, "ranking")) ranking_.push_back(e);
                          rankingMore_ = jget<bool>(r.data, "has_more", false);
                          rankingTotal_ = jget<int>(r.data, "total", 0);
                          rankingMe_ = at(r.data, "me");
                          rankingCache_ = r.fromCache;
                      }));
    }

    /// A change sent to the server; `done` (a sentence) told on success, then everything read again.
    void act(const std::string& method, const std::string& path, const Json& body, const std::string& done, std::function<void(const ApiResult&)> then = {}) {
        if (busy_) return;
        busy_ = true;
        g_.online.send(method, path, body, OnlineService::guard(life_, [this, done, then](const ApiResult& r) {
                           busy_ = false;
                           if (!r.ok) {
                               say(r.message, true);
                               return;
                           }
                           if (!done.empty()) say(done, false);
                           if (then) then(r);
                           loadMine();
                           loadPage();
                           g_.online.refreshMyClan();
                       }));
    }

    void confirm(std::string text, std::string action, std::function<void()> f, bool danger = true) {
        confirmText_ = std::move(text);
        confirmAction_ = std::move(action);
        confirmFn_ = std::move(f);
        confirmDanger_ = danger;
        modal_ = Modal::Confirm;
    }

    // ------------------------------------------------------------- layout
    void layout() {
        UIContext& ui = g_.ui;
        const float W = ui.screenW(), H = ui.screenH(), m = ui.s(22);
        header_ = {0, 0, W, ui.s(64)};
        zoneA_ = {m, header_.bottom() + ui.s(8), ui.s(272), H - header_.bottom() - ui.s(8) - m};
        zoneB_ = {zoneA_.right() + ui.s(18), zoneA_.y, W - m - zoneA_.right() - ui.s(18), zoneA_.h};
    }

    void drawHeader() {
        UIContext& ui = g_.ui;
        ui.r().fillGradientV(header_, Color{6, 10, 24, 230}, Color{6, 10, 24, 0});
        Vec2 tsz = ui.label("COSMIC ONLINE", {ui.s(32), header_.center().y}, 34, ui.theme().accent, Align::Left, FontFace::Title, VAlign::Middle);
        Vec2 ssz = ui.label("Clans", {ui.s(32) + tsz.x + ui.s(18), header_.center().y + ui.s(3)}, 19, ui.theme().textDim, Align::Left, FontFace::Body,
                            VAlign::Middle);
        const float connX = zoneB_.right() - ui.s(240);
        ui_.drawConnection({connX, header_.center().y});
        // The last answer of the server (a change, a refusal) between the title and the connection.
        if (!status_.empty() && ui.time() - statusT_ < 7.0f) {
            const float x = ui.s(32) + tsz.x + ui.s(18) + ssz.x + ui.s(30);
            fitLabel(ui, status_, {x, header_.center().y}, 16, connX - ui.s(24) - x, statusError_ ? ui.theme().danger : ui.theme().success);
        }
    }

    /// Under the navigation: the player's clan in short (or the invitations waiting), sized to
    /// the room left; nothing when it does not fit.
    void drawInfoCard(float navH) {
        UIContext& ui = g_.ui;
        const float top = zoneA_.y + navH + ui.s(16), room = zoneA_.bottom() - top, h = ui.s(92);
        if (h > room || !g_.online.loggedIn() || !mineLoaded_) return;
        Rect card{zoneA_.x, top, zoneA_.w, h};
        ui.r().fillRoundedRect(card, ui.s(14), Color{10, 16, 34, 215});
        ui.r().drawRoundedRect(card, ui.s(14), ui.theme().border, ui.s(1.2f));
        const Json& c = at(mine_, "clan");
        const float x = card.x + ui.s(14);
        if (c.is_object()) {
            drawClanEmblem(ui, {x, card.y + ui.s(16), ui.s(60), ui.s(60)}, clans::emblemFromJson(at(c, "emblem")));
            const float tx = x + ui.s(74), w = card.right() - ui.s(12) - tx;
            fitLabel(ui, clans::bracket(jstr(c, "tag")), {tx, card.y + ui.s(26)}, 17, w, kGold, FontFace::Bold, Align::Left, true);
            fitLabel(ui, jstr(c, "name"), {tx, card.y + ui.s(48)}, 15, w, ui.theme().text, FontFace::Bold, Align::Left, true);
            fitLabel(ui, clans::roleLabel(jstr(c, "role")), {tx, card.y + ui.s(70)}, 14, w, clanRoleColor(ui, jstr(c, "role")));
        } else {
            const int inv = static_cast<int>(at(mine_, "invitations").size());
            const float w = card.w - ui.s(28);
            fitLabel(ui, "Sans clan", {x, card.y + ui.s(28)}, 18, w, ui.theme().text, FontFace::Bold);
            std::string line = inv > 0 ? std::to_string(inv) + (inv > 1 ? " invitations reçues" : " invitation reçue") : "Aucune invitation";
            fitLabel(ui, line, {x, card.y + ui.s(58)}, 15, w, inv > 0 ? ui.theme().warm : ui.theme().textDim);
        }
    }

    // --------------------------------------------------------------- body
    void drawBody() {
        UIContext& ui = g_.ui;
        const int invitations = static_cast<int>(at(mine_, "invitations").size());
        // « Gestion » (2.63.0): for the chief and the officers, next to « Mon clan ».
        const bool manager = g_.online.loggedIn() && clans::canManage(myClanRole());
        if (tab_ == kTabManage && !manager && mineLoaded_) tab_ = kTabMine;
        std::vector<std::string> labels = {"Mon clan", "Rechercher", "Classement des clans"};
        std::vector<int> ids = {kTabMine, kTabSearch, kTabRanking};
        std::vector<int> badges = {invitations, 0, 0};
        if (manager) {
            labels.insert(labels.begin() + 1, "Gestion");
            ids.insert(ids.begin() + 1, kTabManage);
            badges.insert(badges.begin() + 1, myApplications_);  // applications waiting for an answer
        }
        Rect tabsR{zoneB_.x, zoneB_.y, std::min(zoneB_.w * (manager ? 0.7f : 0.62f), ui.s(manager ? 880 : 700)), ui.s(48)};
        const int shownTab = static_cast<int>(std::find(ids.begin(), ids.end(), tab_) - ids.begin()) % static_cast<int>(ids.size());
        int tab = shownTab;
        ui.tabs("tabs", tabsR, labels, tab, badges);
        if (manager) ui.newBadge("tabs", {tabsR.x + tabsR.w / static_cast<float>(labels.size()), tabsR.y, tabsR.w / static_cast<float>(labels.size()), tabsR.h},
                                 "clans.tab.manage");  // « NOUVEAU »
        if (tab != shownTab) {
            tab_ = ids[static_cast<size_t>(std::clamp(tab, 0, static_cast<int>(ids.size()) - 1))];
            tagState_ = TagState::None;
            if (pageFromList_) closePage();
            if ((tab_ == kTabMine || tab_ == kTabManage) && !myClanId().empty()) openPage(myClanId(), false);
            if (tab_ == kTabSearch && !searchLoaded_) loadSearch(false);
            if (tab_ == kTabRanking) loadRanking(false);
        }
        // At the right of the tabs: « Retour » on a page opened from a list, the offline note.
        float right = zoneB_.right();
        const bool showPage = pageShown();
        // The discussion on screen is « open »: its messages are not announced by a toast.
        g_.online.setOpenConversation(showPage ? convId_ : std::string());
        if (showPage && pageFromList_) {
            UIContext::ButtonStyle st;
            st.small = true;
            // The sheet the screen was opened on (over a profile, SOCIAL…): back to that screen.
            const bool entry = pushed_ && pageId_ == entryId_;
            const std::string label = entry ? "Retour" : "Retour à la liste";
            const float bw = std::max(ui.s(130), textW(ui, label, 16, FontFace::Bold) + ui.s(36));
            if (ui.button("back", {right - bw, tabsR.y + ui.s(4), bw, ui.s(40)}, label, st)) back();
            right -= bw + ui.s(16);
        }
        const bool cache = showPage || tab_ == kTabManage ? pageCache_ : tab_ == kTabSearch ? searchCache_ : tab_ == kTabRanking ? rankingCache_ : mineCache_;
        if (cache) fitLabel(ui, "Hors ligne : données en cache", {right, tabsR.center().y}, 14, right - tabsR.right() - ui.s(16), ui.theme().warm, FontFace::Body,
                            Align::Right);
        Rect body{zoneB_.x, tabsR.bottom() + ui.s(14), zoneB_.w, zoneB_.bottom() - tabsR.bottom() - ui.s(14)};
        if (tagState_ != TagState::None && pageId_.empty()) {
            const std::string t = tagState_ == TagState::Busy ? "Recherche du clan " + clans::bracket(wantedTag_) + "…" : tagError_;
            fitLabel(ui, t, {body.x + ui.s(8), body.y + ui.s(20)}, 17, body.w - ui.s(16), tagState_ == TagState::Busy ? ui.theme().textDim : ui.theme().danger);
        } else if (showPage) drawPage(body);
        else if (tab_ == kTabManage) drawManage(body);
        else if (tab_ == kTabMine) drawWithoutClan(body);
        else if (tab_ == kTabSearch) drawSearch(body);
        else drawRanking(body);
    }

    // ------------------------------------------------------- without a clan
    void drawWithoutClan(const Rect& body) {
        UIContext& ui = g_.ui;
        if (!g_.online.loggedIn()) {
            Rect card{body.x, body.y, std::min(body.w, ui.s(760)), ui.s(10)};
            static const char* kText =
                "Connectez-vous à Cosmic Online pour créer un clan ou en rejoindre un. Les clans, leurs pages et leur classement restent "
                "visibles sans compte, et le jeu se joue sans compte.";
            const float w = card.w - ui.s(48);
            const float th = ui.text().measureWrapped(kText, FontFace::Body, ui.s(17), w).y;
            card.h = ui.s(24) + ui.s(40) + th + ui.s(20) + ui.s(48) + ui.s(24);
            panel(ui, card, ui.theme().border);
            fitLabel(ui, "Les clans de Cosmic Online", {card.x + ui.s(24), card.y + ui.s(44)}, 26, w, ui.theme().text, FontFace::Title);
            ui.labelWrapped(kText, {card.x + ui.s(24), card.y + ui.s(76)}, w, 17, ui.theme().textDim);
            UIContext::ButtonStyle p;
            p.primary = true;
            if (ui.button("login", {card.x + ui.s(24), card.bottom() - ui.s(72), ui.s(240), ui.s(48)}, "Se connecter", p)) askAccount();
            return;
        }
        if (!mineLoaded_) {
            ui.label("Chargement…", {body.x + ui.s(10), body.y + ui.s(20)}, 17, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            return;
        }
        // Left: what a clan is, and the two ways in. Right: the invitations and applications.
        const float leftW = std::clamp(body.w * 0.42f, ui.s(420), ui.s(620));
        Rect left{body.x, body.y, leftW, body.h};
        Rect right{left.right() + ui.s(18), body.y, body.right() - left.right() - ui.s(18), body.h};
        static const char* kAbout =
            "Un clan réunit jusqu'à 50 joueurs autour d'un tag, d'une devise et d'un emblème. Ses membres discutent entre eux et "
            "additionnent leurs meilleurs résultats vérifiés sur les niveaux EXTRÊME pour le classement des clans. Un seul clan par joueur.";
        const float w = left.w - ui.s(48);
        const float th = ui.text().measureWrapped(kAbout, FontFace::Body, ui.s(16), w).y;
        Rect card{left.x, left.y, left.w, ui.s(24) + ui.s(40) + th + ui.s(24) + ui.s(48) * 2 + ui.s(12) + ui.s(24)};
        panel(ui, card, ui.theme().border);
        fitLabel(ui, "Vous n'avez pas encore de clan", {card.x + ui.s(24), card.y + ui.s(44)}, 24, w, ui.theme().text, FontFace::Title);
        ui.labelWrapped(kAbout, {card.x + ui.s(24), card.y + ui.s(76)}, w, 16, ui.theme().textDim);
        float by = card.y + ui.s(76) + th + ui.s(24);
        UIContext::ButtonStyle p;
        p.primary = true;
        if (ui.button("create", {card.x + ui.s(24), by, w, ui.s(48)}, "Créer un clan", p)) openForm(true);
        if (ui.button("find", {card.x + ui.s(24), by + ui.s(60), w, ui.s(48)}, "Rechercher un clan")) {
            tab_ = kTabSearch;
            if (!searchLoaded_) loadSearch(false);
        }
        drawRequestsOfMine(right);
    }

    /// The invitations received (accept, decline, look) and the applications sent (withdraw).
    void drawRequestsOfMine(const Rect& r) {
        UIContext& ui = g_.ui;
        panel(ui, r, ui.theme().border);
        const Json& inv = at(mine_, "invitations");
        const Json& app = at(mine_, "applications");
        float y = sectionTitle(ui, r, "INVITATIONS ET CANDIDATURES");
        Rect view{r.x + ui.s(12), y, r.w - ui.s(24), r.bottom() - ui.s(12) - y};
        if (inv.empty() && app.empty()) {
            ui.labelWrapped("Aucune invitation reçue, aucune candidature envoyée. Un chef ou un officier peut vous inviter ; vous pouvez aussi "
                            "postuler depuis la page d'un clan.",
                            {view.x + ui.s(4), view.y}, view.w - ui.s(8), 15, ui.theme().textDim);
            return;
        }
        const float rowH = ui.s(64);
        ui.beginScroll("requests", view, static_cast<float>(inv.size() + app.size()) * (rowH + ui.s(8)));
        float ry = view.y - ui.scrollOffset();
        auto row = [&](const Json& item, bool invitation) {
            const Json& c = at(item, "clan");
            const std::string id = jstr(c, "id");
            Rect rr{view.x, ry, view.w - ui.s(12), rowH};
            ry += rowH + ui.s(8);
            ui.r().fillRoundedRect(rr, ui.s(8), kRow);
            drawClanEmblem(ui, {rr.x + ui.s(8), rr.y + ui.s(8), ui.s(48), ui.s(48)}, clans::emblemFromJson(at(c, "emblem")));
            // The buttons first (right to left), then the texts in the room they leave.
            UIContext::ButtonStyle st;
            st.small = true;
            float bx = rr.right() - ui.s(8);
            auto btn = [&](const std::string& key, const std::string& label, bool primary, bool danger) {
                UIContext::ButtonStyle s2 = st;
                s2.primary = primary;
                s2.danger = danger;
                const float bw = textW(ui, label, 15, FontFace::Bold) + ui.s(30);
                bx -= bw;
                bool c2 = ui.button(key + id, {bx, rr.y + ui.s(14), bw, ui.s(36)}, label, s2);
                bx -= ui.s(8);
                return c2;
            };
            if (invitation) {
                if (btn("decline", "Refuser", false, false))
                    act("DELETE", "/api/v1/clans/" + id + "/join", Json(), "Invitation refusée.");
                if (btn("accept", "Accepter", true, false))
                    act("POST", "/api/v1/clans/" + id + "/join", Json::object(), "Bienvenue dans le clan !", [this, id](const ApiResult&) {
                        tab_ = kTabMine;
                        openPage(id, false);
                    });
            } else if (btn("withdraw", "\x1F" "clan|Retirer", false, false)) {
                act("DELETE", "/api/v1/clans/" + id + "/join", Json(), "Candidature retirée.");
            }
            if (btn("see", "\x1F" "clan|Voir", false, false)) openPage(id, true);
            const float tx = rr.x + ui.s(68), room = bx - tx - ui.s(4);
            fitLabel(ui, clans::tagged(jstr(c, "name"), jstr(c, "tag")), {tx, rr.y + ui.s(22)}, 17, room, ui.theme().text, FontFace::Bold, Align::Left, true);
            if (invitation) {
                const std::string by = jstr(at(item, "by"), "username");
                fitLabel(ui, by.empty() ? std::string("Invitation reçue") : "Invité par " + by, {tx, rr.y + ui.s(46)}, 14, room, ui.theme().warm);
            } else {
                Vec2 ps = fitLabel(ui, "Candidature envoyée", {tx, rr.y + ui.s(46)}, 14, room, ui.theme().textDim);
                if (const std::string msg = jstr(item, "message"); !msg.empty())
                    fitLabel(ui, " · « " + msg + " »", {tx + ps.x, rr.y + ui.s(46)}, 14, room - ps.x, ui.theme().textDim, FontFace::Body, Align::Left, true);
            }
        };
        for (const auto& i : inv) row(i, true);
        for (const auto& a : app) row(a, false);
        ui.endScroll();
    }

    // ---------------------------------------------------------------- lists
    /// A clan in a list: emblem, [TAG] name, members, joining, points. Returns true when chosen.
    bool clanRow(const std::string& key, const Json& c, const Rect& rr, const std::string& left = {}, Color leftColor = {}) {
        UIContext& ui = g_.ui;
        UIContext::ButtonStyle st;
        st.align = Align::Left;
        const bool chosen = ui.button(key, rr, "", st);
        float x = rr.x + ui.s(12);
        if (!left.empty()) {
            ui.label(left, {x + ui.s(26), rr.center().y}, 22, leftColor, Align::Center, FontFace::Bold, VAlign::Middle);
            x += ui.s(60);
        }
        drawClanEmblem(ui, {x, rr.y + ui.s(8), rr.h - ui.s(16), rr.h - ui.s(16)}, clans::emblemFromJson(at(c, "emblem")));
        x += rr.h - ui.s(16) + ui.s(14);
        // The points at the right edge first.
        const std::string pts = clans::pointsLabel(jget<double>(c, "points", 0.0));
        const float pw = textW(ui, pts, 18, FontFace::Bold);
        const float xr = rr.right() - ui.s(16);
        ui.label(pts, {xr, rr.center().y}, 18, kGold, Align::Right, FontFace::Bold, VAlign::Middle);
        const float room = xr - pw - ui.s(24) - x;
        // [TAG] then the name.
        const std::string tag = clans::bracket(jstr(c, "tag"));
        const float tw = std::min(textW(ui, tag, 18, FontFace::Bold, true), room * 0.4f);
        fitLabel(ui, tag, {x, rr.y + ui.s(22)}, 18, tw, kGold, FontFace::Bold, Align::Left, true);
        fitLabel(ui, jstr(c, "name"), {x + tw + ui.s(10), rr.y + ui.s(22)}, 18, room - tw - ui.s(10), ui.theme().text, FontFace::Bold, Align::Left, true);
        const std::string sub = std::to_string(jget<int>(c, "members", 0)) + " / " + std::to_string(jget<int>(c, "max_members", clans::kMaxMembers)) +
                                " membres · " + clans::policyLabel(jstr(c, "join_policy"));
        fitLabel(ui, sub, {x, rr.y + ui.s(46)}, 14, room, ui.theme().textDim);
        return chosen;
    }

    void drawSearch(const Rect& body) {
        UIContext& ui = g_.ui;
        const float fh = ui.s(46);
        // « Créer un clan » at the right when the player can (logged in, without a clan).
        float fieldRight = body.right();
        if (g_.online.loggedIn() && mineLoaded_ && myClanId().empty()) {
            UIContext::ButtonStyle p;
            p.primary = true;
            const float bw = textW(ui, "Créer un clan", 17, FontFace::Bold) + ui.s(40);
            if (ui.button("createS", {body.right() - bw, body.y, bw, fh}, "Créer un clan", p)) openForm(true);
            fieldRight -= bw + ui.s(16);
        }
        const std::string before = query_;
        ui.textField("q", {body.x, body.y, std::min(fieldRight - body.x, ui.s(560)), fh}, query_, "Tag ou nom d'un clan", 64);
        if (query_ != before) {
            searchDirty_ = true;
            searchT_ = ui.time();
        }
        Rect view{body.x, body.y + fh + ui.s(14), body.w, body.bottom() - body.y - fh - ui.s(14)};
        if (search_.empty()) {
            ui.label(searchLoaded_ ? (query_.empty() ? "Aucun clan pour l'instant : créez le premier !" : "Aucun clan ne correspond.") : "Chargement…",
                     {view.x + ui.s(8), view.y + ui.s(20)}, 17, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            return;
        }
        const float rowH = ui.s(70);
        const float content = static_cast<float>(search_.size()) * (rowH + ui.s(8)) + (searchMore_ ? ui.s(60) : 0.0f);
        ui.beginScroll("results", view, content);
        float y = view.y - ui.scrollOffset();
        for (const auto& c : search_) {
            if (clanRow("c" + jstr(c, "id"), c, {view.x, y, view.w - ui.s(14), rowH})) openPage(jstr(c, "id"), true);
            y += rowH + ui.s(8);
        }
        if (searchMore_) {
            UIContext::ButtonStyle st;
            st.enabled = !searchBusy_;
            if (ui.button("more", {view.x + (view.w - ui.s(260)) * 0.5f, y + ui.s(4), ui.s(260), ui.s(44)}, "Plus de clans", st)) loadSearch(true);
        }
        ui.endScroll();
    }

    void drawRanking(const Rect& body) {
        UIContext& ui = g_.ui;
        static const char* kRule =
            "Points d'un clan : la somme, sur chaque niveau EXTRÊME, du meilleur résultat vérifié par le serveur de ses membres (Solo pour "
            "l'instant ; la coopération Duo et Trio arrive bientôt).";
        const float noteH = ui.text().measureWrapped(kRule, FontFace::Body, ui.s(15), body.w - ui.s(8)).y;
        ui.labelWrapped(kRule, {body.x + ui.s(4), body.y}, body.w - ui.s(8), 15, ui.theme().textDim);
        float y = body.y + noteH + ui.s(12);
        if (rankingMe_.is_object()) {
            const Json& c = at(rankingMe_, "clan");
            const int rank = jget<int>(rankingMe_, "rank", 0);
            std::string line = "Votre clan " + clans::bracket(jstr(c, "tag")) + " : " +
                               (rank > 0 ? clans::ordinal(rank) + " sur " + std::to_string(rankingTotal_) : std::string("pas encore classé")) + " · " +
                               clans::pointsLabel(jget<double>(c, "points", 0.0));
            fitLabel(ui, line, {body.x + ui.s(4), y + ui.s(14)}, 17, body.w - ui.s(8), ui.theme().accent, FontFace::Bold);
            y += ui.s(40);
        }
        Rect view{body.x, y, body.w, body.bottom() - y};
        if (ranking_.empty()) {
            ui.label(rankingLoaded_ ? "Aucun clan classé pour l'instant." : "Chargement…", {view.x + ui.s(8), view.y + ui.s(20)}, 17, ui.theme().textDim,
                     Align::Left, FontFace::Body, VAlign::Middle);
            return;
        }
        const float rowH = ui.s(66);
        const float content = static_cast<float>(ranking_.size()) * (rowH + ui.s(8)) + (rankingMore_ ? ui.s(60) : 0.0f);
        ui.beginScroll("ranking", view, content);
        float ry = view.y - ui.scrollOffset();
        const std::string mine = myClanId();
        for (const auto& e : ranking_) {
            const Json& c = at(e, "clan");
            const int rank = jget<int>(e, "rank", 0);
            const Color rc = rank == 1 ? kGold : rank == 2 ? Color{217, 226, 242, 255} : rank == 3 ? Color{224, 160, 112, 255} : ui.theme().textDim;
            Rect rr{view.x, ry, view.w - ui.s(14), rowH};
            if (jstr(c, "id") == mine) ui.r().fillRoundedRect(rr, ui.s(10), Color{18, 48, 74, 200});
            if (clanRow("r" + jstr(c, "id"), c, rr, std::to_string(rank), rc)) openPage(jstr(c, "id"), true);
            ry += rowH + ui.s(8);
        }
        if (rankingMore_) {
            UIContext::ButtonStyle st;
            st.enabled = !rankingBusy_;
            if (ui.button("more", {view.x + (view.w - ui.s(260)) * 0.5f, ry + ui.s(4), ui.s(260), ui.s(44)}, "Plus de clans", st)) loadRanking(true);
        }
        ui.endScroll();
    }

    // ----------------------------------------------------------------- page
    void drawPage(const Rect& body) {
        UIContext& ui = g_.ui;
        const Json& c = at(page_, "clan");
        if (!c.is_object()) {
            std::string t = !pageLoaded_ ? std::string("Chargement…") : pageError_.empty() ? std::string("Clan introuvable.") : pageError_;
            fitLabel(ui, t, {body.x + ui.s(8), body.y + ui.s(20)}, 17, body.w - ui.s(16), pageError_.empty() ? ui.theme().textDim : ui.theme().danger);
            return;
        }
        Rect band{body.x, body.y, body.w, ui.s(132)};
        drawBand(band, c);
        const float top = band.bottom() + ui.s(16);
        Rect cols{body.x, top, body.w, body.bottom() - top};
        const float membersW = std::clamp(cols.w * 0.32f, ui.s(340), ui.s(520));
        const float asideW = std::clamp(cols.w * 0.25f, ui.s(320), ui.s(380));
        Rect members{cols.x, cols.y, membersW, cols.h};
        Rect aside{cols.right() - asideW, cols.y, asideW, cols.h};
        Rect bests{members.right() + ui.s(16), cols.y, aside.x - ui.s(16) - members.right() - ui.s(16), cols.h};
        drawMembers(members, c);
        drawBests(bests, c);
        drawAside(aside, c);
    }

    /// The band at the top of the page: emblem, name and [TAG], motto, pills; the buttons of the
    /// player's role at its right (placed first: the texts take the room they leave).
    void drawBand(const Rect& band, const Json& c) {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRectGradient(band, ui.s(16), Color{28, 20, 54, 240}, Color{15, 23, 48, 240});
        ui.r().drawRoundedRect(band, ui.s(16), kViolet, ui.s(1.2f));
        const float em = band.h - ui.s(36);
        drawClanEmblem(ui, {band.x + ui.s(20), band.y + ui.s(18), em, em}, clans::emblemFromJson(at(c, "emblem")));
        // Buttons, right to left.
        const std::string role = myRole(), policy = jstr(c, "join_policy");
        struct Btn {
            std::string key, label;
            bool primary = false, danger = false, enabled = true;
        };
        std::vector<Btn> btns;
        // A note under the buttons when the player cannot join (another clan, invitation only, full).
        std::string note;
        if (!role.empty()) {
            if (clans::canManage(role)) {
                btns.push_back({"invite", "Inviter", true});
                btns.push_back({"manage", "Gestion"});  // 2.63.0: requests, members, settings
            }
            btns.push_back({"leave", "\x1F" "clan|Quitter", false, true});
        } else {
            // Joining, as the clan's mode allows (clans::joinOffer: the same rules as the server).
            const clans::JoinOffer offer = clans::joinOffer(c, at(page_, "me"), g_.online.loggedIn(), mineLoaded_, myClanId());
            if (!offer.action.empty()) btns.push_back({offer.action, offer.label, offer.action != "withdraw", false, offer.enabled});
            if (offer.action == "accept") btns.push_back({"decline", "Refuser"});
            note = offer.note;
        }
        float bx = band.right() - ui.s(20);
        const float by = band.y + ui.s(20), bh = ui.s(44);
        const float tx = band.x + ui.s(20) + em + ui.s(20);
        for (const Btn& b : btns) {
            UIContext::ButtonStyle st;
            st.primary = b.primary;
            st.danger = b.danger;
            st.enabled = b.enabled && !busy_;
            const float bw = std::max(ui.s(110), textW(ui, b.label, 16, FontFace::Bold) + ui.s(36));
            bx -= bw;
            if (ui.button("b" + b.key, {bx, by, bw, bh}, b.label, st)) bandAction(b.key, c);
            bx -= ui.s(10);
        }
        float buttonsLeft = btns.empty() ? band.right() - ui.s(20) : bx + ui.s(10);
        if (!note.empty()) {
            if (btns.empty()) {
                // No button: the note takes their place (at most 45 % of the band's text room).
                const float nw = std::min(textW(ui, note, 14) + ui.s(4), (band.right() - ui.s(20) - tx) * 0.45f);
                fitLabel(ui, note, {band.right() - ui.s(20), by + bh * 0.5f}, 14, nw, ui.theme().warm, FontFace::Body, Align::Right);
                buttonsLeft = band.right() - ui.s(20) - nw;
            } else {
                // Under the buttons, never wider than them.
                fitLabel(ui, note, {band.right() - ui.s(20), by + bh + ui.s(18)}, 14, band.right() - ui.s(20) - buttonsLeft, ui.theme().warm, FontFace::Body,
                         Align::Right);
            }
        }
        const float room = buttonsLeft - ui.s(20) - tx;
        // Line 1: the name, then [TAG] after it (the tag measured first).
        const std::string tag = clans::bracket(jstr(c, "tag"));
        const float tagW = std::min(textW(ui, tag, 20, FontFace::Bold, true), room * 0.3f);
        const float nameRoom = room - tagW - ui.s(14);
        Vec2 ns;
        {
            i18n::Verbatim words;  // the clan's name as its chief wrote it
            const std::string name = jstr(c, "name");
            const float nf = ui.fitFont(name, FontFace::Title, 30, nameRoom, 0.6f);
            ns = ui.label(ui.elide(name, FontFace::Title, nf, nameRoom), {tx, band.y + ui.s(34)}, nf, ui.theme().text, Align::Left, FontFace::Title,
                          VAlign::Middle);
        }
        fitLabel(ui, tag, {tx + ns.x + ui.s(12), band.y + ui.s(36)}, 20, tagW, kGold, FontFace::Bold, Align::Left, true);
        // Line 2: « motto » · fondé le … (the date measured first, the motto in the room left).
        const std::string founded = "fondé le " + dayText(jstr(c, "created_at"));
        const std::string motto = jstr(c, "motto");
        const float ly = band.y + ui.s(70);
        if (motto.empty()) {
            fitLabel(ui, "Fondé le " + dayText(jstr(c, "created_at")), {tx, ly}, 15, room, ui.theme().textDim);
        } else {
            const float fw = std::min(textW(ui, " · " + founded, 15), room * 0.4f);
            Vec2 ms = fitLabel(ui, "« " + motto + " »", {tx, ly}, 15, room - fw, Color{201, 211, 238, 255}, FontFace::Body, Align::Left, true);
            fitLabel(ui, " · " + founded, {tx + ms.x, ly}, 15, fw, ui.theme().textDim);
        }
        // Line 3: pills, while they fit.
        float px = tx;
        const float pcy = band.y + ui.s(104), pmax = buttonsLeft - ui.s(20);
        const int members = jget<int>(c, "members", 0);
        auto addPill = [&](const std::string& t, Color border, Color text) {
            float w = pill(ui, px, pcy, t, border, text, pmax);
            if (w > 0.0f) px += w + ui.s(8);
        };
        addPill(std::to_string(members) + " membres sur " + std::to_string(jget<int>(c, "max_members", clans::kMaxMembers)), Color{34, 48, 90, 255},
                ui.theme().text);
        addPill(clans::policyLabel(policy), Color{34, 48, 90, 255}, ui.theme().text);
        const int rank = jget<int>(c, "rank", 0);
        addPill(rank > 0 ? clans::ordinal(rank) + " au classement des clans" : std::string("Pas encore classé"), kViolet, Color{201, 179, 255, 255});
        addPill(clans::pointsLabel(jget<double>(c, "points", 0.0)), kGold, kGold);
        if (!role.empty()) addPill("Vous : " + clans::roleLabel(role), Color{34, 48, 90, 255}, clanRoleColor(ui, role));
    }

    void bandAction(const std::string& key, const Json& c) {
        const std::string id = jstr(c, "id");
        const std::string who = clans::tagged(jstr(c, "name"), jstr(c, "tag"));
        if (key == "invite") {
            inviteName_.clear();
            modal_ = Modal::Invite;
        } else if (key == "manage") {
            goManage();
        } else if (key == "leave") {
            const std::string me = g_.online.playerId();
            std::string text;
            if (myRole() == "leader") {
                const std::string next = clans::successorName(at(page_, "members"), me);
                text = next.empty() ? "Vous êtes le dernier membre : quitter le clan le dissout. Continuer ?"
                                    : "Quitter le clan ? Le rôle de chef passera à " + next + ".";
            } else {
                text = "Quitter le clan " + who + " ? Vous ne verrez plus sa discussion.";
            }
            confirm(text, "\x1F" "clan|Quitter", [this, id, me] {
                act("DELETE", "/api/v1/clans/" + id + "/members/" + me, Json(), "Vous avez quitté le clan.", [this](const ApiResult&) { forgetMyClan(); });
            });
        } else if (key == "login") {
            askAccount();
        } else if (key == "accept" || key == "join") {
            act("POST", "/api/v1/clans/" + id + "/join", Json::object(), "Bienvenue dans le clan !", [this, id](const ApiResult&) {
                tab_ = kTabMine;
                pageFromList_ = false;
                openPage(id, false);
            });
        } else if (key == "decline" || key == "withdraw") {
            act("DELETE", "/api/v1/clans/" + id + "/join", Json(), key == "decline" ? "Invitation refusée." : "Candidature retirée.");
        } else if (key == "apply") {
            applyMessage_.clear();
            modal_ = Modal::Apply;
        }
    }

    void drawMembers(const Rect& r, const Json& c) {
        UIContext& ui = g_.ui;
        panel(ui, r, ui.theme().border);
        const Json& members = at(page_, "members");
        float y = sectionTitle(ui, r, "MEMBRES", std::to_string(members.size()) + " / " + std::to_string(jget<int>(c, "max_members", clans::kMaxMembers)),
                               ui.theme().textDim);
        // The note at the bottom (measured), the list above it.
        static const char* kNote = "Présence : seulement pour les membres du clan, et si le joueur la partage.";
        const float nw = r.w - ui.s(32);
        const float nh = ui.text().measureWrapped(kNote, FontFace::Body, ui.s(13), nw).y;
        const bool note = !myRole().empty();
        Rect view{r.x + ui.s(10), y, r.w - ui.s(20), r.bottom() - ui.s(12) - y - (note ? nh + ui.s(10) : 0.0f)};
        if (note) ui.labelWrapped(kNote, {r.x + ui.s(16), view.bottom() + ui.s(6)}, nw, 13, ui.theme().textDim);
        const float rowH = ui.s(44);
        ui.beginScroll("members", view, static_cast<float>(members.size()) * (rowH + ui.s(6)));
        float ry = view.y - ui.scrollOffset();
        for (const auto& m : members) {
            const Json& p = at(m, "player");
            const std::string pid = jstr(p, "id"), role = jstr(m, "role");
            Rect rr{view.x, ry, view.w - ui.s(12), rowH};
            ry += rowH + ui.s(6);
            UIContext::ButtonStyle st;
            st.align = Align::Left;
            if (ui.button("m" + pid, rr, "", st)) {
                memberSel_ = m;
                modal_ = Modal::Member;
            }
            float x = rr.x + ui.s(10);
            // The presence pill: only the clan's members receive it (the server's rule).
            if (m.contains("presence")) {
                drawPresenceDot(ui, {x + ui.s(5), rr.center().y}, ui.s(5), jstr(m, "presence"), ui.s(1.5f));
                x += ui.s(18);
            }
            const float w = rr.right() - ui.s(10) - x;
            const float nameW = w * 0.40f, roleW = w * 0.22f, bestW = w - nameW - roleW - ui.s(16);
            const bool me = pid == g_.online.playerId();
            fitLabel(ui, jstr(p, "username"), {x, rr.center().y}, 15, nameW - ui.s(6), me ? ui.theme().accent : ui.theme().text, FontFace::Bold, Align::Left,
                     true);
            fitLabel(ui, clans::roleLabel(role), {x + nameW + ui.s(8), rr.center().y}, 13, roleW - ui.s(6), clanRoleColor(ui, role));
            const Json& best = at(m, "best");
            std::string b = best.is_object() ? clans::resultLabel(jget<double>(best, "score", 0.0), jget<bool>(best, "finished", false), 0) + " · " +
                                                   jstr(best, "title")
                                             : std::string("—");
            fitLabel(ui, b, {x + nameW + roleW + ui.s(16), rr.center().y}, 13, bestW, Color{201, 211, 238, 255});
        }
        ui.endScroll();
    }

    void drawBests(const Rect& r, const Json& c) {
        UIContext& ui = g_.ui;
        panel(ui, r, ui.theme().border);
        float y = sectionTitle(ui, r, "MEILLEURES ÉQUIPES DU CLAN", clans::pointsLabel(jget<double>(c, "points", 0.0)), kGold);
        static const char* kNote =
            "Points du clan : la meilleure équipe du clan sur chaque niveau EXTRÊME, vérifiée par le serveur. Solo pour l'instant : la "
            "coopération Duo et Trio arrive bientôt.";
        const float nw = r.w - ui.s(32);
        const float nh = ui.text().measureWrapped(kNote, FontFace::Body, ui.s(13), nw).y;
        Rect view{r.x + ui.s(10), y, r.w - ui.s(20), r.bottom() - ui.s(12) - y - nh - ui.s(10)};
        ui.labelWrapped(kNote, {r.x + ui.s(16), view.bottom() + ui.s(6)}, nw, 13, ui.theme().textDim);
        const Json& levels = at(page_, "levels");
        if (levels.empty()) {
            ui.labelWrapped("Aucun niveau EXTRÊME classé pour l'instant.", {view.x + ui.s(6), view.y}, view.w - ui.s(12), 15, ui.theme().textDim);
            return;
        }
        const float rowH = ui.s(56);
        ui.beginScroll("bests", view, static_cast<float>(levels.size()) * (rowH + ui.s(6)));
        float ry = view.y - ui.scrollOffset();
        for (const auto& l : levels) {
            Rect rr{view.x, ry, view.w - ui.s(12), rowH};
            ry += rowH + ui.s(6);
            const std::string lid = jstr(l, "level_id");
            UIContext::ButtonStyle st;
            st.align = Align::Left;
            // The level's « course à la vérification » (its board) opens from its row.
            if (ui.button("l" + lid, rr, "", st)) g_.app.pushScene(makeRaceBoard(g_, "official", lid));
            const Json& best = at(l, "best");
            const float resultW = ui.s(160), modeW = ui.s(64);
            const float xr = rr.right() - ui.s(12);
            const float tx = rr.x + ui.s(12), room = xr - resultW - modeW - ui.s(16) - tx;
            std::string title = jstr(l, "title");
            if (const LevelEntry* e = g_.content.level(lid)) title = e->name;
            fitLabel(ui, title, {tx, rr.y + ui.s(19)}, 15, room, ui.theme().text, FontFace::Bold);
            if (best.is_object()) {
                std::string team;
                for (const auto& p : at(best, "players")) team += (team.empty() ? "" : " · ") + jstr(p, "username", "?");
                fitLabel(ui, team, {tx, rr.y + ui.s(39)}, 12, room, ui.theme().textDim, FontFace::Body, Align::Left, true);
                fitLabel(ui, clans::modeLabel(jget<int>(best, "team_size", 1)), {xr - resultW - modeW, rr.center().y}, 13, modeW - ui.s(6),
                         Color{201, 211, 238, 255});
                const bool fin = jget<bool>(best, "finished", false);
                fitLabel(ui, clans::resultLabel(jget<double>(best, "score", 0.0), fin, jget<int>(best, "rank", 0)), {xr, rr.center().y}, 14, resultW,
                         fin ? kGold : ui.theme().accent, FontFace::Bold, Align::Right);
            } else {
                fitLabel(ui, "Aucune tentative vérifiée", {tx, rr.y + ui.s(39)}, 12, room, ui.theme().textDim);
                fitLabel(ui, "—", {xr, rr.center().y}, 14, resultW, ui.theme().textDim, FontFace::Bold, Align::Right);
            }
        }
        ui.endScroll();
    }

    /// Right column: the pending requests (chief and officers), then the discussion (members);
    /// for the others, what joining this clan takes.
    void drawAside(const Rect& r, const Json& c) {
        UIContext& ui = g_.ui;
        const std::string role = myRole();
        if (role.empty()) {
            panel(ui, r, ui.theme().border);
            float y = sectionTitle(ui, r, "REJOINDRE CE CLAN");
            const std::string policy = jstr(c, "join_policy");
            std::string text = policy == "open"     ? "Ce clan est ouvert à tous : « Rejoindre » vous y fait entrer aussitôt, s'il reste de la place."
                               : policy == "invite" ? "Ce clan recrute sur invitation : son chef ou un officier doit vous inviter."
                                                    : "Ce clan recrute sur candidature : envoyez un court message, le chef ou un officier vous répond.";
            text += " Sa discussion est réservée à ses membres.";
            ui.labelWrapped(text, {r.x + ui.s(16), y}, r.w - ui.s(32), 15, ui.theme().textDim);
            return;
        }
        Rect chat = r;
        if (clans::canManage(role)) {
            // 2.63.0: the requests are handled in « Gestion »; here, how many wait, and the way there.
            int apps = 0, invs = 0;
            for (const auto& q : at(page_, "requests")) (jstr(q, "kind") == "apply" ? apps : invs) += 1;
            const Rect box{r.x, r.y, r.w, ui.s(76)};
            panel(ui, box, apps > 0 ? ui.theme().warm : ui.theme().border);
            UIContext::ButtonStyle st;
            st.small = true;
            st.primary = apps > 0;
            const float bw = textW(ui, "Gestion", 15, FontFace::Bold) + ui.s(36);
            const Rect b{box.right() - ui.s(14) - bw, box.y + (box.h - ui.s(40)) * 0.5f, bw, ui.s(40)};
            if (ui.button("toManage", b, "Gestion", st)) goManage();
            const float tx = box.x + ui.s(16), room = b.x - ui.s(12) - tx;
            fitLabel(ui, "DEMANDES EN ATTENTE", {tx, box.y + ui.s(25)}, 13, room, ui.theme().textDim, FontFace::Bold);
            const std::string line = apps == 0 && invs == 0 ? std::string("Aucune demande en attente.")
                                                            : std::to_string(apps) + (apps > 1 ? " candidatures" : " candidature") + " · " +
                                                                  std::to_string(invs) + (invs > 1 ? " invitations envoyées" : " invitation envoyée");
            fitLabel(ui, line, {tx, box.y + ui.s(51)}, 15, room, apps > 0 ? ui.theme().warm : ui.theme().text);
            chat = {r.x, box.bottom() + ui.s(12), r.w, r.bottom() - box.bottom() - ui.s(12)};
        }
        drawChat(chat);
    }

    // ------------------------------------------------------------- « Gestion » (2.63.0)
    /// The chief and the officers: the applications received and the invitations sent (left),
    /// the members and their roles (middle), the clan's settings (right; changed by the chief).
    void drawManage(const Rect& body) {
        UIContext& ui = g_.ui;
        const Json& c = at(page_, "clan");
        if (!manageShown() || !c.is_object()) {
            const std::string t = myClanId().empty() ? std::string("Vous n'avez pas de clan.")
                                  : !pageLoaded_     ? std::string("Chargement…")
                                  : pageError_.empty() ? std::string("Clan introuvable.")
                                                       : pageError_;
            fitLabel(ui, t, {body.x + ui.s(8), body.y + ui.s(20)}, 17, body.w - ui.s(16), pageError_.empty() ? ui.theme().textDim : ui.theme().danger);
            return;
        }
        const std::string role = myRole();
        if (!clans::canManage(role)) {
            fitLabel(ui, "Seuls le chef et les officiers du clan gèrent ses demandes et ses membres.", {body.x + ui.s(8), body.y + ui.s(20)}, 17,
                     body.w - ui.s(16), ui.theme().textDim);
            return;
        }
        const float leftW = std::clamp(body.w * 0.36f, ui.s(380), ui.s(560));
        const float rightW = std::clamp(body.w * 0.27f, ui.s(320), ui.s(420));
        const Rect left{body.x, body.y, leftW, body.h};
        const Rect right{body.right() - rightW, body.y, rightW, body.h};
        const Rect mid{left.right() + ui.s(16), body.y, right.x - ui.s(16) - left.right() - ui.s(16), body.h};
        const float half = std::floor((left.h - ui.s(12)) * 0.5f);
        drawPending({left.x, left.y, left.w, half}, true);
        drawPending({left.x, left.y + half + ui.s(12), left.w, left.h - half - ui.s(12)}, false);
        drawManageMembers(mid, c, role);
        drawSettings(right, c, role);
    }

    /// A box's title at its left and a button at its right (measured first); true when clicked.
    /// `y` receives the top of the box's content.
    bool titleWithButton(const Rect& r, const std::string& title, const std::string& key, const std::string& label, float& y) {
        UIContext& ui = g_.ui;
        UIContext::ButtonStyle st;
        st.small = true;
        st.enabled = !busy_;
        const float bw = std::min(textW(ui, label, 14, FontFace::Bold) + ui.s(30), r.w * 0.5f);
        const Rect b{r.right() - ui.s(12) - bw, r.y + ui.s(8), bw, ui.s(34)};
        const bool clicked = ui.button(key, b, label, st);
        fitLabel(ui, title, {r.x + ui.s(16), r.y + ui.s(25)}, 14, b.x - ui.s(12) - r.x - ui.s(16), ui.theme().textDim, FontFace::Bold);
        y = r.y + ui.s(50);
        return clicked;
    }

    /// The applications received (`applications`: accept, refuse, the message) or the
    /// invitations sent (who, by whom, when; « Annuler »).
    void drawPending(const Rect& box, bool applications) {
        UIContext& ui = g_.ui;
        const auto list = clans::pendingRequests(at(page_, "requests"), applications ? "apply" : "invite");
        const std::string id = pageId_, me = g_.online.playerId();
        panel(ui, box, applications && !list.empty() ? ui.theme().warm : ui.theme().border);
        float y = 0.0f;
        if (applications) {
            y = sectionTitle(ui, box, "CANDIDATURES REÇUES · " + std::to_string(list.size()));
        } else if (titleWithButton(box, "INVITATIONS ENVOYÉES · " + std::to_string(list.size()), "inviteM", "Inviter un joueur", y)) {
            inviteName_.clear();
            modal_ = Modal::Invite;
        }
        const Rect view{box.x + ui.s(10), y, box.w - ui.s(20), box.bottom() - ui.s(10) - y};
        if (list.empty()) {
            const char* empty = applications ? "Aucune candidature en attente. Un joueur postule depuis la fiche du clan quand il recrute sur candidature."
                                             : "Aucune invitation en attente. Invitez un joueur par son pseudonyme, ou depuis son profil (onglet « Clan »).";
            ui.labelWrapped(empty, {view.x + ui.s(6), view.y}, view.w - ui.s(12), 14, ui.theme().textDim);
            return;
        }
        const float rowH = ui.s(78);
        ui.beginScroll(applications ? "apps" : "invs", view, static_cast<float>(list.size()) * (rowH + ui.s(6)));
        float ry = view.y - ui.scrollOffset();
        for (const clans::PendingRequest& q : list) {
            const Rect rr{view.x, ry, view.w - ui.s(12), rowH};
            ry += rowH + ui.s(6);
            ui.r().fillRoundedRect(rr, ui.s(8), kRow);
            // The avatar opens the player's profile (mouse), like everywhere.
            ui_.drawAvatar(q.avatar, q.playerId, {rr.x + ui.s(8), rr.y + ui.s(10), ui.s(40), ui.s(40)});
            const float tx = rr.x + ui.s(58), xr = rr.right() - ui.s(10);
            // Line 1: the date at the right (measured first), the name in the room left.
            const std::string when = (applications ? "reçue le " : "le ") + dayText(q.createdAt);
            const float ww = std::min(textW(ui, when, 13) + ui.s(4), (xr - tx) * 0.4f);
            fitLabel(ui, when, {xr, rr.y + ui.s(20)}, 13, ww, ui.theme().textDim, FontFace::Body, Align::Right);
            fitLabel(ui, q.username, {tx, rr.y + ui.s(20)}, 16, xr - ww - ui.s(10) - tx, ui.theme().text, FontFace::Bold, Align::Left, true);
            // Line 2: the buttons at the right (placed first), the message or who invited at their left.
            UIContext::ButtonStyle st;
            st.small = true;
            st.enabled = !busy_;
            float bx = xr;
            auto btn = [&](const std::string& key, const std::string& label, bool primary) {
                UIContext::ButtonStyle s2 = st;
                s2.primary = primary;
                const float bw = textW(ui, label, 14, FontFace::Bold) + ui.s(26);
                bx -= bw;
                const bool clicked = ui.button(key + q.playerId, {bx, rr.y + ui.s(40), bw, ui.s(30)}, label, s2);
                bx -= ui.s(6);
                return clicked;
            };
            const std::string pid = q.playerId, name = q.username;
            std::string line;
            Color lineColor = ui.theme().textDim;
            bool verbatim = false;
            if (applications) {
                if (btn("refuse", "Refuser", false))
                    act("POST", "/api/v1/clans/" + id + "/requests/" + pid, {{"accept", false}}, "Candidature refusée.");
                if (btn("accept", "Accepter", true))
                    act("POST", "/api/v1/clans/" + id + "/requests/" + pid, {{"accept", true}}, name + " a rejoint le clan.");
                line = q.message.empty() ? std::string("Sans message") : "« " + q.message + " »";
                verbatim = !q.message.empty();
                if (verbatim) lineColor = Color{201, 211, 238, 255};
            } else {
                if (btn("cancel", "Annuler", false)) act("DELETE", "/api/v1/clans/" + id + "/invites/" + pid, Json(), "Invitation annulée.");
                line = q.byId == me ? std::string("Invité par vous") : q.byName.empty() ? std::string("Invitation envoyée") : "Invité par " + q.byName;
            }
            fitLabel(ui, line, {tx, rr.y + ui.s(55)}, 13, bx - ui.s(4) - tx, lineColor, FontFace::Body, Align::Left, verbatim);
        }
        ui.endScroll();
    }

    /// The members and their roles: « Gérer » opens what the player's role allows on a member
    /// (name an officer, hand over the chief's role, exclude), « Profil » their profile.
    void drawManageMembers(const Rect& r, const Json& c, const std::string& role) {
        UIContext& ui = g_.ui;
        panel(ui, r, ui.theme().border);
        const Json& members = at(page_, "members");
        float y = sectionTitle(ui, r, "MEMBRES ET RÔLES", std::to_string(members.size()) + " / " + std::to_string(jget<int>(c, "max_members", clans::kMaxMembers)),
                               ui.theme().textDim);
        static const char* kNote = "Le chef nomme et retire les officiers, et peut transmettre son rôle ; le chef et les officiers excluent les membres.";
        const float nw = r.w - ui.s(32);
        const float nh = ui.text().measureWrapped(kNote, FontFace::Body, ui.s(13), nw).y;
        const Rect view{r.x + ui.s(10), y, r.w - ui.s(20), r.bottom() - ui.s(12) - y - nh - ui.s(10)};
        ui.labelWrapped(kNote, {r.x + ui.s(16), view.bottom() + ui.s(6)}, nw, 13, ui.theme().textDim);
        const std::string me = g_.online.playerId();
        const float rowH = ui.s(56);
        ui.beginScroll("roles", view, static_cast<float>(members.size()) * (rowH + ui.s(6)));
        float ry = view.y - ui.scrollOffset();
        for (const auto& m : members) {
            const Json& p = at(m, "player");
            const std::string pid = jstr(p, "id"), mrole = jstr(m, "role");
            const Rect rr{view.x, ry, view.w - ui.s(12), rowH};
            ry += rowH + ui.s(6);
            ui.r().fillRoundedRect(rr, ui.s(8), kRow);
            // The button first (right), then the texts in the room it leaves.
            const bool self = pid == me;
            const bool manageable = !self && ((role == "leader" && mrole != "leader") || clans::canExclude(role, mrole));
            float xr = rr.right() - ui.s(8);
            if (!self) {
                const std::string label = manageable ? "Gérer" : "Profil";
                UIContext::ButtonStyle st;
                st.small = true;
                st.primary = manageable;
                st.enabled = !busy_;
                const float bw = textW(ui, label, 14, FontFace::Bold) + ui.s(30);
                if (ui.button("role" + pid, {xr - bw, rr.y + (rowH - ui.s(32)) * 0.5f, bw, ui.s(32)}, label, st)) {
                    if (manageable) {
                        memberSel_ = m;
                        modal_ = Modal::Member;
                    } else {
                        g_.app.pushScene(makePlayerProfile(g_, pid));
                    }
                }
                xr -= bw + ui.s(10);
            }
            const float tx = rr.x + ui.s(12), room = xr - tx;
            fitLabel(ui, jstr(p, "username") + (self ? " (vous)" : ""), {tx, rr.y + ui.s(19)}, 15, room, self ? ui.theme().accent : ui.theme().text,
                     FontFace::Bold, Align::Left, !self);
            const Vec2 rs = fitLabel(ui, clans::roleLabel(mrole), {tx, rr.y + ui.s(40)}, 13, room * 0.4f, clanRoleColor(ui, mrole), FontFace::Bold);
            if (const std::string since = dayText(jstr(m, "joined_at")); !since.empty())
                fitLabel(ui, " · membre depuis le " + since, {tx + rs.x, rr.y + ui.s(40)}, 13, room - rs.x, ui.theme().textDim);
        }
        ui.endScroll();
    }

    /// The clan's settings: what they are, and (the chief) change them, hand over the role, dissolve.
    void drawSettings(const Rect& r, const Json& c, const std::string& role) {
        UIContext& ui = g_.ui;
        panel(ui, r, ui.theme().border);
        float y = sectionTitle(ui, r, "PARAMÈTRES DU CLAN");
        const float x = r.x + ui.s(16), w = r.w - ui.s(32);
        const float em = ui.s(64);
        drawClanEmblem(ui, {x, y, em, em}, clans::emblemFromJson(at(c, "emblem")));
        fitLabel(ui, clans::bracket(jstr(c, "tag")), {x + em + ui.s(14), y + ui.s(18)}, 18, w - em - ui.s(14), kGold, FontFace::Bold, Align::Left, true);
        fitLabel(ui, jstr(c, "name"), {x + em + ui.s(14), y + ui.s(44)}, 15, w - em - ui.s(14), ui.theme().text, FontFace::Bold, Align::Left, true);
        y += em + ui.s(18);
        // The buttons from the bottom up (the chief), the description in the room above them.
        struct Btn {
            std::string key, label;
            bool primary = false, danger = false;
        };
        std::vector<Btn> btns;
        if (role == "leader") {
            btns.push_back({"dissolve", "Dissoudre le clan…", false, true});
            btns.push_back({"transfer", "Transmettre le rôle de chef…"});
            btns.push_back({"edit", "Modifier le clan…", true});
        }
        const float bh = ui.s(46), gap = ui.s(10);
        float by = r.bottom() - ui.s(16) - bh;
        std::string chosen;
        for (const Btn& b : btns) {
            UIContext::ButtonStyle st;
            st.primary = b.primary;
            st.danger = b.danger;
            st.enabled = !busy_;
            if (ui.button("set/" + b.key, {x, by, w, bh}, b.label, st)) chosen = b.key;
            by -= bh + gap;
        }
        const float bottom = btns.empty() ? r.bottom() - ui.s(16) : by + bh;  // the top of the highest button
        auto line = [&](const std::string& caption, const std::string& value, bool verbatim) {
            if (y + ui.s(44) > bottom) return;
            fitLabel(ui, caption, {x, y + ui.s(8)}, 13, w, ui.theme().textDim, FontFace::Bold);
            y += ui.s(18);
            i18n::Verbatim words(verbatim);
            const float vh = std::min(ui.text().measureWrapped(value, FontFace::Body, ui.s(15), w).y, bottom - y - ui.s(4));
            if (vh < ui.s(14)) return;
            if (ui.text().measureWrapped(value, FontFace::Body, ui.s(15), w).y <= vh + 0.5f) ui.labelWrapped(value, {x, y}, w, 15, ui.theme().text);
            else fitLabel(ui, value, {x, y + ui.s(10)}, 15, w, ui.theme().text, FontFace::Body, Align::Left, verbatim);
            y += vh + ui.s(12);
        };
        const std::string motto = jstr(c, "motto");
        line("DEVISE", motto.empty() ? std::string("Aucune devise.") : "« " + motto + " »", !motto.empty());
        line("ADHÉSION", clans::policyLabel(jstr(c, "join_policy")), false);
        line("FONDÉ LE", dayText(jstr(c, "created_at")), false);
        if (role != "leader")
            line("RÉSERVÉ AU CHEF", "Seul le chef modifie le nom, le tag, la devise, l'emblème et l'adhésion, transmet son rôle ou dissout le clan.", false);
        if (chosen == "edit") openForm(false);
        else if (chosen == "transfer") modal_ = Modal::Transfer;
        else if (chosen == "dissolve") askDissolve();
    }

    /// The clan's discussion: the newest messages (sender, text), the field and « Envoyer ».
    void drawChat(const Rect& r) {
        UIContext& ui = g_.ui;
        panel(ui, r, ui.theme().border);
        float y = sectionTitle(ui, r, "DISCUSSION DU CLAN");
        const float ih = ui.s(42);
        Rect input{r.x + ui.s(12), r.bottom() - ui.s(12) - ih, r.w - ui.s(24), ih};
        Rect view{r.x + ui.s(10), y, r.w - ui.s(20), input.y - ui.s(10) - y};
        if (convId_.empty() || !chatLoaded_) {
            fitLabel(ui, convId_.empty() ? "Discussion indisponible." : "Chargement…", {view.x + ui.s(6), view.y + ui.s(12)}, 14, view.w, ui.theme().textDim);
        } else if (messages_.empty()) {
            ui.labelWrapped("Aucun message. Écrivez le premier !", {view.x + ui.s(6), view.y}, view.w - ui.s(12), 14, ui.theme().textDim);
        } else {
            // Heights first (each message wrapped in the column), then drawing.
            const float tw = view.w - ui.s(22), fs = 14;
            std::vector<float> hs;
            float content = ui.s(4);
            for (const auto& m : messages_) {
                const bool deleted = jget<bool>(m, "deleted", false);
                const std::string body = deleted ? std::string("Message supprimé") : jstr(m, "body");
                float h;
                {
                    i18n::Verbatim words(!deleted);
                    h = ui.text().measureWrapped(body, FontFace::Body, ui.s(fs), tw).y;
                }
                hs.push_back(ui.s(20) + h + ui.s(8));
                content += hs.back();
            }
            if (stickBottom_) {
                ui.setScroll("chat", 1e9f);
                stickBottom_ = false;
            }
            ui.beginScroll("chat", view, content);
            float my = view.y + ui.s(4) - ui.scrollOffset();
            for (size_t i = 0; i < messages_.size(); ++i) {
                const Json& m = messages_[i];
                const float h = hs[i];
                if (my + h >= view.y && my <= view.bottom()) {
                    const Json& s = at(m, "sender");
                    const bool mine = jget<bool>(m, "mine", false);
                    const std::string when = clockText(jstr(m, "created_at"));
                    const float ww = textW(ui, when, 11) + ui.s(4);
                    ui.label(when, {view.x + ui.s(6) + tw, my + ui.s(9)}, 11, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
                    fitLabel(ui, jstr(s, "username"), {view.x + ui.s(6), my + ui.s(9)}, 14, tw - ww - ui.s(8), mine ? ui.theme().accent : kGold,
                             FontFace::Bold, Align::Left, true);
                    const bool deleted = jget<bool>(m, "deleted", false);
                    i18n::Verbatim words(!deleted);
                    ui.labelWrapped(deleted ? "Message supprimé" : jstr(m, "body"), {view.x + ui.s(6), my + ui.s(20)}, tw, fs,
                                    deleted ? ui.theme().textDim : Color{201, 211, 238, 255});
                }
                my += h;
            }
            ui.endScroll();
        }
        // The field and « Envoyer » (Enter sends and keeps the field open).
        const float sendW = std::max(ui.s(100), textW(ui, "Envoyer", 15, FontFace::Bold) + ui.s(30));
        const bool enter = ui.editingText() && ui.focused("draft") && g_.app.input().keyPressed(SDL_SCANCODE_RETURN);
        ui.textField("draft", {input.x, input.y, input.w - sendW - ui.s(8), input.h}, draft_, "Écrire au clan…", kMaxChat);
        UIContext::ButtonStyle p;
        p.primary = true;
        p.small = true;
        p.enabled = !sending_ && !convId_.empty() && draft_.find_first_not_of(' ') != std::string::npos;
        const bool clicked = ui.button("send", {input.right() - sendW, input.y, sendW, input.h}, sending_ ? "Envoi…" : "Envoyer", p);
        if ((clicked || enter) && p.enabled) {
            sendChat();
            if (enter) ui.editText("draft");
        }
    }

    // --------------------------------------------------------------- modals
    void openForm(bool create) {
        if (!g_.online.loggedIn()) return askAccount();
        formCreate_ = create;
        formError_.clear();
        const Json& c = at(page_, "clan");
        if (!create && c.is_object()) {
            fName_ = jstr(c, "name");
            fTag_ = jstr(c, "tag");
            fMotto_ = jstr(c, "motto");
            const clans::Emblem e = clans::emblemFromJson(at(c, "emblem"));
            const auto& shapes = clans::emblemShapes();
            fShape_ = static_cast<int>(std::find(shapes.begin(), shapes.end(), e.shape) - shapes.begin());
            fPrimary_ = clans::nearestPaletteIndex(e.primary);
            fSecondary_ = clans::nearestPaletteIndex(e.secondary);
            const auto& pol = clans::policyIds();
            fPolicy_ = static_cast<int>(std::find(pol.begin(), pol.end(), jstr(c, "join_policy")) - pol.begin());
        } else {
            fName_.clear();
            fTag_.clear();
            fMotto_.clear();
            fShape_ = 0;
            fPrimary_ = 0;
            fSecondary_ = 4;
            fPolicy_ = 1;
        }
        fShape_ = std::clamp(fShape_, 0, static_cast<int>(clans::emblemShapes().size()) - 1);
        fPolicy_ = std::clamp(fPolicy_, 0, 2);
        modal_ = Modal::Form;
        g_.ui.editText("form/name");  // called inside the screen's "clans" id
    }

    clans::Emblem formEmblem() const {
        clans::Emblem e;
        e.shape = clans::emblemShapes()[static_cast<size_t>(fShape_)];
        e.primary = clans::palette()[static_cast<size_t>(fPrimary_)].color;
        e.secondary = clans::palette()[static_cast<size_t>(fSecondary_)].color;
        return e;
    }

    /// A centred box `w` × `h` on the dimmed screen.
    Rect modalBox(float w, float h) {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 175});
        w = std::min(w, ui.screenW() - ui.s(40));
        h = std::min(h, ui.screenH() - ui.s(40));
        Rect box{(ui.screenW() - w) * 0.5f, (ui.screenH() - h) * 0.5f, w, h};
        ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
        ui.r().drawRoundedRect(box, ui.s(18), ui.theme().accent.withAlphaF(0.55f), ui.s(1.5f));
        return box;
    }

    void drawModal() {
        UIContext& ui = g_.ui;
        ui.pushId("clans");
        if (modal_ == Modal::Form) drawForm();
        else if (modal_ == Modal::Invite) drawInvite();
        else if (modal_ == Modal::Apply) drawApply();
        else if (modal_ == Modal::Member) drawMember();
        else if (modal_ == Modal::Confirm) drawConfirm();
        else if (modal_ == Modal::Transfer) drawTransfer();
        ui.popId();
    }

    /// « Gestion › Transmettre le rôle de chef… » (the chief): the member who becomes chief, then a confirmation.
    void drawTransfer() {
        UIContext& ui = g_.ui;
        ui.pushId("transfer");
        const std::string me = g_.online.playerId(), id = pageId_;
        std::vector<const Json*> list;
        for (const auto& m : at(page_, "members"))
            if (jstr(at(m, "player"), "id") != me && jstr(m, "role") != "leader") list.push_back(&m);
        const float w = ui.s(620), tw = w - ui.s(60), rowH = ui.s(46);
        const float listH = list.empty() ? ui.s(40) : std::min(static_cast<float>(list.size()) * (rowH + ui.s(8)), ui.s(400));
        Rect box = modalBox(w, ui.s(104) + listH + ui.s(24) + ui.s(48) + ui.s(28));
        const float x = box.x + ui.s(30);
        fitLabel(ui, "Transmettre le rôle de chef", {x, box.y + ui.s(42)}, 26, tw, ui.theme().accent, FontFace::Title);
        fitLabel(ui, "Choisissez le nouveau chef : vous deviendrez officier.", {x, box.y + ui.s(78)}, 15, tw, ui.theme().textDim);
        const Rect view{x, box.y + ui.s(104), tw, std::min(listH, box.bottom() - ui.s(100) - box.y - ui.s(104))};
        std::string chosenId, chosenName;
        if (list.empty()) {
            fitLabel(ui, "Aucun autre membre : invitez d'abord un joueur.", {view.x, view.y + ui.s(16)}, 16, view.w, ui.theme().textDim);
        } else {
            ui.beginScroll("list", view, static_cast<float>(list.size()) * (rowH + ui.s(8)));
            float ry = view.y - ui.scrollOffset();
            for (const Json* m : list) {
                const Json& p = at(*m, "player");
                const std::string pid = jstr(p, "id"), name = jstr(p, "username"), role = jstr(*m, "role");
                const Rect rr{view.x, ry, view.w - ui.s(12), rowH};
                ry += rowH + ui.s(8);
                UIContext::ButtonStyle st;
                st.align = Align::Left;
                st.enabled = !busy_;
                if (ui.button("m" + pid, rr, "", st)) {
                    chosenId = pid;
                    chosenName = name;
                }
                const float rw = std::min(textW(ui, clans::roleLabel(role), 14, FontFace::Bold) + ui.s(4), rr.w * 0.35f);
                fitLabel(ui, clans::roleLabel(role), {rr.right() - ui.s(14), rr.center().y}, 14, rw, clanRoleColor(ui, role), FontFace::Bold, Align::Right);
                fitLabel(ui, name, {rr.x + ui.s(14), rr.center().y}, 16, rr.w - ui.s(28) - rw - ui.s(12), ui.theme().text, FontFace::Bold, Align::Left, true);
            }
            ui.endScroll();
        }
        const float cW = textW(ui, "Annuler", 17, FontFace::Bold) + ui.s(44);
        const bool cancel = ui.button("cancel", {box.right() - ui.s(30) - cW, box.bottom() - ui.s(76), cW, ui.s(48)}, "Annuler") ||
                            (ui.backPressed() && !ui.editingText());
        ui.popId();
        if (!chosenId.empty()) {
            modal_ = Modal::None;
            confirm("Transmettre le rôle de chef à " + chosenName + " ? Vous deviendrez officier ; seul le nouveau chef pourra vous le rendre.", "Transmettre",
                    [this, id, chosenId, chosenName] {
                        act("POST", "/api/v1/clans/" + id + "/transfer", {{"player_id", chosenId}}, chosenName + " est maintenant le chef du clan.");
                    },
                    false);
        } else if (cancel) {
            modal_ = Modal::None;
        }
    }

    void drawForm() {
        UIContext& ui = g_.ui;
        ui.pushId("form");
        const float rowH = ui.s(80), w = ui.s(900);
        const float h = ui.s(84) + 7 * rowH + ui.s(36) + ui.s(76);
        Rect box = modalBox(w, h);
        const float x = box.x + ui.s(30);
        fitLabel(ui, formCreate_ ? "Créer un clan" : "Paramètres du clan", {x, box.y + ui.s(42)}, 28, box.w - ui.s(60), ui.theme().accent, FontFace::Title);
        // Fields on the left, the emblem's preview on the right.
        const float previewW = ui.s(220);
        const float fw = box.w - ui.s(60) - previewW - ui.s(30);
        float y = box.y + ui.s(84);
        auto caption = [&](const std::string& t) { fitLabel(ui, t, {x, y + ui.s(10)}, 15, fw, ui.theme().textDim); };
        caption("Nom du clan (3 à 32 caractères)");
        ui.textField("name", {x, y + ui.s(26), fw, ui.s(44)}, fName_, "Les Gardiens de l'Aube", 96);
        y += rowH;
        caption("Tag (2 à 5 lettres ou chiffres, unique)");
        ui.textField("tag", {x, y + ui.s(26), std::min(fw, ui.s(260)), ui.s(44)}, fTag_, "AUBE", 5);
        y += rowH;
        caption("Devise (facultative, 80 caractères au plus)");
        ui.textField("motto", {x, y + ui.s(26), fw, ui.s(44)}, fMotto_, "Une phase à la fois.", 240);
        y += rowH;
        std::vector<std::string> shapes;
        for (const auto& s : clans::emblemShapes()) shapes.push_back(clans::shapeLabel(s));
        std::vector<std::string> colors;
        for (const auto& c : clans::palette()) colors.push_back(c.name);
        caption("Forme de l'emblème");
        ui.cycle("shape", {x, y + ui.s(26), fw, ui.s(44)}, "", fShape_, shapes);
        y += rowH;
        caption("Couleur de la forme");
        ui.r().fillRoundedRect({x, y + ui.s(32), ui.s(32), ui.s(32)}, ui.s(6), clans::palette()[static_cast<size_t>(fPrimary_)].color);
        ui.cycle("primary", {x + ui.s(44), y + ui.s(26), fw - ui.s(44), ui.s(44)}, "", fPrimary_, colors);
        y += rowH;
        caption("Couleur du cadre et du fond");
        ui.r().fillRoundedRect({x, y + ui.s(32), ui.s(32), ui.s(32)}, ui.s(6), clans::palette()[static_cast<size_t>(fSecondary_)].color);
        ui.cycle("secondary", {x + ui.s(44), y + ui.s(26), fw - ui.s(44), ui.s(44)}, "", fSecondary_, colors);
        y += rowH;
        std::vector<std::string> policies;
        for (const auto& p : clans::policyIds()) policies.push_back(clans::policyLabel(p));
        caption("Adhésion");
        ui.cycle("policy", {x, y + ui.s(26), fw, ui.s(44)}, "", fPolicy_, policies);
        y += rowH;
        // Preview: the emblem, then « [TAG] Nom » wrapped under it.
        const float px = box.right() - ui.s(30) - previewW;
        drawClanEmblem(ui, {px + (previewW - ui.s(160)) * 0.5f, box.y + ui.s(96), ui.s(160), ui.s(160)}, formEmblem());
        {
            i18n::Verbatim words;
            const std::string shown = clans::tagged(clans::cleanName(fName_), fTag_);
            ui.labelWrapped(shown.empty() ? std::string("—") : shown, {px, box.y + ui.s(276)}, previewW, 17, ui.theme().text);
        }
        // The first problem of the form (checked like the server does), or the server's answer.
        const std::string name = clans::cleanName(fName_);
        std::string problem = clans::nameError(name);
        if (problem.empty()) problem = clans::tagError(fTag_);
        if (problem.empty() && clans::textLength(clans::cleanName(fMotto_)) > clans::kMottoMax) problem = "Devise : 80 caractères au plus.";
        const std::string shownProblem = !formError_.empty() ? formError_ : (fName_.empty() && fTag_.empty()) ? std::string() : problem;
        fitLabel(ui, shownProblem, {x, y + ui.s(16)}, 15, box.w - ui.s(60), ui.theme().danger);
        // Buttons.
        const float by = box.bottom() - ui.s(66);
        UIContext::ButtonStyle p;
        p.primary = true;
        p.enabled = problem.empty() && !busy_;
        const std::string okLabel = formCreate_ ? "Créer le clan" : "Enregistrer";
        const float okW = textW(ui, okLabel, 17, FontFace::Bold) + ui.s(44), cancelW = textW(ui, "Annuler", 17, FontFace::Bold) + ui.s(44);
        const bool ok = ui.button("ok", {box.right() - ui.s(30) - okW, by, okW, ui.s(48)}, okLabel, p);
        const bool cancel = ui.button("cancel", {box.right() - ui.s(30) - okW - ui.s(12) - cancelW, by, cancelW, ui.s(48)}, "Annuler");
        if (!formCreate_) {
            UIContext::ButtonStyle d;
            d.danger = true;
            const float dw = std::min(textW(ui, "Dissoudre le clan…", 17, FontFace::Bold) + ui.s(44), box.right() - ui.s(30) - okW - cancelW - ui.s(24) - x);
            if (ui.button("dissolve", {x, by, dw, ui.s(48)}, "Dissoudre le clan…", d)) askDissolve();
        }
        if (ok && p.enabled) submitForm(name);
        if (cancel || (ui.backPressed() && !ui.editingText())) modal_ = Modal::None;
        ui.popId();
    }

    void submitForm(const std::string& name) {
        Json body = {{"name", name},
                     {"tag", fTag_},
                     {"motto", clans::cleanName(fMotto_)},
                     {"emblem", clans::emblemToJson(formEmblem())},
                     {"join_policy", clans::policyIds()[static_cast<size_t>(fPolicy_)]}};
        busy_ = true;
        formError_.clear();
        const bool create = formCreate_;
        const std::string path = create ? std::string("/api/v1/clans") : "/api/v1/clans/" + pageId_;
        g_.online.send(create ? "POST" : "PATCH", path, body, OnlineService::guard(life_, [this, create](const ApiResult& r) {
                           busy_ = false;
                           if (!r.ok) {
                               formError_ = r.message;
                               return;
                           }
                           modal_ = Modal::None;
                           say(create ? "Votre clan est créé : vous en êtes le chef." : "Clan modifié.", false);
                           const std::string id = jstr(at(r.data, "clan"), "id");
                           if (create && !id.empty()) {
                               tab_ = kTabMine;
                               pageFromList_ = false;
                               openPage(id, false);
                           }
                           loadMine();
                           loadPage();
                           g_.online.refreshMyClan();
                       }));
    }

    void askDissolve() {
        const Json& c = at(page_, "clan");
        const std::string id = jstr(c, "id");
        confirm("Dissoudre le clan " + clans::tagged(jstr(c, "name"), jstr(c, "tag")) +
                    " ? Ses membres le quittent, sa discussion et ses points disparaissent. Cette action est définitive.",
                "Dissoudre", [this, id] {
                    act("DELETE", "/api/v1/clans/" + id, {{"confirm", true}}, "Le clan est dissous.", [this](const ApiResult&) { forgetMyClan(); });
                });
    }

    void drawInvite() {
        UIContext& ui = g_.ui;
        ui.pushId("invite");
        static const char* kText = "Le joueur reçoit une invitation, qu'il accepte depuis l'écran CLANS. Un joueur qui a déjà postulé rejoint le clan aussitôt.";
        const float w = ui.s(640), tw = w - ui.s(60);
        const float th = ui.text().measureWrapped(kText, FontFace::Body, ui.s(15), tw).y;
        Rect box = modalBox(w, ui.s(84) + th + ui.s(16) + ui.s(46) + ui.s(26) + ui.s(76));
        const float x = box.x + ui.s(30);
        fitLabel(ui, "Inviter un joueur", {x, box.y + ui.s(42)}, 26, tw, ui.theme().accent, FontFace::Title);
        ui.labelWrapped(kText, {x, box.y + ui.s(76)}, tw, 15, ui.theme().textDim);
        const float fy = box.y + ui.s(84) + th + ui.s(16);
        ui.textField("name", {x, fy, tw, ui.s(46)}, inviteName_, "Pseudonyme du joueur", 20);
        const float by = box.bottom() - ui.s(66);
        UIContext::ButtonStyle p;
        p.primary = true;
        p.enabled = inviteName_.size() >= 3 && !busy_;
        const float okW = textW(ui, "Inviter", 17, FontFace::Bold) + ui.s(44), cW = textW(ui, "Annuler", 17, FontFace::Bold) + ui.s(44);
        if (ui.button("ok", {box.right() - ui.s(30) - okW, by, okW, ui.s(48)}, "Inviter", p) && p.enabled) {
            const std::string name = inviteName_;
            act("POST", "/api/v1/clans/" + pageId_ + "/invites", {{"username", name}}, "Invitation envoyée à " + name + ".");
            modal_ = Modal::None;
        }
        if (ui.button("cancel", {box.right() - ui.s(30) - okW - ui.s(12) - cW, by, cW, ui.s(48)}, "Annuler") || (ui.backPressed() && !ui.editingText()))
            modal_ = Modal::None;
        ui.popId();
    }

    void drawApply() {
        UIContext& ui = g_.ui;
        ui.pushId("apply");
        const Json& c = at(page_, "clan");
        const float w = ui.s(680), tw = w - ui.s(60);
        Rect box = modalBox(w, ui.s(84) + ui.s(30) + ui.s(46) + ui.s(30) + ui.s(76));
        const float x = box.x + ui.s(30);
        fitLabel(ui, "Postuler à " + clans::tagged(jstr(c, "name"), jstr(c, "tag")), {x, box.y + ui.s(42)}, 26, tw, ui.theme().accent, FontFace::Title,
                 Align::Left, true);
        fitLabel(ui, "Un court message pour le chef et les officiers (facultatif, 120 caractères au plus) :", {x, box.y + ui.s(96)}, 15, tw,
                 ui.theme().textDim);
        ui.textField("msg", {x, box.y + ui.s(114), tw, ui.s(46)}, applyMessage_, "dispo le soir", 240);
        const bool tooLong = clans::textLength(clans::cleanName(applyMessage_)) > clans::kMessageMax;
        const float by = box.bottom() - ui.s(66);
        UIContext::ButtonStyle p;
        p.primary = true;
        p.enabled = !busy_ && !tooLong;
        const std::string okLabel = "Envoyer la candidature";
        const float okW = textW(ui, okLabel, 17, FontFace::Bold) + ui.s(44), cW = textW(ui, "Annuler", 17, FontFace::Bold) + ui.s(44);
        if (ui.button("ok", {box.right() - ui.s(30) - okW, by, okW, ui.s(48)}, okLabel, p) && p.enabled) {
            act("POST", "/api/v1/clans/" + pageId_ + "/join", {{"message", clans::cleanName(applyMessage_)}}, "Candidature envoyée.");
            modal_ = Modal::None;
        }
        if (ui.button("cancel", {box.right() - ui.s(30) - okW - ui.s(12) - cW, by, cW, ui.s(48)}, "Annuler") || (ui.backPressed() && !ui.editingText()))
            modal_ = Modal::None;
        ui.popId();
    }

    /// A member chosen in the list: their profile, and what the player's role allows on them.
    void drawMember() {
        UIContext& ui = g_.ui;
        ui.pushId("member");
        const Json& p = at(memberSel_, "player");
        const std::string pid = jstr(p, "id"), name = jstr(p, "username"), role = jstr(memberSel_, "role");
        const std::string mine = myRole(), me = g_.online.playerId(), id = pageId_;
        struct Choice {
            std::string key, label;
            bool danger = false;
        };
        std::vector<Choice> choices{{"profile", "Voir le profil"}};
        if (mine == "leader" && pid != me) {
            if (role == "officer") choices.push_back({"demote", "Retirer le rôle d'officier"});
            if (role == "member") choices.push_back({"promote", "Nommer officier"});
            choices.push_back({"transfer", "Transmettre le rôle de chef…"});
        }
        if (pid != me && clans::canExclude(mine, role)) choices.push_back({"kick", "Exclure du clan…", true});
        const float w = ui.s(560), bh = ui.s(48);
        Rect box = modalBox(w, ui.s(100) + static_cast<float>(choices.size() + 1) * (bh + ui.s(10)) + ui.s(20));
        const float x = box.x + ui.s(30), bw = box.w - ui.s(60);
        const std::string tag = jstr(at(page_, "clan"), "tag");
        fitLabel(ui, clans::tagged(name, tag), {x, box.y + ui.s(40)}, 26, bw, ui.theme().text, FontFace::Title, Align::Left, true);
        fitLabel(ui, clans::roleLabel(role), {x, box.y + ui.s(74)}, 16, bw, clanRoleColor(ui, role), FontFace::Bold);
        float y = box.y + ui.s(100);
        std::string chosen;
        for (const Choice& c : choices) {
            UIContext::ButtonStyle st;
            st.danger = c.danger;
            st.enabled = !busy_;
            if (ui.button(c.key, {x, y, bw, bh}, c.label, st)) chosen = c.key;
            y += bh + ui.s(10);
        }
        const bool close = ui.button("close", {x, y, bw, bh}, "Fermer") || (ui.backPressed() && !ui.editingText());
        ui.popId();
        if (close) modal_ = Modal::None;
        if (chosen.empty()) return;
        modal_ = Modal::None;
        if (chosen == "profile") g_.app.pushScene(makePlayerProfile(g_, pid));
        else if (chosen == "promote")
            act("PATCH", "/api/v1/clans/" + id + "/members/" + pid, {{"role", "officer"}}, name + " est maintenant officier.");
        else if (chosen == "demote")
            act("PATCH", "/api/v1/clans/" + id + "/members/" + pid, {{"role", "member"}}, name + " n'est plus officier.");
        else if (chosen == "transfer")
            confirm("Transmettre le rôle de chef à " + name + " ? Vous deviendrez officier ; seul le nouveau chef pourra vous le rendre.", "Transmettre",
                    [this, id, pid, name] { act("POST", "/api/v1/clans/" + id + "/transfer", {{"player_id", pid}}, name + " est maintenant le chef du clan."); },
                    false);
        else if (chosen == "kick")
            confirm("Exclure " + name + " du clan ? Il quitte aussi sa discussion.", "Exclure",
                    [this, id, pid, name] { act("DELETE", "/api/v1/clans/" + id + "/members/" + pid, Json(), name + " a été exclu du clan."); });
    }

    void drawConfirm() {
        UIContext& ui = g_.ui;
        ui.pushId("confirm");
        const float w = ui.s(640), tw = w - ui.s(60);
        const float th = ui.text().measureWrapped(confirmText_, FontFace::Body, ui.s(18), tw).y;
        Rect box = modalBox(w, ui.s(40) + th + ui.s(30) + ui.s(48) + ui.s(30));
        ui.labelWrapped(confirmText_, {box.x + ui.s(30), box.y + ui.s(36)}, tw, 18, ui.theme().text);
        const float by = box.bottom() - ui.s(78);
        UIContext::ButtonStyle d;
        d.danger = confirmDanger_;
        d.primary = !confirmDanger_;
        const float okW = textW(ui, confirmAction_, 17, FontFace::Bold) + ui.s(44), cW = textW(ui, "Annuler", 17, FontFace::Bold) + ui.s(44);
        const bool ok = ui.button("ok", {box.right() - ui.s(30) - okW, by, okW, ui.s(48)}, confirmAction_, d);
        const bool cancel = ui.button("cancel", {box.right() - ui.s(30) - okW - ui.s(12) - cW, by, cW, ui.s(48)}, "Annuler") || ui.backPressed();
        ui.popId();
        if (ok) {
            modal_ = Modal::None;
            if (confirmFn_) confirmFn_();
        } else if (cancel) {
            modal_ = Modal::None;
        }
    }

    // ----------------------------------------------------------------- state
    GameServices& g_;
    OnlineUi ui_;
    MenuBackground bg_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string wanted_;
    std::string wantedTag_;   // makeClanByTag: the tag clicked
    std::string entryId_;     // the clan's sheet the screen was opened on (pushed): « Retour » goes back
    TagState tagState_ = TagState::None;
    std::string tagError_;
    int myApplications_ = 0;  // applications waiting in the player's clan (badge of « Gestion »)
    bool pushed_ = false;
    bool wasLoggedIn_ = false;
    Rect header_, zoneA_, zoneB_;
    int tab_ = kTabMine;

    Json mine_;
    bool mineLoaded_ = false, mineCache_ = false;
    int mineGen_ = 0;

    std::string pageId_;
    bool pageFromList_ = false;
    Json page_;
    bool pageLoaded_ = false, pageCache_ = false;
    std::string pageError_;
    int pageGen_ = 0;
    float pageTimer_ = 0.0f;

    std::string convId_, newestSeen_, draft_;
    Json messages_ = Json::array();
    bool chatLoaded_ = false, chatBusy_ = false, sending_ = false, stickBottom_ = false;
    float chatTimer_ = 0.0f;

    std::string query_;
    Json search_ = Json::array();
    bool searchLoaded_ = false, searchMore_ = false, searchBusy_ = false, searchCache_ = false, searchDirty_ = false;
    float searchT_ = 0.0f;
    int searchGen_ = 0;

    Json ranking_ = Json::array(), rankingMe_;
    bool rankingLoaded_ = false, rankingMore_ = false, rankingBusy_ = false, rankingCache_ = false;
    int rankingTotal_ = 0, rankingGen_ = 0;

    Modal modal_ = Modal::None;
    bool busy_ = false;
    bool formCreate_ = true;
    std::string fName_, fTag_, fMotto_, formError_;
    int fShape_ = 0, fPrimary_ = 0, fSecondary_ = 4, fPolicy_ = 1;
    std::string inviteName_, applyMessage_;
    Json memberSel_;
    std::string confirmText_, confirmAction_;
    std::function<void()> confirmFn_;
    bool confirmDanger_ = true;

    std::string status_;
    bool statusError_ = false;
    float statusT_ = -100.0f;
};

}  // namespace

std::unique_ptr<Scene> makeClans(GameServices& g, const std::string& clanId, bool pushed) {
    return std::make_unique<ClanScene>(g, clanId, pushed);
}

std::unique_ptr<Scene> makeClanByTag(GameServices& g, const std::string& tag) { return std::make_unique<ClanScene>(g, std::string(), true, tag); }

}  // namespace cosmic
