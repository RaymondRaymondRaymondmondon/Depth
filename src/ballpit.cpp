// Ball Pit Brawl's headless core (see ballpit.h). The spec's sections map to the pieces here: the arena kit
// (MakeArena), the traversal table (StepPlayer), foam guns, the knife and the vacuum (StepWeapons, StepDarts), balls
// and the one shared supply (StepBallHands, StepBalls, StepSupply, the cannons), score and the store (StepStore),
// streak rewards and joke items (StepRewards, StepEnts), and the four modes (StepMode). The bots are ballpit_bots.cpp.
#include "ballpit.h"
#include "json.h"
#include "redtide.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bp {

// ---------------------------------------------------------------- the config
static Config LoadCfg() {
    Config c; Json j = LoadJsonFile(rt::DataDir() + "/../ballpit/ballpit_config.json");
    const Json& p = j["player"]; const Json& h = j["health"]; const Json& k = j["knife"]; const Json& b = j["balls"]; const Json& d = j["darts"];
    const Json& s = j["score"]; const Json& st = j["store"]; const Json& o = j["objectives"];
    c.height = p["height"].F(c.height); c.crouchH = p["crouch_height"].F(c.crouchH); c.subH = p["submerged_height"].F(c.subH); c.radius = p["radius"].F(c.radius);
    c.eyeFromTop = p["eye_from_top"].F(c.eyeFromTop); c.gravity = p["gravity"].F(c.gravity);
    c.run = p["run"].F(c.run); c.crouch = p["crouch"].F(c.crouch); c.jumpH = p["jump_height"].F(c.jumpH); c.climb = p["climb"].F(c.climb); c.tunnel = p["tunnel"].F(c.tunnel);
    c.slide = p["slide"].F(c.slide); c.bridge = p["bridge"].F(c.bridge); c.wade = p["wade"].F(c.wade); c.submerged = p["submerged"].F(c.submerged); c.airControl = p["air_control"].F(c.airControl);
    c.stepUp = p["step_up"].F(c.stepUp); c.safeFall = p["safe_fall"].F(c.safeFall); c.fallDmgPerM = p["fall_damage_per_m"].F(c.fallDmgPerM); c.bridgeSpread = p["bridge_spread"].F(c.bridgeSpread);
    c.flagSpeed = p["flag_speed"].F(c.flagSpeed); c.vacuumSpeed = p["vacuum_speed"].F(c.vacuumSpeed);
    c.health = h["max"].F(c.health); c.regenAfter = h["regen_after"].F(c.regenAfter); c.regenRate = h["regen_rate"].F(c.regenRate); c.respawn = h["respawn"].F(c.respawn);
    c.knifeDmg = k["damage"].F(c.knifeDmg); c.knifeCool = k["cooldown"].F(c.knifeCool); c.knifeReach = k["reach"].F(c.knifeReach);
    c.ballR = b["radius"].F(c.ballR); c.throwSpeed = b["throw_speed"].F(c.throwSpeed); c.throwWindup = b["throw_windup"].F(c.throwWindup); c.throwLoft = b["throw_loft_deg"].F(c.throwLoft);
    c.grabTime = b["grab_time"].F(c.grabTime); c.cannonSpeed = b["cannon_speed"].F(c.cannonSpeed); c.ballGrav = b["gravity_scale"].F(c.ballGrav); c.ballsTotal = b["total"].I(c.ballsTotal);
    c.hopperMax = b["hopper"].I(c.hopperMax); c.conveyorTime = b["conveyor_time"].F(c.conveyorTime); c.cannonRate = b["cannon_rate"].F(c.cannonRate); c.cannonYaw = b["cannon_yaw_deg"].F(c.cannonYaw);
    c.cannonPitch = b["cannon_pitch_deg"].F(c.cannonPitch); c.cannonMount = b["cannon_mount"].F(c.cannonMount); c.cannonHeat = b["cannon_heat"].F(c.cannonHeat); c.cannonCool = b["cannon_cool"].F(c.cannonCool);
    c.dartFloorLife = d["floor_life"].F(c.dartFloorLife); c.dartCap = d["cap"].I(c.dartCap); c.dartStart = d["start"].I(c.dartStart); c.dartMax = d["max"].I(c.dartMax);
    c.vacuumRange = d["vacuum_range"].F(c.vacuumRange); c.vacuumCone = d["vacuum_cone_deg"].F(c.vacuumCone); c.vacuumRate = d["vacuum_rate"].F(c.vacuumRate);
    c.scoreKO = s["ko"].I(c.scoreKO); c.scoreCannonKO = s["cannon_ko"].I(c.scoreCannonKO); c.scoreAssist = s["assist"].I(c.scoreAssist); c.scoreCapture = s["capture"].I(c.scoreCapture);
    c.scoreReturn = s["return"].I(c.scoreReturn); c.scorePlant = s["plant"].I(c.scorePlant); c.scoreDefuse = s["defuse"].I(c.scoreDefuse); c.assistWindow = s["assist_window"].F(c.assistWindow);
    c.dartPack = st["dart_pack"].I(c.dartPack); c.dartPackCost = st["dart_pack_cost"].I(c.dartPackCost); c.disarmKit = st["disarm_kit"].I(c.disarmKit); c.storeSafe = st["safe_seconds"].F(c.storeSafe); c.jokeCool = st["joke_cooldown"].F(c.jokeCool);
    c.flagReturn = o["flag_return"].F(c.flagReturn); c.plantTime = o["plant"].F(c.plantTime); c.fuse = o["fuse"].F(c.fuse); c.defuseTime = o["defuse"].F(c.defuseTime); c.defuseKit = o["defuse_kit"].F(c.defuseKit);
    c.roundTime = o["round_time"].F(c.roundTime); c.bombRounds = o["bomb_rounds"].I(c.bombRounds); c.bombSwap = o["bomb_swap"].I(c.bombSwap);
    for (size_t i = 0; i < j["guns"].Size(); i++) {
        const Json& g = j["guns"][i]; GunDef d2;
        d2.key = g["key"].Str0(); d2.name = g["name"].Str0(); d2.role = g["role"].Str0(); d2.cost = g["cost"].I(0); d2.dmg = g["damage"].F(35); d2.pellets = g["pellets"].I(1); d2.mag = g["mag"].I(1);
        d2.rate = g["rate"].F(1); d2.reload = g["reload"].F(1); d2.speed = g["speed"].F(20); d2.grav = g["gravity"].F(0.5f); d2.spread = g["spread"].F(1); d2.burst = g["burst"].I(1);
        d2.spinUp = g["spin_up"].F(0); d2.slow = g["slow"].F(1); d2.knock = g["knockback"].F(0); d2.splash = g["splash"].F(0); d2.dartCost = g["dart_cost"].I(1); d2.ads = g["ads"].Bool0(true);
        c.guns.push_back(d2);
    }
    while ((int)c.guns.size() < G_COUNT) { GunDef d2; d2.name = "Blaster"; c.guns.push_back(d2); }
    for (size_t i = 0; i < j["rewards"].Size(); i++) { const Json& r = j["rewards"][i]; c.rewards.push_back({r["key"].Str0(), r["name"].Str0(), r["text"].Str0(), r["counter"].Str0(), r["streak"].I(300), r["time"].F(0)}); }
    while (c.rewards.size() < 6) c.rewards.push_back({"?", "?", "", "", 9999, 0});
    for (size_t i = 0; i < j["jokes"].Size(); i++) { const Json& r = j["jokes"][i]; c.jokes.push_back({r["key"].Str0(), r["name"].Str0(), r["text"].Str0(), r["cost"].I(5)}); }
    for (size_t i = 0; i < j["modes"].Size(); i++) { const Json& m = j["modes"][i]; ModeDef md; md.key = m["key"].Str0(); md.name = m["name"].Str0(); md.goal = m["goal"].Str0(); md.minP = m["min"].I(2); md.maxP = m["max"].I(12); md.respawn = m["respawn"].F(5); md.toWin = m["to_win"].I(25); md.timeLimit = m["time"].F(600); md.teams = m["teams"].Bool0(true); c.modes.push_back(md); }
    while (c.modes.size() < MD_COUNT) c.modes.push_back(ModeDef{});
    for (size_t i = 0; i < j["dad_jokes"].Size(); i++) c.dadJokes.push_back(j["dad_jokes"][i].Str0());
    for (size_t i = 0; i < j["prizes"].Size(); i++) c.prizes.push_back(j["prizes"][i].Str0());
    if (c.dadJokes.empty()) c.dadJokes.push_back("I'd tell you a joke about ball pits, but you'd have to dig for it.");
    if (c.prizes.empty()) c.prizes.push_back("Plastic Ring");
    return c;
}
static Config& CfgStore() { static Config c = LoadCfg(); return c; }
const Config& Cfg() { return CfgStore(); }
Config& CfgMutable() { return CfgStore(); }
const char* ModeName(int m) { return Cfg().modes[std::clamp(m, 0, MD_COUNT - 1)].name.c_str(); }
const char* RewardName(int r) { return Cfg().rewards[std::clamp(r, 0, 5)].name.c_str(); }

// the spec's cover table: what each material stops
bool BlocksMove(int m) { return m != M_NET ? true : true; }
bool BlocksShot(int m) { return m != M_FRAME && m != M_RAIL; }
bool BlocksSight(int m) { return m != M_NET && m != M_FRAME && m != M_RAIL; }

// ---------------------------------------------------------------- the arena (spec "Layout (default map, 60 m x 30 m hall)")
// Two team towers (team 0 at x < 0), the central atrium with its own four-level structure, galleries along both long
// walls, rope bridges and net tunnels across at level 3, slides, crawl tunnels, ball pits on the ground floor, and the
// conveyor along the long walls. Everything is on a 1 m grid and mirror-symmetric in x.
Arena MakeArena() {
    Arena a;
    auto box = [&](float x0, float y0, float z0, float x1, float y1, float z1, int mat, int col = 0) { Solid s; s.lo = {std::min(x0, x1), std::min(y0, y1), std::min(z0, z1)}; s.hi = {std::max(x0, x1), std::max(y0, y1), std::max(z0, z1)}; s.mat = (uint8_t)mat; s.colour = (uint8_t)col; a.solids.push_back(s); };
    auto deck = [&](float x0, float z0, float x1, float z1, float top, int col) { box(x0, top - 0.3f, z0, x1, top, z1, M_DECK, col); };
    // the hall's walls and ceiling
    box(-31, 0, -16, -30, a.ceil, 16, M_WALL); box(30, 0, -16, 31, a.ceil, 16, M_WALL);
    box(-31, 0, -16, 31, a.ceil, -15, M_WALL); box(-31, 0, 15, 31, a.ceil, 16, M_WALL);
    for (int side = 0; side < 2; side++) {
        float s = side == 0 ? -1.0f : 1.0f; auto X = [&](float x) { return s * x; };
        int col = side == 0 ? 1 : 2;   // (team colours on the towers' pads)
        // the tower: level 2 (the base: spawn and store), level 3 (the team cannons), the roof (the flag)
        deck(X(19), -14, X(29), 14, L2, col);
        deck(X(21), -11, X(29), 11, L3, col);
        deck(X(23), -6, X(29), 6, ROOF, col);
        // posts (the coloured pipe frame) at the decks' corners and along the front
        for (float z : {-14.0f, -11.0f, -6.0f, -2.0f, 2.0f, 6.0f, 11.0f, 14.0f}) for (float x : {19.0f, 21.0f, 23.0f}) {
            float top = x == 19 ? L3 : x == 21 ? ROOF : ROOF + 1.2f;
            if ((x == 21 && fabsf(z) > 11.5f) || (x == 23 && fabsf(z) > 6.5f)) continue;
            box(X(x) - 0.08f, 0, z - 0.08f, X(x) + 0.08f, top, z + 0.08f, M_FRAME, 3);
        }
        // nets along level 2's front, open in the middle where the cargo nets come up
        box(X(19) - 0.04f, L2, -14, X(19) + 0.04f, L3 - 0.2f, -4, M_NET); box(X(19) - 0.04f, L2, 4, X(19) + 0.04f, L3 - 0.2f, 14, M_NET);
        // level 3's front: rails with gaps for the bridges and the ladder tops; the roof's rails round three sides
        for (auto seg : {std::pair<float, float>{-11, -10}, {-8, -3.4f}, {-1.6f, 1.6f}, {3.4f, 8}, {10, 11}}) box(X(21) - 0.05f, L3, seg.first, X(21) + 0.05f, L3 + 1.0f, seg.second, M_RAIL, 3);
        for (auto seg : {std::pair<float, float>{-6, -5}, {-3, 3}, {5, 6}}) box(X(23) - 0.05f, ROOF, seg.first, X(23) + 0.05f, ROOF + 1.0f, seg.second, M_RAIL, 3);
        box(X(23), ROOF, -6.05f, X(29), ROOF + 1.0f, -5.95f, M_RAIL, 3); box(X(23), ROOF, 5.95f, X(29), ROOF + 1.0f, 6.05f, M_RAIL, 3);
        // solid plastic panels: the towers' sides under level 2 (the base is a room), and the backs of the roof
        box(X(23), 0, -14.1f, X(29), L2 - 0.3f, -13.9f, M_PANEL, col); box(X(23), 0, 13.9f, X(29), L2 - 0.3f, 14.1f, M_PANEL, col);
        box(X(28.9f), ROOF, -6, X(29), ROOF + 1.6f, 6, M_PANEL, col);
        // foam padding blocks: low cover on the decks
        box(X(20), L2, -8, X(21), L2 + 1, -6, M_PAD, col); box(X(20), L2, 6, X(21), L2 + 1, 8, M_PAD, col);
        box(X(22), L3, -2, X(23), L3 + 1, 2, M_PAD, col);
        box(X(25), ROOF, -2, X(26), ROOF + 0.9f, -1, M_PAD, col); box(X(25), ROOF, 1, X(26), ROOF + 0.9f, 2, M_PAD, col);
        box(X(25), L2, -4, X(26), L2 + 1, -2.5f, M_PAD, col); box(X(25), L2, 2.5f, X(26), L2 + 1, 4, M_PAD, col);
        // the ladders and cargo nets up the front (exposed: your back to the atrium)
        for (float z : {-2.0f, 2.0f}) a.climbs.push_back({{std::min(X(18.3f), X(19.0f)), 0, z - 1}, {std::max(X(18.3f), X(19.0f)), L2, z + 1}, {s, 0, 0}, true});
        for (float z : {-9.0f, 9.0f}) a.climbs.push_back({{std::min(X(20.3f), X(21.0f)), L2, z - 0.8f}, {std::max(X(20.3f), X(21.0f)), L3, z + 0.8f}, {s, 0, 0}, false});
        for (float z : {-4.0f, 4.0f}) a.climbs.push_back({{std::min(X(22.3f), X(23.0f)), L3, z - 0.8f}, {std::max(X(22.3f), X(23.0f)), ROOF, z + 0.8f}, {s, 0, 0}, false});
        // the team cannons on level 3's front, aimed across the atrium
        for (float z : {-6.5f, 6.5f}) a.cannons.push_back({{X(21.9f), L3 + 1.0f, z}, side == 0 ? 0.0f : PI, side});
        // two tube slides from level 3 down into the side pits
        for (float zs : {-1.0f, 1.0f}) {
            Slide sl; sl.colour = (uint8_t)(zs < 0 ? 4 : 5);
            sl.pts = {{X(21.4f), L3, zs * 10.6f}, {X(20.0f), L3 - 0.3f, zs * 10.6f}, {X(18.6f), L3 - 1.2f, zs * 10.4f}, {X(17.6f), 3.6f, zs * 9.8f}, {X(16.8f), 2.0f, zs * 9.2f}, {X(16.0f), 0.9f, zs * 8.6f}, {X(15.0f), 0.3f, zs * 8.0f}};
            a.slides.push_back(sl);
        }
        // the crawl tunnel from the tower's ground floor to the central pit
        a.tunnels.push_back({{std::min(X(10.5f), X(19.5f)), 0, -0.7f}, {std::max(X(10.5f), X(19.5f)), 1.25f, 0.7f}});
        box(X(10.5f), 0, -0.95f, X(19.5f), 1.45f, -0.7f, M_PANEL, 4); box(X(10.5f), 0, 0.7f, X(19.5f), 1.45f, 0.95f, M_PANEL, 4);
        box(X(10.5f), 1.25f, -0.95f, X(19.5f), 1.45f, 0.95f, M_PANEL, 4);
        // the side pits (two each end, in front of the tower) and their low padded walls
        for (float zs : {-1.0f, 1.0f}) {
            Pit p; p.lo = {std::min(X(12), X(18)), 0, zs < 0 ? -11.0f : 5.0f}; p.hi = {std::max(X(12), X(18)), 0.9f, zs < 0 ? -5.0f : 11.0f}; a.pits.push_back(p);
            box(p.lo.x - 0.3f, 0, p.lo.z - 0.3f, p.hi.x + 0.3f, 0.5f, p.lo.z, M_PAD, 6); box(p.lo.x - 0.3f, 0, p.hi.z, p.hi.x + 0.3f, 0.5f, p.hi.z + 0.3f, M_PAD, 6);
            box(p.lo.x - 0.3f, 0, p.lo.z, p.lo.x, 0.5f, p.hi.z, M_PAD, 6); box(p.hi.x, 0, p.lo.z, p.hi.x + 0.3f, 0.5f, p.hi.z, M_PAD, 6);
        }
        // ground-floor cover between the pits
        box(X(11), 0, -3.5f, X(12.5f), 1.0f, -2.2f, M_PAD, 7); box(X(11), 0, 2.2f, X(12.5f), 1.0f, 3.5f, M_PAD, 7);
        box(X(21), 0, -9, X(22), 1.0f, -7, M_PAD, col); box(X(21), 0, 7, X(22), 1.0f, 9, M_PAD, col);
        // the store (a counter at the base's back corner), spawns, the flag on the roof, the bomb sites
        a.stores.push_back({{X(27.6f), L2, -12.6f}, side});
        box(X(27), L2, -13.9f, X(28.4f), L2 + 1.05f, -13.3f, M_PANEL, 8);
        for (int k = 0; k < 6; k++) a.spawns.push_back({{X(25.5f + (k % 2) * 1.8f), L2, -7.0f + (k / 2) * 4.0f + (k % 2)}, side});
        a.flags.push_back({{X(27.0f), ROOF, 0}, side});
        a.bombSites.push_back({{X(25.0f), L2, 9.0f}, side}); a.bombSites.push_back({{X(26.0f), L3, -7.0f}, side});
        // the bridges at level 3 from the tower to the centre: a rope bridge (open) and a net tunnel
        for (int b = 0; b < 2; b++) {
            float zc = b == 0 ? 2.5f : -2.5f; float x0 = std::min(X(4.5f), X(21)), x1 = std::max(X(4.5f), X(21));
            box(x0, L3 - 0.12f, zc - 0.7f, x1, L3, zc + 0.7f, M_DECK, b == 0 ? 9 : 10);
            a.bridges.push_back({{x0, L3 - 0.2f, zc - 0.7f}, {x1, L3 + 0.6f, zc + 0.7f}, b == 1});
            if (b == 0) { box(x0, L3, zc - 0.75f, x1, L3 + 1.0f, zc - 0.7f, M_RAIL, 9); box(x0, L3, zc + 0.7f, x1, L3 + 1.0f, zc + 0.75f, M_RAIL, 9); }
            else { box(x0, L3, zc - 0.78f, x1, L3 + 2.1f, zc - 0.72f, M_NET); box(x0, L3, zc + 0.72f, x1, L3 + 2.1f, zc + 0.78f, M_NET); box(x0, L3 + 2.1f, zc - 0.78f, x1, L3 + 2.16f, zc + 0.78f, M_NET); }
        }
        // the gutter-and-belt spots (for the balls' look) along each tower's front
        a.gutters.push_back({{X(19.2f), 0.05f, 0}, side});
    }
    // the galleries along both long walls at level 2: netted on the atrium side (the safe route), an obstacle course
    for (float zs : {-1.0f, 1.0f}) {
        float z0 = zs < 0 ? -14.0f : 11.0f, z1 = zs < 0 ? -11.0f : 14.0f, zf = zs < 0 ? -11.0f : 11.0f;
        deck(-19, z0, 19, z1, L2, 11);
        for (auto seg : {std::pair<float, float>{-19, -11}, {-9, -1.5f}, {1.5f, 9}, {11, 19}}) box(seg.first, L2, zf - 0.04f, seg.second, L3 - 0.5f, zf + 0.04f, M_NET);
        for (float x : {-19.0f, -11.0f, -9.0f, -1.5f, 1.5f, 9.0f, 11.0f, 19.0f}) box(x - 0.08f, 0, zf - 0.08f, x + 0.08f, L3 - 0.5f, zf + 0.08f, M_FRAME, 3);
        // punching bags (hang from the frame: duck past) and padded rollers (crawl under)
        for (float x : {-15.0f, -5.0f, 5.0f, 15.0f}) { float zb = zs * (x < 0 ? 12.2f : 12.8f); box(x - 0.3f, L2 + 0.9f, zb - 0.3f, x + 0.3f, L2 + 2.6f, zb + 0.3f, M_PAD, 12); }
        for (float x : {-7.0f, 7.0f}) box(x - 0.35f, L2 + 1.2f, std::min(z0, z1), x + 0.35f, L2 + 1.7f, std::max(z0, z1), M_PAD, 13);
        // the cargo nets up from the ground floor (gaps in the gallery's net)
        for (float x : {-10.0f, 10.0f}) a.climbs.push_back({{x - 1, 0, zs < 0 ? -11.0f : 10.3f}, {x + 1, L2, zs < 0 ? -10.3f : 11.0f}, {0, 0, zs}, true});
        // the open crossing from the gallery to the centre's level 2 (exposed)
        deck(-1.5f, zs < 0 ? -11.0f : 6.0f, 1.5f, zs < 0 ? -6.0f : 11.0f, L2, 14);
        // the conveyor belt along the wall on the ground floor (a low step), and the hub where it meets the lift
        box(-28, 0, zs * 13.0f - 0.6f, 28, 0.45f, zs * 13.0f + 0.6f, M_PAD, 15);
        a.hub[zs < 0 ? 0 : 1] = {0, 0, zs * 14.3f};
        box(-1, 0, zs * 14.3f - 0.6f, 1, 2.2f, zs * 14.3f + 0.6f, M_PANEL, 16);   // (the hub's blower housing)
    }
    // the central structure: level 2 (12 x 12), level 3 (9 x 9), the roof (6 x 6) with the two neutral cannons
    deck(-6, -6, 6, 6, L2, 17); deck(-4.5f, -4.5f, 4.5f, 4.5f, L3, 18); deck(-3, -3, 3, 3, ROOF, 19);
    for (float x : {-6.0f, 6.0f}) for (float z : {-6.0f, 6.0f}) box(x - 0.1f, 0, z - 0.1f, x + 0.1f, L2, z + 0.1f, M_FRAME, 3);
    for (float x : {-4.5f, 4.5f}) for (float z : {-4.5f, 4.5f}) box(x - 0.1f, L2, z - 0.1f, x + 0.1f, L3, z + 0.1f, M_FRAME, 3);
    for (float x : {-3.0f, 3.0f}) for (float z : {-3.0f, 3.0f}) box(x - 0.08f, L3, z - 0.08f, x + 0.08f, ROOF + 1.2f, z + 0.08f, M_FRAME, 3);
    for (float zs : {-1.0f, 1.0f}) { box(-3, ROOF, zs * 3 - 0.05f, -1, ROOF + 1, zs * 3 + 0.05f, M_RAIL, 3); box(1, ROOF, zs * 3 - 0.05f, 3, ROOF + 1, zs * 3 + 0.05f, M_RAIL, 3); }
    box(-0.7f, L3, -0.7f, 0.7f, L3 + 1.0f, 0.7f, M_PAD, 7); box(-4, L2, -1, -3, L2 + 1, 1, M_PAD, 7); box(3, L2, -1, 4, L2 + 1, 1, M_PAD, 7);
    a.cannons.push_back({{-2.1f, ROOF + 1.0f, 0}, PI, -1}); a.cannons.push_back({{2.1f, ROOF + 1.0f, 0}, 0, -1});
    for (float zs : {-1.0f, 1.0f}) {
        a.climbs.push_back({{-1, 0, zs < 0 ? -6.7f : 6.0f}, {1, L2, zs < 0 ? -6.0f : 6.7f}, {0, 0, -zs}, true});
        a.climbs.push_back({{-1, L3, zs < 0 ? -3.7f : 3.0f}, {1, ROOF, zs < 0 ? -3.0f : 3.7f}, {0, 0, -zs}, false});
    }
    for (float xs : {-1.0f, 1.0f}) a.climbs.push_back({{xs < 0 ? -5.2f : 4.5f, L2, -1}, {xs < 0 ? -4.5f : 5.2f, L3, 1}, {-xs, 0, 0}, false});
    // the central pit round the structure's feet
    { Pit p; p.lo = {-9, 0, -8}; p.hi = {9, 0.9f, 8}; a.pits.push_back(p);
      box(-9.3f, 0, -8.3f, 9.3f, 0.5f, -8, M_PAD, 6); box(-9.3f, 0, 8, 9.3f, 0.5f, 8.3f, M_PAD, 6);
      box(-9.3f, 0, -8, -9, 0.5f, -1.2f, M_PAD, 6); box(-9.3f, 0, 1.2f, -9, 0.5f, 8, M_PAD, 6); box(9, 0, -8, 9.3f, 0.5f, -1.2f, M_PAD, 6); box(9, 0, 1.2f, 9.3f, 0.5f, 8, M_PAD, 6); }
    // the two spiral slides from the central roof down into the pit
    for (float xs : {-1.0f, 1.0f}) {
        Slide sl; sl.spiral = true; sl.colour = (uint8_t)(xs < 0 ? 4 : 5);
        sl.pts.push_back({xs * 2.2f, ROOF, -3.3f}); sl.pts.push_back({xs * 4.2f, ROOF - 0.6f, -5.4f});
        Vector3 ax{xs * 7.4f, 0, -7.0f}; float r = 1.8f; float a0 = atan2f(-5.4f - ax.z, xs * 4.2f - ax.x);
        for (int k = 1; k <= 26; k++) { float u = k / 26.0f, ang = a0 - xs * u * 4.2f * PI; sl.pts.push_back({ax.x + cosf(ang) * r, ROOF - 0.9f - u * (ROOF - 1.6f), ax.z + sinf(ang) * r}); }
        sl.pts.push_back({xs * 6.2f, 0.25f, -4.6f});
        a.slides.push_back(sl);
    }
    // the free-for-all spawns: spread over the whole hall
    for (Vector3 p : std::vector<Vector3>{{-26, L2, 8}, {26, L2, -8}, {-12, 0, 13.5f}, {12, 0, -13.5f}, {-14, L2, 12.5f}, {14, L2, -12.5f}, {0, L2, 12.5f}, {0, L2, -12.5f}, {-3, 0, -10}, {3, 0, 10}, {-25, L3, 0}, {25, L3, 0}, {-20.5f, 0, 0}, {20.5f, 0, 0}})
        a.spawns.push_back({p, -1});
    for (auto& sl : a.slides) { sl.len = 0; for (size_t i = 1; i < sl.pts.size(); i++) sl.len += Vector3Distance(sl.pts[i - 1], sl.pts[i]); }
    // fill the pits (the rest of the supply after the six hoppers' 40 each), by area
    const Config& C = Cfg(); int nCan = (int)a.cannons.size(); int left = C.ballsTotal - nCan * C.hopperMax;
    float area = 0; for (auto& p : a.pits) area += (p.hi.x - p.lo.x) * (p.hi.z - p.lo.z);
    int given = 0; for (size_t i = 0; i < a.pits.size(); i++) { Pit& p = a.pits[i]; float ar = (p.hi.x - p.lo.x) * (p.hi.z - p.lo.z); p.balls = i + 1 == a.pits.size() ? left - given : (int)(left * ar / area); p.cap = (int)(p.balls * 1.6f) + 40; given += p.balls; }
    return a;
}

// ---------------------------------------------------------------- small helpers
float World::Rand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.0f; }
float Player::Height() const { const Config& C = Cfg(); return submerged ? C.subH : (po == PO_CROUCH || po == PO_SLIDE || inTunnel) ? C.crouchH : C.height; }
Vector3 Player::Eye() const { return {pos.x, pos.y + Height() - Cfg().eyeFromTop, pos.z}; }
static float SegDist(Vector3 p, Vector3 a, Vector3 b) { Vector3 ab = Vector3Subtract(b, a); float L2v = Vector3LengthSqr(ab); float u = L2v > 0 ? std::clamp(Vector3DotProduct(Vector3Subtract(p, a), ab) / L2v, 0.0f, 1.0f) : 0; return Vector3Distance(p, Vector3Add(a, Vector3Scale(ab, u))); }
static bool InXZ(Vector3 p, Vector3 lo, Vector3 hi, float pad = 0) { return p.x > lo.x - pad && p.x < hi.x + pad && p.z > lo.z - pad && p.z < hi.z + pad; }
static bool Walkable(int m) { return m == M_DECK || m == M_PAD || m == M_PANEL; }

float World::GroundAt(Vector3 p, float r, float maxUp) const {
    float g = 0;
    for (const auto& b : arena.solids) if (Walkable(b.mat) && p.x + r > b.lo.x && p.x - r < b.hi.x && p.z + r > b.lo.z && p.z - r < b.hi.z && b.hi.y <= p.y + maxUp) g = std::max(g, b.hi.y);
    return g;
}
bool World::Ray(Vector3 o, Vector3 d, float maxT, float* tHit, int what) const {
    float best = maxT; bool any = false;
    for (const auto& b : arena.solids) {
        if (what == 1 && !BlocksShot(b.mat)) continue;
        if (what == 2 && !BlocksSight(b.mat)) continue;
        float t0 = 0, t1 = best; bool ok = true;
        for (int ax = 0; ax < 3 && ok; ax++) {
            float oo = ax == 0 ? o.x : ax == 1 ? o.y : o.z, dd = ax == 0 ? d.x : ax == 1 ? d.y : d.z, lo = ax == 0 ? b.lo.x : ax == 1 ? b.lo.y : b.lo.z, hi = ax == 0 ? b.hi.x : ax == 1 ? b.hi.y : b.hi.z;
            if (fabsf(dd) < 1e-7f) { if (oo < lo || oo > hi) ok = false; continue; }
            float a = (lo - oo) / dd, c = (hi - oo) / dd; if (a > c) std::swap(a, c);
            t0 = std::max(t0, a); t1 = std::min(t1, c); if (t0 > t1) ok = false;
        }
        if (ok && t0 < best) { best = t0; any = true; }
    }
    if (o.y + d.y * best < 0) { float tf = -o.y / std::min(-1e-6f, d.y); if (d.y < 0 && tf < best) { best = tf; any = true; } }   // (the hall floor)
    if (tHit) *tHit = best;
    return any;
}
int World::PitAt(Vector3 p, float pad) const { for (int i = 0; i < (int)arena.pits.size(); i++) { const Pit& q = arena.pits[i]; if (InXZ(p, q.lo, q.hi, pad) && p.y < q.hi.y + 0.2f && p.y > q.lo.y - 0.3f) return i; } return -1; }
static float PitSurface(const Pit& q) { return q.lo.y + 0.25f + (q.hi.y - q.lo.y - 0.25f) * std::clamp((float)q.balls / std::max(1.0f, q.cap * 0.62f), 0.0f, 1.0f); }
int World::StoreNear(Vector3 p) const { for (int i = 0; i < (int)arena.stores.size(); i++) { const Spot& s = arena.stores[i]; if (fabsf(p.y - s.p.y) < 1.0f && Vector2Distance({p.x, p.z}, {s.p.x, s.p.z}) < 2.6f) return i; } return -1; }
Vector3 World::CannonSeat(int c) const { const CannonDef& d = arena.cannons[c]; return {d.pivot.x - cosf(d.yaw0) * 1.0f, d.pivot.y - 1.0f, d.pivot.z - sinf(d.yaw0) * 1.0f}; }
int World::CannonNear(Vector3 p) const { for (int i = 0; i < (int)arena.cannons.size(); i++) { Vector3 s = CannonSeat(i); if (fabsf(p.y - s.y) < 0.8f && Vector2Distance({p.x, p.z}, {s.x, s.z}) < 1.6f) return i; } return -1; }
Vector3 World::CannonMuzzle(int c) const { const Cannon& k = cannons[c]; const CannonDef& d = arena.cannons[c]; Vector3 f{cosf(k.pitch) * cosf(k.yaw), sinf(k.pitch), cosf(k.pitch) * sinf(k.yaw)}; return Vector3Add(d.pivot, Vector3Add(Vector3Scale(f, 1.3f), {0, 0.1f, 0})); }
int World::BallsTotal() const { int n = (int)balls.size() + (int)belt.arrive.size(); for (const auto& p : arena.pits) n += p.balls; for (const auto& c : cannons) n += c.hopper; return n; }
bool World::Hittable(const Player& p) const { return p.present && p.alive && p.po != PO_SLIDE && !p.submerged && !(p.inStore && p.storeT < Cfg().storeSafe); }
const GunDef& World::Gun(const Player& p) const { return Cfg().guns[p.wield == 1 && p.gun < G_COUNT ? p.gun : G_STARTER]; }
int World::Leader() const { int b = -1; for (const auto& p : players) if (p.present && (b < 0 || p.kos > players[b].kos || (p.kos == players[b].kos && p.score > players[b].score))) b = p.id; return b; }

// a slide's feet point at arc length s
static Vector3 SlideAt(const Slide& sl, float s, Vector3* dir) {
    for (size_t i = 1; i < sl.pts.size(); i++) {
        float L = Vector3Distance(sl.pts[i - 1], sl.pts[i]);
        if (s <= L || i + 1 == sl.pts.size()) { Vector3 d = Vector3Normalize(Vector3Subtract(sl.pts[i], sl.pts[i - 1])); if (dir) *dir = d; return Vector3Lerp(sl.pts[i - 1], sl.pts[i], L > 0 ? std::clamp(s / L, 0.0f, 1.0f) : 1); }
        s -= L;
    }
    if (dir) *dir = {1, 0, 0}; return sl.pts.back();
}

// ---------------------------------------------------------------- the world
void World::Init(int md, int n, uint32_t seed) {
    arena = MakeArena(); rng = seed ? seed * 2654435761u + 7 : 1; for (int i = 0; i < 3; i++) Rand();
    mode = std::clamp(md, 0, MD_COUNT - 1);
    players.clear(); darts.clear(); balls.clear(); ents.clear(); events.clear(); belt.arrive.clear();
    cannons.assign(arena.cannons.size(), Cannon{});
    for (size_t i = 0; i < cannons.size(); i++) { cannons[i].hopper = Cfg().hopperMax; cannons[i].yaw = arena.cannons[i].yaw0; }
    n = std::clamp(n, 1, MAX_PLAYERS);
    for (int i = 0; i < n; i++) { Player p; p.id = i; p.team = i % 2; players.push_back(p); }
    teamKOs[0] = teamKOs[1] = caps[0] = caps[1] = 0; round = 0; roundWins[0] = roundWins[1] = 0; attackers = 0; winner = -1; roundWinner = -1; t = 0;
    for (int s = 0; s < 2; s++) { flags[s].home = arena.flags[s].p; flags[s].p = flags[s].home; flags[s].carrier = -1; flags[s].home_ = true; }
    noRespawn = mode == MD_BOMB;
    NewRound();
}
void World::NewRound() {
    const Config& C = Cfg(); round++; phase = PH_WARMUP; phaseT = 0; roundWinner = -1;
    if (mode == MD_BOMB) attackers = round >= C.bombSwap ? 1 : 0;
    // a new round puts everyone home (in the other modes this is the only round)
    for (auto& p : players) {
        Input keep = p.in; std::string nm = p.name; int id = p.id, tm = p.team; bool pres = p.present, bot = p.bot;
        int score = p.score, cash = p.cash, kos = p.kos, deaths = p.deaths, caps2 = p.caps, gun = p.gun, reserve = p.reserve, pick = p.rewardPick, prizes = p.prizes; bool crown = p.crown, beanie = p.beanie; uint8_t jk[16]; std::copy(p.jokes, p.jokes + 16, jk);
        bool first = round == 1;
        p = Player{}; p.id = id; p.team = tm; p.present = pres; p.bot = bot; p.name = nm; p.in = keep; p.rewardPick = pick; p.prizes = prizes; p.crown = crown; p.beanie = beanie; std::copy(jk, jk + 16, p.jokes);
        if (!first) { p.score = score; p.cash = cash; p.kos = kos; p.deaths = deaths; p.caps = caps2; p.gun = gun; p.reserve = std::max(reserve, C.dartStart); }
        else { p.reserve = C.dartStart; }
        if (p.gun < G_COUNT) p.mag[1] = C.guns[p.gun].mag;
        p.mag[0] = 1;
        Respawn(p); p.respawnT = 0;
    }
    darts.clear(); for (auto& e : ents) e.dead = true;
    if (mode == MD_BOMB) {
        bomb = Bomb{};
        std::vector<int> att; for (auto& p : players) if (p.present && p.team == attackers) att.push_back(p.id);
        if (!att.empty()) { int c = att[(int)(Rand() * att.size()) % att.size()]; bomb.carrier = c; players[c].bomb = true; bomb.p = players[c].pos; }
    }
    Emit(EV_ROUND, {0, 0, 0}, -1, -1, (float)round);
}
void World::Respawn(Player& p) {
    const Config& C = Cfg();
    // the team's tower (free for all: the spread spawn farthest from everyone else)
    std::vector<const Spot*> opts;
    for (const auto& s : arena.spawns) if ((mode == MD_FFA && s.team < 0) || (mode != MD_FFA && s.team == p.team)) opts.push_back(&s);
    if (opts.empty()) for (const auto& s : arena.spawns) opts.push_back(&s);
    const Spot* best = opts[0]; float bd = -1;
    for (const Spot* s : opts) {
        float d = 1e9f; for (const auto& q : players) if (q.id != p.id && q.alive && q.present) d = std::min(d, Vector3Distance(q.pos, s->p));
        d += Rand() * 6;   // (a little shuffle so spawns aren't predictable)
        if (d > bd) { bd = d; best = s; }
    }
    p.pos = best->p; p.vel = {}; p.po = PO_STAND; p.alive = true; p.hp = C.health; p.sinceHurt = 10; p.fallTop = p.pos.y; p.grounded = true;
    p.yaw = mode == MD_FFA ? atan2f(-p.pos.z, -p.pos.x) : (p.team == 0 ? 0 : PI); p.pitch = 0;
    p.ball = -1; p.cannon = -1; p.carry = -1; p.climb = p.slide = -1; p.drive = -1; p.streak = 0; p.rewardReady = p.rewardUsed = 0;
    p.mag[0] = 1; p.wield = p.gun < G_COUNT ? 1 : 0; p.reloadT = 0; p.cool = 0; p.burstLeft = 0; p.spin = 0; p.throwT = 0; p.grabT = 0; p.fingerT = 0; p.plantT = p.defuseT = 0; p.storeT = 0;
    p.lastHitBy[0] = p.lastHitBy[1] = -1;
    Emit(EV_RESPAWN, p.pos, p.id);
}

// ---------------------------------------------------------------- damage, knockouts, score
void World::Hurt(Player& v, float dmg, int by, const char* how, bool cannon, bool ko) {
    if (!v.alive || dmg <= 0) return;
    if (v.drive >= 0 && v.drive < (int)ents.size() && ents[v.drive].kind == E_TANK && !ents[v.drive].dead) {   // (in the tank: its armor takes it)
        Ent& tk = ents[v.drive]; tk.armor -= ko ? 100 : dmg; Emit(EV_HIT, tk.p, v.id, by, ko ? 100 : dmg);
        if (tk.armor <= 0) { tk.dead = true; v.drive = -1; v.po = PO_STAND; }
        return;
    }
    if (by >= 0 && by != v.id) { if (v.lastHitBy[0] != by) { v.lastHitBy[1] = v.lastHitBy[0]; v.lastHitT[1] = v.lastHitT[0]; } v.lastHitBy[0] = by; v.lastHitT[0] = t; }
    v.hp -= ko ? v.hp + 1 : dmg; v.sinceHurt = 0;
    Emit(EV_HIT, v.pos, v.id, by, ko ? 999 : dmg);
    if (v.hp <= 0) KnockOut(v, by, how, cannon);
}
static void AddScore(World& w, Player& p, int pts) {
    p.score += pts; p.cash += pts; p.streak += pts;
    for (int r = 0; r < 6; r++) if ((p.rewardPick >> r) & 1) if (!((p.rewardReady >> r) & 1) && p.streak >= Cfg().rewards[r].streak) { p.rewardReady |= (uint8_t)(1 << r); w.Emit(EV_STREAK, p.pos, p.id, -1, (float)r); }
}
void World::KnockOut(Player& v, int by, const char* how, bool cannon) {
    const Config& C = Cfg(); (void)how;
    if (!v.alive) return;
    v.alive = false; v.hp = 0; v.deaths++; v.respawnT = mode == MD_BOMB ? 1e9f : C.modes[mode].respawn; v.po = PO_DEAD;
    // what you held falls: a ball goes loose (and on to the belt), the flag and the bomb drop where you were
    if (v.ball >= 0 && v.ball < (int)balls.size()) { Ball& b = balls[v.ball]; b.st = BS_LOOSE; b.holder = -1; b.v = {0, 1, 0}; b.p = Vector3Add(v.pos, {0, 1.0f, 0}); }
    v.ball = -1;
    if (v.cannon >= 0) { cannons[v.cannon].op = -1; v.cannon = -1; }
    if (v.carry >= 0) { Flag& f = flags[v.carry]; f.carrier = -1; f.home_ = false; f.dropT = 0; f.p = {v.pos.x, GroundAt(v.pos, 0.2f, 0.3f), v.pos.z}; Emit(EV_FLAG_DROP, f.p, v.id, -1, (float)v.carry); v.carry = -1; }
    if (v.bomb) { v.bomb = false; bomb.carrier = -1; bomb.p = {v.pos.x, GroundAt(v.pos, 0.2f, 0.3f), v.pos.z}; }
    if (v.drive >= 0 && v.drive < (int)ents.size()) { ents[v.drive].dead = true; v.drive = -1; }
    v.streak = 0; v.rewardReady = 0; v.rewardUsed = 0;
    Emit(EV_KO, v.pos, v.id, by, cannon ? 1.0f : 0.0f);
    if (by >= 0 && by < (int)players.size() && by != v.id) {
        Player& k = players[by];
        if (Enemies(k, v)) { k.kos++; if (mode != MD_FFA) teamKOs[k.team]++; AddScore(*this, k, cannon ? C.scoreCannonKO : C.scoreKO); }
    }
    for (int i = 0; i < 2; i++) { int a = v.lastHitBy[i]; if (a >= 0 && a != by && a != v.id && t - v.lastHitT[i] <= C.assistWindow && a < (int)players.size()) { AddScore(*this, players[a], C.scoreAssist); Emit(EV_ASSIST, v.pos, a, v.id); } }
}

// ---------------------------------------------------------------- movement (spec "Movement and traversal")
// a body moving in the arena: a circle in xz against every solid it overlaps in height, landing on walkable tops
static void MoveBody(const World& w, Vector3& pos, Vector3& vel, bool& grounded, float radius, float height, float dt) {
    const Config& C = Cfg();
    Vector3 prev = pos; Vector3 np = Vector3Add(pos, Vector3Scale(vel, dt));
    float X = w.arena.halfX - radius, Z = w.arena.halfZ - radius; np.x = std::clamp(np.x, -X, X); np.z = std::clamp(np.z, -Z, Z);
    // heads: a slab overhead stops a rise
    if (vel.y > 0) for (const auto& b : w.arena.solids) {
        if (!BlocksMove(b.mat) || !InXZ(np, b.lo, b.hi, radius * 0.6f)) continue;
        if (prev.y + height <= b.lo.y + 0.02f && np.y + height > b.lo.y) { np.y = b.lo.y - height; vel.y = 0; }
    }
    float step = grounded ? C.stepUp : 0.05f;
    for (int pass = 0; pass < 2; pass++) for (const auto& b : w.arena.solids) {
        if (!BlocksMove(b.mat)) continue;
        if (b.hi.y <= np.y + step || b.lo.y >= np.y + height) continue;
        float cx = std::clamp(np.x, b.lo.x, b.hi.x), cz = std::clamp(np.z, b.lo.z, b.hi.z), dx = np.x - cx, dz = np.z - cz, d = sqrtf(dx * dx + dz * dz);
        if (d >= radius) continue;
        if (d < 1e-4f) { float px = std::min(np.x - b.lo.x, b.hi.x - np.x), pz = std::min(np.z - b.lo.z, b.hi.z - np.z); if (px < pz) np.x = np.x - b.lo.x < b.hi.x - np.x ? b.lo.x - radius : b.hi.x + radius; else np.z = np.z - b.lo.z < b.hi.z - np.z ? b.lo.z - radius : b.hi.z + radius; }
        else { np.x = cx + dx / d * radius; np.z = cz + dz / d * radius; float vn = (vel.x * dx + vel.z * dz) / d; if (vn < 0) { vel.x -= vn * dx / d; vel.z -= vn * dz / d; } }
    }
    float g = w.GroundAt(np, radius * 0.6f, std::max(0.0f, prev.y - np.y) + step);
    if (np.y <= g) { np.y = g; if (vel.y < 0) vel.y = 0; grounded = true; } else grounded = np.y - g < 0.03f && vel.y <= 0;
    if (np.y + height > w.arena.ceil) { np.y = w.arena.ceil - height; vel.y = std::min(vel.y, 0.0f); }
    pos = np;
}
static bool Headroom(const World& w, Vector3 p, float r, float from, float to) {
    for (const auto& b : w.arena.solids) if (BlocksMove(b.mat) && InXZ(p, b.lo, b.hi, r * 0.8f) && b.lo.y < p.y + to && b.hi.y > p.y + from) return false;
    return true;
}
void World::StepPlayer(Player& p) {
    const Config& C = Cfg(); const Input& in = p.in; float dt = STEP;
    p.poT += dt;
    if (p.po != PO_CANNON) { p.yaw = in.yaw; p.pitch = std::clamp(in.pitch, -1.5f, 1.5f); }
    Vector3 fwd{cosf(p.yaw), 0, sinf(p.yaw)}, rgt{-sinf(p.yaw), 0, cosf(p.yaw)};
    Vector3 wish = Vector3Add(Vector3Scale(fwd, in.moveX), Vector3Scale(rgt, in.moveZ)); float wl = Vector3Length(wish); if (wl > 1) wish = Vector3Scale(wish, 1 / wl);
    // a slide: along its path at slide speed; nobody can stop or leave it (or hit you in it) until the exit
    if (p.po == PO_SLIDE) {
        const Slide& sl = arena.slides[p.slide]; p.slideS += C.slide * dt; Vector3 d;
        p.pos = SlideAt(sl, p.slideS, &d);
        if (p.slideS >= sl.len) { p.po = PO_STAND; p.vel = Vector3Scale(d, C.slide * 0.5f); p.vel.y = 0; p.grounded = false; p.fallTop = p.pos.y; p.slide = -1; }
        return;
    }
    // a ladder or cargo net: up and down at climb speed; the top mantles onto the deck; jump lets go
    if (p.po == PO_CLIMB) {
        const Climb& c = arena.climbs[p.climb]; Vector3 mid{(c.lo.x + c.hi.x) / 2, 0, (c.lo.z + c.hi.z) / 2};
        // hold the face: against it, along it within the net
        if (fabsf(c.into.x) > 0.5f) { p.pos.x = c.into.x > 0 ? c.hi.x - C.radius : c.lo.x + C.radius; p.pos.z = std::clamp(p.pos.z + in.moveZ * c.into.x * -0.0f, c.lo.z + 0.3f, c.hi.z - 0.3f); }
        else { p.pos.z = c.into.z > 0 ? c.hi.z - C.radius : c.lo.z + C.radius; p.pos.x = std::clamp(p.pos.x, c.lo.x + 0.3f, c.hi.x - 0.3f); }
        (void)mid; p.vel = {};
        float up = Vector3DotProduct(wish, c.into) > 0.3f ? 1.0f : Vector3DotProduct(wish, c.into) < -0.3f ? -1.0f : in.moveX;
        p.pos.y += up * C.climb * dt;
        if (p.pos.y >= c.hi.y) { p.po = PO_STAND; p.pos = {p.pos.x + c.into.x * 0.9f, c.hi.y, p.pos.z + c.into.z * 0.9f}; p.grounded = true; p.fallTop = p.pos.y; p.climb = -1; Emit(EV_CLIMB, p.pos, p.id); }
        else if (in.jump || (p.pos.y <= c.lo.y + 0.01f && up < 0)) { p.pos.y = std::max(p.pos.y, c.lo.y); p.po = PO_STAND; p.climb = -1; p.vel = Vector3Scale(c.into, -1.5f); p.grounded = false; p.fallTop = p.pos.y; }
        return;
    }
    if (p.po == PO_CANNON || p.po == PO_DRIVE) { p.vel = {}; return; }
    // the postures: crouch held (standing needs headroom: a tunnel keeps you down)
    bool want = in.crouch;
    if (!want && (p.po == PO_CROUCH) && !Headroom(*this, p.pos, C.radius, C.crouchH - 0.05f, C.height)) want = true;
    p.po = want ? PO_CROUCH : PO_STAND;
    // where you are: a tunnel, a pit (wading, submerged), a bridge
    p.inTunnel = false; for (const auto& tn : arena.tunnels) if (InXZ(p.pos, tn.lo, tn.hi) && p.pos.y < tn.hi.y - 0.3f) p.inTunnel = true;
    int pit = PitAt(p.pos); float surface = pit >= 0 ? PitSurface(arena.pits[pit]) : 0;
    p.inPit = pit >= 0 && p.pos.y < surface - 0.1f;
    p.submerged = p.inPit && p.po == PO_CROUCH && surface >= p.pos.y + C.subH + 0.05f;
    p.onBridge = false; for (const auto& br : arena.bridges) if (InXZ(p.pos, br.lo, br.hi) && p.pos.y > br.lo.y && p.pos.y < br.hi.y) p.onBridge = true;
    // ladders: walk into a net or ladder from its foot
    if (p.ball < 0 || true) for (int i = 0; i < (int)arena.climbs.size(); i++) {
        const Climb& c = arena.climbs[i];
        if (!InXZ(p.pos, c.lo, c.hi, 0.15f) || fabsf(p.pos.y - c.lo.y) > 0.35f || Vector3DotProduct(wish, c.into) < 0.5f || p.carry >= 0 && false) continue;
        bool crowded = false; for (const auto& e : ents) if (!e.dead && e.kind == E_KID && Enemies(p, p) == false && e.team != p.team && InXZ(e.p, c.lo, c.hi, 0.6f) && fabsf(e.p.y - c.lo.y) < 1.0f) crowded = true;
        if (crowded) continue;   // (the kids are in the way)
        p.po = PO_CLIMB; p.climb = i; p.poT = 0; p.vel = {}; p.pos.y = c.lo.y; return;
    }
    // slides: step into the top
    for (int i = 0; i < (int)arena.slides.size(); i++) {
        const Slide& sl = arena.slides[i]; Vector3 top = sl.pts[0];
        if (fabsf(p.pos.y - top.y) > 0.5f || Vector2Distance({p.pos.x, p.pos.z}, {top.x, top.z}) > 0.75f) continue;
        Vector3 into = Vector3Normalize(Vector3Subtract(sl.pts[1], sl.pts[0])); into.y = 0; into = Vector3Normalize(into);
        if (Vector3DotProduct(wish, into) < 0.3f && !in.use) continue;
        bool crowded = false; for (const auto& e : ents) if (!e.dead && e.kind == E_KID && e.team != p.team && Vector3Distance(e.p, top) < 1.2f) crowded = true;
        if (crowded) continue;
        if (p.cannon >= 0) continue;
        p.po = PO_SLIDE; p.slide = i; p.slideS = 0; p.vel = {}; p.submerged = false; Emit(EV_SLIDE, top, p.id, -1, (float)i); return;
    }
    // speed from the traversal table
    float spd = C.run;
    if (p.inTunnel) spd = C.tunnel; else if (p.po == PO_CROUCH) spd = C.crouch;
    if (p.onBridge) spd = std::min(spd, C.bridge);
    if (p.inPit) spd = p.submerged ? C.submerged : std::min(spd, C.wade);
    if (p.vacuuming) spd *= C.vacuumSpeed;
    if (p.carry >= 0) spd *= C.flagSpeed;
    if (p.wield == 1 && p.gun == G_BELT) spd *= Cfg().guns[G_BELT].slow;
    if (p.fingerT > 0) spd *= 1.0f;
    Vector3 wantV = Vector3Scale(wish, spd);
    float k = p.grounded ? 1.0f : C.airControl;
    p.vel.x += (wantV.x - p.vel.x) * std::min(1.0f, dt * 14 * k); p.vel.z += (wantV.z - p.vel.z) * std::min(1.0f, dt * 14 * k);
    if (in.jump && p.grounded && p.po == PO_STAND && !p.submerged) { p.vel.y = sqrtf(2 * C.gravity * C.jumpH); p.grounded = false; }
    p.vel.y -= C.gravity * dt;
    if (p.inPit) p.vel.y = std::max(p.vel.y, -6.0f);   // (the balls slow a fall into the pit)
    bool wasGround = p.grounded; float prevY = p.pos.y;
    MoveBody(*this, p.pos, p.vel, p.grounded, C.radius, p.Height(), dt);
    // falls: up to 3 m is safe, a pit is safe from any height
    if (!wasGround) p.fallTop = std::max(p.fallTop, prevY);
    if (wasGround && p.grounded) p.fallTop = p.pos.y;
    if (!wasGround && p.grounded) {
        float drop = p.fallTop - p.pos.y; bool soft = PitAt(p.pos, 0.1f) >= 0;
        if (drop > 0.6f) Emit(EV_LAND, p.pos, p.id, -1, soft ? -drop : drop);
        if (drop > C.safeFall && !soft) Hurt(p, (drop - C.safeFall) * C.fallDmgPerM, -1, "the fall");
        p.fallTop = p.pos.y;
    }
    // kids get under your feet: push out of them
    for (const auto& e : ents) if (!e.dead && e.kind == E_KID && e.team != p.team) {
        float dx = p.pos.x - e.p.x, dz = p.pos.z - e.p.z, d = sqrtf(dx * dx + dz * dz);
        if (d < 0.55f && d > 1e-3f && fabsf(p.pos.y - e.p.y) < 1.2f) { p.pos.x = e.p.x + dx / d * 0.55f; p.pos.z = e.p.z + dz / d * 0.55f; }
    }
}

// ---------------------------------------------------------------- foam guns, the knife, the vacuum (spec "Weapons")
void World::FireDart(Player& p, const GunDef& g, Vector3 from, Vector3 dir) {
    const Config& C = Cfg();
    for (int k = 0; k < g.pellets; k++) {
        float spread = g.spread * DEG2RAD;
        if (p.in.aim && g.ads) spread *= 0.45f;
        if (p.onBridge) spread *= C.bridgeSpread;
        if (!p.grounded) spread *= 1.6f;
        if (Vector3Length({p.vel.x, 0, p.vel.z}) > 1) spread *= 1.25f;
        float a = (Rand() - 0.5f) * 2 * spread, b = (Rand() - 0.5f) * 2 * spread;
        Vector3 up{0, 1, 0}, r = Vector3Normalize(Vector3CrossProduct(dir, up)); if (Vector3Length(r) < 0.1f) r = {1, 0, 0}; Vector3 u = Vector3CrossProduct(r, dir);
        Vector3 d = Vector3Normalize(Vector3Add(dir, Vector3Add(Vector3Scale(r, a), Vector3Scale(u, b))));
        Dart dt; dt.p = from; dt.v = Vector3Scale(d, g.speed); dt.owner = p.id; dt.team = mode == MD_FFA ? -1 - p.id : p.team; dt.dmg = g.dmg; dt.grav = g.grav; dt.knock = g.knock; dt.splash = g.splash;
        dt.kind = (uint8_t)(g.splash > 0 ? 2 : g.knock > 0 ? 1 : 0);
        darts.push_back(dt);
    }
    Emit(g.splash > 0 ? EV_ROCKET : EV_SHOT, from, p.id, -1, (float)(&g - &C.guns[0]));
}
void World::StepWeapons(Player& p) {
    const Config& C = Cfg(); const Input& in = p.in; float dt = STEP;
    p.cool = std::max(0.0f, p.cool - dt); p.knifeT = std::max(0.0f, p.knifeT - dt); p.knifeSwing = std::max(0.0f, p.knifeSwing - dt); p.swapT = std::max(0.0f, p.swapT - dt);
    p.fingerT = std::max(0.0f, p.fingerT - dt);
    bool canAct = p.alive && p.po != PO_CLIMB && p.po != PO_SLIDE && p.po != PO_CANNON && p.po != PO_DRIVE && !p.submerged;
    // switching: 1 the starter, 2 the bought gun (a flag carrier keeps the starter)
    if (in.slot >= 0 && in.slot != p.wield && !(in.slot == 1 && p.gun >= G_COUNT) && !(p.carry >= 0 && in.slot == 1)) { p.wield = in.slot; p.swapT = 0.3f; p.reloadT = 0; p.burstLeft = 0; p.spin = 0; }
    if (p.carry >= 0 && p.wield == 1) { p.wield = 0; p.swapT = 0.3f; }
    // the knife: always carried, the only attack in a crawl tunnel; from behind it knocks out outright
    if (in.knife && canAct && p.knifeT <= 0 && p.ball < 0) {
        p.knifeT = C.knifeCool; p.knifeSwing = 0.3f; Emit(EV_KNIFE, p.pos, p.id);
        Vector3 eye = p.Eye(), look = p.Look(); int best = -1; float bd = 1e9f;
        for (auto& v : players) {
            if (!Enemies(p, v) || !Hittable(v)) continue;
            Vector3 c{v.pos.x, v.pos.y + v.Height() * 0.6f, v.pos.z}; Vector3 to = Vector3Subtract(c, eye); float d = Vector3Length(to);
            if (d > C.knifeReach + C.radius || Vector3DotProduct(Vector3Scale(to, 1 / std::max(0.01f, d)), look) < 0.6f || !ShotClear(eye, c)) continue;
            if (d < bd) { bd = d; best = v.id; }
        }
        if (best >= 0) {
            Player& v = players[best]; Vector3 vf{cosf(v.yaw), 0, sinf(v.yaw)}, fromV = Vector3Normalize({p.pos.x - v.pos.x, 0, p.pos.z - v.pos.z});
            bool back = Vector3DotProduct(vf, fromV) < -0.5f;   // (the attacker is behind the victim)
            Hurt(v, C.knifeDmg, p.id, back ? "a knife in the back" : "the knife", false, back);
        } else {   // (the knife pops a health box, a drone, an RC car)
            for (auto& e : ents) if (!e.dead && (e.kind == E_HEALTHBOX || e.kind == E_RCCAR || e.kind == E_DRONE) && e.team != p.team && Vector3Distance(e.p, eye) < C.knifeReach + 0.5f) { e.hp -= C.knifeDmg; if (e.hp <= 0) e.dead = true; break; }
        }
    }
    // the vacuum: hold to suck up foam darts in a 4 m cone ahead (anyone's); 70% speed; no firing
    p.vacuuming = in.vacuum && canAct && p.ball < 0 && p.reserve < C.dartMax;
    if (p.vacuuming) {
        p.vacuumT += dt; float want = C.vacuumRate * dt; static float acc[MAX_PLAYERS] = {}; acc[p.id % MAX_PLAYERS] += want;
        Vector3 eye = p.Eye(), look = p.Look();
        for (size_t i = 0; i < darts.size() && acc[p.id % MAX_PLAYERS] >= 1 && p.reserve < C.dartMax; i++) {
            Dart& d = darts[i]; if (d.live) continue;
            Vector3 to = Vector3Subtract(d.p, eye); float L = Vector3Length(to);
            if (L > C.vacuumRange + 1.0f || Vector2Distance({d.p.x, d.p.z}, {p.pos.x, p.pos.z}) > C.vacuumRange) continue;
            if (Vector3DotProduct(Vector3Normalize({to.x, 0, to.z}), Vector3Normalize({look.x, 0, look.z})) < cosf(C.vacuumCone * DEG2RAD)) continue;
            if (d.p.y < p.pos.y - 2.5f || d.p.y > p.pos.y + 2.0f) continue;
            p.reserve++; acc[p.id % MAX_PLAYERS] -= 1; Emit(EV_VACUUM, d.p, p.id, d.owner); darts.erase(darts.begin() + i); i--;
        }
        acc[p.id % MAX_PLAYERS] = std::min(acc[p.id % MAX_PLAYERS], 1.0f);
        return;
    }
    if (!canAct || p.swapT > 0 || p.ball >= 0 || p.fingerT > 0) { p.spin = std::max(0.0f, p.spin - dt * 2); return; }
    if (p.inTunnel) return;   // (knife only in here)
    const GunDef& g = Gun(p); int slot = p.wield == 1 && p.gun < G_COUNT ? 1 : 0;
    int magSize = g.mag;
    // reloading
    if (p.reloadT > 0) {
        p.reloadT -= dt;
        if (p.reloadT <= 0) { int need = magSize - p.mag[slot]; int can = std::min(need, p.reserve / std::max(1, g.dartCost)); p.mag[slot] += can; p.reserve -= can * g.dartCost; }
        return;
    }
    bool wantReload = (in.reload && p.mag[slot] < magSize) || (p.mag[slot] <= 0 && (in.fire || slot == 0));
    if (wantReload) { if (p.reserve >= g.dartCost) { p.reloadT = g.reload; Emit(EV_RELOAD, p.pos, p.id, -1, (float)(&g - &C.guns[0])); } else if (in.fire && p.cool <= 0) { Emit(EV_DRY, p.pos, p.id); p.cool = 0.4f; } return; }
    // the belt-fed spins up before it fires
    if (g.spinUp > 0) { if (in.fire) p.spin = std::min(g.spinUp, p.spin + dt); else p.spin = std::max(0.0f, p.spin - dt * 1.5f); if (p.spin < g.spinUp) return; }
    Vector3 eye = p.Eye(), look = p.Look();
    auto muzzle = [&]() { Vector3 r = Vector3Normalize(Vector3CrossProduct(look, {0, 1, 0})); float side = g.key == "dual" ? (p.dualSide ? -0.18f : 0.18f) : 0.16f; return Vector3Add(eye, Vector3Add(Vector3Scale(look, 0.45f), Vector3Add(Vector3Scale(r, side), {0, -0.12f, 0}))); };
    if (p.burstLeft > 0) {   // (the rest of a three-round burst)
        p.burstT -= dt;
        if (p.burstT <= 0 && p.mag[slot] > 0) { FireDart(p, g, muzzle(), look); p.mag[slot]--; p.burstLeft--; p.burstT = 0.07f; }
        if (p.mag[slot] <= 0) p.burstLeft = 0;
        return;
    }
    if (in.fire && p.cool <= 0 && p.mag[slot] > 0) {
        FireDart(p, g, muzzle(), look); p.mag[slot]--; p.cool = 1.0f / std::max(0.1f, g.rate); p.dualSide ^= 1;
        if (g.burst > 1) { p.burstLeft = std::min(g.burst - 1, p.mag[slot]); p.burstT = 0.07f; }
        if (slot == 0 && p.mag[0] <= 0 && p.reserve > 0) { p.reloadT = g.reload; }
        p.storeT = 99;   // (firing ends the store's grace)
    }
}

// ---------------------------------------------------------------- balls in hand (spec "Thrown balls")
void World::ThrowBall(Player& p) {
    const Config& C = Cfg(); if (p.ball < 0) return;
    Ball& b = balls[p.ball]; Vector3 look = p.Look();
    float pitch = asinf(std::clamp(look.y, -1.0f, 1.0f)) + C.throwLoft * DEG2RAD; Vector3 d{cosf(pitch) * cosf(p.yaw), sinf(pitch), cosf(pitch) * sinf(p.yaw)};
    b.st = BS_LIVE; b.holder = -1; b.thrower = p.id; b.team = mode == MD_FFA ? -1 - p.id : p.team; b.fromCannon = false; b.age = 0;
    b.p = Vector3Add(p.Eye(), Vector3Add(Vector3Scale(d, 0.5f), {0, -0.1f, 0})); b.v = Vector3Scale(d, C.throwSpeed);
    p.ball = -1; Emit(EV_THROW, b.p, p.id);
}
void World::StepBallHands(Player& p) {
    const Config& C = Cfg(); const Input& in = p.in; float dt = STEP;
    bool canAct = p.alive && p.po != PO_CLIMB && p.po != PO_SLIDE && p.po != PO_CANNON && p.po != PO_DRIVE && !p.submerged && p.carry < 0 && p.fingerT <= 0;
    if (p.throwT > 0) { p.throwT -= dt; if (p.throwT <= 0) ThrowBall(p); return; }
    if (p.ball >= 0) { if (in.fire && canAct && !p.inTunnel) p.throwT = C.throwWindup; return; }
    if (!canAct) { p.grabT = 0; return; }
    // reach into a pit (0.4 s): in it, or standing at its edge
    int pit = PitAt(p.pos, 0.8f);
    if (pit >= 0 && arena.pits[pit].balls > 0 && (in.grab || p.grabT > 0)) {
        p.grabT += dt;
        if (p.grabT >= C.grabTime) { p.grabT = 0; arena.pits[pit].balls--; Ball b; b.st = BS_HELD; b.holder = p.id; b.p = p.Eye(); balls.push_back(b); p.ball = (int)balls.size() - 1; Emit(EV_GRAB, p.pos, p.id, -1, (float)pit); }
        return;
    }
    p.grabT = 0;
    // walk over a loose ball
    for (int i = 0; i < (int)balls.size(); i++) {
        Ball& b = balls[i]; if (b.st != BS_LOOSE || Vector3Length(b.v) > 6) continue;
        if (Vector2Distance({p.pos.x, p.pos.z}, {b.p.x, b.p.z}) < C.radius + 0.35f && b.p.y > p.pos.y - 0.3f && b.p.y < p.pos.y + 1.0f) { b.st = BS_HELD; b.holder = p.id; p.ball = i; Emit(EV_GRAB, p.pos, p.id, -1, -1); break; }
    }
}

// ---------------------------------------------------------------- darts in flight and on the floor
void World::StepDarts() {
    const Config& C = Cfg(); float dt = STEP;
    for (size_t i = 0; i < darts.size(); i++) {
        Dart& d = darts[i]; d.age += dt;
        if (!d.live) continue;
        float spd = Vector3Length(d.v); int n = std::max(1, (int)ceilf(spd * dt / 0.25f)); float h = dt / n; bool stop = false;
        for (int s = 0; s < n && !stop; s++) {
            Vector3 prev = d.p; d.v.y -= C.gravity * d.grav * h; d.p = Vector3Add(d.p, Vector3Scale(d.v, h));
            Vector3 seg = Vector3Subtract(d.p, prev); float L = Vector3Length(seg); if (L < 1e-5f) continue; Vector3 dir = Vector3Scale(seg, 1 / L);
            // players first (the near ones along this little segment)
            int hit = -1; float ht = 1e9f;
            for (auto& v : players) {
                if (v.id == d.owner || !Hittable(v)) continue;
                if (d.team >= 0 && v.team == d.team && mode != MD_FFA) continue;
                Vector3 a{v.pos.x, v.pos.y + 0.15f, v.pos.z}, b{v.pos.x, v.pos.y + v.Height() - 0.15f, v.pos.z};
                if (SegDist(d.p, a, b) < C.radius + 0.06f || SegDist(Vector3Lerp(prev, d.p, 0.5f), a, b) < C.radius + 0.06f) { float tt = Vector3Distance(prev, v.pos); if (tt < ht) { ht = tt; hit = v.id; } }
            }
            // the reward entities: kids (darts bounce off), drones, cars, the tank, health boxes
            int he = -1; for (int e = 0; e < (int)ents.size(); e++) { Ent& en = ents[e]; if (en.dead) continue; float r = en.kind == E_KID ? 0.35f : en.kind == E_TANK ? 1.0f : en.kind == E_DRONE ? 0.4f : en.kind == E_RCCAR ? 0.35f : en.kind == E_HEALTHBOX ? 0.35f : 0;
                if (r <= 0 || (en.kind != E_KID && en.team == d.team)) continue; Vector3 c = Vector3Add(en.p, {0, en.kind == E_KID ? 0.6f : en.kind == E_TANK ? 0.7f : 0.2f, 0}); if (Vector3Distance(c, d.p) < r + (en.kind == E_KID ? 0.25f : 0)) { he = e; break; } }
            float tw = 1e9f; bool wall = Ray(prev, dir, L, &tw, 1);
            if (hit >= 0 && (!wall || Vector3Distance(prev, players[hit].pos) - C.radius < tw + 0.2f)) {
                Player& v = players[hit];
                if (d.kind == 2) { d.p = Vector3Lerp(prev, d.p, 0.5f); stop = true; break; }
                Hurt(v, d.dmg, d.owner, d.kind == 1 ? "a mega dart" : d.kind == 3 ? "a turret" : "a dart");
                if (d.knock > 0 && v.alive) { Vector3 kb = Vector3Normalize({d.v.x, 0, d.v.z}); v.vel = Vector3Add(v.vel, Vector3Scale(kb, d.knock * 5)); }
                d.live = false; d.p = {v.pos.x + (Rand() - 0.5f) * 0.6f, v.pos.y, v.pos.z + (Rand() - 0.5f) * 0.6f}; d.p.y = GroundAt(d.p, 0.05f, 0.5f) + 0.02f; d.v = {}; d.age = 0; stop = true; break;
            }
            if (he >= 0) {
                Ent& en = ents[he];
                if (en.kind != E_KID) { if (en.kind == E_TANK) en.armor -= d.dmg; else if (en.kind == E_HEALTHBOX) en.hp -= 1; else en.hp -= d.dmg; if ((en.kind == E_TANK ? en.armor : en.hp) <= 0) { en.dead = true; if (en.kind == E_TANK && en.owner >= 0) { players[en.owner].drive = -1; players[en.owner].po = PO_STAND; } } Emit(EV_HIT, en.p, -1, d.owner, d.dmg); }
                d.live = false; d.p.y = GroundAt(d.p, 0.05f, 0.3f) + 0.02f; d.v = {}; d.age = 0; stop = true; break;
            }
            if (wall) { d.p = Vector3Add(prev, Vector3Scale(dir, std::max(0.0f, tw - 0.03f))); stop = true; break; }
        }
        if (stop || d.p.y < 0.02f) {
            if (d.kind == 2) {   // the foam rocket bursts: splash within 2 m
                Emit(EV_SPLASH, d.p, d.owner);
                for (auto& v : players) { if (!Hittable(v)) continue; if (d.team >= 0 && v.team == d.team && mode != MD_FFA && v.id != d.owner) continue; Vector3 c{v.pos.x, v.pos.y + 0.9f, v.pos.z}; float r = Vector3Distance(c, d.p); if (r < d.splash + C.radius && ShotClear(d.p, c)) Hurt(v, d.dmg * (1 - 0.5f * std::min(1.0f, r / d.splash)), d.owner, "a foam rocket"); }
                for (auto& en : ents) if (!en.dead && en.kind != E_KID && Vector3Distance(en.p, d.p) < d.splash + 0.5f) { if (en.kind == E_TANK) en.armor -= d.dmg; else en.hp -= d.dmg; if ((en.kind == E_TANK ? en.armor : en.hp) <= 0) en.dead = true; }
                darts.erase(darts.begin() + i); i--; continue;
            }
            if (d.live) { d.live = false; d.v = {}; d.age = 0; d.p.y = std::max(d.p.y, 0.02f); float g = GroundAt(d.p, 0.03f, 0.05f); if (d.p.y - g > 0.25f) d.p.y = GroundAt(d.p, 0.03f, 0.0f) + 0.02f; else d.p.y = g + 0.02f; }
        }
    }
    // floor darts last 60 s; never more than the cap (oldest first)
    darts.erase(std::remove_if(darts.begin(), darts.end(), [&](const Dart& d) { return !d.live && d.age > C.dartFloorLife; }), darts.end());
    if ((int)darts.size() > C.dartCap) {
        int extra = (int)darts.size() - C.dartCap;
        std::vector<std::pair<float, int>> floor; for (int i = 0; i < (int)darts.size(); i++) if (!darts[i].live) floor.push_back({-darts[i].age, i});
        std::sort(floor.begin(), floor.end()); std::vector<char> kill(darts.size(), 0); for (int k = 0; k < extra && k < (int)floor.size(); k++) kill[floor[k].second] = 1;
        size_t w = 0; for (size_t i = 0; i < darts.size(); i++) if (!kill[i]) darts[w++] = darts[i]; darts.resize(w);
    }
}

// ---------------------------------------------------------------- balls in flight and loose (spec "Ball cannons and the conveyor loop")
void World::ToBelt(int n) { for (int i = 0; i < n; i++) belt.arrive.push_back(t + Cfg().conveyorTime); }
void World::StepBalls() {
    const Config& C = Cfg(); float dt = STEP;
    std::vector<char> gone(balls.size(), 0);
    for (size_t i = 0; i < balls.size(); i++) {
        Ball& b = balls[i]; b.age += dt;
        if (b.st == BS_HELD) { if (b.holder >= 0 && b.holder < (int)players.size()) { const Player& h = players[b.holder]; b.p = Vector3Add(h.Eye(), Vector3Add(Vector3Scale(h.Look(), 0.4f), {0, -0.3f, 0})); } b.v = {}; continue; }
        float spd = Vector3Length(b.v); int n = std::max(1, (int)ceilf(spd * dt / 0.12f)); float h = dt / n;
        for (int s = 0; s < n; s++) {
            Vector3 prev = b.p; b.v.y -= C.gravity * C.ballGrav * h; b.p = Vector3Add(b.p, Vector3Scale(b.v, h));
            // into a pit: it stays there until someone grabs it
            int pit = PitAt(b.p); if (pit >= 0 && b.p.y < PitSurface(arena.pits[pit]) && b.v.y <= 0.5f) { arena.pits[pit].balls++; gone[i] = 1; break; }
            // a live ball knocks out the first player it touches (until its first bounce)
            if (b.st == BS_LIVE) {
                bool hitSomething = false;
                for (auto& v : players) {
                    if (!v.alive || !v.present || v.id == b.thrower) continue;
                    if (b.team >= 0 && v.team == b.team && mode != MD_FFA) continue;
                    Vector3 a{v.pos.x, v.pos.y + 0.1f, v.pos.z}, top{v.pos.x, v.pos.y + v.Height() - 0.1f, v.pos.z};
                    if (SegDist(b.p, a, top) > C.radius + C.ballR + 0.03f) continue;
                    if (Hittable(v)) Hurt(v, 999, b.thrower, b.fromCannon ? "a cannon ball" : "a ball", b.fromCannon, true);
                    b.st = BS_LOOSE; b.v = Vector3Scale(b.v, -0.25f); hitSomething = true; break;
                }
                if (!hitSomething) for (auto& en : ents) {
                    if (en.dead || en.kind == E_CHICKEN || en.kind == E_CUSHION || en.kind == E_CONFETTI) continue;
                    if (en.kind != E_KID && en.team == b.team) continue;
                    float r = en.kind == E_TANK ? 1.0f : 0.45f; Vector3 c = Vector3Add(en.p, {0, en.kind == E_TANK ? 0.7f : en.kind == E_KID ? 0.6f : 0.2f, 0});
                    if (Vector3Distance(c, b.p) > r + C.ballR) continue;
                    if (en.kind == E_TANK) { en.armor -= 100; if (en.armor <= 0) { en.dead = true; if (en.owner >= 0) { players[en.owner].drive = -1; players[en.owner].po = PO_STAND; } } }
                    else if (en.kind != E_KID) en.dead = true;   // (one ball: a health box, a car, a drone)
                    b.st = BS_LOOSE; b.v = Vector3Scale(b.v, -0.3f); hitSomething = true; break;
                }
                if (hitSomething) break;
            }
            // the arena: bounce off; the first bounce ends a live ball
            Vector3 seg = Vector3Subtract(b.p, prev); float L = Vector3Length(seg);
            if (L > 1e-5f) {
                float tw; Vector3 dir = Vector3Scale(seg, 1 / L);
                if (Ray(prev, dir, L + C.ballR, &tw, 1)) {
                    Vector3 hp = Vector3Add(prev, Vector3Scale(dir, std::max(0.0f, tw - C.ballR)));
                    // find the face's normal from the solid the point sits on
                    Vector3 nrm{0, 1, 0}; float bestD = 1e9f;
                    if (hp.y < 0.12f) nrm = {0, 1, 0};
                    else for (const auto& bx : arena.solids) {
                        if (!BlocksShot(bx.mat)) continue;
                        Vector3 q{std::clamp(hp.x, bx.lo.x, bx.hi.x), std::clamp(hp.y, bx.lo.y, bx.hi.y), std::clamp(hp.z, bx.lo.z, bx.hi.z)};
                        float dd = Vector3Distance(q, hp); if (dd < bestD) { bestD = dd; Vector3 dv = Vector3Subtract(hp, q); if (Vector3Length(dv) > 1e-4f) nrm = Vector3Normalize(dv); else { nrm = Vector3Scale(dir, -1); } }
                    }
                    float ax = fabsf(nrm.x), ay = fabsf(nrm.y), az = fabsf(nrm.z); nrm = ay >= ax && ay >= az ? Vector3{0, nrm.y > 0 ? 1.0f : -1.0f, 0} : ax >= az ? Vector3{nrm.x > 0 ? 1.0f : -1.0f, 0, 0} : Vector3{0, 0, nrm.z > 0 ? 1.0f : -1.0f};
                    b.p = hp;
                    float vn = Vector3DotProduct(b.v, nrm); if (vn < 0) b.v = Vector3Subtract(Vector3Scale(b.v, 0.8f), Vector3Scale(nrm, vn * 1.3f));
                    if (b.st == BS_LIVE) { b.st = BS_LOOSE; Emit(EV_BOUNCE, b.p, -1, b.thrower, 0); }
                }
            }
            if (b.p.y < C.ballR) { b.p.y = C.ballR; if (b.v.y < 0) b.v.y = -b.v.y * 0.5f; b.v.x *= 0.9f; b.v.z *= 0.9f; if (b.st == BS_LIVE) { b.st = BS_LOOSE; Emit(EV_BOUNCE, b.p, -1, b.thrower, 0); } }
        }
        if (gone[i]) continue;
        // a loose ball that has slowed on a deck or the floor rolls into the gutter and rides the belt home
        if (b.st == BS_LOOSE) {
            float g = GroundAt(b.p, 0.02f, 0.05f);
            if (b.p.y - C.ballR - g < 0.05f) { b.v.x *= 1 - dt * 2.5f; b.v.z *= 1 - dt * 2.5f; }
            if ((Vector3Length(b.v) < 0.6f && b.p.y - C.ballR - g < 0.08f && b.age > 0.4f) || b.age > 12) { gone[i] = 2; }
        }
    }
    // (removing balls shifts indices: fix every holder's index)
    if (std::count_if(gone.begin(), gone.end(), [](char c) { return c != 0; })) {
        std::vector<int> remap(balls.size(), -1); size_t w = 0;
        for (size_t i = 0; i < balls.size(); i++) { if (gone[i] == 2) ToBelt(1); if (gone[i]) continue; remap[i] = (int)w; balls[w++] = balls[i]; }
        balls.resize(w);
        for (auto& p : players) if (p.ball >= 0) p.ball = p.ball < (int)remap.size() ? remap[p.ball] : -1;
    }
}
// the belts deliver to the lift, which always refills the emptiest hopper; with every hopper full the balls go to the
// central pit
void World::StepSupply() {
    const Config& C = Cfg();
    for (size_t i = 0; i < belt.arrive.size(); i++) {
        if (belt.arrive[i] > t) continue;
        int best = -1; for (int c = 0; c < (int)cannons.size(); c++) if (cannons[c].hopper < C.hopperMax && (best < 0 || cannons[c].hopper < cannons[best].hopper)) best = c;
        if (best >= 0) { cannons[best].hopper++; Emit(EV_HOPPER, arena.cannons[best].pivot, -1, -1, (float)best); }
        else arena.pits.back().balls++;
        belt.arrive.erase(belt.arrive.begin() + i); i--;
    }
}

// ---------------------------------------------------------------- the cannons
void World::FireCannon(int c) {
    const Config& C = Cfg(); Cannon& k = cannons[c]; if (k.hopper <= 0) return;
    k.hopper--; Vector3 f{cosf(k.pitch) * cosf(k.yaw), sinf(k.pitch), cosf(k.pitch) * sinf(k.yaw)};
    Ball b; b.st = BS_LIVE; b.thrower = k.op; b.team = k.op >= 0 ? (mode == MD_FFA ? -1 - k.op : players[k.op].team) : -1; b.fromCannon = true;
    b.p = CannonMuzzle(c); float sp = 0.03f; b.v = Vector3Scale(Vector3Normalize({f.x + (Rand() - 0.5f) * sp, f.y + (Rand() - 0.5f) * sp, f.z + (Rand() - 0.5f) * sp}), C.cannonSpeed);
    balls.push_back(b); Emit(EV_CANNON, b.p, k.op, -1, (float)c);
}
void World::StepCannon(Player& p) {
    const Config& C = Cfg(); const Input& in = p.in; float dt = STEP;
    if (p.po != PO_CANNON) {
        // getting on: stand at its seat and press use (0.5 s); not with a flag or a ball
        if (in.use && p.alive && p.carry < 0 && p.ball < 0 && p.po != PO_CLIMB && p.po != PO_SLIDE) {
            int c = CannonNear(p.pos);
            if (c >= 0 && cannons[c].op < 0) { cannons[c].op = p.id; p.cannon = c; p.po = PO_CANNON; p.mountT = 0; p.pos = CannonSeat(c); p.vel = {}; }
        }
        return;
    }
    Cannon& k = cannons[p.cannon]; const CannonDef& d = arena.cannons[p.cannon];
    p.pos = CannonSeat(p.cannon); p.mountT += dt;
    // the turning limits (blind spots behind and above)
    float dy = atan2f(sinf(p.in.yaw - d.yaw0), cosf(p.in.yaw - d.yaw0)); dy = std::clamp(dy, -C.cannonYaw * DEG2RAD, C.cannonYaw * DEG2RAD);
    p.yaw = d.yaw0 + dy; p.pitch = std::clamp(in.pitch, -C.cannonPitch * DEG2RAD, C.cannonPitch * DEG2RAD); k.yaw = p.yaw; k.pitch = p.pitch;
    if (in.use && p.mountT > 0.1f) { k.op = -1; p.cannon = -1; p.po = PO_STAND; return; }   // (getting off is instant)
    if (in.jump) { k.op = -1; p.cannon = -1; p.po = PO_STAND; return; }
}
static void StepCannonHeat(World& w) {
    const Config& C = Cfg(); float dt = STEP;
    for (int c = 0; c < (int)w.cannons.size(); c++) {
        Cannon& k = w.cannons[c]; k.shotT = std::max(0.0f, k.shotT - dt);
        bool firing = false;
        if (k.op >= 0) { const Player& p = w.players[k.op]; if (!p.alive || p.po != PO_CANNON) k.op = -1; else firing = p.in.fire && p.mountT >= C.cannonMount; }
        if (k.overheated) { k.coolT -= dt; if (k.coolT <= 0) { k.overheated = false; k.heat = 0; } continue; }
        if (firing && k.hopper > 0) {
            k.heat += dt;
            if (k.shotT <= 0) { w.FireCannon(c); k.shotT = 1.0f / C.cannonRate; }
            if (k.heat >= C.cannonHeat) { k.overheated = true; k.coolT = C.cannonCool; w.Emit(EV_OVERHEAT, w.arena.cannons[c].pivot, k.op, -1, (float)c); }
        } else k.heat = std::max(0.0f, k.heat - dt * 1.5f);
    }
}

// ---------------------------------------------------------------- the store (spec "Score, store, and ammo")
void World::StepStore(Player& p) {
    const Config& C = Cfg(); const Input& in = p.in;
    int s = StoreNear(p.pos);
    bool open = s >= 0 && (mode == MD_FFA || arena.stores[s].team == p.team) && p.alive;
    if (open) { if (!p.inStore) p.storeT = 0; p.inStore = true; p.storeT += STEP; } else { p.inStore = false; p.storeT = 99; }
    if (in.buy < 0 || !open) return;
    int b = in.buy;
    if (b >= 0 && b < G_COUNT) {
        const GunDef& g = C.guns[b];
        if (b == G_STARTER || p.gun == b || p.cash < g.cost) return;
        p.cash -= g.cost; p.gun = b; p.mag[1] = g.mag; p.wield = p.carry >= 0 ? 0 : 1; p.reloadT = 0; p.spin = 0;
        Emit(EV_BUY, p.pos, p.id, -1, (float)b);
    } else if (b == 100) {
        if (p.cash < C.dartPackCost || p.reserve >= C.dartMax) return;
        p.cash -= C.dartPackCost; p.reserve = std::min(C.dartMax, p.reserve + C.dartPack); Emit(EV_BUY, p.pos, p.id, -1, 100);
    } else if (b == 101) {
        if (mode != MD_BOMB || p.kit || p.cash < C.disarmKit || p.team == attackers) return;
        p.cash -= C.disarmKit; p.kit = true; Emit(EV_BUY, p.pos, p.id, -1, 101);
    } else if (b >= 200 && b < 200 + (int)C.jokes.size()) {
        const JokeDef& j = C.jokes[b - 200]; if (p.cash < j.cost || p.jokes[b - 200] >= 9) return;
        p.cash -= j.cost; p.jokes[b - 200]++; Emit(EV_BUY, p.pos, p.id, -1, (float)b);
    }
}

// ---------------------------------------------------------------- streak rewards and joke items (spec "Streak rewards", "Joke shop")
void World::StepRewards(Player& p) {
    const Config& C = Cfg(); const Input& in = p.in;
    p.jokeCool = std::max(0.0f, p.jokeCool - STEP);
    if (in.pickRewards >= 0) { int bits = in.pickRewards & 63, n = 0; for (int r = 0; r < 6; r++) n += (bits >> r) & 1; if (n == 3 && !p.alive) p.rewardPick = bits; else if (n == 3 && round <= 1 && phase == PH_WARMUP) p.rewardPick = bits; }
    if (!p.alive) return;
    // a ready reward, used when you like
    int r = in.reward;
    if (r >= 0 && r < 6 && ((p.rewardReady >> r) & 1) && !((p.rewardUsed >> r) & 1) && p.po != PO_SLIDE && p.po != PO_CLIMB && p.drive < 0) {
        Vector3 fwd{cosf(p.yaw), 0, sinf(p.yaw)}; bool used = true;
        switch (r) {
            case 0: {   // the instant dart refill: you and teammates within 10 m
                for (auto& q : players) if (q.alive && (q.id == p.id || (mode != MD_FFA && q.team == p.team && Vector3Distance(q.pos, p.pos) <= 10))) { q.reserve = C.dartMax; q.mag[0] = 1; if (q.gun < G_COUNT) q.mag[1] = C.guns[q.gun].mag; }
                break; }
            case 1: { Ent e; e.kind = E_HEALTHBOX; e.owner = p.id; e.team = mode == MD_FFA ? -1 - p.id : p.team; e.p = Vector3Add(p.pos, Vector3Scale(fwd, 0.9f)); e.p.y = GroundAt(e.p, 0.2f, 0.6f); e.hp = 3; e.life = C.rewards[1].time; ents.push_back(e); break; }
            case 2: {   // the kids flood the enemy half (free for all: everywhere but near you)
                for (int k = 0; k < 8; k++) {
                    Ent e; e.kind = E_KID; e.owner = p.id; e.team = mode == MD_FFA ? -1 - p.id : p.team; e.life = C.rewards[2].time;
                    float side = mode == MD_FFA ? (p.pos.x > 0 ? -1.0f : 1.0f) : (p.team == 0 ? 1.0f : -1.0f);
                    e.p = {side * (2 + Rand() * 4), 0, (Rand() - 0.5f) * 20}; e.goal = e.p; e.t = Rand() * 3; ents.push_back(e);
                }
                break; }
            case 3: { Ent e; e.kind = E_RCCAR; e.owner = p.id; e.team = mode == MD_FFA ? -1 - p.id : p.team; e.p = Vector3Add(p.pos, Vector3Scale(fwd, 0.8f)); e.p.y = GroundAt(e.p, 0.2f, 0.6f); e.yaw = p.yaw; e.hp = 50; e.life = C.rewards[3].time; ents.push_back(e); p.drive = (int)ents.size() - 1; p.po = PO_DRIVE; break; }
            case 4: { Ent e; e.kind = E_DRONE; e.owner = p.id; e.team = mode == MD_FFA ? -1 - p.id : p.team; e.p = Vector3Add(p.pos, {0, 2.6f, 0}); e.hp = 80; e.life = C.rewards[4].time; ents.push_back(e); break; }
            case 5: {   // the tank only drives on the ground floor and the wide level 2 decks
                if (p.pos.y > L2 + 0.2f) { used = false; break; }
                Ent e; e.kind = E_TANK; e.owner = p.id; e.team = mode == MD_FFA ? -1 - p.id : p.team; e.p = p.pos; e.yaw = p.yaw; e.armor = 400; e.life = C.rewards[5].time; ents.push_back(e); p.drive = (int)ents.size() - 1; p.po = PO_DRIVE; break; }
        }
        if (used) { p.rewardUsed |= (uint8_t)(1 << r); Emit(EV_REWARD, p.pos, p.id, -1, (float)r); }
    }
    // joke items: harmless, team-only, 10 s apart
    int j = in.joke;
    if (j >= 0 && j < (int)C.jokes.size() && p.jokes[j] > 0 && p.jokeCool <= 0) {
        const std::string& key = C.jokes[j].key; bool ok = true; float a = (float)j;
        Vector3 fwd{cosf(p.yaw), 0, sinf(p.yaw)};
        if (key == "cushion") { Ent e; e.kind = E_CUSHION; e.owner = p.id; e.team = p.team; e.p = Vector3Add(p.pos, Vector3Scale(fwd, 0.8f)); e.p.y = GroundAt(e.p, 0.2f, 0.6f); e.life = 180; ents.push_back(e); }
        else if (key == "chicken") { Ent e; e.kind = E_CHICKEN; e.owner = p.id; e.team = p.team; e.p = p.Eye(); e.v = Vector3Scale(p.Look(), 10); e.life = 6; ents.push_back(e); }
        else if (key == "confetti") { Ent e; e.kind = E_CONFETTI; e.owner = p.id; e.team = p.team; e.p = Vector3Add(p.Eye(), Vector3Scale(fwd, 0.6f)); e.life = 2; ents.push_back(e); }
        else if (key == "dadjoke") { jokeLine = (int)(Rand() * C.dadJokes.size()) % (int)C.dadJokes.size(); a = 1000.0f + jokeLine; }
        else if (key == "capsule") { int pr = (int)(Rand() * C.prizes.size()) % (int)C.prizes.size(); p.prizes++; a = 2000.0f + pr; }
        else if (key == "crown") {   // on the nearest teammate in front of you (within 4 m)
            int best = -1; float bd = 4; for (auto& q : players) if (q.id != p.id && q.alive && q.team == p.team && mode != MD_FFA) { float d = Vector3Distance(q.pos, p.pos); if (d < bd && Vector3DotProduct(Vector3Normalize(Vector3Subtract(q.pos, p.pos)), fwd) > 0.3f) { bd = d; best = q.id; } }
            if (best < 0) ok = false; else { players[best].crown = true; a = 3000.0f + best; }
        }
        else if (key == "finger") p.fingerT = 10;
        else if (key == "beanie") p.beanie = true;
        if (ok) { p.jokes[j]--; p.jokeCool = C.jokeCool; Emit(EV_JOKE, p.pos, p.id, -1, a); }
    }
}
// the reward entities and joke props
void World::StepEnts() {
    const Config& C = Cfg(); float dt = STEP;
    for (size_t i = 0; i < ents.size(); i++) {
        Ent& e = ents[i]; if (e.dead) continue;
        e.life -= dt; e.t += dt; e.cool = std::max(0.0f, e.cool - dt);
        if (e.life <= 0) { e.dead = true; if ((e.kind == E_RCCAR || e.kind == E_TANK) && e.owner >= 0 && players[e.owner].drive == (int)i) { players[e.owner].drive = -1; players[e.owner].po = PO_STAND; } continue; }
        Player* own = e.owner >= 0 && e.owner < (int)players.size() ? &players[e.owner] : nullptr;
        switch (e.kind) {
            case E_HEALTHBOX: {
                if (e.hp <= 0) { e.dead = true; break; }
                for (auto& q : players) if (q.alive && (q.id == e.owner || (mode != MD_FFA && q.team == e.team)) && q.hp < C.health && Vector3Distance(q.pos, e.p) < 0.9f) { q.hp = std::min(C.health, q.hp + 50); e.uses++; Emit(EV_REWARD, e.p, q.id, e.owner, 11); if (e.uses >= 5) { e.dead = true; break; } }
                break; }
            case E_KID: {   // run about the enemy half: ladders, slide exits, pits, in front of enemies
                if (e.t > 2.5f || Vector2Distance({e.p.x, e.p.z}, {e.goal.x, e.goal.z}) < 0.5f) {
                    e.t = 0; float side = e.p.x >= 0 ? 1.0f : -1.0f; int pick = (int)(Rand() * 4);
                    if (pick == 0 && !arena.climbs.empty()) { const Climb& c = arena.climbs[(int)(Rand() * arena.climbs.size()) % arena.climbs.size()]; if (c.lo.y < 0.1f) e.goal = {(c.lo.x + c.hi.x) / 2 - c.into.x * 0.4f, 0, (c.lo.z + c.hi.z) / 2 - c.into.z * 0.4f}; }
                    else if (pick == 1) { int best = -1; float bd = 1e9f; for (auto& q : players) if (q.alive && (mode == MD_FFA ? q.id != e.owner : q.team != e.team) && q.pos.y < 0.6f) { float d = Vector3Distance(q.pos, e.p) + Rand() * 4; if (d < bd) { bd = d; best = q.id; } } if (best >= 0) { Vector3 q = players[best].pos; e.goal = {q.x + cosf(players[best].yaw) * 1.2f, 0, q.z + sinf(players[best].yaw) * 1.2f}; } }
                    else if (pick == 2 && !arena.pits.empty()) { const Pit& pt = arena.pits[(int)(Rand() * arena.pits.size()) % arena.pits.size()]; e.goal = {pt.lo.x + Rand() * (pt.hi.x - pt.lo.x), 0, pt.lo.z + Rand() * (pt.hi.z - pt.lo.z)}; }
                    else e.goal = {side * (3 + Rand() * 15), 0, (Rand() - 0.5f) * 24};
                    if (mode != MD_FFA) { float half = e.team == 0 ? 1.0f : -1.0f; if (e.goal.x * half < 1) e.goal.x = half * (2 + Rand() * 14); }
                }
                Vector3 to = Vector3Subtract(e.goal, e.p); to.y = 0; float L = Vector3Length(to);
                Vector3 want = L > 0.1f ? Vector3Scale(to, 3.4f / L) : Vector3{0, 0, 0};
                e.v.x += (want.x - e.v.x) * std::min(1.0f, dt * 6); e.v.z += (want.z - e.v.z) * std::min(1.0f, dt * 6);
                bool g = true; MoveBody(*this, e.p, e.v, g, 0.25f, 1.1f, dt); e.yaw = atan2f(e.v.z, e.v.x);
                if (Rand() < dt * 0.4f) Emit(EV_REWARD, e.p, -1, e.owner, 12);   // (a shriek)
                break; }
            case E_RCCAR: {
                if (!own || !own->alive || own->drive != (int)i) { e.dead = true; break; }
                if (e.hp <= 0) { e.dead = true; own->drive = -1; own->po = PO_STAND; break; }
                const Input& in = own->in; e.yaw = in.yaw;
                Vector3 fwd{cosf(e.yaw), 0, sinf(e.yaw)}, rgt{-sinf(e.yaw), 0, cosf(e.yaw)};
                Vector3 wish = Vector3Add(Vector3Scale(fwd, in.moveX), Vector3Scale(rgt, in.moveZ)); if (Vector3Length(wish) > 1) wish = Vector3Normalize(wish);
                e.v.x += (wish.x * 6.5f - e.v.x) * std::min(1.0f, dt * 5); e.v.z += (wish.z * 6.5f - e.v.z) * std::min(1.0f, dt * 5); e.v.y -= C.gravity * dt;
                bool g = false; MoveBody(*this, e.p, e.v, g, 0.22f, 0.35f, dt);
                if (in.fire && e.cool <= 0) {   // the turret: 10 damage, 4 shots a second
                    e.cool = 0.25f; Dart d; d.p = Vector3Add(e.p, {0, 0.35f, 0}); Vector3 look{cosf(in.pitch) * cosf(in.yaw), sinf(in.pitch), cosf(in.pitch) * sinf(in.yaw)};
                    d.v = Vector3Scale(look, 22); d.owner = e.owner; d.team = e.team; d.dmg = 10; d.grav = 0.4f; d.kind = 3; darts.push_back(d); Emit(EV_SHOT, d.p, e.owner, -1, 99);
                }
                if (in.use && e.t > 0.5f) { e.dead = true; own->drive = -1; own->po = PO_STAND; }
                break; }
            case E_DRONE: {
                if (!own || !own->alive) { e.dead = true; break; }
                if (e.hp <= 0) { e.dead = true; break; }
                Vector3 want = Vector3Add(own->pos, {cosf(e.t * 0.8f) * 1.2f, 2.8f, sinf(e.t * 0.8f) * 1.2f}); e.p = Vector3Lerp(e.p, want, std::min(1.0f, dt * 3));
                if (e.cool <= 0) {
                    int best = -1; float bd = 30;
                    for (auto& q : players) if (Enemies(*own, q) && Hittable(q)) { Vector3 c{q.pos.x, q.pos.y + q.Height() * 0.6f, q.pos.z}; float d = Vector3Distance(c, e.p); if (d < bd && Sees(e.p, c)) { bd = d; best = q.id; } }
                    if (best >= 0) {
                        const Player& q = players[best]; Vector3 c{q.pos.x, q.pos.y + q.Height() * 0.6f, q.pos.z}; float tof = bd / 20; c = Vector3Add(c, Vector3Scale(q.vel, tof)); c.y += 0.5f * C.gravity * 0.4f * tof * tof;
                        Dart d; d.p = e.p; d.v = Vector3Scale(Vector3Normalize(Vector3Subtract(c, e.p)), 20); d.owner = e.owner; d.team = e.team; d.dmg = 8; d.grav = 0.4f; d.kind = 3; darts.push_back(d); e.cool = 1.0f / 3; e.yaw = atan2f(d.v.z, d.v.x); Emit(EV_SHOT, e.p, e.owner, -1, 98);
                    }
                }
                break; }
            case E_TANK: {
                if (!own || !own->alive || own->drive != (int)i) { e.dead = true; break; }
                const Input& in = own->in;
                Vector3 fwd{cosf(in.yaw), 0, sinf(in.yaw)}, rgt{-sinf(in.yaw), 0, cosf(in.yaw)};
                Vector3 wish = Vector3Add(Vector3Scale(fwd, in.moveX), Vector3Scale(rgt, in.moveZ)); if (Vector3Length(wish) > 1) wish = Vector3Normalize(wish);
                e.v.x += (wish.x * 4.0f - e.v.x) * std::min(1.0f, dt * 3); e.v.z += (wish.z * 4.0f - e.v.z) * std::min(1.0f, dt * 3); e.v.y -= C.gravity * dt;
                if (Vector3Length({e.v.x, 0, e.v.z}) > 0.3f) e.yaw = atan2f(e.v.z, e.v.x);
                // (it only drives on the ground floor and wide decks: it won't roll off an edge, and nothing lifts it)
                Vector3 np = Vector3Add(e.p, Vector3Scale(e.v, dt * 6)); float gNext = GroundAt(np, 0.2f, 0.6f);
                if (gNext < e.p.y - 0.4f) { e.v.x = 0; e.v.z = 0; }
                bool g = true; MoveBody(*this, e.p, e.v, g, 0.85f, 1.3f, dt);
                own->pos = e.p; own->yaw = in.yaw;
                if (in.fire && e.cool <= 0) {   // twin flywheel blasters: 15 damage, 10 shots a second
                    e.cool = 0.1f; Vector3 look{cosf(in.pitch) * cosf(in.yaw), sinf(in.pitch), cosf(in.pitch) * sinf(in.yaw)}; Vector3 r = Vector3Normalize(Vector3CrossProduct(look, {0, 1, 0}));
                    Dart d; d.p = Vector3Add(e.p, Vector3Add({0, 1.0f, 0}, Vector3Add(Vector3Scale(look, 0.9f), Vector3Scale(r, (e.uses++ % 2) ? 0.3f : -0.3f))));
                    d.v = Vector3Scale(Vector3Normalize(Vector3Add(look, {(Rand() - 0.5f) * 0.04f, (Rand() - 0.5f) * 0.04f, (Rand() - 0.5f) * 0.04f})), 22); d.owner = e.owner; d.team = e.team; d.dmg = 15; d.grav = 0.45f; d.kind = 3; darts.push_back(d); Emit(EV_SHOT, d.p, e.owner, -1, 97);
                }
                if (in.use && e.t > 0.5f) { e.dead = true; own->drive = -1; own->po = PO_STAND; }
                break; }
            case E_CHICKEN: {   // bounces about squawking, and does nothing else
                e.v.y -= C.gravity * dt; Vector3 prev = e.p; e.p = Vector3Add(e.p, Vector3Scale(e.v, dt));
                float tw; Vector3 seg = Vector3Subtract(e.p, prev); float L = Vector3Length(seg);
                if (L > 1e-4f && Ray(prev, Vector3Scale(seg, 1 / L), L + 0.12f, &tw, 0)) { e.p = prev; e.v = Vector3Scale(e.v, -0.6f); e.v.y = fabsf(e.v.y) + 2.5f; e.v.x += (Rand() - 0.5f) * 3; e.v.z += (Rand() - 0.5f) * 3; Emit(EV_JOKE, e.p, e.owner, -1, 500); }
                float g = GroundAt(e.p, 0.1f, 0.1f); if (e.p.y < g + 0.1f) { e.p.y = g + 0.1f; e.v.y = fabsf(e.v.y) * 0.7f + 1.5f; e.v.x *= 0.85f; e.v.z *= 0.85f; Emit(EV_JOKE, e.p, e.owner, -1, 500); }
                e.yaw += dt * 8;
                break; }
            case E_CUSHION: {
                for (auto& q : players) if (q.alive && q.id != e.owner && q.team == e.team && mode != MD_FFA && Vector3Distance(q.pos, e.p) < 0.5f) { Emit(EV_JOKE, e.p, q.id, e.owner, 501); e.dead = true; break; }
                break; }
            default: break;
        }
    }
    ents.erase(std::remove_if(ents.begin(), ents.end(), [](const Ent& e) { return e.dead; }), ents.end());
    // (drive indices: re-find each driver's entity after the removal)
    for (auto& p : players) if (p.po == PO_DRIVE) { p.drive = -1; for (int i = 0; i < (int)ents.size(); i++) if ((ents[i].kind == E_RCCAR || ents[i].kind == E_TANK) && ents[i].owner == p.id) p.drive = i; if (p.drive < 0) p.po = PO_STAND; }
}

// ---------------------------------------------------------------- the modes (spec "Game modes")
void World::StepMode() {
    const Config& C = Cfg(); const ModeDef& md = C.modes[mode];
    auto endMatch = [&](int w) { winner = w; phase = PH_OVER; phaseT = 0; Emit(EV_ROUND, {0, 0, 0}, w, -1, -1); };
    if (mode == MD_FFA) {
        int lead = Leader();
        if (lead >= 0 && players[lead].kos >= md.toWin) endMatch(lead);
        else if (phaseT >= md.timeLimit) endMatch(lead);
        return;
    }
    if (mode == MD_TDM) {
        if (teamKOs[0] >= md.toWin || teamKOs[1] >= md.toWin) endMatch(teamKOs[0] >= md.toWin ? 0 : 1);
        else if (phaseT >= md.timeLimit) endMatch(teamKOs[0] > teamKOs[1] ? 0 : teamKOs[1] > teamKOs[0] ? 1 : -1);
        return;
    }
    if (mode == MD_CTF) {
        for (int s = 0; s < 2; s++) {
            Flag& f = flags[s];
            if (f.carrier >= 0) { const Player& c = players[f.carrier]; if (!c.alive) f.carrier = -1; else f.p = Vector3Add(c.pos, {0, 1.6f, 0}); continue; }
            if (!f.home_) { f.dropT += STEP; if (f.dropT >= C.flagReturn) { f.home_ = true; f.p = f.home; Emit(EV_FLAG_RETURN, f.p, -1, -1, (float)s); } }
            for (auto& p : players) {
                if (!p.alive || p.po == PO_SLIDE || p.po == PO_CANNON || p.po == PO_DRIVE || Vector3Distance(p.pos, f.p) > 1.3f) continue;
                if (p.team == s) {   // a defender touching their dropped flag sends it home
                    if (!f.home_) { f.home_ = true; f.p = f.home; AddScore(*this, p, C.scoreReturn); Emit(EV_FLAG_RETURN, f.p, p.id, -1, (float)s); }
                } else if (p.carry < 0) {
                    f.carrier = p.id; f.home_ = false; p.carry = s;
                    if (p.ball >= 0) { Ball& b = balls[p.ball]; b.st = BS_LOOSE; b.holder = -1; p.ball = -1; }
                    if (p.cannon >= 0) { cannons[p.cannon].op = -1; p.cannon = -1; }
                    Emit(EV_FLAG_TAKE, f.p, p.id, -1, (float)s); break;
                }
            }
        }
        // a capture: the carrier reaches their own flag, at home
        for (auto& p : players) if (p.alive && p.carry >= 0) {
            Flag& mine = flags[p.team];
            if (mine.home_ && Vector3Distance(p.pos, mine.home) < 1.4f) {
                Flag& f = flags[p.carry]; f.carrier = -1; f.home_ = true; f.p = f.home; p.carry = -1; caps[p.team]++; p.caps++; AddScore(*this, p, C.scoreCapture);
                Emit(EV_CAPTURE, p.pos, p.id, -1, (float)p.team);
            }
        }
        if (caps[0] >= md.toWin || caps[1] >= md.toWin) endMatch(caps[0] >= md.toWin ? 0 : 1);
        else if (phaseT >= md.timeLimit) endMatch(caps[0] > caps[1] ? 0 : caps[1] > caps[0] ? 1 : -1);
        return;
    }
    // disarm the bomb: rounds without respawns
    if (phase == PH_ROUND_END) return;
    int def = 1 - attackers; int aliveA = 0, aliveD = 0; for (const auto& p : players) if (p.present && p.alive) { if (p.team == attackers) aliveA++; else aliveD++; }
    auto endRound = [&](int w) {
        roundWins[w]++; roundWinner = w; phase = PH_ROUND_END; phaseT = 0; Emit(EV_ROUND, {0, 0, 0}, w, -1, (float)round);
        if (roundWins[w] >= md.toWin) endMatch(w);
    };
    // the bomb: carried, dropped (any attacker picks it up), planted at a site in the defenders' tower, defused
    if (!bomb.planted) {
        if (bomb.carrier >= 0) { Player& c = players[bomb.carrier]; if (!c.alive) bomb.carrier = -1; else bomb.p = c.pos; }
        else for (auto& p : players) if (p.alive && p.team == attackers && Vector3Distance(p.pos, bomb.p) < 1.2f) { bomb.carrier = p.id; p.bomb = true; break; }
        if (bomb.carrier >= 0) {
            Player& c = players[bomb.carrier]; int site = -1;
            for (int i = 0; i < (int)arena.bombSites.size(); i++) if (arena.bombSites[i].team == def && Vector3Distance(c.pos, arena.bombSites[i].p) < 1.6f) site = i;
            if (site >= 0 && c.in.use == false && c.po == PO_STAND && c.in.moveX == 0 && c.in.moveZ == 0 && c.in.fire == false && c.in.crouch == false && c.in.vacuum == false && c.in.knife == false && c.in.grab == false && c.in.reload == false && c.in.jump == false && c.in.aim == false && c.in.slot < 0 && c.in.buy < 0 && c.in.joke < 0 && c.in.reward < 0 && c.plantT >= 0) { }
            if (site >= 0 && (c.in.use || c.plantT > 0) && Vector3Length({c.vel.x, 0, c.vel.z}) < 0.5f) {
                c.plantT += STEP;
                if (c.plantT >= C.plantTime) { bomb.planted = true; bomb.site = site; bomb.fuseT = C.fuse; bomb.p = arena.bombSites[site].p; bomb.carrier = -1; c.bomb = false; c.plantT = 0; AddScore(*this, c, C.scorePlant); Emit(EV_PLANT, bomb.p, c.id); }
            } else c.plantT = 0;
        }
        if (aliveA == 0) { endRound(def); return; }
        if (aliveD == 0) { endRound(attackers); return; }
        if (phaseT >= C.roundTime) { endRound(def); return; }
    } else {
        bomb.fuseT -= STEP;
        bool someone = false;
        for (auto& p : players) {
            if (!p.alive || p.team != def || Vector3Distance(p.pos, bomb.p) > 1.6f) { p.defuseT = 0; continue; }
            if (p.in.use || p.defuseT > 0) {
                if (Vector3Length({p.vel.x, 0, p.vel.z}) > 0.5f) { p.defuseT = 0; continue; }
                someone = true; p.defuseT += STEP;
                if (p.defuseT >= (p.kit ? C.defuseKit : C.defuseTime)) { bomb.done = true; AddScore(*this, p, C.scoreDefuse); Emit(EV_DEFUSE, bomb.p, p.id); endRound(def); return; }
            }
        }
        (void)someone;
        if (bomb.fuseT <= 0) { bomb.done = true; Emit(EV_BOOM, bomb.p); for (auto& p : players) if (p.alive && Vector3Distance(p.pos, bomb.p) < 6) KnockOut(p, -1, "the bomb", false); endRound(attackers); return; }
        if (aliveD == 0) { endRound(attackers); return; }
    }
}

// ---------------------------------------------------------------- the step
void World::Step() {
    const Config& C = Cfg(); t += STEP; phaseT += STEP;
    if (phase == PH_OVER) return;
    if (phase == PH_ROUND_END) { if (phaseT >= 4) NewRound(); return; }
    if (phase == PH_WARMUP) {
        for (auto& p : players) { p.vel = {}; p.yaw = p.in.yaw; p.pitch = p.in.pitch; StepRewards(p); StepStore(p); }
        if (phaseT >= 3) { phase = PH_PLAY; phaseT = 0; Emit(EV_ROUND, {0, 0, 0}, -1, -1, 0); }
        return;
    }
    for (auto& p : players) {
        if (!p.present) continue;
        if (!p.alive) { StepRewards(p); if (!noRespawn) { p.respawnT -= STEP; if (p.respawnT <= 0) Respawn(p); } continue; }
        p.sinceHurt += STEP; if (p.sinceHurt >= C.regenAfter && p.hp < C.health) p.hp = std::min(C.health, p.hp + C.regenRate * STEP);
        StepCannon(p);
        StepPlayer(p);
        if (!p.alive) continue;
        StepStore(p);
        StepWeapons(p);
        StepBallHands(p);
        StepRewards(p);
    }
    StepCannonHeat(*this);
    StepDarts();
    StepBalls();
    StepSupply();
    StepEnts();
    StepMode();
}

}  // namespace bp
