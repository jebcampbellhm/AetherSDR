#pragma once
#include "gui/CanonSurface.h"
#include "models/RadioStation.h"
#include <QMap>
#include <QPointer>
class QVBoxLayout;
class QLabel;
class QComboBox;
namespace AetherSDR {
class ConnectionPanel;
class StationReceiverPanel;
struct RadioInfo;

// Extra receivers stay separate from the primary radio's operating/TX controls.
class StationWorkspace final : public CanonCanvas {
    Q_OBJECT
public:
    explicit StationWorkspace(QWidget* parent = nullptr);
    ~StationWorkspace() override;
signals:
    void monitorSelectionChanged(int source);
    void monitorFrameReady(int source, const AetherSDR::PcmFrame& frame);
private:
    void showConnect();
    void addReceiver(const RadioInfo& info, const QVariantMap& options);
    void selectMonitor(int source);
    RadioStation m_station;
    QMap<int, StationReceiverPanel*> m_panels;
    QPointer<ConnectionPanel> m_connection;
    QVBoxLayout* m_receivers;
    QLabel* m_hint;
    QComboBox* m_monitor;
};
}
