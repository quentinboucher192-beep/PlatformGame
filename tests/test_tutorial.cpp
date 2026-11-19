// 2.64: the tutorial of the first launch, « Le premier envol de Nova » (game/tutorial/Tutorial.h,
// data/tutorial, tools/levelgen/tutorial.py): its level is finished by a script of inputs (and
// replays the same), every explanation card stops the run while it is open, it is offered to a
// new player only and never again (finished, skipped or merely started; kept by the copies and
// the migration of the progression), its boss is the easiest one of the game slowed down, its
// cards never overlap, and the levels of the campaign keep their fingerprints.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/BossDifficulty.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/ProgressMerge.h"
#include "game/progression/Progression.h"
#include "game/tutorial/Tutorial.h"
#include "tests/TestFramework.h"
#include "ui/screens/GameScene.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/TutorialScenes.h"

using namespace cosmic;

namespace {

ContentDatabase& content() {
    static ContentDatabase c;
    static bool loaded = false;
    if (!loaded) {
        c.reload();
        loaded = true;
    }
    return c;
}

LevelData tutorialLevel() {
    std::string err;
    auto level = tutorial::loadLevel(&err);
    if (!level) std::printf("  didacticiel illisible : %s\n", err.c_str());
    CHECK(level.has_value());
    return level ? *level : LevelData{};
}

struct TutorialSim {
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    std::vector<std::string> cards;  // the cards taken, in order (the scene would show them)

    TutorialSim() {
        res.init(nullptr, &audio);
        WorldServices s{res, audio, content(), nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->setKeysPerSlot(true);  // as the game plays it (each power slot has its key)
        CHECK(w->load(tutorialLevel()));
        w->setTraining(true);  // as GameScene plays the tutorial: nothing counts
        w->seedGameplay(levelSeed(w->level().id));
    }
    void step(InputFrame f) {
        if (w->cinematic().active() && w->cinematic().skippable()) f.flags |= InputFrame::SkipCinematic;
        stepWorld(*w, f, kSimDt);
        while (auto c = w->takeTutorialCard()) cards.push_back(*c);
    }
};

/// The bubble Nova stands on, if any.
MovingPlatformEntity* bubbleUnder(GameWorld& w) {
    const Collider* c = w.physics().get(w.player().groundCollider());
    if (!c || !c->dynamic || !c->owner) return nullptr;
    auto* mp = dynamic_cast<MovingPlatformEntity*>(static_cast<LevelEntity*>(c->owner));
    return mp && mp->isBubble() ? mp : nullptr;
}

MovingPlatformEntity* bubbleAt(GameWorld& w, float startX) {
    for (auto& e : w.entities())
        if (auto* mp = dynamic_cast<MovingPlatformEntity*>(e.get()); mp && mp->isBubble() && std::abs(e->object().rect.x - startX) < 1.0f) return mp;
    return nullptr;
}

/// A player who goes through the tutorial: right along the route, a full jump when blocked, a
/// jump before the brook, attacks at the sleeping creatures and at the guardian, the Ruée at the
/// crystal wall, standing still on a bubble until its end. Its inputs depend only on the state of
/// the world: they are recorded as the run's script.
class TutorialBot {
public:
    InputFrame next(GameWorld& w, int step) {
        const Player& p = w.player();
        const float x = p.center().x, tile = x / 16.0f;
        int dir = 1;
        bool jump = false, attack = false, dash = false;
        // Stuck against a step or a wall: a full jump, a double jump at its apex.
        if (std::abs(x - lastX_) < 0.05f && p.grounded()) ++stuck_;
        else stuck_ = 0;
        lastX_ = x;
        if (jumpHold_ > 0) {
            --jumpHold_;
            jump = true;
            if (jumpHold_ == 0 && !p.grounded()) doubleDue_ = true;
        } else if (doubleDue_) {
            doubleDue_ = false;  // released for a step: the next press is the double jump
            jumpHold_ = 30;
        }
        auto fullJump = [&] {
            if (p.grounded() && jumpHold_ == 0) jumpHold_ = 34;
        };
        // The sleeping garden: the nearest creature, struck until it is gone.
        const Enemy* critter = nullptr;
        for (const auto& e : w.enemies()) {
            if (!e->alive() || e->def().isBoss || std::abs(e->center().y - p.center().y) > 48.0f) continue;
            const float d = std::abs(e->center().x - x);
            if (d < 200.0f && (!critter || d < std::abs(critter->center().x - x))) critter = e.get();
        }
        if (critter) {
            const float dx = critter->center().x - x;
            if (std::abs(dx) > 14.0f) {
                dir = dx > 0.0f ? 1 : -1;
            } else {
                dir = 0;
                face_ = dx < 0.0f ? -1 : 1;
                attack = step % 30 == 0;
            }
        }
        // The guardian: close in, strike, jump when it gets ready.
        if (Enemy* b = w.activeBoss(); b && b->alive()) {
            const float dx = b->center().x - x;
            const float reach = b->box.w * 0.5f + 12.0f;
            dir = dx > reach ? 1 : dx < -reach ? -1 : 0;
            if (dir == 0) {
                face_ = dx < 0.0f ? -1 : 1;
                attack = step % 24 == 0;
            }
            if (b->state() == EnemyState::Telegraph && p.grounded() && step % 90 == 0) fullJump();
        }
        // The crystal wall: the Ruée.
        if (tile > 182.0f && tile < 186.5f && w.player().hasPower("dash")) {
            if (stuck_ > 3) dash = step % 20 == 0;
        } else if (stuck_ > 4 && dir != 0) {
            fullJump();
        }
        // The brook after the lantern: jump from its edge.
        if (tile > 245.0f && tile < 245.9f && p.grounded()) fullJump();
        // The stream: wait for the bubble at the bank, stand on it until it ends.
        if (MovingPlatformEntity* bub = bubbleUnder(w)) {
            dir = x < bub->box.center().x + 6.0f ? 1 : 0;
        } else if (tile > 194.6f && tile < 196.0f && p.grounded()) {
            MovingPlatformEntity* b1 = bubbleAt(w, 196.0f * 16.0f);
            if (!b1 || b1->popped() || std::abs(b1->box.x - 196.0f * 16.0f) > 0.5f) dir = 0;
        } else if (tile > 216.0f && tile < 217.2f && p.grounded()) {
            MovingPlatformEntity* b2 = bubbleAt(w, 217.0f * 16.0f);
            if (!b2 || b2->popped() || std::abs(b2->box.x - 217.0f * 16.0f) > 0.5f) dir = 0;
        }
        InputFrame f;
        auto hold = [&](Action a) { f.held |= 1u << static_cast<int>(a); };
        if (dir > 0) hold(Action::MoveRight);
        if (dir < 0) hold(Action::MoveLeft);
        if (dir == 0 && face_ != 0 && (attack || std::abs(p.facing - static_cast<float>(face_)) > 0.5f)) {
            // Turned towards the guardian for a step (a tap of the direction).
            hold(face_ > 0 ? Action::MoveRight : Action::MoveLeft);
            dir = face_;
        }
        f.moveX = static_cast<int8_t>(dir * 127);
        face_ = 0;
        if (jump) hold(Action::Jump);
        if (attack) hold(Action::Attack);
        if (dash) hold(Action::PowerSlot1);
        f.pressed = f.held & ~prev_.held;
        f.released = prev_.held & ~f.held;
        prev_ = f;
        return f;
    }

private:
    InputFrame prev_;
    float lastX_ = -1.0f;
    int stuck_ = 0;
    int jumpHold_ = 0;
    bool doubleDue_ = false;
    int face_ = 0;
};

bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.right() - 0.5f && b.x < a.right() - 0.5f && a.y < b.bottom() - 0.5f && b.y < a.bottom() - 0.5f;
}
bool inside(const Rect& in, const Rect& out) {
    return in.x >= out.x - 0.5f && in.y >= out.y - 0.5f && in.right() <= out.right() + 0.5f && in.bottom() <= out.bottom() + 0.5f;
}

void pressKey(Application& app, SDL_Scancode sc, SDL_Keycode key) {
    SDL_Event down{};
    down.type = SDL_EVENT_KEY_DOWN;
    down.key.scancode = sc;
    down.key.key = key;
    down.key.down = true;
    SDL_PushEvent(&down);
    app.runFrames(2);
    SDL_Event up = down;
    up.type = SDL_EVENT_KEY_UP;
    up.key.down = false;
    SDL_PushEvent(&up);
}

}  // namespace

TEST_CASE("tutorial: its level is outside the campaign, its cards in the order of the catalogue") {
    LevelData level = tutorialLevel();
    CHECK(level.tutorial);
    CHECK(level.id == "premier_envol");
    CHECK(content().level(level.id) == nullptr);              // never in the game's level lists
    CHECK(content().campaignPlanetOf(level.id).empty());      // no planet of the map
    CHECK(level.timerMode == TimerMode::Unlimited);
    CHECK(level.lives == 0);
    // Its nine cards, each asked once by the level's events, in the order the route shows them.
    const auto order = tutorial::levelCards(level);
    CHECK(order.size() == 9);
    CHECK(order.size() == tutorial::cards().size());
    for (size_t i = 0; i < order.size() && i < tutorial::cards().size(); ++i) CHECK(order[i] == tutorial::cards()[i].id);
    for (const auto& c : tutorial::cards()) {
        CHECK(!c.title.empty());
        CHECK(!c.text.empty());
    }
    // The real objects of the game it uses: bouncing mushrooms, carrying bubbles, a lantern, the
    // tablet of the Ruée, three sleeping creatures, its boss.
    std::map<std::string, int> types;
    int sleepers = 0, bosses = 0;
    for (const LevelObject& o : level.objects) {
        ++types[o.type];
        if (o.type == "enemy" && o.propStr("enemy") == "gloop_dormeur") ++sleepers;
        if (o.type == "enemy" && o.prop<bool>("boss", false)) {
            ++bosses;
            CHECK(o.propStr("enemy") == tutorial::kBoss);
        }
    }
    CHECK(types["bounce_pad"] >= 2);
    CHECK(types["bubble"] == 2);
    CHECK(types["checkpoint"] == 1);
    CHECK(types["power_unlock"] == 1);
    CHECK(types["shard"] >= 10);
    CHECK(sleepers == 3);
    CHECK(bosses == 1);
    // A creature of the tutorial never riposts.
    const EnemyDefinition* sleeper = content().enemy("gloop_dormeur");
    CHECK(sleeper && sleeper->tutorial && sleeper->attacks.empty() && sleeper->contactDamage <= 0.0f);
}

TEST_CASE("tutorial: a script of inputs finishes the level, and plays the same run again") {
    TutorialSim sim;
    TutorialBot bot;
    std::vector<InputFrame> script;
    const int maxSteps = 120 * 900;
    int steps = 0;
    for (; steps < maxSteps && sim.w->state() != WorldState::Completed; ++steps) {
        InputFrame f = bot.next(*sim.w, steps);
        script.push_back(f);
        sim.step(f);
    }
    if (sim.w->state() != WorldState::Completed) {
        std::printf("  bloqué à x=%.0f (tuile %.1f), y=%.0f, chutes %d\n", sim.w->player().center().x, sim.w->player().center().x / 16.0f,
                    sim.w->player().center().y, sim.w->stats().deaths);
        for (const auto& e : sim.w->enemies())
            std::printf("    %s pv %.1f x=%.0f y=%.0f\n", e->def().id.c_str(), e->hp(), e->center().x, e->center().y);
    }
    CHECK(sim.w->state() == WorldState::Completed);
    std::printf("  didacticiel fini en %.1f s de jeu (%d chutes), %zu explications\n", steps / 120.0, sim.w->stats().deaths, sim.cards.size());
    // Every card was asked on the way, each once, in the route's order.
    CHECK(sim.cards == tutorial::levelCards(sim.w->level()));
    CHECK(sim.w->player().hasPower("dash"));
    const uint64_t end = simulationStateHash(*sim.w);
    // The script replays the same run (the tutorial is a deterministic level like the others).
    TutorialSim again;
    for (const InputFrame& f : script) again.step(f);
    CHECK(again.w->state() == WorldState::Completed);
    CHECK(simulationStateHash(*again.w) == end);
}

TEST_CASE("tutorial: no deadly fall, a bubble carries Nova and is born again") {
    TutorialSim sim;
    for (int i = 0; i < 120 * 8; ++i) sim.step(InputFrame{});  // the intro, Nova on her feet
    GameWorld& w = *sim.w;
    CHECK(w.state() == WorldState::Playing);
    // On the bank before the stream, then over it: caught at once, back on the bank.
    const Vec2 bank{194.0f * 16.0f, 30.0f * 16.0f};
    w.player().teleportTo(bank);
    for (int i = 0; i < 60; ++i) sim.step(InputFrame{});
    CHECK(w.player().grounded());
    w.player().teleportTo({205.0f * 16.0f, 20.0f * 16.0f});
    bool fell = false;
    for (int i = 0; i < 240; ++i) {
        sim.step(InputFrame{});
        fell = fell || w.player().center().y > 31.0f * 16.0f;
    }
    CHECK(fell);
    CHECK(w.stats().deaths == 0);
    CHECK(w.player().alive());
    CHECK(w.player().grounded());
    CHECK(std::abs(w.player().feet().x - bank.x) < 24.0f);
    // The first bubble: Nova on it, carried to the island; it pops, then is born again at its place.
    MovingPlatformEntity* b1 = bubbleAt(w, 196.0f * 16.0f);
    CHECK(b1 != nullptr);
    if (!b1) return;
    w.player().teleportTo({196.0f * 16.0f + 20.0f, 30.0f * 16.0f});
    for (int i = 0; i < 20; ++i) sim.step(InputFrame{});
    CHECK(bubbleUnder(w) == b1);
    bool popped = false;
    for (int i = 0; i < 120 * 7 && !popped; ++i) {
        sim.step(InputFrame{});
        popped = b1->popped();
    }
    CHECK(popped);
    for (int i = 0; i < 30; ++i) sim.step(InputFrame{});
    CHECK(w.player().grounded());
    CHECK(w.player().feet().x > 212.0f * 16.0f);  // on the island
    CHECK(w.stats().deaths == 0);
    for (int i = 0; i < 120 * 2; ++i) sim.step(InputFrame{});
    CHECK(!b1->popped());
    CHECK(std::abs(b1->box.x - 196.0f * 16.0f) < 0.5f);
    // A blow taking all her health: back at the start (no lantern yet) at once, no fail.
    w.player().health = 1;
    w.player().kill(w);
    for (int i = 0; i < 120 * 3; ++i) sim.step(InputFrame{});
    CHECK(w.player().alive());
    CHECK(w.state() == WorldState::Playing);
}

TEST_CASE("tutorial: its boss is the easiest of the game, slowed down for the tutorial") {
    const EnemyDefinition* tb = content().enemy(tutorial::kBoss);
    CHECK(tb != nullptr);
    if (!tb) return;
    CHECK(tb->isBoss);
    CHECK(tb->tutorial);
    // The easiest boss by the rating of the bosses (game/enemies/BossDifficulty.h), among the
    // game's own (echoes and the tutorial's aside).
    const EnemyDefinition* easiest = nullptr;
    double best = 1e300;
    for (const EnemyDefinition* d : content().enemies()) {
        if (!d->isBoss || !d->builtin || d->tutorial || !d->variantOf.empty()) continue;
        const double s = bossDifficulty(*d).score;
        if (s < best) {
            best = s;
            easiest = d;
        }
    }
    CHECK(easiest != nullptr);
    if (!easiest) return;
    CHECK(tb->variantOf == easiest->id);
    CHECK(tb->animation == easiest->animation);
    CHECK(bossDifficulty(*tb).score < best * 0.25);
    CHECK(bossDifficulty(*tb).stars == 1);
    // Slower: less health, its attacks rarer and announced longer, never harder.
    CHECK(tb->hp < easiest->hp);
    CHECK(tb->enrageTime <= 0.0f);
    CHECK(!tb->attacks.empty());
    for (const AttackDef& a : tb->attacks) {
        const AttackDef* o = easiest->attack(a.id);
        CHECK(o != nullptr);
        if (!o) continue;
        CHECK(a.telegraph > o->telegraph);
        CHECK(a.cooldown >= o->cooldown);
        CHECK(a.damage <= o->damage);
    }
    for (const BossPhase& ph : tb->phases) {
        CHECK(ph.telegraphMultiplier >= 1.2f);
        CHECK(ph.cooldownMultiplier > 1.0f);
        CHECK(ph.speedMultiplier < 1.0f);
        CHECK(ph.enrageTime <= 0.0f);
    }
}

TEST_CASE("tutorial: offered once to whoever has not seen it, never again, kept by copies and migrations") {
    Progression fresh;
    CHECK(tutorial::due(fresh));
    // 2.62.2: a progression written before the tutorial (no key), or marked "before" by 2.61.x:
    // not seen, so it is offered once too.
    Progression old;
    old.fromJson(Json{{"completedLevels", Json::array({"sylvanox_01"})}, {"lastPlanet", "sylvanox"}});
    CHECK(old.tutorial.empty());
    CHECK(tutorial::due(old));
    Progression marked;
    marked.fromJson(Json{{"tutorial", "before"}});
    CHECK(marked.tutorial.empty());
    CHECK(tutorial::due(marked));
    // Every state once seen keeps it away, and is saved.
    for (const char* st : {"started", "skipped", "finished"}) {
        Progression p;
        p.tutorial = st;
        CHECK(!tutorial::due(p));
        Progression back;
        back.fromJson(p.toJson());
        CHECK(back.tutorial == st);
        CHECK(!tutorial::due(back));
    }
    {
        Progression p;  // a new player who closed the game at the welcome: still due
        Progression back;
        back.fromJson(p.toJson());
        CHECK(tutorial::due(back));
    }
    // Levels completed do not matter, only seen / not seen.
    Progression played;
    played.markCompleted("sylvanox_01");
    CHECK(tutorial::due(played));
    // Copies and migrations merge the furthest state: never offered again once gone through.
    auto merged = [](const Json& a, const Json& b) { return jstr(mergeProgressJson(a, b), "tutorial"); };
    Json neu = Json{{"tutorial", ""}}, before = Json{{"completedLevels", Json::array()}};
    CHECK(merged(neu, before).empty());  // a copy of an older game: not seen either
    CHECK(merged(Json{{"tutorial", "before"}}, neu) != "skipped");
    CHECK(merged(neu, Json{{"tutorial", "finished"}}) == "finished");
    CHECK(merged(Json{{"tutorial", "skipped"}}, neu) == "skipped");
    CHECK(merged(Json{{"tutorial", "started"}}, Json{{"tutorial", "finished"}}) == "finished");
    CHECK(merged(Json{{"tutorial", "finished"}}, Json{{"tutorial", "skipped"}}) == "finished");
    const Json files = mergeProgressCopyFiles(Json{{"progress", neu}}, Json{{"progress", Json{{"tutorial", "skipped"}}}});
    Progression restored;
    restored.fromJson(files.value("progress", Json::object()));
    CHECK(restored.tutorial == "skipped");
    CHECK(!tutorial::due(restored));
}

TEST_CASE("tutorial: the souvenir title is the tutorial's, local, shown once earned") {
    CosmeticCatalog cat;
    CHECK(cat.loadDefault());
    const CosmeticItem* t = cat.find(tutorial::kSouvenirTitle);
    CHECK(t != nullptr);
    if (!t) return;
    CHECK(t->category == "title");
    CHECK(t->souvenir == "tutorial");
    CHECK(t->look == "Premier envol");
    CosmeticProgress p;
    CHECK(!cosmeticUnlocked(*t, p));
    CHECK(!cosmeticShown(*t, p));  // never shown to whom cannot earn it any more
    CHECK(cosmeticThreshold(*t, 100) == -1);
    for (const auto& m : cat.milestones(100))
        for (const CosmeticItem* it : m.items) CHECK(it->id != t->id);
    p.tutorialFinished = true;
    CHECK(cosmeticUnlocked(*t, p));
    CHECK(cosmeticShown(*t, p));
}

TEST_CASE("tutorial: the levels of the campaign are untouched (no tutorial rule, no bubble)") {
    CHECK(kSimulationVersion == 17);  // 2.71: the attack upwards and downwards (no tutorial rule); 2.76: parts, flight; 17: every boss
    int levels = 0;
    for (const PlanetData* planet : content().planets())
        for (const std::string& id : planet->levels) {
            auto level = content().loadLevel(id);
            CHECK(level.has_value());
            if (!level) continue;
            ++levels;
            CHECK(!level->tutorial);
            CHECK(!level->toJson().contains("tutorial"));
            for (const LevelObject& o : level->objects) CHECK(o.type != "bubble");
            for (const LevelEvent& e : level->events)
                for (const EventAction& a : e.actions) CHECK(a.type != "tutorial");
            for (const LevelObject& o : level->objects)
                if (o.type == "enemy") CHECK(!content().enemy(o.propStr("enemy")) || !content().enemy(o.propStr("enemy"))->tutorial);
        }
    CHECK(levels >= 25);
    // Their ranked fingerprints are those Cosmic Online holds (server/migrations/0113_levels_2_58_0.sql,
    // the last time the levels changed): records, replays and ghosts stay valid.
    static const std::map<std::string, std::string> ranked = {
        {"aethra_01", "233486c417575fab7791d74dce2b0b9efd0a691f5373abb59f5fcd1a0e952158"},
        {"aethra_02", "7490679679aae98e1bb87a97ad5ea361c524cdf0c0d2a6a96c0d586233e7d6fa"},
        {"aethra_03", "973d1e5f219f97fce747d544859698c35231aedda4068b987c62500a1ca7924c"},
        {"aethra_04", "30467f96c0833dffd283bf6698a4004866cee294787c594056760958e3bdba32"},
        {"confins_01", "3e066c2b86050e19990f5f1b933b464710600ae7dfe79a668a36905c482931b4"},
        {"confins_02", "77bd69b0efbc6f23c529c5f92abd13342652a0cc2975ebdc9688a58adf2a2ac5"},
        {"confins_03", "949ef666095455e496a6a092e8671b2f393526490973f57f3c020e2cec4b8b4f"},
        {"confins_04", "279b37a2e963a15feefce477702e364370a65521625861b0d8f0e298a1f71892"},
        {"confins_05", "319bcc1fb92d5290cdb22463c172f92efda6e04c5caff64fcb74151716e525e1"},
        {"confins_06", "782c4fd35c1306d0aa0a006a882114e25f8ee6eef08fb850611445eeae80b7fb"},
        {"glacior_01", "7ce1fc5d51b4030941264b36e16a7bc02dd27a0ddab5cc26989898ade5004c8a"},
        {"glacior_02", "67007fd953554208502a0c5a556454633fa132c771747d60d86f389e6d2469d2"},
        {"glacior_03", "2464b36267b5cfe18c5a9fe94e51a4ce6fe070d86d1beb1182804b41da088342"},
        {"glacior_04", "323a9dbbc3d6b7f45e80e34727ac984e05932a7375f790c3c4d1ec8e24cb97d0"},
        {"noyau_01", "ddb2a07685215f202bad0ea01cfd75117a53a31d7bba68089e0081f583992f61"},
        {"noyau_02", "ed48dc0ef0bcde1e10dc431daba926953c774b0753330615d4585345fc604dac"},
        {"noyau_03", "b3a88f4ca0c15df46ea83e1ca06168bc62a6685f8c476c762280c21a2a41d26d"},
        {"pyralis_01", "87819685a33efc1128f02785063b5080f9991b04358448e8a4a88bf4843311b4"},
        {"pyralis_02", "0273ae751c6e127556d856af086fe75f0d2e778b6137409b2034e212df71816d"},
        {"pyralis_03", "eb8a3004b1b364fb67c16b7b852cef027872bf7ee1fcbdacfd332d30611d98be"},
        {"pyralis_04", "9b9e80b62e474175383b9a2fc04320303a3d0c3917a9b8b3884f98ce669cc320"},
        {"sylvanox_01", "49d7d22cc13e1286a88914755e0a3151c9288ae0b6f776b8fab05647398bbb0a"},
        {"sylvanox_02", "d9060b739c93dac2399c3e296e6c92008a90a0071b410bcfa151731f6db4cb8b"},
        {"sylvanox_03", "228fb791e64a318b95b04766c998f47eba4a3848cce3b882841f5c438dbf2b46"},
        {"sylvanox_04", "d5ecbc117ca88f26f1fc1b7063003deee6c1304df24c80ff10fb707e05a9206e"},
    };
    for (const auto& [id, hash] : ranked) {
        const LevelEntry* e = content().level(id);
        CHECK(e != nullptr);
        if (!e) continue;
        auto text = FileUtil::readText(e->path);
        CHECK(text.has_value());
        if (!text) continue;
        auto fp = fingerprintRankedLevel(content(), Json::parse(*text));
        if (!fp || fp->hash != hash) std::printf("  empreinte changée : %s\n", id.c_str());
        CHECK(fp && fp->hash == hash);
    }
}

// The cards over the run in the real game loop (headless): the run waits while a card is open —
// its time, the timer, Nova — and goes on after « Compris, je continue » (Entrée); every card of
// the tutorial does so. Nothing of a card overlaps nor leaves it, at 960×540 and 1600×900, in
// French and in English. The tutorial is started (never offered again), finished (the souvenir,
// the end card), or left from its pause menu (skipped).
TEST_CASE("tutorial: every explanation card stops the run until it is read (headless game loop)") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    Application app;
    AppConfig cfg;
    cfg.headless = true;
    cfg.width = 960;
    cfg.height = 540;
    cfg.vsync = false;
    CHECK(app.init(cfg));
    {
        GameServices g(app);
        g.loadAll();
        g.progression.tutorial.clear();  // a new player
        CHECK(tutorial::due(g.progression));
        std::unique_ptr<Scene> scene = makeTutorialRun(g);
        CHECK(scene != nullptr);
        if (!scene) {
            app.shutdown();
            return;
        }
        GameScene* game = static_cast<GameScene*>(scene.get());
        CHECK(game->tutorial());
        app.pushScene(std::move(scene));
        app.runFrames(5);
        CHECK(g.progression.tutorial == "started");  // never offered again, even if the game closes now
        CHECK(!tutorial::due(g.progression));
        GameWorld* w = game->world();
        CHECK(w != nullptr);
        if (w) {
            CHECK(w->training());  // nothing of it counts
            // The first card comes once Nova stands in the nursery (after the intro).
            for (int i = 0; i < 900 && game->tutorialCard().empty(); ++i) app.runFrames(1);
            CHECK(game->tutorialCard() == "move");
            app.runFrames(3);
            CHECK(app.sceneCount() == 2);
            auto frozen = [&](const char* id) {
                const float time = w->time(), timer = w->timer().elapsed();
                const Vec2 player = w->player().box.pos();
                app.runFrames(90);
                CHECK(app.sceneCount() == 2);
                CHECK(game->tutorialCard() == id);
                CHECK(w->time() == time);
                CHECK(w->timer().elapsed() == timer);
                CHECK(w->player().box.pos().x == player.x && w->player().box.pos().y == player.y);
                // « Compris, je continue » (Entrée): the run goes on.
                pressKey(app, SDL_SCANCODE_RETURN, SDLK_RETURN);
                app.runFrames(10);
                CHECK(app.sceneCount() == 1);
                CHECK(game->tutorialCard().empty());
                app.runFrames(20);
                CHECK(w->time() > time);
            };
            // A press at the very opening never closes it unread.
            pressKey(app, SDL_SCANCODE_RETURN, SDLK_RETURN);
            app.runFrames(2);
            CHECK(app.sceneCount() == 2);
            frozen("move");
            for (const auto& c : tutorial::cards()) {
                if (c.id == "move") continue;
                w->requestTutorialCard(c.id);
                app.runFrames(3);
                CHECK(game->tutorialCard() == c.id);
                frozen(c.id.c_str());
            }
            // Asked again, a card never comes back.
            w->requestTutorialCard("jump");
            app.runFrames(3);
            CHECK(game->tutorialCard().empty());
            // Nothing of a card overlaps nor leaves it, nor the end card.
            for (Vec2 size : {Vec2{960, 540}, Vec2{1600, 900}, Vec2{1280, 720}}) {
                app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
                app.renderer().refreshOutputSize();
                for (Language lang : {Language::French, Language::English}) {
                    i18n::setLanguage(lang);
                    app.runFrames(1);
                    g.ui.begin(app, 1.0f / 60.0f);
                    const Rect screen{0, 0, g.ui.screenW(), g.ui.screenH()};
                    for (const auto& c : tutorial::cards()) {
                        const std::string kicker = "BIP T'EXPLIQUE · 9 / 9";
                        const TutorialCardLayout L = layoutTutorialCard(g.ui, c, kicker, &game->powerKeys());
                        CHECK(inside(L.panel, screen));
                        std::vector<Rect> parts = {L.bip, L.kicker, L.title, L.preview, L.text, L.note, L.button};
                        for (const Rect& k : L.keys) parts.push_back(k.expanded(g.ui.s(8)));
                        for (size_t i = 0; i < parts.size(); ++i) {
                            CHECK(inside(parts[i], L.panel));
                            for (size_t j = i + 1; j < parts.size(); ++j) {
                                if (overlaps(parts[i], parts[j]))
                                    std::printf("  carte %s : éléments %zu et %zu se chevauchent (%gx%g)\n", c.id.c_str(), i, j, size.x, size.y);
                                CHECK(!overlaps(parts[i], parts[j]));
                            }
                        }
                        CHECK(g.ui.text().measure(c.title, FontFace::Title, L.titlePx).x <= L.title.w + 0.5f);
                        CHECK(g.ui.text().measure(kicker, FontFace::Bold, L.kickerPx).x <= L.kicker.w + 0.5f);
                        CHECK(g.ui.text().measureWrapped(c.text, FontFace::Body, L.textPx, L.text.w).y <= L.text.h + 0.5f);
                        CHECK(g.ui.text().measureWrapped(kTutorialPauseNote, FontFace::Body, L.notePx, L.note.w + 1.0f).y <= L.note.h + 0.5f);
                        CHECK(L.buttonText.x >= L.button.x && L.buttonText.right() + g.ui.s(14) + L.buttonGlyphW <= L.button.right() + 0.5f);
                        for (size_t k = 0; k < L.keys.size() && k < c.keys.size(); ++k)
                            CHECK(tutorialKeyRowWidth(g.ui, c.keys[k], &game->powerKeys(), L.keyPx, L.glyphH) <= L.keys[k].w + 0.5f);
                        CHECK(L.textPx >= 12.0f && L.notePx >= 12.0f && L.kickerPx >= 12.0f);
                    }
                    const TutorialEndLayout E = layoutTutorialEnd(g.ui);
                    CHECK(inside(E.panel, screen));
                    std::vector<Rect> parts = {E.bip, E.title, E.subtitle, E.learntTitle, E.souvenirBox, E.reminder, E.wear, E.start};
                    parts.insert(parts.end(), E.learnt.begin(), E.learnt.end());
                    for (size_t i = 0; i < parts.size(); ++i) {
                        CHECK(inside(parts[i], E.panel));
                        for (size_t j = i + 1; j < parts.size(); ++j) CHECK(!overlaps(parts[i], parts[j]));
                    }
                    CHECK(inside(E.souvenirLabel, E.souvenirBox) && inside(E.souvenirPill, E.souvenirBox));
                    CHECK(!overlaps(E.souvenirLabel, E.souvenirPill));
                    CHECK(E.learnt.size() == tutorial::learnt().size());
                    for (bool confirm : {false, true}) {
                        const TutorialWelcomeLayout W = layoutTutorialWelcome(g.ui, confirm);
                        CHECK(inside(W.panel, screen));
                        const std::vector<Rect> wp = {W.bip, W.titleRect, W.textRect, W.start, W.skip};
                        for (size_t i = 0; i < wp.size(); ++i) {
                            CHECK(inside(wp[i], W.panel));
                            for (size_t j = i + 1; j < wp.size(); ++j) CHECK(!overlaps(wp[i], wp[j]));
                        }
                        CHECK(g.ui.text().measure(W.title, FontFace::Title, W.titlePx).x <= W.titleRect.w + 0.5f);
                        CHECK(g.ui.text().measureWrapped(W.text, FontFace::Body, W.textPx, W.textRect.w).y <= W.textRect.h + 0.5f);
                        CHECK(g.ui.text().measure(W.startText, FontFace::Bold, W.buttonPx).x <= W.start.w - g.ui.s(28));
                        CHECK(g.ui.text().measure(W.skipText, FontFace::Bold, W.buttonPx).x <= W.skip.w - g.ui.s(28));
                    }
                }
            }
            i18n::setLanguage(Language::French);
            app.setWindowSize(960, 540);
            app.renderer().refreshOutputSize();
            // The end: the exit reached, its closing scene, then the end card and the souvenir.
            for (auto& e : w->entities())
                if (e->type() == "exit") {
                    w->reachExit(*e);
                    break;
                }
            for (int i = 0; i < 1200 && g.progression.tutorial != "finished"; ++i) app.runFrames(1);
            CHECK(g.progression.tutorial == "finished");
            app.runFrames(5);
            CHECK(app.sceneCount() == 2);  // the end card over the finished run
            const CosmeticItem* title = g.cosmetics.find(tutorial::kSouvenirTitle);
            CHECK(title && cosmeticUnlocked(*title, g.cosmeticProgress()));
            CHECK(g.collection.announced(tutorial::kSouvenirTitle));
            CHECK(!tutorial::due(g.progression));
            // Its souvenir in the copy of the progression (copies, migration).
            CHECK(jstr(g.progressCopyBody()["files"]["progress"], "tutorial") == "finished");
        }
        // Left from its pause menu: skipped for good.
        g.progression.tutorial.clear();
        std::unique_ptr<Scene> again = makeTutorialRun(g);
        GameScene* game2 = static_cast<GameScene*>(again.get());
        app.switchScene(std::move(again), 0.0f);
        app.runFrames(10);
        CHECK(g.progression.tutorial == "started");
        game2->quitToMenu();
        CHECK(g.progression.tutorial == "skipped");
        CHECK(!tutorial::due(g.progression));
        app.runFrames(30);
        app.shutdown();  // the scenes first (they use the services)
    }
}
