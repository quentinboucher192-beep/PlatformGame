// Level editor tools: minimap, read-only banner (campaign levels), history panel and the
// validation panel (local checks + shortcut detector and difficulty, on a worker thread).

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>

#include "editors/level_editor/LevelEditorScene.h"
#include "game/GameServices.h"
#include "game/levels/ObjectTypes.h"

namespace cosmic {

namespace {
std::vector<std::string> splitIds(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',' || c == ';' || c == ' ') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
}  // namespace

// ------------------------------------------------------------------ minimap

void LevelEditorScene::drawMinimap(const Rect& vp) {
    minimapRect_ = {};
    if (!minimapOn_ || level_.bounds.w <= 0 || level_.bounds.h <= 0) return;
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
    const Rect& b = level_.bounds;
    float w = std::min(ui.s(360), vp.w * 0.35f);
    float k = w / b.w;
    float h = std::min(b.h * k, vp.h * 0.3f);
    k = std::min(k, h / b.h);
    w = b.w * k;
    h = b.h * k;
    Rect m{vp.right() - w - ui.s(14), vp.bottom() - h - ui.s(14), w, h};
    minimapRect_ = m.expanded(ui.s(4));
    r.fillRoundedRect(minimapRect_, ui.s(6), Color{6, 9, 20, 225});
    r.drawRoundedRect(minimapRect_, ui.s(6), ui.theme().border, ui.s(1.2f));
    auto toMap = [&](const Rect& wr) {
        return Rect{m.x + (wr.x - b.x) * k, m.y + (wr.y - b.y) * k, std::max(1.0f, wr.w * k), std::max(1.0f, wr.h * k)};
    };
    for (const auto& o : level_.objects) {
        const LevelLayer* l = level_.findLayer(o.layer);
        if (l && !l->visible) continue;
        Color c{};
        if (o.type == "solid") c = {120, 132, 160, 230};
        else if (o.type == "oneway" || o.type == "moving_platform" || o.type == "crumble_platform") c = {180, 160, 110, 230};
        else if (o.type == "liquid") c = o.propStr("kind", "acid") == "water" || o.propStr("kind") == "icewater" ? Color{70, 140, 255, 200}
                                                                                                                 : Color{255, 80, 60, 210};
        else if (o.type == "spikes" || o.type == "laser" || o.type == "damage_zone") c = {255, 77, 109, 230};
        else continue;
        Rect mr = toMap(o.rect);
        if (std::abs(o.rotation) > 45.0f && std::abs(o.rotation) < 135.0f) mr = Rect::fromCenter(mr.center(), {mr.h, mr.w});
        r.fillRect(mr, c);
    }
    auto dot = [&](Vec2 p, Color c, float s) { r.fillRect(Rect::fromCenter({m.x + (p.x - b.x) * k, m.y + (p.y - b.y) * k}, {s, s}), c); };
    for (const auto& o : level_.objects) {
        if (o.type == "enemy") dot(o.rect.center(), o.prop<bool>("boss", false) ? Color{255, 60, 200, 255} : Color{255, 90, 90, 255},
                                   ui.s(o.prop<bool>("boss", false) ? 6 : 3));
        else if (o.type == "checkpoint") dot(o.rect.center(), {120, 230, 140, 255}, ui.s(5));
        else if (o.type == "player_start") dot(o.rect.center(), {57, 240, 216, 255}, ui.s(6));
        else if (o.type == "exit") dot(o.rect.center(), {255, 217, 138, 255}, ui.s(6));
    }
    for (const auto& id : selection_)
        if (const LevelObject* o = level_.findObject(id)) r.drawRect(toMap(o->rect).expanded(1.5f), ui.theme().accent, 1.0f);
    // The part of the level the viewport shows.
    WorldView view = cam_.view(vp);
    Vec2 tl = view.toWorld({vp.x, vp.y}), br = view.toWorld({vp.right(), vp.bottom()});
    Rect seen = toMap({tl.x, tl.y, br.x - tl.x, br.y - tl.y});
    r.pushClip(m);
    r.drawRect(seen, Color{255, 255, 255, 220}, 1.5f);
    r.popClip();
    // Click or drag: centre the view there.
    InputManager& in = g_.app.input();
    if (modal_ == Modal::None && in.mouseDown(SDL_BUTTON_LEFT) && m.contains(in.mousePos()) && drag_ == Drag::None) {
        Vec2 mp = in.mousePos();
        cam_.center = {b.x + (mp.x - m.x) / k, b.y + (mp.y - m.y) / k};
    }
    ui.label("Carte (M)", {minimapRect_.x + ui.s(6), minimapRect_.y - ui.s(10)}, 13, ui.theme().textDim, Align::Left, FontFace::Body,
             VAlign::Middle);
}

// ------------------------------------------------------------ read-only banner

void LevelEditorScene::drawReadOnlyBanner(const Rect& vp) {
    bannerRect_ = {};
    if (!readOnly_) return;
    UIContext& ui = g_.ui;
    Rect bar{vp.x + ui.s(16), vp.y + ui.s(14), std::min(vp.w - ui.s(32), ui.s(900)), ui.s(58)};
    bar.x = vp.center().x - bar.w * 0.5f;
    bannerRect_ = bar;
    ui.r().fillRoundedRect(bar, ui.s(10), Color{40, 26, 10, 235});
    ui.r().drawRoundedRect(bar, ui.s(10), ui.theme().warm, ui.s(1.5f));
    std::string what = readOnlySource_.empty() ? "Niveau de la campagne" : "Niveau importé";
    ui.label(what + " — lecture seule", {bar.x + ui.s(18), bar.y + ui.s(19)}, 19, ui.theme().warm, Align::Left, FontFace::Bold,
             VAlign::Middle);
    ui.label("Vous pouvez l'explorer et le tester ; pour le modifier, faites-en votre copie.", {bar.x + ui.s(18), bar.y + ui.s(41)}, 15,
             ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    UIContext::ButtonStyle p;
    p.primary = true;
    p.small = true;
    if (ui.button("dupedit", {bar.right() - ui.s(270), bar.y + ui.s(9), ui.s(256), bar.h - ui.s(18)}, "Dupliquer pour modifier", p))
        duplicateForEditing();
}

// ------------------------------------------------------------------ history

void LevelEditorScene::drawHistory(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.labelWrapped("Chaque modification est gardée : cliquez sur une étape pour y revenir (Ctrl+Z / Ctrl+Y pour avancer pas à pas).",
                    {r.x, r.y}, r.w, 15, ui.theme().textDim);
    Rect list{r.x, r.y + ui.s(54), r.w, r.h - ui.s(54)};
    float rh = ui.s(40), gap = ui.s(4);
    size_t n = history_.size();
    ui.beginScroll("history", list, static_cast<float>(n) * (rh + gap));
    float y = list.y - ui.scrollOffset();
    for (size_t i = 0; i < n; ++i) {
        size_t idx = n - 1 - i;  // newest first
        if (y > list.y - rh && y < list.bottom()) {
            bool cur = idx == history_.index();
            std::string label = std::format("{}. {}", idx + 1, history_.label(idx));
            Color c = idx > history_.index() ? ui.theme().textDim : Color{};
            if (ui.listItem("h" + std::to_string(idx), {list.x, y, list.w - ui.s(12), rh}, label, cur, idx > history_.index() ? "annulé" : "", c) &&
                !cur && !readOnly_)
                applyJson(history_.jump(idx));
        }
        y += rh + gap;
    }
    ui.endScroll();
}

// --------------------------------------------------------------- validation

void LevelEditorScene::runChecks() {
    checksDirty_ = false;
    issues_.clear();
    auto add = [&](std::string text, std::string id = {}, bool error = false) { issues_.push_back({std::move(text), std::move(id), error}); };
    int starts = 0, exits = 0, checkpoints = 0, bosses = 0;
    std::set<std::string> ids;
    for (const auto& o : level_.objects) ids.insert(o.id);
    const Rect& b = level_.bounds;
    for (const auto& o : level_.objects) {
        if (o.type == "player_start") ++starts;
        if (o.type == "exit") ++exits;
        if (o.type == "checkpoint") ++checkpoints;
        if (!ObjectTypes::find(o.type)) add("Type d'objet inconnu : " + o.type, o.id, true);
        if (!o.rect.intersects(b)) add(o.id + " est hors des limites du niveau", o.id, true);
        if (o.type == "enemy") {
            const EnemyDefinition* d = g_.content.enemy(o.propStr("enemy"));
            if (!d) add(o.id + " : ennemi « " + o.propStr("enemy") + " » introuvable", o.id, true);
            else if (d->isBoss || o.prop<bool>("boss", false)) ++bosses;
        }
        for (const char* key : {"targets", "doors", "waves"})
            for (const auto& ref : splitIds(o.propStr(key)))
                if (!ids.count(ref)) add(o.id + " : la référence « " + ref + " » n'existe pas", o.id, true);
        if ((o.type == "lever" || o.type == "receptor") && o.propStr("targets").empty()) add(o.id + " ne commande aucun objet", o.id);
        if (o.type == "moving_platform" && o.prop<float>("dx", 0.0f) == 0.0f && o.prop<float>("dy", 0.0f) == 0.0f)
            add(o.id + " : plateforme mobile immobile (déplacement nul)", o.id);
    }
    for (const auto& e : level_.events) {
        if (e.source != "level" && !ids.count(e.source) && !g_.content.power(e.source))
            add("Événement « " + (e.name.empty() ? e.id : e.name) + " » : source « " + e.source + " » introuvable", {}, true);
        for (const auto& a : e.actions)
            if (!a.target.empty() && !ids.count(a.target))
                add("Événement « " + (e.name.empty() ? e.id : e.name) + " » : cible « " + a.target + " » introuvable", {}, true);
    }
    if (starts == 0) add("Aucun départ du joueur", {}, true);
    if (starts > 1) add(std::to_string(starts) + " départs du joueur (un seul est utilisé)");
    if (exits == 0) add("Aucune sortie : le niveau ne peut pas être terminé", {}, true);
    if (checkpoints == 0) add("Aucun point de passage : une erreur renvoie au départ");
    if (bosses == 0) add("Pas de boss (facultatif)");
    // Shortcut detector and difficulty: on the worker thread.
    g_.ratings.requestDraft(level_);
}

void LevelEditorScene::drawValidation(const Rect& r) {
    UIContext& ui = g_.ui;
    if (checksDirty_) runChecks();
    UIContext::ButtonStyle st;
    st.small = true;
    if (ui.button("recheck", {r.x, r.y, r.w, ui.s(40)}, "Vérifier à nouveau", st)) runChecks();
    float y = r.y + ui.s(50);
    const auto& dr = g_.ratings.draft();
    bool pending = g_.ratings.draftPending();
    std::string head = pending ? "Analyse du parcours…" : dr ? (dr->reachable ? "Sortie atteignable · difficulté " + dr->label() : "SORTIE INACCESSIBLE")
                                                             : "Analyse du parcours…";
    ui.label(head, {r.x, y + ui.s(10)}, 17, !pending && dr && !dr->reachable ? ui.theme().danger : ui.theme().accent, Align::Left, FontFace::Bold,
             VAlign::Middle);
    y += ui.s(28);
    if (dr && !pending) {
        Vec2 sz = ui.labelWrapped(dr->summary(), {r.x, y}, r.w, 14, ui.theme().textDim);
        y += sz.y + ui.s(8);
    }
    std::vector<Issue> all = issues_;
    if (dr && !pending)
        for (const auto& p : dr->problems) all.push_back({"Raccourci : " + p, {}, true});
    int errors = 0;
    for (const auto& i : all) errors += i.error ? 1 : 0;
    ui.label(all.empty() ? "Aucun problème trouvé." : std::format("{} erreur(s), {} remarque(s)", errors, all.size() - errors), {r.x, y + ui.s(10)}, 16,
             all.empty() ? ui.theme().success : (errors ? ui.theme().danger : ui.theme().warm), Align::Left, FontFace::Body, VAlign::Middle);
    y += ui.s(30);
    Rect list{r.x, y, r.w, r.bottom() - y};
    float rh = ui.s(54), gap = ui.s(4);
    ui.beginScroll("issues", list, static_cast<float>(all.size()) * (rh + gap));
    float iy = list.y - ui.scrollOffset();
    for (size_t i = 0; i < all.size(); ++i) {
        const Issue& is = all[i];
        Rect row{list.x, iy, list.w - ui.s(12), rh};
        if (iy > list.y - rh && iy < list.bottom()) {
            ui.r().fillRoundedRect(row, ui.s(6), Color{16, 24, 46, 200});
            ui.r().fillRect({row.x, row.y, ui.s(4), row.h}, is.error ? ui.theme().danger : ui.theme().warm);
            ui.r().pushClip(row);
            ui.labelWrapped(is.text, {row.x + ui.s(12), row.y + ui.s(6)}, row.w - ui.s(20), 14, ui.theme().text);
            ui.r().popClip();
            if (!is.objectId.empty() && ui.inputEnabled && ui.mouseClicked(row))
                if (const LevelObject* o = level_.findObject(is.objectId)) {
                    selection_ = {o->id};
                    cam_.center = o->rect.center();
                }
        }
        iy += rh + gap;
    }
    ui.endScroll();
}

}  // namespace cosmic
