// ============================================================================
//  DEPTH - entry point. Opens the window and runs whichever scene is active.
// ============================================================================
#include "game.h"
#include <ctime>

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "Depth");
    SetExitKey(KEY_NULL); // Esc is used in-game, so it shouldn't close the window
    SetTargetFPS(60);
    SetRandomSeed((unsigned int)time(nullptr));

    Game g;
    InitGame(g);

    while (!WindowShouldClose()) {
        g.time += GetFrameTime();
        BeginDrawing();
        ClearBackground(Pal::SeaDeep);
        switch (g.scene) {
            case Scene::Hub:        SceneHub(g); break;
            case Scene::Helm:       SceneHelm(g); break;
            case Scene::Crew:       SceneCrew(g); break;
            case Scene::Radar:      SceneRadar(g); break;
            case Scene::Ward:       SceneWard(g); break;
            case Scene::SickLeave:  SceneSickLeave(g); break;
            case Scene::Bookshelf:  SceneBookshelf(g); break;
            case Scene::Periscope:  ScenePeriscope(g); break;
            case Scene::Dungeon:    SceneDungeon(g); break;
            case Scene::Platformer: ScenePlatformer(g); break;
        }
        DrawToast(g);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
