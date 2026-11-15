// Main menu → Statistiques: every boss of the game with the player's counters, kept on this
// computer (game/records/BossStats.h): fights, victories, defeats, victories without being
// hit, and for each phase how many times it was beaten and beaten without a hit. The boss is
// shown with its own animations (once beaten, as in the Collection's gallery: a silhouette
// before), with every place of the game's levels where it can be fought (read from the levels
// on a worker thread, game/content/CreaturePlaces.h); a place opens its level's page. A boss
// met can be trained against (« Entraînement », ui/screens/BossTrainingScene.cpp).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/content/CreaturePlaces.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/BossBadge.h"
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

/// How long a clip is shown before the next one: whole loops, about 2.4 seconds.
float clipHold(float loop) {
    if (loop <= 0.05f) return 2.4f;
    return loop * std::max(1.0f, std::ceil(2.4f / loop));
}

class BossStatsScene : public Scene {
public:
    BossStatsScene(GameServices& g, std::string bossId) : g_(g), wanted_(std::move(bossId)) {}

    void onEnter() override {
        bg_.init(g_);
        for (const auto* d : g_.content.enemies())
            if (d->isBoss) bosses_.push_back(d);
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
        // Automated screenshots: "boss:<id>[:<clip>]" (the clip then stays shown).
        if (!g_.onlineOpen.empty() && g_.onlineOpen.rfind("boss:", 0) == 0) {
            std::string v = g_.onlineOpen.substr(5);
            if (auto c = v.find(':'); c != std::string::npos) {
                forcedClip_ = std::max(0, std::atoi(v.c_str() + c + 1));
                v = v.substr(0, c);
            }
            wanted_ = v;
            g_.onlineOpen.clear();
        }
        for (size_t i = 0; i < bosses_.size(); ++i)
            if (bosses_[i]->id == wanted_) selected_ = static_cast<int>(i);
        g_.ui.setFocus("bstats/b" + std::to_string(selected_));
        atlas_ = officialCreatureAtlas(g_.content);  // starts reading the levels (worker thread)
    }

    void update(float dt) override {
        t_ += dt;
        clipT_ += dt;
        bg_.update(dt);
        if (!atlas_) atlas_ = officialCreatureAtlas(g_.content);
    }

    void render(float) override {
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
            drawDetail(detail, d);
        }
        if (leave || ui.backPressed()) g_.app.switchScene(makeMainMenu(g_), 0.3f);
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

    void drawDetail(const Rect& p, const EnemyDefinition& d) {
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
        float tf = ui.fitFont(d.name, FontFace::Title, 32, rw, 0.6f);
        ui.label(ui.elide(d.name, FontFace::Title, tf, rw), {rx, y + ui.s(20)}, tf, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
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
        // Below: the phases, and where to fight it.
        y = box.bottom() + ui.s(24);
        float bh = p.bottom() - ui.s(22) - y;
        float colGap = ui.s(28);
        float leftW = std::floor((w - colGap) * 0.44f);
        const float trainH = ui.s(60);
        drawPhases({x, y, leftW, bh - trainH - ui.s(14)}, d, r, rec && r.fights > 0);
        drawTraining({x, y + bh - trainH, leftW, trainH}, d, rec);
        drawPlaces({x + leftW + colGap, y, w - leftW - colGap, bh}, d);
    }

    /// The boss in motion: every clip in turn once beaten (activate: the next one), its
    /// silhouette before, as in the gallery.
    void drawAnimation(const Rect& box, const EnemyDefinition& d) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        bool known = beaten(d);
        bool hovered = false;
        bool next = known && clips_.size() > 1 && ui.hotspot("anim", box, &hovered);
        bool focus = known && ui.focused("anim");
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
    }

    void drawPhases(const Rect& area, const EnemyDefinition& d, const BossRecord& r, bool met) {
        UIContext& ui = g_.ui;
        Vec2 hs = ui.label("Phases", {area.x, area.y + ui.s(14)}, 22, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
        const std::string hint = "battues · dont sans dégât";
        float hroom = area.w - hs.x - ui.s(20);
        float hf = fit(ui, hint, FontFace::Body, 15, hroom, 0.7f);
        ui.label(ui.elide(hint, FontFace::Body, hf, hroom), {area.right(), area.y + ui.s(14)}, hf, ui.theme().textDim, Align::Right, FontFace::Body,
                 VAlign::Middle);
        float y = area.y + ui.s(40);
        size_t phases = std::max<size_t>(1, d.phases.size());
        const float reserve = met ? 0.0f : ui.s(80);  // the note below
        float rowH = std::clamp((area.bottom() - y - reserve) / static_cast<float>(phases), ui.s(46), ui.s(74));
        for (size_t i = 0; i < phases; ++i) {
            std::string name = i < d.phases.size() && !d.phases[i].name.empty() ? d.phases[i].name : "Phase " + std::to_string(i + 1);
            int clears = i < r.phaseClears.size() ? r.phaseClears[i] : 0;
            int perfect = i < r.phaseNoHit.size() ? r.phaseNoHit[i] : 0;
            Rect row{area.x, y, area.w, rowH - ui.s(8)};
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
            ui.labelWrapped("Pas encore affronté : ses statistiques commenceront au premier combat.", {area.x, y + ui.s(6)}, area.w, fz(ui, 16),
                            ui.theme().textDim);
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
        if (ui.button("train", {area.x, area.y, bw, area.h}, "Entraînement", st) && reason.empty())
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
        if (ui.hotspot(id, row, &hovered)) {
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
    std::string placesOf_;
    std::vector<CreaturePlace> places_;
};

}  // namespace

std::unique_ptr<Scene> makeBossStats(GameServices& g, const std::string& bossId) { return std::make_unique<BossStatsScene>(g, bossId); }

}  // namespace cosmic
