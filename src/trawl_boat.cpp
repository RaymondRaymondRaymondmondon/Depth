// The Gannet: the sea's surface, the hull (buoyancy, roll, pitch, heave, weight on board), the engine, the six hull
// sections with their leaks and the bilge, and the crew walking her wet deck (design doc, "Boat and sea physics").
// Headless: the host steps it at 60 Hz, the scene draws it, --trawl-boat-test proves it.
#include "trawl.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tw {

// ---------------------------------------------------------------- the sea
void Sea::Set(Weather w, uint32_t s) {
    weather = w; seed = s ? s : 1;
    swell = WeatherSwell(w);
    float a = (seed % 628) / 100.0f;
    float ws = w == Weather::Glass ? 0 : w == Weather::Storm ? 14 : w == Weather::Squall ? 10 : w == Weather::Rain ? 6 : 3;
    wind = {cosf(a) * ws, sinf(a) * ws};
}
float Sea::Height(float x, float z) const {
    if (swell <= 0) return 0;
    // four directional waves: the main swell down the wind, a cross swell, and two short chops
    static const float dirs[4] = {0.0f, 0.9f, -0.7f, 2.1f}, lens[4] = {42, 27, 13, 8}, amps[4] = {0.55f, 0.25f, 0.12f, 0.08f};
    float base = atan2f(wind.y, wind.x) + (seed % 17) * 0.01f;
    float h = 0;
    for (int i = 0; i < 4; i++) {
        float a = base + dirs[i], k = 6.2832f / lens[i], w = sqrtf(9.81f * k);
        h += amps[i] * swell * sinf(k * (x * cosf(a) + z * sinf(a)) - w * t + i * 1.7f + (seed % 31));
    }
    return h;
}

// ---------------------------------------------------------------- the boat
Boat::Boat() {
    for (int i = 0; i < SEC_COUNT; i++) { integrity[i] = integrityMax[i] = D().sectionMax; patched[i] = false; }
}
int SectionAt(Vector2 d) {
    int row = d.x > 3.6f ? 0 : d.x > -3.6f ? 1 : 2;
    return row * 2 + (d.y > 0 ? 1 : 0);
}
Vector2 SectionSpot(int s) {
    // somewhere open to stand in each section (clear of the wheelhouse, the drum, the table and the mast)
    static const Vector2 AT[SEC_COUNT] = {{6.6f, -1.2f}, {6.6f, 1.2f}, {-1.6f, -2.0f}, {-1.6f, 2.0f}, {-7.8f, -1.6f}, {-7.8f, 1.6f}};
    return AT[std::clamp(s, 0, SEC_COUNT - 1)];
}
bool Gannet::StartPatch(int ci) {
    Crew& c = crew[ci];
    if (c.overboard || c.dead || c.fallen || c.station >= 0 || c.patchSec >= 0) return false;
    int s = SectionAt(c.p);
    if (boat.integrity[s] >= D().leakBelow || boat.patched[s]) return false;
    for (const auto& o : crew) if (o.patchSec == s) return false;   // (one hand to a leak)
    if (c.patchKits <= 0) {
        // the ship's kits: borrow one from whoever aboard is carrying them
        int from = -1; for (int k = 0; k < (int)crew.size(); k++) if (crew[k].patchKits > 0 && !crew[k].overboard) from = k;
        if (from < 0) { Say("No patch kit aboard"); return false; }
        crew[from].patchKits--; c.patchKits++;
    }
    c.patchSec = s; c.patchT = c.role == Role::Bosun ? 3.0f : 6.0f;
    Say(std::string("Patching the leak: ") + SectionName(s));
    return true;
}
int Gannet::PatchKits() const { int n = 0; for (const auto& c : crew) if (!c.overboard) n += c.patchKits; return n; }
float Boat::TotalMass() const {
    float m = D().dryMass + bilge;
    for (const auto& l : loads) m += l.kg;
    return m;
}
float Boat::Freeboard() const { return D().freeboard - (TotalMass() - D().dryMass) / (D().waterplane * 1025.0f); }
Vector2 Boat::ToWorld(Vector2 d) const {
    Vector2 f = Forward(), s{-f.y, f.x};   // starboard is to the right of the bow
    return {pos.x + f.x * d.x + s.x * d.y, pos.y + f.y * d.x + s.y * d.y};
}
Vector2 Boat::ToDeck(Vector2 w) const {
    Vector2 f = Forward(), s{-f.y, f.x}, d{w.x - pos.x, w.y - pos.y};
    return {Vector2DotProduct(d, f), Vector2DotProduct(d, s)};
}
float Boat::Speed() const { return Vector2DotProduct(vel, Forward()); }
void Boat::Hit(int s, float dmg) {
    if (s < 0 || s >= SEC_COUNT) return;
    integrity[s] = std::max(0.0f, integrity[s] - dmg);
    patched[s] = false;                    // (a fresh blow opens a patched seam again)
}
void Boat::Shovel(float kg) { float k = std::min(kg, bunker); bunker -= k; firebox += k; }
void Boat::Bleed(float dt) { pressure = std::max(0.0f, pressure - D().bleedRate * dt); }
void Boat::Pump(float kg) { bilge = std::max(0.0f, bilge - kg); }

void Boat::Step(float dt, const Sea& sea) {
    const TrawlData& K = D();
    if (sunk) { heave -= 0.6f * dt; return; }
    float M = TotalMass();
    float sink = (M - K.dryMass) / (K.waterplane * 1025.0f);
    // ---- the water under her: eight hull points against the waves
    const Vector2 pts[8] = {{9, -2.4f}, {9, 2.4f}, {0, -2.9f}, {0, 2.9f}, {-9, -2.6f}, {-9, 2.6f}, {10.6f, 0}, {-10.6f, 0}};
    float h[8], avg = 0;
    for (int i = 0; i < 8; i++) { Vector2 w = ToWorld(pts[i]); h[i] = sea.Height(w.x, w.y); avg += h[i]; }
    avg /= 8;
    float port = (h[0] + h[2] + h[4]) / 3, star = (h[1] + h[3] + h[5]) / 3;
    float fore = (h[0] + h[1] + h[6]) / 3, aft = (h[4] + h[5] + h[7]) / 3;
    float rollWave = -atanf((star - port) / (K.beam * 0.85f)) * K.rollGain;          // starboard water higher lifts starboard: port goes down
    {   // the sea alone rolls her no further than the weather's figure (design doc: a squall "up to 20 deg"): a soft limit
        float cap = std::max(1.0f, WeatherRoll(sea.weather)) * DEG2RAD * 0.8f;        // (the swing overshoots the target a little)
        rollWave = cap * tanhf(rollWave / cap);
    }
    float pitchWave = atanf((fore - aft) / (K.length * 0.8f)) * K.pitchGain;          // bow water higher lifts the bow
    // ---- weight on board: every load's moment, and the bilge water sloshing to the low side (free surface)
    float heelKgM = extraHeelTorque, trimKgM = 0;
    for (const auto& l : loads) { heelKgM += l.kg * l.at.y; trimKgM += l.kg * l.at.x; }
    heelKgM += bilge * 1.2f * sinf(roll);   // (the water in her sloshes to the low side: less stable, not a capsize by itself)
    float gmEff = std::max(0.15f, K.gm - bilge / M * 4.0f);
    float rollTarget = rollWave + atanf(heelKgM / (M * gmEff));
    float pitchTarget = pitchWave - atanf(trimKgM / (M * 15.0f));           // weight aft lifts the bow
    float wr = 6.2832f / K.rollPeriod, wp = 6.2832f / K.pitchPeriod;
    rollVel += (wr * wr * (rollTarget - roll) - 2 * K.rollDamp * wr * rollVel) * dt;
    pitchVel += (wp * wp * (pitchTarget - pitch) - 2 * K.pitchDamp * wp * pitchVel) * dt;
    roll += rollVel * dt; pitch += pitchVel * dt;
    roll = std::clamp(roll, -1.4f, 1.4f);
    float heaveTarget = avg - sink;
    heaveVel += (4.0f * (heaveTarget - heave) - 2.5f * heaveVel) * dt;
    heave += heaveVel * dt;
    // ---- water in her: leaking sections, green water over a rail she has put under
    for (int s = 0; s < SEC_COUNT; s++) {
        float in = integrity[s];
        if (patched[s] || in >= K.leakBelow) continue;
        bilge += (in < K.floodBelow ? K.floodRate : K.leakRate * (1 - in / K.leakBelow) + 8) * dt;
    }
    float fb = Freeboard();
    float rail = fb - fabsf(sinf(roll)) * K.beam * 0.5f;
    if (rail < 0 || fabsf(roll) * 57.2958f > K.beamEnds) { float kg = K.overRailKgPerS * std::max(0.05f, -rail) * dt; bilge += kg; greenWater += kg; }
    if (fb < -0.05f) { sunk = true; return; }
    // ---- the engine: coal burns hotter the fuller the firebox; the telegraph draws steam into the shaft
    int tel = std::clamp(telegraph, -1, 3), step = std::abs(tel);
    float draw = tel < 0 ? K.telegraphDraw[1] : K.telegraphDraw[step];
    if (firebox > 0) {
        float rate = (K.burnBase + K.burnPerDraw * step) * std::clamp(firebox / 4.0f, 0.3f, 2.5f);
        float burn = std::min(firebox, rate * dt);
        firebox -= burn;
        pressure += burn * K.heatPerKg;           // (heat per kg of coal, as pressure)
    }
    pressure -= (K.pressureLoss + draw * K.drawPerStep) * dt;
    pressure = std::clamp(pressure, 0.0f, 1.4f);
    if (pressure > K.redAt) {
        redT += dt;
        if (redT > K.redGrace) { valveT = K.valveStop; fireT = 8; pressure -= 0.5f; redT = 0; }   // the relief valve blows: steam, a fire, a dead screw
    } else redT = std::max(0.0f, redT - dt);
    if (valveT > 0) valveT -= dt;
    if (fireT > 0) fireT -= dt;
    float steam = std::clamp((pressure - 0.2f) / (K.greenLo - 0.2f), 0.0f, 1.0f);
    float want = valveT > 0 ? 0 : draw * steam;
    shaft += std::clamp(want - shaft, -0.5f * dt, 0.5f * dt);
    noise = shaft > 0.05f ? K.noiseByTelegraph[step] * noiseMult : 0;
    // ---- through the water: thrust, drag, the rudder, wind and current
    Vector2 f = Forward(), side{-f.y, f.x};
    Vector2 rel = Vector2Subtract(vel, sea.current);
    float vf = Vector2DotProduct(rel, f), vs = Vector2DotProduct(rel, side);
    float thrust = K.maxThrust * thrustMult * thrustMult * shaft * (tel < 0 ? -1.0f : 1.0f);   // (drag goes as speed squared: 1.3x speed wants 1.69x thrust)
    float Ff = thrust - K.dragFwd * vf * fabsf(vf), Fs = -K.dragSide * vs * fabsf(vs);
    Vector2 windF = Vector2Scale(sea.wind, 60 * Vector2Length(sea.wind));
    Vector2 acc = Vector2Scale(Vector2Add(Vector2Add(Vector2Add(Vector2Scale(f, Ff), Vector2Scale(side, Fs)), windF), extraForce), 1.0f / M);
    vel = Vector2Add(vel, Vector2Scale(acc, dt));
    pos = Vector2Add(pos, Vector2Scale(vel, dt));
    float yawAcc = rudder * (vf * K.rudderYaw + shaft * 0.15f) - K.yawDamp * yawRate;
    yawRate += yawAcc * dt;
    heading += yawRate * dt;
    extraHeelTorque = 0; extraForce = {0, 0};
}

// ---------------------------------------------------------------- the deck
static float HalfBeam(float x) {   // the hull's half-width along her length: straight, then the bow tapers
    if (x > 5) return std::max(0.5f, 3.0f - (x - 5) * 0.42f);
    if (x < -10) return 3.0f - (-10 - x) * 0.8f;
    return 3.0f;
}
// Moored, her port side lies along the quay: a gangplank amidships and the planks ashore.
bool QuayWalkable(Vector2 p) {
    if (p.x > -1.1f && p.x < 1.1f && p.y > -4.0f && p.y < -2.4f) return true;   // the gangplank
    return p.x > -13.5f && p.x < 13.5f && p.y > -9.6f && p.y < -3.9f;
}
static bool gMoored = false;
static bool Walkable(Vector2 p, int deck) {
    if (deck == 0 && gMoored && (p.y < -2.4f) && QuayWalkable(p)) return true;
    if (deck == 1) return p.x > -8.4f && p.x < -2.9f && fabsf(p.y) < 2.3f && !(p.x > -5.8f && p.x < -3.8f && p.y < -0.6f);   // the engine room; the boiler against its port side
    if (p.x < -10.9f || p.x > 10.6f || fabsf(p.y) > HalfBeam(p.x) - 0.3f) return false;
    // the wheelhouse walls (its door is on the aft side, amidships)
    bool inX = p.x > 0.9f && p.x < 5.1f, inY = fabsf(p.y) < 2.1f;
    if (inX && inY) {
        bool wall = p.x < 1.15f || p.x > 4.85f || fabsf(p.y) > 1.85f;
        bool door = p.x < 1.15f && fabsf(p.y) < 0.7f;
        if (wall && !door) return false;
    }
    if (p.x > -9.9f && p.x < -8.9f && fabsf(p.y) < 1.2f) return false;           // the net drum
    if (p.x > -2.9f && p.x < -1.5f && p.y > 1.8f) return false;                  // the gutting table
    if (fabsf(p.x - 0.2f) < 0.35f && fabsf(p.y) < 0.35f) return false;           // the lantern mast
    return true;
}
static const Vector2 LADDER{-3.4f, -1.8f};   // down to the engine room (the same spot below)

// ---------------------------------------------------------------- the boat and her crew
void Gannet::Init(int n, uint32_t seed, Weather w) {
    *this = Gannet{};
    sea.Set(w, seed);
    boat.telegraph = 0;
    for (int i = 0; i < n; i++) {
        Crew c; c.slot = i; c.bot = i > 0; c.role = (Role)(i % (int)Role::COUNT);
        c.p = {-1.0f - i * 0.9f, (i % 2 ? 1.0f : -1.0f) * 0.8f};
        c.patchKits = c.role == Role::Bosun ? 1 : 0;
        crew.push_back(c);
    }
    // the four rods (stage 4's Chandler sells the rest of the tackle; stage 2 rigs a spread)
    const auto& S = Stations();
    for (int i = 0; i < (int)S.size(); i++) {
        Rod r; r.station = i; r.rng = seed * 31u + i * 977u + 5;
        switch (S[i].kind) {
            case StationKind::PortRod: r.tackle = Tackle::Light; break;
            case StationKind::StarRod: r.tackle = Tackle::Medium; break;
            case StationKind::SternRodP: r.tackle = Tackle::Heavy; break;
            case StationKind::SternRodS: r.tackle = Tackle::DeepDrop; r.line = LineType::Braid; break;
            default: continue;
        }
        r.fight.drag = 0.33f * TackleOf(r.tackle).strength;
        rods.push_back(r);
    }
}
int Gannet::DeckFish() const { int n = 0; for (const auto& h : hold) if (!h.gutted) n++; return n; }
void Gannet::Say(const std::string& s) { log.push_back(s); if (log.size() > 12) log.erase(log.begin()); }

void Gannet::Move(int ci, Vector2 wish, bool brace, float dt) {
    Crew& c = crew[ci];
    gMoored = moored;
    if (c.overboard) {
        // treading water: a slow swim, screen-relative like the deck (the deck frame turned into the sea's)
        if (c.dead) return;
        float l = Vector2Length(wish);
        if (l > 1) wish = Vector2Scale(wish, 1 / l);
        Vector2 f = boat.Forward(), sd{-f.y, f.x};
        Vector2 w{f.x * wish.x + sd.x * wish.y, f.y * wish.x + sd.y * wish.y};
        c.swim = Vector2Add(c.swim, Vector2Scale(w, 0.9f * dt));
        return;
    }
    float burnSlow = c.Has(INJ_BURN) ? 0.7f : 1.0f;
    c.braced = brace || c.station >= 0;
    if (c.station >= 0 || c.fallen) wish = {0, 0};
    float l = Vector2Length(wish);
    if (l > 1) wish = Vector2Scale(wish, 1 / l);
    if (l > 0.1f) c.facing = Vector2Normalize(wish);
    float speed = D().walk * burnSlow * (c.carryKg > 30 ? 0.5f : 1.0f) * (c.braced && c.station < 0 ? 0.4f : 1.0f);
    Vector2 want = Vector2Scale(wish, speed);
    // the wet deck: past 12 deg of roll an unbraced hand slides to the low side; past 25 deg they fall
    float rollDeg = boat.RollDeg(), pitchDeg = boat.pitch * 57.2958f;
    Vector2 slide{0, 0};
    if (c.deck == 0 && !c.braced) {
        if (fabsf(rollDeg) > D().braceRoll) slide.y = (rollDeg > 0 ? 1 : -1) * D().slideAccel * (fabsf(rollDeg) - D().braceRoll) / 13.0f;
        if (fabsf(pitchDeg) > D().braceRoll) slide.x = (pitchDeg > 0 ? -1 : 1) * D().slideAccel * (fabsf(pitchDeg) - D().braceRoll) / 13.0f;
        if (fabsf(rollDeg) > D().fallRoll && !c.fallen) { c.fallen = true; c.fallT = D().fallTime; Say("A hand goes down on the wet deck"); { static uint32_t fr = 2463534242u; fr ^= fr << 13; fr ^= fr >> 17; fr ^= fr << 5; if (fr % 4 == 0) Injure(ci, INJ_BROKEN_ARM, "a fall on the wet deck"); } }
    }
    if (c.fallen) { c.fallT -= dt; if (c.fallT <= 0 && fabsf(rollDeg) < D().fallRoll) c.fallen = false; }
    // (sliding, the feet barely grip: the hand's own steps fight the slide only weakly)
    bool sliding = Vector2Length(slide) > 0.01f;
    c.v = Vector2Add(Vector2Lerp(c.v, want, std::min(1.0f, dt * (sliding ? 2.0f : 12.0f))), Vector2Scale(slide, dt));
    Vector2 np = Vector2Add(c.p, Vector2Scale(c.v, dt));
    if (Walkable(np, c.deck)) c.p = np;
    else {
        Vector2 nx{np.x, c.p.y}, ny{c.p.x, np.y};
        if (Walkable(nx, c.deck)) c.p = nx; else c.v.x = 0;
        if (Walkable(ny, c.deck)) c.p.y = ny.y; else {
            // slid into the rail: on her beam ends, over it goes
            if (c.deck == 0 && !c.braced && fabsf(rollDeg) > D().beamEnds && fabsf(c.p.y) > HalfBeam(c.p.x) - 0.6f) { GoOverboard(ci, "over the rail on her beam ends"); }
            c.v.y = 0;
        }
    }
}
bool Gannet::TakeStation(int ci) {
    Crew& c = crew[ci];
    if (c.overboard || c.fallen) return false;
    if (Vector2Distance(c.p, LADDER) < 0.8f && !c.dead) { c.deck = 1 - c.deck; c.station = -1; return true; }   // the ladder
    int s = NearestStation(c.p, c.deck, 1.1f);
    if (s < 0) return false;
    if (c.dead && Stations()[s].kind != StationKind::Bell) return false;   // a ghost touches only the bell
    for (const auto& o : crew) if (&o != &c && o.station == s) return false;   // (one hand per station)
    c.station = s;
    return true;
}
void Gannet::LeaveStation(int ci) { crew[ci].station = -1; }
void Gannet::Primary(int ci, bool held, float dt) {
    Crew& c = crew[ci];
    if (c.station < 0) return;
    float rate = (c.role == Role::Bosun ? 1.25f : 1.0f) * (c.Has(INJ_BROKEN_ARM) ? 0.5f : 1.0f);   // the Bosun's perk: winch, pump and shovel 25% faster; a broken arm halves it
    switch (Stations()[c.station].kind) {
        case StationKind::Boiler:
            if (!held) break;
            c.strokeT += dt * rate;
            while (c.strokeT >= 0.6f) { c.strokeT -= 0.6f; boat.Shovel(D().shovelKg); }
            break;
        case StationKind::Pumps:
            if (!held) break;
            c.strokeT += dt * rate;
            while (c.strokeT >= D().strokeTime) { c.strokeT -= D().strokeTime; boat.Pump(D().pumpKgPerStroke * (secondPump ? 2 : 1)); }
            break;
        case StationKind::Bell: if (held) Say("The bell"); break;
        case StationKind::Gutting: {
            // gut, grade and ice the catch one fish at a time; the guts go over the rail
            if (!held) { gutT = 0; break; }
            // bycatch first: back over the side through the sorting chute (a protected turtle alive, within its minute)
            for (int i = 0; i < (int)hold.size(); i++) if (hold[i].bycatch || hold[i].protectedSp) {
                gutT += dt * rate;
                if (gutT >= 0.8f) { gutT = 0; Say(TextFormat("Returned over the side: %s", hold[i].name.c_str())); hold.erase(hold.begin() + i); }
                return;
            }
            int f = -1; for (int i = 0; i < (int)hold.size(); i++) if (!hold[i].gutted) { f = i; break; }
            if (f < 0) break;
            gutT += dt * rate;
            float need = 1.2f + std::min(3.0f, hold[f].kg * 0.08f);
            if (gutT >= need) {
                gutT = 0;
                CatchRec& r = hold[f];
                r.gutted = true;
                float iceNeed = r.kg * 0.5f;
                if (ice >= iceNeed) { ice -= iceNeed; r.iced = true; Say(TextFormat("Gutted and iced: %s, %s", r.name.c_str(), KgText(r.kg).c_str())); }
                else Say(TextFormat("Gutted, but no ice: the %s will spoil", r.name.c_str()));
                GutsOverboard(r.kg);
            }
            break;
        }
        default: break;
    }
}
void Gannet::Secondary(int ci, bool held, float dt) {
    Crew& c = crew[ci];
    if (c.station >= 0 && held && Stations()[c.station].kind == StationKind::Boiler) boat.Bleed(dt);
}
void Gannet::Scroll(int ci, float amount) {
    Crew& c = crew[ci];
    if (c.station < 0 || amount == 0) return;
    if (Stations()[c.station].kind == StationKind::Helm) boat.telegraph = std::clamp(boat.telegraph + (amount > 0 ? 1 : -1), -1, 3);
    if (Stations()[c.station].kind == StationKind::Lantern) {
        int was = boat.lantern;
        boat.lantern = std::clamp(boat.lantern + (amount > 0 ? 1 : -1), 0, searchlight ? 3 : 2);
        if (boat.lantern != was) { static const char* N[4] = {"hooded", "low", "full", "the searchlight"}; Say(std::string("Lantern: ") + N[boat.lantern]); }
    }
    if (Stations()[c.station].kind == StationKind::Sonar) sonar.band = (sonar.band + (amount > 0 ? 1 : 3)) % 4;   // the depth dial
}
void Gannet::Steer(int ci, float amount, float dt) {
    Crew& c = crew[ci];
    if (c.station < 0 || Stations()[c.station].kind != StationKind::Helm) return;
    boat.rudder = std::clamp(boat.rudder + amount * dt * 1.5f, -1.0f, 1.0f);
    if (amount == 0) boat.rudder *= powf(0.3f, dt);     // (the wheel eases back when let go)
}

void Gannet::Step(float dt) {
    time += dt; sea.t += dt;
    // the hands' weight into the boat
    boat.loads.clear();
    for (const auto& c : crew) if (!c.overboard) boat.loads.push_back({c.deck == 1 ? Vector2{c.p.x, c.p.y * 0.5f} : c.p, D().crewMass + c.carryKg});
    int leaks = 0; for (int s = 0; s < SEC_COUNT; s++) if (boat.integrity[s] < D().leakBelow && !boat.patched[s]) leaks++;
    bool wasSunk = boat.sunk; float valve0 = boat.valveT;
    if (botsOn) StepBots(dt);
    StepRods(dt);
    StepGear(dt);
    StepSonar(dt);
    if (eco) EcoTick(*eco, *this, dt);
    boat.Step(dt, sea);
    if (moored) { boat.pos = moorPos; boat.heading = moorHeading; boat.vel = {0, 0}; boat.yawRate = 0; boat.roll *= 0.9f; boat.pitch *= 0.9f; }
    // the catch spoils: 1% a real minute on deck, 0.2% gutted and iced
    for (auto& h : hold) h.fresh = std::max(0.0f, h.fresh - dt / 60.0f * (h.iced ? 0.002f : 0.01f));
    if (boat.sunk && !wasSunk) Say("The Gannet founders");
    if (boat.valveT > 0 && valve0 <= 0) Say("The relief valve blows: steam in the engine room, the screw stops");
    // a hand patching a leak (a Bosun in 3 s, anyone else in 6): stand in the leaking section with a patch kit and
    // stay there (StartPatch; the scene's E at a leak, or a bot); walking off, falling or going over loses it
    for (auto& c : crew) if (c.patchSec >= 0) {
        if (c.overboard || c.dead || c.fallen || c.station >= 0 || SectionAt(c.p) != c.patchSec || Vector2Length(c.v) > 0.6f) { c.patchSec = -1; continue; }
        c.patchT -= dt;
        if (c.patchT <= 0) { boat.patched[c.patchSec] = true; c.patchKits--; Say(std::string("Leak patched: ") + SectionName(c.patchSec)); c.patchSec = -1; }
    }
    (void)leaks;
}

// ---------------------------------------------------------------- --trawl-boat-test
int RunTrawlBoatTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Trawl stage 1: the boat alone\n");
    // she rolls in a swell and sits steady in a calm
    {
        Gannet g; g.Init(1, 7, Weather::Squall);
        float lo = 0, hi = 0;
        for (int i = 0; i < 60 * 40; i++) { g.Step(1 / 60.0f); lo = std::min(lo, g.boat.RollDeg()); hi = std::max(hi, g.boat.RollDeg()); }
        check(hi - lo > 8 && hi < 30 && lo > -30, TextFormat("a squall rolls her %.1f to %.1f deg (up to 20)", lo, hi));
        Gannet c; c.Init(1, 7, Weather::Glass);
        float m = 0; for (int i = 0; i < 60 * 20; i++) { c.Step(1 / 60.0f); m = std::max(m, fabsf(c.boat.RollDeg())); }
        check(m < 1.5f, TextFormat("on the Glass she lies still (%.2f deg)", m));
    }
    // she lists under weight: five hands at the port rail
    {
        Gannet g; g.Init(5, 3, Weather::Glass);
        for (int i = 0; i < 60 * 10; i++) g.Step(1 / 60.0f);
        float r0 = g.boat.RollDeg();
        for (auto& c : g.crew) c.p = {-0.5f - c.slot * 0.8f, -2.6f};
        for (int i = 0; i < 60 * 15; i++) g.Step(1 / 60.0f);
        float r1 = g.boat.RollDeg();
        check(r1 < r0 - 2.0f, TextFormat("five hands at the port rail list her to port (%.1f -> %.1f deg)", r0, r1));
        // a full net on the stern pitches the bow up
        g.boat.loads.clear();
        for (auto& c : g.crew) c.p = {0, 0};
        float p0 = 0; for (int i = 0; i < 60 * 10; i++) { g.Step(1 / 60.0f); } p0 = g.boat.pitch * 57.3f;
        float p1 = 0;
        for (int i = 0; i < 60 * 15; i++) { g.boat.loads.push_back({{-10, 0}, 3000}); g.boat.Step(1 / 60.0f, g.sea); g.sea.t += 1 / 60.0f; g.boat.loads.pop_back(); }
        // (Gannet::Step rebuilds the loads, so the net is held on the boat directly here)
        p1 = g.boat.pitch * 57.3f;
        check(p1 > p0 + 0.5f, TextFormat("three tonnes of net on the stern lift the bow (%.2f -> %.2f deg)", p0, p1));
    }
    // she makes way on steam, and a fire too full blows the relief valve
    {
        Gannet g; g.Init(1, 5, Weather::Calm);
        g.boat.telegraph = 2; g.boat.pressure = 0.7f; g.boat.firebox = 4;
        float dist = 0; Vector2 p0 = g.boat.pos;
        for (int i = 0; i < 60 * 60; i++) { g.Step(1 / 60.0f); if (g.boat.firebox < 3) g.boat.Shovel(D().shovelKg); }
        dist = Vector2Distance(p0, g.boat.pos);
        check(dist > 150 && g.boat.valveT <= 0, TextFormat("at half ahead, fed, she makes %.0f m in a minute (%.1f m/s)", dist, g.boat.Speed()));
        g.boat.telegraph = 1;
        for (int i = 0; i < 60 * 60 && g.boat.valveT <= 0; i++) { g.boat.firebox = 10; g.Step(1 / 60.0f); }
        check(g.boat.valveT > 0, "an overfed firebox runs the pressure red and blows the relief valve");
        float sp = g.boat.shaft;
        for (int i = 0; i < 60 * 3; i++) g.Step(1 / 60.0f);
        check(g.boat.shaft < sp && g.boat.valveT > 10, "the screw stops for 20 s");
        // a sack at half lasts about three minutes
        Gannet h; h.Init(1, 5, Weather::Calm);
        h.boat.telegraph = 2; h.boat.pressure = 0.7f; h.boat.firebox = 0; h.boat.bunker = D().sackKg;
        float t = 0; while (t < 600 && (h.boat.bunker > 0 || h.boat.firebox > 0.05f)) { if (h.boat.firebox < 3) h.boat.Shovel(D().shovelKg); h.Step(1 / 60.0f); t += 1 / 60.0f; }
        check(t > 120 && t < 260, TextFormat("a sack of coal runs her %.0f s at half (the doc: about 3 minutes)", t));
    }
    // she floods and sinks; the pumps hold a leak
    {
        Gannet g; g.Init(1, 9, Weather::Calm);
        g.boat.Hit(SEC_MID_P, 70);    // a leak (integrity 30)
        for (int i = 0; i < 60 * 30; i++) g.Step(1 / 60.0f);
        float leaked = g.boat.bilge;
        Gannet p; p.Init(1, 9, Weather::Calm);
        p.boat.Hit(SEC_MID_P, 70);
        for (int i = 0; i < 60 * 30; i++) { p.Step(1 / 60.0f); p.boat.Pump(D().pumpKgPerStroke / D().strokeTime / 60.0f); }
        check(leaked > 300 && p.boat.bilge < leaked * 0.2f, TextFormat("a leak lets in %.0f kg in 30 s; one hand on the pump keeps her dry (%.0f kg)", leaked, p.boat.bilge));
        g.boat.Hit(SEC_MID_S, 95); g.boat.Hit(SEC_STERN_P, 95);   // flooding
        float t = 0; float rollAt = 0;
        while (t < 900 && !g.boat.sunk) { g.Step(1 / 60.0f); t += 1 / 60.0f; if (t > 20 && rollAt == 0) rollAt = g.boat.RollDeg(); }
        check(g.boat.sunk && t > 20, TextFormat("two sections flooding sink her in %.0f s (listing %.1f deg on the way)", t, rollAt));
        Gannet q; q.Init(1, 9, Weather::Calm);
        q.boat.Hit(SEC_MID_P, 70); q.boat.patched[SEC_MID_P] = true;
        for (int i = 0; i < 60 * 30; i++) q.Step(1 / 60.0f);
        check(q.boat.bilge < 1, "a patched section stops the leak");
    }
    // the wet deck: past 12 deg an unbraced hand slides, past 25 they fall, braced they hold
    {
        Gannet g; g.Init(2, 4, Weather::Glass);
        g.crew[0].p = {-1, 0}; g.crew[1].p = {-2, 0};
        for (int i = 0; i < 60 * 3; i++) {
            g.boat.roll = 18 / 57.3f; g.boat.rollVel = 0;
            g.Move(0, {0, 0}, false, 1 / 60.0f); g.Move(1, {0, 0}, true, 1 / 60.0f);
            g.sea.t += 1 / 60.0f;
        }
        check(g.crew[0].p.y > 1.5f && fabsf(g.crew[1].p.y) < 0.2f, TextFormat("at 18 deg the unbraced hand slides to starboard (y %.1f), the braced one holds (y %.1f)", g.crew[0].p.y, g.crew[1].p.y));
        g.boat.roll = 28 / 57.3f;
        g.Move(0, {1, 0}, false, 1 / 60.0f);
        check(g.crew[0].fallen, "at 28 deg they fall");
        g.crew[0].fallen = false; g.crew[0].p = {-1, 2.4f};
        for (int i = 0; i < 60 && !g.crew[0].overboard; i++) { g.boat.roll = 40 / 57.3f; g.Move(0, {0, 1}, false, 1 / 60.0f); }
        check(g.crew[0].overboard, "on her beam ends, an unbraced hand at the rail goes over it");
    }
    // stations: the ladder, the helm, one hand each
    {
        Gannet g; g.Init(2, 4, Weather::Calm);
        g.crew[0].p = LADDER;
        check(g.TakeStation(0) && g.crew[0].deck == 1, "the ladder takes a hand down to the engine room");
        g.crew[0].p = {-4.8f, 0.3f};
        check(g.TakeStation(0) && Stations()[g.crew[0].station].kind == StationKind::Boiler, "the boiler is manned below");
        float f0 = g.boat.firebox;
        for (int i = 0; i < 60; i++) g.Primary(0, true, 1 / 60.0f);
        check(g.boat.firebox > f0 + 1, "shovelling feeds the firebox");
        g.crew[1].p = {4.0f, 0.1f};
        check(g.TakeStation(1) && Stations()[g.crew[1].station].kind == StationKind::Helm, "the helm is in the wheelhouse");
        g.Scroll(1, 1); g.Scroll(1, 1);
        check(g.boat.telegraph == 2, "the telegraph rings half ahead");
        g.crew[0].deck = 0; g.crew[0].station = -1; g.crew[0].p = {4.1f, 0};
        check(!g.TakeStation(0), "one hand per station");
    }
    fails += RunTrawlRodTest();
    printf(fails ? "trawl-boat-test: %d check(s) failed\n" : "trawl-boat-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

} // namespace tw
