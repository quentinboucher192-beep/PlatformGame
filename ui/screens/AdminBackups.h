#pragma once
// ADMINISTRATION › Sauvegardes: the copies of all the data kept by the server (one each night,
// and on demand), their download as a file encrypted with a passphrase the admin chooses, and
// the import of such a file, which replaces ALL the data: sent in parts, checked first (nothing
// changes), then applied step by step after the typed confirmation, the server keeping a copy
// of the state just before (docs/online/BACKUP.md). The passphrase never leaves memory.

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/online/OnlineTypes.h"

namespace cosmic {

class GameServices;

class AdminBackups {
public:
    AdminBackups(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole);
    ~AdminBackups();

    /// The tab is shown: the list is read (again with `force`).
    void enter(bool force = false);
    /// Inside the screen's panel.
    void draw(const Rect& inner);
    /// The download dialog (passphrase, then where to save) takes the input.
    bool modal() const { return downloadStep_ == 1; }
    void drawModal();

private:
    void load();
    void createCopy();
    void startDownload();
    void pollDialogs();
    void startImport(const std::string& path);
    void sendPart();
    void check();
    void apply();
    void abandon(const std::string& id);
    void resetImport();
    void drawImport(Rect& area);

    GameServices& g_;
    std::function<void(const std::string&, bool)> say_;
    std::function<bool(const ApiResult&)> lostRole_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);

    Json list_ = Json::array();
    Json openImports_ = Json::array();
    Json maintenance_;
    bool loaded_ = false, busy_ = false, creating_ = false;

    // Download: 1 = passphrase dialog, 2 = choosing the file, 3 = downloading, 4 = writing.
    std::string downloadId_, downloadDate_;
    int downloadStep_ = 0;
    std::string pass1_, pass2_;
    int saveDialog_ = -1;
    std::string savePath_;
    std::future<bool> writing_;

    // Import: 1 = choosing, 2 = fingerprint, 3 = sending, 4 = ready to check, 5 = checked,
    // 6 = applying, 7 = done.
    int importStep_ = 0;
    int openDialog_ = -1;
    std::string importPath_, importName_;
    std::future<std::pair<int64_t, std::string>> hashing_;
    int64_t importSize_ = 0;
    std::string importId_;
    int64_t partSize_ = 0;
    int parts_ = 0, nextPart_ = 0;
    bool sending_ = false;
    std::string importPass_, confirm_;
    Json checked_;
    double progress_ = 0.0;
    std::string stepText_;
    float retryAt_ = -1.0f;
};

}  // namespace cosmic
