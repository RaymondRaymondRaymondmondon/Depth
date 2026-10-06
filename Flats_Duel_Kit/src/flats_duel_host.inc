// ---- Flats Duel: turn-based, hidden hands. The host runs the duel engine (flats_duel.*): it paces combat one lane per
// step, times turns, and plays AI seats (an AI seat, or a player the session has handed to the AI after 2 minutes).
// PASTE INTO: src/arcade_games.cpp, inside `namespace arcade { namespace { ... } }`, next to ScuttleHost.
// Needs:      #include "flats_duel.h" at the top of arcade_games.cpp.
class FlatsDuelHost : public GameHost {
public:
    flats::duel::State s;
    void Start(int players, uint32_t seed) override { (void)players; flats::duel::NewMatch(s, seed | 1); }
    bool Act(int player, Reader& r) override {
        flats::duel::Action a;
        if (player < 0 || player > 1 || !flats::duel::ReadAction(r, a)) return false;
        return flats::duel::Apply(s, player, a);    // Apply re-checks everything (Legal): a client's bytes are never trusted
    }
    bool Tick(float dt, uint32_t ai) override { return flats::duel::HostTick(s, dt, ai); }
    // viewer -1 is a spectator here (both hands hidden), never "everything": nothing the session sends may carry a hand
    void Snapshot(int viewer, Writer& w) const override { flats::duel::Serialize(s, viewer, w); }
    bool Over() const override { return flats::duel::Over(s); }
};
