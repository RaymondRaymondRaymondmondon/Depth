#pragma once
// Little-endian message writing and reading for the Deep Arcade's networking (docs/design/5_Depth_Arcade_Networking.md).
// No raylib and no Windows headers here: the game, the network code and the headless engines all share it.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct Writer {
    std::vector<uint8_t> b;
    void U8(uint32_t v) { b.push_back((uint8_t)v); }
    void U16(uint32_t v) { U8(v & 0xFF); U8((v >> 8) & 0xFF); }
    void U32(uint32_t v) { U16(v & 0xFFFF); U16(v >> 16); }
    void U64(uint64_t v) { U32((uint32_t)v); U32((uint32_t)(v >> 32)); }
    void I32(int32_t v) { U32((uint32_t)v); }
    void F32(float f) { uint32_t u; memcpy(&u, &f, 4); U32(u); }
    void VarU(uint32_t v) { while (v >= 0x80) { U8((v & 0x7F) | 0x80); v >>= 7; } U8(v); }
    void Str(const std::string& s) { VarU((uint32_t)s.size()); b.insert(b.end(), s.begin(), s.end()); }
    void Bytes(const void* p, size_t n) { const uint8_t* q = (const uint8_t*)p; b.insert(b.end(), q, q + n); }
};

struct Reader {
    const uint8_t* p = nullptr; size_t n = 0, i = 0; bool bad = false;
    Reader() = default;
    Reader(const void* data, size_t len) : p((const uint8_t*)data), n(len) {}
    explicit Reader(const std::vector<uint8_t>& v) : p(v.data()), n(v.size()) {}
    bool Has(size_t k) { if (i + k > n) { bad = true; return false; } return true; }
    uint32_t U8() { if (!Has(1)) return 0; return p[i++]; }
    uint32_t U16() { uint32_t a = U8(); return a | (U8() << 8); }
    uint32_t U32() { uint32_t a = U16(); return a | (U16() << 16); }
    uint64_t U64() { uint64_t a = U32(); return a | ((uint64_t)U32() << 32); }
    int32_t I32() { return (int32_t)U32(); }
    float F32() { uint32_t u = U32(); float f; memcpy(&f, &u, 4); return f; }
    uint32_t VarU() { uint32_t v = 0; for (int s = 0; s < 35; s += 7) { uint32_t c = U8(); v |= (c & 0x7F) << s; if (!(c & 0x80) || bad) break; } return v; }
    std::string Str() { uint32_t k = VarU(); if (k > 4096 || !Has(k)) { bad = true; return {}; } std::string s((const char*)p + i, k); i += k; return s; }
    bool Done() const { return i >= n; }
};

inline uint32_t Fnv1a(const void* data, size_t n, uint32_t h = 2166136261u) {
    const uint8_t* q = (const uint8_t*)data;
    for (size_t k = 0; k < n; k++) h = (h ^ q[k]) * 16777619u;
    return h;
}
