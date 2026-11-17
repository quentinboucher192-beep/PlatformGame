// Arrows towards the enemies out of view (2.52, display only): one per pack on the border of the
// screen, « ×N » for several, turned towards the pack's barycentre; none for an enemy on screen
// nor for one never met; never over the HUD, the boss arrow or each other (packs that would land
// at the same place become one).

#include <cmath>
#include <string>
#include <random>

#include "game/gameplay/EnemyArrows.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

const Rect kScreen{0.0f, 0.0f, 1920.0f, 1080.0f};

EnemyArrows::Layout layout1080() {
    EnemyArrows::Layout l;
    l.screen = kScreen;
    return l;
}

Rect enemyAt(Vec2 c) { return Rect::fromCenter(c, {40.0f, 60.0f}); }

Vec2 barycentre(const std::vector<Vec2>& pts) {
    Vec2 b;
    for (Vec2 p : pts) b += p;
    return b / static_cast<float>(pts.size());
}

/// The arrow is turned towards `p` (from its own centre).
bool pointsAt(const EnemyArrows::Arrow& a, Vec2 p, float eps = 0.01f) {
    const float want = std::atan2(p.y - a.pos.y, p.x - a.pos.x);
    return std::abs(std::remainder(a.angle - want, 6.2831853f)) <= eps;
}

/// The arrow's centre is on the frame `inset` inside the screen.
bool onBorder(const EnemyArrows::Layout& l, Vec2 p) {
    const Rect f{l.inset, l.inset, kScreen.w - 2.0f * l.inset, kScreen.h - 2.0f * l.inset};
    const float e = 0.5f;
    const bool inside = p.x >= f.x - e && p.x <= f.right() + e && p.y >= f.y - e && p.y <= f.bottom() + e;
    const bool edge = std::abs(p.x - f.x) < e || std::abs(p.x - f.right()) < e || std::abs(p.y - f.y) < e || std::abs(p.y - f.bottom()) < e;
    return inside && edge;
}

}  // namespace

TEST_CASE("enemy arrows: violet for the enemies of the ambush being fought, the usual arrow for the others") {
    using namespace EnemyArrows;
    const Look fight = look(true), usual = look(false);
    CHECK(std::string(fight.texture) == "ui/ambush_arrow.png");
    CHECK(std::string(usual.texture) == "ui/enemy_arrow.png");  // unchanged, during an ambush too
    CHECK(fight.count.r == kAmbushColor.r && fight.count.g == kAmbushColor.g && fight.count.b == kAmbushColor.b);
    CHECK(usual.count.r == kUsualColor.r && usual.count.g == kUsualColor.g && usual.count.b == kUsualColor.b);
    CHECK(fight.count.r != usual.count.r || fight.count.g != usual.count.g || fight.count.b != usual.count.b);
}

TEST_CASE("enemy arrows: a lone enemy out of view, one arrow without a count") {
    EnemyArrows::Layout l = layout1080();
    // Left of the screen, at mid-height: on the left border, turned left.
    auto arrows = EnemyArrows::layout(l, {enemyAt({-300.0f, 540.0f})});
    CHECK(arrows.size() == 1);
    CHECK(arrows[0].count == 1);
    CHECK(arrows[0].badge.empty());
    CHECK_NEAR(arrows[0].pos.x, l.inset, 0.5);
    CHECK_NEAR(arrows[0].pos.y, 540.0, 0.5);
    CHECK(pointsAt(arrows[0], {-300.0f, 540.0f}));
    CHECK(kScreen.containsRect(arrows[0].box));
    // Far above on the right: on the border where the line towards it leaves the screen.
    const Vec2 far{2600.0f, -900.0f};
    arrows = EnemyArrows::layout(l, {enemyAt(far)});
    CHECK(arrows.size() == 1);
    CHECK(arrows[0].count == 1);
    CHECK(onBorder(l, arrows[0].pos));
    CHECK(pointsAt(arrows[0], far));
    const Vec2 c = kScreen.center();
    CHECK_NEAR(std::atan2(arrows[0].pos.y - c.y, arrows[0].pos.x - c.x), std::atan2(far.y - c.y, far.x - c.x), 0.01);
}

TEST_CASE("enemy arrows: a pack of five, one arrow « ×5 » turned towards its barycentre") {
    EnemyArrows::Layout l = layout1080();
    const std::vector<Vec2> pack = {{1150.0f, -250.0f}, {1230.0f, -300.0f}, {1190.0f, -420.0f}, {1260.0f, -220.0f}, {1120.0f, -350.0f}};
    std::vector<Rect> boxes;
    for (Vec2 p : pack) boxes.push_back(enemyAt(p));
    auto arrows = EnemyArrows::layout(l, boxes);
    CHECK(arrows.size() == 1);
    const EnemyArrows::Arrow& a = arrows[0];
    CHECK(a.count == 5);
    const Vec2 b = barycentre(pack);
    CHECK_NEAR(a.target.x, b.x, 0.01);
    CHECK_NEAR(a.target.y, b.y, 0.01);
    CHECK(pointsAt(a, b));
    // On the top border, where the line from the centre towards the barycentre meets it.
    CHECK_NEAR(a.pos.y, l.inset, 0.5);
    const Vec2 c = kScreen.center();
    CHECK_NEAR(std::atan2(a.pos.y - c.y, a.pos.x - c.x), std::atan2(b.y - c.y, b.x - c.x), 0.01);
    // Its count: under the arrow (inner side), whole on the screen, never on the arrow.
    CHECK(!a.badge.empty());
    CHECK(kScreen.containsRect(a.badge));
    CHECK(a.badge.y >= a.box.bottom());
    CHECK(!a.badge.intersects(a.box));
    CHECK(a.area.containsRect(a.box) && a.area.containsRect(a.badge));
}

TEST_CASE("enemy arrows: two packs on opposite sides, two arrows") {
    EnemyArrows::Layout l = layout1080();
    const std::vector<Vec2> left = {{-400.0f, 480.0f}, {-450.0f, 540.0f}, {-380.0f, 600.0f}};
    const std::vector<Vec2> right = {{2300.0f, 560.0f}, {2350.0f, 640.0f}};
    std::vector<Rect> boxes;
    for (Vec2 p : left) boxes.push_back(enemyAt(p));
    for (Vec2 p : right) boxes.push_back(enemyAt(p));
    auto arrows = EnemyArrows::layout(l, boxes);
    CHECK(arrows.size() == 2);
    // Clockwise from the top left: the right border first.
    CHECK(arrows[0].count == 2);
    CHECK_NEAR(arrows[0].pos.x, kScreen.w - l.inset, 0.5);
    CHECK(pointsAt(arrows[0], barycentre(right)));
    CHECK(arrows[0].badge.right() <= arrows[0].box.x);  // left of it: towards the inside
    CHECK(arrows[1].count == 3);
    CHECK_NEAR(arrows[1].pos.x, l.inset, 0.5);
    CHECK(pointsAt(arrows[1], barycentre(left)));
    CHECK(arrows[1].badge.x >= arrows[1].box.right());
    CHECK(!arrows[0].area.intersects(arrows[1].area));
}

TEST_CASE("enemy arrows: a pack across a corner stays one") {
    EnemyArrows::Layout l = layout1080();
    // Both far towards the top-left corner, one just above its diagonal, one just left of it.
    const std::vector<Vec2> pack = {{-1759.0f, -969.0f}, {-1809.0f, -919.0f}};
    auto arrows = EnemyArrows::layout(l, {enemyAt(pack[0]), enemyAt(pack[1])});
    CHECK(arrows.size() == 1);
    CHECK(arrows[0].count == 2);
    CHECK(pointsAt(arrows[0], barycentre(pack)));
    CHECK(kScreen.containsRect(arrows[0].badge));
}

TEST_CASE("enemy arrows: an enemy on screen, even partly, gets none") {
    EnemyArrows::Layout l = layout1080();
    CHECK(EnemyArrows::layout(l, {enemyAt({900.0f, 500.0f})}).empty());
    CHECK(EnemyArrows::layout(l, {enemyAt({-10.0f, 500.0f})}).empty());   // its right half on screen
    CHECK(EnemyArrows::layout(l, {enemyAt({1000.0f, 1100.0f})}).empty());  // its top on screen
    CHECK(EnemyArrows::layout(l, {}).empty());
    // One on screen, one out of view: one arrow, for the one out of view.
    auto arrows = EnemyArrows::layout(l, {enemyAt({900.0f, 500.0f}), enemyAt({900.0f, 1600.0f})});
    CHECK(arrows.size() == 1);
    CHECK(arrows[0].count == 1);
    CHECK_NEAR(arrows[0].pos.y, kScreen.h - l.inset, 0.5);
}

TEST_CASE("enemy arrows: only the enemies already met, forgotten once dead or gone") {
    using T = EnemyArrows::Tracker;
    T t;
    const Rect away = enemyAt({-500.0f, 400.0f}), here = enemyAt({800.0f, 400.0f});
    // Never on screen: no arrow, whatever happens elsewhere.
    CHECK(t.update(kScreen, {{1, away, true}}).empty());
    CHECK(!t.met(1));
    // Seen on screen: met; out of view afterwards: its arrow.
    CHECK(t.update(kScreen, {{1, away, true}, {2, here, true}}).empty());
    CHECK(t.met(2));
    auto out = t.update(kScreen, {{1, away, true}, {2, enemyAt({2400.0f, 500.0f}), true}});
    CHECK(out.size() == 1);
    CHECK_NEAR(out[0].center().x, 2400.0, 0.01);
    CHECK(!t.met(1));
    CHECK(EnemyArrows::layout(layout1080(), out).size() == 1);
    // Asleep, a boss, dead (not shown): no arrow, and forgotten.
    out = t.update(kScreen, {{1, away, true}, {2, enemyAt({2400.0f, 500.0f}), false}});
    CHECK(out.empty());
    CHECK(!t.met(2));
    // Back out of view: it has to be seen again.
    CHECK(t.update(kScreen, {{2, enemyAt({2400.0f, 500.0f}), true}}).empty());
    // Gone from the level: forgotten too.
    t.update(kScreen, {{3, here, true}});
    CHECK(t.met(3));
    t.update(kScreen, {});
    CHECK(!t.met(3));
    CHECK(t.size() == 0);
    // A new attempt.
    t.update(kScreen, {{4, here, true}});
    t.reset();
    CHECK(!t.met(4));
    CHECK(t.update(kScreen, {{4, away, true}}).empty());
}

TEST_CASE("enemy arrows: clear of the HUD; packs that would land at the same place become one") {
    EnemyArrows::Layout l = layout1080();
    // The HUD over the top left: the arrow of a pack behind it slides along the border, onto
    // the place of another pack's arrow: one arrow for both.
    l.keepOut = {{0.0f, 0.0f, 1000.0f, 200.0f}};
    const std::vector<Vec2> a = {{500.0f, -300.0f}, {520.0f, -340.0f}};
    const Vec2 b{1080.0f, -300.0f};
    auto alone = EnemyArrows::layout(l, {enemyAt(b)});
    CHECK(alone.size() == 1);
    auto arrows = EnemyArrows::layout(l, {enemyAt(a[0]), enemyAt(a[1]), enemyAt(b)});
    CHECK(arrows.size() == 1);
    CHECK(arrows[0].count == 3);
    CHECK(!arrows[0].area.intersects(l.keepOut[0]));
    CHECK(pointsAt(arrows[0], barycentre({a[0], a[1], b})));
    // Without the HUD there, two arrows.
    l.keepOut.clear();
    CHECK(EnemyArrows::layout(l, {enemyAt(a[0]), enemyAt(a[1]), enemyAt(b)}).size() == 2);
}

TEST_CASE("enemy arrows: many enemies around, never an arrow over another nor over the HUD") {
    EnemyArrows::Layout l = layout1080();
    // A HUD as the game draws it: health top left, timer top right, progress bar top centre,
    // boss bar bottom centre, a boss arrow on the right.
    l.keepOut = {{16.0f, 16.0f, 440.0f, 110.0f}, {1626.0f, 26.0f, 278.0f, 182.0f}, {541.0f, 10.0f, 980.0f, 108.0f},
                 {504.0f, 966.0f, 912.0f, 78.0f}, {1780.0f, 400.0f, 126.0f, 126.0f}};
    std::mt19937 rng(2051);
    std::uniform_real_distribution<float> x(-1500.0f, 3400.0f), y(-1200.0f, 2300.0f);
    std::uniform_int_distribution<int> count(1, 40);
    for (int round = 0; round < 300; ++round) {
        std::vector<Rect> boxes;
        int away = 0;
        for (int i = count(rng); i-- > 0;) {
            Rect e = enemyAt({x(rng), y(rng)});
            boxes.push_back(e);
            if (!e.intersects(kScreen)) ++away;
        }
        auto arrows = EnemyArrows::layout(l, boxes);
        int counted = 0;
        for (size_t i = 0; i < arrows.size(); ++i) {
            const EnemyArrows::Arrow& a = arrows[i];
            CHECK(a.count >= 1);
            counted += a.count;
            CHECK(onBorder(l, a.pos));
            CHECK(kScreen.containsRect(a.box));
            CHECK(a.badge.empty() == (a.count == 1));
            if (a.count > 1) CHECK(kScreen.containsRect(a.badge) && !a.badge.intersects(a.box));
            CHECK(pointsAt(a, a.target, 0.001f));
            for (const Rect& k : l.keepOut) CHECK(!a.area.intersects(k));
            for (size_t j = i + 1; j < arrows.size(); ++j) CHECK(!a.area.intersects(arrows[j].area));
        }
        CHECK(counted == away);  // every enemy out of view in one pack
        CHECK(arrows.size() <= static_cast<size_t>(away));
    }
}
