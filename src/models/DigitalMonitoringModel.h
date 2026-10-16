#pragma once
#include <QObject>
#include <QVariantMap>
#include <QPointer>
#include <QHash>
namespace AetherSDR {
class RadioModel;
class IRadioBackend;
// Capability-based monitoring controls; no vendor transport or UTC assumptions.
class DigitalMonitoringModel final : public QObject {
    Q_OBJECT
public:
    explicit DigitalMonitoringModel(RadioModel* radio, QObject* parent = nullptr);
    bool available() const;
    void request(const QString& verb, const QVariantMap& options = {});
signals:
    void availabilityChanged();
    void result(const QString& verb, const QVariantMap& value);
    void error(const QString& message);
private:
    void bind();
    RadioModel* m_radio;
    QPointer<IRadioBackend> m_backend;
    QHash<quint64, QString> m_requests;
    QMetaObject::Connection m_resultConnection, m_errorConnection;
};
}
