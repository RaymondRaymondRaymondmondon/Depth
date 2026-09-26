#include "sprite_renderer.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace art {
using skel::BlendKind;
using skel::Mat2D;

// ---------------------------------------------------------------- textures
TextureManager& TextureManager::Get() { static TextureManager m; return m; }

bool TextureManager::Exists(const std::string& rel) {
    auto it = exists_.find(rel);
    if (it != exists_.end()) return it->second;
    bool e = FileExists((std::string(ASSET_ROOT) + rel).c_str());
    exists_[rel] = e;
    return e;
}

const Texture2D* TextureManager::Load(const std::string& rel) {
    auto it = cache_.find(rel);
    if (it != cache_.end()) return it->second.get();
    if (!Exists(rel)) return nullptr;
    Image img = LoadImage((std::string(ASSET_ROOT) + rel).c_str());
    if (img.data == nullptr || img.width > MAX_TEXTURE_DIM || img.height > MAX_TEXTURE_DIM) { if (img.data) UnloadImage(img); return nullptr; }
    ImageAlphaPremultiply(&img);          // premultiplied alpha keeps heavy ink outlines clean when the art is scaled or blended
    auto tex = std::make_unique<Texture2D>(LoadTextureFromImage(img));
    bytes_ += (size_t)img.width * img.height * 4;
    UnloadImage(img);
    GenTextureMipmaps(tex.get());
    SetTextureFilter(*tex, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(*tex, TEXTURE_WRAP_CLAMP);
    const Texture2D* out = tex.get();
    cache_[rel] = std::move(tex);
    return out;
}

void TextureManager::UnloadAll() {
    for (auto& kv : cache_) UnloadTexture(*kv.second);
    cache_.clear(); exists_.clear(); bytes_ = 0;
}

// ---------------------------------------------------------------- sprites
void SpriteRenderer::Draw(const Texture2D& tex, const Mat2D& m, float pivotX, float pivotY, float scale, Color tint, BlendKind blend, bool flipX) {
    float w = tex.width * scale, h = tex.height * scale;
    float px = pivotX * w, py = pivotY * h;
    float lx[4] = {-px, w - px, w - px, -px}, ly[4] = {-py, -py, h - py, h - py};
    float u[4] = {0, 1, 1, 0}, v[4] = {0, 0, 1, 1};
    if (flipX) { std::swap(u[0], u[1]); std::swap(u[2], u[3]); }
    switch (blend) {
        case BlendKind::Additive: BeginBlendMode(BLEND_ADDITIVE); break;
        case BlendKind::Multiply: BeginBlendMode(BLEND_MULTIPLIED); break;
        default: BeginBlendMode(BLEND_ALPHA_PREMULTIPLY); break;   // the textures are premultiplied on load
    }
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    rlNormal3f(0, 0, 1);
    static const int order[4] = {0, 3, 2, 1};   // counter-clockwise, as raylib expects (its default culling would drop the quad otherwise)
    for (int k = 0; k < 4; k++) {
        int i = order[k];
        float ox, oy;
        m.Apply(lx[i], ly[i], ox, oy);
        rlTexCoord2f(u[i], v[i]);
        rlVertex2f(ox, oy);
    }
    rlEnd();
    rlSetTexture(0);
    EndBlendMode();
}

void SpriteRenderer::Draw(const SpriteDesc& s, const Mat2D& m, bool flipX) {
    const Texture2D* t = TextureManager::Get().Load(s.path);
    if (t) Draw(*t, m, s.pivotX, s.pivotY, s.scale, s.tint, s.blend, flipX);
}

// ---------------------------------------------------------------- characters
bool CharacterRenderer::HasAssets(const std::string& folder) { return TextureManager::Get().Exists("characters/" + folder + "/skeleton.txt"); }

bool CharacterRenderer::Load(const std::string& folder) {
    ready_ = HasAssets(folder) && rig_.LoadText(std::string(ASSET_ROOT) + "characters/" + folder + "/skeleton.txt");
    if (ready_) {
        // ART HOOK: each attachment's `path` is resolved against assets/ (so "characters/diver/head.png"); the images'
        // sizes come from the files themselves, so any resolution works, and `scale` in the skeleton file sets how large
        // the painting appears in the scene (a 1024 px torso at scale 0.2 is about 200 px tall on screen).
        for (auto& sl : rig_.slots)
            for (auto& a : sl.attachments) { const Texture2D* t = TextureManager::Get().Load(a.path); if (t) { a.width = (float)t->width; a.height = (float)t->height; } }
    }
    return ready_;
}

void CharacterRenderer::Draw(Vector2 feet, float scale, bool facingRight, const std::string& anim, float time, Color tint) {
    if (!ready_) return;
    rig_.SetToSetupPose();
    int ai = rig_.FindAnimation(anim);
    if (ai >= 0) rig_.Apply(rig_.animations[ai], time);
    float f = facingRight ? 1.0f : -1.0f;
    Mat2D root = Mat2D::FromTRS(feet.x, feet.y, 0, scale * f, scale);   // a mirrored root flips the whole rig, joints and all
    rig_.UpdateWorld(root);
    for (const skel::Slot& sl : rig_.slots) {
        if (!sl.visible || sl.attachments.empty()) continue;
        const skel::Attachment& at = sl.attachments[std::clamp(sl.active, 0, (int)sl.attachments.size() - 1)];
        const Texture2D* tex = TextureManager::Get().Load(at.path);
        if (!tex) continue;
        Mat2D place = rig_.bones[sl.bone].world * Mat2D::FromTRS(at.offsetX, at.offsetY, at.rotation, 1, 1);
        Color c{(unsigned char)(sl.r * tint.r / 255), (unsigned char)(sl.g * tint.g / 255), (unsigned char)(sl.b * tint.b / 255), (unsigned char)(sl.a * tint.a / 255)};
        SpriteRenderer::Draw(*tex, place, at.pivotX, at.pivotY, at.scale, c, sl.blend, false);
    }
}

// ---------------------------------------------------------------- enemies
bool EnemyRenderer::HasAssets(const std::string& name) { return TextureManager::Get().Exists("enemies/" + name + "/layers.txt"); }

bool EnemyRenderer::Load(const std::string& name) {
    layers_.clear(); hasHull_ = false; ready_ = false;
    std::ifstream in(std::string(ASSET_ROOT) + "enemies/" + name + "/layers.txt");
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string kw;
        ss >> kw;
        if (kw == "layer") {
            EnemyLayer l; std::string blend;
            ss >> l.sprite.path >> l.offsetX >> l.offsetY >> l.sprite.scale >> l.sprite.pivotX >> l.sprite.pivotY >> blend >> l.wobble >> l.wobbleRate;
            l.sprite.blend = blend == "add" ? BlendKind::Additive : blend == "mul" ? BlendKind::Multiply : BlendKind::Alpha;
            l.glow = blend == "add";
            // ART HOOK: `path` is relative to assets/, e.g. enemies/ghost_worm/body.png. List layers back to front: a shadow
            // (mul), the body, a wet-sheen highlight, then glowing eyes (add). Offsets are in art pixels from the feet.
            layers_.push_back(l);
        } else if (kw == "hull") {
            ss >> hull_.x >> hull_.y >> hull_.width >> hull_.height;
            hasHull_ = true;
        }
    }
    ready_ = !layers_.empty();
    return ready_;
}

void EnemyRenderer::Draw(Vector2 feet, float scale, float time, Color tint) {
    for (const EnemyLayer& l : layers_) {
        float sway = l.wobble * std::sin(time * l.wobbleRate * 2.0f + l.offsetX * 0.05f);
        Mat2D m = Mat2D::FromTRS(feet.x + (l.offsetX + sway) * scale, feet.y + l.offsetY * scale, 0, scale, scale);
        SpriteDesc s = l.sprite;
        s.tint = tint;
        SpriteRenderer::Draw(s, m);
    }
}

Rectangle EnemyRenderer::UpdateEnemyCollisionMesh(Vector2 feet, float scale) const {
    if (hasHull_) return {feet.x + hull_.x * scale, feet.y + hull_.y * scale, hull_.width * scale, hull_.height * scale};
    // No explicit hull: the union of the layers' (non-glow) image rectangles, shrunk a little so that empty painted margins,
    // tentacle tips and wisps do not hit the player.
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const EnemyLayer& l : layers_) {
        if (l.glow || l.sprite.blend == BlendKind::Multiply) continue;
        const Texture2D* t = TextureManager::Get().Load(l.sprite.path);
        if (!t) continue;
        float w = t->width * l.sprite.scale, h = t->height * l.sprite.scale;
        x0 = std::min(x0, l.offsetX - l.sprite.pivotX * w); x1 = std::max(x1, l.offsetX + (1 - l.sprite.pivotX) * w);
        y0 = std::min(y0, l.offsetY - l.sprite.pivotY * h); y1 = std::max(y1, l.offsetY + (1 - l.sprite.pivotY) * h);
    }
    if (x1 < x0) return {feet.x, feet.y, 0, 0};
    float w = (x1 - x0) * 0.8f, h = (y1 - y0) * 0.85f, cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    return {feet.x + (cx - w / 2) * scale, feet.y + (cy - h / 2) * scale, w * scale, h * scale};
}

// ---------------------------------------------------------------- parallax
bool ParallaxBackgroundManager::HasAssets(const std::string& region) { return TextureManager::Get().Exists("backgrounds/" + region + "/layers.txt"); }

bool ParallaxBackgroundManager::LoadRegion(const std::string& region) {
    layers_.clear();
    std::ifstream in(std::string(ASSET_ROOT) + "backgrounds/" + region + "/layers.txt");
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string kw, plane; int rep = 1;
        ss >> kw;
        if (kw != "layer") continue;
        BackgroundLayer l;
        ss >> l.path >> l.factorX >> l.factorY >> l.scale >> l.yAnchor >> rep >> plane;
        l.repeatX = rep != 0;
        l.plane = plane == "distant" ? BackgroundLayer::Distant : plane == "fore" ? BackgroundLayer::Foreground : BackgroundLayer::Mid;
        l.name = l.path;
        // ART HOOK: one painted band per layer, e.g. backgrounds/cave/far_water.png (factor 0.05), far_walls.png (0.15),
        // mid_columns.png (0.35), near_rock.png (0.7) and foreground_teeth.png (1.4). Wide paintings tile seamlessly in x.
        layers_.push_back(l);
    }
    std::stable_sort(layers_.begin(), layers_.end(), [](const BackgroundLayer& a, const BackgroundLayer& b) { return a.factorX < b.factorX; });   // far to near
    return !layers_.empty();
}

void ParallaxBackgroundManager::Draw(BackgroundLayer::Plane plane, float camX, float camY, int screenW, int screenH) const {
    (void)screenH;
    for (const BackgroundLayer& l : layers_) {
        if (l.plane != plane) continue;
        Vector2 sc = Scroll(camX, camY, l.factorX, l.factorY);
        if (l.draw) { l.draw(sc.x, sc.y); continue; }
        const Texture2D* t = TextureManager::Get().Load(l.path);
        if (!t) continue;
        float w = t->width * l.scale, h = t->height * l.scale;
        float y = l.yAnchor + sc.y;
        // A wide band repeats seamlessly in x: start at the last tile edge left of the screen. Nothing is generated per pixel
        // and nothing is a tiny noise pattern: the tile IS the painting.
        float startX = l.repeatX ? sc.x - std::floor(sc.x / w) * w - w : sc.x;
        int n = l.repeatX ? (int)std::ceil((screenW - startX) / w) + 1 : 1;
        for (int i = 0; i < n; i++) {
            Mat2D m = Mat2D::FromTRS(startX + i * w, y, 0, 1, 1);
            SpriteRenderer::Draw(*t, m, 0.0f, 0.0f, l.scale, l.tint, l.blend);
        }
        (void)h;
    }
}

}  // namespace art
