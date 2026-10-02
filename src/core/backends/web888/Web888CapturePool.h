#pragma once

#include "Web888CapturePlan.h"
#include <QObject>
#include <QSet>
#include <map>

class QTimer;
namespace AetherSDR {
class Web888IqCapture;
class Web888IqProcessor;

// Owner-thread hardware leases. A profile change acquires all new captures
// before releasing old ones; unchanged windows keep their capture and audio.
// This budget includes our existing leases and reserved listeners. Server
// occupancy remains advisory: a busy reply can still reject a pending lease.
class Web888CapturePool : public QObject {
    Q_OBJECT
public:
    explicit Web888CapturePool(QObject* parent = nullptr);
    ~Web888CapturePool() override;
    bool applyProfile(const QString& endpoint, const QVector<Web888DigitalPreset>& presets,
                      double sampleRateHz, int receiverBudget, int reservedListening = 1,
                      const QString& password = {});
    void stop();
    int activeCount() const { return static_cast<int>(m_active.size()); }
    int pendingCount() const { return static_cast<int>(m_pending.size()); }
    bool changingProfile() const { return m_changing; }
    QVector<quint64> activeIds() const;
signals:
    void profileReady();
    void profileFailed(const QString& reason);
    void captureFailed(quint64 captureId, const QString& reason);
    void decoderAudioReady(const AetherSDR::Web888DecoderAudio& audio);
protected:
    virtual Web888IqCapture* createCapture(Web888IqProcessor* processor);
private:
    struct Entry {
        Web888IqCapture* capture{};
        Web888CaptureWindow window;
        QSet<int> publishedSlices;
        bool ready{};
    };
    static bool sameWindow(const Web888CaptureWindow& a, const Web888CaptureWindow& b);
    void release(Entry& entry);
    void abortPending(const QString& reason);
    void commitIfReady();
    void onFailure(quint64 id, const QString& reason);
    std::map<quint64, Entry> m_active, m_pending;
    QSet<quint64> m_desired;
    QString m_endpoint;
    quint64 m_nextId{1}, m_transaction{};
    bool m_changing{false};
    QTimer* m_deadline{};
    Web888IqProcessor* m_processor{};
};
} // namespace AetherSDR
