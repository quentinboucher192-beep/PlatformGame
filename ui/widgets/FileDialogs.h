#pragma once

#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

namespace cosmic {

/// Native open / save dialogs (SDL3). The result arrives asynchronously, possibly on
/// another thread: callers poll `result(id)` each frame.
namespace FileDialogs {
int openFile(SDL_Window* w, const char* filterName, const char* pattern, const std::string& defaultDir);
int saveFile(SDL_Window* w, const char* filterName, const char* pattern, const std::string& defaultPath);
int openFolder(SDL_Window* w, const std::string& defaultDir);
/// nullopt while pending; empty string if cancelled.
std::optional<std::string> result(int id);
}  // namespace FileDialogs

}  // namespace cosmic
