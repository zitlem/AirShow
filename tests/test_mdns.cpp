// AirShow's built-in mDNS responder: DNS wire format and query answering.
// The responder runs on a random high port and is queried over loopback the
// way legacy (non-5353) DNS-SD clients do, so no multicast routing is needed.

#include <gtest/gtest.h>
#include "discovery/DnsMessage.h"
#include "discovery/MdnsResponder.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QNetworkDatagram>
#include <QRandomGenerator>
#include <QThread>
#include <QUdpSocket>

#include <algorithm>
#include <optional>

using namespace airshow;

namespace {

const dns::Record* find(const std::vector<dns::Record>& records, uint16_t type) {
    for (const auto& r : records)
        if (r.type == type) return &r;
    return nullptr;
}

std::string txtString(const dns::Record& r) {
    return std::string(r.rdata.begin(), r.rdata.end());
}

uint16_t randomPort() {
    return static_cast<uint16_t>(20000 + QRandomGenerator::global()->bounded(20000));
}

// Sends one query from an ephemeral port and returns the first reply, if any.
std::optional<dns::Message> ask(uint16_t port, const dns::Message& q, int timeoutMs) {
    QUdpSocket socket;
    if (!socket.bind(QHostAddress::LocalHost, 0)) return std::nullopt;
    const auto bytes = dns::serialize(q);
    socket.writeDatagram(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<qint64>(bytes.size()), QHostAddress::LocalHost, port);
    if (!socket.waitForReadyRead(timeoutMs)) return std::nullopt;
    const QByteArray data = socket.receiveDatagram().data();
    return dns::parse(reinterpret_cast<const uint8_t*>(data.constData()),
                      static_cast<size_t>(data.size()));
}

dns::Message question(const dns::Name& name, uint16_t type, uint16_t id = 0x1234) {
    dns::Message q;
    q.id = id;
    q.questions.push_back({name, type, dns::kClassIn, false});
    return q;
}

// Probing takes ~1 s; retry until the responder starts answering.
std::optional<dns::Message> askUntilAnswered(uint16_t port, const dns::Message& q) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 5000) {
        auto reply = ask(port, q, 250);
        if (reply && !reply->answers.empty()) return reply;
    }
    return std::nullopt;
}

void sendResponse(uint16_t port, const dns::Message& m) {
    QUdpSocket socket;
    socket.bind(QHostAddress::LocalHost, 0);
    const auto bytes = dns::serialize(m);
    socket.writeDatagram(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<qint64>(bytes.size()), QHostAddress::LocalHost, port);
}

const dns::Name kTestType = dns::parseName("_airshowtest._tcp.local");

} // namespace

// ── Wire format ──────────────────────────────────────────────────────────────

TEST(DnsMessageTest, RoundTripsAndCompressesNames) {
    const dns::Name instance = {"Living Room.Main"};  // dot inside a label
    const dns::Name full = {"Living Room.Main", "_airplay", "_tcp", "local"};
    const dns::Name host = {"pc-AirShow", "local"};

    dns::Message m;
    m.id    = 7;
    m.flags = dns::kFlagResponse | dns::kFlagAuthoritative;
    m.answers.push_back(dns::makePtr(dns::parseName("_airplay._tcp.local"), full, 4500));
    dns::Record srv = dns::makeSrv(full, 7000, host, 120);
    srv.cacheFlush = true;
    m.additionals.push_back(srv);
    m.additionals.push_back(dns::makeTxt(full, {"deviceid=AA:BB", "rs="}, 4500));
    m.additionals.push_back(dns::makeA(host, 0xC0A80105, 120));

    const auto bytes = dns::serialize(m);
    const auto parsed = dns::parse(bytes.data(), bytes.size());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->id, 7);
    EXPECT_TRUE(parsed->isResponse());
    ASSERT_EQ(parsed->answers.size(), 1u);
    ASSERT_EQ(parsed->additionals.size(), 3u);

    EXPECT_TRUE(dns::sameName(parsed->answers[0].target, full));
    EXPECT_EQ(parsed->answers[0].target[0], instance[0]);  // label kept whole
    const dns::Record& s = parsed->additionals[0];
    EXPECT_EQ(s.port, 7000);
    EXPECT_TRUE(s.cacheFlush);
    EXPECT_TRUE(dns::sameName(s.target, host));
    EXPECT_TRUE(s.sameAs(srv));
    EXPECT_EQ(txtString(parsed->additionals[1]), std::string("\x0e" "deviceid=AA:BB" "\x03" "rs="));
    EXPECT_EQ(parsed->additionals[2].rdata, (std::vector<uint8_t>{192, 168, 1, 5}));

    // The instance name is written once and pointed at afterwards.
    const std::string raw(bytes.begin(), bytes.end());
    EXPECT_EQ(raw.find("Living Room.Main"), raw.rfind("Living Room.Main"));
}

TEST(DnsMessageTest, NamesCompareCaseInsensitively) {
    EXPECT_TRUE(dns::sameName(dns::parseName("_AirPlay._TCP.local"),
                              dns::parseName("_airplay._tcp.LOCAL")));
    EXPECT_FALSE(dns::sameName(dns::parseName("a.local"), dns::parseName("b.local")));
}

TEST(DnsMessageTest, NsecListsOnlyGivenTypes) {
    const dns::Record nsec = dns::makeNsec({"h", "local"}, {dns::kA}, 120);
    // next name "h.local", window 0, 1 byte, bit for type 1.
    const std::vector<uint8_t> expected = {1, 'h', 5, 'l', 'o', 'c', 'a', 'l', 0, 0, 1, 0x40};
    EXPECT_EQ(nsec.rdata, expected);
}

TEST(DnsMessageTest, RejectsMalformedPackets) {
    // Header claims one question but the packet ends.
    const uint8_t truncated[] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    EXPECT_FALSE(dns::parse(truncated, sizeof truncated).has_value());

    // A compression pointer pointing at itself.
    const uint8_t loop[] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0xC0, 12, 0, 1, 0, 1};
    EXPECT_FALSE(dns::parse(loop, sizeof loop).has_value());

    // Label length running past the end.
    const uint8_t overrun[] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 40, 'a', 'b'};
    EXPECT_FALSE(dns::parse(overrun, sizeof overrun).has_value());

    EXPECT_FALSE(dns::parse(nullptr, 0).has_value());
}

TEST(DnsMessageTest, SurvivesRandomInput) {
    auto* rng = QRandomGenerator::global();
    std::vector<uint8_t> buf;
    for (int i = 0; i < 20000; ++i) {
        buf.resize(rng->bounded(200));
        for (auto& b : buf) b = static_cast<uint8_t>(rng->bounded(256));
        if (buf.size() >= 12) buf[5] = static_cast<uint8_t>(rng->bounded(4));  // few questions
        (void)dns::parse(buf.data(), buf.size());
    }
    SUCCEED();
}

// ── Responder ────────────────────────────────────────────────────────────────

class MdnsResponderTest : public ::testing::Test {
protected:
    void SetUp() override {
        port = randomPort();
        responder = std::make_unique<MdnsResponder>(port);
        if (!responder->isAvailable()) GTEST_SKIP() << "cannot bind UDP port " << port;
    }

    uint16_t port = 0;
    std::unique_ptr<MdnsResponder> responder;
};

TEST_F(MdnsResponderTest, AnswersBrowseWithServiceDetails) {
    ASSERT_TRUE(responder->advertise("_airshowtest._tcp", "Test Receiver", 7400,
                                     {{"ver", "1"}, {"fn", "Test Receiver"}}));

    const auto reply = askUntilAnswered(port, question(kTestType, dns::kPtr, 0x4242));
    ASSERT_TRUE(reply.has_value()) << "no answer to the PTR query";

    // Legacy unicast reply: our ID and question echoed, TTLs capped at 10 s.
    EXPECT_EQ(reply->id, 0x4242);
    ASSERT_EQ(reply->questions.size(), 1u);
    const dns::Record* ptr = find(reply->answers, dns::kPtr);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->target.front(), "Test Receiver");
    EXPECT_LE(ptr->ttl, 10u);

    // The SRV and TXT come along so the client can connect without asking again.
    const dns::Record* srv = find(reply->additionals, dns::kSrv);
    ASSERT_NE(srv, nullptr);
    EXPECT_EQ(srv->port, 7400);
    EXPECT_EQ(dns::toString(srv->target), responder->hostName());
    EXPECT_FALSE(srv->cacheFlush);
    const dns::Record* txt = find(reply->additionals, dns::kTxt);
    ASSERT_NE(txt, nullptr);
    EXPECT_NE(txtString(*txt).find("fn=Test Receiver"), std::string::npos);
    EXPECT_NE(find(reply->additionals, dns::kNsec), nullptr);

    // The service type is listed for "what services are there?" browsers.
    const auto types = ask(port, question(dns::parseName("_services._dns-sd._udp.local"), dns::kPtr), 1000);
    ASSERT_TRUE(types.has_value());
    const dns::Record* typePtr = find(types->answers, dns::kPtr);
    ASSERT_NE(typePtr, nullptr);
    EXPECT_TRUE(dns::sameName(typePtr->target, kTestType));
}

TEST_F(MdnsResponderTest, SkipsAnswersTheQuerierAlreadyKnows) {
    responder->advertise("_airshowtest._tcp", "Known", 7400, {});
    const auto first = askUntilAnswered(port, question(kTestType, dns::kPtr));
    ASSERT_TRUE(first.has_value());

    dns::Message q = question(kTestType, dns::kPtr);
    dns::Record known = *find(first->answers, dns::kPtr);
    known.ttl = 4500;
    q.answers.push_back(known);
    EXPECT_FALSE(ask(port, q, 500).has_value());
}

TEST_F(MdnsResponderTest, AnswersAaaaWithNsec) {
    responder->advertise("_airshowtest._tcp", "Nsec", 7400, {});
    ASSERT_TRUE(askUntilAnswered(port, question(kTestType, dns::kPtr)).has_value());

    const dns::Name host = dns::parseName(responder->hostName());
    const auto reply = ask(port, question(host, dns::kAaaa), 1000);
    ASSERT_TRUE(reply.has_value());
    const dns::Record* nsec = find(reply->answers, dns::kNsec);
    ASSERT_NE(nsec, nullptr);
    EXPECT_TRUE(dns::sameName(nsec->name, host));
    EXPECT_EQ(find(reply->answers, dns::kAaaa), nullptr);
}

TEST_F(MdnsResponderTest, ServesUpdatedTxtRecord) {
    responder->advertise("_airshowtest._tcp", "Txt", 7000, {{"pk", "0000"}});
    ASSERT_TRUE(askUntilAnswered(port, question(kTestType, dns::kPtr)).has_value());

    responder->updateTxtRecord("_airshowtest._tcp", "pk", "abcd");
    const dns::Name instance = {"Txt", "_airshowtest", "_tcp", "local"};
    QElapsedTimer t;
    t.start();
    std::string txt;
    while (t.elapsed() < 2000 && txt.find("pk=abcd") == std::string::npos) {
        const auto reply = ask(port, question(instance, dns::kTxt), 250);
        if (reply && find(reply->answers, dns::kTxt)) txt = txtString(*find(reply->answers, dns::kTxt));
    }
    EXPECT_NE(txt.find("pk=abcd"), std::string::npos) << "TXT was: " << txt;
    EXPECT_EQ(txt.find("pk=0000"), std::string::npos);
}

TEST_F(MdnsResponderTest, RenamesWhenNameIsTakenWhileProbing) {
    responder->advertise("_airshowtest._tcp", "Taken", 7400, {});
    QThread::msleep(50);

    // Another device already answers for "Taken" with a different SRV.
    const dns::Name taken = {"Taken", "_airshowtest", "_tcp", "local"};
    dns::Message other;
    other.flags = dns::kFlagResponse | dns::kFlagAuthoritative;
    dns::Record srv = dns::makeSrv(taken, 9999, {"elsewhere", "local"}, 120);
    srv.cacheFlush = true;
    other.answers.push_back(srv);
    sendResponse(port, other);

    const auto reply = askUntilAnswered(port, question(kTestType, dns::kPtr));
    ASSERT_TRUE(reply.has_value());
    const dns::Record* ptr = find(reply->answers, dns::kPtr);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->target.front(), "Taken (2)");
    EXPECT_EQ(responder->instanceNames("_airshowtest._tcp"),
              std::vector<std::string>{"Taken (2)"});
}

TEST_F(MdnsResponderTest, StopWithdrawsServices) {
    responder->advertise("_airshowtest._tcp", "Gone", 7400, {});
    ASSERT_TRUE(askUntilAnswered(port, question(kTestType, dns::kPtr)).has_value());
    responder->stop();
    EXPECT_FALSE(ask(port, question(kTestType, dns::kPtr), 500).has_value());
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
