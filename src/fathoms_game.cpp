// Fathoms, the Deep Arcade's strategy game (Scene::Fathoms): an overhead RTS camera over the archipelago, drawn with
// the arcade's inked 3D renderer in daylight; drag-select, right-click orders, a command card, a minimap, the
// diplomacy / pirate / tribe / Exchange / Logbook panels. Solo games step a local World (AI rivals); networked games
// mirror the host's fog-filtered snapshots. Every action is a fa::Command (Issue), so solo and network play alike.
#include "game.h"
#include "input.h"
#include "redtide_render.h"
#include "fathoms.h"
#include "fathoms_net.h"
#include "fathoms_art.h"
#include "arcade_session.h"
#include "sound.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace fa;

namespace {
struct Msg { std::string text; Color c; float t; Vector2 at; };
struct Mark { Vector2 p; float t; Color c; };
struct FScene {
    bool active = false, shot = false; World local, mirror; World* hostW = nullptr; arcade::Session* net = nullptr; std::string netName; bool helloSent = false;
    int me = 0; int seenVersion = -1; uint32_t evTotal = 0, evSeen = 0; float t = 0, acc = 0;
    Camera3D cam{}; Vector2 camC{64, 64}; float camH = 34, camHWant = 34;
    std::vector<int> sel; int selB = -1, selSite = -1, selNode = -1;
    bool dragging = false; Vector2 dragA{};
    int place = -1; int order = 0;   // order: 1 attack-move, 2 patrol, 3 hero ability point, 4 rally, 5 unload point, 6 convert target, 7 trade target
    std::vector<int> groups[10];
    std::deque<Msg> msgs; std::vector<Mark> marks;
    int panel = 0; int panelSite = -1; int page = 0; bool help = true; float helpT = 0;
    TerrainMesh terrain; float shadeT = 0; Model sea{}; bool seaReady = false;
    Texture2D mini{}; bool miniReady = false; float miniT = 0; uint32_t miniKey = 0; std::vector<Color> miniBase;
    std::string toast; float toastT = 0;
    fa::Settings solo;
    float lastAttackT = -100; Vector2 lastAttackAt{};
    bool over = false;
};
FScene S;
World& W() { return S.hostW ? *S.hostW : S.net ? S.mirror : S.local; }
Player& Me() { return W().players[std::clamp(S.me, 0, (int)W().players.size() - 1)]; }
const Balance& Bl() { return B(); }
const Color SCREEN_DIM{160, 166, 180, 255};
Color FadeC(Color c, float a) { c.a = (unsigned char)std::clamp(a * 255.0f, 0.0f, 255.0f); return c; }
Color Mix2(Color a, Color b, float k) { k = std::clamp(k, 0.0f, 1.0f); return {(unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k), (unsigned char)(a.b + (b.b - a.b) * k), 255}; }
std::string UName(const UnitDef& d) { if (!d.name.empty() && d.name != d.key) return d.name; std::string s = d.key; for (auto& ch : s) if (ch == '_') ch = ' '; if (!s.empty()) s[0] = (char)toupper(s[0]); return s; }
std::string BName(const std::string& key) {
    static const std::map<std::string, std::string> N = {{"harbor", "Harbor"}, {"cottage", "Cottage"}, {"farmstead", "Farmstead"}, {"mine_shed", "Mine Shed"}, {"farm", "Farm"}, {"dock", "Dock"}, {"barracks", "Barracks"}, {"stable", "Stable"}, {"lighthouse", "Lighthouse"}, {"watchtower", "Watchtower"}, {"palisade", "Palisade"}, {"seawall", "Stone Seawall"}, {"colony_hall", "Colony Hall"}, {"workshop", "Workshop"}, {"siege_works", "Siege Works"}, {"exchange", "Exchange"}, {"coastal_battery", "Coastal Battery"}, {"coaling_station", "Coaling Station"}, {"chapel", "Chapel of the Deep"}, {"airship_hangar", "Airship Hangar"}, {"flak_tower", "Flak Tower"}, {"wonder", "Wonder"}, {"totem", "Totem"}, {"brood_pool", "Brood Pool"}};
    auto it = N.find(key); return it != N.end() ? it->second : key;
}
std::string TName(const TechDef& t) { if (!t.name.empty() && t.name != t.key) { std::string s = t.name; for (auto& ch : s) if (ch == '_') ch = ' '; s[0] = (char)toupper(s[0]); return s; } std::string s = t.key; for (auto& ch : s) if (ch == '_') ch = ' '; s[0] = (char)toupper(s[0]); return s; }
std::string CostStr(const Cost& c) { std::string s; static const char* A[R_COUNT] = {"F", "B", "C", "I", "D"}; for (int r = 0; r < R_COUNT; r++) if (c[r] > 0.5f) s += (s.empty() ? "" : " ") + std::to_string((int)lroundf(c[r])) + A[r]; return s.empty() ? "free" : s; }
bool Afford(const Cost& c) { for (int r = 0; r < R_COUNT; r++) if (Me().res[r] + 1e-3f < c[r]) return false; return true; }
void Say(const std::string& s, Color c = WHITE, Vector2 at = {-1, -1}) { S.msgs.push_back({s, c, 0, at}); if (S.msgs.size() > 7) S.msgs.pop_front(); }

// ---------------------------------------------------------------- commands (every action goes this way)
void Issue(Command c) {
    c.player = S.me;
    if (S.net) { Writer w; w.U8(0); WriteCommand(c, w); S.net->Act(w); return; }
    W().Apply(c);
}
Command Cmd(int kind) { Command c; c.kind = (uint8_t)kind; return c; }
std::vector<int> MySel() { std::vector<int> v; for (int id : S.sel) if (const Unit* u = W().U(id)) if (u->owner == S.me) v.push_back(id); return v; }

// ---------------------------------------------------------------- the camera and picking
Vector3 W3(Vector2 p, float lift = 0) { return {p.x, TileY(W(), p.x, p.y) + lift, p.y}; }
void StepCam(float dt) {
    World& w = W(); float sp = (12 + S.camH * 0.9f) * dt;
    if (!Typing()) {
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A) && !IsKeyDown(KEY_LEFT_CONTROL)) S.camC.x -= sp;
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) S.camC.x += sp;
        if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) S.camC.y -= sp;
        if (IsKeyDown(KEY_DOWN) || (IsKeyDown(KEY_S) && IsKeyDown(KEY_LEFT_SHIFT))) S.camC.y += sp;
    }
    Vector2 m = GetMousePosition(); if (!S.shot && IsWindowFocused()) { if (m.x < 6) S.camC.x -= sp; if (m.x > SCREEN_W - 6) S.camC.x += sp; if (m.y < 6) S.camC.y -= sp; if (m.y > SCREEN_H - 6) S.camC.y += sp; }
    float wh = GetMouseWheelMove(); if (wh != 0) S.camHWant = std::clamp(S.camHWant * (wh > 0 ? 0.88f : 1.14f), 12.0f, 80.0f);
    S.camH += (S.camHWant - S.camH) * std::min(1.0f, dt * 10);
    S.camC.x = std::clamp(S.camC.x, 4.0f, (float)w.W - 4); S.camC.y = std::clamp(S.camC.y, 4.0f, (float)w.H - 4);
    S.cam.target = {S.camC.x, 0, S.camC.y}; S.cam.position = {S.camC.x, S.camH, S.camC.y + S.camH * 0.62f}; S.cam.up = {0, 1, 0}; S.cam.fovy = 45; S.cam.projection = CAMERA_PERSPECTIVE;
}
bool Ground(Vector2 mouse, Vector2* out) {
    Ray r = GetScreenToWorldRayEx(mouse, S.cam, SCREEN_W, SCREEN_H); if (r.direction.y > -1e-3f) return false;
    float y = 0.2f; Vector3 p{};
    for (int i = 0; i < 3; i++) { float t = (y - r.position.y) / r.direction.y; p = Vector3Add(r.position, Vector3Scale(r.direction, t)); y = TileY(W(), p.x, p.z) + 0.1f; }
    *out = {p.x, p.z}; return true;
}
Vector2 Scr(Vector3 p) { return GetWorldToScreenEx(p, S.cam, SCREEN_W, SCREEN_H); }
bool Visible(const Unit& u) { World& w = W(); if (u.dead || u.inside >= 0 || u.garrisoned >= 0) return false; if (u.owner == S.me || w.Allied(u.owner, S.me)) return true; if (u.hiddenT > 0) return false; return w.Sees(S.me, u.p); }
bool Known(const Building& b) { World& w = W(); Vector2 c = b.Centre(); return b.owner == S.me || w.Allied(b.owner, S.me) || (w.In((int)c.x, (int)c.y) && Me().explored.size() > (size_t)w.Idx((int)c.x, (int)c.y) && Me().explored[w.Idx((int)c.x, (int)c.y)]); }
bool ExploredAt(Vector2 p) { World& w = W(); int x = (int)p.x, y = (int)p.y; return w.In(x, y) && !Me().explored.empty() && Me().explored[w.Idx(x, y)]; }
float UnitH(const Unit& u) { const UnitDef& d = W().UD(u); return (d.tags & TG_ULTIMATE) ? 5.0f : (d.tags & TG_SHIP) ? 1.6f : (d.tags & TG_AIR) ? 2.8f : (d.tags & TG_LARGE) ? 2.2f : 1.7f; }
Vector3 UnitPos(const Unit& u) { const UnitDef& d = W().UD(u); float y = (d.move == MV_SEA || d.move == MV_AIR) ? 0.0f : TileY(W(), u.p.x, u.p.y); if (d.move == MV_AIR) y = 3.0f; return {u.p.x, y, u.p.y}; }
int PickUnit(Vector2 m) {
    int best = -1; float bd = 22;
    for (const auto& u : W().units) { if (!Visible(u)) continue; Vector3 p = UnitPos(u); p.y += UnitH(u) * 0.5f; Vector2 s = Scr(p); float d = Vector2Distance(s, m); float rr = (W().UD(u).tags & (TG_SHIP | TG_ULTIMATE)) ? 34.0f : 22.0f; if (d < rr && d < bd + (u.owner == S.me ? 4 : 0)) { bd = d; best = u.id; } }
    return best;
}
int PickBuilding(Vector2 g) { World& w = W(); int x = (int)g.x, y = (int)g.y; if (!w.In(x, y)) return -1; int id = w.occ[w.Idx(x, y)]; const Building* b = w.Bd(id); return b && Known(*b) ? id : -1; }
int PickNode(Vector2 g) { int best = -1; float bd = 1.3f; World& w = W(); for (size_t i = 0; i < w.nodes.size(); i++) { const Node& n = w.nodes[i]; if (n.amount <= 0 || !ExploredAt(n.p)) continue; float d = Vector2Distance(n.p, g); if (d < bd) { bd = d; best = (int)i; } } return best; }
int PickSite(Vector2 g) { int best = -1; float bd = 3.5f; World& w = W(); for (size_t i = 0; i < w.sites.size(); i++) { const Site& s = w.sites[i]; if (s.kind == S_KRAKEN || s.kind == S_GHOST || !ExploredAt(s.p)) continue; if (s.kind == S_RUIN && s.state == 2 && s.relic <= 0) continue; float d = Vector2Distance(s.p, g); if (d < bd) { bd = d; best = (int)i; } } return best; }

// ---------------------------------------------------------------- orders from clicks
void RightClick(Vector2 m) {
    World& w = W(); std::vector<int> mine = MySel(); Vector2 g; if (!Ground(m, &g)) return;
    if (S.selB >= 0 && mine.empty()) { const Building* b = w.Bd(S.selB); if (b && b->owner == S.me) { Command c = Cmd(C_RALLY); c.target = b->id; c.at = g; Issue(c); S.marks.push_back({g, 0, {120, 220, 255, 255}}); } return; }
    if (mine.empty()) return;
    int uid = PickUnit(m);
    if (uid >= 0) {
        const Unit* t = w.U(uid);
        if (t && w.Enemies(S.me, t->owner)) { Command c = Cmd(C_ATTACK); c.units = mine; c.target = uid; c.a = 0; Issue(c); S.marks.push_back({t->p, 0, Pal::Bad}); PlayCue("ui.click"); return; }
        if (t && t->owner == S.me && w.UD(*t).carry > 0) { Command c = Cmd(C_BOARD); c.units = mine; c.target = uid; Issue(c); S.marks.push_back({t->p, 0, Pal::Good}); return; }
    }
    int bid = PickBuilding(g);
    if (bid >= 0) {
        const Building* b = w.Bd(bid);
        if (b && w.Enemies(S.me, b->owner)) { Command c = Cmd(C_ATTACK); c.units = mine; c.target = bid; c.a = 1; Issue(c); S.marks.push_back({b->Centre(), 0, Pal::Bad}); return; }
        if (b && b->owner == S.me) {
            bool workers = false; for (int id : mine) if (const Unit* u = w.U(id)) if ((w.UD(*u).tags & TG_WORKER) && !(w.UD(*u).tags & TG_SHIP)) workers = true;
            if (workers && (b->progress < 1 || b->hp < w.BD(*b).hp)) { Command c = Cmd(C_REPAIR); c.units = mine; c.target = bid; Issue(c); S.marks.push_back({b->Centre(), 0, Pal::Brass}); return; }
            if (w.BD(*b).garrison > 0) { Command c = Cmd(C_GARRISON); c.units = mine; c.target = bid; Issue(c); S.marks.push_back({b->Centre(), 0, Pal::Good}); return; }
        }
    }
    int site = PickSite(g);
    if (site >= 0 && w.sites[site].hp > 0 && (w.sites[site].kind == S_COVE || w.sites[site].kind == S_TRIBE) && IsKeyDown(KEY_LEFT_CONTROL)) { Command c = Cmd(C_ATTACK); c.units = mine; c.target = site; c.a = 3; Issue(c); S.marks.push_back({w.sites[site].p, 0, Pal::Bad}); return; }
    int node = PickNode(g);
    if (node >= 0) { Command c = Cmd(C_GATHER); for (int id : mine) if (const Unit* u = w.U(id)) if (w.UD(*u).tags & TG_WORKER) c.units.push_back(id); if (!c.units.empty()) { c.target = node; Issue(c); S.marks.push_back({w.nodes[node].p, 0, Pal::Good}); return; } }
    Command c = Cmd(C_MOVE); c.units = mine; c.at = g; Issue(c); S.marks.push_back({g, 0, {150, 255, 160, 255}});
}
void SelectBox(Vector2 a, Vector2 b, bool add) {
    Rectangle r{std::min(a.x, b.x), std::min(a.y, b.y), fabsf(a.x - b.x), fabsf(a.y - b.y)};
    if (!add) S.sel.clear(); S.selB = -1; S.selSite = -1; S.selNode = -1;
    bool anyMil = false; std::vector<int> got;
    for (const auto& u : W().units) { if (u.owner != S.me || !Visible(u)) continue; Vector2 s = Scr(Vector3Add(UnitPos(u), {0, 0.5f, 0})); if (CheckCollisionPointRec(s, r)) { got.push_back(u.id); if (!(W().UD(u).tags & TG_WORKER)) anyMil = true; } }
    for (int id : got) { const Unit* u = W().U(id); if (anyMil && (W().UD(*u).tags & TG_WORKER)) continue; if (std::find(S.sel.begin(), S.sel.end(), id) == S.sel.end()) S.sel.push_back(id); }
}
void LeftClick(Vector2 m, bool add) {
    World& w = W(); Vector2 g; if (!Ground(m, &g)) return;
    if (S.place >= 0) {
        int sz = Bl().buildings[S.place].size; int x = (int)floorf(g.x - sz * 0.5f + 0.5f), y = (int)floorf(g.y - sz * 0.5f + 0.5f);
        Command c = Cmd(C_BUILD); c.def = S.place; c.x = x; c.y = y; c.units = MySel(); std::string why;
        if (!w.CanPlace(S.me, S.place, x, y, &why)) { S.toast = "Can't build there: " + why; S.toastT = 2.5f; FathomsCue(FAC_DENY, 0.7f, 0); return; }
        Issue(c); PlayCue("ui.click"); if (!IsKeyDown(KEY_LEFT_SHIFT)) S.place = -1; return;
    }
    if (S.order) {
        std::vector<int> mine = MySel(); int o = S.order; S.order = 0;
        if (o == 1 || o == 2) { Command c = Cmd(o == 1 ? C_ATTACK_MOVE : C_PATROL); c.units = mine; c.at = g; Issue(c); S.marks.push_back({g, 0, Pal::Bad}); return; }
        if (o == 3) { Command c = Cmd(C_ABILITY); c.units = {Me().heroUnit}; c.at = g; c.target = PickUnit(m); Issue(c); return; }
        if (o == 4) { Command c = Cmd(C_RALLY); c.target = S.selB; c.at = g; Issue(c); return; }
        if (o == 5) { Command c = Cmd(C_UNLOAD); c.units = mine; c.at = g; Issue(c); S.marks.push_back({g, 0, Pal::Good}); return; }
        if (o == 6) { Command c = Cmd(C_CONVERT); c.units = mine; c.target = PickUnit(m); Issue(c); return; }
        if (o == 7) { int bid = PickBuilding(g); int site = PickSite(g); Command c = Cmd(C_TRADE); c.units = mine; if (bid >= 0) { c.target = bid; c.a = 0; c.at = w.Bd(bid)->Centre(); } else if (site >= 0 && w.sites[site].kind == S_COVE) { c.target = site; c.a = 1000 + site; c.at = w.sites[site].p; } else return; Issue(c); return; }
    }
    int uid = PickUnit(m);
    if (uid >= 0) {
        const Unit* u = w.U(uid);
        if (IsKeyDown(KEY_LEFT_CONTROL) || (S.sel.size() == 1 && S.sel[0] == uid && GetTime() - 0 < 0)) {}
        static double lastClick = 0; static int lastId = -1; bool dbl = GetTime() - lastClick < 0.3 && lastId == uid; lastClick = GetTime(); lastId = uid;
        if (dbl && u->owner == S.me) { S.sel.clear(); for (const auto& o : w.units) if (o.owner == S.me && o.def == u->def && Visible(o) && Vector2Distance(Scr(UnitPos(o)), {SCREEN_W / 2.0f, SCREEN_H / 2.0f}) < 900) S.sel.push_back(o.id); S.selB = -1; return; }
        if (!add) S.sel.clear(); if (std::find(S.sel.begin(), S.sel.end(), uid) == S.sel.end()) S.sel.push_back(uid); S.selB = -1; S.selSite = -1; S.selNode = -1; PlayCue("ui.click"); return;
    }
    int bid = PickBuilding(g); if (bid >= 0) { S.sel.clear(); S.selB = bid; S.selSite = -1; S.selNode = -1; PlayCue("ui.click"); return; }
    int site = PickSite(g); if (site >= 0) { S.sel.clear(); S.selB = -1; S.selSite = site; S.selNode = -1; const Site& st = w.sites[site]; if (st.kind == S_COVE) { S.panel = 2; S.panelSite = site; } if (st.kind == S_TRIBE && st.hp > 0) { S.panel = 3; S.panelSite = site; } return; }
    int node = PickNode(g); if (node >= 0) { S.sel.clear(); S.selB = -1; S.selNode = node; return; }
    if (!add) { S.sel.clear(); S.selB = -1; S.selSite = -1; S.selNode = -1; }
}

// ---------------------------------------------------------------- events: messages and sounds
void Cue(int kind, Vector2 at) { float d = Vector2Distance(at, S.camC), k = kind == FAC_ERA || kind == FAC_ALARM || kind == FAC_TECH || kind == FAC_BUILT || kind == FAC_TREMOR || kind == FAC_KRAKEN || kind == FAC_ULTIMATE || kind == FAC_DRUMS ? 0.8f : std::clamp(1.0f - d / (S.camH * 1.4f), 0.0f, 1.0f); if (k > 0.02f) FathomsCue(kind, k, std::clamp((at.x - S.camC.x) / std::max(8.0f, S.camH), -0.9f, 0.9f)); }
std::string Who(int p) { World& w = W(); if (p >= 0 && p < (int)w.players.size()) return p == S.me ? "You" : w.players[p].name; return p == OWN_PIRATE ? "Pirates" : p == OWN_TRIBE ? "A tribe" : "Something"; }
void ReadEvents() {
    World& w = W(); uint32_t total = w.evCount;
    if (S.evSeen > total) S.evSeen = total;
    int n = (int)std::min<uint32_t>(total - S.evSeen, (uint32_t)w.events.size());
    for (int i = (int)w.events.size() - n; i < (int)w.events.size(); i++) {
        const Event& e = w.events[i]; bool near = Vector2Distance(e.at, S.camC) < S.camH * 1.1f;
        switch (e.kind) {
            case EV_SHOT: if (near && w.Sees(S.me, e.at)) Cue(e.c == D_BLAST ? FAC_BOOM : FAC_SHOT, e.at); break;
            case EV_DIE: if (near && w.Sees(S.me, e.at)) Cue(FAC_DIE, e.at); if (w.In((int)e.at.x, (int)e.at.y)) { const Unit* x = nullptr; (void)x; } break;
            case EV_BUILT: if (e.b == S.me) { Say(BName(Bl().buildings[std::clamp(e.c, 0, (int)Bl().buildings.size() - 1)].key) + " built", Pal::Good, e.at); Cue(FAC_BUILT, e.at); } break;
            case EV_TRAINED: if (e.b == S.me && near) Cue(FAC_READY, e.at); break;
            case EV_RESEARCHED: if (e.a == S.me) { Say(TName(Bl().techs[std::clamp(e.b, 0, (int)Bl().techs.size() - 1)]) + " researched", Pal::Teal); Cue(FAC_TECH, e.at); } break;
            case EV_ERA: { static const char* E[3] = {"Sail", "Steam", "Leviathan"}; Say(Who(e.a) + (e.a == S.me ? " reach the " : " reaches the ") + E[std::clamp(e.b, 0, 2)] + " Era", e.a == S.me ? Pal::Brass : WHITE); Cue(FAC_ERA, e.at); break; }
            case EV_HIT: if (e.a > 0 && w.U(e.a) && w.U(e.a)->owner == S.me && S.t - S.lastAttackT > 12) { S.lastAttackT = S.t; S.lastAttackAt = e.at; Say("You are under attack!", Pal::Bad, e.at); Cue(FAC_ALARM, e.at); } else if (e.a < 0 && e.a > -1000) { const Building* b = w.Bd(-e.a); if (b && b->owner == S.me && S.t - S.lastAttackT > 12) { S.lastAttackT = S.t; S.lastAttackAt = e.at; Say(BName(w.BD(*b).key) + " under attack!", Pal::Bad, e.at); Cue(FAC_ALARM, e.at); } } break;
            case EV_RAZED: if (near) Cue(FAC_RAZED, e.at); if (e.b == S.me) Say("Building destroyed", Pal::Good, e.at); break;
            case EV_CLAIM: if (e.a == S.me) Say(e.c == 1 ? "You hold the volcano's altar!" : "Island claimed", Pal::Brass, e.at); break;
            case EV_ERUPT: Say(e.b == 1 ? "The volcano erupts!" : e.b == 2 ? "The Sun God has fallen" : "The volcano stirs", {255, 150, 70, 255}, e.at); Cue(FAC_ERUPT, e.at); break;
            case EV_TREMOR: Say("Tremors: the volcano will erupt in 30 s", {255, 170, 90, 255}, e.at); Cue(FAC_TREMOR, e.at); break;
            case EV_WEATHER: { static const char* N[4] = {"The weather clears", "Fog rolls in", "A storm is gathering", "A whirlpool opens"}; Say(N[std::clamp(e.a, 0, 3)], {180, 210, 230, 255}, e.at); break; }
            case EV_PIRATE_WARN: if (e.a == S.me) { Say("Pirates have been hired against you! 20 s - buy them off at the cove", Pal::Bad, e.at); Cue(FAC_ALARM, e.at); } break;
            case EV_PIRATE_ARRIVE: if (e.a == S.me) Say("The pirates arrive!", Pal::Bad, e.at); else if (e.b == S.me) Say("Your pirates strike", Pal::Brass, e.at); break;
            case EV_TRIBE_RAID: if (e.a == S.me) { Say("Tribal raiders land!", Pal::Bad, e.at); Cue(FAC_DRUMS, e.at); } break;
            case EV_TRIBE_ALLY: if (e.a == S.me) { Say("Kinship: the tribe is your ally", Pal::Good, e.at); Cue(FAC_DRUMS, e.at); } break;
            case EV_RELIC: Say(Who(e.a) + (e.a == S.me ? " recover a relic" : " recovers a relic"), {120, 230, 220, 255}, e.at); Cue(FAC_RELIC, e.at); break;
            case EV_KRAKEN: Say(e.b == 2 ? "The Kraken is slain!" : "The Kraken wakes in the central sea", {230, 120, 120, 255}, e.at); Cue(FAC_KRAKEN, e.at); break;
            case EV_GHOST: Say(e.b == 2 ? "The Ghost Ship sinks" : "A Ghost Pirate Ship roams the sea", {150, 230, 210, 255}, e.at); break;
            case EV_ELIM: Say(Who(e.a) + (e.a == S.me ? " have been eliminated" : " has been eliminated"), Pal::Bad); break;
            case EV_DIPLO: if (e.b == S.me || e.a == S.me) { static const char* D[5] = {"War", "Ceasefire", "Peace", "Open Harbors", "Allied"}; Say(Who(e.a) + " -> " + Who(e.b) + ": " + D[std::clamp(e.c, 0, 4)], {200, 220, 255, 255}); } break;
            case EV_MOLT: if (near) Cue(FAC_MOLT, e.at); break;
            case EV_CONVERT: if (near) Cue(FAC_CONVERT, e.at); break;
            case EV_ULTIMATE: if (e.b == S.me || near) Cue(FAC_ULTIMATE, e.at); if (e.c >= 0 && e.b >= 0 && e.b < (int)w.players.size()) Say(Who(e.b) + " summon" + (e.b == S.me ? " " : "s ") + Bl().factions[w.players[e.b].faction].ultimate + "!", {255, 120, 255, 255}, e.at); break;
            case EV_HERO: if (near) Cue(FAC_HERO, e.at); break;
            case EV_CARD: if (e.a == S.me && e.b > 0) { S.panel = 6; } break;
            case EV_LAND: break;
            case EV_VICTORY: S.over = true;  break;
            default: break;
        }
    }
    S.evSeen = total;
}

// ---------------------------------------------------------------- 3D
void EnsureSea() {
    if (S.seaReady || !IsWindowReady()) return;
    const int N = 64; Mesh m{}; m.vertexCount = N * N * 6; m.triangleCount = N * N * 2;
    m.vertices = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float)); m.texcoords = (float*)MemAlloc(m.vertexCount * 2 * sizeof(float)); m.colors = (unsigned char*)MemAlloc(m.vertexCount * 4); m.normals = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    for (int i = 0; i < m.vertexCount * 3; i++) m.normals[i] = (i % 3 == 1) ? 1.0f : 0.0f;
    for (int i = 0; i < m.vertexCount; i++) { m.colors[i * 4] = 40; m.colors[i * 4 + 1] = 120; m.colors[i * 4 + 2] = 130; m.colors[i * 4 + 3] = 200; }
    UploadMesh(&m, true); S.sea = LoadModelFromMesh(m); S.seaReady = true;
}
void UpdateSea(float t) {
    const int N = 64; float sc = std::max(1.5f, S.camH / 18.0f);
    float cx = floorf(S.camC.x / sc) * sc - N * sc / 2, cz = floorf(S.camC.y / sc) * sc - N * sc / 2;
    Mesh& m = S.sea.meshes[0]; float* v = m.vertices; int k = 0;
    auto H = [&](float x, float z) { return 0.05f * sinf(x * 0.5f + t * 1.1f) + 0.04f * sinf(z * 0.7f - t * 1.3f); };
    auto put = [&](int i, int j) { float x = cx + i * sc, z = cz + j * sc; v[k++] = x; v[k++] = H(x, z); v[k++] = z; };
    for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) { put(i, j); put(i + 1, j); put(i + 1, j + 1); put(i, j); put(i + 1, j + 1); put(i, j + 1); }
    UpdateMeshBuffer(m, 0, v, m.vertexCount * 3 * sizeof(float), 0);
}
Matrix Place(Vector3 p, float yaw, float s) { return MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s), MatrixRotateY(-yaw)), MatrixTranslate(p.x, p.y, p.z)); }
void DrawRing(Vector2 at, float r, Color c, float lift = 0.12f) { rt::DrawStaticGlow(RingModel(), MatrixMultiply(MatrixScale(r, 1, r), MatrixTranslate(at.x, TileY(W(), at.x, at.y) + lift, at.y)), c, 0.8f); }
void DrawWorld3D(float dt) {
    World& w = W(); (void)dt;
    BuildTerrain(w, S.terrain);
    S.shadeT -= dt; if (S.shadeT <= 0) { ShadeTerrain(w, S.terrain, S.me); S.shadeT = 0.4f; }
    rt::DrawStatic(S.terrain.model, MatrixIdentity(), WHITE);
    // resource nodes (where explored)
    for (const auto& n : w.nodes) { if (n.amount <= 0 || n.kind == N_FARM || !ExploredAt(n.p)) continue; float y = Bl().nodes[n.kind].water ? 0.0f : TileY(w, n.p.x, n.p.y); float k = 0.7f + 0.3f * std::min(1.0f, n.amount / std::max(1.0f, n.cap));
        float bob = n.kind == N_FISH ? sinf(S.t * 2 + n.p.x) * 0.05f : 0; rt::DrawStatic(NodeModel(n.kind), Place({n.p.x, y + bob, n.p.y}, n.p.x * 1.7f + (n.kind == N_FISH ? S.t * 0.4f : 0), k), WHITE);
        if (n.kind == N_VENT) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.3f, 0.05f, 0.3f), MatrixTranslate(n.p.x, y + 0.12f, n.p.y)), {200, 100, 255, 255}, 1.2f + 0.4f * sinf(S.t * 3)); }
    // sites
    for (size_t i = 0; i < w.sites.size(); i++) {
        const Site& s = w.sites[i]; if (!ExploredAt(s.p)) continue; float y = TileY(w, s.p.x, s.p.y);
        if (s.kind == S_COVE) rt::DrawStatic(SiteModel(S_COVE, 0), Place({s.p.x, y, s.p.y}, 0.3f, 1), s.hp > 0 ? WHITE : Color{120, 110, 100, 255});
        if (s.kind == S_TRIBE && s.state != 3) rt::DrawStatic(SiteModel(S_TRIBE, s.large), Place({s.p.x, y, s.p.y}, 0.9f, 1), WHITE);
        if (s.kind == S_ALTAR) { rt::DrawStatic(SiteModel(S_ALTAR, 0), Place({s.p.x, y, s.p.y}, 0, 1), WHITE); rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.4f, 0.4f, 0.4f), MatrixTranslate(s.p.x, y + 1.55f, s.p.y)), s.holder >= 0 ? PlayerColor(w.players[s.holder].color) : Color{255, 170, 60, 255}, 1.6f);
            if (s.count == 1) for (auto& q : s.lava) if (((int)(q.x * 7 + q.y * 3) % 3) == 0) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.5f, 0.04f, 0.5f), MatrixTranslate(q.x + 0.5f, TileY(w, q.x + 0.5f, q.y + 0.5f) + 0.05f, q.y + 0.5f)), {255, 90, 30, 255}, 0.6f + 0.6f * sinf(S.t * 8 + q.x)); }
        if (s.kind == S_RUIN && (s.state != 2 || s.relic > 0)) rt::DrawStatic(SiteModel(S_RUIN, s.relic > 0), Place({s.p.x, s.state == 2 ? 0.0f : y, s.p.y}, 0, s.state == 2 ? 0.5f : 1), WHITE);
    }
    for (int k = 0; k < (int)w.tile.size(); k += 1) if (w.tile[k] == T_LAVA && ExploredAt({(float)(k % w.W), (float)(k / w.W)})) { float x = k % w.W + 0.5f, z = k / w.W + 0.5f; rt::DrawCubeGlow(MatrixMultiply(MatrixScale(1.0f, 0.06f, 1.0f), MatrixTranslate(x, TileY(w, x, z) + 0.04f, z)), {255, 110, 30, 255}, 1.2f + 0.3f * sinf(S.t * 4 + x)); }
    // buildings
    for (const auto& b : w.buildings) {
        if (b.dead || !Known(b)) continue; const BuildingDef& d = w.BD(b); int f = IsPlayer(b.owner) && b.owner < (int)w.players.size() ? w.players[b.owner].faction : -1; int col = IsPlayer(b.owner) && b.owner < (int)w.players.size() ? w.players[b.owner].color : 6;
        float y = TileY(w, b.Centre().x, b.Centre().y); if (d.key == "dock") y = 0.15f;
        float k = b.progress < 1 ? 0.25f + 0.75f * b.progress : 1.0f;
        Matrix m = MatrixMultiply(MatrixScale(1, k, 1), MatrixTranslate((float)b.x, y, (float)b.y));
        bool seenNow = w.Sees(S.me, b.Centre()) || b.owner == S.me; rt::DrawStatic(BuildingModel(b.def, f, col), m, seenNow ? WHITE : Color{150, 150, 160, 255});
        if (b.progress < 1) for (int q = 0; q < 4; q++) rt::DrawCubeM(MatrixMultiply(MatrixScale(0.08f, 1.6f * k + 0.4f, 0.08f), MatrixTranslate(b.x + (q % 2) * (b.size - 0.2f) + 0.1f, y + 0.8f * k, b.y + (q / 2) * (b.size - 0.2f) + 0.1f)), {150, 110, 70, 255});
        if (d.key == "lighthouse" && b.progress >= 1) { rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.5f, 0.45f, 0.5f), MatrixTranslate(b.x + 1.0f, y + 3.9f, b.y + 1.0f)), {255, 240, 180, 255}, 2.0f); }
        if (b.relics > 0) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.25f, 0.25f, 0.25f), MatrixTranslate(b.Centre().x, y + 2.6f + 0.1f * sinf(S.t * 2), b.Centre().y)), {120, 240, 230, 255}, 1.8f);
        if (b.id == S.selB) DrawRing(b.Centre(), b.size * 0.75f, b.owner == S.me ? Color{120, 255, 140, 255} : Pal::Bad);
        if (b.id == S.selB && b.hasRally && b.owner == S.me) DrawRing(b.rally, 0.5f, {120, 220, 255, 255});
    }
    // units
    for (const auto& u : w.units) {
        if (!Visible(u)) continue; const UnitDef& d = w.UD(u); int f = IsPlayer(u.owner) && u.owner < (int)w.players.size() ? w.players[u.owner].faction : -1; int col = IsPlayer(u.owner) && u.owner < (int)w.players.size() ? w.players[u.owner].color : u.owner == OWN_PIRATE ? 6 : 7;
        Vector3 p = UnitPos(u); float sc = 1.0f + 0.25f * u.molts;
        bool moving = Vector2Length(u.vel) > 0.05f; float bob = moving ? fabsf(sinf(S.t * 9 + u.id)) * 0.07f : 0;
        if (d.move == MV_SEA) { bob = sinf(S.t * 1.6f + u.p.x * 0.7f) * 0.05f; }
        if (d.move == MV_AIR) bob = sinf(S.t * 1.2f + u.id) * 0.15f;
        float lunge = u.cool > d.reload - 0.25f && d.attack > 0 && d.range < 1.5f ? 0.15f : 0;
        p = Vector3Add(p, {cosf(u.facing) * lunge, bob, sinf(u.facing) * lunge});
        Matrix m = Place(p, u.facing, (d.tags & TG_SHIP) ? 1.15f * sc : 1.45f * sc);
        if (d.move == MV_SEA) m = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixRotateX(sinf(S.t * 1.3f + u.id) * 0.06f), MatrixRotateY(-u.facing)), MatrixScale(1.15f * sc, 1.15f * sc, 1.15f * sc)), MatrixTranslate(p.x, p.y, p.z));
        Color tint = u.cocoonT > 0 ? Color{220, 200, 180, 255} : u.hiddenT > 0 && u.owner == S.me ? Color{170, 190, 210, 255} : WHITE;
        if (u.stunT > 0) tint = Mix2(tint, {255, 255, 140, 255}, 0.4f);
        rt::DrawStatic(UnitModel(u.def, (d.faction >= 0 || (d.tags & (TG_HERO | TG_ULTIMATE))) ? f : (u.owner == OWN_PIRATE || u.owner == OWN_TRIBE || u.owner == OWN_WILD ? -1 : f), col), m, tint);
        bool sel = std::find(S.sel.begin(), S.sel.end(), u.id) != S.sel.end();
        if (sel) DrawRing(u.p, (d.tags & (TG_SHIP | TG_ULTIMATE)) ? 1.3f : 0.55f, u.owner == S.me ? Color{120, 255, 140, 255} : Pal::Bad, d.move == MV_SEA ? 0.08f : 0.12f);
        if (u.relic > 0) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.18f, 0.18f, 0.18f), MatrixTranslate(p.x, p.y + 1.8f, p.z)), {120, 240, 230, 255}, 1.8f);
        if (d.special == "beam" || (d.tags & TG_ULTIMATE)) rt::AddLateLight({p.x, p.y + 2, p.z}, 8, d.special == "beam" ? Color{255, 170, 60, 255} : Color{200, 120, 255, 255}, 1.2f);
    }
    // shots in the air
    for (const auto& s : w.shots) {
        float k = std::clamp(s.t / std::max(0.05f, s.T), 0.0f, 1.0f); Vector2 q = Vector2Lerp(s.a, s.b, k); float arc = s.kind == 1 ? sinf(k * PI) * s.h : 0.3f + sinf(k * PI) * 0.2f;
        float y = std::max(TileY(w, q.x, q.y), 0.0f) + 0.6f + arc;
        Color c = s.kind == 1 ? Color{60, 56, 52, 255} : s.kind == 3 ? Color{255, 160, 60, 255} : s.kind == 4 ? Color{140, 220, 255, 255} : s.kind == 5 ? Color{170, 220, 255, 255} : Color{255, 230, 160, 255};
        if (s.kind == 3 || s.kind == 4) { Vector3 a{s.a.x, TileY(w, s.a.x, s.a.y) + 1.4f, s.a.y}, b{s.b.x, TileY(w, s.b.x, s.b.y) + 0.6f, s.b.y}; Vector3 mid = Vector3Lerp(a, b, 0.5f); float L = Vector3Distance(a, b); rt::DrawCubeGlow(MatrixMultiply(MatrixMultiply(MatrixScale(L, 0.12f, 0.12f), MatrixRotateY(-atan2f(b.z - a.z, b.x - a.x))), MatrixTranslate(mid.x, mid.y, mid.z)), c, 2.0f * (1 - k)); }
        else rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.14f, 0.14f, 0.14f), MatrixTranslate(q.x, y, q.y)), c, s.kind == 1 ? 0.2f : 1.4f);
    }
    // order marks
    for (auto& mk : S.marks) DrawRing(mk.p, 0.3f + mk.t * 1.2f, FadeC(mk.c, 1 - mk.t * 2));
    // the building being placed
    if (S.place >= 0) {
        Vector2 g; if (Ground(GetMousePosition(), &g)) { int sz = Bl().buildings[S.place].size; int x = (int)floorf(g.x - sz * 0.5f + 0.5f), y = (int)floorf(g.y - sz * 0.5f + 0.5f); bool ok = w.CanPlace(S.me, S.place, x, y);
            rt::DrawStatic(BuildingModel(S.place, Me().faction, Me().color), MatrixTranslate((float)x, TileY(w, x + sz * 0.5f, y + sz * 0.5f), (float)y), ok ? Color{150, 255, 160, 255} : Color{255, 110, 100, 255});
            rt::DrawCubeGlow(MatrixMultiply(MatrixScale((float)sz, 0.04f, (float)sz), MatrixTranslate(x + sz * 0.5f, TileY(w, x + sz * 0.5f, y + sz * 0.5f) + 0.05f, y + sz * 0.5f)), ok ? Color{80, 255, 120, 255} : Color{255, 60, 50, 255}, 0.5f); }
    }
}
void Render(float dt) {
    World& w = W(); EnsureSea();
    rt::SceneLight L; float stormK = w.weather == 2 && Vector2Distance(w.weatherAt, S.camC) < w.weatherR + 20 ? 1.0f : 0.0f, fogK = w.weather == 1 && Vector2Distance(w.weatherAt, S.camC) < w.weatherR + 20 ? 1.0f : 0.0f;
    Color horizon{176, 214, 236, 255}, zenith{80, 140, 210, 255};
    L.fog = Mix2(horizon, {120, 126, 134, 255}, stormK * 0.7f); L.fog = Mix2(L.fog, {214, 220, 224, 255}, fogK * 0.7f); L.fogDensity = 0.004f * (1 + 2 * stormK + 4 * fogK); L.fogBanks = 0;
    L.key = {0, 0, 0, 255}; L.lampRange = 1; L.lampPos = {0, -500, 0}; L.lampDir = {0, -1, 0};
    L.fill = {120, 140, 160, 255}; L.rim = {200, 220, 240, 255};
    L.moonDir = Vector3Normalize({-0.45f, -1.0f, -0.35f}); L.moon = {255, 246, 226, 255}; L.moonK = 1.05f * (1 - 0.5f * stormK);
    L.skyAmb = {170, 196, 224, 255}; L.seaAmb = {80, 96, 110, 255}; L.ambK = 0.8f;
    L.surfaceY = 1e5f; L.time = S.t; L.outline = 0.5f; L.outlineTint = {30, 36, 46, 255}; L.stipple = 0; L.grain = 0.2f; L.aoK = 0.45f; L.aoRadius = 0.5f; L.filmic = 0; L.saturation = 1.25f;
    rt::ApplyGameQuality();
    rt::RenderBegin(S.cam, L);
    rt::SkyLook sk; sk.zenith = Mix2(zenith, {90, 96, 106, 255}, stormK * 0.7f); sk.horizon = L.fog; sk.cloud = {236, 240, 244, 255}; sk.moonDir = Vector3Negate(L.moonDir); sk.moonPhase = 0.5f; sk.cloudCover = 0.25f + 0.6f * stormK; sk.stars = 0; sk.time = S.t;
    rt::DrawSkyDome(sk);
    DrawWorld3D(dt);
    UpdateSea(S.t);
    rt::WaterLook wl; wl.deep = {34, 120, 150, 255}; wl.zenith = zenith; wl.horizon = horizon; wl.boatPos = {1e6f, 1e6f}; wl.boatLen = 0.1f; wl.boatBeam = 0.1f; wl.alpha = 0.55f; wl.moonK = 0.7f; wl.crest = 0.03f + 0.15f * stormK;
    rt::DrawWater(S.sea, wl);
    rt::RenderEnd();
}

// ---------------------------------------------------------------- the minimap
const Rectangle MINI{12, SCREEN_H - 200.0f, 188, 188};
void UpdateMini() {
    World& w = W(); uint32_t key = w.set.seed * 7 + (uint32_t)w.W;
    if (!S.miniReady || S.miniKey != key) {
        if (S.miniReady) UnloadTexture(S.mini);
        Image im = GenImageColor(w.W, w.H, BLACK); S.mini = LoadTextureFromImage(im); UnloadImage(im); S.miniReady = true; S.miniKey = key;
        S.miniBase.resize(w.W * w.H);
        for (int k = 0; k < w.W * w.H; k++) { int t = w.tile[k]; S.miniBase[k] = t == T_DEEP ? Color{30, 70, 100, 255} : t == T_SHALLOW ? Color{70, 140, 160, 255} : t == T_KELP ? Color{40, 100, 80, 255} : t == T_CURRENT ? Color{40, 90, 130, 255} : t == T_BEACH ? Color{220, 200, 150, 255} : t == T_JUNGLE ? Color{50, 110, 50, 255} : t == T_HILL ? Color{140, 140, 90, 255} : t == T_MOUNTAIN ? Color{120, 116, 110, 255} : t == T_LAVA ? Color{255, 110, 40, 255} : Color{100, 160, 70, 255}; }
        S.miniT = 0;
    }
    S.miniT -= GetFrameTime(); if (S.miniT > 0) return; S.miniT = 0.5f;
    std::vector<Color> px(w.W * w.H); const Player& P = Me();
    for (int k = 0; k < w.W * w.H; k++) { Color c = S.miniBase[k]; if (w.tile[k] == T_LAVA) c = {255, 110, 40, 255}; int o = w.owner[k]; if (o >= 0 && o < (int)w.players.size() && w.Land(w.tile[k])) c = Mix2(c, PlayerColor(w.players[o].color), 0.35f);
        if (P.explored.empty() || !P.explored[k]) c = {12, 14, 20, 255}; else if (!P.seen[k]) c = {(unsigned char)(c.r * 0.55f), (unsigned char)(c.g * 0.55f), (unsigned char)(c.b * 0.55f), 255}; px[k] = c; }
    UpdateTexture(S.mini, px.data());
}
void DrawMini() {
    World& w = W(); UpdateMini();
    DrawRectangleRounded({MINI.x - 4, MINI.y - 4, MINI.width + 8, MINI.height + 8}, 0.04f, 6, {20, 24, 32, 230});
    DrawTexturePro(S.mini, {0, 0, (float)w.W, (float)w.H}, MINI, {0, 0}, 0, WHITE);
    float kx = MINI.width / w.W, ky = MINI.height / w.H;
    for (const auto& b : w.buildings) if (!b.dead && Known(b)) { Color c = IsPlayer(b.owner) && b.owner < (int)w.players.size() ? PlayerColor(w.players[b.owner].color) : WHITE; DrawRectangle((int)(MINI.x + b.x * kx), (int)(MINI.y + b.y * ky), std::max(2, (int)(b.size * kx)), std::max(2, (int)(b.size * ky)), c); }
    for (const auto& u : w.units) if (Visible(u)) { Color c = IsPlayer(u.owner) && u.owner < (int)w.players.size() ? PlayerColor(w.players[u.owner].color) : u.owner == OWN_PIRATE ? Color{30, 30, 30, 255} : Color{240, 240, 240, 255}; DrawRectangle((int)(MINI.x + u.p.x * kx), (int)(MINI.y + u.p.y * ky), 2, 2, c); }
    for (const auto& s : w.sites) if (ExploredAt(s.p) && (s.kind == S_COVE || s.kind == S_TRIBE || s.kind == S_ALTAR || (s.kind == S_RUIN && s.relic > 0))) DrawCircleLines((int)(MINI.x + s.p.x * kx), (int)(MINI.y + s.p.y * ky), 3, s.kind == S_RUIN ? Color{120, 240, 230, 255} : s.kind == S_ALTAR ? Color{255, 150, 60, 255} : s.kind == S_COVE ? Color{20, 20, 20, 255} : Color{230, 200, 120, 255});
    if (w.weather) DrawCircleLines((int)(MINI.x + w.weatherAt.x * kx), (int)(MINI.y + w.weatherAt.y * ky), w.weatherR * kx, w.weather == 2 ? Color{200, 200, 255, 200} : Color{220, 220, 220, 160});
    if (S.t - S.lastAttackT < 6) DrawCircleLines((int)(MINI.x + S.lastAttackAt.x * kx), (int)(MINI.y + S.lastAttackAt.y * ky), 6 + 4 * sinf(S.t * 10), Pal::Bad);
    // the camera's view
    Vector2 c[4]; Vector2 corners[4] = {{0, 0}, {SCREEN_W, 0}, {SCREEN_W, SCREEN_H}, {0, SCREEN_H}}; for (int i = 0; i < 4; i++) { Vector2 g{S.camC.x, S.camC.y}; Ground(corners[i], &g); c[i] = {MINI.x + std::clamp(g.x, 0.0f, (float)w.W) * kx, MINI.y + std::clamp(g.y, 0.0f, (float)w.H) * ky}; }
    for (int i = 0; i < 4; i++) DrawLineEx(c[i], c[(i + 1) % 4], 1.5f, WHITE);
    Vector2 m = GetMousePosition();
    if (CheckCollisionPointRec(m, MINI)) {
        Vector2 g{(m.x - MINI.x) / kx, (m.y - MINI.y) / ky};
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && S.place < 0) S.camC = g;
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { std::vector<int> mine = MySel(); if (!mine.empty()) { Command cm = Cmd(C_MOVE); cm.units = mine; cm.at = g; Issue(cm); } }
    }
}

// ---------------------------------------------------------------- the HUD
struct Btn { std::string label, tip; int key = 0; bool on = true; std::function<void()> act; Color c{60, 70, 90, 255}; };
std::vector<Btn> gCard;
void Bar(float x, float y, float w, float h, float k, Color c) { DrawRectangleRec({x, y, w, h}, {20, 22, 30, 220}); DrawRectangleRec({x, y, w * std::clamp(k, 0.0f, 1.0f), h}, c); }
void TopBar() {
    World& w = W(); Player& P = Me();
    DrawRectangle(0, 0, SCREEN_W, 30, {18, 22, 32, 230}); DrawLine(0, 30, SCREEN_W, 30, Pal::BrassDk);
    static const Color RC[R_COUNT] = {{230, 170, 90, 255}, {214, 166, 82, 255}, {150, 150, 160, 255}, {200, 110, 255, 255}, {250, 220, 90, 255}};
    static const char* RN[R_COUNT] = {"Food", "Brass", "Coal", "Ichor", "Doubloons"};
    float x = 12; for (int r = 0; r < R_COUNT; r++) { DrawCircle((int)x + 6, 15, 6, RC[r]); TxtBold(TextFormat("%d", (int)P.res[r]), x + 16, 6, 18, WHITE); Rectangle hit{x, 0, 110, 30}; if (CheckCollisionPointRec(GetMousePosition(), hit)) { S.toast = RN[r]; S.toastT = 0.1f; } x += 110; }
    TxtBold(TextFormat("Pop %d/%d", P.pop, P.popCap), x + 6, 6, 18, P.pop >= P.popCap ? Pal::Bad : WHITE); x += 120;
    static const char* E[3] = {"Sail Era", "Steam Era", "Leviathan Era"}; TxtBold(E[std::clamp(P.era, 0, 2)], x, 6, 18, Pal::Brass); x += 140;
    if (P.faction == 4) { TxtBold(TextFormat("Devotion %d", (int)P.devotion), x, 6, 18, {120, 230, 220, 255}); x += 140; }
    int m = (int)w.t / 60, s = (int)w.t % 60; TxtBold(TextFormat("%02d:%02d", m, s), SCREEN_W - 170, 6, 18, WHITE);
    if (w.set.timeCap > 0) { int left = std::max(0, (int)(w.set.timeCap - w.t)); Txt(TextFormat("cap %d:%02d", left / 60, left % 60), SCREEN_W - 106, 8, 15, SCREEN_DIM); }
    TxtBold(TextFormat("Score %d", (int)P.score), SCREEN_W - 300, 6, 18, {200, 220, 255, 255});
    // countdowns everyone sees
    float y = 38;
    if (w.relicLeader >= 0 && w.relicCountT >= 0) { DrawTextCenteredBold(TextFormat("%s holds every relic: victory in %d s", Who(w.relicLeader).c_str(), (int)(Bl().relicCountdown - (w.t - w.relicCountT))), SCREEN_W / 2.0f, y, 18, {120, 240, 230, 255}); y += 22; }
    if (w.volcanoLeader >= 0 && w.volcanoCountT >= 0) { DrawTextCenteredBold(TextFormat("%s holds the volcano: ascension in %d s", Who(w.volcanoLeader).c_str(), (int)(Bl().volcanoCountdown - (w.t - w.volcanoCountT))), SCREEN_W / 2.0f, y, 18, {255, 160, 80, 255}); y += 22; }
    if (P.noTownT >= 0 && P.alive) DrawTextCenteredBold(TextFormat("No town centre! Land a Colony Ship within %d s", (int)(Bl().conquestGrace - (w.t - P.noTownT))), SCREEN_W / 2.0f, y, 18, Pal::Bad);
}
void SelectionPanel(Rectangle r) {
    World& w = W(); Panel(r, {28, 32, 44, 235});
    auto line = [&](const std::string& s, float yy, int sz, Color c) { Txt(s, r.x + 12, r.y + yy, sz, c); };
    std::vector<const Unit*> us; for (int id : S.sel) if (const Unit* u = w.U(id)) if (Visible(*u)) us.push_back(u);
    if (us.size() == 1) {
        const Unit& u = *us[0]; const UnitDef& d = w.UD(u); float mx = MaxHp(w, u);
        TxtBold(UName(d), r.x + 12, r.y + 8, 20, u.owner == S.me ? Pal::Brass : WHITE); line(Who(u.owner), 32, 14, SCREEN_DIM);
        Bar(r.x + 12, r.y + 54, r.width - 24, 10, u.hp / std::max(1.0f, mx), u.hp > mx * 0.5f ? Pal::Good : u.hp > mx * 0.25f ? Pal::Brass : Pal::Bad); line(TextFormat("%d / %d HP", (int)u.hp, (int)mx), 66, 14, WHITE);
        std::string st = d.attack > 0 ? TextFormat("Attack %d %s  range %.0f   Armor %d/%d", (int)d.attack, d.type == D_MELEE ? "melee" : d.type == D_PIERCE ? "pierce" : "blast", d.range, (int)(d.armor[0] + u.auraArmor), (int)(d.armor[1] + u.auraArmor)) : TextFormat("Armor %d/%d", (int)d.armor[0], (int)d.armor[1]);
        line(st, 86, 14, {210, 210, 220, 255});
        if (IsPlayer(u.owner) && !(d.tags & TG_WORKER)) line(TextFormat("Morale %d%s%s", (int)u.morale, u.molts ? TextFormat("   Molts %d", u.molts) : "", u.relic ? "   carrying a RELIC" : ""), 104, 14, u.morale < 50 ? Pal::Bad : Pal::Good);
        if (u.carry > 0) line(TextFormat("Carrying %d %s", (int)u.carry, ResName(u.carryRes)), 104, 14, Pal::Brass);
        if (!u.cargo.empty()) line(TextFormat("Aboard: %d", (int)u.cargo.size()), 122, 14, WHITE);
        if (u.stunT > 0 || u.bleedT > 0 || u.poisonT > 0 || u.cocoonT > 0) line(std::string(u.stunT > 0 ? "Stunned " : "") + (u.bleedT > 0 ? "Bleeding " : "") + (u.poisonT > 0 ? "Poisoned " : "") + (u.cocoonT > 0 ? "Molting " : ""), 122, 14, {255, 220, 120, 255});
    } else if (us.size() > 1) {
        TxtBold(TextFormat("%d selected", (int)us.size()), r.x + 12, r.y + 8, 20, Pal::Brass);
        std::map<int, int> cnt; for (auto* u : us) cnt[u->def]++; float x = r.x + 12, y = r.y + 36;
        for (auto& kv : cnt) { std::string s = TextFormat("%d %s", kv.second, UName(Bl().units[kv.first]).c_str()); Txt(s, x, y, 14, WHITE); y += 17; if (y > r.y + r.height - 18) { y = r.y + 36; x += 150; } }
    } else if (S.selB >= 0) {
        const Building* b = w.Bd(S.selB); if (!b) { S.selB = -1; return; } const BuildingDef& d = w.BD(*b);
        TxtBold(d.key == "wonder" && IsPlayer(b->owner) ? Bl().factions[w.players[b->owner].faction].wonder : BName(d.key), r.x + 12, r.y + 8, 20, b->owner == S.me ? Pal::Brass : WHITE); line(Who(b->owner), 32, 14, SCREEN_DIM);
        Bar(r.x + 12, r.y + 54, r.width - 24, 10, b->hp / std::max(1.0f, d.hp), Pal::Good); line(TextFormat("%d / %d HP%s", (int)b->hp, (int)d.hp, b->progress < 1 ? TextFormat("   building %d%%", (int)(b->progress * 100)) : ""), 66, 14, WHITE);
        if (!b->garrison.empty()) line(TextFormat("Garrisoned: %d / %d", (int)b->garrison.size(), d.garrison), 86, 14, WHITE);
        if (b->relics) line(TextFormat("Relics held: %d", b->relics), 104, 14, {120, 240, 230, 255});
        if (b->owner == S.me && !b->queue.empty()) {
            const QueueItem& q = b->queue[0]; float need = q.kind == 0 ? Bl().units[q.def].train : q.kind == 1 ? Bl().techs[q.def].time : Bl().eraTime[q.def];
            std::string what = q.kind == 0 ? UName(Bl().units[q.def]) : q.kind == 1 ? TName(Bl().techs[q.def]) : (q.def == 1 ? "the Steam Era" : "the Leviathan Era");
            line("Making " + what, 122, 14, Pal::Brass); Bar(r.x + 12, r.y + 140, r.width - 24, 8, b->qT / std::max(0.1f, need), Pal::Brass);
            for (size_t i = 0; i < b->queue.size() && i < 8; i++) { Rectangle q2{r.x + 12 + i * 34.0f, r.y + 152, 30, 22}; DrawRectangleRec(q2, {50, 56, 72, 255}); DrawTextCentered(TextFormat("%d", (int)i + 1), q2.x + 15, q2.y + 3, 14, WHITE);
                if (CheckCollisionPointRec(GetMousePosition(), q2) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { Command c = Cmd(C_CANCEL); c.target = b->id; c.a = (int)i; Issue(c); } }
        }
    } else if (S.selSite >= 0 && S.selSite < (int)w.sites.size()) {
        const Site& s = w.sites[S.selSite]; static const char* N[7] = {"Pirate Cove", "Tribal Town", "Volcano Altar", "Sunken Ruin", "The Kraken", "The Ghost Ship", "Wreck"};
        TxtBold(N[std::clamp(s.kind, 0, 6)], r.x + 12, r.y + 8, 20, Pal::Brass);
        if (s.maxHp > 1) { Bar(r.x + 12, r.y + 40, r.width - 24, 10, s.hp / std::max(1.0f, s.maxHp), Pal::Bad); line(TextFormat("%d / %d", (int)s.hp, (int)s.maxHp), 52, 14, WHITE); }
        if (s.kind == S_ALTAR) line(s.holder >= 0 ? "Held by " + Who(s.holder) : "Unclaimed: kill the Sun God, then stand on it 45 s", 74, 14, WHITE);
        if (s.kind == S_RUIN) line(s.relic > 0 ? "A relic lies here (sentinels guard it)" : "Plundered", 74, 14, WHITE);
        if (s.kind == S_COVE) line(TextFormat("Your reputation %d. Ctrl+right-click to storm it.", (int)s.rep[S.me]), 74, 14, WHITE);
        if (s.kind == S_TRIBE) line(s.peace[S.me] == 2 ? "Your ally (Kinship)" : s.peace[S.me] == 1 ? "Truce (tribute)" : "Hostile. Ctrl+right-click to attack it.", 74, 14, WHITE);
    } else if (S.selNode >= 0 && S.selNode < (int)w.nodes.size()) {
        const Node& n = w.nodes[S.selNode]; static const char* N[N_COUNT] = {"Fish school", "Fruit grove", "Kelp bed", "Brass seam", "Wreck", "Coal seam", "Ichor vent", "Pearl bank", "Farm"};
        TxtBold(N[n.kind], r.x + 12, r.y + 8, 20, Pal::Brass); line(TextFormat("%d %s left", (int)n.amount, ResName(Bl().nodes[n.kind].res)), 36, 14, WHITE); line(Bl().nodes[n.kind].water ? "Worked by Fishing Boats" : "Worked by Workers", 56, 14, SCREEN_DIM);
    } else { TxtBold(Bl().factions[Me().faction].name, r.x + 12, r.y + 8, 20, Pal::Brass); line(Bl().factions[Me().faction].mechanic, 34, 13, WHITE); line(Bl().factions[Me().faction].weakness, 54, 13, SCREEN_DIM); line("Drag to select. Right-click to order. F1: help", 84, 13, {200, 210, 230, 255}); }
}
void BuildCard() {
    gCard.clear(); World& w = W(); Player& P = Me(); std::vector<int> mine = MySel();
    auto add = [&](const std::string& l, const std::string& tip, int key, bool on, std::function<void()> f, Color c = {60, 70, 90, 255}) { gCard.push_back({l, tip, key, on, f, c}); };
    bool workers = false, military = false, transports = false, priests = false, traders = false, chapelNear = false;
    for (int id : mine) { const Unit* u = w.U(id); const UnitDef& d = w.UD(*u); if ((d.tags & TG_WORKER) && !(d.tags & TG_SHIP)) workers = true; else if (d.attack > 0) military = true; if (d.carry > 0) transports = true; if (d.special == "convert") priests = true; if (d.key == "trade_ship") traders = true;
        for (const auto& b : w.buildings) if (!b.dead && b.owner == S.me && w.BD(b).key == "chapel" && Vector2Distance(b.Centre(), u->p) < 4) chapelNear = true; }
    if (workers) {
        int eraMax = P.era; int idx = 0;
        for (size_t i = 0; i < Bl().buildings.size(); i++) {
            const BuildingDef& d = Bl().buildings[i]; if (d.key == "harbor" || (d.faction >= 0 && d.faction != P.faction) || (d.key == "farm" && P.faction == 2)) continue;
            bool pageOk = (idx / 12) == S.page; idx++; if (!pageOk) continue;
            Cost cost = d.cost; bool can = d.era <= eraMax && Afford(cost) && !(d.key == "wonder" && P.wonder >= 0);
            std::string nm = d.key == "wonder" ? Bl().factions[P.faction].wonder : BName(d.key);
            add(nm, nm + "  (" + CostStr(cost) + ")" + (d.era > eraMax ? (d.era == 1 ? "  needs the Steam Era" : "  needs the Leviathan Era") : ""), 0, can, [i] { S.place = (int)i; });
        }
        if (idx > 12) add(S.page ? "< Back" : "More >", "More buildings", KEY_TAB, true, [] { S.page = 1 - S.page; });
    }
    if (!mine.empty()) {
        add("Stop", "Stop (S)", KEY_S, true, [mine] { Command c = Cmd(C_STOP); c.units = mine; Issue(c); });
        if (military) { add("Attack-move", "Attack-move (A, then click)", KEY_A, true, [] { S.order = 1; }); add("Patrol", "Patrol (P, then click)", 0, true, [] { S.order = 2; });
            static const char* ST[4] = {"Aggressive", "Defensive", "Hold ground", "Passive"}; for (int s = 0; s < 4; s++) add(ST[s], std::string("Stance: ") + ST[s], 0, true, [mine, s] { Command c = Cmd(C_STANCE); c.units = mine; c.a = s; Issue(c); }); }
        if (transports) add("Unload", "Unload here (U, then click the shore)", KEY_U, true, [] { S.order = 5; });
        if (priests) add("Convert", "Convert an enemy unit (30 Devotion)", KEY_C, P.devotion >= 30, [] { S.order = 6; });
        if (traders) add("Trade route", "Click a rival's Exchange or a pirate cove", KEY_T, true, [] { S.order = 7; });
        if (chapelNear && P.faction == 4) add("Offer", "Offer these units at the Chapel: half their cost in Devotion", 0, true, [mine] { Command c = Cmd(C_OFFER); c.units = mine; Issue(c); });
        for (int id : mine) if (id == P.heroUnit) { const Unit* h = w.U(id); add(Bl().factions[P.faction].heroActive, Bl().factions[P.faction].heroActive + (h->abilityT > 0 ? TextFormat("  (%d s)", (int)h->abilityT) : "  (R)"), KEY_R, h->abilityT <= 0, [P] { if (P.faction == 0 || P.faction == 5) { Command c = Cmd(C_ABILITY); c.units = {Me().heroUnit}; Issue(c); } else S.order = 3; }, {110, 70, 130, 255}); }
    }
    if (S.selB >= 0 && mine.empty()) {
        const Building* b = w.Bd(S.selB);
        if (b && b->owner == S.me && b->progress >= 1) {
            const BuildingDef& bd = w.BD(*b);
            std::vector<int> can;
            for (size_t i = 0; i < Bl().units.size(); i++) { const UnitDef& d = Bl().units[i];
                bool here = std::find(bd.trains.begin(), bd.trains.end(), d.key) != bd.trains.end() || (d.faction == P.faction && d.from == bd.key);
                if (!here || (d.faction >= 0 && d.faction != P.faction) || (P.faction == 2 && (d.key == "torpedo_boat" || d.key == "ironclad")) || (P.faction == 5 && d.key == "medic" && false)) continue;
                if (d.key == "hero" && bd.key != "harbor") continue; can.push_back((int)i); }
            for (int i : can) { const UnitDef& d = Bl().units[i]; Cost c = w.PriceOf(S.me, d); bool ok = d.era <= P.era && Afford(c); std::string nm = d.key == "hero" ? Bl().factions[P.faction].heroName : (P.faction == 5 && d.key == "medic") ? "Mechanic" : UName(d);
                add(nm, nm + "  (" + CostStr(c) + ", " + std::to_string(d.pop) + " pop)" + (d.era > P.era ? "  needs a later era" : ""), 0, ok, [b, i] { Command c = Cmd(C_TRAIN); c.target = b->id; c.def = i; Issue(c); PlayCue("ui.click"); }, {54, 80, 70, 255}); }
            for (size_t i = 0; i < Bl().techs.size(); i++) { const TechDef& t = Bl().techs[i]; if (P.tech.size() <= i || P.tech[i] || t.era > P.era) continue;
                bool here = t.at == bd.key || (t.faction >= 0 && bd.key == "harbor"); if (!here || (t.faction >= 0 && t.faction != P.faction)) continue; if (t.forgeLine >= 0 && P.forge[t.forgeLine] != t.forgeLevel - 1) continue;
                std::string fx; for (auto& f : t.fx) fx += " " + f.first;
                add(TName(t), TName(t) + "  (" + CostStr(t.cost) + ")" + fx, 0, Afford(t.cost), [b, i] { Command c = Cmd(C_RESEARCH); c.target = b->id; c.def = (int)i; Issue(c); }, {70, 60, 100, 255}); }
            if (bd.key == "harbor" && P.era < 2) { Cost c = Bl().eraCost[P.era + 1]; add(P.era == 0 ? "Steam Era" : "Leviathan Era", std::string("Advance (") + CostStr(c) + "): needs two of " + (P.era == 0 ? "Barracks, Stable, Dock, Farmstead, Mine Shed, a second-island Lighthouse" : "Workshop, Siege Works, Exchange, Coastal Battery, Colony Hall, Chapel"), 0, Afford(c) && P.eraBuilding < 0, [b] { Command c2 = Cmd(C_ERA); c2.target = b->id; Issue(c2); }, {120, 90, 40, 255}); }
            if (P.era >= 2) { bool any = P.ultimateUnit >= 0; add(Bl().factions[P.faction].ultimate, std::string("Summon your ultimate (") + (P.faction == 4 ? "150 Ichor, 600 Devotion" : "300 Ichor") + ")" + (P.ultimateCD > 0 ? TextFormat("  ready in %d s", (int)P.ultimateCD) : ""), 0, !any && P.ultimateCD <= 0, [] { Issue(Cmd(C_ULTIMATE)); }, {130, 50, 120, 255}); }
            if (!b->garrison.empty()) add("Ungarrison", "Let everyone out", 0, true, [b] { Command c = Cmd(C_UNGARRISON); c.target = b->id; Issue(c); });
            if (bd.key == "exchange") add("Exchange", "Trade resources and sell for Doubloons", KEY_X, true, [] { S.panel = 4; });
            add("Rally", "Set where new units go (or right-click)", 0, true, [] { S.order = 4; });
            if (b->progress < 1) add("Cancel", "Cancel construction (80% back)", 0, true, [b] { Command c = Cmd(C_CANCEL); c.target = b->id; c.a = -1; Issue(c); });
        }
    }
}
void CommandCard(Rectangle r) {
    Panel(r, {28, 32, 44, 235}); BuildCard();
    const int cols = 4; float bw = (r.width - 20) / cols, bh = 34; std::string tip;
    for (size_t i = 0; i < gCard.size() && i < 20; i++) {
        Btn& b = gCard[i]; Rectangle br{r.x + 10 + (i % cols) * bw, r.y + 10 + (i / cols) * (bh + 4), bw - 4, bh};
        bool hov = CheckCollisionPointRec(GetMousePosition(), br);
        DrawRectangleRounded(br, 0.2f, 4, b.on ? (hov ? Mix2(b.c, WHITE, 0.25f) : b.c) : Color{40, 42, 50, 255}); DrawRectangleRoundedLinesEx(br, 0.2f, 4, 1, b.on ? Pal::BrassDk : Color{60, 60, 66, 255});
        std::string l = b.label; int fs = 13; while (MeasureTxt(l, fs) > bw - 10 && fs > 10) fs--; if (MeasureTxt(l, fs) > bw - 10) l = l.substr(0, 12) + ".";
        DrawTextCentered(l, br.x + br.width / 2, br.y + 10, fs, b.on ? WHITE : Color{130, 130, 140, 255});
        if (hov) tip = b.tip;
        if (b.on && ((hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) || (b.key && !Typing() && IsKeyPressed(b.key) && !IsKeyDown(KEY_LEFT_CONTROL)))) { PlayCue("ui.click"); b.act(); }
    }
    if (!tip.empty()) { int tw = MeasureTxt(tip, 14) + 16; float tx = std::min((float)SCREEN_W - tw - 6, r.x); DrawRectangleRounded({tx, r.y - 30, (float)tw, 24}, 0.3f, 4, {14, 16, 24, 240}); Txt(tip, tx + 8, r.y - 26, 14, WHITE); }
}
// ---------------------------------------------------------------- the panels
bool PanelButton(Rectangle r, const char* t, bool on = true) { return Button(r, t, on, 15); }
void DrawPanels() {
    World& w = W(); Player& P = Me(); if (!S.panel) return;
    Rectangle p{SCREEN_W / 2.0f - 300, 110, 600, 400}; Panel(p, {26, 30, 42, 245});
    if (Button({p.x + p.width - 40, p.y + 8, 30, 26}, "x", true, 15)) { S.panel = 0; return; }
    float y = p.y + 16;
    auto H = [&](const std::string& s) { TxtBold(s, p.x + 20, y, 22, Pal::Brass); y += 34; };
    if (S.panel == 1) {   // diplomacy
        H("Diplomacy");
        static const char* D[5] = {"War", "Ceasefire", "Peace", "Open Harbors", "Allied"};
        for (const auto& q : w.players) { if (q.id == S.me) continue;
            Txt(TextFormat("%s (%s)  score %d  %s", q.name.c_str(), Bl().factions[q.faction].name.c_str(), (int)q.score, q.alive ? D[P.stance[q.id]] : "eliminated"), p.x + 20, y, 15, q.alive ? WHITE : SCREEN_DIM);
            if (q.alive && w.set.diplomacy) for (int s = 0; s < 5; s++) if (PanelButton({p.x + 20 + s * 112.0f, y + 20, 106, 24}, D[s], s != P.stance[q.id])) { Command c = Cmd(C_DIPLO); c.a = q.id; c.b = s; Issue(c); }
            if (q.alive && P.stance[q.id] >= DP_PEACE) for (int r = 0; r < R_COUNT; r++) if (PanelButton({p.x + 20 + r * 112.0f, y + 48, 106, 22}, TextFormat("Send 100 %s", ResName(r)), P.res[r] >= 100)) { Command c = Cmd(C_TRIBUTE); c.a = q.id; c.b = r; c.amount = 100; Issue(c); }
            y += w.set.diplomacy ? 80 : 26; }
        if (!w.set.diplomacy) Txt("Diplomacy is locked in this game (teams are fixed).", p.x + 20, y + 6, 15, SCREEN_DIM);
    }
    if (S.panel == 2 && S.panelSite >= 0 && S.panelSite < (int)w.sites.size()) {   // a pirate cove
        const Site& s = w.sites[S.panelSite]; H(s.owner == S.me ? "Your Pirate Cove" : "Pirate Cove");
        Txt(TextFormat("Reputation %d   Doubloons %d", (int)s.rep[S.me], (int)P.res[R_DOUB]), p.x + 20, y, 15, WHITE); y += 26;
        for (int d = 0; d < (int)Bl().deals.size(); d++) { const auto& dl = Bl().deals[d];
            Txt(TextFormat("%s (%d Doubloons, %s Era): attack a rival for %d s", dl.key == "cutter" ? "Cutter Raid" : dl.key == "squadron" ? "Pirate Squadron" : "Black Flag Armada", (int)dl.price, dl.era == 0 ? "Sail" : dl.era == 1 ? "Steam" : "Leviathan", (int)dl.time), p.x + 20, y, 14, WHITE); y += 18;
            float x = p.x + 20; for (const auto& q : w.players) if (q.id != S.me && q.alive && !w.Allied(q.id, S.me)) { if (PanelButton({x, y, 120, 22}, q.name.c_str(), P.era >= dl.era)) { Command c = Cmd(C_HIRE); c.a = S.panelSite; c.b = d; c.target = q.id; Issue(c); } x += 126; } y += 28; }
        Txt("Black market:", p.x + 20, y, 15, Pal::Brass); y += 22;
        for (int r = 0; r < 3; r++) { if (PanelButton({p.x + 20 + r * 186.0f, y, 180, 22}, TextFormat("Sell 100 %s (+20 D)", ResName(r)), P.res[r] >= 100)) { Command c = Cmd(C_HIRE); c.a = S.panelSite; c.b = 4; c.x = r; Issue(c); } }
        y += 26; for (int r = 0; r < 4; r++) { if (PanelButton({p.x + 20 + r * 140.0f, y, 134, 22}, TextFormat("Buy %s %s", r == R_ICHOR ? "40" : "100", ResName(r)), P.res[R_DOUB] >= (r == R_ICHOR ? 100 : 60))) { Command c = Cmd(C_HIRE); c.a = S.panelSite; c.b = 5; c.x = r; Issue(c); } }
        y += 30; for (int q = 0; q < (int)w.players.size(); q++) if (q != S.me && PanelButton({p.x + 20 + q * 96.0f, y, 90, 22}, TextFormat("Intel: %s", w.players[q].name.substr(0, 6).c_str()), P.res[R_DOUB] >= 100)) { Command c = Cmd(C_HIRE); c.a = S.panelSite; c.b = 3; c.target = q; Issue(c); }
        y += 32;
        for (size_t i = 0; i < w.contracts.size(); i++) { const Contract& k = w.contracts[i]; if (k.live || k.warnT <= w.t) continue;
            if (k.target == S.me) { Txt(TextFormat("Pirates hired against you arrive in %d s:", (int)(k.warnT - w.t)), p.x + 20, y, 15, Pal::Bad); if (PanelButton({p.x + 330, y - 2, 120, 22}, TextFormat("Call off %d", (int)(k.price * 1.25f)), P.res[R_DOUB] >= k.price * 1.25f)) { Command c = Cmd(C_BID); c.a = (int)i; c.b = 0; Issue(c); } if (PanelButton({p.x + 456, y - 2, 120, 22}, TextFormat("Send back %d", (int)(k.price * 1.5f)), P.res[R_DOUB] >= k.price * 1.5f)) { Command c = Cmd(C_BID); c.a = (int)i; c.b = 1; Issue(c); } y += 26; }
            if (k.hirer == S.me && k.bids > 0) { if (PanelButton({p.x + 20, y, 200, 22}, TextFormat("Top their bid: %d", (int)(k.price * 1.25f)), P.res[R_DOUB] >= k.price * 1.25f)) { Command c = Cmd(C_BID); c.a = (int)i; c.b = 2; Issue(c); } y += 26; } }
    }
    if (S.panel == 3 && S.panelSite >= 0 && S.panelSite < (int)w.sites.size()) {   // a tribal town
        const Site& s = w.sites[S.panelSite]; H("Tribal Town");
        Txt(s.peace[S.me] == 2 ? "They are your kin: a warrior every minute, up to six." : s.peace[S.me] == 1 ? "Truce: they won't raid you while the tribute is paid." : "They raid you from minute 6. Conquer, pay tribute, or (Islanders) make Kinship.", p.x + 20, y, 15, WHITE); y += 30;
        if (s.peace[S.me] == 0 && PanelButton({p.x + 20, y, 260, 28}, "Pay tribute (200 Food, 100 Brass)", P.res[R_FOOD] >= 200 && P.res[R_BRASS] >= 100)) { Command c = Cmd(C_TRIBE); c.a = S.panelSite; c.b = 0; Issue(c); }
        if (s.peace[S.me] == 1) { if (PanelButton({p.x + 20, y, 260, 28}, "Buy a Tribal Warrior (50 Food)", P.res[R_FOOD] >= 50)) { Command c = Cmd(C_TRIBE); c.a = S.panelSite; c.b = 1; Issue(c); } if (PanelButton({p.x + 290, y, 200, 28}, "Stop paying")) { Command c = Cmd(C_TRIBE); c.a = S.panelSite; c.b = 3; Issue(c); } }
        y += 40;
        if (P.faction == 1 && s.peace[S.me] != 2) {
            int sh = -1; for (int id : MySel()) if (const Unit* u = w.U(id)) if (w.UD(*u).special == "kinship") sh = id;
            if (PanelButton({p.x + 20, y, 400, 28}, sh >= 0 ? "Kinship ritual with the selected Shaman (150 Food, 100 Doubloons)" : "Select a Shaman for the Kinship ritual", sh >= 0 && P.res[R_FOOD] >= 150 && P.res[R_DOUB] >= 100)) { Command c = Cmd(C_TRIBE); c.a = S.panelSite; c.b = 2; c.units = {sh}; Issue(c); S.panel = 0; }
        }
    }
    if (S.panel == 4) {   // the Exchange
        H("The Exchange");
        Txt("Trade 100 at a time; each trade moves the price 3%, and prices drift back.", p.x + 20, y, 14, WHITE); y += 26;
        for (int a = 0; a < 3; a++) { for (int b = 0; b < 3; b++) if (a != b) { float got = Bl().exBaseOut / std::max(0.2f, P.exchange[b]) * P.exchange[a];
                if (PanelButton({p.x + 20 + (b - (b > a)) * 280.0f, y, 270, 26}, TextFormat("100 %s -> %d %s", ResName(a), (int)got, ResName(b)), P.res[a] >= 100)) { Command c = Cmd(C_EXCHANGE); c.a = a; c.b = b; Issue(c); } }
            y += 32; }
        for (int a = 0; a < 3; a++) if (PanelButton({p.x + 20 + a * 186.0f, y, 180, 26}, TextFormat("100 %s -> 15 D", ResName(a)), P.res[a] >= 100)) { Command c = Cmd(C_EXCHANGE); c.a = a; c.b = 4; Issue(c); }
    }
    if (S.panel == 6) {   // Nemo's Logbook
        H("Nemo's Logbook: keep one card");
        if (P.cardOffer.empty()) { Txt("No cards on offer (a new hand comes with each era).", p.x + 20, y, 15, WHITE); for (int c : P.cards) { y += 24; Txt("Kept: " + Bl().factions[0].cards[c], p.x + 20, y, 15, Pal::Brass); } }
        static const char* DESC[8] = {"Ships +10% damage", "Reveal every ruin and wreck", "Submarines +25% HP", "Ichor gathering +30%", "Ships +15% speed", "Diving Marines +2 armor", "Lighthouses cost 30% less", "Coal upkeep -25%"};
        for (size_t i = 0; i < P.cardOffer.size(); i++) { int c = P.cardOffer[i]; Rectangle cr{p.x + 20 + i * 190.0f, y, 180, 220}; DrawRectangleRounded(cr, 0.08f, 6, {236, 226, 196, 255}); DrawRectangleRoundedLinesEx(cr, 0.08f, 6, 2, Pal::BrassDk);
            DrawTextCenteredBold(Bl().factions[0].cards[c], cr.x + 90, cr.y + 30, 16, Pal::Ink); DrawTextCentered(DESC[c % 8], cr.x + 90, cr.y + 110, 14, Pal::Ink);
            if (PanelButton({cr.x + 30, cr.y + 176, 120, 30}, "Keep")) { Command cm = Cmd(C_CARD); cm.a = (int)i; Issue(cm); S.panel = 0; } }
    }
    if (S.panel == 5) {   // help
        H("How to play Fathoms");
        static const char* L[] = {"Grow a colony from your Harbor: workers gather Food, Brass and Coal; Fishing Boats fish.", "Build Cottages for population, Farmsteads and Mine Sheds as drop sites near resources.",
            "Advance at the Harbor: Steam (two of Barracks/Stable/Dock/Farmstead/Mine Shed), then Leviathan.", "Claim islands with a Lighthouse: carry workers over in a Transport (board, then Unload).",
            "Left-drag selects; right-click moves, attacks, gathers, garrisons, boards. A attack-move, S stop.", "Ctrl+1-9 sets a group, 1-9 recalls it. Space jumps to the last alert. Tab: next idle worker.",
            "Pirate coves and tribal towns: click them. Ctrl+right-click attacks them. Relics go to a Chapel or Harbor.", "Win by conquest, by holding every relic for 3 minutes, by holding the volcano altar 4 minutes, or on score.",
            "F2 diplomacy, F3 Logbook (Nautilus), Esc the game menu."};
        for (auto* l : L) { Txt(l, p.x + 20, y, 15, WHITE); y += 28; }
    }
}
void DrawHud() {
    World& w = W();
    // health bars over the hurt and the selected
    for (const auto& u : w.units) { if (!Visible(u)) continue; float mx = MaxHp(w, u); bool sel = std::find(S.sel.begin(), S.sel.end(), u.id) != S.sel.end(); if (!sel && u.hp >= mx - 0.5f) continue;
        Vector2 s = Scr(Vector3Add(UnitPos(u), {0, UnitH(u) + 0.3f, 0})); if (s.x < 0 || s.y < 30 || s.x > SCREEN_W || s.y > SCREEN_H) continue; float bw = (w.UD(u).tags & (TG_SHIP | TG_ULTIMATE)) ? 44.0f : 26.0f;
        Bar(s.x - bw / 2, s.y, bw, 4, u.hp / std::max(1.0f, mx), u.owner == S.me ? Pal::Good : w.Allied(u.owner, S.me) ? Pal::Teal : Pal::Bad); if (u.chan > 0) Bar(s.x - bw / 2, s.y + 5, bw, 3, u.chan / 45.0f, {220, 200, 120, 255}); }
    for (const auto& b : w.buildings) { if (b.dead || !Known(b)) continue; float mx = w.BD(b).hp; if (b.id != S.selB && b.hp >= mx - 0.5f && b.progress >= 1) continue; Vector2 s = Scr(Vector3Add(W3(b.Centre()), {0, 2.4f, 0})); if (s.y < 30) continue; Bar(s.x - 30, s.y, 60, 5, b.hp / mx, b.owner == S.me ? Pal::Good : Pal::Bad); if (b.progress < 1) Bar(s.x - 30, s.y + 6, 60, 4, b.progress, Pal::Brass); }
    for (const auto& s2 : w.sites) if ((s2.kind == S_COVE || s2.kind == S_TRIBE) && s2.hp > 0 && s2.hp < s2.maxHp && ExploredAt(s2.p)) { Vector2 s = Scr(Vector3Add(W3(s2.p), {0, 4.5f, 0})); Bar(s.x - 40, s.y, 80, 6, s2.hp / s2.maxHp, Pal::Bad); }
    // floating messages near the camera
    for (auto& m : S.msgs) if (m.at.x >= 0 && m.t < 3) { Vector2 s = Scr(Vector3Add(W3(m.at), {0, 3 + m.t, 0})); if (s.y > 30 && s.y < SCREEN_H - 220) DrawTextCenteredBold(m.text, s.x, s.y, 15, FadeC(m.c, 1 - m.t / 3)); }
    TopBar();
    // the message log, top left
    float y = 40; for (auto& m : S.msgs) { if (m.t > 12) continue; TxtBold(m.text, 14, y, 15, FadeC(m.c, std::min(1.0f, (12 - m.t) / 2))); y += 19; }
    // the drag box
    if (S.dragging) { Vector2 b = GetMousePosition(); Rectangle r{std::min(S.dragA.x, b.x), std::min(S.dragA.y, b.y), fabsf(S.dragA.x - b.x), fabsf(S.dragA.y - b.y)}; DrawRectangleRec(r, {120, 255, 140, 30}); DrawRectangleLinesEx(r, 1, {120, 255, 140, 200}); }
    DrawMini();
    SelectionPanel({210, SCREEN_H - 184.0f, 420, 176});
    CommandCard({SCREEN_W - 448.0f, SCREEN_H - 184.0f, 440, 176});
    // panel buttons
    if (Button({SCREEN_W - 120.0f, 36, 110, 24}, "Diplomacy", true, 13)) S.panel = S.panel == 1 ? 0 : 1;
    if (Me().faction == 0 && Button({SCREEN_W - 120.0f, 64, 110, 24}, "Logbook", true, 13)) S.panel = S.panel == 6 ? 0 : 6;
    if (Button({SCREEN_W - 120.0f, Me().faction == 0 ? 92.0f : 64.0f, 110, 24}, "Help", true, 13)) S.panel = S.panel == 5 ? 0 : 5;
    if (S.order) DrawTextCenteredBold(S.order == 1 ? "Attack-move: click a place" : S.order == 2 ? "Patrol: click a place" : S.order == 3 ? "Ability: click a target" : S.order == 4 ? "Rally: click a place" : S.order == 5 ? "Unload: click the shore" : S.order == 6 ? "Convert: click an enemy" : "Trade: click an Exchange or a cove", SCREEN_W / 2.0f, SCREEN_H - 210, 18, Pal::Brass);
    if (S.place >= 0) DrawTextCenteredBold("Place " + BName(Bl().buildings[S.place].key) + ": left-click (Shift: more), right-click cancels", SCREEN_W / 2.0f, SCREEN_H - 210, 18, Pal::Brass);
    if (S.toastT > 0) DrawTextCenteredBold(S.toast, SCREEN_W / 2.0f, SCREEN_H - 236, 17, WHITE);
    DrawPanels();
    if (S.help && S.helpT < 14) { Rectangle r{SCREEN_W / 2.0f - 330, 60, 660, 70}; DrawRectangleRounded(r, 0.2f, 6, {14, 18, 28, 220}); DrawTextCenteredBold("Fathoms: build an island empire", SCREEN_W / 2.0f, r.y + 10, 20, Pal::Brass); DrawTextCentered("Drag-select workers, right-click resources. Build Cottages and a Barracks. F1 for help.", SCREEN_W / 2.0f, r.y + 40, 15, WHITE); }
    // the end
    if (w.over) {
        bool won = std::find(w.winners.begin(), w.winners.end(), S.me) != w.winners.end();
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, {0, 0, 0, 140});
        DrawTextCenteredBold(won ? "VICTORY" : "DEFEAT", SCREEN_W / 2.0f, 180, 64, won ? Pal::Brass : Pal::Bad);
        float yy = 280; std::vector<const Player*> ps; for (auto& q : w.players) ps.push_back(&q); std::sort(ps.begin(), ps.end(), [](const Player* a, const Player* b) { return a->score > b->score; });
        for (auto* q : ps) { DrawTextCentered(TextFormat("%s  (%s)  score %d   kills %d   lost %d   %s", q->name.c_str(), Bl().factions[q->faction].name.c_str(), (int)q->score, q->kills, q->losses, std::find(w.winners.begin(), w.winners.end(), q->id) != w.winners.end() ? "WINNER" : ""), SCREEN_W / 2.0f, yy, 18, PlayerColor(q->color)); yy += 26; }
    }
}

// ---------------------------------------------------------------- input
void HandleInput(float dt) {
    World& w = W(); (void)dt;
    if (Typing()) return;
    Vector2 m = GetMousePosition();
    bool overUi = m.y > SCREEN_H - 190 || m.y < 32 || (S.panel && CheckCollisionPointRec(m, {SCREEN_W / 2.0f - 300, 110, 600, 400})) || (m.x > SCREEN_W - 124 && m.y < 120);
    if (IsKeyPressed(KEY_F1)) S.panel = S.panel == 5 ? 0 : 5;
    if (IsKeyPressed(KEY_F2)) S.panel = S.panel == 1 ? 0 : 1;
    if (IsKeyPressed(KEY_F3) && Me().faction == 0) S.panel = S.panel == 6 ? 0 : 6;
    if (IsKeyPressed(KEY_ESCAPE)) { if (S.place >= 0 || S.order) { S.place = -1; S.order = 0; } else if (S.panel) S.panel = 0; else GameMenuRequest(); }
    if (IsKeyPressed(KEY_SPACE) && S.lastAttackT > -50) S.camC = S.lastAttackAt;
    if (IsKeyPressed(KEY_TAB) && S.sel.empty() && S.selB < 0) { static size_t cyc = 0; std::vector<int> idle; for (const auto& u : w.units) if (u.owner == S.me && (w.UD(u).tags & TG_WORKER) && u.order == O_IDLE && u.inside < 0) idle.push_back(u.id); if (!idle.empty()) { int id = idle[cyc++ % idle.size()]; S.sel = {id}; S.camC = w.U(id)->p; } }
    if (IsKeyPressed(KEY_H)) { for (const auto& b : w.buildings) if (b.owner == S.me && w.BD(b).key == "harbor") { S.camC = b.Centre(); S.selB = b.id; S.sel.clear(); break; } }
    for (int k = 0; k <= 9; k++) if (IsKeyPressed(KEY_ZERO + k)) { if (IsKeyDown(KEY_LEFT_CONTROL)) S.groups[k] = MySel(); else if (!S.groups[k].empty()) { std::vector<int> g; for (int id : S.groups[k]) if (w.U(id)) g.push_back(id); S.groups[k] = g; static double last = 0; static int lastK = -1; if (GetTime() - last < 0.35 && lastK == k && !g.empty()) S.camC = w.U(g[0])->p; last = GetTime(); lastK = k; S.sel = g; S.selB = -1; } }
    if (IsKeyPressed(KEY_DELETE)) { Command c = Cmd(C_DELETE); c.units = MySel(); Issue(c); }
    if (!overUi && !CheckCollisionPointRec(m, MINI)) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { if (S.place >= 0 || S.order) LeftClick(m, IsKeyDown(KEY_LEFT_SHIFT)); else { S.dragging = true; S.dragA = m; } }
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { if (S.place >= 0 || S.order) { S.place = -1; S.order = 0; } else RightClick(m); }
    }
    if (S.dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { S.dragging = false; if (Vector2Distance(S.dragA, m) < 6) LeftClick(m, IsKeyDown(KEY_LEFT_SHIFT)); else SelectBox(S.dragA, m, IsKeyDown(KEY_LEFT_SHIFT)); }
}

// ---------------------------------------------------------------- solo, network, the scene
std::vector<Command> gAiCmds;
void StepSolo(float dt) {
    World& w = W(); S.acc += std::min(dt, 0.25f);
    while (S.acc >= STEP) { for (auto& p : w.players) if (p.ai && p.alive && !w.over) { gAiCmds.clear(); AiThink(w, p.id, gAiCmds); for (auto& c : gAiCmds) w.Apply(c); } w.Step(); S.acc -= STEP; }
}
bool NetFrame(Game& g, float dt) {
    arcade::Session& N = *S.net; N.Update(GetTime(), dt);
    if (N.stage != arcade::S_PLAYING) { S.net = nullptr; S.hostW = nullptr; S.active = false; g.scene = Scene::Arcade; return false; }
    int seat = N.MyPlayer();
    if (N.role == arcade::R_HOST) { S.hostW = FathomsHostWorld(N.HostGame()); S.me = FathomsSeatPlayer(N.HostGame(), seat); if (S.me < 0) S.me = 0; }
    else if (N.stateVersion != S.seenVersion && !N.Snapshot().empty()) { S.seenVersion = N.stateVersion; Reader r(N.Snapshot()); ReadWorld(r, S.mirror, &S.evTotal); S.me = std::max(0, seat); }
    if (W().players.empty()) { ClearBackground({16, 30, 44, 255}); DrawTextCenteredBold("Charting the archipelago...", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 12, 24, WHITE); return false; }
    if (!S.helloSent) { Writer o; o.U8(1); o.Str(S.netName); N.Act(o); S.helloSent = true; for (const auto& b : W().buildings) if (b.owner == S.me && W().BD(b).key == "harbor") S.camC = b.Centre(); S.evSeen = W().evCount; }
    return true;
}
void FathomsSound(float dt);
}  // namespace

// ---------------------------------------------------------------- entry points
void StartFathoms(Game& g, const fa::Settings& s) {
    S = FScene{}; S.active = true; S.solo = s;
    S.local.Init(s); S.me = 0;
    for (const auto& b : S.local.buildings) if (b.owner == 0 && S.local.BD(b).key == "harbor") S.camC = b.Centre();
    S.evSeen = S.local.evCount; g.scene = Scene::Fathoms;
}
void StartFathomsNet(Game& g, arcade::Session* net, const char* name) { S = FScene{}; S.active = true; S.net = net; S.netName = name ? name : "Player"; g.scene = Scene::Fathoms; }
void LeaveFathoms(Game& g) {
    if (S.net) { if (S.net->role == arcade::R_HOST) S.net->BackToLobby(); else S.net->Leave(); S.net = nullptr; }
    S.hostW = nullptr; S.active = false; g.scene = Scene::Arcade;
}
bool FathomsActive() { return S.active; }
void SceneFathoms(Game& g) {
    if (!S.active) { fa::Settings s; StartFathoms(g, s); }
    float dt = S.shot ? 1 / 30.0f : std::min(GetFrameTime(), 0.1f); S.t += dt; S.helpT += dt;
    if (S.net) { if (!NetFrame(g, dt)) return; }
    else if (!S.shot && !W().over) StepSolo(dt);
    for (auto& m : S.msgs) m.t += dt; for (auto& mk : S.marks) mk.t += dt; S.marks.erase(std::remove_if(S.marks.begin(), S.marks.end(), [](const Mark& k) { return k.t > 0.5f; }), S.marks.end());
    S.toastT -= dt;
    // prune the selection of the dead and the hidden
    { std::vector<int> keep; for (int id : S.sel) if (const Unit* u = W().U(id)) if (Visible(*u)) keep.push_back(id); S.sel = keep; if (S.selB >= 0 && !W().Bd(S.selB)) S.selB = -1; }
    ReadEvents();
    StepCam(dt);
    if (!S.shot) HandleInput(dt);
    Render(dt);
    DrawHud();
    FathomsSound(dt);
}

// ---------------------------------------------------------------- shots (--shots shots fa_)
void DebugFathomsShot(Game& g, int which) {
    fa::Settings s; s.players = which == 3 ? 4 : 2; s.seed = 31 + which; s.faction[0] = which % 6; s.faction[1] = (which + 3) % 6; for (int k = 0; k < 6; k++) s.ai[k] = true; s.timeCap = 0;
    StartFathoms(g, s); S.shot = true; S.help = false;
    World& w = S.local; std::vector<Command> cm;
    auto run = [&](float secs) { for (int i = 0; i < (int)(secs / STEP) && !w.over; i++) { for (auto& p : w.players) if (p.ai && p.alive) { cm.clear(); AiThink(w, p.id, cm); for (auto& c : cm) w.Apply(c); } w.Step(); } };
    if (which == 0) { run(4); }
    if (which == 1) { run(9 * 60); }
    if (which == 2) { run(16 * 60); for (const auto& u : w.units) if (u.owner == 0 && !(w.UD(u).tags & TG_WORKER) && w.UD(u).attack > 0) { S.camC = u.p; break; } }
    if (which == 3) { run(12 * 60); }
    if (which == 4) { run(30); S.camHWant = S.camH = 14; for (const auto& b : w.buildings) if (b.owner == 0 && w.BD(b).key == "harbor") S.camC = Vector2Add(b.Centre(), {2, 2}); }
    if (which == 5) { run(60); for (auto& p : w.players) p.explored.assign(w.W * w.H, 1); S.camHWant = S.camH = 80; S.camC = {w.W * 0.5f, w.H * 0.5f}; }
    if (which == 6) { run(11 * 60); for (const auto& st : w.sites) if (st.kind == S_ALTAR) { S.camC = st.p; break; } for (auto& p : w.players) p.explored.assign(w.W * w.H, 1); }
    if (which == 7) {   // a showcase of the units: every shared and unique unit lined up on the home island
        run(1); Vector2 at{}; for (const auto& b : w.buildings) if (b.owner == 0 && w.BD(b).key == "harbor") at = Vector2Add(b.Centre(), {0, 0});
        int k = 0; for (size_t i = 0; i < Bl().units.size(); i++) { const UnitDef& d = Bl().units[i]; if (d.move == MV_SEA || d.key == "ultimate" || d.key == "kraken") continue; Vector2 p{}; bool found = false; for (int tries = 0; tries < 400 && !found; tries++) { p = {at.x - 12 + (float)((k * 7 + tries) % 25), at.y - 12 + (float)(((k * 7 + tries) / 25) % 25)}; found = w.Land(w.TileAt(p)) && w.occ[w.Idx((int)p.x, (int)p.y)] < 0; bool clash = false; for (const auto& o : w.units) if (Vector2Distance(o.p, p) < 1.6f) clash = true; if (clash) found = false; } if (!found) continue; int id = w.SpawnUnit(0, (int)i, p); if (Unit* u = w.U(id)) u->facing = PI / 2; k++; }
        S.camC = {at.x, at.y - 9}; S.camHWant = S.camH = 16;
    }
    if (which == 8) {   // the navy
        run(1); Vector2 at{}; for (const auto& b : w.buildings) if (b.owner == 0 && w.BD(b).key == "harbor") at = b.Centre(); Vector2 sea = at; for (int r = 4; r < 20; r++) { Vector2 q = Vector2Add(at, Vector2Scale(Vector2Normalize(Vector2Subtract({w.W * 0.5f, w.H * 0.5f}, at)), (float)r)); if (w.TileAt(q) == T_DEEP) { sea = q; break; } }
        int k = 0; for (size_t i = 0; i < Bl().units.size(); i++) { const UnitDef& d = Bl().units[i]; if (d.move != MV_SEA || d.key == "kraken") continue; int id = w.SpawnUnit(0, (int)i, {sea.x - 6 + (k % 5) * 3.0f, sea.y + (k / 5) * 2.5f}); if (Unit* u = w.U(id)) u->facing = 0; k++; }
        S.camC = Vector2Add(sea, {0, 2}); S.camHWant = S.camH = 20;
    }
    if (which == 9) { run(1); for (auto& p : w.players) { p.res = {5000, 5000, 5000, 5000, 500}; p.era = 2; } int k = 0; Vector2 at{}; for (const auto& b : w.buildings) if (b.owner == 0 && w.BD(b).key == "harbor") at = b.Centre();
        for (size_t i = 0; i < Bl().buildings.size(); i++) { const BuildingDef& d = Bl().buildings[i]; if (d.key == "harbor" || (d.faction >= 0 && d.faction != w.players[0].faction)) continue; for (int r = 0; r < 30; r++) { int x = (int)at.x - 12 + (k % 6) * 4 + (r % 5) - 2, y = (int)at.y - 12 + (k / 6) * 4 + r / 5; if (w.CanPlace(0, (int)i, x, y)) { w.PlaceBuilding(0, (int)i, x, y, true); break; } } k++; }
        S.camC = {at.x - 2, at.y - 4}; S.camHWant = S.camH = 30; }
    S.evSeen = w.evCount; ReadEvents();
    if (which == 2 || which == 3) { S.sel.clear(); for (const auto& u : w.units) if (u.owner == 0 && w.UD(u).attack > 0 && !(w.UD(u).tags & TG_WORKER) && S.sel.size() < 12) S.sel.push_back(u.id); }
    for (int i = 0; i < 3; i++) StepCam(1 / 30.0f);
}

// ---------------------------------------------------------------- sound (sound_fathoms.inl renders it)
namespace {
void FathomsSound(float dt) {
    (void)dt; World& w = W(); FaAudio a; a.on = true; a.era = Me().era; a.faction = Me().faction;
    int near = 0, foes = 0; for (const auto& u : w.units) { if (!Visible(u)) continue; float d = Vector2Distance(u.p, S.camC); if (d < S.camH) { near++; if (w.Enemies(S.me, u.owner) && w.UD(u).attack > 0) foes++; } }
    a.battle = std::clamp(foes / 10.0f, 0.0f, 1.0f); a.busy = std::clamp(near / 40.0f, 0.0f, 1.0f); a.storm = w.weather == 2 ? 1.0f : 0.0f; a.zoom = std::clamp((S.camH - 12) / 68, 0.0f, 1.0f);
    a.over = w.over ? (std::find(w.winners.begin(), w.winners.end(), S.me) != w.winners.end() ? 1 : 2) : 0;
    int lava = 0; for (const auto& s : w.sites) if (s.kind == S_ALTAR && s.count == 2 && Vector2Distance(s.p, S.camC) < 40) lava = 1; a.lava = (float)lava;
    AudioFathoms(a);
}
}
