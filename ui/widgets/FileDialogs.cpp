#include "ui/widgets/FileDialogs.h"

#include <map>
#include <mutex>

namespace cosmic::FileDialogs {

namespace {
std::mutex g_mutex;
std::map<int, std::optional<std::string>> g_results;
int g_next = 1;

struct Request {
    int id;
    SDL_DialogFileFilter filter;
    std::string name, pattern;
};

void SDLCALL callback(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* req = static_cast<Request*>(userdata);
    std::string out;
    if (filelist && filelist[0]) out = filelist[0];
    {
        std::lock_guard lock(g_mutex);
        g_results[req->id] = out;
    }
    delete req;
}

Request* make(const char* name, const char* pattern) {
    std::lock_guard lock(g_mutex);
    auto* r = new Request;
    r->id = g_next++;
    r->name = name ? name : "";
    r->pattern = pattern ? pattern : "";
    r->filter = {r->name.c_str(), r->pattern.c_str()};
    g_results[r->id] = std::nullopt;
    return r;
}
}  // namespace

int openFile(SDL_Window* w, const char* filterName, const char* pattern, const std::string& defaultDir) {
    Request* r = make(filterName, pattern);
    int id = r->id;
    SDL_ShowOpenFileDialog(callback, r, w, &r->filter, 1, defaultDir.empty() ? nullptr : defaultDir.c_str(), false);
    return id;
}

int saveFile(SDL_Window* w, const char* filterName, const char* pattern, const std::string& defaultPath) {
    Request* r = make(filterName, pattern);
    int id = r->id;
    SDL_ShowSaveFileDialog(callback, r, w, &r->filter, 1, defaultPath.empty() ? nullptr : defaultPath.c_str());
    return id;
}

int openFolder(SDL_Window* w, const std::string& defaultDir) {
    Request* r = make("", "");
    int id = r->id;
    SDL_ShowOpenFolderDialog(callback, r, w, defaultDir.empty() ? nullptr : defaultDir.c_str(), false);
    return id;
}

std::optional<std::string> result(int id) {
    std::lock_guard lock(g_mutex);
    auto it = g_results.find(id);
    if (it == g_results.end() || !it->second) return std::nullopt;
    std::string v = *it->second;
    g_results.erase(it);
    return v;
}

}  // namespace cosmic::FileDialogs
