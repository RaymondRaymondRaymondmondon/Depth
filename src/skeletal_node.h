// ============================================================================
//  DEPTH - a small 2D skeletal animation core: bones, slots, attachments, animations.
//
//  This is the structure a Spine2D / DragonBones export maps onto. A Skeleton is a tree of Bones (each with a local
//  transform relative to its parent); Slots sit on bones and hold an Attachment (a textured region drawn at the bone's
//  world transform); an Animation is a set of timelines that set bone transforms over time. Child bones inherit
//  position, rotation and scale from their parent (the world matrix is the parent's world matrix times the local one),
//  so a hand on a sword rides the arm, which rides the shoulder, which rides the torso.
//
//  Nothing here draws or loads: sprite_renderer.h does that. Importing an exported skeleton is a matter of filling these
//  structs (see Skeleton::LoadText for the plain-text format used by the game, and the note in it about JSON).
// ============================================================================
#pragma once
#include <cmath>
#include <string>
#include <vector>

namespace skel {

// A 2D affine transform, stored as the matrix [a c tx; b d ty]. Composition order: (parent * child).
struct Mat2D {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    static Mat2D FromTRS(float x, float y, float rotDeg, float sx, float sy) {
        float r = rotDeg * 0.01745329252f, cs = std::cos(r), sn = std::sin(r);
        Mat2D m;
        m.a = cs * sx; m.b = sn * sx; m.c = -sn * sy; m.d = cs * sy; m.tx = x; m.ty = y;
        return m;
    }
    Mat2D operator*(const Mat2D& o) const { // this applied after o
        Mat2D m;
        m.a = a * o.a + c * o.b;  m.b = b * o.a + d * o.b;
        m.c = a * o.c + c * o.d;  m.d = b * o.c + d * o.d;
        m.tx = a * o.tx + c * o.ty + tx;  m.ty = b * o.tx + d * o.ty + ty;
        return m;
    }
    void Apply(float x, float y, float& ox, float& oy) const { ox = a * x + c * y + tx; oy = b * x + d * y + ty; }
    float Rotation() const { return std::atan2(b, a) * 57.29577951f; }
    float ScaleX() const { return std::sqrt(a * a + b * b); }
    float ScaleY() const { return std::sqrt(c * c + d * d); }
};

struct Transform {                // a bone's pose relative to its parent
    float x = 0, y = 0, rotation = 0, scaleX = 1, scaleY = 1;
};

struct Bone {
    std::string name;
    int parent = -1;              // index into Skeleton::bones, -1 for the root
    float length = 0;
    Transform setup;              // the bind pose
    Transform pose;               // the current pose (animations write here)
    Mat2D world;                  // computed by Skeleton::UpdateWorld
};

// What a slot shows. `path` is the art team's file (relative to the game's assets folder); the renderer resolves it through the
// TextureManager. pivot is where the bone's origin sits inside the image (0..1, 0.5/0.5 is the centre), so joints line up.
struct Attachment {
    std::string name;
    std::string path;             // e.g. "characters/diver/arm_upper.png"
    float pivotX = 0.5f, pivotY = 0.5f;
    float offsetX = 0, offsetY = 0, rotation = 0;   // placement relative to the bone
    float scale = 1;              // texture scale (a 2048 px painting shown at 1/8 size is 0.125)
    float width = 0, height = 0;  // filled in when the texture loads
};

enum class BlendKind { Alpha, Additive, Multiply };

struct Slot {
    std::string name;
    int bone = 0;
    std::vector<Attachment> attachments;   // the active one is `active`; skins/expressions swap it
    int active = 0;
    unsigned char r = 255, g = 255, b = 255, a = 255;   // tint: white means the art is drawn exactly as painted
    BlendKind blend = BlendKind::Alpha;    // Additive for ethereal glows, Multiply for baked shadow layers
    bool visible = true;
};

struct Key { float time; float value; };
struct Timeline {                 // one property of one bone over time
    int bone = 0;
    enum Prop { Rotation, X, Y, ScaleX, ScaleY } prop = Rotation;
    std::vector<Key> keys;        // sorted by time; linear interpolation between them
    float Sample(float t) const;
};
struct Animation {
    std::string name;
    float duration = 1;
    bool loop = true;
    std::vector<Timeline> timelines;
};

class Skeleton {
public:
    std::vector<Bone> bones;
    std::vector<Slot> slots;      // drawn in order: back to front
    std::vector<Animation> animations;

    int FindBone(const std::string& n) const { for (size_t i = 0; i < bones.size(); i++) if (bones[i].name == n) return (int)i; return -1; }
    int FindSlot(const std::string& n) const { for (size_t i = 0; i < slots.size(); i++) if (slots[i].name == n) return (int)i; return -1; }
    int FindAnimation(const std::string& n) const { for (size_t i = 0; i < animations.size(); i++) if (animations[i].name == n) return (int)i; return -1; }
    void SetToSetupPose() { for (auto& b : bones) b.pose = b.setup; }
    void Apply(const Animation& a, float t, float mix = 1.0f);   // write timeline values into the pose (mix blends with what is there)
    void UpdateWorld(const Mat2D& root);                          // bones are stored parents-first, so one pass suffices
    // The plain-text format: lines of `bone name parent x y rot sx sy len`, `slot name bone`, `attach slot name path px py scale`,
    // `anim name duration`, `key bone prop time value`. Returns false if the file is missing or malformed. A Spine2D JSON export
    // maps onto exactly these fields: parse "bones", "slots", "skins" and "animations" into the same structs.
    bool LoadText(const std::string& file);
};

}  // namespace skel
