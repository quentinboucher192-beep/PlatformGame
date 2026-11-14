#include "ui/online/OnlineWidgets.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "engine/core/FileUtil.h"
#include "game/GameServices.h"
#include "game/online/OnlineSession.h"
#include "ui/screens/Thumbnail.h"

namespace cosmic {

OnlineUi::OnlineUi(GameServices& g) : g_(g) {
    countries_.load(Paths::dataDir() / "online" / "country_codes.json", Paths::dataDir() / "online" / "flags.json");
    countryMap_.load(Paths::assetsDir() / "online" / "earth_countries.png");
    if (auto j = FileUtil::loadJson(Paths::dataDir() / "online" / "avatars.json")) avatarNames_ = (*j).value("avatars", Json::array());
}

std::string OnlineUi::countryName(const std::string& code) const {
    if (const CountryInfo* c = countries_.find(code)) return c->nameFr;
    return code;
}

void OnlineUi::drawFlag(const std::string& code, const Rect& dst, float alpha) const {
    auto tex = g_.app.resources().texture("online/flags.png", false);
    if (!tex) return;
    const CountryInfo* c = countries_.find(code);
    int cell = c ? c->flagCell : 0;
    int cols = countries_.flagColumns();
    Rect src{static_cast<float>((cell % cols) * countries_.flagCellW()), static_cast<float>((cell / cols) * countries_.flagCellH()),
             static_cast<float>(countries_.flagCellW()), static_cast<float>(countries_.flagCellH())};
    g_.app.renderer().drawTexture(*tex, &src, dst, Color::white().withAlphaF(alpha));
}

std::string OnlineUi::avatarName(int i) const {
    if (i >= 0 && i < static_cast<int>(avatarNames_.size())) return jstr(avatarNames_[static_cast<size_t>(i)], "name_fr");
    return {};
}

void OnlineUi::drawAvatar(const std::string& avatar, const std::string& playerId, const Rect& dst) {
    if (avatar == "custom" && !playerId.empty() && g_.settings.online.bandwidth > 0) {
        auto it = customAvatars_.find(playerId);
        if (it != customAvatars_.end() && it->second) {
            g_.app.renderer().drawTexture(*it->second, nullptr, dst);
            return;
        }
        if (!requested_.count(playerId)) {
            requested_.insert(playerId);
            std::weak_ptr<int> alive = life_;
            g_.online.getBinary("/api/v1/players/" + playerId + "/avatar", [this, alive, playerId](const HttpResponse& r) {
                if (!alive.lock() || r.networkFailure() || r.status != 200 || r.body.size() > 64 * 1024) return;
                std::vector<uint8_t> png(r.body.begin(), r.body.end());
                customAvatars_[playerId] = textureFromPng(g_, png);
            });
        }
    }
    int index = 0;
    if (avatar.size() == 9 && avatar.rfind("avatar_", 0) == 0) index = std::clamp(std::atoi(avatar.c_str() + 7) - 1, 0, 15);
    auto tex = g_.app.resources().texture("online/avatars.png", false);
    if (!tex) return;
    Rect src{static_cast<float>((index % 4) * 128), static_cast<float>((index / 4) * 128), 128, 128};
    g_.app.renderer().drawTexture(*tex, &src, dst);
}

Color OnlineUi::statusColor(RecordStatus s) {
    switch (s) {
        case RecordStatus::Local: return {140, 160, 185, 255};
        case RecordStatus::Pending: return {255, 179, 71, 255};
        case RecordStatus::Verified: return {120, 230, 140, 255};
        case RecordStatus::Rejected: return {255, 77, 109, 255};
    }
    return {140, 160, 185, 255};
}

void OnlineUi::drawStatusChip(RecordStatus s, Vec2 pos, float height) const {
    UIContext& ui = g_.ui;
    std::string text = recordStatusLabel(s);
    float w = ui.text().measure(text, FontFace::Bold, height * 0.62f).x + height;
    Rect r{pos.x, pos.y, w, height};
    Color c = statusColor(s);
    ui.r().fillRoundedRect(r, height * 0.5f, c.withAlphaF(0.18f));
    ui.r().drawRoundedRect(r, height * 0.5f, c.withAlphaF(0.85f), std::max(1.0f, ui.s(1.2f)));
    ui.label(text, r.center(), height * 0.62f / ui.scale(), c, Align::Center, FontFace::Bold, VAlign::Middle);
}

void OnlineUi::drawConnection(Vec2 pos) const {
    UIContext& ui = g_.ui;
    OnlineState st = g_.online.state();
    Color c = st == OnlineState::Online ? Color{120, 230, 140, 255}
              : st == OnlineState::Connecting ? Color{255, 179, 71, 255}
              : st == OnlineState::Offline ? Color{255, 77, 109, 255}
                                           : Color{140, 160, 185, 255};
    float pulse = st == OnlineState::Connecting ? 0.5f + 0.5f * std::sin(ui.time() * 6.0f) : 1.0f;
    ui.r().drawGlow(pos, ui.s(12), c.withAlphaF(0.5f * pulse));
    ui.r().fillCircle(pos, ui.s(5), c.withAlphaF(pulse));
    std::string label = onlineStateLabel(st);
    if (st == OnlineState::Online && g_.online.syncing()) label += " · synchronisation…";
    ui.label(label, {pos.x + ui.s(12), pos.y}, 17, ui.theme().text, Align::Left, FontFace::Body, VAlign::Middle);
}

std::string OnlineUi::displayName(const std::string& name, int rank, bool me) const {
    if (me || g_.settings.online.showPseudonyms) return name;
    return rank > 0 ? "Joueur n°" + std::to_string(rank) : "Joueur";
}

bool OnlineUi::countryPicker(const std::string& id, const Rect& area, std::string& filter, std::string& code) {
    UIContext& ui = g_.ui;
    ui.pushId(id);
    ui.textField("filter", {area.x, area.y, area.w, ui.s(36)}, filter, "Rechercher un pays…", 40);
    std::string f;
    for (char c : filter) f.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    std::vector<const CountryInfo*> shown;
    for (const auto& c : countries_.all()) {
        std::string n;
        for (char ch : c.nameFr) n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        std::string cc;
        for (char ch : c.code) cc.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        if (f.empty() || n.find(f) != std::string::npos || cc == f) shown.push_back(&c);
    }
    Rect view{area.x, area.y + ui.s(44), area.w, area.h - ui.s(44)};
    float rowH = ui.s(36);
    ui.beginScroll("list", view, shown.size() * (rowH + ui.s(4)));
    float y = view.y - ui.scrollOffset();
    bool picked = false;
    for (const CountryInfo* c : shown) {
        Rect row{view.x, y, view.w - ui.s(12), rowH};
        if (row.bottom() >= view.y && row.y <= view.bottom()) {
            if (ui.listItem(c->code, row, "", c->code == code)) {
                code = c->code;
                picked = true;
            }
            drawFlag(c->code, {row.x + ui.s(12), row.center().y - ui.s(9), ui.s(24), ui.s(18)});
            ui.label(c->nameFr, {row.x + ui.s(46), row.center().y}, 17, ui.theme().text, Align::Left, FontFace::Body, VAlign::Middle);
        } else {
            ui.listItem(c->code, row, "", false);
        }
        y += rowH + ui.s(4);
    }
    ui.endScroll();
    ui.popId();
    return picked;
}

bool OnlineUi::avatarPicker(const std::string& id, const Rect& area, std::string& avatar) {
    UIContext& ui = g_.ui;
    ui.pushId(id);
    int cols = 8;
    float cell = std::min(area.w / cols, area.h / 2.0f);
    bool picked = false;
    for (int i = 0; i < 16; ++i) {
        char name[16];
        std::snprintf(name, sizeof(name), "avatar_%02d", i + 1);
        Rect r{area.x + (i % cols) * cell, area.y + (i / cols) * cell, cell - ui.s(6), cell - ui.s(6)};
        UIContext::ButtonStyle st;
        st.selected = avatar == name;
        st.small = true;
        if (ui.button(name, r, "", st)) {
            avatar = name;
            picked = true;
        }
        drawAvatar(name, "", r.expanded(-ui.s(4)));
    }
    ui.popId();
    return picked;
}

std::string OnlineUi::ageText(double seconds) {
    if (seconds < 90) return "à l'instant";
    if (seconds < 3600) return "il y a " + std::to_string(static_cast<int>(seconds / 60)) + " min";
    if (seconds < 86400) return "il y a " + std::to_string(static_cast<int>(seconds / 3600)) + " h";
    return "il y a " + std::to_string(static_cast<int>(seconds / 86400)) + " j";
}

std::string OnlineUi::ageOf(const std::string& iso) {
    auto t = parseIsoUtc(iso);
    if (!t) return {};
    double secs = std::chrono::duration<double>(std::chrono::system_clock::now() - *t).count();
    return ageText(std::max(0.0, secs));
}

}  // namespace cosmic
