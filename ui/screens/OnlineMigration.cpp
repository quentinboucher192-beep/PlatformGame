#include "ui/screens/OnlineMigration.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <string>

#include "engine/core/Scene.h"
#include "game/GameServices.h"
#include "game/online/HttpTransport.h"
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
        // The copy as the old game sent it ({format, game_version, summary, files}), served as
        // the file itself (not in the API envelope): read like a download.
        g_.online.getBinary("/api/v1/players/me/progress-restore/file",
                            [this, life = std::weak_ptr<int>(life_)](const HttpResponse& r) {
                                if (life.expired()) return;
                                busy_ = false;
                                Json body = r.status == 200 ? Json::parse(r.body, nullptr, false) : Json();
                                if (r.networkFailure() || r.status != 200 || !body.is_object()) {
                                    error_ = true;
                                    message_ = r.status == 404 ? std::string("Aucune progression n'attend plus d'être restaurée.")
                                                               : std::string("Cosmic Online ne répond pas : réessayez plus tard.");
                                    return;
                                }
                                std::string err;
                                if (!g_.restoreProgressCopy(jfield(body, "files", Json()), &err)) {
                                    error_ = true;
                                    message_ = err;
                                    return;
                                }
                                done_ = true;
                                message_ = "Progression restaurée : elle s'ajoute à celle de cet ordinateur.";
                                g_.online.send("POST", "/api/v1/players/me/progress-restore", Json{{"action", "done"}}, [](const ApiResult&) {});
                            },
                            16u << 20);
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
                                 : std::string(" · aucune copie de la progression");
        // The confirmation field sits above the buttons; everything else stays above it.
        const float bh = ui.s(50), fh = ui.s(44);
        const float cy = in.bottom() - bh - ui.s(14) - fh, limit = cy - ui.s(8);
        y += boundedText(ui, from, in.x, y, in.w, in.y + ui.s(48), 16, ui.theme().text) + ui.s(10);
        // To: the target account and its preview.
        const float bw = ui.s(170);
        ui.textField("to", {in.x, y, in.w - bw - ui.s(10), fh}, to_, "Pseudonyme du compte cible", 20);
        UIContext::ButtonStyle ps;
        ps.small = true;
        ps.enabled = !busy_ && to_.size() >= 3;
        if (ui.button("preview", {in.right() - bw, y, bw, fh}, "Aperçu", ps)) load();
        y += fh + ui.s(12);
        // The categories: a box each, with what the source has, then what the target has.
        const Json& sc = jfield(jfield(preview_, "source", Json::object()), "counts", Json::object());
        const Json& tc = jfield(jfield(preview_, "target", Json::object()), "counts", Json::object());
        struct Item {
            const char* id;
            std::string caption;
        };
        auto n = [](const Json& c, const char* cat, const char* key) { return jget<int64_t>(jfield(c, cat, Json::object()), key, 0); };
        auto hours = [](int64_t secs) { return secs >= 3600 ? std::to_string(secs / 3600) + " h" : std::to_string(secs / 60) + " min"; };
        const Item items[6] = {
            {"records", "Records, meilleurs temps et replays (" + std::to_string(n(sc, "records", "records")) + " · " +
                            std::to_string(n(sc, "records", "bests")) + " meilleurs temps ; cible : " + std::to_string(n(tc, "records", "records")) + ")"},
            {"achievements", "Succès déclarés (" + std::to_string(n(sc, "achievements", "count")) + " · " + std::to_string(n(sc, "achievements", "stars")) +
                                 " étoiles ; cible : " + std::to_string(n(tc, "achievements", "count")) + ")"},
            {"rewards", "Récompenses de défis, titres et combinaisons créées (" + std::to_string(n(sc, "rewards", "won")) + ")"},
            {"playtime", "Temps joué (" + hours(n(sc, "playtime", "seconds")) + ")"},
            {"levels", "Niveaux publiés et projets, le créateur change (" + std::to_string(n(sc, "levels", "published")) + " · " +
                           std::to_string(n(sc, "levels", "projects")) + " projets)"},
            {"progress", "Progression locale (dernière copie) : le jeu du compte cible propose de la restaurer"},
        };
        // Nine rows of boxes and their gaps (38), then the two notes (about 54): the rows shrink to fit.
        const float rowH = std::clamp((limit - y - ui.s(38) - ui.s(54)) / 9.0f, ui.s(26), ui.s(38));
        for (int i = 0; i < 6; ++i) {
            const bool can = i != 5 || copy.is_object();
            if (tick(ui, std::string("item_") + items[i].id, {in.x, y, in.w, rowH}, items[i].caption, chosen_[i] && can, can)) chosen_[i] = !chosen_[i];
            y += rowH + ui.s(2);
        }
        y += ui.s(8);
        if (tick(ui, "merge", {in.x, y, in.w, rowH}, "Fusionner : le meilleur de chaque côté est gardé (meilleur temps, succès additionnés)", !replace_))
            replace_ = false;
        y += rowH + ui.s(2);
        if (tick(ui, "replace", {in.x, y, in.w, rowH}, "Remplacer : les données du compte cible sont remplacées", replace_)) replace_ = true;
        y += rowH + ui.s(8);
        if (tick(ui, "close", {in.x, y, in.w, rowH}, "Fermer le compte source ensuite (connexion impossible, pseudonyme libéré dans 30 jours)", close_))
            close_ = !close_;
        y += rowH + ui.s(8);
        y += boundedText(ui,
                         "Une sauvegarde complète du serveur est faite juste avant. Chaque migration est inscrite au Journal et les deux joueurs "
                         "reçoivent une notification.",
                         in.x, y, in.w, limit, 14, ui.theme().textDim) +
             ui.s(6);
        // What refuses it, or the result.
        std::string note = message_;
        if (note.empty())
            for (const Json& pr : jfield(preview_, "problems", Json::array()))
                if (pr.is_string()) note += (note.empty() ? "" : " ") + pr.get<std::string>();
        if (note.empty() && !to_.empty() && preview_.is_object() && !jget<bool>(preview_, "target_found", false)) note = "Compte cible introuvable.";
        if (!note.empty()) y += boundedText(ui, note, in.x, y, in.w, limit, 15, error_ || message_.empty() ? ui.theme().danger : ui.theme().success) + ui.s(6);
        // The confirmation, then the buttons.
        ui.textField("confirm", {in.x, cy, std::min(in.w * 0.5f, ui.s(420)), fh}, confirm_, "Tapez MIGRER pour confirmer", 10);
        bool any = false;
        for (int i = 0; i < 6; ++i) any = any || chosen_[i];
        const bool ready = !busy_ && !done_ && any && confirm_ == "MIGRER" && jget<bool>(preview_, "target_found", false) &&
                           jfield(preview_, "problems", Json::array()).empty() && jstr(jfield(preview_, "target", Json::object()), "username") == to_;
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
        std::string q;
        for (char c : to_) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') q += c;
            else {
                char buf[4];
                std::snprintf(buf, sizeof buf, "%%%02X", static_cast<unsigned char>(c));
                q += buf;
            }
        }
        g_.online.get("/api/v1/admin/players/" + jstr(source_, "id") + "/migration?to=" + q, OnlineService::guard(life_, [this](const ApiResult& r) {
                          busy_ = false;
                          if (!r.ok) {
                              error_ = true;
                              message_ = r.message;
                              return;
                          }
                          preview_ = r.data;
                      }),
                      false);
    }
    void migrate() {
        static const char* ids[6] = {"records", "achievements", "rewards", "playtime", "levels", "progress"};
        Json list = Json::array();
        for (int i = 0; i < 6; ++i)
            if (chosen_[i]) list.push_back(ids[i]);
        busy_ = true;
        error_ = false;
        message_ = "Sauvegarde complète du serveur, puis migration…";
        g_.online.send("POST", "/api/v1/admin/players/" + jstr(source_, "id") + "/migrate",
                       Json{{"to_username", to_}, {"mode", replace_ ? "replace" : "merge"}, {"items", list}, {"close_source", close_}, {"confirm", confirm_}},
                       OnlineService::guard(life_, [this](const ApiResult& r) {
                           busy_ = false;
                           if (!r.ok) {
                               error_ = true;
                               message_ = r.message;
                               return;
                           }
                           done_ = true;
                           message_ = "Données de « " + jstr(source_, "username") + " » migrées vers « " + to_ + " ». Sauvegarde faite avant : " +
                                      jstr(jfield(r.data, "backup", Json::object()), "id") + ".";
                       }),
                       180000);
    }
    GameServices& g_;
    Json source_, preview_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string to_, confirm_, message_;
    bool chosen_[6] = {true, true, true, true, false, true};
    bool replace_ = false, close_ = false, busy_ = false, done_ = false, error_ = false;
};

}  // namespace

std::unique_ptr<Scene> makeMigrationPrepare(GameServices& g) { return std::make_unique<MigrationPrepareScene>(g); }
std::unique_ptr<Scene> makeMigrationRecover(GameServices& g) { return std::make_unique<MigrationRecoverScene>(g); }
std::unique_ptr<Scene> makeProgressRestore(GameServices& g, const Json& restore) { return std::make_unique<ProgressRestoreScene>(g, restore); }
std::unique_ptr<Scene> makeAdminMigration(GameServices& g, const Json& source) { return std::make_unique<AdminMigrationScene>(g, source); }

}  // namespace cosmic
