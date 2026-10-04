// Scuffle stage 5: the other five worlds (doc pp. 9-11): the checks of their pieces, water, walls and set pieces
// (ScuffleWorldChecks, in --scuffle-test). The worlds' stages, the generator and the finales are in scuffle_build.cpp.
#include "scuffle.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace sf {

static int WF = 0;
static void WC(bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) WF++; }
// a test stage: w x h tiles, stone floor two rows deep
static Stage Box(int world = WD_NAUTILUS, int w = 40, int h = 18) {
    Stage s; s.w = w; s.h = h; s.t.assign(w * h, T_EMPTY); s.world = world; s.name = "test";
    for (int x = 0; x < w; x++) { s.Set(x, 0, T_STONE); s.Set(x, 1, T_STONE); }
    s.spawns = {{5 * TILE, 2 * TILE}, {(w - 5) * TILE, 2 * TILE}};
    return s;
}
static void Fill(Stage& s, int x0, int y0, int x1, int y1, uint8_t k) { for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) s.Set(x, y, k); }
static Piece& Add(Stage& s, int kind, int x, int y, int w, int h) {
    Piece p; p.kind = (uint8_t)kind; PieceDefaults(p); p.x = x; p.y = y; p.w = w; p.h = h;
    if (kind == PK_COLUMN || kind == PK_EEL || kind == PK_GRATE || kind == PK_DRIP || kind == PK_DART || kind == PK_REACHER) Fill(s, x, y, x + w - 1, y + h - 1, T_STONE);
    if (kind == PK_SHARK || kind == PK_TOAD) Fill(s, x, y, x + w - 1, y + h - 1, T_WATER);
    if (kind == PK_POOL) Fill(s, x, y, x + w - 1, y + h - 1, T_WOOD);
    s.pieces.push_back(p); return s.pieces.back();
}
// one or two sticks placed by hand (no crates, no wall unless asked)
static World Put(const Stage& s, std::vector<Vector2> at, bool wall = false) {
    World w; w.Init(s, (int)at.size(), 7); w.nextCrate = 1e9f; w.wallOn = wall;
    for (size_t i = 0; i < at.size(); i++) w.SpawnStick(w.sticks[i], at[i], 1);
    return w;
}
static void Run(World& w, float s, std::function<void(World&)> fn = nullptr) { int n = (int)lroundf(s / STEP); for (int i = 0; i < n; i++) { if (fn) fn(w); w.Step(); } }
static float RunUntilDead(World& w, int who, float maxS, std::function<void(World&)> fn = nullptr) { int n = (int)(maxS / STEP); for (int i = 0; i < n; i++) { if (fn) fn(w); w.Step(); if (!w.sticks[who].alive) return w.t; } return -1; }
static int Arm(World& w, Stick& k, const char* key) { int it = w.SpawnWeapon(WeaponIndex(key), k.pt[J_HAND_R].p, {0, 0}); w.Pickup(k, it); k.fireCool = 0; return it; }
static float F(float tiles) { return tiles * TILE; }

int ScuffleWorldChecks() {
    WF = 0;
    printf("Scuffle: stage 5 (the other five worlds: water, their pieces, their walls and set pieces)\n");
    // ---- water and brine
    {
        Stage s = Box(WD_REEF); Fill(s, 2, 2, 37, 10, T_WATER);
        World w = Put(s, {{F(12), F(2)}});
        Run(w, 3);
        Stick& k = w.sticks[0];
        WC(k.alive && k.wet && k.swimT > 2.5f, TextFormat("water: you swim (%.1f s with your head under, still alive)", k.swimT));
        Arm(w, k, "carbine"); size_t b0 = w.bullets.size();
        k.in.fire = false; w.Step(); k.in.fire = true; w.Step(); w.Step();
        bool carbineDry = w.bullets.size() == b0;
        k.in.fire = false; w.Step(); w.DropWeapon(k, {0, 0}, false); Arm(w, k, "harpoon"); w.Step(); k.in.aim = {1, 0}; k.in.fire = true; w.Step(); w.Step(); k.in.fire = false;
        WC(carbineDry && w.bullets.size() > b0, "a gun won't fire underwater; the harpoon does");
        float died = RunUntilDead(w, 0, 8);
        WC(!k.alive && k.cause == "drowned" && died > 7 && died < 9.5f, TextFormat("and you drown after 8 s (%.1f s): %s", died, k.cause.c_str()));
        Stage b = Box(WD_VOID); Fill(b, 2, 2, 37, 6, T_BRINE);
        World wb = Put(b, {{F(12), F(2)}});
        float db = RunUntilDead(wb, 0, 6);
        WC(db > 2.5f && db < 4 && wb.sticks[0].cause == "the brine", TextFormat("the Void's brine takes you in 3 s (%.1f s)", db));
    }
    // ---- low gravity, crumbling floors, urchins
    {
        Stage s = Box(WD_VOID); World w0 = Put(s, {{F(12), F(2)}});
        Run(w0, 0.6f); float top0 = 0; Run(w0, 1.5f, [&](World& w) { w.sticks[0].in.jump = true; top0 = std::max(top0, w.sticks[0].pos.y); });
        Add(s, PK_LOWG, 2, 2, 30, 14);
        World w1 = Put(s, {{F(12), F(2)}});
        Run(w1, 0.6f); float top1 = 0; Run(w1, 2.5f, [&](World& w) { w.sticks[0].in.jump = true; top1 = std::max(top1, w.sticks[0].pos.y); });
        WC(top1 - F(2) > 1.8f * (top0 - F(2)), TextFormat("a low gravity pocket: the jump goes %.1f m (%.1f m outside it)", top1 - F(2), top0 - F(2)));
        Stage c = Box(WD_CAVE); Fill(c, 10, 6, 16, 6, T_CRUMBLE);
        World wc = Put(c, {{F(13), F(7)}});
        Run(wc, 0.3f); bool still = wc.stage.At(13, 6) == T_CRUMBLE; Run(wc, 1.0f);
        WC(still && wc.stage.At(13, 6) == T_EMPTY, "a crumbling floor holds a moment, then goes");
        Stage u = Box(WD_REEF); u.Set(14, 2, T_URCHIN);
        World wu = Put(u, {{F(11), F(2)}});
        Run(wu, 1.0f, [](World& w) { w.sticks[0].in.moveX = 1; });
        WC(wu.sticks[0].hp <= 85 && wu.sticks[0].lastHitT > 0, TextFormat("urchins sting (%.0f HP left)", wu.sticks[0].hp));
    }
    // ---- the Cave
    {
        Stage s = Box(WD_CAVE); s.Set(20, 2, T_CRYSTAL);
        World w = Put(s, {{F(23), F(2)}});
        Run(w, 0.2f);
        bool shattered = w.ShotAt({F(20.5f), F(2.5f)}, 0.05f) && w.stage.At(20, 2) == T_EMPTY;
        Run(w, 0.6f);
        WC(shattered && w.sticks[0].hp < 100, TextFormat("crystal shatters when shot, into shrapnel (%.0f HP left nearby)", w.sticks[0].hp));
        Stage st = Box(WD_CAVE); Fill(st, 0, 16, 39, 17, T_STONE); Add(st, PK_STALACTITE, 15, 13, 1, 3);
        World ws = Put(st, {{F(15.5f), F(2)}});
        Run(ws, 1.0f); float before = ws.sticks[0].hp;
        ws.ShotAt({F(15.5f), F(14)}, 0.05f); Run(ws, 1.5f);
        WC(before == 100 && ws.sticks[0].hp <= 30, TextFormat("a stalactite hangs until it's shot, then falls on whoever's under it (%.0f HP left)", ws.sticks[0].hp));
        Stage d = Box(WD_CAVE); Add(d, PK_DRIP, 15, 14, 1, 1);
        World wd = Put(d, {{F(15.5f), F(2)}});
        Run(wd, 3);
        WC(wd.sticks[0].hp <= 80, TextFormat("acid drips (%.0f HP left)", wd.sticks[0].hp));
        Stage r = Box(WD_CAVE); Add(r, PK_STREAM, 3, 2, 34, 4);
        World wr = Put(r, {{F(6), F(2)}});
        Run(wr, 0.3f); float x0 = wr.sticks[0].pos.x; Run(wr, 2);
        WC(wr.sticks[0].pos.x - x0 > 5, TextFormat("the slipstream carries a stick standing still (%.1f m in 2 s)", wr.sticks[0].pos.x - x0));
        Stage su = Box(WD_REEF); Piece& surge = Add(su, PK_STREAM, 3, 2, 34, 4); surge.period = 10; surge.on = 2;
        World wsu = Put(su, {{F(6), F(2)}});
        Run(wsu, 0.3f); float sx0 = wsu.sticks[0].pos.x; Run(wsu, 2.0f); float sx1 = wsu.sticks[0].pos.x; Run(wsu, 4); float sx2 = wsu.sticks[0].pos.x;
        WC(sx1 - sx0 > 3 && fabsf(sx2 - sx1) < 0.5f, TextFormat("the Reef's surge channel runs on a count (%.1f m while on, %.1f m while off)", sx1 - sx0, sx2 - sx1));
        Stage o = Box(WD_CAVE); Add(o, PK_TOAD, 4, 2, 4, 2);
        World wo = Put(o, {{F(10), F(2)}});
        float dt = RunUntilDead(wo, 0, 5);
        WC(dt > 0 && wo.sticks[0].cause == "the toad", TextFormat("the toad's tongue takes a stick at the pool's edge (%.1f s)", dt));
    }
    // ---- the Reef
    {
        Stage s = Box(WD_REEF); Add(s, PK_REACHER, 14, 2, 1, 2);
        World w = Put(s, {{F(11), F(2)}});
        float heldX = -1; bool held = false, freed = false;
        Run(w, 3, [&](World& ww) { ww.sticks[0].in.moveX = -1; if (ww.t < 0.9f) ww.sticks[0].in.moveX = 1;
            if (ww.stage.pieces[0].hold == 0) { if (!held) heldX = ww.sticks[0].pos.x; held = true; } else if (held) freed = true; });
        WC(held && freed && w.sticks[0].pos.x < heldX - 0.5f, "reacher coral holds a stick that touches it, then lets go");
        Stage e = Box(WD_REEF); Fill(e, 14, 2, 15, 9, T_STONE); Piece& eel = Add(e, PK_EEL, 14, 2, 1, 2); eel.dx = -1;
        World we = Put(e, {{F(13), F(2)}});
        Run(we, 1.2f);
        WC(we.sticks[0].hp <= 65, TextFormat("an eel bites whatever stands in front of its hole (%.0f HP left)", we.sticks[0].hp));
        Stage h = Box(WD_REEF); Fill(h, 0, 0, 39, 1, T_EMPTY); Add(h, PK_SHARK, 0, 0, 40, 4);
        World wh = Put(h, {{F(12), F(1)}});
        float ds = RunUntilDead(wh, 0, 3);
        WC(ds > 0 && wh.sticks[0].cause == "a shark", TextFormat("sharks in the water below (%.1f s: %s)", ds, wh.sticks[0].cause.c_str()));
    }
    // ---- Atlantis
    {
        Stage s = Box(WD_ATLANTIS); Add(s, PK_GRATE, 14, 1, 2, 1);
        World w = Put(s, {{F(15), F(2)}});
        float dg = RunUntilDead(w, 0, 3);
        WC(dg > 0.7f && w.sticks[0].cause == "the Wyrm", TextFormat("the Wyrm strikes up from a grate after its tell (%.1f s)", dg));
        Stage tu = Box(WD_ATLANTIS); Add(tu, PK_TUNA, 0, 2, 40, 3);
        World wt = Put(tu, {{F(15), F(2)}});
        Run(wt, 1.5f);
        WC(wt.sticks[0].hp <= 75 && wt.sticks[0].lastHitT > 0, TextFormat("the tuna lane: a ram (%.0f HP left)", wt.sticks[0].hp));
        Stage sl = Box(WD_ATLANTIS); Add(sl, PK_SLUICE, 10, 2, 11, 5);
        World wl = Put(sl, {{F(3), F(2)}});
        Run(wl, 1); bool on = wl.InLiquid({F(15), F(3)}); Run(wl, 5); bool off = !wl.InLiquid({F(15), F(3)});
        WC(on && off, "the sluice floods its terrace, then drains");
        Stage co = Box(WD_ATLANTIS); Add(co, PK_COLUMN, 12, 2, 1, 6);
        World wc = Put(co, {{F(15), F(2)}});
        Run(wc, 0.5f); float hb = wc.sticks[0].hp;
        wc.ShotAt({F(12.5f), F(5)}, 0.05f); Run(wc, 1.5f);
        WC(hb == 100 && wc.sticks[0].hp <= 20 && wc.stage.At(12, 7) == T_EMPTY && wc.stage.Solid(15, 2), TextFormat("a column topples when shot, crushes what's under it and lies as rubble (%.0f HP left)", wc.sticks[0].hp));
    }
    // ---- the Void
    {
        Stage s = Box(WD_VOID); Piece& lure = Add(s, PK_LURE, 30, 3, 1, 1); lure.travel = 14;
        World w = Put(s, {{F(22), F(2)}});
        Run(w, 0.3f); float x0 = w.sticks[0].pos.x; Run(w, 2);
        WC(w.sticks[0].pos.x - x0 > 1, TextFormat("the leviathan's lure pulls (%.1f m in 2 s)", w.sticks[0].pos.x - x0));
        Stage m = Box(WD_VOID); Add(m, PK_WORM, 2, 2, 36, 1);
        World wm = Put(m, {{F(15), F(2)}});
        float dw = RunUntilDead(wm, 0, 3);
        WC(dw > 0.6f && wm.sticks[0].cause == "the sand worm", TextFormat("the sand worm bursts up under a stick after the dust (%.1f s)", dw));
    }
    // ---- the Salon
    {
        Stage s = Box(WD_SALON); Piece& crowd = Add(s, PK_CROWD, 0, 15, 40, 2); crowd.period = 0.6f;
        World w = Put(s, {{F(12), F(2)}, {F(26), F(2)}});
        Run(w, 10);
        WC(w.sticks[0].hp + w.sticks[1].hp < 200, TextFormat("the crowd throws bottles (%.0f HP lost between two sticks in 10 s)", 200 - w.sticks[0].hp - w.sticks[1].hp));
        Stage d = Box(WD_SALON); Add(d, PK_DART, 2, 2, 1, 2);
        World wd = Put(d, {{F(15), F(2)}});
        Run(wd, 1.5f);
        WC(wd.sticks[0].hp <= 80, TextFormat("the dartboard throws darts (%.0f HP left)", wd.sticks[0].hp));
        Stage pl = Box(WD_SALON); Add(pl, PK_POOL, 5, 2, 16, 1);
        World wp = Put(pl, {{F(14), F(3)}});
        Run(wp, 2);
        WC(wp.sticks[0].hp <= 90, TextFormat("the pool table's balls (%.0f HP left)", wp.sticks[0].hp));
        Stage b = Box(WD_SALON); Fill(b, 10, 2, 20, 3, T_BAR); Add(b, PK_BOUNCER, 22, 2, 1, 1);
        World wb = Put(b, {{F(15), F(4)}});
        Run(wb, 1.2f);
        WC(wb.sticks[0].hp <= 80 && wb.sticks[0].lastHitT > 0, TextFormat("the bouncer throws out anyone on the bar (%.0f HP left)", wb.sticks[0].hp));
        Stage g = Box(WD_SALON); Add(g, PK_DOG, 5, 2, 25, 1);
        World wg = Put(g, {{F(20), F(2)}});
        Run(wg, 4);
        WC(wg.sticks[0].hp <= 85, TextFormat("the dog bites (%.0f HP left)", wg.sticks[0].hp));
    }
    // ---- the gate: every world's wall closes in, and every finale's set piece wakes at its second and works
    {
        static const char* WANT[WD_COUNT] = {"the wall", "the wall", "drowned", "drowned", "the wall", "the wall"};
        std::string got;
        bool all = true;
        for (int wd = 0; wd < WD_COUNT; wd++) {
            Stage s = Box(wd);
            World w = Put(s, {{F(20), F(2)}}, true);
            Run(w, 44);
            bool before = w.sticks[0].alive;
            float dt = RunUntilDead(w, 0, 45);
            bool ok = before && dt > 0 && w.sticks[0].cause == WANT[wd];
            all = all && ok;
            got += TextFormat("%s%s %.0f s", got.empty() ? "" : "; ", WorldName(wd), dt);
            if (!ok) got += " (" + w.sticks[0].cause + ")";
        }
        WC(all, "each world's wall closes in after 45 s (" + got + ")");
    }
    {
        struct SP { int world, kind; const char* what; };
        static const SP SETS[WD_COUNT] = {{WD_NAUTILUS, PK_PROPELLER, "the propeller starts"}, {WD_CAVE, PK_CLAW, "the Lobster rises"}, {WD_REEF, PK_KRAKEN, "the kraken reaches up from the drop-off"},
                                          {WD_ATLANTIS, PK_GRATE, "the Wyrm surfaces in the plaza"}, {WD_VOID, PK_LURE, "the leviathan's lure appears"}, {WD_SALON, PK_CROWD, "closing time: the whole bar thrown in"}};
        std::string got; bool all = true;
        for (const auto& sp : SETS) {
            Stage s = Box(sp.world);
            Piece* p = nullptr;
            Vector2 at{F(15), F(2)};
            switch (sp.kind) {
                case PK_PROPELLER: p = &Add(s, PK_PROPELLER, 18, 2, 2, 3); at = {F(16.5f), F(2)}; break;
                case PK_CLAW: p = &Add(s, PK_CLAW, 2, 2, 36, 4); break;
                case PK_KRAKEN: p = &Add(s, PK_KRAKEN, 2, 2, 36, 2); break;
                case PK_GRATE: p = &Add(s, PK_GRATE, 14, 1, 2, 1); break;
                case PK_LURE: { p = &Add(s, PK_LURE, 24, 2, 1, 1); p->travel = 12; p->power = 9; Fill(s, 22, 0, 26, 1, T_EMPTY); break; }
                case PK_CROWD: { p = &Add(s, PK_CROWD, 0, 15, 40, 2); p->period = 0.15f; p->power = 25; break; }
            }
            p->start = 5;
            World w = Put(s, {at});
            Run(w, 4.9f);
            bool asleep = w.sticks[0].alive && w.sticks[0].hp == 100;
            float dt = RunUntilDead(w, 0, 20);
            bool ok = asleep && dt > 5;
            all = all && ok;
            got += TextFormat("%s%s %.1f s", got.empty() ? "" : "; ", sp.what, dt);
            if (!ok) got += asleep ? " (never killed)" : " (woke early)";
        }
        WC(all, "each world's finale set piece sleeps until its second, then kills (" + got + ")");
    }
    printf(WF ? "  stage 5: %d FAILED\n" : "  stage 5: all checks passed\n", WF);
    return WF;
}

} // namespace sf
