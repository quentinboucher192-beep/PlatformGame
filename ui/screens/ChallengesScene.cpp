// Main menu → « Défis » (2.32, docs/online/DAILY_CHALLENGES.md): the challenge of the day
// (« Aujourd'hui »: level, countdown, rules, tiers, my best time and its state, Jouer / Revoir
// mon meilleur replay / S'entraîner, the day's leaderboard) and the past ones (« Archives »:
// my tier per day, Rejouer hors classement); before a run, what changes today, the tiers,
// the ghost and the connection; after it, the time, the tier, the streak and the sending
// state. The challenge stays playable offline until its end once downloaded; without an
// account the results stay on this computer.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/online/LevelFingerprint.h"
#include "packaging/PackageImporter.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/GameScene.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/Thumbnail.h"

namespace cosmic {

namespace {

/// Never below 11.5 pixels on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

/// The size (from `size`, never below 11.5 pixels) at which `t` fits `room`.
float fit(UIContext& ui, const std::string& t, FontFace face, float size, float room, float minScale = 0.75f) {
    return std::max(ui.fitFont(t, face, fz(ui, size), room, minScale), 11.5f / ui.s(1.0f));
}

/// `t` fitted (then shortened with « … » if needed) in `room` pixels, drawn; returns its size.
Vec2 fitLabel(UIContext& ui, const std::string& t, Vec2 pos, float size, float room, Color c, Align a = Align::Left,
              FontFace face = FontFace::Body, VAlign va = VAlign::Middle, float minScale = 0.75f) {
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

/// A rounded chip whose left edge is at `x`, centred on `cy`; returns its width.
float chip(UIContext& ui, const std::string& text, float x, float cy, float h, Color c, float maxW = 1e9f, bool filled = false) {
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

/// "5 h 12 min", "12 min 05 s", "45 s".
std::string countdown(int64_t ms) {
    int64_t s = std::max<int64_t>(0, ms / 1000);
    char b[48];
    if (s >= 3600) std::snprintf(b, sizeof b, "%lld h %02lld min", static_cast<long long>(s / 3600), static_cast<long long>((s / 60) % 60));
    else if (s >= 60) std::snprintf(b, sizeof b, "%lld min %02lld s", static_cast<long long>(s / 60), static_cast<long long>(s % 60));
    else std::snprintf(b, sizeof b, "%lld s", static_cast<long long>(s));
    return b;
}

/// "16 novembre 2026" (`year` false: "16 novembre").
std::string frenchDate(const std::string& ymd, bool year = true) {
    static const char* const kMonths[12] = {"janvier", "février", "mars",      "avril",   "mai",      "juin",
                                            "juillet", "août",    "septembre", "octobre", "novembre", "décembre"};
    int y = 0, m = 0, d = 0;
    if (std::sscanf(ymd.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3 || m < 1 || m > 12) return ymd;
    std::string out = std::to_string(d) + " " + kMonths[m - 1];
    if (year) out += " " + std::to_string(y);
    return out;
}

/// Where the challenge's level is on this computer.
struct LevelPlace {
    std::string localId;      // "" when it is not installed
    bool sameVersion = false; // the version of the challenge (its fingerprint)
    std::string problem;      // why it cannot be played ("" = it can)
};

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

/// "Glacior · niveau 2" (campaign) or "par <auteur> · niveau de la communauté" (the name as
/// its player wrote it: drawn verbatim by the caller).
std::string planetLine(GameServices& g, const DailyChallenge& c) {
    if (c.community()) return {};
    const LevelEntry* e = g.content.level(c.level.levelId);
    if (!e) return {};
    std::string out;
    if (const PlanetData* p = g.content.planet(e->planet)) out = p->name;
    auto list = g.content.levelsForPlanet(e->planet);
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i]->id == e->id) out += (out.empty() ? "" : " · ") + std::string("niveau ") + std::to_string(i + 1);
    return out;
}

/// The level's name as shown (campaign: the game's name, translated; community: verbatim).
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

/// The rules in short (chips).
std::vector<std::string> ruleChips(GameServices& g, const DailyChallenge& c) {
    const ChallengeRules& r = c.rules;
    std::vector<std::string> out;
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
        std::string cond = t == ChallengeTier::Bronze ? std::string("Arriver au bout")
                           : t == ChallengeTier::Silver ? "En " + formatRunTime(c.silverMs) + " ou moins"
                                                        : "En " + formatRunTime(c.goldMs) + " ou moins" + (c.goldNoDamage ? std::string(", sans dégât") : std::string());
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
            if (local != hash) return done(true, "Installé, mais cette copie diffère de la version du défi : vos temps n'y seront pas envoyés.");
            done(true, "Niveau installé : il reste jouable hors ligne.");
        },
        16u << 20);
}

/// Connection line of the screens (what happens to a run's result).
std::string connectionLine(GameServices& g, bool practice, const LevelPlace& place) {
    if (practice) return "Hors classement : rien n'est envoyé ni gardé comme meilleur temps.";
    if (!place.localId.empty() && !place.sameVersion) return "Votre version du niveau diffère de celle du défi : temps gardé sur ce PC.";
    if (!g.online.loggedIn()) return "Sans compte : votre résultat restera sur ce PC (ni classement ni envoi).";
    if (g.online.state() == OnlineState::Online)
        return "Connecté : votre temps sera envoyé à la fin du défi.";
    return "Hors ligne : votre temps partira à la prochaine connexion.";
}

// ======================================================================== the Défis screen

class ChallengesScene : public Scene {
public:
    ChallengesScene(GameServices& g, int tab, std::string select) : g_(g), ou_(g), tab_(std::clamp(tab, 0, 1)), selected_(std::move(select)) {}

    void onEnter() override {
        bg_.init(g_);
        g_.playMusic(g_.menuMusic());
        g_.challenges.refresh(true);
        if (g_.online.loggedIn()) g_.challenges.refreshMine();
        if (tab_ == 1) {
            if (g_.challenges.past().size() < 8 && !g_.challenges.archivesExhausted()) loadMore();
            g_.ui.setFocus(selected_.empty() ? "chal/tab1" : "chal/replay");
        } else {
            g_.ui.setFocus("chal/play");
        }
    }
    void onResume() override {
        g_.playMusic(g_.menuMusic());
        boardFor_.clear();  // a run may have changed it
    }

    void update(float dt) override {
        bg_.update(dt);
        t_ += dt;
        if (msgTimer_ > 0.0f) msgTimer_ -= dt;
        // The leaderboard of the day's challenge (logged in only), again from time to time.
        const DailyChallenge* c = g_.challenges.current();
        boardAge_ += dt;
        if (c && g_.online.loggedIn() && !boardBusy_ && (boardFor_ != c->id + "/" + scopeId() || boardAge_ > 60.0f)) fetchBoard(*c);
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.72f);
        ui.pushId("chal");
        const float W = ui.screenW(), H = ui.screenH(), m = ui.s(40);
        const float y0 = ui.s(26);
        // Header: title, the two tabs, the connection, Retour.
        float tf = 32.0f;
        Vec2 ts = ui.label("DÉFIS", {m, y0 + ui.s(24)}, tf, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        float x = m + ts.x + ui.s(28);
        static const char* const kTabs[2] = {"Aujourd'hui", "Archives"};
        static const char* const kTabFeatures[2] = {"challenges.tab.today", "challenges.tab.archives"};
        for (int i = 0; i < 2; ++i) {
            float w = ui.text().measure(kTabs[i], FontFace::Bold, ui.s(18)).x + ui.s(40);
            UIContext::ButtonStyle st;
            st.small = true;
            st.selected = tab_ == i;
            st.fontSize = 18;
            Rect tr{x, y0 + ui.s(4), w, ui.s(42)};
            if (ui.button("tab" + std::to_string(i), tr, kTabs[i], st) && tab_ != i) switchTab(i);
            ui.newBadge("tab" + std::to_string(i), tr, kTabFeatures[i], true, true);
            x += w + ui.s(10);
        }
        if (ui.input().menuPressed(Action::MenuTabNext) || ui.input().menuPressed(Action::MenuTabPrev)) switchTab(1 - tab_);
        UIContext::ButtonStyle bs;
        bs.small = true;
        Rect back{W - m - ui.s(150), y0 + ui.s(4), ui.s(150), ui.s(42)};
        bool leave = ui.button("back", back, "Retour", bs);
        drawConnection({x + ui.s(16), y0 + ui.s(25)}, back.x - ui.s(20) - (x + ui.s(16)));
        Rect body{m, ui.s(92), W - 2 * m, H - ui.s(92) - ui.s(26)};
        if (tab_ == 0) drawToday(body);
        else drawArchives(body);
        ui.popId();
        if (leave || ui.backPressed()) g_.app.switchScene(makeMainMenu(g_), 0.3f);
        ui.end();
    }

private:
    const char* scopeId() const { return scope_ == 0 ? "world" : scope_ == 1 ? "country" : "friends"; }

    void switchTab(int t) {
        tab_ = t;
        if (tab_ == 1 && g_.challenges.past().size() < 8 && !g_.challenges.archivesExhausted()) loadMore();
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

    // ------------------------------------------------------------------ Aujourd'hui

    void drawToday(const Rect& body) {
        UIContext& ui = g_.ui;
        const DailyChallenge* c = g_.challenges.current();
        if (!c) {
            drawNoChallenge(body);
            return;
        }
        const float gap = ui.s(22);
        Rect left{body.x, body.y, (body.w - gap) * 0.57f, body.h};
        Rect right{left.right() + gap, body.y, body.right() - left.right() - gap, body.h};
        drawChallengeCard(*c, left);
        drawBoard(*c, right);
    }

    void drawNoChallenge(const Rect& body) {
        UIContext& ui = g_.ui;
        Rect p = Rect::fromCenter(body.center(), {std::min(ui.s(900), body.w), std::min(ui.s(330), body.h)});
        ui.panel(p, true, 16);
        float x = p.x + ui.s(36), w = p.w - ui.s(72), y = p.y + ui.s(34);
        auto fs = g_.challenges.fetchState();
        std::string head = fs == ChallengeService::Fetch::Loading ? "Recherche du défi du jour…" : "Aucun défi du jour sur ce PC";
        fitLabel(ui, head, {x, y + ui.s(16)}, 28, w, ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(58);
        std::string text;
        if (!g_.online.usable())
            text = "Cosmic Online est indisponible dans cette version ou désactivé dans les paramètres : le défi du jour vient du serveur.";
        else if (fs == ChallengeService::Fetch::Loading)
            text = "Le défi est demandé au serveur.";
        else if (fs == ChallengeService::Fetch::Offline)
            text = "Hors ligne : connectez-vous à Internet pour recevoir le défi du jour. Une fois téléchargé, il reste jouable hors ligne jusqu'à sa fin.";
        else if (fs == ChallengeService::Fetch::Failed)
            text = "Le serveur n'a pas donné de défi : " + g_.challenges.fetchMessage();
        else
            text = "Le défi du jour vient du serveur ; une fois téléchargé, il reste jouable hors ligne jusqu'à sa fin.";
        Vec2 s = ui.labelWrapped(text, {x, y}, w, fz(ui, 18), ui.theme().textDim);
        y += s.y + ui.s(24);
        UIContext::ButtonStyle st;
        st.primary = true;
        st.enabled = g_.online.usable() && fs != ChallengeService::Fetch::Loading;
        float bw = std::min(w, ui.s(320));
        if (ui.button("play", {x, std::min(y, p.bottom() - ui.s(84)), bw, ui.s(56)}, "Réessayer", st)) g_.challenges.refresh(true);
        float ax = x + bw + ui.s(16);
        if (!g_.challenges.past().empty() && ax + ui.s(200) < p.right() - ui.s(36)) {
            if (ui.button("toarch", {ax, std::min(y, p.bottom() - ui.s(84)), std::min(ui.s(300), p.right() - ui.s(36) - ax), ui.s(56)}, "Voir les archives"))
                switchTab(1);
        }
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

    void drawChallengeCard(const DailyChallenge& c, const Rect& area) {
        UIContext& ui = g_.ui;
        ui.panel(area, false, 16);
        const float x = area.x + ui.s(28), w = area.w - ui.s(56);
        float y = area.y + ui.s(24);
        LevelPlace place = levelPlace(g_, c);
        // Line 1: the day, and the countdown in a pill at the right.
        std::string left = "Défi du " + frenchDate(c.date, false);
        std::string cd = "Se termine dans " + countdown(c.endsAtMs - g_.challenges.nowMs());
        float ph = ui.s(32);
        float pw = chipWidth(ui, cd, ph);
        float leftRoom = w - pw - ui.s(16);
        if (leftRoom < ui.s(120)) {
            // Narrow: the countdown under the day.
            fitLabel(ui, left, {x, y + ph * 0.5f}, 17, w, ui.theme().accent, Align::Left, FontFace::Bold);
            y += ph + ui.s(8);
            chip(ui, cd, x, y + ph * 0.5f, ph, ui.theme().warm, w);
        } else {
            fitLabel(ui, left, {x, y + ph * 0.5f}, 17, leftRoom, ui.theme().accent, Align::Left, FontFace::Bold);
            chip(ui, cd, x + w - pw, y + ph * 0.5f, ph, ui.theme().warm, pw);
        }
        y += ph + ui.s(14);
        // Below the level, everything has its height: the level's preview takes what is left.
        const float by = area.bottom() - ui.s(24) - ui.s(58);
        const std::vector<std::string> chips = ruleChips(g_, c);
        const float chipH = ui.s(32);
        const float below = ui.s(30) + chipRowsHeight(ui, chips, w, chipH) + ui.s(18)  // rules
                            + ui.s(30) + ui.s(96) + ui.s(18)                           // tiers
                            + ui.s(84) + ui.s(36);                                      // my best, note
        y += drawLevelHeader(c, place, x, y, w, std::clamp(by - y - below, ui.s(84), ui.s(330))) + ui.s(16);
        // The rules (chips).
        fitLabel(ui, "Règles du jour", {x, y + ui.s(11)}, 18, w, ui.theme().accent, Align::Left, FontFace::Bold);
        y += ui.s(30);
        y += drawChipRows(ui, chips, x, y, w, chipH, ui.theme().accent) + ui.s(18);
        // The tiers.
        const ChallengeResult* res = g_.challenges.result(c.id);
        fitLabel(ui, "Paliers", {x, y + ui.s(11)}, 18, w, ui.theme().accent, Align::Left, FontFace::Bold);
        y += ui.s(30);
        drawTierCards(ui, c, {x, y, w, ui.s(96)}, res ? res->tier : ChallengeTier::None, true);
        y += ui.s(96) + ui.s(18);
        // My best time, its state, the streak.
        Rect box{x, y, w, std::max(ui.s(60), std::min(ui.s(84), by - ui.s(16) - y))};
        drawMyBest(c, res, box);
        y = box.bottom() + ui.s(10);
        // A note (the level to download, another version, a message) above the buttons.
        std::string note = msgTimer_ > 0.0f ? msg_ : place.problem;
        if (note.empty() && !place.localId.empty() && !place.sameVersion)
            note = "Votre version du niveau diffère de celle du défi : vos temps resteront sur ce PC.";
        if (!note.empty() && by - y > ui.s(18)) fitLabel(ui, note, {x, (y + by) * 0.5f}, 16, w, ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle, 0.7f);
        // Jouer (or Télécharger le niveau), Revoir mon meilleur replay, S'entraîner.
        const float gap = ui.s(14);
        float b1 = w * 0.26f, b2 = w * 0.37f, b3 = w - b1 - b2 - 2 * gap;
        UIContext::ButtonStyle primary;
        primary.primary = true;
        bool needDownload = c.community() && place.localId.empty();
        primary.enabled = needDownload ? (g_.online.usable() && !downloading_) : place.problem.empty();
        std::string playLabel = needDownload ? (downloading_ ? "Téléchargement…" : "Télécharger") : "Jouer";
        if (ui.button("play", {x, by, b1, ui.s(58)}, playLabel, primary)) {
            if (needDownload) download(c);
            else g_.app.pushScene(makeChallengeStart(g_, c.id, false));
        }
        UIContext::ButtonStyle st;
        st.enabled = res && res->hasReplay && !place.localId.empty();
        if (ui.button("best", {x + b1 + gap, by, b2, ui.s(58)}, "Revoir mon meilleur replay", st)) watchBest(c);
        UIContext::ButtonStyle pr;
        pr.enabled = !place.localId.empty();
        if (ui.button("practice", {x + b1 + b2 + 2 * gap, by, b3, ui.s(58)}, "S'entraîner (hors classement)", pr))
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
        // Top line: the caption at the left, the streak at the right.
        ChallengeService::Streak s = g_.challenges.streak();
        std::string streak = "Série : " + std::to_string(s.current) + (s.current > 1 ? " jours" : " jour") + (s.fromServer ? "" : " (sur ce PC)");
        float sw = ui.text().measure(streak, FontFace::Bold, ui.s(fz(ui, 15))).x;
        sw = std::min(sw, w * 0.5f);
        fitLabel(ui, streak, {x + w, top}, 15, sw, s.current > 0 ? ui.theme().warm : ui.theme().textDim, Align::Right, FontFace::Bold);
        fitLabel(ui, "Mon meilleur temps", {x, top}, 15, w - sw - ui.s(16), ui.theme().textDim);
        if (!res || res->bestMs <= 0) {
            fitLabel(ui, "Pas encore de temps sur ce défi.", {x, bottom}, 19, w, ui.theme().text, Align::Left, FontFace::Bold);
            return;
        }
        Vec2 t = ui.label(formatRunTime(res->bestMs), {x, bottom}, fz(ui, 24), ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        float cx = x + t.x + ui.s(14);
        float h = ui.s(28);
        cx += chip(ui, challengeTierLabel(res->tier), cx, bottom, h, tierColor(res->tier), x + w - cx, true) + ui.s(10);
        auto [state, note] = bestState(c);
        std::string label = challengeRunStateLabel(state);
        if (cx < x + w - ui.s(60)) cx += chip(ui, label, cx, bottom, h, stateColor(ui.theme(), state), x + w - cx) + ui.s(10);
        if (!note.empty() && cx < x + w - ui.s(60)) fitLabel(ui, note, {cx, bottom}, 14, x + w - cx, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle, 0.7f);
    }

    /// The level's preview (rendered once per challenge), nullptr when it is not installed.
    TexturePtr thumbnail(const DailyChallenge& c, const LevelPlace& place) {
        auto it = thumbs_.find(c.id);
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
        if (t || !place.localId.empty()) thumbs_[c.id] = t;
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
        g_.app.pushScene(makeLocalReplayViewer(g_, std::move(*rep), "Mon meilleur temps · défi du " + frenchDate(c.date)));
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
        std::string key = c.id + "/" + scopeId();
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
        }));
    }

    void drawBoard(const DailyChallenge& c, const Rect& area) {
        UIContext& ui = g_.ui;
        ui.panel(area, false, 16);
        float x = area.x + ui.s(24), w = area.w - ui.s(48), y = area.y + ui.s(22);
        fitLabel(ui, "Classement du jour", {x, y + ui.s(16)}, 24, w, ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(48);
        if (!g_.online.loggedIn()) {
            std::string text = g_.online.usable()
                                   ? "Connectez-vous à Cosmic Online pour voir le classement du jour et y entrer. Sans compte, le défi se joue quand même : vos résultats restent sur ce PC."
                                   : "Le classement du jour vient de Cosmic Online, indisponible ici. Le défi se joue quand même : vos résultats restent sur ce PC.";
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
                {"Monde, Pays, Amis", "les meilleurs temps du jour, avec leur palier."},
                {"Vérifié", "le serveur a contrôlé le replay de la partie."},
                {"En attente de contrôle", "le temps est reçu, son replay pas encore contrôlé."},
                {"Hors ligne : à l'envoi", "la partie attend la connexion pour partir."},
            };
            for (const auto& [k, v] : lines) {
                if (y + ui.s(60) > area.bottom() - ui.s(16)) break;
                float cw = chip(ui, k, x, y + ui.s(14), ui.s(28), ui.theme().accent, w * 0.5f);
                Vec2 s = ui.labelWrapped(v, {x + cw + ui.s(12), y + ui.s(3)}, w - cw - ui.s(12), fz(ui, 16), ui.theme().textDim);
                y += std::max(ui.s(28), s.y) + ui.s(14);
            }
            return;
        }
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
        // The player's own line (its PENDING time too), at the bottom when there is one.
        const Json& me = board_.value("me", Json());
        bool hasMe = boardOk_ && me.is_object() && jget<int64_t>(me, "time_ms", 0) > 0;
        if (hasMe) list.h -= ui.s(64);
        if (boardBusy_ && !boardOk_) {
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
        const Json& entries = board_.value("entries", Json::array());
        if (!entries.is_array() || entries.empty()) {
            ui.labelWrapped(scope_ == 2 ? "Aucun ami n'a encore de temps vérifié sur ce défi." : "Aucun temps vérifié pour l'instant : soyez le premier !",
                            {x, y}, w, fz(ui, 17), ui.theme().textDim);
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
                drawBoardRow(e, row, jget<bool>(e, "is_me", false));
            }
            ui.endScroll();
        }
        if (hasMe) {
            Rect row{x, area.bottom() - ui.s(20) - ui.s(54), w, ui.s(50)};
            ui.r().fillRoundedRect(row, ui.s(8), ui.theme().accent.withAlpha(30));
            Json mine = me;
            mine["is_me"] = true;
            drawBoardRow(mine, row, true);
        }
        if (boardCached_) fitLabel(ui, "Hors ligne : dernier classement reçu", {x + w, area.y + ui.s(38)}, 14, w * 0.45f, ui.theme().warm, Align::Right);
    }

    void drawBoardRow(const Json& e, const Rect& row, bool me) {
        UIContext& ui = g_.ui;
        if (me) ui.r().drawRoundedRect(row, ui.s(8), ui.theme().accent.withAlpha(150), std::max(1.0f, ui.s(1.2f)));
        else ui.r().fillRoundedRect(row, ui.s(8), Color{12, 18, 38, 200});
        float cy = row.center().y, x = row.x + ui.s(12);
        int rank = jget<int>(e, "rank", 0);
        std::string rk = rank > 0 ? std::to_string(rank) : std::string("—");
        fitLabel(ui, rk, {x + ui.s(34), cy}, 18, ui.s(40), rank == 1 ? tierColor(ChallengeTier::Gold) : ui.theme().text, Align::Right, FontFace::Bold);
        x += ui.s(48);
        // Right side first: status, tier, time (each measured), then the name in what is left.
        float right = row.right() - ui.s(10);
        std::string status = jstr(e, "status", "VERIFIED");
        ChallengeRunState st = challengeRunStateFromServer(status);
        float h = ui.s(26);
        float sw = chipWidth(ui, challengeRunStateLabel(st), h);
        if (right - sw > x + ui.s(200)) {
            chip(ui, challengeRunStateLabel(st), right - sw, cy, h, stateColor(ui.theme(), st), sw);
            right -= sw + ui.s(8);
        }
        ChallengeTier tier = challengeTierFromJson(e.value("tier", Json()));
        if (tier != ChallengeTier::None) {
            float tw = chipWidth(ui, challengeTierLabel(tier), h);
            if (right - tw > x + ui.s(160)) {
                chip(ui, challengeTierLabel(tier), right - tw, cy, h, tierColor(tier), tw, true);
                right -= tw + ui.s(10);
            }
        }
        std::string time = formatRunTime(jget<int64_t>(e, "time_ms", 0));
        float tw = ui.text().measure(time, FontFace::Bold, ui.s(fz(ui, 18))).x;
        ui.label(time, {right, cy}, fz(ui, 18), ui.theme().text, Align::Right, FontFace::Bold, VAlign::Middle);
        right -= tw + ui.s(14);
        // The player: flag, then the name as they chose it (hidden in streaming mode).
        const Json& p = e.value("player", Json());
        std::string name = p.is_object() ? jstr(p, "username") : p.is_string() ? p.get<std::string>() : std::string();
        std::string country = p.is_object() ? jstr(p, "country_code") : std::string();
        if (!country.empty() && right - x > ui.s(80)) {
            ou_.drawFlag(country, {x, cy - ui.s(9), ui.s(26), ui.s(18)});
            x += ui.s(34);
        }
        std::string shown = me ? std::string("Vous") : ou_.displayName(name, rank, false);
        i18n::Verbatim pseudo;
        if (right - x > ui.s(24)) fitLabel(ui, me && !name.empty() ? name : shown, {x, cy}, 17, right - x, me ? ui.theme().accent : ui.theme().text);
    }

    // ------------------------------------------------------------------ Archives

    void loadMore() {
        g_.challenges.loadArchives([this, life = std::weak_ptr<int>(life_)](bool ok, const std::string& m) {
            if (!life.lock()) return;
            if (!ok && g_.online.state() == OnlineState::Offline) say("Hors ligne : les défis passés gardés sur ce PC restent là.");
            else if (!ok) say(m.empty() ? std::string("Archives indisponibles.") : m);
        });
    }

    void drawArchives(const Rect& body) {
        UIContext& ui = g_.ui;
        auto list = g_.challenges.past();
        const float gap = ui.s(22);
        Rect left{body.x, body.y, (body.w - gap) * 0.42f, body.h};
        Rect right{left.right() + gap, body.y, body.right() - left.right() - gap, body.h};
        ui.panel(left, false, 16);
        float x = left.x + ui.s(20), w = left.w - ui.s(40), y = left.y + ui.s(20);
        fitLabel(ui, "Défis passés", {x, y + ui.s(14)}, 22, w, ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(44);
        const DailyChallenge* sel = nullptr;
        for (const DailyChallenge* c : list)
            if (c->id == selected_) sel = c;
        if (!sel && !list.empty()) {
            sel = list.front();
            selected_ = sel->id;
        }
        bool more = g_.online.usable() && !g_.challenges.archivesExhausted();
        float bottomBar = more ? ui.s(64) : 0.0f;
        Rect view{x, y, w, left.bottom() - ui.s(16) - bottomBar - y};
        if (list.empty()) {
            std::string t = g_.challenges.archivesLoading() ? "Chargement des défis passés…"
                            : g_.online.usable()           ? "Aucun défi passé pour l'instant."
                                                           : "Aucun défi passé sur ce PC. Les archives viennent de Cosmic Online.";
            ui.labelWrapped(t, {x, y}, w, fz(ui, 17), ui.theme().textDim);
        } else {
            const float rowH = ui.s(78);
            ui.beginScroll("arch", view, rowH * static_cast<float>(list.size()));
            float ry = view.y - ui.scrollOffset();
            for (size_t i = 0; i < list.size(); ++i) {
                const DailyChallenge& c = *list[i];
                Rect row{view.x, ry + rowH * static_cast<float>(i), view.w - ui.s(12), rowH - ui.s(8)};
                if (row.bottom() < view.y - rowH || row.y > view.bottom() + rowH) {
                    ui.hotspot("row" + c.id, row);  // still in the focus order
                    continue;
                }
                bool hovered = false;
                if (ui.hotspot("row" + c.id, row, &hovered)) selected_ = c.id;
                if (ui.focused("row" + c.id)) selected_ = c.id;
                bool on = selected_ == c.id;
                ui.r().fillRoundedRect(row, ui.s(10), on ? Color{28, 64, 84, 240} : hovered ? Color{20, 30, 56, 230} : Color{12, 18, 38, 220});
                if (on || ui.focused("row" + c.id)) ui.r().drawRoundedRect(row, ui.s(10), ui.theme().accent.withAlpha(180), std::max(1.0f, ui.s(1.5f)));
                const ChallengeResult* res = g_.challenges.result(c.id);
                float rx = row.x + ui.s(16), rroom = row.w - ui.s(32);
                // My tier at the right (or « — » when I did not finish it).
                ChallengeTier t = res ? res->tier : ChallengeTier::None;
                float h = ui.s(28);
                std::string tl = t == ChallengeTier::None ? std::string("Non joué") : challengeTierLabel(t);
                float tw = chipWidth(ui, tl, h);
                chip(ui, tl, row.right() - ui.s(16) - tw, row.center().y, h, tierColor(t), tw, t != ChallengeTier::None);
                rroom -= tw + ui.s(14);
                fitLabel(ui, frenchDate(c.date), {rx, row.y + row.h * 0.32f}, 17, rroom, ui.theme().accent, Align::Left, FontFace::Bold);
                std::optional<i18n::Verbatim> v;
                if (c.community()) v.emplace();
                fitLabel(ui, levelTitle(g_, c), {rx, row.y + row.h * 0.70f}, 17, rroom, ui.theme().text);
            }
            ui.endScroll();
        }
        if (more) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.enabled = !g_.challenges.archivesLoading();
            if (ui.button("more", {x, left.bottom() - ui.s(16) - ui.s(48), w, ui.s(48)},
                          g_.challenges.archivesLoading() ? "Chargement…" : "Charger les défis précédents", st))
                loadMore();
        }
        drawArchiveDetail(sel, right);
    }

    void drawArchiveDetail(const DailyChallenge* c, const Rect& area) {
        UIContext& ui = g_.ui;
        ui.panel(area, false, 16);
        float x = area.x + ui.s(28), w = area.w - ui.s(56), y = area.y + ui.s(24);
        if (!c) {
            ui.labelWrapped("Choisissez un défi dans la liste.", {x, y}, w, fz(ui, 17), ui.theme().textDim);
            return;
        }
        LevelPlace place = levelPlace(g_, *c);
        fitLabel(ui, "Défi du " + frenchDate(c->date), {x, y + ui.s(12)}, 17, w, ui.theme().accent, Align::Left, FontFace::Bold);
        y += ui.s(38);
        const float by = area.bottom() - ui.s(24) - ui.s(58);
        const std::vector<std::string> chips = ruleChips(g_, *c);
        const float chipH = ui.s(30);
        const float below = chipRowsHeight(ui, chips, w, chipH) + ui.s(18) + ui.s(92) + ui.s(18) + ui.s(80) + ui.s(50);
        y += drawLevelHeader(*c, place, x, y, w, std::clamp(by - y - below, ui.s(84), ui.s(330))) + ui.s(18);
        y += drawChipRows(ui, chips, x, y, w, chipH, ui.theme().accent) + ui.s(18);
        const ChallengeResult* res = g_.challenges.result(c->id);
        drawTierCards(ui, *c, {x, y, w, ui.s(92)}, res ? res->tier : ChallengeTier::None, true);
        y += ui.s(92) + ui.s(18);
        // My result that day.
        Rect box{x, y, w, std::max(ui.s(56), std::min(ui.s(80), by - ui.s(46) - y))};
        ui.r().fillRoundedRect(box, ui.s(10), Color{12, 18, 38, 220});
        float bx = box.x + ui.s(18), bw = box.w - ui.s(36), cy = box.center().y;
        if (!res || res->bestMs <= 0) {
            fitLabel(ui, "Vous n'avez pas fini ce défi le jour même.", {bx, cy}, 18, bw, ui.theme().textDim);
        } else {
            Vec2 l = ui.label("Mon temps : ", {bx, cy}, fz(ui, 18), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            Vec2 t = ui.label(formatRunTime(res->bestMs), {bx + l.x, cy}, fz(ui, 22), ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            float cx = bx + l.x + t.x + ui.s(14);
            ChallengeRunState st = ChallengeRunState::LocalOnly;
            if (const ChallengeSubmission* s = g_.challenges.bestSubmission(c->id); s && g_.online.loggedIn() && s->ownerId == g_.online.playerId()) st = s->state;
            else if (auto ss = g_.challenges.serverStatus(c->id)) st = *ss;
            if (cx < bx + bw - ui.s(60)) chip(ui, challengeRunStateLabel(st), cx, cy, ui.s(28), stateColor(ui.theme(), st), bx + bw - cx);
        }
        y = box.bottom() + ui.s(8);
        std::string note = msgTimer_ > 0.0f ? msg_ : place.problem;
        if (note.empty() && !place.localId.empty() && !place.sameVersion) note = "Le niveau a changé depuis ce défi : la partie se joue sur votre version.";
        if (note.empty()) note = "Rejouer un défi passé ne compte pas : hors classement, rien n'est envoyé.";
        if (by - y > ui.s(30)) fitLabel(ui, note, {x, y + ui.s(16)}, 15, w, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle, 0.7f);
        const float gap = ui.s(14);
        float b1 = (w - gap) * 0.5f;
        bool needDownload = c->community() && place.localId.empty();
        UIContext::ButtonStyle primary;
        primary.primary = true;
        primary.enabled = needDownload ? (g_.online.usable() && !downloading_) : place.problem.empty();
        std::string label = needDownload ? (downloading_ ? "Téléchargement…" : "Télécharger le niveau") : "Rejouer (hors classement)";
        if (ui.button("replay", {x, by, b1, ui.s(58)}, label, primary)) {
            if (needDownload) download(*c);
            else g_.app.pushScene(makeChallengeStart(g_, c->id, true));
        }
        UIContext::ButtonStyle st;
        st.enabled = res && res->hasReplay && !place.localId.empty();
        if (ui.button("archbest", {x + b1 + gap, by, w - b1 - gap, ui.s(58)}, "Revoir mon meilleur replay", st)) watchBest(*c);
    }

    GameServices& g_;
    OnlineUi ou_;
    MenuBackground bg_;
    int tab_ = 0;
    std::string selected_;
    int scope_ = 0;
    Json board_;
    std::string boardFor_;
    std::string boardMsg_, boardCode_;
    bool boardBusy_ = false, boardOk_ = false, boardOffline_ = false, boardCached_ = false;
    float boardAge_ = 0.0f;
    bool downloading_ = false;
    std::map<std::string, TexturePtr> thumbs_;
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
                    const Json& entries = r.data.value("entries", Json::array());
                    if (!entries.is_array() || entries.empty()) return;
                    const Json& e = entries.front();
                    if (jget<bool>(e, "is_me", false) || !jget<bool>(e, "has_replay", true) || jstr(e, "run_id").empty()) return;
                    topRun_ = jstr(e, "run_id");
                    topMs_ = jget<int64_t>(e, "time_ms", 0);
                    const Json& p = e.value("player", Json());
                    topName_ = p.is_object() ? jstr(p, "username") : std::string();
                }));
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
        if (!challenge_) {
            fitLabel(ui, "Ce défi n'est plus sur ce PC.", {x, y + ui.s(20)}, 26, w, ui.theme().danger, Align::Left, FontFace::Bold);
            cancel = ui.button("cancel", {panel.right() - ui.s(32) - ui.s(220), panel.bottom() - ui.s(80), ui.s(220), ui.s(54)}, "Retour");
        } else {
            const DailyChallenge& c = *challenge_;
            std::string title = practice_ ? "ENTRAÎNEMENT — HORS CLASSEMENT" : "Défi du " + frenchDate(c.date, false);
            fitLabel(ui, title, {x, y + ui.s(20)}, 32, w, practice_ ? ui.theme().warm : ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle, 0.6f);
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
            go = ui.button("go", goR, downloadingGhost_ ? "Fantôme…" : practice_ ? "Lancer l'entraînement" : "Lancer le défi", primary);
            Rect cancelR{goR.x - ui.s(16) - ui.s(200), by, ui.s(200), ui.s(56)};
            cancel = ui.button("cancel", cancelR, "Annuler");
            std::string line = msgTimer_ > 0.0f ? msg_ : place_.problem;
            float room = cancelR.x - ui.s(24) - x;
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
        // One row per tier: its name at the left, its condition at the right, « obtenu » mark.
        const ChallengeResult* res = practice_ ? nullptr : g_.challenges.result(c.id);
        const ChallengeTier tiers[3] = {ChallengeTier::Bronze, ChallengeTier::Silver, ChallengeTier::Gold};
        float rowH = std::min(ui.s(56), (area.bottom() - y - 2 * ui.s(10)) / 3.0f);
        if (rowH < ui.s(34)) return;
        for (ChallengeTier t : tiers) {
            Rect r{x, y, w, rowH};
            bool got = res && res->tier >= t;
            Color col = tierColor(t);
            ui.r().fillRoundedRect(r, ui.s(8), got ? col.withAlphaF(0.16f) : Color{12, 18, 38, 220});
            ui.r().drawRoundedRect(r, ui.s(8), col.withAlphaF(got ? 0.95f : 0.45f), std::max(1.0f, ui.s(1.2f)));
            std::string cond = t == ChallengeTier::Bronze ? std::string("Arriver au bout")
                               : t == ChallengeTier::Silver ? "En " + formatRunTime(c.silverMs) + " ou moins"
                                                            : "En " + formatRunTime(c.goldMs) + " ou moins" + (c.goldNoDamage ? std::string(", sans dégât") : std::string());
            if (got) cond += " · obtenu";
            float nameW = std::min(ui.s(110), r.w * 0.3f);
            fitLabel(ui, challengeTierLabel(t), {r.x + ui.s(14), r.center().y}, 19, nameW, col, Align::Left, FontFace::Bold);
            fitLabel(ui, cond, {r.right() - ui.s(14), r.center().y}, 16, r.w - nameW - ui.s(40), got ? col : ui.theme().text, Align::Right,
                     FontFace::Body, VAlign::Middle, 0.7f);
            y += rowH + ui.s(10);
        }
    }

    void drawRight(const DailyChallenge& c, const Rect& area) {
        UIContext& ui = g_.ui;
        float x = area.x, w = area.w, y = area.y;
        fitLabel(ui, "Ce qui change aujourd'hui", {x, y + ui.s(12)}, 22, w, ui.theme().text, Align::Left, FontFace::Bold);
        y += ui.s(40);
        // Under the rules: the ghost choice and the connection (what becomes of the result).
        const std::string conn = connectionLine(g_, practice_, place_);
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
        if (!topRun_.empty()) opts.push_back({2, "Le 1er du jour · " + formatRunTime(topMs_)});
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
        // Ended while this screen was open: the run can only be a practice now.
        if (!practice_ && g_.challenges.nowMs() >= challenge_->endsAtMs) {
            practice_ = true;
            g_.uiSound("ui_error");
            say("Ce défi vient de se terminer : la partie sera hors classement.");
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
                e.meta.label = topName_.empty() ? std::string("1er du jour") : topName_;
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
    std::optional<DailyChallenge> challenge_;
    LevelPlace place_;
    std::optional<LevelData> level_;
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

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        const float W = ui.screenW(), H = ui.screenH();
        ui.r().fillRect({0, 0, W, H}, Color{4, 6, 14, 180});
        ui.pushId("cend");
        const int rows = d_.completed ? (d_.practice ? 2 : 4) : 1;
        // The sending state, followed live (and why, when it says more than the state).
        ChallengeRunState state = d_.report.state;
        std::string note = d_.completed && !d_.practice ? d_.report.note : std::string();
        if (const ChallengeSubmission* sub = g_.challenges.submission(d_.attemptId)) {
            state = sub->state;
            note = sub->note;
        }
        if (d_.practice) note = "Hors classement : ce temps n'est ni envoyé ni gardé comme meilleur temps.";
        float pw = std::min(ui.s(900), W - ui.s(80));
        const float w = pw - ui.s(72);
        float noteH = note.empty() ? 0.0f : ui.text().measureWrapped(note, FontFace::Body, ui.s(fz(ui, 16)), w).y + ui.s(14);
        float ph = std::min(ui.s(30 + 56 + 24 + 44) + ui.s(64) * static_cast<float>(rows) + noteH + ui.s(24 + 60 + 30), H - ui.s(60));
        Rect p{(W - pw) * 0.5f, (H - ph) * 0.5f, pw, ph};
        ui.panel(p, true, 18);
        float x = p.x + ui.s(36), y = p.y + ui.s(30);
        std::string title = !d_.completed ? "DÉFI ÉCHOUÉ" : d_.practice ? "ENTRAÎNEMENT TERMINÉ" : "DÉFI RÉUSSI";
        Color tc = !d_.completed ? ui.theme().danger : d_.practice ? ui.theme().warm : ui.theme().success;
        fitLabel(ui, title, {p.center().x, y + ui.s(28)}, 44, w, tc, Align::Center, FontFace::Title, VAlign::Middle, 0.6f);
        y += ui.s(56) + ui.s(24);
        {
            // The level (a player's level: as its author named it), the day.
            std::string sub = levelTitle(g_, d_.challenge);
            std::string tail = "  ·  défi du " + frenchDate(d_.challenge.date) + (d_.practice ? "  ·  hors classement" : "");
            std::optional<i18n::Verbatim> v;
            if (d_.challenge.community()) v.emplace();
            float sw = std::min(ui.text().measure(sub, FontFace::Bold, ui.s(fz(ui, 20))).x, w * 0.6f);
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
            if (!extra.empty()) ew = std::min(chipWidth(ui, extra, ui.s(28)), w * 0.3f);
            float kroom = w - ui.s(36) - vw - ui.s(16) - (ew > 0 ? ew + ui.s(12) : 0.0f);
            fitLabel(ui, k, {rr.x + ui.s(18), rr.center().y}, 18, kroom, ui.theme().textDim);
            ui.label(v, {rr.right() - ui.s(18), rr.center().y}, vf, c, Align::Right, FontFace::Bold, VAlign::Middle);
            if (ew > 0.0f) chip(ui, extra, rr.right() - ui.s(18) - vw - ui.s(12) - ew, rr.center().y, ui.s(28), extraC, ew);
            y += ui.s(64);
        };
        if (!d_.completed) {
            std::string why = d_.failReason.empty() ? std::string("Partie terminée") : d_.failReason;
            row("Raison", why, ui.theme().danger);
        } else {
            std::string newBest = !d_.practice && d_.report.newBest ? "Nouveau meilleur temps" : std::string();
            row("Temps", formatRunTime(d_.timeMs), ui.theme().accent, newBest, ui.theme().success);
            std::string tier = challengeTierLabel(d_.tier);
            std::string dmg = d_.damageTaken == 0 ? std::string("sans dégât") : std::to_string(d_.damageTaken) + (d_.damageTaken > 1 ? " dégâts" : " dégât");
            row("Palier atteint", tier, tierColor(d_.tier), dmg, d_.damageTaken == 0 ? ui.theme().success : ui.theme().textDim);
            if (!d_.practice) {
                ChallengeService::Streak s = g_.challenges.streak();
                std::string sv = std::to_string(s.current) + (s.current > 1 ? " jours" : " jour");
                row(s.fromServer ? "Série (Cosmic Online)" : "Série (sur ce PC)", sv, s.current > 0 ? ui.theme().warm : ui.theme().text);
                row("Envoi", challengeRunStateLabel(state), stateColor(ui.theme(), state));
            }
        }
        if (!note.empty()) ui.labelWrapped(note, {x, y}, w, fz(ui, 16), ui.theme().textDim);
        // Rejouer, Revoir la partie, Retour aux défis.
        float by = p.bottom() - ui.s(30) - ui.s(60), gap = ui.s(16);
        float bw = (w - 2 * gap) / 3.0f;
        UIContext::ButtonStyle primary;
        primary.primary = true;
        bool again = ui.button("again", {x, by, bw, ui.s(60)}, "Recommencer", primary);
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
            g_.app.pushScene(makeLocalReplayViewer(g_, *d_.replay, "Votre partie · défi du " + frenchDate(d_.challenge.date)));
        } else if (back || ui.backPressed()) {
            game_.quitToMenu();
        }
        ui.end();
    }

private:
    GameServices& g_;
    GameScene& game_;
    ChallengeEndData d_;
};

}  // namespace

std::unique_ptr<Scene> makeChallenges(GameServices& g, int tab, const std::string& select) {
    return std::make_unique<ChallengesScene>(g, tab, select);
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
