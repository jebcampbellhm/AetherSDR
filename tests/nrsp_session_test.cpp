#include "core/backends/nrsp/NrspSession.h"
#include "core/backends/nrsp/NrspAuthentication.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <memory>
#include <utility>

using namespace AetherSDR::nrsp;

namespace {
bool check(bool value, const char* message)
{
    if (!value) {
        std::fprintf(stderr, "nrsp_session_test: %s\n", message);
    }
    return value;
}

struct Evidence {
    QByteArray hello;
    QByteArray challenge;
    QByteArray response;
    QByteArray iq;
    QByteArray display;
    SessionKey key;
    QByteArray source;
};

Evidence evidence(const QJsonObject& json)
{
    Evidence result;
    const QJsonObject session = json.value("sessions").toObject().value("direct").toObject();
    result.hello = QByteArray::fromHex(session.value("hello_wire_hex").toString().toLatin1());
    const QByteArray payload = QByteArray::fromHex(session.value("hello_decoded_hex").toString().toLatin1());
    result.key = parseHello(payload)->session;
    result.source = payload.mid(8, 32) + QByteArray(32, '\0');
    for (const QJsonValue& value : json.value("examples").toArray()) {
        const QJsonObject example = value.toObject();
        if (example.value("session").toString() != "direct") {
            continue;
        }
        const QByteArray wire = QByteArray::fromHex(example.value("wire_hex").toString().toLatin1());
        switch (example.value("expected_type").toInt()) {
        case 51:
            result.challenge = QByteArray::fromHex(example.value("expected_payload_hex").toString().toLatin1());
            break;
        case 52: result.response = wire; break;
        case 217: result.iq = wire; break;
        case 212: result.display = wire; break;
        default: break;
        }
    }
    return result;
}

QByteArray wire(const Evidence& e, quint8 type, const QByteArray& payload)
{
    return *encodeFrame(type, payload, e.key);
}

bool lifecycle(const Evidence& e)
{
    NrspSession session(nullptr, [&e] { return e.challenge; });
    QList<Frame> sent;
    QString error;
    int ready = 0;
    int sourceUpdates = 0;
    int iqFrames = 0;
    QObject::connect(&session, &NrspSession::sendBytes, &session, [&](const QByteArray& bytes) {
        FrameDecoder decoder;
        decoder.setSession(e.key);
        Frame frame;
        if (decoder.append(bytes) && decoder.next(frame) == FrameDecoder::Result::Ready) {
            sent.append(frame);
        }
    });
    QObject::connect(&session, &NrspSession::ready, &session, [&] { ++ready; });
    QObject::connect(&session, &NrspSession::sourceChanged, &session, [&] { ++sourceUpdates; });
    QObject::connect(&session, &NrspSession::failed, &session, [&](const QString& message) { error = message; });
    QObject::connect(&session, &NrspSession::receiverData, &session,
        [&](quint8 type, const ReceiverData&) { iqFrames += type != 212; });

    session.begin(0);
    if (!check(!session.createReceiver(ReceiverConfig{}, 0) && sent.isEmpty(), "no commands before verification")) {
        return false;
    }
    session.receive(e.hello, 10);
    if (!check(sent.size() == 1 && sent.first().type == 51
               && sent.first().payload == e.challenge && !session.isReady(), "hello initiates challenge only")) {
        return false;
    }
    session.receive(e.response, 20);
    if (!check(session.isReady() && ready == 1 && error.isEmpty(), "captured verification makes session ready")) {
        return false;
    }
    sent.clear();
    const ReceiverConfig receiver{1, 910550000, 48000, 1024};
    if (!check(session.createReceiver(receiver, 30), "receiver creation")) {
        return false;
    }
    QList<int> types;
    for (const Frame& frame : std::as_const(sent)) {
        types.append(frame.type);
    }
    if (!check(types == QList<int>{54,55,56,36,31,37,38,30,32}
               && readU32(sent[1].payload, 4) == 5 && readU64(sent.last().payload, 4) == receiver.frequencyHz,
               "48k creation decimation and tune-after-enable order")) {
        return false;
    }
    if (!check(!session.createReceiver(receiver, 40) && !session.tuneReceiver(99, 1000000), "receiver identity admission")) {
        return false;
    }
    session.receive(e.iq, 1000);
    if (!check(iqFrames == 1, "IQ reaches active receiver")) {
        return false;
    }
    const int beforeReadback = sourceUpdates;
    if (!check(session.setSourceFrequency(100000000)
               && session.hello()->source.frequencyHz == 910550000 && sourceUpdates == beforeReadback,
               "source request does not fabricate radio readback")) {
        return false;
    }
    QByteArray state = e.source;
    state.replace(0, 8, u64(100000000));
    session.receive(wire(e, 202, state), 1100);
    if (!check(session.hello()->source.frequencyHz == 100000000 && session.setAntenna(2), "antenna C accepts low source frequency")) {
        return false;
    }
    state.replace(24, 4, u32(2));
    session.receive(wire(e, 202, state), 1200);
    if (!check(!session.setSourceFrequency(910550000), "antenna C high-frequency refusal")) {
        return false;
    }
    SourceState observer = session.hello()->source;
    observer.frequencyHz = 14100000;
    observer.antenna = 0;
    observer.owner = false;
    session.observeSharedSource(observer);
    if (!check(session.hello()->source.frequencyHz == 14100000 && session.hello()->source.owner
               && session.hello()->source.antenna == 0,
               "fresh hello updates shared source without adopting observer ownership")) { return false; }
    state.replace(28, 4, u32(0));
    session.receive(wire(e, 202, state), 1300);
    observer.owner = true;
    session.observeSharedSource(observer);
    if (!check(!session.hello()->source.owner, "observer cannot promote a non-owning session")) { return false; }
    sent.clear();
    if (!check(!session.setAntenna(0) && !session.setRfAgc(false) && !session.setRfNotch(true)
               && !session.setDabNotch(true) && !session.setSourceFrequency(100100000)
               && !session.setDisplayCadence(12) && sent.isEmpty(), "ownership loss withdraws all shared controls")) {
        return false;
    }
    if (!check(session.tuneReceiver(1, 100010000), "non-owner retains private receiver tuning")) {
        return false;
    }
    // Waterfall and ping traffic must not conceal a dead IQ stream.
    session.receive(e.display + wire(e, 207, u64(5900)), 5900);
    session.tick(6000);
    if (!check(!session.isActive() && error.contains("IQ stopped"), "IQ-only deadline")) {
        return false;
    }
    const int retiredFrames = iqFrames;
    const qsizetype retiredWrites = sent.size();
    session.receive(e.iq + e.response, 7000);
    session.tick(8000);
    return check(iqFrames == retiredFrames && sent.size() == retiredWrites, "retired session ignores stale input");
}

bool deadlines(const Evidence& e)
{
    NrspSession session(nullptr, [&e] { return e.challenge; });
    QString error;
    int ready = 0;
    int challenges = 0;
    QObject::connect(&session, &NrspSession::failed, &session, [&](const QString& message) { error = message; });
    QObject::connect(&session, &NrspSession::ready, &session, [&] { ++ready; });
    QObject::connect(&session, &NrspSession::sendBytes, &session, [&](const QByteArray& bytes) {
        FrameDecoder decoder;
        decoder.setSession(e.key);
        decoder.append(bytes);
        Frame frame;
        if (decoder.next(frame) == FrameDecoder::Result::Ready && frame.type == 51) {
            ++challenges;
        }
    });
    session.begin(0);
    session.tick(5000);
    if (!check(!session.isActive() && error.contains("hello"), "hello deadline")) {
        return false;
    }
    session.begin(10000);
    session.receive(e.hello, 10001);
    session.tick(15001);
    if (!check(!session.isActive() && error.contains("verifying"), "initial verification deadline")) {
        return false;
    }
    session.begin(20000);
    session.receive(e.hello + e.response, 20010);
    session.tick(320009);
    if (!check(challenges == 2 && ready == 1, "no early renewal")) {
        return false;
    }
    session.tick(320010);
    if (!check(challenges == 3 && session.isReady(), "verification renews at 300 seconds")) {
        return false;
    }
    session.receive(e.response, 320020);
    if (!check(ready == 1, "renewal does not duplicate ready")) {
        return false;
    }
    session.tick(620020);
    session.tick(625020);
    if (!check(!session.isActive() && error.contains("verifying"), "renewal deadline")) {
        return false;
    }
    session.begin(700000);
    session.receive(e.hello + e.response, 700010);
    session.receive(wire(e, 222, u32(1)), 700020);
    return check(!session.isActive() && error.contains("expired"), "observed expiry terminates session");
}

bool synchronousRetirement(const Evidence& e)
{
    NrspSession session(nullptr, [&e] { return e.challenge; });
    session.begin(0);
    session.receive(e.hello + e.response, 10);
    QObject::connect(&session, &NrspSession::sendBytes, &session, [&] { session.stop(); });
    if (!check(!session.setSourceFrequency(14100000) && !session.isActive(),
               "a transport failure during a write must refuse the command")) { return false; }

    auto destroyed = std::make_unique<NrspSession>(nullptr, [&e] { return e.challenge; });
    destroyed->begin(0);
    destroyed->receive(e.hello + e.response, 10);
    QObject::connect(destroyed.get(), &NrspSession::sendBytes, QCoreApplication::instance(),
                     [&] { destroyed.reset(); });
    const bool accepted = destroyed->setSourceFrequency(14100000);
    return check(!accepted && !destroyed, "session can be destroyed synchronously by its transport");
}

QByteArray loginHello(const Evidence& e)
{
    FrameDecoder decoder;
    Frame hello;
    decoder.append(e.hello);
    decoder.next(hello);
    hello.payload.replace(0x5c, 4, u32(*readU32(hello.payload, 0x5c) | 0x80u));
    hello.payload.replace(0x24, 4, u32(*readU32(hello.payload, 0x24) & ~1u));
    hello.payload.replace(0x6c, 64, QByteArray("0123456789abcdef").repeated(4));
    return *encodeFrame(201, hello.payload, SessionKey{});
}

bool accountLogin(const Evidence& e)
{
    const QByteArray nonce = QByteArray("0123456789abcdef").repeated(4);
    const auto response = makeLoginResponse(nonce, QStringLiteral("listener"), QStringLiteral("unit-test-only"));
    // Independent hashlib vector; these are dummy credentials, not a capture.
    const QByteArray expected = QByteArray("listener") + QByteArray(8, '\0')
        + "6b8fc1b45bad801abfb2b537b9883ce9d4371e9e775a93f8b9c6da13102a3d79";
    if (!check(response && *response == expected, "nonce-bound account login vector")) { return false; }
    if (!check(!makeLoginResponse(QByteArray(64, 'z'), "user", "password")
               && !makeLoginResponse(nonce, QString(17, 'u'), "password")
               && !makeLoginResponse(nonce, QString::fromUtf8("\xc3\xa9").repeated(9), "password")
               && !makeLoginResponse(nonce, QString("a") + QChar(0) + "b", "password")
               && !makeLoginResponse(nonce, "user", {}), "login input boundaries")) { return false; }

    NrspSession session(nullptr, [&e] { return e.challenge; });
    QList<Frame> sent;
    QString error;
    int ready = 0;
    int iq = 0;
    QObject::connect(&session, &NrspSession::sendBytes, &session, [&](const QByteArray& bytes) {
        FrameDecoder decoder;
        decoder.setSession(e.key);
        Frame frame;
        decoder.append(bytes);
        if (decoder.next(frame) == FrameDecoder::Result::Ready) { sent.append(frame); }
    });
    QObject::connect(&session, &NrspSession::failed, &session, [&](const QString& reason) { error = reason; });
    QObject::connect(&session, &NrspSession::ready, &session, [&] { ++ready; });
    QObject::connect(&session, &NrspSession::receiverData, &session,
                     [&](quint8 type, const ReceiverData&) { iq += type != 212; });
    session.begin(0, "listener", "unit-test-only");
    session.receive(loginHello(e), 10);
    if (!check(sent.size() == 1 && sent.first().type == 58 && sent.first().payload == expected
               && !session.isReady() && !session.createReceiver({}, 10), "login precedes verification and reception")) {
        return false;
    }
    session.receive(wire(e, 221, u32(1)), 20);
    if (!check(sent.size() == 2 && sent.last().type == 51 && !session.isReady()
               && !session.hello()->source.owner, "login acceptance does not grant control or bypass verification")) {
        return false;
    }
    session.receive(e.response, 30);
    if (!check(ready == 1 && session.isReady() && session.createReceiver({1, 910550000, 48000, 1024}, 30),
               "authenticated listener can receive")) { return false; }
    sent.clear();
    if (!check(!session.setAntenna(1) && !session.setSourceFrequency(14100000)
               && !session.setRfAgc(false) && !session.setRfNotch(true) && !session.setDabNotch(true)
               && !session.setDisplayCadence(24) && sent.isEmpty(), "listener emits no shared controls")) {
        return false;
    }
    if (!check(session.tuneReceiver(1, 910600000) && sent.last().type == 32,
               "listener keeps independent slice tuning")) { return false; }
    session.receive(e.iq + e.display, 40);
    if (!check(iq == 1 && session.isReady() && error.isEmpty(), "no control is a receiving session, not a login error")) {
        return false;
    }
    session.stop();
    sent.clear();
    session.begin(50);
    session.receive(loginHello(e), 60);
    if (!check(!session.isActive() && error.contains("requires a username") && sent.isEmpty(),
               "credentials are not reused on another connection")) { return false; }
    for (const QByteArray& payload : {u32(0), u32(9), QByteArray(2, '\0'), u32(1) + u32(0)}) {
        error.clear(); sent.clear();
        session.begin(100, "listener", "unit-test-only");
        session.receive(loginHello(e), 110);
        session.receive(wire(e, 221, payload), 120);
        if (!check(!session.isActive() && !error.isEmpty() && sent.size() == 1,
                   "rejected, unknown, or malformed login must not create a receiver")) { return false; }
    }
    error.clear();
    session.begin(200, "listener", "unit-test-only");
    session.receive(loginHello(e), 210);
    session.tick(5209);
    if (!check(session.isActive(), "login deadline is not early")) { return false; }
    session.tick(5210);
    return check(!session.isActive() && error.contains("login response"), "bounded login response wait");
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        return 1;
    }
    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly)) {
        return 1;
    }
    const Evidence e = evidence(QJsonDocument::fromJson(file.readAll()).object());
    if (!check(!e.hello.isEmpty() && !e.challenge.isEmpty() && !e.response.isEmpty()
               && !e.iq.isEmpty() && !e.display.isEmpty(), "complete capture evidence")) {
        return 1;
    }
    return lifecycle(e) && deadlines(e) && synchronousRetirement(e) && accountLogin(e) ? 0 : 1;
}
