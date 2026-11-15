#include "ui/screens/ChallengeRewardsUi.h"

#include <algorithm>
#include <cmath>

#include "game/GameServices.h"
#include "ui/screens/CosmeticFx.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

/// Never below 11.5 pixels on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

const Color kGold{255, 214, 90, 255};
const Color kGreen{120, 230, 140, 255};

/// Nova in a suit (a frame of its recoloured sheet), as the Collection draws her.
void drawNova(GameServices& g, const std::string& suitLook, float t, const Rect& dst, Color tint) {
    AnimationSetPtr set = g.app.resources().animation("animations/nova.json");
    if (!set || !set->texture) return;
    const AnimationClip* c = set->clip("idle");
    if (!c || c->frameCount() == 0) return;
    float total = std::max(0.01f, c->totalDuration()), local = std::fmod(t, total), acc = 0.0f;
    int frame = 0;
    for (int i = 0; i < c->frameCount(); ++i) {
        acc += c->durations[static_cast<size_t>(i)];
        if (local < acc) {
            frame = i;
            break;
        }
    }
    TexturePtr tex = set->texture;
    if (!suitLook.empty() && suitLook != "standard")
        if (TexturePtr s = g.app.resources().texture("sprites/player/suits/nova_" + suitLook + ".png")) tex = s;
    Rect src = set->frameRect(*c, frame);
    g.app.renderer().drawTexture(*tex, &src, dst, tint);
}

/// The title's gold chip centred in `box` (its font smaller when the text is long).
void drawTitleChip(UIContext& ui, const Rect& box, const std::string& text, bool on) {
    float fs = fz(ui, std::min(14.0f, box.h / ui.s(1.0f) * 0.42f));
    fs = std::max(ui.fitFont(text, FontFace::Bold, fs, box.w - ui.s(18), 0.8f), 11.5f / ui.s(1.0f));
    float w = std::min(box.w - ui.s(4), ui.text().measure(text, FontFace::Bold, ui.s(fs)).x + ui.s(16));
    Rect r = Rect::fromCenter(box.center(), {w, ui.s(fs) + ui.s(10)});
    Color col = on ? kGold : ui.theme().textDim;
    ui.r().fillRoundedRect(r, r.h * 0.5f, col.withAlpha(28));
    ui.r().drawRoundedRect(r, r.h * 0.5f, col.withAlpha(160), std::max(1.0f, ui.s(1.2f)));
    ui.label(ui.elide(text, FontFace::Bold, fs, r.w - ui.s(10)), r.center(), fs, col, Align::Center, FontFace::Bold, VAlign::Middle);
}

/// The size of a title's chip text in a tile `h` high.
float titleFont(const UIContext& ui, float h) { return fz(ui, std::min(14.0f, h / ui.s(1.0f) * 0.42f)); }

/// A medal of the tier's colour (the mark of a tile), centred on `c`.
void drawTierMark(UIContext& ui, Vec2 c, float radius, int tier) {
    Renderer& r = ui.r();
    Color col = challengeTierColor(tier);
    r.fillCircle(c, radius + std::max(1.0f, ui.s(1.5f)), Color{8, 12, 26, 255});
    r.fillCircle(c, radius, col);
    // A medal: a star struck in it, a darker shade of the tier's colour.
    Color ink{static_cast<uint8_t>(col.r * 0.55f), static_cast<uint8_t>(col.g * 0.55f), static_cast<uint8_t>(col.b * 0.55f), 255};
    Vec2 pts[10];
    for (int i = 0; i < 10; ++i) {
        float a = -1.5707963f + static_cast<float>(i) * 0.6283185f;
        float rr = (i % 2 == 0 ? 0.68f : 0.3f) * radius;
        pts[i] = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
    }
    for (int i = 0; i < 10; ++i) r.fillTriangle(c, pts[i], pts[(i + 1) % 10], ink);
}

/// A green tick in a dark disc (« obtenu »), centred on `c`.
void drawObtainedMark(UIContext& ui, Vec2 c, float radius) {
    Renderer& r = ui.r();
    r.fillCircle(c, radius + std::max(1.0f, ui.s(1.5f)), Color{8, 12, 26, 255});
    r.fillCircle(c, radius, kGreen);
    float k = radius / 7.0f;
    Color ink{10, 40, 18, 255};
    r.drawLine({c.x - 3.6f * k, c.y + 0.2f * k}, {c.x - 1.0f * k, c.y + 2.8f * k}, ink, std::max(1.5f, 1.9f * k));
    r.drawLine({c.x - 1.0f * k, c.y + 2.8f * k}, {c.x + 3.8f * k, c.y - 2.6f * k}, ink, std::max(1.5f, 1.9f * k));
}

}  // namespace

std::string challengeConditionText(const CosmeticItem& it) {
    const ChallengeUnlock& c = it.challenge;
    const std::string n = std::to_string(c.count);
    if (c.streak) {
        if (c.kind == "weekly") return "Réussir le défi de la semaine " + n + " semaines de suite.";
        if (c.kind == "monthly") return "Réussir le défi du mois " + n + " mois de suite.";
        return "Réussir le défi du jour " + n + " jours de suite.";
    }
    std::string what;
    if (c.count <= 1)
        what = c.kind == "weekly" ? "un défi de la semaine" : c.kind == "monthly" ? "un défi du mois" : c.kind == "daily" ? "un défi du jour"
                                                                                                    : "un défi (du jour, de la semaine ou du mois)";
    else
        what = c.kind == "weekly"    ? n + " défis de la semaine"
               : c.kind == "monthly" ? n + " défis du mois"
               : c.kind == "daily"   ? n + " défis du jour"
                                     : n + " défis (du jour, de la semaine ou du mois)";
    std::string tier = c.tier >= 3 ? "en Or" : c.tier == 2 ? "en Argent ou en Or" : "à n'importe quel palier";
    return "Réussir " + what + " " + tier + ".";
}

std::string challengeConditionNote(const CosmeticItem& it) {
    if (it.challenge.streak)
        return "Une partie par jour, à n'importe quel palier, à l'heure : une partie hors délai ne compte pas. Seules les parties vérifiées par "
               "Cosmic Online comptent.";
    return "À l'heure ou hors délai. Seules les parties vérifiées par Cosmic Online comptent.";
}

std::string challengeConditionShort(const CosmeticItem& it) {
    const ChallengeUnlock& c = it.challenge;
    const std::string n = std::to_string(c.count);
    const std::string tier = c.tier >= 3 ? "en Or" : c.tier == 2 ? "en Argent" : "en Bronze";
    if (c.streak) return c.kind == "weekly" ? n + " semaines de suite" : c.kind == "monthly" ? n + " mois de suite" : n + " jours de suite";
    if (c.count <= 1 && c.kind == "weekly") return "Semaine " + tier;
    if (c.count <= 1 && c.kind == "monthly") return "Mois " + tier;
    if (c.count <= 1 && c.kind == "daily") return "Jour " + tier;
    return c.count <= 1 ? "1 défi " + tier : n + " défis " + tier;
}

std::string challengeProgressText(const CosmeticItem& it, const CosmeticProgress& p) {
    auto [have, need] = cosmeticChallengeProgress(it, p);
    std::string v = std::to_string(have) + " / " + std::to_string(need);
    return it.challenge.streak ? "Meilleure série : " + v : "Progression : " + v;
}

int challengeRewardTier(const CosmeticItem& it) { return it.challenge.streak ? 1 : std::clamp(it.challenge.tier, 1, 3); }

Color challengeTierColor(int tier) {
    // As the tiers of the challenge screens.
    if (tier >= 3) return {255, 204, 84, 255};
    if (tier == 2) return {198, 210, 228, 255};
    return {214, 140, 82, 255};
}

void drawCosmeticPreview(GameServices& g, const CosmeticItem& it, const Rect& box, float time, bool unlocked) {
    UIContext& ui = g.ui;
    Renderer& r = ui.r();
    Color tint = unlocked ? Color::white() : Color{255, 255, 255, 110};
    if (it.category == "suit") {
        float s = std::min(box.w, box.h) * 1.15f;
        r.pushClip(box);
        drawNova(g, it.look, time, Rect::fromCenter({box.center().x, box.center().y - s * 0.04f}, {s, s}), tint);
        r.popClip();
    } else if (it.category == "trail") {
        r.pushClip(box);
        drawTrailSample(r, it.look, box, time, unlocked ? 1.0f : 0.4f);
        r.popClip();
    } else if (it.category == "frame") {
        float scale = box.h / 80.0f;
        float rad = std::max(ui.s(5), box.h * 0.5f - kAvatarFrameMargin * scale - ui.s(1));
        Vec2 c{box.center().x, box.center().y + kAvatarFrameMargin * scale * 0.25f};
        r.fillCircle(c, rad, Color{24, 34, 60, 255});
        drawAvatarFrame(r, c, rad, it.look, scale, time);
        if (!unlocked) r.fillRect(box, Color{11, 18, 38, 100});
    } else if (it.category == "title") {
        drawTitleChip(ui, box, it.isDefault ? std::string("Sans titre") : it.look, unlocked);
    } else if (it.category == "victory") {
        r.pushClip(box);
        drawVictoryFx(r, it.look, {box.center().x, box.bottom() - box.h * 0.04f}, std::fmod(time, 1.6f) + 0.3f, false, box.h / 105.0f);
        if (!unlocked) r.fillRect(box, Color{11, 18, 38, 100});
        r.popClip();
    }
}

float ChallengeRewardRow::tileWidth(GameServices& g, const CosmeticItem& it, float h) {
    if (it.category != "title") return h;
    UIContext& ui = g.ui;
    float fs = titleFont(ui, h);
    return std::max(h, ui.text().measure(it.look, FontFace::Bold, ui.s(fs)).x + ui.s(16) + ui.s(14));
}

float ChallengeRewardRow::width(GameServices& g, const std::vector<const CosmeticItem*>& items, float h) {
    float w = 0.0f;
    for (size_t i = 0; i < items.size(); ++i) w += tileWidth(g, *items[i], h) + (i ? g.ui.s(10) : 0.0f);
    return w;
}

const CosmeticItem* ChallengeRewardRow::draw(GameServices& g, const std::string& idPrefix, const std::vector<const CosmeticItem*>& items, float x,
                                             float y, float h, float maxW, const std::string& feature) {
    UIContext& ui = g.ui;
    Renderer& r = ui.r();
    tip_ = nullptr;
    newDue_ = false;
    const CosmeticProgress p = g.cosmeticProgress();
    const CosmeticItem* activated = nullptr;
    const float gap = ui.s(10), right = x + maxW;
    float cx = x;
    for (const CosmeticItem* it : items) {
        const float w = tileWidth(g, *it, h);
        if (cx + w > right + 0.5f) break;  // no room: left out rather than overlapping
        Rect tile{cx, y, w, h};
        const std::string name = idPrefix + it->id;
        bool hovered = false;
        if (ui.hotspot(name, tile, &hovered)) activated = it;
        if (!feature.empty() && ui.newBadge(name, tile, feature, false, true)) newDue_ = true;
        const bool focused = ui.focused(name);
        const bool got = cosmeticUnlocked(*it, p);
        const int tier = challengeRewardTier(*it);
        const Color tc = challengeTierColor(tier);
        r.fillRoundedRect(tile, ui.s(9), got ? Color{14, 34, 30, 245} : Color{11, 18, 38, 245});
        drawCosmeticPreview(g, *it, tile.expanded(-ui.s(5)), ui.time(), got);
        // Locked: how far my verified results go, a thin bar along the bottom.
        if (!got) {
            auto [have, need] = cosmeticChallengeProgress(*it, p);
            if (have > 0 && need > 0) {
                Rect bar{tile.x + ui.s(7), tile.bottom() - ui.s(7), tile.w - ui.s(14), std::max(2.0f, ui.s(3))};
                r.fillRoundedRect(bar, bar.h * 0.5f, Color{40, 52, 80, 255});
                r.fillRoundedRect({bar.x, bar.y, bar.w * static_cast<float>(have) / static_cast<float>(need), bar.h}, bar.h * 0.5f, tc);
            }
        }
        Color border = focused || hovered ? ui.theme().accent : got ? kGreen.withAlpha(210) : tc.withAlphaF(0.55f);
        r.drawRoundedRect(tile, ui.s(9), border, std::max(1.0f, ui.s(focused || hovered ? 2.2f : 1.4f)));
        // The tier's medal on the top right corner, « obtenu » on the bottom right one.
        const float mr = std::max(ui.s(5.5f), h * 0.11f);
        drawTierMark(ui, {tile.right() - mr * 0.55f, tile.y + mr * 0.55f}, mr, tier);
        if (got) drawObtainedMark(ui, {tile.right() - mr * 0.55f, tile.bottom() - mr * 0.55f}, mr);
        if ((ui.usingPointer() && hovered) || (!ui.usingPointer() && focused)) {
            tip_ = it;
            tipTile_ = tile;
        }
        cx += w + gap;
    }
    return activated;
}

void ChallengeRewardRow::drawTooltip(GameServices& g) {
    if (!tip_) return;
    UIContext& ui = g.ui;
    Renderer& r = ui.r();
    const CosmeticItem& it = *tip_;
    const CosmeticProgress p = g.cosmeticProgress();
    const bool got = cosmeticUnlocked(it, p);
    const bool known = p.challenges && p.challenges->known;
    const int tier = challengeRewardTier(it);
    const Color tc = challengeTierColor(tier);
    const float W = ui.screenW(), H = ui.screenH();
    const float w = std::min(ui.s(420), W - ui.s(24));
    const float pad = ui.s(16), inner = w - 2 * pad;
    // The texts, measured first: the box is as high as they need.
    const std::string sub = cosmeticCategorySingular(it.category) + " · récompense des défis";
    const std::string cond = challengeConditionText(it), note = challengeConditionNote(it);
    std::string state;
    Color stateC = ui.theme().text;
    if (got) {
        state = "Obtenu : à équiper dans la Collection";
        stateC = kGreen;
    } else if (!known) {
        state = g.online.loggedIn() ? "Vos résultats n'ont pas encore été lus sur Cosmic Online." : "Connectez-vous à Cosmic Online pour la gagner.";
        stateC = ui.theme().warm;
    } else {
        state = challengeProgressText(it, p);
    }
    const std::string hint = "voir dans la Collection";
    const float fsName = 19, fsSub = fz(ui, 14), fsCond = fz(ui, 16), fsNote = fz(ui, 13.5f), fsState = fz(ui, 15.5f), fsHint = fz(ui, 13.5f);
    const float condH = ui.text().measureWrapped(cond, FontFace::Body, ui.s(fsCond), inner).y;
    const float noteH = ui.text().measureWrapped(note, FontFace::Body, ui.s(fsNote), inner).y;
    const float stateH = ui.text().measureWrapped(state, FontFace::Bold, ui.s(fsState), inner).y;
    const bool bar = !got && known;
    const float hintH = ui.s(26);
    float h = pad + ui.s(26) + ui.s(22) + ui.s(10) + condH + ui.s(6) + noteH + ui.s(12) + stateH + (bar ? ui.s(14) : 0.0f) + ui.s(12) + hintH + pad;
    // Above the tile (its left edge on the tile's) when there is room, else below; always on screen.
    Rect box{tipTile_.x, tipTile_.y - ui.s(12) - h, w, h};
    if (box.y < ui.s(8)) box.y = tipTile_.bottom() + ui.s(12);
    box.y = std::clamp(box.y, ui.s(8), std::max(ui.s(8), H - h - ui.s(8)));
    box.x = std::clamp(box.x, ui.s(8), std::max(ui.s(8), W - w - ui.s(8)));
    r.fillRoundedRect(box.expanded(ui.s(1)), ui.s(13), Color{2, 4, 10, 200});
    r.fillRoundedRect(box, ui.s(12), Color{10, 15, 32, 250});
    r.drawRoundedRect(box, ui.s(12), tc.withAlphaF(0.9f), std::max(1.0f, ui.s(1.6f)));
    float x = box.x + pad, y = box.y + pad;
    // Name (with the tier's medal at the right), category.
    const float mr = ui.s(8);
    drawTierMark(ui, {box.right() - pad - mr, y + ui.s(13)}, mr, tier);
    float nf = std::max(ui.fitFont(it.name, FontFace::Bold, fsName, inner - mr * 2 - ui.s(12), 0.75f), 11.5f / ui.s(1.0f));
    ui.label(ui.elide(it.name, FontFace::Bold, nf, inner - mr * 2 - ui.s(12)), {x, y + ui.s(13)}, nf, ui.theme().text, Align::Left, FontFace::Bold,
             VAlign::Middle);
    y += ui.s(26);
    ui.label(ui.elide(sub, FontFace::Body, fsSub, inner), {x, y + ui.s(11)}, fsSub, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    y += ui.s(22) + ui.s(10);
    ui.labelWrapped(cond, {x, y}, inner, fsCond, tc);
    y += condH + ui.s(6);
    ui.labelWrapped(note, {x, y}, inner, fsNote, ui.theme().textDim);
    y += noteH + ui.s(12);
    ui.labelWrapped(state, {x, y}, inner, fsState, stateC);
    y += stateH;
    if (bar) {
        auto [have, need] = cosmeticChallengeProgress(it, p);
        Rect b{x, y + ui.s(6), inner, ui.s(8)};
        r.fillRoundedRect(b, b.h * 0.5f, Color{30, 40, 66, 255});
        if (need > 0 && have > 0) r.fillRoundedRect({b.x, b.y, b.w * static_cast<float>(have) / static_cast<float>(need), b.h}, b.h * 0.5f, tc);
        y += ui.s(14);
    }
    y += ui.s(12);
    // How to open it: the confirm key of the device in use (a click with the mouse).
    r.fillRect({x, y - ui.s(6), inner, std::max(1.0f, ui.s(1))}, Color{60, 76, 110, 160});
    const float gh = ui.s(22);
    float gw = Glyphs::drawAction(ui, Action::MenuConfirm, {x, y + hintH * 0.5f}, gh);
    ui.label(ui.elide(hint, FontFace::Body, fsHint, inner - gw - ui.s(8)), {x + gw + ui.s(8), y + hintH * 0.5f}, fsHint, ui.theme().textDim, Align::Left,
             FontFace::Body, VAlign::Middle);
}

}  // namespace cosmic
