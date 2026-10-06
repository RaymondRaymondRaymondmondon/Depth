#include "sound.h"
#include "beasts.h"
#include "trawl.h"
#include "trawl_wreck.h"
#include "trawl_eco.h"
#include "trawl_session.h"
#include "trawl_net.h"
#include "redtide_net.h"
#include "redtide.h"
#include "redtide_match.h"
#include "skins.h"
// ============================================================================
//  DEPTH - entry point. Opens the window and runs whichever scene is active.
//
//  Developer switches:
//    depth.exe --sim 400 [level] [random|sensible] [cave tier 0-4]   auto-play expeditions, print balance
//    depth.exe --shots <folder>    render every screen to PNGs and quit
//    depth.exe --verify            prove every platformer section can be crossed
//    depth.exe --flats-sim 1000    play Flats headlessly and report how it goes
//    depth.exe --sprites <file.png>  draw every sprite in the game onto one sheet
// ============================================================================
#include "game.h"
#include "voice.h"
#include "flight.h"
#include "mouthful.h"
#include "mouthful_net.h"
#include "nightoff.h"
#include "nightoff_games.h"
#include "nightoff_net.h"
#include "scuffle.h"
#include "scuffle_net.h"
#include "warp.h"
#include "warp_net.h"
#include "ballpit.h"
#include "fathoms.h"
#include "fathoms_net.h"
#include "ballpit_net.h"
#include "fowl.h"
#include "fowl_net.h"
#include "noclip.h"
#include "noclip_net.h"
#include "flight_costumes.h"
#include "study.h"
#include "course.h"
#include "expr.h"
#include "input.h"
#include "levelgen.h"
#include "relics.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>

Game* gCurrentGame = nullptr;
static void RunScene(Game& g) {
    gDiveGear = g.scene == Scene::Dungeon;   // masks and helmets only on expedition
    gCurrentGame = &g;
    switch (g.scene) {
        case Scene::Study:      SceneStudy(g); break;
        case Scene::Arcade:     SceneArcade(g); break;
        case Scene::RedTide:    SceneRedTide(g); break;
        case Scene::Trawl:      SceneTrawl(g); break;
        case Scene::Flight:     SceneFlight(g); break;
        case Scene::Mouthful:   SceneMouthful(g); break;
        case Scene::NightOff:   SceneNightOff(g); break;
        case Scene::Scuffle:    SceneScuffle(g); break;
        case Scene::Warp:       SceneWarp(g); break;
        case Scene::Fowl:       SceneFowl(g); break;
        case Scene::Noclip:     SceneNoclip(g); break;
        case Scene::BallPit:    SceneBallPit(g); break;
        case Scene::Fathoms:    SceneFathoms(g); break;
        case Scene::Hub:        SceneHub(g); break;
        case Scene::Helm:       SceneHelm(g); break;
        case Scene::Crew:       SceneCrew(g); break;
        case Scene::Radar:      SceneRadar(g); break;
        case Scene::Ward:       SceneWard(g); break;
        case Scene::SickLeave:  SceneSickLeave(g); break;
        case Scene::Bookshelf:  SceneBookshelf(g); break;
        case Scene::Periscope:  ScenePeriscope(g); break;
        case Scene::Workshop:   SceneWorkshop(g); break;
        case Scene::Dungeon:    SceneDungeon(g); break;
        case Scene::Platformer: ScenePlatformer(g); break;
        case Scene::Cards:      SceneCards(g); break;
        case Scene::Abyss:      SceneAbyss(g); break;
    }
    EndStationPanel();
}

// Starts a platform level on the first generator seed that contains the given set-piece, standing just before it.
// Puts the diver in front of the first launcher of `type` (T torpedo tube, N cannon, y barrel chute) in the first layout that has one.
static void ShotAtLauncher(Game& g, int level, char type) {
    for (int seed = 1; seed < 60; seed++) {
        g.platLayouts[level] = {seed, 100};
        StartPlatform(g, level);
        for (auto& l : g.plat.launchers)
            if (l.type == type) { g.plat.pos = {(l.tx - 9) * 32.0f, l.ty * 32.0f + 4 - 28}; l.t = 1.0f; g.plat.time = 0.5f; return; }
    }
}
static void ShotAtPiece(Game& g, int level, SetPiece sp, bool ghost = false, int hopsBefore = 1) {
    unsigned seed = 1;
    GenLevel gl;
    for (;; seed++) { gl = GenerateLevel(level, seed, 1.0f); if (gl.setPieces[(int)sp] > 0) break; }
    g.platLayouts[level] = {(int)seed, 100};
    if (ghost) g.platLayouts[level].push_back(1);
    StartPlatform(g, level);
    for (size_t i = 1; i < gl.path.size(); i++)
        if (gl.path[i].tag == sp) {
            const GenWaypoint& w = gl.path[i >= (size_t)hopsBefore ? i - hopsBefore : 0];
            g.plat.pos = {w.tx * 32.0f + 6, (w.ty - g.plat.genTop + 1) * 32.0f - 26};
            break;
        }
}
// Starts a platform level, lets its creatures live a few seconds out of the diver's sight, then stands the diver at
// the busiest spot (the beast with the most others near it) - depth.exe --shots shots fauna
static void ShotAtFauna(Game& g, int level, int seed, float secs, int pick = 0, int species = -1) {
    g.platLayouts[level] = {seed, 100};
    StartPlatform(g, level);
    PlatformState& p = g.plat;
    float keep = p.deathTimer;
    p.deathTimer = 1;
    for (int f = 0; f < (int)(secs * 60); f++) BeastsUpdate(p, 1 / 60.0f);
    p.deathTimer = keep;
    std::vector<std::pair<int, int>> ranked;
    for (int i = 0; i < (int)p.fauna.beasts.size(); i++) {
        const Beast& b = p.fauna.beasts[i];
        if (b.life != BeastLife::Alive || b.hidden || (species >= 0 && b.species != species)) continue;
        int n = 0;
        for (const auto& o : p.fauna.beasts) if (o.life != BeastLife::Gone && !o.hidden && fabsf(o.pos.x - b.pos.x) < 280 && fabsf(o.pos.y - b.pos.y) < 160) n++;
        ranked.push_back({-n, i});
    }
    std::sort(ranked.begin(), ranked.end());
    // skip clusters too close to one already picked, so each pick shows a different place
    std::vector<Vector2> used;
    for (const auto& r : ranked) {
        Vector2 at = p.fauna.beasts[r.second].pos;
        bool near = false;
        for (auto u : used) if (fabsf(u.x - at.x) < 600) near = true;
        if (near) continue;
        used.push_back(at);
        if ((int)used.size() > pick) {
            // stand the diver on safe floor nearby - back from a big hunter, or it'd take the diver before the shot
            float want = at.x - (species >= 0 ? 330.0f : 120.0f);
            for (int d = 0; d < 30; d++) for (int sgn = -1; sgn <= 1; sgn += 2) {
                int cx = (int)(want / 32) + d * sgn;
                for (int y = std::max(1, (int)(at.y / 32) - 8); y < p.h - 1; y++) {
                    if (PlatSolid(p, cx, y)) break;
                    char below = PlatTileAt(p, cx, y + 1);
                    if (PlatSolid(p, cx, y + 1) && below != 'x' && below != 't' && below != 'g' && !PlatSolid(p, cx, y - 1)) { p.pos = {cx * 32.0f + 6, y * 32.0f + 32 - 28}; BeastsDiverRespawned(p, p.pos); return; }
                }
            }
            p.pos = {want, at.y - 40};
            return;
        }
    }
}
// The Hull's ParkourReference1.3 roster, posed: 0 the whale (the diver on its back), 1 a siphon mid-pull, 2 the
// megalodon winding up to strike, 3 hull-kelp and the flora near it.
static void ShotHull13(Game& g, int which) {
    ShotAtFauna(g, PL_HULL, 404, 3, 0, which == 3 ? HS_KELP : HS_WHALE);
    PlatformState& p = g.plat;
    auto& B = p.fauna.beasts;
    auto find = [&](int sp) { for (int i = 0; i < (int)B.size(); i++) if (B[i].life == BeastLife::Alive && B[i].species == sp) return i; return -1; };
    if (which == 0) { int w = find(HS_WHALE); if (w >= 0) { p.pos = {B[w].pos.x - 10, B[w].pos.y - 27 - 26}; p.vel = {0, 0}; for (int f = 0; f < 30; f++) BeastsUpdate(p, 1 / 60.0f); } }
    if (which == 1) {
        int s = find(HS_SIPHON);
        if (s >= 0) { p.pos = {B[s].anchor.x + B[s].facing * 110 - 10, B[s].anchor.y - 10}; B[s].act = BeastAct::Strike; B[s].actT = -1.0f; B[s].stunT = 0; }
    }
    if (which == 2) {
        p.fauna.apexT = 999; p.fauna.calmT = 999; p.fauna.tension = 0;
        int m = -1;
        for (int f = 0; f < 60 && m < 0; f++) { BeastsUpdate(p, 1 / 60.0f); m = find(HS_MEGALODON); }
        if (m >= 0) { B[m].pos = {p.pos.x - 6 * 32.0f, p.pos.y - 3 * 32.0f}; B[m].facing = 1; B[m].act = BeastAct::Coil; B[m].actT = 0.5f; B[m].target = BEAST_DIVER; }
    }
}
// The Grand Kraken on the fleet: 0 a slam winding up over the diver, 1 two arms wrapped round a ship about to snap, 2 just after the snap.
static void ShotKraken(Game& g, int which) {
    g.platLayouts[PL_PIRATE] = {606, 100, 0, 0x7fffffff};
    StartPlatform(g, PL_PIRATE);
    PlatformState& p = g.plat;
    if (p.snaps.empty()) return;
    const GenSnap s = p.snaps[0];
    int cx = s.col + 6, cy = s.bottom - 4 - p.genTop - 1; // standing on the deck (row Ds), not on a yard up the mast
    p.pos = {cx * 32.0f + 6, (cy + 1) * 32.0f - 26}; p.onGround = true;
    p.fauna.apexT = 999; p.fauna.calmT = 999; p.fauna.tension = 0;
    BeastsUpdate(p, 1 / 60.0f);
    for (auto& b : p.fauna.beasts) {
        if (b.life != BeastLife::Alive || b.species != PS_KRAKEN) continue;
        Rectangle d = PlatDiverBox(p);
        if (which == 0) { b.target = 1; b.act = BeastAct::Coil; b.actT = -0.5f; b.anchor = {d.x + 5 * 32.0f, p.waterY}; b.goal = {d.x + 10, d.y + d.height}; b.special = 5; }
        else { b.target = 2; b.carry = 0; b.act = BeastAct::Coil; b.actT = which == 1 ? 0.2f : 2.1f; b.special = 5; }
    }
    if (which == 2) for (int f = 0; f < 20; f++) BeastsUpdate(p, 1 / 60.0f);
}
static const char* gShotFilter = nullptr; // depth.exe --shots <folder> <text>: only screens whose name contains <text>
void DebugWardrobe(Game& g, int game); void TakeShots(const Game& base, const std::string& dir) {
    skins::gNoSave = true;   // (shots play counts and match ends: the player's wardrobe file is never touched)
    struct Shot { const char* name; std::function<void(Game&)> setup; };
    const Shot shots[] = {
        {"hub", [](Game& g) { g.scene = Scene::Hub; }},
        {"hub_cat", [](Game& g) { g.scene = Scene::Hub; DebugPetCat(); }},
        {"hub_hover_crew", [](Game& g) { g.scene = Scene::Hub; DebugSalonHover(0); }},
        {"hub_hover_library", [](Game& g) { g.scene = Scene::Hub; DebugSalonHover(1); }},
        {"hub_hover_radar", [](Game& g) { g.scene = Scene::Hub; DebugSalonHover(2); }},
        {"hub_hover_helm", [](Game& g) { g.scene = Scene::Hub; DebugSalonHover(3); }},
        {"hub_hover_periscope", [](Game& g) { g.scene = Scene::Hub; DebugSalonHover(4); }},
        {"hub_hover_workshop", [](Game& g) { g.scene = Scene::Hub; DebugSalonHover(5); }},
        {"hub_hover_sickbay", [](Game& g) { g.scene = Scene::Hub; DebugSalonHover(6); }},
        {"hub_hover_ward", [](Game& g) { g.scene = Scene::Hub; DebugSalonHover(7); }},
        {"hub_hover_cards", [](Game& g) { g.scene = Scene::Hub; DebugSalonHover(8); }},
        {"hub_hover_arcade", [](Game& g) { g.scene = Scene::Hub; DebugSalonHover(9); }},
        {"hub_hover_study", [](Game& g) { g.scene = Scene::Hub; DebugSalonHover(10); }},
        {"study", [](Game& g) { g.scene = Scene::Study; study::DebugStudyShot(0); }},
        {"study_lounge", [](Game& g) { g.scene = Scene::Study; study::DebugStudyShot(1); }},
        {"study_dim80", [](Game& g) { g.scene = Scene::Study; study::DebugStudyShot(2); }},
        {"study_lounge_dim80", [](Game& g) { g.scene = Scene::Study; study::DebugStudyShot(3); }},
        {"study_lounge_reduce_motion", [](Game& g) { g.scene = Scene::Study; study::DebugStudyShot(4); }},
        {"study_drawer_soundscape", [](Game& g) { g.scene = Scene::Study; study::DebugStudyShot(5); }},
        {"study_drawer_scene", [](Game& g) { g.scene = Scene::Study; study::DebugStudyShot(6); }},
        {"study_drawer_courses", [](Game& g) { g.scene = Scene::Study; study::DebugStudyShot(7); }},
        {"study_chronometer", [](Game& g) { g.scene = Scene::Study; study::DebugStudyShot(8); }},
        {"study_descent", [](Game& g) { g.scene = Scene::Study; study::DebugStudyShot(9); }},
        {"arcade", [](Game& g) { g.scene = Scene::Arcade; }},
        {"arcade_lobby", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeShot(0); }},
        {"arcade_table", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeShot(1); }},
        {"arcade_result", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeShot(2); }},
        {"arcade_rules", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeShot(3); }},
        {"arcade_bets", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeShot(10); }},
        {"arcade_redtide", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeReel(4); }},
        {"arcade_custom", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeReel(104); }},
        {"skins_trawl", [](Game& g) { DebugWardrobe(g, 0); }},
        {"skins_redtide", [](Game& g) { DebugWardrobe(g, 1); }},
        {"skins_gallery_trawl", [](Game& g) { DebugTrawlShot(g, 58); }},   // (DEPTH_SKINPAGE=0..7: ten skins a page)
        {"skins_gallery_redtide", [](Game& g) { DebugRedTideShot(g, 206); }},
        {"costumes_gallery_trawl", [](Game& g) { DebugTrawlShot(g, 59); }},   // (DEPTH_SKINPAGE=0..1)
        {"costumes_gallery_redtide", [](Game& g) { DebugRedTideShot(g, 207); }},
        {"costumes_wardrobe_trawl", [](Game& g) { DebugWardrobe(g, 2); }},
        {"costumes_wardrobe_redtide", [](Game& g) { DebugWardrobe(g, 3); }},
        {"arcade_trawl", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeReel(1); }},
        {"mouthful_fry", [](Game& g) { DebugMouthfulShot(g, 0); }}, {"mouthful_reef", [](Game& g) { DebugMouthfulShot(g, 1); }},
        {"mouthful_wall", [](Game& g) { DebugMouthfulShot(g, 2); }}, {"mouthful_blue", [](Game& g) { DebugMouthfulShot(g, 3); }},
        {"mouthful_trench", [](Game& g) { DebugMouthfulShot(g, 4); }}, {"mouthful_fork", [](Game& g) { DebugMouthfulShot(g, 5); }},
        {"mouthful_king", [](Game& g) { DebugMouthfulShot(g, 6); }}, {"mouthful_results", [](Game& g) { DebugMouthfulShot(g, 7); }},
        {"mouthful_lineup", [](Game& g) { DebugMouthfulShot(g, 8); }}, {"mouthful_guest", [](Game& g) { DebugMouthfulShot(g, 9); }},
        {"mouthful_boat", [](Game& g) { DebugMouthfulShot(g, 10); }}, {"mouthful_orcas", [](Game& g) { DebugMouthfulShot(g, 11); }},
        {"mouthful_redtide", [](Game& g) { DebugMouthfulShot(g, 12); }}, {"mouthful_whalefall", [](Game& g) { DebugMouthfulShot(g, 13); }},
        {"mouthful_dusk", [](Game& g) { DebugMouthfulShot(g, 14); }},
        {"mouthful_skins", [](Game& g) { DebugMouthfulShot(g, 15); }},
        {"night_door", [](Game& g) { DebugNightOffShot(g, 0); }}, {"night_menu", [](Game& g) { DebugNightOffShot(g, 1); }},
        {"night_hammered", [](Game& g) { DebugNightOffShot(g, 2); }}, {"night_snug", [](Game& g) { DebugNightOffShot(g, 3); }},
        {"night_passedout", [](Game& g) { DebugNightOffShot(g, 4); }}, {"night_morning", [](Game& g) { DebugNightOffShot(g, 5); }},
        {"night_crowd", [](Game& g) { DebugNightOffShot(g, 6); }}, {"night_talk", [](Game& g) { DebugNightOffShot(g, 7); }},
        {"night_game_darts", [](Game& g) { DebugNightOffShot(g, 8); }}, {"night_game_pool", [](Game& g) { DebugNightOffShot(g, 9); }},
        {"night_game_golf", [](Game& g) { DebugNightOffShot(g, 10); }}, {"night_game_slots", [](Game& g) { DebugNightOffShot(g, 11); }},
        {"night_game_scratch", [](Game& g) { DebugNightOffShot(g, 12); }}, {"night_game_fortune", [](Game& g) { DebugNightOffShot(g, 13); }},
        {"night_game_menu", [](Game& g) { DebugNightOffShot(g, 14); }},
        {"night_brawl", [](Game& g) { DebugNightOffShot(g, 15); }}, {"night_wreck", [](Game& g) { DebugNightOffShot(g, 16); }}, {"night_dog", [](Game& g) { DebugNightOffShot(g, 17); }},
        {"night_guest", [](Game& g) { DebugNightOffShot(g, 20); }}, {"arcade_night", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeReel(7); }},
        {"night_flirt", [](Game& g) { DebugNightOffShot(g, 18); }}, {"night_emote", [](Game& g) { DebugNightOffShot(g, 27); }}, {"scuffle_fight", [](Game& g) { DebugScuffleShot(g, 0); }}, {"scuffle_haymaker", [](Game& g) { DebugScuffleShot(g, 1); }}, {"scuffle_match", [](Game& g) { DebugScuffleShot(g, 2); }}, {"scuffle_flood", [](Game& g) { DebugScuffleShot(g, 3); }}, {"scuffle_editor", [](Game& g) { DebugScuffleEditorShot(g, 0); }}, {"scuffle_guest", [](Game& g) { DebugScuffleShot(g, 4); }}, {"scuffle_arsenal", [](Game& g) { DebugScuffleShot(g, 5); }}, {"scuffle_blackout", [](Game& g) { DebugScuffleShot(g, 6); }}, {"scuffle_mode_teams", [](Game& g) { DebugScuffleShot(g, 41); }}, {"scuffle_mode_king", [](Game& g) { DebugScuffleShot(g, 42); }}, {"scuffle_mode_egg", [](Game& g) { DebugScuffleShot(g, 43); }}, {"scuffle_mode_hunt", [](Game& g) { DebugScuffleShot(g, 45); }}, {"scuffle_mode_duel", [](Game& g) { DebugScuffleShot(g, 46); }}, {"scuffle_mode_gauntlet", [](Game& g) { DebugScuffleShot(g, 49); }}, {"scuffle_boss_lobster", [](Game& g) { DebugScuffleShot(g, 60); }}, {"scuffle_boss_lobster_late", [](Game& g) { DebugScuffleShot(g, 66); }}, {"scuffle_boss_kraken", [](Game& g) { DebugScuffleShot(g, 61); }}, {"scuffle_boss_kraken_late", [](Game& g) { DebugScuffleShot(g, 67); }}, {"scuffle_boss_wyrm", [](Game& g) { DebugScuffleShot(g, 62); }}, {"scuffle_boss_wyrm_late", [](Game& g) { DebugScuffleShot(g, 68); }}, {"scuffle_boss_sungod", [](Game& g) { DebugScuffleShot(g, 63); }}, {"scuffle_boss_sungod_late", [](Game& g) { DebugScuffleShot(g, 69); }}, {"scuffle_boss_goliath", [](Game& g) { DebugScuffleShot(g, 64); }}, {"scuffle_boss_goliath_late", [](Game& g) { DebugScuffleShot(g, 70); }}, {"scuffle_boss_bouncer", [](Game& g) { DebugScuffleShot(g, 65); }}, {"scuffle_boss_bouncer_late", [](Game& g) { DebugScuffleShot(g, 71); }}, {"scuffle_mode_boss", [](Game& g) { DebugScuffleShot(g, 60); }}, {"scuffle_replay", [](Game& g) { DebugScuffleShot(g, 90); }}, {"scuffle_locker", [](Game& g) { sf::gLockerNoSave = true; sf::MyLocker().tokens = 140; sf::MyLocker().owned = {"brass", "neon", "sailor", "crown", "pixel", "redtide", "goat", "beanie"}; sf::MyLocker().skin = "neon"; sf::MyLocker().hat = "crown"; DebugArcadeScuffleLocker(g, 0); }}, {"scuffle_locker_hats", [](Game& g) { DebugArcadeScuffleLocker(g, 1); }}, {"scuffle_wardrobe_0", [](Game& g) { DebugScuffleShot(g, 80); }}, {"scuffle_wardrobe_1", [](Game& g) { DebugScuffleShot(g, 81); }}, {"scuffle_wardrobe_2", [](Game& g) { DebugScuffleShot(g, 82); }}, {"scuffle_wardrobe_3", [](Game& g) { DebugScuffleShot(g, 83); }}, {"scuffle_wardrobe_4", [](Game& g) { DebugScuffleShot(g, 84); }},{"scuffle_world_cave", [](Game& g) { DebugScuffleShot(g, 11); }}, {"scuffle_world_reef", [](Game& g) { DebugScuffleShot(g, 12); }}, {"scuffle_world_atlantis", [](Game& g) { DebugScuffleShot(g, 13); }}, {"scuffle_world_void", [](Game& g) { DebugScuffleShot(g, 14); }}, {"scuffle_world_salon", [](Game& g) { DebugScuffleShot(g, 15); }}, {"scuffle_wall_cave", [](Game& g) { DebugScuffleShot(g, 21); }}, {"scuffle_wall_reef", [](Game& g) { DebugScuffleShot(g, 22); }}, {"scuffle_wall_atlantis", [](Game& g) { DebugScuffleShot(g, 23); }}, {"scuffle_wall_void", [](Game& g) { DebugScuffleShot(g, 24); }}, {"scuffle_wall_salon", [](Game& g) { DebugScuffleShot(g, 25); }}, {"scuffle_finale_nautilus", [](Game& g) { DebugScuffleShot(g, 30); }}, {"scuffle_finale_cave", [](Game& g) { DebugScuffleShot(g, 31); }}, {"scuffle_finale_reef", [](Game& g) { DebugScuffleShot(g, 32); }}, {"scuffle_finale_atlantis", [](Game& g) { DebugScuffleShot(g, 33); }}, {"scuffle_finale_void", [](Game& g) { DebugScuffleShot(g, 34); }}, {"scuffle_finale_salon", [](Game& g) { DebugScuffleShot(g, 35); }},{"arcade_lobby_scuffle", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeShot(11); }},{"scuffle_editor_engine", [](Game& g) { DebugScuffleEditorShot(g, 1); }},{"arcade_scuffle", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeReel(8); }}, {"bp_atrium", [](Game& g) { DebugBallPitShot(g, 0); }}, {"bp_bridge", [](Game& g) { DebugBallPitShot(g, 1); }}, {"bp_cannon", [](Game& g) { DebugBallPitShot(g, 2); }}, {"bp_store", [](Game& g) { DebugBallPitShot(g, 3); }}, {"bp_submerged", [](Game& g) { DebugBallPitShot(g, 4); }}, {"bp_slide", [](Game& g) { DebugBallPitShot(g, 5); }}, {"bp_ground", [](Game& g) { DebugBallPitShot(g, 6); }}, {"bp_over", [](Game& g) { DebugBallPitShot(g, 7); }}, {"bp_rewards", [](Game& g) { DebugBallPitShot(g, 8); }}, {"bp_flag", [](Game& g) { DebugBallPitShot(g, 9); }}, {"bp_overview", [](Game& g) { DebugBallPitShot(g, 10); }}, {"bp_down", [](Game& g) { DebugBallPitShot(g, 11); }}, {"bp_bomb", [](Game& g) { DebugBallPitShot(g, 12); }}, {"bp_perf", [](Game& g) { DebugBallPitShot(g, 13); }}, {"bp_shopfront", [](Game& g) { DebugBallPitShot(g, 14); }}, {"bp_kids", [](Game& g) { DebugBallPitShot(g, 15); }}, {"arcade_ballpit", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeReel(12); }}, {"fa_start", [](Game& g) { DebugFathomsShot(g, 0); }}, {"fa_steam", [](Game& g) { DebugFathomsShot(g, 1); }}, {"fa_battle", [](Game& g) { DebugFathomsShot(g, 2); }}, {"fa_four", [](Game& g) { DebugFathomsShot(g, 3); }}, {"fa_close", [](Game& g) { DebugFathomsShot(g, 4); }}, {"fa_map", [](Game& g) { DebugFathomsShot(g, 5); }}, {"fa_volcano", [](Game& g) { DebugFathomsShot(g, 6); }}, {"fa_units", [](Game& g) { DebugFathomsShot(g, 7); }}, {"fa_navy", [](Game& g) { DebugFathomsShot(g, 8); }}, {"fa_town", [](Game& g) { DebugFathomsShot(g, 9); }}, {"fa_card_worker", [](Game& g) { DebugFathomsShot(g, 10); }}, {"fa_card_harbor", [](Game& g) { DebugFathomsShot(g, 11); }}, {"fa_panel_cove", [](Game& g) { DebugFathomsShot(g, 12); }}, {"arcade_fathoms", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeReel(3); }}, {"warp_rush", [](Game& g) { DebugWarpShot(g, 0); }}, {"warp_portals", [](Game& g) { DebugWarpShot(g, 1); }}, {"warp_extreme", [](Game& g) { DebugWarpShot(g, 2); }}, {"warp_catch", [](Game& g) { DebugWarpShot(g, 3); }}, {"warp_bench", [](Game& g) { DebugWarpShot(g, 4); }}, {"warp_won", [](Game& g) { DebugWarpShot(g, 5); }}, {"warp_window", [](Game& g) { DebugWarpShot(g, 6); }}, {"arcade_warp", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeReel(9); }}, {"fowl_hunt", [](Game& g) { DebugFowlShot(g, 0); }}, {"fowl_room", [](Game& g) { DebugFowlShot(g, 1); }}, {"fowl_gumball", [](Game& g) { DebugFowlShot(g, 10); }}, {"fowl_slots", [](Game& g) { DebugFowlShot(g, 11); }}, {"fowl_counter", [](Game& g) { DebugFowlShot(g, 2); }}, {"fowl_booth_guns", [](Game& g) { DebugFowlShot(g, 12); }}, {"fowl_booth_slop", [](Game& g) { DebugFowlShot(g, 13); }}, {"fowl_tally", [](Game& g) { DebugFowlShot(g, 3); }}, {"fowl_slop", [](Game& g) { DebugFowlShot(g, 4); }}, {"fowl_sabotage", [](Game& g) { DebugFowlShot(g, 5); }}, {"fowl_podium", [](Game& g) { DebugFowlShot(g, 6); }}, {"fowl_round13", [](Game& g) { DebugFowlShot(g, 7); }}, {"fowl_bonus", [](Game& g) { DebugFowlShot(g, 8); }}, {"fowl_night", [](Game& g) { DebugFowlShot(g, 9); }}, {"arcade_fowl", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeReel(10); }}, {"fowl_locker", [](Game& g) { DebugArcadeFowlLocker(g); }}, {"noclip_lobby", [](Game& g) { DebugNoclipShot(g, 0); }}, {"noclip_lab", [](Game& g) { DebugNoclipShot(g, 1); }}, {"noclip_surface", [](Game& g) { DebugNoclipShot(g, 2); }}, {"noclip_pipes", [](Game& g) { DebugNoclipShot(g, 3); }}, {"noclip_lightsout", [](Game& g) { DebugNoclipShot(g, 4); }}, {"noclip_map", [](Game& g) { DebugNoclipShot(g, 5); }}, {"noclip_desk", [](Game& g) { DebugNoclipShot(g, 6); }}, {"noclip_hound", [](Game& g) { DebugNoclipShot(g, 7); }}, {"noclip_insane", [](Game& g) { DebugNoclipShot(g, 8); }}, {"noclip_suburbs", [](Game& g) { DebugNoclipShot(g, 9); }}, {"arcade_noclip", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeReel(11); }}, {"noclip_locker", [](Game& g) { DebugArcadeNoclipLocker(g); }}, {"scuffle_training", [](Game& g) { DebugScuffleTraining(g); }}, {"noclip_crew", [](Game& g) { DebugNoclipShot(g, 10); }}, {"noclip_cast", [](Game& g) { DebugNoclipShot(g, 11); }}, {"noclip_beasts", [](Game& g) { DebugNoclipShot(g, 12); }}, {"noclip_bearing", [](Game& g) { DebugNoclipShot(g, 13); }}, {"noclip_bearmap", [](Game& g) { DebugNoclipShot(g, 14); }}, {"night_wares", [](Game& g) { DebugNightOffShot(g, 28); }}, {"night_cloakroom", [](Game& g) { DebugNightCloakroom(0); g.scene = Scene::Arcade; gNoCloakShot = true; }}, {"night_skins_0", [](Game& g) { DebugNightOffShot(g, 60); }}, {"night_skins_1", [](Game& g) { DebugNightOffShot(g, 61); }}, {"night_skins_2", [](Game& g) { DebugNightOffShot(g, 62); }}, {"night_skins_3", [](Game& g) { DebugNightOffShot(g, 63); }}, {"night_skins_4", [](Game& g) { DebugNightOffShot(g, 64); }}, {"night_skins_5", [](Game& g) { DebugNightOffShot(g, 65); }}, {"night_skins_6", [](Game& g) { DebugNightOffShot(g, 66); }}, {"night_skins_7", [](Game& g) { DebugNightOffShot(g, 67); }}, {"night_skins_8", [](Game& g) { DebugNightOffShot(g, 68); }}, {"night_season_festival", [](Game& g) { DebugNightOffShot(g, 50); }}, {"night_season_storm", [](Game& g) { DebugNightOffShot(g, 51); }}, {"night_season_wedding", [](Game& g) { DebugNightOffShot(g, 52); }}, {"night_season_masquerade", [](Game& g) { DebugNightOffShot(g, 56); }}, {"night_monkey_bar", [](Game& g) { DebugNightOffShot(g, 40); }}, {"night_monkey_library", [](Game& g) { DebugNightOffShot(g, 41); }}, {"night_monkey_roof", [](Game& g) { DebugNightOffShot(g, 42); }}, {"night_monkey_rope", [](Game& g) { DebugNightOffShot(g, 43); }}, {"night_monkey_billiards", [](Game& g) { DebugNightOffShot(g, 44); }}, {"night_angler", [](Game& g) { DebugNightOffShot(g, 29); }},
        {"night_ev_bikers", [](Game& g) { DebugNightOffShot(g, 21); }}, {"night_ev_robbery", [](Game& g) { DebugNightOffShot(g, 22); }},
        {"night_ev_band", [](Game& g) { DebugNightOffShot(g, 23); }}, {"night_cards_poker", [](Game& g) { DebugNightOffShot(g, 25); }}, {"night_cards_bullshit", [](Game& g) { DebugNightOffShot(g, 26); }}, {"night_ev_police", [](Game& g) { DebugNightOffShot(g, 24); }}, {"night_morning_kidney", [](Game& g) { DebugNightOffShot(g, 19); }},
        {"mouthful_wardrobe", [](Game& g) { DebugMouthfulWardrobe(); g.scene = Scene::Arcade; DebugArcadeReel(206); }},
        {"arcade_mouthful", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeReel(6); }},
        {"flight_dawn", [](Game& g) { DebugFlightShot(g, 0); }},
        {"flight_strike", [](Game& g) { DebugFlightShot(g, 1); }},
        {"flight_nest", [](Game& g) { DebugFlightShot(g, 2); }},
        {"flight_high", [](Game& g) { DebugFlightShot(g, 3); }},
        {"flight_colony", [](Game& g) { DebugFlightShot(g, 4); }},
        {"flight_panel", [](Game& g) { DebugFlightShot(g, 5); }},
        {"flight_stack", [](Game& g) { DebugFlightShot(g, 6); }},
        {"flight_town", [](Game& g) { DebugFlightShot(g, 7); }},
        {"flight_atoll", [](Game& g) { DebugFlightShot(g, 8); }},
        {"flight_chart", [](Game& g) { DebugFlightShot(g, 9); }},
        {"flight_battle", [](Game& g) { DebugFlightShot(g, 10); }},
        {"flight_flocks", [](Game& g) { DebugFlightShot(g, 11); }},
        {"flight_chartwar", [](Game& g) { DebugFlightShot(g, 12); }},
        {"flight_society", [](Game& g) { DebugFlightShot(g, 13); }},
        {"flight_results", [](Game& g) { DebugFlightShot(g, 14); }},
        {"flight_guest", [](Game& g) { DebugFlightShot(g, 15); }},
        {"flight_kraken", [](Game& g) { DebugFlightShot(g, 16); }},
        {"flight_ape", [](Game& g) { DebugFlightShot(g, 17); }},
        {"flight_volcano", [](Game& g) { DebugFlightShot(g, 18); }},
        {"flight_storm", [](Game& g) { DebugFlightShot(g, 19); }},
        {"flight_outpost", [](Game& g) { DebugFlightShot(g, 20); }},
        {"flight_long", [](Game& g) { DebugFlightShot(g, 21); }},
        {"flight_visitor", [](Game& g) { DebugFlightShot(g, 22); }},
        {"flight_isle_iceberg", [](Game& g) { DebugFlightShot(g, 23); }}, {"flight_isle_lighthouse", [](Game& g) { DebugFlightShot(g, 24); }},
        {"flight_isle_shipwreck", [](Game& g) { DebugFlightShot(g, 25); }}, {"flight_isle_mangrove", [](Game& g) { DebugFlightShot(g, 26); }},
        {"flight_isle_kelpraft", [](Game& g) { DebugFlightShot(g, 27); }}, {"flight_isle_clifftown", [](Game& g) { DebugFlightShot(g, 28); }},
        {"flight_isle_iron", [](Game& g) { DebugFlightShot(g, 29); }}, {"flight_isle_whale", [](Game& g) { DebugFlightShot(g, 30); }},
        {"flight_isle_siren", [](Game& g) { DebugFlightShot(g, 31); }}, {"flight_isle_maelstrom", [](Game& g) { DebugFlightShot(g, 32); }},
        {"flight_isle_ghost", [](Game& g) { DebugFlightShot(g, 33); }}, {"flight_isle_bird", [](Game& g) { DebugFlightShot(g, 34); }},
        {"flight_lf_fog", [](Game& g) { DebugFlightShot(g, 35); }}, {"flight_lf_roc", [](Game& g) { DebugFlightShot(g, 36); }},
        {"flight_lf_sunken", [](Game& g) { DebugFlightShot(g, 37); }}, {"flight_lf_thorns", [](Game& g) { DebugFlightShot(g, 38); }},
        {"flight_lf_page_year", [](Game& g) { DebugFlightShot(g, 39); }}, {"flight_lf_page_powers", [](Game& g) { DebugFlightShot(g, 40); }},
        {"flight_lf_page_building", [](Game& g) { DebugFlightShot(g, 41); }}, {"flight_lf_page_dynasty", [](Game& g) { DebugFlightShot(g, 42); }},
        {"flight_wardrobe", [](Game& g) { DebugArcadeFlightWardrobe(g, 0, "captain_nemo", -1); }},
        {"flight_wardrobe_eggs", [](Game& g) { DebugArcadeFlightWardrobe(g, 1, "phoenix", -1); }},
        {"flight_wardrobe_livery", [](Game& g) { DebugArcadeFlightWardrobe(g, 2, "top_hats", -1); }},
        {"flight_costumes_1", [](Game& g) { DebugArcadeFlightWardrobe(g, 0, nullptr, 0); }},
        {"flight_costumes_2", [](Game& g) { DebugArcadeFlightWardrobe(g, 0, nullptr, 1); }},
        {"flight_costumes_3", [](Game& g) { DebugArcadeFlightWardrobe(g, 0, nullptr, 2); }},
        {"flight_costumes_4", [](Game& g) { DebugArcadeFlightWardrobe(g, 0, nullptr, 3); }},
        {"arcade_flight", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeReel(5); }},
        {"trawl_deck", [](Game& g) { DebugTrawlShot(g, 0); }},
        {"trawl_engine", [](Game& g) { DebugTrawlShot(g, 1); }},
        {"trawl_wheelhouse", [](Game& g) { DebugTrawlShot(g, 2); }},
        {"trawl_squall", [](Game& g) { DebugTrawlShot(g, 3); }},
        {"trawl_fishon", [](Game& g) { DebugTrawlShot(g, 4); }},
        {"trawl_jump", [](Game& g) { DebugTrawlShot(g, 5); }},
        {"trawl_lagoon", [](Game& g) { DebugTrawlShot(g, 6); }},
        {"trawl_searchlight", [](Game& g) { DebugTrawlShot(g, 7); }},
        {"trawl_shark", [](Game& g) { DebugTrawlShot(g, 8); }},
        {"trawl_dock", [](Game& g) { DebugTrawlShot(g, 9); }},
        {"trawl_chandler", [](Game& g) { DebugTrawlShot(g, 10); }},
        {"trawl_market", [](Game& g) { DebugTrawlShot(g, 11); }},
        {"trawl_scales", [](Game& g) { DebugTrawlShot(g, 25); }},
        {"trawl_gunsmith", [](Game& g) { DebugTrawlShot(g, 26); }},
        {"trawl_skiff", [](Game& g) { DebugTrawlShot(g, 27); }},
        {"trawl_davit", [](Game& g) { DebugTrawlShot(g, 28); }},
        {"trawl3d_skiff", [](Game& g) { DebugTrawlShot(g, 127); }},
        {"trawl3d_davit", [](Game& g) { DebugTrawlShot(g, 128); }},
        {"trawl_atoll", [](Game& g) { DebugTrawlShot(g, 29); }},
        {"trawl_lighthouse", [](Game& g) { DebugTrawlShot(g, 37); }},
        {"trawl_sandbar", [](Game& g) { DebugTrawlShot(g, 38); }},
        {"trawl3d_lighthouse", [](Game& g) { DebugTrawlShot(g, 137); }},
        {"trawl3d_sandbar", [](Game& g) { DebugTrawlShot(g, 138); }},
        {"trawl_cup", [](Game& g) { DebugTrawlShot(g, 39); }},
        {"trawl3d_cup", [](Game& g) { DebugTrawlShot(g, 139); }},
        {"trawl_chalkboard", [](Game& g) { DebugTrawlShot(g, 30); }},
        {"trawl_roleups", [](Game& g) { DebugTrawlShot(g, 57); }},
        {"trawl_weeds", [](Game& g) { DebugTrawlShot(g, 31); }},
        {"trawl3d_weeds", [](Game& g) { DebugTrawlShot(g, 131); }},
        {"trawl_weeds_threats", [](Game& g) { DebugTrawlShot(g, 32); }},
        {"trawl3d_weeds_threats", [](Game& g) { DebugTrawlShot(g, 132); }},
        {"trawl_grotto", [](Game& g) { DebugTrawlShot(g, 33); }},
        {"trawl3d_grotto", [](Game& g) { DebugTrawlShot(g, 133); }},
        {"trawl_atlantis", [](Game& g) { DebugTrawlShot(g, 34); }},
        {"trawl3d_atlantis", [](Game& g) { DebugTrawlShot(g, 134); }},
        {"trawl_dive", [](Game& g) { DebugTrawlShot(g, 35); }},
        {"trawl3d_atoll", [](Game& g) { DebugTrawlShot(g, 129); }},
        {"trawl_chart", [](Game& g) { DebugTrawlShot(g, 12); }},
        {"trawl_clock", [](Game& g) { DebugTrawlShot(g, 13); }},
        {"trawl_quota", [](Game& g) { DebugTrawlShot(g, 14); }},
        {"trawl_net", [](Game& g) { DebugTrawlShot(g, 15); }},
        {"trawl_rifle", [](Game& g) { DebugTrawlShot(g, 16); }},
        {"trawl_overboard", [](Game& g) { DebugTrawlShot(g, 17); }},
        {"trawl_ghost", [](Game& g) { DebugTrawlShot(g, 18); }},
        {"trawl_harpoon", [](Game& g) { DebugTrawlShot(g, 19); }},
        {"trawl_locker", [](Game& g) { DebugTrawlShot(g, 20); }},
        {"trawl_bots", [](Game& g) { DebugTrawlShot(g, 21); }},
        {"trawl_guest", [](Game& g) { DebugTrawlShot(g, 22); }},
        {"trawl_sonar", [](Game& g) { DebugTrawlShot(g, 23); }},
        {"trawl_helmchart", [](Game& g) { DebugTrawlShot(g, 24); }},
        {"trawl3d_helmchart", [](Game& g) { DebugTrawlShot(g, 124); }},
        {"trawl3d_guest", [](Game& g) { DebugTrawlShot(g, 122); }},
        {"trawl3d_bots", [](Game& g) { DebugTrawlShot(g, 121); }},
        {"trawl3d_deck", [](Game& g) { DebugTrawlShot(g, 100); }},
        {"trawl3d_engine", [](Game& g) { DebugTrawlShot(g, 101); }},
        {"trawl3d_wheelhouse", [](Game& g) { DebugTrawlShot(g, 102); }},
        {"trawl3d_squall", [](Game& g) { DebugTrawlShot(g, 103); }},
        {"trawl3d_spray", [](Game& g) { DebugTrawlShot(g, 136); }},
        // the Trawl Visual Overhaul Spec's harness (--shots shots/vis tvis_): the same eight views before and after
        // each phase
        {"tvis_1_helm_fog", [](Game& g) { DebugTrawlShot(g, 140); }},
        {"tvis_2_gutting_rain", [](Game& g) { DebugTrawlShot(g, 141); }},
        {"tvis_3_dock", [](Game& g) { DebugTrawlShot(g, 109); }},
        {"tvis_4_carbine_rain", [](Game& g) { DebugTrawlShot(g, 143); }},
        {"tvis_5_starboard_rod", [](Game& g) { DebugTrawlShot(g, 104); }},
        {"tvis_6_roles", [](Game& g) { DebugTrawlShot(g, 145); }},
        {"tvis_6_faces", [](Game& g) { DebugTrawlShot(g, 146); }},
        {"tvis_7_guns", [](Game& g) { DebugTrawlShot(g, 147); }},
        {"tvis_7_test_head", [](Game& g) { DebugTrawlShot(g, 148); }},
        {"tvis_8_bots", [](Game& g) { DebugTrawlShot(g, 149); }},
        {"tvis_9_fp_rig", [](Game& g) { DebugTrawlShot(g, 150); }},
        {"tvis_7_gun_states", [](Game& g) { DebugTrawlShot(g, 151); }},
    {"tvis_10_boat_bow", [](Game& g) { DebugTrawlShot(g, 152); }},
    {"tvis_10_boat_stern", [](Game& g) { DebugTrawlShot(g, 153); }},
    {"tvis_10_boat_deck", [](Game& g) { DebugTrawlShot(g, 154); }},
    {"tvis_11_fish", [](Game& g) { DebugTrawlShot(g, 155); }},
    {"tvis_12_six_rain", [](Game& g) { DebugTrawlShot(g, 142); }},
    {"tvis_13_expressions", [](Game& g) { DebugTrawlShot(g, 160); }}, {"tvis_14_ground_lagoon", [](Game& g) { DebugTrawlShot(g, 161); }}, {"tvis_14_ground_weeds", [](Game& g) { DebugTrawlShot(g, 162); }}, {"tvis_14_ground_grotto", [](Game& g) { DebugTrawlShot(g, 163); }}, {"tvis_14_ground_atlantis", [](Game& g) { DebugTrawlShot(g, 164); }},
    {"tvis_11_deck_catch", [](Game& g) { DebugTrawlShot(g, 156); }},
        {"tvis_4_sidearm_rain", [](Game& g) { DebugTrawlShot(g, 144); }},
        {"trawl3d_fishon", [](Game& g) { DebugTrawlShot(g, 104); }},
        {"trawl3d_jump", [](Game& g) { DebugTrawlShot(g, 105); }},
        {"trawl3d_lagoon", [](Game& g) { DebugTrawlShot(g, 106); }},
        {"trawl3d_searchlight", [](Game& g) { DebugTrawlShot(g, 107); }},
        {"trawl3d_shark", [](Game& g) { DebugTrawlShot(g, 108); }},
        {"trawl3d_dock", [](Game& g) { DebugTrawlShot(g, 109); }},
        {"trawl3d_net", [](Game& g) { DebugTrawlShot(g, 115); }},
        {"trawl3d_rifle", [](Game& g) { DebugTrawlShot(g, 116); }},
        {"trawl3d_overboard", [](Game& g) { DebugTrawlShot(g, 117); }},
        {"trawl3d_ghost", [](Game& g) { DebugTrawlShot(g, 118); }},
        {"trawl3d_harpoon", [](Game& g) { DebugTrawlShot(g, 119); }},
        {"redtide_tank", [](Game& g) { DebugRedTideShot(g, 0); }},
        {"redtide_guest", [](Game& g) { DebugRedTideShot(g, 90); }},
        {"rvis_3_divers", [](Game& g) { DebugRedTideShot(g, 200); }},
        {"rvis_3_faces", [](Game& g) { DebugRedTideShot(g, 201); }},
        {"rvis_3_swim", [](Game& g) { DebugRedTideShot(g, 202); }},
        {"rvis_3_skins", [](Game& g) { DebugRedTideShot(g, 203); }},
        {"rvis_4_guns", [](Game& g) { DebugRedTideShot(g, 204); }},
        {"rvis_3_crew_salon", [](Game& g) { DebugRedTideShot(g, 17); }},
        {"rvis_6_wreckers", [](Game& g) { DebugRedTideShot(g, 18); }}, {"rvis_8_raiders", [](Game& g) { DebugRedTideShot(g, 38); }}, {"rvis_9_lostones", [](Game& g) { DebugRedTideShot(g, 48); }},
        {"rvis_3_hands", [](Game& g) { DebugRedTideShot(g, 19); }},
        {"rvis_3_handstudio", [](Game& g) { DebugRedTideShot(g, 205); }},
        {"rvis_7_drowned", [](Game& g) { DebugRedTideShot(g, 26); }},
        {"rvis_10_remnant", [](Game& g) { DebugRedTideShot(g, 58); }},
        {"redtide_silhouette", [](Game& g) { DebugRedTideShot(g, 1); }},
        {"redtide_species_ship_1", [](Game& g) { DebugRedTideShot(g, 2); }},
        {"redtide_species_ship_2", [](Game& g) { DebugRedTideShot(g, 3); }},
        {"redtide_ship_bridge", [](Game& g) { DebugRedTideShot(g, 10); }},
        {"redtide_ship_salon", [](Game& g) { DebugRedTideShot(g, 11); }},
        {"redtide_ship_engine", [](Game& g) { DebugRedTideShot(g, 12); }},
        {"redtide_ship_keel", [](Game& g) { DebugRedTideShot(g, 13); }},
        {"redtide_ship_hunt", [](Game& g) { DebugRedTideShot(g, 14); }},
        {"redtide_ship_cabins", [](Game& g) { DebugRedTideShot(g, 15); }},
        {"redtide_ship_salvage", [](Game& g) { DebugRedTideShot(g, 16); }},
        {"redtide_cave_mouth", [](Game& g) { DebugRedTideShot(g, 20); }},
        {"redtide_cave_gallery", [](Game& g) { DebugRedTideShot(g, 21); }},
        {"redtide_cave_chimney", [](Game& g) { DebugRedTideShot(g, 22); }},
        {"redtide_cave_dry", [](Game& g) { DebugRedTideShot(g, 23); }},
        {"redtide_cave_cathedral", [](Game& g) { DebugRedTideShot(g, 24); }},
        {"redtide_cave_sump", [](Game& g) { DebugRedTideShot(g, 25); }},
        {"redtide_reef_lagoon", [](Game& g) { DebugRedTideShot(g, 30); }},
        {"redtide_reef_forest", [](Game& g) { DebugRedTideShot(g, 31); }},
        {"redtide_reef_bommie", [](Game& g) { DebugRedTideShot(g, 32); }},
        {"redtide_reef_wall", [](Game& g) { DebugRedTideShot(g, 33); }},
        {"redtide_reef_bluehole", [](Game& g) { DebugRedTideShot(g, 34); }},
        {"redtide_reef_matriarch", [](Game& g) { DebugRedTideShot(g, 35); }},
        {"redtide_atlantis_gate", [](Game& g) { DebugRedTideShot(g, 40); }},
        {"redtide_atlantis_town", [](Game& g) { DebugRedTideShot(g, 41); }},
        {"redtide_atlantis_forum", [](Game& g) { DebugRedTideShot(g, 42); }},
        {"redtide_atlantis_chapel", [](Game& g) { DebugRedTideShot(g, 43); }},
        {"redtide_atlantis_wall", [](Game& g) { DebugRedTideShot(g, 44); }},
        {"redtide_atlantis_wyrm", [](Game& g) { DebugRedTideShot(g, 45); }},
        {"redtide_void_rim", [](Game& g) { DebugRedTideShot(g, 50); }},
        {"redtide_void_galleries", [](Game& g) { DebugRedTideShot(g, 51); }},
        {"redtide_void_labs", [](Game& g) { DebugRedTideShot(g, 52); }},
        {"redtide_void_vault", [](Game& g) { DebugRedTideShot(g, 53); }},
        {"redtide_void_warrens", [](Game& g) { DebugRedTideShot(g, 54); }},
        {"redtide_void_overlook", [](Game& g) { DebugRedTideShot(g, 55); }},
        {"redtide_page_dossier", [](Game& g) { rt::DebugRedTidePage(1, 0, 6); g.scene = Scene::RedTide; }},
        {"redtide_page_dossier_locked", [](Game& g) { rt::DebugRedTidePage(1, 2, 1); g.scene = Scene::RedTide; }},
        {"redtide_page_records", [](Game& g) { rt::DebugRedTidePage(2, 0, -1); g.scene = Scene::RedTide; }},
        {"redtide_page_howto", [](Game& g) { rt::DebugRedTidePage(3, 0, -1); g.scene = Scene::RedTide; }},
        {"redtide_page_charms", [](Game& g) { rt::DebugRedTidePage(4, 0, -1); g.scene = Scene::RedTide; }},
        {"redtide_page_locker", [](Game& g) { rt::DebugRedTidePage(5, 0, -1); g.scene = Scene::RedTide; }},
        {"panel_ward", [](Game& g) { g.scene = Scene::Ward; Hero& h = g.roster[1]; g.selectedHero = h.id; h.hp = h.hp / 2; h.ailments = (1u << AIL_SALT_ROT) | (1u << AIL_BENDS); h.habits = (1u << HB_STEADY_HANDS) | (1u << HB_NIGHT_EYES) | (1u << HB_JUMPY); h.habitLocked = 1u << HB_NIGHT_EYES; g.gold = 400; }},
        {"panel_sickbay", [](Game& g) { g.scene = Scene::SickLeave; }},
        {"flats_menu", [](Game& g) { g.scene = Scene::Cards; }},
        {"flats_play", [](Game& g) { g.scene = Scene::Cards; DebugFlatsDeal(); }},
        {"flats_combat", [](Game& g) { g.scene = Scene::Cards; DebugFlatsCombat(); }},
        {"flats_won", [](Game& g) { g.scene = Scene::Cards; DebugFlatsWon(); }},
        {"flats_boon", [](Game& g) { g.scene = Scene::Cards; DebugFlatsBoon(); }},
        {"flats_map", [](Game& g) { g.scene = Scene::Cards; DebugFlatsMap(); }},
        {"flats_campfire", [](Game& g) { g.scene = Scene::Cards; DebugFlatsCampfire(); }},
        {"flats_reward", [](Game& g) { g.scene = Scene::Cards; DebugFlatsReward(); }},
        {"flats_shop", [](Game& g) { g.scene = Scene::Cards; DebugFlatsShop(); }},
        {"flats_deck", [](Game& g) { g.scene = Scene::Cards; DebugFlatsDeck(); }},
        {"hub_leave", [](Game& g) { g.scene = Scene::Hub; g.roster[0].onLeave = 1; g.roster[1].rattled = true; }},
        {"crew", [](Game& g) { g.scene = Scene::Crew; g.roster[1].level = 3; g.selectedHero = g.roster[1].id; }},
        {"helm", [](Game& g) { g.scene = Scene::Helm; g.tierCleared[(int)Location::Cave] = 1; g.tierSel[(int)Location::Cave] = 2; }},
        {"radar", [](Game& g) { g.scene = Scene::Radar; }},
        {"workshop", [](Game& g) { g.scene = Scene::Workshop; g.gold = 500; g.upgrades[UP_BUNKS] = 1; }},
        {"workshop_drill", [](Game& g) { g.scene = Scene::Workshop; g.gold = 500; DebugWorkshopTab(1); g.selectedHero = g.roster[0].id; g.roster[0].drill[0] = 2; g.roster[0].drill[1] = 1; }},
        {"combat_elite", [](Game& g) { g.tierCleared[(int)Location::Cave] = 4; g.tierSel[(int)Location::Cave] = 4; DebugEnterCombat(g); for (auto& e : g.dungeon.enemies) if (!e.boss) e.elite = true; }},
        {"library", [](Game& g) { g.scene = Scene::Bookshelf; g.bookTab = 1; }},
        {"library_memorial", [](Game& g) { g.scene = Scene::Bookshelf; g.bookTab = 5; MemorialEntry me; me.name = "Ishmael"; me.cls = 2; me.level = 4; me.cause = "slain by The Lobster in the Cave"; g.memorial.push_back(me); me.name = "Queequeg"; me.cls = 4; me.level = 2; me.cause = "bled out in the Weeds"; g.memorial.push_back(me); }},
        {"library_log", [](Game& g) { g.scene = Scene::Bookshelf; g.bookTab = 6; SeaLog(g, "First met: Sea Louse (the Cave). Fast and fragile; its bites bleed."); SeaLog(g, "A barnacled sea chest: Inside: 18 gold."); SeaLog(g, "Slew The Lobster in the Cave (cave level 1)."); }},
        {"hub_voyage_salvager", [](Game& g) { g.scene = Scene::Hub; g.voyageEvent = VE_SALVAGER; g.salvagerStock = {3, 9}; g.gold = 300; }},
        {"hub_voyage_sharp", [](Game& g) { g.scene = Scene::Hub; g.voyageEvent = VE_CARD_SHARP; }},
        {"combat", [](Game& g) { g.dungeon.light = 60; DebugEnterCombat(g); }},
        {"abyss", [](Game& g) { StartAbyss(g); g.scene = Scene::Abyss; }},
        {"abyss_deep", [](Game& g) { StartAbyss(g); g.scene = Scene::Abyss; g.abyss.playerPos.y = -120.0f; }},
        {"abyss_vent", [](Game& g) { StartAbyss(g); g.scene = Scene::Abyss; g.abyss.playerPos.y = -145.0f; }},
        {"abyss_bowling", [](Game& g) { StartAbyss(g); g.scene = Scene::Abyss; g.abyss.playerPos.y = -330.0f; }},
        {"abyss_maze", [](Game& g) { StartAbyss(g); g.scene = Scene::Abyss; g.abyss.playerPos.y = -485.0f; }},
        {"abyss_brinepool", [](Game& g) { StartAbyss(g); g.scene = Scene::Abyss; g.abyss.playerPos.y = -625.0f; }},
        {"abyss_leviathan", [](Game& g) { StartAbyss(g); g.scene = Scene::Abyss; g.abyss.playerPos.y = -250.0f;
                                           for (auto& c : g.abyss.creatures) if (c.kind == AbyssCreatureKind::Leviathan) {
                                               c.state = AbyssCreatureState::Passing; c.stateTimer = 0; c.pos = {2, -250, 2};
                                           } }},
        {"abyss_won", [](Game& g) { StartAbyss(g); g.scene = Scene::Abyss; g.abyss.depth = ABYSS_DEPTH_SPAN - 12; g.abyss.won = true; g.abyss.awarded = true; }},
        {"abyss_dead", [](Game& g) { StartAbyss(g); g.scene = Scene::Abyss; g.abyss.stamina = 0; g.abyss.dead = true; g.abyss.awarded = true; }},
        // Verification only, for the painted-art rollout (depth.exe --gen-crew-art): every class rendered in
        // combat, four at a time, so the new skeletal/painted CharacterRenderer path is exercised for all twelve.
        {"crew_art_1", [](Game& g) { g.dungeon.light = 60; DebugEnterCombat(g); HeroClass cls[4] = {HeroClass::Nurse, HeroClass::Diver, HeroClass::Captain, HeroClass::Mechanic};
                                      for (int p = 0; p < PARTY_SIZE; p++) if (Hero* h = FindHero(g, g.party[p])) h->cls = cls[p]; }},
        {"crew_art_2", [](Game& g) { g.dungeon.light = 60; DebugEnterCombat(g); HeroClass cls[4] = {HeroClass::Whaler, HeroClass::Stowaway, HeroClass::Merman, HeroClass::Queen};
                                      for (int p = 0; p < PARTY_SIZE; p++) if (Hero* h = FindHero(g, g.party[p])) h->cls = cls[p]; }},
        {"crew_art_3", [](Game& g) { g.dungeon.light = 60; DebugEnterCombat(g); HeroClass cls[4] = {HeroClass::Robot, HeroClass::Octopus, HeroClass::Siren, HeroClass::Wisp};
                                      for (int p = 0; p < PARTY_SIZE; p++) if (Hero* h = FindHero(g, g.party[p])) h->cls = cls[p]; }},
        {"combat_dark", [](Game& g) { DebugEnterCombat(g); g.dungeon.light = 10; }},
        {"chart", [](Game& g) { g.tierSel[0] = 2; g.tierCleared[0] = 4; StartDungeon(g, Location::Cave); g.dungeon.corridorT = 5; g.dungeon.light = 80; g.dungeon.lightShown = 80; }},
        {"chart_dark", [](Game& g) { g.tierSel[0] = 4; g.tierCleared[0] = 4; g.upgrades[UP_SONAR] = 3; StartDungeon(g, Location::Cave); g.dungeon.corridorT = 5; g.dungeon.light = 15; g.dungeon.lightShown = 15; }},
        {"chart_walk", [](Game& g) { StartDungeon(g, Location::Cave); auto n = g.dungeon.chart.Neighbours(g.dungeon.curRoom); g.dungeon.chart.edges[g.dungeon.chart.EdgeBetween(g.dungeon.curRoom, n[0])].segs.assign(3, CorridorEvent::None); DebugChartWalk(g, n[0]); }},
        {"chart_curio", [](Game& g) { StartDungeon(g, Location::Atlantis); DebugChartEvent(g, 1); }},
        {"chart_camp", [](Game& g) { StartDungeon(g, Location::Weeds); DebugChartEvent(g, 2); }},
        {"helm_objective", [](Game& g) { g.scene = Scene::Helm; g.objectiveSel = Objective::Chart; }},
        {"chart_campfire", [](Game& g) { StartDungeon(g, Location::Cave); DebugChartEvent(g, 3); }},
        {"helm_quartermaster", [](Game& g) { g.scene = Scene::Helm; g.gold = 300; SuggestedKit(Location::Island, g.provision); }},
        {"menu_main", [](Game& g) { g.scene = Scene::Hub; }},
        {"menu_settings", [](Game& g) { g.scene = Scene::Hub; }},
        {"menu_controls", [](Game& g) { g.scene = Scene::Hub; }}, {"menu_howto", [](Game& g) { g.scene = Scene::Hub; }},
        {"menu_voice", [](Game& g) { g.scene = Scene::Arcade; }},
        {"menu_graphics", [](Game& g) { DebugTrawlShot(g, 140); }},
        {"combat_walk", [](Game& g) { DebugEnterCombat(g); g.dungeon.phase = DPhase::Walking; g.dungeon.walkT = 0.4f; }},
        {"combat_deep", [](Game& g) { g.tierCleared[(int)Location::Cave] = 4; g.tierSel[(int)Location::Cave] = 3; DebugEnterCombat(g); }},
        {"boss_sun_phase2", [](Game& g) { DebugSetEnemies(g, Location::Island, {EnemyType::SunGod, EnemyType::TribalShaman}); g.dungeon.enemies[0].hp = g.dungeon.enemies[0].maxHp / 3; }},
        {"boss_leviathan", [](Game& g) { g.tierCleared[(int)Location::Trench] = 4; g.tierSel[(int)Location::Trench] = 5; DebugSetEnemies(g, Location::Trench, {EnemyType::Leviathan, EnemyType::LanternAngler}); g.dungeon.pressure = 3; }},
        {"boss_abyssal_eye", [](Game& g) { DebugSetEnemies(g, Location::Hadal, {EnemyType::AbyssalEye, EnemyType::StarSpawn}); }},
        {"boss_abyssal_eye3", [](Game& g) { DebugSetEnemies(g, Location::Hadal, {EnemyType::AbyssalEye, EnemyType::DrownedOracle}); g.dungeon.enemies[0].hp = g.dungeon.enemies[0].maxHp / 4; }},
        {"helm_deep", [](Game& g) { g.scene = Scene::Helm; for (int l = 0; l < 4; l++) g.tierCleared[l] = 4; DebugHelmDeep(); }},
        {"foes_new_cave", [](Game& g) { DebugSetEnemies(g, Location::Cave, {EnemyType::BarnacleCrab, EnemyType::SeaLouse, EnemyType::LanternAngler, EnemyType::LanternAngler}); }},
        {"foes_new_island", [](Game& g) { DebugSetEnemies(g, Location::Island, {EnemyType::FireDancer, EnemyType::IdolBearer, EnemyType::FireDancer, EnemyType::IdolBearer}); }},
        {"foes_new_weeds", [](Game& g) { DebugSetEnemies(g, Location::Weeds, {EnemyType::MantisShrimp, EnemyType::KelpWraith, EnemyType::MantisShrimp, EnemyType::KelpWraith}); }},
        {"foes_new_atlantis", [](Game& g) { DebugSetEnemies(g, Location::Atlantis, {EnemyType::StarSpawn, EnemyType::DrownedOracle, EnemyType::StarSpawn, EnemyType::DrownedOracle}); }},
        {"foes_crab", [](Game& g) { DebugSetEnemies(g, Location::Cave, {EnemyType::SeaLouse, EnemyType::CaveShrimp, EnemyType::DysCrustacean, EnemyType::SeaLouse}); }},
        {"boss_lobster", [](Game& g) { DebugSetEnemies(g, Location::Cave, {EnemyType::Lobster, EnemyType::CaveShrimp, EnemyType::SeaLouse}); }},
        {"boss_queen", [](Game& g) { DebugSetEnemies(g, Location::Cave, {EnemyType::CrustaceanQueen, EnemyType::DysCrustacean}); }},
        {"foes_tribal", [](Game& g) { DebugSetEnemies(g, Location::Island, {EnemyType::TribalSpearman, EnemyType::WarDog, EnemyType::TribalShaman, EnemyType::TribalSpearman}); }},
        {"boss_demigod", [](Game& g) { DebugSetEnemies(g, Location::Island, {EnemyType::TribalDemigod, EnemyType::WarDog, EnemyType::TribalShaman}); }},        {"foes_merfolk", [](Game& g) { DebugSetEnemies(g, Location::Weeds, {EnemyType::FeralMerman, EnemyType::Siren, EnemyType::FeralMerman, EnemyType::Siren}); }},
        {"boss_neptune", [](Game& g) { DebugSetEnemies(g, Location::Weeds, {EnemyType::Neptune, EnemyType::FeralMerman}); }},        {"foes_weeds", [](Game& g) { DebugSetEnemies(g, Location::Weeds, {EnemyType::GiantOctopus, EnemyType::ElectricEel, EnemyType::GiantOctopus}); }},
        {"boss_shark", [](Game& g) { DebugSetEnemies(g, Location::Weeds, {EnemyType::GreatWhite, EnemyType::GiantOctopus, EnemyType::Siren}); }},        {"foes_atlantis", [](Game& g) { DebugSetEnemies(g, Location::Atlantis, {EnemyType::LostInfantry, EnemyType::LostCultist, EnemyType::LostInfantry, EnemyType::LostCultist}); }},
        {"boss_armored", [](Game& g) { DebugSetEnemies(g, Location::Atlantis, {EnemyType::ArmorLostOne, EnemyType::LostCultist, EnemyType::LostInfantry}); }},
        {"boss_alien", [](Game& g) { DebugSetEnemies(g, Location::Atlantis, {EnemyType::AlienHorror, EnemyType::LostInfantry, EnemyType::LostCultist}); }},
        {"boss_cthulhu", [](Game& g) { DebugSetEnemies(g, Location::Atlantis, {EnemyType::Cthulhu, EnemyType::LostCultist}); }},        {"foes_cave2", [](Game& g) { DebugSetEnemies(g, Location::Cave, {EnemyType::BrineWorm, EnemyType::GhostWorm, EnemyType::BrineWorm}); }},
        {"boss_diver", [](Game& g) { DebugSetEnemies(g, Location::Cave, {EnemyType::LostDiver, EnemyType::BrineWorm, EnemyType::SeaLouse}); }},
        {"boss_coconut", [](Game& g) { DebugSetEnemies(g, Location::Island, {EnemyType::CoconutQueen, EnemyType::TribalSpearman, EnemyType::WarDog}); }},
        {"boss_sun", [](Game& g) { DebugSetEnemies(g, Location::Island, {EnemyType::SunGod, EnemyType::TribalShaman}); }},        {"island", [](Game& g) { DebugEnterCombat(g, Location::Island); }},
        {"weeds", [](Game& g) { DebugEnterCombat(g, Location::Weeds); }},
        {"atlantis", [](Game& g) { DebugEnterCombat(g, Location::Atlantis); }},
        {"inventory", [](Game& g) { DebugEnterCombat(g); g.dungeon.phase = DPhase::RoomClear; g.dungeon.roomGold = 24;
                                     g.dungeon.pendingItem = true; g.dungeon.pendingItemVal = {ItemKind::Relic, 4};
                                     g.dungeon.inventory = {{ItemKind::Battery}, {ItemKind::Bandage}, {ItemKind::Key}, {ItemKind::Relic, 1}}; }},
        {"chest", [](Game& g) { DebugEnterCombat(g); g.dungeon.phase = DPhase::Treasure; g.dungeon.roomIsChest = true; g.dungeon.chestOpened = false;
                                 g.dungeon.inventory = {{ItemKind::Key}}; }},
        {"periscope", [](Game& g) { g.scene = Scene::Periscope; g.platCleared[0] = true; }},
        {"periscope_abyss", [](Game& g) { g.scene = Scene::Periscope; g.platCleared[0] = g.platCleared[1] = g.platCleared[2] = true; g.abyssBest = 410; }},        {"periscope_folder", [](Game& g) { g.scene = Scene::Periscope; g.periscopeSel = PL_ISLAND; }},        {"dossier_island", [](Game& g) { g.scene = Scene::Periscope; g.dossier = PL_ISLAND; g.platSeen[PL_ISLAND] = 0x5A5B7ull; g.dossierPick = IS_BOAR; }},        {"dossier_weeds", [](Game& g) { g.scene = Scene::Periscope; g.dossier = PL_WEEDS; g.platSeen[PL_WEEDS] = ~0ull; g.dossierPick = WS_SHARK; }},        {"dossier_abyss", [](Game& g) { g.scene = Scene::Periscope; g.dossier = PL_COUNT; g.platSeen[PL_COUNT] = 0x1F0F7ull; g.dossierPick = 13; }},
        {"pipes", [](Game& g) { StartPlatform(g, PL_PIPES); }},
        {"pipes_riser", [](Game& g) { g.platLayouts[PL_PIPES] = {101, 100}; StartPlatform(g, PL_PIPES); g.plat.pos = g.plat.spawns[1]; }},
        {"pipes_shaft", [](Game& g) { g.platLayouts[PL_PIPES] = {202, 100}; StartPlatform(g, PL_PIPES); g.plat.pos = g.plat.spawns[3]; }},
        {"pipes_twins", [](Game& g) { g.platLayouts[PL_PIPES] = {303, 100}; StartPlatform(g, PL_PIPES); g.plat.pos = g.plat.spawns[2]; }},
        {"pipes_critters", [](Game& g) { g.platLayouts[PL_PIPES] = {303, 100}; StartPlatform(g, PL_PIPES);
                                          Vector2 c = g.plat.critters.size() > 3 ? g.plat.critters[3].home : Vector2{700, 450};
                                          g.plat.pos = {c.x - 130, c.y - 26}; }},
        {"hull", [](Game& g) { g.platLayouts[PL_HULL] = {404, 100}; StartPlatform(g, PL_HULL); g.plat.pos = g.plat.spawns[1]; }},
        {"hull_shaft", [](Game& g) { g.platLayouts[PL_HULL] = {505, 100}; StartPlatform(g, PL_HULL); g.plat.pos = g.plat.spawns[3]; }},        {"hull_kraken", [](Game& g) { StartPlatform(g, PL_HULL); g.plat.pos = {(g.plat.w - 24) * 32 + 420.0f, 200}; g.plat.boss.state = 2; }},        {"hull_kraken_sweep", [](Game& g) { StartPlatform(g, PL_HULL); g.plat.pos = {(g.plat.w - 24) * 32 + 760.0f, 200}; PlatformState& q = g.plat; int c = (int)(q.pos.x / 32), r = (int)(q.pos.y / 32); while (r < q.h - 1 && q.tiles[r][c] != '#') r++; q.pos.y = r * 32.0f - 26; q.onGround = true; g.plat.boss.sweepT = -0.0001f; g.plat.boss.sweepT = 0.001f - 0.35f + 0.35f; g.plat.boss.sweepDir = 1; g.plat.boss.sweepY = g.plat.pos.y + 26; }},
        {"pirate", [](Game& g) { g.platLayouts[PL_PIRATE] = {606, 100}; StartPlatform(g, PL_PIRATE); g.plat.pos = g.plat.spawns[1]; }},
        {"fauna_hull", [](Game& g) { ShotAtFauna(g, PL_HULL, 404, 6); }},
        {"fauna_hull2", [](Game& g) { ShotAtFauna(g, PL_HULL, 404, 6, 1); }},
        {"fauna_hull13_whale", [](Game& g) { ShotHull13(g, 0); }},
        {"pirate_kraken_slam", [](Game& g) { ShotKraken(g, 0); }},
        {"atlantis13_leviathan", [](Game& g) { ShotAtFauna(g, PL_ATLANTIS, 808, 2, 0, AS_OLEVIATHAN); }},
        {"weeds13_manatee", [](Game& g) { ShotAtFauna(g, PL_WEEDS, 707, 2, 0, WS_MANATEE); }},
        {"weeds13_mantis", [](Game& g) { ShotAtFauna(g, PL_WEEDS, 707, 2, 0, WS_HMANTIS); }},
        {"island13_beetle", [](Game& g) { ShotAtFauna(g, PL_ISLAND, 303, 2, 0, IS_GBEETLE); }},
        {"island13_flora", [](Game& g) { ShotAtFauna(g, PL_ISLAND, 303, 2, 0, IS_DRUM); }},
        {"island13_stalker", [](Game& g) { ShotAtFauna(g, PL_ISLAND, 303, 2, 0, IS_MSTALKER); }},
        {"cave13_tortoise", [](Game& g) { ShotAtFauna(g, PL_CAVE, 505, 2, 0, CS_CTORTOISE); }},
        {"cave13_stalker", [](Game& g) { ShotAtFauna(g, PL_CAVE, 505, 2, 0, CS_STALKER); }},
        {"cave13_flora", [](Game& g) { ShotAtFauna(g, PL_CAVE, 505, 2, 0, CS_LSHROOM); }},
        {"cave13_arachnid", [](Game& g) { ShotAtFauna(g, PL_CAVE, 505, 1, 0, CS_CABBAGE); PlatformState& p = g.plat; p.fauna.apexT = 999; p.fauna.calmT = 999; p.vel = {200, 0}; p.onGround = true; BeastsUpdate(p, 1 / 60.0f); p.vel = {0, 0}; p.fauna.glowSuitT = 5; for (auto& w : p.fauna.webs) w.revealed = true; if (!p.fauna.webs.empty()) p.pos.x = p.fauna.webs[0].top.x - 5 * 32; }},
        {"pirate13_mimic", [](Game& g) { g.platLayouts[PL_PIRATE] = {606, 100, 0, 0}; ShotAtFauna(g, PL_PIRATE, 606, 2, 0, PS_CUTTLE); }},
        {"pirate13_tortoise", [](Game& g) { ShotAtFauna(g, PL_PIRATE, 606, 2, 0, PS_TORTOISE); }},
        {"pirate13_mantis", [](Game& g) { ShotAtFauna(g, PL_PIRATE, 606, 2, 0, PS_MANTIS); }},
        {"pirate13_flora", [](Game& g) { ShotAtFauna(g, PL_PIRATE, 606, 2, 0, PS_MKELP); }},
        {"pirate_kraken_wrap", [](Game& g) { ShotKraken(g, 1); }},
        {"pirate_kraken_snapped", [](Game& g) { ShotKraken(g, 2); }},
        {"fauna_hull13_siphon", [](Game& g) { ShotHull13(g, 1); }},
        {"fauna_hull13_megalodon", [](Game& g) { ShotHull13(g, 2); }},
        {"fauna_hull13_flora", [](Game& g) { ShotHull13(g, 3); }},
        {"fauna_pirate", [](Game& g) { ShotAtFauna(g, PL_PIRATE, 606, 6); }},
        {"fauna_pirate2", [](Game& g) { ShotAtFauna(g, PL_PIRATE, 606, 6, 1); }},
        {"fauna_island", [](Game& g) { ShotAtFauna(g, PL_ISLAND, 303, 6); }},
        {"fauna_island2", [](Game& g) { ShotAtFauna(g, PL_ISLAND, 303, 6, 1); }},
        {"fauna_cave", [](Game& g) { ShotAtFauna(g, PL_CAVE, 505, 6); }},
        {"fauna_cave2", [](Game& g) { ShotAtFauna(g, PL_CAVE, 505, 6, 1); }},
        {"fauna_pipes", [](Game& g) { ShotAtFauna(g, PL_PIPES, 303, 6); }},
        {"fauna_pipes2", [](Game& g) { ShotAtFauna(g, PL_PIPES, 303, 6, 1); }},        {"plat_weeds", [](Game& g) { g.platLayouts[PL_WEEDS] = {707, 100}; StartPlatform(g, PL_WEEDS); }},
        {"plat_weeds2", [](Game& g) { g.platLayouts[PL_WEEDS] = {707, 100}; StartPlatform(g, PL_WEEDS); g.plat.pos = g.plat.spawns[4]; }},
        {"plat_atlantis", [](Game& g) { g.platLayouts[PL_ATLANTIS] = {808, 100}; StartPlatform(g, PL_ATLANTIS); }},
        {"plat_atlantis2", [](Game& g) { g.platLayouts[PL_ATLANTIS] = {808, 100}; StartPlatform(g, PL_ATLANTIS); g.plat.pos = g.plat.spawns[4]; }},
        {"fauna_weeds", [](Game& g) { ShotAtFauna(g, PL_WEEDS, 707, 6); }},
        {"fauna_weeds_shark", [](Game& g) { ShotAtFauna(g, PL_WEEDS, 707, 4, 0, WS_SHARK); }},
        {"fauna_weeds_merman", [](Game& g) { ShotAtFauna(g, PL_WEEDS, 712, 2, 0, WS_MERMAN); }},
        {"fauna_weeds_ray", [](Game& g) { ShotAtFauna(g, PL_WEEDS, 707, 4, 0, WS_RAY); }},
        {"fauna_weeds_lineup", [](Game& g) { // well-fed, calm big animals posed beside the diver, to judge their size
            g.platLayouts[PL_WEEDS] = {707, 100}; StartPlatform(g, PL_WEEDS);
            PlatformState& p = g.plat;
            for (auto& b : p.fauna.beasts) b.life = BeastLife::Gone;
            int kinds[4] = {WS_MERMAN, WS_SHARK, WS_RAY, WS_BARRACUDA};
            for (int k = 0; k < 4; k++) {
                p.fauna.beasts.emplace_back();
                Beast& b = p.fauna.beasts.back();
                b.species = kinds[k]; b.id = 9000 + k; b.pos = {p.pos.x + 120 + k * 130.0f, p.pos.y - 60 - (k % 2) * 40}; b.facing = -1; b.hunger = 0; b.act = BeastAct::Idle; b.thinkT = 99;
                for (auto& s : b.spine) s = {b.pos.x + 20, b.pos.y};
            }
        }},
        {"fauna_atlantis_angler", [](Game& g) { ShotAtFauna(g, PL_ATLANTIS, 808, 4, 0, AS_ANGLER); }},
        {"fauna_atlantis_guardian", [](Game& g) { ShotAtFauna(g, PL_ATLANTIS, 808, 4, 0, AS_GUARDIAN); }},
        {"fauna_weeds2", [](Game& g) { ShotAtFauna(g, PL_WEEDS, 707, 6, 1); }},
        {"fauna_atlantis", [](Game& g) { ShotAtFauna(g, PL_ATLANTIS, 808, 6); }},
        {"fauna_atlantis2", [](Game& g) { ShotAtFauna(g, PL_ATLANTIS, 808, 6, 1); }},
        {"island", [](Game& g) { g.platLayouts[PL_ISLAND] = {303, 100}; StartPlatform(g, PL_ISLAND); }},
        {"island_village", [](Game& g) { g.platLayouts[PL_ISLAND] = {304, 100}; StartPlatform(g, PL_ISLAND); }},
        {"cave2", [](Game& g) { g.platLayouts[PL_CAVE] = {505, 100}; StartPlatform(g, PL_CAVE); }},
        {"pirate_hatch", [](Game& g) { g.platLayouts[PL_PIRATE] = {707, 100}; StartPlatform(g, PL_PIRATE); g.plat.pos = g.plat.spawns[3]; }},
        {"pirate_hold", [](Game& g) { g.platHard = true; g.platLayouts[PL_PIRATE] = {808, 100}; StartPlatform(g, PL_PIRATE); g.plat.pos = g.plat.spawns[4]; }},
        {"pirate_stairs", [](Game& g) { g.platLayouts[PL_PIRATE] = {909, 100}; StartPlatform(g, PL_PIRATE); g.plat.pos = g.plat.spawns[6]; }},        {"pirate_cannon", [](Game& g) { ShotAtLauncher(g, PL_PIRATE, 'N'); }},
        {"pirate_barrel", [](Game& g) { ShotAtLauncher(g, PL_PIRATE, 'y'); }},
        {"hull_cavern", [](Game& g) {
            for (unsigned seed = 1; seed < 80; seed++) {
                GenLevel gl = GenerateLevel(PL_HULL, seed, 1.0f);
                for (size_t i = 1; i < gl.path.size(); i++)
                    if (gl.path[i].tag == SetPiece::ShaftDown && gl.rows[gl.exitRow - 9][gl.path[i].tx - 1] == 'R') { // a cavern: reef rock overhead
                        g.platLayouts[PL_HULL] = {(int)seed, 100};
                        StartPlatform(g, PL_HULL);
                        const GenWaypoint& w = gl.path[i - 1];
                        g.plat.pos = {w.tx * 32.0f + 6, (w.ty - g.plat.genTop + 1) * 32.0f - 26};
                        return;
                    }
            }
        }},
        {"pipes_piston", [](Game& g) { ShotAtPiece(g, PL_PIPES, SetPiece::PistonCorridor, false, 1); }},
        {"pirate_crossfire", [](Game& g) { ShotAtPiece(g, PL_PIRATE, SetPiece::GunCrossfire, false, 1); }},
        {"hull_vent",[](Game& g) { ShotAtPiece(g, PL_HULL, SetPiece::SteamBoost, false, 1); }},
        {"hull_torpedo",[](Game& g) { ShotAtLauncher(g, PL_HULL, 'T'); }},
        {"pipes_vent", [](Game& g) { ShotAtPiece(g, PL_PIPES, SetPiece::SteamBoost); g.plat.time = 0.4f; }},
        {"pipes_crumble", [](Game& g) { ShotAtPiece(g, PL_PIPES, SetPiece::CrumbleRun); }},
        {"hull_barnacle", [](Game& g) { ShotAtPiece(g, PL_HULL, SetPiece::BarnacleShaft); }},
        {"pirate_gap", [](Game& g) { ShotAtPiece(g, PL_PIRATE, SetPiece::ShipGap, false, 2); }},
        {"pirate_ladder", [](Game& g) { ShotAtPiece(g, PL_PIRATE, SetPiece::MastLadder, false, 1); }},
        {"pipes_drop", [](Game& g) { ShotAtPiece(g, PL_PIPES, SetPiece::PipeDrop, false, 1); }},
        {"pirate_ghost", [](Game& g) { ShotAtPiece(g, PL_PIRATE, SetPiece::ShipGap, true, 3); }},        {"pirate_boss", [](Game& g) { StartPlatform(g, PL_PIRATE); g.plat.pos = {(g.plat.w - 24) * 32 + 150.0f, g.plat.boss.home.y + 40}; }},        {"pirate_blunderbuss", [](Game& g) { StartPlatform(g, PL_PIRATE); g.plat.pos = {(g.plat.w - 24) * 32 + 150.0f, g.plat.boss.home.y + 40}; g.plat.boss.hp = 2; g.plat.boss.state = 6; g.plat.boss.timer = -2.0f; g.plat.boss.dir = -1; }},
    };
    std::vector<Shot> all(std::begin(shots), std::end(shots));
    // the Flats showcase: every card and component (depth.exe --shots shots/Flats flats)
    static const char* SHEETS[9] = {"cards_all_1_common", "cards_all_2_uncommon", "cards_all_3_rare", "cards_all_4_dealers_and_kraken", "cards_all_5_atlantis", "components_sigils", "components_charms_bottles_editions", "components_map_nodes", "components_dealers"};
    static std::vector<std::string> showNames;
    showNames.clear();
    showNames.reserve(9 + FlatsCatalogSize());
    for (int i = 0; i < 9; i++) { showNames.push_back(std::string("flats_") + SHEETS[i]); all.push_back({showNames.back().c_str(), [i](Game& g) { g.scene = Scene::Cards; DebugFlatsShowcase(i); }}); }
    for (int i = 1; i < FlatsCatalogSize(); i++) {
        std::string slug = FlatsCardName(i);
        for (char& ch : slug) ch = isalnum((unsigned char)ch) ? (char)tolower((unsigned char)ch) : '_';
        char buf[96]; snprintf(buf, sizeof buf, "flats_card_%02d_%s", i, slug.c_str());
        showNames.push_back(buf);
        all.push_back({showNames.back().c_str(), [i](Game& g) { g.scene = Scene::Cards; DebugFlatsShowcase(100 + i); }});
    }
    all.push_back({"lab_sprites_1", [](Game& g) { g.scene = Scene::Cards; DebugFlatsShowcase(9); }});
    all.push_back({"lab_sprites_2", [](Game& g) { g.scene = Scene::Cards; DebugFlatsShowcase(10); }});
    all.push_back({"flats_event_vents", [](Game& g) { g.scene = Scene::Cards; DebugFlatsVents(); }});
    all.push_back({"flats_event_scrimshaw", [](Game& g) { g.scene = Scene::Cards; DebugFlatsScrimshaw(); }});
    all.push_back({"flats_event_splicers", [](Game& g) { g.scene = Scene::Cards; DebugFlatsSplicers(); }});
    all.push_back({"flats_event_barnacle_cluster", [](Game& g) { g.scene = Scene::Cards; DebugFlatsBarnacle(); }});
    all.push_back({"flats_event_maelstrom", [](Game& g) { g.scene = Scene::Cards; DebugFlatsMaelstrom(); }});
    all.push_back({"flats_boss_phase1_drowned_phalanx", [](Game& g) { g.scene = Scene::Cards; DebugFlatsBoss(1); }});
    all.push_back({"flats_boss_phase2_lunar_tide", [](Game& g) { g.scene = Scene::Cards; DebugFlatsBoss(2); }});
    static char names[12][32];
    static const char* LN[4] = {"cave", "island", "weeds", "atlantis"};
    for (int loc = 0; loc < 4; loc++)
        for (int v = 0; v < 3; v++) { // every location in each of its three atmospheric states
            snprintf(names[loc * 3 + v], 32, "atm_%s_%d", LN[loc], v);
            all.push_back({names[loc * 3 + v], [loc, v](Game& g) { DebugEnterCombat(g, (Location)loc); g.dungeon.atmos = v; g.dungeon.visSeed = 4000u + loc * 77 + v * 13; }});
        }
    for (const auto& s : all) {
        if (gShotFilter && !strstr(s.name, gShotFilter)) continue;
        Game g = base;
        DebugSalonHover(-1);
        s.setup(g);
        // DEPTH_SHOTFRAMES=N: run N frames instead of 90 and print the average frame time (a frame-rate check on this PC)
        static int nFrames = getenv("DEPTH_SHOTFRAMES") ? std::max(2, atoi(getenv("DEPTH_SHOTFRAMES"))) : 90;
        double tFrames = 0;
        for (int f = 0; f < nFrames; f++) {
            if (f == nFrames / 2) tFrames = GetTime();
            g.time += 1 / 60.0f;
            if (f == 60 && strncmp(s.name, "menu_", 5) == 0) { SnapshotFrame(); DebugMenuPage(strstr(s.name, "howto") ? 5 : strstr(s.name, "settings") ? 1 : strstr(s.name, "controls") ? 2 : strstr(s.name, "graphics") ? 3 : strstr(s.name, "voice") ? 4 : 0); } // between frames, as in play
            BeginFrame();
            if (GameMenuActive()) GameMenuFrame(g); else RunScene(g);
            EndFrame(g.time);
        }
        if (getenv("DEPTH_SHOTFRAMES")) printf("frame time %s: %.2f ms (%d frames)\n", s.name, (GetTime() - tFrames) * 1000.0 / (nFrames - nFrames / 2), nFrames - nFrames / 2);
        std::string path = dir + "/" + s.name + ".png";
        TraceLog(LOG_INFO, "shot %s: %s", path.c_str(), SaveFrameShot(path.c_str()) ? "ok" : "FAILED");
    }
}

// Renders every sprite in the game onto eight pages and stitches them into one image.
static void MakeSpriteSheet(const std::string& path) {
    const std::function<void(float)> pages[16] = {
        [](float t) { DrawCrewSpritePage(t); },       [](float t) { DrawSalonSpritePage(t); },
        [](float t) { DrawCaveSpritePage(t); },       [](float t) { DrawPlatformSpritePage(0, t); },
        [](float t) { DrawPlatformSpritePage(1, t); }, [](float t) { DrawPlatformSpritePage(2, t); },
        [](float t) { FlatsSpritePage(t); },          [](float t) { DrawItemSpritePage(t); },
        [](float t) { DrawBestiarySpritePage(0, t); }, [](float t) { DrawBestiarySpritePage(1, t); }, [](float t) { DrawBestiarySpritePage(2, t); }, [](float t) { DrawPlatformSpritePage(3, t); },
        [](float t) { DrawPlatformSpritePage(4, t); }, [](float t) { DrawPlatformSpritePage(5, t); }, [](float t) { DrawPlatformSpritePage(6, t); }, [](float t) { DrawPlatformSpritePage(7, t); },
    };
    if (const char* one = getenv("DEPTH_PAGE")) { // DEPTH_PAGE=<n>: render just that page to the file
        int i = std::clamp(atoi(one), 0, 15);
        BeginFrame(); SetPost(0.0f, 0.0f, 0.0f);
        DrawVGradient({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Color{46, 50, 58, 255}, Color{24, 26, 32, 255});
        pages[i](1.3f); EndFrame(1.3f);
        Image page = GrabFrame(); ExportImage(page, path.c_str()); UnloadImage(page);
        return;
    }
    Image sheet = GenImageColor(SCREEN_W * 2, SCREEN_H * 6, BLACK);
    for (int i = 0; i < 12; i++) { // (pages 12-15, the biome galleries, render only one at a time via DEPTH_PAGE)
        BeginFrame();
        SetPost(0.0f, 0.0f, 0.0f);
        DrawVGradient({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Color{46, 50, 58, 255}, Color{24, 26, 32, 255});
        pages[i](1.3f);
        DrawRectangleLinesEx({0, 0, (float)SCREEN_W, (float)SCREEN_H}, 2, Pal::BrassDk);
        EndFrame(1.3f);
        Image page = GrabFrame();
        ImageDraw(&sheet, page, {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {(float)(i % 2) * SCREEN_W, (float)(i / 2) * SCREEN_H, (float)SCREEN_W, (float)SCREEN_H}, WHITE);
        UnloadImage(page);
    }
    TraceLog(LOG_INFO, "sprite sheet %s: %s", path.c_str(), ExportImage(sheet, path.c_str()) ? "ok" : "FAILED");
    UnloadImage(sheet);
}

// One full-page PNG per class - for actually inspecting one character's art at a time, not squinting at a
// dense multi-pose sheet or a shared tile a badly oversized rig can spill out of. Writes <basePath>_01.png
// through _12.png, one per HeroClass in enum order.
static void MakeCrewGallery(const std::string& basePath) {
    for (int c = 0; c < (int)HeroClass::COUNT; c++) {
        BeginFrame();
        SetPost(0.0f, 0.0f, 0.0f);
        DrawCrewGalleryPage((HeroClass)c, 1.3f);
        EndFrame(1.3f);
        Image page = GrabFrame();
        char num[4]; snprintf(num, sizeof(num), "%02d", c + 1);
        std::string path = basePath + "_" + num + ".png";
        TraceLog(LOG_INFO, "gallery %s: %s", path.c_str(), ExportImage(page, path.c_str()) ? "ok" : "FAILED");
        UnloadImage(page);
    }
}

int main(int argc, char** argv) {
    SetRandomSeed((unsigned int)time(nullptr));
    if (argc >= 4 && strcmp(argv[1], "--gen") == 0) { // developer: print a generated level as ASCII (level 0-2, seed, optional first/last column)
        GenLevel gl = GenerateLevel(atoi(argv[2]), (unsigned)atoi(argv[3]), 1.0f);
        int c0 = argc >= 5 ? atoi(argv[4]) : 0, c1 = argc >= 6 ? atoi(argv[5]) : std::min(gl.w, c0 + 120);
        printf("%dx%d, exit row %d, %d waypoints\n", gl.w, gl.h, gl.exitRow, (int)gl.path.size());
        for (int r = 0; r < gl.h; r++) printf("%2d %s\n", r, gl.rows[r].substr(std::min(c0, gl.w), std::max(0, std::min(c1, gl.w) - c0)).c_str());
        for (auto& wp : gl.path) printf("(%d,%d,%d) ", wp.tx, wp.ty, (int)wp.tag);
        printf("\n");
        return 0;
    }
    if (argc >= 6 && strcmp(argv[1], "--boss") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        SimulateBossFight(atoi(argv[2]), atoi(argv[3]), std::clamp(atoi(argv[4]), 0, CAVE_TIERS - 1), std::clamp(atoi(argv[5]), 0, (int)EnemyType::COUNT - 1), argc >= 7 && strcmp(argv[6], "random") == 0);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--sim") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        if (argc >= 7) gSimLocation = atoi(argv[6]);   // the location (0 Cave ... 4 Trench, 5 Hadal)
        SimulateExpeditions(argc >= 3 ? atoi(argv[2]) : 400, argc >= 4 ? atoi(argv[3]) : 0,
                            argc >= 5 && strcmp(argv[4], "random") == 0, argc >= 6 ? std::clamp(atoi(argv[5]), 0, CAVE_TIERS - 1) : 0);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--flats-sim") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        FlatsSim(argc >= 3 ? atoi(argv[2]) : 1000, argc >= 4 && strcmp(argv[3], "sensible") == 0);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--relic-test") == 0) { // the relic rules, without a window
        const auto& all = RelicRegistry::All();
        printf("%d relics\n", (int)all.size());
        int hard = 0;
        for (auto& r : all) hard += r.isHardWeapon;
        printf("hard weapons: %d\n", hard);
        Hero h;
        std::string why;
        int sword = -1, gun = -1, wrench = -1, pliers = -1, rivet = -1;
        for (int i = 0; i < (int)all.size(); i++) { if (all[i].name == "Sword") sword = i; if (all[i].name == "Tesla Gun") gun = i; if (all[i].name == "Wrench") wrench = i; if (all[i].name == "Pliers") pliers = i; if (all[i].name == "Rivet Gun") rivet = i; }
        h.relics[0] = sword;
        bool okGun = CanEquipRelic(h, gun, &why);
        printf("Sword then Tesla Gun: %s (%s)\n", okGun ? "allowed" : "refused", why.c_str());
        printf("Sword then Wrench: %s\n", CanEquipRelic(h, wrench, &why) ? "allowed" : "refused");
        h.relics[1] = wrench;
        bool okPliers = CanEquipRelic(h, pliers, &why);
        printf("third relic (Pliers): %s (%s)\n", okPliers ? "allowed" : "refused", why.c_str());
        RelicSynergy s1 = CheckRelicSynergies(all[rivet], all[pliers]);
        printf("Rivet Gun + Pliers: %s %s\n", s1.active ? "synergy" : "none", s1.text);
        for (int a = 0; a < (int)all.size(); a++) for (int b = a + 1; b < (int)all.size(); b++) { RelicSynergy s = CheckRelicSynergies(all[a], all[b]); if (s.active) printf("synergy: %s + %s -> %s\n", all[a].name.c_str(), all[b].name.c_str(), s.name); }
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--audio-test") == 0) return AudioSelfTest(argc >= 3 ? argv[2] : nullptr) ? 0 : 1;
    if (argc >= 2 && strcmp(argv[1], "--verify") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return VerifyPlatformLevels();
    }
    if (argc >= 2 && strcmp(argv[1], "--verify-critters") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return VerifyCritters() ? 0 : 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--verify-moves") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return VerifyMoves() ? 0 : 1;
    }
    // Red Tide (the Deep Arcade's survival shooter): the headless ecosystem tools
    if (argc >= 3 && strcmp(argv[1], "--eco-sim") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return rt::RunEcoSim(argv[2], argc >= 4 ? (float)atof(argv[3]) : 5.0f, argc >= 5 ? argv[4] : "sprat");
    }
    if (argc >= 3 && strcmp(argv[1], "--web-check") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        if (strcmp(argv[2], "all") == 0) { int r = 0; for (const char* k : {"ship", "cave", "reef", "atlantis", "void"}) r |= rt::RunWebCheck(k); return r; }
        return rt::RunWebCheck(argv[2]);
    }
    if (argc >= 2 && strcmp(argv[1], "--trawl-eco") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlEco(argc, argv); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-sim") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlSim(argc, argv); }
    if (argc >= 2 && strcmp(argv[1], "--redtide-voice-test") == 0) { SetTraceLogLevel(LOG_WARNING); return rt::RunRedTideVoiceTest(); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-voice-test") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlVoiceTest(); }
    if (argc >= 2 && strcmp(argv[1], "--flight-test") == 0) { SetTraceLogLevel(LOG_WARNING); return fl::RunFlightTest(); }
    if (argc >= 2 && strcmp(argv[1], "--flight-colony-test") == 0) { SetTraceLogLevel(LOG_WARNING); return fl::RunFlightColonyTest(); }
    if (argc >= 2 && strcmp(argv[1], "--flight-sim") == 0) { SetTraceLogLevel(LOG_WARNING); return fl::RunFlightSim(argc, argv); }
    if (argc >= 2 && strcmp(argv[1], "--flight-fish") == 0) { SetTraceLogLevel(LOG_WARNING); return fl::RunFlightFishTest(); }
    if (argc >= 2 && strcmp(argv[1], "--flight-fair") == 0) { SetTraceLogLevel(LOG_WARNING); return fl::RunFlightFairTest(argc, argv); }
    if (argc >= 2 && strcmp(argv[1], "--flight-danger-test") == 0) { SetTraceLogLevel(LOG_WARNING); return fl::RunFlightDangerTest(); }
    if (argc >= 2 && strcmp(argv[1], "--flight-costume-test") == 0) { SetTraceLogLevel(LOG_WARNING); return fl::RunFlightCostumeTest(); }
    if (argc >= 2 && strcmp(argv[1], "--flight-long-test") == 0) { SetTraceLogLevel(LOG_WARNING); return fl::RunFlightLongTest(); }
    if (argc >= 2 && strcmp(argv[1], "--flight-longflight-test") == 0) { SetTraceLogLevel(LOG_WARNING); return fl::RunFlightLongFlightTest(); }
    if (argc >= 2 && strcmp(argv[1], "--flight-long") == 0) { SetTraceLogLevel(LOG_WARNING); return fl::RunFlightLongSim(argc, argv); }
    if (argc >= 2 && strcmp(argv[1], "--flight-siege") == 0) { SetTraceLogLevel(LOG_WARNING); return fl::RunFlightSiege(argc, argv); }
    // Mouthful (arcade game 8): the rules, a bot-only round, a duel of two forms
    if (argc >= 2 && strcmp(argv[1], "--mouthful-test") == 0) { SetTraceLogLevel(LOG_WARNING); return mf::RunMouthfulTest(); }
    if (argc >= 2 && strcmp(argv[1], "--mouthful-net-test") == 0) { SetTraceLogLevel(LOG_WARNING); return mf::RunMouthfulNetTest(); }
    if (argc >= 2 && strcmp(argv[1], "--mouthful-skins-test") == 0) { SetTraceLogLevel(LOG_WARNING); return mf::RunMouthfulSkinsTest(); }
    // A Night Off (arcade game 6)
    if (argc >= 2 && strcmp(argv[1], "--night-test") == 0) { SetTraceLogLevel(LOG_WARNING); return no::RunNightTest(); }
    if (argc >= 2 && strcmp(argv[1], "--scuffle-test") == 0) { SetTraceLogLevel(LOG_WARNING); return sf::RunScuffleTest(); }
    // NOCLIP (arcade game 10)
    if (argc >= 2 && strcmp(argv[1], "--noclip-test") == 0) { SetTraceLogLevel(LOG_WARNING); return nc::RunNoclipTest(); }
    if (argc >= 2 && strcmp(argv[1], "--noclip-gen") == 0) { SetTraceLogLevel(LOG_WARNING); return nc::RunNoclipGen(argc >= 3 ? atoi(argv[2]) : 0, argc >= 4 ? (uint32_t)atoi(argv[3]) : 1); }
    if (argc >= 2 && strcmp(argv[1], "--noclip-net-test") == 0) { SetTraceLogLevel(LOG_WARNING); return nc::RunNoclipNetTest(); }
    if (argc >= 2 && strcmp(argv[1], "--noclip-sim") == 0) { SetTraceLogLevel(LOG_WARNING); return nc::RunNoclipSim(argc >= 3 ? std::max(1, atoi(argv[2])) : 4, argc >= 4 ? std::max(1, atoi(argv[3])) : 3, argc >= 5 ? std::max(1, atoi(argv[4])) : 4); }
    // Fowl Play (arcade game 11)
    if (argc >= 2 && strcmp(argv[1], "--fowl-test") == 0) { SetTraceLogLevel(LOG_WARNING); return fp::RunFowlTest(); }
    if (argc >= 2 && strcmp(argv[1], "--fowl-sim") == 0) { SetTraceLogLevel(LOG_WARNING); return fp::RunFowlSim(argc >= 3 ? std::max(1, atoi(argv[2])) : 4, argc >= 4 ? std::clamp(atoi(argv[3]), 1, 6) : 6, argc >= 5 ? atoi(argv[4]) : -1); }
    if (argc >= 2 && strcmp(argv[1], "--fowl-net-test") == 0) { SetTraceLogLevel(LOG_WARNING); return fp::RunFowlNetTest(); }
    if (argc >= 2 && strcmp(argv[1], "--fowl-gamble-sim") == 0) { SetTraceLogLevel(LOG_WARNING); return fp::RunFowlGambleSim(argc >= 3 ? std::max(1, atoi(argv[2])) : 10000); }
    // Ball Pit Brawl (arcade game 13)
    // Fathoms (arcade game, Strategy)
    if (argc >= 2 && strcmp(argv[1], "--fathoms-test") == 0) { SetTraceLogLevel(LOG_WARNING); return fa::RunFathomsTest(argc >= 3 ? atoi(argv[2]) : 0); }
    if (argc >= 2 && strcmp(argv[1], "--fathoms-net-test") == 0) { SetTraceLogLevel(LOG_WARNING); return fa::RunFathomsNetTest(); }
    if (argc >= 2 && strcmp(argv[1], "--fathoms-balance") == 0) { SetTraceLogLevel(LOG_WARNING); return fa::RunFathomsBalance(); }
    if (argc >= 2 && strcmp(argv[1], "--fathoms-sim") == 0) { SetTraceLogLevel(LOG_WARNING); return fa::RunFathomsSim(argc >= 3 ? std::max(1, atoi(argv[2])) : 4, argc >= 4 ? std::clamp(atoi(argv[3]), 2, 6) : 2, argc >= 5 ? std::clamp(atoi(argv[4]), 5, 90) : 30, argc >= 6 ? std::clamp(atoi(argv[5]), 0, 3) : 1); }
    if (argc >= 2 && strcmp(argv[1], "--ballpit-test") == 0) { SetTraceLogLevel(LOG_WARNING); return bp::RunBallPitTest(); }
    if (argc >= 2 && strcmp(argv[1], "--ballpit-net-test") == 0) { SetTraceLogLevel(LOG_WARNING); return bp::RunBallPitNetTest(); }
    if (argc >= 2 && strcmp(argv[1], "--ballpit-sim") == 0) { SetTraceLogLevel(LOG_WARNING); return bp::RunBallPitSim(argc >= 3 ? std::max(1, atoi(argv[2])) : 4, argc >= 4 ? std::clamp(atoi(argv[3]), 0, 3) : 1, argc >= 5 ? std::clamp(atoi(argv[4]), 2, 12) : 8); }
    // Warp Dodgeball (arcade game 12)
    if (argc >= 2 && strcmp(argv[1], "--warp-test") == 0) { SetTraceLogLevel(LOG_WARNING); return wd::RunWarpTest(); }
    if (argc >= 2 && strcmp(argv[1], "--warp-net-test") == 0) { SetTraceLogLevel(LOG_WARNING); return wd::RunWarpNetTest(); }
    if (argc >= 2 && strcmp(argv[1], "--warp-sim") == 0) { SetTraceLogLevel(LOG_WARNING); return wd::RunWarpSim(argc >= 3 ? std::max(1, atoi(argv[2])) : 10, argc >= 4 ? std::max(1, atoi(argv[3])) : 4); }
    if (argc >= 2 && strcmp(argv[1], "--scuffle-arsenal") == 0) { SetTraceLogLevel(LOG_WARNING); return sf::RunScuffleArsenal(argc >= 3 ? std::max(1, atoi(argv[2])) : 1); }
    if (argc >= 2 && strcmp(argv[1], "--scuffle-net-test") == 0) { SetTraceLogLevel(LOG_WARNING); return sf::RunScuffleNetTest(); }
    if (argc >= 2 && strcmp(argv[1], "--scuffle-build-packs") == 0) { SetTraceLogLevel(LOG_WARNING); return sf::RunScuffleBuildPacks(); }
    if (argc >= 2 && strcmp(argv[1], "--scuffle-verify-all") == 0) { SetTraceLogLevel(LOG_WARNING); return sf::RunScuffleVerify("all"); }
    if (argc >= 3 && strcmp(argv[1], "--scuffle-verify") == 0) { SetTraceLogLevel(LOG_WARNING); return sf::RunScuffleVerify(argv[2]); }
    if (argc >= 2 && strcmp(argv[1], "--scuffle-determinism") == 0) { SetTraceLogLevel(LOG_WARNING); return sf::RunScuffleDeterminism(argc >= 3 ? (uint32_t)atoi(argv[2]) : 1); }
    if (argc >= 2 && strcmp(argv[1], "--scuffle-sim") == 0) { SetTraceLogLevel(LOG_WARNING); int ar = 0; if (argc >= 5) { std::string a = argv[4]; ar = a == "melee" ? 1 : a == "chaos" ? 2 : a == "snakes" ? 3 : a == "random" ? 4 : 0; } return sf::RunScuffleSim(argc >= 3 ? atoi(argv[2]) : 4, argc >= 4 ? atoi(argv[3]) : 50, ar); }
    if (argc >= 2 && strcmp(argv[1], "--patron-check") == 0) { SetTraceLogLevel(LOG_WARNING); return no::RunPatronCheck(); }
    if (argc >= 2 && strcmp(argv[1], "--night-net-test") == 0) { SetTraceLogLevel(LOG_WARNING); return no::RunNightNetTest(); }
    if (argc >= 2 && strcmp(argv[1], "--night-sim") == 0) {   // --night-sim <crowd 0-2> <players> [runs] [careful|reckless|mixed] [mode 0-6]
        SetTraceLogLevel(LOG_WARNING);
        int style = argc >= 6 ? (strcmp(argv[5], "reckless") == 0 ? 1 : strcmp(argv[5], "mixed") == 0 ? 2 : 0) : 2;
        return no::RunNightSim(argc >= 3 ? atoi(argv[2]) : 1, argc >= 4 ? atoi(argv[3]) : 1, argc >= 5 ? atoi(argv[4]) : 10, style, argc >= 7 ? atoi(argv[6]) : 0);
    }
    if (argc >= 2 && strcmp(argv[1], "--game-check") == 0) { SetTraceLogLevel(LOG_WARNING); return no::RunGameCheck(argc >= 3 ? argv[2] : "all", argc >= 4 ? atoi(argv[3]) : 200); }
    if (argc >= 2 && strcmp(argv[1], "--mouthful-round") == 0) { SetTraceLogLevel(LOG_WARNING); return mf::RunMouthfulRound(argc > 2 ? atoi(argv[2]) : 11, argc > 3 ? (float)atof(argv[3]) : 15.0f, argc > 4 ? (uint32_t)atoi(argv[4]) : 1u, argc > 5 ? atoi(argv[5]) : 1, argc > 6 ? atoi(argv[6]) : 0); }
    if (argc >= 5 && strcmp(argv[1], "--mouthful-duel") == 0) { SetTraceLogLevel(LOG_WARNING); return mf::RunMouthfulDuel(argv[2], argv[3], (float)atof(argv[4]), argc > 5 ? atoi(argv[5]) : 40); }
    if (argc >= 2 && strcmp(argv[1], "--flight-society-test") == 0) { SetTraceLogLevel(LOG_WARNING); return fl::RunFlightSocietyTest(); }
    if (argc >= 2 && strcmp(argv[1], "--flight-net-test") == 0) { SetTraceLogLevel(LOG_WARNING); return fl::RunFlightNetTest(); }
    if (argc >= 2 && strcmp(argv[1], "--flight-war") == 0) { SetTraceLogLevel(LOG_WARNING); return fl::RunFlightWar(argc, argv); }
    if (argc >= 2 && strcmp(argv[1], "--flight-scout-test") == 0) { SetTraceLogLevel(LOG_WARNING); return fl::RunFlightScoutTest(); }
    if (argc >= 2 && strcmp(argv[1], "--bet-test") == 0) { SetTraceLogLevel(LOG_WARNING); return RunBetTest(); }
    if (argc >= 2 && strcmp(argv[1], "--voice-test") == 0) { SetTraceLogLevel(LOG_WARNING); return voice::RunVoiceTest(); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-gear-test") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlGearTest(); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-skiff-test") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlSkiffTest(); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-quest-test") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlQuestTest(); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-below-test") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlBelowTest(); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-weeds-test") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlWeedsTest(); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-grotto-test") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlGrottoTest(); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-atlantis-test") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlAtlantisTest(); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-divescene-test") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlDiveSceneTest(); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-dive-test") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlDiveTest(); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-wreck") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlWreck(argc, argv); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-bot-test") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlBotTest(); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-net-test") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlNetTest(); }
    if (argc >= 2 && strcmp(argv[1], "--redtide-net-test") == 0) { SetTraceLogLevel(LOG_WARNING); return rt::RunRedTideNetTest(); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-sail-diag") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlSailDiag(); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-session-test") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlSessionTest(); }
    if (argc >= 2 && strcmp(argv[1], "--skins-test") == 0) { SetTraceLogLevel(LOG_WARNING); return skins::RunSkinsTest(); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-shakedown-test") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlShakedownTest(); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-eco-test") == 0) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlEcoTest(); }
    if (argc >= 2 && strcmp(argv[1], "--trawl-fight") == 0) return tw::RunTrawlFight(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "--trawl-boat-test") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return tw::RunTrawlBoatTest();
    }
    if (argc >= 2 && strcmp(argv[1], "--redtide-test") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return RunRedTideTest();
    }
    if (argc >= 3 && strcmp(argv[1], "--redtide-map-test") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return rt::RunRedTideMapTest(argv[2]);
    }
    if (argc >= 2 && strcmp(argv[1], "--redtide-profile-test") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return rt::RunRedTideProfileTest();
    }
    if (argc >= 2 && strcmp(argv[1], "--redtide-mode-test") == 0) { SetTraceLogLevel(LOG_WARNING); return rt::RunRedTideModeTest(); }
    if (argc >= 2 && strcmp(argv[1], "--redtide-match-test") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return rt::RunRedTideMatchTest();
    }
    // --redtide-sim <map> <tides> [careful|careless] [runs] [players]
    if (argc >= 3 && strcmp(argv[1], "--redtide-sim") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return rt::RunRedTideSim(argv[2], argc >= 4 ? atoi(argv[3]) : 10, argc >= 5 ? argv[4] : "careful", argc >= 6 ? atoi(argv[5]) : 3, argc >= 7 ? atoi(argv[6]) : 4);
    }
    if (argc >= 3 && strcmp(argv[1], "--eco-test") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return rt::RunEcoTest(argv[2]);
    }
    if (argc >= 2 && strcmp(argv[1], "--verify-beasts") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return VerifyBeasts() ? 0 : 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--verify-pipe-ecosystem") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return VerifyBeastBiome(PL_PIPES) ? 0 : 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--verify-pirate-ecosystem") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return VerifyBeastBiome(PL_PIRATE) ? 0 : 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--verify-island-ecosystem") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return VerifyBeastBiome(PL_ISLAND) ? 0 : 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--verify-cave-ecosystem") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return VerifyBeastBiome(PL_CAVE) ? 0 : 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--verify-weeds-ecosystem") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return VerifyBeastBiome(PL_WEEDS) ? 0 : 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--verify-atlantis-ecosystem") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return VerifyBeastBiome(PL_ATLANTIS) ? 0 : 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--verify-abyss") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return VerifyAbyss() ? 0 : 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--stage7-test") == 0) { SetTraceLogLevel(LOG_WARNING); return Stage7Test(); }
    // the Deep Arcade: --scuttle-sim [matches] (the rules, bots only); --net-loop [lag ms] [mem] (host + two guests in one process)
    // the Study: --study-audio-test [out.wav] [seconds] renders every soundscape layer and style offline
    if (argc >= 2 && strcmp(argv[1], "--study-audio-test") == 0) {
        const char* wav = nullptr; float secs = 60;
        for (int i = 2; i < argc; i++) { if (strstr(argv[i], ".wav")) wav = argv[i]; else secs = std::max(5.0f, (float)atof(argv[i])); }
        return RunStudyAudioTest(wav, secs);
    }
    if (argc >= 2 && strcmp(argv[1], "--study-save-test") == 0) return study::RunSaveTest();
    // course packs (stage 17): --course-verify [course] [unit], --course-report [course], --course-expand [course] [unit], --course-seed-test
    if (argc >= 2 && strcmp(argv[1], "--expr-test") == 0) return expr::RunExprTest();
    if (argc >= 2 && strcmp(argv[1], "--course-verify") == 0) return RunCourseVerify(argc, argv, 2);
    if (argc >= 2 && strcmp(argv[1], "--course-report") == 0) return RunCourseReport(argc, argv, 2);
    if (argc >= 2 && strcmp(argv[1], "--course-expand") == 0) return RunCourseExpand(argc, argv, 2);
    if (argc >= 2 && strcmp(argv[1], "--course-seed-test") == 0) return RunCourseSeedTest(argc, argv, 2);
    if (argc >= 2 && strcmp(argv[1], "--scuttle-sim") == 0) return RunScuttleSim(argc >= 3 ? std::max(1, atoi(argv[2])) : 2000);
    if (argc >= 2 && strcmp(argv[1], "--net-loop") == 0) {
        int lag = 0; bool mem = false, trawl = false, redtide = false, flight = false, mouthful = false, night = false;
        for (int i = 2; i < argc; i++) { if (strcmp(argv[i], "mem") == 0) mem = true; else if (strcmp(argv[i], "trawl") == 0) trawl = true; else if (strcmp(argv[i], "redtide") == 0) redtide = true; else if (strcmp(argv[i], "flight") == 0) flight = true; else if (strcmp(argv[i], "mouthful") == 0) mouthful = true; else if (strcmp(argv[i], "night") == 0) night = true; else if (strcmp(argv[i], "scuttle") != 0 && strcmp(argv[i], "scuffle") != 0 && strcmp(argv[i], "series") != 0 && strcmp(argv[i], "warp") != 0 && strcmp(argv[i], "fowl") != 0 && strcmp(argv[i], "noclip") != 0 && strcmp(argv[i], "ballpit") != 0 && strcmp(argv[i], "fathoms") != 0) lag = atoi(argv[i]); }
        if (mouthful) { SetTraceLogLevel(LOG_WARNING); return mf::RunMouthfulNetLoop(mem); }
        for (int i = 2; i < argc; i++) if (strcmp(argv[i], "noclip") == 0) { SetTraceLogLevel(LOG_WARNING); int lg = 0; for (int k = 2; k < argc; k++) if (atoi(argv[k]) > 0) lg = atoi(argv[k]); return nc::RunNoclipNetLoop(lg, mem); }
        for (int i = 2; i < argc; i++) if (strcmp(argv[i], "fowl") == 0) { SetTraceLogLevel(LOG_WARNING); int lg = 0; for (int k = 2; k < argc; k++) if (atoi(argv[k]) > 0) lg = atoi(argv[k]); return fp::RunFowlNetLoop(lg, mem); }
        for (int i = 2; i < argc; i++) if (strcmp(argv[i], "fathoms") == 0) { SetTraceLogLevel(LOG_WARNING); int lg = 0; for (int k = 2; k < argc; k++) if (atoi(argv[k]) > 0) lg = atoi(argv[k]); return fa::RunFathomsNetLoop(lg, mem); }
        for (int i = 2; i < argc; i++) if (strcmp(argv[i], "ballpit") == 0) { SetTraceLogLevel(LOG_WARNING); int lg = 0; for (int k = 2; k < argc; k++) if (atoi(argv[k]) > 0) lg = atoi(argv[k]); return bp::RunBallPitNetLoop(lg, mem); }
        for (int i = 2; i < argc; i++) if (strcmp(argv[i], "warp") == 0) { SetTraceLogLevel(LOG_WARNING); int lg = 0; for (int k = 2; k < argc; k++) if (atoi(argv[k]) > 0) lg = atoi(argv[k]); return wd::RunWarpNetLoop(lg, mem); }
        for (int i = 2; i < argc; i++) if (strcmp(argv[i], "scuffle") == 0) { SetTraceLogLevel(LOG_WARNING); return sf::RunScuffleNetLoop(lag, mem); }
        if (night) { SetTraceLogLevel(LOG_WARNING); bool series = false; for (int i = 2; i < argc; i++) series |= strcmp(argv[i], "series") == 0; return series ? no::RunNightSeries(mem, lag) : no::RunNightNetLoop(mem); }
        if (trawl) { SetTraceLogLevel(LOG_WARNING); return tw::RunTrawlNetLoop(mem); }
        if (redtide) { SetTraceLogLevel(LOG_WARNING); return rt::RunRedTideNetLoop(mem); }
        if (flight) { SetTraceLogLevel(LOG_WARNING); return fl::RunFlightNetLoop(mem); }
        return RunNetLoop(lag, mem);
    }
    if (argc >= 4 && strcmp(argv[1], "--brain-test") == 0) { SetTraceLogLevel(LOG_WARNING); BrainTest(atoi(argv[2]), std::max(1, atoi(argv[3]))); return 0; }
    // --gen-chart <tier 0-4> <seed> [count]: print an expedition chart and check the generation rules (on `count` seeds from `seed`)
    if (argc >= 4 && strcmp(argv[1], "--gen-chart") == 0) {
        int tier = std::clamp(atoi(argv[2]), 0, CAVE_TIERS - 1), count = argc >= 5 ? std::max(1, atoi(argv[4])) : 1, bad = 0;
        unsigned seed = (unsigned)atoi(argv[3]);
        for (int i = 0; i < count; i++) {
            Chart c = GenerateChart(tier, seed + i);
            std::string why;
            bool ok = CheckChart(c, tier, why);
            if (i == 0) printf("%s", ChartAscii(c).c_str());
            if (!ok) { bad++; printf("seed %u breaks the rules: %s\n", seed + i, why.c_str()); }
            if (i == 0) printf("tier %d (cave level %d): %d rooms, %d corridors, fewest fights to the boss %d\n", tier, CAVE_TIER_LEVEL[tier], (int)c.rooms.size(), (int)c.edges.size(), ChartFightsTo(c, c.entrance, c.boss));
        }
        printf("%d of %d charts follow the rules\n", count - bad, count);
        return bad ? 1 : 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--gen-siren-art") == 0) {
        return GenerateSirenArt() ? 0 : 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--gen-crew-art") == 0) {
        return GenerateAllCrewArt() ? 0 : 1;
    }
    const char* shotDir = argc >= 3 && strcmp(argv[1], "--shots") == 0 ? argv[2] : nullptr;
    const char* spriteFile = argc >= 3 && strcmp(argv[1], "--sprites") == 0 ? argv[2] : nullptr;
    const char* galleryBase = argc >= 3 && strcmp(argv[1], "--gallery") == 0 ? argv[2] : nullptr;
    const bool flatsUiTest = argc >= 2 && strcmp(argv[1], "--flats-ui-test") == 0;
    // --figures <dir> [filter]: a sheet per hero and enemy (idle, walk, windup, strike, hit, death) as <dir>/fig_<name>.png
    // --silhouette [dir] [filter]: the same sheets with every figure solid black, as <dir>/sil_<name>.png
    const bool figSheets = argc >= 3 && strcmp(argv[1], "--figures") == 0, silSheets = argc >= 2 && strcmp(argv[1], "--silhouette") == 0;

    // (DEPTH_UNCAPPED=1: no vsync and no frame cap, so DEPTH_SHOTFRAMES measures the real cost of a frame)
    const bool uncapped = getenv("DEPTH_UNCAPPED") != nullptr;
    SetConfigFlags((uncapped ? 0 : FLAG_VSYNC_HINT) | FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_W, SCREEN_H, "Depth");
    SetWindowMinSize(640, 360);
    SetExitKey(KEY_NULL); // Esc is used in-game, so it shouldn't close the window
    SetTargetFPS(uncapped ? 0 : 60);
    InitArt();
    RelicSpriteGenerator::Init(); // draw every relic's SVG icon

    Game g;
    InitGame(g);

    // --study-motion-audit <lounge|study> <seconds>: frame-to-frame brightness by screen region; fails on a flash or fast motion
    if (argc >= 3 && strcmp(argv[1], "--study-motion-audit") == 0) {
        int sc = strstr(argv[2], "lounge") ? study::SC_LOUNGE : study::SC_STUDY;
        int rc = study::RunMotionAudit(sc, argc >= 4 ? std::max(2.0f, (float)atof(argv[3])) : 30.0f);
        RelicSpriteGenerator::Unload(); UnloadArt(); CloseWindow();
        return rc;
    }
    if (flatsUiTest) { // the Flats battle screen driven by the auto-player, through the real drawing and animation code
        g.scene = Scene::Cards;
        DebugFlatsAutoplay(argc >= 3 ? atoi(argv[2]) : 10);
        for (int f = 0; f < 200000 && FlatsAutoplayActive(); f++) { g.time += 1 / 60.0f; BeginFrame(); RunScene(g); EndFrame(g.time); }
    } else if (figSheets || silSheets) {
        std::string dir = argc >= 3 ? argv[2] : "shots";
        const char* filter = argc >= 4 ? argv[3] : nullptr;
        gSilhouette = silSheets;
        for (int kind = 0; kind < 2; kind++)
            for (int i = 0; i < (kind == 0 ? (int)HeroClass::COUNT : (int)EnemyType::COUNT); i++) {
                std::string name = kind == 0 ? ClassName((HeroClass)i) : MakeEnemy((EnemyType)i, 1).name;
                for (char& ch : name) ch = isalnum((unsigned char)ch) ? (char)tolower((unsigned char)ch) : '_';
                if (filter && name.find(filter) == std::string::npos) continue;
                for (int f = 0; f < 70; f++) { g.time += 1 / 60.0f; BeginFrame(); SetPost(0.3f, 0.02f, 0.2f); DrawFigureSheet(kind == 0, i, g.time); EndFrame(g.time); }
                std::string path = dir + (silSheets ? "/sil_" : "/fig_") + (kind == 0 ? "hero_" : "") + name + ".png";   // heroes get their own prefix: the Weeds' Siren would overwrite the crew's
                TraceLog(LOG_INFO, "sheet %s: %s", path.c_str(), SaveFrameShot(path.c_str()) ? "ok" : "FAILED");
            }
        gSilhouette = false;
    } else if (spriteFile) {
        MakeSpriteSheet(spriteFile);
    } else if (galleryBase) {
        MakeCrewGallery(galleryBase);
    } else if (shotDir) {
        gShotFilter = argc >= 4 ? argv[3] : nullptr;
        TakeShots(g, shotDir);
    } else {
        InitAudioDevice(); // only for real play: the tools above run silently
        AudioInit();       // the parkour section's synthesizer (sound.cpp)
        if (LoadGame(g)) Toast(g, "Welcome back aboard. Your progress was loaded.");
        LoadSettings();                                    // settings.txt: volumes, brightness, fullscreen, key bindings (kept across new games)
        if (GameSettings().fullscreen) ToggleBorderlessWindowed();
        if (argc >= 3 && strcmp(argv[1], "--play") == 0) { int lv = atoi(argv[2]); if (lv >= PL_COUNT) { StartAbyss(g); g.scene = Scene::Abyss; } else StartPlatform(g, std::clamp(lv, 0, PL_COUNT - 1), true); } // developer: straight into a dive (nothing is unlocked or saved by it)
        Scene last = g.scene;
        while (!WindowShouldClose() && !GameMenuWantsQuit()) {
            if (IsKeyPressed(KEY_F11)) { ToggleBorderlessWindowed(); GameSettings().fullscreen = !GameSettings().fullscreen; SaveSettings(); } // F11: fill the screen (the frame is letterboxed to fit)
            // the game menu (Esc): the game is paused while it is open (the Periscope's dossier keeps Esc for closing itself)
            if (!GameMenuActive() && GameMenuTakeRequest()) GameMenuOpen();
            if (!GameMenuActive() && ActPressed(A_MENU) && !(Typing() && !IsKeyPressed(KEY_ESCAPE)) && !(g.scene == Scene::Periscope && g.dossier >= 0) && !(g.scene == Scene::NightOff && NightOffOwnsEsc())) GameMenuOpen();
            if (GameMenuActive()) {
                BeginFrame();
                GameMenuFrame(g);
                if (g.scene == Scene::Trawl) TrawlMenuTick(GetFrameTime());   // (a crew at sea doesn't stop for one hand's menu)
                if (g.scene == Scene::RedTide) RedTideMenuTick(GetFrameTime());   // (nor does the tide)
                if (g.scene == Scene::Flight) FlightMenuTick(GetFrameTime());     // (nor the other colonies)
                if (g.scene == Scene::Mouthful) MouthfulMenuTick(GetFrameTime()); // (nor the reef)
                if (g.scene == Scene::Scuffle) ScuffleMenuTick(GetFrameTime());   // (nor the fight: a bot stands in)
                MouseLookFrameEnd();                                          // (the menu needs the pointer)
                ArcadeVoiceFrame(GetFrameTime());                             // (voices carry on while the menu's open)
                DrawVoiceHud();
                AudioFrame(GetFrameTime(), g.scene == Scene::Platformer || g.scene == Scene::Abyss);
                EndFrame(g.time);
                continue;
            }
            g.time += GetFrameTime();
            BeginFrame();
            RunScene(g);
            MouseLookFrameEnd();   // a scene that stopped asking for mouse look gets its pointer back
            {   // aboard the Nautilus (the salon and its station screens) the waltz and the ship's bed play
                bool aboard = g.scene != Scene::Platformer && g.scene != Scene::Abyss && g.scene != Scene::Dungeon && g.scene != Scene::Study
                              && !(g.scene == Scene::RedTide && RedTideAudioActive()) && g.scene != Scene::Trawl && g.scene != Scene::Flight && g.scene != Scene::Mouthful && g.scene != Scene::NightOff && g.scene != Scene::Scuffle && g.scene != Scene::Fowl && g.scene != Scene::Noclip && g.scene != Scene::BallPit && g.scene != Scene::Fathoms;
                AudioHub(aboard, g.scene == Scene::Hub ? -1 : (int)g.scene, g.mourning);
                AudioStudy(g.scene == Scene::Study);   // below the hatch: the Study's own soundscape instead
                if (g.scene != Scene::Dungeon) AudioExpedition(ExpAudio{});   // (the Dungeon scene sets it every frame)
                if (g.scene != Scene::RedTide) AudioRedTide(RtAudio{});      // (and the Red Tide scene)
                if (g.scene != Scene::Trawl) AudioTrawl(TwAudio{});          // (and the Trawl)
                if (g.scene != Scene::Flight) AudioFlight(FlAudio{});        // (and the Flight)
                if (g.scene != Scene::Mouthful) AudioMouthful(MfAudio{}); if (g.scene != Scene::Scuffle) AudioScuffle(SfAudio{}); if (g.scene != Scene::Fowl) AudioFowl(FpAudio{}); if (g.scene != Scene::Noclip) AudioNoclip(NcAudio{}); if (g.scene != Scene::BallPit) AudioBallPit(BpAudio{}); if (g.scene != Scene::Fathoms) AudioFathoms(FaAudio{});    // (and Mouthful)
            if (g.scene != Scene::NightOff) AudioNightOff(NoAudio{});    // (and A Night Off)
            }
            ArcadeVoiceFrame(GetFrameTime());   // the arcade's voice chat: the mic out, the table's voices in
            AudioFrame(GetFrameTime(), g.scene == Scene::Platformer || g.scene == Scene::Abyss);
            DrawToast(g);
            DrawVoiceHud();
            EndFrame(g.time);
            if (g.scene != last && g.scene == Scene::Hub) SaveGame(g); // autosave whenever you're back aboard
            last = g.scene;
        }
        if (g.scene != Scene::Dungeon) SaveGame(g); // quitting mid-expedition keeps the last save from aboard
    }
    RelicSpriteGenerator::Unload();
    UnloadArt();
    AudioClose();
    if (IsAudioDeviceReady()) CloseAudioDevice();
    CloseWindow();
    return 0;
}
