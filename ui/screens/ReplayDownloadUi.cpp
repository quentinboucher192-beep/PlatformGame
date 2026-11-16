#include "ui/screens/ReplayDownloadUi.h"

#include <algorithm>
#include <cmath>

#include "game/GameServices.h"
#include "ui/screens/Navigation.h"

namespace cosmic::ReplayDownloadUi {

namespace {

/// Collection › Replays (the list of the downloaded replays).
constexpr int kCollectionReplaysTab = 4;

/// The tooltip asked this frame (the icon hovered or focused).
struct Tip {
    std::string text;
    Rect anchor;
    float stamp = -1.0f;
};
Tip& tip() {
    static Tip t;
    return t;
}

const char* labelOf(ReplayDownloads::State s) {
    switch (s) {
        case ReplayDownloads::State::Busy: return "Téléchargement…";
        case ReplayDownloads::State::Done: return "Téléchargé";
        default: return "Télécharger";
    }
}

/// Starts the download, or (already kept) opens the list where it is.
void activate(GameServices& g, ReplayDownloads::State s, const ReplayDownloadRequest& req) {
    if (s == ReplayDownloads::State::None) g.replayDownloads.start(req);
    else if (s == ReplayDownloads::State::Done) g.app.pushScene(makeCollection(g, kCollectionReplaysTab));
}

}  // namespace

ReplayDownloadRequest request(const std::string& source, const std::string& remoteId, const std::string& path, const std::string& player,
                              const std::string& shownName, const std::string& levelId, const std::string& levelName,
                              const std::string& category, int64_t timeMs, const std::string& achievedAt) {
    ReplayDownloadRequest r;
    r.path = path;
    r.meta.id = ReplayLibrary::makeId(source, remoteId);
    r.meta.source = source;
    r.meta.player = player;
    r.meta.levelId = levelId;
    r.meta.levelName = levelName;
    r.meta.category = category;
    r.meta.timeMs = timeMs;
    r.meta.achievedAt = achievedAt;
    r.title = shownName.empty() ? std::string("Le replay") : "Le replay de " + shownName;
    if (!levelName.empty()) r.title += " (" + levelName + ")";
    return r;
}

const char* longestLabel() { return "Téléchargement…"; }

void button(GameServices& g, const std::string& name, const Rect& r, const ReplayDownloadRequest& req, UIContext::ButtonStyle st) {
    const ReplayDownloads::State s = g.replayDownloads.state(req.meta.id);
    st.enabled = st.enabled && !req.meta.id.empty() && s != ReplayDownloads::State::Busy;
    st.selected = st.selected || s == ReplayDownloads::State::Done;
    if (g.ui.button(name, r, labelOf(s), st)) activate(g, s, req);
}

void icon(GameServices& g, const std::string& name, const Rect& r, const ReplayDownloadRequest& req, bool enabled) {
    UIContext& ui = g.ui;
    const ReplayDownloads::State s = g.replayDownloads.state(req.meta.id);
    UIContext::ButtonStyle st;
    st.small = true;
    st.enabled = enabled && !req.meta.id.empty();
    st.selected = s == ReplayDownloads::State::Done;
    if (ui.button(name, r, "", st)) activate(g, s, req);
    Renderer& rd = ui.r();
    const Vec2 c = r.center();
    const float u = std::min(r.w, r.h) / 28.0f;  // the glyph follows the button's size
    const Color col = !st.enabled ? ui.theme().textDim : s == ReplayDownloads::State::Done ? ui.theme().success : ui.theme().accent;
    if (s == ReplayDownloads::State::Busy) {
        // Three dots, one lit after the other.
        int lit = static_cast<int>(std::fmod(ui.time() * 3.0f, 3.0f));
        for (int i = 0; i < 3; ++i) rd.fillCircle({c.x + (static_cast<float>(i) - 1.0f) * 6.0f * u, c.y}, 2.2f * u, col.withAlphaF(i == lit ? 1.0f : 0.35f));
    } else if (s == ReplayDownloads::State::Done) {
        rd.drawLine({c.x - 6.5f * u, c.y + 0.5f * u}, {c.x - 2.0f * u, c.y + 5.0f * u}, col, std::max(1.5f, 2.4f * u));
        rd.drawLine({c.x - 2.0f * u, c.y + 5.0f * u}, {c.x + 7.0f * u, c.y - 5.5f * u}, col, std::max(1.5f, 2.4f * u));
    } else {
        // An arrow down onto a tray.
        rd.fillRect({c.x - 1.3f * u, c.y - 8.0f * u, 2.6f * u, 7.5f * u}, col);
        rd.fillTriangle({c.x - 5.5f * u, c.y - 1.0f * u}, {c.x + 5.5f * u, c.y - 1.0f * u}, {c.x, c.y + 4.5f * u}, col);
        rd.fillRect({c.x - 7.0f * u, c.y + 6.5f * u, 14.0f * u, 2.0f * u}, col);
    }
    if (ui.mouseOver(r) || ui.focused(name)) {
        Tip& t = tip();
        t.text = s == ReplayDownloads::State::Done   ? "Téléchargé : à regarder sans connexion dans Collection › Replays"
                 : s == ReplayDownloads::State::Busy ? "Téléchargement du replay…"
                                                     : "Télécharger ce replay pour le regarder sans connexion";
        t.anchor = r;
        t.stamp = ui.time();
    }
}

void drawTip(GameServices& g) {
    UIContext& ui = g.ui;
    Tip& t = tip();
    if (t.stamp != ui.time() || t.text.empty()) return;
    t.stamp = -1.0f;
    const float W = ui.screenW(), H = ui.screenH();
    const float pad = ui.s(12), fs = std::max(15.0f, 11.5f / ui.s(1.0f));
    // As wide as its text, never wider than the screen: it wraps then.
    const float inner = std::min(ui.text().measure(t.text, FontFace::Body, ui.s(fs)).x + ui.s(2), std::min(ui.s(420), W - ui.s(16) - 2 * pad));
    const float textH = ui.text().measureWrapped(t.text, FontFace::Body, ui.s(fs), inner).y;
    const float w = inner + 2 * pad, h = textH + 2 * pad;
    // Under the icon when there is room, else above it; always on screen.
    Rect box{t.anchor.center().x - w * 0.5f, t.anchor.bottom() + ui.s(8), w, h};
    if (box.bottom() > H - ui.s(8)) box.y = t.anchor.y - ui.s(8) - h;
    box.y = std::clamp(box.y, ui.s(8), std::max(ui.s(8), H - h - ui.s(8)));
    box.x = std::clamp(box.x, ui.s(8), std::max(ui.s(8), W - w - ui.s(8)));
    ui.r().fillRoundedRect(box, ui.s(10), Color{10, 15, 32, 250});
    ui.r().drawRoundedRect(box, ui.s(10), ui.theme().accent.withAlphaF(0.8f), std::max(1.0f, ui.s(1.4f)));
    ui.labelWrapped(t.text, {box.x + pad, box.y + pad}, inner, fs, ui.theme().text);
}

std::optional<ReplayData> localCopy(GameServices& g, const std::string& id, std::string* error) {
    if (id.empty() || !g.replayLibrary.has(id)) return std::nullopt;
    return g.replayLibrary.load(id, error);
}

}  // namespace cosmic::ReplayDownloadUi
