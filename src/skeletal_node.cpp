#include "skeletal_node.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace skel {

float Timeline::Sample(float t) const {
    if (keys.empty()) return 0;
    if (t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time) return keys.back().value;
    for (size_t i = 1; i < keys.size(); i++)
        if (t <= keys[i].time) {
            float k = (t - keys[i - 1].time) / std::max(1e-5f, keys[i].time - keys[i - 1].time);
            return keys[i - 1].value + (keys[i].value - keys[i - 1].value) * k;
        }
    return keys.back().value;
}

void Skeleton::Apply(const Animation& a, float t, float mix) {
    if (a.loop && a.duration > 0) t = std::fmod(t, a.duration);
    for (const Timeline& tl : a.timelines) {
        if (tl.bone < 0 || tl.bone >= (int)bones.size()) continue;
        Transform& p = bones[tl.bone].pose;
        float v = tl.Sample(t);
        switch (tl.prop) {
            case Timeline::Rotation: p.rotation = p.rotation + (v - p.rotation) * mix; break;
            case Timeline::X: p.x = p.x + (v - p.x) * mix; break;
            case Timeline::Y: p.y = p.y + (v - p.y) * mix; break;
            case Timeline::ScaleX: p.scaleX = p.scaleX + (v - p.scaleX) * mix; break;
            case Timeline::ScaleY: p.scaleY = p.scaleY + (v - p.scaleY) * mix; break;
        }
    }
}

void Skeleton::UpdateWorld(const Mat2D& root) {
    for (Bone& b : bones) {
        Mat2D local = Mat2D::FromTRS(b.pose.x, b.pose.y, b.pose.rotation, b.pose.scaleX, b.pose.scaleY);
        b.world = b.parent >= 0 ? bones[b.parent].world * local : root * local;   // a child inherits its parent's position, rotation and scale
    }
}

bool Skeleton::LoadText(const std::string& file) {
    std::ifstream in(file);
    if (!in) return false;
    bones.clear(); slots.clear(); animations.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string kw;
        ss >> kw;
        if (kw == "bone") {
            Bone b; std::string parent;
            ss >> b.name >> parent >> b.setup.x >> b.setup.y >> b.setup.rotation >> b.setup.scaleX >> b.setup.scaleY >> b.length;
            b.parent = parent == "-" ? -1 : FindBone(parent);
            if (parent != "-" && b.parent < 0) return false;   // parents must be listed first
            b.pose = b.setup;
            bones.push_back(b);
        } else if (kw == "slot") {
            Slot s; std::string bone;
            ss >> s.name >> bone;
            s.bone = FindBone(bone);
            if (s.bone < 0) return false;
            slots.push_back(s);
        } else if (kw == "attach") {
            std::string slot; Attachment a;
            ss >> slot >> a.name >> a.path >> a.pivotX >> a.pivotY >> a.scale;
            int si = FindSlot(slot);
            if (si < 0) return false;
            slots[si].attachments.push_back(a);
        } else if (kw == "anim") {
            Animation a;
            ss >> a.name >> a.duration;
            animations.push_back(a);
        } else if (kw == "key") {
            std::string bone, prop; Key k;
            ss >> bone >> prop >> k.time >> k.value;
            if (animations.empty()) return false;
            int bi = FindBone(bone);
            if (bi < 0) return false;
            Timeline::Prop p = prop == "rot" ? Timeline::Rotation : prop == "x" ? Timeline::X : prop == "y" ? Timeline::Y : prop == "sx" ? Timeline::ScaleX : Timeline::ScaleY;
            auto& tls = animations.back().timelines;
            auto it = std::find_if(tls.begin(), tls.end(), [&](const Timeline& t) { return t.bone == bi && t.prop == p; });
            if (it == tls.end()) { Timeline t; t.bone = bi; t.prop = p; tls.push_back(t); it = tls.end() - 1; }
            it->keys.push_back(k);
        }
    }
    return !bones.empty();
}

}  // namespace skel
