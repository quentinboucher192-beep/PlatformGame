#include "engine/input/InputScript.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "engine/core/Log.h"

namespace cosmic {

namespace {
Action parseAction(const std::string& n) {
    if (n == "left") return Action::MoveLeft;
    if (n == "right") return Action::MoveRight;
    if (n == "up") return Action::MoveUp;
    if (n == "down") return Action::MoveDown;
    return actionFromId(n);
}
}  // namespace

bool InputScript::parse(const std::string& script) {
    entries_.clear();
    std::stringstream ss(script);
    std::string item;
    while (std::getline(ss, item, ';')) {
        size_t c = item.find(':');
        if (c == std::string::npos) continue;
        Entry e;
        e.step = std::atoi(item.substr(0, c).c_str());
        std::stringstream as(item.substr(c + 1));
        std::string a;
        while (std::getline(as, a, ',')) {
            if (a.empty()) continue;
            if (a == "leave") {
                e.choice = 255;
                continue;
            }
            if (a.size() == 7 && a.rfind("choice", 0) == 0 && a[6] >= '1' && a[6] <= '8') {
                e.choice = static_cast<uint8_t>(a[6] - '0');
                continue;
            }
            Action act = parseAction(a);
            if (act == Action::Count) {
                Log::warn("Input script: unknown action '{}'", a);
                continue;
            }
            e.actions.push_back(act);
        }
        entries_.push_back(e);
    }
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) { return a.step < b.step; });
    return !entries_.empty();
}

PlayerInput InputScript::next() {
    PlayerInput in;
    while (index_ < entries_.size() && entries_[index_].step <= step_) {
        std::array<bool, kActionCount> now{};
        for (Action a : entries_[index_].actions) now[static_cast<int>(a)] = true;
        for (int i = 0; i < kActionCount; ++i) {
            if (now[i] && !held_[i]) in.pressed[i] = true;
            if (!now[i] && held_[i]) in.released[i] = true;
        }
        held_ = now;
        if (entries_[index_].choice) choice_ = entries_[index_].choice;
        ++index_;
    }
    in.held = held_;
    float x = 0, y = 0;
    if (held_[static_cast<int>(Action::MoveLeft)]) x -= 1;
    if (held_[static_cast<int>(Action::MoveRight)]) x += 1;
    if (held_[static_cast<int>(Action::MoveUp)]) y -= 1;
    if (held_[static_cast<int>(Action::MoveDown)]) y += 1;
    in.move = {x, y};
    ++step_;
    return in;
}

}  // namespace cosmic
