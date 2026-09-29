// ============================================================================
//  DEPTH - the rig's clip data (a data file, as the Master Reference asks: numbers live here, not in logic).
//  Each clip keys channel offsets over time. Attacks follow one grammar: a windup of anticipation (ANTICIPATE),
//  a strike of 2-3 frames (SNAP), then overshoot and settle (SETTLE). Reactions are proportional: a hit is a
//  flinch, a crit a stagger, death a collapse that holds.
// ============================================================================
#include "rig.h"

namespace rig {
namespace {
using K = Key;
Track T(int ch, std::initializer_list<Key> k) { return Track{ch, std::vector<Key>(k)}; }

std::vector<Clip> MakeClips() {
    std::vector<Clip> c(CL_COUNT);
    c[CL_IDLE] = {"idle", 4.4f, true, {
        T(C_HIPX, {K{0, 0}, K{1.1f, 1.6f}, K{2.2f, 0}, K{3.3f, -1.4f}, K{4.4f, 0}}),     // weight from foot to foot
        T(C_LEAN, {K{0, 0}, K{2.2f, 0.025f}, K{4.4f, 0}}),
        T(C_HEAD, {K{0, 0}, K{1.6f, -0.05f}, K{2.9f, 0.04f}, K{4.4f, 0}}),               // a glance up, then down
    }};
    c[CL_BREATHE] = {"breathe", 3.2f, true, {
        T(C_CHEST, {K{0, 0}, K{1.4f, -0.025f}, K{3.2f, 0}}),
        T(C_HFY, {K{0, 0}, K{1.4f, -1.2f}, K{3.2f, 0}}), T(C_HBY, {K{0, 0}, K{1.4f, -1.2f}, K{3.2f, 0}}),
        T(C_HIPY, {K{0, 0.6f}, K{1.4f, 0}, K{3.2f, 0.6f}}),
    }};
    c[CL_STRESSED] = {"stressed idle", 1.9f, true, {
        T(C_HEAD, {K{0, 0.22f}, K{0.9f, 0.3f}, K{1.1f, 0.12f, SNAP}, K{1.9f, 0.22f}}),  // a nervous look over the shoulder
        T(C_HIPY, {K{0, 6}, K{1.9f, 6}}),
        T(C_HBX, {K{0, 8}, K{1.9f, 8}}), T(C_HBY, {K{0, -14}, K{0.95f, -17}, K{1.9f, -14}}),
        T(C_LEAN, {K{0, 0.08f}, K{1.9f, 0.08f}}),
    }};
    c[CL_DEATHSDOOR] = {"death's door idle", 2.6f, true, {
        T(C_HIPY, {K{0, 24}, K{1.3f, 28}, K{2.6f, 24}}),                                    // on one knee, heaving
        T(C_LEAN, {K{0, 0.22f}, K{1.3f, 0.3f}, K{2.6f, 0.22f}}),
        T(C_HEAD, {K{0, 0.38f}, K{1.3f, 0.5f}, K{2.6f, 0.38f}}),
        T(C_HFY, {K{0, 10}, K{2.6f, 10}}), T(C_FBX, {K{0, -6}, K{2.6f, -6}}), T(C_FBY, {K{0, 0}, K{2.6f, 0}}),
    }};
    c[CL_WALK] = {"walk", 0.72f, true, {
        T(C_FFX, {K{0, -12}, K{0.36f, 13}, K{0.72f, -12}}), T(C_FFY, {K{0, 0}, K{0.18f, 7}, K{0.36f, 0}, K{0.72f, 0}}),
        T(C_FBX, {K{0, 13}, K{0.36f, -12}, K{0.72f, 13}}), T(C_FBY, {K{0, 0}, K{0.36f, 0}, K{0.54f, 7}, K{0.72f, 0}}),
        T(C_HIPY, {K{0, 1}, K{0.18f, -2}, K{0.36f, 1}, K{0.54f, -2}, K{0.72f, 1}}),
        T(C_HFX, {K{0, 10}, K{0.36f, -8}, K{0.72f, 10}}), T(C_HBX, {K{0, -8}, K{0.36f, 10}, K{0.72f, -8}}),
        T(C_LEAN, {K{0, 0.06f}, K{0.72f, 0.06f}}),
    }};
    c[CL_GUARD] = {"guard", 1.0f, true, {
        T(C_HIPY, {K{0, 8}, K{1, 8}}), T(C_FFX, {K{0, 6}, K{1, 6}}), T(C_FBX, {K{0, -4}, K{1, -4}}),
        T(C_HFX, {K{0, 8}, K{1, 8}}), T(C_HFY, {K{0, -20}, K{1, -20}}), T(C_WEAPON, {K{0, -30}, K{1, -30}}),
        T(C_HBX, {K{0, 10}, K{1, 10}}), T(C_HBY, {K{0, -14}, K{1, -14}}),
    }};
    c[CL_SLASH] = {"slash", 0.62f, false, {
        T(C_HFX, {K{0, 0}, K{0.2f, -12, ANTICIPATE}, K{0.26f, 36, SNAP}, K{0.62f, 0, SETTLE}}),
        T(C_HFY, {K{0, 0}, K{0.2f, -42, ANTICIPATE}, K{0.26f, 8, SNAP}, K{0.62f, 0, SETTLE}}),
        T(C_WEAPON, {K{0, 0}, K{0.2f, -70, ANTICIPATE}, K{0.26f, 45, SNAP}, K{0.62f, 0, SETTLE}}),
        T(C_LEAN, {K{0, 0}, K{0.2f, -0.12f}, K{0.26f, 0.32f, SNAP}, K{0.62f, 0, SETTLE}}),
        T(C_ROOTX, {K{0, 0}, K{0.2f, -6}, K{0.26f, 22, SNAP}, K{0.62f, 0, SETTLE}}),
        T(C_FFX, {K{0, 0}, K{0.26f, 12, SNAP}, K{0.62f, 0}}),
    }};
    c[CL_THRUST] = {"thrust", 0.66f, false, {
        T(C_HFX, {K{0, 0}, K{0.22f, -18, ANTICIPATE}, K{0.29f, 46, SNAP}, K{0.66f, 0, SETTLE}}),
        T(C_HFY, {K{0, 0}, K{0.22f, -8}, K{0.29f, -6, SNAP}, K{0.66f, 0, SETTLE}}),
        T(C_HIPY, {K{0, 0}, K{0.22f, 10, ANTICIPATE}, K{0.29f, 6}, K{0.66f, 0}}),
        T(C_LEAN, {K{0, 0}, K{0.22f, -0.14f}, K{0.29f, 0.36f, SNAP}, K{0.66f, 0, SETTLE}}),
        T(C_ROOTX, {K{0, 0}, K{0.22f, -8}, K{0.29f, 38, SNAP}, K{0.66f, 0, SETTLE}}),
        T(C_FFX, {K{0, 0}, K{0.29f, 16, SNAP}, K{0.66f, 0}}),
    }};
    c[CL_SWING] = {"heavy swing", 0.92f, false, {
        T(C_HFY, {K{0, 0}, K{0.34f, -72, ANTICIPATE}, K{0.42f, 22, SNAP}, K{0.92f, 0, SETTLE}}),
        T(C_HFX, {K{0, 0}, K{0.34f, -8}, K{0.42f, 32, SNAP}, K{0.92f, 0, SETTLE}}),
        T(C_WEAPON, {K{0, 0}, K{0.34f, -115, ANTICIPATE}, K{0.42f, 60, SNAP}, K{0.92f, 0, SETTLE}}),
        T(C_LEAN, {K{0, 0}, K{0.34f, -0.22f}, K{0.42f, 0.46f, SNAP}, K{0.92f, 0, SETTLE}}),
        T(C_HIPY, {K{0, 0}, K{0.34f, -2}, K{0.42f, 12, SNAP}, K{0.92f, 0}}),
        T(C_ARMZ, {K{0, 0}, K{0.42f, 1, SNAP}, K{0.92f, 0}}),                                    // the follow-through crosses the body
    }};
    c[CL_SHOOT] = {"shoot", 0.7f, false, {
        T(C_HFX, {K{0, 0}, K{0.24f, 32}, K{0.3f, 24, SNAP}, K{0.7f, 0, SETTLE}}),
        T(C_HFY, {K{0, 0}, K{0.24f, -26}, K{0.3f, -34, SNAP}, K{0.7f, 0, SETTLE}}),
        T(C_ROOTX, {K{0, 0}, K{0.24f, 0}, K{0.3f, -12, SNAP}, K{0.7f, 0, SETTLE}}),              // kicked back by the recoil
        T(C_LEAN, {K{0, 0}, K{0.24f, 0.05f}, K{0.3f, -0.16f, SNAP}, K{0.7f, 0, SETTLE}}),
        T(C_HBX, {K{0, 0}, K{0.24f, 22}, K{0.7f, 0}}), T(C_HBY, {K{0, 0}, K{0.24f, -22}, K{0.7f, 0}}),
    }};
    c[CL_THROW] = {"throw", 0.66f, false, {
        T(C_HFX, {K{0, 0}, K{0.22f, -24, ANTICIPATE}, K{0.3f, 38, SNAP}, K{0.66f, 0, SETTLE}}),
        T(C_HFY, {K{0, 0}, K{0.22f, -44, ANTICIPATE}, K{0.3f, -12, SNAP}, K{0.66f, 0, SETTLE}}),
        T(C_LEAN, {K{0, 0}, K{0.22f, -0.16f}, K{0.3f, 0.26f, SNAP}, K{0.66f, 0, SETTLE}}),
        T(C_HBX, {K{0, 0}, K{0.22f, 18}, K{0.3f, -6}, K{0.66f, 0}}), T(C_HBY, {K{0, 0}, K{0.22f, -18}, K{0.66f, 0}}),
    }};
    c[CL_CAST] = {"cast", 0.9f, false, {
        T(C_HBX, {K{0, 0}, K{0.3f, 30, OVERSHOOT}, K{0.9f, 0}}), T(C_HBY, {K{0, 0}, K{0.3f, -40, OVERSHOOT}, K{0.9f, 0}}),
        T(C_HFX, {K{0, 0}, K{0.3f, 22, OVERSHOOT}, K{0.9f, 0}}), T(C_HFY, {K{0, 0}, K{0.3f, -48, OVERSHOOT}, K{0.9f, 0}}),
        T(C_HEAD, {K{0, 0}, K{0.3f, -0.16f}, K{0.9f, 0}}), T(C_ROOTY, {K{0, 0}, K{0.3f, -4}, K{0.9f, 0}}),
    }};
    c[CL_HEAL] = {"heal", 0.9f, false, {
        T(C_HIPY, {K{0, 0}, K{0.3f, 18, OVERSHOOT}, K{0.9f, 0}}), T(C_HFX, {K{0, 0}, K{0.3f, 26}, K{0.9f, 0}}), T(C_HFY, {K{0, 0}, K{0.3f, 8}, K{0.9f, 0}}),
        T(C_HEAD, {K{0, 0}, K{0.3f, 0.22f}, K{0.9f, 0}}), T(C_LEAN, {K{0, 0}, K{0.3f, 0.2f}, K{0.9f, 0}}),
    }};
    c[CL_SONG] = {"song", 1.0f, false, {
        T(C_HFX, {K{0, 0}, K{0.3f, 16}, K{1, 0}}), T(C_HFY, {K{0, 0}, K{0.3f, -32}, K{1, 0}}),
        T(C_HBX, {K{0, 0}, K{0.3f, 14}, K{1, 0}}), T(C_HBY, {K{0, 0}, K{0.3f, -28}, K{1, 0}}),
        T(C_HEAD, {K{0, 0}, K{0.3f, -0.22f}, K{0.65f, -0.16f}, K{1, 0}}), T(C_CHEST, {K{0, 0}, K{0.3f, -0.08f}, K{1, 0}}),
    }};
    c[CL_HIT] = {"hit", 0.5f, false, {
        T(C_ROOTX, {K{0, 0}, K{0.05f, -16, SNAP}, K{0.5f, 0, SETTLE}}),
        T(C_LEAN, {K{0, 0}, K{0.05f, -0.3f, SNAP}, K{0.5f, 0, SETTLE}}),
        T(C_HEAD, {K{0, 0}, K{0.05f, -0.38f, SNAP}, K{0.5f, 0, SETTLE}}),
        T(C_HBY, {K{0, 0}, K{0.06f, -26, SNAP}, K{0.5f, 0}}),
    }};
    c[CL_DODGE] = {"dodge", 0.5f, false, {
        T(C_ROOTX, {K{0, 0}, K{0.12f, -26, SNAP}, K{0.5f, 0, SMOOTH}}),
        T(C_HIPY, {K{0, 0}, K{0.12f, 12}, K{0.5f, 0}}), T(C_LEAN, {K{0, 0}, K{0.12f, -0.2f}, K{0.5f, 0}}),
        T(C_FFX, {K{0, 0}, K{0.12f, -10}, K{0.5f, 0}}),
    }};
    c[CL_STAGGER] = {"stagger", 0.9f, false, {
        T(C_ROOTX, {K{0, 0}, K{0.08f, -30, SNAP}, K{0.9f, 0, SETTLE}}),
        T(C_LEAN, {K{0, 0}, K{0.08f, -0.45f, SNAP}, K{0.35f, 0.15f}, K{0.9f, 0, SETTLE}}),
        T(C_HIPY, {K{0, 0}, K{0.35f, 12}, K{0.9f, 0}}),
        T(C_FFX, {K{0, 0}, K{0.2f, -18}, K{0.9f, 0}}), T(C_FBX, {K{0, 0}, K{0.3f, -12}, K{0.9f, 0}}),
        T(C_HBY, {K{0, 0}, K{0.08f, -30, SNAP}, K{0.9f, 0}}),
    }};
    c[CL_CRIT] = {"crit reaction", 1.0f, false, {
        T(C_ROOTX, {K{0, 0}, K{0.06f, -40, SNAP}, K{1, 0, SETTLE}}),
        T(C_LEAN, {K{0, 0}, K{0.06f, -0.6f, SNAP}, K{0.4f, 0.3f}, K{1, 0, SETTLE}}),
        T(C_HEAD, {K{0, 0}, K{0.06f, -0.6f, SNAP}, K{0.4f, 0.3f}, K{1, 0}}),
        T(C_HIPY, {K{0, 0}, K{0.4f, 22}, K{1, 0}}),
        T(C_HBY, {K{0, 0}, K{0.06f, -40, SNAP}, K{1, 0}}), T(C_HFY, {K{0, 0}, K{0.3f, 16}, K{1, 0}}),
    }};
    c[CL_DEATH] = {"death", 1.5f, false, {
        T(C_HIPY, {K{0, 0}, K{0.3f, 26}, K{0.6f, 30}, K{1.1f, 64, SNAP}, K{1.5f, 66, SETTLE}}),   // to a knee, then down
        T(C_LEAN, {K{0, 0}, K{0.3f, 0.2f}, K{0.6f, 0.35f}, K{1.1f, 1.05f, SNAP}, K{1.5f, 1.1f}}),
        T(C_HEAD, {K{0, 0}, K{0.3f, 0.3f}, K{1.1f, 0.6f}, K{1.5f, 0.6f}}),
        T(C_HFY, {K{0, 0}, K{0.6f, 20}, K{1.5f, 34}}), T(C_HFX, {K{0, 0}, K{1.1f, 24}}),
        T(C_HBY, {K{0, 0}, K{1.1f, 24}}),
    }};
    c[CL_VICTORY] = {"victory", 1.2f, false, {
        T(C_HFY, {K{0, 0}, K{0.35f, -74, OVERSHOOT}, K{1.2f, -70}}), T(C_HFX, {K{0, 0}, K{0.35f, 4}}),
        T(C_WEAPON, {K{0, 0}, K{0.35f, -80, OVERSHOOT}}),
        T(C_HEAD, {K{0, 0}, K{0.35f, -0.25f}}), T(C_LEAN, {K{0, 0}, K{0.35f, -0.12f}}),
        T(C_HBX, {K{0, 0}, K{0.35f, 10}}), T(C_HBY, {K{0, 0}, K{0.35f, -8}}),
    }};
    return c;
}
}  // namespace

const Clip& GetClip(int id) {
    static const std::vector<Clip> clips = MakeClips();
    return clips[id < 0 || id >= CL_COUNT ? 0 : id];
}
const char* ClipName(int id) { return GetClip(id).name; }

}  // namespace rig
