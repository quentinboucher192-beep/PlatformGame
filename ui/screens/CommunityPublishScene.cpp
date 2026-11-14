// WORKSHOP → Mes niveaux → Publier en ligne: sends a player level to ONLINE LEVELS.
// Conditions: an account, a start and an exit, and a completion of this exact version of the
// level (editor test from the start, see GameScene) whose replay goes with it. The package
// holds the level and its own files only (the game's assets are on every installation).

#include <algorithm>

#include "engine/core/FileUtil.h"
#include "game/GameServices.h"
#include "game/online/LevelFingerprint.h"
#include "packaging/PackageExporter.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/Thumbnail.h"

namespace cosmic {

namespace {

class CommunityPublishScene : public Scene {
public:
    CommunityPublishScene(GameServices& g, std::string levelId) : g_(g), levelId_(std::move(levelId)) {}

    bool isOverlay() const override { return true; }

    void onEnter() override {
        refresh();
        autoPublish_ = g_.onlineOpen == "publish-now";  // automated checks
        if (autoPublish_) g_.onlineOpen.clear();
    }
    void update(float) override {
        if (autoPublish_ && !busy_ && !done_ && level_ && g_.online.loggedIn() && hasStart_ && hasExit_ && completionOk_ &&
            g_.online.state() == OnlineState::Online && entry_ && g_.ratings.get(*entry_)) {
            autoPublish_ = false;
            publish();
        }
    }
    void onResume() override { refresh(); }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 200});
        Rect box{ui.screenW() * 0.5f - ui.s(520), ui.screenH() * 0.5f - ui.s(380), ui.s(1040), ui.s(760)};
        ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
        ui.r().drawRoundedRect(box, ui.s(18), ui.theme().accent.withAlphaF(0.55f), ui.s(1.5f));
        ui.pushId("publish");
        ui.label(communityId_.empty() ? "PUBLIER EN LIGNE" : "PUBLIER UNE NOUVELLE VERSION", {box.x + ui.s(36), box.y + ui.s(44)}, 28,
                 ui.theme().accent, Align::Left, FontFace::Title, VAlign::Middle);
        ui.label("ONLINE LEVELS · Cosmic Online", {box.right() - ui.s(36), box.y + ui.s(44)}, 16, ui.theme().textDim, Align::Right,
                 FontFace::Body, VAlign::Middle);
        float x = box.x + ui.s(36), w = box.w * 0.5f - ui.s(56), y = box.y + ui.s(96);
        if (!level_) {
            ui.label("Niveau introuvable.", {x, y}, 20, ui.theme().danger);
        } else {
            // Checks
            auto check = [&](bool ok, const std::string& text, const std::string& hint = {}) {
                Rect dot{x + ui.s(2), y + ui.s(4), ui.s(16), ui.s(16)};
                ui.r().fillRoundedRect(dot, ui.s(8), ok ? ui.theme().success : ui.theme().danger);
                ui.label(text, {x + ui.s(30), y + ui.s(12)}, 18, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
                y += ui.s(30);
                if (!ok && !hint.empty()) {
                    Vec2 sz = ui.labelWrapped(hint, {x + ui.s(30), y}, w - ui.s(30), 14, ui.theme().textDim);
                    y += sz.y + ui.s(8);
                }
                y += ui.s(6);
            };
            check(g_.online.loggedIn(), g_.online.loggedIn() ? "Compte : " + g_.online.username() : "Compte Cosmic Online",
                  "Publier demande un compte (jouer n'en demande jamais).");
            check(hasStart_ && hasExit_, "Départ et sortie", "Placez un départ du joueur et une sortie dans l'éditeur.");
            check(completionOk_, completionOk_ ? "Terminé depuis le départ (" + timeText(completionMs_) + ")" : "Niveau terminé depuis le départ",
                  completionMissing_ ? "Testez le niveau depuis le départ dans l'éditeur (F5, pas « Tester ici ») et allez jusqu'à la sortie : "
                                       "un niveau impossible ne peut pas être publié."
                                     : "Le niveau a changé depuis votre dernière fin de niveau : enregistrez-le, puis terminez-le de nouveau "
                                       "depuis le départ (F5 dans l'éditeur).");
            const DifficultyRating* r = entry_ ? g_.ratings.get(*entry_) : nullptr;
            ui.label("Difficulté calculée : " + (r ? r->label() : std::string("calcul…")), {x, y + ui.s(12)}, 17, ui.theme().warm, Align::Left,
                     FontFace::Bold, VAlign::Middle);
            y += ui.s(34);
            ui.label(thumbFile_ ? "Vignette : celle capturée dans l'éditeur" : "Vignette : vue du départ (Niveau → Capturer la vignette)",
                     {x, y + ui.s(12)}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            y += ui.s(40);
            ui.labelWrapped("Ce qui est envoyé : le niveau, vos ennemis et pouvoirs personnalisés, vos fichiers propres et le replay de votre "
                            "fin de niveau. Les autres joueurs pourront le télécharger, le noter, le signaler et y classer leurs temps.",
                            {x, y}, w, 14, ui.theme().textDim);

            // Fields
            float x2 = box.x + box.w * 0.5f + ui.s(10), w2 = box.w * 0.5f - ui.s(46), y2 = box.y + ui.s(96);
            ui.label("Titre", {x2, y2}, 15, ui.theme().textDim);
            ui.textField("title", {x2, y2 + ui.s(22), w2, ui.s(44)}, title_, "3 à 60 caractères", 60);
            ui.label("Description", {x2, y2 + ui.s(80)}, 15, ui.theme().textDim);
            ui.textField("desc", {x2, y2 + ui.s(102), w2, ui.s(44)}, description_, "Facultative (500 caractères)", 500);
            const PlanetData* pl = g_.content.planet(level_->planet);
            ui.label("Planète : " + (pl ? pl->name : level_->planet) + "  ·  " + std::to_string(level_->objects.size()) + " objets", {x2, y2 + ui.s(170)},
                     16, ui.theme().text);
            if (!communityId_.empty())
                ui.labelWrapped("Déjà publié (version " + std::to_string(publishedVersion_) +
                                    ") : la nouvelle version remplace l'ancienne dans les listes ; les temps de l'ancienne restent dans son "
                                    "classement.",
                                {x2, y2 + ui.s(200)}, w2, 14, ui.theme().textDim);
            // Progress / result
            if (!status_.empty())
                ui.labelWrapped(status_, {x2, y2 + ui.s(300)}, w2, 17, statusError_ ? ui.theme().danger : ui.theme().success);
        }
        float by = box.bottom() - ui.s(76);
        UIContext::ButtonStyle p;
        p.primary = true;
        bool titleOk = title_.size() >= 3;
        // The computed difficulty goes with the publication: wait for it (worker thread).
        bool rated = entry_ && g_.ratings.get(*entry_);
        p.enabled = level_ && g_.online.loggedIn() && hasStart_ && hasExit_ && completionOk_ && titleOk && rated && !busy_ && !done_;
        if (ui.button("go", {box.x + ui.s(36), by, ui.s(360), ui.s(54)}, busy_ ? "Publication…" : (done_ ? "Publié" : "Publier"), p)) publish();
        if (!g_.online.loggedIn()) {
            UIContext::ButtonStyle l;
            l.small = true;
            if (ui.button("login", {box.x + ui.s(416), by, ui.s(300), ui.s(54)}, "Se connecter", l)) g_.app.pushScene(makeOnlineAccount(g_, false));
        }
        if (ui.button("close", {box.right() - ui.s(216), by, ui.s(180), ui.s(54)}, "Fermer") || (ui.backPressed() && !busy_)) {
            ui.popId();
            ui.end();
            g_.app.popScene();
            return;
        }
        ui.popId();
        ui.end();
    }

private:
    static std::string timeText(int64_t ms) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%d:%02d.%03d", static_cast<int>(ms / 60000), static_cast<int>(ms / 1000 % 60), static_cast<int>(ms % 1000));
        return buf;
    }

    void refresh() {
        entry_ = g_.content.level(levelId_);
        level_ = g_.content.loadLevel(levelId_);
        if (!level_) return;
        hasStart_ = level_->countObjects("player_start") > 0;
        hasExit_ = level_->countObjects("exit") > 0;
        if (title_.empty()) title_ = level_->name;
        if (description_.empty()) description_ = level_->description;
        hash_ = fingerprintGameplay(g_.content, *level_);
        fs::path base = Paths::userLevelsDir() / FileUtil::slugify(levelId_);
        fs::path side = base;
        side += ".completion.json";
        replayFile_ = base;
        replayFile_ += ".completion.cprz";
        completionOk_ = false;
        completionMissing_ = true;
        if (auto j = FileUtil::loadJson(side); j && FileUtil::exists(replayFile_)) {
            completionMissing_ = false;
            completionOk_ = jstr(*j, "gameplay_hash") == hash_;
            completionMs_ = jget<int64_t>(*j, "time_ms", 0);
        }
        fs::path thumb = base;
        thumb += ".thumb.png";
        thumbFile_ = FileUtil::exists(thumb);
        const Json& c = level_->metadata.value("community", Json::object());
        communityId_ = jstr(c, "id");
        publishedVersion_ = jget<int>(c, "version", 0);
    }

    void fail(const std::string& m) {
        busy_ = false;
        status_ = m;
        statusError_ = true;
    }

    void publish() {
        if (!level_) return;
        busy_ = true;
        statusError_ = false;
        status_ = "Préparation du package…";
        // Package: the level and its own files (the game's assets stay out).
        fs::path dir = Paths::userDir() / "online" / "publish";
        std::error_code ec;
        fs::create_directories(dir, ec);
        ExportOptions opt;
        opt.destination = dir / (FileUtil::slugify(levelId_) + ".cplanet");
        opt.version = level_->version;
        opt.skipInstalledAssets = true;
        opt.author = g_.online.username();  // the level's gameplay fingerprint ignores the author
        fs::path thumb = Paths::userLevelsDir() / (FileUtil::slugify(levelId_) + ".thumb.png");
        if (auto png = FileUtil::readBinary(thumb)) opt.thumbnailPng = *png;
        else opt.thumbnailPng = renderLevelThumbnail(g_, *level_);
        PackageExporter ex(g_.content, g_.app.resources());
        ExportPlan plan = ex.plan(*level_, opt, entry_ ? entry_->assetRoot : fs::path{});
        if (!plan.ok()) return fail("Fichiers manquants : " + plan.missing.front());
        std::string err;
        if (!ex.write(*level_, plan, opt, &err)) return fail("Package impossible : " + err);
        auto pkg = FileUtil::readBinary(opt.destination);
        auto replay = FileUtil::readBinary(replayFile_);
        if (!pkg || !replay) return fail("Lecture du package ou du replay impossible.");
        const DifficultyRating* r = entry_ ? g_.ratings.get(*entry_) : nullptr;
        Json body = {{"title", title_}, {"description", description_}, {"planet", level_->planet}, {"level_hash", hash_},
                     {"min_game_version", PackageFormat::engineVersion()}};
        if (r) body["difficulty"] = r->score;
        if (!communityId_.empty()) body["community_id"] = communityId_;
        status_ = "Création de la publication…";
        auto pkgBytes = std::make_shared<std::string>(pkg->begin(), pkg->end());
        auto replayBytes = std::make_shared<std::string>(replay->begin(), replay->end());
        g_.online.send("POST", "/api/v1/community/levels", body, OnlineService::guard(life_, [this, pkgBytes, replayBytes](const ApiResult& res) {
            if (!res.ok) return fail(res.message);
            std::string id = jstr(res.data, "id");
            int version = jget<int>(res.data, "version", 1);
            const Json& up = res.data.value("upload", Json::object());
            std::string pkgPath = jstr(up, "package"), replayPath = jstr(up, "replay");
            if (pkgPath.rfind("/api/v1/community/", 0) != 0 || replayPath.rfind("/api/v1/community/", 0) != 0) return fail("Réponse inattendue du serveur.");
            status_ = "Envoi du package (" + std::to_string(pkgBytes->size() / 1024 + 1) + " Ko)…";
            g_.online.sendBytes("PUT", pkgPath, *pkgBytes, OnlineService::guard(life_, [this, id, version, replayPath, replayBytes](const ApiResult& p) {
                if (!p.ok) return fail(p.message);
                status_ = "Envoi du replay de fin de niveau…";
                g_.online.sendBytes("PUT", replayPath, *replayBytes, OnlineService::guard(life_, [this, id, version](const ApiResult& rr) {
                    if (!rr.ok) {
                        std::string why;
                        for (const auto& x : rr.details.value("reasons", Json::array()))
                            if (x.is_string()) why += " · " + x.get<std::string>();
                        return fail(rr.message + why);
                    }
                    busy_ = false;
                    done_ = jget<bool>(rr.data, "published", false);
                    if (!done_) return fail("Envoi incomplet : réessayez.");
                    // Remember the publication in the level (metadata only: not part of the gameplay).
                    if (level_) {
                        level_->metadata["community"] = {{"id", id}, {"version", version}, {"published_at", FileUtil::nowString()}};
                        g_.content.saveUserLevel(*level_);
                    }
                    communityId_ = id;
                    publishedVersion_ = version;
                    status_ = "Publié ! Version " + std::to_string(version) + " visible dans ONLINE LEVELS (Récents).";
                    g_.online.notify(OnlineService::Notification::Kind::Success, "Niveau publié", "« " + title_ + " » est en ligne.");
                }));
            }));
        }));
    }

    GameServices& g_;
    std::string levelId_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    const LevelEntry* entry_ = nullptr;
    std::optional<LevelData> level_;
    bool hasStart_ = false, hasExit_ = false;
    bool completionOk_ = false, completionMissing_ = true;
    int64_t completionMs_ = 0;
    bool thumbFile_ = false;
    std::string hash_;
    fs::path replayFile_;
    std::string title_, description_;
    std::string communityId_;
    int publishedVersion_ = 0;
    bool busy_ = false, done_ = false, autoPublish_ = false;
    std::string status_;
    bool statusError_ = false;
};

}  // namespace

std::unique_ptr<Scene> makeCommunityPublish(GameServices& g, const std::string& levelId) {
    return std::make_unique<CommunityPublishScene>(g, levelId);
}

}  // namespace cosmic
