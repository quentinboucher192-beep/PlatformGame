// Notifications of the main menu: new campaign levels, powers, monsters and bosses, campaign
// levels modified (records dropped), the version installed and newer installers to download.

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>

#include <SDL3/SDL_misc.h>

#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "packaging/Sha256.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

struct KindStyle {
    const char* glyph;
    Color color;
    const char* name;
};

KindStyle styleOf(const std::string& kind) {
    if (kind == "level") return {"N", {57, 240, 216, 255}, "Nouveau niveau"};
    if (kind == "level_changed") return {"M", {255, 179, 71, 255}, "Niveau modifié"};
    if (kind == "record") return {"R", {255, 77, 109, 255}, "Record"};
    if (kind == "power") return {"P", {176, 76, 255, 255}, "Pouvoir"};
    if (kind == "enemy") return {"E", {255, 138, 92, 255}, "Monstre"};
    if (kind == "boss") return {"B", {255, 217, 138, 255}, "Boss"};
    if (kind == "update") return {"", {120, 230, 140, 255}, "Mise à jour"};
    return {"i", {127, 184, 255, 255}, "Version"};
}

std::string sizeText(int64_t bytes) {
    if (bytes <= 0) return "";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f Mo", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buf;
}

class NotificationsScene : public Scene {
public:
    explicit NotificationsScene(GameServices& g) : g_(g) {}

    void onEnter() override {
        g_.ui.setFocus("news/back");
        if (g_.online.usable()) g_.checkForUpdate();
    }
    void onExit() override { g_.news.markAllRead(); }
    void update(float dt) override {
        t_ += dt;
        bg_.update(dt);
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.55f);
        float W = std::min(ui.screenW() - ui.s(120), ui.s(1240));
        float x0 = (ui.screenW() - W) * 0.5f;
        float y = ui.s(48);
        ui.label("NOTIFICATIONS", {x0, y}, 44, ui.theme().text, Align::Left, FontFace::Title);
        ui.pushId("news");
        UIContext::ButtonStyle st;
        st.small = true;
        st.enabled = g_.news.unread() > 0;
        float bw = ui.s(280);
        if (ui.button("readall", {x0 + W - bw - ui.s(196), y, bw, ui.s(52)}, "Tout marquer comme lu", st)) g_.news.markAllRead();
        if (ui.button("back", {x0 + W - ui.s(180), y, ui.s(180), ui.s(52)}, "Retour") || ui.backPressed()) {
            g_.app.switchScene(makeMainMenu(g_), 0.3f);
            ui.popId();
            ui.end();
            return;
        }
        y += ui.s(78);
        const auto& items = g_.news.items();
        if (items.empty()) {
            ui.labelWrapped(g_.news.scanned() ? "Rien de nouveau pour l'instant. Les nouveaux niveaux, pouvoirs, monstres et boss, les records "
                                                "à refaire après une modification et les nouvelles versions du jeu apparaîtront ici."
                                              : "Recherche des nouveautés…",
                            {x0, y}, W, 22, ui.theme().textDim);
            ui.popId();
            ui.end();
            return;
        }
        // Card heights from the wrapped text (nothing overlaps).
        float textW = W - ui.s(120) - ui.s(300);
        std::vector<float> heights;
        float total = 0.0f;
        for (const auto& it : items) {
            float th = it.text.empty() ? 0.0f : ui.text().measureWrapped(it.text, FontFace::Body, ui.s(18), textW).y;
            float h = std::max(ui.s(100), ui.s(70) + th + ui.s(22));
            heights.push_back(h);
            total += h + ui.s(12);
        }
        Rect view{x0, y, W, ui.screenH() - y - ui.s(40)};
        ui.beginScroll("list", view, total);
        float cy = view.y - ui.scrollOffset();
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& it = items[i];
            Rect card{x0, cy, W - ui.s(14), heights[i]};
            cy += heights[i] + ui.s(12);
            if (card.bottom() < view.y || card.y > view.bottom()) continue;
            KindStyle k = styleOf(it.kind);
            ui.panel(card, !it.read, 12);
            if (!it.read) ui.r().fillRoundedRect({card.x, card.y + ui.s(10), ui.s(4), card.h - ui.s(20)}, ui.s(2), k.color);
            Vec2 ic{card.x + ui.s(52), card.y + ui.s(48)};
            ui.r().drawGlow(ic, ui.s(40), k.color.withAlpha(it.read ? 40 : 90));
            ui.r().fillCircle(ic, ui.s(26), Color{8, 12, 26, 255});
            ui.r().drawRing(ic, ui.s(26), ui.s(2.5f), k.color);
            if (k.glyph[0]) {
                ui.label(k.glyph, ic, 24, k.color, Align::Center, FontFace::Bold, VAlign::Middle);
            } else {  // download arrow
                ui.r().drawLine(ic + Vec2{0, -ui.s(11)}, ic + Vec2{0, ui.s(4)}, k.color, ui.s(3.5f));
                ui.r().fillTriangle(ic + Vec2{-ui.s(9), ui.s(1)}, ic + Vec2{ui.s(9), ui.s(1)}, ic + Vec2{0, ui.s(11)}, k.color);
                ui.r().fillRect({ic.x - ui.s(11), ic.y + ui.s(13), ui.s(22), ui.s(3)}, k.color);
            }
            float tx = card.x + ui.s(100);
            float titleRoom = card.right() - tx - ui.s(320);
            ui.label(it.title, {tx, card.y + ui.s(16)}, ui.fitFont(it.title, FontFace::Bold, 22, titleRoom), ui.theme().text, Align::Left,
                     FontFace::Bold);
            ui.label(std::string(k.name) + "  ·  " + it.date, {tx, card.y + ui.s(46)}, 15, k.color.withAlpha(200));
            if (!it.text.empty()) ui.labelWrapped(it.text, {tx, card.y + ui.s(70)}, textW, 18, ui.theme().textDim);
            actions(it, card);
        }
        ui.endScroll();
        ui.popId();
        ui.end();
    }

private:
    void actions(const NewsCenter::Item& it, const Rect& card) {
        UIContext& ui = g_.ui;
        Rect b{card.right() - ui.s(296), card.y + ui.s(18), ui.s(276), ui.s(50)};
        UIContext::ButtonStyle st;
        st.small = true;
        if (it.kind == "level" || it.kind == "level_changed" || it.kind == "record") {
            if (g_.content.level(it.target) && ui.button("play" + it.id, b, it.kind == "record" ? "Refaire un record" : "Jouer", st))
                g_.app.switchScene(makeGameScene(g_, it.target), 0.5f);
            return;
        }
        if (it.kind != "update") return;
        // Installer: download, check its SHA-256, then run it or open its folder.
        fs::path file = Paths::userDir() / "telechargements" / ("CosmicPlatformer-Setup-" + it.target + ".exe");
        bool ready = FileUtil::exists(file) && verified_.count(it.target);
        if (downloading_ == it.target) {
            ui.label("Téléchargement…", b.center(), 18, ui.theme().accent, Align::Center, FontFace::Bold, VAlign::Middle);
        } else if (ready) {
            st.primary = true;
            if (ui.button("run" + it.id, b, "Lancer l'installateur", st)) {
                std::string url = "file:///" + Paths::toUtf8(file);
                std::replace(url.begin(), url.end(), '\\', '/');
                SDL_OpenURL(url.c_str());
            }
            UIContext::ButtonStyle s2;
            s2.small = true;
            if (ui.button("dir" + it.id, {b.x, b.bottom() + ui.s(8), b.w, ui.s(42)}, "Ouvrir le dossier", s2)) {
                std::string url = "file:///" + Paths::toUtf8(file.parent_path());
                std::replace(url.begin(), url.end(), '\\', '/');
                SDL_OpenURL(url.c_str());
            }
        } else {
            st.primary = true;
            st.enabled = g_.online.usable() && downloading_.empty() && !it.download.empty();
            std::string label = "Télécharger (.exe" + (it.size > 0 ? ", " + sizeText(it.size) : std::string()) + ")";
            if (ui.button("dl" + it.id, b, label, st)) download(it, file);
            if (!errors_[it.target].empty())
                ui.labelWrapped(errors_[it.target], {b.x, b.bottom() + ui.s(6)}, b.w, 15, ui.theme().danger);
        }
    }

    void download(const NewsCenter::Item& it, const fs::path& file) {
        downloading_ = it.target;
        errors_[it.target].clear();
        std::string version = it.target, sha = it.sha256;
        auto alive = life_;
        g_.online.getBinary(
            it.download,
            [this, version, sha, file, alive = std::weak_ptr<int>(alive)](const HttpResponse& r) {
                if (alive.expired()) return;
                downloading_.clear();
                if (r.status != 200 || r.body.empty()) {
                    errors_[version] = "Échec du téléchargement (" + (r.status ? "erreur " + std::to_string(r.status) : std::string("réseau")) + ").";
                    return;
                }
                std::vector<uint8_t> bytes(r.body.begin(), r.body.end());
                if (!sha.empty() && Sha256::hex(bytes) != sha) {
                    errors_[version] = "Fichier refusé : son empreinte SHA-256 ne correspond pas à celle annoncée.";
                    return;
                }
                std::error_code ec;
                fs::create_directories(file.parent_path(), ec);
                if (!FileUtil::writeBinary(file, bytes)) {
                    errors_[version] = "Impossible d'écrire le fichier dans le dossier de l'utilisateur.";
                    return;
                }
                verified_.insert(version);
            },
            256u * 1024u * 1024u);
    }

    GameServices& g_;
    MenuBackground bg_;
    float t_ = 0.0f;
    std::string downloading_;
    std::set<std::string> verified_;
    std::map<std::string, std::string> errors_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace

std::unique_ptr<Scene> makeNotifications(GameServices& g) { return std::make_unique<NotificationsScene>(g); }

}  // namespace cosmic
