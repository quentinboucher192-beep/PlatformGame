// ONLINE → PROJETS: collaborative projects (Cosmic Online, phase 2). A project is a level
// edited by several players with roles (lecteur, testeur, éditeur, administrateur,
// propriétaire) checked by the server. Each member works on a local copy of the level (a
// normal level of the WORKSHOP, opened in the usual editor); "Envoyer mes modifications"
// makes a new revision of the project from it, and a copy made from an older revision is
// never sent over a newer one: the server refuses it and the player chooses what to do,
// with his copy kept. Invitations: keys (shown once), links, direct invitations, join
// requests, open projects. Nothing here is decided by the game alone.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <map>

#include <SDL3/SDL_clipboard.h>

#include "engine/core/FileUtil.h"
#include "game/GameServices.h"
#include "game/levels/ObjectTypes.h"
#include "game/online/OnlineSession.h"
#include "packaging/Sha256.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/OnlineNav.h"

namespace cosmic {

namespace {

const char* kRoles[] = {"viewer", "tester", "editor", "admin", "owner"};

std::string roleName(const std::string& r) {
    if (r == "owner") return "Propriétaire";
    if (r == "admin") return "Administrateur";
    if (r == "editor") return "Éditeur";
    if (r == "tester") return "Testeur";
    if (r == "viewer") return "Lecteur";
    return r;
}

/// The role inside a sentence ("comme éditeur").
std::string roleWord(const std::string& r) {
    std::string n = roleName(r);
    if (n == "Éditeur") return "éditeur";
    if (!n.empty()) n[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(n[0])));
    return n;
}

int roleRank(const std::string& r) {
    for (int i = 0; i < 5; ++i)
        if (r == kRoles[i]) return i + 1;
    return 0;
}

std::string visibilityName(const std::string& v) {
    if (v == "private") return "Privé (membres seulement)";
    if (v == "invite") return "Sur invitation";
    if (v == "request") return "Public, sur demande";
    if (v == "open") return "Ouvert à tous";
    return v;
}
const char* kVisibilities[] = {"private", "invite", "request", "open"};
const std::vector<std::string> kVisibilityLabels = {"Privé", "Sur invitation", "Public, sur demande", "Ouvert à tous"};
const char* kJoinRoles[] = {"viewer", "tester", "editor"};
const std::vector<std::string> kJoinRoleLabels = {"Lecteur", "Testeur", "Éditeur"};
const char* kInviteRoles[] = {"viewer", "tester", "editor", "admin"};
const std::vector<std::string> kInviteRoleLabels = {"Lecteur", "Testeur", "Éditeur", "Administrateur"};
const int kUses[] = {1, 5, 20, 0};
const std::vector<std::string> kUseLabels = {"1 utilisation", "5 utilisations", "20 utilisations", "Illimitée"};
const int kHours[] = {1, 24, 24 * 7, 24 * 30};
const std::vector<std::string> kHourLabels = {"1 heure", "24 heures", "7 jours", "30 jours"};

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out.push_back(static_cast<char>(c));
        else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

// ------------------------------------------------------------ local copies

/// Links between local levels and projects (online/projects.json): {localId: {project_id,
/// name, revision, local_hash}}. local_hash: the level as written from that revision, to
/// tell whether the player changed his copy since.
struct ProjectLinks {
    Json links = Json::object();
    fs::path file() const { return Paths::userDir() / "online" / "projects.json"; }
    void load() {
        links = Json::object();
        if (auto t = FileUtil::readText(file())) {
            Json j = Json::parse(*t, nullptr, false);
            if (j.is_object()) links = j;
        }
    }
    void save() const {
        std::error_code ec;
        fs::create_directories(file().parent_path(), ec);
        FileUtil::writeTextAtomic(file(), links.dump(1));
    }
    std::string localOf(const std::string& projectId) const {
        for (auto it = links.begin(); it != links.end(); ++it)
            if (jstr(it.value(), "project_id") == projectId) return it.key();
        return {};
    }
};

/// Local id of a project's working copy (stable for every member).
std::string localIdFor(const std::string& projectId) { return "projet_" + projectId.substr(0, 8); }

std::string levelHash(const LevelData& l) { return Sha256::hex(l.toJson().dump()); }

class ProjectsScene : public Scene {
public:
    ProjectsScene(GameServices& g, std::string wanted) : g_(g), ui_(g), wanted_(std::move(wanted)) {}

    void onEnter() override {
        g_.playMusic("music/menu_theme.ogg");
        bg_.init(g_);
        g_.ui.setFocus("online/nav/projects");
        links_.load();
        loadList();
        if (!wanted_.empty()) open(wanted_);
        // Automated screenshots: "project-tab:N" opens a tab of the project.
        if (g_.onlineOpen.rfind("project-tab:", 0) == 0) {
            detailTab_ = std::clamp(std::atoi(g_.onlineOpen.c_str() + 12), 0, 6);
            g_.onlineOpen.clear();
        } else if (g_.onlineOpen == "project-new") {
            g_.onlineOpen.clear();
            openCreate();
        } else if (g_.onlineOpen.rfind("join-key:", 0) == 0) {  // an invitation link was opened
            key_ = g_.onlineOpen.substr(9);
            g_.onlineOpen.clear();
            say("Lien d'invitation reçu : choisissez « Rejoindre » pour entrer dans le projet.", false);
        } else if (g_.onlineOpen == "project-invitations" || g_.onlineOpen == "project-discover") {
            listTab_ = g_.onlineOpen == "project-invitations" ? 1 : 2;
            g_.onlineOpen.clear();
            loadList();
        }
        requireAccount();
    }
    void onResume() override {
        accountAsked_ = false;
        links_.load();
        loadList();
        if (!selected_.empty()) open(selected_);
    }
    void update(float dt) override {
        bg_.update(dt);
        requireAccount();
        if (searchDirty_ > 0.0f && (searchDirty_ -= dt) <= 0.0f) loadList();
        // Automated screenshots of the history: "project-rev:N[:M]" selects revision N (and
        // compares it with M), "project-name:N" / "project-restore:N" open the dialogs,
        // "project-vcmp" compares the oldest named version with the latest revision.
        const std::string& h = g_.onlineOpen;
        if ((h.rfind("project-rev:", 0) == 0 || h.rfind("project-name:", 0) == 0 || h.rfind("project-restore:", 0) == 0 || h == "project-vcmp") &&
            detail_.is_object() && member()) {
            const int want = h == "project-vcmp" ? 6 : 5;
            if (detailTab_ != want) {
                detailTab_ = want;
                loadTab();
            }
        }
        if ((h.rfind("project-rev:", 0) == 0 || h.rfind("project-name:", 0) == 0 || h.rfind("project-restore:", 0) == 0 || h == "project-vcmp") &&
            detail_.is_object() && member() && tabData_.is_object()) {
            std::string what = h;
            g_.onlineOpen.clear();
            if (what == "project-vcmp") {
                Json v = tabData_.value("versions", Json::array());
                if (!v.empty()) compareWith(jget<int>(v.back(), "revision", 1), jget<int>(detail_, "revision", 1));
            } else {
                size_t c = what.find(':');
                selectedRev_ = std::atoi(what.c_str() + c + 1);
                compareTo_ = std::max(1, selectedRev_ - 1);
                if (what[8] == 'r' && what[9] == 'e' && what[10] == 'v') {
                    size_t c2 = what.find(':', c + 1);
                    if (c2 != std::string::npos) {
                        compareTo_ = std::max(1, std::atoi(what.c_str() + c2 + 1));
                        compareWith(compareTo_, selectedRev_);
                    }
                } else if (what[8] == 'n') {
                    naming_ = true;
                    versionName_ = "Bêta 2";
                    versionNotes_ = "Avec le pont et la porte haute.";
                } else {
                    confirm_ = "restore";
                }
            }
        }
        // Automated checks of the working copy: "project-download", "project-send",
        // "project-pull" run once the project is loaded.
        const std::string& o = g_.onlineOpen;
        if ((o == "project-download" || o == "project-send" || o == "project-pull" || o == "project-live" || o.rfind("project-live-edit:", 0) == 0 ||
             o.rfind("project-live-copy:", 0) == 0) &&
            detail_.is_object() && !working_ && member()) {
            std::string what = o;
            g_.onlineOpen.clear();
            if (what == "project-live") goLive();
            else if (what.rfind("project-live-copy:", 0) == 0) {
                g_.onlineOpen = "live-copy:" + what.substr(18);  // read by the live editor
                goLive();
            } else if (what.rfind("project-live-edit:", 0) == 0) {
                g_.onlineOpen = "live-edit:" + what.substr(18);  // read by the live editor
                goLive();
            }
            else if (what == "project-download") download(true, [this](const std::string&) { say("Copie locale téléchargée.", false); });
            else if (what == "project-send") sendChanges();
            else {
                if (copyState().modified) keepModifiedCopy();
                download(true, [this](const std::string&) {
                    conflict_ = Json();
                    say("Dernière version récupérée ; vos modifications sont gardées dans « … (mes modifications) ».", false);
                });
            }
        }
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        layout();
        bg_.render(g_, 0.5f);
        const bool top = g_.app.topScene() == this;
        const bool modal = creating_ || naming_ || !confirm_.empty();
        ui.inputEnabled = top && !modal;
        drawHeader();
        drawOnlineNav(g_, zoneA_, OnlineSection::Projects);
        drawList();
        drawDetail();
        ui.inputEnabled = top;
        if (creating_) drawCreate();
        else if (naming_) drawNaming();
        else if (!confirm_.empty()) drawConfirm();
        else if (ui.backPressed() && !ui.editingText()) {
            if (!selected_.empty()) selected_.clear();
            else g_.app.switchScene(makeMainMenu(g_));
        }
        ui.inputEnabled = true;
        if (top) ui.end();
    }

private:
    void requireAccount() {
        if (g_.online.loggedIn()) {
            accountAsked_ = false;
            return;
        }
        if (accountAsked_ || g_.app.topScene() != this || g_.app.transitioning()) return;
        accountAsked_ = true;
        g_.app.pushScene(makeOnlineAccount(g_, !g_.settings.online.onboardingDone, true));
    }

    void say(const std::string& m, bool error) {
        status_ = m;
        statusError_ = error;
        statusT_ = g_.ui.time();
    }

    // ---------------------------------------------------------------- data
    void loadList() {
        if (!g_.online.loggedIn()) return;
        std::string path = listTab_ == 0 ? "/api/v1/projects" : listTab_ == 1 ? "/api/v1/projects/invitations" : "/api/v1/projects/discover?limit=30";
        if (listTab_ == 2 && !query_.empty()) path += "&q=" + urlEncode(query_);
        int gen = ++listGen_;
        listBusy_ = true;
        g_.online.get(
            path, OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                if (gen != listGen_) return;
                listBusy_ = false;
                listLoaded_ = true;
                if (!r.ok) return say(r.message, true);
                list_ = r.data.value(listTab_ == 1 ? "invitations" : "projects", Json::array());
                if (listTab_ == 1) invitationCount_ = static_cast<int>(list_.size());
            }),
            false);
        if (listTab_ != 1)  // the counter of the "Invitations" tab
            g_.online.get("/api/v1/projects/invitations", OnlineService::guard(life_, [this](const ApiResult& r) {
                              if (r.ok) invitationCount_ = static_cast<int>(r.data.value("invitations", Json::array()).size());
                          }),
                          false);
    }

    void open(const std::string& id) {
        selected_ = id;
        detail_ = Json();
        tabData_ = Json();
        createdKey_.clear();
        g_.online.get("/api/v1/projects/" + id, OnlineService::guard(life_, [this, id](const ApiResult& r) {
                          if (id != selected_) return;
                          if (!r.ok) {
                              say(r.message, true);
                              if (r.status == 404) selected_.clear();
                              return;
                          }
                          detail_ = r.data.value("project", Json::object());
                          for (int i = 0; i < 3; ++i)
                              if (jstr(detail_, "join_role") == kJoinRoles[i]) requestRole_ = i;
                          if (!manager() && detailTab_ >= 2 && detailTab_ <= 3) detailTab_ = 0;
                          loadTab();
                      }),
                      false);
    }

    void loadTab() {
        tabData_ = Json();
        if (!member()) return;
        std::string base = "/api/v1/projects/" + selected_;
        std::string path = detailTab_ == 1 ? base + "/members" : detailTab_ == 2 ? base + "/invites" : detailTab_ == 3 ? base + "/requests"
                         : detailTab_ == 4 ? base + "/activity"
                         : detailTab_ == 6 ? base + "/versions"
                         : detailTab_ == 5 ? base + "/revisions?limit=100" + revisionFilter()
                                           : base + "/revisions?limit=10";
        int gen = ++tabGen_;
        g_.online.get(path, OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                          if (gen != tabGen_) return;
                          if (!r.ok) return say(r.message, true);
                          tabData_ = r.data;
                          // The authors of the filter, from the unfiltered history.
                          if (detailTab_ == 5 && revAuthor_ == 0 && revQuery_.empty()) {
                              revAuthors_.clear();
                              for (const auto& rv : tabData_.value("revisions", Json::array())) {
                                  if (!rv.contains("author") || !rv["author"].is_object()) continue;
                                  std::string id = jstr(rv["author"], "id");
                                  if (std::none_of(revAuthors_.begin(), revAuthors_.end(), [&](const auto& a) { return a.first == id; }))
                                      revAuthors_.push_back({id, jstr(rv["author"], "username")});
                              }
                          }
                      }),
                      false);
    }

    std::string revisionFilter() const {
        std::string f;
        if (revAuthor_ > 0 && revAuthor_ <= static_cast<int>(revAuthors_.size())) f += "&author=" + revAuthors_[static_cast<size_t>(revAuthor_ - 1)].first;
        if (!revQuery_.empty()) f += "&q=" + urlEncode(revQuery_);
        return f;
    }

    std::string role() const { return detail_.is_object() ? jstr(detail_, "my_role") : std::string{}; }
    bool member() const { return !role().empty(); }
    bool manager() const { return roleRank(role()) >= 4; }
    bool editor() const { return roleRank(role()) >= 3; }
    bool tester() const { return roleRank(role()) >= 2; }

    void done(const ApiResult& r, const std::string& ok, bool reload = true) {
        if (!r.ok) return say(r.message, true);
        say(ok, false);
        if (reload) {
            loadList();
            if (!selected_.empty()) open(selected_);
        }
    }

    // ------------------------------------------------------- local copy
    struct CopyState {
        bool exists = false, linked = false, modified = false, behind = false;
        int revision = 0;
    };
    CopyState copyState() const {
        CopyState s;
        std::string local = links_.localOf(selected_);
        if (local.empty()) return s;
        s.linked = true;
        const Json& link = links_.links[local];
        s.revision = jget<int>(link, "revision", 0);
        s.behind = s.revision < jget<int>(detail_, "revision", 0);
        if (auto lvl = g_.content.loadLevel(local)) {
            s.exists = true;
            s.modified = levelHash(*lvl) != jstr(link, "local_hash");
        }
        return s;
    }

    /// Downloads a revision of the project and writes it as the local copy (`linked`), or as
    /// an independent level (a reader's copy).
    void download(bool linked, std::function<void(const std::string&)> then) {
        std::string id = selected_, name = jstr(detail_, "name");
        working_ = true;
        g_.online.getBinary("/api/v1/projects/" + id + "/content", [this, id, name, linked, then, life = std::weak_ptr<int>(life_)](const HttpResponse& r) {
            if (!life.lock()) return;
            working_ = false;
            if (r.status != 200) return say(r.networkFailure() ? "Connexion impossible : " + r.errorMessage : "Téléchargement refusé (" + std::to_string(r.status) + ").", true);
            Json j = Json::parse(r.body, nullptr, false);
            std::string err;
            auto lvl = j.is_object() ? LevelData::fromJson(j, &err) : std::nullopt;
            if (!lvl) return say("Le niveau du projet est illisible : " + (err.empty() ? std::string("format inconnu") : err), true);
            std::string local = linked ? localIdFor(id) : localIdFor(id) + "_copie";
            lvl->id = local;
            if (!linked) lvl->name = name + " (copie)";
            if (!g_.content.saveUserLevel(*lvl)) return say("Impossible d'écrire la copie locale.", true);
            if (linked) {
                links_.load();
                links_.links[local] = {{"project_id", id}, {"name", name}, {"revision", std::atoi(r.header("x-project-revision").c_str())},
                                       {"local_hash", levelHash(*lvl)}};
                links_.save();
            }
            g_.ratings.contentChanged();
            if (then) then(local);
        });
    }

    void goLive() {
        // A local copy changed offline is kept apart first (the room's document replaces it).
        if (copyState().modified) keepModifiedCopy();
        std::string role = this->role();
        std::string pid = selected_;
        download(true, [this, role, pid](const std::string& local) { g_.app.switchScene(makeLevelEditorLive(g_, local, pid, role)); });
    }

    /// Before replacing a modified local copy, it is kept as an independent level.
    void keepModifiedCopy() {
        std::string local = links_.localOf(selected_);
        auto lvl = local.empty() ? std::nullopt : g_.content.loadLevel(local);
        if (!lvl) return;
        lvl->id = local + "_mes_modifs";
        lvl->name = lvl->name + " (mes modifications)";
        g_.content.saveUserLevel(*lvl);
    }

    void sendChanges() {
        std::string local = links_.localOf(selected_);
        auto lvl = local.empty() ? std::nullopt : g_.content.loadLevel(local);
        if (!lvl) return say("Copie locale introuvable : ouvrez d'abord le projet dans l'éditeur.", true);
        int base = jget<int>(links_.links[local], "revision", 0);
        std::string id = selected_;
        Json content = lvl->toJson();
        std::string hash = levelHash(*lvl);
        working_ = true;
        g_.online.send("PUT", "/api/v1/projects/" + id + "/content", {{"base_revision", base}, {"content", content}, {"message", message_}},
                       OnlineService::guard(life_, [this, id, local, hash](const ApiResult& r) {
                           working_ = false;
                           if (r.status == 409 && r.code == "REVISION_CONFLICT") {
                               conflict_ = r.details.is_object() ? r.details : Json::object();
                               say("Le projet a changé entre-temps : vos modifications n'ont pas été envoyées (elles restent dans votre copie).", true);
                               open(id);
                               return;
                           }
                           if (!r.ok) return say(r.message, true);
                           const Json& saved = r.data.value("saved", Json::object());
                           links_.load();
                           links_.links[local]["revision"] = jget<int>(saved, "revision", 0);
                           links_.links[local]["local_hash"] = hash;
                           links_.save();
                           message_.clear();
                           conflict_ = Json();
                           done(r, "Révision " + std::to_string(jget<int>(saved, "revision", 0)) + " envoyée.");
                       }));
    }

    // ------------------------------------------------------------- layout
    void layout() {
        UIContext& ui = g_.ui;
        float W = ui.screenW(), H = ui.screenH(), m = ui.s(22);
        header_ = {0, 0, W, ui.s(64)};
        zoneA_ = {m, header_.bottom() + ui.s(8), ui.s(272), H - header_.bottom() - ui.s(8) - m};
        zoneD_ = {W - m - ui.s(760), zoneA_.y, ui.s(760), zoneA_.h};
        zoneL_ = {zoneA_.right() + ui.s(18), zoneA_.y, zoneD_.x - ui.s(18) - zoneA_.right() - ui.s(18), zoneA_.h};
    }

    void panel(const Rect& r) {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(r, ui.s(14), Color{8, 13, 30, 222});
        ui.r().drawRoundedRect(r, ui.s(14), ui.theme().border, ui.s(1.2f));
    }

    void drawHeader() {
        UIContext& ui = g_.ui;
        ui.r().fillGradientV(header_, Color{6, 10, 24, 230}, Color{6, 10, 24, 0});
        Vec2 tsz = ui.label("COSMIC ONLINE", {ui.s(32), header_.center().y}, 34, ui.theme().accent, Align::Left, FontFace::Title, VAlign::Middle);
        ui.label("Projets collaboratifs", {ui.s(32) + tsz.x + ui.s(18), header_.center().y + ui.s(3)}, 19, ui.theme().textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
        ui_.drawConnection({zoneD_.right() - ui.s(240), header_.center().y});
    }

    void chip(const std::string& t, Vec2 left, Color c, float size = 12) {
        UIContext& ui = g_.ui;
        float w = ui.text().measure(t, FontFace::Bold, ui.s(size)).x + ui.s(16);
        Rect r{left.x, left.y - ui.s(11), w, ui.s(22)};
        ui.r().fillRoundedRect(r, ui.s(11), c.withAlphaF(0.2f));
        ui.r().drawRoundedRect(r, ui.s(11), c.withAlphaF(0.8f), ui.s(1));
        ui.label(t, r.center(), size, c, Align::Center, FontFace::Bold, VAlign::Middle);
    }
    float chipWidth(const std::string& t, float size = 12) { return g_.ui.text().measure(t, FontFace::Bold, g_.ui.s(size)).x + g_.ui.s(16); }

    // --------------------------------------------------------------- list
    void drawList() {
        UIContext& ui = g_.ui;
        panel(zoneL_);
        ui.pushId("projects/list");
        float x = zoneL_.x + ui.s(12), w = zoneL_.w - ui.s(24), y = zoneL_.y + ui.s(12);
        int tab = listTab_;
        ui.tabs("tabs", {x, y, w, ui.s(40)}, {"Mes projets", "Invitations" + (invitationCount_ > 0 ? " (" + std::to_string(invitationCount_) + ")" : std::string()), "Découvrir"},
                tab);
        if (tab != listTab_) {
            listTab_ = tab;
            list_ = Json::array();
            listLoaded_ = false;
            loadList();
        }
        y += ui.s(52);
        // Create, join with a key.
        UIContext::ButtonStyle p;
        p.primary = true;
        p.small = true;
        if (ui.button("new", {x, y, w, ui.s(42)}, "Nouveau projet", p)) openCreate();
        y += ui.s(52);
        float jb = ui.s(130);
        ui.textField("key", {x, y, w - jb - ui.s(8), ui.s(42)}, key_, "Clé ou lien d'invitation (CPJ-…)", 200);
        UIContext::ButtonStyle js;
        js.small = true;
        js.enabled = !key_.empty();
        if (ui.button("join", {x + w - jb, y, jb, ui.s(42)}, "Rejoindre", js)) {
            g_.online.send("POST", "/api/v1/projects/join", {{"key", key_}}, OnlineService::guard(life_, [this](const ApiResult& r) {
                               if (!r.ok) return say(r.message, true);
                               key_.clear();
                               std::string id = jstr(r.data.value("project", Json::object()), "id");
                               listTab_ = 0;
                               done(r, "Bienvenue dans le projet !");
                               open(id);
                           }));
        }
        y += ui.s(52);
        if (listTab_ == 2) {
            std::string before = query_;
            ui.textField("q", {x, y, w, ui.s(42)}, query_, "Nom du projet ou du propriétaire", 60);
            if (query_ != before) searchDirty_ = 0.4f;
            y += ui.s(52);
        }
        Rect view{x, y, w, zoneL_.bottom() - y - ui.s(40)};
        if (list_.empty()) {
            std::string empty = !listLoaded_ || listBusy_ ? "Chargement…"
                              : listTab_ == 0 ? "Aucun projet : créez-en un à partir d'un de vos niveaux, ou rejoignez-en un avec une clé."
                              : listTab_ == 1 ? "Aucune invitation en attente."
                                              : "Aucun projet ouvert aux autres joueurs pour l'instant.";
            ui.labelWrapped(empty, {view.x + ui.s(4), view.y + ui.s(6)}, view.w - ui.s(8), 16, ui.theme().textDim);
        } else {
            const float rh = ui.s(listTab_ == 1 ? 118 : 92), gap = ui.s(8);
            ui.beginScroll("rows", view, list_.size() * (rh + gap));
            float cy = view.y - ui.scrollOffset();
            for (const auto& it : list_) {
                Rect row{view.x, cy, view.w - ui.s(12), rh};
                cy += rh + gap;
                if (row.bottom() < view.y || row.y > view.bottom()) continue;
                if (listTab_ == 1) drawInvitation(it, row);
                else drawProjectRow(it, row);
            }
            ui.endScroll();
        }
        if (!status_.empty() && ui.time() - statusT_ < 8.0f)
            ui.label(status_, {x, zoneL_.bottom() - ui.s(20)}, ui.fitFont(status_, FontFace::Body, 15, w), statusError_ ? ui.theme().danger : ui.theme().success,
                     Align::Left, FontFace::Body, VAlign::Middle);
        ui.popId();
    }

    void drawProjectRow(const Json& p, const Rect& row) {
        UIContext& ui = g_.ui;
        std::string id = jstr(p, "id");
        UIContext::ButtonStyle st;
        st.selected = id == selected_;
        if (ui.button("p" + id, row, "", st)) open(id);
        float x = row.x + ui.s(14), room = row.w - ui.s(28);
        std::string r = jstr(p, "my_role");
        std::string badge = r.empty() ? visibilityName(jstr(p, "visibility")) : roleName(r);
        float bw = chipWidth(badge);
        std::string name = jstr(p, "name");
        ui.label(name, {x, row.y + ui.s(24)}, ui.fitFont(name, FontFace::Bold, 19, room - bw - ui.s(12)), ui.theme().text, Align::Left, FontFace::Bold,
                 VAlign::Middle);
        chip(badge, {row.right() - ui.s(14) - bw, row.y + ui.s(24)}, r.empty() ? ui.theme().warm : ui.theme().accent);
        const Json& o = p.value("owner", Json::object());
        int rev = jget<int>(p, "revision", 0), members = jget<int>(p, "member_count", 1);
        std::string sub = "par " + jstr(o, "username") + "  ·  " + std::to_string(members) + (members > 1 ? " membres" : " membre") + "  ·  " +
                          (rev > 0 ? "révision " + std::to_string(rev) : std::string("pas encore de contenu"));
        ui.label(sub, {x, row.y + ui.s(52)}, ui.fitFont(sub, FontFace::Body, 14, room), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        std::string when = "modifié " + OnlineUi::ageOf(jstr(p, "updated_at"));
        if (jget<bool>(p, "requested", false)) when += "  ·  demande envoyée";
        ui.label(when, {x, row.y + ui.s(74)}, ui.fitFont(when, FontFace::Body, 13, room), ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
    }

    void drawInvitation(const Json& inv, const Rect& row) {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(row, ui.s(8), Color{14, 22, 46, 230});
        ui.r().drawRoundedRect(row, ui.s(8), ui.theme().accent.withAlphaF(0.5f), ui.s(1));
        const Json& p = inv.value("project", Json::object());
        const Json& by = inv.value("invited_by", Json::object());
        float x = row.x + ui.s(14), room = row.w - ui.s(28);
        std::string name = jstr(p, "name");
        ui.label(name, {x, row.y + ui.s(22)}, ui.fitFont(name, FontFace::Bold, 19, room), ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        std::string sub = jstr(by, "username") + " vous invite comme " + roleWord(jstr(inv, "role")) + "  ·  expire " + expiryText(jstr(inv, "expires_at"));
        ui.label(sub, {x, row.y + ui.s(48)}, ui.fitFont(sub, FontFace::Body, 14, room), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        std::string id = jstr(inv, "id");
        UIContext::ButtonStyle a;
        a.small = true;
        a.primary = true;
        float bw = std::min(ui.s(200), (room - ui.s(10)) * 0.5f);
        if (ui.button("acc" + id, {x, row.y + ui.s(68), bw, ui.s(38)}, "Accepter", a))
            g_.online.send("POST", "/api/v1/projects/invitations/" + id + "/accept", Json(), OnlineService::guard(life_, [this](const ApiResult& r) {
                               if (!r.ok) return say(r.message, true);
                               std::string pid = jstr(r.data.value("project", Json::object()), "id");
                               listTab_ = 0;
                               done(r, "Bienvenue dans le projet !");
                               open(pid);
                           }));
        UIContext::ButtonStyle d;
        d.small = true;
        d.danger = true;
        if (ui.button("dec" + id, {x + bw + ui.s(10), row.y + ui.s(68), bw, ui.s(38)}, "Refuser", d))
            g_.online.send("DELETE", "/api/v1/projects/invitations/" + id, Json(),
                           OnlineService::guard(life_, [this](const ApiResult& r) { done(r, "Invitation refusée."); }));
    }

    static std::string expiryText(const std::string& iso) {
        auto t = parseIsoUtc(iso);
        if (!t) return "jamais";
        double secs = std::chrono::duration<double>(*t - std::chrono::system_clock::now()).count();
        if (secs <= 0) return "expirée";
        if (secs < 3600) return "dans " + std::to_string(static_cast<int>(secs / 60) + 1) + " min";
        if (secs < 86400) return "dans " + std::to_string(static_cast<int>(secs / 3600)) + " h";
        return "dans " + std::to_string(static_cast<int>(secs / 86400)) + " j";
    }

    // ------------------------------------------------------------- detail
    void drawDetail() {
        UIContext& ui = g_.ui;
        panel(zoneD_);
        ui.pushId("projects/detail");
        float x = zoneD_.x + ui.s(20), w = zoneD_.w - ui.s(40), y = zoneD_.y + ui.s(16);
        if (selected_.empty() || !detail_.is_object()) {
            ui.label(selected_.empty() ? "Choisissez un projet" : "Chargement…", {zoneD_.center().x, zoneD_.y + ui.s(60)}, 20, ui.theme().textDim, Align::Center,
                     FontFace::Bold, VAlign::Middle);
            ui.labelWrapped("Un projet est un niveau que plusieurs joueurs construisent ensemble. Chacun a un rôle : les lecteurs le consultent, "
                            "les testeurs y jouent, les éditeurs le modifient, les administrateurs gèrent les membres et les invitations. Le "
                            "serveur vérifie chaque rôle ; chaque envoi crée une nouvelle révision, et une copie faite d'après une révision "
                            "ancienne n'écrase jamais la plus récente.",
                            {x, zoneD_.y + ui.s(100)}, w, 16, ui.theme().textDim);
            ui.popId();
            return;
        }
        std::string name = jstr(detail_, "name");
        std::string r = role();
        float bw = r.empty() ? 0.0f : chipWidth(roleName(r), 14);
        ui.label(name, {x, y + ui.s(18)}, ui.fitFont(name, FontFace::Title, 26, w - bw - ui.s(16)), ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        if (!r.empty()) chip(roleName(r), {x + w - bw, y + ui.s(18)}, ui.theme().accent, 14);
        y += ui.s(42);
        const Json& o = detail_.value("owner", Json::object());
        int mc = jget<int>(detail_, "member_count", 1);
        std::string sub = "par " + jstr(o, "username") + "  ·  " + visibilityName(jstr(detail_, "visibility")) + "  ·  " + std::to_string(mc) +
                          (mc > 1 ? " membres" : " membre");
        ui.label(sub, {x, y + ui.s(10)}, ui.fitFont(sub, FontFace::Body, 15, w), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        y += ui.s(28);
        std::string desc = jstr(detail_, "description");
        if (!desc.empty()) {
            Vec2 ds = ui.labelWrapped(desc, {x, y}, w, 15, ui.theme().text);
            y += std::min(ds.y, ui.s(60)) + ui.s(8);
        }
        if (!member()) {
            drawNonMember(x, y, w);
            ui.popId();
            return;
        }
        std::vector<std::string> tabs = {"Projet", "Membres", "Invitations", "Demandes", "Activité", "Révisions", "Versions"};
        int tab = detailTab_;
        ui.tabs("tabs", {x, y, w, ui.s(40)}, tabs, tab);
        if (tab != detailTab_) {
            if ((tab == 2 || tab == 3) && !manager()) {
                say("Réservé aux administrateurs du projet.", true);
            } else {
                detailTab_ = tab;
                compare_ = Json();
                compareLabel_.clear();
                selectedRev_ = 0;
                loadTab();
            }
        }
        y += ui.s(52);
        Rect area{x, y, w, zoneD_.bottom() - y - ui.s(16)};
        switch (detailTab_) {
            case 0: drawOverview(area); break;
            case 1: drawMembers(area); break;
            case 2: drawInvites(area); break;
            case 3: drawRequests(area); break;
            case 5: drawRevisions(area); break;
            case 6: drawVersions(area); break;
            default: drawActivity(area); break;
        }
        ui.popId();
    }

    void drawNonMember(float x, float y, float w) {
        UIContext& ui = g_.ui;
        std::string vis = jstr(detail_, "visibility");
        bool requested = jget<bool>(detail_, "requested", false);
        std::string text = vis == "open" ? "Ce projet est ouvert : vous le rejoignez comme " + roleWord(jstr(detail_, "join_role")) + "."
                         : requested     ? "Votre demande a été envoyée ; un administrateur du projet l'acceptera ou la refusera."
                                         : "Demandez à rejoindre ce projet : un administrateur choisira votre rôle.";
        Vec2 ts = ui.labelWrapped(text, {x, y + ui.s(8)}, w, 16, ui.theme().textDim);
        y += ts.y + ui.s(20);
        if (!requested && vis == "request") {
            ui.textField("msg", {x, y, w, ui.s(42)}, joinMessage_, "Message pour les administrateurs (facultatif)", 300);
            y += ui.s(52);
        }
        UIContext::ButtonStyle p;
        p.primary = !requested;
        p.danger = requested;
        std::string label = vis == "open" ? "Rejoindre le projet" : requested ? "Annuler ma demande" : "Demander à rejoindre";
        if (ui.button("join", {x, y, w, ui.s(46)}, label, p)) {
            std::string path = "/api/v1/projects/" + selected_ + "/join";
            if (requested) g_.online.send("DELETE", path, Json(), OnlineService::guard(life_, [this](const ApiResult& r) { done(r, "Demande annulée."); }));
            else
                g_.online.send("POST", path, {{"message", joinMessage_}}, OnlineService::guard(life_, [this](const ApiResult& r) {
                                   joinMessage_.clear();
                                   done(r, jget<bool>(r.data, "joined", false) ? "Bienvenue dans le projet !" : "Demande envoyée.");
                               }));
        }
    }

    void drawOverview(Rect a) {
        UIContext& ui = g_.ui;
        float x = a.x, w = a.w, y = a.y;
        const Json& last = detail_.value("last_revision", Json());
        std::string rev = last.is_object() ? "Révision " + std::to_string(jget<int>(last, "revision", 0)) + " par " + jstr(last, "author") + ", " +
                                                 OnlineUi::ageOf(jstr(last, "created_at")) +
                                                 (jstr(last, "message").empty() ? std::string() : " : « " + jstr(last, "message") + " »")
                                           : std::string("Pas encore de contenu : un éditeur doit envoyer le niveau.");
        Vec2 rs = ui.labelWrapped(rev, {x, y}, w, 16, ui.theme().text);
        y += rs.y + ui.s(10);
        // Local copy.
        CopyState cs = copyState();
        std::string copy = !cs.linked || !cs.exists ? "Pas de copie locale de ce projet sur cet ordinateur."
                         : cs.modified && cs.behind ? "Votre copie (révision " + std::to_string(cs.revision) + ") est modifiée, et le projet a avancé."
                         : cs.modified              ? "Votre copie est modifiée : envoyez-la pour créer une nouvelle révision."
                         : cs.behind                ? "Votre copie (révision " + std::to_string(cs.revision) + ") est en retard sur le projet."
                                                    : "Votre copie locale est à jour.";
        Color cc = cs.modified ? ui.theme().warm : cs.behind ? ui.theme().danger : ui.theme().success;
        Vec2 cps = ui.labelWrapped(copy, {x, y}, w, 15, cs.linked && cs.exists ? cc : ui.theme().textDim);
        y += cps.y + ui.s(12);
        if (conflict_.is_object() && !conflict_.empty()) {
            std::string c = "Conflit : le projet est passé à la révision " + std::to_string(jget<int>(conflict_, "current_revision", 0)) +
                            (jstr(conflict_, "saved_by").empty() ? std::string() : " (par " + jstr(conflict_, "saved_by") + ")") +
                            ". « Récupérer la dernière version » garde vos modifications dans un niveau à part, que vous pourrez reporter.";
            Vec2 s = ui.labelWrapped(c, {x, y}, w, 15, ui.theme().danger);
            y += s.y + ui.s(10);
        }
        float half = (w - ui.s(10)) * 0.5f;
        UIContext::ButtonStyle st;
        st.small = true;
        st.enabled = !working_ && jget<int>(detail_, "revision", 0) > 0;
        bool hasContent = jget<int>(detail_, "revision", 0) > 0;
        if (editor()) {
            UIContext::ButtonStyle pe = st;
            pe.primary = true;
            if (ui.button("edit", {x, y, half, ui.s(44)}, cs.linked && cs.exists ? "Ouvrir dans l'éditeur" : "Télécharger et éditer", pe)) {
                if (cs.linked && cs.exists) g_.app.switchScene(makeLevelEditor(g_, links_.localOf(selected_)));
                else download(true, [this](const std::string& local) { g_.app.switchScene(makeLevelEditor(g_, local)); });
            }
            UIContext::ButtonStyle se = st;
            // Made from an older revision: the server would refuse it; get the latest first.
            se.enabled = !working_ && cs.linked && cs.exists && cs.modified && !cs.behind;
            se.primary = se.enabled;
            if (ui.button("send", {x + half + ui.s(10), y, half, ui.s(44)}, working_ ? "Envoi…" : "Envoyer mes modifications", se)) sendChanges();
            y += ui.s(54);
            if (cs.linked && cs.exists && cs.modified) {
                ui.textField("msg", {x, y, w, ui.s(40)}, message_, "Description de vos modifications (facultatif)", 200);
                y += ui.s(50);
            }
        }
        // Live editing with the other members (editors edit, the others watch).
        if (hasContent) {
            UIContext::ButtonStyle ls = st;
            ls.primary = true;
            if (ui.button("live", {x, y, w, ui.s(44)}, editor() ? "Édition en direct avec les autres membres" : "Regarder l'édition en direct", ls)) {
                goLive();
            }
            y += ui.s(54);
        }
        if (tester() && hasContent) {
            if (ui.button("play", {x, y, half, ui.s(44)}, "Jouer la dernière version", st))
                download(false, [this](const std::string& local) {
                    PlayOptions po;
                    po.editorTest = true;  // a test: no record, no campaign progress
                    if (auto lvl = g_.content.loadLevel(local)) po.level = *lvl;
                    g_.app.switchScene(makeGameScene(g_, local, po));
                });
        }
        if (hasContent && (cs.behind || !cs.exists || !cs.linked || conflict_.is_object())) {
            float bx = tester() ? x + half + ui.s(10) : x;
            if (editor() && cs.linked) {
                if (ui.button("pull", {bx, y, half, ui.s(44)}, "Récupérer la dernière version", st)) {
                    if (cs.modified) keepModifiedCopy();
                    download(true, [this, kept = cs.modified](const std::string&) {
                        conflict_ = Json();
                        say(kept ? "Dernière version récupérée ; vos modifications sont gardées dans « … (mes modifications) »." : "Dernière version récupérée.",
                            false);
                    });
                }
            } else if (!editor()) {
                if (ui.button("copy", {bx, y, half, ui.s(44)}, "Télécharger une copie", st))
                    download(false, [this](const std::string&) { say("Copie ajoutée à WORKSHOP → Mes niveaux.", false); });
            }
        }
        if (tester() || !editor()) y += ui.s(54);
        // Recent revisions.
        Json revs = tabData_.is_object() ? tabData_.value("revisions", Json::array()) : Json::array();
        if (!revs.empty()) {
            ui.label("Révisions récentes", {x, y + ui.s(10)}, 16, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
            y += ui.s(28);
            for (const auto& rv : revs) {
                if (y + ui.s(26) > a.bottom() - ui.s(manager() ? 162 : 58)) break;
                std::string line = "n°" + std::to_string(jget<int>(rv, "revision", 0)) + "  ·  " + jstr(rv.value("author", Json::object()), "username") + "  ·  " +
                                   OnlineUi::ageOf(jstr(rv, "created_at")) + (jstr(rv, "message").empty() ? std::string() : "  ·  " + jstr(rv, "message"));
                ui.label(line, {x, y + ui.s(12)}, ui.fitFont(line, FontFace::Body, 14, w), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
                y += ui.s(26);
            }
        }
        // Settings (admins), leave / delete.
        float by = a.bottom() - ui.s(46);
        if (manager()) {
            int vis = 0, jr = 0;
            for (int i = 0; i < 4; ++i)
                if (jstr(detail_, "visibility") == kVisibilities[i]) vis = i;
            for (int i = 0; i < 3; ++i)
                if (jstr(detail_, "join_role") == kJoinRoles[i]) jr = i;
            int v0 = vis, j0 = jr;
            ui.cycle("vis", {x, by - ui.s(104), w, ui.s(42)}, "Accès au projet", vis, kVisibilityLabels);
            ui.cycle("jr", {x, by - ui.s(52), w, ui.s(42)}, "Rôle des nouveaux membres", jr, kJoinRoleLabels);
            if (vis != v0 || jr != j0)
                g_.online.send("PATCH", "/api/v1/projects/" + selected_, {{"visibility", kVisibilities[vis]}, {"join_role", kJoinRoles[jr]}},
                               OnlineService::guard(life_, [this](const ApiResult& r) { done(r, "Réglages du projet enregistrés."); }));
        }
        UIContext::ButtonStyle d;
        d.small = true;
        d.danger = true;
        if (role() == "owner") {
            if (ui.button("delete", {x, by, w, ui.s(42)}, "Supprimer le projet", d)) confirm_ = "delete";
        } else if (ui.button("leave", {x, by, w, ui.s(42)}, "Quitter le projet", d)) {
            confirm_ = "leave";
        }
    }

    void drawMembers(const Rect& a) {
        UIContext& ui = g_.ui;
        Json list = tabData_.is_object() ? tabData_.value("members", Json::array()) : Json::array();
        if (list.empty()) {
            ui.label("Chargement…", {a.x, a.y + ui.s(14)}, 16, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            return;
        }
        const float rh = ui.s(60), gap = ui.s(6);
        ui.beginScroll("members", a, list.size() * (rh + gap));
        float cy = a.y - ui.scrollOffset();
        const std::string me = g_.online.playerId(), my = role();
        for (const auto& m : list) {
            Rect row{a.x, cy, a.w - ui.s(14), rh};
            cy += rh + gap;
            if (row.bottom() < a.y || row.y > a.bottom()) continue;
            ui.r().fillRoundedRect(row, ui.s(8), Color{14, 22, 46, 220});
            const Json& pl = m.value("player", Json::object());
            std::string pid = jstr(pl, "id"), r = jstr(m, "role");
            ui_.drawAvatar(jstr(pl, "avatar", "avatar_01"), pid, {row.x + ui.s(10), row.y + ui.s(10), ui.s(40), ui.s(40)});
            // Actions on the right, measured first.
            bool canEdit = pid != me && r != "owner" && (my == "owner" || (my == "admin" && roleRank(r) < 4));
            float actionsW = canEdit ? ui.s(190) + ui.s(100) + ui.s(10) : 0.0f;
            if (my == "owner" && pid != me) actionsW += ui.s(120);
            float nameRoom = row.w - ui.s(70) - actionsW - ui.s(20);
            std::string name = jstr(pl, "username") + (pid == me ? " (vous)" : "");
            ui.label(name, {row.x + ui.s(62), row.y + ui.s(20)}, ui.fitFont(name, FontFace::Bold, 17, nameRoom), ui.theme().text, Align::Left, FontFace::Bold,
                     VAlign::Middle);
            std::string since = (canEdit ? std::string() : roleName(r) + "  ·  ") + "rejoint " + OnlineUi::ageOf(jstr(m, "joined_at"));
            ui.label(since, {row.x + ui.s(62), row.y + ui.s(42)}, ui.fitFont(since, FontFace::Body, 13, nameRoom), ui.theme().textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
            float bx = row.right() - ui.s(10) - actionsW;
            if (canEdit) {
                // The roles this member may receive from me.
                std::vector<std::string> opts = {"Lecteur", "Testeur", "Éditeur"};
                if (my == "owner") opts.push_back("Administrateur");
                int cur = std::max(0, roleRank(r) - 1);
                int sel = std::min(cur, static_cast<int>(opts.size()) - 1);
                int before = sel;
                ui.cycle("role" + pid, {bx, row.y + ui.s(10), ui.s(190), ui.s(40)}, "", sel, opts);
                if (sel != before)
                    g_.online.send("PATCH", "/api/v1/projects/" + selected_ + "/members/" + pid, {{"role", kRoles[sel]}},
                                   OnlineService::guard(life_, [this](const ApiResult& r) { done(r, "Rôle modifié."); }));
                UIContext::ButtonStyle d;
                d.small = true;
                d.danger = true;
                d.fontSize = 15;
                if (ui.button("rm" + pid, {bx + ui.s(200), row.y + ui.s(10), ui.s(100), ui.s(40)}, "Retirer", d)) {
                    confirm_ = "remove";
                    confirmTarget_ = pid;
                    confirmName_ = jstr(pl, "username");
                }
                bx += ui.s(310);
            }
            if (my == "owner" && pid != me) {
                UIContext::ButtonStyle t;
                t.small = true;
                t.fontSize = 15;
                if (ui.button("tr" + pid, {bx, row.y + ui.s(10), ui.s(110), ui.s(40)}, "Transférer", t)) {
                    confirm_ = "transfer";
                    confirmTarget_ = pid;
                    confirmName_ = jstr(pl, "username");
                }
            }
        }
        ui.endScroll();
    }

    void drawInvites(Rect a) {
        UIContext& ui = g_.ui;
        float x = a.x, w = a.w, y = a.y;
        // A new key.
        float cw = (w - ui.s(20)) / 3.0f;
        ui.cycle("irole", {x, y, cw, ui.s(42)}, "", inviteRole_, role() == "owner" ? kInviteRoleLabels
                                                                               : std::vector<std::string>(kInviteRoleLabels.begin(), kInviteRoleLabels.end() - 1));
        ui.cycle("iuses", {x + cw + ui.s(10), y, cw, ui.s(42)}, "", inviteUses_, kUseLabels);
        ui.cycle("ihours", {x + 2 * (cw + ui.s(10)), y, cw, ui.s(42)}, "", inviteHours_, kHourLabels);
        y += ui.s(52);
        UIContext::ButtonStyle p;
        p.primary = true;
        p.small = true;
        if (ui.button("mkkey", {x, y, w, ui.s(42)}, "Créer une clé d'invitation", p)) {
            Json body = {{"role", kInviteRoles[inviteRole_]}, {"expires_hours", kHours[inviteHours_]}};
            if (kUses[inviteUses_] > 0) body["max_uses"] = kUses[inviteUses_];
            g_.online.send("POST", "/api/v1/projects/" + selected_ + "/invites", body, OnlineService::guard(life_, [this](const ApiResult& r) {
                               if (!r.ok) return say(r.message, true);
                               createdKey_ = jstr(r.data, "key");
                               createdLink_ = jstr(r.data, "link");
                               say("Clé créée : elle ne sera plus affichée, copiez-la maintenant.", false);
                               loadTab();
                           }));
        }
        y += ui.s(52);
        if (!createdKey_.empty()) {
            Rect box{x, y, w, ui.s(96)};
            ui.r().fillRoundedRect(box, ui.s(10), ui.theme().accent.withAlphaF(0.12f));
            ui.r().drawRoundedRect(box, ui.s(10), ui.theme().accent, ui.s(1.2f));
            ui.label(createdKey_, {box.x + ui.s(14), box.y + ui.s(24)}, ui.fitFont(createdKey_, FontFace::Bold, 20, box.w - ui.s(28)), ui.theme().accent, Align::Left,
                     FontFace::Bold, VAlign::Middle);
            UIContext::ButtonStyle s;
            s.small = true;
            s.fontSize = 15;
            float bw = (box.w - ui.s(38)) * 0.5f;
            if (ui.button("cpkey", {box.x + ui.s(14), box.y + ui.s(48), bw, ui.s(36)}, "Copier la clé", s)) {
                SDL_SetClipboardText(createdKey_.c_str());
                say("Clé copiée dans le presse-papiers.", false);
            }
            if (ui.button("cplink", {box.x + ui.s(24) + bw, box.y + ui.s(48), bw, ui.s(36)}, "Copier le lien", s)) {
                SDL_SetClipboardText(createdLink_.c_str());
                say("Lien copié : collez-le dans « Rejoindre » ou ouvrez-le sur un ordinateur où le jeu est installé.", false);
            }
            y += ui.s(106);
        }
        // Direct invitation by pseudonym.
        float ib = ui.s(160);
        ui.textField("who", {x, y, w - ib - ui.s(8), ui.s(42)}, invitee_, "Inviter un joueur par son pseudonyme", 20);
        UIContext::ButtonStyle s;
        s.small = true;
        s.enabled = invitee_.size() >= 2;
        if (ui.button("invite", {x + w - ib, y, ib, ui.s(42)}, "Inviter", s)) inviteByName();
        y += ui.s(54);
        // Existing invitations.
        Json list = tabData_.is_object() ? tabData_.value("invites", Json::array()) : Json::array();
        Rect view{x, y, w, a.bottom() - y};
        if (list.empty()) {
            ui.label("Aucune invitation.", {x, y + ui.s(14)}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            return;
        }
        const float rh = ui.s(50), gap = ui.s(6);
        ui.beginScroll("invites", view, list.size() * (rh + gap));
        float cy = view.y - ui.scrollOffset();
        for (const auto& i : list) {
            Rect row{view.x, cy, view.w - ui.s(14), rh};
            cy += rh + gap;
            if (row.bottom() < view.y || row.y > view.bottom()) continue;
            ui.r().fillRoundedRect(row, ui.s(8), Color{14, 22, 46, 220});
            std::string st = jstr(i, "status");
            std::string who = i.contains("target") && i["target"].is_object() ? "pour " + jstr(i["target"], "username") : "clé …" + jstr(i, "key_hint");
            std::string uses = i["max_uses"].is_number() ? std::to_string(jget<int>(i, "uses", 0)) + "/" + std::to_string(jget<int>(i, "max_uses", 0))
                                                        : std::to_string(jget<int>(i, "uses", 0)) + " utilisations";
            std::string line = who + "  ·  " + roleName(jstr(i, "role")) + "  ·  " + uses + "  ·  " +
                               (st == "active" ? "expire " + expiryText(jstr(i, "expires_at")) : st == "revoked" ? std::string("révoquée")
                                                                                         : st == "expired" ? std::string("expirée")
                                                                                                           : std::string("épuisée"));
            float rb = st == "active" ? ui.s(130) : 0.0f;
            ui.label(line, {row.x + ui.s(12), row.center().y}, ui.fitFont(line, FontFace::Body, 15, row.w - rb - ui.s(30)),
                     st == "active" ? ui.theme().text : ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            if (st == "active") {
                UIContext::ButtonStyle d;
                d.small = true;
                d.danger = true;
                d.fontSize = 15;
                if (ui.button("rv" + jstr(i, "id"), {row.right() - rb - ui.s(8), row.y + ui.s(8), rb, row.h - ui.s(16)}, "Révoquer", d))
                    g_.online.send("DELETE", "/api/v1/projects/" + selected_ + "/invites/" + jstr(i, "id"), Json(),
                                   OnlineService::guard(life_, [this](const ApiResult& r) {
                                       done(r, "Invitation révoquée.", false);
                                       loadTab();
                                   }));
            }
        }
        ui.endScroll();
    }

    void inviteByName() {
        std::string name = invitee_;
        g_.online.get("/api/v1/players/search?q=" + urlEncode(name), OnlineService::guard(life_, [this, name](const ApiResult& r) {
                          if (!r.ok) return say(r.message, true);
                          std::string id;
                          for (const auto& p : r.data.value("players", Json::array())) {
                              std::string u = jstr(p, "username");
                              if (u.size() == name.size() && std::equal(u.begin(), u.end(), name.begin(), [](char a, char b) {
                                      return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                                  }))
                                  id = jstr(p, "id");
                          }
                          if (id.empty()) return say("Aucun joueur ne s'appelle « " + name + " ».", true);
                          g_.online.send("POST", "/api/v1/projects/" + selected_ + "/invites", {{"role", kInviteRoles[inviteRole_]}, {"player_id", id}},
                                         OnlineService::guard(life_, [this](const ApiResult& r2) {
                                             if (!r2.ok) return say(r2.message, true);
                                             invitee_.clear();
                                             say("Invitation envoyée : le joueur la verra dans ses notifications et dans PROJETS → Invitations.", false);
                                             loadTab();
                                         }));
                      }),
                      false);
    }

    void drawRequests(const Rect& a) {
        UIContext& ui = g_.ui;
        Json list = tabData_.is_object() ? tabData_.value("requests", Json::array()) : Json::array();
        if (list.empty()) {
            ui.labelWrapped(jstr(detail_, "visibility") == "request" ? "Aucune demande en attente."
                                                                     : "Les demandes ne sont possibles que si l'accès du projet est « Public, sur demande ».",
                            {a.x, a.y + ui.s(6)}, a.w, 15, ui.theme().textDim);
            return;
        }
        const float rh = ui.s(96), gap = ui.s(8);
        ui.beginScroll("requests", a, list.size() * (rh + gap));
        float cy = a.y - ui.scrollOffset();
        for (const auto& q : list) {
            Rect row{a.x, cy, a.w - ui.s(14), rh};
            cy += rh + gap;
            if (row.bottom() < a.y || row.y > a.bottom()) continue;
            ui.r().fillRoundedRect(row, ui.s(8), Color{14, 22, 46, 220});
            const Json& pl = q.value("player", Json::object());
            std::string pid = jstr(pl, "id");
            ui_.drawAvatar(jstr(pl, "avatar", "avatar_01"), pid, {row.x + ui.s(10), row.y + ui.s(10), ui.s(40), ui.s(40)});
            std::string head = jstr(pl, "username") + "  ·  " + OnlineUi::ageOf(jstr(q, "created_at"));
            ui.label(head, {row.x + ui.s(62), row.y + ui.s(20)}, ui.fitFont(head, FontFace::Bold, 16, row.w - ui.s(80)), ui.theme().text, Align::Left,
                     FontFace::Bold, VAlign::Middle);
            std::string msg = jstr(q, "message").empty() ? "(sans message)" : "« " + jstr(q, "message") + " »";
            ui.label(msg, {row.x + ui.s(62), row.y + ui.s(42)}, ui.fitFont(msg, FontFace::Body, 14, row.w - ui.s(80)), ui.theme().textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
            float bw = (row.w - ui.s(82)) / 3.0f;
            float bx = row.x + ui.s(62);
            ui.cycle("rr" + pid, {bx, row.y + ui.s(56), bw, ui.s(34)}, "", requestRole_, kJoinRoleLabels);
            UIContext::ButtonStyle p;
            p.small = true;
            p.primary = true;
            p.fontSize = 15;
            if (ui.button("ok" + pid, {bx + bw + ui.s(10), row.y + ui.s(56), bw - ui.s(5), ui.s(34)}, "Accepter", p))
                g_.online.send("POST", "/api/v1/projects/" + selected_ + "/requests/" + pid, {{"accept", true}, {"role", kJoinRoles[requestRole_]}},
                               OnlineService::guard(life_, [this](const ApiResult& r) { done(r, "Membre ajouté."); }));
            UIContext::ButtonStyle d;
            d.small = true;
            d.danger = true;
            d.fontSize = 15;
            if (ui.button("no" + pid, {bx + 2 * bw + ui.s(15), row.y + ui.s(56), bw - ui.s(5), ui.s(34)}, "Refuser", d))
                g_.online.send("POST", "/api/v1/projects/" + selected_ + "/requests/" + pid, {{"accept", false}},
                               OnlineService::guard(life_, [this](const ApiResult& r) { done(r, "Demande refusée."); }));
        }
        ui.endScroll();
    }

    static std::string activityText(const Json& a) {
        std::string who = a.contains("actor") && a["actor"].is_object() ? jstr(a["actor"], "username") : std::string("Un ancien membre");
        const Json& d = a.value("details", Json::object());
        std::string act = jstr(a, "action"), other = jstr(d, "player_name");
        if (act == "created") return who + " a créé le projet";
        if (act == "saved")
            return who + " a envoyé la révision " + std::to_string(jget<int>(d, "revision", 0)) +
                   (jstr(d, "message").empty() ? std::string() : " : « " + jstr(d, "message") + " »");
        if (act == "member_joined") return who + " a rejoint le projet comme " + roleWord(jstr(d, "role"));
        if (act == "member_left") return who + " a quitté le projet";
        if (act == "member_removed") return who + " a retiré " + other + " du projet";
        if (act == "role_changed") return who + " a donné à " + other + " le rôle " + roleWord(jstr(d, "to"));
        if (act == "transferred") return who + " a transféré le projet à " + jstr(d, "to_name");
        if (act == "invite_created") return who + " a créé une clé d'invitation (" + roleWord(jstr(d, "role")) + ")";
        if (act == "invite_revoked") return who + " a révoqué une invitation";
        if (act == "invited") return who + " a invité " + other + " (" + roleWord(jstr(d, "role")) + ")";
        if (act == "join_requested") return who + " demande à rejoindre le projet";
        if (act == "request_refused") return who + " a refusé la demande de " + other;
        if (act == "settings") return who + " a modifié les réglages du projet";
        return who + " : " + act;
    }

    // ------------------------------------------------------------ history
    /// Downloads a revision or a named version as an independent level (never the linked copy).
    void historyCopy(const std::string& path, const std::string& what, const std::string& slug) {
        std::string id = selected_, name = jstr(detail_, "name");
        working_ = true;
        g_.online.getBinary(path, [this, id, name, what, slug, life = std::weak_ptr<int>(life_)](const HttpResponse& r) {
            if (!life.lock()) return;
            working_ = false;
            if (r.status != 200) return say(r.networkFailure() ? "Connexion impossible : " + r.errorMessage : "Téléchargement refusé (" + std::to_string(r.status) + ").", true);
            Json j = Json::parse(r.body, nullptr, false);
            std::string err;
            auto lvl = j.is_object() ? LevelData::fromJson(j, &err) : std::nullopt;
            if (!lvl) return say("Contenu illisible : " + (err.empty() ? std::string("format inconnu") : err), true);
            lvl->id = localIdFor(id) + "_" + slug;
            lvl->name = name + " (" + what + ")";
            if (!g_.content.saveUserLevel(*lvl)) return say("Impossible d'écrire la copie.", true);
            g_.ratings.contentChanged();
            say("« " + lvl->name + " » est dans WORKSHOP → Mes niveaux.", false);
        });
    }

    void compareWith(int from, int to) {
        compare_ = Json();
        compareLabel_ = "Révision " + std::to_string(from) + " → révision " + std::to_string(to);
        int gen = ++compareGen_;
        g_.online.get("/api/v1/projects/" + selected_ + "/compare?from=" + std::to_string(from) + "&to=" + std::to_string(to),
                      OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                          if (gen != compareGen_) return;
                          if (!r.ok) {
                              compareLabel_.clear();
                              return say(r.message, true);
                          }
                          compare_ = r.data;
                      }),
                      false);
    }

    /// A key of the level or of an object, in words.
    static std::string keyName(const std::string& k) {
        static const std::map<std::string, std::string> names = {
            {"type", "type"}, {"layer", "calque"}, {"rect", "position et taille"}, {"rotation", "rotation"}, {"flipX", "retournement"},
            {"z", "profondeur"}, {"props", "propriétés"}, {"name", "nom"}, {"description", "description"}, {"planet", "planète"},
            {"music", "musique"}, {"bossMusic", "musique du boss"}, {"ambience", "ambiance"}, {"ambientLight", "lumière"},
            {"background", "fond"}, {"backgroundColor", "couleur du fond"}, {"bounds", "limites"}, {"cameraZoom", "zoom de la caméra"},
            {"damageFactor", "dégâts"}, {"difficulty", "difficulté"}, {"events", "événements"}, {"intro", "cinématique d'entrée"},
            {"outro", "cinématique de fin"}, {"killY", "hauteur de chute"}, {"layers", "calques"}, {"lives", "vies"},
            {"maxPowers", "pouvoirs maximum"}, {"medals", "médailles"}, {"metadata", "informations"}, {"startPowers", "pouvoirs de départ"},
            {"timer", "chronomètre"}, {"author", "auteur"}, {"format", "format"}, {"formatVersion", "version du format"}, {"version", "version"}};
        auto it = names.find(k);
        return it != names.end() ? it->second : k;
    }

    static std::string typeName(const std::string& type) {
        const ObjectTypeInfo* t = ObjectTypes::find(type);
        return t ? t->name : type;
    }

    /// The comparison in `r`: the counts, then each change.
    void drawCompare(const Rect& r) {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(r, ui.s(8), Color{16, 24, 50, 235});
        float x = r.x + ui.s(12), w = r.w - ui.s(24);
        if (!compare_.is_object()) {
            std::string t = compareLabel_.empty() ? "Choisissez une révision à comparer, puis « Comparer »." : compareLabel_ + " : comparaison…";
            ui.label(t, {x, r.y + ui.s(18)}, ui.fitFont(t, FontFace::Body, 14, w), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            return;
        }
        const Json& c = compare_.value("counts", Json::object());
        auto n = [&](const char* k) { return std::to_string(jget<int>(c, k, 0)); };
        std::string head = compareLabel_ + " : " + n("added") + " ajouté(s), " + n("removed") + " supprimé(s), " + n("changed") + " modifié(s), " +
                           n("settings") + " réglage(s)";
        ui.label(head, {x, r.y + ui.s(18)}, ui.fitFont(head, FontFace::Bold, 15, w), ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
        std::vector<std::pair<Color, std::string>> lines;
        for (const auto& o : compare_.value("added", Json::array())) lines.push_back({ui.theme().success, "+ " + typeName(jstr(o, "type")) + "  (" + jstr(o, "id") + ")"});
        for (const auto& o : compare_.value("removed", Json::array())) lines.push_back({ui.theme().danger, "- " + typeName(jstr(o, "type")) + "  (" + jstr(o, "id") + ")"});
        for (const auto& o : compare_.value("changed", Json::array())) {
            std::string f;
            for (const auto& k : o.value("fields", Json::array()))
                if (k.is_string()) f += (f.empty() ? "" : ", ") + keyName(k.get<std::string>());
            lines.push_back({ui.theme().warm, "~ " + typeName(jstr(o, "type")) + "  (" + jstr(o, "id") + ") : " + f});
        }
        std::string settings;
        for (const auto& k : compare_.value("settings", Json::array()))
            if (k.is_string()) settings += (settings.empty() ? "" : ", ") + keyName(k.get<std::string>());
        if (!settings.empty()) lines.push_back({ui.theme().accent, "Réglages du niveau : " + settings});
        if (lines.empty()) lines.push_back({ui.theme().textDim, "Aucune différence : les deux révisions ont le même contenu."});
        Rect list{x, r.y + ui.s(36), w, r.h - ui.s(42)};
        const float lh = ui.s(22);
        ui.beginScroll("cmp", list, lines.size() * lh);
        float ly = list.y - ui.scrollOffset();
        for (const auto& [col, text] : lines) {
            if (ly + lh > list.y && ly < list.bottom())
                ui.label(text, {list.x, ly + lh * 0.5f}, ui.fitFont(text, FontFace::Body, 14, list.w - ui.s(16)), col, Align::Left, FontFace::Body, VAlign::Middle);
            ly += lh;
        }
        ui.endScroll();
    }

    void drawRevisions(const Rect& a) {
        UIContext& ui = g_.ui;
        float x = a.x, w = a.w, y = a.y;
        // Filters: author, words of the message.
        std::vector<std::string> authors = {"Tous les auteurs"};
        for (const auto& au : revAuthors_) authors.push_back(au.second);
        int au = std::clamp(revAuthor_, 0, static_cast<int>(authors.size()) - 1);
        float cw = std::floor(w * 0.42f);
        ui.cycle("author", {x, y, cw, ui.s(40)}, "", au, authors);
        std::string before = revQuery_;
        ui.textField("rq", {x + cw + ui.s(10), y, w - cw - ui.s(10), ui.s(40)}, revQuery_, "Chercher dans les messages", 60);
        if (au != revAuthor_) {
            revAuthor_ = au;
            selectedRev_ = 0;
            loadTab();
        }
        if (before != revQuery_) revSearchT_ = ui.time();
        if (revSearchT_ > 0.0f && ui.time() - revSearchT_ > 0.45f) {
            revSearchT_ = 0.0f;
            selectedRev_ = 0;
            loadTab();
        }
        y += ui.s(50);
        Json list = tabData_.is_object() ? tabData_.value("revisions", Json::array()) : Json::array();
        const bool hasSel = selectedRev_ > 0;
        const float panelH = hasSel ? ui.s(272) : 0.0f;
        Rect view{x, y, w, a.bottom() - y - (hasSel ? panelH + ui.s(10) : 0.0f)};
        if (list.empty()) {
            std::string t = !tabData_.is_object() ? "Chargement…" : (revAuthor_ || !revQuery_.empty()) ? "Aucune révision ne correspond." : "Aucune révision.";
            ui.label(t, {x, y + ui.s(14)}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        } else {
            const float rh = ui.s(54), gap = ui.s(4);
            ui.beginScroll("revs", view, list.size() * (rh + gap));
            float cy = view.y - ui.scrollOffset();
            for (const auto& rv : list) {
                Rect row{view.x, cy, view.w - ui.s(14), rh};
                cy += rh + gap;
                if (row.bottom() < view.y || row.y > view.bottom()) continue;
                int n = jget<int>(rv, "revision", 0);
                UIContext::ButtonStyle st;
                st.selected = n == selectedRev_;
                if (ui.button("rv" + std::to_string(n), row, "", st)) {
                    selectedRev_ = n;
                    compare_ = Json();
                    compareLabel_.clear();
                    compareTo_ = n > 1 ? n - 1 : std::max(1, jget<int>(detail_, "revision", 1));
                }
                std::string version = rv.contains("version") && rv["version"].is_string() ? rv["version"].get<std::string>() : std::string();
                float vw = version.empty() ? 0.0f : std::min(chipWidth(version), row.w * 0.35f);
                std::string msg = jstr(rv, "message");
                std::string head = "n°" + std::to_string(n) + "  " + (msg.empty() ? std::string("(sans message)") : msg);
                ui.label(head, {row.x + ui.s(12), row.y + ui.s(18)}, ui.fitFont(head, FontFace::Bold, 16, row.w - vw - ui.s(36)), ui.theme().text, Align::Left,
                         FontFace::Bold, VAlign::Middle);
                if (!version.empty()) chip(version, {row.right() - ui.s(12) - vw, row.y + ui.s(18)}, ui.theme().success);
                std::string who = rv.contains("author") && rv["author"].is_object() ? "par " + jstr(rv["author"], "username")
                                : jget<bool>(rv, "live", false)                    ? std::string("édition en direct")
                                                                                   : std::string("auteur parti");
                std::string sub = who + "  ·  " + OnlineUi::ageOf(jstr(rv, "created_at")) + "  ·  SHA-256 " + jstr(rv, "content_hash").substr(0, 12) + "…";
                ui.label(sub, {row.x + ui.s(12), row.y + ui.s(39)}, ui.fitFont(sub, FontFace::Body, 13, row.w - ui.s(24)), ui.theme().textDim, Align::Left,
                         FontFace::Body, VAlign::Middle);
            }
            ui.endScroll();
        }
        if (!hasSel) return;
        // What can be done with the selected revision.
        Rect pr{x, a.bottom() - panelH, w, panelH};
        ui.r().fillRoundedRect(pr, ui.s(10), Color{10, 16, 36, 245});
        ui.r().drawRoundedRect(pr, ui.s(10), ui.theme().accent.withAlphaF(0.45f), ui.s(1));
        float px = pr.x + ui.s(12), pw = pr.w - ui.s(24), py = pr.y + ui.s(10);
        int last = std::max(1, jget<int>(detail_, "revision", 1));
        std::vector<std::string> others;
        for (int i = last; i >= 1; --i) others.push_back("Comparer à la révision " + std::to_string(i));
        int other = std::clamp(last - compareTo_, 0, static_cast<int>(others.size()) - 1);
        const float bw = std::floor((pw - ui.s(20)) / 3.0f), bh = ui.s(38);
        ui.cycle("cmpwith", {px, py, bw * 2 + ui.s(10), bh}, "", other, others);
        compareTo_ = last - other;
        UIContext::ButtonStyle s;
        s.small = true;
        s.fontSize = 15;
        if (ui.button("cmp", {px + 2 * (bw + ui.s(10)), py, bw, bh}, "Comparer", s)) compareWith(compareTo_, selectedRev_);
        py += bh + ui.s(8);
        UIContext::ButtonStyle e = s;
        e.enabled = editor() && !working_;
        if (ui.button("restore", {px, py, bw, bh}, "Restaurer…", e)) confirm_ = "restore";
        UIContext::ButtonStyle c = s;
        c.enabled = !working_;
        if (ui.button("copy", {px + bw + ui.s(10), py, bw, bh}, "Télécharger une copie", c))
            historyCopy("/api/v1/projects/" + selected_ + "/content?revision=" + std::to_string(selectedRev_), "révision " + std::to_string(selectedRev_),
                        "r" + std::to_string(selectedRev_));
        if (ui.button("name", {px + 2 * (bw + ui.s(10)), py, bw, bh}, "Nommer une version…", e)) {
            naming_ = true;
            versionName_.clear();
            versionNotes_.clear();
        }
        py += bh + ui.s(10);
        drawCompare({px, py, pw, pr.bottom() - py - ui.s(10)});
    }

    void drawVersions(const Rect& a) {
        UIContext& ui = g_.ui;
        Json list = tabData_.is_object() ? tabData_.value("versions", Json::array()) : Json::array();
        Vec2 hs = ui.labelWrapped("Une version nomme une révision pour toujours : elle ne se modifie pas, et son empreinte SHA-256 est vérifiée à chaque "
                                  "téléchargement. Pour en créer une : onglet « Révisions », puis « Nommer une version… ».",
                                  {a.x, a.y}, a.w, 14, ui.theme().textDim);
        const bool cmp = !compareLabel_.empty();
        const float cmpH = cmp ? ui.s(200) : 0.0f;
        float top = a.y + hs.y + ui.s(12);
        Rect view{a.x, top, a.w, a.bottom() - top - (cmp ? cmpH + ui.s(10) : 0.0f)};
        if (list.empty()) {
            ui.label(tabData_.is_object() ? "Aucune version nommée pour l'instant." : "Chargement…", {view.x, view.y + ui.s(14)}, 15, ui.theme().textDim,
                     Align::Left, FontFace::Body, VAlign::Middle);
            return;
        }
        int last = std::max(1, jget<int>(detail_, "revision", 1));
        const float rh = ui.s(112), gap = ui.s(6);
        ui.beginScroll("vers", view, list.size() * (rh + gap));
        float cy = view.y - ui.scrollOffset();
        for (const auto& v : list) {
            Rect row{view.x, cy, view.w - ui.s(14), rh};
            cy += rh + gap;
            if (row.bottom() < view.y || row.y > view.bottom()) continue;
            ui.r().fillRoundedRect(row, ui.s(8), Color{14, 22, 46, 230});
            ui.r().drawRoundedRect(row, ui.s(8), ui.theme().border, ui.s(1));
            int rev = jget<int>(v, "revision", 0);
            std::string name = jstr(v, "name"), vid = jstr(v, "id"), hash = jstr(v, "content_hash");
            float rx = row.x + ui.s(12), rw = row.w - ui.s(24);
            ui.label(name, {rx, row.y + ui.s(18)}, ui.fitFont(name, FontFace::Bold, 18, rw), ui.theme().success, Align::Left, FontFace::Bold, VAlign::Middle);
            std::string by = v.contains("author") && v["author"].is_object() ? "par " + jstr(v["author"], "username") : std::string("auteur parti");
            std::string sub = "révision " + std::to_string(rev) + "  ·  SHA-256 " + hash.substr(0, 10) + "…" + (hash.size() > 10 ? hash.substr(hash.size() - 6) : "") +
                              "  ·  " + by + "  ·  " + OnlineUi::ageOf(jstr(v, "created_at"));
            ui.label(sub, {rx, row.y + ui.s(40)}, ui.fitFont(sub, FontFace::Body, 13, rw), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            std::string notes = jstr(v, "notes");
            for (char& ch : notes)
                if (ch == '\n') ch = ' ';
            if (!notes.empty())
                ui.label(notes, {rx, row.y + ui.s(61)}, ui.fitFont(notes, FontFace::Body, 13, rw, 0.8f), ui.theme().text, Align::Left, FontFace::Body,
                         VAlign::Middle);
            const float bw = std::floor((rw - ui.s(20)) / 3.0f), by0 = row.bottom() - ui.s(36);
            UIContext::ButtonStyle s;
            s.small = true;
            s.fontSize = 14;
            s.enabled = !working_;
            if (ui.button("vc" + vid, {rx, by0, bw, ui.s(28)}, "Télécharger une copie", s))
                historyCopy("/api/v1/projects/" + selected_ + "/versions/" + vid + "/content", "version " + name, "v" + vid.substr(0, 8));
            UIContext::ButtonStyle d = s;
            d.enabled = rev != last;
            if (ui.button("vd" + vid, {rx + bw + ui.s(10), by0, bw, ui.s(28)}, rev == last ? "C'est la dernière" : "Comparer à la dernière", d))
                compareWith(rev, last);
            UIContext::ButtonStyle e = s;
            e.enabled = editor() && !working_ && rev != last;
            if (ui.button("vr" + vid, {rx + 2 * (bw + ui.s(10)), by0, bw, ui.s(28)}, "Restaurer…", e)) {
                selectedRev_ = rev;
                confirm_ = "restore";
            }
        }
        ui.endScroll();
        if (cmp) drawCompare({a.x, a.bottom() - cmpH, a.w, cmpH});
    }

    void drawNaming() {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 170});
        Rect box{ui.screenW() * 0.5f - ui.s(340), ui.screenH() * 0.5f - ui.s(180), ui.s(680), ui.s(360)};
        ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
        ui.r().drawRoundedRect(box, ui.s(18), ui.theme().accent.withAlphaF(0.55f), ui.s(1.5f));
        ui.pushId("projects/name");
        float x = box.x + ui.s(30), w = box.w - ui.s(60), y = box.y + ui.s(24);
        std::string title = "Nommer la révision " + std::to_string(selectedRev_);
        ui.label(title, {x, y + ui.s(16)}, ui.fitFont(title, FontFace::Title, 26, w), ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        y += ui.s(44);
        Vec2 hs = ui.labelWrapped("Le nom est définitif : une version ne se modifie pas et ne se remplace pas. Les autres membres sont prévenus.", {x, y}, w, 15,
                                  ui.theme().textDim);
        y += hs.y + ui.s(14);
        ui.textField("vname", {x, y, w, ui.s(44)}, versionName_, "Nom (par exemple « Bêta 1 »)", 40);
        y += ui.s(54);
        ui.textField("vnotes", {x, y, w, ui.s(44)}, versionNotes_, "Notes (facultatif)", 300);
        UIContext::ButtonStyle p;
        p.primary = true;
        p.enabled = !versionName_.empty() && !working_;
        if (ui.button("ok", {x, box.bottom() - ui.s(70), ui.s(260), ui.s(46)}, working_ ? "Création…" : "Créer la version", p)) {
            working_ = true;
            g_.online.send("POST", "/api/v1/projects/" + selected_ + "/versions", {{"name", versionName_}, {"revision", selectedRev_}, {"notes", versionNotes_}},
                           OnlineService::guard(life_, [this](const ApiResult& r) {
                               working_ = false;
                               if (!r.ok) return say(r.message, true);
                               naming_ = false;
                               say("Version « " + jstr(r.data.value("version", Json::object()), "name") + " » créée.", false);
                               loadTab();
                           }));
        } else if (ui.button("cancel", {box.right() - ui.s(210), box.bottom() - ui.s(70), ui.s(180), ui.s(46)}, "Annuler") || (ui.backPressed() && !ui.editingText())) {
            naming_ = false;
        }
        ui.popId();
    }

    void drawActivity(const Rect& a) {
        UIContext& ui = g_.ui;
        Json list = tabData_.is_object() ? tabData_.value("activity", Json::array()) : Json::array();
        if (list.empty()) {
            ui.label("Chargement…", {a.x, a.y + ui.s(14)}, 16, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            return;
        }
        const float rh = ui.s(38);
        ui.beginScroll("activity", a, list.size() * rh);
        float cy = a.y - ui.scrollOffset();
        for (const auto& e : list) {
            if (cy + rh > a.y && cy < a.bottom()) {
                std::string when = OnlineUi::ageOf(jstr(e, "created_at"));
                float ww = ui.s(120);
                std::string t = activityText(e);
                ui.label(t, {a.x, cy + rh * 0.5f}, ui.fitFont(t, FontFace::Body, 15, a.w - ww - ui.s(30)), ui.theme().text, Align::Left, FontFace::Body,
                         VAlign::Middle);
                ui.label(when, {a.right() - ui.s(16), cy + rh * 0.5f}, 13, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
            }
            cy += rh;
        }
        ui.endScroll();
    }

    // ------------------------------------------------------------- dialogs
    void openCreate() {
        creating_ = true;
        newName_.clear();
        newDesc_.clear();
        newVisibility_ = 0;
        newLevel_ = 0;
        myLevels_.clear();
        for (const LevelEntry* e : g_.content.levels())
            if (e->source == ContentSource::User && e->id.rfind("projet_", 0) != 0) myLevels_.push_back(e);
        g_.ui.setFocus("projects/new/name");
    }

    void drawCreate() {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 170});
        Rect box{ui.screenW() * 0.5f - ui.s(360), ui.screenH() * 0.5f - ui.s(260), ui.s(720), ui.s(520)};
        ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
        ui.r().drawRoundedRect(box, ui.s(18), ui.theme().accent.withAlphaF(0.55f), ui.s(1.5f));
        ui.pushId("projects/new");
        float x = box.x + ui.s(30), w = box.w - ui.s(60), y = box.y + ui.s(26);
        ui.label("Nouveau projet", {x, y + ui.s(14)}, 26, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        y += ui.s(50);
        std::vector<std::string> levels;
        for (const LevelEntry* e : myLevels_) levels.push_back(e->name);
        if (levels.empty()) {
            Vec2 s = ui.labelWrapped("Vous n'avez aucun niveau personnel : créez d'abord un niveau dans WORKSHOP → Mes niveaux, puis faites-en un projet.",
                                     {x, y}, w, 16, ui.theme().warm);
            y += s.y + ui.s(10);
        } else {
            ui.cycle("level", {x, y, w, ui.s(44)}, "Niveau", newLevel_, levels);
            y += ui.s(54);
        }
        ui.textField("name", {x, y, w, ui.s(44)}, newName_, "Nom du projet (3 à 60 caractères)", 60);
        y += ui.s(54);
        ui.textField("desc", {x, y, w, ui.s(44)}, newDesc_, "Description (facultatif)", 500);
        y += ui.s(54);
        ui.cycle("vis", {x, y, w, ui.s(44)}, "Accès", newVisibility_, kVisibilityLabels);
        y += ui.s(54);
        ui.labelWrapped("Vous en serez le propriétaire. Le niveau choisi devient la révision 1 du projet ; votre niveau d'origine n'est pas modifié.",
                        {x, y}, w, 14, ui.theme().textDim);
        UIContext::ButtonStyle p;
        p.primary = true;
        p.enabled = !myLevels_.empty() && newName_.size() >= 3 && !working_;
        if (ui.button("ok", {x, box.bottom() - ui.s(70), ui.s(260), ui.s(46)}, working_ ? "Création…" : "Créer le projet", p)) createProject();
        if (ui.button("cancel", {box.right() - ui.s(210), box.bottom() - ui.s(70), ui.s(180), ui.s(46)}, "Annuler") || ui.backPressed()) creating_ = false;
        ui.popId();
    }

    void createProject() {
        const LevelEntry* e = myLevels_[static_cast<size_t>(std::clamp(newLevel_, 0, static_cast<int>(myLevels_.size()) - 1))];
        auto lvl = g_.content.loadLevel(e->id);
        if (!lvl) return say("Niveau illisible.", true);
        working_ = true;
        Json body = {{"name", newName_}, {"description", newDesc_}, {"visibility", kVisibilities[newVisibility_]}, {"planet", lvl->planet},
                     {"content", lvl->toJson()}, {"message", "Version initiale"}};
        g_.online.send("POST", "/api/v1/projects", body, OnlineService::guard(life_, [this](const ApiResult& r) {
                           working_ = false;
                           if (!r.ok) return say(r.message, true);
                           creating_ = false;
                           listTab_ = 0;
                           std::string id = jstr(r.data.value("project", Json::object()), "id");
                           done(r, "Projet créé : invitez des joueurs dans l'onglet Invitations.");
                           open(id);
                       }));
    }

    void drawConfirm() {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 170});
        Rect box{ui.screenW() * 0.5f - ui.s(330), ui.screenH() * 0.5f - ui.s(130), ui.s(660), ui.s(260)};
        ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
        ui.r().drawRoundedRect(box, ui.s(18), ui.theme().danger.withAlphaF(0.6f), ui.s(1.5f));
        ui.pushId("projects/confirm");
        std::string name = jstr(detail_, "name");
        std::string text = confirm_ == "delete"   ? "Supprimer définitivement « " + name + " » et toutes ses révisions ? Les copies locales des membres restent sur leurs ordinateurs."
                         : confirm_ == "leave"    ? "Quitter « " + name + " » ? Il faudra une nouvelle invitation pour revenir. Votre copie locale reste dans vos niveaux."
                         : confirm_ == "remove"   ? "Retirer " + confirmName_ + " du projet ?"
                         : confirm_ == "restore"
                             ? "Restaurer la révision " + std::to_string(selectedRev_) +
                                   " ? Son contenu devient une nouvelle révision : rien n'est effacé de l'historique, et l'édition en direct en cours est "
                                   "d'abord enregistrée."
                                                  : "Transférer « " + name + " » à " + confirmName_ + " ? Vous deviendrez administrateur du projet.";
        ui.labelWrapped(text, {box.x + ui.s(30), box.y + ui.s(30)}, box.w - ui.s(60), 17, ui.theme().text);
        UIContext::ButtonStyle d;
        d.danger = true;
        std::string label = confirm_ == "delete"  ? "Supprimer"
                          : confirm_ == "leave"   ? "Quitter"
                          : confirm_ == "remove"  ? "Retirer"
                          : confirm_ == "restore" ? "Restaurer"
                                                  : "Transférer";
        if (ui.button("ok", {box.x + ui.s(30), box.bottom() - ui.s(70), ui.s(240), ui.s(46)}, label, d)) {
            std::string what = confirm_, pid = confirmTarget_, id = selected_;
            confirm_.clear();
            std::string base = "/api/v1/projects/" + id;
            if (what == "delete")
                g_.online.send("DELETE", base, Json(), OnlineService::guard(life_, [this](const ApiResult& r) {
                                   if (r.ok) selected_.clear();
                                   done(r, "Projet supprimé.");
                               }));
            else if (what == "leave")
                g_.online.send("DELETE", base + "/members/" + g_.online.playerId(), Json(), OnlineService::guard(life_, [this](const ApiResult& r) {
                                   if (r.ok) selected_.clear();
                                   done(r, "Vous avez quitté le projet.");
                               }));
            else if (what == "restore")
                g_.online.send("POST", base + "/revisions/" + std::to_string(selectedRev_) + "/restore", Json::object(),
                               OnlineService::guard(life_, [this](const ApiResult& r) {
                                   int n = jget<int>(r.data.value("saved", Json::object()), "revision", 0);
                                   done(r, "Restauration enregistrée : révision " + std::to_string(n) + ".");
                                   if (r.ok) selectedRev_ = 0;
                               }));
            else if (what == "remove")
                g_.online.send("DELETE", base + "/members/" + pid, Json(), OnlineService::guard(life_, [this](const ApiResult& r) { done(r, "Membre retiré."); }));
            else
                g_.online.send("POST", base + "/transfer", {{"player_id", pid}},
                               OnlineService::guard(life_, [this](const ApiResult& r) { done(r, "Projet transféré."); }));
        } else if (ui.button("cancel", {box.right() - ui.s(210), box.bottom() - ui.s(70), ui.s(180), ui.s(46)}, "Annuler") || ui.backPressed()) {
            confirm_.clear();
        }
        ui.popId();
    }

    GameServices& g_;
    OnlineUi ui_;
    MenuBackground bg_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string wanted_;
    bool accountAsked_ = false;
    Rect header_, zoneA_, zoneL_, zoneD_;
    ProjectLinks links_;
    // List.
    int listTab_ = 0, listGen_ = 0, invitationCount_ = 0;
    Json list_ = Json::array();
    bool listBusy_ = false, listLoaded_ = false;
    std::string key_, query_;
    float searchDirty_ = 0.0f;
    // Detail.
    std::string selected_;
    Json detail_, tabData_, conflict_;
    int detailTab_ = 0, tabGen_ = 0, compareGen_ = 0;
    bool working_ = false;
    std::string message_, joinMessage_, invitee_, createdKey_, createdLink_;
    int inviteRole_ = 2, inviteUses_ = 0, inviteHours_ = 2, requestRole_ = 0;
    // Dialogs.
    bool creating_ = false;
    std::string newName_, newDesc_;
    int newVisibility_ = 0, newLevel_ = 0;
    std::vector<const LevelEntry*> myLevels_;
    std::string confirm_, confirmTarget_, confirmName_;
    // History (revisions, comparison, named versions).
    int selectedRev_ = 0, compareTo_ = 1, revAuthor_ = 0;
    float revSearchT_ = 0.0f;
    std::string revQuery_, compareLabel_, versionName_, versionNotes_;
    std::vector<std::pair<std::string, std::string>> revAuthors_;
    Json compare_;
    bool naming_ = false;
    std::string status_;
    bool statusError_ = false;
    float statusT_ = -100.0f;
};

}  // namespace

std::unique_ptr<Scene> makeProjects(GameServices& g, const std::string& projectId) { return std::make_unique<ProjectsScene>(g, projectId); }

std::unique_ptr<Scene> makeEditorExit(GameServices& g, const std::string& levelId) {
    ProjectLinks links;
    links.load();
    if (links.links.contains(levelId)) return makeProjects(g, jstr(links.links[levelId], "project_id"));
    return makeContentManager(g);
}

}  // namespace cosmic
