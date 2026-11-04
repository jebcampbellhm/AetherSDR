#include "MainWindow.h"
#include "TabletUi.h"
#include "AppletPanel.h"
#include "CanonWindow.h"
#include "ConnectionPanel.h"
#include "GuardedSlider.h"
#include "TitleBar.h"
#include "core/DigitalVoiceFeature.h"
#include "models/SliceModel.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDoubleValidator>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScroller>
#include <QSignalBlocker>
#include <QSizeGrip>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <array>

namespace AetherSDR {
namespace {

constexpr int kTouchTarget = 48;

QPushButton* touchButton(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setMinimumSize(kTouchTarget, kTouchTarget);
    button->setAutoDefault(false);
    button->setAccessibleName(text);
    button->setFocusPolicy(Qt::StrongFocus);
    return button;
}


CanonWindow* touchSheet(const QString& title, QWidget* parent, QVBoxLayout** content)
{
    auto* sheet = new CanonWindow(title, parent);
    sheet->setAttribute(Qt::WA_DeleteOnClose);
    sheet->setWindowModality(Qt::WindowModal);
    sheet->setReturnClicksDefault(false);
    sheet->setObjectName(QStringLiteral("tabletSheet"));
    auto* layout = new QVBoxLayout(sheet->bodyWidget());
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);
    auto* heading = new QLabel(title, sheet);
    heading->setAccessibleName(title);
    layout->addWidget(heading);
    auto* scroll = new QScrollArea(sheet);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget(scroll);
    *content = new QVBoxLayout(body);
    (*content)->setContentsMargins(0, 0, 0, 0);
    (*content)->setSpacing(12);
    scroll->setWidget(body);
    QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
    layout->addWidget(scroll, 1);
    auto* done = touchButton(QObject::tr("Done"), sheet);
    QObject::connect(done, &QPushButton::clicked, sheet, &QDialog::accept);
    layout->addWidget(done);
    styleTabletSurface(sheet->bodyWidget());
    const QSize available = parent->size();
    sheet->setLaunchSize(QSize(qMin(520, available.width() - 24),
                              qMin(640, available.height() - 24)));
    return sheet;
}

void explainUnavailable(QWidget* widget, const QString& reason)
{
    widget->setEnabled(false);
    widget->setToolTip(reason);
    widget->setAccessibleDescription(reason);
}

} // namespace

void MainWindow::setupTabletUi()
{
    if (!tabletUiEnabled()) {
        return;
    }
    setMinimumSize(320, 400);
    m_titleBar->hide();
    statusBar()->hide();
    m_appletPanel->hide();
    if (m_sizeGrip) {
        m_sizeGrip->hide();
    }
    auto* shell = qobject_cast<QVBoxLayout*>(centralWidget()->layout());
    if (!shell) {
        return;
    }

    auto* header = new QWidget(centralWidget());
    header->setObjectName(QStringLiteral("tabletHeader"));
    auto* rows = new QVBoxLayout(header);
    rows->setContentsMargins(12, 8, 12, 8);
    rows->setSpacing(8);
    auto* identity = new QHBoxLayout;
    auto* brand = new QLabel(QStringLiteral("AetherSDR"), header);
    QFont brandFont = brand->font();
    brandFont.setBold(true);
    brand->setFont(brandFont);
    auto* connection = new QLabel(tr("Not connected"), header);
    connection->setAccessibleName(tr("Radio connection"));
    connection->setWordWrap(true);
    auto* radio = touchButton(tr("Radio"), header);
    radio->setObjectName(QStringLiteral("tabletRadio"));
    connect(radio, &QPushButton::clicked, this, &MainWindow::showConnectionDialog);
    identity->addWidget(brand);
    identity->addWidget(connection, 1);
    identity->addWidget(radio);
    rows->addLayout(identity);

    auto* tuning = new QHBoxLayout;
    auto* frequency = touchButton(tr("Choose a radio"), header);
    frequency->setObjectName(QStringLiteral("tabletFrequency"));
    frequency->setAccessibleName(tr("Receive frequency"));
    frequency->setAccessibleDescription(tr("Tap to enter a frequency in megahertz."));
    frequency->setMinimumHeight(60);
    connect(frequency, &QPushButton::clicked, this, &MainWindow::showTabletFrequencyEntry);
    auto* receiver = new GuardedComboBox(header);
    receiver->setMinimumHeight(kTouchTarget);
    receiver->setAccessibleName(tr("Active receiver"));
    connect(receiver, &QComboBox::currentIndexChanged, this, [this, receiver](int index) {
        setActiveSlice(receiver->itemData(index).toInt());
    });
    tuning->addWidget(frequency, 1);
    tuning->addWidget(receiver);
    rows->addLayout(tuning);

    auto* controls = new QHBoxLayout;
    auto* mode = new GuardedComboBox(header);
    mode->setAccessibleName(tr("Receive mode"));
    mode->setMinimumHeight(kTouchTarget);
    connect(mode, &QComboBox::currentTextChanged, this, [this](const QString& value) {
        if (auto* slice = activeSlice()) {
            slice->setMode(value);
        }
    });
    auto* down = touchButton(QStringLiteral("−"), header);
    auto* up = touchButton(QStringLiteral("+"), header);
    down->setAccessibleName(tr("Tune down one step"));
    up->setAccessibleName(tr("Tune up one step"));
    auto step = [this](int direction) {
        if (auto* slice = activeSlice()) {
            const double target = slice->frequency()
                + direction * qMax(1, slice->stepHz()) / 1.0e6;
            applyTuneRequest(slice, target, TuneIntent::IncrementalTune, "tablet-step");
        }
    };
    connect(down, &QPushButton::clicked, this, [step] { step(-1); });
    connect(up, &QPushButton::clicked, this, [step] { step(1); });
    auto* mute = touchButton(tr("Mute"), header);
    mute->setCheckable(true);
    mute->setAccessibleName(tr("Mute active receiver"));
    connect(mute, &QPushButton::clicked, this, [this](bool checked) {
        if (auto* slice = activeSlice()) {
            slice->setAudioMute(checked);
        }
    });
    auto* lock = touchButton(tr("Lock"), header);
    lock->setCheckable(true);
    lock->setAccessibleName(tr("Lock receive frequency"));
    connect(lock, &QPushButton::clicked, this, [this](bool checked) {
        if (auto* slice = activeSlice()) {
            slice->setLocked(checked);
        }
    });
    controls->addWidget(mode, 1);
    controls->addWidget(down);
    controls->addWidget(up);
    controls->addWidget(mute);
    controls->addWidget(lock);
    rows->addLayout(controls);
    styleTabletSurface(header);
    shell->insertWidget(0, header);

    auto* navigation = new QScrollArea(centralWidget());
    navigation->setWidgetResizable(true);
    navigation->setFrameShape(QFrame::NoFrame);
    navigation->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigation->setFixedHeight(76);
    auto* actions = new QWidget(navigation);
    actions->setObjectName(QStringLiteral("tabletControls"));
    auto* nav = new QHBoxLayout(actions);
    nav->setContentsMargins(12, 8, 12, 8);
    nav->setSpacing(8);
    auto addAction = [actions, nav](const QString& name) {
        auto* button = touchButton(name, actions);
        nav->addWidget(button);
        return button;
    };
    auto* rx = addAction(tr("Receiver"));
    connect(rx, &QPushButton::clicked, this, &MainWindow::showTabletReceiver);
    auto* nr = addAction(tr("Noise reduction"));
    connect(nr, &QPushButton::clicked, this, &MainWindow::showTabletNoiseReduction);
    auto* setup = addAction(tr("Settings"));
    connect(setup, &QPushButton::clicked, this, [this] { openRadioSetupPage(); });
    auto* speech = addAction(tr("Copy Assist"));
#ifdef AETHER_ASR_ENABLED
    connect(speech, &QPushButton::clicked, this, &MainWindow::showCopyAssist);
#else
    explainUnavailable(speech, tr("Speech recognition is not included in this build."));
#endif
    auto* more = addAction(tr("All controls"));
    more->setCheckable(true);
    connect(more, &QPushButton::clicked, this, [this](bool visible) {
        m_appletPanel->setVisible(visible);
    });
    navigation->setWidget(actions);
    QScroller::grabGesture(navigation->viewport(), QScroller::TouchGesture);
    styleTabletSurface(actions);
    // Keep complete labels at narrow widths; the bar should scroll instead
    // of squeezing every button down to the generic touch-target minimum.
    for (auto* button : actions->findChildren<QPushButton*>()) {
        button->ensurePolished();
        button->setMinimumWidth(qMax(kTouchTarget, button->sizeHint().width()));
    }
    shell->addWidget(navigation);

    // Re-read the current slice instead of retaining one across band changes,
    // receiver deletion or session switches. This timer only paints live state.
    auto* refresh = new QTimer(header);
    refresh->setInterval(150);
    connect(refresh, &QTimer::timeout, this,
            [this, connection, frequency, receiver, mode, down, up, mute, lock] {
        auto* slice = activeSlice();
        connection->setText(m_radioModel.isConnected()
            ? tr("Connected · %1").arg(m_radioModel.nickname()) : tr("Not connected"));
        const QSignalBlocker receiverBlock(receiver);
        const QSignalBlocker modeBlock(mode);
        const QSignalBlocker muteBlock(mute);
        const QSignalBlocker lockBlock(lock);
        QStringList ids;
        for (const auto* current : m_radioModel.slices()) {
            ids.append(QString::number(current->sliceId()));
        }
        if (receiver->property("sliceIds").toStringList() != ids) {
            receiver->clear();
            for (const auto* current : m_radioModel.slices()) {
                receiver->addItem(tr("RX %1").arg(current->letter()), current->sliceId());
            }
            receiver->setProperty("sliceIds", ids);
        }
        for (QWidget* control : std::array<QWidget*, 7>{frequency, receiver, mode,
                                                       down, up, mute, lock}) {
            control->setEnabled(slice != nullptr);
            control->setToolTip(QString());
            control->setAccessibleDescription(slice ? QString() : tr("Connect to a radio first."));
        }
        if (!slice) {
            frequency->setText(tr("Choose a radio"));
            mode->clear();
            mode->setProperty("modeList", QStringList());
            return;
        }
        receiver->setCurrentIndex(receiver->findData(slice->sliceId()));
        frequency->setText(tr("%1 MHz").arg(slice->frequency(), 0, 'f', 6));
        frequency->setAccessibleDescription(tr("Tap to enter a frequency in megahertz."));
        QStringList modes = slice->modeList();
        if (modes.isEmpty()) {
            const auto control = m_radioModel.backendCapabilities().receiveModeControl;
            modes = control ? control->modes : QStringList{slice->mode()};
        }
        modes = filterUnavailableDigitalVoiceModes(modes);
        if (mode->property("modeList").toStringList() != modes) {
            mode->clear();
            mode->addItems(modes);
            mode->setProperty("modeList", modes);
        }
        mode->setCurrentText(slice->mode());
        mute->setChecked(slice->audioMute());
        lock->setChecked(slice->isLocked());
        if (slice->isLocked()) {
            for (QWidget* control : std::array<QWidget*, 3>{frequency, down, up}) {
                explainUnavailable(control, tr("Unlock the receiver to tune."));
            }
        }
        const QString stepText = tr("%1 Hz per step").arg(slice->stepHz());
        down->setToolTip(stepText);
        up->setToolTip(stepText);
    });
    refresh->start();
}

void MainWindow::showTabletFrequencyEntry()
{
    QPointer<SliceModel> slice = activeSlice();
    if (!slice || tuneBlockedByGuards(slice)) {
        return;
    }
    QVBoxLayout* layout = nullptr;
    auto* sheet = touchSheet(tr("Tune receiver %1").arg(slice->letter()), this, &layout);
    auto* entry = new QLineEdit(QString::number(slice->frequency(), 'f', 6), sheet);
    entry->setObjectName(QStringLiteral("tabletFrequencyEntry"));
    entry->setAccessibleName(tr("Frequency in MHz"));
    entry->setAccessibleDescription(tr("Enter megahertz, for example 14.074."));
    entry->setReadOnly(true); // The large keypad avoids a keyboard covering the spectrum.
    entry->setAlignment(Qt::AlignRight);
    entry->setMaxLength(12);
    entry->selectAll();
    entry->setProperty("replaceOnNextKey", true);
    layout->addWidget(entry);
    auto* keypad = new QGridLayout;
    const QStringList keys{QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
                           QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6"),
                           QStringLiteral("7"), QStringLiteral("8"), QStringLiteral("9"),
                           QStringLiteral("."), QStringLiteral("0"), tr("Delete")};
    for (int i = 0; i < keys.size(); ++i) {
        auto* key = touchButton(keys[i], sheet);
        key->setMinimumHeight(56);
        keypad->addWidget(key, i / 3, i % 3);
        connect(key, &QPushButton::clicked, sheet, [entry, text = keys[i]] {
            // iOS clears a line edit's selection when a keypad button gains
            // focus. Track replacement explicitly so the first tap never
            // appends to the current frequency.
            QString value = entry->property("replaceOnNextKey").toBool()
                ? QString() : entry->text();
            entry->setProperty("replaceOnNextKey", false);
            if (text == QObject::tr("Delete")) {
                value.chop(1);
            } else if (text != QStringLiteral(".") || !value.contains(QLatin1Char('.'))) {
                value += text;
            }
            entry->setText(value);
        });
    }
    layout->addLayout(keypad);
    auto* error = new QLabel(sheet);
    error->setWordWrap(true);
    error->setAccessibleName(tr("Frequency validation"));
    layout->addWidget(error);
    auto* tune = touchButton(tr("Tune"), sheet);
    layout->addWidget(tune);
    connect(tune, &QPushButton::clicked, sheet, [this, sheet, entry, error, slice] {
        bool valid = false;
        const double mhz = entry->text().toDouble(&valid);
        if (!valid || !std::isfinite(mhz) || mhz <= 0.0 || mhz > 100000.0) {
            error->setText(tr("Enter a valid frequency in MHz."));
            return;
        }
        if (!slice || m_radioModel.slice(slice->sliceId()) != slice || tuneBlockedByGuards(slice)) {
            error->setText(tr("The receiver is unavailable or locked."));
            return;
        }
        applyTuneRequest(slice, mhz, TuneIntent::CommandedTargetCenter, "tablet-keypad");
        sheet->accept();
    });
    sheet->show();
}

void MainWindow::showTabletReceiver()
{
    QVBoxLayout* layout = nullptr;
    auto* sheet = touchSheet(tr("Receiver"), this, &layout);
    auto* instructions = new QLabel(tr("These controls follow the selected receiver."), sheet);
    instructions->setWordWrap(true);
    layout->addWidget(instructions);
    auto* gainLabel = new QLabel(tr("Audio level"), sheet);
    auto* gain = new GuardedSlider(Qt::Horizontal, sheet);
    gain->setAccessibleName(tr("Receiver audio level"));
    gain->setRange(0, 100);
    layout->addWidget(gainLabel);
    layout->addWidget(gain);
    auto* agcLabel = new QLabel(tr("AGC threshold"), sheet);
    auto* agc = new GuardedSlider(Qt::Horizontal, sheet);
    agc->setAccessibleName(tr("AGC threshold"));
    layout->addWidget(agcLabel);
    layout->addWidget(agc);
    auto* step = new GuardedComboBox(sheet);
    step->setAccessibleName(tr("Tuning step"));
    for (int hz : {1, 10, 50, 100, 500, 1000, 5000, 10000}) {
        step->addItem(tr("%1 Hz per step").arg(hz), hz);
    }
    layout->addWidget(step);
    connect(step, &QComboBox::currentIndexChanged, this, [this, step](int index) {
        applyOperatorTuningStep(step->itemData(index).toInt());
    });
    connect(gain, &QSlider::valueChanged, this, [this](int value) {
        if (auto* slice = activeSlice()) {
            slice->setAudioGain(static_cast<float>(value));
        }
    });
    connect(agc, &QSlider::valueChanged, this, [this](int value) {
        if (auto* slice = activeSlice()) {
            slice->setAgcTKnobLevel(value);
        }
    });
    auto* dsp = touchButton(tr("Open AetherRX"), sheet);
    connect(dsp, &QPushButton::clicked, this, [this, sheet] {
        sheet->accept();
        toggleAetherRxDialog();
    });
    layout->addWidget(dsp);
    layout->addStretch();
    auto* timer = new QTimer(sheet);
    connect(timer, &QTimer::timeout, sheet, [this, gain, gainLabel, agc, agcLabel, step] {
        auto* slice = activeSlice();
        gain->setEnabled(slice != nullptr);
        agc->setEnabled(slice != nullptr);
        step->setEnabled(slice != nullptr);
        if (!slice) {
            gain->setAccessibleDescription(tr("Connect to a radio first."));
            agc->setAccessibleDescription(tr("Connect to a radio first."));
            return;
        }
        const QSignalBlocker gainBlock(gain);
        const QSignalBlocker agcBlock(agc);
        const QSignalBlocker stepBlock(step);
        if (step->findData(slice->stepHz()) < 0) {
            step->addItem(tr("%1 Hz per step").arg(slice->stepHz()), slice->stepHz());
        }
        step->setCurrentIndex(step->findData(slice->stepHz()));
        if (!gain->isSliderDown()) {
            gain->setValue(qRound(slice->audioGain()));
        }
        if (!agc->isSliderDown()) {
            agc->setRange(slice->agcTKnobMinimum(), slice->agcTKnobMaximum());
            agc->setValue(slice->agcTKnobLevel());
        }
        gainLabel->setText(tr("Audio level · %1%").arg(gain->value()));
        agcLabel->setText(tr("AGC threshold · %1").arg(agc->value()));
    });
    timer->start(150);
    sheet->show();
}

void MainWindow::showTabletNoiseReduction()
{
    QVBoxLayout* layout = nullptr;
    auto* sheet = touchSheet(tr("Noise reduction"), this, &layout);
    auto* note = new QLabel(tr("Process received audio on this iPad."), sheet);
    note->setWordWrap(true);
    layout->addWidget(note);
    const QStringList methods{QStringLiteral("NR2"), QStringLiteral("RN2"),
                              QStringLiteral("NR4"), QStringLiteral("NNR"),
                              QStringLiteral("MNR")};
    for (const QString& method : methods) {
        auto* button = touchButton(method, sheet);
        button->setCheckable(true);
        button->setChecked(activeAetherDspMethod() == method);
        button->setAccessibleName(tr("%1 noise reduction").arg(method));
        connect(button, &QPushButton::clicked, this, [this, method](bool enabled) {
            setAetherDspMethodEnabled(method, enabled);
        });
        auto* timer = new QTimer(button);
        connect(timer, &QTimer::timeout, button, [this, button, method] {
            const QSignalBlocker block(button);
            button->setChecked(activeAetherDspMethod() == method);
        });
        timer->start(200);
#ifndef HAVE_SPECBLEACH
        if (method == QStringLiteral("NR4")) {
            explainUnavailable(button, tr("NR4 is not included in this build."));
        }
#endif
#ifndef __APPLE__
        if (method == QStringLiteral("MNR")) {
            explainUnavailable(button, tr("MNR requires Apple Accelerate."));
        }
#endif
        layout->addWidget(button);
    }
    auto* settings = touchButton(tr("NR4 settings"), sheet);
    connect(settings, &QPushButton::clicked, this, [this, sheet] {
        const QPoint anchor = mapToGlobal(rect().center());
        sheet->accept();
        showNr4ParamPopup(anchor);
    });
    layout->addWidget(settings);
    layout->addStretch();
    sheet->show();
}

} // namespace AetherSDR
