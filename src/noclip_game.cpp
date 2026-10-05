// NOCLIP's scene (Scene::Noclip): the Surface between days, first person in the Backrooms, the Labs' panels, the field
// map, the HUD on the wrist, the camcorder's overlay, this player's hallucinations, and proximity voice. The rules are
// noclip.cpp's; everything you do is an nc::Input or an nc::Command, solo or networked.
#include "game.h"
#include "input.h"
#include "noclip.h"
#include "noclip_render.h"
#include "noclip_net.h"
#include "arcade_session.h"
#include "sound.h"
#include "voice.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <string>
#include <vector>

using namespace nc;

namespace {
struct Sub { std::string text; float t; Color c; };
struct NcScene {
    bool active = false, shot = false, help = true;
    World Wm; World* hostW = nullptr; arcade::Session* net = nullptr; uint32_t evSeen = 0; int seenVersion = -1; bool helloSent = false; std::string netName;
    int me = 0, bots = 3; std::vector<uint32_t> botRng;
    float camYaw = 0, camPitch = 0, t = 0, acc = 0, bob = 0, blackout = 0, hurtFlash = 0;
    int panel = -1;            // -1 none, 0 the portal desk, 1 the commissary, 2 the monitors
    bool map = false; int surfTab = 0;
    size_t evCursor = 0; std::deque<Sub> subs;
    std::vector<Entity> fakes; float fakeT = 0; float valueLie = 1;   // the hallucinations (this client only)
    Camera3D cam{};
    std::vector<Command> outbox;
    int tokens = 0; bool profLoaded = false; int lastDay = 0; int lastWeek = 1;
} S;
World& W() { return S.hostW ? *S.hostW : S.Wm; }
Player& Me() { return W().crew[std::clamp(S.me, 0, std::max(0, (int)W().crew.size() - 1))]; }
void Send(Command c) { if (S.net) S.outbox.push_back(c); else Me().cmds.push_back(c); }
Color Mx(Color a, Color b, float k) { k = std::clamp(k, 0.0f, 1.0f); return {(unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k), (unsigned char)(a.b + (b.b - a.b) * k), 255}; }
const Color INK{235, 232, 220, 255}, DIM{170, 166, 150, 255}, WARN{255, 160, 110, 255}, OK{150, 230, 170, 255};
void Say(const std::string& s, Color c = INK) { S.subs.push_back({s, S.t, c}); while (S.subs.size() > 6) S.subs.pop_front(); }

// the profile: arcade tokens (10 a day survived, 50 a quota, 25 a new entity photographed)
struct NcProfile { int career = 0; std::vector<std::string> owned; std::string eq[5]; } gNcProf;
void LoadTokens() { if (S.profLoaded) return; S.profLoaded = true; std::ifstream f("noclip_profile.txt"); std::string k; while (f >> k) { if (k == "tokens") f >> S.tokens; else if (k == "career") f >> gNcProf.career; else if (k == "own") { std::string s; f >> s; gNcProf.owned.push_back(s); } else if (k == "eq") { int slot; std::string s; f >> slot >> s; if (slot >= 0 && slot < 5) gNcProf.eq[slot] = s; } } }
void SaveTokens() { std::ofstream f("noclip_profile.txt"); f << "tokens " << S.tokens << "\ncareer " << gNcProf.career << "\n"; for (auto& s : gNcProf.owned) f << "own " << s << "\n"; for (int k = 0; k < 5; k++) if (!gNcProf.eq[k].empty()) f << "eq " << k << " " << gNcProf.eq[k] << "\n"; }
int CosIdx(const std::string& id) { for (int i = 0; i < (int)D().cosmetics.size(); i++) if (D().cosmetics[i].id == id) return i; return -1; }
int SlotOf(const std::string& kind) { return kind == "hat" ? 0 : kind == "vest" ? 1 : kind == "lamp" ? 2 : kind == "suit" ? 3 : 4; }
std::string RankName() { std::string r = "Intern"; for (const auto& x : D().ranks) if (gNcProf.career >= x.second) r = x.first; return r; }
int RankIdx() { int r = 0; for (int i = 0; i < (int)D().ranks.size(); i++) if (gNcProf.career >= D().ranks[i].second) r = i; return r; }

// ---------------------------------------------------------------- events: subtitles for the tells, the feed, sound
void ReadEvents() {
    World& w = W(); uint32_t fresh = w.evCount - (uint32_t)S.evCursor; S.evCursor = w.evCount;
    for (size_t k = w.events.size() - std::min<size_t>(fresh, w.events.size()); k < w.events.size(); k++) {
        const Event& e = w.events[k]; bool mine = e.who == S.me; const Player& m = Me();
        auto near = [&]() { return e.level == m.level && Vector3Distance(e.at, m.p) < 30; };
        switch (e.kind) {
            case E_TELL: if (mine || near()) { if (e.a == 5) Say("[" + e.s + "'s voice, from somewhere close]", WARN); else if (e.a == 0) Say("[" + e.s + "]", DIM); else Say(e.s, INK); if (!S.shot) NoclipCue(e.a == 0 ? NCC_TELL : NCC_CLICK, 0.6f, 0); } break;
            case E_HOWL: if (near()) { Say("[a howl]", WARN); if (!S.shot) NoclipCue(NCC_HOWL, 0.8f, 0); } break;
            case E_HURT: if (mine) { S.hurtFlash = 0.6f; if (!S.shot) NoclipCue(NCC_HURT, 0.8f, 0); } break;
            case E_DOWNED: Say(w.crew[e.who].name + " is down (" + e.s + ")", WARN); break;
            case E_DIED: Say(w.crew[e.who].name + " died: " + e.s, WARN); if (!S.shot) NoclipCue(NCC_DEATH, mine ? 1.0f : 0.5f, 0); break;
            case E_TAKEN: Say(mine ? "You were taken (" + e.s + ")" : w.crew[e.who].name + " is gone", WARN); break;
            case E_REVIVED: Say(w.crew[e.who].name + " is back up", OK); break;
            case E_NOCLIP: if (mine) { S.blackout = 2; Say("You fell through. Level " + std::to_string(e.level) + ": " + D().levels[e.level].name, INK); if (!S.shot) NoclipCue(NCC_NOCLIP, 1, 0); } break;
            case E_EXIT: if (mine) { Say("Level " + std::to_string(e.level) + ": " + D().levels[e.level].name, INK); if (!S.shot) NoclipCue(NCC_DOOR, 0.8f, 0); } break;
            case E_LAB_ONLINE: Say("Lab " + e.s + " is online: on the network for the rest of the campaign", OK); break;
            case E_PORTAL_START: Say("The portal is charging (30 s). Everything on the level can hear it.", WARN); break;
            case E_PORTAL_OPEN: Say("The portal is open: into the hall!", OK); if (!S.shot) NoclipCue(NCC_PORTAL, 1, 0); break;
            case E_JUMP: if (mine) Say("Jumped to " + D().levels[e.level].name, OK); break;
            case E_PHOTO: if (mine) Say(e.a > 0 ? "Photographed: " + D().entities[e.by].name + " (first: +" + std::to_string((int)e.a) + " credit)" : "Photographed: " + D().entities[e.by].name, OK); if (mine && e.a > 0) { S.tokens += D().tokEntity; SaveTokens(); } break;
            case E_LOST: if (mine) Say("You're Lost. Your feet are walking somewhere.", WARN); else Say(w.crew[e.who].name + " is Lost: grab them (E)!", WARN); break;
            case E_OVERTIME: Say("OVERTIME. The portals are shut until 06:00. Get to a lit Lab.", WARN); if (!S.shot) NoclipCue(NCC_OVERTIME, 1, 0); break;
            case E_BREACH: Say("A breach: something tipped a Lab's crate", WARN); break;
            case E_INVITE: if (mine) Say("A Partygoer: \"Come to the party!\"  (Y to accept. Don't.)", Color{255, 200, 240, 255}); break;
            case E_PAGE: Say("PA: \"Paging " + e.s + " to surgery.\"", WARN); break;
            case E_LOCKDOWN: if (m.level == 16) { Say("The asylum's bell: LOCKDOWN. Get in a cell.", WARN); if (!S.shot) NoclipCue(NCC_BELL, 1, 0); } break;
            case E_TRADE: if (mine) Say(e.a > 0 ? "You paid for drinks." : "The Faceling takes it, and leaves you almond water.", OK); break;
            case E_CRATE: if (mine && !S.shot) NoclipCue(NCC_CLICK, 0.6f, 0); break;
            case E_PICKUP: if (mine && !S.shot) NoclipCue(NCC_PICKUP, 0.6f, 0); break;
            case E_STAY: if (mine) Say("Stay? It's lovely here. (Y to stay. You won't be back until morning.)", Color{255, 230, 200, 255}); break;
            case E_SALE: if (e.by == 0 || e.by == 2) { gNcProf.career += (int)e.a; SaveTokens(); } break;
            case E_SHUTDOWN: Say("The Bureau has stopped maintaining your portals.", WARN); break;
            case E_QUOTA: Say("Quota met. Next week's: " + std::to_string((int)e.a), OK); S.tokens += D().tokQuota; SaveTokens(); break;
            default: break;
        }
    }
    while (!S.subs.empty() && S.t - S.subs.front().t > 7) S.subs.pop_front();
}

// ---------------------------------------------------------------- hallucinations (client-side: the others don't see them)
void Hallucinate(float dt) {
    const Player& p = Me(); if (p.st != PS_ALIVE) { S.fakes.clear(); return; }
    S.fakeT -= dt;
    float hal = W().mode == 5 ? 15.0f : 0.0f;
    if (p.sanity < 50 + hal && S.fakeT <= 0 && S.fakes.size() < 2) {
        S.fakeT = 8 + (rand() % 100) / 10.0f;
        Entity f; f.hallucination = true; f.def = EntityIndex(p.sanity < 30 && rand() % 2 ? "hound" : "faceling"); if (f.def < 0) f.def = 0; f.level = p.level;
        float a = p.yaw + ((rand() % 200) / 100.0f - 1) * 1.2f; f.p = Vector3Add(p.p, {cosf(a) * 9, 0, sinf(a) * 9}); f.uid = 900000 + rand() % 1000; f.st = ES_CHASE;
        Level& lv = W().L(p.level); if (!lv.Solid(lv.CellX(f.p.x), lv.CellZ(f.p.z))) S.fakes.push_back(f);
        if (!S.shot) NoclipCue(rand() % 2 ? NCC_STEPS : NCC_TELL, 0.5f, ((rand() % 200) / 100.0f - 1));   // (footsteps behind; a giggle)
    }
    for (auto& f : S.fakes) { Vector3 d = Vector3Subtract(p.p, f.p); d.y = 0; float L = Vector3Length(d); if (L < 1.4f) f.st = ES_GONE; else { f.v = Vector3Scale(d, 2.5f / L); f.p = Vector3Add(f.p, Vector3Scale(f.v, dt)); f.yaw = atan2f(d.z, d.x); } }
    S.fakes.erase(std::remove_if(S.fakes.begin(), S.fakes.end(), [](const Entity& f) { return f.st == ES_GONE; }), S.fakes.end());
    S.valueLie = p.sanity < 30 ? 0.5f + 1.5f * fabsf(sinf(S.t * 0.13f)) : 1;
}

// ---------------------------------------------------------------- input
int NearSpot(const Player& p, int* labOut) {
    World& w = W(); Level& lv = w.L(p.level);
    for (int k = 0; k < (int)lv.labs.size(); k++) for (const auto& sp : lv.labs[k].spots) if (Vector2Distance({sp.at.x, sp.at.z}, {p.p.x, p.p.z}) < 1.7f) { if (labOut) *labOut = k; return sp.part; }
    return -1;
}
void Gather() {
    Player& p = Me(); Input in; World& w = W();
    bool ui = S.panel >= 0 || S.map || !w.inDay || p.st == PS_SURFACE;
    Vector2 md = MouseLook(!S.shot && !ui);
    S.camYaw += md.x * 0.0024f; S.camPitch = std::clamp(S.camPitch - md.y * 0.0024f, -1.35f, 1.35f);
    in.yaw = S.camYaw; in.pitch = S.camPitch;
    if (!ui) {
        in.moveX = (IsKeyDown(KEY_W) ? 1.0f : 0) - (IsKeyDown(KEY_S) ? 1.0f : 0); in.moveZ = (IsKeyDown(KEY_D) ? 1.0f : 0) - (IsKeyDown(KEY_A) ? 1.0f : 0);
        in.sprint = IsKeyDown(KEY_LEFT_SHIFT); in.crouch = IsKeyDown(KEY_LEFT_CONTROL); in.lamp = IsKeyPressed(KEY_F);
        in.drop = IsKeyPressed(KEY_G); in.throwIt = IsKeyPressed(KEY_T); in.primary = IsMouseButtonPressed(MOUSE_BUTTON_LEFT); in.primaryHeld = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        for (int k = 0; k < 5; k++) if (IsKeyPressed(KEY_ONE + k)) in.slot = k;
        float wh = GetMouseWheelMove(); if (wh != 0) in.slot = (p.sel + (wh < 0 ? 1 : p.toolSlots - 1)) % p.toolSlots;
        if (IsKeyPressed(KEY_E)) {
            // the Lab's panels open here; traders and the Innkeeper take a C_TRADE; everything else is Interact
            int lab = -1, part = NearSpot(p, &lab);
            if (part == LP_DESK) S.panel = 0; else if (part == LP_SHOP) S.panel = 1; else if (part == LP_MONITORS) S.panel = 2;
            else { bool traded = false; for (const auto& e : w.ents) { const std::string& id = D().entities[e.def].id; if (e.level == p.level && Vector3Distance(e.p, p.p) < 2.6f && ((id == "faceling" && e.mimicOf == -2) || id == "innkeeper")) { Send(Command{C_TRADE}); traded = true; break; } } if (!traded) in.use = true; }
        }
        if (IsKeyPressed(KEY_Y)) Send(Command{C_ACCEPT});
        if (p.impostor && IsKeyPressed(KEY_R)) Send(Command{C_GRAB});
        if (IsKeyPressed(KEY_M)) S.map = true;
        if (IsKeyPressed(KEY_F1)) S.help = !S.help;
    } else {
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_E) || IsKeyPressed(KEY_M)) { S.panel = -1; S.map = false; }
    }
    p.in = in;
}

// ---------------------------------------------------------------- the Lab's panels, the map
Rectangle Panel() { return {SCREEN_W / 2.0f - 340, 90, 680, 500}; }
void PanelFrame(const char* title) { Rectangle r = Panel(); DrawRectangleRec(r, Color{14, 16, 14, 235}); DrawRectangleLinesEx(r, 2, Color{180, 140, 70, 255}); DrawTextCenteredBold(title, r.x + r.width / 2, r.y + 12, 24, Color{240, 210, 150, 255}); DrawTextCentered("E or Esc closes", r.x + r.width / 2, r.y + r.height - 24, 13, DIM); }
bool Row(Rectangle r, const std::string& l, const std::string& rt, bool en) {
    bool hov = CheckCollisionPointRec(GetMousePosition(), r); DrawRectangleRec(r, hov && en ? Color{50, 60, 50, 230} : Color{30, 34, 30, 220});
    Txt(l, r.x + 8, r.y + (r.height - 15) / 2, 15, en ? INK : DIM); Txt(rt, r.x + r.width - 8 - MeasureTxt(rt, 15), r.y + (r.height - 15) / 2, 15, en ? Color{240, 210, 120, 255} : DIM);
    return hov && en && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
std::string LabName(const LabState& l) { const LevelDef& d = D().levels[l.level]; return "Lab " + (l.idx < (int)d.labs.size() ? d.labs[l.idx] : std::string("?")) + " (Level " + std::to_string(l.level) + ")"; }
void PanelDesk() {
    World& w = W(); Player& p = Me(); Rectangle r = Panel(); PanelFrame("The portal desk");
    LabState* lab = w.LabAt(p.level, p.p); if (!lab) { S.panel = -1; return; }
    float y = r.y + 54;
    Txt(TextFormat("%s   generator: %s   network crates: $%d", LabName(*lab).c_str(), lab->online && lab->fuel > 0 ? TextFormat("running (%.1f days of fuel)", lab->fuel) : "dead", w.CrateTotal()), r.x + 20, y, 15, INK); y += 30;
    if (lab->openT > 0) DrawTextCenteredBold(TextFormat("THE PORTAL IS OPEN: %d s. Into the hall!", (int)lab->openT), r.x + r.width / 2, y, 20, OK);
    else if (lab->charging) { DrawRectangle((int)r.x + 20, (int)y, (int)(640 * lab->charge), 20, Color{120, 160, 255, 255}); DrawRectangleLines((int)r.x + 20, (int)y, 640, 20, INK); DrawTextCentered(TextFormat("charging %d%%", (int)(lab->charge * 100)), r.x + r.width / 2, y + 2, 15, BLACK); }
    else if (lab->cooldown > 0) DrawTextCentered(TextFormat("cooling down (%d s)", (int)lab->cooldown), r.x + r.width / 2, y, 16, DIM);
    y += 34;
    bool can = lab->online && lab->fuel > 0 && !w.overtime;
    if (Button({r.x + 20, y, 200, 36}, lab->charging ? "Pause the charge" : lab->charge > 0 ? "Resume the charge" : "Signal the portal", can && lab->cooldown <= 0 && lab->openT <= 0, 15)) Send(Command{lab->charge > 0 ? C_PORTAL_PAUSE : C_PORTAL});
    if (Button({r.x + 240, y, 200, 36}, lab->locked ? "Unlock the doors" : "Lock the doors", can, 15)) Send(Command{C_LOCK});
    y += 52;
    Txt("Jump to another online Lab (10 s; one an hour each):", r.x + 20, y, 15, INK); y += 24;
    for (int i = 0; i < (int)w.labs.size(); i++) { const LabState& l = w.labs[i]; if (!l.online || &l == lab) continue; if (Row({r.x + 20, y, 640, 26}, LabName(l) + (l.fuel > 0 ? "" : " (no fuel)"), p.jumpCool > 0 ? TextFormat("in %d min", (int)p.jumpCool) : "jump", l.fuel > 0 && p.jumpCool <= 0 && !w.overtime && can)) { Command c; c.kind = C_JUMP; c.a = i; Send(c); S.panel = -1; } y += 30; }
    if ((lab->upgrades >> 6) & 1) { if (Button({r.x + 460, r.y + 118, 200, 36}, lab->sirenT > 0 ? "The Siren is wailing" : "Sound the Siren", lab->sirenT <= 0 && can, 14)) Send(Command{C_SIREN}); }
    if ((lab->upgrades >> 5) & 1) { y += 10; Txt(lab->cargoCool > 0 ? "Cargo link: cooling down" : "Cargo link: send this crate to", r.x + 20, y, 14, INK); y += 22; if (lab->cargoCool <= 0) for (int i = 0; i < (int)w.labs.size(); i++) { const LabState& l = w.labs[i]; if (!l.online || &l == lab) continue; if (Row({r.x + 20, y, 640, 24}, LabName(l), "send", can)) { Command c; c.kind = C_CARGO; c.a = i; Send(c); } y += 28; } }
    if (lab->jumpFor == S.me) DrawTextCentered(TextFormat("jumping in %.0f s: stay in the hall", lab->jumpT), r.x + r.width / 2, y + 6, 16, OK);
}
void PanelShop() {
    World& w = W(); Player& p = Me(); Rectangle r = Panel(); PanelFrame("The commissary");
    float mul = D().labPriceMul[std::clamp(p.level, 0, LEVELS - 1)] * 1.25f;
    Txt(TextFormat("Crew cash: $%d   (this Lab's prices are %d%% of the Surface's)", w.cash, (int)(mul * 100)), r.x + 20, r.y + 50, 15, INK);
    for (int i = 0; i < (int)D().items.size(); i++) { const ItemDef& it = D().items[i]; float x = r.x + 20 + (i / 11) * 325, y = r.y + 80 + (i % 11) * 34; int pr = (int)roundf(it.price * mul); if (Row({x, y, 315, 30}, it.name, TextFormat("$%d", pr), w.cash >= pr)) { Command c; c.kind = C_LAB_BUY; c.a = i; Send(c); } }
}
void DrawFieldMap(bool monitors) {
    World& w = W(); Player& p = Me(); Level& lv = w.L(p.level);
    Rectangle r{SCREEN_W / 2.0f - 380, 60, 760, 560}; DrawRectangleRec(r, Color{226, 216, 186, 245}); DrawRectangleLinesEx(r, 3, Color{90, 70, 50, 255});
    DrawTextCenteredBold(monitors ? "The monitors" : TextFormat("Bureau field map: Level %d, %s", p.level, D().levels[p.level].name.c_str()), r.x + r.width / 2, r.y + 8, 20, Color{60, 40, 30, 255});
    float cs = std::min((r.width - 40) / lv.w, (r.height - 70) / lv.h); float ox = r.x + (r.width - cs * lv.w) / 2, oz = r.y + 40;
    const auto* seen = (size_t)p.level < w.seenCells.size() ? &w.seenCells[p.level] : nullptr;
    int lie = p.sanity < 30 ? 1 : 0;   // (the map lies by a room)
    LabState* myLab = w.LabAt(p.level, p.p);
    for (int z = 0; z < lv.h; z++) for (int x = 0; x < lv.w; x++) {
        bool known = seen && (size_t)(z * lv.w + x) < seen->size() && (*seen)[z * lv.w + x];
        if (monitors && myLab) { const LabPlan& lp = lv.labs[myLab->idx]; known = known || (abs(x - (lp.x0 + lp.x1) / 2) < ((myLab->upgrades >> 2) & 1 ? 26 : 16) && abs(z - (lp.z0 + lp.z1) / 2) < ((myLab->upgrades >> 2) & 1 ? 26 : 16)); }
        if (!known) continue;
        uint8_t t = lv.At(x + lie, z); Color c = lv.Walkable(x + lie, z) ? Color{200, 186, 150, 255} : Color{90, 70, 50, 255}; if (t == T_LABFLOOR) c = Color{150, 170, 190, 255}; if (t == T_DEEP || t == T_WATER) c = Color{110, 140, 170, 255}; if (t == T_PIT) c = BLACK;
        DrawRectangle((int)(ox + x * cs), (int)(oz + z * cs), (int)ceilf(cs), (int)ceilf(cs), c);
    }
    for (const auto& e : lv.exits) { std::string key = std::to_string(p.level) + ":" + e.label; bool knownExit = std::find(w.learned.begin(), w.learned.end(), key) != w.learned.end() || ((p.suits >> 5) & 1 && Vector3Distance(lv.Center(e.cx, e.cz), p.p) < 20); bool seenIt = seen && (*seen)[e.cz * lv.w + e.cx]; if (!knownExit && !seenIt) continue; Vector2 at{ox + (e.cx + 0.5f) * cs, oz + (e.cz + 0.5f) * cs}; DrawCircleV(at, 5, e.noclip ? Color{200, 120, 40, 255} : Color{40, 140, 60, 255}); if (knownExit) Txt(TextFormat("%s -> %d", e.label.c_str(), e.to), at.x + 6, at.y - 6, 11, Color{60, 40, 30, 255}); }
    for (int k = 0; k < (int)lv.labs.size(); k++) { const LabPlan& lp = lv.labs[k]; LabState* l = w.Lab(p.level, k); DrawRectangleLines((int)(ox + lp.x0 * cs), (int)(oz + lp.z0 * cs), (int)((lp.x1 - lp.x0 + 1) * cs), (int)((lp.z1 - lp.z0 + 1) * cs), l && l->online ? Color{40, 90, 160, 255} : Color{120, 120, 120, 255}); Txt("Lab " + lp.name + (l && l->online ? "" : " (dormant)"), ox + lp.x0 * cs, oz + lp.z0 * cs - 12, 11, Color{40, 60, 100, 255}); }
    bool radio = false; for (int k = 0; k < p.toolSlots; k++) if (p.tools[k].item == ItemIndex("radio")) radio = true;
    for (const auto& q : w.crew) if (q.level == p.level && q.Alive() && (q.id == S.me || radio)) DrawCircleV({ox + q.p.x / CELL * cs, oz + q.p.z / CELL * cs}, q.id == S.me ? 5.0f : 4.0f, q.id == S.me ? Color{220, 40, 40, 255} : Color{40, 40, 200, 255});
    if (monitors) for (const auto& e : w.ents) if (e.level == p.level && myLab) { const LabPlan& lp = lv.labs[myLab->idx]; if (fabsf(e.p.x / CELL - (lp.x0 + lp.x1) / 2.0f) < 16 && fabsf(e.p.z / CELL - (lp.z0 + lp.z1) / 2.0f) < 16) { DrawCircleV({ox + e.p.x / CELL * cs, oz + e.p.z / CELL * cs}, 4, Color{200, 30, 30, 255}); Txt(D().entities[e.def].name, ox + e.p.x / CELL * cs + 5, oz + e.p.z / CELL * cs - 5, 10, Color{150, 20, 20, 255}); } }
    DrawTextCentered(D().levels[p.level].compass ? "The compass works here." : "The compass spins.", r.x + r.width / 2, r.y + r.height - 22, 13, Color{90, 70, 50, 255});
}

// ---------------------------------------------------------------- the HUD on the wrist, the camcorder's overlay
void DrawHud(Game& g) {
    World& w = W(); Player& p = Me(); float cx = SCREEN_W / 2.0f, cy = SCREEN_H / 2.0f;
    if (S.hurtFlash > 0) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{180, 20, 20, (unsigned char)(120 * S.hurtFlash)});
    ncr::Stamp(TextFormat("WEEK %d  DAY %d   %s", w.week, std::min(w.day, D().daysPerWeek), D().levels[p.level].name.c_str()), w.ClockText() + (w.overtime ? "  OVERTIME" : ""), S.t);
    for (size_t i = 0; i < S.subs.size(); i++) DrawTextCentered(S.subs[i].text, cx, SCREEN_H - 150.0f - (S.subs.size() - 1 - i) * 22, 16, Fade(S.subs[i].c, std::min(1.0f, (7 - (S.t - S.subs[i].t)) / 1.5f)));
    if (p.st == PS_DEAD) { DrawTextCenteredBold("You're a Wanderer now", cx, 80, 26, Color{180, 200, 255, 255}); DrawTextCentered("Drift through walls; whisper to the living within 3 m. You'll be back at the Surface in the morning.", cx, 112, 15, INK); return; }
    if (p.st == PS_TAKEN) { DrawTextCenteredBold("Taken", cx, cy - 20, 34, WARN); DrawTextCentered("Something wearing you is with the crew now.", cx, cy + 22, 16, INK); return; }
    if (p.st == PS_DOWNED) { DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{90, 0, 0, 90}); DrawTextCenteredBold(TextFormat("DOWN: %d s", (int)p.downT), cx, cy - 30, 30, WARN); DrawTextCentered("Crawl (W). Shout for a teammate: they revive you with E (a medkit makes it quick).", cx, cy + 10, 15, INK); }
    // the wrist: the watch and the three meters
    Rectangle wr{24, SCREEN_H - 150.0f, 230, 90}; DrawRectangleRounded(wr, 0.2f, 6, Color{20, 20, 18, 200});
    TxtBold(w.ClockText(), wr.x + 12, wr.y + 8, 24, w.clock > D().evening ? WARN : INK);
    auto bar = [&](float y, float v, Color c, const char* l) { DrawRectangle((int)wr.x + 12, (int)(wr.y + y), (int)(150 * v / 100), 8, c); DrawRectangleLines((int)wr.x + 12, (int)(wr.y + y), 150, 8, Color{90, 90, 80, 255}); Txt(l, wr.x + 168, wr.y + y - 4, 13, DIM); };
    bar(40, p.health, Color{220, 70, 60, 255}, "health"); bar(55, p.sanity, Color{150, 120, 230, 255}, "sanity"); bar(70, p.stamina, Color{230, 200, 80, 255}, "stamina");
    std::string inj = (p.injuries & IN_BLEED ? "BLEEDING " : std::string()) + (p.injuries & IN_SPRAIN ? "sprain " : "") + (p.injuries & IN_BREAK ? "BREAK" : ""); if (!inj.empty()) Txt(inj, wr.x + 110, wr.y + 12, 13, WARN);
    if (p.lamp && p.battery > 0) Txt(TextFormat("lamp %d min", (int)(p.battery / 60) + 1), wr.x + 110, wr.y + 26, 12, DIM); else Txt("lamp OFF (F)", wr.x + 110, wr.y + 26, 12, p.battery > 0 ? DIM : WARN);
    // the pack: tools, then pockets and hands
    for (int k = 0; k < p.toolSlots; k++) {
        Rectangle s{cx - p.toolSlots * 52 / 2.0f + k * 52, SCREEN_H - 70.0f, 48, 48}; DrawRectangleRec(s, k == p.sel ? Color{70, 60, 30, 220} : Color{20, 20, 18, 200}); DrawRectangleLinesEx(s, 1, k == p.sel ? Color{240, 200, 90, 255} : Color{90, 90, 80, 255});
        Txt(TextFormat("%d", k + 1), s.x + 3, s.y + 2, 11, DIM);
        if (p.tools[k].item >= 0) { const ItemDef& it = D().items[p.tools[k].item]; std::string nm = it.name.substr(0, 9); DrawTextCentered(nm, s.x + 24, s.y + 18, 11, INK); if (p.tools[k].charges > 1) Txt(TextFormat("x%d", p.tools[k].charges), s.x + 30, s.y + 34, 11, DIM); }
    }
    float hy = SCREEN_H - 96.0f;
    auto lootLine = [&](const Loot& l) { const LootDef& d = D().loot[l.def]; return d.name + "  ~$" + std::to_string((int)(l.value * S.valueLie)) + (l.damaged ? " (damaged)" : ""); };
    if (p.hands.def >= 0) DrawTextCentered("In your hands: " + lootLine(p.hands) + (p.carryWith >= 0 ? "  (two of you)" : ""), cx, hy, 15, INK);
    for (int k = 0; k < 2; k++) if (p.pocket[k].def >= 0) DrawTextCentered("Pocket: " + lootLine(p.pocket[k]), cx, hy - 18 - k * 18, 13, DIM);
    // what's in front of you
    DrawCircle((int)cx, (int)cy, 2, Fade(INK, 0.7f));
    {
        int lab = -1, part = NearSpot(p, &lab); std::string prompt;
        static const char* PART[LP_COUNT] = {"the portal ring (stand here to go up)", "E: the portal desk", "E: the drop-off crate (put everything in)", "E: the commissary", "E: the bunks (sleep once a day: +20 sanity)", "E: the generator (needs a fuel canister)", "E: the monitors", "E: the archive (a map fragment)"};
        if (part >= 0) prompt = PART[part];
        Vector3 f{cosf(p.yaw), 0, sinf(p.yaw)}; Vector3 front = Vector3Add(p.p, Vector3Scale(f, 0.9f));
        for (const auto& it : w.items) if (it.level == p.level && it.loot.def >= 0 && Vector3Distance(it.p, front) < 1.7f) { prompt = "E: pick up " + lootLine(it.loot); break; }
        for (const auto& q : w.crew) if (q.id != S.me && q.level == p.level && Vector3Distance(q.p, p.p) < 1.8f && (q.st == PS_DOWNED || q.lostT > 0)) prompt = q.st == PS_DOWNED ? "E: revive " + q.name : "E: grab " + q.name + " (they're Lost)";
        Level& lv = w.L(p.level); int fx = lv.CellX(front.x), fz = lv.CellZ(front.z);
        for (const auto& e : lv.exits) if (!e.noclip && e.cx == fx && e.cz == fz) prompt = "E: " + e.label + " (to Level " + std::to_string(e.to) + ")";
        for (const auto& lp : lv.labs) { if (Vector3Distance(lv.Center(lp.breakerX, lp.breakerZ), p.p) < 1.8f) prompt = "E: a breaker (the Lab's blast door)"; if (Vector3Distance(lv.Center(lp.doorX, lp.doorZ), p.p) < 2.2f) { LabState* l = w.LabAt(p.level, lv.Center(lp.x0 + 1, lp.z0 + 1)); if (l && !l->doorOpen) prompt = "A blast door. Find the level's breaker, or pry it (a crowbar and two of you)."; } }
        for (const auto& e : w.ents) { const std::string& id = D().entities[e.def].id; if (e.level == p.level && Vector3Distance(e.p, p.p) < 2.6f && ((id == "faceling" && e.mimicOf == -2) || id == "innkeeper")) prompt = id == "faceling" ? "E: trade (a pocket thing for almond water)" : "E: pay for drinks ($20)"; }
        for (const auto& lvr : w.levers) if (lvr.level == p.level && Vector3Distance(lvr.at, p.p) < 1.7f) prompt = lvr.kind == 0 ? "E: turn the valve (opens this level's sealed hatches)" : w.power ? "E: throw the breaker (power off: quiet, dark, the elevator dies)" : "E: throw the breaker (power on: machines, light, live floors)";
        for (const auto& e : w.ents) if (e.level == p.level && D().entities[e.def].id == "survivor" && Vector3Distance(e.p, p.p) < 2.2f && e.target != S.me) prompt = "E: \"Are you... Bureau?\" (they'll follow you; get them to a Lab's portal)";
        for (const auto& q : w.crew) if (q.id != S.me && q.atParty && q.level == p.level && Vector3Distance(q.p, p.p) < 2.0f) prompt = "E: pull " + q.name + " out of the party";
        if (p.level == 5) { int dx = lv.CellX(front.x), dz = lv.CellZ(front.z); if (lv.At(dx, dz) == T_DOOR && prompt.empty()) prompt = "Room " + std::to_string(World::RoomNumber(dx, dz)); }
        if (p.atParty) prompt = "You're at the party. It's lovely. (Someone has to come and fetch you.)";
        if (!prompt.empty() && S.panel < 0 && !S.map) DrawTextCentered(prompt, cx, cy + 34, 16, INK);
    }
    {   // the scanner, if you carry one
        bool scanner = false; for (int k = 0; k < p.toolSlots; k++) if (p.tools[k].item == ItemIndex("scanner")) scanner = true;
        if (scanner) {
            Vector2 sc{SCREEN_W - 100.0f, SCREEN_H - 190.0f}; DrawCircleV(sc, 70, Color{10, 30, 14, 210}); DrawCircleLines((int)sc.x, (int)sc.y, 70, Color{60, 200, 90, 255}); DrawCircleLines((int)sc.x, (int)sc.y, 35, Color{40, 120, 60, 255});
            auto blip = [&](Vector3 at) { Vector3 d = Vector3Subtract(at, p.p); float a = atan2f(d.z, d.x) - p.yaw - PI / 2, L = Vector2Length({d.x, d.z}); if (L > 20) return; DrawCircleV({sc.x + cosf(a) * L * 3.4f, sc.y + sinf(a) * L * 3.4f}, 3, Color{120, 255, 140, (unsigned char)(160 + 90 * sinf(S.t * 6))}); };
            for (const auto& e : w.ents) { const std::string& id = D().entities[e.def].id; if (e.level == p.level && id != "skinstealer" && id != "crew" && id != "mirrorthing" && id != "seer") blip(e.p); }
            for (const auto& q : w.crew) if (q.id != S.me && q.level == p.level && q.Alive() && !q.impostor) blip(q.p);
            Txt("SCANNER", sc.x - 26, sc.y + 74, 11, Color{60, 200, 90, 255});
        }
    }
    if (p.impostor) DrawTextCentered(p.takenOnLevel == p.level ? "You are the Skin-Stealer. You've fed on this level." : "You are the Skin-Stealer. R takes a teammate within reach (one per level). You can't die.", cx, 64, 15, Color{255, 120, 120, 255});
    if (p.lostT > 0) DrawTextCenteredBold("LOST", cx, cy - 80, 30, WARN);
    if (p.blackoutT > 0) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{0, 0, 0, (unsigned char)(255 * std::min(1.0f, p.blackoutT))});
    if (S.help && S.panel < 0 && !S.map) {
        Rectangle r{SCREEN_W - 330.0f, 70, 314, 232}; DrawRectangleRounded(r, 0.05f, 6, Color{10, 10, 10, 180});
        const char* L[] = {"WASD walk, Shift sprint, Ctrl crouch", "E use / pick up / open, G drop, T throw", "F headlamp; 1-5 or wheel: a tool; click uses it", "M the field map; Caps Lock: talk (nearby)", "Bring loot to a Lab's crate, signal the portal", "at the desk, and stand in the ring to go up", "Sanity: almond water, company, Lab lights", "Never run from a Smiler. Walk past Hounds.", "Y accepts a Partygoer (don't).  F1 hides this"};
        for (int i = 0; i < 9; i++) Txt(L[i], r.x + 10, r.y + 8 + i * 24, 14, INK);
    }
    if (S.panel == 0) PanelDesk(); else if (S.panel == 1) PanelShop(); else if (S.panel == 2) DrawFieldMap(true);
    if (S.map) DrawFieldMap(false);
    (void)g;
}

// ---------------------------------------------------------------- the Surface
void DrawSurface(Game& g) {
    World& w = W(); float cx = SCREEN_W / 2.0f;
    ncr::Stamp(TextFormat("THE SURFACE   WEEK %d  DAY %d of %d", w.week, std::min(w.day, D().daysPerWeek), D().daysPerWeek), "NIGHT", S.t);
    Rectangle r{cx - 450, 70, 900, 560}; DrawRectangleRec(r, Color{16, 14, 12, 225}); DrawRectangleLinesEx(r, 2, Color{200, 140, 60, 255});
    static const char* TABS[6] = {"Whiteboard", "Loading bay", "Shop", "Lab upgrades", "Dossier", "Insertion"};
    for (int k = 0; k < 6; k++) if (Button({r.x + 10 + k * 148.0f, r.y + 8, 140, 30}, TABS[k], true, 14)) S.surfTab = k;
    float y = r.y + 56;
    Txt(TextFormat("Quota %d   Credit %d   Cash $%d   Days left this week: %d", w.quota, w.credit, w.cash, std::max(0, D().daysPerWeek - w.day + 1)), r.x + 20, y, 18, w.credit >= w.quota ? OK : INK);
    Txt(TextFormat("%s (%d career credit)", RankName().c_str(), gNcProf.career), r.x + r.width - 20 - MeasureTxt(TextFormat("%s (%d career credit)", RankName().c_str(), gNcProf.career), 14), y + 3, 14, DIM); y += 30;
    if (w.failed && w.mode != 0) {
        const char* head = w.mode == 1 ? (w.won ? "You found the way out." : "Nobody found the way out.") : w.mode == 4 ? "Expedition over" : w.mode == 6 ? (w.won ? "The crew got out and left it behind." : "The thing wearing a friend wins.") : "The Bureau has stopped maintaining your portals.";
        DrawTextCenteredBold(head, cx, y + 60, 24, w.won ? OK : WARN);
        if (w.mode == 4) DrawTextCentered(TextFormat("Score: %d (credit plus everything in the bay)", w.score), cx, y + 100, 18, INK);
        if (Button({cx - 100, y + 150, 200, 40}, "Back to the arcade", true, 16)) { LeaveNoclip(g); }
        return;
    }
    if (w.failed) { DrawTextCenteredBold("MEMO: The Bureau has stopped maintaining your portals.", cx, y + 60, 22, WARN); DrawTextCentered(TextFormat("Weeks survived: %d.  Entities documented: %d.  It was nice knowing you.", w.weeksSurvived, [&] { int n = 0; for (int k = 0; k < 32; k++) n += (w.dossier >> k) & 1; return n; }()), cx, y + 96, 16, INK); if (Button({cx - 100, y + 140, 200, 40}, "Back to the arcade", true, 16)) { LeaveNoclip(g); } return; }
    switch (S.surfTab) {
        case 0: {   // the whiteboard
            if (!w.memo.empty()) { DrawWrapped(("MEMO: " + w.memo).c_str(), {r.x + 20, y, r.width - 40, 40}, 15, DIM); y += 44; }
            Txt("Forecast (active today: more loot, more entities):", r.x + 20, y, 15, INK); y += 20;
            std::string fc; for (int f : w.forecast) fc += "Level " + std::to_string(f) + " (" + D().levels[f].name + ")   "; Txt(fc, r.x + 30, y, 14, WARN); y += 28;
            if (w.contract >= 0) { const ContractDef& c = D().contracts[w.contract]; Txt("Today's contract: " + c.name + ": " + c.task + TextFormat("   (pays %d credit%s)", c.creditMax, c.cash ? TextFormat(", $%d", c.cash) : ""), r.x + 20, y, 15, Color{200, 220, 255, 255}); y += 28; }
            Txt("Sold so far:", r.x + 20, y, 15, INK); y += 20;
            for (int k = std::max(0, (int)w.sold.size() - 12); k < (int)w.sold.size(); k++) { Txt(TextFormat("%s  (Level %d)  %s $%d", w.sold[k].what.c_str(), w.sold[k].level, w.sold[k].fence ? "the Fence" : "the Bureau", w.sold[k].value), r.x + 30, y, 13, DIM); y += 17; }
            break;
        }
        case 1: {   // the loading bay: sell to the Bureau (quota credit) or to Dez (cash)
            if (w.bay.empty()) { Txt("The bay is empty. Extracted loot lands here.", r.x + 20, y, 15, DIM); break; }
            if (Button({r.x + 20, y, 250, 30}, "Everything to the Bureau", true, 14)) { Command c; c.kind = C_SELL_ALL; c.a = 0; Send(c); }
            if (Button({r.x + 290, y, 250, 30}, "Everything to the Fence", true, 14)) { Command c; c.kind = C_SELL_ALL; c.a = 1; Send(c); }
            y += 40;
            for (int i = 0; i < (int)w.bay.size() && i < 16; i++) {
                const Loot& l = w.bay[i]; const LootDef& d = D().loot[l.def];
                Txt(TextFormat("%s  ($%d%s)  from Level %d", d.name.c_str(), l.damaged ? l.value / 2 : l.value, l.damaged ? ", damaged" : "", l.foundOn), r.x + 20, y + 6, 14, INK);
                if (Button({r.x + 560, y, 150, 26}, "Bureau (credit)", true, 12)) { Command c; c.kind = C_SELL_BUREAU; c.a = i; Send(c); }
                if (Button({r.x + 720, y, 150, 26}, "Fence (cash)", true, 12)) { Command c; c.kind = C_SELL_FENCE; c.a = i; Send(c); }
                if (!d.note.empty() && CheckCollisionPointRec(GetMousePosition(), {r.x + 20, y, 520, 26})) DrawTextCentered("Bureau note: " + d.note, cx, r.y + r.height - 50, 14, DIM);
                y += 30;
            }
            break;
        }
        case 2: {   // the shop: gear for whoever's buying (you), and the suits
            Player& p = Me();
            for (int i = 0; i < (int)D().items.size(); i++) { const ItemDef& it = D().items[i]; float x = r.x + 20 + (i / 11) * 290, yy = y + (i % 11) * 32; if (Row({x, yy, 280, 28}, it.name, TextFormat("$%d", it.price), w.cash >= it.price)) { Command c; c.kind = C_BUY; c.a = i; Send(c); } if (CheckCollisionPointRec(GetMousePosition(), {x, yy, 280, 28})) DrawTextCentered(it.note, cx, r.y + r.height - 50, 14, DIM); }
            for (int i = 0; i < (int)D().suits.size(); i++) { const SuitDef& s = D().suits[i]; bool own = (p.suits >> i) & 1; if (Row({r.x + 600, y + i * 32.0f, 280, 28}, s.name + (own ? " (yours)" : ""), TextFormat("$%d", s.price), !own && w.cash >= s.price)) { Command c; c.kind = C_BUY_SUIT; c.a = i; Send(c); } if (CheckCollisionPointRec(GetMousePosition(), {r.x + 600, y + i * 32.0f, 280, 28})) DrawTextCentered(s.note, cx, r.y + r.height - 50, 14, DIM); }
            break;
        }
        case 3: {   // Lab upgrades, installed per Lab
            int row = 0;
            for (int li = 0; li < (int)w.labs.size(); li++) { const LabState& l = w.labs[li]; if (!l.online) continue; Txt(LabName(l), r.x + 20, y + row * 30.0f + 6, 14, INK); for (int u = 0; u < (int)D().labUps.size(); u++) { bool has = (l.upgrades >> u) & 1; Rectangle b{r.x + 250 + u * 90.0f, y + row * 30.0f, 86, 26}; if (Button(b, D().labUps[u].name.substr(0, 11).c_str(), !has && w.cash >= D().labUps[u].price, 11)) { Command c; c.kind = C_BUY_UPGRADE; c.a = u; c.b = li; Send(c); } if (CheckCollisionPointRec(GetMousePosition(), b)) DrawTextCentered(D().labUps[u].name + TextFormat(" ($%d): ", D().labUps[u].price) + D().labUps[u].note, cx, r.y + r.height - 50, 14, DIM); } row++; }
            if (!row) Txt("No Labs online but Alpha... restart dormant ones in the field.", r.x + 20, y, 15, DIM);
            break;
        }
        case 4: {   // the dossier: what's been photographed, and its counter
            for (int i = 0; i < (int)D().entities.size(); i++) { const EntityDef& e = D().entities[i]; bool known = (w.dossier >> i) & 1; Txt(known ? e.name + ": " + e.counter : "??? (photograph it: a Bureau camera, lit, within 15 m)", r.x + 20, y + i * 21.0f, 13, known ? INK : DIM); }
            break;
        }
        case 5: {   // where tomorrow starts
            for (int i = 0; i < (int)w.labs.size(); i++) { const LabState& l = w.labs[i]; if (!l.online || l.level == 12) continue; if (Row({r.x + 20, y, 500, 30}, LabName(l) + (l.fuel > 0 ? "" : " (no fuel: it'll be dark)"), w.insertion == i ? "chosen" : "insert here", w.insertion != i)) { Command c; c.kind = C_INSERT; c.a = i; Send(c); } y += 34; }
            break;
        }
    }
    bool weekEnd = w.day > D().daysPerWeek;
    if (Button({cx - 130, r.y + r.height - 50, 260, 40}, weekEnd ? (w.credit >= w.quota ? "Hand in the week, then go" : "Hand in the week (short!)") : "Through the portal", !S.net || S.net->role == arcade::R_HOST, 16)) Send(Command{C_START_DAY});
    if (S.net && S.net->role != arcade::R_HOST) DrawTextCentered("The host sends the crew through.", cx, r.y + r.height - 6, 13, DIM);
    if (Button({r.x + 10, r.y + r.height - 50, 140, 40}, "Leave", true, 14)) LeaveNoclip(g);
}

// ---------------------------------------------------------------- the camera, the voice
void StepCamera(float dt) {
    Player& p = Me(); World& w = W();
    float sp = Vector2Length({p.vel.x, p.vel.z}); S.bob += dt * sp * 2.2f;
    Vector3 eye = p.Eye(); if (p.st == PS_ALIVE) eye.y += sinf(S.bob) * 0.04f * std::min(1.0f, sp / 3);
    Level& lv = w.L(p.level); uint8_t t = lv.At(lv.CellX(p.p.x), lv.CellZ(p.p.z)); if (t == T_DEEP) eye.y = 0.25f; else if (t == T_WATER) eye.y -= 0.5f;
    if (p.st == PS_DOWNED) eye.y = 0.4f;
    S.cam.position = eye; float yaw = S.camYaw, pitch = S.camPitch;
    if (p.lostT > 0) { yaw = p.yaw; }
    if (p.sanity < 30) { yaw += sinf(S.t * 0.7f) * 0.01f; pitch += cosf(S.t * 0.5f) * 0.01f; }
    S.cam.target = Vector3Add(eye, {cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw)}); S.cam.up = {0, 1, 0}; if (p.level == 17 && w.inDay) { float r = w.Roll() * 0.07f; Vector3 fw{cosf(yaw), 0, sinf(yaw)}, rt{-fw.z, 0, fw.x}; S.cam.up = Vector3Add(Vector3Scale({0, 1, 0}, cosf(r)), Vector3Scale(rt, sinf(r))); } S.cam.fovy = 72; S.cam.projection = CAMERA_PERSPECTIVE;
}
void Voice() {
    if (!S.net) return; World& w = W(); const Player& me = Me();
    for (const auto& q : w.crew) {
        if (q.id == S.me) continue; int seat = S.net->SeatOfPlayer(q.id); if (seat < 0) continue;
        voice::Hearing h; float d = q.level == me.level ? Vector3Distance(q.p, me.p) : 999;
        auto hasRadio = [](const Player& x) { for (int k = 0; k < x.toolSlots; k++) if (x.tools[k].item == ItemIndex("radio")) return true; return false; };
        bool radios = hasRadio(q) && hasRadio(me);
        if (q.st == PS_DEAD) { h.gain = d < 3 ? 0.4f * (1 - d / 3) : 0; h.ghost = 1; }
        else if (me.st == PS_SURFACE || q.st == PS_SURFACE) { h.gain = me.st == q.st ? 1 : 0; }
        else { h.gain = std::clamp(1 - d / 25, 0.0f, 1.0f); if (!w.LineOfSight(me.level, me.Eye(), q.Eye())) h.muffle = 0.7f; if (h.gain < 0.05f && radios && q.level == me.level) { h.gain = 0.6f; h.radio = 1; } }
        Vector3 rel = Vector3Subtract(q.p, me.p); h.pan = std::clamp(sinf(atan2f(rel.z, rel.x) - me.yaw), -1.0f, 1.0f);
        voice::SetHearing(seat, h);
    }
}
}  // namespace


void StartNoclip(Game& g, int bots, int mode) {
    LoadTokens();
    S = NcScene{}; S.active = true; S.bots = std::clamp(bots, 0, MAX_CREW - 1); LoadTokens();
    S.Wm.Init(1, S.bots, mode, (uint32_t)GetRandomValue(1, 1 << 30));
    S.Wm.crew[0].name = "You";
    { Player& me = S.Wm.crew[0]; int rk = RankIdx(); const char* gear[4] = {nullptr, "flashlight", "radio", "scanner"}; for (int k = 1; k <= std::min(rk, 3); k++) { int it = ItemIndex(gear[k]); for (int s = 0; s < me.toolSlots && it >= 0; s++) if (me.tools[s].item < 0) { me.tools[s] = {it, 1, 0}; break; } }
      for (int s = 0; s < 5; s++) if (!gNcProf.eq[s].empty()) { int ci = CosIdx(gNcProf.eq[s]); if (s == 0) me.hat = ci; else if (s == 1) me.vest = ci; else if (s == 2) me.lamp_c = ci; else if (s == 3) me.suitCos = ci; else me.costume = ci; } }
    S.botRng.resize(S.Wm.crew.size()); for (size_t i = 0; i < S.botRng.size(); i++) S.botRng[i] = 4242u * (uint32_t)(i + 1) + (uint32_t)GetRandomValue(0, 1 << 20);
    S.camYaw = 0; S.camPitch = 0; S.lastDay = S.Wm.day;
    g.scene = Scene::Noclip;
}
void StartNoclipNet(Game& g, arcade::Session* net, const char* name) {
    LoadTokens();
    S = NcScene{}; S.active = true; S.net = net; S.netName = name ? name : "Salvager"; S.Wm.mirror = true;
    g.scene = Scene::Noclip;
}
void LeaveNoclip(Game& g) {
    if (S.net) { if (S.net->role == arcade::R_HOST) S.net->BackToLobby(); else S.net->Leave(); S.net = nullptr; }
    S.hostW = nullptr; S.active = false; MouseLook(false); ncr::Unload(); g.scene = Scene::Arcade;
}
void SceneNoclip(Game& g) {
    if (!S.active) StartNoclip(g, 3, 0);
    float dt = S.shot ? 1 / 60.0f : std::min(GetFrameTime(), 1 / 20.0f);
    S.t += dt; S.hurtFlash = std::max(0.0f, S.hurtFlash - dt * 1.5f); S.blackout = std::max(0.0f, S.blackout - dt);
    if (S.net) {
        arcade::Session& N = *S.net; N.Update(GetTime(), dt);
        if (N.stage != arcade::S_PLAYING) { S.net = nullptr; S.hostW = nullptr; S.active = false; MouseLook(false); g.scene = Scene::Arcade; return; }
        int seat = N.MyPlayer();
        if (N.role == arcade::R_HOST) { S.hostW = NoclipHostWorld(N.HostGame()); S.me = NoclipSeatPlayer(N.HostGame(), seat); }
        else if (N.stateVersion != S.seenVersion && !N.Snapshot().empty()) { S.seenVersion = N.stateVersion; Reader r(N.Snapshot()); ReadWorld(r, S.Wm, &S.evSeen); S.me = seat; }
        if (W().crew.empty() || S.me < 0 || S.me >= (int)W().crew.size()) { ClearBackground(BLACK); DrawTextCenteredBold("Connecting to the Bureau...", SCREEN_W / 2.0f, SCREEN_H / 2.0f, 22, INK); return; }
        if (!S.helloSent) { Writer o; o.U8(2); Command c; c.kind = C_NAME; c.s = S.netName; WriteCommand(c, o); N.Act(o); for (int s = 0; s < 5; s++) if (!gNcProf.eq[s].empty()) { Writer x; x.U8(2); Command cc; cc.kind = C_COSMETIC; cc.a = s; cc.b = CosIdx(gNcProf.eq[s]); WriteCommand(cc, x); N.Act(x); } S.helloSent = true; }
        Gather();
        Writer iw; iw.U8(0); WriteInput(Me().in, iw); N.Act(iw);
        for (auto& c : S.outbox) { Writer o; o.U8(2); WriteCommand(c, o); N.Act(o); } S.outbox.clear();
        if (!S.hostW) for (auto& q : S.Wm.crew) S.Wm.SeeMap(q);
        Voice();
    } else if (!S.shot) {
        Gather();
        S.acc += dt; Input mine = Me().in; bool first = true;
        while (S.acc >= STEP) {
            for (auto& p : S.Wm.crew) if (p.id != S.me) BotInput(S.Wm, p.id, p.in, p.cmds, S.botRng[p.id]);
            Me().in = mine; if (!first) { Me().in.use = Me().in.drop = Me().in.throwIt = Me().in.lamp = Me().in.primary = false; Me().in.slot = -1; }
            S.Wm.Step(); S.acc -= STEP; first = false;
        }
    }
    World& w = W();
    if (w.day != S.lastDay) { if (w.day > S.lastDay && !S.shot) { S.tokens += D().tokDay; SaveTokens(); } S.lastDay = w.day; }
    ReadEvents();
    Player& p = Me();
    // the frame: the Surface between days; the level otherwise (a Wanderer sees through the dark)
    { NcAudio a; a.on = !S.shot; a.surface = !w.inDay || p.st == PS_SURFACE; a.level = p.level; a.overtime = w.overtime; a.lightsOut = p.level == 1 && w.lightsOutT > 0; a.sanity = p.sanity; LabState* lab = w.inDay ? w.LabAt(p.level, p.p) : nullptr; a.inLab = lab != nullptr; for (const auto& l : w.labs) if (l.level == p.level && l.charging) a.charge = std::max(a.charge, l.charge); a.dead = p.st == PS_DEAD; AudioNoclip(a); }
    if (!w.inDay || p.st == PS_SURFACE) {
        ncr::View v; v.surface = true; v.t = S.t; v.lamp = false; v.cam.position = {16 + sinf(S.t * 0.05f) * 3, 2.2f, 17}; v.cam.target = {16, 1.6f, 5}; v.cam.up = {0, 1, 0}; v.cam.fovy = 65; v.cam.projection = CAMERA_PERSPECTIVE; v.me = S.me;
        ncr::RenderSurface(w, v); DrawSurface(g);
        if (p.st == PS_SURFACE && w.inDay) DrawTextCentered("You're up. The rest of the crew is still down there.", SCREEN_W / 2.0f, 46, 15, WARN);
        return;
    }
    Hallucinate(dt);
    StepCamera(dt);
    ncr::View v; v.cam = S.cam; v.level = p.level; v.t = S.t; v.sanity = p.sanity; v.lamp = p.lamp && p.battery > 0 && p.st != PS_DEAD; v.lampRange = (p.suits >> 4) & 1 ? 16 : 9; v.me = S.me; v.ghost = p.st == PS_DEAD;
    if (p.lampFlicker > 0) { p.lampFlicker -= dt; if (sinf(S.t * 40) > 0.3f) v.lamp = false; }
    v.flashlight = p.tools[std::clamp(p.sel, 0, 4)].item == ItemIndex("flashlight") && p.in.primaryHeld;
    v.noise = std::clamp((60 - p.sanity) / 60.0f, 0.0f, 1.0f) * 0.7f + (w.overtime ? 0.2f : 0); v.blackout = std::max(S.blackout * 0.5f, p.blackoutT > 0 ? 1.0f : 0.0f);
    v.fakes = S.fakes; v.teammatesAsFacelings = p.sanity < 30 && fmodf(S.t, 20) < 6;
    ncr::Render(w, v);
    DrawHud(g);
}
static std::string gNcCrate; static float gNcCrateT = 0;
bool NoclipLockerPage(Game& g) {
    (void)g; LoadTokens(); const Data& d = D(); ClearBackground(Color{14, 14, 12, 255});
    DrawTextCenteredBold("The Bureau locker", SCREEN_W / 2.0f, 22, 30, Color{240, 210, 150, 255});
    DrawTextCentered(TextFormat("%d arcade tokens   -   %s   -   10 a day survived, 50 a quota met, 25 a new entity photographed", S.tokens, RankName().c_str()), SCREEN_W / 2.0f, 60, 15, DIM);
    static const Color TIER[4] = {{200, 200, 200, 255}, {110, 200, 255, 255}, {220, 140, 255, 255}, {255, 200, 80, 255}};
    for (int i = 0; i < (int)d.cosmetics.size(); i++) {
        const CosDef& it = d.cosmetics[i]; float x = 24 + (i / 14) * 412.0f, y = 92 + (i % 14) * 36.0f; int slot = SlotOf(it.kind);
        bool own = std::find(gNcProf.owned.begin(), gNcProf.owned.end(), it.id) != gNcProf.owned.end(), on = gNcProf.eq[slot] == it.id;
        DrawRectangle((int)x, (int)y, 400, 32, on ? Color{70, 60, 30, 230} : Color{34, 32, 28, 220}); DrawRectangle((int)x, (int)y, 5, 32, TIER[std::clamp(it.tier, 0, 3)]);
        Txt(it.name, x + 12, y + 8, 15, own ? INK : DIM);
        int price = d.cosPrice[std::clamp(it.tier, 0, 3)];
        std::string right = on ? "worn" : own ? "wear it" : it.tier >= 3 ? "crate only" : TextFormat("%d tokens", price);
        if (Button({x + 294, y + 3, 100, 26}, right.c_str(), !on && (own || (it.tier < 3 && S.tokens >= price)), 12)) { if (!own) { S.tokens -= price; gNcProf.owned.push_back(it.id); } gNcProf.eq[slot] = it.id; SaveTokens(); }
    }
    if (Button({SCREEN_W - 270.0f, SCREEN_H - 120.0f, 240, 44}, TextFormat("Open a crate (%d token%s)", d.cratePrice, d.cratePrice == 1 ? "" : "s"), S.tokens >= d.cratePrice, 15)) {
        S.tokens -= d.cratePrice; int tot = 0; for (int k = 0; k < 4; k++) tot += d.crateW[k]; int roll = GetRandomValue(0, tot - 1), tier = 0; for (; tier < 3; tier++) { roll -= d.crateW[tier]; if (roll < 0) break; }
        std::vector<int> pool; for (int i = 0; i < (int)d.cosmetics.size(); i++) if (d.cosmetics[i].tier == tier) pool.push_back(i);
        if (!pool.empty()) { const CosDef& it = d.cosmetics[pool[GetRandomValue(0, (int)pool.size() - 1)]]; bool dup = std::find(gNcProf.owned.begin(), gNcProf.owned.end(), it.id) != gNcProf.owned.end(); if (!dup) gNcProf.owned.push_back(it.id); gNcCrate = it.name + (dup ? " (a duplicate)" : ""); gNcCrateT = 3; }
        SaveTokens(); PlayCue("arc.deal");
    }
    if (gNcCrateT > 0) { gNcCrateT -= GetFrameTime(); DrawTextCenteredBold("From the crate: " + gNcCrate, SCREEN_W / 2.0f, SCREEN_H - 70.0f, 20, Color{255, 220, 120, 255}); }
    return Button({24, SCREEN_H - 60.0f, 160, 40}, "Back", true, 16) || IsKeyPressed(KEY_ESCAPE);
}
void NoclipMenuTick(float dt) { if (S.active && S.net) { Writer w; w.U8(0); Input in; WriteInput(in, w); S.net->Act(w); S.net->Update(GetTime(), dt); } }
int NoclipTokens() { LoadTokens(); return S.tokens; }

// --shots: 0 the Lobby from Lab Alpha's door, 1 Lab Alpha inside, 2 the Surface, 3 Pipe Dreams, 4 Lights Out with a Smiler,
// 5 the field map, 6 the portal desk mid-charge, 7 the Habitable Zone with a Hound, 8 low sanity (hallucinations), 9 the suburbs
void DebugNoclipShot(Game& g, int which) {
    StartNoclip(g, 2, 0); S.shot = true; S.help = which == 0;
    World& w = S.Wm; w.crew[0].cmds.push_back(Command{C_START_DAY}); w.Step();
    Player& p = w.crew[0]; Level& l0 = w.L(0); const LabPlan& lp = l0.labs[0];
    auto put = [&](int level, Vector3 at, float yaw, float pitch) { if (p.level != level) w.Transit(p, level, false, "shot"); p.p = at; p.yaw = yaw; S.camYaw = yaw; S.camPitch = pitch; for (auto& q : w.crew) w.SeeMap(q); };
    auto open = [&](const Level& lv) { for (int i = 0; i < 4000; i++) { int x = 4 + (i * 37) % (lv.w - 8), z = 4 + (i * 53) % (lv.h - 8); if (lv.At(x, z) == T_FLOOR && lv.At(x + 1, z) == T_FLOOR && lv.At(x + 2, z) == T_FLOOR && lv.At(x + 3, z) == T_FLOOR) return lv.Center(x, z); } return lv.start; };
    if (which == 0) { Vector3 d = l0.Center(lp.doorX, lp.doorZ + (lp.doorZ > lp.z1 ? 2 : -2)); put(0, d, lp.doorZ > lp.z1 ? PI / 2 : -PI / 2, 0); w.crew[1].p = Vector3Add(d, {2, 0, 2}); w.crew[1].level = 0; }
    if (which == 1) { put(0, lp.spots[LP_CRATE].at, 0.6f, -0.1f); w.crew[1].p = lp.spots[LP_DESK].at; }
    if (which == 2) { w.EndDay(true); WorldItem wi; for (int k = 0; k < 6; k++) { Loot l; l.def = k; l.value = 20 + k * 10; l.foundOn = 0; w.bay.push_back(l); } }
    if (which == 3) { Level& lv = w.L(2); put(2, open(lv), 0, 0); }
    if (which == 4) { Level& lv = w.L(6); Vector3 at = open(lv); put(6, at, 0, 0); Entity e; e.def = EntityIndex("smiler"); e.level = 6; e.p = Vector3Add(at, {6, 0, 0}); e.uid = 5; w.ents.push_back(e); }
    if (which == 5) { put(0, l0.start, 0, 0); for (int k = 0; k < 30; k++) { p.p = Vector3Add(l0.start, {(float)(k % 6) * 6, 0, (float)(k / 6) * 6}); w.SeeMap(p); } p.p = l0.start; S.map = true; }
    if (which == 6) { put(0, lp.spots[LP_DESK].at, 0, 0); w.Lab(0, 0)->charging = true; w.Lab(0, 0)->charge = 0.55f; S.panel = 0; }
    if (which == 7) { Level& lv = w.L(1); Vector3 at = open(lv); put(1, at, 0, 0); Entity e; e.def = EntityIndex("hound"); e.level = 1; e.p = Vector3Add(at, {4, 0, 0.5f}); e.uid = 6; e.yaw = PI * 0.62f; e.v = {-1.5f, 0, 0.8f}; w.ents.push_back(e); }
    if (which == 8) { Vector3 at = open(l0); put(0, at, 0, 0); p.sanity = 22; S.fakeT = 0; Hallucinate(0.1f); Entity e; e.hallucination = true; e.def = EntityIndex("faceling"); e.level = 0; e.p = Vector3Add(at, {5, 0, 0}); S.fakes.push_back(e); }
    if (which == 9) { Level& lv = w.L(9); Vector3 at = lv.start; for (int i = 0; i < lv.w * lv.h; i++) if (lv.flags[i] & CF_STREETLIGHT) { at = lv.Center(i % lv.w, i / lv.w); break; } put(9, Vector3Add(at, {1, 0, 1}), 0.4f, 0.05f); }
    if (which == 10 || which == 11) {   // the crew and the cast, close up (the hazmat suits; the Orange costume)
        Vector3 at = open(l0); put(0, at, 0, 0.02f);
        for (int k = 1; k <= 2; k++) { Player& q = w.crew[k]; q.level = 0; q.st = PS_ALIVE; q.p = Vector3Add(at, {3.2f, 0, k == 1 ? -0.75f : 0.75f}); q.yaw = PI + (k == 1 ? 0.3f : -0.3f); q.vel = which == 11 ? Vector3{-2.5f, 0, 0} : Vector3{}; }
        for (int i = 0; i < (int)D().cosmetics.size(); i++) if (D().cosmetics[i].id == "bureau_orange") w.crew[2].costume = i;
        if (which == 11) for (const char* id : {"faceling", "partygoer", "patient", "warden"}) { static int n = 0; Entity e; e.def = EntityIndex(id); e.level = 0; e.uid = 900 + n; e.p = Vector3Add(at, {5.5f, 0, -2.4f + (n % 4) * 1.6f}); e.yaw = PI; e.st = ES_IDLE; w.ents.push_back(e); n++; }
    }
    S.lastDay = w.day; ReadEvents(); StepCamera(1 / 60.0f);
}
