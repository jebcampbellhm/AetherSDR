#include "gui/StationWorkspace.h"
#include "gui/StationReceiverPanel.h"
#include "gui/ConnectionPanel.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QComboBox>
#include <QCheckBox>
#include <QSignalBlocker>
#include <utility>

namespace AetherSDR {
StationWorkspace::StationWorkspace(RadioModel* primary, QWidget* parent)
    : CanonCanvas(parent), m_primary(primary)
{
    setObjectName(QStringLiteral("stationWorkspace"));
    setCloseButtonVisible(false);
    auto* layout = new QVBoxLayout(bodyWidget());
    auto* header = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add receiver…"), bodyWidget());
    add->setAccessibleName(tr("Add independent receiver"));
    add->setAccessibleDescription(tr("Connect another receiver without disconnecting the primary radio."));
    header->addWidget(add);
    auto* monitorLabel = new QLabel(tr("Listen to:"), bodyWidget());
    m_monitor = new QComboBox(bodyWidget());
    m_monitor->setObjectName(QStringLiteral("stationMonitorSource"));
    m_monitor->setAccessibleName(tr("Radio for PC audio"));
    m_monitor->setAccessibleDescription(tr("Select one radio or Off. PC Audio must also be enabled; selection does not enable it."));
    m_monitor->addItem(tr("Primary radio"), 0);
    m_monitor->addItem(tr("Off"), -1);
    monitorLabel->setBuddy(m_monitor);
    header->addWidget(monitorLabel); header->addWidget(m_monitor);
    connect(m_monitor, &QComboBox::activated, this, [this] {
        selectMonitor(m_monitor->currentData().toInt());
    });
    m_hint = new QLabel(tr("Use your transmit radio as primary and add receivers here. Additional receivers cannot transmit. Listen to changes audio only."), bodyWidget());
    m_hint->setWordWrap(true); header->addWidget(m_hint, 1); layout->addLayout(header);
    m_transmitPrimary = new QCheckBox(tr("Allow transmit on primary radio"), bodyWidget());
    m_transmitPrimary->setObjectName(QStringLiteral("stationTransmitPrimary"));
    m_transmitPrimary->setAccessibleDescription(tr("Explicitly designate the primary radio for transmit. This does not key it. Permission clears on disconnect."));
    layout->addWidget(m_transmitPrimary);
    if (m_primary) {
        m_primary->setStationTransmitAllowed(false);
        connect(m_primary, &RadioModel::connectionStateChanged, this, [this](bool connected) {
            if (!connected && m_primary) { m_primary->setStationTransmitAllowed(false); }
            refreshTransmitPermission();
        });
        connect(m_primary, &RadioModel::capabilitiesChanged, this, [this] { refreshTransmitPermission(); });
    }
    connect(m_transmitPrimary, &QCheckBox::toggled, this, [this](bool allowed) {
        if (m_primary) { m_primary->setStationTransmitAllowed(allowed && m_primary->isConnected()
            && m_primary->backendCapabilities().canTransmit); }
        refreshTransmitPermission();
        emit primaryTransmitPermissionChanged(m_primary && m_primary->stationTransmitAllowed());
    });
    refreshTransmitPermission();
    auto* scroll = new QScrollArea(bodyWidget()); scroll->setWidgetResizable(true);
    auto* content = new QWidget(scroll); m_receivers = new QVBoxLayout(content);
    m_receivers->addStretch(); scroll->setWidget(content); layout->addWidget(scroll, 1);
    connect(add, &QPushButton::clicked, this, &StationWorkspace::showConnect);
    connect(&m_station, &RadioStation::sessionRemoved, this, [this](int id) {
        if (m_monitor->currentData().toInt() == id + 1) { selectMonitor(-1); }
        const QSignalBlocker blocker(m_monitor);
        m_monitor->removeItem(m_monitor->findData(id + 1));
        auto* panel = m_panels.take(id);
        if (panel) { panel->detach(); m_receivers->removeWidget(panel); panel->hide(); panel->deleteLater(); }
    });
    connect(&m_station, &RadioStation::audioFrameReady, this,
        [this](int id, quint64 revision, const PcmFrame& frame) {
            if (m_station.acceptsAudio(id, revision, frame)) { emit monitorFrameReady(id + 1, frame); }
        });
}
void StationWorkspace::refreshTransmitPermission()
{
    const bool available = m_primary && m_primary->isConnected()
        && m_primary->backendCapabilities().canTransmit;
    if (!available && m_primary) { m_primary->setStationTransmitAllowed(false); }
    const QSignalBlocker blocker(m_transmitPrimary);
    m_transmitPrimary->setChecked(available && m_primary->stationTransmitAllowed());
    m_transmitPrimary->setEnabled(available);
    m_transmitPrimary->setToolTip(available
        ? tr("Only the primary radio may transmit. Selecting a receiver for listening does not change this designation.")
        : tr("Connect a transmit-capable primary radio before enabling transmit. Additional radios remain receive-only."));
}
StationWorkspace::~StationWorkspace()
{
    if (m_primary) { m_primary->setStationTransmitAllowed(false); }
    if (m_connection) { delete m_connection.data(); }
    for (auto* panel : std::as_const(m_panels)) { panel->detach(); }
    for (int id : m_station.sessionIds()) { m_station.removeSession(id); }
}
void StationWorkspace::selectMonitor(int source)
{
    if (!m_station.selectAudioSession(source > 0 ? source - 1 : -1)) { return; }
    const QSignalBlocker blocker(m_monitor);
    m_monitor->setCurrentIndex(m_monitor->findData(source));
    emit monitorSelectionChanged(source);
}
void StationWorkspace::showConnect()
{
    if (!m_connection) {
        m_connection = new ConnectionPanel(this);
        m_connection->setWindowTitle(tr("Add independent receiver"));
        m_connection->selectManualConnection();
        connect(m_connection, &ConnectionPanel::connectRequested, this,
            [this](const RadioInfo& info) { addReceiver(info, {}); });
        connect(m_connection, &ConnectionPanel::connectWithOptionsRequested, this,
            &StationWorkspace::addReceiver);
    }
    m_connection->fitAndClampToScreen(screen());
    m_connection->show(); m_connection->raise(); m_connection->activateWindow();
}
void StationWorkspace::addReceiver(const RadioInfo& info, const QVariantMap& options)
{
    if (m_panels.size() >= 2) {
        m_connection->setStatusText(tr("Two additional receivers are already open. Remove one before adding another."));
        return;
    }
    const int id = m_station.addSession(info.model + QStringLiteral(" · ") + info.address.toString());
    auto* session = m_station.session(id);
    if (!session) { return; }
    m_monitor->addItem(session->label(), id + 1);
    auto* panel = new StationReceiverPanel(session, this);
    m_panels.insert(id, panel); m_receivers->insertWidget(m_receivers->count() - 1, panel, 1);
    panel->setMinimumHeight(340);
    connect(panel, &StationReceiverPanel::removeRequested, this, [this](int removeId) { m_station.removeSession(removeId); });
    if (m_connection) { m_connection->hide(); }
    session->radioModel().connectToRadioWithOptions(info, options);
}
}
