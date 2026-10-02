// Red Tide's Visual Overhaul (Red_Tide_Reference/"Red Tide - Visual Overhaul Spec.pdf"; progress in
// docs/REDTIDE_VISUAL.md): the four divers on the shared humanoid figure (figure3d.h, tools/artgen/rt_divers.py), and
// the studio, the harness's fixed shots of each new asset (--shots shots/vis rvis_).
#include "redtide_vis.h"
#include "figure3d.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace rt {

const Model* DiverModel(int voice) {
    static const char* F[4] = {"shared/divers/diver_diver.glb", "shared/divers/diver_whaler.glb", "shared/divers/diver_stowaway.glb", "shared/divers/diver_mechanic.glb"};
    return LoadAsset(F[std::clamp(voice, 0, 3)]);
}
bool DiversReady() { return DiverModel(0) != nullptr; }

// a diver's own face, steady for the slot (the person inside the suit)
static Color DiverSkin(int voice) {
    static const Color SKIN[4] = {{222, 170, 130, 255}, {160, 108, 76, 255}, {238, 196, 160, 255}, {198, 140, 104, 255}};
    return SKIN[std::clamp(voice, 0, 3)];
}

// the body language from the quips (spec, "Personality in posture"): the Diver economical and upright, the Whaler
// heavy-shouldered and forward-leaning, the Stowaway loose with a little sway, the Mechanic tight and quick
static void Temperament(int voice, fig::Pose& P, float t) {
    switch (voice) {
        case 1: P.nod += 0.12f; P.reach = std::max(P.reach, 0.12f); break;
        case 2: P.look += 0.15f * sinf(t * 0.7f); P.nod += 0.05f * sinf(t * 0.9f); break;
        case 3: P.elbow = std::max(P.elbow, 0.15f); P.grip = std::max(P.grip, 0.6f); break;
        default: break;
    }
}

std::vector<Matrix> DrawDiverFigure(int voice, Matrix frame, fig::Pose P, float t, Color tint) {
    const Model* m = DiverModel(voice);
    if (!m) return {};
    Temperament(voice, P, t);
    fig::Build B; B.build = voice == 3 ? 1.05f : 1.0f;
    std::vector<Matrix> skin = fig::PoseFigure(*m, B, P, t);
    std::vector<Recolor> rc = {{"skin", DiverSkin(voice)}};
    DrawPbrSkinned(*m, frame, skin, rc, 0.35f, tint);
    return skin;
}

// ---------------------------------------------------------------- the studio
// 0 the four divers front, side and back; 1 their faces through the ports; 2 swimming (the flutter kick, mid-stroke)
void DrawRedTideStudio(int which, float t) {
    SceneLight L;
    // a clear, bright patch of shallow water: the sun from above, the sea's blue fill, a little haze
    L.fog = {38, 92, 104, 255}; L.fogDensity = 0.01f;
    L.fill = {40, 96, 112, 255}; L.rim = {120, 200, 220, 255}; L.key = {255, 236, 200, 255};
    L.surfaceY = 6; L.time = t;
    L.lampRange = 14; L.lampCone = 0.55f;
    L.moonDir = Vector3Normalize({-0.3f, -1.0f, -0.2f}); L.moon = {200, 230, 235, 255}; L.moonK = 0.55f;
    L.ambK = 0.55f; L.skyAmb = {60, 116, 136, 255}; L.seaAmb = {16, 34, 40, 255};
    L.filmic = 1; L.exposure = 0.85f; L.aoK = 0.6f; L.outline = 0.35f; L.outlineTint = {16, 44, 52, 255}; L.stipple = 0;
    Camera3D cam{}; cam.fovy = 40; cam.projection = CAMERA_PERSPECTIVE; cam.up = {0, 1, 0};
    auto lamp = [&](Vector3 at, Vector3 target) { L.lampPos = at; L.lampDir = Vector3Normalize(Vector3Subtract(target, at)); };
    const float FRONT = -PI / 2, SIDE = 0, BACK = PI / 2;
    if (which == 0) {
        cam.position = {0, 1.3f, 10.5f}; cam.target = {0, 1.0f, 0}; cam.fovy = 34;
        lamp({-3, 5, 7}, {0, 1, 0});
        RenderBegin(cam, L);
        for (int d = 0; d < 4; d++) for (int v = 0; v < 3; v++) {
            float x = (d - 1.5f) * 2.6f + (v - 1) * 0.78f;
            fig::Pose P; P.breathe = t * 1.4f + d; P.blink = 0;
            DrawDiverFigure(d, fig::Frame({x, 0, v == 1 ? -0.3f : 0.0f}, v == 0 ? FRONT : v == 1 ? SIDE : BACK), P, t, WHITE);
        }
    } else if (which == 1) {
        cam.position = {0, 1.66f, 3.2f}; cam.target = {0, 1.6f, 0}; cam.fovy = 30;
        lamp({-1.2f, 2.6f, 2.6f}, {0, 1.6f, 0});
        RenderBegin(cam, L);
        for (int d = 0; d < 4; d++) {
            fig::Pose P; P.breathe = t * 1.4f + d; P.shout = d == 2 ? 0.6f : 0;
            DrawDiverFigure(d, fig::Frame({(d - 1.5f) * 0.62f, 0, 0}, FRONT + (d - 1.5f) * 0.12f), P, t, WHITE);
        }
    } else if (which == 2) {
        cam.position = {0, 2.2f, 8.5f}; cam.target = {0, 1.4f, 0}; cam.fovy = 38;
        lamp({-3, 6, 6}, {0, 1, 0});
        RenderBegin(cam, L);
        for (int d = 0; d < 4; d++) {
            fig::Pose P; P.breathe = t * 2.2f + d; P.swim = 1; P.kickPh = t * 5 + d * 1.3f;
            // the body lies along the stroke: pitched forward about its hips, drifting
            Matrix tip = MatrixMultiply(MatrixMultiply(MatrixTranslate(0, -1.0f, 0), MatrixRotateZ(-1.25f)), MatrixTranslate(0, 1.0f, 0));
            DrawDiverFigure(d, MatrixMultiply(tip, fig::Frame({(d - 1.5f) * 2.4f, 0.6f + 0.25f * (d % 2), 0}, d % 2 ? -0.5f : 0.5f)), P, t, WHITE);
        }
    }
    RenderEnd();
}

} // namespace rt
