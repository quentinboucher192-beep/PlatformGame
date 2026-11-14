// Scripted scenes of the power guide: one per power, each checked by the tests.

#include "game/powers/PowerDemos.h"

#include <initializer_list>

#include "engine/input/InputManager.h"
#include "game/gameplay/GameWorld.h"

namespace cosmic {

namespace {

PlayerInput in(int dir, std::initializer_list<Action> pressed = {}, std::initializer_list<Action> held = {}) {
    PlayerInput p;
    if (dir > 0) {
        p.held[static_cast<int>(Action::MoveRight)] = true;
        p.move.x = 1.0f;
    } else if (dir < 0) {
        p.held[static_cast<int>(Action::MoveLeft)] = true;
        p.move.x = -1.0f;
    }
    for (Action a : pressed) p.pressed[static_cast<int>(a)] = p.held[static_cast<int>(a)] = true;
    for (Action a : held) p.held[static_cast<int>(a)] = true;
    if (p.held[static_cast<int>(Action::MoveDown)]) p.move.y = 1.0f;
    return p;
}

void put(LevelData& l, const std::string& type, Rect r, Json props = Json::object(), const std::string& layer = "gameplay") {
    LevelObject o;
    o.id = "d" + std::to_string(l.objects.size() + 1);
    o.type = type;
    o.layer = layer;
    o.rect = r;
    o.props = std::move(props);
    l.objects.push_back(o);
}

void startAt(LevelData& l, float x, float feetY) {
    for (auto& o : l.objects)
        if (o.type == "player_start") o.rect = {x - 5, feetY - 22, 10, 22};
}

Json stone() { return {{"material", "ruin_stone"}}; }

float feetX(const GameWorld& w) { return w.player().box.center().x; }
float feetY(const GameWorld& w) { return w.player().box.bottom(); }

}  // namespace

bool powerIsPassive(const std::string& power) { return power == "magnet" || power == "air_dash" || power == "triple_jump"; }

std::vector<PowerDemo> powerDemos() {
    std::vector<PowerDemo> v;
    auto add = [&](std::string power) -> PowerDemo& {
        v.emplace_back();
        v.back().power = std::move(power);
        return v.back();
    };
    {
        PowerDemo& d = add("dash");  // breaks a crystal wall
        d.layout = [](LevelData& l) { put(l, "power_gate", {262, 736, 16, 64}, {{"power", "dash"}}); };
        d.script = [](int s) { return s < 25 ? in(0) : s == 58 ? in(1, {Action::Dash}) : s < 170 ? in(1) : in(0); };
        d.success = [](const GameWorld& w) { return feetX(w) > 300.0f; };
    }
    {
        PowerDemo& d = add("wave");  // dissolves an energy barrier
        d.layout = [](LevelData& l) { put(l, "power_gate", {300, 728, 16, 72}, {{"power", "wave"}}); };
        d.script = [](int s) { return s < 12 ? in(1) : s == 40 || s == 70 ? in(0, {Action::Wave}) : s > 120 && s < 290 ? in(1) : in(0); };
        d.success = [](const GameWorld& w) { return feetX(w) > 330.0f; };
    }
    {
        PowerDemo& d = add("shield");  // crosses a plasma curtain
        d.layout = [](LevelData& l) { put(l, "power_gate", {250, 712, 16, 88}, {{"power", "shield"}}); };
        d.script = [](int s) { return s < 20 ? in(0) : s == 30 ? in(1, {Action::Shield}) : s < 150 ? in(1) : in(0); };
        d.success = [](const GameWorld& w) { return feetX(w) > 290.0f; };
    }
    {
        PowerDemo& d = add("impulse");  // breaks cracked rock
        d.layout = [](LevelData& l) { put(l, "power_gate", {206, 768, 64, 32}, {{"power", "impulse"}}); };
        d.script = [](int s) { return s < 20 ? in(1) : s == 45 ? in(0, {Action::Impulse}) : s > 90 && s < 220 ? in(1) : in(0); };
        d.success = [](const GameWorld& w) { return feetX(w) > 290.0f && feetY(w) > 795.0f; };
    }
    {
        PowerDemo& d = add("teleport");  // through a phase barrier
        d.layout = [](LevelData& l) { put(l, "power_gate", {240, 712, 16, 88}, {{"power", "teleport"}}); };
        d.script = [](int s) { return s < 20 ? in(1) : s == 40 ? in(1, {Action::Teleport}) : s < 160 ? in(1) : in(0); };
        d.success = [](const GameWorld& w) { return feetX(w) > 275.0f; };
    }
    {
        PowerDemo& d = add("glide");  // over a gap too wide to jump
        d.layout = [](LevelData& l) {
            put(l, "solid", {60, 700, 180, 100}, stone(), "terrain");
            put(l, "solid", {440, 700, 140, 100}, stone(), "terrain");
            startAt(l, 150, 700);
        };
        d.script = [](int s) { return s < 30 ? in(0) : s == 52 ? in(1, {Action::Jump}) : s < 280 ? in(1, {}, {Action::Jump}) : in(0); };
        d.success = [](const GameWorld& w) { return feetX(w) > 450.0f && feetY(w) < 705.0f && w.player().grounded(); };
        d.cam = {320, 720};
        d.viewH = 190.0f;
        d.period = 420;
    }
    {
        PowerDemo& d = add("grapple");  // pulled up to an anchor
        d.layout = [](LevelData& l) { put(l, "grapple_point", {300, 650, 16, 16}); };
        d.script = [](int s) {
            return s < 20 ? in(1) : s == 40 ? in(1, {Action::Jump}) : s == 62 ? in(1, {Action::Interact}) : s < 200 ? in(1) : in(0);
        };
        d.success = [](const GameWorld& w) { return feetY(w) < 700.0f; };
        d.cam = {280, 735};
        d.viewH = 180.0f;
    }
    {
        PowerDemo& d = add("ground_pound");  // through a cracked floor
        d.layout = [](LevelData& l) {
            put(l, "solid", {170, 720, 40, 80}, stone(), "terrain");
            put(l, "solid", {274, 720, 40, 80}, stone(), "terrain");
            put(l, "power_gate", {210, 720, 64, 16}, {{"power", "ground_pound"}});
            startAt(l, 242, 720);
        };
        d.script = [](int s) { return s < 40 ? in(0) : s == 40 ? in(0, {Action::Jump}) : s == 70 ? in(0, {Action::Attack}, {Action::MoveDown}) : in(0); };
        d.success = [](const GameWorld& w) { return feetY(w) > 790.0f; };
        d.cam = {242, 740};
    }
    {
        PowerDemo& d = add("charge_shot");  // a charged sphere on an enemy
        d.layout = [](LevelData& l) { put(l, "enemy", {330, 788, 18, 12}, {{"enemy", "glowmite"}, {"patrol", 40}}); };
        d.script = [](int s) {
            if (s < 10) return in(1);
            if (s == 20) return in(0, {Action::Attack});
            return s > 20 && s < 150 ? in(0, {}, {Action::Attack}) : in(0);
        };
        d.success = [](const GameWorld& w) { return w.stats().kills >= 1; };
    }
    {
        PowerDemo& d = add("triple_jump");  // up a ledge two jumps can't reach
        d.layout = [](LevelData& l) { put(l, "solid", {280, 675, 140, 125}, stone(), "terrain"); };
        d.script = [](int s) {
            if (s < 20) return in(0);
            if (s == 20 || s == 44 || s == 68) return in(1, {Action::Jump}, {Action::Jump});
            return s < 200 ? in(1, {}, {Action::Jump}) : in(0);
        };
        d.success = [](const GameWorld& w) { return feetX(w) > 290.0f && feetY(w) < 680.0f && w.player().grounded(); };
        d.cam = {280, 715};
        d.viewH = 190.0f;
        d.period = 300;
    }
    {
        PowerDemo& d = add("time_slow");  // enemies slowed down
        d.layout = [](LevelData& l) {
            put(l, "enemy", {300, 788, 18, 12}, {{"enemy", "glowmite"}, {"patrol", 90}});
            put(l, "enemy", {360, 788, 18, 12}, {{"enemy", "glowmite"}, {"patrol", 90}});
        };
        d.script = [](int s) { return s == 90 ? in(0, {Action::Special}) : in(0); };
        d.success = [](const GameWorld& w) { return w.timeSlowRemaining() > 0.0f; };
    }
    {
        PowerDemo& d = add("magnet");  // shards fly to the player
        d.layout = [](LevelData& l) {
            for (int i = 0; i < 6; ++i) put(l, "shard", {230.0f + i * 22.0f, 740.0f - static_cast<float>(i % 2) * 18.0f, 12, 12});
        };
        d.script = [](int s) { return s > 40 && s < 120 ? in(1) : in(0); };
        d.success = [](const GameWorld& w) { return w.stats().shards >= 6; };
    }
    {
        PowerDemo& d = add("air_dash");  // a second dash before landing
        d.extra = {"dash"};
        d.layout = [](LevelData& l) {
            put(l, "solid", {60, 700, 180, 100}, stone(), "terrain");
            put(l, "solid", {440, 740, 200, 60}, stone(), "terrain");
            startAt(l, 190, 700);
        };
        d.script = [](int s) {
            if (s < 20) return in(0);
            if (s == 60) return in(1, {Action::Jump}, {Action::Jump});
            if (s == 90 || s == 146) return in(1, {Action::Dash});
            if (s > 60 && s < 92) return in(1, {}, {Action::Jump});
            return s < 260 ? in(1) : in(0);
        };
        d.success = [](const GameWorld& w) { return feetX(w) > 445.0f && feetY(w) < 745.0f && w.player().grounded(); };
        d.cam = {350, 720};
        d.viewH = 190.0f;
    }
    {
        PowerDemo& d = add("plasma_bomb");  // bounces, blows up an enemy and cracked rock
        d.layout = [](LevelData& l) {
            put(l, "enemy", {262, 788, 18, 12}, {{"enemy", "glowmite"}, {"patrol", 20}});
            put(l, "power_gate", {300, 768, 64, 32}, {{"power", "impulse"}});
        };
        d.script = [](int s) { return s < 6 ? in(1) : s == 30 ? in(0, {Action::Special}) : s > 150 && s < 320 ? in(1) : in(0); };
        d.success = [](const GameWorld& w) { return w.stats().kills >= 1 && feetX(w) > 370.0f; };
    }
    return v;
}

LevelData powerDemoLevel(const PowerDemo& d) {
    LevelData l = LevelData::makeEmpty("guide_" + d.power, d.power, "sylvanox");
    l.intro.steps.clear();
    l.outro.steps.clear();
    l.events.clear();
    l.startPowers = d.extra;
    l.startPowers.push_back(d.power);
    l.maxPowers = 0;
    l.timerMode = TimerMode::Unlimited;
    for (auto& o : l.objects)
        if (o.type == "exit") o.rect.x = 2300;  // no exit in the frame
    if (d.layout) d.layout(l);
    return l;
}

}  // namespace cosmic
