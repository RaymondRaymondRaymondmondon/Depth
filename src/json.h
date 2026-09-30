#pragma once
// A small JSON reader for the game's data files (data/redtide, data/trawl). Parses the whole document into a tree
// of Json values; lookups on a missing key or a wrong type return a shared null value, so data code can read
// optional fields without checking each step.
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o; // keeps file order

    bool IsNull() const { return type == Null; }
    bool IsNum() const { return type == Num; }
    bool IsStr() const { return type == Str; }
    bool IsArr() const { return type == Arr; }
    bool IsObj() const { return type == Obj; }
    const Json& operator[](const std::string& key) const;
    const Json& operator[](size_t i) const;
    bool Has(const std::string& key) const;
    size_t Size() const { return type == Arr ? a.size() : type == Obj ? o.size() : 0; }
    // Numbers read from either a JSON number or a numeric string ("12"); strings from a string or a number.
    double Num0(double def = 0) const;
    float F(double def = 0) const { return (float)Num0(def); }
    int I(int def = 0) const { return (int)Num0(def); }
    std::string Str0(const std::string& def = "") const;
    bool Bool0(bool def = false) const;

    static const Json& NullValue();
};

// Parses text; on a syntax error returns a Null and fills err (line:col message).
Json ParseJson(const std::string& text, std::string* err = nullptr);
// Reads and parses a file; returns Null (and logs a warning) if it can't be read or parsed.
Json LoadJsonFile(const std::string& path);
