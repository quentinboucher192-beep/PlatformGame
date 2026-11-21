#include "ui/screens/GiftsUi.h"

#include <algorithm>
#include <cmath>

#include "engine/core/I18n.h"
#include "ui/online/RaidWidgets.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::giftsui {

namespace {

/// `t` at `size` when it fits `room`, else smaller (down to `minScale`), then shortened; a player's words verbatim.
Text fitText(UIContext& ui, const std::string& t, FontFace face, float size, float room, float minScale = 0.75f, bool verbatim = false) {
    i18n::Verbatim v(verbatim);
    Text out;
    out.size = ui.fitFont(t, face, size, std::max(1.0f, room), minScale);
    out.shown = ui.elide(t, face, out.size, std::max(1.0f, room));
    return out;
}

float width(UIContext& ui, const Text& t, FontFace face, bool verbatim = false) {
    i18n::Verbatim v(verbatim);
    return ui.text().measure(t.shown, face, ui.s(t.size)).x;
}

/// Buttons side by side, centred in [x, x + w], each as wide as its label needs (one size for all, smaller when
/// they would not fit), `h` high from `y`.
std::vector<Rect> buttonRow(UIContext& ui, const std::vector<std::string>& labels, float x, float w, float y, float h, float size, float gap,
                            float* sizeOut) {
    std::vector<Rect> out;
    if (labels.empty()) return out;
    const float pad = ui.s(44);
    const float n = static_cast<float>(labels.size());
    const float share = (w - gap * (n - 1.0f)) / n;
    auto rowW = [&](float f) {
        float sum = gap * (n - 1.0f);
        for (const auto& l : labels) sum += ui.text().measure(l, FontFace::Bold, ui.s(f)).x + pad;
        return sum;
    };
    // One size for every button: the largest at which each fits an equal share of the row.
    float fs = size;
    if (rowW(fs) > w)
        for (const auto& l : labels) fs = std::min(fs, ui.fitFont(l, FontFace::Bold, size, share - pad, 0.6f));
    std::vector<float> ws;
    float total = gap * (n - 1.0f);
    for (const auto& l : labels) {
        ws.push_back(ui.text().measure(l, FontFace::Bold, ui.s(fs)).x + pad);
        total += ws.back();
    }
    if (total > w) {  // even at the smallest size: equal shares (the labels shortened by the buttons)
        for (float& v : ws) v = share;
        total = w;
    }
    float bx = x + (w - total) * 0.5f;
    for (float bw : ws) {
        out.push_back({bx, y, bw, h});
        bx += bw + gap;
    }
    *sizeOut = fs;
    return out;
}

}  // namespace

// ------------------------------------------------------------ COSMIC ONLINE › CADEAUX

ScreenLayout screenLayout(UIContext& ui) {
    ScreenLayout L;
    const float W = ui.screenW(), H = ui.screenH(), m = ui.s(22);
    L.header = {0, 0, W, ui.s(64)};
    L.nav = {m, L.header.bottom() + ui.s(8), ui.s(272), H - L.header.bottom() - ui.s(8) - m};
    const float bottom = H - m - ui.s(40);
    L.detail = {W - m - ui.s(620), L.nav.y, ui.s(620), bottom - L.nav.y};
    const float lx = L.nav.right() + ui.s(18);
    L.list = {lx, L.nav.y, L.detail.x - ui.s(18) - lx, bottom - L.nav.y};
    L.hint = {L.list.x, bottom + ui.s(4), L.detail.right() - L.list.x, ui.s(36)};
    return L;
}

ListHead listHead(UIContext& ui, const Rect& list, const std::string& count) {
    ListHead L;
    const float pad = ui.s(18), x = list.x + pad, w = list.w - 2 * pad, cy = list.y + pad + ui.s(16);
    L.title = fitText(ui, "MES CADEAUX", FontFace::Bold, 22, w * 0.6f);
    const float tw = width(ui, L.title, FontFace::Bold);
    L.title.box = {x, cy - ui.s(16), tw, ui.s(32)};
    const float room = w - tw - ui.s(18);
    L.count = fitText(ui, count, FontFace::Body, 17, room);
    const float cw = width(ui, L.count, FontFace::Body);
    L.count.box = {x + w - cw, cy - ui.s(14), cw, ui.s(28)};
    L.tabs = {x, cy + ui.s(26), std::min(w, ui.s(560)), ui.s(44)};
    L.rows = {x, L.tabs.bottom() + ui.s(14), w, list.bottom() - pad - (L.tabs.bottom() + ui.s(14))};
    return L;
}

RowLayout rowLayout(UIContext& ui, const Rect& row, const std::string& title, const std::string& sub, const std::string& status, bool verbatimTitle) {
    RowLayout L;
    const float isz = std::min(ui.s(76), row.h - ui.s(12));
    L.icon = {row.x + ui.s(10), row.center().y - isz * 0.5f, isz, isz};
    L.statusText = fitText(ui, status, FontFace::Bold, 15, ui.s(150));
    const float sw = width(ui, L.statusText, FontFace::Bold) + ui.s(22);
    L.status = {row.right() - ui.s(16) - sw, row.center().y - ui.s(15), sw, ui.s(30)};
    L.statusText.box = L.status;
    const float x = L.icon.right() + ui.s(16), room = L.status.x - ui.s(14) - x;
    L.title = fitText(ui, title, FontFace::Bold, 21, room, 0.75f, verbatimTitle);
    L.title.box = {x, row.y + row.h * 0.36f - ui.s(14), room, ui.s(28)};
    L.sub = fitText(ui, sub, FontFace::Body, 16, room);
    L.sub.box = {x, row.y + row.h * 0.68f - ui.s(12), room, ui.s(24)};
    return L;
}

DetailLayout detailLayout(UIContext& ui, const Rect& panel, const DetailTexts& t) {
    DetailLayout L;
    const float pad = ui.s(26), x = panel.x + pad, w = panel.w - 2 * pad;
    float y = panel.y + pad;
    L.kind = fitText(ui, t.kind, FontFace::Bold, 16, w);
    L.kind.box = {x, y, w, ui.s(24)};
    y += ui.s(30);
    // The buttons at the bottom, the note above them; the art takes what the texts leave.
    const float bh = ui.s(52);
    const float by = panel.bottom() - pad - bh;
    L.actions = buttonRow(ui, t.actions, x, w, by, bh, 20, ui.s(14), &L.actionSize);
    float bottom = t.actions.empty() ? panel.bottom() - pad : by - ui.s(12);
    if (!t.note.empty()) {
        L.note = fitText(ui, t.note, FontFace::Body, 15, w, 0.7f);
        L.note.box = {x, bottom - ui.s(24), w, ui.s(24)};
        bottom = L.note.box.y - ui.s(8);
    }
    // The message: up to four lines, smaller rather than cut (the admin's words, never translated).
    for (float sz : {19.0f, 17.0f, 15.0f}) {
        L.messageSize = sz;
        L.messageLines = raidui::wrap(ui, t.message, FontFace::Body, sz, w - ui.s(32), 5, true);
        if (L.messageLines.size() <= 4) break;
    }
    if (L.messageLines.size() > 4) L.messageLines = raidui::wrap(ui, t.message, FontFace::Body, L.messageSize, w - ui.s(32), 4, true);
    L.messageLineH = ui.s(L.messageSize + 7);
    const float msgH = std::max(ui.s(60), static_cast<float>(std::max<size_t>(1, L.messageLines.size())) * L.messageLineH + ui.s(22));
    L.message = {x, bottom - msgH, w, msgH};
    bottom = L.message.y - ui.s(14);
    L.from = fitText(ui, t.from, FontFace::Body, 19, w, 0.7f);
    L.from.box = {x, bottom - ui.s(26), w, ui.s(26)};
    bottom = L.from.box.y - ui.s(8);
    L.name = fitText(ui, t.name, FontFace::Bold, 32, w, 0.6f, t.verbatimName);
    L.name.box = {x, bottom - ui.s(44), w, ui.s(44)};
    bottom = L.name.box.y - ui.s(8);
    L.art = {x, y, w, std::max(0.0f, bottom - y)};
    return L;
}

ContentRowLayout contentRowLayout(UIContext& ui, const Rect& row, const std::string& name, const std::string& kind, const std::string& equip) {
    ContentRowLayout L;
    const float isz = std::min(ui.s(54), row.h - ui.s(10));
    L.icon = {row.x + ui.s(8), row.center().y - isz * 0.5f, isz, isz};
    // « Équiper » at the right, as wide as its label needs (at most 42 % of the row, its label smaller then).
    const float pad = ui.s(30), maxW = row.w * 0.42f;
    L.equipSize = ui.fitFont(equip, FontFace::Bold, 16, maxW - pad, 0.65f);
    const float ew = std::min(maxW, ui.text().measure(equip, FontFace::Bold, ui.s(L.equipSize)).x + pad);
    L.equip = {row.right() - ui.s(10) - ew, row.center().y - ui.s(19), ew, ui.s(38)};
    const float x = L.icon.right() + ui.s(12), room = L.equip.x - ui.s(12) - x;
    L.name = fitText(ui, name, FontFace::Bold, 18, room, 0.7f);
    L.name.box = {x, row.y + row.h * 0.34f - ui.s(12), room, ui.s(24)};
    L.kind = fitText(ui, kind, FontFace::Body, 14, room, 0.75f);
    L.kind.box = {x, row.y + row.h * 0.72f - ui.s(10), room, ui.s(20)};
    return L;
}

// ------------------------------------------------------------ the opening

RevealLayout revealLayout(UIContext& ui, const RevealTexts& t) {
    RevealLayout L;
    const float W = ui.screenW(), H = ui.screenH();
    L.scale = std::min(W / 1280.0f, H / 720.0f);
    L.origin = {(W - 1280.0f * L.scale) * 0.5f, (H - 720.0f * L.scale) * 0.5f};
    L.boxBase = L.origin.y + 452.0f * L.scale;
    L.itemY = L.origin.y + 250.0f * L.scale;
    const float colW = std::min(W - ui.s(120), ui.s(1650));
    const float x = (W - colW) * 0.5f;
    L.hint = {x, H - ui.s(54), colW, ui.s(40)};
    L.kind = fitText(ui, t.kind, FontFace::Bold, 21, colW);
    L.name = fitText(ui, t.name, FontFace::Bold, 54, colW, 0.5f);
    L.from = fitText(ui, t.from, FontFace::Body, 23, colW, 0.7f);
    const float msgW = std::min(colW, ui.s(1140));
    for (float sz : {24.0f, 21.0f, 18.0f}) {
        L.messageSize = sz;
        L.messageLines = raidui::wrap(ui, t.message, FontFace::Body, sz, msgW, 3, true);
        if (L.messageLines.size() <= 2) break;
    }
    if (L.messageLines.size() > 2) L.messageLines = raidui::wrap(ui, t.message, FontFace::Body, L.messageSize, msgW, 2, true);
    L.messageLineH = ui.s(L.messageSize + 8);
    const float kindH = ui.s(30), nameH = ui.s(L.name.size + 10), fromH = ui.s(32), gap = ui.s(8), bh = ui.s(60);
    const float msgH = static_cast<float>(L.messageLines.size()) * L.messageLineH;
    const float total = kindH + gap + nameH + gap + fromH + (msgH > 0 ? gap + msgH : 0.0f) + ui.s(18) + bh;
    // Under the item (the mockup's 456 of 720), higher when the texts need it, never over the keys.
    float y = std::min(L.origin.y + 456.0f * L.scale + ui.s(16), L.hint.y - ui.s(12) - total);
    L.kind.box = {x, y, colW, kindH};
    y += kindH + gap;
    L.name.box = {x, y, colW, nameH};
    y += nameH + gap;
    L.from.box = {x, y, colW, fromH};
    y += fromH;
    if (msgH > 0) {
        y += gap;
        L.messageY = y;
        y += msgH;
    }
    y += ui.s(18);
    L.buttons = buttonRow(ui, t.buttons, x, colW, y, bh, 22, ui.s(18), &L.buttonSize);
    return L;
}

// ------------------------------------------------------------ ADMINISTRATION › Cadeaux

AdminLayout adminLayout(UIContext& ui, const Rect& inner) {
    AdminLayout L;
    const float gap = ui.s(16);
    const float w1 = std::floor(inner.w * 0.35f), w2 = std::floor(inner.w * 0.31f);
    L.rewards = {inner.x, inner.y, w1, inner.h};
    L.players = {L.rewards.right() + gap, inner.y, w2, inner.h};
    L.send = {L.players.right() + gap, inner.y, inner.right() - (L.players.right() + gap), inner.h};
    L.itemColumns = L.rewards.w >= ui.s(560) ? 4 : 3;
    L.wrapColumns = 6;
    L.itemH = ui.s(118);
    L.wrapH = ui.s(84);
    L.playerH = ui.s(48);
    L.historyH = ui.s(58);
    return L;
}

}  // namespace cosmic::giftsui
