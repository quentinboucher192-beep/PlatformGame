// Drawing helpers of the challenges shared by « Défis » and ADMINISTRATION › Calendrier des
// défis (see ChallengeUi.h).

#include "ui/screens/ChallengeUi.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

#include "engine/core/FileUtil.h"
#include "game/GameServices.h"
#include "ui/screens/Thumbnail.h"

namespace cosmic::challenge_ui {

float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

float fit(UIContext& ui, const std::string& t, FontFace face, float size, float room, float minScale) {
    return std::max(ui.fitFont(t, face, fz(ui, size), room, minScale), 11.5f / ui.s(1.0f));
}

Vec2 fitLabel(UIContext& ui, const std::string& t, Vec2 pos, float size, float room, Color c, Align a, FontFace face, VAlign va, float minScale) {
    if (room <= ui.s(8)) return {};
    float f = fit(ui, t, face, size, room, minScale);
    return ui.label(ui.elide(t, face, f, room), pos, f, c, a, face, va);
}

Color tierColor(ChallengeTier t) {
    switch (t) {
        case ChallengeTier::Bronze: return {214, 140, 82, 255};
        case ChallengeTier::Silver: return {198, 210, 228, 255};
        case ChallengeTier::Gold: return {255, 204, 84, 255};
        case ChallengeTier::None: break;
    }
    return {140, 160, 185, 255};
}

float chip(UIContext& ui, const std::string& text, float x, float cy, float h, Color c, float maxW, bool filled) {
    float fs = std::max(h / ui.s(1.0f) * 0.5f, 11.5f / ui.s(1.0f));
    float tw = ui.text().measure(text, FontFace::Bold, ui.s(fs)).x;
    float w = std::min(maxW, tw + h);
    Rect r{x, cy - h * 0.5f, w, h};
    ui.r().fillRoundedRect(r, h * 0.5f, filled ? c.withAlphaF(0.9f) : c.withAlphaF(0.16f));
    ui.r().drawRoundedRect(r, h * 0.5f, c.withAlphaF(0.85f), std::max(1.0f, ui.s(1.2f)));
    ui.label(ui.elide(text, FontFace::Bold, fs, w - h * 0.6f), r.center(), fs, filled ? Color{10, 14, 28, 255} : c, Align::Center, FontFace::Bold,
             VAlign::Middle);
    return w;
}

float chipWidth(UIContext& ui, const std::string& text, float h) {
    float fs = std::max(h / ui.s(1.0f) * 0.5f, 11.5f / ui.s(1.0f));
    return ui.text().measure(text, FontFace::Bold, ui.s(fs)).x + h;
}

MedalPlan planMedals(float room, float h, const std::array<float, 3>& numW) {
    MedalPlan plan;
    if (room <= 0.0f || h <= 0.0f) return plan;
    auto lay = [&](MedalFit fit, std::array<bool, 3> shown, float extra, float gap) {
        MedalPlan p;
        p.fit = fit;
        float x = 0.0f;
        for (int i = 0; i < 3; ++i) {
            if (!shown[i]) continue;
            if (x > 0.0f) x += gap;
            p.x[i] = x;
            p.w[i] = numW[i] + extra;
            x += p.w[i];
        }
        p.width = x;
        return p;
    };
    // Three pills: padding, the tier's disk, a gap, its number, padding; then the numbers alone
    // (room for the outline of mine around each), then the Or one alone.
    const MedalPlan pills = lay(MedalFit::Pills, {true, true, true}, 2 * MedalPlan::pad(h) + MedalPlan::disk(h) + MedalPlan::inner(h), h * 0.22f);
    if (pills.width <= room) return pills;
    const MedalPlan numbers = lay(MedalFit::Numbers, {true, true, true}, h * 0.36f, h * 0.16f);
    if (numbers.width <= room) return numbers;
    const MedalPlan gold = lay(MedalFit::GoldOnly, {false, false, true}, h * 0.36f, 0.0f);
    if (gold.width <= room) return gold;
    return plan;
}

float medalFont(const UIContext& ui, float h) { return std::max(h / ui.s(1.0f) * 0.55f, 11.5f / ui.s(1.0f)); }

std::array<float, 3> medalNumberWidths(UIContext& ui, const ChallengeTierCounts& t, float h) {
    const float fs = ui.s(medalFont(ui, h));
    return {ui.text().measure(std::to_string(t.bronze), FontFace::Bold, fs).x, ui.text().measure(std::to_string(t.silver), FontFace::Bold, fs).x,
            ui.text().measure(std::to_string(t.gold), FontFace::Bold, fs).x};
}

void drawMedals(UIContext& ui, const MedalPlan& plan, const ChallengeTierCounts& t, float x, float cy, float h, ChallengeTier mine) {
    if (plan.fit == MedalFit::None) return;
    static const ChallengeTier kTiers[3] = {ChallengeTier::Bronze, ChallengeTier::Silver, ChallengeTier::Gold};
    const float fs = medalFont(ui, h);
    for (int i = 0; i < 3; ++i) {
        if (plan.w[i] <= 0.0f) continue;
        const Color c = tierColor(kTiers[i]);
        const Rect r{x + plan.x[i], cy - h * 0.5f, plan.w[i], h};
        const bool me = mine == kTiers[i];
        const std::string n = std::to_string(t.of(kTiers[i]));
        if (plan.fit == MedalFit::Pills) {
            ui.r().fillRoundedRect(r, h * 0.5f, c.withAlphaF(me ? 0.30f : 0.14f));
            // Mine: a bright outline, thicker; the others a thin one of their colour.
            ui.r().drawRoundedRect(r, h * 0.5f, me ? Color::white() : c.withAlphaF(0.7f), std::max(1.0f, ui.s(me ? 2.0f : 1.0f)));
            const float d = MedalPlan::disk(h);
            const Vec2 dc{r.x + MedalPlan::pad(h) + d * 0.5f, cy};
            ui.r().fillCircle(dc, d * 0.5f, c);
            ui.r().drawCircle(dc, d * 0.5f, Color{40, 30, 10, 200}, std::max(1.0f, ui.s(0.8f)));
            ui.label(n, {dc.x + d * 0.5f + MedalPlan::inner(h), cy}, fs, c, Align::Left, FontFace::Bold, VAlign::Middle);
        } else {
            if (me) ui.r().drawRoundedRect(r, h * 0.3f, Color::white(), std::max(1.0f, ui.s(1.5f)));
            ui.label(n, r.center(), fs, c, Align::Center, FontFace::Bold, VAlign::Middle);
        }
    }
}

std::string medalsText(const ChallengeTierCounts& t) {
    return std::to_string(t.bronze) + " bronze · " + std::to_string(t.silver) + " argent · " + std::to_string(t.gold) + " or (joueurs vérifiés)";
}

std::string percentText(float p) {
    p = std::clamp(p, 0.0f, 100.0f);
    if (p >= 100.0f) return "100 %";
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", std::floor(p * 10.0f) / 10.0f);
    std::string s = b;
    std::replace(s.begin(), s.end(), '.', ',');
    return s + " %";
}

namespace {
const char* const kMonthNames[12] = {"janvier", "février", "mars",      "avril",   "mai",      "juin",
                                     "juillet", "août",    "septembre", "octobre", "novembre", "décembre"};
const char* const kMonthTitles[12] = {"Janvier", "Février", "Mars",      "Avril",   "Mai",      "Juin",
                                      "Juillet", "Août",    "Septembre", "Octobre", "Novembre", "Décembre"};
}  // namespace

const char* monthTitle(int month) { return kMonthTitles[std::clamp(month, 1, 12) - 1]; }

std::string frenchDate(const std::string& ymd, bool year) {
    int y = 0, m = 0, d = 0;
    if (std::sscanf(ymd.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3 || m < 1 || m > 12) return ymd;
    std::string out = std::to_string(d) + " " + kMonthNames[m - 1];
    if (year) out += " " + std::to_string(y);
    return out;
}

std::string frenchMonth(const std::string& ym) {
    int y = 0, m = 0;
    if (std::sscanf(ym.c_str(), "%4d-%2d", &y, &m) != 2 || m < 1 || m > 12) return ym;
    return std::string(kMonthNames[m - 1]) + " " + std::to_string(y);
}

LevelPlace levelPlace(GameServices& g, const DailyChallenge& c) {
    LevelPlace p;
    if (!c.community()) {
        const LevelEntry* e = g.content.level(c.level.levelId);
        if (!e || e->source != ContentSource::Builtin) {
            p.problem = "Ce niveau n'existe pas dans votre version du jeu : mettez le jeu à jour.";
            return p;
        }
        p.localId = e->id;
        const LevelFingerprint* fp = g.online.fingerprints().get(g.content, e->id);
        p.sameVersion = fp && fp->hash == c.level.hash;
        // 2.45: another revision is current (published online by an administrator); the
        // challenge keeps the one it was drawn on, when this computer has it.
        if (!p.sameVersion && g.campaignRevisions.fileFor(g.content, e->id, c.level.hash)) p.sameVersion = true;
        return p;
    }
    // A community level: the copy installed from ONLINE LEVELS at the challenge's version.
    for (const LevelEntry* e : g.content.levels()) {
        const Json* info = g.online.communityInstall(e->id);
        if (!info || jstr(*info, "id") != c.level.levelId) continue;
        if (jget<int>(*info, "version", 0) != c.level.version) continue;
        p.localId = e->id;
        p.sameVersion = jstr(*info, "level_hash") == c.level.hash && jget<bool>(*info, "verified", false);
        return p;
    }
    p.problem = "Niveau de la communauté à télécharger (version " + std::to_string(c.level.version) + ").";
    return p;
}

std::string planetLineOf(GameServices& g, const std::string& levelId) {
    const LevelEntry* e = g.content.level(levelId);
    if (!e) return {};
    std::string out;
    if (const PlanetData* p = g.content.planet(e->planet)) out = p->name;
    auto list = g.content.levelsForPlanet(e->planet);
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i]->id == e->id) out += (out.empty() ? "" : " · ") + std::string("niveau ") + std::to_string(i + 1);
    return out;
}

std::string planetLine(GameServices& g, const DailyChallenge& c) { return c.community() ? std::string() : planetLineOf(g, c.level.levelId); }

std::string levelTitle(GameServices& g, const DailyChallenge& c) {
    if (!c.community())
        if (const LevelEntry* e = g.content.level(c.level.levelId)) return e->name;
    return c.level.title.empty() ? c.level.levelId : c.level.title;
}

std::string powerList(GameServices& g, const std::vector<std::string>& ids) {
    std::string out;
    for (const std::string& id : ids) {
        const PowerDefinition* d = g.content.power(id);
        out += (out.empty() ? "" : ", ") + (d ? d->name : id);
    }
    return out;
}

std::string tierCondition(const DailyChallenge& c, ChallengeTier t) {
    if (c.finishObjective) {
        // Whole sentences (one template each for the translation).
        if (t == ChallengeTier::Bronze) return "Atteindre " + std::to_string(c.bronzeProgress) + " % du parcours";
        if (t == ChallengeTier::Silver) return "Atteindre " + std::to_string(c.silverProgress) + " % du parcours";
        return c.goldNoDamage ? std::string("Finir le niveau, sans dégât") : std::string("Finir le niveau");
    }
    if (t == ChallengeTier::Bronze) return "Arriver au bout";
    if (t == ChallengeTier::Silver) return "En " + formatRunTime(c.silverMs) + " ou moins";
    return "En " + formatRunTime(c.goldMs) + " ou moins" + (c.goldNoDamage ? std::string(", sans dégât") : std::string());
}

// ------------------------------------------------------------------ the calendar's cells

TexturePtr CalendarArt::cellThumb(const DailyChallenge& c) {
    LevelPlace place = levelPlace(g_, c);
    if (place.localId.empty()) return nullptr;
    return levelThumb(place.localId, c.level.hash.substr(0, 16));
}

TexturePtr CalendarArt::levelThumb(const std::string& localId, const std::string& version) {
    if (localId.empty() || !g_.content.level(localId)) return nullptr;
    std::string safe;
    for (char ch : version) safe += std::isalnum(static_cast<unsigned char>(ch)) ? ch : '_';
    const std::string key = localId + "_" + safe.substr(0, 16);
    const LevelPlace place{localId, true, {}};
    auto it = thumbs_.find(key);
    if (it != thumbs_.end()) return it->second;
    const fs::path file = Paths::userDir() / "cache" / "calendar" / (key + ".png");
    if (auto png = FileUtil::readBinary(file); png && !png->empty()) {
        TexturePtr t = textureFromPng(g_, *png);
        thumbs_[key] = t;
        return t;
    }
    if (budget_ <= 0) return nullptr;
    --budget_;
    TexturePtr t;
    if (auto level = g_.content.loadLevel(place.localId)) {
        std::string root;
        if (const LevelEntry* e = g_.content.level(place.localId)) root = Paths::toUtf8(e->assetRoot);
        auto png = renderLevelThumbnail(g_, *level, root);
        if (!png.empty()) {
            t = textureFromPng(g_, png);
            FileUtil::writeBinary(file, png);
        }
    }
    thumbs_[key] = t;
    return t;
}

void CalendarArt::drawCellImage(const Rect& r, const DailyChallenge& c, bool lit) {
    UIContext& ui = g_.ui;
    TexturePtr t = cellThumb(c);
    if (!t || t->width() <= 0 || t->height() <= 0) return;
    Rect in = r.expanded(-std::max(1.0f, ui.s(1)));
    float tw = static_cast<float>(t->width()), th = static_cast<float>(t->height());
    float k = std::max(in.w / tw, in.h / th);
    float sw = in.w / k, sh = in.h / k;
    Rect src{(tw - sw) * 0.5f, (th - sh) * 0.5f, sw, sh};
    ui.r().pushClip(in);
    ui.r().drawTexture(*t, &src, in, Color{255, 255, 255, static_cast<uint8_t>(lit ? 255 : 225)});
    ui.r().fillGradientV(in, Color{8, 12, 26, lit ? uint8_t(70) : uint8_t(100)}, Color{8, 12, 26, 215});
    ui.r().popClip();
}

std::vector<std::pair<std::string, Color>> CalendarArt::tagsOf(const DailyChallenge& c) {
    const UITheme& th = g_.ui.theme();
    std::vector<std::pair<std::string, Color>> tags;
    if (extremeLevel(c)) tags.push_back({"EXTRÊME", th.danger});
    if (c.community()) tags.push_back({"Communauté", th.accent});
    // 2.41: a challenge ranked by its progress.
    if (c.finishObjective) tags.push_back({"Finir le niveau", th.success});
    tags.push_back({std::to_string(c.rules.hearts) + (c.rules.hearts > 1 ? " cœurs" : " cœur"), th.warm});
    if (!c.rules.checkpoints) tags.push_back({"Sans checkpoint", Color{185, 200, 218, 255}});
    if (!c.rules.stations) tags.push_back({"Sans station", Color{185, 200, 218, 255}});
    return tags;
}

void CalendarArt::drawTags(const DailyChallenge& c, float x, float cy, float right, float h) {
    UIContext& ui = g_.ui;
    for (const auto& [text, color] : tagsOf(c)) {
        float tw = chipWidth(ui, text, h);
        if (x + tw > right) break;
        x += chip(ui, text, x, cy, h, color) + ui.s(5);
    }
}

bool CalendarArt::extremeLevel(const DailyChallenge& c) {
    const std::string key = c.level.levelId + ":" + c.level.hash;
    auto it = extremeOf_.find(key);
    if (it != extremeOf_.end()) return it->second;
    LevelPlace place = levelPlace(g_, c);
    if (place.localId.empty()) return false;  // not installed yet: asked again later
    bool e = false;
    if (auto data = g_.content.loadLevel(place.localId)) e = data->extreme && data->oneLife();
    return extremeOf_[key] = e;
}

}  // namespace cosmic::challenge_ui
