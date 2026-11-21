// 2.83: ADMINISTRATION › Raids, generic (maquette « Administration des raids »): the raid catalogue
// (game/raid/RaidCatalog, data/raids) gives every raid its title, name, subtitle, pitch, icon and poster,
// formats, difficulty range and counts; the screen's code names no raid (it reads them all from the
// catalogue); its « Raid » list, the raid's poster and its format plates fit, nothing over anything, at
// 1280 × 720 and 1920 × 1080, in French and in English.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "game/raid/RaidCatalog.h"
#include "tests/TestFramework.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/AdminRaids.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.right() - 0.5f && b.x < a.right() - 0.5f && a.y < b.bottom() - 0.5f && b.y < a.bottom() - 0.5f;
}
bool inside(const Rect& in, const Rect& out) {
    return in.x >= out.x - 0.5f && in.y >= out.y - 0.5f && in.right() <= out.right() + 0.5f && in.bottom() <= out.bottom() + 0.5f;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// The string literals of a C++ source (comments, characters and raw strings left out).
std::vector<std::string> literals(const std::string& src) {
    std::vector<std::string> out;
    size_t i = 0;
    const size_t n = src.size();
    while (i < n) {
        if (src.compare(i, 2, "//") == 0) {
            i = src.find('\n', i);
            if (i == std::string::npos) break;
        } else if (src.compare(i, 2, "/*") == 0) {
            i = src.find("*/", i + 2);
            if (i == std::string::npos) break;
            i += 2;
        } else if (src.compare(i, 3, "R\"(") == 0) {
            i = src.find(")\"", i + 3);
            if (i == std::string::npos) break;
            i += 2;
        } else if (src[i] == '\'') {
            ++i;
            while (i < n && src[i] != '\'') i += src[i] == '\\' ? 2 : 1;
            ++i;
        } else if (src[i] == '"') {
            std::string lit;
            ++i;
            while (i < n && src[i] != '"') {
                if (src[i] == '\\' && i + 1 < n) {
                    lit += src[i + 1];
                    i += 2;
                } else lit += src[i++];
            }
            ++i;
            out.push_back(lit);
        } else ++i;
    }
    return out;
}

/// A headless game (no window shown), over its menu.
struct Rig {
    Application app;
    std::unique_ptr<GameServices> g;
    Rig() {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        AppConfig cfg;
        cfg.headless = true;
        cfg.width = 1280;
        cfg.height = 720;
        cfg.vsync = false;
        app.init(cfg);
        app.headlessDt = 1.0f / 30.0f;
        g = std::make_unique<GameServices>(app);
        g->loadAll();
        app.onSceneRender = [this](bool covered) { g->ui.sceneInput = !covered; };
        g->progression.tutorial = "finished";
        g->settings.gameplay.attractTrailer = false;
        g->progression.asterionFilmSeen = true;
        app.pushScene(makeMainMenu(*g));
        app.runFrames(3);
    }
    ~Rig() {
        app.shutdown();
        app.onSceneRender = nullptr;
    }
};

}  // namespace

TEST_CASE("raid catalogue (2.83): every raid of data/raids with its texts, art, formats, difficulty range and counts") {
    std::vector<std::string> problems;
    const std::vector<raid::CatalogEntry> cat = raid::loadCatalog(Paths::dataDir() / "raids", &problems);
    for (const std::string& p : problems) std::printf("  %s\n", p.c_str());
    CHECK(problems.empty());
    CHECK(!cat.empty());
    CHECK(raid::catalog().size() == cat.size());
    for (const raid::CatalogEntry& e : cat) {
        CHECK(!e.title.empty() && !e.name.empty() && !e.subtitle.empty() && !e.pitch.empty());
        CHECK(raid::findRaid(e.id) != nullptr);
        // Its art: images of assets/ (an existing image, a crop of it).
        for (const raid::CatalogArt* a : {&e.icon, &e.poster}) {
            CHECK(a->valid());
            CHECK(FileUtil::exists(Paths::assetsDir() / a->image));
            CHECK(a->crop.w > 0.0f && a->crop.h > 0.0f);
        }
        CHECK(!e.formats.empty());
        for (const raid::CatalogFormat& f : e.formats) {
            CHECK(!f.id.empty() && !f.name.empty());
            CHECK(f.minMinutes > 0 && f.minMinutes <= f.maxMinutes);
        }
        CHECK(e.defaultFormat >= 0 && e.defaultFormat < static_cast<int>(e.formats.size()));
        CHECK(e.difficultyMin >= 1 && e.difficultyMin <= e.difficultyDefault && e.difficultyDefault <= e.difficultyMax);
        CHECK(e.parts > 0 && e.gigaboss > 0 && e.gigaboss <= e.parts);
        if (e.hasHeart) CHECK(!e.heartLabel.empty());
    }
    // Asterion, as the server knows it (raids.ts: RAID_FORMATS, the event's difficulty 1 to 5): its eight parts,
    // seven gigaboss and its heart; its sheet.
    const raid::CatalogEntry* a = raid::findRaid("asterion");
    CHECK(a != nullptr);
    if (!a) return;
    CHECK(a->parts == 8 && a->gigaboss == 7 && a->hasHeart);
    CHECK(a->formats.size() == 3);
    if (a->formats.size() == 3) {
        CHECK(a->formats[0].id == "court" && a->formats[0].minMinutes == 25 && a->formats[0].maxMinutes == 30);
        CHECK(a->formats[1].id == "standard" && a->formats[1].minMinutes == 45 && a->formats[1].maxMinutes == 60);
        CHECK(a->formats[2].id == "legendaire" && a->formats[2].minMinutes == 75 && a->formats[2].maxMinutes == 90);
    }
    CHECK(a->formats[static_cast<size_t>(a->defaultFormat)].id == "standard");
    CHECK(a->difficultyMin == 1 && a->difficultyMax == 5);
    CHECK(a->sheet == "asterion");
    const std::vector<std::string> chips = raidPosterChips(*a);
    CHECK(chips.size() == 4);
    if (chips.size() == 4) {
        CHECK(chips[0] == "8 parties");
        CHECK(chips[1] == "7 Gigaboss + " + a->heartLabel);
        CHECK(chips[2] == "Court · Standard · Légendaire");
        CHECK(chips[3] == "Difficulté 1 à 5");
    }
}

TEST_CASE("ADMINISTRATION > Raids (2.83): the screen names no raid, everything comes from the raid catalogue") {
    const auto root = Paths::dataDir().parent_path();
    int read = 0;
    for (const char* f : {"ui/screens/AdminRaids.cpp", "ui/screens/AdminRaids.h"}) {
        const auto src = FileUtil::readText(root / f);
        CHECK(src.has_value());
        if (!src) continue;
        ++read;
        for (const std::string& lit : literals(*src)) {
            const std::string l = lower(lit);
            for (const raid::CatalogEntry& e : raid::catalog()) {
                std::vector<std::string> own = {e.id, e.name, e.title, e.subtitle, e.pitch};
                if (!e.heartLabel.empty()) own.push_back(e.heartLabel);
                for (const raid::CatalogFormat& fm : e.formats) own.push_back(fm.name);
                for (const std::string& o : own) {
                    if (o.empty() || l.find(lower(o)) == std::string::npos) continue;
                    std::printf("  %s : « %s » dans \"%s\"\n", f, o.c_str(), lit.substr(0, 80).c_str());
                    CHECK(l.find(lower(o)) == std::string::npos);
                }
            }
        }
    }
    CHECK(read == 2);
}

TEST_CASE("ADMINISTRATION > Raids (2.83): the raid's list, its poster and its formats fit at 1280 x 720 and 1920 x 1080, in French and in English") {
    Rig r;
    GameServices& g = *r.g;
    AdminRaids raids(g, [](const std::string&, bool) {}, [](const ApiResult&) { return false; });
    const raid::CatalogEntry* first = raid::catalog().empty() ? nullptr : &raid::catalog().front();
    CHECK(first != nullptr);
    if (!first) return;
    for (Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}})
        for (Language lang : {Language::French, Language::English}) {
            r.app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
            r.app.renderer().refreshOutputSize();
            i18n::setLanguage(lang);
            r.app.runFrames(1);
            r.app.renderer().refreshOutputSize();
            UIContext& ui = g.ui;
            ui.begin(r.app, 1.0f / 60.0f);
            // The body of ADMINISTRATION as AdminScene places it (its tabs on their own line: the least room).
            const float W = std::min(ui.screenW() - ui.s(96), ui.s(1760)), x0 = (ui.screenW() - W) * 0.5f;
            const float bodyY = ui.s(36) + ui.s(52) + ui.s(22) + ui.s(52) + ui.s(16);
            const Rect body{x0, bodyY, W, ui.screenH() - bodyY - ui.s(64)};
            const Rect inner{body.x + ui.s(22), body.y + ui.s(18), body.w - ui.s(44), body.h - ui.s(36)};
            raids.draw(inner);
            ui.end();
            const AdminRaidsLayout& L = raids.lastLayout();
            const RaidPosterLayout& P = L.poster;
            const char* where = lang == Language::French ? "fr" : "en";
            // The columns: side by side, on the screen.
            CHECK(inside(L.form, inner) && inside(L.right, inner));
            CHECK(!overlaps(L.form, L.right));
            CHECK(L.sideTabsBottom <= L.right.bottom());
            // The list, the poster, the plates: one under the other, in the form's column.
            CHECK(inside(L.raidBox, L.fields));
            CHECK(P.card.y >= L.raidBox.bottom() - 0.5f);
            CHECK(P.card.x >= L.form.x - 0.5f && P.card.right() <= L.fields.right() + 0.5f);
            CHECK(P.height > 0.0f);
            CHECK(L.plates.size() == first->formats.size());
            for (size_t i = 0; i < L.plates.size(); ++i) {
                CHECK(L.plates[i].y >= P.card.bottom() - 0.5f);
                CHECK(L.plates[i].x >= L.form.x && L.plates[i].right() <= L.fields.right() + 0.5f);
                for (size_t j = i + 1; j < L.plates.size(); ++j) CHECK(!overlaps(L.plates[i], L.plates[j]));
                const float tw = raidui::textW(ui, L.plateLabels[i], L.plateFonts[i], FontFace::Bold);
                if (tw > L.plates[i].w - ui.s(16)) std::printf("  [%s %.0f] plaque « %s » : %.1f > %.1f\n", where, size.x, L.plateLabels[i].c_str(), tw, L.plates[i].w);
                CHECK(tw <= L.plates[i].w - ui.s(16) + 0.5f);
            }
            // The poster: everything inside its card, the tag on its art, no text over another nor over the art.
            CHECK(inside(P.accent, P.card) && inside(P.art, P.card));
            if (P.tag.r.w > 0.0f) CHECK(inside(P.tag.r, P.art));
            std::vector<const RaidPosterText*> texts;
            for (const auto* v : {&P.name, &P.subtitle, &P.pitch})
                for (const RaidPosterText& t : *v) texts.push_back(&t);
            for (const RaidPosterText& t : P.chips) texts.push_back(&t);
            if (P.link.r.w > 0.0f) texts.push_back(&P.link);
            CHECK(!P.name.empty() && !P.subtitle.empty() && !P.pitch.empty() && P.chips.size() == raidPosterChips(*first).size());
            CHECK(P.link.r.w > 0.0f);  // the raid has its sheet
            for (size_t i = 0; i < texts.size(); ++i) {
                const RaidPosterText& t = *texts[i];
                CHECK(inside(t.r, P.card));
                CHECK(!overlaps(t.r, P.art));
                const float w = raidui::textW(ui, t.text, t.size, t.face, t.verbatim);
                if (w > t.r.w + 0.5f) std::printf("  [%s %.0f] « %s » : %.1f > %.1f\n", where, size.x, t.text.c_str(), w, t.r.w);
                CHECK(w <= t.r.w + 0.5f);
                CHECK(ui.s(t.size) <= t.r.h + 0.5f);
                for (size_t j = i + 1; j < texts.size(); ++j) CHECK(!overlaps(t.r, texts[j]->r));
            }
            for (size_t i = 0; i < P.chipBoxes.size(); ++i) {
                CHECK(inside(P.chipBoxes[i], P.card));
                CHECK(!overlaps(P.chipBoxes[i], P.art));
                for (size_t j = i + 1; j < P.chipBoxes.size(); ++j) CHECK(!overlaps(P.chipBoxes[i], P.chipBoxes[j]));
                if (P.link.r.w > 0.0f) CHECK(!overlaps(P.chipBoxes[i], P.link.r));
                for (const auto* v : {&P.name, &P.subtitle, &P.pitch})
                    for (const RaidPosterText& t : *v) CHECK(!overlaps(P.chipBoxes[i], t.r));
            }
            // The poster says the raid of the catalogue: its name, as this language shows it.
            if (!P.name.empty()) CHECK(P.name.front().text.find(i18n::tr(first->name)) != std::string::npos);
        }
    i18n::setLanguage(Language::French);
}
