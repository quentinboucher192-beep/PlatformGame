// Level editor viewport: camera, tools (select, move, resize, box select, place,
// rubber-band creation), keyboard shortcuts and overlays.

#include <cmath>

#include "editors/level_editor/LevelEditorScene.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/levels/DecorAtlas.h"

namespace cosmic {

namespace {
// Handle order: 0 TL, 1 T, 2 TR, 3 R, 4 BR, 5 B, 6 BL, 7 L
Vec2 handlePos(const Rect& r, int i) {
    switch (i) {
        case 0: return {r.x, r.y};
        case 1: return {r.center().x, r.y};
        case 2: return {r.right(), r.y};
        case 3: return {r.right(), r.center().y};
        case 4: return {r.right(), r.bottom()};
        case 5: return {r.center().x, r.bottom()};
        case 6: return {r.x, r.bottom()};
        default: return {r.x, r.center().y};
    }
}
bool rubberBand(const std::string& type) {
    return type == "solid" || type == "oneway" || type == "liquid" || type == "trigger" || type == "combat_zone" ||
           type == "camera_zone" || type == "secret_area" || type == "damage_zone" || type == "slow_zone" ||
           type == "ladder" || type == "spikes" || type == "emitter" || type == "power_gate";
}
}  // namespace

void LevelEditorScene::handleViewport(const Rect& vp) {
    UIContext& ui = g_.ui;
    InputManager& in = g_.app.input();
    WorldView view = cam_.view(vp);
    Vec2 mouse = in.mousePos();
    Vec2 world = view.toWorld(mouse);
    // The minimap and the read-only banner sit over the viewport: clicks there are theirs.
    bool inside = vp.contains(mouse) && !(minimapOn_ && minimapRect_.contains(mouse)) && !(readOnly_ && bannerRect_.contains(mouse));
    float grid = gridOn_ ? (gridIndex_ == 0 ? 8.0f : gridIndex_ == 1 ? 16.0f : 32.0f) : 1.0f;

    // ---- camera: wheel zoom, middle / right drag, keyboard & gamepad pan
    if (inside && in.wheel() != 0.0f && !ui.editingText()) cam_.zoomAt(vp, mouse, in.wheel() > 0 ? 1.15f : 1.0f / 1.15f);
    bool panButton = in.mouseDown(SDL_BUTTON_MIDDLE) || in.mouseDown(SDL_BUTTON_RIGHT) ||
                     (tool_ == Tool::Pan && in.mouseDown(SDL_BUTTON_LEFT)) || (in.keyDown(SDL_SCANCODE_SPACE) && in.mouseDown(SDL_BUTTON_LEFT));
    if (panButton && (inside || drag_ == Drag::Pan)) {
        if (drag_ == Drag::None) drag_ = Drag::Pan;
        if (drag_ == Drag::Pan) cam_.center -= in.mouseDelta() / view.scale;
    } else if (drag_ == Drag::Pan) {
        drag_ = Drag::None;
    }
    if (!ui.editingText() && !in.ctrl()) {
        float speed = 600.0f / cam_.zoom * (g_.app.frameTime() / 1000.0f) * (in.shift() ? 2.5f : 1.0f);
        if (in.keyDown(SDL_SCANCODE_LEFT) || in.keyDown(SDL_SCANCODE_A)) cam_.center.x -= speed;
        if (in.keyDown(SDL_SCANCODE_RIGHT) || in.keyDown(SDL_SCANCODE_D)) cam_.center.x += speed;
        if (in.keyDown(SDL_SCANCODE_UP) || in.keyDown(SDL_SCANCODE_W)) cam_.center.y -= speed;
        if (in.keyDown(SDL_SCANCODE_DOWN) || in.keyDown(SDL_SCANCODE_S)) cam_.center.y += speed;
        cam_.center += in.leftStick() * speed;
    }

    hovered_ = inside && drag_ == Drag::None ? objectAt(world) : hovered_;
    if (!inside && drag_ == Drag::None) hovered_.clear();

    // ---- object picking for ObjectRef properties
    if (!pickTarget_.empty()) {
        if (in.keyPressed(SDL_SCANCODE_ESCAPE)) pickTarget_.clear();
        if (inside && in.mousePressed(SDL_BUTTON_LEFT)) {
            std::string hit = objectAt(world);
            LevelObject* p = primary();
            if (!hit.empty() && p) {
                std::string cur = p->propStr(pickTarget_.c_str());
                p->props[pickTarget_] = cur.empty() ? hit : cur + "," + hit;
                markChanged("Lier " + hit);
                flash("Objet " + hit + " ajouté");
            }
            pickTarget_.clear();
        }
        return;
    }

    // ---- left button
    if (inside && in.mousePressed(SDL_BUTTON_LEFT) && drag_ == Drag::None && !in.keyDown(SDL_SCANCODE_SPACE)) {
        dragStartWorld_ = world;
        dragStartScreen_ = mouse;
        dragAccum_ = {};
        // Resize handle of the single selection?
        if (LevelObject* p = primary(); p && tool_ == Tool::Select && !readOnly_) {
            const ObjectTypeInfo* t = ObjectTypes::find(p->type);
            if (t && t->resizable && layerEditable(p->layer)) {
                for (int i = 0; i < 8; ++i) {
                    Vec2 hp = view.toScreen(handlePos(p->rect, i));
                    if (distance(hp, mouse) < ui.s(9)) {
                        drag_ = Drag::Resize;
                        resizeHandle_ = i;
                        resizeStart_ = p->rect;
                        break;
                    }
                }
            }
        }
        if (drag_ == Drag::None && tool_ == Tool::Place && readOnly_) {
            flash("Lecture seule : « Dupliquer pour modifier » d'abord", true);
            tool_ = Tool::Select;
        } else if (drag_ == Drag::None && tool_ == Tool::Place) {
            const auto& pal = ObjectTypes::palette();
            PaletteEntry entry;
            bool ok = false;
            if (!placeEnemy_.empty()) {
                entry = {"", "enemy", {{"enemy", placeEnemy_}}, {}};
                ok = true;
            } else if (!placeDecor_.empty()) {
                entry = {"", "decor", {{"sprite", placeDecor_}}, {}};
                ok = true;
            } else if (paletteIndex_ >= 0 && paletteIndex_ < static_cast<int>(pal.size())) {
                entry = pal[paletteIndex_];
                ok = true;
            }
            if (ok) {
                if (rubberBand(entry.type)) {
                    drag_ = Drag::Create;
                } else {
                    const ObjectTypeInfo* t = ObjectTypes::find(entry.type);
                    Vec2 size = entry.size.lengthSq() > 0 ? entry.size : (t ? t->defaultSize : Vec2{16, 16});
                    if (entry.type == "enemy") {
                        if (const EnemyDefinition* d = g_.content.enemy(placeEnemy_)) size = d->size;
                    }
                    if (entry.type == "decor") {
                        if (const DecorSprite* s = DecorAtlas::shared(g_.app.resources()).find(placeDecor_))
                            size = {s->rect.w, s->rect.h};
                    }
                    float sg = gridOn_ ? std::min(grid, t ? std::max(1.0f, t->snap) : grid) : 1.0f;
                    Vec2 p = snapPoint(world - size * 0.5f, sg);
                    // Characters / pickups: place feet on the grid line under the cursor.
                    std::string id = createObject(entry, {p.x, p.y, size.x, size.y});
                    selection_ = {id};
                }
            }
        } else if (drag_ == Drag::None) {
            std::string hit = objectAt(world);
            if (hit.empty()) {
                if (!in.shift()) selection_.clear();
                drag_ = Drag::BoxSelect;
            } else {
                if (in.shift()) {
                    if (selection_.count(hit)) selection_.erase(hit);
                    else selection_.insert(hit);
                } else if (!selection_.count(hit)) {
                    selection_ = {hit};
                }
                if (!in.alt()) expandToGroups();  // Alt+clic : un seul objet d'un groupe
                if (selection_.count(hit) && !readOnly_) drag_ = Drag::Move;
            }
        }
    }

    // ---- dragging
    if (drag_ == Drag::Move && in.mouseDown(SDL_BUTTON_LEFT)) {
        Vec2 total = world - dragStartWorld_;
        LevelObject* ref = level_.findObject(*selection_.begin());
        float sg = ref ? snapFor(*ref) : grid;
        Vec2 snapped = sg > 1 ? Vec2{std::round(total.x / sg) * sg, std::round(total.y / sg) * sg} : total;
        Vec2 step = snapped - dragAccum_;
        if (step.lengthSq() > 0) {
            moveSelection(step);
            dragAccum_ = snapped;
        }
    } else if (drag_ == Drag::Resize && in.mouseDown(SDL_BUTTON_LEFT)) {
        if (LevelObject* p = primary()) {
            float sg = snapFor(*p);
            Vec2 w = snapPoint(world, sg);
            Rect r = resizeStart_;
            float minS = std::max(4.0f, sg);
            int h = resizeHandle_;
            if (h == 0 || h == 6 || h == 7) { float right = r.right(); r.x = std::min(w.x, right - minS); r.w = right - r.x; }
            if (h == 2 || h == 3 || h == 4) r.w = std::max(minS, w.x - r.x);
            if (h == 0 || h == 1 || h == 2) { float bottom = r.bottom(); r.y = std::min(w.y, bottom - minS); r.h = bottom - r.y; }
            if (h == 4 || h == 5 || h == 6) r.h = std::max(minS, w.y - r.y);
            p->rect = r;
        }
    }
    if (in.mouseReleased(SDL_BUTTON_LEFT)) {
        if (drag_ == Drag::Move && dragAccum_.lengthSq() > 0) markChanged("Déplacer");
        if (drag_ == Drag::Resize) markChanged("Redimensionner");
        if (drag_ == Drag::BoxSelect) {
            Rect box{std::min(dragStartWorld_.x, world.x), std::min(dragStartWorld_.y, world.y),
                     std::abs(world.x - dragStartWorld_.x), std::abs(world.y - dragStartWorld_.y)};
            if (box.w > 2 && box.h > 2)
                for (const auto& o : level_.objects)
                    if (layerEditable(o.layer) && box.intersects(o.rect)) selection_.insert(o.id);
        }
        if (drag_ == Drag::Create) {
            const auto& pal = ObjectTypes::palette();
            if (paletteIndex_ >= 0 && paletteIndex_ < static_cast<int>(pal.size())) {
                const PaletteEntry& entry = pal[paletteIndex_];
                const ObjectTypeInfo* t = ObjectTypes::find(entry.type);
                float sg = (entry.type == "solid" || entry.type == "oneway" || entry.type == "liquid") ? std::max(grid, 16.0f) : grid;
                Vec2 a = snapPoint(dragStartWorld_, sg), b = snapPoint(world, sg);
                Rect r{std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
                Vec2 def = entry.size.lengthSq() > 0 ? entry.size : (t ? t->defaultSize : Vec2{16, 16});
                if (r.w < sg) r.w = def.x;
                if (r.h < sg) r.h = def.y;
                if (entry.type == "oneway") r.h = 16;
                std::string id = createObject(entry, r);
                selection_ = {id};
            }
        }
        drag_ = Drag::None;
    }
}

void LevelEditorScene::handleShortcuts() {
    UIContext& ui = g_.ui;
    InputManager& in = g_.app.input();
    if (ui.editingText()) return;
    if (in.ctrl()) {
        if (in.shortcutPressed(SDLK_Z)) {
            if (in.shift()) applyJson(history_.redo());
            else applyJson(history_.undo());
        }
        if (in.shortcutPressed(SDLK_Y)) applyJson(history_.redo());
        if (in.shortcutPressed(SDLK_S)) save();
        if (in.shortcutPressed(SDLK_C)) copySelection();
        if (in.shortcutPressed(SDLK_V)) paste(cam_.view(viewport_).toWorld(in.mousePos()));
        if (in.shortcutPressed(SDLK_D)) duplicateSelection();
        if (in.shortcutPressed(SDLK_G)) groupSelection(!in.shift());
        if (in.shortcutPressed(SDLK_A)) {
            selection_.clear();
            for (const auto& o : level_.objects)
                if (layerEditable(o.layer)) selection_.insert(o.id);
        }
        return;
    }
    if (in.keyPressed(SDL_SCANCODE_DELETE) || in.keyPressed(SDL_SCANCODE_BACKSPACE)) deleteSelection();
    if (in.keyPressed(SDL_SCANCODE_ESCAPE)) {
        if (tool_ == Tool::Place) tool_ = Tool::Select;
        else selection_.clear();
    }
    if (in.shortcutPressed(SDLK_R)) rotateSelection(in.shift() ? -15.0f : 15.0f);
    if (in.shortcutPressed(SDLK_F)) {
        for (auto& o : level_.objects)
            if (selection_.count(o.id)) o.flipX = !o.flipX;
        if (!selection_.empty()) markChanged("Retourner");
    }
    if (in.shortcutPressed(SDLK_G)) gridOn_ = !gridOn_;
    if (in.shortcutPressed(SDLK_M)) minimapOn_ = !minimapOn_;
    if (in.keyPressed(SDL_SCANCODE_F5)) testPlay(in.shift());
    if (in.keyPressed(SDL_SCANCODE_F1)) modal_ = Modal::Help;
    if (in.shortcutPressed(SDLK_V)) tool_ = Tool::Select;
    if (in.shortcutPressed(SDLK_H)) tool_ = Tool::Pan;
    if (in.keyPressed(SDL_SCANCODE_HOME)) cam_.zoom = 1.0f;
    // Nudge with Alt + arrows
    if (in.alt()) {
        Vec2 d;
        if (in.keyPressed(SDL_SCANCODE_LEFT)) d.x = -1;
        if (in.keyPressed(SDL_SCANCODE_RIGHT)) d.x = 1;
        if (in.keyPressed(SDL_SCANCODE_UP)) d.y = -1;
        if (in.keyPressed(SDL_SCANCODE_DOWN)) d.y = 1;
        if (d.lengthSq() > 0 && !selection_.empty()) {
            moveSelection(d * (gridOn_ ? 16.0f : 1.0f));
            markChanged("Déplacer");
        }
    }
}

void LevelEditorScene::drawViewport(const Rect& vp, float alpha) {
    Renderer& r = g_.app.renderer();
    r.pushClip(vp);
    if (world_) {
        WorldView view = cam_.view(vp);
        RenderOptions opt;
        opt.editor = true;
        opt.lighting = g_.settings.graphics.dynamicLighting;
        renderer_.render(r, g_.app.resources(), *world_, view, alpha, opt);
    }
    drawOverlays(vp);
    r.setView(WorldView{});
    drawMinimap(vp);
    drawReadOnlyBanner(vp);
    r.popClip();
}

void LevelEditorScene::drawOverlays(const Rect& vp) {
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
    WorldView view = cam_.view(vp);
    r.setView(view);
    InputManager& in = g_.app.input();
    Vec2 world = view.toWorld(in.mousePos());

    // Grid
    float grid = gridIndex_ == 0 ? 8.0f : gridIndex_ == 1 ? 16.0f : 32.0f;
    if (gridOn_ && grid * view.scale >= 6.0f) {
        Rect vis = view.visibleWorld();
        Color gc{255, 255, 255, 30};
        for (float x = std::floor(vis.x / grid) * grid; x < vis.right(); x += grid) {
            Color c = std::fmod(std::abs(x), grid * 8) < 0.5f ? Color{255, 255, 255, 40} : gc;
            r.drawWorldLine({x, vis.y}, {x, vis.bottom()}, c, 1.0f);
        }
        for (float y = std::floor(vis.y / grid) * grid; y < vis.bottom(); y += grid) {
            Color c = std::fmod(std::abs(y), grid * 8) < 0.5f ? Color{255, 255, 255, 40} : gc;
            r.drawWorldLine({vis.x, y}, {vis.right(), y}, c, 1.0f);
        }
    }
    // Level bounds & kill line
    r.drawWorldRect(level_.bounds, {57, 240, 216, 160}, 2.0f);
    float ky = level_.effectiveKillY();
    r.drawWorldLine({level_.bounds.x, ky}, {level_.bounds.right(), ky}, {255, 77, 109, 120}, 1.0f);

    // Objects without visuals in the preview
    for (const auto& o : level_.objects) {
        const LevelLayer* l = level_.findLayer(o.layer);
        if (l && !l->visible) continue;
        if (o.type == "player_start") {
            auto anim = g_.app.resources().animation("animations/nova.json");
            if (anim && anim->texture) {
                if (const AnimationClip* c = anim->clip("idle")) {
                    Rect src = anim->frameRect(*c, static_cast<int>(time_ * 8) % c->frameCount());
                    Vec2 f = o.rect.bottomCenter();
                    bool fl = o.prop<bool>("faceLeft", false);
                    Rect dst{f.x - (fl ? anim->frameWidth - anim->pivot.x : anim->pivot.x), f.y - anim->pivot.y,
                             static_cast<float>(anim->frameWidth), static_cast<float>(anim->frameHeight)};
                    r.drawWorldTexture(*anim->texture, &src, dst, Color::white(), BlendMode::Alpha, fl);
                }
            }
            r.drawWorldRect(o.rect.expanded(3), {57, 240, 216, 200}, 1.5f);
        } else if (o.type == "solid" && !o.prop<bool>("collision", true)) {
            r.drawWorldRect(o.rect, {255, 255, 255, 60}, 1.0f);
        }
    }

    // Hover / selection
    if (!hovered_.empty() && !selection_.count(hovered_))
        if (const LevelObject* o = level_.findObject(hovered_)) r.drawWorldRect(o->rect.expanded(1), {255, 255, 255, 120}, 1.0f);
    for (const auto& id : selection_) {
        const LevelObject* o = level_.findObject(id);
        if (!o) continue;
        float pulse = 0.7f + 0.3f * std::sin(time_ * 6.0f);
        r.drawWorldRect(o->rect.expanded(1), Color{57, 240, 216, 255}.withAlphaF(pulse), 2.0f);
        if (const LevelObject* p = selection_.size() == 1 ? o : nullptr) {
            const ObjectTypeInfo* t = ObjectTypes::find(p->type);
            if (t && t->resizable)
                for (int i = 0; i < 8; ++i) {
                    Vec2 hp = view.toScreen(handlePos(p->rect, i));
                    r.fillRect(Rect::fromCenter(hp, {ui.s(10), ui.s(10)}), {8, 12, 24, 255});
                    r.drawRect(Rect::fromCenter(hp, {ui.s(10), ui.s(10)}), {57, 240, 216, 255}, 2.0f);
                }
            TextStyle st;
            st.size = ui.s(16);
            st.color = {57, 240, 216, 255};
            st.shadow = true;
            Vec2 lp = view.toScreen(p->rect.pos());
            ui.text().draw(p->id + " · " + (t ? t->name : p->type), {lp.x, lp.y - ui.s(22)}, st);
        }
    }
    // Box select / creation rectangles
    if (drag_ == Drag::BoxSelect || drag_ == Drag::Create) {
        Rect box{std::min(dragStartWorld_.x, world.x), std::min(dragStartWorld_.y, world.y), std::abs(world.x - dragStartWorld_.x),
                 std::abs(world.y - dragStartWorld_.y)};
        if (drag_ == Drag::Create) {
            float sg = std::max(grid, 16.0f);
            Vec2 a = snapPoint(dragStartWorld_, sg), b = snapPoint(world, sg);
            box = {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
        }
        r.fillWorldRect(box, {57, 240, 216, 35});
        r.drawWorldRect(box, {57, 240, 216, 200}, 1.5f);
    }
    // Placement ghost
    if (tool_ == Tool::Place && viewport_.contains(in.mousePos()) && drag_ == Drag::None) {
        Vec2 p = snapPoint(world, gridOn_ ? grid : 1.0f);
        r.drawWorldRect(Rect::fromCenter(p, {16, 16}), {255, 217, 138, 200}, 1.5f);
    }
    if (!pickTarget_.empty()) {
        if (!hovered_.empty())
            if (const LevelObject* o = level_.findObject(hovered_)) r.drawWorldRect(o->rect.expanded(3), {255, 217, 138, 255}, 2.0f);
    }
}

}  // namespace cosmic
