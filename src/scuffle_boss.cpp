// Scuffle stage 8: Boss Arena (doc p. 16). Everyone against a Depth boss on its own stage: the Lobster (claws sweep;
// crystal falls), the Kraken (arms over the edge; the eye is the weak point), the Wyrm (surfaces from grates), the Sun
// God (fire beams), the Goliath (inhales), the Bouncer (throws). Each has three phases (at two thirds and a third of its
// health), and the stage changes with them. Headless and deterministic: the boss is a state machine stepped with the
// world, its body a set of hit circles (`BossPart`: armour takes a quarter, the weak point double) that shots, blasts,
// fists, kicks and swings reach through `BossStrike`. Every attack has a tell, and while it's coming the boss marks
// where it will land (`Boss::danger`): the scene draws the tells, the bots step out of the marks.
#include "scuffle.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace sf {

const char* BossName(int b) {
    static const char* N[BK_COUNT] = {"the Lobster", "the Kraken", "the Wyrm", "the Sun God", "the Goliath", "the Bouncer"};
    return b >= 0 && b < BK_COUNT ? N[b] : "the boss";
}
int BossOfWorld(int world) {
    static const int B[WD_COUNT] = {BK_KRAKEN, BK_LOBSTER, BK_SUN_GOD, BK_WYRM, BK_GOLIATH, BK_BOUNCER};
    return world >= 0 && world < WD_COUNT ? B[world] : BK_LOBSTER;
}
static int WorldOfBoss(int b) { static const int W[BK_COUNT] = {WD_CAVE, WD_NAUTILUS, WD_ATLANTIS, WD_REEF, WD_VOID, WD_SALON}; return W[std::clamp(b, 0, BK_COUNT - 1)]; }

// ---------------------------------------------------------------- the arenas (32 x 18 tiles, built here: y counts up from the floor)
namespace {
struct Arena {
    int w = 32, h = 18; std::vector<std::string> g;
    Arena() { g.assign(h, std::string(w, '.')); }
    char& at(int x, int y) { return g[h - 1 - y][x]; }
    Arena& fill(int x0, int y0, int x1, int y1, char c = '#') { for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) at(x, y) = c; return *this; }
    Arena& spawn(int x, int y) { at(x, y) = 'S'; return *this; }
    Arena& box() { fill(0, 0, 31, 0); fill(0, 0, 0, 17); fill(31, 0, 31, 17); return *this; }
    Stage make(const char* name, int world) { Stage s = StageFromText(g, name); s.world = world; return s; }
};
}
Stage BossArena(int kind) {
    Arena a;
    switch (kind) {
    case BK_LOBSTER:   // the Cave: crystal in the roof; the Lobster holds the right third, ledges on the left to get over its claws
        a.box().fill(0, 17, 31, 17).fill(2, 16, 6, 16, 'x').fill(11, 16, 15, 16, 'x').fill(20, 16, 24, 16, 'x');
        a.fill(2, 4, 7, 4).fill(9, 8, 14, 8, '-').fill(3, 11, 7, 11, '-').fill(16, 11, 20, 11);
        a.spawn(3, 1).spawn(6, 1).spawn(9, 1).spawn(12, 1);
        return a.make("The Lobster's Grotto", WD_CAVE);
    case BK_KRAKEN:   // the Nautilus's deck at sea: water both sides of the hull, rigging overhead, ropes to climb back aboard
        a.fill(0, 0, 31, 0).fill(1, 1, 30, 3, '~').fill(0, 1, 0, 6).fill(31, 1, 31, 6);
        a.fill(5, 4, 26, 4).fill(13, 5, 18, 6).fill(2, 4, 3, 4, '-').fill(28, 4, 29, 4, '-');
        a.fill(7, 8, 11, 8, '-').fill(20, 8, 24, 8, '-').fill(13, 11, 18, 11, '-');
        a.spawn(8, 5).spawn(11, 5).spawn(20, 5).spawn(23, 5);
        return a.make("The Open Deck", WD_NAUTILUS);
    case BK_WYRM:   // Atlantis's cistern: four grates in the floor (drawn by the boss), terraces, a high bridge
        a.box().fill(1, 5, 6, 5).fill(25, 5, 30, 5).fill(10, 9, 21, 9, '-').fill(14, 13, 17, 13);
        a.spawn(2, 1).spawn(9, 1).spawn(22, 1).spawn(29, 1);
        return a.make("The Cistern", WD_ATLANTIS);
    case BK_SUN_GOD:   // the Reef in sunlight: wooden scaffolds (they burn in the last phase), ropes high up
        a.box().fill(3, 4, 9, 4, 'w').fill(22, 4, 28, 4, 'w').fill(12, 8, 19, 8, 'w').fill(5, 11, 9, 11, '-').fill(22, 11, 26, 11, '-');
        a.spawn(2, 1).spawn(10, 1).spawn(21, 1).spawn(29, 1);
        return a.make("The Sunlit Shelf", WD_REEF);
    case BK_GOLIATH:   // the Void: the Goliath's head fills the right; ledges and ropes on the left to hang on to
        a.box().fill(1, 4, 7, 4).fill(4, 8, 10, 8, '-').fill(10, 12, 15, 12, '-').fill(13, 5, 17, 5);
        a.spawn(2, 1).spawn(5, 1).spawn(8, 1).spawn(11, 1);
        return a.make("The Goliath's Gape", WD_VOID);
    default:   // the Bouncer: the Salon after closing: tables, a chandelier, the balconies
        a.box().fill(0, 17, 31, 17).fill(5, 4, 9, 4, 'w').fill(22, 4, 26, 4, 'w').fill(12, 8, 19, 8, '-').fill(1, 11, 6, 11).fill(25, 11, 30, 11);
        a.spawn(3, 1).spawn(8, 1).spawn(23, 1).spawn(28, 1);
        return a.make("The Salon, After Hours", WD_SALON);
    }
}
Boss MakeBoss(int kind, int players, const Stage& s) {
    Boss B; B.kind = kind; B.face = -1; B.next = 2.0f;
    static const float BASE[BK_COUNT] = {2000, 1900, 2000, 1300, 2100, 2600};
    B.maxHp = B.hp = BASE[std::clamp(kind, 0, BK_COUNT - 1)] * (0.5f + 0.5f * std::max(1, players));
    switch (kind) {
    case BK_LOBSTER: B.pos = {14.8f, 0.6f}; break;
    case BK_KRAKEN: B.pos = {s.Width() / 2, 1.0f}; B.s[0] = 0; B.s[1] = 3.0f; B.side = 1; break;   // (s0: the eye's rise 0..1; s1: until it surfaces)
    case BK_WYRM: B.pos = {s.Width() / 2, -2}; break;
    case BK_SUN_GOD: B.pos = {s.Width() / 2, 8.2f}; break;
    case BK_GOLIATH: B.pos = {16.8f, 0.6f}; break;
    default: B.pos = {s.Width() - 3.0f, 0.6f}; break;
    }
    return B;
}

// ---------------------------------------------------------------- the boss's side of the world
bool World::BossTouch(Vector2 at, float r) const {
    if (boss.kind < 0 || boss.dead) return false;
    for (const auto& p : boss.parts) if (p.mul > 0 && Vector2Distance(at, p.p) < p.r + r) return true;
    return false;
}
bool World::BossStrike(Vector2 at, float r, float dmg, int by, bool splash) {
    Boss& B = boss;
    if (B.kind < 0 || B.dead || dmg <= 0) return false;
    int bi = -1; float bd = 1e9f;
    for (int i = 0; i < (int)B.parts.size(); i++) { const BossPart& p = B.parts[i]; if (p.mul <= 0) continue; float d = Vector2Distance(at, p.p) - p.r; if (d < r && (bi < 0 || p.mul > B.parts[bi].mul || (p.mul == B.parts[bi].mul && d < bd))) { bd = d; bi = i; } }
    if (bi < 0) return false;
    const BossPart& p = B.parts[bi];
    float f = splash ? 1 - std::clamp(bd / std::max(0.1f, r), 0.0f, 1.0f) * 0.6f : 1.0f;
    float dd = dmg * p.mul * f;
    B.hp -= dd; B.hitT = 0.12f;
    if (by >= 0 && by < MAX_STICKS) B.dmgBy[by] += dd;
    Emit(EV_HIT, at, -1, by, p.mul >= 1.5f ? 2.0f : p.mul < 0.5f ? 0.5f : 1.0f);
    if (B.hp <= 0) { B.hp = 0; B.dead = true; B.deadT = 0; B.act = 0; B.danger.clear(); Emit(EV_EVENT, B.pos, -1, by, 410); }
    return true;
}

namespace {
bool Live(const Stick& k) { return k.alive && k.present && k.finished < 0; }
float Ease(float u) { u = std::clamp(u, 0.0f, 1.0f); return u * u * (3 - 2 * u); }
Vector2 Bez(Vector2 a, Vector2 c, Vector2 b, float u) { float v = 1 - u; return {v * v * a.x + 2 * v * u * c.x + u * u * b.x, v * v * a.y + 2 * v * u * c.y + u * u * b.y}; }
// a stick's body near a circle (head, neck, pelvis, knees)
bool Touches(const Stick& k, Vector2 c, float r) { for (int j : {J_HEAD, J_NECK, J_PELVIS, J_KNEE_L, J_KNEE_R}) if (Vector2Distance(k.pt[j].p, c) < r + k.pt[j].r) return true; return false; }
float SegDist(Vector2 p, Vector2 a, Vector2 b) { Vector2 ab = Vector2Subtract(b, a); float L2 = Vector2LengthSqr(ab); float u = L2 > 0 ? std::clamp(Vector2DotProduct(Vector2Subtract(p, a), ab) / L2, 0.0f, 1.0f) : 0; return Vector2Distance(p, Vector2Add(a, Vector2Scale(ab, u))); }
// the nearest living stick to a point (or -1), and a ballistic throw from a to b in t seconds under g
int Nearest(const World& w, Vector2 p) { int bi = -1; float bd = 1e9f; for (const auto& k : w.sticks) if (Live(k)) { float d = Vector2Distance(k.pt[J_PELVIS].p, p); if (d < bd) { bd = d; bi = k.id; } } return bi; }
Vector2 Lob(Vector2 a, Vector2 b, float t, float g) { return {(b.x - a.x) / t, (b.y - a.y + 0.5f * g * t * t) / t}; }
}

// the boss hurts what touches it (each stick at most every 0.6 s), and its blows name it as the cause
static void HurtNear(World& w, Vector2 c, float r, float dmg, float knock, Vector2 dir) {
    for (auto& k : w.sticks) {
        if (!Live(k) || k.hazT > 0 || !Touches(k, c, r)) continue;
        Vector2 d = Vector2Length(dir) > 0.01f ? dir : Vector2Normalize(Vector2Add(Vector2Subtract(k.pt[J_PELVIS].p, c), {0, 0.5f}));
        w.Hit(k, -1, dmg, d, knock, knock >= 8, BossName(w.boss.kind)); k.hazT = 0.6f;
    }
}
static void Shot(World& w, Vector2 at, Vector2 v, float dmg, float knock, float grav) { int b = w.HazardBullet(-4, at, v, dmg, knock, grav); w.bullets[b].life = 5; }
// a falling thing's tell (dust from the roof, a shadow on the floor): it drops when the time runs out
static void Drop(World& w, float x, float tell) { w.boss.marks.push_back({x, w.stage.Height() - TILE * 1.6f, tell}); }

// ---------------------------------------------------------------- the phase changes: the stage changes with them
static void PhaseChange(World& w) {
    Boss& B = w.boss; Stage& s = w.stage;
    auto clear = [&](int x0, int y0, int x1, int y1) { for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) if (s.At(x, y) != T_EMPTY) { s.Set(x, y, T_EMPTY); w.Emit(EV_HIT, {(x + 0.5f) * TILE, (y + 0.5f) * TILE}, -1, -1, 0); } };
    auto set = [&](int x0, int y0, int x1, int y1, uint8_t t) { for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) s.Set(x, y, t); };
    switch (B.kind) {
    case BK_LOBSTER:   // the roof cracks (more crystal), then the rope in the middle snaps and the low ledge crumbles
        if (B.phase == 1) { set(7, 16, 10, 16, T_CRYSTAL); set(16, 16, 19, 16, T_CRYSTAL); }
        else { clear(9, 8, 14, 8); set(2, 4, 7, 4, T_CRUMBLE); }
        break;
    case BK_KRAKEN:   // the deckhouse is torn off, then the deck breaks in two (the eye can come up through the gap)
        if (B.phase == 1) clear(13, 5, 18, 6);
        else clear(14, 4, 17, 4);
        break;
    case BK_WYRM:   // the terraces crack (they crumble under you), then the cistern floods
        if (B.phase == 1) { set(1, 5, 6, 5, T_CRUMBLE); set(25, 5, 30, 5, T_CRUMBLE); }
        else w.floodY = 1.15f;
        break;
    case BK_SUN_GOD:   // the high ropes burn away, then every scaffold catches
        if (B.phase == 1) { clear(5, 11, 9, 11); clear(22, 11, 26, 11); }
        else { if (w.fireT.size() != s.t.size()) w.fireT.assign(s.t.size(), 0); for (int i = 0; i < (int)s.t.size(); i++) if (s.t[i] == T_WOOD && w.fireT[i] <= 0) w.fireT[i] = STEP * (1 + (i % 7)); }
        break;
    case BK_GOLIATH:   // the ledge before its mouth goes in, then the low rope (fewer things to hold on to)
        if (B.phase == 1) clear(13, 5, 17, 5);
        else clear(4, 8, 10, 8);
        break;
    default:   // the Bouncer throws the tables, then brings the chandelier down
        if (B.phase == 1) { clear(5, 4, 9, 4); clear(22, 4, 26, 4); }
        else { clear(12, 8, 19, 8); for (int i = 0; i < 4; i++) Drop(w, 7.5f + i * 1.3f, 0.6f + i * 0.15f); }
        break;
    }
}

// ---------------------------------------------------------------- the six bosses
// Each sets its parts (and what they hurt) from its pose, its danger marks while an attack is coming, and its next act.
static void StepLobster(World& w, Boss& B) {
    const float dt = STEP; float spd = B.phase == 2 ? 1.35f : B.phase == 1 ? 1.15f : 1.0f;
    Vector2 P = B.pos; int tgt = B.target;
    Vector2 rest{P.x - 2.1f, 1.3f}, claw = rest, pinch{P.x - 1.6f, 2.5f};
    float clawHurt = 0;
    B.actT += dt * spd;
    switch (B.act) {
    case 0:   // idle: shuffle to face the target, then choose
        if (tgt >= 0) { float want = std::clamp(w.sticks[tgt].pos.x + 6.5f, B.phase == 2 ? 10.0f : 12.5f, 14.8f); B.pos.x += std::clamp(want - P.x, -1.0f, 1.0f) * dt * (B.phase == 2 ? 2.2f : 1.0f); }
        if ((B.next -= dt * spd) <= 0 && tgt >= 0) {
            const Stick& o = w.sticks[tgt]; float dx = P.x - o.pos.x; int r = (int)(w.Rand() * 10);
            B.act = o.pos.y < 2.0f && dx < 12 && r < 5 ? 1 : (B.phase >= 1 && r >= 7) ? 3 : 2; B.actT = 0; B.at = o.pt[J_PELVIS].p; B.count = 0;
        }
        claw.y += sinf(B.t * 3) * 0.1f;
        break;
    case 1: {   // the sweep: drawn back (0.9 s, shaking), across the floor (0.7 s), back (0.6 s); phase 3 sweeps twice
        float tell = 0.9f, go = 0.7f, back = 0.6f;
        if (B.actT < tell) { claw = {P.x - 1.1f + sinf(B.t * 40) * 0.05f, 1.0f}; B.danger.push_back({1.0f, 0.6f, P.x - 1.6f, 1.2f}); }
        else if (B.actT < tell + go) { float u = (B.actT - tell) / go; claw = {Lerp(P.x - 1.6f, 1.4f, Ease(u)), 1.0f}; clawHurt = 30; B.danger.push_back({claw.x - 1.6f, 0.6f, 3.2f, 1.2f}); }
        else if (B.actT < tell + go + back) { float u = (B.actT - tell - go) / back; claw = {Lerp(1.4f, rest.x, Ease(u)), Lerp(1.0f, rest.y, u)}; }
        else if (B.phase == 2 && B.count++ == 0) B.actT = tell * 0.5f;
        else { B.act = 0; B.next = B.phase == 2 ? 0.7f : 1.3f; }
        break;
    }
    case 2: {   // the slam: the claw rises over the target (tracking for 0.8 s), comes down, the roof sheds crystal
        float tell = 0.8f;
        if (B.actT < tell) { if (tgt >= 0) B.at.x = Lerp(B.at.x, w.sticks[tgt].pos.x, 0.06f); float x = std::clamp(B.at.x, 1.2f, P.x - 1.4f); B.at.x = x; claw = {x, Lerp(rest.y, 5.2f, Ease(B.actT / 0.4f))}; B.danger.push_back({x - 1.3f, 0.6f, 2.6f, 4.6f}); }
        else if (B.actT < tell + 0.12f) { float u = (B.actT - tell) / 0.12f; claw = {B.at.x, Lerp(5.2f, 1.0f, u)}; clawHurt = 40; }
        else if (B.actT < tell + 0.7f) {
            claw = {B.at.x, 1.0f}; if (B.actT - dt * spd < tell + 0.12f) { w.Emit(EV_EXPLODE, {B.at.x, 0.7f}, -1, -1, 0.9f); clawHurt = 40; for (int i = 0; i < 2 + B.phase; i++) Drop(w, 1.2f + w.Rand() * (P.x - 2.6f), 0.7f + i * 0.2f); }
        }
        else if (B.actT < tell + 1.2f) { float u = (B.actT - tell - 0.7f) / 0.5f; claw = {Lerp(B.at.x, rest.x, Ease(u)), Lerp(1.0f, rest.y, u)}; }
        else { B.act = 0; B.next = B.phase == 2 ? 0.6f : 1.2f; }
        break;
    }
    case 3: {   // bubbles: three from the mouth at the target (0.4 s tell)
        if (B.actT >= 0.4f + B.count * 0.25f && B.count < 3 && tgt >= 0) { Vector2 m{P.x - 1.5f, 1.8f}; Vector2 d = Vector2Normalize(Vector2Subtract(w.sticks[tgt].pt[J_NECK].p, m)); Shot(w, m, Vector2Scale(d, 7), 18, 5, 0); B.count++; }
        if (B.actT > 1.4f) { B.act = 0; B.next = 1.0f; }
        break;
    }
    }
    // the body: the shell (armour), the head, the eyes on their stalks (the weak point), the claws
    P = B.pos; float bob = sinf(B.t * 2.2f) * 0.06f;
    B.parts = {{{P.x, 1.6f + bob}, 1.05f, 0.25f, 0}, {{P.x + 1.1f, 1.45f + bob}, 0.9f, 0.25f, 0}, {{P.x + 2.0f, 1.1f}, 0.65f, 0.25f, 0},
               {{P.x - 1.05f, 1.75f + bob}, 0.55f, 1.0f, 0}, {{P.x - 1.25f, 2.75f + bob}, 0.26f, 2.0f, 0}, {{P.x - 0.85f, 2.85f + bob}, 0.26f, 2.0f, 0},
               {claw, 0.62f, clawHurt > 0 ? 0.25f : 0.6f, clawHurt}, {pinch, 0.42f, 0.4f, 0}};
    B.chain = {claw, pinch};   // (for the drawing)
}

static void StepKraken(World& w, Boss& B) {
    const float dt = STEP; float spd = B.phase == 2 ? 1.3f : B.phase == 1 ? 1.12f : 1.0f;
    float W = w.stage.Width(), deckY = 3.0f;
    // the eye: comes up beside the hull (or, once the deck breaks, through the gap), looks, goes down
    float up = B.phase == 0 ? 4.5f : B.phase == 1 ? 3.6f : 3.0f;
    if (B.s[0] <= 0 && (B.s[1] -= dt) <= 0) { B.s[0] = 0.001f; B.s[2] = 0; int n = B.phase == 2 ? 3 : 2; B.side = (B.side + 1 + (int)(w.Rand() * (n - 1))) % n; }
    if (B.s[0] > 0) { B.s[2] += dt; B.s[0] = B.s[2] < 0.8f ? B.s[2] / 0.8f : B.s[2] < 0.8f + up ? 1 : std::max(0.0f, 1 - (B.s[2] - 0.8f - up) / 0.7f); if (B.s[2] > 1.5f + up) { B.s[0] = 0; B.s[1] = B.phase == 2 ? 3.5f : 5.0f; } }
    float ex = B.side == 0 ? 1.7f : B.side == 1 ? W - 1.7f : W / 2;
    Vector2 eye{ex, Lerp(0.4f, B.side == 2 ? 3.4f : 3.9f, Ease(B.s[0]))};
    B.pos = eye;
    // the arms (two at most): the first is the act (1 a slam: rising over the edge as its shadow grows, down across the
    // deck, lying there, back; 3 a sweep at head height); the second, from phase 2, a slam on another stick
    B.parts.clear(); B.chain.clear(); B.danger.clear();
    if (B.s[0] > 0.35f) { B.parts.push_back({eye, 0.78f, 2.0f, 0}); B.parts.push_back({{eye.x, eye.y + 0.95f}, 0.6f, 0.15f, 0}); }
    if ((B.next -= dt * spd) <= 0 && B.target >= 0) {
        int arms = B.phase >= 1 ? 2 : 1; int r = (int)(w.Rand() * 10);
        for (int a = 0; a < arms; a++) {
            int tg = a == 0 ? B.target : Nearest(w, {W - w.sticks[B.target].pos.x, 3});
            float tx = tg >= 0 ? std::clamp(w.sticks[tg].pos.x, 3.4f, W - 3.4f) : W / 2;
            if (a == 0) { B.act = B.phase >= 1 && r < 4 ? 3 : 1; B.actT = 0; B.at.x = tx; B.face = tx < W / 2 ? -1 : 1; }
            else { B.s[5] = tx; B.s[6] = 1; B.s[7] = 0; }   // (the second arm: s5 its mark, s6 on, s7 its clock)
        }
        B.next = 99;
    }
    auto arm = [&](int mode, float t, float tx, int side) {
        // base below the gunwale on its side, over the edge, to the tip
        float ex2 = side < 0 ? 2.9f : W - 2.9f; Vector2 base{side < 0 ? 0.6f : W - 0.6f, 0.4f}, lip{ex2, deckY + 0.8f}, tip;
        float hurt = 0; bool done = false;
        if (mode == 1) {   // rise (1.0 s), slam (0.15), lie there (1.3), go back (0.6)
            if (t < 1.0f) { tip = {tx, Lerp(deckY + 0.8f, 7.2f, Ease(t / 0.6f))}; B.danger.push_back({tx - 1.6f, deckY, 3.2f, 4.5f}); }
            else if (t < 1.15f) { tip = {tx, Lerp(7.2f, deckY + 0.35f, (t - 1.0f) / 0.15f)}; hurt = 40; }
            else if (t < 2.45f) { tip = {tx, deckY + 0.35f}; hurt = t < 1.3f ? 40 : 0; if (t - dt < 1.15f) w.Emit(EV_EXPLODE, tip, -1, -1, 0.8f); }
            else if (t < 3.05f) { float u = (t - 2.45f) / 0.6f; tip = Vector2Lerp({tx, deckY + 0.35f}, {ex2, 0.5f}, Ease(u)); }
            else done = true;
        } else {   // the sweep: up at the edge (0.8 s), then across the deck at head height (0.9), then back over the side
            float far = side < 0 ? W - 3.2f : 3.2f;
            if (t < 0.8f) { tip = {ex2 + side * -0.4f, Lerp(deckY, deckY + 1.6f, Ease(t / 0.5f))}; float x0 = std::min(ex2, far), x1 = std::max(ex2, far); B.danger.push_back({x0, deckY + 0.6f, x1 - x0, 1.4f}); }
            else if (t < 1.7f) { float u = (t - 0.8f) / 0.9f; tip = {Lerp(ex2, far, Ease(u)), deckY + 1.45f}; hurt = 32; B.danger.push_back({tip.x - 1.6f, deckY + 0.6f, 3.2f, 1.4f}); }
            else if (t < 2.4f) { float u = (t - 1.7f) / 0.7f; tip = Vector2Lerp({far, deckY + 1.45f}, {ex2, 0.5f}, Ease(u)); }
            else done = true;
        }
        if (done) return false;
        Vector2 ctrl = mode == 1 && t < 1.0f ? Vector2{lip.x, tip.y} : Vector2{(lip.x + tip.x) / 2, std::max(lip.y, tip.y) + 0.8f};
        for (int i = 0; i <= 9; i++) {
            float u = i / 9.0f; Vector2 p = u < 0.3f ? Vector2Lerp(base, lip, u / 0.3f) : Bez(lip, ctrl, tip, (u - 0.3f) / 0.7f);
            p.y += sinf(B.t * 5 + i * 0.7f) * 0.06f * (1 - u);
            B.chain.push_back(p);
            if (i >= 3) B.parts.push_back({p, 0.42f - i * 0.018f, 0.35f, hurt});
        }
        return true;
    };
    if (B.act) {
        B.actT += dt * spd;
        if (!arm(B.act, B.actT, B.at.x, B.face)) { B.act = 0; if (B.s[6] == 0) B.next = B.phase == 2 ? 1.0f : 1.6f; }
    }
    if (B.s[6] > 0) {
        B.s[7] += dt * spd; int side = B.s[5] < W / 2 ? -1 : 1;   // (the second arm: from the side its target is on)
        if (!arm(1, B.s[7], B.s[5], side)) { B.s[6] = 0; B.s[7] = 0; if (!B.act) B.next = B.phase == 2 ? 1.0f : 1.6f; }
    }
    if (!B.act && B.s[6] == 0 && B.next > 50) B.next = 1.0f;
    for (const auto& p : B.parts) if (p.hurt > 0) HurtNear(w, p.p, p.r, p.hurt, 12, Vector2Normalize({p.p.x < W / 2 ? 0.6f : -0.6f, 0.8f}));
}

static void StepWyrm(World& w, Boss& B) {
    const float dt = STEP; float spd = B.phase == 2 ? 1.35f : B.phase == 1 ? 1.15f : 1.0f;
    static const float GRATE[4] = {2.85f, 7.65f, 11.55f, 16.35f};   // (the grates' middles, x in metres)
    B.parts.clear(); B.danger.clear();
    auto nearestGrate = [&](float x) { int bi = 0; for (int i = 1; i < 4; i++) if (fabsf(GRATE[i] - x) < fabsf(GRATE[bi] - x)) bi = i; return bi; };
    B.actT += dt * spd;
    // the body follows the head's path (kept in chain, newest first)
    auto body = [&](float hurt) {
        float seg = 0.5f; int n = 14; float acc = 0; size_t j = 0;
        for (int i = 1; i <= n && j + 1 < B.chain.size(); i++) {
            float want = i * seg;
            while (j + 1 < B.chain.size() && acc + Vector2Distance(B.chain[j], B.chain[j + 1]) < want) { acc += Vector2Distance(B.chain[j], B.chain[j + 1]); j++; }
            if (j + 1 >= B.chain.size()) break;
            Vector2 p = B.chain[j]; if (p.y > 0.3f) B.parts.push_back({p, 0.46f - i * 0.012f, 0.6f, hurt});
        }
        if (B.chain.size() > 600) B.chain.resize(600);
    };
    switch (B.act) {
    case 0:   // under the floor: choose a grate near the target
        B.pos = {B.pos.x, -2};
        if ((B.next -= dt * spd) <= 0 && B.target >= 0) {
            float tx = w.sticks[B.target].pos.x; int g1 = nearestGrate(tx), r = (int)(w.Rand() * 10);
            B.act = r < 3 ? 2 : 1; B.actT = 0; B.at = {GRATE[g1], 0.6f};
            int g2 = g1; while (g2 == g1 || abs(g2 - g1) > 2) g2 = (int)(w.Rand() * 4) % 4;
            B.from = {GRATE[g2], 0.6f}; B.chain.clear(); B.count = 0;
        }
        break;
    case 1: {   // the leap: the grate rattles (1.0 s), it bursts up and arcs over into another grate (1.4 s), the tail follows
        float tell = 1.0f, arc = B.phase == 2 ? 1.05f : 1.4f, apex = 6.2f + B.phase * 0.6f;
        if (B.actT < tell) B.danger.push_back({B.at.x - 1.1f, 0.6f, 2.2f, 5.5f});
        else if (B.actT < tell + arc) {
            float u = (B.actT - tell) / arc; Vector2 a = {B.at.x, -0.6f}, b = {B.from.x, -0.6f};
            B.pos = {Lerp(a.x, b.x, u), Lerp(a.y, b.y, u) + 4 * apex * u * (1 - u)};
            B.chain.insert(B.chain.begin(), B.pos);
            B.danger.push_back({B.from.x - 1.1f, 0.6f, 2.2f, 3.0f});
            if (B.phase >= 1 && B.count < 3 && u > 0.4f + B.count * 0.08f && B.target >= 0) { Vector2 d = Vector2Normalize(Vector2Subtract(w.sticks[B.target].pt[J_NECK].p, B.pos)); Shot(w, B.pos, Vector2Scale(d, 8), 16, 4, w.gravity * 0.3f); B.count++; }
        } else if (B.actT < tell + arc + 1.0f) { B.pos = {B.from.x, -1.5f}; B.chain.insert(B.chain.begin(), B.pos); }
        else { B.chain.clear(); B.act = 0; B.next = B.phase == 2 && B.s[0]++ < 1 ? 0.3f : B.phase == 2 ? 1.2f : 1.8f; if (B.s[0] > 1) B.s[0] = 0; }
        if (B.act) {
            if (B.pos.y > 0.2f) B.parts.insert(B.parts.begin(), {B.pos, 0.62f, 2.0f, 35});
            body(16);
        }
        break;
    }
    case 2: {   // it rears up out of a grate (0.9 s tell), looks round spitting acid for 2.4 s (its head is open), and sinks
        float tell = 0.9f, stay = 2.4f;
        if (B.actT < tell) { B.danger.push_back({B.at.x - 1.1f, 0.6f, 2.2f, 4.0f}); B.pos = {B.at.x, -1}; }
        else {
            float u = B.actT - tell; float h = u < 0.4f ? Ease(u / 0.4f) : u < 0.4f + stay ? 1 : 1 - Ease((u - 0.4f - stay) / 0.5f);
            B.pos = {B.at.x + sinf(B.t * 1.7f) * 0.5f * h, Lerp(-0.6f, 3.9f, h)};
            B.chain.clear(); for (int i = 0; i < 60; i++) B.chain.push_back({B.at.x + sinf(B.t * 1.7f - i * 0.05f) * 0.5f * h * (1 - i / 60.0f), B.pos.y - i * 0.08f});
            if (u > 0.5f && u < 0.4f + stay && B.target >= 0 && fmodf(u, B.phase == 2 ? 0.45f : 0.7f) < dt * spd) { Vector2 d = Vector2Normalize(Vector2Subtract(w.sticks[B.target].pt[J_NECK].p, B.pos)); Shot(w, B.pos, Vector2Scale(d, 9), 18, 5, w.gravity * 0.25f); }
            if (B.pos.y > 0.2f) B.parts.insert(B.parts.begin(), {B.pos, 0.62f, 2.0f, 28});
            body(14);
            if (u > 0.9f + stay) { B.chain.clear(); B.act = 0; B.next = B.phase == 2 ? 0.8f : 1.5f; }
        }
        break;
    }
    }
    for (const auto& p : B.parts) if (p.hurt > 0) HurtNear(w, p.p, p.r, p.hurt, 10, {});
}

static void StepSunGod(World& w, Boss& B) {
    const float dt = STEP; float spd = B.phase == 2 ? 1.3f : B.phase == 1 ? 1.12f : 1.0f;
    float W = w.stage.Width();
    B.pos.x = W / 2 + sinf(B.t * 0.35f) * 5.0f; B.pos.y = Lerp(B.pos.y, B.phase == 2 ? 6.8f : 8.2f, dt) + sinf(B.t * 1.3f) * 0.004f;
    Vector2 P = B.pos, handL{P.x - 1.6f, P.y - 0.5f}, handR{P.x + 1.6f, P.y - 0.5f};
    B.parts.clear(); B.danger.clear(); B.chain.clear();
    // the beams: from a hand to a point on the floor that sweeps under the target
    auto beam = [&](Vector2 hand, float t, float x0, float x1, int who) {
        float tell = 1.0f, fire = 0.9f;
        if (t < tell) {
            float x = who >= 0 ? w.sticks[who].pt[J_PELVIS].p.x : x0;
            B.chain.push_back(hand); B.chain.push_back({x, 0.6f}); B.chain.push_back({0, 0});   // (a thin line: the tell)
            B.danger.push_back({std::min(x0, x1) - 1.0f, 0.6f, fabsf(x1 - x0) + 2.0f, 6.0f});   // (the whole sweep to come)
            return true;
        }
        if (t >= tell + fire) return false;
        float u = (t - tell) / fire, x = Lerp(x0, x1, Ease(u));
        Vector2 end{x, 0.6f}, d = Vector2Normalize(Vector2Subtract(end, hand));
        for (float s = 0; s < 20; s += 0.15f) { Vector2 p = Vector2Add(hand, Vector2Scale(d, s)); if (w.stage.Solid((int)floorf(p.x / TILE), (int)floorf(p.y / TILE))) { end = p; break; } end = p; }
        B.chain.push_back(hand); B.chain.push_back(end); B.chain.push_back({1, 0});   // (the beam itself)
        B.danger.push_back({std::min(x, x1) - 1.0f, 0.6f, fabsf(x1 - x) + 2.0f, 6.0f});
        int tx = (int)floorf(end.x / TILE), ty = (int)floorf((end.y - 0.05f) / TILE), i = ty * w.stage.w + tx;
        if (tx >= 0 && ty >= 0 && tx < w.stage.w && ty < w.stage.h && w.stage.At(tx, ty) == T_WOOD) { if (w.fireT.size() != w.stage.t.size()) w.fireT.assign(w.stage.t.size(), 0); if (w.fireT[i] <= 0) w.fireT[i] = STEP; }
        for (auto& k : w.sticks) if (Live(k) && k.hazT <= 0) for (int j : {J_HEAD, J_NECK, J_PELVIS}) if (SegDist(k.pt[j].p, hand, end) < 0.3f) { w.Hit(k, -1, 24, Vector2Normalize({d.x, 0.6f}), 6, false, "the Sun God's beam"); w.Burn(k, 1.2f, -1); k.hazT = 0.5f; break; }
        return true;
    };
    B.actT += dt * spd;
    switch (B.act) {
    case 0:
        if ((B.next -= dt * spd) <= 0 && B.target >= 0) { int r = (int)(w.Rand() * 10); B.act = r < 6 ? 1 : 2; B.actT = 0; B.count = 0; B.s[0] = w.sticks[B.target].pos.x; int t2 = Nearest(w, {W - B.s[0], 1}); B.s[1] = t2 >= 0 ? w.sticks[t2].pos.x : W - B.s[0]; B.s[2] = (float)t2; }
        break;
    case 1: {   // the beam (both hands from phase 2)
        bool a = beam(B.s[0] < P.x ? handL : handR, B.actT, B.s[0] - 2.4f, B.s[0] + 2.4f, B.target);
        bool b = B.phase >= 1 && beam(B.s[1] < P.x ? handL : handR, B.actT, B.s[1] + 2.4f, B.s[1] - 2.4f, (int)B.s[2]);
        if (!a && !b) { B.act = 0; B.next = B.phase == 2 ? 0.8f : 1.4f; }
        break;
    }
    case 2: {   // the sun-rain: motes thrown up out of the halo, falling where the sticks are
        float tell = 0.6f; int n = 5 + 2 * B.phase;
        if (B.actT < tell) for (const auto& k : w.sticks) if (Live(k)) B.danger.push_back({k.pos.x - 1.0f, 0.6f, 2.0f, 3.0f});
        if (B.actT >= tell && B.count < n && B.actT >= tell + B.count * 0.12f) {
            int who = B.count % std::max(1, (int)w.sticks.size()); while (who < (int)w.sticks.size() && !Live(w.sticks[who])) who++;
            float x = who < (int)w.sticks.size() ? w.sticks[who].pos.x + (w.Rand() - 0.5f) * 3 : w.Rand() * W;
            Vector2 from{P.x + (w.Rand() - 0.5f) * 2, P.y + 1.2f}; Shot(w, from, Lob(from, {x, 0.8f}, 1.1f, w.gravity * 0.5f), 15, 5, w.gravity * 0.5f); B.count++;
        }
        if (B.actT > tell + n * 0.12f + 0.4f) { B.act = 0; B.next = B.phase == 2 ? 0.8f : 1.3f; }
        break;
    }
    }
    // the body: the face (the weak point), the core (open in the last phase), the halo's rays (armour)
    B.parts.push_back({{P.x, P.y + 0.85f}, 0.5f, 2.0f, 0});
    B.parts.push_back({P, 0.75f, B.phase == 2 ? 1.6f : 1.0f, 0});
    for (int i = 0; i < 8; i++) { float a = i * PI / 4 + B.t * 0.4f; B.parts.push_back({{P.x + cosf(a) * 1.7f, P.y + sinf(a) * 1.7f}, 0.32f, 0.2f, 0}); }
    B.parts.push_back({handL, 0.3f, 0.5f, 0}); B.parts.push_back({handR, 0.3f, 0.5f, 0});
}

static void StepGoliath(World& w, Boss& B) {
    const float dt = STEP; float spd = B.phase == 2 ? 1.25f : B.phase == 1 ? 1.1f : 1.0f;
    Vector2 P = B.pos; float lunge = B.s[1];
    Vector2 M{P.x - 2.8f - lunge, 2.4f};   // (the mouth)
    float open = B.s[0];
    B.danger.clear(); B.actT += dt * spd;
    switch (B.act) {
    case 0:
        open = Lerp(open, 0.15f, dt * 3);
        if ((B.next -= dt * spd) <= 0 && B.target >= 0) { int r = (int)(w.Rand() * 10); B.act = (B.phase >= 1 && r >= 7) ? 3 : r < 6 ? 1 : 2; B.actT = 0; B.count = 0; }
        break;
    case 1: {   // the inhale: the gills flare (0.8 s), then everything is pulled toward the mouth; inside it, it chews; at the end it snaps shut
        float tell = 0.8f, pull = B.phase == 2 ? 3.4f : 2.6f;
        if (B.actT < tell) { open = Lerp(open, 0.6f, dt * 4); B.danger.push_back({M.x - 4.0f, 0.6f, 4.5f, 4.5f}); }
        else if (B.actT < tell + pull) {
            open = Lerp(open, 1.0f, dt * 6);
            float force = B.phase == 2 ? 5.4f : B.phase == 1 ? 4.7f : 4.0f;
            for (auto& k : w.sticks) {
                if (!Live(k)) continue;
                Vector2 d = Vector2Subtract(M, k.pt[J_PELVIS].p); float L = Vector2Length(d); if (L < 0.05f) continue;
                float f = force * std::clamp(1.2f - L / 16, 0.25f, 1.0f) * (k.st == S_WALL || k.hookOn ? 0.35f : 1.0f);   // (holding a wall or a line: half the pull and more)
                w.Nudge(k, Vector2Scale(Vector2Normalize(d), f * dt));
                if (L < 1.25f && k.hazT <= 0) { w.Hit(k, -1, 12, {-1, 0.3f}, 2, false, "the Goliath"); k.hazT = 0.5f; }
            }
            for (auto& it : w.items) if (it.alive && it.holder < 0) { Vector2 d = Vector2Scale(Vector2Normalize(Vector2Subtract(M, it.a.p)), 2.5f * dt); it.a.p = Vector2Add(it.a.p, d); it.b.p = Vector2Add(it.b.p, d); }
            B.danger.push_back({M.x - 2.2f, 0.6f, 2.4f, 3.6f});
        } else if (B.actT < tell + pull + 0.25f) {
            open = Lerp(open, 0.0f, dt * 18);
            if (B.count++ == 0) { for (auto& k : w.sticks) if (Live(k) && Vector2Distance(k.pt[J_PELVIS].p, M) < 1.6f) { w.Hit(k, -1, 35, Vector2Normalize({-1, 0.5f}), 18, true, "the Goliath's jaws"); k.hazT = 0.6f; } w.Emit(EV_EXPLODE, M, -1, -1, 0.6f); }
        } else { B.act = B.phase >= 1 ? 2 : 0; B.actT = 0; B.count = 0; B.next = B.phase == 2 ? 0.8f : 1.5f; }
        break;
    }
    case 2: {   // the spit: a fan of hard bubbles
        float tell = 0.5f; open = Lerp(open, 0.7f, dt * 6);
        if (B.actT < tell) B.danger.push_back({M.x - 6.0f, M.y - 1.6f, 6.0f, 3.2f});
        else if (B.count == 0) { int n = 5 + B.phase; for (int i = 0; i < n; i++) { float a = PI + (i - (n - 1) * 0.5f) * 0.16f; Shot(w, M, {cosf(a) * 9, sinf(a) * 9 + 1}, 15, 9, 0); } B.count = 1; }
        if (B.actT > tell + 0.5f) { B.act = 0; B.next = B.phase == 2 ? 0.8f : 1.4f; }
        break;
    }
    case 3: {   // the lurch: it draws back (0.7 s) and rams 3.5 m forward, hangs there, draws back
        float tell = 0.7f;
        if (B.actT < tell) { lunge = Lerp(0, -0.6f, Ease(B.actT / tell)); B.danger.push_back({M.x - 4.4f, 0.6f, 4.6f, 4.2f}); }
        else if (B.actT < tell + 0.3f) { lunge = Lerp(-0.6f, 3.5f, Ease((B.actT - tell) / 0.3f)); }
        else if (B.actT < tell + 0.9f) lunge = 3.5f;
        else if (B.actT < tell + 1.7f) lunge = Lerp(3.5f, 0, Ease((B.actT - tell - 0.9f) / 0.8f));
        else { B.act = 0; B.next = 1.0f; lunge = 0; }
        open = Lerp(open, 0.3f, dt * 4);
        break;
    }
    }
    B.s[0] = open; B.s[1] = lunge;
    M = {P.x - 2.8f - lunge, 2.4f};
    float hx = P.x - lunge;
    bool ramming = B.act == 3 && B.actT > 0.7f && B.actT < 1.6f;
    B.parts = {{{hx + 0.2f, 2.6f}, 1.9f, 0.2f, ramming ? 35.0f : 0}, {{hx + 1.6f, 4.0f}, 1.7f, 0.2f, 0}, {{hx + 2.5f, 1.8f}, 1.6f, 0.2f, 0},
               {{hx - 1.0f, 4.4f}, 0.42f, 2.0f, 0},                                         // (the eye)
               {{M.x + 0.3f, M.y + 0.9f + open * 0.4f}, 0.5f, 0.5f, 0}, {{M.x + 0.3f, M.y - 0.9f - open * 0.4f}, 0.5f, 0.5f, 0}};   // (the lips)
    if (open > 0.5f) B.parts.push_back({{M.x + 0.6f, M.y}, 0.85f, 1.5f, 0});   // (inside the open mouth)
    if (ramming) for (const auto& p : B.parts) if (p.hurt > 0) HurtNear(w, {p.p.x - 1.2f, p.p.y - 0.8f}, p.r, p.hurt, 15, {-1, 0.5f});
}

static void StepBouncer(World& w, Boss& B) {
    const float dt = STEP; float spd = B.phase == 2 ? 1.3f : B.phase == 1 ? 1.12f : 1.0f;
    float W = w.stage.Width();
    B.danger.clear(); B.actT += dt * spd;
    float dazed = B.s[0];
    if (dazed > 0) B.s[0] = std::max(0.0f, dazed - dt);
    Vector2 fist{B.pos.x + B.face * 0.9f, 1.9f}; float fistHurt = 0;
    switch (B.act) {
    case 0: {
        if (dazed > 0) break;
        if (B.target >= 0) {
            const Stick& o = w.sticks[B.target]; float dx = o.pos.x - B.pos.x;
            B.face = dx < 0 ? -1 : 1;
            if (fabsf(dx) > 1.3f) B.pos.x += B.face * (B.phase == 2 ? 3.0f : 2.2f) * dt;
            B.s[1] += dt * 6;   // (the walk cycle)
            if ((B.next -= dt * spd) <= 0) {
                int r = (int)(w.Rand() * 10);
                B.act = fabsf(dx) < 1.9f && fabsf(o.pos.y - B.pos.y) < 1.6f ? 1 : (B.phase >= 1 && r < 4) ? 3 : (B.phase == 2 && r < 6) ? 4 : 2;
                B.actT = 0; B.count = 0; B.at = o.pt[J_PELVIS].p;
            }
        }
        break;
    }
    case 1: {   // the grab: arms out (0.45 s), whoever is in reach is lifted and thrown across the room
        float tell = 0.45f;
        fist = {B.pos.x + B.face * Lerp(0.6f, 1.5f, Ease(B.actT / tell)), 1.6f};
        if (B.actT < tell) B.danger.push_back({B.face > 0 ? B.pos.x : B.pos.x - 2.0f, 0.6f, 2.0f, 2.6f});
        else if (B.count == 0) {
            B.count = 1;
            for (auto& k : w.sticks) if (Live(k) && fabsf(k.pos.x - (B.pos.x + B.face * 1.0f)) < 1.1f && k.pos.y < 3.0f) { w.Hit(k, -1, 25, Vector2Normalize({(float)-B.face * 0.2f + (k.pos.x < W / 2 ? 1.0f : -1.0f), 0.9f}), 21, true, "the Bouncer"); k.hazT = 0.6f; }
        }
        if (B.actT > tell + 0.5f) { B.act = 0; B.next = B.phase == 2 ? 0.5f : 1.0f; }
        break;
    }
    case 2: {   // the throw: a stool lifted overhead (0.7 s) and lobbed at the target
        float tell = 0.7f; fist = {B.pos.x, Lerp(1.9f, 4.0f, Ease(B.actT / tell))};
        if (B.actT < tell && B.target >= 0) { Vector2 p = w.sticks[B.target].pos; B.danger.push_back({p.x - 1.2f, 0.6f, 2.4f, 2.4f}); B.at = p; }
        else if (B.count < (B.phase == 2 ? 2 : 1) && B.actT >= tell + B.count * 0.45f) {
            Vector2 from{B.pos.x, 4.0f}, to{B.at.x + (B.count ? (w.Rand() - 0.5f) * 3 : 0), 1.0f};
            float T = std::clamp(fabsf(to.x - from.x) / 9, 0.6f, 1.3f); Shot(w, from, Lob(from, to, T, w.gravity), 30, 12, w.gravity); B.count++;
        }
        if (B.actT > tell + 1.0f) { B.act = 0; B.next = B.phase == 2 ? 0.7f : 1.2f; }
        break;
    }
    case 3: {   // the charge: paws the floor (0.8 s), runs to the wall; a wall dazes him (his head's open for 2.2 s)
        float tell = 0.8f;
        if (B.actT < tell) { float x0 = B.face > 0 ? B.pos.x : 0.6f, x1 = B.face > 0 ? W - 0.6f : B.pos.x; B.danger.push_back({x0, 0.6f, x1 - x0, 3.2f}); }
        else {
            B.pos.x += B.face * 11 * dt; B.s[1] += dt * 14; fistHurt = 35;
            B.danger.push_back({B.face > 0 ? B.pos.x : B.pos.x - 3.0f, 0.6f, 3.0f, 3.2f});
            for (auto& k : w.sticks) if (Live(k) && k.hazT <= 0 && fabsf(k.pos.x - B.pos.x) < 0.9f && k.pos.y < 3.2f) { w.Hit(k, -1, 35, Vector2Normalize({(float)B.face, 0.6f}), 16, true, "the Bouncer's charge"); k.hazT = 0.8f; }
            if (B.pos.x < 1.4f || B.pos.x > W - 1.4f) { B.pos.x = std::clamp(B.pos.x, 1.4f, W - 1.4f); B.s[0] = 2.2f; B.act = 0; B.next = 2.4f; w.Emit(EV_EXPLODE, {B.pos.x + B.face * 0.8f, 2.4f}, -1, -1, 0.7f); Drop(w, B.pos.x - B.face * 2, 0.4f); }
        }
        break;
    }
    case 4: {   // the stomp: he leaps (0.6 s crouch) and lands with a shock along the floor
        float tell = 0.6f;
        if (B.actT < tell) B.danger.push_back({B.pos.x - 4.5f, 0.6f, 9.0f, 1.2f});
        else if (B.count == 0 && B.actT > tell + 0.5f) {
            B.count = 1; w.Emit(EV_EXPLODE, {B.pos.x, 0.7f}, -1, -1, 1.2f);
            for (auto& k : w.sticks) if (Live(k) && k.grounded && fabsf(k.pos.x - B.pos.x) < 4.5f && k.pos.y < 1.2f) { w.Hit(k, -1, 20, Vector2Normalize({k.pos.x < B.pos.x ? -0.4f : 0.4f, 1}), 12, true, "the Bouncer's stomp"); k.hazT = 0.6f; }
        }
        if (B.actT > tell + 1.0f) { B.act = 0; B.next = 1.0f; }
        break;
    }
    }
    B.pos.x = std::clamp(B.pos.x, 1.4f, W - 1.4f);
    float hop = B.act == 4 && B.actT > 0.6f && B.actT < 1.1f ? sinf((B.actT - 0.6f) / 0.5f * PI) * 1.6f : 0;
    Vector2 P{B.pos.x, 0.6f + hop};
    B.parts = {{{P.x + B.face * 0.1f, P.y + 3.05f}, 0.45f, dazed > 0 ? 3.0f : 2.0f, 0},   // (the head)
               {{P.x, P.y + 2.0f}, 0.78f, 1.0f, 0}, {{P.x, P.y + 1.05f}, 0.7f, 0.8f, 5},   // (the belly shoves whoever walks into it)
               {{P.x - 0.35f, P.y + 0.35f}, 0.35f, 0.5f, 0}, {{P.x + 0.35f, P.y + 0.35f}, 0.35f, 0.5f, 0},
               {fist, 0.34f, 0.5f, fistHurt}};
    B.chain = {fist};
}

void World::StepBoss() {
    Boss& B = boss;
    if (B.kind < 0) return;
    const float dt = STEP;
    B.t += dt; B.hitT = std::max(0.0f, B.hitT - dt); B.roarT = std::max(0.0f, B.roarT - dt);
    // falling things (crystal from the roof, the chandelier's pieces)
    for (auto& m : B.marks) { m.z -= dt; if (m.z <= 0) Shot(*this, {m.x, m.y}, {0, -2}, 28, 6, gravity); }
    B.marks.erase(std::remove_if(B.marks.begin(), B.marks.end(), [](const Vector3& m) { return m.z <= 0; }), B.marks.end());
    if (B.dead) { B.deadT += dt; for (auto& p : B.parts) { p.mul = 0; p.hurt = 0; } B.danger.clear(); return; }   // (the last pose stays for the drawing: it sinks, falls, fades)
    // the phases: at two thirds and a third of its health it roars and the stage changes
    int want = B.hp < B.maxHp * 0.33f ? 2 : B.hp < B.maxHp * 0.66f ? 1 : 0;
    if (want > B.phase) { B.phase = want; B.roarT = 1.4f; B.next = std::max(B.next, 1.6f); Emit(EV_EVENT, B.pos, -1, -1, 400 + (float)B.phase); PhaseChange(*this); }
    // its target: the nearest living stick (looked at again every 2.5 s)
    if (B.target < 0 || B.target >= (int)sticks.size() || !Live(sticks[B.target]) || fmodf(B.t, 2.5f) < dt) B.target = Nearest(*this, B.pos);
    if (B.roarT > 0) { B.danger.clear(); if (B.kind != BK_KRAKEN && B.kind != BK_WYRM) return; }
    B.danger.clear();
    switch (B.kind) {
    case BK_LOBSTER: StepLobster(*this, B); break;
    case BK_KRAKEN: StepKraken(*this, B); break;
    case BK_WYRM: StepWyrm(*this, B); break;
    case BK_SUN_GOD: StepSunGod(*this, B); break;
    case BK_GOLIATH: StepGoliath(*this, B); break;
    default: StepBouncer(*this, B); break;
    }
    for (const auto& m : B.marks) B.danger.push_back({m.x - 0.7f, 0.6f, 1.4f, stage.Height()});
    if (B.kind == BK_LOBSTER || B.kind == BK_BOUNCER) for (const auto& p : B.parts) if (p.hurt > 0) HurtNear(*this, p.p, p.r, p.hurt, p.hurt >= 20 ? 12.0f : 6.0f, {B.face * 1.0f, 0.5f});
}

// ---------------------------------------------------------------- the stage-8 checks (in --scuffle-test)
static int BF = 0;
static void BC(bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) BF++; }
// a boss fight with bots: how it ended, how long, the phase reached
struct FightResult { bool won = false; float secs = 0; int phase = 0, deaths = 0; float hpLeft = 1; };
static FightResult Fight(int kind, int players, int skill, uint32_t seed) {
    Match m; m.mode = MD_BOSS; m.world = WorldOfBoss(kind); m.Start(players, 1, seed);
    std::vector<uint32_t> r(players); for (int i = 0; i < players; i++) r[i] = seed * 31 + i * 7919;
    FightResult f;
    for (int s = 0; s < 120 * 400 && !m.Over(); s++) {
        for (int i = 0; i < players; i++) BotInput(m.w, i, m.w.sticks[i].in, r[i], skill);
        m.Step();
        if (m.phase == Match::P_FIGHT) { f.secs = m.w.t; f.phase = std::max(f.phase, m.w.boss.phase); f.hpLeft = m.w.boss.maxHp > 0 ? m.w.boss.hp / m.w.boss.maxHp : 1; }
    }
    f.won = m.gStage > 0; f.deaths = m.gDeaths;
    return f;
}
int ScuffleBossChecks() {
    BF = 0;
    printf("Scuffle: stage 8 (Boss Arena)\n");
    // every arena: the spawns reach each other; every boss is made and stands where it should
    for (int b = 0; b < BK_COUNT; b++) { Stage s = BossArena(b); Reach rc = CheckReachable(s); BC(rc.ok, TextFormat("%s's arena (%s) can be crossed%s", BossName(b), s.name.c_str(), rc.ok ? "" : (": " + rc.why).c_str())); }
    {   // a shot at the weak point does double, at the armour a quarter
        World w; w.Init(BossArena(BK_LOBSTER), 2, 5); w.mode = MD_BOSS; w.boss = MakeBoss(BK_LOBSTER, 2, w.stage); w.Step();
        float h0 = w.boss.hp; Vector2 eye = w.boss.parts[4].p; w.BossStrike(eye, 0.05f, 10, 0); float weak = h0 - w.boss.hp;
        float h1 = w.boss.hp; w.BossStrike(w.boss.parts[0].p, 0.05f, 10, 0); float armour = h1 - w.boss.hp;
        BC(fabsf(weak - 20) < 0.01f && fabsf(armour - 2.5f) < 0.01f, TextFormat("the Lobster's eye takes %.1f of 10, its shell %.1f", weak, armour));
        // a real bullet and a blast find it too
        w.boss.hp = w.boss.maxHp; Bullet b; b.p = {eye.x, eye.y + 1.5f}; b.v = {0, -40}; b.owner = 0; b.weapon = WeaponIndex("gaspistol"); b.dmg = 20; b.life = 1; w.bullets.push_back(b);
        for (int i = 0; i < 20; i++) w.StepBullets();
        BC(w.boss.hp < w.boss.maxHp - 30, TextFormat("a pistol shot finds the eye (%.0f off)", w.boss.maxHp - w.boss.hp));
        float h2 = w.boss.hp; w.Explode(w.boss.parts[3].p, 1.5f, 60, 5, 1, -1); BC(w.boss.hp < h2, "a blast hurts it");
        w.boss.hp = w.boss.maxHp * 0.6f; w.Step(); BC(w.boss.phase == 1, "below two thirds: phase 2 (and the roof cracks)");
    }
    {   // the phases change the stages
        int changed = 0;
        for (int b = 0; b < BK_COUNT; b++) {
            World w; w.Init(BossArena(b), 2, 9); w.mode = MD_BOSS; w.boss = MakeBoss(b, 2, w.stage); std::vector<uint8_t> before = w.stage.t; float fl = w.floodY; size_t fires = 0;
            w.boss.hp = w.boss.maxHp * 0.6f; w.Step(); w.boss.hp = w.boss.maxHp * 0.3f; w.Step();
            for (float f : w.fireT) fires += f > 0;
            if (w.stage.t != before || w.floodY != fl || fires > 0) changed++;
        }
        BC(changed == BK_COUNT, TextFormat("every boss's phases change its stage (%d of %d)", changed, BK_COUNT));
    }
    // the gate: a duo of bots beats the Lobster
    {
        int wins = 0, n = 8; float secs = 0; int deaths = 0;
        for (int i = 0; i < n; i++) { FightResult f = Fight(BK_LOBSTER, 2, 2, 100 + i * 17); wins += f.won; secs += f.secs; deaths += f.deaths; }
        BC(wins * 2 >= n, TextFormat("a duo of Sharp bots beats the Lobster (%d of %d fights, %.0f s on average, %.1f deaths a fight)", wins, n, secs / n, deaths / (float)n));
    }
    // every boss: a fight with four bots ends, both ways possible
    for (int b = 0; b < BK_COUNT; b++) {
        int wins = 0, n = 3, deaths = 0; float secs = 0; int ph = 0;
        for (int i = 0; i < n; i++) { FightResult f = Fight(b, 4, 2, 300 + i * 13 + b); wins += f.won; secs += f.secs; ph = std::max(ph, f.phase); deaths += f.deaths; }
        BC(wins >= 1 && ph == 2, TextFormat("%s: four Sharp bots win %d of %d (%.0f s on average, %.1f deaths a fight), reaching phase %d", BossName(b), wins, n, secs / n, deaths / (float)n, ph + 1));
    }
    { FightResult f = Fight(BK_KRAKEN, 1, 0, 77); BC(!f.won, TextFormat("one Stumble bot alone loses to the Kraken (%.0f%% of its health left)", f.hpLeft * 100)); }
    printf("  stage 8: %s\n", BF ? TextFormat("%d FAILED", BF) : "all checks passed");
    return BF;
}

} // namespace sf
