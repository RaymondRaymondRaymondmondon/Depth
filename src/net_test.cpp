// Headless checks for the Deep Arcade: --scuttle-sim (the rules, bots only) and --net-loop (a host and two guests in
// one process over the real transport, or the in-memory one when this build has no network library).
#include "arcade_session.h"
#include "net.h"
#include "scuttle.h"
#include <algorithm>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <functional>
#include <thread>
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
    setvbuf(stdout, nullptr, _IONBF, 0);
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
    // over the real transport the frames are paced in real time (GNS runs on the clock); in memory they fly
    auto pace = [&]() { if (real) std::this_thread::sleep_for(std::chrono::milliseconds(16)); };
    auto step = [&](int frames) { for (int i = 0; i < frames; i++) { t += dt; host.Update(t, dt); a.Update(t, dt); b.Update(t, dt); pace(); } };
    auto until = [&](std::function<bool()> ok, float seconds) { for (int i = 0; i < seconds * 60 && !ok(); i++) step(1); step(10); };
    if (!a.Join(pa, addr, &err, 0, make())) { printf("FAIL: join a: %s\n", err.c_str()); return 1; }
    if (!b.Join(pb, addr, &err, 0, make())) { printf("FAIL: join b: %s\n", err.c_str()); return 1; }
    until([&] { return a.stage == S_LOBBY && b.stage == S_LOBBY; }, 10);
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
            t += dt; host.Update(t, dt); a.Update(t, dt); b.Update(t, dt); pace();
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

    // the LAN beacon: a browser on this machine hears an announced table by its code
    {
        net::LanBrowser br; net::LanBeacon bc;
        bool started = br.Start() && bc.Start();
        net::LanGame lg; lg.game = "Scuttle"; lg.name = "Captain"; lg.code = host.code; lg.players = 3; lg.maxPlayers = 4; lg.build = BuildId();
        bool heard = false;
        for (int i = 0; i < 30 && started && !heard; i++) {
            bc.Announce(lg);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            br.Poll(i * 0.02);
            for (auto& g : br.games) heard |= g.code == host.code && g.name == "Captain" && g.build == BuildId();
        }
        check(heard, "a LAN browser hears the table's beacon");
    }
    // everyone decodes their own snapshot, exactly as the table screen does; a guest's view must never carry another
    // seat's hand or face-down bets
    struct View { scuttle::State s; int ver = -1, leaks = 0; };
    View va, vb, vh;
    auto refresh = [](Session& ss, View& v) {
        if (v.ver == ss.stateVersion || ss.Snapshot().empty()) return;
        v.ver = ss.stateVersion;
        Reader r(ss.Snapshot());
        if (!scuttle::Deserialize(v.s, r)) return;
        int me = ss.MyPlayer();
        for (int i = 0; i < v.s.nSeats; i++) if (i != me && !v.s.seats[i].hand.empty()) v.leaks++;
        if (std::any_of(v.s.deck.begin(), v.s.deck.end(), [](uint8_t c) { return c != 0; })) v.leaks++;
        if (v.s.phase < scuttle::PH_ROUND_OVER) for (auto& bt : v.s.bets) if (bt.owner != me) v.leaks++;
    };
    auto truth = [&]() { scuttle::State s; Writer w; if (host.HostGame()) host.HostGame()->Snapshot(-1, w); Reader r(w.b); scuttle::Deserialize(s, r); return s; };
    auto act = [](Session& ss, const scuttle::Action& ac) { Writer w; scuttle::WriteAction(ac, w); ss.Act(w); };
    auto stepV = [&](int n) { for (int i = 0; i < n; i++) { step(1); refresh(host, vh); refresh(a, va); refresh(b, vb); } };

    a.Chat("ahoy");
    a.SetReady(true); b.SetReady(true);
    until([&] { return host.seats[1].ready && host.seats[2].ready && !b.chat.empty() && b.chat.back() == "Nurse: ahoy"; }, 5);
    check(host.chat.size() && b.chat.back() == "Nurse: ahoy", "chat reaches everyone");
    std::string why;
    check(host.Launch(&why), ("the host launches" + (why.empty() ? "" : " (" + why + ")")).c_str());
    until([&] { refresh(a, va); refresh(b, vb); return a.stage == S_PLAYING && b.stage == S_PLAYING && va.s.nSeats == 3 && vb.s.nSeats == 3; }, 5);
    check(a.stage == S_PLAYING && b.stage == S_PLAYING && va.s.nSeats == 3, "guests get the match");

    // play: everyone's own bot decides for them, sent as ordinary actions
    uint32_t rng = 99;
    bool dropped = false;
    scuttle::State T = truth();
    for (int guard = 0; guard < 60 * 60 * 30 && T.phase != scuttle::PH_MATCH_OVER; guard++) {
        int actor = scuttle::Actor(T);
        struct P { Session* s; View* v; } ps[3] = {{&host, &vh}, {&a, &va}, {&b, &vb}};
        for (P& p : ps) {
            if (p.s->stage != S_PLAYING || actor < 0 || p.s->MyPlayer() != actor) continue;
            const scuttle::State& V = p.v->s;
            if (scuttle::Actor(V) != actor || V.turnsPlayed != T.turnsPlayed || V.phase != T.phase) continue;   // (wait for the latest state)
            if (guard % 20 == 0) act(*p.s, scuttle::Bot(V, actor, rng));
        }
        // a few turns in, guest b goes silent (a pulled cable: no bye, it just stops answering); the host notices after
        // LOST_AFTER, pauses, and b comes back with its token
        if (!dropped && T.turnsPlayed >= 6) {
            dropped = true;
            uint32_t token = b.rejoinToken;
            for (int i = 0; i < 60 * 8; i++) { t += dt; host.Update(t, dt); a.Update(t, dt); pace(); }
            check(host.paused && a.paused, "the table pauses when a guest goes silent");
            int turnsBefore = truth().turnsPlayed;
            for (int i = 0; i < 60 * 2; i++) { t += dt; host.Update(t, dt); a.Update(t, dt); pace(); }
            check(truth().turnsPlayed == turnsBefore, "nothing moves while paused");
            bool ok = b.Join(pb, addr, &err, token, make());
            vb.ver = -1;
            until([&] { refresh(b, vb); return b.stage == S_PLAYING && !host.paused && vb.s.nSeats == 3; }, 10);
            check(ok && b.stage == S_PLAYING && !host.paused, "the guest rejoins its own seat with its token");
            check(vb.s.nSeats == 3 && b.MyPlayer() >= 0 && !vb.s.seats[b.MyPlayer()].hand.empty(), "the rejoined guest has its hand back");
        }
        stepV(1);
        T = truth();
    }
    check(T.phase == scuttle::PH_MATCH_OVER, "the match finishes");
    stepV(60);
    check(va.s.matchWinner == T.matchWinner && vb.s.matchWinner == T.matchWinner && vh.s.matchWinner == T.matchWinner, "everyone agrees who won");
    check(va.leaks == 0 && vb.leaks == 0 && vh.leaks == 0, "nobody ever saw another seat's hand or face-down bets (the host included)");
    printf("  (%d turns, %d rounds, winner crab %d, %.0f s simulated)\n", T.turnsPlayed, T.round, T.matchWinner, t);

    // back to the lobby and again: the same table can play another match
    host.BackToLobby();
    until([&] { return a.stage == S_LOBBY && b.stage == S_LOBBY; }, 5);
    check(a.stage == S_LOBBY && !host.seats[1].ready, "the host takes everyone back to the lobby");

    // the host leaves: guests are told
    host.Leave();
    until([&] { return a.stage == S_ENDED && b.stage == S_ENDED; }, 10);
    check(a.stage == S_ENDED && b.stage == S_ENDED, "guests are told when the host closes the table");

    // a real-time game through the same session: Drift, snapshotted 20 times a second on the unreliable channel
    {
        std::string addr2 = real ? "127.0.0.1:" + std::to_string(port + 1) : "mem:" + std::to_string(port + 1);
        if (!host.Host(ph, G_TEST_DRIFT, &err, port + 1, make(), false)) { printf("FAIL: drift host: %s\n", err.c_str()); fails++; }
        a.Join(pa, addr2, &err, 0, make());
        until([&] { return a.stage == S_LOBBY; }, 10);
        host.AddAI();
        a.SetReady(true);
        until([&] { return host.seats[1].ready; }, 5);
        check(host.Launch(&why), "a real-time game launches");
        int before = a.snapshotsReceived;
        uint32_t lastTick = 0; bool monotonic = true;
        float startX = -1, endX = -1;
        for (int f = 0; f < 60 * 3; f++) {
            if (f == 30) { Writer w; w.U8(1); a.Act(w); }   // steer right
            step(1);
            if (a.Snapshot().empty()) continue;
            Reader r(a.Snapshot());
            uint32_t tick = r.U32(); r.F32(); int n = r.U8();
            float x = 0; for (int i = 0; i < n; i++) { float px = r.F32(); r.F32(); if (i == a.MyPlayer()) x = px; }
            if (tick < lastTick) monotonic = false;
            lastTick = tick;
            if (f == 25) startX = x;
            endX = x;
        }
        int got = a.snapshotsReceived - before;
        printf("  (drift: %d snapshots in 3 s)\n", got);
        check(got >= 30 && got <= 70, "real-time snapshots arrive at about their rate");
        check(monotonic, "a guest never goes back to an older snapshot");
        check(endX > startX + 80, "a guest's input moves its own piece on the host");
        host.Leave();
        until([&] { return a.stage == S_ENDED; }, 10);
    }

    printf(fails ? "net-loop: %d FAILED\n" : "net-loop: all checks passed\n", fails);
    a.Leave(); b.Leave(); host.Leave();   // (every connection closed before the library goes)
    if (real) net::Shutdown();
    return fails ? 1 : 0;
}
// depth.exe --bet-test: the pre-match bets over the session (a host and two guests in memory): a guest backs a seat,
// everyone sees it, a stake is capped, a bet on an empty seat is refused, bets are cleared for the next match; and the
// payouts' arithmetic
int RunBetTest() {
    using namespace arcade;
    int fails = 0;
    auto check = [&](bool ok, const char* what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; };
    printf("Pre-match bets\n");
    Session host, a, b; std::string err;
    Profile ph{"Captain", 1}, pa{"Nurse", 2}, pb{"Diver", 3};
    bool up = host.Host(ph, G_RED_TIDE, &err, 47813, net::MakeMemoryTransport(), false) && a.Join(pa, "mem:47813", &err, 0, net::MakeMemoryTransport()) && b.Join(pb, "mem:47813", &err, 0, net::MakeMemoryTransport());
    double t = 0;
    auto step = [&](int n) { for (int i = 0; i < n; i++) { t += 1 / 60.0; host.Update(t, 1 / 60.0f); a.Update(t, 1 / 60.0f); b.Update(t, 1 / 60.0f); } };
    for (int i = 0; i < 300 && !(a.stage == S_LOBBY && b.stage == S_LOBBY); i++) step(1);
    step(10);
    a.PlaceBet(b.mySeat, 30); host.PlaceBet(a.mySeat, 500); step(10);
    check(up && host.seats[a.mySeat].betOn == b.mySeat && host.seats[a.mySeat].bet == 30 && b.seats[a.mySeat].betOn == b.mySeat && b.seats[a.mySeat].bet == 30,
          "a guest backs another seat for 30: the host and the other guest both see it");
    check(host.seats[host.mySeat].bet == BET_CAP && a.seats[host.mySeat].bet == BET_CAP && a.seats[host.mySeat].betOn == a.mySeat, "a stake over the cap is held to 50");
    b.PlaceBet(5, 20); step(10);
    check(host.seats[b.mySeat].bet == 0 && host.seats[b.mySeat].betOn == -1, "a bet on an empty seat is refused");
    a.PlaceBet(-1, 0); step(10);
    check(host.seats[a.mySeat].bet == 0 && b.seats[a.mySeat].bet == 0, "a bet can be taken back before the match");
    a.PlaceBet(host.mySeat, 20); step(10);
    for (auto& s : host.seats) if (s.used) s.ready = true;
    a.SetReady(true); b.SetReady(true); step(10);
    std::string why; host.gameOpts = "ship"; bool launched = host.Launch(&why); step(30);
    host.BackToLobby(); step(10);
    check(launched && host.seats[a.mySeat].bet == 0 && a.seats[a.mySeat].bet == 0, "the bets are cleared when the table goes back to the lobby");
    check(BetPayout(20, 3, 1, true) == 60 && BetPayout(20, 4, 2, true) == 40 && BetPayout(20, 4, 4, true) == 20 && BetPayout(20, 4, 1, false) == 0,
          "a right pick pays the stake times the players, shared among joint winners (a level match gives it back); a wrong one nothing");
    host.Leave(); a.Leave(); b.Leave();
    printf(fails ? "bet-test: %d check(s) failed\n" : "bet-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}