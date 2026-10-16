#include "DigitalMonitoringModel.h"
#include "RadioModel.h"
#include "core/backends/IRadioBackend.h"
#include <atomic>
namespace AetherSDR {
DigitalMonitoringModel::DigitalMonitoringModel(RadioModel* radio, QObject* parent)
    : QObject(parent), m_radio(radio)
{
    connect(radio, &RadioModel::connectionStateChanged, this, [this](bool) {
        bind();
        emit availabilityChanged();
    });
    bind();
}
bool DigitalMonitoringModel::available() const
{
    return m_backend && m_backend->isConnected()
        && m_radio->backendDeclaresExtension(QStringLiteral("digital-monitor"));
}
void DigitalMonitoringModel::bind()
{
    disconnect(m_resultConnection);
    disconnect(m_errorConnection);
    m_requests.clear();
    m_backend = m_radio->backend();
    if (!m_backend) return;
    m_resultConnection = connect(m_backend, &IRadioBackend::extensionResult, this,
        [this](quint64 id, const QVariant& value) {
            if (!m_requests.contains(id)) return;
            const auto verb = m_requests.take(id);
            emit result(verb, value.toMap());
        });
    m_errorConnection = connect(m_backend, &IRadioBackend::extensionError, this,
        [this](quint64 id, const QString& message) {
            if (!m_requests.remove(id)) return;
            emit error(message);
        });
}
void DigitalMonitoringModel::request(const QString& verb, const QVariantMap& options)
{
    if (m_radio->backend() != m_backend) bind();
    if (!available()) { emit error(tr("Connect a receiver that supports digital monitoring.")); return; }
    // Separate range from other extension consumers; unique across window lifetimes.
    static std::atomic<quint64> next{quint64(1) << 60};
    if (m_requests.size() >= 8) { emit error(tr("Monitoring receiver is not responding.")); return; }
    const auto id = next.fetch_add(1);
    m_requests.insert(id, verb);
    m_radio->invokeBackendExtension(QStringLiteral("digital-monitor"), verb, id, options);
}
}
