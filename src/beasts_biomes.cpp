// ============================================================================
//  DEPTH - the biomes' living creatures (ParkourReference1.2.pdf, "Biomes and Entity Behaviour"): each biome's
//  species table, food web, and the few behaviours the shared engine (beasts.cpp) can't express on its own.
//  The Hull's are in beasts.cpp, where the engine was first proven.
// ============================================================================
#include "beasts_internal.h"

namespace bk {

// Shared by every biome: a pack or school placed around one spot.
static void Pack(BeastWorld& W, int species, Vector2 at, int n, int school, float spreadX, float spreadY, unsigned salt) {
    for (int k = 0; k < n; k++) {
        int b = NewBeast(W, species, Add(at, Vector2{(Hash(W.seed, salt + k) - 0.5f) * spreadX, (Hash(W.seed, salt + 50 + k) - 0.5f) * spreadY}));
        W.beasts[b].school = school;
    }
}
// Something on the floor at a den if there is one near, else on a floor spot.
static Vector2 NearDenFloor(const BeastWorld& W, const Spots& sp, float u, float radius) {
    Vector2 f = sp.At(u);
    int d = NearestDen(W, {f.x, f.y * TILE}, 12 * TILE);
    if (d >= 0) return {W.dens[d].pos.x + (Hash(W.seed, (unsigned)(u * 9973)) - 0.5f) * 40, W.dens[d].pos.y - radius * 0.6f - 1};
    return sp.Stand(f, radius);
}
static void Hunted(BeastWorld& W, int i, int target, int tid) { // force a hunt without waiting for the next think
    Beast& b = W.beasts[i];
    if (b.act == BeastAct::Coil || b.act == BeastAct::Strike || b.act == BeastAct::Eat) return;
    b.act = BeastAct::Hunt; b.target = target; b.targetId = tid; b.thinkT = 0.5f;
}

// ============================================================ the Pirate Ship
// ECOSYSTEM_BESTIARY.md, "The Pirate Ship": rats run the holds in packs and pick over whatever dies; the ship's
// cats stalk and pounce on them; barn owls come down silently out of the rigging for a rat in the open; gulls
// wheel over the decks in a flock, mob an owl, and snatch a meal from whoever's eating; a lone albatross glides
// the length of the fleet. Gunpowder monkeys, frightened by gunfire, drop a lit powder keg and bolt - the blast
// kills what's small, knocks everything else flat and shakes the fleas out of the guard dogs' coats; fleas that
// find their way back onto a dog drive it berserk, and a berserk dog is the one creature here that can kill
// you. The guard dogs otherwise nose after the diver's scent and shove - which, on a deck over the sea, is enough.
//            name              move            mass  rad  speed sprint accel sight  fov   hear smell hunger  dFear dPrey lethal social band pop  den   scav   traits
const SpeciesDef PIRATE[PS_COUNT] = {
    {"Bilge Rat",        MoveMode::Walk,   0.4f,  6,   60, 170,  900,  150, 2.6f, 0.8f, 0.9f, 0.020f, 0.7f,  0.0f, false, true,  0, 10, 0.8f, 1.0f, T_MOBBER | T_HOST},
    {"Ship's Cat",       MoveMode::Walk,   4.0f,  9,   55, 260, 1200,  220, 1.8f, 0.9f, 0.6f, 0.018f, 0.45f, 0.0f, false, false, 0,  2, 0.5f, 0.3f, T_STRIKER | T_HOST, 80},
    {"Powder Monkey",    MoveMode::Walk,   3.0f,  8,   70, 220, 1000,  170, 2.4f, 0.8f, 0.5f, 0.015f, 0.6f,  0.0f, false, true,  0,  3, 0.6f, 0.6f, T_KLEPTO},
    {"Guard Dog",        MoveMode::Walk,  12.0f, 11,   60, 250, 1100,  200, 2.0f, 0.9f, 1.0f, 0.020f, 0.25f, 0.3f, true,  false, 0,  2, 0.6f, 0.5f, T_STRIKER | T_HOST, 70},
    {"Flea Swarm",       MoveMode::Fly,    0.02f, 4,   30,  90,  400,   60, PI,   0.3f, 0.9f, 0.020f, 0.1f,  0.0f, false, false, 1,  2, 0.0f, 0.0f, T_PARASITE},
    {"Barn Owl",         MoveMode::Fly,    2.0f,  9,   60, 300,  900,  280, 1.4f, 1.0f, 0.3f, 0.020f, 0.35f, 0.0f, false, false, 4,  2, 0.7f, 0.2f, T_STRIKER | T_ECHO, 150},
    {"Gull",             MoveMode::Fly,    0.8f,  7,   80, 240,  700,  240, 2.6f, 0.5f, 0.5f, 0.020f, 0.6f,  0.0f, false, true,  5,  6, 0.2f, 1.0f, T_MOBBER | T_KLEPTO},
    {"Albatross",        MoveMode::Fly,    9.0f, 12,   70, 160,  250,  300, 2.6f, 0.4f, 0.6f, 0.006f, 0.3f,  0.0f, false, false, 9,  1, 0.0f, 0.4f, 0},
};
const FoodEdge PIRATE_WEB[] = {
    {PS_CAT, PS_RAT, 1.0f},  {PS_CAT, PS_GULL, 0.3f},
    {PS_OWL, PS_RAT, 1.0f},  {PS_OWL, PS_MONKEY, 0.25f},
    {PS_DOG, PS_RAT, 0.6f},  {PS_DOG, PS_CAT, 0.2f},
};
constexpr int KEG = 0; // BeastProp kind: a lit powder keg

static void SpawnPirate(BeastWorld& W, PlatformState& p) {
    Spots sp; sp.Build(W, p, 12, W.nav.w - 8);
    if (sp.floor.empty()) return;
    unsigned s = W.seed;
    for (int k = 0; k < 3; k++) Pack(W, PS_RAT, NearDenFloor(W, sp, 0.1f + 0.3f * k + Hash(s, 10 + k) * 0.15f, 6), 3 + (k % 2), 20 + k, 40, 0, 100 + k * 10);
    for (int k = 0; k < 2; k++) NewBeast(W, PS_CAT, sp.Stand(sp.At(0.2f + 0.45f * k + Hash(s, 20 + k) * 0.2f), 9));
    Pack(W, PS_MONKEY, sp.Stand(sp.At(0.35f + Hash(s, 30) * 0.3f), 8), 3, 30, 60, 0, 300);
    for (int k = 0; k < 2; k++) {
        int d = NewBeast(W, PS_DOG, NearDenFloor(W, sp, 0.15f + 0.5f * k + Hash(s, 40 + k) * 0.2f, 11));
        int fl = NewBeast(W, PS_FLEA, W.beasts[d].pos);
        W.beasts[fl].latched = d; W.beasts[fl].targetId = W.beasts[d].id; W.beasts[fl].act = BeastAct::Latched;
    }
    for (int k = 0; k < 2; k++) NewBeast(W, PS_OWL, sp.Above(W, sp.At(0.25f + 0.5f * k), 5));
    Pack(W, PS_GULL, sp.Above(W, sp.At(0.5f + Hash(s, 60) * 0.3f), 6), 6, 40, 90, 40, 600);
    NewBeast(W, PS_ALBATROSS, sp.Above(W, sp.At(0.6f), 9));
}

// A powder keg goes off: the small die, everything else is knocked flat, and fleas are shaken loose.
static void Blast(BeastWorld& W, PlatformState& p, Vector2 at) {
    AddCloud(W, at, 64, 1.8f, 2);
    W.sounds.push_back({at, 1.4f, 0.6f, -1});
    if (!p.verifying) { PlatBurst(p, at, 26, Color{255, 170, 60, 255}, 220, 0.5f, 3); PlatBurst(p, at, 14, Color{60, 50, 45, 255}, 120, 1.0f, 4); }
    for (int j = 0; j < (int)W.beasts.size(); j++) {
        Beast& o = W.beasts[j];
        if (!Alive(o) || o.hidden) continue;
        float d = Dist(o.pos, at);
        if (d > 90) continue;
        if (o.species == PS_FLEA) { o.latched = -1; o.act = BeastAct::Drift; o.actT = 0; o.special = 1; o.vel = Mul(Norm(Sub(o.pos, at)), 160); continue; } // scattered - and angry
        if (d < 50 && o.mass < 5) { Kill(W, p, j, -1); continue; }
        o.vel = Add(o.vel, Mul(Norm(Sub(o.pos, at)), 380 * (1 - d / 90)));
        o.vel.y -= 220;
        Remember(o, MEM_THREAT, -1, 0, at, {0, 0}, 1.0f, W.time);
    }
    Diver dv = SeeDiver(p);
    if (dv.alive && Dist(dv.pos, at) < 80) { p.vel.x += (dv.pos.x > at.x ? 1 : -1) * 320; p.vel.y = std::min(p.vel.y, -260.0f); } // it knocks the diver about too - not fatally
}

static void PirateTick(BeastWorld& W, PlatformState& p, float dt) {
    for (auto& k : W.props) {
        if (k.kind != KEG) continue;
        k.t -= dt;
        k.vel.y = std::min(k.vel.y + WALK_G * dt, 600.0f);
        bool g;
        Collide(W.nav, k.pos, k.vel, 6, 6, dt, g);
        if (g) k.vel.x *= 0.8f;
        if (k.t <= 0) Blast(W, p, k.pos);
    }
    W.props.erase(std::remove_if(W.props.begin(), W.props.end(), [](const BeastProp& k) { return k.kind == KEG && k.t <= 0; }), W.props.end());
}

static void PirateHooks(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    switch (b.species) {
    case PS_MONKEY: { // gunfire close by: it drops the lit keg it was carrying and bolts
        if (b.cooldown > 0) break;
        bool scared = false; Vector2 from = b.pos;
        for (const auto& s : p.shots) if ((s.kind == 0 || s.kind == 5 || s.kind == 2) && Dist(s.pos, b.pos) < 80) { scared = true; from = s.pos; }
        for (const auto& s : W.sounds) if (s.intensity >= 0.9f && s.source == -1 && Dist(s.pos, b.pos) < 100) { scared = true; from = s.pos; }
        if (scared && b.pers.bravery < 0.75f) {
            W.props.push_back({b.pos, {b.facing * 60, -120}, 1.1f + 0.4f * b.pers.intelligence, KEG, i});
            b.cooldown = 12;
            Remember(b, MEM_THREAT, -1, 0, from, {0, 0}, 1.0f, W.time);
            b.act = BeastAct::Flee; b.goal = FleeTarget(W, i, from); b.actT = 0; b.thinkT = 1.0f;
            W.sounds.push_back({b.pos, 0.5f, 0.3f, i}); // the shriek
        }
        break;
    }
    case PS_FLEA: // scattered fleas that land back on a dog drive it berserk
        if (b.latched >= 0 && b.special > 0) {
            Beast& h = W.beasts[b.latched];
            if (h.species == PS_DOG && Alive(h)) { h.special = 7.0f; W.sounds.push_back({h.pos, 0.8f, 0.5f, b.latched}); }
            b.special = 0;
        }
        break;
    case PS_DOG:
        if (b.special > 0) { // berserk: it goes for the diver if it can see or smell them, else anything alive
            b.special -= dt;
            b.hunger = 1; b.fear = 0;
            Diver dv = SeeDiver(p);
            if (dv.alive && Dist(dv.pos, b.pos) < 14 * TILE) Hunted(W, i, BEAST_DIVER, 0);
            else {
                int best = -1; float bd = 10 * TILE;
                for (int j = 0; j < (int)W.beasts.size(); j++) if (j != i && Alive(W.beasts[j]) && !W.beasts[j].hidden && W.beasts[j].species != PS_FLEA && Dist(W.beasts[j].pos, b.pos) < bd) { bd = Dist(W.beasts[j].pos, b.pos); best = j; }
                if (best >= 0) Hunted(W, i, best, W.beasts[best].id);
            }
        }
        break;
    default: break;
    }
}
static bool PirateLethal(const BeastWorld& W, const Beast& b) {
    (void)W;
    return b.species == PS_DOG && b.special > 0; // only a berserk dog kills
}
const BiomeDef& PirateBiome() {
    static const BiomeDef B = [] {
        BiomeDef d;
        d.level = PL_PIRATE; d.species = PIRATE; d.count = PS_COUNT; d.web = PIRATE_WEB; d.webN = (int)(sizeof(PIRATE_WEB) / sizeof(PIRATE_WEB[0]));
        d.water = false; d.clarity = 0.7f; d.daylight = 0.5f; d.arenaLimit = true;
        d.spawn = SpawnPirate; d.hooks = PirateHooks; d.tick = PirateTick; d.lethal = PirateLethal;
        return d;
    }();
    return B;
}

// ============================================================ test benches (depth.exe --verify-<biome>-ecosystem)
// A synthetic box of floor and walls in a biome's world, with nobody in it yet: the diver parked far away.
static void Bench(PlatformState& p, int level, int w, int h) {
    p = PlatformState{};
    p.level = level;
    p.w = w; p.h = h;
    p.tiles.assign(h, std::string(w, '.'));
    for (int x = 0; x < w; x++) { p.tiles[h - 1][x] = '#'; p.tiles[h - 2][x] = '#'; p.tiles[0][x] = '#'; }
    for (int y = 0; y < h; y++) { p.tiles[y][0] = '#'; p.tiles[y][w - 1] = '#'; }
    p.pos = {-5000, -5000};
    p.deathTimer = 1;
}
static void BenchBuild(PlatformState& p) {
    BeastWorld& W = p.fauna;
    W = BeastWorld{};
    W.biome = p.level; W.active = true; W.seed = 777; W.rng = 4243;
    W.nav.w = p.w; W.nav.h = p.h;
    W.nav.solid.assign((size_t)p.w * p.h, 0); W.nav.hazard.assign((size_t)p.w * p.h, 0);
    for (int y = 0; y < p.h; y++) for (int x = 0; x < p.w; x++) {
        W.nav.solid[y * p.w + x] = PlatSolid(p, x, y) ? 1 : 0;
        if (p.tiles[y][x] == 'D') { Den d; d.tx = x; d.ty = y; d.pos = {x * TILE + 16.0f, y * TILE + 0.0f}; W.dens.push_back(d); }
        if (p.tiles[y][x] == 'o') W.lamps.push_back({x * TILE + 16.0f, y * TILE + 16.0f});
    }
    W.scent.Init(p.w, p.h, TILE);
}
static int Put(BeastWorld& W, int species, Vector2 at) { int i = NewBeast(W, species, at); W.beasts[i].pers.abnormal = Abnormal::None; return i; }
static bool Sane(const PlatformState& p) {
    for (const auto& b : p.fauna.beasts) {
        if (b.life == BeastLife::Gone) continue;
        if (std::isnan(b.pos.x) || std::isnan(b.pos.y) || b.pos.x < -50 || b.pos.y < -50 || b.pos.x > p.w * TILE + 50 || b.pos.y > p.h * TILE + 50) return false;
    }
    return true;
}
// A real generated level of this biome: everything spawns, and a minute of its life stays sane.
static bool RealLevel(int level, const char* tag, int minSpecies, int minDens, unsigned seed) {
    PlatformState p;
    p.level = level; p.bossEnabled = false; p.layout = {(int)seed, 100};
    PlatBuildLevel(p);
    p.deathTimer = 1;
    const BeastWorld& W = p.fauna;
    const BiomeDef* B = Biome(level);
    std::vector<int> kinds(B->count, 0);
    for (const auto& b : W.beasts) if (b.life == BeastLife::Alive) kinds[b.species]++;
    int distinct = 0;
    for (int k : kinds) distinct += k > 0;
    TraceLog(LOG_WARNING, "verify-%s: a real level has %d dens, %d beasts of %d species (%d abnormal)", tag, (int)W.dens.size(), (int)W.beasts.size(), distinct, W.abnormals);
    bool ok = true;
    if ((int)W.dens.size() < minDens) { TraceLog(LOG_WARNING, "verify-%s: FAILED - only %d dens", tag, (int)W.dens.size()); ok = false; }
    if (distinct < minSpecies) { TraceLog(LOG_WARNING, "verify-%s: FAILED - only %d species spawned", tag, distinct); ok = false; }
    for (int f = 0; f < 60 * 60; f++) {
        BeastsUpdate(p, 1 / 60.0f);
        if (getenv("DEPTH_BEASTLOG") && f % 600 == 599) {
            std::string line;
            for (int s = 0; s < B->count; s++) {
                int acts[32] = {0}, n = 0, hid = 0;
                for (const auto& b : W.beasts) if (b.species == s && b.life == BeastLife::Alive) { acts[(int)b.act]++; n++; hid += b.hidden; }
                if (!n) continue;
                line += std::string(" | ") + B->species[s].name + " " + std::to_string(n) + ":";
                for (int a = 0; a < 32; a++) if (acts[a]) line += std::string(" ") + BeastActName((BeastAct)a) + "=" + std::to_string(acts[a]);
                if (hid) line += " (in den " + std::to_string(hid) + ")";
            }
            TraceLog(LOG_WARNING, "  t=%ds kills=%d cleared=%d%s", (f + 1) / 60, W.kills, W.scavenged, line.c_str());
        }
    }
    if (!Sane(p)) { TraceLog(LOG_WARNING, "verify-%s: FAILED - a minute of life blew up a position", tag); ok = false; }
    TraceLog(LOG_WARNING, "verify-%s: a minute of life - %d kills (%d by beasts, %d hazards, %d drowned, %d shot), %d bodies cleared, %d births, %d den visits", tag, W.kills, W.deaths[0], W.deaths[1], W.deaths[2], W.deaths[3], W.scavenged, W.births, W.hides);
    return ok;
}

static bool VerifyPirate() {
    bool ok = true;
    auto fail = [&](const char* what) { TraceLog(LOG_WARNING, "verify-pirate-ecosystem: FAILED - %s", what); ok = false; };
    ok &= RealLevel(PL_PIRATE, "pirate-ecosystem", 6, 3, 9);
    // 1) a hungry cat stalks, pounces on and eats a rat; the rat's pack-mates scavenge nothing (the cat's) - just check the kill
    {
        PlatformState p; Bench(p, PL_PIRATE, 50, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int cat = Put(W, PS_CAT, {10 * TILE, 11 * TILE + 20}); W.beasts[cat].hunger = 1; W.beasts[cat].pers.aggression = 0.9f; W.beasts[cat].pers.intelligence = 0.8f;
        int rat = Put(W, PS_RAT, {17 * TILE, 11 * TILE + 24}); W.beasts[rat].pers.bravery = 0.5f;
        bool killed = false, pounced = false;
        for (int f = 0; f < 60 * 45 && !killed; f++) {
            BeastsUpdate(p, 1 / 60.0f);
            if (W.beasts[cat].act == BeastAct::Coil || W.beasts[cat].act == BeastAct::Strike) pounced = true;
            if (W.beasts[rat].life != BeastLife::Alive) killed = true;
            if (getenv("DEPTH_BEASTLOG") && f % 60 == 0) { const Beast& c = W.beasts[cat]; const Beast& r = W.beasts[rat];
                TraceLog(LOG_WARNING, "  cat t=%d %s (%.0f,%.0f) v%.0f tgt %d | rat %s (%.0f,%.0f) fear %.2f", f / 60, BeastActName(c.act), c.pos.x, c.pos.y, c.vel.x, c.target, BeastActName(r.act), r.pos.x, r.pos.y, r.fear); }
        }
        if (!pounced) fail("the cat never crouched and pounced");
        if (!killed) fail("a hungry cat never caught a rat on open deck");
        if (!Sane(p)) fail("positions blew up in the cat test");
    }
    // 2) the chain: gunfire by a monkey -> it drops a lit keg and bolts -> the blast shakes the fleas off a dog ->
    // the fleas find their way back onto it -> the dog goes berserk, and a berserk dog kills
    {
        PlatformState p; Bench(p, PL_PIRATE, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int monkey = Put(W, PS_MONKEY, {20 * TILE, 11 * TILE + 26}); W.beasts[monkey].pers.bravery = 0.1f;
        int dog = Put(W, PS_DOG, {22 * TILE, 11 * TILE + 24}); W.beasts[dog].hunger = 0; W.beasts[dog].pers.wanderlust = 0;
        int flea = Put(W, PS_FLEA, W.beasts[dog].pos); W.beasts[flea].latched = dog; W.beasts[flea].targetId = W.beasts[dog].id; W.beasts[flea].act = BeastAct::Latched;
        p.shots.push_back({{20 * TILE, 11 * TILE + 10}, {0, 0}, 5.0f, 0}); // a musket ball going past the monkey
        bool keg = false, blasted = false, shaken = false, berserk = false;
        for (int f = 0; f < 60 * 40 && !berserk; f++) {
            BeastsUpdate(p, 1 / 60.0f);
            if (f == 5) p.shots.clear();
            if (!W.props.empty()) keg = true;
            if (keg && W.props.empty()) blasted = true;
            if (W.beasts[flea].act == BeastAct::Drift) shaken = true;
            if (W.beasts[dog].special > 0) berserk = true;
            if (getenv("DEPTH_BEASTLOG") && f % 60 == 0) { const Beast& d = W.beasts[dog]; const Beast& fl = W.beasts[flea];
                TraceLog(LOG_WARNING, "  chain t=%d props %d | dog %s (%.0f,%.0f) life %d | flea %s (%.0f,%.0f) latched %d special %.1f", f / 60, (int)W.props.size(), BeastActName(d.act), d.pos.x, d.pos.y, (int)d.life, BeastActName(fl.act), fl.pos.x, fl.pos.y, fl.latched, fl.special); }
        }
        if (!keg) fail("gunfire by a monkey never made it drop a lit keg");
        if (!blasted) fail("the keg never went off");
        if (!shaken) fail("the blast never shook the fleas off the dog");
        if (!berserk) fail("the fleas never got back onto the dog and drove it berserk");
        if (berserk) {
            Beast& d = W.beasts[dog];
            if (!BeastLethalNow(p, d)) fail("a berserk dog isn't lethal to touch");
            Rectangle diver{d.pos.x - 8, d.pos.y - 16, 16, 28};
            if (!BeastsTouchDiver(p, diver)) fail("the diver standing on a berserk dog isn't killed");
        }
    }
    // 3) a gull steals a rat carcass out from under the cat eating it
    {
        PlatformState p; Bench(p, PL_PIRATE, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int body = SpawnCorpse(W, PS_RAT, {20 * TILE, 11 * TILE + 26}, 1.0f);
        int cat = Put(W, PS_CAT, {20 * TILE + 10, 11 * TILE + 20}); W.beasts[cat].act = BeastAct::Eat; W.beasts[cat].target = body; W.beasts[cat].targetId = W.beasts[body].id; W.beasts[cat].hunger = 0.9f;
        int gull = Put(W, PS_GULL, {14 * TILE, 7 * TILE}); W.beasts[gull].hunger = 1; W.beasts[gull].pers.bravery = 0.9f;
        bool stole = false;
        for (int f = 0; f < 60 * 20 && !stole; f++) { BeastsUpdate(p, 1 / 60.0f); if (W.beasts[gull].act == BeastAct::Eat) stole = true; }
        if (!stole) fail("a hungry gull never snatched the cat's meal");
    }
    if (ok) TraceLog(LOG_WARNING, "verify-pirate-ecosystem: OK - stalk/pounce, gunfire/keg/blast/fleas/berserk, and gull theft all confirmed");
    return ok;
}

// ============================================================ not yet moved over (their old overlays still run)
const BiomeDef& IslandBiome() { static const BiomeDef B; return B; }
const BiomeDef& CaveBiome() { static const BiomeDef B; return B; }
const BiomeDef& PipesBiome() { static const BiomeDef B; return B; }
}  // namespace bk

bool VerifyBeastBiome(int level) {
    switch (level) {
    case PL_PIRATE: return bk::VerifyPirate();
    default: return false;
    }
}
