// A Night Off: fights, weapons, the room's mess and damage (doc pp. 15-17, 5-6, 19-20), stage 4. Headless.
//  Fighters are players, patrons and the alley dog (Who). A brawl starts with the first blow and pulls in the violent
//  and the loyal within 3 m; Sister Ash ends one by walking into it. Props are picked up, swung, thrown, flipped and
//  broken; every break during a brawl goes on its ledger, and the bill lands on the tab of whoever started it (the
//  bartender's judgment, which favours whoever he likes). An armed fight calls the police.
#include "nightoff.h"
#include "json.h"
#include "redtide.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace no {

// ---------------------------------------------------------------- data
static FightData LoadFights() {
    FightData d;
    Json j = LoadJsonFile(rt::DataDir() + "/../nightoff/nightoff_fights.json");
    d.hp = j["hp"].F(100); d.reach = j["reach"].F(1.35f);
    const Json& m = j["moves"];
    d.jab = m["jab"]["damage"].F(10); d.jabWind = m["jab"]["windup"].F(0.12f); d.jabRec = m["jab"]["recover"].F(0.35f);
    d.hay = m["haymaker"]["damage"].F(25); d.hayWind = m["haymaker"]["windup"].F(0.8f); d.hayRec = m["haymaker"]["recover"].F(0.6f);
    d.grabHold = m["grab"]["hold"].F(1.5f); d.throwDmg = m["grab"]["throw_damage"].F(15); d.throwSpeed = m["grab"]["throw_speed"].F(6.5f);
    d.shoveDmg = m["shove"]["damage"].F(4); d.shovePush = m["shove"]["push"].F(3.5f); d.shoveRec = m["shove"]["recover"].F(0.5f);
    d.blockT = m["block"]["time"].F(1.2f); d.blockK = m["block"]["factor"].F(0.5f); d.dodgeT = m["dodge"]["time"].F(0.45f); d.dodgeFall = m["dodge"]["fall_from"].F(60);
    for (const Json& w : j["weapons"].a) {
        WeaponDef x; x.key = w["key"].Str0(); x.name = w["name"].Str0(x.key); x.damage = w["damage"].F(10); x.thrown = w["thrown"].F(0); x.breaks = w["breaks"].I(0);
        x.smashTo = w["smash_to"].Str0(); x.breakTo = w["break_to"].Str0(); x.bleed = w["bleed"].Bool0(); x.armed = w["armed"].Bool0(); x.stun = w["stun"].Bool0(); x.shotgun = w["shotgun"].Bool0(); x.price = w["price"].F(0);
        d.weapons.push_back(x);
    }
    for (const auto& kv : j["props"].o) d.prices.push_back({kv.first, kv.second.F(0)});
    d.koS = j["knockout_s"].F(30); d.koBarred = j["knockouts_barred"].I(3); d.crowdR = j["crowd_radius"].F(3);
    d.policeArmed = j["police_armed_s"].F(180); d.policeShotgun = j["police_shotgun_s"].F(60);
    d.afterGood = j["after"]["violent_loud"].F(0.1f); d.afterBad = j["after"]["others"].F(-0.2f); d.afterMin = j["after"]["game_minutes"].F(5);
    d.barPerFight = j["bartender"]["per_fight"].F(-10); d.barPerBreak = j["bartender"]["per_break"].F(-3); d.barBehind = j["bartender"]["behind_counter"].F(-15);
    d.panPrice = j["cook_pan_price"].F(20); d.dogFeeds = j["dog_feeds"].I(3); d.dogBite = j["dog"]["bite"].F(15); d.dogHold = j["dog"]["hold"].F(1.2f);
    return d;
}
const FightData& FD() { static FightData d = LoadFights(); return d; }
int FightData::Weapon(const std::string& key) const { for (int i = 0; i < (int)weapons.size(); i++) if (weapons[i].key == key) return i; return -1; }
float FightData::Price(const std::string& kind) const { for (const auto& p : prices) if (p.first == kind) return p.second; return 0; }
bool Prop::Breakable() const { return kind != "shotgun" && kind != "knife" && kind != "pan" && kind != "club" && kind != "dart"; }

// ---------------------------------------------------------------- who's who
Combat* Night::CombatOf(Who w) {
    if (w.kind == 0 && w.idx >= 0 && w.idx < (int)players.size()) return &players[w.idx].fight;
    if (w.kind == 1 && w.idx >= 0 && w.idx < (int)patrons.size()) return &patrons[w.idx].fight;
    if (w.kind == 2) return &dog.fight;
    return nullptr;
}
Vector2* Night::PosOf(Who w) {
    if (w.kind == 0 && w.idx >= 0 && w.idx < (int)players.size()) return &players[w.idx].pos;
    if (w.kind == 1 && w.idx >= 0 && w.idx < (int)patrons.size()) return &patrons[w.idx].pos;
    if (w.kind == 2) return &dog.pos;
    return nullptr;
}
float* Night::YawOf(Who w) {
    if (w.kind == 0 && w.idx >= 0 && w.idx < (int)players.size()) return &players[w.idx].yaw;
    if (w.kind == 1 && w.idx >= 0 && w.idx < (int)patrons.size()) return &patrons[w.idx].yaw;
    if (w.kind == 2) return &dog.yaw;
    return nullptr;
}
std::string Night::NameOf(Who w) const {
    if (w.kind == 0 && w.idx >= 0 && w.idx < (int)players.size()) return players[w.idx].name.empty() ? std::string("you") : players[w.idx].name;
    if (w.kind == 1 && w.idx >= 0 && w.idx < (int)patrons.size()) return patrons[w.idx].name;
    if (w.kind == 2) return "the dog";
    return "someone";
}
float Night::DrunkOf(Who w) const {
    if (w.kind == 0 && w.idx >= 0 && w.idx < (int)players.size()) return players[w.idx].drunk;
    if (w.kind == 1 && w.idx >= 0 && w.idx < (int)patrons.size()) return patrons[w.idx].drunk;
    return 0;
}
float Night::ToughOf(Who w) const {
    if (w.kind == 0 && w.idx >= 0 && w.idx < (int)players.size()) return Toughness(players[w.idx]);
    if (w.kind == 1 && w.idx >= 0 && w.idx < (int)patrons.size()) return 1 + std::clamp(patrons[w.idx].drunk / 100, 0.0f, 1.0f);
    return 1;
}
bool Night::Present(Who w) const {
    if (w.kind == 0 && w.idx >= 0 && w.idx < (int)players.size()) { State s = players[w.idx].st; return s == State::Active || s == State::Drinking || s == State::Eating || s == State::Vomiting || s == State::Down; }
    if (w.kind == 1 && w.idx >= 0 && w.idx < (int)patrons.size()) { const Patron& c = patrons[w.idx]; return c.inside && !c.gone; }
    if (w.kind == 2) return dog.owner >= 0;
    return false;
}
void Night::AddPop(Vector2 at, float y, const std::string& text, Color c) { if (pops.size() > 40) pops.erase(pops.begin()); pops.push_back({{at.x, y, at.y}, text, 1.3f, c}); }
float Night::AfterFightCharisma(const Player& p, const Patron& c) const {
    if (p.fight.afterT <= 0) return 0;
    bool rough = c.Has(D().Trait("violent")) || c.Has(D().Trait("loud"));
    return rough ? FD().afterGood : FD().afterBad;
}

// ---------------------------------------------------------------- the props
static Prop MakeProp(const char* kind, Vector3 pos, float yaw = 0) {
    Prop p; p.kind = kind; p.pos = pos; p.yaw = yaw; p.weapon = FD().Weapon(kind);
    return p;
}
void Night::InitProps() {
    props.clear(); brawls.clear(); pops.clear(); policeT = -1; policeInT = 0; damage = 0; dog = Dog{};
    const BarData& B = D().bar;
    // the tables (the bar's, and two small ones in the games room) and a chair at every seat round them
    for (const auto& b : B.boxes) if (b.kind == "table") { Prop t = MakeProp("table", {b.r.x + b.r.width / 2, 0, b.r.y + b.r.height / 2}); t.size = {b.r.width, b.r.height}; props.push_back(t); }
    for (Vector2 c : {Vector2{7.6f, 4.0f}, Vector2{7.6f, 14.0f}}) { Prop t = MakeProp("table", {c.x, 0, c.y}); t.size = {1.1f, 1.1f}; props.push_back(t);
        props.push_back(MakeProp("chair", {c.x - 1.0f, 0, c.y}, 0)); props.push_back(MakeProp("chair", {c.x + 1.0f, 0, c.y}, PI)); props.push_back(MakeProp("bottle", {c.x + 0.2f, 0.8f, c.y - 0.1f})); props.push_back(MakeProp("glass", {c.x - 0.2f, 0.8f, c.y + 0.15f})); }
    for (const char* k : {"tables", "cards", "snug", "games"}) if (const auto* s = B.Seats(k)) for (Vector2 v : *s) props.push_back(MakeProp("chair", {v.x, 0, v.y}, 0));
    for (Vector2 st : B.stools) props.push_back(MakeProp("stool", {st.x, 0, st.y}));
    // the windows on the street wall (one is the games room's), the piano, the toilets' mirror, the slot machines
    for (float x : {4.0f, 13.0f, 26.0f, 34.0f}) { Prop w = MakeProp("window", {x, 1.9f, 0.05f}); w.size = {1.6f, 0.2f}; props.push_back(w); }
    for (const auto& b : B.boxes) if (b.kind == "piano") props.push_back(MakeProp("piano", {b.r.x + b.r.width / 2, 0, b.r.y + b.r.height / 2}));
    { Prop m = MakeProp("mirror", {B.mirror.x, 1.6f, B.mirror.y}); props.push_back(m); }
    for (const auto& b : B.boxes) if (b.kind == "slot") { Prop s = MakeProp("slot", {b.r.x + b.r.width / 2, 0, b.r.y + b.r.height / 2}); s.size = {b.r.width, b.r.height}; props.push_back(s); }
    // the weapons that are lying about: the cue rack, the darts in the board, bottles on the tables and the bar, the
    // yard's golf clubs, the kitchen's pan and knife, the shotgun under the till
    for (int k = 0; k < 4; k++) props.push_back(MakeProp("cue", {0.35f, 0.75f, 9.4f + k * 0.18f}));
    for (int k = 0; k < 3; k++) props.push_back(MakeProp("dart", {B.dartboard.x + 0.12f, 1.7f, B.dartboard.y - 0.05f + k * 0.05f}));
    for (const auto& b : B.boxes) if (b.kind == "table") props.push_back(MakeProp("bottle", {b.r.x + b.r.width / 2 + 0.15f, b.h, b.r.y + b.r.height / 2}));
    for (float x : {13.5f, 16.0f, 19.5f}) props.push_back(MakeProp("bottle", {x, 1.12f, 9.5f}));
    for (int k = 0; k < 2; k++) props.push_back(MakeProp("club", {B.golf.x - 0.6f + k * 0.2f, 0.5f, B.golf.y + 0.8f}));
    props.push_back(MakeProp("pan", {27.5f, 1.05f, 16.6f}));
    props.push_back(MakeProp("knife", {28.6f, 1.05f, 16.6f}));
    props.push_back(MakeProp("shotgun", {17.0f, 0.6f, 10.4f}));
    // the alley dog, asleep by the bins
    for (const auto& b : B.boxes) if (b.kind == "bins") dog.pos = {b.r.x + b.r.width / 2 + 1.0f, b.r.y + b.r.height / 2};
    if (dog.pos.x == 0 && dog.pos.y == 0) dog.pos = {-4, 20};
}
int Night::NearestProp(Vector2 at, float range, bool weaponsOnly) const {
    int best = -1; float bd = range;
    for (int i = 0; i < (int)props.size(); i++) {
        const Prop& p = props[i];
        if (p.state != PS_OK && p.state != PS_OVER) continue;
        if (weaponsOnly && !p.Pickup()) continue;
        float d = Vector2Distance(at, {p.pos.x, p.pos.z}); if (d < bd) { bd = d; best = i; }
    }
    return best;
}
void Night::BreakProp(int i, Who by, int brawl, const char* how) {
    if (i < 0 || i >= (int)props.size()) return;
    Prop& p = props[i];
    if (p.state == PS_BROKEN || p.state == PS_GONE) return;
    float price = FD().Price(p.kind);
    if (p.state == PS_HELD) { Combat* h = CombatOf(p.holder); if (h && h->held == i) h->held = -1; }
    p.state = PS_BROKEN; p.vel = {0, 0, 0}; p.pos.y = 0; p.brawl = brawl;
    // glasses and a bottle on a table go with it
    if (p.kind == "table") for (int k = 0; k < (int)props.size(); k++) { Prop& q = props[k]; if ((q.kind == "glass" || q.kind == "bottle") && q.state == PS_OK && fabsf(q.pos.x - p.pos.x) < p.size.x / 2 + 0.1f && fabsf(q.pos.z - p.pos.z) < p.size.y / 2 + 0.1f && q.pos.y > 0.5f) BreakProp(k, by, brawl, "swept off the table"); }
    if (price <= 0) return;
    damage += price;
    if (by.kind == 0 && by.idx >= 0 && by.idx < (int)players.size()) players[by.idx].damageCaused += price;
    bar.mood = std::max(0.0f, bar.mood + FD().barPerBreak);
    std::string what = p.kind == "window" ? "a window" : p.kind == "piano" ? "the piano" : p.kind == "glass" ? "a glass" : p.kind == "bottle" ? "a bottle" : p.kind == "cue" ? "a pool cue" : p.kind == "slot" ? "a slot machine" : p.kind == "mirror" ? "the mirror" : p.kind == "table" ? "a table" : p.kind == "stool" ? "a stool" : "a chair";
    if (brawl >= 0 && brawl < (int)brawls.size()) { brawls[brawl].bill += price; brawls[brawl].broke.push_back(what + (how ? std::string(" (") + how + ")" : std::string())); }
    else if (by.kind == 0 && by.idx >= 0 && by.idx < (int)players.size()) { players[by.idx].tab += price; Note(players[by.idx], 10, TextFormat("Broke %s (%.0f on the tab).", what.c_str(), price)); }
    AddPop({p.pos.x, p.pos.z}, 1.2f, p.kind == "window" ? "CRASH!" : p.kind == "glass" || p.kind == "bottle" ? "Smash!" : "Crack!", {255, 210, 140, 255});
}

// ---------------------------------------------------------------- a brawl
int Night::StartBrawl(Who a, Who b, Who starter) {
    Combat* A = CombatOf(a); Combat* Bc = CombatOf(b); if (!A || !Bc) return -1;
    if (EventOn("wake")) { Say("Not at a wake. Everyone looks at the coffin."); return -1; }   // (fights are impossible at a wake)
    int id = A->brawl >= 0 ? A->brawl : Bc->brawl;
    if (id < 0 || brawls[id].over) {
        Brawl br; br.id = (int)brawls.size(); br.starter = starter; br.at = *PosOf(a); br.room = RoomAt(br.at);
        brawls.push_back(br); id = br.id;
        bar.mood = std::max(0.0f, bar.mood + FD().barPerFight);
        SendHome(0.33f);   // (a fight sends a third of the room home for twenty minutes)
        std::string who = NameOf(starter);
        Say(who + (who == "You" ? " start a fight in " : " starts a fight in ") + br.room + ".");
    }
    auto join = [&](Who w, Combat* c, int side) {
        if (c->brawl == id) return;
        c->brawl = id; c->side = side; brawls[id].size++;
        if (w.kind == 1) { Patron& p = patrons[w.idx]; if (p.talkingTo >= 0 && p.talkingTo < (int)players.size()) EndTalk(players[p.talkingTo]); p.sitting = false; }
        if (w.kind == 0) { Player& p = players[w.idx]; if (p.talk.patron >= 0) EndTalk(p); if (p.game.kind >= 0) EndGame(p); }
    };
    int sa = A->brawl == id ? A->side : (Bc->brawl == id ? 1 - Bc->side : 0);
    join(a, A, sa); join(b, Bc, 1 - sa);
    if (!A->foe.Valid()) A->foe = b;
    if (!Bc->foe.Valid() || Bc->foe == a) Bc->foe = a;
    brawls[id].quietT = 0;
    return id;
}
void Night::Strike(Who att, Who def, float dmg, int weapon, bool hay) {
    Combat* A = CombatOf(att); Combat* D = CombatOf(def); if (!D || !Present(def) || D->Down()) return;
    LibraryRule(att); if (att.kind == 0 && att.idx < (int)players.size() && players[att.idx].st == State::Gone) return;   // (the Monkey: no fights in the library)
    Vector2 dp = *PosOf(def);
    // a blow lands: block halves it, a dodge (sober enough) slips it, a drunk dodge is falling over
    if (D->dodgeT > 0) {
        if (DrunkOf(def) < FD().dodgeFall) { AddPop(dp, 2.0f, "Dodged!", {200, 230, 255, 255}); D->dodgeT = 0; return; }
        D->fallT = 1.2f; AddPop(dp, 1.6f, "Whoops", {230, 200, 160, 255});
    }
    if (D->blockT > 0) { dmg *= FD().blockK; D->blockT = 0; AddPop(dp, 2.0f, "Blocked", {200, 200, 200, 255}); }
    if (def.kind == 0 && def.idx < (int)players.size() && players[def.idx].wareT[W_BARNACLE] > 0) dmg *= 0.7f;   // (Barnacle: no pain)
    float k = 0.75f + 0.25f * ToughOf(att);                   // (a sober one hits for less)
    float hpLoss = dmg * k;
    D->hp -= hpLoss; D->hitT = 0.35f;
    if (weapon >= 0) { const WeaponDef& w = FD().weapons[weapon]; if (w.bleed) D->bleedT = 8; if (w.stun && hay) D->stunT = 1.5f; }
    static const char* POW[6] = {"Whack!", "Thud!", "Pow!", "Oof!", "Crack!", "Bonk!"};
    AddPop(dp, 2.0f, hay ? "WALLOP!" : POW[(int)(Rand() * 6) % 6], hay ? Color{255, 170, 90, 255} : Color{255, 236, 190, 255});
    // anyone not yet in it joins against whoever hit them; the blow keeps the brawl alive
    if (A && D->brawl < 0) StartBrawl(att, def, att);
    else if (A && A->brawl < 0) StartBrawl(att, def, att);
    if (D->brawl >= 0) brawls[D->brawl].quietT = 0;
    if (def.kind == 1) { Patron& c = patrons[def.idx]; c.mood = std::max(0.0f, c.mood - 10); if (att.kind == 0 && att.idx < (int)c.mem.size()) c.mem[att.idx].fights += 1; }
    if (A) D->foe = att;
    // the knockout: 30 s on the floor (three in a night and the bartender bars you)
    if (D->hp <= 0) {
        D->hp = 0; D->downT = FD().koS; D->knockouts++; D->grabbedBy = {}; D->grabbing = {};
        if (D->held >= 0 && D->held < (int)props.size()) { Prop& h = props[D->held]; h.state = PS_OVER; h.holder = {}; h.pos = {dp.x, 0.05f, dp.y}; D->held = -1; }
        AddPop(dp, 2.3f, "K.O.", {255, 120, 100, 255});
        if (D->brawl >= 0) brawls[D->brawl].kos++;
        if (def.kind == 0) {
            Player& p = players[def.idx]; p.st = State::Down;
            if (weapon >= 0 && FD().weapons[weapon].bleed) { Note(p, 9, TextFormat("Stabbed by %s with %s.", NameOf(att).c_str(), FD().weapons[weapon].name.c_str())); Leave(p, E_HOSPITAL, "in the harbour hospital with a stitched arm and a bill for 100"); return; }
            Note(p, 11, TextFormat("Knocked out by %s at %s.", NameOf(att).c_str(), Clock().c_str()));
            if (D->knockouts >= FD().koBarred) { Leave(p, E_THROWN_OUT, "the pavement outside the Gull"); Say("The bartender: \"Three times on my floor. Out.\""); }
        }
        if (att.kind == 0 && def.kind != 0) { Player& p = players[att.idx]; Note(p, p.drunk < 20 ? 5 : 11, TextFormat("Knocked out %s%s.", NameOf(def).c_str(), p.drunk < 20 ? ", stone sober" : "")); }
    }
}
void Night::Attack(Who w, int move) {
    Combat* C = CombatOf(w); if (!C || C->Busy() || C->windT > 0 || C->recT > 0) return;
    if (EventOn("wake") && move != MV_THROW) { if (w.kind == 0) AddPop(*PosOf(w), 2.0f, "Not at a wake", {200, 200, 220, 255}); return; }
    Vector2 pos = *PosOf(w); float yaw = *YawOf(w);
    if (move == MV_THROW) {   // throw what you hold, along your facing (the drunker, the wilder)
        if (C->held < 0 || C->held >= (int)props.size()) return;
        Prop& p = props[C->held];
        const WeaponDef* wd = p.weapon >= 0 ? &FD().weapons[p.weapon] : nullptr;
        if (wd && wd->shotgun) {   // the shotgun: one shot; anyone hit is hospitalized; everyone else freezes
            float a = yaw;
            for (auto& c : patrons) if (c.inside && !c.gone) { Vector2 d = Vector2Subtract(c.pos, pos); float L = Vector2Length(d); if (L < 9 && L > 0.1f && Vector2DotProduct(d, {cosf(a), sinf(a)}) / L > 0.93f) { c.gone = true; c.inside = false; Say(c.name + " is carried out to the hospital."); } }
            for (auto& b : brawls) if (!b.over) EndBrawl(b);
            AddPop(pos, 2.4f, "BLAM!", {255, 90, 60, 255});
            p.state = PS_GONE; C->held = -1;
            if (w.kind == 0) { Player& pl = players[w.idx]; Note(pl, 5, "Fired the bartender's shotgun in the Gull."); Flag("shotgun", pl.name); policeT = 0; Leave(pl, E_ARRESTED, "a cell at the harbour station"); }
            return;
        }
        float drift = (AimMul(DrunkOf(w)) - 1) * 0.3f * (Rand() - 0.5f) * 2;
        float sp = 9;
        p.state = PS_FLYING; p.holder = {}; p.thrower = w; p.brawl = C->brawl;
        p.pos = {pos.x + cosf(yaw) * 0.5f, 1.5f, pos.y + sinf(yaw) * 0.5f};
        p.vel = {cosf(yaw + drift) * sp, 2.2f, sinf(yaw + drift) * sp}; p.spin = 8;
        C->held = -1; C->recT = 0.5f; C->swingT = 1; C->swingDir = {cosf(yaw), sinf(yaw)};
        return;
    }
    if (move == MV_GRAB) {
        if (C->grabbing.Valid()) {   // the throw: into a table, over the bar, through a window
            Combat* G = CombatOf(C->grabbing);
            if (G) { G->grabbedBy = {}; G->push = {cosf(yaw) * FD().throwSpeed, sinf(yaw) * FD().throwSpeed}; G->flying = true; G->stunT = 0.6f; Strike(w, C->grabbing, FD().throwDmg, -1, false); }
            C->grabbing = {}; C->recT = 0.6f; C->swingT = 1;
            return;
        }
        // grab whoever is in front of you
        Who best{}; float bd = FD().reach;
        auto consider = [&](Who o) { if (o == w || !Present(o)) return; Combat* oc = CombatOf(o); if (!oc || oc->Down() || oc->grabbedBy.Valid()) return; Vector2 d = Vector2Subtract(*PosOf(o), pos); float L = Vector2Length(d); if (L < bd && L > 0.05f && Vector2DotProduct(d, {cosf(yaw), sinf(yaw)}) / L > 0.5f) { bd = L; best = o; } };
        for (int i = 0; i < (int)players.size(); i++) consider(PlayerW(i));
        for (int i = 0; i < (int)patrons.size(); i++) if (patrons[i].inside && !patrons[i].gone) consider(PatronW(i));
        if (!best.Valid()) { C->recT = 0.4f; return; }
        Combat* G = CombatOf(best);
        C->grabbing = best; C->grabT = FD().grabHold; G->grabbedBy = w;
        if (G->brawl < 0 || C->brawl < 0) StartBrawl(w, best, w);
        AddPop(*PosOf(best), 2.0f, "Grabbed!", {240, 220, 160, 255});
        return;
    }
    C->move = move; C->swingDir = {cosf(yaw), sinf(yaw)};
    C->windT = move == MV_HAYMAKER ? FD().hayWind : move == MV_SHOVE ? 0.15f : FD().jabWind;
    if (C->held >= 0 && C->held < (int)props.size() && props[C->held].kind == "shotgun") C->windT = 0;   // (you don't swing the shotgun)
}
// a swing lands (after its windup): the fighter in the arc, with drunk drift - a haymaker at 80 might hit the wrong person
static void Land(Night& n, Who w) {
    Combat* C = n.CombatOf(w); Vector2 pos = *n.PosOf(w);
    float drunk = n.DrunkOf(w);
    float drift = drunk > 40 ? (AimMul(drunk) - 1) * 0.55f * (n.Rand() - 0.5f) * 2 : 0;
    Vector2 dir = Vector2Rotate(C->swingDir, drift);
    float reach = FD().reach * (C->held >= 0 && n.props[C->held].kind == "cue" ? 1.4f : 1);
    Who best{}; float bd = reach;
    auto consider = [&](Who o) {
        if (o == w || !n.Present(o)) return; Combat* oc = n.CombatOf(o); if (!oc || oc->Down()) return;
        Vector2 d = Vector2Subtract(*n.PosOf(o), pos); float L = Vector2Length(d); if (L >= bd || L < 0.05f) return;
        if (Vector2DotProduct(d, dir) / L < 0.82f) return;
        bd = L; best = o;
    };
    for (int i = 0; i < (int)n.players.size(); i++) consider(PlayerW(i));
    for (int i = 0; i < (int)n.patrons.size(); i++) if (n.patrons[i].inside && !n.patrons[i].gone) consider(PatronW(i));
    if (n.dog.owner >= 0) consider(DogW());
    C->swingT = 1;
    int weapon = C->held >= 0 && C->held < (int)n.props.size() ? n.props[C->held].weapon : -1;
    if (!best.Valid()) {   // a whiff: the swing goes on into the furniture (a haymaker clears a table)
        n.AddPop(Vector2Add(pos, Vector2Scale(dir, 0.9f)), 1.7f, "Whiff", {200, 190, 170, 255});
        if (C->move == MV_HAYMAKER) for (int i = 0; i < (int)n.props.size(); i++) { Prop& p = n.props[i]; if ((p.kind == "glass" || p.kind == "bottle") && p.state == PS_OK && Vector2Distance({p.pos.x, p.pos.z}, Vector2Add(pos, Vector2Scale(dir, 0.9f))) < 0.6f) n.BreakProp(i, w, C->brawl, "a haymaker"); }
        return;
    }
    if (C->move == MV_SHOVE) {
        Combat* D = n.CombatOf(best); D->push = Vector2Scale(dir, FD().shovePush);
        n.Strike(w, best, FD().shoveDmg, -1, false);
        return;
    }
    float dmg = C->move == MV_HAYMAKER ? FD().hay : FD().jab;
    if (weapon >= 0) dmg = std::max(dmg, FD().weapons[weapon].damage * (C->move == MV_HAYMAKER ? 1.2f : 1));
    n.Strike(w, best, dmg, weapon, C->move == MV_HAYMAKER);
    // a weapon wears: a cue breaks after three hits into a jagged half; a chair after two
    if (weapon >= 0) {
        Prop& p = n.props[C->held]; const WeaponDef& wd = FD().weapons[weapon]; p.hits++;
        if (wd.breaks > 0 && p.hits >= wd.breaks) {
            if (!wd.breakTo.empty()) { float price = FD().Price(p.kind); if (price > 0) { n.damage += price; if (C->brawl >= 0) { n.brawls[C->brawl].bill += price; n.brawls[C->brawl].broke.push_back("a pool cue (snapped over a head)"); } } p.kind = wd.breakTo; p.weapon = FD().Weapon(wd.breakTo); p.hits = 0; }
            else { int idx = C->held; C->held = -1; n.props[idx].holder = {}; n.BreakProp(idx, w, C->brawl, "over a head"); }
        }
    }
}
void Night::EndBrawl(Brawl& b) {
    if (b.over) return;
    b.over = true;
    // who won: the side with someone still standing
    bool standing[2] = {false, false};
    auto each = [&](auto fn) { for (int i = 0; i < (int)players.size(); i++) fn(PlayerW(i)); for (int i = 0; i < (int)patrons.size(); i++) fn(PatronW(i)); fn(DogW()); };
    each([&](Who w) { Combat* c = CombatOf(w); if (c && c->brawl == b.id && Present(w) && !c->Down()) standing[c->side] = true; });
    each([&](Who w) {
        Combat* c = CombatOf(w); if (!c || c->brawl != b.id) return;
        bool won = standing[c->side] && !standing[1 - c->side];
        if (w.kind == 0) {
            Player& p = players[w.idx];
            if (won) { p.fightsWon++; if (p.drunk < 20) p.fightsWonSober++; c->afterT = FD().afterMin * SECONDS_PER_GAME_MINUTE; Note(p, p.drunk < 20 ? 5 : 11, TextFormat("Won a fight in %s%s.", b.room.c_str(), p.drunk < 20 ? ", sober" : "")); }
            else if (!c->Down()) Note(p, 11, TextFormat("Came out of a fight in %s.", b.room.c_str()));
        }
        if (w.kind == 1) { Patron& p = patrons[w.idx]; p.nextGoalT = 0; if (!won && p.Has(D().Trait("good loser"))) p.mood = std::min(100.0f, p.mood + 15); }
        c->brawl = -1; c->foe = {}; c->grabbing = {}; c->grabbedBy = {}; c->move = MV_NONE; c->windT = 0;
    });
    // the bill: on the tab of whoever started it; the bartender's judgment favours whoever he likes
    if (b.bill > 0) {
        int payer = -1;
        if (b.starter.kind == 0) payer = b.starter.idx;
        else if (bar.mood < 35) for (int i = 0; i < (int)players.size(); i++) if (players[i].fight.knockouts >= 0 && Present(PlayerW(i))) { payer = i; break; }   // (he blames you anyway)
        if (payer >= 0) {
            Player& p = players[payer]; p.tab += b.bill;
            std::string list; for (size_t k = 0; k < b.broke.size() && k < 4; k++) list += (k ? ", " : "") + b.broke[k];
            Note(p, 10, TextFormat("The fight in %s cost %.0f on the tab: %s%s.", b.room.c_str(), b.bill, list.c_str(), b.broke.size() > 4 ? ", and more" : ""));
            std::string who = NameOf(PlayerW(payer));
            Say(TextFormat("The bartender adds %.0f to %s tab.", b.bill, who == "You" ? "your" : (who + "'s").c_str()));
        } else Say(TextFormat("The bartender writes %.0f in his book against %s.", b.bill, NameOf(b.starter).c_str()));
    }
}

// ---------------------------------------------------------------- a player's hands in a fight
void Night::PlayerFightInput(Player& p, float dt) {
    Input& in = p.in; Combat& C = p.fight; Who me = PlayerW(p.id);
    C.blockT = in.block ? std::max(C.blockT, 0.15f) : C.blockT;
    if (in.dodge && C.dodgeT <= 0 && !C.Busy()) { C.dodgeT = FD().dodgeT; if (p.drunk >= FD().dodgeFall) { C.fallT = 1.4f; AddPop(p.pos, 1.6f, "Whoops", {230, 200, 160, 255}); } }
    if (in.attack > 0 && in.faceYaw > -50) p.yaw = in.faceYaw;   // (you swing where you're looking)
    if (in.attack > 0) Attack(me, in.attack);
    // pick up the nearest weapon, or put down what you hold (behind the counter: the shotgun, if he isn't looking)
    if (in.pickUp && !C.Busy()) {
        if (C.held >= 0 && C.held < (int)props.size()) { Prop& h = props[C.held]; h.state = PS_OVER; h.holder = {}; h.pos = {p.pos.x + cosf(p.yaw) * 0.4f, 0.05f, p.pos.y + sinf(p.yaw) * 0.4f}; C.held = -1; }
        else {
            int k = NearestProp(p.pos, 1.6f, true);
            if (k >= 0 && props[k].kind == "shotgun") {
                if (Vector2Distance(bar.pos, p.pos) < 4 && bar.mood < 80) { Say("The bartender: \"Hands off.\""); bar.mood = std::max(0.0f, bar.mood + FD().barBehind); k = -1; }
                else { p.barred = true; Note(p, 5, "Took the bartender's shotgun from under the till."); Say("Someone's taken the shotgun from under the till."); }
            }
            if (k >= 0 && props[k].kind == "pan" && p.money >= FD().panPrice) { p.money -= FD().panPrice; Say("Tam sells you the frying pan. He respects you now."); }
            if (k >= 0) { C.held = k; props[k].state = PS_HELD; props[k].holder = me; }
        }
    }
    // smash a bottle on the bar (a 1 s animation): now it's a broken bottle, and the fight is armed
    if (in.smash && C.held >= 0 && props[C.held].kind == "bottle" && !C.Busy()) {
        bool atBar = p.pos.x > 12 && p.pos.x < 21.5f && p.pos.y > 7.6f && p.pos.y < 9.2f;
        if (atBar) { C.smashT = 1.0f; }
    }
    if (C.smashT > 0 && (C.smashT -= dt) <= 0) {
        C.smashT = 0;
        if (C.held >= 0) { Prop& b = props[C.held]; b.kind = "broken"; b.weapon = FD().Weapon("broken"); AddPop(p.pos, 1.8f, "Smash!", {255, 210, 140, 255}); damage += 3; p.tab += 3; if (C.brawl >= 0) { brawls[C.brawl].armed = true; } if (policeT < 0) { policeT = FD().policeArmed; Say("The bartender reaches for the telephone."); } }
    }
    // the alley dog: share your chips with it three times and it's yours for the night
    if (in.feedDog && Vector2Distance(p.pos, dog.pos) < 1.6f && p.money >= 5) {
        p.money -= 5; dog.fed[std::clamp(p.id, 0, 5)]++; dog.sleeping = false;
        AddPop(dog.pos, 0.9f, "*munch*", {240, 220, 160, 255});
        if (dog.fed[p.id] >= FD().dogFeeds && dog.owner != p.id) { int best = 0; for (int i = 0; i < 6; i++) if (dog.fed[i] > dog.fed[best]) best = i; if (best == p.id) { dog.owner = p.id; Note(p, 5, "Made friends with the alley dog."); Say("The alley dog follows you in, tail going."); } }
    }
    in.attack = 0; in.pickUp = false; in.smash = false; in.dodge = false; in.feedDog = false;
}

// ---------------------------------------------------------------- the step
void Night::StepBrawls(float dt) {
    // floating words
    for (auto& p : pops) { p.t -= dt; p.pos.y += dt * 0.6f; }
    pops.erase(std::remove_if(pops.begin(), pops.end(), [](const Pop& p) { return p.t <= 0; }), pops.end());
    // every fighter's timers, swings landing, grabs, knocks, bleeding
    auto each = [&](auto fn) { for (int i = 0; i < (int)players.size(); i++) fn(PlayerW(i)); for (int i = 0; i < (int)patrons.size(); i++) if (patrons[i].inside && !patrons[i].gone) fn(PatronW(i)); if (dog.owner >= 0) fn(DogW()); };
    each([&](Who w) {
        Combat& C = *CombatOf(w); Vector2& pos = *PosOf(w);
        if (C.hpMax <= 0 || (C.hp == 100 && C.hpMax == 100 && C.brawl < 0)) {   // (hit points scale with toughness)
            float mx = w.kind == 0 ? FD().hp * Toughness(players[w.idx]) : w.kind == 1 ? D().types[patrons[w.idx].type].hp * (1 + patrons[w.idx].drunk / 100) : 60;
            C.hp = C.hpMax = mx;
        }
        C.blockT = std::max(0.0f, C.blockT - dt); C.dodgeT = std::max(0.0f, C.dodgeT - dt); C.stunT = std::max(0.0f, C.stunT - dt); C.fallT = std::max(0.0f, C.fallT - dt);
        C.hitT = std::max(0.0f, C.hitT - dt); C.swingT = std::max(0.0f, C.swingT - dt * 3); C.recT = std::max(0.0f, C.recT - dt); C.afterT = std::max(0.0f, C.afterT - dt);
        if (w.kind == 0 && C.brawl >= 0) players[w.idx].lastFightT = t;
        if (C.bleedT > 0) { C.bleedT -= dt; C.hp -= 2 * dt; if (C.hp <= 0 && !C.Down()) { C.hp = 0.01f; Strike(w, w, 1, -1, false); } }
        if (C.downT > 0) {
            C.downT -= dt;
            if (C.downT <= 0) { C.downT = 0; C.hp = C.hpMax * 0.4f; if (w.kind == 0 && players[w.idx].st == State::Down) players[w.idx].st = State::Active; if (w.kind == 1) { Patron& c = patrons[w.idx]; c.mood = std::max(0.0f, c.mood - 20); c.nextGoalT = 0; c.leaveH = std::min(c.leaveH, Hour() + 0.25f); } }
        }
        // the windup ends: the blow
        if (C.windT > 0 && (C.windT -= dt) <= 0) { C.windT = 0; if (!C.Busy()) Land(*this, w); C.recT = C.move == MV_HAYMAKER ? FD().hayRec : C.move == MV_SHOVE ? FD().shoveRec : FD().jabRec; C.move = MV_NONE; }
        // a grab: held for 1.5 s, then thrown (or let go)
        if (C.grabbing.Valid()) {
            Combat* G = CombatOf(C.grabbing);
            if (!G || !Present(C.grabbing) || G->Down() || C.Busy()) { if (G) G->grabbedBy = {}; C.grabbing = {}; }
            else { float yaw = *YawOf(w); *PosOf(C.grabbing) = Vector2Add(pos, {cosf(yaw) * 0.6f, sinf(yaw) * 0.6f}); if ((C.grabT -= dt) <= 0) Attack(w, MV_GRAB); }
        }
        // a knock (a shove, a throw) carries you; a thrown body flips tables, breaks chairs and goes through windows
        if (Vector2LengthSqr(C.push) > 0.01f) {
            Vector2 before = pos;
            pos = Vector2Add(pos, Vector2Scale(C.push, dt));
            float sp = Vector2Length(C.push);
            for (int i = 0; i < (int)props.size(); i++) {
                Prop& p = props[i];
                if (p.state != PS_OK && p.state != PS_OVER) continue;
                Vector2 pp{p.pos.x, p.pos.z};
                if (p.kind == "table" && fabsf(pos.x - pp.x) < p.size.x / 2 + 0.25f && fabsf(pos.y - pp.y) < p.size.y / 2 + 0.25f) {
                    if (C.flying && sp > 4.5f) BreakProp(i, w, C.brawl, "a body through it"); else if (p.state == PS_OK) { p.state = PS_OVER; p.tilt = PI / 2; p.yaw = atan2f(C.push.y, C.push.x); for (int k = 0; k < (int)props.size(); k++) { Prop& q = props[k]; if ((q.kind == "glass" || q.kind == "bottle") && q.state == PS_OK && q.pos.y > 0.5f && fabsf(q.pos.x - pp.x) < p.size.x / 2 + 0.1f && fabsf(q.pos.z - pp.y) < p.size.y / 2 + 0.1f) BreakProp(k, w, C.brawl, "a table going over"); } }
                    C.push = Vector2Scale(C.push, 0.3f);
                } else if ((p.kind == "chair" || p.kind == "stool") && Vector2Distance(pos, pp) < 0.45f) {
                    if (C.flying && sp > 4.0f) BreakProp(i, w, C.brawl, "a body landing on it"); else { p.state = PS_OVER; p.tilt = PI / 2; }
                } else if (p.kind == "slot" && C.flying && sp > 4.5f && fabsf(pos.x - pp.x) < p.size.x / 2 + 0.3f && fabsf(pos.y - pp.y) < p.size.y / 2 + 0.3f) {
                    BreakProp(i, w, C.brawl, "a body into it"); C.push = {0, 0};
                } else if (p.kind == "window" && C.flying && sp > 3.5f && pos.y < 0.45f && fabsf(pos.x - pp.x) < 0.8f) {
                    BreakProp(i, w, C.brawl, TextFormat("%s through it", NameOf(w).c_str()));
                    if (w.kind == 1) { Patron& c = patrons[w.idx]; c.outForNight = true; c.gone = true; c.inside = false; C.brawl = -1; Say(c.name + " goes through the window and is out for the night."); }
                    C.push = {0, 0};
                }
            }
            if (w.kind != 0) Collide(pos, 0.28f);   // (players collide in StepPlayer)
            if (Vector2Distance(before, pos) < sp * dt * 0.3f) C.push = {0, 0};   // (stopped by a wall)
            C.push = Vector2Scale(C.push, std::max(0.0f, 1 - dt * 3.2f));
            if (Vector2Length(C.push) < 1.2f) C.flying = false;
        }
    });
    // flying props: arc, hit people, break on walls and the floor
    for (int i = 0; i < (int)props.size(); i++) {
        Prop& p = props[i];
        if (p.state != PS_FLYING) continue;
        p.vel.y -= 9.8f * dt; p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt)); p.yaw += p.spin * dt;
        Vector2 at{p.pos.x, p.pos.z}, was = at; Collide(at, 0.15f);
        bool wall = Vector2Distance(at, was) > 0.01f;
        Who hit{};
        auto consider = [&](Who o) { if (o == p.thrower || !Present(o)) return; Combat* c = CombatOf(o); if (!c || c->Down()) return; if (Vector2Distance(*PosOf(o), at) < 0.5f && p.pos.y > 0.2f && p.pos.y < 2.0f) hit = o; };
        for (int k = 0; k < (int)players.size(); k++) consider(PlayerW(k));
        for (int k = 0; k < (int)patrons.size(); k++) if (patrons[k].inside && !patrons[k].gone) consider(PatronW(k));
        if (hit.Valid()) {
            float dmg = p.weapon >= 0 ? FD().weapons[p.weapon].thrown : 12;
            Strike(p.thrower, hit, dmg, p.weapon, false);
            if (p.Breakable()) { p.hits += 1; if (p.kind == "bottle" || p.kind == "glass" || p.kind == "broken" || p.kind == "chair" || p.kind == "stool" || p.kind == "jagged" || p.kind == "cue") { BreakProp(i, p.thrower, p.brawl, TextFormat("thrown at %s", NameOf(hit).c_str())); continue; } }
            p.vel = Vector3Scale(p.vel, -0.2f);
        }
        if (wall || p.pos.y <= 0) {
            p.pos.x = at.x; p.pos.z = at.y;
            float sp = Vector3Length(p.vel);
            bool fragile = p.kind == "bottle" || p.kind == "glass" || p.kind == "broken";
            bool smashes = fragile || ((p.kind == "chair" || p.kind == "stool") && sp > 7);
            // a chair through the street wall's window
            for (int k = 0; k < (int)props.size(); k++) if (props[k].kind == "window" && props[k].state == PS_OK && wall && p.pos.z < 0.6f && fabsf(p.pos.x - props[k].pos.x) < 0.8f) { BreakProp(k, p.thrower, p.brawl, "a chair through it"); break; }
            if (smashes && p.Breakable()) { BreakProp(i, p.thrower, p.brawl, "thrown"); continue; }
            p.state = PS_OVER; p.pos.y = 0.05f; p.vel = {0, 0, 0}; p.tilt = PI / 2; p.spin = 0;
        }
    }
    // held props follow their holder
    for (auto& p : props) if (p.state == PS_HELD) { Vector2* h = PosOf(p.holder); float* y = YawOf(p.holder); if (!h || !y || !Present(p.holder)) { p.state = PS_OVER; p.holder = {}; continue; } p.pos = {h->x + cosf(*y) * 0.35f, 1.1f, h->y + sinf(*y) * 0.35f}; p.yaw = *y; }
    // the patrons in a fight: close in, swing, block, throw chairs, grab; cowards run
    for (int i = 0; i < (int)patrons.size(); i++) {
        Patron& c = patrons[i]; Combat& C = c.fight;
        if (!c.inside || c.gone || C.brawl < 0 || C.Down() || C.Busy()) continue;
        Who me = PatronW(i);
        if (c.Has(D().Trait("cowardly")) && C.hp < C.hpMax * 0.4f) { C.brawl = -1; C.foe = {}; c.goal = D().bar.nav.empty() ? c.pos : D().bar.nav[0]; c.path = NavPath(c.pos, c.goal); c.nextGoalT = 6; AddPop(c.pos, 2.0f, "Runs!", {200, 220, 255, 255}); continue; }
        // the foe: whoever hit them last, else the nearest of the other side
        if (!C.foe.Valid() || !Present(C.foe) || CombatOf(C.foe)->Down() || CombatOf(C.foe)->brawl != C.brawl) {
            C.foe = {}; float bd = 1e9f;
            auto consider = [&](Who o) { Combat* oc = CombatOf(o); if (!oc || oc->brawl != C.brawl || oc->side == C.side || oc->Down() || !Present(o)) return; float d = Vector2Distance(*PosOf(o), c.pos); if (d < bd) { bd = d; C.foe = o; } };
            for (int k = 0; k < (int)players.size(); k++) consider(PlayerW(k));
            for (int k = 0; k < (int)patrons.size(); k++) if (patrons[k].inside && !patrons[k].gone) consider(PatronW(k));
            if (dog.owner >= 0) consider(DogW());
        }
        if (!C.foe.Valid()) continue;
        Vector2 to = Vector2Subtract(*PosOf(C.foe), c.pos); float L = Vector2Length(to);
        float ty = atan2f(to.y, to.x); c.yaw += atan2f(sinf(ty - c.yaw), cosf(ty - c.yaw)) * std::min(1.0f, dt * 8);
        if (C.grabbedBy.Valid() || C.grabbing.Valid()) continue;
        bool violent = c.Has(D().Trait("violent"));
        // pick up a chair or a bottle in reach (the violent do), and throw it from range
        if (C.held < 0 && violent && (C.aiT -= dt) <= 0 && Rand() < 0.25f) { int k = NearestProp(c.pos, 1.4f, true); if (k >= 0 && props[k].kind != "shotgun" && props[k].kind != "knife") { C.held = k; props[k].state = PS_HELD; props[k].holder = me; } }
        if (C.held >= 0 && L > 2.0f && L < 7 && C.recT <= 0 && C.windT <= 0 && Rand() < dt * 1.2f && props[C.held].weapon >= 0 && FD().weapons[props[C.held].weapon].thrown >= 15) { Attack(me, MV_THROW); continue; }
        if (L > FD().reach * 0.85f) {
            Vector2 v = Vector2Scale(to, 2.6f / std::max(0.01f, L)); c.vel = Vector2Lerp(c.vel, v, std::min(1.0f, dt * 6));
            c.pos = Vector2Add(c.pos, Vector2Scale(c.vel, dt)); Collide(c.pos, 0.28f); c.walkPh += Vector2Length(c.vel) * dt * 1.6f;
            continue;
        }
        c.vel = Vector2Scale(c.vel, 0.6f);
        // block a haymaker you see coming
        Combat* F = CombatOf(C.foe);
        if (F && F->move == MV_HAYMAKER && F->windT > 0.2f && C.blockT <= 0 && Rand() < dt * (c.drunk < 40 ? 3.0f : 1.0f)) C.blockT = FD().blockT;
        if (C.recT <= 0 && C.windT <= 0 && (C.aiT -= dt) <= 0) {
            float r = Rand();
            int mv = r < 0.55f ? MV_JAB : r < 0.82f ? MV_HAYMAKER : r < 0.92f ? MV_SHOVE : MV_GRAB;
            Attack(me, mv);
            C.aiT = 0.4f + Rand() * 0.9f + c.drunk / 200;
        }
    }
    // the dog fights for whoever fed it most: bites and holds the nearest of the other side
    if (dog.owner >= 0 && dog.owner < (int)players.size()) {
        Player& o = players[dog.owner];
        Combat& C = dog.fight; C.brawl = o.fight.brawl; C.side = o.fight.side;
        Vector2 goal = Vector2Add(o.pos, {-cosf(o.yaw) * 0.8f, -sinf(o.yaw) * 0.8f});
        if (dog.biting.Valid()) {
            Combat* B = CombatOf(dog.biting);
            if (!B || B->Down() || !Present(dog.biting) || (dog.biteT -= dt) <= 0) { if (B) B->grabbedBy = {}; dog.biting = {}; }
            else goal = *PosOf(dog.biting);
        } else if (C.brawl >= 0) {
            Who foe{}; float bd = 6;
            for (int k = 0; k < (int)patrons.size(); k++) { Combat& pc = patrons[k].fight; if (patrons[k].inside && !patrons[k].gone && pc.brawl == C.brawl && pc.side != C.side && !pc.Down()) { float d = Vector2Distance(patrons[k].pos, dog.pos); if (d < bd) { bd = d; foe = PatronW(k); } } }
            if (foe.Valid()) { goal = *PosOf(foe); if (bd < 0.7f && C.recT <= 0) { Strike(DogW(), foe, FD().dogBite, -1, false); CombatOf(foe)->grabbedBy = DogW(); dog.biting = foe; dog.biteT = FD().dogHold; C.recT = 2.0f; AddPop(dog.pos, 1.0f, "GRR!", {255, 200, 120, 255}); } }
        }
        Vector2 to = Vector2Subtract(goal, dog.pos); float L = Vector2Length(to);
        if (L > 0.4f) { dog.vel = Vector2Lerp(dog.vel, Vector2Scale(to, std::min(4.5f, L * 2) / L), std::min(1.0f, dt * 6)); dog.yaw = atan2f(dog.vel.y, dog.vel.x); } else dog.vel = Vector2Scale(dog.vel, 0.8f);
        dog.pos = Vector2Add(dog.pos, Vector2Scale(dog.vel, dt)); Collide(dog.pos, 0.2f); dog.walkPh += Vector2Length(dog.vel) * dt * 3;
    }
    // the crowd: anyone within 3 m with a violent trait joins (against whoever they like least); the Delighted join
    // their friend's side; Sister Ash ends a fight by walking into it
    for (auto& b : brawls) {
        if (b.over) continue;
        b.t += dt; b.quietT += dt;
        std::vector<Who> in; for (int i = 0; i < (int)players.size(); i++) if (players[i].fight.brawl == b.id && Present(PlayerW(i))) in.push_back(PlayerW(i));
        for (int i = 0; i < (int)patrons.size(); i++) if (patrons[i].fight.brawl == b.id && Present(PatronW(i))) in.push_back(PatronW(i));
        int standing[2] = {0, 0}; for (Who w : in) { Combat* c = CombatOf(w); if (!c->Down()) standing[c->side]++; }
        for (int i = 0; i < (int)patrons.size(); i++) {
            Patron& c = patrons[i]; if (!c.inside || c.gone || c.fight.brawl >= 0 || c.fight.Down() || c.type == T_STAFF || c.talkingTo >= 0) continue;
            if (c.name == "Sister Ash") { for (Who w : in) if (Vector2Distance(*PosOf(w), c.pos) < 2.5f) { Say("Sister Ash walks into the middle of it. It stops."); EndBrawl(b); break; } if (b.over) break; continue; }
            bool violent = c.Has(D().Trait("violent")) && !c.Has(D().Trait("cowardly"));
            for (Who w : in) {
                if (Vector2Distance(*PosOf(w), c.pos) > FD().crowdR) continue;
                Combat* fc = CombatOf(w);
                bool friendOf = (w.kind == 1 && c.mood >= 80) || (w.kind == 0 && ((c.friendOf >> std::clamp(w.idx, 0, 7)) & 1));
                if (!violent && !friendOf) continue;
                if (Rand() > dt * 1.5f) continue;
                int side = friendOf ? fc->side : (standing[0] <= standing[1] ? 0 : 1);
                Who other{}; for (Who o : in) if (CombatOf(o)->side != side) other = o;
                if (!other.Valid()) break;
                c.fight.brawl = b.id; c.fight.side = side; c.fight.foe = other; b.size++; c.sitting = false;
                AddPop(c.pos, 2.0f, violent ? "Wades in!" : "Joins in!", {255, 200, 160, 255});
                break;
            }
        }
        if (b.over) continue;
        bool done = (standing[0] == 0 || standing[1] == 0) && b.t > 1.0f;
        if (done || b.quietT > 8) EndBrawl(b);
    }
    // the police: due after an armed fight; while they're in, anyone fighting or armed is arrested
    if (policeT > 0 && (policeT -= dt) <= 0) {   // (the police inspection event: StartEvent sets how long they stay)
        policeT = -1;
        if (!lockIn) { int pi = EventIndex("police"); if (pi >= 0 && !events[pi].started) events[pi].startH = Hour(); else ForceEvent("police", Hour()); StepEvents(0); }
    }
    if (policeInT > 0) {
        policeInT -= dt;
        for (auto& p : players) {
            if (p.st == State::Gone || p.st == State::PassedOut) continue;
            bool armed = p.fight.held >= 0 && p.fight.held < (int)props.size() && props[p.fight.held].weapon >= 0 && FD().weapons[props[p.fight.held].weapon].armed;
            if (p.fight.brawl >= 0 || armed) { Note(p, 9, "Arrested in the Gull."); Leave(p, E_ARRESTED, "a cell at the harbour station"); }
        }
        for (auto& c : patrons) if (c.inside && !c.gone && c.fight.brawl >= 0) { c.gone = true; c.inside = false; Say(c.name + " is taken away by the police."); }
        for (auto& b : brawls) if (!b.over) EndBrawl(b);
    }
}

// ---------------------------------------------------------------- --night-test's stage 4 checks (the gate: a brawl wrecks the games room and the bill is right)
int NightBrawlChecks() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    Night n; Opts o; o.players = 1; o.seed = 77; o.crowd = 1; o.events = false; n.Init(o);
    for (int i = 0; i < (int)(4 * 60 * SECONDS_PER_GAME_MINUTE / 0.1f); i++) n.Step(0.1f);   // (11 p.m.)
    Player& p = n.players[0]; p.st = State::Active; p.drunk = 30; p.pos = {5, 8}; p.yaw = 0; p.tab = 0;
    int props0 = 0; for (const auto& q : n.props) props0 += q.state == PS_OK;
    check(props0 > 60, TextFormat("the room is furnished (%d props)", props0));
    // bring a violent sailor and three regulars into the games room
    std::vector<int> crowd;
    for (auto& c : n.patrons) if (!c.gone && (c.name == "Cutter Jones" || c.name == "Boxer Mags" || c.name == "Big Ruth" || c.name == "Captain Vane" || c.name == "Harrow")) { c.inside = true; c.leaving = false; c.leaveH = 27; c.talkingTo = -1; c.playing = -1; crowd.push_back(c.id); }
    check(crowd.size() >= 4, TextFormat("a crowd in the games room (%d)", (int)crowd.size()));
    for (size_t k = 0; k < crowd.size(); k++) { Patron& c = n.patrons[crowd[k]]; c.pos = {3.0f + k * 1.2f, 7 + (k % 2) * 1.5f}; c.goal = c.pos; c.path.clear(); c.nextGoalT = 1e9f; }
    Patron& foe = n.patrons[crowd[0]];
    foe.pos = {6.1f, 8}; foe.goal = foe.pos; foe.yaw = PI;
    // the player swings first: a haymaker at the sailor
    float tab0 = p.tab, dmg0 = n.damage;
    p.in.attack = MV_HAYMAKER; n.Step(0.02f);
    for (int k = 0; k < 60; k++) n.Step(0.02f);
    check(!n.brawls.empty() && n.brawls[0].starter == PlayerW(0), "the first blow starts a brawl, and it's yours");
    // a chair, thrown; a grab and a throw into the games room's table; the rest is the crowd's
    int chair = -1; for (int i = 0; i < (int)n.props.size(); i++) if (n.props[i].kind == "chair" && n.props[i].state == PS_OK && Vector2Distance({n.props[i].pos.x, n.props[i].pos.z}, {7.6f, 4}) < 1.5f) chair = i;
    if (chair >= 0) { p.pos = {n.props[chair].pos.x, n.props[chair].pos.z + 0.3f}; p.in.pickUp = true; n.Step(0.02f); check(p.fight.held == chair, "you pick up a chair"); p.yaw = atan2f(foe.pos.y - p.pos.y, foe.pos.x - p.pos.x); p.in.attack = MV_THROW; n.Step(0.02f); }
    for (int k = 0; k < 100; k++) n.Step(0.02f);
    { p.pos = {7.6f, 2.3f}; Patron& v = n.patrons[crowd[1]]; v.fight.downT = 0; v.fight.stunT = 0; v.pos = {7.6f, 2.95f}; p.yaw = PI / 2; p.fight.recT = 0; p.fight.windT = 0;
      p.in.attack = MV_GRAB; n.Step(0.02f); check(p.fight.grabbing == PatronW(crowd[1]), "you grab someone"); p.in.attack = MV_GRAB; n.Step(0.02f); }
    for (int k = 0; k < 120; k++) n.Step(0.02f);
    // a body through the games room's window
    { int win = -1; for (int i = 0; i < (int)n.props.size(); i++) if (n.props[i].kind == "window" && fabsf(n.props[i].pos.x - 4) < 0.1f) win = i;
      Patron& v = n.patrons[crowd[2]]; v.fight.downT = 0; v.fight.stunT = 0; v.fight.grabbedBy = {}; if (v.fight.hp <= 0) v.fight.hp = 30;
      p.pos = {4, 1.6f}; v.pos = {4, 1.0f}; p.yaw = -PI / 2; p.fight.recT = 0; p.fight.windT = 0; p.fight.grabbing = {}; p.fight.stunT = 0; p.fight.fallT = 0; p.fight.downT = 0; p.st = State::Active;
      p.in.attack = MV_GRAB; n.Step(0.02f); p.in.attack = MV_GRAB; n.Step(0.02f);
      for (int k = 0; k < 60; k++) n.Step(0.02f);
      check(win >= 0 && n.props[win].state == PS_BROKEN, "a body goes through the window");
      check(v.gone && v.outForNight, "and they're out for the night"); }
    // let it run out (a minute at most)
    for (int k = 0; k < 3000 && !n.brawls[0].over; k++) {
        if (p.st == State::Active && p.fight.brawl >= 0 && !p.fight.Busy()) {   // (the player keeps swinging at the nearest foe)
            Who f{}; float bd = 9; for (int i = 0; i < (int)n.patrons.size(); i++) { const Patron& c = n.patrons[i]; if (c.inside && !c.gone && c.fight.brawl == p.fight.brawl && c.fight.side != p.fight.side && !c.fight.Down()) { float d = Vector2Distance(c.pos, p.pos); if (d < bd) { bd = d; f = PatronW(i); } } }
            if (f.Valid()) { Vector2 to = Vector2Subtract(*n.PosOf(f), p.pos); p.yaw = atan2f(to.y, to.x); if (bd > 1.1f) { p.in.moveX = to.x / bd; p.in.moveZ = to.y / bd; } else { p.in.moveX = p.in.moveZ = 0; if (p.fight.recT <= 0 && p.fight.windT <= 0) p.in.attack = MV_JAB; } }
        }
        n.Step(0.02f);
    }
    p.in.moveX = p.in.moveZ = 0;
    const Brawl& b = n.brawls[0];
    float sum = 0; int broken = 0;
    for (const auto& q : n.props) if (q.state == PS_BROKEN && q.brawl == b.id) { sum += FD().Price(q.kind); broken++; }
    float snapped = 0; for (const auto& s : b.broke) if (s.find("snapped") != std::string::npos) snapped += FD().Price("cue");
    check(b.over, TextFormat("the brawl ends (%.0f s, %d fighters, %d knockouts)", b.t, b.size, b.kos));
    check(broken >= 3 && b.room == "The games room", TextFormat("it wrecks the games room: %d things broken", broken));
    std::string list; for (const auto& s : b.broke) list += "\n          " + s;
    check(fabsf(b.bill - (sum + snapped)) < 0.01f, TextFormat("the bill is right: %.0f for what broke%s", b.bill, list.c_str()));
    check(fabsf((p.tab - tab0) - b.bill) < 0.01f || p.st == State::Gone, TextFormat("and it's on the starter's tab (%.0f)", p.tab - tab0));
    check(n.damage - dmg0 >= b.bill, "the night's damage counts it");
    check(n.bar.mood < 55, TextFormat("the bartender's sour about it (%.0f)", n.bar.mood));
    // the after-fight swing, knockouts, Sister Ash, the police
    { Night m; Opts mo; mo.players = 1; mo.seed = 5; mo.events = false; m.Init(mo); Player& q = m.players[0]; q.st = State::Active; q.pos = {18, 5};
      int a = -1, c2 = -1; for (auto& c : m.patrons) { if (a < 0 && c.reg >= 0 && c.name != "Sister Ash" && c.type != T_STAFF) a = c.id; else if (c2 < 0 && c.reg >= 0 && c.name != "Sister Ash" && c.type != T_STAFF) c2 = c.id; }
      Patron& A = m.patrons[a]; A.inside = true; A.pos = {19, 5}; A.nextGoalT = 1e9f;
      m.Step(0.02f); m.StartBrawl(PlayerW(0), PatronW(a), PlayerW(0));
      for (int k = 0; k < 3 && q.st != State::Gone; k++) { q.fight.hp = 1; q.st = State::Active; q.fight.downT = 0; m.Strike(PatronW(a), PlayerW(0), 50, -1, false); }
      check(q.st == State::Gone && q.ending == E_THROWN_OUT, "three knockouts in a night and the bartender bars you");
      Night s; s.Init(mo); Player& r = s.players[0]; r.st = State::Active; r.pos = {18, 5};
      Patron& B = s.patrons[c2]; B.inside = true; B.pos = {19, 5}; B.nextGoalT = 1e9f;
      int ash = -1; for (auto& c : s.patrons) if (c.name == "Sister Ash") ash = c.id;
      s.Step(0.02f); s.StartBrawl(PlayerW(0), PatronW(c2), PlayerW(0));
      if (ash >= 0) { Patron& S = s.patrons[ash]; S.inside = true; S.gone = false; S.pos = {18.5f, 6.2f}; S.nextGoalT = 1e9f; }
      for (int k = 0; k < 100; k++) s.Step(0.02f);
      check(ash >= 0 && s.brawls[0].over, "Sister Ash ends a fight by walking into it");
      // a bottle smashed on the bar: the fight's armed, and the police are on their way
      Night t; t.Init(mo); Player& u = t.players[0]; u.st = State::Active; u.pos = {13.6f, 8.6f};
      for (int i = 0; i < (int)t.props.size(); i++) if (t.props[i].kind == "bottle" && Vector2Distance({t.props[i].pos.x, t.props[i].pos.z}, u.pos) < 1.5f) { u.fight.held = i; t.props[i].state = PS_HELD; t.props[i].holder = PlayerW(0); break; }
      u.in.smash = true; t.Step(0.02f); for (int k = 0; k < 60; k++) t.Step(0.02f);
      check(u.fight.held >= 0 && t.props[u.fight.held].kind == "broken" && t.policeT > 0, TextFormat("a bottle smashed on the bar is a broken bottle, and the police are %0.f s away", t.policeT));
      t.policeT = 0.01f; Patron& D2 = t.patrons[a]; D2.inside = true; D2.pos = {15, 8.3f}; t.StartBrawl(PlayerW(0), PatronW(a), PlayerW(0));
      for (int k = 0; k < 20; k++) t.Step(0.02f);
      check(u.st == State::Gone && u.ending == E_ARRESTED, "the police arrest whoever's still fighting");
      // the dog: fed three times, it follows you
      Night w; w.Init(mo); Player& x = w.players[0]; x.st = State::Active; x.pos = Vector2Add(w.dog.pos, {0.5f, 0});
      for (int k = 0; k < 3; k++) { x.in.feedDog = true; w.Step(0.02f); }
      check(w.dog.owner == 0, "feed the alley dog three times and it's yours");
    }
    return fails;
}

} // namespace no
