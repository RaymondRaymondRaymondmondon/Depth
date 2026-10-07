// The Deep is a Unity game (TheDeep/, built to games/TheDeep/TheDeep.exe). The arcade starts it as its own program.
#pragma once
#include <string>

// role: "solo", "host" or "join"; addr: the host's address for a guest; seat 0-3 (the crew's places: the raft's seats,
// the suit colours); port: The Deep's own UDP port (Depth's arcade session keeps 47778). False (and *err) if it
// couldn't start.
constexpr int DEEP_PORT = 47779;
bool LaunchDeep(const std::string& role, const std::string& addr, const std::string& name, std::string* err, int seat = 0, int port = DEEP_PORT);
bool DeepRunning();   // is the program we started still open?
