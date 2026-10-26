#pragma once

#include "models/RadioSession.h"
#include <QObject>
#include <QPointer>
#include <map>
#include <memory>

namespace AetherSDR {

// Owns independent radio models; selection never replaces another backend.
// This layer opens no audio device and grants no transmit authority.
class RadioStation final : public QObject {
    Q_OBJECT
public:
    explicit RadioStation(QObject* parent = nullptr);
    ~RadioStation() override;
    int addSession(const QString& label);
    bool removeSession(int id);
    RadioSession* session(int id) const;
    QList<int> sessionIds() const;
    bool selectSession(int id);
    int selectedSessionId() const { return m_selected; }
    bool selectAudioSession(int id); // -1 is silent; selection is independent of focus
    int audioSessionId() const { return m_audio; }
    quint64 audioRevision() const { return m_audioRevision; }
    bool acceptsAudio(int id, quint64 revision, const PcmFrame& frame) const;
    void watchTransmitter(RadioModel* primary);
    bool receiveInterrupted() const { return m_receiveInterrupted; }

signals:
    void sessionAdded(int id);
    void sessionRemoved(int id);
    void selectionChanged(int id);
    void audioSelectionChanged(int id, quint64 revision);
    void receiveInterruptionChanged(bool interrupted);
    // A queued consumer must recheck acceptsAudio before feeding its device.
    void audioFrameReady(int id, quint64 revision, const AetherSDR::PcmFrame& frame);

private:
    void setReceiveInterrupted(bool interrupted);
    QPointer<RadioModel> m_transmitter;
    QList<QMetaObject::Connection> m_transmitterConnections;
    quint64 m_transmitterRevision = 0;
    bool m_receiveInterrupted = false;
    quint64 m_receiveRevision = 0;
    std::map<int, std::unique_ptr<RadioSession>> m_sessions;
    int m_nextId = 0;
    int m_selected = -1;
    int m_audio = -1;
    quint64 m_audioRevision = 0;
};

} // namespace AetherSDR
