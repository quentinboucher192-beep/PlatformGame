// Boss statistics → « Entraînement »: the settings of a training against a boss (hearts,
// power slots, the powers in them, all its phases or one of them), then its own arena
// (game/gameplay/BossTraining.h), and the end of a training (time, damage, phase reached;
// Recommencer, Changer les réglages, Retour). A training never counts: nothing of it reaches
// the achievements, the statistics, the records, the progression or Cosmic Online.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/content/CreaturePlaces.h"
#include "game/gameplay/BossTraining.h"
#include "game/player/PlayerConfig.h"
#include "ui/screens/CreatureArt.h"
#include "ui/screens/GameScene.h"
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

const char* const kNotice = "Entraînement — ne compte pas pour les succès ni les statistiques";

/// A name as shown, lower case and without accents, to sort names alphabetically.
std::string sortKey(const std::string& name) {
    // The Latin-1 letters (U+00C0..U+00FF) without their accents.
    static const char kBase[64] = {'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',
                                   'd', 'n', 'o', 'o', 'o', 'o', 'o', ' ', 'o', 'u', 'u', 'u', 'u', 'y', ' ', 's',
                                   'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',
                                   'd', 'n', 'o', 'o', 'o', 'o', 'o', ' ', 'o', 'u', 'u', 'u', 'u', 'y', ' ', 'y'};
    const std::string shown = i18n::tr(name);
    std::string k;
    for (size_t i = 0; i < shown.size(); ++i) {
        auto c = static_cast<unsigned char>(shown[i]);
        auto next = i + 1 < shown.size() ? static_cast<unsigned char>(shown[i + 1]) : 0;
        if (c == 0xC3 && next >= 0x80 && next <= 0xBF) {
            k += kBase[next - 0x80];
            ++i;
        } else if (c == 0xC5 && (next == 0x92 || next == 0x93)) {  // Œ œ
            k += 'o';
            k += 'e';
            ++i;
        } else {
            k += c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
        }
    }
    return k;
}

std::string fightTime(float s) {
    int t = static_cast<int>(std::lround(std::max(0.0f, s) * 10.0f));
    char b[32];
    std::snprintf(b, sizeof b, "%d:%02d,%d", t / 600, (t / 10) % 60, t % 10);
    return b;
}

/// The notice, in a warm pill sized to its text (`room` at most); returns its height.
float drawNotice(UIContext& ui, Vec2 leftMid, float room) {
    float fs = fit(ui, kNotice, FontFace::Bold, 17, room - ui.s(36), 0.7f);
    float tw = ui.text().measure(kNotice, FontFace::Bold, ui.s(fs)).x;
    Rect pill{leftMid.x, leftMid.y - ui.s(fs + 18) * 0.5f, std::min(room, tw + ui.s(36)), ui.s(fs + 18)};
    ui.r().fillRoundedRect(pill, pill.h * 0.5f, ui.theme().warm.withAlpha(30));
    ui.r().drawRoundedRect(pill, pill.h * 0.5f, ui.theme().warm.withAlpha(170), std::max(1.0f, ui.s(1.5f)));
    ui.label(ui.elide(kNotice, FontFace::Bold, fs, pill.w - ui.s(36)), {pill.x + ui.s(18), pill.center().y}, fs, ui.theme().warm, Align::Left,
             FontFace::Bold, VAlign::Middle);
    return pill.h;
}

// =================================================================== settings

class BossTrainingScene : public Scene {
public:
    BossTrainingScene(GameServices& g, std::string bossId) : g_(g), bossId_(std::move(bossId)) {}

    void onEnter() override {
        bg_.init(g_);
        def_ = g_.content.enemy(bossId_);
        prefs_.load();
        atlas_ = officialCreatureAtlas(g_.content);
        for (const PowerDefinition* p : g_.content.powers())
            if (g_.content.isNativePower(p->id)) powers_.push_back(p);
        // In the alphabetical order of their names as shown (accents aside).
        std::sort(powers_.begin(), powers_.end(), [](const PowerDefinition* a, const PowerDefinition* b) { return sortKey(a->name) < sortKey(b->name); });
        if (def_) clips_ = creatureClips(g_, *def_);
        if (g_.onlineOpen == "train-go") {  // automated screenshots
            autoGo_ = true;
            g_.onlineOpen.clear();
        }
        g_.ui.setFocus("btrain/go");
    }

    void update(float dt) override {
        t_ += dt;
        clipT_ += dt;
        bg_.update(dt);
        if (!atlas_) atlas_ = officialCreatureAtlas(g_.content);
        if (atlas_ && !placeRead_) readPlace();
        if (msgTimer_ > 0.0f) msgTimer_ -= dt;
        if (autoGo_ && placeRead_ && lockReason().empty()) {
            autoGo_ = false;
            launch();
        }
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.7f);
        ui.pushId("btrain");
        const float W = ui.screenW(), H = ui.screenH(), m = ui.s(48);
        UIContext::ButtonStyle small;
        small.small = true;
        Rect back{W - m - ui.s(200), ui.s(30), ui.s(200), ui.s(46)};
        bool leave = ui.button("back", back, "Retour", small);
        float tf = ui.fitFont("ENTRAÎNEMENT", FontFace::Title, 40, back.x - m - ui.s(24), 0.6f);
        ui.label("ENTRAÎNEMENT", {m, ui.s(52)}, tf, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        std::string who = !def_ ? bossId_ : def_->bossTitle.empty() ? def_->name : def_->bossTitle;
        ui.label(ui.elide(who, FontFace::Bold, fz(ui, 20), W - 2 * m), {m, ui.s(100)}, fz(ui, 20), ui.theme().accent, Align::Left, FontFace::Bold,
                 VAlign::Middle);
        drawNotice(ui, {m, ui.s(146)}, W - 2 * m);
        float top = ui.s(184), bottom = H - ui.s(108);
        Rect left{m, top, std::min(ui.s(560), (W - 2 * m) * 0.36f), bottom - top};
        Rect right{left.right() + ui.s(24), top, W - m - left.right() - ui.s(24), bottom - top};
        drawBoss(left);
        drawSettings(right);
        // Bottom bar: why it cannot start (if so), Retour, Entrer dans l'arène.
        float by = H - ui.s(84);
        UIContext::ButtonStyle go;
        go.primary = true;
        std::string reason = lockReason();
        go.enabled = reason.empty();
        Rect goR{W - m - ui.s(400), by, ui.s(400), ui.s(60)};
        bool start = ui.button("go", goR, "Entrer dans l'arène", go);
        float note = goR.x - m - ui.s(24);
        if (!reason.empty() && note > ui.s(120)) {
            float f = fit(ui, reason, FontFace::Body, 17, note, 0.75f);
            ui.label(ui.elide(reason, FontFace::Body, f, note), {m, goR.center().y}, f, ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
        } else if (msgTimer_ > 0.0f && note > ui.s(120)) {
            float f = fit(ui, msg_, FontFace::Body, 17, note, 0.75f);
            ui.label(ui.elide(msg_, FontFace::Body, f, note), {m, goR.center().y}, f, ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
        }
        ui.popId();
        if (start && reason.empty()) launch();
        else if (start) g_.uiSound("ui_error");
        if (leave || ui.backPressed()) g_.app.switchScene(makeBossStats(g_, bossId_), 0.3f);
        ui.end();
    }

private:
    /// Why the training cannot start ("" = it can).
    std::string lockReason() const {
        if (!def_ || !def_->isBoss) return "Ce boss n'existe plus.";
        if (!atlas_) return "Recherche de son arène dans les niveaux…";
        if (!place_) return "Ce boss n'est placé dans aucun niveau du jeu : pas d'arène où s'entraîner.";
        const BossRecord* rec = g_.bossStats.find(bossId_);
        if (!g_.collection.met.count(bossId_) && !(rec && rec->fights > 0)) return "Rencontrez-le d'abord dans son niveau : l'entraînement s'ouvre ensuite.";
        if (!level_) return "Son niveau ne peut pas être lu.";
        return {};
    }

    /// Its arena (from the levels), and the choices: the last ones for this boss, else the
    /// player's own hearts, the level's slots and the powers found on the way.
    void readPlace() {
        placeRead_ = true;
        place_ = bossTrainingPlace(*atlas_, g_.content, bossId_);
        if (!place_) return;
        level_ = g_.content.loadLevel(place_->levelId);
        int maxHearts = PlayerConfig::fromJson(g_.content.playerConfig()).maxHealth;
        hearts_ = std::clamp(maxHearts, kTrainingMinHearts, kTrainingMaxHearts);
        slots_ = std::clamp(level_ && level_->powerSlots() > 0 ? level_->powerSlots() : 2, 1, LevelData::kMaxPowerSlots);
        chosen_ = place_->powersBefore;
        if (static_cast<int>(chosen_.size()) > slots_) chosen_.erase(chosen_.begin(), chosen_.end() - slots_);
        if (const BossTrainingChoice* c = prefs_.find(bossId_)) {
            if (c->hearts > 0) hearts_ = std::clamp(c->hearts, kTrainingMinHearts, kTrainingMaxHearts);
            if (c->slots > 0) slots_ = std::clamp(c->slots, 1, LevelData::kMaxPowerSlots);
            chosen_ = c->powers;
            phase_ = c->phase;
        }
        chosen_ = fitTrainingPowers(g_.content, chosen_, slots_);
        int phases = def_ ? static_cast<int>(def_->phases.size()) : 0;
        if (phase_ >= phases) phase_ = -1;
    }

    void drawBoss(const Rect& area) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        ui.panel(area, false, 16);
        if (!def_) return;
        float x = area.x + ui.s(20), w = area.w - ui.s(40), y = area.y + ui.s(20);
        Rect box{x, y, w, std::min(ui.s(300), area.h * 0.42f)};
        r.fillRoundedRect(box, ui.s(12), Color{11, 18, 38, 255});
        // As in the statistics and the gallery: its colours and its clips once beaten.
        const BossRecord* rec = g_.bossStats.find(bossId_);
        bool known = rec && rec->kills > 0;
        std::string clip = "idle";
        if (known && !clips_.empty()) {
            clip_ %= static_cast<int>(clips_.size());
            float loop = std::max(0.05f, creatureClipDuration(g_, *def_, clips_[static_cast<size_t>(clip_)]));
            if (clipT_ >= loop * std::max(1.0f, std::ceil(2.4f / loop))) {
                clip_ = (clip_ + 1) % static_cast<int>(clips_.size());
                clipT_ = 0.0f;
            }
            clip = clips_[static_cast<size_t>(clip_)];
        }
        r.pushClip(box);
        drawCreature(g_, *def_, box.expanded(-ui.s(16)), known ? clipT_ : t_, known ? Color::white() : Color{0, 0, 0, 110}, clip);
        r.popClip();
        if (!known) ui.label("?", box.center(), 48, Color{90, 106, 130, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
        y = box.bottom() + ui.s(22);
        auto line = [&](const std::string& text, float size, Color c, FontFace face) {
            Vec2 sz = ui.labelWrapped(text, {x, y}, w, fz(ui, size), c);
            y += sz.y + ui.s(8);
            (void)face;
        };
        if (!atlas_) {
            line("Recherche de son arène dans les niveaux…", 16, ui.theme().textDim, FontFace::Body);
            return;
        }
        if (!place_) return;
        const PlanetData* planet = g_.content.planet(place_->planetId);
        ui.label("Arène", {x, y}, fz(ui, 18), ui.theme().accent, Align::Left, FontFace::Bold);
        y += ui.s(30);
        float nf = fit(ui, place_->levelName, FontFace::Bold, 20, w, 0.75f);
        ui.label(ui.elide(place_->levelName, FontFace::Bold, nf, w), {x, y}, nf, ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(32);
        std::string where = planet ? planet->name : std::string("Hors campagne");
        if (place_->levelNumber > 0) where += " · " + std::string("niveau ") + std::to_string(place_->levelNumber);
        line(where, 16, ui.theme().textDim, FontFace::Body);
        bool front = place_->checkpointId.empty() || place_->bossesBetween > 0;
        std::string start = !front && !place_->checkpointLabel.empty() ? "Départ : point de contrôle « " + place_->checkpointLabel + " »"
                            : !front                                   ? std::string("Départ : le point de contrôle avant son arène")
                                                                       : std::string("Départ : devant son arène");
        line(start, 16, Color{185, 200, 218, 255}, FontFace::Body);
        y += ui.s(6);
        // Its phases, with the life it has when each begins.
        if (def_->phases.size() > 1 && y + ui.s(30) < area.bottom()) {
            ui.label("Phases", {x, y}, fz(ui, 18), ui.theme().accent, Align::Left, FontFace::Bold);
            y += ui.s(30);
            for (size_t i = 0; i < def_->phases.size(); ++i) {
                if (y + ui.s(24) > area.bottom() - ui.s(10)) break;
                const BossPhase& ph = def_->phases[i];
                int pct = static_cast<int>(std::lround(ph.hpThreshold * 100.0f));
                std::string text = std::to_string(i + 1) + ". " + (ph.name.empty() ? "Phase " + std::to_string(i + 1) : ph.name) + " · " +
                                   std::to_string(pct) + " % de vie";
                float f = fit(ui, text, FontFace::Body, 16, w, 0.75f);
                ui.label(ui.elide(text, FontFace::Body, f, w), {x, y}, f, phase_ == static_cast<int>(i) ? ui.theme().text : ui.theme().textDim);
                y += ui.s(28);
            }
        }
    }

    void drawSettings(const Rect& area) {
        UIContext& ui = g_.ui;
        ui.panel(area, false, 16);
        float x = area.x + ui.s(24), w = area.w - ui.s(48), y = area.y + ui.s(22);
        if (!place_) {
            ui.labelWrapped(atlas_ ? "Pas d'arène : rien à régler." : "Recherche de son arène dans les niveaux…", {x, y}, w, fz(ui, 17),
                            ui.theme().textDim);
            return;
        }
        float gap = ui.s(20), rowH = ui.s(56);
        float half = (w - gap) * 0.5f;
        int hearts = hearts_, slots = slots_;
        ui.sliderInt("hearts", {x, y, half, rowH}, "Cœurs", hearts, kTrainingMinHearts, kTrainingMaxHearts);
        ui.sliderInt("slots", {x + half + gap, y, half, rowH}, "Emplacements de pouvoirs", slots, 1, LevelData::kMaxPowerSlots);
        hearts_ = hearts;
        if (slots != slots_) {
            slots_ = slots;
            chosen_ = fitTrainingPowers(g_.content, chosen_, slots_);
        }
        y += rowH + ui.s(14);
        std::vector<std::string> phases = {"Toutes les phases"};
        if (def_)
            for (size_t i = 0; i < def_->phases.size(); ++i)
                phases.push_back("Phase " + std::to_string(i + 1) + " : " + (def_->phases[i].name.empty() ? std::string("sans nom") : def_->phases[i].name));
        int idx = std::clamp(phase_ + 1, 0, static_cast<int>(phases.size()) - 1);
        if (ui.cycle("phase", {x, y, w, rowH}, "Phase", idx, phases)) phase_ = idx - 1;
        y += rowH + ui.s(22);
        // The powers: a grid of every power of the game, the chosen ones numbered by slot.
        std::string head = "Pouvoirs";
        Vec2 hs = ui.label(head, {x, y + ui.s(12)}, fz(ui, 20), ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
        std::string count = std::to_string(chosen_.size()) + " / " + std::to_string(slots_);
        Vec2 cs = ui.label(count, {x + hs.x + ui.s(14), y + ui.s(12)}, fz(ui, 20), chosen_.size() >= static_cast<size_t>(slots_) ? ui.theme().warm : ui.theme().text,
                           Align::Left, FontFace::Bold, VAlign::Middle);
        std::string hint = "Un pouvoir sur la même touche qu'un autre le remplace.";
        float hx = x + hs.x + cs.x + ui.s(40), hroom = x + w - hx;
        if (hroom > ui.s(80)) {
            float f = fit(ui, hint, FontFace::Body, 15, hroom, 0.75f);
            ui.label(ui.elide(hint, FontFace::Body, f, hroom), {x + w, y + ui.s(12)}, f, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
        }
        y += ui.s(34);
        const int cols = 3;
        float cgap = ui.s(10);
        float cw = (w - cgap * (cols - 1)) / cols;
        int rows = static_cast<int>((powers_.size() + cols - 1) / cols);
        float ch = std::clamp((area.bottom() - ui.s(18) - y - cgap * (rows - 1)) / std::max(1, rows), ui.s(40), ui.s(60));
        for (size_t i = 0; i < powers_.size(); ++i) {
            const PowerDefinition& p = *powers_[i];
            Rect c{x + (cw + cgap) * static_cast<float>(i % cols), y + (ch + cgap) * static_cast<float>(i / cols), cw, ch};
            auto it = std::find(chosen_.begin(), chosen_.end(), p.id);
            bool on = it != chosen_.end();
            UIContext::ButtonStyle st;
            st.selected = on;
            st.small = true;
            if (ui.button("pw" + std::to_string(i), c, "", st)) toggle(p.id);
            Rect icon{c.x + ui.s(10), c.center().y - (ch - ui.s(16)) * 0.5f, ch - ui.s(16), ch - ui.s(16)};
            ui.icon("ui/icons_powers.png", p.icon, 32, icon, on ? Color::white() : Color{150, 160, 180, 255});
            float tx = icon.right() + ui.s(10);
            float badge = on ? ui.s(30) : 0.0f;
            float room = c.right() - ui.s(12) - badge - tx;
            float f = fit(ui, p.name, FontFace::Bold, 17, room, 0.7f);
            ui.label(ui.elide(p.name, FontFace::Bold, f, room), {tx, c.center().y}, f, on ? ui.theme().text : ui.theme().textDim, Align::Left,
                     FontFace::Bold, VAlign::Middle);
            if (on) {
                // Its slot.
                Vec2 bc{c.right() - ui.s(24), c.center().y};
                ui.r().fillCircle(bc, ui.s(12), ui.theme().accent);
                ui.label(std::to_string(it - chosen_.begin() + 1), bc, fz(ui, 15), Color{8, 12, 26, 255}, Align::Center, FontFace::Bold,
                         VAlign::Middle);
            }
        }
    }

    void toggle(const std::string& id) {
        auto it = std::find(chosen_.begin(), chosen_.end(), id);
        if (it != chosen_.end()) {
            chosen_.erase(it);
            return;
        }
        if (static_cast<int>(chosen_.size()) >= slots_) {
            g_.uiSound("ui_error");
            say("Tous les emplacements sont pris : retirez d'abord un pouvoir, ou ajoutez un emplacement.");
            return;
        }
        std::vector<std::string> wanted = chosen_;
        wanted.push_back(id);
        std::vector<std::string> fitted = fitTrainingPowers(g_.content, wanted, slots_);
        // A power on the same input as one already chosen replaces it (as in a level).
        const PowerDefinition* added = g_.content.power(id);
        for (const std::string& old : chosen_)
            if (std::find(fitted.begin(), fitted.end(), old) == fitted.end())
                if (const PowerDefinition* o = g_.content.power(old); o && added)
                    say("« " + added->name + " » remplace « " + o->name + " » (même touche).");
        chosen_ = fitted;
    }

    void say(const std::string& m) {
        msg_ = m;
        msgTimer_ = 4.0f;
    }

    void launch() {
        if (!place_) return;
        BossTrainingChoice c;
        c.hearts = hearts_;
        c.slots = slots_;
        c.powers = chosen_;
        c.phase = phase_;
        prefs_.set(bossId_, c);
        prefs_.save();
        PlayOptions o;
        BossTrainingSetup s;
        s.bossId = bossId_;
        s.levelId = place_->levelId;
        s.bossObject = place_->objectId;
        // The checkpoint just before its arena, unless other bosses come first (then in front
        // of its arena).
        if (place_->bossesBetween == 0) {
            s.checkpointId = place_->checkpointId;
            s.spawn = place_->checkpointFeet;
        }
        s.hearts = hearts_;
        s.slots = slots_;
        s.powers = chosen_;
        s.phase = phase_;
        o.bossTraining = s;
        g_.app.switchScene(makeGameScene(g_, place_->levelId, std::move(o)), 0.4f);
    }

    GameServices& g_;
    std::string bossId_;
    const EnemyDefinition* def_ = nullptr;
    MenuBackground bg_;
    BossTrainingPrefs prefs_;
    std::shared_ptr<const CreatureAtlas> atlas_;
    bool placeRead_ = false;
    std::optional<CreaturePlace> place_;
    std::optional<LevelData> level_;
    std::vector<const PowerDefinition*> powers_;
    std::vector<std::string> clips_;
    int clip_ = 0;
    float clipT_ = 0.0f, t_ = 0.0f;
    int hearts_ = 10, slots_ = 2, phase_ = -1;
    std::vector<std::string> chosen_;
    std::string msg_;
    float msgTimer_ = 0.0f;
    bool autoGo_ = false;
};

// =================================================================== end of a training

class BossTrainingEndScene : public Scene {
public:
    BossTrainingEndScene(GameServices& g, GameScene& game, BossTrainingResult r) : g_(g), game_(game), r_(std::move(r)) {}
    bool isOverlay() const override { return true; }
    bool updatesSceneBelow() const override { return true; }

    void onEnter() override { g_.ui.setFocus("btend/again"); }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        const float W = ui.screenW(), H = ui.screenH();
        ui.r().fillRect({0, 0, W, H}, Color{4, 6, 14, 170});
        ui.pushId("btend");
        // Title, subtitle, three rows, the notice, the buttons: the panel is as high as that.
        float pw = std::min(ui.s(860), W - ui.s(80)), ph = std::min(ui.s(30 + 66 + 40 + 3 * 60 + 6 + 36 + 34 + 60 + 30), H - ui.s(80));
        Rect p{(W - pw) * 0.5f, (H - ph) * 0.5f, pw, ph};
        ui.panel(p, true, 18);
        float x = p.x + ui.s(36), w = p.w - ui.s(72), y = p.y + ui.s(30);
        bool single = r_.setup.phase >= 0;
        std::string title = !r_.won ? "DÉFAITE" : single ? "PHASE RÉUSSIE" : "BOSS VAINCU";
        float tf = ui.fitFont(title, FontFace::Title, 44, w, 0.6f);
        ui.label(title, {p.center().x, y + ui.s(26)}, tf, r_.won ? ui.theme().success : ui.theme().danger, Align::Center, FontFace::Title,
                 VAlign::Middle);
        y += ui.s(66);
        std::string sub = r_.bossName + "  ·  " +
                          (single ? "Phase " + std::to_string(r_.setup.phase + 1) + (r_.phaseName.empty() || r_.phase != r_.setup.phase ? std::string{} : " : " + r_.phaseName)
                                  : std::string("toutes les phases"));
        float sf = fit(ui, sub, FontFace::Bold, 20, w, 0.7f);
        ui.label(ui.elide(sub, FontFace::Bold, sf, w), {p.center().x, y}, sf, ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
        y += ui.s(40);
        // The training's result.
        auto row = [&](const std::string& k, const std::string& v, Color c) {
            Rect rr{x, y, w, ui.s(52)};
            ui.r().fillRoundedRect(rr, ui.s(8), Color{12, 18, 38, 220});
            float vf = fit(ui, v, FontFace::Bold, 22, w * 0.45f, 0.7f);
            float vw = ui.text().measure(v, FontFace::Bold, ui.s(vf)).x;
            float kroom = w - ui.s(36) - vw - ui.s(16);
            float kf = fit(ui, k, FontFace::Body, 18, kroom, 0.75f);
            ui.label(ui.elide(k, FontFace::Body, kf, kroom), {rr.x + ui.s(18), rr.center().y}, kf, ui.theme().textDim, Align::Left, FontFace::Body,
                     VAlign::Middle);
            ui.label(v, {rr.right() - ui.s(18), rr.center().y}, vf, c, Align::Right, FontFace::Bold, VAlign::Middle);
            y += ui.s(60);
        };
        row("Temps du combat", r_.time > 0.0f ? fightTime(r_.time) : std::string("—"), ui.theme().accent);
        row("Dégâts reçus", r_.damage == 0 ? std::string("aucun") : std::to_string(r_.damage) + (r_.damage > 1 ? " cœurs" : " cœur"),
            r_.damage == 0 ? ui.theme().success : ui.theme().warm);
        std::string reached = std::to_string(r_.phase + 1) + " / " + std::to_string(std::max(1, r_.phases));
        if (!r_.phaseName.empty()) reached += "  ·  " + r_.phaseName;
        row("Phase atteinte", reached, ui.theme().text);
        y += ui.s(6);
        drawNotice(ui, {x, y + ui.s(18)}, w);
        // Recommencer, Changer les réglages, Retour.
        float by = p.bottom() - ui.s(30) - ui.s(60), gap = ui.s(16);
        float bw = (w - 2 * gap) / 3.0f;
        UIContext::ButtonStyle primary;
        primary.primary = true;
        bool again = ui.button("again", {x, by, bw, ui.s(60)}, "Recommencer", primary);
        bool settings = ui.button("settings", {x + bw + gap, by, bw, ui.s(60)}, "Changer les réglages");
        bool back = ui.button("back", {x + 2 * (bw + gap), by, bw, ui.s(60)}, "Retour");
        ui.popId();
        InputManager& in = g_.app.input();
        if (again || in.keyPressed(SDL_SCANCODE_R) || in.rawPadButton(SDL_GAMEPAD_BUTTON_BACK)) {
            g_.app.popScene();
            game_.restart();
        } else if (settings) {
            g_.app.switchScene(makeBossTraining(g_, r_.setup.bossId), 0.3f);
        } else if (back || ui.backPressed()) {
            g_.app.switchScene(makeBossStats(g_, r_.setup.bossId), 0.3f);
        }
        ui.end();
    }

private:
    GameServices& g_;
    GameScene& game_;
    BossTrainingResult r_;
};

}  // namespace

std::unique_ptr<Scene> makeBossTraining(GameServices& g, const std::string& bossId) { return std::make_unique<BossTrainingScene>(g, bossId); }

std::unique_ptr<Scene> makeBossTrainingEnd(GameServices& g, GameScene& game, const BossTrainingResult& result) {
    return std::make_unique<BossTrainingEndScene>(g, game, result);
}

}  // namespace cosmic
