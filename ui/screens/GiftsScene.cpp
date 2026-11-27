// A parcel (« Colis de X »: the bigger box tied both ways) shows no count anywhere before it is opened; its opening
// plays the same scene for its first item, then each next one rises from the open box after « Suivant » (Entrée /
// A, a click), with its own « Équiper »; after the last, « Le colis est vide ». « Plus tard » mid-parcel: the rest
// is already granted (the server opened it whole), the list shows it opened with everything it held.
//
// COSMIC ONLINE › CADEAUX (docs/COLLECTION.md « Cadeaux »): the gifts an administrator gave me. The list (to open,
// opened, all), each gift with its wrap bobbing (or what it held once opened), « Cadeau de X », its date and
// « Contenu : surprise »; the gift chosen in a large panel (the box among rays, who sent it, when, the message,
// « Ouvrir le cadeau », or « Équiper » and « Revoir l'ouverture » once opened). And the opening itself: the scene
// darkens, rays of the wrap's light turn, the box shakes harder and harder, hops and squashes while light leaks
// from its seam; the lid flies off, a flash, a shockwave and 120 shards; the item rises in a pulsing halo ringed
// with twelve stars, then its name, who sent it, the message and « Équiper maintenant » (Y), « Cadeau suivant »
// (Entrée / A), « Plus tard » (Échap / B). A click or Entrée / A skips the animation.
//
// Cosmic Online decides everything: what a gift holds is only known once the server opened it (and granted it:
// the item among the rewards won, a badge among the raid rewards claimed). Offline, the gifts wait.

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "engine/input/InputManager.h"
#include "game/GameServices.h"
#include "game/online/Gifts.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/GiftArt.h"
#include "ui/screens/GiftsUi.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/OnlineNav.h"
#include "ui/widgets/Glyphs.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kBoom = 1.75f;           // the lid flies off
constexpr float kReveal = kBoom + 1.15f;  // the texts appear
const Color kGold{255, 205, 110, 255};
const Color kMuted{140, 152, 186, 255};

/// « De X · 20/11/2026 ».
std::string fromLine(const gifts::Gift& g) {
    const std::string d = gifts::dateText(g.createdAt);
    return "De " + g.from + (d.empty() ? std::string() : " · " + d);
}

// ------------------------------------------------------------------ equipping what a gift held

enum class EquipState { Can, Equipped, NoSlot, Unknown };

EquipState equipState(GameServices& g, const gifts::Reward& r) {
    if (r.kind == "badge") {
        if (!g.achievementCatalog.find(r.id)) return EquipState::Unknown;
        const std::vector<std::string> shown = g.shownBadges();
        if (std::find(shown.begin(), shown.end(), r.id) != shown.end()) return EquipState::Equipped;
        const int slots = g.achievementCatalog.badgeSlots(g.cosmeticProgress().stars);
        if (static_cast<int>(shown.size()) >= slots) return EquipState::NoSlot;
        return g.achievements.unlocked(r.id) ? EquipState::Can : EquipState::Unknown;
    }
    const CosmeticItem* it = g.cosmetics.find(r.id);
    if (!it || it->category != r.kind) return EquipState::Unknown;
    const CosmeticItem* on = g.collection.equipped(g.cosmetics, r.kind, g.cosmeticProgress());
    return on && on->id == r.id ? EquipState::Equipped : EquipState::Can;
}

/// Wears what a gift held (a badge: shown on the profile, in the first free slot).
void equipReward(GameServices& g, const gifts::Reward& r) {
    if (r.kind == "badge") {
        std::vector<std::string> chosen = g.shownBadges();
        chosen.push_back(r.id);
        g.collection.badges = std::move(chosen);
        g.collection.badgesChosen = true;
        g.collection.save();
        g.syncBadges();
    } else {
        g.collection.equip(r.kind, r.id);
        g.collection.markAnnounced(r.id);
        g.collection.save();
        g.syncCosmetics();
        g.noteOutfit();
    }
    g.uiSound("ui_confirm", 0.6f);
}

std::string equipLabel(EquipState s, bool now) {
    if (s == EquipState::Equipped) return "Équipé";
    if (s == EquipState::NoSlot) return "Badges du profil complets";
    return now ? "Équiper maintenant" : "Équiper";
}

/// The raid rewards this account holds, as Cosmic Online gave them (a badge received: its achievement unlocks).
void rememberRaidRewards(GameServices& g, const Json& list) {
    if (!list.is_array()) return;
    std::set<std::string> ids;
    for (const Json& v : list)
        if (v.is_string() && v.get<std::string>().size() <= 64) ids.insert(v.get<std::string>());
    if (ids == g.collection.raidRewards) return;
    g.collection.raidRewards = std::move(ids);
    g.collection.save();
    g.checkAchievements();
}

/// The hints of a screen's bottom line, left to right while they fit.
void drawHints(UIContext& ui, const Rect& line, const std::vector<std::pair<Action, std::string>>& hints, float h) {
    float x = line.x;
    const float cy = line.center().y;
    for (const auto& [a, text] : hints) {
        const float w = Glyphs::measureAction(ui, a, h) + ui.s(8) + ui.text().measure(text, FontFace::Body, ui.s(16)).x;
        if (x + w > line.right()) break;
        x += Glyphs::drawHint(ui, a, text, {x, cy}, h) + ui.s(26);
    }
}

// ================================================================== COSMIC ONLINE › CADEAUX

class GiftsScene : public Scene {
public:
    GiftsScene(GameServices& g, int64_t select) : g_(g), online_(g), sel_(select) {}

    void onEnter() override {
        const auto& list = g_.online.myGifts();
        filter_ = gifts::unopenedCount(list) > 0 || list.empty() ? 0 : 2;
        if (sel_ != 0)
            for (const auto& x : list)
                if (x.key() == sel_) filter_ = x.opened ? 1 : 0;
        reload();
    }
    void onResume() override {
        // Back from an opening: the list as the server gave it (the gift now opened, what it held shown).
        if (gifts::unopenedCount(g_.online.myGifts()) == 0 && filter_ == 0) filter_ = 1;
    }

    void update(float dt) override { bg_.update(dt); }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        L_ = giftsui::screenLayout(ui);
        bg_.render(g_, 0.5f);
        const bool top = g_.app.topScene() == this;
        ui.inputEnabled = top;
        drawHeader();
        drawOnlineNav(g_, L_.nav, OnlineSection::Gifts);
        drawList();
        drawDetail();
        drawHints(ui, L_.hint,
                  {{Action::MenuConfirm, "ouvrir"}, {Action::MenuTabNext, "filtre"}, {Action::MenuBack, "retour"}}, ui.s(30));
        if (top && ui.backPressed() && !ui.editingText()) g_.app.switchScene(makeMainMenu(g_));
        ui.inputEnabled = true;
        if (top) ui.end();
    }

private:
    void reload() {
        if (!g_.online.loggedIn()) return;
        loading_ = true;
        g_.online.refreshGifts(OnlineService::guard(life_, [this](const ApiResult& r) {
            loading_ = false;
            error_ = r.ok ? std::string() : r.message;
            offline_ = r.fromCache;
        }));
    }

    void panel(const Rect& r) {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(r, ui.s(14), Color{8, 13, 30, 222});
        ui.r().drawRoundedRect(r, ui.s(14), ui.theme().border, ui.s(1.2f));
    }

    void drawHeader() {
        UIContext& ui = g_.ui;
        ui.r().fillGradientV(L_.header, Color{6, 10, 24, 230}, Color{6, 10, 24, 0});
        Vec2 tsz = drawOnlineTitle(g_, {ui.s(32), L_.header.center().y});
        ui.label("Cadeaux", {ui.s(32) + tsz.x + ui.s(18), L_.header.center().y + ui.s(3)}, 19, ui.theme().textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
        online_.drawConnection({L_.detail.right() - ui.s(240), L_.header.center().y});
    }

    const gifts::Gift* selected() const {
        for (const auto& x : g_.online.myGifts())
            if (x.key() == sel_) return &x;
        return nullptr;
    }

    void drawList() {
        UIContext& ui = g_.ui;
        panel(L_.list);
        ui.pushId("gifts/list");
        const auto& all = g_.online.myGifts();
        const int unopened = gifts::unopenedCount(all);
        const std::string count = std::to_string(unopened) + " à ouvrir · " + std::to_string(all.size()) + (all.size() > 1 ? " reçus" : " reçu");
        const giftsui::ListHead H = giftsui::listHead(ui, L_.list, g_.online.loggedIn() ? count : std::string());
        ui.label(H.title.shown, {H.title.box.x, H.title.box.center().y}, H.title.size, kGold, Align::Left, FontFace::Bold, VAlign::Middle);
        if (!H.count.shown.empty())
            ui.label(H.count.shown, {H.count.box.right(), H.count.box.center().y}, H.count.size, kMuted, Align::Right, FontFace::Body, VAlign::Middle);
        if (!g_.online.loggedIn()) {
            const Vec2 used = ui.labelWrapped("Connectez-vous à Cosmic Online pour recevoir et ouvrir les cadeaux des administrateurs.",
                                              {H.rows.x, H.tabs.y}, H.rows.w, 17, ui.theme().textDim);
            UIContext::ButtonStyle st;
            st.primary = true;
            if (ui.button("login", {H.rows.x, H.tabs.y + used.y + ui.s(16), std::min(H.rows.w, ui.s(300)), ui.s(50)}, "Se connecter", st))
                g_.app.pushScene(makeOnlineAccount(g_, false));
            ui.popId();
            return;
        }
        int tab = filter_;
        ui.tabs("filters", H.tabs, {"À ouvrir", "Ouverts", "Tous"}, tab, {-unopened, 0, 0});
        if (tab != filter_) {
            filter_ = tab;
            sel_ = 0;
        }
        const auto shown = gifts::filtered(all, static_cast<gifts::Filter>(filter_));
        if (!shown.empty() && std::none_of(shown.begin(), shown.end(), [&](const gifts::Gift* x) { return x->key() == sel_; })) sel_ = shown.front()->key();
        if (shown.empty()) {
            const std::string empty = loading_          ? "Lecture des cadeaux…"
                                      : !error_.empty() ? error_
                                      : filter_ == 0    ? "Aucun cadeau à ouvrir pour l'instant."
                                                        : "Aucun cadeau ici pour l'instant.";
            ui.labelWrapped(empty, {H.rows.x, H.rows.y + ui.s(20)}, H.rows.w, 17, ui.theme().textDim);
            ui.popId();
            return;
        }
        const float rowH = ui.s(giftsui::kRowH), gap = ui.s(giftsui::kRowGap);
        ui.beginScroll("rows", H.rows, static_cast<float>(shown.size()) * (rowH + gap));
        float y = H.rows.y - ui.scrollOffset();
        for (const gifts::Gift* x : shown) {
            const Rect row{H.rows.x, y, H.rows.w - ui.s(16), rowH};
            y += rowH + gap;
            const std::string name = "g" + std::to_string(x->key());
            bool hover = false;
            const bool hit = ui.hotspot(name, row, &hover);
            const bool focused = ui.focused(name);
            if (focused && !ui.usingPointer()) sel_ = x->key();
            if (hit) {
                if (x->key() == sel_ && !x->opened) open(*x);
                sel_ = x->key();
            }
            if (row.bottom() < H.rows.y || row.y > H.rows.bottom()) continue;
            drawRow(*x, row, x->key() == sel_, hover || focused);
        }
        ui.endScroll();
        ui.popId();
    }

    void drawRow(const gifts::Gift& x, const Rect& row, bool on, bool hot) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        r.fillRoundedRect(row, ui.s(10), on ? Color{22, 41, 79, 255} : hot ? Color{20, 32, 64, 255} : Color{18, 26, 60, 255});
        r.drawRoundedRect(row, ui.s(10), on ? ui.theme().accent : Color{44, 58, 104, 255}, ui.s(on ? 2.0f : 1.0f));
        // A parcel: « Colis de X », never how many it holds; once opened, its contents are in the detail.
        const std::string kind = x.reward ? gifts::kindName(x.reward->kind) : std::string();
        const std::string date = gifts::dateText(x.createdAt);
        std::string title, sub;
        if (x.parcel) {
            title = "Colis de " + x.from;
            sub = (x.opened ? std::string("Colis ouvert") : std::string("Contenu : surprise")) + (date.empty() ? "" : " · " + date);
        } else {
            title = x.opened && x.reward ? giftart::rewardName(g_, *x.reward) : "Cadeau de " + x.from;
            sub = x.opened ? kind + " · de " + x.from + (date.empty() ? "" : " · " + date) : "Contenu : surprise" + (date.empty() ? "" : " · " + date);
        }
        const giftsui::RowLayout R = giftsui::rowLayout(ui, row, title, sub, x.opened ? "Ouvert" : "À ouvrir", false);
        r.pushClip(R.icon);
        giftart::drawGiftIcon(g_, &online_, x, R.icon, ui.time());
        r.popClip();
        ui.label(R.title.shown, {R.title.box.x, R.title.box.center().y}, R.title.size, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        ui.label(R.sub.shown, {R.sub.box.x, R.sub.box.center().y}, R.sub.size, kMuted, Align::Left, FontFace::Body, VAlign::Middle);
        if (x.opened) r.drawRoundedRect(R.status, ui.s(6), ui.theme().success.withAlpha(150), ui.s(1.2f));
        else r.fillRoundedRect(R.status, ui.s(6), kGold);
        ui.label(R.statusText.shown, R.status.center(), R.statusText.size, x.opened ? ui.theme().success : Color{42, 26, 0, 255}, Align::Center,
                 FontFace::Bold, VAlign::Middle);
    }

    void drawDetail() {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        panel(L_.detail);
        const gifts::Gift* x = g_.online.loggedIn() ? selected() : nullptr;
        if (!x) return;
        ui.pushId("gifts/detail");
        giftsui::DetailTexts T;
        const bool canOpen = canReach();
        EquipState es = EquipState::Unknown;
        const std::vector<gifts::Reward> held = x->parcel ? gifts::revealed(*x) : std::vector<gifts::Reward>{};
        if (x->parcel) {
            T.kind = "Colis · emballage " + gifts::wrapName(x->wrap);
            if (x->opened) {
                T.name = "Colis de " + x->from;
                T.actions = {"Revoir l'ouverture"};
                for (const auto& w : held)
                    if (equipState(g_, w) == EquipState::NoSlot) T.note = "Retirez d'abord un badge du profil (Collection › Badges du profil).";
            } else {
                T.name = "Que contient-il ?";
                T.actions = {"Ouvrir le colis"};
                if (!canOpen) T.note = "Hors ligne : le colis vous attend, il s'ouvrira une fois connecté.";
            }
        } else if (x->opened && x->reward) {
            es = equipState(g_, *x->reward);
            T.kind = gifts::kindName(x->reward->kind);
            T.name = giftart::rewardName(g_, *x->reward);
            T.actions = {equipLabel(es, false), "Revoir l'ouverture"};
            if (es == EquipState::NoSlot) T.note = "Retirez d'abord un badge du profil (Collection › Badges du profil).";
        } else {
            T.kind = "Cadeau · emballage " + gifts::wrapName(x->wrap);
            T.name = "Que contient-il ?";
            T.actions = {"Ouvrir le cadeau"};
            if (!canOpen) T.note = "Hors ligne : le cadeau vous attend, il s'ouvrira une fois connecté.";
        }
        T.from = fromLine(*x);
        T.message = x->message.empty() ? std::string() : "« " + x->message + " »";
        const giftsui::DetailLayout D = giftsui::detailLayout(ui, L_.detail, T);
        const Color light = gifts::wrapLight(x->wrap);
        ui.label(D.kind.shown, D.kind.box.center(), D.kind.size, kMuted, Align::Center, FontFace::Bold, VAlign::Middle);
        // The art: the box among turning rays, or what it held.
        const float t = ui.time();
        if (x->parcel && x->opened) {
            drawContents(held, D.art, light);  // what it held, each with its own « Équiper »
        } else {
            r.pushClip(D.art);
            drawArt(*x, D.art, light, t);
            r.popClip();
        }
        {
            i18n::Verbatim v(false);
            ui.label(D.name.shown, D.name.box.center(), D.name.size, x->opened && !x->parcel ? light : ui.theme().text, Align::Center, FontFace::Bold,
                     VAlign::Middle);
        }
        ui.label(D.from.shown, D.from.box.center(), D.from.size, kMuted, Align::Center, FontFace::Body, VAlign::Middle);
        r.fillRoundedRect(D.message, ui.s(8), Color{18, 26, 60, 255});
        r.fillRect({D.message.x, D.message.y + ui.s(4), ui.s(4), D.message.h - ui.s(8)}, kGold);
        if (D.messageLines.empty()) {
            ui.label("Pas de message.", {D.message.x + ui.s(18), D.message.center().y}, 17, kMuted, Align::Left, FontFace::Body, VAlign::Middle);
        } else {
            raidui::drawLines(ui, D.messageLines, {D.message.x + ui.s(18), D.message.y + ui.s(11)}, D.messageSize, D.messageLineH, ui.theme().text);
        }
        if (!D.note.shown.empty()) ui.label(D.note.shown, D.note.box.center(), D.note.size, ui.theme().warm, Align::Center, FontFace::Body, VAlign::Middle);
        for (size_t i = 0; i < D.actions.size(); ++i) {
            UIContext::ButtonStyle st;
            st.fontSize = D.actionSize;
            if (x->parcel && x->opened) {
                if (ui.button("replay", D.actions[i], T.actions[i], st)) g_.app.pushScene(makeGiftOpening(g_, GiftOpening{*x, true, false}));
            } else if (x->opened && x->reward) {
                if (i == 0) {
                    st.primary = es == EquipState::Can;
                    st.selected = es == EquipState::Equipped;
                    st.enabled = es == EquipState::Can;
                    if (ui.button("equip", D.actions[i], T.actions[i], st)) equipReward(g_, *x->reward);
                } else if (ui.button("replay", D.actions[i], T.actions[i], st)) {
                    g_.app.pushScene(makeGiftOpening(g_, GiftOpening{*x, true, false}));
                }
            } else {
                st.primary = true;
                st.enabled = canOpen;
                if (ui.button("open", D.actions[i], T.actions[i], st)) open(*x);
            }
        }
        ui.popId();
    }

    /// The big art of the detail: what an opened gift held, or the box (a gift's or a parcel's) among turning rays.
    void drawArt(const gifts::Gift& x, const Rect& art, Color light, float t) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        if (x.opened && x.reward) {
            const float size = std::min(art.h * 0.8f, art.w * (x.reward->kind == "title" ? 0.34f : 0.6f));
            r.drawGlow(art.center(), size * 0.7f, light.withAlphaF(0.25f));
            giftart::drawReward(g_, &online_, *x.reward, art.center(), size, t, true);
        } else {
            const Vec2 c{art.center().x, art.y + art.h * 0.6f};
            r.drawGlow(c, art.h * 0.55f, light.withAlphaF(0.4f));
            for (int i = 0; i < 10; ++i) {
                const float a = t * 0.2f + static_cast<float>(i) * kPi / 5.0f, L = art.h * 0.75f;
                const Vec2 p1{c.x + std::cos(a - 0.11f) * L, c.y + std::sin(a - 0.11f) * L}, p2{c.x + std::cos(a + 0.11f) * L, c.y + std::sin(a + 0.11f) * L};
                r.fillTriangle(c, p1, p2, light.withAlphaF(0.08f), BlendMode::Additive);
            }
            const float px = std::min(art.h / 58.0f, art.w / 52.0f);
            giftart::BoxPose pose;
            pose.parcel = x.parcel;
            pose.rot = std::sin(t * 3.0f) * 0.035f;
            giftart::drawBox(g_, x.wrap, {art.center().x, art.bottom() - art.h * 0.06f + std::sin(t * 2.0f) * ui.s(6)}, px, pose);
        }
    }

    /// An opened parcel's rewards, in the order they came out, each with its icon, its name, its kind and its own
    /// « Équiper » (a scrolled list in the art's room).
    void drawContents(const std::vector<gifts::Reward>& held, const Rect& area, Color light) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const float rowH = ui.s(giftsui::kContentRowH), gap = ui.s(giftsui::kContentRowGap);
        ui.beginScroll("contents", area, static_cast<float>(held.size()) * (rowH + gap));
        float y = area.y - ui.scrollOffset();
        for (size_t i = 0; i < held.size(); ++i) {
            const gifts::Reward& w = held[i];
            const Rect row{area.x, y, area.w - ui.s(16), rowH};
            y += rowH + gap;
            const EquipState es = equipState(g_, w);
            const std::string label = equipLabel(es, false);
            const giftsui::ContentRowLayout C = giftsui::contentRowLayout(ui, row, giftart::rewardName(g_, w), gifts::kindName(w.kind), label);
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = C.equipSize;
            st.primary = es == EquipState::Can;
            st.selected = es == EquipState::Equipped;
            st.enabled = es == EquipState::Can;
            const bool visible = !(row.bottom() < area.y || row.y > area.bottom());
            if (visible) {
                r.fillRoundedRect(row, ui.s(10), Color{18, 26, 60, 255});
                r.drawRoundedRect(row, ui.s(10), Color{44, 58, 104, 255}, ui.s(1.0f));
            }
            if (ui.button("equip/" + std::to_string(i), C.equip, label, st)) equipReward(g_, w);
            if (!visible) continue;
            r.pushClip(C.icon);
            r.drawGlow(C.icon.center(), C.icon.w * 0.5f, light.withAlphaF(0.25f));
            giftart::drawReward(g_, &online_, w, C.icon.center(), C.icon.w * (w.kind == "title" ? 0.5f : 0.9f), ui.time(), false);
            r.popClip();
            ui.label(C.name.shown, {C.name.box.x, C.name.box.center().y}, C.name.size, light, Align::Left, FontFace::Bold, VAlign::Middle);
            ui.label(C.kind.shown, {C.kind.box.x, C.kind.box.center().y}, C.kind.size, kMuted, Align::Left, FontFace::Body, VAlign::Middle);
        }
        ui.endScroll();
    }

    /// Cosmic Online can be asked now (offline, the gifts wait).
    bool canReach() const { return g_.online.usable() && g_.online.loggedIn() && !offline_ && g_.online.state() != OnlineState::Offline; }

    void open(const gifts::Gift& x) {
        if (x.opened || !canReach()) return;
        g_.uiSound("ui_confirm", 0.5f);
        g_.app.pushScene(makeGiftOpening(g_, GiftOpening{x, false, false}));
    }

    GameServices& g_;
    OnlineUi online_;
    MenuBackground bg_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    giftsui::ScreenLayout L_;
    int64_t sel_ = 0;
    int filter_ = 0;
    bool loading_ = false, offline_ = false;
    std::string error_;
};

// ================================================================== the opening

class GiftOpeningScene : public Scene {
public:
    GiftOpeningScene(GameServices& g, GiftOpening o) : g_(g), online_(g), o_(std::move(o)) {}

    bool isOverlay() const override { return true; }

    void onEnter() override { start(); }

    void update(float dt) override {
        dt = std::min(dt, 0.05f);
        t_ += dt;
        // Until Cosmic Online opened it, the box keeps shaking just before it bursts.
        if (!known() && t_ > kBoom - 0.05f) t_ = kBoom - 0.05f;
        if (!rumbled_) {
            rumbled_ = true;
            g_.uiSound("story_stone", 0.55f);
        }
        if (known() && t_ >= kBoom && !boomed_) boom();
        for (Part& p : parts_) {
            p.age += dt;
            if (p.age > p.life) continue;
            p.vy += 700.0f * dt;
            const float drag = std::pow(0.985f, dt * 60.0f);
            p.vx *= drag;
            p.vy *= drag;
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.rot += dt * 5.0f;
        }
        if (known() && t_ >= kReveal && !revealed_) {
            revealed_ = true;
            g_.ui.setFocus("gift/open/next");
        }
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        const bool top = g_.app.topScene() == this;
        ui.inputEnabled = top;
        drawScene();
        ui.pushId("gift/open");
        if (!error_.empty()) drawError();
        else if (revealed_) drawReveal();
        else drawWaiting();
        ui.popId();
        ui.inputEnabled = true;
        if (top) ui.end();
    }

private:
    struct Part {
        float x, y, vx, vy, life, age, s, rot;
        Color c;
        bool star;
    };

    /// What it held is known (the server opened it, or a replay, or the administrator's preview).
    bool known() const { return gifts::isRevealed(o_.gift); }
    /// The box's mouth above its base, in the scene's pixels (6 per pixel of the sheet).
    float mouth() const { return giftart::mouthHeight(o_.gift.parcel) * 6.0f; }

    void start() {
        t_ = 0.0f;
        boomed_ = revealed_ = rumbled_ = equippedNow_ = rise_ = false;
        parts_.clear();
        error_.clear();
        u_ = gifts::Unwrapping{};
        u_.items = gifts::revealed(o_.gift);
        rng_.seed(static_cast<uint32_t>(o_.gift.key() * 2654435761u + 17u));
        if (o_.replay || o_.preview || known()) return;
        // Opening is the server's: it grants what the gift (or every reward of the parcel) holds, once; the game
        // learns it here, a parcel's rewards in the order they come out.
        const int64_t key = o_.gift.key();
        const bool parcel = o_.gift.parcel;
        g_.online.send("POST", gifts::openPath(o_.gift), Json::object(), OnlineService::guard(life_, [this, key, parcel](const ApiResult& r) {
                           if (key != o_.gift.key()) return;
                           if (!r.ok) {
                               error_ = !r.message.empty() ? r.message
                                        : parcel        ? std::string("Le colis n'a pas pu être ouvert.")
                                                        : std::string("Le cadeau n'a pas pu être ouvert.");
                               g_.uiSound("ui_error", 0.6f);
                               return;
                           }
                           auto opened = gifts::parseGift(jfield(r.data, "gift", Json()));
                           if (!opened || !opened->opened || opened->parcel != parcel || !gifts::isRevealed(*opened)) {
                               error_ = "Réponse inattendue de Cosmic Online.";
                               return;
                           }
                           o_.gift = *opened;
                           u_.items = gifts::revealed(*opened);
                           g_.online.noteGiftOpened(*opened);
                           g_.online.refreshGifts();  // the list kept for offline follows
                           bool badge = false, item = false;
                           for (const auto& w : u_.items) (w.kind == "badge" ? badge : item) = true;
                           if (badge) rememberRaidRewards(g_, jfield(r.data, "raid_rewards_claimed", Json()));
                           if (item) g_.challenges.refreshWon();  // the items among the rewards won (offline too)
                       }));
    }

    /// Shards and stars from the box (`n` at the best particle quality).
    void burst(int n) {
        const Color light = gifts::wrapLight(o_.gift.wrap);
        const int q = g_.settings.graphics.particleQuality;
        n = q <= 0 ? n / 3 : q == 1 ? n * 2 / 3 : n;
        std::uniform_real_distribution<float> U(0.0f, 1.0f);
        for (int i = 0; i < n; ++i) {
            const float a = U(rng_) * 2.0f * kPi, v = 200.0f + U(rng_) * 700.0f;
            parts_.push_back({640.0f, 452.0f, std::cos(a) * v, std::sin(a) * v - 300.0f, 1.0f + U(rng_) * 1.3f, 0.0f, 3.0f + U(rng_) * 7.0f, U(rng_) * 6.0f,
                              U(rng_) < 0.35f ? Color::white() : light, U(rng_) < 0.5f});
        }
    }

    void boom() {
        boomed_ = true;
        burst(120);
        std::uniform_real_distribution<float> U(0.0f, 1.0f);
        lidVx_ = (U(rng_) < 0.5f ? -1.0f : 1.0f) * (160.0f + U(rng_) * 120.0f);
        lidVr_ = (U(rng_) - 0.5f) * 7.0f;
        g_.uiSound("story_egg_crack", 0.8f);
        g_.uiSound("story_sing", 0.5f);
    }

    /// « Suivant » in a parcel: the next item rises from the same box (its lid already gone), or the box is empty.
    void nextItem() {
        if (!u_.advance()) return;
        revealed_ = false;
        equippedNow_ = false;
        if (u_.empty) {
            t_ = kReveal;
            g_.uiSound("ui_confirm", 0.4f);
            return;
        }
        rise_ = true;
        t_ = kBoom;
        parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [](const Part& p) { return p.age > p.life; }), parts_.end());
        burst(48);
        g_.uiSound("story_sing", 0.45f);
    }

    // ---------------------------------------------------------------- drawing (the mockup's 1280 x 720 frame)
    Vec2 P(float x, float y) const { return {o_x + x * k_, o_y + y * k_}; }

    void ellipse(Vec2 c, float rx, float ry, Color col) {
        std::vector<Vec2> pts;
        for (int i = 0; i < 32; ++i) pts.push_back({c.x + std::cos(i * kPi / 16.0f) * rx, c.y + std::sin(i * kPi / 16.0f) * ry});
        g_.ui.r().fillConvexPolygon(pts, col);
    }

    void ellipseRing(Vec2 c, float rx, float ry, float thick, Color col) {
        std::vector<SDL_Vertex> v;
        std::vector<int> idx;
        const SDL_FColor fc{col.r / 255.0f, col.g / 255.0f, col.b / 255.0f, col.a / 255.0f};
        constexpr int n = 64;
        for (int i = 0; i <= n; ++i) {
            const float a = static_cast<float>(i) * 2.0f * kPi / n, ca = std::cos(a), sa = std::sin(a);
            v.push_back({{c.x + ca * (rx - thick * 0.5f), c.y + sa * (ry - thick * 0.5f)}, fc, {0, 0}});
            v.push_back({{c.x + ca * (rx + thick * 0.5f), c.y + sa * (ry + thick * 0.5f)}, fc, {0, 0}});
            if (i > 0) {
                const int b = (i - 1) * 2;
                idx.insert(idx.end(), {b, b + 1, b + 2, b + 1, b + 3, b + 2});
            }
        }
        g_.ui.r().drawGeometry(nullptr, v, idx, BlendMode::Additive);
    }

    void rays(Vec2 c, float t, float alpha, Color light) {
        if (alpha <= 0.0f) return;
        std::vector<SDL_Vertex> v;
        std::vector<int> idx;
        const SDL_FColor in{light.r / 255.0f, light.g / 255.0f, light.b / 255.0f, alpha}, out{light.r / 255.0f, light.g / 255.0f, light.b / 255.0f, 0.0f};
        const float L = 900.0f * k_;
        for (int i = 0; i < 16; ++i) {
            const float a = t * 0.25f + static_cast<float>(i + 1) * 2.0f * kPi / 16.0f, wd = std::atan(i % 2 ? 0.05f : 0.09f);
            const int b = static_cast<int>(v.size());
            v.push_back({{c.x, c.y}, in, {0, 0}});
            v.push_back({{c.x + std::cos(a - wd) * L, c.y + std::sin(a - wd) * L}, out, {0, 0}});
            v.push_back({{c.x + std::cos(a + wd) * L, c.y + std::sin(a + wd) * L}, out, {0, 0}});
            idx.insert(idx.end(), {b, b + 1, b + 2});
        }
        g_.ui.r().drawGeometry(nullptr, v, idx, BlendMode::Additive);
    }

    void drawScene() {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const float W = ui.screenW(), H = ui.screenH();
        k_ = std::min(W / 1280.0f, H / 720.0f);
        o_x = (W - 1280.0f * k_) * 0.5f;
        o_y = (H - 720.0f * k_) * 0.5f;
        const float t = t_, shake = std::clamp(g_.settings.graphics.screenShake, 0.0f, 1.0f);
        const Color light = gifts::wrapLight(o_.gift.wrap);
        const float S = 6.0f * k_;  // screen pixels per pixel of the wraps' sheet
        const Vec2 base = P(640.0f, 452.0f);
        const bool parcel = o_.gift.parcel;
        const float top = giftart::mouthHeight(parcel), half = parcel ? 20.0f : 16.0f;  // the box's top edge, half its width
        // The scene darkens, the wrap's light fills it.
        r.fillRect({0, 0, W, H}, Color{4, 6, 18, 255}.withAlphaF(std::min(0.88f, 0.35f + t * 0.5f)));
        r.drawGlow(P(640.0f, 330.0f), 700.0f * k_, light.withAlphaF(std::min(0.22f, t * 0.08f) + (t > kBoom ? 0.12f : 0.0f)));
        // Rays turning slowly, brighter once open.
        float rayA = t < kBoom ? std::max(0.0f, (t - 0.3f) / kBoom) * 0.18f : std::min(0.55f, 0.18f + (t - kBoom) * 1.2f);
        if (t > kBoom + 1.2f) rayA *= 0.75f + 0.25f * std::sin(t * 2.0f);
        if (u_.empty) rayA *= 0.4f;
        rays(t < kBoom ? Vec2{base.x, base.y - 120.0f * k_} : P(640.0f, 330.0f), t, rayA, light);
        if (t < kBoom) {
            // Anticipation: harder and harder shaking, hops and squash, light leaking from the seam.
            const float k = t / kBoom, wob = std::sin(t * (10.0f + k * 30.0f));
            const float rot = wob * (0.02f + k * 0.09f) * shake;
            const float hop = -std::abs(std::sin(t * kPi * 1.6f)) * (k > 0.35f ? 18.0f * k : 0.0f) * shake * k_;
            const float sq = 1.0f + wob * 0.03f * k * shake;
            ellipse({base.x, base.y + 6.0f * k_}, 120.0f * k_ * (1.0f - (-hop) / (120.0f * k_)), 16.0f * k_, Color{0, 0, 0, 115});
            giftart::BoxPose pose;
            pose.parcel = parcel;
            pose.rot = rot;
            pose.sx = 1.0f / sq;
            pose.sy = sq;
            pose.lidDy = -std::max(0.0f, k - 0.6f) * 20.0f * std::abs(std::sin(t * 25.0f)) * shake / 6.0f;
            const Vec2 b{base.x, base.y + hop};
            giftart::drawBox(g_, o_.gift.wrap, b, S, pose);
            const float sy = b.y - top * S, la = std::max(0.0f, k - 0.25f) / 0.75f;
            if (la > 0.0f) {
                r.fillGradientV({b.x - half * S, sy - 40.0f * k_ * la, 2.0f * half * S, 40.0f * k_ * la + 4.0f * k_}, Color{255, 255, 255, 0},
                                light.withAlphaF(0.9f * la), BlendMode::Additive);
                r.fillRect({b.x - (half - 1.0f) * S, sy - 1.0f * k_, 2.0f * (half - 1.0f) * S, 3.0f * k_}, Color::white().withAlphaF(la), BlendMode::Additive);
                for (int i = 0; i < 5; ++i) {
                    const float bx = b.x + (i - 2) * 5.5f * S;
                    const float h = (60.0f + static_cast<float>((i * 17) % 40)) * la * (1.0f + 0.3f * std::sin(t * 9.0f + i)) * k_;
                    r.fillGradientV({bx - 3.0f * k_, sy - h, 6.0f * k_, h}, Color{255, 255, 255, 0}, Color::white().withAlphaF(0.7f * la), BlendMode::Additive);
                }
            }
            for (int i = 0; i < 14; ++i) {  // sparks rising
                const float p = std::fmod(t * 0.6f + i * 0.137f, 1.0f);
                if (p * kBoom > t) continue;
                const Vec2 c = P(640.0f + std::sin(i * 7.3f + t) * 120.0f * p + (i - 7) * 12.0f, 452.0f - 30.0f - p * 260.0f);
                giftart::star(r, {c.x, c.y + hop}, (3.0f + i % 3) * k_, t * 3.0f + i, light.withAlphaF((1.0f - p) * la), BlendMode::Additive);
            }
            return;
        }
        const float u = t - kBoom;
        // The open box, its inner glow; the lid flying off with gravity and spin (the first item of a parcel only:
        // the next ones rise from the box already open).
        ellipse({base.x, base.y + 6.0f * k_}, 120.0f * k_, 16.0f * k_, Color{0, 0, 0, 115});
        giftart::BoxPose open;
        open.open = true;
        open.parcel = parcel;
        giftart::drawBox(g_, o_.gift.wrap, base, S, open);
        const Vec2 mouthAt{base.x, base.y - top * S};
        r.drawGlow(mouthAt, 170.0f * k_, light.withAlphaF(u_.empty ? 0.2f : 0.45f));
        r.drawGlow(mouthAt, 70.0f * k_, Color{255, 255, 255, static_cast<uint8_t>(u_.empty ? 70 : 190)});
        if (u < 1.4f && !rise_) {
            const float lx = lidVx_ * u, ly = -1150.0f * u + 900.0f * u * u;
            giftart::drawLid(g_, o_.gift.wrap, base, S, {lx * k_, ly * k_}, lidVr_ * u, std::max(0.0f, 1.0f - u / 1.4f), parcel);
        }
        if (u < 0.9f && !rise_) {  // the shockwave
            const float rx = (40.0f + u * 900.0f) * k_;
            ellipseRing(mouthAt, rx, rx * 0.55f, (10.0f * (1.0f - u / 0.9f) + 1.0f) * k_, Color::white().withAlphaF(1.0f - u / 0.9f));
        }
        // The item rises (ease-out-back) in a pulsing halo, ringed with twelve stars (none once the parcel is empty).
        if (const gifts::Reward* rw = u_.current()) {
            const float kk = std::min(1.0f, u / 0.95f);
            const float e = 1.0f + 2.70158f * std::pow(kk - 1.0f, 3.0f) + 1.70158f * std::pow(kk - 1.0f, 2.0f);
            const float from = 452.0f - mouth();
            const float iy = from + (250.0f - from) * e, sc = 0.3f + 0.7f * e;
            const float pulse = 1.0f + 0.06f * std::sin(t * 3.2f);
            const Vec2 ic = P(640.0f, iy);
            r.drawGlow(ic, 190.0f * sc * pulse * k_, light.withAlphaF(0.55f));
            r.drawGlow(ic, 80.0f * sc * pulse * k_, Color{255, 255, 255, 200});
            r.drawRing(ic, 135.0f * sc * k_, std::max(1.0f, 2.0f * k_), light.withAlphaF(0.7f), BlendMode::Additive);
            for (int i = 0; i < 12; ++i) {
                const float a = t * 0.9f + i * kPi / 6.0f;
                giftart::star(r, {ic.x + std::cos(a) * 135.0f * sc * k_, ic.y + std::sin(a) * 135.0f * sc * k_}, (i % 3 ? 3.0f : 6.0f) * sc * k_, t * 2.0f + i,
                              Color::white(), BlendMode::Additive);
            }
            giftart::drawReward(g_, &online_, *rw, ic, (rw->kind == "title" ? 120.0f : 190.0f) * sc * k_, t, true);
        }
        if (u < 0.45f && !rise_ && !u_.empty) r.fillRect({0, 0, W, H}, Color::white().withAlphaF((1.0f - u / 0.45f) * (0.5f + 0.4f * shake)));
        for (const Part& p : parts_) {  // the shards
            if (p.age > p.life) continue;
            const Color c = p.c.withAlphaF(std::max(0.0f, 1.0f - p.age / p.life));
            const Vec2 at = P(p.x, p.y);
            if (p.star) giftart::star(r, at, p.s * k_, p.rot, c, BlendMode::Additive);
            else r.fillRect({at.x - p.s * k_ / 3.0f, at.y - p.s * k_ / 3.0f, p.s * k_ * 0.66f, p.s * k_ * 0.66f}, c, BlendMode::Additive);
        }
        for (int i = 0; i < 26; ++i) {  // dust of light rising
            const float p = std::fmod(t * 0.18f + i * 0.0731f, 1.0f);
            giftart::star(r, P(640.0f + std::sin(i * 12.9f) * 520.0f + std::sin(t + i) * 20.0f, 720.0f - p * 720.0f), (2.0f + i % 3) * k_, t + i,
                          light.withAlphaF(std::sin(p * kPi) * 0.8f), BlendMode::Additive);
        }
    }

    void drawWaiting() {
        UIContext& ui = g_.ui;
        InputManager& in = g_.app.input();
        const bool skip = in.menuPressed(Action::MenuConfirm) || in.keyPressed(SDL_SCANCODE_SPACE) || in.mousePressed(SDL_BUTTON_LEFT);
        if (skip && known()) {
            if (!boomed_) boom();
            t_ = kReveal;
        }
        const std::string line = known() || t_ < kBoom - 0.1f ? "Clic / Entrée : passer l'animation"
                                 : o_.gift.parcel           ? "Ouverture du colis…"
                                                            : "Ouverture du cadeau…";
        ui.label(line, {ui.screenW() - ui.s(36), ui.s(36)}, 18, kMuted, Align::Right, FontFace::Body, VAlign::Middle);
        if (ui.backPressed() && o_.preview) g_.app.popScene();
    }

    void drawError() {
        UIContext& ui = g_.ui;
        const float W = ui.screenW(), H = ui.screenH(), w = std::min(W - ui.s(80), ui.s(820));
        const Rect box{(W - w) * 0.5f, H * 0.62f, w, ui.s(170)};
        ui.r().fillRoundedRect(box, ui.s(14), Color{14, 20, 44, 240});
        ui.r().drawRoundedRect(box, ui.s(14), ui.theme().danger, ui.s(1.5f));
        const auto lines = raidui::wrap(ui, error_, FontFace::Body, 19, w - ui.s(48), 2);
        raidui::drawLines(ui, lines, {box.x + ui.s(24), box.y + ui.s(22)}, 19, ui.s(26), ui.theme().text);
        UIContext::ButtonStyle st;
        st.primary = true;
        if (ui.button("back", {box.center().x - ui.s(110), box.bottom() - ui.s(70), ui.s(220), ui.s(52)}, "Retour", st) || ui.backPressed())
            g_.app.popScene();
    }

    /// The texts under the item (or the empty parcel) and the buttons of this step.
    giftsui::RevealTexts revealTexts(const gifts::Gift* next, int left, EquipState es) const {
        const gifts::Gift& x = o_.gift;
        giftsui::RevealTexts T;
        const std::string d = gifts::dateText(x.createdAt);
        T.message = x.message.empty() ? std::string() : "« " + x.message + " »";
        if (const gifts::Reward* rw = u_.current()) {
            T.kind = gifts::kindName(rw->kind) + (o_.preview ? " · aperçu" : "");
            T.name = giftart::rewardName(g_, *rw);
            T.from = o_.preview ? "De " + x.from + " · ce que verront les joueurs" : "De " + x.from + (d.empty() ? "" : " · " + d) + " · ajouté à votre collection";
        } else {  // the parcel is empty
            T.kind = "Colis · emballage " + gifts::wrapName(x.wrap) + (o_.preview ? " · aperçu" : "");
            T.name = "Le colis est vide";
            T.from = o_.preview ? "De " + x.from + " · ce que verront les joueurs" : "De " + x.from + (d.empty() ? "" : " · " + d) + " · tout est dans votre collection";
        }
        const std::string equip = equippedNow_ && es != EquipState::NoSlot ? std::string("Équipé") : equipLabel(es, true);
        if (o_.preview) T.buttons = x.parcel && !u_.empty ? std::vector<std::string>{"Suivant", "Retour à l'envoi"} : std::vector<std::string>{"Retour à l'envoi"};
        else if (x.parcel && !u_.empty) T.buttons = {equip, "Suivant", "Plus tard"};
        else if (x.parcel) T.buttons = next ? std::vector<std::string>{"Cadeau suivant (" + std::to_string(std::max(1, left)) + ")", "Fermer"} : std::vector<std::string>{"Fermer"};
        else {
            T.buttons = {equip, next ? "Cadeau suivant (" + std::to_string(std::max(1, left)) + ")" : std::string("Voir mes cadeaux")};
            if (next) T.buttons.push_back("Plus tard");
        }
        return T;
    }

    void drawReveal() {
        UIContext& ui = g_.ui;
        const gifts::Gift& x = o_.gift;
        const gifts::Reward* rw = u_.current();
        if (!rw && !(x.parcel && u_.empty)) return;
        const Color light = gifts::wrapLight(x.wrap);
        const gifts::Gift* next = o_.preview ? nullptr : gifts::nextToOpen(g_.online.myGifts(), x.key());
        const int left = o_.preview ? 0 : gifts::unopenedCount(g_.online.myGifts()) - (x.opened ? 0 : 1);
        const EquipState es = o_.preview || !rw ? EquipState::Unknown : equipState(g_, *rw);
        const giftsui::RevealTexts T = revealTexts(next, left, es);
        const giftsui::RevealLayout R = giftsui::revealLayout(ui, T);
        const float a = std::clamp((t_ - kReveal) / 0.5f, 0.0f, 1.0f), dy = (1.0f - a) * ui.s(16);
        // A dark veil behind the texts, so that they read over the light.
        const Rect veil{0, R.kind.box.y - ui.s(20), ui.screenW(), ui.screenH() - R.kind.box.y + ui.s(20)};
        ui.r().fillGradientV(veil, Color{4, 6, 18, 0}, Color{4, 6, 18, static_cast<uint8_t>(200 * a)});
        ui.label(R.kind.shown, {R.kind.box.center().x, R.kind.box.center().y + dy}, R.kind.size, light.withAlphaF(a), Align::Center, FontFace::Bold,
                 VAlign::Middle);
        ui.r().drawGlow({R.name.box.center().x, R.name.box.center().y + dy}, R.name.box.h * 1.6f, light.withAlphaF(0.18f * a));
        ui.label(R.name.shown, {R.name.box.center().x, R.name.box.center().y + dy}, R.name.size, (rw ? light : ui.theme().text).withAlphaF(a), Align::Center,
                 FontFace::Bold, VAlign::Middle);
        ui.label(R.from.shown, {R.from.box.center().x, R.from.box.center().y + dy}, R.from.size, kMuted.withAlphaF(a), Align::Center, FontFace::Body,
                 VAlign::Middle);
        {
            i18n::Verbatim v(true);  // wrap() gave them as the administrator wrote them
            float y = R.messageY + dy;
            for (const auto& l : R.messageLines) {
                ui.label(l, {ui.screenW() * 0.5f, y + R.messageLineH * 0.5f}, R.messageSize, ui.theme().text.withAlphaF(a), Align::Center, FontFace::Body,
                         VAlign::Middle);
                y += R.messageLineH;
            }
        }
        if (a < 0.3f) return;
        InputManager& in = g_.app.input();
        const bool yEdge = in.menuPressed(Action::GiftEquip);  // 2.93: Y / Ⓨ by default (Paramètres › Contrôles)
        UIContext::ButtonStyle primary;
        primary.primary = true;
        primary.fontSize = R.buttonSize;
        UIContext::ButtonStyle plain;
        plain.fontSize = R.buttonSize;
        if (o_.preview) {  // the administrator's preview: « Suivant » through a parcel, « Retour à l'envoi »
            if (x.parcel && !u_.empty) {
                if (ui.button("next", R.buttons[0], T.buttons[0], primary)) {
                    nextItem();
                    return;
                }
                if (ui.button("back", R.buttons[1], T.buttons[1], plain) || ui.backPressed()) g_.app.popScene();
                drawRevealHints(R, false, {{Action::MenuConfirm, "suivant"}, {Action::MenuBack, "retour"}});
                return;
            }
            if (ui.button("next", R.buttons[0], T.buttons[0], primary) || ui.backPressed()) g_.app.popScene();
            drawRevealHints(R, false, {{Action::MenuBack, "retour"}});
            return;
        }
        if (rw) {  // « Équiper » what is shown (Y): each item of a parcel has its own
            UIContext::ButtonStyle eq;
            eq.fontSize = R.buttonSize;
            eq.enabled = es == EquipState::Can && !equippedNow_;
            eq.selected = es == EquipState::Equipped || equippedNow_;
            const bool equipAsked = ui.button("equip", R.buttons[0], T.buttons[0], eq) || (yEdge && eq.enabled);
            if (equipAsked && eq.enabled) {
                equipReward(g_, *rw);
                equippedNow_ = true;
            }
        }
        if (x.parcel && rw) {  // the next item of the parcel, or « Plus tard » (the rest is already in the collection)
            if (ui.button("next", R.buttons[1], T.buttons[1], primary)) {
                nextItem();
                return;
            }
            if (ui.button("later", R.buttons[2], T.buttons[2], plain) || ui.backPressed()) {
                g_.app.popScene();
                return;
            }
            drawRevealHints(R, true, {{Action::MenuConfirm, "suivant"}, {Action::MenuBack, "plus tard"}});
            return;
        }
        // The gift's last step, or the empty parcel: the next gift waiting, or back to the list.
        const size_t at = rw ? 1 : 0;
        if (ui.button("next", R.buttons[at], T.buttons[at], primary)) {
            if (next) {
                o_.gift = *next;
                o_.replay = false;
                start();
                return;
            }
            g_.app.popScene();
            return;
        }
        if ((R.buttons.size() > at + 1 && ui.button(x.parcel ? "close" : "later", R.buttons[at + 1], T.buttons[at + 1], plain)) || ui.backPressed()) {
            g_.app.popScene();  // « Plus tard » / « Fermer »: the others wait
            return;
        }
        if (x.parcel && next) drawRevealHints(R, false, {{Action::MenuConfirm, "cadeau suivant"}, {Action::MenuBack, "fermer"}});
        else if (x.parcel) drawRevealHints(R, false, {{Action::MenuBack, "fermer"}});
        else drawRevealHints(R, true, {{Action::MenuConfirm, next ? "cadeau suivant" : "mes cadeaux"}, {Action::MenuBack, next ? "plus tard" : "fermer"}});
    }

    /// The keys of this step, centred on the bottom line (`equip`: « Y équiper » first).
    void drawRevealHints(const giftsui::RevealLayout& R, bool equip, const std::vector<std::pair<Action, std::string>>& hints) {
        UIContext& ui = g_.ui;
        const float h = ui.s(30);
        float w = 0.0f;
        for (const auto& [a, text] : hints) w += Glyphs::measureAction(ui, a, h) + ui.s(8) + ui.text().measure(text, FontFace::Body, ui.s(16)).x + ui.s(26);
        float x = R.hint.center().x - w * 0.5f;
        if (equip) {  // « Y équiper »: the key and the pad's Y (2.93: the player's, measured as drawn)
            const InputBindings& b = ui.input().bindings();
            const SDL_Scancode key = b.keyOf(Action::GiftEquip);
            const PadBinding pad = b.padOf(Action::GiftEquip);
            const float keysW = Glyphs::drawBinding(ui, key, {}, false, {0, 0}, h, false) + ui.s(4) + Glyphs::drawBinding(ui, SDL_SCANCODE_UNKNOWN, pad, true, {0, 0}, h, false);
            const float yw = keysW + ui.s(8) + ui.text().measure("équiper", FontFace::Body, ui.s(16)).x + ui.s(26);
            x -= yw * 0.5f;
            x += Glyphs::drawBinding(ui, key, {}, false, {x, R.hint.center().y}, h) + ui.s(4);
            x += Glyphs::drawBinding(ui, SDL_SCANCODE_UNKNOWN, pad, true, {x, R.hint.center().y}, h) + ui.s(8);
            x += ui.label("équiper", {x, R.hint.center().y}, 16, kMuted, Align::Left, FontFace::Body, VAlign::Middle).x + ui.s(26);
        }
        for (const auto& [a, text] : hints) x += Glyphs::drawHint(ui, a, text, {x, R.hint.center().y}, h) + ui.s(26);
    }

    GameServices& g_;
    OnlineUi online_;
    GiftOpening o_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::mt19937 rng_;
    gifts::Unwrapping u_;  // what comes out of the box, one after another
    float t_ = 0.0f, k_ = 1.0f, o_x = 0.0f, o_y = 0.0f;
    float lidVx_ = 200.0f, lidVr_ = 2.0f;
    bool boomed_ = false, revealed_ = false, rumbled_ = false, equippedNow_ = false;
    bool rise_ = false;  // a parcel's next item: the box is already open, its lid gone
    std::vector<Part> parts_;
    std::string error_;
};

}  // namespace

std::unique_ptr<Scene> makeGifts(GameServices& g, int64_t select) { return std::make_unique<GiftsScene>(g, select); }
std::unique_ptr<Scene> makeGiftOpening(GameServices& g, GiftOpening opening) { return std::make_unique<GiftOpeningScene>(g, std::move(opening)); }

}  // namespace cosmic
