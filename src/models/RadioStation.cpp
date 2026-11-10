#include "models/RadioStation.h"
#include <QPointer>
#include <limits>
#include <utility>

namespace AetherSDR {
RadioStation::RadioStation(QObject* parent) : QObject(parent) {}
RadioStation::~RadioStation()
{
    for (const auto& connection : std::as_const(m_transmitterConnections)) { disconnect(connection); }
    if (m_transmitter) { m_transmitter->setStationReceiveInterrupted(false); }
}

void RadioStation::watchTransmitter(RadioModel* primary)
{
    if (m_transmitter == primary) { return; }
    for (const auto& connection : std::as_const(m_transmitterConnections)) { disconnect(connection); }
    m_transmitterConnections.clear();
    if (m_transmitter) { m_transmitter->setStationReceiveInterrupted(false); }
    m_transmitter = primary;
    const auto revision = ++m_transmitterRevision;
    if (!primary) { return; } // Losing observation is not proof that TX stopped.
    m_transmitterConnections.append(connect(primary, &RadioModel::radioTransmittingChanged, this,
        [this, revision](bool keyed) {
            if (m_transmitter && revision == m_transmitterRevision && keyed) { setReceiveInterrupted(true); }
        }));
    m_transmitterConnections.append(connect(primary, &RadioModel::radioTransmitConfirmed, this,
        [this, revision](bool keyed) {
            if (m_transmitter && revision == m_transmitterRevision) { setReceiveInterrupted(keyed); }
        }));
    // An optimistic unkey or disconnect reset cannot resume interrupted audio.
    // The next accepted RX readback may be unchanged and must still release it.
    primary->setStationReceiveInterrupted(m_receiveInterrupted);
    if (primary->isRadioTransmitting()) { setReceiveInterrupted(true); }
}

void RadioStation::setReceiveInterrupted(bool interrupted)
{
    if (m_receiveInterrupted == interrupted) { return; }
    m_receiveInterrupted = interrupted;
    const auto revision = ++m_receiveRevision;
    const QPointer<RadioStation> alive(this);
    if (m_transmitter) { m_transmitter->setStationReceiveInterrupted(interrupted); }
    if (!alive || revision != m_receiveRevision) { return; }
    const auto ids = sessionIds();
    for (int id : ids) {
        if (auto* value = session(id)) { value->radioModel().setStationReceiveInterrupted(interrupted); }
        if (!alive || revision != m_receiveRevision) { return; }
    }
    emit receiveInterruptionChanged(m_receiveInterrupted);
}

int RadioStation::addSession(const QString& label)
{
    if (m_nextId == std::numeric_limits<int>::max()) { return -1; }
    const int id = m_nextId++;
    auto value = std::make_unique<RadioSession>();
    // Additional sessions are receivers. Focus and speaker selection cannot
    // confer TX authority, even if a transmit-capable backend is connected.
    value->radioModel().setStationTransmitAllowed(false);
    value->radioModel().setStationReceiveInterrupted(m_receiveInterrupted);
    value->setSessionId(id);
    value->setLabel(label);
    connect(&value->radioModel(), &RadioModel::rxDemodAudioReady, this,
        [this, id](const PcmFrame& frame) {
            const quint64 revision = m_audioRevision;
            if (acceptsAudio(id, revision, frame)) { emit audioFrameReady(id, revision, frame); }
        });
    m_sessions.emplace(id, std::move(value));
    emit sessionAdded(id);
    return id;
}

RadioSession* RadioStation::session(int id) const
{
    const auto found = m_sessions.find(id);
    return found == m_sessions.end() ? nullptr : found->second.get();
}

QList<int> RadioStation::sessionIds() const
{
    QList<int> result;
    for (const auto& [id, value] : m_sessions) { result.append(id); }
    return result;
}

bool RadioStation::selectSession(int id)
{
    if (id != -1 && !session(id)) { return false; }
    if (m_selected == id) { return true; }
    m_selected = id;
    emit selectionChanged(id);
    return true;
}

bool RadioStation::selectAudioSession(int id)
{
    if (id != -1 && !session(id)) { return false; }
    if (m_audio == id) { return true; }
    if (m_audioRevision == std::numeric_limits<quint64>::max()) { return false; }
    m_audio = id;
    ++m_audioRevision;
    emit audioSelectionChanged(id, m_audioRevision);
    return true;
}

bool RadioStation::acceptsAudio(int id, quint64 revision, const PcmFrame& frame) const
{
    return id >= 0 && id == m_audio && revision == m_audioRevision
        && session(id) && frame.current();
}

bool RadioStation::removeSession(int id)
{
    const auto found = m_sessions.find(id);
    if (found == m_sessions.end()) { return false; }
    // Retire membership before any callback or model teardown can reenter.
    auto retired = std::move(found->second);
    m_sessions.erase(found);
    const bool selected = m_selected == id;
    const bool audible = m_audio == id;
    if (selected) { m_selected = -1; }
    if (audible) {
        m_audio = -1;
        if (m_audioRevision != std::numeric_limits<quint64>::max()) { ++m_audioRevision; }
    }
    QPointer<RadioStation> guard(this);
    // A removal can originate in a model signal; let its emitting stack unwind.
    RadioSession* retiring = retired.release();
    retiring->deleteLater();
    retiring->radioModel().disconnectFromRadio();
    if (!guard) { return true; }
    emit sessionRemoved(id);
    if (!guard) { return true; }
    if (selected && m_selected == -1) { emit selectionChanged(-1); }
    if (!guard) { return true; }
    if (audible && m_audio == -1) { emit audioSelectionChanged(-1, m_audioRevision); }
    return true;
}
} // namespace AetherSDR
