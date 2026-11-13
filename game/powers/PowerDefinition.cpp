#include "game/powers/PowerDefinition.h"

namespace cosmic {

Json PowerDefinition::toJson() const {
    return {{"format", "cosmic.power"},
            {"id", id},
            {"behavior", behavior},
            {"name", name},
            {"description", description},
            {"icon", icon},
            {"input", actionId(input)},
            {"energyCost", energyCost},
            {"cooldown", cooldown},
            {"duration", duration},
            {"color", color.toHex()},
            {"sound", sound},
            {"soundEnd", soundEnd},
            {"params", params}};
}

PowerDefinition PowerDefinition::fromJson(const Json& j) {
    PowerDefinition d;
    d.id = jstr(j, "id");
    d.behavior = jstr(j, "behavior", d.id);
    d.name = jstr(j, "name", d.id);
    d.description = jstr(j, "description");
    d.icon = std::clamp(jget<int>(j, "icon", 0), 0, 15);
    Action a = actionFromId(jstr(j, "input", "dash"));
    d.input = (a == Action::Count || !actionIsRebindable(a)) ? Action::Dash : a;
    d.energyCost = std::clamp(jget<float>(j, "energyCost", 20.0f), 0.0f, 100.0f);
    d.cooldown = std::clamp(jget<float>(j, "cooldown", 1.0f), 0.0f, 120.0f);
    d.duration = std::clamp(jget<float>(j, "duration", 0.0f), 0.0f, 120.0f);
    d.color = colorFromJson(j.value("color", Json()), d.color);
    d.sound = jstr(j, "sound");
    d.soundEnd = jstr(j, "soundEnd");
    if (j.contains("params") && j["params"].is_object()) d.params = j["params"];
    return d;
}

}  // namespace cosmic
