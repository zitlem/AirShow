#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// DNS wire format (RFC 1035) with the Multicast DNS bits (RFC 6762) — just
// enough for MdnsResponder to parse queries and build responses.
namespace airshow::dns {

// A domain name as its labels, e.g. {"_airplay", "_tcp", "local"}. Labels are
// raw bytes: DNS-SD instance names may contain spaces, dots and UTF-8.
using Name = std::vector<std::string>;

// "_airplay._tcp.local" -> {"_airplay", "_tcp", "local"} (no escape handling;
// only for fixed names — build instance names label by label).
Name parseName(const std::string& dotted);
std::string toString(const Name& name);
// Names compare ASCII case-insensitively.
bool sameName(const Name& a, const Name& b);

enum Type : uint16_t {
    kA    = 1,
    kPtr  = 12,
    kTxt  = 16,
    kAaaa = 28,
    kSrv  = 33,
    kNsec = 47,
    kAny  = 255,
};

constexpr uint16_t kClassIn  = 1;
constexpr uint16_t kClassAny = 255;

constexpr uint16_t kFlagResponse      = 0x8000;  // QR
constexpr uint16_t kFlagAuthoritative = 0x0400;  // AA
constexpr uint16_t kFlagTruncated     = 0x0200;  // TC

constexpr size_t kMaxLabel = 63;

struct Question {
    Name     name;
    uint16_t type    = 0;
    uint16_t rrclass = kClassIn;
    bool     unicastResponse = false;  // QU: top bit of the class
};

struct Record {
    Name     name;
    uint16_t type    = 0;
    uint16_t rrclass = kClassIn;
    bool     cacheFlush = false;       // top bit of the class in responses
    uint32_t ttl = 0;

    // PTR and SRV keep their target name (compressible); every other type
    // keeps raw rdata.
    Name     target;
    uint16_t priority = 0;
    uint16_t weight   = 0;
    uint16_t port     = 0;
    std::vector<uint8_t> rdata;

    // Uncompressed rdata with names lowercased, for comparing records
    // (conflict detection, known answers, probe tie-breaks).
    std::vector<uint8_t> canonicalRdata() const;
    // Same name, type, class and rdata (TTL and cache-flush ignored).
    bool sameAs(const Record& other) const;
};

struct Message {
    uint16_t id    = 0;
    uint16_t flags = 0;
    std::vector<Question> questions;
    std::vector<Record>   answers;
    std::vector<Record>   authorities;
    std::vector<Record>   additionals;

    bool isResponse() const { return flags & kFlagResponse; }
};

// Returns nullopt for anything malformed (truncation, compression loops,
// over-long names).
std::optional<Message> parse(const uint8_t* data, size_t size);
// Serialises with name compression. Labels longer than 63 bytes are cut.
std::vector<uint8_t> serialize(const Message& message);

Record makePtr(const Name& name, const Name& target, uint32_t ttl);
Record makeSrv(const Name& name, uint16_t port, const Name& target, uint32_t ttl);
// Each entry is one "key=value" string (max 255 bytes; longer ones are dropped).
Record makeTxt(const Name& name, const std::vector<std::string>& entries, uint32_t ttl);
// ipv4 in host byte order.
Record makeA(const Name& name, uint32_t ipv4, uint32_t ttl);
// NSEC asserting that `types` are the only record types `name` has (RFC 6762 §6.1).
Record makeNsec(const Name& name, const std::vector<uint16_t>& types, uint32_t ttl);

} // namespace airshow::dns
