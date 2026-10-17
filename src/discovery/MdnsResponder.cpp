#include "discovery/MdnsResponder.h"
#include "discovery/DnsMessage.h"
#include <QElapsedTimer>
#include <QHostAddress>
#include <QMetaObject>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QUdpSocket>
#include <QVariant>
#include <glib.h>
#include <algorithm>
#include <functional>
#include <tuple>

namespace airshow {
namespace {

// TTLs from RFC 6762 §10: records naming a host 120 s, everything else 75 min.
constexpr uint32_t kHostTtl   = 120;
constexpr uint32_t kOtherTtl  = 4500;
constexpr uint32_t kLegacyTtl = 10;   // cap for replies to legacy unicast queries (§6.7)

constexpr int kProbeCount       = 3;     // §8.1: three probes, 250 ms apart
constexpr int kProbeIntervalMs  = 250;
constexpr int kTickMs           = 25;
constexpr int kRescanIntervalMs = 10000;
constexpr size_t kMaxPacket     = 1400;  // stay under a 1500-byte MTU

QHostAddress mdnsGroup() { return QHostAddress(QStringLiteral("224.0.0.251")); }

dns::Name concat(dns::Name a, const dns::Name& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// Cut a UTF-8 string to at most maxBytes without splitting a character.
std::string fitUtf8(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t n = maxBytes;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    return s.substr(0, n);
}

// "DESKTOP-1234-AirShow": the machine's host name reduced to a DNS label, so
// SRV records point at a name no other responder on this machine answers for.
std::string defaultHostLabel() {
    const QString host = QSysInfo::machineHostName().section(QLatin1Char('.'), 0, 0);
    std::string label;
    for (const QChar c : host) {
        if (c.unicode() < 128 && c.isLetterOrNumber())
            label += static_cast<char>(c.unicode());
        else if (!label.empty() && label.back() != '-')
            label += '-';
        if (label.size() >= 40) break;
    }
    while (!label.empty() && label.back() == '-') label.pop_back();
    return label.empty() ? "AirShow" : label + "-AirShow";
}

bool isUnique(const dns::Record& r) { return r.type != dns::kPtr; }

bool containsRecord(const std::vector<dns::Record>& records, const dns::Record& r) {
    return std::any_of(records.begin(), records.end(),
                       [&](const dns::Record& x) { return x.sameAs(r); });
}

// RFC 6762 §8.2: order both sets by class, type and rdata, then compare them
// pairwise; the lexicographically later set wins. <0 means `ours` loses.
int compareRecordSets(std::vector<dns::Record> ours, std::vector<dns::Record> theirs) {
    const auto key = [](const dns::Record& r) {
        return std::make_tuple(r.rrclass, r.type, r.canonicalRdata());
    };
    const auto byKey = [&](const dns::Record& a, const dns::Record& b) { return key(a) < key(b); };
    std::sort(ours.begin(), ours.end(), byKey);
    std::sort(theirs.begin(), theirs.end(), byKey);
    for (size_t i = 0; i < std::min(ours.size(), theirs.size()); ++i) {
        if (byKey(ours[i], theirs[i])) return -1;
        if (byKey(theirs[i], ours[i])) return 1;
    }
    if (ours.size() == theirs.size()) return 0;
    return ours.size() < theirs.size() ? -1 : 1;
}

// Splits an oversized response into several packets, answers first.
std::vector<std::vector<uint8_t>> toPackets(const dns::Message& m) {
    std::vector<uint8_t> whole = dns::serialize(m);
    if (whole.size() <= kMaxPacket || !m.authorities.empty()) return {std::move(whole)};

    std::vector<std::vector<uint8_t>> packets;
    dns::Message base = m;
    base.answers.clear();
    base.additionals.clear();
    dns::Message cur = base;
    const auto add = [&](const dns::Record& r, bool additional) {
        auto& section = additional ? cur.additionals : cur.answers;
        section.push_back(r);
        if (cur.answers.size() + cur.additionals.size() > 1 &&
            dns::serialize(cur).size() > kMaxPacket) {
            section.pop_back();
            packets.push_back(dns::serialize(cur));
            cur = base;
            (additional ? cur.additionals : cur.answers).push_back(r);
        }
    };
    for (const auto& r : m.answers) add(r, false);
    for (const auto& r : m.additionals) add(r, true);
    if (!cur.answers.empty() || !cur.additionals.empty()) packets.push_back(dns::serialize(cur));
    return packets;
}

} // namespace

class MdnsResponderCore : public QObject {
public:
    MdnsResponderCore(uint16_t port, std::string hostLabel)
        : m_port(port), m_hostLabel(std::move(hostLabel)) {}

    bool start() {
        m_clock.start();
        m_socket = new QUdpSocket(this);
        if (!m_socket->bind(QHostAddress::AnyIPv4, m_port,
                            QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            g_warning("MdnsResponder: cannot open UDP port %u: %s", m_port,
                      qPrintable(m_socket->errorString()));
            return false;
        }
        m_socket->setSocketOption(QAbstractSocket::MulticastTtlOption, 255);
        m_socket->setSocketOption(QAbstractSocket::MulticastLoopbackOption, 1);
        connect(m_socket, &QUdpSocket::readyRead, this, &MdnsResponderCore::readPending);

        m_probeTimer = new QTimer(this);
        m_probeTimer->setInterval(kTickMs);
        connect(m_probeTimer, &QTimer::timeout, this, &MdnsResponderCore::probeTick);

        auto* rescan = new QTimer(this);
        rescan->setInterval(kRescanIntervalMs);
        connect(rescan, &QTimer::timeout, this, &MdnsResponderCore::rescanInterfaces);
        rescan->start();
        rescanInterfaces();

        beginProbing(m_host, randomDelay());
        g_message("MdnsResponder: built-in mDNS responder on port %u, %zu interface(s), host %s",
                  m_port, m_ifaces.size(), hostName().c_str());
        return true;
    }

    void addService(const std::string& type, const std::string& name, uint16_t port,
                    const std::vector<TxtRecord>& txt) {
        Service s;
        s.id       = m_nextId++;
        s.type     = type;
        s.baseName = name;
        s.port     = port;
        s.txt      = txt;
        m_services.push_back(std::move(s));
        beginProbing(m_services.back().group, randomDelay());
    }

    void renameAll(const std::string& name) {
        for (auto& s : m_services) {
            if (s.group.announced) sendGoodbye(s);
            s.baseName = name;
            s.suffix   = 1;
            beginProbing(s.group, randomDelay());
        }
    }

    void updateTxt(const std::string& type, const std::string& key, const std::string& value) {
        for (auto& s : m_services) {
            if (s.type != type) continue;
            auto it = std::find_if(s.txt.begin(), s.txt.end(),
                                   [&](const TxtRecord& r) { return r.key == key; });
            if (it != s.txt.end()) it->value = value;
            else s.txt.push_back({key, value});
            if (s.group.announced) {
                ++s.group.generation;
                announceService(s.id);
            }
        }
    }

    // Goodbyes for every service, then forget them (the socket stays open).
    void shutdown() {
        for (const auto& s : m_services)
            if (s.group.announced) sendGoodbye(s);
        m_services.clear();
    }

    std::string hostName() const { return dns::toString(hostFqdn()); }

    std::vector<std::string> instanceNames(const std::string& type) const {
        std::vector<std::string> names;
        for (const auto& s : m_services)
            if (s.type == type) names.push_back(instanceLabel(s));
        return names;
    }

private:
    // A name we must own exclusively: our host name, or one service instance.
    struct Group {
        int    probesSent  = 0;
        bool   announced   = false;
        qint64 nextProbeAt = 0;
        int    generation  = 0;   // bumped on re-probe/update; stops stale re-announcements
    };

    struct Service {
        int                    id = 0;
        std::string            type;      // "_airplay._tcp"
        std::string            baseName;  // requested instance name
        int                    suffix = 1;  // "Name (2)" after a conflict
        uint16_t               port = 0;
        std::vector<TxtRecord> txt;
        Group                  group;
    };

    struct Iface {
        QNetworkInterface                net;
        std::vector<QNetworkAddressEntry> v4;
    };

    // ── Names and records ────────────────────────────────────────────────────

    dns::Name hostFqdn() const {
        const std::string label = m_hostSuffix == 1
            ? m_hostLabel : m_hostLabel + "-" + std::to_string(m_hostSuffix);
        return {label, "local"};
    }

    static dns::Name typeName(const Service& s) {
        return concat(dns::parseName(s.type), {"local"});
    }

    static std::string instanceLabel(const Service& s) {
        const std::string suffix = s.suffix == 1 ? "" : " (" + std::to_string(s.suffix) + ")";
        const std::string base = s.baseName.empty() ? "AirShow" : s.baseName;
        return fitUtf8(base, dns::kMaxLabel - suffix.size()) + suffix;
    }

    static dns::Name instanceName(const Service& s) {
        return concat({instanceLabel(s)}, typeName(s));
    }

    static dns::Record enumPtr(const Service& s) {
        return dns::makePtr(dns::parseName("_services._dns-sd._udp.local"), typeName(s), kOtherTtl);
    }

    static dns::Record instancePtr(const Service& s) {
        return dns::makePtr(typeName(s), instanceName(s), kOtherTtl);
    }

    dns::Record srv(const Service& s) const {
        dns::Record r = dns::makeSrv(instanceName(s), s.port, hostFqdn(), kHostTtl);
        r.cacheFlush = true;
        return r;
    }

    static dns::Record txt(const Service& s) {
        std::vector<std::string> entries;
        for (const auto& t : s.txt) entries.push_back(t.key + "=" + t.value);
        dns::Record r = dns::makeTxt(instanceName(s), entries, kOtherTtl);
        r.cacheFlush = true;
        return r;
    }

    // A records for one interface's addresses (all interfaces if null), plus
    // an NSEC saying we have no other address types — spares clients a wait
    // for AAAA answers that never come.
    std::vector<dns::Record> hostRecords(const Iface* iface) const {
        std::vector<dns::Record> out;
        const auto addFrom = [&](const Iface& i) {
            for (const auto& e : i.v4) {
                dns::Record a = dns::makeA(hostFqdn(), e.ip().toIPv4Address(), kHostTtl);
                a.cacheFlush = true;
                if (!containsRecord(out, a)) out.push_back(a);
            }
        };
        if (iface) addFrom(*iface);
        else for (const auto& i : m_ifaces) addFrom(i);
        dns::Record nsec = dns::makeNsec(hostFqdn(), {dns::kA}, kHostTtl);
        nsec.cacheFlush = true;
        out.push_back(nsec);
        return out;
    }

    // Everything we currently answer for, as seen from `iface`.
    std::vector<dns::Record> currentRecords(const Iface* iface) const {
        std::vector<dns::Record> out;
        if (m_host.announced) out = hostRecords(iface);
        for (const auto& s : m_services) {
            if (!s.group.announced) continue;
            const dns::Record e = enumPtr(s);
            if (!containsRecord(out, e)) out.push_back(e);
            out.push_back(instancePtr(s));
            out.push_back(srv(s));
            out.push_back(txt(s));
        }
        return out;
    }

    // ── Sending ──────────────────────────────────────────────────────────────

    void sendTo(const dns::Message& m, const QHostAddress& to, quint16 port) {
        for (const auto& p : toPackets(m))
            m_socket->writeDatagram(reinterpret_cast<const char*>(p.data()),
                                    static_cast<qint64>(p.size()), to, port);
    }

    void multicast(const dns::Message& m, const Iface& iface) {
        m_socket->setMulticastInterface(iface.net);
        sendTo(m, mdnsGroup(), m_port);
    }

    static dns::Message response() {
        dns::Message m;
        m.flags = dns::kFlagResponse | dns::kFlagAuthoritative;
        return m;
    }

    // §8.3: announce at least twice, one second apart; a third copy after a
    // further two seconds rides out Wi-Fi packet loss.
    void repeat(const std::function<void()>& send) {
        send();
        QTimer::singleShot(1000, this, send);
        QTimer::singleShot(3000, this, send);
    }

    // ── Probing and announcing ───────────────────────────────────────────────

    int randomDelay() const { return static_cast<int>(QRandomGenerator::global()->bounded(250)); }

    void beginProbing(Group& g, int delayMs) {
        g.probesSent  = 0;
        g.announced   = false;
        g.nextProbeAt = m_clock.elapsed() + delayMs;
        ++g.generation;
        if (!m_probeTimer->isActive()) m_probeTimer->start();
    }

    void probeTick() {
        const qint64 now = m_clock.elapsed();
        bool probing = false;
        const auto step = [&](Group& g, const std::function<void()>& probe,
                              const std::function<void()>& announce, bool canAnnounce) {
            if (g.announced) return;
            probing = true;
            if (now < g.nextProbeAt) return;
            if (g.probesSent < kProbeCount) {
                probe();
                ++g.probesSent;
                g.nextProbeAt = now + kProbeIntervalMs;
            } else if (canAnnounce) {
                g.announced = true;
                announce();
            }
        };
        step(m_host, [this] { sendHostProbe(); }, [this] { announceHost(); }, true);
        // A service only goes live once its SRV target (our host name) answers.
        for (size_t i = 0; i < m_services.size(); ++i) {
            const int id = m_services[i].id;
            step(m_services[i].group,
                 [this, i] { sendServiceProbe(m_services[i]); },
                 [this, id] {
                     const Service* s = findService(id);
                     g_message("MdnsResponder: advertising '%s' (%s)",
                               instanceLabel(*s).c_str(), s->type.c_str());
                     announceService(id);
                 },
                 m_host.announced);
        }
        if (!probing) m_probeTimer->stop();
    }

    // Probes ask "does anyone own this name?" (type ANY) and carry the records
    // we intend to use in the authority section for tie-breaking (§8.1, §8.2).
    // They don't ask for unicast replies: on Windows the system's DNS client
    // shares port 5353 and could be handed a unicast reply instead of us.
    void sendHostProbe() {
        for (const auto& iface : m_ifaces) {
            dns::Message m;
            m.questions.push_back({hostFqdn(), dns::kAny, dns::kClassIn, false});
            m.authorities = hostProbeRecords(&iface);
            multicast(m, iface);
        }
    }

    std::vector<dns::Record> hostProbeRecords(const Iface* iface) const {
        std::vector<dns::Record> out;
        for (auto r : hostRecords(iface)) {
            if (r.type != dns::kA) continue;
            r.cacheFlush = false;
            out.push_back(r);
        }
        return out;
    }

    std::vector<dns::Record> serviceProbeRecords(const Service& s) const {
        dns::Record a = srv(s);
        dns::Record b = txt(s);
        a.cacheFlush = b.cacheFlush = false;
        return {a, b};
    }

    void sendServiceProbe(const Service& s) {
        dns::Message m;
        m.questions.push_back({instanceName(s), dns::kAny, dns::kClassIn, false});
        m.authorities = serviceProbeRecords(s);
        for (const auto& iface : m_ifaces) multicast(m, iface);
    }

    void announceHost() {
        const int gen = m_host.generation;
        repeat([this, gen] {
            if (!m_host.announced || m_host.generation != gen) return;
            for (const auto& iface : m_ifaces) {
                dns::Message m = response();
                m.answers = hostRecords(&iface);
                multicast(m, iface);
            }
        });
        // SRV records name the host; if it was renamed they changed too.
        for (const auto& s : m_services)
            if (s.group.announced) announceService(s.id);
    }

    void announceService(int id) {
        const Service* s = findService(id);
        if (!s) return;
        const int gen = s->group.generation;
        repeat([this, id, gen] {
            const Service* s = findService(id);
            if (!s || !s->group.announced || s->group.generation != gen) return;
            for (const auto& iface : m_ifaces) {
                dns::Message m = response();
                m.answers = {enumPtr(*s), instancePtr(*s), srv(*s), txt(*s)};
                if (m_host.announced) m.additionals = hostRecords(&iface);
                multicast(m, iface);
            }
        });
    }

    // TTL 0 tells caches to drop the records now (§10.1).
    void sendGoodbye(const Service& s) {
        dns::Message m = response();
        m.answers = {instancePtr(s), srv(s), txt(s)};
        for (auto& r : m.answers) r.ttl = 0;
        for (const auto& iface : m_ifaces) multicast(m, iface);
    }

    // ── Receiving ────────────────────────────────────────────────────────────

    void readPending() {
        while (m_socket->hasPendingDatagrams()) {
            const QNetworkDatagram dg = m_socket->receiveDatagram();
            const QByteArray data = dg.data();
            const auto msg = dns::parse(reinterpret_cast<const uint8_t*>(data.constData()),
                                        static_cast<size_t>(data.size()));
            if (!msg) continue;
            if (msg->isResponse()) handleResponse(*msg, dg);
            else handleQuery(*msg, dg);
        }
    }

    void handleQuery(const dns::Message& q, const QNetworkDatagram& dg) {
        const QHostAddress from = dg.senderAddress();
        const auto fromPort = static_cast<quint16>(dg.senderPort());
        const Iface* iface = ifaceFor(dg);
        if (!q.authorities.empty() && !isOwnAddress(from)) resolveSimultaneousProbe(q, iface);

        const std::vector<dns::Record> records = currentRecords(iface);
        dns::Message resp = response();
        bool allUnicast = !q.questions.empty();
        bool shared = false;
        for (const auto& question : q.questions) {
            allUnicast = allUnicast && question.unicastResponse;
            if (question.rrclass != dns::kClassIn && question.rrclass != dns::kClassAny) continue;
            bool nameKnown = false;
            bool typeKnown = false;
            for (const auto& r : records) {
                if (!dns::sameName(r.name, question.name)) continue;
                nameKnown = true;
                if (question.type != r.type && question.type != dns::kAny) continue;
                typeKnown = true;
                if (isKnownAnswer(q, r) || containsRecord(resp.answers, r)) continue;
                resp.answers.push_back(r);
                shared = shared || !isUnique(r);
            }
            // Asked for a type our host doesn't have (AAAA): answer with the NSEC.
            if (nameKnown && !typeKnown && dns::sameName(question.name, hostFqdn())) {
                for (const auto& r : records)
                    if (r.type == dns::kNsec && dns::sameName(r.name, question.name) &&
                        !containsRecord(resp.answers, r))
                        resp.answers.push_back(r);
            }
        }
        if (resp.answers.empty()) return;
        addAdditionals(resp, records);

        // Legacy unicast query (not from port 5353): plain DNS reply straight
        // back, echoing ID and questions, short TTLs, no cache-flush bits (§6.7).
        if (fromPort != m_port) {
            resp.id = q.id;
            resp.questions = q.questions;
            for (auto& question : resp.questions) question.unicastResponse = false;
            for (auto* section : {&resp.answers, &resp.additionals}) {
                for (auto& r : *section) {
                    r.cacheFlush = false;
                    r.ttl = std::min(r.ttl, kLegacyTtl);
                }
            }
            sendTo(resp, from, fromPort);
            return;
        }

        // Probe answers always go multicast, so a prober sharing port 5353 with
        // another responder (Windows) still hears them.
        const QHostAddress dest = dg.destinationAddress();
        const bool unicast = q.authorities.empty() &&
                             (allUnicast || (!dest.isNull() && !dest.isMulticast()));
        // §6: shared answers wait 20–120 ms so responders don't all collide;
        // a truncated query has more known answers coming, so wait for those.
        int delay = shared ? 20 + static_cast<int>(QRandomGenerator::global()->bounded(101)) : 0;
        if (q.flags & dns::kFlagTruncated)
            delay = 400 + static_cast<int>(QRandomGenerator::global()->bounded(101));
        const int ifIndex = iface ? iface->net.index() : -1;
        QTimer::singleShot(delay, this, [this, resp, from, fromPort, unicast, ifIndex] {
            if (unicast) {
                sendTo(resp, from, fromPort);
            } else if (const Iface* i = ifaceByIndex(ifIndex)) {
                multicast(resp, *i);
            } else {
                for (const auto& i : m_ifaces) multicast(resp, i);
            }
        });
    }

    // RFC 6763 §12: with a PTR answer include the instance's SRV and TXT, and
    // with any SRV our address records.
    static void addAdditionals(dns::Message& resp, const std::vector<dns::Record>& records) {
        const auto addMatching = [&](uint16_t type, const dns::Name& name) {
            bool added = false;
            for (const auto& r : records) {
                if (r.type != type || !dns::sameName(r.name, name)) continue;
                added = true;
                if (!containsRecord(resp.answers, r) && !containsRecord(resp.additionals, r))
                    resp.additionals.push_back(r);
            }
            return added;
        };
        std::vector<dns::Name> hosts;
        const std::vector<dns::Record> answers = resp.answers;
        for (const auto& a : answers) {
            if (a.type == dns::kPtr && addMatching(dns::kSrv, a.target)) {
                addMatching(dns::kTxt, a.target);
                for (const auto& r : records)
                    if (r.type == dns::kSrv && dns::sameName(r.name, a.target)) hosts.push_back(r.target);
            }
            if (a.type == dns::kSrv) hosts.push_back(a.target);
        }
        for (const auto& h : hosts) {
            addMatching(dns::kA, h);
            addMatching(dns::kNsec, h);
        }
    }

    static bool isKnownAnswer(const dns::Message& q, const dns::Record& r) {
        // §7.1: skip records the querier already has with at least half our TTL.
        return std::any_of(q.answers.begin(), q.answers.end(), [&](const dns::Record& k) {
            return k.sameAs(r) && k.ttl >= r.ttl / 2;
        });
    }

    // Another host probing for a name we are probing for too (§8.2).
    void resolveSimultaneousProbe(const dns::Message& q, const Iface* iface) {
        const auto check = [&](Group& g, const dns::Name& name,
                               const std::vector<dns::Record>& ours) {
            if (g.announced) return;
            std::vector<dns::Record> theirs;
            for (const auto& r : q.authorities)
                if (dns::sameName(r.name, name)) theirs.push_back(r);
            if (theirs.empty() || compareRecordSets(ours, theirs) >= 0) return;
            g_message("MdnsResponder: another device is probing for '%s' — retrying in 1 s",
                      dns::toString(name).c_str());
            beginProbing(g, 1000);
        };
        check(m_host, hostFqdn(), hostProbeRecords(iface));
        for (auto& s : m_services) check(s.group, instanceName(s), serviceProbeRecords(s));
    }

    // A response from another device using one of our unique names with
    // different data is a conflict (§9).
    void handleResponse(const dns::Message& m, const QNetworkDatagram& dg) {
        if (isOwnAddress(dg.senderAddress())) return;  // our own packets, looped back
        bool hostConflict = false;
        std::vector<int> serviceConflicts;
        for (const auto* section : {&m.answers, &m.additionals}) {
            for (const auto& r : *section) {
                if (r.ttl == 0) continue;
                if (r.type == dns::kA && dns::sameName(r.name, hostFqdn()) && !isOwnARecord(r))
                    hostConflict = true;
                for (const auto& s : m_services) {
                    if (!dns::sameName(r.name, instanceName(s))) continue;
                    if (((r.type == dns::kSrv && !r.sameAs(srv(s))) ||
                         (r.type == dns::kTxt && !r.sameAs(txt(s)))) &&
                        std::find(serviceConflicts.begin(), serviceConflicts.end(), s.id) ==
                            serviceConflicts.end())
                        serviceConflicts.push_back(s.id);
                }
            }
        }
        // Once per name per packet, however many of its records clash.
        if (hostConflict) onConflict(m_host, hostName(), [this] { ++m_hostSuffix; });
        for (const int id : serviceConflicts) {
            Service* s = findService(id);
            onConflict(s->group, instanceLabel(*s), [s] { ++s->suffix; });
        }
    }

    // While probing, a conflict means the name is taken: pick the next one.
    // Once announced, probe again under the same name first (§9).
    void onConflict(Group& g, const std::string& name, const std::function<void()>& nextName) {
        if (g.announced) {
            g_message("MdnsResponder: conflicting answer for '%s' — probing again", name.c_str());
        } else {
            nextName();
            g_message("MdnsResponder: '%s' is taken on this network — trying another name",
                      name.c_str());
        }
        beginProbing(g, 0);
    }

    // ── Interfaces ───────────────────────────────────────────────────────────

    void rescanInterfaces() {
        std::vector<Iface> found;
        for (const QNetworkInterface& net : QNetworkInterface::allInterfaces()) {
            const auto flags = net.flags();
            if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning) ||
                !(flags & QNetworkInterface::CanMulticast) || (flags & QNetworkInterface::IsLoopBack))
                continue;
            Iface i{net, {}};
            for (const auto& e : net.addressEntries())
                if (e.ip().protocol() == QAbstractSocket::IPv4Protocol) i.v4.push_back(e);
            if (!i.v4.empty()) found.push_back(std::move(i));
        }

        bool changed = false;
        for (const auto& i : found) {
            const Iface* old = ifaceByIndex(i.net.index());
            if (!old && !m_socket->joinMulticastGroup(mdnsGroup(), i.net)) {
                g_warning("MdnsResponder: cannot join mDNS group on %s: %s",
                          qPrintable(i.net.humanReadableName()),
                          qPrintable(m_socket->errorString()));
            }
            changed = changed || !old || addresses(*old) != addresses(i);
        }
        for (const auto& old : m_ifaces) {
            const bool gone = std::none_of(found.begin(), found.end(), [&](const Iface& i) {
                return i.net.index() == old.net.index();
            });
            if (gone) {
                m_socket->leaveMulticastGroup(mdnsGroup(), old.net);
                changed = true;
            }
        }
        m_ifaces = std::move(found);
        m_ownAddresses.clear();
        for (const auto& i : m_ifaces)
            for (const auto& e : i.v4) m_ownAddresses.push_back(e.ip().toIPv4Address());

        // New network or address: tell it who we are.
        if (changed && m_host.announced) {
            ++m_host.generation;
            announceHost();
        }
    }

    static std::vector<quint32> addresses(const Iface& i) {
        std::vector<quint32> out;
        for (const auto& e : i.v4) out.push_back(e.ip().toIPv4Address());
        return out;
    }

    const Iface* ifaceByIndex(int index) const {
        for (const auto& i : m_ifaces)
            if (i.net.index() == index) return &i;
        return nullptr;
    }

    // The interface a datagram arrived on: from the packet info when the OS
    // reports it, else the interface whose subnet holds the sender.
    const Iface* ifaceFor(const QNetworkDatagram& dg) const {
        if (dg.interfaceIndex() != 0)
            if (const Iface* i = ifaceByIndex(static_cast<int>(dg.interfaceIndex()))) return i;
        for (const auto& i : m_ifaces)
            for (const auto& e : i.v4)
                if (dg.senderAddress().isInSubnet(e.ip(), e.prefixLength())) return &i;
        return nullptr;
    }

    bool isOwnAddress(const QHostAddress& a) const {
        bool ok = false;
        const quint32 v4 = a.toIPv4Address(&ok);
        return ok && std::find(m_ownAddresses.begin(), m_ownAddresses.end(), v4) != m_ownAddresses.end();
    }

    bool isOwnARecord(const dns::Record& r) const {
        if (r.rdata.size() != 4) return false;
        const quint32 v4 = (quint32(r.rdata[0]) << 24) | (quint32(r.rdata[1]) << 16) |
                           (quint32(r.rdata[2]) << 8) | r.rdata[3];
        return std::find(m_ownAddresses.begin(), m_ownAddresses.end(), v4) != m_ownAddresses.end();
    }

    const Service* findService(int id) const {
        for (const auto& s : m_services)
            if (s.id == id) return &s;
        return nullptr;
    }

    Service* findService(int id) {
        for (auto& s : m_services)
            if (s.id == id) return &s;
        return nullptr;
    }

    const uint16_t       m_port;
    const std::string    m_hostLabel;
    int                  m_hostSuffix = 1;
    Group                m_host;
    std::vector<Service> m_services;
    int                  m_nextId = 1;
    std::vector<Iface>   m_ifaces;
    std::vector<quint32> m_ownAddresses;
    QUdpSocket*          m_socket     = nullptr;
    QTimer*              m_probeTimer = nullptr;
    QElapsedTimer        m_clock;
};

// ── MdnsResponder ────────────────────────────────────────────────────────────

MdnsResponder::MdnsResponder(uint16_t port) {
    m_thread = new QThread;
    m_thread->setObjectName(QStringLiteral("mdns-responder"));
    m_core = new MdnsResponderCore(port, defaultHostLabel());
    m_core->moveToThread(m_thread);
    QObject::connect(m_thread, &QThread::finished, m_core, &QObject::deleteLater);
    m_thread->start();
    QMetaObject::invokeMethod(m_core, [this] { m_available = m_core->start(); },
                              Qt::BlockingQueuedConnection);
}

MdnsResponder::~MdnsResponder() {
    stop();
    m_thread->quit();
    m_thread->wait();
    delete m_thread;
}

bool MdnsResponder::advertise(const std::string& serviceType,
                              const std::string& name,
                              uint16_t port,
                              const std::vector<TxtRecord>& txt) {
    if (!m_available) return false;
    QMetaObject::invokeMethod(m_core, [core = m_core, serviceType, name, port, txt] {
        core->addService(serviceType, name, port, txt);
    }, Qt::QueuedConnection);
    return true;
}

bool MdnsResponder::rename(const std::string& newName) {
    if (!m_available) return false;
    QMetaObject::invokeMethod(m_core, [core = m_core, newName] { core->renameAll(newName); },
                              Qt::QueuedConnection);
    return true;
}

void MdnsResponder::stop() {
    if (!m_available) return;
    // Blocking, so the goodbyes are on the wire before the caller moves on.
    QMetaObject::invokeMethod(m_core, [core = m_core] { core->shutdown(); },
                              Qt::BlockingQueuedConnection);
}

bool MdnsResponder::updateTxtRecord(const std::string& serviceType,
                                    const std::string& key,
                                    const std::string& value) {
    if (!m_available) return false;
    QMetaObject::invokeMethod(m_core, [core = m_core, serviceType, key, value] {
        core->updateTxt(serviceType, key, value);
    }, Qt::QueuedConnection);
    return true;
}

std::string MdnsResponder::hostName() const {
    std::string name;
    QMetaObject::invokeMethod(m_core, [&] { name = m_core->hostName(); },
                              Qt::BlockingQueuedConnection);
    return name;
}

std::vector<std::string> MdnsResponder::instanceNames(const std::string& serviceType) const {
    std::vector<std::string> names;
    QMetaObject::invokeMethod(m_core, [&] { names = m_core->instanceNames(serviceType); },
                              Qt::BlockingQueuedConnection);
    return names;
}

} // namespace airshow
