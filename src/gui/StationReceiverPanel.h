#pragma once
#include "gui/CanonSurface.h"
#include <QMap>
#include <QPointer>

class QLabel;
class QPushButton;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QVBoxLayout;
namespace AetherSDR {
class RadioSession;
class RadioModel;
class SliceModel;
class PanadapterModel;
class SpectrumWidget;

// Every pane and intent belongs to this one model; IDs never cross radios.
class StationReceiverPanel final : public CanonCanvas {
    Q_OBJECT
public:
    explicit StationReceiverPanel(RadioSession* session, QWidget* parent = nullptr);
    ~StationReceiverPanel() override;
    int sessionId() const { return m_sessionId; }
    void detach();
signals:
    void removeRequested(int sessionId);
private:
    void addPan(PanadapterModel* pan);
    void removePan(const QString& id);
    void addSlice(SliceModel* slice);
    void refreshSlices();
    void refreshControls();
    void refreshOverlays();
    SliceModel* selectedSlice() const;
    const int m_sessionId;
    QPointer<RadioModel> m_model;
    QLabel* m_status;
    QComboBox* m_slice;
    QDoubleSpinBox* m_frequency;
    QComboBox* m_mode;
    QSpinBox* m_low;
    QSpinBox* m_high;
    QPushButton* m_addSlice;
    QPushButton* m_removeSlice;
    QPushButton* m_addPan;
    QVBoxLayout* m_pansLayout;
    struct PanView { QPointer<PanadapterModel> model; SpectrumWidget* spectrum; };
    QMap<QString, PanView> m_pans;
};
}
