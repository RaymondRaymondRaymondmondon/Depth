#include "sound.h"
#include "beasts.h"
#include "redtide.h"
#include "redtide_match.h"
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
static void TakeShots(const Game& base, const std::string& dir) {
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
        {"study", [](Game& g) { g.scene = Scene::Study; }},
        {"arcade", [](Game& g) { g.scene = Scene::Arcade; }},
        {"arcade_redtide", [](Game& g) { g.scene = Scene::Arcade; DebugArcadeReel(4); }},
        {"redtide_tank", [](Game& g) { DebugRedTideShot(g, 0); }},
        {"redtide_silhouette", [](Game& g) { DebugRedTideShot(g, 1); }},
        {"redtide_species_ship_1", [](Game& g) { DebugRedTideShot(g, 2); }},
        {"redtide_species_ship_2", [](Game& g) { DebugRedTideShot(g, 3); }},
        {"redtide_ship_bridge", [](Game& g) { DebugRedTideShot(g, 10); }},
        {"redtide_ship_salon", [](Game& g) { DebugRedTideShot(g, 11); }},
        {"redtide_ship_engine", [](Game& g) { DebugRedTideShot(g, 12); }},
        {"redtide_ship_keel", [](Game& g) { DebugRedTideShot(g, 13); }},
        {"redtide_ship_hunt", [](Game& g) { DebugRedTideShot(g, 14); }},
        {"redtide_ship_cabins", [](Game& g) { DebugRedTideShot(g, 15); }},
        {"redtide_cave_mouth", [](Game& g) { DebugRedTideShot(g, 20); }},
        {"redtide_cave_gallery", [](Game& g) { DebugRedTideShot(g, 21); }},
        {"redtide_cave_chimney", [](Game& g) { DebugRedTideShot(g, 22); }},
        {"redtide_cave_dry", [](Game& g) { DebugRedTideShot(g, 23); }},
        {"redtide_cave_cathedral", [](Game& g) { DebugRedTideShot(g, 24); }},
        {"redtide_cave_sump", [](Game& g) { DebugRedTideShot(g, 25); }},
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
        {"menu_controls", [](Game& g) { g.scene = Scene::Hub; }},
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
        for (int f = 0; f < 90; f++) {
            g.time += 1 / 60.0f;
            if (f == 60 && strncmp(s.name, "menu_", 5) == 0) { SnapshotFrame(); DebugMenuPage(strstr(s.name, "settings") ? 1 : strstr(s.name, "controls") ? 2 : 0); } // between frames, as in play
            BeginFrame();
            if (GameMenuActive()) GameMenuFrame(g); else RunScene(g);
            EndFrame(g.time);
        }
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
    if (argc >= 2 && strcmp(argv[1], "--redtide-test") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return RunRedTideTest();
    }
    if (argc >= 3 && strcmp(argv[1], "--redtide-map-test") == 0) {
        SetTraceLogLevel(LOG_WARNING);
        return rt::RunRedTideMapTest(argv[2]);
    }
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

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_W, SCREEN_H, "Depth");
    SetWindowMinSize(640, 360);
    SetExitKey(KEY_NULL); // Esc is used in-game, so it shouldn't close the window
    SetTargetFPS(60);
    InitArt();
    RelicSpriteGenerator::Init(); // draw every relic's SVG icon

    Game g;
    InitGame(g);

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
            if (!GameMenuActive() && ActPressed(A_MENU) && !(g.scene == Scene::Periscope && g.dossier >= 0)) GameMenuOpen();
            if (GameMenuActive()) {
                BeginFrame();
                GameMenuFrame(g);
                AudioFrame(GetFrameTime(), g.scene == Scene::Platformer || g.scene == Scene::Abyss);
                EndFrame(g.time);
                continue;
            }
            g.time += GetFrameTime();
            BeginFrame();
            RunScene(g);
            {   // aboard the Nautilus (the salon and its station screens) the waltz and the ship's bed play
                bool aboard = g.scene != Scene::Platformer && g.scene != Scene::Abyss && g.scene != Scene::Dungeon;
                AudioHub(aboard, g.scene == Scene::Hub ? -1 : (int)g.scene, g.mourning);
                if (g.scene != Scene::Dungeon) AudioExpedition(ExpAudio{});   // (the Dungeon scene sets it every frame)
            }
            AudioFrame(GetFrameTime(), g.scene == Scene::Platformer || g.scene == Scene::Abyss);
            DrawToast(g);
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
