#pragma once

#include <deque>

#include "game/online/OnlineService.h"

namespace cosmic {

class GameServices;

/// In-game notifications of Cosmic Online (new record, record verified or refused, record
/// beaten, synchronisation...), drawn above every screen without blocking it.
class OnlineToasts {
public:
    void update(GameServices& g, float dt);
    void render(GameServices& g);

private:
    struct Toast {
        OnlineService::Notification n;
        float t = 0.0f;
    };
    std::deque<Toast> toasts_;
};

}  // namespace cosmic
