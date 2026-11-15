#include "game/online/UpdateInstaller.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_misc.h>

#include <algorithm>
#include <thread>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "game/online/OnlineService.h"
#include "packaging/Sha256.h"

namespace cosmic {

namespace fs = std::filesystem;

void UpdateInstaller::init(OnlineService& online, NewsCenter& news, std::string currentVersion) {
    online_ = &online;
    news_ = &news;
    current_ = std::move(currentVersion);
}

const NewsCenter::Item* UpdateInstaller::offer() const {
    if (!news_) return nullptr;
    const NewsCenter::Item* best = nullptr;
    for (const auto& it : news_->items()) {
        if (it.kind != "update" || it.download.empty() || !versionNewer(it.target, current_)) continue;
        if (!best || versionNewer(it.target, best->target)) best = &it;
    }
    return best;
}

UpdateInstaller::State UpdateInstaller::state(const std::string& version) const {
    auto it = states_.find(version);
    return it == states_.end() ? State::Idle : it->second;
}

std::string UpdateInstaller::error(const std::string& version) const {
    auto it = errors_.find(version);
    return it == errors_.end() ? std::string() : it->second;
}

fs::path UpdateInstaller::file(const std::string& version) const {
    return Paths::userDir() / "telechargements" / ("CosmicPlatformer-Setup-" + version + ".exe");
}

void UpdateInstaller::download(const NewsCenter::Item& item, bool launchWhenReady) {
    if (!online_) return;
    const std::string version = item.target;
    if (launchWhenReady) launchWhenReady_ = version;
    State s = state(version);
    if (s == State::Downloading || s == State::Checking) return;
    if (s == State::Ready) {
        if (launchWhenReady) launchNow_ = version;
        return;
    }
    states_[version] = State::Downloading;
    errors_.erase(version);
    std::string sha = item.sha256;
    fs::path target = file(version);
    online_->getBinary(
        item.download,
        [this, version, sha, target, alive = std::weak_ptr<int>(life_)](const HttpResponse& r) {
            if (alive.expired()) return;
            if (r.status != 200 || r.body.empty()) {
                states_[version] = State::Failed;
                errors_[version] = r.status ? "Échec du téléchargement (erreur " + std::to_string(r.status) + ")."
                                            : std::string("Téléchargement impossible : pas de connexion.");
                return;
            }
            // The SHA-256 of ~40 MB and the write: off the main thread.
            auto check = std::make_shared<Check>();
            checks_[version] = check;
            states_[version] = State::Checking;
            std::thread([check, bytes = std::vector<uint8_t>(r.body.begin(), r.body.end()), sha, target] {
                bool ok = true;
                std::string err;
                if (sha.empty() || Sha256::hex(bytes) != sha) {
                    ok = false;
                    err = "Fichier refusé (empreinte SHA-256 différente).";
                } else {
                    std::error_code ec;
                    fs::create_directories(target.parent_path(), ec);
                    if (!FileUtil::writeBinary(target, bytes)) {
                        ok = false;
                        err = "Écriture impossible dans le dossier de l'utilisateur.";
                    }
                }
                std::lock_guard<std::mutex> lock(check->m);
                check->ok = ok;
                check->error = err;
                check->done = true;
            }).detach();
        },
        256u * 1024u * 1024u);
}

void UpdateInstaller::update() {
    for (auto it = checks_.begin(); it != checks_.end();) {
        std::shared_ptr<Check> c = it->second;
        bool done, ok;
        std::string err;
        {
            std::lock_guard<std::mutex> lock(c->m);
            done = c->done;
            ok = c->ok;
            err = c->error;
        }
        if (!done) {
            ++it;
            continue;
        }
        const std::string version = it->first;
        states_[version] = ok ? State::Ready : State::Failed;
        if (!ok) errors_[version] = err;
        if (ok) Log::info("Installateur {} téléchargé et vérifié (SHA-256)", version);
        if (ok && launchWhenReady_ == version) launchNow_ = version;
        it = checks_.erase(it);
    }
}

bool UpdateInstaller::takeLaunch(std::string* version) {
    if (launchNow_.empty()) return false;
    if (version) *version = launchNow_;
    launchNow_.clear();
    launchWhenReady_.clear();
    return true;
}

bool UpdateInstaller::launch(const std::string& version) {
    if (state(version) != State::Ready) return false;
    errors_.erase(version);
#ifdef _WIN32
    fs::path target = file(version);
#else
    // Not Windows: the installer cannot run here; its folder opens instead.
    fs::path target = file(version).parent_path();
#endif
    std::string path = Paths::toUtf8(target);
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string url = (path.rfind('/', 0) == 0 ? "file://" : "file:///") + path;
    if (!SDL_OpenURL(url.c_str())) {
        Log::warn("Installateur {} : ouverture impossible ({})", version, SDL_GetError());
        errors_[version] = "Lancement impossible : ouvrez-le dans le dossier « telechargements ».";
        return false;
    }
    return true;
}

}  // namespace cosmic
