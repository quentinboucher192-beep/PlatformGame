// 2.62: « COOPÉRATION », the lobby of a team (docs/COOP.md, mockup screen 1): the EXTRÊME level,
// the 2 or 3 players, the rules of the team. On one PC: player 1 plays with the keyboard and
// the mouse (and a gamepad if it chooses one), players 2 and 3 take a place by pressing Start
// or A on their own gamepad, and leave it with B. Every text is measured and placed one after
// the other (fitted, wrapped or shortened): nothing overlaps.
// 2.63.5: « Campagne / Communauté »: the EXTRÊME community levels installed from Cosmic Online
// too (docs/COOP.md § 9). A player of a room who does not have its level (or not that version)
// gets it before « Prêt »: downloaded from Cosmic Online, or sent by the PC that hosts on the
// local network (bounded, SHA-256 checked off the main thread), then its fingerprint compared:
// a game never starts with two versions of a level.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <map>
#include <set>

#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/input/LocalPad.h"
#include "game/GameServices.h"
#include "game/coop/CoopLevels.h"
#include "game/coop/LanHost.h"
#include "game/coop/NetCoopLink.h"
#include "game/coop/NetSocket.h"
#include "game/online/LevelFingerprint.h"
#include "game/cosmetics/SuitRecolor.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/gameplay/InputFrame.h"
#include "game/online/Replay.h"
#include "packaging/PackageImporter.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/CoopGameScene.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

/// The packages downloaded from Cosmic Online (NIVEAUX EN LIGNE, this lobby).
fs::path downloadsDir() { return Paths::userDir() / "online" / "downloads"; }

/// A Cosmic Online id as the relay gives it (never trusted: it goes into a path).
bool onlineId(const std::string& s) {
    if (s.size() != 36) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-')) return false;
    return true;
}

struct Rule {
    const char* title;
    const char* text;
    Color color;
};

const Rule kRules[] = {
    {"Pas de coups entre alliés", "Attaques et pouvoirs ne touchent que les ennemis, jamais un coéquipier.", {62, 230, 214, 255}},
    {"Joueurs solides, sans poussée",
     "On ne se traverse pas et on ne se pousse pas : un allié arrête comme un mur, sans bouger. On peut se poser sur sa tête ; s'il s'en va, on tombe.",
     {62, 230, 214, 255}},
    {"Réanimation", "Un joueur à terre laisse son âme : un allié reste 2 s près d'elle pour le relever. 2 réanimations par équipe et par tentative.",
     {255, 210, 122, 255}},
    {"Tous à terre en même temps", "La tentative s'arrête, comme en solo ; elle compte dans la course à la vérification.", {255, 143, 163, 255}},
    {"Déconnexion",
     "Le joueur passe en veille (intouchable, ne gêne personne) ; 60 s pour revenir près de l'équipe. Sinon l'équipe continue, classée « incomplète ».",
     {201, 179, 255, 255}},
    {"Caméra commune", "Elle cadre toute l'équipe ; un joueur hors de l'écran depuis 3 s revient près de l'allié le plus proche.", {201, 211, 238, 255}},
};

class CoopLobbyScene : public Scene {
public:
    CoopLobbyScene(GameServices& g, std::optional<CoopSetup> previous, std::string wanted)
        : g_(g), previous_(std::move(previous)), wantedLevel_(std::move(wanted)) {}

    ~CoopLobbyScene() override { g_.app.input().setReservedPads({}); }

    void onEnter() override {
        startLevelSearch();
        p1Pad_ = g_.app.input().gamepadId();
        seats_.clear();
        CoopSeat me;
        me.name = g_.online.loggedIn() ? g_.online.username() : "Joueur 1";
        me.accountId = g_.online.loggedIn() ? g_.online.playerId() : "";
        me.device = CoopSeat::Device::Keyboard;
        me.color = coopColor(0);
        seats_.push_back(me);
        if (previous_ && previous_->mode == CoopSetup::Mode::SamePc) {
            // The team of the game just played, when its gamepads are still there.
            const auto pads = connectedGamepads();
            for (size_t i = 1; i < previous_->seats.size(); ++i) {
                const CoopSeat& s = previous_->seats[i];
                if (s.device == CoopSeat::Device::Pad &&
                    std::find(pads.begin(), pads.end(), static_cast<SDL_JoystickID>(s.pad)) != pads.end())
                    addPad(static_cast<SDL_JoystickID>(s.pad));
            }
            wantedLevel_ = previous_->levelId;
        }
        if (previous_ && previous_->mode != CoopSetup::Mode::SamePc) {
            // A network game just left: the same mode, and the way back into it (60 s).
            mode_ = previous_->mode == CoopSetup::Mode::Lan ? 1 : 2;
            wantedLevel_ = previous_->levelId;
        }
        nova_ = g_.app.resources().animation("animations/nova.json");
        anim_.setSet(nova_);
        anim_.play("idle", true);
        g_.ui.setFocus("coop/play");
    }

    void onExit() override { g_.app.input().setReservedPads({}); }

    void update(float dt) override {
        t_ += dt;
        anim_.update(dt);
        if (levelsFuture_.valid() && levelsFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            // The level chosen stays chosen when the list is read again (a level installed).
            const std::string keep = levelsLoaded_ && chosenLevel() ? chosenLevel()->id : wantedLevel_;
            levels_ = levelsFuture_.get();
            levelsLoaded_ = true;
            level_ = -1;
            for (size_t i = 0; i < levels_.size(); ++i)
                if (levels_[i].id == keep) level_ = static_cast<int>(i);
            if (level_ >= 0) source_ = levels_[static_cast<size_t>(level_)].community ? 1 : 0;
            else level_ = firstOfSource(source_);
        }
        if (mode_ == 0) watchPads(dt);
        updateNet(dt);
    }

    // ------------------------------------------------------------------ levels

    /// The search of the levels (campaign, community installs), off the main thread.
    void startLevelSearch() {
        levelsFuture_ = std::async(std::launch::async, [&content = g_.content, installs = g_.online.communityInstalls(), dir = downloadsDir()] {
            return findCoopLevels(content, installs, dir);
        });
    }

    /// The first level of a source (0: the campaign, 1: the community), -1 if none.
    int firstOfSource(int source) const {
        for (size_t i = 0; i < levels_.size(); ++i)
            if (levels_[i].community == (source == 1)) return static_cast<int>(i);
        return -1;
    }
    int countOfSource(int source) const {
        return static_cast<int>(std::count_if(levels_.begin(), levels_.end(), [source](const CoopLevel& l) { return l.community == (source == 1); }));
    }

    // ------------------------------------------------------------------ network

    std::string myName() const { return g_.online.loggedIn() ? g_.online.username() : "Joueur"; }

    const CoopLevel* chosenLevel() const {
        return level_ >= 0 && level_ < static_cast<int>(levels_.size()) ? &levels_[static_cast<size_t>(level_)] : nullptr;
    }

    std::string levelHash(const std::string& id) {
        const LevelFingerprint* fp = g_.online.fingerprints().get(g_.content, id);
        return fp ? fp->hash : std::string(64, '0');
    }

    /// What the relay is told of a level: a community level by its online id and version.
    coopnet::RoomLevel roomLevelOf(const CoopLevel& l) {
        coopnet::RoomLevel r;
        r.title = l.name.substr(0, 80);
        if (l.community) {
            r.source = "community";
            r.id = l.communityId;
            r.hash = l.hash;
            r.version = l.version;
        } else {
            r.id = l.id;
            r.hash = levelHash(l.id);
        }
        return r;
    }

    /// The level of the room, as the relay says it (never trusted: bounded, checked here).
    coopnet::RoomLevel lobbyLevel() const {
        coopnet::RoomLevel r;
        const Json& j = link_->lobby();
        if (!j.is_object()) return r;
        r.id = jstr(j, "level").substr(0, 64);
        r.hash = jstr(j, "hash").substr(0, 64);
        r.title = jstr(j, "title").substr(0, 80);
        r.source = jstr(j, "source", "campaign") == "community" ? "community" : "campaign";
        r.version = std::clamp(jget<int>(j, "version", 0), 0, 100000);
        return r;
    }

    void failRoom(const std::string& why) {
        room_ = Room::Failed;
        roomError_ = why;
    }

    /// Every frame in a room: the room's level is on this PC in its version, or it comes.
    void followRoomLevel() {
        if (!link_ || !link_->connected() || !link_->lobby().is_object()) return;
        const coopnet::RoomLevel l = lobbyLevel();
        if (l.id.empty()) return;
        if (!(l == roomLevel_)) {
            roomLevel_ = l;
            ++roomGen_;
            room_ = Room::Checking;
            roomLocal_.clear();
            roomError_.clear();
            if (ready_) {
                ready_ = false;
                link_->setReady(false);
            }
        }
        if (room_ == Room::Checking) resolveRoomLevel();
        if (room_ == Room::Fetching && fromHost_) followHostPackage();
    }

    void resolveRoomLevel() {
        const coopnet::RoomLevel& l = roomLevel_;
        if (!l.community()) {
            if (g_.content.level(l.id) && !g_.content.campaignPlanetOf(l.id).empty() && levelHash(l.id) == l.hash) {
                room_ = Room::Ready;
                roomLocal_ = l.id;
            } else {
                failRoom("Ce niveau de la campagne n'est pas dans la même version sur ce PC : mettez le jeu à jour pour jouer ensemble.");
            }
            return;
        }
        if (!levelsLoaded_ || levelsFuture_.valid() || installFuture_.valid()) return;  // still reading the levels
        if (const CoopLevel* c = findRoomLevel(levels_, l.id, l.version, l.hash)) {
            room_ = Room::Ready;
            roomLocal_ = c->id;
            return;
        }
        if (!onlineId(l.id) || l.version <= 0 || l.hash.size() != 64) return failRoom("Le niveau de ce salon est illisible.");
        if (link_->seat() == 0) return failRoom("Ce niveau n'est plus sur ce PC dans cette version.");
        // Missing here (or another version): the host sends it on the local network, Cosmic
        // Online otherwise.
        if (mode_ == 1) {
            fromHost_ = true;
            room_ = Room::Fetching;
            link_->requestPackage();
        } else {
            downloadRoomLevel();
        }
    }

    void followHostPackage() {
        const coopnet::PackageReceiver& p = link_->package();
        if (p.failed()) return failRoom(p.error());
        if (p.unavailable()) return downloadRoomLevel();  // no package kept on the host's PC
        if (p.complete()) {
            const std::string sha = p.sha256();
            install(link_->takePackage(), sha, true);
        }
    }

    void downloadRoomLevel() {
        fromHost_ = false;
        if (!g_.online.usable()) {
            return failRoom(mode_ == 1 ? "L'hôte n'a pas gardé le fichier de ce niveau et Cosmic Online est injoignable : téléchargez-le dans "
                                         "NIVEAUX EN LIGNE, puis rejoignez la partie."
                                       : "Cosmic Online est injoignable : le niveau du salon ne peut pas être téléchargé.");
        }
        room_ = Room::Fetching;
        received_ = std::make_shared<std::atomic<size_t>>(0);
        const int gen = roomGen_;
        g_.online.getBinary(
            "/api/v1/community/levels/" + roomLevel_.id + "/package?version=" + std::to_string(roomLevel_.version),
            [this, gen, life = std::weak_ptr<int>(life_)](const HttpResponse& r) {
                if (life.expired() || gen != roomGen_) return;
                if (r.status != 200)
                    return failRoom(r.networkFailure() ? "Téléchargement du niveau impossible : " + r.errorMessage
                                                       : "Téléchargement du niveau refusé (" + std::to_string(r.status) + ").");
                install(std::vector<uint8_t>(r.body.begin(), r.body.end()), r.header("x-content-sha256"), false);
            },
            coopnet::kMaxPackage, received_);
    }

    /// Checks (SHA-256), keeps and installs a package off the main thread. `shaRequired`: the
    /// local network always announces it; Cosmic Online gives it in a header.
    void install(std::vector<uint8_t> bytes, std::string sha, bool shaRequired) {
        room_ = Room::Installing;
        installGen_ = roomGen_;
        installFuture_ = std::async(std::launch::async, [&content = g_.content, bytes = std::move(bytes), sha = std::move(sha), shaRequired,
                                                         level = roomLevel_]() -> Installed {
            Installed out;
            out.level = level;
            const std::string& id = level.id;
            const int version = level.version;
            if (bytes.empty() || bytes.size() > coopnet::kMaxPackage) {
                out.error = "Le niveau reçu est vide ou trop volumineux : il n'est pas installé.";
                return out;
            }
            if ((shaRequired || !sha.empty()) && !coopnet::packageMatches(bytes, sha)) {
                out.error = "Le niveau reçu est abîmé (son empreinte SHA-256 ne correspond pas) : il n'est pas installé.";
                return out;
            }
            std::error_code ec;
            fs::create_directories(downloadsDir(), ec);
            out.file = communityPackagePath(downloadsDir(), id, version);
            if (!FileUtil::writeBinary(out.file, bytes)) {
                out.error = "Écriture du niveau impossible sur ce PC.";
                return out;
            }
            // The same verifications as any package (paths, types, sizes, hashes...).
            PackageImporter imp(content);
            PackageInspection insp = imp.inspect(out.file);
            if (!insp.valid) {
                out.error = "Niveau refusé : " + (insp.errors.empty() ? std::string("paquet invalide") : insp.errors.front());
                return out;
            }
            std::string err;
            if (!imp.install(insp, ConflictPolicy::Replace, &out.id, &err)) out.error = "Installation du niveau impossible : " + err;
            return out;
        });
    }

    /// The package installed (or refused): the content is read again on the main thread, once
    /// no search reads it any more; the fingerprint decides whether it is the room's version.
    void finishInstall() {
        Installed res = installFuture_.get();
        const bool current = installGen_ == roomGen_;  // the room did not change its level meanwhile
        std::error_code ec;
        if (!res.error.empty()) {
            if (!res.file.empty()) fs::remove(res.file, ec);
            if (current) failRoom(res.error);
            return;
        }
        g_.content.reload();
        g_.ratings.contentChanged();
        auto lvl = g_.content.loadLevel(res.id);
        const bool same = lvl && fingerprintGameplay(g_.content, *lvl) == res.level.hash;
        g_.online.noteCommunityInstall(res.id, {{"id", res.level.id},
                                                {"version", res.level.version},
                                                {"level_hash", res.level.hash},
                                                {"title", res.level.title},
                                                {"verified", same}});
        if (!same) fs::remove(res.file, ec);
        if (current && !same) failRoom("La copie reçue n'est pas la version de ce salon (empreintes différentes) : la partie ne peut pas commencer.");
        else if (current) room_ = Room::Checking;  // found in the list read again
        startLevelSearch();
    }

    void joinNet(std::unique_ptr<CoopTransport> transport, uint8_t wantedSeat = 255) {
        link_ = std::make_unique<NetCoopLink>(std::move(transport), myName(), wantedSeat, g_.online.loggedIn() ? g_.online.playerId() : std::string());
        if (host_) link_->keepAlive(host_);
        link_->start();
        netError_.clear();
        ready_ = false;
    }

    void leaveNet() {
        link_.reset();
        host_.reset();
        roomId_.clear();
        ready_ = false;
        roomLevel_ = {};
        ++roomGen_;
        room_ = Room::None;
        roomLocal_.clear();
        roomError_.clear();
    }

    void hostLan() {
        const CoopLevel* l = chosenLevel();
        if (!l) return;
        auto host = std::make_shared<LanHost>();
        hosted_ = roomLevelOf(*l);
        if (!host->start(myName(), hosted_, l->package)) {
            netError_ = "Impossible d'héberger : le port 47620 de ce PC est déjà pris.";
            return;
        }
        host_ = host;
        joinNet(makeLanTransport("127.0.0.1", net::kLanPort));
    }

    void createRoom() {
        const CoopLevel* l = chosenLevel();
        if (!l || busy_) return;
        busy_ = true;
        const coopnet::RoomLevel rl = roomLevelOf(*l);
        Json level = {{"source", rl.source}, {"id", rl.id}, {"hash", rl.hash}, {"title", rl.title}};
        if (rl.community()) level["version"] = rl.version;
        Json body = {{"level", level}};
        g_.online.send("POST", "/api/v1/coop/rooms", body, OnlineService::guard(life_, [this](const ApiResult& r) {
            busy_ = false;
            if (!r.ok) {
                netError_ = r.message.empty() ? "Création du salon impossible." : r.message;
                return;
            }
            roomId_ = jstr(r.data.value("room", Json::object()), "id");
            connectRoom(roomId_, 255);
        }));
    }

    void connectRoom(const std::string& room, uint8_t wantedSeat) {
        roomId_ = room;
        busy_ = true;
        g_.online.freshAccess([this, room, wantedSeat, life = std::weak_ptr<int>(life_)](const std::string& token, const std::string& url) {
            if (life.expired()) return;
            busy_ = false;
            if (token.empty() || url.empty()) {
                netError_ = "Connectez-vous à Cosmic Online pour jouer en ligne.";
                return;
            }
            joinNet(makeWebSocketTransport(url + "/api/v1/coop/rooms/" + room + "/socket", token), wantedSeat);
        });
    }

    void invite(const std::string& playerId) {
        if (roomId_.empty()) return;
        g_.online.send("POST", "/api/v1/coop/rooms/" + roomId_ + "/invite", Json{{"player_id", playerId}},
                       OnlineService::guard(life_, [this, playerId](const ApiResult& r) {
                           if (r.ok) invited_.insert(playerId);
                           else netError_ = r.message.empty() ? "Invitation impossible." : r.message;
                       }));
    }

    void updateNet(float dt) {
        if (mode_ == 1) {
            if (!searching_ && !link_) {
                search_.start();
                searching_ = true;
            }
        } else if (searching_) {
            search_.stop();
            searching_ = false;
        }
        if (mode_ == 2 && g_.online.loggedIn() && !link_) {
            invTimer_ -= dt;
            if (invTimer_ <= 0.0f) {
                invTimer_ = 5.0f;
                g_.online.get("/api/v1/coop/invitations", OnlineService::guard(life_, [this](const ApiResult& r) {
                    if (r.ok) invitations_ = r.data.value("invitations", Json::array());
                }), false);
            }
        }
        if (mode_ == 2 && link_ && friends_.is_null() && g_.online.loggedIn()) {
            friends_ = Json::array();
            g_.online.get("/api/v1/friends", OnlineService::guard(life_, [this](const ApiResult& r) {
                if (r.ok) friends_ = r.data.value("friends", Json::array());
            }));
        }
        // A level installed for a room: the content is read again once no search reads it.
        if (installFuture_.valid() && !levelsFuture_.valid() && installFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            finishInstall();
        if (!link_) return;
        link_->update(dt);
        if (!link_->error().empty()) {
            netError_ = link_->error();
            leaveNet();
            return;
        }
        if (!link_->endReason().empty()) {
            netError_ = link_->endReason();
            leaveNet();
            return;
        }
        // The level the host chose follows on the local network.
        if (host_ && chosenLevel()) {
            const coopnet::RoomLevel rl = roomLevelOf(*chosenLevel());
            if (!(rl == hosted_)) {
                hosted_ = rl;
                host_->setLevel(rl, chosenLevel()->package);
            }
        }
        followRoomLevel();
        if (link_->go()) startNetGame();
    }

    void startNetGame() {
        const auto& go = *link_->go();
        if (!levelsLoaded_ || levelsFuture_.valid() || installFuture_.valid()) return;  // the levels are being read: soon
        // The level must be the same version here: the simulation would not be the same. A
        // community level by its online id and its fingerprint (and the room's version).
        std::string local, communityId;
        int version = 0;
        for (const CoopLevel& l : levels_)
            if (l.community && l.communityId == go.level && l.hash == go.hash && (!roomLevel_.community() || l.version == roomLevel_.version)) {
                local = l.id;
                communityId = l.communityId;
                version = l.version;
            }
        if (local.empty() && g_.content.level(go.level) && !g_.content.campaignPlanetOf(go.level).empty() && levelHash(go.level) == go.hash)
            local = go.level;
        if (local.empty()) {
            netError_ = roomLevel_.community() ? "Ce niveau de la communauté n'est pas sur ce PC dans la version du salon : la partie ne peut pas commencer."
                                               : "Ce niveau n'est pas dans la même version sur ce PC : mettez le jeu à jour.";
            leaveNet();
            return;
        }
        CoopSetup s;
        s.mode = mode_ == 1 ? CoopSetup::Mode::Lan : CoopSetup::Mode::Online;
        s.levelId = local;
        s.communityId = communityId;
        s.communityVersion = version;
        s.localSeat = link_->seat();
        s.seed = go.seed;
        s.attemptId = go.attempt;
        s.inputDelay = go.delay;
        s.roomId = roomId_;
        s.address = mode_ == 1 ? (host_ ? std::string("127.0.0.1") : joinAddress_) : std::string();
        const Json seats = link_->lobby().value("seats", Json::array());
        for (int i = 0; i < go.players; ++i) {
            CoopSeat seat;
            seat.name = "Joueur " + std::to_string(i + 1);
            for (const auto& j : seats)
                if (jget<int>(j, "seat", -1) == i) {
                    seat.name = jstr(j, "name", seat.name).substr(0, 24);
                    seat.accountId = jstr(j, "account").substr(0, 64);  // checked by the server: each confirms
                }
            seat.device = i == link_->seat() ? CoopSeat::Device::Keyboard : CoopSeat::Device::Remote;
            if (i == link_->seat()) seat.accountId = g_.online.loggedIn() ? g_.online.playerId() : "";
            seat.color = coopColor(i);
            s.seats.push_back(seat);
        }
        if (searching_) search_.stop();
        searching_ = false;
        host_.reset();  // the link keeps it alive
        g_.app.switchScene(makeCoopGame(g_, std::move(s), std::move(link_)), 0.4f);
    }

    /// Back into the network game just left (within 60 s): the same seat.
    void rejoin() {
        if (!previous_ || previous_->localSeat < 0) return;
        const uint8_t seat = static_cast<uint8_t>(previous_->localSeat);
        if (previous_->mode == CoopSetup::Mode::Online && !previous_->roomId.empty()) connectRoom(previous_->roomId, seat);
        else if (!previous_->address.empty()) {
            joinAddress_ = previous_->address;
            joinNet(makeLanTransport(previous_->address, net::kLanPort), seat);
        }
    }

    /// Every gamepad but player 1's is watched (and kept from the menus): Start or A takes a
    /// place, B leaves it.
    void watchPads(float dt) {
        const auto pads = connectedGamepads();
        // The ones gone: their watcher, their place.
        for (auto it = watchers_.begin(); it != watchers_.end();) {
            if (std::find(pads.begin(), pads.end(), it->first) == pads.end()) {
                removePad(it->first);
                it = watchers_.erase(it);
            } else {
                ++it;
            }
        }
        if (p1Pad_ != 0 && std::find(pads.begin(), pads.end(), p1Pad_) == pads.end()) p1Pad_ = 0;
        if (p1Pad_ != 0) watchers_.erase(p1Pad_);  // player 1's own: it takes no place
        std::vector<SDL_JoystickID> reserved;
        for (SDL_JoystickID id : pads) {
            if (id == p1Pad_) continue;
            reserved.push_back(id);
            auto& w = watchers_[id];
            if (!w) {
                w = std::make_unique<LocalPad>();
                w->open(id);
            }
        }
        if (reserved != reserved_) {
            reserved_ = reserved;
            g_.app.input().setReservedPads(reserved_);
        }
        for (auto& [id, w] : watchers_) {
            if (!w) continue;
            w->update(g_.settings.input, dt);
            const int seat = seatOfPad(id);
            if (seat < 0 && (w->startPressed() || w->confirmPressed())) addPad(id);
            else if (seat > 0 && w->backPressed()) removePad(id);
        }
    }

    int seatOfPad(SDL_JoystickID id) const {
        for (size_t i = 1; i < seats_.size(); ++i)
            if (seats_[i].device == CoopSeat::Device::Pad && seats_[i].pad == id) return static_cast<int>(i);
        return -1;
    }

    void addPad(SDL_JoystickID id) {
        if (seats_.size() >= 3 || seatOfPad(id) >= 0 || id == p1Pad_) return;
        CoopSeat s;
        const int n = static_cast<int>(seats_.size());
        s.name = "Joueur " + std::to_string(n + 1);
        s.device = CoopSeat::Device::Pad;
        s.pad = id;
        s.color = coopColor(n);
        seats_.push_back(s);
    }

    void removePad(SDL_JoystickID id) {
        const int seat = seatOfPad(id);
        if (seat <= 0) return;
        seats_.erase(seats_.begin() + seat);
        for (size_t i = 0; i < seats_.size(); ++i) {
            seats_[i].color = coopColor(static_cast<int>(i));
            if (i > 0) seats_[i].name = "Joueur " + std::to_string(i + 1);
        }
    }

    void start() {
        const CoopLevel* l = chosenLevel();
        if (seats_.size() < 2 || !l) return;
        CoopSetup s;
        s.mode = CoopSetup::Mode::SamePc;
        s.levelId = l->id;
        if (l->community) {
            s.communityId = l->communityId;
            s.communityVersion = l->version;
        }
        s.seats = seats_;
        s.localSeat = -1;
        s.seed = levelSeed(s.levelId);
        s.attemptId = makeUuidV4();
        const int n = static_cast<int>(seats_.size());
        g_.app.input().setReservedPads({});
        watchers_.clear();  // the game opens its own
        g_.app.switchScene(makeCoopGame(g_, std::move(s), std::make_unique<LocalCoopLink>(n)), 0.4f);
    }

    // ---------------------------------------------------------------------- drawing

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        Renderer& r = ui.r();
        const float W = ui.screenW(), H = ui.screenH();
        r.fillRect({0, 0, W, H}, Color{7, 11, 26, 255});
        const float pad = ui.s(28), gap = ui.s(16);
        float y = ui.s(22);
        ui.pushId("coop");
        // Header: title, « NOUVEAU », the line about the mode, « Retour » at the right.
        const float headH = ui.s(48);
        const Rect back{W - pad - ui.s(170), y + (headH - ui.s(44)) * 0.5f, ui.s(170), ui.s(44)};
        if (ui.button("back", back, "Retour") || (ui.backPressed() && !choosingLevel_ && !inviting_)) {
            leaveNet();
            ui.popId();
            g_.app.switchScene(makeMainMenu(g_), 0.3f);
            return;
        }
        const float titleW = ui.text().measure("COOPÉRATION", FontFace::Title, ui.s(34)).x;
        ui.label("COOPÉRATION", {pad, y + headH * 0.5f}, 34, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        float hx = pad + titleW + ui.s(16);
        {
            const std::string sub = "Niveaux EXTRÊME à 2 ou 3 joueurs · une seule vie chacun · classement par équipe";
            const float room = back.x - ui.s(20) - hx;
            if (room > ui.s(60)) {
                const float fs = ui.fitFont(sub, FontFace::Body, 16, room, 0.75f);
                ui.label(ui.elide(sub, FontFace::Body, fs, room), {hx, y + headH * 0.5f}, fs, ui.theme().textDim, Align::Left, FontFace::Body,
                         VAlign::Middle);
            }
        }
        y += headH + ui.s(16);
        const float bodyH = H - y - ui.s(22);
        const float colL = std::min(ui.s(380), W * 0.25f), colR = std::min(ui.s(400), W * 0.26f);
        const Rect left{pad, y, colL, bodyH};
        const Rect right{W - pad - colR, y, colR, bodyH};
        const Rect center{left.right() + gap, y, right.x - gap - (left.right() + gap), bodyH};
        drawLevel(left);
        drawTeam(center);
        drawRules(right);
        if (choosingLevel_) drawLevelList();
        ui.popId();
    }

    /// A small grey heading in capitals; returns the y under it.
    float heading(const std::string& t, float x, float y) {
        UIContext& ui = g_.ui;
        ui.label(t, {x, y}, 14, Color{143, 155, 189, 255}, Align::Left, FontFace::Bold, VAlign::Top);
        return y + ui.text().measure(t, FontFace::Bold, ui.s(14)).y + ui.s(10);
    }

    /// A guest of a network room: the room's level (the host chooses it), and how it comes.
    bool showsRoomLevel() const { return link_ && link_->connected() && link_->seat() > 0 && !roomLevel_.id.empty(); }

    void drawLevel(const Rect& box) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        r.fillRoundedRect(box, ui.s(14), Color{15, 23, 48, 255});
        r.drawRoundedRect(box, ui.s(14), Color{34, 48, 90, 255}, ui.s(1));
        const float in = ui.s(16), x = box.x + in, w = box.w - in * 2.0f;
        float y = heading("NIVEAU", x, box.y + in);
        const bool guest = showsRoomLevel();
        if (!guest) {
            // 2.63.5: the campaign's EXTRÊME levels or the community's installed ones (fixed in a room).
            const Rect tabs{x, y, w, ui.s(40)};
            int src = source_;
            if (ui.tabs("source", tabs, {"Campagne", "Communauté"}, src) && src != source_ && !link_) {
                source_ = src;
                level_ = firstOfSource(source_);
            }
            ui.newBadge("source", tabs, "coop.community");  // « NOUVEAU » (2.63.5)
            y = tabs.bottom() + ui.s(12);
        }
        const CoopLevel* shown = chosenLevel();
        if (guest) {
            shown = nullptr;
            for (const CoopLevel& l : levels_)
                if (l.id == roomLocal_ && room_ == Room::Ready) shown = &l;
        }
        if (!shown) {
            if (guest) {
                drawRoomLevelState(x, y, w);
                return;
            }
            const std::string m = !levelsLoaded_ ? "Recherche des niveaux EXTRÊME…"
                                  : source_ == 0
                                      ? "Aucun niveau EXTRÊME installé."
                                      : "Aucun niveau EXTRÊME de la communauté installé : choisissez-en un dans NIVEAUX EN LIGNE, puis « Jouer en coop ».";
            y += ui.labelWrapped(m, {x, y}, w, 17, ui.theme().textDim).y + ui.s(14);
            if (levelsLoaded_ && source_ == 1 && !link_ && ui.button("online", {x, y, w, ui.s(44)}, "Niveaux en ligne…")) {
                leaveNet();
                g_.app.switchScene(makeOnlineLevels(g_), 0.3f);
            }
            return;
        }
        const CoopLevel& l = *shown;
        // The planet's picture with the EXTRÊME badge.
        const Rect pic{x, y, w, std::min(ui.s(180), box.h * 0.26f)};
        r.fillRoundedRect(pic, ui.s(10), Color{26, 18, 64, 255});
        if (const PlanetData* p = g_.content.planet(l.planetId); p && !p->illustration.empty())
            if (auto tex = g_.app.resources().texture(p->illustration, false)) {
                const float s = std::min(pic.w / static_cast<float>(tex->width()), pic.h / static_cast<float>(tex->height())) * 0.9f;
                const Vec2 sz{tex->width() * s, tex->height() * s};
                r.drawTexture(*tex, nullptr, Rect::fromCenter(pic.center(), sz));
            }
        ui.extremeBadge({pic.x + ui.s(10), pic.y + ui.s(22)}, ui.s(26));
        y = pic.bottom() + ui.s(14);
        {
            std::optional<i18n::Verbatim> theirs;  // a community level's title: its author's words
            if (l.community) theirs.emplace();
            const float nameFs = ui.fitFont(l.name, FontFace::Bold, 22, w, 0.7f);
            ui.label(ui.elide(l.name, FontFace::Bold, nameFs, w), {x, y}, nameFs, ui.theme().text, Align::Left, FontFace::Bold);
            y += ui.text().measure(l.name, FontFace::Bold, ui.s(nameFs)).y + ui.s(8);
        }
        std::string detail = l.community ? "Communauté · par " + (l.author.empty() ? std::string("un joueur") : l.author) + " · version " +
                                               std::to_string(l.version)
                                         : l.planet;
        if (!l.bosses.empty()) {
            detail += detail.empty() ? "" : " · ";
            for (size_t i = 0; i < l.bosses.size(); ++i) detail += (i ? ", " : "") + l.bosses[i];
        }
        detail += "\nMêmes règles qu'en solo : aucune vie de plus.";
        y += ui.labelWrapped(detail, {x, y}, w, 16, Color{201, 211, 238, 255}).y + ui.s(12);
        // How the game is ranked: never on one PC (an account opens only on its own computer,
        // 2.47), on the network when every player has an account (docs/COOP.md § 6 and § 8).
        const std::string rank = mode_ == 0 ? "Sur ce PC, la partie n'est pas classée (un compte ne s'ouvre que sur son propre ordinateur) : pour les classements Duo et Trio, jouez en réseau local ou en ligne."
                                            : "Classée en Duo ou en Trio si chaque joueur est connecté à son compte et confirme la tentative.";
        const Vec2 rs = ui.text().measureWrapped(rank, FontFace::Body, ui.s(15), w - ui.s(24));
        const Rect rb{x, y, w, rs.y + ui.s(20)};
        if (rb.bottom() + ui.s(56) < box.bottom()) {
            r.fillRoundedRect(rb, ui.s(10), Color{28, 20, 54, 255});
            r.drawRoundedRect(rb, ui.s(10), Color{166, 107, 255, 255}, ui.s(1));
            ui.labelWrapped(rank, {x + ui.s(12), y + ui.s(10)}, w - ui.s(24), 15, Color{201, 179, 255, 255});
            y = rb.bottom() + ui.s(12);
        }
        if (guest) return;  // the host chooses
        UIContext::ButtonStyle bs;
        // In an online room, the level is the room's; on the local network it follows the host.
        bs.enabled = countOfSource(source_) > 1 && !(link_ && mode_ == 2);
        if (ui.button("level", {x, box.bottom() - in - ui.s(44), w, ui.s(44)}, "Changer de niveau…", bs)) {
            choosingLevel_ = true;
            ui.setFocus("coop/levels/" + std::to_string(level_));
        }
    }

    /// The room's level not (yet) here: its title, how it comes (with its progress), or why not.
    void drawRoomLevelState(float x, float y, float w) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        {
            i18n::Verbatim theirs;  // the title the host's level has
            const std::string t = roomLevel_.title.empty() ? std::string("Niveau du salon") : roomLevel_.title;
            const float fs = ui.fitFont(t, FontFace::Bold, 22, w, 0.7f);
            ui.label(ui.elide(t, FontFace::Bold, fs, w), {x, y}, fs, ui.theme().text, Align::Left, FontFace::Bold);
            y += ui.text().measure(t, FontFace::Bold, ui.s(fs)).y + ui.s(8);
        }
        if (roomLevel_.community()) {
            y += ui.labelWrapped("Communauté · version " + std::to_string(roomLevel_.version), {x, y}, w, 16, Color{201, 211, 238, 255}).y + ui.s(12);
        }
        std::string what;
        Color c = Color{201, 211, 238, 255};
        float progress = -1.0f;
        switch (room_) {
            case Room::Checking: what = "Recherche du niveau sur ce PC…"; break;
            case Room::Fetching: {
                const uint32_t got = fromHost_ ? link_->package().received() : static_cast<uint32_t>(received_ ? received_->load() : 0);
                uint32_t total = fromHost_ ? link_->package().total() : static_cast<uint32_t>(std::max(0, jget<int>(link_->lobby(), "size", 0)));
                what = fromHost_ ? "Ce PC n'a pas ce niveau : l'hôte l'envoie par le réseau local."
                                 : "Ce PC n'a pas ce niveau dans cette version : téléchargement depuis Cosmic Online.";
                if (total > 0) progress = std::min(1.0f, static_cast<float>(got) / static_cast<float>(total));
                char mb[48];
                std::snprintf(mb, sizeof mb, "%.1f", static_cast<double>(got) / (1024.0 * 1024.0));
                std::string t = mb;
                for (char& ch : t)
                    if (ch == '.') ch = ',';
                what += "\n" + (progress >= 0.0f ? std::to_string(static_cast<int>(progress * 100.0f)) + " % · " : std::string()) + t + " Mo reçus";
                break;
            }
            case Room::Installing: what = "Vérification de l'empreinte et installation du niveau…"; break;
            case Room::Failed:
                what = roomError_;
                c = ui.theme().danger;
                break;
            default: break;
        }
        if (!what.empty()) y += ui.labelWrapped(what, {x, y}, w, 15, c).y + ui.s(12);
        if (room_ == Room::Fetching || room_ == Room::Installing) {
            // The bar: what came of the package (a moving part while its size is unknown).
            const Rect bar{x, y, w, ui.s(12)};
            r.fillRoundedRect(bar, ui.s(6), Color{26, 36, 70, 255});
            if (room_ == Room::Installing) progress = 1.0f;
            if (progress >= 0.0f) {
                r.fillRoundedRect({bar.x, bar.y, std::max(bar.h, bar.w * progress), bar.h}, ui.s(6), ui.theme().accent);
            } else {
                const float k = std::fmod(t_ * 0.6f, 1.0f);
                r.fillRoundedRect({bar.x + (bar.w - bar.w * 0.25f) * k, bar.y, bar.w * 0.25f, bar.h}, ui.s(6), ui.theme().accent);
            }
        }
    }

    void drawTeam(const Rect& full) {
        UIContext& ui = g_.ui;
        // Where the team plays: this PC, the local network, online.
        const Rect tabs{full.x, full.y, full.w, ui.s(44)};
        int m = mode_;
        if (ui.tabs("mode", tabs, {"Sur ce PC", "Réseau local", "En ligne"}, m) && m != mode_ && !link_) {
            mode_ = m;
            netError_.clear();
        }
        const Rect box{full.x, tabs.bottom() + ui.s(14), full.w, full.bottom() - tabs.bottom() - ui.s(14)};
        if (mode_ != 0) {
            drawNet(box);
            return;
        }
        drawLocalTeam(box);
    }

    void drawLocalTeam(const Rect& box) {
        UIContext& ui = g_.ui;
        float y = heading("ÉQUIPE", box.x, box.y);
        {
            const int pads = static_cast<int>(connectedGamepads().size());
            const std::string t = "Sur ce PC · " + std::to_string(pads) + (pads > 1 ? " manettes branchées" : " manette branchée");
            ui.label(ui.elide(t, FontFace::Body, 15, box.w * 0.6f), {box.right(), box.y}, 15, ui.theme().accent, Align::Right, FontFace::Body);
        }
        // The bottom row: the ranking line and « Jouer ».
        const float rowH = ui.s(52);
        const float rowY = box.bottom() - rowH;
        const std::string play = seats_.size() >= 3 ? "Jouer à 3" : "Jouer à 2";
        const float playW = std::max(ui.s(200), ui.text().measure(play, FontFace::Bold, ui.s(20)).x + ui.s(60));
        UIContext::ButtonStyle ps;
        ps.primary = true;
        ps.enabled = seats_.size() >= 2 && !levels_.empty();
        const Rect playRect{box.right() - playW, rowY, playW, rowH};
        if (ui.button("play", playRect, seats_.size() < 2 ? "2 joueurs au moins" : play, ps)) start();
        ui.newBadge("play", playRect, "coop");  // 2.62: « NOUVEAU » until the button is reached
        {
            const std::string t = "À deux, l'équipe est classée en Duo ; à trois, en Trio. Les classements ne se mélangent jamais.";
            const float room = box.w - playW - ui.s(20);
            const Vec2 sz = ui.text().measureWrapped(t, FontFace::Body, ui.s(15), room);
            ui.labelWrapped(t, {box.x, rowY + std::max(0.0f, (rowH - sz.y) * 0.5f)}, room, 15, Color{143, 155, 189, 255});
        }
        // Three places side by side.
        const float cardsH = rowY - ui.s(12) - y;
        const float cw = (box.w - ui.s(24)) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            const Rect c{box.x + static_cast<float>(i) * (cw + ui.s(12)), y, cw, cardsH};
            drawSeat(i, c);
        }
    }

    void drawSeat(int i, const Rect& c) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const bool filled = i < static_cast<int>(seats_.size());
        const Color border = filled ? seats_[static_cast<size_t>(i)].color : Color{34, 48, 90, 255};
        r.fillRoundedRect(c, ui.s(14), Color{15, 23, 48, 255});
        r.drawRoundedRect(c, ui.s(14), border, ui.s(2));
        const float in = ui.s(14), w = c.w - in * 2.0f;
        float y = c.y + in;
        ui.label("JOUEUR " + std::to_string(i + 1), {c.x + in, y}, 13, Color{143, 155, 189, 255}, Align::Left, FontFace::Bold);
        y += ui.s(26);
        if (!filled) {
            // A free place: how to take it (a gamepad of this PC).
            const Rect dash{c.x + in, y, w, c.bottom() - in - y};
            r.drawRoundedRect(dash, ui.s(10), Color{44, 60, 106, 255}, ui.s(2));
            std::string t = "Place libre\n\nAppuyez sur Start ou A d'une autre manette pour prendre cette place.";
            if (connectedGamepads().size() <= (p1Pad_ != 0 ? 1u : 0u) + (seats_.size() - 1)) t = "Place libre\n\nBranchez une manette de plus pour un joueur de plus.";
            const Vec2 sz = ui.text().measureWrapped(t, FontFace::Body, ui.s(16), w - ui.s(24));
            ui.labelWrapped(t, {dash.x + ui.s(12), dash.center().y - sz.y * 0.5f}, w - ui.s(24), 16, Color{201, 211, 238, 255});
            return;
        }
        const CoopSeat& s = seats_[static_cast<size_t>(i)];
        // Nova in its suit (the colour of the player), idle.
        const float spriteH = std::min(ui.s(170), (c.bottom() - y) * 0.42f);
        if (nova_ && nova_->texture) {
            TexturePtr suit = i == 0 ? g_.suitSheet(g_.cosmeticLook("suit")) : g_.suitTextures.get(g_.app.resources(), g_.app.renderer(), suitOf(i));
            const Texture& tex = suit ? *suit : *nova_->texture;
            const Rect src = anim_.sourceRect();
            if (src.h > 0.0f) {
                const float k = spriteH / src.h;
                r.drawTexture(tex, &src, {c.center().x - src.w * k * 0.5f, y, src.w * k, spriteH});
            }
        }
        y += spriteH + ui.s(10);
        const float nameFs = ui.fitFont(s.name, FontFace::Bold, 20, w, 0.7f);
        ui.label(ui.elide(s.name, FontFace::Bold, nameFs, w), {c.center().x, y}, nameFs, ui.theme().text, Align::Center, FontFace::Bold);
        y += ui.text().measure(s.name, FontFace::Bold, ui.s(nameFs)).y + ui.s(6);
        std::string device;
        if (s.device == CoopSeat::Device::Pad) device = "manette : " + gamepadName(static_cast<SDL_JoystickID>(s.pad));
        else device = p1Pad_ != 0 ? "clavier · manette : " + gamepadName(p1Pad_) : "clavier et souris";
        device += s.accountId.empty() ? " · sans compte" : " · compte connecté";
        y += ui.labelWrapped(device, {c.x + in, y}, w, 14, Color{201, 211, 238, 255}).y + ui.s(8);
        // What it can do here: player 1 picks its gamepad; the others leave with B.
        if (i == 0) {
            std::vector<SDL_JoystickID> free;
            for (SDL_JoystickID id : connectedGamepads())
                if (seatOfPad(id) < 0) free.push_back(id);
            UIContext::ButtonStyle bs;
            bs.small = true;
            bs.enabled = !free.empty();
            const Rect b{c.x + in, c.bottom() - in - ui.s(40), w, ui.s(40)};
            if (ui.button("p1pad", b, p1Pad_ ? "Changer de manette" : "Prendre une manette", bs)) {
                // The next free gamepad (or none: the keyboard alone).
                auto it = std::find(free.begin(), free.end(), p1Pad_);
                p1Pad_ = (it == free.end()) ? free.front() : (it + 1 == free.end() ? 0 : *(it + 1));
            }
        } else {
            const std::string t = "B sur sa manette : quitter la place";
            const float fs = ui.fitFont(t, FontFace::Body, 14, w, 0.75f);
            ui.label(ui.elide(t, FontFace::Body, fs, w), {c.center().x, c.bottom() - in - ui.s(20)}, fs, Color{143, 155, 189, 255}, Align::Center,
                     FontFace::Body, VAlign::Middle);
        }
    }

    // ------------------------------------------------------------ network drawing

    /// A card with a title and a wrapped text; returns the y under the text.
    float card(const Rect& c, const std::string& title, const std::string& text) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        r.fillRoundedRect(c, ui.s(14), Color{15, 23, 48, 255});
        r.drawRoundedRect(c, ui.s(14), Color{34, 48, 90, 255}, ui.s(1));
        const float in = ui.s(16), w = c.w - in * 2.0f;
        const float fs = ui.fitFont(title, FontFace::Bold, 20, w, 0.7f);
        ui.label(ui.elide(title, FontFace::Bold, fs, w), {c.x + in, c.y + in}, fs, ui.theme().text, Align::Left, FontFace::Bold);
        float y = c.y + in + ui.text().measure(title, FontFace::Bold, ui.s(fs)).y + ui.s(10);
        if (!text.empty()) y += ui.labelWrapped(text, {c.x + in, y}, w, 15, Color{201, 211, 238, 255}).y + ui.s(12);
        return y;
    }

    void drawNet(const Rect& box) {
        UIContext& ui = g_.ui;
        // The last error, at the bottom, wrapped (never over the rest: the rest stops above it).
        Rect area = box;
        if (!netError_.empty()) {
            const Vec2 sz = ui.text().measureWrapped(netError_, FontFace::Body, ui.s(15), box.w);
            ui.labelWrapped(netError_, {box.x, box.bottom() - sz.y}, box.w, 15, ui.theme().danger);
            area.h -= sz.y + ui.s(12);
        }
        if (link_) drawNetLobby(area);
        else if (mode_ == 1) drawLanChoice(area);
        else drawOnlineChoice(area);
    }

    void drawLanChoice(const Rect& box) {
        UIContext& ui = g_.ui;
        const float gap = ui.s(12);
        const Rect left{box.x, box.y, box.w * 0.42f, box.h};
        const Rect right{left.right() + gap, box.y, box.right() - left.right() - gap, box.h};
        float y = card(left, "Héberger sur ce PC",
                       "Ce PC fait le relais de la partie : les joueurs du réseau local la trouvent dans leur liste. Ni compte ni serveur.");
        const float in = ui.s(16), w = left.w - in * 2.0f;
        UIContext::ButtonStyle ps;
        ps.primary = true;
        ps.enabled = chosenLevel() != nullptr;
        if (ui.button("host", {left.x + in, y, w, ui.s(48)}, "Héberger la partie", ps)) hostLan();
        y += ui.s(60);
        if (previous_ && previous_->mode == CoopSetup::Mode::Lan && previous_->localSeat >= 0 && !previous_->address.empty() &&
            ui.button("rejoin", {left.x + in, y, w, ui.s(48)}, "Revenir dans la partie"))
            rejoin();
        y = card(right, "Parties du réseau local", "");
        const float rw = right.w - in * 2.0f;
        // The address of a PC, typed (the list may not reach a PC behind a router).
        const float fieldY = right.bottom() - in - ui.s(48);
        const float joinW = std::max(ui.s(150), ui.text().measure("Rejoindre", FontFace::Bold, ui.s(20)).x + ui.s(40));
        ui.textField("address", {right.x + in, fieldY, rw - joinW - ui.s(10), ui.s(48)}, joinAddress_, "Adresse d'un PC (ex. 192.168.1.20)", 64);
        UIContext::ButtonStyle js;
        js.enabled = !joinAddress_.empty();
        if (ui.button("joinaddr", {right.right() - in - joinW, fieldY, joinW, ui.s(48)}, "Rejoindre", js))
            joinNet(makeLanTransport(joinAddress_, net::kLanPort));
        const auto games = search_.games();
        const Rect list{right.x + in, y, rw, fieldY - ui.s(12) - y};
        if (games.empty()) {
            ui.labelWrapped("Recherche des parties hébergées sur le réseau local…", {list.x, list.y}, list.w, 15, ui.theme().textDim);
            return;
        }
        const float rowH = ui.s(58), gap2 = ui.s(8);
        ui.beginScroll("lan", list, static_cast<float>(games.size()) * (rowH + gap2));
        float ry = list.y - ui.scrollOffset();
        ui.pushId("lan");
        for (size_t i = 0; i < games.size(); ++i) {
            const LanGame& g = games[i];
            i18n::Verbatim theirs;  // a name chosen by the host: never translated
            const std::string sub = g.title + " · " + std::to_string(g.players) + "/3" + (g.started ? " · en cours" : "");
            if (ui.listItem(std::to_string(i), {list.x, ry, list.w - ui.s(14), rowH}, g.host + " (" + g.address + ")", false, sub)) {
                joinAddress_ = g.address;
                joinNet(makeLanTransport(g.address, g.port));
            }
            ry += rowH + gap2;
        }
        ui.popId();
        ui.endScroll();
    }

    void drawOnlineChoice(const Rect& box) {
        UIContext& ui = g_.ui;
        if (!g_.online.loggedIn()) {
            card(box, "Jouer en ligne", "Connectez-vous à Cosmic Online (menu En ligne) pour créer un salon ou rejoindre celui d'un ami.");
            return;
        }
        const float gap = ui.s(12), in = ui.s(16);
        const Rect left{box.x, box.y, box.w * 0.42f, box.h};
        const Rect right{left.right() + gap, box.y, box.right() - left.right() - gap, box.h};
        float y = card(left, "Créer un salon", "Vous êtes l'hôte : invitez un ou deux amis ; ils rejoignent depuis leur écran de coopération.");
        const float w = left.w - in * 2.0f;
        UIContext::ButtonStyle ps;
        ps.primary = true;
        ps.enabled = chosenLevel() != nullptr && !busy_;
        if (ui.button("create", {left.x + in, y, w, ui.s(48)}, busy_ ? "Connexion…" : "Créer un salon", ps)) createRoom();
        y += ui.s(60);
        if (previous_ && previous_->mode == CoopSetup::Mode::Online && previous_->localSeat >= 0 && !previous_->roomId.empty() &&
            ui.button("rejoin", {left.x + in, y, w, ui.s(48)}, "Revenir dans la partie"))
            rejoin();
        y = card(right, "Invitations reçues", "");
        const Rect list{right.x + in, y, right.w - in * 2.0f, right.bottom() - in - y};
        if (!invitations_.is_array() || invitations_.empty()) {
            ui.labelWrapped("Aucune invitation pour l'instant : un ami vous invite depuis son salon.", {list.x, list.y}, list.w, 15, ui.theme().textDim);
            return;
        }
        const float rowH = ui.s(58), gap2 = ui.s(8);
        ui.beginScroll("invites", list, static_cast<float>(invitations_.size()) * (rowH + gap2));
        float ry = list.y - ui.scrollOffset();
        ui.pushId("invites");
        for (size_t i = 0; i < invitations_.size(); ++i) {
            const Json& v = invitations_[i];
            const std::string host = jstr(v.value("host", Json::object()), "username", "Un ami");
            i18n::Verbatim theirs;
            if (ui.listItem(std::to_string(i), {list.x, ry, list.w - ui.s(14), rowH}, host, false, jstr(v, "title")) && !busy_)
                connectRoom(jstr(v, "room"), 255);
            ry += rowH + gap2;
        }
        ui.popId();
        ui.endScroll();
    }

    void drawNetLobby(const Rect& box) {
        UIContext& ui = g_.ui;
        float y = heading("ÉQUIPE", box.x, box.y);
        const Json& lobby = link_->lobby();
        if (!link_->connected()) {
            ui.label("Connexion au relais…", {box.x, y}, 18, ui.theme().textDim, Align::Left, FontFace::Body);
            return;
        }
        {
            const int rtt = link_->pingMs(link_->seat());
            const std::string t = rtt >= 0 ? "Liaison " + std::to_string(rtt) + " ms" : "Liaison : mesure…";
            ui.label(ui.elide(t, FontFace::Body, 15, box.w * 0.5f), {box.right(), box.y}, 15, ui.theme().accent, Align::Right, FontFace::Body);
        }
        const bool host = link_->seat() == 0;
        const Json seats = lobby.value("seats", Json::array());
        int ready = 0;
        for (const auto& s : seats)
            if (jget<bool>(s, "ready", false)) ++ready;
        // The bottom row: leave, then « Prêt » or « Lancer ».
        const float rowH = ui.s(52), rowY = box.bottom() - rowH;
        const float bw = std::min(ui.s(260), box.w * 0.32f);
        if (ui.button("leave", {box.x, rowY, bw, rowH}, "Quitter le salon")) {
            leaveNet();
            return;
        }
        UIContext::ButtonStyle ps;
        ps.primary = true;
        if (host) {
            ps.enabled = seats.size() >= 2 && ready >= static_cast<int>(seats.size());
            const std::string t = "Lancer · " + std::to_string(ready) + " / " + std::to_string(seats.size()) + " prêts";
            if (ui.button("start", {box.right() - bw, rowY, bw, rowH}, t, ps)) link_->startGame(static_cast<int>(seats.size()));
        } else {
            // 2.63.5: « Prêt » only once the room's level is here, in its version.
            const bool levelHere = room_ == Room::Ready;
            ps.selected = ready_;
            ps.enabled = levelHere;
            std::string label = ready_ ? "Prêt · annuler" : "Je suis prêt";
            if (!levelHere)
                label = room_ == Room::Fetching ? "Niveau en route…" : room_ == Room::Installing ? "Installation…" : room_ == Room::Failed ? "Niveau manquant" : "Vérification…";
            if (ui.button("ready", {box.right() - bw, rowY, bw, rowH}, label, ps) && levelHere) {
                ready_ = !ready_;
                link_->setReady(ready_);
            }
        }
        const float cardsH = rowY - ui.s(12) - y;
        const float cw = (box.w - ui.s(24)) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            const Json* seat = nullptr;
            for (const auto& s : seats)
                if (jget<int>(s, "seat", -1) == i) seat = &s;
            drawNetSeat(i, {box.x + static_cast<float>(i) * (cw + ui.s(12)), y, cw, cardsH}, seat, host);
        }
        if (inviting_) drawInvite();
    }

    void drawNetSeat(int i, const Rect& c, const Json* seat, bool host) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const Color color = coopColor(i);
        r.fillRoundedRect(c, ui.s(14), Color{15, 23, 48, 255});
        r.drawRoundedRect(c, ui.s(14), seat ? color : Color{34, 48, 90, 255}, ui.s(2));
        const float in = ui.s(14), w = c.w - in * 2.0f;
        float y = c.y + in;
        ui.label("JOUEUR " + std::to_string(i + 1), {c.x + in, y}, 13, Color{143, 155, 189, 255}, Align::Left, FontFace::Bold);
        y += ui.s(26);
        if (!seat) {
            if (host && mode_ == 2) {
                UIContext::ButtonStyle bs;
                bs.enabled = !roomId_.empty();
                if (ui.button("invite" + std::to_string(i), {c.x + in, y, w, ui.s(44)}, "Inviter un ami", bs)) inviting_ = true;
                y += ui.s(56);
            }
            const std::string t = mode_ == 1 ? "Place libre : un joueur du réseau local la prend en rejoignant la partie."
                                             : "Place libre : un ami invité la prend en rejoignant le salon.";
            ui.labelWrapped(t, {c.x + in, y}, w, 15, Color{201, 211, 238, 255});
            return;
        }
        const float spriteH = std::min(ui.s(150), (c.bottom() - y) * 0.4f);
        if (nova_ && nova_->texture) {
            TexturePtr suit = i == 0 ? nullptr : g_.suitTextures.get(g_.app.resources(), g_.app.renderer(), suitOf(i));
            const Texture& tex = suit ? *suit : *nova_->texture;
            const Rect src = anim_.sourceRect();
            if (src.h > 0.0f) {
                const float k = spriteH / src.h;
                r.drawTexture(tex, &src, {c.center().x - src.w * k * 0.5f, y, src.w * k, spriteH});
            }
        }
        y += spriteH + ui.s(10);
        std::string name = jstr(*seat, "name", "Joueur").substr(0, 24);
        {
            i18n::Verbatim theirs;  // a player's name
            const float fs = ui.fitFont(name, FontFace::Bold, 20, w, 0.7f);
            ui.label(ui.elide(name, FontFace::Bold, fs, w), {c.center().x, y}, fs, ui.theme().text, Align::Center, FontFace::Bold);
            y += ui.text().measure(name, FontFace::Bold, ui.s(fs)).y + ui.s(6);
        }
        const int ping = jget<int>(*seat, "ping", -1);
        std::string detail = i == link_->seat() ? "vous" : (ping >= 0 ? std::to_string(ping) + " ms" : "liaison : mesure…");
        if (i == 0) detail += " · hôte";
        const float fs = ui.fitFont(detail, FontFace::Body, 15, w, 0.75f);
        ui.label(ui.elide(detail, FontFace::Body, fs, w), {c.center().x, y}, fs, Color{201, 211, 238, 255}, Align::Center, FontFace::Body);
        y += ui.s(28);
        const bool isReady = jget<bool>(*seat, "ready", false);
        const std::string state = isReady ? "Prêt" : "Pas encore prêt";
        const float sw = ui.text().measure(state, FontFace::Bold, ui.s(15)).x + ui.s(24);
        const Rect pill{c.center().x - sw * 0.5f, y, sw, ui.s(28)};
        if (pill.bottom() < c.bottom() - in) {
            r.drawRoundedRect(pill, ui.s(14), isReady ? color : Color{143, 155, 189, 255}, ui.s(1.2f));
            ui.label(state, pill.center(), 15, isReady ? color : Color{143, 155, 189, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
        }
    }

    void drawInvite() {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 10, 200});
        const float rowH = ui.s(58), gap = ui.s(8);
        const size_t n = friends_.is_array() ? friends_.size() : 0;
        const float W = std::min(ui.s(640), ui.screenW() - ui.s(40));
        const float listH = std::min(ui.screenH() - ui.s(240), std::max(rowH, static_cast<float>(n) * (rowH + gap)));
        const Rect box{(ui.screenW() - W) * 0.5f, (ui.screenH() - listH - ui.s(160)) * 0.5f, W, listH + ui.s(160)};
        ui.panel(box, true, 16);
        ui.label("Inviter un ami", {box.center().x, box.y + ui.s(36)}, 28, ui.theme().text, Align::Center, FontFace::Title, VAlign::Middle);
        const Rect view{box.x + ui.s(24), box.y + ui.s(76), box.w - ui.s(48), listH};
        if (n == 0) {
            ui.labelWrapped("Aucun ami à inviter : ajoutez des amis dans SOCIAL.", {view.x, view.y}, view.w, 16, ui.theme().textDim);
        } else {
            ui.beginScroll("friends", view, static_cast<float>(n) * (rowH + gap));
            float y = view.y - ui.scrollOffset();
            ui.pushId("friends");
            for (size_t i = 0; i < n; ++i) {
                const Json& p = friends_[i].value("player", Json::object());
                const std::string id = jstr(p, "id");
                const bool done = invited_.count(id) > 0;
                i18n::Verbatim theirs;
                if (ui.listItem(std::to_string(i), {view.x, y, view.w - ui.s(14), rowH}, jstr(p, "username"), done, done ? "invité" : "") && !done)
                    invite(id);
                y += rowH + gap;
            }
            ui.popId();
            ui.endScroll();
        }
        if (ui.button("closeinvite", {box.center().x - ui.s(120), box.bottom() - ui.s(64), ui.s(240), ui.s(48)}, "Fermer") || ui.backPressed())
            inviting_ = false;
    }

    static SuitColors suitOf(int seat) {
        if (seat == 1) return {{255, 231, 240}, {255, 122, 184}, {255, 210, 122}};
        return {{255, 243, 214}, {255, 210, 122}, {62, 230, 214}};
    }

    void drawRules(const Rect& box) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        r.fillRoundedRect(box, ui.s(14), Color{15, 23, 48, 255});
        r.drawRoundedRect(box, ui.s(14), Color{34, 48, 90, 255}, ui.s(1));
        const float in = ui.s(16), x = box.x + in, w = box.w - in * 2.0f;
        const float top = heading("RÈGLES DE L'ÉQUIPE", x, box.y + in);
        // Measured first: the list scrolls when the column is too short for it.
        float total = 0.0f;
        for (const Rule& rule : kRules)
            total += ui.text().measureWrapped(rule.title, FontFace::Body, ui.s(15), w - ui.s(20)).y + ui.text().measureWrapped(rule.text, FontFace::Body, ui.s(13), w - ui.s(20)).y + ui.s(30);
        const Rect view{box.x + ui.s(4), top, box.w - ui.s(8), box.bottom() - in - top};
        ui.beginScroll("rules", view, total);
        float y = top - ui.scrollOffset();
        for (const Rule& rule : kRules) {
            const float th = ui.text().measureWrapped(rule.title, FontFace::Body, ui.s(15), w - ui.s(20)).y;
            const float bh = ui.text().measureWrapped(rule.text, FontFace::Body, ui.s(13), w - ui.s(20)).y;
            const Rect rb{x, y, w, th + bh + ui.s(22)};
            r.fillRoundedRect(rb, ui.s(8), Color{10, 17, 36, 255});
            ui.labelWrapped(rule.title, {x + ui.s(10), y + ui.s(8)}, w - ui.s(20), 15, rule.color);
            ui.labelWrapped(rule.text, {x + ui.s(10), y + ui.s(12) + th}, w - ui.s(20), 13, Color{201, 211, 238, 255});
            y = rb.bottom() + ui.s(8);
        }
        ui.endScroll();
    }

    void drawLevelList() {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        r.fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 10, 200});
        const float rowH = ui.s(64), gap = ui.s(8);
        const float W = std::min(ui.s(720), ui.screenW() - ui.s(40));
        const int count = countOfSource(source_);
        const float listH = std::min(ui.screenH() - ui.s(220), static_cast<float>(count) * (rowH + gap));
        const Rect box{(ui.screenW() - W) * 0.5f, (ui.screenH() - listH - ui.s(150)) * 0.5f, W, listH + ui.s(150)};
        ui.panel(box, true, 16);
        const std::string title = source_ == 1 ? "Niveaux EXTRÊME de la communauté" : "Choisir un niveau EXTRÊME";
        ui.label(title, {box.center().x, box.y + ui.s(36)}, ui.fitFont(title, FontFace::Title, 28, box.w - ui.s(48)), ui.theme().text, Align::Center,
                 FontFace::Title, VAlign::Middle);
        const Rect view{box.x + ui.s(24), box.y + ui.s(70), box.w - ui.s(48), listH};
        ui.beginScroll("levellist", view, static_cast<float>(count) * (rowH + gap));
        float y = view.y - ui.scrollOffset();
        ui.pushId("levels");
        for (size_t i = 0; i < levels_.size(); ++i) {
            const CoopLevel& l = levels_[i];
            if (l.community != (source_ == 1)) continue;
            const std::string sub = l.community ? "par " + (l.author.empty() ? std::string("un joueur") : l.author) + " · version " + std::to_string(l.version)
                                                : l.planet;
            std::optional<i18n::Verbatim> theirs;  // a community level's title and author
            if (l.community) theirs.emplace();
            if (ui.listItem(std::to_string(i), {view.x, y, view.w - ui.s(14), rowH}, l.name, static_cast<int>(i) == level_, sub)) {
                level_ = static_cast<int>(i);
                choosingLevel_ = false;
                ui.setFocus("coop/level");
            }
            y += rowH + gap;
        }
        ui.popId();
        ui.endScroll();
        if (ui.button("closelist", {box.center().x - ui.s(120), box.bottom() - ui.s(64), ui.s(240), ui.s(48)}, "Fermer") || ui.backPressed()) {
            choosingLevel_ = false;
            ui.setFocus("coop/level");
        }
    }

    GameServices& g_;
    std::optional<CoopSetup> previous_;
    std::future<std::vector<CoopLevel>> levelsFuture_;
    std::vector<CoopLevel> levels_;
    bool levelsLoaded_ = false;
    int level_ = -1;
    int source_ = 0;  // 0: the campaign, 1: the community (2.63.5)
    std::string wantedLevel_;
    std::vector<CoopSeat> seats_;
    std::map<SDL_JoystickID, std::unique_ptr<LocalPad>> watchers_;
    std::vector<SDL_JoystickID> reserved_;
    SDL_JoystickID p1Pad_ = 0;
    AnimationSetPtr nova_;
    Animator anim_;
    bool choosingLevel_ = false;
    float t_ = 0.0f;
    // Network (local network, online).
    int mode_ = 0;  // 0: this PC, 1: the local network, 2: online
    std::unique_ptr<NetCoopLink> link_;
    std::shared_ptr<LanHost> host_;
    LanSearch search_;
    bool searching_ = false;
    std::string joinAddress_, roomId_, netError_;
    coopnet::RoomLevel hosted_;  // the level this PC's relay offers (local network)
    // 2.63.5: the level of the room on this PC: found, coming (host or Cosmic Online), or why not.
    enum class Room { None, Checking, Ready, Fetching, Installing, Failed };
    struct Installed {
        coopnet::RoomLevel level;
        std::string id, error;
        fs::path file;
    };
    Room room_ = Room::None;
    coopnet::RoomLevel roomLevel_;
    std::string roomLocal_, roomError_;
    int roomGen_ = 0, installGen_ = 0;
    bool fromHost_ = false;
    std::shared_ptr<std::atomic<size_t>> received_;
    std::future<Installed> installFuture_;
    Json invitations_, friends_;
    std::set<std::string> invited_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    bool ready_ = false, busy_ = false, inviting_ = false;
    float invTimer_ = 0.0f;
};

}  // namespace

std::unique_ptr<Scene> makeCoopLobby(GameServices& g, std::optional<CoopSetup> previous, const std::string& level) {
    std::string wanted = level;
    if (wanted.empty() && previous) wanted = previous->levelId;
    return std::make_unique<CoopLobbyScene>(g, std::move(previous), std::move(wanted));
}

}  // namespace cosmic
