// Level editor layout (the approved "fixed panels" design): top bar (file menu, undo,
// tools, modes, test menu), left EXPLORATEUR (layers and their objects as a tree, history,
// checks), right column (BIBLIOTHÈQUE and ENTITÉ, each folding), a header above the view
// (breadcrumb, grid, snapping, zoom, overlays) and, under the view, the table of a multiple
// selection. Every text is measured to fit its box.

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

#include "editors/level_editor/LevelEditorScene.h"
#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/levels/DecorAtlas.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

constexpr float kGrids[] = {8.0f, 16.0f, 32.0f};

/// A small triangle (twisty / menu arrow): the fonts have no such glyph.
void arrow(Renderer& r, Vec2 c, float s, bool down, Color col) {
    if (down) r.fillTriangle({c.x - s, c.y - s * 0.5f}, {c.x + s, c.y - s * 0.5f}, {c.x, c.y + s * 0.6f}, col);
    else r.fillTriangle({c.x - s * 0.5f, c.y - s}, {c.x - s * 0.5f, c.y + s}, {c.x + s * 0.6f, c.y}, col);
}

/// A five-pointed star (favorites).
void star(Renderer& r, Vec2 c, float rad, Color col) {
    Vec2 p[10];
    for (int i = 0; i < 10; ++i) {
        float a = -1.5707963f + i * 0.6283185f;
        float rr = i % 2 == 0 ? rad : rad * 0.45f;
        p[i] = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
    }
    for (int i = 0; i < 10; ++i) r.fillTriangle(c, p[i], p[(i + 1) % 10], col);
}

}  // namespace

// ------------------------------------------------------------------ top bar

void LevelEditorScene::drawToolbar(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.r().fillGradientV(r, {18, 26, 50, 255}, {12, 16, 32, 255});
    ui.r().fillRect({r.x, r.bottom() - 1, r.w, 1}, ui.theme().border);
    float x = r.x + ui.s(14), y = r.y + ui.s(9), h = r.h - ui.s(18);
    ui.pushId("tb");
    // Brand and document.
    const float brandW = ui.s(232);
    ui.label("COSMIC STUDIO", {x, r.y + ui.s(19)}, ui.fitFont("COSMIC STUDIO", FontFace::Title, 15, brandW), ui.theme().accent, Align::Left, FontFace::Title,
             VAlign::Middle);
    std::string doc = level_.name + (readOnly_ ? "  · lecture seule" : dirty_ ? "  · modifié" : "  · enregistré");
    ui.label(doc, {x, r.y + ui.s(43)}, ui.fitFont(doc, FontFace::Body, 15, brandW), dirty_ ? ui.theme().warm : ui.theme().textDim, Align::Left,
             FontFace::Body, VAlign::Middle);
    x += brandW + ui.s(12);
    auto btn = [&](const char* id, const std::string& label, float w, bool primary = false, bool enabled = true, bool selected = false) {
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = 18;
        st.primary = primary;
        st.enabled = enabled;
        st.selected = selected;
        Rect b{x, y, ui.s(w), h};
        bool c = ui.button(id, b, label, st);
        x += ui.s(w) + ui.s(6);
        return c;
    };
    auto gap = [&] { x += ui.s(10); };
    {
        Rect fb{x, y, ui.s(112), h};
        if (btn("file", "Fichier    ", 112, false, true, menu_ == Menu::File)) {
            menu_ = menu_ == Menu::File ? Menu::None : Menu::File;
            menuAnchor_ = fb;
        }
        arrow(ui.r(), {fb.right() - ui.s(18), fb.center().y}, ui.s(5), true, ui.theme().text);
        if (menu_ == Menu::File) menuAnchor_ = fb;
    }
    gap();
    if (btn("undo", "Annuler", 92, false, history_.canUndo())) applyJson(history_.undo());
    if (btn("redo", "Rétablir", 96, false, history_.canRedo())) applyJson(history_.redo());
    gap();
    if (btn("sel", "Sélection", 104, false, true, tool_ == Tool::Select)) tool_ = Tool::Select;
    if (btn("pan", "Main", 70, false, true, tool_ == Tool::Pan)) tool_ = Tool::Pan;
    gap();
    if (btn("settings", "Niveau", 86)) {
        modal_ = Modal::Settings;
        g_.ratings.requestDraft(level_);  // computed on a worker thread, shown in the dialog
    }
    if (btn("events", "Événements", 124)) modal_ = Modal::Events;
    if (btn("cine", "Cinématiques", 136)) modal_ = Modal::Cinematics;
    // Right: test (with its menu), help; between them, live editing presence.
    float presenceX = x + ui.s(10);
    x = r.right() - ui.s(14) - ui.s(40) - ui.s(6) - ui.s(36) - ui.s(2) - ui.s(128);
    if (live_) drawPresence({presenceX, r.y + ui.s(4), x - presenceX - ui.s(16), r.h - ui.s(8)});
    {
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = 18;
        st.primary = true;
        if (ui.button("test", {x, y, ui.s(128), h}, "Tester (F5)", st)) testPlay(false);
        Rect mb{x + ui.s(130), y, ui.s(36), h};
        st.selected = menu_ == Menu::Test;
        if (ui.button("testmenu", mb, "", st)) {
            menu_ = menu_ == Menu::Test ? Menu::None : Menu::Test;
            menuAnchor_ = {x, y, ui.s(166), h};
        }
        arrow(ui.r(), mb.center(), ui.s(5), true, ui.theme().text);
        if (menu_ == Menu::Test) menuAnchor_ = {x, y, ui.s(166), h};
        x += ui.s(172);
    }
    if (btn("help", "?", 40)) modal_ = Modal::Help;
    ui.popId();
}

void LevelEditorScene::drawMenus() {
    if (menu_ == Menu::None) return;
    UIContext& ui = g_.ui;
    struct Item {
        const char* id;
        std::string label;
        bool enabled;
        bool danger;
    };
    std::vector<Item> items;
    if (menu_ == Menu::File) {
        items = {{"open", "Ouvrir un niveau…", true, false},
                 {"new", "Nouveau niveau…", true, false},
                 {"save", readOnly_ ? "Dupliquer pour modifier" : (dirty_ ? "Enregistrer (Ctrl+S)" : "Enregistré"), readOnly_ || dirty_, false},
                 {"quit", "Quitter l'éditeur", true, true}};
    } else {
        LevelObject* po = primary();
        items = {{"start", "Depuis le début", true, false}, {"view", "Depuis la vue", true, false}, {"sel", "Depuis la sélection", po != nullptr, false}};
    }
    float w = std::max(menuAnchor_.w, ui.s(250)), ih = ui.s(42);
    Rect box{menuAnchor_.x, menuAnchor_.bottom() + ui.s(4), w, ih * items.size() + ui.s(12)};
    if (box.right() > ui.screenW() - ui.s(8)) box.x = ui.screenW() - ui.s(8) - box.w;
    ui.r().fillRoundedRect(box.expanded(ui.s(2)), ui.s(10), Color{2, 4, 12, 160});
    ui.r().fillRoundedRect(box, ui.s(10), Color{14, 20, 42, 252});
    ui.r().drawRoundedRect(box, ui.s(10), ui.theme().accent.withAlphaF(0.5f), ui.s(1.2f));
    ui.pushId("menu");
    bool chosen = false;
    for (size_t i = 0; i < items.size(); ++i) {
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = 17;
        st.align = Align::Left;
        st.enabled = items[i].enabled;
        st.danger = items[i].danger;
        Rect ir{box.x + ui.s(6), box.y + ui.s(6) + i * ih, box.w - ui.s(12), ih - ui.s(4)};
        if (!ui.button(items[i].id, ir, items[i].label, st)) continue;
        chosen = true;
        std::string id = items[i].id;
        if (menu_ == Menu::File) {
            if (id == "open") modal_ = Modal::Open;
            else if (id == "new") {
                modal_ = Modal::New;
                newId_.clear();
                newName_.clear();
            } else if (id == "save") {
                if (readOnly_) duplicateForEditing();
                else save();
            } else if (id == "quit") {
                if (dirty_) modal_ = Modal::ConfirmQuit;
                else g_.app.switchScene(makeEditorExit(g_, level_.id));
            }
        } else {
            if (id == "start") testPlay(false);
            else if (id == "view") testPlay(true);
            else if (LevelObject* po = primary())
                testFrom(po->type == "solid" || po->type == "oneway" ? Vec2{po->rect.center().x, po->rect.y} : po->rect.bottomCenter());
        }
    }
    ui.popId();
    // A click anywhere else, or Échap, closes the menu.
    InputManager& in = g_.app.input();
    if (chosen || ui.backPressed() || (in.mousePressed(SDL_BUTTON_LEFT) && !box.contains(in.mousePos()) && !menuAnchor_.contains(in.mousePos())))
        menu_ = Menu::None;
}

// --------------------------------------------------------------- left panel

void LevelEditorScene::drawLeftPanel(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.r().fillRect(r, {12, 16, 32, 245});
    ui.r().fillRect({r.right() - 1, r.y, 1, r.h}, ui.theme().border);
    const int warnings = static_cast<int>(issues_.size());
    int tab = static_cast<int>(leftTab_);
    ui.tabs("lefttabs", {r.x + ui.s(8), r.y + ui.s(8), r.w - ui.s(16), ui.s(40)},
            {"Explorateur", "Historique", warnings > 0 ? "Vérifier (" + std::to_string(warnings) + ")" : std::string("Vérifier")}, tab);
    leftTab_ = static_cast<LeftTab>(tab);
    Rect c{r.x + ui.s(10), r.y + ui.s(58), r.w - ui.s(20), r.h - ui.s(68)};
    ui.pushId("left");
    if (leftTab_ == LeftTab::Explorer) drawExplorer(c);
    else if (leftTab_ == LeftTab::History) drawHistory(c);
    else drawValidation(c);
    ui.popId();
}

namespace {
std::string foldLower(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            unsigned char d = static_cast<unsigned char>(s[++i]);
            const char* map = "aaaaaaaceeeeiiiidnooooo/ouuuuyby";
            out.push_back(map[d & 0x1F]);
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}
}  // namespace

void LevelEditorScene::drawExplorer(const Rect& r) {
    UIContext& ui = g_.ui;
    {
        float hw = (r.w - ui.s(6)) * 0.5f;
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = 14;
        st.selected = explorerGroup_ == 0;
        if (ui.button("bylayer", {r.x, r.y, hw, ui.s(30)}, "Par calque", st)) explorerGroup_ = 0;
        st.selected = explorerGroup_ == 1;
        if (ui.button("bytype", {r.x + hw + ui.s(6), r.y, hw, ui.s(30)}, "Par type", st)) explorerGroup_ = 1;
    }
    ui.textField("search", {r.x, r.y + ui.s(36), r.w, ui.s(40)}, outlinerSearch_, "Filtrer : id, type, ennemi, groupe…", 40);
    const float settingsH = explorerGroup_ == 0 ? ui.s(258) : 0.0f;
    Rect list{r.x, r.y + ui.s(84), r.w, r.h - ui.s(84) - settingsH};
    if (explorerGroup_ == 1) {
        // Objects grouped by type, the most numerous first.
        const std::string q = foldLower(outlinerSearch_);
        std::map<std::string, std::vector<size_t>> byType;
        for (size_t oi = 0; oi < level_.objects.size(); ++oi) {
            const LevelObject& o = level_.objects[oi];
            const ObjectTypeInfo* t = ObjectTypes::find(o.type);
            std::string label = (t ? t->name : o.type) + " " + o.id + " " + o.propStr("enemy") + " " + o.propStr("group");
            if (!q.empty() && foldLower(label).find(q) == std::string::npos) continue;
            byType[o.type].push_back(oi);
        }
        std::vector<std::pair<std::string, std::vector<size_t>>> groups(byType.begin(), byType.end());
        std::stable_sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) { return a.second.size() > b.second.size(); });
        struct TRow {
            bool head;
            std::string type;
            size_t index;
        };
        std::vector<TRow> rows;
        for (const auto& [type, idx] : groups) {
            rows.push_back({true, type, 0});
            if (explorerOpen_.count("type:" + type) || !q.empty())
                for (size_t i : idx) rows.push_back({false, type, i});
        }
        const float rh = ui.s(36);
        ui.beginScroll("typetree", list, rows.size() * rh);
        float y = list.y - ui.scrollOffset();
        for (const auto& row : rows) {
            Rect rr{list.x, y, list.w - ui.s(12), rh - ui.s(2)};
            y += rh;
            if (rr.bottom() < list.y || rr.y > list.bottom()) continue;
            const ObjectTypeInfo* t = ObjectTypes::find(row.type);
            if (row.head) {
                bool open = explorerOpen_.count("type:" + row.type) > 0;
                UIContext::ButtonStyle rs;
                rs.small = true;
                rs.align = Align::Left;
                rs.fontSize = 16;
                std::string label = "      " + (t ? t->name : row.type) + "  (" + std::to_string(byType[row.type].size()) + ")";
                if (ui.button("T" + row.type, rr, label, rs)) {
                    if (open) explorerOpen_.erase("type:" + row.type);
                    else explorerOpen_.insert("type:" + row.type);
                }
                arrow(ui.r(), {rr.x + ui.s(14), rr.center().y}, ui.s(5), open, t ? t->editorColor : ui.theme().accent);
            } else {
                const LevelObject& o = level_.objects[row.index];
                const LevelLayer* l = level_.findLayer(o.layer);
                Rect ir{rr.x + ui.s(22), rr.y, rr.w - ui.s(22), rr.h};
                UIContext::ButtonStyle os;
                os.small = true;
                os.align = Align::Left;
                os.fontSize = 15;
                os.selected = selection_.count(o.id) > 0;
                std::string label = o.id + "  ·  " + (l ? l->name : o.layer) + (o.type == "enemy" ? "  (" + o.propStr("enemy") + ")" : std::string());
                if (ui.button("O" + o.id, ir, label, os)) {
                    if (g_.app.input().shift()) {
                        if (selection_.count(o.id)) selection_.erase(o.id);
                        else selection_.insert(o.id);
                    } else {
                        selection_ = {o.id};
                        cam_.center = o.rect.center();
                    }
                    entityOpen_ = true;
                }
            }
        }
        ui.endScroll();
        return;
    }
    const std::string q = foldLower(outlinerSearch_);
    struct Row {
        bool layer;
        size_t index;  // layer index or object index
        std::string label;
    };
    std::vector<Row> rows;
    for (size_t li = 0; li < level_.layers.size(); ++li) {
        const LevelLayer& l = level_.layers[li];
        std::vector<Row> children;
        for (size_t oi = 0; oi < level_.objects.size(); ++oi) {
            const LevelObject& o = level_.objects[oi];
            if (o.layer != l.id) continue;
            const ObjectTypeInfo* t = ObjectTypes::find(o.type);
            std::string label = (t ? t->name : o.type) + "  ·  " + o.id;
            if (o.type == "enemy") label += " (" + o.propStr("enemy") + ")";
            if (std::string gid = o.propStr("group"); !gid.empty()) label += "  [" + gid + "]";
            if (!q.empty() && foldLower(label + " " + o.type + " " + o.propStr("sprite")).find(q) == std::string::npos) continue;
            children.push_back({false, oi, label});
        }
        bool open = explorerOpen_.count(l.id) > 0 || !q.empty();
        if (!q.empty() && children.empty()) continue;
        rows.push_back({true, li, l.name});
        if (open)
            for (auto& c : children) rows.push_back(std::move(c));
    }
    const float rh = ui.s(36);
    ui.beginScroll("tree", list, rows.size() * rh);
    float y = list.y - ui.scrollOffset();
    for (const auto& row : rows) {
        Rect rr{list.x, y, list.w - ui.s(12), rh - ui.s(2)};
        y += rh;
        if (rr.bottom() < list.y || rr.y > list.bottom()) continue;
        if (row.layer) {
            LevelLayer& l = level_.layers[row.index];
            int count = 0;
            for (const auto& o : level_.objects) count += o.layer == l.id ? 1 : 0;
            bool open = explorerOpen_.count(l.id) > 0;
            bool active = activeLayer_ == l.id;
            // Toggles first (right), the row fills what is left.
            float tw = ui.s(52);
            Rect vis{rr.right() - 2 * tw - ui.s(4), rr.y + ui.s(3), tw, rr.h - ui.s(6)};
            Rect lock{rr.right() - tw, rr.y + ui.s(3), tw, rr.h - ui.s(6)};
            UIContext::ButtonStyle rs;
            rs.small = true;
            rs.align = Align::Left;
            rs.fontSize = 16;
            rs.selected = active;
            Rect main{rr.x, rr.y, vis.x - rr.x - ui.s(4), rr.h};
            std::string label = "      " + l.name + "  (" + std::to_string(count) + ")";
            if (ui.button("L" + l.id, main, label, rs)) {
                activeLayer_ = l.id;
                if (open) explorerOpen_.erase(l.id);
                else explorerOpen_.insert(l.id);
            }
            arrow(ui.r(), {main.x + ui.s(14), main.center().y}, ui.s(5), open, ui.theme().accent);
            UIContext::ButtonStyle ts;
            ts.small = true;
            ts.fontSize = 14;
            ts.selected = l.visible;
            if (ui.button("V" + l.id, vis, l.visible ? "Vue" : "—", ts)) {
                l.visible = !l.visible;
                markChanged("Visibilité du calque");
            }
            ts.selected = l.locked;
            if (ui.button("K" + l.id, lock, l.locked ? "Verr." : "Libre", ts)) {
                l.locked = !l.locked;
                markChanged("Verrou du calque");
            }
        } else {
            const LevelObject& o = level_.objects[row.index];
            const ObjectTypeInfo* t = ObjectTypes::find(o.type);
            Rect ir{rr.x + ui.s(22), rr.y, rr.w - ui.s(22), rr.h};
            ui.r().fillRect({ir.x - ui.s(8), ir.y + ui.s(8), ui.s(4), ir.h - ui.s(16)}, t ? t->editorColor : ui.theme().border);
            UIContext::ButtonStyle os;
            os.small = true;
            os.align = Align::Left;
            os.fontSize = 15;
            os.selected = selection_.count(o.id) > 0;
            if (ui.button("O" + o.id, ir, row.label, os)) {
                if (g_.app.input().shift()) {
                    if (selection_.count(o.id)) selection_.erase(o.id);
                    else selection_.insert(o.id);
                } else {
                    selection_ = {o.id};
                    cam_.center = o.rect.center();
                }
                entityOpen_ = true;
            }
        }
    }
    ui.endScroll();
    drawLayerSettings({r.x, r.bottom() - settingsH + ui.s(8), r.w, settingsH - ui.s(8)});
}

void LevelEditorScene::drawLayerSettings(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.r().fillRect({r.x, r.y - ui.s(4), r.w, 1}, ui.theme().border);
    float y = r.y + ui.s(4);
    LevelLayer* al = nullptr;
    for (auto& l : level_.layers)
        if (l.id == activeLayer_) al = &l;
    if (al) {
        std::string head = "Calque actif";
        ui.label(head, {r.x, y + ui.s(12)}, 15, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
        y += ui.s(28);
        std::string name = al->name;
        if (ui.textField("lname", {r.x, y, r.w, ui.s(40)}, name)) {
            al->name = name;
            markChanged("Renommer le calque");
        }
        y += ui.s(46);
        if (ui.toggle("lfg", {r.x, y, r.w, ui.s(40)}, "Devant le joueur", al->foreground)) markChanged("Calque avant-plan");
        y += ui.s(46);
    }
    float bw = (r.w - ui.s(6)) / 2.0f;
    UIContext::ButtonStyle st;
    st.small = true;
    st.fontSize = 15;
    if (ui.button("ladd", {r.x, y, bw, ui.s(40)}, "Nouveau calque", st)) {
        LevelLayer l;
        int n = static_cast<int>(level_.layers.size()) + 1;
        l.id = "layer_" + std::to_string(n);
        while (level_.findLayer(l.id)) l.id += "b";
        l.name = "Calque " + std::to_string(n);
        l.order = level_.layers.back().order + 5;
        level_.layers.push_back(l);
        activeLayer_ = l.id;
        markChanged("Ajouter un calque");
    }
    UIContext::ButtonStyle d = st;
    d.danger = true;
    bool used = al && std::any_of(level_.objects.begin(), level_.objects.end(), [&](const auto& o) { return o.layer == al->id; });
    d.enabled = al && !used && level_.layers.size() > 1;
    if (ui.button("ldel", {r.x + bw + ui.s(6), y, bw, ui.s(40)}, "Supprimer le calque", d) && al) {
        std::string gone = al->id;
        level_.layers.erase(std::remove_if(level_.layers.begin(), level_.layers.end(), [&](const auto& l) { return l.id == gone; }), level_.layers.end());
        activeLayer_ = level_.layers.front().id;
        markChanged("Supprimer un calque");
        return;
    }
    y += ui.s(46);
    UIContext::ButtonStyle mv = st;
    mv.enabled = !selection_.empty() && !readOnly_;
    if (ui.button("tolayer", {r.x, y, r.w, ui.s(40)}, "Mettre la sélection dans ce calque", mv)) {
        for (auto& o : level_.objects)
            if (selection_.count(o.id)) o.layer = activeLayer_;
        markChanged("Changer de calque");
    }
}


// ------------------------------------------------------------------ library

void LevelEditorScene::drawLibrary(const Rect& r) {
    UIContext& ui = g_.ui;
    static const char* cats[] = {"Plateformes", "Terrain", "Pièges", "Jeu", "Zones", "Ennemis", "Décors", "Favoris"};
    // Two rows of four categories.
    float cw = (r.w - ui.s(18)) / 4.0f, ch = ui.s(32);
    for (int i = 0; i < 8; ++i) {
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = 14;
        st.selected = libraryCategory_ == i;
        st.primary = libraryCategory_ == i;
        if (ui.button(std::string("cat") + std::to_string(i), {r.x + (i % 4) * (cw + ui.s(6)), r.y + (i / 4) * (ch + ui.s(5)), cw, ch}, cats[i], st))
            libraryCategory_ = i;
    }
    float y = r.y + 2 * ch + ui.s(12);
    ui.textField("search", {r.x, y, r.w, ui.s(38)}, librarySearch_, "Rechercher…", 40);
    y += ui.s(44);
    Rect list{r.x, y, r.w, r.bottom() - y};
    const std::string q = foldLower(librarySearch_);
    auto ok = [&](const std::string& text) { return q.empty() || foldLower(text).find(q) != std::string::npos; };
    const auto& pal = ObjectTypes::palette();
    auto inCat = [&](const PaletteEntry& e, int cat) {
        const ObjectTypeInfo* t = ObjectTypes::find(e.type);
        if (!t) return false;
        switch (cat) {
            case 0: return t->category == ObjCategory::Platforms;
            case 1: return t->category == ObjCategory::Terrain;
            case 2: return t->category == ObjCategory::Hazards;
            case 3: return t->category == ObjCategory::Gameplay || t->category == ObjCategory::Interactive || t->category == ObjCategory::Lights;
            case 4: return t->category == ObjCategory::Zones;
            default: return false;
        }
    };
    // Rows: palette entries, enemies and decor sprites, each with its favorite key.
    struct Item {
        int kind;  // 0 palette, 1 enemy, 2 decor
        int palette = -1;
        const EnemyDefinition* enemy = nullptr;
        std::string decor;
        std::string key;
    };
    std::vector<Item> items;
    const bool fav = libraryCategory_ == 7;
    for (int i = 0; i < static_cast<int>(pal.size()); ++i) {
        std::string key = "p:" + pal[i].type + ":" + pal[i].label;
        if ((fav ? favorites_.count(key) > 0 : inCat(pal[i], libraryCategory_)) && ok(pal[i].label + " " + pal[i].type)) items.push_back({0, i, nullptr, {}, key});
    }
    if (libraryCategory_ == 5 || fav)
        for (const EnemyDefinition* e : g_.content.enemies()) {
            std::string key = "e:" + e->id;
            if ((!fav || favorites_.count(key)) && ok(e->name + " " + e->id + " " + familyLabel(e->family))) items.push_back({1, -1, e, {}, key});
        }
    if (libraryCategory_ == 6 || fav) {
        DecorAtlas& atlas = DecorAtlas::shared(g_.app.resources());
        for (const auto& n : atlas.names()) {
            std::string key = "d:" + n;
            if ((!fav || favorites_.count(key)) && ok(n)) items.push_back({2, -1, nullptr, n, key});
        }
    }
    if (items.empty()) {
        ui.labelWrapped(fav ? "Aucun favori : l'étoile à droite d'un élément l'ajoute ici." : "Aucun élément ne correspond.", {list.x, list.y + ui.s(6)}, list.w, 15,
                        ui.theme().textDim);
        return;
    }
    const float rh = ui.s(48), gap = ui.s(5);
    ui.beginScroll("libscroll", list, items.size() * (rh + gap));
    float ry = list.y - ui.scrollOffset();
    auto thumb = [&](const Rect& row, TexturePtr tex, const Rect* src, Color fallback) {
        Rect box{row.x + ui.s(6), row.y + ui.s(5), row.h - ui.s(10), row.h - ui.s(10)};
        ui.r().fillRoundedRect(box, ui.s(4), Color{6, 9, 20, 255});
        if (tex) {
            Rect s = src ? *src : Rect{0, 0, static_cast<float>(tex->width()), static_cast<float>(tex->height())};
            float k = std::min((box.w - ui.s(4)) / s.w, (box.h - ui.s(4)) / s.h);
            ui.r().drawTexture(*tex, &s, Rect::fromCenter(box.center(), {s.w * k, s.h * k}));
        } else {
            ui.r().fillRoundedRect(box.expanded(-ui.s(8)), ui.s(3), fallback);
        }
    };
    for (const auto& it : items) {
        Rect row{list.x, ry, list.w - ui.s(12), rh};
        ry += rh + gap;
        if (row.bottom() < list.y || row.y > list.bottom()) continue;
        const float starW = ui.s(34);
        Rect main{row.x, row.y, row.w - starW - ui.s(4), row.h};
        if (it.kind == 0) {
            const PaletteEntry& e = pal[static_cast<size_t>(it.palette)];
            const ObjectTypeInfo* t = ObjectTypes::find(e.type);
            bool sel = tool_ == Tool::Place && paletteIndex_ == it.palette && placeEnemy_.empty() && placeDecor_.empty();
            if (ui.listItem("p" + std::to_string(it.palette), main, "        " + e.label, sel, "", t ? t->editorColor : Color{})) {
                paletteIndex_ = it.palette;
                placeEnemy_.clear();
                placeDecor_.clear();
                tool_ = Tool::Place;
            }
            TexturePtr tex;
            if (t)
                for (const auto& a : t->assets)
                    if (a.size() > 4 && a.substr(a.size() - 4) == ".png") {
                        tex = g_.app.resources().texture(a);
                        break;
                    }
            Rect src{0, 0, 16, 16};
            bool sheet = tex && tex->width() > 2 * tex->height();  // animation strips: first frame
            if (tex && sheet) src = {0, 0, static_cast<float>(tex->height()), static_cast<float>(tex->height())};
            thumb(main, tex, sheet ? &src : nullptr, t ? t->editorColor : Color{120, 120, 140, 255});
        } else if (it.kind == 1) {
            const EnemyDefinition* e = it.enemy;
            bool sel = tool_ == Tool::Place && placeEnemy_ == e->id;
            if (ui.listItem("e" + e->id, main, "        " + e->name, sel, "", {255, 77, 109, 255})) {
                placeEnemy_ = e->id;
                placeDecor_.clear();
                tool_ = Tool::Place;
            }
            AnimationSetPtr anim = g_.app.resources().animation(e->animation);
            if (anim && anim->texture) {
                const AnimationClip* c = anim->clip("idle");
                if (!c && !anim->clips.empty()) c = &anim->clips.begin()->second;
                Rect src = c ? anim->frameRect(*c, 0) : Rect{0, 0, static_cast<float>(anim->frameWidth), static_cast<float>(anim->frameHeight)};
                thumb(main, anim->texture, &src, {});
            } else {
                thumb(main, nullptr, nullptr, {255, 77, 109, 255});
            }
        } else {
            bool sel = tool_ == Tool::Place && placeDecor_ == it.decor;
            if (ui.listItem("d" + it.decor, main, "        " + it.decor, sel)) {
                placeDecor_ = it.decor;
                placeEnemy_.clear();
                tool_ = Tool::Place;
            }
            DecorAtlas& atlas = DecorAtlas::shared(g_.app.resources());
            if (const DecorSprite* sp = atlas.find(it.decor); sp && sp->texture) {
                Rect src{sp->rect.x, sp->rect.y, sp->rect.w, sp->rect.h};
                thumb(main, sp->texture, &src, {});
            }
        }
        // Favorite.
        Rect sb{row.right() - starW, row.y + (row.h - starW) * 0.5f, starW, starW};
        UIContext::ButtonStyle ss;
        ss.small = true;
        bool on = favorites_.count(it.key) > 0;
        ss.selected = on;
        if (ui.button("f" + it.key, sb, "", ss)) {
            if (on) favorites_.erase(it.key);
            else favorites_.insert(it.key);
            saveFavorites();
        }
        star(ui.r(), sb.center(), ui.s(9), on ? Color{255, 206, 84, 255} : Color{120, 130, 160, 255});
    }
    ui.endScroll();
}

// -------------------------------------------------------------- right panel

void LevelEditorScene::drawRightPanel(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.r().fillRect(r, {12, 16, 32, 245});
    ui.r().fillRect({r.x, r.y, 1, r.h}, ui.theme().border);
    const float hh = ui.s(40);
    auto header = [&](const char* id, const std::string& title, bool& open, float y) {
        Rect h{r.x + ui.s(8), y, r.w - ui.s(16), hh - ui.s(4)};
        UIContext::ButtonStyle st;
        st.small = true;
        st.align = Align::Left;
        st.fontSize = 16;
        if (ui.button(id, h, "      " + title, st)) open = !open;
        arrow(ui.r(), {h.x + ui.s(16), h.center().y}, ui.s(5), open, ui.theme().accent);
    };
    ui.pushId("right");
    // The entity is what the player works on: it gets the room it needs.
    bool lib = libraryOpen_, ent = entityOpen_;
    float avail = r.h - 2 * hh - ui.s(12);
    float libH = lib && ent ? avail * (selection_.empty() ? 0.62f : 0.38f) : lib ? avail : 0.0f;
    float entH = ent ? avail - libH : 0.0f;
    float y = r.y + ui.s(6);
    header("libhead", "BIBLIOTHÈQUE", libraryOpen_, y);
    y += hh;
    if (lib && libH > ui.s(120)) {
        drawLibrary({r.x + ui.s(10), y, r.w - ui.s(20), libH});
        y += libH;
    }
    std::string entTitle = selection_.empty() ? "ENTITÉ" : selection_.size() > 1 ? "ENTITÉ  ·  " + std::to_string(selection_.size()) + " sélectionnés" : "ENTITÉ";
    header("enthead", entTitle, entityOpen_, y + ui.s(4));
    y += hh + ui.s(4);
    if (ent && entH > ui.s(160)) drawProperties({r.x, y, r.w, r.bottom() - y});
    ui.popId();
}

// ---------------------------------------------------------- canvas header

void LevelEditorScene::drawCanvasHeader(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.r().fillRect(r, {10, 14, 30, 250});
    ui.r().fillRect({r.x, r.bottom() - 1, r.w, 1}, ui.theme().border);
    ui.pushId("canvas");
    const float h = r.h - ui.s(10), y = r.y + ui.s(5);
    // Right to left: overlays, zoom, grid and snapping (measured first).
    float x = r.right() - ui.s(8);
    auto small = [&](const std::string& id, const std::string& label, float w, bool selected, bool enabled = true) {
        x -= ui.s(w);
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = 14;
        st.selected = selected;
        st.enabled = enabled;
        bool c = ui.button(id, {x, y, ui.s(w), h}, label, st);
        x -= ui.s(4);
        return c;
    };
    if (small("ovcol", "Collisions", 90, overlayCollisions_)) overlayCollisions_ = !overlayCollisions_;
    if (small("ovzone", "Zones", 62, overlayZones_)) overlayZones_ = !overlayZones_;
    if (small("ovlink", "Liens", 58, overlayLinks_)) overlayLinks_ = !overlayLinks_;
    if (small("ovgrid", "Grille", 62, gridOn_)) gridOn_ = !gridOn_;
    x -= ui.s(10);
    if (small("fit", "Tout le niveau", 118, false)) {
        // The whole level in the view.
        float base = viewport_.h / 270.0f;
        float zx = viewport_.w / (base * std::max(1.0f, level_.bounds.w)), zy = viewport_.h / (base * std::max(1.0f, level_.bounds.h));
        cam_.zoom = std::clamp(std::min(zx, zy) * 0.95f, 0.05f, 8.0f);
        cam_.center = level_.bounds.center();
    }
    if (small("zin", "+", 30, false)) cam_.zoomAt(viewport_, viewport_.center(), 1.25f);
    char zl[16];
    std::snprintf(zl, sizeof zl, "%.0f %%", cam_.zoom * 100.0f);
    if (small("z100", zl, 62, false)) cam_.zoom = 1.0f;
    if (small("zout", "-", 30, false)) cam_.zoomAt(viewport_, viewport_.center(), 1.0f / 1.25f);
    x -= ui.s(10);
    if (small("snap", "Aimanter", 84, snapOn_)) snapOn_ = !snapOn_;
    for (int i = 2; i >= 0; --i)
        if (small("g" + std::to_string(i), std::to_string(static_cast<int>(kGrids[i])), 34, gridIndex_ == i)) {
            gridIndex_ = i;
            snapOn_ = true;
        }
    float right = x - ui.s(8);
    // Breadcrumb in what is left.
    std::string crumb = "Niveau";
    if (const LevelLayer* l = level_.findLayer(activeLayer_)) crumb += "  ›  " + l->name;
    if (selection_.size() == 1) {
        if (const LevelObject* o = level_.findObject(*selection_.begin())) {
            const ObjectTypeInfo* t = ObjectTypes::find(o->type);
            crumb = "Niveau";
            if (const LevelLayer* l = level_.findLayer(o->layer)) crumb += "  ›  " + l->name;
            crumb += "  ›  " + (t ? t->name : o->type) + " · " + o->id;
        }
    } else if (selection_.size() > 1) {
        crumb += "  ›  " + std::to_string(selection_.size()) + " objets sélectionnés";
    }
    ui.label(crumb, {r.x + ui.s(12), r.center().y}, ui.fitFont(crumb, FontFace::Body, 16, right - r.x - ui.s(12)), ui.theme().textDim, Align::Left,
             FontFace::Body, VAlign::Middle);
    ui.popId();
}

// ---------------------------------------------------------- selection table

void LevelEditorScene::drawSelectionTable(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.r().fillRect(r, {10, 14, 30, 250});
    ui.r().fillRect({r.x, r.y, r.w, 1}, ui.theme().border);
    ui.pushId("seltable");
    float x = r.x + ui.s(10), w = r.w - ui.s(20), y = r.y + ui.s(6);
    // Actions.
    std::string head = std::to_string(selection_.size()) + " sélectionnés";
    float hw = ui.text().measure(head, FontFace::Bold, ui.s(16)).x + ui.s(12);
    ui.label(head, {x, y + ui.s(16)}, 16, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
    float bx = x + hw;
    auto act = [&](const char* id, const std::string& label, float bw, bool danger = false) {
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = 14;
        st.danger = danger;
        st.enabled = !readOnly_;
        bool c = ui.button(id, {bx, y, ui.s(bw), ui.s(32)}, label, st);
        bx += ui.s(bw) + ui.s(6);
        return c;
    };
    std::string layerName = level_.findLayer(activeLayer_) ? level_.findLayer(activeLayer_)->name : "?";
    if (act("tolayer", "Vers « " + layerName + " »", 170)) {
        for (auto& o : level_.objects)
            if (selection_.count(o.id)) o.layer = activeLayer_;
        markChanged("Changer de calque");
    }
    if (act("grp", "Grouper", 90)) groupSelection(true);
    if (act("ungrp", "Dégrouper", 100)) groupSelection(false);
    if (act("dup", "Dupliquer", 100)) duplicateSelection();
    if (act("del", "Retirer", 90, true)) {
        deleteSelection();
        ui.popId();
        return;
    }
    y += ui.s(40);
    // Columns.
    struct Col {
        const char* name;
        float frac;
    };
    static const Col cols[] = {{"Type", 0.22f}, {"Id", 0.16f}, {"Calque", 0.15f}, {"Position", 0.15f}, {"Groupe", 0.11f}, {"Relié à", 0.15f}, {"Visible", 0.06f}};
    float cx = x;
    for (const auto& c : cols) {
        ui.label(c.name, {cx + ui.s(4), y + ui.s(10)}, 13, ui.theme().textDim, Align::Left, FontFace::Bold, VAlign::Middle);
        cx += w * c.frac;
    }
    y += ui.s(22);
    Rect list{x, y, w, r.bottom() - y - ui.s(4)};
    std::vector<const LevelObject*> sel;
    for (const auto& o : level_.objects)
        if (selection_.count(o.id)) sel.push_back(&o);
    const float rh = ui.s(28);
    ui.beginScroll("rows", list, sel.size() * rh);
    float ry = list.y - ui.scrollOffset();
    for (const LevelObject* o : sel) {
        Rect row{list.x, ry, list.w - ui.s(12), rh - ui.s(2)};
        ry += rh;
        if (row.bottom() < list.y || row.y > list.bottom()) continue;
        if (ui.button("r" + o->id, row, "")) {
            selection_ = {o->id};
            cam_.center = o->rect.center();
            ui.endScroll();
            ui.popId();
            return;
        }
        const ObjectTypeInfo* t = ObjectTypes::find(o->type);
        std::string links;
        if (t)
            for (const auto& pd : t->props)
                if (pd.type == PropType::ObjectRef && !o->propStr(pd.key.c_str()).empty()) links += (links.empty() ? "" : ", ") + o->propStr(pd.key.c_str());
        const LevelLayer* l = level_.findLayer(o->layer);
        std::string cells[] = {t ? t->name : o->type,
                               o->id,
                               l ? l->name : o->layer,
                               std::format("{:.0f}, {:.0f}", o->rect.x, o->rect.y),
                               o->propStr("group").empty() ? "—" : o->propStr("group"),
                               links.empty() ? "—" : links,
                               l && !l->visible ? "non" : "oui"};
        float c2 = row.x;
        for (size_t i = 0; i < 7; ++i) {
            float cw = w * cols[i].frac - ui.s(8);
            ui.label(cells[i], {c2 + ui.s(4), row.center().y}, ui.fitFont(cells[i], FontFace::Body, 14, cw), i == 0 && t ? t->editorColor : ui.theme().text,
                     Align::Left, FontFace::Body, VAlign::Middle);
            c2 += w * cols[i].frac;
        }
    }
    ui.endScroll();
    ui.popId();
}

// ------------------------------------------------------------------ overlays

void LevelEditorScene::drawLinks(const Rect& viewport) {
    Renderer& r = g_.ui.r();
    WorldView view = cam_.view(viewport);
    r.setView(view);
    for (const auto& o : level_.objects) {
        const LevelLayer* l = level_.findLayer(o.layer);
        if (l && !l->visible) continue;
        const ObjectTypeInfo* t = ObjectTypes::find(o.type);
        if (overlayZones_ && t && (t->category == ObjCategory::Zones)) {
            r.drawWorldRect(o.rect, Color{176, 76, 255, 150}, 1.5f);
        }
        if (overlayCollisions_ && (o.type == "solid" || o.type == "oneway" || o.type == "enemy"))
            r.drawWorldRect(o.rect, o.type == "enemy" ? Color{255, 77, 109, 170} : Color{255, 220, 120, 150}, 1.0f);
        if (!overlayLinks_ || !t) continue;
        for (const auto& pd : t->props) {
            if (pd.type != PropType::ObjectRef) continue;
            std::string ids = o.propStr(pd.key.c_str());
            size_t start = 0;
            while (start < ids.size()) {
                size_t comma = ids.find(',', start);
                std::string id = ids.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                start = comma == std::string::npos ? ids.size() : comma + 1;
                id.erase(0, id.find_first_not_of(' '));
                id.erase(id.find_last_not_of(' ') + 1);
                const LevelObject* target = id.empty() ? nullptr : level_.findObject(id);
                if (!target) continue;
                Vec2 a = view.toScreen(o.rect.center()), b = view.toScreen(target->rect.center());
                bool hot = selection_.count(o.id) || selection_.count(target->id);
                Color c = hot ? Color{255, 179, 71, 255} : Color{255, 179, 71, 130};
                r.drawLine(a, b, c, g_.ui.s(hot ? 2.5f : 1.5f));
                Vec2 d = b - a;
                float len = std::sqrt(d.x * d.x + d.y * d.y);
                if (len > g_.ui.s(20)) {
                    Vec2 u{d.x / len, d.y / len}, n{-u.y, u.x};
                    float s = g_.ui.s(8);
                    Vec2 tip = b - u * g_.ui.s(6);
                    r.fillTriangle(tip, tip - u * s + n * s * 0.6f, tip - u * s - n * s * 0.6f, c);
                }
            }
        }
    }
}

// ---------------------------------------------------------------- favorites

void LevelEditorScene::loadFavorites() {
    favorites_.clear();
    if (auto j = FileUtil::loadJson(Paths::userDir() / "editor" / "favorites.json"); j && j->is_array())
        for (const auto& v : *j)
            if (v.is_string()) favorites_.insert(v.get<std::string>());
}

void LevelEditorScene::saveFavorites() const {
    std::error_code ec;
    fs::create_directories(Paths::userDir() / "editor", ec);
    FileUtil::saveJson(Paths::userDir() / "editor" / "favorites.json", Json(std::vector<std::string>(favorites_.begin(), favorites_.end())));
}

}  // namespace cosmic
