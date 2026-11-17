// « Histoire des giga-boss » (2.51): before the first fight against a giga boss (8 phases or
// more, never an echo), its story is told over the run, which waits for it: not a step is
// simulated, not an input recorded (the replays, records and rankings never see it). Told when
// it has not been seen (a story skipped counts), never in a boss training, an editor test or an
// automated run; « seen » is kept with the boss statistics and united by the progress merge.

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <set>

#include "engine/animation/Animation.h"
#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "game/content/BossStories.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/entities/Entities.h"
#include "game/online/ProgressMerge.h"
#include "game/player/Player.h"
#include "game/records/BossStats.h"
#include "saves/Settings.h"
#include "tests/TestFramework.h"
#include "ui/screens/BossStoryScene.h"
#include "ui/screens/GameScene.h"
#include "ui/screens/Navigation.h"

using namespace cosmic;

namespace {

/// Width and height of a PNG file (its IHDR chunk), {0, 0} when it is not one.
std::pair<int, int> pngSize(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    unsigned char h[24] = {};
    if (!f.read(reinterpret_cast<char*>(h), sizeof h)) return {0, 0};
    if (h[0] != 0x89 || h[1] != 'P' || h[2] != 'N' || h[3] != 'G') return {0, 0};
    auto be = [&](int i) { return (h[i] << 24) | (h[i + 1] << 16) | (h[i + 2] << 8) | h[i + 3]; };
    return {be(16), be(20)};
}

}  // namespace

TEST_CASE("boss stories: every giga boss of the game has its story of six lines, long enough to read") {
    ContentDatabase content;
    content.reload();
    BossStories stories;
    std::string err;
    CHECK(stories.loadDefault(&err));
    CHECK(err.empty());
    std::set<std::string> giga;
    for (const EnemyDefinition* d : content.enemies()) {
        if (!d->builtin || !d->isBoss || !d->variantOf.empty() || d->phases.size() < 8) continue;
        CHECK(isGigaBoss(*d));
        giga.insert(d->id);
        const BossStory* s = stories.find(d->id);
        CHECK(s != nullptr);
        if (!s) continue;
        CHECK(s->shots.size() == 6);
        CHECK(!s->title.empty());
        CHECK(story::duration(*s) >= 15.0f);
        for (const StoryShot& shot : s->shots) {
            CHECK(!shot.text.empty() && !shot.scene.empty());
            CHECK(story::shotDuration(shot.text) >= story::kMinShot);
            // Its painting: in the game's assets, 16:9.
            auto [w, h] = pngSize(Paths::assetsDir() / shot.image);
            CHECK(w > 0 && h > 0);
            CHECK(w * 9 == h * 16);
        }
    }
    CHECK(giga.count("singularis") && giga.count("pyrrhax") && giga.count("monolithe") && giga.count("kraken"));  // 2.53: the Kraken
    CHECK(giga.count("phenix"));  // 2.56: the Phénix
    CHECK(giga.count("liche"));   // 2.57: the Liche
    CHECK(giga.count("titan"));   // 2.58: the Titan Mécanique
    // Every story is a giga boss's (the echoes and the bosses of three phases have none).
    for (const auto& [id, s] : stories.all()) {
        const EnemyDefinition* d = content.enemy(id);
        CHECK(d != nullptr && isGigaBoss(*d));
    }
    // The texts as written (the first and last lines of each).
    CHECK(stories.find("singularis")->shots[0].text ==
          "Il y a très longtemps, au centre de Kepler-Voss, brillait une toute jeune étoile. Elle s'appelait Lyse.");
    CHECK(stories.find("pyrrhax")->shots[5].text == "Depuis, il garde la dernière frontière. Plus personne ne se moquera jamais de son feu.");
    CHECK(stories.find("monolithe")->shots[1].text ==
          "Les premiers enfants des Confins le trouvèrent dans une caverne et y gravèrent une rune, pour jouer : « Protège-nous ».");
    CHECK(stories.find("kraken")->shots[1].text ==
          "Dans le noir, les poissons perdus suivaient sa lueur jusqu'aux sources chaudes. On l'appelait « la Lanterne ».");
    CHECK(stories.find("kraken")->shots[5].text ==
          "Le Kraken allume toujours ses lueurs au fond de la Fosse. Il attend qu'on le suive, comme autrefois.");
    CHECK(stories.find("pyrrhax")->title == "Le Dragon d'Obsidienne");
    CHECK(stories.find("kraken")->title == "La Lanterne des profondeurs");  // its name on the card: « LE KRAKEN DES ABYSSES »
    CHECK(stories.find("phenix")->shots[0].text ==
          "Autrefois, chaque matin, un petit oiseau de lumière quittait l'étoile du centre pour porter l'aube aux Confins.");
    CHECK(stories.find("phenix")->shots[5].text ==
          "Depuis, le Phénix se consume chaque matin dans son nid. Il ne sait plus s'arrêter, de peur que la nuit gagne.");
    CHECK(stories.find("phenix")->title == "Le Porteur d'aube");  // its name on the card: « LE PHÉNIX STELLAIRE »
    CHECK(stories.find("liche")->shots[0].text == "Sous les Confins dorment des catacombes. Le vieil Ossian y allumait une bougie pour chaque mort.");
    CHECK(stories.find("liche")->shots[5].text ==
          "La Liche veille toujours sur sa crypte. Elle a oublié son propre nom… mais pas un seul des leurs.");
    CHECK(stories.find("liche")->title == "Le Gardien des noms");  // its name on the card: « LA LICHE DES CATACOMBES »
    CHECK(stories.find("titan")->shots[0].text ==
          "Sous les Confins grondait une fonderie. Mille ouvriers y coulaient l'acier des vaisseaux du système.");
    CHECK(stories.find("titan")->shots[5].text ==
          "Le Titan Mécanique travaille toujours. Pour lui, chaque visiteur est un voleur venu éteindre ses feux.");
    CHECK(stories.find("titan")->title == "Le Dernier Quart");  // its name on the card: « LE TITAN MÉCANIQUE »
    // Time to read: max(4.5 s, 1.2 s + characters / 16); 35 to 50 s with the title card.
    CHECK_NEAR(story::shotDuration("court"), 4.5f, 1e-4f);
    CHECK_NEAR(story::shotDuration(std::string(160, 'a')), 11.2f, 1e-4f);
    CHECK(story::characters("éàü") == 3);
    for (const char* id : {"singularis", "pyrrhax", "monolithe", "kraken", "phenix", "liche", "titan"}) {
        float t = story::duration(*stories.find(id));
        CHECK(t >= 35.0f && t <= 50.0f);
    }
}

// 2.60: version 2 of the stories. Pyrrhax's story is told in six animated scenes: its sprites
// (sheets in the game's animation format), its layers of scenery, its keys within each shot.
// 2.63: the other giga bosses' stories follow, one by one (kAnimated); the remaining ones are still
// read as version 1 (their paintings).
namespace {

const std::set<std::string> kAnimated = {"pyrrhax", "monolithe", "kraken"};

// The checks every story told in animated scenes passes; the sheets its scenes use go in `sheets`.
// `shadowShots`: the shots where a character casts its shadow on the ground (not in deep space).
void checkAnimatedStory(const BossStory& p, std::set<std::string>& sheets, int shadowShots) {
    CHECK(p.version == 2);
    CHECK(p.shots.size() == 6);
    int shadowed = 0;
    for (const StoryShot& shot : p.shots) {
        CHECK(shot.stage.has_value());
        if (!shot.stage) continue;
        const StoryStage& st = *shot.stage;
        // Its still stays (the shot's image when the scene cannot be played).
        auto [w, h] = pngSize(Paths::assetsDir() / shot.image);
        CHECK(w == 960 && h == 540);
        CHECK(st.size.x == 960.0f && st.size.y == 540.0f);
        // Every time it names is within the shot (seconds of its French line).
        const float dur = story::shotDuration(shot.text);
        CHECK(st.lastTime() <= dur + 1e-3f);
        CHECK(st.camera.size() >= 2);
        for (const StoryKey& k : st.camera) CHECK(k.t >= 0.0f && k.t <= dur + 1e-3f);
        for (const StoryShake& s : st.shakes) CHECK(s.from >= 0.0f && s.to <= dur + 1e-3f && s.from <= s.to);
        for (const StoryFlash& f : st.flashes) CHECK(f.t >= 0.0f && f.t <= dur + 1e-3f);
        // The camera moves in every shot, and nothing stays still: no fixed painting behind (the
        // scenery is animated sprites in layers, like the characters, at their depths), every sprite
        // plays a clip of several frames (or moves), the lights breathe, the dust and ash fly.
        const auto [c0, z0] = st.cameraAt(0.0f);
        const auto [c1, z1] = st.cameraAt(dur);
        const bool cameraMoves = c0.x != c1.x || c0.y != c1.y || z0 != z1;
        CHECK(cameraMoves);
        int scenery = 0, sprites = 0, pulses = 0, shadows = 0, kinds = 0;
        std::set<float> depths;
        for (const StoryElement& e : st.elements) {
            CHECK(!e.keys.empty());
            for (size_t k = 1; k < e.keys.size(); ++k) CHECK(e.keys[k - 1].t <= e.keys[k].t);
            for (const StoryKey& k : e.keys) CHECK(k.t >= 0.0f && k.t <= dur + 1e-3f);
            pulses += e.pulse > 0.0f ? 1 : 0;
            CHECK(e.kind != StoryElementKind::Layer);  // no still picture
            if (e.kind == StoryElementKind::Sprite) {
                ++sprites;
                shadows += e.shadow.on ? 1 : 0;
                if (e.depth < 1.0f || e.repeat) {
                    ++scenery;
                    depths.insert(e.depth);
                }
                sheets.insert(e.source);
                auto j = FileUtil::loadJson(Paths::assetsDir() / e.source);
                CHECK(j.has_value());
                if (!j) continue;
                auto set = AnimationSet::fromJson(*j, e.source);
                auto [sw, sh] = pngSize(Paths::assetsDir() / set->texturePath);
                CHECK(sw >= set->frameWidth && sh >= set->frameHeight);
                bool animated = false;
                for (const StoryKey& k : e.keys) {
                    if (k.clip.empty()) continue;
                    const AnimationClip* c = set->clip(k.clip);
                    CHECK(c != nullptr);
                    if (!c) std::printf("  %s: no clip %s in %s\n", e.id.c_str(), k.clip.c_str(), e.source.c_str());
                    if (c) {
                        animated |= c->frameCount() > 1;
                        // every frame it plays is inside its sheet
                        CHECK((c->start + c->frameCount()) * set->frameWidth <= sw && (c->row + 1) * set->frameHeight <= sh);
                    }
                }
                CHECK(animated);
                if (!animated) std::printf("  %s (%s) plays no animated clip\n", e.id.c_str(), e.source.c_str());
            } else if (e.kind == StoryElementKind::Particles) {
                CHECK(e.area.w > 0.0f && e.area.h > 0.0f);
                ++kinds;
            }
        }
        CHECK(scenery >= 6);        // sky, stars, clouds or mist, far spires, near spires, ground…
        CHECK(depths.size() >= 4);  // …at depths of their own (a real parallax)
        CHECK(sprites >= 12);
        CHECK(pulses >= 2);         // lights that breathe
        CHECK(kinds >= 2);          // embers, dust, ash, sparks…
        shadowed += shadows >= 1 ? 1 : 0;  // characters casting their shadow
    }
    CHECK(shadowed >= shadowShots);
    const story::StageAssets assets = story::stageAssets(p);
    CHECK(assets.animations.size() == sheets.size());
    CHECK(assets.textures.empty());  // no fixed layer: every layer of scenery is an animated sheet
    // Its duration and its title card do not change.
    CHECK(story::duration(p) >= 35.0f && story::duration(p) <= 50.0f);
}

bool hasSheets(const std::set<std::string>& sheets, const char* boss, std::initializer_list<const char*> names) {
    bool all = true;
    for (const char* s : names) {
        const bool there = sheets.count(std::string("animations/stories/") + boss + "_" + s + ".json") == 1;
        if (!there) std::printf("  %s: no sheet %s in its scenes\n", boss, s);
        all = all && there;
    }
    return all;
}

}  // namespace

TEST_CASE("boss stories v2: the stories in animated scenes and the ones still painted") {
    BossStories stories;
    std::string err;
    CHECK(stories.loadDefault(&err));
    CHECK(err.empty());
    int others = 0;
    for (const auto& [id, s] : stories.all()) {
        if (kAnimated.count(id)) {
            CHECK(s.version == 2);
            continue;
        }
        // The other stories: version 1, their paintings, no scene.
        ++others;
        CHECK(s.version == 1);
        for (const StoryShot& shot : s.shots) {
            CHECK(!shot.stage.has_value());
            CHECK(!shot.image.empty());
        }
    }
    CHECK(others == 7 - static_cast<int>(kAnimated.size()));
}

TEST_CASE("boss stories v2: Pyrrhax in animated scenes, every file and clip there, keys within their shots") {
    BossStories stories;
    CHECK(stories.loadDefault());
    const BossStory* p = stories.find("pyrrhax");
    CHECK(p != nullptr);
    if (!p) return;
    std::set<std::string> sheets;
    checkAnimatedStory(*p, sheets, 6);
    // The hatchlings, the egg, the nest, fire, smoke, embers, the sky, the frontier, and the boss's
    // own sheet when it has grown.
    CHECK(hasSheets(sheets, "pyrrhax", {"fumerolle", "frere", "soeur", "jeune", "oeuf", "coquille", "nid", "flamme", "fumee", "volute",
                                        "braise", "etoile", "astre", "lune", "porte", "rocher"}));
    CHECK(sheets.count("animations/pyrrhax.json") == 1);
    CHECK(hasSheets(sheets, "pyrrhax", {"ciel_nuit", "ciel_rouge", "etoiles", "nuages", "brume", "pics_loin", "pics", "sol", "avant",
                                        "flammeches", "lave"}));
}

// 2.63: the Monolithe's story (« Le Golem Primordial »): the pebble in the dust, the children of the
// cave and their rune, the stone growing and standing up, the village, the ships, the ruins.
TEST_CASE("boss stories v2: the Monolithe in animated scenes") {
    BossStories stories;
    CHECK(stories.loadDefault());
    const BossStory* p = stories.find("monolithe");
    CHECK(p != nullptr);
    if (!p) return;
    std::set<std::string> sheets;
    checkAnimatedStory(*p, sheets, 5);  // the first shot is in the dust of space
    CHECK(hasSheets(sheets, "monolithe", {"caillou", "grain", "rocher", "enfant1", "enfant2", "enfant3", "ancien", "villageois",
                                          "villageoise", "hutte", "feu", "torche", "vaisseau", "nuage", "astre"}));
    CHECK(hasSheets(sheets, "monolithe", {"cosmos", "etoiles", "poussiere", "grotte_fond", "stalactites", "piliers", "brume", "sol",
                                          "avant", "ciel", "montagnes", "nuages"}));
    CHECK(sheets.count("animations/monolithe.json") == 1);  // the golem, once it stands up
    CHECK(sheets.count("animations/nova.json") == 1);       // the visitor it does not know
}

// 2.63: the Kraken's story (« La Lanterne des profondeurs »): the egg in the black sea, the fish
// following the little light, the star's light falling into the sea, the eight arms lighting up,
// the fish fleeing from its shadow, the Trench.
TEST_CASE("boss stories v2: the Kraken in animated scenes") {
    BossStories stories;
    CHECK(stories.loadDefault());
    const BossStory* p = stories.find("kraken");
    CHECK(p != nullptr);
    if (!p) return;
    std::set<std::string> sheets;
    checkAnimatedStory(*p, sheets, 6);
    CHECK(hasSheets(sheets, "kraken", {"poulpe", "jeune", "oeuf", "poisson", "meduse", "source", "bulles", "etincelle", "etoile", "astre"}));
    CHECK(hasSheets(sheets, "kraken", {"abysses", "vase", "croute", "recifs_loin", "recifs", "algues", "fond", "avant", "ciel", "etoiles",
                                       "mer", "vagues", "fosse"}));
    CHECK(sheets.count("animations/kraken.json") == 1);  // the Kraken, grown
}

TEST_CASE("boss stories v2: the format (keys, clips, camera, shakes, flashes; version 1 unchanged)") {
    // Keys: what a key does not give is kept; pos goes from one key to the next with its ease;
    // flip and clip change at the key; a clip starts at its key, `offset` seconds in.
    std::vector<StoryKey> keys(4);
    keys[0].t = 0.0f;
    keys[0].pos = Vec2{0.0f, 10.0f};
    keys[0].clip = "idle";
    keys[1].t = 2.0f;
    keys[1].pos = Vec2{100.0f, 10.0f};
    keys[1].ease = StoryEase::Linear;
    keys[2].t = 3.0f;
    keys[2].clip = "walk";
    keys[2].offset = 0.5f;
    keys[2].flip = true;
    keys[2].alpha = 0.0f;
    keys[2].ease = StoryEase::Step;
    keys[3].t = 4.0f;
    keys[3].alpha = 1.0f;
    keys[3].ease = StoryEase::Linear;
    StoryElementState s = storyStateAt(keys, 1.0f);
    CHECK_NEAR(s.pos.x, 50.0f, 1e-3f);
    CHECK_NEAR(s.pos.y, 10.0f, 1e-3f);
    CHECK(s.clip == "idle");
    CHECK_NEAR(s.clipTime, 1.0f, 1e-4f);
    CHECK(!s.flip);
    s = storyStateAt(keys, 2.9f);  // a step: still the value before
    CHECK_NEAR(s.alpha, 1.0f, 1e-4f);
    CHECK_NEAR(s.pos.x, 100.0f, 1e-3f);
    s = storyStateAt(keys, 3.2f);
    CHECK(s.clip == "walk" && s.flip);
    CHECK_NEAR(s.clipTime, 0.7f, 1e-4f);
    CHECK_NEAR(s.alpha, 0.2f, 1e-4f);  // 0 from its key (a step), then towards the next one
    s = storyStateAt(keys, 3.5f);
    CHECK_NEAR(s.alpha, 0.5f, 1e-4f);
    s = storyStateAt(keys, 9.0f);  // after the last key: its values
    CHECK_NEAR(s.alpha, 1.0f, 1e-4f);
    CHECK_NEAR(s.pos.x, 100.0f, 1e-3f);
    // Before its first clip starts, a sprite shows that clip's first frame.
    std::vector<StoryKey> late(2);
    late[0].t = 0.0f;
    late[1].t = 1.5f;
    late[1].clip = "hatch";
    s = storyStateAt(late, 0.4f);
    CHECK(s.clip == "hatch");
    CHECK_NEAR(s.clipTime, 0.0f, 1e-5f);
    CHECK_NEAR(storyEase(StoryEase::InOut, 0.5f), 0.5f, 1e-5f);
    CHECK_NEAR(storyEase(StoryEase::In, 0.5f), 0.25f, 1e-5f);
    CHECK_NEAR(storyEase(StoryEase::Out, 0.5f), 0.75f, 1e-5f);

    // A story of version 2, read from its JSON.
    Json j = Json::parse(R"({
      "boss": "test", "version": 2, "title": "Essai",
      "shots": [
        {"text": "Une ligne.", "image": "stories/x_1.png",
         "stage": {"backdrop": "#102030",
                   "camera": [{"t": 0, "pos": [480, 270], "zoom": 1}, {"t": 4, "pos": [500, 300], "zoom": 2, "ease": "linear"}],
                   "shake": [{"from": 1, "to": 2, "amount": 5}],
                   "flash": [{"t": 3, "in": 0.2, "out": 0.5, "color": "#ffffff", "alpha": 0.8}],
                   "elements": [
                     {"id": "ciel", "layer": "stories/pyrrhax/ciel_nuit.png", "depth": 0.05, "drift": [4, 0], "repeat": true,
                      "keys": [{"t": 0, "pos": [-80, -30]}]},
                     {"id": "x", "sprite": "animations/stories/pyrrhax_oeuf.json", "blend": "add", "pulse": [0.5, 2.0],
                      "shadow": {"alpha": 0.3, "size": [80, 12], "light": [100, 400]}, "drift": [3, 0],
                      "keys": [{"t": 2, "clip": "crack"}, {"t": 0, "pos": [480, 450], "clip": "idle"}]},
                     {"id": "l", "glow": "#ff8a3a", "radius": 120, "keys": [{"t": 0, "alpha": 0.5}]},
                     {"id": "p", "particles": "embers", "color": "#ffb066", "area": [0, 0, 960, 540], "keys": [{"t": 0}]}
                   ]}},
        {"text": "Une autre.", "image": "stories/x_2.png"}
      ]})");
    std::string why;
    auto v2 = BossStory::fromJson(j, &why);
    if (!v2) std::printf("  %s\n", why.c_str());
    CHECK(v2.has_value());
    if (v2) {
        CHECK(v2->version == 2);
        CHECK(v2->shots.size() == 2);
        CHECK(v2->shots[0].stage.has_value() && !v2->shots[1].stage.has_value());
        const StoryStage& st = *v2->shots[0].stage;
        CHECK(st.elements.size() == 4);
        CHECK(st.elements[0].kind == StoryElementKind::Layer && st.elements[0].repeat && st.elements[0].drift.x == 4.0f);
        CHECK(st.elements[1].kind == StoryElementKind::Sprite && st.elements[1].additive);
        CHECK(st.elements[1].shadow.on && st.elements[1].shadow.size.x == 80.0f && st.elements[1].shadow.light.has_value());
        CHECK(st.elements[1].drift.x == 3.0f);
        // a breathing light: whole at the start of its period, down to (1 - pulse) in its middle
        CHECK_NEAR(st.elements[1].pulseAt(0.0f), 1.0f, 1e-5f);
        CHECK_NEAR(st.elements[1].pulseAt(1.0f), 0.5f, 1e-5f);
        CHECK_NEAR(st.elements[1].pulseAt(2.0f), 1.0f, 1e-5f);
        CHECK(!st.elements[0].shadow.on && st.elements[0].pulseAt(1.0f) == 1.0f);
        CHECK(st.elements[1].keys.front().t == 0.0f);  // its keys sorted by time
        CHECK(st.elements[2].kind == StoryElementKind::Glow && st.elements[2].radius == 120.0f);
        CHECK(st.elements[3].kind == StoryElementKind::Particles && st.elements[3].source == "embers");
        CHECK(st.backdrop.r == 0x10 && st.backdrop.g == 0x20 && st.backdrop.b == 0x30);
        auto [cam, zoom] = st.cameraAt(2.0f);
        CHECK_NEAR(cam.x, 490.0f, 1e-3f);
        CHECK_NEAR(zoom, 1.5f, 1e-4f);
        // The view never leaves the stage: a view as large as the stage is centred.
        Vec2 c = st.clampCamera({900.0f, 10.0f}, {480.0f, 270.0f});
        CHECK_NEAR(c.x, 720.0f, 1e-3f);
        CHECK_NEAR(c.y, 135.0f, 1e-3f);
        c = st.clampCamera({900.0f, 10.0f}, {960.0f, 540.0f});
        CHECK_NEAR(c.x, 480.0f, 1e-3f);
        CHECK_NEAR(c.y, 270.0f, 1e-3f);
        CHECK(st.shakeAt(0.5f).x == 0.0f && st.shakeAt(2.5f).y == 0.0f);
        const Vec2 sh = st.shakeAt(1.5f);
        CHECK(sh.x != 0.0f || sh.y != 0.0f);
        CHECK(std::fabs(sh.x) <= 5.0f + 1e-3f && std::fabs(sh.y) <= 5.0f + 1e-3f);
        Color fc = Color::black();
        CHECK_NEAR(st.flashAt(3.0f, &fc), 0.8f, 1e-4f);
        CHECK(st.flashAt(2.5f) == 0.0f && st.flashAt(3.6f) == 0.0f);
        CHECK_NEAR(st.lastTime(), 4.0f, 1e-4f);
    }
    // Damaged scenes are refused (the story is skipped, never half read).
    Json bad = j;
    bad["shots"][0]["stage"]["elements"][0].erase("layer");
    CHECK(!BossStory::fromJson(bad).has_value());
    bad = j;
    bad["shots"][0]["stage"]["elements"][1]["keys"] = Json::array();
    CHECK(!BossStory::fromJson(bad).has_value());
    bad = j;
    bad["version"] = 3;  // a newer format: not read
    CHECK(!BossStory::fromJson(bad).has_value());
    // Version 1: a « stage » is not read (a story of version 1 stays painted).
    Json v1 = j;
    v1["version"] = 1;
    auto old = BossStory::fromJson(v1);
    CHECK(old.has_value() && old->version == 1 && !old->shots[0].stage.has_value());
    Json none = j;
    none.erase("version");
    auto implicit = BossStory::fromJson(none);
    CHECK(implicit.has_value() && implicit->version == 1);

    // The frames a sprite plays: a looping clip wraps, the hatching egg goes on with its
    // « hatched » clip, a clip it does not have is its idle one.
    auto ej = FileUtil::loadJson(Paths::assetsDir() / "animations/stories/pyrrhax_oeuf.json");
    CHECK(ej.has_value());
    if (ej) {
        auto set = AnimationSet::fromJson(*ej, "animations/stories/pyrrhax_oeuf.json");
        const AnimationClip* idle = set->clip("idle");
        const AnimationClip* hatch = set->clip("hatch");
        const AnimationClip* hatched = set->clip("hatched");
        CHECK(idle && hatch && hatched);
        if (idle && hatch && hatched) {
            CHECK(hatch->next == "hatched" && !hatch->loop && hatched->loop);
            const float total = hatch->totalDuration();
            CHECK(storySpriteFrame(*set, "hatch", 0.0f).y == set->frameRect(*hatch, 0).y);
            const Rect after = storySpriteFrame(*set, "hatch", total + 0.01f);
            CHECK(after.y == set->frameRect(*hatched, 0).y && after.x == set->frameRect(*hatched, 0).x);
            const Rect wrapped = storySpriteFrame(*set, "idle", idle->totalDuration() * 3.0f + 0.001f);
            CHECK(wrapped.x == set->frameRect(*idle, 0).x);
            CHECK(storySpriteFrame(*set, "no_such_clip", 0.0f).y == set->frameRect(*idle, 0).y);
        }
    }
}

TEST_CASE("boss stories: told when not seen, for giga bosses only, never in a boss training") {
    ContentDatabase content;
    content.reload();
    BossStories stories;
    CHECK(stories.loadDefault());
    const EnemyDefinition* pyrrhax = content.enemy("pyrrhax");
    const EnemyDefinition* thornwarden = content.enemy("thornwarden");
    const EnemyDefinition* echo = content.enemy("echo_thornwarden");
    CHECK(pyrrhax && thornwarden && echo);
    BossStats stats;
    StoryContext run;
    const int before = static_cast<int>(StorySetting::BeforeFirstFight), never = static_cast<int>(StorySetting::Never);
    CHECK(storyDue(*pyrrhax, stories, stats, before, run));
    // Not a giga boss: a boss of three phases, an echo (even of a giga boss).
    CHECK(!storyDue(*thornwarden, stories, stats, before, run));
    EnemyDefinition echoOfGiga = *pyrrhax;
    echoOfGiga.variantOf = "pyrrhax";
    CHECK(!isGigaBoss(echoOfGiga));
    CHECK(!storyDue(echoOfGiga, stories, stats, before, run));
    CHECK(!isGigaBoss(*echo));
    EnemyDefinition seven = *pyrrhax;
    seven.phases.resize(7);
    CHECK(!isGigaBoss(seven));
    // A giga boss without a story (made in the enemy editor): nothing to tell.
    EnemyDefinition homemade = *pyrrhax;
    homemade.id = "my_giga_boss";
    CHECK(isGigaBoss(homemade));
    CHECK(!storyDue(homemade, stories, stats, before, run));
    // The setting « Jamais ».
    CHECK(!storyDue(*pyrrhax, stories, stats, never, run));
    // Never in a boss training, an editor test, an automated run.
    StoryContext training;
    training.bossTraining = true;
    CHECK(!storyDue(*pyrrhax, stories, stats, before, training));
    StoryContext editor;
    editor.editorTest = true;
    CHECK(!storyDue(*pyrrhax, stories, stats, before, editor));
    StoryContext scripted;
    scripted.scripted = true;
    CHECK(!storyDue(*pyrrhax, stories, stats, before, scripted));
    // Once seen (or skipped), never again; the other giga bosses still have theirs.
    stats.markStorySeen("pyrrhax");
    CHECK(stats.dirty());
    CHECK(!storyDue(*pyrrhax, stories, stats, before, run));
    CHECK(storyDue(*content.enemy("monolithe"), stories, stats, before, run));
}

TEST_CASE("boss stories: « seen » kept with the boss statistics, united by the progress merge, setting saved") {
    BossStats a;
    a.at("pyrrhax").fights = 2;
    a.markStorySeen("pyrrhax");
    BossStats copy;
    copy.fromJson(a.toJson());
    CHECK(copy.storySeen("pyrrhax"));
    CHECK(!copy.storySeen("singularis"));
    CHECK(copy.find("pyrrhax") && copy.find("pyrrhax")->fights == 2);
    // A save written before 2.51 (no list): nothing seen, nothing lost.
    BossStats old;
    old.fromJson(Json{{"bosses", {{"pyrrhax", BossRecord{}.toJson()}}}});
    CHECK(old.storiesSeen().empty() && old.find("pyrrhax"));
    // « Migrer vers un nouveau compte »: the stories seen on either side.
    BossStats b;
    b.markStorySeen("monolithe");
    b.markStorySeen("pyrrhax");
    BossStats merged;
    merged.fromJson(mergeBossStatsJson(a.toJson(), b.toJson()));
    CHECK(merged.storySeen("pyrrhax") && merged.storySeen("monolithe") && !merged.storySeen("singularis"));
    CHECK(merged.storiesSeen().size() == 2);
    BossStats mine;
    mine.fromJson(mergeBossStatsJson(a.toJson(), Json::object()));
    CHECK(mine.storySeen("pyrrhax"));
    // Paramètres › Jeu › « Histoire des giga-boss »: before the first fight by default.
    Settings s;
    CHECK(s.gameplay.bossStories == 0);
    s.gameplay.bossStories = 1;
    CHECK(Settings::fromJson(s.toJson()).gameplay.bossStories == 1);
    Json bad = s.toJson();
    bad["gameplay"]["bossStories"] = 7;
    CHECK(Settings::fromJson(bad).gameplay.bossStories == 1);
}

// The run in the real game loop (headless): Pyrrhax's fight starts, its story is told over the
// run, and for as long as it lasts nothing of the run moves — its time, its timer, the player,
// the boss. Skipped (Space), the fight starts where it was; the story counts as seen.
TEST_CASE("boss stories: the run waits while the story is told (headless game loop)") {
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
        CHECK(!g.bossStats.storySeen("pyrrhax"));
        PlayOptions opt;
        opt.spawn = Vec2{64.0f, 640.0f};  // Les Confins 1, its start (no intro)
        std::unique_ptr<Scene> scene = makeGameScene(g, "confins_01", opt);
        GameScene* game = static_cast<GameScene*>(scene.get());
        app.pushScene(std::move(scene));
        app.runFrames(20);
        GameWorld* w = game->world();
        CHECK(w != nullptr);
        if (w) {
            const float t0 = w->time();
            CHECK(t0 > 0.0f);
            CHECK(game->storyBoss().empty());
            // The arena wakes Pyrrhax (its object in the level).
            LevelEntity* spawn = w->entity("o86");
            CHECK(spawn != nullptr);
            if (spawn) spawn->handleAction(*w, "activate", Json::object());
            app.runFrames(3);
            CHECK(w->activeBoss() != nullptr);
            CHECK(game->storyBoss() == "pyrrhax");
            CHECK(app.sceneCount() == 2);  // the story over the run
            CHECK(g.bossStats.storySeen("pyrrhax"));
            const float time = w->time(), timer = w->timer().elapsed();
            const Vec2 player = w->player().box.pos();
            const float bossHp = w->activeBoss() ? w->activeBoss()->hp() : -1.0f;
            const Vec2 boss = w->activeBoss() ? w->activeBoss()->box.pos() : Vec2{};
            // 2.60: its scenes' textures were all loaded when it started (its layers, its sprite
            // sheets): none is read while it plays.
            const size_t textures = app.resources().textureCount();
            for (const std::string& tex : story::stageAssets(*g.stories.find("pyrrhax")).textures)
                CHECK(app.resources().exists(tex));
            app.runFrames(300);  // five seconds of story (its first scene, the second one starting)
            CHECK(app.resources().textureCount() == textures);
            CHECK(app.sceneCount() == 2);
            CHECK(w->time() == time);
            CHECK(w->timer().elapsed() == timer);
            CHECK(w->player().box.pos().x == player.x && w->player().box.pos().y == player.y);
            CHECK(w->activeBoss() && w->activeBoss()->hp() == bossHp);
            CHECK(w->activeBoss() && w->activeBoss()->box.pos().x == boss.x && w->activeBoss()->box.pos().y == boss.y);
            // Skipped: a short fade, then the fight goes on from where it was.
            SDL_Event down{};
            down.type = SDL_EVENT_KEY_DOWN;
            down.key.scancode = SDL_SCANCODE_SPACE;
            down.key.key = SDLK_SPACE;
            down.key.down = true;
            SDL_PushEvent(&down);
            app.runFrames(2);
            SDL_Event up = down;
            up.type = SDL_EVENT_KEY_UP;
            up.key.down = false;
            SDL_PushEvent(&up);
            app.runFrames(30);
            CHECK(app.sceneCount() == 1);
            CHECK(game->storyBoss().empty());
            app.runFrames(60);
            CHECK(w->time() > time + 0.8f);
            CHECK(w->timer().elapsed() > timer);
        }
        // The narration never reaches « Espace · Ⓐ Passer »: every line of every story (the
        // longest ones included), in both languages, at the smallest window (960x540) and at
        // 1600x900, stays in two lines at most, left of the measured hint, inside the band.
        for (Vec2 size : {Vec2{960, 540}, Vec2{1600, 900}}) {
            app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
            app.renderer().refreshOutputSize();
            for (Language lang : {Language::French, Language::English}) {
                i18n::setLanguage(lang);
                app.runFrames(1);
                g.ui.begin(app, 1.0f / 60.0f);
                CHECK(std::fabs(g.ui.screenW() - size.x) < 1.0f && std::fabs(g.ui.screenH() - size.y) < 1.0f);
                size_t longest = 0;
                for (const auto& [id, st] : g.stories.all())
                    for (const StoryShot& shot : st.shots) {
                        const std::string shown = i18n::tr(shot.text);
                        longest = std::max(longest, story::characters(shown));
                        const StoryNarrationLayout lay = layoutStoryNarration(g.ui, shown);
                        CHECK(!lay.lines.empty() && lay.lines.size() <= kStoryNarrationLines);
                        CHECK(lay.area.right() < lay.hint.x);
                        CHECK(lay.hint.right() <= g.ui.screenW());
                        CHECK(lay.px >= 12.0f);
                        CHECK(lay.top >= lay.area.y - 0.5f);
                        CHECK(lay.top + lay.lineH * static_cast<float>(lay.lines.size()) <= lay.area.bottom() + 0.5f);
                        i18n::Verbatim asShown;
                        for (const auto& l : lay.lines) CHECK(g.ui.text().measure(l, FontFace::Body, lay.px).x <= lay.area.w + 0.5f);
                    }
                CHECK(longest > 100);  // the longest lines were among them
            }
        }
        i18n::setLanguage(Language::French);
        app.shutdown();  // the scenes first (they use the services)
    }
}
