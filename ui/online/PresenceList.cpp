#include "ui/online/PresenceList.h"

#include <algorithm>

#include "ui/online/OnlineWidgets.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::presencelist {

namespace {

int rankOf(const std::string& presence) {
    if (presence == "online") return 0;
    if (presence == "away") return 1;
    if (presence == "offline") return 2;
    return 3;  // no presence (not sent, or not fresh)
}

}  // namespace

std::string shown(const Json& entry, bool fromCache) {
    if (fromCache) return {};
    const std::string p = jstr(entry, "presence");
    if (p == "online" || p == "away" || p == "offline") return p;
    if (p == "hidden") return "offline";
    return {};
}

int onlineCount(const Json& data, const char* key, int total, bool fromCache) {
    if (fromCache || !data.is_object() || total < 0) return -1;
    auto it = data.find(key);
    if (it == data.end() || !it->is_number_integer()) return -1;
    const long long n = it->get<long long>();
    return n < 0 || n > total ? -1 : static_cast<int>(n);
}

std::string counterText(int online, int total) {
    if (online < 0 || total < 0) return {};
    return std::to_string(online) + "/" + std::to_string(total) + " en ligne";
}

std::vector<size_t> orderByPresence(const Json& list, bool fromCache) {
    std::vector<size_t> order;
    if (!list.is_array()) return order;
    std::vector<int> rank;
    rank.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        order.push_back(i);
        rank.push_back(rankOf(shown(list[i], fromCache)));
    }
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return rank[a] < rank[b]; });
    return order;
}

HeaderLayout layoutHeader(UIContext& ui, float x, float right, float cy, const std::string& title, float titleSize, FontFace titleFace,
                          const std::string& counter, float counterSize, FontFace counterFace, float counterShare) {
    HeaderLayout L;
    const float line = std::max(0.0f, right - x);
    const float h = ui.s(std::max(titleSize, counterSize)) * 1.4f;
    float used = 0.0f;
    if (!counter.empty() && line > 0.0f) {
        // The counter first: its own width (a pixel more for the rounding), within its share.
        const float full = ui.text().measure(counter, counterFace, ui.s(counterSize)).x + ui.s(1.0f);
        const float room = std::min(full, line * counterShare);
        L.counterSize = ui.fitFont(counter, counterFace, counterSize, room, 0.75f);
        L.counterText = ui.elide(counter, counterFace, L.counterSize, room);
        L.counter = {right - room, cy - h * 0.5f, room, h};
        used = room + ui.s(12);
    }
    const float troom = std::max(0.0f, line - used);
    L.titleSize = ui.fitFont(title, titleFace, titleSize, troom, 0.75f);
    L.titleText = ui.elide(title, titleFace, L.titleSize, troom);
    L.title = {x, cy - h * 0.5f, troom, h};
    return L;
}

void drawHeader(UIContext& ui, const HeaderLayout& L, FontFace titleFace, Color titleColor, FontFace counterFace, Color counterColor) {
    if (!L.titleText.empty() && L.title.w > 0.0f)
        ui.label(L.titleText, {L.title.x, L.title.center().y}, L.titleSize, titleColor, Align::Left, titleFace, VAlign::Middle);
    if (!L.counterText.empty() && L.counter.w > 0.0f)
        ui.label(L.counterText, {L.counter.right(), L.counter.center().y}, L.counterSize, counterColor, Align::Right, counterFace, VAlign::Middle);
}

Color counterColor(int online, Color none) { return online > 0 ? presenceDotColor("online") : none; }

}  // namespace cosmic::presencelist
