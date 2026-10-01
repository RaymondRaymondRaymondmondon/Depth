// The Trawl's numbers (design doc: "Numbers live in trawl_data.cpp"). Species records live in
// data/trawl/trawl_species.json; everything else a designer tunes is here.
#include "trawl.h"
#include <algorithm>

namespace tw {

const TrawlData& D() { static TrawlData d; return d; }

const char* CatchSourceName(int s) { static const char* N[CS_COUNT] = {"hook", "net", "gun", "set gear", "dive"}; return N[s < 0 || s >= CS_COUNT ? 0 : s]; }

const char* WeatherName(Weather w) {
    static const char* N[(int)Weather::COUNT] = {"Calm", "Fog", "Rain", "Squall", "Storm", "The Glass"};
    return N[(int)w];
}
float WeatherSwell(Weather w) {   // "Weather": Calm 0.2 m, Fog 0.3, Rain 0.6, Squall 1.5, Storm 3, the Glass mirror-flat
    static const float S[(int)Weather::COUNT] = {0.2f, 0.3f, 0.6f, 1.5f, 3.0f, 0.0f};
    return S[(int)w];
}
float WeatherRoll(Weather w) {    // the most the sea alone rolls her (degrees): Squall "up to 20", Storm "up to 30"; the gentler ones scaled by the swell
    static const float R[(int)Weather::COUNT] = {5, 7, 12, 20, 30, 0};
    return R[(int)w];
}
const char* SectionName(int s) {
    static const char* N[SEC_COUNT] = {"bow, port", "bow, starboard", "midships, port", "midships, starboard", "stern, port", "stern, starboard"};
    return N[s < 0 || s >= SEC_COUNT ? 0 : s];
}
const char* RoleName(Role r) { static const char* N[(int)Role::COUNT] = {"Bosun", "Angler", "Diver", "Medic"}; return N[(int)r]; }

// The Gannet's stations (design doc, "The Gannet's stations"), laid out on a 22 x 6 m deck: bow at +x, starboard at
// +y. The wheelhouse stands just forward of midships; the engine room is below, reached by the ladder aft of it.
const std::vector<StationDef>& Stations() {
    static const std::vector<StationDef> S = {
        {StationKind::Helm, "Helm", {4.2f, 0}, 0, "A/D steer, W/S or scroll the engine telegraph"},
        {StationKind::Boiler, "Boiler", {-4.8f, 0.2f}, 1, "Left mouse shovels coal, right mouse bleeds pressure"},
        {StationKind::Pumps, "Bilge pumps", {-7.0f, 1.4f}, 1, "Left mouse works the pump"},
        {StationKind::PortRod, "Port rod", {-0.8f, -2.55f}, 0, "Cast, set, fight, land"},
        {StationKind::StarRod, "Starboard rod", {-0.8f, 2.55f}, 0, "Cast, set, fight, land"},
        {StationKind::SternRodP, "Stern rod (port)", {-10.2f, -1.7f}, 0, "Trolling rod"},
        {StationKind::SternRodS, "Stern rod (starboard)", {-10.2f, 1.7f}, 0, "Trolling rod"},
        {StationKind::NetWinch, "Net winch", {-8.4f, 0}, 0, "Shoot and haul the trawl"},
        {StationKind::Lantern, "Lantern mast", {0.2f, 0}, 0, "Scroll raises and lowers the lamp"},
        {StationKind::Sonar, "Sonar and radio", {2.4f, -1.2f}, 0, "Ping and mark"},
        {StationKind::Harpoon, "Harpoon cannon", {9.6f, 0}, 0, "Aim, fire, reload"},
        {StationKind::Gutting, "Gutting table", {-2.2f, 1.5f}, 0, "Gut, grade and ice"},
        {StationKind::AirPump, "Air pump", {-6.2f, 2.3f}, 0, "Keeps a diver breathing"},
        {StationKind::Bell, "Ship's bell", {6.0f, 1.9f}, 0, "Ring it"},
        {StationKind::Printer, "Telegraph printer", {2.4f, 1.2f}, 0, "Tear off and read the Owners' tape"},
        {StationKind::Locker, "Deck locker", {-6.4f, -2.3f}, 0, "Stow and take gear: 1-4 picks a slot"},
    };
    return S;
}
float LanternRadius(int level) { static const float R[4] = {4, 8, 14, 30}; return R[std::clamp(level, 0, 3)]; }
int NearestStation(Vector2 at, int deck, float r) {
    int best = -1; float bd = r * r;
    const auto& S = Stations();
    for (int i = 0; i < (int)S.size(); i++) {
        if (S[i].deck != deck) continue;
        float dx = S[i].at.x - at.x, dy = S[i].at.y - at.y, d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

} // namespace tw

// ---------------------------------------------------------------- fishing (design doc, "Tackle", "Fight tuning")
namespace tw {
const TackleDef& TackleOf(Tackle t) {
    //                                   rating  cast  reel  low   spool  rodSoft price  drop
    static const TackleDef T[(int)Tackle::COUNT] = {
        {"Handline",               8,     6,   0.8f, 0.8f,  60,  0.05f,   0, false},
        {"Light rod",              6,    30,   1.5f, 1.5f, 200,  0.25f,  40, false},
        {"Medium rod",            15,    25,   1.0f, 1.0f, 250,  0.12f, 120, false},
        {"Heavy boat rod",        40,    15,   0.8f, 0.45f, 350, 0.05f, 300, false},
        {"Deep-drop reel",        30,     0,   1.2f, 1.2f, 450,  0.06f, 450, true},
        {"Big-game chair",       120,    15,   0.7f, 0.4f, 600,  0.02f, 900, false},
    };
    return T[(int)t];
}
const LineDef& LineOf(LineType l) {
    static const LineDef L[(int)LineType::COUNT] = {
        {"Mono", 0.18f, 0.2f, false, 1.0f},    // stretchy, forgiving, visible to wary fish
        {"Braid", 0.03f, 0.5f, false, 0.8f},   // no stretch, strong, cut by teeth and coral
        {"Wire leader", 0.02f, 1.0f, true, 1.6f},   // bite-proof, spooks wary fish
        {"Glow line", 0.15f, 0.2f, false, 1.3f},    // seen by everything
    };
    return L[(int)l];
}
const char* HookName(Hook h) { static const char* N[(int)Hook::COUNT] = {"Small", "Circle", "Big-game treble"}; return N[(int)h]; }
const char* PatternName(Pattern p) { static const char* N[(int)Pattern::COUNT] = {"None", "Run", "Dive", "Jump", "Circle", "Cover", "Roll"}; return N[(int)p]; }
float PatternPull(Pattern p) { static const float R[(int)Pattern::COUNT] = {0.3f, 0.8f, 0.9f, 0.6f, 0.5f, 0.7f, 0.6f}; return R[(int)p]; }
const char* FightEndName(FightEnd e) {
    static const char* N[] = {"on", "landed", "line snapped", "threw the hook", "pulled the hook", "slack: hook fell out", "spooled", "spooked", "taken"};
    return N[(int)e];
}
const SkillDef& SkillOf(Skill s) {   // design doc, "Bot skill": reaction, hook-set, gaff; bow and keel are the fight's
    static const SkillDef S[(int)Skill::COUNT] = {
        {"Green", 0.70f, 0.50f, 0.60f, 0.80f, 0.55f},
        {"Able", 0.40f, 0.70f, 0.80f, 0.92f, 0.75f},
        {"Old Hand", 0.25f, 0.85f, 0.90f, 0.97f, 0.90f},
    };
    return S[(int)s];
}
// Stage 2's stand-in fish (stage 3 reads the real species records): the design doc's five target fights and a few
// more to show each pattern.
const std::vector<FishSpec>& DummyFish() {
    static const std::vector<FishSpec> F = {
        //  name            kg    a               b               wary   teeth  depth floor  price
        {"snapper",          3, Pattern::Run,    Pattern::None,   false, false,  10,   30,  3},
        {"lingcod",         15, Pattern::Dive,   Pattern::Cover,  false, true,   30,   40,  4, 1.0f, 1.0f, 0.7f},
        {"yellowfin",       40, Pattern::Run,    Pattern::Circle, true,  false,  20,  600,  5, 1.0f, 1.6f, 1.8f},   // tireless, soft-mouthed
        {"sturgeon",       150, Pattern::Dive,   Pattern::None,   true,  false, 140,  150,  8, 0.3f, 1.5f},   // slow, very long
        {"marlin",         250, Pattern::Jump,   Pattern::Run,    true,  true,    3, 2000,  6, 1.0f, 1.25f, 0.4f},   // a bony bill holds a hook
        {"mahi",            10, Pattern::Jump,   Pattern::None,   false, false,   2,  400,  4},
        {"leopard shark",   12, Pattern::Roll,   Pattern::None,   false, true,   18,   20,  1},
        {"opah",            40, Pattern::Circle, Pattern::None,   false, false,  60,  800,  6},
        {"silverside",     0.1f, Pattern::None,  Pattern::None,   false, false,   2,   20, 0.8f},
    };
    return F;
}
const FishSpec* FindDummyFish(const std::string& name) {
    for (const auto& f : DummyFish()) if (name == f.name) return &f;
    return nullptr;
}
} // namespace tw
