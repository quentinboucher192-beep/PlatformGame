#include "ui/screens/OnlineMigration.h"

#include <algorithm>
#include <functional>
#include <string>

#include "engine/core/Scene.h"
#include "game/GameServices.h"
#include "ui/screens/ChallengeUi.h"

namespace cosmic {

namespace {

/// "2026-11-16T15:20:31Z" -> « 16 novembre · 15:20 » (UTC).
std::string stamp(const std::string& iso) {
    if (iso.size() < 16) return iso;
    return challenge_ui::frenchDate(iso.substr(0, 10), false) + " · " + iso.substr(11, 5);
}

/// The frame of the three windows: a dark veil, a box centred, its title; returns the inside.
Rect modalFrame(UIContext& ui, float w, float h, const std::string& kicker, const std::string& title, Color accent) {
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 205});
    w = std::min(w, ui.screenW() - ui.s(40));
    h = std::min(h, ui.screenH() - ui.s(40));
    Rect box{ui.screenW() * 0.5f - w * 0.5f, ui.screenH() * 0.5f - h * 0.5f, w, h};
    ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
    ui.r().drawRoundedRect(box, ui.s(18), accent.withAlphaF(0.6f), ui.s(1.5f));
    float x = box.x + ui.s(36), cw = box.w - ui.s(72), y = box.y + ui.s(28);
    ui.label(ui.elide(kicker, FontFace::Bold, 14, cw), {x, y + ui.s(8)}, 14, accent, Align::Left, FontFace::Bold, VAlign::Middle);
    y += ui.s(24);
    const float fs = ui.fitFont(title, FontFace::Title, 28, cw);
    ui.label(title, {x, y + ui.s(16)}, fs, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
    y += ui.s(46);
    return {x, y, cw, box.bottom() - y - ui.s(24)};
}

/// Two buttons at the bottom right of the inside (the second the primary); 1 or 2 when pressed.
int footer(UIContext& ui, const Rect& in, const std::string& a, const std::string& b, bool bEnabled, bool aEnabled = true) {
    const float bh = ui.s(50), gap = ui.s(14);
    float bw = std::max(ui.text().measure(b, FontFace::Bold, ui.s(20)).x, ui.text().measure(a, FontFace::Bold, ui.s(20)).x) + ui.s(48);
    bw = std::min(bw, (in.w - gap) * 0.5f);
    const float y = in.bottom() - bh;
    int pressed = 0;
    UIContext::ButtonStyle sa;
    sa.enabled = aEnabled;
    sa.fontSize = ui.fitFont(a, FontFace::Bold, 20, bw - ui.s(24));
    if (!a.empty() && ui.button("a", {in.right() - bw * 2 - gap, y, bw, bh}, a, sa)) pressed = 1;
    UIContext::ButtonStyle sb;
    sb.primary = true;
    sb.enabled = bEnabled;
    sb.fontSize = ui.fitFont(b, FontFace::Bold, 20, bw - ui.s(24));
    if (!b.empty() && ui.button("b", {in.right() - bw, y, bw, bh}, b, sb)) pressed = 2;
    return pressed;
}

// ------------------------------------------------------------------ 1. on the old computer

class MigrationPrepareScene : public Scene {
public:
    explicit MigrationPrepareScene(GameServices& g) : g_(g) {}
    bool isOverlay() const override { return true; }
    void onEnter() override {
        if (!g_.online.loggedIn() || !g_.online.usable()) {
            fail("Connectez-vous à Cosmic Online pour préparer une migration.");
            return;
        }
        // The copy first: the new account receives the progression as it is now.
        g_.sendProgressCopy([this, life = std::weak_ptr<int>(life_)](const ApiResult& r) {
            if (life.expired()) return;
            if (!r.ok) return fail(r.message.empty() ? std::string("La copie de la progression n'a pas pu partir.") : r.message);
            g_.online.send("POST", "/api/v1/players/me/migration", Json::object(), OnlineService::guard(life_, [this](const ApiResult& c) {
                               if (!c.ok) return fail(c.message);
                               code_ = jstr(c.data, "code");
                               expires_ = jstr(c.data, "expires_at");
                               state_ = State::Shown;
                           }));
        });
    }
    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        Rect in = modalFrame(ui, ui.s(820), ui.s(560), "SUR L'ANCIEN ORDINATEUR (" + g_.online.username() + ")", "PRÉPARER UNE MIGRATION",
                             ui.theme().warm);
        ui.pushId("migprep");
        float y = in.y;
        if (state_ == State::Working) {
            ui.labelWrapped("Envoi d'une copie de la progression, puis création du code…", {in.x, y}, in.w, 18, ui.theme().textDim);
        } else if (state_ == State::Error) {
            ui.labelWrapped(error_, {in.x, y}, in.w, 18, ui.theme().danger);
        } else if (state_ == State::Cancelled) {
            ui.labelWrapped("Migration annulée : le code ne fonctionne plus.", {in.x, y}, in.w, 18, ui.theme().text);
        } else {
            y += ui.labelWrapped("Une copie de la progression vient d'être envoyée. Ce code permet à un nouveau compte, sur un autre ordinateur, "
                                 "de récupérer toutes vos données.",
                                 {in.x, y}, in.w, 17, ui.theme().text)
                     .y +
                 ui.s(16);
            ui.label("Code de migration", {in.x, y + ui.s(8)}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            y += ui.s(22);
            Rect codeBox{in.x, y, in.w, ui.s(84)};
            ui.r().fillRoundedRect(codeBox, ui.s(12), Color{6, 9, 20, 230});
            ui.r().drawRoundedRect(codeBox, ui.s(12), ui.theme().warm.withAlphaF(0.7f), ui.s(1.5f));
            const float fs = ui.fitFont(code_, FontFace::Title, 44, codeBox.w - ui.s(40));
            ui.label(code_, codeBox.center(), fs, ui.theme().warm, Align::Center, FontFace::Title, VAlign::Middle);
            y += codeBox.h + ui.s(10);
            const std::string valid = "Valable 24 h · une seule fois" + (expires_.empty() ? std::string() : " · jusqu'au " + stamp(expires_));
            y += ui.labelWrapped(valid, {in.x, y}, in.w, 15, ui.theme().textDim).y + ui.s(8);
            ui.labelWrapped("Ne le donnez à personne. Il faut aussi le mot de passe de " + g_.online.username() + ".", {in.x, y}, in.w, 15,
                            ui.theme().warm);
        }
        const int b = footer(ui, in, state_ == State::Shown ? "Annuler la migration" : "", "Fermer", true, !busy_);
        if (b == 1 && !busy_) {
            busy_ = true;
            g_.online.send("DELETE", "/api/v1/players/me/migration", Json::object(), OnlineService::guard(life_, [this](const ApiResult& r) {
                               busy_ = false;
                               if (r.ok) state_ = State::Cancelled;
                               else fail(r.message);
                           }));
        }
        ui.popId();
        if (b == 2 || ui.backPressed()) {
            ui.end();
            g_.app.popScene();
            return;
        }
        ui.end();
    }

private:
    enum class State { Working, Shown, Cancelled, Error };
    void fail(const std::string& m) {
        error_ = m.empty() ? std::string("Cosmic Online ne répond pas : réessayez plus tard.") : m;
        state_ = State::Error;
    }
    GameServices& g_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    State state_ = State::Working;
    std::string code_, expires_, error_;
    bool busy_ = false;
};

// ------------------------------------------------------------------ 2. on the new computer

class MigrationRecoverScene : public Scene {
public:
    explicit MigrationRecoverScene(GameServices& g) : g_(g) {}
    bool isOverlay() const override { return true; }
    void onEnter() override { g_.ui.setFocus("migrec/user"); }
    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        Rect in = modalFrame(ui, ui.s(820), ui.s(640), "SUR CET ORDINATEUR, AVEC LE COMPTE " + g_.online.username(), "RÉCUPÉRER UN ANCIEN COMPTE",
                             ui.theme().accent);
        ui.pushId("migrec");
        float y = in.y;
        const float fh = ui.s(44);
        auto field = [&](const char* id, const std::string& label, std::string& value, size_t maxLen, bool secret) {
            ui.label(label, {in.x, y + ui.s(8)}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            y += ui.s(20);
            if (secret) ui.passwordField(id, {in.x, y, in.w, fh}, value, label);
            else ui.textField(id, {in.x, y, in.w, fh}, value, label, maxLen);
            y += fh + ui.s(10);
        };
        field("user", "Ancien pseudonyme", user_, 20, false);
        field("pass", "Son mot de passe", pass_, 128, true);
        field("code", "Code de migration", code_, 13, false);
        const std::string who = user_.empty() ? std::string("l'ancien compte") : user_;
        y += ui.labelWrapped("Passent sur " + g_.online.username() + " : records et replays, succès, récompenses, temps joué, et la copie de la "
                             "progression (rechargée sur ce PC). Le meilleur des deux côtés est gardé si " + g_.online.username() +
                                 " a déjà joué.",
                             {in.x, y}, in.w, 15, ui.theme().textDim)
                 .y +
             ui.s(4);
        y += ui.labelWrapped(who + " est ensuite fermé ; son pseudonyme se libère après 30 jours.", {in.x, y}, in.w, 15, ui.theme().warm).y +
             ui.s(6);
        if (!message_.empty()) ui.labelWrapped(message_, {in.x, y}, in.w, 16, error_ ? ui.theme().danger : ui.theme().success);
        const bool ready = !busy_ && !done_ && user_.size() >= 3 && !pass_.empty() && code_.size() >= 9;
        const int b = footer(ui, in, done_ ? "" : "Annuler", done_ ? "Fermer" : "Migrer les données", done_ || ready, !busy_);
        ui.popId();
        if (b == 2 && !done_) submit();
        else if (b == 2 || ((b == 1 || ui.backPressed()) && !busy_)) {
            ui.end();
            g_.app.popScene();
            if (done_ && restore_.is_object()) g_.app.pushScene(makeProgressRestore(g_, restore_));
            return;
        }
        ui.end();
    }

private:
    void submit() {
        busy_ = true;
        message_ = "Migration en cours…";
        error_ = false;
        g_.online.send("POST", "/api/v1/players/me/migrate-from", Json{{"username", user_}, {"password", pass_}, {"code", code_}},
                       OnlineService::guard(life_, [this](const ApiResult& r) {
                           busy_ = false;
                           pass_.clear();
                           if (!r.ok) {
                               error_ = true;
                               message_ = r.message.empty() ? std::string("Cosmic Online ne répond pas : réessayez plus tard.") : r.message;
                               return;
                           }
                           done_ = true;
                           message_ = "Les données de " + user_ + " sont maintenant sur " + g_.online.username() + ".";
                           g_.online.get("/api/v1/players/me/progress-restore", OnlineService::guard(life_, [this](const ApiResult& p) {
                                             if (p.ok) restore_ = jfield(p.data, "restore", Json());
                                         }),
                                         false);
                       }),
                       60000);
    }
    GameServices& g_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string user_, pass_, code_, message_;
    bool busy_ = false, done_ = false, error_ = false;
    Json restore_;
};

// ------------------------------------------------------------------ 3. the progression transferred

class ProgressRestoreScene : public Scene {
public:
    ProgressRestoreScene(GameServices& g, Json restore) : g_(g), restore_(std::move(restore)) {}
    bool isOverlay() const override { return true; }
    void onEnter() override { g_.ui.setFocus("restore/b"); }
    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        Rect in = modalFrame(ui, ui.s(860), ui.s(560), "PROGRESSION MIGRÉE", "RESTAURER LA PROGRESSION TRANSFÉRÉE", ui.theme().accent);
        ui.pushId("restore");
        float y = in.y;
        const Json& copy = jfield(restore_, "copy", Json::object());
        const Json& s = jfield(copy, "summary", Json::object());
        y += ui.labelWrapped("Les données de " + jstr(restore_, "from_username", "l'ancien compte") + " ont été migrées sur " + g_.online.username() +
                                 " (copie de la progression du " + stamp(jstr(copy, "created_at")) + ").",
                             {in.x, y}, in.w, 17, ui.theme().text)
                 .y +
             ui.s(16);
        const int64_t secs = jget<int64_t>(s, "play_seconds", 0);
        struct Cell {
            std::string value, label;
        };
        const int levels = jget<int>(s, "levels", 0), ach = jget<int>(s, "achievements", 0), bosses = jget<int>(s, "bosses", 0);
        Cell cells[4] = {{std::to_string(levels), levels > 1 ? "niveaux finis" : "niveau fini"},
                         {std::to_string(ach), "succès"},
                         {std::to_string(bosses), bosses > 1 ? "boss battus" : "boss battu"},
                         {secs >= 3600 ? std::to_string(secs / 3600) + " h" : std::to_string(secs / 60) + " min", "de jeu"}};
        const float cw = (in.w - ui.s(36)) / 4.0f, ch = ui.s(100);
        for (int i = 0; i < 4; ++i) {
            Rect r{in.x + i * (cw + ui.s(12)), y, cw, ch};
            ui.r().fillRoundedRect(r, ui.s(12), Color{8, 11, 24, 200});
            ui.r().drawRoundedRect(r, ui.s(12), ui.theme().border, ui.s(1));
            const float fs = ui.fitFont(cells[i].value, FontFace::Title, 30, cw - ui.s(24));
            ui.label(cells[i].value, {r.x + ui.s(14), r.y + ui.s(30)}, fs, ui.theme().accent, Align::Left, FontFace::Title, VAlign::Middle);
            ui.labelWrapped(cells[i].label, {r.x + ui.s(14), r.y + ui.s(56)}, cw - ui.s(28), 14, ui.theme().textDim);
        }
        y += ch + ui.s(16);
        y += ui.labelWrapped("Elle s'ajoute à la progression de " + g_.online.username() + " sur cet ordinateur (le meilleur de chaque côté est "
                             "gardé). Une copie de la progression actuelle est faite avant.",
                             {in.x, y}, in.w, 15, ui.theme().textDim)
                 .y +
             ui.s(8);
        if (!message_.empty()) ui.labelWrapped(message_, {in.x, y}, in.w, 16, error_ ? ui.theme().danger : ui.theme().success);
        const int b = footer(ui, in, done_ ? "" : "Plus tard", done_ ? "Fermer" : "Restaurer", !busy_, !busy_);
        ui.popId();
        if (b == 2 && !done_) restore();
        else if (b == 1 || b == 2 || (ui.backPressed() && !busy_)) {
            if (!done_) g_.online.send("POST", "/api/v1/players/me/progress-restore", Json{{"action", "later"}}, [](const ApiResult&) {});
            ui.end();
            g_.app.popScene();
            return;
        }
        ui.end();
    }

private:
    void restore() {
        busy_ = true;
        error_ = false;
        message_ = "Téléchargement de la progression…";
        g_.online.get("/api/v1/players/me/progress-restore/file", OnlineService::guard(life_, [this](const ApiResult& r) {
                          busy_ = false;
                          if (!r.ok) {
                              error_ = true;
                              message_ = r.message.empty() ? std::string("Cosmic Online ne répond pas : réessayez plus tard.") : r.message;
                              return;
                          }
                          // The copy as the old game sent it: {format, game_version, summary, files}.
                          const Json& body = r.data.contains("files") ? r.data : jfield(r.data, "copy", Json::object());
                          std::string err;
                          if (!g_.restoreProgressCopy(jfield(body, "files", Json()), &err)) {
                              error_ = true;
                              message_ = err;
                              return;
                          }
                          done_ = true;
                          message_ = "Progression restaurée : elle s'ajoute à celle de cet ordinateur.";
                          g_.online.send("POST", "/api/v1/players/me/progress-restore", Json{{"action", "done"}}, [](const ApiResult&) {});
                      }),
                      false);
    }
    GameServices& g_;
    Json restore_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string message_;
    bool busy_ = false, done_ = false, error_ = false;
};

}  // namespace

std::unique_ptr<Scene> makeMigrationPrepare(GameServices& g) { return std::make_unique<MigrationPrepareScene>(g); }
std::unique_ptr<Scene> makeMigrationRecover(GameServices& g) { return std::make_unique<MigrationRecoverScene>(g); }
std::unique_ptr<Scene> makeProgressRestore(GameServices& g, const Json& restore) { return std::make_unique<ProgressRestoreScene>(g, restore); }

}  // namespace cosmic
