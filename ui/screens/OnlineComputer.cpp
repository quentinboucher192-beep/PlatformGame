#include "ui/screens/OnlineComputer.h"

#include <algorithm>

#include "engine/core/Scene.h"
#include "game/GameServices.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/ChallengeUi.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

/// "2026-11-16T15:20:31Z" -> « 16 novembre · 15:20 » (UTC).
std::string stamp(const std::string& iso) {
    if (iso.size() < 16) return iso;
    return challenge_ui::frenchDate(iso.substr(0, 10), false) + " · " + iso.substr(11, 5);
}

std::string plural(int n, const std::string& one, const std::string& many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

}  // namespace

OnlineComputerTab::OnlineComputerTab(GameServices& g, OnlineUi& ui, std::function<void(const std::string&, bool)> say)
    : g_(g), ui_(ui), say_(std::move(say)) {}

void OnlineComputerTab::enter() {
    name_ = g_.online.device().name;
    if (g_.online.loggedIn()) load();
}

void OnlineComputerTab::load() {
    if (loading_ || !g_.online.usable()) return;
    loading_ = true;
    g_.online.get("/api/v1/players/me/device", OnlineService::guard(life_, [this](const ApiResult& r) {
                      loading_ = false;
                      if (!r.ok) return say_(r.message, true);
                      data_ = r.data;
                      const Json& d = jfield(data_, "device", Json());
                      if (d.is_object() && !jstr(d, "name").empty()) name_ = jstr(d, "name");
                  }),
                  false);
}

void OnlineComputerTab::draw(const Rect& c) {
    UIContext& ui = g_.ui;
    ui.pushId("computer");
    const DeviceIdentity& dev = g_.online.device();
    const float gap = ui.s(14);
    float leftW = c.w * 0.58f, x = c.x, y = c.y;
    // ----- the computer and its account
    ui.label(g_.online.loggedIn() ? "Compte lié à cet ordinateur" : "Cet ordinateur", {x, y + ui.s(11)}, 19, ui.theme().text, Align::Left,
             FontFace::Bold, VAlign::Middle);
    y += ui.s(30);
    std::string info;
    if (g_.online.loggedIn()) {
        const Json& d = jfield(data_, "device", Json());
        info = "« " + g_.online.username() + " » ne s'utilise que sur cet ordinateur, et aucun autre compte ne peut s'y connecter ni y être créé.";
        if (d.is_object() && !jstr(d, "bound_at").empty()) info = "Lié depuis le " + stamp(jstr(d, "bound_at")) + ". " + info;
    } else if (!dev.boundUsername.empty()) {
        info = "Cet ordinateur est lié au compte « " + dev.boundUsername + " » : seul ce compte peut s'y connecter. Déconnecté, vous jouez en local ; "
               "à la reconnexion, tout est rechargé dans le compte.";
    } else {
        info = "Aucun compte n'est lié à cet ordinateur : le premier compte créé ou connecté ici le sera, et ne pourra plus servir ailleurs.";
    }
    y += ui.labelWrapped(info, {x, y}, leftW, 16, ui.theme().textDim).y + gap;
    ui.label("Nom de l'ordinateur", {x, y + ui.s(8)}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    y += ui.s(20);
    float bw = ui.s(150);
    ui.textField("name", {x, y, leftW - bw - ui.s(10), ui.s(44)}, name_, "Mon ordinateur", 40);
    UIContext::ButtonStyle small;
    small.small = true;
    small.enabled = !busy_ && !DeviceIdentity::cleanName(name_).empty() && DeviceIdentity::cleanName(name_) != dev.name;
    if (ui.button("rename", {x + leftW - bw, y, bw, ui.s(44)}, "Renommer", small)) {
        busy_ = true;
        g_.online.renameDevice(name_, OnlineService::guard(life_, [this](const ApiResult& r) {
            busy_ = false;
            name_ = g_.online.device().name;
            if (r.ok || r.code == "OFFLINE") say_("Ordinateur renommé « " + name_ + " ».", false);
            else say_(r.message, true);
        }));
    }
    y += ui.s(44) + gap * 1.5f;

    if (g_.online.loggedIn()) {
        // ----- local and online
        ui.label("Une seule progression sur cet ordinateur", {x, y + ui.s(11)}, 18, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        y += ui.s(28);
        y += ui.labelWrapped("Déconnecté, vous jouez en local : la progression continue ici. À la reconnexion, tout est rechargé dans le compte "
                             "(succès, temps en attente, tenue) et une copie de la progression est envoyée.",
                             {x, y}, leftW, 15, ui.theme().textDim)
                 .y +
             ui.s(8);
        UIContext::ButtonStyle out;
        out.small = true;
        out.enabled = !busy_;
        float ow = std::min(leftW, ui.text().measure("Se déconnecter (jouer en local)", FontFace::Bold, ui.s(18)).x + ui.s(48));
        if (ui.button("logout", {x, y, ow, ui.s(42)}, "Se déconnecter (jouer en local)", out)) {
            busy_ = true;
            g_.online.logout(false, OnlineService::guard(life_, [this](const ApiResult&) {
                busy_ = false;
                data_ = Json();
                say_("Vous jouez maintenant en local : à la prochaine connexion, ce que vous ferez sera rechargé dans le compte.", false);
            }));
        }
        y += ui.s(42) + gap * 1.5f;
        // ----- the copy of the progression
        ui.label("Copie en ligne de la progression", {x, y + ui.s(11)}, 18, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        y += ui.s(28);
        const Json& copy = jfield(data_, "copy", Json());
        std::string ct;
        if (copy.is_object()) {
            const Json& s = jfield(copy, "summary", Json::object());
            ct = "Dernière copie : " + stamp(jstr(copy, "created_at")) + " (" + plural(jget<int>(s, "levels", 0), "niveau fini", "niveaux finis") + ", " +
                 plural(jget<int>(s, "achievements", 0), "succès", "succès") + "). Envoyée après les parties, au plus toutes les 10 minutes.";
        } else {
            ct = loading_ ? std::string("Lecture…") : std::string("Aucune copie pour l'instant : elle part après la prochaine partie.");
        }
        y += ui.labelWrapped(ct, {x, y}, leftW, 15, ui.theme().textDim).y + ui.s(8);
        UIContext::ButtonStyle send;
        send.small = true;
        send.enabled = !g_.progressCopyBusy_ && g_.online.usable();
        float sw = std::min(leftW, ui.text().measure("Envoyer une copie maintenant", FontFace::Bold, ui.s(18)).x + ui.s(48));
        if (ui.button("copy", {x, y, sw, ui.s(42)}, "Envoyer une copie maintenant", send))
            g_.sendProgressCopy([this, life = std::weak_ptr<int>(life_)](const ApiResult& r) {
                if (life.expired()) return;
                if (r.ok) {
                    say_("Copie de la progression envoyée.", false);
                    load();
                } else {
                    say_(r.message.empty() ? std::string("Copie impossible : connectez-vous à Cosmic Online.") : r.message, true);
                }
            });

        // ----- security alerts (right column)
        float rx = c.x + leftW + ui.s(30), rw = c.right() - rx, ry = c.y;
        ui.label("Alertes de sécurité", {rx, ry + ui.s(11)}, 19, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        ry += ui.s(34);
        const Json& events = jfield(data_, "events", Json::array());
        if (!events.is_array() || events.empty()) ui.labelWrapped(loading_ ? "Lecture…" : "Aucune alerte.", {rx, ry}, rw, 15, ui.theme().textDim);
        else
            for (const Json& e : events) {
                if (ry > c.bottom() - ui.s(40)) break;
                const bool refused = jstr(e, "kind") == "refused";
                std::string t = refused ? "Connexion refusée depuis un autre ordinateur" : "Compte lié à « " + jstr(e, "device_name", "cet ordinateur") + " »";
                if (refused && !jstr(e, "country").empty()) t += " (" + ui_.countryName(jstr(e, "country")) + ")";
                ui.r().fillCircle({rx + ui.s(5), ry + ui.s(10)}, ui.s(4), refused ? ui.theme().warm : ui.theme().accent);
                float h = ui.labelWrapped(t, {rx + ui.s(18), ry}, rw - ui.s(18), 15, ui.theme().text).y;
                ry += h + ui.s(2);
                ui.label(stamp(jstr(e, "at")), {rx + ui.s(18), ry + ui.s(9)}, 13, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
                ry += ui.s(28);
            }
    }
    ui.popId();
}

// ------------------------------------------------------------------ « De retour en ligne »

namespace {

class ReconnectSummaryScene : public Scene {
public:
    explicit ReconnectSummaryScene(GameServices& g) : g_(g), summary_(g.reconnectSummary) { g.reconnectSummary = Json(); }
    bool isOverlay() const override { return true; }
    void onEnter() override { g_.ui.setFocus("reconnect/ok"); }
    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 200});
        Rect box{ui.screenW() * 0.5f - ui.s(430), ui.screenH() * 0.5f - ui.s(250), ui.s(860), ui.s(500)};
        ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
        ui.r().drawRoundedRect(box, ui.s(18), ui.theme().accent.withAlphaF(0.55f), ui.s(1.5f));
        float x = box.x + ui.s(40), w = box.w - ui.s(80), y = box.y + ui.s(34);
        ui.label("DE RETOUR EN LIGNE", {x, y + ui.s(14)}, 28, ui.theme().accent, Align::Left, FontFace::Title, VAlign::Middle);
        y += ui.s(48);
        y += ui.labelWrapped("Pendant la déconnexion, vous avez joué en local. C'est maintenant dans le compte « " + g_.online.username() + " » :",
                             {x, y}, w, 18, ui.theme().text)
                 .y +
             ui.s(18);
        struct Cell {
            std::string value, label;
        };
        const int minutes = jget<int>(summary_, "minutes", 0);
        Cell cells[4] = {{std::to_string(jget<int>(summary_, "levels", 0)), jget<int>(summary_, "levels", 0) > 1 ? "niveaux finis" : "niveau fini"},
                         {std::to_string(jget<int>(summary_, "runs", 0)), "temps à envoyer aux classements"},
                         {std::to_string(jget<int>(summary_, "achievements", 0)), "succès (+" + std::to_string(jget<int>(summary_, "stars", 0)) + " étoiles)"},
                         {minutes >= 60 ? std::to_string(minutes / 60) + " h " + std::to_string(minutes % 60) : std::to_string(minutes) + " min", "de jeu"}};
        const float cw = (w - ui.s(36)) / 4.0f, ch = ui.s(110);
        for (int i = 0; i < 4; ++i) {
            Rect r{x + i * (cw + ui.s(12)), y, cw, ch};
            ui.r().fillRoundedRect(r, ui.s(12), Color{8, 11, 24, 200});
            ui.r().drawRoundedRect(r, ui.s(12), ui.theme().border, ui.s(1));
            float fs = ui.fitFont(cells[i].value, FontFace::Title, 30, cw - ui.s(24));
            ui.label(cells[i].value, {r.x + ui.s(14), r.y + ui.s(30)}, fs, i == 2 ? ui.theme().warm : ui.theme().accent, Align::Left, FontFace::Title,
                     VAlign::Middle);
            ui.labelWrapped(cells[i].label, {r.x + ui.s(14), r.y + ui.s(56)}, cw - ui.s(28), 14, ui.theme().textDim);
        }
        y += ch + ui.s(18);
        ui.labelWrapped("Les temps passent par la vérification habituelle avant d'entrer dans les classements. Une copie de la progression part "
                        "dans quelques secondes.",
                        {x, y}, w, 15, ui.theme().textDim);
        ui.pushId("reconnect");
        UIContext::ButtonStyle p;
        p.primary = true;
        if (ui.button("ok", {box.right() - ui.s(240), box.bottom() - ui.s(76), ui.s(200), ui.s(50)}, "Continuer", p) || ui.backPressed()) {
            ui.popId();
            ui.end();
            g_.app.popScene();
            return;
        }
        ui.popId();
        ui.end();
    }

private:
    GameServices& g_;
    Json summary_;
};

}  // namespace

std::unique_ptr<Scene> makeReconnectSummary(GameServices& g) { return std::make_unique<ReconnectSummaryScene>(g); }

}  // namespace cosmic
