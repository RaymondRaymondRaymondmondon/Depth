#pragma once
// Scuffle over the Deep Arcade's session (doc p. 19, "Netcode"): the host runs the real match at 120 Hz and snapshots
// the whole of it 30 times a second; every guest keeps a mirror match and runs its own stick ahead of the host
// (prediction). Each of a guest's steps carries a numbered input; the host plays them in order, one a step, and says
// in each snapshot which was the last it used (the ack). A guest that reads a snapshot starts again from it and replays
// its inputs the host hasn't used yet (rollback of the local stick, with everyone else carried on their last input),
// and the scene eases whatever moved under the eye (a correction offset that fades in a tenth of a second).
// All play, solo included, is an sf::Input. The snapshot is one templated Visit (write and read): any field the
// screen draws or the step reads must be in it; --scuffle-net-test checks the mirror writes back byte for byte.
#include "scuffle.h"
#include "arcade_game.h"
#include "net_msg.h"
#include <deque>
#include <memory>
#include <string>

namespace sf {
enum SfAct : uint8_t { SA_INPUTS = 1, SA_HELLO = 2 };
struct InputFrame { uint32_t seq = 0; Input in; };
Input QuantizeInput(const Input& in);                       // what the host will read (a guest predicts with the same)
void WriteInputs(const std::vector<InputFrame>& frames, Writer& w);
bool ReadInputs(Reader& r, std::vector<InputFrame>& out);   // (after the SA_INPUTS byte)
void OrderHello(Writer& w, const std::string& name);

void WriteMatch(Match& m, std::vector<std::string>& names, int viewer, uint32_t ack, Writer& out);
void PackMatch(Match& m, std::vector<std::string>& names, int viewer, uint32_t ack, Writer& out);   // compressed
bool ReadMatch(Reader& r, Match& m, std::vector<std::string>& names, int* viewerOut = nullptr, uint32_t* ackOut = nullptr);
void StepMirror(Match& m);                                  // a mirror's step: the world and the countdown (the host scores and starts rounds)

// the guest's side: the mirror, the inputs not yet used by the host, and the host's word on what happened
struct Predictor {
    Match m; bool have = false; int me = -1;
    std::deque<InputFrame> hist; uint32_t nextSeq = 1, ack = 0;
    std::vector<Event> hostEvents; uint32_t hostEventBase = 0;   // (what the scene splashes: only what the host saw)
    std::vector<std::string> names;
    float lastCorrection = 0; bool lastFree = false;      // (metres my stick moved when the last snapshot was applied; free: the host had it alive, on its feet, untouched for a second)
    uint32_t snaps = 0;
    void Local(const Input& in, std::vector<InputFrame>& outbox);   // one predicted step with my input
    bool Apply(Reader& r);                                 // a snapshot: start from it, replay what the host hasn't used
};

std::unique_ptr<arcade::GameHost> MakeScuffleHost();
Match* ScuffleHostMatch(arcade::GameHost* h);
const std::vector<std::string>* ScuffleHostNames(arcade::GameHost* h);
std::string ScuffleHostOpts(int toWin, int arsenal, int skill);
uint32_t ScuffleDataHash();
int RunScuffleNetTest();                                    // --scuffle-net-test
int RunScuffleNetLoop(int lagMs, bool mem);                 // --net-loop scuffle [lagMs] [mem]: the doc's gate, eight players
}
