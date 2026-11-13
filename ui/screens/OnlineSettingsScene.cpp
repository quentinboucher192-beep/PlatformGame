// ONLINE SETTINGS: client preferences, account and privacy (stored on the server), friends,
// GDPR rights (export, deletion), cache and synchronisation.

#include <cmath>

#include "engine/core/FileUtil.h"
#include "game/GameServices.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

class OnlineSettingsScene : public Scene {
public:
    explicit OnlineSettingsScene(GameServices& g) : g_(g), ui_(g) {}

    bool isOverlay() const override { return true; }

    void onEnter() override {
        loadProfileFields();
        g_.ui.setFocus("osettings/tabs");
        if (g_.online.loggedIn()) g_.online.refreshProfile(OnlineService::guard(life_, [this](const ApiResult& r) {
            if (r.ok) loadProfileFields();
        }));
    }

    void update(float dt) override { t_ += dt; }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 200});
        box_ = {ui.screenW() * 0.5f - ui.s(500), ui.screenH() * 0.5f - ui.s(360), ui.s(1000), ui.s(720)};
        ui.r().fillRoundedRect(box_, ui.s(18), Color{9, 14, 32, 248});
        ui.r().drawRoundedRect(box_, ui.s(18), ui.theme().accent.withAlphaF(0.55f), ui.s(1.5f));
        ui.label("PARAMÈTRES EN LIGNE", {box_.x + ui.s(36), box_.y + ui.s(42)}, 30, ui.theme().accent, Align::Left, FontFace::Title, VAlign::Middle);
        ui.pushId("osettings");
        UIContext::ButtonStyle st;
        st.small = true;
        if (ui.button("close", {box_.right() - ui.s(150), box_.y + ui.s(22), ui.s(120), ui.s(40)}, "Fermer", st) || ui.backPressed()) {
            ui.popId();
            ui.end();
            g_.app.popScene();
            return;
        }
        int tab = tab_;
        ui.tabs("tabs", {box_.x + ui.s(30), box_.y + ui.s(78), box_.w - ui.s(60), ui.s(42)}, {"Préférences", "Compte et confidentialité", "Amis"}, tab);
        if (tab != tab_) {
            tab_ = tab;
            message_.clear();
            if (tab_ == 2) loadFriends();
        }
        content_ = {box_.x + ui.s(36), box_.y + ui.s(138), box_.w - ui.s(72), box_.h - ui.s(196)};
        if (tab_ == 0) preferences();
        else if (tab_ == 1) account();
        else friends();
        if (!message_.empty())
            ui.labelWrapped(message_, {box_.x + ui.s(36), box_.bottom() - ui.s(46)}, box_.w - ui.s(72), 16,
                            messageError_ ? ui.theme().danger : ui.theme().success);
        ui.popId();
        ui.end();
    }

private:
    void say(const std::string& m, bool error) {
        message_ = m;
        messageError_ = error;
    }

    void loadProfileFields() {
        const Json& p = g_.online.profile();
        if (!p.is_object()) return;
        country_ = jstr(p, "country_code");
        std::string lv = jstr(p, "location_visibility", "hidden");
        locVis_ = lv == "globe" ? 2 : lv == "country" ? 1 : 0;
        profilePublic_ = jstr(p, "profile_visibility", "public") == "public";
        allowFriends_ = jget<bool>(p, "allow_friend_requests", true);
        avatar_ = jstr(p, "avatar", "avatar_01");
        if (avatar_ == "custom") avatar_.clear();
    }

    // ---------------------------------------------------------- preferences
    void preferences() {
        UIContext& ui = g_.ui;
        OnlineSettings s = g_.settings.online;
        float x = content_.x, w = content_.w * 0.5f - ui.s(16), y = content_.y;
        ui.pushId("prefs");
        ui.toggle("enabled", {x, y, w, ui.s(42)}, "Activer Cosmic Online", s.enabled);
        ui.toggle("autologin", {x, y + ui.s(50), w, ui.s(42)}, "Connexion automatique", s.autoLogin);
        ui.toggle("autosync", {x, y + ui.s(100), w, ui.s(42)}, "Synchronisation automatique", s.autoSync);
        ui.toggle("names", {x, y + ui.s(150), w, ui.s(42)}, "Afficher les pseudonymes", s.showPseudonyms);
        int bw = s.bandwidth;
        ui.cycle("bandwidth", {x, y + ui.s(200), w, ui.s(42)}, "Bande passante", bw, {"Économie", "Normale"});
        s.bandwidth = bw;
        if (s.enabled != g_.settings.online.enabled || s.autoLogin != g_.settings.online.autoLogin || s.autoSync != g_.settings.online.autoSync ||
            s.showPseudonyms != g_.settings.online.showPseudonyms || s.bandwidth != g_.settings.online.bandwidth) {
            g_.settings.online = s;
            g_.saveSettings();
            g_.online.setSettings(s);
        }
        ui.labelWrapped("« Afficher les pseudonymes » désactivé : les autres joueurs apparaissent sous la forme « Joueur n°X » (diffusion en "
                        "direct). « Économie » limite le nombre de marqueurs et ne télécharge pas les avatars personnalisés.",
                        {x, y + ui.s(256)}, w, 15, ui.theme().textDim);
        UIContext::ButtonStyle st;
        st.enabled = g_.online.usable() && g_.online.loggedIn() && !g_.online.syncing();
        if (ui.button("sync", {x, y + ui.s(350), w, ui.s(44)}, g_.online.syncing() ? "Synchronisation…" : "Synchroniser maintenant", st)) {
            g_.online.syncNow(true);
            say("Synchronisation lancée.", false);
        }
        if (ui.button("cache", {x, y + ui.s(402), w, ui.s(44)}, "Effacer le cache en ligne")) {
            size_t n = g_.online.clearCache();
            say(std::to_string(n) + " élément(s) du cache supprimé(s).", false);
        }
        // Technical information
        float x2 = x + w + ui.s(32);
        float yy = y;
        auto line = [&](const std::string& k, const std::string& v) {
            ui.label(k, {x2, yy}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            ui.labelWrapped(v, {x2, yy + ui.s(10)}, w, 17, ui.theme().text);
            yy += ui.s(58);
        };
        const OnlineConfig& c = g_.online.config();
        line("Serveur", c.serverUrl.empty() ? "non configuré" : c.serverUrl);
        line("Origine de l'adresse", c.source);
        line("État", onlineStateLabel(g_.online.state()));
        line("Client HTTP", g_.online.transportName());
        line("Performances en attente d'envoi", std::to_string(g_.online.unsentCount()));
        line("Records en cours de vérification", std::to_string(g_.online.pendingVerificationCount()));
        ui.popId();
    }

    // -------------------------------------------------------------- account
    void account() {
        UIContext& ui = g_.ui;
        if (!g_.online.loggedIn()) {
            ui.labelWrapped("Vous n'êtes pas connecté. Les réglages de confidentialité de votre compte apparaîtront ici après la connexion.",
                            {content_.x, content_.y}, content_.w, 18, ui.theme().textDim);
            UIContext::ButtonStyle p;
            p.primary = true;
            p.enabled = g_.online.usable();
            if (ui.button("login", {content_.x, content_.y + ui.s(80), ui.s(320), ui.s(48)}, "Se connecter / créer un compte", p))
                g_.app.pushScene(makeOnlineAccount(g_, false));
            return;
        }
        if (pickingCountry_) {
            if (ui_.countryPicker("countries", {content_.x, content_.y, content_.w, content_.h - ui.s(60)}, countryFilter_, country_)) {
                pickingCountry_ = false;
                if (locVis_ == 0) locVis_ = 1;
            }
            if (ui.button("nocountry", {content_.x, content_.bottom() - ui.s(48), ui.s(300), ui.s(44)}, "Ne pas indiquer de pays")) {
                country_.clear();
                locVis_ = 0;
                pickingCountry_ = false;
            }
            return;
        }
        float x = content_.x, w = content_.w * 0.5f - ui.s(16), y = content_.y;
        ui.pushId("account");
        ui.label("Connecté en tant que " + g_.online.username(), {x, y + ui.s(10)}, 20, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        y += ui.s(36);
        ui.toggle("public", {x, y, w, ui.s(42)}, "Profil public visible", profilePublic_);
        UIContext::ButtonStyle cs;
        cs.align = Align::Left;
        if (ui.button("country", {x, y + ui.s(50), w, ui.s(42)}, country_.empty() ? "Pays : non indiqué" : "      Pays : " + ui_.countryName(country_), cs))
            pickingCountry_ = true;
        if (!country_.empty()) ui_.drawFlag(country_, {x + ui.s(14), y + ui.s(62), ui.s(24), ui.s(18)});
        int vis = country_.empty() ? 0 : locVis_;
        if (ui.cycle("locvis", {x, y + ui.s(100), w, ui.s(42)}, "Visibilité du pays", vis, {"Masqué", "Classements", "Classements et globe"}) &&
            !country_.empty())
            locVis_ = vis;
        ui.toggle("friends", {x, y + ui.s(150), w, ui.s(42)}, "Autoriser les demandes d'amis", allowFriends_);
        ui.label("Avatar", {x, y + ui.s(206)}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        ui_.avatarPicker("avatars", {x, y + ui.s(220), w, w / 4.0f}, avatar_);
        UIContext::ButtonStyle p;
        p.primary = true;
        p.enabled = !busy_;
        if (ui.button("save", {x, content_.bottom() - ui.s(48), w, ui.s(46)}, "Enregistrer", p)) saveProfile();

        float x2 = x + w + ui.s(32);
        float yy = y;
        ui.labelWrapped("Vos données (RGPD) : pseudonyme, avatar, pays facultatif, temps, replays et journaux de sécurité (adresses IP "
                        "uniquement sous forme d'empreinte). Vous pouvez les exporter ou supprimer définitivement votre compte.",
                        {x2, yy}, w, 15, ui.theme().textDim);
        yy += ui.s(96);
        UIContext::ButtonStyle s;
        s.enabled = !busy_;
        if (ui.button("export", {x2, yy, w, ui.s(42)}, "Exporter mes données", s)) exportData();
        if (ui.button("logout", {x2, yy + ui.s(50), w, ui.s(42)}, "Se déconnecter", s))
            g_.online.logout(false, OnlineService::guard(life_, [this](const ApiResult&) { say("Déconnecté.", false); }));
        if (ui.button("logoutall", {x2, yy + ui.s(100), w, ui.s(42)}, "Déconnecter tous mes appareils", s))
            g_.online.logout(true, OnlineService::guard(life_, [this](const ApiResult& r) {
                say(r.ok ? "Toutes vos sessions ont été fermées." : "Déconnecté localement : " + r.message, !r.ok);
            }));
        yy += ui.s(170);
        ui.label("Supprimer le compte", {x2, yy}, 18, ui.theme().danger, Align::Left, FontFace::Bold, VAlign::Middle);
        ui.labelWrapped("Supprime définitivement le compte, les records, les replays, les amis et l'avatar sur le serveur. Les records "
                        "locaux restent sur cet ordinateur.",
                        {x2, yy + ui.s(14)}, w, 14, ui.theme().textDim);
        ui.passwordField("delpass", {x2, yy + ui.s(70), w, ui.s(42)}, deletePassword_, "Mot de passe pour confirmer");
        UIContext::ButtonStyle d;
        d.danger = true;
        d.enabled = !busy_ && !deletePassword_.empty();
        if (ui.button("delete", {x2, yy + ui.s(120), w, ui.s(44)}, confirmDelete_ ? "Confirmer la suppression définitive" : "Supprimer mon compte", d)) {
            if (!confirmDelete_) confirmDelete_ = true;
            else deleteAccount();
        }
        ui.popId();
    }

    void saveProfile() {
        static const char* vis[] = {"hidden", "country", "globe"};
        Json patch = {{"profile_visibility", profilePublic_ ? "public" : "private"},
                      {"location_visibility", country_.empty() ? "hidden" : vis[locVis_]},
                      {"allow_friend_requests", allowFriends_}};
        patch["country_code"] = country_.empty() ? Json(nullptr) : Json(country_);
        if (!avatar_.empty()) patch["avatar"] = avatar_;
        busy_ = true;
        g_.online.updateProfile(patch, OnlineService::guard(life_, [this](const ApiResult& r) {
            busy_ = false;
            say(r.ok ? "Paramètres de confidentialité enregistrés." : r.message, !r.ok);
            if (r.ok) loadProfileFields();
        }));
    }

    void exportData() {
        busy_ = true;
        g_.online.exportData(OnlineService::guard(life_, [this](const ApiResult& r) {
            busy_ = false;
            if (!r.ok) return say(r.message, true);
            fs::path file = Paths::exportsDir() / ("cosmic_online_donnees_" + FileUtil::slugify(g_.online.username()) + ".json");
            std::error_code ec;
            fs::create_directories(file.parent_path(), ec);
            if (FileUtil::saveJson(file, r.data)) say("Données exportées dans " + Paths::toUtf8(file), false);
            else say("Impossible d'écrire " + Paths::toUtf8(file), true);
        }));
    }

    void deleteAccount() {
        busy_ = true;
        g_.online.deleteAccount(deletePassword_, OnlineService::guard(life_, [this](const ApiResult& r) {
            busy_ = false;
            confirmDelete_ = false;
            deletePassword_.clear();
            say(r.ok ? "Compte et données en ligne supprimés." : r.message, !r.ok);
        }));
    }

    // -------------------------------------------------------------- friends
    void loadFriends() {
        if (!g_.online.loggedIn()) return;
        friendsLoading_ = true;
        g_.online.get("/api/v1/friends", OnlineService::guard(life_, [this](const ApiResult& r) {
            friendsLoading_ = false;
            if (r.ok) {
                friends_ = r.data;
                friendsCache_ = r.fromCache;
            } else {
                say(r.message, true);
            }
        }));
    }

    void friends() {
        UIContext& ui = g_.ui;
        if (!g_.online.loggedIn()) {
            ui.labelWrapped("Connectez-vous pour ajouter des amis et comparer vos temps.", {content_.x, content_.y}, content_.w, 18, ui.theme().textDim);
            return;
        }
        ui.pushId("friends");
        float x = content_.x, w = content_.w;
        ui.textField("add", {x, content_.y, w - ui.s(250), ui.s(42)}, addName_, "Pseudonyme de votre ami", 20);
        UIContext::ButtonStyle p;
        p.primary = true;
        p.enabled = addName_.size() >= 3;
        if (ui.button("send", {x + w - ui.s(236), content_.y, ui.s(236), ui.s(42)}, "Envoyer une demande", p)) {
            g_.online.send("POST", "/api/v1/friends/request", {{"username", addName_}}, OnlineService::guard(life_, [this](const ApiResult& r) {
                say(r.ok ? (jstr(r.data, "status") == "accepted" ? "Vous êtes maintenant amis." : "Demande envoyée.") : r.message, !r.ok);
                if (r.ok) {
                    addName_.clear();
                    loadFriends();
                }
            }));
        }
        Rect view{x, content_.y + ui.s(56), w, content_.h - ui.s(60)};
        std::vector<std::pair<std::string, Json>> sections = {{"Demandes reçues", friends_.value("incoming", Json::array())},
                                                              {"Amis", friends_.value("friends", Json::array())},
                                                              {"Demandes envoyées", friends_.value("outgoing", Json::array())}};
        float total = 0;
        for (auto& s : sections) total += ui.s(34) + std::max<size_t>(1, s.second.size()) * ui.s(52);
        ui.beginScroll("list", view, total);
        float y = view.y - ui.scrollOffset();
        int k = 0;
        for (auto& [title, list] : sections) {
            ui.label(title + " (" + std::to_string(list.size()) + ")", {x, y + ui.s(16)}, 18, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
            y += ui.s(34);
            if (list.empty()) {
                ui.label(friendsLoading_ ? "Chargement…" : "Aucun", {x + ui.s(12), y + ui.s(18)}, 16, ui.theme().textDim, Align::Left, FontFace::Body,
                         VAlign::Middle);
                y += ui.s(52);
                continue;
            }
            for (const auto& f : list) {
                const Json& pl = f["player"];
                std::string pid = jstr(pl, "id");
                Rect row{x, y, w - ui.s(12), ui.s(46)};
                ui.r().fillRoundedRect(row, ui.s(6), Color{16, 24, 46, 200});
                ui_.drawAvatar(jstr(pl, "avatar", "avatar_01"), pid, {row.x + ui.s(6), row.y + ui.s(4), ui.s(38), ui.s(38)});
                std::string cc = jstr(pl, "country_code");
                float nx = row.x + ui.s(54);
                if (!cc.empty()) {
                    ui_.drawFlag(cc, {nx, row.center().y - ui.s(8), ui.s(22), ui.s(16)});
                    nx += ui.s(30);
                }
                ui.label(jstr(pl, "username"), {nx, row.center().y}, 18, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
                UIContext::ButtonStyle s;
                s.small = true;
                std::string id = std::to_string(k++);
                if (title == "Demandes reçues") {
                    if (ui.button("acc" + id, {row.right() - ui.s(260), row.y + ui.s(6), ui.s(124), ui.s(34)}, "Accepter", s))
                        g_.online.send("POST", "/api/v1/friends/accept", {{"player_id", pid}}, OnlineService::guard(life_, [this](const ApiResult& r) {
                            say(r.ok ? "Demande acceptée." : r.message, !r.ok);
                            loadFriends();
                        }));
                }
                std::string rm = title == "Amis" ? "Retirer" : title == "Demandes reçues" ? "Refuser" : "Annuler";
                if (ui.button("rm" + id, {row.right() - ui.s(128), row.y + ui.s(6), ui.s(120), ui.s(34)}, rm, s))
                    g_.online.send("DELETE", "/api/v1/friends/" + pid, Json(), OnlineService::guard(life_, [this](const ApiResult& r) {
                        say(r.ok ? "C'est fait." : r.message, !r.ok);
                        loadFriends();
                    }));
                y += ui.s(52);
            }
        }
        ui.endScroll();
        if (friendsCache_)
            ui.label("Hors ligne : liste en cache", {content_.right(), content_.y - ui.s(24)}, 14, ui.theme().warm, Align::Right, FontFace::Body,
                     VAlign::Middle);
        ui.popId();
    }

    GameServices& g_;
    OnlineUi ui_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    Rect box_, content_;
    float t_ = 0.0f;
    int tab_ = 0;
    std::string message_;
    bool messageError_ = false;
    bool busy_ = false;
    std::string country_, countryFilter_, avatar_ = "avatar_01";
    int locVis_ = 0;
    bool profilePublic_ = true, allowFriends_ = true;
    bool pickingCountry_ = false;
    std::string deletePassword_;
    bool confirmDelete_ = false;
    Json friends_ = Json::object();
    bool friendsLoading_ = false, friendsCache_ = false;
    std::string addName_;
};

}  // namespace

std::unique_ptr<Scene> makeOnlineSettings(GameServices& g) { return std::make_unique<OnlineSettingsScene>(g); }

}  // namespace cosmic
