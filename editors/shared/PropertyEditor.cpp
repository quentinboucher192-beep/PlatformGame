#include "editors/shared/PropertyEditor.h"

#include <algorithm>

#include "engine/core/FileUtil.h"
#include "game/GameServices.h"
#include "game/levels/DecorAtlas.h"

namespace cosmic {

Rect FieldRow::field(const std::string& label, float h) {
    float hh = h > 0 ? h : ui.s(44);
    ui.label(label, {x, y + hh * 0.5f}, 18, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    Rect r{x + labelW, y, width - labelW, hh};
    y += hh + ui.s(8);
    return r;
}

std::vector<std::string> PropertyEditor::enemyIds(GameServices& g) {
    std::vector<std::string> v;
    for (const auto* e : g.content.enemies()) v.push_back(e->id);
    return v;
}

std::vector<std::string> PropertyEditor::powerIds(GameServices& g) {
    std::vector<std::string> v;
    for (const auto* p : g.content.powers()) v.push_back(p->id);
    return v;
}

std::vector<std::string> PropertyEditor::decorNames(GameServices& g) { return DecorAtlas::shared(g.app.resources()).names(); }

std::vector<std::string> PropertyEditor::effectNames(GameServices& g) {
    std::vector<std::string> v;
    for (auto& [k, _] : g.content.effects().items()) v.push_back(k);
    std::sort(v.begin(), v.end());
    return v;
}

std::vector<std::string> PropertyEditor::assetFiles(const std::string& folder, const std::string& ext) {
    std::vector<std::string> v;
    std::error_code ec;
    fs::path dir = Paths::assetsDir() / folder;
    if (!fs::is_directory(dir, ec)) return v;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && FileUtil::extensionLower(e.path()) == ext)
            v.push_back(folder + "/" + Paths::toUtf8(e.path().filename()));
    std::sort(v.begin(), v.end());
    return v;
}

namespace {
bool cycleString(UIContext& ui, const std::string& id, const Rect& r, const std::string& label, std::string& value,
                 const std::vector<std::string>& options, const std::vector<std::string>* labels = nullptr) {
    if (options.empty()) return ui.textField(id, r, value);
    int idx = 0;
    for (size_t i = 0; i < options.size(); ++i)
        if (options[i] == value) idx = static_cast<int>(i);
    const auto& shown = labels && labels->size() == options.size() ? *labels : options;
    if (ui.cycle(id, r, label, idx, shown)) {
        value = options[idx];
        return true;
    }
    return false;
}
}  // namespace

bool PropertyEditor::editOne(GameServices& g, const PropDef& def, Json& props, float x, float& y, float width) {
    UIContext& ui = g.ui;
    FieldRow row{ui, x, width, width * 0.42f, y};
    const std::string id = "prop_" + def.key;
    bool changed = false;
    if (!props.contains(def.key)) props[def.key] = def.def;
    Json& v = props[def.key];
    switch (def.type) {
        case PropType::Bool: {
            bool b = v.is_boolean() ? v.get<bool>() : false;
            Rect r{x, y, width, ui.s(44)};
            y += ui.s(52);
            if (ui.toggle(id, r, def.label, b)) {
                v = b;
                changed = true;
            }
            break;
        }
        case PropType::Int:
        case PropType::Float: {
            float f = v.is_number() ? v.get<float>() : 0.0f;
            float mn = def.max > def.min ? def.min : -1e6f, mx = def.max > def.min ? def.max : 1e6f;
            Rect r = row.field(def.label);
            if (ui.numberField(id, r, f, def.type == PropType::Int ? 1.0f : def.step, mn, mx,
                               def.type == PropType::Int ? "%.0f" : "%.2f")) {
                if (def.type == PropType::Int) v = static_cast<int>(std::lround(f));
                else v = f;
                changed = true;
            }
            break;
        }
        case PropType::Enum: {
            std::string s = v.is_string() ? v.get<std::string>() : "";
            Rect r{x, y, width, ui.s(46)};
            y += ui.s(54);
            if (cycleString(ui, id, r, def.label, s, def.options, &def.optionLabels)) {
                v = s;
                changed = true;
            }
            break;
        }
        case PropType::EnemyRef:
        case PropType::PowerRef:
        case PropType::DecorRef:
        case PropType::EffectRef: {
            std::string s = v.is_string() ? v.get<std::string>() : "";
            std::vector<std::string> opts = def.type == PropType::EnemyRef  ? enemyIds(g)
                                            : def.type == PropType::PowerRef ? powerIds(g)
                                            : def.type == PropType::DecorRef ? decorNames(g)
                                                                              : effectNames(g);
            Rect r{x, y, width, ui.s(46)};
            y += ui.s(54);
            if (cycleString(ui, id, r, def.label, s, opts)) {
                v = s;
                changed = true;
            }
            break;
        }
        case PropType::Color: {
            std::string s = v.is_string() ? v.get<std::string>() : "#ffffff";
            if (colorPicker(ui, id, x, y, width, width * 0.42f, def.label, s)) {
                v = s;
                changed = true;
            }
            break;
        }
        case PropType::ObjectRef: {
            std::string s = v.is_string() ? v.get<std::string>() : "";
            Rect r = row.field(def.label);
            if (ui.textField(id, {r.x, r.y, r.w - ui.s(96), r.h}, s, "ids séparés par des virgules")) {
                v = s;
                changed = true;
            }
            if (ui.button(id + "_pick", {r.right() - ui.s(90), r.y, ui.s(90), r.h}, "Choisir") && onPickObject)
                onPickObject(def.key);
            break;
        }
        case PropType::SoundRef: {
            std::string s = v.is_string() ? v.get<std::string>() : "";
            std::vector<std::string> opts = {""};
            for (const auto& f : assetFiles("music", ".ogg")) opts.push_back(f);
            Rect r{x, y, width, ui.s(46)};
            y += ui.s(54);
            std::vector<std::string> labels;
            for (const auto& o : opts) labels.push_back(o.empty() ? "(aucune)" : o);
            if (cycleString(ui, id, r, def.label, s, opts, &labels)) {
                v = s;
                changed = true;
            }
            break;
        }
        case PropType::String:
        case PropType::Text: {
            std::string s = v.is_string() ? v.get<std::string>() : "";
            Rect r = row.field(def.label);
            if (ui.textField(id, r, s, "", def.type == PropType::Text ? 400 : 120)) {
                v = s;
                changed = true;
            }
            break;
        }
    }
    return changed;
}

bool PropertyEditor::colorPicker(UIContext& ui, const std::string& id, float x, float& y, float width, float labelW,
                                 const std::string& label, std::string& value) {
    bool changed = false;
    FieldRow row{ui, x, width, labelW, y};
    Rect r = row.field(label);
    Rect sw{r.right() - r.h, r.y, r.h, r.h};
    if (ui.textField(id, {r.x, r.y, r.w - r.h - ui.s(6), r.h}, value, "auto (#rrggbb)", 9)) changed = true;
    if (value.empty()) {
        ui.r().drawRoundedRect(sw, ui.s(6), ui.theme().textDim, ui.s(1.5f));
        ui.label("A", sw.center(), 14, ui.theme().textDim, Align::Center, FontFace::Bold, VAlign::Middle);
    } else {
        ui.r().fillRoundedRect(sw, ui.s(6), Color::fromHex(value, Color::white()));
    }
    static const char* presets[] = {"", "#ff4660", "#ff8a3c", "#ffd24a", "#78e68c", "#39f0d8", "#56acff", "#b04cff", "#ff7ad9", "#f2f2f2"};
    float px = r.x, sz = ui.s(26), gap = ui.s(6);
    for (int i = 0; i < 10 && px + sz <= r.right(); ++i) {
        Rect pr{px, y, sz, sz};
        bool on = value == presets[i];
        if (ui.mouseClicked(pr)) {
            value = presets[i];
            changed = true;
        }
        if (presets[i][0]) ui.r().fillRoundedRect(pr, ui.s(5), Color::fromHex(presets[i], Color::white()));
        else ui.label("A", pr.center(), 13, ui.theme().textDim, Align::Center, FontFace::Bold, VAlign::Middle);
        ui.r().drawRoundedRect(pr, ui.s(5), on ? ui.theme().accent : Color{90, 106, 144, 255}, ui.s(on ? 2.0f : 1.0f));
        px += sz + gap;
    }
    y += sz + ui.s(10);
    return changed;
}

bool PropertyEditor::edit(GameServices& g, const std::vector<PropDef>& defs, Json& props, float x, float& y, float width) {
    bool changed = false;
    for (const auto& d : defs) changed |= editOne(g, d, props, x, y, width);
    return changed;
}

}  // namespace cosmic
