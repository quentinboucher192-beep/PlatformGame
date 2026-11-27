// 2.95 (docs/RAIDS.md §39): the raid's chat — its channels and what the field says (« /g », « /r »), the text a player
// may send, the server's pace mirrored, who may write where (a player down or eliminated still talks, a spectator of
// another group on « Raid » only, a local test that sends nothing), the simulated players' thoughts turned into lines
// only when they change and at a readable pace, the frames of a player typing (neutral: the replays stay exact), the
// relay's messages, the HUD element and its settings, and every line measured into its box.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <cmath>
#include <memory>

#include "engine/core/Application.h"
#include "engine/core/Scene.h"
#include "engine/input/InputManager.h"
#include "game/GameServices.h"
#include "game/coop/CoopProtocol.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/RaidChat.h"
#include "saves/RaidHudLayout.h"
#include "tests/TestFramework.h"
#include "ui/online/RaidChatUi.h"
#include "ui/online/RaidHud.h"
#include "ui/online/RaidMenu.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;
using raidchat::Channel;

namespace {
struct ChatBlank : Scene {  // a frame drawn at each size: the interface's scale follows it
    void render(float) override {}
};
}  // namespace

TEST_CASE("raid chat (2.95): the channels, « /g » and « /r », and the text a player may send") {
    CHECK(std::string(raidchat::channelLabel(Channel::Group)) == "Groupe");
    CHECK(std::string(raidchat::channelLabel(Channel::Raid)) == "Raid");
    CHECK(raidchat::otherChannel(Channel::Group) == Channel::Raid);
    // A plain message goes on the channel chosen, cleaned.
    raidchat::Draft d = raidchat::parseDraft("  On   y va\t!  ", Channel::Raid);
    CHECK(d.channel == Channel::Raid);
    CHECK(d.text == "On y va !");
    CHECK(!d.switchOnly && !d.unknownCommand);
    // « /g texte », « /groupe », « /r », « /raid » (any case): that channel for this message.
    d = raidchat::parseDraft("/g Golem à l'ouest", Channel::Raid);
    CHECK(d.channel == Channel::Group);
    CHECK(d.text == "Golem à l'ouest");
    d = raidchat::parseDraft("/RAID La Main est ouverte", Channel::Group);
    CHECK(d.channel == Channel::Raid);
    CHECK(d.text == "La Main est ouverte");
    d = raidchat::parseDraft("/groupe   ", Channel::Raid);
    CHECK(d.channel == Channel::Group);
    CHECK(d.switchOnly);
    d = raidchat::parseDraft("/r", Channel::Group);
    CHECK(d.switchOnly);
    CHECK(d.channel == Channel::Raid);
    // Another command is never sent.
    CHECK(raidchat::parseDraft("/kick Nova", Channel::Group).unknownCommand);
    CHECK(raidchat::parseDraft("/", Channel::Group).unknownCommand);
    CHECK(raidchat::parseDraft("   ", Channel::Group).text.empty());
    // One line, no control nor direction character, invalid UTF-8 dropped, 200 characters at most (whole ones).
    CHECK(raidchat::clean("a\x01" "b\x7f" "c\xe2\x80\xae" "d\xe2\x80\x8b" "e") == "abcde");
    CHECK(raidchat::clean("ligne\nsuivante\r\n") == "ligne suivante");
    CHECK(raidchat::clean("x\xff\xfey") == "xy");
    CHECK(raidchat::clean("\xc3") == "");
    std::string long1;
    for (int i = 0; i < 250; ++i) long1 += "\xc3\xa9";  // « é »
    const std::string c = raidchat::clean(long1);
    CHECK(raidchat::codePoints(c) == raidchat::kMaxChars);
    CHECK(c.size() == raidchat::kMaxChars * 2);
    CHECK(raidchat::codePoints("\xf0\x9f\x99\x82!") == 2);
}

TEST_CASE("raid chat (2.95): the server's pace mirrored, 3 at once then one a second") {
    raidchat::RateLimit r;
    CHECK(r.take(0.0f));
    CHECK(r.take(0.0f));
    CHECK(r.take(0.0f));
    CHECK(!r.take(0.0f));
    CHECK(r.waitFor(0.0f) > 0.9f);
    CHECK(!r.take(0.5f));
    CHECK(r.take(1.01f));
    CHECK(!r.take(1.2f));
    CHECK(r.take(10.0f));
    CHECK(r.waitFor(10.0f) == 0.0f);
}

TEST_CASE("raid chat (2.95): who may write where — down, eliminated, spectating, a local test, a server before 2.95") {
    raidchat::Where online;
    online.relayed = online.connected = online.serverChat = true;
    // A player down, waiting for a revival, and a player whose group was wiped still read and write.
    raidchat::Where down = online;
    down.down = true;
    CHECK(raidchat::access(down, Channel::Group).can);
    CHECK(raidchat::access(down, Channel::Raid).sends);
    raidchat::Where wiped = online;
    wiped.eliminated = true;
    CHECK(raidchat::access(wiped, Channel::Group).can);
    CHECK(raidchat::access(wiped, Channel::Raid).can);
    // A spectator of another group: « Raid » only (that group's « Groupe » is not theirs).
    raidchat::Where other = online;
    other.watchingOther = true;
    CHECK(!raidchat::access(other, Channel::Group).can);
    CHECK(!raidchat::access(other, Channel::Group).why.empty());
    CHECK(raidchat::access(other, Channel::Raid).can);
    // A local test: allowed, nothing sent. No link, or a relay before 2.95: never pretended sent.
    raidchat::Where local;
    local.local = true;
    CHECK(raidchat::access(local, Channel::Raid).can);
    CHECK(!raidchat::access(local, Channel::Raid).sends);
    raidchat::Where cut = online;
    cut.connected = false;
    CHECK(!raidchat::access(cut, Channel::Group).can);
    raidchat::Where old = online;
    old.serverChat = false;
    CHECK(!raidchat::access(old, Channel::Raid).can);

    // Submitted: sent to the relay (nothing shown before it comes back), or shown here in a local test.
    std::vector<std::pair<Channel, std::string>> sent;
    auto send = [&](Channel ch, const std::string& t) { sent.push_back({ch, t}); };
    raidchat::Chat chat;
    chat.channel = Channel::Group;
    CHECK(chat.submit("/r Bravo !", down, 1.0f, send, "Nova", "id-nova") == raidchat::Chat::Result::Sent);
    CHECK(sent.size() == 1);
    CHECK(sent[0].first == Channel::Raid);
    CHECK(sent[0].second == "Bravo !");
    CHECK(chat.channel == Channel::Group);  // the prefix is for this message only
    CHECK(chat.log.lines().empty());
    CHECK(chat.submit("/r", down, 1.0f, send, "Nova") == raidchat::Chat::Result::Switched);
    CHECK(chat.channel == Channel::Raid);
    CHECK(chat.submit("   ", down, 1.0f, send, "Nova") == raidchat::Chat::Result::Empty);
    CHECK(chat.submit("/x", down, 1.0f, send, "Nova") == raidchat::Chat::Result::Unknown);
    CHECK(chat.log.lines().back().kind == raidchat::Kind::Local);
    // The pace: three at once, the fourth waits (said in the chat, never sent).
    CHECK(chat.submit("un", down, 5.0f, send, "Nova") == raidchat::Chat::Result::Sent);
    CHECK(chat.submit("deux", down, 5.0f, send, "Nova") == raidchat::Chat::Result::Sent);
    CHECK(chat.submit("trois", down, 5.0f, send, "Nova") == raidchat::Chat::Result::Sent);
    CHECK(chat.submit("quatre", down, 5.0f, send, "Nova") == raidchat::Chat::Result::Wait);
    CHECK(sent.size() == 4);
    // A spectator of another group on « Groupe »: refused, said on that channel.
    CHECK(chat.submit("/g coucou", other, 9.0f, send, "Nova") == raidchat::Chat::Result::Refused);
    CHECK(chat.log.lines().back().channel == Channel::Group);
    CHECK(sent.size() == 4);
    // A local test: this player's line here (« Vous »), nothing sent.
    raidchat::Chat test;
    CHECK(test.submit("Prêt !", local, 3.0f, send, "Vous", "") == raidchat::Chat::Result::Shown);
    CHECK(sent.size() == 4);
    CHECK(test.log.lines().size() == 1);
    CHECK(test.log.lines()[0].mine);
    CHECK(test.log.lines()[0].name == "Vous");
    CHECK(test.log.lines()[0].text == "Prêt !");
}

TEST_CASE("raid chat (2.95): the simulated players' thoughts — only when they change, held, at a readable pace") {
    using raidchat::AiFeed;
    AiFeed feed;
    std::vector<raidchat::Thought> now = {{1, "Joueur simulé 1", "Réanimateur", "réanime Joueur simulé 3"}};
    CHECK(feed.update(0.0f, now).empty());  // not held yet (no flicker)
    std::vector<raidchat::Thought> out = feed.update(AiFeed::kStable + 0.1f, now);
    CHECK(out.size() == 1);
    CHECK(out[0].seat == 1);
    CHECK(out[0].intent == "réanime Joueur simulé 3");
    CHECK(feed.update(AiFeed::kStable + 0.2f, now).empty());  // the same thought: never twice
    CHECK(feed.update(30.0f, now).empty());
    // A second one: not before the global gap.
    const float t2 = 30.0f;
    now.push_back({2, "Joueur simulé 2", "Frappeur", "frappe le point faible"});
    CHECK(feed.update(t2, now).empty());
    out = feed.update(t2 + AiFeed::kStable + 0.1f, now);
    CHECK(out.size() == 1);
    CHECK(out[0].seat == 2);
    // The first one changes: held, then written once its own gap and the global one passed.
    const float t3 = t2 + 5.0f;
    now[0].intent = "attire le boss";
    CHECK(feed.update(t3, now).empty());
    CHECK(feed.update(t3 + AiFeed::kStable + 0.1f, now).size() == 1);  // its last line was long ago
    // Two changes too close: the global gap holds the second.
    now[1].intent = "court vers la plaque";
    CHECK(feed.update(t3 + 2.0f, now).empty());
    CHECK(feed.update(t3 + 3.7f, now).empty());  // the global gap since the last line not passed
    CHECK(feed.update(t2 + AiFeed::kStable + 0.1f + AiFeed::kSeatGap + 0.1f, now).size() == 1);
    // Back and forth to what was written: nothing (it is what the chat already says).
    const float t4 = 100.0f;
    now[1].intent = "frappe le point faible";
    feed.update(t4, now);
    now[1].intent = "court vers la plaque";
    CHECK(feed.update(t4 + AiFeed::kStable + 0.1f, now).empty());
    // An empty intention is never a line; another group shown starts afresh.
    feed.reset();
    CHECK(feed.update(200.0f, {{4, "Joueur simulé 4", "Coureur", ""}}).empty());
    CHECK(feed.update(205.0f, {{4, "Joueur simulé 4", "Coureur", ""}}).empty());
    // As a line of « Groupe », marked as the AI's, never sent.
    raidchat::Chat chat;
    chat.aiLine({3, "Joueur simulé 3", "Réanimateur", "réanime Joueur 5"}, 7.0f);
    CHECK(chat.log.lines().size() == 1);
    CHECK(chat.log.lines()[0].kind == raidchat::Kind::Ai);
    CHECK(chat.log.lines()[0].channel == Channel::Group);
    CHECK(chat.log.lines()[0].seat == 3);
}

TEST_CASE("raid chat (2.95): the frames of a player typing are neutral, the chat never reaches the simulation") {
    PlayerInput in;
    in.held[static_cast<int>(Action::MoveRight)] = true;
    in.pressed[static_cast<int>(Action::Jump)] = true;
    in.move = {1.0f, 0.0f};
    CHECK(!(raidmenu::playerFrame(in, raidmenu::Hold{}) == InputFrame{}));
    raidmenu::Hold typing;
    typing.chat = true;
    CHECK(typing.any());
    CHECK(raidmenu::playerFrame(in, typing) == InputFrame{});
    raidmenu::Overlay o;
    CHECK(!(o.frame(in, false, false, false) == InputFrame{}));
    o.chatOpen = true;
    CHECK(o.frame(in, false, false, false) == InputFrame{});
    CHECK(o.wantsPointer());
    // Its keys are never bits of the recorded actions.
    CHECK(static_cast<int>(Action::ChatOpen) >= InputFrame::kActions);
    CHECK(static_cast<int>(Action::ChatChannel) >= InputFrame::kActions);
}

TEST_CASE("raid chat (2.95): the lines kept, the history given again once, the fading") {
    raidchat::Log log;
    raidchat::Line l;
    l.channel = Channel::Raid;
    l.playerId = "p1";
    l.name = "Nova";
    l.text = "Salut";
    l.at = 1000;
    l.clock = 5.0f;
    log.push(l);
    // Given again on a reconnection: already here, not twice; a new old one is kept in its place, never as new.
    CHECK(!log.pushHistory(l));
    raidchat::Line older = l;
    older.text = "Plus ancien";
    older.at = 900;
    CHECK(log.pushHistory(older));
    CHECK(log.lines().size() == 2);
    CHECK(log.lines()[0].text == "Plus ancien");
    CHECK(raidchat::lineAlpha(log.lines()[0], 5.0f, 10.0f) == 0.0f);
    // Fading: whole while new, over its last second, gone after the delay.
    CHECK(raidchat::lineAlpha(l, 5.0f, 10.0f) == 1.0f);
    CHECK(raidchat::lineAlpha(l, 13.9f, 10.0f) == 1.0f);
    CHECK(raidchat::lineAlpha(l, 14.5f, 10.0f) > 0.4f);
    CHECK(raidchat::lineAlpha(l, 14.5f, 10.0f) < 0.6f);
    CHECK(raidchat::lineAlpha(l, 15.0f, 10.0f) == 0.0f);
    for (int i = 0; i < 300; ++i) log.push(l);
    CHECK(log.lines().size() == raidchat::kLogMax);
}

TEST_CASE("raid chat (2.95): the relay's messages, read exactly or refused") {
    const std::vector<uint8_t> m = coopnet::chatSend(1, "Prêt !");
    CHECK(m[0] == static_cast<uint8_t>(coopnet::Msg::ChatSend));
    CHECK(m[1] == 1);
    CHECK(m[2] + (m[3] << 8) == 7);
    // Cut on a whole character past 1024 bytes.
    std::string big;
    for (int i = 0; i < 600; ++i) big += "\xc3\xa9";
    const std::vector<uint8_t> b = coopnet::chatSend(0, big);
    CHECK(b[2] + (b[3] << 8) == 1024);
    coopnet::ChatLineMsg line;
    line.channel = 1;
    line.flags = coopnet::kChatHistory;
    line.at = 1800000000u;
    line.account = "id";
    line.name = "Nova";
    line.label = "[AUBE]·2";
    line.text = "La Main est ouverte !";
    const std::vector<uint8_t> w = coopnet::chatLine(line);
    coopnet::ChatLineMsg back;
    CHECK(coopnet::readChatLine(w.data() + 1, w.size() - 1, back));
    CHECK(back.channel == 1);
    CHECK(back.flags == coopnet::kChatHistory);
    CHECK(back.at == 1800000000u);
    CHECK(back.name == "Nova");
    CHECK(back.label == "[AUBE]·2");
    CHECK(back.text == line.text);
    // A channel, a flag or a length the game does not know: refused.
    std::vector<uint8_t> bad = w;
    bad[1] = 2;
    CHECK(!coopnet::readChatLine(bad.data() + 1, bad.size() - 1, back));
    bad = w;
    bad[2] = 8;
    CHECK(!coopnet::readChatLine(bad.data() + 1, bad.size() - 1, back));
    CHECK(!coopnet::readChatLine(w.data() + 1, w.size() - 2, back));
}

TEST_CASE("raid chat (2.95): its HUD element and settings, saved and read back within their bounds") {
    CHECK(std::string(RaidHudLayout::id(RaidHudElement::Chat)) == "chat");
    CHECK(RaidHudLayout::canHide(RaidHudElement::Chat));
    RaidHudLayout l;
    CHECK(l[RaidHudElement::Chat].visible);
    l.chat.width = 1.3f;
    l.chat.lines = 9;
    l.chat.fade = 20.0f;
    l.chat.opacity = 0.3f;
    l[RaidHudElement::Chat].custom = true;
    l[RaidHudElement::Chat].x = 0.6f;
    l[RaidHudElement::Chat].y = 0.7f;
    const RaidHudLayout back = RaidHudLayout::fromJson(l.toJson());
    CHECK(back.chat == l.chat);
    CHECK(back[RaidHudElement::Chat].custom);
    // A file with absurd values: clamped; an older file: the defaults.
    Json j = l.toJson();
    j["chat"]["lines"] = 500;
    j["chat"]["width"] = -3.0;
    j["chat"]["fade"] = 1e9;
    j["chat"]["opacity"] = "beaucoup";
    const RaidHudLayout clamped = RaidHudLayout::fromJson(j);
    CHECK(clamped.chat.lines == RaidChatSettings::kMaxLines);
    CHECK(clamped.chat.width == RaidChatSettings::kMinWidth);
    CHECK(clamped.chat.fade == RaidChatSettings::kMaxFade);
    CHECK(clamped.chat.opacity == RaidChatSettings{}.opacity);
    Json old = RaidHudLayout{}.toJson();
    old.erase("chat");
    CHECK(RaidHudLayout::fromJson(old).chat == RaidChatSettings{});
    CHECK(RaidHudLayout::fromJson(old)[RaidHudElement::Chat].visible);
}

TEST_CASE("raid chat (2.95): its default place over no element of the raids' HUD, every line and part measured into its box") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    Application app;
    AppConfig cfg;
    cfg.headless = true;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.vsync = false;
    CHECK(app.init(cfg));
    {
        GameServices g(app);
        g.loadAll();
        app.pushScene(std::make_unique<ChatBlank>());
        for (const Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}, Vec2{1366, 768}, Vec2{2560, 1440}, Vec2{1024, 768}}) {
            app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
            app.renderer().refreshOutputSize();
            app.runFrames(1);
            app.renderer().refreshOutputSize();
            UIContext& ui = g.ui;
            ui.begin(app, 1.0f / 60.0f);
            const float W = ui.screenW(), H = ui.screenH();
            CHECK(std::abs(ui.scale() - std::max(0.5f, std::min(W / 1920.0f, H / 1080.0f))) < 0.001f);
            // The game's HUD at its raid places (the vitals and powers at the top left, the gigaboss's bar at the
            // bottom centre), as Hud places them by default.
            raidhud::HudAreas hud;
            hud.vitals = {ui.s(40), ui.s(34), ui.s(300), ui.s(90)};
            hud.powers = {ui.s(40), ui.s(140), ui.s(360), ui.s(70)};
            hud.boss = {W * 0.5f - ui.s(456), H - ui.s(86) - ui.s(44), ui.s(912), ui.s(78)};
            for (const raidhud::HudAreas& areas : {raidhud::HudAreas{}, hud}) {
                RaidHudLayout l;
                const Rect chat = raidhud::area(ui, l, RaidHudElement::Chat, areas);
                CHECK(chat.x >= 0.0f);
                CHECK(chat.y >= 0.0f);
                CHECK(chat.right() <= W + 0.5f);
                CHECK(chat.bottom() <= H + 0.5f);
                CHECK(chat.x < W * 0.5f);   // at the left
                CHECK(chat.y > H * 0.5f);   // at the bottom
                CHECK(chat.h >= ui.s(150));  // its 7 lines and its field
                for (int i = 0; i < kRaidHudElements; ++i) {
                    const auto e = static_cast<RaidHudElement>(i);
                    if (e == RaidHudElement::Chat) continue;
                    const Rect o = raidhud::area(ui, l, e, areas);
                    if (o.w <= 0.0f) continue;
                    const bool clear = !chat.intersects(o);
                    if (!clear)
                        std::printf("  « Chat » (%.0f %.0f %.0f %.0f) touche « %s » (%.0f %.0f %.0f %.0f) en %.0f x %.0f\n", chat.x, chat.y, chat.w, chat.h,
                                    RaidHudLayout::label(e), o.x, o.y, o.w, o.h, size.x, size.y);
                    CHECK(clear);
                }
                // Its 12 lines at 160 %: still inside the screen (the editor refuses what would touch another element).
                l.chat.lines = RaidChatSettings::kMaxLines;
                l.chat.width = RaidChatSettings::kMaxWidth;
                const Rect big = raidhud::area(ui, l, RaidHudElement::Chat, areas);
                CHECK(big.y >= -0.5f);
                CHECK(big.right() <= W + 0.5f);
            }
            // Open, its parts one after the other inside its box; closed, its small button inside it too.
            RaidHudLayout l;
            const Rect area = raidhud::area(ui, l, RaidHudElement::Chat, hud);
            raidchatui::Look look;
            look.openKey = "T";
            look.hints = "Entrée : envoyer · Tab : canal · Échap : fermer · /g, /r";
            for (const Language lang : {Language::French, Language::English}) {
                i18n::setLanguage(lang);
                const raidchatui::Layout L = raidchatui::layout(ui, area, 1.0f, true, look, Channel::Group);
                for (const Rect& r : {L.header, L.history, L.quick, L.pill, L.field, L.send, L.quickPrev, L.quickText, L.quickNext}) {
                    CHECK(area.containsRect(r));
                    CHECK(r.w > 0.0f);
                }
                CHECK(L.header.bottom() <= L.history.y + 0.5f);
                CHECK(L.history.bottom() <= L.quick.y + 0.5f);
                CHECK(L.quick.bottom() <= L.row.y + 0.5f);
                CHECK(L.pill.right() <= L.field.x);
                CHECK(L.field.right() <= L.send.x);
                CHECK(L.quickPrev.right() <= L.quickText.x);
                CHECK(L.quickText.right() <= L.quickNext.x);
                // The words of « Envoyer » and of the channels fit their buttons (at their size, or fitted).
                CHECK(ui.text().measure(i18n::tr("Envoyer"), FontFace::Bold, ui.s(14)).x <= L.send.w + 0.5f);
                const raidchatui::Layout C = raidchatui::layout(ui, area, 1.0f, false, look, Channel::Group);
                CHECK(area.containsRect(C.pill));
                CHECK(C.pill.y >= C.lines.bottom() - 0.5f);
                // A line of 200 characters with a word longer than the box: every piece inside the width, in order.
                raidchat::Line line;
                line.channel = Channel::Raid;
                line.label = "[AUBE]·2";
                line.name = "Nova";
                line.text = std::string(120, 'W') + " et puis quelques mots qui suivent pour finir la ligne";
                const float width = L.history.w - ui.s(22);
                const std::vector<raidchatui::Visual> rows = raidchatui::wrapLine(ui, line, look, L.font, width);
                CHECK(rows.size() >= 3);
                for (const raidchatui::Visual& row : rows) {
                    CHECK(!row.empty());
                    float x = 0.0f;
                    for (const raidchatui::Piece& p : row) {
                        CHECK(p.x >= x - 0.01f);
                        CHECK(p.x + p.w <= width + 0.5f);
                        x = p.x + p.w;
                    }
                }
                // The AI's line: its tag, its name, its role and its intention.
                raidchat::Line ai;
                ai.kind = raidchat::Kind::Ai;
                ai.name = "Joueur simulé 3";
                ai.role = "Réanimateur";
                ai.text = "réanime Joueur simulé 5";
                const auto airows = raidchatui::wrapLine(ui, ai, look, L.font, width);
                std::string all;
                for (const auto& row : airows)
                    for (const auto& p : row) all += p.text + " ";
                CHECK(all.find(lang == Language::English ? "AI" : "IA") != std::string::npos);
                CHECK(all.find(lang == Language::English ? "Simulated player 3" : "Joueur simulé 3") != std::string::npos);  // the game's name, translated
            }
            i18n::setLanguage(Language::French);
            ui.end();
        }
    }
    app.shutdown();
}
