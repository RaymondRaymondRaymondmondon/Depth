// Fowl Play's scene (Scene::Fowl): first person at your stall, the marsh in front, the clubhouse room behind. The rules
// are fowl.cpp's; everything you do is an fp::Input (Gather) or an fp::Command (the room's panels), solo or networked.
#include "game.h"
#include "input.h"
#include "redtide_render.h"
#include "fowl.h"
#include "fowl_art.h"
#include "fowl_net.h"
#include "arcade_session.h"
#include "sound.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace fp;

// ---------------------------------------------------------------- the profile (tokens, the permanent locker)
namespace {
struct FowlProfile { int tokens = 0; bool ufoFirst = false; std::vector<std::string> owned; std::vector<std::pair<std::string, std::string>> eq; };   // eq: kind -> cosmetic id
FowlProfile gProf; bool gProfLoaded = false;
void LoadProf() { if (gProfLoaded) return; gProfLoaded = true; std::ifstream f("fowl_profile.txt"); std::string k; while (f >> k) { if (k == "tokens") f >> gProf.tokens; else if (k == "ufo") { int u; f >> u; gProf.ufoFirst = u; } else if (k == "own") { std::string s; f >> s; gProf.owned.push_back(s); } else if (k == "eq") { std::string a, b; f >> a >> b; gProf.eq.push_back({a, b}); } } }
void SaveProf() { std::ofstream f("fowl_profile.txt"); f << "tokens " << gProf.tokens << "\nufo " << (gProf.ufoFirst ? 1 : 0) << "\n"; for (auto& s : gProf.owned) f << "own " << s << "\n"; for (auto& e : gProf.eq) f << "eq " << e.first << " " << e.second << "\n"; }
bool Owns(const std::string& id) { return std::find(gProf.owned.begin(), gProf.owned.end(), id) != gProf.owned.end(); }
std::string Equipped(const std::string& kind) { for (auto& e : gProf.eq) if (e.first == kind) return e.second; return ""; }
void Equip(const std::string& kind, const std::string& id) { for (auto& e : gProf.eq) if (e.first == kind) { e.second = id; return; } gProf.eq.push_back({kind, id}); }
int CosIndex(const std::string& id) { for (int i = 0; i < (int)D().cosmetics.size(); i++) if (D().cosmetics[i].id == id) return i; return -1; }

const char* SYMBOL[5] = {"DUCK", "DOG", "GUN", "BELL", "ZAPPA"};
const Color STALL_COL[6] = {{200, 80, 60, 255}, {60, 120, 200, 255}, {80, 160, 90, 255}, {220, 170, 60, 255}, {150, 90, 190, 255}, {80, 170, 180, 255}};
const char* STATION_NAME[7] = {"the gun counter", "the mystery-gun machine", "the slots", "the scratch-offs", "the Slop Shop", "the trophy wall", "the bar"};

struct Pop { std::string text; Color col; float t; Vector3 at; bool world; };
struct FowlScene {
    bool active = false, shot = false, help = true;
    World Wm; World* hostW = nullptr; arcade::Session* net = nullptr; uint32_t evSeen = 0; int seenVersion = -1; bool helloSent = false; std::string netName; double snapAt = 0;
    int me = 0, skill = 1, mode = 0, bots = 5; std::vector<uint32_t> botRng;
    float camYaw = 0, camPitch = 0.2f, t = 0, acc = 0, kick = 0, flash = 0, megaFlash = 0, bell = 0;
    size_t evCursor = 0; std::vector<Pop> pops; std::deque<std::pair<std::string, float>> feed;
    int panel = -1; int slopTab = 0, sabPick = -1; float swapWheel = 0;
    int lastPhase = -1; bool tokensPaid = false; int tokensEarned = 0;
    std::vector<int> myWaveBirds;
    Camera3D cam{};
    std::vector<Command> outbox;
    struct Tracer { Vector3 a, b; Color c; float t; }; std::vector<Tracer> tracers;
} S;

World& W() { return S.hostW ? *S.hostW : S.Wm; }
Player& Me() { return W().players[std::clamp(S.me, 0, std::max(0, (int)W().players.size() - 1))]; }
void Send(Command c) { if (S.net) S.outbox.push_back(c); else Me().cmds.push_back(c); }
Color Mx(Color a, Color b, float k) { k = std::clamp(k, 0.0f, 1.0f); return {(unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k), (unsigned char)(a.b + (b.b - a.b) * k), 255}; }
std::string Money(int m) { return m < 0 ? TextFormat("-$%d", -m) : TextFormat("$%d", m); }
bool Zoomed() { const Player& p = Me(); if (!p.G().Has() || !p.in.alt) return false; if (D().guns[p.G().def].special == "scope") return true; for (int s = 0; s < SL_COUNT; s++) if (p.G().att[s] >= 0 && D().atts[p.G().att[s]].zoom > 1) return true; return false; }

// ---------------------------------------------------------------- events: pops, the feed, sound
void ReadEvents() {
    auto& ev = W().events; uint32_t fresh = W().evCount - (uint32_t)S.evCursor; S.evCursor = W().evCount;
    for (size_t k = ev.size() - std::min<size_t>(fresh, ev.size()); k < ev.size(); k++) {
        const Event& e = ev[k]; bool mine = e.who == S.me, byMe = e.by == S.me;
        switch (e.kind) {
            case EV_FLASH: {
                if (e.who == S.me) { S.flash = 1; S.kick = 1; }
                if (!S.shot && e.who >= 0 && e.by >= 0) { const GunDef& G = D().guns[std::clamp(e.by, 0, (int)D().guns.size() - 1)]; int k = G.type == "Shotgun" ? FPC_BOOM : G.type == "SMG" || G.type == "Machine gun" || G.special == "spinup" ? FPC_RATTLE : G.type == "Rifle" || G.type == "Sniper" || G.type == "Assault rifle" ? FPC_CRACK : G.special == "beam" || G.special == "chain" ? FPC_RAY : G.Fun() ? FPC_POP : FPC_ZAP; float pan = std::clamp((W().players[e.who].pos.x - Me().pos.x) / -12.0f, -1.0f, 1.0f); FowlCue(k, e.who == S.me ? 0.8f : 0.25f, pan); }
                if (e.a >= 99) S.megaFlash = 1; break; }
            case EV_KILL: if (mine) { int def = (int)e.a; const BirdDef& B = D().birds[std::clamp(def, 0, (int)D().birds.size() - 1)]; S.pops.push_back({B.pays >= 0 ? TextFormat("+$%d", B.pays) : TextFormat("-$%d", -B.pays), B.pays >= 0 ? Color{255, 240, 120, 255} : Color{255, 120, 100, 255}, S.t, e.at, true}); if (!S.shot) { const std::string& bid = B.id; FowlCue(bid == "goose" ? FPC_HONK : bid == "swan" ? FPC_HISS : bid == "phoenix" ? FPC_SHRIEK : bid == "armored" ? FPC_PING : bid == "clay" ? FPC_POP : FPC_QUACK, 0.7f, 0); if (Me().killSound >= 0) { const std::string& ks = D().cosmetics[Me().killSound].id; PlayCue(ks == "snd_quack" ? "arc.gull" : ks == "snd_kazoo" ? "arc.chat" : ks == "snd_horn" ? "arc.match" : ks == "snd_trombone" ? "arc.lose" : ks == "snd_boing" ? "arc.molt" : "arc.tick", 0.7f); } } } break;
            case EV_HIT: if (mine && e.by == -2) S.pops.push_back({"squeak", {250, 220, 120, 255}, S.t, e.at, true}); break;
            case EV_ARMOR: if (mine) { S.pops.push_back({"PING", {200, 220, 240, 255}, S.t, e.at, true}); if (!S.shot) FowlCue(FPC_PING, 0.6f, 0); } break;
            case EV_DOG_LAUGH: S.feed.push_back({"The whole wave got away. The dog laughs.", S.t}); if (!S.shot) FowlCue(FPC_LAUGH, 0.8f, 0); break;
            case EV_DOG_SHOT: if (!S.shot) FowlCue(FPC_SQUEAK, 0.7f, 0); S.feed.push_back({W().players[e.who].name + " shot the dog (-1 bird; it will remember)", S.t}); break;
            case EV_PERFECT: S.feed.push_back({W().players[e.who].name + ": a PERFECT WAVE (+$" + std::to_string(D().perfectBonus) + ")", S.t}); if (!S.shot) FowlCue(FPC_CHEER, mine ? 0.8f : 0.4f, 0); break;
            case EV_BELL: S.bell = 1.5f; if (!S.shot) FowlCue(FPC_BELL, 0.9f, 0); break;
            case EV_JACKPOT: S.feed.push_back({W().players[e.who].name + " hit the JACKPOT on the slots!", S.t}); if (!S.shot) FowlCue(FPC_FANFARE, 1.0f, 0); break;
            case EV_SLOT: if (mine && !S.shot) FowlCue(Me().slotWin > 0 ? FPC_DING : FPC_SQUEAK, 0.7f, 0); break;
            case EV_SCRATCH: if (mine) S.pops.push_back({e.a > 1 ? TextFormat("Scratched: $%d", (int)e.a) : e.by >= 0 ? "A winner!" : "Nothing this time", {240, 220, 140, 255}, S.t, {}, false}); break;
            case EV_MYSTERY: if (mine) S.pops.push_back({"The capsule: " + D().guns[std::clamp(e.by, 0, (int)D().guns.size() - 1)].name, {255, 200, 120, 255}, S.t, {}, false}); if (!S.shot) FowlCue(FPC_CRANK, 0.8f, 0); break;
            case EV_BUY: if (mine && !S.shot) PlayCue("ui.confirm", 0.6f); break;
            case EV_SABOTAGE: { const SlopItem& it = D().sabotage[std::clamp((int)e.a, 0, (int)D().sabotage.size() - 1)]; if (W().phase == PH_INTER && e.by >= 0) S.feed.push_back({W().players[e.by].name + " bought " + it.name + " for " + W().players[e.who].name, S.t}); else if (mine) S.pops.push_back({it.name + "!", {255, 140, 200, 255}, S.t, {}, false}); break; }
            case EV_COUNTER: if (mine) S.pops.push_back({"Countered", {160, 255, 180, 255}, S.t, {}, false}); break;
            case EV_JAM: if (mine) S.pops.push_back({"The swan bit your gun! (jammed)", {255, 160, 120, 255}, S.t, {}, false}); break;
            case EV_DROP: if (mine) S.pops.push_back({"Butter fingers!", {255, 230, 140, 255}, S.t, {}, false}); break;
            case EV_HATOFF: if (mine) S.pops.push_back({"Your hat!", {255, 200, 140, 255}, S.t, {}, false}); break;
            case EV_BOO: if (mine) { S.pops.push_back({"Boo!", {255, 120, 120, 255}, S.t, e.at, true}); if (!S.shot) FowlCue(FPC_BOO, 0.8f, 0); } break;
            case EV_UFO: if (!S.shot) FowlCue(FPC_HUM, 0.8f, 0); S.feed.push_back({"A UFO! Shoot it to free the ducks it takes.", S.t}); break;
            case EV_FREE: if (mine) S.pops.push_back({"Freed!", {150, 255, 170, 255}, S.t, e.at, true}); break;
            case EV_BONUS: S.feed.push_back({"BONUS WAVE: clays from the traps, everyone on the grey pistol", S.t}); break;
            case EV_RELOAD: if (mine && !S.shot) FowlCue(FPC_RELOAD, 0.6f, 0); break;
            case EV_WAVE: if (!S.shot) FowlCue(FPC_BARK, 0.5f, 0.2f); break;
            case EV_HONK: if (!S.shot) FowlCue(FPC_HONK, 0.9f, 0); break;
            case EV_ESCAPE: break;
            case EV_DECOY: if (mine) S.pops.push_back({"A decoy! -$10", {255, 140, 100, 255}, S.t, e.at, true}); break;
            default: break;
        }
    }
    while (S.feed.size() > 5) S.feed.pop_front();
    while (!S.feed.empty() && S.t - S.feed.front().second > 9) S.feed.pop_front();
    S.pops.erase(std::remove_if(S.pops.begin(), S.pops.end(), [](const Pop& p) { return S.t - p.t > 1.6f; }), S.pops.end());
}

// ---------------------------------------------------------------- input
void Gather(float dt) {
    Player& p = Me(); Input in; World& w = W();
    bool hunt = w.phase == PH_HUNT || w.phase == PH_BONUS;
    bool panelOpen = S.panel >= 0;
    Vector2 md = MouseLook(!S.shot && !panelOpen && w.phase != PH_OVER);
    float sens = 0.0024f * (Zoomed() ? 0.45f : 1.0f);
    if (p.reverseT > 0) md.x = -md.x;   // (Reverse Controls)
    S.camYaw -= md.x * sens; S.camPitch = std::clamp(S.camPitch - md.y * sens, -1.1f, 1.4f);
    // the bees, the ghost pepper: the aim won't sit still
    float sx = 0, sy = 0; if (p.beesT > 0) { sx += sinf(S.t * 7.3f) * 0.03f; sy += cosf(S.t * 5.9f) * 0.02f; } if (p.pepperT > 0) { sx += sinf(S.t * 3.1f) * 0.015f; sy += cosf(S.t * 2.7f) * 0.015f; }
    in.yaw = S.camYaw + sx; in.pitch = S.camPitch + sy;
    if (!panelOpen) {
        float fwd = (IsKeyDown(KEY_W) ? 1.0f : 0) - (IsKeyDown(KEY_S) ? 1.0f : 0), right = (IsKeyDown(KEY_D) ? 1.0f : 0) - (IsKeyDown(KEY_A) ? 1.0f : 0);
        if (p.room && !hunt) { Vector2 f{sinf(S.camYaw), cosf(S.camYaw)}, r{-cosf(S.camYaw), sinf(S.camYaw)}; in.moveX = f.x * fwd + r.x * right; in.moveZ = f.y * fwd + r.y * right; }
        else { in.moveX = -right; in.moveZ = 0; }   // (in the stall: step left and right; looking out over the marsh, right is -x)
        in.fire = IsMouseButtonDown(MOUSE_BUTTON_LEFT) && hunt; in.alt = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
        in.reload = IsKeyPressed(KEY_R); in.crouch = IsKeyDown(KEY_LEFT_CONTROL); in.wipe = IsKeyDown(KEY_V);
        in.lean = hunt ? (IsKeyDown(KEY_E) ? -1.0f : 0) + (IsKeyDown(KEY_Q) ? 1.0f : 0) : 0;
        float wheel = GetMouseWheelMove(); in.swap = IsKeyPressed(KEY_ONE) && p.hand != 0 || IsKeyPressed(KEY_TWO) && p.hand != 1 || IsKeyPressed(KEY_TAB) || wheel != 0;
        if (IsKeyPressed(KEY_H)) Send(Command{CMD_HOOK_SWAP});
        if (IsKeyPressed(KEY_T)) { for (int k = 0; k < 8; k++) if (p.scratchPocket[k] > 0 && p.scratchOpen < 0) { Command c; c.kind = CMD_SCRATCH_OPEN; c.a = k; Send(c); break; } }
        if (IsKeyPressed(KEY_F1)) S.help = !S.help;
        // the room: E at a station opens it
        if (w.phase == PH_INTER && IsKeyPressed(KEY_E)) { int st = w.NearStation(p); if (st >= 0) { S.panel = st; S.sabPick = -1; PlayCue("ui.click"); } else for (int k = 0; k < (int)w.floor.size(); k++) if (Vector2Distance({w.floor[k].p.x, w.floor[k].p.z}, {p.pos.x, p.pos.z}) < 1.6f) { Command c; c.kind = CMD_TAKE_FLOOR; c.a = k; Send(c); break; } }
    } else if (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_ESCAPE) || w.phase != PH_INTER) S.panel = -1;
    if (S.net) in.lagSteps = std::clamp((int)roundf((float)(GetTime() - S.snapAt) * 60) + 4, 0, HIST);
    p.in = in;
    (void)dt;
}

// ---------------------------------------------------------------- the room's panels
Rectangle PanelRect() { return {SCREEN_W / 2.0f - 380, 70, 760, 560}; }
void PanelFrame(const char* title, const char* sub) {
    Rectangle r = PanelRect(); DrawRectangleRounded(r, 0.04f, 6, Color{24, 18, 14, 238}); DrawRectangleRoundedLinesEx(r, 0.04f, 6, 3, Color{200, 160, 80, 255});
    DrawTextCenteredBold(title, r.x + r.width / 2, r.y + 12, 26, Color{255, 220, 150, 255});
    if (sub) DrawTextCentered(sub, r.x + r.width / 2, r.y + 44, 14, Color{210, 200, 180, 255});
    DrawTextCentered(TextFormat("You have %s   -   %d s left   -   E or Esc closes", Money(Me().money).c_str(), (int)std::max(0.0f, W().interLen - W().phaseT)), r.x + r.width / 2, r.y + r.height - 26, 14, Color{200, 190, 170, 255});
}
bool Row(Rectangle r, const std::string& left, const std::string& right, bool enabled, bool highlight = false) {
    bool hov = CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRec(r, highlight ? Color{90, 60, 30, 230} : hov && enabled ? Color{70, 54, 40, 230} : Color{40, 32, 26, 200});
    Txt(left, r.x + 8, r.y + (r.height - 15) / 2, 15, enabled ? Color{240, 230, 210, 255} : Color{130, 120, 110, 255});
    Txt(right, r.x + r.width - 8 - MeasureTxt(right, 15), r.y + (r.height - 15) / 2, 15, enabled ? Color{255, 220, 120, 255} : Color{130, 120, 110, 255});
    return hov && enabled && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
std::string GunStats(const GunDef& g) {
    return TextFormat("%s  dmg %d%s  mag %d  %s  range %dm", g.type.empty() ? "Fun gun" : g.type.c_str(), g.damage >= 99 ? 0 : g.damage, g.pellets > 1 ? TextFormat(" x%d", g.pellets) : "", g.mag, g.autoFire ? "auto" : "semi", (int)g.range);
}
void PanelGunCounter() {
    const World& w = W(); const Player& p = Me(); const ModeDef& md = w.M(); Rectangle r = PanelRect();
    PanelFrame("The gun counter", "Mr. Zappa: \"Every one a genuine toy. Click to buy; it goes straight in your hands.\"");
    bool noGuns = md.zapperOnly || md.mystery;
    // today's deal (only you see it)
    if (p.deal >= 0) {
        std::string what = p.dealIsAtt ? D().atts[p.deal].name : D().guns[p.deal].name; int price = (int)((p.dealIsAtt ? D().atts[p.deal].price : D().guns[p.deal].price) * 0.7f);
        if (Row({r.x + 20, r.y + 66, r.width - 40, 28}, "Today's deal, just for you: " + what + " (30% off)", Money(price), (!noGuns || p.dealIsAtt) && p.money >= price, true)) { Command c; c.kind = CMD_DEAL; c.b = p.hand; Send(c); }
    }
    // the guns, two columns
    float y0 = r.y + 104; int n = 0; std::string hover;
    for (int i = 0; i < (int)D().guns.size(); i++) {
        const GunDef& g = D().guns[i]; if (g.price <= 0) continue;
        float x = r.x + 20 + (n / 15) * 250, y = y0 + (n % 15) * 26; n++;
        bool own = p.guns[0].def == i || p.guns[1].def == i || p.hook.def == i;
        Rectangle rr{x, y, 240, 24};
        if (Row(rr, g.name + (own ? " (yours)" : ""), Money(g.price), !noGuns && p.money >= g.price, g.Fun())) { Command c; c.kind = CMD_BUY_GUN; c.a = i; Send(c); }
        if (CheckCollisionPointRec(GetMousePosition(), rr)) hover = g.name + ": " + GunStats(g) + ". " + g.note;
    }
    // the pegboard: attachments for the gun in your hands
    float ax = r.x + 520; Txt("The pegboard", ax, y0 - 2, 16, Color{255, 220, 150, 255});
    Txt(p.G().Has() ? ("for your " + D().guns[p.G().def].name).c_str() : "", ax, y0 + 16, 13, Color{200, 190, 170, 255});
    int k = 0;
    for (int i = 0; i < (int)D().atts.size(); i++) {
        const AttDef& a = D().atts[i]; if (a.slot == "cosmetic") continue;
        bool on = p.G().Has() && a.slotIdx >= 0 && p.G().att[a.slotIdx] == i;
        Rectangle rr{ax, y0 + 36 + k * 26.0f, 220, 24}; k++;
        if (Row(rr, a.name + (on ? " (on)" : ""), Money(a.price), p.money >= a.price && !on && (a.slotIdx >= 0 ? p.G().Has() : true))) { Command c; c.kind = CMD_BUY_ATT; c.a = i; c.b = p.hand; Send(c); }
        if (CheckCollisionPointRec(GetMousePosition(), rr)) hover = a.name + " (" + a.slot + "): " + a.note;
    }
    if (p.G().Has() && D().guns[p.G().def].ammoPrice > 0) { if (Row({ax, y0 + 36 + k * 26.0f + 8, 220, 24}, "Flares x4", Money(D().guns[p.G().def].ammoPrice * 4), p.money >= D().guns[p.G().def].ammoPrice * 4)) { Command c; c.kind = CMD_BUY_AMMO; c.a = 4; Send(c); } }
    if (noGuns) DrawTextCentered(md.zapperOnly ? "Zapper Only: the counter sells attachments, nothing else" : "Mystery Box: no gun counter; the machine is free once a break", r.x + r.width / 2, r.y + r.height - 64, 15, Color{255, 180, 120, 255});
    if (!hover.empty()) DrawWrapped(hover.c_str(), {r.x + 20, r.y + r.height - 70, r.width - 40, 40}, 14, Color{220, 220, 200, 255});
}
void PanelMystery() {
    const Player& p = Me(); Rectangle r = PanelRect(); const ModeDef& md = W().M();
    PanelFrame("The mystery-gun machine", "One crank: a plastic capsule with a random gun (any of them, from the Bread Gun to the Mega Zapper)");
    int price = p.mysteryFree ? 0 : D().mysteryPrice;
    DrawCircle((int)(r.x + r.width / 2), (int)(r.y + 230), 110, Color{200, 40, 40, 255}); DrawCircle((int)(r.x + r.width / 2), (int)(r.y + 210), 80, Color{220, 230, 240, 200});
    for (int k = 0; k < 12; k++) DrawCircle((int)(r.x + r.width / 2 - 50 + (k % 4) * 33), (int)(r.y + 180 + (k / 4) * 28), 13, k % 3 == 0 ? Color{240, 200, 60, 255} : k % 3 == 1 ? Color{70, 170, 230, 255} : Color{240, 120, 160, 255});
    bool can = !md.zapperOnly && (p.money >= price || md.practice);
    if (Button({r.x + r.width / 2 - 120, r.y + 380, 240, 46}, price ? TextFormat("Crank it (%s)", Money(price).c_str()) : "Crank it (free)", can, 18)) Send(Command{CMD_MYSTERY});
    DrawTextCentered("The gun goes in your hands; your other gun goes to the stall hook (or the floor, if the hook is full).", r.x + r.width / 2, r.y + 440, 14, Color{210, 200, 180, 255});
    if (W().interLen - W().phaseT < 10) DrawTextCentered("A crank now plays a drumroll; the capsule opens on the porch.", r.x + r.width / 2, r.y + 462, 14, Color{255, 200, 120, 255});
}
void PanelSlots() {
    const Player& p = Me(); Rectangle r = PanelRect();
    PanelFrame("The slots", "Two of a kind returns the bet; three ducks, dogs or guns pay a gun; three bells x12; three Zappa logos x30");
    float cx = r.x + r.width / 2, cy = r.y + 200;
    DrawRectangleRounded({cx - 230, cy - 70, 460, 140}, 0.1f, 6, Color{170, 30, 40, 255});
    for (int k = 0; k < 3; k++) {
        Rectangle reel{cx - 210 + k * 145.0f, cy - 52, 130, 104}; DrawRectangleRec(reel, Color{250, 244, 230, 255});
        int sym = p.slotT > 0 && p.slotT > 0.4f + k * 0.5f ? (int)(S.t * 14 + k * 3) % 5 : p.reels[k];
        DrawTextCenteredBold(SYMBOL[sym], reel.x + 65, reel.y + 40, 24, sym == 4 ? Color{200, 30, 30, 255} : Color{40, 40, 50, 255});
    }
    if (p.slotT <= 0 && p.slotBet > 0) { std::string res = p.slotWin >= 1000 ? "A gun: " + D().guns[p.slotWin - 1000].name : p.slotWin > 0 ? "Pays " + Money(p.slotWin) : "Nothing"; DrawTextCenteredBold(res, cx, cy + 86, 22, p.slotWin > 0 ? Color{255, 230, 120, 255} : Color{200, 190, 180, 255}); }
    for (int i = 0; i < (int)D().slotBets.size(); i++) { int bet = D().slotBets[i]; if (Button({cx - 230 + i * 160.0f, cy + 140, 140, 44}, TextFormat("Pull for $%d", bet), p.slotT <= 0 && p.money >= bet, 16)) { Command c; c.kind = CMD_SLOT; c.a = bet; Send(c); PlayCue("arc.turn", 0.6f); } }
    DrawTextCentered(TextFormat("This break: won %s, lost %s", Money(p.gambleWon).c_str(), Money(p.gambleLost).c_str()), cx, cy + 210, 14, Color{210, 200, 180, 255});
}
void PanelScratch() {
    const Player& p = Me(); Rectangle r = PanelRect();
    PanelFrame("The scratch-off counter", "Buy tickets for your pocket; scratch them now, or on the porch between waves (T)");
    for (int i = 0; i < (int)D().scratch.size() && i < 8; i++) {
        const Scratch& s = D().scratch[i]; float y = r.y + 80 + i * 56;
        std::string top = s.prizes.empty() ? "" : s.prizes.back().first; if (top == "dog") top = "the dog fetches yours first and finds you a golden duck"; else if (top == "slop") top = "a Slop Shop item"; else if (top == "gun+200") top = "a gun plus $200"; else top = "$" + top;
        if (Row({r.x + 20, y, 440, 46}, s.name + "  (1 in " + std::to_string(s.odds) + "; top: " + top + ")", Money(s.price), p.money >= s.price)) { Command c; c.kind = CMD_SCRATCH; c.a = i; Send(c); }
        Txt(TextFormat("in your pocket: %d", p.scratchPocket[i]), r.x + 480, y + 14, 15, Color{220, 210, 190, 255});
        if (p.scratchPocket[i] > 0 && Button({r.x + 620, y + 6, 110, 34}, "Scratch", p.scratchOpen < 0, 15)) { Command c; c.kind = CMD_SCRATCH_OPEN; c.a = i; Send(c); }
    }
    if (p.scratchOpen >= 0) { float k = 1 - p.scratchT / D().scratchTime; DrawRectangle((int)(r.x + 120), (int)(r.y + 380), (int)(520 * k), 30, Color{220, 200, 120, 255}); DrawRectangleLines((int)(r.x + 120), (int)(r.y + 380), 520, 30, Color{240, 220, 160, 255}); DrawTextCentered("scratching...", r.x + r.width / 2, r.y + 386, 16, BLACK); }
}
void PanelSlop() {
    const World& w = W(); const Player& p = Me(); Rectangle r = PanelRect();
    PanelFrame("The Slop Shop", "The raccoon: \"Look sharp, look silly, or ruin somebody's afternoon.\"");
    const char* TABS[3] = {"Cosmetics", "Sabotage", "Counters"};
    for (int k = 0; k < 3; k++) if (Button({r.x + 150 + k * 160.0f, r.y + 66, 150, 30}, TABS[k], true, 15)) { S.slopTab = k; S.sabPick = -1; }
    float y0 = r.y + 110;
    if (S.slopTab == 0) {
        int n2 = 0; for (int i = 0; i < (int)D().cosmetics.size(); i++) { const SlopItem& it = D().cosmetics[i]; if (it.crateOnly) continue; float x = r.x + 20 + (n2 / 13) * 242, y = y0 + (n2 % 13) * 27; n2++; bool on = p.hat == i || p.paint == i || p.dance == i || p.flag == i || p.dogCoat == i || p.killSound == i || p.tracer == i; if (Row({x, y, 234, 25}, it.name + (on ? " *" : ""), Money(it.price), p.money >= it.price && !on)) { Command c; c.kind = CMD_COSMETIC; c.a = i; Send(c); } }
        DrawTextCentered("Match-only: tokens buy the permanent ones in the arcade.", r.x + r.width / 2, r.y + r.height - 60, 14, Color{210, 200, 180, 255});
    } else if (S.slopTab == 1) {
        if (S.sabPick < 0) {
            for (int i = 0; i < (int)D().sabotage.size(); i++) { const SlopItem& it = D().sabotage[i]; float x = r.x + 20 + (i / 7) * 365, y = y0 + (i % 7) * 52; if (Row({x, y, 350, 46}, it.name, Money(it.price) + "+", !w.M().practice)) S.sabPick = i; DrawWrapped(it.effect.c_str(), {x + 8, y + 26, 300, 20}, 12, Color{200, 190, 170, 255}); }
            DrawTextCentered("It lands on their next hunt. Everyone sees who did it, and every one has a counter.", r.x + r.width / 2, r.y + r.height - 60, 14, Color{210, 200, 180, 255});
        } else {
            const SlopItem& it = D().sabotage[S.sabPick];
            DrawTextCenteredBold(it.name + ": on whom?", r.x + r.width / 2, y0, 20, Color{255, 200, 220, 255});
            DrawTextCentered(it.effect + "   (counter: " + it.counter + ")", r.x + r.width / 2, y0 + 28, 14, Color{210, 200, 180, 255});
            int k = 0;
            for (const auto& q : w.players) {
                if (q.id == p.id) continue; bool mate = w.M().teams && q.team == p.team; int price = w.SabPrice(S.sabPick, q.id); bool full = (int)q.incoming.size() >= D().perTarget;
                std::string lbl = q.name + (q.id == w.Leader() ? " (the leader: +50%)" : "") + (full ? " - sold out" : "") + (mate ? " - your team" : "");
                if (Row({r.x + 160, y0 + 64 + k * 40.0f, 440, 34}, lbl, Money(price), !full && !mate && p.money >= price)) { Command c; c.kind = CMD_SABOTAGE; c.a = S.sabPick; c.b = q.id; Send(c); S.sabPick = -1; }
                k++;
            }
            if (Button({r.x + r.width / 2 - 60, y0 + 64 + k * 40.0f + 10, 120, 30}, "Back", true, 14)) S.sabPick = -1;
        }
    } else {
        Txt("Coming your way next hunt:", r.x + 30, y0, 16, Color{255, 200, 220, 255});
        int k = 0; for (const auto& s : p.incoming) { const SlopItem& it = D().sabotage[s.item]; Txt(TextFormat("%s from %s%s", it.name.c_str(), W().players[s.by].name.c_str(), s.countered ? " (countered)" : ""), r.x + 40, y0 + 24 + k * 20.0f, 15, s.countered ? Color{160, 255, 180, 255} : Color{255, 160, 160, 255}); k++; }
        if (!k) Txt("nothing (yet)", r.x + 40, y0 + 24, 15, Color{180, 180, 170, 255});
        float y = y0 + 40 + std::max(1, k) * 20.0f; int n = 0;
        for (int i = 0; i < (int)D().sabotage.size(); i++) { const SlopItem& it = D().sabotage[i]; if (it.counterPrice <= 0) continue; bool have = (p.counters >> i) & 1; if (Row({r.x + 20 + (n % 2) * 365.0f, y + (n / 2) * 32.0f, 350, 28}, it.counter + "  vs " + it.name + (have ? " (bought)" : ""), Money(it.counterPrice), !have && p.money >= it.counterPrice)) { Command c; c.kind = CMD_COUNTER; c.a = i; Send(c); } n++; }
        DrawTextCentered("Buy a counter before the hunt and that sabotage does nothing. (Grip tape is on the pegboard.)", r.x + r.width / 2, r.y + r.height - 60, 14, Color{210, 200, 180, 255});
    }
}
void PanelTrophies() {
    const World& w = W(); Rectangle r = PanelRect(); PanelFrame("The trophy wall", "Every shooter's best round so far, and the club's records");
    int k = 0; int clubBest = 0; std::string clubWho;
    for (const auto& p : w.players) { int best = 0; for (int b : p.best) best = std::max(best, b); if (best > clubBest) { clubBest = best; clubWho = p.name; } Txt(TextFormat("%-12s best round %2d birds   accuracy %d%%   perfect waves %d", p.name.c_str(), best, p.shots ? p.hits * 100 / p.shots : 0, p.perfect), r.x + 60, r.y + 90 + k * 30.0f, 17, STALL_COL[p.stall % 6]); k++; }
    if (clubBest) DrawTextCenteredBold(TextFormat("Club record this match: %d birds in a round (%s)", clubBest, clubWho.c_str()), r.x + r.width / 2, r.y + 330, 18, Color{255, 220, 140, 255});
}
void PanelBar() { Rectangle r = PanelRect(); PanelFrame("The bar", nullptr); DrawTextCentered("Nothing for sale. The dog is asleep under it.", r.x + r.width / 2, r.y + 200, 20, Color{230, 210, 180, 255}); }

// ---------------------------------------------------------------- the HUD
void DrawStrip() {   // the old screen's strip: round, bullets, the birds of the wave, the score
    const World& w = W(); const Player& p = Me(); float y = SCREEN_H - 64.0f;
    DrawRectangle(0, (int)y, SCREEN_W, 64, Color{12, 12, 16, 230}); DrawRectangle(0, (int)y, SCREEN_W, 3, Color{60, 200, 90, 255});
    TxtBold(TextFormat("R=%d", w.round), 24, y + 18, 26, Color{120, 240, 120, 255});
    // bullets
    const Gun& g = p.G(); Rectangle box{110, y + 10, 200, 44}; DrawRectangleLinesEx(box, 2, Color{60, 200, 90, 255}); Txt("SHOT", box.x + 6, box.y + 26, 13, Color{120, 240, 120, 255});
    if (g.Has()) { int mag = g.MagSize(); int show = std::min(mag, 12); for (int k = 0; k < show; k++) { bool full = k < (int)ceilf((float)g.ammo * show / std::max(1, mag)); DrawRectangle((int)(box.x + 50 + k * 12), (int)(box.y + 8), 7, 16, full ? Color{250, 210, 80, 255} : Color{60, 60, 66, 255}); } if (mag > 12) Txt(TextFormat("%d", g.ammo), box.x + 50, box.y + 26, 13, Color{250, 210, 80, 255}); if (g.reserve > 0 || D().guns[g.def].ammoPrice > 0) Txt(TextFormat("+%d", g.reserve), box.x + 160, box.y + 26, 13, Color{200, 200, 180, 255}); if (g.reloadT > 0) Txt("RELOAD", box.x + 110, box.y + 26, 13, Color{255, 140, 100, 255}); }
    // the wave's birds: little silhouettes that fill red as they're hit
    Rectangle hb{330, y + 10, 560, 44}; DrawRectangleLinesEx(hb, 2, Color{60, 200, 90, 255}); Txt("HIT", hb.x + 6, hb.y + 26, 13, Color{120, 240, 120, 255});
    int k = 0; int wv = std::max(0, w.wave - 1);
    for (const auto& b : w.birds) { if (b.wave != wv || D().birds[b.def].decoy || D().birds[b.def].clay || k >= 22) continue; float x = hb.x + 44 + k * 22.0f; Color c = b.killer == S.me ? Color{240, 60, 60, 255} : b.killer >= 0 ? Color{140, 140, 150, 255} : b.escaped ? Color{50, 50, 56, 255} : WHITE; DrawTriangle({x, y + 30}, {x + 16, y + 22}, {x + 8, y + 18}, c); DrawCircle((int)x + 14, (int)y + 20, 4, c); k++; }
    // score
    TxtBold(TextFormat("%06d", p.birds * 100), SCREEN_W - 260.0f, y + 12, 28, WHITE); Txt("SCORE", SCREEN_W - 260.0f, y + 42, 13, Color{120, 240, 120, 255});
    TxtBold(Money(p.money), SCREEN_W - 110.0f, y + 16, 22, Color{255, 220, 120, 255});
}
void DrawBoard() {   // every stall: name, birds, money, what's on them
    const World& w = W(); float x0 = SCREEN_W / 2.0f - 3 * 150;
    for (const auto& p : w.players) {
        float x = x0 + p.stall * 150; Rectangle r{x + 4, 8, 142, 52}; bool mine = p.id == S.me;
        DrawRectangleRounded(r, 0.2f, 4, Color{20, 18, 16, (unsigned char)(mine ? 230 : 190)}); DrawRectangle((int)r.x, (int)r.y, 6, 52, STALL_COL[p.stall % 6]);
        Txt(TextFormat("%d %s%s", p.stall + 1, p.name.c_str(), p.menace ? " (Menace)" : ""), r.x + 10, r.y + 4, 14, mine ? WHITE : Color{220, 220, 210, 255});
        TxtBold(TextFormat("%d", w.Score(p)), r.x + 10, r.y + 22, 22, STALL_COL[p.stall % 6]);
        Txt(Money(p.money), r.x + 60, r.y + 26, 15, Color{255, 220, 120, 255});
        int k = 0; for (const auto& s : (w.phase == PH_INTER ? p.incoming : p.active)) { if (s.countered) continue; DrawCircle((int)(r.x + 132 - k * 12), (int)(r.y + 42), 5, Color{255, 120, 200, 255}); k++; }
    }
}
void DrawHud(Game& g) {
    World& w = W(); const Player& p = Me(); float cx = SCREEN_W / 2.0f, cy = SCREEN_H / 2.0f;
    // the sabotage on your screen
    if (p.smudgeT > 0) { for (int k = 0; k < 9; k++) { float a = k * 0.7f; DrawCircleGradient((int)(cx + cosf(a) * 220 + sinf(k * 3.1f) * 60), (int)(cy + sinf(a * 1.7f) * 160), 150 + 30 * (k % 3), Color{170, 150, 110, (unsigned char)(120 * (1 - p.wipeT / 2))}, Color{170, 150, 110, 0}); } DrawTextCentered("Lens smudge: hold V to wipe it", cx, SCREEN_H - 130.0f, 15, Color{255, 230, 180, 255}); }
    if (p.pepperT > 0) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{255, 40, 20, (unsigned char)(70 + 30 * sinf(S.t * 5))});
    if (p.reverseT > 0) DrawTextCentered(TextFormat("REVERSE CONTROLS  %d", (int)ceilf(p.reverseT)), cx, 120, 18, Color{255, 140, 200, 255});
    if (p.bagpipe) DrawTextCentered("A bagpiper is playing beside your stall", cx, 140, 14, Color{220, 200, 180, 255});
    if (S.megaFlash > 0) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{255, 255, 255, (unsigned char)(255 * S.megaFlash)});
    if (S.flash > 0.5f) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{255, 255, 255, 26});   // (a light-gun flash, one frame)
    if (Zoomed()) { DrawRing({cx, cy}, 250, 900, 0, 360, 48, BLACK); DrawLine((int)cx - 250, (int)cy, (int)cx + 250, (int)cy, BLACK); DrawLine((int)cx, (int)cy - 250, (int)cx, (int)cy + 250, BLACK); }
    DrawBoard();
    for (size_t i = 0; i < S.feed.size(); i++) Txt(S.feed[i].first, 20, 76 + i * 20.0f, 15, Color{240, 236, 220, 255});
    // pops: in the sky where the bird fell, or under the crosshair
    for (const auto& pp : S.pops) { float a = S.t - pp.t; Vector2 at{cx, cy + 60 - a * 30}; if (pp.world) { at = GetWorldToScreen(pp.at, S.cam); at.y -= a * 30; } DrawTextCenteredBold(pp.text, at.x, at.y, 20, Fade(pp.col, 1 - a / 1.6f)); }
    if (w.phase == PH_HUNT || w.phase == PH_BONUS) {
        // the light-gun reticle
        DrawCircleLines((int)cx, (int)cy, 9, WHITE); DrawLine((int)cx - 16, (int)cy, (int)cx - 6, (int)cy, WHITE); DrawLine((int)cx + 6, (int)cy, (int)cx + 16, (int)cy, WHITE); DrawLine((int)cx, (int)cy - 16, (int)cx, (int)cy - 6, WHITE); DrawLine((int)cx, (int)cy + 6, (int)cx, (int)cy + 16, WHITE);
        if (p.G().Has() && p.G().def >= 0 && D().guns[p.G().def].special == "spinup" && p.G().spin < 1) DrawRing({cx, cy}, 20, 24, -90, -90 + 360 * p.G().spin, 30, Color{250, 210, 80, 255});
        float left = (w.phase == PH_HUNT ? D().hunt : D().bonusLength) - w.phaseT;
        DrawTextCenteredBold(TextFormat("%d", (int)ceilf(std::max(0.0f, left))), cx, 70, 26, left < 10 ? Color{255, 140, 100, 255} : WHITE);
        if (p.gunDropT > 0) DrawTextCentered("You dropped it! Picking it up...", cx, cy + 40, 18, Color{255, 230, 140, 255});
        if (p.jamT > 0) DrawTextCentered(TextFormat("JAMMED %.1f", p.jamT), cx, cy + 40, 18, Color{255, 140, 120, 255});
        if (p.rubberLeft > 0) DrawTextCentered("Rubber ducky rounds! Reload early (R) for real ones", cx, cy + 60, 15, Color{255, 230, 140, 255});
        if (p.cardboardT > 0) DrawTextCentered("A cardboard moose! Shoot it three times", cx, cy + 80, 15, Color{230, 200, 150, 255});
        if (w.round >= D().goldenHour && w.phase == PH_HUNT) DrawTextCentered("THE GOLDEN HOUR: every bird pays double", cx, 100, 16, Color{255, 210, 100, 255});
        DrawStrip();
    }
    if (w.phase == PH_TALLY) {
        Rectangle r{cx - 260, 110, 520, 60 + 32.0f * w.players.size()}; DrawRectangleRounded(r, 0.05f, 6, Color{20, 16, 12, 220});
        DrawTextCenteredBold(TextFormat("Round %d", w.round), cx, r.y + 12, 24, Color{255, 220, 150, 255});
        std::vector<const Player*> order; for (const auto& q : w.players) order.push_back(&q); std::sort(order.begin(), order.end(), [&](auto a, auto b) { return w.Score(*a) > w.Score(*b); });
        for (size_t i = 0; i < order.size(); i++) { const Player& q = *order[i]; Txt(TextFormat("%d. %-12s +%d birds   +%s   total %d", (int)i + 1, q.name.c_str(), q.roundBirds, Money(q.roundMoney).c_str(), w.Score(q)), r.x + 30, r.y + 50 + i * 32.0f, 18, q.id == S.me ? WHITE : STALL_COL[q.stall % 6]); }
    }
    if (w.phase == PH_INTER) {
        float left = w.interLen - w.phaseT;
        DrawTextCenteredBold(TextFormat("The clubhouse is open: %d s", (int)ceilf(left)), cx, 70, 22, left < D().bell ? Color{255, 140, 100, 255} : Color{255, 230, 180, 255});
        if (S.bell > 0) DrawTextCenteredBold("DING DING  - ten seconds!", cx, 100, 24, Color{255, 200, 80, 255});
        if (p.deal >= 0 && S.panel < 0) DrawTextCentered(TextFormat("Mr. Zappa has a deal just for you: %s, 30%% off", p.dealIsAtt ? D().atts[p.deal].name.c_str() : D().guns[p.deal].name.c_str()), cx, 128, 14, Color{255, 220, 150, 255});
        int st = w.NearStation(p);
        if (S.panel < 0) {
            if (st >= 0) DrawTextCenteredBold(TextFormat("E: %s", STATION_NAME[st]), cx, cy + 40, 20, WHITE);
            for (const auto& f : w.floor) if (Vector2Distance({f.p.x, f.p.z}, {p.pos.x, p.pos.z}) < 1.6f) { DrawTextCenteredBold(TextFormat("E: pick up the %s", D().guns[f.g.def].name.c_str()), cx, cy + 66, 18, WHITE); break; }
            DrawCircle((int)cx, (int)cy, 3, WHITE);
        } else switch (S.panel) { case 0: PanelGunCounter(); break; case 1: PanelMystery(); break; case 2: PanelSlots(); break; case 3: PanelScratch(); break; case 4: PanelSlop(); break; case 5: PanelTrophies(); break; default: PanelBar(); break; }
        if (S.panel < 0) {
            // your guns: in hand, the other, the hook
            Txt(TextFormat("Hands: %s / %s    Hook: %s    (1/2 or wheel: swap, H: the hook)", p.guns[0].Has() ? D().guns[p.guns[0].def].name.c_str() : "-", p.guns[1].Has() ? D().guns[p.guns[1].def].name.c_str() : "-", p.hook.Has() ? D().guns[p.hook.def].name.c_str() : "empty"), 20, SCREEN_H - 30.0f, 15, Color{230, 220, 200, 255});
        }
    }
    if (p.scratchOpen >= 0 && S.panel != 3) { float k = 1 - p.scratchT / D().scratchTime; DrawRectangle((int)cx - 100, SCREEN_H - 110, (int)(200 * k), 10, Color{220, 200, 120, 255}); DrawTextCentered("scratching a ticket...", cx, SCREEN_H - 128.0f, 13, Color{240, 230, 180, 255}); }
    if (S.help && (w.phase == PH_HUNT || w.phase == PH_INTER) && S.panel < 0) {
        Rectangle r{SCREEN_W - 330.0f, 76, 316, 210}; DrawRectangleRounded(r, 0.06f, 6, Color{10, 10, 14, 180});
        const char* L[] = {"Mouse aims; left mouse shoots", "Right mouse: scope / both barrels", "A / D: step in your stall; Ctrl crouch; Q/E lean", "R reload, 1/2 or wheel swap, H the hook", "Between hunts: WASD walks the room, E uses", "Shooting the dog costs you a bird", "Decoys (no eyes) cost $10", "T scratches a ticket from your pocket", "F1 hides this"};
        for (int i = 0; i < 9; i++) Txt(L[i], r.x + 10, r.y + 8 + i * 22, 14, Color{230, 230, 220, 255});
    }
    if (w.phase == PH_OVER) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{0, 0, 0, 150});
        std::vector<const Player*> order; for (const auto& q : w.players) order.push_back(&q); std::sort(order.begin(), order.end(), [&](auto a, auto b) { return w.Score(*a) > w.Score(*b); });
        DrawTextCenteredBold(order.size() && order[0]->id == S.me ? "YOU WIN THE DOG'S TROPHY" : TextFormat("%s WINS THE DOG'S TROPHY", order.size() ? order[0]->name.c_str() : "?"), cx, 80, 34, Color{255, 220, 120, 255});
        for (size_t i = 0; i < order.size(); i++) { const Player& q = *order[i]; Txt(TextFormat("%d. %-11s birds %3d  acc %2d%%  money %s  gambling %+d  sabotage %d/%d  %s", (int)i + 1, q.name.c_str(), q.birds, q.shots ? q.hits * 100 / q.shots : 0, Money(q.money).c_str(), q.gambleWon - q.gambleLost, q.sabGiven, q.sabTaken, q.mostExpensive >= 0 ? TextFormat("(%s: %d birds)", D().guns[q.mostExpensive].name.c_str(), q.mostExpensiveBirds) : ""), cx - 470, 150 + i * 34.0f, 17, q.id == S.me ? WHITE : STALL_COL[q.stall % 6]); }
        if (S.tokensEarned > 0) DrawTextCenteredBold(TextFormat("+%d arcade tokens (you have %d)", S.tokensEarned, gProf.tokens), cx, 380, 20, Color{150, 230, 255, 255});
        if (S.net) {
            if (S.net->role == arcade::R_HOST) { if (Button({cx - 230, 450, 200, 44}, "Rematch", true, 16)) { std::string why; S.net->Rematch(&why); S.tokensPaid = false; S.tokensEarned = 0; return; } }
            else DrawTextCentered("Waiting for the host", cx, 462, 15, WHITE);
            if (Button({cx + 30, 450, 200, 44}, S.net->role == arcade::R_HOST ? "Back to the lobby" : "Leave", true, 16)) { LeaveFowl(g); return; }
        } else {
            if (Button({cx - 230, 450, 200, 44}, "Again", true, 16)) { StartFowl(g, S.mode, S.bots, S.skill); return; }
            if (Button({cx + 30, 450, 200, 44}, "Back to the arcade", true, 16)) { LeaveFowl(g); return; }
        }
    }
}

// ---------------------------------------------------------------- the camera and the world
void StepCamera(float dt) {
    Player& p = Me(); S.kick = std::max(0.0f, S.kick - dt * 6);
    S.cam.position = p.Eye();
    if (W().phase == PH_TALLY) { S.camPitch += (-0.12f - S.camPitch) * std::min(1.0f, dt * 2); }
    float kickUp = S.kick * (p.G().Has() ? D().guns[p.G().def].kick : 1) * 0.012f;
    Vector3 look{cosf(S.camPitch + kickUp) * sinf(S.camYaw), sinf(S.camPitch + kickUp), cosf(S.camPitch + kickUp) * cosf(S.camYaw)};
    if (p.beesT > 0) look = Vector3Add(look, {sinf(S.t * 7.3f) * 0.03f, cosf(S.t * 5.9f) * 0.02f, 0});
    if (p.pepperT > 0) { look.x += sinf(S.t * 3.1f) * 0.02f; look.y += cosf(S.t * 2.7f) * 0.02f; }
    S.cam.target = Vector3Add(S.cam.position, look); S.cam.up = {sinf(p.lean * 0.15f) * cosf(S.camYaw), 1, -sinf(p.lean * 0.15f) * sinf(S.camYaw)};
    float fov = 70; if (Zoomed()) { float z = 2; if (p.G().Has() && D().guns[p.G().def].special == "scope") z = 4; fov /= z; }
    S.cam.fovy = fov; S.cam.projection = CAMERA_PERSPECTIVE;
}
void DrawViewmodel() {
    Player& p = Me(); if (!p.G().Has() || p.room || Zoomed()) return;
    if (p.gunDropT > 0) return;
    Vector3 f = Vector3Normalize(Vector3Subtract(S.cam.target, S.cam.position)), r = Vector3Normalize(Vector3CrossProduct(f, {0, 1, 0})), u = Vector3CrossProduct(r, f);
    float reload = p.G().reloadT > 0 ? sinf(std::min(1.0f, p.G().reloadT / std::max(0.1f, p.G().ReloadTime())) * PI) : 0;
    float kick = S.kick * 0.06f, jam = p.jamT > 0 ? sinf(S.t * 40) * 0.01f : 0;
    float sway = sinf(S.t * 1.7f) * 0.004f;
    Vector3 at = Vector3Add(S.cam.position, Vector3Add(Vector3Scale(f, 0.5f - kick), Vector3Add(Vector3Scale(r, 0.17f + jam + sway), Vector3Scale(u, -0.17f - reload * 0.15f))));
    // the gun's frame: barrel along the look, tipped up by the kick and rolled through a reload
    Matrix base = {r.x, u.x, f.x, at.x, r.y, u.y, f.y, at.y, r.z, u.z, f.z, at.z, 0, 0, 0, 1};
    Matrix m = MatrixMultiply(MatrixMultiply(MatrixRotateX(-kick * 3), MatrixRotateZ(reload * 0.8f)), base);
    fpart::DrawToyGun(p.G().def, m, p.paint, S.t * (p.G().spin > 0 ? 30 * p.G().spin : 0), 0.62f);
}
void Render() {
    World& w = W(); bool night = w.M().night;
    rt::SceneLight L;
    float dusk = w.phase == PH_INTER ? 0.3f : 0.0f;
    L.fog = night ? Color{14, 16, 24, 255} : Color{214, 170, 130, 255}; L.fogDensity = night ? 0.02f : 0.006f;
    L.fill = night ? Color{40, 44, 70, 255} : Color{120, 110, 130, 255}; L.rim = night ? Color{80, 90, 140, 255} : Color{255, 190, 120, 255}; L.key = night ? Color{120, 130, 180, 255} : Color{255, 214, 150, 255};
    L.surfaceY = 1e5f; L.time = S.t;
    L.moonDir = Vector3Normalize({0.25f, -0.35f, -1}); L.moon = night ? Color{150, 160, 220, 255} : Color{255, 196, 130, 255}; L.moonK = night ? 0.25f : 0.75f - dusk;
    L.ambK = night ? 0.35f : 0.85f; L.skyAmb = night ? Color{50, 60, 90, 255} : Color{255, 200, 160, 255}; L.seaAmb = night ? Color{20, 24, 30, 255} : Color{110, 100, 90, 255};
    L.outline = 0.8f; L.outlineTint = {30, 20, 16, 255}; L.stipple = 0.25f; L.grain = 0.25f; L.aoK = 0.35f; L.aoRadius = 0.5f; L.filmic = 0.15f; L.saturation = 1.2f;
    if (w.flareT > 0) L.AddPoint({0, 30, 50}, 120, {255, 120, 90, 255}, 1.2f * std::min(1.0f, w.flareT / 2));
    for (int i = 0; i < 3; i++) L.AddPoint({-10.0f + i * 10, 3.3f, -9}, 12, {255, 220, 160, 255}, 0.7f);   // (the room's lamps)
    for (int i = 0; i < 3; i++) L.AddPoint({-7.5f + i * 7.5f, 3.0f, 0.5f}, 6, {255, 210, 150, 255}, 0.4f);   // (porch lamps)
    rt::ApplyGameQuality(); rt::RenderBegin(S.cam, L);
    rt::SkyLook sk; sk.zenith = night ? Color{6, 8, 20, 255} : Color{70, 80, 150, 255}; sk.horizon = night ? Color{30, 30, 50, 255} : Color{255, 170, 110, 255}; sk.cloud = night ? Color{30, 34, 50, 255} : Color{250, 160, 140, 255};
    sk.moonDir = night ? Vector3Normalize({-0.25f, 0.4f, 1}) : Vector3Normalize({0.3f, -0.5f, -1});   // (by day the sun is our own; the dome's moon is down) sk.moonPhase = 0.5f; sk.cloudCover = 0.35f; sk.stars = night ? 1.0f : 0.0f; sk.time = S.t;
    rt::DrawSkyDome(sk);
    fpart::DrawMarsh(S.t, night, w.flareT);
    fpart::DrawPorch(w, S.me, S.t);
    fpart::DrawRoom(w, S.t);
    for (const auto& p : w.players) if (p.id != S.me || false) fpart::DrawPlayerFigure(w, p, S.t);
    for (const auto& b : w.birds) fpart::DrawBird(b, S.t, night && w.flareT <= 0);
    fpart::DrawDog(w, S.t, Me().pos.x);
    fpart::DrawProjs(w, S.t);
    S.tracers.erase(std::remove_if(S.tracers.begin(), S.tracers.end(), [](const FowlScene::Tracer& x) { return S.t - x.t > 0.15f; }), S.tracers.end());
    for (const auto& tr : S.tracers) { Vector3 a = Vector3Add(tr.a, {0, -0.25f, 0}), d = Vector3Subtract(tr.b, a); float L = Vector3Length(d); if (L < 0.5f) continue; Vector3 f = Vector3Scale(d, 1 / L), r = Vector3Normalize(Vector3CrossProduct({0, 1, 0}, f)), u = Vector3CrossProduct(f, r); Vector3 mid = Vector3Lerp(a, tr.b, 0.5f); Matrix m = {r.x * 0.03f, u.x * 0.03f, f.x * L, mid.x, r.y * 0.03f, u.y * 0.03f, f.y * L, mid.y, r.z * 0.03f, u.z * 0.03f, f.z * L, mid.z, 0, 0, 0, 1}; rt::DrawCubeGlow(m, tr.c, 1.6f); }
    DrawViewmodel();
    rt::RenderEnd();
}
}  // namespace

int gFowlMode = 0;
// ---------------------------------------------------------------- start, leave, the frame
void StartFowl(Game& g, int mode, int bots, int skill) {
    LoadProf();
    S = FowlScene{}; S.active = true; S.mode = std::clamp(mode, 0, (int)D().modes.size() - 1); S.bots = std::clamp(bots, 0, MAX_PLAYERS - 1); S.skill = std::clamp(skill, 0, 2);
    if (D().modes[S.mode].practice) S.bots = 0;
    S.Wm.Init(1, S.bots, S.mode, (uint32_t)GetRandomValue(1, 1 << 30));
    static const char* BOTS[6] = {"Gus", "Mabel", "Otis", "Pearl", "Hank", "Dottie"};
    for (auto& p : S.Wm.players) p.name = p.id == 0 ? "You" : BOTS[p.id % 6];
    // the permanent cosmetics from the locker
    Player& me = S.Wm.players[0]; me.hat = CosIndex(Equipped("hat")); me.paint = std::max(0, CosIndex(Equipped("paint"))); me.killSound = CosIndex(Equipped("sound")); me.dogCoat = CosIndex(Equipped("dog")); me.flag = CosIndex(Equipped("flag")); me.dance = CosIndex(Equipped("dance")); me.tracer = CosIndex(Equipped("tracer"));
    S.botRng.resize(S.Wm.players.size()); for (size_t i = 0; i < S.botRng.size(); i++) S.botRng[i] = 7777u * (uint32_t)(i + 1) + (uint32_t)GetRandomValue(0, 1 << 20);
    S.camYaw = 0; S.camPitch = 0.15f;
    g.scene = Scene::Fowl;
}
void StartFowlNet(Game& g, arcade::Session* net, const char* name) {
    LoadProf();
    S = FowlScene{}; S.active = true; S.net = net; S.netName = name ? name : "Shooter"; S.camPitch = 0.15f;
    g.scene = Scene::Fowl;
}
void LeaveFowl(Game& g) {
    if (S.net) { if (S.net->role == arcade::R_HOST) S.net->BackToLobby(); else S.net->Leave(); S.net = nullptr; }
    S.hostW = nullptr; S.active = false; MouseLook(false); g.scene = Scene::Arcade;
}
static void PayTokens() {
    if (S.tokensPaid || S.shot) return; S.tokensPaid = true;
    const World& w = W(); const Player& p = Me(); const Data& d = D();
    int t = d.tokMatch + p.birds / std::max(1, d.tokPerBirds) + (w.champion == p.id ? d.tokWin : 0) + (p.perfect > 0 ? d.tokPerfect : 0);
    if (p.ufoKills > 0 && !gProf.ufoFirst) { t += d.tokUfo; gProf.ufoFirst = true; }
    if (w.M().practice) t = 0;
    gProf.tokens += t; S.tokensEarned = t; SaveProf();
}
void SceneFowl(Game& g) {
    if (!S.active) { StartFowl(g, 0, 5, 1); }
    float dt = S.shot ? 1 / 60.0f : std::min(GetFrameTime(), 1 / 20.0f);
    S.t += dt; S.flash = std::max(0.0f, S.flash - dt * 30); S.megaFlash = std::max(0.0f, S.megaFlash - dt * 1.5f); S.bell = std::max(0.0f, S.bell - dt);
    if (S.net) {
        arcade::Session& N = *S.net; N.Update(GetTime(), dt);
        if (N.stage != arcade::S_PLAYING) { S.net = nullptr; S.hostW = nullptr; S.active = false; MouseLook(false); g.scene = Scene::Arcade; return; }
        int seat = N.MyPlayer();
        if (N.role == arcade::R_HOST) { S.hostW = FowlHostWorld(N.HostGame()); S.me = FowlSeatPlayer(N.HostGame(), seat); S.snapAt = GetTime(); }
        else if (N.stateVersion != S.seenVersion && !N.Snapshot().empty()) { S.seenVersion = N.stateVersion; Reader r(N.Snapshot()); ReadWorld(r, S.Wm, &S.evSeen); S.me = seat; S.snapAt = GetTime(); }
        if (W().players.empty() || S.me < 0 || S.me >= (int)W().players.size()) { ClearBackground(Color{30, 22, 16, 255}); DrawTextCenteredBold("To the Fowl Play Gun Club...", SCREEN_W / 2.0f, SCREEN_H / 2.0f, 24, Color{255, 220, 150, 255}); return; }
        if (!S.helloSent) { Writer o; o.U8(2); Command c; c.kind = CMD_NAME; c.s = S.netName; WriteCommand(c, o); N.Act(o); S.helloSent = true; }
        Gather(dt);
        Writer iw; iw.U8(0); WriteInput(Me().in, iw); N.Act(iw);
        for (auto& c : S.outbox) { Writer o; o.U8(2); WriteCommand(c, o); N.Act(o); } S.outbox.clear();
        if (!S.hostW) for (auto& b : S.Wm.birds) if (b.st == BI_FLY) b.p = Vector3Add(b.p, Vector3Scale(b.v, dt));   // (between snapshots the birds fly on)
    } else if (!S.shot) {
        Gather(dt);
        S.acc += dt; Input mine = Me().in; bool first = true;
        while (S.acc >= STEP) {
            for (auto& p : S.Wm.players) if (p.id != S.me) BotInput(S.Wm, p.id, p.in, p.cmds, S.botRng[p.id], S.skill);
            Me().in = mine; if (!first) { Me().in.reload = Me().in.swap = false; }
            S.Wm.Step(); S.acc -= STEP; first = false;
        }
    }
    if (W().phase == PH_HUNT && S.lastPhase != PH_HUNT) { S.camYaw = 0; S.camPitch = 0.15f; S.panel = -1; }
    if (W().phase == PH_INTER && S.lastPhase != PH_INTER) { S.camYaw = PI; S.camPitch = -0.05f; }
    if (W().phase == PH_OVER && S.lastPhase != PH_OVER) PayTokens();
    S.lastPhase = W().phase;
    { FpAudio a; a.on = !S.shot; const World& w = W(); a.phase = w.phase == PH_HUNT ? 1 : w.phase == PH_BONUS ? 2 : w.phase == PH_INTER ? 3 : w.phase == PH_TALLY ? 4 : w.phase == PH_OVER ? 5 : 0; a.round = w.round; a.golden = w.round >= D().goldenHour; a.night = w.M().night; a.bagpipe = Me().bagpipe; a.slop = S.panel == 4; AudioFowl(a); }
    ReadEvents(); StepCamera(dt); Render(); DrawHud(g);
}
void FowlMenuTick(float dt) { if (S.active && S.net) { Writer w; w.U8(0); Input in; WriteInput(in, w); S.net->Act(w); S.net->Update(GetTime(), dt); } }
int FowlTokens() { LoadProf(); return gProf.tokens; }

// ---------------------------------------------------------------- the locker (permanent cosmetics, bought with arcade tokens; the crate)
static std::string gCrateWon; static float gCrateT = 0;
bool FowlLockerPage(Game& g) {
    (void)g; LoadProf(); const Data& d = D();
    ClearBackground(Color{26, 18, 14, 255});
    DrawTextCenteredBold("The Fowl Play locker", SCREEN_W / 2.0f, 24, 30, Color{255, 220, 150, 255});
    DrawTextCentered(TextFormat("%d arcade tokens  -  10 a match, 1 per 5 birds, 25 for a win, 20 for a perfect wave, 50 for your first UFO", gProf.tokens), SCREEN_W / 2.0f, 62, 15, Color{220, 210, 190, 255});
    static const Color TIER[4] = {{200, 200, 200, 255}, {110, 200, 255, 255}, {220, 140, 255, 255}, {255, 200, 80, 255}};
    static const char* TNAME[4] = {"common", "uncommon", "rare", "special"};
    for (int i = 0; i < (int)d.cosmetics.size(); i++) {
        const SlopItem& it = d.cosmetics[i]; float x = 30 + (i / 14) * 410.0f, y = 96 + (i % 14) * 36.0f; Rectangle r{x, y, 396, 32};
        bool own = Owns(it.id), on = Equipped(it.kind) == it.id;
        DrawRectangleRec(r, on ? Color{80, 60, 30, 230} : Color{44, 34, 28, 220}); DrawRectangle((int)x, (int)y, 5, 32, TIER[std::clamp(it.tier, 0, 3)]);
        Txt(it.name, x + 12, y + 8, 15, own ? WHITE : Color{200, 190, 180, 255});
        std::string right = on ? "worn" : own ? "wear it" : it.crateOnly ? "crate only" : TextFormat("%d tokens", d.lockerPrice[std::clamp(it.tier, 0, 3)]);
        bool can = on ? false : own ? true : !it.crateOnly && gProf.tokens >= d.lockerPrice[std::clamp(it.tier, 0, 3)];
        if (Button({x + 290, y + 3, 100, 26}, right.c_str(), can, 12)) {
            if (!own) { gProf.tokens -= d.lockerPrice[std::clamp(it.tier, 0, 3)]; gProf.owned.push_back(it.id); }
            Equip(it.kind, it.id); SaveProf();
        }
        if (on && Button({x + 236, y + 3, 50, 26}, "off", true, 12)) { Equip(it.kind, "-"); SaveProf(); }
        (void)TNAME;
    }
    // the crate: one of the forty by tier
    Rectangle cr{SCREEN_W - 260.0f, SCREEN_H - 150.0f, 230, 46};
    if (Button(cr, TextFormat("Open a crate (%d tokens)", d.cratePrice), gProf.tokens >= d.cratePrice, 15)) {
        gProf.tokens -= d.cratePrice; int tot = 0; for (int k = 0; k < 4; k++) tot += d.crateWeight[k]; int roll = GetRandomValue(0, tot - 1), tier = 0; for (; tier < 3; tier++) { roll -= d.crateWeight[tier]; if (roll < 0) break; }
        std::vector<int> pool; for (int i = 0; i < (int)d.cosmetics.size(); i++) if (d.cosmetics[i].tier == tier) pool.push_back(i);
        if (!pool.empty()) { const SlopItem& it = d.cosmetics[pool[GetRandomValue(0, (int)pool.size() - 1)]]; if (Owns(it.id)) { gProf.tokens += d.cratePrice / 2; gCrateWon = it.name + " (a duplicate: half your tokens back)"; } else { gProf.owned.push_back(it.id); gCrateWon = it.name + " (" + TNAME[tier] + ")"; } gCrateT = 3; }
        SaveProf(); PlayCue("arc.deal");
    }
    if (gCrateT > 0) { gCrateT -= GetFrameTime(); DrawTextCenteredBold("From the crate: " + gCrateWon, SCREEN_W / 2.0f, SCREEN_H - 96.0f, 20, Color{255, 220, 120, 255}); }
    return Button({30, SCREEN_H - 60.0f, 160, 40}, "Back", true, 16) || IsKeyPressed(KEY_ESCAPE);
}
void DebugFowlLocker() { LoadProf(); }

// --shots: 0 the hunt (round 4), 1 the clubhouse room, 2 the gun counter panel, 3 the tally with the dog, 4 the Slop Shop,
// 5 sabotage on screen (bees, smudge, the moose), 6 the podium, 7 round 13 (swans, the convoy), 8 the bonus clays, 9 night
void DebugFowlShot(Game& g, int which) {
    StartFowl(g, which == 9 ? ModeIndex("night") : 0, 5, 2); S.shot = true; S.help = which == 0;
    World& w = S.Wm; uint32_t r = 11;
    auto run = [&](float secs) { for (int i = 0; i < (int)(secs / STEP) && w.phase != PH_OVER; i++) { for (auto& p : w.players) if (p.id != 0) BotInput(w, p.id, p.in, p.cmds, r, 2); w.Step(); } };
    auto toRound = [&](int rd, Phase ph, float into) { while (!(w.round == rd && w.phase == ph) && w.phase != PH_OVER) { if (w.phase == PH_INTER) w.phaseT = std::max(w.phaseT, w.interLen - STEP); if (w.phase == PH_HUNT) w.phaseT = std::max(w.phaseT, D().hunt - STEP); if (w.phase == PH_BONUS) w.phaseT = std::max(w.phaseT, D().bonusLength - STEP); w.Step(); } run(into); };
    Player& me = w.players[0];
    if (which == 0 || which == 9) { toRound(4, PH_HUNT, 12.4f); S.camYaw = 0.05f; S.camPitch = 0.22f; me.guns[1] = MakeGun(GunIndex("pump")); me.hand = 1; }
    if (which == 1) { toRound(2, PH_INTER, 6); me.room = 1; me.pos = {2, 0, -5}; S.camYaw = PI - 0.4f; S.camPitch = -0.08f; }
    if (which == 2) { toRound(2, PH_INTER, 3); me.room = 1; me.pos = Vector3Add(World::Station(0), {1.5f, 0, 0}); me.money = 640; S.panel = 0; S.camYaw = PI; }
    if (which == 3) { toRound(3, PH_TALLY, 1.0f); S.camYaw = 0; S.camPitch = 0.0f; }
    if (which == 4) { toRound(5, PH_INTER, 3); me.room = 1; me.pos = Vector3Add(World::Station(4), {-1.6f, 0, 0}); me.money = 820; S.panel = 4; S.slopTab = 1; S.camYaw = -PI / 2; }
    if (which == 5) { toRound(6, PH_HUNT, 8); me.beesT = 8; me.smudgeT = 15; me.cardboardT = 10; me.cardboardHP = 3; me.bagpipe = true; S.camYaw = 0; S.camPitch = 0.12f; }
    if (which == 6) { w.players[0].birds = 141; w.players[2].birds = 133; w.EndMatch(); S.tokensEarned = 47; }
    if (which == 7) { toRound(13, PH_HUNT, 20); S.camYaw = -0.15f; S.camPitch = 0.25f; me.guns[1] = MakeGun(GunIndex("lightning")); me.hand = 1; }
    if (which == 8) { toRound(3, PH_BONUS, 4.5f); S.camYaw = 0.3f; S.camPitch = 0.3f; }
    S.lastPhase = w.phase;
    StepCamera(1 / 60.0f);
}
