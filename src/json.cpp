#include "json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

static const Json kNull;
const Json& Json::operator[](const char* key) const { if (type == OBJ) for (auto& kv : o) if (kv.first == key) return kv.second; return kNull; }
const Json& Json::operator[](size_t i) const { return type == ARR && i < a.size() ? a[i] : kNull; }
bool Json::Has(const char* key) const { if (type == OBJ) for (auto& kv : o) if (kv.first == key) return true; return false; }

namespace {
struct P {
    const std::string& t; size_t i = 0; std::string err;
    explicit P(const std::string& s) : t(s) {}
    int Line() const { int l = 1; for (size_t k = 0; k < i && k < t.size(); k++) if (t[k] == '\n') l++; return l; }
    bool Fail(const char* m) { if (err.empty()) err = std::string(m) + " at line " + std::to_string(Line()); return false; }
    void Ws() { while (i < t.size()) { if (isspace((unsigned char)t[i])) i++; else if (t.compare(i, 2, "//") == 0) { while (i < t.size() && t[i] != '\n') i++; } else break; } }
    bool Str(std::string& out) {
        if (t[i] != '"') return Fail("expected a string");
        i++;
        while (i < t.size() && t[i] != '"') {
            char c = t[i++];
            if (c == '\\' && i < t.size()) {
                char e = t[i++];
                switch (e) {
                case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break;
                case 'b': out += '\b'; break; case 'f': out += '\f'; break;
                case 'u': {
                    if (i + 4 > t.size()) return Fail("bad \\u escape");
                    unsigned cp = (unsigned)strtoul(t.substr(i, 4).c_str(), nullptr, 16); i += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF && t.compare(i, 2, "\\u") == 0) { unsigned lo = (unsigned)strtoul(t.substr(i + 2, 4).c_str(), nullptr, 16); i += 6; cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); }
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 63)); }
                    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63)); }
                    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 63)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63)); }
                } break;
                default: out += e; break;
                }
            } else out += c;
        }
        if (i >= t.size()) return Fail("unterminated string");
        i++;
        return true;
    }
    bool Val(Json& v) {
        Ws();
        if (i >= t.size()) return Fail("unexpected end");
        char c = t[i];
        if (c == '{') {
            v.type = Json::OBJ; i++; Ws();
            if (i < t.size() && t[i] == '}') { i++; return true; }
            for (;;) {
                Ws(); std::string k; if (!Str(k)) return false;
                Ws(); if (i >= t.size() || t[i] != ':') return Fail("expected ':'"); i++;
                Json x; if (!Val(x)) return false;
                v.o.emplace_back(std::move(k), std::move(x));
                Ws(); if (i < t.size() && t[i] == ',') { i++; Ws(); if (i < t.size() && t[i] == '}') { i++; return true; } continue; }
                if (i < t.size() && t[i] == '}') { i++; return true; }
                return Fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.type = Json::ARR; i++; Ws();
            if (i < t.size() && t[i] == ']') { i++; return true; }
            for (;;) {
                Json x; if (!Val(x)) return false;
                v.a.push_back(std::move(x));
                Ws(); if (i < t.size() && t[i] == ',') { i++; Ws(); if (i < t.size() && t[i] == ']') { i++; return true; } continue; }
                if (i < t.size() && t[i] == ']') { i++; return true; }
                return Fail("expected ',' or ']'");
            }
        }
        if (c == '"') { v.type = Json::STR; return Str(v.s); }
        if (t.compare(i, 4, "true") == 0) { v.type = Json::BOOL; v.b = true; i += 4; return true; }
        if (t.compare(i, 5, "false") == 0) { v.type = Json::BOOL; v.b = false; i += 5; return true; }
        if (t.compare(i, 4, "null") == 0) { v.type = Json::NUL; i += 4; return true; }
        char* end = nullptr;
        double d = strtod(t.c_str() + i, &end);
        if (end == t.c_str() + i) return Fail("unexpected character");
        v.type = Json::NUM; v.n = d; i = end - t.c_str();
        return true;
    }
};
}

bool ParseJson(const std::string& text, Json& out, std::string* err) {
    P p(text);
    out = Json{};
    if (!p.Val(out)) { if (err) *err = p.err; return false; }
    p.Ws();
    if (p.i != text.size()) { p.Fail("trailing characters"); if (err) *err = p.err; return false; }
    return true;
}
bool LoadJsonFile(const std::string& path, Json& out, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
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
Json JStr(const std::string& s) { Json j; j.type = Json::STR; j.s = s; return j; }
Json JNum(double n) { Json j; j.type = Json::NUM; j.n = n; return j; }
static void W(const Json& j, int ind, int depth, std::string& o) {
    auto nl = [&](int d) { if (ind < 0) return; o += '\n'; o.append((size_t)(d * ind), ' '); };
    switch (j.type) {
    case Json::NUL: o += "null"; break;
    case Json::BOOL: o += j.b ? "true" : "false"; break;
    case Json::NUM: { char b[40]; if (j.n == (long long)j.n && fabs(j.n) < 1e15) snprintf(b, sizeof b, "%lld", (long long)j.n); else snprintf(b, sizeof b, "%.17g", j.n); o += b; } break;
    case Json::STR: o += '"'; o += JsonEscape(j.s); o += '"'; break;
    case Json::ARR: {
        bool flat = true; for (auto& x : j.a) if (x.type == Json::ARR || x.type == Json::OBJ) flat = false;
        o += '[';
        for (size_t i = 0; i < j.a.size(); i++) { if (i) o += flat || ind < 0 ? ", " : ","; if (!flat) nl(depth + 1); W(j.a[i], ind, depth + 1, o); }
        if (!flat && !j.a.empty()) nl(depth);
        o += ']';
    } break;
    case Json::OBJ:
        o += '{';
        for (size_t i = 0; i < j.o.size(); i++) { if (i) o += ind < 0 ? ", " : ","; nl(depth + 1); o += '"'; o += JsonEscape(j.o[i].first); o += "\": "; W(j.o[i].second, ind, depth + 1, o); }
        if (!j.o.empty()) nl(depth);
        o += '}';
        break;
    }
}
std::string WriteJson(const Json& j, int indent) { std::string o; W(j, indent == 0 ? 2 : indent, 0, o); return o; }
