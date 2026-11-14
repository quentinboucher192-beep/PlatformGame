// Level editor — the chat of a collaborative project, in the live editor. Messages are kept
// by the server with the project (GET/POST /projects/{id}/messages); the room tells the id of
// the latest one with each heartbeat, and only then are the new ones fetched. A message may
// answer another one: the answer shows who and what it answers.

#include <algorithm>

#include <SDL3/SDL_scancode.h>

#include "editors/level_editor/LevelEditorScene.h"
#include "game/online/OnlineService.h"
#include "game/GameServices.h"
#include "ui/online/OnlineWidgets.h"

namespace cosmic {

namespace {
constexpr size_t kChatMax = 500;

std::string oneLine(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    return s;
}
}  // namespace

void LevelEditorScene::loadChat(bool older) {
    if (!live_ || chatLoading_) return;
    chatLoading_ = true;
    std::string path = "/api/v1/projects/" + liveProject_ + "/messages?limit=50";
    if (older && !chatMessages_.empty()) path += "&before=" + std::to_string(jget<int64_t>(chatMessages_[0], "id", 0));
    else if (!older && chatLoaded_) path += "&after=" + std::to_string(chatLastId_);
    const bool first = !chatLoaded_;
    std::weak_ptr<int> life = life_;
    g_.online.get(path, [this, life, older, first](const ApiResult& r) {
        if (!life.lock()) return;
        chatLoading_ = false;
        if (!r.ok) {
            chatError_ = r.message;
            return;
        }
        chatError_.clear();
        Json list = r.data.value("messages", Json::array());
        if (older) {
            Json merged = list;
            for (const auto& m : chatMessages_) merged.push_back(m);
            chatMessages_ = std::move(merged);
            chatMore_ = jget<bool>(r.data, "more", false);
        } else {
            for (const auto& m : list) {
                int64_t id = jget<int64_t>(m, "id", 0);
                if (id <= chatLastId_) continue;  // already shown (sent from here)
                chatMessages_.push_back(m);
                chatLastId_ = id;
            }
            if (first) {
                chatMore_ = jget<bool>(r.data, "more", false);
                chatSeenId_ = chatLastId_;  // the history is not "unread"
            }
            chatStick_ = true;
        }
        chatLoaded_ = true;
    }, false);
}

void LevelEditorScene::updateChat() {
    if (!live_ || live_->state() != LiveSession::State::Live) return;
    if (!chatLoaded_) {
        loadChat(false);
        return;
    }
    if (live_->chatSeq() > chatLastId_) loadChat(false);
    if (chatOpen_) chatSeenId_ = chatLastId_;
}

int LevelEditorScene::chatUnread() const {
    int n = 0;
    const std::string me = g_.online.playerId();
    for (const auto& m : chatMessages_)
        if (jget<int64_t>(m, "id", 0) > chatSeenId_ && jstr(m.value("author", Json::object()), "id") != me) ++n;
    return n;
}

void LevelEditorScene::sendChat() {
    std::string text = chatDraft_;
    while (!text.empty() && text.back() == ' ') text.pop_back();
    if (text.empty() || chatSending_ || !live_) return;
    Json body = {{"body", text}};
    if (chatReplyTo_.is_object()) body["reply_to"] = jget<int64_t>(chatReplyTo_, "id", 0);
    chatSending_ = true;
    std::weak_ptr<int> life = life_;
    g_.online.send("POST", "/api/v1/projects/" + liveProject_ + "/messages", body, [this, life](const ApiResult& r) {
        if (!life.lock()) return;
        chatSending_ = false;
        if (!r.ok) {
            chatError_ = r.message;  // the text stays in the field
            return;
        }
        chatError_.clear();
        chatDraft_.clear();
        chatReplyTo_ = Json();
        Json m = r.data.value("message", Json::object());
        int64_t id = jget<int64_t>(m, "id", 0);
        if (id > chatLastId_) {
            chatMessages_.push_back(m);
            chatLastId_ = id;
            chatSeenId_ = id;
        }
        chatStick_ = true;
    });
}

void LevelEditorScene::drawChatToggle(const Rect& r) {
    UIContext& ui = g_.ui;
    UIContext::ButtonStyle st;
    st.small = true;
    st.selected = chatOpen_;
    st.fontSize = 16;
    int unread = chatOpen_ ? 0 : chatUnread();
    if (ui.button("chat", r, "", st)) {
        chatOpen_ = !chatOpen_;
        if (chatOpen_) chatStick_ = true;
    }
    // Label on the left, the unread counter in its own place on the right.
    std::string n = unread > 99 ? "99+" : std::to_string(unread);
    float bw = unread > 0 ? std::max(ui.s(22), ui.text().measure(n, FontFace::Bold, ui.s(13)).x + ui.s(10)) : 0.0f;
    float room = r.w - ui.s(20) - (bw > 0 ? bw + ui.s(6) : 0.0f);
    std::string label = "Discussion";
    float fs = ui.fitFont(label, FontFace::Bold, 16, room, 0.7f);
    ui.label(ui.elide(label, FontFace::Bold, fs, room), {r.x + ui.s(10), r.center().y}, fs, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
    if (unread > 0) {
        Rect b{r.right() - ui.s(8) - bw, r.center().y - ui.s(11), bw, ui.s(22)};
        ui.r().fillRoundedRect(b, ui.s(11), ui.theme().danger);
        ui.label(n, b.center(), 13, Color::white(), Align::Center, FontFace::Bold, VAlign::Middle);
    }
}

void LevelEditorScene::drawChat() {
    if (!live_ || !chatOpen_ || chatRect_.w <= 0.0f) return;
    UIContext& ui = g_.ui;
    Renderer& rr = ui.r();
    const Rect R = chatRect_;
    rr.fillRoundedRect(R, ui.s(12), Color{8, 12, 28, 245});
    rr.drawRoundedRect(R, ui.s(12), ui.theme().accent.withAlphaF(0.5f), ui.s(1.2f));
    ui.pushId("chat");
    const float pad = ui.s(12), x = R.x + pad, w = R.w - 2 * pad;
    // Header.
    float y = R.y + ui.s(8);
    UIContext::ButtonStyle small;
    small.small = true;
    small.fontSize = 14;
    const float closeW = ui.s(84);
    std::string title = "Discussion du projet";
    ui.label(title, {x, y + ui.s(17)}, ui.fitFont(title, FontFace::Bold, 18, w - closeW - ui.s(10)), ui.theme().accent, Align::Left, FontFace::Bold,
             VAlign::Middle);
    if (ui.button("close", {R.right() - pad - closeW, y, closeW, ui.s(34)}, "Fermer", small)) chatOpen_ = false;
    y += ui.s(44);
    // Composer at the bottom: the answered message, the error, the field and its button.
    const float inputH = ui.s(42);
    float bottom = R.bottom() - pad;
    Rect input{x, bottom - inputH, w, inputH};
    bottom = input.y - ui.s(6);
    Rect replyBar{}, errorLine{};
    if (chatReplyTo_.is_object()) {
        replyBar = {x, bottom - ui.s(34), w, ui.s(34)};
        bottom = replyBar.y - ui.s(6);
    }
    if (!chatError_.empty()) {
        errorLine = {x, bottom - ui.s(22), w, ui.s(22)};
        bottom = errorLine.y - ui.s(4);
    }
    // Messages, newest at the bottom.
    Rect view{x, y, w, bottom - y};
    const std::string me = g_.online.playerId();
    const float textW = view.w - ui.s(24);
    struct Item {
        const Json* m;
        float h, bodyH;
        bool quote;
    };
    std::vector<Item> items;
    float total = chatMore_ ? ui.s(40) : 0.0f;
    for (const auto& m : chatMessages_) {
        bool quote = m.contains("reply_to") && m["reply_to"].is_object();
        float bodyH = ui.text().measureWrapped(jstr(m, "body"), FontFace::Body, ui.s(15), textW).y;
        float h = ui.s(8) + ui.s(22) + (quote ? ui.s(22) : 0.0f) + bodyH + ui.s(10);
        items.push_back({&m, h, bodyH, quote});
        total += h + ui.s(6);
    }
    if (chatMessages_.empty()) {
        std::string t = !chatLoaded_ ? (chatError_.empty() ? "Chargement…" : "Discussion indisponible pour l'instant.")
                                     : "Aucun message. Écrivez aux autres membres du projet : les messages sont gardés avec le projet.";
        ui.labelWrapped(t, {view.x, view.y + ui.s(6)}, view.w, 15, ui.theme().textDim);
    } else {
        if (chatStick_) {
            ui.setScroll("list", std::max(0.0f, total - view.h));
            chatStick_ = false;
        }
        ui.beginScroll("list", view, total);
        float cy = view.y - ui.scrollOffset();
        if (chatMore_) {
            if (cy + ui.s(34) > view.y && cy < view.bottom()) {
                UIContext::ButtonStyle o = small;
                o.enabled = !chatLoading_;
                if (ui.button("older", {view.x, cy, view.w - ui.s(14), ui.s(34)}, chatLoading_ ? "Chargement…" : "Messages précédents", o)) loadChat(true);
            }
            cy += ui.s(40);
        }
        for (const auto& it : items) {
            const Json& m = *it.m;
            Rect box{view.x, cy, view.w - ui.s(14), it.h};
            cy += it.h + ui.s(6);
            if (box.bottom() < view.y || box.y > view.bottom()) continue;
            const Json& a = m.value("author", Json::object());
            bool mine = jstr(a, "id") == me;
            Color tone = ui.theme().accent;
            if (const LiveSession::Participant* p = live_->participant(jstr(a, "id"))) tone = p->color;
            rr.fillRoundedRect(box, ui.s(8), mine ? Color{18, 36, 56, 230} : Color{16, 22, 42, 230});
            rr.fillRect({box.x, box.y + ui.s(6), ui.s(3), box.h - ui.s(12)}, tone);
            float bx = box.x + ui.s(10), bw = box.w - ui.s(16), by = box.y + ui.s(8);
            // Author, time, and « Répondre » in its own place on the right.
            const float replyW = ui.s(86);
            UIContext::ButtonStyle rs = small;
            rs.fontSize = 13;
            int64_t id = jget<int64_t>(m, "id", 0);
            if (ui.button("re" + std::to_string(id), {box.right() - ui.s(6) - replyW, by - ui.s(2), replyW, ui.s(24)}, "Répondre", rs)) {
                chatReplyTo_ = m;
                ui.editText("draft");
            }
            std::string who = mine ? std::string("Vous") : jstr(a, "username");
            std::string when = OnlineUi::ageOf(jstr(m, "created_at"));
            float room = bw - replyW - ui.s(8);
            float ww = ui.text().measure(when, FontFace::Body, ui.s(12)).x;
            float nameRoom = std::max(ui.s(40), room - ww - ui.s(10));
            float nfs = ui.fitFont(who, FontFace::Bold, 15, nameRoom, 0.8f);
            std::string shown = ui.elide(who, FontFace::Bold, nfs, nameRoom);
            Vec2 ns = ui.label(shown, {bx, by + ui.s(10)}, nfs, tone, Align::Left, FontFace::Bold, VAlign::Middle);
            if (ns.x + ui.s(10) + ww <= room)
                ui.label(when, {bx + ns.x + ui.s(10), by + ui.s(10)}, 12, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            by += ui.s(22);
            if (it.quote) {
                const Json& q = m["reply_to"];
                std::string qt = "En réponse à " + jstr(q, "username") + " : « " + oneLine(jstr(q, "excerpt")) + " »";
                ui.label(ui.elide(qt, FontFace::Body, 13, bw), {bx, by + ui.s(10)}, 13, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
                by += ui.s(22);
            }
            ui.labelWrapped(jstr(m, "body"), {bx, by}, textW, 15, ui.theme().text);
        }
        ui.endScroll();
    }
    // The answered message.
    if (replyBar.w > 0.0f) {
        rr.fillRoundedRect(replyBar, ui.s(6), Color{20, 40, 64, 230});
        const float cancelW = ui.s(84);
        std::string t = "Réponse à " + jstr(chatReplyTo_.value("author", Json::object()), "username") + " : « " + oneLine(jstr(chatReplyTo_, "body")) + " »";
        float room = replyBar.w - cancelW - ui.s(20);
        ui.label(ui.elide(t, FontFace::Body, 13, room), {replyBar.x + ui.s(8), replyBar.center().y}, 13, ui.theme().text, Align::Left, FontFace::Body,
                 VAlign::Middle);
        UIContext::ButtonStyle c = small;
        c.fontSize = 13;
        if (ui.button("noreply", {replyBar.right() - ui.s(4) - cancelW, replyBar.y + ui.s(4), cancelW, replyBar.h - ui.s(8)}, "Annuler", c))
            chatReplyTo_ = Json();
    }
    if (errorLine.w > 0.0f)
        ui.label(ui.elide(chatError_, FontFace::Body, 13, errorLine.w), {errorLine.x, errorLine.center().y}, 13, ui.theme().danger, Align::Left,
                 FontFace::Body, VAlign::Middle);
    // The field: Enter sends and keeps it open.
    const float sendW = ui.s(100);
    bool enter = ui.editingText() && ui.focused("draft") && g_.app.input().keyPressed(SDL_SCANCODE_RETURN);
    ui.textField("draft", {input.x, input.y, input.w - sendW - ui.s(8), input.h}, chatDraft_, "Votre message…", kChatMax);
    UIContext::ButtonStyle p;
    p.primary = true;
    p.small = true;
    p.fontSize = 15;
    p.enabled = !chatSending_ && chatDraft_.find_first_not_of(' ') != std::string::npos;
    bool clicked = ui.button("send", {input.right() - sendW, input.y, sendW, input.h}, chatSending_ ? "Envoi…" : "Envoyer", p);
    if ((clicked || enter) && p.enabled) {
        sendChat();
        if (enter) ui.editText("draft");
    }
    ui.popId();
}

}  // namespace cosmic
