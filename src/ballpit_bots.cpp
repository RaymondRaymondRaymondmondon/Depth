// Ball Pit Brawl's bots, their navigation graph, and the acceptance tests (the spec's milestone tests) and the sim.
// The graph: a 1 m grid of standing spots on the hall floor and every deck (the cells you can only crouch through
// marked), joined to their neighbours; one-way drops off edges (3 m or less, or into a pit); the ladders and cargo
// nets (foot to the deck above); the slides (top to exit, one way). Bots path over it with A*.
#include "ballpit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <queue>
#include <unordered_map>

namespace bp {

enum EdgeKind : uint8_t { NE_WALK, NE_DROP, NE_CLIMB, NE_SLIDE };
struct NavEdge { int to; float cost; uint8_t kind; int16_t idx; };
struct NavNode { Vector3 p; bool crouch = false, pit = false; std::vector<NavEdge> out; };
struct Nav { std::vector<NavNode> nodes; std::unordered_map<long long, int> cell; int Nearest(Vector3 p, float maxD = 4) const; };

static long long Key(int ix, int iz, int ih) { return ((long long)(ix + 1000) << 32) | ((long long)(iz + 1000) << 12) | (ih & 0xFFF); }

int Nav::Nearest(Vector3 p, float maxD) const {
    int ix = (int)floorf(p.x), iz = (int)floorf(p.z); int best = -1; float bd = maxD;
    for (int dx = -3; dx <= 3; dx++) for (int dz = -3; dz <= 3; dz++) for (int ih = 0; ih <= 100; ih += 1) {
        auto it = cell.find(Key(ix + dx, iz + dz, ih)); if (it == cell.end()) continue;
        const NavNode& n = nodes[it->second]; float d = Vector3Distance(n.p, p) + (n.p.y > p.y + 0.6f ? 3 : 0) + fabsf(n.p.y - p.y) * 1.5f;
        if (d < bd) { bd = d; best = it->second; }
    }
    if (best < 0) for (int i = 0; i < (int)nodes.size(); i++) { float d = Vector3Distance(nodes[i].p, p); if (d < bd + 6) { bd = d; best = i; } }
    return best;
}

static bool BodyFits(const World& w, Vector3 p, float h0, float h1, float r) {
    for (const auto& b : w.arena.solids) {
        if (!BlocksMove(b.mat)) continue;
        if (b.hi.y <= p.y + h0 || b.lo.y >= p.y + h1) continue;
        float cx = std::clamp(p.x, b.lo.x, b.hi.x), cz = std::clamp(p.z, b.lo.z, b.hi.z);
        if ((p.x - cx) * (p.x - cx) + (p.z - cz) * (p.z - cz) < r * r) return false;
    }
    return fabsf(p.x) < w.arena.halfX - r && fabsf(p.z) < w.arena.halfZ - r;
}

static Nav BuildNav(const Arena& a) {
    World w; w.arena = a;   // (for the queries)
    const Config& C = Cfg(); Nav nav;
    std::vector<float> heights = {0};
    for (const auto& s : a.solids) if (s.mat == M_DECK) { bool have = false; for (float h : heights) if (fabsf(h - s.hi.y) < 0.05f) have = true; if (!have) heights.push_back(s.hi.y); }
    auto add = [&](Vector3 p, bool crouch, bool pit, int ix, int iz) { NavNode n; n.p = p; n.crouch = crouch; n.pit = pit; nav.nodes.push_back(n); nav.cell[Key(ix, iz, (int)lroundf(p.y * 10))] = (int)nav.nodes.size() - 1; };
    for (int ix = -30; ix < 30; ix++) for (int iz = -15; iz < 15; iz++) for (float h : heights) {
        Vector3 p{ix + 0.5f, h, iz + 0.5f};
        if (w.GroundAt(Vector3Add(p, {0, 0.01f, 0}), 0.12f, 0.04f) != h && !(h == 0 && w.GroundAt({p.x, 0.6f, p.z}, 0.12f, 0) <= 0.56f)) continue;
        float g = w.GroundAt({p.x, h + 0.6f, p.z}, 0.12f, 0); p.y = g; if (fabsf(g - h) > 0.56f) continue;
        bool stand = BodyFits(w, p, C.stepUp, C.height, C.radius * 0.95f), crouch = !stand && BodyFits(w, p, C.stepUp, C.crouchH, C.radius * 0.95f);
        if (!stand && !crouch) continue;
        bool pit = w.PitAt({p.x, p.y + 0.1f, p.z}) >= 0;
        add(p, crouch, pit, ix, iz);
    }
    auto find = [&](int ix, int iz, float h) { for (int dh = -6; dh <= 6; dh++) { auto it = nav.cell.find(Key(ix, iz, (int)lroundf(h * 10) + dh)); if (it != nav.cell.end()) return it->second; } return -1; };
    // walking between neighbours (same level, the body fits half way)
    for (int i = 0; i < (int)nav.nodes.size(); i++) {
        NavNode& n = nav.nodes[i]; int ix = (int)floorf(n.p.x), iz = (int)floorf(n.p.z);
        for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
            if (!dx && !dz) continue;
            int j = find(ix + dx, iz + dz, n.p.y); if (j < 0) continue;
            const NavNode& m = nav.nodes[j]; if (fabsf(m.p.y - n.p.y) > 0.56f) continue;
            Vector3 mid = Vector3Lerp(n.p, m.p, 0.5f); bool needCrouch = n.crouch || m.crouch;
            if (!BodyFits(w, mid, C.stepUp, needCrouch ? C.crouchH : C.height, C.radius * 0.9f)) { if (!BodyFits(w, mid, C.stepUp, C.crouchH, C.radius * 0.9f)) continue; needCrouch = true; }
            if (dx && dz && (find(ix + dx, iz, n.p.y) < 0 || find(ix, iz + dz, n.p.y) < 0)) continue;   // (no cutting corners)
            float cost = Vector3Distance(n.p, m.p) * (needCrouch ? 2.2f : 1.0f) * ((n.pit || m.pit) ? 2.6f : 1.0f);
            n.out.push_back({j, cost, NE_WALK, 0});
        }
    }
    // drops off an edge: 3 m or less, or into a pit; one way
    for (int i = 0; i < (int)nav.nodes.size(); i++) {
        NavNode& n = nav.nodes[i]; if (n.p.y < 0.6f) continue; int ix = (int)floorf(n.p.x), iz = (int)floorf(n.p.z);
        for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
            if (!dx == !dz) continue;   // (straight off the edge)
            if (find(ix + dx, iz + dz, n.p.y) >= 0) continue;
            Vector3 over{n.p.x + dx * 1.0f, n.p.y, n.p.z + dz * 1.0f};
            if (!BodyFits(w, Vector3Lerp(n.p, over, 0.55f), 0.05f, C.height, C.radius * 0.9f)) continue;   // (a rail or a net)
            float g = w.GroundAt(over, 0.25f, -0.4f); bool pit = w.PitAt({over.x, g + 0.1f, over.z}) >= 0;
            if (n.p.y - g > C.safeFall + 0.05f && !pit) continue;
            int j = find((int)floorf(over.x), (int)floorf(over.z), g); if (j < 0) continue;
            n.out.push_back({j, 1.6f, NE_DROP, 0});
        }
    }
    // the ladders and nets: from the foot up to the deck
    for (int c = 0; c < (int)a.climbs.size(); c++) {
        const Climb& cl = a.climbs[c]; Vector3 foot{(cl.lo.x + cl.hi.x) / 2 - cl.into.x * 0.0f, cl.lo.y, (cl.lo.z + cl.hi.z) / 2};
        Vector3 top{foot.x + cl.into.x * 1.1f, cl.hi.y, foot.z + cl.into.z * 1.1f};
        if (fabsf(cl.into.x) > 0.5f) top.x = (cl.into.x > 0 ? cl.hi.x : cl.lo.x) + cl.into.x * 0.7f; else top.z = (cl.into.z > 0 ? cl.hi.z : cl.lo.z) + cl.into.z * 0.7f;
        Vector3 approach = Vector3Subtract(foot, Vector3Scale(cl.into, 0.7f));
        int fa = nav.Nearest(approach, 2.5f), tb = nav.Nearest(top, 2.0f); if (fa < 0 || tb < 0) continue;
        // the foot as its own node (exactly where the climb starts)
        NavNode fn; fn.p = foot; nav.nodes.push_back(fn); int fi = (int)nav.nodes.size() - 1;
        nav.nodes[fa].out.push_back({fi, 0.8f, NE_WALK, 0}); nav.nodes[fi].out.push_back({fa, 0.8f, NE_WALK, 0});
        nav.nodes[fi].out.push_back({tb, (cl.hi.y - cl.lo.y) / C.climb + 1, NE_CLIMB, (int16_t)c});
        if (nav.nodes[tb].p.y - nav.nodes[fa].p.y <= C.safeFall + 0.1f) {}   // (coming down is a drop where it's safe)
    }
    // the slides: top to exit, one way
    for (int s = 0; s < (int)a.slides.size(); s++) {
        const Slide& sl = a.slides[s]; Vector3 into = Vector3Normalize({sl.pts[1].x - sl.pts[0].x, 0, sl.pts[1].z - sl.pts[0].z});
        Vector3 before = Vector3Subtract(sl.pts[0], Vector3Scale(into, 0.6f));
        int ta = nav.Nearest(before, 2.0f), ex = nav.Nearest(Vector3Add(sl.pts.back(), {into.x * 0.5f, 0, into.z * 0.5f}), 3.0f); if (ta < 0 || ex < 0) continue;
        NavNode tn; tn.p = {sl.pts[0].x - into.x * 0.25f, sl.pts[0].y, sl.pts[0].z - into.z * 0.25f}; nav.nodes.push_back(tn); int ti = (int)nav.nodes.size() - 1;
        nav.nodes[ta].out.push_back({ti, 0.8f, NE_WALK, 0}); nav.nodes[ti].out.push_back({ta, 0.8f, NE_WALK, 0});
        nav.nodes[ti].out.push_back({ex, sl.len / C.slide, NE_SLIDE, (int16_t)s});
    }
    return nav;
}
const Nav& NavOf(const Arena& a) { static Nav n = BuildNav(a); return n; }

static std::vector<int> AStar(const Nav& nav, int from, int to) {
    std::vector<int> path; if (from < 0 || to < 0) return path;
    int N = (int)nav.nodes.size(); std::vector<float> g(N, 1e9f); std::vector<int> came(N, -1); std::vector<char> closed(N, 0);
    using QE = std::pair<float, int>; std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    g[from] = 0; open.push({0, from}); Vector3 goal = nav.nodes[to].p; int it = 0;
    while (!open.empty() && it++ < 40000) {
        int u = open.top().second; open.pop(); if (closed[u]) continue; closed[u] = 1;
        if (u == to) break;
        for (const NavEdge& e : nav.nodes[u].out) {
            float ng = g[u] + e.cost; if (ng >= g[e.to]) continue;
            g[e.to] = ng; came[e.to] = u; open.push({ng + Vector3Distance(nav.nodes[e.to].p, goal) * 0.9f, e.to});
        }
    }
    if (came[to] < 0 && from != to) return path;
    for (int v = to; v >= 0; v = came[v]) { path.push_back(v); if (v == from) break; }
    std::reverse(path.begin(), path.end());
    return path;
}
static const NavEdge* EdgeBetween(const Nav& nav, int a, int b) { for (const NavEdge& e : nav.nodes[a].out) if (e.to == b) return &e; return nullptr; }

// ---------------------------------------------------------------- a bot's memory (per world, per player)
struct BotMem { std::vector<int> path; size_t at = 0; Vector3 goal{}; float repath = 0, stuck = 0, t = 0; Vector3 lastPos{}; int target = -1; float aimErrX = 0, aimErrY = 0, strafe = 1, strafeT = 0; int role = 0; float cannonT = 0, lastSeenT = 0; float wander = 0; Vector3 wanderGoal{}; };
static std::map<const World*, std::vector<BotMem>> gMem;
static BotMem& MemOf(const World& w, int id) {
    auto& v = gMem[&w]; if (v.size() != w.players.size()) v.assign(w.players.size(), BotMem{});
    BotMem& m = v[id]; if (m.t > w.t + 1) m = BotMem{}; m.t = w.t; return m;
}
static float R01(uint32_t& r) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (r & 0xFFFFFF) / 16777216.0f; }

// steer along the path toward goal: fills the movement part of the input (and crouch/use for the path's demands)
static void FollowPath(World& w, const Player& p, BotMem& m, Vector3 goal, Input& in, bool faceMove) {
    const Nav& nav = NavOf(w.arena);
    m.repath -= STEP;
    if (m.path.empty() || m.repath <= 0 || Vector3Distance(goal, m.goal) > 2.0f) {
        m.goal = goal; m.repath = 1.2f + R01(*(uint32_t*)&m.wander) * 0.0f;
        int a = nav.Nearest(p.pos), b = nav.Nearest(goal, 6);
        m.path = AStar(nav, a, b); m.at = 0;
    }
    if (m.path.empty()) { Vector3 d = Vector3Subtract(goal, p.pos); d.y = 0; if (Vector3Length(d) > 0.3f) { d = Vector3Normalize(d); float fx = cosf(p.yaw), fz = sinf(p.yaw); in.moveX = d.x * fx + d.z * fz; in.moveZ = d.x * -fz + d.z * fx; } return; }
    // advance past nodes we've reached
    while (m.at + 1 < m.path.size()) {
        const NavNode& n = nav.nodes[m.path[m.at]]; float d = Vector2Distance({p.pos.x, p.pos.z}, {n.p.x, n.p.z});
        const NavEdge* e = EdgeBetween(nav, m.path[m.at], m.path[m.at + 1]);
        bool reached = d < 0.55f && fabsf(p.pos.y - n.p.y) < 1.0f;
        // (on a slide or a ladder: past it once we're at its far end)
        if (e && (e->kind == NE_CLIMB || e->kind == NE_SLIDE)) { const NavNode& nx = nav.nodes[m.path[m.at + 1]]; if (Vector2Distance({p.pos.x, p.pos.z}, {nx.p.x, nx.p.z}) < 1.2f && fabsf(p.pos.y - nx.p.y) < 0.8f) { m.at++; continue; } }
        if (!reached) {
            // skip ahead if the next node is closer and on our level (the grid's zig-zags)
            const NavNode& nx = nav.nodes[m.path[m.at + 1]];
            if (e && e->kind == NE_WALK && Vector2Distance({p.pos.x, p.pos.z}, {nx.p.x, nx.p.z}) < d && fabsf(nx.p.y - p.pos.y) < 0.6f) { m.at++; continue; }
            break;
        }
        m.at++;
    }
    int cur = m.path[std::min(m.at, m.path.size() - 1)]; int nxt = m.path[std::min(m.at + 1, m.path.size() - 1)];
    const NavEdge* e = m.at + 1 < m.path.size() ? EdgeBetween(nav, cur, nxt) : nullptr;
    Vector3 target = nav.nodes[m.at + 1 < m.path.size() && Vector2Distance({p.pos.x, p.pos.z}, {nav.nodes[cur].p.x, nav.nodes[cur].p.z}) < 0.55f ? nxt : cur].p;
    Vector3 wish{};
    if (e && e->kind == NE_CLIMB && Vector2Distance({p.pos.x, p.pos.z}, {nav.nodes[cur].p.x, nav.nodes[cur].p.z}) < 0.6f) { const Climb& c = w.arena.climbs[e->idx]; wish = c.into; }
    else if (e && e->kind == NE_SLIDE && Vector2Distance({p.pos.x, p.pos.z}, {nav.nodes[cur].p.x, nav.nodes[cur].p.z}) < 0.6f) { const Slide& s = w.arena.slides[e->idx]; wish = Vector3Normalize({s.pts[1].x - s.pts[0].x, 0, s.pts[1].z - s.pts[0].z}); in.use = true; }
    else { wish = Vector3Subtract(target, p.pos); wish.y = 0; float L = Vector3Length(wish); if (L > 1e-3f) wish = Vector3Scale(wish, 1 / L); }
    if (nav.nodes[cur].crouch || nav.nodes[nxt].crouch) in.crouch = true;
    if (p.po == PO_CLIMB) { in.moveX = 1; in.moveZ = 0; if (faceMove) { in.yaw = atan2f(w.arena.climbs[p.climb].into.z, w.arena.climbs[p.climb].into.x); } return; }
    if (faceMove && Vector3Length(wish) > 0.1f) in.yaw = atan2f(wish.z, wish.x);
    float fx = cosf(in.yaw), fz = sinf(in.yaw);
    in.moveX = wish.x * fx + wish.z * fz; in.moveZ = wish.x * -fz + wish.z * fx;
    // stuck: hop, and try again
    if (Vector3Distance(p.pos, m.lastPos) < 0.03f && Vector3Length(wish) > 0.1f) m.stuck += STEP; else m.stuck = std::max(0.0f, m.stuck - STEP * 2);
    m.lastPos = p.pos;
    if (m.stuck > 0.8f) { in.jump = true; m.repath = 0; if (m.stuck > 2.5f) { m.path.clear(); m.stuck = 0; } }
}

// a test helper: walk a player to a spot by the graph
void BotGoto(World& w, int me, Vector3 goal, Input& in) { const Player& p = w.players[me]; BotMem& m = MemOf(w, me); in = Input{}; in.yaw = p.yaw; in.pitch = 0; FollowPath(w, p, m, goal, in, true); }

// aim at a point with a projectile of speed s and gravity scale g (lead and drop)
static void AimAt(const Player& p, Vector3 target, Vector3 tvel, float speed, float grav, Input& in, float errX, float errY) {
    Vector3 eye = p.Eye(); float tof = Vector3Distance(eye, target) / std::max(1.0f, speed);
    Vector3 lead = Vector3Add(target, Vector3Scale(tvel, tof * 0.9f)); tof = Vector3Distance(eye, lead) / std::max(1.0f, speed);
    lead.y += 0.5f * Cfg().gravity * grav * tof * tof;
    Vector3 d = Vector3Subtract(lead, eye);
    in.yaw = atan2f(d.z, d.x) + errX; in.pitch = atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z)) + errY;
}

void BotInput(World& w, int me, Input& in, uint32_t& rng, int skill) {
    const Config& C = Cfg(); Player& p = w.players[me]; BotMem& m = MemOf(w, me);
    Input prev = in; in = Input{}; in.yaw = p.yaw; in.pitch = p.pitch;
    if (w.phase != PH_PLAY) { in.yaw = prev.yaw; return; }
    if (!p.alive) return;
    float skillK = skill == 0 ? 1.8f : skill == 1 ? 1.0f : 0.55f;
    // the store first: a better gun if the wallet allows it, darts when low
    {
        static const int PLAN[] = {G_PISTOL, G_REVOLVER, G_BURST, G_FLYWHEEL, G_LONG, G_ROCKET};
        int want = -1; for (int g : PLAN) if (C.guns[g].cost <= p.cash && (p.gun >= G_COUNT || C.guns[g].cost > C.guns[p.gun].cost)) want = g;
        if (want >= 0 && p.inStore) { in.buy = want; return; }
        if (p.inStore && p.reserve < 24 && p.cash >= C.dartPackCost) { in.buy = 100; return; }
        if (p.inStore && w.mode == MD_BOMB && p.team != w.attackers && !p.kit && p.cash >= C.disarmKit + 150) { in.buy = 101; return; }
        bool shopping = want >= 0 && w.mode != MD_BOMB ? true : (want >= 0 && w.phase == PH_PLAY && w.phaseT < 20);
        if (shopping && !p.inStore && p.carry < 0 && !p.bomb) {
            int s = -1; float bd = 1e9f; for (int i = 0; i < (int)w.arena.stores.size(); i++) if (w.mode == MD_FFA || w.arena.stores[i].team == p.team) { float d = Vector3Distance(w.arena.stores[i].p, p.pos); if (d < bd) { bd = d; s = i; } }
            if (s >= 0 && bd < 26) { FollowPath(w, p, m, w.arena.stores[s].p, in, true); return; }
        }
    }
    // a ready streak reward: use it
    for (int r = 0; r < 6; r++) if (((p.rewardReady >> r) & 1) && !((p.rewardUsed >> r) & 1)) { if (r == 5 && p.pos.y > L2 + 0.2f) continue; if (r == 3) continue; in.reward = r; return; }
    // the target: the nearest enemy you can see (or the one you last saw)
    int tgt = -1; float td = 1e9f;
    for (auto& q : w.players) {
        if (!w.Enemies(p, q) || !q.alive || q.po == PO_SLIDE || q.submerged) continue;
        Vector3 c{q.pos.x, q.pos.y + q.Height() * 0.6f, q.pos.z}; float d = Vector3Distance(p.Eye(), c);
        if (d > 45 || !w.Sees(p.Eye(), c)) continue;
        Vector3 to = Vector3Normalize(Vector3Subtract(c, p.Eye())); if (Vector3DotProduct(to, p.Look()) < -0.2f && d > 8) continue;   // (behind you and not close: unseen)
        if (d < td) { td = d; tgt = q.id; }
    }
    if (tgt != m.target) { m.target = tgt; m.aimErrX = (R01(rng) - 0.5f) * 0.12f * skillK; m.aimErrY = (R01(rng) - 0.5f) * 0.08f * skillK; }
    m.aimErrX *= 1 - STEP * 1.2f / skillK; m.aimErrY *= 1 - STEP * 1.2f / skillK;   // (the aim settles)
    // on a cannon: sweep what it can reach; get off when there's nothing, or no balls
    if (p.po == PO_CANNON) {
        const Cannon& k = w.cannons[p.cannon]; m.cannonT += STEP;
        if (tgt >= 0) { const Player& q = w.players[tgt]; AimAt(p, {q.pos.x, q.pos.y + 1.0f, q.pos.z}, q.vel, C.cannonSpeed, C.ballGrav, in, m.aimErrX * 0.5f, m.aimErrY * 0.5f); in.fire = !k.overheated && k.heat < C.cannonHeat - 0.2f; m.cannonT = 0; }
        if (k.hopper <= 0 || m.cannonT > 4) in.use = true;
        return;
    }
    if (p.po == PO_DRIVE) {   // the tank: roll at the nearest enemy and fire
        if (tgt >= 0) { const Player& q = w.players[tgt]; AimAt(p, {q.pos.x, q.pos.y + 1.0f, q.pos.z}, q.vel, 22, 0.45f, in, m.aimErrX, m.aimErrY); in.fire = true; in.moveX = td > 8 ? 1.0f : 0; }
        else { int best = -1; float bd = 1e9f; for (auto& q : w.players) if (w.Enemies(p, q) && q.alive) { float d = Vector3Distance(q.pos, p.pos); if (d < bd) { bd = d; best = q.id; } } if (best >= 0) FollowPath(w, p, m, w.players[best].pos, in, true); }
        return;
    }
    // the objective sets where to go; a fight interrupts it
    Vector3 goal = p.pos; bool haveGoal = false;
    if (m.role == 0) m.role = 1 + (int)(R01(rng) * 3);   // 1 attacker, 2 defender, 3 roamer
    if (w.mode == MD_CTF) {
        int them = 1 - p.team; const Flag& enemy = w.flags[them]; const Flag& mine = w.flags[p.team];
        if (p.carry >= 0) { goal = mine.home; haveGoal = true; }
        else if (!mine.home_ && mine.carrier < 0) { goal = mine.p; haveGoal = true; }
        else if (mine.carrier >= 0) { goal = w.players[mine.carrier].pos; haveGoal = true; }
        else if (m.role != 2 || enemy.carrier < 0) { goal = enemy.carrier >= 0 ? w.players[enemy.carrier].pos : enemy.p; haveGoal = true; }
        else { goal = Vector3Add(mine.home, {p.team == 0 ? 3.0f : -3.0f, 0, 0}); haveGoal = true; }
    } else if (w.mode == MD_BOMB) {
        int def = 1 - w.attackers;
        if (p.team == w.attackers) {
            if (!w.bomb.planted) {
                if (p.bomb) { int best = -1; float bd = 1e9f; for (int i = 0; i < (int)w.arena.bombSites.size(); i++) if (w.arena.bombSites[i].team == def) { float d = Vector3Distance(w.arena.bombSites[i].p, p.pos) + (m.role == 1 ? 0 : 6); if (d < bd) { bd = d; best = i; } } if (best >= 0) { goal = w.arena.bombSites[best].p; haveGoal = true; if (Vector3Distance(p.pos, goal) < 1.4f) { in.use = true; in.yaw = p.yaw; return; } } }
                else if (w.bomb.carrier < 0) { goal = w.bomb.p; haveGoal = true; }
                else { goal = w.players[w.bomb.carrier].pos; haveGoal = m.role != 3; if (!haveGoal) { goal = w.arena.bombSites[def * 2 + (me % 2)].p; haveGoal = true; } }
            } else { goal = w.bomb.p; haveGoal = true; }
        } else {
            if (w.bomb.planted) { goal = w.bomb.p; haveGoal = true; if (Vector3Distance(p.pos, goal) < 1.4f && tgt < 0) { in.use = true; return; } }
            else { goal = w.arena.bombSites[def * 2 + (me % 2)].p; goal.x += (def == 0 ? 1.0f : -1.0f) * 2.5f; haveGoal = true; }
        }
    }
    // go get darts: low, and some on the floor nearby
    int mag = p.mag[p.wield == 1 && p.gun < G_COUNT ? 1 : 0];
    if (p.reserve + mag < 8 && tgt < 0) {
        int best = -1; float bd = 9; for (int i = 0; i < (int)w.darts.size(); i++) if (!w.darts[i].live) { float d = Vector3Distance(w.darts[i].p, p.pos); if (d < bd && fabsf(w.darts[i].p.y - p.pos.y) < 1.5f) { bd = d; best = i; } }
        if (best >= 0) {
            Vector3 dp = w.darts[best].p;
            if (bd < 3.0f) { Vector3 d = Vector3Subtract(dp, p.Eye()); in.yaw = atan2f(d.z, d.x); in.pitch = atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z)); in.vacuum = true; in.moveX = bd > 1.2f ? 0.6f : 0; return; }
            goal = dp; haveGoal = true;
        }
    }
    // a cannon nearby, loaded, with nobody on it: some bots like a go
    if (!haveGoal && tgt < 0 && p.carry < 0 && p.ball < 0 && m.role == 3) {
        for (int c = 0; c < (int)w.cannons.size(); c++) {
            if (w.cannons[c].op >= 0 || w.cannons[c].hopper < 12 || (w.arena.cannons[c].team >= 0 && w.arena.cannons[c].team != p.team && w.mode != MD_FFA)) continue;
            Vector3 seat = w.CannonSeat(c); float d = Vector3Distance(seat, p.pos);
            if (d < 1.4f) { in.use = true; m.cannonT = 0; return; }
            if (d < 14) { goal = seat; haveGoal = true; break; }
        }
    }
    // in a pit with empty hands and nothing to shoot: grab a ball
    if (p.inPit && p.ball < 0 && p.carry < 0 && (tgt < 0 || td > 12) && R01(rng) < 0.02f) in.grab = true;
    if (p.grabT > 0) { in.grab = true; return; }
    if (tgt >= 0) {
        const Player& q = w.players[tgt]; Vector3 c{q.pos.x, q.pos.y + q.Height() * 0.6f, q.pos.z};
        bool clear = w.ShotClear(p.Eye(), c);
        if (p.ball >= 0) {   // a ball: a one-hit knockout until its first bounce
            AimAt(p, c, q.vel, C.throwSpeed, C.ballGrav, in, m.aimErrX, m.aimErrY - C.throwLoft * DEG2RAD);
            in.fire = clear && td < 24;
        } else if (td < 1.9f && !p.inTunnel == false) { AimAt(p, c, {}, 100, 0, in, 0, 0); in.knife = true; }
        else if (td < 1.7f) { AimAt(p, c, {}, 100, 0, in, 0, 0); in.knife = true; }
        else {
            const GunDef& g = w.Gun(p);
            AimAt(p, c, q.vel, g.speed, g.grav, in, m.aimErrX, m.aimErrY);
            float off = fabsf(atan2f(sinf(in.yaw - p.yaw), cosf(in.yaw - p.yaw))) + fabsf(in.pitch - p.pitch);
            in.fire = clear && off < 0.12f && td < (g.speed * 1.6f);
            in.aim = td > 12;
            if (mag <= 0 && p.reserve <= 0) in.knife = td < 2;
            // the long gun wants the starter for close work
            if (p.gun < G_COUNT && td < 6 && (p.gun == G_LONG || p.gun == G_CROSSBOW || p.gun == G_ROCKET)) in.slot = 0; else if (p.gun < G_COUNT && p.wield == 0 && p.carry < 0) in.slot = 1;
        }
        // keep moving: strafe, close in, or back off; or follow the objective if we have one
        m.strafeT -= STEP; if (m.strafeT <= 0) { m.strafeT = 0.6f + R01(rng) * 1.2f; m.strafe = R01(rng) < 0.5f ? -1.0f : 1.0f; }
        if (haveGoal && (p.carry >= 0 || p.bomb)) { Input mv = in; FollowPath(w, p, m, goal, mv, false); in.moveX = mv.moveX; in.moveZ = mv.moveZ; in.crouch = mv.crouch; in.use = in.use || mv.use; in.jump = mv.jump; }
        else if (!clear) { Input mv = in; FollowPath(w, p, m, q.pos, mv, false); in.moveX = mv.moveX; in.moveZ = mv.moveZ; in.crouch = mv.crouch; in.jump = mv.jump; }
        else { in.moveZ = m.strafe * 0.8f; in.moveX = td > 14 ? 0.7f : td < 5 ? -0.3f : 0; if (p.inTunnel) { in.moveZ = 0; in.moveX = 1; } }
        return;
    }
    if (!haveGoal) {
        // hunt: toward the nearest enemy (bots know the hall), now and then somewhere else for a while
        m.wander -= STEP;
        int best = -1; float bd = 1e9f; for (auto& q : w.players) if (w.Enemies(p, q) && q.alive) { float d = Vector3Distance(q.pos, p.pos); if (d < bd) { bd = d; best = q.id; } }
        if (m.wander <= 0) { m.wander = 6 + R01(rng) * 8; const Nav& nav = NavOf(w.arena); m.wanderGoal = nav.nodes[(int)(R01(rng) * nav.nodes.size()) % nav.nodes.size()].p; }
        goal = best >= 0 && m.role != 3 ? w.players[best].pos : m.wanderGoal; haveGoal = true;
    }
    FollowPath(w, p, m, goal, in, true);
    in.pitch = -0.05f;
}

// ---------------------------------------------------------------- the acceptance tests (the spec's milestones)
static void Tick(World& w, int n, std::vector<uint32_t>& rng, int skill, int scripted = -1) {
    for (int i = 0; i < n; i++) { for (auto& p : w.players) if (p.id != scripted) BotInput(w, p.id, p.in, rng[p.id], skill); w.Step(); }
}
int RunBallPitTest() {
    int fails = 0; auto check = [&](bool ok, const char* what) { std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what); if (!ok) fails++; };
    const Config& C = Cfg();
    std::printf("Ball Pit Brawl test\n");
    // 1: the kit and the map; a player can climb from the ground to level 3 and slide back down
    {
        World w; w.Init(MD_TDM, 2, 1); w.phase = PH_PLAY; w.players[1].present = false; w.players[1].alive = false;
        Player& p = w.players[0];
        const Nav& nav = NavOf(w.arena);
        check(nav.nodes.size() > 1500, "the navigation graph covers the hall (floor and decks)");
        std::printf("    (nav nodes %d)\n", (int)nav.nodes.size());
        p.pos = {-14, 0, 2}; p.fallTop = 0;
        Vector3 goal{-24, L3, 3}; float tUp = 0; bool up = false;
        for (int i = 0; i < 60 * 60 && !up; i++) { BotGoto(w, 0, goal, p.in); w.Step(); tUp += STEP; if (p.pos.y > L3 - 0.1f && Vector2Distance({p.pos.x, p.pos.z}, {goal.x, goal.z}) < 2.0f) up = true; }
        check(up, "a player climbs from the ground floor to level 3 (the cargo net, then the ladder)");
        std::printf("    (%.1f s)\n", tUp);
        // the slide down from level 3
        const Slide& sl = w.arena.slides[0]; Vector3 top = sl.pts[0]; Vector3 into = Vector3Normalize({sl.pts[1].x - top.x, 0, sl.pts[1].z - top.z});
        p.pos = Vector3Subtract(top, Vector3Scale(into, 0.4f)); p.pos.y = w.GroundAt({p.pos.x, top.y + 0.2f, p.pos.z}, 0.2f, 0); p.vel = {}; p.po = PO_STAND; p.grounded = true;
        bool slid = false, landed = false; float hpBefore = p.hp;
        for (int i = 0; i < 60 * 10; i++) { p.in = Input{}; p.in.yaw = atan2f(into.z, into.x); p.in.moveX = 1; w.Step(); if (p.po == PO_SLIDE) slid = true; if (slid && p.po == PO_STAND && p.grounded) { landed = true; break; } }
        check(slid && landed && p.pos.y < 1.0f, "and slides back down to the ground floor (into a pit)");
        check(p.hp >= hpBefore, "the slide's landing in the pit is safe");
    }
    // 2: movement: the table's speeds; submerged players can't be hit by darts
    {
        World w; w.Init(MD_TDM, 2, 2); w.phase = PH_PLAY; Player& p = w.players[0]; Player& q = w.players[1];
        q.pos = {0, 0, -14.5f};
        auto speedAt = [&](Vector3 at, bool crouch, float yaw) {
            p.pos = at; p.vel = {}; p.po = PO_STAND; p.grounded = true; Vector3 a{};
            for (int i = 0; i < 90; i++) { p.in = Input{}; p.in.yaw = yaw; p.in.moveX = 1; p.in.crouch = crouch; w.Step(); if (i == 29) a = p.pos; }
            return Vector2Distance({a.x, a.z}, {p.pos.x, p.pos.z}) / (60 * STEP);
        };
        float run = speedAt({-27.8f, L2, -10}, false, PI / 2), cr = speedAt({-27.8f, L2, -10}, true, PI / 2);
        float wade = speedAt({-6, 0, -7}, false, 0);
        float tun = speedAt({-19.2f, 0, 0}, true, 0);
        float brg = speedAt({-19, L3, 2.5f}, false, 0);
        std::printf("    (run %.2f, crouch %.2f, wade %.2f, tunnel %.2f, bridge %.2f m/s)\n", run, cr, wade, tun, brg);
        check(fabsf(run - C.run) < 0.3f && fabsf(cr - C.crouch) < 0.25f && fabsf(wade - C.wade) < 0.25f && fabsf(tun - C.tunnel) < 0.25f && fabsf(brg - C.bridge) < 0.3f, "run, crouch, wade, tunnel and bridge speeds match the table");
        // climbing and jumping
        p.pos = {-18.6f, 0, 2}; p.vel = {}; p.po = PO_STAND; float y0 = 0, t0 = 0; bool climbing = false;
        for (int i = 0; i < 240; i++) { p.in = Input{}; p.in.yaw = PI; p.in.moveX = 1; w.Step(); if (p.po == PO_CLIMB && !climbing) { climbing = true; y0 = p.pos.y; t0 = w.t; } if (climbing && w.t - t0 > 1.0f) break; }
        float climbSpd = climbing ? (p.pos.y - y0) / (w.t - t0) : 0; std::printf("    (climb %.2f m/s)\n", climbSpd);
        check(climbing && fabsf(climbSpd - C.climb) < 0.15f, "the cargo net climbs at 1.5 m/s");
        p.po = PO_STAND; p.pos = {-26, L2, -10}; p.vel = {}; p.grounded = true; float top = 0;
        for (int i = 0; i < 90; i++) { p.in = Input{}; p.in.jump = i == 2; w.Step(); top = std::max(top, p.pos.y - L2); }
        std::printf("    (jump %.2f m)\n", top); check(fabsf(top - C.jumpH) < 0.08f, "a jump reaches 1 m");
        // submerged in a pit: crouch under the balls, and darts can't touch you
        p.pos = {-5, 0, -6}; p.vel = {}; p.po = PO_STAND;
        for (int i = 0; i < 30; i++) { p.in = Input{}; p.in.crouch = true; w.Step(); }
        float sub = speedAt({-5, 0, -6}, true, 0);
        check(p.submerged, "crouching in a ball pit submerges you");
        std::printf("    (submerged %.2f m/s)\n", sub); check(fabsf(sub - C.submerged) < 0.2f, "submerged, you move at 1 m/s");
        p.in = Input{}; p.in.crouch = true; w.Step();
        q.pos = {-5, 0, -1}; q.alive = true; float before = p.hp; Dart d; d.p = {-5, 0.3f, -2.5f}; d.v = {0, 0, -20}; d.owner = 1; d.team = 1; d.dmg = 35; d.grav = 0; w.darts.push_back(d);
        for (int i = 0; i < 30; i++) { p.in = Input{}; p.in.crouch = true; q.in = Input{}; w.Step(); }
        check(p.hp == before, "a dart passes over a submerged player");
        // a fall: 3 m is safe, 6 m hurts, a pit is safe from any height
        p.po = PO_STAND; p.pos = {-10, L3 + 3, 13.5f}; p.vel = {}; p.hp = 100; p.grounded = false; p.fallTop = p.pos.y;
        for (int i = 0; i < 120; i++) { p.in = Input{}; w.Step(); }
        check(p.hp < 100, "a long fall onto a deck hurts");
        p.pos = {-7.5f, 9, -7}; p.vel = {}; p.hp = 100; p.grounded = false; p.fallTop = p.pos.y; for (int i = 0; i < 120; i++) { p.in = Input{}; w.Step(); }
        check(p.hp == 100, "a 9 m fall into a ball pit is safe");
    }
    // 3: the knife and the starter blaster, health, respawn
    {
        World w; w.Init(MD_TDM, 2, 3); w.phase = PH_PLAY; Player& a = w.players[0]; Player& v = w.players[1];
        auto reset = [&]() { a.pos = {-26, L2, -9}; a.yaw = PI / 2; a.vel = {}; v.pos = {-26, L2, -7.6f}; v.yaw = PI / 2; v.alive = true; v.hp = 100; v.po = PO_STAND; v.vel = {}; v.sinceHurt = 0; v.storeT = 99; a.storeT = 99; a.knifeT = 0; };
        reset(); v.yaw = -PI / 2;   // (facing the attacker)
        int swings = 0; for (int i = 0; i < 300 && v.alive; i++) { a.in = Input{}; a.in.yaw = PI / 2; a.in.pitch = -0.2f; a.in.knife = i % 40 == 0; v.in = Input{}; v.in.yaw = -PI / 2; if (a.in.knife) swings++; w.Step(); }
        check(!v.alive && swings == 2, "two knife hits knock out a full-health player");
        reset(); v.yaw = PI / 2;   // (facing away: the attacker is behind)
        for (int i = 0; i < 20 && v.alive; i++) { a.in = Input{}; a.in.yaw = PI / 2; a.in.pitch = -0.2f; a.in.knife = i == 0; v.in = Input{}; v.in.yaw = PI / 2; w.Step(); }
        check(!v.alive, "a knife hit from behind is an instant knockout");
        reset(); v.yaw = -PI / 2; a.pos = {-26, L2, -12}; a.reserve = 12;
        int shots = 0, evBefore = (int)w.evCount;
        for (int i = 0; i < 60 * 8 && v.alive; i++) {
            a.in = Input{}; Vector3 d = Vector3Subtract({v.pos.x, v.pos.y + 1.1f, v.pos.z}, a.Eye()); a.in.yaw = atan2f(d.z, d.x); a.in.pitch = atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z)) + 0.01f; a.in.fire = true; a.in.aim = true;
            v.in = Input{}; v.in.yaw = -PI / 2; int m0 = a.mag[0]; w.Step(); if (a.mag[0] < m0) shots++;
        }
        (void)evBefore;
        std::printf("    (starter shots fired: %d)\n", shots);
        check(!v.alive && shots == 3, "three starter darts knock out a full-health player");
        check(v.respawnT > 0, "a knocked-out player waits to respawn");
        for (int i = 0; i < 60 * 6; i++) { a.in = Input{}; v.in = Input{}; w.Step(); }
        check(v.alive && v.hp == C.health, "and comes back after 5 s at full health");
        v.hp = 50; v.sinceHurt = 0; for (int i = 0; i < 60 * 3; i++) { v.in = Input{}; w.Step(); } float mid = v.hp; for (int i = 0; i < 60 * 3; i++) { v.in = Input{}; w.Step(); }
        check(mid == 50 && v.hp > 60, "health regenerates only after 4 s without damage");
    }
    // 4: balls: pit pickup, throwing, a one-hit knockout until the first bounce
    {
        World w; w.Init(MD_TDM, 2, 4); w.phase = PH_PLAY; Player& a = w.players[0]; Player& v = w.players[1];
        int total0 = w.BallsTotal();
        a.pos = {-6, 0, 4}; a.storeT = 99; v.storeT = 99;
        int pitBefore = w.arena.pits.back().balls; float tg = 0;
        for (int i = 0; i < 60 && a.ball < 0; i++) { a.in = Input{}; a.in.grab = i == 0; w.Step(); tg += STEP; }
        check(a.ball >= 0 && w.arena.pits.back().balls == pitBefore - 1, "reaching into a pit takes a ball");
        std::printf("    (grab %.2f s)\n", tg); check(fabsf(tg - C.grabTime) < 0.05f, "it takes 0.4 s");
        v.pos = {-6, 0, -2}; v.alive = true; v.hp = 100; v.yaw = PI / 2;
        // stand v outside the pit (so the ball isn't swallowed): a deck target instead
        a.pos = {-26, L2, -9}; v.pos = {-26, L2, 3}; v.in = Input{};
        for (int i = 0; i < 90 && v.alive; i++) { a.in = Input{}; Vector3 d = Vector3Subtract({v.pos.x, v.pos.y + 1.0f, v.pos.z}, a.Eye()); a.in.yaw = atan2f(d.z, d.x); a.in.pitch = atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z)) - 0.03f; a.in.fire = i == 0; v.in = Input{}; w.Step(); }
        check(!v.alive, "a thrown ball knocks out a full-health player in one hit");
        // a bounced ball is just a ball
        v.alive = true; v.hp = 100; v.po = PO_STAND; v.pos = {-24, L2, -1}; Ball b; b.st = BS_LOOSE; b.p = {-24, L2 + 1.0f, -3}; b.v = {0, 0, 9}; b.thrower = 0; b.team = 0; w.balls.push_back(b); w.arena.pits[0].balls--;
        for (int i = 0; i < 40; i++) { v.in = Input{}; w.Step(); }
        check(v.alive && v.hp == 100, "a ball after its first bounce doesn't knock anyone out");
        for (int i = 0; i < 60 * 20; i++) { a.in = Input{}; v.in = Input{}; w.Step(); }
        check(w.BallsTotal() == total0, "the supply is exact: every ball is counted somewhere (pits, belt, hoppers, loose)");
    }
    // 5: the conveyor and the cannons: empty one, wait, and only balls that came back through the conveyor refill it
    {
        World w; w.Init(MD_TDM, 2, 5); w.phase = PH_PLAY; Player& a = w.players[0]; w.players[1].present = false; w.players[1].alive = false;
        int total0 = w.BallsTotal(); int c = 0; Vector3 seat = w.CannonSeat(c);
        a.pos = seat; a.in = Input{}; a.in.use = true; w.Step();
        check(a.po == PO_CANNON, "a player gets on a cannon");
        int shots = 0, hop0 = w.cannons[c].hopper; float firstT = -1, lastT = 0; bool over = false;
        for (int i = 0; i < 60 * 20 && w.cannons[c].hopper > 0; i++) { a.in = Input{}; a.in.yaw = w.arena.cannons[c].yaw0; a.in.pitch = 0.15f; a.in.fire = true; int h = w.cannons[c].hopper; w.Step(); if (w.cannons[c].hopper < h) { shots++; if (firstT < 0) firstT = w.t; lastT = w.t; } if (w.cannons[c].overheated) over = true; }
        check(over, "continuous fire overheats it (after 3 s)");
        check(shots == hop0 && w.cannons[c].hopper == 0, "it fires until its hopper is empty");
        int hopperGain = 0, fromBelt = 0; size_t beltBefore = 0;
        for (int i = 0; i < 60 * 30; i++) { a.in = Input{}; beltBefore = w.belt.arrive.size(); int h = w.cannons[c].hopper; int ev = (int)w.evCount; w.Step(); if (w.cannons[c].hopper > h) { hopperGain += w.cannons[c].hopper - h; } (void)ev; (void)beltBefore; }
        for (const auto& e : w.events) if (e.kind == EV_HOPPER && (int)e.a == c) fromBelt++;
        std::printf("    (fired %d, refilled %d, lift deliveries logged %d)\n", shots, hopperGain, fromBelt);
        check(hopperGain > 0 && hopperGain <= shots, "after waiting it refills, only from balls that rode the conveyor back");
        check(w.BallsTotal() == total0, "and the total supply never changes");
    }
    // 6: score, the store, the vacuum, gun tiers
    {
        World w; w.Init(MD_TDM, 2, 6); w.phase = PH_PLAY; Player& a = w.players[0];
        a.pos = w.arena.stores[0].p; a.cash = C.guns[G_REVOLVER].cost; a.in = Input{}; w.Step();
        a.in = Input{}; a.in.buy = G_REVOLVER; w.Step();
        check(a.gun == G_REVOLVER && a.cash == 0, "buying with exactly enough score works");
        a.cash = C.guns[G_BURST].cost - 1; a.in = Input{}; a.in.buy = G_BURST; w.Step();
        check(a.gun == G_REVOLVER, "one short isn't enough");
        int sc = a.score; a.cash = 500; a.in = Input{}; a.in.buy = 100; w.Step(); check(a.score == sc, "spending never lowers the leaderboard total");
        // vacuum: darts from both teams on the floor ahead
        a.pos = {-26, L2, -10}; a.reserve = 0; w.darts.clear();
        for (int k = 0; k < 6; k++) { Dart d; d.live = false; d.p = {-26 + (k % 3) * 0.3f - 0.3f, L2 + 0.02f, -8.0f + (k / 3) * 0.4f}; d.owner = k % 2 ? 1 : 0; d.team = k % 2; w.darts.push_back(d); }
        float tv = 0; for (int i = 0; i < 120 && a.reserve < 6; i++) { a.in = Input{}; a.in.yaw = PI / 2; a.in.pitch = -0.4f; a.in.vacuum = true; w.Step(); tv += STEP; }
        check(a.reserve == 6, "vacuuming picks up both teams' darts"); std::printf("    (6 darts in %.2f s)\n", tv);
        check(tv < 0.75f, "at about 10 darts a second");
        // streaks: a reward unlocks at its threshold, once per life
        a.streak = 0; a.rewardReady = 0; a.rewardPick = 0b000111;
        int before = a.score; Player& v = w.players[1]; v.pos = {-26, L2, -8.6f}; v.alive = true; v.hp = 1; v.storeT = 99; a.storeT = 99; a.pos = {-26, L2, -10}; a.yaw = PI / 2;
        for (int k = 0; k < 3; k++) { v.alive = true; v.hp = 1; v.po = PO_STAND; v.pos = {-26, L2, -8.6f}; a.in = Input{}; a.in.yaw = PI / 2; a.in.pitch = -0.2f; a.in.knife = true; a.knifeT = 0; w.Step(); }
        check(a.score - before >= 300 && (a.rewardReady & 1), "300 streak unlocks the dart refill");
        a.reserve = 0; a.in = Input{}; a.in.reward = 0; w.Step(); check(a.reserve == C.dartMax, "and using it fills your reserve");
    }
    // 7: the modes complete with bots and declare the right winner
    for (int md = 0; md < MD_COUNT; md++) {
        World w; int n = md == MD_FFA ? 6 : 8; w.Init(md, n, 70 + md);
        for (auto& p : w.players) p.bot = true;
        std::vector<uint32_t> rng(n); for (int i = 0; i < n; i++) rng[i] = 1234 + i * 77;
        // (quicker matches for the test)
        Config& cm = CfgMutable(); ModeDef keep = cm.modes[md];
        if (md == MD_FFA) cm.modes[md].toWin = 6; if (md == MD_TDM) cm.modes[md].toWin = 15; if (md == MD_CTF) cm.modes[md].toWin = 1; if (md == MD_BOMB) cm.modes[md].toWin = 2;
        cm.modes[md].timeLimit = md == MD_BOMB ? 120 : 420;
        int steps = 0; while (w.phase != PH_OVER && steps < 60 * 60 * 25) { Tick(w, 60, rng, 1); steps += 60; }
        bool right = false;
        if (md == MD_FFA) { int lead = w.Leader(); right = w.winner == lead; }
        else if (md == MD_TDM) right = (w.winner == 0 && w.teamKOs[0] >= w.teamKOs[1]) || (w.winner == 1 && w.teamKOs[1] >= w.teamKOs[0]) || (w.winner == -1 && w.teamKOs[0] == w.teamKOs[1]);
        else if (md == MD_CTF) right = (w.winner >= 0 && w.caps[w.winner] >= w.caps[1 - w.winner]) || (w.winner == -1 && w.caps[0] == w.caps[1]);
        else right = w.winner >= 0 && w.roundWins[w.winner] >= cm.modes[md].toWin;
        int kos = 0; for (auto& p : w.players) kos += p.kos;
        std::printf("    (%s: %.0f s, winner %d, KOs %d, team KOs %d-%d, caps %d-%d, rounds %d-%d, supply %d)\n", ModeName(md), steps * STEP, w.winner, kos, w.teamKOs[0], w.teamKOs[1], w.caps[0], w.caps[1], w.roundWins[0], w.roundWins[1], w.BallsTotal());
        char what[96]; std::snprintf(what, sizeof what, "%s completes with bots and declares the right winner", ModeName(md));
        check(w.phase == PH_OVER && right, what);
        cm.modes[md] = keep;
    }
    // the kids' swarm hunts: within a few seconds most of them are hanging off enemies, who slow down
    {
        World w; w.Init(MD_TDM, 4, 91); for (auto& p : w.players) { p.bot = false; p.storeT = 99; }
        w.phase = PH_PLAY; Player& a = w.players[0]; int foeId = -1; for (auto& q : w.players) if (q.team != a.team) { foeId = q.id; break; }
        Player& foe = w.players[foeId]; foe.pos = {6, 0, 3}; foe.vel = {};
        a.rewardReady = 63; a.rewardPick = 63; a.in = Input{}; a.in.reward = 2; w.Step(); a.in = Input{};
        int kids = 0; for (auto& e : w.ents) if (!e.dead && e.kind == E_KID) kids++;
        for (int i = 0; i < 60 * 6; i++) { for (auto& q : w.players) q.in = Input{}; w.Step(); }
        int near = 0; for (auto& e : w.ents) if (!e.dead && e.kind == E_KID) for (auto& q : w.players) if (q.team != a.team && q.alive && Vector3Distance(e.p, q.pos) < 1.6f) { near++; break; }
        char what[96]; std::snprintf(what, sizeof what, "the kids' swarm finds the enemies (%d of %d kids on them after 6 s)", near, kids);
        check(kids == 8 && near >= 5, what);
        Vector3 p0 = foe.pos; for (int i = 0; i < 60; i++) { foe.in = Input{}; foe.in.moveX = 1; foe.in.yaw = 0; w.Step(); }
        float mobbed = Vector3Distance(p0, foe.pos);
        std::snprintf(what, sizeof what, "a mobbed enemy is slowed (%.1f m in 1 s)", mobbed); check(mobbed < Cfg().run * 0.8f, what);
    }
    std::printf(fails ? "Ball Pit Brawl: %d FAILED\n" : "Ball Pit Brawl: all checks passed\n", fails);
    return fails ? 1 : 0;
}

// --ballpit-sim <matches> <mode> <players>: whole bot matches; how long they take, how they're won, what gets used
int RunBallPitSim(int matches, int mode, int players) {
    std::printf("Ball Pit Brawl sim: %d %s matches, %d players\n", matches, ModeName(mode), players);
    double secs = 0; int kos = 0, cannonKO = 0, ballKO = 0, knifeKO = 0, dartKO = 0, buys = 0, rewards = 0, timeouts = 0, slides = 0, climbs = 0;
    for (int m = 0; m < matches; m++) {
        World w; w.Init(mode, players, 1000 + m); std::vector<uint32_t> rng(players); for (int i = 0; i < players; i++) rng[i] = 99 + i * 31 + m;
        size_t seen = 0; int steps = 0;
        while (w.phase != PH_OVER && steps < 60 * 60 * 20) {
            Tick(w, 60, rng, 1); steps += 60;
            for (; seen < w.events.size(); seen++) { const Event& e = w.events[seen]; if (e.kind == EV_KO) { kos++; if (e.a > 0.5f) cannonKO++; } if (e.kind == EV_BUY) buys++; if (e.kind == EV_REWARD && e.a < 10) rewards++; if (e.kind == EV_SLIDE) slides++; if (e.kind == EV_CLIMB) climbs++; }
            if (seen > w.events.size()) seen = w.events.size();
            if (w.events.size() > 300) { w.events.clear(); seen = 0; }
        }
        if (w.phase != PH_OVER) timeouts++;
        secs += steps * STEP; (void)ballKO; (void)knifeKO; (void)dartKO;
        std::printf("  match %d: %.0f s, winner %d, team KOs %d-%d, caps %d-%d, rounds %d-%d\n", m + 1, steps * STEP, w.winner, w.teamKOs[0], w.teamKOs[1], w.caps[0], w.caps[1], w.roundWins[0], w.roundWins[1]);
    }
    std::printf("  average %.0f s; knockouts %d (%d by cannon); buys %d; rewards %d; slides %d; climbs %d; unfinished %d\n", secs / std::max(1, matches), kos, cannonKO, buys, rewards, slides, climbs, timeouts);
    return 0;
}

}  // namespace bp
