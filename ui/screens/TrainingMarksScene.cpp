// 2.91 — Pause › « Startpos et mesures » (a section training): the StartPos and the measure points
// of this level version over the paused run (TrainingMarksPanel): start the next attempt from a
// StartPos (or from the training's own start), delete them, rename and move the measure points,
// the display options. « Reprendre » goes back to the run.

#include "game/GameServices.h"
#include "ui/screens/GameScene.h"
#include "ui/screens/TrainingMarksPanel.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

class TrainingMarksScene : public Scene {
public:
    TrainingMarksScene(GameServices& g, GameScene& game) : g_(g), game_(game) {}
    // The whole screen (never over another interactive screen: its UI frame and focus stay this one's).
    bool isOverlay() const override { return false; }
    void onEnter() override { g_.ui.setFocus("marks/resume"); }

    void render(float) override {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{4, 6, 14, 255});
        const float m = ui.s(48);
        const Rect panel{m, ui.s(36), ui.screenW() - 2 * m, ui.screenH() - ui.s(72)};
        ui.panel(panel, true, 18);
        ui.pushId("marks");
        const float x = panel.x + ui.s(30), w = panel.w - ui.s(60);
        float y = panel.y + ui.s(26);
        // Title, the level, « Reprendre ».
        const float rw = ui.s(220);
        UIContext::ButtonStyle rs;
        rs.small = true;
        const bool resume = ui.button("resume", {panel.right() - ui.s(30) - rw, y, rw, ui.s(46)}, "Reprendre", rs);
        const std::string title = "STARTPOS ET POINTS DE MESURE";
        const float tf = ui.fitFont(title, FontFace::Title, 32, w * 0.55f, 0.6f);
        const Vec2 ts = ui.label(title, {x, y + ui.s(23)}, tf, th.text, Align::Left, FontFace::Title, VAlign::Middle);
        const std::string sub = game_.levelName() + " · version actuelle";
        const float subRoom = w - rw - ui.s(30) - ts.x - ui.s(24);
        if (subRoom > ui.s(80))
            ui.label(ui.elide(sub, FontFace::Body, fz(ui, 18), subRoom), {x + ts.x + ui.s(24), y + ui.s(23)}, fz(ui, 18), th.textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
        y += ui.s(70);
        // The panel itself.
        TrainingMarksPanel::Context c;
        c.levelKey = game_.levelKey();
        c.levelBounds = game_.level().bounds;
        for (const LevelObject& o : game_.level().objects)
            if (o.type == "checkpoint") c.checkpoints.push_back({o.rect.bottomCenter().x, o.propStr("label")});
        TrainingSession& session = game_.trainingSession();
        c.where = [&session](Vec2 feet) { return session.whereOnRoute(feet); };
        c.startFrom = [this](int id) { startFrom_ = id, start_ = true; };
        c.movePair = [this, &session](int id) {
            session.moveNext(id);
            move_ = true;
        };
        const float hintsH = ui.s(44);
        panel_.draw(g_, {x, y, w, panel.bottom() - y - hintsH - ui.s(20)}, c);
        // The keys.
        const float gh = ui.s(28);
        Glyphs::drawHint(ui, Action::MenuBack, "Reprendre", {x, panel.bottom() - ui.s(32)}, gh);
        ui.popId();
        if (start_) {
            // The next attempt from that StartPos (0: the training's own start): over the pause too.
            start_ = false;
            g_.app.popScene();
            g_.app.popScene();
            game_.restartFrom(startFrom_);
        } else if (move_) {
            // « Déplacer A / B »: back to the run, the next measure marks place them again.
            move_ = false;
            g_.app.popScene();
            g_.app.popScene();
            game_.world()->hint("Point de mesure : posez A, puis B, là où ils doivent aller", 3.0f);
        } else if (resume || (ui.backPressed() && !panel_.editing())) {
            g_.app.popScene();
        }
        ui.end();
    }

private:
    GameServices& g_;
    GameScene& game_;
    TrainingMarksPanel panel_;
    int startFrom_ = 0;
    bool start_ = false, move_ = false;
};

}  // namespace

std::unique_ptr<Scene> makeTrainingMarks(GameServices& g, GameScene& game) { return std::make_unique<TrainingMarksScene>(g, game); }

}  // namespace cosmic
