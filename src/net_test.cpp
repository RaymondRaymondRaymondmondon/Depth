// Headless checks for the Deep Arcade: --scuttle-sim (the rules, bots only) and --net-loop (a host and two guests in
// one process over the real transport, or the in-memory one when this build has no network library).
#include "arcade_session.h"
#include "net.h"
#include "scuttle.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

int RunScuttleSim(int matches) {
    int fails = 0;
    for (int n = 2; n <= scuttle::MAX_SEATS; n++) {
        scuttle::SimResult r = scuttle::Simulate(matches, n, 1234 + n);
        printf("Scuttle, %d crabs, %d matches: %.1f turns and %.2f rounds a match; wins by seat:", n, r.matches, r.avgTurns, r.avgRounds);
        int lo = 1 << 30, hi = 0, done = 0;
        for (int i = 0; i < n; i++) { printf(" %.0f%%", 100.0 * r.seatWins[i] / r.matches); lo = std::min(lo, r.seatWins[i]); hi = std::max(hi, r.seatWins[i]); done += r.seatWins[i]; }
        printf("\n");
        if (done < r.matches) { printf("  FAIL: %d matches never finished\n", r.matches - done); fails++; }
        // seat fairness: no seat more than 30% above or below an even share (the design doc's bar)
        double even = (double)r.matches / n;
        if (hi > even * 1.3 || lo < even * 0.7) { printf("  FAIL: seat advantage beyond 30%% of an even share\n"); fails++; }
        if (r.avgTurns > 120) { printf("  FAIL: matches run too long\n"); fails++; }
    }
    printf(fails ? "Scuttle sim: %d problem(s)\n" : "Scuttle sim: OK\n", fails);
    return fails ? 1 : 0;
}

// the net loop: host + two guests; the guests play by the bot; one guest drops mid-match and rejoins with its token;
// a third guest with a different build is turned away; nobody ever sees another seat's hand.
int RunNetLoop(int lagMs, bool forceMemory) {
    using namespace arcade;
    std::string err;
    bool real = !forceMemory && net::Init(&err);
    auto make = [&]() { return real ? net::MakeTransport() : net::MakeMemoryTransport(); };
    if (real && lagMs > 0) net::SetFakeLag(lagMs, 1.0f);
    uint16_t port = 47790;
    std::string addr = real ? "127.0.0.1:" + std::to_string(port) : "mem:" + std::to_string(port);
    printf("net-loop over %s%s\n", real ? "GameNetworkingSockets (loopback UDP)" : "the in-memory transport", lagMs && real ? (" with " + std::to_string(lagMs) + " ms lag and 1% loss").c_str() : "");

    Session host, a, b;
    Profile ph{"Captain", 1}, pa{"Nurse", 2}, pb{"Diver", 3};
    if (!host.Host(ph, G_SCUTTLE, &err, port, make(), false)) { printf("FAIL: host: %s\n", err.c_str()); return 1; }
    double t = 0; const float dt = 1.0f / 60;
    auto step = [&](int frames) { for (int i = 0; i < frames; i++) { t += dt; host.Update(t, dt); a.Update(t, dt); b.Update(t, dt); } };
    if (!a.Join(pa, addr, &err, 0, make())) { printf("FAIL: join a: %s\n", err.c_str()); return 1; }
    if (!b.Join(pb, addr, &err, 0, make())) { printf("FAIL: join b: %s\n", err.c_str()); return 1; }
    step(120);
    int fails = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { printf("FAIL: %s\n", what); fails++; } else printf("  ok: %s\n", what); };
    check(a.stage == S_LOBBY && b.stage == S_LOBBY, "both guests reach the lobby");
    int seated = 0; for (auto& s : host.seats) seated += s.used;
    check(seated == 3, "the host sees three seats");
    check(b.seats[1].used && b.seats[2].used && b.seats[1].name == "Nurse", "guests see the same lobby");

    // a guest from a different build is turned away with a reason
    {
        Session c; Profile pc{"Stowaway", 4};
        std::string e2;
        // (pretend: send a HELLO with a wrong build by hand through a raw transport)
        auto raw = make();
        int conn = raw->Connect(addr, &e2);
        bool rejected = false;
        for (int i = 0; i < 180 && conn >= 0; i++) {
            t += dt; host.Update(t, dt); a.Update(t, dt); b.Update(t, dt);
            std::vector<net::Event> ev; raw->Poll(ev);
            for (auto& e : ev) {
                if (e.kind == net::Event::Connected) {
                    Writer w; w.U8(1); w.U8(PROTOCOL); w.U32(BuildId() ^ 1); w.U32(DataHash()); w.Str("Stowaway"); w.U64(4); w.U32(0);
                    raw->Send(conn, net::CH_CONTROL, w.b.data(), (int)w.b.size());
                } else if (e.kind == net::Event::Message && !e.data.empty() && e.data[0] == 3) rejected = true;
            }
            if (rejected) break;
        }
        raw->CloseAll();
        check(rejected, "a different build is rejected");
        (void)c; (void)pc;
    }

    a.Chat("ahoy");
    a.SetReady(true); b.SetReady(true);
    step(30);
    check(host.chat.size() && b.chat.back() == "Nurse: ahoy", "chat reaches everyone");
    std::string why;
    check(host.Launch(&why), ("the host launches" + (why.empty() ? "" : " (" + why + ")")).c_str());
    step(30);
    check(a.stage == S_PLAYING && b.stage == S_PLAYING && a.view.nSeats == 3, "guests get the match");

    // play: everyone's own bot decides for them, sent as ordinary actions
    uint32_t rng = 99;
    bool dropped = false;
    int guard = 0;
    for (; guard < 60 * 60 * 30 && host.Authoritative().phase != scuttle::PH_MATCH_OVER; guard++) {
        const scuttle::State& T = host.Authoritative();
        int actor = scuttle::Actor(T);
        for (Session* s : {&host, &a, &b}) {
            if (s->stage != S_PLAYING || actor < 0 || s->MyCrab() != actor) continue;
            if (scuttle::Actor(s->view) != actor || s->view.turnsPlayed != T.turnsPlayed || s->view.phase != T.phase) continue;   // (wait for the latest state)
            if (guard % 20 == 0) s->Act(scuttle::Bot(s->view, actor, rng));
        }
        // a few turns in, guest b goes silent (a pulled cable: no bye, it just stops answering); the host notices after
        // LOST_AFTER, pauses, and b comes back with its token
        if (!dropped && T.turnsPlayed >= 6) {
            dropped = true;
            uint32_t token = b.rejoinToken;
            for (int i = 0; i < 60 * 8; i++) { t += dt; host.Update(t, dt); a.Update(t, dt); }
            check(host.paused && a.paused, "the table pauses when a guest goes silent");
            int turnsBefore = host.Authoritative().turnsPlayed;
            for (int i = 0; i < 60 * 2; i++) { t += dt; host.Update(t, dt); a.Update(t, dt); }
            check(host.Authoritative().turnsPlayed == turnsBefore, "nothing moves while paused");
            bool ok = b.Join(pb, addr, &err, token, make());
            step(180);
            check(ok && b.stage == S_PLAYING && !host.paused, "the guest rejoins its own seat with its token");
            check(b.view.nSeats == 3 && b.MyCrab() >= 0 && !b.view.seats[b.MyCrab()].hand.empty(), "the rejoined guest has its hand back");
        }
        step(1);
    }
    check(host.Authoritative().phase == scuttle::PH_MATCH_OVER, "the match finishes");
    step(60);
    check(a.view.matchWinner == host.Authoritative().matchWinner && b.view.matchWinner == host.Authoritative().matchWinner, "everyone agrees who won");
    check(a.leaks == 0 && b.leaks == 0, "no guest ever saw another seat's hand or face-down bets");
    printf("  (%d turns, %d rounds, winner crab %d, %.0f s simulated)\n", host.Authoritative().turnsPlayed, host.Authoritative().round, host.Authoritative().matchWinner, t);

    // the host leaves: guests are told
    host.Leave();
    step(60);
    check(a.stage == S_ENDED && b.stage == S_ENDED, "guests are told when the host closes the table");
    printf(fails ? "net-loop: %d FAILED\n" : "net-loop: all checks passed\n", fails);
    if (real) net::Shutdown();
    return fails ? 1 : 0;
}
