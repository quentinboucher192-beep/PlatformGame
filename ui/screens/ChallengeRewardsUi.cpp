#include "ui/screens/ChallengeRewardsUi.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "game/GameServices.h"
#include "ui/screens/CosmeticFx.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

/// Never below 11.5 pixels on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

const Color kGold{255, 214, 90, 255};
const Color kGreen{120, 230, 140, 255};

/// Nova in a suit (a frame of its recoloured sheet), as the Collection draws her (a suit created
/// on the server: from `colors` when given, else from the colours this game knows).
void drawNova(GameServices& g, const std::string& suitLook, float t, const Rect& dst, Color tint, const SuitColors* colors = nullptr) {
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
    if (TexturePtr s = g.suitSheet(suitLook, colors)) tex = s;
    Rect src = set->frameRect(*c, frame);
    g.app.renderer().drawTexture(*tex, &src, dst, tint);
}

/// The title's chip centred in `box`, gold (2.43: a server title in its colour; its font smaller when the text is long).
void drawTitleChip(UIContext& ui, const Rect& box, const std::string& text, bool on, Color gold = kGold) {
    float fs = fz(ui, std::min(14.0f, box.h / ui.s(1.0f) * 0.42f));
    fs = std::max(ui.fitFont(text, FontFace::Bold, fs, box.w - ui.s(18), 0.8f), 11.5f / ui.s(1.0f));
    float w = std::min(box.w - ui.s(4), ui.text().measure(text, FontFace::Bold, ui.s(fs)).x + ui.s(16));
    Rect r = Rect::fromCenter(box.center(), {w, ui.s(fs) + ui.s(10)});
    Color col = on ? gold : ui.theme().textDim;
    ui.r().fillRoundedRect(r, r.h * 0.5f, col.withAlpha(28));
    ui.r().drawRoundedRect(r, r.h * 0.5f, col.withAlpha(160), std::max(1.0f, ui.s(1.2f)));
    ui.label(ui.elide(text, FontFace::Bold, fs, r.w - ui.s(10)), r.center(), fs, col, Align::Center, FontFace::Bold, VAlign::Middle);
}

const Color kCyan{110, 220, 255, 255};

/// The colour of a reward's condition (its mark, its tooltip's border).
Color prizeColor(const ChallengePrize& p) {
    if (p.condition == "tier") return challengeTierColor(p.value);
    if (p.condition == "top") return kGold;
    return kCyan;
}

/// « Top N » in a small gold badge at the top right corner of `tile` (2.43).
void drawTopMark(UIContext& ui, const Rect& tile, int n) {
    const std::string t = "Top " + std::to_string(n);
    const float fs = fz(ui, 11.5f);
    const float w = ui.text().measure(t, FontFace::Bold, ui.s(fs)).x + ui.s(10), h = ui.s(fs) + ui.s(6);
    Rect b{tile.right() - w - ui.s(3), tile.y + ui.s(3), w, h};
    ui.r().fillRoundedRect(b.expanded(std::max(1.0f, ui.s(1.5f))), h * 0.5f, Color{8, 12, 26, 255});
    ui.r().fillRoundedRect(b, h * 0.5f, kGold);
    ui.label(t, b.center(), fs, Color{60, 40, 0, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
}

/// A finish flag in a disc (« avoir fini le niveau »), centred on `c`.
void drawFinishMark(UIContext& ui, Vec2 c, float radius) {
    Renderer& r = ui.r();
    r.fillCircle(c, radius + std::max(1.0f, ui.s(1.5f)), Color{8, 12, 26, 255});
    r.fillCircle(c, radius, kCyan);
    const float k = radius / 7.0f;
    Color ink{8, 40, 56, 255};
    r.drawLine({c.x - 2.6f * k, c.y - 4.0f * k}, {c.x - 2.6f * k, c.y + 4.2f * k}, ink, std::max(1.2f, 1.4f * k));
    r.fillTriangle({c.x - 2.0f * k, c.y - 4.0f * k}, {c.x + 4.0f * k, c.y - 1.9f * k}, {c.x - 2.0f * k, c.y + 0.4f * k}, ink);
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

Color titleColor(const std::string& color) {
    if (color == "silver") return {206, 216, 232, 255};
    if (color == "cyan") return {96, 226, 240, 255};
    if (color == "violet") return {196, 146, 255, 255};
    return kGold;
}

std::string prizeConditionShort(const ChallengePrize& p) {
    if (p.condition == "tier") return std::string(p.value >= 3 ? "Or" : p.value == 2 ? "Argent" : "Bronze") + " atteint";
    if (p.condition == "top") return "Top " + std::to_string(p.value) + " final";
    return "Niveau fini";
}

std::string conditionShort(const std::string& condition, int value) {
    if (condition != "tier" && condition != "top" && condition != "finished") return {};
    ChallengePrize p;
    p.condition = condition;
    p.value = value;
    return prizeConditionShort(p);
}

std::string prizeConditionText(const ChallengePrize& p) {
    if (p.condition == "tier") {
        if (p.value >= 3) return "Atteindre le palier Or sur ce défi.";
        if (p.value == 2) return "Atteindre le palier Argent (ou Or) sur ce défi.";
        return "Atteindre un palier (Bronze, Argent ou Or) sur ce défi.";
    }
    if (p.condition == "top")
        return p.value <= 1 ? std::string("Finir premier du classement final de ce défi.")
                            : "Finir dans les " + std::to_string(p.value) + " premiers du classement final de ce défi.";
    return "Finir le niveau de ce défi pendant qu'il a lieu.";
}

std::string wonOriginText(const WonReward& w) {
    if (w.challengeKind == "weekly") return "Défi de la semaine " + std::to_string(w.period.size() > 6 ? std::atoi(w.period.c_str() + 6) : 0);
    if (w.challengeKind == "monthly") {
        static const char* const kMonths[12] = {"janvier", "février", "mars",      "avril",   "mai",      "juin",
                                                "juillet", "août",    "septembre", "octobre", "novembre", "décembre"};
        int yy = 0, mm = 0;
        if (std::sscanf(w.period.c_str(), "%4d-%2d", &yy, &mm) == 2 && mm >= 1 && mm <= 12)
            return std::string("Défi du mois de ") + kMonths[mm - 1] + " " + std::to_string(yy);
        return "Défi du mois";
    }
    const std::string& d = w.date.size() >= 10 ? w.date : w.period;
    return d.size() >= 10 ? "Défi du jour du " + d.substr(8, 2) + "/" + d.substr(5, 2) : std::string("Défi du jour");
}

std::optional<CosmeticItem> prizeLook(GameServices& g, const ChallengePrize& p) {
    if (p.title()) {
        CosmeticItem it;
        it.id = p.ref;
        it.category = "title";
        it.name = p.name;
        it.look = p.name;
        it.color = p.color;
        it.description = "Un titre créé sur Cosmic Online.";
        return it;
    }
    if (p.suit()) {
        if (!p.suitColors) return std::nullopt;
        CosmeticItem it;
        it.id = p.ref;
        it.category = "suit";
        it.name = p.name;
        it.look = p.ref;
        it.suitColors = p.suitColors;
        it.description = "Une combinaison créée sur Cosmic Online.";
        return it;
    }
    if (p.creation()) {
        // 2.59: a trail, a frame or a victory effect created on the server: drawn from its look.
        if (!p.creationLook) return std::nullopt;
        CosmeticItem it;
        it.id = p.ref;
        it.category = p.kind;
        it.name = p.name;
        it.look = p.ref;
        it.creationLook = p.creationLook;
        it.description = p.kind == "trail"   ? "Une traînée créée sur Cosmic Online."
                         : p.kind == "frame" ? "Un cadre d'avatar créé sur Cosmic Online."
                                             : "Un effet de victoire créé sur Cosmic Online.";
        return it;
    }
    const CosmeticItem* it = g.cosmetics.find(p.ref);
    if (!it || it->isDefault) return std::nullopt;
    return *it;
}

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
    // 2.59: a creation of the server carries its look (else found by its reference).
    const CreationLook* made = it.creationLook ? &*it.creationLook : nullptr;
    if (it.category == "suit") {
        float s = std::min(box.w, box.h) * 1.15f;
        r.pushClip(box);
        drawNova(g, it.look, time, Rect::fromCenter({box.center().x, box.center().y - s * 0.04f}, {s, s}), tint,
                 it.suitColors ? &*it.suitColors : nullptr);
        r.popClip();
    } else if (it.category == "trail") {
        r.pushClip(box);
        drawTrailSample(r, it.look, box, time, unlocked ? 1.0f : 0.4f, made);
        r.popClip();
    } else if (it.category == "frame") {
        float scale = box.h / 80.0f;
        float rad = std::max(ui.s(5), box.h * 0.5f - kAvatarFrameMargin * scale - ui.s(1));
        Vec2 c{box.center().x, box.center().y + kAvatarFrameMargin * scale * 0.25f};
        r.fillCircle(c, rad, Color{24, 34, 60, 255});
        drawAvatarFrame(r, c, rad, it.look, scale, time, made);
        if (!unlocked) r.fillRect(box, Color{11, 18, 38, 100});
    } else if (it.category == "title") {
        drawTitleChip(ui, box, it.isDefault ? std::string("Sans titre") : it.look, unlocked, it.color.empty() ? kGold : titleColor(it.color));
    } else if (it.category == "victory") {
        r.pushClip(box);
        drawVictoryFx(r, it.look, {box.center().x, box.bottom() - box.h * 0.04f}, std::fmod(time, 1.6f) + 0.3f, false, box.h / 105.0f, made);
        if (!unlocked) r.fillRect(box, Color{11, 18, 38, 100});
        r.popClip();
    }
}

// ------------------------------------------------------------------ exclusive items (2.60)

bool exclusiveItem(const CosmeticItem& it) { return it.serverMade() || it.challengeReward(); }

Color exclusiveColor() { return {255, 204, 112, 255}; }

void drawExclusiveAura(UIContext& ui, const Rect& box, float radius, float spread) {
    if (spread < 1.0f || box.w <= 1.0f || box.h <= 1.0f) return;
    const float t = ui.time();
    // A slow breath (about 2.7 s), and a brighter part that goes round the thumbnail.
    const float pulse = 0.62f + 0.38f * std::sin(t * 2.3f);
    const Color c = exclusiveColor();
    radius = std::clamp(radius, 0.0f, std::min(box.w, box.h) * 0.5f);
    constexpr int kSeg = 6;
    const Vec2 centers[4] = {{box.right() - radius, box.y + radius},
                             {box.right() - radius, box.bottom() - radius},
                             {box.x + radius, box.bottom() - radius},
                             {box.x + radius, box.y + radius}};
    const float starts[4] = {-kPi / 2, 0, kPi / 2, kPi};
    // Kept from a call to the next (the interface draws on one thread): no allocation per frame.
    static std::vector<SDL_Vertex> v;
    static std::vector<int> idx;
    v.clear();
    idx.clear();
    auto vtx = [](Vec2 p, Color col) {
        SDL_Vertex x{};
        x.position = {p.x, p.y};
        x.color = {col.r / 255.0f, col.g / 255.0f, col.b / 255.0f, col.a / 255.0f};
        return x;
    };
    const float total = 4.0f * (kSeg + 1);
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i <= kSeg; ++i) {
            const float a = starts[k] + (kPi / 2) * static_cast<float>(i) / kSeg;
            const Vec2 dir{std::cos(a), std::sin(a)};
            const float u = static_cast<float>(k * (kSeg + 1) + i) / total;
            const float shimmer = 0.7f + 0.3f * std::sin((u - t * 0.18f) * 2.0f * kPi);
            const float alpha = std::clamp(150.0f * pulse * shimmer, 0.0f, 255.0f);
            // The glow fades from the thumbnail's edge to `spread` beyond it (never further).
            v.push_back(vtx(centers[k] + dir * radius, c.withAlpha(static_cast<uint8_t>(alpha))));
            v.push_back(vtx(centers[k] + dir * (radius + spread), c.withAlpha(0)));
        }
    const int n = static_cast<int>(v.size() / 2);
    for (int i = 0; i < n; ++i) {
        const int a = i * 2, b = ((i + 1) % n) * 2;
        idx.insert(idx.end(), {a, b, b + 1, a, b + 1, a + 1});
    }
    ui.r().drawGeometry(nullptr, v, idx);
    // Its edge, crisp, on the thumbnail's border.
    const float line = std::max(1.0f, ui.s(1.2f));
    ui.r().drawRoundedRect(box.expanded(std::min(spread, line)), radius + std::min(spread, line), c.withAlphaF(0.32f + 0.4f * pulse), line);
}

std::string challengeOriginText(const std::string& kind, const std::string& period, const std::string& date) {
    // « 10/11/2026 » from the first day of the period ("2026-11-10").
    auto day = [](const std::string& d) {
        int yy = 0, mm = 0, dd = 0;
        if (d.size() < 10 || std::sscanf(d.c_str(), "%4d-%2d-%2d", &yy, &mm, &dd) != 3 || mm < 1 || mm > 12 || dd < 1 || dd > 31) return std::string();
        char b[16];
        std::snprintf(b, sizeof b, "%02d/%02d/%04d", dd, mm, yy);
        return std::string(b);
    };
    if (kind == "monthly") {
        static const char* const kMonths[12] = {"janvier", "février", "mars",      "avril",   "mai",      "juin",
                                                "juillet", "août",    "septembre", "octobre", "novembre", "décembre"};
        int yy = 0, mm = 0;
        const std::string& m = period.size() >= 7 ? period : date;
        if (std::sscanf(m.c_str(), "%4d-%2d", &yy, &mm) != 2 || mm < 1 || mm > 12) return {};
        // « du mois d'avril, d'août, d'octobre »: elided before a vowel.
        const std::string month = kMonths[mm - 1];
        const bool vowel = month[0] == 'a' || month[0] == 'o';
        const std::string year = std::to_string(yy);
        if (vowel) return "défi du mois d'" + month + " " + year;
        return "défi du mois de " + month + " " + year;
    }
    if (kind == "weekly") {
        const std::string d = day(date);
        return d.empty() ? std::string() : "défi de la semaine du " + d;
    }
    if (kind == "daily") {
        const std::string d = day(date.size() >= 10 ? date : period);
        return d.empty() ? std::string() : "défi du jour du " + d;
    }
    return {};
}

std::string exclusiveOriginText(const std::string& kind, const std::string& period, const std::string& date, const std::string& condition, int value) {
    const std::string from = challengeOriginText(kind, period, date);
    if (from.empty()) return "Exclusif aux défis";
    const std::string cond = conditionShort(condition, value);
    return "Exclusif : " + from + (cond.empty() ? std::string() : " · " + cond);
}

std::string exclusiveText(GameServices& g, const CosmeticItem& it) {
    const WonRewards& won = g.challenges.wonRewards();
    if (const WonReward* w = won.origin(it.id)) return exclusiveOriginText(w->challengeKind, w->period, w->date, w->condition, w->value);
    if (it.challengeReward()) return "Exclusif aux défis : " + challengeConditionText(it);
    return "Exclusif aux défis";
}

std::string reserveText() { return "Réserve : pas encore donné en récompense"; }

std::string adminCreationTip(const Json& c, bool* reserve) {
    if (reserve) *reserve = false;
    if (const Json& g = jfield(c, "given_by", Json()); g.is_object())
        return exclusiveOriginText(jstr(g, "kind"), jstr(g, "period"), jstr(g, "date"), jstr(g, "condition"), jget<int>(g, "value", 0));
    if (jget<int>(c, "uses", 0) > 0 || jget<int>(c, "owners", 0) > 0) return "Exclusif aux défis";
    if (reserve) *reserve = true;
    return reserveText();
}

bool ExclusiveTip::wanted(const UIContext& ui, bool hovered, bool focused) { return ui.usingPointer() ? hovered : focused; }

void ExclusiveTip::offer(const Rect& tile, std::string name, std::string text, bool reserve) {
    tile_ = tile;
    name_ = std::move(name);
    text_ = std::move(text);
    reserve_ = reserve;
}

void ExclusiveTip::draw(GameServices& g) {
    if (text_.empty()) return;
    UIContext& ui = g.ui;
    Renderer& r = ui.r();
    const float W = ui.screenW(), H = ui.screenH();
    const float pad = ui.s(14);
    const float fsName = 17, fsText = fz(ui, 15);
    // As wide as its texts need (at most 380, never wider than the screen), then as high.
    const float maxInner = std::min(ui.s(380), W - ui.s(24)) - 2 * pad;
    const float nameW = ui.text().measure(name_, FontFace::Bold, ui.s(fsName)).x;
    const float textW = ui.text().measure(text_, FontFace::Body, ui.s(fsText)).x;
    const float inner = std::max(ui.s(120), std::min(maxInner, std::max(nameW, textW) + ui.s(2)));
    const float textH = ui.text().measureWrapped(text_, FontFace::Body, ui.s(fsText), inner).y;
    const float nameH = name_.empty() ? 0.0f : ui.s(26);
    const float w = inner + 2 * pad, h = pad + nameH + textH + pad;
    // Above the tile (its left edge on the tile's) when there is room, else below; always on screen.
    Rect box{tile_.x, tile_.y - ui.s(10) - h, w, h};
    if (box.y < ui.s(8)) box.y = tile_.bottom() + ui.s(10);
    box.y = std::clamp(box.y, ui.s(8), std::max(ui.s(8), H - h - ui.s(8)));
    box.x = std::clamp(box.x, ui.s(8), std::max(ui.s(8), W - w - ui.s(8)));
    const Color c = reserve_ ? ui.theme().textDim : exclusiveColor();
    r.fillRoundedRect(box.expanded(ui.s(1)), ui.s(12), Color{2, 4, 10, 200});
    r.fillRoundedRect(box, ui.s(11), Color{10, 15, 32, 250});
    r.drawRoundedRect(box, ui.s(11), c.withAlphaF(0.9f), std::max(1.0f, ui.s(1.6f)));
    float y = box.y + pad;
    if (!name_.empty()) {
        const float nf = std::max(ui.fitFont(name_, FontFace::Bold, fsName, inner, 0.75f), 11.5f / ui.s(1.0f));
        ui.label(ui.elide(name_, FontFace::Bold, nf, inner), {box.x + pad, y + ui.s(11)}, nf, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        y += nameH;
    }
    ui.labelWrapped(text_, {box.x + pad, y}, inner, fsText, reserve_ ? ui.theme().text : c);
}

float ChallengeRewardRow::tileWidth(GameServices& g, const CosmeticItem& it, float h) {
    if (it.category != "title") return h;
    UIContext& ui = g.ui;
    float fs = titleFont(ui, h);
    return std::max(h, ui.text().measure(it.look, FontFace::Bold, ui.s(fs)).x + ui.s(16) + ui.s(14));
}

void ChallengeRewardRow::setPrizes(GameServices& g, const DailyChallenge* c) {
    prizes_.clear();
    prizeChallenge_.clear();
    if (!c) return;
    prizeChallenge_ = c->id;
    prizeKind_ = challengeKindId(c->kind);
    prizePeriod_ = c->period;
    prizeDate_ = c->date;
    prizeEndsMs_ = c->endsAtMs;
    const WonRewards& won = g.challenges.wonRewards();
    for (const ChallengePrize& p : c->prizes)
        if (auto look = prizeLook(g, p)) {
            // 2.60: exclusive (a creation of the server, a reward of the challenges of the catalogue): this challenge, its condition.
            const bool ex = exclusiveItem(*look);
            prizes_.push_back({std::move(*look), p, won.wonAt(c->id, p.ref), ex});
        }
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
    tipPrize_ = -1;
    newDue_ = false;
    const CosmeticProgress p = g.cosmeticProgress();
    const CosmeticItem* activated = nullptr;
    const float gap = ui.s(10), right = x + maxW;
    float cx = x;
    bool full = false;
    // 2.43: the rewards of this challenge first.
    for (size_t k = 0; k < prizes_.size(); ++k) {
        const Prize& pz = prizes_[k];
        const float w = tileWidth(g, pz.item, h);
        if (cx + w > right + 0.5f) {
            full = true;  // no room: left out rather than overlapping
            break;
        }
        Rect tile{cx, y, w, h};
        const std::string name = idPrefix + "prize" + std::to_string(k);
        bool hovered = false;
        if (ui.hotspot(name, tile, &hovered)) {
            // A catalogue item opens in the Collection; a server title or suit once won.
            if (const CosmeticItem* it = g.cosmetics.find(pz.prize.ref); it && !pz.prize.serverMade()) activated = it;
            else if (const CosmeticItem* t = p.won ? p.won->item(pz.prize.ref) : nullptr) activated = t;
        }
        if (ui.newBadge(name, tile, "challenges.prizes", false, true)) newDue_ = true;
        const bool focused = ui.focused(name);
        const Color tc = prizeColor(pz.prize);
        r.fillRoundedRect(tile, ui.s(9), pz.won ? Color{14, 34, 30, 245} : Color{11, 18, 38, 245});
        // 2.60: an exclusive item's aura, between its preview and the tile's border (inside the tile).
        if (pz.exclusive) drawExclusiveAura(ui, tile.expanded(-ui.s(5)), ui.s(6), ui.s(4));
        drawCosmeticPreview(g, pz.item, tile.expanded(-ui.s(5)), ui.time(), pz.won || pz.prize.serverMade());
        Color border = focused || hovered ? ui.theme().accent : pz.won ? kGreen.withAlpha(210) : tc.withAlphaF(0.75f);
        r.drawRoundedRect(tile, ui.s(9), border, std::max(1.0f, ui.s(focused || hovered ? 2.2f : 1.4f)));
        // The mark of its condition on the top right corner, « obtenu » on the bottom right one.
        const float mr = std::max(ui.s(5.5f), h * 0.11f);
        if (pz.prize.condition == "tier") drawTierMark(ui, {tile.right() - mr * 0.55f, tile.y + mr * 0.55f}, mr, pz.prize.value);
        else if (pz.prize.condition == "top") drawTopMark(ui, tile, pz.prize.value);
        else drawFinishMark(ui, {tile.right() - mr * 0.55f, tile.y + mr * 0.55f}, mr);
        if (pz.won) drawObtainedMark(ui, {tile.right() - mr * 0.55f, tile.bottom() - mr * 0.55f}, mr);
        if ((ui.usingPointer() && hovered) || (!ui.usingPointer() && focused)) {
            tipPrize_ = static_cast<int>(k);
            tipTile_ = tile;
        }
        cx += w + gap;
    }
    for (const CosmeticItem* it : items) {
        const float w = tileWidth(g, *it, h);
        if (full || cx + w > right + 0.5f) break;  // no room: left out rather than overlapping
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
        // 2.60: a reward of the challenges is exclusive: its aura, inside the tile.
        if (exclusiveItem(*it)) drawExclusiveAura(ui, tile.expanded(-ui.s(5)), ui.s(6), ui.s(4));
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
    if (tipPrize_ >= 0 && tipPrize_ < static_cast<int>(prizes_.size())) return drawPrizeTooltip(g, prizes_[static_cast<size_t>(tipPrize_)]);
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
    // 2.60: exclusive to the challenges, and (once won at one) where: « Exclusif : défi du jour du… ».
    const WonReward* origin = p.won ? p.won->origin(it.id) : nullptr;
    const std::string excl = origin ? exclusiveOriginText(origin->challengeKind, origin->period, origin->date, origin->condition, origin->value)
                                    : std::string("Exclusif aux défis");
    const float fsExcl = fz(ui, 14.5f);
    const float exclH = ui.text().measureWrapped(excl, FontFace::Body, ui.s(fsExcl), inner).y;
    const float condH = ui.text().measureWrapped(cond, FontFace::Body, ui.s(fsCond), inner).y;
    const float noteH = ui.text().measureWrapped(note, FontFace::Body, ui.s(fsNote), inner).y;
    const float stateH = ui.text().measureWrapped(state, FontFace::Bold, ui.s(fsState), inner).y;
    const bool bar = !got && known;
    const float hintH = ui.s(26);
    float h = pad + ui.s(26) + ui.s(22) + ui.s(6) + exclH + ui.s(10) + condH + ui.s(6) + noteH + ui.s(12) + stateH + (bar ? ui.s(14) : 0.0f) + ui.s(12) +
              hintH + pad;
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
    y += ui.s(22) + ui.s(6);
    ui.labelWrapped(excl, {x, y}, inner, fsExcl, exclusiveColor());
    y += exclH + ui.s(10);
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

namespace cosmic {

void ChallengeRewardRow::drawPrizeTooltip(GameServices& g, const Prize& pz) {
    UIContext& ui = g.ui;
    Renderer& r = ui.r();
    const CosmeticItem& it = pz.item;
    const Color tc = prizeColor(pz.prize);
    const float W = ui.screenW(), H = ui.screenH();
    const float w = std::min(ui.s(420), W - ui.s(24));
    const float pad = ui.s(16), inner = w - 2 * pad;
    // The texts, measured first: the box is as high as they need.
    // Agreed with the category's noun: « Titre créé », « Combinaison créée », « Traînée créée »...
    const bool feminine = pz.prize.suit() || pz.prize.kind == "trail";
    const std::string sub = cosmeticCategorySingular(it.category) +
                            (!pz.prize.serverMade() ? "" : feminine ? " créée sur Cosmic Online" : " créé sur Cosmic Online") +
                            " · récompense de ce défi";
    const std::string cond = prizeConditionText(pz.prize);
    const std::string note = "Accordée par Cosmic Online à la fin du défi, d'après les parties vérifiées reçues à l'heure. Jamais retirée.";
    const int64_t now = g.challenges.nowMs();
    const WonRewards& won = g.challenges.wonRewards();
    std::string state;
    Color stateC = ui.theme().text;
    if (pz.won) {
        state = "Gagnée : à équiper dans la Collection";
        stateC = kGreen;
    } else if (!g.online.loggedIn()) {
        state = "Connectez-vous à Cosmic Online pour la gagner.";
        stateC = ui.theme().warm;
    } else if (now < prizeEndsMs_) {
        state = "À gagner jusqu'à la fin du défi.";
    } else if (now < prizeEndsMs_ + 2LL * 3600 * 1000 || !won.known) {
        state = "Défi terminé : les récompenses sont accordées dans l'heure qui suit.";
        stateC = ui.theme().warm;
    } else {
        state = "Pas gagnée sur ce défi.";
        stateC = ui.theme().textDim;
    }
    // Opened in the Collection: an item of the catalogue, a server title or suit once won.
    const bool opens = !pz.prize.serverMade() || pz.won;
    const std::string hint = "voir dans la Collection";
    const float fsName = 19, fsSub = fz(ui, 14), fsCond = fz(ui, 16), fsNote = fz(ui, 13.5f), fsState = fz(ui, 15.5f), fsHint = fz(ui, 13.5f);
    // 2.60: an exclusive item: « Exclusif : défi du jour du 10/11/2026 · Or atteint ».
    const float fsExcl = fz(ui, 14.5f);
    const std::string excl = pz.exclusive ? exclusiveOriginText(prizeKind_, prizePeriod_, prizeDate_, pz.prize.condition, pz.prize.value) : std::string();
    const float exclH = excl.empty() ? 0.0f : ui.text().measureWrapped(excl, FontFace::Body, ui.s(fsExcl), inner).y + ui.s(6);
    const float condH = ui.text().measureWrapped(cond, FontFace::Body, ui.s(fsCond), inner).y;
    const float noteH = ui.text().measureWrapped(note, FontFace::Body, ui.s(fsNote), inner).y;
    const float stateH = ui.text().measureWrapped(state, FontFace::Bold, ui.s(fsState), inner).y;
    const float hintH = opens ? ui.s(26) : 0.0f;
    float h = pad + ui.s(26) + ui.s(22) + exclH + ui.s(10) + condH + ui.s(6) + noteH + ui.s(12) + stateH + (opens ? ui.s(12) + hintH : 0.0f) + pad;
    Rect box{tipTile_.x, tipTile_.y - ui.s(12) - h, w, h};
    if (box.y < ui.s(8)) box.y = tipTile_.bottom() + ui.s(12);
    box.y = std::clamp(box.y, ui.s(8), std::max(ui.s(8), H - h - ui.s(8)));
    box.x = std::clamp(box.x, ui.s(8), std::max(ui.s(8), W - w - ui.s(8)));
    r.fillRoundedRect(box.expanded(ui.s(1)), ui.s(13), Color{2, 4, 10, 200});
    r.fillRoundedRect(box, ui.s(12), Color{10, 15, 32, 250});
    r.drawRoundedRect(box, ui.s(12), tc.withAlphaF(0.9f), std::max(1.0f, ui.s(1.6f)));
    float x = box.x + pad, y = box.y + pad;
    // Name (a title in its colour), then what it is.
    const Color nameC = it.category == "title" && !it.color.empty() ? titleColor(it.color) : ui.theme().text;
    float nf = std::max(ui.fitFont(it.name, FontFace::Bold, fsName, inner, 0.75f), 11.5f / ui.s(1.0f));
    ui.label(ui.elide(it.name, FontFace::Bold, nf, inner), {x, y + ui.s(13)}, nf, nameC, Align::Left, FontFace::Bold, VAlign::Middle);
    y += ui.s(26);
    ui.label(ui.elide(sub, FontFace::Body, fsSub, inner), {x, y + ui.s(11)}, fsSub, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    y += ui.s(22);
    if (!excl.empty()) {
        y += ui.s(6);
        ui.labelWrapped(excl, {x, y}, inner, fsExcl, exclusiveColor());
        y += exclH - ui.s(6);
    }
    y += ui.s(10);
    ui.labelWrapped(cond, {x, y}, inner, fsCond, tc);
    y += condH + ui.s(6);
    ui.labelWrapped(note, {x, y}, inner, fsNote, ui.theme().textDim);
    y += noteH + ui.s(12);
    ui.labelWrapped(state, {x, y}, inner, fsState, stateC);
    y += stateH;
    if (!opens) return;
    y += ui.s(12);
    r.fillRect({x, y - ui.s(6), inner, std::max(1.0f, ui.s(1))}, Color{60, 76, 110, 160});
    const float gh = ui.s(22);
    float gw = Glyphs::drawAction(ui, Action::MenuConfirm, {x, y + hintH * 0.5f}, gh);
    ui.label(ui.elide(hint, FontFace::Body, fsHint, inner - gw - ui.s(8)), {x + gw + ui.s(8), y + hintH * 0.5f}, fsHint, ui.theme().textDim, Align::Left,
             FontFace::Body, VAlign::Middle);
}

}  // namespace cosmic
