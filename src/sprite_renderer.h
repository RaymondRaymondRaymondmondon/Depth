// ============================================================================
//  DEPTH - the high-resolution art pipeline: textures, sprites, skeletal characters, enemies and parallax backgrounds.
//
//  Until now every image in the game was drawn in code. This layer lets the art team drop painted, high-resolution files
//  into an `assets/` folder next to depth.exe and have them used instead, with no primitive shapes, no tinting and no rust or
//  noise shader on top: art is drawn exactly as painted (white tint), with precise alpha blending and per-joint pivots.
//  Where a file or folder is missing the game keeps using its built-in procedural drawing, so nothing breaks while art arrives.
//
//  WHERE THE ART GOES (paths are relative to assets/):
//    characters/<class>/skeleton.txt      one skeleton per expedition class (nurse, diver, captain, mechanic, ...):
//                                         bones, slots and attachments (see skeletal_node.h for the format and the Spine2D mapping).
//                                         Attachment images sit beside it: characters/diver/head.png, arm_upper.png, weapon.png ...
//    enemies/<name>/layers.txt            one file per creature (ghost_worm, lobster, cthulhu ...): a list of layered sprites
//                                         back to front, each with its own offset, scale, tint and blend mode; or a skeleton.txt
//                                         for creatures that need to writhe. Atlases up to MAX_TEXTURE_DIM square are supported.
//    backgrounds/<region>/layers.txt      the parallax layers of each region (cave, island, weeds, atlantis): one line per layer.
// ============================================================================
#pragma once
#include "raylib.h"
#include "skeletal_node.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace art {

constexpr int MAX_TEXTURE_DIM = 8192;          // boss atlases are big: allow up to 8192 x 8192 (raise if the GPU allows)
const char* const ASSET_ROOT = "assets/";

// ---------------------------------------------------------------- textures
// Loads each file once, with mipmaps and bilinear filtering (painted art must scale smoothly), and keeps it alive.
class TextureManager {
public:
    static TextureManager& Get();
    const Texture2D* Load(const std::string& relPath);   // nullptr if the file does not exist or is too large
    bool Exists(const std::string& relPath);              // cached FileExists on assets/<relPath>
    void UnloadAll();
    size_t BytesInUse() const { return bytes_; }
private:
    std::map<std::string, std::unique_ptr<Texture2D>> cache_;
    std::map<std::string, bool> exists_;
    size_t bytes_ = 0;
};

// ---------------------------------------------------------------- one sprite
struct SpriteDesc {
    std::string path;
    float pivotX = 0.5f, pivotY = 0.5f;   // the point of the image that sits on the origin (0..1)
    float scale = 1;
    Color tint = WHITE;                   // white = as painted
    skel::BlendKind blend = skel::BlendKind::Alpha;
};

class SpriteRenderer {
public:
    // Draws `tex` with its pivot at the world transform's origin. Rotation and scale come from the matrix, so a limb drawn
    // at its bone's transform pivots about the joint. `flipX` mirrors about the pivot (characters facing left).
    static void Draw(const Texture2D& tex, const skel::Mat2D& world, float pivotX, float pivotY, float scale, Color tint, skel::BlendKind blend, bool flipX = false);
    static void Draw(const SpriteDesc& s, const skel::Mat2D& world, bool flipX = false);
};

// ---------------------------------------------------------------- characters
// A skeletal character: the explorers. Bones and slots come from skeleton.txt; each slot's attachment is a high-res image.
class CharacterRenderer {
public:
    // Loads assets/characters/<folder>/skeleton.txt. Returns false (and the caller falls back to the procedural figure) if absent.
    bool Load(const std::string& folder);
    bool Ready() const { return ready_; }
    // `feet` is where the root bone stands; `scale` converts art pixels to screen pixels. `anim` is an animation name in the
    // skeleton ("idle", "walk", "attack"...); if it is missing the setup pose is drawn.
    void Draw(Vector2 feet, float scale, bool facingRight, const std::string& anim, float time, Color tint = WHITE);
    skel::Skeleton& Rig() { return rig_; }
    static bool HasAssets(const std::string& folder);
private:
    skel::Skeleton rig_;
    bool ready_ = false;
};

// ---------------------------------------------------------------- enemies
struct EnemyLayer {
    SpriteDesc sprite;
    float offsetX = 0, offsetY = 0;       // from the enemy's feet, in art pixels (y up is negative)
    float wobble = 0;                     // idle sway in pixels: layers that sway at different rates read as a living body
    float wobbleRate = 1;
    bool glow = false;                    // drawn additively over the body: eyes, ethereal light, bioluminescence
};

// A creature made of layered sprites (and optionally a skeleton for writhing bodies). Collision is derived from the art.
class EnemyRenderer {
public:
    // Loads assets/enemies/<name>/layers.txt: `layer path offsetX offsetY scale pivotX pivotY blend(alpha|add|mul) wobble rate`
    // and an optional `hull x y w h` line: the collision box in art pixels relative to the feet (otherwise it is the union of
    // the layers' opaque bounds).
    bool Load(const std::string& name);
    bool Ready() const { return ready_; }
    void Draw(Vector2 feet, float scale, float time, Color tint = WHITE);
    // The physics / hit box for this creature at `feet` and `scale`, following the new high-res dimensions instead of the old
    // geometric primitives: call this whenever the enemy moves or its scale changes.
    Rectangle UpdateEnemyCollisionMesh(Vector2 feet, float scale) const;
    static bool HasAssets(const std::string& name);
private:
    std::vector<EnemyLayer> layers_;
    Rectangle hull_{0, 0, 0, 0};          // in art pixels relative to the feet
    bool hasHull_ = false, ready_ = false;
};

// ---------------------------------------------------------------- parallax backgrounds
// One plane of the scene. Either a painted texture, or (until the art exists) a draw callback. `factor` is how far the layer
// moves for one unit of camera movement: 0 is fixed to the screen (the far sky), 1 moves with the world, above 1 is a foreground
// that sweeps past faster than the camera.
struct BackgroundLayer {
    std::string name;
    std::string path;                                      // painted texture, relative to assets/ (may be empty)
    float factorX = 0.5f, factorY = 0.1f;
    float scale = 1;
    float yAnchor = 0;                                     // where the layer's top edge sits on screen, in pixels
    bool repeatX = true;                                   // tile horizontally forever (a wide painted band, not a tiny pattern)
    Color tint = WHITE;
    skel::BlendKind blend = skel::BlendKind::Alpha;
    std::function<void(float scrollX, float scrollY)> draw;   // fallback / procedural: receives this layer's scroll offset
    enum Plane { Distant, Mid, Foreground } plane = Mid;
};

class ParallaxBackgroundManager {
public:
    void Clear() { layers_.clear(); }
    void Add(const BackgroundLayer& l) { layers_.push_back(l); }
    // Reads assets/backgrounds/<region>/layers.txt: `layer path factorX factorY scale yAnchor repeat plane(distant|mid|fore)`.
    // Returns false if the region has no painted layers (the caller keeps its procedural background).
    bool LoadRegion(const std::string& region);
    static bool HasAssets(const std::string& region);
    // Draws every layer of the given plane, each scrolled by camera * its own factor. Call Distant, then the world, then
    // Mid, Foreground where each belongs in the frame.
    void Draw(BackgroundLayer::Plane plane, float camX, float camY, int screenW, int screenH) const;
    // The scroll offset a layer with this factor sees, for a camera at (camX, camY): the one place the parallax maths lives.
    static Vector2 Scroll(float camX, float camY, float factorX, float factorY) { return {-camX * factorX, -camY * factorY}; }
    bool Empty() const { return layers_.empty(); }
private:
    std::vector<BackgroundLayer> layers_;
};

}  // namespace art
