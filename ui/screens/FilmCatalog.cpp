#include "ui/screens/FilmCatalog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <tuple>

#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/content/BossStories.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/progression/Progression.h"
#include "game/tutorial/Tutorial.h"
#include "ui/screens/AsterionFilm.h"
#include "ui/screens/BossStoryScene.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/TutorialScenes.h"

namespace cosmic {

namespace {

constexpr const char* kKindStory = "Histoire de giga-boss";
constexpr const char* kKindAsterion = "Film d'Asterion";

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool matches(const FilmEntry& e, const std::string& search) {
    if (search.empty()) return true;
    const std::string q = lower(search);
    for (const std::string* t : {&e.title, &e.kind, &e.where, &e.id}) {
        if (lower(*t).find(q) != std::string::npos) return true;
        if (lower(i18n::tr(*t)).find(q) != std::string::npos) return true;  // as it reads in the language shown
    }
    return false;
}

int groupOf(const FilmEntry& e) {
    if (e.id.rfind("story:", 0) == 0) return 1;
    if (e.id.rfind("asterion_", 0) == 0) return 2;
    return 3;
}

}  // namespace

std::vector<FilmEntry> filmCatalog(GameServices& g, bool forPlayer) {
    std::vector<FilmEntry> out;
    if (!forPlayer)
        for (const auto& [boss, s] : g.stories.all()) {  // every story of data/stories (a new giga boss's comes with its file)
            if (s.shots.empty()) continue;
            FilmEntry e;
            e.id = "story:" + boss;
            const EnemyDefinition* d = g.content.enemy(boss);
            e.title = (d ? d->name : boss) + (s.title.empty() ? std::string() : " — " + s.title);
            e.kind = kKindStory;
            e.where = "Avant le premier combat contre ce giga-boss · Collection › Galerie › Gardiens (« Revoir l'histoire »)";
            e.seconds = story::duration(s);
            e.thumb = s.shots.front().image;
            out.push_back(std::move(e));
        }
    if (auto f = asterionfilm::film()) {
        FilmEntry e;
        e.id = "asterion_film";
        e.title = "ASTERION — " + f->title;
        e.kind = kKindAsterion;
        e.where = "La première fois que le titan apparaît (CLANS › RAIDS, fiche d'un événement) · « Revoir la cinématique » sur la carte des raids";
        e.seconds = story::duration(*f);
        e.thumb = "ui/films/asterion_film.png";
        e.unlocked = !forPlayer || g.progression.asterionFilmSeen;
        e.lock = "Se débloque à sa première projection (CLANS › RAIDS).";
        out.push_back(std::move(e));
    }
    if (auto f = asterionfilm::victory()) {
        FilmEntry e;
        e.id = "asterion_victoire";
        e.title = f->title;
        e.kind = kKindAsterion;
        e.where = "À la chute validée du Cœur, pour toute l'instance · « Revoir la victoire » sur la fiche de l'événement";
        e.seconds = story::duration(*f);
        e.thumb = "ui/films/asterion_victoire.png";
        e.unlocked = !forPlayer || asterionfilm::aftermathUnlocked(g.progression);
        e.lock = "Se débloque quand Asterion tombe : pour ceux qui étaient là, puis pour tous quand le résultat est officiel.";
        out.push_back(std::move(e));
    }
    if (auto f = asterionfilm::aftermath()) {
        FilmEntry e;
        e.id = "asterion_apres";
        e.title = f->title;
        e.kind = kKindAsterion;
        e.where = "Après le film de la victoire, en même temps pour les clans de la chute · fiche de l'événement, fiche récapitulative du raid, Collection › Galerie › Films";
        e.seconds = story::duration(*f);
        e.thumb = "ui/films/asterion_apres.png";
        e.unlocked = !forPlayer || asterionfilm::aftermathUnlocked(g.progression);
        e.lock = "Se débloque quand Asterion tombe : pour ceux qui étaient là, puis pour tous quand le résultat est officiel.";
        out.push_back(std::move(e));
    }
    // 2.82: the transition films after Asterion, teasers of raid 2: open to everyone in the Collection.
    for (const auto& [id, film, where, thumb] :
         {std::tuple{"asterion_balancier", asterionfilm::balancier(),
                     "Collection › Galerie › Films, pour tous : une avant-première du raid 2 (après la chute d'Asterion)", "ui/films/asterion_balancier.png"},
          std::tuple{"asterion_tour", asterionfilm::tour(), "Collection › Galerie › Films, pour tous : une avant-première du raid 2 (si Asterion survit)",
                     "ui/films/asterion_tour.png"}}) {
        if (!film) continue;
        FilmEntry e;
        e.id = id;
        e.title = film->title;
        e.kind = kKindAsterion;
        e.where = where;
        e.seconds = story::duration(*film);
        e.thumb = thumb;
        out.push_back(std::move(e));
    }
    if (g.trailer && !g.trailer->shots.empty()) {
        FilmEntry e;
        e.id = "trailer";
        e.title = "Bande-annonce";
        e.kind = "Bande-annonce";
        e.where = "Menu principal, après une minute sans rien toucher (Paramètres › Jeu › « Bande-annonce à l'accueil après 1 min d'inactivité »)";
        e.seconds = story::duration(*g.trailer);
        e.thumb = "ui/films/trailer.png";
        out.push_back(std::move(e));
    }
    if (!forPlayer && std::filesystem::exists(Paths::dataDir() / tutorial::kLevelFile)) {
        FilmEntry e;
        e.id = "tutorial";
        e.title = "Le premier envol de Nova";
        e.kind = "Didacticiel";
        e.where = "Au premier lancement du jeu, une seule fois (« Bienvenue, Nova ! »)";
        e.tutorial = true;
        out.push_back(std::move(e));
    }
    return out;
}

std::string filmLengthText(const FilmEntry& e) {
    if (e.tutorial) return "environ 6 min, à jouer";
    const int s = std::max(1, static_cast<int>(std::lround(e.seconds)));
    if (s < 60) return std::to_string(s) + " s";
    return std::to_string(s / 60) + " min " + std::to_string(s % 60) + " s";
}

std::unique_ptr<Scene> makeFilmScene(GameServices& g, const FilmEntry& e) {
    if (e.id.rfind("story:", 0) == 0) return makeBossStory(g, e.id.substr(6));  // never marked « seen »: nothing changes
    if (e.id == "asterion_film") return asterionfilm::make(g, false);           // a replay: « vu » is not written
    if (e.id == "asterion_victoire") return asterionfilm::makeVictory(g, false);
    if (e.id == "asterion_apres") return asterionfilm::makeAftermathPreview(g);  // the generic text: no server data here
    if (e.id == "asterion_balancier") return asterionfilm::makeTeaser(g, asterionfilm::Teaser::Balancier);  // 2.82
    if (e.id == "asterion_tour") return asterionfilm::makeTeaser(g, asterionfilm::Teaser::Tour);
    if (e.id == "trailer") return makeTrailer(g, false);
    if (e.id == "tutorial") return makeTutorialReplay(g);  // nothing of it is kept (PlayOptions::tutorialReplay)
    return nullptr;
}

float filmRowHeight(UIContext& ui) { return std::max(ui.s(104), 72.0f); }

FilmRowLayout layoutFilmRow(UIContext& ui, const Rect& row, const FilmEntry& e, bool forPlayer) {
    FilmRowLayout L;
    TextRenderer& tr = ui.text();
    const float pad = ui.s(10);
    const float th = row.h - 2.0f * pad;
    L.thumb = {row.x + pad, row.y + pad, th * 16.0f / 9.0f, th};
    L.buttonText = e.tutorial ? "Rejouer le didacticiel" : (!forPlayer || e.unlocked) ? "Regarder" : "Verrouillé";
    L.metaPx = ui.s(15);
    const float btnPx = ui.s(16);
    float bw = tr.measure(L.buttonText, FontFace::Bold, btnPx).x + ui.s(36);
    bw = std::min(bw, row.w * 0.3f);
    const float bh = std::min(ui.s(44), row.h - 2.0f * pad);
    L.button = {row.right() - pad - bw, row.center().y - bh * 0.5f, bw, bh};
    const float tx = L.thumb.right() + ui.s(14);
    const float room = std::max(0.0f, L.button.x - ui.s(14) - tx);
    // The title: its size fitted to the room (down to 70 %), then shortened.
    const std::string title = i18n::tr(e.title);
    i18n::Verbatim asShown;  // translated once, measured as drawn
    L.titlePx = ui.s(ui.fitFont(title, FontFace::Bold, 19.0f, room, 0.7f));
    L.titleShown = tr.measure(title, FontFace::Bold, L.titlePx).x <= room ? title : ui.elide(title, FontFace::Bold, L.titlePx / ui.s(1.0f), room);
    const float titleH = L.titlePx * 1.25f;
    std::string meta = i18n::tr(e.kind) + " · " + i18n::tr(filmLengthText(e));
    if (forPlayer && !e.unlocked) meta += " · " + i18n::tr("verrouillé");
    L.metaShown = tr.measure(meta, FontFace::Body, L.metaPx).x <= room ? meta : ui.elide(meta, FontFace::Body, L.metaPx / ui.s(1.0f), room);
    const float metaH = L.metaPx * 1.3f;
    const std::string where = i18n::tr(forPlayer && !e.unlocked ? e.lock : e.where);
    // Where players see it: two lines at most, smaller rather than a third, shortened at last.
    const float top = row.y + pad, bottom = row.bottom() - pad;
    const float whereRoom = std::max(0.0f, bottom - (top + titleH + metaH + ui.s(4)));
    const float minPx = std::max(10.0f, ui.s(12));
    for (float px = std::max(minPx, ui.s(14)); px >= minPx - 0.01f; px -= std::max(0.5f, ui.s(1))) {
        L.wherePx = px;
        L.whereLines.clear();
        std::string cur;
        size_t start = 0;
        while (start <= where.size()) {
            size_t sp = where.find(' ', start);
            std::string w = where.substr(start, sp == std::string::npos ? std::string::npos : sp - start);
            start = sp == std::string::npos ? where.size() + 1 : sp + 1;
            if (w.empty()) continue;
            std::string cand = cur.empty() ? w : cur + " " + w;
            if (!cur.empty() && tr.measure(cand, FontFace::Body, px).x > room) {
                L.whereLines.push_back(cur);
                cur = w;
            } else {
                cur = std::move(cand);
            }
        }
        if (!cur.empty()) L.whereLines.push_back(cur);
        if (L.whereLines.size() <= 2 && static_cast<float>(L.whereLines.size()) * px * 1.3f <= whereRoom) break;
    }
    const size_t fit = static_cast<size_t>(std::max(0.0f, std::floor(whereRoom / (L.wherePx * 1.3f))));
    if (L.whereLines.size() > std::min<size_t>(2, fit)) {
        L.whereLines.resize(std::min<size_t>(2, fit));
        if (!L.whereLines.empty()) L.whereLines.back() = ui.elide(L.whereLines.back() + " …", FontFace::Body, L.wherePx / ui.s(1.0f), room);
    }
    for (auto& l : L.whereLines)
        if (tr.measure(l, FontFace::Body, L.wherePx).x > room) l = ui.elide(l, FontFace::Body, L.wherePx / ui.s(1.0f), room);
    const float whereH = static_cast<float>(L.whereLines.size()) * L.wherePx * 1.3f;
    const float block = titleH + metaH + (whereH > 0.0f ? ui.s(4) + whereH : 0.0f);
    float y = top + std::max(0.0f, (bottom - top - block) * 0.5f);
    L.title = {tx, y, room, titleH};
    y += titleH;
    L.meta = {tx, y, room, metaH};
    y += metaH + ui.s(4);
    L.where = {tx, y, room, whereH};
    return L;
}

std::vector<FilmEntry> FilmList::shown() const {
    std::vector<FilmEntry> out;
    for (FilmEntry& e : filmCatalog(g_, forPlayer_))
        if ((filter == 0 || groupOf(e) == filter) && matches(e, search)) out.push_back(std::move(e));
    return out;
}

void FilmList::draw(const Rect& r) {
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    t_ += g_.app.frameTime() / 1000.0f;
    ui.pushId(forPlayer_ ? "films_player" : "films");
    // The search field, then the filters (each as wide as its label), on one line when they fit.
    const float bh = ui.s(44);
    static const char* kFilters[4] = {"Tous", "Histoires de giga-boss", "Films d'Asterion", "Autres"};
    float fw[4], fwSum = 0.0f;
    for (int i = 0; i < 4; ++i) fwSum += (fw[i] = ui.text().measure(kFilters[i], FontFace::Bold, ui.s(16)).x + ui.s(30)) + ui.s(8);
    const float searchW = std::min(ui.s(420), r.w * 0.4f);
    float y = r.y;
    float fx = r.x + searchW + ui.s(14);
    if (!forPlayer_) {  // the player's list is short: no search, no filter
        ui.textField("search", {r.x, y, searchW, bh}, search, "Rechercher un film…", 60);
        if (fx + fwSum > r.right()) {  // the filters on their own line
            y += bh + ui.s(10);
            fx = r.x;
        }
        for (int i = 0; i < 4; ++i) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = 16;
            st.selected = filter == i;
            const float w = std::min(fw[i], std::max(ui.s(60), (r.right() - fx) - (3 - i) * ui.s(8)));
            if (ui.button(std::string("filter") + std::to_string(i), {fx, y, w, bh}, kFilters[i], st)) filter = i;
            fx += w + ui.s(8);
        }
        y += bh + ui.s(14);
    }
    const std::vector<FilmEntry> items = shown();
    const Rect list{r.x, y, r.w, r.bottom() - y};
    if (items.empty()) {
        ui.label("Aucun film ne correspond à la recherche.", {list.center().x, list.y + ui.s(40)}, 18, ui.theme().textDim, Align::Center, FontFace::Body,
                 VAlign::Middle);
        ui.popId();
        return;
    }
    const float rowH = filmRowHeight(ui), gap = ui.s(8);
    ui.beginScroll("list", list, static_cast<float>(items.size()) * (rowH + gap));
    float yy = list.y - ui.scrollOffset();
    const float w = list.w - ui.s(16);
    for (const FilmEntry& e : items) {
        const Rect row{list.x, yy, w, rowH};
        yy += rowH + gap;
        const FilmRowLayout L = layoutFilmRow(ui, row, e, forPlayer_);
        const bool visible = !(row.bottom() < list.y || row.y > list.bottom());
        if (visible) {
            rr.fillRoundedRect(row, ui.s(10), Color{18, 26, 50, 235});
            // The thumbnail, covering its frame (its own aspect kept, cropped).
            rr.fillRoundedRect(L.thumb, ui.s(6), Color{6, 8, 18, 255});
            if (e.thumb.empty()) {
                rr.fillRoundedRect(L.thumb, ui.s(6), Color{255, 247, 232, 255});  // the tutorial's cream world, Bip
                drawBip(rr, L.thumb.center(), L.thumb.h * 0.55f, t_);
            } else if (TexturePtr tex = g_.app.resources().exists(e.thumb) ? g_.app.resources().texture(e.thumb, true) : nullptr;
                       tex && tex->width() > 0 && tex->height() > 0) {
                const float tw = static_cast<float>(tex->width()), tht = static_cast<float>(tex->height());
                const float want = L.thumb.w / L.thumb.h;
                Rect src{0, 0, tw, tht};
                if (tw / tht > want) src = {(tw - tht * want) * 0.5f, 0, tht * want, tht};
                else src = {0, (tht - tw / want) * 0.5f, tw, tw / want};
                rr.drawTexture(*tex, &src, L.thumb, (!forPlayer_ || e.unlocked) ? Color::white() : Color{90, 90, 110, 255});
            }
            rr.drawRoundedRect(L.thumb, ui.s(6), ui.theme().border, ui.s(1));
            {
                i18n::Verbatim asShown;  // measured and shortened as translated by the layout
                ui.label(L.titleShown, {L.title.x, L.title.center().y}, L.titlePx / ui.s(1.0f), ui.theme().text, Align::Left, FontFace::Bold,
                         VAlign::Middle);
                ui.label(L.metaShown, {L.meta.x, L.meta.center().y}, L.metaPx / ui.s(1.0f), ui.theme().accent, Align::Left, FontFace::Body, VAlign::Middle);
                for (size_t k = 0; k < L.whereLines.size(); ++k)
                    ui.label(L.whereLines[k], {L.where.x, L.where.y + (static_cast<float>(k) + 0.5f) * L.wherePx * 1.3f}, L.wherePx / ui.s(1.0f),
                             ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            }
        }
        if (forPlayer_ && !e.unlocked) {  // locked: a mark, not a button (its row says why)
            if (visible) {
                rr.drawRoundedRect(L.button, ui.s(8), ui.theme().textDim, ui.s(1));
                ui.label(L.buttonText, L.button.center(), 16, ui.theme().textDim, Align::Center, FontFace::Bold, VAlign::Middle);
            }
            continue;
        }
        UIContext::ButtonStyle bs;
        bs.small = true;
        bs.fontSize = 16;
        bs.primary = true;
        // Off screen: its button only (keyboard and gamepad reach every row; the scroll follows the focus).
        if (ui.button("play_" + e.id, L.button, L.buttonText, bs)) {
            if (auto s = makeFilmScene(g_, e)) {
                if (e.tutorial) g_.app.switchScene(std::move(s), 0.5f);  // a run: the administration comes back at its end
                else g_.app.pushScene(std::move(s));
            }
        }
    }
    ui.endScroll();
    ui.popId();
}

}  // namespace cosmic
