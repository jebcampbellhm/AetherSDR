#pragma once

#include "Web888IqSplitter.h"
#include <QObject>
#include <memory>

namespace AetherSDR {
// One serial DSP worker shared by a pool. Both directions have bounded queues;
// at most one owner-thread drain notification is posted at a time.
class Web888IqProcessor : public QObject {
    Q_OBJECT
public:
    explicit Web888IqProcessor(QObject* parent = nullptr);
    ~Web888IqProcessor() override;
    enum class Submission { Accepted, Backpressure, Invalid, Inactive };
    struct Stats {
        int sessions{}, queuedInputs{}, queuedResults{}, processingInputs{}, peakInputs{}, peakResults{};
        quint64 droppedInputs{}, droppedResults{}, processedBlocks{}, processingNanoseconds{};
    };
    quint64 open(const QVector<Web888DigitalSlice>& slices, QString* error = nullptr);
    void close(quint64 session);
    Submission submit(quint64 session, const KiwiIqBlock& block);
    Stats stats() const;
signals:
    // Emitted on the processor's owner thread, never the DSP worker.
    void processed(quint64 session, double inputRateHz,
                   const QVector<AetherSDR::Web888DecoderAudio>& audio, const QString& error);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    void drain();
};
} // namespace AetherSDR
