#include "game/online/ReplayDownloads.h"

#include <algorithm>
#include <chrono>

#include "game/online/OnlineService.h"

namespace cosmic {

ReplayDownloads::~ReplayDownloads() {
    // A worker still checking a replay finishes first (a few milliseconds).
    for (auto& j : jobs_)
        if (j.result.valid()) j.result.wait();
}

ReplayDownloads::State ReplayDownloads::state(const std::string& id) const {
    if (id.empty()) return State::None;
    if (busy_.count(id)) return State::Busy;
    return library_.has(id) ? State::Done : State::None;
}

void ReplayDownloads::fail(const std::string& id, const std::string& why) {
    if (!id.empty()) busy_.erase(id);
    online_.notify(OnlineService::Notification::Kind::Error, "Téléchargement du replay impossible", why.empty() ? std::string("Replay indisponible.") : why);
}

bool ReplayDownloads::start(ReplayDownloadRequest req) {
    const std::string id = req.meta.id;
    if (id.empty() || req.path.empty()) {
        fail({}, "Replay inconnu.");
        return false;
    }
    if (busy_.count(id) || library_.has(id)) return true;
    if (std::string why = library_.roomFor(0); !why.empty()) {
        fail(id, why);
        return false;
    }
    if (!online_.usable()) {
        fail(id, "Cosmic Online est hors de portée : une connexion est nécessaire pour télécharger un replay (pas pour le regarder ensuite).");
        return false;
    }
    busy_.insert(id);
    const size_t maxBytes = std::min<size_t>(req.maxBytes, static_cast<size_t>(ReplayLibrary::kMaxFileBytes));
    online_.getBinary(
        req.path,
        [this, alive = std::weak_ptr<int>(life_), id, title = std::move(req.title), meta = std::move(req.meta)](const HttpResponse& r) {
            if (!alive.lock()) return;
            if (r.networkFailure() || r.status != 200 || r.body.empty()) {
                std::string why = r.networkFailure() ? "Connexion impossible (" + std::string(netErrorLabel(r.error)) + ") : réessayez plus tard."
                                  : r.status == 403  ? std::string("Ce joueur a masqué son profil : replay indisponible.")
                                  : r.status == 404  ? std::string("Ce replay n'est plus sur Cosmic Online.")
                                  : r.status == 200  ? std::string("Replay vide.")
                                                     : parseApiResponse(r).message;
                return fail(id, why);
            }
            // Checked (decoded) and written off the main thread; listed by update().
            auto bytes = std::make_shared<const std::vector<uint8_t>>(r.body.begin(), r.body.end());
            auto error = std::make_shared<std::string>();
            Job job;
            job.id = id;
            job.title = title.empty() ? std::string("Le replay") : title;
            job.error = error;
            job.result = std::async(std::launch::async, [bytes, meta, error, root = library_.root()]() -> std::optional<DownloadedReplay> {
                auto checked = ReplayLibrary::check(*bytes, meta, error.get());
                if (!checked || !ReplayLibrary::writeFile(root, *checked, *bytes, error.get())) return std::nullopt;
                return checked;
            });
            jobs_.push_back(std::move(job));
        },
        maxBytes);
    return true;
}

void ReplayDownloads::update() {
    for (size_t i = 0; i < jobs_.size();) {
        Job& j = jobs_[i];
        if (j.result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++i;
            continue;
        }
        std::optional<DownloadedReplay> meta = j.result.get();
        const std::string id = j.id, title = j.title;
        std::string why = *j.error;
        jobs_.erase(jobs_.begin() + static_cast<std::ptrdiff_t>(i));
        busy_.erase(id);
        if (!meta) {
            fail(id, why);
            continue;
        }
        if (!library_.commit(std::move(*meta), &why)) {
            fail(id, why);
            continue;
        }
        online_.notify(OnlineService::Notification::Kind::Success, "Replay téléchargé",
                       title + " est gardé sur ce PC : à regarder sans connexion dans Collection › Replays.");
    }
}

}  // namespace cosmic
