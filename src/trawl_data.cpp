// The Trawl's numbers (design doc: "Numbers live in trawl_data.cpp"). Species records live in
// data/trawl/trawl_species.json; everything else a designer tunes is here.
#include "trawl.h"

namespace tw {

const TrawlData& D() { static TrawlData d; return d; }

const char* WeatherName(Weather w) {
    static const char* N[(int)Weather::COUNT] = {"Calm", "Fog", "Rain", "Squall", "Storm", "The Glass"};
    return N[(int)w];
}
float WeatherSwell(Weather w) {   // "Weather": Calm 0.2 m, Fog 0.3, Rain 0.6, Squall 1.5, Storm 3, the Glass mirror-flat
    static const float S[(int)Weather::COUNT] = {0.2f, 0.3f, 0.6f, 1.5f, 3.0f, 0.0f};
    return S[(int)w];
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
    };
    return S;
}
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
