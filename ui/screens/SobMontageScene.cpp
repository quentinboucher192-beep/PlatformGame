// Section training › « Regarder » (2.60.3): the Sum of Best of a level version watched as one
// run — its best segments one after the other, full runs and section trainings together.
//
// Each segment is played from the exact replay of the run (or the training attempt) that did
// it, with the advanced viewer's engine (ReplayPlayback): the level is simulated again with
// that run's inputs, quietly up to the step it reached the segment's start, then shown up to
// the step it reached its end. A training attempt's replay starts as the attempt did (its
// checkpoint, its powers: applySectionTraining). What is shown is the real simulation of each
// run, never an estimate; the state digests are checked on the way. At each cut, a banner
// says which segment it is, where it was done (« course du 12/11 », « entraînement du 17/11 »)
// and its time; the chronometer adds the best times up to the Sum of Best. A segment whose best
// time has no replay on this computer (done before 2.60.3) is said so, never invented; its
// time is added all the same. Display only: nothing ranked, nothing sent, nothing recorded.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <map>
#include <memory>

#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/Hud.h"
#include "game/ghost/GhostRace.h"
#include "game/ghost/ReplayPlayback.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "ui/screens/LevelPowerKeysScene.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

/// Never below 11.5 pixels (small windows): below, the font loses its spaces.
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

constexpr std::array<float, 3> kSpeeds{0.5f, 1.0f, 2.0f};
const std::array<const char*, 3> kSpeedLabels{"Vitesse ×0,5", "Vitesse ×1", "Vitesse ×2"};
constexpr int kSeekBudget = 3000;    // steps simulated per frame while reaching a segment's start
constexpr float kBannerTime = 2.6f;  // the banner of a cut
constexpr float kCardTime = 4.0f;    // a segment that cannot be shown: its card
const Color kGold{255, 214, 90, 255};

/// "17/11" from "2026-11-17 10:30" (empty when it is not a date).
std::string dayMonth(const std::string& date) {
    if (date.size() < 10 || date[4] != '-' || date[7] != '-') return {};
    return date.substr(8, 2) + "/" + date.substr(5, 2);
}

/// What the montage reads before it starts: every replay it needs, checked (worker thread).
struct Loaded {
    std::map<std::string, std::shared_ptr<const ReplayData>> replays;  // by file
    std::map<std::string, std::string> errors;                         // by file (French)
};

class SobMontageScene : public Scene {
public:
    SobMontageScene(GameServices& g, SobMontageRequest req) : g_(g), req_(std::move(req)) {}

    void onEnter() override {
        g_.ui.setFocus("montage/play");
        const LevelEntry* entry = g_.content.level(req_.levelId);
        if (!entry) return fail("Ce niveau n'est pas installé dans votre jeu.");
        std::string err;
        auto level = g_.content.loadLevel(req_.levelId, &err);
        if (!level) return fail("Chargement du niveau impossible : " + err);
        level_ = *level;
        // The installed level must be the version of the segments (ranked or gameplay fingerprint).
        std::string ranked;
        if (auto text = FileUtil::readText(entry->path)) {
            Json j = Json::parse(*text, nullptr, false);
            if (!j.is_discarded())
                if (auto fp = fingerprintRankedLevel(g_.content, j)) ranked = fp->hash;
        }
        if (req_.levelHash.empty() || (req_.levelHash != ranked && req_.levelHash != fingerprintGameplay(g_.content, level_)))
            return fail("Ce niveau a changé depuis ces meilleurs segments : ils ne se rejouent que sur leur version.");
        if (!entry->assetRoot.empty()) {
            g_.app.resources().pushMount(entry->assetRoot);
            mounted_ = true;
        }
        // Every replay read and checked off the main thread (one file may hold several segments).
        std::vector<std::string> files;
        for (const MontageSegment& s : req_.plan.segments)
            if (s.hasReplay() && std::find(files.begin(), files.end(), s.file) == files.end()) files.push_back(s.file);
        const std::string hash = req_.levelHash;
        loading_ = std::async(std::launch::async, [files, hash] {
            Loaded out;
            for (const std::string& f : files) {
                std::string why;
                auto r = GhostStore::loadReplay(Paths::fromUtf8(f), &why);
                if (!r) out.errors[f] = "Replay illisible : " + why;
                else if (r->levelHash != hash) out.errors[f] = "Ce replay vient d'une autre version du niveau : il ne peut pas être rejoué ici.";
                else if (!r->exact()) out.errors[f] = "Ce replay n'est pas rejouable à l'identique (ancien format).";
                else out.replays[f] = std::make_shared<const ReplayData>(std::move(*r));
            }
            return out;
        });
        status_ = "Lecture des replays…";
    }

    void onExit() override {
        playback_.reset();
        if (mounted_) g_.app.resources().popMount();
        mounted_ = false;
        g_.app.input().clearPowerSlotKeys();
    }

    void update(float dt) override {
        if (loading_.valid() && loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            loaded_ = loading_.get();
            ready_ = true;
            goTo(0);
        }
        if (!ready_) return;
        const float speed = kSpeeds[static_cast<size_t>(speedIdx_)];
        float wdt = 0.0f;
        switch (phase_) {
            case Phase::Seeking:
                if (playback_->advance(kSeekBudget)) arrivedAtStart();
                break;
            case Phase::Playing:
                if (playing_) {
                    wdt = dt * speed;
                    acc_ += wdt;
                    int guard = 0;
                    while (phase_ == Phase::Playing && acc_ >= kSimDt && guard++ < 64) {
                        acc_ -= kSimDt;
                        stepOne();
                    }
                }
                break;
            case Phase::Card:
                if (playing_ && (cardT_ -= dt) <= 0.0f) next();
                break;
            case Phase::Done:
                break;
        }
        if (bannerT_ > 0.0f && playing_) bannerT_ -= dt;
        if (playback_) {
            if (playback_->generation() != gen_) {
                gen_ = playback_->generation();
                hud_ = Hud{};
            }
            if (phase_ != Phase::Seeking) {
                playback_->world().update(phase_ == Phase::Playing ? wdt : 0.0f);
                hud_.update(playback_->world(), phase_ == Phase::Playing ? wdt : 0.0f);
            }
            // The HUD shows each power with its key in this level for this player (2.31).
            if (!powerKeysReady_) powerKeys_ = playerPowerKeys(g_, level_);
            powerKeysReady_ = true;
            applyPowerSlotKeys(g_.app.input(), playback_->world().player(), powerKeys_);
        }
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        Renderer& r = g_.app.renderer();
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        r.fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{6, 9, 20, 255});
        if (!ready_) {
            drawStatus();
            ui.end();
            return;
        }
        layout();
        if (playback_) {
            RenderOptions opt;
            opt.lighting = g_.settings.graphics.dynamicLighting;
            float alpha = phase_ == Phase::Playing && playing_ ? std::clamp(acc_ / kSimDt, 0.0f, 1.0f) : 1.0f;
            WorldView view = playback_->world().camera().view(r.width(), r.height(), alpha);
            if (!rendererBuilt_) {
                renderer_.build(r, g_.app.resources(), playback_->world());
                rendererBuilt_ = true;
            }
            renderer_.render(r, g_.app.resources(), playback_->world(), view, alpha, opt);
            r.setView(WorldView{});
            if (phase_ == Phase::Seeking) r.fillRect({0, 0, ui.screenW(), stripRect_.y}, Color{4, 6, 14, 200});
            else drawGameHud(view);
        }
        drawChrono();
        if (phase_ == Phase::Card) drawCard();
        else if (phase_ == Phase::Done) drawEnd();
        else if (phase_ == Phase::Seeking) drawSeeking();
        if (bannerT_ > 0.0f && (phase_ == Phase::Playing || phase_ == Phase::Seeking)) drawBanner();
        drawStrip();
        handleShortcuts();
        ui.end();
    }

private:
    enum class Phase { Seeking, Playing, Card, Done };

    const std::vector<MontageSegment>& segs() const { return req_.plan.segments; }
    int count() const { return static_cast<int>(segs().size()); }

    void fail(const std::string& m) {
        status_ = m;
        failed_ = true;
    }

    // ------------------------------------------------------------- the montage

    /// The durations of the segments before `k`, added (the chronometer at its start).
    float sumBefore(int k) const {
        double s = 0.0;
        for (int i = 0; i < k && i < count(); ++i)
            if (segs()[static_cast<size_t>(i)].duration > 0.0f) s += segs()[static_cast<size_t>(i)].duration;
        return static_cast<float>(s);
    }

    void goTo(int k) {
        k = std::clamp(k, 0, count());
        doneSum_ = sumBefore(k);
        startSegment(k);
    }

    void next() { goTo(cur_ + 1); }

    /// Where segment `s` starts on its replay's chronometer: a run's start is the first step
    /// its timer runs (not the level's intro).
    static double seekTarget(const MontageSegment& s) { return s.start < 0.002f ? 0.004 : static_cast<double>(s.start); }

    void startSegment(int k) {
        cur_ = k;
        acc_ = 0.0f;
        bannerT_ = kBannerTime;
        if (k >= count()) {
            phase_ = Phase::Done;
            playing_ = false;
            g_.ui.setFocus("montage/play");
            return;
        }
        const MontageSegment& s = segs()[static_cast<size_t>(k)];
        std::shared_ptr<const ReplayData> rd;
        if (!s.hasReplay()) return card(noReplayText(s));
        if (auto it = loaded_.errors.find(s.file); it != loaded_.errors.end()) return card(it->second);
        if (auto it = loaded_.replays.find(s.file); it != loaded_.replays.end()) rd = it->second;
        if (!rd) return card("Replay introuvable sur ce PC.");
        const double target = seekTarget(s);
        // The same run going on (its next segment): the playback continues where it is.
        const bool reuse = playback_ && playFile_ == s.file && playTraining_ == s.training && !playback_->desynced() &&
                           static_cast<double>(playback_->runTime()) <= target + 0.0005;
        if (!reuse) {
            playback_.reset();  // it refers to its replay: gone first
            replay_ = rd;
            ReplayPlayback::Setup setup;
            if (s.training) setup = [start = s.trainingStart](GameWorld& w) { applySectionTraining(w, start); };
            ResourceManager& res = g_.app.resources();
            WorldServices ws{res, g_.app.audio(), g_.content, nullptr, g_.settings.graphics.screenShake, 0.7f};
            playback_ = std::make_unique<ReplayPlayback>(ws, level_, *replay_, std::move(setup));
            playFile_ = s.file;
            playTraining_ = s.training;
            gen_ = playback_->generation();
            hud_ = Hud{};
        }
        playback_->seekTime(target);
        phase_ = Phase::Seeking;
        if (playback_->advance(kSeekBudget)) arrivedAtStart();
    }

    void card(const std::string& why) {
        phase_ = Phase::Card;
        cardT_ = kCardTime;
        cardWhy_ = why;
    }

    void arrivedAtStart() {
        const MontageSegment& s = segs()[static_cast<size_t>(cur_)];
        if (playback_->desynced()) return card(desyncText());
        if (static_cast<double>(playback_->runTime()) + 0.0005 < seekTarget(s))
            return card("Ce replay s'arrête avant le début du segment : il ne peut pas le montrer.");
        playback_->world().update(0.0f);
        phase_ = Phase::Playing;
        acc_ = 0.0f;
    }

    void stepOne() {
        const MontageSegment& s = segs()[static_cast<size_t>(cur_)];
        bool ok = playback_->step();
        if (playback_->desynced()) return card(desyncText());
        // The segment ends at the step its run reached its end (its split, its finish).
        if (static_cast<double>(playback_->runTime()) + 0.0005 >= static_cast<double>(s.end) || !ok) next();
    }

    static std::string desyncText() {
        return "Ce replay ne se rejoue plus à l'identique (moteur d'une autre version du jeu) : ce segment est passé, son temps compté.";
    }

    static std::string noReplayText(const MontageSegment& s) {
        if (s.duration < 0.0f) return "Temps inconnu : aucune course complète ni aucun entraînement n'a fait ce segment seul.";
        return "Pas de replay pour ce meilleur temps (d'avant la version 2.60.3, ou repris d'un autre ordinateur) : battez-le, en course ou à "
               "l'entraînement, pour l'ajouter.";
    }

    /// « Segment 2 · entraînement du 17/11 · 0:23,45 ».
    std::string segmentLine(int k) const {
        const MontageSegment& s = segs()[static_cast<size_t>(k)];
        // 2.91: where its best was done (Partie, Entraînement, Enchaînement, StartPos, En ligne).
        std::string where = i18n::tr(bestSourceLabel(s.source));
        if (std::string d = dayMonth(s.date); !d.empty()) where += " · " + d;
        return "Segment " + std::to_string(k + 1) + " · " + where + " · " + (s.duration > 0.0f ? formatGhostTime(s.duration) : std::string("temps inconnu"));
    }
    std::string segmentName(int k) const {
        return k >= 0 && k < static_cast<int>(req_.names.size()) ? req_.names[static_cast<size_t>(k)] : "Segment " + std::to_string(k + 1);
    }

    /// The chronometer: the best times of the segments before, and the one shown so far.
    float chrono() const {
        float t = doneSum_;
        if (phase_ == Phase::Playing && cur_ < count()) {
            const MontageSegment& s = segs()[static_cast<size_t>(cur_)];
            t += std::clamp(playback_->runTime() - s.start, 0.0f, std::max(0.0f, s.duration));
        }
        return t;
    }

    void togglePlay() {
        if (phase_ == Phase::Done) {
            goTo(0);
            playing_ = true;
            return;
        }
        playing_ = !playing_;
    }

    // ------------------------------------------------------------- drawing

    float px(float size) const { return g_.ui.s(fz(g_.ui, size)); }

    void layout() {
        UIContext& ui = g_.ui;
        const float stripH = ui.s(14) + px(20) * 1.5f + ui.s(12) + ui.s(10) + ui.s(14) + ui.s(50) + ui.s(14);
        stripRect_ = {0, ui.screenH() - stripH, ui.screenW(), stripH};
        hudRects_.clear();
    }

    void drawStatus() {
        UIContext& ui = g_.ui;
        float w = std::min(ui.screenW() - ui.s(80), ui.s(1000));
        float f = fz(ui, 22);
        Vec2 sz = ui.text().measureWrapped(status_, FontFace::Body, ui.s(f), w);
        ui.labelWrapped(status_, {(ui.screenW() - std::min(w, sz.x)) * 0.5f, ui.screenH() * 0.42f - sz.y * 0.5f}, w, f,
                        failed_ ? ui.theme().danger : ui.theme().text);
        if (ui.button("montage/back", {ui.screenW() * 0.5f - ui.s(120), ui.screenH() * 0.42f + sz.y * 0.5f + ui.s(30), ui.s(240), ui.s(48)}, "Retour") ||
            ui.backPressed())
            g_.app.popScene();
    }

    /// The game's HUD (vitals and powers at their usual places), clear of the chronometer.
    void drawGameHud(const WorldView& view) {
        GameWorld& w = playback_->world();
        if (w.cinematic().active() || w.state() != WorldState::Playing) return;
        UIContext& ui = g_.ui;
        GameplaySettings hs = g_.settings.gameplay;
        hs.focusMode = false;
        hs.showTimer = false;
        hs.progressBar = 0;
        hs.showMessages = false;
        hs.focusMessages = false;
        hs.showInputHints = false;
        for (HudElement e : {HudElement::Splits, HudElement::Enemies, HudElement::Ambush, HudElement::Collectibles, HudElement::Boss}) hs.hud[e].visible = false;
        for (HudElement e : {HudElement::Vitals, HudElement::Powers}) hs.hud[e].custom = false;
        for (HudElement e : {HudElement::Vitals, HudElement::Powers}) {
            if (!hudVisible(hs, e)) continue;
            Rect a = hud_.area(ui, w, e, hud_.place(ui, w, hs, e));
            if (a.w <= 0.0f) continue;
            // Never under the chronometer nor the controls: that block is left out then.
            if (a.intersects(chronoRect_) || a.bottom() > stripRect_.y) {
                setHudVisible(hs, e, false);
                continue;
            }
            hudRects_.push_back(a.expanded(ui.s(6)));
        }
        hud_.render(ui, w, view, nullptr, hs);
    }

    /// Top right: the Sum of Best's chronometer, the record and what is left to gain.
    void drawChrono() {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        const float m = ui.s(16), pad = ui.s(16);
        const float maxW = std::min(ui.s(420), ui.screenW() * 0.42f);
        const float tf = fz(ui, 14), cf = 34.0f, lf = fz(ui, 15);
        const std::string title = "SOMME DES MEILLEURS";
        const std::string clock = formatGhostTime(chrono());
        std::string line1 = req_.plan.total > 0.0f ? "sur " + formatGhostTime(req_.plan.total) : "somme incomplète";
        if (req_.plan.pbTime > 0.0f) line1 += " · record " + formatGhostTime(req_.plan.pbTime);
        std::string line2;
        if (float gain = req_.plan.possibleGain(); gain >= 0.0f) line2 = formatGhostDelta(-gain) + " possible";
        float w = std::max({ui.text().measure(title, FontFace::Bold, ui.s(tf)).x, ui.text().measure(clock, FontFace::Title, ui.s(cf)).x,
                            ui.text().measure(line1, FontFace::Body, ui.s(lf)).x, ui.text().measure(line2, FontFace::Body, ui.s(lf)).x}) +
                  2 * pad;
        w = std::min(w, maxW);
        const float inner = w - 2 * pad;
        const float cfit = ui.fitFont(clock, FontFace::Title, cf, inner, 0.6f);
        const float h = pad + ui.s(tf) * 1.4f + ui.s(cfit) * 1.25f + ui.s(lf) * 1.45f + (line2.empty() ? 0.0f : ui.s(lf) * 1.45f) + pad * 0.6f;
        chronoRect_ = {ui.screenW() - m - w, m, w, h};
        ui.r().fillRoundedRect(chronoRect_, ui.s(12), Color{6, 10, 22, 225});
        ui.r().drawRoundedRect(chronoRect_, ui.s(12), kGold.withAlpha(150), std::max(1.0f, ui.s(1.5f)));
        float x = chronoRect_.x + pad, y = chronoRect_.y + pad;
        ui.label(ui.elide(title, FontFace::Bold, tf, inner), {x, y}, tf, kGold, Align::Left, FontFace::Bold);
        y += ui.s(tf) * 1.4f;
        ui.label(clock, {x, y}, cfit, th.text, Align::Left, FontFace::Title);
        y += ui.s(cfit) * 1.25f;
        ui.label(ui.elide(line1, FontFace::Body, lf, inner), {x, y}, lf, th.textDim);
        y += ui.s(lf) * 1.45f;
        if (!line2.empty()) ui.label(ui.elide(line2, FontFace::Body, lf, inner), {x, y}, lf, th.success);
    }

    /// The top of the free part of the screen: under the HUD blocks and the chronometer.
    float freeTop() const {
        float top = chronoRect_.bottom();
        for (const Rect& r : hudRects_) top = std::max(top, r.bottom());
        return top + g_.ui.s(16);
    }

    /// A box of lines centred in the part of the screen left free, at `y`.
    struct Line {
        std::string text;
        FontFace face;
        float size;
        Color color;
    };
    Rect drawBox(const std::vector<Line>& lines, float y, float alpha, Color border, float wrapW = 0.0f, const std::string& wrapped = {},
                 float wrapSize = 0.0f, Color wrapColor = {}) {
        UIContext& ui = g_.ui;
        const float pad = ui.s(18);
        const float maxW = std::min(ui.screenW() - 2 * ui.s(24), ui.s(860));
        float w = 0.0f, h = 0.0f;
        for (const Line& l : lines) {
            w = std::max(w, ui.text().measure(l.text, l.face, ui.s(l.size)).x);
            h += ui.s(l.size) * 1.45f;
        }
        Vec2 ws;
        if (!wrapped.empty()) {
            ws = ui.text().measureWrapped(wrapped, FontFace::Body, ui.s(wrapSize), std::min(wrapW, maxW - 2 * pad));
            w = std::max(w, ws.x);
            h += ui.s(8) + ws.y;
        }
        w = std::min(w + 2 * pad, maxW);
        const float inner = w - 2 * pad;
        Rect box{(ui.screenW() - w) * 0.5f, y, w, h + 2 * pad};
        // Never under the controls: moved up when the window is short.
        if (box.bottom() > stripRect_.y - ui.s(8)) box.y = std::max(ui.s(8), stripRect_.y - ui.s(8) - box.h);
        auto a = [&](Color c) { return c.withAlpha(static_cast<uint8_t>(static_cast<float>(c.a) * alpha)); };
        ui.r().fillRoundedRect(box, ui.s(12), a(Color{6, 10, 22, 230}));
        ui.r().drawRoundedRect(box, ui.s(12), a(border), std::max(1.0f, ui.s(1.5f)));
        float ly = box.y + pad;
        for (const Line& l : lines) {
            float f = ui.fitFont(l.text, l.face, l.size, inner, 0.75f);
            ui.label(ui.elide(l.text, l.face, f, inner), {box.center().x, ly + ui.s(l.size) * 0.72f}, f, a(l.color), Align::Center, l.face, VAlign::Middle);
            ly += ui.s(l.size) * 1.45f;
        }
        // Drawn at its measured width (the same lines), the block centred.
        if (!wrapped.empty()) ui.labelWrapped(wrapped, {box.x + pad + (inner - ws.x) * 0.5f, ly + ui.s(8)}, ws.x + 1.0f, wrapSize, a(wrapColor));
        return box;
    }

    void drawBanner() {
        UIContext& ui = g_.ui;
        if (cur_ >= count()) return;
        const float alpha = std::clamp(bannerT_ / 0.5f, 0.0f, 1.0f);
        const MontageSegment& s = segs()[static_cast<size_t>(cur_)];
        drawBox({{segmentLine(cur_), FontFace::Bold, fz(ui, 22), s.training ? ui.theme().accent : kGold},
                 {segmentName(cur_), FontFace::Body, fz(ui, 16), ui.theme().text}},
                freeTop(), alpha, s.training ? ui.theme().accent : kGold);
    }

    void drawSeeking() {
        UIContext& ui = g_.ui;
        if (bannerT_ > 0.0f) return;  // the banner says it already
        std::string t = "Préparation du segment… " + std::to_string(static_cast<int>(playback_->seekProgress() * 100.0f)) + " %";
        drawBox({{t, FontFace::Bold, fz(ui, 18), ui.theme().text}}, freeTop(), 1.0f, ui.theme().accent);
    }

    void drawCard() {
        UIContext& ui = g_.ui;
        const MontageSegment& s = segs()[static_cast<size_t>(cur_)];
        std::string add = s.duration > 0.0f ? "Son temps est ajouté au chronomètre." : "";
        std::string text = cardWhy_ + (add.empty() ? "" : " " + add);
        drawBox({{segmentLine(cur_), FontFace::Bold, fz(ui, 22), ui.theme().warm}, {segmentName(cur_), FontFace::Body, fz(ui, 16), ui.theme().text}},
                std::max(freeTop(), ui.screenH() * 0.3f), 1.0f, ui.theme().warm, ui.s(640), text, fz(ui, 16), ui.theme().textDim);
    }

    void drawEnd() {
        UIContext& ui = g_.ui;
        const MontagePlan& p = req_.plan;
        std::string sum = p.total > 0.0f ? "Somme des meilleurs : " + formatGhostTime(p.total) : "Somme des meilleurs incomplète : " + formatGhostTime(doneSum_);
        std::vector<Line> lines{{"Fin des meilleurs segments", FontFace::Bold, fz(ui, 24), kGold}, {sum, FontFace::Bold, fz(ui, 20), ui.theme().text}};
        if (p.pbTime > 0.0f) {
            std::string rec = "Record : " + formatGhostTime(p.pbTime);
            if (float gain = p.possibleGain(); gain >= 0.0f) rec += " · " + formatGhostDelta(-gain) + " possible";
            lines.push_back({rec, FontFace::Body, fz(ui, 17), ui.theme().textDim});
        }
        int training = 0;
        for (const MontageSegment& s : segs()) training += s.source != BestSource::Run && s.source != BestSource::Online ? 1 : 0;
        std::string shown = std::to_string(p.withReplay) + " segment" + (p.withReplay > 1 ? "s" : "") + " sur " + std::to_string(count()) + " en replay";
        if (training > 0) shown += " · " + std::to_string(training) + " d'entraînement";
        lines.push_back({shown, FontFace::Body, fz(ui, 15), ui.theme().textDim});
        drawBox(lines, std::max(freeTop(), ui.screenH() * 0.3f), 1.0f, kGold);
    }

    /// Bottom: what is shown, where in the montage, and the controls.
    void drawStrip() {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        Renderer& r = ui.r();
        const Rect& s = stripRect_;
        r.fillGradientV({s.x, s.y - ui.s(24), s.w, ui.s(24)}, Color{4, 8, 18, 0}, Color{4, 8, 18, 235});
        r.fillRect(s, Color{4, 8, 18, 235});
        const float x0 = s.x + ui.s(24), x1 = s.right() - ui.s(24), w = x1 - x0;
        float y = s.y + ui.s(14);
        // Title, and whether the replay plays exactly.
        std::string mode;
        Color mc = th.textDim;
        if (playback_ && (phase_ == Phase::Playing || phase_ == Phase::Seeking)) {
            if (playback_->desynced()) {
                mode = "Désynchronisé";
                mc = th.danger;
            } else {
                mode = "Rejoué à l'identique · " + std::to_string(playback_->checksPassed()) + " contrôles vérifiés";
                mc = th.success;
            }
        }
        const float tf = fz(ui, 20), mf0 = fz(ui, 15);
        const float modeRoom = w * 0.5f;
        const float mf = ui.fitFont(mode, FontFace::Bold, mf0, modeRoom, 0.8f);
        std::string modeShown = ui.elide(mode, FontFace::Bold, mf, modeRoom);
        const float modeW = modeShown.empty() ? 0.0f : ui.text().measure(modeShown, FontFace::Bold, ui.s(mf)).x + ui.s(24);
        const float cy = y + px(20) * 0.75f;
        const std::string title = "MEILLEURS SEGMENTS - " + (req_.levelName.empty() ? req_.levelId : req_.levelName);  // the title font has no « · »
        ui.label(ui.elide(title, FontFace::Title, tf, w - modeW), {x0, cy}, tf, kGold, Align::Left, FontFace::Title, VAlign::Middle);
        if (!modeShown.empty()) ui.label(modeShown, {x1, cy}, mf, mc, Align::Right, FontFace::Bold, VAlign::Middle);
        y += px(20) * 1.5f + ui.s(12);
        // The segments, as long as their best times; the current one lit, those without a replay dim.
        {
            Rect bar{x0, y, w, ui.s(10)};
            const float total = std::max(0.001f, sumBefore(count()));
            float bx = bar.x;
            const float gap = std::max(1.0f, ui.s(2));
            for (int i = 0; i < count(); ++i) {
                const MontageSegment& sg = segs()[static_cast<size_t>(i)];
                float sw = sg.duration > 0.0f ? bar.w * sg.duration / total : 0.0f;
                if (sw <= 0.0f) continue;
                Color c = !sg.hasReplay() ? Color{60, 66, 84, 255} : i < cur_ ? kGold.withAlpha(200) : i == cur_ ? kGold : Color{40, 50, 80, 255};
                Rect piece{bx, bar.y, std::max(1.0f, sw - gap), bar.h};
                r.fillRoundedRect(piece, ui.s(3), c);
                if (i == cur_ && phase_ == Phase::Playing && sg.duration > 0.0f) {
                    float k = std::clamp((playback_->runTime() - sg.start) / sg.duration, 0.0f, 1.0f);
                    r.fillRect({piece.x + piece.w * k - ui.s(1), bar.y - ui.s(3), std::max(1.0f, ui.s(2)), bar.h + ui.s(6)}, Color::white());
                }
                bx += sw;
            }
            y += ui.s(10) + ui.s(14);
        }
        // Controls: the segment before, play / pause, the next one, the speed; « Retour » on the right.
        const float bh = ui.s(50), gap = ui.s(12);
        UIContext::ButtonStyle st;
        st.small = true;
        const float backW = std::min(ui.s(180), w * 0.2f);
        const float room = w - backW - gap;
        const float bw = std::min(ui.s(230), (room - 3 * gap) / 4.0f);
        float bx = x0;
        UIContext::ButtonStyle prevSt = st;
        prevSt.enabled = cur_ > 0;
        if (ui.button("montage/prev", {bx, y, bw, bh}, "Segment précédent", prevSt) && prevSt.enabled) goTo(cur_ - 1);
        bx += bw + gap;
        UIContext::ButtonStyle playSt = st;
        playSt.primary = true;
        const std::string playText = phase_ == Phase::Done ? "Revoir" : playing_ ? "Pause" : "Lecture";
        if (ui.button("montage/play", {bx, y, bw, bh}, playText, playSt)) togglePlay();
        bx += bw + gap;
        UIContext::ButtonStyle nextSt = st;
        nextSt.enabled = cur_ < count();
        if (ui.button("montage/next", {bx, y, bw, bh}, "Segment suivant", nextSt) && nextSt.enabled) next();
        bx += bw + gap;
        if (ui.button("montage/speed", {bx, y, bw, bh}, kSpeedLabels[static_cast<size_t>(speedIdx_)], st))
            speedIdx_ = (speedIdx_ + 1) % static_cast<int>(kSpeeds.size());
        if (ui.button("montage/back", {x1 - backW, y, backW, bh}, "Retour", st)) closeRequested_ = true;
    }

    void handleShortcuts() {
        UIContext& ui = g_.ui;
        InputManager& in = g_.app.input();
        if (closeRequested_ || ui.backPressed()) {
            g_.app.popScene();
            return;
        }
        if (!ui.acceptsInput() || ui.editingText()) return;
        // 2.93: the replay viewer's keys (Paramètres › Contrôles › « Replays »): play / pause (K, X),
        // back / forward (J / L: the segment before, the next one).
        auto key = [&](Action a) { return in.keyPressedFor(a, true); };
        if (key(Action::ReplayPlay) || in.padPressed(Action::ReplayPlay)) togglePlay();
        if (key(Action::ReplayBack) && cur_ > 0) goTo(cur_ - 1);
        if (key(Action::ReplayForward) && cur_ < count()) next();
        if (in.menuPressed(Action::MenuTabPrev) && cur_ > 0) goTo(cur_ - 1);
        if (in.menuPressed(Action::MenuTabNext) && cur_ < count()) next();
    }

    GameServices& g_;
    SobMontageRequest req_;
    LevelData level_;
    bool mounted_ = false;
    std::string status_;
    bool failed_ = false;
    std::future<Loaded> loading_;
    Loaded loaded_;
    bool ready_ = false;
    // The replay shown now.
    std::shared_ptr<const ReplayData> replay_;
    std::unique_ptr<ReplayPlayback> playback_;
    std::string playFile_;
    bool playTraining_ = false;
    uint32_t gen_ = 0;
    GameRenderer renderer_;
    bool rendererBuilt_ = false;
    Hud hud_;
    PowerKeyMap powerKeys_;
    bool powerKeysReady_ = false;
    // Where the montage is.
    Phase phase_ = Phase::Card;
    int cur_ = 0;
    float doneSum_ = 0.0f;
    float cardT_ = 0.0f;
    std::string cardWhy_;
    float bannerT_ = 0.0f;
    bool playing_ = true;
    int speedIdx_ = 1;
    float acc_ = 0.0f;
    bool closeRequested_ = false;
    // Layout of the last frame.
    Rect stripRect_, chronoRect_;
    std::vector<Rect> hudRects_;
};

}  // namespace

std::unique_ptr<Scene> makeSobMontage(GameServices& g, SobMontageRequest req) { return std::make_unique<SobMontageScene>(g, std::move(req)); }

}  // namespace cosmic
