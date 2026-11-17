#include "game/coop/CoopLink.h"

namespace cosmic {

Color coopColor(int seat) {
    // As the lobby and the HUD show them (docs/COOP.md): 1 cyan, 2 pink, 3 gold.
    static const Color kColors[3] = {{62, 230, 214, 255}, {255, 122, 184, 255}, {255, 210, 122, 255}};
    return kColors[seat < 0 ? 0 : seat > 2 ? 2 : seat];
}

}  // namespace cosmic
