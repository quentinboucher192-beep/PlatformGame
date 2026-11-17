// The titan of the clan raids (game/online/AsterionBody, ui/widgets/AsterionFigure): its sprite in
// layers (assets/ui/raids/asterion.json, made by tools/assetgen/environment/raid_asterion.py) and
// its parts (data/raids/asterion.json): the part under a point (hit masks, the topmost first,
// breathing included), the tooltip of each part, the look of each state, the frames of the idle
// animation, and the generator's output (the same bytes at every render, when Python is there).

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "TestFramework.h"
#include "engine/core/FileUtil.h"
#include "game/online/AsterionBody.h"

using namespace cosmic;
using asterion::State;

#ifdef _WIN32
#define COSMIC_NULL_DEVICE "NUL"
#else
#define COSMIC_NULL_DEVICE "/dev/null"
#endif

namespace {

asterion::Sprite sprite() {
    auto j = FileUtil::loadJson("assets/ui/raids/asterion.json");
    return j ? asterion::parseSprite(*j) : asterion::Sprite{};
}

std::vector<asterion::Part> parts() {
    auto j = FileUtil::loadJson("data/raids/asterion.json");
    return j ? asterion::parseParts(*j) : std::vector<asterion::Part>{};
}

int indexOf(const asterion::Sprite& s, const std::string& id) {
    for (size_t i = 0; i < s.layers.size(); ++i)
        if (s.layers[i].id == id) return static_cast<int>(i);
    return -1;
}

/// Width and height of a PNG (its IHDR), 0 when unreadable.
std::pair<int, int> pngSize(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    unsigned char b[24] = {};
    if (!f.read(reinterpret_cast<char*>(b), 24)) return {0, 0};
    auto be = [&](int o) { return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]; };
    return {be(16), be(20)};
}

}  // namespace

TEST_CASE("asterion: the sprite's layers, their looks and cells inside the sheet") {
    const asterion::Sprite s = sprite();
    CHECK(s.valid);
    CHECK(s.canvasW == 160 && s.canvasH == 128);
    const std::vector<std::string> order = {"aura", "jambe_droite", "jambe_gauche", "abdomen", "torse", "bras_droit", "bras_gauche", "crane", "coeur"};
    CHECK(s.layers.size() == order.size());
    for (size_t i = 0; i < order.size() && i < s.layers.size(); ++i) CHECK(s.layers[i].id == order[i]);
    const auto [sw, sh] = pngSize("assets/" + s.sheet);
    CHECK(sw > 0 && sh > 0);
    for (const asterion::Layer& l : s.layers) {
        CHECK(l.x >= 0 && l.y >= 0 && l.x + l.w <= s.canvasW && l.y + l.h <= s.canvasH);
        const bool aura = l.id == "aura";
        CHECK(l.hasMask() == !aura);
        for (const char* look : {"intact", "affaibli", "detruit", "inconnu", "contour"}) {
            const bool has = l.cells.count(look) && !l.cells.at(look).empty();
            CHECK(has == (!aura || std::string(look) == "intact"));
            if (!has) continue;
            for (const asterion::Cell& c : l.cells.at(look)) CHECK(c.x >= 0 && c.y >= 0 && c.x + l.w <= sw && c.y + l.h <= sh);
        }
        for (const asterion::Span& sp : l.mask) CHECK(sp.y >= 0 && sp.y < s.canvasH && sp.x0 >= 0 && sp.x0 < sp.x1 && sp.x1 <= s.canvasW);
    }
    // The idle animation: the aura's 6 frames, the heart's 4 beats; the upper body breathes.
    const asterion::Layer* aura = s.layer("aura");
    const asterion::Layer* heart = s.layer("coeur");
    CHECK(aura && aura->cells.at("intact").size() == 6 && aura->fps > 0.0f);
    CHECK(heart && heart->cells.at("intact").size() == 4 && heart->fps > 0.0f);
    if (heart) {
        CHECK(heart->cellAt("intact", 0.0f) != heart->cellAt("intact", 1.0f / heart->fps + 0.01f));
        CHECK(heart->cellAt("intact", 0.0f) == heart->cellAt("intact", 4.0f / heart->fps + 0.01f));
        CHECK(heart->cellAt("affaibli", 3.7f) == &heart->cells.at("affaibli").front());
        CHECK(heart->cellAt("absent", 0.0f) == nullptr);
    }
    const asterion::Layer* torso = s.layer("torse");
    const asterion::Layer* leg = s.layer("jambe_droite");
    CHECK(torso && asterion::breathLift(*torso, 0.5f) == 1 && asterion::breathLift(*torso, 2.5f) == 0);
    CHECK(leg && asterion::breathLift(*leg, 0.5f) == 0);
}

TEST_CASE("asterion: hit-testing the parts by their masks") {
    const asterion::Sprite s = sprite();
    CHECK(s.valid);
    std::set<int> found;
    for (size_t i = 0; i < s.layers.size(); ++i) {
        const asterion::Layer& l = s.layers[i];
        if (!l.hasMask()) continue;
        // Its focus point is on it, and nothing drawn above covers it.
        CHECK(l.contains(l.anchorX, l.anchorY));
        CHECK(asterion::hitTest(s, l.anchorX, l.anchorY) == static_cast<int>(i));
        found.insert(asterion::hitTest(s, l.anchorX, l.anchorY));
    }
    CHECK(found.size() == 8);
    // The heart over the chest, the eyes in the skull, the burning fist, the claws, the feet.
    CHECK(asterion::hitTest(s, 80, 54) == indexOf(s, "coeur"));
    CHECK(asterion::hitTest(s, 74, 19) == indexOf(s, "crane"));
    CHECK(asterion::hitTest(s, 22, 86) == indexOf(s, "bras_droit"));
    CHECK(asterion::hitTest(s, 137, 84) == indexOf(s, "bras_gauche"));
    CHECK(asterion::hitTest(s, 58, 122) == indexOf(s, "jambe_droite"));
    CHECK(asterion::hitTest(s, 102, 122) == indexOf(s, "jambe_gauche"));
    // The empty corners and the aura are never a part; outside the canvas, nothing.
    CHECK(asterion::hitTest(s, 1, 1) == -1);
    CHECK(asterion::hitTest(s, 158, 126) == -1);
    CHECK(asterion::hitTest(s, -5, 40) == -1);
    CHECK(asterion::hitTest(s, 80, 400) == -1);
    // Breathing: the torso drawn a row higher is found a row higher.
    const int t = indexOf(s, "torse");
    if (t >= 0) {
        std::vector<int> lifts(s.layers.size(), 0);
        lifts[static_cast<size_t>(t)] = 1;
        const asterion::Layer& l = s.layers[static_cast<size_t>(t)];
        CHECK(asterion::hitTest(s, l.anchorX, l.anchorY - 1, lifts) == t);
        int top = 1000;
        for (const asterion::Span& sp : l.mask)
            if (sp.x0 <= 80 && 80 < sp.x1 && sp.y < top) top = sp.y;
        CHECK(top < 1000);
        CHECK(l.contains(80, top) && !l.contains(80, top - 1));
    }
}

TEST_CASE("asterion: the parts and their tooltips") {
    const std::vector<asterion::Part> ps = parts();
    const asterion::Sprite s = sprite();
    CHECK(ps.size() == 8);
    const std::vector<std::pair<std::string, std::string>> bosses = {{"crane", "Skullmaw"},   {"bras_gauche", "Dreadfang"}, {"bras_droit", "Pyroclast"},
                                                                     {"torse", "Heartbreaker"}, {"abdomen", "Voidworm"},     {"jambe_gauche", "Graviton"},
                                                                     {"jambe_droite", "Titanus"}};
    for (size_t i = 0; i < bosses.size() && i < ps.size(); ++i) {
        CHECK(ps[i].id == bosses[i].first);
        CHECK(ps[i].boss == bosses[i].second);
    }
    for (const asterion::Part& p : ps) {
        const asterion::Layer* l = s.layer(p.id);
        CHECK(l && l->hasMask());  // every part has its layer and its mask
        const asterion::Tooltip intact = asterion::tooltipOf(p, State::Intact);
        CHECK(intact.title == p.name);
        CHECK(intact.boss == p.boss);
        CHECK(intact.state == "Intact");
        CHECK(!intact.effect.empty());
        // Not discovered: its gigaboss unknown, nothing said of its effect.
        const asterion::Tooltip fog = asterion::tooltipOf(p, State::Unknown);
        CHECK(fog.title == p.name && fog.state == "Non découvert" && fog.effect.empty());
        CHECK(fog.boss == (p.boss.empty() ? "" : "inconnu"));
    }
    if (ps.size() == 8) {
        CHECK(ps[0].name == "CRÂNE");
        CHECK(ps[7].id == "coeur" && ps[7].boss.empty());
        const asterion::Tooltip t = asterion::tooltipOf(ps[0], State::Destroyed);
        CHECK(t.boss == "Skullmaw" && t.state == "Détruit · gigaboss vaincu");
        CHECK(t.effect == "Détruire le crâne aveugle Asterion : ses attaques perdent en précision.");
        CHECK(asterion::tooltipOf(ps[1], State::Attacked).state == "Sous attaque");
        CHECK(asterion::tooltipOf(ps[2], State::Weakened).state == "Affaibli");
    }
}

TEST_CASE("asterion: the state of each part and how it is drawn") {
    CHECK(asterion::stateFromString("intact") == State::Intact);
    CHECK(asterion::stateFromString("attaque") == State::Attacked);
    CHECK(asterion::stateFromString("affaibli") == State::Weakened);
    CHECK(asterion::stateFromString("detruit") == State::Destroyed);
    CHECK(asterion::stateFromString("inconnu") == State::Unknown);
    CHECK(asterion::stateFromString("n'importe quoi") == State::Intact);
    const Json event = {{"id", "e1"}, {"body", {{"crane", "detruit"}, {"bras_droit", "attaque"}, {"torse", 3}}}};
    const auto states = asterion::statesOf(event);
    CHECK(asterion::stateOf(states, "crane") == State::Destroyed);
    CHECK(asterion::stateOf(states, "bras_droit") == State::Attacked);
    CHECK(asterion::stateOf(states, "torse") == State::Intact);  // not a word: intact
    CHECK(asterion::stateOf(states, "abdomen") == State::Intact);
    CHECK(asterion::statesOf(Json{{"id", "e1"}}).empty());  // phase 1: no body, everything intact
    CHECK(asterion::statesOf(Json()).empty());
    const asterion::Visual intact = asterion::visualOf(State::Intact);
    CHECK(intact.look == "intact" && !intact.attackRim && intact.glow == 1.0f);
    const asterion::Visual attacked = asterion::visualOf(State::Attacked);
    CHECK(attacked.look == "intact" && attacked.attackRim && attacked.glow == 1.0f);
    const asterion::Visual weak = asterion::visualOf(State::Weakened);
    CHECK(weak.look == "affaibli" && !weak.attackRim && weak.glow > 0.0f && weak.glow < 1.0f);
    const asterion::Visual dead = asterion::visualOf(State::Destroyed);
    CHECK(dead.look == "detruit" && !dead.attackRim && dead.glow == 0.0f);
    const asterion::Visual fog = asterion::visualOf(State::Unknown);
    CHECK(fog.look == "inconnu" && !fog.attackRim && fog.glow == 0.0f);
    // Every look a state asks for exists on every part.
    const asterion::Sprite s = sprite();
    for (const asterion::Layer& l : s.layers)
        if (l.hasMask())
            for (State st : {State::Intact, State::Attacked, State::Weakened, State::Destroyed, State::Unknown}) CHECK(l.cellAt(asterion::visualOf(st).look, 0.0f));
}

TEST_CASE("asterion: the generator gives the same bytes at every render") {
    // Needs Python with numpy and Pillow (the asset tools); skipped without them.
    if (std::system("python3 -c \"import numpy, PIL\" > " COSMIC_NULL_DEVICE " 2>&1") != 0) {
        std::printf("  (python3 + numpy + Pillow absents : vérification du générateur d'Asterion sautée)\n");
        return;
    }
    CHECK(std::system("python3 tools/assetgen/environment/raid_asterion.py --check > " COSMIC_NULL_DEVICE " 2>&1") == 0);
}
