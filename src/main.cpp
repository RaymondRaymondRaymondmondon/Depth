// ============================================================================
//  DEPTH - entry point. Opens the window and runs whichever scene is active.
//
//  Developer switches:
//    depth.exe --sim 400 [level] [random]   auto-play expeditions and print balance stats
//    depth.exe --shots <folder>    render every screen to PNGs and quit
//    depth.exe --verify            prove every platformer section can be crossed
// ============================================================================
#include "game.h"
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
    auto hubAt = [](float cam) { return [cam](Game& g) { g.scene = Scene::Hub; g.hubCam = g.hubCamTarget = cam; }; };
    const Shot shots[] = {
        {"hub_bow", hubAt(-160)},
        {"hub_library", hubAt(640)},
        {"hub_radar", hubAt(1200)},
        {"hub_helm", hubAt(1690)},
        {"hub_periscope", hubAt(2500)},
        {"hub_sickbay", hubAt(3500)},
        {"hub_stern", hubAt(3880)},
        {"crew", [](Game& g) { g.scene = Scene::Crew; g.roster[1].level = 3; g.selectedHero = g.roster[1].id; }},
        {"helm", [](Game& g) { g.scene = Scene::Helm; }},
        {"radar", [](Game& g) { g.scene = Scene::Radar; }},
        {"workshop", [](Game& g) { g.scene = Scene::Workshop; g.gold = 500; g.upgrades[UP_BUNKS] = 1; }},
        {"library", [](Game& g) { g.scene = Scene::Bookshelf; g.bookTab = 1; }},
        {"combat", [](Game& g) { g.dungeon.light = 60; DebugEnterCombat(g); }},
        {"combat_dark", [](Game& g) { DebugEnterCombat(g); g.dungeon.light = 10; }},
        {"periscope", [](Game& g) { g.scene = Scene::Periscope; g.platCleared[0] = true; }},
        {"pipes", [](Game& g) { StartPlatform(g, PL_PIPES); }},
        {"pipes_chimney", [](Game& g) { g.platLayouts[PL_PIPES] = {2, 0, 1, 3, 4, 5}; StartPlatform(g, PL_PIPES); g.plat.pos = {24 * 32 + 200, 200}; }},
        {"hull", [](Game& g) { g.platLayouts[PL_HULL] = {1, 0, 2, 3, 4}; StartPlatform(g, PL_HULL); g.plat.pos = {24 * 32 + 100, 300}; }},
        {"hull_kraken", [](Game& g) { StartPlatform(g, PL_HULL); g.plat.pos = {(g.plat.w - 24) * 32 + 420.0f, 200}; g.plat.boss.state = 2; }},
        {"pirate", [](Game& g) { g.platLayouts[PL_PIRATE] = {0, 1, 2, 3, 4}; StartPlatform(g, PL_PIRATE); g.plat.pos = {24 * 32 + 100, 250}; }},
        {"pirate_boss", [](Game& g) { StartPlatform(g, PL_PIRATE); g.plat.pos = {(g.plat.w - 24) * 32 + 200.0f, 300}; }},
    };
    for (const auto& s : shots) {
        Game g = base;
        s.setup(g);
        float cam = g.hubCam;
        for (int f = 0; f < 90; f++) {
            g.time += 1 / 60.0f;
            g.hubCam = g.hubCamTarget = cam;
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
                            argc >= 5 && strcmp(argv[4], "random") == 0);
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
        while (!WindowShouldClose()) {
            g.time += GetFrameTime();
            BeginFrame();
            RunScene(g);
            DrawToast(g);
            EndFrame(g.time);
        }
    }
    UnloadArt();
    CloseWindow();
    return 0;
}
