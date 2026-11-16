#include "ui/screens/DownloadedReplaysUi.h"

#include <algorithm>
#include <ctime>
#include <set>

#include "engine/core/Log.h"
#include "game/GameServices.h"
#include "game/ghost/Ghost.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/ReplayCompareNav.h"

namespace cosmic {

namespace {

/// Never below 11.5 pixels on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

/// `t` at `size` (never below 11.5 px, at most 20 % smaller), shortened with « … » if it still
/// does not fit `room`; drawn left-aligned, centred on `y`.
void fitted(UIContext& ui, const std::string& t, float x, float y, float size, float room, Color c, FontFace face) {
    if (room <= ui.s(8)) return;
    float fs = std::max(ui.fitFont(t, face, fz(ui, size), room, 0.8f), fz(ui, 11.5f));
    ui.label(ui.elide(t, face, fs, room), {x, y}, fs, c, Align::Left, face, VAlign::Middle);
}

/// "16/11/2026" of a date in seconds since 1970 (local time).
std::string localDate(int64_t seconds) {
    if (seconds <= 0) return {};
    std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%d/%m/%Y", &tmv);
    return buf;
}

/// "12/11/2026" of an ISO-8601 date from the server ("" when unreadable).
std::string isoDate(const std::string& iso) {
    if (iso.size() < 10 || iso[4] != '-' || iso[7] != '-') return {};
    return iso.substr(8, 2) + "/" + iso.substr(5, 2) + "/" + iso.substr(0, 4);
}

std::string megabytesText(uint64_t bytes) {
    // One decimal under 10 Mo, with a French comma.
    uint64_t tenths = (bytes * 10 + (1u << 19)) >> 20;
    if (tenths < 100) return std::to_string(tenths / 10) + "," + std::to_string(tenths % 10) + " Mo";
    return std::to_string((tenths + 5) / 10) + " Mo";
}

}  // namespace

DownloadedReplaysPanel::~DownloadedReplaysPanel() {
    if (checking_.valid()) checking_.wait();
    if (opening_ && opening_->replay.valid()) opening_->replay.wait();
}

void DownloadedReplaysPanel::say(const std::string& m, bool error) {
    msg_ = m;
    msgError_ = error;
    msgTime_ = g_.ui.time();
    if (error) g_.uiSound("ui_error");
}

void DownloadedReplaysPanel::checkVersions() {
    if (checking_.valid()) return;
    std::vector<std::string> ids;
    for (const auto& e : g_.replayLibrary.entries())
        if (!versions_.count(e.levelId) && std::find(ids.begin(), ids.end(), e.levelId) == ids.end()) ids.push_back(e.levelId);
    if (ids.empty()) return;
    checkingIds_ = ids;
    // Reading the levels and their fingerprints takes a moment: off the main thread.
    checking_ = std::async(std::launch::async, [ids, &content = g_.content] {
        Versions out;
        for (const auto& id : ids) out[id] = installedLevelVersions(content, id);
        return out;
    });
}

void DownloadedReplaysPanel::update() {
    if (checking_.valid() && checking_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        for (auto& [id, v] : checking_.get()) versions_[id] = std::move(v);
        checkingIds_.clear();
    }
    if (opening_ && opening_->replay.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::optional<ReplayData> rep = opening_->replay.get();
        const std::string id = opening_->id, why = *opening_->error;
        const Action a = opening_->action;
        opening_.reset();
        const DownloadedReplay* e = g_.replayLibrary.find(id);
        if (!e) return;  // removed meanwhile
        if (!rep) return say(why.empty() ? std::string("Replay illisible.") : why, true);
        msg_.clear();
        opened(*e, a, std::move(*rep));
    }
}

void DownloadedReplaysPanel::open(const DownloadedReplay& e, Action a) {
    if (opening_) return;
    Opening o;
    o.id = e.id;
    o.action = a;
    o.error = std::make_shared<std::string>();
    // Read and decoded again (the file is never trusted), off the main thread.
    o.replay = std::async(std::launch::async, [file = g_.replayLibrary.pathOf(e), hash = e.levelHash, err = o.error] {
        return ReplayLibrary::loadFile(file, hash, err.get());
    });
    opening_ = std::move(o);
    say("Ouverture du replay…", false);
}

void DownloadedReplaysPanel::opened(const DownloadedReplay& e, Action a, ReplayData rep) {
    rep.levelId = e.levelId;  // the level installed here, noted at download
    Log::info("Replay téléchargé ouvert ({}) : {}", a == Action::Watch ? "lecture" : "comparaison", e.id);
    const std::string who = shownPlayer(e);
    const std::string title = who.empty() ? std::string("Replay téléchargé") : "Replay de " + who;
    if (a == Action::Watch) {
        g_.app.pushScene(makeLocalReplayViewer(g_, std::move(rep), title));
        return;
    }
    // « Comparer »: the comparison of replays of this level, with this one as A.
    ReplayCompareRequest req;
    req.levelId = e.levelId;
    req.current = std::make_shared<const ReplayData>(std::move(rep));
    GhostMeta& m = req.currentMeta;
    m.source = "current";
    m.label = title;
    m.playerId = e.playerId;
    m.levelId = e.levelId;
    m.levelHash = e.levelHash;
    m.category = e.category;
    m.timeMs = e.timeMs;
    m.date = e.achievedAt;
    g_.app.pushScene(makeReplayCompareSetup(g_, std::move(req)));
}

std::string DownloadedReplaysPanel::shownPlayer(const DownloadedReplay& e) const {
    if (e.player.empty()) return {};
    // Hidden in streaming mode, as on the leaderboards; the player's own name stays.
    bool mine = !e.playerId.empty() && e.playerId == g_.online.playerId();
    return mine || g_.settings.online.showPseudonyms ? e.player : std::string("Joueur");
}

std::string DownloadedReplaysPanel::levelName(const DownloadedReplay& e) const {
    if (const LevelEntry* l = g_.content.level(e.levelId)) return l->name;
    return e.levelName.empty() ? e.levelId : e.levelName;
}

void DownloadedReplaysPanel::draw(const Rect& area) {
    UIContext& ui = g_.ui;
    checkVersions();
    const auto& all = g_.replayLibrary.entries();
    ui.pushId("dlrep");

    // ------------------------------------------------------------------ header
    // The level filter at the right: « Tous les niveaux », then each level with its count.
    std::vector<std::string> levelIds;
    std::map<std::string, int> counts;
    for (const auto& e : all) {
        if (!counts[e.levelId]++) levelIds.push_back(e.levelId);
    }
    std::map<std::string, std::string> names;
    for (const auto& id : levelIds)
        for (const auto& e : all)
            if (e.levelId == id) {
                names[id] = levelName(e);
                break;
            }
    std::stable_sort(levelIds.begin(), levelIds.end(), [&](const std::string& a, const std::string& b) { return names[a] < names[b]; });
    if (!filterLevel_.empty() && !counts.count(filterLevel_)) filterLevel_.clear();
    const float headH = ui.s(46);
    const float fw = std::min(ui.s(480), area.w * 0.42f);
    Rect fr{area.right() - fw, area.y, fw, headH};
    float titleRoom = fr.x - ui.s(20) - area.x;
    if (!levelIds.empty()) {
        // The cycle shows its value between « < » and « > »: each name shortened to that room.
        const float valueRoom = (fr.right() - ui.s(44)) - (fr.x + fr.w * 0.45f + ui.s(36)) - ui.s(8);
        std::vector<std::string> options{"Tous les niveaux"};
        int index = 0;
        for (size_t i = 0; i < levelIds.size(); ++i) {
            std::string o = names[levelIds[i]] + " (" + std::to_string(counts[levelIds[i]]) + ")";
            options.push_back(ui.elide(o, FontFace::Bold, 13.0f, valueRoom));
            if (levelIds[i] == filterLevel_) index = static_cast<int>(i) + 1;
        }
        if (ui.cycle("level", fr, "Niveau", index, options)) {
            filterLevel_ = index == 0 ? std::string() : levelIds[static_cast<size_t>(index) - 1];
            confirmDelete_.clear();
        }
    } else {
        titleRoom = area.w;
    }
    Vec2 ts{0, 0};
    {
        const std::string title = "Replays téléchargés";
        float tf = std::max(ui.fitFont(title, FontFace::Bold, 22, titleRoom, 0.7f), fz(ui, 11.5f));
        ts = ui.label(ui.elide(title, FontFace::Bold, tf, titleRoom), {area.x, area.y + headH * 0.5f}, tf, ui.theme().text, Align::Left, FontFace::Bold,
                      VAlign::Middle);
    }
    const std::string summary = std::to_string(all.size()) + (all.size() > 1 ? " replays · " : " replay · ") + megabytesText(g_.replayLibrary.totalBytes()) +
                                " sur " + std::to_string(ReplayLibrary::kMaxTotalBytes >> 20) + " Mo";
    fitted(ui, summary, area.x + ts.x + ui.s(16), area.y + headH * 0.5f, 15, titleRoom - ts.x - ui.s(16), ui.theme().textDim, FontFace::Body);
    float y = area.y + headH + ui.s(12);
    const std::string info = "Gardés sur cet ordinateur : ils se regardent sans connexion et sans compte. On les télécharge depuis les classements "
                             "en ligne, les défis et les courses à la vérification (« Télécharger » ou la flèche à côté de « Voir »).";
    y += ui.labelWrapped(info, {area.x, y}, area.w, fz(ui, 15), ui.theme().textDim).y + ui.s(14);

    // -------------------------------------------------------------------- list
    const float footH = ui.s(40);
    Rect list{area.x, y, area.w, area.bottom() - footH - ui.s(8) - y};
    std::vector<const DownloadedReplay*> shown;
    for (const auto& e : all)
        if (filterLevel_.empty() || e.levelId == filterLevel_) shown.push_back(&e);
    // The buttons: one width for the three, the widest label's.
    const float bfs = fz(ui, 17);
    float bw = 0.0f;
    for (const char* l : {"Regarder", "Comparer", "Supprimer", "Confirmer", "Ouverture…"})
        bw = std::max(bw, ui.text().measure(l, FontFace::Bold, ui.s(bfs)).x);
    bw += ui.s(36);
    if (shown.empty()) {
        ui.labelWrapped("Aucun replay téléchargé pour l'instant. Sur un classement en ligne, « Télécharger » (ou la flèche à côté de « Voir ») "
                        "garde un replay sur ce PC pour le regarder plus tard, même sans connexion.",
                        {list.x, list.y + ui.s(10)}, std::min(list.w, ui.s(900)), fz(ui, 18), ui.theme().textDim);
    } else {
        const float rh = ui.s(92), gap = ui.s(8);
        ui.beginScroll("list", list, static_cast<float>(shown.size()) * (rh + gap));
        float cy = list.y - ui.scrollOffset();
        for (const DownloadedReplay* e : shown) {
            // The 14 px at the right stay free for the scroll bar. Every row's buttons are
            // called (the keyboard and the gamepad reach them, the list follows the focus).
            Rect row{list.x, cy, list.w - ui.s(14), rh};
            drawRow(*e, row, bw, bfs, row.bottom() >= list.y && row.y <= list.bottom());
            cy += rh + gap;
        }
        ui.endScroll();
    }
    if (!deleteNow_.empty()) {
        std::string why, name;
        if (const DownloadedReplay* e = g_.replayLibrary.find(deleteNow_)) name = levelName(*e);
        if (g_.replayLibrary.remove(deleteNow_, &why)) say(name.empty() ? std::string("Replay supprimé de ce PC.") : "Replay supprimé de ce PC (" + name + ").", false);
        else say(why, true);
        deleteNow_.clear();
    }

    // -------------------------------------------------------------------- foot
    std::string foot = "Au plus " + std::to_string(ReplayLibrary::kMaxCount) + " replays et " + std::to_string(ReplayLibrary::kMaxTotalBytes >> 20) +
                       " Mo sur ce PC ; un replay n'est jamais supprimé sans vous.";
    Color fc = ui.theme().textDim;
    if (!confirmDelete_.empty()) {
        foot = "Appuyez sur « Confirmer » pour supprimer ce replay de ce PC (il reste en ligne s'il y est encore).";
        fc = ui.theme().warm;
    } else if (!msg_.empty() && (opening_ || ui.time() - msgTime_ < 6.0f)) {
        foot = msg_;
        fc = msgError_ ? ui.theme().danger : ui.theme().success;
    }
    fitted(ui, foot, area.x, area.bottom() - footH * 0.5f, 16, area.w, fc, FontFace::Body);
    ui.popId();
}

void DownloadedReplaysPanel::drawRow(const DownloadedReplay& e, const Rect& row, float bw, float fs, bool visible) {
    UIContext& ui = g_.ui;
    Renderer& r = ui.r();
    if (visible) r.fillRoundedRect(row, ui.s(10), Color{14, 20, 42, 220});
    // What the level installed here says of this replay.
    auto v = versions_.find(e.levelId);
    const bool known = v != versions_.end();
    const ReplayLevelMatch match = known ? matchReplayLevel(e.levelHash, v->second.has_value(), v->second.value_or(std::vector<std::string>{}))
                                         : ReplayLevelMatch::OtherRevision;
    // Right to left: « Supprimer », « Comparer », « Regarder ».
    const float bh = ui.s(44), by = row.center().y - bh * 0.5f, bgap = ui.s(10);
    Rect del{row.right() - ui.s(14) - bw, by, bw, bh};
    Rect cmp{del.x - bgap - bw, by, bw, bh};
    Rect watch{cmp.x - bgap - bw, by, bw, bh};
    UIContext::ButtonStyle st;
    st.small = true;
    st.fontSize = fs;
    const bool busy = opening_ && opening_->id == e.id;
    st.primary = true;
    st.enabled = known && match == ReplayLevelMatch::Same && !opening_;
    if (ui.button("watch/" + e.id, watch, busy && opening_->action == Action::Watch ? "Ouverture…" : "Regarder", st)) open(e, Action::Watch);
    st.primary = false;
    st.enabled = known && match != ReplayLevelMatch::NotInstalled && !opening_;
    if (ui.button("cmp/" + e.id, cmp, busy && opening_->action == Action::Compare ? "Ouverture…" : "Comparer", st)) open(e, Action::Compare);
    st.enabled = !busy;
    st.danger = true;
    const std::string delName = "del/" + e.id;
    const bool confirming = confirmDelete_ == e.id;
    if (ui.button(delName, del, confirming ? "Confirmer" : "Supprimer", st)) {
        if (!confirming) {
            confirmDelete_ = e.id;
        } else {
            // Removed once the list is drawn (the rows point into the library).
            confirmDelete_.clear();
            deleteNow_ = e.id;
        }
    }
    // The confirmation waits only while the button keeps the focus or the pointer.
    if (confirming && !ui.focused(delName) && !ui.mouseOver(del)) confirmDelete_.clear();
    if (!visible) return;

    // The text, in what the buttons leave.
    const float tx = row.x + ui.s(18), room = watch.x - ui.s(20) - tx;
    if (room < ui.s(60)) return;
    // 1: the level, and the time at the right.
    const std::string time = e.timeMs > 0 ? formatRunTime(e.timeMs) : std::string();
    float timeW = time.empty() ? 0.0f : ui.text().measure(time, FontFace::Bold, ui.s(19)).x;
    if (!time.empty() && timeW < room * 0.4f) {
        ui.label(time, {tx + room, row.y + ui.s(22)}, 19, ui.theme().accent, Align::Right, FontFace::Bold, VAlign::Middle);
        timeW += ui.s(16);
    } else {
        timeW = 0.0f;
    }
    fitted(ui, levelName(e), tx, row.y + ui.s(22), 19, room - timeW, ui.theme().text, FontFace::Bold);
    // 2: what it is and whose.
    const std::string who = shownPlayer(e);
    std::string what = e.category;
    if (!who.empty()) what = e.category.empty() ? "par " + who : e.category + "  ·  par " + who;
    fitted(ui, what, tx, row.y + ui.s(46), 15, room, ui.theme().text, FontFace::Body);
    // 3: the dates, or why it cannot be watched.
    std::string line;
    Color lc = ui.theme().textDim;
    if (!known) {
        line = "Vérification de la version du niveau…";
    } else if (match == ReplayLevelMatch::NotInstalled) {
        line = "Niveau absent de ce jeu : installez-le pour regarder ce replay.";
        lc = ui.theme().warm;
    } else if (match == ReplayLevelMatch::OtherRevision) {
        line = "Ce replay a été fait sur une autre version du niveau : il ne peut pas être rejoué tel quel.";
        lc = ui.theme().warm;
    } else {
        const std::string made = isoDate(e.achievedAt), got = localDate(e.downloadedAt);
        if (!made.empty() && !got.empty()) line = "Réalisé le " + made + "  ·  téléchargé le " + got;
        else if (!made.empty()) line = "Réalisé le " + made;
        else if (!got.empty()) line = "Téléchargé le " + got;
    }
    fitted(ui, line, tx, row.y + ui.s(70), 14, room, lc, FontFace::Body);
}

}  // namespace cosmic
