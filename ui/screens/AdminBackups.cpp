#include "ui/screens/AdminBackups.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <vector>

#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"
#include "game/GameServices.h"
#include "game/online/OnlineSession.h"
#include "packaging/Sha256.h"
#include "ui/widgets/FileDialogs.h"

namespace cosmic {

namespace {

const char* const kConfirm = "REMPLACER TOUTES LES DONNÉES";

std::string sizeText(double bytes) {
    char b[32];
    if (bytes >= 1024.0 * 1024 * 1024) std::snprintf(b, sizeof b, "%.1f Go", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024.0 * 1024) std::snprintf(b, sizeof b, "%.1f Mo", bytes / (1024.0 * 1024));
    else std::snprintf(b, sizeof b, "%.0f Ko", std::max(1.0, bytes / 1024.0));
    std::string s = b;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

/// "16/11/2026 · 03:17" (local time of this computer).
std::string stamp(const std::string& iso) {
    auto t = parseIsoUtc(iso);
    if (!t) return "—";
    std::time_t tt = std::chrono::system_clock::to_time_t(*t);
    std::tm m{};
#ifdef _WIN32
    localtime_s(&m, &tt);
#else
    localtime_r(&tt, &m);
#endif
    char b[40];
    std::snprintf(b, sizeof b, "%02d/%02d/%04d · %02d:%02d", m.tm_mday, m.tm_mon + 1, m.tm_year + 1900, m.tm_hour, m.tm_min);
    return b;
}

std::string kindText(const std::string& k) {
    if (k == "auto") return "Automatique";
    if (k == "manual") return "Manuelle";
    if (k == "pre_import") return "Avant un import";
    return k;
}

std::string statusText(const std::string& s) {
    if (s == "ready") return "prête";
    if (s == "running") return "en cours";
    if (s == "failed") return "échouée";
    return s;
}

/// The French message of an error answered as JSON by the server (download), else a generic one.
std::string errorOf(const HttpResponse& r) {
    if (r.networkFailure()) return "Serveur injoignable : réessayez plus tard.";
    try {
        Json j = Json::parse(r.body);
        if (j.contains("error") && j["error"].is_object()) return jstr(j["error"], "message", "Téléchargement refusé.");
    } catch (...) {
    }
    return "Téléchargement refusé (" + std::to_string(r.status) + ").";
}

}  // namespace

AdminBackups::AdminBackups(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole)
    : g_(g), say_(std::move(say)), lostRole_(std::move(lostRole)) {}

AdminBackups::~AdminBackups() {
    if (writing_.valid()) writing_.wait();
    if (hashing_.valid()) hashing_.wait();
}

void AdminBackups::enter(bool force) {
    if (!loaded_ || force) load();
}

void AdminBackups::load() {
    busy_ = true;
    g_.online.get("/api/v1/admin/backups", OnlineService::guard(life_, [this](const ApiResult& r) {
                      busy_ = false;
                      if (lostRole_(r)) return;
                      if (!r.ok) return say_(r.message, true);
                      loaded_ = true;
                      list_ = r.data.value("backups", Json::array());
                      openImports_ = r.data.value("imports", Json::array());
                      maintenance_ = r.data.contains("maintenance") ? r.data["maintenance"] : Json();
                  }),
                  false);
}

void AdminBackups::createCopy() {
    creating_ = true;
    say_("Copie en cours : cela peut prendre quelques secondes…", false);
    // A whole copy takes a while on the server: wait up to ten minutes. If the connection gives
    // up first, the copy goes on over there: the list shows it « en cours », then « prête ».
    g_.online.send("POST", "/api/v1/admin/backups", Json::object(), OnlineService::guard(life_, [this](const ApiResult& r) {
                       creating_ = false;
                       if (lostRole_(r)) return;
                       if (r.networkFailure()) {
                           say_("La copie continue sur le serveur : la liste l'affichera « prête » une fois terminée (Actualiser).", false);
                           return load();
                       }
                       if (!r.ok) return say_(r.message, true);
                       say_("Copie terminée.", false);
                       load();
                   }),
                   600000);
}

// ------------------------------------------------------------------ download

void AdminBackups::startDownload() {
    downloadStep_ = 3;
    say_("Téléchargement et chiffrement de la sauvegarde…", false);
    Json body = {{"passphrase", pass1_}};
    pass1_.assign(pass1_.size(), ' ');
    pass1_.clear();
    pass2_.clear();
    std::string path = savePath_;
    g_.online.postForBinary("/api/v1/admin/backups/" + downloadId_ + "/download", body,
                            [this, life = std::weak_ptr<int>(life_), path](const HttpResponse& r) {
                                if (life.expired()) return;
                                if (r.status != 200) {
                                    downloadStep_ = 0;
                                    return say_(errorOf(r), true);
                                }
                                downloadStep_ = 4;
                                auto data = std::make_shared<std::string>(r.body);
                                writing_ = std::async(std::launch::async, [path, data] {
                                    std::ofstream f(Paths::fromUtf8(path), std::ios::binary | std::ios::trunc);
                                    f.write(data->data(), static_cast<std::streamsize>(data->size()));
                                    return static_cast<bool>(f);
                                });
                            },
                            static_cast<size_t>(1) << 30);
}

// ------------------------------------------------------------------ import

void AdminBackups::startImport(const std::string& path) {
    importPath_ = path;
    importName_ = Paths::toUtf8(Paths::fromUtf8(path).filename());
    importStep_ = 2;
    progress_ = 0.0;
    stepText_ = "Calcul de l'empreinte du fichier…";
    hashing_ = std::async(std::launch::async, [path]() -> std::pair<int64_t, std::string> {
        std::ifstream f(Paths::fromUtf8(path), std::ios::binary);
        if (!f) return {-1, {}};
        Sha256 h;
        std::vector<char> buf(1 << 20);
        int64_t total = 0;
        while (f) {
            f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            std::streamsize n = f.gcount();
            if (n <= 0) break;
            h.update(buf.data(), static_cast<size_t>(n));
            total += n;
        }
        auto d = h.finish();
        static const char* hexd = "0123456789abcdef";
        std::string hex;
        for (uint8_t b : d) {
            hex += hexd[b >> 4];
            hex += hexd[b & 15];
        }
        return {total, hex};
    });
}

void AdminBackups::sendPart() {
    if (sending_ || nextPart_ >= parts_) return;
    std::ifstream f(Paths::fromUtf8(importPath_), std::ios::binary);
    int64_t off = static_cast<int64_t>(nextPart_) * partSize_;
    int64_t len = std::min(partSize_, importSize_ - off);
    std::string bytes(static_cast<size_t>(std::max<int64_t>(0, len)), '\0');
    f.seekg(off);
    f.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!f) {
        importStep_ = 0;
        return say_("Lecture du fichier impossible : import interrompu.", true);
    }
    sending_ = true;
    int n = nextPart_;
    g_.online.sendBytes("PUT", "/api/v1/admin/backups/imports/" + importId_ + "/parts/" + std::to_string(n), std::move(bytes),
                        OnlineService::guard(life_, [this, n](const ApiResult& r) {
                            sending_ = false;
                            if (lostRole_(r)) return;
                            if (!r.ok) {
                                stepText_ = r.message;
                                retryAt_ = -1.0f;
                                return say_(r.message, true);
                            }
                            nextPart_ = n + 1;
                            progress_ = parts_ > 0 ? static_cast<double>(nextPart_) / parts_ : 1.0;
                            if (nextPart_ >= parts_) {
                                importStep_ = 4;
                                stepText_ = "Fichier envoyé. Entrez sa phrase secrète puis « Vérifier » : rien n'est encore modifié.";
                            }
                        }),
                        180000);
}

void AdminBackups::check() {
    stepText_ = "Vérification du fichier…";
    g_.online.send("POST", "/api/v1/admin/backups/imports/" + importId_ + "/check", Json{{"passphrase", importPass_}},
                   OnlineService::guard(life_, [this](const ApiResult& r) {
                       if (lostRole_(r)) return;
                       if (!r.ok) {
                           stepText_ = r.message;
                           return say_(r.message, true);
                       }
                       checked_ = r.data;
                       importStep_ = 5;
                       stepText_ = jget<bool>(r.data, "compatible", false)
                                       ? "Sauvegarde lisible et compatible. Pour remplacer toutes les données, tapez la phrase de confirmation."
                                       : "Cette sauvegarde ne peut pas être importée sur ce serveur.";
                   }),
                   600000);
}

void AdminBackups::apply() {
    importStep_ = 6;
    retryAt_ = -1.0f;
    g_.online.send("POST", "/api/v1/admin/backups/imports/" + importId_ + "/apply", Json{{"passphrase", importPass_}, {"confirm", confirm_}},
                   OnlineService::guard(life_, [this](const ApiResult& r) {
                       if (lostRole_(r)) return;
                       if (!r.ok) {
                           if (r.code == "IMPORT_BUSY" || r.networkFailure()) {
                               retryAt_ = g_.ui.time() + 5.0f;  // a step is running: ask again
                               return;
                           }
                           importStep_ = 5;
                           stepText_ = r.message;
                           return say_(r.message, true);
                       }
                       progress_ = jget<double>(r.data, "progress", progress_);
                       stepText_ = jstr(r.data, "message", stepText_);
                       if (jget<bool>(r.data, "done", false)) {
                           importStep_ = 7;
                           importPass_.clear();
                           confirm_.clear();
                           say_(stepText_.empty() ? std::string("Restauration terminée.") : stepText_, false);
                           load();
                       } else {
                           retryAt_ = g_.ui.time() + 0.2f;  // the next step
                       }
                   }),
                   180000);
}

void AdminBackups::abandon(const std::string& id) {
    g_.online.send("DELETE", "/api/v1/admin/backups/imports/" + id, Json(), OnlineService::guard(life_, [this, id](const ApiResult& r) {
                       if (lostRole_(r)) return;
                       if (!r.ok) return say_(r.message, true);
                       if (id == importId_) resetImport();
                       say_("Import abandonné.", false);
                       load();
                   }));
}

void AdminBackups::resetImport() {
    importStep_ = 0;
    importId_.clear();
    importPass_.clear();
    confirm_.clear();
    checked_ = Json();
    progress_ = 0.0;
    nextPart_ = parts_ = 0;
    stepText_.clear();
}

void AdminBackups::pollDialogs() {
    if (saveDialog_ >= 0)
        if (auto r = FileDialogs::result(saveDialog_)) {
            saveDialog_ = -1;
            if (r->empty()) {
                downloadStep_ = 0;
            } else {
                savePath_ = *r;
                if (savePath_.size() < 5 || savePath_.substr(savePath_.size() - 5) != ".cpbk") savePath_ += ".cpbk";
                startDownload();
            }
        }
    if (openDialog_ >= 0)
        if (auto r = FileDialogs::result(openDialog_)) {
            openDialog_ = -1;
            if (r->empty()) importStep_ = 0;
            else startImport(*r);
        }
    if (writing_.valid() && writing_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        bool ok = writing_.get();
        downloadStep_ = 0;
        say_(ok ? "Sauvegarde enregistrée : " + savePath_ + ". Gardez la phrase secrète à part : sans elle, le fichier est illisible."
                : "Impossible d'écrire le fichier " + savePath_ + ".",
             !ok);
    }
    if (hashing_.valid() && hashing_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto [size, sha] = hashing_.get();
        if (size <= 0) {
            importStep_ = 0;
            return say_("Fichier illisible ou vide.", true);
        }
        importSize_ = size;
        importStep_ = 3;
        stepText_ = "Préparation de l'envoi…";
        g_.online.send("POST", "/api/v1/admin/backups/imports", Json{{"size", size}, {"sha256", sha}},
                       OnlineService::guard(life_, [this](const ApiResult& r) {
                           if (lostRole_(r)) return;
                           if (!r.ok) {
                               importStep_ = 0;
                               return say_(r.message, true);
                           }
                           importId_ = jstr(r.data, "import_id");
                           partSize_ = jget<int64_t>(r.data, "part_size", 8 << 20);
                           parts_ = jget<int>(r.data, "parts", static_cast<int>((importSize_ + partSize_ - 1) / partSize_));
                           nextPart_ = 0;
                           stepText_ = "Envoi du fichier…";
                       }));
    }
    if (importStep_ == 3 && !importId_.empty()) sendPart();
    if (importStep_ == 6 && retryAt_ >= 0.0f && g_.ui.time() >= retryAt_) apply();
}

// ------------------------------------------------------------------ drawing

void AdminBackups::draw(const Rect& in) {
    pollDialogs();
    UIContext& ui = g_.ui;
    ui.pushId("backups");
    float y = in.y, bh = ui.s(46);
    UIContext::ButtonStyle st;
    st.primary = true;
    st.enabled = !creating_ && downloadStep_ == 0;
    float w1 = ui.text().measure("Faire une copie maintenant", FontFace::Bold, ui.s(20)).x + ui.s(48);
    if (ui.button("create", {in.x, y, w1, bh}, creating_ ? "Copie en cours…" : "Faire une copie maintenant", st)) createCopy();
    UIContext::ButtonStyle st2;
    st2.enabled = importStep_ == 0 || importStep_ == 7;
    float w2 = ui.text().measure("Importer une sauvegarde…", FontFace::Bold, ui.s(20)).x + ui.s(48);
    if (ui.button("import", {in.x + w1 + ui.s(12), y, w2, bh}, "Importer une sauvegarde…", st2)) {
        resetImport();
        importStep_ = 1;
        openDialog_ = FileDialogs::openFile(g_.app.window(), "Sauvegarde Cosmic Online", "cpbk", Paths::toUtf8(Paths::userDir()));
    }
    float w3 = ui.text().measure("Actualiser", FontFace::Bold, ui.s(20)).x + ui.s(48);
    UIContext::ButtonStyle st3;
    st3.enabled = !busy_;
    if (ui.button("refresh", {in.x + w1 + w2 + ui.s(24), y, w3, bh}, "Actualiser", st3)) load();
    {
        std::string info = "Une copie automatique chaque nuit · 14 gardées · manuelles 90 jours";
        float room = in.right() - (in.x + w1 + w2 + w3 + ui.s(48));
        float fs = ui.fitFont(info, FontFace::Body, 15, room);
        ui.label(ui.elide(info, FontFace::Body, fs, room), {in.right(), y + bh * 0.5f}, fs, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
    }
    y += bh + ui.s(12);
    // A copy running (this one or another admin's, or the night's): its real progress, read
    // from the server every 2 seconds.
    {
        const Json* running = nullptr;
        for (const auto& b : list_)
            if (jstr(b, "status") == "running") {
                running = &b;
                break;
            }
        if ((running || creating_) && !busy_ && ui.time() - lastPoll_ > 2.0f) {
            lastPoll_ = ui.time();
            load();
        }
        if (running || creating_) {
            double p = running ? std::clamp(jget<double>(*running, "progress", 0.0), 0.0, 1.0) : 0.0;
            std::string step = running ? jstr(*running, "step") : std::string();
            Rect box{in.x, y, in.w, ui.s(58)};
            ui.r().fillRoundedRect(box, ui.s(8), Color{18, 30, 56, 240});
            std::string head = "Copie en cours : " + std::to_string(static_cast<int>(p * 100.0 + 0.5)) + " %";
            if (!step.empty()) head += " · " + step;
            float room = box.w - ui.s(24);
            float fs2 = ui.fitFont(head, FontFace::Bold, 16, room);
            ui.label(ui.elide(head, FontFace::Bold, fs2, room), {box.x + ui.s(12), box.y + ui.s(17)}, fs2, ui.theme().text, Align::Left, FontFace::Bold,
                     VAlign::Middle);
            Rect bar{box.x + ui.s(12), box.y + ui.s(34), box.w - ui.s(24), ui.s(12)};
            ui.r().fillRoundedRect(bar, ui.s(6), Color{36, 48, 77, 255});
            if (p > 0.0) ui.r().fillRoundedRect({bar.x, bar.y, bar.w * static_cast<float>(p), bar.h}, ui.s(6), ui.theme().accent);
            y += box.h + ui.s(10);
        }
    }
    if (maintenance_.is_object()) {
        Rect m{in.x, y, in.w, ui.s(36)};
        ui.r().fillRoundedRect(m, ui.s(8), ui.theme().warm.withAlphaF(0.15f));
        std::string t = "Maintenance en cours : les joueurs ne peuvent rien modifier pendant l'import.";
        ui.label(ui.elide(t, FontFace::Bold, 16, m.w - ui.s(24)), {m.x + ui.s(12), m.center().y}, 16, ui.theme().warm, Align::Left, FontFace::Bold, VAlign::Middle);
        y += m.h + ui.s(10);
    }
    Rect area{in.x, y, in.w, in.bottom() - y};
    if (importStep_ > 0) drawImport(area);

    // Open imports left on the server (another session, a closed game): abandon them.
    for (const auto& im : openImports_) {
        std::string id = jstr(im, "id", jstr(im, "import_id"));
        if (id.empty() || id == importId_) continue;
        Rect row{area.x, area.y, area.w, ui.s(40)};
        ui.r().fillRoundedRect(row, ui.s(8), Color{30, 26, 22, 235});
        std::string t = "Import laissé en attente sur le serveur (" + jstr(im, "status", "?") + ", " + stamp(jstr(im, "created_at")) + ")";
        float bw = ui.s(150);
        ui.label(ui.elide(t, FontFace::Body, 15, row.w - bw - ui.s(36)), {row.x + ui.s(12), row.center().y}, 15, ui.theme().text, Align::Left, FontFace::Body,
                 VAlign::Middle);
        UIContext::ButtonStyle sm;
        sm.small = true;
        if (ui.button("abandon" + id, {row.right() - bw - ui.s(6), row.y + ui.s(4), bw, row.h - ui.s(8)}, "Abandonner", sm)) abandon(id);
        area.y += row.h + ui.s(8);
        area.h -= row.h + ui.s(8);
    }

    // The copies kept by the server.
    static const char* heads[] = {"Date", "Type", "Par", "Lignes", "Fichiers", "Taille", "État", ""};
    static const float share[] = {0.19f, 0.14f, 0.15f, 0.10f, 0.09f, 0.10f, 0.09f, 0.14f};
    float cx[9];
    cx[0] = area.x;
    for (int i = 0; i < 8; ++i) cx[i + 1] = cx[i] + area.w * share[i];
    for (int i = 0; i < 7; ++i)
        ui.label(ui.elide(heads[i], FontFace::Bold, 14, cx[i + 1] - cx[i] - ui.s(14)), {cx[i] + ui.s(12), area.y + ui.s(13)}, 14, ui.theme().textDim,
                 Align::Left, FontFace::Bold, VAlign::Middle);
    Rect listR{area.x, area.y + ui.s(30), area.w, area.bottom() - area.y - ui.s(30)};
    int n = list_.is_array() ? static_cast<int>(list_.size()) : 0;
    float rowH = ui.s(48), gap = ui.s(6);
    if (n == 0)
        ui.label(loaded_ ? "Aucune copie pour l'instant." : "Chargement…", {listR.center().x, listR.y + ui.s(30)}, 17, ui.theme().textDim, Align::Center,
                 FontFace::Body, VAlign::Middle);
    ui.beginScroll("list", listR, n * (rowH + gap));
    for (int i = 0; i < n; ++i) {
        const Json& b = list_[i];
        Rect row{listR.x, listR.y + i * (rowH + gap) - ui.scrollOffset(), listR.w - ui.s(16), rowH};
        ui.r().fillRoundedRect(row, ui.s(8), Color{18, 26, 50, 235});
        auto cell = [&](int c, const std::string& t, Color col) {
            float room = cx[c + 1] - cx[c] - ui.s(16);
            ui.label(ui.elide(t, FontFace::Body, 16, room), {cx[c] + ui.s(12), row.center().y}, 16, col, Align::Left, FontFace::Body, VAlign::Middle);
        };
        std::string status = jstr(b, "status");
        cell(0, stamp(jstr(b, "created_at")), ui.theme().text);
        cell(1, kindText(jstr(b, "kind")), ui.theme().text);
        const Json& by = b.contains("by") ? b["by"] : Json();
        cell(2, by.is_object() ? jstr(by, "username") : "Serveur", by.is_object() ? ui.theme().text : ui.theme().textDim);
        cell(3, std::to_string(jget<int64_t>(b, "rows", 0)), ui.theme().text);
        cell(4, std::to_string(jget<int64_t>(b, "files", 0)), ui.theme().text);
        cell(5, sizeText(static_cast<double>(jget<int64_t>(b, "bytes", 0))), ui.theme().text);
        cell(6, statusText(status), status == "failed" ? ui.theme().danger : status == "ready" ? ui.theme().success : ui.theme().warm);
        UIContext::ButtonStyle sm;
        sm.small = true;
        sm.enabled = status == "ready" && downloadStep_ == 0;
        Rect btn{cx[7] + ui.s(6), row.y + ui.s(6), std::min(cx[8] - cx[7], row.right() - cx[7]) - ui.s(12), row.h - ui.s(12)};
        if (ui.button("dl" + jstr(b, "id"), btn, "Télécharger…", sm)) {
            downloadId_ = jstr(b, "id");
            downloadDate_ = jstr(b, "created_at").substr(0, 10);
            pass1_.clear();
            pass2_.clear();
            downloadStep_ = 1;
            ui.setFocus("backups-dl/p1");
        }
    }
    ui.endScroll();
    ui.popId();
}

void AdminBackups::drawImport(Rect& area) {
    UIContext& ui = g_.ui;
    float h = ui.s(importStep_ >= 4 ? 236 : 110);
    Rect box{area.x, area.y, area.w, h};
    ui.r().fillRoundedRect(box, ui.s(10), Color{24, 20, 40, 240});
    ui.r().drawRoundedRect(box, ui.s(10), ui.theme().warm.withAlphaF(0.7f), ui.s(1.2f));
    float x = box.x + ui.s(18), w = box.w - ui.s(36), y = box.y + ui.s(14);
    std::string title = "Import : " + (importName_.empty() ? std::string("choix du fichier…") : importName_);
    ui.label(ui.elide(title, FontFace::Bold, 18, w), {x, y + ui.s(10)}, 18, ui.theme().warm, Align::Left, FontFace::Bold, VAlign::Middle);
    y += ui.s(30);
    // Progress (sending, then applying).
    if (importStep_ == 3 || importStep_ == 6 || importStep_ == 7) {
        Rect bar{x, y, w, ui.s(10)};
        ui.r().fillRoundedRect(bar, ui.s(5), Color{36, 48, 77, 255});
        ui.r().fillRoundedRect({bar.x, bar.y, bar.w * static_cast<float>(std::clamp(progress_, 0.0, 1.0)), bar.h}, ui.s(5), ui.theme().accent);
        y += ui.s(18);
    }
    float fs = ui.fitFont(stepText_, FontFace::Body, 16, w);
    ui.label(ui.elide(stepText_, FontFace::Body, fs, w), {x, y + ui.s(10)}, fs, ui.theme().text, Align::Left, FontFace::Body, VAlign::Middle);
    y += ui.s(28);
    UIContext::ButtonStyle sm;
    sm.small = true;
    float bh = ui.s(40);
    if (importStep_ >= 4 && importStep_ <= 6) {
        // Passphrase and check.
        float fw = std::min(ui.s(420), w * 0.5f);
        ui.passwordField("ipass", {x, y, fw, bh}, importPass_, "Phrase secrète du fichier");
        sm.enabled = importStep_ != 6 && importPass_.size() >= 12;
        if (ui.button("check", {x + fw + ui.s(10), y, ui.s(170), bh}, "Vérifier", sm)) check();
        sm.enabled = importStep_ != 6;
        if (ui.button("abandon", {x + fw + ui.s(190), y, ui.s(170), bh}, "Abandonner", sm)) abandon(importId_);
        y += bh + ui.s(10);
        if (checked_.is_object()) {
            const Json& b = checked_.value("backup", Json::object());
            std::string sum = "Copie du " + stamp(jstr(b, "created_at")) + " · " + std::to_string(jget<int64_t>(b, "rows", 0)) + " lignes · " +
                              std::to_string(jget<int64_t>(b, "files", 0)) + " fichiers · " + sizeText(static_cast<double>(jget<int64_t>(b, "bytes", 0)));
            ui.label(ui.elide(sum, FontFace::Body, 15, w), {x, y + ui.s(9)}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            y += ui.s(22);
            for (const auto& p : checked_.value("problems", Json::array())) {
                if (!p.is_string() || y > box.bottom() - bh - ui.s(16)) break;
                ui.label(ui.elide(p.get<std::string>(), FontFace::Body, 14, w), {x, y + ui.s(8)}, 14, ui.theme().warm, Align::Left, FontFace::Body,
                         VAlign::Middle);
                y += ui.s(20);
            }
            if (jget<bool>(checked_, "compatible", false)) {
                float cy = box.bottom() - bh - ui.s(14);
                float fw2 = std::min(ui.s(460), w * 0.55f);
                ui.textField("confirm", {x, cy, fw2, bh}, confirm_, std::string("Tapez : ") + kConfirm, 40);
                UIContext::ButtonStyle d;
                d.danger = true;
                d.small = true;
                d.enabled = importStep_ != 6 && confirm_ == kConfirm && importPass_.size() >= 12;
                if (ui.button("apply", {x + fw2 + ui.s(10), cy, ui.s(320), bh}, importStep_ == 6 ? "Remplacement en cours…" : "Remplacer toutes les données", d)) {
                    progress_ = 0.0;
                    apply();
                }
            }
        }
    } else if (importStep_ == 7) {
        if (ui.button("close", {x, y, ui.s(170), bh}, "Fermer", sm)) resetImport();
    }
    area.y += h + ui.s(10);
    area.h -= h + ui.s(10);
}

void AdminBackups::drawModal() {
    UIContext& ui = g_.ui;
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 185});
    Rect box{ui.screenW() * 0.5f - ui.s(360), ui.screenH() * 0.5f - ui.s(190), ui.s(720), ui.s(380)};
    ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
    ui.r().drawRoundedRect(box, ui.s(18), ui.theme().accent.withAlphaF(0.7f), ui.s(1.5f));
    ui.pushId("backups-dl");
    float x = box.x + ui.s(30), w = box.w - ui.s(60), y = box.y + ui.s(26);
    std::string title = "Télécharger la copie du " + stamp(downloadDate_ + "T12:00:00Z").substr(0, 10);
    ui.label(ui.elide(title, FontFace::Title, 24, w), {x, y + ui.s(14)}, 24, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
    y += ui.s(44);
    Vec2 used = ui.labelWrapped("Le fichier contient les données de tous les joueurs. Il est chiffré avec la phrase secrète choisie ici (12 caractères "
                                "au moins) : sans elle, personne ne peut le lire, vous non plus. Le serveur ne la garde pas.",
                                {x, y}, w, 16, ui.theme().textDim);
    y += used.y + ui.s(14);
    float fh = ui.s(44);
    ui.passwordField("p1", {x, y, w, fh}, pass1_, "Phrase secrète");
    y += fh + ui.s(10);
    ui.passwordField("p2", {x, y, w, fh}, pass2_, "La même phrase, encore une fois");
    y += fh + ui.s(10);
    std::string hint = pass1_.size() < 12 ? "12 caractères au moins." : pass1_ != pass2_ ? "Les deux phrases sont différentes." : "";
    if (!hint.empty())
        ui.label(hint, {x, y + ui.s(8)}, 15, ui.theme().warm, Align::Left, FontFace::Body, VAlign::Middle);
    float bh = ui.s(46), by = box.bottom() - bh - ui.s(22);
    UIContext::ButtonStyle ok;
    ok.primary = true;
    ok.enabled = pass1_.size() >= 12 && pass1_.size() <= 200 && pass1_ == pass2_;
    if (ui.button("cancel", {box.right() - ui.s(30) - ui.s(330) - ui.s(12) - ui.s(170), by, ui.s(170), bh}, "Annuler") || ui.backPressed()) {
        pass1_.clear();
        pass2_.clear();
        downloadStep_ = 0;
    } else if (ui.button("save", {box.right() - ui.s(30) - ui.s(330), by, ui.s(330), bh}, "Choisir où l'enregistrer…", ok)) {
        downloadStep_ = 2;
        std::string def = Paths::toUtf8(Paths::userDir() / ("cosmic-backup-" + downloadDate_ + ".cpbk"));
        saveDialog_ = FileDialogs::saveFile(g_.app.window(), "Sauvegarde Cosmic Online", "cpbk", def);
    }
    ui.popId();
}

}  // namespace cosmic
