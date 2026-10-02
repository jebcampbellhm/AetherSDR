#include "Web888DecoderProcess.h"
#include "Web888DecoderWav.h"
#include <QDateTime>
#include <QFileInfo>
#include <QSaveFile>
#include <QThread>
#include <QTimeZone>
#include <cmath>

namespace AetherSDR {
Web888DecoderProcess::Web888DecoderProcess(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<Web888DecoderJob>();
    m_process.setParent(this);
    m_deadline.setParent(this);
    m_lifetimePoll.setParent(this);
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_deadline.setSingleShot(true);
    m_lifetimePoll.setInterval(50);
    connect(&m_deadline, &QTimer::timeout, this, [this] { abort(QStringLiteral("Decoder deadline exceeded")); });
    connect(&m_lifetimePoll, &QTimer::timeout, this, [this] {
        if (m_job && !m_job->current()) abort(QStringLiteral("Decoder source or route was revoked"));
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &Web888DecoderProcess::drain);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(QStringLiteral("Decoder could not start: ") + m_process.errorString());
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        drain();
        const QString error = !m_failure.isEmpty() ? m_failure
            : status != QProcess::NormalExit || code != 0 ? QStringLiteral("Decoder exited unsuccessfully (%1)").arg(code)
            : QString();
        finish(error);
    });
}

Web888DecoderProcess::~Web888DecoderProcess()
{
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

bool Web888DecoderProcess::start(const Web888DecoderJob& job, const QString& executable,
                                int timeoutMs, QString* error)
{
    if (error) error->clear();
    auto fail = [&](const QString& reason) { if (error) *error = reason; return false; };
    if (QThread::currentThread() != thread()) return fail(QStringLiteral("Decoder requires its owner thread"));
    if (busy()) return fail(QStringLiteral("Decoder is busy"));
    if (!std::isfinite(job.period.audio.dialHz) || job.period.audio.dialHz <= 0)
        return fail(QStringLiteral("Invalid decoder dial frequency"));
    const QFileInfo tool(executable);
    if (!tool.isAbsolute() || !tool.isFile() || !tool.isExecutable())
        return fail(QStringLiteral("Select an existing absolute decoder executable path"));
    if (timeoutMs < 100 || timeoutMs > 120000) return fail(QStringLiteral("Invalid decoder deadline"));
    const auto wav = web888DecoderWav(job, error);
    if (!wav) return false;
    const auto utc = QDateTime::fromMSecsSinceEpoch(job.period.startUtcTicks / 12, QTimeZone::UTC);
    if (!utc.isValid()) return fail(QStringLiteral("Invalid decoder UTC date"));
    auto directory = std::make_unique<QTemporaryDir>();
    if (!directory->isValid()) return fail(QStringLiteral("Cannot create decoder working directory"));
    const QString name = utc.toString(job.mode == "WSPR" ? "yyMMdd_HHmm'.wav'" : "yyMMdd_HHmmss'.wav'");
    const QString path = directory->filePath(name);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(*wav) != wav->size() || !file.commit())
        return fail(QStringLiteral("Cannot write decoder WAV"));
    if (!job.current()) return fail(QStringLiteral("Decoder job was revoked"));
    QStringList arguments;
    if (job.mode == "WSPR") {
        arguments = {"-H", "-f", QString::number(job.period.audio.dialHz / 1e6, 'f', 6), "-a", directory->path(), path};
    } else {
        arguments = {job.mode == "FT8" ? "-8" : "-5", "-p", job.mode == "FT8" ? "15" : "7.5",
                     "-d", "2", "-w", "0", "-m", "1", "-a", directory->path(), "-t", directory->path(), path};
    }
    m_job = job;
    m_directory = std::move(directory);
    m_output.clear();
    m_failure.clear();
    m_process.setWorkingDirectory(m_directory->path());
    m_deadline.start(timeoutMs);
    m_lifetimePoll.start();
    m_process.start(tool.absoluteFilePath(), arguments, QIODevice::ReadOnly);
    return true;
}

void Web888DecoderProcess::drain()
{
    constexpr qsizetype limit = 65536;
    while (m_process.bytesAvailable() > 0) {
        const QByteArray part = m_process.read(4096);
        if (part.isEmpty()) break;
        if (!m_failure.isEmpty()) continue;
        if (m_output.size() + part.size() > limit) {
            abort(QStringLiteral("Decoder output limit exceeded"));
        } else {
            m_output += part;
        }
    }
}

void Web888DecoderProcess::abort(const QString& reason)
{
    if (!m_job || !m_failure.isEmpty()) return;
    m_failure = reason;
    m_output.clear();
    m_process.kill();
}

void Web888DecoderProcess::cancel()
{
    if (QThread::currentThread() == thread()) abort(QStringLiteral("Decoder cancelled"));
}

void Web888DecoderProcess::finish(const QString& error)
{
    if (!m_job) return;
    m_deadline.stop();
    m_lifetimePoll.stop();
    const auto job = std::move(*m_job);
    const QString failure = !job.current() ? QStringLiteral("Decoder source or route was revoked") : error;
    const QByteArray output = failure.isEmpty() ? std::move(m_output) : QByteArray();
    m_job.reset();
    m_directory.reset();
    m_output.clear();
    m_failure.clear();
    emit completed(job, output, failure);
}
} // namespace AetherSDR
