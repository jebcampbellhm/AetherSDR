#include "gui/StationReceiverPanel.h"
#include "gui/SpectrumWidget.h"
#include "gui/SpectrumOverlayMenu.h"
#include "models/RadioSession.h"
#include "models/PanadapterModel.h"
#include "models/SliceModel.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <array>
#include <utility>

namespace AetherSDR {
StationReceiverPanel::StationReceiverPanel(RadioSession* session, QWidget* parent)
    : CanonCanvas(parent), m_sessionId(session->sessionId()), m_model(&session->radioModel())
{
    setObjectName(QStringLiteral("stationReceiver_%1").arg(m_sessionId));
    auto* layout = new QVBoxLayout(bodyWidget());
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(session->label(), bodyWidget());
    title->setTextFormat(Qt::PlainText);
    header->addWidget(title);
    m_status = new QLabel(tr("Connecting"), bodyWidget());
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    header->addWidget(m_status, 1);
    placeCloseButtonIn(header);
    layout->addLayout(header);
    connect(this, &CanonSurface::closeRequested, this, [this] { emit removeRequested(m_sessionId); });
    auto* controls = new QHBoxLayout;
    m_slice = new QComboBox(bodyWidget());
    m_slice->setAccessibleName(tr("Receive slice"));
    m_frequency = new QDoubleSpinBox(bodyWidget());
    m_frequency->setRange(0.0, 6000.0); m_frequency->setDecimals(6);
    m_frequency->setSuffix(tr(" MHz")); m_frequency->setSingleStep(0.001);
    m_frequency->setKeyboardTracking(false);
    m_frequency->setAccessibleName(tr("Receiver frequency"));
    m_mode = new QComboBox(bodyWidget()); m_mode->setAccessibleName(tr("Receive mode"));
    m_low = new QSpinBox(bodyWidget()); m_high = new QSpinBox(bodyWidget());
    for (auto* spin : {m_low, m_high}) {
        spin->setRange(-24000, 24000); spin->setSuffix(tr(" Hz")); spin->setKeyboardTracking(false);
    }
    m_low->setAccessibleName(tr("Low filter edge")); m_high->setAccessibleName(tr("High filter edge"));
    controls->addWidget(m_slice); controls->addWidget(m_frequency);
    controls->addWidget(m_mode); controls->addWidget(m_low); controls->addWidget(m_high);
    auto* add = m_addSlice = new QPushButton(tr("Add slice"), bodyWidget());
    auto* remove = m_removeSlice = new QPushButton(tr("Remove slice"), bodyWidget());
    add->setAccessibleName(tr("Add receive slice")); remove->setAccessibleName(tr("Remove receive slice"));
    controls->addWidget(add); controls->addWidget(remove);
    m_addPan = new QPushButton(tr("Add waterfall"), bodyWidget());
    m_addPan->setAccessibleName(tr("Add receiver waterfall"));
    controls->addWidget(m_addPan);
    connect(m_addPan, &QPushButton::clicked, this, [this] {
        if (m_model && !m_model->createPanadapter()) { m_status->setText(tr("Additional waterfall unavailable on this receiver.")); }
    });
    layout->addLayout(controls);
    m_pansLayout = new QVBoxLayout; layout->addLayout(m_pansLayout, 1);
    connect(m_slice, &QComboBox::activated, this, [this] {
        if (auto* slice = selectedSlice()) { slice->setActive(true); }
        refreshSlices();
    });
    connect(m_frequency, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (auto* slice = selectedSlice()) { slice->setFrequency(value); }
    });
    connect(m_mode, &QComboBox::textActivated, this, [this](const QString& mode) {
        if (auto* slice = selectedSlice()) { slice->setMode(mode); }
    });
    const auto filter = [this] {
        if (auto* slice = selectedSlice(); slice && m_low->value() < m_high->value()) {
            slice->setFilterWidth(m_low->value(), m_high->value());
        }
    };
    connect(m_low, &QSpinBox::editingFinished, this, filter);
    connect(m_high, &QSpinBox::editingFinished, this, filter);
    connect(add, &QPushButton::clicked, this, [this] {
        if (!m_model) { return; }
        const auto* slice = selectedSlice();
        const QString panId = slice ? slice->panId() : (m_pans.isEmpty() ? QString() : m_pans.firstKey());
        if (!m_model->addSliceOnPan(panId)) { m_status->setText(tr("Additional slice unavailable on this receiver.")); }
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (auto* slice = selectedSlice(); slice && !m_model->removeSlice(slice->sliceId())) {
            m_status->setText(tr("This receive slice cannot be removed."));
        }
    });
    connect(m_model, &RadioModel::connectionStateChanged, this, [this](bool on) {
        m_status->setText(on ? tr("Receiving") : tr("Disconnected"));
        if (!on) { for (const auto& view : std::as_const(m_pans)) { view.spectrum->clearDisplay(); } }
        refreshControls();
    });
    connect(m_model, &RadioModel::connectionError, this, [this](const QString& error) { m_status->setText(error); });
    connect(m_model, &RadioModel::capabilitiesChanged, this, &StationReceiverPanel::applyCapabilities);
    connect(m_model, &RadioModel::panadapterAdded, this, &StationReceiverPanel::addPan);
    connect(m_model, &RadioModel::panadapterRemoved, this, &StationReceiverPanel::removePan);
    connect(m_model, &RadioModel::sliceAdded, this, &StationReceiverPanel::addSlice);
    connect(m_model, &RadioModel::sliceRemoved, this, [this](int id) {
        for (const auto& view : std::as_const(m_pans)) { view.spectrum->removeSliceOverlay(id); }
        refreshSlices();
    });
    connect(m_model, &RadioModel::panFeedSpectrumReady, this,
        [this](quint32 stream, const QVector<float>& bins, qint64, const SpectrumDecodeScale& scale) {
            for (const auto& view : std::as_const(m_pans)) {
                if (view.model && view.model->panStreamId() == stream) { view.spectrum->updateSpectrum(bins, scale); }
            }
        });
    connect(m_model, &RadioModel::panFeedWaterfallRowReady, this,
        [this](quint32 stream, const QVector<float>& bins, double lo, double hi, quint32 time, qint64, bool coherent) {
            for (const auto& view : std::as_const(m_pans)) {
                if (view.model && view.model->wfStreamId() == stream) {
                    view.spectrum->updateWaterfallRow(bins, lo, hi, time, coherent);
                }
            }
        });
    for (auto* pan : m_model->panadapters()) { addPan(pan); }
    for (auto* slice : m_model->slices()) { addSlice(slice); }
    refreshControls();
}

StationReceiverPanel::~StationReceiverPanel() { detach(); }
void StationReceiverPanel::detach()
{
    if (m_model) { QObject::disconnect(m_model, nullptr, this, nullptr); }
    m_model.clear();
    for (const auto& view : std::as_const(m_pans)) {
        if (view.model) { QObject::disconnect(view.model, nullptr, view.spectrum, nullptr); }
        view.spectrum->prepareForShutdown();
    }
    setEnabled(false);
}
SliceModel* StationReceiverPanel::selectedSlice() const
{
    return m_model && m_slice->currentIndex() >= 0 ? m_model->slice(m_slice->currentData().toInt()) : nullptr;
}
void StationReceiverPanel::refreshSlices()
{
    int previous = m_slice->currentIndex() < 0 ? -1 : m_slice->currentData().toInt();
    if (m_model) {
        for (auto* slice : m_model->slices()) { if (slice->isActive()) { previous = slice->sliceId(); break; } }
    }
    const QSignalBlocker blocker(m_slice);
    m_slice->clear();
    if (m_model) {
        for (auto* slice : m_model->slices()) { m_slice->addItem(tr("Slice %1").arg(slice->letter()), slice->sliceId()); }
    }
    const int index = m_slice->findData(previous);
    if (index >= 0) { m_slice->setCurrentIndex(index); }
    refreshControls(); refreshOverlays();
}
void StationReceiverPanel::applyCapabilities(bool connected, const RadioCapabilities& caps)
{
    for (const auto& view : std::as_const(m_pans)) {
        view.spectrum->setRadioOwnsDbmScale(!connected || caps.radioOwnsDbmScale);
        view.spectrum->setPanBinsAbsolute(connected && caps.panBinsAbsolute());
    }
    refreshControls();
}
void StationReceiverPanel::refreshControls()
{
    const auto* slice = selectedSlice();
    for (QWidget* control : std::array<QWidget*, 4>{m_frequency, m_mode, m_low, m_high}) {
        control->setEnabled(slice && m_model && m_model->isConnected());
        control->setAccessibleDescription(slice ? QString() : tr("Connect a receiver and select a receive slice."));
    }
    const bool connected = m_model && m_model->isConnected();
    m_addSlice->setEnabled(connected && m_model->slices().size() < m_model->maxSlices());
    m_addSlice->setAccessibleDescription(m_addSlice->isEnabled() ? QString() : tr("Connect a receiver with a free receive slot."));
    m_removeSlice->setEnabled(connected && slice);
    m_removeSlice->setAccessibleDescription(slice ? QString() : tr("Select a receive slice to remove."));
    m_addPan->setEnabled(connected && m_model->panadapters().size() < m_model->maxPanadapters());
    m_addPan->setAccessibleDescription(m_addPan->isEnabled() ? QString() : tr("No additional waterfall is available on this receiver."));
    if (!slice) { return; }
    const QSignalBlocker frequency(m_frequency), mode(m_mode), low(m_low), high(m_high);
    if (!m_frequency->hasFocus()) { m_frequency->setValue(slice->frequency()); }
    m_mode->clear(); m_mode->addItems(slice->modeList());
    if (m_mode->findText(slice->mode()) < 0) { m_mode->addItem(slice->mode()); }
    m_mode->setCurrentText(slice->mode());
    if (!m_low->hasFocus()) { m_low->setValue(slice->filterLow()); }
    if (!m_high->hasFocus()) { m_high->setValue(slice->filterHigh()); }
}
void StationReceiverPanel::refreshOverlays()
{
    if (!m_model) { return; }
    for (const auto& view : std::as_const(m_pans)) {
        if (!view.model) { continue; }
        for (auto* slice : m_model->slices()) {
            if (slice->panId() != view.model->panId()) { view.spectrum->removeSliceOverlay(slice->sliceId()); continue; }
            view.spectrum->setSliceOverlay(slice->sliceId(), slice->frequency(), slice->filterLow(),
                slice->filterHigh(), false, slice == selectedSlice(), slice->mode());
        }
    }
}
void StationReceiverPanel::addSlice(SliceModel* slice)
{
    const auto refresh = [this] { refreshControls(); refreshOverlays(); };
    connect(slice, &SliceModel::frequencyChanged, this, refresh);
    connect(slice, &SliceModel::modeChanged, this, refresh);
    connect(slice, &SliceModel::filterChanged, this, refresh);
    connect(slice, &SliceModel::panIdChanged, this, refresh);
    connect(slice, &SliceModel::modeListChanged, this, refresh);
    connect(slice, &SliceModel::activeChanged, this, [this](bool active) { if (active) { refreshSlices(); } });
    refreshSlices();
}
void StationReceiverPanel::addPan(PanadapterModel* pan)
{
    if (!pan || m_pans.contains(pan->panId())) { return; }
    auto* spectrum = new SpectrumWidget(bodyWidget(), SpectrumWidget::SettingsPersistence::SessionOnly);
    spectrum->setMinimumHeight(180);
    if (auto* menu = spectrum->overlayMenu()) {
        menu->setEnabled(false);
        const QString reason = tr("Use this receiver panel’s controls above. The full display menu is not connected in the station preview.");
        menu->setToolTip(reason); menu->setAccessibleDescription(reason);
    }
    spectrum->setAccessibleName(tr("Receiver spectrum and waterfall"));
    spectrum->setPanGeometryConfirmationRequired(true);
    spectrum->observeFrequencyRange(pan->centerMhz(), pan->bandwidthMhz());
    spectrum->setDbmRange(pan->minDbm(), pan->maxDbm());
    m_pans.insert(pan->panId(), {pan, spectrum}); m_pansLayout->addWidget(spectrum, 1);
    applyCapabilities(m_model->isConnected(), m_model->backendCapabilities());
    connect(pan, &PanadapterModel::infoChanged, spectrum, &SpectrumWidget::observeFrequencyRange);
    connect(pan, &PanadapterModel::levelChanged, spectrum, &SpectrumWidget::setDbmRange);
    QPointer<PanadapterModel> guardedPan(pan);
    QPointer<SpectrumWidget> guardedSpectrum(spectrum);
    // A retired widget can still have queued gestures, and a replacement can
    // reuse both the pan ID and model. Only the currently owned view may act.
    const auto currentPan = [this, guardedPan, guardedSpectrum] {
        if (!m_model || !guardedPan || !guardedSpectrum) { return false; }
        const auto view = m_pans.constFind(guardedPan->panId());
        return view != m_pans.cend() && view->model == guardedPan
            && view->spectrum == guardedSpectrum;
    };
    const auto geometry = [this, guardedPan, currentPan](std::optional<double> center, std::optional<double> span) {
        if (currentPan()) {
            m_model->requestPanDisplay(guardedPan, {PanDisplayField::Geometry, PanGeometryRequest{center, span}});
        }
    };
    connect(spectrum, &SpectrumWidget::frequencyRangeChangeRequested, this,
        [geometry](double center, double span) { geometry(center, span); });
    connect(spectrum, &SpectrumWidget::centerChangeRequested, this,
        [geometry](double center) { geometry(center, {}); });
    connect(spectrum, &SpectrumWidget::bandwidthChangeRequested, this,
        [geometry](double span) { geometry({}, span); });
    connect(spectrum, &SpectrumWidget::frequencyClicked, this, [this, guardedPan, currentPan](double mhz) {
        if (!currentPan()) { return; }
        if (auto* slice = selectedSlice(); slice && guardedPan && slice->panId() == guardedPan->panId()) { slice->setFrequency(mhz); }
    });
    connect(spectrum, &SpectrumWidget::sliceTuneRequested, this, [this, guardedPan, currentPan](int id, double mhz) {
        if (!currentPan()) { return; }
        auto* slice = m_model ? m_model->slice(id) : nullptr;
        if (slice && guardedPan && slice->panId() == guardedPan->panId()) { slice->setFrequency(mhz); }
    });
    connect(spectrum, &SpectrumWidget::incrementalTuneRequested, this, [this, guardedPan, currentPan](double mhz) {
        if (!currentPan()) { return; }
        if (auto* slice = selectedSlice(); slice && guardedPan && slice->panId() == guardedPan->panId()) { slice->setFrequency(mhz); }
    });
    connect(spectrum, &SpectrumWidget::filterChangeRequested, this, [this, guardedPan, currentPan](int low, int high) {
        if (!currentPan()) { return; }
        if (auto* slice = selectedSlice(); slice && guardedPan && slice->panId() == guardedPan->panId()) { slice->setFilterWidth(low, high); }
    });
    connect(spectrum, &SpectrumWidget::sliceCreateRequested, this, [this, guardedPan, currentPan](double mhz) {
        if (currentPan()) { m_model->addSliceOnPan(guardedPan->panId(), mhz); }
    });
    connect(spectrum, &SpectrumWidget::sliceCloseRequested, this, [this, guardedPan, currentPan](int id) {
        if (!currentPan()) { return; }
        auto* slice = m_model ? m_model->slice(id) : nullptr;
        if (slice && guardedPan && slice->panId() == guardedPan->panId()) { m_model->removeSlice(id); }
    });
    connect(spectrum, &SpectrumWidget::sliceClicked, this, [this, guardedPan, currentPan](int id) {
        if (!currentPan()) { return; }
        const auto* slice = m_model->slice(id);
        if (!slice || slice->panId() != guardedPan->panId()) { return; }
        const int index = m_slice->findData(id); if (index >= 0) {
            m_slice->setCurrentIndex(index);
            if (auto* slice = selectedSlice()) { slice->setActive(true); }
            refreshSlices();
        }
    });
    connect(spectrum, &SpectrumWidget::dbmRangeChangeRequested, spectrum, &SpectrumWidget::setDbmRange);
    refreshOverlays(); refreshControls();
}
void StationReceiverPanel::removePan(const QString& id)
{
    if (!m_pans.contains(id)) { return; }
    const auto view = m_pans.take(id);
    view.spectrum->prepareForShutdown(); m_pansLayout->removeWidget(view.spectrum);
    view.spectrum->hide(); view.spectrum->deleteLater();
    refreshControls();
}
}
