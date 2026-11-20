// 2.78.1: the « fiche récapitulative » of a finished raid of the clan (ui/screens/RaidRecapScene.h).

#include "ui/screens/RaidRecapScene.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "engine/animation/Animation.h"
#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/achievements/Achievements.h"
#include "game/content/ContentDatabase.h"
#include "game/cosmetics/Collection.h"
#include "game/online/AsterionBody.h"
#include "game/online/Clans.h"
#include "game/online/Raids.h"
#include "game/raid/RaidSheets.h"
#include "ui/online/ClanWidgets.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/AsterionFilm.h"
#include "ui/screens/ChallengeRewardsUi.h"
#include "ui/screens/CreatureArt.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/ProfileBadgesUi.h"
#include "ui/widgets/AsterionFigure.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

namespace {

using raidui::kBorder;
using raidui::kDim;
using raidui::kGold;
using raidui::kGreen;
using raidui::kSky;
using raidui::kText;

const Color kPage{14, 19, 44, 240};
const Color kPanel{18, 26, 54, 245};
const Color kPanel2{24, 34, 68, 250};
const Color kLine{44, 58, 104, 255};
const Color kHeart{255, 96, 120, 255};
const Color kCyan{110, 200, 255, 255};
const Color kGrey{200, 206, 230, 255};
const Color kMuted{150, 160, 196, 255};

Color toneColor(int tone) {
    switch (tone) {
        case 0: return kGreen;
        case 1: return kHeart;
        case 2: return kCyan;
        case 3: return kGold;
        default: return kGrey;
    }
}

/// Never below 11.5 pixels on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

/// `t` fitted to `r` (smaller down to `minScale`, never under 11.5 px, then shortened).
RecapText fitted(UIContext& ui, const std::string& t, const Rect& r, FontFace face, float size, float minScale = 0.75f, Align a = Align::Left,
                 bool verbatim = false) {
    RecapText out;
    out.r = r;
    out.face = face;
    out.align = a;
    out.verbatim = verbatim;
    out.size = fz(ui, size);
    out.px = ui.s(out.size);
    if (t.empty() || r.w < ui.s(8)) return out;
    i18n::Verbatim v(verbatim);
    out.size = std::max(ui.fitFont(t, face, fz(ui, size), r.w, minScale), 11.5f / ui.s(1.0f));
    out.text = ui.elide(t, face, out.size, r.w);
    out.px = ui.s(out.size);
    return out;
}

/// Pieces as shown: the game's words translated, the others as they are; a text the game's words alone make stays
/// French until it is drawn (fitted() translates it).
RecapText fittedPieces(UIContext& ui, const raids::Pieces& p, const Rect& r, FontFace face, float size, float minScale = 0.75f, Align a = Align::Left) {
    bool raw = false;
    for (const raids::Piece& x : p) raw = raw || x.verbatim;
    if (!raw) return fitted(ui, raids::joined(p), r, face, size, minScale, a);
    std::string shown;
    for (const raids::Piece& x : p) shown += x.verbatim ? x.text : i18n::tr(x.text);
    return fitted(ui, shown, r, face, size, minScale, a, true);
}

float measureW(UIContext& ui, const std::string& t, FontFace face, float size) { return ui.text().measure(t, face, ui.s(fz(ui, size))).x; }

/// The name of a part as the recap shows it (translated where drawn).
std::string partName(const std::string& part) {
    static const std::map<std::string, std::string> k = {{"crane", "Crâne"},         {"bras_gauche", "Bras gauche"},   {"bras_droit", "Bras droit"},
                                                          {"torse", "Torse"},         {"abdomen", "Abdomen"},           {"jambe_gauche", "Jambe gauche"},
                                                          {"jambe_droite", "Jambe droite"}, {"coeur", "Cœur"}};
    auto it = k.find(part);
    return it == k.end() ? part : it->second;
}

std::vector<std::pair<std::string, std::string>> partNames() {
    std::vector<std::pair<std::string, std::string>> out;
    for (const char* p : {"crane", "bras_gauche", "bras_droit", "torse", "abdomen", "jambe_gauche", "jambe_droite", "coeur"}) out.push_back({p, partName(p)});
    return out;
}

/// The tiles of the clan's rewards: each reward, the badges of the gigaboss together at the end (-1).
std::vector<int> rewardTiles(const raids::Recap& r) {
    std::vector<int> out;
    bool giga = false;
    for (size_t i = 0; i < r.rewards.size(); ++i) {
        if (r.rewards[i].gigaboss) giga = true;
        else out.push_back(static_cast<int>(i));
    }
    if (giga) out.push_back(-1);
    return out;
}

/// « Badge », « Titre de clan », « Combinaison »… of a reward.
std::string rewardKind(const raids::RecapReward& w) {
    if (w.kind == "badge") return "Badge";
    if (w.name.rfind("Titre de clan", 0) == 0) return "Titre de clan";
    if (w.ref.rfind("title_", 0) == 0) return "Titre";
    if (w.ref.rfind("suit_", 0) == 0) return "Combinaison";
    if (w.ref.rfind("frame_", 0) == 0) return "Cadre de profil";
    if (w.ref.rfind("victory_", 0) == 0) return "Effet de victoire";
    return "Récompense";
}

/// The name of a reward between its quotes (« Cœur d'Asterion »), else whole; the silver suit says so.
std::string rewardShortName(const raids::RecapReward& w) {
    const std::string open = "« ", close = " »";
    const size_t a = w.name.find(open), b = w.name.rfind(close);
    if (a == std::string::npos || b == std::string::npos || b <= a) return w.name;
    std::string inner = w.name.substr(a + open.size(), b - a - open.size());
    if (w.name.size() > b + close.size()) inner += w.name.substr(b + close.size());  // « … » argentée
    return inner;
}

std::string playersText(int n) { return std::to_string(n) + (n > 1 ? " joueurs" : " joueur"); }

}  // namespace

std::vector<const RecapText*> RaidRecapLayout::texts() const {
    std::vector<const RecapText*> out = {&crumb, &listNote, &title, &eventLine, &clanLine, &pending, &timelineTitle, &timelineSub, &bestTitle, &bestPending,
                                         &bestEmpty, &rewardsTitle, &rewardsPage, &rewardsNote, &rewardsEmpty, &meTitle, &mePending, &hint};
    for (const Row& r : rows)
        if (r.r.bottom() <= list.bottom() + 0.5f) {
            out.push_back(&r.title);
            out.push_back(&r.line);
        }
    for (const RecapText& t : titanCaption) out.push_back(&t);
    for (const Card& c : cards) {
        out.push_back(&c.value);
        out.push_back(&c.caption);
    }
    for (const TRow& r : trows) {
        out.push_back(&r.name);
        out.push_back(&r.boss);
        out.push_back(&r.label);
        out.push_back(&r.label2);
    }
    for (const RecapText& t : ticks) out.push_back(&t);
    for (const BRow& r : brows) {
        out.push_back(&r.label);
        out.push_back(&r.name);
        out.push_back(&r.value);
    }
    for (const Tile& t : tiles) {
        out.push_back(&t.kind);
        out.push_back(&t.name);
        out.push_back(&t.count);
    }
    for (const MLine& l : mlines) {
        out.push_back(&l.label);
        out.push_back(&l.value);
    }
    for (const RecapText& t : meNote) out.push_back(&t);
    for (const Button& b : buttons) out.push_back(&b.text);
    std::vector<const RecapText*> shown;
    for (const RecapText* t : out)
        if (!t->text.empty()) shown.push_back(t);
    return shown;
}

RaidRecapLayout layoutRaidRecap(UIContext& ui, const raids::Recap& rc, const std::vector<raids::RecapEntry>& list, const RaidRecapOptions& opt) {
    RaidRecapLayout L;
    const float W = ui.screenW(), H = ui.screenH(), m = ui.s(24);
    L.screen = {0.0f, 0.0f, W, H};
    // The top: « ‹ Retour », then where we are.
    L.back = {m, ui.s(16), measureW(ui, "‹ Retour", FontFace::Bold, 17) + ui.s(36), ui.s(42)};
    const float cx = L.back.right() + ui.s(16);
    L.crumb = fitted(ui, "CLANS › RAIDS › RAIDS TERMINÉS", {cx, ui.s(16), W - m - cx, ui.s(42)}, FontFace::Bold, 17);
    const float top = ui.s(72);
    const float btnH = ui.s(46), btnY = H - ui.s(16) - btnH;

    // ------------------------------------------------------------ the left column
    const float lw = std::min(ui.s(350), W * 0.24f);
    const float colBottom = H - ui.s(16);
    const float cardH = std::min(ui.s(344), (colBottom - top) * 0.46f);
    L.titanCard = {m, colBottom - cardH, lw, cardH};
    const float capH = ui.s(24), capY = L.titanCard.bottom() - ui.s(12) - 2.0f * capH - ui.s(4);
    L.titanBox = {L.titanCard.x + ui.s(12), L.titanCard.y + ui.s(12), L.titanCard.w - ui.s(24), capY - ui.s(8) - (L.titanCard.y + ui.s(12))};
    if (rc.valid) {
        std::string line1 = std::to_string(rc.partsDown) + " / " + std::to_string(rc.partsTotal) + " parties tombées";
        line1 += rc.heartFallen ? " · Cœur vaincu" : rc.heart.open ? " · Cœur ouvert" : " · Cœur scellé";
        const std::string line2 = std::to_string(rc.relics) + " / " + std::to_string(rc.relicsTotal) + " reliques rapportées";
        const float tx = L.titanCard.x + ui.s(14), tw = L.titanCard.w - ui.s(28);
        L.titanCaption.push_back(fitted(ui, line1, {tx, capY, tw, capH}, FontFace::Bold, 15));
        L.titanCaption.push_back(fitted(ui, line2, {tx, capY + capH + ui.s(4), tw, capH}, FontFace::Body, 14));
    }
    L.listNote = fitted(ui, "Les fiches restent dans l'historique du clan.", {m, L.titanCard.y - ui.s(10) - ui.s(24), lw, ui.s(24)}, FontFace::Body, 14);
    L.list = {m, top, lw, L.listNote.r.y - ui.s(8) - top};
    L.rowH = ui.s(78);
    L.rowGap = ui.s(10);
    for (size_t i = 0; i < list.size(); ++i) {
        RaidRecapLayout::Row row;
        row.r = {L.list.x, L.list.y + static_cast<float>(i) * (L.rowH + L.rowGap), L.list.w - ui.s(12), L.rowH};
        row.mark = {row.r.right() - ui.s(14) - ui.s(24), row.r.y + ui.s(12), ui.s(24), ui.s(24)};
        const float tx = row.r.x + ui.s(16);
        row.title = fitted(ui, raids::recapEntryTitle(list[i]), {tx, row.r.y + ui.s(10), row.mark.x - ui.s(8) - tx, ui.s(28)}, FontFace::Bold, 19);
        row.line = fitted(ui, raids::recapEntryLine(list[i]), {tx, row.r.y + ui.s(44), row.r.right() - ui.s(16) - tx, ui.s(24)}, FontFace::Body, 15);
        L.rows.push_back(std::move(row));
    }

    // ------------------------------------------------------------ the right part
    const float rx = m + lw + ui.s(22), rw = W - m - rx;
    L.header = {rx, top, rw, ui.s(136)};
    if (!rc.valid) return L;
    {
        const float pad = ui.s(22);
        const float asideW = std::min(ui.s(300), rw * 0.24f);
        L.aside = {L.header.right() - pad - asideW, L.header.y + ui.s(14), asideW, L.header.h - ui.s(28)};
        L.emblem = {L.header.x + pad, L.header.y + (L.header.h - ui.s(80)) * 0.5f, ui.s(80), ui.s(80)};
        const float tx = L.emblem.right() + ui.s(18), tw = L.aside.x - ui.s(18) - tx;
        L.title = fitted(ui, raids::recapTitle(rc), {tx, L.header.y + ui.s(12), tw, ui.s(52)}, FontFace::Title, 38, 0.6f);
        L.eventLine = fitted(ui, raids::recapEventLine(rc), {tx, L.header.y + ui.s(68), tw, ui.s(26)}, FontFace::Body, 17);
        L.clanLine = fittedPieces(ui, raids::recapClanPieces(rc), {tx, L.header.y + ui.s(98), tw, ui.s(26)}, FontFace::Body, 18);
        L.pending = fitted(ui, raids::recapPendingText(rc), {L.aside.x, L.aside.y, L.aside.w, ui.s(28)}, FontFace::Bold, 14, 0.75f, Align::Right);
    }
    // The seven key figures.
    {
        const float cy = L.header.bottom() + ui.s(16), ch = ui.s(96), gap = ui.s(14);
        const auto figs = raids::recapFigures(rc);
        const float n = static_cast<float>(figs.size());
        const float cw = (rw - (n - 1.0f) * gap) / n;
        for (size_t i = 0; i < figs.size(); ++i) {
            RaidRecapLayout::Card c;
            c.id = figs[i].id;
            c.tone = figs[i].tone;
            c.r = {rx + static_cast<float>(i) * (cw + gap), cy, cw, ch};
            const bool icon = c.id == "heart" || c.id == "relics" || c.id == "revives";
            const float is = ui.s(28), side = ui.s(10) + is + ui.s(6);
            const Rect full{c.r.x + ui.s(10), c.r.y + ui.s(8), c.r.w - ui.s(20), ui.s(44)};
            const Rect beside{c.r.x + side, c.r.y + ui.s(8), c.r.w - 2.0f * side, ui.s(44)};
            const bool room = icon && measureW(ui, figs[i].value, FontFace::Bold, 28) * 0.8f <= beside.w;
            if (room) c.icon = {c.r.x + ui.s(10), c.r.y + ui.s(16), is, is};
            c.value = fitted(ui, figs[i].value, room ? beside : full, FontFace::Bold, 28, 0.7f, Align::Center);
            c.caption = fitted(ui, figs[i].caption, {c.r.x + ui.s(8), c.r.y + ui.s(58), c.r.w - ui.s(16), ui.s(28)}, FontFace::Body, 15, 0.75f, Align::Center);
            L.cards.push_back(std::move(c));
        }
    }
    const float midY = L.cards.empty() ? L.header.bottom() + ui.s(16) : L.cards.front().r.bottom() + ui.s(16);
    const float lowerH = ui.s(222), lowerY = btnY - ui.s(14) - lowerH;
    const float midH = lowerY - ui.s(16) - midY;
    const float tw = std::floor(rw * 0.565f), bx = rx + tw + ui.s(18), bw = rx + rw - bx;
    L.timeline = {rx, midY, tw, midH};
    L.best = {bx, midY, bw, midH};
    L.rewards = {rx, lowerY, tw, lowerH};
    L.me = {bx, lowerY, bw, lowerH};
    const bool pending = raids::recapPending(rc);

    // ------------------------------------------------------------ « LES SEPT PARTIES ET LE CŒUR »
    {
        const float pad = ui.s(20), x0 = L.timeline.x + pad, w0 = L.timeline.w - 2.0f * pad, y = L.timeline.y;
        L.timelineTitle = fitted(ui, "LES SEPT PARTIES ET LE CŒUR", {x0, y + ui.s(12), w0, ui.s(30)}, FontFace::Title, 17, 0.7f);
        L.timelineSub =
            fitted(ui, "Heure de chute de chaque Gigaboss · groupe qui l'a abattu · relique rapportée", {x0, y + ui.s(44), w0, ui.s(22)}, FontFace::Body, 13);
        const float rowsTop = y + ui.s(72), axisH = ui.s(26);
        const float rowsBottom = L.timeline.bottom() - ui.s(12) - axisH - ui.s(4);
        const size_t n = rc.parts.size() + 1;
        const float rowH = std::min(ui.s(48), (rowsBottom - rowsTop) / static_cast<float>(n));
        const float labelW = std::min(ui.s(240), w0 * 0.33f), nameW = ui.s(128);
        // The time axis: from the start to the next whole hour after the end.
        L.axisFrom = rc.startsAt;
        const int64_t dur = std::max<int64_t>(rc.endAt - rc.startsAt, 60);
        const int64_t hours = std::max<int64_t>(1, (dur + 3599) / 3600);
        L.axisTo = rc.startsAt + hours * 3600;
        for (size_t i = 0; i < n; ++i) {
            const bool heart = i + 1 == n;
            const raids::RecapPart& p = heart ? static_cast<const raids::RecapPart&>(rc.heart) : rc.parts[i];
            RaidRecapLayout::TRow row;
            row.part = heart ? "coeur" : p.part;
            row.heart = heart;
            const float ry = rowsTop + static_cast<float>(i) * rowH;
            row.r = {x0, ry, w0, rowH};
            const float ph = rowH - ui.s(4);
            row.portrait = {x0, ry + ui.s(2), ph, ph};
            const float nx = row.portrait.right() + ui.s(10);
            row.name = fitted(ui, partName(row.part), {nx, ry + ui.s(1), nameW, rowH * 0.54f - ui.s(1)}, FontFace::Bold, 16, 0.8f);
            const std::string boss = heart ? std::string("Cœur d'Asterion") : p.boss;
            row.boss = fitted(ui, boss, {nx, ry + rowH * 0.54f, nameW, rowH * 0.44f}, FontFace::Body, 13, 0.8f, Align::Left, !heart);
            const float relicX = nx + nameW + ui.s(8), rs = std::min(ui.s(22), rowH - ui.s(6));
            if (!heart) row.relic = {relicX, ry + (rowH - rs) * 0.5f, rs, rs};
            const Rect labelR{row.r.right() - labelW, ry, labelW, rowH};
            const std::string when = raids::recapPartLabel(p, heart, rc.heart.groups).first;
            row.label = fitted(ui, when, {labelR.x, ry + ui.s(1), labelW, rowH * 0.5f - ui.s(1)}, FontFace::Bold, 14, 0.8f);
            row.label2 = fittedPieces(ui, raids::recapWhoPieces(p, heart, rc.heart.groups), {labelR.x, ry + rowH * 0.5f, labelW, rowH * 0.5f - ui.s(1)},
                                      FontFace::Body, 13, 0.8f);
            const float trackX = relicX + ui.s(22) + ui.s(12);
            row.track = {trackX, ry + rowH * 0.5f - ui.s(5), labelR.x - ui.s(14) - trackX, ui.s(10)};
            if (p.fallen && p.at > 0 && L.axisTo > L.axisFrom) {
                const float k = std::clamp(static_cast<float>(p.at - L.axisFrom) / static_cast<float>(L.axisTo - L.axisFrom), 0.0f, 1.0f);
                row.mark = row.track.x + row.track.w * k;
            }
            L.trows.push_back(std::move(row));
        }
        if (!L.trows.empty()) {
            const Rect& tr = L.trows.front().track;
            L.axis = {tr.x, rowsTop + static_cast<float>(n) * rowH + ui.s(4), tr.w, ui.s(2)};
            // A tick each hour (every two when they would crowd), its label centred under it and kept in the panel.
            const int64_t step = hours > 6 ? 2 : 1;
            float prevRight = -1e9f;
            for (int64_t h = 0; h <= hours; h += step) {
                const float x = L.axis.x + L.axis.w * static_cast<float>(h) / static_cast<float>(hours);
                const std::string t = raids::hhmm(raids::toSystemLocal(L.axisFrom + h * 3600));
                const float w = measureW(ui, t, FontFace::Body, 12) + ui.s(2);
                Rect r{x - w * 0.5f, L.axis.bottom() + ui.s(4), w, ui.s(20)};
                r.x = std::clamp(r.x, x0, x0 + w0 - w);
                if (r.x < prevRight + ui.s(8)) continue;
                L.tickX.push_back(x);
                L.ticks.push_back(fitted(ui, t, r, FontFace::Body, 12, 1.0f, Align::Center));
                prevRight = r.right();
            }
        }
    }

    // ------------------------------------------------------------ « LES MEILLEURS DU RAID »
    {
        const float pad = ui.s(20), x1 = L.best.x + pad, w1 = L.best.w - 2.0f * pad, y = L.best.y;
        float titleRoom = w1;
        if (pending) {
            const float pw = std::min(measureW(ui, "en attente de validation", FontFace::Bold, 12) + ui.s(4), w1 * 0.45f);
            L.bestPending = fitted(ui, "en attente de validation", {x1 + w1 - pw, y + ui.s(14), pw, ui.s(26)}, FontFace::Bold, 12, 0.8f, Align::Right);
            titleRoom = w1 - pw - ui.s(12);
        }
        L.bestTitle = fitted(ui, "LES MEILLEURS DU RAID", {x1, y + ui.s(12), titleRoom, ui.s(30)}, FontFace::Title, 17, 0.7f);
        std::vector<const raids::RecapBest*> shown;
        for (const raids::RecapBest& b : rc.best)
            if (b.followed) shown.push_back(&b);
        const float rowsTop = y + ui.s(56);
        const float rowH = std::min(ui.s(56), (L.best.bottom() - ui.s(14) - rowsTop) / static_cast<float>(std::max<size_t>(shown.size(), 1)));
        for (size_t i = 0; i < shown.size(); ++i) {
            const raids::RecapBest& b = *shown[i];
            RaidRecapLayout::BRow row;
            row.category = b.category;
            for (const raids::RecapCategory& c : raids::recapCategories())
                if (c.id == b.category) row.tone = c.tone;
            row.r = {x1, rowsTop + static_cast<float>(i) * rowH, w1, rowH - ui.s(6)};
            row.bar = {row.r.x, row.r.y, ui.s(5), row.r.h};
            const float valW = ui.s(88);
            row.value = fitted(ui, raids::recapBestValue(b), {row.r.right() - ui.s(12) - valW, row.r.y, valW, row.r.h}, FontFace::Bold, 18, 0.75f, Align::Right);
            std::string label;
            for (const raids::RecapCategory& c : raids::recapCategories())
                if (c.id == b.category) label = c.label;
            const float labelW = row.r.w * 0.40f;
            row.label = fitted(ui, label, {row.r.x + ui.s(16), row.r.y, labelW, row.r.h}, FontFace::Body, 16);
            const float av = std::max(ui.s(10), row.r.h - ui.s(10));
            const float nx0 = row.label.r.right() + ui.s(10);
            if (b.has) row.avatar = {nx0, row.r.y + (row.r.h - av) * 0.5f, av, av};
            const float nx = b.has ? row.avatar.right() + ui.s(10) : nx0;
            const std::string name = !b.has ? std::string("personne") : b.me ? std::string("Vous") : b.username;
            row.name = fitted(ui, name, {nx, row.r.y, row.value.r.x - ui.s(10) - nx, row.r.h}, FontFace::Bold, 17, 0.75f, Align::Left, b.has && !b.me);
            L.brows.push_back(std::move(row));
        }
    }

    // ------------------------------------------------------------ « RÉCOMPENSES OBTENUES PAR LE CLAN »
    {
        const float pad = ui.s(20), x2 = L.rewards.x + pad, w2 = L.rewards.w - 2.0f * pad, y = L.rewards.y;
        const std::vector<int> tiles = rewardTiles(rc);
        const float tileW = ui.s(150), tileH = ui.s(128), gap = ui.s(12);
        const int perPage = std::max(1, static_cast<int>((w2 + gap) / (tileW + gap)));
        L.rewardPages = std::max(1, (static_cast<int>(tiles.size()) + perPage - 1) / perPage);
        L.rewardPage = std::clamp(opt.rewardPage, 0, L.rewardPages - 1);
        float titleRoom = w2;
        if (L.rewardPages > 1) {
            const float bs = ui.s(36);
            L.next = {x2 + w2 - bs, y + ui.s(12), bs, ui.s(30)};
            const std::string pg = std::to_string(L.rewardPage + 1) + " / " + std::to_string(L.rewardPages);
            const float pw = measureW(ui, pg, FontFace::Body, 13) + ui.s(4);
            L.rewardsPage = fitted(ui, pg, {L.next.x - ui.s(6) - pw, y + ui.s(12), pw, ui.s(30)}, FontFace::Body, 13, 1.0f, Align::Center);
            L.prev = {L.rewardsPage.r.x - ui.s(6) - bs, y + ui.s(12), bs, ui.s(30)};
            titleRoom = L.prev.x - ui.s(12) - x2;
        }
        L.rewardsTitle = fitted(ui, "RÉCOMPENSES OBTENUES PAR LE CLAN", {x2, y + ui.s(12), titleRoom, ui.s(30)}, FontFace::Title, 17, 0.7f);
        const float ty = y + ui.s(52);
        for (int k = 0; k < perPage; ++k) {
            const int at = L.rewardPage * perPage + k;
            if (at >= static_cast<int>(tiles.size())) break;
            RaidRecapLayout::Tile t;
            t.index = tiles[static_cast<size_t>(at)];
            t.r = {x2 + static_cast<float>(k) * (tileW + gap), ty, tileW, tileH};
            t.preview = {t.r.x + ui.s(8), t.r.y + ui.s(6), t.r.w - ui.s(16), ui.s(56)};
            std::string kind, name, count;
            if (t.index < 0) {
                int n = 0, grants = 0;
                for (const raids::RecapReward& w : rc.rewards)
                    if (w.gigaboss) {
                        ++n;
                        grants += w.players;
                    }
                kind = "Badges";
                name = std::to_string(n) + " Gigaboss";
                count = std::to_string(grants) + (grants > 1 ? " badges reçus" : " badge reçu");
            } else {
                const raids::RecapReward& w = rc.rewards[static_cast<size_t>(t.index)];
                kind = rewardKind(w);
                name = rewardShortName(w);
                count = playersText(w.players);
            }
            const float ix = t.r.x + ui.s(6), iw = t.r.w - ui.s(12);
            t.kind = fitted(ui, kind, {ix, t.r.y + ui.s(64), iw, ui.s(20)}, FontFace::Body, 12, 0.8f, Align::Center);
            t.name = fitted(ui, name, {ix, t.r.y + ui.s(84), iw, ui.s(22)}, FontFace::Bold, 14, 0.75f, Align::Center);
            t.count = fitted(ui, count, {ix, t.r.y + ui.s(106), iw, ui.s(18)}, FontFace::Body, 12, 0.8f, Align::Center);
            L.tiles.push_back(std::move(t));
        }
        if (tiles.empty())
            L.rewardsEmpty = fitted(ui, "Aucune récompense accordée au clan pour ce raid.", {x2, ty + ui.s(10), w2, ui.s(26)}, FontFace::Body, 15);
        std::string note;
        if (!rc.titles.empty()) {
            note = "Titres du clan : ";
            for (size_t i = 0; i < rc.titles.size(); ++i) note += (i ? ", " : "") + rc.titles[i];
        } else {
            note = "Accordées par le serveur d'après les résultats validés · à récupérer dans CLANS › RAIDS";
        }
        L.rewardsNote = fitted(ui, note, {x2, ty + tileH + ui.s(8), w2, ui.s(22)}, FontFace::Body, 13);
    }

    // ------------------------------------------------------------ « VOTRE RAID »
    {
        const float pad = ui.s(20), x3 = L.me.x + pad, w3 = L.me.w - 2.0f * pad, y = L.me.y;
        float titleRoom = w3;
        if (pending && rc.me.played) {
            const float pw = std::min(measureW(ui, "en attente de validation", FontFace::Bold, 12) + ui.s(4), w3 * 0.45f);
            L.mePending = fitted(ui, "en attente de validation", {x3 + w3 - pw, y + ui.s(14), pw, ui.s(26)}, FontFace::Bold, 12, 0.8f, Align::Right);
            titleRoom = w3 - pw - ui.s(12);
        }
        L.meTitle = fitted(ui, "VOTRE RAID", {x3, y + ui.s(12), titleRoom, ui.s(30)}, FontFace::Title, 17, 0.7f);
        const auto lines = raids::recapMyLines(rc, partNames());
        if (lines.empty()) {
            const auto wrapped =
                raidui::wrap(ui, "Vous n'avez pas joué ce raid : vos chiffres (groupe, parties abattues, zones, réanimations, part du Cœur) apparaissent ici quand vous y prenez part.",
                             FontFace::Body, fz(ui, 15), w3, 4);
            for (size_t i = 0; i < wrapped.size(); ++i) {
                RecapText t;
                t.r = {x3, y + ui.s(54) + static_cast<float>(i) * ui.s(28), w3, ui.s(26)};
                t.text = i18n::keep(wrapped[i]);
                t.size = fz(ui, 15);
                t.px = ui.s(t.size);
                L.meNote.push_back(t);
            }
        }
        const float lineTop = y + ui.s(52);
        const float lineH = lines.empty() ? 0.0f : std::min(ui.s(36), (L.me.bottom() - ui.s(10) - lineTop) / static_cast<float>(lines.size()));
        const float labelW = w3 * 0.36f;
        for (size_t i = 0; i < lines.size(); ++i) {
            RaidRecapLayout::MLine l;
            const float ly = lineTop + static_cast<float>(i) * lineH, lh = std::min(lineH - ui.s(2), ui.s(30));
            l.label = fitted(ui, lines[i].first, {x3, ly, labelW, lh}, FontFace::Body, 15, 0.8f);
            const Rect vr{x3 + labelW + ui.s(10), ly, w3 - labelW - ui.s(10), lh};
            std::string value = lines[i].second;
            // Too many parts for the line: their number, never a cut list.
            if (lines[i].first == "Parties abattues" && measureW(ui, value, FontFace::Bold, 15) * 0.75f > vr.w)
                value = std::to_string(rc.me.parts.size()) + " parties";
            l.value = lines[i].first == "Groupe" ? fittedPieces(ui, raids::recapMyGroupPieces(rc), vr, FontFace::Bold, 15, 0.75f, Align::Right)
                                                 : fitted(ui, value, vr, FontFace::Bold, 15, 0.75f, Align::Right);
            L.mlines.push_back(std::move(l));
        }
    }

    // ------------------------------------------------------------ the buttons
    {
        std::vector<std::pair<std::string, std::string>> want;
        if (opt.titanMap) want.push_back({"titan", "Carte du titan"});
        if (rc.heartFallen && opt.victoryFilm) want.push_back({"film", "Revoir le film de victoire"});
        if (opt.eventSheet) want.push_back({"event", "Fiche de l'événement"});
        const std::string hint = "Échap / B : retour";
        const float hintW = std::min(measureW(ui, hint, FontFace::Body, 15) + ui.s(8), rw * 0.25f);
        const float gap = ui.s(12), avail = rw - hintW - ui.s(16);
        float total = 0.0f;
        std::vector<float> widths;
        for (const auto& [id, label] : want) {
            widths.push_back(measureW(ui, label, FontFace::Bold, 18) + ui.s(44));
            total += widths.back();
        }
        total += gap * static_cast<float>(std::max<size_t>(want.size(), 1) - 1);
        const float k = total > avail && total > 0.0f ? avail / total : 1.0f;
        float x = rx;
        for (size_t i = 0; i < want.size(); ++i) {
            RaidRecapLayout::Button b;
            b.id = want[i].first;
            b.r = {x, btnY, std::floor(widths[i] * k), btnH};
            b.text = fitted(ui, want[i].second, {b.r.x + ui.s(14), b.r.y, b.r.w - ui.s(28), b.r.h}, FontFace::Bold, 18, 0.7f, Align::Center);
            x = b.r.right() + gap * k;
            L.buttons.push_back(std::move(b));
        }
        L.hint = fitted(ui, hint, {rx + rw - hintW, btnY, hintW, btnH}, FontFace::Body, 15, 0.75f, Align::Right);
    }
    return L;
}

namespace {

/// An animated sprite of the game's art (its own animator).
struct ArtSprite {
    AnimationSetPtr set;
    Animator anim;
    void load(ResourceManager& res, const std::string& path, const std::string& clip) {
        set = res.animation(path);
        if (!set) return;
        anim.setSet(set);
        if (anim.has(clip)) anim.play(clip, true);
    }
    /// The current frame fitted in `box` (whole multiples once enlarged).
    bool draw(Renderer& r, const Rect& box, Color tint = Color::white()) const {
        if (!set || !set->texture || box.w <= 0.0f || box.h <= 0.0f) return false;
        const Rect src = anim.sourceRect();
        if (src.w <= 0.0f || src.h <= 0.0f) return false;
        float k = std::min(box.w / src.w, box.h / src.h);
        if (k >= 1.0f) k = std::floor(k);
        r.drawTexture(*set->texture, &src, Rect::fromCenter(box.center(), {src.w * k, src.h * k}), tint);
        return true;
    }
};

class RaidRecapScene : public Scene {
public:
    RaidRecapScene(GameServices& g, std::string clanId, std::string eventId) : g_(g), net_(g), clanId_(std::move(clanId)), eventId_(std::move(eventId)) {}
    RaidRecapScene(GameServices& g, const Json& list, const Json& recap) : g_(g), net_(g), fixture_(true) {
        list_ = raids::parseRecapList(list);
        listLoaded_ = true;
        recap_ = raids::parseRecap(recap);
        loaded_ = recap_.valid;
        clanId_ = recap_.clanId;
        eventId_ = recap_.eventId;
    }

    void onEnter() override {
        bg_.init(g_);
        sheets_ = raid::loadRaidSheets(g_.content, g_.achievementCatalog, g_.cosmetics);
        relic_.load(g_.app.resources(), "animations/relic.json", "idle");
        g_.ui.setFocus("recap/back");
        if (!fixture_) loadList();
    }
    void onResume() override {
        if (!fixture_ && loaded_) loadRecap(eventId_);
    }

    void update(float dt) override {
        bg_.update(dt);
        relic_.anim.update(dt);
        t_ += dt;
    }

    RaidRecapView view() const { return view_; }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.6f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{8, 12, 28, 190});
        ui.inputEnabled = g_.app.topScene() == this;
        ui.pushId("recap");
        view_ = RaidRecapView{};
        view_.valid = true;
        view_.loaded = loaded_;
        view_.eventId = eventId_;
        RaidRecapOptions opt;
        opt.victoryFilm = asterionfilm::victory() != nullptr;
        // The event's own screens read the event as this player may (a clan raid: its clans' members only).
        opt.titanMap = !fixture_ && (recap_.kind == "world" || recap_.member);
        opt.eventSheet = opt.titanMap;
        opt.rewardPage = rewardPage_;
        const RaidRecapLayout L = layoutRaidRecap(ui, recap_, list_, opt);
        rewardPage_ = L.rewardPage;
        bool back = false;
        if (ui.button("back", L.back, "‹ Retour")) back = true;
        text(L.crumb, kMuted);
        drawList(L);
        if (!loaded_) {
            const Rect r{L.header.x, L.header.y, L.header.w, ui.s(60)};
            raidui::fit(ui, error_.empty() ? (listLoaded_ && list_.empty() ? "Aucun raid terminé pour l'instant : la fiche d'un raid apparaît ici dès qu'il est fini."
                                                                            : "Chargement de la fiche du raid…")
                                           : error_,
                        {r.x, r.center().y}, 17, r.w, error_.empty() ? kDim : ui.theme().danger);
        } else {
            drawHeader(L);
            drawCards(L);
            drawTimeline(L);
            drawBest(L);
            drawRewards(L);
            drawMe(L);
            for (const RaidRecapLayout::Button& b : L.buttons) {
                UIContext::ButtonStyle st;
                st.primary = &b == &L.buttons.front();
                st.fontSize = b.text.size;
                if (ui.button(b.id, b.r, b.text.text, st)) press(b.id);
            }
            text(L.hint, kMuted);
        }
        view_.buttons = static_cast<int>(L.buttons.size());
        ui.popId();
        titan_.drawTooltip(g_);
        if (!back && ui.backPressed()) back = true;
        ui.end();
        if (back) g_.app.popScene();
    }

private:
    // ------------------------------------------------------------------ data
    void loadList() {
        const int gen = ++listGen_;
        g_.online.get("/api/v1/clans/" + clanId_ + "/raids", OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                          if (gen != listGen_) return;
                          if (!r.ok) {
                              error_ = r.message;
                              return;
                          }
                          list_ = raids::parseRecapList(r.data);
                          listLoaded_ = true;
                          if (eventId_.empty() && !list_.empty()) eventId_ = list_.front().id;
                          if (!eventId_.empty()) loadRecap(eventId_);
                      }));
    }
    void loadRecap(const std::string& id) {
        const int gen = ++recapGen_;
        g_.online.get("/api/v1/clans/" + clanId_ + "/raids/" + id + "/recap", OnlineService::guard(life_, [this, gen, id](const ApiResult& r) {
                          if (gen != recapGen_) return;
                          if (!r.ok) {
                              error_ = r.message;
                              loaded_ = false;
                              return;
                          }
                          recap_ = raids::parseRecap(r.data);
                          loaded_ = recap_.valid;
                          eventId_ = id;
                          error_.clear();
                      }));
    }
    void select(const std::string& id) {
        if (id == eventId_ || fixture_) return;
        eventId_ = id;
        loaded_ = false;
        rewardPage_ = 0;
        loadRecap(id);
    }
    void press(const std::string& id) {
        if (id == "titan") g_.app.pushScene(makeRaidTitan(g_, eventId_));
        else if (id == "event") g_.app.pushScene(makeRaidEvent(g_, eventId_));
        else if (id == "film") {
            if (auto film = asterionfilm::makeVictory(g_, false)) g_.app.pushScene(std::move(film));
        }
    }

    // ------------------------------------------------------------------ drawing helpers
    void text(const RecapText& t, Color c) {
        if (t.text.empty()) return;
        i18n::Verbatim v(t.verbatim);
        const float x = t.align == Align::Center ? t.r.center().x : t.align == Align::Right ? t.r.right() : t.r.x;
        g_.ui.label(t.text, {x, t.r.center().y}, t.size, c, t.align, t.face, VAlign::Middle);
    }
    void panel(const Rect& r, Color fill = kPanel, Color edge = kLine) {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(r, ui.s(14), fill);
        ui.r().drawRoundedRect(r, ui.s(14), edge, ui.s(1.2f));
    }
    const EnemyDefinition* bossOf(const std::string& part) const {
        if (part == "coeur") return g_.content.enemy("asterion_coeur");
        const raid::GigabossSheet* s = sheets_.ofPart(part);
        return s ? s->def : nullptr;
    }

    // ------------------------------------------------------------------ the left column
    void drawList(const RaidRecapLayout& L) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const float content = static_cast<float>(L.rows.size()) * (L.rowH + L.rowGap);
        ui.beginScroll("list", L.list, content);
        const float off = ui.scrollOffset();
        for (size_t i = 0; i < L.rows.size(); ++i) {
            const RaidRecapLayout::Row& row0 = L.rows[i];
            Rect row = row0.r;
            row.y -= off;
            const raids::RecapEntry& e = list_[i];
            UIContext::ButtonStyle st;
            st.selected = e.id == eventId_;
            const std::string rid = "row" + std::to_string(i);
            if (ui.button(rid, row, "", st)) select(e.id);
            if (row.bottom() < L.list.y || row.y > L.list.bottom()) continue;
            if (e.id == eventId_) r.drawRoundedRect(row, ui.s(12), kCyan, ui.s(2));
            // The mark: Asterion's heart burst (won), a dim heart (it stood).
            const Vec2 c{row0.mark.center().x, row0.mark.center().y - off};
            if (e.heartFallen) {
                r.drawGlow(c, row0.mark.w * 0.7f, Color{255, 200, 120, 150});
                r.fillCircle(c, row0.mark.w * 0.22f, Color{255, 236, 200, 255});
                r.drawRing(c, row0.mark.w * 0.42f, std::max(1.0f, ui.s(2)), kGold.withAlpha(200));
            } else {
                r.fillCircle(c, row0.mark.w * 0.26f, Color{120, 60, 80, 255});
                r.drawRing(c, row0.mark.w * 0.42f, std::max(1.0f, ui.s(1.5f)), kMuted.withAlpha(160));
            }
            RecapText t = row0.title, l = row0.line;
            t.r.y -= off;
            l.r.y -= off;
            text(t, kText);
            text(l, e.heartFallen ? kGreen : kMuted);
        }
        ui.endScroll();
        view_.rows = static_cast<int>(L.rows.size());
        text(L.listNote, kMuted);
        // The titan of the raid shown: its parts as they ended, the relics carried back by their parts.
        panel(L.titanCard, kPanel2);
        if (!loaded_) return;
        std::map<std::string, asterion::State> states;
        for (const raids::RecapPart& p : recap_.parts)
            states[p.part] = p.fallen ? asterion::State::Destroyed : p.phase >= 2 ? asterion::State::Weakened : p.phase == 1 ? asterion::State::Attacked
                                                                                                                              : asterion::State::Intact;
        states["coeur"] = recap_.heartFallen ? asterion::State::Destroyed : recap_.heart.open ? asterion::State::Weakened : asterion::State::Intact;
        titan_.draw(g_, "titan", L.titanBox, states);
        view_.titan = titan_.bounds().w > 0.0f;
        for (const raids::RecapPart& p : recap_.parts) {
            Vec2 a;
            if (!p.relic || !titan_.anchorOf(p.part, a)) continue;
            const float s = std::max(ui.s(14), titan_.bounds().w / 160.0f * 12.0f);
            relic_.draw(r, Rect::fromCenter({a.x + s * 0.7f, a.y - s * 0.7f}, {s, s}));
        }
        for (size_t i = 0; i < L.titanCaption.size(); ++i) text(L.titanCaption[i], i == 0 ? (recap_.heartFallen ? kGold : kText) : kDim);
    }

    // ------------------------------------------------------------------ the header
    void drawHeader(const RaidRecapLayout& L) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const Rect& h = L.header;
        r.fillRoundedRect(h, ui.s(14), kPanel2);
        // The backdrop: the stars of Asterion's fall (the victory film's art), dimmed under the texts.
        r.pushClip(h);
        auto tex = [&](const char* path) { return g_.app.resources().texture(path); };
        if (auto sky = tex("stories/asterion/espace.png")) {
            const float k = std::max(h.w / static_cast<float>(sky->width()), h.h / static_cast<float>(sky->height()));
            const float dw = static_cast<float>(sky->width()) * k, dh = static_cast<float>(sky->height()) * k;
            const float drift = std::fmod(t_ * ui.s(6.0f), std::max(1.0f, dw - h.w + 1.0f));
            r.drawTexture(*sky, nullptr, {h.x - drift * 0.2f, h.y + (h.h - dh) * 0.5f, dw, dh}, Color{255, 255, 255, 200});
            view_.backdrop = true;
        }
        if (auto neb = tex("stories/asterion/nebuleuse.png")) {
            const float nh = h.h * 1.2f, nw = nh * static_cast<float>(neb->width()) / static_cast<float>(std::max(1, neb->height()));
            r.drawTexture(*neb, nullptr, {h.x + h.w * 0.35f, h.y - h.h * 0.1f, nw, nh}, Color{255, 255, 255, 150});
        }
        if (recap_.heartFallen) {
            // The heart gone supernova, and its wave through the galaxy, behind the right of the header.
            const Vec2 c{h.right() - h.w * 0.16f, h.center().y};
            const float pulse = 0.5f + 0.5f * std::sin(t_ * 1.6f);
            if (auto onde = tex("stories/asterion/onde.png")) {
                const float s = h.h * (1.5f + 0.25f * pulse);
                r.drawTexture(*onde, nullptr, Rect::fromCenter(c, {s, s}), Color{255, 255, 255, static_cast<uint8_t>(90 + 60 * (1.0f - pulse))});
            }
            if (auto nova = tex("stories/asterion/nova.png")) {
                const float s = h.h * 1.25f;
                r.drawTexture(*nova, nullptr, Rect::fromCenter(c, {s, s}), Color{255, 255, 255, 220}, BlendMode::Additive);
            }
        }
        r.fillGradientH({h.x, h.y, h.w * 0.75f, h.h}, Color{12, 16, 40, 225}, Color{12, 16, 40, 90});
        r.popClip();
        r.drawRoundedRect(h, ui.s(14), recap_.heartFallen ? kGold.withAlpha(150) : kLine, ui.s(1.2f));
        clans::Emblem em = clans::emblemFromJson(Json{{"shape", recap_.emblemShape}, {"primary", recap_.emblemPrimary}, {"secondary", recap_.emblemSecondary}});
        drawClanEmblem(ui, L.emblem, em);
        text(L.title, recap_.heartFallen ? kGold : kText);
        text(L.eventLine, kDim);
        text(L.clanLine, kText);
        if (!L.pending.text.empty()) {
            // Readable over the supernova: a dark backing under the texts of the right.
            const float tw = std::min(L.pending.r.w, ui.text().measure(L.pending.text, L.pending.face, L.pending.px).x);
            r.fillRoundedRect({L.pending.r.right() - tw - ui.s(10), L.pending.r.y - ui.s(2), tw + ui.s(16), L.pending.r.h + ui.s(4)}, ui.s(8),
                              Color{10, 14, 34, 210});
            text(L.pending, kGold);
        }
    }

    void drawCards(const RaidRecapLayout& L) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        for (const RaidRecapLayout::Card& c : L.cards) {
            const Color tc = toneColor(c.tone);
            r.fillRoundedRect(c.r, ui.s(12), kPanel);
            r.drawRoundedRect(c.r, ui.s(12), tc.withAlpha(c.tone == 4 ? 140 : 200), ui.s(2));
            if (c.icon.w > 0.0f) {
                const Vec2 ic = c.icon.center();
                if (c.id == "relics") relic_.draw(r, c.icon);
                else if (c.id == "heart") {
                    const float beat = 0.5f + 0.5f * std::sin(t_ * 3.2f);
                    r.drawGlow(ic, c.icon.w * (0.55f + 0.1f * beat), Color{255, 90, 120, static_cast<uint8_t>(110 + 80 * beat)});
                    r.fillCircle(ic, c.icon.w * 0.22f, recap_.heartFallen ? Color{255, 220, 200, 255} : Color{200, 60, 90, 255});
                } else if (c.id == "revives") {
                    const float a = c.icon.w * 0.16f, b = c.icon.w * 0.42f;
                    r.drawGlow(ic, c.icon.w * 0.5f, Color{124, 240, 168, 90});
                    r.fillRect({ic.x - a, ic.y - b, 2 * a, 2 * b}, kGreen);
                    r.fillRect({ic.x - b, ic.y - a, 2 * b, 2 * a}, kGreen);
                }
            }
            text(c.value, c.tone == 4 ? kText : tc);
            text(c.caption, kDim);
        }
        view_.cards = static_cast<int>(L.cards.size());
    }

    // ------------------------------------------------------------------ « LES SEPT PARTIES ET LE CŒUR »
    void drawTimeline(const RaidRecapLayout& L) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        panel(L.timeline);
        text(L.timelineTitle, kGold);
        text(L.timelineSub, kMuted);
        for (float x : L.tickX) r.fillRect({x - ui.s(0.5f), L.trows.empty() ? L.axis.y : L.trows.front().r.y, std::max(1.0f, ui.s(1)), L.axis.y - L.trows.front().r.y},
                                          Color{60, 74, 124, 90});
        for (size_t i = 0; i < L.trows.size(); ++i) {
            const RaidRecapLayout::TRow& row = L.trows[i];
            const raids::RecapPart& p = row.heart ? static_cast<const raids::RecapPart&>(recap_.heart) : recap_.parts[i];
            // Its gigaboss, animated (its sprite of the combat), dimmed while it stands.
            r.fillRoundedRect(row.portrait, ui.s(6), Color{10, 14, 32, 255});
            if (const EnemyDefinition* def = bossOf(row.part)) {
                r.pushClip(row.portrait);
                drawCreature(g_, *def, row.portrait.expanded(-ui.s(2)), t_ + static_cast<float>(i) * 0.37f, p.fallen ? Color::white() : Color{170, 170, 190, 255},
                             "idle");
                r.popClip();
                ++view_.portraits;
            }
            if (p.fallen) r.drawRoundedRect(row.portrait, ui.s(6), row.heart ? kHeart : kGreen.withAlpha(180), std::max(1.0f, ui.s(1.5f)));
            text(row.name, row.heart ? kHeart : kText);
            text(row.boss, kMuted);
            // The relic of its branch: the relic's own sprite, a dim outline when it was not carried back.
            if (row.relic.w > 0.0f) {
                if (p.relic) relic_.draw(r, row.relic);
                else r.drawCircle(row.relic.center(), row.relic.w * 0.32f, Color{80, 92, 140, 160}, std::max(1.0f, ui.s(1.2f)));
            }
            // The track: from the start to its fall (or to the end, dim, while it stood).
            const Color track = row.heart ? Color{70, 30, 48, 255} : Color{28, 52, 70, 255};
            r.fillRoundedRect(row.track, row.track.h * 0.5f, Color{20, 28, 56, 255});
            if (row.mark >= 0.0f) {
                r.fillRoundedRect({row.track.x, row.track.y, std::max(row.track.h, row.mark - row.track.x), row.track.h}, row.track.h * 0.5f,
                                  row.heart ? Color{150, 48, 78, 255} : Color{40, 110, 110, 255});
                const Vec2 c{row.mark, row.track.center().y};
                const float rad = ui.s(9);
                r.fillCircle(c, rad, row.heart ? kHeart : kGreen);
                const Color ink{14, 30, 26, 255};
                r.drawLine({c.x - rad * 0.45f, c.y}, {c.x - rad * 0.1f, c.y + rad * 0.38f}, ink, std::max(1.0f, ui.s(2)));
                r.drawLine({c.x - rad * 0.1f, c.y + rad * 0.38f}, {c.x + rad * 0.5f, c.y - rad * 0.4f}, ink, std::max(1.0f, ui.s(2)));
            } else if (p.phase > 0) {
                r.fillRoundedRect(row.track, row.track.h * 0.5f, track);
            }
            text(row.label, p.fallen ? (row.heart ? kHeart : kGreen) : kMuted);
            text(row.label2, p.fallen ? (p.by.me ? kGold : kText) : kMuted);
        }
        view_.timelineRows = static_cast<int>(L.trows.size());
        if (L.axis.w > 0.0f) r.fillRect(L.axis, Color{60, 74, 124, 200});
        for (const RecapText& t : L.ticks) text(t, kMuted);
    }

    // ------------------------------------------------------------------ « LES MEILLEURS DU RAID »
    void drawBest(const RaidRecapLayout& L) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        panel(L.best);
        text(L.bestTitle, kGold);
        text(L.bestPending, kGold);
        for (const RaidRecapLayout::BRow& row : L.brows) {
            r.fillRoundedRect(row.r, ui.s(8), Color{24, 34, 70, 230});
            r.fillRect(row.bar, toneColor(row.tone));
            text(row.label, kDim);
            for (const raids::RecapBest& b : recap_.best)
                if (b.category == row.category && b.has && row.avatar.w > 0.0f) net_.drawAvatar(b.avatar, b.playerId, row.avatar);
            text(row.name, row.name.verbatim ? kText : (row.name.text == "personne" ? kMuted : kGold));
            text(row.value, toneColor(row.tone));
        }
        view_.bestRows = static_cast<int>(L.brows.size());
    }

    // ------------------------------------------------------------------ « RÉCOMPENSES OBTENUES PAR LE CLAN »
    void drawRewards(const RaidRecapLayout& L) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        panel(L.rewards);
        text(L.rewardsTitle, kGold);
        if (L.rewardPages > 1) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.enabled = L.rewardPage > 0;
            if (ui.button("rprev", L.prev, "‹", st)) rewardPage_ = L.rewardPage - 1;
            st.enabled = L.rewardPage + 1 < L.rewardPages;
            if (ui.button("rnext", L.next, "›", st)) rewardPage_ = L.rewardPage + 1;
            text(L.rewardsPage, kDim);
        }
        for (const RaidRecapLayout::Tile& t : L.tiles) {
            r.fillRoundedRect(t.r, ui.s(10), Color{24, 34, 70, 235});
            const raids::RecapReward* w = t.index >= 0 ? &recap_.rewards[static_cast<size_t>(t.index)] : nullptr;
            Color edge = w ? (w->kind == "badge" ? Color{190, 150, 255, 255} : kGold) : kGreen;
            r.drawRoundedRect(t.r, ui.s(10), edge.withAlpha(190), std::max(1.0f, ui.s(1.5f)));
            if (drawReward(w, t.preview)) ++view_.previews;
            text(t.kind, kMuted);
            text(t.name, kText);
            text(t.count, w && w->mine ? kGold : kDim);
        }
        view_.tiles = static_cast<int>(L.tiles.size());
        text(L.rewardsEmpty, kMuted);
        text(L.rewardsNote, kMuted);
    }

    /// A reward drawn with its own texture: a badge's emblem, an item's preview (the suit on Nova, the
    /// title's chip, the frame, the victory effect); the badges of the gigaboss side by side. False when
    /// this game does not know it (a newer one): nothing is drawn in its place.
    bool drawReward(const raids::RecapReward* w, const Rect& box) {
        UIContext& ui = g_.ui;
        if (!w) {
            std::vector<const AchievementDef*> defs;
            for (const raids::RecapReward& x : recap_.rewards)
                if (x.gigaboss)
                    if (const AchievementDef* a = g_.achievementCatalog.find(x.ref)) defs.push_back(a);
            if (defs.empty()) return false;
            const float rad = std::min(box.h * 0.32f, box.w / (static_cast<float>(defs.size()) * 2.2f));
            const float step = std::min(rad * 2.3f, box.w / static_cast<float>(defs.size()));
            const float x0 = box.center().x - step * (static_cast<float>(defs.size()) - 1.0f) * 0.5f;
            for (size_t i = 0; i < defs.size(); ++i) {
                const float y = box.center().y + (i % 2 ? rad * 0.45f : -rad * 0.45f);
                drawBadgeEmblem(g_, *defs[i], {x0 + step * static_cast<float>(i), y}, rad);
            }
            return true;
        }
        if (w->kind == "badge") {
            const AchievementDef* a = g_.achievementCatalog.find(w->ref);
            if (!a) return false;
            drawBadgeEmblem(g_, *a, box.center(), box.h * 0.44f);
            return true;
        }
        const CosmeticItem* it = g_.cosmetics.find(w->ref);
        if (!it) return false;
        Rect pr = box;
        if (it->category != "title") pr = Rect::fromCenter(box.center(), {std::min(box.w, box.h * 1.6f), box.h});
        else pr = Rect::fromCenter(box.center(), {box.w, std::min(box.h, ui.s(34))});
        drawCosmeticPreview(g_, *it, pr, t_, true);
        return true;
    }

    // ------------------------------------------------------------------ « VOTRE RAID »
    void drawMe(const RaidRecapLayout& L) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        r.fillRoundedRect(L.me, ui.s(14), kPanel);
        r.drawRoundedRect(L.me, ui.s(14), kCyan.withAlpha(200), ui.s(2));
        text(L.meTitle, kCyan);
        text(L.mePending, kGold);
        for (const RaidRecapLayout::MLine& l : L.mlines) {
            text(l.label, kMuted);
            text(l.value, kText);
        }
        for (const RecapText& t : L.meNote) text(t, kDim);
    }

    GameServices& g_;
    OnlineUi net_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    MenuBackground bg_;
    raid::RaidSheets sheets_;
    AsterionFigure titan_;
    ArtSprite relic_;
    bool fixture_ = false;
    std::string clanId_, eventId_, error_;
    std::vector<raids::RecapEntry> list_;
    raids::Recap recap_;
    bool listLoaded_ = false, loaded_ = false;
    int listGen_ = 0, recapGen_ = 0, rewardPage_ = 0;
    float t_ = 0.0f;
    RaidRecapView view_;
};

}  // namespace

std::unique_ptr<Scene> makeRaidRecap(GameServices& g, const std::string& clanId, const std::string& eventId) {
    return std::make_unique<RaidRecapScene>(g, clanId, eventId);
}

std::unique_ptr<Scene> makeRaidRecapFrom(GameServices& g, const Json& list, const Json& recap) { return std::make_unique<RaidRecapScene>(g, list, recap); }

RaidRecapView raidRecapView(const Scene* s) {
    const auto* r = dynamic_cast<const RaidRecapScene*>(s);
    return r ? r->view() : RaidRecapView{};
}

}  // namespace cosmic
