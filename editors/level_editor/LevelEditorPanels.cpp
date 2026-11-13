// Level editor panels: toolbar, asset library, layers, object list, properties, status.

#include <algorithm>
#include <cmath>
#include <format>

#include "editors/level_editor/LevelEditorScene.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/levels/DecorAtlas.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

void LevelEditorScene::drawToolbar(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.r().fillGradientV(r, {18, 26, 50, 255}, {12, 16, 32, 255});
    ui.r().fillRect({r.x, r.bottom() - 1, r.w, 1}, ui.theme().border);
    float x = r.x + ui.s(12), y = r.y + ui.s(9), h = r.h - ui.s(18);
    ui.pushId("tb");
    auto btn = [&](const char* id, const std::string& label, float w, bool primary = false, bool enabled = true,
                   bool selected = false) {
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = 18;
        st.primary = primary;
        st.enabled = enabled;
        st.selected = selected;
        bool c = ui.button(id, {x, y, ui.s(w), h}, label, st);
        x += ui.s(w) + ui.s(6);
        return c;
    };
    if (btn("open", "Ouvrir", 88)) modal_ = Modal::Open;
    if (btn("new", "Nouveau", 98)) {
        modal_ = Modal::New;
        newId_.clear();
        newName_.clear();
    }
    if (btn("save", dirty_ ? "Enregistrer*" : "Enregistrer", 130, dirty_)) save();
    x += ui.s(10);
    if (btn("undo", "Annuler", 92, false, history_.canUndo())) applyJson(history_.undo());
    if (btn("redo", "Rétablir", 96, false, history_.canRedo())) applyJson(history_.redo());
    x += ui.s(10);
    if (btn("sel", "Sélection", 104, false, true, tool_ == Tool::Select)) tool_ = Tool::Select;
    if (btn("pan", "Main", 70, false, true, tool_ == Tool::Pan)) tool_ = Tool::Pan;
    static const char* grids[] = {"Grille 8", "Grille 16", "Grille 32"};
    if (btn("grid", gridOn_ ? grids[gridIndex_] : "Grille off", 112, false, true, gridOn_)) {
        if (!gridOn_) gridOn_ = true;
        else if (gridIndex_ < 2) ++gridIndex_;
        else {
            gridIndex_ = 0;
            gridOn_ = false;
        }
    }
    if (btn("zoomout", "-", 40)) cam_.zoomAt(viewport_, viewport_.center(), 1.0f / 1.25f);
    if (btn("zoomin", "+", 40)) cam_.zoomAt(viewport_, viewport_.center(), 1.25f);
    x += ui.s(10);
    if (btn("settings", "Niveau", 90)) {
        modal_ = Modal::Settings;
        g_.ratings.requestDraft(level_);  // computed on a worker thread, shown in the dialog
    }
    if (btn("events", "Événements", 124)) modal_ = Modal::Events;
    if (btn("cine", "Cinématiques", 136)) modal_ = Modal::Cinematics;
    x += ui.s(10);
    if (btn("test", "Tester (F5)", 126, true)) testPlay(false);
    if (btn("testhere", "Tester ici", 112)) testPlay(true);
    if (btn("help", "?", 40)) modal_ = Modal::Help;
    float qx = r.right() - ui.s(112);
    UIContext::ButtonStyle q;
    q.small = true;
    q.fontSize = 18;
    q.danger = true;
    if (ui.button("quit", {qx, y, ui.s(100), h}, "Quitter", q)) {
        if (dirty_) modal_ = Modal::ConfirmQuit;
        else g_.app.switchScene(makeContentManager(g_));
    }
    ui.popId();
}

void LevelEditorScene::drawLeftPanel(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.r().fillRect(r, {12, 16, 32, 245});
    ui.r().fillRect({r.right() - 1, r.y, 1, r.h}, ui.theme().border);
    int tab = static_cast<int>(leftTab_);
    ui.tabs("lefttabs", {r.x + ui.s(8), r.y + ui.s(8), r.w - ui.s(16), ui.s(42)}, {"Bibliothèque", "Calques", "Objets"}, tab);
    leftTab_ = static_cast<LeftTab>(tab);
    Rect c{r.x + ui.s(10), r.y + ui.s(60), r.w - ui.s(20), r.h - ui.s(70)};
    ui.pushId("left");
    if (leftTab_ == LeftTab::Library) drawLibrary(c);
    else if (leftTab_ == LeftTab::Layers) drawLayers(c);
    else drawObjectList(c);
    ui.popId();
}

void LevelEditorScene::drawLibrary(const Rect& r) {
    UIContext& ui = g_.ui;
    static const std::vector<std::string> cats = {"Terrain", "Gameplay", "Ennemis", "Décors"};
    ui.cycle("cat", {r.x, r.y, r.w, ui.s(44)}, "Catégorie", libraryCategory_, cats);
    Rect list{r.x, r.y + ui.s(54), r.w, r.h - ui.s(54)};
    const auto& pal = ObjectTypes::palette();
    std::vector<int> items;
    auto inCat = [&](const PaletteEntry& e) {
        const ObjectTypeInfo* t = ObjectTypes::find(e.type);
        if (!t) return false;
        switch (libraryCategory_) {
            case 0:
                return t->category == ObjCategory::Terrain || t->category == ObjCategory::Platforms ||
                       t->category == ObjCategory::Hazards;
            case 1:
                return t->category == ObjCategory::Gameplay || t->category == ObjCategory::Interactive ||
                       t->category == ObjCategory::Zones || t->category == ObjCategory::Lights;
            default: return false;
        }
    };
    float rh = ui.s(50), gap = ui.s(6);
    if (libraryCategory_ <= 1) {
        for (int i = 0; i < static_cast<int>(pal.size()); ++i)
            if (inCat(pal[i])) items.push_back(i);
        ui.beginScroll("palscroll", list, items.size() * (rh + gap));
        float y = list.y - ui.scrollOffset();
        for (int i : items) {
            const ObjectTypeInfo* t = ObjectTypes::find(pal[i].type);
            bool sel = tool_ == Tool::Place && paletteIndex_ == i && placeEnemy_.empty() && placeDecor_.empty();
            if (ui.listItem("p" + std::to_string(i), {list.x, y, list.w - ui.s(12), rh}, pal[i].label, sel, "",
                            t ? t->editorColor : Color{})) {
                paletteIndex_ = i;
                placeEnemy_.clear();
                placeDecor_.clear();
                tool_ = Tool::Place;
            }
            y += rh + gap;
        }
        ui.endScroll();
    } else if (libraryCategory_ == 2) {
        auto enemies = g_.content.enemies();
        ui.beginScroll("enscroll", list, enemies.size() * (ui.s(70) + gap));
        float y = list.y - ui.scrollOffset();
        for (size_t i = 0; i < enemies.size(); ++i) {
            const EnemyDefinition* e = enemies[i];
            bool sel = tool_ == Tool::Place && placeEnemy_ == e->id;
            if (ui.listItem("e" + std::to_string(i), {list.x, y, list.w - ui.s(12), ui.s(70)}, e->name, sel,
                            std::string(familyLabel(e->family)) + (e->builtin ? "" : " · perso"), {255, 77, 109, 255})) {
                placeEnemy_ = e->id;
                placeDecor_.clear();
                tool_ = Tool::Place;
            }
            y += ui.s(70) + gap;
        }
        ui.endScroll();
    } else {
        DecorAtlas& atlas = DecorAtlas::shared(g_.app.resources());
        auto names = atlas.names();
        float cell = (list.w - ui.s(24)) / 3.0f;
        int rows = (static_cast<int>(names.size()) + 2) / 3;
        ui.beginScroll("decscroll", list, rows * (cell + gap));
        float y0 = list.y - ui.scrollOffset();
        for (size_t i = 0; i < names.size(); ++i) {
            Rect cr{list.x + (i % 3) * (cell + ui.s(4)), y0 + (i / 3) * (cell + gap), cell, cell};
            bool sel = tool_ == Tool::Place && placeDecor_ == names[i];
            if (ui.listItem("d" + std::to_string(i), cr, "", sel)) {
                placeDecor_ = names[i];
                placeEnemy_.clear();
                tool_ = Tool::Place;
            }
            if (const DecorSprite* s = atlas.find(names[i]); s && s->texture) {
                Rect src{s->rect.x, s->rect.y, s->rect.w, s->rect.h};
                float k = std::min((cell - ui.s(16)) / src.w, (cell - ui.s(16)) / src.h);
                ui.r().drawTexture(*s->texture, &src, Rect::fromCenter(cr.center(), {src.w * k, src.h * k}));
            }
        }
        ui.endScroll();
    }
}

void LevelEditorScene::drawLayers(const Rect& r) {
    UIContext& ui = g_.ui;
    float y = r.y;
    float rh = ui.s(50);
    for (size_t i = 0; i < level_.layers.size(); ++i) {
        LevelLayer& l = level_.layers[i];
        std::string id = "layer" + std::to_string(i);
        Rect row{r.x, y, r.w, rh};
        int count = 0;
        for (const auto& o : level_.objects) count += o.layer == l.id ? 1 : 0;
        if (ui.listItem(id, {row.x, row.y, row.w - ui.s(150), rh}, l.name, activeLayer_ == l.id,
                        std::to_string(count) + " objets" + (l.foreground ? " · avant-plan" : "")))
            activeLayer_ = l.id;
        UIContext::ButtonStyle s;
        s.small = true;
        s.fontSize = 16;
        s.selected = l.visible;
        if (ui.button(id + "vis", {row.right() - ui.s(144), row.y + ui.s(5), ui.s(46), rh - ui.s(10)}, l.visible ? "Vue" : "—", s)) {
            l.visible = !l.visible;
            markChanged("Visibilité du calque");
        }
        s.selected = l.locked;
        if (ui.button(id + "lock", {row.right() - ui.s(94), row.y + ui.s(5), ui.s(46), rh - ui.s(10)}, l.locked ? "Verr." : "Libre", s)) {
            l.locked = !l.locked;
            markChanged("Verrou du calque");
        }
        s.selected = false;
        if (ui.button(id + "up", {row.right() - ui.s(44), row.y + ui.s(5), ui.s(40), rh - ui.s(10)}, "^", s) && i > 0) {
            std::swap(level_.layers[i].order, level_.layers[i - 1].order);
            level_.sortLayers();
            markChanged("Ordre des calques");
        }
        y += rh + ui.s(6);
    }
    y += ui.s(10);
    if (LevelLayer* al = [&]() -> LevelLayer* {
            for (auto& l : level_.layers)
                if (l.id == activeLayer_) return &l;
            return nullptr;
        }()) {
        ui.label("Calque actif", {r.x, y}, 18, ui.theme().textDim);
        y += ui.s(28);
        std::string name = al->name;
        if (ui.textField("lname", {r.x, y, r.w, ui.s(44)}, name)) {
            al->name = name;
            markChanged("Renommer le calque");
        }
        y += ui.s(52);
        if (ui.toggle("lfg", {r.x, y, r.w, ui.s(44)}, "Devant le joueur", al->foreground)) markChanged("Calque avant-plan");
        y += ui.s(52);
        UIContext::ButtonStyle d;
        d.danger = true;
        d.small = true;
        bool used = std::any_of(level_.objects.begin(), level_.objects.end(), [&](const auto& o) { return o.layer == al->id; });
        d.enabled = !used && level_.layers.size() > 1;
        if (ui.button("ldel", {r.x, y, r.w * 0.48f, ui.s(44)}, "Supprimer", d)) {
            std::string gone = al->id;
            level_.layers.erase(std::remove_if(level_.layers.begin(), level_.layers.end(), [&](const auto& l) { return l.id == gone; }),
                                level_.layers.end());
            activeLayer_ = level_.layers.front().id;
            markChanged("Supprimer un calque");
            return;
        }
    }
    UIContext::ButtonStyle st;
    st.small = true;
    if (ui.button("ladd", {r.x + r.w * 0.52f, y, r.w * 0.48f, ui.s(44)}, "Nouveau calque", st)) {
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
    y += ui.s(56);
    if (!selection_.empty()) {
        UIContext::ButtonStyle mv;
        mv.small = true;
        if (ui.button("tolayer", {r.x, y, r.w, ui.s(44)}, "Déplacer la sélection ici", mv)) {
            for (auto& o : level_.objects)
                if (selection_.count(o.id)) o.layer = activeLayer_;
            markChanged("Changer de calque");
        }
    }
}

void LevelEditorScene::drawObjectList(const Rect& r) {
    UIContext& ui = g_.ui;
    float rh = ui.s(44), gap = ui.s(4);
    ui.beginScroll("objlist", r, level_.objects.size() * (rh + gap));
    float y = r.y - ui.scrollOffset();
    for (size_t i = 0; i < level_.objects.size(); ++i) {
        const LevelObject& o = level_.objects[i];
        if (y > r.y - rh && y < r.bottom()) {
            const ObjectTypeInfo* t = ObjectTypes::find(o.type);
            std::string label = o.id + "  " + (t ? t->name : o.type);
            if (o.type == "enemy") label += " (" + o.propStr("enemy") + ")";
            if (o.type == "decor") label += " (" + o.propStr("sprite") + ")";
            if (ui.listItem("o" + std::to_string(i), {r.x, y, r.w - ui.s(12), rh}, label, selection_.count(o.id) > 0, "",
                            t ? t->editorColor : Color{})) {
                selection_ = {o.id};
                cam_.center = o.rect.center();
            }
        }
        y += rh + gap;
    }
    ui.endScroll();
}

void LevelEditorScene::drawProperties(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.r().fillRect(r, {12, 16, 32, 245});
    ui.r().fillRect({r.x, r.y, 1, r.h}, ui.theme().border);
    float x = r.x + ui.s(14), w = r.w - ui.s(28);
    float y = r.y + ui.s(12);
    ui.pushId("props");
    if (selection_.empty()) {
        ui.label("Propriétés", {x, y}, 24, ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(40);
        ui.labelWrapped("Sélectionnez un objet dans la vue, ou choisissez un élément dans la bibliothèque puis cliquez "
                        "(ou tracez un rectangle) pour le placer.\n\nF1 : liste des raccourcis.",
                        {x, y}, w, 19, ui.theme().textDim);
        ui.popId();
        return;
    }
    if (selection_.size() > 1) {
        ui.label(std::to_string(selection_.size()) + " objets sélectionnés", {x, y}, 22, ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(50);
        UIContext::ButtonStyle d;
        d.danger = true;
        if (ui.button("delmany", {x, y, w, ui.s(48)}, "Supprimer la sélection", d)) deleteSelection();
        y += ui.s(58);
        if (ui.button("dupmany", {x, y, w, ui.s(48)}, "Dupliquer (Ctrl+D)")) duplicateSelection();
        ui.popId();
        return;
    }
    LevelObject* o = primary();
    if (!o) {
        ui.popId();
        return;
    }
    const ObjectTypeInfo* t = ObjectTypes::find(o->type);
    ui.label(t ? t->name : o->type, {x, y}, 24, t ? t->editorColor : ui.theme().text, Align::Left, FontFace::Bold);
    ui.label(o->id, {x + w, y + ui.s(6)}, 18, ui.theme().textDim, Align::Right);
    y += ui.s(40);
    Rect area{r.x, y, r.w, r.bottom() - y - ui.s(70)};
    float contentH = ui.s(60) * 6 + ui.s(56) * static_cast<float>((t ? t->props.size() : 0) + 3) + ui.s(120);
    ui.beginScroll("propscroll", area, contentH);
    float sy = y - ui.scrollOffset();
    bool changed = false;
    FieldRow row{ui, x, w - ui.s(10), (w - ui.s(10)) * 0.42f, sy};
    // Layer
    {
        std::vector<std::string> names, ids;
        int cur = 0;
        for (size_t i = 0; i < level_.layers.size(); ++i) {
            names.push_back(level_.layers[i].name);
            ids.push_back(level_.layers[i].id);
            if (level_.layers[i].id == o->layer) cur = static_cast<int>(i);
        }
        if (ui.cycle("layer", {x, sy, w - ui.s(10), ui.s(46)}, "Calque", cur, names)) {
            o->layer = ids[cur];
            changed = true;
        }
        sy += ui.s(54);
    }
    float step = snapFor(*o);
    changed |= ui.numberField("x", row.field("X"), o->rect.x, step, -1e5f, 1e6f, "%.0f");
    changed |= ui.numberField("y", row.field("Y"), o->rect.y, step, -1e5f, 1e6f, "%.0f");
    if (!t || t->resizable) {
        changed |= ui.numberField("w", row.field("Largeur"), o->rect.w, step, 1, 1e5f, "%.0f");
        changed |= ui.numberField("h", row.field("Hauteur"), o->rect.h, step, 1, 1e5f, "%.0f");
    }
    if (t && (t->rotatable || t->freeRotation)) changed |= ui.numberField("rot", row.field("Rotation (°)"), o->rotation, t->freeRotation ? 15.0f : 90.0f, -360, 360, "%.0f");
    float zf = static_cast<float>(o->z);
    if (ui.numberField("z", row.field("Ordre (z)"), zf, 1, -100, 100, "%.0f")) {
        o->z = static_cast<int>(zf);
        changed = true;
    }
    if (ui.toggle("flip", {x, sy, w - ui.s(10), ui.s(44)}, "Miroir horizontal", o->flipX)) changed = true;
    sy += ui.s(54);
    if (t) {
        ui.separator({x, sy}, w);
        sy += ui.s(12);
        changed |= props_.edit(g_, t->props, o->props, x, sy, w - ui.s(10));
        if (!t->help.empty()) {
            sy += ui.s(6);
            Vec2 hs = ui.labelWrapped(t->help, {x, sy}, w - ui.s(10), 17, ui.theme().textDim);
            sy += hs.y;
        }
        // Enemy: show the definition summary + shortcut to the enemy editor.
        if (o->type == "enemy") {
            if (const EnemyDefinition* d = g_.content.enemy(o->propStr("enemy"))) {
                sy += ui.s(10);
                ui.label(std::format("PV {:.0f} · vitesse {:.0f} · {}", d->hp, d->speed, movementLabel(d->movement)), {x, sy}, 17,
                         ui.theme().textDim);
                sy += ui.s(28);
                o->rect.w = d->size.x;
                o->rect.h = d->size.y;
            }
        }
    }
    ui.endScroll();
    if (changed) markChanged("Propriétés");
    UIContext::ButtonStyle d;
    d.danger = true;
    d.small = true;
    if (ui.button("delete", {x, r.bottom() - ui.s(60), w * 0.48f, ui.s(48)}, "Supprimer", d)) deleteSelection();
    UIContext::ButtonStyle s;
    s.small = true;
    if (ui.button("dup", {x + w * 0.52f, r.bottom() - ui.s(60), w * 0.48f, ui.s(48)}, "Dupliquer", s)) duplicateSelection();
    ui.popId();
}

void LevelEditorScene::drawStatus(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.r().fillRect(r, {6, 9, 20, 255});
    Vec2 world = cam_.view(viewport_).toWorld(g_.app.input().mousePos());
    std::string s = std::format("{}  ·  {} objets  ·  {} événements  ·  x {:.0f}  y {:.0f}  (tuile {}, {})  ·  zoom {:.0f}%  ·  calque : {}",
                                level_.name + " [" + level_.id + "]", level_.objects.size(), level_.events.size(), world.x,
                                world.y, static_cast<int>(std::floor(world.x / 16)), static_cast<int>(std::floor(world.y / 16)),
                                cam_.zoom * 100, level_.findLayer(activeLayer_) ? level_.findLayer(activeLayer_)->name : "?");
    ui.label(s, {r.x + ui.s(14), r.center().y}, 17, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    std::string right = dirty_ ? "Modifications non enregistrées" : (lastSaved_.empty() ? "" : "Enregistré à " + lastSaved_);
    if (!pickTarget_.empty()) right = "Choisissez un objet cible…";
    ui.label(right, {r.right() - ui.s(14), r.center().y}, 17, dirty_ ? ui.theme().warm : ui.theme().textDim, Align::Right,
             FontFace::Body, VAlign::Middle);
}

}  // namespace cosmic
