#include "TestSettingsProfile.h"
#include "models/RadioStation.h"
#include <QCoreApplication>
#include <QPointer>
#include <QEvent>
#include <cstdio>

using namespace AetherSDR;
int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("radio-station"));
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool value, const char* text) {
        std::printf("[%s] %s\n", value ? "PASS" : "FAIL", text);
        if (!value) { ++failures; }
    };
    RadioStation station;
    const int web = station.addSession(QStringLiteral("Web-888"));
    const int nrsp = station.addSession(QStringLiteral("nRSP-ST"));
    const int icom = station.addSession(QStringLiteral("Icom"));
    check(station.sessionIds().size() == 3, "three independently owned sessions");
    check(&station.session(web)->radioModel() != &station.session(nrsp)->radioModel(),
        "receivers have distinct model ownership");
    check(station.audioSessionId() == -1, "new station is silent");
    station.selectSession(web);
    check(station.audioSessionId() == -1, "focus does not enable audio");
    check(!station.selectSession(900) && station.selectedSessionId() == web,
        "unknown focus leaves valid selection intact");
    PcmProducer producer;
    producer.start();
    const auto frame = producer.produce(QVector<float>{0.1f, 0.1f});
    int deliveries = 0;
    QObject::connect(&station, &RadioStation::audioFrameReady, &app,
        [&](int id, quint64 rev, const PcmFrame& pcm) {
            if (station.acceptsAudio(id, rev, pcm)) { ++deliveries; }
        }, Qt::QueuedConnection);
    station.selectAudioSession(web);
    const auto firstRevision = station.audioRevision();
    emit station.session(web)->radioModel().backendAudioFrameReady(*frame);
    station.selectAudioSession(nrsp);
    station.selectAudioSession(web);
    QCoreApplication::processEvents();
    check(deliveries == 0 && !station.acceptsAudio(web, firstRevision, *frame),
        "queued audio cannot survive switching away and back");
    emit station.session(web)->radioModel().backendAudioFrameReady(*frame);
    emit station.session(nrsp)->radioModel().backendAudioFrameReady(*frame);
    QCoreApplication::processEvents();
    check(deliveries == 1, "only explicitly selected session feeds audio");
    emit station.session(web)->radioModel().backendAudioFrameReady(*frame);
    check(station.removeSession(web), "selected session retires");
    QCoreApplication::processEvents();
    check(deliveries == 1 && station.audioSessionId() == -1
        && station.selectedSessionId() == -1, "removal silences queued audio and clears focus");
    check(station.session(nrsp) && station.session(icom), "other radio models survive removal");
    check(station.addSession(QStringLiteral("replacement")) > icom, "session IDs never reused");
    station.selectAudioSession(nrsp);
    producer.invalidate();
    check(!station.acceptsAudio(nrsp, station.audioRevision(), *frame), "retired PCM epochs refused");
    check(!station.removeSession(web), "repeated removal is harmless");
    QPointer<RadioSession> retiring = station.session(nrsp);
    QObject::connect(&retiring->radioModel(), &RadioModel::connectionError, &app,
        [&](const QString&) { station.removeSession(nrsp); });
    emit retiring->radioModel().connectionError(QStringLiteral("injected failure"));
    check(retiring && !station.session(nrsp), "signal-driven removal defers object destruction");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(retiring.isNull(), "retired model is destroyed after its signal unwinds");
    return failures ? 1 : 0;
}
