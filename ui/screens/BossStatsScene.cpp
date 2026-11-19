// Main menu → Statistiques: every boss of the game with the player's counters, kept on this
// computer (game/records/BossStats.h): fights, victories, defeats, victories without being
// hit, and for each phase how many times it was beaten and beaten without a hit. The boss is
// shown with its own animations (once beaten, as in the Collection's gallery: a silhouette
// before), with every place of the game's levels where it can be fought (read from the levels
// on a worker thread, game/content/CreaturePlaces.h); a place opens its level's page. A boss
// met can be trained against (« Entraînement », ui/screens/BossTrainingScene.cpp).
// « Fiche » (2.37): what the boss is, read from its definition (data/enemies): health, contact
// damage, speed, weaknesses, time limit, each phase (from which share of its health, its rhythm,
// its attacks) and each attack (type, damage, wind-up, cooldown). Nothing is estimated there.
// 2.60: each attack of the sheet unfolds (a click, Entrée / A; « Tout déplier », « Tout
// replier »): what it does, its characteristics read from its definition, and an example of it
// played by the game engine (ui/screens/BossAttackPreview.h): the boss in its arena giving that
// attack again and again, Nova dodging it; one example at a time, the unfolded attack that has
// the focus or the last unfolded (game/enemies/BossAttackSheet.h). The fold stays while the
// screen is open.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/content/CreaturePlaces.h"
#include "game/enemies/BossAttackSheet.h"
#include "game/enemies/BossDifficulty.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/BossBadge.h"
#include "ui/online/RatingWidgets.h"
#include "ui/widgets/BossSheetWidgets.h"
#include "ui/widgets/HitPartsView.h"
#include "ui/screens/BossStoryScene.h"
#include "ui/screens/CreatureArt.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

/// Never below 11.5 pixels on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

/// The size (from `size`, never below 11.5 pixels) at which `t` fits `room`; what still does
/// not fit is shortened with « … » by the caller.
float fit(UIContext& ui, const std::string& t, FontFace face, float size, float room, float minScale = 0.8f) {
    return std::max(ui.fitFont(t, face, fz(ui, size), room, minScale), 11.5f / ui.s(1.0f));
}

std::string seconds(float s) {
    if (s <= 0.0f) return "—";
    int t = static_cast<int>(std::lround(s * 10.0f));
    char b[32];
    std::snprintf(b, sizeof b, "%d:%02d,%d", t / 600, (t / 10) % 60, t % 10);
    return b;
}

std::string dateFr(const std::string& d) {
    // FileUtil::nowString(): "YYYY-MM-DD HH:MM:SS".
    if (d.size() < 16) return d.empty() ? "—" : d;
    return d.substr(8, 2) + "/" + d.substr(5, 2) + "/" + d.substr(0, 4) + " " + d.substr(11, 5);
}

/// « ×1,25 »: a multiplier as the sheet shows it (French decimal comma, no useless zero).
std::string times(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", v);
    std::string t = b;
    while (!t.empty() && t.back() == '0') t.pop_back();
    if (!t.empty() && t.back() == '.') t.pop_back();
    if (auto dot = t.find('.'); dot != std::string::npos) t[dot] = ',';
    return "×" + t;
}

/// « 0,6 s », « 12 » : a number with at most `decimals` decimals, French decimal comma.
std::string num(float v, int decimals = 1) {
    char b[32];
    std::snprintf(b, sizeof b, "%.*f", decimals, v);
    std::string t = b;
    if (t.find('.') != std::string::npos) {
        while (!t.empty() && t.back() == '0') t.pop_back();
        if (!t.empty() && t.back() == '.') t.pop_back();
    }
    if (auto dot = t.find('.'); dot != std::string::npos) t[dot] = ',';
    return t;
}

/// How long a clip is shown before the next one: whole loops, about 2.4 seconds.
float clipHold(float loop) {
    if (loop <= 0.05f) return 2.4f;
    return loop * std::max(1.0f, std::ceil(2.4f / loop));
}

class BossStatsScene : public Scene {
public:
    BossStatsScene(GameServices& g, std::string bossId, std::string fromAsset = {})
        : g_(g), wanted_(std::move(bossId)), fromAsset_(std::move(fromAsset)), attacks_(g) {}

    void onEnter() override {
        bg_.init(g_);
        for (const auto* d : g_.content.enemies())
            if (d->isBoss && !d->tutorial && d->raid.empty()) bosses_.push_back(d);  // 2.64: not the tutorial's; 2.68: nor a raid's
        // Bosses met first (most victories), then the others by name.
        std::stable_sort(bosses_.begin(), bosses_.end(), [&](const EnemyDefinition* a, const EnemyDefinition* b) {
            const BossRecord* ra = g_.bossStats.find(a->id);
            const BossRecord* rb = g_.bossStats.find(b->id);
            int fa = ra ? ra->fights : 0, fb = rb ? rb->fights : 0;
            if ((fa > 0) != (fb > 0)) return fa > 0;
            int ka = ra ? ra->kills : 0, kb = rb ? rb->kills : 0;
            if (ka != kb) return ka > kb;
            return a->name < b->name;
        });
        // Automated screenshots: "boss:<id>[:<clip>][:fiche][:attaques][:attaque=<attack id>|:tout]"
        // (the clip then stays shown; « fiche »: its sheet; « attaques »: its list of attacks at the
        // top; an attack unfolded with its example, or every attack unfolded).
        std::string unfold;
        bool unfoldAll = false, listOnTop = false;
        if (!g_.onlineOpen.empty() && g_.onlineOpen.rfind("boss:", 0) == 0) {
            std::string v = g_.onlineOpen.substr(5);
            std::vector<std::string> parts;
            for (size_t from = 0;;) {
                size_t c = v.find(':', from);
                parts.push_back(v.substr(from, c == std::string::npos ? std::string::npos : c - from));
                if (c == std::string::npos) break;
                from = c + 1;
            }
            wanted_ = parts.front();
            for (size_t i = 1; i < parts.size(); ++i) {
                const std::string& p = parts[i];
                if (p == "fiche") tab_ = 1;
                else if (p == "frapper") tab_ = 2;  // 2.76.0: « Où frapper » (its parts)
                else if (p.rfind("frapper=", 0) == 0) {
                    tab_ = 2;
                    hitView_.choosePose(p.substr(8));
                }
                else if (p == "tout") unfoldAll = true;
                else if (p == "attaques") listOnTop = true;
                else if (p.rfind("attaque=", 0) == 0) unfold = p.substr(8);
                else if (!p.empty() && std::isdigit(static_cast<unsigned char>(p.front()))) forcedClip_ = std::max(0, std::atoi(p.c_str()));
            }
            if (!unfold.empty() || unfoldAll || listOnTop) tab_ = 1;
            g_.onlineOpen.clear();
        }
        for (size_t i = 0; i < bosses_.size(); ++i)
            if (bosses_[i]->id == wanted_) selected_ = static_cast<int>(i);
        g_.ui.setFocus("bstats/b" + std::to_string(selected_));
        if (!bosses_.empty() && (!unfold.empty() || unfoldAll)) {
            const EnemyDefinition& d = *bosses_[static_cast<size_t>(selected_)];
            AttackFold& f = attacks_.fold(d.id);
            if (unfoldAll) {
                std::vector<std::string> ids;
                for (const AttackDef& a : d.attacks) ids.push_back(a.id);
                f.expandAll(ids);
            }
            if (!unfold.empty() && d.attack(unfold)) {
                f.expand(unfold);
                attacks_.scrollTo(unfold);
            }
        }
        if (listOnTop) attacks_.scrollToList();
        atlas_ = officialCreatureAtlas(g_.content);  // starts reading the levels (worker thread)
        attacks_.setAtlas(atlas_);
    }

    void fixedUpdate(float) override { attacks_.fixedUpdate(); }

    void update(float dt) override {
        t_ += dt;
        clipT_ += dt;
        bg_.update(dt);
        if (!atlas_) {
            atlas_ = officialCreatureAtlas(g_.content);
            attacks_.setAtlas(atlas_);
        }
        attacks_.update(dt);
    }

    void render(float alpha) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.6f);
        ui.pushId("bstats");
        float W = ui.screenW(), H = ui.screenH();
        float m = ui.s(48);
        UIContext::ButtonStyle bs;
        bs.small = true;
        Rect back{W - m - ui.s(200), ui.s(30), ui.s(200), ui.s(46)};
        float titleRoom = back.x - ui.s(24) - m;
        float tf = ui.fitFont("STATISTIQUES DES BOSS", FontFace::Title, 40, titleRoom, 0.6f);
        ui.label("STATISTIQUES DES BOSS", {m, ui.s(52)}, tf, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        bool leave = ui.button("back", back, "Retour", bs);
        drawSummary({m, ui.s(100), W - 2 * m, ui.s(56)});
        float top = ui.s(176);
        Rect list{m, top, std::min(ui.s(520), (W - 2 * m) * 0.38f), H - top - ui.s(32)};
        Rect detail{list.right() + ui.s(24), top, W - m - list.right() - ui.s(24), H - top - ui.s(32)};
        drawList(list);
        if (!bosses_.empty()) {
            const EnemyDefinition& d = *bosses_[static_cast<size_t>(std::clamp(selected_, 0, static_cast<int>(bosses_.size()) - 1))];
            show(d);
            drawDetail(detail, d, alpha);
            // One example at a time, only while the sheet is shown.
            if (tab_ != 1) attacks_.stop();
        }
        if (leave || ui.backPressed()) {
            if (!fromAsset_.empty()) g_.app.switchScene(makeAdmin(g_, "assets:" + fromAsset_), 0.3f);  // 2.63: back to Administration › Assets
            else g_.app.switchScene(makeStatistics(g_, 2), 0.3f);                                      // 2.54: back to Statistiques › Boss
        }
        ui.popId();
        ui.end();
    }

private:
    /// The boss shown changes: its clips, its places (once the levels are read).
    void show(const EnemyDefinition& d) {
        if (shown_ != d.id) {
            shown_ = d.id;
            clips_ = creatureClips(g_, d);
            clip_ = forcedClip_ >= 0 && !clips_.empty() ? forcedClip_ % static_cast<int>(clips_.size()) : 0;
            holdClip_ = forcedClip_ >= 0;
            forcedClip_ = -1;
            clipT_ = 0.0f;
            placesOf_.clear();
            places_.clear();
        }
        if (atlas_ && placesOf_ != d.id) {
            placesOf_ = d.id;
            places_ = atlas_->fightsOf(g_.content, d.id);
        }
    }

    /// « Revoir l'histoire » (2.51): a giga boss with a story, already met or whose story was told.
    bool storyReplayable(const EnemyDefinition& d, const BossRecord* rec) const {
        if (!isGigaBoss(d) || !g_.stories.find(d.id)) return false;
        return g_.bossStats.storySeen(d.id) || (rec && rec->fights > 0) || g_.collection.met.count(d.id) > 0;
    }
    /// Told again over the sheet (no run is touched); seen from now on.
    void openStory(const std::string& bossId) {
        if (auto story = makeBossStory(g_, bossId)) {
            g_.bossStats.markStorySeen(bossId);
            g_.bossStats.save();
            g_.app.pushScene(std::move(story));
        }
    }

    bool beaten(const EnemyDefinition& d) const {
        const BossRecord* r = g_.bossStats.find(d.id);
        return r && r->kills > 0;
    }

    void drawSummary(const Rect& r) {
        UIContext& ui = g_.ui;
        int met = 0, beatenCount = 0, kills = 0, noHit = 0, phases = 0;
        for (const auto* d : bosses_)
            if (const BossRecord* rec = g_.bossStats.find(d->id)) {
                met += rec->fights > 0 ? 1 : 0;
                beatenCount += rec->kills > 0 ? 1 : 0;
                kills += rec->kills;
                noHit += rec->noHitKills;
                phases += rec->noHitPhases();
            }
        struct Chip {
            std::string label, value;
            Color c;
        };
        std::vector<Chip> chips = {
            {"Boss vaincus", std::to_string(beatenCount) + " / " + std::to_string(bosses_.size()), ui.theme().accent},
            {"Victoires", std::to_string(kills), ui.theme().warm},
            {"Combats sans dégât", std::to_string(noHit), ui.theme().success},
            {"Phases sans dégât", std::to_string(phases), Color{150, 220, 255, 255}},
            {"Boss rencontrés", std::to_string(met), ui.theme().textDim},
        };
        float gap = ui.s(14);
        float cw = (r.w - gap * (chips.size() - 1)) / static_cast<float>(chips.size());
        for (size_t i = 0; i < chips.size(); ++i) {
            Rect c{r.x + i * (cw + gap), r.y, cw, r.h};
            ui.r().fillRoundedRect(c, ui.s(10), Color{10, 16, 34, 220});
            ui.r().drawRoundedRect(c, ui.s(10), chips[i].c.withAlphaF(0.5f), std::max(1.0f, ui.s(1.2f)));
            // The value first (it must stay whole), the label in the room left of it.
            float vf = ui.fitFont(chips[i].value, FontFace::Bold, 24, cw * 0.35f);
            float vw = ui.text().measure(chips[i].value, FontFace::Bold, ui.s(vf)).x;
            float room = cw - ui.s(28) - vw - ui.s(12);
            float lf = fit(ui, chips[i].label, FontFace::Body, 16, room, 0.75f);
            ui.label(ui.elide(chips[i].label, FontFace::Body, lf, room), {c.x + ui.s(14), c.center().y}, lf, ui.theme().textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
            ui.label(chips[i].value, {c.right() - ui.s(14), c.center().y}, vf, chips[i].c, Align::Right, FontFace::Bold, VAlign::Middle);
        }
    }

    void drawList(const Rect& area) {
        UIContext& ui = g_.ui;
        float rh = ui.s(78), gap = ui.s(8);
        ui.beginScroll("list", area, static_cast<float>(bosses_.size()) * (rh + gap));
        float y = area.y - ui.scrollOffset();
        for (size_t i = 0; i < bosses_.size(); ++i) {
            const EnemyDefinition& d = *bosses_[i];
            Rect row{area.x, y, area.w - ui.s(14), rh};
            y += rh + gap;
            std::string id = "b" + std::to_string(i);
            UIContext::ButtonStyle st;
            st.selected = static_cast<int>(i) == selected_;
            if (ui.button(id, row, "", st)) selected_ = static_cast<int>(i);
            if (ui.focused(id)) selected_ = static_cast<int>(i);
            ui.newBadge(id, row, "boss." + d.id, true, true);  // « NOUVEAU » (2.34.1): a new boss
            if (row.bottom() < area.y || row.y > area.bottom()) continue;
            const BossRecord* rec = g_.bossStats.find(d.id);
            bool met = rec && rec->fights > 0;
            BossBadge::draw(ui.r(), g_.app.resources(), d, {row.x + ui.s(42), row.center().y}, ui.s(26), false, t_);
            float tx = row.x + ui.s(82), room = row.right() - tx - ui.s(14);
            float nf = ui.fitFont(d.name, FontFace::Bold, 19, room, 0.75f);
            ui.label(ui.elide(d.name, FontFace::Bold, nf, room), {tx, row.y + ui.s(26)}, nf, met ? ui.theme().text : ui.theme().textDim, Align::Left,
                     FontFace::Bold, VAlign::Middle);
            std::string sub = !met ? "Jamais affronté"
                                   : std::to_string(rec->kills) + (rec->kills > 1 ? " victoires" : " victoire") + "  ·  " +
                                         std::to_string(rec->noHitKills) + " sans dégât  ·  " + std::to_string(rec->noHitPhases()) +
                                         (rec->noHitPhases() > 1 ? " phases parfaites" : " phase parfaite");
            float sf = fit(ui, sub, FontFace::Body, 15, room, 0.8f);
            ui.label(ui.elide(sub, FontFace::Body, sf, room), {tx, row.y + ui.s(54)}, sf,
                     rec && rec->noHitKills > 0 ? ui.theme().success : ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        }
        ui.endScroll();
    }

    void drawDetail(const Rect& p, const EnemyDefinition& d, float alpha) {
        UIContext& ui = g_.ui;
        ui.panel(p, true, 16);
        const BossRecord* rec = g_.bossStats.find(d.id);
        BossRecord empty;
        const BossRecord& r = rec ? *rec : empty;
        const float pad = ui.s(26);
        float x = p.x + pad, w = p.w - 2 * pad, y = p.y + ui.s(22);
        // Top: the boss (animated) and its counters.
        float topH = std::min(ui.s(300), p.h * 0.36f);
        Rect box{x, y, std::min(ui.s(360), w * 0.32f), topH};
        drawAnimation(box, d);
        float rx = box.right() + ui.s(26), rw = x + w - rx;
        // 2.51 « Revoir l'histoire »: a giga boss already met (or whose story was told), at the
        // right of its name; the name keeps the room left.
        float nameRoom = rw;
        if (storyReplayable(d, rec)) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = 16;
            const std::string lbl = "Revoir l'histoire";
            const float bw = std::min(rw * 0.45f, ui.text().measure(lbl, FontFace::Bold, ui.s(16)).x + ui.s(44));
            const Rect sb{rx + rw - bw, y, bw, ui.s(40)};
            if (ui.button("story", sb, lbl, st)) openStory(d.id);
            // 2.60: a story told anew (Pyrrhax's, in animated scenes) has its own novelty.
            const std::string fresh = "stats.story." + d.id;
            ui.newBadge("story", sb, ui.isNew(fresh) ? fresh : std::string("stats.story"), true, true);
            nameRoom = rw - bw - ui.s(16);
        }
        float tf = ui.fitFont(d.name, FontFace::Title, 32, nameRoom, 0.6f);
        ui.label(ui.elide(d.name, FontFace::Title, tf, nameRoom), {rx, y + ui.s(20)}, tf, ui.theme().text, Align::Left, FontFace::Title,
                 VAlign::Middle);
        std::string title = d.bossTitle.empty() ? d.description : d.bossTitle;
        if (!title.empty())
            ui.label(ui.elide(title, FontFace::Body, fz(ui, 17), rw), {rx, y + ui.s(56)}, fz(ui, 17), ui.theme().textDim, Align::Left, FontFace::Body,
                     VAlign::Middle);
        struct Cell {
            std::string label, value;
            Color c;
        };
        std::vector<Cell> cells = {
            {"Combats", std::to_string(r.fights), ui.theme().text},
            {"Victoires", std::to_string(r.kills), ui.theme().warm},
            {"Défaites", std::to_string(r.defeats), ui.theme().danger},
            {"Victoires sans dégât", std::to_string(r.noHitKills), ui.theme().success},
            {"Meilleur combat", seconds(r.bestKill), ui.theme().accent},
            {"Dernière victoire", r.lastKill.empty() ? "—" : dateFr(r.lastKill), ui.theme().textDim},
        };
        const int cols = 3;
        float gap = ui.s(12), cy = y + ui.s(84);
        float ch = (box.bottom() - cy - gap) / 2.0f;
        float cw = (rw - gap * (cols - 1)) / cols;
        for (size_t i = 0; i < cells.size(); ++i) {
            Rect c{rx + (i % cols) * (cw + gap), cy + (i / cols) * (ch + gap), cw, ch};
            ui.r().fillRoundedRect(c, ui.s(10), Color{8, 13, 28, 230});
            ui.r().drawRoundedRect(c, ui.s(10), cells[i].c.withAlphaF(0.35f), std::max(1.0f, ui.s(1.0f)));
            float room = c.w - ui.s(32);
            float lf = fit(ui, cells[i].label, FontFace::Body, 16, room, 0.75f);
            ui.label(ui.elide(cells[i].label, FontFace::Body, lf, room), {c.x + ui.s(16), c.y + ch * 0.3f}, lf, ui.theme().textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
            float vf = ui.fitFont(cells[i].value, FontFace::Bold, 30, room, 0.5f);
            ui.label(ui.elide(cells[i].value, FontFace::Bold, vf, room), {c.x + ui.s(16), c.y + ch * 0.68f}, vf, cells[i].c, Align::Left,
                     FontFace::Bold, VAlign::Middle);
        }
        // Below, two sub-tabs: the player's statistics (the phases, where to fight it), and the
        // boss's sheet.
        y = box.bottom() + ui.s(18);
        Rect tabsR{x, y, std::min(w, ui.s(620)), ui.s(40)};
        ui.tabs("sub", tabsR, {"Mes statistiques", "Fiche", "Où frapper"}, tab_);
        // « NOUVEAU » (2.37) on the sheet's tab until the sheet is shown once.
        if (tab_ == 1 && ui.isNew("stats.sheet") && ui.featureSeen) ui.featureSeen("stats.sheet");
        if (ui.isNew("stats.sheet")) ui.drawNewPill(tabsR.right() - ui.s(10), tabsR.y, true);
        y = tabsR.bottom() + ui.s(14);
        float bh = p.bottom() - ui.s(22) - y;
        float colGap = ui.s(28);
        if (tab_ == 2) {
            // 2.76.0: its hit parts on its sprite, pose by pose (ui/widgets/HitPartsView).
            hitView_.draw(g_, {x, y, w, bh}, d, g_.app.frameTime() / 1000.0f);
            return;
        }
        if (tab_ == 1) {
            // Its difficulty in stars (computed from its definition: game/enemies/BossDifficulty.h).
            const float dh = ui.s(40);
            drawBossDifficulty(ui, {x, y, w, dh}, bossDifficulty(d).stars);
            y += dh + ui.s(12);
            bh -= dh + ui.s(12);
            float leftW = std::floor((w - colGap) * 0.4f);
            drawSheetFacts({x, y, leftW, bh}, d);
            drawSheetPhases({x + leftW + colGap, y, w - leftW - colGap, bh}, d, alpha);
            return;
        }
        float leftW = std::floor((w - colGap) * 0.44f);
        const float trainH = ui.s(56);
        drawPhases({x, y, leftW, bh - trainH - ui.s(12)}, d, r, rec && r.fights > 0);
        drawTraining({x, y + bh - trainH, leftW, trainH}, d, rec);
        drawPlaces({x + leftW + colGap, y, w - leftW - colGap, bh}, d);
    }

    // ---------------------------------------------------------------- « Fiche » (2.37)

    /// What the boss is: one line per fact, the value at the right (read from its definition).
    void drawSheetFacts(const Rect& area, const EnemyDefinition& d) {
        UIContext& ui = g_.ui;
        ui.label("Caractéristiques", {area.x, area.y + ui.s(14)}, 22, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
        struct Fact {
            std::string label, value;
            Color c;
        };
        std::vector<Fact> facts;
        facts.push_back({"Points de vie", num(d.hp, 0), ui.theme().danger});
        facts.push_back({"Phases", std::to_string(std::max<size_t>(1, d.phases.size())), ui.theme().text});
        facts.push_back({"Attaques", std::to_string(d.attacks.size()), ui.theme().text});
        facts.push_back({"Dégâts au contact", num(d.contactDamage), ui.theme().warm});
        facts.push_back({"Vitesse", num(d.speed, 0) + (d.chaseSpeedMultiplier != 1.0f ? "  (" + times(d.chaseSpeedMultiplier) + " en poursuite)" : ""),
                         ui.theme().text});
        facts.push_back({"Déplacement", d.flying ? std::string("En vol") : std::string(movementLabel(d.movement)), ui.theme().text});
        facts.push_back({"Résistance au recul", num(std::clamp(d.knockbackResist, 0.0f, 1.0f) * 100.0f, 0) + " %", ui.theme().text});
        // A time limit: the whole fight's, or the phases' (the boss enrages after it).
        std::string limit;
        if (d.enrageTime > 0.0f) limit = num(d.enrageTime, 0) + " s";
        for (size_t i = 0; i < d.phases.size(); ++i)
            if (d.phases[i].enrageTime > 0.0f) {
                if (!limit.empty()) limit += ", ";
                limit += num(d.phases[i].enrageTime, 0) + " s (phase " + std::to_string(i + 1) + ")";
            }
        facts.push_back({"Limite de temps", limit.empty() ? std::string("Aucune") : limit, limit.empty() ? ui.theme().textDim : ui.theme().danger});
        // What hurts it more (or less) than usual.
        std::string weak, strong;
        for (int t = 0; t < static_cast<int>(DamageType::Count); ++t) {
            float m = d.multiplier(static_cast<DamageType>(t));
            if (m == 1.0f) continue;
            std::string& out = m > 1.0f ? weak : strong;
            if (!out.empty()) out += ", ";
            out += std::string(damageTypeLabel(static_cast<DamageType>(t))) + " " + times(m);
        }
        facts.push_back({"Faiblesses", weak.empty() ? std::string("Aucune") : weak, weak.empty() ? ui.theme().textDim : ui.theme().success});
        if (!strong.empty()) facts.push_back({"Résistances", strong, ui.theme().danger});
        if (!d.finalAttacks.empty())
            facts.push_back({"Dernier souffle", std::to_string(d.finalAttacks.size()) + (d.finalAttacks.size() > 1 ? " attaques à survivre" : " attaque à survivre"),
                             ui.theme().warm});
        Rect view{area.x, area.y + ui.s(40), area.w, area.bottom() - area.y - ui.s(40)};
        float rowH = ui.s(36);
        ui.beginScroll("facts", view, static_cast<float>(facts.size()) * rowH);
        float y = view.y - ui.scrollOffset();
        for (size_t i = 0; i < facts.size(); ++i) {
            Rect row{view.x, y, view.w - ui.s(14), rowH - ui.s(4)};
            y += rowH;
            if (row.bottom() < view.y || row.y > view.bottom()) continue;
            ui.r().fillRoundedRect(row, ui.s(6), i % 2 ? Color{12, 18, 38, 160} : Color{16, 24, 48, 200});
            // The value first (as whole as it can be: all the room the label leaves), the label
            // in the room left.
            float lw0 = ui.text().measure(facts[i].label, FontFace::Body, ui.s(fz(ui, 16))).x;
            float vroom = std::max(row.w * 0.5f, row.w - ui.s(36) - lw0);
            float vf = fit(ui, facts[i].value, FontFace::Bold, 17, vroom, 0.7f);
            std::string value = ui.elide(facts[i].value, FontFace::Bold, vf, vroom);
            float vw = ui.text().measure(value, FontFace::Bold, ui.s(vf)).x;
            ui.label(value, {row.right() - ui.s(12), row.center().y}, vf, facts[i].c, Align::Right, FontFace::Bold, VAlign::Middle);
            float lroom = row.w - ui.s(24) - vw - ui.s(12);
            float lf = fit(ui, facts[i].label, FontFace::Body, 16, lroom, 0.75f);
            ui.label(ui.elide(facts[i].label, FontFace::Body, lf, lroom), {row.x + ui.s(12), row.center().y}, lf, ui.theme().textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
        }
        ui.endScroll();
    }

    /// Its phases (from which share of its health, its rhythm, its attacks), its last stand,
    /// then every attack once (type, damage, wind-up, cooldown). One scroll region: the
    /// content's height is the one measured on the previous frame.
    void drawSheetPhases(const Rect& area, const EnemyDefinition& d, float alpha) {
        UIContext& ui = g_.ui;
        ui.label("Phases et attaques", {area.x, area.y + ui.s(14)}, 22, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
        Rect view{area.x, area.y + ui.s(40), area.w, area.bottom() - area.y - ui.s(40)};
        if (sheetOf_ != d.id) {
            sheetOf_ = d.id;
            sheetH_ = view.h;
            ui.setScroll("sheet", 0.0f);
        }
        ui.beginScroll("sheet", view, sheetH_);
        const float top = view.y - ui.scrollOffset();
        float y = top;
        const float w = view.w - ui.s(16);
        auto attackName = [&](const std::string& id) {
            const AttackDef* a = d.attack(id);
            return a ? a->name : id;
        };
        auto names = [&](const std::vector<std::string>& ids) {
            std::string out;
            for (const auto& id : ids) {
                if (!out.empty()) out += "  ·  ";
                out += attackName(id);
            }
            return out;
        };
        // One block: a title line (a right part when it fits), a dim line, the attacks wrapped.
        auto block = [&](const std::string& title, const std::string& right, const std::string& rhythm, const std::string& attacks, Color accent) {
            const float pad = ui.s(12);
            float inner = w - 2 * pad;
            float af = fz(ui, 15);
            float ah = attacks.empty() ? 0.0f : ui.text().measureWrapped(attacks, FontFace::Body, ui.s(af), inner).y;
            float rf = fit(ui, rhythm, FontFace::Body, 14, inner, 0.75f);
            float h = pad + ui.s(24) + (rhythm.empty() ? 0.0f : ui.s(22)) + (ah > 0.0f ? ah + ui.s(6) : 0.0f) + pad;
            Rect b{view.x, y, w, h};
            if (b.bottom() >= view.y && b.y <= view.bottom()) {
                ui.r().fillRoundedRect(b, ui.s(8), Color{12, 18, 38, 220});
                ui.r().fillRect({b.x, b.y + ui.s(6), ui.s(3), b.h - ui.s(12)}, accent);
                float ly = b.y + pad + ui.s(12);
                float rw = 0.0f;
                if (!right.empty()) {
                    float rfz = fit(ui, right, FontFace::Body, 15, inner * 0.45f, 0.7f);
                    std::string rt = ui.elide(right, FontFace::Body, rfz, inner * 0.45f);
                    rw = ui.text().measure(rt, FontFace::Body, ui.s(rfz)).x + ui.s(12);
                    ui.label(rt, {b.right() - pad, ly}, rfz, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
                }
                float troom = inner - rw;
                float tf = fit(ui, title, FontFace::Bold, 18, troom, 0.75f);
                ui.label(ui.elide(title, FontFace::Bold, tf, troom), {b.x + pad, ly}, tf, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
                float cy = b.y + pad + ui.s(24);
                if (!rhythm.empty()) {
                    ui.label(ui.elide(rhythm, FontFace::Body, rf, inner), {b.x + pad, cy + ui.s(11)}, rf, Color{170, 188, 210, 255}, Align::Left,
                             FontFace::Body, VAlign::Middle);
                    cy += ui.s(22);
                }
                if (ah > 0.0f) ui.labelWrapped(attacks, {b.x + pad, cy + ui.s(4)}, inner, af, ui.theme().text);
            }
            y += h + ui.s(8);
        };
        const size_t phases = d.phases.size();
        for (size_t i = 0; i < phases; ++i) {
            const BossPhase& ph = d.phases[i];
            std::string title = std::to_string(i + 1) + ". " + (ph.name.empty() ? "Phase " + std::to_string(i + 1) : ph.name);
            std::string from = i == 0 ? std::string("dès le début") : "à " + num(ph.hpThreshold * 100.0f, 1) + " % des PV";
            // 2.56: after the Phénix's « Cendres », the phase begins with its rebirth.
            if (i > 0 && d.phases[i - 1].rebirth > 0.0f) from = "renaît avec " + num(d.phases[i - 1].rebirth * 100.0f, 1) + " % des PV";
            std::string rhythm = "Vitesse " + times(ph.speedMultiplier) + "  ·  Recharge " + times(ph.cooldownMultiplier) + "  ·  Annonces " +
                                 times(ph.telegraphMultiplier);
            if (ph.rebirth > 0.0f) rhythm += "  ·  Se consume puis renaît";
            if (!ph.phylactery.empty()) rhythm += "  ·  Cache son âme dans un phylactère";  // 2.57: the Liche
            // 2.58: the Titan's heat gauge (how many attacks before it vents) and its stall.
            if (ph.heat > 0.0f) rhythm += "  ·  Surchauffe après " + std::to_string(static_cast<int>(std::ceil(1.0f / ph.heat - 0.001f))) + " attaques";
            if (!ph.stall.empty()) rhythm += "  ·  Cale à la fin : tournez les vannes";
            if (ph.combo > 1) rhythm += "  ·  Enchaîne " + std::to_string(ph.combo) + " attaques";
            if (ph.summonOnEnter) rhythm += "  ·  Appelle des renforts";
            if (ph.enrageTime > 0.0f) rhythm += "  ·  Rage après " + num(ph.enrageTime, 0) + " s";
            block(title, from, rhythm, names(ph.attacks), Color{255, 179, 71, 200});
        }
        if (phases == 0) {
            std::vector<std::string> all;
            for (const auto& a : d.attacks) all.push_back(a.id);
            block("Phase unique", "dès le début", "", names(all), Color{255, 179, 71, 200});
        }
        if (!d.finalAttacks.empty())
            block("Dernier souffle", "à 0 PV, invincible", d.finalText, names(d.finalAttacks), ui.theme().danger.withAlpha(220));
        // Every attack once (2.60: each one unfolds).
        y += ui.s(6);
        AttackList::Options opts;
        opts.scroll = "sheet";
        opts.feature = "stats.sheet.attacks";  // « NOUVEAU » (2.60)
        opts.boss = &d;
        // Its examples once the boss has been met (as its training: nothing is given away before).
        const BossRecord* rec = g_.bossStats.find(d.id);
        opts.engineShown = g_.collection.met.count(d.id) > 0 || (rec && rec->fights > 0);
        opts.engineHidden = "Rencontrez ce boss dans son niveau : l'exemple de ses attaques s'ouvre ensuite.";
        opts.alpha = alpha;
        y = attacks_.draw(view, top, y, w, d.id, attackItems(d), opts);
        sheetH_ = y - top;
        ui.endScroll();
    }

    // ------------------------------------------------- the attacks, folded or unfolded (2.60)

    /// The attacks of `d` as the sheet shows them (read again when another boss is shown).
    const std::vector<AttackListItem>& attackItems(const EnemyDefinition& d) {
        if (entriesOf_ != d.id) {
            entriesOf_ = d.id;
            items_ = attackListItems(bossAttackEntries(d, [this](const std::string& id) {
                const EnemyDefinition* e = g_.content.enemy(id);
                return e ? e->name : id;
            }));
        }
        return items_;
    }

    /// The boss in motion: every clip in turn once beaten (activate: the next one), its
    /// silhouette before, as in the gallery.
    void drawAnimation(const Rect& box, const EnemyDefinition& d) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        bool known = beaten(d);
        bool hovered = false;
        // Always reachable (keyboard, gamepad, mouse): its « NOUVEAU » can be seen before the
        // boss is beaten; until then, activating it says no.
        bool activated = ui.hotspot("anim", box, &hovered);
        bool next = activated && known && clips_.size() > 1;
        if (activated && !known) g_.uiSound("ui_error");
        bool focus = ui.focused("anim");
        r.fillRoundedRect(box, ui.s(12), Color{11, 18, 38, 255});
        r.drawRoundedRect(box, ui.s(12), focus || hovered ? ui.theme().accent : ui.theme().accent.withAlpha(50), std::max(1.0f, ui.s(focus ? 2.0f : 1.2f)));
        if (next) {
            clip_ = (clip_ + 1) % static_cast<int>(clips_.size());
            clipT_ = 0.0f;
            holdClip_ = false;
        }
        std::string clip = "idle";
        if (known && !clips_.empty()) {
            clip_ %= static_cast<int>(clips_.size());
            if (!holdClip_ && clipT_ >= clipHold(creatureClipDuration(g_, d, clips_[static_cast<size_t>(clip_)]))) {
                clip_ = (clip_ + 1) % static_cast<int>(clips_.size());
                clipT_ = 0.0f;
            }
            clip = clips_[static_cast<size_t>(clip_)];
        }
        const float captionH = ui.s(40);
        Rect stage{box.x + ui.s(14), box.y + ui.s(14), box.w - ui.s(28), box.h - ui.s(14) - captionH};
        r.pushClip(box);
        drawCreature(g_, d, stage, known ? clipT_ : t_, known ? Color::white() : Color{0, 0, 0, 110}, clip);
        r.popClip();
        if (!known) ui.label("?", stage.center(), 48, Color{90, 106, 130, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
        Rect cap{box.x + ui.s(1), box.bottom() - captionH, box.w - ui.s(2), captionH - ui.s(1)};
        r.fillRect(cap, Color{6, 10, 22, 200});
        std::string text = !known ? "Battez-le pour le voir en entier"
                                  : creatureClipLabel(d, clip) + "  ·  " + std::to_string(clip_ + 1) + " / " + std::to_string(clips_.size());
        float room = cap.w - ui.s(24);
        float f = fit(ui, text, FontFace::Body, 15, room, 0.75f);
        ui.label(ui.elide(text, FontFace::Body, f, room), cap.center(), f, known ? ui.theme().text : ui.theme().textDim, Align::Center,
                 FontFace::Body, VAlign::Middle);
        // « NOUVEAU » (2.27) on the animated boss; seen once it gets the focus or a click.
        ui.newBadge("anim", box, "stats.animations");
    }

    void drawPhases(const Rect& area, const EnemyDefinition& d, const BossRecord& r, bool met) {
        UIContext& ui = g_.ui;
        Vec2 hs = ui.label("Phases", {area.x, area.y + ui.s(14)}, 22, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
        const std::string hint = "battues · dont sans dégât";
        float hroom = area.w - hs.x - ui.s(20);
        float hf = fit(ui, hint, FontFace::Body, 15, hroom, 0.7f);
        ui.label(ui.elide(hint, FontFace::Body, hf, hroom), {area.right(), area.y + ui.s(14)}, hf, ui.theme().textDim, Align::Right, FontFace::Body,
                 VAlign::Middle);
        Rect view{area.x, area.y + ui.s(40), area.w, area.bottom() - area.y - ui.s(40)};
        size_t phases = std::max<size_t>(1, d.phases.size());
        const float reserve = met ? 0.0f : ui.s(80);  // the note below
        float rowH = std::clamp((view.h - reserve) / static_cast<float>(phases), ui.s(46), ui.s(74));
        // Many phases (Singularis: 8) in a short panel: they scroll, the note with them.
        const float content = rowH * static_cast<float>(phases) + reserve;
        ui.beginScroll("phases", view, content);
        float y = view.y - ui.scrollOffset();
        const float rw = view.w - (content > view.h ? ui.s(14) : 0.0f);
        for (size_t i = 0; i < phases; ++i) {
            std::string name = i < d.phases.size() && !d.phases[i].name.empty() ? d.phases[i].name : "Phase " + std::to_string(i + 1);
            int clears = i < r.phaseClears.size() ? r.phaseClears[i] : 0;
            int perfect = i < r.phaseNoHit.size() ? r.phaseNoHit[i] : 0;
            Rect row{area.x, y, rw, rowH - ui.s(8)};
            ui.r().fillRoundedRect(row, ui.s(8), Color{12, 18, 38, 220});
            float ly = row.y + row.h * 0.36f;
            std::string counts = std::to_string(clears) + "  ·  " + std::to_string(perfect) + " sans dégât";
            float cf = fit(ui, counts, FontFace::Bold, 16, row.w * 0.5f, 0.7f);
            float cw = ui.text().measure(counts, FontFace::Bold, ui.s(cf)).x;
            ui.label(counts, {row.right() - ui.s(14), ly}, cf, perfect > 0 ? ui.theme().success : ui.theme().textDim, Align::Right, FontFace::Bold,
                     VAlign::Middle);
            std::string label = std::to_string(i + 1) + ". " + name;
            float room = row.w - ui.s(28) - cw - ui.s(16);
            float lf = fit(ui, label, FontFace::Bold, 18, room, 0.75f);
            ui.label(ui.elide(label, FontFace::Bold, lf, room), {row.x + ui.s(14), ly}, lf, ui.theme().text, Align::Left, FontFace::Bold,
                     VAlign::Middle);
            Rect bar{row.x + ui.s(14), row.y + row.h * 0.74f - ui.s(5), row.w - ui.s(28), ui.s(10)};
            ui.r().fillRoundedRect(bar, ui.s(5), Color{26, 34, 60, 255});
            if (clears > 0) {
                float f = std::clamp(static_cast<float>(perfect) / static_cast<float>(clears), 0.0f, 1.0f);
                if (f > 0.0f) ui.r().fillRoundedRect({bar.x, bar.y, std::max(bar.h, bar.w * f), bar.h}, ui.s(5), ui.theme().success);
            }
            y += rowH;
        }
        if (!met)
            ui.labelWrapped("Pas encore affronté : ses statistiques commenceront au premier combat.", {area.x, y + ui.s(6)}, rw, fz(ui, 16),
                            ui.theme().textDim);
        ui.endScroll();
    }

    /// « Entraînement »: for a boss already met (as the gallery, nothing is given away before);
    /// otherwise the reason, and nothing starts.
    void drawTraining(const Rect& area, const EnemyDefinition& d, const BossRecord* rec) {
        UIContext& ui = g_.ui;
        std::string reason;
        if (!atlas_) reason = "Recherche de son arène…";
        else if (!bossTrainingPlace(*atlas_, g_.content, d.id)) reason = "Dans aucun niveau du jeu : pas d'arène";
        else if (!g_.collection.met.count(d.id) && !(rec && rec->fights > 0)) reason = "À rencontrer d'abord dans son niveau";
        UIContext::ButtonStyle st;
        st.enabled = reason.empty();
        float bw = std::min(ui.s(250), area.w * 0.48f);
        bool trainClicked = ui.button("train", {area.x, area.y, bw, area.h}, "Entraînement", st);
        ui.newBadge("train", {area.x, area.y, bw, area.h}, "stats.training");  // « NOUVEAU » (2.28)
        if (trainClicked && reason.empty())
            g_.app.switchScene(makeBossTraining(g_, d.id), 0.3f);
        std::string note = reason.empty() ? "Ne compte pas pour les succès ni les statistiques" : reason;
        float tx = area.x + bw + ui.s(16), room = area.right() - tx;
        if (room < ui.s(60)) return;
        // One or two lines, centred on the button.
        float f = fz(ui, 15);
        float one = ui.text().measure(note, FontFace::Body, ui.s(f)).x;
        Color c = reason.empty() ? ui.theme().textDim : ui.theme().warm;
        if (one <= room) {
            ui.label(note, {tx, area.center().y}, f, c, Align::Left, FontFace::Body, VAlign::Middle);
            return;
        }
        // Two lines, cut at the space that balances them best, each fitted to the room (the text
        // as displayed, translated first: its halves are drawn as they are).
        const std::string shown = i18n::tr(note);
        i18n::Verbatim asShown;
        std::string a = shown, b;
        float best = 1e30f;
        for (size_t i = shown.find(' '); i != std::string::npos; i = shown.find(' ', i + 1)) {
            std::string l1 = shown.substr(0, i), l2 = shown.substr(i + 1);
            float wmax = std::max(ui.text().measure(l1, FontFace::Body, ui.s(f)).x, ui.text().measure(l2, FontFace::Body, ui.s(f)).x);
            if (wmax < best) {
                best = wmax;
                a = l1;
                b = l2;
            }
        }
        float f2 = std::min(fit(ui, a, FontFace::Body, f, room), fit(ui, b, FontFace::Body, f, room));
        float lineH = ui.s(f2) * 1.3f;
        ui.label(ui.elide(a, FontFace::Body, f2, room), {tx, area.center().y - lineH * 0.5f}, f2, c, Align::Left, FontFace::Body, VAlign::Middle);
        ui.label(ui.elide(b, FontFace::Body, f2, room), {tx, area.center().y + lineH * 0.5f}, f2, c, Align::Left, FontFace::Body, VAlign::Middle);
    }

    /// Every place of the game's levels where it can be fought; each opens its level's page.
    void drawPlaces(const Rect& area, const EnemyDefinition& d) {
        UIContext& ui = g_.ui;
        Vec2 hs = ui.label("Où l'affronter", {area.x, area.y + ui.s(14)}, 22, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
        // « NOUVEAU » (2.27) after the title; seen once a place gets the focus or a click.
        if (ui.isNew("stats.locations"))
            ui.drawNewPill(area.x + hs.x + ui.s(14) + ui.newPillWidth(), area.y + ui.s(14));
        Rect view{area.x, area.y + ui.s(40), area.w, area.bottom() - area.y - ui.s(40)};
        if (!atlas_) {
            // Reading the levels (worker thread): a turning arc while it lasts.
            Vec2 c{view.x + ui.s(14), view.y + ui.s(20)};
            ui.r().drawArc(c, ui.s(10), std::max(1.5f, ui.s(3)), t_ * 5.0f, t_ * 5.0f + 4.2f, ui.theme().accent);
            float room = view.w - ui.s(40);
            float f = fit(ui, "Recherche dans les niveaux du jeu…", FontFace::Body, 16, room, 0.75f);
            ui.label("Recherche dans les niveaux du jeu…", {c.x + ui.s(22), c.y}, f, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            return;
        }
        std::string count;
        if (!places_.empty()) count = std::to_string(places_.size()) + (places_.size() > 1 ? " lieux" : " lieu");
        float croom = area.w - hs.x - ui.s(20);
        if (!count.empty() && croom > 0.0f) {
            float cf = fit(ui, count, FontFace::Body, 15, croom, 0.7f);
            ui.label(count, {area.right(), area.y + ui.s(14)}, cf, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
        }
        if (places_.empty()) {
            ui.labelWrapped(d.builtin ? "Ce boss n'est placé dans aucun niveau du jeu."
                                      : "Ce boss n'est placé dans aucun niveau du jeu : il vient de l'éditeur d'ennemis ou d'un pack importé.",
                            {view.x, view.y + ui.s(4)}, view.w, fz(ui, 16), ui.theme().textDim);
            return;
        }
        float rowH = ui.s(128), gap = ui.s(10);
        ui.beginScroll("places", view, static_cast<float>(places_.size()) * (rowH + gap) - gap);
        float y = view.y - ui.scrollOffset();
        for (size_t i = 0; i < places_.size(); ++i) {
            Rect row{view.x, y, view.w - ui.s(14), rowH};
            y += rowH + gap;
            drawPlace(row, i, places_[i], d);
        }
        ui.endScroll();
    }

    void drawPlace(const Rect& row, size_t index, const CreaturePlace& p, const EnemyDefinition& d) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const LevelEntry* level = g_.content.level(p.levelId);
        const PlanetData* planet = g_.content.planet(p.planetId);
        bool open = level && planet && levelPlanetOpen(*level, g_.content, g_.progression);
        std::string lock = level ? levelLockReason(*level, g_.content, g_.progression) : std::string{};
        bool done = g_.progression.levelCompleted(p.levelId);
        std::string id = "p" + std::to_string(index);
        bool hovered = false;
        bool activated = ui.hotspot(id, row, &hovered);
        ui.newBadge(id, row, "stats.locations", false);
        if (activated) {
            if (open) g_.app.switchScene(makeLevelSelect(g_, p.planetId, p.levelId, d.id), 0.3f);
            else g_.uiSound("ui_error");
        }
        if (row.bottom() < 0.0f || row.y > ui.screenH()) return;
        bool focus = ui.focused(id);
        r.fillRoundedRect(row, ui.s(10), focus || hovered ? Color{20, 32, 60, 240} : Color{12, 18, 38, 225});
        r.drawRoundedRect(row, ui.s(10), focus ? ui.theme().accent : ui.theme().accent.withAlpha(hovered ? 120 : 36), std::max(1.0f, ui.s(focus ? 2.0f : 1.2f)));
        // The planet, as seen in the levels.
        Rect th{row.x + ui.s(12), row.y + ui.s(12), ui.s(132), row.h - ui.s(24)};
        r.pushClip(th);
        if (planet) drawPlanetBackdrop(g_, planet->id, th, open ? Color::white() : Color{70, 74, 96, 255});
        else r.fillRect(th, Color{8, 11, 24, 255});
        r.popClip();
        r.drawRect(th, (planet ? planet->color : ui.theme().border).withAlpha(140), std::max(1.0f, ui.s(1.0f)));
        float tx = th.right() + ui.s(16), room = row.right() - tx - ui.s(14);
        auto line = [&](const std::string& text, float yy, float size, Color c, FontFace face) {
            float f = fit(ui, text, face, size, room);
            ui.label(ui.elide(text, face, f, room), {tx, row.y + ui.s(yy)}, f, c, Align::Left, face, VAlign::Middle);
        };
        line(p.levelName, 26, 19, ui.theme().text, FontFace::Bold);
        // Which level, which fight.
        std::string where = planet ? planet->name : std::string("Hors campagne");
        if (p.levelNumber > 0) where += " · " + std::string("niveau ") + std::to_string(p.levelNumber);
        if (p.enemyId != d.id) where += " · " + std::string("son écho");
        if (p.bossCount > 1) where += " · " + std::string("boss ") + std::to_string(p.bossRank) + " sur " + std::to_string(p.bossCount);
        line(where, 54, 16, ui.theme().textDim, FontFace::Body);
        // Where in the level.
        std::string spot = !p.checkpointLabel.empty() ? "Après le point de contrôle « " + p.checkpointLabel + " »"
                           : !p.checkpointId.empty()  ? std::string("Après un point de contrôle")
                                                      : std::string("Depuis le départ du niveau, sans point de contrôle");
        line(spot, 81, 16, Color{185, 200, 218, 255}, FontFace::Body);
        // Can it be played?
        float sy = row.y + ui.s(108);
        if (!lock.empty() || !open) {
            float is = ui.s(22);
            ui.icon("ui/icons_hud.png", 9, 16, {tx, sy - is * 0.5f, is, is});
            float lroom = room - is - ui.s(8);
            std::string text = !lock.empty() ? lock : std::string("Niveau hors campagne");
            float f = fit(ui, text, FontFace::Body, 16, lroom, 0.8f);
            ui.label(ui.elide(text, FontFace::Body, f, lroom), {tx + is + ui.s(8), sy}, f, ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
        } else {
            line(done ? "Niveau terminé · voir sa page" : "Ouvert · voir sa page", 108, 16, done ? ui.theme().success : ui.theme().accent, FontFace::Body);
        }
    }

    GameServices& g_;
    std::string wanted_;
    std::string fromAsset_;  // 2.63: opened from ADMINISTRATION › Assets on this asset
    MenuBackground bg_;
    std::vector<const EnemyDefinition*> bosses_;
    int selected_ = 0;
    float t_ = 0.0f;
    // The boss shown: its clips (one after the other), its places.
    std::string shown_;
    std::vector<std::string> clips_;
    int clip_ = 0;
    float clipT_ = 0.0f;
    int forcedClip_ = -1;
    bool holdClip_ = false;
    std::shared_ptr<const CreatureAtlas> atlas_;
    int tab_ = 0;              // 0: « Mes statistiques », 1: « Fiche », 2: « Où frapper » (2.76.0)
    HitPartsView hitView_;     // 2.76.0
    std::string sheetOf_;      // the boss whose sheet is shown, and its content's height
    float sheetH_ = 0.0f;
    std::string placesOf_;
    std::vector<CreaturePlace> places_;
    // 2.60: the attacks of the sheet (read from the boss shown), unfolded or not per boss for
    // the whole visit, and the one example played.
    std::string entriesOf_;
    std::vector<AttackListItem> items_;
    AttackList attacks_;  // 2.73: ui/widgets/BossSheetWidgets (shared with the raid sheets)
};

}  // namespace

std::unique_ptr<Scene> makeBossStats(GameServices& g, const std::string& bossId, const std::string& fromAsset) {
    return std::make_unique<BossStatsScene>(g, bossId, fromAsset);
}

}  // namespace cosmic
