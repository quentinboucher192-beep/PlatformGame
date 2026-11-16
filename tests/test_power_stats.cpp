// Power characteristics (2.17): shown in the guide, the power sheet and when a power is
// picked up, read from the definitions; energy costs follow strength (the charged shot,
// 20, is the reference: a weaker attack costs less).

#include <cmath>
#include <string>

#include "game/content/ContentDatabase.h"
#include "game/powers/PowerStats.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
std::string valueOf(const std::vector<PowerStat>& stats, const std::string& key) {
    for (const auto& s : stats)
        if (s.key == key) return s.value;
    return {};
}
}  // namespace

TEST_CASE("power stats: the charged shot's sheet reads its definition") {
    ContentDatabase content;
    content.reload();
    const PowerDefinition* d = content.power("charge_shot");
    CHECK(d != nullptr);
    if (!d) return;
    auto stats = powerStats(*d);
    CHECK(!stats.empty() && stats.front().key == "energy");
    CHECK(valueOf(stats, "energy") == "20");
    CHECK(valueOf(stats, "damage") == "4");
    CHECK(valueOf(stats, "radius") == "8 px");
    CHECK(valueOf(stats, "range") == "420 px");
    CHECK(valueOf(stats, "chargeTime") == "0,85 s");
    CHECK(stats.back().key == "cooldown" && stats.back().value == "0,3 s");
    CHECK(powerStatsLine(*d, 3) == "20 énergie  ·  Dégâts 4  ·  Rayon 8 px");
    // A passive power costs nothing and says so.
    if (const PowerDefinition* g = content.power("glide")) CHECK(valueOf(powerStats(*g), "energy") == "gratuit");
}

TEST_CASE("power stats: a weaker attack costs less energy than the charged shot") {
    ContentDatabase content;
    content.reload();
    const PowerDefinition* ref = content.power("charge_shot");
    CHECK(ref != nullptr);
    if (!ref) return;
    float refDamage = ref->param("damage", 0.0f);
    for (const auto* p : content.powers()) {
        if (p->id == ref->id || !p->params.contains("damage")) continue;
        float dmg = p->param("damage", 0.0f);
        // A blade that keeps cutting its target (Boomerang chargé): what one throw deals at most,
        // the impact and every blow of its longest spin.
        if (p->params.contains("tickInterval") && p->params.contains("spinTimes") && p->params["spinTimes"].is_array() &&
            !p->params["spinTimes"].empty()) {
            float longest = p->params["spinTimes"].back().get<float>();
            dmg *= 1.0f + std::floor(longest / p->param("tickInterval", 1.0f) + 1e-3f);
        }
        if (dmg < refDamage) {
            if (p->energyCost > ref->energyCost) std::cout << "  " << p->id << " costs more than the charged shot\n";
            CHECK(p->energyCost <= ref->energyCost);
        }
        if (dmg <= refDamage / 2.0f) CHECK(p->energyCost < ref->energyCost);
    }
}
