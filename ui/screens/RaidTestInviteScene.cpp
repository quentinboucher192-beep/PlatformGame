// 2.80 (docs/RAIDS.md « Tester avec des joueurs invités »): the invitations to an administrator's test of Asterion still to answer, over the
// screen they were opened from (NOTIFICATIONS: its strip, or the notification « Répondre à l'invitation »).
// Each one says who invites, which test (one zone of a branch, or the whole raid simulated: its groups),
// where this player would play (the seat of a simulated player; the group), whether the administrator made
// them invincible and the time left (3 minutes). « Accepter et rejoindre » checks this game's fingerprint
// with the server (the same version and rules as the test, else a clear message), then joins the test in
// place of this window (RaidGameScene); « Refuser » tells the administrator. A test never counts.
//
// Keyboard, gamepad (every button takes the focus, the list scrolls to it, Échap / B goes back) and mouse;
// every text measured into its room (wrapped on counted lines, fitted, shortened).

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/online/Raids.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidRun.h"
#include "game/raid/RaidTestRelay.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

using namespace raidui;

const Color kRed{255, 106, 106, 255};

class RaidTestInviteScene : public Scene {
public:
    explicit RaidTestInviteScene(GameServices& g) : g_(g) {}

    bool isOverlay() const override { return true; }

    void onEnter() override {
        list_ = g_.online.testInvitations();
        load();
        g_.ui.setFocus("raidtestinvite/back");
        focusFirst_ = true;
    }

    void update(float dt) override {
        reloadIn_ -= dt;
        if (reloadIn_ <= 0.0f && !busy_) load();
    }

    void load() {
        reloadIn_ = 10.0f;
        if (!g_.online.loggedIn()) return;
        g_.online.get("/api/v1/raid-tests/invitations", OnlineService::guard(life_, [this](const ApiResult& r) {
                          loaded_ = true;
                          if (!r.ok || r.fromCache) return;
                          skew_ = skewOf(r.data);
                          list_ = r.data.value("invitations", Json::array());
                          g_.online.noteTestInvitations(list_);
                      }),
                      false);
    }

    /// What the test is, from its settings (the branch and zone named from this game's data).
    static std::string describe(const Json& inv) {
        const std::string mode = jstr(inv, "mode");
        const auto c = raid::RelayedTestConfig::fromJson(mode, inv.value("config", Json::object()));
        if (!c) return "Un test d'Asterion (réglages illisibles pour cette version du jeu : mettez-la à jour).";
        if (c->fullRaid)
            return "Raid complet simulé : " + std::to_string(c->groups) + (c->groups > 1 ? " groupes de " : " groupe de ") + std::to_string(c->bots + 1) +
                   " places · vous jouez dans le Groupe " + std::to_string(jget<int>(inv, "group", 0) + 1) + " à la place d'un joueur simulé.";
        std::string where = c->branch;
        for (const raid::Branch* b : raid::Branch::all())
            if (b->id == c->branch) {
                const raid::ZoneInfo* z = b->zone(c->zone == raid::RaidRun::kArmory ? 0 : c->zone);
                where = b->name + " · " + (c->zone == raid::RaidRun::kArmory ? std::string("Vestiaire") : z ? z->name : std::string("?"));
            }
        return "Une zone : " + where + " · " + std::to_string(c->players) + " places · vous jouez à la place d'un joueur simulé.";
    }

    void answer(const Json& inv, bool yes) {
        if (busy_) return;
        busy_ = true;
        error_.clear();
        const std::string id = jstr(inv, "test_id");
        if (!yes) {
            g_.online.send("POST", "/api/v1/raid-tests/" + id + "/decline", Json::object(), OnlineService::guard(life_, [this](const ApiResult& r) {
                               busy_ = false;
                               if (!r.ok) error_ = r.message;
                               load();
                           }));
            return;
        }
        g_.online.send("POST", "/api/v1/raid-tests/" + id + "/accept", {{"fingerprint", raid::testFingerprint()}},
                       OnlineService::guard(life_, [this](const ApiResult& r) {
                           busy_ = false;
                           if (!r.ok) {
                               error_ = r.message;
                               load();
                               return;
                           }
                           const Json t = r.data.value("test", Json::object());
                           RaidTestJoin j;
                           j.testId = jstr(t, "id");
                           j.mode = jstr(t, "mode");
                           j.fingerprint = jstr(t, "fingerprint");
                           j.config = t.value("config", Json::object());
                           j.adminName = jstr(t.value("admin", Json::object()), "username");
                           j.group = jget<int>(r.data.value("invitation", Json::object()), "group", 0);
                           g_.app.replaceTop(makeRaidTestJoin(g_, std::move(j)), 0.3f);
                       }));
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        const bool top = g_.app.topScene() == this;
        ui.inputEnabled = top;
        const float W = ui.screenW(), H = ui.screenH();
        ui.r().fillRect({0, 0, W, H}, Color{2, 4, 12, 200});
        const float w = std::min(ui.s(940), W - ui.s(32)), h = std::min(ui.s(640), H - ui.s(32));
        const Rect box{(W - w) * 0.5f, (H - h) * 0.5f, w, h};
        ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
        ui.r().drawRoundedRect(box, ui.s(18), kGold, ui.s(1.5f));
        ui.pushId("raidtestinvite");
        const float p = ui.s(24), x = box.x + p, iw = w - p * 2;
        fit(ui, "INVITATION À UN TEST D'ASTERION", {x, box.y + ui.s(40)}, 26, iw, kGold, FontFace::Title);
        const auto intro = wrap(ui, "Un administrateur teste le raid et vous invite à y jouer. Un test ne compte jamais : ni record, ni contribution, ni "
                                    "récompense, ni progression du raid.",
                                FontFace::Body, 15, iw, 2);
        float y = box.y + ui.s(68);
        y += drawLines(ui, intro, {x, y}, 15, ui.s(21), kDim) + ui.s(12);
        // The footer first (its room kept): the error, then « Retour ».
        const float bh = ui.s(50);
        const Rect back{x, box.bottom() - p - bh, iw, bh};
        const Rect err{x, back.y - ui.s(30), iw, ui.s(22)};
        const Rect list{x, y, iw, err.y - ui.s(8) - y};
        const int64_t now = nowUtc(skew_);
        if (list_.empty() || !list_.is_array()) {
            const auto lines = wrap(ui, loaded_ ? "Aucune invitation à un test en attente (elles durent 3 minutes)." : "Chargement des invitations…",
                                    FontFace::Body, 17, list.w, 3);
            drawLines(ui, lines, {list.x, list.y + ui.s(12)}, 17, ui.s(24), kText);
        } else {
            const float cardH = ui.s(196), gap = ui.s(12);
            ui.beginScroll("list", list, static_cast<float>(list_.size()) * (cardH + gap));
            float cy = list.y - ui.scrollOffset();
            int clicked = -1;
            bool accept = false;
            for (size_t i = 0; i < list_.size(); ++i, cy += cardH + gap) {
                const Json& inv = list_[i];
                const Rect card{list.x, cy, list.w - ui.s(14), cardH};
                if (card.bottom() < list.y || card.y > list.bottom()) continue;
                ui.r().fillRoundedRect(card, ui.s(12), Color{22, 28, 60, 255});
                const float cx = card.x + ui.s(16), cw = card.w - ui.s(32);
                // The time left, at the right of the first line; who invites in what is left.
                // An invitation accepted (its game left the test): only to come back while the test lasts.
                const bool accepted = jstr(inv, "status") == "accepted";
                const int64_t end = raids::parseUtc(jstr(inv, "expires_at")).value_or(now);
                const int64_t left = accepted ? 1 : std::max<int64_t>(0, end - now);
                char clock[24];
                std::snprintf(clock, sizeof clock, "%d:%02d", static_cast<int>(left / 60), static_cast<int>(left % 60));
                const std::string timeT = accepted ? std::string("acceptée") : left > 0 ? "expire dans " + std::string(clock) : std::string("expirée");
                const float tw = std::min(cw * 0.4f, textW(ui, timeT, 14, FontFace::Bold) + ui.s(4));
                fit(ui, timeT, {card.right() - ui.s(16) - tw, card.y + ui.s(26)}, 14, tw, left > 0 ? kGold : kRed, FontFace::Bold);
                const std::string admin = jstr(inv.value("admin", Json::object()), "username");
                fit(ui, admin.empty() ? std::string("Un administrateur") : admin, {cx, card.y + ui.s(26)}, 19, cw - tw - ui.s(16), kText, FontFace::Bold,
                    Align::Left, true);
                const auto what = wrap(ui, describe(inv), FontFace::Body, 15, cw, 2);
                drawLines(ui, what, {cx, card.y + ui.s(46)}, 15, ui.s(21), kDim);
                const bool inv8 = jget<bool>(inv, "invincible", false);
                fit(ui, inv8 ? "Invincible : oui (choisi par l'administrateur, qui peut le changer pendant le test)"
                             : "Invincible : non (l'administrateur peut le changer pendant le test)",
                    {cx, card.y + ui.s(100)}, 15, cw, inv8 ? kGreen : kDim);
                const float gap2 = ui.s(12), bw = accepted ? cw : (cw - gap2) * 0.5f, by = card.bottom() - ui.s(16) - ui.s(48);
                UIContext::ButtonStyle ok;
                ok.primary = true;
                ok.enabled = !busy_ && left > 0;
                const std::string okT = accepted ? "Revenir dans le test" : "Accepter et rejoindre";
                ok.fontSize = ui.fitFont(okT, FontFace::Bold, 22, bw - ui.s(24));
                ui.pushId(jstr(inv, "test_id"));
                if (focusFirst_ && i == 0) {
                    focusFirst_ = false;
                    ui.setFocus("accept");
                }
                if (ui.button("accept", {cx, by, bw, ui.s(48)}, okT, ok)) {
                    clicked = static_cast<int>(i);
                    accept = true;
                }
                UIContext::ButtonStyle no;
                no.enabled = !busy_ && left > 0;
                no.fontSize = ui.fitFont("Refuser", FontFace::Bold, 22, bw - ui.s(24));
                if (!accepted && ui.button("decline", {cx + bw + gap2, by, bw, ui.s(48)}, "Refuser", no)) clicked = static_cast<int>(i);
                ui.popId();
            }
            ui.endScroll();
            if (clicked >= 0) answer(Json(list_[static_cast<size_t>(clicked)]), accept);
        }
        if (busy_) fit(ui, "Réponse envoyée au serveur…", {err.x, err.center().y}, 15, err.w, kDim);
        else if (!error_.empty()) fit(ui, error_, {err.x, err.center().y}, 15, err.w, kRed);
        UIContext::ButtonStyle bs;
        bs.fontSize = ui.fitFont("Retour", FontFace::Bold, 22, back.w - ui.s(24));
        if (ui.button("back", back, "Retour", bs) || (top && ui.backPressed())) g_.app.popScene();
        ui.popId();
        ui.inputEnabled = true;
        if (top) ui.end();
    }

private:
    GameServices& g_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    Json list_ = Json::array();
    bool loaded_ = false, busy_ = false, focusFirst_ = false;
    float reloadIn_ = 10.0f;
    int64_t skew_ = 0;
    std::string error_;
};

}  // namespace

std::unique_ptr<Scene> makeRaidTestInvites(GameServices& g) { return std::make_unique<RaidTestInviteScene>(g); }

}  // namespace cosmic
