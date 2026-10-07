// The Deep is a Unity game (TheDeep/, built to games/TheDeep/TheDeep.exe). The arcade starts it as its own program.
#pragma once
#include <string>

// role: "solo", "host" or "join"; addr: the host's address for a guest. False (and *err) if it couldn't start.
bool LaunchDeep(const std::string& role, const std::string& addr, const std::string& name, std::string* err);
bool DeepRunning();   // is the program we started still open?
