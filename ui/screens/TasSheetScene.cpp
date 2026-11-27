// « Fiche du TAS »: the TAS of a level in one category (tabs: the level's other TAS), said honestly
// (« meilleur temps trouvé par la machine », not a proven optimum), what it is (category, versions of the
// level and of the simulation, when it was computed, the tries explored), checked here by the game's own
// engine (its replay played again: identical, its checks, its deaths, damage, shards and powers read
// from that playback), its route over the whole level (mine dashed beside it), its splits against my
// best segments, and the ways to it: watch it, race it, compare it with my record. Pushed over the
// screen it was opened from (the ghosts' setup, the rankings, the statistics, the replay viewer).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "engine/input/InputManager.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/gameplay/GameWorld.h"
#include "game/ghost/GhostRace.h"
#include "game/ghost/GhostStore.h"
#include "game/ghost/PersonalBests.h"
#include "game/ghost/ReplayCompare.h"
#include "game/ghost/ReplayPlayback.h"
#include "game/online/LevelFingerprint.h"
#include "game/powers/PowerDefinition.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/ReplayCompareNav.h"
#include "ui/screens/StatisticsArt.h"
#include "ui/screens/TasUi.h"
#include "ui/widgets/Glyphs.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

namespace {

using tasui::kBehind;
using tasui::kTas;

/// Never below 11.5 pixels on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }
float lineH(UIContext& ui, float size) { return ui.s(fz(ui, size)) * 1.4f; }
constexpr Color kGold{255, 214, 90, 255};
constexpr Color kMine{110, 214, 255, 255};  // my record's route (the colour of my ghosts)

/// What the game's engine says of the TAS's replay, played here again from its inputs.
struct EngineCheck {
    enum class State { Reading, Running, Identical, Desynced, Unreadable, Unfinished } state = State::Reading;
    std::string why;
    size_t checks = 0;
    uint32_t desyncTick = 0, tick = 0, ticks = 0;
    int64_t finalMs = 0;
    int deaths = 0, damage = 0, shards = 0, totalShards = 0;
    std::vector<std::string> powers;  // used, by id
};

class TasSheetScene : public Scene {
public:
    TasSheetScene(GameServices& g, tasui::SheetRequest req) : g_(g), req_(std::move(req)), art_(g) {}
    bool isOverlay() const override { return true; }

    void onEnter() override {
        if (const LevelEntry* e = g_.content.level(req_.levelId)) {
            levelName_ = e->name;
            assetRoot_ = Paths::toUtf8(e->assetRoot);
        }
        if (levelName_.empty()) levelName_ = req_.levelId;
        if (auto l = g_.content.loadLevel(req_.levelId)) {
            level_ = std::move(*l);
            haveLevel_ = true;
        }
        if (const tas::TasRun* t = tas::catalog().tasFor(g_, req_.levelId, req_.category)) version_ = t->levelVersion;
        for (RunCategory c : kAllCategories)
            if (tas::catalog().tasFor(g_, req_.levelId, c)) cats_.push_back(c);
        if (std::find(cats_.begin(), cats_.end(), req_.category) == cats_.end() && !cats_.empty()) req_.category = cats_.front();
        readMine();
        startCategory();
        g_.ui.setFocus("tassheet/watch");
    }

    void onExit() override { stopCheck(); }

    void update(float) override {
        art_.update();
        collectReads();
        runCheck();
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{4, 6, 14, 252});
        const float m = ui.s(48);
        const Rect panel{m, ui.s(36), ui.screenW() - 2 * m, ui.screenH() - ui.s(72)};
        ui.panel(panel, true, 18);
        tasui::Pieces& P = tasui::pieces("sheet");
        P.clear(panel);
        ui.pushId("tassheet");
        const tas::TasRun* t = run();
        bool back = false;
        if (!t) {
            // Hidden meanwhile (the level changed under it): said, and only « Retour ».
            const std::string msg = "Ce TAS n'est plus valable pour la version installée du niveau : il reviendra une fois recalculé.";
            ui.labelWrapped(msg, {panel.x + ui.s(30), panel.y + ui.s(40)}, panel.w - ui.s(60), fz(ui, 18), ui.theme().textDim);
            back = ui.button("back", {panel.right() - ui.s(30) - ui.s(180), panel.bottom() - ui.s(84), ui.s(180), ui.s(54)}, "Retour");
        } else {
            const float pad = ui.s(30);
            const float x0 = panel.x + pad, inner = panel.w - 2 * pad;
            const float top = drawHeader(*t, x0, panel.y + ui.s(26), inner);
            const float by = panel.bottom() - ui.s(70);
            const float bottom = by - ui.s(16);
            const float lw = std::max(ui.s(300), inner * 0.317f);
            drawLeft(*t, {x0, top, lw, bottom - top});
            const float rx = x0 + lw + ui.s(24);
            drawRight(*t, {rx, top, x0 + inner - rx, bottom - top});
            back = drawBottom(*t, {x0, by, inner, ui.s(54)});
        }
        ui.popId();
        if (back || ui.backPressed()) g_.app.popScene();
        ui.end();
    }

private:
    // ------------------------------------------------------------------ data

    const tas::TasRun* run() { return tas::catalog().tasFor(g_, req_.levelId, req_.category); }
    std::string levelKey(const tas::TasRun& t) const { return GhostStore::levelKey(t.levelId, t.levelHash); }

    /// My record of the version the TAS is of: its replay (the dashed route, « Comparer avec mon record »).
    void readMine() {
        const tas::TasRun* any = nullptr;
        for (RunCategory c : cats_)
            if ((any = tas::catalog().tasFor(g_, req_.levelId, c))) break;
        if (!any) return;
        for (GhostEntry& e : g_.ghosts.localGhosts(req_.levelId))
            if (e.meta.source == "pb" && e.meta.levelHash == any->levelHash) {
                mine_ = e;
                break;
            }
        if (!mine_) return;
        const fs::path f = mine_->file;
        mineJob_ = std::async(std::launch::async, [f]() -> std::shared_ptr<const ReplayData> {
            auto r = GhostStore::loadReplay(f);
            return r ? std::make_shared<const ReplayData>(std::move(*r)) : nullptr;
        });
    }

    /// The category shown: its replay read off the main thread, then played by the engine here.
    void startCategory() {
        stopCheck();
        tasReplay_.reset();
        tasTrack_.reset();
        const tas::TasRun* t = run();
        if (!t) return;
        tasJob_ = std::async(std::launch::async, [rec = static_cast<const TasRecord&>(*t), root = tas::catalog().root()]() -> std::shared_ptr<const ReplayData> {
            auto r = loadTasReplay(rec, root);
            return r ? std::make_shared<const ReplayData>(std::move(*r)) : nullptr;
        });
        if (!checks_.count(req_.category)) checks_[req_.category] = EngineCheck{};
        g_.ui.setScroll("tassheet/rows", 0.0f);
    }

    void collectReads() {
        auto ready = [](auto& f) { return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; };
        if (ready(mineJob_)) {
            mineReplay_ = mineJob_.get();
            if (mineReplay_) {
                GhostTrack tr;
                tr.load(*mineReplay_);
                mineRoute_ = sampleRoute(tr);
            }
        }
        if (ready(tasJob_)) {
            tasReplay_ = tasJob_.get();
            EngineCheck& c = checks_[req_.category];
            const tas::TasRun* t = run();
            if (!tasReplay_ || !t || tasReplay_->levelHash != t->levelHash) {
                tasReplay_.reset();
                if (c.state == EngineCheck::State::Reading) {
                    c.state = EngineCheck::State::Unreadable;
                    c.why = "replay illisible";
                }
                return;
            }
            auto tr = std::make_shared<GhostTrack>();
            tr->load(*tasReplay_);
            tasTrack_ = tr;
            if (c.state == EngineCheck::State::Reading) startCheck();
        }
    }

    /// A run's path over the level: its feet every 1/10 s (cut at the jumps: deaths, respawns).
    static std::vector<std::vector<Vec2>> sampleRoute(const GhostTrack& t) {
        std::vector<std::vector<Vec2>> out;
        std::vector<Vec2> line;
        Vec2 prev;
        for (float s = 0.0f;; s += 0.1f) {
            const float at = std::min(s, t.duration());
            const GhostTrack::Pose p = t.poseAt(at);
            const bool cut = !p.visible || (!line.empty() && (p.feet - prev).lengthSq() > 160.0f * 160.0f);
            if (cut && line.size() > 1) out.push_back(std::move(line));
            if (cut) line.clear();
            if (p.visible) line.push_back(p.feet + Vec2{0.0f, -14.0f});
            prev = p.feet;
            if (at >= t.duration()) break;
        }
        if (line.size() > 1) out.push_back(std::move(line));
        return out;
    }

    // ------------------------------------------------------------------ the engine's check

    void startCheck() {
        if (!tasReplay_ || !haveLevel_) return;
        EngineCheck& c = checks_[req_.category];
        if (!tasReplay_->exact()) {
            c.state = EngineCheck::State::Unreadable;
            c.why = "replay sans entrées";
            return;
        }
        ResourceManager& res = g_.app.resources();
        if (!assetRoot_.empty()) res.pushMount(Paths::fromUtf8(assetRoot_));
        WorldServices ws{res, g_.app.audio(), g_.content, nullptr, 0.0f, 0.0f};
        checkRun_ = std::make_unique<ReplayPlayback>(ws, level_, *tasReplay_);
        if (!assetRoot_.empty()) res.popMount();
        checkReplay_ = tasReplay_;
        checkCat_ = req_.category;
        c.state = EngineCheck::State::Running;
        c.ticks = checkRun_->totalTicks();
        used_.clear();
        cooldowns_.clear();
    }

    void stopCheck() {
        if (checkRun_) {
            EngineCheck& c = checks_[checkCat_];
            if (c.state == EngineCheck::State::Running) c = EngineCheck{};  // started again when shown again
        }
        checkRun_.reset();
        checkReplay_.reset();
    }

    /// A few milliseconds of simulation per frame: the screen stays responsive.
    void runCheck() {
        if (!checkRun_) return;
        EngineCheck& c = checks_[checkCat_];
        ResourceManager& res = g_.app.resources();
        if (!assetRoot_.empty()) res.pushMount(Paths::fromUtf8(assetRoot_));
        const auto t0 = std::chrono::steady_clock::now();
        bool more = true;
        while (more && std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(6)) {
            for (int i = 0; i < 20 && more; ++i) {
                more = checkRun_->step();
                notePowers();
            }
        }
        if (!assetRoot_.empty()) res.popMount();
        c.tick = checkRun_->tick();
        c.checks = checkRun_->checksPassed();
        if (more) return;
        const GameWorld& w = checkRun_->world();
        c.desyncTick = checkRun_->desyncTick();
        c.finalMs = static_cast<int64_t>(std::llround(static_cast<double>(checkRun_->runTime()) * 1000.0));
        c.deaths = w.stats().deaths;
        c.damage = w.stats().damageTaken;
        c.shards = w.stats().collectibles();
        c.totalShards = w.stats().totalCollectibles();
        c.powers.assign(used_.begin(), used_.end());
        for (const std::string& p : w.stats().powersUsed)
            if (std::find(c.powers.begin(), c.powers.end(), p) == c.powers.end()) c.powers.push_back(p);
        if (checkRun_->desynced()) c.state = EngineCheck::State::Desynced;
        else if (w.state() != WorldState::Completed && w.state() != WorldState::Outro) c.state = EngineCheck::State::Unfinished;
        else c.state = EngineCheck::State::Identical;
        checkRun_.reset();
        checkReplay_.reset();
    }

    /// The powers the run really used: one that became active, or whose cooldown started again.
    void notePowers() {
        const Player& p = checkRun_->world().player();
        for (const auto& pw : p.powers()) {
            const std::string& id = pw->id();
            const float cd = pw->cooldownRemaining();
            auto it = cooldowns_.find(id);
            const bool restarted = it != cooldowns_.end() && cd > it->second + 1e-4f;
            if (pw->active() || restarted) used_.insert(id);
            cooldowns_[id] = cd;
        }
    }

    // ------------------------------------------------------------------ texts

    std::string checkpointName(const std::string& id, bool end) const {
        if (id.empty()) return end ? "Arrivée" : "Départ";
        int n = 0;
        for (const LevelObject& o : level_.objects)
            if (o.type == "checkpoint") {
                ++n;
                if (o.id == id) {
                    const std::string l = o.propStr("label");
                    return l.empty() ? "Point de passage " + std::to_string(n) : l;
                }
            }
        return "Point de passage";
    }
    bool checkpointAt(const std::string& id, Vec2& out) const {
        for (const LevelObject& o : level_.objects)
            if (o.type == "checkpoint" && o.id == id) {
                out = o.rect.bottomCenter();
                return true;
            }
        return false;
    }

    struct Fact {
        std::string key, value;
        Color color;
    };
    std::vector<Fact> facts(const tas::TasRun& t) const {
        const UITheme& th = g_.ui.theme();
        std::vector<Fact> f;
        f.push_back({"Catégorie", tasui::categoryName(t.category) + " · " + tasui::categoryShortRule(t.category), th.text});
        f.push_back({"Version du niveau", version_ > 0 ? std::to_string(version_) + " (classée)" : std::string("version installée"), th.text});
        f.push_back({"Version de la simulation", std::to_string(t.simVersion), th.text});
        if (const std::string d = tas::dateText(t.computedAt); !d.empty()) f.push_back({"Calculé le", d, th.text});
        if (t.explored > 0) {
            const uint64_t n = static_cast<uint64_t>(t.explored);
            f.push_back({"Effort de recherche", n >= 1000000 ? tas::countText(n) + " d'essais explorés" : tas::countText(n) + " essais explorés",
                         th.text});
        }
        const auto it = checks_.find(t.category);
        const EngineCheck c = it != checks_.end() ? it->second : EngineCheck{};
        switch (c.state) {
            case EngineCheck::State::Reading: f.push_back({"Vérification", "lecture du replay…", th.textDim}); break;
            case EngineCheck::State::Running: {
                const int pct = c.ticks > 0 ? static_cast<int>(100.0 * c.tick / c.ticks) : 0;
                f.push_back({"Vérification", "rejoué par le moteur… " + std::to_string(pct) + " %", th.textDim});
                break;
            }
            case EngineCheck::State::Identical:
                f.push_back({"Vérification", "rejoué à l'identique · " + std::to_string(c.checks) + " contrôles", th.success});
                break;
            case EngineCheck::State::Desynced:
                f.push_back({"Vérification", "désynchronisé au pas " + std::to_string(c.desyncTick), th.danger});
                break;
            case EngineCheck::State::Unfinished: f.push_back({"Vérification", "le replay s'arrête avant l'arrivée", th.danger}); break;
            case EngineCheck::State::Unreadable: f.push_back({"Vérification", "impossible : " + c.why, th.danger}); break;
        }
        f.push_back({"Classement", "hors classement, jamais compté", kTas});
        const bool engine = c.state == EngineCheck::State::Identical || c.state == EngineCheck::State::Unfinished;
        f.push_back({"Morts · dégâts subis", std::to_string(engine ? c.deaths : t.deaths) + " · " + std::to_string(engine ? c.damage : t.damageTaken), th.text});
        if (engine && c.totalShards > 0) f.push_back({"Éclats et reliques", std::to_string(c.shards) + " / " + std::to_string(c.totalShards), th.text});
        else if (t.totalCollectibles > 0)
            f.push_back({"Éclats et reliques", std::to_string(t.collectibles) + " / " + std::to_string(t.totalCollectibles), th.text});
        if (engine) {
            std::string pw;
            for (const std::string& id : c.powers)
                if (const PowerDefinition* d = g_.content.power(id)) pw += (pw.empty() ? "" : ", ") + i18n::tr(d->name);
            f.push_back({"Pouvoirs utilisés", pw.empty() ? std::string("aucun") : i18n::keep(pw), th.text});
        } else if (c.state == EngineCheck::State::Reading || c.state == EngineCheck::State::Running) {
            f.push_back({"Pouvoirs utilisés", "lus pendant la vérification…", th.textDim});
        }
        std::error_code ec;
        const auto bytes = fs::file_size(t.replayFile, ec);
        if (!ec && bytes > 0) f.push_back({"Taille du replay", std::to_string(std::max<uintmax_t>(1, (bytes + 512) / 1024)) + " Ko", th.text});
        return f;
    }

    // ------------------------------------------------------------------ drawing

    /// Title, pill, the level and its version; the categories at the right (or under, when they do
    /// not fit beside). Returns where the content starts.
    float drawHeader(const tas::TasRun& t, float x, float y, float w) {
        UIContext& ui = g_.ui;
        tasui::Pieces& P = tasui::pieces("sheet");
        // The categories: measured buttons.
        const float tf = fz(ui, 17), th = ui.s(44), tg = ui.s(8);
        std::vector<float> tw;
        float tabsW = 0.0f;
        for (RunCategory c : cats_) {
            tw.push_back(std::max(ui.s(110), ui.text().measure(tasui::categoryName(c), FontFace::Bold, ui.s(tf)).x + ui.s(40)));
            tabsW += tw.back() + (tw.size() > 1 ? tg : 0.0f);
        }
        const std::string title = "FICHE DU TAS", pillText = "MACHINE · HORS CLASSEMENT";
        const float pf = fz(ui, 13);
        const float pillW = tasui::pill(ui, pillText, {0, 0}, pf, false);
        float titleF = 34.0f;
        float titleW = ui.text().measure(title, FontFace::Title, ui.s(titleF)).x;
        const bool beside = titleW + ui.s(16) + pillW + ui.s(30) + tabsW <= w;
        const float titleRoom = (beside ? w - tabsW - ui.s(30) : w) - ui.s(16) - pillW;
        titleF = ui.fitFont(title, FontFace::Title, 34.0f, titleRoom, 0.6f);
        titleW = ui.text().measure(title, FontFace::Title, ui.s(titleF)).x;
        const float cy = y + ui.s(22);
        ui.label(title, {x, cy}, titleF, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        P.add("title", {x, cy - ui.s(titleF) * 0.6f, titleW, ui.s(titleF) * 1.2f});
        const float pw = tasui::pill(ui, pillText, {x + titleW + ui.s(16), cy}, pf);
        P.add("pill", {x + titleW + ui.s(16), cy - ui.s(pf), pw, ui.s(pf) * 2.0f});
        std::string sub = levelName_ + "  ·  " + (version_ > 0 ? "version " + std::to_string(version_) + " (classée)" : std::string("version installée"));
        float y2 = y + ui.s(62);
        const float sf = fz(ui, 18);
        {
            i18n::Verbatim asIs(!g_.content.level(req_.levelId));
            const std::string s = ui.elide(sub, FontFace::Body, sf, w);
            ui.label(s, {x, y2 + ui.s(12)}, sf, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            P.add("subtitle", {x, y2 + ui.s(12) - ui.s(sf) * 0.6f, ui.text().measure(s, FontFace::Body, ui.s(sf)).x, ui.s(sf) * 1.2f});
        }
        float tx = x + w - tabsW, tyy = y + ui.s(2);
        if (!beside) {
            tyy = y2 + ui.s(36);
            tx = x;
        }
        for (size_t i = 0; i < cats_.size(); ++i) {
            const Rect r{tx, tyy, tw[i], th};
            UIContext::ButtonStyle st;
            st.small = true;
            st.selected = cats_[i] == req_.category;
            st.fontSize = tf;
            if (ui.button("cat" + std::to_string(i), r, tasui::categoryName(cats_[i]), st) && cats_[i] != req_.category) selectCategory(cats_[i]);
            if (st.selected) ui.r().drawRoundedRect(r, ui.s(8), kTas, std::max(1.5f, ui.s(2.5f)));
            P.add("tab" + std::to_string(i), r);
            tx += tw[i] + tg;
        }
        // Q / E, LB / RB: the previous, the next category.
        if (cats_.size() > 1 && g_.ui.acceptsInput()) {
            InputManager& in = g_.app.input();
            int k = static_cast<int>(std::find(cats_.begin(), cats_.end(), req_.category) - cats_.begin());
            const int n = static_cast<int>(cats_.size());
            if (in.menuPressed(Action::MenuTabPrev)) selectCategory(cats_[static_cast<size_t>((k + n - 1) % n)]);
            else if (in.menuPressed(Action::MenuTabNext)) selectCategory(cats_[static_cast<size_t>((k + 1) % n)]);
        }
        (void)t;
        return (beside ? y2 + ui.s(44) : tyy + th + ui.s(16));
    }

    void selectCategory(RunCategory c) {
        if (c == req_.category) return;
        req_.category = c;
        g_.uiSound("ui_move", 0.6f);
        startCategory();
    }

    void drawLeft(const tas::TasRun& t, const Rect& a) {
        UIContext& ui = g_.ui;
        tasui::Pieces& P = tasui::pieces("sheet");
        const UITheme& th = ui.theme();
        float y = a.y;
        // The time and what it is.
        {
            const float pad = ui.s(20), room = a.w - 2 * pad;
            const std::string kick = tasui::categoryName(t.category) + " · MEILLEUR TEMPS TROUVÉ PAR LA MACHINE";
            const float kf = ui.fitFont(kick, FontFace::Bold, fz(ui, 13), room, 0.75f);
            const std::string time = tas::timeText(t.timeMs);
            const float vf = ui.fitFont(time, FontFace::Title, 60.0f, room, 0.5f);
            const std::string text = t.sameAsAny ? "Le même parcours que l'ANY% : sans mort" + std::string(t.category == RunCategory::NoDamage ? " ni dégât" : "") +
                                                       ", il vaut aussi pour cette catégorie. C'est le meilleur temps que notre recherche automatique a trouvé : un "
                                                       "joueur, ou une meilleure recherche, peut le battre."
                                                 : "Le meilleur temps que notre recherche automatique a trouvé sur le moteur du jeu. Ce n'est pas un optimum prouvé : "
                                                   "un joueur, ou une meilleure recherche, peut le battre.";
            const float xf = fz(ui, 15);
            const float textH = ui.text().measureWrapped(text, FontFace::Body, ui.s(xf), room).y;
            const float kh = ui.s(kf) * 1.5f, vh = ui.s(vf) * 1.27f;
            const Rect card{a.x, y, a.w, ui.s(16) + kh + vh + ui.s(10) + textH + ui.s(18)};
            ui.r().fillRoundedRectGradient(card, ui.s(12), tasui::kTasCard, Color{14, 20, 40, 235});
            ui.r().drawRoundedRect(card, ui.s(12), tasui::kTasLine, std::max(1.0f, ui.s(1.2f)));
            float yy = card.y + ui.s(16);
            ui.label(ui.elide(kick, FontFace::Bold, kf, room), {card.x + pad, yy}, kf, kTas, Align::Left, FontFace::Bold);
            P.add("hero/kicker", {card.x + pad, yy, std::min(room, ui.text().measure(kick, FontFace::Bold, ui.s(kf)).x), ui.s(kf) * 1.3f});
            yy += kh;
            ui.label(time, {card.x + pad, yy}, vf, kTas, Align::Left, FontFace::Title);
            P.add("hero/time", {card.x + pad, yy, ui.text().measure(time, FontFace::Title, ui.s(vf)).x, ui.s(vf) * 1.2f});
            yy += vh + ui.s(10);
            ui.labelWrapped(text, {card.x + pad, yy}, room, xf, ui.theme().text);
            P.add("hero/text", {card.x + pad, yy, room, textH});
            y = card.bottom() + ui.s(14);
        }
        // Its identity: every value from the computation and the engine's playback.
        const std::vector<Fact> fs = facts(t);
        const float kf = fz(ui, 16), vf0 = fz(ui, 17);
        const float rowH = std::max(ui.s(36), lineH(ui, 17) + ui.s(6));
        const std::string how = "Une seule recherche générique, lancée chez nous et jamais sur votre ordinateur, joue la simulation déterministe (120 images "
                                "par seconde) et garde la course la plus rapide qu'elle trouve. Le résultat est un replay ordinaire : le jeu le rejoue à "
                                "l'identique, c'est la preuve qu'il est faisable. Il est livré avec les niveaux et recalculé quand le niveau ou la "
                                "simulation change.";
        const float hf = fz(ui, 15);
        const float howH = ui.text().measureWrapped(how, FontFace::Body, ui.s(hf), a.w).y + lineH(ui, 13) + ui.s(10);
        const float factsH = rowH * static_cast<float>(fs.size());
        float rh = rowH;
        if (y + factsH > a.bottom()) rh = std::max(lineH(ui, 16), (a.bottom() - y) / static_cast<float>(fs.size()));
        for (size_t i = 0; i < fs.size(); ++i) {
            const Fact& f = fs[i];
            const float cy = y + rh * 0.5f;
            if (y + rh > a.bottom() + 0.5f) break;
            const float keyW = std::min(a.w * 0.48f, ui.text().measure(f.key, FontFace::Body, ui.s(kf)).x);
            const std::string k = ui.elide(f.key, FontFace::Body, kf, a.w * 0.48f);
            ui.label(k, {a.x, cy}, kf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
            P.add("fact/key" + std::to_string(i), {a.x, cy - ui.s(kf) * 0.6f, keyW, ui.s(kf) * 1.2f});
            const float room = a.w - keyW - ui.s(16);
            const float vf = ui.fitFont(f.value, FontFace::Bold, vf0, room, 0.75f);
            const std::string v = ui.elide(f.value, FontFace::Bold, vf, room);
            const float vw = ui.text().measure(v, FontFace::Bold, ui.s(vf)).x;
            ui.label(v, {a.right(), cy}, vf, f.color, Align::Right, FontFace::Bold, VAlign::Middle);
            P.add("fact/value" + std::to_string(i), {a.right() - vw, cy - ui.s(vf) * 0.6f, vw, ui.s(vf) * 1.2f});
            ui.r().fillRect({a.x, y + rh - 1.0f, a.w, 1.0f}, ui.theme().accent.withAlpha(26));
            y += rh;
        }
        y += ui.s(12);
        // How it is made: when there is room for it whole.
        if (y + howH <= a.bottom()) {
            ui.label("COMMENT IL EST FAIT", {a.x, y}, fz(ui, 13), th.textDim, Align::Left, FontFace::Bold);
            P.add("how/kicker", {a.x, y, ui.text().measure("COMMENT IL EST FAIT", FontFace::Bold, ui.s(fz(ui, 13))).x, ui.s(fz(ui, 13)) * 1.3f});
            y += lineH(ui, 13) + ui.s(10);
            const Vec2 sz = ui.labelWrapped(how, {a.x, y}, a.w, hf, Color{200, 211, 226, 255});
            P.add("how/text", {a.x, y, a.w, sz.y});
        }
    }

    void drawRight(const tas::TasRun& t, const Rect& a) {
        UIContext& ui = g_.ui;
        tasui::Pieces& P = tasui::pieces("sheet");
        const UITheme& th = ui.theme();
        // The route over the level.
        const float lf = fz(ui, 14);
        const float cardH = std::min(ui.s(264), a.h * 0.42f);
        const Rect card{a.x, a.y, a.w, cardH};
        ui.r().fillRoundedRect(card, ui.s(12), Color{14, 20, 40, 235});
        ui.r().drawRoundedRect(card, ui.s(12), th.border, std::max(1.0f, ui.s(1.0f)));
        const float pad = ui.s(18);
        const float headH = ui.s(46);
        {
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = fz(ui, 14);
            const std::string b2 = "Mon record", b1 = "TAS";
            const float w2 = ui.text().measure(b2, FontFace::Bold, ui.s(st.fontSize)).x + ui.s(26), w1 = ui.text().measure(b1, FontFace::Bold, ui.s(st.fontSize)).x + ui.s(26);
            const float bh = ui.s(30), by = card.y + ui.s(10);
            float bx = card.right() - pad - w2;
            const bool mineKnown = !mineRoute_.empty();
            if (mineKnown) {
                st.selected = showMine_;
                const Rect r2{bx, by, w2, bh};
                if (ui.button("showmine", r2, b2, st)) showMine_ = !showMine_;
                P.add("route/mine", r2);
                bx -= ui.s(8) + w1;
            } else {
                bx = card.right() - pad - w1;
            }
            st.selected = showTas_;
            const Rect r1{bx, by, w1, bh};
            if (ui.button("showtas", r1, b1, st)) showTas_ = !showTas_;
            P.add("route/tas", r1);
            const std::string head = "Trajet sur le niveau";
            const float room = bx - ui.s(12) - (card.x + pad);
            const float hf = ui.fitFont(head, FontFace::Bold, fz(ui, 18), room, 0.7f);
            const std::string hs = ui.elide(head, FontFace::Bold, hf, room);
            ui.label(hs, {card.x + pad, by + bh * 0.5f}, hf, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
            P.add("route/head", {card.x + pad, by + bh * 0.5f - ui.s(hf) * 0.6f, ui.text().measure(hs, FontFace::Bold, ui.s(hf)).x, ui.s(hf) * 1.2f});
        }
        const float legendH = lineH(ui, 14) + ui.s(12);
        const Rect pic{card.x + ui.s(4), card.y + headH, card.w - ui.s(8), cardH - headH - legendH};
        drawRoute(t, pic);
        P.add("route/picture", pic);
        // Its legend, then what a click does (when it fits after it).
        {
            float lx = card.x + pad;
            const float ly = pic.bottom() + legendH * 0.5f;
            auto item = [&](int kind, const std::string& text) {
                const float sw = ui.s(28);
                if (kind == 0) ui.r().fillRect({lx, ly - ui.s(1.5f), sw, ui.s(3)}, kTas);
                else if (kind == 1)
                    for (float d = 0; d < sw; d += ui.s(9)) ui.r().fillRect({lx + d, ly - ui.s(1.2f), std::min(ui.s(5), sw - d), ui.s(2.5f)}, kMine);
                else {
                    ui.r().fillCircle({lx + ui.s(8), ly}, ui.s(8), Color{11, 16, 32, 255});
                    ui.r().drawCircle({lx + ui.s(8), ly}, ui.s(8), Color::white(), std::max(1.0f, ui.s(1.5f)));
                    ui.label("1", {lx + ui.s(8), ly}, fz(ui, 10), Color::white(), Align::Center, FontFace::Bold, VAlign::Middle);
                }
                const float ix = lx + (kind == 2 ? ui.s(22) : sw + ui.s(8));
                const float w = ui.text().measure(text, FontFace::Body, ui.s(lf)).x;
                ui.label(text, {ix, ly}, lf, Color{200, 211, 226, 255}, Align::Left, FontFace::Body, VAlign::Middle);
                P.add("route/legend" + std::to_string(kind), {lx, ly - ui.s(lf) * 0.6f, ix + w - lx, ui.s(lf) * 1.2f});
                lx = ix + w + ui.s(20);
            };
            const std::string mineText = mine_ ? "Mon record (" + formatGhostSeconds(static_cast<float>(mine_->meta.timeMs) / 1000.0f) + ")" : std::string();
            const std::string marks = "points de passage, numérotés comme le tableau";
            const float need = ui.s(36) + ui.text().measure("TAS (machine)", FontFace::Body, ui.s(lf)).x + ui.s(20) +
                               (mineRoute_.empty() ? 0.0f : ui.s(36) + ui.text().measure(mineText, FontFace::Body, ui.s(lf)).x + ui.s(20)) + ui.s(22) +
                               ui.text().measure(marks, FontFace::Body, ui.s(lf)).x;
            const float room = card.right() - pad - lx;
            item(0, "TAS (machine)");
            if (!mineRoute_.empty()) item(1, mineText);
            if (need <= room) item(2, marks);
            const std::string hint = "Clic sur le trajet : la visionneuse s'ouvre à cet instant";
            const float hw = ui.text().measure(hint, FontFace::Body, ui.s(fz(ui, 13))).x;
            if (lx + hw <= card.right() - pad) {
                ui.label(hint, {card.right() - pad, ly}, fz(ui, 13), th.textDim, Align::Right, FontFace::Body, VAlign::Middle);
                P.add("route/hint", {card.right() - pad - hw, ly - ui.s(fz(ui, 13)) * 0.6f, hw, ui.s(fz(ui, 13)) * 1.2f});
            }
        }
        // The splits.
        float y = card.bottom() + ui.s(16);
        ui.label("TEMPS DE PASSAGE DU TAS", {a.x, y}, fz(ui, 13), th.textDim, Align::Left, FontFace::Bold);
        P.add("splits/kicker", {a.x, y, ui.text().measure("TEMPS DE PASSAGE DU TAS", FontFace::Bold, ui.s(fz(ui, 13))).x, ui.s(fz(ui, 13)) * 1.3f});
        y += lineH(ui, 13) + ui.s(6);
        drawSplits(t, {a.x, y, a.w, a.bottom() - y});
    }

    /// The level drawn small (the statistics' picture of it), the TAS's route over it, mine dashed.
    void drawRoute(const tas::TasRun& t, const Rect& box) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        r.fillRoundedRect(box, ui.s(8), Color{20, 9, 11, 255});
        const statsart::LevelInfo* li = art_.info(req_.levelId);
        const int pw = std::max(8, static_cast<int>(std::lround(box.w / 8.0f)) * 8), ph = std::max(8, static_cast<int>(std::lround(box.h / 8.0f)) * 8);
        const Texture* tex = art_.stretch(req_.levelId, 0.0f, 1.0f, pw, ph);
        if (!li || !li->ok || !tex) {
            const std::string m = art_.failed(req_.levelId) ? "Aperçu indisponible" : "Dessin du niveau…";
            ui.label(m, box.center(), fz(ui, 15), ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);
            return;
        }
        r.drawTexture(*tex, nullptr, box);
        const Rect win = statsart::stretchWindow(*li, 0.0f, 1.0f, static_cast<float>(pw), static_cast<float>(ph));
        auto toBox = [&](Vec2 p) { return Vec2{box.x + (p.x - win.x) / win.w * box.w, box.y + (p.y - win.y) / win.h * box.h}; };
        r.pushClip(box);
        const float lw = std::max(1.5f, ui.s(2.5f));
        if (showMine_)
            for (const auto& line : mineRoute_)
                for (size_t i = 1; i < line.size(); ++i)
                    if (i % 2 == 0) r.drawLine(toBox(line[i - 1] + Vec2{0.0f, -6.0f}), toBox(line[i] + Vec2{0.0f, -6.0f}), kMine.withAlpha(230), std::max(1.0f, ui.s(2.0f)));
        std::vector<std::vector<Vec2>> tasRoute;
        if (!t.route.empty()) tasRoute.push_back(t.route);
        else if (tasTrack_) tasRoute = sampleRoute(*tasTrack_);
        if (showTas_) {
            for (const auto& line : tasRoute)
                for (size_t i = 1; i < line.size(); ++i) r.drawLine(toBox(line[i - 1]), toBox(line[i]), kTas.withAlpha(70), lw + ui.s(4));
            for (const auto& line : tasRoute)
                for (size_t i = 1; i < line.size(); ++i) r.drawLine(toBox(line[i - 1]), toBox(line[i]), kTas, lw);
        }
        // The checkpoints of the TAS, numbered as the table's segments end there; its finish.
        const float rad = std::max(ui.s(9), 7.0f);
        for (size_t k = 0; k < t.splits.size(); ++k) {
            Vec2 at;
            if (!checkpointAt(t.splits[k].checkpoint, at)) continue;
            const Vec2 c = toBox(at + Vec2{0.0f, -20.0f});
            r.fillCircle(c, rad, Color{11, 16, 32, 255});
            r.drawCircle(c, rad, Color::white(), std::max(1.0f, ui.s(1.5f)));
            ui.label(std::to_string(k + 1), c, fz(ui, 11), Color::white(), Align::Center, FontFace::Bold, VAlign::Middle);
        }
        if (!tasRoute.empty() && !tasRoute.back().empty()) {
            const Vec2 c = toBox(tasRoute.back().back());
            r.fillCircle(c, rad, Color{11, 16, 32, 255});
            r.drawCircle(c, rad, kTas, std::max(1.0f, ui.s(1.5f)));
            r.fillTriangle({c.x - rad * 0.35f, c.y - rad * 0.5f}, {c.x - rad * 0.35f, c.y + rad * 0.5f}, {c.x + rad * 0.5f, c.y}, kTas);
        }
        // A click on the route: the viewer opens at that instant (the TAS's nearest moment).
        bool hovered = false;
        const bool clicked = ui.hotspot("route", box, &hovered);
        float at = 0.0f;
        bool near = false;
        if (tasTrack_ && hovered && ui.usingPointer()) {
            const Vec2 mp = ui.input().mousePos();
            const Vec2 world{win.x + (mp.x - box.x) / box.w * win.w, win.y + (mp.y - box.y) / box.h * win.h};
            at = tasTrack_->nearestTime(world);
            const GhostTrack::Pose p = tasTrack_->poseAt(at);
            if (p.visible && (toBox(p.feet + Vec2{0.0f, -14.0f}) - mp).length() < ui.s(40)) {
                near = true;
                const Vec2 c = toBox(p.feet + Vec2{0.0f, -14.0f});
                r.fillCircle(c, ui.s(6), Color::white());
                r.fillCircle(c, ui.s(4), kTas);
                const std::string tt = formatGhostTime(at);
                const Vec2 ts = ui.text().measure(tt, FontFace::Bold, ui.s(fz(ui, 13)));
                Rect tip{c.x + ui.s(10), c.y - ts.y - ui.s(14), ts.x + ui.s(14), ts.y + ui.s(6)};
                tip.x = std::clamp(tip.x, box.x + ui.s(2), box.right() - ui.s(2) - tip.w);
                tip.y = std::clamp(tip.y, box.y + ui.s(2), box.bottom() - ui.s(2) - tip.h);
                r.fillRoundedRect(tip, ui.s(5), Color{8, 12, 26, 235});
                ui.label(tt, tip.center(), fz(ui, 13), kTas, Align::Center, FontFace::Bold, VAlign::Middle);
            }
        }
        r.popClip();
        if (ui.focused("route")) r.drawRoundedRect(box, ui.s(8), ui.theme().accent, std::max(1.0f, ui.s(1.5f)));
        if (clicked) tasui::watch(g_, t, near ? static_cast<double>(at) : 0.0);
    }

    void drawSplits(const tas::TasRun& t, const Rect& a) {
        UIContext& ui = g_.ui;
        tasui::Pieces& P = tasui::pieces("sheet");
        const UITheme& th = ui.theme();
        const float fs = fz(ui, 16), hf = fz(ui, 12);
        const std::string key = levelKey(t);
        // The segments of the TAS: from the start, checkpoint after checkpoint, to the finish.
        struct Row {
            std::string from, to, name;
            int64_t tas = -1, cum = -1;
            float mine = -1.0f;
        };
        std::vector<Row> rows;
        for (size_t i = 0; i <= t.splits.size(); ++i) {
            Row r;
            r.from = i == 0 ? std::string() : t.splits[i - 1].checkpoint;
            r.to = i < t.splits.size() ? t.splits[i].checkpoint : std::string();
            r.name = checkpointName(r.from, false) + " → " + checkpointName(r.to, true);
            r.tas = t.segment(r.from, r.to);
            r.cum = i < t.splits.size() ? t.splits[i].timeMs : t.timeMs;
            if (const PersonalBests::Record* rec = g_.bests.record(key, r.from, r.to); rec && rec->best > 0.0f) r.mine = rec->best;
            rows.push_back(std::move(r));
        }
        // Columns from the right, measured: watch | best − TAS | my best | cumul | TAS | segment.
        // Each column as wide as its widest value or its header (measured in the language shown).
        const float num = ui.text().measure("10:00,000", FontFace::Bold, ui.s(fs)).x;
        auto colW = [&](float value, const char* head) { return std::max(value, ui.text().measure(head, FontFace::Bold, ui.s(hf)).x) + ui.s(18); };
        const float tasW = colW(num, "TAS"), cumW = colW(num, "CUMUL"), mineW = colW(num, "MON MEILLEUR");
        const float gapW = colW(ui.text().measure("+1:15,500", FontFace::Bold, ui.s(fs)).x, "MEILLEUR − TAS");
        const std::string watchText = "▶ TAS";
        const float bw = std::max(ui.text().measure(watchText, FontFace::Bold, ui.s(fz(ui, 13))).x + ui.s(22),
                                  ui.text().measure("REGARDER", FontFace::Bold, ui.s(hf)).x + ui.s(4));
        const float cWatch = a.right() - ui.s(10);
        const float cGap = cWatch - bw - ui.s(16), cMine = cGap - gapW, cCum = cMine - mineW, cTas = cCum - cumW;
        const float cNo = a.x + ui.s(12), cName = a.x + ui.s(14) + ui.text().measure("00", FontFace::Bold, ui.s(fs)).x + ui.s(10);
        const float nameRoom = cTas - tasW - cName - ui.s(8);
        const bool wide = nameRoom >= ui.s(80);
        const float headH = lineH(ui, 12) + ui.s(4);
        auto head = [&](const std::string& s, float x, Align al, const std::string& id) {
            const float w = ui.text().measure(s, FontFace::Bold, ui.s(hf)).x;
            ui.label(s, {x, a.y}, hf, th.textDim, al, FontFace::Bold);
            P.add("splits/head/" + id, {al == Align::Right ? x - w : x, a.y, w, ui.s(hf) * 1.3f});
        };
        head("#", cNo, Align::Left, "no");
        if (wide) head("SEGMENT", cName, Align::Left, "name");
        head("TAS", cTas, Align::Right, "tas");
        head("CUMUL", cCum, Align::Right, "cum");
        head("MON MEILLEUR", cMine, Align::Right, "mine");
        head("MEILLEUR − TAS", cGap, Align::Right, "gap");
        head("REGARDER", cWatch, Align::Right, "watch");
        const float rh = std::max(ui.s(34), lineH(ui, 16) + ui.s(6));
        const float foot = lineH(ui, 14) + ui.s(10);
        const float totalH = rh + ui.s(6);
        const Rect list{a.x, a.y + headH, a.w, std::max(rh, a.h - headH - totalH - foot)};
        ui.beginScroll("rows", list, static_cast<float>(rows.size()) * rh);
        float y = list.y - ui.scrollOffset();
        bool allMine = true;
        float mineSum = 0.0f;
        for (size_t i = 0; i < rows.size(); ++i, y += rh) {
            const Row& r = rows[i];
            if (r.mine > 0.0f) mineSum += r.mine;
            else allMine = false;
            const Rect row{list.x, y, list.w - ui.s(12), rh - ui.s(2)};
            const Rect wb{cWatch - bw, row.center().y - ui.s(13), bw, ui.s(26)};
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = fz(ui, 13);
            st.enabled = r.tas > 0;
            const bool go = ui.button("seg" + std::to_string(i), wb, watchText, st);
            if (row.bottom() < list.y || row.y > list.bottom()) continue;
            if (i % 2 == 0) ui.r().fillRoundedRect(row, ui.s(6), Color{18, 26, 50, 140});
            const float cy = row.center().y;
            const Rect vis{list.x, list.y, list.w, list.h};
            auto cell = [&](const std::string& s, float x, Align al, FontFace f, Color c, const std::string& id) {
                ui.label(s, {x, cy}, fs, c, al, f, VAlign::Middle);
                const float w = ui.text().measure(s, f, ui.s(fs)).x;
                const Rect rr{al == Align::Right ? x - w : x, cy - ui.s(fs) * 0.6f, w, ui.s(fs) * 1.2f};
                if (vis.containsRect(rr)) P.add("row" + std::to_string(i) + "/" + id, rr);
            };
            cell(std::to_string(i + 1), cNo, Align::Left, FontFace::Bold, th.text, "no");
            if (wide) {
                i18n::Verbatim asIs(false);
                cell(ui.elide(r.name, FontFace::Body, fs, nameRoom), cName, Align::Left, FontFace::Body, th.text, "name");
            }
            cell(r.tas > 0 ? tas::timeText(r.tas) : std::string("—"), cTas, Align::Right, FontFace::Bold, kTas, "tas");
            cell(formatRunMs(r.cum), cCum, Align::Right, FontFace::Body, Color{200, 211, 226, 255}, "cum");
            cell(r.mine > 0.0f ? formatGhostSeconds(r.mine) : std::string("—"), cMine, Align::Right, FontFace::Body, r.mine > 0.0f ? kGold : th.textDim, "mine");
            if (r.mine > 0.0f && r.tas > 0) {
                const int64_t d = static_cast<int64_t>(std::llround(static_cast<double>(r.mine) * 1000.0)) - r.tas;
                cell(tas::gapText(d), cGap, Align::Right, FontFace::Bold, d > 0 ? kBehind : th.success, "gap");
            } else {
                cell("—", cGap, Align::Right, FontFace::Body, th.textDim, "gap");
            }
            if (vis.containsRect(wb)) P.add("row" + std::to_string(i) + "/watch", wb);
            if (go && st.enabled) tasui::watchSegment(g_, t, r.from, r.to, r.name);
        }
        ui.endScroll();
        // The total: the TAS, my Sum of Best on the same segments (when each is known), the gap.
        const Rect tot{a.x, list.bottom() + ui.s(4), a.w - ui.s(12), rh};
        ui.r().fillRoundedRect(tot, ui.s(6), Color{46, 60, 14, 130});
        ui.r().drawRoundedRect(tot, ui.s(6), tasui::kTasLine, std::max(1.0f, ui.s(1.0f)));
        const float cy = tot.center().y;
        auto tcell = [&](const std::string& s, float x, Align al, Color c, const std::string& id) {
            ui.label(s, {x, cy}, fs, c, al, FontFace::Bold, VAlign::Middle);
            const float w = ui.text().measure(s, FontFace::Bold, ui.s(fs)).x;
            P.add("total/" + id, {al == Align::Right ? x - w : x, cy - ui.s(fs) * 0.6f, w, ui.s(fs) * 1.2f});
        };
        tcell("Total", wide ? cName : cNo, Align::Left, th.text, "name");
        tcell(tas::timeText(t.timeMs), cTas, Align::Right, kTas, "tas");
        tcell("—", cCum, Align::Right, th.textDim, "cum");
        if (allMine && mineSum > 0.0f) {
            tcell(formatGhostSeconds(mineSum), cMine, Align::Right, kGold, "mine");
            const int64_t d = static_cast<int64_t>(std::llround(static_cast<double>(mineSum) * 1000.0)) - t.timeMs;
            tcell(tas::gapText(d), cGap, Align::Right, d > 0 ? kBehind : th.success, "gap");
        } else {
            tcell("—", cMine, Align::Right, th.textDim, "mine");
            tcell("—", cGap, Align::Right, th.textDim, "gap");
        }
        const std::string note = "« Mon meilleur » : chaque meilleur segment, d'où qu'il vienne ; leur total est la somme des meilleurs (SOB).";
        const float nf = ui.fitFont(note, FontFace::Body, fz(ui, 14), a.w, 0.8f);
        const std::string ns = ui.elide(note, FontFace::Body, nf, a.w);
        const float ny = tot.bottom() + ui.s(8);
        ui.label(ns, {a.x, ny}, nf, th.textDim, Align::Left, FontFace::Body);
        P.add("splits/note", {a.x, ny, ui.text().measure(ns, FontFace::Body, ui.s(nf)).x, ui.s(nf) * 1.3f});
    }

    /// The hints at the left, the actions at the right (measured: the hints go when they do not fit).
    bool drawBottom(const tas::TasRun& t, const Rect& bar) {
        UIContext& ui = g_.ui;
        tasui::Pieces& P = tasui::pieces("sheet");
        const float bf = fz(ui, 20);
        struct Btn {
            std::string id, text;
            bool primary = false, enabled = true;
        };
        std::vector<Btn> bs = {{"back", "Retour"}, {"compare", "Comparer avec mon record"}, {"race", "Course contre le TAS"}, {"watch", "▶ Regarder le TAS"}};
        bs[1].enabled = mine_.has_value();
        bs[3].primary = true;
        std::vector<float> w;
        float total = 0.0f;
        for (const Btn& b : bs) {
            w.push_back(ui.text().measure(b.text, FontFace::Bold, ui.s(bf)).x + ui.s(40));
            total += w.back();
        }
        total += ui.s(12) * static_cast<float>(bs.size() - 1);
        float f = bf;
        if (total > bar.w) {
            // Every label smaller, the same size for the four.
            const float k = bar.w / total;
            f = std::max(fz(ui, 12), bf * k);
            total = 0.0f;
            for (size_t i = 0; i < bs.size(); ++i) {
                w[i] = ui.text().measure(bs[i].text, FontFace::Bold, ui.s(f)).x + ui.s(24);
                total += w[i];
            }
            total += ui.s(12) * static_cast<float>(bs.size() - 1);
        }
        float x = bar.right() - total;
        const float buttonsLeft = x;
        bool back = false;
        for (size_t i = 0; i < bs.size(); ++i) {
            UIContext::ButtonStyle st;
            st.primary = bs[i].primary;
            st.enabled = bs[i].enabled;
            st.fontSize = f;
            const Rect r{x, bar.y, w[i], bar.h};
            const bool hit = ui.button(bs[i].id, r, bs[i].text, st);
            if (bs[i].id == "watch") ui.r().drawRoundedRect(r, ui.s(8), kTas, std::max(1.0f, ui.s(1.5f)));
            P.add("button/" + bs[i].id, r);
            x += w[i] + ui.s(12);
            if (!hit || !st.enabled) continue;
            if (bs[i].id == "back") back = true;
            else if (bs[i].id == "watch") tasui::watch(g_, t);
            else if (bs[i].id == "race") raceIt();
            else if (bs[i].id == "compare") compareWithMine(t);
        }
        // The hints, when they fit before the buttons.
        const float hh = ui.s(30), hfz = fz(ui, 17);
        const float cy = bar.center().y;
        float hx = bar.x;
        auto hintW = [&](bool cats) {
            if (cats)
                return Glyphs::measureAction(ui, Action::MenuTabPrev, hh) + ui.s(4) + Glyphs::measureAction(ui, Action::MenuTabNext, hh) + ui.s(8) +
                       ui.text().measure("Catégorie", FontFace::Body, ui.s(hfz)).x;
            return Glyphs::measureAction(ui, Action::MenuBack, hh) + ui.s(8) + ui.text().measure("Retour", FontFace::Body, ui.s(hfz)).x;
        };
        const bool withCats = cats_.size() > 1;
        const float need = (withCats ? hintW(true) + ui.s(26) : 0.0f) + hintW(false);
        if (hx + need <= buttonsLeft - ui.s(20)) {
            if (withCats) {
                const float x0 = hx;
                hx += Glyphs::drawAction(ui, Action::MenuTabPrev, {hx, cy}, hh) + ui.s(4);
                hx += Glyphs::drawAction(ui, Action::MenuTabNext, {hx, cy}, hh) + ui.s(8);
                const Vec2 s = ui.label("Catégorie", {hx, cy}, hfz, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
                hx += s.x;
                P.add("hint/cats", {x0, cy - hh * 0.5f, hx - x0, hh});
                hx += ui.s(26);
            }
            const float x0 = hx;
            hx += Glyphs::drawAction(ui, Action::MenuBack, {hx, cy}, hh) + ui.s(8);
            const Vec2 s = ui.label("Retour", {hx, cy}, hfz, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            P.add("hint/back", {x0, cy - hh * 0.5f, hx + s.x - x0, hh});
        }
        return back;
    }

    void raceIt() {
        if (req_.onRace) {
            const RunCategory c = req_.category;
            auto cb = req_.onRace;
            g_.app.popScene();
            cb(c);
            return;
        }
        tasui::race(g_, req_.levelId, req_.category);
    }

    /// « Comparer avec mon record »: the existing comparison, A my record, B the TAS.
    void compareWithMine(const tas::TasRun& t) {
        if (!mine_) return;
        auto a = GhostStore::loadReplay(mine_->file);
        auto b = loadTasReplay(t, tas::catalog().root());
        if (!a || !b) return;
        ReplayChoice ca, cb;
        ca.replay = std::make_shared<const ReplayData>(std::move(*a));
        ca.meta = mine_->meta;
        cb.replay = std::make_shared<const ReplayData>(std::move(*b));
        cb.meta = t.ghost().meta;
        g_.app.pushScene(makeReplayCompare(g_, req_.levelId, std::move(ca), std::move(cb)));
    }

    GameServices& g_;
    tasui::SheetRequest req_;
    statsart::LevelArt art_;
    std::vector<RunCategory> cats_;
    std::string levelName_, assetRoot_;
    LevelData level_;
    bool haveLevel_ = false;
    int version_ = 0;
    std::optional<GhostEntry> mine_;
    std::future<std::shared_ptr<const ReplayData>> mineJob_, tasJob_;
    std::shared_ptr<const ReplayData> mineReplay_, tasReplay_, checkReplay_;
    std::vector<std::vector<Vec2>> mineRoute_;
    std::shared_ptr<const GhostTrack> tasTrack_;
    std::map<RunCategory, EngineCheck> checks_;
    std::unique_ptr<ReplayPlayback> checkRun_;
    RunCategory checkCat_ = RunCategory::AnyPercent;
    std::set<std::string> used_;
    std::map<std::string, float> cooldowns_;
    bool showTas_ = true, showMine_ = true;
};

}  // namespace

namespace tasui {
std::unique_ptr<Scene> makeSheet(GameServices& g, SheetRequest req) { return std::make_unique<TasSheetScene>(g, std::move(req)); }
}  // namespace tasui

}  // namespace cosmic
