// 2.81.2 (titan 11, docs/RAIDS.md « Mécanismes et embuscades »): the co-op mechanisms of Asterion — the proof:
//
// * every mechanism (a lock's plate, a goods lift, the twin plates) waits for its players AND for every ambush of
//   its zone laid before it (« Embuscade à vaincre d'abord »), never for one only it leads to (no deadlock);
// * a goods lift that needs N players stays still with fewer on it (it was « triggered »: the first rider started it);
// * with a mechanism inactive (those before it working), the next mechanism (or the way out) cannot be reached by ANY
//   move the simulated players know (raid::NavGraph: jumps, double and triple jump, dash, impulse, wall jumps) — and
//   can once it works; the walls of its doors go up to the top of the level (no wall jumps over them);
// * the pill over a plate, the raid's tabs (one width), the HUD's button (the key alone), « Raid à tester »: measured;
// * the contributions table: every part and the heart chosen in every state (not discovered, no group, fighting,
//   fallen) with the mouse, the keys and the gamepad, a row opened each time: no crash, a clear empty state.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "engine/core/Scene.h"
#include "engine/input/InputManager.h"
#include "engine/physics/PhysicsWorld.h"
#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"
#include "game/online/Raids.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidNav.h"
#include "game/raid/RaidRun.h"
#include "tests/TestFramework.h"
#include "ui/online/ContribTable.h"
#include "ui/online/RaidHud.h"
#include "ui/online/RaidMenu.h"
#include "ui/online/RaidWidgets.h"
#include "ui/widgets/AsterionFigure.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;
using namespace cosmic::raid;

namespace {

struct Svc {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    Svc() {
        content.reload();
        res.init(nullptr, &audio);
    }
    WorldServices ws() { return WorldServices{res, audio, content, nullptr, 0.0f, 1.0f}; }
};

uint32_t allBits(const Branch& br) {
    uint32_t s = 0;
    for (const ObjectiveInfo& o : br.objectives) s |= 1u << o.index;
    for (const EffectInfo& e : br.effects)
        if (e.bit >= 0 && e.bit < 32) s |= 1u << e.bit;
    return s;
}

std::unique_ptr<RaidRun> zoneRun(Svc& s, const Branch& br, int zone, uint32_t state, int players = 2) {
    RaidRun::Test t;
    t.on = true;
    t.invincible = true;
    t.players = players;
    auto run = std::make_unique<RaidRun>(s.ws(), br, kMaxInstancePlayers);
    run->setTest(t);
    run->setQuiet(true);
    if (!run->begin(state, zone)) return nullptr;
    return run;
}

/// One step: seats 0 and 1 in play standing still, the others vacant.
void stepIdle(RaidRun& run, uint32_t state) {
    InputFrame frames[kMaxInstancePlayers];
    for (int s = 0; s < kMaxInstancePlayers; ++s) frames[s].flags = s < 2 ? 0u : InputFrame::Vacant;
    run.step(frames, kMaxInstancePlayers, state);
}

/// The zones of Asterion with co-op mechanisms on the way (the expeditions, the heart's aorta, the Poignet-verrou).
struct ZoneRef {
    const Branch* br;
    int zone;
};
std::vector<ZoneRef> mechanismZones() {
    std::vector<ZoneRef> out;
    for (const Branch* b : Branch::all())
        for (const ZoneInfo& z : b->zones)
            if (const ZoneRules* r = b->zoneRules(z.id); r && !r->coops.empty()) out.push_back({b, z.id});
    return out;
}

/// The ambushes of a zone (its objectives of role « ambush »): objective index → its arena's rectangle.
std::map<int, Rect> ambushArenas(const Branch& br, const ZoneRules& rules, GameWorld& w) {
    std::map<int, Rect> out;
    for (const ZoneObjective& zo : rules.objectives) {
        if (zo.index < 0 || br.objectives[static_cast<size_t>(zo.index)].role != "ambush") continue;
        if (LevelEntity* e = w.entity(zo.source)) out[zo.index] = e->box;
    }
    return out;
}

/// The mechanisms of a zone in the order of the way (west to east), twin plates as one: the indices of their plates.
std::vector<std::vector<size_t>> mechanisms(const ZoneRules& rules) {
    std::vector<std::vector<size_t>> out;
    std::set<std::string> groups;
    for (size_t k = 0; k < rules.coops.size(); ++k) {
        const CoopPlate& c = rules.coops[k];
        if (c.targets.empty()) continue;
        bool passage = false;  // the armory's « Prêts »: a passage of the group, no way to block
        for (const Passage& p : rules.passages) passage = passage || p.plate == c.id;
        if (passage) continue;
        if (!c.group.empty() && !groups.insert(c.group).second) continue;
        std::vector<size_t> m{k};
        for (size_t j = k + 1; j < rules.coops.size(); ++j)
            if (!c.group.empty() && rules.coops[j].group == c.group) m.push_back(j);
        out.push_back(m);
    }
    std::sort(out.begin(), out.end(), [&](const auto& a, const auto& b) { return rules.coops[a[0]].rect.x < rules.coops[b[0]].rect.x; });
    return out;
}

/// Makes a mechanism work as its plate does (its doors open, its lift running).
void work(GameWorld& w, const ZoneRules& rules, const std::vector<size_t>& m) {
    for (size_t k : m)
        for (const std::string& t : rules.coops[k].targets)
            if (LevelEntity* e = w.entity(t)) {
                if (auto* d = dynamic_cast<DoorEntity*>(e)) d->setOpen(w, true, true);
                else e->handleAction(w, rules.coops[k].action, Json::object());
            }
}

}  // namespace

// ============================================================================ the rules of a mechanism

TEST_CASE("raid mechanisms (2.81.2): every co-op mechanism waits for every ambush of its zone laid before it, never one it alone leads to") {
    Svc s;
    int checked = 0;
    for (const ZoneRef& zr : mechanismZones()) {
        auto run = zoneRun(s, *zr.br, zr.zone, allBits(*zr.br));
        CHECK(run);
        if (!run) continue;
        const ZoneRules& rules = run->rules();
        const std::map<int, Rect> arenas = ambushArenas(*zr.br, rules, run->world());
        for (const CoopPlate& c : rules.coops) {
            // Its list: exactly the ambushes west of its plate (reached without it), each a known objective.
            std::set<int> want, have;
            for (const auto& [idx, box] : arenas)
                if (box.right() <= c.rect.x + 0.5f) want.insert(idx);
            for (int a : c.ambushIndex) {
                CHECK(a >= 0);
                have.insert(a);
            }
            if (want != have) std::printf("  %s zone %d « %s » : embuscades attendues %zu, lues %zu\n", zr.br->part.c_str(), zr.zone, c.id.c_str(), want.size(), have.size());
            CHECK(want == have);
            for (int a : have) CHECK(arenas.at(a).right() <= c.rect.x + 0.5f);  // never one east of it (only it leads there)
            ++checked;
        }
        // The expeditions: every mechanism comes after an ambush (the user's « 1 / 3 » at the start waits for the first one).
        if (!arenas.empty())
            for (const CoopPlate& c : rules.coops)
                if (c.group.empty() || c.group == rules.coops.front().group) CHECK(!c.ambushIndex.empty());
    }
    std::printf("  %d mécanismes vérifiés (7 expéditions et l'aorte du Cœur)\n", checked);
    CHECK(checked >= 8 * 3);
}

TEST_CASE("raid mechanisms (2.81.2): a plate held does nothing while an ambush before it is left (« Embuscade à vaincre d'abord »), then works") {
    Svc s;
    for (const char* part : {"crane", "bras_droit", "coeur"}) {
        const Branch* br = nullptr;
        for (const Branch* b : Branch::all())
            if (b->part == part) br = b;
        CHECK(br != nullptr);
        if (!br) continue;
        const int zone = br->part == "coeur" ? 1 : 7;
        uint32_t noAmbush = allBits(*br);
        for (const ObjectiveInfo& o : br->objectives)
            if (o.zone == zone && o.role == "ambush") noAmbush &= ~(1u << o.index);
        for (uint32_t state : {noAmbush, allBits(*br)}) {
            auto run = zoneRun(s, *br, zone, state, 1);
            CHECK(run);
            if (!run) continue;
            const ZoneRules& rules = run->rules();
            const auto mech = mechanisms(rules);
            CHECK(!mech.empty());
            // Its first mechanism, its plates held by the one player in play (one is enough alone).
            const size_t k = mech.front().front();
            GameWorld& w = run->world();
            w.restoreMember(1, w.carryOf(1), rules.coops[k].rect.bottomCenter());
            for (int t = 0; t < 30; ++t) {
                InputFrame frames[kMaxInstancePlayers];
                for (int q = 0; q < kMaxInstancePlayers; ++q) frames[q].flags = q == 1 ? 0u : InputFrame::Vacant;
                run->step(frames, kMaxInstancePlayers, state);
            }
            const RaidRun::CoopView v = run->coop(k);
            if (state == noAmbush) {
                CHECK(v.ambush);
                CHECK(!v.done);
                CHECK(v.on >= v.need);  // held: only the ambush is missing
            } else {
                CHECK(!v.ambush);
                CHECK(v.done);
            }
        }
    }
}

TEST_CASE("raid mechanisms (2.81.2): a goods lift that needs several players stays still with fewer on it, then starts with them") {
    Svc s;
    int lifts = 0;
    for (const ZoneRef& zr : mechanismZones()) {
        auto run = zoneRun(s, *zr.br, zr.zone, allBits(*zr.br), 2);
        CHECK(run);
        if (!run) continue;
        const ZoneRules& rules = run->rules();
        for (size_t k = 0; k < rules.coops.size(); ++k) {
            const CoopPlate& c = rules.coops[k];
            MovingPlatformEntity* lift = nullptr;
            for (const std::string& t : c.targets)
                if (auto* mp = dynamic_cast<MovingPlatformEntity*>(run->world().entity(t))) lift = mp;
            if (!lift || c.players < 2) continue;
            ++lifts;
            GameWorld& w = run->world();
            const Vec2 at = lift->box.pos();
            // One of the two in play on it (« 1 / 2 »): it does not move.
            w.restoreMember(1, w.carryOf(1), {lift->box.center().x, lift->box.y});
            for (int t = 0; t < 240; ++t) stepIdle(*run, allBits(*zr.br));
            CHECK(run->coop(k).on == 1);
            CHECK(!run->coop(k).done);
            CHECK(std::fabs(lift->box.x - at.x) < 0.01f && std::fabs(lift->box.y - at.y) < 0.01f);
            // Both on it: it starts (and its door at the top, if any, opens).
            w.restoreMember(0, w.carryOf(0), {lift->box.center().x - 12.0f, lift->box.y});
            for (int t = 0; t < 240; ++t) stepIdle(*run, allBits(*zr.br));
            CHECK(run->coop(k).done);
            CHECK(lift->box.y < at.y - 16.0f);
            for (const std::string& t : c.targets)
                if (auto* d = dynamic_cast<DoorEntity*>(w.entity(t))) CHECK(d->isOpen());
        }
    }
    std::printf("  %d monte-charges vérifiés\n", lifts);
    CHECK(lifts >= 8);
}

// ============================================================================ no way around a mechanism

TEST_CASE("raid mechanisms (2.81.2): with a mechanism inactive, no move of the simulated players reaches what it opens; its doors' walls reach the top") {
    Svc s;
    int proofs = 0, faults = 0;
    const char* auditEnv = std::getenv("COSMIC_MECH_AUDIT");  // the audit: every fault printed, none stops it
    const bool audit = auditEnv && *auditEnv;
    for (const ZoneRef& zr : mechanismZones()) {
        const uint32_t state = allBits(*zr.br);
        auto probe = zoneRun(s, *zr.br, zr.zone, state);
        CHECK(probe);
        if (!probe) continue;
        const ZoneRules& rules = probe->rules();
        const auto mech = mechanisms(rules);
        // Each door a plate opens: a solid wall over it on every row up to the top of the level.
        {
            GameWorld& w = probe->world();
            MoveOptions o;
            for (const CoopPlate& c : rules.coops)
                for (const std::string& t : c.targets)
                    if (auto* d = dynamic_cast<DoorEntity*>(w.entity(t)))
                        for (float y = 8.0f; y < d->box.y; y += 16.0f) {
                            const bool wall = w.physics().overlapsSolid({d->box.center().x - 1.0f, y - 1.0f, 2.0f, 2.0f}, o);
                            if (!wall) std::printf("  %s zone %d : passage au-dessus de la porte %s à y = %.0f\n", zr.br->part.c_str(), zr.zone, t.c_str(), y);
                            faults += wall ? 0 : 1;
                            if (!audit) CHECK(wall);
                        }
        }
        // The goals in order: each mechanism's next mechanism, the last one's: the way out (toward the lair).
        std::vector<Rect> goal;
        for (size_t i = 1; i < mech.size(); ++i) goal.push_back(rules.coops[mech[i].front()].rect);
        const Passage* out = nullptr;
        for (const Passage& p : rules.passages)
            if (!out || p.to > out->to) out = &p;
        CHECK(out != nullptr);
        if (!out) continue;
        goal.push_back(out->rect);
        // Graph j: the mechanisms before j working, j and the next ones not (their lifts' rides refused).
        for (size_t j = 0; j <= mech.size(); ++j) {
            auto run = zoneRun(s, *zr.br, zr.zone, state);
            GameWorld& w = run->world();
            std::set<uint32_t> stopped;  // the colliders of the lifts not started
            for (size_t i = 0; i < mech.size(); ++i) {
                if (i < j) {
                    work(w, rules, mech[i]);
                    continue;
                }
                for (size_t k : mech[i])
                    for (const std::string& t : rules.coops[k].targets)
                        if (auto* mp = dynamic_cast<MovingPlatformEntity*>(w.entity(t))) stopped.insert(mp->colliderId());
            }
            NavGraph g;
            g.build(w, rules.magma.present ? rules.magma.zone : std::string());
            auto avoid = [&](int a, int b) {
                for (int n : {a, b}) {
                    const int m = g.node(n).mover;
                    if (m >= 0 && stopped.count(g.movers()[static_cast<size_t>(m)].collider)) return true;
                }
                return false;
            };
            auto it = rules.spawns.find("start");
            if (it == rules.spawns.end() || it->second.empty()) it = rules.spawns.begin();
            const int from = g.locate(it->second.front());
            CHECK(from >= 0);
            auto reach = [&](const Rect& r) {
                const int to = g.nearest(r.bottomCenter(), std::max(80.0f, r.w), [](const NavGraph::Node& n) { return n.mover < 0; });
                std::vector<int> path;
                return to >= 0 && from >= 0 && g.path(from, to, path, 4000000, 0xFF, avoid);
            };
            // Before j: reached (the proof is not empty); j's goal: not reached while j is inactive.
            if (j > 0) {
                const bool ok = reach(goal[j - 1]);
                if (!ok) std::printf("  %s zone %d : but %zu non atteint avec les mécanismes d'avant en marche\n", zr.br->part.c_str(), zr.zone, j - 1);
                faults += ok ? 0 : 1;
                if (!audit) CHECK(ok);
            }
            if (j < mech.size()) {
                const bool leak = reach(goal[j]);
                if (leak)
                    std::printf("  %s zone %d : « %s » contourné (son but atteint sans lui)\n", zr.br->part.c_str(), zr.zone,
                                rules.coops[mech[j].front()].id.c_str());
                faults += leak ? 1 : 0;
                if (!audit) CHECK(!leak);
                ++proofs;
            }
        }
    }
    std::printf("  %d mécanismes impossibles à contourner (graphe des joueurs simulés), %d défauts\n", proofs, faults);
    CHECK(faults == 0);
    CHECK(proofs >= 8 * 3);
}

// ============================================================================ the interface

namespace {

struct Blank : Scene {
    void render(float) override {}
};

void send(InputManager& in, const SDL_Event& e) {
    in.beginFrame();
    in.handleEvent(e);
}
SDL_Event keyEvent(SDL_Scancode sc) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = sc;
    e.key.down = true;
    return e;
}
SDL_Event padEvent(SDL_GamepadButton b) {
    SDL_Event e{};
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    e.gbutton.button = static_cast<Uint8>(b);
    e.gbutton.down = true;
    return e;
}

/// A titan model in every state: fighting, fallen, not discovered (no group, nothing counted), not in this game
/// (closed), the heart shut or never entered; `noGroups`: « Raid entier » empty too.
contrib::Model model(bool noGroups, bool heartShut) {
    contrib::Model m;
    m.legend = "Test : chiffres comptés par la simulation.";
    m.tabKey = "Tab";
    m.parts = contrib::titanParts();
    m.raid.title = "Raid entier";
    contrib::Row r;
    r.name = "Joueur simulé 1";
    r.bossTenths = 1200;
    r.damageTenths = 3400;
    r.section = 0;
    r.tag = "G1";
    r.zoneDamage = {{"1. Mâchoire", 400}};
    if (!noGroups) {
        m.raid.rows = {r};
        contrib::GroupCard c;
        c.name = "Groupe 1 · CRÂNE";
        c.part = 0;
        m.groups.push_back(c);
        contrib::GroupCard empty;  // a group of 0 players, its part not in this game
        empty.name = "Groupe 2";
        empty.part = 3;
        m.groups.push_back(empty);
    }
    for (size_t i = 0; i < m.parts.size(); ++i) {
        contrib::Part& p = m.parts[i];
        const bool heart = p.id == "coeur";
        if (i == 0 && !noGroups) {  // fighting: its groups, its zones' tabs, a table
            p.state = asterion::State::Attacked;
            p.groups = {1};
            p.whole.rows = {r};
            p.whole.sections = {"Groupe 1 · sur place"};
            p.zones = {p.whole};
            for (int z = 0; z < 8; ++z) p.tabs.push_back({std::to_string(z + 1) + ". Zone", z < 2 ? 1.0f : 0.0f, z < 2, z < 1 ? 0 : -1});
        } else if (i == 1) {  // fallen, its tabs, nothing counted on this computer
            p.state = asterion::State::Destroyed;
            p.fallen = true;
            for (int z = 0; z < 8; ++z) p.tabs.push_back({std::to_string(z + 1) + ". Zone", 1.0f, true, -1});
        } else if (i == 2) {  // not discovered: its tabs greyed, no group, nothing
            p.state = asterion::State::Unknown;
            for (int z = 0; z < 8; ++z) p.tabs.push_back({std::to_string(z + 1) + ". Zone", 0.0f, false, -1});
        } else if (i == 3) {  // not in this game: closed, no tabs
            p.open = false;
            p.closed = "Cette partie n'est pas dans cette simulation.";
        } else if (heart) {
            p.state = asterion::State::Unknown;
            if (heartShut) {
                p.open = false;
                p.closed = "Le Cœur est fermé tant que les sept Gigaboss ne sont pas tombés (3 / 7).";
                p.tabs = {{"1. Aorte cosmique", 0.0f, false, -1}, {"Antre · Cœur", 0.0f, false, -1}};
            }
        } else {
            p.state = asterion::State::Unknown;  // no tabs, nothing: « PAS DÉCOUVERT »
        }
    }
    return m;
}

}  // namespace

TEST_CASE("raid mechanisms (2.81.2): contributions — every part and the heart chosen in every state, mouse, keys and gamepad, a row opened: no crash") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    Application app;
    AppConfig cfg;
    cfg.headless = true;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.vsync = false;
    CHECK(app.init(cfg));
    {
        GameServices g(app);
        g.loadAll();
        g.progression.asterionFilmSeen = true;
        app.pushScene(std::make_unique<Blank>());
        InputManager& in = app.input();
        AsterionFigure fig;
        int frames = 0, messages = 0, silent = 0;  // silent: a part with nothing that showed an empty table
        for (bool noGroups : {false, true})
            for (bool heartShut : {true, false})
                for (bool embedded : {true, false})
                    for (bool withFigure : {true, false}) {
                        contrib::Model m = model(noGroups, heartShut);
                        m.embedded = embedded;  // the sim panel's tab; else the raid menu's, the results', the recap's screen
                        CHECK(m.parts.size() == 8);
                        contrib::View v;
                        auto frame = [&](const SDL_Event* e, bool keys) {
                            app.runFrames(1);
                            if (e) send(in, *e);
                            if (keys) v.handleKeys(in, m);
                            UIContext& ui = g.ui;
                            ui.begin(app, 1.0f / 60.0f);
                            const Rect r{ui.s(24), ui.s(24), ui.screenW() - ui.s(48), ui.screenH() - ui.s(48)};
                            if (withFigure) contrib::draw(g, fig, r, m, v);
                            else contrib::draw(ui, r, m, v);
                            const contrib::Layout L = contrib::layout(ui, r, m, v);
                            ui.end();
                            ++frames;
                            return L;
                        };
                        auto openRow = [&](const SDL_Event& down, const SDL_Event& confirm) {
                            frame(&down, true);
                            frame(&confirm, true);
                            frame(nullptr, false);
                            frame(&confirm, true);  // closed again
                        };
                        // The mouse: from the fighting part (its tabs laid out) to each part, each time a click.
                        for (int target = -1; target < static_cast<int>(m.parts.size()); ++target) {
                            v.choose(0);
                            contrib::Layout L = frame(nullptr, false);
                            if (L.callouts.size() != m.parts.size()) continue;  // no titan at this size
                            const Rect at = target < 0 ? L.overview : L.callouts[static_cast<size_t>(target)];
                            SDL_Event mv{};
                            mv.type = SDL_EVENT_MOUSE_MOTION;
                            mv.motion.x = at.center().x;
                            mv.motion.y = at.center().y;
                            SDL_Event down{};
                            down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                            down.button.button = SDL_BUTTON_LEFT;
                            down.button.down = true;
                            down.button.x = at.center().x;
                            down.button.y = at.center().y;
                            SDL_Event up = down;
                            up.type = SDL_EVENT_MOUSE_BUTTON_UP;
                            up.button.down = false;
                            frame(&mv, false);
                            app.runFrames(1);
                            in.beginFrame();
                            in.handleEvent(mv);
                            in.handleEvent(down);
                            {
                                UIContext& ui = g.ui;
                                ui.begin(app, 1.0f / 60.0f);
                                const Rect r{ui.s(24), ui.s(24), ui.screenW() - ui.s(48), ui.screenH() - ui.s(48)};
                                if (withFigure) contrib::draw(g, fig, r, m, v);
                                else contrib::draw(ui, r, m, v);
                                ui.end();
                            }
                            frame(&up, false);  // the click: applied once this image is drawn
                            CHECK(v.part == target);
                            L = frame(nullptr, false);
                            if (target >= 0) {
                                const contrib::Part& p = m.parts[static_cast<size_t>(target)];
                                const bool none = !p.open || (p.groups.empty() && p.whole.rows.empty() && p.whole.sections.empty() && p.zones.empty());
                                if (none) {
                                    silent += L.message.empty() ? 1 : 0;  // a clear empty state, not an empty table (checked last)
                                    ++messages;
                                }
                            }
                            openRow(keyEvent(SDL_SCANCODE_DOWN), keyEvent(SDL_SCANCODE_RETURN));
                        }
                        // The keys (Q / E) and the gamepad (LB / RB) around every part, a row opened at each, Tab through its zones.
                        v.choose(-1);
                        for (int k = 0; k < 2 * static_cast<int>(m.parts.size() + 1); ++k) {
                            const bool pad = k % 2 == 1;
                            const SDL_Event next = pad ? padEvent(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) : keyEvent(SDL_SCANCODE_E);
                            frame(&next, true);
                            openRow(pad ? padEvent(SDL_GAMEPAD_BUTTON_DPAD_DOWN) : keyEvent(SDL_SCANCODE_DOWN),
                                    pad ? padEvent(SDL_GAMEPAD_BUTTON_SOUTH) : keyEvent(SDL_SCANCODE_RETURN));
                            const SDL_Event tab = keyEvent(SDL_SCANCODE_TAB);
                            frame(&tab, true);
                            openRow(keyEvent(SDL_SCANCODE_DOWN), keyEvent(SDL_SCANCODE_RETURN));
                        }
                        for (int k = 0; k < static_cast<int>(m.parts.size() + 1); ++k) {
                            const SDL_Event prev = k % 2 ? padEvent(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) : keyEvent(SDL_SCANCODE_Q);
                            frame(&prev, true);
                        }
                        // A view left on a part that the model no longer has, an opened row past the rows: back to the raid.
                        v.part = 42;
                        v.expanded = 99;
                        v.selected = 99;
                        frame(nullptr, false);
                        CHECK(v.part == -1);
                    }
        std::printf("  %d images de la table des contributions, %d états vides annoncés\n", frames, messages);
        CHECK(messages > 0);
        CHECK(silent == 0);
        // The server's tables with nothing in them (the results, the recap): drawn, a part chosen, no crash.
        contrib::Model empty = contrib::modelOfResults(raids::Results{}, "");
        empty.parts = contrib::titanParts();
        contrib::View v;
        for (int p = -1; p < static_cast<int>(empty.parts.size()); ++p) {
            app.runFrames(1);
            v.choose(p);
            v.selected = 0;
            v.toggle = true;
            UIContext& ui = g.ui;
            ui.begin(app, 1.0f / 60.0f);
            contrib::draw(g, fig, {ui.s(24), ui.s(24), ui.screenW() - ui.s(48), ui.screenH() - ui.s(48)}, empty, v);
            ui.end();
        }
    }
}

TEST_CASE("raid mechanisms (2.81.2): the plate's pill, the tabs of one width, the HUD's button (the key alone), « Raid à tester » — measured") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    Application app;
    AppConfig cfg;
    cfg.headless = true;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.vsync = false;
    CHECK(app.init(cfg));
    {
        GameServices g(app);
        g.loadAll();
        app.pushScene(std::make_unique<Blank>());
        CHECK(raidmenu::buttonText("Tab") == "Tab");
        CHECK(raidmenu::buttonText("LB").find("MENU DU RAID") == std::string::npos);
        const std::vector<std::string> simTabs = {"Groupes", "Contributions", "Événements du monde", "Carte du titan"};
        for (Language lang : {Language::French, Language::English}) {
            i18n::setLanguage(lang);
            for (const Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}, Vec2{1024, 600}}) {
                app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
                app.renderer().refreshOutputSize();
                app.runFrames(1);
                app.renderer().refreshOutputSize();
                UIContext& ui = g.ui;
                ui.begin(app, 1.0f / 60.0f);
                const float W = ui.screenW();
                // The pill over a plate: its whole text (measured) inside the screen, even at its edges.
                for (float px : {-40.0f, 10.0f, W * 0.5f, W - 30.0f}) {
                    const Rect plate{px, ui.s(300), ui.s(120), ui.s(40)};
                    const raidhud::Pill a = raidhud::coopPill(ui, plate, 1, 3, true, W);
                    CHECK(a.text == raidhud::kAmbushFirst);
                    CHECK(a.box.x >= 0.0f && a.box.right() <= W + 0.5f);
                    CHECK(raidui::textW(ui, a.text, a.font, FontFace::Bold) <= a.box.w - ui.s(10) + 0.5f);
                    CHECK(a.box.bottom() <= plate.y);  // above the plate, never on it
                    const raidhud::Pill n = raidhud::coopPill(ui, plate, 1, 3, false, W);
                    CHECK(n.text == "1 / 3");
                }
                // The sim panel's tabs: one width (the widest name in this language), the same whichever is chosen.
                float bottom = 0.0f;
                const std::vector<Rect> t = raidui::tabRects(ui, ui.s(40), ui.s(90), W - ui.s(40), simTabs, bottom);
                CHECK(t.size() == simTabs.size());
                float widest = 0.0f;
                for (const std::string& l : simTabs) widest = std::max(widest, raidui::textW(ui, l, 14, FontFace::Bold));
                for (size_t i = 0; i < t.size(); ++i) {
                    CHECK(std::fabs(t[i].w - t[0].w) < 0.01f);
                    CHECK(widest <= t[i].w - ui.s(16) + 0.5f || t[i].w >= W - ui.s(80) - 0.5f);
                    CHECK(t[i].right() <= W - ui.s(40) + 0.5f);
                    for (size_t j = i + 1; j < t.size(); ++j) CHECK(!t[i].intersects(t[j]));
                }
                for (int sel = 0; sel < 4; ++sel) {
                    float b2 = 0.0f;
                    raidui::tabs(ui, "t", ui.s(40), ui.s(90), W - ui.s(40), simTabs, sel, b2);
                    CHECK(std::fabs(b2 - bottom) < 0.01f);
                }
                // The raid menu's sections: one width too.
                const raidmenu::MenuLayout ml = raidmenu::layoutMenu(ui, "Tab");
                for (const Rect& r : ml.tabs) CHECK(std::fabs(r.w - ml.tabs.front().w) < 0.01f);
                // The HUD's button: the key alone, a comfortable click zone inside its area.
                const Rect area{ui.s(20), ui.s(20), ui.s(360), ui.s(200)};
                const raidmenu::NoticeLayout nl = raidmenu::layoutNotices(ui, area, 1.0f, {}, raidmenu::buttonText("Tab"), true);
                CHECK(area.containsRect(nl.button));
                CHECK(nl.button.w >= ui.s(72) - 0.5f);
                // « Raid à tester »: closed, one row; open, its options under it (nothing over its button).
                bool open = false;
                int index = 0;
                const Rect dr{ui.s(40), ui.s(200), ui.s(420), ui.s(44)};
                CHECK(std::fabs(raidui::dropdown(ui, "raid", dr, "Raid à tester", {"Asterion"}, index, open) - dr.bottom()) < 0.01f);
                open = true;
                const float under = raidui::dropdown(ui, "raid", dr, "Raid à tester", {"Asterion"}, index, open);
                CHECK(under >= dr.bottom() + ui.s(36));
                CHECK(index == 0);
                ui.end();
            }
        }
        i18n::setLanguage(Language::French);
    }
}
