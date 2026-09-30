#pragma once
// A small JSON reader for the Study's course packs (no raylib). Parse errors carry a line number.
#include <string>
#include <utility>
#include <vector>

struct Json {
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
    bool b = false; double n = 0; std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;
    const Json& operator[](const char* key) const;          // a missing key gives a null
    const Json& operator[](size_t i) const;                  // out of range gives a null
    bool Has(const char* key) const;
    bool IsNull() const { return type == NUL; }
    size_t Size() const { return type == ARR ? a.size() : type == OBJ ? o.size() : 0; }
    std::string Str(const std::string& def = "") const { return type == STR ? s : type == NUM ? std::to_string(n) : def; }
    double Num(double def = 0) const { return type == NUM ? n : type == BOOL ? (b ? 1 : 0) : def; }
    int Int(int def = 0) const { return type == NUM ? (int)n : def; }
    bool Bool(bool def = false) const { return type == BOOL ? b : type == NUM ? n != 0 : def; }
};
bool ParseJson(const std::string& text, Json& out, std::string* err);
bool LoadJsonFile(const std::string& path, Json& out, std::string* err);
std::string JsonEscape(const std::string& s);
std::string WriteJson(const Json& j, int indent = 0);          // indent < 0: one line
Json JStr(const std::string& s);
Json JNum(double n);
