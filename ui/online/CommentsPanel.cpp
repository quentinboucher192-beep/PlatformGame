#include "ui/online/CommentsPanel.h"

#include <algorithm>

#include "game/GameServices.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

const char* kSorts[] = {"recent", "popular", "oldest"};
const char* kReactions[] = {"like", "love", "funny", "impressive", "not_useful"};
// Server values (COMMENT_REPORT_REASONS) and their labels.
const char* kReasons[] = {"spam", "harassment", "inappropriate", "spoiler", "other"};
const std::vector<std::string> kReasonLabels = {"Spam ou publicité", "Harcèlement ou insultes", "Contenu inapproprié", "Divulgue la solution",
                                                "Autre"};

/// Places buttons one after the other, going to the next line when the row is full.
struct Flow {
    float x0, x, y, right, h, gap;
    Rect next(float w) {
        if (x + w > right && x > x0) {
            x = x0;
            y += h + gap;
        }
        Rect r{x, y, std::min(w, right - x0), h};
        x += r.w + gap;
        return r;
    }
    float bottom() const { return y + h; }
};

std::string trimmed(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string{} : s.substr(a, b - a + 1);
}

}  // namespace

const char* CommentsPanel::reactionLabel(const std::string& kind) {
    if (kind == "like") return "J'aime";
    if (kind == "love") return "J'adore";
    if (kind == "funny") return "Drôle";
    if (kind == "impressive") return "Impressionnant";
    if (kind == "not_useful") return "Pas utile";
    return "";
}

CommentsPanel::CommentsPanel(GameServices& g, OnlineUi& ui) : g_(g), ui_(ui) {}

void CommentsPanel::open(const std::string& levelId, const std::string& title) {
    bool same = levelId == levelId_;
    levelId_ = levelId;
    title_ = title;
    if (!same) {
        comments_ = Json::array();
        replies_.clear();
        expanded_.clear();
        draft_.clear();
        replyTo_.clear();
        editing_.clear();
        loaded_ = false;
        total_ = 0;
    }
    load(false);
}

void CommentsPanel::say(const std::string& m, bool error) {
    status_ = m;
    statusError_ = error;
    statusT_ = g_.ui.time();
}

void CommentsPanel::load(bool more) {
    if (levelId_.empty()) return;
    std::string path = "/api/v1/community/levels/" + levelId_ + "/comments?limit=20&sort=" + kSorts[sort_];
    if (more && nextOffset_ > 0) path += "&offset=" + std::to_string(nextOffset_);
    busy_ = true;
    int gen = ++generation_;
    std::string level = levelId_;
    g_.online.get(
        path, OnlineService::guard(life_, [this, more, gen, level](const ApiResult& r) {
            if (gen != generation_ || level != levelId_) return;
            busy_ = false;
            loaded_ = true;
            if (!r.ok) return say(r.message, true);
            Json list = r.data.value("comments", Json::array());
            if (more)
                for (auto& c : list) comments_.push_back(c);
            else
                comments_ = list;
            total_ = jget<int>(r.data, "comment_count", static_cast<int>(comments_.size()));
            nextOffset_ = r.meta.is_object() && r.meta.contains("next_offset") && r.meta["next_offset"].is_number() ? r.meta["next_offset"].get<int>() : -1;
            if (expandAll_) {
                expandAll_ = false;
                for (const auto& c : comments_)
                    if (jget<int>(c, "reply_count", 0) > 0) expanded_.insert(jstr(c, "id"));
            }
            // Replies already open are read again (new answers, reactions).
            if (!more)
                for (const auto& id : expanded_) loadReplies(id);
        }),
        false);
}

void CommentsPanel::loadReplies(const std::string& id) {
    g_.online.get(
        "/api/v1/community/comments/" + id + "/replies?limit=50", OnlineService::guard(life_, [this, id](const ApiResult& r) {
            if (!r.ok) return say(r.message, true);
            replies_[id] = r.data.value("replies", Json::array());
        }),
        false);
}

Json* CommentsPanel::find(const std::string& id) {
    for (auto& c : comments_)
        if (jstr(c, "id") == id) return &c;
    for (auto& [parent, list] : replies_)
        for (auto& c : list)
            if (jstr(c, "id") == id) return &c;
    return nullptr;
}

void CommentsPanel::submit() {
    std::string body = trimmed(draft_);
    if (body.empty() || sending_) return;
    sending_ = true;
    auto done = [this](const std::string& ok, const std::string& parent) {
        return OnlineService::guard(life_, [this, ok, parent](const ApiResult& r) {
            sending_ = false;
            if (!r.ok) return say(r.message, true);
            say(ok, false);
            draft_.clear();
            replyTo_.clear();
            editing_.clear();
            if (!parent.empty()) {
                expanded_.insert(parent);
                loadReplies(parent);
            }
            load(false);
        });
    };
    if (!editing_.empty()) {
        g_.online.send("PATCH", "/api/v1/community/comments/" + editing_, {{"body", body}}, done("Commentaire modifié.", {}));
        return;
    }
    Json req = {{"body", body}};
    std::string parent;
    if (!replyTo_.empty()) {
        req["parent_id"] = replyTo_;
        // A reply to a reply goes under the same comment (one level of replies).
        parent = replyTo_;
        if (const Json* c = find(replyTo_); c && c->contains("parent_id") && (*c)["parent_id"].is_string()) parent = (*c)["parent_id"].get<std::string>();
    }
    g_.online.send("POST", "/api/v1/community/levels/" + levelId_ + "/comments", req,
                   done(replyTo_.empty() ? "Commentaire publié." : "Réponse publiée.", parent));
}

void CommentsPanel::react(const std::string& id, const std::string& kind, const std::string& mine) {
    bool remove = kind == mine;
    auto cb = OnlineService::guard(life_, [this, id](const ApiResult& r) {
        if (!r.ok) return say(r.message, true);
        if (Json* c = find(id)) {
            (*c)["reactions"] = r.data.value("reactions", Json::object());
            (*c)["my_reaction"] = r.data.value("my_reaction", Json());
        }
    });
    if (remove) g_.online.send("DELETE", "/api/v1/community/comments/" + id + "/reaction", Json(), cb);
    else g_.online.send("PUT", "/api/v1/community/comments/" + id + "/reaction", {{"kind", kind}}, cb);
}

void CommentsPanel::pin(const std::string& id, bool on) {
    g_.online.send(on ? "POST" : "DELETE", "/api/v1/community/comments/" + id + "/pin", Json(), OnlineService::guard(life_, [this, on](const ApiResult& r) {
                       if (!r.ok) return say(r.message, true);
                       say(on ? "Commentaire épinglé en tête." : "Commentaire désépinglé.", false);
                       load(false);
                   }));
}

float CommentsPanel::item(const Json& c, float x, float y, float w, bool reply, bool draw) {
    UIContext& ui = g_.ui;
    const float pad = ui.s(12), top = y;
    const std::string id = jstr(c, "id");
    const bool deleted = jget<bool>(c, "deleted", false);
    const bool hidden = jget<bool>(c, "hidden", false);
    const bool pinned = jget<bool>(c, "pinned", false);
    const bool mine = jget<bool>(c, "mine", false);
    const bool logged = g_.online.loggedIn();
    const float av = ui.s(reply ? 30 : 36);
    const float tx = x + pad + av + ui.s(12), tw = x + w - pad - tx;
    const float total = draw ? item(c, x, y, w, reply, false) : 0.0f;
    if (draw) {
        Rect bg{x, y, w, total};
        ui.r().fillRoundedRect(bg, ui.s(10), reply ? Color{12, 19, 40, 215} : Color{14, 22, 46, 230});
        ui.r().drawRoundedRect(bg, ui.s(10), pinned ? ui.theme().success.withAlphaF(0.7f) : ui.theme().border.withAlphaF(0.6f), ui.s(1.2f));
    }
    y += pad;
    // Header: avatar, pseudonym (opens the profile), badges, date.
    if (draw) {
        std::string when = OnlineUi::ageOf(jstr(c, "created_at"));
        if (c.contains("edited_at") && c["edited_at"].is_string()) when += " · modifié";
        float dw = ui.text().measure(when, FontFace::Body, ui.s(13)).x;
        ui.label(when, {x + w - pad, y + av * 0.5f}, 13, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
        float right = x + w - pad - dw - ui.s(14);
        if (deleted || !c.contains("author") || !c["author"].is_object()) {
            ui.r().fillRoundedRect({x + pad, y, av, av}, av * 0.5f, Color{30, 36, 60, 255});
            ui.label("Commentaire supprimé par son auteur", {tx, y + av * 0.5f}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        } else {
            const Json& a = c["author"];
            ui_.drawAvatar(jstr(a, "avatar", "avatar_01"), jstr(a, "id"), {x + pad, y, av, av});
            // Badges, measured first so that the pseudonym takes what is left.
            struct Chip {
                std::string text;
                Color color;
            };
            std::vector<Chip> chips;
            if (jget<bool>(c, "level_author", false)) chips.push_back({"AUTEUR DU NIVEAU", ui.theme().accent});
            else if (jget<bool>(c, "creator", false)) chips.push_back({"CRÉATEUR", ui.theme().warm});
            if (pinned) chips.push_back({"ÉPINGLÉ", ui.theme().success});
            float chipsW = 0.0f;
            for (const auto& ch : chips) chipsW += ui.text().measure(ch.text, FontFace::Bold, ui.s(12)).x + ui.s(16) + ui.s(8);
            std::string name = jstr(a, "username");
            float nameW = std::min(ui.text().measure(name, FontFace::Bold, ui.s(16)).x + ui.s(22), std::max(ui.s(80), right - tx - chipsW));
            UIContext::ButtonStyle ns;
            ns.small = true;
            ns.fontSize = 16;
            ns.enabled = !jstr(a, "id").empty();
            if (ui.button("who/" + id, {tx, y + av * 0.5f - ui.s(15), nameW, ui.s(30)}, name, ns))
                g_.app.pushScene(makePlayerProfile(g_, jstr(a, "id")));
            float cx = tx + nameW + ui.s(8);
            for (const auto& ch : chips) {
                float cw = ui.text().measure(ch.text, FontFace::Bold, ui.s(12)).x + ui.s(16);
                if (cx + cw > right) break;
                Rect cr{cx, y + av * 0.5f - ui.s(11), cw, ui.s(22)};
                ui.r().fillRoundedRect(cr, ui.s(11), ch.color.withAlphaF(0.22f));
                ui.r().drawRoundedRect(cr, ui.s(11), ch.color.withAlphaF(0.8f), ui.s(1));
                ui.label(ch.text, cr.center(), 12, ch.color, Align::Center, FontFace::Bold, VAlign::Middle);
                cx += cw + ui.s(8);
            }
        }
    }
    y += av + ui.s(8);
    if (!deleted) {
        std::string body = jstr(c, "body");
        Vec2 sz = ui.text().measureWrapped(body, FontFace::Body, ui.s(16), tw);
        if (draw) ui.labelWrapped(body, {tx, y}, tw, 16, ui.theme().text);
        y += sz.y + ui.s(8);
        if (hidden) {
            std::string note = "Masqué après des signalements : en attente de la décision des modérateurs (vous seul le voyez).";
            Vec2 ns = ui.text().measureWrapped(note, FontFace::Body, ui.s(13), tw);
            if (draw) ui.labelWrapped(note, {tx, y}, tw, 13, ui.theme().warm);
            y += ns.y + ui.s(6);
        }
        // Reactions: one per player; choosing it again removes it.
        const Json& counts = c.value("reactions", Json::object());
        std::string my = c.contains("my_reaction") && c["my_reaction"].is_string() ? c["my_reaction"].get<std::string>() : std::string{};
        Flow f{tx, tx, y, x + w - pad, ui.s(30), ui.s(6)};
        for (const char* k : kReactions) {
            int n = jget<int>(counts, k, 0);
            std::string label = std::string(reactionLabel(k)) + (n > 0 ? "  " + std::to_string(n) : "");
            Rect r = f.next(ui.text().measure(label, FontFace::Bold, ui.s(14)).x + ui.s(24));
            if (draw) {
                UIContext::ButtonStyle st;
                st.small = true;
                st.fontSize = 14;
                st.selected = my == k;
                st.primary = my == k;
                st.enabled = logged && !hidden;
                if (ui.button("re/" + id + "/" + k, r, label, st)) react(id, k, my);
            }
        }
        y = f.bottom() + ui.s(6);
    }
    // Actions.
    struct Act {
        std::string key, label;
        bool danger;
    };
    std::vector<Act> acts;
    const Json& a = c.value("author", Json::object());
    if (!deleted && logged && !hidden) acts.push_back({"reply", "Répondre", false});
    if (jget<bool>(c, "can_edit", false)) acts.push_back({"edit", "Modifier", false});
    if (mine && !deleted) acts.push_back({"delete", "Supprimer", true});
    if (!mine && !deleted && logged) acts.push_back({"report", jget<bool>(c, "reported", false) ? "Signalé" : "Signaler", true});
    if (jget<bool>(c, "can_pin", false)) acts.push_back({"pin", pinned ? "Désépingler" : "Épingler", false});
    int replies = jget<int>(c, "reply_count", 0);
    if (!reply && replies > 0)
        acts.push_back({"replies", expanded_.count(id) ? "Masquer les réponses"
                                                         : (replies == 1 ? std::string("Voir la réponse") : "Voir les " + std::to_string(replies) + " réponses"),
                        false});
    if (!acts.empty()) {
        Flow f{tx, tx, y, x + w - pad, ui.s(28), ui.s(6)};
        for (const auto& act : acts) {
            Rect r = f.next(ui.text().measure(act.label, FontFace::Bold, ui.s(13)).x + ui.s(22));
            if (!draw) continue;
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = 13;
            st.danger = act.danger;
            st.enabled = !(act.key == "report" && jget<bool>(c, "reported", false));
            if (!ui.button("act/" + id + "/" + act.key, r, act.label, st)) continue;
            if (act.key == "reply") {
                replyTo_ = id;
                replyName_ = jstr(a, "username");
                editing_.clear();
                ui.setFocus("draft");
            } else if (act.key == "edit") {
                editing_ = id;
                replyTo_.clear();
                draft_ = jstr(c, "body");
                ui.setFocus("draft");
            } else if (act.key == "delete") {
                confirmDelete_ = id;
            } else if (act.key == "report") {
                confirmReport_ = id;
                reportReason_ = 0;
            } else if (act.key == "pin") {
                pin(id, !pinned);
            } else if (act.key == "replies") {
                if (expanded_.count(id)) expanded_.erase(id);
                else {
                    expanded_.insert(id);
                    loadReplies(id);
                }
            }
        }
        y = f.bottom();
    }
    return y + pad - top;
}

float CommentsPanel::listHeight(float w) {
    UIContext& ui = g_.ui;
    float h = 0.0f, gap = ui.s(10), indent = ui.s(44);
    for (const auto& c : comments_) {
        h += item(c, 0, 0, w, false, false) + gap;
        std::string id = jstr(c, "id");
        if (expanded_.count(id) && replies_.count(id))
            for (const auto& r : replies_[id]) h += item(r, 0, 0, w - indent, true, false) + gap;
    }
    if (nextOffset_ > 0) h += ui.s(60);
    return h;
}

bool CommentsPanel::draw(const Rect& area) {
    UIContext& ui = g_.ui;
    bool stay = true;
    ui.pushId("comments");
    float x = area.x + ui.s(12), w = area.w - ui.s(24), y = area.y + ui.s(12);
    // Header: back to the list, title, sort.
    UIContext::ButtonStyle back;
    back.small = true;
    float bw = ui.s(200);
    if (ui.button("back", {x, y, bw, ui.s(40)}, "Retour à la liste", back)) stay = false;
    float sortW = std::min(ui.s(380), w * 0.42f);
    int sort = sort_;
    ui.tabs("sort", {x + w - sortW, y, sortW, ui.s(40)}, {"Récents", "Populaires", "Anciens"}, sort);
    if (sort != sort_) {
        sort_ = sort;
        load(false);
    }
    std::string head = "Commentaires (" + std::to_string(total_) + ") · " + title_;
    float room = w - bw - sortW - ui.s(32);
    ui.label(head, {x + bw + ui.s(16), y + ui.s(20)}, ui.fitFont(head, FontFace::Bold, 19, room), ui.theme().text, Align::Left, FontFace::Bold,
             VAlign::Middle);
    y += ui.s(52);
    // Composer.
    if (!g_.online.loggedIn()) {
        ui.label("Connectez-vous pour commenter.", {x, y + ui.s(16)}, 16, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        y += ui.s(40);
    } else {
        if (!replyTo_.empty() || !editing_.empty()) {
            std::string ctx = !editing_.empty() ? "Modification de votre commentaire" : "Réponse à " + replyName_;
            UIContext::ButtonStyle cs;
            cs.small = true;
            cs.fontSize = 14;
            float cw = ui.s(120);
            ui.label(ctx, {x, y + ui.s(14)}, ui.fitFont(ctx, FontFace::Body, 15, w - cw - ui.s(12)), ui.theme().accent, Align::Left, FontFace::Body,
                     VAlign::Middle);
            if (ui.button("cancelctx", {x + w - cw, y, cw, ui.s(28)}, "Annuler", cs)) {
                replyTo_.clear();
                if (!editing_.empty()) draft_.clear();
                editing_.clear();
            }
            y += ui.s(34);
        }
        float pw = ui.s(160);
        ui.textField("draft", {x, y, w - pw - ui.s(10), ui.s(44)}, draft_,
                     replyTo_.empty() ? "Écrire un commentaire (1000 caractères au plus)" : "Écrire une réponse", 1000);
        UIContext::ButtonStyle ps;
        ps.primary = true;
        ps.small = true;
        ps.enabled = !sending_ && !trimmed(draft_).empty();
        if (ui.button("send", {x + w - pw, y, pw, ui.s(44)}, sending_ ? "Envoi…" : (editing_.empty() ? "Publier" : "Enregistrer"), ps)) submit();
        y += ui.s(54);
    }
    // List.
    Rect view{x, y, w, area.bottom() - y - ui.s(40)};
    if (comments_.empty()) {
        ui.labelWrapped(!loaded_ || busy_ ? "Chargement…" : "Aucun commentaire pour l'instant : donnez le premier avis sur ce niveau.", {x + ui.s(4), y + ui.s(8)},
                        w - ui.s(8), 16, ui.theme().textDim);
    } else {
        float cw = view.w - ui.s(14);
        ui.beginScroll("list", view, listHeight(cw));
        float cy = view.y - ui.scrollOffset(), gap = ui.s(10), indent = ui.s(44);
        for (const auto& c : comments_) {
            float h = item(c, 0, 0, cw, false, false);
            if (cy + h > view.y && cy < view.bottom()) item(c, view.x, cy, cw, false, true);
            cy += h + gap;
            std::string id = jstr(c, "id");
            if (!expanded_.count(id) || !replies_.count(id)) continue;
            for (const auto& r : replies_[id]) {
                float rh = item(r, 0, 0, cw - indent, true, false);
                if (cy + rh > view.y && cy < view.bottom()) item(r, view.x + indent, cy, cw - indent, true, true);
                cy += rh + gap;
            }
        }
        if (nextOffset_ > 0) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.enabled = !busy_;
            if (ui.button("more", {view.center().x - ui.s(150), cy + ui.s(8), ui.s(300), ui.s(42)}, busy_ ? "Chargement…" : "Plus de commentaires", st))
                load(true);
        }
        ui.endScroll();
    }
    if (!status_.empty() && ui.time() - statusT_ < 8.0f)
        ui.label(status_, {x, area.bottom() - ui.s(20)}, ui.fitFont(status_, FontFace::Body, 15, w), statusError_ ? ui.theme().danger : ui.theme().success,
                 Align::Left, FontFace::Body, VAlign::Middle);
    ui.popId();
    return stay;
}

void CommentsPanel::drawModal() {
    UIContext& ui = g_.ui;
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 170});
    Rect box{ui.screenW() * 0.5f - ui.s(320), ui.screenH() * 0.5f - ui.s(150), ui.s(640), ui.s(300)};
    ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
    ui.r().drawRoundedRect(box, ui.s(18), ui.theme().accent.withAlphaF(0.55f), ui.s(1.5f));
    ui.pushId("comments/confirm");
    UIContext::ButtonStyle d;
    d.danger = true;
    Rect ok{box.x + ui.s(30), box.bottom() - ui.s(70), ui.s(240), ui.s(46)};
    Rect cancel{box.right() - ui.s(210), box.bottom() - ui.s(70), ui.s(180), ui.s(46)};
    if (!confirmDelete_.empty()) {
        ui.labelWrapped("Supprimer votre commentaire ? S'il a reçu des réponses, il reste affiché comme « supprimé » pour qu'on puisse suivre la "
                        "discussion.",
                        {box.x + ui.s(30), box.y + ui.s(30)}, box.w - ui.s(60), 17, ui.theme().text);
        if (ui.button("ok", ok, "Supprimer", d)) {
            std::string id = confirmDelete_;
            confirmDelete_.clear();
            g_.online.send("DELETE", "/api/v1/community/comments/" + id, Json(), OnlineService::guard(life_, [this](const ApiResult& r) {
                               if (!r.ok) return say(r.message, true);
                               say("Commentaire supprimé.", false);
                               load(false);
                           }));
        } else if (ui.button("cancel", cancel, "Annuler") || ui.backPressed()) {
            confirmDelete_.clear();
        }
    } else if (!confirmReport_.empty()) {
        ui.labelWrapped("Signaler ce commentaire aux modérateurs ? Après plusieurs signalements, il est masqué en attendant leur décision.",
                        {box.x + ui.s(30), box.y + ui.s(30)}, box.w - ui.s(60), 17, ui.theme().text);
        ui.cycle("reason", {box.x + ui.s(30), box.y + ui.s(120), box.w - ui.s(60), ui.s(42)}, "Motif", reportReason_, kReasonLabels);
        if (ui.button("ok", ok, "Signaler", d)) {
            std::string id = confirmReport_;
            confirmReport_.clear();
            g_.online.send("POST", "/api/v1/community/comments/" + id + "/report", {{"reason", kReasons[std::clamp(reportReason_, 0, 4)]}},
                           OnlineService::guard(life_, [this, id](const ApiResult& r) {
                               if (!r.ok) return say(r.message, true);
                               say("Commentaire signalé. Merci.", false);
                               if (Json* c = find(id)) (*c)["reported"] = true;
                           }));
        } else if (ui.button("cancel", cancel, "Annuler") || ui.backPressed()) {
            confirmReport_.clear();
        }
    }
    ui.popId();
}

}  // namespace cosmic
