// Scuffle stage 3: the Nautilus world's built-in stages (doc pp. 9, 18), made with a small builder and written out in the
// editor's text form (data/scuffle/stages/nautilus.txt) by --scuffle-build-packs; the game loads the text. Twenty layouts
// and a variant of each (mirrored, the hazards retimed or swapped) make the world's forty. The signature stages are the
// Engine Room (pistons on a rhythm), the Torpedo Tubes (tubes fire sticks across the stage) and the Salon Window (the
// glass breaks at 20 s).
#include "scuffle.h"
#include "redtide.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <functional>
#include "raymath.h"

namespace sf {

namespace {
struct B {
    Stage s;
    explicit B(const char* name, int w = 32, int h = 18) { s.name = name; s.w = w; s.h = h; s.t.assign(w * h, T_EMPTY); s.world = WD_NAUTILUS; s.author = "Depth"; }
    B& fill(int x0, int y0, int x1, int y1, uint8_t k = T_STONE) { for (int y = std::min(y0, y1); y <= std::max(y0, y1); y++) for (int x = std::min(x0, x1); x <= std::max(x0, x1); x++) s.Set(x, y, k); return *this; }
    B& floor(int y, uint8_t k = T_STONE) { return fill(0, y, s.w - 1, y, k); }
    B& walls(int top) { fill(0, 0, 0, top); return fill(s.w - 1, 0, s.w - 1, top); }
    B& gap(int x0, int x1, int y0, int y1) { return fill(x0, y0, x1, y1, T_EMPTY); }
    B& spawn(int x, int y) { s.spawns.push_back({(x + 0.5f) * TILE, y * TILE}); return *this; }   // (feet at row y: standing on row y-1)
    B& piece(uint8_t kind, int x, int y, int w, int h, int dx, int dy, float travel, float period, float phase = 0, float on = 0.8f, float power = 1) {
        Piece p; p.kind = kind; p.x = x; p.y = y; p.w = w; p.h = h; p.dx = dx; p.dy = dy; p.travel = travel; p.period = period; p.phase = phase; p.on = on; p.power = power;
        if (kind == PK_VENT || kind == PK_WINDOW) for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++) s.Set(xx, yy, kind == PK_VENT ? T_STONE : T_GLASS);
        s.pieces.push_back(p); return *this;
    }
    B& crate(int c) { s.crateCols.push_back(c); return *this; }
    B& wrap() { s.wrap = true; return *this; }
};
Stage Mirror(const Stage& in, const char* name) {
    Stage s = in; s.name = name;
    for (int y = 0; y < s.h; y++) for (int x = 0; x < s.w; x++) { uint8_t k = in.At(s.w - 1 - x, y); if (k == T_CONV_L) k = T_CONV_R; else if (k == T_CONV_R) k = T_CONV_L; s.t[y * s.w + x] = k; }
    for (auto& sp : s.spawns) sp.x = s.Width() - sp.x;
    for (auto& p : s.pieces) { p.x = s.w - p.x - p.w; p.dx = -p.dx; if (p.kind != PK_WINDOW) p.phase = p.phase + 0.5f >= 1 ? p.phase - 0.5f : p.phase + 0.5f; }
    for (auto& c : s.crateCols) c = s.w - 1 - c;
    return s;
}
}

std::vector<Stage> BuildNautilus() {
    std::vector<Stage> v;
    // 1. The Engine Room: two pistons slam the middle floor on a rhythm; steam vents in the side wells lift you back up
    { B b("The Engine Room"); b.floor(6).gap(6, 9, 6, 6).gap(22, 25, 6, 6).fill(0, 0, 4, 2).fill(27, 0, 31, 2).fill(7, 0, 9, 2).fill(22, 0, 24, 2)
        .fill(10, 15, 13, 15).fill(18, 15, 21, 15).fill(4, 11, 8, 11).fill(23, 11, 27, 11)
        .piece(PK_PISTON, 11, 13, 2, 2, 0, -1, 6, 2.6f, 0.0f, 0.7f).piece(PK_PISTON, 19, 13, 2, 2, 0, -1, 6, 2.6f, 0.5f, 0.7f)
        .piece(PK_VENT, 8, 2, 1, 1, 0, 1, 7, 2.5f, 0.0f, 1.0f, 1.0f).piece(PK_VENT, 23, 2, 1, 1, 0, 1, 7, 2.5f, 0.5f, 1.0f, 1.0f)
        .spawn(2, 7).spawn(14, 7).spawn(17, 7).spawn(29, 7).spawn(6, 12).spawn(25, 12);
      v.push_back(b.s); }
    // 2. The Torpedo Tubes: a tube in each wall fires anyone at its mouth across the stage
    { B b("The Torpedo Tubes"); b.floor(1).gap(13, 18, 1, 1).walls(9).fill(1, 5, 4, 5).fill(27, 5, 30, 5).fill(9, 8, 22, 8).fill(13, 12, 18, 12)
        .piece(PK_TUBE, 1, 2, 1, 1, 1, 0, 0, 1.0f, 0, 0, 20).piece(PK_TUBE, 30, 2, 1, 1, -1, 0, 0, 1.0f, 0, 0, 20)
        .spawn(5, 2).spawn(26, 2).spawn(3, 6).spawn(28, 6).spawn(11, 9).spawn(20, 9).spawn(15, 13).spawn(16, 13);
      v.push_back(b.s); }
    // 3. The Salon Window: the floor in the middle is the great window; at 20 s it breaks over the dark
    { B b("The Salon Window"); b.fill(0, 2, 7, 3).fill(24, 2, 31, 3).fill(4, 9, 10, 9).fill(21, 9, 27, 9).fill(13, 13, 18, 13)
        .piece(PK_WINDOW, 8, 3, 16, 1, 0, 0, 0, 0, 20.0f)
        .spawn(3, 4).spawn(28, 4).spawn(12, 4).spawn(19, 4).spawn(7, 10).spawn(24, 10).spawn(15, 14).spawn(16, 14);
      v.push_back(b.s); }
    // 4. The Ballast Tanks: stepped tanks, wet (icy) floors, vents between them
    { B b("The Ballast Tanks"); b.fill(0, 0, 6, 1, T_ICE).fill(9, 0, 15, 3, T_ICE).fill(16, 0, 22, 3, T_ICE).fill(25, 0, 31, 1, T_ICE).fill(3, 7, 9, 7).fill(22, 7, 28, 7).fill(12, 10, 19, 10)
        .piece(PK_VENT, 7, 0, 1, 1, 0, 1, 6, 3.0f, 0.0f, 1.0f).piece(PK_VENT, 24, 0, 1, 1, 0, 1, 6, 3.0f, 0.5f, 1.0f)
        .spawn(2, 2).spawn(29, 2).spawn(11, 4).spawn(20, 4).spawn(5, 8).spawn(26, 8).spawn(14, 11).spawn(17, 11);
      v.push_back(b.s); }
    // 5. The Galley: conveyors along the floor (one each way), an elevator in the middle
    { B b("The Galley"); b.fill(0, 1, 13, 1, T_CONV_R).fill(18, 1, 31, 1, T_CONV_L).fill(0, 0, 13, 0).fill(18, 0, 31, 0).fill(2, 6, 8, 6).fill(23, 6, 29, 6).fill(10, 11, 21, 11)
        .piece(PK_ELEVATOR, 14, 1, 4, 1, 0, 1, 8, 6.0f)
        .spawn(3, 2).spawn(28, 2).spawn(9, 2).spawn(22, 2).spawn(5, 7).spawn(26, 7).spawn(13, 12).spawn(18, 12);
      v.push_back(b.s); }
    // 6. The Conning Tower: a chimney to climb (wall jumps), the rails live at the top
    { B b("The Conning Tower"); b.floor(1).gap(12, 19, 1, 1).fill(10, 2, 11, 9).fill(20, 2, 21, 9).fill(10, 10, 11, 10, T_RAIL).fill(20, 10, 21, 10, T_RAIL).fill(0, 5, 5, 5).fill(26, 5, 31, 5).fill(4, 9, 8, 9).fill(23, 9, 27, 9)
        .fill(13, 7, 18, 7).fill(12, 15, 19, 15)
        .spawn(3, 2).spawn(28, 2).spawn(7, 2).spawn(24, 2).spawn(3, 8).spawn(28, 8).spawn(15, 10).spawn(16, 10);
      v.push_back(b.s); }
    // 7. The Propeller Shaft: the screw turns at the bottom of the middle; it pulls
    { B b("The Propeller Shaft"); b.fill(0, 0, 10, 3).fill(21, 0, 31, 3).fill(4, 8, 11, 8).fill(20, 8, 27, 8).fill(12, 12, 19, 12).fill(13, 4, 18, 4, T_ROPE)
        .piece(PK_PROPELLER, 14, 0, 4, 2, 0, 0, 0, 0, 0, 0, 1.0f)
        .spawn(2, 4).spawn(29, 4).spawn(8, 4).spawn(23, 4).spawn(6, 9).spawn(25, 9).spawn(14, 13).spawn(17, 13);
      v.push_back(b.s); }
    // 8. The Pressure Hull: it wraps left to right (off one side, in at the other)
    { B b("The Pressure Hull"); b.wrap().floor(2).gap(8, 11, 2, 2).gap(20, 23, 2, 2).fill(0, 0, 31, 1).gap(8, 11, 0, 1).gap(20, 23, 0, 1).fill(0, 7, 5, 7).fill(26, 7, 31, 7).fill(12, 7, 19, 7).fill(5, 12, 10, 12).fill(21, 12, 26, 12)
        .spawn(3, 3).spawn(15, 3).spawn(16, 3).spawn(28, 3).spawn(2, 8).spawn(29, 8).spawn(14, 8).spawn(17, 8);
      v.push_back(b.s); }
    // 9. The Reactor Rails: the floor's middle sections are rails, live on a rhythm; the safe ledges are high
    { B b("The Reactor Rails"); b.floor(1).fill(0, 0, 31, 0).fill(8, 1, 13, 1, T_RAIL).fill(18, 1, 23, 1, T_RAIL).fill(3, 6, 9, 6).fill(22, 6, 28, 6).fill(12, 9, 19, 9).fill(0, 12, 5, 12).fill(26, 12, 31, 12)
        .spawn(3, 2).spawn(28, 2).spawn(15, 2).spawn(16, 2).spawn(5, 7).spawn(26, 7).spawn(2, 13).spawn(29, 13);
      v.push_back(b.s); }
    // 10. The Cargo Hold: two elevator shafts, crate zones over the shelves
    { B b("The Cargo Hold"); b.floor(1).fill(0, 0, 31, 0).fill(0, 6, 7, 6).fill(24, 6, 31, 6).fill(12, 6, 19, 6).fill(0, 11, 7, 11).fill(24, 11, 31, 11).fill(12, 11, 19, 11)
        .piece(PK_ELEVATOR, 9, 2, 2, 1, 0, 1, 9, 5.0f).piece(PK_ELEVATOR, 21, 2, 2, 1, 0, 1, 9, 5.0f, 0.5f).crate(3).crate(15).crate(28)
        .spawn(3, 2).spawn(28, 2).spawn(14, 2).spawn(17, 2).spawn(4, 7).spawn(27, 7).spawn(15, 12).spawn(16, 12);
      v.push_back(b.s); }
    // 11. The Pipe Run: rope catwalks over a bottomless drop
    { B b("The Pipe Run"); b.fill(0, 0, 5, 4).fill(26, 0, 31, 4).fill(6, 4, 25, 4, T_ROPE).fill(3, 9, 10, 9, T_ROPE).fill(21, 9, 28, 9, T_ROPE).fill(12, 9, 19, 9).fill(9, 13, 22, 13, T_ROPE)
        .spawn(2, 5).spawn(29, 5).spawn(10, 5).spawn(21, 5).spawn(14, 10).spawn(17, 10).spawn(5, 10).spawn(26, 10);
      v.push_back(b.s); }
    // 12. The Brig: cells with pistons for doors (they slam sideways)
    { B b("The Brig"); b.floor(1).fill(0, 0, 31, 0).fill(0, 7, 31, 7).gap(13, 18, 7, 7).fill(8, 2, 8, 3).fill(23, 2, 23, 3).fill(4, 12, 27, 12).gap(12, 19, 12, 12)
        .piece(PK_PISTON, 9, 2, 2, 2, 1, 0, 2, 3.0f, 0.0f, 0.8f).piece(PK_PISTON, 21, 2, 2, 2, -1, 0, 2, 3.0f, 0.5f, 0.8f)
        .spawn(3, 2).spawn(28, 2).spawn(15, 2).spawn(16, 2).spawn(4, 8).spawn(27, 8).spawn(8, 13).spawn(23, 13);
      v.push_back(b.s); }
    // 13. The Chart Room: glass floors that break under a stick's weight
    { B b("The Chart Room"); b.fill(0, 0, 7, 2).fill(24, 0, 31, 2).fill(8, 2, 23, 2, T_GLASS).fill(3, 7, 10, 7).fill(21, 7, 28, 7).fill(11, 7, 20, 7, T_GLASS).fill(12, 12, 19, 12)
        .spawn(3, 3).spawn(28, 3).spawn(5, 8).spawn(26, 8).spawn(14, 13).spawn(17, 13).spawn(7, 3).spawn(24, 3);
      v.push_back(b.s); }
    // 14. The Bilge: a low steel ceiling, wet floors, vents
    { B b("The Bilge"); b.floor(1, T_ICE).fill(0, 0, 31, 0).fill(0, 10, 9, 10).fill(22, 10, 31, 10).fill(4, 5, 11, 5).fill(20, 5, 27, 5).fill(13, 8, 18, 8)
        .piece(PK_VENT, 2, 1, 1, 1, 0, 1, 4, 3.0f, 0.0f, 0.8f).piece(PK_VENT, 29, 1, 1, 1, 0, 1, 4, 3.0f, 0.5f, 0.8f).piece(PK_VENT, 15, 1, 2, 1, 0, 1, 5, 4.0f, 0.25f, 1.0f)
        .spawn(5, 2).spawn(26, 2).spawn(11, 2).spawn(20, 2).spawn(6, 6).spawn(25, 6).spawn(14, 9).spawn(17, 9);
      v.push_back(b.s); }
    // 15. The Observation Deck: high glass and a rail along the roof
    { B b("The Observation Deck"); b.fill(0, 0, 31, 1).gap(12, 19, 0, 1).fill(12, 1, 19, 1, T_GLASS).fill(2, 6, 9, 6).fill(22, 6, 29, 6).fill(11, 10, 20, 10, T_GLASS).fill(0, 14, 31, 14, T_RAIL).gap(5, 26, 14, 14)
        .spawn(3, 2).spawn(28, 2).spawn(8, 2).spawn(23, 2).spawn(4, 7).spawn(27, 7).spawn(14, 11).spawn(17, 11);
      v.push_back(b.s); }
    // 16. The Escape Hatch: a lift up the middle; the sides fall away
    { B b("The Escape Hatch"); b.fill(0, 0, 9, 2).fill(22, 0, 31, 2).fill(0, 8, 8, 8).fill(23, 8, 31, 8).fill(10, 13, 21, 13)
        .piece(PK_ELEVATOR, 14, 2, 4, 1, 0, 1, 9, 7.0f)
        .spawn(3, 3).spawn(28, 3).spawn(7, 3).spawn(24, 3).spawn(4, 9).spawn(27, 9).spawn(13, 14).spawn(18, 14);
      v.push_back(b.s); }
    // 17. The Boiler: vents on staggered timers under the platforms
    { B b("The Boiler"); b.floor(1).fill(0, 0, 31, 0).fill(4, 7, 10, 7).fill(21, 7, 27, 7).fill(12, 11, 19, 11)
        .piece(PK_VENT, 6, 1, 1, 1, 0, 1, 5, 2.0f, 0.0f, 0.6f).piece(PK_VENT, 13, 1, 1, 1, 0, 1, 9, 2.0f, 0.33f, 0.6f).piece(PK_VENT, 18, 1, 1, 1, 0, 1, 9, 2.0f, 0.66f, 0.6f).piece(PK_VENT, 25, 1, 1, 1, 0, 1, 5, 2.0f, 0.5f, 0.6f)
        .spawn(2, 2).spawn(29, 2).spawn(10, 2).spawn(21, 2).spawn(8, 8).spawn(23, 8).spawn(15, 12).spawn(16, 12);
      v.push_back(b.s); }
    // 18. The Gangway: narrow walkways over a bottomless drop, the middle one a conveyor
    { B b("The Gangway"); b.fill(0, 0, 4, 3).fill(27, 0, 31, 3).fill(7, 3, 12, 3).fill(19, 3, 24, 3).fill(13, 6, 18, 6, T_CONV_R).fill(4, 9, 11, 9).fill(20, 9, 27, 9).fill(13, 13, 18, 13)
        .spawn(2, 4).spawn(29, 4).spawn(9, 4).spawn(22, 4).spawn(6, 10).spawn(25, 10).spawn(15, 14).spawn(16, 14);
      v.push_back(b.s); }
    // 19. The Periscope Well: a tall column in the middle, live rails on its sides
    { B b("The Periscope Well"); b.floor(1).fill(0, 0, 31, 0).fill(14, 2, 17, 12).fill(13, 4, 13, 9, T_RAIL).fill(18, 4, 18, 9, T_RAIL).fill(1, 6, 7, 6).fill(24, 6, 30, 6).fill(8, 10, 12, 10).fill(19, 10, 23, 10)
        .spawn(3, 2).spawn(28, 2).spawn(10, 2).spawn(21, 2).spawn(4, 7).spawn(27, 7).spawn(15, 13).spawn(16, 13);
      v.push_back(b.s); }
    // 20. The Steam Catwalks: rope catwalks and the vents under them
    { B b("The Steam Catwalks"); b.fill(0, 0, 31, 0).fill(0, 1, 31, 1).gap(10, 21, 0, 1).fill(3, 5, 12, 5, T_ROPE).fill(19, 5, 28, 5, T_ROPE).fill(9, 9, 22, 9, T_ROPE).fill(13, 13, 18, 13)
        .piece(PK_VENT, 4, 1, 1, 1, 0, 1, 7, 3.0f, 0.0f, 0.8f).piece(PK_VENT, 27, 1, 1, 1, 0, 1, 7, 3.0f, 0.5f, 0.8f)
        .spawn(2, 2).spawn(29, 2).spawn(7, 2).spawn(24, 2).spawn(6, 6).spawn(25, 6).spawn(14, 10).spawn(17, 10);
      v.push_back(b.s); }
    // the variants: mirrored, with the rhythm moved (the forty)
    static const char* VAR[20] = {"The Engine Room (Night Shift)", "The Torpedo Tubes (Reloaded)", "The Salon Window (Storm)", "The Ballast Tanks (Flooded)", "The Galley (Rush Hour)",
                                  "The Conning Tower (Surfacing)", "The Propeller Shaft (Full Ahead)", "The Pressure Hull (Deep)", "The Reactor Rails (Overload)", "The Cargo Hold (Shifting)",
                                  "The Pipe Run (Frayed)", "The Brig (Lockdown)", "The Chart Room (Cracked)", "The Bilge (Rising)", "The Observation Deck (Dusk)",
                                  "The Escape Hatch (Last Out)", "The Boiler (Overpressure)", "The Gangway (Wet)", "The Periscope Well (Down)", "The Steam Catwalks (Fog)"};
    int n = (int)v.size();
    for (int i = 0; i < n; i++) {
        Stage m = Mirror(v[i], VAR[i]);
        for (auto& p : m.pieces) { if (p.kind == PK_PISTON) p.period *= 0.8f; if (p.kind == PK_VENT) p.period *= 0.85f; if (p.kind == PK_WINDOW) p.phase = 15; if (p.kind == PK_ELEVATOR) p.period *= 0.75f; }
        if (i == 17) for (int y = 0; y < m.h; y++) for (int x = 0; x < m.w; x++) if (m.At(x, y) == T_STONE && y == 3) m.Set(x, y, T_ICE);
        v.push_back(m);
    }
    return v;
}
int RunScuffleBuildPacks() {
    static const char* KEY[WD_COUNT] = {"nautilus", "cave", "reef", "atlantis", "void", "salon"};
    int badAll = 0;
    for (int wd = 0; wd < WD_COUNT; wd++) {
        if (getenv("DEPTH_WORLD") && atoi(getenv("DEPTH_WORLD")) != wd) continue;
        std::vector<Stage> v = BuildWorld(wd);
        std::string path = rt::DataDir() + "/../scuffle/stages/" + KEY[wd] + ".txt";
        std::ofstream f(path);
        f << "; Scuffle: " << WorldName(wd) << "'s built-in stages (doc pp. 9-11, 18) and its three finales, in the editor's text form. Written by depth.exe --scuffle-build-packs;\n";
        f << "; each stage: a '== name' line, the rows (top first), then name/world/... and '@n' piece lines. Open any of them in the editor.\n";
        int bad = 0, fin = 0;
        for (const auto& s : v) {
            Reach r = CheckReachable(s);
            if (!r.ok) { bad++; printf("  UNREACHABLE  %s: %s\n", s.name.c_str(), r.why.c_str()); }
            fin += s.finale;
            f << "== " << s.name << "\n";
            for (const auto& l : StageToText(s)) f << l << "\n";
        }
        printf("scuffle-build-packs: %s: %d stages and %d finales written to %s (%d fail the reachability check)\n", WorldName(wd), (int)v.size() - fin, fin, path.c_str(), bad);
        badAll += bad;
    }
    return badAll ? 1 : 0;
}

int RunScuffleVerify(const std::string& arg) {
    std::vector<Stage> list;
    if (arg == "all") { for (int w = 0; w < WD_COUNT; w++) { auto p = LoadWorldPack(w); list.insert(list.end(), p.begin(), p.end()); } }
    else { Stage s; std::string err; std::vector<Stage> pack;
           if (StageFromCode(arg, s, &err)) list.push_back(s); else if (PackFromCode(arg, pack, &err)) list = pack; else { printf("scuffle-verify: %s\n", err.c_str()); return 1; } }
    int bad = 0;
    for (const auto& s : list) { Reach r = CheckReachable(s); if (!r.ok) bad++; printf("  %s  %s (%s): %d spawns, %d places reached%s%s\n", r.ok ? "ok  " : "FAIL", s.name.c_str(), WorldName(s.world), r.spawns, r.nodes, r.ok ? "" : ": ", r.why.c_str()); }
    printf(bad ? "scuffle-verify: %d of %d stages FAIL the reachability check\n" : "scuffle-verify: all %d stages pass the reachability check\n", bad ? bad : (int)list.size(), (int)list.size());
    return bad ? 1 : 0;
}

// ---------------------------------------------------------------- the stage-3 checks (in --scuffle-test)
static int SF3 = 0;
static void C3(bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) SF3++; }
static bool Same(const Stage& a, const Stage& b) {
    if (a.w != b.w || a.h != b.h || a.t != b.t || a.name != b.name || a.world != b.world || a.wrap != b.wrap || a.spawns.size() != b.spawns.size() || a.pieces.size() != b.pieces.size() || a.crateCols != b.crateCols) {
        if (getenv("DEPTH_SAMETRACE")) printf("    differ: size %d/%d %d/%d tiles %d name %d world %d/%d wrap %d spawns %d/%d pieces %d/%d crates %d\n", a.w, b.w, a.h, b.h, (int)(a.t != b.t), (int)(a.name != b.name), a.world, b.world, (int)(a.wrap != b.wrap), (int)a.spawns.size(), (int)b.spawns.size(), (int)a.pieces.size(), (int)b.pieces.size(), (int)(a.crateCols != b.crateCols));
        return false;
    }
    for (size_t i = 0; i < a.spawns.size(); i++) if (fabsf(a.spawns[i].x - b.spawns[i].x) > 0.01f || fabsf(a.spawns[i].y - b.spawns[i].y) > 0.01f) return false;
    for (size_t i = 0; i < a.pieces.size(); i++) { const Piece& p = a.pieces[i]; const Piece& q = b.pieces[i]; if (p.kind != q.kind || p.x != q.x || p.y != q.y || p.w != q.w || p.h != q.h || p.dx != q.dx || p.dy != q.dy || fabsf(p.travel - q.travel) > 0.06f || fabsf(p.period - q.period) > 0.06f || fabsf(p.phase - q.phase) > 0.006f || fabsf(p.on - q.on) > 0.006f || fabsf(p.power - q.power) > 0.06f) { if (getenv("DEPTH_SAMETRACE")) printf("    piece %d differs: kind %d/%d at %d,%d/%d,%d size %dx%d/%dx%d d %d,%d/%d,%d travel %g/%g period %g/%g phase %g/%g on %g/%g power %g/%g\n", (int)i, p.kind, q.kind, p.x, p.y, q.x, q.y, p.w, p.h, q.w, q.h, p.dx, p.dy, q.dx, q.dy, p.travel, q.travel, p.period, q.period, p.phase, q.phase, p.on, q.on, p.power, q.power); return false; } }
    return true;
}
static void Run(World& w, float s, std::function<void(World&)> fn = nullptr) { int n = (int)(s / STEP); for (int i = 0; i < n; i++) { if (fn) fn(w); w.Step(); } }
int ScuffleStageChecks() {
    SF3 = 0;
    printf("Scuffle: stage 3 (stages as text and codes, the reachability check, the Nautilus pieces and its forty stages)\n");
    std::vector<Stage> naut; for (const auto& s : LoadWorldPack(WD_NAUTILUS)) if (!s.finale) naut.push_back(s);
    C3(naut.size() == 40, TextFormat("the Nautilus pack: %d stages from data/scuffle/stages/nautilus.txt", (int)naut.size()));
    bool sigs = false; int found = 0; for (const auto& s : naut) found += s.name == "The Engine Room" || s.name == "The Torpedo Tubes" || s.name == "The Salon Window"; sigs = found == 3;
    C3(sigs, "with its signature stages: the Engine Room, the Torpedo Tubes, the Salon Window");
    // the gate: a stage round-trips through a code (and the text form, and a pack)
    { const Stage& s = naut.empty() ? StoneStage() : naut[0]; std::string code = StageToCode(s); Stage back; std::string err;
      bool okc = StageFromCode(code, back, &err);
      C3(okc && Same(s, back), TextFormat("a stage round-trips through a code (%d characters: %s...)%s", (int)code.size(), code.substr(0, 24).c_str(), okc ? "" : (" " + err).c_str()));
      Stage tx = StageFromText(StageToText(s), s.name.c_str()); C3(Same(s, tx), "and through the editor's text form");
      std::vector<Stage> pk(naut.begin(), naut.begin() + std::min<size_t>(20, naut.size())), pk2; std::string pc = PackToCode(pk);
      bool all = PackFromCode(pc, pk2, &err) && pk2.size() == pk.size(); for (size_t i = 0; all && i < pk.size(); i++) all = Same(pk[i], pk2[i]);
      C3(all, TextFormat("a pack of 20 round-trips too (%d characters)", (int)pc.size()));
      Stage junk; C3(!StageFromCode("SCF1-notacode!!", junk, &err) && !StageFromCode("hello", junk, &err), "and a damaged code is refused"); }
    // the reachability check: the forty pass; a walled-off spawn fails
    { int bad = 0; std::string who; for (const auto& s : naut) { Reach r = CheckReachable(s); if (!r.ok) { bad++; who += " [" + s.name + ": " + r.why + "]"; } }
      C3(bad == 0, TextFormat("every Nautilus stage passes the reachability check%s", who.c_str()));
      Stage boxed = StageFromText({"................................", "................................", "....#####.......................", "....#.S.#.......................", "....#####...............S.......", "################################"}, "Boxed");
      Reach r = CheckReachable(boxed); C3(!r.ok, "a spawn boxed in stone fails it (" + r.why + ")"); }
    // the pieces
    auto flat = [](std::vector<std::string> extra = {}) { std::vector<std::string> rows(16, std::string(32, (char)46)); rows.push_back(std::string(32, (char)35)); rows.push_back(std::string(32, (char)35)); for (auto& e : extra) rows.push_back(e); return rows; };
    { Stage s = StageFromText(flat(), "Piston"); Piece p; p.kind = PK_PISTON; p.x = 10; p.y = 8; p.w = 2; p.h = 2; p.dx = 0; p.dy = -1; p.travel = 6; p.period = 2; p.on = 0.6f; s.pieces.push_back(p);
      World w; w.Init(s, 1, 1); w.nextCrate = 1e9f; w.sticks[0].pos = {6.6f, 1.2f}; Run(w, 2.5f);
      C3(!w.sticks[0].alive && w.sticks[0].cause == "a piston", "a piston slams down and crushes the stick under it"); }
    { Stage s = StageFromText(flat(), "Lift"); Piece p; p.kind = PK_ELEVATOR; p.x = 5; p.y = 2; p.w = 4; p.h = 1; p.dx = 0; p.dy = 1; p.travel = 8; p.period = 6; s.pieces.push_back(p);
      World w; w.Init(s, 1, 1); w.nextCrate = 1e9f; w.sticks[0].pos = {4.2f, 1.9f}; Run(w, 0.4f); float y0 = w.sticks[0].pos.y; Run(w, 2.8f);
      C3(w.sticks[0].pos.y > y0 + 3.5f && w.sticks[0].alive, TextFormat("an elevator carries the stick standing on it (%.1f m up)", w.sticks[0].pos.y - y0)); }
    { Stage s = StageFromText(flat(), "Vent"); Piece p; p.kind = PK_VENT; p.x = 10; p.y = 1; p.w = 1; p.h = 1; p.dy = 1; p.travel = 6; p.period = 3; p.on = 1; s.pieces.push_back(p);
      World w; w.Init(s, 1, 1); w.nextCrate = 1e9f; w.sticks[0].pos = {6.3f, 1.2f}; float top = 0; Run(w, 0.8f, [&](World& ww) { top = std::max(top, ww.sticks[0].pos.y); });
      C3(top > 3.5f, TextFormat("a steam vent blows the stick up (%.1f m)", top - 1.2f)); }
    { Stage s = StageFromText(flat(), "Rail"); for (int x = 8; x < 14; x++) s.Set(x, 1, T_RAIL);
      World w; w.Init(s, 1, 1); w.nextCrate = 1e9f; w.sticks[0].pos = {6.3f, 1.2f}; Run(w, 4.5f);
      C3(!w.sticks[0].alive && w.sticks[0].cause == "an electrified rail", "an electrified rail kills when it's live"); }
    { Stage s = StageFromText(flat(), "Fan"); Piece p; p.kind = PK_PROPELLER; p.x = 12; p.y = 2; p.w = 3; p.h = 2; s.pieces.push_back(p);
      World w; w.Init(s, 1, 1); w.nextCrate = 1e9f; w.sticks[0].pos = {5.4f, 1.2f}; Run(w, 3.0f, [](World& ww) { ww.sticks[0].in.moveX = 1; });
      C3(!w.sticks[0].alive && w.sticks[0].cause == "the propeller", "the propeller shreds"); }
    { Stage s = StageFromText(flat(), "Tube"); Piece p; p.kind = PK_TUBE; p.x = 2; p.y = 2; p.w = 1; p.h = 1; p.dx = 1; p.power = 20; s.pieces.push_back(p);
      World w; w.Init(s, 1, 1); w.nextCrate = 1e9f; w.sticks[0].pos = {3.0f, 1.2f}; float far = 0; Run(w, 1.5f, [&](World& ww) { ww.sticks[0].in.moveX = -1; far = std::max(far, ww.sticks[0].pt[J_PELVIS].p.x); });
      C3(far > 9, TextFormat("a torpedo tube fires the stick across the stage (to %.0f m)", far)); }
    { Stage s = StageFromText(flat(), "Window"); Piece p; p.kind = PK_WINDOW; p.x = 0; p.y = 1; p.w = 32; p.h = 1; p.phase = 20; s.pieces.push_back(p); for (int x = 0; x < 32; x++) { s.Set(x, 1, T_GLASS); s.Set(x, 0, T_EMPTY); }
      World w; w.Init(s, 1, 1); w.nextCrate = 1e9f; w.sticks[0].pos = {6.3f, 1.2f}; Run(w, 19.5f); bool held = w.sticks[0].alive && w.sticks[0].grounded; Run(w, 3.0f);
      C3(held && !w.sticks[0].alive, "the window holds until its second (20 s), then gives way"); }
    { Stage s = StageFromText(flat(), "Glass"); for (int x = 8; x < 14; x++) { s.Set(x, 1, T_GLASS); s.Set(x, 0, T_EMPTY); }
      World w; w.Init(s, 1, 1); w.nextCrate = 1e9f; w.sticks[0].pos = {6.3f, 1.2f}; Run(w, 2.0f);
      C3(!w.sticks[0].alive, "glass breaks under a stick's weight"); }
    { std::vector<std::string> rows(18, std::string(32, (char)46)); rows[17] = std::string(32, (char)35); rows[10] = std::string(32, (char)45); Stage s = StageFromText(rows, "Rope");
      World w; w.Init(s, 1, 1); w.nextCrate = 1e9f; Stick& k = w.sticks[0]; k.pos = {6.3f, 6.0f}; Run(w, 1.0f); bool onRope = k.grounded && k.pos.y > 4;
      Run(w, 0.8f, [](World& ww) { ww.sticks[0].in.moveY = -1; }); Run(w, 0.8f);
      C3(onRope && k.pos.y < 1.0f, TextFormat("a rope bridge: you land on it from above (%d), hold down to drop through", (int)onRope)); }
    { Stage s = StageFromText(flat(), "Ice"); Stage s2 = s; for (int x = 0; x < 32; x++) s2.Set(x, 1, T_ICE);
      auto slide = [&](const Stage& st) { World w; w.Init(st, 1, 1); w.nextCrate = 1e9f; Stick& k = w.sticks[0]; k.pos = {4, 1.2f}; Run(w, 0.3f); Run(w, 0.8f, [](World& ww) { ww.sticks[0].in.moveX = 1; }); k.in = Input{}; float x0 = k.pos.x; Run(w, 1.0f); return k.pos.x - x0; };
      float a = slide(s), b = slide(s2); C3(b > a * 3, TextFormat("ice: let go and you slide on (%.1f m against %.1f m)", b, a)); }
    { Stage s = StageFromText(flat(), "Conveyor"); for (int x = 0; x < 32; x++) s.Set(x, 1, T_CONV_R);
      World w; w.Init(s, 1, 1); w.nextCrate = 1e9f; Stick& k = w.sticks[0]; k.pos = {4, 1.2f}; Run(w, 0.3f); float x0 = k.pos.x; Run(w, 2.0f);
      C3(k.pos.x - x0 > 4, TextFormat("a conveyor carries you (%.1f m)", k.pos.x - x0)); }
    { Stage s = StageFromText(flat(), "Wrap"); s.wrap = true;
      World w; w.Init(s, 1, 1); w.nextCrate = 1e9f; Stick& k = w.sticks[0]; k.pos = {18.6f, 1.2f}; Run(w, 0.3f); Run(w, 0.6f, [](World& ww) { ww.sticks[0].in.moveX = 1; });
      C3(k.alive && k.pos.x < 6, TextFormat("a wrapping stage: off the right edge, in at the left (%.1f)", k.pos.x)); }
    return SF3;
}

} // namespace sf
