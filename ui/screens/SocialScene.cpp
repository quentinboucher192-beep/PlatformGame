// SOCIAL (ONLINE section): private conversations between friends, group discussions, the
// friend list (requests, removal) and blocked players. Everything comes from the server
// (Cosmic Online); nothing is simulated: offline, the last lists read are shown from the
// cache and sending is refused with the server's / network's message.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <functional>
#include <map>

#include "engine/input/InputManager.h"
#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "ui/online/GlobeRenderer.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/OnlineNav.h"

namespace cosmic {

namespace {

constexpr float kMessagesPoll = 4.0f;       // open conversation
constexpr float kConversationsPoll = 15.0f;  // conversation list
constexpr float kFriendsPoll = 15.0f;        // friends list on screen (presence, runs being played)
constexpr float kGlobePoll = 120.0f;         // globe of the players (shown without a conversation)
constexpr size_t kMaxMessage = 500;

/// "2026-11-13T21:04:05.123Z" -> seconds since the epoch (UTC), -1 if malformed.
int64_t isoSeconds(const std::string& s) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) != 6) return -1;
    // Days from civil (proleptic Gregorian calendar).
    y -= mo <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = era * 146097 + doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + se;
}

std::tm localTm(int64_t t) {
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    return tmv;
}

/// "21:04" today, "13/11 21:04" otherwise (local time of this computer).
std::string clockText(const std::string& iso) {
    int64_t t = isoSeconds(iso);
    if (t < 0) return {};
    std::tm m = localTm(t), now = localTm(static_cast<int64_t>(std::time(nullptr)));
    char buf[32];
    if (m.tm_year == now.tm_year && m.tm_yday == now.tm_yday) std::snprintf(buf, sizeof buf, "%02d:%02d", m.tm_hour, m.tm_min);
    else std::snprintf(buf, sizeof buf, "%02d/%02d %02d:%02d", m.tm_mday, m.tm_mon + 1, m.tm_hour, m.tm_min);
    return buf;
}

std::string oneLine(std::string s, size_t maxBytes) {
    for (char& c : s)
        if (c == '\n' || c == '\r') c = ' ';
    if (s.size() > maxBytes) {
        size_t cut = maxBytes;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        s = s.substr(0, cut) + "…";
    }
    return s;
}

class SocialScene : public Scene {
public:
    SocialScene(GameServices& g, std::string conversationId, int tab)
        : g_(g), ui_(g), wantedConversation_(std::move(conversationId)), tab_(std::clamp(tab, 0, 2)) {}

    void onEnter() override {
        g_.playMusic("music/menu_theme.ogg");
        bg_.init(g_);
        g_.ui.setFocus("online/nav/social");
        // Screenshots / support: --online-open friends|blocked|newgroup|members.
        if (g_.onlineOpen == "friends") tab_ = 1;
        if (g_.onlineOpen == "blocked") tab_ = 2;
        if (g_.onlineOpen == "newgroup" || g_.onlineOpen == "members") openModal_ = g_.onlineOpen;
        if (g_.onlineOpen.rfind("watch:", 0) == 0) {
            watchName_ = g_.onlineOpen.substr(6);
            tab_ = 1;
        }
        g_.onlineOpen.clear();
        globe_.init(g_.app.resources());
        reloadAll();
        requireAccount();
    }

    void onResume() override {
        accountAsked_ = false;
        reloadAll();
    }

    void onExit() override { g_.online.setOpenConversation({}); }

    void update(float dt) override {
        bg_.update(dt);
        t_ += dt;
        requireAccount();
        updateGlobe(dt);
        if (!g_.online.loggedIn()) return;
        // The counters of the conversations follow the unread poll of OnlineService.
        // (An older server gives only the total: the list keeps its own counts then.)
        int known = 0;
        for (const auto& c : convs_) known += g_.online.unreadIn(jstr(c, "id"));
        const bool perConversation = g_.online.unreadMessages() == 0 || known > 0;
        if (unreadSeen_ != g_.online.unreadRevision() && perConversation) {
            unreadSeen_ = g_.online.unreadRevision();
            for (auto& c : convs_) {
                std::string id = jstr(c, "id");
                c["unread"] = id == convId_ ? 0 : g_.online.unreadIn(id);
            }
        }
        convTimer_ += dt;
        if (convTimer_ >= kConversationsPoll) loadConversations();
        // The friends list on screen: presence and runs being played stay current.
        friendsTimer_ += dt;
        if ((tab_ == 1 && friendsTimer_ >= kFriendsPoll) || (!watchName_.empty() && friendsTimer_ >= 2.0f)) loadFriends();
        if (!convId_.empty()) {
            msgTimer_ += dt;
            if (msgTimer_ >= kMessagesPoll) loadMessages(false);
        }
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        layout();
        bg_.render(g_, 0.5f);
        ui.inputEnabled = modal_ == Modal::None;
        drawHeader();
        float navH = drawOnlineNav(g_, zoneA_, OnlineSection::Social);
        drawInfoCard(navH);
        drawListZone();
        drawChatZone();
        ui.inputEnabled = true;
        if (modal_ != Modal::None) drawModal();
        else if (ui.backPressed() && !ui.editingText()) {
            if (!convId_.empty()) closeConversation();
            else g_.app.switchScene(makeOnlineRanking(g_));
        }
        ui.end();
    }

private:
    enum class Modal { None, NewGroup, Members, Confirm };

    // ------------------------------------------------------------- account
    void requireAccount() {
        if (g_.online.loggedIn()) {
            accountAsked_ = false;
            return;
        }
        if (accountAsked_ || g_.app.topScene() != this || g_.app.transitioning()) return;
        accountAsked_ = true;
        g_.app.pushScene(makeOnlineAccount(g_, !g_.settings.online.onboardingDone, true));
    }

    void say(const std::string& m, bool error) {
        status_ = m;
        statusError_ = error;
        statusT_ = g_.ui.time();
    }

    // ---------------------------------------------------------------- data
    void reloadAll() {
        if (!g_.online.loggedIn()) return;
        loadConversations();
        loadFriends();
        loadBlocks();
        g_.online.refreshUnread();
        requestGlobe();
    }

    void loadConversations() {
        convTimer_ = 0.0f;
        if (!g_.online.loggedIn() || convBusy_) return;
        convBusy_ = true;
        g_.online.get("/api/v1/social/conversations", OnlineService::guard(life_, [this](const ApiResult& r) {
            convBusy_ = false;
            convLoaded_ = true;
            if (!r.ok) {
                if (!r.fromCache) say(r.message, true);
                return;
            }
            convs_ = r.data.value("conversations", Json::array());
            convCache_ = r.fromCache;
            // The conversation on screen is being read: no counter on it.
            for (auto& c : convs_)
                if (jstr(c, "id") == convId_) c["unread"] = 0;
            if (!wantedConversation_.empty()) {
                std::string id = wantedConversation_;
                wantedConversation_.clear();
                openConversation(id);
            }
            if (!convId_.empty())
                if (const Json* c = conversation(convId_)) current_ = *c;
        }));
    }

    void loadFriends() {
        friendsTimer_ = 0.0f;
        if (!g_.online.loggedIn()) return;
        g_.online.get("/api/v1/friends", OnlineService::guard(life_, [this](const ApiResult& r) {
            friendsLoaded_ = true;
            if (!r.ok) return;
            friends_ = r.data;
            if (!r.fromCache) g_.online.noteFriends(r.data);  // badges, no repeated notification
            // A run being played (2.35) is only true now: never from the cache (« Regarder »
            // shows only when it can work).
            if (r.fromCache && friends_.contains("friends") && friends_["friends"].is_array())
                for (auto& f : friends_["friends"])
                    if (f.is_object()) f.erase("playing");
            // Automated checks (--online-open watch:<pseudonym>): that friend's run, once playing.
            if (!watchName_.empty())
                for (const auto& f : friends_.value("friends", Json::array()))
                    if (jstr(f["player"], "username") == watchName_ && f.contains("playing")) {
                        watchName_.clear();
                        g_.app.pushScene(makeSpectate(g_, jstr(f["player"], "id"), jstr(f["player"], "username"), jstr(f["player"], "avatar", "avatar_01")));
                        break;
                    }
            if (openModal_ == "newgroup") {
                openModal_.clear();
                for (const auto& f : friends_.value("friends", Json::array()))
                    if (groupPick_.size() < 2) groupPick_.push_back(jstr(f["player"], "id"));
                groupName_ = "Course du vendredi";
                modal_ = Modal::NewGroup;
            }
        }));
    }

    void loadBlocks() {
        if (!g_.online.loggedIn()) return;
        g_.online.get("/api/v1/social/blocks", OnlineService::guard(life_, [this](const ApiResult& r) {
            if (r.ok) blocks_ = r.data.value("blocked", Json::array());
        }));
    }

    const Json* conversation(const std::string& id) const {
        for (const auto& c : convs_)
            if (jstr(c, "id") == id) return &c;
        return nullptr;
    }

    void openConversation(const std::string& id) {
        if (id == convId_) return;
        convId_ = id;
        messages_ = Json::array();
        hasMore_ = false;
        messagesLoaded_ = false;
        selectedMsg_.clear();
        draft_.clear();
        if (const Json* c = conversation(id)) current_ = *c;
        else current_ = Json::object();
        g_.online.setOpenConversation(id);
        globeInput_ = {};
        stickBottom_ = true;
        loadMessages(false);
    }

    void closeConversation() {
        convId_.clear();
        current_ = Json::object();
        messages_ = Json::array();
        g_.online.setOpenConversation({});
        if (modal_ == Modal::Members) modal_ = Modal::None;
    }

    /// `older`: loads the page before the first message shown; otherwise the newest page,
    /// merged with what is already displayed.
    void loadMessages(bool older) {
        msgTimer_ = 0.0f;
        if (convId_.empty() || msgBusy_) return;
        std::string path = "/api/v1/social/conversations/" + convId_ + "/messages?limit=50";
        if (older && !messages_.empty()) path += "&before=" + jstr(messages_.front(), "created_at");
        msgBusy_ = true;
        std::string id = convId_;
        g_.online.get(
            path, OnlineService::guard(life_, [this, id, older](const ApiResult& r) {
                msgBusy_ = false;
                if (id != convId_) return;
                messagesLoaded_ = true;
                if (!r.ok) {
                    if (r.status == 404) {
                        say("Cette conversation n'est plus accessible.", true);
                        closeConversation();
                        loadConversations();
                    }
                    return;
                }
                msgCache_ = r.fromCache;
                if (openModal_ == "members" && jstr(current_, "kind") == "group") {
                    openModal_.clear();
                    renameText_ = jstr(current_, "name");
                    modal_ = Modal::Members;
                }
                mergeMessages(r.data.value("messages", Json::array()), older, jget<bool>(r.data, "has_more", false));
            }),
            false);
    }

    void mergeMessages(const Json& page, bool older, bool more) {
        if (older) {
            Json merged = page;
            for (const auto& m : messages_) merged.push_back(m);
            messages_ = merged;
            hasMore_ = more;
            return;
        }
        if (messages_.empty()) hasMore_ = more;
        std::map<std::string, size_t> index;
        for (size_t i = 0; i < messages_.size(); ++i) index[jstr(messages_[i], "id")] = i;
        bool fresh = false, fromOthers = false;
        for (const auto& m : page) {
            auto it = index.find(jstr(m, "id"));
            if (it != index.end()) {
                messages_[it->second] = m;  // deletions
                continue;
            }
            messages_.push_back(m);
            fresh = true;
            if (!jget<bool>(m, "mine", false)) fromOthers = true;
        }
        if (fresh) {
            std::stable_sort(messages_.begin(), messages_.end(),
                             [](const Json& a, const Json& b) { return jstr(a, "created_at") < jstr(b, "created_at"); });
            if (atBottom_) stickBottom_ = true;
        }
        // Read on screen: the server marks it, the badges follow.
        const Json* c = conversation(convId_);
        if (fromOthers || (c && jget<int>(*c, "unread", 0) > 0)) markRead();
    }

    void markRead() {
        std::string id = convId_;
        g_.online.send("POST", "/api/v1/social/conversations/" + id + "/read", Json::object(),
                       OnlineService::guard(life_, [this, id](const ApiResult& r) {
                           if (!r.ok) return;
                           for (auto& c : convs_)
                               if (jstr(c, "id") == id) c["unread"] = 0;
                           // The badges (SOCIAL, menu, tab) drop at once; the poll confirms.
                           g_.online.noteConversationRead(id);
                           unreadSeen_ = g_.online.unreadRevision();
                           g_.online.refreshUnread();
                       }));
    }

    void sendDraft() {
        std::string text = draft_;
        while (!text.empty() && (text.back() == ' ')) text.pop_back();
        size_t start = text.find_first_not_of(' ');
        if (start == std::string::npos || convId_.empty() || sending_) return;
        sending_ = true;
        std::string id = convId_;
        g_.online.send("POST", "/api/v1/social/conversations/" + id + "/messages", {{"body", text.substr(start)}},
                       OnlineService::guard(life_, [this, id](const ApiResult& r) {
                           sending_ = false;
                           if (!r.ok) return say(r.message, true);
                           if (id != convId_) return;
                           draft_.clear();
                           stickBottom_ = true;
                           loadMessages(false);
                           loadConversations();
                       }));
    }

    void openDirect(const std::string& playerId) {
        g_.online.send("POST", "/api/v1/social/direct", {{"player_id", playerId}}, OnlineService::guard(life_, [this](const ApiResult& r) {
            if (!r.ok) return say(r.message, true);
            const Json& c = r.data["conversation"];
            std::string id = jstr(c, "id");
            if (!conversation(id)) convs_.insert(convs_.begin(), c);
            tab_ = 0;
            openConversation(id);
            loadConversations();
        }));
    }

    void confirm(std::string text, std::string action, std::function<void()> f, bool danger = true) {
        confirmText_ = std::move(text);
        confirmAction_ = std::move(action);
        confirmFn_ = std::move(f);
        confirmDanger_ = danger;
        reportReason_ = -1;
        modal_ = Modal::Confirm;
    }

    void block(const std::string& playerId, const std::string& name) {
        confirm("Bloquer " + name + " ? Votre amitié prend fin, ses messages vous sont masqués (y compris dans les groupes) et il ne pourra "
                "plus vous écrire ni vous envoyer de demande d'ami. Vous pourrez le débloquer dans l'onglet « Bloqués ».",
                "Bloquer", [this, playerId] {
                    g_.online.send("POST", "/api/v1/social/blocks", {{"player_id", playerId}}, OnlineService::guard(life_, [this](const ApiResult& r) {
                        say(r.ok ? "Joueur bloqué." : r.message, !r.ok);
                        if (!r.ok) return;
                        if (jstr(current_, "kind") == "direct") closeConversation();
                        reloadAll();
                        g_.online.refreshUnread();
                    }));
                });
    }

    // -------------------------------------------------------------- layout
    void layout() {
        UIContext& ui = g_.ui;
        float W = ui.screenW(), H = ui.screenH();
        float m = ui.s(22);
        header_ = {0, 0, W, ui.s(64)};
        zoneA_ = {m, header_.bottom() + ui.s(8), ui.s(272), H - header_.bottom() - ui.s(8) - m};
        zoneL_ = {zoneA_.right() + ui.s(18), zoneA_.y, ui.s(470), zoneA_.h};
        zoneC_ = {zoneL_.right() + ui.s(18), zoneA_.y, W - m - zoneL_.right() - ui.s(18), zoneA_.h};
    }

    void drawHeader() {
        UIContext& ui = g_.ui;
        ui.r().fillGradientV(header_, Color{6, 10, 24, 230}, Color{6, 10, 24, 0});
        Vec2 tsz = ui.label("COSMIC ONLINE", {ui.s(32), header_.center().y}, 34, ui.theme().accent, Align::Left, FontFace::Title, VAlign::Middle);
        ui.label("Social", {ui.s(32) + tsz.x + ui.s(18), header_.center().y + ui.s(3)}, 19, ui.theme().textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
        ui_.drawConnection({zoneC_.right() - ui.s(240), header_.center().y});
    }

    /// Under the navigation column (whatever its height), sized to its content and to the room
    /// left: the explanation is left out rather than drawn over or past anything.
    void drawInfoCard(float navH) {
        UIContext& ui = g_.ui;
        const float top = zoneA_.y + navH + ui.s(16), room = zoneA_.bottom() - top;
        float w = zoneA_.w - ui.s(32);
        int unread = g_.online.unreadMessages(), req = g_.online.pendingFriendRequests();
        static const char* kNote =
            "Seuls vos amis peuvent vous écrire. Les messages sont privés : seuls les membres de la conversation les voient. "
            "Bloquer un joueur met fin à l'amitié et masque ses messages.";
        const float linesH = ui.s(req > 0 ? 88 : 66);
        const float noteH = ui.text().measureWrapped(kNote, FontFace::Body, ui.s(14), w).y;
        float h = linesH + ui.s(10) + noteH + ui.s(14);
        const bool note = h <= room;
        if (!note) h = linesH + ui.s(4);
        if (h > room) return;  // not even the summary fits: the column stays free
        Rect card{zoneA_.x, top, zoneA_.w, h};
        ui.r().fillRoundedRect(card, ui.s(14), Color{10, 16, 34, 215});
        ui.r().drawRoundedRect(card, ui.s(14), ui.theme().border, ui.s(1.2f));
        float x = card.x + ui.s(16);
        std::string name = g_.online.loggedIn() ? g_.online.username() : "Non connecté";
        ui.label(name, {x, card.y + ui.s(22)}, ui.fitFont(name, FontFace::Bold, 20, w), ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        std::string line = unread > 0 ? std::to_string(unread) + (unread > 1 ? " messages non lus" : " message non lu") : "Aucun message non lu";
        ui.label(line, {x, card.y + ui.s(48)}, ui.fitFont(line, FontFace::Body, 15, w), unread > 0 ? ui.theme().accent : ui.theme().textDim, Align::Left,
                 FontFace::Body, VAlign::Middle);
        if (req > 0) {
            std::string r = std::to_string(req) + (req > 1 ? " demandes d'amis" : " demande d'ami");
            ui.label(r, {x, card.y + ui.s(70)}, ui.fitFont(r, FontFace::Body, 15, w), ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
        }
        if (note) ui.labelWrapped(kNote, {x, card.y + linesH + ui.s(10)}, w, 14, ui.theme().textDim);
    }

    // ------------------------------------------------------------ list zone
    void drawListZone() {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(zoneL_, ui.s(14), Color{8, 13, 30, 220});
        ui.r().drawRoundedRect(zoneL_, ui.s(14), ui.theme().border, ui.s(1.2f));
        ui.pushId("social/list");
        int nFriends = static_cast<int>(friends_.value("friends", Json::array()).size());
        bool in = g_.online.loggedIn();
        int req = in ? g_.online.pendingFriendRequests() : 0;
        std::string amis = "Amis (" + std::to_string(nFriends) + ")";
        int tab = tab_;
        // Red counters: unread messages on « Discussions », friend requests on « Amis ». A
        // counter is left out of a tab too narrow for it and a readable label (the same
        // numbers stay on SOCIAL and on each conversation).
        const Rect tabsRect{zoneL_.x + ui.s(12), zoneL_.y + ui.s(12), zoneL_.w - ui.s(24), ui.s(40)};
        const std::vector<std::string> labels = {"Discussions", amis, "Bloqués"};
        std::vector<int> badges = {in ? unreadTotal() : 0, req, 0};
        for (size_t i = 0; i < labels.size(); ++i) {
            if (badges[i] == 0) continue;
            const float tw = tabsRect.w / static_cast<float>(labels.size()) - ui.s(4);
            const float bh = std::min(ui.s(20), tabsRect.h - ui.s(8));
            const std::string n = badges[i] > 99 ? "99+" : std::to_string(badges[i]);
            const float pill = std::max(bh, ui.text().measure(n, FontFace::Bold, ui.s(13)).x + ui.s(10)) + ui.s(8);
            if (ui.text().measure(labels[i], FontFace::Bold, ui.s(21) * 0.72f).x > tw - pill - ui.s(10)) badges[i] = 0;
        }
        ui.tabs("tabs", tabsRect, labels, tab, badges);
        {
            // « NOUVEAU » on the « Amis » tab (2.35): « Regarder » is under it.
            const float tw = tabsRect.w / static_cast<float>(labels.size());
            ui.newBadge("tabs", {tabsRect.x + tw + ui.s(2), tabsRect.y, tw - ui.s(4), tabsRect.h}, "social.tab.friends", true, true);
        }
        if (tab != tab_) {
            tab_ = tab;
            expanded_.clear();
            if (tab_ == 1) loadFriends();
            if (tab_ == 2) loadBlocks();
        }
        Rect area{zoneL_.x + ui.s(12), zoneL_.y + ui.s(64), zoneL_.w - ui.s(24), zoneL_.h - ui.s(76)};
        if (!g_.online.loggedIn()) {
            ui.labelWrapped("Connectez-vous pour discuter avec vos amis.", {area.x, area.y}, area.w, 17, ui.theme().textDim);
        } else if (tab_ == 0) {
            drawConversations(area);
        } else if (tab_ == 1) {
            drawFriends(area);
        } else {
            drawBlocked(area);
        }
        ui.popId();
    }

    void drawConversations(const Rect& area) {
        UIContext& ui = g_.ui;
        UIContext::ButtonStyle p;
        p.primary = true;
        p.small = true;
        p.enabled = !friends_.value("friends", Json::array()).empty();
        if (ui.button("newgroup", {area.x, area.y, ui.s(210), ui.s(40)}, "Nouveau groupe", p)) {
            groupName_.clear();
            groupPick_.clear();
            modal_ = Modal::NewGroup;
        }
        if (convCache_)
            ui.label("Hors ligne : liste en cache", {area.right(), area.y + ui.s(20)}, 14, ui.theme().warm, Align::Right, FontFace::Body, VAlign::Middle);
        Rect view{area.x, area.y + ui.s(52), area.w, area.h - ui.s(52)};
        if (convs_.empty()) {
            ui.labelWrapped(convLoaded_ ? "Aucune discussion pour l'instant. Ouvrez l'onglet « Amis » et choisissez « Écrire », ou créez un groupe "
                                          "avec plusieurs amis."
                                        : "Chargement…",
                            {view.x, view.y + ui.s(8)}, view.w, 16, ui.theme().textDim);
            return;
        }
        const float rowH = ui.s(66);
        ui.beginScroll("convs", view, convs_.size() * (rowH + ui.s(6)));
        float y = view.y - ui.scrollOffset();
        for (const auto& c : convs_) {
            std::string id = jstr(c, "id");
            Rect row{view.x, y, view.w - ui.s(12), rowH};
            bool sel = id == convId_;
            UIContext::ButtonStyle st;
            st.selected = sel;
            st.align = Align::Left;
            if (ui.button("c" + id, row, "", st)) openConversation(id);
            drawConversationAvatar(c, {row.x + ui.s(8), row.y + ui.s(9), ui.s(48), ui.s(48)});
            float tx = row.x + ui.s(66);
            int unread = sel ? 0 : jget<int>(c, "unread", 0);
            // Unread: an accent bar at the left of the row and the counter at its right edge,
            // placed first so that the time and the preview keep the room left of it.
            if (unread > 0) ui.r().fillRoundedRect({row.x + ui.s(2), row.y + ui.s(10), ui.s(4), row.h - ui.s(20)}, ui.s(2), ui.theme().accent);
            float pillLeft = row.right() - ui.s(12);
            if (unread > 0)
                pillLeft = OnlineUi::countPill(ui, pillLeft, row.y + ui.s(46), unread, ui.theme().danger, Color::white(), 22, 13) - ui.s(8);
            std::string name = jstr(c, "name");
            std::string when = c.contains("last_message") && c["last_message"].is_object() ? clockText(jstr(c["last_message"], "created_at")) : "";
            float whenW = when.empty() ? 0.0f : ui.text().measure(when, FontFace::Body, ui.s(13)).x + ui.s(12);
            float room = row.right() - ui.s(12) - whenW - tx - ui.s(6);
            {
                i18n::Verbatim playerWords;  // a pseudonym or a group name stays as its players wrote it
                const float nfs = ui.fitFont(name, FontFace::Bold, 18, room);
                ui.label(ui.elide(name, FontFace::Bold, nfs, room), {tx, row.y + ui.s(22)}, nfs, unread > 0 ? Color::white() : ui.theme().text,
                         Align::Left, FontFace::Bold, VAlign::Middle);
            }
            std::string preview;
            bool playerText = false;
            if (c.contains("last_message") && c["last_message"].is_object()) {
                const Json& lm = c["last_message"];
                bool mine = jstr(lm, "sender_id") == g_.online.playerId();
                // The game's own words translated here; the message itself is never translated.
                std::string who = mine ? i18n::tr("Vous") : jstr(lm, "sender");
                preview = (jstr(c, "kind") == "group" || mine ? who + " : " : "") + oneLine(jstr(lm, "body"), 70);
                if (jstr(lm, "body").empty()) preview = who + " : " + i18n::tr("message supprimé");
                playerText = true;
                ui.label(when, {row.right() - ui.s(12), row.y + ui.s(22)}, 13, unread > 0 ? ui.theme().accent : ui.theme().textDim, Align::Right,
                         FontFace::Body, VAlign::Middle);
            } else {
                preview = jstr(c, "kind") == "group" ? std::to_string(c.value("members", Json::array()).size()) + " membres · aucun message"
                                                     : "Aucun message";
            }
            // The preview ends before the counter (shortened with « … »).
            float previewRoom = pillLeft - tx;
            {
                i18n::Verbatim playerWords(playerText);
                ui.label(ui.elide(preview, FontFace::Body, 15, previewRoom), {tx, row.y + ui.s(46)}, 15, unread > 0 ? ui.theme().text : ui.theme().textDim,
                         Align::Left, FontFace::Body, VAlign::Middle);
            }
            y += rowH + ui.s(6);
        }
        ui.endScroll();
    }

    void drawConversationAvatar(const Json& c, const Rect& r) {
        UIContext& ui = g_.ui;
        if (jstr(c, "kind") == "direct") {
            for (const auto& m : c.value("members", Json::array()))
                if (jstr(m, "id") != g_.online.playerId()) {
                    ui_.drawAvatar(jstr(m, "avatar", "avatar_01"), jstr(m, "id"), r);
                    return;
                }
            ui.r().fillRoundedRect(r, r.w * 0.5f, Color{40, 46, 66, 255});
            return;
        }
        // Group: initials on a coloured disc (colour from the name).
        std::string name = jstr(c, "name", "?");
        uint32_t h = 2166136261u;
        for (unsigned char ch : name) h = (h ^ ch) * 16777619u;
        Color col{static_cast<uint8_t>(60 + h % 120), static_cast<uint8_t>(70 + (h >> 8) % 120), static_cast<uint8_t>(110 + (h >> 16) % 120), 255};
        ui.r().fillRoundedRect(r, r.w * 0.5f, col);
        std::string initial = name.substr(0, 1);
        if (!name.empty() && (static_cast<unsigned char>(name[0]) & 0x80)) {
            size_t n = 1;
            while (n < name.size() && (static_cast<unsigned char>(name[n]) & 0xC0) == 0x80) ++n;
            initial = name.substr(0, n);
        }
        ui.label(initial, r.center(), 22, Color::white(), Align::Center, FontFace::Bold, VAlign::Middle);
        ui.r().fillRoundedRect({r.right() - ui.s(16), r.bottom() - ui.s(16), ui.s(18), ui.s(18)}, ui.s(9), Color{10, 16, 34, 255});
        ui.label(std::to_string(c.value("members", Json::array()).size()), {r.right() - ui.s(7), r.bottom() - ui.s(7)}, 11, ui.theme().accent,
                 Align::Center, FontFace::Bold, VAlign::Middle);
    }

    static Color presenceColor(const std::string& p) {
        return p == "online" ? Color{72, 220, 120, 255} : p == "away" ? Color{255, 176, 60, 255} : Color{120, 128, 150, 255};
    }
    static std::string presenceWord(const std::string& p) { return p == "online" ? "en ligne" : p == "away" ? "absent" : "hors ligne"; }

    void drawFriends(const Rect& area) {
        UIContext& ui = g_.ui;
        ui.textField("add", {area.x, area.y, area.w - ui.s(150), ui.s(40)}, addName_, "Pseudonyme", 20);
        UIContext::ButtonStyle p;
        p.primary = true;
        p.small = true;
        p.enabled = addName_.size() >= 3;
        if (ui.button("send", {area.right() - ui.s(140), area.y, ui.s(140), ui.s(40)}, "Ajouter", p)) {
            g_.online.send("POST", "/api/v1/friends/request", {{"username", addName_}}, OnlineService::guard(life_, [this](const ApiResult& r) {
                say(r.ok ? (jstr(r.data, "status") == "accepted" ? "Vous êtes maintenant amis." : "Demande d'ami envoyée.") : r.message, !r.ok);
                if (r.ok) {
                    addName_.clear();
                    loadFriends();
                }
            }));
        }
        // « Regarder » new (2.35) and no friend playing now: one line says where it appears (the
        // trail of the « NOUVEAU » pills ends here; read for a few seconds, it is seen).
        float hintH = 0.0f;
        bool anyPlaying = false;
        for (const auto& f : friends_.value("friends", Json::array())) anyPlaying = anyPlaying || (f.contains("playing") && f["playing"].is_object());
        if (friendsLoaded_ && !anyPlaying && ui.isNew("social.watch")) {
            static const char* kHint = "« Regarder » apparaît ici quand un ami joue et vous autorise à le regarder.";
            const float pw = ui.newPillWidth(true), top = area.y + ui.s(52);
            const float tw = area.w - pw - ui.s(10);
            const float hf = std::max(14.0f, 11.5f / ui.s(1.0f));  // never below 11.5 pixels
            const Vec2 ts = ui.text().measureWrapped(kHint, FontFace::Body, ui.s(hf), tw);
            hintH = std::max(ts.y, ui.newPillHeight(true)) + ui.s(10);
            ui.drawNewPill(area.x + pw, top + ui.newPillHeight(true) * 0.5f + ui.s(1), true);
            ui.labelWrapped(kHint, {area.x + pw + ui.s(10), top}, tw, hf, ui.theme().textDim);
            if (hintSince_ < 0.0f) hintSince_ = ui.time();
            else if (ui.time() - hintSince_ > 3.0f && ui.featureSeen) ui.featureSeen("social.watch");
        } else {
            hintSince_ = -1.0f;
        }
        Rect view{area.x, area.y + ui.s(52) + hintH, area.w, area.h - ui.s(52) - hintH};
        struct Section {
            const char* title;
            const char* kind;
            Json list;
        };
        // Friends: online first, then away, then offline (each group by name, as received).
        Json byPresence = Json::array();
        for (const char* want : {"online", "away", "offline", ""})
            for (const auto& f : friends_.value("friends", Json::array())) {
                std::string p = jstr(f, "presence");
                if (p != "online" && p != "away" && p != "offline") p.clear();
                if (p == want) byPresence.push_back(f);
            }
        std::vector<Section> sections = {{"Demandes reçues", "in", friends_.value("incoming", Json::array())},
                                         {"Amis", "friend", byPresence},
                                         {"Demandes envoyées", "out", friends_.value("outgoing", Json::array())}};
        const float rowH = ui.s(52), extra = ui.s(46);
        // A friend playing a run that may be watched (2.35): a line more, « Regarder ».
        const float playH = ui.s(52);
        auto playing = [](const Section& s, const Json& f) {
            return s.kind == std::string("friend") && f.contains("playing") && f["playing"].is_object();
        };
        float total = 0;
        for (auto& s : sections) {
            total += ui.s(34) + std::max<size_t>(1, s.list.size()) * rowH;
            for (const auto& f : s.list) {
                if (jstr(f["player"], "id") == expanded_) total += extra;
                if (playing(s, f)) total += playH;
            }
        }
        ui.beginScroll("friends", view, total);
        float y = view.y - ui.scrollOffset();
        for (auto& s : sections) {
            ui.label(std::string(s.title) + " (" + std::to_string(s.list.size()) + ")", {view.x, y + ui.s(16)}, 17, ui.theme().accent, Align::Left,
                     FontFace::Bold, VAlign::Middle);
            y += ui.s(34);
            if (s.list.empty()) {
                ui.label(friendsLoaded_ ? "Aucun" : "Chargement…", {view.x + ui.s(12), y + ui.s(18)}, 15, ui.theme().textDim, Align::Left,
                         FontFace::Body, VAlign::Middle);
                y += rowH;
                continue;
            }
            for (const auto& f : s.list) {
                const Json& pl = f["player"];
                std::string pid = jstr(pl, "id"), name = jstr(pl, "username");
                bool open = pid == expanded_;
                const bool isPlaying = playing(s, f);
                Rect row{view.x, y, view.w - ui.s(12), rowH - ui.s(6) + (isPlaying ? playH : 0.0f) + (open ? extra : 0.0f)};
                ui.r().fillRoundedRect(row, ui.s(6), Color{16, 24, 46, 200});
                ui_.drawAvatar(jstr(pl, "avatar", "avatar_01"), pid, {row.x + ui.s(5), row.y + ui.s(4), ui.s(38), ui.s(38)});
                // Presence (2.32.3): a pill on the avatar's corner, its word after the name.
                std::string presence = s.kind == std::string("friend") ? jstr(f, "presence") : std::string();
                if (!presence.empty()) {
                    Color pc = presenceColor(presence);
                    Vec2 pd{row.x + ui.s(38), row.y + ui.s(37)};
                    ui.r().fillCircle(pd, ui.s(7), Color{10, 14, 30, 255});
                    ui.r().fillCircle(pd, ui.s(5), pc);
                }
                float nx = row.x + ui.s(52);
                std::string cc = jstr(pl, "country_code");
                if (!cc.empty()) {
                    ui_.drawFlag(cc, {nx, row.y + ui.s(15), ui.s(22), ui.s(16)});
                    nx += ui.s(30);
                }
                UIContext::ButtonStyle b;
                b.small = true;
                float bx = row.right() - ui.s(6);
                auto rowButton = [&](const std::string& id, const std::string& label, float w, bool primary = false, bool danger = false) {
                    UIContext::ButtonStyle st = b;
                    st.primary = primary;
                    st.danger = danger;
                    bx -= w;
                    bool c = ui.button(id + pid, {bx, row.y + ui.s(6), w, ui.s(34)}, label, st);
                    bx -= ui.s(6);
                    return c;
                };
                std::string kind = s.kind;
                if (kind == "in") {
                    if (rowButton("refuse", "Refuser", ui.s(96)))
                        friendCall("DELETE", "/api/v1/friends/" + pid, Json(), "Demande refusée.");
                    if (rowButton("accept", "Accepter", ui.s(104), true))
                        friendCall("POST", "/api/v1/friends/accept", {{"player_id", pid}}, "Vous êtes maintenant amis.");
                } else if (kind == "friend") {
                    if (rowButton("more", "…", ui.s(40))) expanded_ = open ? std::string{} : pid;
                    if (rowButton("write", "Écrire", ui.s(92), true)) openDirect(pid);
                } else {
                    if (rowButton("cancel", "Annuler", ui.s(96))) friendCall("DELETE", "/api/v1/friends/" + pid, Json(), "Demande annulée.");
                }
                float room = bx - nx - ui.s(4);
                const std::string word = presence.empty() ? std::string() : presenceWord(presence);
                float ww = word.empty() ? 0.0f : ui.text().measure(word, FontFace::Body, ui.s(14)).x + ui.s(14);
                if (ww > room * 0.45f) ww = 0.0f;  // no room: the pill on the avatar says it
                float nf = ui.fitFont(name, FontFace::Bold, 17, room - ww);
                Vec2 ns;
                {
                    i18n::Verbatim v;  // the name as its player chose it
                    ns = ui.label(name, {nx, row.y + ui.s(23)}, nf, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
                }
                if (ww > 0.0f)
                    ui.label(word, {nx + ns.x + ui.s(12), row.y + ui.s(23)}, 14, presenceColor(presence), Align::Left, FontFace::Body, VAlign::Middle);
                float ay = row.y + rowH - ui.s(4);
                if (isPlaying) {
                    drawPlaying(f, pid, name, jstr(pl, "avatar", "avatar_01"), {row.x + ui.s(52), row.y + rowH + ui.s(4), row.right() - ui.s(6) - (row.x + ui.s(52)), ui.s(34)});
                    ay += playH;
                }
                if (open) {
                    UIContext::ButtonStyle d = b;
                    d.danger = true;
                    if (ui.button("remove" + pid, {row.x + ui.s(52), ay, ui.s(160), ui.s(34)}, "Retirer l'ami", b))
                        confirm("Retirer " + name + " de vos amis ? Votre conversation privée restera lisible mais vous ne pourrez plus vous écrire.",
                                "Retirer", [this, pid] {
                                    friendCall("DELETE", "/api/v1/friends/" + pid, Json(), "Ami retiré.");
                                    expanded_.clear();
                                });
                    if (ui.button("block" + pid, {row.x + ui.s(222), ay, ui.s(130), ui.s(34)}, "Bloquer", d)) block(pid, name);
                }
                y += rowH + (isPlaying ? playH : 0.0f) + (open ? extra : 0.0f);
            }
        }
        ui.endScroll();
    }

    /// The line of a friend playing a run they let friends watch: what they play, then
    /// « Regarder » at the right (its « NOUVEAU » pill on its top edge, clear of the row's
    /// buttons above). The text is measured into the room the button leaves.
    void drawPlaying(const Json& f, const std::string& pid, const std::string& name, const std::string& avatar, const Rect& line) {
        UIContext& ui = g_.ui;
        const Json& p = f["playing"];
        const float bw = std::max(ui.s(118), ui.text().measure("Regarder", FontFace::Bold, ui.s(16)).x + ui.s(36));
        const Rect btn{line.right() - bw, line.y, bw, line.h};
        UIContext::ButtonStyle st;
        st.small = true;
        st.primary = true;
        if (ui.button("watch" + pid, btn, "Regarder", st)) g_.app.pushScene(makeSpectate(g_, pid, name, avatar));
        ui.newBadge("watch" + pid, btn, "social.watch", true, true);  // « NOUVEAU » (2.35)
        const std::string prefix = jstr(p, "kind") == "challenge" ? "En défi · " : "En partie · ";
        const std::string title = jstr(p, "level_title");
        // Never below 11.5 pixels (small windows): below, the font loses its spaces.
        const float fs = std::max(14.0f, 11.5f / ui.s(1.0f)), cy = line.center().y;
        float room = btn.x - ui.s(12) - line.x;
        const float pw = ui.text().measure(prefix, FontFace::Body, ui.s(fs)).x;
        if (pw >= room) return;
        ui.label(prefix, {line.x, cy}, fs, Color{255, 120, 140, 255}, Align::Left, FontFace::Body, VAlign::Middle);
        room -= pw;
        if (title.empty() || room < ui.s(24)) return;
        // A campaign level's name is translated; a community level's title is its author's.
        i18n::Verbatim v(jstr(p, "level_source") == "community");
        ui.label(ui.elide(title, FontFace::Body, fs, room), {line.x + pw, cy}, fs, ui.theme().text, Align::Left, FontFace::Body, VAlign::Middle);
    }

    void friendCall(const std::string& method, const std::string& path, const Json& body, const std::string& done) {
        g_.online.send(method, path, body, OnlineService::guard(life_, [this, done](const ApiResult& r) {
            say(r.ok ? done : r.message, !r.ok);
            loadFriends();
            g_.online.refreshFriends();
        }));
    }

    void drawBlocked(const Rect& area) {
        UIContext& ui = g_.ui;
        ui.labelWrapped("Un joueur bloqué ne peut plus vous écrire ni vous envoyer de demande d'ami, et ses messages vous sont masqués dans "
                        "les groupes. Il n'est pas prévenu.",
                        {area.x, area.y}, area.w, 15, ui.theme().textDim);
        Rect view{area.x, area.y + ui.s(70), area.w, area.h - ui.s(70)};
        if (blocks_.empty()) {
            ui.label("Aucun joueur bloqué", {view.x, view.y + ui.s(16)}, 16, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            return;
        }
        const float rowH = ui.s(52);
        ui.beginScroll("blocks", view, blocks_.size() * rowH);
        float y = view.y - ui.scrollOffset();
        for (const auto& b : blocks_) {
            const Json& pl = b["player"];
            std::string pid = jstr(pl, "id");
            Rect row{view.x, y, view.w - ui.s(12), rowH - ui.s(6)};
            ui.r().fillRoundedRect(row, ui.s(6), Color{16, 24, 46, 200});
            ui_.drawAvatar(jstr(pl, "avatar", "avatar_01"), pid, {row.x + ui.s(5), row.y + ui.s(4), ui.s(38), ui.s(38)});
            ui.label(jstr(pl, "username"), {row.x + ui.s(52), row.center().y}, 17, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            UIContext::ButtonStyle st;
            st.small = true;
            if (ui.button("unblock" + pid, {row.right() - ui.s(126), row.y + ui.s(6), ui.s(120), ui.s(34)}, "Débloquer", st))
                g_.online.send("DELETE", "/api/v1/social/blocks/" + pid, Json(), OnlineService::guard(life_, [this](const ApiResult& r) {
                    say(r.ok ? "Joueur débloqué (vous n'êtes plus amis : renvoyez une demande si vous le souhaitez)." : r.message, !r.ok);
                    loadBlocks();
                    g_.online.refreshUnread();
                }));
            y += rowH;
        }
        ui.endScroll();
    }

    // ------------------------------------------------------------ chat zone
    struct Bubble {
        size_t index;
        float y, h, w;
        bool mine, showName;
    };

    void drawChatZone() {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(zoneC_, ui.s(14), Color{8, 13, 30, 220});
        ui.r().drawRoundedRect(zoneC_, ui.s(14), ui.theme().border, ui.s(1.2f));
        ui.pushId("social/chat");
        if (convId_.empty()) {
            // Without an open conversation: the globe of the players of Cosmic Online.
            drawPlayersGlobe();
            ui.popId();
            return;
        }
        // Title bar
        Rect bar{zoneC_.x, zoneC_.y, zoneC_.w, ui.s(70)};
        bool group = jstr(current_, "kind") == "group";
        drawConversationAvatar(current_, {bar.x + ui.s(14), bar.y + ui.s(11), ui.s(48), ui.s(48)});
        std::string title = jstr(current_, "name");
        {
            i18n::Verbatim playerWords;  // the group's name as its players wrote it
            ui.label(title, {bar.x + ui.s(74), bar.y + ui.s(26)}, ui.fitFont(title, FontFace::Bold, 22, bar.w - ui.s(420)), ui.theme().text,
                     Align::Left, FontFace::Bold, VAlign::Middle);
        }
        size_t nMembers = current_.value("members", Json::array()).size();
        ui.label(group ? std::to_string(nMembers) + " membres" : "Conversation privée", {bar.x + ui.s(74), bar.y + ui.s(50)}, 14, ui.theme().textDim,
                 Align::Left, FontFace::Body, VAlign::Middle);
        UIContext::ButtonStyle st;
        st.small = true;
        float bx = bar.right() - ui.s(14);
        if (ui.button("close", {bx - ui.s(110), bar.y + ui.s(16), ui.s(110), ui.s(38)}, "Fermer", st)) {
            closeConversation();
            ui.popId();
            return;
        }
        bx -= ui.s(120);
        if (group) {
            if (ui.button("members", {bx - ui.s(140), bar.y + ui.s(16), ui.s(140), ui.s(38)}, "Membres", st)) {
                renameText_ = title;
                addPick_ = 0;
                modal_ = Modal::Members;
            }
        } else {
            for (const auto& m : current_.value("members", Json::array()))
                if (std::string pid = jstr(m, "id"); pid != g_.online.playerId()) {
                    UIContext::ButtonStyle d = st;
                    d.danger = true;
                    if (ui.button("block", {bx - ui.s(140), bar.y + ui.s(16), ui.s(140), ui.s(38)}, "Bloquer", d)) block(pid, jstr(m, "username"));
                }
        }
        ui.separator({zoneC_.x + ui.s(14), bar.bottom()}, zoneC_.w - ui.s(28));

        // Messages
        Rect inputRow{zoneC_.x + ui.s(14), zoneC_.bottom() - ui.s(62), zoneC_.w - ui.s(28), ui.s(46)};
        Rect view{zoneC_.x + ui.s(10), bar.bottom() + ui.s(6), zoneC_.w - ui.s(20), inputRow.y - ui.s(40) - bar.bottom()};
        drawMessages(view, group);
        drawStatus({inputRow.x, inputRow.y - ui.s(32), inputRow.w, ui.s(28)});

        // Input: Enter sends and keeps the field open.
        bool enter = ui.editingText() && ui.focused("draft") && g_.app.input().keyPressed(SDL_SCANCODE_RETURN);
        ui.textField("draft", {inputRow.x, inputRow.y, inputRow.w - ui.s(150), inputRow.h}, draft_,
                     group ? "Message au groupe…" : "Votre message…", kMaxMessage);
        UIContext::ButtonStyle p;
        p.primary = true;
        p.enabled = !sending_ && draft_.find_first_not_of(' ') != std::string::npos;
        bool clicked = ui.button("send", {inputRow.right() - ui.s(140), inputRow.y, ui.s(140), inputRow.h}, sending_ ? "Envoi…" : "Envoyer", p);
        if ((clicked || enter) && p.enabled) {
            sendDraft();
            if (enter) ui.editText("draft");
        }
        if (!draft_.empty())
            ui.label(std::to_string(draft_.size()) + " / " + std::to_string(kMaxMessage), {inputRow.right() - ui.s(160), inputRow.bottom() + ui.s(10)}, 12,
                     ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
        ui.popId();
    }

    // ------------------------------------------------------- players globe
    /// Unread messages shown on the « Discussions » tab: the sum of the rows once the list
    /// is known (so that both always agree), the last poll before.
    int unreadTotal() const {
        if (!convLoaded_) return g_.online.unreadMessages();
        int n = 0;
        for (const auto& c : convs_)
            if (jstr(c, "id") != convId_) n += jget<int>(c, "unread", 0);
        return n;
    }

    void requestGlobe() {
        globeTimer_ = 0.0f;
        if (!g_.online.loggedIn() || globeLoading_) return;
        globeLoading_ = true;
        g_.online.get("/api/v1/globe/players", OnlineService::guard(life_, [this](const ApiResult& r) {
            globeLoading_ = false;
            globeLoaded_ = true;
            if (!r.ok) {
                // Nothing in the cache either: no point is shown (never an invented one).
                globeError_ = r.message;
                globeCountries_.clear();
                return;
            }
            globeError_.clear();
            globeCache_ = r.fromCache;
            globeAge_ = r.cacheAgeSeconds;
            globeCountries_.clear();
            for (const auto& c : r.data.value("countries", Json::array()))
                if (auto cc = GlobeCountryCount::fromJson(c)) globeCountries_.push_back(*cc);
            std::stable_sort(globeCountries_.begin(), globeCountries_.end(),
                             [](const GlobeCountryCount& a, const GlobeCountryCount& b) { return a.players > b.players; });
            // Automated checks (--online-focus <country code>): that country is chosen.
            if (g_.onlineFocus.size() == 2)
                for (size_t i = 0; i < globeCountries_.size(); ++i)
                    if (globeCountries_[i].code == g_.onlineFocus) {
                        g_.onlineFocus.clear();
                        focusCountry(static_cast<int>(i));
                        break;
                    }
        }));
    }

    int globePlayers() const {
        int n = 0;
        for (const auto& c : globeCountries_) n += c.players;
        return n;
    }

    int globeIndexOf(const std::string& code) const {
        for (size_t i = 0; i < globeCountries_.size(); ++i)
            if (globeCountries_[i].code == code) return static_cast<int>(i);
        return -1;
    }

    /// Chooses a country (« » buttons, click): the globe turns to it and its card stays shown.
    void focusCountry(int index) {
        if (index < 0 || index >= static_cast<int>(globeCountries_.size())) return;
        globeFocus_ = globeCountries_[static_cast<size_t>(index)].code;
        globeCam_.flyTo(globeCountries_[static_cast<size_t>(index)].location, std::max(globeCam_.zoom, 1.35), 1.0f);
        globeIdle_ = 0.0f;
    }

    /// The player's own country, when they are counted on this globe (visible location, public profile).
    std::string myGlobeCountry() const {
        const Json& p = g_.online.profile();
        if (!p.is_object() || jstr(p, "location_visibility") != "globe" || jstr(p, "profile_visibility", "public") != "public") return {};
        return jstr(p, "country_code");
    }

    void updateGlobe(float dt) {
        globeCam_.update(dt);
        globeIdle_ += dt;
        if (g_.online.loggedIn() && (globeTimer_ += dt) >= kGlobePoll && convId_.empty()) requestGlobe();
        globeHover_.clear();
        if (!convId_.empty() || modal_ != Modal::None || g_.app.topScene() != this || g_.ui.editingText()) {
            globeInput_ = {};
            return;
        }
        InputManager& in = g_.app.input();
        Vec2 mp = in.mousePos();
        bool blocked = globeButtons_.contains(mp) || globeInfo_.contains(mp);
        bool click = GlobeRenderer::handleInput(in, globeInput_, globeCam_, globeView_, globeArea_, blocked, dt, g_.ui.s(5));
        if (globeInput_.touched) globeIdle_ = 0.0f;
        // Hover: a point first, otherwise the country under the pointer (no visible player).
        if (globeArea_.contains(mp) && !blocked) {
            int hit = pickCountryPoint(globePoints_, mp, globePickRadius());
            if (hit >= 0) globeHover_ = globeCountries_[static_cast<size_t>(globePoints_[static_cast<size_t>(hit)].index)].code;
            else if (auto geo = unprojectScreen(globeCam_, globeView_, mp))
                if (const CountryInfo* ci = ui_.countries().byIndex(ui_.countryMap().indexAt(*geo))) globeHover_ = ci->code;
        }
        if (click) {
            globeIdle_ = 0.0f;
            int i = globeIndexOf(globeHover_);
            if (i >= 0) {
                focusCountry(i);
                g_.uiSound("ui_move");
            } else {
                globeFocus_ = globeHover_;  // a country without visible players (or the ocean: none)
            }
        }
        // Gentle rotation while nobody touches the globe and no country is chosen.
        if (globeIdle_ > 6.0f && globeFocus_.empty() && globeHover_.empty() && !globeCam_.flying())
            globeCam_.center.lon = wrapLon(globeCam_.center.lon + dt * 3.0);
    }

    float globePickRadius() const { return std::max(g_.ui.s(16), 4.6f * markerScale(g_.ui.scale(), globeCam_.zoom) * 1.6f + g_.ui.s(6)); }

    static std::string playersText(int n) { return n == 1 ? std::string("1 joueur") : std::to_string(n) + " joueurs"; }

    void drawPlayersGlobe() {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const bool in = g_.online.loggedIn();
        const float pad = ui.s(18), x = zoneC_.x + pad, w = zoneC_.w - 2 * pad;
        // Header, one line under the other: title, total (or the state), what to do.
        float y = zoneC_.y + ui.s(14);
        const std::string title = "Joueurs de Cosmic Online";
        // « NOUVEAU » (2.26) after the title; seen once the player points at this globe (the
        // mouse moved over it, a click, a drag) or reaches its buttons.
        const bool globeNew = ui.isNew("social.globe");
        const float titleRoom = globeNew ? w - ui.newPillWidth() - ui.s(14) : w;
        Vec2 tsz = ui.label(title, {x, y + ui.s(17)}, ui.fitFont(title, FontFace::Title, 26, titleRoom), ui.theme().accent, Align::Left,
                            FontFace::Title, VAlign::Middle);
        if (globeNew) {
            ui.drawNewPill(x + tsz.x + ui.s(14) + ui.newPillWidth(), y + ui.s(17));
            const bool pointed = ui.acceptsInput() && ui.usingPointer() && g_.app.input().mouseMovedThisFrame() && ui.mouseOver(zoneC_);
            if ((ui.mouseClicked(zoneC_) || pointed) && ui.featureSeen) ui.featureSeen("social.globe");
        }
        y += ui.s(38);
        std::string total;
        Color totalColor = ui.theme().text;
        const int players = globePlayers(), countries = static_cast<int>(globeCountries_.size());
        if (!in) {
            total = "Connectez-vous à Cosmic Online pour voir les joueurs sur le globe.";
            totalColor = ui.theme().textDim;
        } else if (!globeError_.empty()) {
            total = "Globe indisponible : " + globeError_;
            totalColor = ui.theme().warm;
        } else if (!globeLoaded_) {
            total = "Chargement des joueurs…";
            totalColor = ui.theme().textDim;
        } else if (players == 0) {
            total = "Aucun joueur visible sur le globe pour l'instant";
            totalColor = ui.theme().textDim;
        } else {
            total = playersText(players) + " dans " + std::to_string(countries) + " pays";
        }
        float tfs = ui.fitFont(total, FontFace::Bold, 19, w);
        ui.label(ui.elide(total, FontFace::Bold, tfs, w), {x, y + ui.s(12)}, tfs, totalColor, Align::Left, FontFace::Bold, VAlign::Middle);
        y += ui.s(28);
        const bool offline = globeCache_ && globeError_.empty() && in;
        std::string sub = offline ? "Hors ligne · données " + OnlineUi::ageText(globeAge_) : "Choisissez une discussion à gauche, ou « Écrire » à un ami.";
        float sfs = ui.fitFont(sub, FontFace::Body, 15, w);
        ui.label(ui.elide(sub, FontFace::Body, sfs, w), {x, y + ui.s(10)}, sfs, offline ? ui.theme().warm : ui.theme().textDim, Align::Left,
                 FontFace::Body, VAlign::Middle);
        y += ui.s(28);

        // Footer, from the bottom up: messages of the screen, privacy note, country card.
        float bottom = zoneC_.bottom() - ui.s(10);
        Rect statusRow{x, bottom - ui.s(26), w, ui.s(26)};
        bottom = statusRow.y - ui.s(2);
        static const char* kNote =
            "Positions approximatives : centre du pays, jamais plus précis. Seuls sont comptés les joueurs qui ont choisi d'apparaître "
            "sur le globe avec un profil public ; aucun nom n'est transmis.";
        float noteH = ui.text().measureWrapped(kNote, FontFace::Body, ui.s(13), w).y;
        Rect note{x, bottom - noteH, w, noteH};
        bottom = note.y - ui.s(10);
        globeInfo_ = {x, bottom - ui.s(64), w, ui.s(64)};
        bottom = globeInfo_.y - ui.s(10);
        const float bw = ui.s(44), gap = ui.s(6);
        globeButtons_ = {zoneC_.right() - pad - bw, y, bw, bw * 5 + gap * 4};
        globeArea_ = {zoneC_.x + ui.s(4), y, globeButtons_.x - ui.s(8) - (zoneC_.x + ui.s(4)), std::max(ui.s(40), bottom - y)};
        globeView_.center = globeArea_.center();
        globeView_.baseRadius = std::min(globeArea_.w, globeArea_.h) * 0.46f;

        // The Earth and the points (clipped to their area).
        r.pushClip(globeArea_);
        GeoPoint sun = subsolarPoint(std::chrono::system_clock::now());
        globe_.renderBackdrop(r, globeView_, globeCam_, t_);
        globe_.renderEarth(r, globeView_, globeCam_, sun);
        globe_.renderAtmosphere(r, globeView_, globeCam_, sun, t_);
        globePoints_ = in && globeError_.empty() ? projectCountries(globeCountries_, globeCam_, globeView_) : std::vector<GlobeCountryPoint>{};
        const float ms = markerScale(ui.scale(), globeCam_.zoom);
        const std::string mine = myGlobeCountry();
        std::vector<Rect> taken = {globeButtons_};
        std::vector<float> radii(globePoints_.size(), 0.0f);
        for (size_t k = 0; k < globePoints_.size(); ++k) {
            const GlobeCountryPoint& pt = globePoints_[k];
            const GlobeCountryCount& c = globeCountries_[static_cast<size_t>(pt.index)];
            bool hot = c.code == globeHover_ || c.code == globeFocus_;
            float scale = ms * (1.0f + std::min(0.7f, 0.16f * std::log2(static_cast<float>(c.players))));
            if (hot) scale *= 1.2f;
            float pulse = std::sin(t_ * 2.2f + static_cast<float>(pt.index) * 0.9f) * 0.5f + 0.5f;
            // Room taken by the point: its dot and the rings of « your country » / of the choice.
            radii[k] = globe_.renderMarker(r, pt.screen, ui.theme().accent, scale, pulse, hot, c.code == mine, pt.fade) +
                       (hot ? 11.0f : c.code == mine ? 5.0f : 0.0f) * scale;
            float rr = radii[k] + ui.s(2);
            taken.push_back({pt.screen.x - rr, pt.screen.y - rr, rr * 2, rr * 2});
        }
        // Number of players beside each point (measured; the biggest countries first; a
        // number without free room stays in the card below).
        std::vector<size_t> bySize(globePoints_.size());
        for (size_t k = 0; k < bySize.size(); ++k) bySize[k] = k;
        std::stable_sort(bySize.begin(), bySize.end(), [&](size_t a, size_t b) {
            return globeCountries_[static_cast<size_t>(globePoints_[a].index)].players > globeCountries_[static_cast<size_t>(globePoints_[b].index)].players;
        });
        Rect pillZone{globeArea_.x + ui.s(4), globeArea_.y + ui.s(4), globeArea_.w - ui.s(8), globeArea_.h - ui.s(8)};
        for (size_t k : bySize) {
            const GlobeCountryPoint& pt = globePoints_[k];
            GlobeRenderer::renderCount(ui, pt.screen, radii[k], globeCountries_[static_cast<size_t>(pt.index)].players, ui.theme().accent, pt.fade,
                                       pillZone, &taken);
        }
        r.popClip();

        drawGlobeButtons();
        drawGlobeCard(mine);
        ui.labelWrapped(kNote, {note.x, note.y}, note.w, 13, ui.theme().textDim.withAlphaF(0.85f));
        drawStatus(statusRow);
    }

    void drawGlobeButtons() {
        UIContext& ui = g_.ui;
        UIContext::ButtonStyle st;
        st.small = true;
        const float bw = globeButtons_.w, gap = ui.s(6);
        float y = globeButtons_.y;
        ui.pushId("globe");
        // The globe controls focused with the keyboard or the gamepad: the novelty is seen.
        if (ui.featureSeen && ui.isNew("social.globe") &&
            (ui.focused("zoomin") || ui.focused("zoomout") || ui.focused("prev") || ui.focused("next") || ui.focused("home")))
            ui.featureSeen("social.globe");
        const int n = static_cast<int>(globeCountries_.size());
        if (ui.button("zoomin", {globeButtons_.x, y, bw, bw}, "+", st)) {
            globeCam_.zoomBy(1.3);
            globeIdle_ = 0.0f;
        }
        y += bw + gap;
        if (ui.button("zoomout", {globeButtons_.x, y, bw, bw}, "−", st)) {
            globeCam_.zoomBy(1.0 / 1.3);
            globeIdle_ = 0.0f;
        }
        y += bw + gap;
        UIContext::ButtonStyle nav = st;
        nav.enabled = n > 0;
        int cur = globeIndexOf(globeFocus_);
        if (ui.button("prev", {globeButtons_.x, y, bw, bw}, "«", nav)) focusCountry(cur < 0 ? n - 1 : (cur - 1 + n) % n);
        y += bw + gap;
        if (ui.button("next", {globeButtons_.x, y, bw, bw}, "»", nav)) focusCountry(cur < 0 ? 0 : (cur + 1) % n);
        y += bw + gap;
        if (ui.button("home", {globeButtons_.x, y, bw, bw}, "", st)) {
            globeFocus_.clear();
            globeCam_.flyTo({25.0, globeCam_.center.lon}, 1.0, 0.8f);
        }
        // « Recentrer » icon: a small target.
        Vec2 c{globeButtons_.x + bw * 0.5f, y + bw * 0.5f};
        ui.r().drawRing(c, ui.s(9), ui.s(2), ui.theme().text);
        ui.r().fillCircle(c, ui.s(3), ui.theme().text);
        ui.popId();
    }

    /// The hovered or chosen country: flag, name, number of players. Without one, what to do
    /// (and, when the player is not counted, how to appear: their own choice).
    void drawGlobeCard(const std::string& mine) {
        UIContext& ui = g_.ui;
        const Rect& b = globeInfo_;
        ui.r().fillRoundedRect(b, ui.s(10), Color{10, 16, 34, 225});
        ui.r().drawRoundedRect(b, ui.s(10), ui.theme().border, ui.s(1.2f));
        const bool in = g_.online.loggedIn();
        const std::string code = !globeHover_.empty() ? globeHover_ : globeFocus_;
        float x = b.x + ui.s(14);
        if (!code.empty() && in && globeError_.empty()) {
            ui_.drawFlag(code, {x, b.center().y - ui.s(13), ui.s(34), ui.s(26)});
            x += ui.s(46);
            int i = globeIndexOf(code);
            int n = i >= 0 ? globeCountries_[static_cast<size_t>(i)].players : 0;
            std::string line = n > 0 ? playersText(n) : std::string("Aucun joueur visible sur le globe");
            if (code == mine) line += " · votre pays";
            const float room = b.right() - ui.s(14) - x;
            std::string name = ui_.countryName(code);
            ui.label(ui.elide(name, FontFace::Bold, 20, room), {x, b.y + ui.s(22)}, 20, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            ui.label(ui.elide(line, FontFace::Body, 16, room), {x, b.y + ui.s(45)}, 16, n > 0 ? ui.theme().accent : ui.theme().textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
            return;
        }
        // Not counted: say why, with the way to change it.
        const bool counted = !mine.empty() || !in || g_.online.profile().is_null();
        float right = b.right() - ui.s(12);
        if (!counted) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = 16;
            const std::string label = "Paramètres en ligne";
            float bw = ui.text().measure(label, FontFace::Bold, ui.s(16)).x + ui.s(34);
            if (ui.button("globe/settings", {right - bw, b.center().y - ui.s(18), bw, ui.s(36)}, label, st)) g_.app.pushScene(makeOnlineSettings(g_, 0));
            right -= bw + ui.s(12);
        }
        std::string t = !in       ? "Le globe montre les joueurs par pays une fois connecté."
                        : counted ? "Survolez un point, ou parcourez les pays avec les boutons à droite du globe, pour voir leur nombre de joueurs."
                                  : "Vous n'êtes pas compté : choisissez « Classements et globe » et un profil public pour apparaître.";
        const float room = right - x;
        float fs = 15.0f;
        Vec2 sz = ui.text().measureWrapped(t, FontFace::Body, ui.s(fs), room);
        if (sz.y > b.h - ui.s(10)) {
            fs = 13.0f;  // two short lines at most at small sizes
            sz = ui.text().measureWrapped(t, FontFace::Body, ui.s(fs), room);
        }
        ui.labelWrapped(t, {x, b.center().y - sz.y * 0.5f}, room, fs, ui.theme().textDim);
    }

    void drawStatus(const Rect& r) {
        UIContext& ui = g_.ui;
        if (status_.empty() || ui.time() - statusT_ > 6.0f) return;
        ui.label(status_, {r.x, r.center().y}, ui.fitFont(status_, FontFace::Body, 15, r.w), statusError_ ? ui.theme().danger : ui.theme().success,
                 Align::Left, FontFace::Body, VAlign::Middle);
    }

    void drawMessages(const Rect& view, bool group) {
        UIContext& ui = g_.ui;
        if (!messagesLoaded_) {
            ui.label("Chargement…", view.center(), 17, ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);
            return;
        }
        if (messages_.empty()) {
            ui.label("Aucun message. Écrivez le premier !", view.center(), 17, ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);
            return;
        }
        // Layout first (heights), then drawing inside the scroll region.
        const float maxW = std::min(view.w * 0.72f, ui.s(620));
        const float pad = ui.s(12), fs = 17;
        std::vector<Bubble> bubbles;
        float y = hasMore_ ? ui.s(52) : ui.s(8);
        std::string prevSender;
        for (size_t i = 0; i < messages_.size(); ++i) {
            const Json& m = messages_[i];
            bool mine = jget<bool>(m, "mine", false);
            std::string sender = jstr(m["sender"], "id");
            bool showName = group && !mine && sender != prevSender;
            prevSender = sender;
            bool deleted = jget<bool>(m, "deleted", false);
            std::string body = deleted ? "Message supprimé" : jstr(m, "body");
            // A message is shown as its author wrote it (only « Message supprimé » is translated).
            Vec2 sz = [&] {
                i18n::Verbatim playerWords(!deleted);
                return ui.text().measureWrapped(body, FontFace::Body, ui.s(fs), maxW - 2 * pad);
            }();
            float h = sz.y + 2 * pad - ui.s(4) + ui.s(18);  // text + time line
            float w = std::max(sz.x + 2 * pad, ui.s(90));
            if (showName) y += ui.s(20);
            bubbles.push_back({i, y, h, w, mine, showName});
            y += h + ui.s(8);
            if (jstr(m, "id") == selectedMsg_) y += ui.s(42);
        }
        const float content = y + ui.s(8);
        if (stickBottom_) {
            ui.setScroll("messages", 1e9f);
            stickBottom_ = false;
        }
        ui.beginScroll("messages", view, content);
        const float off = ui.scrollOffset();
        atBottom_ = off >= content - view.h - ui.s(30);
        const float top = view.y - off;
        if (hasMore_) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.enabled = !msgBusy_;
            if (ui.button("older", {view.center().x - ui.s(160), top + ui.s(8), ui.s(320), ui.s(36)}, "Messages précédents", st)) loadMessages(true);
        }
        for (const Bubble& b : bubbles) {
            const Json& m = messages_[b.index];
            float by = top + b.y;
            if (by > view.bottom() || by + b.h + ui.s(60) < view.y) continue;
            bool deleted = jget<bool>(m, "deleted", false);
            std::string id = jstr(m, "id");
            const Json& sender = m["sender"];
            if (b.showName)
                ui.label(jstr(sender, "username"), {view.x + ui.s(54), by - ui.s(10)}, 14, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
            float bx = b.mine ? view.right() - ui.s(18) - b.w : view.x + ui.s(54);
            if (!b.mine && group && b.showName)
                ui_.drawAvatar(jstr(sender, "avatar", "avatar_01"), jstr(sender, "id"), {view.x + ui.s(10), by, ui.s(36), ui.s(36)});
            else if (!b.mine && !group && (b.index == 0 || jget<bool>(messages_[b.index - 1], "mine", false)))
                ui_.drawAvatar(jstr(sender, "avatar", "avatar_01"), jstr(sender, "id"), {view.x + ui.s(10), by, ui.s(36), ui.s(36)});
            Rect r{bx, by, b.w, b.h};
            bool sel = id == selectedMsg_;
            Color fill = deleted ? Color{20, 26, 44, 200} : b.mine ? Color{22, 92, 98, 235} : Color{30, 40, 70, 235};
            ui.r().fillRoundedRect(r, ui.s(12), fill);
            if (sel) ui.r().drawRoundedRect(r, ui.s(12), ui.theme().accent, ui.s(1.5f));
            if (!deleted && ui.inputEnabled && ui.mouseClicked(r)) selectedMsg_ = sel ? std::string{} : id;
            const float pad = ui.s(12);
            {
                i18n::Verbatim playerWords(!deleted);
                ui.labelWrapped(deleted ? "Message supprimé" : jstr(m, "body"), {r.x + pad, r.y + pad - ui.s(2)}, b.w - 2 * pad + ui.s(2), 17,
                                deleted ? ui.theme().textDim : ui.theme().text);
            }
            ui.label(clockText(jstr(m, "created_at")), {r.right() - pad, r.bottom() - ui.s(12)}, 13, ui.theme().textDim.withAlphaF(0.9f), Align::Right,
                     FontFace::Body, VAlign::Middle);
            if (sel) {
                UIContext::ButtonStyle st;
                st.small = true;
                Rect a{b.mine ? r.right() - ui.s(150) : r.x, r.bottom() + ui.s(6), ui.s(150), ui.s(34)};
                if (b.mine) {
                    st.danger = true;
                    if (ui.button("del" + id, a, "Supprimer", st))
                        g_.online.send("DELETE", "/api/v1/social/messages/" + id, Json(), OnlineService::guard(life_, [this](const ApiResult& r2) {
                            say(r2.ok ? "Message supprimé." : r2.message, !r2.ok);
                            selectedMsg_.clear();
                            loadMessages(false);
                        }));
                } else if (ui.button("rep" + id, a, "Signaler", st)) {
                    confirm("Signaler ce message de " + jstr(sender, "username") + " aux modérateurs ?", "Signaler", [this, id] {
                        static const char* reasons[] = {"Insulte ou harcèlement", "Spam", "Contenu inapproprié", "Autre"};
                        Json body = Json::object();
                        if (reportReason_ >= 0) body["reason"] = reasons[reportReason_];
                        g_.online.send("POST", "/api/v1/social/messages/" + id + "/report", body, OnlineService::guard(life_, [this](const ApiResult& r2) {
                            say(r2.ok ? "Message signalé. Merci." : r2.message, !r2.ok);
                            selectedMsg_.clear();
                        }));
                    });
                    reportReason_ = 0;
                }
            }
        }
        ui.endScroll();
        if (msgCache_)
            ui.label("Hors ligne : messages en cache", {view.right() - ui.s(10), view.y + ui.s(12)}, 13, ui.theme().warm, Align::Right, FontFace::Body,
                     VAlign::Middle);
    }

    // --------------------------------------------------------------- modals
    void drawModal() {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 170});
        if (modal_ == Modal::NewGroup) drawNewGroup();
        else if (modal_ == Modal::Members) drawMembers();
        else drawConfirm();
    }

    Rect modalBox(float w, float h) {
        UIContext& ui = g_.ui;
        Rect box{ui.screenW() * 0.5f - ui.s(w) * 0.5f, ui.screenH() * 0.5f - ui.s(h) * 0.5f, ui.s(w), ui.s(h)};
        ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
        ui.r().drawRoundedRect(box, ui.s(18), ui.theme().accent.withAlphaF(0.55f), ui.s(1.5f));
        return box;
    }

    void drawNewGroup() {
        UIContext& ui = g_.ui;
        Rect box = modalBox(640, 640);
        ui.pushId("social/newgroup");
        ui.label("NOUVEAU GROUPE", {box.x + ui.s(30), box.y + ui.s(40)}, 26, ui.theme().accent, Align::Left, FontFace::Title, VAlign::Middle);
        ui.textField("name", {box.x + ui.s(30), box.y + ui.s(76), box.w - ui.s(60), ui.s(44)}, groupName_, "Nom du groupe (40 caractères au plus)", 40);
        ui.label("Membres (vos amis, 19 au plus)", {box.x + ui.s(30), box.y + ui.s(146)}, 16, ui.theme().textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
        Rect view{box.x + ui.s(30), box.y + ui.s(164), box.w - ui.s(60), box.h - ui.s(250)};
        Json list = friends_.value("friends", Json::array());
        ui.beginScroll("pick", view, list.size() * ui.s(48));
        float y = view.y - ui.scrollOffset();
        for (const auto& f : list) {
            const Json& pl = f["player"];
            std::string pid = jstr(pl, "id");
            bool on = std::find(groupPick_.begin(), groupPick_.end(), pid) != groupPick_.end();
            bool was = on;
            ui.toggle("f" + pid, {view.x, y, view.w - ui.s(14), ui.s(42)}, "      " + jstr(pl, "username"), on);
            ui_.drawAvatar(jstr(pl, "avatar", "avatar_01"), pid, {view.x + ui.s(10), y + ui.s(6), ui.s(30), ui.s(30)});
            if (on != was) {
                if (on && groupPick_.size() < 19) groupPick_.push_back(pid);
                else groupPick_.erase(std::remove(groupPick_.begin(), groupPick_.end(), pid), groupPick_.end());
            }
            y += ui.s(48);
        }
        ui.endScroll();
        UIContext::ButtonStyle p;
        p.primary = true;
        p.enabled = !groupName_.empty() && !groupPick_.empty() && !creating_;
        float by = box.bottom() - ui.s(70);
        if (ui.button("create", {box.x + ui.s(30), by, ui.s(300), ui.s(46)},
                      "Créer le groupe (" + std::to_string(groupPick_.size() + 1) + ")", p)) {
            creating_ = true;
            Json ids = Json::array();
            for (const auto& id : groupPick_) ids.push_back(id);
            g_.online.send("POST", "/api/v1/social/groups", {{"name", groupName_}, {"member_ids", ids}},
                           OnlineService::guard(life_, [this](const ApiResult& r) {
                               creating_ = false;
                               if (!r.ok) return say(r.message, true);
                               modal_ = Modal::None;
                               const Json& c = r.data["conversation"];
                               convs_.insert(convs_.begin(), c);
                               tab_ = 0;
                               openConversation(jstr(c, "id"));
                               say("Groupe créé.", false);
                           }));
        }
        if (ui.button("cancel", {box.right() - ui.s(210), by, ui.s(180), ui.s(46)}, "Annuler") || ui.backPressed()) modal_ = Modal::None;
        drawStatus({box.x + ui.s(30), by - ui.s(34), box.w - ui.s(60), ui.s(28)});
        ui.popId();
    }

    void drawMembers() {
        UIContext& ui = g_.ui;
        Rect box = modalBox(680, 680);
        ui.pushId("social/members");
        const std::string me = g_.online.playerId();
        bool owner = false;
        Json members = current_.value("members", Json::array());
        for (const auto& m : members)
            if (jstr(m, "id") == me && jstr(m, "role") == "owner") owner = true;
        ui.label("MEMBRES DU GROUPE", {box.x + ui.s(30), box.y + ui.s(40)}, 26, ui.theme().accent, Align::Left, FontFace::Title, VAlign::Middle);
        float y0 = box.y + ui.s(76);
        if (owner) {
            ui.textField("rename", {box.x + ui.s(30), y0, box.w - ui.s(250), ui.s(42)}, renameText_, "Nom du groupe", 40);
            UIContext::ButtonStyle st;
            st.small = true;
            st.enabled = !renameText_.empty() && renameText_ != jstr(current_, "name");
            if (ui.button("renameok", {box.right() - ui.s(210), y0, ui.s(180), ui.s(42)}, "Renommer", st))
                g_.online.send("PATCH", "/api/v1/social/groups/" + convId_, {{"name", renameText_}}, OnlineService::guard(life_, [this](const ApiResult& r) {
                    say(r.ok ? "Groupe renommé." : r.message, !r.ok);
                    if (r.ok) {
                        current_["name"] = jstr(r.data, "name");
                        loadConversations();
                    }
                }));
            y0 += ui.s(56);
        }
        Rect view{box.x + ui.s(30), y0, box.w - ui.s(60), box.bottom() - ui.s(owner ? 190 : 130) - y0};
        ui.beginScroll("list", view, members.size() * ui.s(50));
        float y = view.y - ui.scrollOffset();
        for (const auto& m : members) {
            std::string pid = jstr(m, "id");
            Rect row{view.x, y, view.w - ui.s(14), ui.s(44)};
            ui.r().fillRoundedRect(row, ui.s(6), Color{16, 24, 46, 200});
            ui_.drawAvatar(jstr(m, "avatar", "avatar_01"), pid, {row.x + ui.s(5), row.y + ui.s(4), ui.s(36), ui.s(36)});
            std::string name = jstr(m, "username") + (pid == me ? " (vous)" : "");
            ui.label(name, {row.x + ui.s(50), row.center().y}, 17, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            if (jstr(m, "role") == "owner")
                ui.label("créateur", {row.right() - ui.s(owner && pid != me ? 140 : 12), row.center().y}, 14, ui.theme().warm, Align::Right, FontFace::Body,
                         VAlign::Middle);
            if (owner && pid != me) {
                UIContext::ButtonStyle st;
                st.small = true;
                if (ui.button("rm" + pid, {row.right() - ui.s(120), row.y + ui.s(5), ui.s(114), ui.s(34)}, "Retirer", st))
                    g_.online.send("DELETE", "/api/v1/social/groups/" + convId_ + "/members/" + pid, Json(),
                                   OnlineService::guard(life_, [this, pid](const ApiResult& r) {
                                       say(r.ok ? "Membre retiré." : r.message, !r.ok);
                                       if (!r.ok) return;
                                       Json kept = Json::array();
                                       for (const auto& x : current_.value("members", Json::array()))
                                           if (jstr(x, "id") != pid) kept.push_back(x);
                                       current_["members"] = kept;
                                       loadConversations();
                                   }));
            }
            y += ui.s(50);
        }
        ui.endScroll();
        float by = box.bottom() - ui.s(70);
        if (owner) {
            // Friends not in the group yet.
            std::vector<std::string> ids, names;
            for (const auto& f : friends_.value("friends", Json::array())) {
                std::string pid = jstr(f["player"], "id");
                bool in = false;
                for (const auto& m : members) in = in || jstr(m, "id") == pid;
                if (!in) {
                    ids.push_back(pid);
                    names.push_back(jstr(f["player"], "username"));
                }
            }
            float ay = by - ui.s(58);
            if (ids.empty()) {
                ui.label("Tous vos amis sont déjà dans le groupe.", {box.x + ui.s(30), ay + ui.s(21)}, 15, ui.theme().textDim, Align::Left, FontFace::Body,
                         VAlign::Middle);
            } else {
                addPick_ = std::clamp(addPick_, 0, static_cast<int>(ids.size()) - 1);
                ui.cycle("addpick", {box.x + ui.s(30), ay, box.w - ui.s(250), ui.s(42)}, "Ajouter", addPick_, names);
                UIContext::ButtonStyle st;
                st.small = true;
                st.primary = true;
                st.enabled = members.size() < 20;
                std::string pid = ids[static_cast<size_t>(addPick_)];
                if (ui.button("addok", {box.right() - ui.s(210), ay, ui.s(180), ui.s(42)}, "Ajouter", st))
                    g_.online.send("POST", "/api/v1/social/groups/" + convId_ + "/members", {{"player_id", pid}},
                                   OnlineService::guard(life_, [this](const ApiResult& r) {
                                       say(r.ok ? "Ami ajouté au groupe." : r.message, !r.ok);
                                       if (r.ok) {
                                           current_ = r.data["conversation"];
                                           loadConversations();
                                       }
                                   }));
            }
        }
        UIContext::ButtonStyle d;
        d.danger = true;
        if (ui.button("leave", {box.x + ui.s(30), by, ui.s(240), ui.s(46)}, "Quitter le groupe", d)) {
            std::string id = convId_;
            confirm(owner ? "Quitter le groupe ? Le membre le plus ancien en devient le créateur ; le groupe disparaît s'il n'y a plus personne."
                          : "Quitter le groupe ? Vous ne verrez plus ses messages.",
                    "Quitter", [this, id, me] {
                        g_.online.send("DELETE", "/api/v1/social/groups/" + id + "/members/" + me, Json(), OnlineService::guard(life_, [this](const ApiResult& r) {
                            say(r.ok ? "Vous avez quitté le groupe." : r.message, !r.ok);
                            if (!r.ok) return;
                            closeConversation();
                            loadConversations();
                            g_.online.refreshUnread();
                        }));
                    });
        }
        if (modal_ == Modal::Members &&
            (ui.button("close", {box.right() - ui.s(210), by, ui.s(180), ui.s(46)}, "Fermer") || ui.backPressed()))
            modal_ = Modal::None;
        drawStatus({box.x + ui.s(30), box.bottom() - ui.s(16) - ui.s(14), box.w - ui.s(60), ui.s(20)});
        ui.popId();
    }

    void drawConfirm() {
        UIContext& ui = g_.ui;
        bool report = reportReason_ >= 0;
        Rect box = modalBox(620, report ? 330 : 280);
        ui.pushId("social/confirm");
        ui.labelWrapped(confirmText_, {box.x + ui.s(30), box.y + ui.s(30)}, box.w - ui.s(60), 18, ui.theme().text);
        if (report)
            ui.cycle("reason", {box.x + ui.s(30), box.bottom() - ui.s(128), box.w - ui.s(60), ui.s(42)}, "Motif", reportReason_,
                     {"Insulte ou harcèlement", "Spam", "Contenu inapproprié", "Autre"});
        UIContext::ButtonStyle st;
        st.danger = confirmDanger_;
        st.primary = !confirmDanger_;
        float by = box.bottom() - ui.s(70);
        if (ui.button("ok", {box.x + ui.s(30), by, ui.s(240), ui.s(46)}, confirmAction_, st)) {
            modal_ = Modal::None;
            if (confirmFn_) confirmFn_();
        } else if (ui.button("cancel", {box.right() - ui.s(210), by, ui.s(180), ui.s(46)}, "Annuler") || ui.backPressed()) {
            modal_ = Modal::None;
        }
        ui.popId();
    }

    GameServices& g_;
    OnlineUi ui_;
    MenuBackground bg_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string wantedConversation_;
    std::string openModal_;
    int tab_ = 0;
    bool accountAsked_ = false;
    Rect header_, zoneA_, zoneL_, zoneC_;

    Json convs_ = Json::array();
    bool convBusy_ = false, convLoaded_ = false, convCache_ = false;
    float convTimer_ = 0.0f;
    Json friends_ = Json::object();
    bool friendsLoaded_ = false;
    float friendsTimer_ = 0.0f;
    std::string watchName_;   // --online-open watch:<pseudonym>
    float hintSince_ = -1.0f;  // since when the hint of « Regarder » is shown
    Json blocks_ = Json::array();
    std::string expanded_, addName_;

    std::string convId_;
    Json current_ = Json::object();
    Json messages_ = Json::array();
    bool hasMore_ = false, msgBusy_ = false, messagesLoaded_ = false, msgCache_ = false;
    float msgTimer_ = 0.0f;
    bool stickBottom_ = false, atBottom_ = true;
    std::string selectedMsg_, draft_;
    bool sending_ = false;

    Modal modal_ = Modal::None;
    std::string groupName_;
    std::vector<std::string> groupPick_;
    bool creating_ = false;
    std::string renameText_;
    int addPick_ = 0;
    std::string confirmText_, confirmAction_;
    std::function<void()> confirmFn_;
    bool confirmDanger_ = true;
    int reportReason_ = -1;

    std::string status_;
    bool statusError_ = false;
    float statusT_ = 0.0f;

    int unreadSeen_ = -1;
    float t_ = 0.0f;
    // Globe of the players (GET /globe/players): counts per country, never a person.
    GlobeRenderer globe_;
    GlobeCamera globeCam_;
    GlobeView globeView_;
    GlobeRenderer::Input globeInput_;
    Rect globeArea_, globeButtons_, globeInfo_;
    std::vector<GlobeCountryCount> globeCountries_;
    std::vector<GlobeCountryPoint> globePoints_;
    std::string globeHover_, globeFocus_, globeError_;
    bool globeLoading_ = false, globeLoaded_ = false, globeCache_ = false;
    double globeAge_ = 0.0;
    float globeTimer_ = 0.0f, globeIdle_ = 10.0f;
};

}  // namespace

std::unique_ptr<Scene> makeSocial(GameServices& g, const std::string& conversationId, int tab) {
    return std::make_unique<SocialScene>(g, conversationId, tab);
}

}  // namespace cosmic
