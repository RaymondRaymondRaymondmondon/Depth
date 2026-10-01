// The Trawl's wrecks (see trawl_wreck.h): the generator, the doc's checks, and depth.exe --trawl-wreck.
#include "trawl_wreck.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>

namespace tw {

namespace {
struct Rng { uint32_t s; float F() { s = s * 1664525u + 1013904223u; return (s >> 8) * (1.0f / 16777216.0f); } int I(int lo, int hi) { return lo + (int)(F() * (hi - lo + 1)) % (hi - lo + 1); } };
// the doc's table (page 56): grounds, depth, rooms, salvage budget, locked cabins
struct TypeDef { const char* name; float d0, d1; int r0, r1; float b0, b1; int locked; int relics0, relics1; bool bell; int len, decks; };
const TypeDef TD[(int)WreckType::COUNT] = {
    {"sloop",          8,  20,  3,  5,   80,  200, 0, 0, 0, false, 5, 1},
    {"whaler",        15,  40,  6,  9,  200,  450, 1, 0, 0, false, 6, 2},
    {"smuggler hull", 20,  60,  8, 12,  300,  700, 2, 0, 0, false, 7, 2},
    {"galleon",       60, 110, 12, 18,  600, 1400, 3, 0, 0, true,  9, 3},
    {"Atlantean terrace", 40, 120, 10, 16, 300, 800, 1, 1, 3, true, 8, 2},
};
const float HOSE = 40, CELL_W = 4, CELL_H = 3;
struct ItemDef { const char* name; float v0, v1, kg; };
const ItemDef COMMON[] = {{"brass fittings", 10, 40, 6}, {"a bottle of old rum", 15, 30, 2}, {"scrimshaw", 40, 80, 1}, {"a ship's bell", 60, 90, 30}, {"a chronometer", 80, 150, 4}};
}

const char* WreckTypeName(WreckType t) { return TD[(int)t].name; }
float Wreck::Budget() const { float s = 0; for (const auto& i : salvage) s += i.value; return s; }
float Wreck::LockedShare() const { float s = 0, l = 0; for (const auto& i : salvage) { s += i.value; if (i.room >= 0 && rooms[i.room].locked) l += i.value; } return s > 0 ? l / s : 0; }

std::vector<float> HoseDistances(const Wreck& w) {
    int n = (int)w.rooms.size();
    std::vector<float> d(n, 1e9f);
    using Q = std::pair<float, int>;
    std::priority_queue<Q, std::vector<Q>, std::greater<Q>> q;
    auto centre = [&](int r) { const auto& R = w.rooms[r]; return std::pair<float, float>{(R.x + R.w * 0.5f) * CELL_W, (R.y + 0.5f) * CELL_H}; };
    for (int e : w.entries) { d[e] = CELL_H; q.push({d[e], e}); }   // (down from the breach into the room)
    while (!q.empty()) {
        auto [dd, r] = q.top(); q.pop();
        if (dd > d[r]) continue;
        for (const auto& L : w.links) {
            int o = L.a == r ? L.b : L.b == r ? L.a : -1;
            if (o < 0) continue;
            auto a = centre(r), b = centre(o);
            float step = fabsf(a.first - b.first) + fabsf(a.second - b.second);
            if (dd + step < d[o]) { d[o] = dd + step; q.push({d[o], o}); }
        }
    }
    return d;
}

Wreck GenerateWreck(WreckType t, uint32_t seed, const std::string& ground) {
    const TypeDef& T = TD[(int)t];
    Rng R{seed * 2654435761u + 977};
    Wreck w; w.type = t; w.seed = seed; w.bell = T.bell;
    w.depth = T.d0 + R.F() * (T.d1 - T.d0);
    w.onSide = R.F() < 0.3f;
    int want = R.I(T.r0, T.r1);
    // the hull: decks of `len` cells, the lower decks a cell shorter at each end (the hull's curve); rooms 1-3 cells wide
    int decks = T.decks + (want > T.len * T.decks ? 1 : 0);
    w.gw = T.len + (want > T.len * 2 ? 2 : 0); w.gh = decks;
    for (int y = 0; y < decks && (int)w.rooms.size() < want; y++) {
        int x0 = y == 0 ? 0 : std::min(y, 1), x1 = w.gw - (y == 0 ? 0 : std::min(y, 1));
        for (int x = x0; x < x1 && (int)w.rooms.size() < want;) {
            int left = want - (int)w.rooms.size();
            int roomW = std::min(x1 - x, std::clamp(1 + (int)(R.F() * 3), 1, 3));
            // (keep enough cells for the rooms still wanted)
            int cellsLeftAfter = 0; for (int yy = y; yy < decks; yy++) { int a = yy == 0 ? 0 : std::min(yy, 1), b = w.gw - (yy == 0 ? 0 : std::min(yy, 1)); cellsLeftAfter += b - a; }
            cellsLeftAfter -= (x - x0) + roomW;
            if (cellsLeftAfter < left - 1) roomW = 1;
            WreckRoom r; r.x = x; r.y = y; r.w = roomW;
            r.kind = t == WreckType::Terrace ? (roomW >= 2 ? "hall" : "chamber") : y == 0 ? (x == x0 ? "captain's cabin" : roomW >= 2 ? "deckhouse" : "cabin") : y == decks - 1 ? "hold" : (roomW >= 2 ? "berth deck" : "store");
            w.rooms.push_back(r);
            x += roomW;
        }
    }
    int n = (int)w.rooms.size();
    // doors along each deck, hatches between decks (each lower room joins one above it), breaches on the top deck
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) {
        const auto& a = w.rooms[i]; const auto& b = w.rooms[j];
        if (a.y == b.y && (a.x + a.w == b.x || b.x + b.w == a.x)) w.links.push_back({i, j, 0});
    }
    for (int j = 0; j < n; j++) {
        if (w.rooms[j].y == 0) continue;
        int best = -1;
        for (int i = 0; i < n; i++) if (w.rooms[i].y == w.rooms[j].y - 1 && w.rooms[i].x < w.rooms[j].x + w.rooms[j].w && w.rooms[j].x < w.rooms[i].x + w.rooms[i].w && (best < 0 || R.F() < 0.5f)) best = i;
        if (best >= 0) w.links.push_back({best, j, 1});
    }
    // the first breach: a top-deck room near the middle (where the hull broke)
    { int best = 0; float bd = 1e9f; for (int i = 0; i < n; i++) if (w.rooms[i].y == 0) { float d = fabsf(w.rooms[i].x + w.rooms[i].w * 0.5f - w.gw * 0.5f) + R.F(); if (d < bd) { bd = d; best = i; } } w.entries.push_back(best); }
    // more breaches until every room is within the hose (the hull torn further; a bell wreck's bell sits at one breach)
    for (int guard = 0; guard < n; guard++) {
        auto d = HoseDistances(w);
        int far = -1; float fd = HOSE;
        for (int i = 0; i < n; i++) if (d[i] > fd) { fd = d[i]; far = i; }
        if (far < 0) break;
        // tear the hull at the room nearest the far one that has the outer hull above or beside it
        int pick = far;
        for (int i = 0; i < n; i++) if (w.rooms[i].y == 0 && fabsf((float)w.rooms[i].x - w.rooms[far].x) <= 1) { pick = i; break; }
        if (std::find(w.entries.begin(), w.entries.end(), pick) != w.entries.end()) pick = far;
        w.entries.push_back(pick);
    }
    // salvage: the type's budget, richer and heavier deeper in; locked cabins hold 40% of the value
    float budget = T.b0 + R.F() * (T.b1 - T.b0);
    auto d = HoseDistances(w);
    std::vector<int> order(n); for (int i = 0; i < n; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return d[a] > d[b]; });   // deepest first
    int locks = std::min(T.locked, std::max(0, n - 1));
    for (int k = 0, placed = 0; k < n && placed < locks; k++) { int r = order[k]; if (std::find(w.entries.begin(), w.entries.end(), r) != w.entries.end()) continue; w.rooms[r].locked = true; w.rooms[r].kind = t == WreckType::Terrace ? "sealed vault" : t == WreckType::Galleon && placed == 0 ? "strongroom" : "locked cabin"; placed++; }
    std::vector<int> lockedRooms, openRooms;
    for (int i = 0; i < n; i++) (w.rooms[i].locked ? lockedRooms : openRooms).push_back(i);
    float lockedBudget = lockedRooms.empty() ? 0 : budget * 0.4f, openBudget = budget - lockedBudget;
    auto fill = [&](const std::vector<int>& roomsIn, float amount, bool locked) {
        if (roomsIn.empty()) return;
        std::vector<int> rs = roomsIn; std::sort(rs.begin(), rs.end(), [&](int a, int b) { return d[a] > d[b]; });
        float left = amount; int k = 0;
        while (left > 5) {
            int room = rs[(k++) % rs.size()];
            SalvageItem it;
            if (locked && k == 1) { it.name = t == WreckType::Terrace ? "a sealed casket" : "a sealed strongbox"; it.value = std::min(left, 100 + R.F() * 150); it.kg = 22; it.twoDiver = true; }
            else { const ItemDef& c = COMMON[R.I(0, 4)]; it.name = c.name; it.value = std::min(left, c.v0 + R.F() * (c.v1 - c.v0)); it.kg = c.kg; it.twoDiver = c.kg >= 30; }
            it.room = room; left -= it.value; w.salvage.push_back(it);
            if (w.salvage.size() > 40) break;
        }
    };
    fill(lockedRooms, lockedBudget, true); fill(openRooms, openBudget, false);
    // relics (the terrace's 1-3, and a rare one anywhere else: 150-600 each, on top of the budget), Atlantean idols
    int relics = T.relics1 > 0 ? R.I(T.relics0, T.relics1) : (R.F() < 0.15f ? 1 : 0);
    for (int i = 0; i < relics; i++) { SalvageItem it; it.name = "a relic"; it.relic = true; it.value = 150 + R.F() * 450; it.kg = 5; it.room = order[i % std::max(1, n)]; w.salvage.push_back(it); }
    if (ground == "atlantis") for (int i = 0; i < n; i++) if (w.rooms[i].kind.find("cabin") != std::string::npos && R.F() < 0.1f) { SalvageItem it; it.name = "an Atlantean idol"; it.cursed = true; it.value = 120 + R.F() * 80; it.kg = 8; it.room = i; w.salvage.push_back(it); }
    // air pockets: one per five rooms, the farthest rooms first
    int pockets = std::max(1, (n + 4) / 5);
    for (int k = 0, placed = 0; k < n && placed < pockets; k++) { int r = order[k]; if (!w.rooms[r].air) { w.rooms[r].air = true; placed++; k += 1; } }
    // a squeeze passage (~30%): a shortcut from a near room to the richest room
    if (R.F() < 0.3f && n >= 4) {
        float best = -1; int rich = -1; for (int i = 0; i < n; i++) { float v = 0; for (const auto& s : w.salvage) if (s.room == i) v += s.value; if (v > best) { best = v; rich = i; } }
        int near = w.entries[0];
        if (rich >= 0 && rich != near) w.links.push_back({near, rich, 2});
    }
    // residents from the ground's roster: morays, octopus, isopods, congers, a Ghost Worm hatchling (the Grotto,
    // Atlantis), a Drowned sailor per four rooms (the Grotto, Atlantis)
    std::vector<std::string> roster = ground == "grotto" ? std::vector<std::string>{"white conger", "isopods", "albino octopus"}
                                    : ground == "atlantis" ? std::vector<std::string>{"frill shark", "isopods", "jumbo squid"}
                                    : std::vector<std::string>{"moray eel", "reef octopus", "isopods"};
    for (int i = 0; i < n; i += 2) w.residents.push_back({roster[R.I(0, (int)roster.size() - 1)], order[i], false});
    if (ground == "grotto" || ground == "atlantis") {
        if (R.F() < 0.5f) w.residents.push_back({"a Ghost Worm hatchling", order[0], false});
        for (int i = 0; i < n / 4; i++) w.residents.push_back({"a Drowned sailor", order[(i * 3 + 1) % n], false});
    }
    return w;
}

bool CheckWreck(const Wreck& w, std::string* why) {
    auto no = [&](const std::string& m) { if (why) *why = m; return false; };
    const TypeDef& T = TD[(int)w.type];
    int n = w.Rooms();
    if (n < T.r0 || n > T.r1) return no("room count out of the type's range");
    auto d = HoseDistances(w);
    for (int i = 0; i < n; i++) if (d[i] > HOSE + 0.01f) { char b[96]; snprintf(b, sizeof b, "room %d is %.0f m of hose from any entry", i, d[i]); return no(b); }
    int air = 0, locked = 0; for (const auto& r : w.rooms) { air += r.air; locked += r.locked; }
    if (air < (n + 4) / 5) return no("too few air pockets");
    if (locked != std::min(T.locked, std::max(0, n - 1))) return no("wrong number of locked cabins");
    float budget = 0; for (const auto& s : w.salvage) if (!s.relic && s.name != "an Atlantean idol") budget += s.value;
    if (budget < T.b0 * 0.9f || budget > T.b1 * 1.05f) { char b[96]; snprintf(b, sizeof b, "salvage budget %.0f outside %.0f-%.0f", budget, T.b0, T.b1); return no(b); }
    if (locked > 0) { float ls = w.LockedShare(); if (ls < 0.25f || ls > 0.55f) { char b[64]; snprintf(b, sizeof b, "locked share %.0f%%", ls * 100); return no(b); } }
    return true;
}

std::string WreckAscii(const Wreck& w) {
    // each cell as 4 characters: the room's index, L locked, A air, E entry; '=' hull, '.' empty
    std::string out;
    char head[160]; snprintf(head, sizeof head, "%s, %.0f m%s%s, %d rooms, %d entries, salvage %.0f (%.0f%% locked)\n", WreckTypeName(w.type), w.depth, w.onSide ? ", on her side" : "", w.bell ? " (bell)" : "", w.Rooms(), (int)w.entries.size(), w.Budget(), w.LockedShare() * 100);
    out += head;
    for (int y = 0; y < w.gh; y++) {
        std::string line;
        for (int x = 0; x < w.gw; x++) {
            int r = -1; for (int i = 0; i < w.Rooms(); i++) if (w.rooms[i].y == y && x >= w.rooms[i].x && x < w.rooms[i].x + w.rooms[i].w) r = i;
            if (r < 0) { line += "    "; continue; }
            const auto& R = w.rooms[r];
            bool entry = std::find(w.entries.begin(), w.entries.end(), r) != w.entries.end();
            char c[8]; snprintf(c, sizeof c, "%c%02d%c", x == R.x ? '|' : ' ', r, R.locked ? 'L' : R.air ? 'A' : entry ? 'E' : ' ');
            line += c;
        }
        out += line + "|\n";
    }
    auto d = HoseDistances(w);
    for (int i = 0; i < w.Rooms(); i++) {
        char b[160]; float v = 0; int items = 0; for (const auto& s : w.salvage) if (s.room == i) { v += s.value; items++; }
        snprintf(b, sizeof b, "  %02d %-16s hose %2.0f m  %d items %4.0f%s%s\n", i, w.rooms[i].kind.c_str(), d[i], items, v, w.rooms[i].locked ? "  locked" : "", w.rooms[i].air ? "  air pocket" : "");
        out += b;
    }
    for (const auto& L : w.links) if (L.kind == 2) { char b[64]; snprintf(b, sizeof b, "  squeeze: %d -> %d\n", L.a, L.b); out += b; }
    for (const auto& r : w.residents) { char b[96]; snprintf(b, sizeof b, "  resident: %s in %d\n", r.what.c_str(), r.room); out += b; }
    return out;
}

std::vector<Wreck> GroundWrecks(const std::string& ground, uint32_t seed) {
    Rng R{seed * 40503u + 11};
    std::vector<WreckType> types;
    if (ground == "weeds") { types = {WreckType::Sloop, WreckType::Whaler}; }
    else if (ground == "grotto") { int n = R.I(3, 4); for (int i = 0; i < n; i++) types.push_back(i % 2 ? WreckType::Smuggler : WreckType::Whaler); }
    else if (ground == "atlantis") { int n = R.I(2, 3); for (int i = 0; i < n; i++) types.push_back(WreckType::Galleon); types.push_back(WreckType::Terrace); }
    else { int n = R.I(1, 2); for (int i = 0; i < n; i++) types.push_back(WreckType::Sloop); }
    std::vector<Wreck> out;
    for (size_t i = 0; i < types.size(); i++) {
        // (redraw a layout that fails the doc's checks: it doesn't happen often)
        for (uint32_t k = 0; k < 20; k++) { Wreck w = GenerateWreck(types[i], seed * 131u + (uint32_t)i * 7919u + k * 104729u, ground); if (CheckWreck(w, nullptr) || k == 19) { out.push_back(w); break; } }
    }
    return out;
}

int RunTrawlWreck(int argc, char** argv) {
    uint32_t seed = argc >= 3 ? (uint32_t)strtoul(argv[2], nullptr, 10) : 1;
    std::string which = argc >= 4 ? argv[3] : "all";
    int count = argc >= 5 ? atoi(argv[4]) : 1;
    int fails = 0, made = 0, redraws = 0;
    for (int t = 0; t < (int)WreckType::COUNT; t++) {
        if (which != "all" && atoi(which.c_str()) != t) continue;
        for (int i = 0; i < count; i++) {
            Wreck w = GenerateWreck((WreckType)t, seed + i, t >= 3 ? "atlantis" : t == 2 ? "grotto" : "weeds");
            std::string why; bool ok = CheckWreck(w, &why);
            made++; if (!ok) redraws++;
            if (count == 1) { printf("%s", WreckAscii(w).c_str()); printf("  %s\n\n", ok ? "passes the doc's rules" : ("FAILS: " + why).c_str()); }
        }
    }
    // the grounds' sets (with redraws) must all pass
    for (const char* gname : {"lagoon", "weeds", "grotto", "atlantis"}) for (int i = 0; i < std::max(1, count); i++) {
        auto ws = GroundWrecks(gname, seed + i);
        for (const auto& w : ws) { std::string why; if (!CheckWreck(w, &why)) { fails++; printf("FAIL  %s seed %u: %s: %s\n", gname, seed + i, WreckTypeName(w.type), why.c_str()); } }
    }
    printf("wrecks: %d generated, %d failed a first draw; ground sets: %s\n", made, redraws, fails ? "FAILED" : "all pass the doc's rules");
    return fails ? 1 : 0;
}

} // namespace tw
