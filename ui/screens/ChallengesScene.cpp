// Main menu → « Défis » (2.32 and 2.33, docs/online/DAILY_CHALLENGES.md and
// CHALLENGES_PERIODS.md): the challenges of the day, of the week and of the month (« Jour »,
// « Semaine », « Mois »: level, countdown, rules, tiers, my best time and its state, the streak
// of that kind, Jouer / Revoir mon meilleur replay / S'entraîner, the leaderboard) and the
// « Calendrier » (a month, Monday to Sunday, with each day's and each week's challenge and the
// month's one at the head; a cell opens that challenge's card, where a challenge that is over
// has its leaderboard « Pendant le défi » / « Depuis » and « Jouer (hors délai) »); before a
// run, what changes, the tiers, the ghost and the connection; after it, the time, the tier,
// the streak and the sending state. A challenge stays playable offline until its end once
// downloaded; without an account the results stay on this computer.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Log.h"
#include "game/online/Clans.h"
#include "game/GameServices.h"
#include "game/online/LevelFingerprint.h"
#include "packaging/PackageImporter.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/ChallengeRewardsUi.h"
#include "ui/screens/ChallengeUi.h"
#include "ui/screens/GameScene.h"
#include "ui/screens/LevelPowerKeysScene.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/ReplayDownloadUi.h"
#include "ui/screens/Thumbnail.h"
#include "ui/widgets/Charts.h"

namespace cosmic {

namespace {

using namespace challenge_ui;

Color stateColor(const UITheme& th, ChallengeRunState s) {
    switch (s) {
        case ChallengeRunState::Verified: return th.success;
        case ChallengeRunState::Rejected: return th.danger;
        case ChallengeRunState::Pending:
        case ChallengeRunState::Sent: return th.accent;
        case ChallengeRunState::Waiting: return th.warm;
        case ChallengeRunState::LocalOnly: break;
    }
    return th.textDim;
}

/// "3 j 05 h", "5 h 12 min", "12 min 05 s", "45 s".
std::string countdown(int64_t ms) {
    int64_t s = std::max<int64_t>(0, ms / 1000);
    char b[48];
    if (s >= 86400) std::snprintf(b, sizeof b, "%lld j %02lld h", static_cast<long long>(s / 86400), static_cast<long long>((s / 3600) % 24));
    else if (s >= 3600) std::snprintf(b, sizeof b, "%lld h %02lld min", static_cast<long long>(s / 3600), static_cast<long long>((s / 60) % 60));
    else if (s >= 60) std::snprintf(b, sizeof b, "%lld min %02lld s", static_cast<long long>(s / 60), static_cast<long long>(s % 60));
    else std::snprintf(b, sizeof b, "%lld s", static_cast<long long>(s));
    return b;
}

/// "Défi du 16 novembre", "Défi de la semaine du 16 novembre", "Défi du mois de novembre 2026".
std::string challengeTitle(const DailyChallenge& c, bool year = false) {
    switch (c.kind) {
        case ChallengeKind::Weekly: return "Défi de la semaine du " + frenchDate(c.date, year);
        case ChallengeKind::Monthly: return "Défi du mois de " + frenchMonth(c.period);
        case ChallengeKind::Daily: break;
    }
    return "Défi du " + frenchDate(c.date, year);
}

/// "Classement du jour", "… de la semaine", "… du mois".
const char* boardTitle(ChallengeKind k) {
    return k == ChallengeKind::Weekly ? "Classement de la semaine" : k == ChallengeKind::Monthly ? "Classement du mois" : "Classement du jour";
}

/// "Série : 3 jours" / "… 1 semaine" / "… 2 mois".
std::string streakCount(ChallengeKind k, int n) {
    if (k == ChallengeKind::Weekly) return std::to_string(n) + (n > 1 ? " semaines" : " semaine");
    if (k == ChallengeKind::Monthly) return std::to_string(n) + " mois";
    return std::to_string(n) + (n > 1 ? " jours" : " jour");
}

/// "Le 1er du jour" (the first of the board, as a ghost).
const char* firstOfBoard(ChallengeKind k) {
    return k == ChallengeKind::Weekly ? "Le 1er de la semaine" : k == ChallengeKind::Monthly ? "Le 1er du mois" : "Le 1er du jour";
}

/// The screen of the « Défis » menu that shows a challenge: its kind's tab while it runs, else
/// the calendar on it.
int challengeTab(const ChallengeService& s, const DailyChallenge& c) { return s.ended(c) ? 3 : static_cast<int>(c.kind); }

/// The rules in short (chips).
std::vector<std::string> ruleChips(GameServices& g, const DailyChallenge& c) {
    const ChallengeRules& r = c.rules;
    std::vector<std::string> out;
    if (c.finishObjective) out.push_back("Objectif : finir le niveau");
    out.push_back(std::to_string(r.hearts) + (r.hearts > 1 ? " cœurs" : " cœur"));
    out.push_back(std::to_string(r.slots) + (r.slots > 1 ? " emplacements" : " emplacement"));
    out.push_back(r.powers.empty() ? std::string("Aucun pouvoir au départ") : "Pouvoirs : " + powerList(g, r.powers));
    out.push_back(r.stations ? "Stations actives" : "Stations éteintes");
    out.push_back(r.checkpoints ? "Points de passage" : "Sans points de passage");
    return out;
}

/// Each rule explained (« Ce qui change aujourd'hui »), against the level as installed.
std::vector<std::pair<std::string, std::string>> ruleExplanations(GameServices& g, const DailyChallenge& c, const LevelData* level) {
    const ChallengeRules& r = c.rules;
    std::vector<std::pair<std::string, std::string>> out;
    // 2.41: « Finir le niveau »: ranked by how far the run goes, its tiers a share of the route.
    if (c.finishObjective)
        out.push_back({"Objectif", c.goldNoDamage ? "Finir le niveau : le classement suit la progression (le plus loin d'abord, puis le premier à finir). Or en finissant sans dégât."
                                                  : "Finir le niveau : le classement suit la progression (le plus loin d'abord, puis le premier à finir). Or en finissant."});
    std::string hearts = std::to_string(r.hearts) + (r.hearts > 1 ? " cœurs" : " cœur");
    out.push_back({"Cœurs", hearts + " pour tout le niveau, pleins au départ ; les cœurs de cristal du niveau ne donnent rien."});
    // Whole sentences (one template each for the translation).
    std::string slots;
    if (level && level->powerSlots() > 0 && level->powerSlots() != r.slots)
        slots = r.slots > 1 ? std::to_string(r.slots) + " emplacements de pouvoirs (le niveau en a " + std::to_string(level->powerSlots()) + ")."
                            : std::to_string(r.slots) + " emplacement de pouvoir (le niveau en a " + std::to_string(level->powerSlots()) + ").";
    else
        slots = r.slots > 1 ? std::to_string(r.slots) + " emplacements de pouvoirs." : std::to_string(r.slots) + " emplacement de pouvoir.";
    out.push_back({"Emplacements", slots});
    std::string powers = r.powers.empty() ? std::string("Vous partez sans aucun pouvoir.")
                                          : "Vous partez avec : " + powerList(g, r.powers) + " (à la place des pouvoirs de départ du niveau).";
    out.push_back({"Pouvoirs", powers});
    out.push_back({"Stations", r.stations ? "Les stations de pouvoirs du niveau fonctionnent comme d'habitude."
                                          : "Les stations de pouvoirs du niveau sont éteintes : elles n'offrent rien."});
    out.push_back({"Points de passage", r.checkpoints ? "Les points de passage fonctionnent comme d'habitude."
                                                      : "Aucun point de passage : une chute ramène au départ du niveau, cœurs pleins et pouvoirs du défi."});
    return out;
}

/// The three tiers (Bronze, Argent, Or) side by side in `area`, `got` the best one obtained.
void drawTierCards(UIContext& ui, const DailyChallenge& c, const Rect& area, ChallengeTier got, bool showObtained) {
    const float gap = ui.s(12);
    const float w = (area.w - 2 * gap) / 3.0f;
    const ChallengeTier tiers[3] = {ChallengeTier::Bronze, ChallengeTier::Silver, ChallengeTier::Gold};
    for (int i = 0; i < 3; ++i) {
        ChallengeTier t = tiers[i];
        Rect r{area.x + (w + gap) * static_cast<float>(i), area.y, w, area.h};
        bool obtained = showObtained && got >= t;
        Color col = tierColor(t);
        ui.r().fillRoundedRect(r, ui.s(10), obtained ? col.withAlphaF(0.16f) : Color{12, 18, 38, 230});
        ui.r().drawRoundedRect(r, ui.s(10), col.withAlphaF(obtained ? 0.95f : 0.45f), std::max(1.0f, ui.s(obtained ? 2.0f : 1.2f)));
        float x = r.x + ui.s(14), tw = r.w - ui.s(28);
        std::string cond = tierCondition(c, t);
        // Name, condition and state one under the other, sized to the card's height.
        float rowH = (r.h - ui.s(16)) / 3.0f;
        float y = r.y + ui.s(8) + rowH * 0.5f;
        fitLabel(ui, challengeTierLabel(t), {x, y}, 20, tw, col, Align::Left, FontFace::Bold);
        y += rowH;
        fitLabel(ui, cond, {x, y}, 15, tw, ui.theme().text, Align::Left, FontFace::Body, VAlign::Middle, 0.7f);
        y += rowH;
        if (showObtained)
            fitLabel(ui, obtained ? "Obtenu" : "Pas encore", {x, y}, 14, tw, obtained ? col : ui.theme().textDim, Align::Left, FontFace::Bold);
    }
}

/// Height of the « Récompenses » section (its title, then its tiles `tileH` high).
float rewardsSectionHeight(const UIContext& ui, float tileH) { return ui.s(32) + tileH; }

/// « Récompenses » (2.35): the title (and, when it fits, what counts at its right), then the
/// tiles of the rewards of the challenges a run can advance (ChallengeRewardsUi.h), from `y`.
/// Returns the reward activated (it opens in the Collection).
const CosmeticItem* drawRewardsSection(GameServices& g, ChallengeRewardRow& row, const std::vector<const CosmeticItem*>& items, float x, float y,
                                       float w, float titleSize, float tileH) {
    UIContext& ui = g.ui;
    Vec2 ts = fitLabel(ui, "Récompenses", {x, y + ui.s(11)}, titleSize, w, ui.theme().accent, Align::Left, FontFace::Bold);
    const CosmeticItem* hit = row.draw(g, "reward_", items, x, y + ui.s(32), tileH, w, "challenges.rewards");
    float after = x + ts.x + ui.s(14);
    // « NOUVEAU » (2.35) after the title, until a reward gets the focus.
    if (row.newDue()) after = ui.drawNewPill(after + ui.newPillWidth(true), y + ui.s(11), true).right() + ui.s(14);
    const std::string caption = "Parties vérifiées par Cosmic Online";
    float cf = fz(ui, 14);
    if (ui.text().measure(caption, FontFace::Body, ui.s(cf)).x <= x + w - after)
        ui.label(caption, {x + w, y + ui.s(11)}, cf, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
    return hit;
}

/// Chips laid out from `x`, wrapping in `w`; returns the height used.
float drawChipRows(UIContext& ui, const std::vector<std::string>& chips, float x, float y, float w, float h, Color c) {
    float cx = x, cy = y;
    const float gap = ui.s(8);
    for (const std::string& t : chips) {
        float cw = std::min(w, chipWidth(ui, t, h));
        if (cx > x && cx + cw > x + w) {
            cx = x;
            cy += h + gap;
        }
        chip(ui, t, cx, cy + h * 0.5f, h, c, w);
        cx += cw + gap;
    }
    return cy + h - y;
}

/// The height drawChipRows would use.
float chipRowsHeight(UIContext& ui, const std::vector<std::string>& chips, float w, float h) {
    float cx = 0.0f, rows = 1.0f;
    const float gap = ui.s(8);
    for (const std::string& t : chips) {
        float cw = std::min(w, chipWidth(ui, t, h));
        if (cx > 0.0f && cx + cw > w) {
            cx = 0.0f;
            rows += 1.0f;
        }
        cx += cw + gap;
    }
    return rows * h + (rows - 1.0f) * gap;
}

/// Downloads and installs the community level of a challenge (its frozen version) through the
/// same checks as ONLINE LEVELS. `done(ok, message)` on the main thread.
void downloadChallengeLevel(GameServices& g, const DailyChallenge& c, std::weak_ptr<int> life, std::function<void(bool, const std::string&)> done) {
    std::string id = c.level.levelId, title = c.level.title, hash = c.level.hash;
    int version = c.level.version;
    g.online.getBinary(
        "/api/v1/community/levels/" + id + "/package?version=" + std::to_string(version),
        [&g, id, title, hash, version, life, done](const HttpResponse& r) {
            if (!life.lock()) return;
            if (r.status != 200)
                return done(false, r.networkFailure() ? std::string("Connexion impossible : le niveau sera téléchargé plus tard.")
                                                      : "Téléchargement refusé (" + std::to_string(r.status) + ").");
            fs::path dir = Paths::userDir() / "online" / "downloads";
            std::error_code ec;
            fs::create_directories(dir, ec);
            fs::path file = dir / (id + "-v" + std::to_string(version) + ".cplanet");
            if (!FileUtil::writeBinary(file, std::vector<uint8_t>(r.body.begin(), r.body.end()))) return done(false, "Écriture impossible.");
            PackageImporter imp(g.content);
            PackageInspection insp = imp.inspect(file);
            if (!insp.valid) return done(false, "Package refusé : " + (insp.errors.empty() ? std::string("invalide") : insp.errors.front()));
            std::string installed, err;
            if (!imp.install(insp, ConflictPolicy::Replace, &installed, &err)) return done(false, "Installation impossible : " + err);
            g.content.reload();
            g.ratings.contentChanged();
            auto lvl = g.content.loadLevel(installed);
            if (!lvl) return done(false, "Niveau installé illisible.");
            std::string serverHash = r.header("x-level-hash");
            std::string local = fingerprintGameplay(g.content, *lvl);
            bool verified = !serverHash.empty() && local == serverHash;
            g.online.noteCommunityInstall(installed, {{"id", id}, {"version", version}, {"level_hash", serverHash.empty() ? local : serverHash},
                                                      {"title", title}, {"verified", verified}});
            if (local != hash) {
                Log::warn("Défi : niveau {} v{} installé, empreinte {} au lieu de {} (défi) / {} (serveur)", id, version, local, hash, serverHash);
                return done(true, "Installé, mais cette copie diffère de la version du défi : vos temps n'y seront pas envoyés.");
            }
            done(true, "Niveau installé : il reste jouable hors ligne.");
        },
        16u << 20);
}

/// Connection line of the screens (what happens to a run's result).
std::string connectionLine(GameServices& g, bool practice, bool late, const LevelPlace& place, bool finish = false) {
    if (practice) return "Hors classement : rien n'est envoyé ni gardé comme meilleur temps.";
    if (!place.localId.empty() && !place.sameVersion) return "Votre version du niveau diffère de celle du défi : temps gardé sur ce PC.";
    if (!g.online.loggedIn()) return "Sans compte : votre résultat restera sur ce PC (ni classement ni envoi).";
    if (late) return "Hors délai : votre temps ira dans le classement « Depuis », sans compter pour la série.";
    // 2.41, « Finir le niveau »: how far the run went counts too, not only a finish.
    if (g.online.state() == OnlineState::Online)
        return finish ? "Connecté : votre progression sera envoyée à la fin de la partie." : "Connecté : votre temps sera envoyé à la fin du défi.";
    return finish ? "Hors ligne : votre progression partira à la prochaine connexion." : "Hors ligne : votre temps partira à la prochaine connexion.";
}

// ======================================================================== the Défis screen

class ChallengesScene : public Scene {
public:
    ChallengesScene(GameServices& g, int tab, std::string select) : g_(g), ou_(g), tab_(std::clamp(tab, 0, 3)), art_(g) {
        // The calendar on a month ("2026-10") or on a challenge's card (its id).
        if (tab_ == 3 && !select.empty()) {
            if (challenge_period::valid(ChallengeKind::Monthly, select)) month_ = select;
            else detail_ = select;
        }
    }

    void onEnter() override {
        bg_.init(g_);
        g_.playMusic(g_.menuMusic());
        g_.challenges.refresh(true);
        if (g_.online.loggedIn()) g_.challenges.refreshMine();
        if (const DailyChallenge* c = detail_.empty() ? nullptr : g_.challenges.find(detail_); c && month_.empty()) month_ = c->date.substr(0, 7);
        if (month_.empty()) month_ = ChallengeService::utcMonth(g_.challenges.nowMs());
        if (tab_ == 3) {
            g_.challenges.loadCalendar(month_);
            g_.ui.setFocus(detail_.empty() ? "chal/d" + ChallengeService::utcDate(g_.challenges.nowMs()) : "chal/play");
        } else {
            g_.ui.setFocus("chal/play");
        }
    }
    void onResume() override {
        g_.playMusic(g_.menuMusic());
        boardFor_.clear();  // a run may have changed it
        if (tab_ == 3) g_.challenges.loadCalendar(month_, true);
    }

    void update(float dt) override {
        bg_.update(dt);
        t_ += dt;
        if (msgTimer_ > 0.0f) msgTimer_ -= dt;
        // The leaderboard of the challenge shown (logged in only), again from time to time.
        const DailyChallenge* c = shown();
        boardAge_ += dt;
        if (c && g_.online.loggedIn() && !boardBusy_ && (boardFor_ != boardKey(*c) || boardAge_ > 60.0f)) fetchBoard(*c);
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.72f);
        if (!pendingFocus_.empty()) {
            ui.setFocus(pendingFocus_);
            pendingFocus_.clear();
        }
        ui.pushId("chal");
        const float W = ui.screenW(), H = ui.screenH(), m = ui.s(40);
        const float y0 = ui.s(26);
        // Header: title, the four tabs, the connection, Retour.
        float tf = 32.0f;
        Vec2 ts = ui.label("DÉFIS", {m, y0 + ui.s(24)}, tf, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        float x = m + ts.x + ui.s(28);
        static const char* const kTabs[4] = {"Jour", "Semaine", "Mois", "Calendrier"};
        static const char* const kTabFeatures[4] = {"challenges.tab.today", "challenges.tab.week", "challenges.tab.month", "challenges.tab.calendar"};
        for (int i = 0; i < 4; ++i) {
            // Never narrower than its « NOUVEAU » pill: two pills side by side never touch.
            float w = std::max(ui.text().measure(kTabs[i], FontFace::Bold, ui.s(18)).x + ui.s(40), ui.newPillWidth(true) + ui.s(18));
            UIContext::ButtonStyle st;
            st.small = true;
            st.selected = tab_ == i;
            st.fontSize = 18;
            Rect tr{x, y0 + ui.s(4), w, ui.s(42)};
            if (ui.button("tab" + std::to_string(i), tr, kTabs[i], st) && tab_ != i) switchTab(i);
            ui.newBadge("tab" + std::to_string(i), tr, kTabFeatures[i], true, true);
            x += w + ui.s(10);
        }
        if (ui.input().menuPressed(Action::MenuTabNext)) switchTab((tab_ + 1) % 4);
        else if (ui.input().menuPressed(Action::MenuTabPrev)) switchTab((tab_ + 3) % 4);
        UIContext::ButtonStyle bs;
        bs.small = true;
        Rect back{W - m - ui.s(150), y0 + ui.s(4), ui.s(150), ui.s(42)};
        bool leave = ui.button("back", back, "Retour", bs);
        drawConnection({x + ui.s(16), y0 + ui.s(25)}, back.x - ui.s(20) - (x + ui.s(16)));
        Rect body{m, ui.s(92), W - 2 * m, H - ui.s(92) - ui.s(26)};
        rewardsShown_ = false;
        if (tab_ < 3) {
            drawKind(static_cast<ChallengeKind>(tab_), body);
        } else if (const DailyChallenge* c = detail_.empty() ? nullptr : g_.challenges.find(detail_)) {
            drawCardAndBoard(*c, body, true);
        } else {
            detail_.clear();
            drawCalendar(body);
            // 2.60: the medals' tooltip, over the other cells.
            charts::drawTip(ui, medalTip_);
        }
        // The tooltip of a reward, over the card and the leaderboard; of a download icon.
        if (rewardsShown_) rewardRow_.drawTooltip(g_);
        ReplayDownloadUi::drawTip(g_);
        ui.popId();
        // Back: from a challenge's card to the calendar, else to the main menu.
        if (!leave && ui.backPressed() && tab_ == 3 && !detail_.empty()) closeDetail();
        else if (leave || ui.backPressed()) g_.app.switchScene(makeMainMenu(g_), 0.3f);
        ui.end();
    }

private:
    const char* scopeId() const { return scope_ == 0 ? "world" : scope_ == 1 ? "country" : "friends"; }

    /// The challenge whose card is on screen (nullptr: none, or the calendar's grid).
    const DailyChallenge* shown() const {
        if (tab_ < 3) return g_.challenges.current(static_cast<ChallengeKind>(tab_));
        return detail_.empty() ? nullptr : g_.challenges.find(detail_);
    }
    /// « Depuis » (period=all) only for a challenge that is over.
    bool allPeriod(const DailyChallenge& c) const { return period_ == 1 && g_.challenges.ended(c); }
    /// « Plus loin » (board=progress) only for a challenge whose level has a route; a « Finir le
    /// niveau » challenge (2.41) is ranked by it first (its first button).
    bool progressBoard(const DailyChallenge& c) const {
        if (!c.progressAvailable) return false;
        return c.finishObjective ? boardKind_ == 0 : boardKind_ == 1;
    }
    std::string boardKey(const DailyChallenge& c) const {
        return c.id + "/" + scopeId() + (allPeriod(c) ? "/all" : "/live") + (progressBoard(c) ? "/progress" : "/time");
    }

    void switchTab(int t) {
        tab_ = t;
        if (tab_ == 3) g_.challenges.loadCalendar(month_);
    }

    void openDetail(const std::string& id, const std::string& from) {
        detail_ = id;
        detailFrom_ = from;
        pendingFocus_ = "chal/play";
    }
    void closeDetail() {
        detail_.clear();
        if (!detailFrom_.empty()) pendingFocus_ = "chal/" + detailFrom_;
    }
    void changeMonth(int delta) {
        std::string m = challenge_period::addMonths(month_, delta);
        if (m.empty()) return;
        month_ = m;
        g_.challenges.loadCalendar(month_);
    }

    void say(const std::string& m) {
        msg_ = m;
        msgTimer_ = 5.0f;
    }

    void drawConnection(Vec2 leftMid, float room) {
        UIContext& ui = g_.ui;
        if (room < ui.s(140)) return;
        OnlineState st = g_.online.state();
        Color c = st == OnlineState::Online ? ui.theme().success : st == OnlineState::Offline ? ui.theme().danger : ui.theme().textDim;
        std::string text;
        if (!g_.online.usable()) text = std::string("Cosmic Online : ") + onlineStateLabel(st);
        else if (!g_.online.loggedIn()) text = std::string(onlineStateLabel(st)) + " · sans compte";
        else text = std::string(onlineStateLabel(st)) + " · ";
        ui.r().fillCircle({leftMid.x + ui.s(6), leftMid.y}, ui.s(5), c);
        float tx = leftMid.x + ui.s(20), troom = room - ui.s(20);
        Vec2 s = fitLabel(ui, text, {tx, leftMid.y}, 16, troom, ui.theme().textDim);
        if (g_.online.loggedIn() && s.x < troom - ui.s(20)) {
            i18n::Verbatim name;  // the pseudonym as its player chose it
            fitLabel(ui, g_.online.username(), {tx + s.x, leftMid.y}, 16, troom - s.x, ui.theme().text, Align::Left, FontFace::Bold);
        }
    }

    // ------------------------------------------------------------ Jour, Semaine, Mois

    void drawKind(ChallengeKind kind, const Rect& body) {
        const DailyChallenge* c = g_.challenges.current(kind);
        if (!c) {
            drawNoChallenge(kind, body);
            return;
        }
        drawCardAndBoard(*c, body, false);
    }

    void drawCardAndBoard(const DailyChallenge& c, const Rect& body, bool fromCalendar) {
        UIContext& ui = g_.ui;
        const float gap = ui.s(22);
        Rect left{body.x, body.y, (body.w - gap) * 0.57f, body.h};
        Rect right{left.right() + gap, body.y, body.right() - left.right() - gap, body.h};
        drawChallengeCard(c, left, fromCalendar);
        drawBoard(c, right);
    }

    void drawNoChallenge(ChallengeKind kind, const Rect& body) {
        UIContext& ui = g_.ui;
        Rect p = Rect::fromCenter(body.center(), {std::min(ui.s(900), body.w), std::min(ui.s(330), body.h)});
        ui.panel(p, true, 16);
        float x = p.x + ui.s(36), w = p.w - ui.s(72), y = p.y + ui.s(34);
        auto fs = g_.challenges.fetchState();
        std::string head = fs == ChallengeService::Fetch::Loading ? "Recherche du défi…"
                           : kind == ChallengeKind::Weekly        ? "Aucun défi de la semaine sur ce PC"
                           : kind == ChallengeKind::Monthly       ? "Aucun défi du mois sur ce PC"
                                                                  : "Aucun défi du jour sur ce PC";
        fitLabel(ui, head, {x, y + ui.s(16)}, 28, w, ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(58);
        std::string text;
        if (!g_.online.usable())
            text = "Cosmic Online est indisponible dans cette version ou désactivé dans les paramètres : les défis viennent du serveur.";
        else if (fs == ChallengeService::Fetch::Loading)
            text = "Les défis sont demandés au serveur.";
        else if (fs == ChallengeService::Fetch::Offline)
            text = "Hors ligne : connectez-vous à Internet pour recevoir les défis. Une fois téléchargé, un défi reste jouable hors ligne jusqu'à sa fin.";
        else if (fs == ChallengeService::Fetch::Failed)
            text = "Le serveur n'a pas donné de défi : " + g_.challenges.fetchMessage();
        else if (kind != ChallengeKind::Daily && g_.challenges.legacyServer())
            text = "Ce serveur Cosmic Online ne propose encore que le défi du jour.";
        else
            text = "Pas de défi de cette sorte en ce moment. Les défis viennent du serveur ; une fois téléchargé, un défi reste jouable hors ligne jusqu'à sa fin.";
        Vec2 s = ui.labelWrapped(text, {x, y}, w, fz(ui, 18), ui.theme().textDim);
        y += s.y + ui.s(24);
        UIContext::ButtonStyle st;
        st.primary = true;
        st.enabled = g_.online.usable() && fs != ChallengeService::Fetch::Loading;
        float bw = std::min(w, ui.s(320));
        float by = std::min(y, p.bottom() - ui.s(84));
        if (ui.button("play", {x, by, bw, ui.s(56)}, "Réessayer", st)) g_.challenges.refresh(true);
        float ax = x + bw + ui.s(16);
        if (ax + ui.s(200) < p.right() - ui.s(36))
            if (ui.button("tocalendar", {ax, by, std::min(ui.s(300), p.right() - ui.s(36) - ax), ui.s(56)}, "Voir le calendrier")) switchTab(3);
    }

    /// The level: its name, its planet (campaign: and its description) or its author
    /// (community), its preview at the right, `blockH` high at most. Returns the height used.
    float drawLevelHeader(const DailyChallenge& c, const LevelPlace& place, float x, float y, float w, float blockH) {
        UIContext& ui = g_.ui;
        float prevW = std::min(blockH * 16.0f / 9.0f, w * 0.5f);
        float prevH = prevW * 9.0f / 16.0f;
        bool preview = blockH >= ui.s(110) && prevW >= ui.s(180);
        float tw = preview ? w - prevW - ui.s(22) : w;
        if (preview) {
            Rect pr{x + w - prevW, y, prevW, prevH};
            ui.r().fillRoundedRect(pr, ui.s(10), Color{11, 18, 38, 255});
            if (TexturePtr t = thumbnail(c, place)) ui.r().drawTexture(*t, nullptr, pr.expanded(-ui.s(3)), Color::white());
            else fitLabel(ui, place.localId.empty() ? "Niveau à télécharger" : "Aperçu indisponible", pr.center(), 15, pr.w - ui.s(16), ui.theme().textDim, Align::Center);
            ui.r().drawRoundedRect(pr, ui.s(10), ui.theme().accent.withAlpha(90), std::max(1.0f, ui.s(1.5f)));
        }
        float ty = y;
        {
            std::string title = levelTitle(g_, c);
            std::optional<i18n::Verbatim> verbatim;
            if (c.community()) verbatim.emplace();  // a player's level: as its author named it
            fitLabel(ui, title, {x, ty + ui.s(20)}, 32, tw, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle, 0.6f);
        }
        ty += ui.s(46);
        if (c.community()) {
            Vec2 s = ui.label("par ", {x, ty + ui.s(12)}, fz(ui, 18), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            {
                i18n::Verbatim author;
                fitLabel(ui, c.level.authorName.empty() ? std::string("?") : c.level.authorName, {x + s.x, ty + ui.s(12)}, 18, tw - s.x,
                         ui.theme().text, Align::Left, FontFace::Bold);
            }
            ty += ui.s(30);
            fitLabel(ui, "Niveau de la communauté, version " + std::to_string(c.level.version), {x, ty + ui.s(12)}, 16, tw, ui.theme().textDim);
        } else {
            fitLabel(ui, planetLine(g_, c), {x, ty + ui.s(12)}, 18, tw, ui.theme().textDim);
            // The level's own description, in the room left of the preview (whole lines only;
            // too long even smaller: left out rather than cut).
            const LevelEntry* e = g_.content.level(c.level.levelId);
            float room = (preview ? y + prevH : ty + ui.s(36)) - (ty + ui.s(40));
            if (e && !e->description.empty() && room > ui.s(40))
                for (float size : {16.0f, 14.0f}) {
                    float fs = fz(ui, size);
                    if (ui.text().measureWrapped(e->description, FontFace::Body, ui.s(fs), tw).y > room) continue;
                    ui.labelWrapped(e->description, {x, ty + ui.s(40)}, tw, fs, Color{185, 200, 218, 255});
                    break;
                }
        }
        return std::max(preview ? prevH : 0.0f, ui.s(c.community() ? 108 : 78));
    }

    void drawChallengeCard(const DailyChallenge& c, const Rect& area, bool fromCalendar) {
        UIContext& ui = g_.ui;
        ui.panel(area, false, 16);
        const float x = area.x + ui.s(28), w = area.w - ui.s(56);
        float y = area.y + ui.s(24);
        LevelPlace place = levelPlace(g_, c);
        const bool over = g_.challenges.ended(c);
        // Line 1: (from the calendar: back to it), the challenge, its countdown (or « Terminé »)
        // in a pill at the right.
        float ph = ui.s(32), lx = x;
        if (fromCalendar) {
            const std::string bl = "Calendrier";
            float bw = ui.text().measure(bl, FontFace::Bold, ui.s(16)).x + ui.s(36);
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = 16;
            if (ui.button("tocal", {x, y - ui.s(4), bw, ph + ui.s(8)}, bl, st)) closeDetail();
            lx = x + bw + ui.s(16);
        }
        std::string left = challengeTitle(c, over);
        std::string cd = over ? std::string("Terminé") : "Se termine dans " + countdown(c.endsAtMs - g_.challenges.nowMs());
        Color cdc = over ? ui.theme().textDim : ui.theme().warm;
        float pw = chipWidth(ui, cd, ph);
        float leftRoom = x + w - pw - ui.s(16) - lx;
        if (leftRoom < ui.s(120)) {
            // Narrow: the countdown under the title.
            fitLabel(ui, left, {lx, y + ph * 0.5f}, 17, x + w - lx, ui.theme().accent, Align::Left, FontFace::Bold);
            y += ph + ui.s(8);
            chip(ui, cd, x, y + ph * 0.5f, ph, cdc, w);
        } else {
            fitLabel(ui, left, {lx, y + ph * 0.5f}, 17, leftRoom, ui.theme().accent, Align::Left, FontFace::Bold);
            chip(ui, cd, x + w - pw, y + ph * 0.5f, ph, cdc, pw);
        }
        y += ph + ui.s(14);
        // Below the level, everything has its height: the level's preview takes what is left.
        const float by = area.bottom() - ui.s(24) - ui.s(58);
        const std::vector<std::string> chips = ruleChips(g_, c);
        const float chipH = ui.s(32);
        // 2.35: the rewards of the challenges a run of this one can advance (a run « hors
        // délai » never counts for a series).
        const std::vector<const CosmeticItem*> rewards = challengeRewardsFor(g_.cosmetics, c.kindId(), over);
        // 2.43: and first, this challenge's own rewards (an administrator's), before and after it.
        rewardRow_.setPrizes(g_, &c);
        const bool anyReward = !rewards.empty() || rewardRow_.hasPrizes();
        const float rewardTileH = ui.s(76);
        const float rewardsH = anyReward ? rewardsSectionHeight(ui, rewardTileH) + ui.s(16) : 0.0f;
        const float below = ui.s(30) + chipRowsHeight(ui, chips, w, chipH) + ui.s(18)  // rules
                            + ui.s(30) + ui.s(96) + ui.s(18)                           // tiers
                            + rewardsH                                                  // rewards
                            + ui.s(84) + ui.s(36);                                      // my best, note
        y += drawLevelHeader(c, place, x, y, w, std::clamp(by - y - below, ui.s(84), ui.s(330))) + ui.s(16);
        // The rules (chips).
        fitLabel(ui, "Règles du défi", {x, y + ui.s(11)}, 18, w, ui.theme().accent, Align::Left, FontFace::Bold);
        y += ui.s(30);
        y += drawChipRows(ui, chips, x, y, w, chipH, ui.theme().accent) + ui.s(18);
        // The tiers.
        const ChallengeResult* res = g_.challenges.result(c.id);
        fitLabel(ui, "Paliers", {x, y + ui.s(11)}, 18, w, ui.theme().accent, Align::Left, FontFace::Bold);
        y += ui.s(30);
        drawTierCards(ui, c, {x, y, w, ui.s(96)}, myDay(c.id).tier, true);
        y += ui.s(96) + ui.s(18);
        // The rewards (tiles with their tooltip, drawn over everything at the end of the frame).
        if (anyReward) {
            if (const CosmeticItem* it = drawRewardsSection(g_, rewardRow_, rewards, x, y, w, 18, rewardTileH))
                g_.app.pushScene(makeCollectionOn(g_, it->id));
            rewardsShown_ = true;
            y += rewardsH;
        }
        // My best time, its state, the streak of this kind.
        Rect box{x, y, w, std::max(ui.s(60), std::min(ui.s(84), by - ui.s(16) - y))};
        drawMyBest(c, res, box);
        y = box.bottom() + ui.s(10);
        // A note (the level to download, another version, the end, a message) above the buttons.
        std::string note = msgTimer_ > 0.0f ? msg_ : place.problem;
        if (note.empty() && !place.localId.empty() && !place.sameVersion)
            note = over ? "Le niveau a changé depuis ce défi : « S'entraîner » seulement, hors classement."
                        : "Votre version du niveau diffère de celle du défi : vos temps resteront sur ce PC.";
        if (note.empty() && over) note = "Défi terminé : une partie maintenant est hors délai (classement « Depuis », sans la série).";
        if (!note.empty() && by - y > ui.s(18)) fitLabel(ui, note, {x, (y + by) * 0.5f}, 16, w, ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle, 0.7f);
        // Jouer (or Télécharger, or Jouer hors délai), Revoir mon meilleur replay, S'entraîner:
        // widths after their labels, which share one size.
        const float gap = ui.s(14);
        UIContext::ButtonStyle primary;
        primary.primary = true;
        bool needDownload = c.community() && place.localId.empty();
        primary.enabled = needDownload ? (g_.online.usable() && !downloading_) : place.problem.empty() && (!over || place.sameVersion);
        const std::string playLabel = needDownload ? (downloading_ ? "Téléchargement…" : "Télécharger") : over ? "Jouer (hors délai)" : "Jouer";
        const std::string bestLabel = "Revoir mon meilleur replay", practiceLabel = "S'entraîner (hors classement)";
        const std::string* labels[3] = {&playLabel, &bestLabel, &practiceLabel};
        float tws[3], sum = 0.0f;
        for (int i = 0; i < 3; ++i) sum += tws[i] = ui.text().measure(*labels[i], FontFace::Bold, ui.s(26)).x + ui.s(40);
        // Each label's width, the room left shared equally (too narrow: each in proportion).
        float bws[3];
        const float room = w - 2 * gap;
        for (int i = 0; i < 3; ++i) bws[i] = sum <= room ? tws[i] + (room - sum) / 3.0f : room * tws[i] / sum;
        float fs = 26.0f;
        for (int i = 0; i < 3; ++i) fs = std::min(fs, ui.fitFont(*labels[i], FontFace::Bold, 26.0f, bws[i] - ui.s(28)));
        primary.fontSize = fs;
        if (ui.button("play", {x, by, bws[0], ui.s(58)}, playLabel, primary)) {
            if (needDownload) download(c);
            else g_.app.pushScene(makeChallengeStart(g_, c.id, false));
        }
        UIContext::ButtonStyle st;
        st.fontSize = fs;
        st.enabled = res && res->hasReplay && !place.localId.empty();
        if (ui.button("best", {x + bws[0] + gap, by, bws[1], ui.s(58)}, bestLabel, st)) watchBest(c);
        UIContext::ButtonStyle pr;
        pr.fontSize = fs;
        pr.enabled = !place.localId.empty();
        if (ui.button("practice", {x + bws[0] + bws[1] + 2 * gap, by, bws[2], ui.s(58)}, practiceLabel, pr))
            g_.app.pushScene(makeChallengeStart(g_, c.id, true));
    }

    /// The state of my best run: the server's when it knows it, else the queue's, else local.
    std::pair<ChallengeRunState, std::string> bestState(const DailyChallenge& c) const {
        if (const ChallengeSubmission* s = g_.challenges.bestSubmission(c.id); s && s->ownerId == g_.online.playerId() && g_.online.loggedIn())
            return {s->state, s->note};
        if (auto st = g_.challenges.serverStatus(c.id)) return {*st, {}};
        return {ChallengeRunState::LocalOnly, g_.online.loggedIn() ? std::string() : std::string("Sans compte")};
    }

    void drawMyBest(const DailyChallenge& c, const ChallengeResult* res, const Rect& box) {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(box, ui.s(10), Color{12, 18, 38, 220});
        float x = box.x + ui.s(18), w = box.w - ui.s(36);
        float top = box.y + box.h * 0.30f, bottom = box.y + box.h * 0.70f;
        // Top line: the caption at the left, the streak of this kind at the right.
        ChallengeService::Streak s = g_.challenges.streak(c.kind);
        std::string streak = "Série : " + streakCount(c.kind, s.current) + (s.fromServer ? "" : " (sur ce PC)");
        float sw = ui.text().measure(streak, FontFace::Bold, ui.s(fz(ui, 15))).x;
        sw = std::min(sw, w * 0.5f);
        fitLabel(ui, streak, {x + w, top}, 15, sw, s.current > 0 ? ui.theme().warm : ui.theme().textDim, Align::Right, FontFace::Bold);
        fitLabel(ui, c.finishObjective ? "Ma progression" : "Mon meilleur temps", {x, top}, 15, w - sw - ui.s(16), ui.theme().textDim);
        // This computer's best, or the server's when it is better (a run made elsewhere).
        const MyDay day = myDay(c.id);
        if (c.finishObjective && day.bestMs <= 0) {
            // « Finir le niveau » (2.41): how far I went, and the tier it reached.
            const float best = g_.challenges.bestProgress(c.id);
            if (best <= 0.0f) {
                fitLabel(ui, "Pas encore d'essai sur ce défi.", {x, bottom}, 19, w, ui.theme().text, Align::Left, FontFace::Bold);
                return;
            }
            Vec2 t = ui.label(percentText(best), {x, bottom}, fz(ui, 24), ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            float cx = x + t.x + ui.s(14);
            float h = ui.s(28);
            if (day.tier != ChallengeTier::None)
                cx += chip(ui, challengeTierLabel(day.tier), cx, bottom, h, tierColor(day.tier), x + w - cx, true) + ui.s(10);
            std::string left = "du parcours · Or en finissant";
            if (cx < x + w - ui.s(60)) fitLabel(ui, left, {cx, bottom}, 15, x + w - cx, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle, 0.7f);
            return;
        }
        if (day.bestMs <= 0) {
            // « Plus loin » (2.33): not finished yet, my furthest attempt at the right.
            float ew = 0.0f;
            const float best = c.progressAvailable ? g_.challenges.bestProgress(c.id) : 0.0f;
            if (best > 0.0f && best < 100.0f) {
                const std::string essai = "Mon meilleur essai : " + percentText(best);
                ew = std::min(ui.text().measure(essai, FontFace::Bold, ui.s(fz(ui, 18))).x, w * 0.5f);
                fitLabel(ui, essai, {x + w, bottom}, 18, ew, ui.theme().accent, Align::Right, FontFace::Bold);
                ew += ui.s(16);
            }
            fitLabel(ui, "Pas encore de temps sur ce défi.", {x, bottom}, 19, w - ew, ui.theme().text, Align::Left, FontFace::Bold);
            return;
        }
        Vec2 t = ui.label(formatRunTime(day.bestMs), {x, bottom}, fz(ui, 24), ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        float cx = x + t.x + ui.s(14);
        float h = ui.s(28);
        cx += chip(ui, challengeTierLabel(day.tier), cx, bottom, h, tierColor(day.tier), x + w - cx, true) + ui.s(10);
        auto [state, note] = bestState(c);
        if (!res || res->bestMs <= 0 || day.bestMs < res->bestMs) {
            state = day.state;
            note.clear();
        }
        std::string label = challengeRunStateLabel(state);
        if (cx < x + w - ui.s(60)) cx += chip(ui, label, cx, bottom, h, stateColor(ui.theme(), state), x + w - cx) + ui.s(10);
        if (day.late && cx < x + w - ui.s(60)) cx += chip(ui, "Hors délai", cx, bottom, h, ui.theme().warm, x + w - cx) + ui.s(10);
        if (!note.empty() && cx < x + w - ui.s(60)) fitLabel(ui, note, {cx, bottom}, 14, x + w - cx, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle, 0.7f);
    }

    /// The level's preview (rendered once per challenge), nullptr when it is not installed.
    TexturePtr thumbnail(const DailyChallenge& c, const LevelPlace& place) {
        const std::string key = c.id + ":" + c.level.hash;
        auto it = thumbs_.find(key);
        if (it != thumbs_.end()) return it->second;
        TexturePtr t;
        if (!place.localId.empty())
            if (auto level = g_.content.loadLevel(place.localId)) {
                std::string root;
                if (const LevelEntry* e = g_.content.level(place.localId)) root = Paths::toUtf8(e->assetRoot);
                auto png = renderLevelThumbnail(g_, *level, root);
                if (!png.empty()) t = textureFromPng(g_, png);
            }
        // Not kept while the level is missing (it may be downloaded in a moment).
        if (t || !place.localId.empty()) thumbs_[key] = t;
        return t;
    }

    void watchBest(const DailyChallenge& c) {
        auto rep = g_.challenges.bestReplay(c.id);
        if (!rep) {
            g_.uiSound("ui_error");
            say("Replay introuvable sur ce PC.");
            return;
        }
        // The replay names the level as installed when it was played.
        LevelPlace place = levelPlace(g_, c);
        if (!place.localId.empty()) rep->levelId = place.localId;
        g_.app.pushScene(makeLocalReplayViewer(g_, std::move(*rep), "Mon meilleur temps – " + challengeTitle(c, true)));
    }

    /// « Voir » on a leaderboard row: that run's replay (or, `attempt`, that attempt's on
    /// « Plus loin »; public once verified), downloaded once (kept in saves/challenges/ghosts/),
    /// then played in the replay viewer.
    /// « Télécharger » (2.45): a run's replay (or an attempt's, on « Plus loin ») kept on this
    /// computer, on the level installed here for the challenge of the board.
    ReplayDownloadRequest runDownload(const Json& e, const std::string& replayId, bool attempt) {
        const DailyChallenge& c = *boardChallenge_;
        const Json& p = jfield(e, "player", Json());
        const std::string who = p.is_object() ? jstr(p, "username") : p.is_string() ? p.get<std::string>() : std::string();
        const std::string shown = who.empty() ? std::string() : ou_.displayName(who, jget<int>(e, "rank", 0), jget<bool>(e, "is_me", false));
        const LevelEntry* lvl = boardPlace_.localId.empty() ? nullptr : g_.content.level(boardPlace_.localId);
        ReplayDownloadRequest q = ReplayDownloadUi::request(
            attempt ? "challenge-attempt" : "challenge", replayId,
            std::string(attempt ? "/api/v1/challenges/attempts/" : "/api/v1/challenges/runs/") + replayId + "/replay", who, shown, boardPlace_.localId,
            lvl ? lvl->name : c.level.title, challengeTitle(c, true), jget<int64_t>(e, "time_ms", 0), jstr(e, "created_at", c.date));
        if (p.is_object()) q.meta.playerId = jstr(p, "id");
        return q;
    }

    void watchRun(const Json& e, const std::string& replayId, bool attempt) {
        const DailyChallenge* c = shown();
        if (!c || !watching_.empty()) return;
        const std::string id = c->id;
        const Json& p = jfield(e, "player", Json());
        // The pseudonym as shown on the board (hidden in streaming mode).
        const std::string name = ou_.displayName(p.is_object() ? jstr(p, "username") : std::string(), jget<int>(e, "rank", 0), jget<bool>(e, "is_me", false));
        // 2.45: downloaded on this computer, it plays from its copy (no network needed).
        if (auto rep = ReplayDownloadUi::localCopy(g_, ReplayLibrary::makeId(attempt ? "challenge-attempt" : "challenge", replayId))) {
            std::string title = challengeTitle(*c, true);
            if (!name.empty()) title = name + " – " + title;
            g_.app.pushScene(makeLocalReplayViewer(g_, std::move(*rep), title));
            return;
        }
        watching_ = replayId;
        g_.challenges.downloadRunReplay(id, replayId, [this, id, name, life = std::weak_ptr<int>(life_)](std::optional<fs::path> file, const std::string& why) {
            if (!life.lock()) return;
            watching_.clear();
            std::optional<ReplayData> rep = file ? GhostStore::loadReplay(*file) : std::nullopt;
            const DailyChallenge* c = g_.challenges.find(id);
            if (!rep || !c) {
                g_.uiSound("ui_error");
                say(why.empty() ? std::string("Replay indisponible.") : why);
                return;
            }
            // The replay names the level as installed on its player's computer: this one's copy.
            LevelPlace place = levelPlace(g_, *c);
            if (place.localId.empty()) {
                g_.uiSound("ui_error");
                say(place.problem.empty() ? std::string("Niveau absent de ce PC.") : place.problem);
                return;
            }
            rep->levelId = place.localId;
            std::string title = challengeTitle(*c, true);
            if (!name.empty()) title = name + " – " + title;
            g_.app.pushScene(makeLocalReplayViewer(g_, std::move(*rep), title));
        }, attempt);
    }

    void download(const DailyChallenge& c) {
        downloading_ = true;
        say("Téléchargement du niveau…");
        downloadChallengeLevel(g_, c, life_, [this](bool ok, const std::string& m) {
            downloading_ = false;
            say(m);
            if (!ok) g_.uiSound("ui_error");
        });
    }

    // ------------------------------------------------------------------ leaderboard

    void fetchBoard(const DailyChallenge& c) {
        boardBusy_ = true;
        boardAge_ = 0.0f;
        std::string key = boardKey(c);
        if (boardId_ != c.id) {
            // Another challenge: never its board under this one's title.
            boardId_ = c.id;
            boardOk_ = false;
            board_ = Json();
        }
        boardFor_ = key;
        g_.challenges.leaderboard(c.id, scopeId(), OnlineService::guard(life_, [this, key](const ApiResult& r) {
            boardBusy_ = false;
            if (key != boardFor_) return;
            boardOk_ = r.ok;
            boardCached_ = r.fromCache;
            boardMsg_ = r.ok ? std::string() : r.message;
            boardCode_ = r.status == 400 ? std::string("BAD_REQUEST") : r.code;  // scope=country without a country: 400
            boardOffline_ = !r.ok && (r.networkFailure() || r.status >= 500);
            if (r.ok) board_ = r.data;
        }), allPeriod(c), progressBoard(c));
    }

    void drawBoard(const DailyChallenge& c, const Rect& area) {
        UIContext& ui = g_.ui;
        ui.panel(area, false, 16);
        float x = area.x + ui.s(24), w = area.w - ui.s(48), y = area.y + ui.s(22);
        fitLabel(ui, boardTitle(c.kind), {x, y + ui.s(16)}, 24, w, ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(48);
        if (!g_.online.loggedIn()) {
            std::string text = g_.online.usable()
                                   ? "Connectez-vous à Cosmic Online pour voir le classement du défi et y entrer. Sans compte, le défi se joue quand même : vos résultats restent sur ce PC."
                                   : "Le classement du défi vient de Cosmic Online, indisponible ici. Le défi se joue quand même : vos résultats restent sur ce PC.";
            Vec2 s = ui.labelWrapped(text, {x, y}, w, fz(ui, 17), ui.theme().textDim);
            y += s.y + ui.s(22);
            UIContext::ButtonStyle st;
            st.enabled = g_.online.usable();
            if (ui.button("login", {x, y, std::min(w, ui.s(300)), ui.s(52)}, "Se connecter", st)) g_.app.pushScene(makeOnlineAccount(g_, false));
            y += ui.s(52) + ui.s(34);
            // What the leaderboard shows once connected (each time's state, honestly worded).
            fitLabel(ui, "Avec un compte", {x, y + ui.s(11)}, 18, w, ui.theme().accent, Align::Left, FontFace::Bold);
            y += ui.s(34);
            const std::pair<const char*, const char*> lines[] = {
                {"Monde, Pays, Amis", "les meilleurs temps du défi, avec leur palier."},
                {"Vérifié", "le serveur a contrôlé le replay de la partie."},
                {"En attente de contrôle", "le temps est reçu, son replay pas encore contrôlé."},
                {"Hors ligne : à l'envoi", "la partie attend la connexion pour partir."},
                {"Hors délai", "partie jouée après la fin du défi : classée dans « Depuis »."},
            };
            for (const auto& [k, v] : lines) {
                if (y + ui.s(60) > area.bottom() - ui.s(16)) break;
                float cw = chip(ui, k, x, y + ui.s(14), ui.s(28), ui.theme().accent, w * 0.5f);
                Vec2 s = ui.labelWrapped(v, {x + cw + ui.s(12), y + ui.s(3)}, w - cw - ui.s(12), fz(ui, 16), ui.theme().textDim);
                y += std::max(ui.s(28), s.y) + ui.s(14);
            }
            return;
        }
        // A challenge that is over: the runs made while it ran, or also those made since.
        if (g_.challenges.ended(c)) {
            static const char* const kPeriods[2] = {"Pendant le défi", "Depuis"};
            float pw = (w - ui.s(10)) * 0.5f;
            for (int i = 0; i < 2; ++i) {
                UIContext::ButtonStyle st;
                st.small = true;
                st.selected = period_ == i;
                st.fontSize = ui.fitFont(kPeriods[i], FontFace::Bold, 17, pw - ui.s(24), 0.7f);
                Rect pr{x + (pw + ui.s(10)) * static_cast<float>(i), y, pw, ui.s(40)};
                if (ui.button("period" + std::to_string(i), pr, kPeriods[i], st) && period_ != i) {
                    period_ = i;
                    fetchBoard(c);
                }
                if (i == 1) ui.newBadge("period1", pr, "challenges.board.period", true, true);
            }
            y += ui.s(52);
        }
        // « Meilleurs temps » / « Plus loin » (2.33): the attempts that went the furthest,
        // finished or not, when the level has a route.
        if (c.progressAvailable) {
            // « Finir le niveau » (2.41): its own board (the progress) first.
            static const char* const kBoards[2] = {"Meilleurs temps", "Plus loin"};
            static const char* const kFinishBoards[2] = {"Progression", "Meilleurs temps"};
            const char* const* labels = c.finishObjective ? kFinishBoards : kBoards;
            float bw = (w - ui.s(10)) * 0.5f;
            for (int i = 0; i < 2; ++i) {
                UIContext::ButtonStyle st;
                st.small = true;
                st.selected = boardKind_ == i;
                st.fontSize = ui.fitFont(labels[i], FontFace::Bold, 17, bw - ui.s(24), 0.7f);
                Rect br{x + (bw + ui.s(10)) * static_cast<float>(i), y, bw, ui.s(40)};
                if (ui.button("kind" + std::to_string(i), br, labels[i], st) && boardKind_ != i) {
                    boardKind_ = i;
                    fetchBoard(c);
                }
                if (i == 1 && !c.finishObjective) ui.newBadge("kind1", br, "challenges.board.progress", true, true);
            }
            y += ui.s(52);
        }
        const bool progress = progressBoard(c);
        // What the rows' downloads note (2.45): the challenge, the level installed here.
        boardChallenge_ = &c;
        boardPlace_ = levelPlace(g_, c);
        // Monde, Pays, Amis.
        static const char* const kScopes[3] = {"Monde", "Pays", "Amis"};
        float sw = (w - 2 * ui.s(10)) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.selected = scope_ == i;
            st.fontSize = 17;
            if (ui.button("scope" + std::to_string(i), {x + (sw + ui.s(10)) * static_cast<float>(i), y, sw, ui.s(40)}, kScopes[i], st) && scope_ != i) {
                scope_ = i;
                fetchBoard(c);
            }
        }
        y += ui.s(52);
        Rect list{x, y, w, area.bottom() - ui.s(20) - y};
        const bool fresh = boardFor_ == boardKey(c);
        // The player's own line (its PENDING time too), at the bottom when there is one.
        const Json& me = jfield(board_, "me", Json());
        bool hasMe = fresh && boardOk_ && me.is_object() &&
                     (progress ? jget<float>(me, "progress", 0.0f) > 0.0f : jget<int64_t>(me, "time_ms", 0) > 0);
        // Pinned at the bottom only when my run is not already in the list.
        if (hasMe)
            for (const Json& e : jfield(board_, "entries", Json::array()))
                if (jget<bool>(e, "is_me", false) || (!jstr(me, "run_id").empty() && jstr(e, "run_id") == jstr(me, "run_id"))) hasMe = false;
        if (hasMe) list.h -= ui.s(64);
        if (!fresh || (boardBusy_ && !boardOk_)) {
            fitLabel(ui, "Chargement…", {x, y + ui.s(14)}, 17, w, ui.theme().textDim);
            return;
        }
        if (!boardOk_) {
            std::string text = boardOffline_ ? "Hors ligne : classement indisponible pour l'instant."
                               : boardCode_ == "BAD_REQUEST" && scope_ == 1
                                   ? "Pays inconnu : choisissez votre pays dans votre compte (EN LIGNE) pour ce classement."
                                   : "Classement indisponible : " + boardMsg_;
            Vec2 s = ui.labelWrapped(text, {x, y}, w, fz(ui, 17), ui.theme().textDim);
            if (ui.button("retry", {x, y + s.y + ui.s(16), std::min(w, ui.s(240)), ui.s(46)}, "Réessayer")) fetchBoard(c);
            return;
        }
        const Json& entries = jfield(board_, "entries", Json::array());
        if (!entries.is_array() || entries.empty()) {
            std::string text = progress ? (scope_ == 2 ? "Aucun ami n'a encore d'essai classé sur ce défi."
                                                       : "Aucun essai classé pour l'instant : allez le plus loin possible !")
                               : scope_ == 2 ? "Aucun ami n'a encore de temps vérifié sur ce défi."
                                             : "Aucun temps vérifié pour l'instant : soyez le premier !";
            ui.labelWrapped(text, {x, y}, w, fz(ui, 17), ui.theme().textDim);
        } else {
            const float rowH = ui.s(46);
            float content = rowH * static_cast<float>(entries.size());
            ui.beginScroll("board", list, content);
            float ry = list.y - ui.scrollOffset();
            int i = 0;
            for (const Json& e : entries) {
                Rect row{list.x, ry + rowH * static_cast<float>(i), list.w - ui.s(10), rowH - ui.s(4)};
                ++i;
                if (row.bottom() < list.y || row.y > list.bottom()) continue;
                drawBoardRow(e, row, jget<bool>(e, "is_me", false), progress);
            }
            ui.endScroll();
        }
        if (hasMe) {
            Rect row{x, area.bottom() - ui.s(20) - ui.s(54), w, ui.s(50)};
            ui.r().fillRoundedRect(row, ui.s(8), ui.theme().accent.withAlpha(30));
            Json mine = me;
            mine["is_me"] = true;
            drawBoardRow(mine, row, true, progress);
        }
        if (boardCached_) fitLabel(ui, "Hors ligne : dernier classement reçu", {x + w, area.y + ui.s(38)}, 14, w * 0.45f, ui.theme().warm, Align::Right);
    }

    /// A row of a leaderboard: « Meilleurs temps » (time, tier) or « Plus loin » (`progress`:
    /// how far, « Fini » for a finished run).
    void drawBoardRow(const Json& e, const Rect& row, bool me, bool progress) {
        UIContext& ui = g_.ui;
        if (me) ui.r().drawRoundedRect(row, ui.s(8), ui.theme().accent.withAlpha(150), std::max(1.0f, ui.s(1.2f)));
        else ui.r().fillRoundedRect(row, ui.s(8), Color{12, 18, 38, 200});
        float cy = row.center().y, x = row.x + ui.s(12);
        int rank = jget<int>(e, "rank", 0);
        std::string rk = rank > 0 ? std::to_string(rank) : std::string("—");
        fitLabel(ui, rk, {x + ui.s(34), cy}, 18, ui.s(40), rank == 1 ? tierColor(ChallengeTier::Gold) : ui.theme().text, Align::Right, FontFace::Bold);
        x += ui.s(48);
        // Right side first: « Voir » (its replay), status, « hors délai », tier (or « Fini »),
        // time (or progress), each measured, then the name in what is left.
        float right = row.right() - ui.s(10);
        // The replay: a run's (run_id), or on « Plus loin » an attempt's (its server id).
        std::string replayId = jstr(e, "run_id");
        bool attemptReplay = false;
        if (replayId.empty() && progress) {
            replayId = jstr(e, "attempt_id");
            attemptReplay = true;
        }
        if (jget<bool>(e, "has_replay", false) && !replayId.empty()) {
            const std::string label = watching_ == replayId ? "…" : "Voir";
            float bw = std::max(ui.text().measure("Voir", FontFace::Bold, ui.s(fz(ui, 15))).x, ui.text().measure("…", FontFace::Bold, ui.s(fz(ui, 15))).x) + ui.s(28);
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = fz(ui, 15);
            st.enabled = watching_.empty();
            if (ui.button("watch" + replayId + (me ? "m" : ""), {right - bw, row.y + ui.s(5), bw, row.h - ui.s(10)}, label, st))
                watchRun(e, replayId, attemptReplay);
            right -= bw + ui.s(10);
            // 2.45: kept on this computer, to watch it without a connection.
            if (boardChallenge_) {
                const float dw = row.h - ui.s(10);
                ReplayDownloadUi::icon(g_, "dl" + replayId + (me ? "m" : ""), {right - dw, row.y + ui.s(5), dw, dw}, runDownload(e, replayId, attemptReplay));
                right -= dw + ui.s(10);
            }
        }
        std::string status = jstr(e, "status", "VERIFIED");
        ChallengeRunState st = challengeRunStateFromServer(status);
        float h = ui.s(26);
        float sw = chipWidth(ui, challengeRunStateLabel(st), h);
        if (right - sw > x + ui.s(200)) {
            chip(ui, challengeRunStateLabel(st), right - sw, cy, h, stateColor(ui.theme(), st), sw);
            right -= sw + ui.s(8);
        }
        if (jget<bool>(e, "late", false)) {
            float lw = chipWidth(ui, "Hors délai", h);
            if (right - lw > x + ui.s(180)) {
                chip(ui, "Hors délai", right - lw, cy, h, ui.theme().warm, lw);
                right -= lw + ui.s(8);
            }
        }
        if (progress) {
            // « Finir le niveau » (2.41): the tier the server computed for that progress.
            if (ChallengeTier tier = challengeTierFromJson(jfield(e, "tier", Json())); tier != ChallengeTier::None) {
                float tw = chipWidth(ui, challengeTierLabel(tier), h);
                if (right - tw > x + ui.s(160)) {
                    chip(ui, challengeTierLabel(tier), right - tw, cy, h, tierColor(tier), tw, true);
                    right -= tw + ui.s(10);
                }
            } else if (jget<bool>(e, "finished", false)) {
                float fw = chipWidth(ui, "Fini", h);
                if (right - fw > x + ui.s(160)) {
                    chip(ui, "Fini", right - fw, cy, h, ui.theme().success, fw, true);
                    right -= fw + ui.s(10);
                }
            }
        } else if (ChallengeTier tier = challengeTierFromJson(jfield(e, "tier", Json())); tier != ChallengeTier::None) {
            float tw = chipWidth(ui, challengeTierLabel(tier), h);
            if (right - tw > x + ui.s(160)) {
                chip(ui, challengeTierLabel(tier), right - tw, cy, h, tierColor(tier), tw, true);
                right -= tw + ui.s(10);
            }
        }
        std::string value = progress ? percentText(jget<float>(e, "progress", 0.0f)) : formatRunTime(jget<int64_t>(e, "time_ms", 0));
        float tw = ui.text().measure(value, FontFace::Bold, ui.s(fz(ui, 18))).x;
        ui.label(value, {right, cy}, fz(ui, 18), ui.theme().text, Align::Right, FontFace::Bold, VAlign::Middle);
        right -= tw + ui.s(14);
        // The player: flag, then the name as they chose it (hidden in streaming mode).
        const Json& p = jfield(e, "player", Json());
        std::string name = p.is_object() ? jstr(p, "username") : p.is_string() ? p.get<std::string>() : std::string();
        std::string country = p.is_object() ? jstr(p, "country_code") : std::string();
        if (!country.empty() && right - x > ui.s(80)) {
            ou_.drawFlag(country, {x, cy - ui.s(9), ui.s(26), ui.s(18)});
            x += ui.s(34);
        }
        // 2.66: « [TAG] name » (the tag of the player's clan).
        std::string shown = me ? std::string("Vous") : p.is_object() ? ou_.displayName(p, rank, false) : ou_.displayName(name, rank, false);
        const std::string mine = p.is_object() ? clans::taggedName(p) : name;
        i18n::Verbatim pseudo;
        if (right - x > ui.s(24)) fitLabel(ui, me && !name.empty() ? mine : shown, {x, cy}, 17, right - x, me ? ui.theme().accent : ui.theme().text);
    }

    // ------------------------------------------------------------------ my results

    /// My result of a challenge: the best of this computer's and the server's (a run made on
    /// another computer counts too), with the state of that time.
    struct MyDay {
        int64_t bestMs = 0;
        ChallengeTier tier = ChallengeTier::None;
        ChallengeRunState state = ChallengeRunState::LocalOnly;
        bool late = false;  // that time was made « hors délai »
    };
    MyDay myDay(const std::string& id) const {
        MyDay d;
        if (const ChallengeResult* r = g_.challenges.result(id); r && r->bestMs > 0) {
            d.bestMs = r->bestMs;
            d.tier = r->tier;
            d.late = r->bestLate;
            if (const ChallengeSubmission* s = g_.challenges.bestSubmission(id); s && g_.online.loggedIn() && s->ownerId == g_.online.playerId()) {
                d.state = s->state;
                d.late = s->late;
            }
        }
        if (auto s = g_.challenges.serverResult(id); s && s->bestMs > 0) {
            if (d.bestMs <= 0 || s->bestMs <= d.bestMs) {
                d.bestMs = s->bestMs;
                d.state = s->status;
                d.late = s->late;
            }
            d.tier = std::max(d.tier, s->tier);
        }
        // « Finir le niveau » (2.41): an unfinished attempt reaches a tier too (here, or as the
        // server computed it). Never on a « Vitesse » challenge (no tier without a time).
        if (const ChallengeResult* r = g_.challenges.result(id)) d.tier = std::max(d.tier, r->tier);
        if (auto s = g_.challenges.serverResult(id)) {
            d.tier = std::max(d.tier, s->tier);
            if (d.bestMs <= 0) d.state = s->status;
        }
        return d;
    }

    // ------------------------------------------------------------------ Calendrier

    /// One cell of the calendar (a day, a week): `head` at the top left, then the challenge's
    /// level and my tier and state; a hotspot (click, Entrée, A: its card) when there is one.
    void drawCell(const std::string& name, const Rect& r, const DailyChallenge* c, const std::string& head, bool future, bool today, std::string& info) {
        UIContext& ui = g_.ui;
        Color bg = future ? Color{10, 14, 26, 140} : Color{12, 18, 38, 215};
        bool hovered = false, focused = false;
        if (c && !future) {
            if (ui.hotspot(name, r, &hovered)) openDetail(c->id, name);
            focused = ui.focused(name);
            if (hovered || focused) {
                bg = Color{26, 44, 76, 240};
                info = challengeTitle(*c, true);
            }
        }
        ui.r().fillRoundedRect(r, ui.s(8), bg);
        // 2.38: the challenge's level in the background (its preview, darkened under the text),
        // and a ribbon of its kind at the top (day: cyan, week: violet).
        if (c && !future) {
            art_.drawCellImage(r, *c, hovered || focused);
            Color kc = c->kind == ChallengeKind::Weekly ? ui.theme().violet : c->kind == ChallengeKind::Monthly ? ui.theme().warm : ui.theme().accent;
            ui.r().fillRoundedRect({r.x + ui.s(8), r.y, r.w - ui.s(16), std::max(2.0f, ui.s(3))}, ui.s(1.5f), kc.withAlpha(220));
        }
        if (today) ui.r().drawRoundedRect(r, ui.s(8), ui.theme().accent, std::max(1.5f, ui.s(2.5f)));
        else if (focused) ui.r().drawRoundedRect(r, ui.s(8), ui.theme().accent.withAlpha(190), std::max(1.0f, ui.s(1.5f)));
        const float px = r.x + ui.s(10), room = r.w - ui.s(20);
        const float l1 = r.y + r.h * 0.22f, l2 = r.y + r.h * 0.52f, l3 = r.y + r.h * 0.80f;
        // Line 1: the day (or the week), my tier at the right.
        float tierW = 0.0f;
        const MyDay day = c && !future ? myDay(c->id) : MyDay{};
        if (day.tier != ChallengeTier::None) {
            float ch = std::min(ui.s(24), r.h * 0.3f);
            tierW = std::min(chipWidth(ui, challengeTierLabel(day.tier), ch), room * 0.55f);
            chip(ui, challengeTierLabel(day.tier), r.right() - ui.s(8) - tierW, l1, ch, tierColor(day.tier), tierW, true);
        }
        Color hc = today ? ui.theme().accent : future ? ui.theme().textDim.withAlpha(120) : ui.theme().text;
        fitLabel(ui, head, {px, l1}, 18, room - tierW - ui.s(6), hc, Align::Left, FontFace::Bold);
        if (!c || future) return;
        {
            std::optional<i18n::Verbatim> v;
            if (c->community()) v.emplace();  // a player's level: as its author named it
            fitLabel(ui, levelTitle(g_, *c), {px, l2}, 15, room, Color{185, 200, 218, 255});
        }
        // Line 3: where my best run stands (nothing when I did not play it), then (2.38) the
        // tags of what makes this challenge what it is, as many as fit whole, and (2.60) at the
        // right its medals, measured first: the text and the tags take what they leave.
        std::string st;
        Color sc = ui.theme().textDim;
        if (day.bestMs > 0) {
            if (day.state == ChallengeRunState::Rejected) st = "Refusé", sc = ui.theme().danger;
            else if (day.late) st = "Hors délai", sc = ui.theme().warm;
            else if (day.state == ChallengeRunState::Verified) st = "Vérifié", sc = ui.theme().success;
            else if (day.state == ChallengeRunState::Pending || day.state == ChallengeRunState::Sent) st = "En attente", sc = ui.theme().accent;
            else if (day.state == ChallengeRunState::Waiting) st = "À l'envoi", sc = ui.theme().warm;
            else st = "Sur ce PC";
        } else if (float pr = c->finishObjective ? g_.challenges.bestProgress(c->id) : 0.0f; pr > 0.0f) {
            // « Finir le niveau » (2.41): how far I went.
            st = percentText(pr), sc = ui.theme().accent;
        }
        const float tagH = std::min(ui.s(20), r.h * 0.2f);
        float right = r.right() - ui.s(8);
        const float stW = st.empty() ? 0.0f : std::min(ui.text().measure(st, FontFace::Bold, ui.s(fz(ui, 14))).x, room * 0.5f) + ui.s(8);
        medalsAt(*c, px + stW, right, l3, tagH, day.tier, hovered || focused, r);
        float tx = px;
        if (!st.empty()) tx += fitLabel(ui, st, {px, l3}, 14, right - px - ui.s(4), sc, Align::Left, FontFace::Bold).x + ui.s(8);
        art_.drawTags(*c, tx, l3, right, tagH);
    }

    /// 2.60: the medals of a challenge (its players at Bronze, Argent, Or, from the server) at
    /// the right of [left, right] on the line `cy`, as many as fit (MedalPlan); `right` becomes
    /// the left of what was drawn. Hovered or focused (`lit`): their tooltip, drawn last.
    void medalsAt(const DailyChallenge& c, float left, float& right, float cy, float h, ChallengeTier mine, bool lit, const Rect& owner) {
        UIContext& ui = g_.ui;
        const std::optional<ChallengeTierCounts> t = g_.challenges.tierCounts(c.id);
        if (!t) return;  // never read (offline without a kept answer): nothing, never made up
        const MedalPlan plan = planMedals(right - left, h, medalNumberWidths(ui, *t, h));
        Rect area{right - plan.width, cy - h * 0.5f, plan.width, h};
        if (plan.fit != MedalFit::None) {
            drawMedals(ui, plan, *t, area.x, cy, h, mine);
            right = area.x - ui.s(6);
        }
        if (lit || (plan.fit != MedalFit::None && ui.mouseOver(area))) {
            medalTip_.show = true;
            medalTip_.anchor = plan.fit != MedalFit::None ? Vec2{area.center().x, area.y} : Vec2{owner.center().x, owner.y};
            medalTip_.title.clear();
            medalTip_.rows = {{charts::kNoSwatch, medalsText(*t), {}}};
        }
    }

    /// The month's challenge, at the head of the calendar.
    void drawMonthBand(const Rect& r, const DailyChallenge* c, std::string& info) {
        UIContext& ui = g_.ui;
        bool hovered = false, focused = false;
        if (c) {
            if (ui.hotspot("month", r, &hovered)) openDetail(c->id, "month");
            focused = ui.focused("month");
            if (hovered || focused) info = challengeTitle(*c, true);
        }
        ui.r().fillRoundedRect(r, ui.s(10), hovered || focused ? Color{26, 44, 76, 240} : Color{14, 22, 46, 225});
        if (c) art_.drawCellImage(r, *c, hovered || focused);  // 2.38: its level behind
        ui.r().drawRoundedRect(r, ui.s(10), ui.theme().violet.withAlpha(focused ? 230 : 120), std::max(1.0f, ui.s(focused ? 2.0f : 1.2f)));
        float x = r.x + ui.s(16), right = r.right() - ui.s(16), cy = r.center().y, h = std::min(ui.s(30), r.h * 0.6f);
        x += chip(ui, "Défi du mois", x, cy, h, ui.theme().violet, r.w * 0.3f, true) + ui.s(16);
        if (!c) {
            std::string none = g_.challenges.legacyServer() ? "Ce serveur ne propose pas encore de défi du mois."
                                                            : "Pas de défi du mois pour " + frenchMonth(month_) + ".";
            fitLabel(ui, none, {x, cy}, 17, right - x, ui.theme().textDim);
            return;
        }
        // At the right: my tier and the state of my best run, measured first.
        const MyDay day = myDay(c->id);
        if (day.bestMs > 0) {
            std::string st = day.late ? std::string("Hors délai") : challengeRunStateLabel(day.state);
            Color sc = day.late ? ui.theme().warm : stateColor(ui.theme(), day.state);
            float sw = std::min(chipWidth(ui, st, h), (right - x) * 0.35f);
            chip(ui, st, right - sw, cy, h, sc, sw);
            right -= sw + ui.s(8);
            float tw = std::min(chipWidth(ui, challengeTierLabel(day.tier), h), (right - x) * 0.3f);
            chip(ui, challengeTierLabel(day.tier), right - tw, cy, h, tierColor(day.tier), tw, true);
            right -= tw + ui.s(14);
        } else if (day.tier != ChallengeTier::None) {
            // « Finir le niveau » (2.41): an attempt's tier.
            float tw = std::min(chipWidth(ui, challengeTierLabel(day.tier), h), (right - x) * 0.3f);
            chip(ui, challengeTierLabel(day.tier), right - tw, cy, h, tierColor(day.tier), tw, true);
            right -= tw + ui.s(14);
        }
        // 2.60: its medals, left of them (a third of what is left at most: the title keeps its room).
        {
            float mright = right;
            medalsAt(*c, right - (right - x) * 0.34f, mright, cy, std::min(ui.s(24), r.h * 0.45f), day.tier, hovered || focused, r);
            if (mright < right) right = mright - ui.s(8);
        }
        float used = 0.0f;
        {
            std::optional<i18n::Verbatim> v;
            if (c->community()) v.emplace();
            used = fitLabel(ui, levelTitle(g_, *c), {x, cy}, 19, right - x, ui.theme().text, Align::Left, FontFace::Bold).x;
        }
        // 2.38: its tags after the title, those that fit whole.
        art_.drawTags(*c, x + used + ui.s(14), cy, right, std::min(ui.s(24), r.h * 0.45f));
    }

    void drawCalendar(const Rect& body) {
        UIContext& ui = g_.ui;
        ChallengeService& cs = g_.challenges;
        art_.beginFrame();  // one level preview rendered per frame at most (CalendarArt::cellThumb)
        medalTip_.show = false;  // 2.60: the medals' tooltip of the cell hovered or focused
        // « NOUVEAU » (2.60) on « Calendrier » until the calendar with its medals is seen once.
        if (ui.isNew("challenges.calendar.medals") && ui.featureSeen) ui.featureSeen("challenges.calendar.medals");
        // « NOUVEAU » (2.38) on « Calendrier » until the calendar with its images is seen once.
        if (ui.isNew("challenges.calendar.look") && ui.featureSeen) ui.featureSeen("challenges.calendar.look");
        ui.panel(body, false, 16);
        const float x = body.x + ui.s(22), w = body.w - ui.s(44);
        float y = body.y + ui.s(16);
        const int64_t now = cs.nowMs();
        const int64_t today = now / 86400000LL;
        const std::string thisMonth = ChallengeService::utcMonth(now);
        if (month_.empty() || month_ > thisMonth) month_ = thisMonth;
        // Never before the oldest month with a challenge (the server's word; offline, the
        // oldest one kept here), never after this one.
        std::string first = cs.firstMonth();
        if (first.empty()) {
            auto past = cs.past();
            first = past.empty() ? thisMonth : past.back()->date.substr(0, 7);
        }
        // Mois précédent, the month, Mois suivant.
        const float hh = ui.s(44);
        {
            static const char* const kTitles[12] = {"Janvier", "Février", "Mars",      "Avril",   "Mai",      "Juin",
                                                    "Juillet", "Août",    "Septembre", "Octobre", "Novembre", "Décembre"};
            const std::string prevL = "Mois précédent", nextL = "Mois suivant";
            float bw = std::max(ui.text().measure(prevL, FontFace::Bold, ui.s(17)).x, ui.text().measure(nextL, FontFace::Bold, ui.s(17)).x) + ui.s(40);
            UIContext::ButtonStyle nb;
            nb.small = true;
            nb.fontSize = 17;
            nb.enabled = month_ > first;
            if (ui.button("prev", {x, y, bw, hh}, prevL, nb)) changeMonth(-1);
            nb.enabled = month_ < thisMonth;
            if (ui.button("next", {x + w - bw, y, bw, hh}, nextL, nb)) changeMonth(+1);
            int yy = 0, mm = 0;
            std::sscanf(month_.c_str(), "%4d-%2d", &yy, &mm);
            std::string title = std::string(kTitles[std::clamp(mm, 1, 12) - 1]) + " " + std::to_string(yy);
            fitLabel(ui, title, {x + w * 0.5f, y + hh * 0.5f}, 28, w - 2 * bw - ui.s(40), ui.theme().text, Align::Center, FontFace::Title);
        }
        y += hh + ui.s(12);
        std::string info;
        // The month's challenge at the head.
        const DailyChallenge* mc = cs.find("monthly-" + month_);
        if (mc && mc->startsAtMs > now) mc = nullptr;  // never shown before its start
        const float bandH = ui.s(54);
        drawMonthBand({x, y, w, bandH}, mc, info);
        y += bandH + ui.s(12);
        // The days, Monday to Sunday, and a column for each week's challenge.
        const float gap = ui.s(6);
        const float weekW = std::max(ui.s(160), w * 0.15f);
        const float dayW = (w - weekW - 7 * gap) / 7.0f;
        static const char* const kDays[7] = {"Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi", "Dimanche"};
        const float headH = ui.s(26);
        for (int d = 0; d < 7; ++d)
            fitLabel(ui, kDays[d], {x + (dayW + gap) * static_cast<float>(d) + ui.s(10), y + headH * 0.5f}, 15, dayW - ui.s(12), ui.theme().textDim, Align::Left,
                     FontFace::Bold);
        fitLabel(ui, "Semaine", {x + 7 * (dayW + gap) + ui.s(10), y + headH * 0.5f}, 15, weekW - ui.s(12), ui.theme().violet, Align::Left, FontFace::Bold);
        y += headH + ui.s(4);
        const int64_t firstDay = challenge_period::firstDay(ChallengeKind::Monthly, month_).value_or(today);
        const int days = std::max(28, challenge_period::daysInMonth(month_));
        const int64_t gridStart = firstDay - challenge_period::weekday(firstDay);
        const int rows = static_cast<int>((firstDay + days - 1 - gridStart) / 7) + 1;
        const float footH = ui.s(30);
        const float gridBottom = body.bottom() - ui.s(14) - footH - ui.s(8);
        const float rowH = std::min(ui.s(120), (gridBottom - y - gap * static_cast<float>(rows - 1)) / static_cast<float>(rows));
        for (int r = 0; r < rows; ++r) {
            const int64_t weekStart = gridStart + 7 * r;
            const float ry = y + (rowH + gap) * static_cast<float>(r);
            for (int d = 0; d < 7; ++d) {
                const int64_t day = weekStart + d;
                Rect cell{x + (dayW + gap) * static_cast<float>(d), ry, dayW, rowH};
                if (day < firstDay || day >= firstDay + days) {
                    // Another month's day: left empty (its challenges are in its own month).
                    ui.r().fillRoundedRect(cell, ui.s(8), Color{10, 14, 26, 90});
                    continue;
                }
                const std::string date = challenge_period::dateOfDay(day);
                const DailyChallenge* c = cs.find("daily-" + date);
                if (c && c->startsAtMs > now) c = nullptr;
                drawCell("d" + date, cell, c, std::to_string(day - firstDay + 1), day > today, day == today, info);
            }
            // The week's challenge (ISO week of that row).
            const std::string week = challenge_period::ofDay(ChallengeKind::Weekly, weekStart);
            const DailyChallenge* wc = cs.find("weekly-" + week);
            if (wc && wc->startsAtMs > now) wc = nullptr;
            Rect wcell{x + 7 * (dayW + gap), ry, weekW, rowH};
            int wn = std::atoi(week.substr(6).c_str());
            drawCell("w" + week, wcell, wc, "Semaine " + std::to_string(wn), weekStart > today, false, info);
            if (wcell.w > 0) ui.r().drawRoundedRect(wcell, ui.s(8), ui.theme().violet.withAlpha(70), std::max(1.0f, ui.s(1.0f)));
        }
        // Footer: what the calendar shows (the server's answer, or what is kept here), and the
        // challenge under the pointer or the focus.
        const float fy = body.bottom() - ui.s(14) - footH * 0.5f;
        const CalendarMonthView v = monthStatus();
        float used = 0.0f;
        if (!v.text.empty()) used = fitLabel(ui, v.text, {x, fy}, 15, w * 0.62f, v.color, Align::Left, FontFace::Body, VAlign::Middle, 0.7f).x + ui.s(24);
        if (!info.empty() && w - used > ui.s(80)) fitLabel(ui, info, {x + w, fy}, 15, w - used, ui.theme().text, Align::Right, FontFace::Bold);
    }

    struct CalendarMonthView {
        std::string text;
        Color color;
    };
    CalendarMonthView monthStatus() const {
        const UITheme& th = g_.ui.theme();
        const auto& m = g_.challenges.calendar(month_);
        using S = ChallengeService::CalendarMonth::State;
        if (!g_.online.usable()) return {"Cosmic Online indisponible : seuls les défis gardés sur ce PC sont montrés.", th.warm};
        switch (m.state) {
            case S::Loading: return {"Chargement du calendrier…", th.textDim};
            case S::Cached: return {"Hors ligne : dernier calendrier reçu, avec les défis gardés sur ce PC.", th.warm};
            case S::Offline: return {"Hors ligne : seuls les défis gardés sur ce PC sont montrés.", th.warm};
            case S::Failed: return {"Calendrier indisponible : " + m.message, th.warm};
            case S::Never: return {"Seuls les défis gardés sur ce PC sont montrés.", th.textDim};
            case S::Ok: break;
        }
        if (g_.challenges.legacyServer()) return {"Ce serveur ne propose encore que les défis du jour.", th.textDim};
        return {};
    }

    GameServices& g_;
    OnlineUi ou_;
    MenuBackground bg_;
    int tab_ = 0;
    std::string detail_;      // the calendar shows this challenge's card ("" = the month)
    std::string detailFrom_;  // the cell it was opened from (the focus goes back to it)
    std::string month_;       // the calendar's month ("2026-11")
    std::string pendingFocus_;
    int scope_ = 0;
    int period_ = 0;  // 0: « Pendant le défi » (period=live), 1: « Depuis » (period=all)
    int boardKind_ = 0;  // 0: « Meilleurs temps », 1: « Plus loin » (board=progress, 2.33)
    ChallengeRewardRow rewardRow_;  // the rewards of the card shown (2.35)
    bool rewardsShown_ = false;
    charts::Tip medalTip_;          // 2.60: the medals of the calendar's cell hovered or focused
    Json board_;
    std::string boardFor_, boardId_;
    std::string boardMsg_, boardCode_;
    bool boardBusy_ = false, boardOk_ = false, boardOffline_ = false, boardCached_ = false;
    float boardAge_ = 0.0f;
    bool downloading_ = false;
    std::string watching_;  // the run whose replay is being downloaded (« Voir »)
    const DailyChallenge* boardChallenge_ = nullptr;  // the board drawn this frame (its rows' downloads)
    LevelPlace boardPlace_;                            // its level here
    std::map<std::string, TexturePtr> thumbs_;
    CalendarArt art_;  // the calendar's previews and tags (ChallengeUi.h)
    std::string msg_;
    float msgTimer_ = 0.0f;
    float t_ = 0.0f;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

// ======================================================================== before the start

class ChallengeStartScene : public Scene {
public:
    ChallengeStartScene(GameServices& g, std::string id, bool practice) : g_(g), id_(std::move(id)), practice_(practice) {}
    bool isOverlay() const override { return true; }

    void onEnter() override {
        if (const DailyChallenge* c = g_.challenges.find(id_)) challenge_ = *c;
        if (challenge_) {
            place_ = levelPlace(g_, *challenge_);
            if (!place_.localId.empty()) {
                level_ = g_.content.loadLevel(place_.localId);
                if (level_) {
                    hasKeyPowers_ = !playerPowerKeys(g_, *level_, challenge_->rules.powers).powers.empty();
                    std::string root;
                    if (const LevelEntry* e = g_.content.level(place_.localId)) root = Paths::toUtf8(e->assetRoot);
                    auto png = renderLevelThumbnail(g_, *level_, root);
                    if (!png.empty()) thumb_ = textureFromPng(g_, png);
                }
            }
            // My best run of this challenge, as a ghost.
            if (const ChallengeResult* r = g_.challenges.result(challenge_->id); r && r->hasReplay && FileUtil::exists(g_.challenges.bestReplayPath(challenge_->id)))
                bestMs_ = r->bestMs;
            // The first of the day (its replay is public once verified).
            if (g_.online.usable())
                g_.challenges.leaderboard(challenge_->id, "world", OnlineService::guard(life_, [this](const ApiResult& r) {
                    if (!r.ok || r.fromCache) return;
                    const Json& entries = jfield(r.data, "entries", Json::array());
                    if (!entries.is_array() || entries.empty()) return;
                    const Json& e = entries.front();
                    if (jget<bool>(e, "is_me", false) || !jget<bool>(e, "has_replay", true) || jstr(e, "run_id").empty()) return;
                    topRun_ = jstr(e, "run_id");
                    topMs_ = jget<int64_t>(e, "time_ms", 0);
                    const Json& p = jfield(e, "player", Json());
                    topName_ = p.is_object() ? jstr(p, "username") : std::string();
                }));
        }
        // A challenge that is over: the run is « hors délai » (« Depuis » only), on the
        // challenge's version of the level only, else a practice.
        if (challenge_ && !practice_ && g_.challenges.ended(*challenge_)) {
            late_ = true;
            if (!place_.localId.empty() && !place_.sameVersion) practice_ = true;
        }
        g_.ui.setFocus("cstart/go");
    }

    void update(float dt) override {
        if (msgTimer_ > 0.0f) msgTimer_ -= dt;
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        const float W = ui.screenW(), H = ui.screenH();
        ui.r().fillRect({0, 0, W, H}, Color{4, 6, 14, 236});
        const float m = ui.s(44);
        Rect panel{m, ui.s(32), W - 2 * m, H - ui.s(64)};
        ui.panel(panel, true, 18);
        ui.pushId("cstart");
        float x = panel.x + ui.s(32), w = panel.w - ui.s(64), y = panel.y + ui.s(26);
        bool cancel = false, go = false;
        rewardsShown_ = false;
        if (!challenge_) {
            fitLabel(ui, "Ce défi n'est plus sur ce PC.", {x, y + ui.s(20)}, 26, w, ui.theme().danger, Align::Left, FontFace::Bold);
            cancel = ui.button("cancel", {panel.right() - ui.s(32) - ui.s(220), panel.bottom() - ui.s(80), ui.s(220), ui.s(54)}, "Retour");
        } else {
            const DailyChallenge& c = *challenge_;
            std::string title = practice_ ? "ENTRAÎNEMENT — HORS CLASSEMENT" : challengeTitle(c, late_);
            // A run « hors délai »: said at the right of the title.
            float tagW = 0.0f;
            if (late_ && !practice_) {
                const std::string tag = "Hors délai : classé dans « Depuis »";
                tagW = std::min(chipWidth(ui, tag, ui.s(34)), w * 0.45f);
                chip(ui, tag, x + w - tagW, y + ui.s(20), ui.s(34), ui.theme().warm, tagW);
                tagW += ui.s(20);
            }
            fitLabel(ui, title, {x, y + ui.s(20)}, 32, w - tagW, practice_ ? ui.theme().warm : ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle, 0.6f);
            y += ui.s(52);
            {
                std::string sub = levelTitle(g_, c);
                std::optional<i18n::Verbatim> v;
                if (c.community()) v.emplace();
                Vec2 s = fitLabel(ui, sub, {x, y + ui.s(12)}, 22, w * 0.6f, ui.theme().accent, Align::Left, FontFace::Bold);
                v.reset();
                float sx = x + s.x;
                if (c.community()) {
                    Vec2 p = ui.label("  ·  par ", {sx, y + ui.s(12)}, fz(ui, 18), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
                    i18n::Verbatim author;
                    fitLabel(ui, c.level.authorName, {sx + p.x, y + ui.s(12)}, 18, x + w - sx - p.x, ui.theme().text, Align::Left, FontFace::Bold);
                } else if (std::string pl = planetLine(g_, c); !pl.empty()) {
                    fitLabel(ui, "  ·  " + pl, {sx, y + ui.s(12)}, 18, x + w - sx, ui.theme().textDim);
                }
            }
            y += ui.s(40);
            float bottom = panel.bottom() - ui.s(96);
            float colGap = ui.s(28);
            Rect left{x, y, (w - colGap) * 0.42f, bottom - y};
            Rect right{left.right() + colGap, y, x + w - left.right() - colGap, bottom - y};
            drawLeft(c, left);
            drawRight(c, right);
            // Bottom bar: a message (if any), Annuler, Lancer.
            float by = panel.bottom() - ui.s(78);
            UIContext::ButtonStyle primary;
            primary.primary = true;
            primary.enabled = place_.problem.empty() && level_.has_value() && !downloadingGhost_;
            Rect goR{panel.right() - ui.s(32) - ui.s(330), by, ui.s(330), ui.s(56)};
            const std::string goLabel = downloadingGhost_ ? "Fantôme…" : practice_ ? "Lancer l'entraînement" : late_ ? "Lancer (hors délai)" : "Lancer le défi";
            primary.fontSize = ui.fitFont(goLabel, FontFace::Bold, 26, goR.w - ui.s(28), 0.6f);
            go = ui.button("go", goR, goLabel, primary);
            Rect cancelR{goR.x - ui.s(16) - ui.s(200), by, ui.s(200), ui.s(56)};
            cancel = ui.button("cancel", cancelR, "Annuler");
            float leftEnd = cancelR.x;
            // The keys of the powers of this run (the level's and the imposed ones), for this
            // player: the same as the level's « Touches… » (also in the pause menu).
            if (hasKeyPowers_ && level_) {
                Rect keysR{cancelR.x - ui.s(16) - ui.s(220), by, ui.s(220), ui.s(56)};
                UIContext::ButtonStyle ks;
                const std::string label = "Touches…";
                ks.fontSize = ui.fitFont(label, FontFace::Bold, 26, keysR.w - ui.s(24), 0.6f);
                bool openKeys = ui.button("keys", keysR, label, ks);
                ui.newBadge("keys", keysR, "challenges.keys");  // « NOUVEAU » (2.32.1)
                if (openKeys) g_.app.pushScene(makeLevelPowerKeys(g_, *level_, c.rules.powers));
                leftEnd = keysR.x;
            }
            std::string line = msgTimer_ > 0.0f ? msg_ : place_.problem;
            float room = leftEnd - ui.s(24) - x;
            if (!line.empty() && room > ui.s(120)) {
                float f = fit(ui, line, FontFace::Body, 17, room, 0.7f);
                if (ui.text().measure(line, FontFace::Body, ui.s(f)).x <= room) {
                    ui.label(line, {x, goR.center().y}, f, ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
                } else {
                    // Two lines in the room left of the buttons.
                    float h = ui.text().measureWrapped(line, FontFace::Body, ui.s(fz(ui, 15)), room).y;
                    ui.labelWrapped(line, {x, goR.center().y - h * 0.5f}, room, fz(ui, 15), ui.theme().warm);
                }
            }
        }
        // The tooltip of a reward, over everything else.
        if (rewardsShown_) rewardRow_.drawTooltip(g_);
        ui.popId();
        if (go) launch();
        else if (cancel || ui.backPressed()) g_.app.popScene();
        ui.end();
    }

private:
    void say(const std::string& m) {
        msg_ = m;
        msgTimer_ = 5.0f;
    }

    void drawLeft(const DailyChallenge& c, const Rect& area) {
        UIContext& ui = g_.ui;
        float x = area.x, w = area.w, y = area.y;
        // The level's preview (16:9), as high as the room allows.
        float ph = std::min(w * 9.0f / 16.0f, area.h * 0.5f);
        Rect prev{x, y, ph * 16.0f / 9.0f, ph};
        ui.r().fillRoundedRect(prev, ui.s(12), Color{11, 18, 38, 255});
        if (thumb_) ui.r().drawTexture(*thumb_, nullptr, prev.expanded(-ui.s(3)), Color::white());
        else fitLabel(ui, place_.localId.empty() ? "Niveau à télécharger" : "Aperçu indisponible", prev.center(), 17, prev.w - ui.s(20),
                      ui.theme().textDim, Align::Center);
        ui.r().drawRoundedRect(prev, ui.s(12), ui.theme().accent.withAlpha(90), std::max(1.0f, ui.s(1.5f)));
        y = prev.bottom() + ui.s(22);
        fitLabel(ui, "Paliers", {x, y + ui.s(11)}, 19, w, ui.theme().accent, Align::Left, FontFace::Bold);
        y += ui.s(32);
        // 2.35: under the tiers, the rewards of the challenges this run can advance (none for a
        // practice run; a run « hors délai » never counts for a series).
        std::vector<const CosmeticItem*> rewards;
        if (!practice_) rewards = challengeRewardsFor(g_.cosmetics, c.kindId(), late_);
        // 2.43: and first, this challenge's own rewards (none for a practice run or a late one).
        rewardRow_.setPrizes(g_, practice_ || late_ ? nullptr : &c);
        const float tileH = ui.s(76);
        float rewardsH = rewards.empty() && !rewardRow_.hasPrizes() ? 0.0f : ui.s(18) + rewardsSectionHeight(ui, tileH);
        // One row per tier: its name at the left, its condition at the right, « obtenu » mark.
        const ChallengeResult* res = practice_ ? nullptr : g_.challenges.result(c.id);
        const ChallengeTier tiers[3] = {ChallengeTier::Bronze, ChallengeTier::Silver, ChallengeTier::Gold};
        float rowH = std::min(ui.s(56), (area.bottom() - rewardsH - y - 2 * ui.s(10)) / 3.0f);
        if (rowH < ui.s(34) && rewardsH > 0.0f) {
            // Too low for both: the tiers first (the rewards stay on the challenge's card).
            rewards.clear();
            rewardRow_.setPrizes(g_, nullptr);
            rewardsH = 0.0f;
            rowH = std::min(ui.s(56), (area.bottom() - y - 2 * ui.s(10)) / 3.0f);
        }
        if (rowH < ui.s(34)) return;
        // The best tier reached here or, as the server knows it, elsewhere (2.41: an attempt's too).
        ChallengeTier have = res ? res->tier : ChallengeTier::None;
        if (!practice_)
            if (auto s = g_.challenges.serverResult(c.id)) have = std::max(have, s->tier);
        for (ChallengeTier t : tiers) {
            Rect r{x, y, w, rowH};
            bool got = have >= t;
            Color col = tierColor(t);
            ui.r().fillRoundedRect(r, ui.s(8), got ? col.withAlphaF(0.16f) : Color{12, 18, 38, 220});
            ui.r().drawRoundedRect(r, ui.s(8), col.withAlphaF(got ? 0.95f : 0.45f), std::max(1.0f, ui.s(1.2f)));
            std::string cond = tierCondition(c, t);
            if (got) cond += " · obtenu";
            float nameW = std::min(ui.s(110), r.w * 0.3f);
            fitLabel(ui, challengeTierLabel(t), {r.x + ui.s(14), r.center().y}, 19, nameW, col, Align::Left, FontFace::Bold);
            fitLabel(ui, cond, {r.right() - ui.s(14), r.center().y}, 16, r.w - nameW - ui.s(40), got ? col : ui.theme().text, Align::Right,
                     FontFace::Body, VAlign::Middle, 0.7f);
            y += rowH + ui.s(10);
        }
        rewardsShown_ = !rewards.empty() || rewardRow_.hasPrizes();
        if (rewardsShown_)
            if (const CosmeticItem* it = drawRewardsSection(g_, rewardRow_, rewards, x, y + ui.s(8), w, 19, tileH))
                g_.app.pushScene(makeCollectionOn(g_, it->id));
    }

    void drawRight(const DailyChallenge& c, const Rect& area) {
        UIContext& ui = g_.ui;
        float x = area.x, w = area.w, y = area.y;
        fitLabel(ui, "Ce qui change dans ce défi", {x, y + ui.s(12)}, 22, w, ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(40);
        // Under the rules: the ghost choice and the connection (what becomes of the result).
        const std::string conn = connectionLine(g_, practice_, late_, place_, c.finishObjective);
        const float connH = ui.s(32) + ui.text().measureWrapped(conn, FontFace::Body, ui.s(fz(ui, 17)), w - ui.s(24)).y;
        const float ghostH = ui.s(32 + 54);
        float rulesBottom = area.bottom() - ghostH - connH - ui.s(36);
        const float tagW = std::min(ui.s(210), w * 0.32f);
        for (const auto& [name, text] : ruleExplanations(g_, c, level_ ? &*level_ : nullptr)) {
            float textW = w - tagW - ui.s(14);
            float fs = fz(ui, 17);
            float h = ui.text().measureWrapped(text, FontFace::Body, ui.s(fs), textW).y;
            if (y + h > rulesBottom) {
                fs = fz(ui, 15);
                h = ui.text().measureWrapped(text, FontFace::Body, ui.s(fs), textW).y;
                if (y + h > rulesBottom) break;
            }
            Rect row{x, y - ui.s(6), w, h + ui.s(12)};
            ui.r().fillRoundedRect(row, ui.s(8), Color{12, 18, 38, 200});
            fitLabel(ui, name, {x + ui.s(12), y + ui.s(10)}, 16, tagW - ui.s(16), ui.theme().accent, Align::Left, FontFace::Bold);
            ui.labelWrapped(text, {x + tagW, y}, textW, fs, ui.theme().text);
            y += h + ui.s(20);
        }
        // The ghost: only the choices really available.
        float gy = y + ui.s(8);
        fitLabel(ui, "Fantôme", {x, gy + ui.s(12)}, 19, w, ui.theme().accent, Align::Left, FontFace::Bold);
        gy += ui.s(32);
        // The connection, under the ghost choice.
        {
            float cy = gy + ui.s(48) + ui.s(28);
            fitLabel(ui, practice_ ? "Résultat" : "Connexion", {x, cy + ui.s(12)}, 19, w, ui.theme().accent, Align::Left, FontFace::Bold);
            cy += ui.s(32);
            OnlineState st = g_.online.state();
            Color dot = practice_ ? ui.theme().warm : !g_.online.loggedIn() ? ui.theme().textDim : st == OnlineState::Online ? ui.theme().success : ui.theme().danger;
            ui.r().fillCircle({x + ui.s(7), cy + ui.s(11)}, ui.s(5), dot);
            ui.labelWrapped(conn, {x + ui.s(24), cy}, w - ui.s(24), fz(ui, 17), ui.theme().text);
        }
        std::vector<std::pair<int, std::string>> opts = {{0, "Aucun"}};
        if (bestMs_ > 0) opts.push_back({1, "Mon meilleur · " + formatRunTime(bestMs_)});
        if (!topRun_.empty()) opts.push_back({2, std::string(firstOfBoard(c.kind)) + " · " + formatRunTime(topMs_)});
        float gap = ui.s(12);
        float bw = (w - gap * static_cast<float>(opts.size() - 1)) / static_cast<float>(opts.size());
        for (size_t i = 0; i < opts.size(); ++i) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.selected = ghost_ == opts[i].first;
            st.fontSize = 17;
            if (ui.button("ghost" + std::to_string(opts[i].first), {x + (bw + gap) * static_cast<float>(i), gy, bw, ui.s(48)}, opts[i].second, st))
                ghost_ = opts[i].first;
        }
    }

    GhostEntry bestGhost() const {
        GhostEntry e;
        e.file = g_.challenges.bestReplayPath(challenge_->id);
        e.meta.source = "pb";
        e.meta.label = g_.online.loggedIn() && !g_.online.username().empty() ? g_.online.username() : "Moi";
        e.meta.levelId = place_.localId;
        e.meta.timeMs = bestMs_;
        if (auto rep = GhostStore::loadReplay(e.file)) e.meta.levelHash = rep->levelHash;
        return e;
    }

    void launch() {
        if (!challenge_ || !level_) return;
        // 2.60: changed (an administrator, nobody had a result on it) while this screen was open:
        // never its former version; its new one from « Défis ».
        if (g_.challenges.changedSince(*challenge_)) {
            g_.uiSound("ui_error");
            say("Ce défi vient de changer : revenez aux défis pour jouer sa nouvelle version.");
            return;
        }
        // Ended while this screen was open: the run is « hors délai » now (said once), or a
        // practice when the level installed is not the challenge's version.
        if (!practice_ && !late_ && g_.challenges.ended(*challenge_)) {
            late_ = true;
            practice_ = !place_.localId.empty() && !place_.sameVersion;
            g_.uiSound("ui_error");
            say(practice_ ? "Ce défi vient de se terminer : la partie sera hors classement."
                          : "Ce défi vient de se terminer : la partie sera hors délai (classement « Depuis »).");
            return;
        }
        PlayOptions o;
        o.challenge = PlayOptions::Challenge{*challenge_, practice_};
        o.ghostsChosen = true;
        if (ghost_ == 1) o.ghosts.push_back(bestGhost());
        if (ghost_ == 2) {
            // Downloaded first (kept for the next runs), then the run starts.
            downloadingGhost_ = true;
            g_.challenges.downloadRunReplay(challenge_->id, topRun_, [this, o, life = std::weak_ptr<int>(life_)](std::optional<fs::path> file, const std::string& why) mutable {
                if (!life.lock()) return;
                downloadingGhost_ = false;
                if (!file) {
                    ghost_ = 0;
                    topRun_.clear();
                    say(why.empty() ? std::string("Fantôme indisponible.") : why);
                    return;
                }
                GhostEntry e;
                e.file = *file;
                e.meta.source = "world";
                e.meta.label = topName_.empty() ? std::string(firstOfBoard(challenge_->kind)) : topName_;
                e.meta.levelId = place_.localId;
                e.meta.timeMs = topMs_;
                e.meta.validated = true;
                if (auto rep = GhostStore::loadReplay(e.file)) e.meta.levelHash = rep->levelHash;
                o.ghosts.push_back(std::move(e));
                g_.app.switchScene(makeGameScene(g_, place_.localId, std::move(o)), 0.4f);
            });
            return;
        }
        g_.app.switchScene(makeGameScene(g_, place_.localId, std::move(o)), 0.4f);
    }

    GameServices& g_;
    std::string id_;
    bool practice_ = false;
    bool late_ = false;  // the challenge is over: a run « hors délai »
    std::optional<DailyChallenge> challenge_;
    LevelPlace place_;
    std::optional<LevelData> level_;
    bool hasKeyPowers_ = false;  // the run has powers with a key of their own (« Touches… »)
    ChallengeRewardRow rewardRow_;  // the rewards this run can advance (2.35)
    bool rewardsShown_ = false;
    TexturePtr thumb_;
    int ghost_ = 0;  // 0 none, 1 my best, 2 the first of the day
    int64_t bestMs_ = 0;
    std::string topRun_, topName_;
    int64_t topMs_ = 0;
    bool downloadingGhost_ = false;
    std::string msg_;
    float msgTimer_ = 0.0f;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

// ======================================================================== the end of a run

class ChallengeEndScene : public Scene {
public:
    ChallengeEndScene(GameServices& g, GameScene& game, ChallengeEndData d) : g_(g), game_(game), d_(std::move(d)) {}
    bool isOverlay() const override { return true; }
    bool updatesSceneBelow() const override { return true; }

    void onEnter() override { g_.ui.setFocus("cend/again"); }

    void update(float dt) override {
        t_ += dt;
        // 2.35: once the server has verified the run, a reward of the challenges it unlocks (or
        // any reward not announced yet) gets the card « Nouvelles récompenses », as at the end
        // of a level. Checked again at each new reading of the server's results.
        if (t_ > 1.2f && g_.app.topScene() == this && g_.challenges.rewardsRevision() != rewardsSeen_) {
            rewardsSeen_ = g_.challenges.rewardsRevision();
            if (!g_.collection.fresh(g_.cosmetics, g_.cosmeticProgress()).empty()) g_.app.pushScene(makeNewRewards(g_));
        }
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        const float W = ui.screenW(), H = ui.screenH();
        ui.r().fillRect({0, 0, W, H}, Color{4, 6, 14, 180});
        ui.pushId("cend");
        // 2.60: the challenge changed (an administrator, nobody had a result on it) while it was
        // played, or since: this run was on its former version and counts for nothing. Said
        // instead of a success (no time « new best », no tier, no sending).
        const bool changed = !d_.practice && (d_.report.changed || (d_.attempt && d_.attempt->changed) || g_.challenges.changedSince(d_.challenge));
        const bool late = d_.completed && !d_.practice && !changed && d_.report.late;
        // « Plus loin » (2.33): an unfinished attempt, its progress (and, when it went further
        // than my best one, its sending state).
        const bool attempt = !changed && !d_.completed && d_.attempt.has_value();
        // Time, tier, (the streak: not for a run « hors délai »), the sending state; a fall:
        // why, (the attempt's progress and its sending state).
        const bool finishTier = attempt && d_.challenge.finishObjective;  // 2.41: an attempt's tier
        const int rows = changed       ? 1
                         : d_.completed ? (d_.practice ? 2 : late ? 3 : 4)
                                        : 1 + (attempt ? (d_.attempt->newBest ? 2 : 1) : 0) + (finishTier ? 1 : 0);
        // The sending state, followed live (and why, when it says more than the state).
        ChallengeRunState state = attempt ? d_.attempt->state : d_.report.state;
        std::string note = attempt ? d_.attempt->note : d_.completed && !d_.practice ? d_.report.note : std::string();
        if (const ChallengeSubmission* sub = g_.challenges.submission(d_.attemptId, attempt)) {
            state = sub->state;
            note = sub->note;
        }
        if (d_.practice) note = "Hors classement : ce temps n'est ni envoyé ni gardé comme meilleur temps.";
        // The notes under the rows, one after the other.
        std::vector<std::pair<std::string, Color>> notes;
        if (changed) notes.push_back({ChallengeService::changedNote(), ui.theme().warm});
        if (late) notes.push_back({"Hors délai : classé dans « Depuis », ne compte pas pour la série.", ui.theme().warm});
        if (!note.empty() && !changed) notes.push_back({note, ui.theme().textDim});
        float pw = std::min(ui.s(900), W - ui.s(80));
        const float w = pw - ui.s(72);
        float noteH = 0.0f;
        for (const auto& [t, c] : notes) noteH += ui.text().measureWrapped(t, FontFace::Body, ui.s(fz(ui, 16)), w).y + ui.s(10);
        if (noteH > 0.0f) noteH += ui.s(4);
        float ph = std::min(ui.s(30 + 56 + 24 + 44) + ui.s(64) * static_cast<float>(rows) + noteH + ui.s(24 + 60 + 30), H - ui.s(60));
        Rect p{(W - pw) * 0.5f, (H - ph) * 0.5f, pw, ph};
        ui.panel(p, true, 18);
        float x = p.x + ui.s(36), y = p.y + ui.s(30);
        std::string title = changed ? "DÉFI CHANGÉ" : !d_.completed ? "DÉFI ÉCHOUÉ" : d_.practice ? "ENTRAÎNEMENT TERMINÉ" : "DÉFI RÉUSSI";
        Color tc = changed ? ui.theme().warm : !d_.completed ? ui.theme().danger : d_.practice ? ui.theme().warm : ui.theme().success;
        fitLabel(ui, title, {p.center().x, y + ui.s(28)}, 44, w, tc, Align::Center, FontFace::Title, VAlign::Middle, 0.6f);
        y += ui.s(56) + ui.s(24);
        {
            // The level (a player's level: as its author named it), the challenge.
            std::string sub = levelTitle(g_, d_.challenge);
            std::string tail = "  ·  " + challengeTitle(d_.challenge, true) + (d_.practice ? "  ·  hors classement" : late ? "  ·  hors délai" : "");
            std::optional<i18n::Verbatim> v;
            if (d_.challenge.community()) v.emplace();
            float sw = std::min(ui.text().measure(sub, FontFace::Bold, ui.s(fz(ui, 20))).x, w * 0.5f);
            v.reset();
            float tw = std::min(ui.text().measure(tail, FontFace::Body, ui.s(fz(ui, 18))).x, w - sw);
            float sx = p.center().x - (sw + tw) * 0.5f;
            if (d_.challenge.community()) v.emplace();
            Vec2 s = fitLabel(ui, sub, {sx, y}, 20, sw, ui.theme().text, Align::Left, FontFace::Bold);
            v.reset();
            fitLabel(ui, tail, {sx + s.x, y}, 18, w - s.x - (sx - x), ui.theme().textDim);
        }
        y += ui.s(30);
        auto row = [&](const std::string& k, const std::string& v, Color c, const std::string& extra = {}, Color extraC = {}) {
            Rect rr{x, y, w, ui.s(54)};
            ui.r().fillRoundedRect(rr, ui.s(8), Color{12, 18, 38, 220});
            float vf = fit(ui, v, FontFace::Bold, 22, w * 0.42f, 0.7f);
            float vw = ui.text().measure(v, FontFace::Bold, ui.s(vf)).x;
            float ew = 0.0f;
            if (!extra.empty()) ew = std::min(chipWidth(ui, extra, ui.s(28)), w * 0.45f);
            float kroom = w - ui.s(36) - vw - ui.s(16) - (ew > 0 ? ew + ui.s(12) : 0.0f);
            fitLabel(ui, k, {rr.x + ui.s(18), rr.center().y}, 18, kroom, ui.theme().textDim);
            ui.label(v, {rr.right() - ui.s(18), rr.center().y}, vf, c, Align::Right, FontFace::Bold, VAlign::Middle);
            if (ew > 0.0f) chip(ui, extra, rr.right() - ui.s(18) - vw - ui.s(12) - ew, rr.center().y, ui.s(28), extraC, ew);
            y += ui.s(64);
        };
        if (changed) {
            // Only what happened, never a success of a version that no longer exists.
            if (d_.completed) row("Temps", formatRunTime(d_.timeMs), ui.theme().textDim);
            else row("Raison", d_.failReason.empty() ? std::string("Partie terminée") : d_.failReason, ui.theme().danger);
        } else if (!d_.completed) {
            std::string why = d_.failReason.empty() ? std::string("Partie terminée") : d_.failReason;
            row("Raison", why, ui.theme().danger);
            if (attempt) {
                const ChallengeService::AttemptReport& a = *d_.attempt;
                row("Progression de l'essai", percentText(a.progress), a.newBest ? ui.theme().success : ui.theme().accent,
                    a.newBest ? std::string("Nouveau record de progression") : std::string(), ui.theme().success);
                if (finishTier) {
                    // The server's tier once it has scored the replay, else this computer's reading.
                    ChallengeTier t = a.tier;
                    if (const ChallengeSubmission* sub = g_.challenges.submission(d_.attemptId, true); sub && sub->serverProgress >= 0.0f) t = sub->serverTier;
                    std::string next = t == ChallengeTier::Silver   ? std::string("Or en finissant")
                                       : t == ChallengeTier::Bronze ? "Argent à " + std::to_string(d_.challenge.silverProgress) + " %"
                                                                    : "Bronze à " + std::to_string(d_.challenge.bronzeProgress) + " %";
                    row("Palier atteint", t == ChallengeTier::None ? std::string("Aucun") : challengeTierLabel(t), tierColor(t), next, ui.theme().textDim);
                }
                if (a.newBest) row("Envoi", challengeRunStateLabel(state), stateColor(ui.theme(), state));
            }
        } else {
            std::string newBest = !d_.practice && d_.report.newBest ? "Nouveau meilleur temps" : std::string();
            row("Temps", formatRunTime(d_.timeMs), ui.theme().accent, newBest, ui.theme().success);
            std::string tier = challengeTierLabel(d_.tier);
            std::string dmg = d_.damageTaken == 0 ? std::string("sans dégât") : std::to_string(d_.damageTaken) + (d_.damageTaken > 1 ? " dégâts" : " dégât");
            row("Palier atteint", tier, tierColor(d_.tier), dmg, d_.damageTaken == 0 ? ui.theme().success : ui.theme().textDim);
            if (!d_.practice) {
                if (!late) {
                    ChallengeService::Streak s = g_.challenges.streak(d_.challenge.kind);
                    row(s.fromServer ? "Série (Cosmic Online)" : "Série (sur ce PC)", streakCount(d_.challenge.kind, s.current),
                        s.current > 0 ? ui.theme().warm : ui.theme().text);
                }
                row("Envoi", challengeRunStateLabel(state), stateColor(ui.theme(), state));
            }
        }
        for (const auto& [t, c] : notes) {
            Vec2 s = ui.labelWrapped(t, {x, y}, w, fz(ui, 16), c);
            y += s.y + ui.s(10);
        }
        // Rejouer, Revoir la partie, Retour aux défis.
        float by = p.bottom() - ui.s(30) - ui.s(60), gap = ui.s(16);
        float bw = (w - 2 * gap) / 3.0f;
        UIContext::ButtonStyle primary;
        primary.primary = true;
        // A changed challenge: its new version (from « Défis »), never its former one again.
        bool again = ui.button("again", {x, by, bw, ui.s(60)}, changed ? "Nouvelle version" : "Recommencer", primary);
        UIContext::ButtonStyle rs;
        rs.enabled = d_.completed && d_.replay != nullptr;
        bool watch = ui.button("watch", {x + bw + gap, by, bw, ui.s(60)}, "Revoir la partie", rs);
        bool back = ui.button("back", {x + 2 * (bw + gap), by, bw, ui.s(60)}, "Retour aux défis");
        ui.popId();
        InputManager& in = g_.app.input();
        if (again || in.keyPressed(SDL_SCANCODE_R) || in.rawPadButton(SDL_GAMEPAD_BUTTON_BACK)) {
            g_.app.popScene();
            game_.restart();
        } else if (watch && d_.replay) {
            g_.app.pushScene(makeLocalReplayViewer(g_, *d_.replay, "Votre partie – " + challengeTitle(d_.challenge, true)));
        } else if (back || ui.backPressed()) {
            game_.quitToMenu();
        }
        ui.end();
    }

private:
    GameServices& g_;
    GameScene& game_;
    ChallengeEndData d_;
    float t_ = 0.0f;
    int rewardsSeen_ = -1;  // the reading of the rewards already checked
};

}  // namespace

std::unique_ptr<Scene> makeChallenges(GameServices& g, int tab, const std::string& select) {
    return std::make_unique<ChallengesScene>(g, tab, select);
}

std::unique_ptr<Scene> makeChallengesOn(GameServices& g, const std::string& challengeId) {
    const DailyChallenge* c = g.challenges.find(challengeId);
    if (!c) return makeChallenges(g, challengeId.rfind("weekly-", 0) == 0 ? 1 : challengeId.rfind("monthly-", 0) == 0 ? 2 : 0);
    int tab = challengeTab(g.challenges, *c);
    return makeChallenges(g, tab, tab == 3 ? challengeId : std::string());
}

std::unique_ptr<Scene> makeChallengeStart(GameServices& g, const std::string& challengeId, bool practice) {
    return std::make_unique<ChallengeStartScene>(g, challengeId, practice);
}

std::unique_ptr<Scene> makeChallengeEnd(GameServices& g, GameScene& game, ChallengeEndData data) {
    return std::make_unique<ChallengeEndScene>(g, game, std::move(data));
}

std::unique_ptr<Scene> makeChallengeRun(GameServices& g, const std::string& challengeId, bool practice) {
    const DailyChallenge* c = g.challenges.find(challengeId);
    if (!c) return nullptr;
    LevelPlace place = levelPlace(g, *c);
    if (place.localId.empty() || !place.problem.empty()) return nullptr;
    PlayOptions o;
    o.challenge = PlayOptions::Challenge{*c, practice};
    o.ghostsChosen = true;
    return makeGameScene(g, place.localId, std::move(o));
}

}  // namespace cosmic
