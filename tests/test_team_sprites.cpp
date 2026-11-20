// Display only: every character drawn animates like the player of a solo game. A team's other
// players (coop seats, online or on one PC; a raid's players and its simulated players; the
// replay of a team) had no GameWorld::update of their own: they stayed on the FIRST frame of the
// last clip their simulation started (idle; a hit; the beam of a revival, a thin vertical line
// in the colours of their suit), and their dash afterimages never faded (rows of cyan figures
// frozen across a raid's simulation); and the outline of their colour covered their whole body.
// Nothing here touches a run: the digests stay those of tests/test_coop.cpp and test_determinism.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Paths.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/cosmetics/SuitRecolor.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/online/LevelFingerprint.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

constexpr float kImage = 1.0f / 60.0f;  // one image drawn: two steps of the simulation

int bit(Action a) { return 1 << static_cast<int>(a); }

/// What the renderer draws for a player (Player::render, GameRenderer::drawTeam): its clip and frame,
/// the source rectangle in its sheet, the rectangle on the world around its feet (frame size, pivot,
/// scale), its side.
struct Drawn {
    std::string clip;
    int frame = 0;
    Rect src, dst;
    bool flip = false;
    bool operator==(const Drawn& o) const {
        auto same = [](const Rect& a, const Rect& b) {
            return std::abs(a.x - b.x) < 1e-3f && std::abs(a.y - b.y) < 1e-3f && std::abs(a.w - b.w) < 1e-3f && std::abs(a.h - b.h) < 1e-3f;
        };
        return clip == o.clip && frame == o.frame && same(src, o.src) && same(dst, o.dst) && flip == o.flip;
    }
};

Drawn drawnOf(const Player& p) {
    Drawn d;
    d.clip = p.anim.current();
    d.frame = p.anim.frame();
    d.src = p.anim.sourceRect();
    d.flip = p.facing < 0.0f;
    const Vec2 feet = p.renderFeet(0.5f);
    d.dst = p.anim.destRect(feet, d.flip).translated(Vec2{-feet.x, -feet.y});
    return d;
}

struct Services {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    Services() {
        content.reload();
        res.init(nullptr, &audio);
    }
    WorldServices ws() { return WorldServices{res, audio, content, nullptr, 0.0f, 1.0f}; }
};

Services& services() {
    static Services s;
    return s;
}

/// A world on the flat test level (no intro): solo (`team` 0) or a team of `team` players, each
/// with the dash on its first slot (keys per slot, as in coop).
std::unique_ptr<GameWorld> flatWorld(int team) {
    Services& s = services();
    LevelData l = LevelData::makeEmpty("test_team_sprites", "Test", "sylvanox");
    l.intro.steps.clear();
    auto w = std::make_unique<GameWorld>(s.ws(), WorldMode::Play);
    if (team > 1) w->setTeamSize(team);
    w->setKeysPerSlot(true);
    w->load(l);
    w->seedGameplay(levelSeed("test_team_sprites"));
    if (const PowerDefinition* dash = s.content.power("dash"))
        for (int i = 0; i < w->teamSize(); ++i) w->playerAt(i).grantPower(*dash);
    return w;
}

/// A scripted run through the clips a player shows on the ground and in the air: standing, walking
/// and running both ways, jumps and falls, the double jump, dashes, crouching, a strike, a hit.
InputFrame scripted(int step) {
    InputFrame f;
    auto hold = [&](Action a) { f.held |= static_cast<uint32_t>(bit(a)); };
    auto right = [&](int8_t x = 127) {
        hold(Action::MoveRight);
        f.moveX = x;
    };
    auto left = [&] {
        hold(Action::MoveLeft);
        f.moveX = -127;
    };
    if (step < 120) return f;                                  // standing
    if (step < 150) right(50);                                 // walking
    else if (step < 300) right();                              // running
    if (step >= 240 && step < 270) hold(Action::Jump);         // a jump, its fall
    if (step >= 330 && step < 345) hold(Action::Jump);         // a jump...
    if (step >= 352 && step < 362) hold(Action::Jump);         // ...and a jump in the air
    if (step >= 300 && step < 420) left();                     // back the other way
    if (step >= 430 && step < 436) hold(Action::PowerSlot1);   // a dash, standing
    if (step >= 480 && step < 560) {                           // a dash after a jump
        right();
        if (step < 500) hold(Action::Jump);
        if (step >= 505 && step < 510) hold(Action::PowerSlot1);
    }
    if (step >= 620 && step < 700) f.moveY = 127;              // crouching
    if (step >= 760 && step < 765) hold(Action::Attack);       // a strike
    return f;
}

/// Edges of a frame from the one before (what InputFrame carries).
InputFrame withEdges(InputFrame f, const InputFrame& prev) {
    f.pressed = f.held & ~prev.held;
    f.released = prev.held & ~f.held;
    return f;
}

void hit(GameWorld& w, int seat) {
    GameWorld::PlayerScope who(w, seat);
    Player& p = w.playerAt(seat);
    DamageInfo d;
    d.amount = 1;
    d.type = DamageType::Melee;
    d.source = p.center() + Vec2{-20.0f, 0.0f};
    d.knockback = 160.0f;
    p.hurt(w, d);
}

}  // namespace

TEST_CASE("team sprites: every seat of a team is drawn as the solo player, image after image, through every clip") {
    for (int n : {2, 3}) {
        auto solo = flatWorld(0);
        auto team = flatWorld(n);
        InputFrame prev;
        std::set<std::string> clips;
        int compared = 0, differing = 0, maxFrame[3] = {0, 0, 0};
        for (int step = 0; step < 960; ++step) {
            const InputFrame f = withEdges(scripted(step), prev);
            prev = f;
            stepWorld(*solo, f, kSimDt);
            std::vector<InputFrame> frames(static_cast<size_t>(n), f);
            stepTeam(*team, frames.data(), n, kSimDt);
            if (step == 860) {  // a hit, the same for everyone
                hit(*solo, 0);
                for (int i = 0; i < n; ++i) hit(*team, i);
            }
            if (step % 2 == 0) continue;
            solo->update(kImage);
            team->update(kImage);
            // From the first move (before it, each idle began at its own moment: the same clip, another phase).
            if (step < 121) continue;
            const Drawn want = drawnOf(solo->player());
            clips.insert(want.clip);
            for (int i = 0; i < n; ++i) {
                const Drawn got = drawnOf(team->playerAt(i));
                maxFrame[i] = std::max(maxFrame[i], got.frame);
                ++compared;
                if (!(got == want)) {
                    if (++differing <= 6)
                        std::printf("  %d joueurs, siège %d, pas %d : « %s » image %d (solo : « %s » image %d)\n", n, i, step, got.clip.c_str(), got.frame,
                                    want.clip.c_str(), want.frame);
                }
            }
        }
        CHECK(compared > 0);
        CHECK(differing == 0);
        for (int i = 0; i < n; ++i) CHECK(maxFrame[i] > 0);  // the frames advance (before: seat 0 only)
        for (const char* c : {"idle", "walk", "run", "jump", "fall", "double_jump", "dash", "crouch", "attack", "hurt"}) {
            if (!clips.count(c)) std::printf("  clip absent du parcours : %s\n", c);
            CHECK(clips.count(c) == 1);
        }
    }
}

TEST_CASE("team sprites: a player revived plays its whole materialisation, then stands (never a still beam of light)") {
    auto w = flatWorld(3);
    InputFrame none, interact;
    interact.held = static_cast<uint32_t>(bit(Action::Interact));
    auto step = [&](int seat0Holds, int ticks, bool draw) {
        for (int k = 0; k < ticks; ++k) {
            std::vector<InputFrame> frames(3, none);
            if (seat0Holds) frames[0] = interact;
            stepTeam(*w, frames.data(), 3, kSimDt);
            if (draw && k % 2 == 1) w->update(kImage);
        }
    };
    step(0, 60, true);
    const float ground = w->playerAt(0).feet().y;
    for (int seat : {1, 2}) {
        w->playerAt(0).teleportTo({500.0f, ground});
        w->playerAt(seat).teleportTo({700.0f, ground});
        {
            GameWorld::PlayerScope s(*w, seat);
            w->playerAt(seat).kill(*w);
        }
        step(0, 150, true);
        CHECK(w->member(seat).presence == GameWorld::Presence::Down);
        w->playerAt(0).teleportTo(w->member(seat).soul + Vec2{-20.0f, 0.0f});
        for (int k = 0; k < 4 * kSimHz && w->member(seat).presence == GameWorld::Presence::Down; ++k) step(1, 1, true);
        CHECK(w->member(seat).presence == GameWorld::Presence::Playing);
        // The materialisation (respawn: 10 frames in 0.76 s), image after image.
        std::set<int> frames;
        bool sawRespawn = false;
        for (int k = 0; k < 2 * 140; ++k) {
            step(0, 1, false);
            if (k % 2 == 0) continue;
            w->update(kImage);
            const Player& p = w->playerAt(seat);
            if (p.anim.current() == "respawn") {
                sawRespawn = true;
                frames.insert(p.anim.frame());
            }
        }
        CHECK(sawRespawn);
        std::printf("  siège %d ranimé : %zu images de la matérialisation montrées, puis « %s »\n", seat, frames.size(),
                    w->playerAt(seat).anim.current().c_str());
        CHECK(frames.size() >= 8);  // before: frame 0 only, a thin beam for as long as nothing else played
        CHECK(w->playerAt(seat).anim.current() == "idle");
        CHECK(w->playerAt(seat).anim.sourceRect().w == 32.0f);
    }
}

TEST_CASE("team sprites: dash afterimages fade and stay few, drawn or not (coop seats, a world stepped unseen)") {
    for (bool drawn : {true, false}) {
        auto w = flatWorld(3);
        InputFrame prev;
        size_t most[3] = {0, 0, 0};
        bool made[3] = {false, false, false};
        int lastDash = 0;
        for (int step = 0; step < 120 * 12; ++step) {
            InputFrame f;
            f.held = static_cast<uint32_t>(bit(step % 240 < 120 ? Action::MoveRight : Action::MoveLeft));
            f.moveX = step % 240 < 120 ? 127 : -127;
            if (step % 50 < 4) {
                f.held |= static_cast<uint32_t>(bit(Action::PowerSlot1));
                lastDash = step;
            }
            if (step % 90 < 12) f.held |= static_cast<uint32_t>(bit(Action::Jump));
            f = withEdges(f, prev);
            prev = f;
            std::vector<InputFrame> frames(3, f);
            stepTeam(*w, frames.data(), 3, kSimDt);
            if (drawn && step % 2 == 1) w->update(kImage);
            for (int i = 0; i < 3; ++i) {
                most[i] = std::max(most[i], w->playerAt(i).afterimageCount());
                made[i] = made[i] || w->playerAt(i).afterimageCount() > 0;
            }
        }
        // Then standing still: in less than their life, none left.
        const int still = static_cast<int>(Player::kAfterimageLife * kSimHz) + 2 + 12;
        for (int k = 0; k < still; ++k) {
            std::vector<InputFrame> frames(3, InputFrame{});
            stepTeam(*w, frames.data(), 3, kSimDt);
            if (drawn && k % 2 == 1) w->update(kImage);
        }
        CHECK(lastDash > 0);
        for (int i = 0; i < 3; ++i) {
            std::printf("  %s, siège %d : au plus %zu images rémanentes, %zu à la fin\n", drawn ? "dessiné" : "jamais dessiné", i, most[i],
                        w->playerAt(i).afterimageCount());
            CHECK(made[i]);
            CHECK(most[i] <= Player::kMaxAfterimages);
            CHECK(w->playerAt(i).afterimageCount() == 0);
        }
    }
}

TEST_CASE("team sprites: another player's outline goes around its body, never over it") {
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    CHECK(loadRgbaImage(Paths::assetsDir() / "sprites" / "player" / "nova.png", px, w, h));
    const std::vector<uint8_t> o = silhouetteOutline(px, w, h, 32, 32);
    CHECK(o.size() == px.size());
    if (o.size() != px.size()) return;
    size_t ring = 0, overBody = 0, alone = 0, open = 0;
    auto solid = [&](int x, int y) { return px[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4 + 3] > 24; };
    auto lined = [&](int x, int y) { return o[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4 + 3] > 0; };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const bool cellLeft = x % 32 > 0, cellRight = x % 32 < 31, cellUp = y % 32 > 0, cellDown = y % 32 < 31;
            if (lined(x, y)) {
                ++ring;
                if (solid(x, y)) ++overBody;  // over the suit: its colours hidden
                const bool touches = (cellLeft && solid(x - 1, y)) || (cellRight && solid(x + 1, y)) || (cellUp && solid(x, y - 1)) ||
                                     (cellDown && solid(x, y + 1));
                if (!touches) ++alone;
            } else if (!solid(x, y)) {
                const bool touches = (cellLeft && solid(x - 1, y)) || (cellRight && solid(x + 1, y)) || (cellUp && solid(x, y - 1)) ||
                                     (cellDown && solid(x, y + 1));
                if (touches) ++open;  // a gap in the outline
            }
        }
    std::printf("  liseré : %zu pixels autour de Nova, %zu sur son corps, %zu isolés, %zu trous\n", ring, overBody, alone, open);
    CHECK(ring > 1000);
    CHECK(overBody == 0);
    CHECK(alone == 0);
    CHECK(open == 0);
    CHECK(GameRenderer::kOtherPlayerOutlineAlpha >= 150);
}
