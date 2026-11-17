// 2.61 Studio de Créations (see Studio.h): the rail of tools, the canvas and its input (mouse,
// keyboard, gamepad), the palette, the HSV picker and the emitter of a particle.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "engine/core/I18n.h"
#include "engine/input/InputManager.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "ui/screens/ChallengeUi.h"
#include "ui/screens/CosmeticFx.h"
#include "ui/studio/Studio.h"
#include "ui/studio/StudioStyle.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::studio {

using challenge_ui::fitLabel;
using challenge_ui::fz;
using namespace style;

namespace {

struct ToolInfo {
    Tool tool;
    const char* name;
    const char* key;
    const char* help;
};
const ToolInfo kTools[] = {
    {Tool::Pencil, "Crayon", "B", "Clic : couleur 1 · clic droit : couleur 2 · Maj : ligne droite depuis le dernier point"},
    {Tool::Eraser, "Gomme", "E", "Rend les pixels transparents · Maj : ligne droite"},
    {Tool::Fill, "Pot de peinture", "G", "Remplit la zone d'une même couleur, dans l'image en cours · clic droit : couleur 2"},
    {Tool::Picker, "Pipette", "I", "Prend la couleur d'un pixel · clic droit : pour la couleur 2"},
    {Tool::Line, "Ligne", "L", "Glisser d'un bout à l'autre · clic droit : couleur 2"},
    {Tool::Rect, "Rectangle", "R", "Glisser d'un coin à l'autre · Maj en lâchant : plein"},
    {Tool::Ellipse, "Ellipse", "O", "Glisser d'un coin à l'autre · Maj en lâchant : pleine"},
    {Tool::Select, "Sélection", "M", "Glisser pour choisir un rectangle · Suppr : l'effacer · Échap : la quitter"},
    {Tool::Move, "Déplacer", "V", "Glisser la sélection, ou toute l'image si rien n'est choisi · flèches : 1 pixel"},
    {Tool::Mirror, "Miroir et symétrie", "H", "Retourner la sélection ou l'image (H, Maj+H) · symétrie en dessinant (S)"},
};
constexpr int kToolCount = static_cast<int>(sizeof(kTools) / sizeof(kTools[0]));

const char* kSymNames[4] = {"aucune", "verticale", "horizontale", "les deux"};

void hsvToRgb(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b) {
    h = std::fmod(std::fmod(h, 360.0f) + 360.0f, 360.0f);
    const float c = v * s, x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f)), m = v - c;
    float rr = 0, gg = 0, bb = 0;
    if (h < 60) rr = c, gg = x;
    else if (h < 120) rr = x, gg = c;
    else if (h < 180) gg = c, bb = x;
    else if (h < 240) gg = x, bb = c;
    else if (h < 300) rr = x, bb = c;
    else rr = c, bb = x;
    auto to8 = [](float f) { return static_cast<uint8_t>(std::clamp(std::lround(f * 255.0f), 0L, 255L)); };
    r = to8(rr + m);
    g = to8(gg + m);
    b = to8(bb + m);
}

void rgbToHsv(Px c, float& h, float& s, float& v) {
    const float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    v = mx;
    s = mx > 0 ? d / mx : 0;
    if (d <= 0.0f) return;  // grey: the hue stays where it was
    if (mx == r) h = 60.0f * std::fmod((g - b) / d, 6.0f);
    else if (mx == g) h = 60.0f * ((b - r) / d + 2.0f);
    else h = 60.0f * ((r - g) / d + 4.0f);
    if (h < 0) h += 360.0f;
}

/// The icon of a tool, drawn in a box of 20 × 20 units.
void toolIcon(Renderer& r, Tool t, const Rect& box, Color c, float th) {
    const float k = box.w / 20.0f;
    auto P = [&](float x, float y) { return Vec2{box.x + x * k, box.y + y * k}; };
    auto L = [&](float x0, float y0, float x1, float y1) { r.drawLine(P(x0, y0), P(x1, y1), c, th); };
    switch (t) {
    case Tool::Pencil:
        L(3, 17, 4, 14), L(4, 14, 13, 5), L(13, 5, 15, 7), L(15, 7, 6, 16), L(6, 16, 3, 17), L(12, 4, 14, 2), L(14, 2, 18, 6), L(18, 6, 16, 8);
        break;
    case Tool::Eraser:
        L(3, 12, 10, 4), L(10, 4, 17, 11), L(17, 11, 11, 17), L(11, 17, 8, 17), L(8, 17, 3, 12), L(8, 17, 17, 17), L(6, 9, 13, 16);
        break;
    case Tool::Fill:
        L(4, 9, 10, 3), L(10, 3, 16, 9), L(16, 9, 10, 15), L(10, 15, 4, 9), L(17, 12, 18, 15), L(18, 15, 17, 16);
        break;
    case Tool::Picker:
        L(3, 17, 5, 16), L(5, 16, 13, 8), L(13, 8, 11, 6), L(11, 6, 3, 14), L(3, 14, 3, 17), L(12, 5, 14, 3), L(14, 3, 17, 6), L(17, 6, 15, 8);
        break;
    case Tool::Line:
        L(4, 16, 16, 4);
        break;
    case Tool::Rect:
        L(4, 5, 16, 5), L(16, 5, 16, 15), L(16, 15, 4, 15), L(4, 15, 4, 5);
        break;
    case Tool::Ellipse:
        for (int i = 0; i < 24; ++i) {
            const float a0 = i * 6.2831853f / 24.0f, a1 = (i + 1) * 6.2831853f / 24.0f;
            L(10 + 7 * std::cos(a0), 10 + 5 * std::sin(a0), 10 + 7 * std::cos(a1), 10 + 5 * std::sin(a1));
        }
        break;
    case Tool::Select:
        for (int i = 0; i < 6; ++i) {
            const float a = 3 + i * 2.4f;
            L(a, 4, a + 1.2f, 4), L(a, 16, a + 1.2f, 16);
        }
        for (int i = 0; i < 5; ++i) {
            const float a = 4 + i * 2.4f;
            L(3, a, 3, a + 1.2f), L(17, a, 17, a + 1.2f);
        }
        break;
    case Tool::Move:
        L(10, 2, 10, 18), L(2, 10, 18, 10), L(10, 2, 8, 4), L(10, 2, 12, 4), L(10, 18, 8, 16), L(10, 18, 12, 16), L(2, 10, 4, 8), L(2, 10, 4, 12),
            L(18, 10, 16, 8), L(18, 10, 16, 12);
        break;
    case Tool::Mirror:
        L(10, 2, 10, 18), L(8, 5, 3, 15), L(3, 15, 8, 15), L(8, 15, 8, 5), L(12, 5, 17, 15), L(17, 15, 12, 15), L(12, 15, 12, 5);
        break;
    default:
        break;
    }
}

std::string zoomText(float z) { return "Zoom " + std::to_string(static_cast<int>(std::lround(z * 100.0f))) + " %"; }

}  // namespace

// ------------------------------------------------------------------ the rail

void Studio::drawRail(const Rect& r) {
    Renderer& rr = ui_.r();
    drawSection(r);
    ui_.pushId("rail");
    const float pad = ui_.s(6), gap = ui_.s(4);
    const float btn = std::clamp((r.h - 2 * pad - gap * (kToolCount - 1)) / kToolCount, ui_.s(22), std::min(ui_.s(40), r.w - 2 * pad));
    float y = r.y + pad;
    OpenDoc* d = active();
    for (int i = 0; i < kToolCount; ++i) {
        const ToolInfo& t = kTools[i];
        const Rect b{r.center().x - btn * 0.5f, y, btn, btn};
        y += btn + gap;
        bool hovered = false;
        const std::string id = std::string("tool") + std::to_string(i);
        if (ui_.hotspot(id, b, &hovered)) {
            if (t.tool == Tool::Mirror) mirrorMenu_ = !mirrorMenu_;
            else {
                if (d && tool_ != t.tool) dropSelection(*d);
                tool_ = t.tool;
                mirrorMenu_ = false;
            }
        }
        const bool on = t.tool == Tool::Mirror ? mirrorMenu_ : tool_ == t.tool;
        rr.fillRoundedRect(b, ui_.s(8), on ? kSelRow : kField);
        rr.drawRoundedRect(b, ui_.s(8), on ? kAccent : (hovered || ui_.focused(id)) ? kAccent.withAlpha(170) : kBorder,
                           std::max(1.0f, ui_.s(on ? 2.0f : 1.0f)));
        const float ib = btn * 0.56f;
        toolIcon(rr, t.tool, {b.center().x - ib * 0.5f, b.center().y - ib * 0.5f, ib, ib}, on ? Color::white() : kSub, std::max(1.2f, ui_.s(1.6f)));
        tip(id, b, std::string(t.name) + " · " + t.key, t.help);
        if (t.tool == Tool::Mirror) mirrorButton_ = b;
    }
    ui_.popId();
}

void Studio::mirror(OpenDoc& d, bool vertical) {
    const IRect f = currentFrame(d);
    if (d.lifted) {
        if (vertical) d.liftedPx.flipV(d.liftedPx.bounds());
        else d.liftedPx.flipH(d.liftedPx.bounds());
        markDirty(d, f);
        return;
    }
    const IRect target = d.hasSel ? d.sel.intersect(f) : f;
    beginEdit(d, target);
    if (vertical) d.doc.image.flipV(target);
    else d.doc.image.flipH(target);
    markDirty(d, target);
    commit(d, vertical ? "Miroir vertical" : "Miroir horizontal");
}

void Studio::dropSelection(OpenDoc& d) {
    if (!d.lifted) return;
    const IRect f = currentFrame(d);
    d.doc.image.paste(d.liftedPx, d.liftX, d.liftY, f);
    d.sel = IRect{d.liftX, d.liftY, d.liftedPx.w(), d.liftedPx.h()}.intersect(f);
    d.hasSel = !d.sel.empty();
    d.lifted = false;
    d.liftedPx = PixelImage();
    commit(d, "Déplacer");
    markDirty(d, f);
}

void Studio::setColor(int which, Px c) {
    c.a = 255;
    (which == 0 ? color1_ : color2_) = c;
    if (which == editColor_) rgbToHsv(c, hue_, sat_, val_);
}

// ------------------------------------------------------------------ the canvas panel

void Studio::drawCanvasPanel(const Rect& r) {
    OpenDoc* dp = active();
    if (!dp) return;
    OpenDoc& d = *dp;
    Renderer& rr = ui_.r();
    // A particle has its emitter beside it (as the mock-up's third screen).
    Rect panel = r;
    if (particleKind(d.doc.kind)) {
        const float ew = std::clamp(ui_.s(300), 220.0f, r.w * 0.42f);
        panel.w = r.w - ew - ui_.s(8);
        drawEmitter({panel.right() + ui_.s(8), r.y, ew, r.h});
    }
    drawSection(panel);
    ui_.pushId("canvas");
    const float headH = ui_.s(36), footH = ui_.s(30);
    const Rect head{panel.x, panel.y, panel.w, headH};
    const Rect foot{panel.x, panel.bottom() - footH, panel.w, footH};
    rr.fillRect({panel.x + ui_.s(1), head.bottom(), panel.w - ui_.s(2), std::max(1.0f, ui_.s(1))}, kBorder);
    rr.fillRect({panel.x + ui_.s(1), foot.y, panel.w - ui_.s(2), std::max(1.0f, ui_.s(1))}, kBorder);
    const Rect view{panel.x + ui_.s(1), head.bottom() + ui_.s(1), panel.w - ui_.s(2), foot.y - head.bottom() - ui_.s(1)};
    canvasView_ = view;
    drawCanvas(view);

    // The header: where we are, the zoom, the grid, the onion skin, the symmetry, undo / redo.
    const DocAnim* a = currentAnim(d);
    const float pad = ui_.s(12);
    float x = head.x + pad;
    const float cy = head.center().y;
    const float bh = ui_.s(26);
    const std::string undoL = "Annuler", redoL = "Rétablir";
    const float uw = ui_.text().measure(undoL, FontFace::Bold, ui_.s(fz(ui_, 13))).x + ui_.s(22);
    const float rw = ui_.text().measure(redoL, FontFace::Bold, ui_.s(fz(ui_, 13))).x + ui_.s(22);
    const float right = head.right() - pad - uw - rw - ui_.s(6);
    const Rect undoR{right, cy - bh * 0.5f, uw, bh}, redoR{right + uw + ui_.s(6), cy - bh * 0.5f, rw, bh};
    if (smallButton("undo", undoR, undoL, d.history.canUndo())) undo(d);
    tip("undo", undoR, "Annuler · Ctrl+Z", d.history.canUndo() ? d.history.undoLabel() : "Rien à annuler.");
    if (smallButton("redo", redoR, redoL, d.history.canRedo())) redo(d);
    tip("redo", redoR, "Rétablir · Ctrl+Y", d.history.canRedo() ? d.history.redoLabel() : "Rien à rétablir.");
    const std::string where = a ? a->name + " · image " + std::to_string(d.frame + 1) + " / " + std::to_string(a->count) : std::string();
    const float whereW = std::min(ui_.text().measure(where, FontFace::Bold, ui_.s(fz(ui_, 13.5f))).x, ui_.s(200));
    fitLabel(ui_, where, {x, cy}, 13.5f, whereW, kText, Align::Left, FontFace::Bold);
    x += whereW + ui_.s(14);
    // The zoom: − value +.
    const float zb = ui_.s(24);
    const IRect f = currentFrame(d);
    const float autoZ = std::max(1.0f, std::floor(std::min((view.w - ui_.s(40)) / std::max(1, f.w), (view.h - ui_.s(40)) / std::max(1, f.h))));
    const float z = d.zoom > 0 ? d.zoom : autoZ;
    const std::string zt = zoomText(z);
    const float ztw = ui_.text().measure(zt, FontFace::Body, ui_.s(fz(ui_, 13))).x;
    auto fits = [&](float w) { return x + w <= right - ui_.s(10); };
    if (fits(2 * zb + ztw + ui_.s(12))) {
        if (smallButton("zout", {x, cy - zb * 0.5f, zb, zb}, "−")) d.zoom = std::max(1.0f, z > 8 ? std::floor(z / 1.25f) : z - 1);
        tip("zout", {x, cy - zb * 0.5f, zb, zb}, "Zoom arrière", "Molette vers soi, sur la toile");
        ui_.label(zt, {x + zb + ui_.s(6) + ztw * 0.5f, cy}, fz(ui_, 13), kSub, Align::Center, FontFace::Body, VAlign::Middle);
        if (smallButton("zin", {x + zb + ztw + ui_.s(12), cy - zb * 0.5f, zb, zb}, "+")) d.zoom = std::min(64.0f, z >= 8 ? std::ceil(z * 1.25f) : z + 1);
        tip("zin", {x + zb + ztw + ui_.s(12), cy - zb * 0.5f, zb, zb}, "Zoom avant", "Molette vers l'avant, sur la toile");
        x += 2 * zb + ztw + ui_.s(12) + ui_.s(14);
    }
    auto checkW = [&](const std::string& t) { return ui_.text().measure(t, FontFace::Body, ui_.s(fz(ui_, 13))).x + ui_.s(30); };
    const std::string gridL = "Grille", onionL = "Pelure d'oignon (± 1)";
    if (fits(checkW(gridL))) {
        const Rect cr{x, cy - bh * 0.5f, checkW(gridL), bh};
        check("grid", cr, gridL, d.grid);
        tip("grid", cr, "Grille · Ctrl+G", "Une ligne entre chaque pixel quand le zoom le permet.");
        x += cr.w + ui_.s(8);
    }
    if (fits(checkW(onionL))) {
        const Rect cr{x, cy - bh * 0.5f, checkW(onionL), bh};
        check("onion", cr, onionL, d.onion);
        tip("onion", cr, "Pelure d'oignon · Ctrl+P", "L'image d'avant (bleutée) et celle d'après (rosée), en transparence, pour suivre le mouvement.");
        x += cr.w + ui_.s(8);
    }
    const std::string symL = std::string("Symétrie : ") + kSymNames[static_cast<int>(d.sym)];
    const float symW = ui_.text().measure(symL, FontFace::Body, ui_.s(fz(ui_, 13))).x + ui_.s(22);
    if (fits(symW)) {
        const Rect sr{x, cy - bh * 0.5f, symW, bh};
        if (chip("sym", sr, symL, d.sym != Symmetry::None)) d.sym = static_cast<Symmetry>((static_cast<int>(d.sym) + 1) % 4);
        tip("sym", sr, "Symétrie en dessinant · S", "Ce qui est dessiné se reflète dans l'image : aucune, verticale, horizontale, les deux.");
    }

    // The footer: the pixel under the cursor, the frame's size, the two colours, the help.
    {
        float fx = foot.x + pad;
        const float fcy = foot.center().y;
        const bool on = hoverX_ >= 0;
        const std::string pos = on ? "x " + std::to_string(hoverX_ - f.x) + " · y " + std::to_string(hoverY_ - f.y) : std::string("x – · y –");
        const float pw = ui_.text().measure("x 00 · y 00", FontFace::Body, ui_.s(fz(ui_, 13))).x + ui_.s(14);
        fitLabel(ui_, pos, {fx, fcy}, 13, pw, kSub);
        fx += pw;
        const std::string size = std::to_string(f.w) + " × " + std::to_string(f.h) + " px" + (d.doc.kind == DocKind::Suit ? " (taille de Nova)" : "");
        const float sw = ui_.text().measure(size, FontFace::Body, ui_.s(fz(ui_, 13))).x + ui_.s(16);
        const float room = foot.right() - pad - fx;
        if (sw < room) {
            fitLabel(ui_, size, {fx, fcy}, 13, sw, kDim);
            fx += sw;
        }
        const std::string cols = "Couleur 1 " + hexOf(color1_) + " · couleur 2 " + hexOf(color2_);
        const float cw = ui_.text().measure(cols, FontFace::Body, ui_.s(fz(ui_, 13))).x + ui_.s(16);
        if (fx + cw < foot.right() - pad) {
            fitLabel(ui_, cols, {fx, fcy}, 13, cw, kDim);
            fx += cw;
        }
        const std::string help = ui_.usingPointer() ? "Molette : zoom · espace + glisser : déplacer la vue"
                                                    : "A : outil · gâchettes : images · LB / RB : outils · X / Y : annuler / rétablir";
        const float hw = ui_.text().measure(help, FontFace::Body, ui_.s(fz(ui_, 13))).x;
        if (fx + hw < foot.right() - pad) ui_.label(help, {foot.right() - pad, fcy}, fz(ui_, 13), kDim, Align::Right, FontFace::Body, VAlign::Middle);
    }

    // The mirror menu, over the canvas next to the rail.
    if (mirrorMenu_) {
        const float mw = ui_.s(270), rowH = ui_.s(32), mp = ui_.s(8);
        const Rect m{panel.x + ui_.s(4), std::min(mirrorButton_.y, panel.bottom() - (3 * rowH + 2 * mp + ui_.s(28))), mw, 3 * rowH + 2 * mp + ui_.s(28)};
        mirrorMenuRect_ = m;
        rr.fillRoundedRect(m, ui_.s(10), kTip);
        rr.drawRoundedRect(m, ui_.s(10), kAccent, std::max(1.0f, ui_.s(1)));
        float my = m.y + mp;
        if (smallButton("mirh", {m.x + mp, my, mw - 2 * mp, rowH}, "Retourner à l'horizontale · H")) mirror(d, false);
        my += rowH + ui_.s(4);
        if (smallButton("mirv", {m.x + mp, my, mw - 2 * mp, rowH}, "Retourner à la verticale · Maj+H")) mirror(d, true);
        my += rowH + ui_.s(6);
        fitLabel(ui_, "Symétrie en dessinant · S", {m.x + mp, my + ui_.s(8)}, 12.5f, mw - 2 * mp, kDim);
        my += ui_.s(20);
        const float cw = (mw - 2 * mp - 3 * ui_.s(4)) / 4.0f;
        for (int i = 0; i < 4; ++i)
            if (chip("symc" + std::to_string(i), {m.x + mp + i * (cw + ui_.s(4)), my, cw, rowH - ui_.s(4)}, kSymNames[i], static_cast<int>(d.sym) == i))
                d.sym = static_cast<Symmetry>(i);
    } else {
        mirrorMenuRect_ = {};
    }
    ui_.popId();
}

void Studio::drawCanvas(const Rect& view) {
    OpenDoc& d = *active();
    Renderer& rr = ui_.r();
    if (d.texDirty || !d.texRect.empty()) uploadTexture(d);
    drawChecker(view, ui_.s(16));
    const IRect f = currentFrame(d);
    const float autoZ = std::max(1.0f, std::floor(std::min((view.w - ui_.s(40)) / std::max(1, f.w), (view.h - ui_.s(40)) / std::max(1, f.h))));
    const float z = d.zoom > 0 ? d.zoom : autoZ;
    const Rect fs{std::round(view.center().x - f.w * z * 0.5f + d.pan.x), std::round(view.center().y - f.h * z * 0.5f + d.pan.y), f.w * z, f.h * z};
    rr.pushClip(view);
    rr.fillRect(fs, Color{12, 18, 38, 120});
    const DocAnim* a = currentAnim(d);
    if (d.tex && a) {
        auto drawFrame = [&](int index, Color tint) {
            const IRect fr = d.doc.frameRect(*a, index);
            const Rect src{static_cast<float>(fr.x), static_cast<float>(fr.y), static_cast<float>(fr.w), static_cast<float>(fr.h)};
            rr.drawTexture(*d.tex, &src, fs, tint);
        };
        // The onion skin: the frames before and after, faint.
        if (d.onion && a->count > 1 && !d.playing) {
            drawFrame((d.frame - 1 + a->count) % a->count, Color{140, 190, 255, 90});
            drawFrame((d.frame + 1) % a->count, Color{255, 160, 200, 55});
        }
        drawFrame(d.playing ? frameAt(*a, d.playClock) : d.frame, Color::white());
    }
    // A frame of an avatar: where the avatar is (the ring goes around it).
    if (d.doc.kind == DocKind::Frame) {
        const float rad = f.w * z * 0.5f / (1.0f + kAvatarFrameMargin / 40.0f);
        rr.drawCircle(fs.center(), rad, kWarm.withAlpha(150), std::max(1.0f, ui_.s(1.5f)));
    }
    if (d.grid && z >= 4.0f) {
        const Color gc{143, 155, 189, 46};
        for (int i = 1; i < f.w; ++i) rr.fillRect({fs.x + i * z, fs.y, 1, fs.h}, gc);
        for (int i = 1; i < f.h; ++i) rr.fillRect({fs.x, fs.y + i * z, fs.w, 1}, gc);
    }
    rr.drawRect(fs.expanded(1), kAccent.withAlpha(150), 1.0f);
    // The selection (dashes), the cursor of the keyboard / gamepad, the pixel under the mouse.
    auto screenOf = [&](const IRect& ir) { return Rect{fs.x + (ir.x - f.x) * z, fs.y + (ir.y - f.y) * z, ir.w * z, ir.h * z}; };
    if (d.hasSel || d.lifted) {
        const IRect s = d.lifted ? IRect{d.liftX, d.liftY, d.liftedPx.w(), d.liftedPx.h()} : d.sel;
        const Rect sr = screenOf(s);
        const float dash = ui_.s(5);
        const float phase = std::fmod(clock_ * 20.0f, dash * 2);
        for (float t = -phase; t < sr.w; t += dash * 2) {
            const float a0 = std::max(0.0f, t), a1 = std::min(sr.w, t + dash);
            if (a1 > a0) {
                rr.fillRect({sr.x + a0, sr.y, a1 - a0, 1.5f}, kWarm);
                rr.fillRect({sr.x + a0, sr.bottom() - 1.5f, a1 - a0, 1.5f}, kWarm);
            }
        }
        for (float t = -phase; t < sr.h; t += dash * 2) {
            const float a0 = std::max(0.0f, t), a1 = std::min(sr.h, t + dash);
            if (a1 > a0) {
                rr.fillRect({sr.x, sr.y + a0, 1.5f, a1 - a0}, kWarm);
                rr.fillRect({sr.right() - 1.5f, sr.y + a0, 1.5f, a1 - a0}, kWarm);
            }
        }
    }
    rr.popClip();
    handleCanvasInput(d, view, fs, z);
    rr.pushClip(view);
    if (hoverX_ >= 0) rr.drawRect(screenOf({hoverX_, hoverY_, 1, 1}).expanded(1), kWarm, std::max(1.5f, ui_.s(2)));
    if (!ui_.usingPointer() && ui_.focused("canvas")) {
        const Rect c = screenOf({f.x + cursorX_, f.y + cursorY_, 1, 1});
        rr.drawRect(c.expanded(2), Color::white(), std::max(2.0f, ui_.s(2.5f)));
    }
    if (d.loadingImage) fitLabel(ui_, "Téléchargement de son image…", {view.center().x, view.y + ui_.s(16)}, 14, view.w - ui_.s(20), kWarm, Align::Center);
    rr.popClip();
}

void Studio::handleCanvasInput(OpenDoc& d, const Rect& view, const Rect& fs, float z) {
    InputManager& in = ui_.input();
    const IRect f = currentFrame(d);
    const Vec2 m = in.mousePos();
    const bool overMenu = mirrorMenu_ && mirrorMenuRect_.contains(m);
    const bool inside = ui_.acceptsInput() && view.contains(m) && !overMenu;
    // The canvas takes part in the keyboard / gamepad navigation: a cursor moves on it.
    bool hovered = false;
    const bool confirm = ui_.hotspot("canvas", view, &hovered) && !ui_.usingPointer();
    const bool focusedPad = ui_.focused("canvas") && !ui_.usingPointer();
    hoverX_ = hoverY_ = -1;
    if (inside && ui_.usingPointer()) {
        const int px = f.x + static_cast<int>(std::floor((m.x - fs.x) / z)), py = f.y + static_cast<int>(std::floor((m.y - fs.y) / z));
        if (f.contains(px, py)) {
            hoverX_ = px;
            hoverY_ = py;
        }
        // The wheel zooms around the pixel under the mouse.
        if (in.wheel() != 0.0f) {
            const float nz = in.wheel() > 0 ? std::min(64.0f, z >= 8 ? std::ceil(z * 1.25f) : z + 1) : std::max(1.0f, z > 8 ? std::floor(z / 1.25f) : z - 1);
            const Vec2 c = view.center() + d.pan;
            const Vec2 rel = (m - c) / z;
            d.zoom = nz;
            d.pan = m - rel * nz - view.center();
        }
    }
    // Moving the view: middle button, or space + left button.
    const bool panBtn = in.mouseDown(SDL_BUTTON_MIDDLE) || (in.keyDown(SDL_SCANCODE_SPACE) && in.mouseDown(SDL_BUTTON_LEFT) && !ui_.editingText());
    if (panBtn && (inside || panning_)) {
        panning_ = true;
        d.pan = d.pan + in.mouseDelta();
        return;
    }
    panning_ = false;
    // The right stick moves the view too.
    if (focusedPad) d.pan = d.pan - in.rightStick() * (ui_.s(600) * dt_);

    // The gamepad / keyboard cursor and its buttons.
    if (focusedPad) {
        cursorX_ = std::clamp(cursorX_, 0, f.w - 1);
        cursorY_ = std::clamp(cursorY_, 0, f.h - 1);
        // Arrows / D-pad: the cursor; at the edge, the focus leaves the canvas that way.
        if (d.lifted) {
            int dx = (in.menuPressed(Action::MenuRight) ? 1 : 0) - (in.menuPressed(Action::MenuLeft) ? 1 : 0);
            int dy = (in.menuPressed(Action::MenuDown) ? 1 : 0) - (in.menuPressed(Action::MenuUp) ? 1 : 0);
            d.liftX += dx;
            d.liftY += dy;
            if (dx || dy) markDirty(d, f);
            ui_.consumeNavigation(true, true);
        } else {
            const bool l = in.menuPressed(Action::MenuLeft), r = in.menuPressed(Action::MenuRight), u = in.menuPressed(Action::MenuUp), dn = in.menuPressed(Action::MenuDown);
            if ((l && cursorX_ > 0) || (r && cursorX_ < f.w - 1)) {
                cursorX_ += r ? 1 : -1;
                ui_.consumeNavigation(true, false);
            }
            if ((u && cursorY_ > 0) || (dn && cursorY_ < f.h - 1)) {
                cursorY_ += dn ? 1 : -1;
                ui_.consumeNavigation(false, true);
            }
        }
        // LB / RB: the tools; LT / RT: the frames; X / Y: undo / redo.
        if (in.menuPressed(Action::MenuTabPrev) || in.menuPressed(Action::MenuTabNext)) {
            dropSelection(d);
            int t = static_cast<int>(tool_) + (in.menuPressed(Action::MenuTabNext) ? 1 : -1);
            const int n = static_cast<int>(Tool::Mirror);  // the mirror is a menu, not a tool
            tool_ = static_cast<Tool>((t + n) % n);
        }
        const float lt = in.leftTrigger(), rt = in.rightTrigger();
        const DocAnim* a = currentAnim(d);
        if (a && ((lt > 0.6f && triggerL_ <= 0.6f) || (rt > 0.6f && triggerR_ <= 0.6f))) {
            dropSelection(d);
            d.frame = (d.frame + (rt > 0.6f && triggerR_ <= 0.6f ? 1 : -1) + a->count) % a->count;
        }
        triggerL_ = lt;
        triggerR_ = rt;
        if (in.rawPadButton(SDL_GAMEPAD_BUTTON_WEST) && !padWest_) undo(d);
        if (in.rawPadButton(SDL_GAMEPAD_BUTTON_NORTH) && !padNorth_) redo(d);
        padWest_ = in.rawPadButton(SDL_GAMEPAD_BUTTON_WEST);
        padNorth_ = in.rawPadButton(SDL_GAMEPAD_BUTTON_NORTH);
        const bool held = in.rawPadButton(SDL_GAMEPAD_BUTTON_SOUTH) || in.keyDown(SDL_SCANCODE_RETURN) || in.keyDown(SDL_SCANCODE_SPACE);
        const int x = f.x + cursorX_, y = f.y + cursorY_;
        if (confirm && !padHeld_) {
            padHeld_ = true;
            applyTool(d, x, y, true, true, false, false, false);
        } else if (padHeld_ && held) {
            applyTool(d, x, y, false, true, false, false, false);
        } else if (padHeld_) {
            padHeld_ = false;
            applyTool(d, x, y, false, false, true, false, false);
        }
        return;
    }
    padHeld_ = false;
    // The mouse: the left button (colour 1) or the right one (colour 2).
    if (!ui_.usingPointer() && !stroking_) return;
    const int px = f.x + static_cast<int>(std::floor((m.x - fs.x) / z)), py = f.y + static_cast<int>(std::floor((m.y - fs.y) / z));
    const bool left = in.mousePressed(SDL_BUTTON_LEFT), right = in.mousePressed(SDL_BUTTON_RIGHT);
    if (!stroking_ && inside && (left || right) && !in.keyDown(SDL_SCANCODE_SPACE)) {
        stroking_ = true;
        strokeSecondary_ = right;
        applyTool(d, px, py, true, true, false, right, in.shift());
        return;
    }
    if (stroking_) {
        const bool held = strokeSecondary_ ? in.mouseDown(SDL_BUTTON_RIGHT) : in.mouseDown(SDL_BUTTON_LEFT);
        if (held) applyTool(d, px, py, false, true, false, strokeSecondary_, in.shift());
        else {
            stroking_ = false;
            applyTool(d, px, py, false, false, true, strokeSecondary_, in.shift());
        }
    }
}

void Studio::applyTool(OpenDoc& d, int x, int y, bool pressed, bool held, bool released, bool secondary, bool shift) {
    const IRect f = currentFrame(d);
    Canvas cv{d.doc.image, f, d.sym};
    const Px c = secondary ? color2_ : color1_;

    const int cx = std::clamp(x, f.x, f.right() - 1), cy = std::clamp(y, f.y, f.bottom() - 1);
    switch (tool_) {
    case Tool::Pencil:
    case Tool::Eraser: {
        const Px ink = tool_ == Tool::Eraser ? Px{} : c;
        // Only the pixels a stroke changes go to the texture.
        if (pressed) {
            beginEdit(d, f);
            if (shift && lastX_ >= 0 && f.contains(lastX_, lastY_)) markDirty(d, drawLine(cv, lastX_, lastY_, x, y, ink));
            else markDirty(d, plot(cv, x, y, ink));
        } else if (held && !released) {
            if (lastX_ != x || lastY_ != y) markDirty(d, drawLine(cv, lastX_, lastY_, x, y, ink));
        }
        lastX_ = x;
        lastY_ = y;
        if (released) commit(d, tool_ == Tool::Eraser ? "Gomme" : "Crayon");
        break;
    }
    case Tool::Fill:
        if (pressed && f.contains(x, y)) {
            beginEdit(d, f);
            markDirty(d, floodFill(cv, x, y, c));
            commit(d, "Pot de peinture");
        }
        break;
    case Tool::Picker:
        if (pressed && f.contains(x, y)) {
            const Px p = d.doc.image.get(x, y);
            if (p.a == 0) hooks_.say("Pixel transparent : choisissez un pixel de couleur.", false);
            else setColor(secondary ? 1 : 0, p);
        }
        break;
    case Tool::Line:
    case Tool::Rect:
    case Tool::Ellipse:
        if (pressed) {
            beginEdit(d, f);
            strokeX0_ = cx;
            strokeY0_ = cy;
            shapeRect_ = {};
        }
        if (editOpen_) {
            // The shape drawn last frame goes, the new one comes: only those pixels change.
            restoreEdit(d, shapeRect_);
            IRect drawn;
            if (tool_ == Tool::Line) drawn = drawLine(cv, strokeX0_, strokeY0_, cx, cy, c);
            else if (tool_ == Tool::Rect) drawn = drawRect(cv, strokeX0_, strokeY0_, cx, cy, c, released && shift);
            else drawn = drawEllipse(cv, strokeX0_, strokeY0_, cx, cy, c, released && shift);
            // With the symmetry the shape may be mirrored anywhere in the frame.
            shapeRect_ = d.sym == Symmetry::None ? IRect::between(strokeX0_, strokeY0_, cx, cy).unite(drawn) : f;
            markDirty(d, shapeRect_);
        }
        if (released) commit(d, tool_ == Tool::Line ? "Ligne" : tool_ == Tool::Rect ? "Rectangle" : "Ellipse");
        break;
    case Tool::Select:
        if (pressed) {
            dropSelection(d);
            strokeX0_ = cx;
            strokeY0_ = cy;
            d.hasSel = false;
        }
        if (held || released) {
            d.sel = IRect::between(strokeX0_, strokeY0_, cx, cy).intersect(f);
            d.hasSel = !(released && d.sel.w <= 1 && d.sel.h <= 1 && strokeX0_ == cx && strokeY0_ == cy);
        }
        break;
    case Tool::Move:
        if (pressed) {
            if (!d.lifted) {
                beginEdit(d, f);
                const IRect s = d.hasSel ? d.sel.intersect(f) : f;
                d.liftedPx = d.doc.image.crop(s);
                d.doc.image.fill(s, Px{});
                d.liftX = s.x;
                d.liftY = s.y;
                d.lifted = true;
                d.hasSel = true;
                d.sel = s;
            }
            strokeX0_ = x - d.liftX;
            strokeY0_ = y - d.liftY;
        } else if (held && d.lifted) {
            d.liftX = x - strokeX0_;
            d.liftY = y - strokeY0_;
        }
        markDirty(d, f);
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------------ the right column

void Studio::drawRight(const Rect& r) {
    const float gap = ui_.s(12);
    const float ph = std::min(drawPalette(r, true), r.h * 0.62f);
    drawPalette({r.x, r.y, r.w, ph}, false);
    drawPreview({r.x, r.y + ph + gap, r.w, r.h - ph - gap});
}

float Studio::drawPalette(const Rect& r, bool measure) {
    Renderer& rr = ui_.r();
    const float pad = ui_.s(12), gap = ui_.s(6);
    const float w = r.w - 2 * pad, x = r.x + pad;
    OpenDoc* d = active();
    // The palettes: the creation's own first, then those of the library.
    const auto& libs = palettes();
    const int sources = 1 + static_cast<int>(libs.size());
    paletteSource_ = std::clamp(paletteSource_, 0, sources - 1);
    const bool own = paletteSource_ == 0;
    std::vector<Px> colors;
    std::string sourceName;
    if (own) {
        sourceName = d ? (d->doc.name.empty() ? std::string("Sans nom") : d->doc.name) : std::string("Création");
        if (d) colors = d->doc.palette;
    } else {
        sourceName = libs[static_cast<size_t>(paletteSource_ - 1)].name;
        colors = libs[static_cast<size_t>(paletteSource_ - 1)].colors;
    }
    const int cells = static_cast<int>(colors.size()) + (own && d ? 1 : 0);
    const int cols = 8;
    const int rows = std::clamp((cells + cols - 1) / cols, 1, 4);
    const float cw = (w - gap * (cols - 1)) / cols, ch = std::min(ui_.s(28), cw * 1.1f);
    const float pickH = ui_.s(104);
    const float height = pad + ui_.s(24) + rows * (ch + gap) + ui_.s(4) + pickH + ui_.s(10) + ui_.s(36) + pad;
    if (measure) return height;
    drawSection(r);
    ui_.pushId("palette");
    float y = r.y + pad;
    // Title and the palette shown (a click: the next one).
    {
        const float nameW = std::min(w * 0.6f, ui_.text().measure(sourceName, FontFace::Body, ui_.s(fz(ui_, 13))).x + ui_.s(22));
        sectionTitle("PALETTE", x, y, w - nameW - ui_.s(8));
        const Rect src{x + w - nameW, y - ui_.s(2), nameW, ui_.s(22)};
        bool hovered = false;
        if (ui_.hotspot("source", src, &hovered)) paletteSource_ = (paletteSource_ + (ui_.input().shift() ? sources - 1 : 1)) % sources;
        {
            i18n::Verbatim name;
            fitLabel(ui_, sourceName, {src.right() - ui_.s(14), src.center().y}, 13, nameW - ui_.s(16), hovered || ui_.focused("source") ? kAccent : kSub,
                     Align::Right);
        }
        const Vec2 c{src.right() - ui_.s(5), src.center().y};
        rr.fillTriangle({c.x - ui_.s(4), c.y - ui_.s(2)}, {c.x + ui_.s(4), c.y - ui_.s(2)}, {c.x, c.y + ui_.s(3)}, kSub);
        tip("source", src, "Palette : " + sourceName,
            "Clic : la palette suivante (Maj : la précédente) · celle de la création, de Nova, des planètes, couleurs de base.");
        y += ui_.s(24);
    }
    // The swatches: a click gives colour 1, a right click colour 2; « + » adds colour 1.
    InputManager& in = ui_.input();
    for (int i = 0; i < std::min(cells, rows * cols); ++i) {
        const Rect s{x + (i % cols) * (cw + gap), y + (i / cols) * (ch + gap), cw, ch};
        const std::string id = "sw" + std::to_string(i);
        bool hovered = false;
        const bool clicked = ui_.hotspot(id, s, &hovered);
        if (i < static_cast<int>(colors.size())) {
            const Px c = colors[static_cast<size_t>(i)];
            drawSwatch(s, c, c == color1_ || (hovered || ui_.focused(id)));
            if (clicked) {
                if (own && d && in.shift()) {
                    const Json before = d->doc.metaJson();
                    d->doc.palette.erase(d->doc.palette.begin() + i);
                    commitMeta(*d, "Retirer de la palette", before);
                } else {
                    setColor(0, c);
                }
            }
            if (hovered && in.mousePressed(SDL_BUTTON_RIGHT)) setColor(1, c);
            tip(id, s, hexOf(c), own ? "Clic : couleur 1 · clic droit : couleur 2 · Maj+clic : la retirer" : "Clic : couleur 1 · clic droit : couleur 2");
        } else {
            // « + »: colour 1 into the creation's palette.
            rr.drawRoundedRect(s, ui_.s(5), hovered || ui_.focused(id) ? kAccent : kDim, std::max(1.0f, ui_.s(1)));
            const Vec2 c = s.center();
            rr.drawLine({c.x - ui_.s(5), c.y}, {c.x + ui_.s(5), c.y}, kDim, std::max(1.2f, ui_.s(1.5f)));
            rr.drawLine({c.x, c.y - ui_.s(5)}, {c.x, c.y + ui_.s(5)}, kDim, std::max(1.2f, ui_.s(1.5f)));
            if (clicked && d && d->doc.palette.size() < 31 && std::find(d->doc.palette.begin(), d->doc.palette.end(), color1_) == d->doc.palette.end()) {
                const Json before = d->doc.metaJson();
                d->doc.palette.push_back(color1_);
                commitMeta(*d, "Ajouter à la palette", before);
            }
            tip(id, s, "Ajouter la couleur 1 à la palette", hexOf(color1_));
        }
    }
    y += rows * (ch + gap) + ui_.s(4);
    drawPicker({x, y, w, pickH});
    y += pickH + ui_.s(10);
    const Rect rec{x, y, w, ui_.s(36)};
    bool hovered = false;
    if (ui_.hotspot("recolor", rec, &hovered) && d) {
        recolorOpen_ = true;
        recolorFrom_ = color2_;
        recolorTo_ = color1_;
        recolorSuit_ = d->doc.colors;
    }
    rr.fillRoundedRect(rec, ui_.s(8), kVioletBg);
    rr.drawRoundedRect(rec, ui_.s(8), hovered || ui_.focused("recolor") ? Color::white() : kViolet, std::max(1.0f, ui_.s(hovered ? 2.0f : 1.0f)));
    fitLabel(ui_, "Recolorer toutes les images…", rec.center(), 14, rec.w - ui_.s(16), d ? kText : kDim, Align::Center);
    tip("recolor", rec, "Recolorer toutes les images…", "Une couleur remplacée par une autre dans toutes les images d'un coup (ou une animation, ou une image).");
    ui_.newBadge("recolor", rec, "admin.creations.studio.recolor", true, true);
    ui_.popId();
    return height;
}

void Studio::drawPicker(const Rect& r) {
    Renderer& rr = ui_.r();
    InputManager& in = ui_.input();
    ui_.pushId("picker");
    const float sq = std::min(r.w * 0.5f, ui_.s(150)), hueW = ui_.s(16), gap = ui_.s(10);
    const Rect svR{r.x, r.y, sq, r.h};
    const Rect hueR{svR.right() + gap, r.y, hueW, r.h};
    const Rect info{hueR.right() + gap, r.y, r.right() - hueR.right() - gap, r.h};
    // The square: saturation (→) and value (↑) of the hue; the bar: the hue.
    const int steps = 12;
    for (int i = 0; i < steps; ++i)
        for (int j = 0; j < steps; ++j) {
            uint8_t cr, cg, cb;
            hsvToRgb(hue_, (i + 0.5f) / steps, 1.0f - (j + 0.5f) / steps, cr, cg, cb);
            rr.fillRect({svR.x + i * svR.w / steps, svR.y + j * svR.h / steps, svR.w / steps + 1, svR.h / steps + 1}, Color{cr, cg, cb, 255});
        }
    for (int j = 0; j < 24; ++j) {
        uint8_t cr, cg, cb;
        hsvToRgb(j * 15.0f, 1, 1, cr, cg, cb);
        uint8_t dr, dg, db;
        hsvToRgb((j + 1) * 15.0f, 1, 1, dr, dg, db);
        rr.fillGradientV({hueR.x, hueR.y + j * hueR.h / 24, hueR.w, hueR.h / 24 + 1}, Color{cr, cg, cb, 255}, Color{dr, dg, db, 255});
    }
    bool hovSv = false, hovHue = false;
    ui_.hotspot("sv", svR, &hovSv);
    ui_.hotspot("hue", hueR, &hovHue);
    bool changed = false;
    if (ui_.acceptsInput()) {
        if (hovSv && in.mousePressed(SDL_BUTTON_LEFT)) dragPicker_ = 1;
        if (hovHue && in.mousePressed(SDL_BUTTON_LEFT)) dragPicker_ = 2;
        if (!in.mouseDown(SDL_BUTTON_LEFT)) dragPicker_ = 0;
        const Vec2 m = in.mousePos();
        if (dragPicker_ == 1) {
            sat_ = std::clamp((m.x - svR.x) / svR.w, 0.0f, 1.0f);
            val_ = std::clamp(1.0f - (m.y - svR.y) / svR.h, 0.0f, 1.0f);
            changed = true;
        } else if (dragPicker_ == 2) {
            hue_ = std::clamp((m.y - hueR.y) / hueR.h, 0.0f, 1.0f) * 359.0f;
            changed = true;
        }
        if (ui_.focused("sv") && !ui_.editingText()) {
            const float k = in.shift() ? 0.01f : 0.04f;
            if (in.menuPressed(Action::MenuLeft)) sat_ -= k, changed = true;
            if (in.menuPressed(Action::MenuRight)) sat_ += k, changed = true;
            if (in.menuPressed(Action::MenuUp)) val_ += k, changed = true;
            if (in.menuPressed(Action::MenuDown)) val_ -= k, changed = true;
            sat_ = std::clamp(sat_, 0.0f, 1.0f);
            val_ = std::clamp(val_, 0.0f, 1.0f);
            ui_.consumeNavigation(true, true);
        }
        if (ui_.focused("hue") && !ui_.editingText()) {
            const float k = in.shift() ? 1.0f : 6.0f;
            if (in.menuPressed(Action::MenuUp)) hue_ -= k, changed = true;
            if (in.menuPressed(Action::MenuDown)) hue_ += k, changed = true;
            hue_ = std::fmod(hue_ + 360.0f, 360.0f);
            ui_.consumeNavigation(false, true);
        }
    }
    if (changed) {
        Px c;
        hsvToRgb(hue_, sat_, val_, c.r, c.g, c.b);
        c.a = 255;
        (editColor_ == 0 ? color1_ : color2_) = c;
    }
    rr.drawRect(svR, ui_.focused("sv") ? kAccent : kBorder, std::max(1.0f, ui_.s(1)));
    rr.drawRect(hueR, ui_.focused("hue") ? kAccent : kBorder, std::max(1.0f, ui_.s(1)));
    const Vec2 knob{svR.x + sat_ * svR.w, svR.y + (1.0f - val_) * svR.h};
    rr.drawCircle(knob, ui_.s(5), Color::white(), std::max(1.5f, ui_.s(2)));
    rr.fillRect({hueR.x - ui_.s(2), hueR.y + hue_ / 360.0f * hueR.h - ui_.s(1.5f), hueR.w + ui_.s(4), ui_.s(3)}, Color::white());
    tip("sv", svR, "Saturation et valeur", "Glisser · flèches : de 4 % en 4 % (Maj : 1 %)");
    tip("hue", hueR, "Teinte", "Glisser · ↑ ↓ : de 6° en 6° (Maj : 1°)");
    // The two colours (a click: the one the picker edits), the code, the values.
    const float sh = ui_.s(30), sgap = ui_.s(6);
    const float sw = (info.w - sgap) * 0.5f;
    for (int i = 0; i < 2; ++i) {
        const Rect s{info.x + i * (sw + sgap), info.y, sw, sh};
        const std::string id = i == 0 ? "c1" : "c2";
        if (ui_.hotspot(id, s)) {
            editColor_ = i;
            rgbToHsv(i == 0 ? color1_ : color2_, hue_, sat_, val_);
        }
        drawSwatch(s, i == 0 ? color1_ : color2_, editColor_ == i);
        const Px c = i == 0 ? color1_ : color2_;
        const bool light = c.r * 0.3f + c.g * 0.59f + c.b * 0.11f > 140.0f;
        ui_.label(i == 0 ? "1" : "2", s.center(), fz(ui_, 13), light ? Color{10, 14, 30, 255} : Color::white(), Align::Center, FontFace::Bold, VAlign::Middle);
        tip(id, s, i == 0 ? "Couleur 1" : "Couleur 2", "Clic : le sélecteur la modifie · X : échanger les deux couleurs");
    }
    Px& cur = editColor_ == 0 ? color1_ : color2_;
    const bool typing = ui_.focused("hex") && ui_.editingText();
    if (!typing && (cur != hexShown_ || hex_.empty())) {
        hex_ = hexOf(cur);
        hexShown_ = cur;
    }
    const float fh = ui_.s(30);
    ui_.textField("hex", {info.x, info.y + sh + ui_.s(8), info.w, fh}, hex_, "#rrggbb", 7);
    if (Px p; typing && parseHex(hex_, p) && p != cur) {
        cur = p;
        hexShown_ = p;
        rgbToHsv(p, hue_, sat_, val_);
    }
    tip("hex", {info.x, info.y + sh + ui_.s(8), info.w, fh}, "Code de la couleur", "#rrggbb, par exemple #3ee6d6");
    char b[64];
    std::snprintf(b, sizeof b, "T %d° · S %d %% · V %d %%", static_cast<int>(std::lround(hue_)), static_cast<int>(std::lround(sat_ * 100)),
                  static_cast<int>(std::lround(val_ * 100)));
    const float ty = info.y + sh + ui_.s(8) + fh + ui_.s(4);
    if (ty + ui_.s(16) <= info.bottom()) fitLabel(ui_, b, {info.x, ty + ui_.s(8)}, 12.5f, info.w, kDim);
    ui_.popId();
}

// ------------------------------------------------------------------ the emitter

void Studio::drawEmitter(const Rect& r) {
    OpenDoc* dp = active();
    if (!dp) return;
    OpenDoc& d = *dp;
    drawSection(r);
    ui_.pushId("emitter");
    const float pad = ui_.s(14);
    const float x = r.x + pad, w = r.w - 2 * pad;
    float y = r.y + pad;
    y += sectionTitle("ÉMETTEUR", x, y, w);
    const Json before = d.doc.metaJson();
    ImageEmitter& e = d.doc.emitter;
    struct Row {
        const char* key;
        const char* label;
        int* value;
        int step;
        std::string text;
    };
    auto signedText = [](int v, const char* unit) { return (v < 0 ? "−" + std::to_string(-v) : std::to_string(v)) + unit; };
    const char* fades[3] = {"aucun", "à la fin", "progressif"};
    std::vector<Row> rows = {
        {"rate", "Particules par seconde", &e.rate, 1, std::to_string(e.rate)},
        {"life_ms", "Durée de vie", &e.lifeMs, 50, std::to_string(e.lifeMs) + " ms"},
        {"size", "Taille", &e.size, 5, std::to_string(e.size) + " %"},
        {"speed", "Vitesse", &e.speed, 5, std::to_string(e.speed) + " px/s"},
        {"spread", "Dispersion (angle)", &e.spread, 5, std::to_string(e.spread) + "°"},
        {"gravity", "Gravité", &e.gravity, 10, signedText(e.gravity, "")},
        {"spin", "Rotation", &e.spin, 10, signedText(e.spin, "°/s")},
        {"fade", "Fondu", &e.fade, 1, fades[std::clamp(e.fade, 0, 2)]},
    };
    const float rowH = ui_.s(18) + ui_.s(4) + ui_.s(8);
    const float avail = r.bottom() - pad - ui_.s(64) - y;
    const float gapR = std::clamp((avail - rows.size() * rowH) / static_cast<float>(rows.size()), ui_.s(2), ui_.s(12));
    bool changed = false;
    for (Row& row : rows) {
        int minV = 0, maxV = 0;
        for (const EmitterBound& b : emitterBounds())
            if (std::string(b.key) == row.key) {
                minV = b.min;
                maxV = b.max;
            }
        const Rect sr{x, y, w, rowH};
        if (y + rowH <= r.bottom() - pad - ui_.s(60)) changed |= slider(std::string("e_") + row.key, sr, row.label, *row.value, minV, maxV, row.step, row.text);
        y += rowH + gapR;
    }
    // The colours from the start of a particle's life to its end; its sparkles.
    const float cy = std::min(y + ui_.s(4), r.bottom() - pad - ui_.s(56));
    const float lw = ui_.text().measure("Couleurs", FontFace::Body, ui_.s(fz(ui_, 14))).x;
    ui_.label("Couleurs", {x, cy + ui_.s(11)}, fz(ui_, 14), kText, Align::Left, FontFace::Body, VAlign::Middle);
    const Rect a{x + lw + ui_.s(10), cy, ui_.s(30), ui_.s(22)}, b{a.right() + ui_.s(26), cy, ui_.s(30), ui_.s(22)};
    for (int i = 0; i < 2; ++i) {
        const Rect s = i == 0 ? a : b;
        Rgb8& col = i == 0 ? d.doc.look.a : d.doc.look.b;
        bool hovered = false;
        if (ui_.hotspot(i == 0 ? "ca" : "cb", s, &hovered)) {
            col = Rgb8{color1_.r, color1_.g, color1_.b};
            changed = true;
        }
        drawSwatch(s, Px::rgb(col.r, col.g, col.b), hovered || ui_.focused(i == 0 ? "ca" : "cb"));
        tip(i == 0 ? "ca" : "cb", s, i == 0 ? "Couleur au début de la vie d'une particule" : "Couleur à la fin de sa vie",
            "Clic : prendre la couleur 1 (" + hexOf(color1_) + ") · les images de la particule sont teintées de l'une à l'autre.");
    }
    ui_.r().drawLine({a.right() + ui_.s(6), a.center().y}, {b.x - ui_.s(6), a.center().y}, kDim, std::max(1.0f, ui_.s(1.5f)));
    ui_.r().fillTriangle({b.x - ui_.s(6), a.center().y}, {b.x - ui_.s(11), a.center().y - ui_.s(4)}, {b.x - ui_.s(11), a.center().y + ui_.s(4)}, kDim);
    const float sx = b.right() + ui_.s(10);
    if (sx + ui_.s(90) <= x + w) changed |= check("sparkles", {sx, cy - ui_.s(2), x + w - sx, ui_.s(26)}, "Étincelles", e.sparkles);
    tip("sparkles", {sx, cy - ui_.s(2), x + w - sx, ui_.s(26)}, "Étincelles", "De petites étoiles aux couleurs de la particule, ici et là.");
    // Committed once a slider is let go (or at once with the keyboard, the gamepad, a click).
    if (dragSlider_ >= 0) {
        if (sliderBefore_.is_null()) sliderBefore_ = before;
    } else if (!sliderBefore_.is_null()) {
        commitMeta(d, "Émetteur", sliderBefore_);
        sliderBefore_ = Json();
    } else if (changed) {
        commitMeta(d, "Émetteur", before);
    }
    ui_.popId();
}

}  // namespace cosmic::studio
