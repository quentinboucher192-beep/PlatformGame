#include "ui/widgets/BossSheetWidgets.h"

#include <algorithm>
#include <cmath>

#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/content/CreaturePlaces.h"
#include "ui/online/RatingWidgets.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

namespace {

/// Never below 11.5 pixels on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

/// The size (from `size`, never below 11.5 pixels) at which `t` fits `room`; what still does
/// not fit is shortened with « … » by the caller.
float fit(UIContext& ui, const std::string& t, FontFace face, float size, float room, float minScale = 0.8f) {
    return std::max(ui.fitFont(t, face, fz(ui, size), room, minScale), 11.5f / ui.s(1.0f));
}

}  // namespace

void drawBossDifficulty(UIContext& ui, const Rect& r, int stars, const std::string& hint) {
    const Color gold{255, 217, 138, 255};
    ui.r().fillRoundedRect(r, ui.s(8), Color{16, 24, 48, 220});
    const float lx = r.x + ui.s(14);
    const float lf = fz(ui, 18);
    const Vec2 ls = ui.label("Difficulté", {lx, r.center().y}, lf, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
    const std::string value = std::to_string(stars) + " / 10";
    const float vf = fz(ui, 18);
    const float vw = ui.text().measure(value, FontFace::Bold, ui.s(vf)).x;
    ui.label(value, {r.right() - ui.s(14), r.center().y}, vf, gold, Align::Right, FontFace::Bold, VAlign::Middle);
    const float sx = lx + ls.x + ui.s(16);
    const float avail = r.right() - ui.s(14) - vw - ui.s(16) - sx;
    const float step = std::clamp(avail / 10.0f, ui.s(10), ui.s(26));
    for (int i = 0; i < 10; ++i)
        drawRatingStar(ui.r(), {sx + step * (static_cast<float>(i) + 0.5f), r.center().y}, step * 0.42f, i < stars ? gold : Color{90, 104, 130, 255},
                       i < stars);
    // Where it comes from, between the stars and the number, when there is room.
    const float hx = sx + step * 10.0f + ui.s(16);
    const float hroom = r.right() - ui.s(14) - vw - ui.s(16) - hx;
    if (!hint.empty() && hroom > ui.s(140)) {
        const float hf = fit(ui, hint, FontFace::Body, 14, hroom, 0.8f);
        ui.label(ui.elide(hint, FontFace::Body, hf, hroom), {hx, r.center().y}, hf, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    }
}

std::vector<AttackListItem> attackListItems(const std::vector<BossAttackEntry>& entries) {
    std::vector<AttackListItem> out;
    out.reserve(entries.size());
    for (const BossAttackEntry& e : entries) {
        AttackListItem it;
        it.id = e.attack->id;
        it.name = e.attack->name;
        it.where = e.where;
        it.summary = e.summary;
        it.description = e.description;
        it.facts = e.facts;
        it.final = e.final;
        it.engineAttack = e.attack->id;
        it.showable = e.plan.showable;
        it.unshown = e.plan.showable ? std::string() : "Exemple indisponible : " + e.plan.reason;
        out.push_back(std::move(it));
    }
    return out;
}

AttackList::AttackList(GameServices& g) : g_(g), preview_(g) {}

void AttackList::fixedUpdate() { preview_.fixedUpdate(); }

void AttackList::update(float dt) {
    dt_ = dt;
    preview_.update(dt);
    if (!drawnPaused_) drawnT_ += dt;
}

void AttackList::stop() {
    preview_.stop();
    drawnId_.clear();
}

float AttackList::factsBlock(const std::vector<AttackFact>& facts, float x, float y, float w, bool draw) {
    UIContext& ui = g_.ui;
    const float size = fz(ui, 14);
    const float lineH = ui.s(24);
    const float indent = ui.s(12);
    float h = 0.0f;
    for (size_t i = 0; i < facts.size(); ++i) {
        const AttackFact& f = facts[i];
        const float lw = ui.text().measure(f.label, FontFace::Body, ui.s(size)).x;
        const float vw = ui.text().measure(f.value, FontFace::Bold, ui.s(size)).x;
        const bool oneLine = lw + ui.s(14) + vw <= w - ui.s(12);
        float vh = 0.0f;
        if (!oneLine) vh = ui.text().measureWrapped(f.value, FontFace::Body, ui.s(size), w - indent - ui.s(6)).y;
        const float rowH = oneLine ? lineH : lineH + vh + ui.s(4);
        if (draw) {
            const Rect row{x, y + h, w, rowH - ui.s(2)};
            ui.r().fillRoundedRect(row, ui.s(4), i % 2 ? Color{12, 18, 38, 150} : Color{18, 27, 52, 190});
            const float room = w - ui.s(12);
            const float lroom = oneLine ? room - vw - ui.s(14) : room;
            const float lf = fit(ui, f.label, FontFace::Body, 14, lroom, 0.8f);
            ui.label(ui.elide(f.label, FontFace::Body, lf, lroom), {row.x + ui.s(6), row.y + lineH * 0.5f - ui.s(1)}, lf, ui.theme().textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
            if (oneLine)
                ui.label(f.value, {row.right() - ui.s(6), row.y + lineH * 0.5f - ui.s(1)}, size, ui.theme().text, Align::Right, FontFace::Bold,
                         VAlign::Middle);
            else
                ui.labelWrapped(f.value, {row.x + indent, row.y + lineH - ui.s(2)}, w - indent - ui.s(6), size, ui.theme().text);
        }
        h += rowH;
    }
    return h;
}

void AttackList::chevron(Vec2 c, float r, bool open, Color col) {
    Renderer& rr = g_.ui.r();
    if (open) rr.fillTriangle({c.x - r, c.y - r * 0.55f}, {c.x + r, c.y - r * 0.55f}, {c.x, c.y + r * 0.65f}, col);
    else rr.fillTriangle({c.x - r * 0.55f, c.y - r}, {c.x + r * 0.65f, c.y}, {c.x - r * 0.55f, c.y + r}, col);
}

float AttackList::draw(const Rect& view, float top, float y, float w, const std::string& key, const std::vector<AttackListItem>& items, const Options& o) {
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
    const UITheme& th = ui.theme();
    AttackFold& fold = folds_[key];
    std::vector<std::string> ids;
    for (const AttackListItem& it : items) ids.push_back(it.id);
    auto visible = [&](const Rect& rc) { return rc.bottom() >= view.y && rc.y <= view.bottom(); };
    if (layoutKey_ != key || layouts_.size() != items.size()) {
        layoutKey_ = key;
        layouts_.assign(items.size(), Layout{});
    }
    rows_ = static_cast<int>(items.size());
    shown_ = 0;

    if (scrollToList_) {  // (automated screenshots: the list of the attacks at the top)
        ui.setScroll(o.scroll, y - top);
        scrollToList_ = false;
    }
    // The title and the two buttons: on its line when they fit, else on the line below.
    {
        UIContext::ButtonStyle st;
        st.small = true;
        const std::string openAll = "Tout déplier", closeAll = "Tout replier";
        const float bf = fz(ui, 15);
        const float gap = ui.s(8), bh = ui.s(34);
        float b1 = ui.text().measure(openAll, FontFace::Bold, ui.s(bf)).x + ui.s(30);
        float b2 = ui.text().measure(closeAll, FontFace::Bold, ui.s(bf)).x + ui.s(30);
        const float titleW = ui.text().measure("Attaques", FontFace::Bold, ui.s(20)).x;
        float bx = view.x, by = y;
        // (The pill « NOUVEAU » sits on the top edge of the first button: room is kept above it.)
        if (titleW + ui.s(20) + b1 + gap + b2 <= w) {
            bx = view.x + w - b1 - gap - b2;
            by = y + ui.s(10);
            ui.label("Attaques", {view.x, by + bh * 0.5f}, 20, th.accent, Align::Left, FontFace::Bold, VAlign::Middle);
            y = by + bh + ui.s(10);
        } else {
            ui.label("Attaques", {view.x, y + ui.s(14)}, 20, th.accent, Align::Left, FontFace::Bold, VAlign::Middle);
            y += ui.s(44);
            if (b1 + gap + b2 > w) b1 = b2 = (w - gap) * 0.5f;
            bx = view.x;
            by = y;
            y += bh + ui.s(8);
        }
        const Rect r1{bx, by, b1, bh}, r2{bx + b1 + gap, by, b2, bh};
        st.enabled = !fold.allExpanded(ids);
        st.fontSize = fit(ui, openAll, FontFace::Bold, 15, b1 - ui.s(16), 0.75f);
        if (ui.button("atk_all", r1, openAll, st)) fold.expandAll(ids);
        if (!o.feature.empty()) ui.newBadge("atk_all", r1, o.feature, true, true);  // « NOUVEAU » (2.60)
        st.enabled = fold.anyExpanded();
        st.fontSize = fit(ui, closeAll, FontFace::Bold, 15, b2 - ui.s(16), 0.75f);
        if (ui.button("atk_none", r2, closeAll, st)) fold.collapseAll();
    }

    std::string focused;
    const float headH = ui.s(56), pad = ui.s(12);
    for (size_t i = 0; i < items.size(); ++i) {
        const AttackListItem& it = items[i];
        const bool open = fold.expanded(it.id);
        const bool engine = !it.engineAttack.empty();
        const std::string rowId = "atk" + std::to_string(i), viewId = "atkv" + std::to_string(i);
        // Unfolded: its layout measured first (the next row goes under it).
        const float iw = w - 2.0f * pad;
        const bool side = iw >= ui.s(560);
        float pw = 0.0f, ph = 0.0f, exH = 0.0f, tw = iw, descH = 0.0f, avoidH = 0.0f, factsH = 0.0f, bodyH = 0.0f;
        const float capH = ui.s(30);
        const float avoidLabelH = ui.s(24);
        // An engine example only when the sheet allows it (a campaign boss: once met); a drawn one
        // when the caller draws it.
        const bool canShow = it.showable && (engine ? o.engineShown && o.boss : static_cast<bool>(o.drawExample));
        const std::string unshown = !it.showable ? it.unshown : engine && !o.engineShown ? o.engineHidden : std::string("Exemple indisponible.");
        if (open) {
            pw = side ? std::clamp(iw * 0.56f, ui.s(260), ui.s(520)) : std::min(iw, ui.s(560));
            ph = std::round(pw * 9.0f / 16.0f);
            tw = side ? iw - pw - ui.s(16) : iw;
            // The texts measured once for this width (not at every frame).
            Layout& L = layouts_[i];
            if (L.width != w || L.canShow != canShow) {
                L.width = w;
                L.canShow = canShow;
                L.descH = ui.text().measureWrapped(it.description, FontFace::Body, ui.s(fz(ui, 15)), tw).y;
                L.avoidH = it.avoid.empty() ? 0.0f : avoidLabelH + ui.text().measureWrapped(it.avoid, FontFace::Body, ui.s(fz(ui, 15)), tw).y;
                L.factsH = factsBlock(it.facts, 0.0f, 0.0f, tw, false);
                L.reasonH = canShow ? 0.0f : ui.text().measureWrapped(unshown, FontFace::Body, ui.s(fz(ui, 15)), pw - 2.0f * pad).y;
            }
            descH = L.descH;
            avoidH = L.avoidH;
            factsH = L.factsH;
            exH = canShow ? ph + capH : L.reasonH + 2.0f * pad;
            const float textH = descH + (avoidH > 0.0f ? ui.s(10) + avoidH : 0.0f) + (factsH > 0.0f ? ui.s(10) + factsH : 0.0f);
            bodyH = side ? std::max(textH, exH) : textH + ui.s(12) + exH;
        }
        const Rect block{view.x, y, w, headH + (open ? bodyH + pad + ui.s(4) : 0.0f)};
        const Rect head{view.x, y, w, headH};
        if (scrollToItem_ == it.id) {
            ui.setScroll(o.scroll, y - top - ui.s(6));  // its top at the top of the list
            scrollToItem_.clear();
        }
        y += block.h + ui.s(6);
        // The header: a click, Entrée or A folds / unfolds it (reachable when scrolled out).
        bool hovered = false;
        if (ui.hotspot(rowId, head, &hovered)) {
            fold.toggle(it.id);
            if (fold.expanded(it.id)) scrollToItem_ = it.id;
        }
        if (!o.feature.empty()) ui.newBadge(rowId, head, o.feature, false);
        const bool focus = ui.focused(rowId);
        if (focus) focused = it.id;
        // Its example's frame (focusable too: activate it to show it, or to pause it).
        Rect frame, cap;
        const float bx = view.x + pad, byy = head.bottom() + ui.s(4);
        const float factsY = byy + descH + (avoidH > 0.0f ? ui.s(10) + avoidH : 0.0f) + (factsH > 0.0f ? ui.s(10) : 0.0f);
        if (open) {
            const float ex = side ? bx + tw + ui.s(16) : bx;
            const float ey = side ? byy : factsY + factsH + ui.s(12);
            frame = {ex, ey, pw, canShow ? ph : exH};
            cap = {ex, ey + ph, pw, capH};
        }
        const bool active = open && canShow &&
                            (engine ? preview_.bossId() == (o.boss ? o.boss->id : std::string()) && preview_.attackId() == it.engineAttack
                                    : fold.preview() == it.id);
        bool frameHover = false;
        if (open && canShow) {
            const Rect whole{frame.x, frame.y, frame.w, ph + capH};
            if (ui.hotspot(viewId, whole, &frameHover)) {
                if (active && engine) preview_.setPaused(!preview_.paused());
                else if (active) drawnPaused_ = !drawnPaused_;
                else fold.expand(it.id);
            }
            if (ui.focused(viewId)) focused = it.id;
        }
        if (!visible(block)) continue;
        ++shown_;
        // Drawn.
        r.fillRoundedRect(block, ui.s(8), open ? Color{16, 25, 50, 235} : Color{12, 18, 38, 190});
        if (focus || hovered) r.drawRoundedRect(head, ui.s(8), focus ? th.accent : th.accent.withAlpha(110), std::max(1.0f, ui.s(focus ? 2.0f : 1.2f)));
        chevron({head.x + ui.s(17), head.y + ui.s(18)}, ui.s(6), open, open ? th.accent : th.textDim);
        const float tx = head.x + ui.s(32), room = head.right() - ui.s(12) - tx;
        // Its name first, and where it comes at the right when there is room.
        const float nf = fit(ui, it.name, FontFace::Bold, 16, room, 0.75f);
        const float nameW = std::min(room, ui.text().measure(it.name, FontFace::Bold, ui.s(nf)).x);
        const float wroom = room - nameW - ui.s(16);
        if (wroom > ui.s(60) && !it.where.empty()) {
            const float wf = fit(ui, it.where, FontFace::Body, 14, wroom, 0.8f);
            ui.label(ui.elide(it.where, FontFace::Body, wf, wroom), {head.right() - ui.s(12), head.y + ui.s(18)}, wf,
                     it.final ? Color{255, 140, 150, 255} : th.textDim, Align::Right, FontFace::Body, VAlign::Middle);
        }
        ui.label(ui.elide(it.name, FontFace::Bold, nf, room), {tx, head.y + ui.s(18)}, nf, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
        const float df = fit(ui, it.summary, FontFace::Body, 14, room, 0.75f);
        ui.label(ui.elide(it.summary, FontFace::Body, df, room), {tx, head.y + ui.s(39)}, df, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        if (!open) continue;
        // What it does, how to avoid it, its characteristics.
        ui.labelWrapped(it.description, {bx, byy}, tw, fz(ui, 15), Color{200, 214, 230, 255});
        if (avoidH > 0.0f) {
            const float ay = byy + descH + ui.s(10);
            const float lf = fit(ui, "Comment l'éviter", FontFace::Bold, 15, tw, 0.8f);
            ui.label(ui.elide("Comment l'éviter", FontFace::Bold, lf, tw), {bx, ay + avoidLabelH * 0.5f - ui.s(2)}, lf, th.success, Align::Left,
                     FontFace::Bold, VAlign::Middle);
            ui.labelWrapped(it.avoid, {bx, ay + avoidLabelH}, tw, fz(ui, 15), Color{196, 240, 210, 255});
        }
        if (factsH > 0.0f) factsBlock(it.facts, bx, factsY, tw, true);
        // Its example.
        if (!canShow) {
            r.fillRoundedRect(frame, ui.s(8), Color{30, 22, 30, 230});
            r.drawRoundedRect(frame, ui.s(8), th.warm.withAlpha(120), std::max(1.0f, ui.s(1.2f)));
            ui.labelWrapped(unshown, {frame.x + pad, frame.y + pad}, frame.w - 2.0f * pad, fz(ui, 15), th.warm);
            continue;
        }
        const bool frameFocus = ui.focused(viewId);
        if (active) {
            if (frame.intersects(view)) {  // out of sight: it waits
                if (engine) preview_.draw(ui, frame, o.alpha);
                else {
                    r.fillRoundedRect(frame, ui.s(8), Color{5, 8, 18, 255});
                    r.pushClip(frame);
                    o.drawExample(frame, it, drawnT_);
                    r.popClip();
                }
            }
        } else {
            r.fillRoundedRect(frame, ui.s(8), Color{5, 8, 18, 255});
            const std::string wait = "Un seul exemple à la fois : cliquez ici, ou sélectionnez cette attaque, pour voir le sien.";
            const float room2 = frame.w - 2.0f * pad;
            const float hh = ui.text().measureWrapped(wait, FontFace::Body, ui.s(fz(ui, 14)), room2).y;
            if (hh <= frame.h - 2.0f * pad) ui.labelWrapped(wait, {frame.x + pad, frame.center().y - hh * 0.5f}, room2, fz(ui, 14), th.textDim);
        }
        // Its caption: the phase shown (or the state of the preparation), and what activating it does.
        r.fillRect(cap, Color{6, 10, 22, 230});
        const std::string left = !active ? std::string("Exemple en attente") : engine ? preview_.caption() : it.caption;
        std::string right;
        const bool running = engine ? preview_.status() == BossAttackPreview::Status::Running : true;
        const bool paused = engine ? preview_.paused() : drawnPaused_;
        if (active && running) right = paused ? "Reprendre : clic, Entrée / A" : "Pause : clic, Entrée / A";
        const float cf = fz(ui, 13);
        float rightW = right.empty() ? 0.0f : ui.text().measure(right, FontFace::Body, ui.s(cf)).x;
        const float leftW = ui.text().measure(left, FontFace::Body, ui.s(cf)).x;
        if (rightW > 0.0f && leftW + ui.s(16) + rightW > cap.w - ui.s(20)) rightW = 0.0f;  // no room: the state only
        if (rightW > 0.0f) ui.label(right, {cap.right() - ui.s(10), cap.center().y}, cf, th.accent, Align::Right, FontFace::Body, VAlign::Middle);
        const float lroom = cap.w - ui.s(20) - (rightW > 0.0f ? rightW + ui.s(16) : 0.0f);
        const float lf = fit(ui, left, FontFace::Body, 13, lroom, 0.85f);
        ui.label(ui.elide(left, FontFace::Body, lf, lroom), {cap.x + ui.s(10), cap.center().y}, lf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        const Rect whole{frame.x, frame.y, frame.w, ph + capH};
        r.drawRoundedRect(whole, ui.s(8), frameFocus || frameHover ? th.accent : th.accent.withAlpha(active ? 90 : 40),
                          std::max(1.0f, ui.s(frameFocus ? 2.0f : 1.2f)));
    }
    // One example at a time: the unfolded row with the focus (after a moment), else the last unfolded.
    fold.update(focused, dt_);
    const std::string& shown = fold.preview();
    const AttackListItem* item = nullptr;
    for (const AttackListItem& it : items)
        if (it.id == shown) item = &it;
    if (item && !item->engineAttack.empty() && o.boss && o.engineShown) preview_.request(o.boss, item->engineAttack, atlas_);
    else preview_.request(nullptr, std::string{}, atlas_);
    // A drawn example starts again from its beginning when another one is shown.
    const std::string drawn = item && item->engineAttack.empty() ? item->id : std::string();
    if (drawn != drawnId_) {
        drawnId_ = drawn;
        drawnT_ = 0.0f;
        drawnPaused_ = false;
    }
    return y;
}

}  // namespace cosmic
