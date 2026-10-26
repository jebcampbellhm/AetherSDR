#pragma once
#include "gui/CanonSurface.h"
#include "models/RadioStation.h"
#include <QMap>
#include <QPointer>
class QVBoxLayout;
class QLabel;
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
private:
    void showConnect();
    void addReceiver(const RadioInfo& info, const QVariantMap& options);
    RadioStation m_station;
    QMap<int, StationReceiverPanel*> m_panels;
    QPointer<ConnectionPanel> m_connection;
    QVBoxLayout* m_receivers;
    QLabel* m_hint;
};
}
