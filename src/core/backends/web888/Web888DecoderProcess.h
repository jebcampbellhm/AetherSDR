#pragma once

#include "Web888DecoderRouter.h"
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

namespace AetherSDR {

// Owner-thread, one child and one retained job. Busy callers leave newer jobs
// in the bounded router. Executables are explicitly configured local tools.
class Web888DecoderProcess : public QObject {
    Q_OBJECT
public:
    explicit Web888DecoderProcess(QObject* parent = nullptr);
    ~Web888DecoderProcess() override;
    bool start(const Web888DecoderJob& job, const QString& executable,
               int timeoutMs = 30000, QString* error = nullptr);
    bool busy() const { return m_job.has_value(); }
    void cancel();

signals:
    // Output is empty on failure/cancellation. Consumers must still check
    // job.current() at use time; a later source revocation invalidates it.
    void completed(const AetherSDR::Web888DecoderJob& job,
                   const QByteArray& output, const QString& error);

private:
    void drain();
    void abort(const QString& reason);
    void finish(const QString& error);
    QProcess m_process;
    QTimer m_deadline, m_lifetimePoll;
    std::unique_ptr<QTemporaryDir> m_directory;
    std::optional<Web888DecoderJob> m_job;
    QByteArray m_output;
    QString m_failure;
};
} // namespace AetherSDR

Q_DECLARE_METATYPE(AetherSDR::Web888DecoderJob)
