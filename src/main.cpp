// ============================================================================
//  DEPTH - entry point. Opens the window and runs whichever scene is active.
//
//  Developer switches:
//    depth.exe --sim 400 [level] [random|sensible] [cave tier 0-4]   auto-play expeditions, print balance
//    depth.exe --shots <folder>    render every screen to PNGs and quit
//    depth.exe --verify            prove every platformer section can be crossed
// ============================================================================
#include "game.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>

static void RunScene(Game& g) {
    switch (g.scene) {
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
    }
}

static void TakeShots(const Game& base, const std::string& dir) {
    struct Shot { const char* name; std::function<void(Game&)> setup; };
    const Shot shots[] = {
        {"hub", [](Game& g) { g.scene = Scene::Hub; }},
        {"hub_leave", [](Game& g) { g.scene = Scene::Hub; g.roster[0].onLeave = 1; g.roster[1].rattled = true; }},
        {"crew", [](Game& g) { g.scene = Scene::Crew; g.roster[1].level = 3; g.selectedHero = g.roster[1].id; }},
        {"helm", [](Game& g) { g.scene = Scene::Helm; g.caveTierCleared = 1; g.caveTier = 2; }},
        {"radar", [](Game& g) { g.scene = Scene::Radar; }},
        {"workshop", [](Game& g) { g.scene = Scene::Workshop; g.gold = 500; g.upgrades[UP_BUNKS] = 1; }},
        {"library", [](Game& g) { g.scene = Scene::Bookshelf; g.bookTab = 1; }},
        {"combat", [](Game& g) { g.dungeon.light = 60; DebugEnterCombat(g); }},
        {"combat_dark", [](Game& g) { DebugEnterCombat(g); g.dungeon.light = 10; }},
        {"combat_walk", [](Game& g) { DebugEnterCombat(g); g.dungeon.phase = DPhase::Walking; g.dungeon.walkT = 0.4f; }},
        {"combat_deep", [](Game& g) { g.caveTierCleared = 4; g.caveTier = 3; DebugEnterCombat(g); }},
        {"periscope", [](Game& g) { g.scene = Scene::Periscope; g.platCleared[0] = true; }},
        {"pipes", [](Game& g) { StartPlatform(g, PL_PIPES); }},
        {"pipes_riser", [](Game& g) { g.platLayouts[PL_PIPES] = {0, 1, 2, 3, 4, 5}; StartPlatform(g, PL_PIPES); g.plat.pos = g.plat.spawns[1]; g.plat.pos.x += 200; g.plat.pos.y -= 160; }},
        {"pipes_shaft", [](Game& g) { g.platLayouts[PL_PIPES] = {3, 6, 1, 0, 2, 4}; StartPlatform(g, PL_PIPES); g.plat.pos = g.plat.spawns[1]; g.plat.pos.x += 330; g.plat.pos.y += 60; }},
        {"pipes_twins", [](Game& g) { g.platLayouts[PL_PIPES] = {6, 5, 1, 0, 2, 4}; StartPlatform(g, PL_PIPES); g.plat.pos = g.plat.spawns[1]; g.plat.pos.x += 60; }},
        {"hull", [](Game& g) { g.platLayouts[PL_HULL] = {1, 0, 2, 3, 4}; StartPlatform(g, PL_HULL); g.plat.pos = {24 * 32 + 100, 300}; }},
        {"hull_kraken", [](Game& g) { StartPlatform(g, PL_HULL); g.plat.pos = {(g.plat.w - 24) * 32 + 420.0f, 200}; g.plat.boss.state = 2; }},
        {"pirate", [](Game& g) { g.platLayouts[PL_PIRATE] = {0, 1, 2, 3, 4}; StartPlatform(g, PL_PIRATE); g.plat.pos = {24 * 32 + 100, 250}; }},
        {"pirate_boss", [](Game& g) { StartPlatform(g, PL_PIRATE); g.plat.pos = {(g.plat.w - 24) * 32 + 200.0f, 300}; }},
    };
    for (const auto& s : shots) {
        Game g = base;
        s.setup(g);
        for (int f = 0; f < 90; f++) {
            g.time += 1 / 60.0f;
            BeginFrame();
            RunScene(g);
            EndFrame(g.time);
        }
        std::string path = dir + "/" + s.name + ".png";
        TraceLog(LOG_INFO, "shot %s: %s", path.c_str(), SaveFrameShot(path.c_str()) ? "ok" : "FAILED");
    }
}

int main(int argc, char** argv) {
    SetRandomSeed((unsigned int)time(nullptr));
    if (argc >= 2 && strcmp(argv[1], "--sim") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        SimulateExpeditions(argc >= 3 ? atoi(argv[2]) : 400, argc >= 4 ? atoi(argv[3]) : 0,
                            argc >= 5 && strcmp(argv[4], "random") == 0, argc >= 6 ? std::clamp(atoi(argv[5]), 0, CAVE_TIERS - 1) : 0);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--verify") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return VerifyPlatformLevels();
    }
    const char* shotDir = argc >= 3 && strcmp(argv[1], "--shots") == 0 ? argv[2] : nullptr;

    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "Depth");
    SetExitKey(KEY_NULL); // Esc is used in-game, so it shouldn't close the window
    SetTargetFPS(60);
    InitArt();

    Game g;
    InitGame(g);

    if (shotDir) {
        TakeShots(g, shotDir);
    } else {
        if (LoadGame(g)) Toast(g, "Welcome back aboard. Your progress was loaded.");
        Scene last = g.scene;
        while (!WindowShouldClose()) {
            g.time += GetFrameTime();
            BeginFrame();
            RunScene(g);
            DrawToast(g);
            EndFrame(g.time);
            if (g.scene != last && g.scene == Scene::Hub) SaveGame(g); // autosave whenever you're back aboard
            last = g.scene;
        }
        if (g.scene != Scene::Dungeon) SaveGame(g); // quitting mid-expedition keeps the last save from aboard
    }
    UnloadArt();
    CloseWindow();
    return 0;
}
