#pragma once
// The shared humanoid figure (the Trawl and Red Tide Visual Overhaul Specs: "one humanoid rig shared by both games").
// One skeleton (tools/artgen/crew.py: pelvis, spine, chest, neck, head, arms with five-fingered hands, legs, eyes and a
// mouth); every sailor and every diver is a skinned .glb on it. Posing is procedural, in code: a walk, a swim, reaching
// to work, gripping, breathing, blinking, a shout, and two-bone IK to put a fist on a grip.
//
// Frames: the model's axes are x forward, y up, z the figure's right; feet at y = 0.
#include "raylib.h"
#include "redtide_render.h"
#include <vector>

namespace fig {

struct Build { float build = 1, height = 1, headW = 1, headH = 1; };   // broad, tall, the head's width and height
struct Pose {
    float walk = 0, walkPh = 0;         // 0..1 how much of a stride, and its phase
    float reach = 0, reachL = -1;       // both arms forward to the work (0..1); reachL < 0: the same as reach
    float elbow = 0;                    // forearms raised (0..1)
    float grip = 0.3f;                  // fingers curled (0 open, 1 a fist)
    float sit = 0, tread = 0;           // seated on a thwart; treading water (arms out, working)
    float crouch = 0;                   // 0..1 a squat: knees deep, heels down, the body leaned over them
    float swim = 0, kickPh = 0;         // swimming (0..1: legs straight back in a flutter kick, arms in a slow stroke)
    float breathe = 0, look = 0, nod = 0;   // breathing phase; head turned (rad), tipped (rad)
    float blink = 0;                    // 0 open, 1 shut
    float shout = 0;                    // the mouth open (a bark, a shout)
    float fear = 0, strain = 0, grin = 0;   // expressions (0..1): wide eyes and an open mouth; a squint and a clenched line; a broad smile
    float swingT = 0;                   // a melee swing (0..1)
    bool fp = false;                    // your own body seen from inside it: no head, the arms up in front of you
    float aimUp = 0, twoHand = 0;       // first person: the arms tipped up (a kick, a raised swing); the left hand under a long tool
    bool ik[2] = {false, false};        // a hand locked to a grip (0 left, 1 right): the rod, the helm's spokes, a fore-end
    Vector3 target[2]{};                // where each fist closes, in the figure's frame (x forward, y up, z right)
};

// the skinning matrices for the pose (bind-to-posed, in the model's space)
std::vector<Matrix> PoseFigure(const Model& m, const Build& b, const Pose& P, float t);
// where the right fist closes (the middle finger's root), in the world
Vector3 FistWorld(const Model& m, const std::vector<Matrix>& skin, Matrix frame, int side = 1);
// a frame from feet, facing yaw (radians about +Y; 0 faces +X)
Matrix Frame(Vector3 feet, float yaw);

// a costume (skins.h; tools/artgen/costumes.py) over a figure on the crew rig: the figure's own skinning matrices, matched
// to the costume's bones by name; false if the costume's model is missing
bool DrawCostume(const char* model, const Model& figure, const std::vector<Matrix>& skin, Matrix frame, Color tint = WHITE);

} // namespace fig
