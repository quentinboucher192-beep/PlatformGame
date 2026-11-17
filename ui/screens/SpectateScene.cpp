// SOCIAL › Amis › « Regarder » (2.36): a friend's run, live.
//
// The friend's game streams its run through the server (game/online/Spectate.h): the level
// and its version, the seed, the keys of the powers, a challenge's rules, then the input of
// every simulation step. This screen plays the run again from them exactly like an exact
// replay (ReplaySimulator, the replay's state digests checked on the way), about a second
// behind live: the level loaded here (the same version: campaign by its fingerprint, a
// community level installed at that version), the game's own camera on the friend, the HUD
// of their run, their name, the level, the time and « EN DIRECT ».
//
// Read only: it reads the server every half second and never sends anything to the friend's
// game; the friend is not told that anybody watches. When the stream stops: « La partie est
// terminée » (the friend finished, failed or left; a new run of theirs is followed at once)
// or « Connexion perdue » (nothing from their game, or from the server, for a while).

#include <algorithm>
#include <cmath>

#include "engine/core/Log.h"
#include "game/GameServices.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/Hud.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Spectate.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/LevelPowerKeysScene.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

constexpr float kPoll = 0.5f;          // reading the server while the run is played
constexpr float kPollIdle = 2.0f;      // no run, or the run ended: a new one is followed at once
constexpr uint32_t kTarget = 96;       // steps kept ahead of the one shown (0.8 s: about 1 s behind live)
constexpr int kCatchUpBudget = 3000;   // steps simulated per frame to catch up
constexpr float kLostAfter = 8.0f;     // no answer from the server for this long: « connexion perdue »
const Color kLive{255, 77, 109, 255};

/// Never below 11.5 pixels (small windows).
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

class SpectateScene : public Scene {
public:
    SpectateScene(GameServices& g, std::string playerId, std::string name, std::string avatar)
        : g_(g), ui_(g), playerId_(std::move(playerId)), name_(std::move(name)), avatar_(std::move(avatar)) {
        ui_.avatarsOpenProfile = false;
    }

    void onEnter() override {
        poll();
        g_.ui.setFocus("spectate/back");
    }

    void onExit() override {
        dropRun();
        g_.app.audio().stopAmbience(600);
        g_.playMusic("music/menu_theme.ogg");  // the music of SOCIAL again
    }

    void update(float dt) override {
        pollTimer_ -= dt;
        sinceAnswer_ += dt;
        if (!busy_ && !stopped_ && pollTimer_ <= 0.0f) poll();
        if (!world_ || !sim_) return;
        advance(dt);
        // The HUD shows each power with this player's key in this level (as the replays do).
        if (!powerKeysReady_) powerKeys_ = playerPowerKeys(g_, level_);
        powerKeysReady_ = true;
        applyPowerSlotKeys(g_.app.input(), world_->player(), powerKeys_);
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        Renderer& r = g_.app.renderer();
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        const float W = ui.screenW(), H = ui.screenH();
        barH_ = std::max(ui.s(64), ui.s(fz(ui, 20)) * 2.6f);
        if (world_) {
            RenderOptions opt;
            opt.lighting = g_.settings.graphics.dynamicLighting;
            const float alpha = std::clamp(acc_ / kSimDt, 0.0f, 1.0f);
            WorldView view = world_->camera().view(r.width(), r.height(), alpha);
            renderer_.render(r, g_.app.resources(), *world_, view, alpha, opt);
            r.setView(WorldView{});
            drawGameHud(view);
        } else {
            r.fillRect({0, 0, W, H}, Color{6, 9, 20, 255});
        }
        drawBar();
        drawOverlay();
        if (ui.backPressed()) leave();
        ui.end();
    }

private:
    /// Back to SOCIAL (or to ONLINE LEVELS to get the level): once, whatever asked first.
    void leave(bool toOnlineLevels = false) {
        if (leaving_) return;
        leaving_ = true;
        stopped_ = true;
        if (toOnlineLevels) g_.app.switchScene(makeOnlineLevels(g_, onlineLevel_), 0.4f);
        else g_.app.popScene();
    }

    // ----------------------------------------------------------------- network
    void poll() {
        if (busy_) return;
        busy_ = true;
        g_.online.get(
            "/api/v1/spectate/" + playerId_ + stream_.query(), OnlineService::guard(life_, [this](const ApiResult& r) {
                busy_ = false;
                answer(r);
            }),
            false);  // never cached: nothing of a friend's run is kept on this computer
    }

    void answer(const ApiResult& r) {
        if (!r.ok) {
            if (r.status == 403 || r.status == 404) {
                // Not (any more) a friend, the setting off, the presence hidden, blocked: the
                // same answer whatever the reason, never a guess.
                stop("Vous ne pouvez pas regarder la partie de " + name_ + ".",
                     "Seuls ses amis peuvent regarder un joueur, et seulement s'il l'autorise dans ses paramètres en ligne.");
                return;
            }
            if (r.status == 401 || !g_.online.loggedIn()) {
                stop("Connectez-vous à Cosmic Online pour regarder vos amis.", {});
                return;
            }
            pollTimer_ = 1.0f;  // offline, the server busy: again soon (« connexion perdue » after a while)
            return;
        }
        sinceAnswer_ = 0.0f;
        answered_ = true;
        std::string err;
        switch (stream_.absorb(r.data, &err)) {
            case SpectateStream::Update::Invalid:
                // A chunk out of order (the room restarted): everything again from the start.
                dropRun();
                pollTimer_ = 0.0f;
                return;
            case SpectateStream::Update::Started:
                startRun();
                break;
            default:
                break;
        }
        if (!stream_.live()) {
            if (world_ || everWatched_) endedText_ = "La partie est terminée.";
        }
        pollTimer_ = stream_.more() ? 0.0f : (stream_.live() && stream_.ended().empty()) ? kPoll : kPollIdle;
    }

    /// Something that cannot be shown: said, and nothing more is asked.
    void stop(const std::string& title, const std::string& text) {
        problem_ = title;
        problemText_ = text;
        stopped_ = true;
        dropRun();
    }

    // --------------------------------------------------------------- the run
    void dropRun() {
        sim_.reset();
        world_.reset();
        if (mounted_) g_.app.resources().popMount();
        mounted_ = false;
        g_.app.input().clearPowerSlotKeys();
        powerKeysReady_ = false;
        acc_ = 0.0f;
        playing_ = false;
        caughtUp_ = false;
    }

    /// A new run of the friend (or the same one sent again from its start): its level, here.
    void startRun() {
        dropRun();
        problem_.clear();
        problemText_.clear();
        onlineLevel_.clear();
        endedText_.clear();
        const SpectateHeader& h = *stream_.header();
        everWatched_ = true;
        if (h.simVersion != kSimulationVersion) {
            problem_ = "Votre version du jeu diffère de celle de " + name_ + ".";
            problemText_ = "Sa partie ne peut pas être rejouée à l'identique ici : mettez le jeu à jour (" + name_ + " joue en " + h.gameVersion + ").";
            return;
        }
        std::string localId;
        if (h.levelSource == "campaign") {
            const LevelFingerprint* fp = g_.online.fingerprints().get(g_.content, h.levelId);
            if (!fp) {
                problem_ = "Ce niveau n'est pas dans votre jeu.";
                problemText_ = "Mettez le jeu à jour pour regarder " + name_ + " jouer « " + h.levelTitle + " ».";
                return;
            }
            if (fp->hash != h.levelHash) {
                problem_ = "Votre version du niveau diffère de celle de " + name_ + ".";
                problemText_ = "Mettez le jeu à jour pour regarder sa partie de « " + h.levelTitle + " ».";
                return;
            }
            localId = h.levelId;
        } else {
            int installed = 0;
            for (const LevelEntry* e : g_.content.levels())
                if (const Json* c = g_.online.communityInstall(e->id); c && jstr(*c, "id") == h.levelId) {
                    installed = jget<int>(*c, "version", 0);
                    if (installed == h.levelVersion) localId = e->id;
                }
            if (localId.empty()) {
                onlineLevel_ = h.levelId;
                problem_ = installed ? "Vous n'avez pas la même version de ce niveau." : "Ce niveau de la communauté n'est pas installé.";
                problemText_ = installed ? "« " + h.levelTitle + " » : vous avez la version " + std::to_string(installed) + ", " + name_ + " joue la version " +
                                               std::to_string(h.levelVersion) + ". Mettez-le à jour dans NIVEAUX EN LIGNE pour regarder."
                                         : "Téléchargez « " + h.levelTitle + " » dans NIVEAUX EN LIGNE pour regarder " + name_ + " y jouer.";
                return;
            }
        }
        const LevelEntry* entry = g_.content.level(localId);
        std::string err;
        auto level = entry ? g_.content.loadLevel(localId, &err) : std::nullopt;
        if (!level) {
            problem_ = "Chargement du niveau impossible.";
            problemText_ = err;
            return;
        }
        level_ = *level;
        if (h.levelSource == "community" && fingerprintGameplay(g_.content, level_) != h.levelHash) {
            onlineLevel_ = h.levelId;
            problem_ = "Votre copie du niveau diffère de celle de " + name_ + ".";
            problemText_ = "Installez-le à nouveau depuis NIVEAUX EN LIGNE pour regarder sa partie.";
            return;
        }
        ResourceManager& res = g_.app.resources();
        if (!entry->assetRoot.empty()) {
            res.pushMount(entry->assetRoot);
            mounted_ = true;
        }
        // The same start as the friend's run (and as an exact replay): the level loaded, its
        // seed, then the run's seed, keys of the powers and rules (ReplaySimulator).
        WorldServices ws{res, g_.app.audio(), g_.content, nullptr, g_.settings.graphics.screenShake, 0.7f};
        world_ = std::make_unique<GameWorld>(ws, WorldMode::Play);
        world_->setKeysPerSlot(h.keysPerSlot);
        world_->load(level_);
        world_->seedGameplay(levelSeed(level_.id));
        world_->silent = true;  // until it has caught up with the friend
        sim_ = std::make_unique<ReplaySimulator>(stream_.replay(), *world_);
        renderer_.build(g_.app.renderer(), res, *world_);
        hud_ = Hud{};
    }

    /// Plays the steps received, about a second behind the last one: a late arrival (or a
    /// pause on the friend's side) catches up quickly and silently, then at the run's pace.
    void advance(float dt) {
        if (sim_->desynced()) {
            if (problem_.empty()) {
                problem_ = "La partie de " + name_ + " ne se rejoue plus à l'identique.";
                problemText_ = "Vos versions du jeu ou du niveau diffèrent : mettez le jeu à jour.";
            }
            return;
        }
        const uint32_t frames = stream_.frames();
        const bool over = !stream_.ended().empty();
        auto available = [&] { return frames - std::min(frames, sim_->tick()); };
        // Far behind (just arrived, or back from a cut): straight to a second behind live.
        if (!caughtUp_ || available() > kTarget + 3 * 120) {
            world_->silent = true;
            const uint32_t goal = over ? 0 : kTarget;
            int budget = kCatchUpBudget;
            while (budget-- > 0 && available() > goal && sim_->step()) {
            }
            if (available() <= goal + 6) {
                caughtUp_ = true;
                playing_ = over || available() >= kTarget / 2;  // a run just begun: a little ahead first
                world_->silent = false;
                acc_ = 0.0f;
                Log::info("Direct : partie de {} rattrapée au pas {} ({} reçus)", name_, sim_->tick(), frames);
            }
            world_->update(dt);
            hud_.update(*world_, dt);
            return;
        }
        // Buffering: nothing left to show before the next chunk arrives.
        if (available() == 0 && !over) playing_ = false;
        if (!playing_ && (available() >= kTarget / 2 || over)) playing_ = true;
        lagLog_ += dt;
        if (lagLog_ >= 10.0f) {
            lagLog_ = 0.0f;
            Log::info("Direct : {} pas d'avance sur l'image montrée ({} reçus)", available(), frames);
        }
        float wdt = 0.0f;
        if (playing_) {
            // A little faster or slower to stay about a second behind live.
            float speed = over ? 1.0f : available() > kTarget + kTarget / 2 ? 1.1f : available() + kTarget / 2 < kTarget ? 0.9f : 1.0f;
            acc_ += dt * speed;
            int guard = 0;
            while (acc_ >= kSimDt && guard++ < 16) {
                if (available() == 0) {
                    acc_ = 0.0f;
                    break;
                }
                acc_ -= kSimDt;
                if (!sim_->step()) break;
            }
            wdt = dt * speed;
        }
        world_->update(wdt);
        hud_.update(*world_, wdt);
    }

    bool lost() const {
        if (stopped_) return false;
        return (answered_ && sinceAnswer_ > kLostAfter) || (stream_.live() && stream_.stale());
    }
    bool showingEnd() const {
        if (!endedText_.empty()) return true;
        if (stream_.ended().empty()) return false;
        return !world_ || !sim_ || sim_->tick() >= stream_.frames();
    }

    // -------------------------------------------------------------- drawing
    /// The game's HUD of the friend's run (vitals, powers, collectibles, boss gauge) clear of
    /// the bar at the bottom; no timer (the bar has the time), no messages.
    void drawGameHud(const WorldView& view) {
        GameWorld& w = *world_;
        if (w.cinematic().active() || w.state() != WorldState::Playing) return;
        UIContext& ui = g_.ui;
        GameplaySettings hs = g_.settings.gameplay;
        hs.focusMode = false;
        hs.showTimer = false;
        hs.progressBar = 0;
        hs.showMessages = false;
        hs.focusMessages = false;
        hs.showInputHints = false;
        hs.hud[HudElement::Splits].visible = false;
        hs.hud[HudElement::Enemies].visible = false;  // 2.52: placed for the game, not for this layout
        const float W = ui.screenW(), H = ui.screenH();
        const Rect free{0, 0, W, H - barH_};
        const float m = ui.s(10);
        for (HudElement e : {HudElement::Vitals, HudElement::Powers, HudElement::Collectibles}) hs.hud[e].custom = false;
        Rect vitals = hudVisible(hs, HudElement::Vitals) ? hud_.area(ui, w, HudElement::Vitals, hud_.place(ui, w, hs, HudElement::Vitals)) : Rect{};
        Rect powers = hudVisible(hs, HudElement::Powers) ? hud_.area(ui, w, HudElement::Powers, hud_.place(ui, w, hs, HudElement::Powers)) : Rect{};
        {
            // The boss gauge above the bar, never under the vitals or the powers.
            HudPlace& hp = hs.hud[HudElement::Boss];
            float k = std::clamp(hp.scale, HudLayout::kMinScale, HudLayout::kMaxScale);
            k = std::min(k, (free.w - 2 * m) / ui.s(912));
            hp.scale = std::max(HudLayout::kMinScale, k);
            hp.custom = true;
            hp.x = free.center().x / W;
            hp.y = (free.bottom() - m - ui.s(34) * hp.scale) / H;
            Rect boss = hud_.area(ui, w, HudElement::Boss, hud_.place(ui, w, hs, HudElement::Boss));
            if (boss.x < m || boss.right() > free.right() - m || (powers.w > 0.0f && boss.intersects(powers)) || (vitals.w > 0.0f && boss.intersects(vitals)))
                setHudVisible(hs, HudElement::Boss, false);
        }
        hud_.render(ui, w, view, nullptr, hs);
    }

    /// The bar at the bottom: « EN DIRECT », the friend, the level, the time, « Retour »;
    /// every piece measured and placed after the previous one.
    void drawBar() {
        UIContext& ui = g_.ui;
        const float W = ui.screenW(), H = ui.screenH();
        const Rect bar{0, H - barH_, W, barH_};
        ui.r().fillGradientV({bar.x, bar.y - ui.s(24), bar.w, ui.s(24)}, Color{4, 7, 18, 0}, Color{4, 7, 18, 200});
        ui.r().fillRect(bar, Color{4, 7, 18, 228});
        ui.r().fillRect({bar.x, bar.y, bar.w, std::max(1.0f, ui.s(1.5f))}, ui.theme().border);
        const float m = ui.s(20), gap = ui.s(14), cy = bar.center().y;
        // « Retour » at the right.
        const float bf = fz(ui, 18);
        const float bw = std::max(ui.s(150), ui.text().measure("Retour", FontFace::Bold, ui.s(bf)).x + ui.s(40));
        const float bh = std::min(barH_ - ui.s(14), std::max(ui.s(42), ui.s(bf) * 2.1f));
        UIContext::ButtonStyle st;
        st.primary = true;
        if (ui.button("spectate/back", {W - m - bw, cy - bh * 0.5f, bw, bh}, "Retour", st)) leave();
        float right = W - m - bw - gap * 1.5f;
        // The time of the run (its chronometer), right of the texts.
        if (world_ && !problemShown()) {
            const std::string t = formatRunTime(static_cast<int64_t>(std::llround(static_cast<double>(world_->timer().elapsed()) * 1000.0)));
            const float tf = fz(ui, 22);
            const float tw = ui.text().measure(t, FontFace::Bold, ui.s(tf)).x;
            if (tw < right - m - ui.s(200)) {
                ui.label(t, {right, cy}, tf, ui.theme().text, Align::Right, FontFace::Bold, VAlign::Middle);
                right -= tw + gap * 1.5f;
            }
        }
        // The chip: what is shown now.
        float x = m;
        std::string chip;
        Color chipColor = kLive;
        if (lost()) {
            chip = "CONNEXION PERDUE";
            chipColor = ui.theme().danger;
        } else if (showingEnd()) {
            chip = "TERMINÉ";
            chipColor = Color{120, 128, 150, 255};
        } else if (world_ && !problemShown()) {
            chip = playing_ || !caughtUp_ ? "EN DIRECT" : "\x1F" "direct|EN ATTENTE";  // waiting for the next steps (« WAITING »)
            if (!playing_ && caughtUp_) chipColor = ui.theme().warm;
        }
        if (!chip.empty()) {
            const float cf = fz(ui, 15);
            const float dot = ui.s(5);
            const float cw = ui.text().measure(chip, FontFace::Bold, ui.s(cf)).x + dot * 2 + ui.s(28);
            const float ch = std::max(ui.s(28), ui.s(cf) * 1.7f);
            if (cw < right - x) {
                Rect c{x, cy - ch * 0.5f, cw, ch};
                ui.r().fillRoundedRect(c, ch * 0.5f, chipColor.withAlpha(56));
                ui.r().drawRoundedRect(c, ch * 0.5f, chipColor, ui.s(1.5f));
                const bool blink = chip != "EN DIRECT" || std::fmod(ui.time(), 1.2f) < 0.8f;
                if (blink) ui.r().fillCircle({c.x + ui.s(12) + dot, c.center().y}, dot, chipColor);
                ui.label(chip, {c.x + ui.s(18) + dot * 2, c.center().y}, cf, Color::white(), Align::Left, FontFace::Bold, VAlign::Middle);
                x = c.right() + gap;
            }
        }
        // The friend (avatar and name), then the level.
        const float av = std::min(barH_ - ui.s(18), ui.s(40));
        if (av < right - x) {
            ui_.drawAvatar(avatar_.empty() ? "avatar_01" : avatar_, playerId_, {x, cy - av * 0.5f, av, av});
            x += av + ui.s(10);
        }
        float room = right - x;
        if (room <= ui.s(40)) return;
        const std::string title = stream_.header() ? stream_.header()->levelTitle : std::string();
        const bool translateTitle = stream_.header() && stream_.header()->levelSource == "campaign";
        std::string kind;
        if (stream_.header() && !stream_.header()->challengeId.empty())
            kind = stream_.header()->challengeKind == "weekly" ? "Défi de la semaine" : stream_.header()->challengeKind == "monthly" ? "Défi du mois" : "Défi du jour";
        // The name first (at most half of the room), the level in what is left.
        const float nf = ui.fitFont(name_, FontFace::Bold, fz(ui, 20), room * 0.5f);
        Vec2 ns;
        {
            i18n::Verbatim v;  // the name as its player chose it
            ns = ui.text().measure(name_, FontFace::Bold, ui.s(nf));
            ui.label(ui.elide(name_, FontFace::Bold, nf, room * 0.5f), {x, cy}, nf, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        }
        x += std::min(ns.x, room * 0.5f) + gap;
        room = right - x;
        if (title.empty() || room <= ui.s(60)) return;
        const float lf = fz(ui, 17);
        std::string line = kind.empty() ? title : kind + " · " + title;
        if (translateTitle) {
            ui.label(ui.elide(line, FontFace::Body, lf, room), {x, cy}, lf, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        } else {
            // A community level's title as its author wrote it (the challenge's tag translated).
            float kx = x;
            if (!kind.empty()) {
                const std::string tag = kind + " · ";
                const float kw = ui.text().measure(tag, FontFace::Body, ui.s(lf)).x;
                if (kw >= room) return;
                ui.label(tag, {kx, cy}, lf, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
                kx += kw;
            }
            i18n::Verbatim v;
            ui.label(ui.elide(title, FontFace::Body, lf, right - kx), {kx, cy}, lf, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        }
    }

    bool problemShown() const { return !problem_.empty(); }

    /// In the middle: why nothing is shown (a problem, waiting, the end, a lost connection),
    /// sized to its measured text, with its buttons under it.
    void drawOverlay() {
        UIContext& ui = g_.ui;
        std::string title, text;
        Color tc = ui.theme().text;
        bool levelButton = false;
        if (problemShown()) {
            title = problem_;
            text = problemText_;
            tc = ui.theme().warm;
            levelButton = !onlineLevel_.empty() && !stopped_;
        } else if (lost()) {
            title = "Connexion perdue";
            text = stream_.stale() ? "La partie de " + name_ + " ne nous parvient plus. Elle reprendra ici si sa connexion revient."
                                   : "Le serveur ne répond plus. Nouvel essai en cours…";
            tc = ui.theme().danger;
        } else if (showingEnd()) {
            title = "La partie est terminée";
            const std::string& e = stream_.ended();
            text = !endedText_.empty() && e.empty() ? name_ + " ne joue plus."
                   : e == "finished"                ? name_ + " a terminé le niveau."
                   : e == "failed"                  ? "La partie de " + name_ + " s'est arrêtée sur un échec."
                   : e == "too_long"                ? "Cette partie est trop longue pour être suivie en direct (30 minutes au plus)."
                                                    : name_ + " a quitté la partie.";
            text += " Sa prochaine partie s'affichera ici.";
        } else if (!world_) {
            title = !answered_ ? "Connexion à la partie de " + name_ + "…" : name_ + " ne joue pas en ce moment.";
            if (answered_) text = "Sa prochaine partie s'affichera ici dès qu'elle commencera.";
        } else if (!caughtUp_) {
            title = "Chargement de la partie…";
        } else {
            return;
        }
        const float W = ui.screenW(), H = ui.screenH() - barH_;
        const float pad = ui.s(28);
        const float w = std::min(W - ui.s(80), ui.s(820));
        const float inner = w - 2 * pad;
        const float tf = ui.fitFont(title, FontFace::Bold, fz(ui, 26), inner, 0.7f);
        const Vec2 tsz = ui.text().measureWrapped(title, FontFace::Bold, ui.s(tf), inner);
        const float bf = fz(ui, 16);
        const Vec2 bsz = text.empty() ? Vec2{} : ui.text().measureWrapped(text, FontFace::Body, ui.s(bf), inner);
        const float btnH = std::max(ui.s(44), ui.s(fz(ui, 18)) * 2.1f);
        const float h = pad + tsz.y + (text.empty() ? 0.0f : ui.s(12) + bsz.y) + (levelButton ? ui.s(20) + btnH : 0.0f) + pad;
        Rect box{(W - w) * 0.5f, std::max(ui.s(20), (H - h) * 0.5f), w, h};
        ui.r().fillRoundedRect(box, ui.s(16), Color{8, 13, 30, 236});
        ui.r().drawRoundedRect(box, ui.s(16), ui.theme().border, ui.s(1.4f));
        float y = box.y + pad;
        ui.labelWrapped(title, {box.x + pad, y}, inner, tf, tc);
        y += tsz.y;
        if (!text.empty()) {
            y += ui.s(12);
            ui.labelWrapped(text, {box.x + pad, y}, inner, bf, ui.theme().textDim);
            y += bsz.y;
        }
        if (levelButton) {
            y += ui.s(20);
            const std::string label = "Ouvrir NIVEAUX EN LIGNE";
            const float lw = std::min(inner, ui.text().measure(label, FontFace::Bold, ui.s(fz(ui, 18))).x + ui.s(48));
            UIContext::ButtonStyle st;
            if (ui.button("spectate/level", {box.x + (box.w - lw) * 0.5f, y, lw, btnH}, label, st))
                leave(true);
        }
    }

    GameServices& g_;
    OnlineUi ui_;
    std::string playerId_, name_, avatar_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    SpectateStream stream_;
    bool busy_ = false, answered_ = false, stopped_ = false, everWatched_ = false, leaving_ = false;
    float pollTimer_ = 0.0f, sinceAnswer_ = 0.0f;
    std::string problem_, problemText_, onlineLevel_, endedText_;

    LevelData level_;
    std::unique_ptr<GameWorld> world_;
    std::unique_ptr<ReplaySimulator> sim_;
    GameRenderer renderer_;
    Hud hud_;
    bool mounted_ = false;
    bool playing_ = false, caughtUp_ = false;
    float acc_ = 0.0f;
    float barH_ = 64.0f;
    float lagLog_ = 0.0f;
    PowerKeyMap powerKeys_;
    bool powerKeysReady_ = false;
};

}  // namespace

std::unique_ptr<Scene> makeSpectate(GameServices& g, const std::string& playerId, const std::string& name, const std::string& avatar) {
    return std::make_unique<SpectateScene>(g, playerId, name, avatar);
}

}  // namespace cosmic
