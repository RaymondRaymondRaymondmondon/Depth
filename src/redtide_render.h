#pragma once
// ============================================================================
//  Red Tide's 3D core (design doc, "Presentation and technology"): first-person 3D in raylib, extending the Abyss's
//  pipeline, with every mesh generated in code. The look is inked low-poly: flat-shaded meshes (faceted by
//  screen-space derivatives, so animated bodies stay faceted), a screen-space ink outline from depth and normal
//  edges with line weight by distance, Bayer stipple in shadow, the salon's paper tones, fog by depth, caustics
//  near the surface, and marine snow.
//
//  Two geometry passes per frame: colour (lit, fogged) and normal/depth (for the ink edges); an ink composite
//  shader lays them into the scene.
// ============================================================================
#include "raylib.h"
#include "redtide.h"
#include <functional>
#include <string>
#include <vector>

namespace rt {

// ---------------------------------------------------------------- meshes
// Vertex texcoords carry animation data for the swim shader: u = position along the body (0 head .. 1 tail, or along
// a limb), v = part code (0 body, 0.25 fin, 0.5+ limb/arm/tentacle with its index in the fraction).
struct MeshBuilder {
    std::vector<float> pos, uv;
    std::vector<unsigned char> col;
    void Tri(Vector3 a, Vector3 b, Vector3 c, Color ca, Color cb, Color cc, Vector2 ta, Vector2 tb, Vector2 tc);
    void Tri(Vector3 a, Vector3 b, Vector3 c, Color col, Vector2 t = {0, 0}) { Tri(a, b, c, col, col, col, t, t, t); }
    void Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col, Vector2 t = {0, 0});
    // A lathed body along +Z (head at +len/2): ring k of `rings` at u = k/rings has half-sizes (rx(u), ry(u));
    // colours from `colorAt(u, angle)`. Segments around each ring.
    void Lathe(float len, int rings, int segs, const std::function<float(float)>& rx, const std::function<float(float)>& ry,
               Color top, Color belly, Vector3 offset = {0, 0, 0}, float vCode = 0, float u0 = 0, float u1 = 1);
    // A tapered tube along a polyline (legs, arms, tentacles, eel bodies); u runs 0..1 along it.
    void Tube(const std::vector<Vector3>& pts, float r0, float r1, int segs, Color c0, Color c1, float vCode, float u0 = 0, float u1 = 1);
    void Octa(Vector3 c, float r, Color col, float vCode = 0);                        // an eye, a bead
    void Box(Vector3 c, Vector3 half, Color col, float vCode = 0);
    void Cone(Vector3 base, Vector3 tip, float r, int segs, Color col, float vCode = 0);
    int Tris() const { return (int)(pos.size() / 9); }
    Mesh Build() const;                                                              // uploads to the GPU
};

// ---------------------------------------------------------------- creatures
enum class AnimMode : int { FishLateral = 0, CetaceanVertical = 1, RayWing = 2, EelFull = 3, JellyPulse = 4, Walker = 5, Cephalopod = 6, Static = 7 };
struct CreatureModel {
    std::string species, plan;
    Model model{};
    bool ready = false;
    float length = 1;              // metres
    float radius = 0.2f;           // for hits and the silhouette check
    float extent = 0;              // the model's largest half-dimension, metres (fins, arms and colonies included)
    AnimMode anim = AnimMode::FishLateral;
    float waves = 1.2f, amp = 0.08f, freq = 6;  // traveling sine: wavelengths along the body, amplitude (x length), Hz scale
    Color base{}, belly{}, accent{};
    bool luminous = false;
    Vector3 weakPoint{0, 0, 0};    // model-space position of the weak point (gills, eyes, underside)
};
// Builds (and caches) the model for a species on a map, from the art workbook's per-species row and body plan.
const CreatureModel& Creature(const std::string& mapKey, const std::string& speciesName);
void UnloadCreatures();

// ---------------------------------------------------------------- the renderer
struct SceneLight {                // the master reference's three-light rig
    Vector3 lampPos{}, lampDir{0, 0, 1};
    float lampRange = 22, lampCone = 0.8f;   // the diver's helmet lamp: the key light
    Color key{255, 238, 200, 255};
    Color fill{40, 90, 110, 255};            // the sea's cool fill
    Color rim{120, 190, 210, 255};
    Color fog{8, 24, 30, 255};
    float fogDensity = 0.045f;
    float surfaceY = 40;                     // caustics fade in near this height
    float time = 0;
    float bloodTint = 0;                     // 0..1: the red at the mask's edge (the scent meter you can see)
    float silhouette = 0;                    // 1: render everything solid black (the --silhouette check)
    // point lights besides the key (the Trawl's deck lamps, fires and flares); Red Tide leaves these empty
    static constexpr int MAX_POINTS = 8;
    struct Point { Vector3 p; float r; Color c; float k; };
    Point points[MAX_POINTS];
    int nPoints = 0;
    bool AddPoint(Vector3 p, float r, Color c, float k) { if (nPoints >= MAX_POINTS) return false; points[nPoints++] = {p, r, c, k}; return true; }
};

void RenderBegin(const Camera3D& cam, const SceneLight& light);   // opens the colour pass
// Draws a creature: world transform (position, facing yaw/pitch), scale, animation phase and intensity (0..1).
void DrawCreature(const CreatureModel& cm, Vector3 pos, float yaw, float pitch, float scale, float phase, float intensity, Color tint = WHITE);
void DrawStatic(const Model& m, Matrix world, Color tint = WHITE);  // level geometry with the same lighting
void DrawCubeM(Matrix world, Color col);                            // a unit cube under any transform (the diver's glove)
void DrawCubeGlow(Matrix world, Color col, float glow);             // the same, lit from within (lamps, fires, flares)
void DrawStaticGlow(const Model& m, Matrix world, Color tint, float glow);
void DrawWorldCube(Vector3 c, Vector3 size, Color col);             // blockout boxes (walls, floors, props)
void RenderEnd();                                                   // runs the normal/depth pass and the ink composite into the scene
void RenderShutdown();
bool RenderReady();

// The geometry drawn this frame is queued so the normal/depth pass can repeat it. Anything drawn between
// RenderBegin and RenderEnd through the functions above is recorded automatically.

} // namespace rt
