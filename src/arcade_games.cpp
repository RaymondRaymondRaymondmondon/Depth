// The Deep Arcade's games as seen by the network session (arcade_game.h): the registry, Scuttle's host, and Drift,
// a tiny real-time game that exists only so --net-loop can prove the snapshot path the Trawl and Fathoms will use.
#include "arcade_game.h"
#include "arcade_session.h"
#include "scuttle.h"
#include <algorithm>
#include <cmath>

namespace tw {   // the Trawl's host (trawl_net.cpp; declared here so this file stays free of raylib)
std::unique_ptr<arcade::GameHost> MakeTrawlHost();
uint32_t TrawlDataHash();
}
namespace rt {   // Red Tide's host (redtide_net.cpp)
std::unique_ptr<arcade::GameHost> MakeRedTideHost();
uint32_t RedTideDataHash();
}

namespace arcade {

const GameInfo& Info(int g) {
    static const GameInfo INFO[G_COUNT] = {
        {"Flats Duel", 2, 2, false, 0, false},
        {"The Trawl", 1, 6, true, 20, true},
        {"Scuttle", 2, scuttle::MAX_SEATS, false, 0, true},
        {"Fathoms", 2, 6, true, 20, false},
        {"Red Tide", 1, 4, true, 20, true},
        {"The Flight", 2, 6, true, 20, false},
    };
    static const GameInfo DRIFT = {"Drift (test)", 2, 6, true, 20, true};
    static const GameInfo NONE = {"?", 2, 2, false, 0, false};
    if (g == G_TEST_DRIFT) return DRIFT;
    return g >= 0 && g < G_COUNT ? INFO[g] : NONE;
}

namespace {
// ---- Scuttle: turn-based, hidden hands; the host runs the rules engine, the heat pause, the timer and the AI crabs
class ScuttleHost : public GameHost {
public:
    scuttle::State s;
    float aiWait = 0, roundWait = 0;
    uint32_t rng = 1;
    static constexpr float AI_THINK = 0.9f, ROUND_PAUSE = 4.0f;

    void Start(int players, uint32_t seed) override { scuttle::NewMatch(s, players, seed | 1); rng = seed ^ 0x51ED270Bu; if (!rng) rng = 1; aiWait = roundWait = 0; }
    bool Act(int player, Reader& r) override {
        scuttle::Action a;
        a.kind = (scuttle::Action::Kind)r.U8(); a.handIdx = (uint8_t)r.U8(); a.target = (int8_t)r.U8(); a.shell = r.U8() != 0;
        if (r.bad || a.kind >= scuttle::Action::NEXTROUND) return false;   // (only the host moves to the next heat)
        a.seat = (uint8_t)player;
        if (!scuttle::Apply(s, a)) return false;
        aiWait = 0;
        return true;
    }
    bool Tick(float dt, uint32_t ai) override {
        if (s.phase == scuttle::PH_MATCH_OVER) return false;
        if (s.phase == scuttle::PH_ROUND_OVER) {
            if ((roundWait += dt) < ROUND_PAUSE) return false;
            roundWait = 0;
            scuttle::Action a; a.kind = scuttle::Action::NEXTROUND;
            return scuttle::Apply(s, a);
        }
        int actor = scuttle::Actor(s);
        if (actor >= 0 && ((ai >> actor) & 1)) {
            if ((aiWait += dt) < AI_THINK) return false;
            aiWait = 0;
            scuttle::Action a = scuttle::Bot(s, actor, rng);
            if (!scuttle::Apply(s, a)) { a.kind = s.phase == scuttle::PH_RESPONSE ? scuttle::Action::RESPOND : scuttle::Action::SKIPBET; a.shell = false; scuttle::Apply(s, a); }
            return true;
        }
        std::vector<scuttle::Action> timeouts;
        scuttle::Tick(s, dt, timeouts);
        bool any = false;
        for (auto& a : timeouts) any |= scuttle::Apply(s, a);
        return any;
    }
    void Snapshot(int viewer, Writer& w) const override { scuttle::Serialize(s, viewer, w); }
    bool Over() const override { return s.phase == scuttle::PH_MATCH_OVER; }
};

// ---- Drift: every player steers a dot; AI dots wander. Real time, 20 snapshots a second, over after 20 s.
class DriftHost : public GameHost {
public:
    struct Dot { float x = 0, y = 0, vx = 0, vy = 0; };
    std::vector<Dot> dots;
    uint32_t tick = 0, rng = 1;
    float t = 0, aiTurn = 0;
    void Start(int players, uint32_t seed) override {
        dots.assign(players, {});
        for (int i = 0; i < players; i++) dots[i].x = 100.0f * i;
        rng = seed | 1; tick = 0; t = 0;
    }
    bool Act(int player, Reader& r) override {
        int dir = (int)r.U8();
        if (r.bad || player < 0 || player >= (int)dots.size() || dir > 8) return false;
        static const int DX[9] = {0, 1, 1, 0, -1, -1, -1, 0, 1}, DY[9] = {0, 0, 1, 1, 1, 0, -1, -1, -1};
        dots[player].vx = DX[dir] * 60.0f; dots[player].vy = DY[dir] * 60.0f;
        return true;
    }
    bool Tick(float dt, uint32_t ai) override {
        t += dt; tick++;
        if ((aiTurn += dt) > 0.5f) {
            aiTurn = 0;
            for (int i = 0; i < (int)dots.size(); i++) if ((ai >> i) & 1) {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                Writer w; w.U8(rng % 9); Reader r(w.b); Act(i, r);
            }
        }
        for (Dot& d : dots) { d.x += d.vx * dt; d.y += d.vy * dt; }
        return true;
    }
    void Snapshot(int, Writer& w) const override {
        w.U32(tick); w.F32(t); w.U8((uint8_t)dots.size());
        for (const Dot& d : dots) { w.F32(d.x); w.F32(d.y); }
    }
    bool Over() const override { return t >= 20; }
};
}

// every rule a peer must share: each game adds its data here (a mismatch is refused at the handshake)
uint32_t DataHash() {
    Writer w;
    for (int g = 0; g < G_COUNT; g++) { const GameInfo& gi = Info(g); w.Str(gi.name); w.U8(gi.minPlayers); w.U8(gi.maxPlayers); w.U8(gi.built); }
    for (int c = 0; c < scuttle::C_COUNT; c++) { w.Str(scuttle::Card(c).name); w.U8(scuttle::Card(c).copies); }
    w.U8(scuttle::TRACK); w.U8(scuttle::HAND); w.U8(scuttle::BETS_PER_ROUND); w.F32(scuttle::TURN_SECONDS); w.F32(scuttle::RESPONSE_SECONDS);
    w.U32(tw::TrawlDataHash());
    w.U32(rt::RedTideDataHash());
    return Fnv1a(w.b.data(), w.b.size());
}

std::unique_ptr<GameHost> MakeGameHost(int g) {
    switch (g) {
        case G_SCUTTLE: return std::make_unique<ScuttleHost>();
        case G_TRAWL: return tw::MakeTrawlHost();
        case G_RED_TIDE: return rt::MakeRedTideHost();
        case G_TEST_DRIFT: return std::make_unique<DriftHost>();
        default: return nullptr;   // Flats Duel, the Trawl, Fathoms and the fifth game come aboard in stages 12-14 and later
    }
}
}
