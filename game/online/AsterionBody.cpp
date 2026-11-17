#include "game/online/AsterionBody.h"

#include <algorithm>
#include <cmath>

namespace cosmic::asterion {

namespace {

int num(const Json& a, size_t i) { return a.is_array() && i < a.size() && a[i].is_number() ? a[i].get<int>() : 0; }

}  // namespace

State stateFromString(const std::string& s) {
    if (s == "attaque") return State::Attacked;
    if (s == "affaibli") return State::Weakened;
    if (s == "detruit") return State::Destroyed;
    if (s == "inconnu") return State::Unknown;
    return State::Intact;
}

bool Layer::contains(int cx, int cy) const {
    // The spans are sorted by row: the first one of row cy, then those of that row.
    auto it = std::lower_bound(mask.begin(), mask.end(), cy, [](const Span& s, int row) { return s.y < row; });
    for (; it != mask.end() && it->y == cy; ++it)
        if (cx >= it->x0 && cx < it->x1) return true;
    return false;
}

const Cell* Layer::cellAt(const std::string& look, float time) const {
    auto it = cells.find(look);
    if (it == cells.end() || it->second.empty()) return nullptr;
    const auto& frames = it->second;
    if (frames.size() == 1 || fps <= 0.0f) return &frames.front();
    const auto n = static_cast<long long>(frames.size());
    long long f = static_cast<long long>(std::floor(std::max(0.0f, time) * fps)) % n;
    return &frames[static_cast<size_t>(f)];
}

const Layer* Sprite::layer(const std::string& id) const {
    for (const Layer& l : layers)
        if (l.id == id) return &l;
    return nullptr;
}

Sprite parseSprite(const Json& j) {
    Sprite s;
    if (!j.is_object() || !j.contains("layers") || !j["layers"].is_array()) return s;
    s.canvasW = num(jfield(j, "canvas"), 0);
    s.canvasH = num(jfield(j, "canvas"), 1);
    s.sheet = jstr(j, "sheet");
    for (const Json& e : j["layers"]) {
        if (!e.is_object()) continue;
        Layer l;
        l.id = jstr(e, "id");
        const Json box = jfield(e, "box");
        l.x = num(box, 0);
        l.y = num(box, 1);
        l.w = num(box, 2);
        l.h = num(box, 3);
        l.fps = e.value("fps", 0.0f);
        l.lift = e.value("lift", 0);
        const Json anchor = jfield(e, "anchor");
        l.anchorX = num(anchor, 0);
        l.anchorY = num(anchor, 1);
        const Json cells = jfield(e, "cells");
        if (cells.is_object())
            for (auto it = cells.begin(); it != cells.end(); ++it) {
                std::vector<Cell>& v = l.cells[it.key()];
                if (it.value().is_array())
                    for (const Json& c : it.value()) v.push_back({num(c, 0), num(c, 1)});
            }
        const Json mask = jfield(e, "mask");
        if (mask.is_array())
            for (const Json& m : mask) l.mask.push_back({num(m, 0), num(m, 1), num(m, 2)});
        std::stable_sort(l.mask.begin(), l.mask.end(), [](const Span& a, const Span& b) { return a.y < b.y; });
        if (!l.id.empty() && l.w > 0 && l.h > 0) s.layers.push_back(std::move(l));
    }
    s.valid = s.canvasW > 0 && s.canvasH > 0 && !s.sheet.empty() && !s.layers.empty();
    return s;
}

std::vector<Part> parseParts(const Json& j) {
    std::vector<Part> out;
    const Json parts = jfield(j, "parts");
    if (!parts.is_array()) return out;
    for (const Json& p : parts) {
        Part part{jstr(p, "id"), jstr(p, "name"), jstr(p, "boss"), jstr(p, "description")};
        if (!part.id.empty() && !part.name.empty()) out.push_back(std::move(part));
    }
    return out;
}

std::map<std::string, State> statesOf(const Json& event) {
    std::map<std::string, State> out;
    const Json body = jfield(event, "body");
    if (!body.is_object()) return out;
    for (auto it = body.begin(); it != body.end(); ++it)
        if (it.value().is_string()) out[it.key()] = stateFromString(it.value().get<std::string>());
    return out;
}

State stateOf(const std::map<std::string, State>& states, const std::string& id) {
    auto it = states.find(id);
    return it == states.end() ? State::Intact : it->second;
}

Visual visualOf(State s) {
    Visual v;
    switch (s) {
        case State::Intact: break;
        case State::Attacked: v.attackRim = true; break;
        case State::Weakened:
            v.look = "affaibli";
            v.glow = 0.45f;
            break;
        case State::Destroyed:
            v.look = "detruit";
            v.glow = 0.0f;
            break;
        case State::Unknown:
            v.look = "inconnu";
            v.glow = 0.0f;
            break;
    }
    return v;
}

int breathLift(const Layer& l, float time) {
    if (l.lift <= 0) return 0;
    // A breath every 4 s: in for 2 s (raised), out for 2 s.
    return std::fmod(std::max(0.0f, time), 4.0f) < 2.0f ? l.lift : 0;
}

int hitTest(const Sprite& sprite, int x, int y, const std::vector<int>& lifts) {
    for (int i = static_cast<int>(sprite.layers.size()) - 1; i >= 0; --i) {
        const Layer& l = sprite.layers[static_cast<size_t>(i)];
        if (!l.hasMask()) continue;
        const int lift = static_cast<size_t>(i) < lifts.size() ? lifts[static_cast<size_t>(i)] : 0;
        if (l.contains(x, y + lift)) return i;
    }
    return -1;
}

std::string stateLabel(State s) {
    switch (s) {
        case State::Intact: return "Intact";
        case State::Attacked: return "Sous attaque";
        case State::Weakened: return "Affaibli";
        case State::Destroyed: return "Détruit · gigaboss vaincu";
        case State::Unknown: return "Non découvert";
    }
    return "Intact";
}

Tooltip tooltipOf(const Part& part, State s) {
    Tooltip t;
    t.title = part.name;
    t.state = stateLabel(s);
    if (s == State::Unknown) {
        if (!part.boss.empty()) t.boss = "inconnu";
        return t;
    }
    t.boss = part.boss;
    t.effect = part.effect;
    return t;
}

}  // namespace cosmic::asterion
