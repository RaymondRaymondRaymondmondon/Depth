// A Night Off's bar games (see nightoff_games.h), stage 3: the rules, the physics and the bots. Headless.
#include "nightoff_games.h"
#include "nightoff.h"
#include "nightoff_cards.h"
#include "json.h"
#include "redtide.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace no {

float GRng::N() { float u = std::max(1e-6f, U()), v = U(); return sqrtf(-2 * logf(u)) * cosf(2 * PI * v); }

// ---------------------------------------------------------------- data
static void Opps(const Json& a, std::vector<Opponent>& out) {
    for (const Json& o : a.a) {
        Opponent x; x.name = o["name"].Str0(); x.tell = o["tell"].Str0(); x.skill = o["skill"].F(1);
        x.hustler = o["hustler"].Bool0(); x.sandbag = o["sandbag"].Bool0(); x.once = o["once"].Bool0(); x.teaches = o["teaches"].Bool0(); x.stake = o["stake"].I(0);
        out.push_back(x);
    }
}
static GamesData LoadGames() {
    GamesData d;
    Json j = LoadJsonFile(rt::DataDir() + "/../nightoff/nightoff_games.json");
    for (const Json& p : j["aim_curve"].a) d.aimCurve.push_back({p[0].F(0), p[1].F(1)});
    const Json& da = j["darts"]; d.dartSigma = da["sigma_mm"].F(26); for (const Json& s : da["stakes"].a) d.dartStakes.push_back(s.I(5)); Opps(da["opponents"], d.darts);
    const Json& po = j["pool"]; d.poolSigma = po["sigma_rad"].F(0.012f); d.poolPower = po["power_noise"].F(0.07f); d.poolMissFrom = po["miss_from"].F(60);
    for (const Json& s : po["stakes"].a) d.poolStakes.push_back(s.I(10)); Opps(po["opponents"], d.pool);
    const Json& go = j["golf"]; d.golfSigma = go["sigma_rad"].F(0.035f); d.golfPower = go["power_noise"].F(0.08f); d.golfCloses = go["closes"].F(26);
    for (const Json& s : go["stakes"].a) d.golfStakes.push_back(s.I(2)); Opps(go["opponents"], d.golf);
    const Json& sl = j["slots"]; d.slotCost = sl["cost"].I(2); for (const Json& s : sl["symbols"].a) d.slotSymbols.push_back(s.Str0());
    for (const Json& m : sl["machines"].a) { std::vector<float> w; for (const Json& x : m["weights"].a) w.push_back(x.F(0)); d.slotWeights.push_back(w); }
    d.slotHonest = sl["honest"].I(1); d.payPair = sl["pays"]["kidney_pair"].I(50); d.payTriple = sl["pays"]["triple"].I(200); d.payLine = sl["pays"]["kidney_line"].I(1000);
    const Json& sc = j["scratch"]; d.scratchCost = sc["cost"].I(5);
    for (const Json& x : sc["prizes"].a) d.prizes.push_back(x.I(0));
    for (const Json& x : sc["dispenser"].a) d.dispenser.push_back(x.F(0));
    for (const Json& x : sc["pip"].a) d.pip.push_back(x.F(0));
    d.pipMap = sc["pip_map"].F(0.02f); for (const Json& s : sc["symbols"].a) d.scratchSymbols.push_back(s.Str0());
    d.fortuneCost = j["fortune"]["cost"].I(10); for (const Json& s : j["fortune"]["deck"].a) d.deck.push_back(s.Str0());
    for (const Json& s : j["fortune"]["lies"].a) d.lies.push_back(s.Str0());
    for (const auto& kv : j["fortune"]["event_cards"].o) d.eventCards.push_back({kv.first, kv.second.Str0()});
    return d;
}
const GamesData& GD() { static GamesData d = LoadGames(); return d; }
float AimMul(float drunk) {
    const auto& c = GD().aimCurve;
    if (c.empty()) return 1;
    if (drunk <= c[0][0]) return c[0][1];
    for (size_t i = 1; i < c.size(); i++) if (drunk <= c[i][0]) { float k = (drunk - c[i - 1][0]) / std::max(1e-3f, c[i][0] - c[i - 1][0]); return c[i - 1][1] + (c[i][1] - c[i - 1][1]) * k; }
    return c.back()[1];
}

// ================================================================ darts
namespace darts {
const int ORDER[20] = {20, 1, 18, 4, 13, 6, 10, 15, 2, 17, 3, 19, 7, 16, 8, 11, 14, 9, 12, 5};
Hit Score(Vector2 m) {
    float r = Vector2Length(m);
    if (r > R_D_OUT) return {0, 0};
    if (r < R_BULL) return {25, 2};
    if (r < R_OUTER) return {25, 1};
    float th = atan2f(m.x, m.y) * RAD2DEG; if (th < 0) th += 360;
    int idx = (int)((th + 9) / 18) % 20;
    int mult = r >= R_T_IN && r <= R_T_OUT ? 3 : r >= R_D_IN ? 2 : 1;
    return {ORDER[idx], mult};
}
Vector2 Bed(int value, int mult) {
    if (value == 25) return mult == 2 ? Vector2{0, 0} : Vector2{0, 11};
    int idx = 0; for (int i = 0; i < 20; i++) if (ORDER[i] == value) idx = i;
    float th = idx * 18 * DEG2RAD, r = mult == 3 ? 103 : mult == 2 ? 166 : 57;
    return {r * sinf(th), r * cosf(th)};
}
void Match::Start(bool clockMode, int first) {
    clock = clockMode; left[0] = left[1] = 501; next[0] = next[1] = 1; turn = first; dart = 0; turnStart = 501; winner = -1; turns = 0;
    turnPts = 0; bust = false; big[0] = big[1] = 0; marks.clear();
}
Vector2 Match::BotAim() const {
    if (clock) return next[turn] >= 21 ? Bed(25, 1) : Bed(next[turn], 1);
    int L = left[turn];
    if (L > 60) return Bed(20, 3);
    if (L == 50) return Bed(25, 2);
    if (L == 25) return Bed(25, 1);
    if (L <= 20) return Bed(L, 1);
    if (L <= 40 && L % 2 == 0) return Bed(L / 2, 2);
    if (L % 3 == 0) return Bed(L / 3, 3);
    if (L <= 40) return Bed(L - 20, 1);       // (leaves twenty)
    return Bed(20, 1);
}
void Match::Throw(Vector2 landed) {
    if (winner >= 0) return;
    if (dart == 0) { marks.clear(); turnPts = 0; bust = false; turnStart = left[turn]; }
    marks.push_back(landed);
    Hit h = Score(landed);
    if (clock) {
        int want = next[turn] >= 21 ? 25 : next[turn];
        if (h.mult > 0 && h.value == want) { next[turn]++; turnPts++; if (next[turn] > 21) { winner = turn; return; } }
    } else {
        int pts = h.Points();
        if (pts > left[turn]) { bust = true; left[turn] = turnStart; }
        else { left[turn] -= pts; turnPts += pts; if (left[turn] == 0) { winner = turn; return; } }
    }
    dart++;
    if (dart >= 3 || bust) {
        if (turnPts == 180) big[turn]++;
        dart = 0; turn ^= 1; turns++;
        if (turns >= 120) winner = clock ? (next[0] >= next[1] ? 0 : 1) : (left[0] <= left[1] ? 0 : 1);   // (a match that won't end)
    }
}
}

// ================================================================ pool
namespace pool {
const Vector2 POCKETS[6] = {{0, 0}, {W / 2, -0.012f}, {W, 0}, {0, H}, {W / 2, H + 0.012f}, {W, H}};
static bool InMouthX(float x) { return x < 0.085f || x > W - 0.085f || fabsf(x - W / 2) < 0.058f; }
static bool InMouthY(float y) { return y < 0.085f || y > H - 0.085f; }
bool Table::Moving() const { for (const auto& x : b) if (!x.in && (fabsf(x.v.x) > 1e-4f || fabsf(x.v.y) > 1e-4f)) return true; return false; }
void Table::Step(float dt, Result& r, bool& hitAny, int& first, float englishLeft, Vector2 shotDir) {
    for (int i = 0; i < 16; i++) {
        Ball& x = b[i]; if (x.in) continue;
        float s = Vector2Length(x.v);
        if (s > 0) { float ns = std::max(0.0f, s - (0.42f + 0.22f * s) * dt); x.v = ns < 0.008f ? Vector2{0, 0} : Vector2Scale(x.v, ns / s); }
        x.p = Vector2Add(x.p, Vector2Scale(x.v, dt));
        // the pockets
        for (int k = 0; k < 6; k++) {
            float cap = (k == 1 || k == 4) ? 0.058f : 0.066f;
            if (Vector2Distance(x.p, POCKETS[k]) < cap) { x.in = true; x.v = {0, 0}; r.potted.push_back(i); if (i == 0) r.scratch = true; break; }
        }
        if (x.in) continue;
        // the cushions (open at the pockets' mouths)
        if (x.p.x < BR && !InMouthY(x.p.y)) { x.p.x = 2 * BR - x.p.x; x.v.x = -x.v.x * 0.78f; }
        if (x.p.x > W - BR && !InMouthY(x.p.y)) { x.p.x = 2 * (W - BR) - x.p.x; x.v.x = -x.v.x * 0.78f; }
        if (x.p.y < BR && !InMouthX(x.p.x)) { x.p.y = 2 * BR - x.p.y; x.v.y = -x.v.y * 0.78f; }
        if (x.p.y > H - BR && !InMouthX(x.p.x)) { x.p.y = 2 * (H - BR) - x.p.y; x.v.y = -x.v.y * 0.78f; }
        if (x.p.x < -0.03f || x.p.x > W + 0.03f || x.p.y < -0.03f || x.p.y > H + 0.03f) { x.in = true; x.v = {0, 0}; r.potted.push_back(i); if (i == 0) r.scratch = true; }
    }
    for (int i = 0; i < 16; i++) {
        if (b[i].in) continue;
        for (int j = i + 1; j < 16; j++) {
            if (b[j].in) continue;
            Vector2 d = Vector2Subtract(b[j].p, b[i].p); float L2 = d.x * d.x + d.y * d.y;
            if (L2 >= 4 * BR * BR || L2 < 1e-12f) continue;
            float L = sqrtf(L2); Vector2 n = Vector2Scale(d, 1 / L);
            float push = (2 * BR - L) / 2; b[i].p = Vector2Subtract(b[i].p, Vector2Scale(n, push)); b[j].p = Vector2Add(b[j].p, Vector2Scale(n, push));
            float vrel = Vector2DotProduct(Vector2Subtract(b[i].v, b[j].v), n);
            if (vrel <= 0) continue;
            float cueSpeed = i == 0 ? Vector2Length(b[0].v) : 0;
            float imp = vrel * (1 + 0.94f) / 2;
            b[i].v = Vector2Subtract(b[i].v, Vector2Scale(n, imp)); b[j].v = Vector2Add(b[j].v, Vector2Scale(n, imp));
            if (i == 0 && first < 0) { first = j; hitAny = true; if (englishLeft != 0) b[0].v = Vector2Add(b[0].v, Vector2Scale(shotDir, englishLeft * 0.55f * cueSpeed)); }
        }
    }
}
Result Table::Simulate(const Shot& s, bool record) {
    Result r; if (record) frames.clear();
    if (b[0].in) return r;
    Vector2 dir{cosf(s.ang), sinf(s.ang)};
    b[0].v = Vector2Scale(dir, std::clamp(s.power, 0.05f, 7.0f));
    bool hit = false; int first = -1; const float dt = 1 / 240.0f; int n = 0;
    while (Moving() && r.time < 25) {
        Step(dt, r, hit, first, s.english, dir); r.time += dt;
        if (record && (n++ % 4) == 0) { std::array<Vector2, 16> f; for (int i = 0; i < 16; i++) f[i] = b[i].in ? Vector2{-9, -9} : b[i].p; frames.push_back(f); }
    }
    if (record) { std::array<Vector2, 16> f; for (int i = 0; i < 16; i++) f[i] = b[i].in ? Vector2{-9, -9} : b[i].p; frames.push_back(f); }
    for (auto& x : b) x.v = {0, 0};
    r.firstHit = first;
    return r;
}
void Match::Rack(GRng& r, int first) {
    Table T; turn = first; winner = -1; group[0] = group[1] = 0; ballInHand = false; broken = false; shots = 0; last.clear();
    Vector2 foot{0.75f * W, H / 2};
    std::vector<int> rest = {1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15};
    for (int i = (int)rest.size() - 1; i > 0; i--) std::swap(rest[i], rest[r.I(i + 1)]);
    int slot = 0, ri = 0;
    for (int row = 0; row < 5; row++) for (int k = 0; k <= row; k++) {
        Vector2 p{foot.x + row * (2 * BR * 0.866f + 0.0004f), foot.y + (k - row / 2.0f) * (2 * BR + 0.0004f)};
        int id = (row == 2 && k == 1) ? 8 : rest[ri++];
        T.b[id].p = p; T.b[id].in = false; slot++;
    }
    T.b[0].p = {0.25f * W, H / 2 + (r.U() - 0.5f) * 0.1f}; T.b[0].in = false;
    t = T;
}
int Match::Left(int who) const {
    int g = group[who], n = 0; if (g == 0) return 7;
    for (int i = (g == 1 ? 1 : 9); i <= (g == 1 ? 7 : 15); i++) n += !t.b[i].in;
    return n;
}
bool Match::Legal(int ball, int who) const {
    if (ball <= 0 || ball > 15 || t.b[ball].in) return false;
    if (group[who] == 0) return ball != 8;
    if (Left(who) > 0) return group[who] == 1 ? ball < 8 : ball > 8;
    return ball == 8;
}
void Match::Play(const Shot& s, bool missedCue) {
    if (winner >= 0) return;
    int who = turn; shots++;
    bool eightOk = Legal(8, who);                           // (the eight is this player's legal ball)
    bool legalBefore[16]; for (int i = 0; i < 16; i++) legalBefore[i] = Legal(i, who);
    Result r; if (!missedCue) r = t.Simulate(s, replay); else { r.missedCue = true; t.frames.clear(); }
    bool potted8 = std::find(r.potted.begin(), r.potted.end(), 8) != r.potted.end();
    bool foul = r.scratch || r.firstHit < 0 || (broken && !legalBefore[r.firstHit]);
    if (!broken) foul = r.scratch || r.firstHit < 0;
    if (potted8) {
        bool legal = broken && eightOk && !foul && r.firstHit == 8;
        winner = legal ? who : 1 - who;
        last = legal ? "the eight, called and sunk" : r.scratch ? "scratched on the eight" : "sank the eight too early";
        return;
    }
    int solids = 0, stripes = 0; for (int i : r.potted) { solids += i >= 1 && i <= 7; stripes += i >= 9; }
    if (broken && group[who] == 0 && !foul && (solids || stripes)) {
        int g = solids && !stripes ? 1 : stripes && !solids ? 2 : (r.potted[0] < 8 ? 1 : 2);
        group[who] = g; group[1 - who] = 3 - g;
    }
    int mine = 0; for (int i : r.potted) if (i != 0 && (group[who] == 0 || (group[who] == 1 ? i < 8 : i > 8))) mine++;
    bool wasBreak = !broken; broken = true;
    if (r.scratch) { t.b[0].in = false; t.b[0].p = {0.25f * W, H / 2}; }
    if (missedCue) last = "missed the cue ball entirely";
    else if (r.scratch) last = "scratched";
    else if (r.firstHit < 0) last = "hit nothing";
    else if (foul) last = "hit the wrong ball first";
    else if (mine) last = mine == 1 ? "sank one" : TextFormat("sank %d", mine);
    else last = wasBreak ? "broke" : "missed";
    if (foul) { turn = 1 - who; ballInHand = true; }
    else if (mine == 0 && !(wasBreak && !r.potted.empty())) { turn = 1 - who; ballInHand = false; }
    else ballInHand = false;
}
// ---- the bot: ghost-ball candidates that are open, a few strengths each, simulated; the best is fumbled by the skill
static float SegDist(Vector2 p, Vector2 a, Vector2 b) { Vector2 ab = Vector2Subtract(b, a); float t = std::clamp(Vector2DotProduct(Vector2Subtract(p, a), ab) / std::max(1e-6f, Vector2DotProduct(ab, ab)), 0.0f, 1.0f); return Vector2Distance(p, Vector2Add(a, Vector2Scale(ab, t))); }
static bool Clear(const Table& T, Vector2 a, Vector2 b, int skip1, int skip2) {
    for (int i = 0; i < 16; i++) { if (i == skip1 || i == skip2 || T.b[i].in) continue; if (SegDist(T.b[i].p, a, b) < 2 * BR - 0.002f) return false; }
    return true;
}
struct Cand { Shot s; float geo; };
static std::vector<Cand> Candidates(const Match& m, int who) {
    std::vector<Cand> out; const Table& T = m.t; Vector2 cue = T.b[0].p;
    for (int i = 1; i <= 15; i++) {
        if (!m.Legal(i, who)) continue;
        Vector2 o = T.b[i].p;
        for (int k = 0; k < 6; k++) {
            Vector2 P = POCKETS[k];
            Vector2 aimP = {std::clamp(P.x, 0.02f, W - 0.02f), std::clamp(P.y, 0.02f, H - 0.02f)};
            Vector2 dOP = Vector2Subtract(aimP, o); float lOP = Vector2Length(dOP); if (lOP < 1e-3f) continue; dOP = Vector2Scale(dOP, 1 / lOP);
            Vector2 ghost = Vector2Subtract(o, Vector2Scale(dOP, 2 * BR));
            Vector2 dCG = Vector2Subtract(ghost, cue); float lCG = Vector2Length(dCG); if (lCG < 1e-3f) continue;
            float cosCut = Vector2DotProduct(Vector2Scale(dCG, 1 / lCG), dOP);
            if (cosCut < 0.25f) continue;
            if (!Clear(T, cue, ghost, 0, i) || !Clear(T, o, aimP, i, 0)) continue;
            float need = sqrtf(2 * 0.6f * (lCG + lOP / std::max(0.3f, cosCut))) + 0.5f;
            Cand c; c.s.ang = atan2f(dCG.y, dCG.x); c.s.power = need; c.geo = cosCut * 2 - 0.4f * (lCG + lOP);
            out.push_back(c);
        }
    }
    std::sort(out.begin(), out.end(), [](const Cand& a, const Cand& b) { return a.geo > b.geo; });
    return out;
}
static float Judge(const Match& before, const Shot& s) {
    Match m = before; int who = m.turn; m.replay = false;
    m.Play(s);
    if (m.winner >= 0) return m.winner == who ? 1000.0f : -1000.0f;
    float sc = 0;
    if (m.turn != who) { sc -= 2; if (m.ballInHand) sc -= 6; }
    else sc += 4 + 0.5f * std::min(3, (int)Candidates(m, who).size());
    sc += 1.5f * (before.Left(who) - m.Left(who)) - 1.0f * (before.Left(1 - who) - m.Left(1 - who));
    return sc;
}
Shot BotShot(Match& m, float skill, GRng& r, bool exact) {
    Shot best; float bs = -1e9f;
    if (!m.broken) { best.ang = atan2f(m.t.b[8].p.y - m.t.b[0].p.y, 0.75f * W - m.t.b[0].p.x) + (r.U() - 0.5f) * 0.01f; best.power = 6.2f; }
    else {
        auto C = Candidates(m, m.turn);
        int K = std::min((int)C.size(), 6);
        static const float POW[3] = {0.8f, 1.15f, 1.6f};
        for (int i = 0; i < K; i++) for (float pk : POW) { Shot s = C[i].s; s.power = std::min(6.5f, s.power * pk); float sc = Judge(m, s); if (sc > bs) { bs = sc; best = s; } }
        if (C.empty()) {   // a safety: roll up to a legal ball
            for (int i = 1; i <= 15; i++) if (m.Legal(i, m.turn)) for (int a = -2; a <= 2; a++) {
                Shot s; Vector2 d = Vector2Subtract(m.t.b[i].p, m.t.b[0].p); s.ang = atan2f(d.y, d.x) + a * 0.03f; s.power = 1.4f;
                float sc = Judge(m, s); if (sc > bs) { bs = sc; best = s; }
            }
        }
    }
    if (!exact) { best.ang += r.N() * GD().poolSigma * skill; best.power *= std::max(0.3f, 1 + r.N() * GD().poolPower * skill); }
    return best;
}
Vector2 BotPlace(Match& m, GRng& r) {
    Vector2 best = {0.25f * W, H / 2}; float bs = -1e9f;
    for (int k = 0; k < 18; k++) {
        Vector2 p{0.1f + r.U() * (W - 0.2f), 0.08f + r.U() * (H - 0.16f)};
        bool free = true; for (int i = 1; i < 16; i++) if (!m.t.b[i].in && Vector2Distance(p, m.t.b[i].p) < 2.2f * BR) free = false;
        if (!free) continue;
        Vector2 keep = m.t.b[0].p; m.t.b[0].p = p;
        auto C = Candidates(m, m.turn); float sc = C.empty() ? -1 : C[0].geo + 0.2f * std::min(4, (int)C.size());
        m.t.b[0].p = keep;
        if (sc > bs) { bs = sc; best = p; }
    }
    return best;
}
}

// ================================================================ mini golf
namespace golf {
static Hole MakeHole(const char* name, const char* feature, Rectangle box, Vector2 tee, Vector2 cup, std::vector<Seg> inner, int par) {
    Hole h; h.name = name; h.feature = feature; h.box = box; h.tee = tee; h.cup = cup; h.par = par;
    Vector2 a{box.x, box.y}, b{box.x + box.width, box.y}, c{box.x + box.width, box.y + box.height}, d{box.x, box.y + box.height};
    h.walls = {{a, b}, {b, c}, {c, d}, {d, a}};
    for (auto& s : inner) h.walls.push_back(s);
    return h;
}
const std::vector<Hole>& Course() {
    static std::vector<Hole> C;
    if (!C.empty()) return C;
    C.push_back(MakeHole("The Plank", "", {0, 0, 5, 1.2f}, {0.5f, 0.6f}, {4.5f, 0.6f}, {}, 2));
    { Hole h = MakeHole("The Windmill", "windmill", {0, 0, 6, 1.5f}, {0.5f, 0.75f}, {5.5f, 0.75f}, {{{3, 0}, {3, 0.52f}}, {{3, 0.98f}, {3, 1.5f}}}, 3); h.mill = {3, 0.75f}; h.millR = 0.6f; C.push_back(h); }
    C.push_back(MakeHole("The Dogleg", "", {0, 0, 5, 4}, {0.6f, 0.6f}, {4.4f, 3.4f}, {{{0, 1.2f}, {3.8f, 1.2f}}, {{3.8f, 1.2f}, {3.8f, 4}}}, 3));
    { Hole h = MakeHole("The Drain", "drain", {0, 0, 6, 1.6f}, {0.5f, 0.9f}, {5.5f, 0.9f}, {{{3, 0.42f}, {3, 1.6f}}, {{4.2f, 0}, {4.2f, 1.15f}}}, 3); h.drain = {3, 0.2f}; h.drainR = 0.16f; h.drainOut = {5.0f, 1.3f}; C.push_back(h); }
    { Hole h = MakeHole("The Sleeping Dog", "dog", {0, 0, 6, 1.6f}, {0.5f, 0.8f}, {5.5f, 0.8f}, {}, 2); h.dog = {3, 0.8f}; h.dogR = 0.45f; C.push_back(h); }
    { Hole h = MakeHole("The Loop", "loop", {0, 0, 6, 1.2f}, {0.5f, 0.6f}, {5.4f, 0.6f}, {}, 2); h.loop = {2.6f, 0, 0.8f, 1.2f}; h.loopV = 2.2f; C.push_back(h); }
    C.push_back(MakeHole("The Bank", "", {0, 0, 6, 2}, {0.5f, 0.5f}, {5.5f, 1.5f}, {{{2, 0}, {2, 1.3f}}, {{4, 2}, {4, 0.7f}}}, 3));
    C.push_back(MakeHole("The Gauntlet", "", {0, 0, 6, 1.6f}, {0.5f, 0.8f}, {5.5f, 0.8f}, {{{2, 0.3f}, {2, 0.75f}}, {{3, 0.85f}, {3, 1.3f}}, {{4, 0.3f}, {4, 0.75f}}}, 2));
    C.push_back(MakeHole("The Fish Tank", "tank", {0, 0, 5, 1.4f}, {0.5f, 0.7f}, {4.75f, 0.7f}, {{{4, 0}, {4.5f, 0.52f}}, {{4, 1.4f}, {4.5f, 0.88f}}}, 2));
    return C;
}
static void Bounce(Ball& b, Vector2 a, Vector2 c, float rest) {
    Vector2 ac = Vector2Subtract(c, a); float t = std::clamp(Vector2DotProduct(Vector2Subtract(b.p, a), ac) / std::max(1e-6f, Vector2DotProduct(ac, ac)), 0.0f, 1.0f);
    Vector2 q = Vector2Add(a, Vector2Scale(ac, t)); Vector2 d = Vector2Subtract(b.p, q); float L = Vector2Length(d);
    if (L >= BR || L < 1e-6f) return;
    Vector2 n = Vector2Scale(d, 1 / L);
    b.p = Vector2Add(q, Vector2Scale(n, BR));
    float vn = Vector2DotProduct(b.v, n);
    if (vn < 0) b.v = Vector2Subtract(b.v, Vector2Scale(n, (1 + rest) * vn));
}
bool Roll(const Hole& h, Ball& b, Sim& s, float maxT, std::vector<Vector2>* path) {
    const float dt = 1 / 120.0f; float t0 = s.t;
    if (h.dogR > 0 && s.dog.x == 0 && s.dog.y == 0) s.dog = h.dog;
    while (s.t - t0 < maxT) {
        float sp = Vector2Length(b.v);
        if (sp < 0.02f) { b.v = {0, 0}; break; }
        float ns = std::max(0.0f, sp - (0.55f + 0.15f * sp) * dt); b.v = Vector2Scale(b.v, ns / sp);
        b.p = Vector2Add(b.p, Vector2Scale(b.v, dt)); s.t += dt;
        for (const auto& w : h.walls) Bounce(b, w.a, w.b, 0.72f);
        if (h.millR > 0) {
            float th = s.t * 2 * PI / 3;
            for (int k = 0; k < 2; k++) { float a = th + k * PI; Vector2 tip = Vector2Add(h.mill, {cosf(a) * h.millR, sinf(a) * h.millR}); Bounce(b, h.mill, tip, 0.6f); }
        }
        if (h.dogR > 0) {
            Vector2 d = Vector2Subtract(b.p, s.dog); float L = Vector2Length(d);
            if (L < h.dogR + BR && L > 1e-4f) {
                Vector2 n = Vector2Scale(d, 1 / L); b.p = Vector2Add(s.dog, Vector2Scale(n, h.dogR + BR));
                float vn = Vector2DotProduct(b.v, n);
                if (vn < 0) { b.v = Vector2Subtract(b.v, Vector2Scale(n, 1.3f * vn)); if (-vn > 0.5f) s.dog.y = std::clamp(s.dog.y - n.y * 0.2f, h.box.y + h.dogR, h.box.y + h.box.height - h.dogR); }
            }
        }
        if (h.drainR > 0 && Vector2Distance(b.p, h.drain) < h.drainR) { b.p = h.drainOut; b.v = {0, 0}; if (path) path->push_back(b.p); break; }
        if (h.loopV > 0 && CheckCollisionPointRec(b.p, h.loop) && b.v.x > 0 && Vector2Length(b.v) < h.loopV) b.v.x = -fabsf(b.v.x) * 0.8f;
        if (Vector2Distance(b.p, h.cup) < CUP) {
            if (Vector2Length(b.v) < 1.3f) { b.holed = true; b.p = h.cup; b.v = {0, 0}; if (path) path->push_back(b.p); return true; }
            b.v = Vector2Scale(b.v, 0.85f);
        }
        if (path && ((int)((s.t - t0) / dt) % 2) == 0) path->push_back(b.p);
    }
    return false;
}
void Match::Start(int first) {
    hole = 0; turn = first; winner = -1; for (auto& r : strokes) for (int& x : r) x = 0;
    for (auto& b : ball) b = Ball{Course()[0].tee}; done[0] = false; done[1] = solo; sim = Sim{}; if (solo) turn = 0;
}
int Match::Total(int who) const { int n = 0; for (int i = 0; i < 9; i++) n += strokes[who][i]; return n; }
void Match::Shoot(float ang, float power, std::vector<Vector2>* path) {
    if (winner >= 0) return;
    const Hole& h = Course()[hole];
    Ball& b = ball[turn];
    b.v = {cosf(ang) * power, sinf(ang) * power};
    strokes[turn][hole]++;
    if (rain && (hole == 3 || hole == 6)) { b.v.y += 0.18f; }   // (rain floats the ball on holes 4 and 7)
    Roll(h, b, sim, 12, path);
    lastHoled = b.holed; lastWho = turn; lastHole = hole; lastStrokes = strokes[turn][hole];
    if (b.holed || strokes[turn][hole] >= maxStrokes) done[turn] = true;
    if (done[turn]) {
        if (!done[1 - turn]) turn = 1 - turn;
        else {
            hole++;
            if (hole >= 9) { int a = Total(0), c = Total(1); winner = solo ? 0 : a < c ? 0 : c < a ? 1 : 2; hole = 8; return; }
            done[0] = false; done[1] = solo; for (auto& x : ball) x = Ball{Course()[hole].tee}; sim.dog = Course()[hole].dog;
            turn = solo || (strokes[0][hole - 1] <= strokes[1][hole - 1]) ? 0 : 1;   // (the hole's winner tees off first)
        }
    }
}
// a distance field per hole (Dijkstra on a 5 cm grid round the walls), so the bot knows the way round a dogleg
struct Field { int w = 0, h = 0; float cell = 0.05f; std::vector<float> d; float At(Vector2 p) const { int x = std::clamp((int)(p.x / cell), 0, w - 1), y = std::clamp((int)(p.y / cell), 0, h - 1); return d[y * w + x]; } };
static bool Crosses(Vector2 a, Vector2 b, const Seg& s) {
    auto cr = [](Vector2 o, Vector2 p, Vector2 q) { return (p.x - o.x) * (q.y - o.y) - (p.y - o.y) * (q.x - o.x); };
    float d1 = cr(s.a, s.b, a), d2 = cr(s.a, s.b, b), d3 = cr(a, b, s.a), d4 = cr(a, b, s.b);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}
static const Field& FieldOf(int hole) {
    static std::vector<Field> F(9);
    Field& f = F[hole];
    if (!f.d.empty()) return f;
    const Hole& H = Course()[hole];
    f.w = (int)(H.box.width / f.cell) + 1; f.h = (int)(H.box.height / f.cell) + 1;
    f.d.assign(f.w * f.h, 1e9f);
    std::vector<std::pair<float, int>> q; int start = std::clamp((int)(H.cup.y / f.cell), 0, f.h - 1) * f.w + std::clamp((int)(H.cup.x / f.cell), 0, f.w - 1);
    f.d[start] = 0; q.push_back({0, start});
    auto cmp = [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first > b.first; };
    while (!q.empty()) {
        std::pop_heap(q.begin(), q.end(), cmp); auto [dd, i] = q.back(); q.pop_back();
        if (dd > f.d[i]) continue;
        int x = i % f.w, y = i / f.w; Vector2 a{(x + 0.5f) * f.cell, (y + 0.5f) * f.cell};
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue; int nx = x + dx, ny = y + dy; if (nx < 0 || ny < 0 || nx >= f.w || ny >= f.h) continue;
            Vector2 b{(nx + 0.5f) * f.cell, (ny + 0.5f) * f.cell}; bool cut = false;
            for (size_t k = 4; k < H.walls.size(); k++) if (Crosses(a, b, H.walls[k])) { cut = true; break; }
            if (cut) continue;
            float nd = dd + f.cell * (dx && dy ? 1.414f : 1.0f); int j = ny * f.w + nx;
            if (nd < f.d[j]) { f.d[j] = nd; q.push_back({nd, j}); std::push_heap(q.begin(), q.end(), cmp); }
        }
    }
    return f;
}
void BotShot(const Match& m, float skill, GRng& r, float& ang, float& power, bool exact) {
    const Hole& h = Course()[m.hole]; const Field& f = FieldOf(m.hole);
    Vector2 p = m.ball[m.turn].p;
    float direct = atan2f(h.cup.y - p.y, h.cup.x - p.x);
    float best = 1e9f; ang = direct; power = 1.5f;
    auto tryShot = [&](float a, float pw) {
        Ball b = m.ball[m.turn]; Sim s = m.sim; b.v = {cosf(a) * pw, sinf(a) * pw};
        bool in = Roll(h, b, s, 10);
        float sc = in ? -1 : f.At(b.p) + (Vector2Distance(b.p, h.drainOut) < 0.05f && h.drainR > 0 ? -0.2f : 0);
        if (sc < best) { best = sc; ang = a; power = pw; }
    };
    static const float POW[6] = {0.9f, 1.4f, 2.0f, 2.7f, 3.5f, 4.4f};
    for (int k = 0; k < 18; k++) for (float pw : POW) tryShot(direct + k * (2 * PI / 18), pw);
    for (int k = -4; k <= 4; k++) for (float pw : POW) tryShot(direct + k * 0.035f, pw);
    float a0 = ang, p0 = power;   // refine round the best
    for (int k = -3; k <= 3; k++) for (int j = -2; j <= 2; j++) tryShot(a0 + k * 0.012f, p0 * (1 + j * 0.06f));
    if (!exact) { ang += r.N() * GD().golfSigma * skill; power *= std::max(0.3f, 1 + r.N() * GD().golfPower * skill); }
}
}

// ================================================================ the slots, scratch-offs, the fortune teller
namespace slots {
static int Pick(const std::vector<float>& w, float u) { float t = 0; for (float x : w) t += x; float a = u * t; for (size_t i = 0; i < w.size(); i++) { a -= w[i]; if (a <= 0) return (int)i; } return (int)w.size() - 1; }
static int Pays(const int s[3], bool& kid) {
    const GamesData& d = GD(); int k = (s[0] == 3) + (s[1] == 3) + (s[2] == 3); kid = false;
    if (k == 3) { kid = true; return d.payLine; }
    if (k == 2) return d.payPair;
    if (s[0] == s[1] && s[1] == s[2] && s[0] <= 2) return d.payTriple;
    return 0;
}
Pull Spin(int machine, GRng& r) {
    const auto& w = GD().slotWeights[std::clamp(machine, 0, (int)GD().slotWeights.size() - 1)];
    Pull p; for (int i = 0; i < 3; i++) p.reel[i] = Pick(w, r.U());
    p.pays = Pays(p.reel, p.kidney);
    return p;
}
float ReturnRate(int machine) {
    const auto& w = GD().slotWeights[std::clamp(machine, 0, (int)GD().slotWeights.size() - 1)];
    float t = 0; for (float x : w) t += x;
    double ev = 0; int n = (int)w.size();
    for (int a = 0; a < n; a++) for (int b = 0; b < n; b++) for (int c = 0; c < n; c++) { int s[3] = {a, b, c}; bool k; ev += (double)w[a] * w[b] * w[c] / (t * t * t) * Pays(s, k); }
    return (float)(ev / GD().slotCost);
}
}
namespace scratch {
Ticket Buy(bool pip, GRng& r) {
    const GamesData& d = GD(); const auto& odds = pip ? d.pip : d.dispenser;
    Ticket t; float u = r.U(), acc = 0;
    for (int i = (int)odds.size() - 1; i >= 1; i--) { acc += odds[i]; if (u < acc) { t.prize = d.prizes[i]; for (int& s : t.sym) s = i; break; } }
    if (t.prize == 0) { do { for (int& s : t.sym) s = r.I((int)d.scratchSymbols.size()); } while (t.sym[0] == t.sym[1] && t.sym[1] == t.sym[2]); }
    t.map = pip && r.U() < d.pipMap;
    return t;
}
}
int GamesData::Card(const std::string& name) const { for (int i = 0; i < (int)deck.size(); i++) if (deck[i] == name) return i; return 0; }
namespace fortune {
// the reading (doc p. 42): three cards, who you'll meet, what you'll do, how it ends, from the night's actual schedule
// (the events still to come, which thief is in, who'll flirt with you), so it's true. The Stitch and the Bathtub
// together mean a thief will make an offer (and the thief comes to find you); the Key, that the safe will open tonight;
// the Dog, feed it; the Morning third, that you'll walk home. A second reading is a lie she tells for fun.
Reading Read(const Night& n, const Player& p, GRng& r) {
    const Data& d = D(); const GamesData& g = GD();
    struct Truth { int card; std::string text; float w; int thief = -1; };
    std::vector<Truth> who, what, end;
    float h = n.Hour();
    auto clock = [](float hr) { int H = (int)hr, M = (int)((hr - H) * 60); int h12 = ((H % 12) == 0) ? 12 : H % 12; return std::string(TextFormat("%d:%02d %s", h12, M, (H % 24) < 12 ? "a.m." : "p.m.")); };
    Reading R;
    if (p.fortuneReads >= 1) {   // a second reading: a lie, for fun
        R.lie = true;
        for (int k = 0; k < 3; k++) { R.card[k] = r.I((int)g.deck.size()); R.text[k] = g.lies.empty() ? "..." : g.lies[r.I((int)g.lies.size())]; }
        for (int k = 1; k < 3; k++) for (int j = 0; j < k; j++) if (R.card[k] == R.card[j]) { R.card[k] = (R.card[k] + 5) % std::max(1, (int)g.deck.size()); j = -1; }
        return R;
    }
    // who you'll meet: a thief (the Siren), the events still to come (each has its card), a flirt, the dog
    int thief = -1;
    for (const auto& c : n.patrons) {
        if (c.gone || !c.thief) continue;
        if (c.inside) { who.push_back({g.Card("The Siren"), "A smile with a cooler sits in this room tonight. Its name is " + c.name + ".", 3, c.id}); thief = c.id; }
        else if (c.arriveH > h && c.arriveH < 26.5f) { who.push_back({g.Card("The Siren"), "A smile with a cooler comes through the door by " + clock(c.arriveH + 0.25f) + " Its name is " + c.name + ".", 3, c.id}); if (thief < 0) thief = c.id; }
    }
    for (const auto& e : n.events) {
        if (e.started || e.startH < h || e.startH > 27) continue;
        std::string key = n.EventKey(e.def), card;
        for (const auto& kv : g.eventCards) if (kv.first == key) card = kv.second;
        if (!card.empty()) who.push_back({g.Card(card), n.EventName(e.def) + " comes in by " + clock(e.startH + 0.2f), 2.5f});
    }
    for (const auto& c : n.patrons) if (c.inside && !c.gone && !c.thief && c.reg >= 0 && (c.type == T_FLIRT || c.Has(d.Trait("flirty")))) { who.push_back({g.Card("The Gull"), c.name + " will look your way before the night is out.", 1}); break; }
    if (n.dog.owner < 0) who.push_back({g.Card("The Dog"), "A dog in the alley is waiting for someone. Feed it, and it will wait for you.", 1});
    // what you'll do: a thief's offer (the Stitch), the safe (the Key), a hustler (the Coin), a knife, a secret, a dance, the bottles
    if (thief >= 0) what.push_back({g.Card("The Stitch"), "Someone will offer to take you home. Look at their hands.", 3, thief});
    bool marlow = false; for (const auto& c : n.patrons) marlow |= !c.gone && c.name == "Old Marlow" && (c.inside || c.arriveH > h);
    if (!n.safeOpened && marlow) what.push_back({g.Card("The Key"), "The safe upstairs is open to whoever has listened to Old Marlow.", 2});
    for (const auto& c : n.patrons) if (c.inside && !c.gone && c.type == T_HUSTLER) { what.push_back({g.Card("The Coin"), "You'll beat " + c.name + " once, because they let you. Not twice.", 1.5f}); break; }
    for (const auto& c : n.patrons) if (c.inside && !c.gone && c.Has(d.Trait("carries a knife"))) { what.push_back({g.Card("The Knife"), c.name + " carries a blade. Don't be the one who finds out.", 1.5f}); break; }
    for (const auto& c : n.patrons) if (c.inside && !c.gone && c.reg >= 0 && !c.secret.empty() && std::find(p.known.begin(), p.known.end(), c.name) == p.known.end()) { what.push_back({g.Card("The Periscope"), c.name + " has a secret worth listening for.", 1}); break; }
    for (const auto& e : n.events) if (n.EventKey(e.def) == "band" && !e.done) { what.push_back({g.Card("The Diver"), "You'll dance tonight, whether you meant to or not.", 1.5f}); break; }
    if (h < d.lastCallHour) what.push_back({g.Card("The Bottle"), "The bottles double at " + clock(d.lastCallHour) + " Drink before, or don't.", 0.7f});
    // how it ends: the Bathtub (with the Stitch), the lock-in, the cartel's ledger, a missing kidney, the Morning
    if (thief >= 0) end.push_back({g.Card("The Bathtub"), "A bathtub of ice and a note, unless somebody stops you at the door.", 3, thief});
    for (const auto& e : n.events) if (!e.done && n.EventKey(e.def) == "lockin") { end.push_back({g.Card("The Lantern"), "The door locks behind you at " + clock(std::max(h, e.startH)), 2}); break; }
    if (p.debt > 0) end.push_back({g.Card("The Abyss"), "They'll come for what you owe, and they'll find you.", 2.5f});
    if (p.kidneys < 2) end.push_back({g.Card("The Drowned"), "Something of yours is missing, and the slot machines remember it.", 2});
    if (p.drunk < 40 && p.kidneys >= 2 && p.debt <= 0) end.push_back({g.Card("The Morning"), "You'll walk home on your own feet, and remember most of it.", 1.2f});
    end.push_back({g.Card("The Helm"), std::string("The man behind the bar thinks you are ") + n.MoodName(n.bar.mood) + ". It ends where he says.", 0.5f});
    if (who.empty()) who.push_back({g.Card("The Gull"), "Nobody new. The same faces, and one of them is yours.", 1});
    if (what.empty()) what.push_back({g.Card("The Bottle"), "You'll drink. She doesn't need the cards for that.", 1});
    auto pick = [&](std::vector<Truth>& v) { float tot = 0; for (auto& x : v) tot += x.w; float u = r.U() * tot; for (auto& x : v) { u -= x.w; if (u <= 0) return x; } return v.back(); };
    Truth a = pick(who), b = pick(what), c = pick(end);
    // the Stitch and the Bathtub come as a pair (a thief's offer is how it ends)
    if (b.card == g.Card("The Stitch")) for (auto& x : end) if (x.card == g.Card("The Bathtub")) c = x;
    if (c.card == g.Card("The Bathtub")) for (auto& x : what) if (x.card == g.Card("The Stitch")) b = x;
    if (b.card == g.Card("The Stitch") && c.card == g.Card("The Bathtub")) R.fated = b.thief;
    R.card[0] = a.card; R.text[0] = a.text; R.card[1] = b.card; R.text[1] = b.text; R.card[2] = c.card; R.text[2] = c.text;
    for (int k = 1; k < 3; k++) for (int j = 0; j < k; j++) if (R.card[k] == R.card[j]) { R.card[k] = (R.card[k] + 7 + k) % std::max(1, (int)g.deck.size()); j = -1; }
    return R;
}
}
// ================================================================ --game-check
static int PlayDarts(float sk0, float sk1, GRng& r, int first) {
    darts::Match m; m.Start(false, first);
    float sig = GD().dartSigma;
    while (m.winner < 0) { float sk = m.turn == 0 ? sk0 : sk1; Vector2 a = m.BotAim(); m.Throw({a.x + r.N() * sig * sk, a.y + r.N() * sig * sk}); }
    return m.winner;
}
static int PlayPool(float sk0, float sk1, float drunk0, GRng& r, int first) {
    pool::Match m; m.Rack(r, first);
    while (m.winner < 0 && m.shots < 200) {
        float sk = m.turn == 0 ? sk0 : sk1;
        if (m.ballInHand) { m.t.b[0].p = pool::BotPlace(m, r); m.t.b[0].in = false; m.ballInHand = false; }
        bool miss = m.turn == 0 && drunk0 > GD().poolMissFrom && r.U() < (drunk0 - GD().poolMissFrom) / 150;
        pool::Shot s = pool::BotShot(m, sk, r);
        m.Play(s, miss);
    }
    if (m.winner < 0) m.winner = m.Left(0) <= m.Left(1) ? 0 : 1;
    return m.winner;
}
static int PlayGolf(float sk0, float sk1, GRng& r, int first) {
    golf::Match m; m.Start(first);
    while (m.winner < 0) { float a, p; golf::BotShot(m, m.turn == 0 ? sk0 : sk1, r, a, p); m.Shoot(a, p); }
    return m.winner;
}
int RunGameCheck(const std::string& game, int games) {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("A Night Off: the bar games (--game-check %s, %d games a cell)\n", game.c_str(), games);
    const float LV[3] = {0, 40, 80}, TARGET[3] = {0.55f, 0.35f, 0.10f};
    auto run = [&](const char* name, const std::vector<Opponent>& opps, auto play) {
        for (const auto& o : opps) {
            float rate[3];
            for (int l = 0; l < 3; l++) {
                GRng r; r.s = 1234 + l * 77 + (uint32_t)o.name.size() * 13;
                float w = 0;
                for (int g = 0; g < games; g++) { int res = play(AimMul(LV[l]), o.skill, LV[l], r, g & 1); w += res == 0 ? 1 : res == 2 ? 0.5f : 0; }
                rate[l] = w / games;
            }
            printf("  %-6s vs %-14s%s: sober %3.0f%%  drunk %3.0f%%  wrecked %3.0f%%\n", name, o.name.c_str(), o.hustler ? " (hustler)" : "", rate[0] * 100, rate[1] * 100, rate[2] * 100);
            if (o.hustler) {
                for (int l = 0; l < 3; l++) check(fabsf(rate[l] - TARGET[l]) <= 0.12f, TextFormat("%s against %s at %.0f drunk: %.0f%% (target %.0f%%)", name, o.name.c_str(), LV[l], rate[l] * 100, TARGET[l] * 100));
                check(rate[0] > 0.5f && rate[2] < 0.25f, TextFormat("%s: a sober player beats the hustler, a wrecked one loses to him", name));
            }
        }
    };
    if (game == "darts" || game == "all") run("darts", GD().darts, [](float a, float b, float, GRng& r, int f) { return PlayDarts(a, b, r, f); });
    if (game == "pool" || game == "all") run("pool", GD().pool, [](float a, float b, float d, GRng& r, int f) { return PlayPool(a, b, d, r, f); });
    if (game == "golf" || game == "all") run("golf", GD().golf, [](float a, float b, float, GRng& r, int f) { return PlayGolf(a, b, r, f); });
    if (game == "slots" || game == "all") {
        for (int m = 0; m < (int)GD().slotWeights.size(); m++) printf("  slots: machine %d returns %.0f%%\n", m + 1, slots::ReturnRate(m) * 100);
        bool ok = true; for (int m = 0; m < (int)GD().slotWeights.size(); m++) ok &= (m == GD().slotHonest) == (slots::ReturnRate(m) >= 1);
        check(ok, "the machines are rigged in the house's favour, all but one");
    }
    if (game == "poker" || game == "bullshit" || game == "all") fails += RunCardCheck(game, std::max(20, games / 2));
    printf(fails ? "game-check: %d check(s) FAILED\n" : "game-check: all checks passed\n", fails);
    return fails ? 1 : 0;
}

} // namespace no
