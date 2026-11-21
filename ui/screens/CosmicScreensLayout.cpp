#include "ui/screens/CosmicScreensLayout.h"

#include <algorithm>

#include "ui/widgets/CosmicStyle.h"

namespace cosmic {

PlanetMapLayout PlanetMapLayout::compute(float W, float H) {
    const cstyle::CosmicLayout L = cstyle::CosmicLayout::of(W, H);
    PlanetMapLayout m;
    m.unit = L.unit;
    m.title = L.rect(40, 16, 700, 56);
    m.subtitle = L.rect(42, 74, 700, 22);
    m.map = L.rect(20, 104, 740, 512);
    // The widest orbit (560) with a planet and its name, and its height squashed (0.42).
    m.mapScale = std::min(m.map.w * 0.5f / 640.0f, m.map.h * 0.5f / 300.0f);
    m.card = L.rect(790, 24, 450, 572);
    m.banner = L.rect(806, 40, 418, 196);
    m.cardText = L.rect(816, 248, 398, 252);
    m.progress = L.rect(816, 510, 398, 70);
    m.explore = L.rect(790, 612, 418, 64);
    m.diamondHalf = 22.0f * L.unit;
    m.back = L.at(64, 646);
    m.backCaption = L.rect(98, 634, 300, 24);
    m.hints = L.rect(40, 680, 720, 28);
    return m;
}

LevelSelectLayout LevelSelectLayout::compute(float W, float H) {
    const cstyle::CosmicLayout L = cstyle::CosmicLayout::of(W, H);
    LevelSelectLayout m;
    m.unit = L.unit;
    m.title = L.rect(36, 14, 490, 56);
    m.subtitle = L.rect(38, 72, 490, 22);
    m.list = L.rect(28, 104, 504, 516);
    m.cardH = 66.0f * L.unit;
    m.cardGap = 8.0f * L.unit;
    m.card = L.rect(556, 24, 692, 584);
    m.play = L.rect(918, 624, 300, 64);
    m.diamondHalf = 22.0f * L.unit;
    m.diamonds = {L.at(584, 656), L.at(644, 656)};
    m.caption = L.rect(676, 644, 230, 24);
    m.back = L.at(56, 646);
    m.backCaption = L.rect(90, 634, 300, 24);
    m.hints = L.rect(28, 680, 500, 28);
    return m;
}

ResultsLayout ResultsLayout::compute(float W, float H) {
    const cstyle::CosmicLayout L = cstyle::CosmicLayout::of(W, H);
    ResultsLayout m;
    m.unit = L.unit;
    m.title = L.rect(360, 14, 560, 60);
    m.levelName = L.rect(340, 76, 600, 24);
    m.subLine = L.rect(260, 102, 760, 22);
    m.diamondHalf = 22.0f * L.unit;
    m.diamonds = {L.at(64, 42), L.at(124, 42)};
    m.caption = L.rect(40, 72, 280, 22);
    m.online = L.rect(974, 14, 266, 48);
    m.rating = L.rect(1040, 72, 200, 40);
    m.stats = L.rect(40, 132, 580, 412);
    m.statsTime = L.rect(64, 148, 532, 84);
    m.statsRows = L.rect(64, 238, 532, 294);
    m.rowH = 42.0f * L.unit;
    m.medalCard = L.rect(660, 132, 580, 412);
    m.record = L.rect(684, 142, 532, 40);
    m.nova = L.rect(690, 184, 130, 130);
    m.medal = L.rect(870, 186, 300, 96);
    m.medalName = L.rect(840, 286, 360, 28);
    m.stars = L.rect(684, 320, 532, 38);
    m.goalsTitle = L.rect(684, 366, 532, 22);
    m.goals = L.rect(684, 392, 532, 140);
    m.goalH = 35.0f * L.unit;
    m.info = L.rect(40, 552, 1200, 48);
    m.status = L.rect(40, 604, 1200, 26);
    m.buttons = L.rect(40, 640, 1200, 64);
    m.buttonGap = 16.0f * L.unit;
    return m;
}

}  // namespace cosmic
