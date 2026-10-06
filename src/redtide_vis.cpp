// Red Tide's Visual Overhaul (Red_Tide_Reference/"Red Tide - Visual Overhaul Spec.pdf"; progress in
// docs/REDTIDE_VISUAL.md): the four divers on the shared humanoid figure (figure3d.h, tools/artgen/rt_divers.py), and
// the studio, the harness's fixed shots of each new asset (--shots shots/vis rvis_).
#include "redtide_vis.h"
#include "figure3d.h"
#include "skins.h"
#include "redtide_profile.h"
#include "game.h"
#include "raymath.h"
#include <algorithm>
#include <map>
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

// the Locker's token skins as material sets on the same mesh (spec, "Skins"): the suit's canvas, and the helmet's
// metal and trim. Verdigris: green patina; Red Tide: deep red canvas, rust-red brass; Bone: bleached ivory; Pearl:
// nacre; Atlantean: bronze with gold. Nothing else changes: the silhouette is the suit's.
void DiverSkinColours(const std::string& suit, const std::string& helmet, std::vector<Recolor>& out) {
    if (suit == "verdigris") { out.push_back({"top", {70, 112, 96, 255}}); out.push_back({"trousers", {62, 100, 86, 255}}); }
    else if (suit == "redtide") { out.push_back({"top", {124, 30, 28, 255}}); out.push_back({"trousers", {104, 26, 24, 255}}); }
    else if (suit == "bone") { out.push_back({"top", {214, 204, 184, 255}}); out.push_back({"trousers", {196, 186, 166, 255}}); }
    else if (suit == "pearl") { out.push_back({"top", {200, 208, 216, 255}}); out.push_back({"trousers", {186, 194, 204, 255}}); }
    else if (suit == "atlantean") { out.push_back({"top", {44, 64, 120, 255}}); out.push_back({"trousers", {36, 52, 100, 255}}); }
    std::string h = helmet.size() > 2 && helmet[1] == '_' ? helmet.substr(2) : helmet;
    if (h == "verdigris") { out.push_back({"hat", {84, 150, 124, 255}}); out.push_back({"accent", {104, 164, 136, 255}}); }
    else if (h == "redtide") { out.push_back({"hat", {150, 58, 40, 255}}); out.push_back({"accent", {172, 72, 46, 255}}); }
    else if (h == "bone") { out.push_back({"hat", {226, 214, 190, 255}}); out.push_back({"accent", {196, 184, 160, 255}}); }
    else if (h == "pearl") { out.push_back({"hat", {228, 232, 238, 255}}); out.push_back({"accent", {206, 220, 236, 255}}); }
    else if (h == "atlantean") { out.push_back({"hat", {150, 104, 50, 255}}); out.push_back({"accent", {236, 186, 64, 255}}); }
}

std::vector<Matrix> DrawDiverFigure(int voice, Matrix frame, fig::Pose P, float t, Color tint, const std::string& suit, const std::string& helmet, const std::vector<Recolor>* extra, bool draw) {
    const Model* m = DiverModel(voice);
    if (!m) return {};
    Temperament(voice, P, t);
    fig::Build B; B.build = voice == 3 ? 1.05f : 1.0f;
    std::vector<Matrix> skin = fig::PoseFigure(*m, B, P, t);
    std::vector<Recolor> rc = {{"skin", DiverSkin(voice)}};
    DiverSkinColours(suit, helmet, rc);
    if (extra) rc.insert(rc.end(), extra->begin(), extra->end());
    if (draw) DrawPbrSkinned(*m, frame, skin, rc, 0.35f, tint);
    return skin;
}

// first person: your own arms in your suit's gloves and sleeves, the body behind the eye (the arms reach forward to
// the gun), each fist closing on its grip (world points) by IK; gripL false: the left hand rests by the right
bool DrawFirstPersonArms(int voice, const Camera3D& cam, Vector3 gripR, Vector3 gripL, bool leftOn, float t, const std::string& suit, const std::string& helmet) {
    static const char* F[4] = {"shared/divers/diver_diver_fp.glb", "shared/divers/diver_whaler_fp.glb", "shared/divers/diver_stowaway_fp.glb", "shared/divers/diver_mechanic_fp.glb"};
    const Model* m = LoadAsset(F[std::clamp(voice, 0, 3)]);
    if (!m) return false;
    Vector3 f = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 flat = Vector3Length(Vector3{f.x, 0, f.z}) > 1e-3f ? Vector3Normalize({f.x, 0, f.z}) : Vector3{1, 0, 0};
    Vector3 up{0, 1, 0}, rgt = Vector3Normalize(Vector3CrossProduct(flat, up));
    // (a viewmodel's scale: the crew's big stylised hands, this close to the eye, would fill the view at full size)
    const float VS = 0.7f;
    // (the shoulders just under the eye and a hand's breadth ahead, so the arms reach the grip with the elbows bent; the
    // torso stays below the bottom of the view)
    Vector3 o = Vector3Add(cam.position, Vector3Add(Vector3Scale(up, -1.21f), Vector3Add(Vector3Scale(flat, 0.1f), Vector3Scale(rgt, 0.04f))));
    Vector3 fx = Vector3Scale(flat, VS), uy = Vector3Scale(up, VS), rz = Vector3Scale(rgt, VS);
    Matrix frame = {fx.x, uy.x, rz.x, o.x, fx.y, uy.y, rz.y, o.y, fx.z, uy.z, rz.z, o.z, 0, 0, 0, 1};
    fig::Pose P; P.fp = true; P.grip = 0.9f; P.reach = 1.0f; P.elbow = 0.45f; P.breathe = t * 1.7f;
    Matrix inv = MatrixInvert(frame);
    P.ik[1] = true; P.target[1] = Vector3Transform(gripR, inv);
    P.ik[0] = true; P.target[0] = Vector3Transform(leftOn ? gripL : Vector3Add(gripR, Vector3Add(Vector3Scale(rgt, -0.09f), {0, -0.06f, 0})), inv);
    fig::Build B; B.build = voice == 3 ? 1.05f : 1.0f;
    std::vector<Matrix> skin = fig::PoseFigure(*m, B, P, t);
    std::vector<Recolor> rc = {{"skin", DiverSkin(voice)}};
    DiverSkinColours(suit, helmet, rc);
    for (const auto& w : skins::WornColours(skins::REDTIDE)) rc.push_back({w.material, w.c});   // (the Wardrobe's skin, over the Locker's)
    DrawPbrSkinned(*m, frame, skin, rc, 0.35f, WHITE);
    return true;
}

// the viewmodel (tools/artgen/rt_fphands.py -> fp_<diver>.glb): gloved hands built closed on a grip, a gauntlet cuff
// and a sleeve per arm, each rigid on its own bone, placed here with one matrix each (no body, no IK): the hands ride the
// gun's own frame, so they kick, roll and sway with it, and each forearm runs from its wrist out of the bottom corner
bool DrawVmArms(const Model& m, const Camera3D& cam, const Matrix* handR, const Matrix* handL, bool leftGrip, float girth, float t, const std::vector<Recolor>& rc, Color tint) {
    const RigInfo& R = RigOf(m);
    std::vector<Matrix> skin(R.parent.size(), Matrix{});   // (all zero: a part not used this frame folds away)
    auto set = [&](const char* n, Matrix mm) { int b = R.Find(n); if (b >= 0) skin[b] = mm; };
    // (the generator's frame, Blender's x forward, y left, z up, arrives as the glTF's x, z, -y: the same as the guns')
    const float CUFF = 0.1f;   // rt_fphands.py CUFF_LEN
    Vector3 f = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 up0{0, 1, 0}, rgt = Vector3Normalize(Vector3CrossProduct(f, up0)), up = Vector3CrossProduct(rgt, f);
    // a forearm: the cuff from the wrist toward the elbow, the sleeve on from inside its mouth to past the elbow
    auto forearm = [&](const char* cuff, const char* arm, Vector3 wrist, Vector3 elbow) {
        Vector3 X = Vector3Normalize(Vector3Subtract(elbow, wrist));
        Vector3 Y = Vector3Normalize(Vector3Subtract(up, Vector3Scale(X, Vector3DotProduct(up, X))));
        Vector3 Z = Vector3CrossProduct(X, Y);
        auto basis = [&](Vector3 p, float sx) {
            Vector3 a = Vector3Scale(X, sx), b = Vector3Scale(Y, girth), c = Vector3Scale(Z, girth);
            return Matrix{a.x, b.x, c.x, p.x, a.y, b.y, c.y, p.y, a.z, b.z, c.z, p.z, 0, 0, 0, 1};
        };
        set(cuff, basis(wrist, girth));
        Vector3 s0 = Vector3Add(wrist, Vector3Scale(X, CUFF * girth * 0.6f));
        if (getenv("DEPTH_VMNOARM")) return;
        set(arm, basis(s0, Vector3Distance(s0, elbow) + 0.25f));
    };
    // the elbows: below and out to each side, a little ahead of the eye (off the bottom of the view), breathing
    float br = 0.006f * sinf(t * 1.7f);
    auto elbowAt = [&](float side) { return Vector3Add(cam.position, Vector3Add(Vector3Scale(rgt, side * 0.3f), Vector3Add(Vector3Scale(up, -0.6f + br), Vector3Scale(f, 0.12f)))); };
    if (handR) {
        set("hand.R", *handR);
        forearm("cuff.R", "arm.R", Vector3Transform({-0.072f, -0.006f, 0.024f}, *handR), elbowAt(1));
    }
    if (handL && !leftGrip) {
        set("hand.L", *handL);
        forearm("cuff.L", "arm.L", Vector3Transform({-0.03f, -0.066f, -0.036f}, *handL), elbowAt(-1));
    } else if (handL) {
        set("grip.L", *handL);
        forearm("cuff.L", "arm.L", Vector3Transform({-0.072f, -0.006f, -0.024f}, *handL), elbowAt(-1));
    }
    DrawPbrSkinned(m, MatrixIdentity(), skin, rc, 0.35f, tint);
    return true;
}

bool DrawViewmodelHands(int voice, const Camera3D& cam, const VmHold& h, float t, const std::string& suit, const std::string& helmet) {
    static const char* F[4] = {"shared/divers/fp_diver.glb", "shared/divers/fp_whaler.glb", "shared/divers/fp_stowaway.glb", "shared/divers/fp_mechanic.glb"};
    const Model* m = LoadAsset(F[std::clamp(voice, 0, 3)]);
    if (!m) return false;
    float tilt = -(90 - h.angle) * DEG2RAD;
    auto gripAt = [&](Vector3 at) { return MatrixMultiply(MatrixMultiply(MatrixRotateZ(tilt), MatrixTranslate(at.x, at.y, at.z)), h.gun); };
    // the right hand on the grip; the left cupping the fore-end, closed over the right hand on a pistol's grip, or
    // round a second pistol
    Matrix hr = gripAt(h.gripR), hl{};
    if (h.left == 1) { Vector3 at = Vector3Add(h.gripL, {0.04f * h.magOut, 0.012f - 0.09f * h.magOut, 0}); hl = MatrixMultiply(MatrixTranslate(at.x, at.y, at.z), h.gun); }
    else if (h.left == 2) hl = gripAt(Vector3Add(h.gripR, {0.004f, -0.014f, -0.028f}));
    else if (h.left == 3) hl = MatrixMultiply(gripAt(h.gripR), MatrixTranslate(h.leftShift.x, h.leftShift.y, h.leftShift.z));
    std::vector<Recolor> rc;
    DiverSkinColours(suit, helmet, rc);
    for (const auto& w : skins::WornColours(skins::REDTIDE)) rc.push_back({w.material, w.c});
    if (const skins::Costume* c = skins::WornCostume(skins::REDTIDE)) rc.push_back({"top", c->sleeve});   // (a costume's sleeves)
    if (getenv("DEPTH_VMDBG")) rc = {{"leather", RED}, {"top", GREEN}, {"accent", BLUE}};   // (which part is which)
    // (girth 1.1: slimmer than the hands' 1.5 licence, the forearms pass close to the eye; a tint down: this close to the
    // eye the helmet lamp would wash the gloves out to tan)
    return DrawVmArms(*m, cam, &hr, h.left ? &hl : nullptr, h.left >= 2, 1.1f, t, rc, Color{165, 165, 165, 255});
}
// the hands' own moments (the spec: "Tonic drinking ... reviving ... have first-person hand animations. Drinking a tonic
// underwater: the bottle feeds through a valve in the helmet"): kind 0 drinks (k 0..1: the left hand brings the bottle up
// to the helmet's valve under the port, tips it, takes it away), kind 1 revives (both hands down before you, working)
bool DrawViewmodelAction(int voice, const Camera3D& cam, int kind, float k, Color liquid, float t, const std::string& suit, const std::string& helmet) {
    static const char* F[4] = {"shared/divers/fp_diver.glb", "shared/divers/fp_whaler.glb", "shared/divers/fp_stowaway.glb", "shared/divers/fp_mechanic.glb"};
    const Model* m = LoadAsset(F[std::clamp(voice, 0, 3)]);
    if (!m) return false;
    Vector3 f = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 rgt = Vector3Normalize(Vector3CrossProduct(f, {0, 1, 0})), up = Vector3CrossProduct(rgt, f);
    auto P = [&](float fw, float side, float h) { return Vector3Add(cam.position, Vector3Add(Vector3Scale(f, fw), Vector3Add(Vector3Scale(rgt, side), Vector3Scale(up, h)))); };
    auto hand = [&](Vector3 at, Vector3 fwd, Vector3 axis, float s) {   // a grip hand at `at`: its grip along axis, its front toward fwd
        Vector3 y = Vector3Normalize(axis), x = Vector3Normalize(Vector3Subtract(fwd, Vector3Scale(y, Vector3DotProduct(fwd, y)))), z = Vector3CrossProduct(x, y);
        x = Vector3Scale(x, s); y = Vector3Scale(y, s); z = Vector3Scale(z, s);
        return Matrix{x.x, y.x, z.x, at.x, x.y, y.y, z.y, at.y, x.z, y.z, z.z, at.z, 0, 0, 0, 1};
    };
    std::vector<Recolor> rc;
    DiverSkinColours(suit, helmet, rc);
    for (const auto& w : skins::WornColours(skins::REDTIDE)) rc.push_back({w.material, w.c});
    if (const skins::Costume* c = skins::WornCostume(skins::REDTIDE)) rc.push_back({"top", c->sleeve});
    const Color tint{165, 165, 165, 255};
    if (kind == 0) {
        // up (0-0.35), tipped into the valve (0.35-0.75), away (0.75-1)
        float rise = k < 0.35f ? k / 0.35f : k < 0.75f ? 1 : 1 - (k - 0.75f) / 0.25f;
        rise = rise * rise * (3 - 2 * rise);
        float tip = k > 0.3f && k < 0.8f ? sinf((k - 0.3f) / 0.5f * PI) : 0;
        Vector3 at = Vector3Lerp(P(0.46f, -0.3f, -0.44f), P(0.38f, -0.04f, -0.2f), rise);
        Vector3 axis = Vector3Normalize(Vector3Add(up, Vector3Add(Vector3Scale(f, -0.6f * tip), Vector3Scale(rgt, 0.5f * tip))));   // (the neck toward the valve)
        Matrix hl = hand(at, f, axis, 1.5f);
        DrawVmArms(*m, cam, nullptr, &hl, true, 1.1f, t, rc, tint);
        // the flask in the fist (stations_rt.py: glass, a brass valve neck), the tonic glowing inside it; the fist closes
        // round its body (the flask's base a little under the grip, its neck up the grip's axis)
        Matrix bm = hand(Vector3Add(at, Vector3Scale(axis, -0.06f)), f, axis, 1.5f);
        if (!DrawRtProp("redtide/stations/flask.glb", bm, nullptr, WHITE))
            DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.05f, 0.1f, 0.05f), MatrixTranslate(0, 0.05f, 0)), bm), Color{170, 130, 60, 255});
        DrawCubeGlow(MatrixMultiply(MatrixMultiply(MatrixScale(0.04f, 0.07f, 0.04f), MatrixTranslate(0, 0.045f, 0)), bm), liquid, 0.8f);
        return true;
    }
    // reviving: both hands down before you, pressing and working at the helmet's purge valve of the one who's down
    float pump = sinf(t * 7) * 0.025f;
    Vector3 down = Vector3Normalize(Vector3Add(Vector3Scale(up, -1), Vector3Scale(f, 0.5f)));
    Matrix hr = hand(P(0.45f, 0.1f, -0.36f + pump), f, Vector3Scale(down, -1), 1.5f), hl = hand(P(0.45f, -0.1f, -0.36f - pump), f, Vector3Scale(down, -1), 1.5f);
    DrawVmArms(*m, cam, &hr, &hl, true, 1.1f, t, rc, tint);
    return true;
}

// ---------------------------------------------------------------- the creature kit (phase 5)
// Red Tide's fish on the Trawl's rigged fish (tools/artgen/fish.py: ten archetypes on a four-bone spine, a ray with
// wings): the body plan and the name choose the archetype, the species record's three colours paint it (back, belly,
// fins), and it swims its spine. Everything else (shells, arms, legs, jellies, colonies) keeps the CreatureBuilder
// model for now. A budget per frame keeps the cost to the nearest few dozen.
static const char* ArchFor(const CreatureModel& cm) {
    std::string n = " " + cm.species;
    for (auto& c : n) c = (char)tolower((unsigned char)c);
    auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
    const std::string& p = cm.plan;
    if (has("swordfish") || has("marlin") || has("sailfish")) return "billfish";
    if (p == "shark" || has("shark") || has("dogfish")) return "shark";
    if (p == "depressiform") return has("flounder") || has("halibut") || has(" sole") || has("plaice") ? "flat" : "ray";
    if (p == "anguilliform" || p == "snake" || p == "burrower") return "eel";
    if (p == "leviathan") return has("shark") ? "shark" : has("ray") || has("manta") ? "ray" : "deep";
    if (p == "compressiform") return "deep";
    if (p == "fusiform") {
        if (has("tuna") || has("mackerel") || has("jack") || has("bonito") || has("trevally") || has("amberjack")) return "tuna";
        if (has("barracuda") || has("pike") || has("needlefish") || has("gar")) return "pike";
        if (has("herring") || has("sardine") || has("anchov") || has("sprat") || has("silverside") || has("minnow") || has("fry")) return "herring";
        return "perch";
    }
    return nullptr;
}
static int gCreatureBudget = 0;
void CreatureBudget(int n) { gCreatureBudget = n; }
static float gCreatureRoll = 0;
void CreatureRoll(float r) { gCreatureRoll = r; }   // (the next creature draws rolled about their length: a corpse belly-up)
// the other body plans (tools/artgen/creatures_rt.py), posed here: the crab's legs step in alternate pairs and its
// pincers open and shut, the shrimp curls its tail, the cephalopod's arms trail in waves, the jelly's bell pulses
// and its tentacles sway, the turtle beats its front flippers, the cetacean's spine undulates up and down
static const char* PlanFor(const CreatureModel& cm) {
    const std::string& p = cm.plan;
    if (p == "crab") return "crab";
    if (p == "shrimp") return "shrimp";
    if (p == "cephalopod") return "cephalopod";
    if (p == "jelly") return "jelly";
    if (p == "turtle") return "turtle";
    if (p == "cetacean" || p == "pinniped") return "cetacean";
    std::string n = cm.species; for (auto& c : n) c = (char)tolower((unsigned char)c);
    auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
    if (p == "echinoderm") return has("star") ? "starfish" : has("cucumber") ? "cucumber" : has("whelk") || has("snail") || has("conch") || has("cowrie") || has("periwinkle") || has("limpet") ? "whelk" : "urchin";
    if (p == "crocodilian") return "croc";
    if (p == "amphibian") return "frog";
    if (p == "bird") return "bird";
    if (p == "insect") return "skater";
    if (p == "leviathan" && (has("whale") || has("orca"))) return "cetacean";
    return nullptr;
}
static bool DrawPlanPbr(const CreatureModel& cm, const char* plan, Vector3 pos, float yaw, float pitch, float scale, float phase, float inten, Color tint, Color gill = {150, 40, 30, 255});
// flora with bones (tools/artgen/flora_rt.py): a plant of `height` standing at pos, swaying in the current by its own
// rig (kelp along its stipe, grass blade by blade, a fan as one sheet, an anemone's tentacles waving), painted its colour
static int gFloraBudget = 1 << 30;
void FloraBudget(int n) { gFloraBudget = n; }
bool FloraBudgetLeft() { return gFloraBudget > 0; }
bool DrawFloraPbr(const char* kind, Vector3 pos, float yaw, float height, Color c, float t, float phase) {
    if (getenv("DEPTH_OLDCREATURES")) return false;
    if (gFloraBudget <= 0) return true;   // (spent: drawn as nothing rather than as a box)
    gFloraBudget--;
    const Model* m = LoadAsset(std::string("redtide/flora/cr_fl_") + kind + ".glb");
    if (!m) return false;
    const RigInfo& R = RigOf(*m);
    RigPose P; P.Reset((int)R.parent.size());
    std::string k = kind;
    const Vector3 X{1, 0, 0}, Z{0, 0, 1};
    for (int b = 0; b < (int)R.name.size(); b++) {
        const std::string& n = R.name[b];
        float a = 0, a2 = 0;
        if (k == "kelp" && n[0] == 'k') { int j = n[1] - '0'; a = (0.06f + 0.025f * j) * sinf(t * 0.8f - j * 0.6f + phase); a2 = 0.5f * a * cosf(t * 0.5f + phase); }
        else if (k == "grass" && n[0] == 'b') { int i = atoi(n.c_str() + 1), j = n.back() - '0'; a = (0.1f + 0.06f * j) * sinf(t * 1.2f + i * 0.7f - j * 0.5f + phase); a2 = 0.4f * a; }
        else if (k == "fan") a = 0.07f * sinf(t * 0.7f + phase + (n == "f1" ? -0.6f : 0));
        else if (k == "anemone" && n[0] == 't') { int i = atoi(n.c_str() + 1), j = n.back() - '0'; a = (0.15f + 0.1f * j) * sinf(t * 1.5f + i * 0.4f + phase); a2 = 0.6f * a; }
        else if (k == "branch" && n[0] == 'b' && n[1] == 'r') a = 0.03f * sinf(t * 0.9f + n[2] + phase);
        else if (k == "sargassum" && n[0] == 's') { a = 0.1f * sinf(t * 1.1f + n[1] + phase); a2 = a; }
        if (a != 0 || a2 != 0) P.rot[b] = QuaternionMultiply(QuaternionFromAxisAngle(Z, a), QuaternionFromAxisAngle(X, a2));
    }
    auto sk = SolveRig(R, P);
    if (getenv("DEPTH_FLORATINY")) height *= 0.01f;   // (a profiling switch: the draws without their pixels)
    Matrix w = MatrixMultiply(MatrixMultiply(MatrixScale(height, height, height), MatrixRotateY(yaw)), MatrixTranslate(pos.x, pos.y, pos.z));
    Color tip{(unsigned char)std::min(255, c.r + 40), (unsigned char)std::min(255, c.g + 40), (unsigned char)std::min(255, c.b + 30), 255};
    Color base{(unsigned char)(c.r * 0.6f), (unsigned char)(c.g * 0.6f), (unsigned char)(c.b * 0.6f), 255};
    SetNextNoShadow();   // (many, small, swaying: their shadows aren't worth a pass)
    DrawPbrSkinned(*m, w, sk, {{"back", c}, {"fin", tip}, {"belly", base}}, 0.4f, WHITE);
    return true;
}

// the bosses (spec, "Bosses"): each its own model in creatures_rt.py; kind as Match::bossKind (0 the Goliath, 1 the
// Lobster, 2 the Matriarch, 3 the Cistern Wyrm, 4 the Lantern Leviathan); hot 0..1 lights the weak point (the
// Goliath's gills in their windows, the Lobster's crystal ringing, the Leviathan's lure)
bool DrawBossPbr(int kind, const CreatureModel& cm, Vector3 pos, float yaw, float pitch, float scale, float phase, float inten, Color tint, float hot) {
    static const char* PL[5] = {"goliath", "lobster", "orca", "wyrm", "angler"};
    if (kind < 0 || kind > 4 || getenv("DEPTH_OLDCREATURES")) return false;
    Color g = kind == 0 ? ColorLerp(Color{70, 30, 26, 255}, Color{255, 70, 40, 255}, hot)
            : kind == 1 ? ColorLerp(Color{170, 150, 220, 255}, Color{240, 230, 255, 255}, hot)
            : kind == 3 ? Color{176, 120, 50, 255}
            : kind == 4 ? ColorLerp(Color{200, 230, 220, 255}, Color{255, 250, 230, 255}, hot) : Color{150, 40, 30, 255};
    CreatureModel c = cm;
    if (kind == 2) { c.base = {22, 24, 28, 255}; c.belly = {230, 232, 228, 255}; c.accent = {30, 32, 36, 255}; }   // (an orca's black and white)
    if (kind == 1) { c.base = {220, 206, 196, 255}; c.belly = {236, 200, 196, 255}; }
    if (kind == 0) { c.base = {112, 96, 70, 255}; c.belly = {176, 156, 116, 255}; c.accent = {70, 56, 40, 255}; }   // (a grouper's browns: dark fins, darker mottling)                                 // (pale from life in the dark; a veined underside)
    int keep = gCreatureBudget; gCreatureBudget = 1;
    bool ok = DrawPlanPbr(c, PL[kind], pos, yaw, pitch, scale, phase, inten, tint, g);
    gCreatureBudget = keep;
    return ok;
}
static bool DrawPlanPbr(const CreatureModel& cm, const char* plan, Vector3 pos, float yaw, float pitch, float scale, float phase, float inten, Color tint, Color gill) {
    const Model* m = LoadAsset(std::string("redtide/creatures/cr_") + plan + ".glb");
    if (!m) return false;
    gCreatureBudget--;
    const RigInfo& R = RigOf(*m);
    RigPose P; P.Reset((int)R.parent.size());
    auto rot = [&](const char* n, Vector3 ax, float a) { int b = R.Find(n); if (b >= 0) P.rot[b] = QuaternionMultiply(P.rot[b], QuaternionFromAxisAngle(ax, a)); };
    const Vector3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1};
    float amp = std::clamp(0.3f + 0.7f * inten, 0.2f, 1.2f);
    std::string pl = plan;
    if (pl == "crab") {
        for (int i = 0; i < 4; i++) for (int s = 0; s < 2; s++) {
            float ph = phase * 1.4f + i * PI / 2 + s * PI;
            rot(TextFormat("leg%d%sa", i, s ? "R" : "L"), Z, (s ? -1.0f : 1.0f) * 0.3f * amp * std::max(0.0f, sinf(ph)));
            rot(TextFormat("leg%d%sa", i, s ? "R" : "L"), Y, 0.25f * amp * cosf(ph));
        }
        float open = 0.35f * (0.5f + 0.5f * sinf(phase * 0.6f));
        rot("pincer.L", Y, open); rot("pincer.R", Y, -open);
        rot("claw.L", X, 0.15f * sinf(phase * 0.4f)); rot("claw.R", X, 0.15f * sinf(phase * 0.4f + 1));
    } else if (pl == "shrimp") {
        for (int k = 0; k < 6; k++) rot(TextFormat("t%d", k), X, 0.1f * amp * sinf(phase - k * 0.8f));
    } else if (pl == "cephalopod") {
        for (int i = 0; i < 8; i++) for (int j = 0; j < 4; j++) {
            float w = sinf(phase * 0.7f - j * 0.9f + i * 0.8f) * (0.15f + 0.12f * j) * amp;
            rot(TextFormat("a%d_%d", i, j), X, w); rot(TextFormat("a%d_%d", i, j), Y, 0.5f * w * cosf(i * 0.8f));
        }
    } else if (pl == "jelly") {
        float pulse = 0.5f + 0.5f * sinf(phase * 0.9f);
        int b = R.Find("bell"); if (b >= 0) P.scale[b] = {1 + 0.1f * pulse, 1 - 0.18f * pulse, 1 + 0.1f * pulse};
        for (int i = 0; i < 8; i++) for (int j = 0; j < 4; j++) rot(TextFormat("k%d_%d", i, j), X, 0.18f * sinf(phase * 0.45f - j * 0.7f + i));
    } else if (pl == "turtle") {
        float f = 0.55f * amp * sinf(phase * 0.5f);
        rot("flip.FL", Z, f); rot("flip.FR", Z, -f);
        rot("flip.RL", Y, 0.25f * sinf(phase * 0.5f + 1)); rot("flip.RR", Y, -0.25f * sinf(phase * 0.5f + 1));
        rot("head", X, 0.08f * sinf(phase * 0.3f));
    } else if (pl == "cetacean" || pl == "orca") {
        for (int k = 0; k < 4; k++) rot(TextFormat("s%d", k), X, amp * 0.6f * sinf(phase * 0.5f - k * 1.2f) * (0.12f + 0.12f * k));
    } else if (pl == "goliath" || pl == "angler") {
        for (int k = 0; k < 4; k++) rot(TextFormat("s%d", k), Y, amp * 0.5f * sinf(phase * 0.6f - k * 1.25f) * (0.1f + 0.14f * k));
        rot("lure", X, 0.2f * sinf(phase * 0.35f)); rot("lure", Y, 0.15f * sinf(phase * 0.27f));
    } else if (pl == "wyrm") {
        for (int k = 0; k < 12; k++) rot(TextFormat("w%d", k), Y, amp * 0.28f * sinf(phase * 0.8f - k * 0.7f));
    } else if (pl == "bird") {   // wingbeats (a dive folds them back)
        float f = 0.6f * sinf(phase * 1.6f) * amp;
        rot("wing.L", Z, f); rot("wing.R", Z, -f);
    } else if (pl == "frog") {   // a breast-stroke kick
        float k = std::max(0.0f, sinf(phase * 0.8f)) * amp;
        rot("hind.L", Y, -0.6f * k); rot("hind.R", Y, 0.6f * k);
        rot("fore.L", Y, 0.3f * k); rot("fore.R", Y, -0.3f * k);
    } else if (pl == "croc") {   // the tail sweeps it along, the legs tucked to the sides
        for (int k = 0; k < 5; k++) rot(TextFormat("s%d", k), Y, amp * 0.35f * sinf(phase * 0.6f - k * 1.0f) * (0.05f + 0.1f * k));
        for (int s = 0; s < 2; s++) { rot(s ? "legFR" : "legFL", Y, 0.3f * sinf(phase * 0.6f + s * PI)); rot(s ? "legRR" : "legRL", Y, 0.3f * sinf(phase * 0.6f + s * PI + 1)); }
    } else if (pl == "cucumber") {
        for (int k = 0; k < 3; k++) rot(TextFormat("s%d", k), Y, 0.12f * sinf(phase * 0.3f - k));
    } else if (pl == "starfish") {
        for (int i = 0; i < 5; i++) rot(TextFormat("arm%d", i), X, 0.08f * sinf(phase * 0.3f + i * 1.3f));
    } else if (pl == "skater") {
        for (int i = 0; i < 3; i++) for (int s = 0; s < 2; s++) rot(TextFormat("leg%d%s", i, s ? "R" : "L"), Y, 0.35f * amp * sinf(phase * 1.5f + i * 2.1f + s * PI));
    } else if (pl == "whelk") {
        rot("head", Y, 0.15f * sinf(phase * 0.25f));
    } else if (pl == "lobster") {
        for (int i = 0; i < 4; i++) for (int s = 0; s < 2; s++) rot(TextFormat("leg%d%sa", i, s ? "R" : "L"), Y, 0.25f * amp * cosf(phase * 1.2f + i * PI / 2 + s * PI));
        for (int k = 0; k < 4; k++) rot(TextFormat("t%d", k), X, 0.08f * amp * sinf(phase * 0.7f - k * 0.8f));
        float open = 0.3f * (0.5f + 0.5f * sinf(phase * 0.5f));
        rot("pincer.L", Y, open); rot("pincer.R", Y, -open);
    }
    std::vector<Matrix> sk = SolveRig(R, P);
    float len = std::max(0.05f, cm.length * scale);
    Matrix w = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(len, len, len), MatrixRotateZ(gCreatureRoll)), MatrixRotateX(-pitch)), MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(pos.x, pos.y, pos.z)));
    Color fin = cm.accent.a > 0 ? cm.accent : Color{(unsigned char)(cm.base.r * 0.8f), (unsigned char)(cm.base.g * 0.8f), (unsigned char)(cm.base.b * 0.8f), 255};
    DrawPbrSkinned(*m, w, sk, {{"back", cm.base}, {"belly", cm.belly}, {"fin", fin}, {"gill", gill}}, 0.2f, tint);
    return true;
}
bool DrawCreaturePbr(const CreatureModel& cm, Vector3 pos, float yaw, float pitch, float scale, float phase, float inten, Color tint) {
    if (gCreatureBudget <= 0 || getenv("DEPTH_OLDCREATURES")) return false;
    if (const char* pl = PlanFor(cm)) return DrawPlanPbr(cm, pl, pos, yaw, pitch, scale, phase, inten, tint);
    const char* a = ArchFor(cm);
    if (!a) return false;
    const Model* m = LoadAsset(std::string("trawl/fish/fish_") + a + ".glb");
    if (!m) return false;
    gCreatureBudget--;
    const RigInfo& R = RigOf(*m);
    RigPose P; P.Reset((int)R.parent.size());
    float amp = std::clamp(0.25f + 0.55f * inten, 0.15f, 1.0f);
    for (int k = 0; k < 4; k++) {
        int b = R.Find(TextFormat("s%d", k));
        if (b >= 0) P.rot[b] = QuaternionFromAxisAngle({0, 1, 0}, amp * sinf(phase - k * 1.25f) * (0.25f + 0.3f * k));
    }
    int wl = R.Find("wing.L"), wr = R.Find("wing.R");
    if (wl >= 0 && wr >= 0) {
        float f = (0.25f + amp * 1.2f) * sinf(phase * 0.45f);
        P.rot[wl] = QuaternionFromAxisAngle({0, 0, 1}, f);
        P.rot[wr] = QuaternionFromAxisAngle({0, 0, 1}, -f);
    }
    std::vector<Matrix> sk = SolveRig(R, P);
    float len = std::max(0.05f, cm.length * scale);
    Matrix w = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(len, len, len), MatrixRotateZ(gCreatureRoll)), MatrixRotateX(-pitch)), MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(pos.x, pos.y, pos.z)));
    Color fin = cm.accent.a > 0 ? cm.accent : Color{(unsigned char)(cm.base.r * 0.8f), (unsigned char)(cm.base.g * 0.8f), (unsigned char)(cm.base.b * 0.8f), 255};
    DrawPbrSkinned(*m, w, sk, {{"back", cm.base}, {"belly", cm.belly}, {"fin", fin}}, 0.0f, tint);
    return true;
}

// ---------------------------------------------------------------- the guns (phase 4: tools/artgen/weapons_rt.py)
const Model* RtWeaponModel(const std::string& id) {
    static std::map<std::string, bool> missing;
    if (missing.count(id)) return nullptr;
    const Model* m = LoadAsset("redtide/weapons/" + id + ".glb");
    if (!m) missing[id] = true;
    return m;
}
static float RtGroupValue(const std::string& g, const RtGunAnim& a) {
    auto open = [&](float r0, float r1, float r2, float r3) { return a.reload < 0 ? 0.0f : std::clamp((a.reload - r0) / (r1 - r0), 0.0f, 1.0f) * (1 - std::clamp((a.reload - r2) / (r3 - r2), 0.0f, 1.0f)); };
    if (g == "hammer") return 1 - a.fire;
    if (g == "trigger") return a.fire;
    if (g == "cylinder" || g == "drum") return (float)a.steps;
    if (g == "latch") return open(0.0f, 0.15f, 0.85f, 1.0f);
    if (g == "clip") return open(0.1f, 0.3f, 0.6f, 0.8f);
    if (g == "bolt") return a.cycle > 0 && a.cycle < 1 ? sinf(a.cycle * PI) : open(0.05f, 0.2f, 0.8f, 0.95f);
    if (g == "pump") return a.reload >= 0 ? 0.5f + 0.5f * sinf(a.reload * PI * 6) : (a.cycle > 0 && a.cycle < 1 ? sinf(a.cycle * PI) : 0.0f);
    if (g == "load") return a.loaded && !(a.reload > 0.2f && a.reload < 0.8f) ? 1.0f : 0.0f;
    if (g == "gauge") return 1 - std::clamp(a.gas, 0.0f, 1.0f);
    return 0;
}
bool DrawRtWeapon(const std::string& id, Matrix frame, const RtGunAnim& a, Color tint, Vector3* gripR, Vector3* gripL, Vector3* muzzle, float glow) {
    const Model* m = RtWeaponModel(id);
    const AssetInfo* A = m ? AssetInfoOf(m) : nullptr;
    if (!m || !A) return false;
    int n = (int)A->parts.size();
    std::vector<Matrix> M(n, MatrixIdentity());
    for (int i = 0; i < n; i++) {
        const AssetPart& p = A->parts[i];
        if (p.group == "static") continue;
        float v = RtGroupValue(p.group, a);
        if (p.kind == "slide") M[i] = MatrixTranslate(p.axis.x * p.amount * v, p.axis.y * p.amount * v, p.axis.z * p.amount * v);
        else if (p.kind == "show") M[i] = v < 0.5f ? MatrixMultiply(MatrixMultiply(MatrixTranslate(-p.pivot.x, -p.pivot.y, -p.pivot.z), MatrixScale(0, 0, 0)), MatrixTranslate(p.pivot.x, p.pivot.y, p.pivot.z)) : MatrixIdentity();
        else M[i] = MatrixMultiply(MatrixMultiply(MatrixTranslate(-p.pivot.x, -p.pivot.y, -p.pivot.z), MatrixRotate(p.axis, p.amount * v)), MatrixTranslate(p.pivot.x, p.pivot.y, p.pivot.z));
    }
    DrawPbrParts(*m, frame, M, tint, glow);
    auto mk = [&](const char* name, Vector3 def) { const AssetMarker* k = A->Marker(name); return Vector3Transform(k ? k->p : def, frame); };
    if (gripR) *gripR = mk("grip_r", {0, 0, 0});
    if (gripL) *gripL = mk("grip_l", {0, 0, 0});
    if (muzzle) *muzzle = mk("muzzle", {0.3f, 0, 0});
    return true;
}
// a baked prop with moving parts (tools/artgen/stations_rt.py and the like): each part's group posed by value(group)
// (0 at rest .. 1 its full travel); false if the model isn't built
bool DrawRtProp(const std::string& path, Matrix frame, const std::function<float(const std::string&)>& value, Color tint, float glow) {
    const Model* m = LoadAsset(path);
    const AssetInfo* A = m ? AssetInfoOf(m) : nullptr;
    if (!m) return false;
    std::vector<Matrix> M;
    if (A) {
        M.assign(A->parts.size(), MatrixIdentity());
        for (size_t i = 0; i < A->parts.size(); i++) {
            const AssetPart& p = A->parts[i];
            if (p.group == "static" || p.group.empty()) continue;
            float v = value ? value(p.group) : 0;
            if (p.kind == "slide") M[i] = MatrixTranslate(p.axis.x * p.amount * v, p.axis.y * p.amount * v, p.axis.z * p.amount * v);
            else M[i] = MatrixMultiply(MatrixMultiply(MatrixTranslate(-p.pivot.x, -p.pivot.y, -p.pivot.z), MatrixRotate(p.axis, p.amount * v)), MatrixTranslate(p.pivot.x, p.pivot.y, p.pivot.z));
        }
    }
    DrawPbrParts(*m, frame, M, tint, glow);
    return true;
}
Vector3 RtWeaponMarker(const std::string& id, const char* name, Vector3 def) {
    const Model* m = RtWeaponModel(id);
    const AssetInfo* A = m ? AssetInfoOf(m) : nullptr;
    const AssetMarker* k = A ? A->Marker(name) : nullptr;
    return k ? k->p : def;
}

// The helmet from inside (spec, "First person": "the brass HUD frames the screen, with faint glass reflections"): the
// front port's brass rim round the view, rivets on it, the helmet's dark copper in the corners, two faint streaks of
// reflection on the glass. Baked once into a texture at a third of the screen and drawn over the frame (under the
// HUD). wet: droplets on the glass (after an air pocket), 0..1.
static Texture2D gPortTex{};
void DrawHelmetPort(float wet, float t) {
    const int W = 640, H = 360;
    if (!gPortTex.id) {
        Image im = GenImageColor(W, H, BLANK);
        Color* px = (Color*)im.data;
        float rx = W * 0.56f, ry = H * 0.6f;   // the port: an oval a little wider than the view is tall
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            float dx = (x - W / 2.0f) / rx, dy = (y - H / 2.0f) / ry, r = sqrtf(dx * dx + dy * dy);
            Color c = BLANK;
            if (r > 1.0f) {
                // the helmet's inside: dark copper, lit faintly toward the rim
                float k = std::clamp((r - 1.0f) / 0.35f, 0.0f, 1.0f);
                c = {(unsigned char)(70 - 45 * k), (unsigned char)(44 - 30 * k), (unsigned char)(26 - 18 * k), 255};
            }
            if (r > 0.955f && r < 1.035f) {
                // the brass rim: a rounded band, bright along its inner edge, darker outward, a soft shadow inside it
                float u = (r - 0.955f) / 0.08f, lit = 0.55f + 0.45f * sinf(u * PI) * (0.7f + 0.3f * (-dy));
                c = {(unsigned char)std::min(255.0f, 170 * lit + 30), (unsigned char)std::min(255.0f, 126 * lit + 20), (unsigned char)(58 * lit + 10), 255};
            } else if (r > 0.92f && r <= 0.955f) {
                float u = (r - 0.92f) / 0.035f;
                c = {0, 0, 0, (unsigned char)(110 * u)};
            }
            px[y * W + x] = c;
        }
        // rivets round the rim
        for (int k = 0; k < 28; k++) {
            float a = k * 2 * PI / 28;
            int cx = (int)(W / 2.0f + cosf(a) * rx * 0.995f), cy = (int)(H / 2.0f + sinf(a) * ry * 0.995f);
            ImageDrawCircle(&im, cx, cy, 4, Color{96, 70, 30, 255});
            ImageDrawCircle(&im, cx - 1, cy - 1, 2, Color{230, 196, 120, 255});
        }
        // the glass's reflections: two faint diagonal streaks in the upper left
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            float s = (x * 0.8f + y) - 170, s2 = (x * 0.8f + y) - 215;
            float a = expf(-s * s / 60.0f) * 0.07f + expf(-s2 * s2 / 18.0f) * 0.05f;
            float dx = (x - W / 2.0f) / rx, dy = (y - H / 2.0f) / ry;
            if (dx * dx + dy * dy > 0.85f || x > W * 0.45f || y > H * 0.55f) continue;
            Color& c = px[y * W + x];
            if (c.a == 0) c = {230, 245, 250, (unsigned char)(a * 255)};
        }
        gPortTex = LoadTextureFromImage(im); UnloadImage(im);
        SetTextureFilter(gPortTex, TEXTURE_FILTER_BILINEAR);
    }
    DrawTexturePro(gPortTex, {0, 0, (float)W, (float)H}, {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0, WHITE);
    if (wet > 0) {   // droplets running down the glass
        for (int k = 0; k < 24; k++) {
            float x = fmodf(k * 173.3f, 1180.0f) + 50, y = fmodf(k * 97.1f + t * (20 + k % 5 * 9), 640.0f) + 30;
            DrawCircleV({x, y}, 3 + k % 3, Fade(Color{220, 240, 250, 255}, 0.18f * wet));
            DrawCircleV({x - 1, y - 1}, 1.2f, Fade(WHITE, 0.4f * wet));
        }
    }
}

// ---------------------------------------------------------------- the water's particles
const Texture2D& SoftDot() {
    static Texture2D t{};
    if (!t.id) {
        Image im = GenImageColor(32, 32, BLANK);
        for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
            float dx = (x - 15.5f) / 15.5f, dy = (y - 15.5f) / 15.5f, r = sqrtf(dx * dx + dy * dy);
            float a = std::clamp(1 - r, 0.0f, 1.0f); a = a * a * (3 - 2 * a);
            ImageDrawPixel(&im, x, y, Color{255, 255, 255, (unsigned char)(a * 255)});
        }
        t = LoadTextureFromImage(im); UnloadImage(im);
        SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    }
    return t;
}
// a bubble: a bright rim, a clear middle and a fleck of highlight up and to the left
static const Texture2D& BubbleTex() {
    static Texture2D t{};
    if (!t.id) {
        Image im = GenImageColor(32, 32, BLANK);
        for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
            float dx = (x - 15.5f) / 15.5f, dy = (y - 15.5f) / 15.5f, r = sqrtf(dx * dx + dy * dy);
            if (r > 1) continue;
            float rim = std::clamp((r - 0.72f) / 0.2f, 0.0f, 1.0f) * std::clamp((1 - r) / 0.08f, 0.0f, 1.0f);
            float hl = std::max(0.0f, 1 - sqrtf((dx + 0.35f) * (dx + 0.35f) + (dy + 0.4f) * (dy + 0.4f)) / 0.22f);
            float a = std::clamp(0.12f + rim * 0.75f + hl, 0.0f, 1.0f);
            ImageDrawPixel(&im, x, y, Color{235, 248, 255, (unsigned char)(a * 255)});
        }
        t = LoadTextureFromImage(im); UnloadImage(im);
        SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    }
    return t;
}
struct Bubble { Vector3 p; float r, life, ph; };
static std::vector<Bubble> gBubbles;
static uint32_t gFxRng = 99;
static float FR() { gFxRng = gFxRng * 1664525u + 1013904223u; return (gFxRng >> 8) / 16777216.0f; }
void FxBubbles(Vector3 at, int n, float spread, float size) {
    for (int k = 0; k < n && gBubbles.size() < 600; k++)
        gBubbles.push_back({{at.x + (FR() - 0.5f) * spread, at.y + (FR() - 0.5f) * spread, at.z + (FR() - 0.5f) * spread}, size * (0.5f + FR()), 4 + FR() * 3, FR() * 6.3f});
}
void FxStep(float dt, float surfaceY) {
    for (auto& b : gBubbles) {
        float rise = 0.55f + b.r * 18;                 // (bigger bubbles climb faster)
        b.p.y += rise * dt;
        b.ph += dt * (5 + b.r * 30);
        b.p.x += sinf(b.ph) * 0.25f * dt; b.p.z += cosf(b.ph * 0.8f) * 0.25f * dt;
        b.r *= 1 + 0.02f * dt;                         // (they swell as the pressure falls)
        b.life -= dt;
        if (b.p.y > surfaceY) b.life = 0;
    }
    gBubbles.erase(std::remove_if(gBubbles.begin(), gBubbles.end(), [](const Bubble& b) { return b.life <= 0; }), gBubbles.end());
}
void FxDrawBubbles(const Camera3D& cam) {
    const Texture2D& tx = BubbleTex();
    for (const auto& b : gBubbles) {
        if (Vector3Distance(b.p, cam.position) > 30) continue;
        float a = std::min(1.0f, b.life * 2);
        DrawBillboard(cam, tx, b.p, b.r * 2, Fade(WHITE, 0.85f * a));
    }
}
void FxClear() { gBubbles.clear(); }

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
    } else if (which == 3) {
        // the skins: each diver as issued and in the Locker's five suit-and-helmet sets
        cam.position = {0, -0.4f, 13.5f}; cam.target = {0, -0.6f, 0}; cam.fovy = 40;
        lamp({-3, 5, 9}, {0, 1, 0});
        RenderBegin(cam, L);
        static const char* SK[6] = {"", "verdigris", "redtide", "bone", "pearl", "atlantean"};
        for (int d = 0; d < 4; d++) for (int s = 0; s < 6; s++) {
            fig::Pose P; P.breathe = t * 1.4f + d + s;
            Vector3 at{(s - 2.5f) * 1.25f, (1.5f - d) * 2.05f - 1.6f, 0};
            DrawDiverFigure(d, MatrixMultiply(MatrixScale(0.9f, 0.9f, 0.9f), fig::Frame(at, FRONT + 0.35f)), P, t, WHITE, SK[s], std::string("h_") + SK[s]);
        }
    } else if (which == 4) {
        // the guns (spec, shot set 3): every baked Red Tide weapon side on, on a neutral ground, in three states for the
        // first (at rest, the shot, mid-reload)
        static const char* ID[] = {"cormorant", "gannet", "needler1", "carbine", "flechette12", "longspeargun", "needler2", "trawlerman", "boltharpoon", "chumthrower", "gatling", "stormlock",
                                   "drumflechette", "reefrattler", "cannonharpoon", "limpetlauncher", "netgun", "harpooncannon", "knife", "boardingaxe", "teslagaff", "trident",
                                   "galvanicrod", "resonator", "anemonegun", "tidestaff", "abyssallure"};
        const int NID = (int)(sizeof(ID) / sizeof(ID[0]));
        cam.position = {0.75f, -0.15f, 4.1f}; cam.target = {0.75f, -0.15f, 0}; cam.fovy = 36;
        lamp({-0.5f, 1.5f, 2.2f}, {0.3f, 0, 0});
        L.fog = {70, 92, 98, 255}; L.fogDensity = 0.002f; L.water = 0; L.outline = 0;
        L.AddPoint({1.2f, 0.8f, 1.2f}, 5, {255, 220, 180, 255}, 0.6f);
        RenderBegin(cam, L);
        for (int k = 0; k < NID; k++) {
            RtGunAnim a; a.gas = 0.6f;
            float x = (k % 3) * 0.78f - 0.35f, y = 0.85f - (k / 3) * 0.24f;
            DrawRtWeapon(ID[k], MatrixMultiply(MatrixRotateX(-0.12f), MatrixTranslate(x, y, 0)), a, WHITE);
        }
        RtGunAnim fire; fire.fire = 1; fire.steps = 1;
        RtGunAnim rel; rel.reload = 0.5f; rel.steps = 3; rel.loaded = false;
        DrawRtWeapon("cormorant", MatrixMultiply(MatrixScale(1.4f, 1.4f, 1.4f), MatrixTranslate(1.95f, 0.85f, 0)), fire, WHITE);
        DrawRtWeapon("needler1", MatrixMultiply(MatrixScale(1.2f, 1.2f, 1.2f), MatrixTranslate(1.95f, 0.55f, 0)), rel, WHITE);
        DrawRtWeapon("flechette12", MatrixTranslate(1.95f, 0.25f, 0), rel, WHITE);
    } else if (which == 5) {
        // the viewmodel from outside (DEPTH_VMVIEW 0 the right side, 1 from above-behind): guns in the hands as first
        // person holds them, each with a stand-in eye behind and to its left for the forearms
        static const char* ID[4] = {"cormorant", "carbine", "needler1", "knife"};
        int view = getenv("DEPTH_VMVIEW") ? atoi(getenv("DEPTH_VMVIEW")) : 0;
        if (view == 0) { cam.position = {0.3f, 0.0f, 2.2f}; cam.target = {0.3f, 0.0f, 0}; }
        else { cam.position = {-1.0f, 0.9f, -0.9f}; cam.target = {0.3f, 0.0f, 0}; }
        cam.fovy = 36;
        lamp({-0.5f, 1.5f, 2.2f}, {0.3f, 0, 0});
        L.fog = {70, 92, 98, 255}; L.fogDensity = 0.002f; L.water = 0; L.outline = 0;
        L.AddPoint({1.2f, 0.8f, 1.2f}, 5, {255, 220, 180, 255}, 0.6f);
        RenderBegin(cam, L);
        for (int k = 0; k < 4; k++) {
            Vector3 at{(k % 2) * 0.75f - 0.1f, 0.3f - (k / 2) * 0.55f, 0};
            Vector3 g = RtWeaponMarker(ID[k], "grip_r", {0, 0, 0});
            Matrix frame = MatrixMultiply(MatrixMultiply(MatrixTranslate(-g.x, -g.y, -g.z), MatrixScale(1.5f, 1.5f, 1.5f)), MatrixTranslate(at.x, at.y, at.z));
            RtGunAnim a;
            DrawRtWeapon(ID[k], frame, a, WHITE);
            Camera3D eye{}; eye.position = Vector3Add(at, {-0.38f, 0.1f, -0.14f}); eye.target = Vector3Add(eye.position, {1, 0, 0}); eye.up = {0, 1, 0};
            VmHold h; h.gun = frame; h.gripR = g; h.gripL = RtWeaponMarker(ID[k], "grip_l", g);
            h.angle = k == 1 ? 58.0f : k == 3 ? 0.0f : 74.0f;
            h.left = k == 3 ? 0 : Vector3Distance(h.gripL, h.gripR) < 0.05f ? 2 : 1;
            DrawViewmodelHands(0, eye, h, t);
        }
    } else if (which == 6) {
        // the skins gallery (skins.h): ten of Red Tide's skins a page (DEPTH_SKINPAGE 0-7), each on the Diver, three
        // quarters on, with its name and rarity under it
        int page = getenv("DEPTH_SKINPAGE") ? atoi(getenv("DEPTH_SKINPAGE")) : 0;
        const auto& cat = skins::Catalogue(skins::REDTIDE);
        cam.position = {0, 1.0f, 9.6f}; cam.target = {0, 0.85f, 0}; cam.fovy = 40;
        lamp({-3, 5, 8}, {0, 1, 0});
        RenderBegin(cam, L);
        for (int k = 0; k < 10; k++) {
            int i = page * 10 + k;
            if (i >= (int)cat.size()) break;
            const skins::Skin& s = cat[i];
            std::vector<Recolor> rc = {{"top", s.top}, {"trousers", s.trousers}, {"hat", s.hat}, {"accent", s.trim}};
            fig::Pose P; P.breathe = t * 1.4f + k;
            Vector3 at{(k % 5 - 2) * 1.75f, k < 5 ? 1.15f : -1.55f, 0};
            DrawDiverFigure(0, MatrixMultiply(MatrixScale(0.82f, 0.82f, 0.82f), fig::Frame(at, FRONT + 0.4f)), P, t, WHITE, "", "", &rc);
            skins::gGallery.push_back({GetWorldToScreenEx({at.x, at.y - 0.12f, at.z}, cam, SCREEN_W, SCREEN_H), s.name, skins::RarityName(s.rarity), skins::RarityColor(s.rarity), s.price});
        }
    } else if (which == 7) {
        // the costumes gallery (skins.h): ten of Red Tide's costumes a page (DEPTH_SKINPAGE 0-1), each worn by the Diver
        int page = getenv("DEPTH_SKINPAGE") ? atoi(getenv("DEPTH_SKINPAGE")) : 0;
        const auto& cat = skins::Costumes(skins::REDTIDE);
        cam.position = {0, 1.0f, 9.6f}; cam.target = {0, 0.85f, 0}; cam.fovy = 40;
        int zoom = getenv("DEPTH_CZOOM") ? atoi(getenv("DEPTH_CZOOM")) : 0;   // (1 or 2: a close-up of that row)
        if (zoom) { float y = zoom == 1 ? 1.15f + 0.75f : -1.55f + 0.75f; cam.position = {0, y + 0.2f, 6.2f}; cam.target = {0, y, 0}; cam.fovy = 34; }
        lamp({-3, 5, 8}, {0, 1, 0});
        int one = getenv("DEPTH_CONE") ? atoi(getenv("DEPTH_CONE")) : -1;   // (one costume, big, from three sides: a close look)
        const char* forceModel = getenv("DEPTH_CMODEL");                    // (or any costume model by name, the Trawl's too)
        if (forceModel && forceModel[0] && one < 0) one = 0;
        if (one >= 0) { cam.position = {0, 1.0f, 4.6f}; cam.target = {0, 0.95f, 0}; cam.fovy = 40; }
        RenderBegin(cam, L);
        if (one >= 0 && one < (int)cat.size()) {
            skins::Costume c = cat[one]; if (forceModel && forceModel[0]) { c.model = forceModel; c.name = forceModel; }
            for (int v = 0; v < 3; v++) {
                fig::Pose P; P.breathe = t * 1.4f;
                Vector3 at{(v - 1) * 1.25f, 0.05f, 0};
                Matrix fr = fig::Frame(at, FRONT + (v == 0 ? 0.6f : v == 1 ? PI / 2 : PI));
                std::vector<Matrix> sk = DrawDiverFigure(0, fr, P, t, WHITE, "", "", nullptr, false);
                if (const Model* dm = DiverModel(0)) fig::DrawCostume(c.model, *dm, sk, fr);
            }
            RenderEnd();
            skins::gGallery.clear();
            DrawTextCenteredBold(TextFormat("%s  (%s)", c.name, c.model), SCREEN_W / 2.0f, 16, 24, WHITE);
            return;
        }
        for (int k = 0; k < 10; k++) {
            int i = page * 10 + k;
            if (i >= (int)cat.size()) break;
            const skins::Costume& c = cat[i];
            fig::Pose P; P.breathe = t * 1.4f + k;
            Vector3 at{(k % 5 - 2) * 1.75f, k < 5 ? 1.15f : -1.55f, 0};
            Matrix fr = MatrixMultiply(MatrixScale(0.82f, 0.82f, 0.82f), fig::Frame(at, FRONT + 0.5f));
            bool has = LoadAsset(std::string("shared/costumes/costume_") + c.model + ".glb") != nullptr;
            std::vector<Matrix> sk = DrawDiverFigure(0, fr, P, t, WHITE, "", "", nullptr, !has);   // (the costume is the whole look)
            if (const Model* dm = DiverModel(0)) fig::DrawCostume(c.model, *dm, sk, fr);
            skins::gGallery.push_back({GetWorldToScreenEx({at.x, at.y - 0.12f, at.z}, cam, SCREEN_W, SCREEN_H), c.name, skins::CostumeTierName(c.tier), skins::RarityColor(c.tier), c.price});
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
    if (which == 6 || which == 7) skins::DrawGallery(skins::REDTIDE, getenv("DEPTH_SKINPAGE") ? atoi(getenv("DEPTH_SKINPAGE")) : 0, which == 7);
}

// the Wardrobe's costume preview (skins::gPreview): the diver you play, turning slowly on the right of the screen
static void WardrobePreview(int game, const char* model, float t) {
    (void)game;
    SceneLight L;
    L.fog = {38, 92, 104, 255}; L.fogDensity = 0.01f;
    L.fill = {40, 96, 112, 255}; L.rim = {120, 200, 220, 255}; L.key = {255, 236, 200, 255};
    L.surfaceY = 6; L.time = t; L.lampRange = 14; L.lampCone = 0.55f;
    L.moonDir = Vector3Normalize({-0.3f, -1.0f, -0.2f}); L.moon = {200, 230, 235, 255}; L.moonK = 0.55f;
    L.ambK = 0.55f; L.skyAmb = {60, 116, 136, 255}; L.seaAmb = {16, 34, 40, 255};
    L.filmic = 1; L.exposure = 0.85f; L.aoK = 0.6f; L.outline = 0.35f; L.outlineTint = {16, 44, 52, 255}; L.stipple = 0;
    Camera3D cam{}; cam.projection = CAMERA_PERSPECTIVE; cam.up = {0, 1, 0}; cam.fovy = 34;
    cam.position = {-1.35f, 1.25f, 4.6f}; cam.target = {-1.35f, 1.0f, 0};
    L.lampPos = {-2, 4, 5}; L.lampDir = Vector3Normalize({2, -3, -5});
    RenderBegin(cam, L);
    fig::Pose P; P.breathe = t * 1.4f;
    Matrix fr = fig::Frame({0, 0, 0}, -PI / 2 + 0.6f * sinf(t * 0.35f) + 0.3f);
    const Profile& prof = GetProfile();
    bool has = model && model[0] && LoadAsset(std::string("shared/costumes/costume_") + model + ".glb") != nullptr;
    std::vector<Matrix> sk = DrawDiverFigure(0, fr, P, t, WHITE, prof.suit, prof.helmet, nullptr, !has);
    if (const Model* dm = DiverModel(0)) fig::DrawCostume(model, *dm, sk, fr);
    RenderEnd();
}
static bool gPreviewSet = (skins::gPreview[skins::REDTIDE] = &WardrobePreview, true);

} // namespace rt
