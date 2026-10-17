#include "discovery/DnsMessage.h"
#include <algorithm>
#include <map>

namespace airshow::dns {
namespace {

char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

void put16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    put16(out, static_cast<uint16_t>(v >> 16));
    put16(out, static_cast<uint16_t>(v));
}

// Uncompressed wire form; lowercased when `fold` is set.
void putPlainName(std::vector<uint8_t>& out, const Name& name, bool fold) {
    for (const auto& label : name) {
        const size_t len = std::min(label.size(), kMaxLabel);
        if (len == 0) continue;
        out.push_back(static_cast<uint8_t>(len));
        for (size_t i = 0; i < len; ++i)
            out.push_back(static_cast<uint8_t>(fold ? lower(label[i]) : label[i]));
    }
    out.push_back(0);
}

// Reads a possibly compressed name at *pos and advances *pos past it.
bool readName(const uint8_t* data, size_t size, size_t* pos, Name* out) {
    out->clear();
    size_t p = *pos;
    bool jumped = false;
    int jumps = 0;
    size_t total = 0;
    while (true) {
        if (p >= size) return false;
        const uint8_t len = data[p];
        if ((len & 0xC0) == 0xC0) {
            if (p + 1 >= size || ++jumps > 32) return false;
            if (!jumped) *pos = p + 2;
            jumped = true;
            p = (static_cast<size_t>(len & 0x3F) << 8) | data[p + 1];
            continue;
        }
        if (len & 0xC0) return false;  // extended label types: unsupported
        if (len == 0) {
            if (!jumped) *pos = p + 1;
            return true;
        }
        total += len + 1;
        if (p + 1 + len > size || total > 255) return false;
        out->emplace_back(reinterpret_cast<const char*>(data + p + 1), len);
        p += 1 + len;
    }
}

bool readRecord(const uint8_t* data, size_t size, size_t* pos, Record* r) {
    if (!readName(data, size, pos, &r->name) || *pos + 10 > size) return false;
    const uint8_t* h = data + *pos;
    r->type       = be16(h);
    r->cacheFlush = be16(h + 2) & 0x8000;
    r->rrclass    = be16(h + 2) & 0x7FFF;
    r->ttl        = be32(h + 4);
    const size_t rdlen = be16(h + 8);
    *pos += 10;
    const size_t end = *pos + rdlen;
    if (end > size) return false;

    size_t p = *pos;
    if (r->type == kPtr) {
        if (!readName(data, size, &p, &r->target) || p > end) return false;
    } else if (r->type == kSrv) {
        if (rdlen < 7) return false;
        r->priority = be16(data + p);
        r->weight   = be16(data + p + 2);
        r->port     = be16(data + p + 4);
        p += 6;
        if (!readName(data, size, &p, &r->target) || p > end) return false;
    } else {
        r->rdata.assign(data + *pos, data + end);
    }
    *pos = end;
    return true;
}

// Writes names with RFC 1035 compression: each suffix already written is
// replaced by a pointer to it.
class Writer {
public:
    std::vector<uint8_t> out;

    void name(const Name& n) {
        for (size_t i = 0; i < n.size(); ++i) {
            if (n[i].empty()) continue;
            const std::string key = suffixKey(n, i);
            const auto it = m_offsets.find(key);
            if (it != m_offsets.end()) {
                put16(out, static_cast<uint16_t>(0xC000 | it->second));
                return;
            }
            if (out.size() < 0x3FFF) m_offsets.emplace(key, static_cast<uint16_t>(out.size()));
            const size_t len = std::min(n[i].size(), kMaxLabel);
            out.push_back(static_cast<uint8_t>(len));
            out.insert(out.end(), n[i].begin(), n[i].begin() + static_cast<long>(len));
        }
        out.push_back(0);
    }

    void record(const Record& r) {
        name(r.name);
        put16(out, r.type);
        put16(out, static_cast<uint16_t>(r.rrclass | (r.cacheFlush ? 0x8000 : 0)));
        put32(out, r.ttl);
        const size_t lenAt = out.size();
        put16(out, 0);
        if (r.type == kPtr) {
            name(r.target);
        } else if (r.type == kSrv) {
            put16(out, r.priority);
            put16(out, r.weight);
            put16(out, r.port);
            name(r.target);
        } else {
            out.insert(out.end(), r.rdata.begin(), r.rdata.end());
        }
        const size_t rdlen = out.size() - lenAt - 2;
        out[lenAt]     = static_cast<uint8_t>(rdlen >> 8);
        out[lenAt + 1] = static_cast<uint8_t>(rdlen);
    }

private:
    static std::string suffixKey(const Name& n, size_t from) {
        std::string key;
        for (size_t i = from; i < n.size(); ++i) {
            if (n[i].empty()) continue;
            const size_t len = std::min(n[i].size(), kMaxLabel);
            key.push_back(static_cast<char>(len));
            for (size_t j = 0; j < len; ++j) key.push_back(lower(n[i][j]));
        }
        return key;
    }

    std::map<std::string, uint16_t> m_offsets;
};

} // namespace

Name parseName(const std::string& dotted) {
    Name name;
    size_t start = 0;
    while (start <= dotted.size()) {
        const size_t dot = dotted.find('.', start);
        const size_t end = dot == std::string::npos ? dotted.size() : dot;
        if (end > start) name.push_back(dotted.substr(start, end - start));
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return name;
}

std::string toString(const Name& name) {
    std::string s;
    for (const auto& label : name) {
        if (!s.empty()) s += '.';
        s += label;
    }
    return s;
}

bool sameName(const Name& a, const Name& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].size() != b[i].size()) return false;
        for (size_t j = 0; j < a[i].size(); ++j)
            if (lower(a[i][j]) != lower(b[i][j])) return false;
    }
    return true;
}

std::vector<uint8_t> Record::canonicalRdata() const {
    std::vector<uint8_t> out;
    if (type == kPtr) {
        putPlainName(out, target, true);
    } else if (type == kSrv) {
        put16(out, priority);
        put16(out, weight);
        put16(out, port);
        putPlainName(out, target, true);
    } else {
        out = rdata;
    }
    return out;
}

bool Record::sameAs(const Record& other) const {
    return type == other.type && rrclass == other.rrclass &&
           sameName(name, other.name) && canonicalRdata() == other.canonicalRdata();
}

std::optional<Message> parse(const uint8_t* data, size_t size) {
    if (size < 12) return std::nullopt;
    Message m;
    m.id    = be16(data);
    m.flags = be16(data + 2);
    const uint16_t counts[4] = {be16(data + 4), be16(data + 6), be16(data + 8), be16(data + 10)};
    size_t pos = 12;

    for (uint16_t i = 0; i < counts[0]; ++i) {
        Question q;
        if (!readName(data, size, &pos, &q.name) || pos + 4 > size) return std::nullopt;
        q.type            = be16(data + pos);
        q.unicastResponse = be16(data + pos + 2) & 0x8000;
        q.rrclass         = be16(data + pos + 2) & 0x7FFF;
        pos += 4;
        m.questions.push_back(std::move(q));
    }
    std::vector<Record>* sections[3] = {&m.answers, &m.authorities, &m.additionals};
    for (int s = 0; s < 3; ++s) {
        for (uint16_t i = 0; i < counts[s + 1]; ++i) {
            Record r;
            if (!readRecord(data, size, &pos, &r)) return std::nullopt;
            sections[s]->push_back(std::move(r));
        }
    }
    return m;
}

std::vector<uint8_t> serialize(const Message& message) {
    Writer w;
    put16(w.out, message.id);
    put16(w.out, message.flags);
    put16(w.out, static_cast<uint16_t>(message.questions.size()));
    put16(w.out, static_cast<uint16_t>(message.answers.size()));
    put16(w.out, static_cast<uint16_t>(message.authorities.size()));
    put16(w.out, static_cast<uint16_t>(message.additionals.size()));
    for (const auto& q : message.questions) {
        w.name(q.name);
        put16(w.out, q.type);
        put16(w.out, static_cast<uint16_t>(q.rrclass | (q.unicastResponse ? 0x8000 : 0)));
    }
    for (const auto& r : message.answers) w.record(r);
    for (const auto& r : message.authorities) w.record(r);
    for (const auto& r : message.additionals) w.record(r);
    return std::move(w.out);
}

Record makePtr(const Name& name, const Name& target, uint32_t ttl) {
    Record r;
    r.name   = name;
    r.type   = kPtr;
    r.ttl    = ttl;
    r.target = target;
    return r;
}

Record makeSrv(const Name& name, uint16_t port, const Name& target, uint32_t ttl) {
    Record r;
    r.name   = name;
    r.type   = kSrv;
    r.ttl    = ttl;
    r.port   = port;
    r.target = target;
    return r;
}

Record makeTxt(const Name& name, const std::vector<std::string>& entries, uint32_t ttl) {
    Record r;
    r.name = name;
    r.type = kTxt;
    r.ttl  = ttl;
    for (const auto& e : entries) {
        if (e.size() > 255) continue;
        r.rdata.push_back(static_cast<uint8_t>(e.size()));
        r.rdata.insert(r.rdata.end(), e.begin(), e.end());
    }
    if (r.rdata.empty()) r.rdata.push_back(0);  // an empty TXT is one zero byte
    return r;
}

Record makeA(const Name& name, uint32_t ipv4, uint32_t ttl) {
    Record r;
    r.name = name;
    r.type = kA;
    r.ttl  = ttl;
    put32(r.rdata, ipv4);
    return r;
}

Record makeNsec(const Name& name, const std::vector<uint16_t>& types, uint32_t ttl) {
    Record r;
    r.name = name;
    r.type = kNsec;
    r.ttl  = ttl;
    putPlainName(r.rdata, name, false);  // "next domain name" is the name itself
    uint8_t bitmap[32] = {};
    size_t used = 0;
    for (const uint16_t t : types) {
        if (t >= 256) continue;  // window 0 only — all mDNS needs
        bitmap[t / 8] |= static_cast<uint8_t>(0x80 >> (t % 8));
        used = std::max(used, static_cast<size_t>(t / 8 + 1));
    }
    r.rdata.push_back(0);  // window block 0
    r.rdata.push_back(static_cast<uint8_t>(used));
    r.rdata.insert(r.rdata.end(), bitmap, bitmap + used);
    return r;
}

} // namespace airshow::dns
