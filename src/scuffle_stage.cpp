// Scuffle stage 3: stages as text and as share codes, the reachability check, and the built-in packs (doc p. 17).
// Headless.
//
// The text form (one char a tile, the top row first): '#' stone (steel aboard the Nautilus), 'w' wood, 'i' ice,
// 'g' glass, '-' rope (one-way), 'e' an electrified rail, '<' '>' conveyors, 'S' a spawn, 'C' a crate zone (its
// column), and the pieces, each a rectangle of one letter: 'P' a piston, 'L' an elevator, 'V' a steam vent (its column
// rises above it), 'F' the propeller, 'T' a torpedo tube's mouth, 'W' a window (glass that breaks at a set second).
// Lines after the rows: "name=", "world=", "wrap=1", "author=", and "@<n> key=value ..." to set the nth piece's
// parameters (dx dy travel period phase on power).
//
// A share code is the stage packed into bytes, compressed and base64'd: "SCF1-..." (a pack of up to 20: "SCP1-...").
#include "scuffle.h"
#include "net_msg.h"
#include "redtide.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <fstream>
#include <queue>
#include <set>
#include <sstream>

namespace sf {

const char* PieceName(int k) { static const char* N[PK_COUNT] = {"piston", "elevator", "steam vent", "propeller", "torpedo tube", "window"}; return k >= 0 && k < PK_COUNT ? N[k] : "?"; }
const char* TileName(int t) { static const char* N[T_COUNT] = {"empty", "stone", "wood", "ice", "glass", "rope", "electrified rail", "conveyor (left)", "conveyor (right)"}; return t >= 0 && t < T_COUNT ? N[t] : "?"; }
const char* WorldName(int w) { static const char* N[WD_COUNT] = {"The Nautilus", "The Cave", "The Reef", "Atlantis", "The Void", "The Salon"}; return w >= 0 && w < WD_COUNT ? N[w] : "?"; }
static const char* WORLD_KEY[WD_COUNT] = {"nautilus", "cave", "reef", "atlantis", "void", "salon"};
static int PieceLetter(char c) { switch (c) { case 'P': return PK_PISTON; case 'L': return PK_ELEVATOR; case 'V': return PK_VENT; case 'F': return PK_PROPELLER; case 'T': return PK_TUBE; case 'W': return PK_WINDOW; default: return -1; } }
static char LetterOf(int kind) { static const char L[PK_COUNT] = {'P', 'L', 'V', 'F', 'T', 'W'}; return kind >= 0 && kind < PK_COUNT ? L[kind] : '?'; }
void PieceDefaults(Piece& p);
static void Defaults(Piece& p) { PieceDefaults(p); }
void PieceDefaults(Piece& p) {
    switch (p.kind) {
        case PK_PISTON: p.dx = 0; p.dy = -1; p.travel = 3; p.period = 2.5f; p.on = 0.8f; break;
        case PK_ELEVATOR: p.dx = 0; p.dy = 1; p.travel = 4; p.period = 6; break;
        case PK_VENT: p.dx = 0; p.dy = 1; p.travel = 5; p.period = 3; p.on = 0.9f; p.power = 1; break;
        case PK_PROPELLER: p.power = 1; break;
        case PK_TUBE: p.dx = 1; p.dy = 0; p.power = 18; p.period = 1.0f; break;
        case PK_WINDOW: p.phase = 20; break;
    }
}

// ---------------------------------------------------------------- the text form
Stage StageFromText(const std::vector<std::string>& lines, const char* name) {
    Stage s; s.name = name ? name : "Untitled";
    std::vector<std::string> rows, meta;
    for (const auto& l : lines) { if (!l.empty() && (l[0] == '@' || l.find('=') != std::string::npos)) meta.push_back(l); else rows.push_back(l); }
    s.h = (int)rows.size(); s.w = 0; for (const auto& r : rows) s.w = std::max(s.w, (int)r.size());
    s.t.assign(std::max(1, s.w * s.h), T_EMPTY);
    std::vector<int> cell(s.w * s.h, 0);   // (piece letters, for grouping)
    std::set<int> crates;
    for (int ry = 0; ry < s.h; ry++) {
        int y = s.h - 1 - ry;
        for (int x = 0; x < (int)rows[ry].size(); x++) {
            char c = rows[ry][x]; uint8_t k = T_EMPTY;
            switch (c) { case '#': k = T_STONE; break; case 'w': k = T_WOOD; break; case 'i': k = T_ICE; break; case 'g': k = T_GLASS; break; case '-': k = T_ROPE; break;
                         case 'e': k = T_RAIL; break; case '<': k = T_CONV_L; break; case '>': k = T_CONV_R; break; case 'V': k = T_STONE; break; case 'W': k = T_GLASS; break; }
            s.t[y * s.w + x] = k;
            if (c == 'S') s.spawns.push_back({(x + 0.5f) * TILE, y * TILE});
            if (c == 'C') crates.insert(x);
            if (PieceLetter(c) >= 0) cell[y * s.w + x] = c;
        }
    }
    // each piece: a connected rectangle of its letter (in reading order: top row first, then left to right)
    std::vector<char> seen(s.w * s.h, 0);
    for (int ry = 0; ry < s.h; ry++) for (int x = 0; x < s.w; x++) {
        int y = s.h - 1 - ry; char c = (char)cell[y * s.w + x];
        if (!c || seen[y * s.w + x]) continue;
        int x1 = x; while (x1 + 1 < s.w && cell[y * s.w + x1 + 1] == c) x1++;
        int y0 = y; while (y0 - 1 >= 0 && cell[(y0 - 1) * s.w + x] == c) y0--;
        for (int yy = y0; yy <= y; yy++) for (int xx = x; xx <= x1; xx++) seen[yy * s.w + xx] = 1;
        Piece p; p.kind = (uint8_t)PieceLetter(c); p.x = x; p.y = y0; p.w = x1 - x + 1; p.h = y - y0 + 1; Defaults(p);
        if (p.kind == PK_TUBE) p.dx = s.Solid(x - 1, y0) ? 1 : s.Solid(x1 + 1, y0) ? -1 : 1;   // (the mouth faces away from the wall)
        s.pieces.push_back(p);
    }
    s.crateCols.assign(crates.begin(), crates.end());
    for (const auto& m : meta) {
        std::istringstream in(m); std::string tok;
        if (m[0] == '@') {
            in >> tok; int n = atoi(tok.c_str() + 1) - 1; if (n < 0 || n >= (int)s.pieces.size()) continue;
            Piece& p = s.pieces[n];
            while (in >> tok) { size_t e = tok.find('='); if (e == std::string::npos) continue; std::string k = tok.substr(0, e); float v = (float)atof(tok.c_str() + e + 1);
                if (k == "dx") p.dx = (int)v; else if (k == "dy") p.dy = (int)v; else if (k == "travel") p.travel = v; else if (k == "period") p.period = std::max(0.2f, v); else if (k == "phase") p.phase = v; else if (k == "on") p.on = v; else if (k == "power") p.power = v; }
            continue;
        }
        size_t e = m.find('='); std::string k = m.substr(0, e), v = m.substr(e + 1);
        if (k == "name") s.name = v; else if (k == "author") s.author = v; else if (k == "wrap") s.wrap = v == "1"; else if (k == "finale") s.finale = v == "1";
        else if (k == "world") for (int i = 0; i < WD_COUNT; i++) if (v == WORLD_KEY[i]) s.world = i;
    }
    return s;
}
std::vector<std::string> StageToText(const Stage& s) {
    std::vector<std::string> out;
    std::vector<std::string> rows(s.h, std::string(s.w, '.'));
    static const char TC[T_COUNT] = {'.', '#', 'w', 'i', 'g', '-', 'e', '<', '>'};
    for (int y = 0; y < s.h; y++) for (int x = 0; x < s.w; x++) rows[s.h - 1 - y][x] = TC[std::min<int>(s.t[y * s.w + x], T_COUNT - 1)];
    for (const auto& p : s.pieces) for (int yy = p.y; yy < p.y + p.h; yy++) for (int xx = p.x; xx < p.x + p.w; xx++) if (xx >= 0 && yy >= 0 && xx < s.w && yy < s.h) rows[s.h - 1 - yy][xx] = LetterOf(p.kind);
    for (int c : s.crateCols) for (int ry = 0; ry < s.h; ry++) if (rows[ry][c] == '.') { rows[ry][c] = 'C'; break; }
    for (const auto& sp : s.spawns) { int x = (int)floorf(sp.x / TILE), y = (int)floorf(sp.y / TILE + 0.01f); if (x >= 0 && y >= 0 && x < s.w && y < s.h) rows[s.h - 1 - y][x] = 'S'; }
    out = rows;
    out.push_back("name=" + s.name); out.push_back(std::string("world=") + WORLD_KEY[std::clamp(s.world, 0, WD_COUNT - 1)]);
    if (s.wrap) out.push_back("wrap=1"); if (s.finale) out.push_back("finale=1"); if (!s.author.empty()) out.push_back("author=" + s.author);
    for (int i = 0; i < (int)s.pieces.size(); i++) { const Piece& p = s.pieces[i]; out.push_back(TextFormat("@%d dx=%d dy=%d travel=%g period=%g phase=%g on=%g power=%g", i + 1, p.dx, p.dy, p.travel, p.period, p.phase, p.on, p.power)); }
    return out;
}

// ---------------------------------------------------------------- codes
static void PackStage(const Stage& s, Writer& w) {
    w.U8(1); w.Str(s.name.substr(0, 40)); w.Str(s.author.substr(0, 24)); w.U8((uint8_t)s.world); w.U8((s.wrap ? 1 : 0) | (s.finale ? 2 : 0));
    w.U8((uint8_t)s.w); w.U8((uint8_t)s.h);
    // the tiles, run-length
    for (int i = 0; i < (int)s.t.size();) { uint8_t k = s.t[i]; int n = 1; while (i + n < (int)s.t.size() && s.t[i + n] == k && n < 255) n++; w.U8((uint8_t)n); w.U8(k); i += n; }
    w.U8(0);
    w.U8((uint8_t)s.spawns.size()); for (const auto& p : s.spawns) { w.U16((uint16_t)lroundf(p.x / TILE * 2)); w.U16((uint16_t)lroundf(p.y / TILE * 2)); }
    w.U8((uint8_t)s.crateCols.size()); for (int c : s.crateCols) w.U8((uint8_t)c);
    w.U8((uint8_t)s.pieces.size());
    for (const auto& p : s.pieces) {
        w.U8(p.kind); w.U8((uint8_t)p.x); w.U8((uint8_t)p.y); w.U8((uint8_t)p.w); w.U8((uint8_t)p.h); w.U8((uint8_t)(p.dx + 8)); w.U8((uint8_t)(p.dy + 8));
        w.U16((uint16_t)lroundf(p.travel * 10)); w.U16((uint16_t)lroundf(p.period * 10)); w.U16((uint16_t)lroundf(p.phase * 100)); w.U16((uint16_t)lroundf(p.on * 100)); w.U16((uint16_t)lroundf(p.power * 10));
    }
}
static bool UnpackStage(Reader& r, Stage& s) {
    int v = r.U8(); if (v != 1) return false;
    s = Stage{}; s.name = r.Str(); s.author = r.Str(); s.world = std::clamp((int)r.U8(), 0, WD_COUNT - 1); int fl = r.U8(); s.wrap = fl & 1; s.finale = (fl & 2) != 0;
    s.w = r.U8(); s.h = r.U8(); if (s.w < 4 || s.h < 4 || s.w > 128 || s.h > 64) return false;
    s.t.assign(s.w * s.h, T_EMPTY);
    int i = 0; for (;;) { int n = r.U8(); if (n == 0 || r.bad) break; uint8_t k = (uint8_t)r.U8(); if (k >= T_COUNT) return false; for (int j = 0; j < n && i < (int)s.t.size(); j++) s.t[i++] = k; }
    int ns = r.U8(); for (int j = 0; j < ns && !r.bad; j++) { float x = r.U16() / 2.0f * TILE, y = r.U16() / 2.0f * TILE; s.spawns.push_back({x, y}); }
    int nc = r.U8(); for (int j = 0; j < nc && !r.bad; j++) s.crateCols.push_back(r.U8());
    int np = r.U8();
    for (int j = 0; j < np && !r.bad; j++) {
        Piece p; p.kind = (uint8_t)r.U8(); if (p.kind >= PK_COUNT) return false;
        p.x = r.U8(); p.y = r.U8(); p.w = r.U8(); p.h = r.U8(); p.dx = (int)r.U8() - 8; p.dy = (int)r.U8() - 8;
        p.travel = r.U16() / 10.0f; p.period = r.U16() / 10.0f; p.phase = r.U16() / 100.0f; p.on = r.U16() / 100.0f; p.power = r.U16() / 10.0f;
        s.pieces.push_back(p);
    }
    return !r.bad && i == (int)s.t.size();
}
static uint32_t Fnv(const unsigned char* p, int n) { uint32_t h = 2166136261u; for (int i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }
static std::string Seal(const char* tag, const Writer& w) {
    int cz = 0; unsigned char* z = CompressData(w.b.data(), (int)w.b.size(), &cz);
    std::string out = tag;
    if (!z) return out;
    // a checksum ahead of the compressed bytes: a damaged code is refused before it's ever decompressed
    std::vector<unsigned char> body(4 + cz); uint32_t h = Fnv(z, cz); for (int i = 0; i < 4; i++) body[i] = (unsigned char)(h >> (8 * i)); memcpy(body.data() + 4, z, cz); MemFree(z);
    int bl = 0; char* b = EncodeDataBase64(body.data(), (int)body.size(), &bl);
    if (b) { out += std::string(b, strnlen(b, (size_t)bl)); MemFree(b); }
    return out;
}
static bool Open(const std::string& code, const char* tag, std::vector<uint8_t>& bytes, std::string* err) {
    std::string c = code; while (!c.empty() && isspace((unsigned char)c.back())) c.pop_back(); size_t s0 = c.find_first_not_of(" \t\r\n"); if (s0 != std::string::npos) c = c.substr(s0);
    if (c.rfind(tag, 0) != 0) { if (err) *err = std::string("not a ") + tag + " code"; return false; }
    for (size_t i = strlen(tag); i < c.size(); i++) { char ch = c[i]; if (!isalnum((unsigned char)ch) && ch != '+' && ch != '/' && ch != '=') { if (err) *err = "the code has stray characters"; return false; } }
    if (c.size() < strlen(tag) + 12) { if (err) *err = "the code is too short"; return false; }
    int zl = 0; unsigned char* z = DecodeDataBase64((const unsigned char*)(c.c_str() + strlen(tag)), &zl);   // (raylib 5.5: from a C string)
    if (!z || zl <= 4) { if (err) *err = "the code is damaged"; if (z) MemFree(z); return false; }
    uint32_t h = 0; for (int i = 0; i < 4; i++) h |= (uint32_t)z[i] << (8 * i);
    if (h != Fnv(z + 4, zl - 4)) { MemFree(z); if (err) *err = "the code is damaged (the checksum doesn't match)"; return false; }
    int rl = 0; unsigned char* raw = DecompressData(z + 4, zl - 4, &rl); MemFree(z);
    if (!raw || rl <= 0) { if (err) *err = "the code is damaged"; if (raw) MemFree(raw); return false; }
    bytes.assign(raw, raw + rl); MemFree(raw);
    return true;
}
std::string StageToCode(const Stage& s) { Writer w; PackStage(s, w); return Seal("SCF1-", w); }
bool StageFromCode(const std::string& code, Stage& out, std::string* err) {
    std::vector<uint8_t> b; if (!Open(code, "SCF1-", b, err)) return false;
    Reader r(b); if (!UnpackStage(r, out)) { if (err) *err = "the stage inside doesn't read"; return false; }
    return true;
}
std::string PackToCode(const std::vector<Stage>& pack) { Writer w; int n = std::min(20, (int)pack.size()); w.U8((uint8_t)n); for (int i = 0; i < n; i++) PackStage(pack[i], w); return Seal("SCP1-", w); }
bool PackFromCode(const std::string& code, std::vector<Stage>& out, std::string* err) {
    std::vector<uint8_t> b; if (!Open(code, "SCP1-", b, err)) return false;
    Reader r(b); int n = r.U8(); out.clear();
    for (int i = 0; i < n; i++) { Stage s; if (!UnpackStage(r, s)) { if (err) *err = "a stage inside doesn't read"; return false; } out.push_back(s); }
    return true;
}

// ---------------------------------------------------------------- the reachability check (the real movement code, a search over landings)
Reach CheckReachable(const Stage& st) {
    Reach R; R.spawns = (int)st.spawns.size();
    if (st.spawns.size() < 2) { R.why = "fewer than two spawns"; return R; }
    World w; w.Init(st, 1, 1); w.nextCrate = 1e9f; w.wallOn = false;
    Stick& k = w.sticks[0];
    // a node: where a stick can stand, at half-tile resolution
    auto key = [&](Vector2 p) { return (int)floorf(p.x / (TILE * 0.5f)) * 4096 + (int)floorf(p.y / (TILE * 0.5f) + 0.2f); };
    auto deadly = [&](const Stick& s) {
        if (!s.alive || s.pos.y < -1.5f || (!st.wrap && (s.pos.x < -0.5f || s.pos.x > st.Width() + 0.5f))) return true;
        for (const auto& p : st.pieces) if (p.kind == PK_PROPELLER && s.pos.x > p.x * TILE - 0.3f && s.pos.x < (p.x + p.w) * TILE + 0.3f && s.pos.y < (p.y + p.h) * TILE && s.pos.y + s.height > p.y * TILE) return true;
        return false;
    };
    // settle each spawn onto the floor
    std::vector<Vector2> starts;
    for (const auto& sp : st.spawns) {
        w.SpawnStick(k, sp, 1); for (int i = 0; i < 240; i++) { k.in = Input{}; w.StepController(k); }
        starts.push_back(k.pos);
    }
    struct Act { int dir; int hold; int frames; bool wall; };
    std::vector<Act> acts;
    for (int d : {-1, 1}) { acts.push_back({d, 0, 18, false}); acts.push_back({d, 0, 50, false}); for (int h : {6, 14, 40}) acts.push_back({d, h, 160, false}); acts.push_back({d, 40, 260, true}); }
    for (int h : {14, 40}) acts.push_back({0, h, 120, false});
    acts.push_back({0, 0, 30, false});   // (drop through rope: down)
    auto explore = [&](Vector2 from, std::set<int>& seen) {
        std::queue<std::pair<Vector2, Vector2>> q;   // (position, velocity at the start: standing)
        q.push({from, {0, 0}}); seen.insert(key(from));
        while (!q.empty() && (int)seen.size() < 6000) {
            auto [p, v] = q.front(); q.pop();
            for (size_t ai = 0; ai < acts.size(); ai++) {
                const Act& a = acts[ai];
                k = Stick{}; k.id = 0; k.pos = p; k.vel = v; k.st = S_STAND; k.grounded = true; k.fallTop = p.y; k.face = a.dir ? a.dir : 1;
                bool dead = false, landed = false;
                int limit = a.hold == 0 ? a.frames + 160 : a.frames;   // (a walk off a ledge keeps falling until it lands)
                for (int f = 0; f < limit; f++) {
                    k.in = Input{}; k.in.moveX = f < a.frames || a.hold > 0 ? (float)a.dir : 0.0f; k.in.jump = f < a.hold;
                    if (ai == acts.size() - 1) k.in.moveY = -1;
                    if (a.wall && f > 40 && k.st == S_WALL && k.climbT > 1.2f) { k.in.jump = true; k.in.moveX = (float)a.dir; }
                    w.StepController(k);
                    if (k.st == S_RAGDOLL) { k.st = S_STAND; }   // (a long fall's stun: still a landing)
                    if (deadly(k)) { dead = true; break; }
                    if (f > 4 && k.grounded && fabsf(k.vel.y) < 0.01f && (a.hold == 0 || f > a.hold)) { landed = true; if (a.hold > 0 || f >= a.frames) break; }
                }
                if (dead || !landed || !k.grounded) continue;
                int kk = key(k.pos);
                if (seen.count(kk)) continue;
                seen.insert(kk); q.push({k.pos, {0, 0}});
            }
        }
    };
    std::vector<std::set<int>> reach(starts.size());
    for (size_t i = 0; i < starts.size(); i++) { explore(starts[i], reach[i]); R.nodes += (int)reach[i].size();
        if (getenv("DEPTH_REACHTRACE")) { float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f; for (int kk : reach[i]) { float x = (kk / 4096) * TILE * 0.5f, y = (kk % 4096) * TILE * 0.5f; x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); } printf("    [%s] spawn %d at (%.2f, %.2f): %d places, x %.1f..%.1f, y %.1f..%.1f\n", st.name.c_str(), (int)i + 1, starts[i].x, starts[i].y, (int)reach[i].size(), x0, x1, y0, y1); } }
    for (size_t i = 0; i < starts.size(); i++) for (size_t j = 0; j < starts.size(); j++) {
        if (i == j) continue;
        bool ok = false;   // (within a tile of the other spawn's landing)
        for (int dx = -2; dx <= 2 && !ok; dx++) for (int dy = -1; dy <= 1 && !ok; dy++) ok = reach[i].count(key(starts[j]) + dx * 4096 + dy) > 0;
        if (!ok) { R.pairsFailed++; if (R.why.empty()) R.why = TextFormat("spawn %d can't reach spawn %d", (int)i + 1, (int)j + 1); }
    }
    R.ok = R.pairsFailed == 0;
    return R;
}

// ---------------------------------------------------------------- the built-in packs
std::vector<Stage> LoadWorldPack(int world) {
    std::vector<Stage> out;
    std::ifstream f(rt::DataDir() + "/../scuffle/stages/" + WORLD_KEY[std::clamp(world, 0, WD_COUNT - 1)] + ".txt");
    std::string line, name; std::vector<std::string> block;
    auto flush = [&]() { if (!block.empty()) { Stage s = StageFromText(block, name.c_str()); s.world = world; for (const auto& l : block) if (l.rfind("world=", 0) == 0) s = StageFromText(block, name.c_str()); out.push_back(s); } block.clear(); };
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("==", 0) == 0) { flush(); name = line.size() > 3 ? line.substr(3) : "Untitled"; continue; }
        if (line.empty() || line[0] == ';') continue;
        block.push_back(line);
    }
    flush();
    for (auto& s : out) if (s.world != world) s.world = world;
    return out;
}

} // namespace sf
