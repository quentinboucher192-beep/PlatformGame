// Main menu → Collection (2.19): the cosmetic rewards unlocked by the achievements' stars
// (Vestiaire, Route des étoiles), the jukebox of the tracks heard in the game (Musiques)
// and the gallery of the planets and creatures met (Galerie). 2.41: the Vestiaire's « Badges
// du profil », achievements shown on the online profile (slots opened by the stars). 2.45:
// « Replays », the online replays downloaded to be watched without a connection
// (ui/screens/DownloadedReplaysUi.h). Nothing here changes a run, a replay or a record.

#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <future>
#include <initializer_list>
#include <map>
#include <set>

#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Log.h"
#include "game/GameServices.h"
#include "game/content/CreaturePlaces.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/ChallengeRewardsUi.h"
#include "ui/screens/CosmeticFx.h"
#include "ui/screens/CreatureArt.h"
#include "ui/screens/DownloadedReplaysUi.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/ProfileBadgesUi.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

/// Never below 11.5 px on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

const Color kGold{255, 214, 90, 255};
const Color kGreen{120, 230, 140, 255};

std::string starsText(int n) { return std::to_string(n) + (n > 1 ? " étoiles" : " étoile"); }

/// A filled five-pointed star.
void drawStar(Renderer& r, Vec2 c, float radius, Color col) {
    Vec2 pts[10];
    for (int i = 0; i < 10; ++i) {
        float a = -1.5707963f + static_cast<float>(i) * 0.6283185f;
        float rr = i % 2 == 0 ? radius : radius * 0.45f;
        pts[i] = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
    }
    for (int i = 0; i < 10; ++i) r.fillTriangle(c, pts[i], pts[(i + 1) % 10], col);
}

// ------------------------------------------------------------------ shared drawing

/// Nova (or a suit) at `dst`: a frame of `clip` from the suit's recoloured sheet (a suit created
/// on Cosmic Online: recoloured at run time, GameServices::suitSheet).
void drawNova(GameServices& g, const std::string& suitLook, const std::string& clip, float t, const Rect& dst, Color tint = Color::white()) {
    AnimationSetPtr set = g.app.resources().animation("animations/nova.json");
    if (!set || !set->texture) return;
    const AnimationClip* c = set->clip(clip);
    if (!c) c = set->clip("idle");
    if (!c || c->frameCount() == 0) return;
    float total = std::max(0.01f, c->totalDuration());
    float local = std::fmod(t, total), acc = 0.0f;
    int frame = 0;
    for (int i = 0; i < c->frameCount(); ++i) {
        acc += c->durations[static_cast<size_t>(i)];
        if (local < acc) {
            frame = i;
            break;
        }
    }
    TexturePtr tex = set->texture;
    if (TexturePtr s = g.suitSheet(suitLook)) tex = s;
    Rect src = set->frameRect(*c, frame);
    g.app.renderer().drawTexture(*tex, &src, dst, tint);
}

/// A title as a gold chip (2.43: a server title in its colour) centred on `c` (its size grows
/// with the text; a smaller font when it would be wider than `maxWidth`, 0: no limit).
void drawTitleChip(UIContext& ui, Vec2 c, const std::string& text, float size, bool on = true, float maxWidth = 0.0f, Color gold = kGold) {
    if (text.empty()) return;
    float fs = fz(ui, size);
    if (maxWidth > ui.s(22)) fs = fz(ui, ui.fitFont(text, FontFace::Bold, fs, maxWidth - ui.s(22)));
    float w = ui.text().measure(text, FontFace::Bold, ui.s(fs)).x + ui.s(22);
    if (maxWidth > ui.s(22)) w = std::min(w, maxWidth);
    Rect r = Rect::fromCenter(c, {w, ui.s(fs) + ui.s(10)});
    Color col = on ? gold : ui.theme().textDim;
    ui.r().fillRoundedRect(r, r.h * 0.5f, col.withAlpha(28));
    ui.r().drawRoundedRect(r, r.h * 0.5f, col.withAlpha(160), std::max(1.0f, ui.s(1.2f)));
    ui.label(ui.elide(text, FontFace::Bold, fs, w - ui.s(14)), c, fs, col, Align::Center, FontFace::Bold, VAlign::Middle);
}

/// The colour of a title item's chip: gold, a server title's own (2.43).
Color chipColor(const CosmeticItem* it) { return it && !it->color.empty() ? titleColor(it->color) : kGold; }

/// Where each creature of the game appears in the campaign (planet ids), read from the
/// levels once, off the main thread (game/content/CreaturePlaces.h).
using PlanetsOf = std::map<std::string, std::vector<std::string>>;
PlanetsOf scanCampaign(const ContentDatabase& content) { return scanCreatures(content).planetsByCreature(); }

struct Track {
    std::string file, title, where, group, planet;
};

/// Vestiaire, Route des étoiles, Musiques, Galerie, Replays.
constexpr int kTabCount = 5;

class CollectionScene : public Scene {
public:
    CollectionScene(GameServices& g, int tab, std::string select = {})
        : g_(g), tab_(std::clamp(tab, 0, kTabCount - 1)), online_(g), select_(std::move(select)), replays_(g) {}

    void onEnter() override {
        loadTracks();
        progress_ = g_.cosmeticProgress();
        if (!select_.empty()) showItem(select_);  // opened on a reward (2.35: from the challenges)
        // Automated screenshots: "col:<tab>[:detail]".
        const std::string& o = g_.onlineOpen;
        if (o.rfind("col-cat:", 0) == 0) cat_ = std::clamp(std::atoi(o.c_str() + 8), 0, 5);  // 5: the profile badges
        if (o.rfind("col-badge:", 0) == 0) {  // the profile badges, this achievement selected
            cat_ = static_cast<int>(g_.cosmetics.categories().size());
            selBadge_ = o.substr(10);
        }
        if (o.rfind("col-sel:", 0) == 0) {
            selItem_ = o.substr(8);
            showItem(selItem_);  // its category too
        }
        if (o.rfind("col-gal:", 0) == 0) galTab_ = std::clamp(std::atoi(o.c_str() + 8), 0, 2);
        if (o.rfind("col-track:", 0) == 0) selTrack_ = o.substr(10);
        routeEnd_ = o == "col-end";  // the route scrolled to its end
        g_.ui.setFocus("col/tab" + std::to_string(tab_));
        scan_ = std::async(std::launch::async, [&content = g_.content] { return scanCampaign(content); });
        adoptPublishedBadges();
    }

    /// Nothing chosen on this computer yet: the badges already on the profile (chosen on
    /// another computer) become this computer's choice, instead of being erased by it.
    void adoptPublishedBadges() {
        if (g_.collection.badgesChosen || !g_.online.usable() || !g_.online.loggedIn() || g_.online.playerId().empty()) return;
        g_.online.get("/api/v1/players/" + g_.online.playerId(), OnlineService::guard(life_, [this](const ApiResult& r) {
                          if (!r.ok || g_.collection.badgesChosen) return;
                          const Json list = r.data.value("badges", Json());
                          if (!list.is_array() || list.empty()) return;
                          std::vector<std::string> ids;
                          for (const auto& b : list)
                              if (std::string id = jstr(b, "id"); !id.empty() && g_.achievementCatalog.find(id)) ids.push_back(id);
                          if (ids.empty()) return;
                          g_.collection.badges = std::move(ids);
                          g_.collection.badgesChosen = true;
                          g_.collection.save();
                      }),
                      false);
    }
    void onExit() override {
        // Back to the menu's music (the jukebox may have changed it).
        g_.app.audio().playMusic(g_.app.resources().soundPath(g_.menuMusic()), 800, true);
    }
    bool isOverlay() const override { return false; }

    void update(float dt) override {
        t_ += dt;
        // Jukebox: next track at the end of a non-repeating one.
        if (playing_ && !loop_ && g_.app.audio().musicEnded()) step(1, true);
        // Trail preview: an emitter going round in front of Nova.
        float a = t_ * 2.4f;
        trailFx_.update(trailCenter_ + Vec2{std::cos(a) * trailRadius_, std::sin(a * 2.0f) * trailRadius_ * 0.25f}, dt, true);
        if (scan_.valid() && scan_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) planetsOf_ = scan_.get();
        replays_.update();
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        Renderer& r = ui.r();
        r.fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{8, 11, 24, 255});
        ui.pushId("col");
        const float m = ui.s(40);
        float y = ui.s(26);
        // Header: title, sections, stars, back.
        ui.label("COLLECTION", {m, y + ui.s(24)}, 32, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        float x = m + ui.text().measure("COLLECTION", FontFace::Title, ui.s(32)).x + ui.s(24);
        static const char* kTabs[kTabCount] = {"Vestiaire", "Route des étoiles", "Musiques", "Galerie", "Replays"};
        float backW = ui.s(140);
        // The tabs fit between the title and « Retour »: a smaller font when the window is narrow.
        float tabFont = 17.0f;
        {
            float room = ui.screenW() - m - backW - ui.s(14) - x, need = 0.0f;
            for (const char* t : kTabs) need += ui.text().measure(t, FontFace::Bold, ui.s(17)).x + ui.s(34) + ui.s(8);
            if (need > room && need > 0.0f) tabFont = std::max(17.0f * std::max(room - kTabCount * ui.s(42), 0.0f) / std::max(need - kTabCount * ui.s(42), 1.0f), 11.5f / ui.s(1.0f));
        }
        for (int i = 0; i < kTabCount; ++i) {
            float w = ui.text().measure(kTabs[i], FontFace::Bold, ui.s(tabFont)).x + ui.s(34);
            UIContext::ButtonStyle st;
            st.small = true;
            st.selected = tab_ == i;
            st.fontSize = tabFont;
            if (ui.button("tab" + std::to_string(i), {x, y + ui.s(4), w, ui.s(42)}, kTabs[i], st)) tab_ = i;
            // « NOUVEAU » while a reward added by an update waits in it (data/news/new_features.json).
            static const char* const kTabFeatures[kTabCount] = {"collection.tab.wardrobe", "collection.tab.route", "collection.tab.music",
                                                                "collection.tab.gallery", "collection.tab.replays"};
            ui.newBadge("tab" + std::to_string(i), {x, y + ui.s(4), w, ui.s(42)}, kTabFeatures[i], true, true);
            x += w + ui.s(8);
        }
        UIContext::ButtonStyle bs;
        bs.small = true;
        bool back = ui.button("back", {ui.screenW() - m - backW, y + ui.s(4), backW, ui.s(42)}, "Retour", bs);
        std::string st = std::to_string(progress_.stars) + " / " + std::to_string(progress_.totalStars);
        float stW = ui.text().measure(st, FontFace::Bold, ui.s(20)).x + ui.text().measure("étoiles", FontFace::Body, ui.s(15)).x + ui.s(64);
        Rect chip{ui.screenW() - m - backW - ui.s(14) - stW, y + ui.s(4), stW, ui.s(42)};
        if (chip.x > x + ui.s(8)) {
            r.fillRoundedRect(chip, ui.s(10), Color{10, 16, 34, 230});
            r.drawRoundedRect(chip, ui.s(10), kGold.withAlpha(140), std::max(1.0f, ui.s(1.5f)));
            Vec2 c{chip.x + ui.s(22), chip.center().y};
            drawStar(r, c, ui.s(10), kGold);
            float tx = c.x + ui.s(16);
            Vec2 sz = ui.label(st, {tx, c.y}, 20, kGold, Align::Left, FontFace::Bold, VAlign::Middle);
            ui.label("étoiles", {tx + sz.x + ui.s(8), c.y}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        }
        Rect area{m, y + ui.s(66), ui.screenW() - 2 * m, ui.screenH() - (y + ui.s(66)) - ui.s(26)};
        switch (tab_) {
            case 0: drawWardrobe(area); break;
            case 1: drawRoute(area); break;
            case 2: drawMusic(area); break;
            case 3: drawGallery(area); break;
            default: replays_.draw(area); break;
        }
        ui.popId();
        if (back || ui.backPressed()) {
            g_.collection.save();
            // Opened from the main menu (it replaced it) or on top of another screen.
            if (g_.app.sceneCount() > 1) g_.app.popScene();
            else g_.app.switchScene(makeMainMenu(g_), 0.3f);
        }
        ui.end();
    }

private:
    // ================================================================= Vestiaire
    void drawWardrobe(const Rect& area) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const auto& cats = g_.cosmetics.categories();
        if (cats.empty()) return;
        const int badgesCat = static_cast<int>(cats.size());  // 2.41: the profile badges, after the categories
        cat_ = std::clamp(cat_, 0, badgesCat);
        // Left: categories.
        float lw = std::min(ui.s(280), area.w * 0.19f);
        float y = area.y;
        for (size_t i = 0; i < cats.size(); ++i) {
            auto items = itemsOf(cats[i].id);
            int got = 0;
            for (auto* it : items) got += cosmeticUnlocked(*it, progress_) ? 1 : 0;
            Rect b{area.x, y, lw, ui.s(64)};
            UIContext::ButtonStyle st;
            st.selected = cat_ == static_cast<int>(i);
            st.align = Align::Left;
            if (ui.button("cat" + std::to_string(i), b, "", st)) {
                cat_ = static_cast<int>(i);
                selItem_.clear();
            }
            ui.newBadge("cat" + std::to_string(i), b, "collection.cat." + cats[i].id, true, true);
            ui.label(ui.elide(cats[i].name, FontFace::Bold, 19, lw - ui.s(32)), {b.x + ui.s(16), b.y + ui.s(10)}, 19, ui.theme().text, Align::Left, FontFace::Bold);
            ui.label(std::to_string(got) + " / " + std::to_string(items.size()) + " débloqués", {b.x + ui.s(16), b.y + ui.s(38)}, fz(ui, 14), ui.theme().textDim);
            y += ui.s(74);
        }
        // 2.41: the profile badges, a place of their own under the categories.
        {
            Rect b{area.x, y, lw, ui.s(64)};
            UIContext::ButtonStyle st;
            st.selected = cat_ == badgesCat;
            st.align = Align::Left;
            if (ui.button("catbadges", b, "", st)) cat_ = badgesCat;
            ui.newBadge("catbadges", b, "collection.cat.badges", true, true);
            ui.label(ui.elide("Badges du profil", FontFace::Bold, 19, lw - ui.s(32)), {b.x + ui.s(16), b.y + ui.s(10)}, 19, ui.theme().text, Align::Left,
                     FontFace::Bold);
            const int slots = g_.achievementCatalog.badgeSlots(progress_.stars);
            const BadgeSlotStep* first = g_.achievementCatalog.nextBadgeStep(0);
            std::string sub = slots > 0 ? std::to_string(g_.shownBadges().size()) + " / " + std::to_string(slots) + " affichés"
                              : first   ? "Dès " + std::to_string(first->stars) + " étoiles"
                                        : std::string("Indisponibles");
            ui.label(ui.elide(sub, FontFace::Body, fz(ui, 14), lw - ui.s(32)), {b.x + ui.s(16), b.y + ui.s(38)}, fz(ui, 14), ui.theme().textDim);
            y += ui.s(74);
        }
        ui.labelWrapped("Les étoiles viennent des succès (menu principal → Succès). Certaines récompenses demandent un succès précis.",
                        {area.x, area.bottom() - ui.s(86)}, lw, fz(ui, 14), ui.theme().textDim);
        if (cat_ == badgesCat) {
            drawBadges(area, lw);
            return;
        }
        const std::string catId = cats[static_cast<size_t>(cat_)].id;

        // Items of the category (2.43: the titles won at a challenge after the catalogue's).
        auto items = itemsOf(catId);
        const CosmeticItem* equipped = g_.collection.equipped(g_.cosmetics, catId, progress_);
        const CosmeticItem* sel = findItem(selItem_);
        if (!sel || sel->category != catId) sel = equipped;
        float pw = std::min(ui.s(560), area.w * 0.38f);
        Rect prev{area.x + lw + ui.s(24), area.y, pw, area.h * 0.68f};
        Rect gridA{prev.right() + ui.s(24), area.y, area.right() - prev.right() - ui.s(24), area.h};
        // Preview: Nova wearing the equipped items, the selected one tried on.
        auto lookOf = [&](const std::string& c) {
            if (sel && sel->category == c) return sel->look;
            return g_.collection.look(g_.cosmetics, c, progress_);
        };
        r.fillRoundedRectGradient(prev, ui.s(16), Color{17, 26, 54, 255}, Color{10, 15, 32, 255});
        r.drawRoundedRect(prev, ui.s(16), ui.theme().accent.withAlpha(80), std::max(1.0f, ui.s(1.5f)));
        float ground = prev.bottom() - prev.h * 0.2f;
        r.fillRect({prev.x + ui.s(2), ground, prev.w - ui.s(4), ui.s(2)}, ui.theme().accent.withAlpha(90));
        r.fillRect({prev.x + ui.s(2), ground + ui.s(2), prev.w - ui.s(4), prev.bottom() - ground - ui.s(4)}, Color{16, 24, 44, 255});
        float novaS = std::min(prev.h * 0.5f, prev.w * 0.45f);
        Rect nova = Rect::fromCenter({prev.center().x - prev.w * 0.08f, ground - novaS * 0.5f}, {novaS, novaS});
        r.pushClip(prev.expanded(-ui.s(2)));
        // Trail, then Nova over it, then the victory effect (replayed every 3 s).
        // The preview's emitter loops around Nova (see update()).
        trailCenter_ = {nova.center().x, nova.center().y + novaS * 0.12f};
        trailRadius_ = novaS * 0.62f;
        trailFx_.draw(r, lookOf("trail"), false, novaS / 110.0f);
        drawNova(g_, lookOf("suit"), "run", t_, nova);
        if (std::string v = lookOf("victory"); v != "classic")
            drawVictoryFx(r, v, {nova.center().x, nova.bottom() - novaS * 0.1f}, std::fmod(t_, 3.2f), false, novaS / 140.0f);
        r.popClip();
        ui.label("Nova", {prev.center().x, prev.y + ui.s(30)}, 22, ui.theme().text, Align::Center, FontFace::Title, VAlign::Middle);
        std::string title = lookOf("title");
        const CosmeticItem* titleItem = sel && sel->category == "title" ? sel : g_.collection.equipped(g_.cosmetics, "title", progress_);
        drawTitleChip(ui, {prev.center().x, prev.y + ui.s(62)}, title.empty() ? "Sans titre" : title, 15, !title.empty(), prev.w - ui.s(24),
                      chipColor(titleItem));
        float fr = ui.s(40);
        Vec2 fc{prev.right() - fr - ui.s(30), ground - fr - ui.s(28)};
        online_.drawAvatar("avatar_01", "", Rect::fromCenter(fc, {fr * 2, fr * 2}));
        drawAvatarFrame(r, fc, fr, lookOf("frame"), ui.s(1), t_);
        ui.label("cadre", {fc.x, fc.y + fr + ui.s(18)}, fz(ui, 13), ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);

        // Detail of the selected item.
        Rect det{prev.x, prev.bottom() + ui.s(16), prev.w, area.bottom() - prev.bottom() - ui.s(16)};
        ui.panel(det, false, 14);
        if (sel) {
            bool unlocked = cosmeticUnlocked(*sel, progress_);
            float dx = det.x + ui.s(20), dy = det.y + ui.s(18);
            // The name, then the category when there is room (a server title's name may be long).
            const float nameRoom = det.w - ui.s(40);
            const float nfs = std::max(ui.fitFont(sel->name, FontFace::Bold, 26, nameRoom, 0.6f), 11.5f / ui.s(1.0f));
            Vec2 nsz = ui.label(ui.elide(sel->name, FontFace::Bold, nfs, nameRoom), {dx, dy}, nfs, ui.theme().text, Align::Left, FontFace::Bold);
            const std::string& catName = cats[static_cast<size_t>(cat_)].name;
            if (dx + nsz.x + ui.s(12) + ui.text().measure(catName, FontFace::Body, ui.s(fz(ui, 15))).x <= det.right() - ui.s(20))
                ui.label(catName, {dx + nsz.x + ui.s(12), dy + ui.s(8)}, fz(ui, 15), ui.theme().textDim);
            ui.labelWrapped(sel->description, {dx, dy + ui.s(44)}, det.w - ui.s(40), fz(ui, 16), Color{185, 200, 218, 255});
            float by = det.bottom() - ui.s(66);
            if (sel->challengeReward()) {
                // 2.35: a reward of the challenges: its whole condition between the description and
                // the button, when it fits (else the short one stays beside the button).
                float descH = ui.text().measureWrapped(sel->description, FontFace::Body, ui.s(fz(ui, 16)), det.w - ui.s(40)).y;
                float cy = dy + ui.s(44) + descH + ui.s(8);
                std::string cond = challengeConditionText(*sel) + " " + challengeConditionNote(*sel);
                for (float size : {15.0f, 13.5f, 12.0f}) {
                    float fs = fz(ui, size);
                    if (cy + ui.text().measureWrapped(cond, FontFace::Body, ui.s(fs), det.w - ui.s(40)).y > by - ui.s(8)) continue;
                    ui.labelWrapped(cond, {dx, cy}, det.w - ui.s(40), fs, kGold);
                    break;
                }
            }
            std::string req = reqText(*sel);
            if (unlocked) {
                bool isEq = equipped == sel;
                UIContext::ButtonStyle st;
                st.primary = !isEq;
                st.selected = isEq;
                if (ui.button("equip", {dx, by, ui.s(170), ui.s(48)}, isEq ? "Équipé" : "Équiper", st) && !isEq) {
                    g_.collection.equip(catId, sel->id);
                    g_.collection.markAnnounced(sel->id);
                    g_.collection.save();
                    g_.syncCosmetics();
                    g_.noteOutfit();
                    progress_ = g_.cosmeticProgress();  // an achievement of the Collection may add stars
                    g_.uiSound("ui_confirm", 0.6f);
                }
                ui.label(ui.elide(req, FontFace::Body, fz(ui, 15), det.right() - dx - ui.s(210)), {dx + ui.s(186), by + ui.s(24)}, fz(ui, 15),
                         ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            } else {
                std::string lock = lockText(*sel);
                float lw2 = ui.text().measure(lock, FontFace::Bold, ui.s(17)).x + ui.s(36);
                Rect lr{dx, by, lw2, ui.s(48)};
                r.fillRoundedRect(lr, ui.s(10), kGold.withAlpha(26));
                ui.label(lock, lr.center(), 17, kGold, Align::Center, FontFace::Bold, VAlign::Middle);
                ui.label(ui.elide(req, FontFace::Body, fz(ui, 15), det.right() - lr.right() - ui.s(36)), {lr.right() + ui.s(16), by + ui.s(24)},
                         fz(ui, 15), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            }
        }

        // Grid of the category's items.
        int got = 0;
        for (auto* it : items) got += cosmeticUnlocked(*it, progress_) ? 1 : 0;
        // 2.43: the titles won at a challenge count too.
        ui.label(cats[static_cast<size_t>(cat_)].name, {gridA.x, gridA.y + ui.s(12)}, 21, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        ui.label(std::to_string(got) + " / " + std::to_string(items.size()) + " débloqués", {gridA.right(), gridA.y + ui.s(12)}, fz(ui, 15),
                 ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
        int cols = 3;
        float gap = ui.s(12);
        float ch = ui.s(156);
        float gy = gridA.y + ui.s(36);
        // The list starts a little above the first cards: a « NOUVEAU » on their top edge stays whole.
        const float padTop = ui.newPillHeight(true) * 0.5f + ui.s(2);
        const float rowGap = std::max(gap, ui.newPillHeight(true) * 0.5f + ui.s(5));  // room for the pill of the row below
        float gridH = padTop + std::ceil(items.size() / static_cast<float>(cols)) * (ch + rowGap);
        // A list longer than the room scrolls: its bar gets a lane of its own, beside the cards.
        float cardsW = gridA.w - (gridH - rowGap > gridA.bottom() - gy + padTop ? ui.s(14) : 0.0f);
        float cw = (cardsW - gap * (cols - 1)) / cols;
        ui.beginScroll("items", {gridA.x, gy - padTop, gridA.w, gridA.bottom() - gy + padTop}, gridH);
        float so = ui.scrollOffset();
        for (size_t i = 0; i < items.size(); ++i) {
            const CosmeticItem& it = *items[i];
            Rect c{gridA.x + (cw + gap) * static_cast<float>(i % cols), gy - so + (ch + rowGap) * static_cast<float>(i / cols), cw, ch};
            bool unlocked = cosmeticUnlocked(it, progress_);
            bool on = sel == &it || (sel && sel->id == it.id);
            if (ui.hotspot("item" + std::to_string(i), c)) selItem_ = it.id;
            // « NOUVEAU » (a reward added by an update): seen on focus (mouse over it too) or a click.
            const bool fresh = ui.newBadge("item" + std::to_string(i), c, "cos." + it.id, false, true);
            r.fillRoundedRect(c, ui.s(12), on ? ui.theme().accent.withAlpha(36) : Color{14, 20, 40, 235});
            r.drawRoundedRect(c, ui.s(12), on ? ui.theme().accent : ui.theme().accent.withAlpha(40), std::max(1.0f, ui.s(1.5f)));
            Rect thumb{c.x + ui.s(10), c.y + ui.s(10), c.w - ui.s(20), ui.s(84)};
            drawThumb(it, thumb, unlocked);
            ui.label(ui.elide(it.name, FontFace::Bold, 16, c.w - ui.s(20)), {c.x + ui.s(10), c.y + ui.s(102)}, 16,
                     unlocked ? ui.theme().text : ui.theme().textDim, Align::Left, FontFace::Bold);
            std::string sh = shortReq(it);
            ui.label(ui.elide(sh, FontFace::Body, fz(ui, 13), c.w - ui.s(20)), {c.x + ui.s(10), c.y + ui.s(128)}, fz(ui, 13),
                     unlocked ? kGreen : kGold);
            float pillRight = c.right() - ui.s(8);
            if (equipped == &it || (equipped && equipped->id == it.id)) {
                std::string e = "Équipé";
                float ew = ui.text().measure(e, FontFace::Bold, ui.s(fz(ui, 12))).x + ui.s(14);
                Rect er{c.right() - ew - ui.s(8), c.y + ui.s(8), ew, ui.s(20)};
                r.fillRoundedRect(er, er.h * 0.5f, ui.theme().accent);
                ui.label(e, er.center(), fz(ui, 12), Color{6, 32, 28, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
                pillRight = er.x - ui.s(6);
            }
            if (fresh) ui.drawNewPill(pillRight, c.y, true);  // on the top edge (left of « Équipé »)
        }
        ui.endScroll();
    }

    void drawThumb(const CosmeticItem& it, const Rect& box, bool unlocked) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        r.fillRoundedRect(box, ui.s(8), Color{11, 18, 38, 255});
        Color tint = unlocked ? Color::white() : Color{255, 255, 255, 90};
        if (it.category == "suit") {
            float s = std::min(box.h, box.w);
            drawNova(g_, it.look, "idle", t_, Rect::fromCenter(box.center(), {s, s}), tint);
        } else if (it.category == "frame") {
            float rad = box.h * 0.32f;
            online_.drawAvatar("avatar_01", "", Rect::fromCenter(box.center(), {rad * 2, rad * 2}));
            drawAvatarFrame(r, box.center(), rad, it.look, ui.s(0.8f), t_);
            if (!unlocked) r.fillRoundedRect(box, ui.s(8), Color{11, 18, 38, 150});
        } else if (it.category == "title") {
            drawTitleChip(ui, box.center(), it.isDefault ? "Sans titre" : it.look, 13, unlocked && !it.isDefault, box.w - ui.s(8), chipColor(&it));
        } else if (it.category == "trail") {
            // A sample of the trail, drawn as in the game, from left (oldest) to right.
            r.pushClip(box);
            drawTrailSample(r, it.look, box.expanded(-ui.s(4)), t_, unlocked ? 1.0f : 0.35f);
            r.popClip();
            if (it.look == "none") ui.label("—", box.center(), 20, ui.theme().textDim, Align::Center, FontFace::Bold, VAlign::Middle);
        } else if (it.category == "victory") {
            r.pushClip(box);
            if (it.look == "classic") r.drawGlow(box.center(), box.h * 0.4f, ui.theme().accent.withAlpha(unlocked ? 110 : 40));
            else drawVictoryFx(r, it.look, {box.center().x, box.bottom() - ui.s(6)}, std::fmod(t_, 2.4f) + 0.15f, false, box.h / 220.0f);
            if (!unlocked) r.fillRect(box, Color{11, 18, 38, 160});
            r.popClip();
        }
    }

    std::string achievementName(const std::string& id) const {
        const AchievementDef* a = g_.achievementCatalog.find(id);
        return a ? a->name : id;
    }
    /// 2.43: where a reward was won at a challenge (« Défi du jour du 20/11 »), "" when it was not.
    std::string wonText(const CosmeticItem& it) const {
        const WonReward* w = progress_.won ? progress_.won->origin(it.id) : nullptr;
        return w ? wonOriginText(*w) : std::string();
    }
    /// The catalogue's items of a category, then the server titles (2.43) or the suits created
    /// on Cosmic Online that were won.
    std::vector<const CosmeticItem*> itemsOf(const std::string& category) const {
        auto items = g_.cosmetics.items(category);
        if (category == "title" && progress_.won)
            for (const CosmeticItem& t : progress_.won->titles) items.push_back(&t);
        if (category == "suit" && progress_.won)
            for (const CosmeticItem& s : progress_.won->suits) items.push_back(&s);
        return items;
    }
    const CosmeticItem* findItem(const std::string& id) const {
        if (const CosmeticItem* it = g_.cosmetics.find(id)) return it;
        return progress_.won ? progress_.won->item(id) : nullptr;
    }
    std::string shortReq(const CosmeticItem& it) const {
        if (std::string w = wonText(it); !w.empty()) return w;
        bool unlocked = cosmeticUnlocked(it, progress_);
        if (!it.achievement.empty()) return unlocked ? "Succès obtenu" : "« " + achievementName(it.achievement) + " »";
        if (it.challengeReward()) return unlocked ? "Défis : obtenu" : "Défis : " + challengeConditionShort(it);
        if (it.isDefault) return "Dès le départ";
        return starsText(cosmeticThreshold(it, progress_.totalStars));
    }
    std::string reqText(const CosmeticItem& it) const {
        if (std::string w = wonText(it); !w.empty()) return "Gagné : " + w;
        if (!it.achievement.empty()) return "Récompense du succès « " + achievementName(it.achievement) + " »";
        if (it.challengeReward())
            return cosmeticUnlocked(it, progress_) ? "Récompense des défis : obtenue" : challengeProgressText(it, progress_);
        if (it.isDefault) return "Disponible dès le départ";
        if (it.allStars) return "Palier : toutes les étoiles (" + std::to_string(progress_.totalStars) + ")";
        return "Palier : " + std::to_string(it.stars) + " étoiles";
    }
    std::string lockText(const CosmeticItem& it) const {
        if (!it.achievement.empty()) return "Succès requis";
        if (it.challengeReward()) return "Défis requis";
        int missing = std::max(0, cosmeticThreshold(it, progress_.totalStars) - progress_.stars);
        return "Encore " + std::to_string(missing) + (missing > 1 ? " étoiles" : " étoile");
    }

    // ================================================================= Badges du profil (2.41)
    /// A wrapped text at the first of `sizes` whose lines end above `bottom` (nothing when even
    /// the smallest does not fit). Returns the height drawn.
    float wrappedFitting(const std::string& t, Vec2 pos, float width, std::initializer_list<float> sizes, float bottom, Color c) {
        UIContext& ui = g_.ui;
        for (float size : sizes) {
            float fs = fz(ui, size);
            float h = ui.text().measureWrapped(t, FontFace::Body, ui.s(fs), width).y;
            if (pos.y + h > bottom) continue;
            ui.labelWrapped(t, pos, width, fs, c);
            return h;
        }
        return 0.0f;
    }
    /// A text beside a button `ui.s(48)` high, centred on `cy`: on two lines at most, smaller
    /// rather than cut (elided only when even the smallest size needs more).
    void sideText(const std::string& t, float x, float cy, float room, Color c) {
        UIContext& ui = g_.ui;
        if (room <= ui.s(40)) return;
        for (float size : {15.0f, 13.5f, 12.0f}) {
            float fs = fz(ui, size), h = ui.text().measureWrapped(t, FontFace::Body, ui.s(fs), room).y;
            if (h > ui.s(48)) continue;
            ui.labelWrapped(t, {x, cy - h * 0.5f}, room, fs, c);
            return;
        }
        ui.label(ui.elide(t, FontFace::Body, fz(ui, 12), room), {x, cy}, fz(ui, 12), c, Align::Left, FontFace::Body, VAlign::Middle);
    }

    /// The achievements shown as badges on the online profile: a preview of the profile's
    /// header with its slots (as many as the stars open), the selected achievement with
    /// « Afficher sur le profil » / « Retirer du profil », and every achievement obtained.
    /// Below the first step: no slot, the steps and how far the stars are from them.
    void drawBadges(const Rect& area, float lw) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const AchievementCatalog& cat = g_.achievementCatalog;
        const int stars = progress_.stars;
        const int slots = cat.badgeSlots(stars);
        const BadgeSlotStep* next = cat.nextBadgeStep(stars);
        const std::vector<std::string> shown = g_.shownBadges();
        std::vector<const AchievementDef*> got;
        for (const auto& a : cat.achievements())
            if (g_.achievements.unlocked(a.id)) got.push_back(&a);
        auto shownIndex = [&](const std::string& id) {
            auto it = std::find(shown.begin(), shown.end(), id);
            return it == shown.end() ? -1 : static_cast<int>(it - shown.begin());
        };
        // The selected achievement: the one chosen, else the first badge, else the first obtained.
        const AchievementDef* sel = cat.find(selBadge_);
        if (!sel || !g_.achievements.unlocked(sel->id)) sel = !shown.empty() ? cat.find(shown.front()) : !got.empty() ? got.front() : nullptr;
        const float pw = std::min(ui.s(560), area.w * 0.38f);
        const Rect prev{area.x + lw + ui.s(24), area.y, pw, area.h * 0.68f};
        const Rect det{prev.x, prev.bottom() + ui.s(16), prev.w, area.bottom() - prev.bottom() - ui.s(16)};
        const Rect gridA{prev.right() + ui.s(24), area.y, area.right() - prev.right() - ui.s(24), area.h};
        const float px = prev.x + ui.s(22), pwIn = prev.w - ui.s(44);

        // ---- Preview: the header of the online profile (frame, name, title, badges).
        r.fillRoundedRectGradient(prev, ui.s(16), Color{17, 26, 54, 255}, Color{10, 15, 32, 255});
        r.drawRoundedRect(prev, ui.s(16), ui.theme().accent.withAlpha(80), std::max(1.0f, ui.s(1.5f)));
        float y = prev.y + ui.s(18);
        Vec2 hs = ui.label(ui.elide("Aperçu de votre profil", FontFace::Bold, 20, pwIn), {px, y}, 20, ui.theme().text, Align::Left, FontFace::Bold);
        y += hs.y + ui.s(16);
        const bool logged = g_.online.loggedIn();
        const float ar = ui.s(44), margin = ui.s(kAvatarFrameMargin);
        const Vec2 ac{px + margin + ar, y + margin + ar};
        online_.drawAvatar(logged ? jstr(g_.online.profile(), "avatar", "avatar_01") : std::string("avatar_01"), logged ? g_.online.playerId() : std::string(),
                           Rect::fromCenter(ac, {ar * 2, ar * 2}));
        drawAvatarFrame(r, ac, ar, g_.collection.look(g_.cosmetics, "frame", progress_), ui.s(1), t_);
        const float nx = ac.x + ar + margin + ui.s(14), room = prev.right() - ui.s(22) - nx;
        float ny = ac.y - ar + ui.s(12);
        {
            const std::string name = logged && !g_.online.username().empty() ? g_.online.username() : std::string("Nova");
            i18n::Verbatim v;  // a pseudonym as the player wrote it
            float nf = ui.fitFont(name, FontFace::Title, 26, room, 0.6f);
            ui.label(ui.elide(name, FontFace::Title, nf, room), {nx, ny}, nf, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        }
        ny += ui.s(34);
        if (std::string title = g_.collection.look(g_.cosmetics, "title", progress_); !title.empty()) {
            float tf = fz(ui, 15), tw = std::min(ui.text().measure(title, FontFace::Bold, ui.s(tf)).x + ui.s(24), room);
            Rect chip{nx, ny - ui.s(13), tw, ui.s(26)};
            const Color tc = chipColor(g_.collection.equipped(g_.cosmetics, "title", progress_));
            r.fillRoundedRect(chip, chip.h * 0.5f, tc.withAlpha(28));
            r.drawRoundedRect(chip, chip.h * 0.5f, tc.withAlpha(160), std::max(1.0f, ui.s(1.2f)));
            ui.label(ui.elide(title, FontFace::Bold, tf, tw - ui.s(24)), chip.center(), tf, tc, Align::Center, FontFace::Bold, VAlign::Middle);
            ny += ui.s(26);
        }
        // The slots: the badges shown, then the free places (none below the first step).
        const float slotGap = ui.s(10), slotD = std::min(ui.s(44), (room - slotGap * 2) / 3.0f);
        ny += ui.s(10);
        const float rowCy = ny + slotD * 0.5f;
        std::vector<const AchievementDef*> shownDefs;
        for (const auto& id : shown) shownDefs.push_back(cat.find(id));
        if (slots > 0) {
            int hit = slotsRow_.draw(g_, "slot", shownDefs, nx, rowCy, slotD, slotGap);
            if (hit >= 0 && shownDefs[static_cast<size_t>(hit)]) selBadge_ = shownDefs[static_cast<size_t>(hit)]->id;
            for (int i = static_cast<int>(shown.size()); i < slots; ++i) {
                Rect box{nx + static_cast<float>(i) * (slotD + slotGap), rowCy - slotD * 0.5f, slotD, slotD};
                const std::string name = "free" + std::to_string(i);
                bool hovered = false;
                if (ui.hotspot(name, box, &hovered)) {
                    // A free place: to the first achievement obtained that is not shown yet.
                    for (size_t k = 0; k < got.size(); ++k)
                        if (shownIndex(got[k]->id) < 0) {
                            selBadge_ = got[k]->id;
                            ui.setFocus("bdg" + std::to_string(k));
                            break;
                        }
                }
                drawBadgeSlot(ui, box.center(), slotD * 0.5f, hovered || ui.focused(name));
            }
            ny = rowCy + slotD * 0.5f;
        } else {
            Vec2 ls = ui.label(ui.elide("Aucun badge pour l'instant", FontFace::Body, fz(ui, 15), room), {nx, ny}, fz(ui, 15), ui.theme().textDim, Align::Left,
                               FontFace::Body);
            ny += ls.y;
        }
        y = std::max(ac.y + ar + margin, ny) + ui.s(20);
        ui.separator({px, y}, pwIn);
        y += ui.s(14);
        // Where the slots stand, and the next step (« 250 étoiles pour un 3e badge »).
        const std::string head = slots > 0 ? "Badges : " + std::to_string(shown.size()) + " / " + std::to_string(slots) + " emplacements utilisés"
                                           : std::string("Badges verrouillés");
        Vec2 hz = ui.label(head, {px, y}, ui.fitFont(head, FontFace::Bold, 18, pwIn), slots > 0 ? ui.theme().text : kGold, Align::Left, FontFace::Bold);
        y += hz.y + ui.s(8);
        std::string step;
        if (next && slots == 0)
            step = std::to_string(next->stars) + " étoiles pour choisir " + std::to_string(next->slots) + " badges (encore " + std::to_string(next->stars - stars) + ").";
        else if (next)
            step = std::to_string(next->stars) + " étoiles pour un " + std::to_string(next->slots) + "e badge (encore " + std::to_string(next->stars - stars) + ").";
        else if (slots > 0)
            step = "Toutes les places sont ouvertes : " + std::to_string(slots) + " badges au plus.";
        else
            step = "Les badges ne sont pas disponibles dans cette version.";
        y += wrappedFitting(step, {px, y}, pwIn, {15.0f, 13.5f, 12.0f}, prev.bottom() - ui.s(12), Color{143, 255, 240, 255}) + ui.s(10);
        if (next && y + ui.s(10) <= prev.bottom() - ui.s(12)) {
            // The stars toward the next step.
            Rect bar{px, y, pwIn, ui.s(10)};
            r.fillRoundedRect(bar, bar.h * 0.5f, Color{20, 28, 52, 255});
            float f = std::clamp(static_cast<float>(stars) / static_cast<float>(std::max(1, next->stars)), 0.0f, 1.0f);
            if (f > 0.0f) r.fillRoundedRect({bar.x, bar.y, std::max(bar.h, bar.w * f), bar.h}, bar.h * 0.5f, kGold);
            y += bar.h + ui.s(14);
        }
        // Published or not: without an account or offline, the choice stays here.
        if (slots > 0 || !logged) {
            std::string pub;
            Color pubC = ui.theme().textDim;
            if (!logged) {
                pub = "Sans compte ou hors ligne, votre choix reste sur cet ordinateur : il sera publié sur votre profil Cosmic Online une fois connecté.";
            } else {
                switch (g_.online.badgesState()) {
                    case OnlineService::BadgesState::Published:
                        pub = "Publiés sur votre profil Cosmic Online : visibles par ceux qui peuvent voir votre profil.";
                        pubC = kGreen;
                        break;
                    case OnlineService::BadgesState::Refused:
                        pub = "Refusés par Cosmic Online : " + g_.online.badgesError();
                        pubC = ui.theme().danger;
                        break;
                    default: pub = "En attente d'envoi vers votre profil Cosmic Online."; break;
                }
            }
            wrappedFitting(pub, {px, y}, pwIn, {14.5f, 13.0f, 11.5f}, prev.bottom() - ui.s(12), pubC);
        }

        // ---- The selected achievement: « Afficher sur le profil » / « Retirer du profil ».
        ui.panel(det, false, 14);
        const float dx = det.x + ui.s(20), dwIn = det.w - ui.s(40);
        const float by = det.bottom() - ui.s(66);
        if (slots == 0) {
            float dy = det.y + ui.s(18);
            dy += wrappedFitting("Un badge est un succès que vous avez obtenu, affiché à côté de votre nom et de votre titre sur votre profil Cosmic Online.",
                                 {dx, dy}, dwIn, {16.0f, 14.0f, 12.5f}, by - ui.s(40), Color{185, 200, 218, 255}) +
                  ui.s(10);
            std::string steps;
            for (const auto& st : cat.badgeSlotSteps())
                steps += (steps.empty() ? "" : "  ·  ") + std::to_string(st.stars) + " étoiles : " + std::to_string(st.slots) + " badges";
            if (!steps.empty() && dy + ui.s(24) <= by - ui.s(6))
                ui.label(ui.elide(steps, FontFace::Bold, fz(ui, 15), dwIn), {dx, dy}, fz(ui, 15), kGold, Align::Left, FontFace::Bold);
            if (next) {
                const int missing = next->stars - stars;
                std::string lock = "Encore " + std::to_string(missing) + (missing > 1 ? " étoiles" : " étoile");
                float lw2 = ui.text().measure(lock, FontFace::Bold, ui.s(17)).x + ui.s(36);
                Rect lr{dx, by, lw2, ui.s(48)};
                r.fillRoundedRect(lr, ui.s(10), kGold.withAlpha(26));
                ui.label(lock, lr.center(), 17, kGold, Align::Center, FontFace::Bold, VAlign::Middle);
                sideText("Les étoiles viennent des succès (menu principal → Succès).", lr.right() + ui.s(16), by + ui.s(24),
                         det.right() - ui.s(20) - (lr.right() + ui.s(16)), ui.theme().textDim);
            }
        } else if (!sel) {
            ui.labelWrapped("Aucun succès obtenu pour l'instant : jouez, puis revenez choisir vos badges.", {dx, det.y + ui.s(18)}, dwIn, fz(ui, 16),
                            ui.theme().textDim);
        } else {
            const AchievementCategory* k = cat.category(sel->category);
            const float er = ui.s(30);
            const Vec2 ec{dx + er, det.y + ui.s(18) + er};
            drawBadgeEmblem(g_, *sel, ec, er);
            const float tx = ec.x + er + ui.s(16), troom = det.right() - ui.s(20) - tx;
            const float nf = ui.fitFont(sel->name, FontFace::Bold, 24, troom, 0.6f);
            ui.label(ui.elide(sel->name, FontFace::Bold, nf, troom), {tx, ec.y - ui.s(12)}, nf, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            Vec2 cs = ui.label(ui.elide(k ? k->name : sel->category, FontFace::Bold, fz(ui, 15), troom * 0.5f), {tx, ec.y + ui.s(16)}, fz(ui, 15),
                               k ? k->color : ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
            for (int i = 0; i < sel->stars; ++i) drawStar(r, {tx + cs.x + ui.s(18) + ui.s(16) * static_cast<float>(i), ec.y + ui.s(16)}, ui.s(7), kGold);
            // The description, smaller when it would reach the button.
            wrappedFitting(sel->description, {dx, ec.y + er + ui.s(14)}, dwIn, {16.0f, 14.0f, 12.5f}, by - ui.s(8), Color{185, 200, 218, 255});
            const int at = shownIndex(sel->id);
            UIContext::ButtonStyle st;
            std::string label, side;
            if (at >= 0) {
                label = "Retirer du profil";
                st.selected = true;
                side = "Badge " + std::to_string(at + 1) + " sur " + std::to_string(slots) + " de votre profil";
            } else {
                label = "Afficher sur le profil";
                st.primary = true;
                st.enabled = static_cast<int>(shown.size()) < slots;
                side = st.enabled ? "Il prendra l'emplacement " + std::to_string(shown.size() + 1) + " sur " + std::to_string(slots) + "."
                                  : "Les " + std::to_string(slots) + " emplacements sont pris : retirez d'abord un badge.";
            }
            const float bw = std::min(dwIn * 0.55f, ui.text().measure(label, FontFace::Bold, ui.s(20)).x + ui.s(48));
            st.fontSize = ui.fitFont(label, FontFace::Bold, 20, bw - ui.s(24));
            if (ui.button("badgeact", {dx, by, bw, ui.s(48)}, label, st)) {
                std::vector<std::string> chosen = shown;
                if (at >= 0) chosen.erase(chosen.begin() + at);
                else chosen.push_back(sel->id);
                setBadges(std::move(chosen));
            }
            // Beside the button, on two lines at most (smaller rather than cut).
            const float sx = dx + bw + ui.s(16);
            sideText(side, sx, by + ui.s(24), det.right() - ui.s(20) - sx, at >= 0 || st.enabled ? ui.theme().textDim : kGold);
        }

        // ---- Right: the achievements obtained (locked: the steps).
        if (slots == 0) {
            ui.label("Paliers des badges", {gridA.x, gridA.y + ui.s(12)}, 21, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            float gy = gridA.y + ui.s(40);
            for (const auto& st : cat.badgeSlotSteps()) {
                Rect c{gridA.x, gy, gridA.w, ui.s(78)};
                const bool reached = stars >= st.stars;
                r.fillRoundedRect(c, ui.s(12), Color{14, 20, 40, 235});
                r.drawRoundedRect(c, ui.s(12), reached ? kGreen.withAlpha(120) : kGold.withAlpha(110), std::max(1.0f, ui.s(1.5f)));
                // A medal with the number of badges the step opens.
                const float mr = ui.s(24);
                const Vec2 mc{c.x + ui.s(18) + mr, c.center().y};
                const Color mcol = reached ? kGreen : kGold;
                r.fillCircle(mc, mr, Color{10, 16, 34, 255});
                r.drawCircle(mc, mr - ui.s(1), mcol.withAlpha(200), std::max(1.0f, ui.s(2.0f)));
                const std::string num = std::to_string(st.slots);
                ui.label(num, mc, ui.fitFont(num, FontFace::Title, 24, mr * 1.4f), mcol, Align::Center, FontFace::Title, VAlign::Middle);
                const float tx = mc.x + mr + ui.s(18);
                const float troom = c.right() - ui.s(16) - tx;
                const std::string what = std::to_string(st.slots) + " badges sur le profil";
                const std::string when = std::to_string(st.stars) + " étoiles" + (reached ? std::string(" · atteint") : " · encore " + std::to_string(st.stars - stars));
                ui.label(ui.elide(what, FontFace::Bold, 18, troom), {tx, c.center().y - ui.s(12)}, 18, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
                ui.label(ui.elide(when, FontFace::Body, fz(ui, 15), troom), {tx, c.center().y + ui.s(14)}, fz(ui, 15), reached ? kGreen : kGold, Align::Left,
                         FontFace::Body, VAlign::Middle);
                gy += c.h + ui.s(12);
            }
            const std::string mine = got.empty() ? std::string("Vous n'avez encore obtenu aucun succès.")
                                                 : "Vos " + std::to_string(got.size()) + " succès obtenus pourront être choisis comme badges.";
            wrappedFitting(mine, {gridA.x, gy + ui.s(4)}, gridA.w, {15.0f, 13.0f}, gridA.bottom(), ui.theme().textDim);
            return;
        }
        Vec2 gh = ui.label("Succès obtenus", {gridA.x, gridA.y + ui.s(12)}, 21, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        const std::string count = std::to_string(got.size()) + " / " + std::to_string(cat.achievements().size());
        const float countRoom = gridA.w - gh.x - ui.s(16);
        if (countRoom > ui.s(30))
            ui.label(ui.elide(count, FontFace::Body, fz(ui, 15), countRoom), {gridA.right(), gridA.y + ui.s(12)}, fz(ui, 15), ui.theme().textDim, Align::Right,
                     FontFace::Body, VAlign::Middle);
        const float gy = gridA.y + ui.s(36);
        if (got.empty()) {
            ui.labelWrapped("Aucun succès obtenu pour l'instant (menu principal → Succès).", {gridA.x, gy}, gridA.w, fz(ui, 16), ui.theme().textDim);
            slotsRow_.drawTooltip(g_, "sélectionner ce badge");
            return;
        }
        const float gap = ui.s(10), ch = ui.s(70);
        const int cols = std::clamp(static_cast<int>((gridA.w + gap) / (ui.s(250) + gap)), 1, 4);
        const float rows = std::ceil(static_cast<float>(got.size()) / static_cast<float>(cols));
        const float gridH = rows * (ch + gap);
        // A list longer than the room scrolls: its bar gets a lane of its own, beside the cards.
        const float cardsW = gridA.w - (gridH > gridA.bottom() - gy ? ui.s(14) : 0.0f);
        const float cw = (cardsW - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        ui.beginScroll("badges", {gridA.x, gy, gridA.w, gridA.bottom() - gy}, gridH);
        const float so = ui.scrollOffset();
        for (size_t i = 0; i < got.size(); ++i) {
            const AchievementDef& a = *got[i];
            Rect c{gridA.x + (cw + gap) * static_cast<float>(i % static_cast<size_t>(cols)), gy - so + (ch + gap) * static_cast<float>(i / static_cast<size_t>(cols)), cw,
                   ch};
            const std::string name = "bdg" + std::to_string(i);
            if (ui.hotspot(name, c)) selBadge_ = a.id;
            if (c.bottom() < gy || c.y > gridA.bottom()) continue;
            const bool on = sel == &a, focus = ui.focused(name);
            const int at = shownIndex(a.id);
            r.fillRoundedRect(c, ui.s(10), on ? ui.theme().accent.withAlpha(36) : Color{14, 20, 40, 235});
            r.drawRoundedRect(c, ui.s(10), on || focus ? ui.theme().accent : at >= 0 ? kGreen.withAlpha(150) : ui.theme().accent.withAlpha(40),
                              std::max(1.0f, ui.s(on || focus ? 2.0f : 1.4f)));
            const float er = ui.s(22);
            drawBadgeEmblem(g_, a, {c.x + ui.s(10) + er, c.center().y}, er);
            const float tx = c.x + ui.s(10) + er * 2 + ui.s(12);
            // « Badge 2 » at the top right of a shown one; the name in what is left.
            float pillW = 0.0f;
            if (at >= 0) {
                const std::string p = "Badge " + std::to_string(at + 1);
                const float pw2 = ui.text().measure(p, FontFace::Bold, ui.s(fz(ui, 12))).x + ui.s(14);
                Rect pr{c.right() - pw2 - ui.s(8), c.y + ui.s(10), pw2, ui.s(20)};
                r.fillRoundedRect(pr, pr.h * 0.5f, kGreen);
                ui.label(p, pr.center(), fz(ui, 12), Color{8, 36, 18, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
                pillW = pw2 + ui.s(12);
            }
            const float nroom = c.right() - ui.s(10) - tx - pillW;
            ui.label(ui.elide(a.name, FontFace::Bold, 16, nroom), {tx, c.y + ui.s(20)}, 16, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            const AchievementCategory* k = cat.category(a.category);
            const float subRoom = c.right() - ui.s(10) - tx;
            Vec2 ks = ui.label(ui.elide(k ? k->name : a.category, FontFace::Body, fz(ui, 13), subRoom * 0.55f), {tx, c.y + ui.s(48)}, fz(ui, 13),
                               k ? k->color : ui.theme().accent, Align::Left, FontFace::Body, VAlign::Middle);
            // Its stars after the category, as many as fit.
            for (int s2 = 0; s2 < a.stars; ++s2) {
                const float sx = tx + ks.x + ui.s(14) + ui.s(13) * static_cast<float>(s2);
                if (sx + ui.s(6) > c.right() - ui.s(8)) break;
                drawStar(r, {sx, c.y + ui.s(48)}, ui.s(5.5f), kGold);
            }
        }
        ui.endScroll();
        // The tooltip of a badge of the preview, over everything.
        slotsRow_.drawTooltip(g_, "sélectionner ce badge");
    }

    void setBadges(std::vector<std::string> ids) {
        g_.collection.badges = std::move(ids);
        g_.collection.badgesChosen = true;
        g_.collection.save();
        g_.syncBadges();
        g_.uiSound("ui_confirm", 0.6f);
    }

    // ================================================================= Route des étoiles
    /// Opens the Vestiaire on an item (its category, the item selected).
    void showItem(const std::string& id) {
        const CosmeticItem* it = findItem(id);
        if (!it) return;
        const auto& cats = g_.cosmetics.categories();
        for (size_t i = 0; i < cats.size(); ++i)
            if (cats[i].id == it->category) cat_ = static_cast<int>(i);
        selItem_ = id;
    }

    void drawRoute(const Rect& area) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        auto ms = g_.cosmetics.milestones(progress_.totalStars);
        const int total = std::max(1, progress_.totalStars);
        const CosmeticCatalog::Milestone* next = nullptr;
        for (const auto& m : ms)
            if (m.stars > progress_.stars) {
                next = &m;
                break;
            }
        auto namesOf = [](const CosmeticCatalog::Milestone& m) {
            std::string s;
            for (size_t i = 0; i < m.items.size(); ++i) s += (i ? (i + 1 == m.items.size() ? " et " : ", ") : "") + m.items[i]->name;
            return s;
        };
        // Progress band.
        Rect band{area.x, area.y, area.w, ui.s(104)};
        ui.panel(band, false, 14);
        float bx = band.x + ui.s(20), bw = band.w - ui.s(40);
        Vec2 sz = ui.label(std::to_string(progress_.stars), {bx, band.y + ui.s(30)}, 24, kGold, Align::Left, FontFace::Bold, VAlign::Middle);
        std::string lead = " étoiles sur " + std::to_string(progress_.totalStars) + " · rien ne s'achète, tout se gagne avec les succès";
        std::string nx = next ? "Prochain palier : " + starsText(next->stars) + ", " + namesOf(*next) + " (encore " +
                                    std::to_string(next->stars - progress_.stars) + ")"
                              : "Toutes les récompenses des paliers sont débloquées";
        // Both sentences on one line, each in its own half (the left one shrinks first).
        float nxRoom = bw * 0.45f;
        float nxSize = ui.fitFont(nx, FontFace::Body, fz(ui, 16), nxRoom);
        float nxW = ui.text().measure(nx, FontFace::Body, ui.s(nxSize)).x;
        float leadRoom = bw - sz.x - nxW - ui.s(24);
        ui.label(lead, {bx + sz.x, band.y + ui.s(30)}, ui.fitFont(lead, FontFace::Body, fz(ui, 17), leadRoom), ui.theme().text, Align::Left, FontFace::Body,
                 VAlign::Middle);
        ui.label(nx, {bx + bw, band.y + ui.s(30)}, nxSize, Color{143, 255, 240, 255}, Align::Right, FontFace::Body, VAlign::Middle);
        Rect bar{bx, band.y + ui.s(56), bw, ui.s(12)};
        r.fillRoundedRect(bar, bar.h * 0.5f, Color{20, 28, 52, 255});
        float fill = std::clamp(progress_.stars / static_cast<float>(total), 0.0f, 1.0f);
        if (fill > 0.0f) r.fillRoundedRect({bar.x, bar.y, bar.w * fill, bar.h}, bar.h * 0.5f, kGold);
        for (const auto& m : ms) {
            float tx = bar.x + bar.w * std::clamp(m.stars / static_cast<float>(total), 0.0f, 1.0f);
            r.fillRect({tx - ui.s(1), bar.y - ui.s(4), ui.s(2), bar.h + ui.s(8)}, progress_.stars >= m.stars ? kGold : Color{140, 160, 185, 150});
        }
        for (int v : {0, total / 4, total / 2, total * 3 / 4, total}) {
            float tx = bar.x + bar.w * v / static_cast<float>(total);
            Align a = v == 0 ? Align::Left : v == total ? Align::Right : Align::Center;
            ui.label(std::to_string(v), {tx, bar.bottom() + ui.s(14)}, fz(ui, 13), ui.theme().textDim, a, FontFace::Body, VAlign::Middle);
        }

        // Milestones, then the rewards of a precise achievement, in one scrolling list (mouse
        // wheel; the keyboard and the gamepad go from reward to reward). A reward opens in the
        // Vestiaire.
        std::vector<const CosmeticItem*> ach, chal;  // 2.35: then those of the challenges
        for (const auto& it : g_.cosmetics.items()) {
            if (!it.achievement.empty()) ach.push_back(&it);
            if (it.challengeReward()) chal.push_back(&it);
        }
        auto cardText = [&](const CosmeticItem* it) {
            if (it->challengeReward()) return challengeConditionText(*it);
            const AchievementDef* a = g_.achievementCatalog.find(it->achievement);
            return a ? a->description : std::string();
        };
        Rect view{area.x, band.bottom() + ui.s(16), area.w, area.bottom() - band.bottom() - ui.s(16)};
        // Under each heading, room for a « NOUVEAU » on the top edge of the first cards.
        const float gap = ui.s(12), inner = view.w - ui.s(16), headH = ui.s(40) + ui.newPillHeight(true) * 0.5f;
        // Between two rows of cards, room for the « NOUVEAU » of the lower one (above its top edge).
        const float rowGap = std::max(gap, ui.newPillHeight(true) * 0.5f + ui.s(7));
        // Milestone cards: as many columns as fit (at least ~170 px each), their height from
        // the longest list of names.
        const int cols = std::clamp(static_cast<int>((inner + gap) / (ui.s(170) + gap)), 3, 10);
        const float cw = (inner - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        const float thumbH = ui.s(72), namesTop = ui.s(40) + thumbH + ui.s(8);
        float namesH = 0.0f;
        for (const auto& m : ms) namesH = std::max(namesH, ui.text().measureWrapped(namesOf(m), FontFace::Body, ui.s(fz(ui, 14)), cw - ui.s(20)).y);
        // The last card (milestones reached) keeps its sentence inside too.
        float summaryH = ui.s(80) + ui.text().measureWrapped("Les étoiles déjà gagnées comptent aussi.", FontFace::Body, ui.s(fz(ui, 13)), cw - ui.s(24)).y;
        const float ch = std::max(namesTop + namesH, summaryH) + ui.s(12);
        const size_t cells = ms.size() + 1;
        const float rows = std::ceil(static_cast<float>(cells) / static_cast<float>(cols));
        // Achievement cards: wide enough for the longest title's chip (its font never goes
        // below what stays readable).
        float cardMin = ui.s(210);
        for (const auto* list : {&ach, &chal})
            for (const CosmeticItem* it : *list)
                if (it->category == "title")
                    cardMin = std::max(cardMin, ui.text().measure(it->look, FontFace::Bold, ui.s(fz(ui, 13))).x + ui.s(22 + 16 + 12));
        const int acols = std::clamp(static_cast<int>((inner + gap) / (cardMin + gap)), 2, 8);
        const float aw = (inner - gap * static_cast<float>(acols - 1)) / static_cast<float>(acols);
        // Both sections' cards are as high as the longest text of either.
        float descH = 0.0f;
        for (const auto* list : {&ach, &chal})
            for (const CosmeticItem* it : *list)
                descH = std::max(descH, ui.text().measureWrapped(cardText(it), FontFace::Body, ui.s(fz(ui, 12.5f)), aw - ui.s(20)).y);
        const float ah = ui.s(80) + descH + ui.s(36);
        const float arows = std::ceil(static_cast<float>(ach.size()) / static_cast<float>(acols));
        const float crows = std::ceil(static_cast<float>(chal.size()) / static_cast<float>(acols));
        float content = headH + rows * (ch + rowGap);
        if (!ach.empty()) content += ui.s(10) + headH + arows * (ah + rowGap);
        if (!chal.empty()) content += ui.s(10) + headH + crows * (ah + rowGap);
        if (routeEnd_) ui.setScroll("route", 1e9f);
        routeEnd_ = false;
        ui.beginScroll("route", view, content);
        float y = view.y - ui.scrollOffset();
        auto visible = [&](const Rect& c) { return c.bottom() >= view.y && c.y <= view.bottom(); };
        ui.label("Paliers", {view.x, y + ui.s(4)}, 21, ui.theme().text, Align::Left, FontFace::Bold);
        y += headH;
        int reached = 0;
        for (const auto& m : ms) reached += progress_.stars >= m.stars ? 1 : 0;
        for (size_t i = 0; i < cells; ++i) {
            Rect c{view.x + (cw + gap) * static_cast<float>(i % static_cast<size_t>(cols)), y + (ch + rowGap) * static_cast<float>(i / static_cast<size_t>(cols)), cw, ch};
            if (i == ms.size()) {
                if (!visible(c)) continue;
                r.drawRoundedRect(c, ui.s(12), kGold.withAlpha(110), std::max(1.0f, ui.s(1.5f)));
                ui.label("Paliers atteints", {c.x + ui.s(12), c.y + ui.s(14)}, fz(ui, 13), ui.theme().textDim);
                ui.label(std::to_string(reached) + " / " + std::to_string(ms.size()), {c.x + ui.s(12), c.y + ui.s(36)}, 28, kGold, Align::Left, FontFace::Bold);
                ui.labelWrapped("Les étoiles déjà gagnées comptent aussi.", {c.x + ui.s(12), c.y + ui.s(80)}, c.w - ui.s(24), fz(ui, 13), ui.theme().textDim);
                continue;
            }
            const auto& m = ms[i];
            bool got = progress_.stars >= m.stars, isNext = next == &m;
            // Thumbnails of its rewards side by side: each one opens in the Vestiaire.
            Rect th{c.x + ui.s(8), c.y + ui.s(40), c.w - ui.s(16), thumbH};
            float each = th.w / static_cast<float>(m.items.size());
            std::vector<Rect> thumbs;
            std::vector<size_t> fresh;  // its rewards added by an update, not seen yet
            for (size_t k = 0; k < m.items.size(); ++k) {
                Rect t{th.x + each * static_cast<float>(k) + (k ? ui.s(3) : 0.0f), th.y, each - (m.items.size() > 1 ? ui.s(3) : 0.0f), th.h};
                thumbs.push_back(t);
                bool open = ui.hotspot("ms" + m.items[k]->id, t);
                if (ui.newBadge("ms" + m.items[k]->id, t, "cos." + m.items[k]->id, false, true)) fresh.push_back(k);
                if (open) {
                    showItem(m.items[k]->id);
                    tab_ = 0;
                    g_.ui.setFocus("tab0");
                }
            }
            if (!visible(c)) continue;
            r.fillRoundedRect(c, ui.s(12), isNext ? ui.theme().accent.withAlpha(30) : Color{14, 20, 40, 235});
            r.drawRoundedRect(c, ui.s(12), got ? kGreen.withAlpha(120) : isNext ? ui.theme().accent : ui.theme().accent.withAlpha(36),
                              std::max(1.0f, ui.s(1.5f)));
            std::string badge = starsText(m.stars);
            float bwid = ui.text().measure(badge, FontFace::Bold, ui.s(fz(ui, 13))).x + ui.s(16);
            Rect br{c.x + ui.s(10), c.y + ui.s(10), bwid, ui.s(22)};
            if (got) r.fillRoundedRect(br, br.h * 0.5f, kGold);
            else r.drawRoundedRect(br, br.h * 0.5f, kGold.withAlpha(150), std::max(1.0f, ui.s(1.2f)));
            ui.label(badge, br.center(), fz(ui, 13), got ? Color{42, 32, 6, 255} : kGold, Align::Center, FontFace::Bold, VAlign::Middle);
            std::string state = got ? "obtenu" : isNext ? "encore " + std::to_string(m.stars - progress_.stars) : "";
            float room = c.right() - br.right() - ui.s(18);
            if (!state.empty() && ui.text().measure(state, FontFace::Bold, ui.s(fz(ui, 12))).x <= room) {
                ui.label(state, {c.right() - ui.s(10), br.center().y}, fz(ui, 12), got ? kGreen : Color{143, 255, 240, 255}, Align::Right, FontFace::Bold,
                         VAlign::Middle);
            } else if (got) {
                // No room for the word: a green tick.
                Vec2 k{c.right() - ui.s(18), br.center().y};
                r.drawLine({k.x - ui.s(6), k.y}, {k.x - ui.s(2), k.y + ui.s(4)}, kGreen, ui.s(2.5f));
                r.drawLine({k.x - ui.s(2), k.y + ui.s(4)}, {k.x + ui.s(6), k.y - ui.s(5)}, kGreen, ui.s(2.5f));
            } else if (isNext) {
                std::string plus = "+" + std::to_string(m.stars - progress_.stars);
                if (ui.text().measure(plus, FontFace::Bold, ui.s(fz(ui, 12))).x <= room)
                    ui.label(plus, {c.right() - ui.s(10), br.center().y}, fz(ui, 12), Color{143, 255, 240, 255}, Align::Right, FontFace::Bold, VAlign::Middle);
            }
            for (size_t k = 0; k < m.items.size(); ++k) {
                drawThumb(*m.items[k], thumbs[k], got || isNext);
                if (ui.focused("ms" + m.items[k]->id)) r.drawRoundedRect(thumbs[k], ui.s(8), ui.theme().accent, std::max(1.0f, ui.s(2.0f)));
                // Which of several rewards is the new one: its thumbnail outlined in the pill's gold.
                if (m.items.size() > 1 && std::find(fresh.begin(), fresh.end(), k) != fresh.end())
                    r.drawRoundedRect(thumbs[k].expanded(-ui.s(2)), ui.s(7), Color{255, 196, 70, 220}, std::max(1.0f, ui.s(1.5f)));
            }
            ui.labelWrapped(namesOf(m), {c.x + ui.s(10), c.y + namesTop}, c.w - ui.s(20), fz(ui, 14), got || isNext ? ui.theme().text : ui.theme().textDim);
            // « NOUVEAU » on the card's top edge, at the right (over its frame, never over its text).
            if (!fresh.empty()) ui.drawNewPill(c.right() - ui.s(8), c.y - ui.s(3), true);
        }
        y += rows * (ch + rowGap);
        // Rewards of a precise achievement, then (2.35) those of the challenges: the same cards,
        // the achievement's stars or the tier asked for at their bottom.
        auto section = [&](const std::vector<const CosmeticItem*>& list, const std::string& head, const std::string& sub, const std::string& prefix,
                           float sectionRows) {
            if (list.empty()) return;
            y += ui.s(10);
            ui.label(head, {view.x, y + ui.s(4)}, 21, ui.theme().text, Align::Left, FontFace::Bold);
            float hw = ui.text().measure(head, FontFace::Bold, ui.s(21)).x;
            float subRoom = inner - hw - ui.s(14);
            if (subRoom > ui.s(120))
                ui.label(ui.elide(sub, FontFace::Body, fz(ui, 14), subRoom), {view.x + hw + ui.s(14), y + ui.s(8)}, fz(ui, 14), ui.theme().textDim);
            y += headH;
            for (size_t i = 0; i < list.size(); ++i) {
                const CosmeticItem& it = *list[i];
                bool got = cosmeticUnlocked(it, progress_);
                Rect c{view.x + (aw + gap) * static_cast<float>(i % static_cast<size_t>(acols)), y + (ah + rowGap) * static_cast<float>(i / static_cast<size_t>(acols)),
                       aw, ah};
                bool open = ui.hotspot(prefix + it.id, c);
                const bool fresh = ui.newBadge(prefix + it.id, c, "cos." + it.id, false, true);
                if (open) {
                    showItem(it.id);
                    tab_ = 0;
                    g_.ui.setFocus("tab0");
                }
                if (!visible(c)) continue;
                bool on = ui.focused(prefix + it.id);
                r.fillRoundedRect(c, ui.s(12), on ? ui.theme().accent.withAlpha(36) : Color{14, 20, 40, 235});
                r.drawRoundedRect(c, ui.s(12), on ? ui.theme().accent : got ? kGreen.withAlpha(120) : ui.theme().accent.withAlpha(36), std::max(1.0f, ui.s(1.5f)));
                drawThumb(it, {c.x + ui.s(8), c.y + ui.s(8), c.w - ui.s(16), ui.s(46)}, got);
                std::string nm = it.category == "suit"    ? "Combinaison " + it.name
                                 : it.category == "trail"  ? "Traînée " + it.name
                                 : it.category == "frame"  ? "Cadre " + it.name
                                 : it.category == "victory" ? "Effet " + it.name
                                                            : std::string("Titre");
                ui.label(ui.elide(nm, FontFace::Bold, 15, c.w - ui.s(20)), {c.x + ui.s(10), c.y + ui.s(58)}, 15, ui.theme().text, Align::Left, FontFace::Bold);
                ui.labelWrapped(cardText(&it), {c.x + ui.s(10), c.y + ui.s(80)}, c.w - ui.s(20), fz(ui, 12.5f), ui.theme().textDim);
                const std::string state = got ? "obtenu" : "à obtenir";
                const float stateW = ui.text().measure(state, FontFace::Bold, ui.s(fz(ui, 12))).x;
                if (it.challengeReward()) {
                    // The tier's medal, then how far my verified results go (« 3 / 10 »).
                    Vec2 mc{c.x + ui.s(16), c.bottom() - ui.s(16)};
                    r.fillCircle(mc, ui.s(7.5f), Color{8, 12, 26, 255});
                    r.fillCircle(mc, ui.s(6), challengeTierColor(challengeRewardTier(it)));
                    auto [have, need] = cosmeticChallengeProgress(it, progress_);
                    std::string pr = std::to_string(have) + " / " + std::to_string(need);
                    float room = c.w - ui.s(20) - ui.s(18) - stateW - ui.s(12);
                    if (!got && room > ui.s(20))
                        ui.label(ui.elide(pr, FontFace::Bold, fz(ui, 12), room), {mc.x + ui.s(14), mc.y}, fz(ui, 12), ui.theme().text, Align::Left,
                                 FontFace::Bold, VAlign::Middle);
                } else {
                    const AchievementDef* a = g_.achievementCatalog.find(it.achievement);
                    int st = a ? a->stars : 0;
                    for (int s = 0; s < st; ++s) drawStar(r, {c.x + ui.s(16) + ui.s(14) * s, c.bottom() - ui.s(16)}, ui.s(6), got ? kGold : kGold.withAlpha(110));
                }
                ui.label(state, {c.right() - ui.s(10), c.bottom() - ui.s(16)}, fz(ui, 12), got ? kGreen : ui.theme().textDim, Align::Right, FontFace::Bold,
                         VAlign::Middle);
                if (fresh) ui.drawNewPill(c.right() - ui.s(8), c.y - ui.s(3), true);
            }
            y += sectionRows * (ah + rowGap);
        };
        section(ach, "Récompenses de succès précis", "Elles ne dépendent pas du nombre d'étoiles : il faut le succès lui-même.", "ach", arows);
        section(chal, "Récompenses des défis", "Menu principal → Défis : seules les parties vérifiées par Cosmic Online comptent.", "chal", crows);
        ui.endScroll();
    }

    // ================================================================= Musiques
    void loadTracks() {
        tracks_.clear();
        auto j = FileUtil::loadJson(Paths::dataDir() / "music" / "tracks.json");
        if (!j || !j->is_object()) return;
        std::map<std::string, std::pair<std::string, std::string>> groups;  // id → name, planet
        if (auto it = j->find("groups"); it != j->end() && it->is_array())
            for (const auto& gr : *it) {
                groups[jstr(gr, "id")] = {jstr(gr, "name"), jstr(gr, "planet")};
                groupOrder_.push_back(jstr(gr, "id"));
            }
        if (auto it = j->find("tracks"); it != j->end() && it->is_array())
            for (const auto& t : *it) {
                Track tr{jstr(t, "file"), jstr(t, "title"), jstr(t, "where"), jstr(t, "group"), ""};
                if (tr.file.empty() || g_.app.resources().soundPath(tr.file).empty()) continue;  // only tracks shipped
                tr.planet = groups.count(tr.group) ? groups[tr.group].second : "noyau";
                tracks_.push_back(std::move(tr));
            }
        for (const auto& [id, gp] : groups) groupNames_[id] = gp.first;
    }
    bool heard(const Track& t) const { return g_.collection.heard.count(t.file) > 0; }
    void play(const Track& t) {
        selTrack_ = t.file;
        g_.app.audio().playMusic(g_.app.resources().soundPath(t.file), 400, loop_);
        playing_ = true;
    }
    void step(int dir, bool automatic) {
        std::vector<const Track*> open;
        for (const auto& t : tracks_)
            if (heard(t)) open.push_back(&t);
        if (open.empty()) return;
        size_t idx = 0;
        for (size_t i = 0; i < open.size(); ++i)
            if (open[i]->file == selTrack_) idx = i;
        if (shuffle_ && open.size() > 1) {
            // Any other heard track (display-only randomness).
            size_t k = static_cast<size_t>(SDL_GetTicks() % (open.size() - 1));
            idx = (idx + 1 + k) % open.size();
        } else {
            idx = (idx + open.size() + static_cast<size_t>(dir + static_cast<int>(open.size()))) % open.size();
        }
        (void)automatic;
        play(*open[idx]);
    }
    static std::string mmss(int64_t ms) {
        int s = static_cast<int>(ms / 1000);
        char b[16];
        std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
        return b;
    }

    void drawMusic(const Rect& area) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        AudioSystem& au = g_.app.audio();
        durationRead_ = false;
        int open = 0;
        for (const auto& t : tracks_) open += heard(t) ? 1 : 0;
        const Track* cur = nullptr;
        for (const auto& t : tracks_)
            if (t.file == selTrack_ && heard(t)) cur = &t;
        if (!cur)
            for (const auto& t : tracks_)
                if (heard(t)) {
                    cur = &t;
                    selTrack_ = t.file;
                    break;
                }
        float pw = std::min(ui.s(470), area.w * 0.32f);
        Rect list{area.x, area.y, area.w - pw - ui.s(22), area.h};
        Rect player{list.right() + ui.s(22), area.y, pw, area.h};
        // List.
        Vec2 hs = ui.label("Morceaux", {list.x, list.y + ui.s(12)}, 20, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        ui.label(std::to_string(open) + " / " + std::to_string(tracks_.size()) + " débloqués", {list.x + hs.x + ui.s(10), list.y + ui.s(13)}, fz(ui, 15),
                 ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        std::string hint = "Un morceau se débloque la première fois que vous l'entendez en jeu.";
        ui.label(ui.elide(hint, FontFace::Body, fz(ui, 14), list.w * 0.55f), {list.right(), list.y + ui.s(13)}, fz(ui, 14), ui.theme().textDim, Align::Right,
                 FontFace::Body, VAlign::Middle);
        float colN = ui.s(40), colDur = ui.s(64);
        float colTitle = (list.w - colN - colDur - ui.s(36)) * 0.45f;
        float hy = list.y + ui.s(40);
        ui.label("N°", {list.x + ui.s(12), hy}, fz(ui, 12.5f), ui.theme().textDim);
        ui.label("Titre", {list.x + ui.s(12) + colN, hy}, fz(ui, 12.5f), ui.theme().textDim);
        ui.label("Où l'entendre", {list.x + ui.s(24) + colN + colTitle, hy}, fz(ui, 12.5f), ui.theme().textDim);
        ui.label("Durée", {list.right() - ui.s(12), hy}, fz(ui, 12.5f), ui.theme().textDim, Align::Right);
        float rowH = ui.s(32), headH = ui.s(28);
        float content = 0;
        std::string lastGroup;
        for (const auto& t : tracks_) {
            if (t.group != lastGroup) content += headH;
            lastGroup = t.group;
            content += rowH + ui.s(3);
        }
        float ly = hy + ui.s(24);
        ui.beginScroll("tracks", {list.x, ly, list.w, list.bottom() - ly}, content);
        float yy = ly - ui.scrollOffset();
        lastGroup.clear();
        int n = 0;
        for (const auto& t : tracks_) {
            if (t.group != lastGroup) {
                std::string gname = groupNames_.count(t.group) ? groupNames_[t.group] : t.group;
                ui.label(gname, {list.x + ui.s(12), yy + headH * 0.55f}, fz(ui, 13), kGold, Align::Left, FontFace::Bold, VAlign::Middle);
                yy += headH;
            }
            lastGroup = t.group;
            ++n;
            Rect row{list.x, yy, list.w - ui.s(14), rowH};
            bool known = heard(t), on = cur == &t;
            if (known && ui.hotspot("trk" + std::to_string(n), row)) play(t);
            // « NOUVEAU » (2.49): a track added by an update (data/news/new_features.json).
            ui.newBadge("trk" + std::to_string(n), row, "track." + fs::path(t.file).stem().string(), true, true);
            r.fillRoundedRect(row, ui.s(8), on ? ui.theme().accent.withAlpha(36) : Color{14, 20, 40, 150});
            if (on) r.drawRoundedRect(row, ui.s(8), ui.theme().accent, std::max(1.0f, ui.s(1.5f)));
            char num[8];
            std::snprintf(num, sizeof num, "%02d", n);
            float cy = row.center().y;
            Color txt = known ? ui.theme().text : Color{90, 106, 130, 255};
            ui.label(num, {row.x + ui.s(12), cy}, fz(ui, 13), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            ui.label(ui.elide(known ? t.title : "???", FontFace::Bold, 15, colTitle), {row.x + ui.s(12) + colN, cy}, 15, txt, Align::Left, FontFace::Bold,
                     VAlign::Middle);
            std::string where = known ? t.where : "À découvrir : " + t.where;
            float wx = row.x + ui.s(24) + colN + colTitle;
            ui.label(ui.elide(where, FontFace::Body, fz(ui, 13), row.right() - colDur - wx - ui.s(12)), {wx, cy}, fz(ui, 13),
                     known ? ui.theme().textDim : Color{90, 106, 130, 255}, Align::Left, FontFace::Body, VAlign::Middle);
            std::string dur = "—";
            if (known) {
                auto it = durations_.find(t.file);
                if (it == durations_.end() && !durationRead_) {
                    // One file per frame: the list fills in without a pause.
                    durationRead_ = true;
                    it = durations_.emplace(t.file, au.fileDurationMs(g_.app.resources().soundPath(t.file))).first;
                }
                if (it != durations_.end() && it->second > 0) dur = mmss(it->second);
            }
            ui.label(dur, {row.right() - ui.s(12), cy}, fz(ui, 14), txt, Align::Right, FontFace::Body, VAlign::Middle);
            yy += rowH + ui.s(3);
        }
        ui.endScroll();

        // Player.
        ui.panel(player, false, 16);
        if (!cur) {
            ui.labelWrapped("Aucun morceau entendu pour l'instant : jouez un niveau.", {player.x + ui.s(20), player.y + ui.s(24)}, player.w - ui.s(40),
                            fz(ui, 16), ui.theme().textDim);
            return;
        }
        bool isPlaying = playing_ && au.currentMusic() == g_.app.resources().soundPath(cur->file);
        if (isPlaying) durations_[cur->file] = au.musicDurationMs();
        Rect art{player.x + ui.s(18), player.y + ui.s(18), player.w - ui.s(36), std::min(ui.s(240), player.h * 0.34f)};
        r.pushClip(art);
        drawPlanetBackdrop(g_, cur->planet, art);
        r.popClip();
        std::string state = !isPlaying ? "À l'arrêt" : au.musicPaused() ? "En pause" : "En lecture";
        float sw = ui.text().measure(state, FontFace::Body, ui.s(fz(ui, 13))).x + ui.s(20);
        Rect sr{art.x + ui.s(10), art.bottom() - ui.s(30), sw, ui.s(22)};
        r.fillRoundedRect(sr, sr.h * 0.5f, Color{8, 11, 24, 210});
        ui.label(state, sr.center(), fz(ui, 13), Color{143, 255, 240, 255}, Align::Center, FontFace::Body, VAlign::Middle);
        float y = art.bottom() + ui.s(18);
        ui.label(ui.elide(cur->title, FontFace::Bold, 24, art.w), {art.x, y}, 24, ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(34);
        ui.label(ui.elide(cur->where, FontFace::Body, fz(ui, 14), art.w), {art.x, y}, fz(ui, 14), ui.theme().textDim);
        y += ui.s(30);
        // Position (click to jump).
        int64_t dur = isPlaying ? au.musicDurationMs() : 0, pos = isPlaying ? au.musicPositionMs() : 0;
        if (dur > 0) pos %= dur;
        Rect bar{art.x, y, art.w, ui.s(8)};
        r.fillRoundedRect(bar, bar.h * 0.5f, Color{26, 36, 64, 255});
        if (dur > 0) r.fillRoundedRect({bar.x, bar.y, bar.w * static_cast<float>(pos) / static_cast<float>(dur), bar.h}, bar.h * 0.5f, ui.theme().accent);
        if (isPlaying && dur > 0 && ui.mouseClicked(bar.expanded(ui.s(6)))) {
            float k = std::clamp((g_.app.input().mousePos().x - bar.x) / bar.w, 0.0f, 1.0f);
            au.seekMusicMs(static_cast<int64_t>(k * static_cast<float>(dur)));
        }
        ui.label(mmss(pos), {bar.x, bar.bottom() + ui.s(14)}, fz(ui, 13), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        ui.label(dur > 0 ? mmss(dur) : "—", {bar.right(), bar.bottom() + ui.s(14)}, fz(ui, 13), ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
        y += ui.s(40);
        // Controls.
        float bsz = ui.s(50), big = ui.s(64);
        float cx = art.center().x;
        UIContext::ButtonStyle st;
        st.small = true;
        Rect prevR = Rect::fromCenter({cx - big * 0.5f - ui.s(14) - bsz * 0.5f, y + big * 0.5f}, {bsz, bsz});
        Rect nextR = Rect::fromCenter({cx + big * 0.5f + ui.s(14) + bsz * 0.5f, y + big * 0.5f}, {bsz, bsz});
        Rect playR = Rect::fromCenter({cx, y + big * 0.5f}, {big, big});
        if (ui.button("prev", prevR, "", st)) step(-1, false);
        UIContext::ButtonStyle pst;
        pst.primary = true;
        bool showPause = isPlaying && !au.musicPaused();
        if (ui.button("play", playR, "", pst)) {
            if (!isPlaying) play(*cur);
            else if (au.musicPaused()) au.resumeMusic();
            else au.pauseMusic();
        }
        if (ui.button("next", nextR, "", st)) step(1, false);
        {
            // Transport icons drawn (the game font has no such symbols).
            Color ic = ui.theme().text;
            auto tri = [&](Vec2 c, float s, float dir) {
                r.fillTriangle({c.x - s * 0.5f * dir, c.y - s * 0.6f}, {c.x - s * 0.5f * dir, c.y + s * 0.6f}, {c.x + s * 0.6f * dir, c.y}, ic);
            };
            float s1 = ui.s(10);
            tri({prevR.center().x + s1 * 0.2f, prevR.center().y}, s1, -1.0f);
            r.fillRect({prevR.center().x - s1 * 1.1f, prevR.center().y - s1 * 0.6f, ui.s(3), s1 * 1.2f}, ic);
            tri({nextR.center().x - s1 * 0.2f, nextR.center().y}, s1, 1.0f);
            r.fillRect({nextR.center().x + s1 * 0.9f, nextR.center().y - s1 * 0.6f, ui.s(3), s1 * 1.2f}, ic);
            float s2 = ui.s(15);
            if (showPause) {
                r.fillRect({playR.center().x - s2 * 0.55f, playR.center().y - s2 * 0.6f, s2 * 0.38f, s2 * 1.2f}, Color::white());
                r.fillRect({playR.center().x + s2 * 0.17f, playR.center().y - s2 * 0.6f, s2 * 0.38f, s2 * 1.2f}, Color::white());
            } else {
                tri({playR.center().x + s2 * 0.12f, playR.center().y}, s2, 1.0f);
            }
        }
        y += big + ui.s(18);
        float hw = (art.w - ui.s(10)) * 0.5f;
        if (ui.toggle("loop", {art.x, y, hw, ui.s(40)}, "Répéter", loop_) && isPlaying) play(*cur);
        ui.toggle("shuffle", {art.x + hw + ui.s(10), y, hw, ui.s(40)}, "Aléatoire", shuffle_);
        // Menu music.
        bool isMenu = g_.menuMusic() == cur->file;
        UIContext::ButtonStyle mst;
        mst.primary = !isMenu;
        mst.selected = isMenu;
        Rect mb{art.x, player.bottom() - ui.s(96), art.w, ui.s(46)};
        std::string ml = isMenu ? "Musique du menu principal (choisie)" : "Jouer ce morceau au menu principal";
        mst.fontSize = ui.fitFont(ml, FontFace::Bold, 17, mb.w - ui.s(24));
        if (ui.button("menu", mb, ml, mst) && !isMenu) {
            g_.collection.menuMusic = cur->file;
            g_.collection.save();
            g_.achievements.stats().add("menu_music");  // 2.24: « Programmateur »
            g_.checkAchievements();
            g_.achievements.save();
            progress_ = g_.cosmeticProgress();
            g_.uiSound("ui_confirm", 0.6f);
        }
        ui.label("Volume : celui de la musique (Paramètres → Audio).", {art.center().x, player.bottom() - ui.s(28)}, fz(ui, 13), ui.theme().textDim,
                 Align::Center, FontFace::Body, VAlign::Middle);
    }

    // ================================================================= Galerie
    struct GalItem {
        std::string id, name, sub, desc;
        bool known = false;
        const EnemyDefinition* def = nullptr;
        const PlanetData* planet = nullptr;
        bool boss = false;
    };
    std::vector<GalItem> galleryItems(int tab) const {
        std::vector<GalItem> out;
        auto planetName = [&](const std::string& id) {
            for (const PlanetData* p : g_.content.planets())
                if (p->id == id) return p->name;
            return id;
        };
        auto planetIndex = [&](const std::string& id) {
            int i = 0;
            for (const PlanetData* p : g_.content.planets()) {
                if (p->id == id) return i;
                ++i;
            }
            return 99;
        };
        if (tab == 0) {
            for (const PlanetData* p : g_.content.planets()) {
                GalItem it;
                it.id = p->id;
                it.name = p->name;
                it.sub = p->subtitle.empty() ? "Planète" : p->subtitle;
                it.desc = p->description;
                it.known = g_.progression.planetUnlocked(*p, g_.content);
                it.planet = p;
                out.push_back(it);
            }
            return out;
        }
        for (const EnemyDefinition* d : g_.content.enemies()) {
            if (!d->builtin || d->isBoss != (tab == 1)) continue;
            auto pit = planetsOf_.find(d->id);
            if (pit == planetsOf_.end()) continue;  // not in the campaign
            GalItem it;
            it.id = d->id;
            it.name = d->name;
            it.def = d;
            it.boss = d->isBoss;
            std::string where;
            for (const auto& pl : pit->second) where += (where.empty() ? "" : ", ") + planetName(pl);
            it.sub = (d->isBoss ? (d->id.rfind("echo_", 0) == 0 ? "Écho · " : "Gardien · ") : "Bestiaire · ") + where;
            it.desc = d->description;
            if (d->isBoss) {
                const BossRecord* br = g_.bossStats.find(d->id);
                it.known = br && br->kills > 0;
            } else {
                it.known = g_.collection.met.count(d->id) > 0;
            }
            out.push_back(it);
        }
        std::stable_sort(out.begin(), out.end(), [&](const GalItem& a, const GalItem& b) {
            bool ea = a.id.rfind("echo_", 0) == 0, eb = b.id.rfind("echo_", 0) == 0;
            if (ea != eb) return !ea;
            auto pa = planetsOf_.find(a.id), pb = planetsOf_.find(b.id);
            int ia = pa == planetsOf_.end() || pa->second.empty() ? 99 : planetIndex(pa->second.front());
            int ib = pb == planetsOf_.end() || pb->second.empty() ? 99 : planetIndex(pb->second.front());
            return ia < ib;
        });
        return out;
    }

    void exportPlanet(const PlanetData& p) {
        // The four layers stacked, at twice their size, in <user folder>/Galerie.
        fs::path dir = Paths::userDir() / "Galerie";
        std::error_code ec;
        fs::create_directories(dir, ec);
        SDL_Surface* out = SDL_CreateSurface(1280, 720, SDL_PIXELFORMAT_RGBA32);
        if (!out) return;
        SDL_FillSurfaceRect(out, nullptr, SDL_MapSurfaceRGBA(out, 8, 11, 24, 255));
        bool any = false;
        for (const char* layer : {"sky", "far", "mid", "near"}) {
            std::string path = g_.app.resources().soundPath("backgrounds/" + p.id + "/" + layer + ".png");
            if (path.empty()) continue;
            if (SDL_Surface* s = IMG_Load(path.c_str())) {
                SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
                SDL_Rect dst{0, 0, 1280, 720};
                SDL_BlitSurfaceScaled(s, nullptr, out, &dst, SDL_SCALEMODE_NEAREST);
                SDL_DestroySurface(s);
                any = true;
            }
        }
        fs::path file = dir / (p.id + ".png");
        bool ok = any && IMG_SavePNG(out, Paths::toUtf8(file).c_str());
        SDL_DestroySurface(out);
        if (ok) {
            g_.collection.exported++;
            g_.collection.save();
            g_.achievements.stats().insert("photographed_planets", p.id);  // 2.24: « Photographe »
            g_.checkAchievements();
            g_.achievements.save();
            progress_ = g_.cosmeticProgress();
            exportedMsg_ = "Enregistrée : " + Paths::toUtf8(file);
        } else {
            exportedMsg_ = "Impossible d'enregistrer l'image.";
        }
        exportedTimer_ = 6.0f;
    }

    void drawGallery(const Rect& area) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        exportedTimer_ = std::max(0.0f, exportedTimer_ - g_.app.frameTime() / 1000.0f);
        float dw = std::min(ui.s(470), area.w * 0.32f);
        Rect left{area.x, area.y, area.w - dw - ui.s(22), area.h};
        Rect det{left.right() + ui.s(22), area.y, dw, area.h};
        static const char* kSub[3] = {"Planètes", "Gardiens", "Bestiaire"};
        float x = left.x;
        for (int i = 0; i < 3; ++i) {
            auto items = galleryItems(i);
            int known = 0;
            for (const auto& it : items) known += it.known ? 1 : 0;
            std::string lbl = std::string(kSub[i]) + "  " + std::to_string(known) + " / " + std::to_string(items.size());
            float w = ui.text().measure(lbl, FontFace::Bold, ui.s(16)).x + ui.s(36);
            UIContext::ButtonStyle st;
            st.small = true;
            st.selected = galTab_ == i;
            st.fontSize = 16;
            if (ui.button("gal" + std::to_string(i), {x, left.y, w, ui.s(40)}, lbl, st)) {
                galTab_ = i;
                galSel_.clear();
                animClip_ = 0;
            }
            static const char* const kGalFeatures[3] = {"gallery.tab.planets", "gallery.tab.bosses", "gallery.tab.creatures"};
            ui.newBadge("gal" + std::to_string(i), {x, left.y, w, ui.s(40)}, kGalFeatures[i], true, true);
            x += w + ui.s(10);
        }
        static const char* kHint[3] = {"Une planète s'ouvre quand vous y arrivez.", "Un gardien entre dans la galerie quand vous le battez.",
                                       "Une créature y entre dès que vous la rencontrez."};
        if (x + ui.s(20) < left.right())
            ui.label(ui.elide(kHint[galTab_], FontFace::Body, fz(ui, 14), left.right() - x - ui.s(20)), {left.right(), left.y + ui.s(20)}, fz(ui, 14),
                     ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
        auto items = galleryItems(galTab_);
        if (galTab_ > 0 && planetsOf_.empty()) {
            ui.label("Lecture des niveaux…", {left.x, left.y + ui.s(70)}, fz(ui, 16), ui.theme().textDim);
            return;
        }
        const GalItem* sel = nullptr;
        for (const auto& it : items)
            if (it.id == galSel_) sel = &it;
        if (!sel)
            for (const auto& it : items)
                if (it.known) {
                    sel = &it;
                    break;
                }
        if (!sel && !items.empty()) sel = &items.front();
        // Grid.
        float gy = left.y + ui.s(54);
        int cols = galTab_ == 0 ? 2 : 6;
        float gap = ui.s(10);
        float cw = (left.w - ui.s(14) - gap * (cols - 1)) / cols;
        float thumbH = galTab_ == 0 ? cw * 9.0f / 16.0f : ui.s(92);
        float ch = thumbH + (galTab_ == 0 ? ui.s(40) : ui.s(48));
        ui.beginScroll("grid", {left.x, gy, left.w, left.bottom() - gy}, std::ceil(items.size() / static_cast<float>(cols)) * (ch + gap));
        float so = ui.scrollOffset();
        for (size_t i = 0; i < items.size(); ++i) {
            const GalItem& it = items[i];
            Rect c{left.x + (cw + gap) * static_cast<float>(i % static_cast<size_t>(cols)), gy - so + (ch + gap) * static_cast<float>(i / static_cast<size_t>(cols)), cw, ch};
            bool on = sel && sel->id == it.id;
            if (ui.hotspot("g" + std::to_string(i), c)) {
                galSel_ = it.id;
                animClip_ = 0;
            }
            // « NOUVEAU » (2.49): a planet, a guardian or a creature added by an update.
            static const char* const kGalKinds[3] = {"gallery.planet.", "gallery.boss.", "gallery.creature."};
            ui.newBadge("g" + std::to_string(i), c, kGalKinds[galTab_] + it.id, true, true);
            r.fillRoundedRect(c, ui.s(10), on ? ui.theme().accent.withAlpha(36) : Color{14, 20, 40, 235});
            r.drawRoundedRect(c, ui.s(10), on ? ui.theme().accent : ui.theme().accent.withAlpha(30), std::max(1.0f, ui.s(1.5f)));
            Rect th{c.x + ui.s(6), c.y + ui.s(6), c.w - ui.s(12), thumbH};
            r.fillRoundedRect(th, ui.s(8), Color{11, 18, 38, 255});
            r.pushClip(th);
            if (it.planet) drawPlanetBackdrop(g_, it.planet->id, th, it.known ? Color::white() : Color{40, 40, 50, 255});
            else if (it.def) drawCreature(g_, *it.def, th.expanded(-ui.s(4)), t_, it.known ? Color::white() : Color{0, 0, 0, 110});
            r.popClip();
            if (!it.known) ui.label("?", th.center(), 22, Color{90, 106, 130, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
            std::string nm = it.known ? it.name : "???";
            float ns = galTab_ == 0 ? 16.0f : fz(ui, 11.5f);
            Rect nameBox{c.x + ui.s(4), th.bottom() + ui.s(4), c.w - ui.s(8), c.bottom() - th.bottom() - ui.s(8)};
            ui.labelWrapped(nm, {nameBox.x, nameBox.y}, nameBox.w, ns, it.known ? ui.theme().text : Color{90, 106, 130, 255});
        }
        ui.endScroll();

        // Detail.
        ui.panel(det, false, 16);
        if (!sel) return;
        Rect big{det.x + ui.s(18), det.y + ui.s(18), det.w - ui.s(36), std::min(ui.s(270), det.h * 0.4f)};
        r.fillRoundedRect(big, ui.s(12), Color{11, 18, 38, 255});
        r.pushClip(big);
        std::vector<std::string> clips;
        if (sel->planet) {
            drawPlanetBackdrop(g_, sel->planet->id, big, sel->known ? Color::white() : Color{40, 40, 50, 255});
        } else if (sel->def) {
            clips = creatureClips(g_, *sel->def);
            std::string clip = clips.empty() ? "idle" : clips[static_cast<size_t>(animClip_) % clips.size()];
            drawCreature(g_, *sel->def, big.expanded(-ui.s(16)), t_, sel->known ? Color::white() : Color{0, 0, 0, 110}, sel->known ? clip : "idle");
        }
        r.popClip();
        if (!sel->known) ui.label("?", big.center(), 48, Color{90, 106, 130, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
        float y = big.bottom() + ui.s(18);
        float dx = det.x + ui.s(20), dwid = det.w - ui.s(40);
        ui.label(ui.elide(sel->known ? sel->name : "Inconnu", FontFace::Bold, 24, dwid), {dx, y}, 24, ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(36);
        ui.label(ui.elide(sel->sub, FontFace::Body, fz(ui, 14), dwid), {dx, y}, fz(ui, 14), ui.theme().textDim);
        y += ui.s(28);
        std::string desc = sel->known ? sel->desc : galTab_ == 0 ? "Pas encore atteinte." : galTab_ == 1 ? "Pas encore vaincu." : "Pas encore rencontrée.";
        Vec2 ds = ui.labelWrapped(desc, {dx, y}, dwid, fz(ui, 15), Color{185, 200, 218, 255});
        y += ds.y + ui.s(16);
        if (sel->known && sel->boss) {
            const BossRecord* br = g_.bossStats.find(sel->id);
            if (br) {
                auto fact = [&](const std::string& k, const std::string& v) {
                    ui.label(k, {dx, y}, fz(ui, 14), ui.theme().textDim);
                    ui.label(v, {dx + dwid, y}, fz(ui, 14), ui.theme().text, Align::Right, FontFace::Bold);
                    y += ui.s(24);
                };
                fact("Victoires", std::to_string(br->kills));
                fact("Victoires sans dégât", std::to_string(br->noHitKills));
                fact("Phases sans dégât", std::to_string(br->noHitPhases()));
            }
        }
        float by = det.bottom() - ui.s(66);
        if (sel->known && sel->planet) {
            UIContext::ButtonStyle st;
            st.primary = true;
            if (ui.button("export", {dx, by, dwid, ui.s(46)}, "Enregistrer en image (PNG)", st)) exportPlanet(*sel->planet);
            if (exportedTimer_ > 0.0f)
                ui.labelWrapped(exportedMsg_, {dx, by - ui.s(46)}, dwid, fz(ui, 13), kGreen);
        } else if (sel->known && sel->def && clips.size() > 1) {
            UIContext::ButtonStyle st;
            std::string lbl = "Animation suivante (" + std::to_string(animClip_ % static_cast<int>(clips.size()) + 1) + " / " + std::to_string(clips.size()) + ")";
            if (ui.button("anim", {dx, by, dwid, ui.s(46)}, lbl, st)) animClip_ = (animClip_ + 1) % static_cast<int>(clips.size());
        }
    }

    GameServices& g_;
    int tab_ = 0;
    OnlineUi online_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string select_;  // the reward to show on arrival ("" = none)
    CosmeticProgress progress_;
    float t_ = 0.0f;
    // Vestiaire
    int cat_ = 0;
    std::string selItem_;
    std::string selBadge_;  // 2.41: the achievement selected among the profile badges
    BadgeRow slotsRow_;     // the badges of the preview (their tooltip)
    TrailFx trailFx_;
    bool routeEnd_ = false;  // automated screenshots: the Route scrolled to its end
    Vec2 trailCenter_;
    float trailRadius_ = 80.0f;
    // Musiques
    std::vector<Track> tracks_;
    std::vector<std::string> groupOrder_;
    std::map<std::string, std::string> groupNames_;
    std::map<std::string, int64_t> durations_;
    bool durationRead_ = false;
    std::string selTrack_;
    bool playing_ = false, loop_ = false, shuffle_ = false;
    // Galerie
    int galTab_ = 1;
    std::string galSel_;
    int animClip_ = 0;
    std::future<PlanetsOf> scan_;
    PlanetsOf planetsOf_;
    std::string exportedMsg_;
    float exportedTimer_ = 0.0f;
    // Replays (2.45)
    DownloadedReplaysPanel replays_;
};

}  // namespace

std::unique_ptr<Scene> makeCollection(GameServices& g, int tab) { return std::make_unique<CollectionScene>(g, tab); }

std::unique_ptr<Scene> makeCollectionOn(GameServices& g, const std::string& itemId) {
    return std::make_unique<CollectionScene>(g, 0, itemId);
}

}  // namespace cosmic
