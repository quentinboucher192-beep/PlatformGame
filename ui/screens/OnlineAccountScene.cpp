// COSMIC ONLINE - account overlay: first-visit explanation, account creation (pseudonym,
// avatar, optional country, location visibility, terms), login, or offline play. An
// account is never required to play.

#include <cmath>

#include "engine/core/FileUtil.h"
#include "game/GameServices.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

enum class Page { Welcome, Register, Login, Terms, Country };

class OnlineAccountScene : public Scene {
public:
    OnlineAccountScene(GameServices& g, bool onboarding) : g_(g), ui_(g), page_(onboarding ? Page::Welcome : Page::Login) {}

    bool isOverlay() const override { return true; }

    void onEnter() override {
        if (auto t = FileUtil::readText(Paths::dataDir() / "online" / "terms_fr.txt")) terms_ = *t;
        else terms_ = "Conditions d'utilisation introuvables dans l'installation.";
        g_.ui.setFocus(page_ == Page::Welcome ? "account/welcome/create" : "account/login/user");
    }

    void update(float dt) override { t_ += dt; }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        float a = std::clamp(t_ / 0.25f, 0.0f, 1.0f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, static_cast<uint8_t>(190 * a)});
        box_ = {ui.screenW() * 0.5f - ui.s(470), ui.screenH() * 0.5f - ui.s(345), ui.s(940), ui.s(690)};
        ui.r().fillRoundedRect(box_, ui.s(18), Color{9, 14, 32, 248});
        ui.r().drawRoundedRect(box_, ui.s(18), ui.theme().accent.withAlphaF(0.55f), ui.s(1.5f));
        switch (page_) {
            case Page::Welcome: welcome(); break;
            case Page::Register: registerPage(); break;
            case Page::Login: loginPage(); break;
            case Page::Terms: termsPage(); break;
            case Page::Country: countryPage(); break;
        }
        if (ui.backPressed() && !busy_) back();
        ui.end();
    }

private:
    void back() {
        if (page_ == Page::Terms || page_ == Page::Country) page_ = Page::Register;
        else if (page_ != Page::Welcome && onboardingVisible()) page_ = Page::Welcome;
        else close(false);
    }
    bool onboardingVisible() const { return !g_.settings.online.onboardingDone; }

    void close(bool connected) {
        (void)connected;
        g_.settings.online.onboardingDone = true;
        g_.saveSettings();
        g_.app.popScene();
    }

    void title(const std::string& t, const std::string& sub) {
        UIContext& ui = g_.ui;
        ui.label(t, {box_.x + ui.s(36), box_.y + ui.s(44)}, 32, ui.theme().accent, Align::Left, FontFace::Title, VAlign::Middle);
        if (!sub.empty()) ui.label(sub, {box_.x + ui.s(36), box_.y + ui.s(80)}, 18, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    }

    void welcome() {
        UIContext& ui = g_.ui;
        title("Bienvenue dans Cosmic Online", "Classements mondiaux, records vérifiés et globe interactif");
        float x = box_.x + ui.s(40), w = box_.w - ui.s(80);
        float y = box_.y + ui.s(118);
        auto point = [&](const std::string& head, const std::string& text) {
            ui.r().fillCircle({x + ui.s(6), y + ui.s(12)}, ui.s(4), ui.theme().accent);
            ui.label(head, {x + ui.s(22), y + ui.s(12)}, 19, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            Vec2 sz = ui.labelWrapped(text, {x + ui.s(22), y + ui.s(30)}, w - ui.s(22), 16, ui.theme().textDim);
            y += ui.s(40) + sz.y;
        };
        point("Facultatif", "Un compte n'est jamais obligatoire : tout le jeu reste jouable hors ligne, avec vos records locaux.");
        point("Ce qui est partagé", "Votre pseudonyme, votre avatar, vos temps et le replay de vos meilleurs runs (la trajectoire de votre "
                                    "personnage, pas une vidéo). Le mot de passe n'est conservé que sous forme chiffrée irréversible.");
        point("Localisation", "Uniquement le pays que vous choisissez, si vous le choisissez. Sur le globe, vous apparaissez au centre de "
                              "ce pays : jamais de position GPS, jamais d'adresse. Vous pouvez la masquer à tout moment.");
        point("Vos droits", "Vous pouvez masquer votre profil, exporter vos données ou supprimer votre compte depuis les paramètres en ligne.");
        if (!g_.online.usable())
            ui.labelWrapped(std::string("Service indisponible : ") + onlineStateLabel(g_.online.state()) + ".", {x, box_.bottom() - ui.s(130)}, w,
                            16, ui.theme().warm);
        ui.pushId("account/welcome");
        float bw = (w - ui.s(40)) / 3, by = box_.bottom() - ui.s(80);
        UIContext::ButtonStyle p;
        p.primary = true;
        p.enabled = g_.online.usable();
        if (ui.button("create", {x, by, bw, ui.s(52)}, "Créer un compte", p)) page_ = Page::Register;
        UIContext::ButtonStyle s;
        s.enabled = g_.online.usable();
        if (ui.button("login", {x + bw + ui.s(20), by, bw, ui.s(52)}, "Se connecter", s)) page_ = Page::Login;
        if (ui.button("offline", {x + 2 * (bw + ui.s(20)), by, bw, ui.s(52)}, "Jouer hors ligne")) close(false);
        ui.popId();
    }

    void showError(float x, float y, float w) {
        UIContext& ui = g_.ui;
        if (busy_) {
            float a = 0.5f + 0.5f * std::sin(t_ * 6.0f);
            ui.label("Connexion au serveur…", {x, y}, 17, ui.theme().accent.withAlphaF(0.5f + 0.5f * a), Align::Left, FontFace::Body, VAlign::Middle);
        } else if (!error_.empty()) {
            ui.labelWrapped(error_, {x, y - ui.s(10)}, w, 16, ui.theme().danger);
        }
    }

    std::string fieldError(const ApiResult& r) const {
        std::string out = r.message;
        if (r.details.is_array())
            for (const auto& d : r.details) {
                std::string f = jstr(d, "field");
                std::string label = f == "username" ? "Pseudonyme" : f == "password" ? "Mot de passe" : f == "country_code" ? "Pays" : f;
                out += "\n" + label + " : " + jstr(d, "message");
            }
        return out;
    }

    void registerPage() {
        UIContext& ui = g_.ui;
        title("Créer un compte", "Votre identité publique dans les classements");
        float x = box_.x + ui.s(36), col = (box_.w - ui.s(96)) / 2, y = box_.y + ui.s(112);
        float x2 = x + col + ui.s(24);
        ui.pushId("account/register");
        auto caption = [&](float cx, float cy, const std::string& t) {
            ui.label(t, {cx, cy}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        };
        caption(x, y, "Pseudonyme (3 à 20 caractères : lettres, chiffres, . _ -)");
        ui.textField("user", {x, y + ui.s(14), col, ui.s(42)}, username_, "NovaRider", 20);
        caption(x, y + ui.s(76), "Mot de passe (8 caractères minimum)");
        ui.passwordField("pass", {x, y + ui.s(90), col, ui.s(42)}, password_, "••••••••");
        caption(x, y + ui.s(152), "Confirmation");
        ui.passwordField("pass2", {x, y + ui.s(166), col, ui.s(42)}, password2_, "••••••••");
        caption(x, y + ui.s(228), "Avatar");
        ui_.avatarPicker("avatar", {x, y + ui.s(242), col, col / 4.0f}, avatar_);

        caption(x2, y, "Pays (facultatif)");
        UIContext::ButtonStyle cs;
        cs.align = Align::Left;
        std::string cl = country_.empty() ? "Aucun (non affiché)" : "      " + ui_.countryName(country_);
        if (ui.button("country", {x2, y + ui.s(14), col, ui.s(42)}, cl, cs)) page_ = Page::Country;
        if (!country_.empty()) ui_.drawFlag(country_, {x2 + ui.s(14), y + ui.s(26), ui.s(24), ui.s(18)});
        caption(x2, y + ui.s(76), "Visibilité géographique");
        int vis = country_.empty() ? 0 : locVis_;
        if (ui.cycle("locvis", {x2, y + ui.s(90), col, ui.s(42)}, "Pays", vis, {"Masqué", "Classements", "Classements et globe"}) && !country_.empty())
            locVis_ = vis;
        ui.toggle("public", {x2, y + ui.s(146), col, ui.s(38)}, "Profil public", profilePublic_);
        ui.toggle("friends", {x2, y + ui.s(190), col, ui.s(38)}, "Accepter les demandes d'amis", allowFriends_);
        ui.toggle("remember", {x2, y + ui.s(234), col, ui.s(38)}, "Connexion automatique", remember_);
        ui.toggle("tos", {x2, y + ui.s(278), col - ui.s(150), ui.s(38)}, "J'accepte les conditions", tos_);
        UIContext::ButtonStyle small;
        small.small = true;
        if (ui.button("terms", {x2 + col - ui.s(140), y + ui.s(280), ui.s(140), ui.s(34)}, "Lire", small)) page_ = Page::Terms;
        ui.labelWrapped("Seul le pays choisi est utilisé, affiché au centre du pays. Aucune position précise n'est jamais collectée.",
                        {x2, y + ui.s(326)}, col, 14, ui.theme().textDim);

        showError(x, box_.bottom() - ui.s(108), box_.w - ui.s(72));
        float by = box_.bottom() - ui.s(70);
        UIContext::ButtonStyle p;
        p.primary = true;
        p.enabled = !busy_;
        if (ui.button("submit", {box_.right() - ui.s(316), by, ui.s(280), ui.s(50)}, "Créer le compte", p)) submitRegister();
        if (ui.button("back", {x, by, ui.s(180), ui.s(50)}, "Retour")) back();
        ui.popId();
    }

    void submitRegister() {
        error_.clear();
        if (username_.size() < 3) error_ = "Choisissez un pseudonyme d'au moins 3 caractères.";
        else if (password_.size() < 8) error_ = "Le mot de passe doit contenir au moins 8 caractères.";
        else if (password_ != password2_) error_ = "Les deux mots de passe sont différents.";
        else if (!tos_) error_ = "Vous devez accepter les conditions d'utilisation pour créer un compte.";
        if (!error_.empty()) return;
        static const char* vis[] = {"hidden", "country", "globe"};
        Json form = {{"username", username_},
                     {"password", password_},
                     {"avatar", avatar_},
                     {"location_visibility", country_.empty() ? "hidden" : vis[locVis_]},
                     {"profile_visibility", profilePublic_ ? "public" : "private"},
                     {"allow_friend_requests", allowFriends_},
                     {"accept_tos", true},
                     {"tos_version", g_.online.config().tosVersion}};
        if (!country_.empty()) form["country_code"] = country_;
        busy_ = true;
        g_.online.registerAccount(form, remember_, OnlineService::guard(life_, [this](const ApiResult& r) {
            busy_ = false;
            if (r.ok) {
                password_.clear();
                password2_.clear();
                close(true);
            } else {
                error_ = fieldError(r);
            }
        }));
    }

    void loginPage() {
        UIContext& ui = g_.ui;
        title("Se connecter", "Retrouvez vos records sur cet ordinateur");
        float x = box_.x + ui.s(36), w = ui.s(460), y = box_.y + ui.s(140);
        ui.pushId("account/login");
        ui.label("Pseudonyme", {x, y}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        ui.textField("user", {x, y + ui.s(14), w, ui.s(44)}, username_, "NovaRider", 20);
        ui.label("Mot de passe", {x, y + ui.s(82)}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        ui.passwordField("pass", {x, y + ui.s(96), w, ui.s(44)}, password_, "••••••••");
        ui.toggle("remember", {x, y + ui.s(160), w, ui.s(40)}, "Connexion automatique sur cet ordinateur", remember_);
        ui.labelWrapped("La connexion automatique conserve un jeton de session protégé par votre compte Windows (ou lisible seulement par "
                        "votre utilisateur sous Linux). Le mot de passe n'est jamais enregistré.",
                        {x + w + ui.s(40), y + ui.s(10)}, box_.right() - x - w - ui.s(76), 15, ui.theme().textDim);
        showError(x, y + ui.s(240), box_.w - ui.s(72));
        float by = box_.bottom() - ui.s(70);
        UIContext::ButtonStyle p;
        p.primary = true;
        p.enabled = !busy_ && !username_.empty() && !password_.empty();
        if (ui.button("submit", {box_.right() - ui.s(316), by, ui.s(280), ui.s(50)}, "Se connecter", p)) {
            busy_ = true;
            error_.clear();
            g_.online.login(username_, password_, remember_, OnlineService::guard(life_, [this](const ApiResult& r) {
                busy_ = false;
                if (r.ok) {
                    password_.clear();
                    close(true);
                } else {
                    error_ = r.message;
                }
            }));
        }
        if (ui.button("create", {x + ui.s(200), by, ui.s(220), ui.s(50)}, "Créer un compte")) page_ = Page::Register;
        if (ui.button("back", {x, by, ui.s(180), ui.s(50)}, "Retour")) back();
        ui.popId();
    }

    void termsPage() {
        UIContext& ui = g_.ui;
        title("Conditions d'utilisation", "Version " + g_.online.config().tosVersion);
        Rect view{box_.x + ui.s(36), box_.y + ui.s(110), box_.w - ui.s(72), box_.h - ui.s(200)};
        float h = ui.text().measureWrapped(terms_, FontFace::Body, ui.s(16), view.w - ui.s(20)).y + ui.s(20);
        ui.pushId("account/terms");
        ui.beginScroll("text", view, h);
        ui.labelWrapped(terms_, {view.x, view.y - ui.scrollOffset()}, view.w - ui.s(20), 16, ui.theme().text);
        ui.endScroll();
        UIContext::ButtonStyle p;
        p.primary = true;
        if (ui.button("accept", {box_.right() - ui.s(316), box_.bottom() - ui.s(70), ui.s(280), ui.s(50)}, "J'accepte", p)) {
            tos_ = true;
            page_ = Page::Register;
        }
        if (ui.button("back", {box_.x + ui.s(36), box_.bottom() - ui.s(70), ui.s(180), ui.s(50)}, "Retour")) page_ = Page::Register;
        ui.popId();
    }

    void countryPage() {
        UIContext& ui = g_.ui;
        title("Pays", "Facultatif : affiché dans les classements selon votre choix");
        Rect area{box_.x + ui.s(36), box_.y + ui.s(110), box_.w - ui.s(72), box_.h - ui.s(200)};
        if (ui_.countryPicker("account/countries", area, countryFilter_, country_)) {
            if (locVis_ == 0) locVis_ = 1;
            page_ = Page::Register;
        }
        ui.pushId("account/country");
        if (ui.button("none", {box_.right() - ui.s(316), box_.bottom() - ui.s(70), ui.s(280), ui.s(50)}, "Ne pas indiquer de pays")) {
            country_.clear();
            locVis_ = 0;
            page_ = Page::Register;
        }
        if (ui.button("back", {box_.x + ui.s(36), box_.bottom() - ui.s(70), ui.s(180), ui.s(50)}, "Retour")) page_ = Page::Register;
        ui.popId();
    }

    GameServices& g_;
    OnlineUi ui_;
    Page page_;
    Rect box_;
    float t_ = 0.0f;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string terms_;
    std::string username_, password_, password2_;
    std::string avatar_ = "avatar_01";
    std::string country_, countryFilter_;
    int locVis_ = 0;
    bool profilePublic_ = true, allowFriends_ = true, remember_ = true, tos_ = false;
    bool busy_ = false;
    std::string error_;
};

}  // namespace

std::unique_ptr<Scene> makeOnlineAccount(GameServices& g, bool onboarding) { return std::make_unique<OnlineAccountScene>(g, onboarding); }

}  // namespace cosmic
