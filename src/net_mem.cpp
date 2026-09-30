// An in-process Transport (see net.h): every endpoint is a mailbox in one global table, so a host and its clients
// can run in one process with no sockets. Reliable and in order on every channel (the session doesn't care).
#include "net.h"
#include <algorithm>
#include <deque>
#include <map>

namespace net {
namespace {
class MemTransport;
struct Link { MemTransport* a = nullptr; MemTransport* b = nullptr; int peer = 0; };
std::map<int, Link> gLinks;           // connection id -> the transport that owns it and the id at the other end
std::map<uint16_t, MemTransport*> gListeners;
int gNextConn = 1000;

class MemTransport : public Transport {
public:
    std::vector<Event> inbox;
    uint16_t port = 0;
    ~MemTransport() override {
        CloseAll();
        for (auto& [id, l] : gLinks) if (l.b == this) l.b = nullptr;
    }
    bool Listen(uint16_t p, std::string* err) override {
        if (gListeners.count(p)) { if (err) *err = "port in use"; return false; }
        gListeners[p] = this; port = p; return true;
    }
    int Connect(const std::string& addr, std::string* err) override {
        uint16_t p = GAME_PORT;
        size_t colon = addr.rfind(':');
        if (colon != std::string::npos) p = (uint16_t)atoi(addr.c_str() + colon + 1);
        auto it = gListeners.find(p);
        if (it == gListeners.end()) { if (err) *err = "nobody hosting on that port"; return -1; }
        int mine = gNextConn++, theirs = gNextConn++;
        gLinks[mine] = {this, it->second, theirs};
        gLinks[theirs] = {it->second, this, mine};
        Event e; e.kind = Event::Connected; e.conn = mine; e.info = "mem"; inbox.push_back(e);
        Event h; h.kind = Event::Connected; h.conn = theirs; h.info = "mem"; it->second->inbox.push_back(h);
        return mine;
    }
    void Send(int conn, Channel ch, const void* data, int n) override {
        auto it = gLinks.find(conn);
        if (it == gLinks.end() || it->second.a != this || !it->second.b) return;
        Event e; e.kind = Event::Message; e.conn = it->second.peer; e.ch = ch;
        e.data.assign((const uint8_t*)data, (const uint8_t*)data + n);
        it->second.b->inbox.push_back(std::move(e));
    }
    void Poll(std::vector<Event>& out) override { for (auto& e : inbox) out.push_back(std::move(e)); inbox.clear(); }
    void Cut(int conn, const char* why, bool tellMe) {
        auto it = gLinks.find(conn);
        if (it == gLinks.end()) return;
        Link l = it->second;
        gLinks.erase(it);
        if (tellMe) { Event e; e.kind = Event::Disconnected; e.conn = conn; e.info = why ? why : ""; inbox.push_back(e); }
        auto pt = gLinks.find(l.peer);
        if (pt != gLinks.end()) {
            MemTransport* other = pt->second.a;
            gLinks.erase(pt);
            if (other) { Event e; e.kind = Event::Disconnected; e.conn = l.peer; e.info = why ? why : "closed"; other->inbox.push_back(e); }
        }
    }
    void Close(int conn, const char* why) override { Cut(conn, why, false); }
    void CloseAll() override {
        std::vector<int> mine;
        for (auto& [id, l] : gLinks) if (l.a == this) mine.push_back(id);
        for (int id : mine) Cut(id, "closing", false);
        if (port && gListeners.count(port) && gListeners[port] == this) gListeners.erase(port);
        port = 0;
    }
    Stats GetStats(int) override { return {}; }
};
}
std::unique_ptr<Transport> MakeMemoryTransport() { return std::make_unique<MemTransport>(); }
void MemoryDrop(int conn) {
    auto it = gLinks.find(conn);
    if (it != gLinks.end() && it->second.a) static_cast<MemTransport*>(it->second.a)->Cut(conn, "connection lost", true);
}
}
