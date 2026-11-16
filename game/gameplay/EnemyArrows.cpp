#include "game/gameplay/EnemyArrows.h"

#include <algorithm>
#include <cmath>

namespace cosmic::EnemyArrows {

namespace {

/// The line the arrows' centres run along (the screen, `inset` inside), with a coordinate along
/// it: the distance from its top-left corner, clockwise (top, right, bottom, left side).
struct Frame {
    Rect r;

    float perimeter() const { return 2.0f * (r.w + r.h); }
    float wrap(float t) const {
        const float p = perimeter();
        t = std::fmod(t, p);
        return t < 0.0f ? t + p : t;
    }
    /// 0 top, 1 right, 2 bottom, 3 left.
    int side(float t) const {
        t = wrap(t);
        if (t < r.w) return 0;
        if (t < r.w + r.h) return 1;
        if (t < 2.0f * r.w + r.h) return 2;
        return 3;
    }
    Vec2 at(float t) const {
        t = wrap(t);
        if (t < r.w) return {r.x + t, r.y};
        t -= r.w;
        if (t < r.h) return {r.right(), r.y + t};
        t -= r.h;
        if (t < r.w) return {r.right() - t, r.bottom()};
        t -= r.w;
        return {r.x, r.bottom() - t};
    }
    /// Where the line from the centre towards `p` meets the frame.
    float project(Vec2 p) const {
        const Vec2 c = r.center();
        Vec2 d = p - c;
        if (d.lengthSq() < 1e-6f) d = {0.0f, -1.0f};
        const float hw = r.w * 0.5f, hh = r.h * 0.5f;
        const float tx = std::abs(d.x) > 1e-6f ? hw / std::abs(d.x) : 1e30f;
        const float ty = std::abs(d.y) > 1e-6f ? hh / std::abs(d.y) : 1e30f;
        float t;
        if (tx < ty) {
            const float y = std::clamp(c.y + d.y * tx, r.y, r.bottom());
            t = d.x > 0.0f ? r.w + (y - r.y) : 2.0f * r.w + r.h + (r.bottom() - y);
        } else {
            const float x = std::clamp(c.x + d.x * ty, r.x, r.right());
            t = d.y < 0.0f ? x - r.x : r.w + r.h + (r.right() - x);
        }
        return wrap(t);
    }
};

struct Pack {
    std::vector<Vec2> points;
    bool placed = false;
    float t = 0.0f;
    Arrow arrow;
};

class Placer {
public:
    explicit Placer(const Layout& l) : l_(l) {
        f_.r = {l.screen.x + l.inset, l.screen.y + l.inset, l.screen.w - 2.0f * l.inset, l.screen.h - 2.0f * l.inset};
    }
    bool valid() const { return f_.r.w > 0.0f && f_.r.h > 0.0f && l_.arrowSize > 0.0f; }
    const Frame& frame() const { return f_; }

    /// The arrow of a pack of `count` enemies centred on `target`, at `t` on the frame.
    Arrow make(float t, int count, Vec2 target) const {
        Arrow a;
        a.pos = f_.at(t);
        a.count = count;
        a.target = target;
        const float as = l_.arrowSize;
        a.box = Rect::fromCenter(a.pos, {as, as});
        Vec2 d = target - a.pos;
        if (d.lengthSq() < 1.0f) d = target - f_.r.center();
        if (d.lengthSq() < 1.0f) d = a.pos - f_.r.center();
        a.angle = std::atan2(d.y, d.x);
        a.area = a.box;
        if (count > 1) {
            // On the inner side of the arrow (below it on the top border, left of it on the
            // right one…), always whole on the screen.
            const Vec2 bs = l_.badgeSize;
            Vec2 c = a.pos;
            switch (f_.side(t)) {
                case 0: c.y += as * 0.5f + l_.gap + bs.y * 0.5f; break;
                case 1: c.x -= as * 0.5f + l_.gap + bs.x * 0.5f; break;
                case 2: c.y -= as * 0.5f + l_.gap + bs.y * 0.5f; break;
                default: c.x += as * 0.5f + l_.gap + bs.x * 0.5f; break;
            }
            Rect b = Rect::fromCenter(c, bs);
            b.x = std::clamp(b.x, l_.screen.x, std::max(l_.screen.x, l_.screen.right() - b.w));
            b.y = std::clamp(b.y, l_.screen.y, std::max(l_.screen.y, l_.screen.bottom() - b.h));
            a.badge = b;
            a.area = a.area.united(b);
        }
        a.area = a.area.expanded(l_.gap * 0.5f);
        return a;
    }

    bool fits(const Arrow& a) const {
        if (!l_.screen.containsRect(a.box)) return false;
        if (a.count > 1 && !l_.screen.containsRect(a.badge)) return false;
        for (const Rect& k : l_.keepOut)
            if (!k.empty() && a.area.intersects(k)) return false;
        return true;
    }

    /// Puts the pack's arrow where the line towards its barycentre meets the frame, or at the
    /// nearest free place along it; false when the whole border is taken.
    bool place(Pack& p) const {
        p.placed = false;
        if (p.points.empty()) return false;
        Vec2 b{0.0f, 0.0f};
        for (Vec2 q : p.points) b += q;
        b = b / static_cast<float>(p.points.size());
        const float t0 = f_.project((b - f_.r.center()).lengthSq() >= 1.0f ? b : p.points.front());
        const int count = static_cast<int>(p.points.size());
        const float step = std::max(1.0f, l_.arrowSize * 0.125f);
        const int steps = static_cast<int>(std::ceil(f_.perimeter() * 0.5f / step));
        for (int k = 0; k <= steps; ++k)
            for (int dir = 1; dir >= -1; dir -= 2) {
                if (k == 0 && dir < 0) continue;
                const float t = f_.wrap(t0 + static_cast<float>(dir * k) * step);
                Arrow a = make(t, count, b);
                if (!fits(a)) continue;
                p.arrow = a;
                p.t = t;
                p.placed = true;
                return true;
            }
        return false;
    }

private:
    const Layout& l_;
    Frame f_;
};

}  // namespace

std::vector<Arrow> layout(const Layout& l, const std::vector<Rect>& enemies) {
    std::vector<Arrow> out;
    Placer placer(l);
    if (!placer.valid()) return out;
    const Frame& f = placer.frame();
    // The enemies out of view, by where their direction meets the border.
    struct Item {
        float t;
        Vec2 p;
    };
    std::vector<Item> items;
    for (const Rect& e : enemies) {
        if (e.intersects(l.screen)) continue;  // on screen, even partly: seen, no arrow
        items.push_back({f.project(e.center()), e.center()});
    }
    if (items.empty()) return out;
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.t < b.t; });
    // Packs along the border, starting after its widest empty stretch (a pack across the
    // top-left corner stays whole).
    const size_t n = items.size();
    const float per = f.perimeter();
    size_t start = 0;
    float widest = -1.0f;
    for (size_t i = 0; i < n; ++i) {
        float gap = i == 0 ? items[0].t + per - items[n - 1].t : items[i].t - items[i - 1].t;
        if (gap > widest) {
            widest = gap;
            start = i;
        }
    }
    std::vector<Pack> packs;
    float first = 0.0f, prev = 0.0f;
    for (size_t k = 0; k < n; ++k) {
        const Item& it = items[(start + k) % n];
        const float u = it.t + (start + k >= n ? per : 0.0f);  // unwrapped from the start
        if (packs.empty() || u - prev > l.packGap || u - first > l.packSpan) {
            packs.emplace_back();
            first = u;
        }
        packs.back().points.push_back(it.p);
        prev = u;
    }
    for (Pack& p : packs) placer.place(p);
    // Two arrows (or their counts) that would touch: one arrow for both packs.
    for (bool merged = true; merged;) {
        merged = false;
        for (size_t i = 0; i < packs.size() && !merged; ++i) {
            if (!packs[i].placed) continue;
            for (size_t j = i + 1; j < packs.size(); ++j) {
                if (!packs[j].placed || !packs[i].arrow.area.intersects(packs[j].arrow.area)) continue;
                packs[i].points.insert(packs[i].points.end(), packs[j].points.begin(), packs[j].points.end());
                packs.erase(packs.begin() + static_cast<std::ptrdiff_t>(j));
                placer.place(packs[i]);
                merged = true;
                break;
            }
        }
    }
    std::sort(packs.begin(), packs.end(), [](const Pack& a, const Pack& b) { return a.t < b.t; });
    for (const Pack& p : packs)
        if (p.placed) out.push_back(p.arrow);
    return out;
}

std::vector<Rect> Tracker::update(const Rect& screen, const std::vector<Enemy>& enemies) {
    std::vector<Rect> away;
    std::unordered_set<uint32_t> present;
    for (const Enemy& e : enemies) {
        if (!e.shown) continue;
        present.insert(e.uid);
        if (e.box.intersects(screen)) met_.insert(e.uid);
        else if (met_.count(e.uid)) away.push_back(e.box);
    }
    // Dead, asleep again or gone from the level: forgotten.
    for (auto it = met_.begin(); it != met_.end();) it = present.count(*it) ? std::next(it) : met_.erase(it);
    return away;
}

}  // namespace cosmic::EnemyArrows
