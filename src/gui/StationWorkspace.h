#pragma once
#include "gui/CanonSurface.h"
#include "models/RadioStation.h"
#include <QMap>
#include <QPointer>
class QVBoxLayout;
class QLabel;
class QComboBox;
class QCheckBox;
namespace AetherSDR {
class ConnectionPanel;
class StationReceiverPanel;
struct RadioInfo;

// Extra receivers stay separate from the primary radio's operating/TX controls.
class StationWorkspace final : public CanonCanvas {
    Q_OBJECT
public:
    explicit StationWorkspace(RadioModel* primary, QWidget* parent = nullptr);
    ~StationWorkspace() override;
signals:
    void primaryTransmitPermissionChanged(bool allowed);
    void monitorSelectionChanged(int source);
    void monitorAvailabilityChanged(int source, bool available);
    void monitorFrameReady(int source, const AetherSDR::PcmFrame& frame);
private:
    friend struct StationWorkspaceTestAccess;
    void showConnect(int receiverId = -1);
    void addReceiver(const RadioInfo& info, const QVariantMap& options);
    void reconnectReceiver(int id, const RadioInfo& info, const QVariantMap& options);
    void connectReceiver(int id, const RadioInfo& info, const QVariantMap& options);
    void selectMonitor(int source);
    void refreshTransmitPermission();
    QPointer<RadioModel> m_primary;
    QCheckBox* m_transmitPrimary;
    RadioStation m_station;
    QMap<int, StationReceiverPanel*> m_panels;
    QPointer<ConnectionPanel> m_connection;
    int m_connectionSession = -1;
    QVBoxLayout* m_receivers;
    QLabel* m_hint;
    QComboBox* m_monitor;
};
}
