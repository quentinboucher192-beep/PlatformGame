// Profile of a player (Cosmic Online), pushed over the current screen: identity (frame, title
// and, 2.41, the badges chosen in the Collection), records, time played (total and per level,
// in hours and minutes), creator card (computed by the server from the published levels),
// creations, followers and followed creators, and the actions FOLLOW / friend / block; 2.62.5:
// the tab « Clan » (the sheet of the player's clan, and « Inviter dans mon clan » for the chief
// and the officers of a clan). Every number comes from the server.

#include <algorithm>
#include <cmath>
#include <map>

#include "engine/core/I18n.h"
#include "game/online/Clans.h"
#include "game/GameServices.h"
#include "game/online/Replay.h"
#include "game/online/OnlineGlobe.h"
#include "game/progression/Progression.h"
#include "ui/online/GlobeRenderer.h"
#include "ui/online/ClanWidgets.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/ChallengeRewardsUi.h"
#include "ui/screens/CosmeticFx.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/ProfileBadgesUi.h"
#include "ui/screens/ReplayDownloadUi.h"
#include "ui/screens/Thumbnail.h"

namespace cosmic {

namespace {

std::string decimal(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", v);
    std::string t = b;
    if (auto p = t.find('.'); p != std::string::npos) t[p] = ',';
    return t;
}

std::string dateFr(const std::string& iso) {
    if (iso.size() < 10) return "—";
    return iso.substr(8, 2) + "/" + iso.substr(5, 2) + "/" + iso.substr(0, 4);
}

std::string runTime(int64_t ms) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%02d.%03d", static_cast<int>(ms / 60000), static_cast<int>(ms / 1000 % 60), static_cast<int>(ms % 1000));
    return buf;
}

const char* kCreationSorts[] = {"popular", "recent", "top"};

/// Never below 11.5 px on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

class PlayerProfileScene : public Scene {
public:
    PlayerProfileScene(GameServices& g, std::string playerId) : g_(g), ui_(g), playerId_(std::move(playerId)) {}

    bool isOverlay() const override { return true; }

    void onEnter() override {
        g_.ui.setFocus("profile/close");
        if (g_.onlineOpen.rfind("profile-tab:", 0) == 0) {  // automated screenshots
            tab_ = std::clamp(std::atoi(g_.onlineOpen.c_str() + 12), 0, kTabClan);
            g_.onlineOpen.clear();
        }
        load();
    }
    void onResume() override { load(); }
    void update(float) override {
        // One's own time played just reached the server: show it.
        double pending = g_.online.pendingPlaytime();
        if (me() && lastPending_ >= 1.0 && pending < 1.0) load();
        lastPending_ = pending;
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 10, 200});
        float W = std::min(ui.screenW() - ui.s(60), ui.s(1500)), H = std::min(ui.screenH() - ui.s(60), ui.s(980));
        box_ = {(ui.screenW() - W) * 0.5f, (ui.screenH() - H) * 0.5f, W, H};
        ui.r().fillRoundedRect(box_, ui.s(18), Color{9, 14, 32, 248});
        ui.r().drawRoundedRect(box_, ui.s(18), ui.theme().accent.withAlphaF(0.55f), ui.s(1.5f));
        ui.pushId("profile");
        ui.inputEnabled = !confirmBlock_;
        UIContext::ButtonStyle cs;
        cs.small = true;
        Rect close{box_.right() - ui.s(170), box_.y + ui.s(20), ui.s(150), ui.s(42)};
        bool leave = ui.button("close", close, "Fermer", cs);
        if (loading_ && !data_.is_object()) {
            ui.label("Chargement du profil…", box_.center(), 20, ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);
        } else if (!data_.is_object()) {
            ui.labelWrapped(error_.empty() ? "Profil indisponible." : error_, {box_.x + ui.s(40), box_.center().y}, box_.w - ui.s(80), 18,
                            ui.theme().danger);
        } else {
            drawProfile(close);
        }
        ReplayDownloadUi::drawTip(g_);  // the download icons' tooltip, over the rest
        ui.inputEnabled = true;
        if (confirmBlock_) drawConfirmBlock();
        else if (leave || (ui.backPressed() && !ui.editingText())) g_.app.popScene();
        ui.popId();
        ui.end();
    }

private:
    bool me() const { return playerId_ == g_.online.playerId(); }

    void say(const std::string& m, bool error) {
        status_ = m;
        statusError_ = error;
        statusT_ = g_.ui.time();
    }

    // ---------------------------------------------------------------- data
    void load() {
        loading_ = true;
        int gen = ++generation_;
        g_.online.get("/api/v1/players/" + playerId_, OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                          if (gen != generation_) return;
                          loading_ = false;
                          if (!r.ok) {
                              error_ = r.message;
                              data_ = Json();
                              return;
                          }
                          error_.clear();
                          data_ = r.data;
                          fromCache_ = r.fromCache;
                          bool hidden = jget<bool>(data_.value("player", Json::object()), "hidden", false);
                          if (hidden && tab_ != kTabClan) tab_ = 0;  // a clan is public: its tab stays
                          loadTab();
                      }));
    }

    void loadTab() {
        tabData_ = Json();
        tabError_.clear();
        if (tab_ == kTabClan) return loadClanTab();
        std::string base = "/api/v1/players/" + playerId_;
        std::string path;
        if (tab_ == 0) path = base + "/creations?limit=30&sort=" + kCreationSorts[creationSort_];
        else if (tab_ == 1) path = base + "/playtime";
        else if (tab_ == 3) path = base + "/races";
        else if (tab_ == 4) path = base + "/followers?limit=60";
        else if (tab_ == 5) path = base + "/following?limit=60";
        if (path.empty()) return;  // best times: in the profile itself
        int gen = ++tabGeneration_;
        tabLoading_ = true;
        g_.online.get(path, OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                          if (gen != tabGeneration_) return;
                          tabLoading_ = false;
                          if (!r.ok) tabError_ = r.code == "FOLLOWS_PRIVATE" ? "Ce joueur garde ses listes d'abonnés et d'abonnements privées." : r.message;
                          else tabData_ = r.data;
                      }));
    }

    /// Tab « Clan » (2.62.5): the sheet of the player's clan (GET /clans/{id}, public), and, for
    /// the chief or an officer of a clan, that clan's pending requests (whether this player was
    /// already invited, or applied).
    void loadClanTab() {
        const int gen = ++tabGeneration_;
        const std::string id = jstr(jfield(data_, "clan", Json()), "id");
        tabLoading_ = !id.empty();
        if (!id.empty())
            g_.online.get("/api/v1/clans/" + id, OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                              if (gen != tabGeneration_) return;
                              tabLoading_ = false;
                              if (!r.ok) tabError_ = r.message;
                              else tabData_ = r.data;
                          }));
        loadMyClan(gen);
    }

    void loadMyClan(int gen) {
        if (!g_.online.loggedIn() || me()) return;
        g_.online.refreshMyClan(OnlineService::guard(life_, [this, gen](const ApiResult& r) {
            if (gen != tabGeneration_ || !r.ok) return;
            const Json c = jfield(r.data, "clan", Json());
            if (!c.is_object() || !clans::canManage(jstr(c, "role"))) {
                myClanPage_ = Json();
                return;
            }
            g_.online.get("/api/v1/clans/" + jstr(c, "id"), OnlineService::guard(life_, [this, gen](const ApiResult& p) {
                              if (gen == tabGeneration_ && p.ok) myClanPage_ = p.data;
                          }));
        }));
    }

    /// « Inviter dans mon clan »: the server checks the role, the room, the blocks, the clan of
    /// the player (who is always the session's chief or officer, never an id sent by the game).
    void inviteToMyClan() {
        const std::string clanId = jstr(jfield(g_.online.myClan(), "clan", Json()), "id");
        if (clanId.empty() || busy_) return;
        busy_ = true;
        const std::string name = jstr(data_["player"], "username");
        g_.online.send("POST", "/api/v1/clans/" + clanId + "/invites", {{"player_id", playerId_}}, OnlineService::guard(life_, [this, name](const ApiResult& r) {
                           busy_ = false;
                           if (!r.ok) return say(r.message, true);
                           if (jget<bool>(r.data, "joined", false)) say(name + " a rejoint votre clan.", false);
                           else if (jget<bool>(r.data, "already", false)) say("Invitation déjà envoyée à " + name + ".", false);
                           else say("Invitation envoyée à " + name + ".", false);
                           load();  // the profile (its clan, if the player joined) and this tab again
                       }));
    }

    void openOther(const std::string& id) {
        if (id.empty() || id == playerId_) return;
        playerId_ = id;
        data_ = Json();
        tab_ = 0;
        load();
    }

    TexturePtr thumbnail(const std::string& id) {
        auto it = thumbs_.find(id);
        if (it != thumbs_.end()) return it->second;
        thumbs_[id] = nullptr;
        g_.online.getBinary("/api/v1/community/levels/" + id + "/thumbnail", [this, id, life = std::weak_ptr<int>(life_)](const HttpResponse& r) {
            if (!life.lock() || r.status != 200 || r.body.size() < 8) return;
            thumbs_[id] = textureFromPng(g_, std::vector<uint8_t>(r.body.begin(), r.body.end()));
        });
        return nullptr;
    }

    // ------------------------------------------------------------- actions
    void follow(bool on, bool notify = true) {
        busy_ = true;
        auto cb = OnlineService::guard(life_, [this, on](const ApiResult& r) {
            busy_ = false;
            if (!r.ok) return say(r.message, true);
            say(on ? "Vous suivez ce créateur." : "Vous ne suivez plus ce créateur.", false);
            load();
        });
        if (on) g_.online.send("POST", "/api/v1/players/" + playerId_ + "/follow", {{"notify", notify}}, cb);
        else g_.online.send("DELETE", "/api/v1/players/" + playerId_ + "/follow", Json(), cb);
    }

    void friendAction(const std::string& state) {
        auto done = OnlineService::guard(life_, [this](const ApiResult& r) {
            say(r.ok ? "C'est fait." : r.message, !r.ok);
            if (r.ok) load();
        });
        if (state == "friends") g_.online.send("DELETE", "/api/v1/friends/" + playerId_, Json(), done);
        else if (state == "request_received") g_.online.send("POST", "/api/v1/friends/accept", {{"player_id", playerId_}}, done);
        else g_.online.send("POST", "/api/v1/friends/request", {{"player_id", playerId_}}, done);
    }

    void block(bool on) {
        auto done = OnlineService::guard(life_, [this, on](const ApiResult& r) {
            if (!r.ok) return say(r.message, true);
            say(on ? "Joueur bloqué : il ne peut plus vous écrire, vous suivre ni réagir à vos commentaires." : "Joueur débloqué.", false);
            load();
        });
        if (on) g_.online.send("POST", "/api/v1/social/blocks", {{"player_id", playerId_}}, done);
        else g_.online.send("DELETE", "/api/v1/social/blocks/" + playerId_, Json(), done);
    }

    // ------------------------------------------------------------- drawing
    void drawProfile(const Rect& close) {
        UIContext& ui = g_.ui;
        const Json& p = data_["player"];
        const Json& creator = data_.value("creator", Json::object());
        const Json& fstate = data_.value("follow", Json::object());
        const bool hidden = jget<bool>(p, "hidden", false);
        const bool isCreator = jget<bool>(creator, "is_creator", false);
        const float x = box_.x + ui.s(28);
        const float w = box_.w - ui.s(56);
        float y = box_.y + ui.s(24);
        // Identity. What the player wears (2.23): null for a profile hidden from the reader.
        const Json& worn = data_.value("cosmetics", Json());
        const bool dressed = !hidden && worn.is_object();
        const float av = ui.s(112);
        ui_.avatarsOpenProfile = false;  // this profile's own avatar: already open
        ui_.drawAvatar(jstr(p, "avatar", "avatar_01"), playerId_, {x, y, av, av});
        ui_.avatarsOpenProfile = true;
        drawAvatarFrame(ui.r(), {x + av * 0.5f, y + av * 0.5f}, av * 0.5f, dressed ? g_.cosmetics.lookOr("frame", wornLook(worn, "frame")) : "simple",
                        ui.s(1.0f), ui.time());
        // A friend's presence (2.33.1): the pill on the avatar, its word on the line under.
        const std::string presence = jstr(p, "presence");
        if (!presence.empty()) {
            float rad = ui.s(13);
            Vec2 pc{x + av - rad * 0.9f, y + av - rad * 0.9f};
            drawPresenceDot(ui, pc, rad, presence, ui.s(3));
        }
        float nx = x + av + ui.s(kAvatarFrameMargin + 14);
        // Actions on the right of the header, under "Fermer".
        std::vector<std::pair<std::string, std::string>> acts;  // key, label
        const bool logged = g_.online.loggedIn();
        if (me()) {
            acts.push_back({"edit", "Modifier mon profil"});
            acts.push_back({"visibility", jstr(p, "follows_visibility", "public") == "private" ? "Abonnements : privés" : "Abonnements : publics"});
        } else if (logged) {
            bool blocked = jget<bool>(fstate, "blocked", false);
            if (!blocked) {
                acts.push_back({"follow", jget<bool>(fstate, "following", false) ? "SUIVI" : "SUIVRE"});
                if (jget<bool>(fstate, "following", false))
                    acts.push_back({"notify", jget<bool>(fstate, "notify", false) ? "Nouveaux niveaux : notifiés" : "Nouveaux niveaux : sans alerte"});
                std::string fr = jstr(p, "friendship");
                if (!hidden)
                    acts.push_back({"friend", fr == "friends" ? "Retirer des amis" : fr == "request_sent" ? "Demande envoyée" : fr == "request_received" ? "Accepter en ami" : "Ajouter en ami"});
            }
            acts.push_back({"block", blocked ? "Débloquer" : "Bloquer"});
        }
        const float bh = ui.s(42), bgap = ui.s(10);
        float actW = ui.s(250);
        float actRight = box_.right() - ui.s(20);
        // Two columns of actions when there are more than two.
        int cols = acts.size() > 2 ? 2 : 1;
        float actX = actRight - cols * actW - (cols - 1) * bgap;
        float ay = close.bottom() + ui.s(12);
        for (size_t i = 0; i < acts.size(); ++i) {
            int col = static_cast<int>(i % static_cast<size_t>(cols)), row = static_cast<int>(i / static_cast<size_t>(cols));
            Rect r{actX + col * (actW + bgap), ay + row * (bh + bgap), actW, bh};
            const std::string& k = acts[i].first;
            UIContext::ButtonStyle st;
            st.small = true;
            st.enabled = !busy_;
            st.primary = k == "follow" && !jget<bool>(fstate, "following", false);
            st.selected = k == "follow" && jget<bool>(fstate, "following", false);
            st.danger = k == "block" && !jget<bool>(fstate, "blocked", false);
            if (k == "friend") st.enabled = jstr(p, "friendship") != "request_sent";
            if (!ui.button("act/" + k, r, acts[i].second, st)) continue;
            if (k == "edit") g_.app.pushScene(makeOnlineSettings(g_, 1));
            else if (k == "visibility") {
                bool priv = jstr(p, "follows_visibility", "public") == "private";
                g_.online.updateProfile({{"follows_visibility", priv ? "public" : "private"}}, OnlineService::guard(life_, [this](const ApiResult& r) {
                                            say(r.ok ? "Réglage enregistré." : r.message, !r.ok);
                                            if (r.ok) load();
                                        }));
            } else if (k == "follow") follow(!jget<bool>(fstate, "following", false));
            else if (k == "notify") follow(true, !jget<bool>(fstate, "notify", false));
            else if (k == "friend") friendAction(jstr(p, "friendship"));
            else if (k == "block") {
                if (jget<bool>(fstate, "blocked", false)) block(false);
                else confirmBlock_ = true;
            }
        }
        float nameRoom = actX - nx - ui.s(20);
        std::string name = jstr(p, "username") + (me() ? " (vous)" : "");
        float nfs = ui.fitFont(name, FontFace::Title, 32, nameRoom * 0.75f);
        Vec2 ns = ui.label(name, {nx, y + ui.s(24)}, nfs, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        // 2.66: the tag of the player's clan next to the name (in the room the name leaves).
        const Json clanJ = jfield(data_, "clan", Json());
        float afterName = nx + ns.x;
        if (const std::string tag = clanJ.is_object() ? clans::bracket(jstr(clanJ, "tag")) : std::string(); !tag.empty()) {
            i18n::Verbatim words;  // the clan's tag as its chief chose it
            const float room = nx + nameRoom - afterName - ui.s(14);
            if (room > ui.s(30)) {
                const float tf = ui.fitFont(tag, FontFace::Bold, 22, room, 0.7f);
                Vec2 ts = ui.label(ui.elide(tag, FontFace::Bold, tf, room), {afterName + ui.s(14), y + ui.s(26)}, tf, Color{255, 210, 122, 255}, Align::Left,
                                   FontFace::Bold, VAlign::Middle);
                // 2.62.5: the tag opens the clan's sheet (the button under the name too, and the tab « Clan »).
                bool hovered = false;
                const Rect tr{afterName + ui.s(14), y + ui.s(26) - ts.y * 0.5f, ts.x, ts.y};
                if (ui.mouseClick("clantag", tr, &hovered)) g_.app.pushScene(makeClans(g_, jstr(clanJ, "id"), true));
                if (hovered) ui.r().fillRect({tr.x, tr.bottom() - ui.s(2), tr.w, std::max(1.0f, ui.s(1.5f))}, Color{255, 210, 122, 255});
                afterName += ui.s(14) + ts.x;
            }
        }
        if (isCreator) {
            float cw = ui.text().measure("CRÉATEUR", FontFace::Bold, ui.s(14)).x + ui.s(22);
            Rect chip{afterName + ui.s(14), y + ui.s(24) - ui.s(14), cw, ui.s(28)};
            if (chip.right() <= nx + nameRoom) {
                ui.r().fillRoundedRect(chip, ui.s(14), ui.theme().warm.withAlphaF(0.22f));
                ui.r().drawRoundedRect(chip, ui.s(14), ui.theme().warm, ui.s(1.2f));
                ui.label("CRÉATEUR", chip.center(), 14, ui.theme().warm, Align::Center, FontFace::Bold, VAlign::Middle);
            }
        }
        float ly = y + ui.s(62);
        // The title under the name, as a gold chip (2.23); the badges (2.41) after it on its
        // line when they fit there, else on a line of their own. Their line starts under the
        // name as drawn (its descenders), the emblems' focus ring included.
        const std::vector<const AchievementDef*> badges = hidden ? std::vector<const AchievementDef*>{} : shownBadges();
        const float bd = ui.s(34), bgap2 = ui.s(8), badgesW = BadgeRow::width(badges.size(), bd, bgap2);
        if (!badges.empty()) ly = std::max(ly, y + ui.s(24) + ns.y * 0.5f + ui.s(6) + bd * 0.5f + ui.s(3));
        float badgesX = -1.0f, badgesY = 0.0f;
        if (std::string title = dressed ? wornName(worn, "title") : std::string(); !title.empty()) {
            float tf = fz(ui, 15), tw = std::min(ui.text().measure(title, FontFace::Bold, ui.s(tf)).x + ui.s(24), nameRoom);
            Rect chip{nx, ly - ui.s(13), tw, std::max(ui.s(26), ui.s(tf) + ui.s(8))};
            chip.y = ly - chip.h * 0.5f;
            // 2.43: a title created on Cosmic Online in its colour.
            const Json& wt = worn.value("title", Json::object());
            const std::string tcol = wt.is_object() ? jstr(wt, "color") : std::string();
            Color gold = tcol.empty() ? Color{255, 214, 90, 255} : titleColor(tcol);
            ui.r().fillRoundedRect(chip, chip.h * 0.5f, gold.withAlpha(28));
            ui.r().drawRoundedRect(chip, chip.h * 0.5f, gold.withAlpha(160), std::max(1.0f, ui.s(1.2f)));
            ui.label(ui.elide(title, FontFace::Bold, tf, tw - ui.s(24)), chip.center(), tf, gold, Align::Center, FontFace::Bold, VAlign::Middle);
            if (!badges.empty() && chip.right() + ui.s(16) + badgesW <= nx + nameRoom) {
                badgesX = chip.right() + ui.s(16);
                badgesY = ly;
            }
            ly += badgesX < 0.0f ? ui.s(34) : ui.s(38);  // the emblems are a little taller than the chip
        }
        if (!badges.empty() && badgesX < 0.0f) {
            badgesX = nx;
            badgesY = ly + ui.s(4);
            ly += ui.s(44);
        }
        // Each badge: the emblem of an achievement of the player, its name under the mouse or the focus.
        badgeRow_.draw(g_, "badge", badgesX < 0.0f ? std::vector<const AchievementDef*>{} : badges, badgesX, badgesY, bd, bgap2);
        std::string cc = jstr(p, "country_code");
        if (!cc.empty()) {
            ui_.drawFlag(cc, {nx, ly - ui.s(10), ui.s(28), ui.s(21)});
            ui.label(ui_.countryName(cc), {nx + ui.s(36), ly}, ui.fitFont(ui_.countryName(cc), FontFace::Body, 17, nameRoom - ui.s(36)), ui.theme().textDim,
                     Align::Left, FontFace::Body, VAlign::Middle);
            ly += ui.s(28);
        }
        // 2.66: the player's clan (emblem, [TAG] name, role): its page opens from here.
        if (clanJ.is_object()) {
            const std::string clanText = clans::tagged(jstr(clanJ, "name"), jstr(clanJ, "tag"));
            const std::string role = " · " + clans::roleLabel(jstr(clanJ, "role"));
            float roleW = ui.text().measure(role, FontFace::Body, ui.s(15)).x;
            float clanW;
            {
                i18n::Verbatim words;
                clanW = ui.text().measure(clanText, FontFace::Bold, ui.s(15)).x;
            }
            Rect cr{nx, ly - ui.s(16), std::min(nameRoom, ui.s(48) + clanW + roleW + ui.s(16)), ui.s(32)};
            UIContext::ButtonStyle cst;
            cst.small = true;
            cst.align = Align::Left;
            if (ui.button("clan", cr, "", cst)) g_.app.pushScene(makeClans(g_, jstr(clanJ, "id"), true));
            drawClanEmblem(ui, {cr.x + ui.s(6), cr.y + ui.s(4), ui.s(24), ui.s(24)}, clans::emblemFromJson(jfield(clanJ, "emblem", Json())));
            const float tx = cr.x + ui.s(40), room = cr.right() - ui.s(10) - tx;
            roleW = std::min(roleW, room * 0.4f);
            Vec2 cs;
            {
                i18n::Verbatim words;  // the clan's name as its chief wrote it
                const float f = ui.fitFont(clanText, FontFace::Bold, 15, room - roleW, 0.75f);
                cs = ui.label(ui.elide(clanText, FontFace::Bold, f, room - roleW), {tx, cr.center().y}, f, Color{255, 210, 122, 255}, Align::Left,
                              FontFace::Bold, VAlign::Middle);
            }
            const float rf = ui.fitFont(role, FontFace::Body, 15, roleW, 0.75f);
            ui.label(ui.elide(role, FontFace::Body, rf, roleW), {tx + cs.x, cr.center().y}, rf, clanRoleColor(ui, jstr(clanJ, "role")), Align::Left,
                     FontFace::Body, VAlign::Middle);
            ly += ui.s(40);
        }
        std::string sub;
        if (!hidden && p.contains("created_at")) sub = "Inscrit le " + dateFr(jstr(p, "created_at"));
        if (jget<bool>(fstate, "followed_by", false)) sub += (sub.empty() ? "" : "  ·  ") + std::string("vous suit");
        if (!presence.empty() || !sub.empty()) {
            float sx = nx;
            if (!presence.empty()) {
                const std::string word = presenceLabel(presence, true);
                Vec2 ws = ui.label(word, {sx, ly}, 15, presenceColor(presence), Align::Left, FontFace::Bold, VAlign::Middle);
                sx += ws.x + ui.s(14);
            }
            if (!sub.empty())
                ui.label(sub, {sx, ly}, ui.fitFont(sub, FontFace::Body, 15, nx + nameRoom - sx), ui.theme().textDim, Align::Left, FontFace::Body,
                         VAlign::Middle);
            ly += ui.s(14);
        }
        float headerBottom = std::max({y + av + ui.s(kAvatarFrameMargin), ly, ay + ((acts.size() + cols - 1) / std::max(cols, 1)) * (bh + bgap) - bgap});
        y = headerBottom + ui.s(18);
        // Statistics.
        const Json& s = data_.value("statistics", Json::object());
        const Json& pt = data_.value("playtime", Json::object());
        struct Tile {
            std::string value, key;
        };
        std::vector<Tile> tiles;
        if (!hidden) {
            tiles.push_back({playTimeText(jget<double>(pt, "total_seconds", 0.0)), "temps de jeu"});
            tiles.push_back({std::to_string(jget<int>(s, "verified_records", 0)), "records vérifiés"});
            tiles.push_back({std::to_string(jget<int>(s, "first_places", 0)), "premières places"});
            tiles.push_back({std::to_string(jget<int>(s, "levels_completed", 0)), "niveaux classés"});
        }
        tiles.push_back({std::to_string(jget<int>(creator, "followers", 0)), "abonnés"});
        tiles.push_back({std::to_string(jget<int>(creator, "following", 0)), "abonnements"});
        y = drawTiles(tiles, x, y, w) + ui.s(10);
        // Achievements' stars: unlocked in the player's game, declared to the server (not verified).
        if (const Json& ach = data_.value("achievements", Json()); !hidden && ach.is_object()) {
            Rect band{x, y, w, ui.s(46)};
            ui.r().fillRoundedRect(band, ui.s(10), Color{30, 26, 12, 220});
            ui.r().drawRoundedRect(band, ui.s(10), Color{255, 214, 90, 120}, ui.s(1.2f));
            Vec2 sc{band.x + ui.s(26), band.center().y};
            Color gold{255, 214, 90, 255};
            // A five-pointed star.
            for (int i = 0; i < 10; ++i) {
                float a0 = -1.5708f + i * 0.6283f, a1 = -1.5708f + (i + 1) * 0.6283f;
                float r0 = i % 2 == 0 ? ui.s(12) : ui.s(5.4f), r1 = i % 2 == 0 ? ui.s(5.4f) : ui.s(12);
                ui.r().fillTriangle(sc, {sc.x + std::cos(a0) * r0, sc.y + std::sin(a0) * r0}, {sc.x + std::cos(a1) * r1, sc.y + std::sin(a1) * r1}, gold);
            }
            std::string v = std::to_string(jget<int>(ach, "stars", 0)) + " / " + std::to_string(jget<int>(ach, "total_stars", 0)) + " étoiles  ·  " +
                            std::to_string(jget<int>(ach, "count", 0)) + " / " + std::to_string(jget<int>(ach, "total_count", 0)) + " succès";
            float vx = band.x + ui.s(50);
            float vf = ui.fitFont(v, FontFace::Bold, 20, w * 0.5f);
            Vec2 vs = ui.label(v, {vx, band.center().y}, vf, gold, Align::Left, FontFace::Bold, VAlign::Middle);
            std::string note = jget<int>(ach, "count", 0) > 0 ? "déclarés par le jeu de ce joueur, non vérifiés par le serveur"
                                                               : "aucun succès déclaré pour l'instant";
            float room = band.right() - (vx + vs.x + ui.s(24)) - ui.s(14);
            if (room > ui.s(80))
                ui.label(ui.elide(note, FontFace::Body, 14, room), {band.right() - ui.s(14), band.center().y}, 14, ui.theme().textDim, Align::Right,
                         FontFace::Body, VAlign::Middle);
            y += band.h + ui.s(10);
        }
        // « Tenue »: the suit (drawn), the trail, the frame and the victory effect worn (2.23).
        if (dressed) y = drawOutfit(worn, x, y, w) + ui.s(10);
        if (isCreator) {
            const Json& rt = creator.value("rating", Json::object());
            std::vector<Tile> ct = {
                {std::to_string(jget<int>(creator, "levels_published", 0)), "niveaux publiés"},
                {std::to_string(jget<int>(creator, "downloads", 0)), "téléchargements"},
                {std::to_string(jget<int>(creator, "unique_players", 0)), "joueurs uniques"},
                {std::to_string(jget<int>(creator, "favorites", 0)), "mis en favori"},
                {rt.is_object() && rt.contains("average") ? decimal(jget<double>(rt, "average", 0.0)) + "/5" : "—", "note moyenne"},
                {dateFr(jstr(creator, "first_published_at")), "premier niveau publié"},
            };
            y = drawTiles(ct, x, y, w) + ui.s(10);
        }
        if (hidden) {
            Vec2 hs = ui.labelWrapped("Ce joueur a choisi de masquer son profil public : seuls son pseudonyme, son avatar et ses niveaux publiés sont visibles.",
                                      {x, y}, w, 16, ui.theme().textDim);
            y += hs.y + ui.s(10);
        }
        // Tabs.
        // 2.62.5: « Clan » (the player's clan is public: shown even on a hidden profile).
        std::vector<std::string> labels = {"Créations", "Temps de jeu", "Meilleurs temps", "Meilleures progressions", "Abonnés", "Abonnements", "Clan"};
        std::vector<int> tabIds = {0, 1, 2, 3, 4, 5, kTabClan};
        if (hidden) {
            labels = {"Créations", "Clan"};
            tabIds = {0, kTabClan};
        }
        const int shownTab = static_cast<int>(std::find(tabIds.begin(), tabIds.end(), tab_) - tabIds.begin()) % static_cast<int>(tabIds.size());
        int tab = shownTab;
        const float tabsW = std::min(w, ui.s(230) * labels.size()), oneTab = tabsW / static_cast<float>(labels.size());
        ui.tabs("tabs", {x, y, tabsW, ui.s(42)}, labels, tab);
        if (!hidden) ui.newBadge("tabs", {x + oneTab * 3, y, oneTab, ui.s(42)}, "profile.races");  // « NOUVEAU » (2.32.2)
        ui.newBadge("tabs", {x + oneTab * static_cast<float>(labels.size() - 1), y, oneTab, ui.s(42)}, "profile.tab.clan");  // 2.62.5
        if (tab != shownTab) {
            tab_ = tabIds[static_cast<size_t>(std::clamp(tab, 0, static_cast<int>(tabIds.size()) - 1))];
            query_.clear();
            filter_ = 0;
            loadTab();
        }
        y += ui.s(54);
        if (tab_ == kTabClan) {
            drawClanTab({x, y, w, box_.bottom() - y - ui.s(44)});
            drawFooter(x, w);
            return;
        }
        // Every tab: a search field (and a filter where it means something), 2.32.2.
        {
            float sw = std::min(ui.s(420), w * 0.42f);
            ui.textField("search", {x, y, sw, ui.s(38)}, query_, "Rechercher…", 40);
            if (tab_ == 2 || tab_ == 3) {
                std::vector<std::string> opts = tab_ == 3 ? std::vector<std::string>{"Toutes", "Terminées", "En cours"} : bestCategories();
                if (filter_ >= static_cast<int>(opts.size())) filter_ = 0;
                float fw = std::min(ui.s(380), w * 0.36f);
                ui.cycle("filter", {x + sw + ui.s(14), y, fw, ui.s(38)}, tab_ == 3 ? "Progressions" : "Catégorie", filter_, opts);
            }
            y += ui.s(50);
        }
        Rect area{x, y, w, box_.bottom() - y - ui.s(44)};
        switch (tab_) {
            case 0: drawCreations(area); break;
            case 1: drawPlaytime(area); break;
            case 2: drawBestTimes(area); break;
            case 3: drawRaces(area); break;
            default: drawPlayers(area); break;
        }
        drawFooter(x, w);
    }

    /// The last answer of the server (or the offline note) at the bottom, then the badges' tooltip.
    void drawFooter(float x, float w) {
        UIContext& ui = g_.ui;
        std::string foot = fromCache_ ? "Hors ligne : profil en cache" : std::string{};
        if (!status_.empty() && ui.time() - statusT_ < 8.0f) foot = status_;
        if (!foot.empty())
            ui.label(ui.elide(foot, FontFace::Body, ui.fitFont(foot, FontFace::Body, 15, w), w), {x, box_.bottom() - ui.s(24)}, ui.fitFont(foot, FontFace::Body, 15, w),
                     statusError_ && foot == status_ ? ui.theme().danger : ui.theme().success, Align::Left, FontFace::Body, VAlign::Middle);
        // The name of the badge under the mouse or the focus, over the rest of the profile.
        badgeRow_.drawTooltip(g_);
    }

    // ------------------------------------------------------------ tab « Clan » (2.62.5)
    /// `t` at `size` (smaller down to 75 %, then shortened with « … ») in `room`; `verbatim`:
    /// a player's words (a clan's name, its motto) are never translated.
    Vec2 fitText(const std::string& t, Vec2 pos, float size, float room, Color c, FontFace face = FontFace::Body, Align a = Align::Left,
                 bool verbatim = false) {
        UIContext& ui = g_.ui;
        if (t.empty() || room <= ui.s(10)) return {};
        i18n::Verbatim v(verbatim);
        const float f = std::max(fz(ui, 11.5f), ui.fitFont(t, face, size, room, 0.75f));
        return ui.label(ui.elide(t, face, f, room), pos, f, c, a, face, VAlign::Middle);
    }

    float textWidth(const std::string& t, float size, FontFace face = FontFace::Body, bool verbatim = false) {
        i18n::Verbatim v(verbatim);
        return g_.ui.text().measure(t, face, g_.ui.s(size)).x;
    }

    /// A small pill whose left edge is `x`; its width (0 when it does not fit before `maxRight`).
    float clanPill(float x, float cy, const std::string& t, Color border, Color text, float maxRight) {
        UIContext& ui = g_.ui;
        const float w = textWidth(t, 13) + ui.s(20);
        if (x + w > maxRight) return 0.0f;
        const Rect r{x, cy - ui.s(13), w, ui.s(26)};
        ui.r().drawRoundedRect(r, ui.s(10), border, ui.s(1.2f));
        ui.label(t, r.center(), 13, text, Align::Center, FontFace::Body, VAlign::Middle);
        return w;
    }

    void drawClanTab(const Rect& area) {
        UIContext& ui = g_.ui;
        const Color gold{255, 210, 122, 255}, violet{166, 107, 255, 255}, panelC{15, 23, 48, 235};
        const Json clanJ = jfield(data_, "clan", Json());
        float y = area.y;
        // « Inviter dans mon clan » (the chief or an officer of a clan, on someone else's profile):
        // the button first, the reason (or what it does) in the room at its right.
        const bool blocked = jget<bool>(jfield(data_, "follow", Json::object()), "blocked", false);
        const clans::InviteOffer offer = g_.online.loggedIn()
                                             ? clans::inviteOffer(g_.online.myClan(), myClanPage_, clanJ, playerId_, g_.online.playerId(), blocked)
                                             : clans::InviteOffer{};
        if (offer.shown) {
            const std::string label = "Inviter dans mon clan";
            const float bw = std::min(textWidth(label, 17, FontFace::Bold) + ui.s(44), area.w * 0.45f);
            const Rect b{area.x, y, bw, ui.s(46)};
            UIContext::ButtonStyle st;
            st.primary = true;
            st.enabled = offer.enabled && !busy_;
            if (ui.button("clan/invite", b, label, st)) inviteToMyClan();
            ui.newBadge("clan/invite", b, "profile.clan.invite");  // « NOUVEAU »
            fitText(offer.note, {b.right() + ui.s(16), b.center().y}, 15, area.right() - b.right() - ui.s(16), offer.enabled ? ui.theme().textDim : ui.theme().warm);
            y += b.h + ui.s(16);
        }
        if (!clanJ.is_object()) {
            fitText(me() ? "Vous n'êtes dans aucun clan." : "Ce joueur n'est dans aucun clan.", {area.x, y + ui.s(22)}, 22, area.w, ui.theme().text, FontFace::Bold);
            static const char* kAbout =
                "Un clan réunit jusqu'à 50 joueurs autour d'un tag, d'une devise et d'un emblème ; ses membres additionnent leurs meilleurs "
                "résultats vérifiés sur les niveaux EXTRÊME. Les clans se trouvent dans COSMIC ONLINE › CLANS.";
            const Vec2 ts = ui.labelWrapped(kAbout, {area.x, y + ui.s(48)}, std::min(area.w, ui.s(900)), 16, ui.theme().textDim);
            const std::string see = "Voir les clans";
            if (ui.button("clan/all", {area.x, y + ui.s(48) + ts.y + ui.s(18), textWidth(see, 17, FontFace::Bold) + ui.s(44), ui.s(46)}, see))
                g_.app.pushScene(makeClans(g_, {}, true));
            return;
        }
        const Json c = jfield(tabData_, "clan", Json());
        const std::string id = jstr(clanJ, "id");
        if (jstr(c, "id").empty()) {
            const std::string t = tabLoading_ ? std::string("Chargement de la fiche du clan…") : !tabError_.empty() ? tabError_ : std::string("Fiche du clan indisponible.");
            fitText(t, {area.x, y + ui.s(20)}, 17, area.w, tabError_.empty() ? ui.theme().textDim : ui.theme().danger);
            return;
        }
        if (area.bottom() - y < ui.s(128)) {
            // Too little room for the sheet (a small window): its name and the button that opens it.
            const std::string open = "Voir la fiche du clan";
            const float ow = std::min(textWidth(open, 16, FontFace::Bold) + ui.s(40), area.w * 0.45f);
            const Rect ob{area.right() - ow, y, ow, ui.s(44)};
            if (ui.button("clan/open", ob, open)) g_.app.pushScene(makeClans(g_, id, true));
            fitText(clans::tagged(jstr(c, "name"), jstr(c, "tag")), {area.x, ob.center().y}, 18, ob.x - ui.s(16) - area.x, ui.theme().text, FontFace::Bold,
                    Align::Left, true);
            return;
        }
        // The sheet's head: emblem, name and [TAG], motto and foundation, pills; « Voir la fiche du
        // clan » at its right (placed first), how to join under it.
        const Rect card{area.x, y, area.w, ui.s(128)};
        ui.r().fillRoundedRectGradient(card, ui.s(14), Color{28, 20, 54, 240}, Color{15, 23, 48, 240});
        ui.r().drawRoundedRect(card, ui.s(14), violet, ui.s(1.2f));
        const float em = card.h - ui.s(32);
        drawClanEmblem(ui, {card.x + ui.s(16), card.y + ui.s(16), em, em}, clans::emblemFromJson(jfield(c, "emblem", Json())));
        const float tx = card.x + ui.s(16) + em + ui.s(18);
        const std::string open = "Voir la fiche du clan";
        const float ow = std::min(textWidth(open, 16, FontFace::Bold) + ui.s(40), card.w * 0.3f);
        const Rect ob{card.right() - ui.s(16) - ow, card.y + ui.s(16), ow, ui.s(44)};
        UIContext::ButtonStyle ost;
        ost.primary = true;
        if (ui.button("clan/open", ob, open, ost)) g_.app.pushScene(makeClans(g_, id, true));
        const Json& page = tabData_;
        const std::string role = jstr(jfield(page, "me", Json()), "role");
        std::string hint = !role.empty() ? "Vous êtes membre de ce clan : " + clans::roleLabel(role) + "."
                                         : clans::joinHint(clans::joinOffer(c, jfield(page, "me", Json()), g_.online.loggedIn(),
                                                                            !g_.online.loggedIn() || g_.online.myClan().is_object(),
                                                                            jstr(jfield(g_.online.myClan(), "clan", Json()), "id")));
        const float hintRoom = std::min(textWidth(hint, 14) + ui.s(4), (card.right() - ui.s(16) - tx) * 0.42f);
        fitText(hint, {card.right() - ui.s(16), card.y + ui.s(96)}, 14, hintRoom, ui.theme().warm, FontFace::Body, Align::Right);
        const float room = ob.x - ui.s(20) - tx;
        // Line 1: the name, then [TAG] after it.
        const std::string tag = clans::bracket(jstr(c, "tag"));
        const float tagW = std::min(textWidth(tag, 19, FontFace::Bold, true), room * 0.3f);
        const Vec2 ns = fitText(jstr(c, "name"), {tx, card.y + ui.s(32)}, 26, room - tagW - ui.s(12), ui.theme().text, FontFace::Title, Align::Left, true);
        fitText(tag, {tx + ns.x + ui.s(12), card.y + ui.s(33)}, 19, tagW, gold, FontFace::Bold, Align::Left, true);
        // Line 2: « motto » · fondé le … (the date measured first).
        const std::string founded = "Fondé le " + dateFr(jstr(c, "created_at"));
        const std::string motto = jstr(c, "motto");
        if (motto.empty()) {
            fitText(founded, {tx, card.y + ui.s(64)}, 15, room, ui.theme().textDim);
        } else {
            const float fw = std::min(textWidth(" · " + founded, 15), room * 0.4f);
            const Vec2 ms = fitText("« " + motto + " »", {tx, card.y + ui.s(64)}, 15, room - fw, Color{201, 211, 238, 255}, FontFace::Body, Align::Left, true);
            fitText(" · " + founded, {tx + ms.x, card.y + ui.s(64)}, 15, fw, ui.theme().textDim);
        }
        // Line 3: pills while they fit (left of the joining hint).
        float px = tx;
        const float pmax = card.right() - ui.s(16) - hintRoom - ui.s(16);
        auto addPill = [&](const std::string& t, Color border, Color text) {
            const float pw = clanPill(px, card.y + ui.s(98), t, border, text, pmax);
            if (pw > 0.0f) px += pw + ui.s(8);
        };
        const Color plain{34, 48, 90, 255};
        addPill(std::to_string(jget<int>(c, "members", 0)) + " membres sur " + std::to_string(jget<int>(c, "max_members", clans::kMaxMembers)), plain,
                ui.theme().text);
        addPill(clans::policyLabel(jstr(c, "join_policy")), plain, ui.theme().text);
        const int rank = jget<int>(c, "rank", 0);
        addPill(rank > 0 ? clans::ordinal(rank) + " au classement des clans" : std::string("Pas encore classé"), violet, Color{201, 179, 255, 255});
        addPill(clans::pointsLabel(jget<double>(c, "points", 0.0)), gold, gold);
        // Under the head: its members (and their role) at the left, its best EXTRÊME results at the right.
        const float top = card.bottom() + ui.s(14);
        const Rect cols{area.x, top, area.w, area.bottom() - top};
        if (cols.h < ui.s(120)) return;
        const float mw = std::floor(cols.w * 0.45f);
        const Rect mr{cols.x, cols.y, mw, cols.h};
        const Rect br{mr.right() + ui.s(14), cols.y, cols.right() - mr.right() - ui.s(14), cols.h};
        auto panelTitle = [&](const Rect& r, const std::string& t, const std::string& right, Color rc) {
            ui.r().fillRoundedRect(r, ui.s(12), panelC);
            ui.r().drawRoundedRect(r, ui.s(12), ui.theme().border, ui.s(1.2f));
            const float cy = r.y + ui.s(22), xl = r.x + ui.s(14), xr = r.right() - ui.s(14);
            const float rw = right.empty() ? 0.0f : std::min(textWidth(right, 14, FontFace::Bold) + ui.s(4), (xr - xl) * 0.45f);
            if (rw > 0.0f) fitText(right, {xr, cy}, 14, rw, rc, FontFace::Bold, Align::Right);
            fitText(t, {xl, cy}, 14, xr - xl - rw - (rw > 0.0f ? ui.s(12) : 0.0f), ui.theme().textDim, FontFace::Bold);
            return Rect{r.x + ui.s(8), r.y + ui.s(42), r.w - ui.s(16), r.h - ui.s(50)};
        };
        // Members: name (their profile opens from the row), role, best result.
        Json members = jfield(page, "members", Json::array());
        if (!members.is_array()) members = Json::array();
        Rect mv = panelTitle(mr, "MEMBRES", std::to_string(members.size()) + " / " + std::to_string(jget<int>(c, "max_members", clans::kMaxMembers)),
                             ui.theme().textDim);
        const float rowH = ui.s(42);
        std::string chosen;
        ui.beginScroll("clan/members", mv, static_cast<float>(members.size()) * (rowH + ui.s(5)));
        float ry = mv.y - ui.scrollOffset();
        for (const auto& m : members) {
            const Json p = jfield(m, "player", Json());
            const std::string pid = jstr(p, "id"), mrole = jstr(m, "role");
            const Rect rr{mv.x, ry, mv.w - ui.s(12), rowH};
            ry += rowH + ui.s(5);
            UIContext::ButtonStyle st;
            st.align = Align::Left;
            st.selected = pid == playerId_;
            if (ui.button("clan/m" + pid, rr, "", st) && pid != playerId_) chosen = pid;
            const float w = rr.w - ui.s(20), nameW = w * 0.42f, roleW = w * 0.2f, bestW = w - nameW - roleW - ui.s(16);
            const float x0 = rr.x + ui.s(10);
            fitText(jstr(p, "username"), {x0, rr.center().y}, 15, nameW - ui.s(6), pid == playerId_ ? ui.theme().accent : ui.theme().text, FontFace::Bold,
                    Align::Left, true);
            fitText(clans::roleLabel(mrole), {x0 + nameW + ui.s(8), rr.center().y}, 13, roleW - ui.s(6), clanRoleColor(ui, mrole));
            const Json best = jfield(m, "best", Json());
            const std::string b = best.is_object() ? clans::resultLabel(jget<double>(best, "score", 0.0), jget<bool>(best, "finished", false), 0) + " · " +
                                                         jstr(best, "title")
                                                   : std::string("—");
            fitText(b, {x0 + nameW + roleW + ui.s(16), rr.center().y}, 13, bestW, Color{201, 211, 238, 255});
        }
        ui.endScroll();
        // Best results: one row per EXTRÊME level (its race board opens from it).
        Json levels = jfield(page, "levels", Json::array());
        if (!levels.is_array()) levels = Json::array();
        Rect bv = panelTitle(br, "MEILLEURS RÉSULTATS EXTRÊME", clans::pointsLabel(jget<double>(c, "points", 0.0)), gold);
        if (levels.empty()) {
            fitText("Aucun niveau EXTRÊME classé pour l'instant.", {bv.x + ui.s(6), bv.y + ui.s(12)}, 15, bv.w - ui.s(12), ui.theme().textDim);
        } else {
            const float lh = ui.s(50);
            ui.beginScroll("clan/bests", bv, static_cast<float>(levels.size()) * (lh + ui.s(5)));
            float ly = bv.y - ui.scrollOffset();
            for (const auto& l : levels) {
                const Rect rr{bv.x, ly, bv.w - ui.s(12), lh};
                ly += lh + ui.s(5);
                const std::string lid = jstr(l, "level_id");
                UIContext::ButtonStyle st;
                st.align = Align::Left;
                if (ui.button("clan/l" + lid, rr, "", st)) g_.app.pushScene(makeRaceBoard(g_, "official", lid));
                const Json best = jfield(l, "best", Json());
                const float resultW = std::min(ui.s(150), rr.w * 0.35f), xr = rr.right() - ui.s(10), x0 = rr.x + ui.s(10);
                const float lroom = xr - resultW - ui.s(12) - x0;
                std::string title = jstr(l, "title");
                if (const LevelEntry* e = g_.content.level(lid)) title = e->name;
                fitText(title, {x0, rr.y + ui.s(17)}, 15, lroom, ui.theme().text, FontFace::Bold);
                if (best.is_object()) {
                    std::string team;
                    for (const auto& p : jfield(best, "players", Json::array())) team += (team.empty() ? "" : " · ") + jstr(p, "username", "?");
                    fitText(team, {x0, rr.y + ui.s(35)}, 12, lroom, ui.theme().textDim, FontFace::Body, Align::Left, true);
                    const bool fin = jget<bool>(best, "finished", false);
                    fitText(clans::resultLabel(jget<double>(best, "score", 0.0), fin, jget<int>(best, "rank", 0)), {xr, rr.center().y}, 14, resultW,
                            fin ? gold : ui.theme().accent, FontFace::Bold, Align::Right);
                } else {
                    fitText("Aucune tentative vérifiée", {x0, rr.y + ui.s(35)}, 12, lroom, ui.theme().textDim);
                    fitText("—", {xr, rr.center().y}, 14, resultW, ui.theme().textDim, FontFace::Bold, Align::Right);
                }
            }
            ui.endScroll();
        }
        if (!chosen.empty()) openOther(chosen);
    }

    /// The badges of the profile (2.41): achievements the server checked against the player's
    /// declared ones and stars, drawn from this game's catalogue (an id it does not know, from
    /// a newer game, is left out), at most as many as the steps open.
    std::vector<const AchievementDef*> shownBadges() const {
        std::vector<const AchievementDef*> out;
        const Json list = jfield(data_, "badges", Json());
        if (!list.is_array()) return out;
        const size_t most = static_cast<size_t>(std::max(0, g_.achievementCatalog.maxBadgeSlots()));
        for (const auto& b : list) {
            if (out.size() >= most) break;
            const AchievementDef* a = g_.achievementCatalog.find(jstr(b, "id"));
            if (a && std::find(out.begin(), out.end(), a) == out.end()) out.push_back(a);
        }
        return out;
    }

    static Color presenceColor(const std::string& p) { return presenceDotColor(p); }

    /// Look of a worn item ("" if absent): checked by lookOr() before being drawn.
    static std::string wornLook(const Json& worn, const char* category) {
        const Json& i = worn.value(category, Json::object());
        return i.is_object() ? jstr(i, "look") : std::string();
    }

    /// Name of a worn item: the game's catalogue by id, the server's name for an id this game
    /// does not know (a newer one); "" for no title.
    std::string wornName(const Json& worn, const char* category) const {
        const Json& i = worn.value(category, Json::object());
        if (!i.is_object()) return {};
        if (const CosmeticItem* it = g_.cosmetics.find(jstr(i, "id")); it && it->category == category)
            return std::string(category) == "title" && it->isDefault ? std::string() : it->name;
        std::string n = jstr(i, "name");
        for (char& c : n)
            if (static_cast<unsigned char>(c) < 0x20) c = ' ';
        if (std::string(category) == "title" && jstr(i, "look").empty()) return {};
        return n;
    }

    float drawOutfit(const Json& worn, float x, float y, float w) {
        UIContext& ui = g_.ui;
        Rect band{x, y, w, ui.s(70)};
        ui.r().fillRoundedRect(band, ui.s(10), Color{18, 28, 54, 230});
        ui.r().drawRoundedRect(band, ui.s(10), ui.theme().accent.withAlpha(70), ui.s(1.2f));
        // Nova in the player's suit (the same sheet recoloured), idle.
        float ns = band.h - ui.s(4);
        Rect nova = Rect::fromCenter({band.x + ui.s(14) + ns * 0.5f, band.center().y}, {ns, ns});
        if (AnimationSetPtr set = g_.app.resources().animation("animations/nova.json"); set && set->texture) {
            if (const AnimationClip* c = set->clip("idle"); c && c->frameCount() > 0) {
                TexturePtr tex = set->texture;
                // A suit created on Cosmic Online: drawn from its colours (the public list kept on this computer).
                std::string suit = g_.cosmetics.lookOr("suit", wornLook(worn, "suit"));
                if (TexturePtr t = g_.suitSheet(suit)) tex = t;
                float local = std::fmod(ui.time(), std::max(0.01f, c->totalDuration())), acc = 0.0f;
                int frame = 0;
                for (int i = 0; i < c->frameCount(); ++i) {
                    acc += c->durations[static_cast<size_t>(i)];
                    if (local < acc) {
                        frame = i;
                        break;
                    }
                }
                Rect src = set->frameRect(*c, frame);
                ui.r().drawTexture(*tex, &src, nova);
            }
        }
        float cx = nova.right() + ui.s(10);
        Vec2 hs = ui.label("Tenue", {cx, band.center().y}, 18, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
        cx += hs.x + ui.s(24);
        struct Cell {
            const char* key;
            const char* category;
        };
        const Cell cells[] = {{"Combinaison", "suit"}, {"Traînée", "trail"}, {"Cadre", "frame"}, {"Victoire", "victory"}};
        float cw = (band.right() - ui.s(14) - cx) / 4.0f;
        for (int i = 0; i < 4; ++i) {
            float px = cx + i * cw;
            if (i > 0) ui.r().fillRect({px - ui.s(10), band.y + ui.s(14), std::max(1.0f, ui.s(1)), band.h - ui.s(28)}, ui.theme().accent.withAlpha(50));
            float room = cw - ui.s(20);
            float kf = fz(ui, 13);
            ui.label(ui.elide(cells[i].key, FontFace::Body, kf, room), {px, band.center().y - ui.s(12)}, kf, ui.theme().textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
            std::string name = wornName(worn, cells[i].category);
            float nf = std::max(fz(ui, 12), ui.fitFont(name, FontFace::Bold, 17, room, 0.8f));
            ui.label(ui.elide(name, FontFace::Bold, nf, room), {px, band.center().y + ui.s(12)}, nf, ui.theme().text, Align::Left, FontFace::Bold,
                     VAlign::Middle);
        }
        return band.bottom();
    }

    template <typename T>
    float drawTiles(const std::vector<T>& tiles, float x, float y, float w) {
        UIContext& ui = g_.ui;
        const float gap = ui.s(10), h = ui.s(74);
        float tw = (w - gap * (tiles.size() - 1)) / static_cast<float>(tiles.size());
        for (size_t i = 0; i < tiles.size(); ++i) {
            Rect c{x + i * (tw + gap), y, tw, h};
            ui.r().fillRoundedRect(c, ui.s(10), Color{18, 28, 54, 230});
            ui.label(tiles[i].value, {c.center().x, c.y + ui.s(28)}, ui.fitFont(tiles[i].value, FontFace::Title, 24, tw - ui.s(16)), ui.theme().accent,
                     Align::Center, FontFace::Title, VAlign::Middle);
            ui.label(tiles[i].key, {c.center().x, c.y + ui.s(56)}, ui.fitFont(tiles[i].key, FontFace::Body, 13, tw - ui.s(12)), ui.theme().textDim,
                     Align::Center, FontFace::Body, VAlign::Middle);
        }
        return y + h;
    }

    bool tabEmptyOrBusy(const Rect& area, const Json& list, const std::string& empty) {
        UIContext& ui = g_.ui;
        if (!tabError_.empty()) {
            ui.labelWrapped(tabError_, {area.x, area.y + ui.s(6)}, area.w, 16, ui.theme().textDim);
            return true;
        }
        if (tabLoading_ || !tabData_.is_object()) {
            ui.label("Chargement…", {area.x, area.y + ui.s(16)}, 16, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            return true;
        }
        if (list.empty()) {
            ui.labelWrapped(empty, {area.x, area.y + ui.s(6)}, area.w, 16, ui.theme().textDim);
            return true;
        }
        return false;
    }

    void drawCreations(Rect area) {
        UIContext& ui = g_.ui;
        float sw = std::min(ui.s(420), area.w * 0.5f);
        int sort = creationSort_;
        ui.tabs("csort", {area.right() - sw, area.y, sw, ui.s(36)}, {"Populaires", "Récents", "Mieux notés"}, sort);
        if (sort != creationSort_) {
            creationSort_ = sort;
            loadTab();
        }
        area.y += ui.s(46);
        area.h -= ui.s(46);
        Json list = filtered(tabData_.is_object() ? tabData_.value("creations", Json::array()) : Json::array(), "title");
        if (tabEmptyOrBusy(area, list, !query_.empty() ? std::string("Aucun niveau ne correspond à la recherche.") : me() ? "Vous n'avez publié aucun niveau : ATELIER → Mes niveaux → Publier."
                                             : "Ce joueur n'a publié aucun niveau pour l'instant."))
            return;
        const float rh = ui.s(84), gap = ui.s(8);
        ui.beginScroll("creations", area, list.size() * (rh + gap));
        float cy = area.y - ui.scrollOffset();
        for (const auto& l : list) {
            Rect row{area.x, cy, area.w - ui.s(14), rh};
            cy += rh + gap;
            if (row.bottom() < area.y || row.y > area.bottom()) continue;
            std::string id = jstr(l, "id");
            bool published = jstr(l, "status") == "published";
            UIContext::ButtonStyle st;
            st.enabled = published;
            if (ui.button("lvl/" + id, row, "", st)) g_.app.switchScene(makeOnlineLevels(g_, id));
            Rect th{row.x + ui.s(8), row.y + ui.s(8), (rh - ui.s(16)) * 16.0f / 9.0f, rh - ui.s(16)};
            ui.r().fillRoundedRect(th, ui.s(6), Color{6, 9, 20, 255});
            if (TexturePtr t = thumbnail(id)) ui.r().drawTexture(*t, nullptr, th);
            float tx = th.right() + ui.s(14), room = row.right() - tx - ui.s(14);
            std::string title = jstr(l, "title");
            std::string badge = published ? "" : (jstr(l, "status") == "hidden" ? "masqué (signalements)" : "brouillon");
            float bw = badge.empty() ? 0.0f : ui.text().measure(badge, FontFace::Bold, ui.s(14)).x + ui.s(16);
            ui.label(title, {tx, row.y + ui.s(24)}, ui.fitFont(title, FontFace::Bold, 19, room - bw), ui.theme().text, Align::Left, FontFace::Bold,
                     VAlign::Middle);
            if (!badge.empty()) ui.label(badge, {row.right() - ui.s(14), row.y + ui.s(24)}, 14, ui.theme().danger, Align::Right, FontFace::Bold, VAlign::Middle);
            std::string stats = "v" + std::to_string(jget<int>(l, "version", 1)) + "  ·  " + std::to_string(jget<int>(l, "downloads", 0)) + " téléch.  ·  " +
                                std::to_string(jget<int>(l, "favorites", 0)) + " favoris  ·  " + std::to_string(jget<int>(l, "comments", 0)) + " commentaires";
            const Json& rt = l.value("rating", Json::object());
            if (rt.is_object() && rt.contains("average")) stats += "  ·  note " + decimal(jget<double>(rt, "average", 0.0)) + "/5";
            ui.label(stats, {tx, row.y + ui.s(56)}, ui.fitFont(stats, FontFace::Body, 14, room), ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
        }
        ui.endScroll();
    }

    void drawPlaytime(Rect area) {
        UIContext& ui = g_.ui;
        const Json& pt = tabData_.is_object() ? tabData_.value("playtime", Json::object()) : Json::object();
        Json list = filtered(pt.value("levels", Json::array()), "title");
        std::string head = "Temps de jeu total : " + playTimeText(jget<double>(pt, "total_seconds", 0.0));
        if (!tabLoading_ && tabError_.empty() && tabData_.is_object()) {
            ui.label(head, {area.x, area.y + ui.s(14)}, 19, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            Vec2 ns = ui.labelWrapped("Temps passé dans les niveaux, envoyé par le jeu à la fin de chaque partie ; le serveur ne compte jamais plus que le "
                                      "temps réellement écoulé.",
                                      {area.x, area.y + ui.s(34)}, area.w, 13, ui.theme().textDim);
            area.y += ui.s(42) + ns.y;
            area.h = box_.bottom() - ui.s(44) - area.y;
        }
        if (tabEmptyOrBusy(area, list, query_.empty() ? "Pas encore de temps de jeu enregistré en ligne." : "Aucun niveau ne correspond à la recherche.")) return;
        const float rh = ui.s(40);
        ui.beginScroll("playtime", area, list.size() * rh);
        float cy = area.y - ui.scrollOffset();
        for (const auto& l : list) {
            if (cy + rh > area.y && cy < area.bottom()) {
                std::string title = jstr(l, "title");
                std::string when = "dernière partie " + OnlineUi::ageOf(jstr(l, "last_played_at"));
                float tw = ui.s(170), ww = ui.s(260);
                ui.label(title, {area.x, cy + rh * 0.5f}, ui.fitFont(title, FontFace::Body, 17, area.w - tw - ww - ui.s(40)), ui.theme().text, Align::Left,
                         FontFace::Body, VAlign::Middle);
                ui.label(when, {area.right() - tw - ui.s(30), cy + rh * 0.5f}, ui.fitFont(when, FontFace::Body, 14, ww), ui.theme().textDim, Align::Right,
                         FontFace::Body, VAlign::Middle);
                ui.label(playTimeText(jget<double>(l, "seconds", 0.0)), {area.right() - ui.s(16), cy + rh * 0.5f}, 18, ui.theme().accent, Align::Right,
                         FontFace::Bold, VAlign::Middle);
            }
            cy += rh;
        }
        ui.endScroll();
    }

    void drawBestTimes(Rect area) {
        UIContext& ui = g_.ui;
        const Json& s = data_.value("statistics", Json::object());
        Json all = s.is_object() ? s.value("best_times", Json::array()) : Json::array();
        Json bests = Json::array();
        const std::vector<std::string> cats = bestCategories();
        for (const auto& b : filtered(all, "level_name")) {
            if (filter_ > 0 && filter_ < static_cast<int>(cats.size()) && categoryText(jstr(b, "category")) != cats[static_cast<size_t>(filter_)]) continue;
            bests.push_back(b);
        }
        if (bests.empty()) {
            ui.label(all.empty() ? "Aucun record vérifié pour l'instant." : "Aucun record ne correspond à la recherche.", {area.x, area.y + ui.s(14)}, 16, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            return;
        }
        const float rh = ui.s(36);
        ui.beginScroll("bests", area, bests.size() * rh);
        float yy = area.y - ui.scrollOffset();
        for (const auto& b : bests) {
            RunCategory cat;
            std::string catLabel = categoryFromId(jstr(b, "category"), cat) ? categoryLabel(cat) : jstr(b, "category");
            std::string what = jstr(b, "level_name") + " · " + catLabel;
            ui.label(what, {area.x, yy + rh * 0.5f}, ui.fitFont(what, FontFace::Body, 16, area.w - ui.s(300)), ui.theme().text, Align::Left, FontFace::Body,
                     VAlign::Middle);
            int rk = jget<int>(b, "rank", 0);
            ui.label("n°" + std::to_string(rk), {area.right() - ui.s(150), yy + rh * 0.5f}, 16,
                     rk <= 3 ? GlobeRenderer::tierColor(markerTier(rk)) : ui.theme().textDim, Align::Right, FontFace::Bold, VAlign::Middle);
            ui.label(runTime(jget<int64_t>(b, "time_ms", 0)), {area.right() - ui.s(16), yy + rh * 0.5f}, 16, ui.theme().text, Align::Right, FontFace::Bold,
                     VAlign::Middle);
            yy += rh;
        }
        ui.endScroll();
    }

    /// The entries of `list` whose `key` contains the search (case-insensitive, ASCII).
    Json filtered(const Json& list, const char* key) const {
        if (query_.empty() || !list.is_array()) return list.is_array() ? list : Json::array();
        auto lower = [](std::string t) {
            std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return t;
        };
        const std::string q = lower(query_);
        Json out = Json::array();
        for (const auto& e : list)
            if (lower(jstr(e, key)).find(q) != std::string::npos) out.push_back(e);
        return out;
    }
    static std::string categoryText(const std::string& id) {
        RunCategory cat;
        return categoryFromId(id, cat) ? categoryLabel(cat) : id;
    }
    /// « Toutes » then the categories of this player's best times (filter of the tab).
    std::vector<std::string> bestCategories() const {
        std::vector<std::string> out = {"Toutes"};
        const Json& s = data_.value("statistics", Json::object());
        if (s.is_object())
            for (const auto& b : s.value("best_times", Json::array())) {
                std::string c = categoryText(jstr(b, "category"));
                if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
            }
        return out;
    }

    /// « Meilleures progressions » (2.32.2): the best verified attempt on every EXTRÊME level.
    void drawRaces(Rect area) {
        UIContext& ui = g_.ui;
        Json all = filtered(tabData_.is_object() ? tabData_.value("races", Json::array()) : Json::array(), "title");
        Json list = Json::array();
        for (const auto& r : all) {
            bool fin = jget<bool>(r, "finished", false);
            if ((filter_ == 1 && !fin) || (filter_ == 2 && fin)) continue;
            list.push_back(r);
        }
        if (tabEmptyOrBusy(area, list, !query_.empty() || filter_ > 0 ? "Aucune progression ne correspond."
                                       : me() ? "Aucune tentative classée sur un niveau EXTRÊME : chaque tentative compte, même sans arriver au bout."
                                              : "Ce joueur n'a pas encore de tentative classée sur un niveau EXTRÊME."))
            return;
        const float rh = ui.s(64), gap = ui.s(6);
        const float viewW = ui.s(104), barW = std::min(ui.s(320), area.w * 0.26f);
        ui.beginScroll("races", area, list.size() * (rh + gap));
        float cy = area.y - ui.scrollOffset();
        for (const auto& r : list) {
            Rect row{area.x, cy, area.w - ui.s(14), rh};
            cy += rh + gap;
            if (row.bottom() < area.y || row.y > area.bottom()) continue;
            ui.r().fillRoundedRect(row, ui.s(10), Color{14, 20, 42, 220});
            // Right to left: « Voir », the rank, the bar with its score; the title in what is left.
            const std::string runId = jstr(r, "run_id");
            Rect vb{row.right() - ui.s(10) - viewW, row.y + (rh - ui.s(42)) * 0.5f, viewW, ui.s(42)};
            UIContext::ButtonStyle vs;
            vs.small = true;
            vs.enabled = jget<bool>(r, "has_replay", false) && fetching_.empty();
            vs.fontSize = ui.fitFont("Voir", FontFace::Bold, 20, viewW - ui.s(20));
            if (ui.button("rv/" + runId, vb, fetching_ == runId ? "…" : "Voir", vs)) watchRace(r);
            // 2.45: the replay kept on this computer, to watch it without a connection.
            Rect db{vb.x - ui.s(10) - ui.s(42), vb.y, ui.s(42), ui.s(42)};
            ReplayDownloadUi::icon(g_, "rd/" + runId, db, raceDownload(r), jget<bool>(r, "has_replay", false) && !runId.empty());
            int rank = jget<int>(r, "rank", 0), n = jget<int>(r, "participants", 0);
            std::string rk = rank > 0 ? "n°" + std::to_string(rank) + " / " + std::to_string(n) : "—";
            float rkw = ui.s(120);
            ui.label(rk, {db.x - ui.s(16), row.center().y}, ui.fitFont(rk, FontFace::Bold, 17, rkw), rank > 0 && rank <= 3 ? GlobeRenderer::tierColor(markerTier(rank)) : ui.theme().text,
                     Align::Right, FontFace::Bold, VAlign::Middle);
            float bx = db.x - ui.s(16) - rkw - ui.s(20) - barW;
            double score = jget<double>(r, "score", 0.0);
            bool fin = jget<bool>(r, "finished", false);
            Rect bar{bx, row.center().y + ui.s(4), barW, ui.s(10)};
            ui.r().fillRoundedRect(bar, ui.s(5), Color{30, 40, 70, 255});
            ui.r().fillRoundedRect({bar.x, bar.y, bar.w * static_cast<float>(std::clamp(score / 100.0, 0.0, 1.0)), bar.h}, ui.s(5),
                                   fin ? ui.theme().success : ui.theme().warm);
            std::string sc = decimal(score) + " %" + (fin ? "  ·  terminé" : "");
            ui.label(sc, {bx, row.center().y - ui.s(12)}, ui.fitFont(sc, FontFace::Bold, 16, barW), fin ? ui.theme().success : ui.theme().text, Align::Left,
                     FontFace::Bold, VAlign::Middle);
            float tx = row.x + ui.s(16), room = bx - ui.s(24) - tx;
            std::string title = jstr(r, "title");
            std::string sub = std::string(jstr(r, "kind") == "community" ? "niveau de la communauté" : "campagne") + "  ·  " + OnlineUi::ageOf(jstr(r, "at"));
            {
                i18n::Verbatim v;  // the level's name as its author wrote it
                ui.label(title, {tx, row.y + ui.s(22)}, ui.fitFont(title, FontFace::Bold, 18, room), ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            }
            ui.label(sub, {tx, row.y + ui.s(46)}, ui.fitFont(sub, FontFace::Body, 14, room), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        }
        ui.endScroll();
    }

    /// « Télécharger » (2.45): this player's best attempt on a level, kept on this computer.
    ReplayDownloadRequest raceDownload(const Json& r) const {
        const std::string runId = jstr(r, "run_id");
        const Json& p = data_.is_object() ? jfield(data_, "player", Json::object()) : Json::object();
        const std::string who = jstr(p, "username");
        const bool official = jstr(r, "kind") != "community";
        const std::string levelId = jstr(r, "level_id");
        const bool installed = official && g_.content.level(levelId) != nullptr;
        ReplayDownloadRequest q = ReplayDownloadUi::request(
            "race", runId, "/api/v1/races/runs/" + runId + "/replay", who, who.empty() ? std::string() : ui_.displayName(who, 0, me()),
            installed ? levelId : std::string(), jstr(r, "title"), "Course à la vérification – " + decimal(jget<double>(r, "score", 0.0)) + " %",
            jget<int64_t>(r, "time_ms", 0), jstr(r, "at"));
        q.meta.playerId = playerId_;
        return q;
    }

    void watchRace(const Json& r) {
        const std::string runId = jstr(r, "run_id");
        const std::string title = jstr(r, "title");
        double score = jget<double>(r, "score", 0.0);
        // 2.45: downloaded on this computer, it plays from its copy (no network needed).
        if (auto rep = ReplayDownloadUi::localCopy(g_, ReplayLibrary::makeId("race", runId))) {
            g_.app.pushScene(makeLocalReplayViewer(g_, std::move(*rep), "Course à la vérification – " + title + " – " + decimal(score) + " %"));
            return;
        }
        fetching_ = runId;
        say("Téléchargement du replay…", false);
        g_.online.getBinary(
            "/api/v1/races/runs/" + runId + "/replay",
            [this, title, score, alive = std::weak_ptr<int>(life_)](const HttpResponse& resp) {
                if (alive.expired()) return;
                fetching_.clear();
                if (resp.status != 200 || resp.body.empty())
                    return say(resp.networkFailure() ? std::string("Connexion impossible : replay indisponible pour l'instant.")
                                                     : "Replay indisponible (" + std::to_string(resp.status) + ").",
                               true);
                std::string err;
                auto rep = ReplayData::decode(std::vector<uint8_t>(resp.body.begin(), resp.body.end()), &err);
                if (!rep) return say("Replay illisible : " + err, true);
                say("", false);
                g_.app.pushScene(makeLocalReplayViewer(g_, std::move(*rep), "Course à la vérification – " + title + " – " + decimal(score) + " %"));
            },
            16u << 20);
    }

    void drawPlayers(const Rect& area) {
        UIContext& ui = g_.ui;
        Json list = filtered(tabData_.is_object() ? tabData_.value("players", Json::array()) : Json::array(), "username");
        if (tabEmptyOrBusy(area, list, !query_.empty() ? "Aucun joueur ne correspond à la recherche."
                                       : tab_ == 4 ? "Personne ne suit encore ce joueur." : "Ce joueur ne suit aucun créateur."))
            return;
        const float rh = ui.s(58), gap = ui.s(6);
        ui.beginScroll("players", area, list.size() * (rh + gap));
        float cy = area.y - ui.scrollOffset();
        std::string chosen;  // opened once the list is closed (never in the middle of it)
        for (const auto& pl : list) {
            Rect row{area.x, cy, area.w - ui.s(14), rh};
            cy += rh + gap;
            if (row.bottom() < area.y || row.y > area.bottom()) continue;
            std::string id = jstr(pl, "id");
            if (ui.button("pl/" + id, row, "")) chosen = id;
            ui_.drawAvatar(jstr(pl, "avatar", "avatar_01"), id, {row.x + ui.s(10), row.y + ui.s(9), ui.s(40), ui.s(40)});
            std::string name = jstr(pl, "username");
            int nf = jget<int>(pl, "followers", 0);
            std::string info = std::to_string(nf) + (nf > 1 ? " abonnés" : " abonné") + "  ·  depuis " + OnlineUi::ageOf(jstr(pl, "since"));
            float iw = ui.s(330);
            Vec2 ns = ui.label(name, {row.x + ui.s(64), row.center().y}, ui.fitFont(name, FontFace::Bold, 18, row.w - iw - ui.s(200)), ui.theme().text,
                               Align::Left, FontFace::Bold, VAlign::Middle);
            if (jget<bool>(pl, "creator", false)) {
                float cw = ui.text().measure("CRÉATEUR", FontFace::Bold, ui.s(12)).x + ui.s(16);
                Rect chip{row.x + ui.s(76) + ns.x, row.center().y - ui.s(11), cw, ui.s(22)};
                ui.r().fillRoundedRect(chip, ui.s(11), ui.theme().warm.withAlphaF(0.22f));
                ui.label("CRÉATEUR", chip.center(), 12, ui.theme().warm, Align::Center, FontFace::Bold, VAlign::Middle);
            }
            ui.label(info, {row.right() - ui.s(14), row.center().y}, ui.fitFont(info, FontFace::Body, 14, iw), ui.theme().textDim, Align::Right,
                     FontFace::Body, VAlign::Middle);
        }
        ui.endScroll();
        if (!chosen.empty()) openOther(chosen);
    }

    void drawConfirmBlock() {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 170});
        Rect box{ui.screenW() * 0.5f - ui.s(330), ui.screenH() * 0.5f - ui.s(130), ui.s(660), ui.s(260)};
        ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
        ui.r().drawRoundedRect(box, ui.s(18), ui.theme().danger.withAlphaF(0.6f), ui.s(1.5f));
        ui.pushId("block");
        std::string name = jstr(data_["player"], "username");
        ui.labelWrapped("Bloquer " + name + " ? Il ne pourra plus vous écrire, vous suivre, ni voir vos commentaires ; ses commentaires vous seront "
                        "masqués. Vous ne le suivrez plus.",
                        {box.x + ui.s(30), box.y + ui.s(30)}, box.w - ui.s(60), 17, ui.theme().text);
        UIContext::ButtonStyle d;
        d.danger = true;
        if (ui.button("ok", {box.x + ui.s(30), box.bottom() - ui.s(70), ui.s(240), ui.s(46)}, "Bloquer", d)) {
            confirmBlock_ = false;
            block(true);
        } else if (ui.button("cancel", {box.right() - ui.s(210), box.bottom() - ui.s(70), ui.s(180), ui.s(46)}, "Annuler") || ui.backPressed()) {
            confirmBlock_ = false;
        }
        ui.popId();
    }

    GameServices& g_;
    OnlineUi ui_;
    BadgeRow badgeRow_;  // the badges of the header (2.41) and their tooltip
    std::string playerId_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    Rect box_;
    double lastPending_ = 0.0;
    Json data_;
    std::string error_;
    bool loading_ = false, fromCache_ = false, busy_ = false, confirmBlock_ = false;
    int generation_ = 0;
    int tab_ = 0, creationSort_ = 0;
    std::string query_;     // search of the current tab
    int filter_ = 0;        // filter of the current tab (category, finished or not)
    std::string fetching_;  // attempt whose replay is downloading
    static constexpr int kTabClan = 6;  // 2.62.5: the player's clan
    Json tabData_;
    Json myClanPage_;  // tab « Clan »: GET /clans/{the viewer's clan} (its pending requests), chief and officers only
    std::string tabError_;
    bool tabLoading_ = false;
    int tabGeneration_ = 0;
    std::map<std::string, TexturePtr> thumbs_;
    std::string status_;
    bool statusError_ = false;
    float statusT_ = -100.0f;
};

}  // namespace

std::unique_ptr<Scene> makePlayerProfile(GameServices& g, const std::string& playerId) {
    return std::make_unique<PlayerProfileScene>(g, playerId);
}

}  // namespace cosmic
