// ============================================================================
//  DEPTH - generating the expedition chart (see chart.h). The rules, from the Master Reference:
//   1. rooms on a loose grid, the entrance on one edge, the boss at the greatest distance from it;
//   2. a spanning tree, then the tier's loops, so at least two distinct routes reach the boss;
//   3. fights on the shortest route; treasure and curios pushed off it, so loot always costs a detour;
//   4. rest rooms about halfway to the boss, never beside it;
//   5. reject any map where the boss can be reached in fewer fights than the tier's minimum, or with more than two
//      dead ends.
// ============================================================================
#include "chart.h"
#include "game.h"
#include <algorithm>
#include <cstdio>
#include <queue>

namespace {
struct Rnd {
    unsigned s;
    unsigned N() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int I(int lo, int hi) { return lo + (int)(N() % (unsigned)(hi - lo + 1)); }
    float F() { return (N() & 0xFFFFFF) / 16777216.0f; }
};

std::vector<int> Hops(const Chart& c, int from, int skipEdge = -1) {
    std::vector<int> d(c.rooms.size(), -1);
    std::queue<int> q;
    d[from] = 0; q.push(from);
    while (!q.empty()) {
        int r = q.front(); q.pop();
        for (int e = 0; e < (int)c.edges.size(); e++) {
            if (e == skipEdge || (c.edges[e].a != r && c.edges[e].b != r)) continue;
            int o = c.Other(e, r);
            if (d[o] < 0) { d[o] = d[r] + 1; q.push(o); }
        }
    }
    return d;
}
int HallFights(const ChartEdge& e) { int n = 0; for (auto s : e.segs) n += s == CorridorEvent::HallFight; return n; }
int RoomCost(const Chart& c, int r) { return c.rooms[r].type == RoomType::Fight ? 1 : 0; }

// the route with the fewest fights (room fights plus hallway fights), by Dijkstra
bool gAvoidBlocked = false;
std::vector<int> Dijkstra(const Chart& c, int from, int to, int* costOut) {
    int n = (int)c.rooms.size();
    std::vector<int> dist(n, 1 << 29), prev(n, -1);
    using P = std::pair<int, int>;
    std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
    dist[from] = 0; pq.push({0, from});
    while (!pq.empty()) {
        auto [d, r] = pq.top(); pq.pop();
        if (d > dist[r]) continue;
        for (int e = 0; e < (int)c.edges.size(); e++) {
            if (c.edges[e].a != r && c.edges[e].b != r) continue;
            int o = c.Other(e, r), nd = d + HallFights(c.edges[e]) + (o == to ? 0 : RoomCost(c, o));
            if (gAvoidBlocked) for (auto s : c.edges[e].segs) if (s == CorridorEvent::Blocked) nd += 100;
            if (nd < dist[o]) { dist[o] = nd; prev[o] = r; pq.push({nd, o}); }
        }
    }
    if (costOut) *costOut = dist[to];
    std::vector<int> path;
    if (dist[to] >= (1 << 29)) return path;
    for (int r = to; r >= 0; r = prev[r]) path.push_back(r);
    std::reverse(path.begin(), path.end());
    return path;
}

bool TryGen(Chart& c, int tier, Rnd& R, std::string& why) {
    const ChartParams& P = ChartParamsFor(tier);
    c = Chart{};
    int W = std::max(4, (P.rooms + 3) / 2), H = P.rooms >= 10 ? 4 : 3;
    std::vector<int> cell(W * H, -1);
    auto addRoom = [&](int x, int y) { ChartRoom r; r.type = RoomType::Fight; r.gx = x; r.gy = y; c.rooms.push_back(r); cell[y * W + x] = (int)c.rooms.size() - 1; return (int)c.rooms.size() - 1; };
    c.entrance = addRoom(0, R.I(0, H - 1));
    // 1. grow a spanning tree across the grid, favouring rooms further from the entrance so the chart reaches out
    int guard = 0;
    while ((int)c.rooms.size() < P.rooms && guard++ < 2000) {
        int from = R.I(0, (int)c.rooms.size() - 1);
        if (R.F() < 0.55f) for (int k = 0; k < 3; k++) { int o = R.I(0, (int)c.rooms.size() - 1); if (c.rooms[o].gx > c.rooms[from].gx) from = o; }
        const int DX[4] = {1, 0, 0, -1}, DY[4] = {0, 1, -1, 0};
        int dir = R.I(0, 3);
        if (R.F() < 0.35f) dir = 0;
        int nx = c.rooms[from].gx + DX[dir], ny = c.rooms[from].gy + DY[dir];
        if (nx < 0 || ny < 0 || nx >= W || ny >= H || cell[ny * W + nx] >= 0) continue;
        int nr = addRoom(nx, ny);
        c.edges.push_back({from, nr, {}, 0, false});
    }
    if ((int)c.rooms.size() < P.rooms) { why = "the grid filled up"; return false; }
    // the boss: the room furthest from the entrance
    auto d0 = Hops(c, c.entrance);
    c.boss = c.entrance;
    for (int r = 0; r < (int)c.rooms.size(); r++)
        if (d0[r] > d0[c.boss] || (d0[r] == d0[c.boss] && c.rooms[r].gx > c.rooms[c.boss].gx)) c.boss = r;
    // 2. loops between grid neighbours that aren't yet joined
    std::vector<std::pair<int, int>> cand;
    for (int a = 0; a < (int)c.rooms.size(); a++)
        for (int b = a + 1; b < (int)c.rooms.size(); b++)
            if (abs(c.rooms[a].gx - c.rooms[b].gx) + abs(c.rooms[a].gy - c.rooms[b].gy) == 1 && c.EdgeBetween(a, b) < 0) cand.push_back({a, b});
    for (int i = (int)cand.size() - 1; i > 0; i--) std::swap(cand[i], cand[R.I(0, i)]);
    // prefer loops that give the boss a second way in
    std::stable_sort(cand.begin(), cand.end(), [&](auto& x, auto& y) {
        auto near = [&](const std::pair<int, int>& p) { return std::min(abs(d0[p.first] - d0[c.boss]), abs(d0[p.second] - d0[c.boss])); };
        return near(x) < near(y);
    });
    if ((int)cand.size() < P.loops) { why = "no room for the loops"; return false; }
    for (int i = 0; i < P.loops; i++) c.edges.push_back({cand[i].first, cand[i].second, {}, 0, true});
    // at least two distinct routes to the boss: some edge of the shortest route can go and the boss is still reachable
    {
        std::vector<int> path = Dijkstra(c, c.entrance, c.boss, nullptr);
        bool alt = false;
        for (size_t i = 1; i < path.size() && !alt; i++) alt = Hops(c, c.entrance, c.EdgeBetween(path[i - 1], path[i]))[c.boss] >= 0;
        if (!alt) { why = "only one route to the boss"; return false; }
    }
    // 3. room types: the shortest route (by hops) holds the fights; the rest hold loot, curios, rest and shrines
    for (auto& r : c.rooms) r.type = RoomType::Treasure;
    c.rooms[c.entrance].type = RoomType::Entrance;
    c.rooms[c.boss].type = RoomType::Boss;
    std::vector<int> route;
    {
        auto dh = Hops(c, c.entrance);
        int r = c.boss;
        route.push_back(r);
        while (r != c.entrance) { for (int o : c.Neighbours(r)) if (dh[o] == dh[r] - 1) { r = o; break; } route.push_back(r); }
    }
    std::vector<bool> onRoute(c.rooms.size(), false);
    {   // exactly the tier's fights along the way, spread out; any other room on it is a quiet junction
        std::vector<int> inner;
        for (int r : route) { onRoute[r] = true; if (r != c.entrance && r != c.boss) { inner.push_back(r); c.rooms[r].type = RoomType::Empty; } }
        int nf = std::min((int)inner.size(), P.fights);
        for (int k = 0; k < nf; k++) c.rooms[inner[(k * (int)inner.size()) / std::max(1, nf) + ((int)inner.size() / std::max(1, nf)) / 2 < (int)inner.size() ? (k * (int)inner.size()) / std::max(1, nf) : k]].type = RoomType::Fight;
    }
    std::vector<int> off;
    for (int r = 0; r < (int)c.rooms.size(); r++) if (!onRoute[r]) off.push_back(r);
    // 4. rest rooms: about halfway, never beside the boss
    int D = d0[c.boss];
    std::sort(off.begin(), off.end(), [&](int a, int b) { return abs(d0[a] * 2 - D) < abs(d0[b] * 2 - D); });
    int rests = 0;
    for (size_t i = 0; i < off.size() && rests < P.rests; i++) {
        int r = off[i];
        if (c.EdgeBetween(r, c.boss) >= 0) continue;
        c.rooms[r].type = RoomType::Rest; rests++;
    }
    if (rests < P.rests) { why = "nowhere to rest"; return false; }
    std::vector<int> left;
    for (int r : off) if (c.rooms[r].type == RoomType::Treasure) left.push_back(r);
    for (int i = (int)left.size() - 1; i > 0; i--) std::swap(left[i], left[R.I(0, i)]);
    int curioRooms = std::min((int)left.size(), (P.curios + 1) / 2), k = 0;
    for (; k < curioRooms; k++) c.rooms[left[k]].type = RoomType::Curio;
    if (tier >= 1 && (int)left.size() - k >= 2) c.rooms[left[k++]].type = RoomType::Shrine;
    int treasures = (int)left.size() - k;
    for (; k < (int)left.size(); k++) if (treasures > 2 && R.F() < 0.3f) { c.rooms[left[k]].type = RoomType::Fight; treasures--; } // a fight guarding the loot
    // corridor segments and their events
    for (auto& e : c.edges) {
        int n = R.I(P.segMin, P.segMax);
        e.segs.assign(n, CorridorEvent::None);
        bool fromEntrance = e.a == c.entrance || e.b == c.entrance;
        for (int s = 0; s < n; s++) {
            float x = R.F();
            if (x < CHART_HALLFIGHT && !(fromEntrance && s == 0)) e.segs[s] = CorridorEvent::HallFight;
            else if (x < CHART_HALLFIGHT + CHART_TRAP) e.segs[s] = CorridorEvent::Trap;
            else if (x < CHART_HALLFIGHT + CHART_TRAP + CHART_LOOT) e.segs[s] = CorridorEvent::Loot;
        }
        if (e.loop && R.F() < 0.3f) e.segs[R.I(0, n - 1)] = CorridorEvent::Blocked; // a blocked passage only ever on a loop: there's always a way round
    }
    int corridorCurios = P.curios - curioRooms;
    for (int tries = 0; corridorCurios > 0 && tries < 200; tries++) {
        auto& e = c.edges[R.I(0, (int)c.edges.size() - 1)];
        int s = R.I(0, (int)e.segs.size() - 1);
        if (e.segs[s] == CorridorEvent::None) { e.segs[s] = CorridorEvent::Curio; corridorCurios--; }
    }
    // 5. enough fights on every route to the boss: top up the easiest route with hallway fights
    for (int it = 0; it < 12; it++) {
        int cost = 0;
        std::vector<int> easy = Dijkstra(c, c.entrance, c.boss, &cost);
        if (easy.size() < 2) { why = "the boss can't be reached"; return false; }
        c.minFights = cost;
        if (cost >= P.fights) break;
        bool added = false;
        for (size_t i = easy.size() - 1; i >= 1 && !added; i--) {
            auto& e = c.edges[c.EdgeBetween(easy[i - 1], easy[i])];
            for (auto& s : e.segs) if (s == CorridorEvent::None || s == CorridorEvent::Loot) { s = CorridorEvent::HallFight; added = true; break; }
        }
        if (!added) { why = "can't make the boss far enough"; return false; }
    }
    Dijkstra(c, c.entrance, c.boss, &c.minFights);
    // what the crew know at the start: the entrance, and the rooms one step from it
    c.rooms[c.entrance].known = c.rooms[c.entrance].scouted = c.rooms[c.entrance].visited = c.rooms[c.entrance].cleared = true;
    for (int o : c.Neighbours(c.entrance)) c.rooms[o].known = c.rooms[o].scouted = true;
    return CheckChart(c, tier, why);
}
}  // namespace

int Chart::EdgeBetween(int r1, int r2) const {
    for (int e = 0; e < (int)edges.size(); e++) if ((edges[e].a == r1 && edges[e].b == r2) || (edges[e].a == r2 && edges[e].b == r1)) return e;
    return -1;
}
std::vector<int> Chart::Neighbours(int r) const {
    std::vector<int> n;
    for (int e = 0; e < (int)edges.size(); e++) if (edges[e].a == r || edges[e].b == r) n.push_back(Other(e, r));
    return n;
}

int ChartFightsTo(const Chart& c, int from, int to) { int cost = 0; Dijkstra(c, from, to, &cost); return cost; }
std::vector<int> ChartPath(const Chart& c, int from, int to, bool avoidBlocked) { gAvoidBlocked = avoidBlocked; auto p = Dijkstra(c, from, to, nullptr); gAvoidBlocked = false; return p; }

bool CheckChart(const Chart& c, int tier, std::string& why) {
    const ChartParams& P = ChartParamsFor(tier);
    if ((int)c.rooms.size() != P.rooms) { why = "wrong number of rooms"; return false; }
    auto d = Hops(c, c.entrance);
    for (int r = 0; r < (int)c.rooms.size(); r++) {
        if (d[r] < 0) { why = "a room can't be reached"; return false; }
        if (d[r] > d[c.boss]) { why = "the boss isn't the furthest room"; return false; }
    }
    if (c.rooms[c.entrance].gx != 0) { why = "the entrance isn't on the edge"; return false; }
    int rests = 0, deadEnds = 0;
    for (int r = 0; r < (int)c.rooms.size(); r++) {
        if (c.rooms[r].type == RoomType::Rest) { rests++; if (c.EdgeBetween(r, c.boss) >= 0) { why = "a rest room beside the boss"; return false; } }
        if (r != c.entrance && r != c.boss && c.Neighbours(r).size() == 1) deadEnds++;
    }
    if (rests != P.rests) { why = "wrong number of rest rooms"; return false; }
    if (deadEnds > 2) { why = "more than two dead ends"; return false; }
    if (ChartFightsTo(c, c.entrance, c.boss) < P.fights) { why = "the boss can be reached in too few fights"; return false; }
    // loot always costs a detour: no treasure or curio on a shortest route
    std::vector<int> shortest = ChartPath(c, c.entrance, c.boss);
    auto dh = Hops(c, c.entrance);
    for (int r = 0; r < (int)c.rooms.size(); r++)
        if ((c.rooms[r].type == RoomType::Treasure || c.rooms[r].type == RoomType::Curio) && dh[r] >= 0) {
            auto db = Hops(c, r);
            if (dh[r] + db[c.boss] == dh[c.boss]) { why = "loot sits on a shortest route"; return false; }
        }
    // two routes to the boss
    bool alt = false;
    for (size_t i = 1; i < shortest.size() && !alt; i++) alt = Hops(c, c.entrance, c.EdgeBetween(shortest[i - 1], shortest[i]))[c.boss] >= 0;
    if (!alt) { why = "only one route to the boss"; return false; }
    return true;
}

Chart GenerateChart(int tier, unsigned seed, std::string* why) {
    Rnd R{seed ? seed * 2654435761u + 1 : 1u};
    Chart c;
    std::string w;
    for (int attempt = 0; attempt < 400; attempt++) {
        if (TryGen(c, tier, R, w)) return c;
        if (why) *why += w + "; ";
    }
    return c; // (never reached in practice: --gen-chart checks thousands of seeds)
}

std::string ChartAscii(const Chart& c) {
    int W = 0, H = 0;
    for (auto& r : c.rooms) { W = std::max(W, r.gx + 1); H = std::max(H, r.gy + 1); }
    std::vector<std::string> lines(H * 2, std::string(W * 4, ' '));
    const char* GLYPH = "FTBECRSo"; // Fight, Treasure, Boss, Entrance, Curio, Rest, Shrine, a quiet junction
    for (int i = 0; i < (int)c.rooms.size(); i++) lines[c.rooms[i].gy * 2][c.rooms[i].gx * 4] = GLYPH[(int)c.rooms[i].type];
    for (auto& e : c.edges) {
        const ChartRoom &a = c.rooms[e.a], &b = c.rooms[e.b];
        if (a.gy == b.gy) { int x = std::min(a.gx, b.gx) * 4; for (int k = 1; k < 4; k++) lines[a.gy * 2][x + k] = e.loop ? '=' : '-'; }
        else { int y = std::min(a.gy, b.gy) * 2 + 1; lines[y][a.gx * 4] = e.loop ? '"' : '|'; }
    }
    std::string out;
    for (auto& l : lines) out += l + "\n";
    const char* EV = ".htlcx"; // none, hallway fight, trap, loot, curio, blocked
    for (auto& e : c.edges) {
        out += "  " + std::to_string(e.a) + "-" + std::to_string(e.b) + (e.loop ? " (loop) " : " ") + "[";
        for (auto s : e.segs) out += EV[(int)s];
        out += "]\n";
    }
    return out;
}
