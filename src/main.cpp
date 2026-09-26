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
#include "levelgen.h"
#include "relics.h"
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
        case Scene::Cards:      SceneCards(g); break;
    }
}

// Starts a platform level on the first generator seed that contains the given set-piece, standing just before it.
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
static const char* gShotFilter = nullptr; // depth.exe --shots <folder> <text>: only screens whose name contains <text>
static void TakeShots(const Game& base, const std::string& dir) {
    struct Shot { const char* name; std::function<void(Game&)> setup; };
    const Shot shots[] = {
        {"hub", [](Game& g) { g.scene = Scene::Hub; }},
        {"hub_cat", [](Game& g) { g.scene = Scene::Hub; DebugPetCat(); }},
        {"cards_menu", [](Game& g) { g.scene = Scene::Cards; }},
        {"cards_play", [](Game& g) { g.scene = Scene::Cards; DebugFlatsDeal(); }},
        {"cards_combat", [](Game& g) { g.scene = Scene::Cards; DebugFlatsCombat(); }},
        {"cards_won", [](Game& g) { g.scene = Scene::Cards; DebugFlatsWon(); }},
        {"cards_boon", [](Game& g) { g.scene = Scene::Cards; DebugFlatsBoon(); }},
        {"cards_map", [](Game& g) { g.scene = Scene::Cards; DebugFlatsMap(); }},
        {"cards_campfire", [](Game& g) { g.scene = Scene::Cards; DebugFlatsCampfire(); }},
        {"cards_reward", [](Game& g) { g.scene = Scene::Cards; DebugFlatsReward(); }},
        {"cards_shop", [](Game& g) { g.scene = Scene::Cards; DebugFlatsShop(); }},
        {"cards_deck", [](Game& g) { g.scene = Scene::Cards; DebugFlatsDeck(); }},
        {"hub_leave", [](Game& g) { g.scene = Scene::Hub; g.roster[0].onLeave = 1; g.roster[1].rattled = true; }},
        {"crew", [](Game& g) { g.scene = Scene::Crew; g.roster[1].level = 3; g.selectedHero = g.roster[1].id; }},
        {"helm", [](Game& g) { g.scene = Scene::Helm; g.tierCleared[(int)Location::Cave] = 1; g.tierSel[(int)Location::Cave] = 2; }},
        {"radar", [](Game& g) { g.scene = Scene::Radar; }},
        {"workshop", [](Game& g) { g.scene = Scene::Workshop; g.gold = 500; g.upgrades[UP_BUNKS] = 1; }},
        {"library", [](Game& g) { g.scene = Scene::Bookshelf; g.bookTab = 1; }},
        {"combat", [](Game& g) { g.dungeon.light = 60; DebugEnterCombat(g); }},
        {"combat_dark", [](Game& g) { DebugEnterCombat(g); g.dungeon.light = 10; }},
        {"combat_walk", [](Game& g) { DebugEnterCombat(g); g.dungeon.phase = DPhase::Walking; g.dungeon.walkT = 0.4f; }},
        {"combat_deep", [](Game& g) { g.tierCleared[(int)Location::Cave] = 4; g.tierSel[(int)Location::Cave] = 3; DebugEnterCombat(g); }},
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
        {"pipes", [](Game& g) { StartPlatform(g, PL_PIPES); }},
        {"pipes_riser", [](Game& g) { g.platLayouts[PL_PIPES] = {101, 100}; StartPlatform(g, PL_PIPES); g.plat.pos = g.plat.spawns[1]; }},
        {"pipes_shaft", [](Game& g) { g.platLayouts[PL_PIPES] = {202, 100}; StartPlatform(g, PL_PIPES); g.plat.pos = g.plat.spawns[3]; }},
        {"pipes_twins", [](Game& g) { g.platLayouts[PL_PIPES] = {303, 100}; StartPlatform(g, PL_PIPES); g.plat.pos = g.plat.spawns[2]; }},
        {"hull", [](Game& g) { g.platLayouts[PL_HULL] = {404, 100}; StartPlatform(g, PL_HULL); g.plat.pos = g.plat.spawns[1]; }},
        {"hull_shaft", [](Game& g) { g.platLayouts[PL_HULL] = {505, 100}; StartPlatform(g, PL_HULL); g.plat.pos = g.plat.spawns[3]; }},        {"hull_kraken", [](Game& g) { StartPlatform(g, PL_HULL); g.plat.pos = {(g.plat.w - 24) * 32 + 420.0f, 200}; g.plat.boss.state = 2; }},
        {"pirate", [](Game& g) { g.platLayouts[PL_PIRATE] = {606, 100}; StartPlatform(g, PL_PIRATE); g.plat.pos = g.plat.spawns[1]; }},
        {"pirate_hatch", [](Game& g) { g.platLayouts[PL_PIRATE] = {707, 100}; StartPlatform(g, PL_PIRATE); g.plat.pos = g.plat.spawns[3]; }},
        {"pirate_hold", [](Game& g) { g.platHard = true; g.platLayouts[PL_PIRATE] = {808, 100}; StartPlatform(g, PL_PIRATE); g.plat.pos = g.plat.spawns[4]; }},
        {"pirate_stairs", [](Game& g) { g.platLayouts[PL_PIRATE] = {909, 100}; StartPlatform(g, PL_PIRATE); g.plat.pos = g.plat.spawns[6]; }},        {"pipes_vent", [](Game& g) { ShotAtPiece(g, PL_PIPES, SetPiece::SteamBoost); g.plat.time = 0.4f; }},
        {"pipes_crumble", [](Game& g) { ShotAtPiece(g, PL_PIPES, SetPiece::CrumbleRun); }},
        {"hull_barnacle", [](Game& g) { ShotAtPiece(g, PL_HULL, SetPiece::BarnacleShaft); }},
        {"pirate_gap", [](Game& g) { ShotAtPiece(g, PL_PIRATE, SetPiece::ShipGap, false, 2); }},
        {"pirate_ladder", [](Game& g) { ShotAtPiece(g, PL_PIRATE, SetPiece::MastLadder, false, 1); }},
        {"pipes_drop", [](Game& g) { ShotAtPiece(g, PL_PIPES, SetPiece::PipeDrop, false, 1); }},
        {"pirate_ghost", [](Game& g) { ShotAtPiece(g, PL_PIRATE, SetPiece::ShipGap, true, 3); }},        {"pirate_boss", [](Game& g) { StartPlatform(g, PL_PIRATE); g.plat.pos = {(g.plat.w - 24) * 32 + 150.0f, g.plat.boss.home.y + 40}; }},
    };
    std::vector<Shot> all(std::begin(shots), std::end(shots));
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

// Renders every sprite in the game onto eight pages and stitches them into one image.
static void MakeSpriteSheet(const std::string& path) {
    const std::function<void(float)> pages[11] = {
        [](float t) { DrawCrewSpritePage(t); },       [](float t) { DrawSalonSpritePage(t); },
        [](float t) { DrawCaveSpritePage(t); },       [](float t) { DrawPlatformSpritePage(0, t); },
        [](float t) { DrawPlatformSpritePage(1, t); }, [](float t) { DrawPlatformSpritePage(2, t); },
        [](float t) { FlatsSpritePage(t); },          [](float t) { DrawItemSpritePage(t); },
        [](float t) { DrawBestiarySpritePage(0, t); }, [](float t) { DrawBestiarySpritePage(1, t); }, [](float t) { DrawBestiarySpritePage(2, t); },
    };
    Image sheet = GenImageColor(SCREEN_W * 2, SCREEN_H * 6, BLACK);
    for (int i = 0; i < 11; i++) {
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
    if (argc >= 2 && strcmp(argv[1], "--verify") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return VerifyPlatformLevels();
    }
    const char* shotDir = argc >= 3 && strcmp(argv[1], "--shots") == 0 ? argv[2] : nullptr;
    const char* spriteFile = argc >= 3 && strcmp(argv[1], "--sprites") == 0 ? argv[2] : nullptr;

    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "Depth");
    SetExitKey(KEY_NULL); // Esc is used in-game, so it shouldn't close the window
    SetTargetFPS(60);
    InitArt();
    RelicSpriteGenerator::Init(); // draw every relic's SVG icon

    Game g;
    InitGame(g);

    if (spriteFile) {
        MakeSpriteSheet(spriteFile);
    } else if (shotDir) {
        gShotFilter = argc >= 4 ? argv[3] : nullptr;
        TakeShots(g, shotDir);
    } else {
        InitAudioDevice(); // only for real play: the tools above run silently
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
    RelicSpriteGenerator::Unload();
    UnloadArt();
    if (IsAudioDeviceReady()) CloseAudioDevice();
    CloseWindow();
    return 0;
}
