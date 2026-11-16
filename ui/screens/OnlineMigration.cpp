#include "ui/screens/OnlineMigration.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <functional>
#include <string>

#include "engine/core/Scene.h"
#include "game/GameServices.h"
#include "game/online/AccountMigration.h"
#include "game/online/HttpTransport.h"
#include "game/online/OnlineSession.h"
#include "ui/screens/ChallengeUi.h"

namespace cosmic {

namespace {

/// "2026-11-16T15:20:31.000Z" -> « 16 novembre · 16:20 » (the local time of this computer).
std::string stamp(const std::string& iso) {
    auto t = parseIsoUtc(iso);
    if (!t) return iso.size() >= 10 ? challenge_ui::frenchDate(iso.substr(0, 10), false) : iso;
    std::time_t tt = std::chrono::system_clock::to_time_t(*t);
    std::tm m{};
#ifdef _WIN32
    localtime_s(&m, &tt);
#else
    localtime_r(&tt, &m);
#endif
    char ymd[48], hm[32];
    std::snprintf(ymd, sizeof ymd, "%04d-%02d-%02d", m.tm_year + 1900, m.tm_mon + 1, m.tm_mday);
    std::snprintf(hm, sizeof hm, "%02d:%02d", m.tm_hour, m.tm_min);
    return challenge_ui::frenchDate(ymd, false) + " · " + hm;
}

/// The frame of the windows: a dark veil, a box centred, its title; returns the inside.
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
    ui.label(ui.elide(title, FontFace::Title, fs, cw), {x, y + ui.s(16)}, fs, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
    y += ui.s(46);
    return {x, y, cw, box.bottom() - y - ui.s(24)};
}

constexpr float kFooterH = 50.0f;

/// Where the text of a window stops: above its two buttons.
float textLimit(UIContext& ui, const Rect& in) { return in.bottom() - ui.s(kFooterH) - ui.s(14); }

/// Two buttons at the bottom right of the inside (the second the primary); 1 or 2 when pressed.
int footer(UIContext& ui, const Rect& in, const std::string& a, const std::string& b, bool bEnabled, bool aEnabled = true) {
    const float bh = ui.s(kFooterH), gap = ui.s(14);
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

/// A text at `y` that never goes past `limit`: wrapped when it fits, else one shortened line, else
/// nothing. Returns its height.
float boundedText(UIContext& ui, const std::string& t, float x, float y, float w, float limit, float size, Color c) {
    if (t.empty()) return 0;
    const float h = ui.text().measureWrapped(t, FontFace::Body, ui.s(size), w).y;
    if (y + h <= limit) return ui.labelWrapped(t, {x, y}, w, size, c).y;
    const float line = ui.s(size) * 1.3f;
    if (y + line > limit) return 0;
    ui.label(ui.elide(t, FontFace::Body, size, w), {x, y + line * 0.5f}, size, c, Align::Left, FontFace::Body, VAlign::Middle);
    return line;
}

const char* kNoAnswer = "Cosmic Online ne répond pas : réessayez plus tard.";

/// The server's French message, or a sentence when the network failed.
std::string messageOf(const ApiResult& r) { return r.message.empty() || r.networkFailure() ? std::string(kNoAnswer) : r.message; }

// ------------------------------------------------------------------ 1. on the old computer

class MigrationPrepareScene : public Scene {
public:
    explicit MigrationPrepareScene(GameServices& g) : g_(g) {}
    bool isOverlay() const override { return true; }
    void onEnter() override {
        g_.ui.setFocus("migprep/b");
        if (!g_.online.loggedIn() || !g_.online.usable()) {
            fail("Connectez-vous à Cosmic Online pour préparer une migration.");
            return;
        }
        // The copy first: the new account receives the progression as it is now.
        g_.sendProgressCopy([this, life = std::weak_ptr<int>(life_)](const ApiResult& r) {
            if (life.expired()) return;
            if (r.ok) return createCode();
            if (r.status != 429) return fail(r.message.empty() || r.networkFailure() ? std::string("La copie de la progression n'a pas pu partir : ") + kNoAnswer
                                                                                  : r.message);
            // Too many copies this hour: the last one the server has is the one transferred.
            const std::string why = r.message;
            g_.online.get("/api/v1/players/me/progress-copy", OnlineService::guard(life_, [this, why](const ApiResult& c) {
                              const Json& copy = c.ok ? jfield(c.data, "copy", Json()) : Json();
                              if (!copy.is_object()) return fail(why);
                              copyNote_ = "Une nouvelle copie de la progression n'a pas pu partir (" + why + ") : c'est celle du " +
                                          stamp(jstr(copy, "created_at")) + " qui sera transférée.";
                              createCode();
                          }),
                          false);
        });
    }
    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        Rect in = modalFrame(ui, ui.s(820), ui.s(620), "SUR L'ANCIEN ORDINATEUR (" + g_.online.username() + ")", "PRÉPARER UNE MIGRATION",
                             ui.theme().warm);
        ui.pushId("migprep");
        const float limit = textLimit(ui, in);
        float y = in.y;
        if (state_ == State::Working) {
            boundedText(ui, "Envoi d'une copie de la progression, puis création du code…", in.x, y, in.w, limit, 18, ui.theme().textDim);
        } else if (state_ == State::Error) {
            boundedText(ui, error_, in.x, y, in.w, limit, 18, ui.theme().danger);
        } else if (state_ == State::Cancelled) {
            boundedText(ui, "Migration annulée : le code ne fonctionne plus.", in.x, y, in.w, limit, 18, ui.theme().text);
        } else {
            const std::string intro = (copyNote_.empty() ? std::string("Une copie de la progression vient d'être envoyée. ") : copyNote_ + " ") +
                                      "Avec ce code, un nouveau compte, sur un autre ordinateur, récupère toutes vos données.";
            y += boundedText(ui, intro, in.x, y, in.w, limit, 17, copyNote_.empty() ? ui.theme().text : ui.theme().warm) + ui.s(14);
            ui.label("Code de migration", {in.x, y + ui.s(8)}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            y += ui.s(22);
            Rect codeBox{in.x, y, in.w, ui.s(84)};
            ui.r().fillRoundedRect(codeBox, ui.s(12), Color{6, 9, 20, 230});
            ui.r().drawRoundedRect(codeBox, ui.s(12), ui.theme().warm.withAlphaF(0.7f), ui.s(1.5f));
            const float fs = ui.fitFont(code_, FontFace::Title, 44, codeBox.w - ui.s(40));
            ui.label(code_, codeBox.center(), fs, ui.theme().warm, Align::Center, FontFace::Title, VAlign::Middle);
            y += codeBox.h + ui.s(10);
            const std::string valid = "Valable 24 h, une seule fois" + (expires_.empty() ? std::string() : " : jusqu'au " + stamp(expires_)) + ".";
            y += boundedText(ui, valid, in.x, y, in.w, limit, 15, ui.theme().textDim) + ui.s(8);
            y += boundedText(ui, "Ne le donnez à personne : avec le mot de passe de " + g_.online.username() + ", il donne toutes ses données. Une fois "
                                 "la migration faite, ce compte est fermé.",
                             in.x, y, in.w, limit, 15, ui.theme().warm) +
                 ui.s(8);
            boundedText(ui, "Sur le nouvel ordinateur, connecté au nouveau compte : PARAMÈTRES EN LIGNE › Cet ordinateur › « Récupérer un ancien "
                            "compte ».",
                        in.x, y, in.w, limit, 15, ui.theme().textDim);
        }
        const int b = footer(ui, in, state_ == State::Shown ? "Annuler la migration" : "", "Fermer", true, !busy_);
        if (b == 1 && !busy_) {
            busy_ = true;
            g_.online.send("DELETE", "/api/v1/players/me/migration", Json::object(), OnlineService::guard(life_, [this](const ApiResult& r) {
                               busy_ = false;
                               if (r.ok) state_ = State::Cancelled;
                               else fail(messageOf(r));
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
    void createCode() {
        g_.online.send("POST", "/api/v1/players/me/migration", Json::object(), OnlineService::guard(life_, [this](const ApiResult& c) {
                           if (!c.ok) return fail(messageOf(c));
                           code_ = jstr(c.data, "code");
                           expires_ = jstr(c.data, "expires_at");
                           if (code_.empty()) return fail("Réponse du serveur incomplète : réessayez.");
                           state_ = State::Shown;
                       }));
    }
    void fail(const std::string& m) {
        error_ = m.empty() ? std::string(kNoAnswer) : m;
        state_ = State::Error;
    }
    GameServices& g_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    State state_ = State::Working;
    std::string code_, expires_, error_, copyNote_;
    bool busy_ = false;
};

// ------------------------------------------------------------------ 2. on the new computer

/// What the player typed as a code: case, spaces and dashes ignored (as the server reads it).
std::string normalizedCode(const std::string& raw) {
    std::string out;
    for (char c : raw)
        if (c != ' ' && c != '-' && c != '\t') out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

class MigrationRecoverScene : public Scene {
public:
    explicit MigrationRecoverScene(GameServices& g) : g_(g) {}
    bool isOverlay() const override { return true; }
    void onEnter() override { g_.ui.setFocus("migrec/user"); }
    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        Rect in = modalFrame(ui, ui.s(820), ui.s(700), "SUR CET ORDINATEUR, AVEC LE COMPTE " + g_.online.username(), "RÉCUPÉRER UN ANCIEN COMPTE",
                             ui.theme().accent);
        ui.pushId("migrec");
        const float limit = textLimit(ui, in);
        float y = in.y;
        const float fh = ui.s(44);
        auto field = [&](const char* id, const std::string& label, std::string& value, size_t maxLen, bool secret) {
            ui.label(label, {in.x, y + ui.s(8)}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            y += ui.s(20);
            if (secret) ui.passwordField(id, {in.x, y, in.w, fh}, value, label, maxLen);
            else ui.textField(id, {in.x, y, in.w, fh}, value, label, maxLen);
            y += fh + ui.s(10);
        };
        if (!done_) {
            field("user", "Pseudonyme de l'ancien compte", user_, 20, false);
            field("pass", "Son mot de passe", pass_, 128, true);
            field("code", "Code de migration (préparé sur l'ancien ordinateur)", code_, 16, false);
            const std::string who = user_.empty() ? std::string("L'ancien compte") : user_;
            y += boundedText(ui, "Passent sur " + g_.online.username() + " : records et replays, succès, récompenses, temps de jeu, et la "
                                     "progression de l'ancien ordinateur (à restaurer ici ensuite). Le meilleur des deux côtés est gardé.",
                             in.x, y, in.w, limit, 15, ui.theme().textDim) +
                 ui.s(4);
            y += boundedText(ui, who + " est ensuite fermé : plus de connexion, son pseudonyme se libère après 30 jours.", in.x, y, in.w, limit, 15,
                             ui.theme().warm) +
                 ui.s(6);
        }
        const bool sameAccount = !user_.empty() && lower(user_) == lower(g_.online.username());
        std::string note = message_;
        if (note.empty() && sameAccount) note = "C'est le compte connecté : saisissez le pseudonyme de l'ANCIEN compte.";
        if (!note.empty()) boundedText(ui, note, in.x, y, in.w, limit, 16, error_ || sameAccount ? ui.theme().danger : ui.theme().success);
        const bool ready = !busy_ && !done_ && !sameAccount && user_.size() >= 3 && !pass_.empty() && normalizedCode(code_).size() == 9;
        const bool restoreNext = done_ && restore_.is_object();
        const int b = footer(ui, in, done_ ? (restoreNext ? "Plus tard" : "") : "Annuler",
                             done_ ? (restoreNext ? "Restaurer la progression" : "Fermer") : "Migrer les données", done_ || ready, !busy_);
        ui.popId();
        if (b == 2 && !done_) submit();
        else if (b == 2 || ((b == 1 || ui.backPressed()) && !busy_)) {
            const bool restoreNow = b == 2 && restoreNext;
            ui.end();
            g_.app.popScene();
            if (restoreNow) g_.app.pushScene(makeProgressRestore(g_, restore_));
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
                               message_ = messageOf(r);
                               return;
                           }
                           done_ = true;
                           g_.ui.setFocus("migrec/b");  // the fields are gone: the next step
                           const Json& m = jfield(r.data, "migration", Json::object());
                           const std::string from = jstr(jfield(m, "source", Json::object()), "username", user_);
                           restore_ = jfield(m, "restore", Json());
                           g_.migration.setPendingRestore(restore_);
                           const std::string me = g_.online.username();
                           message_ = restore_.is_object() ? "Les données de " + from + " sont maintenant sur " + me + " ; " + from +
                                                                 " est fermé. Il reste à restaurer la progression de son ordinateur sur celui-ci."
                                                           : "Les données de " + from + " sont maintenant sur " + me + " ; " + from +
                                                                 " est fermé. Le serveur n'avait aucune copie de la progression de son ordinateur.";
                           // What the account has now: its profile, the rewards won at challenges.
                           g_.online.refreshProfile();
                           g_.challenges.refreshMine();
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
        Rect in = modalFrame(ui, ui.s(860), ui.s(620), "PROGRESSION MIGRÉE", "RESTAURER LA PROGRESSION TRANSFÉRÉE", ui.theme().accent);
        ui.pushId("restore");
        const float limit = textLimit(ui, in);
        float y = in.y;
        const Json& copy = jfield(restore_, "copy", Json::object());
        const Json& s = jfield(copy, "summary", Json::object());
        const std::string device = jstr(copy, "device_name");
        y += boundedText(ui, "Les données de " + jstr(restore_, "from_username", "l'ancien compte") + " ont été migrées sur " + g_.online.username() +
                                 ". Voici la progression de son ordinateur" + (device.empty() ? std::string() : " (« " + device + " »)") +
                                 ", copiée le " + stamp(jstr(copy, "created_at")) + ".",
                         in.x, y, in.w, limit, 17, ui.theme().text) +
             ui.s(14);
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
        if (y + ch <= limit) {
            for (int i = 0; i < 4; ++i) {
                Rect r{in.x + i * (cw + ui.s(12)), y, cw, ch};
                ui.r().fillRoundedRect(r, ui.s(12), Color{8, 11, 24, 200});
                ui.r().drawRoundedRect(r, ui.s(12), ui.theme().border, ui.s(1));
                const float fs = ui.fitFont(cells[i].value, FontFace::Title, 30, cw - ui.s(24));
                ui.label(cells[i].value, {r.x + ui.s(14), r.y + ui.s(30)}, fs, ui.theme().accent, Align::Left, FontFace::Title, VAlign::Middle);
                boundedText(ui, cells[i].label, r.x + ui.s(14), r.y + ui.s(56), cw - ui.s(28), r.bottom() - ui.s(4), 14, ui.theme().textDim);
            }
            y += ch + ui.s(14);
        }
        y += boundedText(ui, "Elle s'ajoute à la progression de cet ordinateur : le meilleur de chaque côté est gardé (meilleurs temps, succès, "
                             "statistiques des boss) et le temps de jeu s'additionne. Les sauvegardes actuelles sont d'abord copiées dans le "
                             "dossier saves.",
                         in.x, y, in.w, limit, 15, ui.theme().textDim) +
             ui.s(6);
        if (!done_)
            y += boundedText(ui, "« Plus tard » la garde sur le serveur : PARAMÈTRES EN LIGNE › Cet ordinateur › « Restaurer la progression "
                                 "transférée ».",
                             in.x, y, in.w, limit, 15, ui.theme().textDim) +
                 ui.s(6);
        if (!message_.empty()) boundedText(ui, message_, in.x, y, in.w, limit, 16, error_ ? ui.theme().danger : ui.theme().success);
        const int b = footer(ui, in, done_ ? "" : "Plus tard", done_ ? "Fermer" : "Restaurer", !busy_, !busy_);
        ui.popId();
        if (b == 2 && !done_) restore();
        else if (b == 1 || b == 2 || (ui.backPressed() && !busy_)) {
            if (!done_) later();
            ui.end();
            g_.app.popScene();
            return;
        }
        ui.end();
    }

private:
    /// « Plus tard »: the server keeps it (postponed_at); the button of « Cet ordinateur » stays.
    void later() {
        GameServices& g = g_;
        g_.online.send("POST", "/api/v1/players/me/progress-restore", Json{{"action", "later"}}, [&g](const ApiResult& r) {
            if (r.ok) g.migration.setPendingRestore(jfield(r.data, "restore", Json()));
        });
    }
    void restore() {
        busy_ = true;
        error_ = false;
        message_ = "Téléchargement de la progression…";
        // The copy as the old game sent it ({format, game_version, player_id, created_at, summary,
        // files}), served as the file itself (not in the API envelope): read like a download.
        g_.online.getBinary("/api/v1/players/me/progress-restore/file",
                            [this, life = std::weak_ptr<int>(life_)](const HttpResponse& r) {
                                if (life.expired()) return;
                                busy_ = false;
                                const RestoreDownload d = readRestoreDownload(r);
                                if (!d.ok) {
                                    error_ = true;
                                    message_ = d.error;
                                    if (d.gone) {
                                        done_ = true;  // nothing waits any more: only « Fermer »
                                        g_.migration.setPendingRestore(Json());
                                    }
                                    return;
                                }
                                std::string err, kept;
                                const auto outcome = g_.restoreProgressCopy(d.files, d.sha256, &err, &kept);
                                if (outcome == GameServices::RestoreOutcome::Failed && kept.empty()) {
                                    error_ = true;  // nothing was changed: « Restaurer » again later
                                    message_ = err;
                                    return;
                                }
                                done_ = true;
                                if (outcome == GameServices::RestoreOutcome::Failed) {
                                    // Merged in memory, a save file not written: the server keeps
                                    // the copy until a later « Restaurer » finds it restored.
                                    error_ = true;
                                    message_ = err;
                                    return;
                                }
                                message_ = outcome == GameServices::RestoreOutcome::AlreadyRestored
                                               ? std::string("Cette progression était déjà restaurée sur cet ordinateur.")
                                               : "Progression restaurée : elle s'ajoute à celle de cet ordinateur (sauvegardes d'avant : saves › " +
                                                     kept + ").";
                                // Forgotten by the server; if this answer is lost, « Restaurer » again
                                // finds it already restored and only tells the server.
                                GameServices& g = g_;
                                g_.online.send("POST", "/api/v1/players/me/progress-restore", Json{{"action", "done"}}, [&g](const ApiResult& a) {
                                    if (a.ok || a.status == 404) g.migration.setPendingRestore(Json());
                                });
                            },
                            8u << 20);
    }
    GameServices& g_;
    Json restore_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string message_;
    bool busy_ = false, done_ = false, error_ = false;
};

// ------------------------------------------------------------------ 4. ADMINISTRATION › Joueurs

/// A check box with its caption, reached like a button (keyboard, gamepad, mouse).
bool tick(UIContext& ui, const std::string& name, const Rect& r, const std::string& caption, bool on, bool enabled = true) {
    bool hovered = false;
    const bool clicked = enabled && ui.hotspot(name, r, &hovered);
    const bool focus = ui.focused(name);
    if (enabled && (hovered || focus)) ui.r().fillRoundedRect(r, ui.s(6), Color{40, 60, 100, 160});
    if (focus) ui.r().drawRoundedRect(r, ui.s(6), ui.theme().accent, ui.s(1.5f));
    const float bs = ui.s(22);
    Rect box{r.x + ui.s(6), r.center().y - bs * 0.5f, bs, bs};
    ui.r().fillRoundedRect(box, ui.s(5), on ? ui.theme().accent.withAlpha(220) : Color{20, 28, 50, 255});
    ui.r().drawRoundedRect(box, ui.s(5), on ? ui.theme().accent : Color{120, 140, 170, 255}, ui.s(1.5f));
    if (on) {
        const Color ink{8, 20, 30, 255};
        ui.r().drawLine({box.x + bs * 0.22f, box.y + bs * 0.52f}, {box.x + bs * 0.42f, box.y + bs * 0.72f}, ink, ui.s(3));
        ui.r().drawLine({box.x + bs * 0.42f, box.y + bs * 0.72f}, {box.x + bs * 0.78f, box.y + bs * 0.30f}, ink, ui.s(3));
    }
    const float tx = box.right() + ui.s(10), room = r.right() - tx - ui.s(6);
    const float fs = ui.fitFont(caption, FontFace::Body, 16, room, 0.7f);
    ui.label(ui.elide(caption, FontFace::Body, fs, room), {tx, r.center().y}, fs, enabled ? (on ? ui.theme().text : ui.theme().textDim) : Color{90, 96, 120, 255},
             Align::Left, FontFace::Body, VAlign::Middle);
    return clicked;
}

std::string trimmed(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

class AdminMigrationScene : public Scene {
public:
    AdminMigrationScene(GameServices& g, Json source) : g_(g), source_(std::move(source)) { load(); }
    bool isOverlay() const override { return true; }
    void onEnter() override { g_.ui.setFocus("admmig/to"); }
    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        Rect in = modalFrame(ui, ui.s(1000), ui.s(820), "ADMINISTRATION", "MIGRER LES DONNÉES D'UN JOUEUR", ui.theme().warm);
        ui.pushId("admmig");
        float y = in.y;
        const std::string name = jstr(source_, "username");
        // From: the account and its latest copy of the progression.
        const Json& copy = jfield(preview_, "copy", Json());
        std::string from = "Depuis « " + name + " »";
        from += copy.is_object() ? " · dernière copie de la progression : " + stamp(jstr(copy, "created_at")) +
                                       (jstr(copy, "device_name").empty() ? std::string() : " (« " + jstr(copy, "device_name") + " »)")
                                 : std::string(preview_.is_object() ? " · aucune copie de la progression" : " · lecture…");
        // The confirmation field sits above the buttons; everything else stays above it.
        const float bh = ui.s(kFooterH), fh = ui.s(44);
        const float cy = in.bottom() - bh - ui.s(14) - fh, limit = cy - ui.s(8);
        y += boundedText(ui, from, in.x, y, in.w, in.y + ui.s(48), 16, ui.theme().text) + ui.s(10);
        // To: the target account and its preview.
        const float bw = ui.s(170);
        ui.textField("to", {in.x, y, in.w - bw - ui.s(10), fh}, to_, "Pseudonyme du compte cible", 20);
        UIContext::ButtonStyle ps;
        ps.small = true;
        ps.enabled = !busy_ && !done_ && trimmed(to_).size() >= 3;
        if (ui.button("preview", {in.right() - bw, y, bw, fh}, "Aperçu", ps)) load();
        y += fh + ui.s(12);
        // The categories: a box each, with what the source has, then what the target has.
        const bool previewed = !trimmed(to_).empty() && previewTo_ == trimmed(to_);
        const Json& sc = jfield(jfield(preview_, "source", Json::object()), "counts", Json::object());
        const Json& tc = previewed ? jfield(jfield(preview_, "target", Json::object()), "counts", Json::object()) : Json::object();
        struct Item {
            const char* id;
            std::string caption;
        };
        auto n = [](const Json& c, const char* cat, const char* key) { return jget<int64_t>(jfield(c, cat, Json::object()), key, 0); };
        auto hours = [](int64_t secs) { return secs >= 3600 ? std::to_string(secs / 3600) + " h" : std::to_string(secs / 60) + " min"; };
        // What the source has; once the target is previewed, what the target has too.
        const std::string rec = std::to_string(n(sc, "records", "records")), bests = std::to_string(n(sc, "records", "bests"));
        const std::string ach = std::to_string(n(sc, "achievements", "count")), stars = std::to_string(n(sc, "achievements", "stars"));
        const std::string won = std::to_string(n(sc, "rewards", "won")), played = hours(n(sc, "playtime", "seconds"));
        const Item items[6] = {
            {"records", previewed ? "Records, meilleurs temps et replays (" + rec + " · " + bests + " meilleurs temps ; cible : " +
                                        std::to_string(n(tc, "records", "records")) + ")"
                                  : "Records, meilleurs temps et replays (" + rec + " · " + bests + " meilleurs temps)"},
            {"achievements", previewed ? "Succès déclarés (" + ach + " · " + stars + " étoiles ; cible : " + std::to_string(n(tc, "achievements", "count")) + ")"
                                       : "Succès déclarés (" + ach + " · " + stars + " étoiles)"},
            {"rewards", previewed ? "Récompenses de défis, titres et combinaisons créées (" + won + " ; cible : " + std::to_string(n(tc, "rewards", "won")) + ")"
                                  : "Récompenses de défis, titres et combinaisons créées (" + won + ")"},
            {"playtime", previewed ? "Temps joué (" + played + " ; cible : " + hours(n(tc, "playtime", "seconds")) + ")" : "Temps joué (" + played + ")"},
            {"levels", "Niveaux publiés et projets, le créateur change (" + std::to_string(n(sc, "levels", "published")) + " · " +
                           std::to_string(n(sc, "levels", "projects")) + " projets)"},
            {"progress", copy.is_object() ? "Progression locale (dernière copie) : le jeu du compte cible propose de la restaurer"
                                          : "Progression locale : aucune copie sur le serveur"},
        };
        // Nine rows of boxes and their gaps (38), then the two notes (about 54): the rows shrink to fit.
        const float rowH = std::clamp((limit - y - ui.s(38) - ui.s(54)) / 9.0f, ui.s(26), ui.s(38));
        for (int i = 0; i < 6; ++i) {
            const bool can = i != 5 || copy.is_object();
            if (tick(ui, std::string("item_") + items[i].id, {in.x, y, in.w, rowH}, items[i].caption, chosen_[i] && can, can && !done_))
                chosen_[i] = !chosen_[i];
            y += rowH + ui.s(2);
        }
        y += ui.s(8);
        if (tick(ui, "merge", {in.x, y, in.w, rowH}, "Fusionner : le meilleur de chaque côté est gardé (meilleur temps, succès additionnés)", !replace_,
                 !done_))
            replace_ = false;
        y += rowH + ui.s(2);
        if (tick(ui, "replace", {in.x, y, in.w, rowH}, "Remplacer : les données du compte cible sont remplacées", replace_, !done_)) replace_ = true;
        y += rowH + ui.s(8);
        if (tick(ui, "close", {in.x, y, in.w, rowH}, "Fermer le compte source ensuite (connexion impossible, pseudonyme libéré dans 30 jours)", close_,
                 !done_))
            close_ = !close_;
        y += rowH + ui.s(8);
        y += boundedText(ui,
                         "Une sauvegarde complète du serveur est faite juste avant. Chaque migration est inscrite au Journal et les deux joueurs "
                         "reçoivent une notification.",
                         in.x, y, in.w, limit, 14, ui.theme().textDim) +
             ui.s(6);
        // What refuses it, what both accounts share, or the result.
        std::vector<std::string> blocking;
        if (previewed) blocking = migrationBlockingProblems(jfield(preview_, "problems", Json::array()), close_);
        if (close_ && jstr(source_, "id") == g_.online.playerId()) blocking.push_back("OWN_ACCOUNT");
        std::string note = message_;
        bool noteBad = error_;
        if (note.empty()) {
            noteBad = true;
            for (const std::string& code : blocking) note += (note.empty() ? "" : "\n") + migrationProblemText(code);
            if (note.empty() && !trimmed(to_).empty() && !previewed) {
                note = busy_ ? "Lecture de l'aperçu…" : "Appuyez sur « Aperçu » pour vérifier le compte cible.";
                noteBad = false;
            } else if (note.empty() && previewed) {
                const Json& sh = jfield(preview_, "shared", Json::object());
                note = "En commun : " + std::to_string(jget<int>(sh, "bests", 0)) + " meilleurs temps (" +
                       std::to_string(jget<int>(sh, "bests_source_better", 0)) + " meilleurs côté source), " +
                       std::to_string(jget<int>(sh, "achievements_new", 0)) + " succès nouveaux pour la cible.";
                noteBad = false;
            }
        }
        // Once done, the confirmation field is gone: the result may use its room.
        const float noteLimit = done_ ? in.bottom() - bh - ui.s(14) : limit;
        const Color noteColor = noteBad ? ui.theme().danger : busy_ || message_.empty() ? ui.theme().textDim : ui.theme().success;
        if (!note.empty()) boundedText(ui, note, in.x, y, in.w, noteLimit, 15, noteColor);
        // The confirmation, then the buttons.
        const std::string confirmWord = jstr(preview_, "confirm", "MIGRER");
        if (!done_) ui.textField("confirm", {in.x, cy, std::min(in.w * 0.5f, ui.s(420)), fh}, confirm_, "Tapez " + confirmWord + " pour confirmer", 10);
        bool any = false;
        for (int i = 0; i < 6; ++i) any = any || (chosen_[i] && (i != 5 || copy.is_object()));
        const bool ready = !busy_ && !done_ && any && trimmed(confirm_) == confirmWord && previewed && jget<bool>(preview_, "target_found", false) &&
                           blocking.empty();
        const int b = footer(ui, in, done_ ? "" : "Annuler", done_ ? "Fermer" : "Migrer", done_ || ready, !busy_);
        ui.popId();
        if (b == 2 && !done_) migrate();
        else if (b == 2 || ((b == 1 || ui.backPressed()) && !busy_)) {
            ui.end();
            g_.app.popScene();
            return;
        }
        ui.end();
    }

private:
    void load() {
        busy_ = true;
        message_.clear();
        error_ = false;
        const std::string to = trimmed(to_);
        g_.online.get("/api/v1/admin/players/" + jstr(source_, "id") + "/migration?to=" + online::encodeQueryValue(to),
                      OnlineService::guard(life_, [this, to](const ApiResult& r) {
                          busy_ = false;
                          if (!r.ok) {
                              error_ = true;
                              message_ = messageOf(r);
                              return;
                          }
                          preview_ = r.data;
                          previewTo_ = to;
                      }),
                      false);
    }
    void migrate() {
        static const char* ids[6] = {"records", "achievements", "rewards", "playtime", "levels", "progress"};
        const bool hasCopy = jfield(preview_, "copy", Json()).is_object();
        Json list = Json::array();
        for (int i = 0; i < 6; ++i)
            if (chosen_[i] && (i != 5 || hasCopy)) list.push_back(ids[i]);
        busy_ = true;
        error_ = false;
        message_ = "Sauvegarde complète du serveur, puis migration…";
        const std::string to = previewTo_;
        g_.online.send("POST", "/api/v1/admin/players/" + jstr(source_, "id") + "/migrate",
                       Json{{"to_username", to}, {"mode", replace_ ? "replace" : "merge"}, {"items", list}, {"close_source", close_}, {"confirm", trimmed(confirm_)}},
                       OnlineService::guard(life_, [this](const ApiResult& r) {
                           busy_ = false;
                           if (!r.ok) {
                               error_ = true;
                               message_ = messageOf(r);
                               return;
                           }
                           done_ = true;
                           g_.ui.setFocus("admmig/b");  // the confirmation field is gone: « Fermer »
                           const Json& m = jfield(r.data, "migration", Json::object());
                           const std::string src = jstr(jfield(m, "source", Json::object()), "username", jstr(source_, "username"));
                           const std::string dst = jstr(jfield(m, "target", Json::object()), "username", previewTo_);
                           // One sentence per line (each one translated as a whole).
                           message_ = jget<bool>(jfield(m, "source", Json::object()), "closed", false)
                                          ? "Données de « " + src + " » migrées vers « " + dst + " » ; « " + src + " » est fermé."
                                          : "Données de « " + src + " » migrées vers « " + dst + " ».";
                           message_ += "\nSauvegarde faite avant : " + jstr(jfield(r.data, "backup", Json::object()), "id") + ".";
                           if (jfield(m, "restore", Json()).is_object()) message_ += "\nLa progression attend d'être restaurée par « " + dst + " ».";
                           const int kept = jget<int>(jfield(m, "files", Json::object()), "kept", 0);
                           if (kept > 0) message_ += "\n" + std::to_string(kept) + " fichiers de replays remplacés restent sur le serveur.";
                       }),
                       180000);
    }
    GameServices& g_;
    Json source_, preview_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string to_, previewTo_, confirm_, message_;
    bool chosen_[6] = {true, true, true, true, false, true};
    bool replace_ = false, close_ = false, busy_ = false, done_ = false, error_ = false;
};

}  // namespace

std::unique_ptr<Scene> makeMigrationPrepare(GameServices& g) { return std::make_unique<MigrationPrepareScene>(g); }
std::unique_ptr<Scene> makeMigrationRecover(GameServices& g) { return std::make_unique<MigrationRecoverScene>(g); }
std::unique_ptr<Scene> makeProgressRestore(GameServices& g, const Json& restore) { return std::make_unique<ProgressRestoreScene>(g, restore); }
std::unique_ptr<Scene> makeAdminMigration(GameServices& g, const Json& source) { return std::make_unique<AdminMigrationScene>(g, source); }

}  // namespace cosmic
