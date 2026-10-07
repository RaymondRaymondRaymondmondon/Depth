// Starting The Deep (the Unity game) from the arcade. Kept apart from everything raylib: windows.h's names clash with
// raylib's (Rectangle, CloseWindow...), so this file includes no other Depth header but its own.
#include "deep_launch.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>

static HANDLE gDeep = nullptr;

static bool Exists(const std::string& p) { DWORD a = GetFileAttributesA(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }

static std::string FindExe()
{
    char buf[MAX_PATH]; GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string dir(buf); dir = dir.substr(0, dir.find_last_of("\\/"));
    char cwd[MAX_PATH]; GetCurrentDirectoryA(MAX_PATH, cwd);
    std::vector<std::string> tries = {
        std::string(cwd) + "\\games\\TheDeep\\TheDeep.exe",
        dir + "\\games\\TheDeep\\TheDeep.exe",
        dir + "\\..\\..\\games\\TheDeep\\TheDeep.exe",   // (build\Release\depth.exe in the repo)
        dir + "\\..\\games\\TheDeep\\TheDeep.exe",
    };
    for (auto& t : tries) if (Exists(t)) return t;
    return "";
}

static std::string Quote(const std::string& s) { std::string o = "\""; for (char c : s) { if (c == '"') o += '\\'; o += c; } return o + "\""; }

bool LaunchDeep(const std::string& role, const std::string& addr, const std::string& name, std::string* err, int seat, int port)
{
    if (DeepRunning()) { if (err) *err = "The Deep is already running"; return false; }
    std::string exe = FindExe();
    if (exe.empty()) { if (err) *err = "The Deep isn't built yet (tools\\deep.ps1 build makes games\\TheDeep\\TheDeep.exe)"; return false; }
    std::string cmd = Quote(exe) + " -role " + role + " -name " + Quote(name.empty() ? "Diver" : name);
    if (!addr.empty()) cmd += " -addr " + Quote(addr);
    cmd += " -seat " + std::to_string(seat) + " -port " + std::to_string(port);
    std::string wdir = exe.substr(0, exe.find_last_of("\\/"));
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> line(cmd.begin(), cmd.end()); line.push_back(0);
    if (!CreateProcessA(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, wdir.c_str(), &si, &pi)) {
        if (err) *err = "Couldn't start The Deep (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    gDeep = pi.hProcess;
    return true;
}

bool DeepRunning()
{
    if (!gDeep) return false;
    if (WaitForSingleObject(gDeep, 0) == WAIT_TIMEOUT) return true;
    CloseHandle(gDeep); gDeep = nullptr;
    return false;
}
