#include "game/raid/bots/BotTypes.h"

#include <algorithm>
#include <cmath>

namespace cosmic::raid::bots {

const char* roleName(Role r) {
    switch (r) {
        case Role::Lure: return "Appât";
        case Role::Striker: return "Frappeur";
        case Role::Reviver: return "Réanimateur";
        case Role::Runner: return "Coureur";
        default: return "Libre";
    }
}

const char* orderName(OrderKind k) {
    switch (k) {
        case OrderKind::Follow: return "Suivez-moi";
        case OrderKind::Wait: return "Attendez ici";
        case OrderKind::Objective: return "Tous sur l'objectif";
        case OrderKind::Revive: return "Réanimez-le";
        case OrderKind::Free: return "Libres";
        case OrderKind::Mark: return "Signal";
        default: return "";
    }
}

InputFrame withOrder(InputFrame f, OrderKind k, Vec2 at) {
    const uint32_t x = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(at.x / 4.0f)), 0, 2047));
    const uint32_t y = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(at.y / 4.0f)), 0, 1023));
    const uint32_t packed = (static_cast<uint32_t>(k) & 7u) | (x << 3) | (y << 14);
    f.flags = static_cast<uint8_t>(f.flags | kOrderFlag);
    f.choice = static_cast<uint8_t>(packed & 0xFFu);
    f.moveX = static_cast<int8_t>(static_cast<uint8_t>((packed >> 8) & 0xFFu));
    f.moveY = static_cast<int8_t>(static_cast<uint8_t>((packed >> 16) & 0xFFu));
    return f;
}

bool orderOf(const InputFrame& f, Order& out) {
    if (!(f.flags & kOrderFlag)) return false;
    const uint32_t packed = static_cast<uint32_t>(f.choice) | (static_cast<uint32_t>(static_cast<uint8_t>(f.moveX)) << 8) |
                            (static_cast<uint32_t>(static_cast<uint8_t>(f.moveY)) << 16);
    const uint32_t k = packed & 7u;
    if (k == 0 || k > static_cast<uint32_t>(OrderKind::Mark)) return false;
    out.kind = static_cast<OrderKind>(k);
    out.at = {static_cast<float>((packed >> 3) & 2047u) * 4.0f, static_cast<float>((packed >> 14) & 1023u) * 4.0f};
    return true;
}

InputFrame withoutOrder(InputFrame f) {
    if (!(f.flags & kOrderFlag)) return f;
    f.flags = static_cast<uint8_t>(f.flags & ~kOrderFlag);
    f.choice = 0;
    f.moveX = 0;
    f.moveY = 0;
    return f;
}

}  // namespace cosmic::raid::bots
