#include "ui/widgets/CosmicFrame.h"
#include "ui/screens/AdminAssets.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "ui/screens/BossStoryScene.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

namespace {

/// Never below 11.5 px on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

const char* const kTypeChips[] = {"Tout", "Images", "Animations", "Sons", "Musiques", "Polices", "Données"};

std::string countText(int n) { return n == 1 ? std::string("1 asset") : std::to_string(n) + " assets"; }
std::string usesText(int n) { return n == 1 ? std::string("1 utilisation") : std::to_string(n) + " utilisations"; }

/// A row of small buttons, the chosen one highlighted, never past `maxRight`: the labels get
/// smaller (down to 12), then shortened. Returns the right edge.
float chips(UIContext& ui, const std::string& id, float x, float y, float h, const std::vector<std::string>& labels, int& active, float maxRight) {
    const float pad = ui.s(30), gap = ui.s(8);
    auto rowWidth = [&](float f) {
        float w = 0.0f;
        for (const std::string& l : labels) w += ui.text().measure(l, FontFace::Bold, ui.s(f)).x + pad + gap;
        return w - gap;
    };
    float font = 16.0f;
    while (font > 12.0f && x + rowWidth(font) > maxRight) font -= 1.0f;
    const bool squeeze = x + rowWidth(font) > maxRight;
    const float share = (maxRight - x - gap * static_cast<float>(labels.size() - 1)) / static_cast<float>(labels.size());
    for (size_t i = 0; i < labels.size(); ++i) {
        UIContext::ButtonStyle st;
        st.small = true;
        st.selected = static_cast<int>(i) == active;
        st.fontSize = font;
        float w = ui.text().measure(labels[i], FontFace::Bold, ui.s(font)).x + pad;
        std::string shown = labels[i];
        if (squeeze) {
            w = std::max(ui.s(24), share);
            shown = ui.elide(labels[i], FontFace::Bold, font, w - ui.s(16));
        }
        if (cframe::button(ui, id + std::to_string(i), {x, y, w, h}, shown, st)) active = static_cast<int>(i);
        x += w + gap;
    }
    return x - gap;
}

/// A speaker (sound) or a note (music) centred on `c`, `size` high.
void audioSymbol(Renderer& r, Vec2 c, float size, bool music, Color col) {
    if (music) {
        r.fillCircle({c.x - size * 0.18f, c.y + size * 0.28f}, size * 0.17f, col);
        r.fillRect({c.x - size * 0.18f + size * 0.12f, c.y - size * 0.4f, size * 0.06f, size * 0.68f}, col);
        r.fillRect({c.x - size * 0.18f + size * 0.12f, c.y - size * 0.4f, size * 0.32f, size * 0.07f}, col);
        return;
    }
    r.fillRect({c.x - size * 0.38f, c.y - size * 0.12f, size * 0.18f, size * 0.24f}, col);
    r.fillTriangle({c.x - size * 0.22f, c.y - size * 0.12f}, {c.x + size * 0.02f, c.y - size * 0.34f}, {c.x + size * 0.02f, c.y + size * 0.34f}, col);
    r.fillTriangle({c.x - size * 0.22f, c.y - size * 0.12f}, {c.x + size * 0.02f, c.y + size * 0.34f}, {c.x - size * 0.22f, c.y + size * 0.12f}, col);
    r.drawArc({c.x + size * 0.02f, c.y}, size * 0.26f, std::max(1.0f, size * 0.05f), -0.7f, 0.7f, col);
}

/// A label drawn as given (a path, a file name, an id): never translated.
void rawLabel(UIContext& ui, const std::string& t, Vec2 pos, float size, Color c, Align a, FontFace face, float room) {
    i18n::Verbatim raw;
    ui.label(ui.elide(t, face, size, room), pos, size, c, a, face, VAlign::Middle);
}

/// `w` × `h` fitted in `box`, whole scales for what is smaller (sharp pixels), centred.
Rect fitIn(float w, float h, const Rect& box) {
    if (w <= 0.0f || h <= 0.0f) return {box.center().x, box.center().y, 0, 0};
    float k = std::min(box.w / w, box.h / h);
    if (k >= 1.0f) k = std::min(8.0f, std::floor(k));
    return Rect::fromCenter(box.center(), {w * k, h * k});
}

}  // namespace

AdminAssets::AdminAssets(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<void(const std::string&)> openEnemy)
    : g_(g), say_(std::move(say)), openEnemy_(std::move(openEnemy)), preview_(g) {}

void AdminAssets::enter() {
    if (!cat_) cat_ = sharedAssetCatalog(g_.content);  // starts reading the assets (worker thread)
}

void AdminAssets::leave() { preview_.stopAudio(); }

void AdminAssets::open(const std::string& path) {
    pendingOpen_ = path;
    enter();
}

void AdminAssets::refilter() {
    dirty_ = false;
    applied_ = search_;
    if (!cat_) return;
    AssetQuery q;
    q.text = search_;
    q.types = type_ <= 0 ? kAllAssetTypes : assetTypeBit(static_cast<AssetType>(type_ - 1));
    q.unusedOnly = unusedOnly_;
    q.folder = folder_;
    if (user_) {
        q.userKind = user_->kind;
        q.userId = user_->id;
    }
    results_ = cat_->filter(q);
    resultBytes_ = 0;
    for (int i : results_) resultBytes_ += cat_->assets()[static_cast<size_t>(i)].bytes;
}

std::string AdminAssets::folderFeature(int folder) const {
    return NewBadges::assetFolderFeature(cat_ && folder > 0 ? cat_->folders()[static_cast<size_t>(folder)].key : std::string());
}

void AdminAssets::linkBadges() {
    // Each asset that can be new (NewBadges, from data/assets/asset_versions.json) leads to its
    // folder, each folder to the one holding it, the root to the tab (and the tab to the menu,
    // data/news/new_features.json): a folder keeps its pill while anything under it is new.
    if (badgesLinked_ || !cat_) return;
    badgesLinked_ = true;
    NewBadges& badges = g_.newBadges;
    for (const AssetEntry& e : cat_->assets()) {
        const std::string id = NewBadges::assetFeature(e.path);
        if (!badges.has(id)) continue;
        badges.link(id, {folderFeature(e.folder)});
        for (int f = e.folder; f > 0;) {
            const int up = cat_->folders()[static_cast<size_t>(f)].parent;
            badges.link(folderFeature(f), {folderFeature(std::max(0, up))});
            f = up;
        }
    }
    badges.link(folderFeature(0), {"admin.tab.assets"});
}

void AdminAssets::select(int index, bool reveal) {
    if (!cat_ || index < 0 || index >= static_cast<int>(cat_->assets().size())) return;
    const AssetEntry& e = cat_->assets()[static_cast<size_t>(index)];
    selected_ = e.path;
    preview_.show(cat_, &e);
    if (!reveal) return;
    // Shown wherever the filters were: they are reset, its folder is opened in the tree.
    if (std::find(results_.begin(), results_.end(), index) == results_.end()) {
        search_.clear();
        type_ = 0;
        unusedOnly_ = false;
        user_.reset();
        folder_ = 0;
        refilter();
    }
    for (int f = e.folder; f >= 0; f = cat_->folders()[static_cast<size_t>(f)].parent) expanded_.insert(f);
    scrollToSelected_ = true;
}

// ------------------------------------------------------------------------------ drawing

void AdminAssets::draw(const Rect& in) {
    UIContext& ui = g_.ui;
    preview_.images().pump(g_.app.renderer());
    if (!cat_) cat_ = sharedAssetCatalog(g_.content);
    ui.pushId("assets");
    if (!cat_) {
        // Read on a worker thread: a turning arc meanwhile.
        const Vec2 c{in.center().x, in.center().y - ui.s(20)};
        const float a = ui.time() * 4.0f;
        ui.r().drawArc(c, ui.s(22), ui.s(4), a, a + 4.2f, ui.theme().accent);
        const std::string t = "Lecture des assets installés…";
        const float fs = ui.fitFont(t, FontFace::Body, 18, in.w);
        ui.label(t, {c.x, c.y + ui.s(48)}, fs, ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);
        ui.popId();
        return;
    }
    linkBadges();
    if (!pendingOpen_.empty()) {
        if (dirty_) refilter();
        const int i = cat_->indexOf(pendingOpen_);
        if (i >= 0) select(i, true);
        else say_("Cet asset n'est plus installé.", true);
        pendingOpen_.clear();
    }
    if (dirty_ || applied_ != search_) refilter();
    // A selection the filters keep, else none.
    if (!selected_.empty() && !cat_->find(selected_)) selected_.clear();
    if (selected_.empty() && preview_.shown()) preview_.show(cat_, nullptr);

    const float rowH = ui.s(46), typesH = ui.s(40), gap = ui.s(12);
    drawToolbar({in.x, in.y, in.w, rowH});
    drawTypes({in.x, in.y + rowH + ui.s(10), in.w, typesH});
    const float top = in.y + rowH + ui.s(10) + typesH + gap;
    const Rect body{in.x, top, in.w, in.bottom() - top};
    float treeW = std::clamp(body.w * 0.2f, ui.s(220), ui.s(330));
    float detailW = std::clamp(body.w * 0.33f, ui.s(320), ui.s(560));
    if (body.w - treeW - detailW - 2 * gap < ui.s(300)) {  // narrow: the side columns give way
        const float over = ui.s(300) - (body.w - treeW - detailW - 2 * gap);
        treeW = std::max(ui.s(180), treeW - over * 0.4f);
        detailW = std::max(ui.s(260), detailW - over * 0.6f);
    }
    const float resultsW = std::max(ui.s(120), body.w - treeW - detailW - 2 * gap);
    drawTree({body.x, body.y, treeW, body.h});
    drawResults({body.x + treeW + gap, body.y, resultsW, body.h});
    drawDetail({body.right() - detailW, body.y, detailW, body.h});
    ui.popId();
}

void AdminAssets::drawToolbar(const Rect& r) {
    UIContext& ui = g_.ui;
    UIContext::ButtonStyle st;
    st.small = true;
    st.fontSize = 16;
    // Right: « Grille » « Liste »; before them « Inutilisés · N »; the search takes the rest.
    const std::string gridT = "Grille", listT = "Liste";
    const float gw = ui.text().measure(gridT, FontFace::Bold, ui.s(16)).x + ui.s(30);
    const float lw = ui.text().measure(listT, FontFace::Bold, ui.s(16)).x + ui.s(30);
    float x = r.right() - lw;
    st.selected = !grid_;
    if (cframe::button(ui, "list", {x, r.y, lw, r.h}, listT, st)) grid_ = false;
    x -= ui.s(8) + gw;
    st.selected = grid_;
    if (cframe::button(ui, "grid", {x, r.y, gw, r.h}, gridT, st)) grid_ = true;
    const std::string unusedT = "Inutilisés · " + std::to_string(cat_->unusedCount());
    const float uw = ui.text().measure(unusedT, FontFace::Bold, ui.s(16)).x + ui.s(30);
    x -= ui.s(16) + uw;
    st.selected = unusedOnly_;
    if (cframe::button(ui, "unused", {x, r.y, uw, r.h}, unusedT, st)) {
        unusedOnly_ = !unusedOnly_;
        dirty_ = true;
    }
    const float sw = std::max(ui.s(120), x - ui.s(16) - r.x);
    const std::string placeholder = "Rechercher : nom, chemin, dossier, type, ennemi, niveau…";
    ui.textField("search", {r.x, r.y, sw, r.h}, search_, ui.elide(placeholder, FontFace::Body, 20, sw - ui.s(24)), 80);
}

void AdminAssets::drawTypes(const Rect& r) {
    UIContext& ui = g_.ui;
    // The results' count at the right when there is room for it, the chips before it.
    const std::string summary = countText(static_cast<int>(results_.size())) + " · " + formatBytes(resultBytes_);
    const float fs = 16.0f;
    const float sw = ui.text().measure(summary, FontFace::Body, ui.s(fs)).x;
    std::vector<std::string> labels(std::begin(kTypeChips), std::end(kTypeChips));
    float need = 0.0f;
    for (const std::string& l : labels) need += ui.text().measure(l, FontFace::Bold, ui.s(12)).x + ui.s(38);
    const bool roomForSummary = r.x + need + ui.s(20) + sw <= r.right();
    int t = type_;
    chips(ui, "type", r.x, r.y, r.h, labels, t, roomForSummary ? r.right() - sw - ui.s(20) : r.right());
    if (t != type_) {
        type_ = t;
        dirty_ = true;
    }
    if (roomForSummary) ui.label(summary, {r.right(), r.center().y}, fs, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
}

void AdminAssets::drawTree(const Rect& r) {
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    cframe::panel(ui, r, false, 12);
    const Rect in{r.x + ui.s(10), r.y + ui.s(10), r.w - ui.s(20), r.h - ui.s(20)};
    const std::string head = "Rangement";
    const float hs = ui.fitFont(head, FontFace::Bold, 18, in.w);
    ui.label(head, {in.x + ui.s(4), in.y + ui.s(14)}, hs, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
    const Rect view{in.x, in.y + ui.s(36), in.w, in.h - ui.s(36)};
    // The rows shown: the open folders' children.
    const auto& folders = cat_->folders();
    std::vector<int> rows;
    std::function<void(int)> walk = [&](int f) {
        rows.push_back(f);
        if (f == 0 || expanded_.count(f))
            for (int c : folders[static_cast<size_t>(f)].children) walk(c);
    };
    walk(0);
    const float rowH = ui.s(38), gap = ui.s(4);
    ui.beginScroll("tree", view, static_cast<float>(rows.size()) * (rowH + gap));
    float y = view.y - ui.scrollOffset();
    const float w = view.w - ui.s(14);  // the scroll bar's lane stays free
    for (int f : rows) {
        const AssetFolder& d = folders[static_cast<size_t>(f)];
        const Rect row{view.x, y, w, rowH};
        y += rowH + gap;
        bool hovered = false;
        const bool hit = ui.hotspot("f" + d.key, row, &hovered);
        const bool open = f == 0 || expanded_.count(f) > 0;
        if (hit) {
            if (folder_ != f) {
                folder_ = f;
                if (!d.children.empty()) expanded_.insert(f);
            } else if (f != 0 && !d.children.empty()) {
                if (open) expanded_.erase(f);
                else expanded_.insert(f);
            }
            dirty_ = true;
            ui.app().resources().playSound("sounds/ui_move.wav", 0.5f);
        }
        if (row.bottom() < view.y || row.y > view.bottom()) continue;
        // 2.69: « NOUVEAU » while an asset under this folder is new (its own focus changes nothing).
        const bool fresh = ui.newBadge("f" + d.key, row, folderFeature(f), false, true);
        const bool sel = folder_ == f;
        const bool focus = ui.focused("f" + d.key);
        if (sel || hovered || focus)
            rr.fillRoundedRect(row, ui.s(6), sel ? Color{30, 70, 90, 235} : Color{28, 40, 70, 200});
        if (focus) rr.drawRoundedRect(row, ui.s(6), ui.theme().accent.withAlphaF(0.8f), ui.s(1.5f));
        const float indent = static_cast<float>(std::max(0, d.depth - 1)) * ui.s(14) + (f == 0 ? 0.0f : ui.s(10));
        float x = row.x + ui.s(8) + indent;
        if (!d.children.empty() && f != 0) {
            const float a = ui.s(5);
            const Vec2 c{x + a, row.center().y};
            if (open) rr.fillTriangle({c.x - a, c.y - a * 0.6f}, {c.x + a, c.y - a * 0.6f}, {c.x, c.y + a * 0.8f}, ui.theme().textDim);
            else rr.fillTriangle({c.x - a * 0.6f, c.y - a}, {c.x - a * 0.6f, c.y + a}, {c.x + a * 0.8f, c.y}, ui.theme().textDim);
        }
        x += ui.s(18);
        // The count and the weight first (their own place at the right), the « NOUVEAU » pill
        // before them, the name in what is left; when the row is short the weight goes, then the count.
        std::string facts = std::to_string(d.count()) + " · " + formatBytes(d.bytes);
        const float ffs = fz(ui, 13);
        const float pillW = fresh ? ui.newPillWidth(true) + ui.s(8) : 0.0f;
        float fw = ui.text().measure(facts, FontFace::Body, ui.s(ffs)).x;
        auto nameRoom = [&] { return row.right() - ui.s(10) - (facts.empty() ? 0.0f : fw + ui.s(8)) - pillW - x; };
        if (nameRoom() < ui.s(70)) {
            facts = std::to_string(d.count());
            fw = ui.text().measure(facts, FontFace::Body, ui.s(ffs)).x;
        }
        if (fresh && nameRoom() < ui.s(30)) facts.clear();
        const float room = nameRoom();
        if (!facts.empty())
            ui.label(facts, {row.right() - ui.s(10), row.center().y}, ffs, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
        if (fresh) ui.drawNewPill(row.right() - ui.s(10) - (facts.empty() ? 0.0f : fw + ui.s(8)), row.center().y, true);
        const FontFace face = sel ? FontFace::Bold : FontFace::Body;
        if (room > ui.s(12)) {
            const float ns = ui.fitFont(d.name, face, 16, room, 0.8f);
            ui.label(ui.elide(d.name, face, ns, room), {x, row.center().y}, ns, sel ? Color::white() : ui.theme().text, Align::Left, face, VAlign::Middle);
        }
    }
    ui.endScroll();
}

void AdminAssets::drawResults(const Rect& r) {
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    cframe::panel(ui, r, false, 12);
    const Rect in{r.x + ui.s(12), r.y + ui.s(10), r.w - ui.s(24), r.h - ui.s(20)};
    // Where we are: the folder's trail (each name translated, joined), then the user filter.
    std::string trail;
    if (folder_ <= 0) trail = i18n::tr("Tous les assets");
    else
        for (int f = folder_; f > 0; f = cat_->folders()[static_cast<size_t>(f)].parent)
            trail = i18n::tr(cat_->folders()[static_cast<size_t>(f)].name) + (trail.empty() ? "" : " › " + trail);
    float y = in.y;
    {
        const float ts = ui.fitFont(trail, FontFace::Bold, 18, in.w, 0.8f);
        rawLabel(ui, trail, {in.x, y + ui.s(14)}, ts, ui.theme().text, Align::Left, FontFace::Bold, in.w);
        y += ui.s(34);
    }
    if (user_) {
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = 15;
        const std::string clearT = "Retirer";
        const float bw = ui.text().measure(clearT, FontFace::Bold, ui.s(15)).x + ui.s(30);
        const float bh = ui.s(36);
        if (cframe::button(ui, "nouser", {in.right() - bw, y, bw, bh}, clearT, st)) {
            user_.reset();
            dirty_ = true;
        }
        // « Utilisés par : Ennemi · Golem », the name as the game shows it (or as it is).
        const std::string lead = i18n::tr("Utilisés par :") + " " + i18n::tr(assetUseKindLabel(user_->kind)) + " · ";
        const std::string name = user_->literal ? user_->name : i18n::tr(user_->name);
        const std::string all = lead + name;
        const float room = in.right() - bw - ui.s(12) - in.x;
        const float fs = ui.fitFont(all, FontFace::Body, 16, room, 0.75f);
        const Rect pill{in.x, y, std::min(room, ui.text().measure(all, FontFace::Body, ui.s(fs)).x + ui.s(20)), bh};
        rr.fillRoundedRect(pill, ui.s(8), ui.theme().accent.withAlphaF(0.12f));
        rr.drawRoundedRect(pill, ui.s(8), ui.theme().accent.withAlphaF(0.6f), ui.s(1));
        rawLabel(ui, all, {pill.x + ui.s(10), pill.center().y}, fs, ui.theme().text, Align::Left, FontFace::Body, pill.w - ui.s(20));
        y += bh + ui.s(10);
    }
    const Rect view{in.x, y, in.w, in.bottom() - y};
    drawnItems_ = 0;
    if (results_.empty()) {
        const std::string t = unusedOnly_ ? "Aucun asset inutilisé ici." : "Aucun asset ne correspond.";
        const float fs = ui.fitFont(t, FontFace::Body, 18, view.w);
        ui.label(t, {view.center().x, view.y + ui.s(40)}, fs, ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);
        return;
    }
    if (grid_) drawGrid(view);
    else drawList(view);
}

void AdminAssets::drawThumb(const AssetEntry& e, const Rect& box) {
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    rr.fillRoundedRect(box, ui.s(6), Color{6, 10, 22, 255});
    const Rect inner{box.x + ui.s(4), box.y + ui.s(4), box.w - ui.s(8), box.h - ui.s(8)};
    switch (e.type) {
        case AssetType::Image:
        case AssetType::Animation: {
            if (TexturePtr t = preview_.images().thumbnail(*cat_, e)) {
                rr.drawTexture(*t, nullptr, fitIn(static_cast<float>(t->width()), static_cast<float>(t->height()), inner));
            } else if (!preview_.images().failed(e.type == AssetType::Animation ? e.sheet : e.path)) {
                const float p = 0.5f + 0.5f * std::sin(ui.time() * 5.0f);
                rr.fillRoundedRect(inner.expanded(-inner.w * 0.3f), ui.s(4), Color{40, 52, 80, static_cast<uint8_t>(90 + 80 * p)});
            }
            return;
        }
        case AssetType::Sound:
        case AssetType::Music:
            audioSymbol(rr, inner.center(), std::min(inner.w, inner.h) * 0.7f, e.type == AssetType::Music, ui.theme().textDim);
            return;
        case AssetType::Font: {
            const int px = std::max(10, static_cast<int>(inner.h * 0.42f));
            if (TexturePtr t = preview_.images().fontSample(rr, *cat_, e, "Aa", px)) {
                const float k = std::min({1.0f, inner.w / static_cast<float>(t->width()), inner.h / static_cast<float>(t->height())});
                rr.drawTexture(*t, nullptr, Rect::fromCenter(inner.center(), {t->width() * k, t->height() * k}), ui.theme().text);
            }
            return;
        }
        case AssetType::Data:
        case AssetType::Count: {
            const bool json = e.name.size() > 5 && e.name.compare(e.name.size() - 5, 5, ".json") == 0;
            const std::string t = json ? "{ }" : "TXT";
            const float fs = ui.fitFont(t, FontFace::Bold, std::max(12.0f, inner.h / ui.s(1.0f) * 0.3f), inner.w);
            rawLabel(ui, t, inner.center(), fs, ui.theme().textDim, Align::Center, FontFace::Bold, inner.w);
            return;
        }
    }
}

std::string AdminAssets::assetLine(const AssetEntry& e) const {
    switch (e.type) {
        case AssetType::Image: return e.width > 0 ? std::to_string(e.width) + " × " + std::to_string(e.height) : std::string(assetTypeLabel(e.type));
        case AssetType::Animation: return e.frames == 1 ? std::string("1 image") : std::to_string(e.frames) + " images";
        case AssetType::Sound:
        case AssetType::Music: return formatDuration(e.durationMs);
        default: return formatBytes(e.bytes);
    }
}

void AdminAssets::drawGrid(const Rect& view) {
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    const float gap = ui.s(10), lane = ui.s(14);
    const float wanted = ui.s(128);
    const int cols = std::max(1, static_cast<int>((view.w - lane + gap) / (wanted + gap)));
    const float cellW = (view.w - lane - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    const float thumb = std::max(ui.s(40), std::min(cellW - ui.s(16), ui.s(108)));
    const float nameS = fz(ui, 14), infoS = fz(ui, 13);
    const float cellH = ui.s(8) + thumb + ui.s(10) + ui.s(nameS) + ui.s(6) + ui.s(infoS) + ui.s(10);
    // 2.69: a new asset's « NOUVEAU » pill sits on its tile's top edge: the rows are far enough
    // apart for it to stay clear of the tile above, the first one low enough for it to stay in view.
    const float pillH = ui.newPillHeight(true);
    const float top = pillH * 0.5f + ui.s(2);
    const float pitch = cellH + std::max(gap, pillH * 0.5f + ui.s(3));
    const int n = static_cast<int>(results_.size());
    const int rows = (n + cols - 1) / cols;
    if (scrollToSelected_) {
        const int p = cat_ ? static_cast<int>(std::find(results_.begin(), results_.end(), cat_->indexOf(selected_)) - results_.begin()) : n;
        if (p < n) {
            ui.setScroll("grid", top + static_cast<float>(p / cols) * pitch - view.h * 0.3f);
            ui.setFocus("cell" + selected_);
        }
        scrollToSelected_ = false;
    }
    ui.beginScroll("grid", view, top + static_cast<float>(rows) * pitch);
    const float off = ui.scrollOffset();
    // Only the rows on screen are drawn (their thumbnails asked); one more above and below
    // takes the focus (the keyboard and the gamepad move on, the list follows).
    const int firstRow = std::max(0, static_cast<int>((off - top) / pitch) - 1);
    const int lastRow = std::min(rows - 1, static_cast<int>((off + view.h) / pitch) + 1);
    for (int row = firstRow; row <= lastRow; ++row)
        for (int c = 0; c < cols; ++c) {
            const int k = row * cols + c;
            if (k >= n) break;
            const int idx = results_[static_cast<size_t>(k)];
            const AssetEntry& e = cat_->assets()[static_cast<size_t>(idx)];
            const Rect cell{view.x + static_cast<float>(c) * (cellW + gap), view.y - off + top + static_cast<float>(row) * pitch, cellW, cellH};
            const std::string name = "cell" + e.path;
            bool hovered = false;
            if (ui.hotspot(name, cell, &hovered)) select(idx, false);
            const bool focus = ui.focused(name);
            if (focus && !ui.usingPointer() && selected_ != e.path) select(idx, false);
            // Also for the tiles just out of view (the focus comes to them from there).
            const bool fresh = ui.newBadge(name, cell, NewBadges::assetFeature(e.path), false, true);
            if (cell.bottom() < view.y || cell.y > view.bottom()) continue;
            ++drawnItems_;
            const bool sel = selected_ == e.path;
            rr.fillRoundedRect(cell, ui.s(8), sel ? Color{26, 50, 76, 245} : hovered || focus ? Color{24, 34, 62, 240} : Color{16, 24, 46, 225});
            if (sel || focus) rr.drawRoundedRect(cell, ui.s(8), ui.theme().accent.withAlphaF(sel ? 0.9f : 0.6f), ui.s(focus ? 2.0f : 1.0f));
            drawThumb(e, {cell.center().x - thumb * 0.5f, cell.y + ui.s(8), thumb, thumb});
            const float ty = cell.y + ui.s(8) + thumb + ui.s(10);
            const float room = cell.w - ui.s(12);
            rawLabel(ui, e.name, {cell.center().x, ty + ui.s(nameS) * 0.5f}, nameS, ui.theme().text, Align::Center, FontFace::Body, room);
            const bool unused = e.unused();
            const std::string info = unused ? std::string("Inutilisé") : assetLine(e);
            const float iy = ty + ui.s(nameS) + ui.s(6) + ui.s(infoS) * 0.5f;
            ui.label(ui.elide(info, FontFace::Body, infoS, room), {cell.center().x, iy}, infoS, unused ? ui.theme().warm : ui.theme().textDim, Align::Center,
                     FontFace::Body, VAlign::Middle);
            if (fresh) ui.drawNewPill(cell.right() - ui.s(8), cell.y, true);
        }
    ui.endScroll();
}

void AdminAssets::drawList(const Rect& view) {
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    const float rowH = ui.s(58), gap = ui.s(6), pitch = rowH + gap, lane = ui.s(14);
    const int n = static_cast<int>(results_.size());
    if (scrollToSelected_) {
        const int p = static_cast<int>(std::find(results_.begin(), results_.end(), cat_->indexOf(selected_)) - results_.begin());
        if (p < n) {
            ui.setScroll("list", static_cast<float>(p) * pitch - view.h * 0.3f);
            ui.setFocus("row" + selected_);
        }
        scrollToSelected_ = false;
    }
    // Columns at the right (type, size, uses), measured once; dropped when the row is narrow.
    const float cs = fz(ui, 14);
    float typeW = 0.0f;
    for (int t = 0; t < static_cast<int>(AssetType::Count); ++t)
        typeW = std::max(typeW, ui.text().measure(assetTypeLabel(static_cast<AssetType>(t)), FontFace::Body, ui.s(cs)).x);
    typeW += ui.s(14);
    const float sizeW = ui.text().measure("999,9 Mo", FontFace::Body, ui.s(cs)).x + ui.s(14);
    const float usesW = std::max(ui.text().measure("Inutilisé", FontFace::Body, ui.s(cs)).x, ui.text().measure(usesText(999), FontFace::Body, ui.s(cs)).x) + ui.s(14);
    const float w = view.w - lane;
    const float thumb = rowH - ui.s(12);
    const float textMin = ui.s(160);
    const float leftW = ui.s(10) + thumb + ui.s(12);
    const bool showUses = w - leftW - (typeW + sizeW + usesW) >= textMin;
    const bool showType = w - leftW - (sizeW + (showUses ? usesW : 0.0f) + typeW) >= textMin;
    const float rightW = sizeW + (showUses ? usesW : 0.0f) + (showType ? typeW : 0.0f);
    ui.beginScroll("list", view, static_cast<float>(n) * pitch);
    const float off = ui.scrollOffset();
    const int first = std::max(0, static_cast<int>(off / pitch) - 1);
    const int last = std::min(n - 1, static_cast<int>((off + view.h) / pitch) + 1);
    for (int k = first; k <= last; ++k) {
        const int idx = results_[static_cast<size_t>(k)];
        const AssetEntry& e = cat_->assets()[static_cast<size_t>(idx)];
        const Rect row{view.x, view.y - off + static_cast<float>(k) * pitch, w, rowH};
        const std::string name = "row" + e.path;
        bool hovered = false;
        if (ui.hotspot(name, row, &hovered)) select(idx, false);
        const bool focus = ui.focused(name);
        if (focus && !ui.usingPointer() && selected_ != e.path) select(idx, false);
        // Also for the rows just out of view (the focus comes to them from there).
        const bool fresh = ui.newBadge(name, row, NewBadges::assetFeature(e.path), false, true);
        if (row.bottom() < view.y || row.y > view.bottom()) continue;
        ++drawnItems_;
        const bool sel = selected_ == e.path;
        rr.fillRoundedRect(row, ui.s(8), sel ? Color{26, 50, 76, 245} : hovered || focus ? Color{24, 34, 62, 240} : Color{16, 24, 46, 225});
        if (sel || focus) rr.drawRoundedRect(row, ui.s(8), ui.theme().accent.withAlphaF(sel ? 0.9f : 0.6f), ui.s(focus ? 2.0f : 1.0f));
        drawThumb(e, {row.x + ui.s(10), row.y + ui.s(6), thumb, thumb});
        const float tx = row.x + leftW;
        const float room = row.right() - rightW - ui.s(10) - tx;
        // The name, then the « NOUVEAU » pill right after it (the name gives way to it).
        const float nameY = row.y + rowH * 0.32f;
        const float pillW = fresh ? ui.newPillWidth(true) + ui.s(8) : 0.0f;
        const float nameRoom = room - pillW;
        float nameW = 0.0f;
        if (nameRoom > ui.s(12)) {
            rawLabel(ui, e.name, {tx, nameY}, 16, ui.theme().text, Align::Left, FontFace::Bold, nameRoom);
            i18n::Verbatim raw;
            nameW = ui.text().measure(ui.elide(e.name, FontFace::Bold, 16, nameRoom), FontFace::Bold, ui.s(16)).x;
        }
        if (fresh) ui.drawNewPill(tx + nameW + pillW, nameY, true);
        rawLabel(ui, e.path, {tx, row.y + rowH * 0.7f}, fz(ui, 13), ui.theme().textDim, Align::Left, FontFace::Body, room);
        float x = row.right() - ui.s(10);
        auto column = [&](const std::string& t, float cw, Color c) {
            ui.label(ui.elide(t, FontFace::Body, cs, cw - ui.s(14)), {x, row.center().y}, cs, c, Align::Right, FontFace::Body, VAlign::Middle);
            x -= cw;
        };
        if (showUses) column(e.unused() ? std::string("Inutilisé") : usesText(e.useCount()), usesW, e.unused() ? ui.theme().warm : ui.theme().textDim);
        column(formatBytes(e.bytes), sizeW, ui.theme().textDim);
        if (showType) column(assetTypeLabel(e.type), typeW, ui.theme().textDim);
    }
    ui.endScroll();
}

void AdminAssets::drawDetail(const Rect& r) {
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    cframe::panel(ui, r, false, 12);
    const Rect in{r.x + ui.s(16), r.y + ui.s(14), r.w - ui.s(32), r.h - ui.s(28)};
    const AssetEntry* e = selected_.empty() ? nullptr : cat_->find(selected_);
    if (!e) {
        ui.labelWrapped("Choisissez un asset : son aperçu, ce que l'on sait de lui et ce qui l'utilise apparaissent ici.", {in.x, in.y + ui.s(6)}, in.w, 17,
                        ui.theme().textDim);
        return;
    }
    if (preview_.shown() != e) preview_.show(cat_, e);
    rr.pushClip(r);  // nothing of the sheet ever leaves its panel
    float y = in.y;
    rawLabel(ui, e->name, {in.x, y + ui.s(13)}, ui.fitFont(e->name, FontFace::Bold, 20, in.w, 0.75f), ui.theme().text, Align::Left, FontFace::Bold, in.w);
    y += ui.s(30);
    rawLabel(ui, e->path, {in.x, y + ui.s(9)}, fz(ui, 14), ui.theme().textDim, Align::Left, FontFace::Body, in.w);
    y += ui.s(26);

    // What is known of it: « key   value » lines. The values are made of translated pieces and
    // of what is shown as it is (sizes, file names, clip names): drawn without translation.
    std::vector<std::pair<std::string, std::string>> facts;  // French key, value as shown
    auto fact = [&](const std::string& k, const std::string& v) { facts.emplace_back(k, v); };
    fact("Type", i18n::tr(assetTypeLabel(e->type)));
    fact("Taille", i18n::tr(formatBytes(e->bytes)));
    if (e->type == AssetType::Image && e->width > 0) fact("Dimensions", std::to_string(e->width) + " × " + std::to_string(e->height) + " px");
    if (e->type == AssetType::Animation) {
        fact("Une image", std::to_string(e->width) + " × " + std::to_string(e->height) + " px");
        fact("Clips", std::to_string(e->clips.size()) + " · " + i18n::tr(e->frames == 1 ? std::string("1 image") : std::to_string(e->frames) + " images"));
        std::string clips;
        for (const AssetClipInfo& c : e->clips) clips += (clips.empty() ? "" : ", ") + c.name + " (" + std::to_string(c.frames) + ")";
        if (!clips.empty()) fact("", clips);
        fact("Planche", e->sheet);
    }
    if (e->type == AssetType::Sound || e->type == AssetType::Music) {
        fact("Durée", i18n::tr(formatDuration(e->durationMs)));
        if (e->sampleRate > 0) {
            const std::string channels = e->channels == 1 ? i18n::tr("mono") : e->channels == 2 ? i18n::tr("stéréo") : i18n::tr(std::to_string(e->channels) + " canaux");
            fact("Format", std::to_string(e->sampleRate) + " Hz · " + channels);
        }
    }
    {
        std::string trail;
        for (int f = e->folder; f > 0; f = cat_->folders()[static_cast<size_t>(f)].parent)
            trail = i18n::tr(cat_->folders()[static_cast<size_t>(f)].name) + (trail.empty() ? "" : " › " + trail);
        fact("Dossier", trail.empty() ? i18n::tr("Tous les assets") : trail);
    }
    const float lineH = ui.s(24);
    const float factsH = static_cast<float>(facts.size()) * lineH;
    const float buttonH = ui.s(44);
    const float usesHeadH = ui.s(34);
    const float usesMin = ui.s(96);
    // The preview takes what the rest leaves, between 110 and 420 pixels (at 1080p).
    const float rest = in.bottom() - y - ui.s(12) - factsH - ui.s(10) - usesHeadH - usesMin - ui.s(12) - buttonH;
    const float previewH = std::max(ui.s(110), std::min(rest, std::min(ui.s(420), in.w * 0.95f + preview_.controlsHeight())));
    preview_.draw({in.x, y, in.w, previewH}, g_.app.frameTime() / 1000.0f);
    y += previewH + ui.s(12);
    float keyW = 0.0f;
    const float ks = fz(ui, 14), vs = fz(ui, 15);
    for (const auto& [k, v] : facts)
        if (!k.empty()) keyW = std::max(keyW, ui.text().measure(k, FontFace::Body, ui.s(ks)).x);
    keyW = std::min(keyW + ui.s(14), in.w * 0.4f);
    for (size_t i = 0; i < facts.size(); ++i) {
        const float cy = y + lineH * 0.5f;
        if (!facts[i].first.empty())
            ui.label(ui.elide(facts[i].first, FontFace::Body, ks, keyW - ui.s(10)), {in.x, cy}, ks, ui.theme().textDim, Align::Left, FontFace::Body,
                     VAlign::Middle);
        rawLabel(ui, facts[i].second, {in.x + keyW, cy}, vs, ui.theme().text, Align::Left, FontFace::Body, in.w - keyW);
        y += lineH;
    }
    y += ui.s(10);
    // « Ouvrir le dossier » at the bottom; « Utilisé par » between.
    const float usesH = in.bottom() - buttonH - ui.s(12) - y;
    if (usesH >= ui.s(64)) drawUses(*e, {in.x, y, in.w, usesH});  // (never over the button, on a very short screen)
    UIContext::ButtonStyle bs;
    bs.small = true;
    bs.fontSize = 16;
    const std::string openT = "Ouvrir le dossier";
    const float bw = std::min(in.w, ui.text().measure(openT, FontFace::Bold, ui.s(16)).x + ui.s(40));
    if (cframe::button(ui, "folder", {in.x, in.bottom() - buttonH, bw, buttonH}, openT, bs)) openFolder(*e);
    const float hintRoom = in.w - bw - ui.s(12);
    if (hintRoom > ui.s(60)) {
        const std::string hint = "Lecture seule : rien n'est modifié ici.";
        const float hs = ui.fitFont(hint, FontFace::Body, fz(ui, 14), hintRoom, 0.8f);
        ui.label(ui.elide(hint, FontFace::Body, hs, hintRoom), {in.x + bw + ui.s(12), in.bottom() - buttonH * 0.5f}, hs, ui.theme().textDim, Align::Left,
                 FontFace::Body, VAlign::Middle);
    }
    rr.popClip();
}

void AdminAssets::drawUses(const AssetEntry& e, const Rect& r) {
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    const std::string head = "Utilisé par";
    const float hs = ui.fitFont(head, FontFace::Bold, 17, r.w * 0.6f);
    ui.label(head, {r.x, r.y + ui.s(14)}, hs, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
    const float headW = ui.text().measure(head, FontFace::Bold, ui.s(hs)).x;
    float top = r.y + ui.s(34);
    if (e.unused()) {
        // Nothing of the game names it (a sheet: only its unused description does, listed below).
        const std::string t = "Inutilisé : rien dans le jeu ne le nomme.";
        const float fs = ui.fitFont(t, FontFace::Body, 15, r.w, 0.75f);
        ui.label(ui.elide(t, FontFace::Body, fs, r.w), {r.x, top + ui.s(10)}, fs, ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
        top += ui.s(28);
        if (e.uses.empty() || top + ui.s(52) > r.bottom()) return;
    } else {
        const std::string n = usesText(e.useCount());
        const float room = r.w - headW - ui.s(12);
        const float ns = fz(ui, 14);
        if (room > ui.s(30))
            ui.label(ui.elide(n, FontFace::Body, ns, room), {r.right(), r.y + ui.s(14)}, ns, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
    }
    const Rect view{r.x, top, r.w, r.bottom() - top};
    const float rowH = ui.s(52), gap = ui.s(5);
    ui.beginScroll("uses", view, static_cast<float>(e.uses.size()) * (rowH + gap));
    float y = view.y - ui.scrollOffset();
    const float w = view.w - ui.s(14);
    UIContext::ButtonStyle bs;
    bs.small = true;
    bs.fontSize = 15;
    const std::string sheetT = "Fiche";
    const float sheetW = ui.text().measure(sheetT, FontFace::Bold, ui.s(15)).x + ui.s(28);
    const float ks = fz(ui, 13), ns = fz(ui, 16);
    for (size_t i = 0; i < e.uses.size(); ++i) {
        const AssetUse& u = e.uses[i];
        const Rect row{view.x, y, w, rowH};
        y += rowH + gap;
        const bool sheet = hasSheet(u);
        const Rect sb{row.right() - ui.s(6) - sheetW, row.center().y - ui.s(17), sheetW, ui.s(34)};
        const Rect area{row.x, row.y, (sheet ? sb.x - ui.s(6) : row.right()) - row.x, row.h};
        const std::string id = "use" + std::to_string(i);
        bool hovered = false;
        if (ui.hotspot(id, area, &hovered)) {
            if (u.kind == AssetUseKind::Descriptor) {
                const int k = cat_->indexOf(u.id);  // the JSON describing it: shown in its turn
                if (k >= 0) select(k, true);
            } else {
                user_ = User{u.kind, u.id, u.name, u.literal};
                folder_ = 0;
                dirty_ = true;
            }
            ui.app().resources().playSound("sounds/ui_move.wav", 0.5f);
        }
        if (sheet && cframe::button(ui, id + "s", sb, sheetT, bs)) openSheet(u);
        if (row.bottom() < view.y || row.y > view.bottom()) continue;
        const bool focus = ui.focused(id);
        rr.fillRoundedRect(area, ui.s(6), hovered || focus ? Color{28, 40, 70, 220} : Color{18, 26, 50, 210});
        if (focus) rr.drawRoundedRect(area, ui.s(6), ui.theme().accent.withAlphaF(0.8f), ui.s(1.5f));
        const float tx = area.x + ui.s(10);
        // « ×N » at the right of the area, the kind above the name.
        const std::string times = "×" + std::to_string(u.count);
        const float cw = ui.text().measure(times, FontFace::Bold, ui.s(ks)).x;
        const float room = area.right() - ui.s(10) - tx;
        ui.label(times, {area.right() - ui.s(10), row.y + rowH * 0.3f}, ks, ui.theme().accent, Align::Right, FontFace::Bold, VAlign::Middle);
        const std::string kind = assetUseKindLabel(u.kind);
        ui.label(ui.elide(kind, FontFace::Body, ks, room - cw - ui.s(8)), {tx, row.y + rowH * 0.3f}, ks, ui.theme().textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
        if (u.literal) rawLabel(ui, u.name, {tx, row.y + rowH * 0.7f}, ns, ui.theme().text, Align::Left, FontFace::Body, room);
        else ui.label(ui.elide(u.name, FontFace::Body, ns, room), {tx, row.y + rowH * 0.7f}, ns, ui.theme().text, Align::Left, FontFace::Body, VAlign::Middle);
    }
    ui.endScroll();
}

// ------------------------------------------------------------------------------ actions

bool AdminAssets::hasSheet(const AssetUse& u) const {
    switch (u.kind) {
        case AssetUseKind::Boss:
        case AssetUseKind::Planet:
        case AssetUseKind::Story:
        case AssetUseKind::Power: return true;
        case AssetUseKind::Enemy: return static_cast<bool>(openEnemy_);
        case AssetUseKind::Level: return !g_.content.campaignPlanetOf(u.id).empty();
        case AssetUseKind::Cosmetic: return true;  // the Vestiaire of the Collection
        default: return false;
    }
}

void AdminAssets::openSheet(const AssetUse& u) {
    preview_.stopAudio();
    const std::string back = selected_;  // « Retour » on the sheet comes back to this asset
    switch (u.kind) {
        case AssetUseKind::Boss: g_.app.switchScene(makeBossStats(g_, u.id, back), 0.3f); return;
        case AssetUseKind::Level: g_.app.switchScene(makeLevelSelect(g_, g_.content.campaignPlanetOf(u.id), u.id, {}, back), 0.3f); return;
        case AssetUseKind::Planet: g_.app.switchScene(makeLevelSelect(g_, u.id, {}, {}, back), 0.3f); return;
        case AssetUseKind::Enemy:
            if (openEnemy_) openEnemy_(u.id);
            return;
        case AssetUseKind::Cosmetic: g_.app.pushScene(makeCollectionOn(g_, u.id)); return;  // over this screen: « Retour » comes back
        case AssetUseKind::Story:
            if (auto s = makeBossStory(g_, u.id)) g_.app.pushScene(std::move(s));
            else say_("Cette histoire ne se lit pas dans ce jeu.", true);
            return;
        case AssetUseKind::Power:
            g_.onlineOpen = "power:" + u.id;  // the guide opens on that power's sheet
            g_.app.pushScene(makePowerGuide(g_));
            return;
        default: return;
    }
}

void AdminAssets::openFolder(const AssetEntry& e) {
    // The folder holding the file, in the system's file manager.
    std::string path = Paths::toUtf8(cat_->absolutePath(e).parent_path());
    std::replace(path.begin(), path.end(), '\\', '/');
    const std::string url = (path.rfind('/', 0) == 0 ? "file://" : "file:///") + path;
    if (SDL_OpenURL(url.c_str())) say_("Dossier de l'asset ouvert.", false);
    else say_("Le dossier ne s'ouvre pas sur cet ordinateur.", true);
}

}  // namespace cosmic
