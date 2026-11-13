// Content management: export levels as .cplanet packages, import packages, manage
// installed packages.

#include <algorithm>
#include <format>

#include "engine/core/FileUtil.h"
#include "game/GameServices.h"
#include "packaging/PackageExporter.h"
#include "packaging/PackageImporter.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/Thumbnail.h"
#include "ui/widgets/FileDialogs.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

std::string humanSize(uint64_t b) {
    if (b > (1u << 20)) return std::format("{:.1f} Mo", b / 1048576.0);
    if (b > 1024) return std::format("{:.0f} Ko", b / 1024.0);
    return std::to_string(b) + " o";
}

class ContentManagerScene : public Scene {
public:
    ContentManagerScene(GameServices& g, std::string importFile, int tab)
        : g_(g), tab_(std::clamp(tab, 0, 4)), importPath_(std::move(importFile)) {}

    void onEnter() override {
        g_.playMusic("music/menu_theme.ogg");
        destDir_ = Paths::toUtf8(Paths::exportsDir());
        refreshLists();
        if (!importPath_.empty()) {
            tab_ = 3;
            analyze();
        }
    }

    void update(float dt) override {
        bg_.update(dt);
        if (messageTimer_ > 0) messageTimer_ -= dt;
        if (dialog_) {
            if (auto r = FileDialogs::result(dialog_)) {
                if (!r->empty()) {
                    if (dialogKind_ == 0) importPath_ = *r, analyze();
                    else if (dialogKind_ == 1) destDir_ = Paths::toUtf8(Paths::fromUtf8(*r).parent_path()), exportFile_ = Paths::toUtf8(Paths::fromUtf8(*r).filename());
                }
                dialog_ = 0;
            }
        }
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.6f);
        float W = ui.screenW(), H = ui.screenH();
        Rect panel{ui.s(50), ui.s(40), W - ui.s(100), H - ui.s(120)};
        ui.panel(panel, false, 18);
        Vec2 titleSize = ui.label("WORKSHOP", {panel.x + ui.s(36), panel.y + ui.s(22)}, 38, ui.theme().text, Align::Left,
                                  FontFace::Title);
        ui.label("Vos niveaux, les niveaux importés, le partage en .cplanet et l'éditeur d'ennemis",
                 {panel.x + ui.s(36) + titleSize.x + ui.s(24), panel.y + ui.s(40)}, 19, ui.theme().textDim);
        Rect tabsR{panel.x + ui.s(36), panel.y + ui.s(84), std::min(ui.s(1250), panel.w - ui.s(72)), ui.s(52)};
        int prev = tab_;
        ui.tabs("cm/tabs", tabsR, {"Mes niveaux", "Niveaux importés", "Exporter", "Importer", "Éditeur d'ennemis"}, tab_);
        if (prev != tab_) refreshLists();
        Rect content{panel.x + ui.s(36), tabsR.bottom() + ui.s(20), panel.w - ui.s(72), panel.bottom() - tabsR.bottom() - ui.s(50)};
        ui.pushId("cm" + std::to_string(tab_));
        if (tab_ == 0) myLevelsTab(content);
        else if (tab_ == 1) installedTab(content);
        else if (tab_ == 2) exportTab(content);
        else if (tab_ == 3) importTab(content);
        else enemyEditorTab(content);
        ui.popId();
        if (messageTimer_ > 0)
            ui.label(message_, {panel.center().x, panel.bottom() - ui.s(26)}, 22, messageColor_, Align::Center, FontFace::Bold,
                     VAlign::Middle);
        // The WORKSHOP is part of ONLINE: going back returns there.
        Glyphs::drawHint(ui, Action::MenuBack, "ONLINE", {ui.s(60), H - ui.s(40)}, ui.s(30));
        if (ui.backPressed()) g_.app.switchScene(makeOnlineRanking(g_));
        ui.end();
    }

private:
    void notify(const std::string& m, Color c) {
        message_ = m;
        messageColor_ = c;
        messageTimer_ = 5.0f;
    }

    void refreshLists() {
        levels_ = g_.content.levels();
        candidates_.clear();
        std::vector<fs::path> dirs = {Paths::exportsDir()};
        if (const char* dl = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS)) dirs.push_back(Paths::fromUtf8(dl));
        if (const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)) dirs.push_back(Paths::fromUtf8(docs));
        std::error_code ec;
        for (const auto& d : dirs) {
            if (!fs::is_directory(d, ec)) continue;
            for (const auto& e : fs::directory_iterator(d, ec))
                if (e.is_regular_file(ec) && FileUtil::extensionLower(e.path()) == PackageFormat::kExtension)
                    candidates_.push_back(e.path());
        }
    }

    // ============================================================= my levels
    void myLevelsTab(const Rect& c) {
        UIContext& ui = g_.ui;
        std::vector<const LevelEntry*> mine;
        for (const LevelEntry* l : g_.content.levels())
            if (l->source == ContentSource::User) mine.push_back(l);
        UIContext::ButtonStyle p;
        p.primary = true;
        if (ui.button("new", {c.x, c.y, ui.s(320), ui.s(56)}, "Nouveau niveau", p)) {
            g_.app.switchScene(makeLevelEditor(g_, ""));
            return;
        }
        ui.label(std::to_string(mine.size()) + (mine.size() > 1 ? " niveaux créés" : " niveau créé"), {c.x + ui.s(350), c.y + ui.s(28)}, 20,
                 ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        Rect list{c.x, c.y + ui.s(76), c.w, c.h - ui.s(76)};
        if (mine.empty()) {
            ui.labelWrapped("Vous n'avez pas encore créé de niveau. « Nouveau niveau » ouvre l'éditeur ; vos créations sont "
                            "enregistrées dans votre dossier utilisateur et peuvent être exportées en .cplanet.",
                            {list.x, list.y}, std::min(list.w, ui.s(900)), 21, ui.theme().textDim);
            return;
        }
        float rh = ui.s(92);
        ui.beginScroll("mine", list, mine.size() * (rh + ui.s(10)));
        float y = list.y - ui.scrollOffset();
        for (size_t i = 0; i < mine.size(); ++i) {
            const LevelEntry* l = mine[i];
            Rect row{list.x, y, list.w - ui.s(16), rh};
            ui.panel(row, false, 10);
            ui.label(l->name.empty() ? l->id : l->name, {row.x + ui.s(20), row.y + ui.s(14)}, 24, ui.theme().text, Align::Left, FontFace::Bold);
            const PlanetData* pl = g_.content.planet(l->planet);
            ui.label(l->id + "  —  " + (pl ? pl->name : l->planet), {row.x + ui.s(20), row.y + ui.s(52)}, 18, ui.theme().textDim);
            std::string id = "m" + std::to_string(i);
            if (ui.button(id + "play", {row.right() - ui.s(700), row.y + ui.s(18), ui.s(150), ui.s(56)}, "Jouer")) {
                g_.app.switchScene(makeGameScene(g_, l->id), 0.5f);
                break;
            }
            if (ui.button(id + "edit", {row.right() - ui.s(536), row.y + ui.s(18), ui.s(150), ui.s(56)}, "Éditer")) {
                g_.app.switchScene(makeLevelEditor(g_, l->id));
                break;
            }
            if (ui.button(id + "export", {row.right() - ui.s(372), row.y + ui.s(18), ui.s(166), ui.s(56)}, "Exporter")) {
                tab_ = 2;
                selectLevel(l->id);
                break;
            }
            UIContext::ButtonStyle d;
            d.danger = true;
            bool confirm = confirmDelete_ == l->id;
            if (ui.button(id + "del", {row.right() - ui.s(192), row.y + ui.s(18), ui.s(170), ui.s(56)}, confirm ? "Confirmer" : "Supprimer", d)) {
                if (confirm) {
                    std::string lid = l->id;
                    confirmDelete_.clear();
                    if (g_.content.deleteUserLevel(lid)) notify("Niveau supprimé", ui.theme().warm);
                    else notify("Suppression impossible", ui.theme().danger);
                    break;
                }
                confirmDelete_ = l->id;
            }
            y += rh + ui.s(10);
        }
        ui.endScroll();
    }

    // ========================================================== enemy editor
    void enemyEditorTab(const Rect& c) {
        UIContext& ui = g_.ui;
        ui.labelWrapped("L'éditeur d'ennemis crée ou modifie des ennemis et des boss : apparence animée, statistiques, "
                        "comportements, attaques et phases, avec une arène de test où vous contrôlez le joueur. Les ennemis "
                        "créés sont utilisables dans vos niveaux et embarqués automatiquement dans les packages .cplanet.",
                        {c.x, c.y}, std::min(c.w, ui.s(1000)), 22, ui.theme().text);
        UIContext::ButtonStyle p;
        p.primary = true;
        if (ui.button("open", {c.x, c.y + ui.s(150), ui.s(380), ui.s(60)}, "Ouvrir l'éditeur d'ennemis", p))
            g_.app.switchScene(makeEnemyEditor(g_));
    }

    // ================================================================ export
    void exportTab(const Rect& c) {
        UIContext& ui = g_.ui;
        float listW = ui.s(520);
        Rect list{c.x, c.y, listW, c.h};
        ui.label("Niveau à exporter", {list.x, list.y}, 22, ui.theme().textDim, Align::Left, FontFace::Bold);
        Rect inner{list.x, list.y + ui.s(36), list.w, list.h - ui.s(36)};
        float rh = ui.s(76);
        ui.beginScroll("exlist", inner, levels_.size() * (rh + ui.s(8)));
        float y = inner.y - ui.scrollOffset();
        for (size_t i = 0; i < levels_.size(); ++i) {
            const LevelEntry* l = levels_[i];
            if (ui.listItem("lv" + std::to_string(i), {inner.x, y, inner.w - ui.s(14), rh}, l->name, selectedLevel_ == l->id,
                            std::string(contentSourceLabel(l->source)) + " — " + l->id)) {
                selectLevel(l->id);
            }
            y += rh + ui.s(8);
        }
        ui.endScroll();

        float x = c.x + listW + ui.s(40), w = c.right() - x;
        if (selectedLevel_.empty()) {
            ui.labelWrapped("Sélectionnez un niveau à gauche. Le package .cplanet contiendra le niveau et toutes ses "
                            "dépendances (textures, sprites, animations, sons, musiques, ennemis, pouvoirs, effets, polices) "
                            "afin de fonctionner sur un autre PC.",
                            {x, c.y}, w, 22, ui.theme().textDim);
            return;
        }
        float fy = c.y;
        float lw = ui.s(220), fw = w - lw;
        auto field = [&](const char* id, const char* label, std::string& v) {
            ui.label(label, {x, fy + ui.s(24)}, 20, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            ui.textField(id, {x + lw, fy, fw, ui.s(48)}, v);
            fy += ui.s(58);
        };
        field("name", "Nom du package", exportName_);
        field("author", "Auteur", exportAuthor_);
        field("version", "Version", exportVersion_);
        field("desc", "Description", exportDesc_);
        field("file", "Nom du fichier", exportFile_);
        ui.label("Destination", {x, fy + ui.s(24)}, 20, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        ui.textField("dest", {x + lw, fy, fw - ui.s(200), ui.s(48)}, destDir_);
        if (ui.button("browse", {x + w - ui.s(186), fy, ui.s(186), ui.s(48)}, "Parcourir…")) {
            dialogKind_ = 1;
            dialog_ = FileDialogs::saveFile(g_.app.window(), "Package Cosmic Platformer", "cplanet",
                                            Paths::toUtf8(Paths::fromUtf8(destDir_) / Paths::fromUtf8(exportFile_)));
        }
        fy += ui.s(66);
        if (ui.button("check", {x, fy, ui.s(330), ui.s(54)}, "Vérifier les dépendances")) buildPlan();
        UIContext::ButtonStyle p;
        p.primary = true;
        p.enabled = plan_.has_value() && plan_->ok();
        if (ui.button("export", {x + ui.s(350), fy, ui.s(260), ui.s(54)}, "Exporter", p)) doExport();
        fy += ui.s(72);
        if (!plan_) return;
        const ExportPlan& pl = *plan_;
        std::string summary = std::format("{} fichiers — {} — {} ennemis, {} pouvoirs, {} ressources", pl.items.size(),
                                          humanSize(pl.totalSize), pl.countKind("enemy"), pl.countKind("power"),
                                          pl.countKind("asset"));
        ui.label(summary, {x, fy}, 21, pl.ok() ? ui.theme().success : ui.theme().danger, Align::Left, FontFace::Bold);
        fy += ui.s(34);
        for (const auto& m : pl.missing) {
            ui.label("Manquant : " + m, {x, fy}, 19, ui.theme().danger);
            fy += ui.s(26);
        }
        for (const auto& wtxt : pl.warnings) {
            ui.label("Attention : " + wtxt, {x, fy}, 19, ui.theme().warm);
            fy += ui.s(26);
        }
        Rect prev{x, fy, w, c.bottom() - fy};
        if (prev.h < ui.s(80)) return;
        ui.panel(prev, false, 10);
        float ih = ui.s(26);
        ui.beginScroll("planlist", prev.expanded(-ui.s(10)), pl.items.size() * ih);
        float iy = prev.y + ui.s(10) - ui.scrollOffset();
        for (const auto& it : pl.items) {
            if (iy > prev.y - ih && iy < prev.bottom()) {
                ui.label(it.archivePath, {prev.x + ui.s(18), iy}, 17, ui.theme().text);
                ui.label(it.kind + "  " + humanSize(it.size), {prev.right() - ui.s(24), iy}, 17, ui.theme().textDim, Align::Right);
            }
            iy += ih;
        }
        ui.endScroll();
    }

    void selectLevel(const std::string& id) {
        selectedLevel_ = id;
        plan_.reset();
        if (const LevelEntry* e = g_.content.level(id)) {
            exportName_ = e->name;
            exportAuthor_ = e->author.empty() ? "Anonyme" : e->author;
            exportDesc_ = e->description;
            exportFile_ = FileUtil::slugify(e->name) + PackageFormat::kExtension;
        }
    }

    void buildPlan() {
        auto level = g_.content.loadLevel(selectedLevel_);
        if (!level) {
            notify("Impossible de charger le niveau", g_.ui.theme().danger);
            return;
        }
        const LevelEntry* e = g_.content.level(selectedLevel_);
        PackageExporter ex(g_.content, g_.app.resources());
        ExportOptions opt = options();
        plan_ = ex.plan(*level, opt, e ? e->assetRoot : fs::path{});
    }

    ExportOptions options() {
        ExportOptions opt;
        opt.packageName = exportName_;
        opt.author = exportAuthor_;
        opt.version = exportVersion_;
        opt.description = exportDesc_;
        std::string file = exportFile_.empty() ? selectedLevel_ + PackageFormat::kExtension : exportFile_;
        if (FileUtil::extensionLower(Paths::fromUtf8(file)) != PackageFormat::kExtension) file += PackageFormat::kExtension;
        opt.destination = Paths::fromUtf8(destDir_) / Paths::fromUtf8(file);
        return opt;
    }

    void doExport() {
        auto level = g_.content.loadLevel(selectedLevel_);
        if (!level) return;
        const LevelEntry* e = g_.content.level(selectedLevel_);
        std::string root = e ? Paths::toUtf8(e->assetRoot) : "";
        ExportOptions opt = options();
        std::error_code ec;
        fs::create_directories(opt.destination.parent_path(), ec);
        opt.thumbnailPng = renderLevelThumbnail(g_, *level, root);
        PackageExporter ex(g_.content, g_.app.resources());
        plan_ = ex.plan(*level, opt, e ? e->assetRoot : fs::path{});
        std::string err;
        if (ex.write(*level, *plan_, opt, &err)) {
            notify("Exporté : " + Paths::toUtf8(opt.destination), g_.ui.theme().success);
            g_.uiSound("medal", 0.5f);
        } else {
            notify("Échec de l'export : " + err, g_.ui.theme().danger);
            g_.uiSound("ui_error");
        }
        refreshLists();
    }

    // ================================================================ import
    void importTab(const Rect& c) {
        UIContext& ui = g_.ui;
        float lw = ui.s(560);
        ui.label("Fichier .cplanet", {c.x, c.y}, 22, ui.theme().textDim, Align::Left, FontFace::Bold);
        ui.textField("path", {c.x, c.y + ui.s(34), lw - ui.s(200), ui.s(48)}, importPath_, "Chemin du fichier…");
        if (ui.button("browse", {c.x + lw - ui.s(186), c.y + ui.s(34), ui.s(186), ui.s(48)}, "Parcourir…")) {
            dialogKind_ = 0;
            dialog_ = FileDialogs::openFile(g_.app.window(), "Package Cosmic Platformer", "cplanet", Paths::toUtf8(Paths::exportsDir()));
        }
        if (ui.button("analyze", {c.x, c.y + ui.s(96), lw, ui.s(54)}, "Analyser le package")) analyze();
        ui.label("Packages trouvés (Exports, Téléchargements, Documents)", {c.x, c.y + ui.s(170)}, 19, ui.theme().textDim);
        Rect list{c.x, c.y + ui.s(204), lw, c.bottom() - c.y - ui.s(204)};
        float rh = ui.s(64);
        ui.beginScroll("cands", list, candidates_.size() * (rh + ui.s(8)));
        float y = list.y - ui.scrollOffset();
        for (size_t i = 0; i < candidates_.size(); ++i) {
            std::string n = Paths::toUtf8(candidates_[i].filename());
            if (ui.listItem("c" + std::to_string(i), {list.x, y, list.w - ui.s(14), rh}, n, importPath_ == Paths::toUtf8(candidates_[i]),
                            Paths::toUtf8(candidates_[i].parent_path()))) {
                importPath_ = Paths::toUtf8(candidates_[i]);
                analyze();
            }
            y += rh + ui.s(8);
        }
        ui.endScroll();
        if (candidates_.empty()) ui.label("Aucun fichier .cplanet trouvé", {list.x, list.y + ui.s(10)}, 19, ui.theme().textDim);

        float x = c.x + lw + ui.s(40), w = c.right() - x;
        if (!insp_) {
            ui.labelWrapped("Choisissez un fichier .cplanet. Avant toute installation le package est vérifié : format, version du "
                            "moteur, chemins dangereux, types de fichiers, intégrité (SHA-256), dépendances et conflits "
                            "d'identifiants. Aucun code provenant d'un package n'est jamais exécuté.",
                            {x, c.y}, w, 22, ui.theme().textDim);
            return;
        }
        const PackageInspection& p = *insp_;
        float y2 = c.y;
        if (thumb_) {
            float tw = std::min(w * 0.5f, ui.s(480));
            Rect tr{x + w - tw, y2, tw, tw * 9.0f / 16.0f};
            ui.r().drawTexture(*thumb_, nullptr, tr);
            ui.r().drawRoundedRect(tr, ui.s(4), ui.theme().border, ui.s(1.5f));
        }
        ui.label(p.name.empty() ? Paths::toUtf8(p.file.filename()) : p.name, {x, y2}, 32, ui.theme().text, Align::Left, FontFace::Title);
        y2 += ui.s(48);
        if (p.valid) {
            ui.label("par " + p.author + "  —  version " + p.version, {x, y2}, 20, ui.theme().textDim);
            y2 += ui.s(30);
            ui.label(std::format("{} fichiers, {} — moteur {}", p.fileCount, humanSize(p.totalSize), p.engineVersion), {x, y2}, 20,
                     ui.theme().textDim);
            y2 += ui.s(30);
            if (p.level)
                ui.label(std::format("{} objets, {} ennemis, {} pouvoirs", p.level->objects.size(), p.enemies.size(), p.powers.size()),
                         {x, y2}, 20, ui.theme().textDim);
            y2 += ui.s(36);
            Vec2 ds = ui.labelWrapped(p.description, {x, y2}, w * (thumb_ ? 0.48f : 1.0f), 20, ui.theme().text);
            y2 += std::max(ds.y, thumb_ ? ui.s(140) : 0.0f) + ui.s(16);
        }
        for (const auto& ch : p.checks) {
            ui.label("• " + ch, {x, y2}, 19, ui.theme().success);
            y2 += ui.s(26);
        }
        for (const auto& e : p.errors) {
            ui.label("× " + e, {x, y2}, 20, ui.theme().danger, Align::Left, FontFace::Bold);
            y2 += ui.s(28);
        }
        for (const auto& wtxt : p.warnings) {
            ui.label("! " + wtxt, {x, y2}, 19, ui.theme().warm);
            y2 += ui.s(26);
        }
        if (!p.valid) return;
        y2 += ui.s(10);
        if (!p.conflicts.empty()) {
            ui.label("Conflits d'identifiants", {x, y2}, 21, ui.theme().warm, Align::Left, FontFace::Bold);
            y2 += ui.s(30);
            for (const auto& cf : p.conflicts) {
                ui.label("• " + cf.id + " : " + cf.detail, {x, y2}, 19, ui.theme().text);
                y2 += ui.s(26);
            }
            ui.cycle("policy", {x, y2 + ui.s(6), std::min(w, ui.s(760)), ui.s(52)}, "Résolution", policy_,
                     {"Renommer les éléments importés", "Remplacer (package / ennemis / pouvoirs)"});
            y2 += ui.s(70);
        }
        UIContext::ButtonStyle pr;
        pr.primary = true;
        if (ui.button("install", {x, y2, ui.s(320), ui.s(56)}, "Importer", pr)) doImport();
        if (!lastImported_.empty()) {
            if (ui.button("playnow", {x + ui.s(340), y2, ui.s(300), ui.s(56)}, "Jouer maintenant"))
                g_.app.switchScene(makeGameScene(g_, lastImported_), 0.5f);
        }
    }

    void analyze() {
        insp_.reset();
        thumb_.reset();
        lastImported_.clear();
        if (importPath_.empty()) return;
        PackageImporter imp(g_.content);
        insp_ = imp.inspect(Paths::fromUtf8(importPath_));
        thumb_ = textureFromPng(g_, insp_->thumbnail);
        if (!insp_->valid) g_.uiSound("ui_error");
    }

    void doImport() {
        if (!insp_ || !insp_->valid) return;
        PackageImporter imp(g_.content);
        std::string levelId, err;
        if (imp.install(*insp_, policy_ == 0 ? ConflictPolicy::Rename : ConflictPolicy::Replace, &levelId, &err)) {
            g_.content.reload();
            g_.ratings.contentChanged();
            lastImported_ = levelId;
            notify("Niveau importé : " + levelId, g_.ui.theme().success);
            g_.uiSound("power_unlock", 0.5f);
            insp_->valid = true;
            insp_->conflicts.clear();
        } else {
            notify("Échec de l'import : " + err, g_.ui.theme().danger);
        }
    }

    // ============================================================= installed
    void installedTab(const Rect& c) {
        UIContext& ui = g_.ui;
        const auto& pkgs = g_.content.packages();
        if (pkgs.empty()) {
            ui.label("Aucun package installé.", {c.x, c.y}, 22, ui.theme().textDim);
            return;
        }
        float rh = ui.s(92);
        ui.beginScroll("pkgs", c, pkgs.size() * (rh + ui.s(10)));
        float y = c.y - ui.scrollOffset();
        for (size_t i = 0; i < pkgs.size(); ++i) {
            const PackageInfo& p = pkgs[i];
            Rect row{c.x, y, c.w - ui.s(16), rh};
            ui.panel(row, false, 10);
            ui.label(p.name, {row.x + ui.s(20), row.y + ui.s(14)}, 24, ui.theme().text, Align::Left, FontFace::Bold);
            ui.label("par " + p.author + "  —  v" + p.version + "  —  installé le " + p.installedAt + "  —  " + p.levelId,
                     {row.x + ui.s(20), row.y + ui.s(52)}, 18, ui.theme().textDim);
            std::string id = "p" + std::to_string(i);
            if (ui.button(id + "play", {row.right() - ui.s(520), row.y + ui.s(18), ui.s(150), ui.s(56)}, "Jouer"))
                g_.app.switchScene(makeGameScene(g_, p.levelId), 0.5f);
            if (ui.button(id + "edit", {row.right() - ui.s(356), row.y + ui.s(18), ui.s(150), ui.s(56)}, "Éditer"))
                g_.app.switchScene(makeLevelEditor(g_, p.levelId));
            UIContext::ButtonStyle d;
            d.danger = true;
            bool confirm = confirmDelete_ == p.id;
            if (ui.button(id + "del", {row.right() - ui.s(192), row.y + ui.s(18), ui.s(170), ui.s(56)}, confirm ? "Confirmer" : "Supprimer", d)) {
                if (confirm) {
                    std::string pid = p.id;
                    confirmDelete_.clear();
                    g_.content.removePackage(pid);
                    g_.ratings.contentChanged();
                    notify("Package supprimé", g_.ui.theme().warm);
                    ui.endScroll();
                    return;
                }
                confirmDelete_ = p.id;
            }
            y += rh + ui.s(10);
        }
        ui.endScroll();
    }

    GameServices& g_;
    MenuBackground bg_;
    int tab_ = 0;
    std::vector<const LevelEntry*> levels_;
    std::string selectedLevel_;
    std::string exportName_, exportAuthor_, exportVersion_ = "1.0", exportDesc_, exportFile_, destDir_;
    std::optional<ExportPlan> plan_;
    std::string importPath_;
    std::vector<fs::path> candidates_;
    std::optional<PackageInspection> insp_;
    TexturePtr thumb_;
    int policy_ = 0;
    std::string lastImported_;
    std::string confirmDelete_;
    int dialog_ = 0;
    int dialogKind_ = 0;
    std::string message_;
    Color messageColor_;
    float messageTimer_ = 0.0f;
};

}  // namespace

std::unique_ptr<Scene> makeContentManager(GameServices& g, const std::string& importFile, int tab) {
    return std::make_unique<ContentManagerScene>(g, importFile, tab);
}

}  // namespace cosmic
