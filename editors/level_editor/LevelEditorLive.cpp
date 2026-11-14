// Live editing of a collaborative project in the level editor (Cosmic Online, phases 3-4):
// my edits become operations (the difference between the document the room knows and
// mine), the other members' operations are applied to the level and to every state of the
// undo history (undo only takes back my own edits), objects selected by another member are
// locked, presence (avatars, cursors, selections, activity) is drawn, a member's view can
// be followed, and a refused edit is shown with the choice to apply it again: nothing is
// overwritten silently.

#include <algorithm>
#include <cmath>
#include <format>

#include "editors/level_editor/LevelEditorScene.h"
#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "packaging/Sha256.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

std::string LevelEditorScene::newObjectId() const {
    std::string id = level_.nextObjectId();
    if (!live_) return id;
    std::string me = g_.online.playerId();
    std::string tag = me.size() >= 4 ? me.substr(0, 4) : std::string("live");
    std::string out = id + "_" + tag;
    for (int n = 2; level_.findObject(out); ++n) out = id + "_" + tag + std::to_string(n);
    return out;
}

std::string LevelEditorScene::lockHolder(const std::string& id) const {
    if (!live_) return {};
    auto it = live_->locks().find(id);
    if (it == live_->locks().end()) return {};
    const LiveSession::Participant* p = live_->participant(it->second);
    return p ? p->name : std::string("un autre membre");
}

void LevelEditorScene::updateLive(float dt) {
    // What I show the others.
    Vec2 mouse = g_.app.input().mousePos();
    if (viewport_.contains(mouse)) live_->setCursor(cam_.view(viewport_).toWorld(mouse));
    else live_->setCursor(std::nullopt);
    live_->setView(cam_.center, cam_.zoom);
    for (auto it = selection_.begin(); it != selection_.end();)
        it = lockHolder(*it).empty() ? std::next(it) : selection_.erase(it);
    live_->setSelection(readOnly_ ? std::set<std::string>{} : selection_);
    std::string activity;
    if (const LevelObject* p = primary(); p && !readOnly_) {
        const ObjectTypeInfo* t = ObjectTypes::find(p->type);
        activity = (drag_ == Drag::Move ? "déplace " : "modifie ") + (t ? t->name : p->type);
    } else if (!readOnly_ && !lastEditLabel_.empty()) {
        activity = lastEditLabel_;
    }
    live_->setActivity(activity);
    live_->update(dt);

    if (auto doc = live_->takeJoinedDocument()) {
        if (auto lvl = LevelData::fromJson(*doc)) {
            lvl->id = requestedId_.empty() ? level_.id : requestedId_;
            level_ = *lvl;
            level_.ensureDefaultLayers();
            selection_.clear();
            history_.reset(level_.toJson(), "Édition en direct");
            liveSynced_ = level_.toJson();
            liveLoaded_ = true;
            liveDirty_ = false;
            dirty_ = false;
            previewDirty_ = true;
            checksDirty_ = true;
        } else {
            flash("Document du projet illisible.", true);
        }
    }
    if (!liveLoaded_) return;
    // Automated checks: "live-edit:<id>" moves that object once the room is joined (even if
    // another member holds it: the server must refuse it).
    if (g_.onlineOpen.rfind("live-edit:", 0) == 0 && time_ > 6.0f) {
        if (LevelObject* o = level_.findObject(g_.onlineOpen.substr(10))) {
            o->rect.x += 32;
            markChanged("Déplacer");
        }
        g_.onlineOpen.clear();
    }
    // My edits, as operations (not in the middle of a drag: at its end).
    if (liveDirty_ && drag_ == Drag::None) {
        liveDirty_ = false;
        Json now = level_.toJson();
        if (!readOnly_) {
            auto ops = LiveOps::diff(liveSynced_, now);
            if (!ops.empty()) live_->submit(ops);
        }
        liveSynced_ = std::move(now);
    }
    // The others' edits (after a drag, never under the mouse).
    if (drag_ == Drag::None) {
        auto ops = live_->takeRemoteOps();
        if (!ops.empty()) applyRemote(ops);
        for (auto& r : live_->takeRejections()) {
            restoreFromServer(r);
            conflicts_.push_back(std::move(r));
        }
    }
    // Following a member's view.
    if (!followId_.empty()) {
        const LiveSession::Participant* p = live_->participant(followId_);
        if (!p || !p->viewCenter) {
            followId_.clear();
        } else {
            float k = std::min(1.0f, dt * 8.0f);
            cam_.center = cam_.center + (*p->viewCenter - cam_.center) * k;
            cam_.zoom += (p->viewZoom - cam_.zoom) * k;
        }
    }
}

void LevelEditorScene::applyRemote(const std::vector<Json>& ops) {
    Json doc = level_.toJson();
    for (const auto& op : ops) {
        LiveOps::apply(doc, op);
        LiveOps::apply(liveSynced_, op);
    }
    history_.transformAll([&](Json& state) {
        for (const auto& op : ops) LiveOps::apply(state, op);
    });
    if (auto lvl = LevelData::fromJson(doc)) {
        std::string id = level_.id;
        level_ = *lvl;
        level_.id = id;
        for (auto it = selection_.begin(); it != selection_.end();)
            it = level_.findObject(*it) ? std::next(it) : selection_.erase(it);
        previewDirty_ = true;
        previewDelay_ = 0.1f;
        checksDirty_ = true;
    }
}

void LevelEditorScene::restoreFromServer(const LiveSession::Rejection& r) {
    // My refused edit is taken back: the level shows what the room has.
    std::string key = LiveOps::touched(r.op);
    Json fix;
    if (key.rfind("d:", 0) == 0) {
        fix = {{"t", "doc"}, {"k", key.substr(2)}, {"v", r.current}};
    } else {
        std::string id = key.substr(2);
        Json doc = level_.toJson();
        bool exists = false;
        for (const auto& o : doc["objects"]) exists |= jstr(o, "id") == id;
        if (r.current.is_object()) {
            if (exists) {
                Json f = r.current;
                f.erase("id");
                fix = {{"t", "set"}, {"id", id}, {"f", f}};
            } else {
                fix = {{"t", "add"}, {"o", r.current}};
            }
        } else if (exists) {
            fix = {{"t", "del"}, {"id", id}};
        }
    }
    if (fix.is_null()) return;
    Json doc = level_.toJson();
    LiveOps::apply(doc, fix);
    LiveOps::apply(liveSynced_, fix);
    if (auto lvl = LevelData::fromJson(doc)) {
        std::string id = level_.id;
        level_ = *lvl;
        level_.id = id;
        previewDirty_ = true;
    }
    history_.commit(level_.toJson(), "Version du projet gardée");
}

// ------------------------------------------------------------------ drawing

void LevelEditorScene::drawPresence(const Rect& area) {
    UIContext& ui = g_.ui;
    if (!live_ || !liveUi_) return;
    ui.pushId("presence");
    // Sync state first (right), the avatars on its left.
    std::string state;
    Color sc = ui.theme().success;
    switch (live_->state()) {
        case LiveSession::State::Joining: state = "Connexion…"; sc = ui.theme().textDim; break;
        case LiveSession::State::Offline: state = "Hors ligne · " + std::to_string(live_->pending()) + " en attente"; sc = ui.theme().warm; break;
        case LiveSession::State::Failed: state = "Accès perdu"; sc = ui.theme().danger; break;
        default:
            state = live_->pending() > 0 ? "Envoi · " + std::to_string(live_->pending()) : "Synchronisé";
            if (!conflicts_.empty()) {
                state = "Conflit à voir";
                sc = ui.theme().danger;
            }
    }
    float sw = std::min(area.w * 0.5f, ui.text().measure(state, FontFace::Bold, ui.s(15)).x + ui.s(16));
    ui.label(state, {area.right() - sw * 0.5f, area.center().y}, ui.fitFont(state, FontFace::Bold, 15, sw), sc, Align::Center, FontFace::Bold, VAlign::Middle);
    float x = area.right() - sw - ui.s(8);
    const float av = std::min(area.h - ui.s(8), ui.s(38));
    const auto& people = live_->participants();
    for (auto it = people.rbegin(); it != people.rend(); ++it) {
        if (x - av < area.x) break;
        x -= av;
        Rect r{x, area.center().y - av * 0.5f, av, av};
        liveUi_->drawAvatar(it->avatar, it->id, r);
        ui.r().drawRing(r.center(), av * 0.5f + ui.s(1), ui.s(2.5f), it->color);
        if (followId_ == it->id) ui.r().drawRing(r.center(), av * 0.5f + ui.s(5), ui.s(1.5f), Color::white());
        // Click: follow this member's view (again: stop).
        if (!it->me && ui.mouseClicked(r)) followId_ = followId_ == it->id ? std::string{} : it->id;
        if (ui.mouseOver(r)) {
            std::string tip = it->name + (it->me ? " (vous)" : followId_ == it->id ? " · suivi (clic : arrêter)" : " · clic : suivre sa vue");
            float tw = ui.text().measure(tip, FontFace::Body, ui.s(14)).x + ui.s(16);
            Rect tr{std::min(r.x, ui.screenW() - tw - ui.s(8)), r.bottom() + ui.s(6), tw, ui.s(26)};
            ui.r().fillRoundedRect(tr, ui.s(6), Color{6, 10, 24, 240});
            ui.label(tip, tr.center(), 14, ui.theme().text, Align::Center, FontFace::Body, VAlign::Middle);
        }
        x -= ui.s(6);
    }
    ui.popId();
}

void LevelEditorScene::drawLiveOverlay(const Rect& vp) {
    if (!live_) return;
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
    WorldView view = cam_.view(vp);
    r.setView(view);
    TextStyle st;
    st.size = ui.s(14);
    st.shadow = true;
    for (const auto& p : live_->participants()) {
        if (p.me) continue;
        // Their selection (locked for me).
        for (const auto& id : p.selection)
            if (const LevelObject* o = level_.findObject(id)) {
                r.drawWorldRect(o->rect.expanded(2), p.color, 2.0f);
                Vec2 lp = view.toScreen(o->rect.pos());
                st.color = p.color;
                ui.text().draw(p.name, {lp.x, lp.y - ui.s(20)}, st);
            }
        // Their cursor.
        if (p.cursor) {
            Vec2 c = view.toScreen(*p.cursor);
            if (!vp.contains(c)) continue;
            float s = ui.s(14);
            r.fillTriangle(c, c + Vec2{0, s * 1.3f}, c + Vec2{s * 0.9f, s * 0.9f}, p.color);
            std::string tag = p.name + (p.activity.empty() ? "" : " · " + p.activity);
            float tw = ui.text().measure(tag, FontFace::Body, ui.s(13)).x + ui.s(12);
            Rect tr{c.x + s, c.y + s, tw, ui.s(22)};
            r.fillRoundedRect(tr, ui.s(6), p.color.withAlphaF(0.85f));
            ui.label(tag, tr.center(), 13, Color{8, 10, 20, 255}, Align::Center, FontFace::Body, VAlign::Middle);
        }
    }
    // Activity of the others, one line each (the latest), bottom-left of the view.
    float y = vp.bottom() - ui.s(16);
    for (const auto& p : live_->participants()) {
        if (p.me || p.activity.empty()) continue;
        std::string line = p.name + " " + p.activity;
        float lw = std::min(vp.w * 0.5f, ui.text().measure(line, FontFace::Body, ui.s(14)).x + ui.s(20));
        Rect lr{vp.x + ui.s(12), y - ui.s(26), lw, ui.s(24)};
        r.fillRoundedRect(lr, ui.s(6), Color{6, 10, 24, 210});
        r.fillRect({lr.x, lr.y + ui.s(4), ui.s(3), lr.h - ui.s(8)}, p.color);
        ui.label(line, {lr.x + ui.s(10), lr.center().y}, ui.fitFont(line, FontFace::Body, 14, lw - ui.s(16)), ui.theme().text, Align::Left, FontFace::Body,
                 VAlign::Middle);
        y -= ui.s(30);
    }
}

void LevelEditorScene::drawConflicts() {
    if (conflicts_.empty()) return;
    UIContext& ui = g_.ui;
    const LiveSession::Rejection& c = conflicts_.front();
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 160});
    Rect box{ui.screenW() * 0.5f - ui.s(360), ui.screenH() * 0.5f - ui.s(150), ui.s(720), ui.s(300)};
    ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
    ui.r().drawRoundedRect(box, ui.s(18), ui.theme().warm.withAlphaF(0.7f), ui.s(1.5f));
    ui.pushId("conflict");
    std::string what = LiveOps::touched(c.op);
    std::string target = "les réglages « " + what.substr(2) + " » du niveau";
    if (what.rfind("o:", 0) == 0) {
        const LevelObject* o = level_.findObject(what.substr(2));
        const ObjectTypeInfo* t = o ? ObjectTypes::find(o->type) : nullptr;
        target = t ? "« " + t->name + " » (" + what.substr(2) + ")" : "l'objet " + what.substr(2);
    }
    std::string who = c.by.empty() ? "un autre membre" : c.by;
    std::string text = c.status == "conflict"  ? who + " a modifié " + target + " pendant que vous le modifiiez. Sa version est gardée."
                     : c.status == "locked"    ? who + " travaille sur " + target + " : votre modification n'a pas été appliquée."
                     : c.status == "not_found" ? target + " a été supprimé par un autre membre : votre modification n'a pas été appliquée."
                     : c.status == "id_taken"  ? "Un autre membre a créé au même moment un objet du même nom : le sien est gardé."
                     : c.status == "forbidden" ? "Votre rôle ne permet plus de modifier ce projet."
                                               : "Le serveur a refusé une modification (" + c.status + ").";
    ui.label("Modification non appliquée", {box.x + ui.s(30), box.y + ui.s(40)}, 24, ui.theme().warm, Align::Left, FontFace::Bold, VAlign::Middle);
    ui.labelWrapped(text + " Rien n'a été écrasé sans vous.", {box.x + ui.s(30), box.y + ui.s(76)}, box.w - ui.s(60), 17, ui.theme().text);
    if (conflicts_.size() > 1)
        ui.label(std::to_string(conflicts_.size() - 1) + " autre(s) à voir ensuite", {box.x + ui.s(30), box.bottom() - ui.s(96)}, 14, ui.theme().textDim);
    UIContext::ButtonStyle p;
    p.primary = true;
    bool canRetry = (c.status == "conflict" || c.status == "locked") && !readOnly_;
    p.enabled = canRetry;
    if (ui.button("again", {box.x + ui.s(30), box.bottom() - ui.s(70), ui.s(320), ui.s(46)}, "Réappliquer ma modification", p)) {
        // Applied again on the current state (I chose it, knowing the other version).
        Json doc = level_.toJson();
        LiveOps::apply(doc, c.op);
        if (auto lvl = LevelData::fromJson(doc)) {
            std::string id = level_.id;
            level_ = *lvl;
            level_.id = id;
            markChanged("Réappliquer ma modification");
        }
        conflicts_.erase(conflicts_.begin());
    } else if (ui.button("keep", {box.right() - ui.s(330), box.bottom() - ui.s(70), ui.s(300), ui.s(46)}, "Garder la version du projet") ||
               ui.backPressed()) {
        conflicts_.erase(conflicts_.begin());
    }
    ui.popId();
}

}  // namespace cosmic
