#include "json.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

const Json& Json::NullValue() { static const Json n; return n; }

const Json& Json::operator[](const std::string& key) const {
    if (type != Obj) return NullValue();
    for (const auto& kv : o) if (kv.first == key) return kv.second;
    return NullValue();
}
const Json& Json::operator[](size_t i) const {
    if (type != Arr || i >= a.size()) return NullValue();
    return a[i];
}
bool Json::Has(const std::string& key) const {
    if (type != Obj) return false;
    for (const auto& kv : o) if (kv.first == key) return true;
    return false;
}
double Json::Num0(double def) const {
    if (type == Num) return n;
    if (type == Bool) return b ? 1 : 0;
    if (type == Str && !s.empty()) {
        char* end = nullptr;
        double v = strtod(s.c_str(), &end);
        if (end && end != s.c_str()) return v;
    }
    return def;
}
std::string Json::Str0(const std::string& def) const {
    if (type == Str) return s;
    if (type == Num) {
        char buf[64];
        if (n == (long long)n) snprintf(buf, sizeof buf, "%lld", (long long)n);
        else snprintf(buf, sizeof buf, "%g", n);
        return buf;
    }
    if (type == Bool) return b ? "true" : "false";
    return def;
}
bool Json::Bool0(bool def) const {
    if (type == Bool) return b;
    if (type == Num) return n != 0;
    if (type == Str) return s == "true" || s == "True" || s == "yes";
    return def;
}

namespace {
struct Parser {
    const char* p;
    const char* start;
    std::string err;
    void Ws() { while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++; }
    bool Fail(const char* msg) {
        if (err.empty()) {
            int line = 1, col = 1;
            for (const char* q = start; q < p; q++) { if (*q == '\n') { line++; col = 1; } else col++; }
            char buf[128];
            snprintf(buf, sizeof buf, "%d:%d %s", line, col, msg);
            err = buf;
        }
        return false;
    }
    static void PutUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    bool Hex4(unsigned& v) {
        v = 0;
        for (int i = 0; i < 4; i++) {
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return Fail("bad \\u escape");
        }
        return true;
    }
    bool String(std::string& out) {
        if (*p != '"') return Fail("expected string");
        p++;
        while (*p && *p != '"') {
            if (*p == '\\') {
                p++;
                switch (*p++) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        unsigned cp;
                        if (!Hex4(cp)) return false;
                        if (cp >= 0xD800 && cp < 0xDC00 && p[0] == '\\' && p[1] == 'u') {
                            p += 2;
                            unsigned lo;
                            if (!Hex4(lo)) return false;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        PutUtf8(out, cp);
                        break;
                    }
                    default: return Fail("bad escape");
                }
            } else out += *p++;
        }
        if (*p != '"') return Fail("unterminated string");
        p++;
        return true;
    }
    bool Value(Json& v) {
        Ws();
        if (*p == '{') {
            v.type = Json::Obj;
            p++;
            Ws();
            if (*p == '}') { p++; return true; }
            for (;;) {
                Ws();
                std::string k;
                if (!String(k)) return false;
                Ws();
                if (*p != ':') return Fail("expected ':'");
                p++;
                v.o.emplace_back(std::move(k), Json());
                if (!Value(v.o.back().second)) return false;
                Ws();
                if (*p == ',') { p++; continue; }
                if (*p == '}') { p++; return true; }
                return Fail("expected ',' or '}'");
            }
        }
        if (*p == '[') {
            v.type = Json::Arr;
            p++;
            Ws();
            if (*p == ']') { p++; return true; }
            for (;;) {
                v.a.emplace_back();
                if (!Value(v.a.back())) return false;
                Ws();
                if (*p == ',') { p++; continue; }
                if (*p == ']') { p++; return true; }
                return Fail("expected ',' or ']'");
            }
        }
        if (*p == '"') { v.type = Json::Str; return String(v.s); }
        if (!strncmp(p, "true", 4)) { v.type = Json::Bool; v.b = true; p += 4; return true; }
        if (!strncmp(p, "false", 5)) { v.type = Json::Bool; v.b = false; p += 5; return true; }
        if (!strncmp(p, "null", 4)) { v.type = Json::Null; p += 4; return true; }
        if (!strncmp(p, "NaN", 3)) { v.type = Json::Null; p += 3; return true; }
        char* end = nullptr;
        double d = strtod(p, &end);
        if (end == p) return Fail("unexpected character");
        v.type = Json::Num;
        v.n = d;
        p = end;
        return true;
    }
};
} // namespace

Json ParseJson(const std::string& text, std::string* err) {
    Parser ps{text.c_str(), text.c_str(), {}};
    Json v;
    if (!ps.Value(v)) {
        if (err) *err = ps.err;
        return Json();
    }
    return v;
}

Json LoadJsonFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { fprintf(stderr, "json: can't read %s\n", path.c_str()); return Json(); }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    Json v = ParseJson(ss.str(), &err);
    if (!err.empty()) fprintf(stderr, "json: %s: %s\n", path.c_str(), err.c_str());
    return v;
}

// ---- the course packs' API
bool ParseJson(const std::string& text, Json& out, std::string* err) {
    std::string e;
    out = ParseJson(text, &e);
    if (!e.empty()) { if (err) *err = e; return false; }
    return true;
}
bool LoadJsonFile(const std::string& path, Json& out, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "can't read " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF) s = s.substr(3);   // a UTF-8 byte-order mark
    std::string e;
    if (!ParseJson(s, out, &e)) { if (err) *err = path + ": " + e; return false; }
    return true;
}
std::string JsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if (c == '\n') o += "\\n"; else if (c == '\t') o += "\\t"; else if (c == '\r') o += "\\r"; else o += c; }
    return o;
}
Json JStr(const std::string& s) { Json j; j.type = Json::Str; j.s = s; return j; }
Json JNum(double n) { Json j; j.type = Json::Num; j.n = n; return j; }
static void W(const Json& j, int ind, int depth, std::string& o) {
    auto nl = [&](int d) { if (ind < 0) return; o += '\n'; o.append((size_t)(d * ind), ' '); };
    switch (j.type) {
    case Json::Null: o += "null"; break;
    case Json::Bool: o += j.b ? "true" : "false"; break;
    case Json::Num: { char b[40]; if (j.n == (long long)j.n && fabs(j.n) < 1e15) snprintf(b, sizeof b, "%lld", (long long)j.n); else snprintf(b, sizeof b, "%.17g", j.n); o += b; } break;
    case Json::Str: o += '"'; o += JsonEscape(j.s); o += '"'; break;
    case Json::Arr: {
        bool flat = true; for (auto& x : j.a) if (x.type == Json::Arr || x.type == Json::Obj) flat = false;
        o += '[';
        for (size_t i = 0; i < j.a.size(); i++) { if (i) o += flat || ind < 0 ? ", " : ","; if (!flat) nl(depth + 1); W(j.a[i], ind, depth + 1, o); }
        if (!flat && !j.a.empty()) nl(depth);
        o += ']';
    } break;
    case Json::Obj:
        o += '{';
        for (size_t i = 0; i < j.o.size(); i++) { if (i) o += ind < 0 ? ", " : ","; nl(depth + 1); o += '"'; o += JsonEscape(j.o[i].first); o += "\": "; W(j.o[i].second, ind, depth + 1, o); }
        if (!j.o.empty()) nl(depth);
        o += '}';
        break;
    }
}
std::string WriteJson(const Json& j, int indent) { std::string o; W(j, indent == 0 ? 2 : indent, 0, o); return o; }
