// 2.61 Studio de Créations (see Studio.h): the frise (the animations and their frames, the
// onion skin's neighbours, playing, frames added, copied, removed) and the live preview.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "engine/core/I18n.h"
#include "engine/input/InputManager.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "ui/screens/ChallengeUi.h"
#include "ui/screens/CosmeticFx.h"
#include "ui/screens/CreationImageFx.h"
#include "ui/studio/Studio.h"
#include "ui/studio/StudioStyle.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::studio {

using challenge_ui::fitLabel;
using challenge_ui::fz;
using namespace style;

// ------------------------------------------------------------------ the frise

void Studio::drawFrise(const Rect& r) {
    OpenDoc* dp = active();
    if (!dp) return;
    OpenDoc& d = *dp;
    Renderer& rr = ui_.r();
    drawSection(r);
    ui_.pushId("frise");
    const float pad = ui_.s(12);
    const float x = r.x + pad, w = r.w - 2 * pad;
    float y = r.y + ui_.s(10);
    const auto anims = animsOf(d);
    if (anims.empty()) {
        ui_.popId();
        return;
    }
    d.anim = std::clamp(d.anim, 0, static_cast<int>(anims.size()) - 1);
    const DocAnim& cur = anims[static_cast<size_t>(d.anim)];
    d.frame = std::clamp(d.frame, 0, cur.count - 1);
    if (d.playing) d.playClock += dt_;
    // Title, the animations (chips: their frames; « à faire » when some are empty) and, for a
    // suit with empty frames, « Compléter avec Nova ».
    const float chipH = ui_.s(26);
    const float titleW = ui_.text().measure("FRISE", FontFace::Bold, ui_.s(fz(ui_, 14))).x + ui_.s(14);
    fitLabel(ui_, "FRISE", {x, y + chipH * 0.5f}, 14, titleW, kDim, Align::Left, FontFace::Bold);
    bool anyEmpty = false;
    std::vector<int> drawn(anims.size(), 0);
    for (size_t i = 0; i < anims.size(); ++i) {
        drawn[i] = d.doc.drawnFrames(anims[i]);
        anyEmpty = anyEmpty || drawn[i] < anims[i].count;
    }
    float stripRight = x + w;
    if (d.doc.kind == DocKind::Suit && anyEmpty) {
        const std::string fill = "Compléter avec Nova";
        const float fw = ui_.text().measure(fill, FontFace::Bold, ui_.s(fz(ui_, 13))).x + ui_.s(24);
        const Rect fb{x + w - fw, y, fw, chipH};
        if (smallButton("complete", fb, fill)) {
            beginEdit(d);
            const PixelImage& nova = novaSheet();
            for (const DocAnim& a : anims)
                for (int i = 0; i < a.count; ++i) {
                    const IRect fr = d.doc.frameRect(a, i);
                    if (d.doc.image.transparent(fr)) d.doc.image.paste(nova.crop(fr), fr.x, fr.y);
                }
            commit(d, "Compléter avec Nova");
        }
        tip("complete", fb, "Compléter avec Nova", "Les images vides prennent celles de Nova (une combinaison doit les avoir toutes).");
        stripRight = fb.x - ui_.s(8);
    }
    {
        const Rect strip{x + titleW, y, stripRight - x - titleW, chipH};
        std::vector<std::string> labels;
        std::vector<float> widths;
        float total = 0.0f;
        for (size_t i = 0; i < anims.size(); ++i) {
            const bool full = drawn[i] >= anims[i].count;
            labels.push_back(full ? anims[i].name + " " + std::to_string(anims[i].count)
                                  : anims[i].name + " " + std::to_string(drawn[i]) + " / " + std::to_string(anims[i].count) + " à faire");
            widths.push_back(ui_.text().measure(labels.back(), FontFace::Bold, ui_.s(fz(ui_, 13))).x + ui_.s(20));
            total += widths.back() + ui_.s(6);
        }
        // When they do not all fit: arrows at both ends, the strip scrolled to the chosen one.
        const bool scroll = total > strip.w;
        const float aw = scroll ? ui_.s(24) : 0.0f;
        const Rect inner{strip.x + aw, strip.y, strip.w - 2 * aw, strip.h};
        float before = 0.0f;
        for (int i = 0; i < d.anim; ++i) before += widths[static_cast<size_t>(i)] + ui_.s(6);
        if (scroll) {
            if (before - friseScroll_ < 0) friseScroll_ = before;
            if (before + widths[static_cast<size_t>(d.anim)] - friseScroll_ > inner.w) friseScroll_ = before + widths[static_cast<size_t>(d.anim)] - inner.w;
            friseScroll_ = std::clamp(friseScroll_, 0.0f, std::max(0.0f, total - inner.w));
            if (smallButton("aprev", {strip.x, strip.y, aw - ui_.s(2), strip.h}, "<", d.anim > 0)) d.anim = std::max(0, d.anim - 1), d.frame = 0;
            if (smallButton("anext", {strip.right() - aw + ui_.s(2), strip.y, aw - ui_.s(2), strip.h}, ">", d.anim + 1 < static_cast<int>(anims.size())))
                d.anim = std::min(static_cast<int>(anims.size()) - 1, d.anim + 1), d.frame = 0;
        } else {
            friseScroll_ = 0.0f;
        }
        rr.pushClip(inner);
        float cx = inner.x - friseScroll_;
        for (size_t i = 0; i < anims.size(); ++i) {
            const Rect c{cx, strip.y, widths[i], strip.h};
            cx += widths[i] + ui_.s(6);
            if (c.right() < inner.x || c.x > inner.right()) continue;
            const bool full = drawn[i] >= anims[i].count;
            if (chip("a" + std::to_string(i), c, labels[i], static_cast<int>(i) == d.anim, true, full ? Color::transparent() : kWarm)) {
                dropSelection(d);
                d.anim = static_cast<int>(i);
                d.frame = 0;
                d.playClock = 0.0f;
            }
            tip("a" + std::to_string(i), c, anims[i].name,
                d.doc.kind == DocKind::Suit ? std::to_string(anims[i].count) + " images, la rangée " + std::to_string(anims[i].row + 1) + " de la planche de Nova"
                                            : std::to_string(anims[i].count) + " images");
        }
        rr.popClip();
    }
    y += chipH + ui_.s(8);
    // The frames (a click: that frame), and at the right the buttons.
    const float bh = ui_.s(32), bgap = ui_.s(6);
    const float bw = std::min(ui_.s(270), w * 0.4f);
    const float thumbsH = r.bottom() - ui_.s(10) - y;
    const float th = std::max(ui_.s(28), thumbsH);
    const float roomW = w - bw - ui_.s(12);
    const float tw = std::clamp((roomW - (cur.count - 1) * ui_.s(8)) / cur.count, ui_.s(26), std::min(th * 0.75f, ui_.s(64)));
    if (d.texDirty || !d.texRect.empty()) uploadTexture(d);
    const int playing = d.playing ? frameAt(cur, d.playClock) : -1;
    rr.pushClip({x, y - ui_.s(2), roomW, th + ui_.s(4)});
    for (int i = 0; i < cur.count; ++i) {
        const Rect cell{x + i * (tw + ui_.s(8)), y, tw, th};
        if (cell.x > x + roomW) break;
        const std::string id = "f" + std::to_string(i);
        bool hovered = false;
        if (ui_.hotspot(id, cell, &hovered)) {
            dropSelection(d);
            d.frame = i;
            d.playing = false;
        }
        rr.fillRoundedRect(cell, ui_.s(8), kField);
        const IRect fr = d.doc.frameRect(cur, i);
        const float k = std::min((cell.w - ui_.s(6)) / fr.w, (cell.h - ui_.s(6)) / fr.h);
        const Rect img = Rect::fromCenter(cell.center(), {fr.w * k, fr.h * k});
        drawChecker(img, std::max(ui_.s(4), img.w / 8));
        if (d.tex) {
            const Rect src{static_cast<float>(fr.x), static_cast<float>(fr.y), static_cast<float>(fr.w), static_cast<float>(fr.h)};
            rr.drawTexture(*d.tex, &src, img);
        }
        const bool sel = i == d.frame;
        rr.drawRoundedRect(cell, ui_.s(8), sel ? kAccent : i == playing ? kViolet : (hovered || ui_.focused(id)) ? kAccent.withAlpha(150) : kBorder,
                           std::max(1.0f, ui_.s(sel || i == playing ? 2.0f : 1.0f)));
        if (d.doc.image.transparent(fr)) fitLabel(ui_, "vide", {cell.center().x, cell.bottom() - ui_.s(9)}, 11.5f, cell.w - ui_.s(4), kWarm, Align::Center);
        tip(id, cell, "Image " + std::to_string(i + 1) + " / " + std::to_string(cur.count), "« , » et « . » : image précédente, suivante · gâchettes à la manette");
    }
    rr.popClip();
    // The buttons: two rows.
    const float bx = x + w - bw;
    const bool strip = d.doc.kind != DocKind::Suit;
    const float b3 = (bw - 2 * bgap) / 3.0f;
    const float by = y + std::max(0.0f, (th - 2 * bh - bgap) * 0.5f);
    const std::string reason = "La combinaison garde la découpe de Nova : " + std::to_string(cur.count) + " images pour « " + cur.name + " ».";
    if (smallButton("add", {bx, by, b3, bh}, "+ Image", strip && d.doc.stripFrames() < kImageFramesMax)) {
        beginEdit(d);
        if (d.doc.insertFrame(d.frame, false)) ++d.frame;
        commit(d, "+ Image");
    }
    tip("add", {bx, by, b3, bh}, "+ Image", strip ? "Une image vide après celle-ci (16 au plus)." : reason);
    if (smallButton("dupf", {bx + b3 + bgap, by, b3, bh}, "Dupliquer", strip ? d.doc.stripFrames() < kImageFramesMax : d.frame + 1 < cur.count)) {
        if (strip) {
            beginEdit(d);
            if (d.doc.insertFrame(d.frame, true)) ++d.frame;
        } else {
            const IRect from = d.doc.frameRect(cur, d.frame), to = d.doc.frameRect(cur, d.frame + 1);
            beginEdit(d, to);
            d.doc.image.paste(d.doc.image.crop(from), to.x, to.y);
            markDirty(d, to);
            ++d.frame;
        }
        commit(d, "Dupliquer l'image");
    }
    tip("dupf", {bx + b3 + bgap, by, b3, bh}, "Dupliquer · Ctrl+D", strip ? "Une copie de cette image, juste après." : "Copie cette image sur la suivante de « " + cur.name + " ».");
    if (smallButton("delf", {bx + 2 * (b3 + bgap), by, b3, bh}, "Supprimer", strip ? d.doc.stripFrames() > 1 : !d.doc.image.transparent(currentFrame(d)), false, true)) {
        if (strip) {
            beginEdit(d);
            d.doc.removeFrame(d.frame);
            d.frame = std::max(0, d.frame - 1);
        } else {
            const IRect fr = currentFrame(d);
            beginEdit(d, fr);
            d.doc.image.fill(fr, Px{});
            markDirty(d, fr);
        }
        commit(d, "Supprimer l'image");
    }
    tip("delf", {bx + 2 * (b3 + bgap), by, b3, bh}, "Supprimer", strip ? "Retire cette image de la particule." : "Vide cette image (à redessiner ou « Compléter avec Nova »).");
    const float y2 = by + bh + bgap;
    const float pw = (bw - bgap) * 0.45f;
    if (smallButton("play", {bx, y2, pw, bh}, d.playing ? "Pause" : "▶ Lecture", true, true)) {
        d.playing = !d.playing;
        d.playClock = 0.0f;
    }
    tip("play", {bx, y2, pw, bh}, d.playing ? "Pause · P" : "Lecture · P", "L'animation au rythme où le jeu la joue.");
    const Rect fpsR{bx + pw + bgap, y2, bw - pw - bgap, bh};
    if (strip) {
        const float sb = ui_.s(26);
        const Json before = d.doc.metaJson();
        if (smallButton("fpsm", {fpsR.x, fpsR.y + (bh - sb) * 0.5f, sb, sb}, "−", d.doc.fps > kImageFpsMin)) d.doc.fps = std::max(kImageFpsMin, d.doc.fps - 1);
        if (smallButton("fpsp", {fpsR.right() - sb, fpsR.y + (bh - sb) * 0.5f, sb, sb}, "+", d.doc.fps < kImageFpsMax)) d.doc.fps = std::min(kImageFpsMax, d.doc.fps + 1);
        commitMeta(d, "Images par seconde", before);
        fitLabel(ui_, std::to_string(d.doc.fps) + " images / s", {fpsR.center().x, fpsR.center().y}, 13, fpsR.w - 2 * sb - ui_.s(8), kSub, Align::Center);
        tip("fpsm", {fpsR.x, fpsR.y, sb, bh}, "Moins d'images par seconde",
            particleKind(d.doc.kind) ? "La lecture de la frise ; dans le jeu, la particule joue ses images sur sa durée de vie." : "Le rythme du cadre animé.");
    } else {
        float total = 0.0f;
        for (float t : cur.durations) total += t;
        const int fps = total > 0 ? static_cast<int>(std::lround(cur.count / total)) : 0;
        fitLabel(ui_, std::to_string(fps) + " images / s (rythme du jeu)", {fpsR.x + ui_.s(4), fpsR.center().y}, 13, fpsR.w - ui_.s(4), kDim);
    }
    ui_.popId();
}

// ------------------------------------------------------------------ the live preview

void Studio::drawPreview(const Rect& r) {
    Renderer& rr = ui_.r();
    drawSection(r);
    ui_.pushId("preview");
    const float pad = ui_.s(12);
    const float x = r.x + pad, w = r.w - 2 * pad;
    float y = r.y + pad;
    y += sectionTitle("APERÇU EN DIRECT", x, y, w);
    OpenDoc* dp = active();
    if (!dp) {
        ui_.labelWrapped("Ouvrez une création : elle se voit ici comme dans le jeu.", {x, y + ui_.s(4)}, w, fz(ui_, 14), kDim);
        ui_.popId();
        return;
    }
    OpenDoc& d = *dp;
    if (d.texDirty || !d.texRect.empty()) uploadTexture(d);
    std::vector<std::string> modes;
    switch (d.doc.kind) {
    case DocKind::Suit: modes = {"En jeu", "Fantôme", "Profil"}; break;
    case DocKind::Trail: modes = {"En jeu", "Fantôme"}; break;
    case DocKind::Frame: modes = {"Profil", "Classement"}; break;
    case DocKind::Victory: modes = {"En jeu"}; break;
    }
    d.previewMode = std::clamp(d.previewMode, 0, static_cast<int>(modes.size()) - 1);
    float cx = x;
    for (size_t i = 0; i < modes.size(); ++i) {
        const float cw = ui_.text().measure(modes[i], FontFace::Bold, ui_.s(fz(ui_, 13))).x + ui_.s(22);
        if (cx + cw > x + w) break;
        if (chip("m" + std::to_string(i), {cx, y, cw, ui_.s(24)}, modes[i], static_cast<int>(i) == d.previewMode)) d.previewMode = static_cast<int>(i);
        cx += cw + ui_.s(6);
    }
    // « Rejouer » for a victory effect.
    if (d.doc.kind == DocKind::Victory) {
        const std::string again = "Rejouer";
        const float aw = ui_.text().measure(again, FontFace::Bold, ui_.s(fz(ui_, 13))).x + ui_.s(24);
        if (cx + aw <= x + w && smallButton("fx_again", {x + w - aw, y - ui_.s(2), aw, ui_.s(28)}, again)) previewClock_ = 0.0f;
    }
    y += ui_.s(24) + ui_.s(10);
    const float capH = ui_.s(20);
    const Rect scene{x, y, w, r.bottom() - pad - capH - ui_.s(6) - y};
    if (scene.h < ui_.s(40)) {
        ui_.popId();
        return;
    }
    previewClock_ += dt_;
    const float t = previewClock_;
    const std::string mode = modes[static_cast<size_t>(d.previewMode)];
    std::string caption;
    // The little scene: a sky, a floor of tiles (as the mock-up).
    const float ground = scene.y + scene.h * 0.62f;
    auto drawScene = [&]() {
        rr.fillRoundedRectGradient({scene.x, scene.y, scene.w, ground - scene.y + ui_.s(10)}, ui_.s(10), Color{26, 18, 64, 255}, Color{42, 22, 80, 255});
        rr.fillRect({scene.x, ground, scene.w, scene.bottom() - ground}, Color{16, 22, 46, 255});
        const float tile = ui_.s(24);
        for (float tx = scene.x; tx < scene.right(); tx += tile * 2) rr.fillRect({tx, ground, std::min(tile, scene.right() - tx), scene.bottom() - ground}, Color{30, 42, 82, 255});
        rr.fillRect({scene.x, ground, scene.w, std::max(1.0f, ui_.s(2))}, kAccent.withAlpha(90));
    };
    const AnimationSetPtr nova = g_.app.resources().animation("animations/nova.json");
    auto clipOf = [&](const std::string& id) -> const DocAnim* {
        for (const DocAnim& a : novaAnims_)
            if (a.id == id) return &a;
        return nullptr;
    };
    // The sheet Nova wears: the creation's for a suit, the player's own suit otherwise.
    TexturePtr sheet = d.doc.kind == DocKind::Suit ? d.tex : g_.suitSheet(g_.cosmeticLook("suit"));
    if (!sheet && nova) sheet = nova->texture;
    rr.pushClip(scene);
    if (d.doc.kind == DocKind::Suit || d.doc.kind == DocKind::Trail) {
        if (mode == "Profil") {
            // As on a profile: the avatar and its frame, Nova in the suit beside it.
            rr.fillRoundedRect(scene, ui_.s(10), Color{14, 20, 40, 255});
            const float size = std::min(scene.h * 0.8f, scene.w * 0.5f);
            const DocAnim* idle = clipOf("idle");
            if (sheet && idle) drawNovaFrame(*sheet, *idle, frameAt(*idle, t), {scene.x + scene.w * 0.62f - size * 0.5f, scene.center().y - size * 0.5f, size, size});
            const float rad = std::min(scene.h * 0.28f, scene.w * 0.16f);
            const Vec2 c{scene.x + scene.w * 0.22f, scene.center().y};
            const bool logged = g_.online.loggedIn();
            online_.drawAvatar(logged ? jstr(g_.online.profile(), "avatar", "avatar_01") : std::string("avatar_01"), logged ? g_.online.playerId() : std::string(),
                               Rect::fromCenter(c, {rad * 2, rad * 2}));
            drawAvatarFrame(rr, c, rad, g_.cosmeticLook("frame"), rad / 40.0f, t);
            caption = "Profil : la combinaison au côté de l'avatar";
        } else {
            drawScene();
            // Nova runs across, again and again; her trail behind her.
            const float size = std::max(ui_.s(32), std::min(scene.w * 0.36f, (ground - scene.y) * 0.85f));
            const float pass = 2.2f;
            const float p = std::fmod(t, pass) / pass;
            const float left = scene.x - size * 0.4f, right = scene.right() + size * 0.4f;
            const float nx = left + (right - left) * p;
            const bool jump = p > 0.45f && p < 0.62f;
            const float hop = jump ? std::sin((p - 0.45f) / 0.17f * 3.14159f) * size * 0.35f : 0.0f;
            const Vec2 center{nx, ground - size * 0.42f - hop};
            if (p < previewLastP_) previewSamples_.clear();
            previewLastP_ = p;
            for (TrailSample& s : previewSamples_) s.age += dt_;
            previewSamples_.erase(std::remove_if(previewSamples_.begin(), previewSamples_.end(), [](const TrailSample& s) { return s.age > 1.0f; }),
                                  previewSamples_.end());
            previewSamples_.push_back({center, 0.0f, ++previewSeed_});
            const bool ghost = mode == "Fantôme";
            if (d.doc.kind == DocKind::Trail) {
                ImageSprite s{d.tex.get(), d.doc.frameW, d.doc.frameH, d.doc.stripFrames(), d.doc.fps};
                drawImageTrail(rr, s, d.doc.emitter, d.doc.look.a, d.doc.look.b, previewSamples_, false, size / 40.0f, ghost ? 0.45f : 1.0f);
            } else {
                // The trail the player wears follows Nova (the game's own drawing).
                previewTrail_.update(center, dt_, true);
                previewTrail_.draw(rr, g_.cosmeticLook("trail"), false, size / 40.0f, ghost ? 0.45f : 1.0f);
            }
            const DocAnim* clip = clipOf(jump ? (hop > 0 && p < 0.53f ? "jump" : "fall") : "run");
            if (sheet && clip) drawNovaFrame(*sheet, *clip, frameAt(*clip, t), {nx - size * 0.5f, ground - size - hop, size, size}, ghost ? Color{200, 230, 255, 115} : Color::white());
            caption = ghost ? "Fantôme : translucide, comme dans les courses" : d.doc.kind == DocKind::Trail ? "Course · la traînée suit Nova" : "Course · la traînée équipée suit Nova";
        }
    } else if (d.doc.kind == DocKind::Frame) {
        ImageSprite s{d.tex.get(), d.doc.frameW, d.doc.frameH, d.doc.stripFrames(), d.doc.fps};
        rr.fillRoundedRect(scene, ui_.s(10), Color{14, 20, 40, 255});
        const bool logged = g_.online.loggedIn();
        const std::string avatar = logged ? jstr(g_.online.profile(), "avatar", "avatar_01") : std::string("avatar_01");
        const std::string pid = logged ? g_.online.playerId() : std::string();
        if (mode == "Profil") {
            const float k = 1.0f + kAvatarFrameMargin / 40.0f;
            const float rad = std::max(ui_.s(16), std::min(scene.w, scene.h) * 0.42f / k);
            const Vec2 c = scene.center();
            online_.drawAvatar(avatar, pid, Rect::fromCenter(c, {rad * 2, rad * 2}));
            drawImageFrame(rr, s, c, rad, rad / 40.0f, t);
            caption = "Profil : le cadre autour de l'avatar";
        } else {
            // A row as the boards draw it.
            const float rowH = std::min(ui_.s(56), scene.h * 0.4f);
            const Rect row{scene.x + ui_.s(6), scene.center().y - rowH * 0.5f, scene.w - ui_.s(12), rowH};
            rr.fillRoundedRect(row, ui_.s(10), Color{18, 26, 50, 235});
            rr.drawRoundedRect(row, ui_.s(10), kAccent, std::max(1.0f, ui_.s(1.5f)));
            const float avs = row.h - ui_.s(16);
            const Vec2 avc{row.x + ui_.s(40) + avs * 0.5f, row.center().y};
            fitLabel(ui_, "14", {row.x + ui_.s(16), row.center().y}, 15, ui_.s(24), kDim, Align::Center, FontFace::Bold);
            online_.drawAvatar(avatar, pid, Rect::fromCenter(avc, {avs, avs}));
            drawImageFrame(rr, s, avc, avs * 0.5f, ui_.s(0.5f), t);
            const float nx2 = avc.x + avs * 0.5f + ui_.s(kAvatarFrameMargin * 0.5f + 10);
            {
                i18n::Verbatim name;
                fitLabel(ui_, logged ? g_.online.username() : std::string("Votre pseudo"), {nx2, row.center().y}, 15, row.right() - ui_.s(8) - nx2, kText,
                         Align::Left, FontFace::Bold);
            }
            caption = "Classement : le cadre dans une ligne";
        }
    } else {
        // Nova at the exit of a level, the effect played around her.
        drawScene();
        const float cycle = kVictoryFxTime + 0.6f;
        const float tt = std::fmod(t, cycle);
        const float scale = std::max(0.2f, std::min(scene.w / 230.0f, scene.h / 220.0f));
        const float size = 52.0f * scale;
        const Vec2 at{scene.center().x, ground};
        const DocAnim* clip = clipOf(tt < kVictoryFxTime ? "victory" : "idle");
        if (sheet && clip) drawNovaFrame(*sheet, *clip, frameAt(*clip, tt, tt >= kVictoryFxTime), {at.x - size * 0.5f, ground - size, size, size});
        ImageSprite s{d.tex.get(), d.doc.frameW, d.doc.frameH, d.doc.stripFrames(), d.doc.fps};
        drawImageVictory(rr, s, d.doc.emitter, d.doc.look.a, d.doc.look.b, at, tt, false, scale);
        const int tenths = static_cast<int>(std::lround(kVictoryFxTime * 10.0f));
        caption = "Sortie du niveau · " + std::to_string(tenths / 10) + "," + std::to_string(tenths % 10) + " s";
    }
    rr.popClip();
    rr.drawRoundedRect(scene, ui_.s(10), kBorder, std::max(1.0f, ui_.s(1)));
    fitLabel(ui_, caption, {x, r.bottom() - pad - capH * 0.5f}, 13, w, kDim);
    ui_.popId();
}

}  // namespace cosmic::studio
