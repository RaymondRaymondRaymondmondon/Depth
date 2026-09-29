// ============================================================================
//  DEPTH - the shared figure rig (Master Reference, "Shared visual direction").
//
//  A humanoid is a set of named bones solved from a handful of animatable channels (hips, lean, the two hands,
//  the two feet, the weapon angle ...): the torso and head hang off the spine by forward kinematics, the arms and
//  legs reach their hand and foot targets by two-bone IK. A Clip keys those channels over time with easing
//  (anticipation, snap, overshoot, settle); clips layer additively over a base (idle, walk, guard) and the
//  combat Pose. Chains are short verlet ropes hung from bones (hair, hems, harpoon lines, tentacles) that trail
//  behind movement, drift with the scene's current and whip on hits. Parts are collected with a depth and drawn
//  back to front, so an arm can cross in front of the torso. Every primitive carries a material.
// ============================================================================
#pragma once
#include "raylib.h"
#include <functional>
#include <vector>

struct Pose;

namespace rig {

// ---------------------------------------------------------------- materials
enum Mat { SKIN, CLOTH, METAL, SHELL, GLOW, WET, MAT_COUNT };
void MLimb(Vector2 a, Vector2 b, float wa, float wb, Color c, Mat m);
void MBall(Vector2 c, float r, Color col, Mat m);
void MQuad(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color c, Mat m);

// ---------------------------------------------------------------- bones and channels
enum Bone { ROOT, HIPS, SPINE, CHEST, NECK, HEAD, SH_F, EL_F, WR_F, SH_B, EL_B, WR_B, HIP_F, KN_F, AN_F, HIP_B, KN_B, AN_B, PROP, BONE_COUNT };
const char* BoneName(int b);

// The animatable channels. Offsets are in reference units (a figure is ~170 tall), y grows DOWN; everything is
// written facing right and mirrored for a figure facing left.
enum Ch {
    C_ROOTX, C_ROOTY,   // the whole body shifts (lunges, knock-backs)
    C_HIPX, C_HIPY,     // the pelvis over the feet (+y crouches)
    C_LEAN,             // the spine's angle from upright, radians, + forward
    C_CHEST,            // extra bend at the chest, radians
    C_HEAD,             // the head's tilt, radians, + bows
    C_HFX, C_HFY,       // the front (weapon) hand, relative to its shoulder
    C_HBX, C_HBY,       // the back hand, relative to its shoulder
    C_FFX, C_FFY,       // the front foot's x and its lift off the ground
    C_FBX, C_FBY,       // the back foot
    C_WEAPON,           // the weapon's angle, degrees, 0 = straight ahead, - up
    C_ARMZ,             // + brings the back arm in front of the torso (a cross-body swing)
    C_COUNT
};
struct RPose {
    float v[C_COUNT] = {};
    float& operator[](int i) { return v[i]; }
    float operator[](int i) const { return v[i]; }
    RPose& operator+=(const RPose& o) { for (int i = 0; i < C_COUNT; i++) v[i] += o.v[i]; return *this; }
};
RPose Scaled(const RPose& p, float k);
RPose Mix(const RPose& a, const RPose& b, float k);

// ---------------------------------------------------------------- clips
enum Ease { LINEAR, SMOOTH, ANTICIPATE /* dips back before it goes */, SNAP /* fast out */, OVERSHOOT /* past, then back */, SETTLE /* a damped wobble into place */ };
float EaseFn(Ease e, float u);
struct Key { float t, v; Ease e = SMOOTH; };
struct Track { int ch; std::vector<Key> keys; };
struct Clip {
    const char* name = "";
    float dur = 1;
    bool loop = false;
    std::vector<Track> tracks;
    RPose Sample(float t) const;   // the channels' offsets at time t (tracks it doesn't key stay 0)
};
// the shared clip set every figure can play; built once
enum ClipId { CL_IDLE, CL_BREATHE, CL_STRESSED, CL_DEATHSDOOR, CL_WALK, CL_GUARD, CL_SLASH, CL_THRUST, CL_SWING, CL_SHOOT, CL_THROW, CL_CAST, CL_HEAL, CL_SONG,
              CL_HIT, CL_DODGE, CL_STAGGER, CL_CRIT, CL_DEATH, CL_VICTORY, CL_COUNT };
const Clip& GetClip(int id);
const char* ClipName(int id);

// ---------------------------------------------------------------- the build of a body
struct Build {
    float thigh = 42, shin = 40;          // leg bones
    float upper = 27, fore = 25;          // arm bones
    float spine = 26, chest = 24, neck = 8, head = 12;
    float shoulderW = 14, hipW = 5;       // half-widths
    float stanceF = 9, stanceB = -9;      // where the feet stand at rest
    float bulk = 1;
};

struct Solved {
    Vector2 p[BONE_COUNT];     // bone origins on the canvas
    float a[BONE_COUNT];       // bone directions (radians, screen space)
    float f = 1, s = 1;        // facing (+1 right) and scale
    float armZ = 0;
    // a point written in the chest's frame (facing right, y down), placed on the canvas
    Vector2 Chest(float x, float y) const;
    Vector2 Head(float x, float y) const;
    Vector2 Hips(float x, float y) const;
    Vector2 Along(int bone, int child, float u, float side) const; // along a limb, offset sideways (+ toward the front)
};
Solved SolveHumanoid(const Build& b, const RPose& p, Vector2 feet, float s, float facing);

// ---------------------------------------------------------------- chains (verlet)
struct Chain {
    std::vector<Vector2> p, q;   // positions and previous positions
    float seg = 6, damp = 0.94f, grav = 520, stiff = 0.12f, width0 = 3, width1 = 1;
    Color col{60, 50, 44, 255};
    Mat mat = CLOTH;
    bool live = false;
    void Init(Vector2 anchor, int n, float segLen, Vector2 dir);
    // anchor: where it hangs from; rest: the direction it hangs toward when still (unit); current: the scene's water
    void Step(Vector2 anchor, Vector2 rest, float dt, Vector2 current);
    void Shift(Vector2 d);        // the canvas moved under it: particles keep their place in the world, so they trail
    void Kick(Vector2 v);         // a hit or a sudden move: a velocity impulse down the chain
    void Draw(float s) const;
    Vector2 Tip() const { return p.empty() ? Vector2{0, 0} : p.back(); }
};

// ---------------------------------------------------------------- the face
struct Face {
    float next = 3, shut = 0;     // time to the next blink, and how long the lids are still down
    Vector2 look{1, 0};           // where the eyes point (unit, facing space)
    int mouth = 0;                // 0 set, 1 open (a shout, a crit), 2 grimace (pain)
    void Update(float dt, unsigned seed);
    bool Closed() const { return shut > 0; }
};
// two eyes under the brow: whites, pupils that follow `look`, lids that shut on a blink
void DrawEyes(const Face& fc, Vector2 c, float spacing, float r, float s, float f, Color lid, Color iris);
void DrawMouth(const Face& fc, Vector2 c, float w, float s, float f, Color lip);

// ---------------------------------------------------------------- per-figure state
struct Instance {
    bool init = false;
    float lastT = 0, dt = 0;
    Vector2 lastOff{0, 0};
    Face face;
    std::vector<Chain> chains;
    int reaction = -1; float reactT = 0; // a reaction clip playing over everything (hit, dodge, stagger, death ...)
    unsigned seed = 1;
};
Instance& Get(int key);          // the state for one figure (a hero's id, an enemy's uid + 1e6 ...)
void Tick(Instance& in, float t); // advance its clocks from the scene time
void React(int key, int clip);    // start a reaction clip on a figure

// ---------------------------------------------------------------- depth-sorted parts
struct Parts {
    struct P { float z; int order; std::function<void()> fn; };
    std::vector<P> list;
    void Add(float z, std::function<void()> fn) { list.push_back({z, (int)list.size(), std::move(fn)}); }
    void Draw();
};

// ---------------------------------------------------------------- the world the figures live in
void SetWorldOffset(Vector2 off);  // canvas -> screen offset of the figure being drawn (so chains trail real motion)
Vector2 WorldOffset();
void SetCurrent(Vector2 c);        // the scene's current: chains drift with it
Vector2 Current();

// Map the combat system's Pose (lean, crouch, reach, raise ...) onto rig channels for a humanoid: the old pose
// fields become blends toward key poses (overhead, thrust, crouch ...), so every combat animation drives the rig.
RPose FromPose(const ::Pose& p, float walk, float t, int style);

}  // namespace rig
