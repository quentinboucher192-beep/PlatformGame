// Level editor panels: toolbar, asset library, layers, object list, properties, status.

#include <algorithm>
#include <cmath>
#include <format>

#include "editors/level_editor/LevelEditorScene.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/gameplay/BossBadge.h"
#include "game/levels/DecorAtlas.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

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
        ui.label("Aligner", {x, y}, 17, ui.theme().accent, Align::Left, FontFace::Bold);
        y += ui.s(28);
        static const char* labels[] = {"Gauche", "Centre H", "Droite", "Haut", "Centre V", "Bas"};
        UIContext::ButtonStyle sm;
        sm.small = true;
        sm.enabled = !readOnly_;
        float bw = (w - ui.s(12)) / 3.0f;
        for (int i = 0; i < 6; ++i)
            if (ui.button(std::string("al") + std::to_string(i), {x + (i % 3) * (bw + ui.s(6)), y + (i / 3) * ui.s(46), bw, ui.s(40)}, labels[i], sm))
                alignSelection(i);
        y += ui.s(98);
        sm.enabled = !readOnly_ && selection_.size() >= 3;
        if (ui.button("sph", {x, y, w * 0.49f, ui.s(40)}, "Répartir H", sm)) alignSelection(6);
        if (ui.button("spv", {x + w * 0.51f, y, w * 0.49f, ui.s(40)}, "Répartir V", sm)) alignSelection(7);
        y += ui.s(56);
        ui.label("Groupe", {x, y}, 17, ui.theme().accent, Align::Left, FontFace::Bold);
        y += ui.s(28);
        sm.enabled = !readOnly_;
        if (ui.button("grp", {x, y, w * 0.49f, ui.s(40)}, "Grouper (Ctrl+G)", sm)) groupSelection(true);
        if (ui.button("ungrp", {x + w * 0.51f, y, w * 0.49f, ui.s(40)}, "Dégrouper", sm)) groupSelection(false);
        y += ui.s(46);
        ui.labelWrapped("Un clic sur un objet groupé sélectionne tout son groupe (Alt+clic : l'objet seul).", {x, y}, w, 14, ui.theme().textDim);
        y += ui.s(50);
        UIContext::ButtonStyle d;
        d.danger = true;
        d.enabled = !readOnly_;
        if (ui.button("delmany", {x, y, w, ui.s(48)}, "Supprimer la sélection", d)) deleteSelection();
        y += ui.s(58);
        if (ui.button("dupmany", {x, y, w, ui.s(48)}, "Dupliquer (Ctrl+D)", sm)) duplicateSelection();
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
    float contentH = ui.s(60) * 6 + ui.s(56) * static_cast<float>((t ? t->props.size() : 0) + 3) + ui.s(120) + ui.s(38) * 8;
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
        // Relations: the objects that point to this one and the events that use it.
        {
            auto listed = [](const std::string& list, const std::string& id) {
                size_t start = 0;
                while (start <= list.size()) {
                    size_t comma = list.find(',', start);
                    std::string part = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                    part.erase(0, part.find_first_not_of(' '));
                    if (!part.empty()) part.erase(part.find_last_not_of(' ') + 1);
                    if (part == id) return true;
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
                return false;
            };
            std::vector<std::pair<std::string, std::string>> from;  // id, label
            for (const auto& other : level_.objects) {
                if (other.id == o->id) continue;
                const ObjectTypeInfo* ot = ObjectTypes::find(other.type);
                if (!ot) continue;
                for (const auto& pd : ot->props)
                    if (pd.type == PropType::ObjectRef && listed(other.propStr(pd.key.c_str()), o->id)) {
                        from.push_back({other.id, "Activé par : " + ot->name + " · " + other.id});
                        break;
                    }
            }
            std::vector<std::pair<int, std::string>> evs;
            for (size_t i = 0; i < level_.events.size(); ++i) {
                const LevelEvent& ev = level_.events[i];
                std::string name = ev.name.empty() ? ev.id : ev.name;
                if (ev.source == o->id) evs.push_back({static_cast<int>(i), "Déclenche l'événement « " + name + " »"});
                else
                    for (const auto& a : ev.actions)
                        if (listed(a.target, o->id)) {
                            evs.push_back({static_cast<int>(i), "Utilisé par l'événement « " + name + " »"});
                            break;
                        }
            }
            if (!from.empty() || !evs.empty()) {
                sy += ui.s(10);
                ui.label("Relations", {x, sy}, 17, ui.theme().accent, Align::Left, FontFace::Bold);
                sy += ui.s(28);
                UIContext::ButtonStyle rs;
                rs.small = true;
                rs.fontSize = 14;
                rs.align = Align::Left;
                std::string pickId;
                for (const auto& [id, label] : from) {
                    if (ui.button("rel" + id, {x, sy, w - ui.s(10), ui.s(34)}, label, rs)) pickId = id;
                    sy += ui.s(38);
                }
                for (const auto& [i, label] : evs) {
                    if (ui.button("ev" + std::to_string(i), {x, sy, w - ui.s(10), ui.s(34)}, label, rs)) {
                        selectedEvent_ = i;
                        modal_ = Modal::Events;
                    }
                    sy += ui.s(38);
                }
                if (!pickId.empty()) {
                    if (const LevelObject* target = level_.findObject(pickId)) cam_.center = target->rect.center();
                    selection_ = {pickId};
                    ui.endScroll();
                    ui.popId();
                    return;
                }
            }
        }
        // Enemy: show the definition summary + shortcut to the enemy editor.
        if (o->type == "enemy") {
            if (const EnemyDefinition* d = g_.content.enemy(o->propStr("enemy"))) {
                sy += ui.s(10);
                ui.label(std::format("PV {:.0f} · vitesse {:.0f} · {}", d->hp, d->speed, movementLabel(d->movement)), {x, sy}, 17,
                         ui.theme().textDim);
                sy += ui.s(28);
                if (d->isBoss) {
                    // Its badge on the progress bar (Aspect, chosen in the enemy editor).
                    Rect pv{x, sy, ui.s(56), ui.s(56)};
                    ui.r().fillRoundedRect(pv, ui.s(10), {6, 9, 20, 255});
                    BossBadge::draw(ui.r(), g_.app.resources(), *d, pv.center(), ui.s(18), false, ui.time());
                    ui.labelWrapped("Icône de ce boss sur la barre de progression (Aspect : éditeur d'ennemis, onglet Boss).",
                                    {pv.right() + ui.s(12), sy + ui.s(4)}, w - pv.w - ui.s(22), 15, ui.theme().textDim);
                    sy += ui.s(66);
                }
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
    d.enabled = !readOnly_;
    if (ui.button("delete", {x, r.bottom() - ui.s(60), w * 0.31f, ui.s(48)}, "Supprimer", d)) deleteSelection();
    UIContext::ButtonStyle s;
    s.small = true;
    s.enabled = !readOnly_;
    if (ui.button("dup", {x + w * 0.345f, r.bottom() - ui.s(60), w * 0.31f, ui.s(48)}, "Dupliquer", s)) duplicateSelection();
    UIContext::ButtonStyle tp;
    tp.small = true;
    tp.primary = true;
    // Play from this object (a checkpoint, a ledge, the boss arena...): feet on top of it.
    if (ui.button("testobj", {x + w * 0.69f, r.bottom() - ui.s(60), w * 0.31f, ui.s(48)}, "Tester ici", tp)) {
        LevelObject* po = primary();
        if (po) testFrom(po->type == "solid" || po->type == "oneway" ? Vec2{po->rect.center().x, po->rect.y} : po->rect.bottomCenter());
    }
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
    std::string right = dirty_ ? "Modifications non enregistrées" : (lastSaved_.empty() ? "" : "Enregistré à " + lastSaved_);
    if (live_) right = "En direct · révision " + std::to_string(live_->revision()) + " · opération " + std::to_string(live_->seq()) +
                       (readOnly_ ? " · lecture seule (" + std::string(liveRole_ == "tester" ? "testeur" : "lecteur") + ")" : "");
    if (!pickTarget_.empty()) right = "Choisissez un objet cible…";
    Vec2 rs = ui.label(right, {r.right() - ui.s(14), r.center().y}, 17, dirty_ ? ui.theme().warm : ui.theme().textDim, Align::Right,
                       FontFace::Body, VAlign::Middle);
    float leftRoom = r.w - rs.x - ui.s(48);
    // Problems found by the checks: a click opens them.
    if (!issues_.empty()) {
        int errors = 0;
        for (const auto& is : issues_) errors += is.error ? 1 : 0;
        std::string w = std::to_string(issues_.size()) + (issues_.size() > 1 ? " avertissements" : " avertissement");
        float ww = ui.text().measure(w, FontFace::Bold, ui.s(15)).x + ui.s(24);
        leftRoom -= ww + ui.s(14);
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = 15;
        st.danger = errors > 0;
        if (ui.button("warnings", {r.right() - ui.s(28) - rs.x - ww, r.y + ui.s(4), ww, r.h - ui.s(8)}, w, st)) leftTab_ = LeftTab::Check;
    }
    ui.label(s, {r.x + ui.s(14), r.center().y}, ui.fitFont(s, FontFace::Body, 17, leftRoom), ui.theme().textDim, Align::Left, FontFace::Body,
             VAlign::Middle);
}

}  // namespace cosmic
