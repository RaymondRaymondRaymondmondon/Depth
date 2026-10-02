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
#include "raymath.h"
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
    // the physically based path (DrawPbr; the Trawl's visual overhaul): a directional moon, a hemisphere ambient
    Vector3 moonDir{-0.4f, -0.8f, 0.3f};     // the way the moonlight travels
    Color moon{150, 170, 210, 255}; float moonK = 0;
    Color skyAmb{30, 40, 60, 255}, seaAmb{6, 10, 16, 255}; float ambK = 0;
    // the ink composite: outline weight (1 the full ink, 0 none), its tint, and the stipple and paper grain (1 on)
    float outline = 1, stipple = 1, grain = 1;
    Color outlineTint{13, 13, 18, 255};
    // the Trawl's lit look (Visual Overhaul phase 2; Red Tide leaves these off): screen-space ambient occlusion from
    // the depth and normal pass (contact shadows), a filmic curve with a split-tone grade, and the lantern's shadow
    float aoK = 0, aoRadius = 0.45f;         // occlusion strength (0 off) and reach in metres
    float filmic = 0, exposure = 1;          // 0 the old straight colour, 1 the filmic curve
    Color gradeLo{0, 0, 0, 255}, gradeHi{255, 255, 255, 255};   // shadows pulled toward gradeLo, highlights toward gradeHi
    float gradeK = 0, saturation = 1;
    bool keyShadow = false;                  // the lamp (the key light) casts shadows (a shadow map from lampPos along lampDir)
    float keyShadowFov = 120;                // its frustum, degrees
};

void RenderBegin(const Camera3D& cam, const SceneLight& light);   // opens the colour pass
// Draws a creature: world transform (position, facing yaw/pitch), scale, animation phase and intensity (0..1).
void DrawCreature(const CreatureModel& cm, Vector3 pos, float yaw, float pitch, float scale, float phase, float intensity, Color tint = WHITE);
void DrawStatic(const Model& m, Matrix world, Color tint = WHITE);  // level geometry with the same lighting
void DrawCubeM(Matrix world, Color col);                            // a unit cube under any transform (the diver's glove)
void DrawCubeGlow(Matrix world, Color col, float glow);             // the same, lit from within (lamps, fires, flares)
void DrawStaticGlow(const Model& m, Matrix world, Color tint, float glow);
void DrawSky(const Model& m, Matrix world, Color tint);           // unlit, unfogged, no ink edges (stars, the moon, rain)
void DrawWorldCube(Vector3 c, Vector3 size, Color col);             // blockout boxes (walls, floors, props)
// The physically based path: a glTF model (base colour, metallic-roughness, normal, occlusion and emission maps, as
// the art generators bake them into assets/) under the same lamp, points and fog, plus the moon and the ambient.
const Model* LoadAsset(const std::string& relPath);                 // assets/<relPath>, cached; nullptr if missing
void DrawPbr(const Model& m, Matrix world, Color tint = WHITE, float wrap = 0);   // wrap: soft wrap-diffuse (skin, cloth)

// Assets with moving parts (the gun kit, tools/artgen/gunkit.py): one mesh per part, in the glb's node order; each
// part's pivot (its node's position), its group (hammer, trigger, cylinder, lever, bolt, break, load...), axis, travel
// and the group it rides on; and the markers (grip_r, grip_l, muzzle, eject, sight, mount_*), all in model space.
struct AssetPart { std::string name, group, kind, parent; Vector3 pivot{0, 0, 0}, axis{0, 0, 1}; float amount = 0; };
struct AssetMarker { Vector3 p{0, 0, 0}, dir{1, 0, 0}; };
struct AssetInfo { std::vector<AssetPart> parts; std::vector<std::pair<std::string, AssetMarker>> markers; const AssetMarker* Marker(const char* n) const { for (const auto& m : markers) if (m.first == n) return &m.second; return nullptr; } };
const AssetInfo* AssetInfoOf(const Model* m);          // nullptr unless the model came from LoadAsset(.glb)
// Draws a model with each mesh under its own local transform first (partLocal[i], model space; missing = identity)
void DrawPbrParts(const Model& m, Matrix world, const std::vector<Matrix>& partLocal, Color tint = WHITE, float glow = 0);
// a light added after RenderBegin (a muzzle flash found only once the gun is placed); dropped if the eight are taken
void AddLateLight(Vector3 p, float r, Color c, float k);

// Skinned characters (the shared rig, tools/artgen/crew.py). A pose is a model-space rotation (and scale) per bone
// about its bind joint, applied down the chain: so "swing the right arm forward" is a rotation about the model's
// lateral axis on upperarm.R, whatever the bone's own axes. SolveRig turns it into the skinning matrices.
struct RigInfo {
    std::vector<int> parent;                 // per bone, -1 at the root
    std::vector<Vector3> joint;              // bind joint positions, model space
    std::vector<std::string> name;
    int Find(const std::string& n) const { for (size_t i = 0; i < name.size(); i++) if (name[i] == n) return (int)i; return -1; }
};
const RigInfo& RigOf(const Model& m);
struct RigPose {
    std::vector<Quaternion> rot;             // per bone, model axes, relative to its parent's posed frame
    std::vector<Vector3> scale;              // per bone (1 = as built): builds, head shapes, a blink
    Vector3 offset{0, 0, 0};                 // the whole figure (a crouch, a bob)
    void Reset(int n) { rot.assign(n, QuaternionIdentity()); scale.assign(n, {1, 1, 1}); offset = {0, 0, 0}; }
};
std::vector<Matrix> SolveRig(const RigInfo& rig, const RigPose& pose);   // skinning matrices (bind model space -> posed)
Matrix BoneWorld(const RigInfo& rig, const std::vector<Matrix>& skin, int bone, Matrix world);   // a joint's frame in the world (to hold things)
// Draws a skinned model in a pose, with named materials recoloured (a sailor's skin tone, coat, hat)
struct Recolor { const char* material; Color c; };
void DrawPbrSkinned(const Model& m, Matrix world, const std::vector<Matrix>& skin, const std::vector<Recolor>& recolor = {}, float wrap = 0.35f, Color tint = WHITE);
void RenderEnd();                                                   // runs the normal/depth pass and the ink composite into the scene
void RenderShutdown();
bool RenderReady();

// The geometry drawn this frame is queued so the normal/depth pass can repeat it. Anything drawn between
// RenderBegin and RenderEnd through the functions above is recorded automatically.

} // namespace rt
