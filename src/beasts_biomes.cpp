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
    {"Ship's Cat",       MoveMode::Walk,   4.0f,  9,   55, 260, 1200,  220, 1.8f, 0.9f, 0.6f, 0.018f, 0.45f, 0.0f, false, false, 0,  2, 0.5f, 0.3f, T_STRIKER | T_HOST | T_CARRY, 80},
    {"Powder Monkey",    MoveMode::Walk,   3.0f,  8,   70, 220, 1000,  170, 2.4f, 0.8f, 0.5f, 0.015f, 0.6f,  0.0f, false, true,  0,  3, 0.6f, 0.6f, T_KLEPTO},
    {"Guard Dog",        MoveMode::Walk,  12.0f, 11,   60, 250, 1100,  200, 2.0f, 0.9f, 1.0f, 0.020f, 0.25f, 0.3f, true,  false, 0,  2, 0.6f, 0.5f, T_STRIKER | T_HOST | T_CARRY, 70},
    {"Flea Swarm",       MoveMode::Fly,    0.02f, 4,   30,  90,  400,   60, PI,   0.3f, 0.9f, 0.020f, 0.1f,  0.0f, false, false, 1,  2, 0.0f, 0.0f, T_PARASITE},
    {"Barn Owl",         MoveMode::Fly,    2.0f,  9,   60, 300,  900,  380, 1.4f, 1.0f, 0.3f, 0.020f, 0.35f, 0.0f, false, false, 4,  2, 0.7f, 0.2f, T_STRIKER | T_ECHO | T_CARRY, 150},
    {"Gull",             MoveMode::Fly,    0.8f,  7,   80, 240,  700,  240, 2.6f, 0.5f, 0.5f, 0.020f, 0.6f,  0.0f, false, true,  5,  6, 0.2f, 1.0f, T_MOBBER | T_KLEPTO},
    {"Albatross",        MoveMode::Fly,    9.0f, 12,   70, 160,  250,  300, 2.6f, 0.4f, 0.6f, 0.006f, 0.3f,  0.0f, false, false, 9,  1, 0.0f, 0.4f, 0},
    // ParkourReference1.3 (beasts_pirate.cpp), adapted to the above-water fleet
    {"Grand Kraken",     MoveMode::Swim, 5000.0f, 40,  60, 120,  100,  600, PI,   1.0f, 1.0f, 0.000f, 0.0f,  0.5f, false, false, 0,  0, 0.0f, 0.0f, T_GIANT},
    {"Timber-Shell Tortoise", MoveMode::Walk, 300.0f, 16, 22, 30, 200, 160, 2.0f, 0.4f, 0.3f, 0.004f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_GIANT},
    {"Rigging-Mimic Cuttlefish", MoveMode::Climb, 6.0f, 10, 40, 300, 900, 260, PI, 0.8f, 0.5f, 0.020f, 0.0f,  0.8f, false, false, 0,  0, 0.0f, 0.2f, T_GIANT | T_CAMO},
    {"Cannoneer Mantis Shrimp", MoveMode::Walk, 2.0f, 8,  0,   0,    0,  320, 1.2f, 0.5f, 0.3f, 0.010f, 0.0f,  0.6f, false, false, 0,  0, 0.0f, 0.0f, T_GIANT},
    {"Wood-Borer Worms",  MoveMode::Sessile, 0.1f, 10,   0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
    {"Copper-Scale Moths", MoveMode::Fly,   0.05f, 4,   40, 200,  900,  120, PI,   0.6f, 0.3f, 0.004f, 0.9f,  0.0f, false, true,  2,  0, 0.3f, 0.0f, T_FLASH},
    {"Cannon-Moss",      MoveMode::Sessile, 0.5f, 14,    0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
    {"Ship-Rot Fungus",  MoveMode::Sessile, 0.5f, 10,    0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
    {"Mast-Kelp",        MoveMode::Sessile, 1.0f, 10,    0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
    {"Barnacle-Cluster", MoveMode::Sessile, 2.0f, 10,    0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
    {"Siren's Lantern Weed", MoveMode::Sessile, 0.5f, 9,  0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
};
const FoodEdge PIRATE_WEB[] = {
    {PS_CAT, PS_RAT, 1.0f},  {PS_CAT, PS_GULL, 0.3f},
    {PS_OWL, PS_RAT, 1.0f},  {PS_OWL, PS_MONKEY, 0.25f},
    {PS_DOG, PS_RAT, 0.6f},  {PS_DOG, PS_CAT, 0.2f},
    {PS_KRAKEN, PS_DOG, 0.3f}, {PS_KRAKEN, PS_CAT, 0.2f}, {PS_KRAKEN, PS_MONKEY, 0.2f}, // everything on deck knows what the dark under the hulls means
    {PS_KRAKEN, PS_RAT, 0.05f}, {PS_KRAKEN, PS_GULL, 0.05f},
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
    Pirate13Spawn(W, p);
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
    PirateDirector(W, p, dt);
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
    default: Pirate13Hook(W, p, i, dt); break; // the 1.3 roster (beasts_pirate.cpp)
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
        static const float SIZES[PS_COUNT] = {1.0f, 1.3f, 1.3f, 1.7f, 1.0f, 1.5f, 1.2f, 1.8f, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        d.sizes = SIZES;
        d.spawn = SpawnPirate; d.hooks = PirateHooks; d.tick = PirateTick; d.lethal = PirateLethal; d.touch = Pirate13Touch;
        return d;
    }();
    return B;
}

// ============================================================ the Island
// ECOSYSTEM_BESTIARY.md, "The Island": wild boar root about the shrines and, cornered, turn and charge whatever
// pressed them - the diver included; tree snakes lie camouflaged along branches and ledges and drop on frogs,
// bats and lizards; monitor lizards bask and scavenge; fruit bats roost in colonies and burst out at any crash;
// orb spiders hang webs that catch them; coconut crabs cut coconuts loose, and the thud brings gulls to steal
// them; poison dart frogs are left alone by anything that's bitten one before; and the villagers' hunting dogs
// run in pairs, nose down on the diver's trail, and shove - which, on a cliff, is enough.
//            name              move              mass  rad  speed sprint accel sight  fov   hear smell hunger  dFear dPrey lethal social band pop  den   scav  traits
const SpeciesDef ISLAND[IS_COUNT] = {
    {"Wild Boar",        MoveMode::Walk,   60.0f, 13,   50, 290, 1100,  180, 1.8f, 0.9f, 1.0f, 0.012f, 0.5f,  0.0f, true,  false, 0,  3, 0.5f, 0.6f, T_STRIKER | T_CHARGER | T_DEFENSIVE, 150},
    {"Tree Snake",       MoveMode::Climb,   1.5f,  6,   35, 260,  900,  130, 1.5f, 0.4f, 0.9f, 0.015f, 0.4f,  0.0f, true,  false, 0,  3, 0.8f, 0.0f, T_STRIKER | T_CAMO, 70},
    {"Monitor Lizard",   MoveMode::Walk,    8.0f, 10,   40, 200, 1000,  200, 2.0f, 0.5f, 0.9f, 0.010f, 0.6f,  0.0f, false, false, 0,  2, 0.5f, 1.0f, 0},
    {"Fruit Bat",        MoveMode::Fly,     0.5f,  6,   70, 230,  900,  150, PI,   1.0f, 0.5f, 0.015f, 0.8f,  0.0f, false, true,  4, 10, 0.95f, 0.3f, T_ROOST | T_ECHO},
    {"Orb Spider",       MoveMode::Sessile, 0.3f, 12,    0,   0,    0,   40, PI,   0.3f, 0.0f, 0.010f, 0.0f,  0.0f, false, false, 0,  3, 0.0f, 0.0f, T_TRAP},
    {"Coconut Crab",     MoveMode::Walk,    4.0f,  9,   35, 120,  800,  140, 2.2f, 0.6f, 0.9f, 0.015f, 0.6f,  0.0f, false, false, 0,  3, 0.7f, 1.0f, 0},
    {"Dart Frog",        MoveMode::Walk,    0.3f,  5,   40, 160,  900,  120, 2.4f, 0.6f, 0.3f, 0.010f, 0.8f,  0.0f, false, false, 0,  6, 0.6f, 0.0f, T_TOXIC},
    {"Gull",             MoveMode::Fly,     0.8f,  7,   80, 240,  700,  240, 2.6f, 0.5f, 0.5f, 0.020f, 0.6f,  0.0f, false, true,  6,  5, 0.2f, 1.0f, T_MOBBER | T_KLEPTO},
    {"Hunting Dog",      MoveMode::Walk,   20.0f, 11,   70, 300, 1200,  200, 2.0f, 0.9f, 1.0f, 0.018f, 0.2f,  0.5f, false, true,  0,  3, 0.5f, 0.6f, T_STRIKER | T_CARRY, 70},
    {"Coconut",          MoveMode::Sessile, 0.5f,  5,    0,   0,    0,    0, 0,    0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, 0},
    // ParkourReference1.3 (beasts_island.cpp), above water
    {"Goliath Island Beetle", MoveMode::Walk, 300.0f, 16, 20, 30, 200, 120, 2.0f, 0.4f, 0.3f, 0.004f, 0.0f, 0.0f, false, false, 0, 0, 0.0f, 0.0f, T_GIANT},
    {"Mangrove Stalker", MoveMode::Walk,   30.0f, 12,    0,   0,    0,  200, PI,   0.8f, 0.6f, 0.010f, 0.0f,  0.8f, false, false, 0,  0, 0.0f, 0.0f, T_GIANT | T_CAMO},
    {"Totem-Centipede",  MoveMode::Sessile, 200.0f, 14,  0,   0,    0,    0, PI,   1.0f, 0.0f, 0.000f, 0.0f,  0.8f, false, false, 0,  0, 0.0f, 0.0f, T_GIANT},
    {"Arch-Serpent",     MoveMode::Swim, 5000.0f, 40,    0,   0,    0,  600, PI,   1.0f, 1.0f, 0.000f, 0.0f,  0.5f, false, false, 0,  0, 0.0f, 0.0f, T_GIANT},
    {"Tidal Mud-Skipper", MoveMode::Walk,   0.2f,  4,   50, 200,  900,  120, 2.4f, 0.8f, 0.3f, 0.008f, 0.9f,  0.0f, false, true,  0,  8, 0.4f, 0.0f, 0},
    {"Glow-Firefly",     MoveMode::Fly,    0.02f, 3,   30, 120,  600,   80, PI,   0.3f, 0.2f, 0.004f, 0.6f,  0.0f, false, true,  2,  0, 0.0f, 0.0f, 0},
    {"Tribal Drum-Fungus", MoveMode::Sessile, 1.0f, 12,  0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
    {"Poison-Dart Vine", MoveMode::Sessile, 0.3f,  6,    0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
    {"Razor-Palm Roots", MoveMode::Sessile, 1.0f, 14,    0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
    {"Idol's Bloom",     MoveMode::Sessile, 0.2f,  8,    0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
    {"Mangrove Root-Sponge", MoveMode::Sessile, 1.0f, 14, 0,  0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
};
const FoodEdge ISLAND_WEB[] = {
    {IS_SNAKE, IS_FROG, 0.5f},  {IS_SNAKE, IS_BAT, 0.7f},   {IS_SNAKE, IS_LIZARD, 0.2f},
    {IS_LIZARD, IS_FROG, 0.4f}, {IS_LIZARD, IS_CRAB, 0.3f}, {IS_LIZARD, IS_SNAKE, 0.3f},
    {IS_DOG, IS_LIZARD, 0.6f},  {IS_DOG, IS_CRAB, 0.3f},
    {IS_SPIDER, IS_BAT, 0.6f},  {IS_SPIDER, IS_FROG, 0.2f},
    {IS_LIZARD, IS_SKIPPER, 0.6f}, {IS_SNAKE, IS_SKIPPER, 0.5f}, {IS_BAT, IS_FIREFLY, 0.6f},
    {IS_MSTALKER, IS_BOAR, 0.4f}, {IS_MSTALKER, IS_LIZARD, 0.5f}, {IS_MSTALKER, IS_DOG, 0.4f}, {IS_SERPENT, IS_BOAR, 0.3f},
};

static void SpawnIsland(BeastWorld& W, PlatformState& p) {
    Spots sp; sp.Build(W, p, 12, W.nav.w - 8);
    if (sp.floor.empty()) return;
    unsigned s = W.seed;
    for (int k = 0; k < 3; k++) NewBeast(W, IS_BOAR, NearDenFloor(W, sp, 0.12f + 0.3f * k + Hash(s, 10 + k) * 0.15f, 13));
    for (int k = 0; k < 3; k++) { int b = NewBeast(W, IS_SNAKE, sp.Stand(sp.At(0.2f + 0.28f * k + Hash(s, 20 + k) * 0.1f), 6)); W.beasts[b].special = 1; }
    for (int k = 0; k < 2; k++) NewBeast(W, IS_LIZARD, sp.Stand(sp.At(0.3f + 0.4f * k), 10));
    for (int k = 0; k < 2; k++) Pack(W, IS_BAT, sp.Above(W, sp.At(0.25f + 0.5f * k + Hash(s, 30 + k) * 0.1f), 5), 5, 50 + k, 50, 20, 300 + k * 10);
    for (int k = 0; k < 3; k++) NewBeast(W, IS_SPIDER, sp.Above(W, sp.At(0.18f + 0.3f * k + Hash(s, 40 + k) * 0.08f), 3));
    for (int k = 0; k < 3; k++) NewBeast(W, IS_CRAB, NearDenFloor(W, sp, 0.1f + 0.33f * k + Hash(s, 50 + k) * 0.1f, 9));
    for (int k = 0; k < 6; k++) NewBeast(W, IS_FROG, sp.Stand(sp.At(0.08f + 0.15f * k + Hash(s, 60 + k) * 0.05f), 5));
    Pack(W, IS_GULL, sp.Above(W, sp.At(0.55f), 7), 5, 60, 90, 40, 700);
    Pack(W, IS_DOG, sp.Stand(sp.At(0.45f + Hash(s, 70) * 0.2f), 11), 2, 70, 30, 0, 800);
    Island13Spawn(W, p);
}

static void IslandHooks(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    switch (b.species) {
    case IS_SNAKE: { // camouflage along its branch while it keeps still
        bool still = Len(b.vel) < 20;
        b.special = Clamp01(b.special + (still ? 0.3f : -0.9f) * dt);
        break;
    }
    case IS_FROG: // it hops rather than walks
        if (b.grounded && fabsf(b.vel.x) > 10 && b.hopT <= 0) { b.vel.y = -260 - 120 * b.pers.energy; b.vel.x *= 1.6f; b.hopT = 0.5f; }
        break;
    case IS_CRAB: // now and then, calm and not too full, it climbs and cuts a coconut down - thud
        b.special -= dt;
        if (b.special <= 0 && b.fear < 0.2f && (b.act == BeastAct::Wander || b.act == BeastAct::Idle)) {
            b.special = 14 + 12 * Hash((unsigned)b.id, (unsigned)W.time);
            Vector2 at{b.pos.x + b.facing * 20, b.pos.y - 3 * TILE};
            while (at.y < b.pos.y && W.nav.Solid((int)floorf(at.x / TILE), (int)floorf(at.y / TILE))) at.y += TILE / 2; // from the lowest open bough
            int c = SpawnCorpse(W, IS_COCONUT, at, 1.0f);
            W.beasts[c].vel = {b.facing * 30, 0};
            W.sounds.push_back({b.pos, 0.55f, 0.4f, -1});
            Remember(b, MEM_FOOD, c, W.beasts[c].id, W.beasts[c].pos, {0, 0}, 1.0f, W.time);
        }
        break;
    default: Island13Hook(W, p, i, dt); break; // the 1.3 roster (beasts_island.cpp)
    }
    (void)p;
}
static bool IslandLethal(const BeastWorld& W, const Beast& b) {
    (void)W;
    if (b.species == IS_BOAR) return b.act == BeastAct::Strike; // a charging boar
    if (b.species == IS_SNAKE) return b.act == BeastAct::Strike; // a striking snake
    return false;
}
const BiomeDef& IslandBiome() {
    static const BiomeDef B = [] {
        BiomeDef d;
        d.level = PL_ISLAND; d.species = ISLAND; d.count = IS_COUNT; d.web = ISLAND_WEB; d.webN = (int)(sizeof(ISLAND_WEB) / sizeof(ISLAND_WEB[0]));
        d.water = false; d.clarity = 0.85f; d.daylight = 0.8f;
        static const float SIZES[IS_COUNT] = {1.7f, 1.4f, 1.4f, 1.0f, 1.1f, 1.3f, 1.0f, 1.2f, 1.5f, 1.0f, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        d.sizes = SIZES;
        d.spawn = SpawnIsland; d.hooks = IslandHooks; d.lethal = IslandLethal; d.touch = Island13Touch; d.tick = Island13Tick;
        return d;
    }();
    return B;
}
// ============================================================ the Cave
// ECOSYSTEM_BESTIARY.md, "The Cave": it's dark here, so light is everything. Cave moths drift toward any glow -
// the diver's lamp, a light pool, a jelly's flash; drifting glow-jellies flash when disturbed, which draws the
// moths in to be stung, and shows everything nearby to whatever is hunting; bats hunt the moths by echo and roost
// in colonies; blind pale salamanders hunt by ear and tongue; fungal beetles, cornered, burst into a spore cloud
// that blinds; cave leeches hang from the ceiling and drop on anything warm that passes under - the diver too,
// and a dropping leech is the one creature here that kills; giant tube worms comb the air with their plumes and
// snap back into their tubes at the slightest tremor.
//            name              move              mass  rad  speed sprint accel sight  fov   hear smell hunger  dFear dPrey lethal social band pop  den   scav  traits
const SpeciesDef CAVE[CS_COUNT] = {
    // the Cave is a flooded cave (the user's correction): its animals are aquatic now - same roles, same behaviours
    {"Cave Cusk-Eel",    MoveMode::Swim,    0.5f,  6,   80, 250,  900,  170, PI,   1.0f, 0.4f, 0.020f, 0.7f,  0.0f, false, true,  4,  8, 0.95f, 0.0f, T_ROOST | T_ECHO | T_CARRY}, // hunts by its lateral line, rests in ceiling crevices
    {"Glow Jelly",       MoveMode::Swim,    0.4f,  7,   14,  45,  120,   80, PI,   0.4f, 0.2f, 0.006f, 0.5f,  0.0f, false, false, 3,  5, 0.0f, 0.0f, T_FLASH},
    {"Blind Olm",        MoveMode::Walk,    2.0f,  9,   35, 190, 1000,   90, 1.6f, 1.0f, 1.0f, 0.016f, 0.5f,  0.0f, false, false, 0,  3, 0.6f, 0.4f, T_STRIKER | T_HOST, 60},
    {"Silt Isopod",      MoveMode::Walk,    0.6f,  6,   25, 110,  700,  100, 2.0f, 0.7f, 0.8f, 0.012f, 0.7f,  0.0f, false, false, 0,  6, 0.6f, 1.0f, 0},
    {"Cave Leech",       MoveMode::Climb,   0.2f,  5,   18,  40,  300,   60, PI,   0.8f, 1.0f, 0.015f, 0.2f,  0.0f, false, false, 0,  5, 0.0f, 0.0f, T_ROOST},
    {"Giant Tube Worm",  MoveMode::Sessile, 3.0f, 10,    0,   0,    0,   50, PI,   0.9f, 0.0f, 0.010f, 0.0f,  0.0f, false, false, 0,  3, 0.0f, 0.0f, T_TRAP},
    {"Glow-Shrimp",      MoveMode::Swim,    0.1f,  4,   45, 130,  600,  160, PI,   0.5f, 0.3f, 0.010f, 0.6f,  0.0f, false, false, 3, 10, 0.2f, 0.0f, T_LIGHTSEEK}, // 1.3's fodder: they swarm round anything that glows
    // ParkourReference1.3 (beasts_cave.cpp)
    {"Blind Cave Loach", MoveMode::Swim,    0.3f,  5,   30, 150,  600,   40, PI,   1.0f, 0.4f, 0.008f, 0.8f,  0.0f, false, true,  1,  8, 0.4f, 0.2f, 0},
    {"Crystal-Shelled Tortoise", MoveMode::Walk, 300.0f, 18, 20, 30, 200, 120, 2.0f, 0.5f, 0.3f, 0.004f, 0.0f, 0.0f, false, false, 0, 0, 0.0f, 0.0f, T_GIANT},
    {"Echo-Stalker",     MoveMode::Walk,   20.0f, 12,   60, 280, 1200,   10, 0.1f, 1.6f, 0.9f, 0.015f, 0.0f,  0.8f, true,  false, 0,  0, 0.0f, 0.4f, T_STRIKER | T_ECHO, 90},
    {"Tremor Worm",      MoveMode::Sessile, 200.0f, 14,  0,   0,    0,    0, PI,   1.0f, 0.0f, 0.000f, 0.0f,  0.8f, false, false, 0,  0, 0.0f, 0.0f, T_GIANT},
    {"Abyssal Arachnid", MoveMode::Climb, 400.0f, 20,   70, 360,  900,  260, PI,   1.0f, 0.6f, 0.010f, 0.0f,  0.8f, false, false, 0,  0, 0.0f, 0.3f, T_GIANT},
    {"Lantern-Shroom",   MoveMode::Sessile, 0.5f,  8,    0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
    {"Acid-Lichen",      MoveMode::Sessile, 0.5f, 10,    0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
    {"Vibration-Spore",  MoveMode::Sessile, 0.5f,  9,    0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
    {"Cave-Cabbage",     MoveMode::Sessile, 1.0f, 14,    0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
    {"Nerve-Root",       MoveMode::Sessile, 0.5f, 10,    0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
};
const FoodEdge CAVE_WEB[] = {
    {CS_CUSK, CS_GSHRIMP, 1.0f},
    {CS_OLM, CS_ISOPOD, 0.7f}, {CS_OLM, CS_GSHRIMP, 0.5f}, {CS_OLM, CS_LEECH, 0.3f},
    {CS_WORM, CS_GSHRIMP, 1.0f}, {CS_WORM, CS_JELLY, 0.4f},
    {CS_JELLY, CS_GSHRIMP, 0.8f},
    {CS_OLM, CS_LOACH, 0.6f}, {CS_CUSK, CS_LOACH, 0.5f}, {CS_STALKER, CS_OLM, 0.6f}, {CS_STALKER, CS_LOACH, 0.4f}, {CS_STALKER, CS_CUSK, 0.3f},
    {CS_ARACHNID, CS_STALKER, 0.6f}, {CS_ARACHNID, CS_OLM, 0.5f},
};

// A ceiling above a floor spot: the underside of the first solid tile going up (for roosting leeches).
static bool CeilingAbove(const BeastWorld& W, Vector2 f, Vector2& out) {
    int x = (int)(f.x / TILE);
    for (int y = (int)f.y - 1; y > 0; y--) if (W.nav.Solid(x, y)) { if ((int)f.y - y < 3) return false; out = {f.x, (y + 1) * TILE + 6.0f}; return true; }
    return false;
}
static void SpawnCave(BeastWorld& W, PlatformState& p) {
    Spots sp; sp.Build(W, p, 12, W.nav.w - 8);
    if (sp.floor.empty()) return;
    unsigned s = W.seed;
    for (int k = 0; k < 2; k++) Pack(W, CS_CUSK, sp.Above(W, sp.At(0.2f + 0.5f * k + Hash(s, 10 + k) * 0.1f), 5), 4, 80 + k, 40, 20, 100 + k * 10);
    for (int k = 0; k < 5; k++) NewBeast(W, CS_JELLY, sp.Above(W, sp.At(0.1f + 0.18f * k + Hash(s, 20 + k) * 0.08f), 3 + (k % 3)));
    for (int k = 0; k < 3; k++) NewBeast(W, CS_OLM, NearDenFloor(W, sp, 0.15f + 0.3f * k + Hash(s, 30 + k) * 0.1f, 9));
    for (int k = 0; k < 6; k++) NewBeast(W, CS_ISOPOD, sp.Stand(sp.At(0.08f + 0.15f * k + Hash(s, 40 + k) * 0.06f), 6));
    for (int k = 0, n = 0; k < 40 && n < 5; k++) {
        Vector2 c;
        if (!CeilingAbove(W, sp.At(Hash(s, 50 + k)), c)) continue;
        int b = NewBeast(W, CS_LEECH, c); W.beasts[b].territory = c; W.beasts[b].act = BeastAct::Ambush; n++;
    }
    for (int k = 0; k < 3; k++) { Vector2 f = sp.At(0.2f + 0.3f * k + Hash(s, 60 + k) * 0.1f); NewBeast(W, CS_WORM, {f.x, (f.y + 1) * TILE - 2}); }
    for (int k = 0; k < 10; k++) NewBeast(W, CS_GSHRIMP, sp.Above(W, sp.At(0.05f + 0.095f * k), 2 + (k % 4)));
    for (auto& b : W.beasts) if (b.species == CS_JELLY || b.species == CS_GSHRIMP) b.territory = b.pos;
    Cave13Spawn(W, p);
}

static void CaveExtras(BeastWorld& W, const PlatformState& p, int i, Choice& best) {
    Beast& b = W.beasts[i];
    if (b.species == CS_GSHRIMP && b.fear < 0.5f) { // the moth to the flame: the brightest light it can see
        Vector2 at{0, 0}; float bright = 0;
        for (const auto& l : W.lights) { float d = Dist(l.pos, b.pos); if (d < 9 * TILE && l.strength / (1 + d / TILE) > bright) { bright = l.strength / (1 + d / TILE); at = l.pos; } }
        for (const auto& l : W.lamps) { float d = Dist(l, b.pos); if (d < 9 * TILE && 0.7f / (1 + d / TILE) > bright) { bright = 0.7f / (1 + d / TILE); at = l; } }
        Diver dv = SeeDiver(p);
        if (dv.alive) { float d = Dist(dv.pos, b.pos); if (d < 9 * TILE && 0.9f / (1 + d / TILE) > bright) { bright = 0.9f / (1 + d / TILE); at = Add(dv.pos, Vector2{0, -12}); } } // the helmet lamp
        if (bright > 0) Consider(b, best, BeastAct::Investigate, 0.45f + bright, -1, 0, Add(at, Vector2{sinf(b.phase * 2) * 18, cosf(b.phase * 1.7f) * 12}));
    }
    if (b.species == CS_JELLY) Consider(b, best, BeastAct::Wander, 0.3f, -1, 0, b.territory); // it drifts where the air takes it
}

static void CaveHooks(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    switch (b.species) {
    case CS_ISOPOD: // cornered, it bursts: a spore cloud that blinds whatever's after it
        if (b.fear > 0.5f && b.cooldown <= 0) {
            for (const auto& m : b.mem) if (m.kind == MEM_THREAT && Recall(b, m, W.time) > 0.3f && Dist(m.pos, b.pos) < 70) {
                AddCloud(W, b.pos, 58, 2.4f, 1);
                W.sounds.push_back({b.pos, 0.5f, 0.3f, i});
                b.cooldown = 9;
                if (!p.verifying) PlatBurst(p, b.pos, 14, Color{160, 200, 100, 255}, 70, 1.0f, 3);
                break;
            }
        }
        break;
    case CS_WORM: { // any tremor near its tube and it's gone - then slowly back out
        if (b.act == BeastAct::Hide) break;
        bool tremor = false;
        for (const auto& s : W.sounds) if (s.source != i && Dist(s.pos, b.pos) < 70 && s.intensity > 0.12f) tremor = true;
        Diver dv = SeeDiver(p);
        if (dv.alive && Dist(dv.pos, b.pos) < 50) tremor = true;
        if (tremor) { b.act = BeastAct::Hide; b.actT = 0; }
        break;
    }
    case CS_LEECH: { // 0 hanging from the ceiling, 1 dropping, 2 crawling back up to its spot
        if (b.special == 0) {
            b.pos = b.territory; b.vel = {0, 0}; b.act = BeastAct::Ambush; b.thinkT = 1;
            if (b.cooldown > 0) break;
            bool below = false;
            Diver dv = SeeDiver(p);
            if (dv.alive && fabsf(dv.pos.x - b.pos.x) < 14 && dv.pos.y > b.pos.y && dv.pos.y - b.pos.y < 11 * TILE && W.nav.LineOfSight(b.pos, dv.pos)) below = true;
            for (const auto& o : W.beasts) if (Alive(o) && !o.hidden && o.species != CS_LEECH && Sp(W.biome, o.species).move != MoveMode::Fly && fabsf(o.pos.x - b.pos.x) < 12 && o.pos.y > b.pos.y && o.pos.y - b.pos.y < 11 * TILE) below = true;
            if (below) { b.special = 1; b.act = BeastAct::Drift; b.actT = 0; b.vel = {0, 40}; W.sounds.push_back({b.pos, 0.2f, 0.2f, i}); }
        } else if (b.special == 1) {
            b.act = BeastAct::Drift; b.thinkT = 1;
            for (int j = 0; j < (int)W.beasts.size(); j++) { // landing on something warm: it bites, and drops off
                Beast& o = W.beasts[j];
                if (j != i && Alive(o) && o.species != CS_LEECH && Dist(o.pos, b.pos) < Sp(W.biome, o.species).radius + 5) { Hurt(W, p, j, 0.3f, CS_LEECH, false); Remember(o, MEM_THREAT, i, b.id, b.pos, {0, 0}, 1, W.time); b.special = 2; b.actT = 0; }
            }
            if (b.grounded || b.actT > 3) { b.special = 2; b.actT = 0; }
        } else {
            b.act = BeastAct::Explore; b.goal = b.territory; b.thinkT = 1; // crawls back up the wall to its spot
            if (Dist(b.pos, b.territory) < 22 || b.actT > 40) { b.special = 0; b.cooldown = 4; b.pos = b.territory; }
        }
        break;
    }
    default: Cave13Hook(W, p, i, dt); break; // the 1.3 roster (beasts_cave.cpp)
    }
}
static bool CaveLethal(const BeastWorld& W, const Beast& b) {
    (void)W;
    if (b.species == CS_STALKER) return b.act == BeastAct::Coil || b.act == BeastAct::Strike || b.act == BeastAct::Hunt; // its jaws, when it has you
    return b.species == CS_LEECH && b.special == 1 && b.vel.y > 60; // a leech coming down on you
}
const BiomeDef& CaveBiome() {
    static const BiomeDef B = [] {
        BiomeDef d;
        d.level = PL_CAVE; d.species = CAVE; d.count = CS_COUNT; d.web = CAVE_WEB; d.webN = (int)(sizeof(CAVE_WEB) / sizeof(CAVE_WEB[0]));
        d.water = true; d.clarity = 0.5f; d.daylight = 0.04f; // pitch-black water
        static const float SIZES[CS_COUNT] = {1.1f, 1.4f, 1.6f, 1.1f, 1.0f, 1.4f, 1.0f, 1.0f, 1, 2.2f, 1, 1, 1, 1, 1, 1, 1};
        d.sizes = SIZES;
        d.spawn = SpawnCave; d.hooks = CaveHooks; d.extras = CaveExtras; d.lethal = CaveLethal; d.touch = Cave13Touch; d.tick = Cave13Tick;
        return d;
    }();
    return B;
}
// ============================================================ the Pipes
// ECOSYSTEM_BESTIARY.md, "The Pipes": "Entities ignore the player; all hazards stem from systemic chaos" - nothing
// here notices the diver at all (CLAUDE.md: the Pipes have no enemies), it just lives. Dust moths drift to the
// duct's surviving light leaks; water-spiders web them; centipedes crawl the walls and ceilings after crickets
// and pillbugs and pick the webs clean; blind pipe-rats hunt by ear in pairs; rust-mites swarm over the pipes;
// pillbugs curl into armoured balls and roll; scavenger mice pick over everything and cockroaches steal from
// them; a glow-beetle bumped in the dark flashes, and the cave crickets bolt from the flash in a stampede that
// sets the next ones off.
//            name              move              mass  rad  speed sprint accel sight  fov   hear smell hunger  dFear dPrey lethal social band pop  den   scav  traits
const SpeciesDef PIPES[PP_COUNT] = {
    {"Dust Moth",        MoveMode::Fly,     0.1f,  4,   40, 120,  600,  140, PI,   0.4f, 0.2f, 0.010f, 0.6f,  0.0f, false, false, 2,  8, 0.1f, 0.0f, T_LIGHTSEEK},
    {"Water-Spider",     MoveMode::Sessile, 0.3f, 11,    0,   0,    0,   40, PI,   0.4f, 0.0f, 0.010f, 0.0f,  0.0f, false, false, 0,  4, 0.0f, 0.0f, T_TRAP},
    {"Centipede",        MoveMode::Climb,   1.0f,  7,   45, 150,  800,  110, 2.0f, 0.8f, 0.9f, 0.016f, 0.4f,  0.0f, false, false, 0,  4, 0.6f, 1.0f, T_STRIKER, 50},
    {"Blind Pipe-Rat",   MoveMode::Walk,    0.6f,  7,   55, 170,  900,   40, 1.2f, 1.0f, 1.0f, 0.018f, 0.5f,  0.0f, false, true,  0,  4, 0.8f, 0.6f, T_MOBBER},
    {"Rust-Mite",        MoveMode::Walk,    0.02f, 3,   30,  70,  500,   60, PI,   0.4f, 0.6f, 0.010f, 0.8f,  0.0f, false, true,  0, 12, 0.4f, 0.5f, T_MOBBER},
    {"Pillbug",          MoveMode::Walk,    0.15f, 5,   20,  60,  400,   70, PI,   0.6f, 0.5f, 0.008f, 0.8f,  0.0f, false, false, 0,  6, 0.5f, 0.6f, T_CURL},
    {"Scavenger Mouse",  MoveMode::Walk,    0.3f,  5,   60, 190, 1000,  120, 2.4f, 0.8f, 0.9f, 0.018f, 0.7f,  0.0f, false, false, 0,  4, 0.8f, 1.0f, 0},
    {"Cockroach",        MoveMode::Walk,    0.3f,  5,   55, 200, 1100,   90, 2.4f, 0.7f, 0.9f, 0.014f, 0.6f,  0.0f, false, false, 0,  6, 0.6f, 1.0f, T_KLEPTO},
    {"Glow-Beetle",      MoveMode::Walk,    0.2f,  5,   25,  90,  500,   80, 2.0f, 0.6f, 0.4f, 0.008f, 0.7f,  0.0f, false, false, 0,  4, 0.5f, 0.5f, T_FLASH},
    {"Cave Cricket",     MoveMode::Walk,    0.15f, 5,   35, 200,  900,  100, PI,   0.9f, 0.3f, 0.010f, 0.9f,  0.0f, false, true,  0,  8, 0.4f, 0.4f, 0},
};
const FoodEdge PIPES_WEB[] = {
    {PP_SPIDER, PP_MOTH, 1.0f}, {PP_SPIDER, PP_MITE, 0.3f},
    {PP_CENTIPEDE, PP_CRICKET, 0.5f}, {PP_CENTIPEDE, PP_PILLBUG, 0.4f}, {PP_CENTIPEDE, PP_MITE, 0.4f},
    {PP_RAT, PP_CENTIPEDE, 0.8f}, {PP_RAT, PP_CRICKET, 0.6f}, {PP_RAT, PP_MOUSE, 0.3f}, {PP_RAT, PP_ROACH, 0.5f},
    {PP_MOUSE, PP_PILLBUG, 0.8f}, {PP_MOUSE, PP_MITE, 0.5f},
};

static void SpawnPipes(BeastWorld& W, PlatformState& p) {
    Spots sp; sp.Build(W, p, 10, W.nav.w - 8);
    if (sp.floor.empty()) return;
    unsigned s = W.seed;
    for (int k = 0; k < 8; k++) { int b = NewBeast(W, PP_MOTH, sp.Above(W, sp.At(0.05f + 0.12f * k + Hash(s, 10 + k) * 0.05f), 2)); W.beasts[b].territory = W.beasts[b].pos; }
    for (int k = 0; k < 4; k++) { // webs strung near the light leaks where the moths come, else anywhere
        Vector2 at = sp.Above(W, sp.At(0.15f + 0.22f * k), 2);
        if (!W.lamps.empty()) { Vector2 l = W.lamps[(size_t)(Hash(s, 20 + k) * W.lamps.size()) % W.lamps.size()]; at = {l.x + (Hash(s, 30 + k) - 0.5f) * 80, l.y + 20}; if (W.nav.Solid((int)(at.x / TILE), (int)(at.y / TILE))) at = l; }
        NewBeast(W, PP_SPIDER, at);
    }
    for (int k = 0; k < 4; k++) NewBeast(W, PP_CENTIPEDE, sp.Stand(sp.At(0.1f + 0.24f * k + Hash(s, 40 + k) * 0.1f), 7));
    for (int k = 0; k < 2; k++) Pack(W, PP_RAT, NearDenFloor(W, sp, 0.2f + 0.5f * k, 7), 2, 90 + k, 30, 0, 500 + k * 10);
    for (int k = 0; k < 2; k++) Pack(W, PP_MITE, sp.Stand(sp.At(0.3f + 0.4f * k), 3), 6, 95 + k, 50, 0, 600 + k * 10);
    for (int k = 0; k < 6; k++) NewBeast(W, PP_PILLBUG, sp.Stand(sp.At(0.07f + 0.15f * k + Hash(s, 70 + k) * 0.05f), 5));
    for (int k = 0; k < 4; k++) NewBeast(W, PP_MOUSE, NearDenFloor(W, sp, 0.12f + 0.24f * k, 5));
    for (int k = 0; k < 6; k++) NewBeast(W, PP_ROACH, sp.Stand(sp.At(0.1f + 0.15f * k + Hash(s, 90 + k) * 0.05f), 5));
    for (int k = 0; k < 4; k++) NewBeast(W, PP_GLOW, sp.Stand(sp.At(0.18f + 0.22f * k), 5));
    for (int k = 0; k < 2; k++) Pack(W, PP_CRICKET, sp.Stand(sp.At(0.25f + 0.45f * k + Hash(s, 110 + k) * 0.1f), 5), 4, 98 + k, 60, 0, 900 + k * 10);
}

static void PipesExtras(BeastWorld& W, const PlatformState& p, int i, Choice& best) {
    Beast& b = W.beasts[i];
    (void)p;
    if (b.species == PP_MOTH && b.fear < 0.5f) { // drawn to the light leaks (never the diver's lamp: it isn't part of their world)
        Vector2 at{0, 0}; float bright = 0;
        for (const auto& l : W.lamps) { float d = Dist(l, b.pos); if (d < 12 * TILE && 0.8f / (1 + d / TILE) > bright) { bright = 0.8f / (1 + d / TILE); at = l; } }
        for (const auto& l : W.lights) { float d = Dist(l.pos, b.pos); if (d < 8 * TILE && l.strength / (1 + d / TILE) > bright) { bright = l.strength / (1 + d / TILE); at = l.pos; } }
        if (bright > 0) Consider(b, best, BeastAct::Investigate, 0.45f + bright, -1, 0, Add(at, Vector2{sinf(b.phase * 2) * 20, cosf(b.phase * 1.7f) * 14}));
    }
}

static void PipesHooks(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    switch (b.species) {
    case PP_PILLBUG: // grabbed at, it curls into a ball that nothing can bite; a curled bug on the move rolls
        if (b.act == BeastAct::Puffed) {
            if (b.actT > 3.5f && b.fear < 0.3f) { b.act = BeastAct::Idle; b.actT = 0; b.thinkT = 0; }
            if (fabsf(b.vel.x) < 30 && b.actT < 0.3f) b.vel.x = b.facing * 140; // the roll
            break;
        }
        for (const auto& m : b.mem) if (m.kind == MEM_THREAT && Recall(b, m, W.time) > 0.15f && Dist(m.pos, b.pos) < 50) {
            b.act = BeastAct::Puffed; b.actT = 0; b.facing = b.pos.x > m.pos.x ? 1.0f : -1.0f;
            W.sounds.push_back({b.pos, 0.15f, 0.2f, i});
            break;
        }
        break;
    case PP_CRICKET: { // a flash in the dark and it bolts in great hops - and a bolting cricket sets the next ones off
        if (b.special > 0) {
            b.special -= dt;
            b.act = BeastAct::Flee; b.thinkT = 0.5f;
            if (b.grounded && b.hopT <= 0) { b.vel.y = -380; b.vel.x = b.facing * 220; b.hopT = 0.45f; }
            for (auto& o : W.beasts) if (&o != &b && Alive(o) && Dist(o.pos, b.pos) < 22) {
                if (o.species == PP_CRICKET && o.special <= 0) { o.special = 1.6f; o.facing = b.facing; } // the stampede
                else if (o.species != PP_CRICKET) { o.vel.x += b.facing * 90; Remember(o, MEM_THREAT, i, b.id, b.pos, b.vel, 0.6f, W.time); } // anything else it crashes into is sent flying
            }
            if (b.special <= 0) b.act = BeastAct::Wander;
            break;
        }
        for (const auto& l : W.lights) if (Dist(l.pos, b.pos) < 70 && Dist(l.pos, b.pos) > 1) { b.special = 1.6f; b.facing = b.pos.x > l.pos.x ? 1.0f : -1.0f; break; }
        if (b.grounded && fabsf(b.vel.x) > 12 && b.hopT <= 0) { b.vel.y = -220; b.hopT = 0.7f; } // it gets about in hops anyway
        break;
    }
    case PP_GLOW: // bumped or frightened, it flashes (FlashTick) - anything knocking into it counts
        if (b.flashT <= 0 && b.cooldown <= 0)
            for (const auto& o : W.beasts) if (&o != &b && Alive(o) && !o.hidden && Dist(o.pos, b.pos) < 12 && Len(o.vel) > 60) { b.flashT = 1.2f; b.cooldown = 4; W.sounds.push_back({b.pos, 0.35f, 0.3f, i}); break; }
        break;
    default: break;
    }
    (void)p;
}
const BiomeDef& PipesBiome() {
    static const BiomeDef B = [] {
        BiomeDef d;
        d.level = PL_PIPES; d.species = PIPES; d.count = PP_COUNT; d.web = PIPES_WEB; d.webN = (int)(sizeof(PIPES_WEB) / sizeof(PIPES_WEB[0]));
        d.water = false; d.ignoreDiver = true; d.clarity = 0.5f; d.daylight = 0.08f;
        static const float SIZES[PP_COUNT] = {1.0f, 1.0f, 1.3f, 1.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.1f};
        d.sizes = SIZES;
        d.spawn = SpawnPipes; d.hooks = PipesHooks; d.extras = PipesExtras;
        return d;
    }();
    return B;
}
// ============================================================ the Weeds
// The user's web for the kelp forest: phytoplankton and spores feed the kelp seahorses; seahorses feed the mermen and
// the tiger sharks; the mermen hunt barracuda; mermen and tiger sharks hunt each other. Everything big that blunders
// into an electric ray gets shocked - the energy of the whole web ends up in the rays - and a shocked body lies
// electrified, which is what the scavenger crabs and the parasitic fungi live on. Sizes against the diver: plankton,
// seahorses, crabs and fungi are all smaller (and different from each other); a ray is about the diver's size; a
// barracuda a little larger; the tiger sharks and mermen three to four times the size.
//            name              move              mass  rad  speed sprint accel sight  fov   hear smell hunger  dFear dPrey lethal social band pop  den   scav  traits
const SpeciesDef WEEDS[WS_COUNT] = {
    {"Phytoplankton",    MoveMode::Swim,    0.02f, 4,   12,  30,  200,   40, PI,   0.1f, 0.1f, 0.000f, 0.3f,  0.0f, false, true,  4, 16, 0.0f, 0.0f, 0},
    {"Kelp Seahorse",    MoveMode::Swim,    0.2f,  5,   25,  90,  500,  120, 2.6f, 0.5f, 0.3f, 0.012f, 0.8f,  0.0f, false, false, 2,  8, 0.7f, 0.0f, T_CAMO},
    {"Barracuda",        MoveMode::Swim,    4.0f,  9,   90, 380, 1300,  260, 1.4f, 0.6f, 0.6f, 0.022f, 0.3f,  0.3f, true,  true,  3,  5, 0.2f, 0.2f, T_STRIKER, 130},
    {"Merman",           MoveMode::Swim,   70.0f, 12,   70, 240,  900,  300, 2.2f, 0.8f, 0.7f, 0.018f, 0.0f,  0.35f, true,  true,  2,  2, 0.6f, 0.3f, T_STRIKER | T_CARRY, 110},
    {"Leviathan Tiger Shark", MoveMode::Swim, 90.0f, 12, 80, 270,  800,  280, 1.8f, 0.9f, 1.0f, 0.020f, 0.0f,  0.35f, true,  false, 3,  2, 0.3f, 0.8f, T_STRIKER | T_CARRY, 120},
    {"Electric Ray",     MoveMode::Swim,    6.0f, 12,   30, 150,  400,  150, PI,   0.7f, 0.6f, 0.008f, 0.5f,  0.0f, true,  false, 0,  4, 0.6f, 0.0f, 0},
    {"Bristle-Crab",     MoveMode::Walk,    0.5f,  6,   40, 110,  700,  130, 2.4f, 0.6f, 0.9f, 0.020f, 0.6f,  0.0f, false, true,  0,  6, 0.7f, 1.0f, T_MOBBER},
    {"Parasitic Fungus", MoveMode::Sessile, 0.1f,  6,    0,   0,    0,   40, PI,   0.0f, 0.8f, 0.006f, 0.0f,  0.0f, false, false, 0,  4, 0.0f, 0.0f, 0},
    // ParkourReference1.3's Seaweed (beasts_weeds.cpp)
    {"Goliath Manatee",  MoveMode::Swim,  400.0f, 20,   30,  50,   80,  160, 2.0f, 0.5f, 1.0f, 0.004f, 0.0f,  0.0f, false, false, 3,  0, 0.0f, 0.0f, T_GIANT},
    {"Mimic Octopus-Stalker", MoveMode::Swim, 8.0f, 11,  0,   0,    0,  180, PI,   0.8f, 0.6f, 0.010f, 0.0f,  0.7f, false, false, 0,  0, 0.0f, 0.0f, T_GIANT | T_CAMO},
    {"Harpoon Mantis",   MoveMode::Walk,    3.0f,  9,    0,   0,    0,  300, PI,   0.6f, 0.3f, 0.010f, 0.0f,  0.6f, false, false, 0,  0, 0.0f, 0.0f, T_GIANT},
    {"Silver-Fin Sardine", MoveMode::Swim,  0.1f,  4,   70, 260, 1100,  150, 2.6f, 0.7f, 0.2f, 0.010f, 0.9f,  0.0f, false, true,  3, 30, 0.0f, 0.0f, 0},
    {"Blood-Kelp",       MoveMode::Sessile, 1.0f, 10,    0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
    {"Luminescent Anemone", MoveMode::Sessile, 1.0f, 11, 0,  0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
    {"Air-Weed",         MoveMode::Sessile, 0.5f, 10,    0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
    {"Tangle-Vine",      MoveMode::Sessile, 2.0f, 16,    0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
    {"Spore-Pod",        MoveMode::Sessile, 0.5f,  9,    0,   0,    0,    0, PI,   0.0f, 0.0f, 0.000f, 0.0f,  0.0f, false, false, 0,  0, 0.0f, 0.0f, T_FLORA},
};
const FoodEdge WEEDS_WEB[] = {
    {WS_SEAHORSE, WS_PLANKTON, 1.0f},
    {WS_MERMAN, WS_SEAHORSE, 0.7f}, {WS_MERMAN, WS_BARRACUDA, 0.8f}, {WS_MERMAN, WS_SHARK, 0.3f},
    {WS_SHARK, WS_SEAHORSE, 0.5f},  {WS_SHARK, WS_MERMAN, 0.5f},    {WS_SHARK, WS_BARRACUDA, 0.6f},
    {WS_BARRACUDA, WS_SEAHORSE, 0.5f},
    {WS_BARRACUDA, WS_SARDINE, 0.9f}, {WS_MERMAN, WS_SARDINE, 0.5f}, {WS_SHARK, WS_SARDINE, 0.3f}, {WS_SEAHORSE, WS_SARDINE, 0.0f},
    {WS_SARDINE, WS_PLANKTON, 0.8f}, {WS_SHARK, WS_MANATEE, 0.2f},
};

static void SpawnWeeds(BeastWorld& W, PlatformState& p) {
    Spots sp; sp.Build(W, p, 12, W.nav.w - 8);
    if (sp.floor.empty()) return;
    unsigned s = W.seed;
    for (int k = 0; k < 2; k++) Pack(W, WS_PLANKTON, sp.Above(W, sp.At(0.2f + 0.45f * k), 5), 8, 110 + k, 70, 40, 100 + k * 20);
    // seahorses cling among the kelp stalks
    std::vector<Vector2> kelp;
    for (int y = 1; y < W.nav.h; y++) for (int x = 12; x < W.nav.w - 8; x++) if (PlatTileAt(p, x, y) == 'w') kelp.push_back({x * TILE + 16.0f, y * TILE + 16.0f});
    for (int k = 0; k < 8; k++) {
        Vector2 at = !kelp.empty() ? kelp[(size_t)(Hash(s, 200 + k) * kelp.size()) % kelp.size()] : sp.Above(W, sp.At(0.1f * k + 0.05f), 2);
        int b = NewBeast(W, WS_SEAHORSE, at); W.beasts[b].special = 1; W.beasts[b].territory = at;
    }
    Pack(W, WS_BARRACUDA, sp.Above(W, sp.At(0.35f + Hash(s, 30) * 0.2f), 4), 5, 120, 90, 30, 300);
    Pack(W, WS_MERMAN, sp.Above(W, sp.At(0.55f + Hash(s, 40) * 0.2f), 3), 2, 130, 60, 20, 400);
    for (int k = 0; k < 2; k++) { int b = NewBeast(W, WS_SHARK, sp.Above(W, sp.At(0.25f + 0.5f * k), 5)); W.beasts[b].pers.wanderlust = std::max(W.beasts[b].pers.wanderlust, 0.7f); }
    for (int k = 0; k < 4; k++) NewBeast(W, WS_RAY, sp.Above(W, sp.At(0.12f + 0.24f * k + Hash(s, 60 + k) * 0.08f), 0));
    for (int k = 0; k < 2; k++) Pack(W, WS_CRAB, NearDenFloor(W, sp, 0.3f + 0.4f * k, 6), 3, 140 + k, 40, 0, 700 + k * 10);
    for (int k = 0; k < 2; k++) { Vector2 f = sp.At(0.4f + 0.3f * k); NewBeast(W, WS_FUNGUS, {f.x, (f.y + 1) * TILE - 3}); }
    Weeds13Spawn(W, p);
}

// A ray's discharge: everything alive close by is shocked (the small die of it), and whatever it kills lies electrified.
static void Discharge(BeastWorld& W, PlatformState& p, int i) {
    Beast& r = W.beasts[i];
    r.special = 0.8f; r.cooldown = 3.5f;
    W.sounds.push_back({r.pos, 1.0f, 0.4f, i});
    for (int j = 0; j < (int)W.beasts.size(); j++) {
        Beast& o = W.beasts[j];
        if (j == i || !Alive(o) || o.hidden || o.species == WS_RAY || Dist(o.pos, r.pos) > 72) continue;
        Hurt(W, p, j, o.mass < 2 ? 1.2f : 0.7f, WS_RAY, false); // the small die of it; the big are hurt
        if (o.life == BeastLife::Corpse) o.special2 = 1; // electrified
        else { Remember(o, MEM_THREAT, i, r.id, r.pos, {0, 0}, 1.0f, W.time); o.vel = Add(o.vel, Mul(Norm(Sub(o.pos, r.pos)), 220)); }
    }
    if (!p.verifying) PlatBurst(p, r.pos, 18, Color{180, 230, 255, 255}, 160, 0.3f, 2);
}
static void WeedsHooks(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    switch (b.species) {
    case WS_SEAHORSE: { // it holds still among the kelp, and all but vanishes
        bool still = Len(b.vel) < 18;
        b.special = Clamp01(b.special + (still ? 0.3f : -0.9f) * dt);
        break;
    }
    case WS_PLANKTON: // it only drifts; the current carries the cloud
        b.vel.x += sinf(W.time * 0.3f + b.id) * 4 * dt;
        break;
    case WS_RAY: {
        b.special -= dt;
        if (b.special > 0 || b.cooldown > 0) break;
        bool pressed = false;
        for (const auto& o : W.beasts) if (Alive(o) && !o.hidden && o.species != WS_RAY && o.mass > 1 && Dist(o.pos, b.pos) < 48) pressed = true; // something big bumped it
        Diver dv = SeeDiver(p);
        if (dv.alive && Dist(dv.pos, b.pos) < 44) pressed = true;
        if (pressed || (b.fear > 0.7f && R(W) < 0.02f)) Discharge(W, p, i);
        break;
    }
    case WS_FUNGUS: { // it grows on the dead - an electrified body most of all - and spreads from it
        for (int j = 0; j < (int)W.beasts.size(); j++) {
            Beast& c = W.beasts[j];
            if (c.life != BeastLife::Corpse || Dist(c.pos, b.pos) > 60) continue;
            c.meat -= dt * (c.special2 > 0 ? 0.12f : 0.04f);
            if (c.meat <= 0.02f) {
                c.life = BeastLife::Gone; W.scavenged++;
                int n = 0; for (const auto& o : W.beasts) n += o.species == WS_FUNGUS && o.life == BeastLife::Alive;
                if (n < 10) { int k = NewBeast(W, WS_FUNGUS, {c.pos.x, c.pos.y}); W.beasts[k].vel = {0, 0}; W.births++; }
            }
            break;
        }
        break;
    }
    default: Weeds13Hook(W, p, i, dt); break; // the 1.3 roster (beasts_weeds.cpp)
    }
}
static void WeedsExtras(BeastWorld& W, const PlatformState& p, int i, Choice& best) {
    Beast& b = W.beasts[i];
    (void)p;
    if (b.species == WS_CRAB) { // an electrified body draws them from far off
        for (int j = 0; j < (int)W.beasts.size(); j++) {
            const Beast& c = W.beasts[j];
            if (c.life == BeastLife::Corpse && c.special2 > 0 && c.id != b.ignoreId && Dist(c.pos, b.pos) < 14 * TILE) { Consider(b, best, BeastAct::Scavenge, 0.7f + 0.3f * b.hunger, j, c.id, c.pos); break; }
        }
    }
    if (b.species == WS_SEAHORSE && b.fear < 0.3f && b.hunger < 0.6f) Consider(b, best, BeastAct::Ambush, 0.3f, -1, 0, b.pos); // clinging still among the kelp, grazing what drifts past
}
static bool WeedsLethal(const BeastWorld& W, const Beast& b) {
    (void)W;
    switch (b.species) {
    case WS_BARRACUDA: return b.act == BeastAct::Strike || b.act == BeastAct::Coil;
    case WS_MERMAN: case WS_SHARK: return b.act == BeastAct::Coil || b.act == BeastAct::Strike || (b.act == BeastAct::Hunt && b.target == BEAST_DIVER); // cruising past, it ignores you; coming for you, it kills
    case WS_RAY: return b.special > 0;
    default: return false;
    }
}
static bool WeedsTouch(const BeastWorld& W, const Beast& b, Rectangle diver) {
    (void)W;
    if (Weeds13Touch(W, b, diver)) return true;
    return b.species == WS_RAY && b.life == BeastLife::Alive && b.special > 0 && CheckCollisionCircleRec(b.pos, 60, diver); // the discharge reaches
}
const BiomeDef& WeedsBiome() {
    static const BiomeDef B = [] {
        BiomeDef d;
        d.level = PL_WEEDS; d.species = WEEDS; d.count = WS_COUNT; d.web = WEEDS_WEB; d.webN = (int)(sizeof(WEEDS_WEB) / sizeof(WEEDS_WEB[0]));
        d.water = true; d.clarity = 0.85f; d.daylight = 0.7f;
        static const float SIZES[WS_COUNT] = {0.5f, 0.8f, 1.5f, 3.5f, 4.2f, 1.0f, 0.8f, 0.7f, 1, 1, 1, 0.6f, 1, 1, 1, 1, 1}; // the Leviathan, bigger still
        d.sizes = SIZES;
        d.spawn = SpawnWeeds; d.hooks = WeedsHooks; d.extras = WeedsExtras; d.lethal = WeedsLethal; d.touch = WeedsTouch; d.tick = Weeds13Tick;
        return d;
    }();
    return B;
}

// ============================================================ Atlantis
// The drowned city has its own web, lit by the glyphs: glyph wisps - motes of living light - drift toward anything
// glowing; blind temple shrimp school after the wisps; anglerfish hang in the dark halls with a lure of their own, and
// what comes to the light is eaten; glyph eels wait in the broken masonry for the shrimp; the stone guardians sit so
// still among the rubble that they are rubble, until something small passes; and the Lost Ones - the drowned citizens
// themselves - shuffle through the streets after any sound, and pick over whatever the others leave.
//            name              move              mass  rad  speed sprint accel sight  fov   hear smell hunger  dFear dPrey lethal social band pop  den   scav  traits
const SpeciesDef ATLANTIS[AS_COUNT] = {
    {"Glyph Wisp",       MoveMode::Swim,    0.02f, 3,   20,  60,  300,   80, PI,   0.1f, 0.1f, 0.000f, 0.4f,  0.0f, false, true,  3, 12, 0.0f, 0.0f, T_LIGHTSEEK},
    {"Temple Shrimp",    MoveMode::Swim,    0.1f,  4,   40, 160,  700,   90, 2.6f, 0.8f, 0.5f, 0.012f, 0.8f,  0.0f, false, true,  2, 12, 0.6f, 0.3f, 0},
    {"Anglerfish",       MoveMode::Swim,    6.0f, 10,   30, 280,  900,  200, 2.0f, 0.6f, 0.6f, 0.018f, 0.2f,  0.4f, true,  false, 3,  3, 0.5f, 0.3f, T_STRIKER | T_CAMO, 90},
    {"Lost One",         MoveMode::Walk,   60.0f, 10,   30,  90,  500,  120, 2.0f, 1.0f, 0.8f, 0.015f, 0.0f,  0.6f, true,  true,  0,  4, 0.6f, 1.0f, 0},
    {"Stone Guardian",   MoveMode::Walk,    6.0f, 12,   25, 260, 1200,  150, 1.6f, 0.7f, 0.3f, 0.010f, 0.1f,  0.4f, true,  false, 0,  3, 0.8f, 0.3f, T_CAMO | T_STRIKER | T_CHARGER, 110},
    {"Glyph Eel",        MoveMode::Swim,    5.0f,  9,   90, 320, 1100,  220, 1.9f, 0.8f, 0.9f, 0.025f, 0.2f,  0.5f, true,  false, 2,  3, 0.9f, 0.4f, T_STRIKER | T_DEN_AMBUSH | T_CARRY, 120},
};
const FoodEdge ATLANTIS_WEB[] = {
    {AS_SHRIMP, AS_WISP, 0.9f},
    {AS_ANGLER, AS_SHRIMP, 1.0f}, {AS_ANGLER, AS_WISP, 0.6f},
    {AS_EEL, AS_SHRIMP, 0.9f},    {AS_EEL, AS_ANGLER, 0.3f},
    {AS_GUARDIAN, AS_SHRIMP, 0.4f}, {AS_GUARDIAN, AS_LOSTONE, 0.3f},
};
static void SpawnAtlantis(BeastWorld& W, PlatformState& p) {
    Spots sp; sp.Build(W, p, 12, W.nav.w - 8);
    if (sp.floor.empty()) return;
    unsigned s = W.seed;
    for (int k = 0; k < 3; k++) Pack(W, AS_WISP, sp.Above(W, sp.At(0.15f + 0.3f * k), 3), 4, 150 + k, 60, 40, 100 + k * 10);
    for (int k = 0; k < 2; k++) Pack(W, AS_SHRIMP, sp.Above(W, sp.At(0.3f + 0.4f * k), 2), 6, 160 + k, 50, 20, 200 + k * 10);
    for (int k = 0; k < 3; k++) { int b = NewBeast(W, AS_ANGLER, sp.Above(W, sp.At(0.2f + 0.3f * k + Hash(s, 30 + k) * 0.1f), 3)); W.beasts[b].special = 1; }
    for (int k = 0; k < 2; k++) Pack(W, AS_LOSTONE, NearDenFloor(W, sp, 0.25f + 0.45f * k, 10), 2, 170 + k, 40, 0, 400 + k * 10);
    for (int k = 0; k < 3; k++) { int b = NewBeast(W, AS_GUARDIAN, sp.Stand(sp.At(0.15f + 0.3f * k + Hash(s, 50 + k) * 0.1f), 12)); W.beasts[b].special = 1; }
    for (int k = 0; k < 3 && !W.dens.empty(); k++) {
        int d = (int)(Hash(s, 60 + k) * W.dens.size()) % (int)W.dens.size();
        int b = NewBeast(W, AS_EEL, DenMouth(W, d));
        W.beasts[b].den = d; EnterDen(W, W.beasts[b], d); W.beasts[b].act = BeastAct::Ambush;
    }
}
static void AtlantisHooks(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    switch (b.species) {
    case AS_ANGLER: { // it hangs still in the dark and keeps its lure lit: the light is the trap
        bool still = Len(b.vel) < 20;
        b.special = Clamp01(b.special + (still ? 0.3f : -0.9f) * dt);
        b.flashT = 0.2f; // its lure is a light everything else can see by, and swims toward
        break;
    }
    case AS_GUARDIAN: { // stone among stone while it keeps still
        bool still = Len(b.vel) < 12;
        b.special = Clamp01(b.special + (still ? 0.4f : -1.5f) * dt);
        break;
    }
    case AS_WISP: b.flashT = std::max(b.flashT, 0.05f); break; // a faint glow of its own
    default: break;
    }
    (void)p;
}
static void AtlantisExtras(BeastWorld& W, const PlatformState& p, int i, Choice& best) {
    Beast& b = W.beasts[i];
    if (b.species == AS_WISP && b.fear < 0.5f) { // toward the brightest light: a glyph, a lure, the diver's lamp
        Vector2 at{0, 0}; float bright = 0;
        for (const auto& l : W.lights) { float d = Dist(l.pos, b.pos); if (d < 10 * TILE && d > 1 && l.strength / (1 + d / TILE) > bright) { bright = l.strength / (1 + d / TILE); at = l.pos; } }
        for (const auto& l : W.lamps) { float d = Dist(l, b.pos); if (d < 10 * TILE && 0.6f / (1 + d / TILE) > bright) { bright = 0.6f / (1 + d / TILE); at = l; } }
        Diver dv = SeeDiver(p);
        if (dv.alive) { float d = Dist(dv.pos, b.pos); if (d < 9 * TILE && 0.8f / (1 + d / TILE) > bright) { bright = 0.8f / (1 + d / TILE); at = Add(dv.pos, Vector2{0, -12}); } }
        if (bright > 0) Consider(b, best, BeastAct::Investigate, 0.45f + bright, -1, 0, Add(at, Vector2{sinf(b.phase * 2) * 16, cosf(b.phase * 1.7f) * 10}));
    }
    if (b.species == AS_GUARDIAN && b.fear < 0.3f) Consider(b, best, BeastAct::Ambush, 0.5f, -1, 0, b.pos); // it settles, and becomes rubble
    if (b.species == AS_LOSTONE && b.fear < 0.5f) { // the drowned hunt by ear: any sound draws them, the louder the surer
        float bs = 0; Vector2 at{0, 0};
        for (const auto& m : b.mem) if (m.kind == MEM_SOUND) { float r = Recall(b, m, W.time); if (r > bs) { bs = r; at = m.pos; } }
        if (bs > 0.05f && Dist(at, b.pos) > TILE) Consider(b, best, BeastAct::Investigate, 0.45f + bs, -1, 0, at);
    }
}
static bool AtlantisLethal(const BeastWorld& W, const Beast& b) {
    (void)W;
    switch (b.species) {
    case AS_ANGLER: case AS_LOSTONE: case AS_EEL: return true;
    case AS_GUARDIAN: return b.act == BeastAct::Strike || b.act == BeastAct::Coil; // rubble, until it moves
    default: return false;
    }
}
static bool AtlantisTouch(const BeastWorld& W, const Beast& b, Rectangle diver) {
    (void)W;
    if (b.species == AS_EEL && b.life == BeastLife::Alive && b.hidden && b.act == BeastAct::Ambush) return CheckCollisionCircleRec({b.pos.x, b.pos.y - 22}, 8, diver);
    return false;
}
const BiomeDef& AtlantisBiome() {
    static const BiomeDef B = [] {
        BiomeDef d;
        d.level = PL_ATLANTIS; d.species = ATLANTIS; d.count = AS_COUNT; d.web = ATLANTIS_WEB; d.webN = (int)(sizeof(ATLANTIS_WEB) / sizeof(ATLANTIS_WEB[0]));
        d.water = true; d.clarity = 0.6f; d.daylight = 0.08f;
        static const float SIZES[AS_COUNT] = {0.6f, 0.8f, 1.8f, 1.4f, 1.8f, 1.8f};
        d.sizes = SIZES;
        d.spawn = SpawnAtlantis; d.hooks = AtlantisHooks; d.extras = AtlantisExtras; d.lethal = AtlantisLethal; d.touch = AtlantisTouch;
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
    // 4) a barn owl takes a rat in the open, carries it off in its talons, and eats it somewhere else
    {
        PlatformState p; Bench(p, PL_PIRATE, 50, 16); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int owl = Put(W, PS_OWL, {15 * TILE, 5 * TILE}); W.beasts[owl].hunger = 1; W.beasts[owl].pers.aggression = 0.9f;
        int rat = Put(W, PS_RAT, {21 * TILE, 13 * TILE + 26}); W.beasts[rat].pers.wanderlust = 0;
        bool carried = false, ate = false; Vector2 killAt{0, 0};
        for (int f = 0; f < 60 * 30 && !ate; f++) {
            BeastsUpdate(p, 1 / 60.0f);
            const Beast& o = W.beasts[owl];
            if (W.beasts[rat].life == BeastLife::Corpse && killAt.x == 0) killAt = W.beasts[rat].pos;
            if (o.carry == rat && killAt.x != 0 && Dist(W.beasts[rat].pos, killAt) > 2 * TILE) carried = true;
            if (carried && o.act == BeastAct::Eat) ate = true;
            if (getenv("DEPTH_BEASTLOG") && f % 60 == 0) TraceLog(LOG_WARNING, "  owl t=%d %s (%.0f,%.0f) tgt %d carry %d | rat %s life %d (%.0f,%.0f) fear %.2f", f / 60, BeastActName(o.act), o.pos.x, o.pos.y, o.target, o.carry, BeastActName(W.beasts[rat].act), (int)W.beasts[rat].life, W.beasts[rat].pos.x, W.beasts[rat].pos.y, W.beasts[rat].fear);
        }
        if (!carried) fail("a barn owl never carried a rat off in its talons");
        if (!ate) fail("the owl never ate what it carried off");
    }
    if (!VerifyPirate13()) ok = false;
    if (ok) TraceLog(LOG_WARNING, "verify-pirate-ecosystem: OK - stalk/pounce, gunfire/keg/blast/fleas/berserk, gull theft and owl carry-off all confirmed");
    return ok;
}

static bool VerifyIsland() {
    bool ok = true;
    auto fail = [&](const char* what) { TraceLog(LOG_WARNING, "verify-island-ecosystem: FAILED - %s", what); ok = false; };
    ok &= RealLevel(PL_ISLAND, "island-ecosystem", 7, 3, 3);
    // 1) a cornered boar turns on the diver pressing it - and a charging boar kills
    {
        PlatformState p; Bench(p, PL_ISLAND, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int boar = Put(W, IS_BOAR, {20 * TILE, 11 * TILE + 24}); W.beasts[boar].pers.bravery = 0.9f; W.beasts[boar].pers.aggression = 0.8f; W.beasts[boar].facing = -1;
        p.deathTimer = 0; p.pos = {15 * TILE, 11 * TILE - 10}; p.vel = {0, 0}; p.onGround = true;
        bool charged = false, lethal = false;
        for (int f = 0; f < 60 * 10 && !lethal; f++) {
            BeastsUpdate(p, 1 / 60.0f);
            p.pos.x = std::min(p.pos.x + 1.0f, 19.0f * TILE); // edging closer
            if (W.beasts[boar].act == BeastAct::Coil || W.beasts[boar].act == BeastAct::Strike) charged = true;
            if (BeastLethalNow(p, W.beasts[boar])) lethal = true;
        }
        if (!charged) fail("a brave boar never turned and charged the diver crowding it");
        if (!lethal) fail("a charging boar isn't a hazard");
    }
    // 2) a coconut crab cuts a coconut down, and something (a gull, a lizard, the crab) eats it
    {
        PlatformState p; Bench(p, PL_ISLAND, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int crab = Put(W, IS_CRAB, {20 * TILE, 11 * TILE + 26}); W.beasts[crab].special = 0; W.beasts[crab].hunger = 0.2f;
        int gull = Put(W, IS_GULL, {12 * TILE, 6 * TILE}); W.beasts[gull].hunger = 1;
        bool cut = false, eaten = false;
        for (int f = 0; f < 60 * 30 && !eaten; f++) {
            BeastsUpdate(p, 1 / 60.0f);
            for (const auto& b : W.beasts) {
                if (b.species == IS_COCONUT && b.life == BeastLife::Corpse) cut = true;
                if (b.act == BeastAct::Eat && Valid(W, b.target, b.targetId) && W.beasts[b.target].species == IS_COCONUT) eaten = true;
            }
        }
        if (!cut) fail("a coconut crab never cut a coconut down");
        if (!eaten) fail("nothing ever ate the fallen coconut");
    }
    // 3) a crash close by bursts a roosting bat colony
    {
        PlatformState p; Bench(p, PL_ISLAND, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int bat = Put(W, IS_BAT, {20 * TILE, 5 * TILE}); W.beasts[bat].pers.bravery = 0.3f;
        W.sounds.push_back({{21 * TILE, 8 * TILE}, 1.0f, 0.5f, -1});
        bool fled = false;
        for (int f = 0; f < 60 * 2 && !fled; f++) { BeastsUpdate(p, 1 / 60.0f); if (W.beasts[bat].act == BeastAct::Flee) fled = true; }
        if (!fled) fail("a crash beside a bat didn't send it flying off");
    }
    // 4) a lizard that bites a dart frog spits it out, and won't go for frogs again
    {
        PlatformState p; Bench(p, PL_ISLAND, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int liz = Put(W, IS_LIZARD, {15 * TILE, 11 * TILE + 26}); W.beasts[liz].hunger = 1; W.beasts[liz].pers.aggression = 0.9f; W.beasts[liz].pers.bravery = 0.8f;
        int frog = Put(W, IS_FROG, {19 * TILE, 11 * TILE + 26}); W.beasts[frog].pers.bravery = 0.9f;
        bool learned = false;
        for (int f = 0; f < 60 * 30 && !learned; f++) { BeastsUpdate(p, 1 / 60.0f); if (W.beasts[liz].trauma >> IS_FROG & 1) learned = true; }
        if (!learned) fail("a lizard never bit a dart frog and learned from it");
        if (W.beasts[frog].life != BeastLife::Alive) fail("the dart frog was eaten despite its poison");
    }
    // 5) an orb web catches a bat that flies into it
    {
        PlatformState p; Bench(p, PL_ISLAND, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        NewBeast(W, IS_SPIDER, {20 * TILE, 6 * TILE});
        int bat = Put(W, IS_BAT, {17 * TILE, 5 * TILE + 20});
        bool caught = false;
        for (int f = 0; f < 60 * 20 && !caught; f++) {
            BeastsUpdate(p, 1 / 60.0f);
            if (f < 60 * 3) { W.beasts[bat].vel = {60, 0}; W.beasts[bat].act = BeastAct::Wander; W.beasts[bat].goal = {24 * TILE, 5 * TILE + 20}; }
            if (W.beasts[bat].life != BeastLife::Alive) caught = true;
        }
        if (!caught) fail("an orb web never caught a bat flying through it");
    }
    if (!VerifyIsland13()) ok = false;
    if (ok) TraceLog(LOG_WARNING, "verify-island-ecosystem: OK - boar charge, coconut cut/steal, bat burst, dart-frog lesson and web catch all confirmed");
    return ok;
}
static bool VerifyCave() {
    bool ok = true;
    auto fail = [&](const char* what) { TraceLog(LOG_WARNING, "verify-cave-ecosystem: FAILED - %s", what); ok = false; };
    ok &= RealLevel(PL_CAVE, "cave-ecosystem", 6, 3, 5);
    // 1) a moth in the dark finds its way to a light
    {
        PlatformState p; Bench(p, PL_CAVE, 40, 14); p.tiles[6][30] = 'o'; BenchBuild(p);
        BeastWorld& W = p.fauna;
        int moth = Put(W, CS_GSHRIMP, {12 * TILE, 8 * TILE});
        bool reached = false;
        for (int f = 0; f < 60 * 20 && !reached; f++) { BeastsUpdate(p, 1 / 60.0f); if (Dist(W.beasts[moth].pos, {30 * TILE + 16, 6 * TILE + 16}) < 3 * TILE) reached = true; }
        if (!reached) fail("a cave moth never found its way to the light");
    }
    // 2) a leech drops from the ceiling onto the diver walking under it, and a dropping leech kills
    {
        PlatformState p; Bench(p, PL_CAVE, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int leech = Put(W, CS_LEECH, {20 * TILE + 16, 1 * TILE + 6}); W.beasts[leech].territory = W.beasts[leech].pos;
        p.deathTimer = 0; p.pos = {20 * TILE + 8, 11 * TILE - 8}; p.vel = {0, 0};
        bool dropped = false, lethal = false, touched = false;
        for (int f = 0; f < 60 * 4; f++) {
            BeastsUpdate(p, 1 / 60.0f);
            if (W.beasts[leech].special == 1) dropped = true;
            if (BeastLethalNow(p, W.beasts[leech])) lethal = true;
            if (BeastsTouchDiver(p, PlatDiverBox(p))) touched = true;
            if (getenv("DEPTH_BEASTLOG") && f % 6 == 0 && f < 120) { const Beast& l = W.beasts[leech]; Rectangle d = PlatDiverBox(p); TraceLog(LOG_WARNING, "  leech f=%d sp %.0f act %s pos (%.0f,%.0f) vel %.0f hidden %d | diver box (%.0f,%.0f %.0fx%.0f)", f, l.special, BeastActName(l.act), l.pos.x, l.pos.y, l.vel.y, (int)l.hidden, d.x, d.y, d.width, d.height); }
        }
        if (!dropped) fail("a ceiling leech never dropped on the diver passing under it");
        if (!lethal || !touched) fail("a dropping leech isn't a hazard to the diver under it");
    }
    // 3) a fungal beetle hunted by a salamander bursts into a blinding spore cloud
    {
        PlatformState p; Bench(p, PL_CAVE, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int sal = Put(W, CS_OLM, {16 * TILE, 11 * TILE + 24}); W.beasts[sal].hunger = 1; W.beasts[sal].pers.aggression = 0.9f;
        int bee = Put(W, CS_ISOPOD, {19 * TILE, 11 * TILE + 26}); W.beasts[bee].pers.bravery = 0.2f;
        bool burst = false;
        for (int f = 0; f < 60 * 20 && !burst; f++) { BeastsUpdate(p, 1 / 60.0f); for (const auto& c : W.ink) if (c.kind == 1) burst = true;
            if (getenv("DEPTH_BEASTLOG") && f % 60 == 0) { const Beast& a = W.beasts[sal]; const Beast& c = W.beasts[bee]; int np = 0; float bs = 0; for (const auto& m : a.mem) if (m.kind == MEM_PREY && m.strength > 0) { np++; bs = std::max(bs, Recall(a, m, W.time)); } TraceLog(LOG_WARNING, "  beetle t=%d sal %s (%.0f) tgt %d prey mems %d best %.2f hunger %.2f sounds %d | beetle %s (%.0f) fear %.2f life %d", f / 60, BeastActName(a.act), a.pos.x, a.target, np, bs, a.hunger, (int)W.sounds.size(), BeastActName(c.act), c.pos.x, c.fear, (int)c.life); } }
        if (!burst) fail("a beetle a salamander was after never burst its spores");
    }
    // 4) a hungry bat catches a moth on the wing
    {
        PlatformState p; Bench(p, PL_CAVE, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int bat = Put(W, CS_CUSK, {12 * TILE, 6 * TILE}); W.beasts[bat].hunger = 1; W.beasts[bat].pers.aggression = 0.9f;
        int moth = Put(W, CS_GSHRIMP, {18 * TILE, 7 * TILE});
        bool ate = false;
        for (int f = 0; f < 60 * 20 && !ate; f++) { BeastsUpdate(p, 1 / 60.0f); if (W.beasts[moth].life != BeastLife::Alive) ate = true; }
        if (!ate) fail("a hungry bat never caught a moth");
    }
    // 5) a tube worm snaps back into its tube at a tremor
    {
        PlatformState p; Bench(p, PL_CAVE, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int worm = NewBeast(W, CS_WORM, {20 * TILE, 12 * TILE - 2});
        W.sounds.push_back({{21 * TILE, 11 * TILE}, 0.3f, 0.3f, -1});
        BeastsUpdate(p, 1 / 60.0f);
        if (W.beasts[worm].act != BeastAct::Hide) fail("a tube worm didn't pull in at a tremor beside it");
    }
    if (!VerifyCave13()) ok = false;
    if (ok) TraceLog(LOG_WARNING, "verify-cave-ecosystem: OK - moth to light, ceiling-leech drop (lethal), spore burst, bat hunt and tube-worm retract all confirmed");
    return ok;
}

static bool VerifyPipes() {
    bool ok = true;
    auto fail = [&](const char* what) { TraceLog(LOG_WARNING, "verify-pipe-ecosystem: FAILED - %s", what); ok = false; };
    ok &= RealLevel(PL_PIPES, "pipe-ecosystem", 8, 3, 1);
    // 1) nothing notices the diver, even standing among them
    {
        PlatformState p; Bench(p, PL_PIPES, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int mouse = Put(W, PP_MOUSE, {20 * TILE, 11 * TILE + 26}), cr = Put(W, PP_CRICKET, {22 * TILE, 11 * TILE + 26});
        p.deathTimer = 0; p.pos = {21 * TILE, 11 * TILE - 8};
        for (int f = 0; f < 60 * 3; f++) { BeastsUpdate(p, 1 / 60.0f); p.vel.x = f % 60 < 30 ? 300.0f : -300.0f; p.onGround = true; BeastsNoise(p, {p.pos.x, p.pos.y}, 1.0f); }
        for (int i : {mouse, cr}) for (const auto& m : W.beasts[i].mem) if (m.strength > 0 && (m.source == BEAST_DIVER || m.source == -1)) { fail("a Pipes creature noticed the diver"); i = 1 << 30; break; }
    }
    // 2) a water-spider's web by a light leak catches a moth drawn to the light
    {
        PlatformState p; Bench(p, PL_PIPES, 40, 14); p.tiles[6][28] = 'o'; BenchBuild(p);
        BeastWorld& W = p.fauna;
        NewBeast(W, PP_SPIDER, {28 * TILE + 16, 7 * TILE + 10});
        int moth = Put(W, PP_MOTH, {12 * TILE, 8 * TILE});
        bool caught = false;
        for (int f = 0; f < 60 * 30 && !caught; f++) { BeastsUpdate(p, 1 / 60.0f); if (W.beasts[moth].life != BeastLife::Alive) caught = true; }
        if (!caught) fail("a moth drawn to a light leak never got caught in the web beside it");
    }
    // 3) a glow-beetle's flash stampedes the crickets, each setting off the next
    {
        PlatformState p; Bench(p, PL_PIPES, 60, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int glow = Put(W, PP_GLOW, {20 * TILE, 11 * TILE + 26});
        int c1 = Put(W, PP_CRICKET, {21 * TILE, 11 * TILE + 26}), c2 = Put(W, PP_CRICKET, {26 * TILE, 11 * TILE + 26}), c3 = Put(W, PP_CRICKET, {29 * TILE, 11 * TILE + 26});
        for (int c : {c1, c2, c3}) W.beasts[c].pers.wanderlust = 0;
        W.beasts[glow].flashT = 1.2f;
        bool a = false, b = false, c = false;
        for (int f = 0; f < 60 * 5; f++) {
            BeastsUpdate(p, 1 / 60.0f);
            a |= W.beasts[c1].special > 0; b |= W.beasts[c2].special > 0; c |= W.beasts[c3].special > 0;
            if (f == 0) W.beasts[glow].flashT = 1.2f;
        }
        if (!a) fail("a glow-beetle's flash never panicked the cricket beside it");
        if (!b || !c) fail("the panicking cricket never set off a stampede of the others");
    }
    // 4) a pillbug a mouse is after curls up - and isn't eaten
    {
        PlatformState p; Bench(p, PL_PIPES, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int mouse = Put(W, PP_MOUSE, {16 * TILE, 11 * TILE + 26}); W.beasts[mouse].hunger = 1; W.beasts[mouse].pers.aggression = 0.9f;
        int bug = Put(W, PP_PILLBUG, {19 * TILE, 11 * TILE + 26});
        bool curled = false;
        for (int f = 0; f < 60 * 15; f++) { BeastsUpdate(p, 1 / 60.0f); if (W.beasts[bug].act == BeastAct::Puffed) curled = true; }
        if (!curled) fail("a pillbug never curled up with a mouse after it");
    }
    if (ok) TraceLog(LOG_WARNING, "verify-pipe-ecosystem: OK - the diver ignored, moth to light into a web, flash stampede and pillbug curl all confirmed");
    return ok;
}
static bool VerifyWeeds() {
    bool ok = true;
    auto fail = [&](const char* what) { TraceLog(LOG_WARNING, "verify-weeds-ecosystem: FAILED - %s", what); ok = false; };
    ok &= RealLevel(PL_WEEDS, "weeds-ecosystem", 7, 3, 707);
    // 1) a seahorse grazes the plankton
    {
        PlatformState p; Bench(p, PL_WEEDS, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int sh = Put(W, WS_SEAHORSE, {18 * TILE, 8 * TILE}); W.beasts[sh].hunger = 1; W.beasts[sh].pers.aggression = 0.9f;
        int pk = Put(W, WS_PLANKTON, {21 * TILE, 8 * TILE});
        bool ate = false;
        for (int f = 0; f < 60 * 20 && !ate; f++) { BeastsUpdate(p, 1 / 60.0f); if (W.beasts[pk].life != BeastLife::Alive) ate = true; }
        if (!ate) fail("a hungry seahorse never grazed the plankton drifting by");
    }
    // 2) something big bumps a ray: the discharge kills the small nearby, the body lies electrified, and the crabs come for it
    {
        PlatformState p; Bench(p, PL_WEEDS, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int ray = Put(W, WS_RAY, {20 * TILE, 11 * TILE + 20});
        int sh = Put(W, WS_SEAHORSE, {20 * TILE + 24, 11 * TILE + 4});
        Put(W, WS_BARRACUDA, {20 * TILE - 20, 11 * TILE + 10});
        int crab = Put(W, WS_CRAB, {27 * TILE, 11 * TILE + 26}); W.beasts[crab].hunger = 0.8f;
        bool shocked = false, electrified = false, scav = false;
        for (int f = 0; f < 60 * 25 && !scav; f++) {
            BeastsUpdate(p, 1 / 60.0f);
            if (W.beasts[ray].special > 0) shocked = true;
            if (W.beasts[sh].life == BeastLife::Corpse && W.beasts[sh].special2 > 0) electrified = true;
            if (W.beasts[crab].act == BeastAct::Eat) scav = true;
        }
        if (!shocked) fail("a barracuda bumping a ray never set off its discharge");
        if (!electrified) fail("the discharge never left an electrified body");
        if (!scav) fail("the scavenger crabs never came for the electrified body");
        if (shocked) { W.beasts[ray].special = 0.5f; Rectangle d{W.beasts[ray].pos.x + 20, W.beasts[ray].pos.y - 14, 16, 28}; if (!BeastsTouchDiver(p, d)) fail("a ray's discharge doesn't reach the diver beside it"); }
    }
    // 3) a tiger shark takes a seahorse and carries it off
    {
        PlatformState p; Bench(p, PL_WEEDS, 50, 16); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int shark = Put(W, WS_SHARK, {12 * TILE, 8 * TILE}); W.beasts[shark].hunger = 1; W.beasts[shark].pers.aggression = 0.9f;
        int sh = Put(W, WS_BARRACUDA, {20 * TILE, 8 * TILE}); W.beasts[sh].school = -1;
        bool killed = false, carried = false;
        for (int f = 0; f < 60 * 25 && !carried; f++) {
            BeastsUpdate(p, 1 / 60.0f);
            if (W.beasts[sh].life != BeastLife::Alive) killed = true;
            if (W.beasts[shark].carry == sh) carried = true;
        }
        if (!killed) fail("a hungry tiger shark never caught a barracuda");
        if (!carried) fail("the shark never carried its kill off");
    }
    if (!VerifyWeeds13()) ok = false;
    if (ok) TraceLog(LOG_WARNING, "verify-weeds-ecosystem: OK - grazing, ray discharge/electrified body/crab scavenging, and the shark's carry-off all confirmed");
    return ok;
}
static bool VerifyAtlantis() {
    bool ok = true;
    auto fail = [&](const char* what) { TraceLog(LOG_WARNING, "verify-atlantis-ecosystem: FAILED - %s", what); ok = false; };
    ok &= RealLevel(PL_ATLANTIS, "atlantis-ecosystem", 6, 3, 808);
    // 1) a wisp drifts to an anglerfish's lure, and the angler eats what the light brings in
    {
        PlatformState p; Bench(p, PL_ATLANTIS, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int ang = Put(W, AS_ANGLER, {24 * TILE, 7 * TILE}); W.beasts[ang].hunger = 1; W.beasts[ang].pers.aggression = 0.9f;
        int wisp = Put(W, AS_WISP, {14 * TILE, 7 * TILE});
        int shrimp = Put(W, AS_SHRIMP, {11 * TILE, 7 * TILE}); W.beasts[shrimp].hunger = 1;
        bool drawn = false, eaten = false;
        for (int f = 0; f < 60 * 30 && !eaten; f++) {
            BeastsUpdate(p, 1 / 60.0f);
            if (Dist(W.beasts[wisp].pos, W.beasts[ang].pos) < 4 * TILE) drawn = true;
            if (W.beasts[ang].act == BeastAct::Eat) eaten = true;
            if (getenv("DEPTH_BEASTLOG") && f % 60 == 0) { const Beast& a = W.beasts[ang]; int np = 0; float br = 0; for (const auto& m : a.mem) if (m.kind == MEM_PREY && m.strength > 0) { np++; br = std::max(br, Recall(a, m, W.time)); } TraceLog(LOG_WARNING, "  angler t=%d %s (%.0f,%.0f) tgt %d camo %.2f prey %d/%.2f | wisp %s (%.0f,%.0f) life %d | shrimp %s (%.0f,%.0f) life %d", f / 60, BeastActName(a.act), a.pos.x, a.pos.y, a.target, a.special, np, br, BeastActName(W.beasts[wisp].act), W.beasts[wisp].pos.x, W.beasts[wisp].pos.y, (int)W.beasts[wisp].life, BeastActName(W.beasts[shrimp].act), W.beasts[shrimp].pos.x, W.beasts[shrimp].pos.y, (int)W.beasts[shrimp].life); }
        }
        if (!drawn) fail("a glyph wisp was never drawn to the anglerfish's lure");
        if (!eaten) fail("the anglerfish never ate what its lure brought in");
    }
    // 2) a stone guardian settles into the rubble, then strikes at a shrimp passing close
    {
        PlatformState p; Bench(p, PL_ATLANTIS, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int gd = Put(W, AS_GUARDIAN, {20 * TILE, 11 * TILE + 20}); W.beasts[gd].hunger = 1; W.beasts[gd].pers.aggression = 0.9f; W.beasts[gd].pers.wanderlust = 0;
        bool hidden = false, struck = false;
        for (int f = 0; f < 60 * 25 && !struck; f++) {
            BeastsUpdate(p, 1 / 60.0f);
            if (W.beasts[gd].special > 0.8f) hidden = true;
            if (f == 60 * 5) Put(W, AS_SHRIMP, {17 * TILE, 11 * TILE + 10});
            if (W.beasts[gd].act == BeastAct::Coil || W.beasts[gd].act == BeastAct::Strike) struck = true;
        }
        if (!hidden) fail("a stone guardian never settled into the rubble");
        if (!struck) fail("a hidden guardian never struck at a shrimp passing close");
    }
    // 3) a Lost One shuffles toward a noise
    {
        PlatformState p; Bench(p, PL_ATLANTIS, 40, 14); BenchBuild(p);
        BeastWorld& W = p.fauna;
        int lo = Put(W, AS_LOSTONE, {10 * TILE, 11 * TILE + 22}); W.beasts[lo].pers.curiosity = 0.9f;
        W.sounds.push_back({{16 * TILE, 11 * TILE}, 1.0f, 0.4f, -1});
        bool heard = false;
        for (int f = 0; f < 60 * 3 && !heard; f++) { BeastsUpdate(p, 1 / 60.0f); if (W.beasts[lo].act == BeastAct::Investigate) heard = true; }
        if (!heard) fail("a Lost One never went after a noise");
    }
    if (ok) TraceLog(LOG_WARNING, "verify-atlantis-ecosystem: OK - lure and catch, the guardian's ambush, and the Lost Ones hunting by sound all confirmed");
    return ok;
}
// ============================================================ not yet moved over (their old overlays still run)
}  // namespace bk

bool VerifyBeastBiome(int level) {
    switch (level) {
    case PL_PIRATE: return bk::VerifyPirate();
    case PL_ISLAND: return bk::VerifyIsland();
    case PL_CAVE: return bk::VerifyCave();
    case PL_PIPES: return bk::VerifyPipes();
    case PL_WEEDS: return bk::VerifyWeeds();
    case PL_ATLANTIS: return bk::VerifyAtlantis();
    default: return false;
    }
}
