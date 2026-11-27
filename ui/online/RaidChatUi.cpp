#include "ui/online/RaidChatUi.h"

#include <algorithm>
#include <cmath>
#include <tuple>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "engine/input/InputManager.h"
#include "engine/rendering/Renderer.h"
#include "ui/online/RaidHud.h"
#include "ui/online/RaidWidgets.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::raidchatui {

using namespace raidui;
using raidchat::Channel;
using raidchat::Kind;
using raidchat::Line;

Color channelColor(Channel c) { return c == Channel::Raid ? Color{255, 176, 74, 255} : Color{124, 240, 240, 255}; }
Color aiColor() { return Color{201, 168, 255, 255}; }

struct View::Cache {
    uint64_t serial = 0;
    float width = 0.0f, px = 0.0f;
    Language language = Language::French;
    std::vector<std::vector<Visual>> lines;  // one per line of the log (empty for a line not shown)
    float height = 0.0f;
};

namespace {

const Color kPanelFill{10, 12, 28, 255};
const Color kServer{255, 138, 76, 255};

/// The placeholder of the field (the channel chosen).
const char* placeholder(Channel c) { return c == Channel::Raid ? "Message à tout le raid…" : "Message à votre groupe…"; }

/// A run of text of one style, before it is wrapped.
struct Span {
    std::string text;
    Color color;
    FontFace face = FontFace::Body;
    bool verbatim = false;  // a player's words: never translated
    bool spaceBefore = true;
};

/// The UTF-8 length of the character starting at `s[i]` (1 for a stray byte).
size_t charLen(const std::string& s, size_t i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    size_t n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
    return std::min(n, s.size() - i);
}

std::vector<Span> spansOf(const Line& l, const Look& look) {
    std::vector<Span> out;
    const bool english = i18n::language() == Language::English;
    out.push_back({"[" + i18n::tr(raidchat::channelLabel(l.channel)) + "]", channelColor(l.channel), FontFace::Bold, true, false});
    const Color name = look.nameColor ? look.nameColor(l) : kText;
    switch (l.kind) {
        case Kind::Player:
            if (!l.label.empty()) out.push_back({l.label, kDim, FontFace::Body, true, true});
            out.push_back({l.name.empty() ? std::string("?") : l.name, name, FontFace::Bold, true, true});
            out.push_back({":", kDim, FontFace::Body, true, !english});
            out.push_back({l.text, kText, FontFace::Body, true, true});
            break;
        case Kind::Ai:
            out.push_back({i18n::tr("IA"), aiColor(), FontFace::Bold, true, true});
            out.push_back({i18n::tr(l.name), name, FontFace::Bold, true, true});  // the game's name (« Joueur simulé 3 »)
            out.push_back({":", kDim, FontFace::Body, true, !english});
            if (!l.role.empty()) {
                out.push_back({i18n::tr(l.role), aiColor(), FontFace::Body, true, true});
                out.push_back({"→", kDim, FontFace::Body, true, true});
            }
            out.push_back({i18n::tr(l.text), kText, FontFace::Body, true, true});
            break;
        case Kind::Server:
            out.push_back({i18n::tr("Serveur"), kServer, FontFace::Bold, true, true});
            out.push_back({":", kDim, FontFace::Body, true, !english});
            out.push_back({i18n::tr(l.text), kPeach, FontFace::Body, true, true});
            break;
        case Kind::Local:
            out.push_back({i18n::tr(l.text), kGold, FontFace::Body, true, true});
            break;
    }
    return out;
}

/// The box of a button's text: its width measured at `size` plus its padding.
float measured(UIContext& ui, const std::string& t, float size, FontFace face) { return textW(ui, t, size, face) + ui.s(20) * size / 15.0f; }

}  // namespace

std::vector<Visual> wrapLine(UIContext& ui, const Line& l, const Look& look, float size, float width) {
    std::vector<Visual> out;
    if (width <= ui.s(20)) return out;
    const std::vector<Span> spans = spansOf(l, look);  // translated first (never under Verbatim: it would keep the French)
    i18n::Verbatim shown(true);  // every text below is already in the language shown
    const float px = ui.s(size);
    auto measure = [&](const std::string& t, FontFace f) { return t.empty() ? 0.0f : ui.text().measure(t, f, px).x; };
    out.emplace_back();
    float x = 0.0f;
    auto place = [&](const std::string& word, const Span& sp, bool space) {
        const float spaceW = space && x > 0.0f ? measure(" ", sp.face) : 0.0f;
        float w = measure(word, sp.face);
        if (x > 0.0f && x + spaceW + w > width) {
            out.emplace_back();
            x = 0.0f;
        } else if (x > 0.0f) {
            x += spaceW;
        }
        // A word wider than the line: cut between its characters, as many as fit on each line.
        std::string rest = word;
        while (w > width - x && !rest.empty()) {
            size_t cut = 0;
            for (size_t i = 0; i < rest.size();) {
                const size_t n = charLen(rest, i);
                if (measure(rest.substr(0, i + n), sp.face) > width - x) break;
                i += n;
                cut = i;
            }
            if (cut == 0) {
                if (x > 0.0f) {  // not even one character after what is there: the next line
                    out.emplace_back();
                    x = 0.0f;
                    continue;
                }
                cut = charLen(rest, 0);  // one character always goes (never an endless line)
            }
            const std::string head = rest.substr(0, cut);
            out.back().push_back({head, sp.color, sp.face, x, measure(head, sp.face)});
            rest = rest.substr(cut);
            out.emplace_back();
            x = 0.0f;
            w = measure(rest, sp.face);
        }
        if (rest.empty()) {
            if (out.back().empty() && out.size() > 1) out.pop_back();
            return;
        }
        Visual& v = out.back();
        // The same style right before (a space between): one piece.
        if (!v.empty() && v.back().color == sp.color && v.back().face == sp.face && spaceW > 0.0f) {
            v.back().text += " " + rest;
            v.back().w = x + w - v.back().x;
        } else {
            v.push_back({rest, sp.color, sp.face, x, w});
        }
        x += w;
    };
    for (const Span& sp : spans) {
        bool first = true;
        std::string word;
        bool spaced = sp.spaceBefore;
        for (size_t i = 0; i <= sp.text.size(); ++i) {
            if (i == sp.text.size() || sp.text[i] == ' ') {
                if (!word.empty()) {
                    place(word, sp, first ? spaced : true);
                    first = false;
                    word.clear();
                } else if (!first) {
                    spaced = true;
                }
                continue;
            }
            word += sp.text[i];
        }
    }
    if (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

Layout layout(UIContext& ui, const Rect& area, float k, bool open, const Look& look, Channel channel) {
    Layout L;
    L.area = area;
    const raidhud::ChatMetrics m = raidhud::chatMetrics(ui, k);
    L.font = m.font;
    L.lineH = m.lineH;
    L.row = {area.x, area.bottom() - m.rowH, area.w, m.rowH};
    L.lines = {area.x, area.y + m.pad, area.w, std::max(0.0f, L.row.y - m.gap - area.y - m.pad)};
    const float rowFont = 14.0f * k;
    if (!open) {
        // « Chat · T »: measured, at the left of the field's row (the mouse only).
        const std::string t = look.openKey.empty() ? std::string("Chat") : "Chat · " + look.openKey;
        const float w = std::min(area.w, measured(ui, t, rowFont, FontFace::Bold));
        L.pill = {area.x, L.row.y + L.row.h * 0.15f, w, L.row.h * 0.7f};
        return L;
    }
    const float inner = area.w - m.pad * 2.0f, x = area.x + m.pad;
    L.header = {x, L.lines.y, inner, m.lineH};
    L.quick = {x, L.lines.bottom() - m.lineH, inner, m.lineH};
    L.history = {area.x + m.pad * 0.5f, L.header.bottom(), area.w - m.pad, std::max(0.0f, L.quick.y - L.header.bottom())};
    const float arrow = std::min(m.lineH * 1.4f, inner * 0.2f);
    L.quickPrev = {x, L.quick.y, arrow, L.quick.h};
    L.quickNext = {L.quick.right() - arrow, L.quick.y, arrow, L.quick.h};
    L.quickText = {L.quickPrev.right() + m.gap, L.quick.y, std::max(0.0f, L.quickNext.x - m.gap - L.quickPrev.right() - m.gap), L.quick.h};
    // The row: the channel (the wider of its two names, measured), « Envoyer » (measured), the field between them.
    const float chanW = std::max(measured(ui, raidchat::channelLabel(Channel::Group), rowFont, FontFace::Bold),
                                 measured(ui, raidchat::channelLabel(Channel::Raid), rowFont, FontFace::Bold));
    (void)channel;
    const float sendW = measured(ui, "Envoyer", rowFont, FontFace::Bold);
    const float pillW = std::min(chanW, area.w * 0.3f), sendBoxW = std::min(sendW, area.w * 0.28f);
    L.pill = {area.x, L.row.y, pillW, L.row.h};
    L.send = {area.right() - sendBoxW, L.row.y, sendBoxW, L.row.h};
    L.field = {L.pill.right() + m.gap, L.row.y, std::max(0.0f, L.send.x - m.gap - L.pill.right() - m.gap), L.row.h};
    return L;
}

Keys handleKeys(UIContext& ui, const InputManager& in, raidchat::Chat& c, View& v, bool allowed, float dt) {
    Keys k;
    if (!c.open) {
        if (allowed && in.menuPressed(Action::ChatOpen)) {
            c.open = true;
            k.opened = true;
            v.focusPending = in.padPressed(Action::ChatOpen) ? View::Focus::Quick : View::Focus::Field;
            v.follow = true;
        }
        return k;
    }
    auto close = [&] {
        c.open = false;
        k.closed = true;
        v.focusPending = View::Focus::None;
        v.keepEditing = false;
        ui.stopEditing();
    };
    if (!allowed) {
        close();
        return k;
    }
    const bool typing = ui.editingText();
    // Échap (the key itself: the field takes the others), B or Start.
    if (in.keyPressed(SDL_SCANCODE_ESCAPE) || in.padPressed(Action::MenuBack)) {
        close();
        k.back = true;
        return k;
    }
    // The channel: its key (read as a key while typing: the field holds the commands then), RB, LB.
    const SDL_Scancode key = in.bindings().keyOf(Action::ChatChannel);
    const bool byKey = isKeyboardScancode(key) && in.keyPressed(key);
    if (byKey || in.padPressed(Action::ChatChannel) || in.padPressed(Action::MenuTabPrev) || in.padPressed(Action::MenuTabNext) ||
        (!typing && in.keyPressedFor(Action::ChatChannel))) {
        c.channel = raidchat::otherChannel(c.channel);
        if (byKey && typing) v.keepEditing = true;  // the field closes on Tab: it is opened again
    }
    if (!typing && in.menuPressed(Action::ChatOpen) && !in.padPressed(Action::ChatOpen)) v.focusPending = View::Focus::Field;
    // The history: Page précédente / suivante, the right stick.
    const float page = ui.s(160);
    if (in.keyPressed(SDL_SCANCODE_PAGEUP)) v.scrollBy -= page;
    if (in.keyPressed(SDL_SCANCODE_PAGEDOWN)) v.scrollBy += page;
    const float sy = in.rightStick().y;
    if (std::abs(sy) > 0.25f) v.scrollBy += sy * ui.s(700) * dt;
    return k;
}

Sent draw(UIContext& ui, raidchat::Chat& c, View& v, const Rect& area, float k, const RaidChatSettings& st, const Look& look, float clock,
          bool chatShown, bool interactive) {
    Sent sent;
    v.pill = {};
    v.drawn = {};
    if (area.w <= ui.s(60) || area.h <= ui.s(40)) return sent;
    Renderer& r = ui.r();
    ui.pushId("chat");
    const Layout L = layout(ui, area, k, c.open, look, c.channel);
    if (!c.open) {
        if (!chatShown) {
            ui.popId();
            return sent;
        }
        v.drawn = area;
        // The recent lines, the newest at the bottom, each on its strip, fading away.
        std::vector<std::vector<Visual>> wrapped;
        std::vector<float> alphas;
        int rows = 0;
        const int maxRows = std::max(1, static_cast<int>(std::floor(L.lines.h / L.lineH + 0.01f)));
        const auto& lines = c.log.lines();
        for (size_t i = lines.size(), scanned = 0; i-- > 0 && rows < maxRows && scanned < 60; ++scanned) {
            const Line& l = lines[i];
            const float a = raidchat::lineAlpha(l, clock, st.fade);
            if (a <= 0.0f) continue;
            wrapped.push_back(wrapLine(ui, l, look, L.font, L.lines.w - ui.s(12) * k));
            alphas.push_back(a);
            rows += static_cast<int>(wrapped.back().size());
        }
        float y = L.lines.bottom();
        for (size_t m = 0; m < wrapped.size() && y - L.lineH >= L.lines.y - 0.5f; ++m) {
            i18n::Verbatim done(true);  // already in the language shown
            const auto& vis = wrapped[m];
            for (size_t j = vis.size(); j-- > 0 && y - L.lineH >= L.lines.y - 0.5f;) {
                y -= L.lineH;
                float right = 0.0f;
                for (const Piece& p : vis[j]) right = std::max(right, p.x + p.w);
                const float a = alphas[m];
                r.fillRect({L.lines.x, y, std::min(L.lines.w, right + ui.s(12) * k), L.lineH},
                           Color{6, 8, 20, 255}.withAlphaF(std::clamp(st.opacity, 0.0f, 1.0f) * a));
                for (const Piece& p : vis[j])
                    ui.label(p.text, {L.lines.x + ui.s(6) * k + p.x, y + L.lineH * 0.5f}, L.font, p.color.withAlphaF(a * p.color.a / 255.0f), Align::Left,
                             p.face, VAlign::Middle);
            }
        }
        // « Chat · T »: the mouse opens it (never the focus nor a key: those are T and Partage).
        bool hover = false;
        if (interactive && ui.mouseClick("open", L.pill, &hover)) {
            c.open = true;
            v.focusPending = View::Focus::Field;
            v.follow = true;
        }
        r.fillRoundedRect(L.pill, ui.s(6) * k, Color{11, 10, 20, static_cast<uint8_t>(hover ? 235 : 170)});
        r.drawRoundedRect(L.pill, ui.s(6) * k, channelColor(c.channel).withAlpha(hover ? 200 : 90), std::max(1.0f, ui.s(1)));
        {
            const std::string t = look.openKey.empty() ? std::string("Chat") : "Chat · " + look.openKey;
            fit(ui, t, {L.pill.x + ui.s(10) * k, L.pill.center().y}, 14.0f * k, L.pill.w - ui.s(20) * k, hover ? kText : kDim, FontFace::Bold);
        }
        v.pill = L.pill;
        ui.popId();
        return sent;
    }
    v.drawn = area;
    // Open: its panel (the game goes on under it: nothing of it shows through its texts).
    r.fillRoundedRect(area, ui.s(8) * k, kPanelFill.withAlphaF(std::max(0.88f, st.opacity)));
    r.drawRoundedRect(area, ui.s(8) * k, channelColor(c.channel).withAlpha(150), std::max(1.0f, ui.s(1.5f)));
    // The row first: a click elsewhere ends the edit of the field before the other buttons read it.
    InputManager& in = ui.input();
    if (v.focusPending == View::Focus::Field) ui.editText("field");
    else if (v.focusPending == View::Focus::Quick) ui.setFocus("quick");
    v.focusPending = View::Focus::None;
    const bool enter = ui.editingText() && ui.focused("field") && in.keyPressed(SDL_SCANCODE_RETURN);
    {
        UIContext::ButtonStyle bs;
        bs.small = true;
        bs.fontSize = 14.0f * k;
        const std::string chan = raidchat::channelLabel(c.channel);
        bs.fontSize = ui.fitFont(chan, FontFace::Bold, 14.0f * k, L.pill.w - ui.s(12));
        if (ui.button("channel", L.pill, chan, bs)) c.channel = raidchat::otherChannel(c.channel);
        r.fillRect({L.pill.x + ui.s(6), L.pill.bottom() - ui.s(3), L.pill.w - ui.s(12), ui.s(3)}, channelColor(c.channel));
    }
    ui.textField("field", L.field, c.draft, placeholder(c.channel), raidchat::kMaxChars * 4);
    if (raidchat::codePoints(c.draft) > raidchat::kMaxChars) {
        // The field counts bytes: cut on a whole character at kMaxChars characters.
        size_t n = 0, i = 0;
        for (; i < c.draft.size() && n < raidchat::kMaxChars; i += charLen(c.draft, i)) ++n;
        c.draft.resize(i);
    }
    if (v.keepEditing) {
        ui.editText("field");
        v.keepEditing = false;
    }
    {
        UIContext::ButtonStyle bs;
        bs.small = true;
        bs.primary = true;
        bs.fontSize = ui.fitFont("Envoyer", FontFace::Bold, 14.0f * k, L.send.w - ui.s(12));
        bs.enabled = c.draft.find_first_not_of(' ') != std::string::npos;
        if ((ui.button("send", L.send, "Envoyer", bs) || enter) && !sent.any) {
            sent.any = true;
            sent.text = c.draft;
        }
    }
    // The title row: the local test's word first (gold), else what the keys do; the count of the characters
    // when the message gets long (measured first, at the right).
    {
        float room = L.header.w;
        const size_t n = raidchat::codePoints(c.draft);
        if (n > raidchat::kMaxChars - 40) {
            const std::string cnt = std::to_string(n) + " / " + std::to_string(raidchat::kMaxChars);
            const float cw = textW(ui, cnt, 13.0f * k, FontFace::Bold) + ui.s(4);
            fit(ui, cnt, {L.header.right() - cw, L.header.center().y}, 13.0f * k, cw, n >= raidchat::kMaxChars ? kOrange : kDim, FontFace::Bold, Align::Left,
                true);
            room = std::max(0.0f, room - cw - ui.s(8));
        }
        if (!look.header.empty()) {
            const float hw = std::min(room, textW(ui, look.header, 13.0f * k, FontFace::Bold) + ui.s(10));
            fit(ui, look.header, {L.header.x, L.header.center().y}, 13.0f * k, hw, kGold, FontFace::Bold);
            room = std::max(0.0f, room - hw);
            if (room > ui.s(60)) fit(ui, look.hints, {L.header.x + hw, L.header.center().y}, 12.0f * k, room, kDim);
        } else {
            fit(ui, look.hints, {L.header.x, L.header.center().y}, 12.0f * k, room, kDim);
        }
    }
    // The history, scrolled: every line wrapped (kept until the log, the width or the language change).
    {
        if (!v.cache) v.cache = std::make_shared<View::Cache>();
        View::Cache& cc = *v.cache;
        const float width = L.history.w - ui.s(14) - ui.s(8) * k;  // the bar's lane at the right
        const float px = ui.s(L.font);
        if (cc.serial != c.log.serial() || std::abs(cc.width - width) > 0.5f || std::abs(cc.px - px) > 0.01f || cc.language != i18n::language() ||
            cc.lines.size() != c.log.lines().size()) {
            cc.serial = c.log.serial();
            cc.width = width;
            cc.px = px;
            cc.language = i18n::language();
            cc.lines.clear();
            cc.height = 0.0f;
            for (const Line& l : c.log.lines()) {
                cc.lines.push_back(wrapLine(ui, l, look, L.font, width));
                cc.height += static_cast<float>(cc.lines.back().size()) * L.lineH;
            }
        }
        const bool grew = v.seen != c.log.serial();
        v.seen = c.log.serial();
        const float content = cc.height + ui.s(4);
        if ((grew && v.follow) || content <= L.history.h) ui.setScroll("history", 1e9f);
        ui.beginScroll("history", L.history, content);
        const float off = ui.scrollOffset();
        if (v.scrollBy != 0.0f) ui.setScroll("history", off + v.scrollBy);
        if (v.scrollBy < 0.0f) v.follow = false;
        v.scrollBy = 0.0f;
        const float maxOff = std::max(0.0f, content - L.history.h);
        if (off >= maxOff - 1.0f) v.follow = true;  // at the newest: it follows them again
        else if (in.wheel() > 0.0f && L.history.contains(in.mousePos())) v.follow = false;
        if (c.log.lines().empty()) {
            fit(ui, "Aucun message pour l'instant.", {L.history.x + ui.s(8) * k, L.history.y + L.lineH * 0.5f}, L.font, L.history.w - ui.s(24), kDim);
        }
        float y = L.history.y + (content < L.history.h ? L.history.h - content : 0.0f) - off;
        {
            i18n::Verbatim done(true);  // already in the language shown
            for (const auto& vis : cc.lines)
                for (const Visual& row : vis) {
                    if (y + L.lineH >= L.history.y && y <= L.history.bottom())
                        for (const Piece& p : row)
                            ui.label(p.text, {L.history.x + ui.s(8) * k + p.x, y + L.lineH * 0.5f}, L.font, p.color, Align::Left, p.face, VAlign::Middle);
                    y += L.lineH;
                }
        }
        ui.endScroll();
    }
    // The quick messages: ◀ ▶ (the mouse; Gauche / Droite on it), the message itself sends it (A, Entrée, a click).
    {
        const auto& qm = raidchat::quickMessages();
        const int n = static_cast<int>(qm.size());
        v.quick = ((v.quick % n) + n) % n;
        bool hover = false;
        const bool focused = ui.focused("quick");
        if (focused && !ui.editingText()) {
            if (in.menuPressed(Action::MenuLeft)) v.quick = (v.quick + n - 1) % n;
            if (in.menuPressed(Action::MenuRight)) v.quick = (v.quick + 1) % n;
            ui.consumeNavigation(true, false);
        }
        bool prevHover = false, nextHover = false;
        if (ui.mouseClick("qprev", L.quickPrev, &prevHover)) v.quick = (v.quick + n - 1) % n;
        if (ui.mouseClick("qnext", L.quickNext, &nextHover)) v.quick = (v.quick + 1) % n;
        if (ui.hotspot("quick", L.quickText, &hover)) {
            sent.any = true;
            sent.quick = true;
            sent.text = i18n::tr(qm[static_cast<size_t>(v.quick)]);  // sent in the language shown
        }
        const bool lit = hover || focused;
        r.fillRoundedRect(L.quickText, ui.s(6) * k, lit ? Color{30, 52, 84, 245} : Color{18, 24, 48, 235});
        if (lit) r.drawRoundedRect(L.quickText, ui.s(6) * k, ui.theme().accent.withAlpha(200), std::max(1.0f, ui.s(1.5f)));
        for (const auto& [rc, t, h] : {std::tuple{L.quickPrev, "◀", prevHover}, std::tuple{L.quickNext, "▶", nextHover}}) {
            r.fillRoundedRect(rc, ui.s(6) * k, h ? Color{30, 52, 84, 245} : Color{18, 24, 48, 235});
            ui.label(t, rc.center(), 13.0f * k, h ? kText : kDim, Align::Center, FontFace::Body, VAlign::Middle);
        }
        const std::string q = std::string(qm[static_cast<size_t>(v.quick)]);
        const std::string label = "Message rapide : " + q;
        fit(ui, label, {L.quickText.center().x, L.quickText.center().y}, 13.0f * k, L.quickText.w - ui.s(12), lit ? kText : kDim, FontFace::Body,
            Align::Center);
    }
    ui.popId();
    return sent;
}

}  // namespace cosmic::raidchatui
