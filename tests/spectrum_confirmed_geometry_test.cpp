// Opt-in real-widget test: no sockets, radio, USB, or synthetic firmware peer.
#include "TestSettingsProfile.h"
#include "FlexSquelchBridgeFixture.h"
#include "gui/SpectrumWidget.h"
#include "gui/SpectrumTraceBins.h"
#include "gui/VfoWidget.h"
#include "gui/SpectrumOverlayMenu.h"
#include "gui/MainWindowHelpers.h"
#include "gui/DisplaySettings.h"
#include "gui/StationReceiverPanel.h"
#include "gui/StationWorkspace.h"
#include "gui/RadioSetupDialog.h"
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QContextMenuEvent>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QAction>
#include <QDir>
#include <QFontDatabase>
#include "gui/ConnectionPanel.h"
#include "core/AppSettings.h"
#include "core/SpectrumDecodeScale.h"
#include "models/RadioSession.h"
#ifdef AETHER_BACKEND_RTL
#include "RtlInjectedDevice.h"
#include "core/backends/rtl/RtlSdrBackend.h"
#endif
#include "core/backends/flex/FlexBackend.h"
#include "core/backends/hl2/Hl2Backend.h"
#include "models/SliceModel.h"
#include "models/PanadapterModel.h"
#include "models/BandPlanManager.h"
#include <QImage>
#include <QToolTip>
#include <QPainter>
#include <QMap>
#include <QVariant>
#include <QCheckBox>
#include <QComboBox>
#include <QPointer>
#include <QEvent>
#include <QLineEdit>
#include <QElapsedTimer>

#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QThread>
#include <QTimer>
#include <QSlider>
#include <QPushButton>
#include <cmath>
#include <cstdio>
#include <memory>

using namespace AetherSDR;

namespace AetherSDR {
struct StationWorkspaceTestAccess {
    static void add(StationWorkspace& workspace, const RadioInfo& info) { workspace.addReceiver(info, {}); }
    static void open(StationWorkspace& workspace, int id) { workspace.showConnect(id); }
    static RadioStation& station(StationWorkspace& workspace) { return workspace.m_station; }
    static ConnectionPanel* form(StationWorkspace& workspace) { return workspace.m_connection; }
    static StationReceiverPanel* panel(StationWorkspace& workspace, int id) { return workspace.m_panels.value(id); }
};
struct SpectrumOffscreenTestAccess {
    static QImage sqlImage(SpectrumWidget& widget) {
        QImage image(800, 300, QImage::Format_ARGB32);
        image.fill(Qt::black);
        QPainter painter(&image);
        widget.drawSquelchLine(painter, image.rect());
        widget.drawAutoSqlFloor(painter, image.rect());
        return image;
    }
    static bool sqlHideTimerActive(const SpectrumWidget& widget) {
        return widget.m_squelchLineHideTimer->isActive();
    }
    static void sqlBins(SpectrumWidget& widget) {
        widget.updateAutoSquelchFromBins(QVector<float>(256, -110.0f));
    }
    static void release(SpectrumWidget& widget, DbmRangeTransition::Range old,
                        DbmRangeTransition::Range target, quint64 generation) {
        widget.m_refLevel = target.maxDbm;
        widget.m_dynamicRange = target.maxDbm - target.minDbm;
        widget.beginDbmRangeTransition(old.minDbm, old.maxDbm, target.minDbm, target.maxDbm);
        widget.setEncoderDbmRange(target.minDbm, target.maxDbm, generation);
    }
    static const QVector<float>& corrected(const SpectrumWidget& widget) { return widget.m_bins; }
    static bool releasePending(const SpectrumWidget& widget) {
        return widget.m_dbmReleaseRebaseUntilMs > 0;
    }
    static void finishSettleWindow(SpectrumWidget& widget) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        widget.m_noiseFloorScaleSettlingUntilMs = now - 1;
        widget.m_noiseFloorReacquisition = NoiseFloorReacquisition{};
        widget.m_noiseFloorReacquisition.arm(now - 1);
    }
    static void allowHeadroom(SpectrumWidget& widget) {
        widget.m_encoderRangeChangedMs = 0;
        widget.m_lastDssRadioHeadroomRequestMs = 0;
        widget.m_noiseFloorScaleSettlingUntilMs = 0;
    }
    static bool acquiring(const SpectrumWidget& widget) { return widget.m_noiseFloorReacquisition.active(); }
    static int freshFrames(const SpectrumWidget& widget) { return widget.m_noiseFloorFreshFrameCount; }
    static float baseline(const SpectrumWidget& widget) { return widget.m_noiseFloorBaselineDbm; }
    static bool baselineValid(const SpectrumWidget& widget) { return widget.m_noiseFloorBaselineValid; }
    static const QVector<float>& trace(const SpectrumWidget& widget) { return widget.displaySpectrumBins(); }
    static QVector<float> supplemental(const SpectrumWidget& widget, const QVector<float>& bins,
                                       double low, double high, bool sameScale) {
        return widget.buildNativeDssSupplementalRow(bins, low, high, sameScale);
    }
    static void prepareHistory(SpectrumWidget& widget) {
        widget.resetCurrentWaterfallRowsForSize(QSize(800, 100), QSize(800, 16));
    }
    static const DssRenderer& dss(const SpectrumWidget& widget) { return widget.m_dss; }
    static void preview(SpectrumWidget& widget, double base, double target, double span) {
        widget.m_frequencyPreviewActive = true;
        widget.m_frequencyPreviewBaseCenterMhz = base;
        widget.m_frequencyPreviewBaseBandwidthMhz = span * 2;
        widget.m_frequencyPreviewTargetCenterMhz = target;
        widget.m_frequencyPreviewTargetBandwidthMhz = span;
    }
    // A point inside the band-plan hover row at `mhz` — the same geometry
    // mouseMoveEvent() hit-tests (bottom 8 px of the spectrum area). (#4667)
    static QPoint bandRow(const SpectrumWidget& widget, double mhz) {
        const int specH = static_cast<int>((widget.height() - widget.freqScaleH()
                                            - SpectrumWidget::DIVIDER_H)
                                           * widget.m_spectrumFrac);
        return {widget.mhzToX(mhz), specH - 4};
    }
    static QPoint marker(SpectrumWidget& widget) {
        QImage image(widget.size(), QImage::Format_ARGB32);
        QPainter painter(&image);
        widget.drawOffScreenSlices(painter, QRect(0, 0, widget.width(), widget.spectrumPixelHeight()));
        return widget.m_offScreenRects.front().center();
    }
    // The shared anchored-zoom tail both the macOS trackpad (ZoomNativeGesture)
    // and the touch pinch (QPinchGesture) funnel through after the refactor.
    static void anchoredZoom(SpectrumWidget& widget, double bandwidthFactor,
                             QPointF globalPos) {
        widget.applyAnchoredZoom(bandwidthFactor, globalPos);
    }
    static void finishZoomSettle(SpectrumWidget& widget) {
        widget.finishFrequencyRangeSettleUpdate();
    }
    // One in-window slice-drag tune step: edgePanTuneRequested with the
    // centre unchanged.
    static void sliceDragTuneStep(SpectrumWidget& widget, int x) {
        widget.driveVfoDragTune(x, "test");
    }
};
}
#ifdef AETHER_BACKEND_RTL
namespace AetherSDR::rtl {
struct RtlCaptureBackendTestAccess {
    static void start(RtlSdrBackend& backend, std::unique_ptr<RtlSdrWorker::Device> device) {
        backend.m_requested.hardware = {100'000'000, 2'400'000, 0, 0, 0, 240};
        backend.m_requested.receivers = {{{0, 100'000'000, -6000, 6000, 0, 3000, 3000}, RtlCaptureTransaction::Mode::Fm}};
        backend.startCapture(std::make_unique<RtlSdrWorker>(std::move(device)));
    }
    static bool idle(const RtlSdrBackend& backend) { return !backend.m_capture.busy() && !backend.m_pendingDrag; }
};
}
#endif
namespace AetherSDR::hl2 {
struct Hl2DspReadbackTestAccess {
    static void receiver(Hl2Backend& backend) {
        backend.m_rx.resize(1);
        backend.m_ids.reset(1);
        backend.m_rx[0].sliceFreqHz = 14'200'000;
        backend.m_rx[0].ncoHz = 14'000'000;
    }
    static double frequency(const Hl2Backend& backend) { return backend.m_rx[0].sliceFreqHz; }
};
}
namespace {
void mouse(SpectrumWidget& widget, QEvent::Type type, int x, int y)
{
    const QPointF local(x, y);
    QMouseEvent event(type, local, local, widget.mapToGlobal(local.toPoint()),
        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}
void wait(int ms)
{
    for (int i = 0; i < ms; i += 5) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
}
bool near(double a, double b) { return std::abs(a - b) < 1e-10; }

// Registered with QT_SCALE_FACTOR=2 (a Retina screen). A FLEX-8600 on 4.3.16
// stops FFT and waterfall data for every client at xpixels 4093 or more.
int runPanPixelsAtRetinaScale()
{
    int failures = 0;
    const auto check = [&](bool ok, const char* name) {
        std::printf("[%s] %s\n", ok ? "OK" : "FAIL", name);
        failures += !ok;
    };
    auto panStorage = std::make_unique<SpectrumWidget>();
    auto& pan = *panStorage;
    pan.resize(1000, 600);
    const double ratio = pan.devicePixelRatioF();
    if (!near(ratio, 2.0)) {
        std::printf("[FAIL] pan pixels: device pixel ratio is %g, expected 2\n", ratio);
        return 1;
    }
    check(panXpixelsFor(&pan) == 2000, "pan pixels: a 1000-point pan requests 2000 device pixels");
    pan.resize(1921, 600);
    check(panXpixelsFor(&pan) == 3840, "pan pixels: a 1921-point pan (3842 unclamped) is capped at 3840");
    pan.resize(2047, 600);
    // The safety bound, from the measured stall: unlike the 3840 checks, which
    // follow the chosen cap, this one must not be relaxed if the cap changes.
    check(panXpixelsFor(&pan) <= 4092,
          "pan pixels: a 2047-point pan (4094 unclamped, a stalling width) requests at most 4092");
    pan.resize(2100, 600);
    check(panXpixelsFor(&pan) == 3840, "pan pixels: a 2100-point pan is capped at 3840");
    pan.resize(2560, 600);
    check(panXpixelsFor(&pan) == 3840, "pan pixels: a 2560-point pan (a 5K screen's full width) is capped at 3840");
    return failures ? 1 : 0;
}

// `get tracedebug <pan> bins` (#5806): the widget hands out the last frame as
// it arrived, with the geometry it arrived under, and refuses while a pan or
// zoom has not been followed by kSettledFrames frames.
int runTraceBins()
{
    int failures = 0;
    const auto check = [&](bool ok, const char* name) {
        std::printf("[%s] %s\n", ok ? "OK" : "FAIL", name);
        failures += !ok;
    };
    constexpr int kSettled = static_cast<int>(SpectrumTraceBins::FrameCapture::kSettledFrames);
    int frameNo = 0;
    // Each frame distinct and sloped, so a reprojected copy cannot equal it.
    const auto frame = [&frameNo]() {
        ++frameNo;
        QVector<float> bins(256);
        for (int i = 0; i < bins.size(); ++i) {
            bins[i] = -130.0f + 0.1f * static_cast<float>(i) + static_cast<float>(frameNo);
        }
        bins[64] = -40.0f - static_cast<float>(frameNo);
        return bins;
    };
    const auto refusal = [](const QVariantMap& m) {
        return m.value(QStringLiteral("refusal")).toString();
    };
    const auto sameBins = [](const QVariantMap& m, const QVector<float>& want) {
        const QVariantList got = m.value(QStringLiteral("bins")).toList();
        if (got.size() != want.size()) {
            return false;
        }
        for (int i = 0; i < want.size(); ++i) {
            if (got.at(i).toDouble() != static_cast<double>(want.at(i))) {
                return false;
            }
        }
        return true;
    };
    const auto nearHz = [](double a, double b) { return std::abs(a - b) < 1e-3; };

    auto wStorage = std::make_unique<SpectrumWidget>();
    auto& w = *wStorage;
    w.resize(1000, 600);
    w.setFrequencyRangeImmediate(14.100, 0.200);
    check(refusal(w.traceBinsSnapshot()).contains(QLatin1String("no frame")),
          "trace bins: refused before any frame");

    QVector<float> last;
    for (int i = 0; i < kSettled; ++i) {
        last = frame();
        w.updateSpectrum(last);
    }
    QVariantMap m = w.traceBinsSnapshot();
    check(refusal(m).isEmpty() && sameBins(m, last),
          "trace bins (a): settled frames are returned exactly as fed");
    check(nearHz(m.value(QStringLiteral("centerHz")).toDouble(), 14'100'000.0)
              && nearHz(m.value(QStringLiteral("spanHz")).toDouble(), 200'000.0)
              && m.value(QStringLiteral("ageMs")).toLongLong() >= 0,
          "trace bins (a): with the ingest geometry and a frame age");

    // (b) A radio-echo zoom: setFrequencyRangeImmediate reprojects m_bins.
    w.setFrequencyRangeImmediate(14.100, 0.100);
    m = w.traceBinsSnapshot();
    check(!refusal(m).isEmpty(), "trace bins (b): refused right after a zoom, before any new frame");
    for (int i = 0; i < kSettled - 1; ++i) {
        last = frame();
        w.updateSpectrum(last);
        check(!refusal(w.traceBinsSnapshot()).isEmpty(),
              "trace bins (b): refused while fewer than kSettledFrames frames followed the zoom");
    }
    last = frame();
    w.updateSpectrum(last);
    m = w.traceBinsSnapshot();
    check(refusal(m).isEmpty() && sameBins(m, last)
              && nearHz(m.value(QStringLiteral("spanHz")).toDouble(), 100'000.0),
          "trace bins (b): the kSettledFrames-th frame after the zoom is returned as fed, at the new span");

    // (b) A centre-only snap (a large pan): m_bins is reprojected, not replaced.
    w.setFrequencyRangeImmediate(14.160, 0.100);
    check(!refusal(w.traceBinsSnapshot()).isEmpty(),
          "trace bins (b): refused right after a large pan");
    for (int i = 0; i < kSettled; ++i) {
        last = frame();
        w.updateSpectrum(last);
    }
    m = w.traceBinsSnapshot();
    check(refusal(m).isEmpty() && sameBins(m, last)
              && nearHz(m.value(QStringLiteral("centerHz")).toDouble(), 14'160'000.0),
          "trace bins (b): settled again after the pan, at the new centre");

    // (b) A local anchored zoom (trackpad/pinch path): in progress until it settles.
    const QPointF anchor(w.mapToGlobal(QPoint(w.width() / 2, w.height() / 2)));
    SpectrumOffscreenTestAccess::anchoredZoom(w, 1.5, anchor);
    check(!refusal(w.traceBinsSnapshot()).isEmpty(),
          "trace bins (b): refused during a local zoom gesture");
    SpectrumOffscreenTestAccess::finishZoomSettle(w);
    for (int i = 0; i < kSettled - 1; ++i) {
        w.updateSpectrum(frame());
    }
    check(!refusal(w.traceBinsSnapshot()).isEmpty(),
          "trace bins (b): refused after the gesture until kSettledFrames frames");
    last = frame();
    w.updateSpectrum(last);
    m = w.traceBinsSnapshot();
    check(refusal(m).isEmpty() && sameBins(m, last),
          "trace bins (b): returned once the gesture's frames settled");

    // (c) A pan-follow nudge animates the centre.
    const double center = w.centerMhz();
    w.setFrequencyRange(center + w.bandwidthMhz() * 0.05, w.bandwidthMhz());
    check(refusal(w.traceBinsSnapshot()).contains(QLatin1String("animation")),
          "trace bins (c): refused while the pan animation runs");
    for (int i = 0; i < kSettled; ++i) {
        w.updateSpectrum(frame());
    }
    check(refusal(w.traceBinsSnapshot()).contains(QLatin1String("animation")),
          "trace bins (c): frames during the animation do not end the refusal");
    wait(400);
    check(!refusal(w.traceBinsSnapshot()).isEmpty(),
          "trace bins (c): refused after the animation lands until new frames arrive");
    for (int i = 0; i < kSettled; ++i) {
        last = frame();
        w.updateSpectrum(last);
    }
    m = w.traceBinsSnapshot();
    check(refusal(m).isEmpty() && sameBins(m, last)
              && nearHz(m.value(QStringLiteral("centerHz")).toDouble(), w.centerMhz() * 1e6),
          "trace bins (c): returned after the animation at the landed centre");

    // (d) rfoust's probe (#6720): frames queued by Receive Sync before a pan
    // and delivered after it. Unflagged, as on 3c71caffb, they pass at the new
    // centre; flagged as held they are refused until kSettledFrames unheld.
    {
        auto qStorage = std::make_unique<SpectrumWidget>();
        auto& q = *qStorage;
        q.resize(1000, 600);
        q.setFrequencyRangeImmediate(14.100, 0.200);
        for (int i = 0; i < kSettled; ++i) {
            q.updateSpectrum(frame());
        }
        QList<QVector<float>> queued;
        for (int i = 0; i < kSettled; ++i) {
            queued.append(frame());
        }
        q.setFrequencyRangeImmediate(14.160, 0.200);
        for (const auto& f : queued) {
            q.updateSpectrum(f);
        }
        QVariantMap qm = q.traceBinsSnapshot();
        check(refusal(qm).isEmpty()
                  && nearHz(qm.value(QStringLiteral("centerHz")).toDouble(), 14'160'000.0),
              "trace bins (d): unflagged queued frames pass at the new centre (the defect)");
        q.setFrequencyRangeImmediate(14.220, 0.200);
        for (const auto& f : queued) {
            q.updateSpectrum(f, {}, true);
        }
        check(refusal(q.traceBinsSnapshot()).contains(QLatin1String("held by Receive Sync")),
              "trace bins (d): held frames are refused");
        for (int i = 0; i < kSettled - 1; ++i) {
            q.updateSpectrum(frame());
        }
        check(!refusal(q.traceBinsSnapshot()).isEmpty(),
              "trace bins (d): the count restarts after held frames");
        last = frame();
        q.updateSpectrum(last);
        qm = q.traceBinsSnapshot();
        check(refusal(qm).isEmpty() && sameBins(qm, last),
              "trace bins (d): unheld frames are accepted after kSettledFrames");
    }

    // The encoder fields are the frame's, not the widget's current range.
    {
        auto eStorage = std::make_unique<SpectrumWidget>();
        auto& e = *eStorage;
        e.resize(1000, 600);
        e.setFrequencyRangeImmediate(7.100, 0.200);
        e.setEncoderDbmRange(-140.0f, -20.0f, 1);
        for (int i = 0; i < kSettled; ++i) {
            e.updateSpectrum(frame(), SpectrumDecodeScale{-140.0f, -20.0f, 1});
        }
        e.setEncoderDbmRange(-150.0f, -30.0f, 2);
        const QVariantMap em = e.traceBinsSnapshot();
        check(refusal(em).isEmpty()
                  && em.value(QStringLiteral("encoderMinDbm")).toFloat() == -140.0f
                  && em.value(QStringLiteral("encoderMaxDbm")).toFloat() == -20.0f,
              "trace bins: the encoder range is the one the returned frame was decoded with");
    }

    // clearDisplay (disconnect, profile load) drops the captured frame.
    w.clearDisplay();
    check(refusal(w.traceBinsSnapshot()).contains(QLatin1String("no frame")),
          "trace bins: refused as no frame after clearDisplay");
    for (int i = 0; i < kSettled; ++i) {
        last = frame();
        w.updateSpectrum(last);
    }
    m = w.traceBinsSnapshot();
    check(refusal(m).isEmpty() && sameBins(m, last), "trace bins: returned again after new frames");

    // A slice-drag tune step leaves the pan where it is: no geometry change.
    {
        const quint64 settledBefore = m.value(QStringLiteral("settledFrames")).toULongLong();
        SpectrumOffscreenTestAccess::sliceDragTuneStep(w, w.width() / 3);
        const QVariantMap after = w.traceBinsSnapshot();
        check(refusal(after).isEmpty()
                  && after.value(QStringLiteral("settledFrames")).toULongLong() == settledBefore,
              "trace bins: a slice-drag tune step with an unchanged centre keeps the settle count");
    }

    // Confirmation mode: a sent request changes nothing until the radio's echo.
    {
        auto fStorage = std::make_unique<SpectrumWidget>();
        auto& f = *fStorage;
        f.resize(1000, 600);
        f.setPanGeometryConfirmationRequired(true);
        f.observeFrequencyRange(10.120, 0.200);
        double reqCenter = -1.0;
        double reqSpan = -1.0;
        QObject::connect(&f, &SpectrumWidget::frequencyRangeChangeRequested, &f,
                         [&](double c, double b) { reqCenter = c; reqSpan = b; });
        for (int i = 0; i < kSettled; ++i) {
            f.updateSpectrum(frame());
        }
        check(refusal(f.traceBinsSnapshot()).isEmpty(), "trace bins (confirm): settled before the request");
        const QPointF mid(f.mapToGlobal(QPoint(f.width() / 2, f.height() / 2)));
        SpectrumOffscreenTestAccess::anchoredZoom(f, 0.5, mid);
        SpectrumOffscreenTestAccess::finishZoomSettle(f);
        check(reqSpan > 0.0 && nearHz(f.bandwidthMhz() * 1e6, 200'000.0),
              "trace bins (confirm): the zoom is requested, not applied");
        for (int i = 0; i < kSettled; ++i) {
            f.updateSpectrum(frame());
        }
        check(refusal(f.traceBinsSnapshot()).contains(QLatin1String("not yet confirmed")),
              "trace bins (confirm): frames before the echo do not end the refusal");
        f.observeFrequencyRange(reqCenter, reqSpan);
        check(!refusal(f.traceBinsSnapshot()).isEmpty(),
              "trace bins (confirm): refused right after the echo moves the geometry");
        for (int i = 0; i < kSettled; ++i) {
            last = frame();
            f.updateSpectrum(last);
        }
        m = f.traceBinsSnapshot();
        check(refusal(m).isEmpty() && sameBins(m, last)
                  && nearHz(m.value(QStringLiteral("spanHz")).toDouble(), reqSpan * 1e6),
              "trace bins (confirm): returned after the echo and settled frames, at the echoed span");
        // An echo that never comes stops refusing after the guard.
        SpectrumOffscreenTestAccess::anchoredZoom(f, 0.5, mid);
        SpectrumOffscreenTestAccess::finishZoomSettle(f);
        for (int i = 0; i < kSettled; ++i) {
            f.updateSpectrum(frame());
        }
        check(refusal(f.traceBinsSnapshot()).contains(QLatin1String("not yet confirmed")),
              "trace bins (confirm): a request without an echo is refused");
        wait(static_cast<int>(SpectrumTraceBins::FrameCapture::kRequestEchoGuardMs) + 200);
        check(refusal(f.traceBinsSnapshot()).isEmpty(),
              "trace bins (confirm): a lost echo stops refusing after kRequestEchoGuardMs");
        // A slice-drag tune step sends no pan command, so nothing awaits an echo.
        SpectrumOffscreenTestAccess::sliceDragTuneStep(f, f.width() / 3);
        check(refusal(f.traceBinsSnapshot()).isEmpty(),
              "trace bins (confirm): a slice-drag tune step with an unchanged centre is not an unconfirmed request");
    }
    return failures ? 1 : 0;
}
}

// Real widgets, isolated settings database, no window shown or radio opened.
static int runStationDisplayIsolation()
{
    int failures = 0;
    const auto check = [&](bool ok, const char* name) {
        std::printf("[%s] %s\n", ok ? "OK" : "FAIL", name);
        failures += !ok;
    };
    auto& settings = AppSettings::instance();
    settings.load();
    QWidget parent;
    auto primaryStorage = std::make_unique<SpectrumWidget>(&parent);
    auto& primary = *primaryStorage;
    primary.setSpectrumFrac(0.4f);
    primary.setWfColorGain(50);
    primary.setShowTuneGuides(false);
    primary.setShowFpsMeters(false);
    primary.setExtendedPassband(false);
    primary.setExtendedTnf(false);
    primary.setThreeDSliceDepth(false);
    // A transient constructor must not run the primary slot's migration.
    settings.setValue("DisplayFftAverage", "42");
    settings.save();
    const auto snapshot = [&] {
        QMap<QString, QVariant> result;
        for (const QString& key : settings.allKeysForDiagnostics()) { result.insert(key, settings.value(key)); }
        return result;
    };
    const auto before = snapshot();
    auto firstStorage = std::make_unique<SpectrumWidget>(&parent, SpectrumWidget::SettingsPersistence::SessionOnly);
    auto& first = *firstStorage;
    auto secondStorage = std::make_unique<SpectrumWidget>(&parent, SpectrumWidget::SettingsPersistence::SessionOnly);
    auto& second = *secondStorage;
    check(first.wfColorGain() == 50 && second.wfColorGain() == 50,
          "extra panels seed the current appearance");
    first.setSpectrumFrac(0.7f);
    first.setWfColorGain(72);
    first.setWfBlackLevel(31);
    first.setWfAutoBlack(false);
    first.setFftSmoothing(3);
    first.setFftDecombHz(100);
    first.setDssGain(40);
    first.setDssFloorDepth(10);
    first.setShowGrid(false);
    first.setShowTuneGuides(true);
    first.setShowFpsMeters(true);
    first.setExtendedPassband(true);
    first.setExtendedTnf(true);
    first.setThreeDSliceDepth(true);
    first.setWaterfallTimeMarkerSeconds(15);
    first.setPanIndex(1);
    first.loadSettings();
    settings.save();
    check(snapshot() == before, "extra panel edits and construction leave shared settings unchanged");
    QString disk;
    check(settings.readAppRowFromDisk("SpectrumSplitRatio", disk) && disk == "0.400"
          && settings.readAppRowFromDisk("DisplayFftAverage", disk) && disk == "42",
          "primary layout and legacy rows remain unchanged on disk");
    check(std::abs(first.spectrumFrac() - 0.7f) < 0.001f && first.wfColorGain() == 72
          && first.waterfallTimeMarkerSeconds() == 15 && first.fftSmoothing() == 3,
          "local edits survive reload and pan-index changes");
    check(!primary.showTuneGuides() && !primary.showFpsMeters() && !primary.extendedPassband()
          && !primary.extendedTnf() && !primary.threeDSliceDepth()
          && !second.showTuneGuides() && !second.showFpsMeters() && second.wfColorGain() == 50,
          "local edits do not propagate to primary or another receiver");
    primary.setShowTuneGuides(true);
    primary.setShowFpsMeters(true);
    primary.setExtendedPassband(true);
    primary.setExtendedTnf(true);
    primary.setThreeDSliceDepth(true);
    primary.setSpectrumFrac(0.3f);
    primary.setWfColorGain(60);
    second.loadSettings();
    check(!second.showTuneGuides() && !second.showFpsMeters() && !second.extendedPassband()
          && !second.extendedTnf() && !second.threeDSliceDepth() && second.wfColorGain() == 50
          && std::abs(second.spectrumFrac() - 0.4f) < 0.001f,
          "primary edits and global display broadcasts cannot overwrite extra receivers");
    check(settings.readAppRowFromDisk("SpectrumSplitRatio", disk) && disk == "0.300"
          && DisplaySettings::extendedPassband(), "primary display settings still persist");
    primary.prepareForShutdown(); first.prepareForShutdown(); second.prepareForShutdown();

    // Identical per-radio stream IDs must still address different view owners.
    RadioSession firstRadio, secondRadio;
    QString fixtureError;
    check(firstRadio.radioModel().automationApplySliceFixture(0, QStringLiteral("A"), &fixtureError),
          "offline receive slice fixture created without a transport connection");
    PanadapterModel firstPan(QStringLiteral("0x40000000"));
    PanadapterModel secondPan(QStringLiteral("0x40000000"));
    StationReceiverPanel firstPanel(&firstRadio), secondPanel(&secondRadio);
    const auto beforePanels = snapshot();
    emit firstRadio.radioModel().panadapterAdded(&firstPan);
    emit secondRadio.radioModel().panadapterAdded(&secondPan);
    auto* firstView = firstPanel.findChild<SpectrumWidget*>();
    auto* secondView = secondPanel.findChild<SpectrumWidget*>();
    check(firstView && secondView && firstView != secondView, "receiver panels create independent views");
    if (firstView && secondView) {
        firstView->resize(800, 300); secondView->resize(800, 300);
        firstView->setSpectrumFrac(0.8f);
        check(snapshot() == beforePanels, "production receiver panels select isolated display preferences");
        RadioCapabilities caps;
        caps.radioOwnsDbmScale = false;
        caps.panAmplitude.emplace();
        caps.panAmplitude->binsAbsolute = true;
        emit firstRadio.radioModel().capabilitiesChanged(true, caps);
        check(!firstView->radioOwnsDbmScale() && firstView->panBinsAbsolute()
              && secondView->radioOwnsDbmScale() && !secondView->panBinsAbsolute(),
              "display scale policy follows only its owning radio");
        emit firstRadio.radioModel().panFeedSpectrumReady(firstPan.panStreamId(),
            QVector<float>(256, -104.0f), 0, SpectrumDecodeScale{});
        check(!SpectrumOffscreenTestAccess::corrected(*firstView).isEmpty()
              && SpectrumOffscreenTestAccess::corrected(*secondView).isEmpty(),
              "identical stream IDs on different radios do not cross-feed spectrum");
        auto* fixtureSlice = firstRadio.radioModel().slice(0);
        int tuneIntents = 0;
        if (!fixtureSlice) { return 1; }
        QObject::connect(fixtureSlice, &SliceModel::frequencyCommandIssued, &firstPanel,
                         [&](double) { ++tuneIntents; });
        // Queue the actual widget signal, then retire/reuse the pan ID before
        // its delivery. The old PanadapterModel deliberately stays alive.
        auto delayedGesture = std::unique_ptr<QThread>(QThread::create([firstView] {
            emit firstView->frequencyClicked(14.100000);
        }));
        delayedGesture->start(); delayedGesture->wait();
        const QPointer<SpectrumWidget> retiredView(firstView);
        emit firstRadio.radioModel().panadapterRemoved(firstPan.panId());
        PanadapterModel replacement(firstPan.panId());
        emit firstRadio.radioModel().panadapterAdded(&replacement);
        SpectrumWidget* replacementView = nullptr;
        for (auto* candidate : firstPanel.findChildren<SpectrumWidget*>()) {
            if (candidate != firstView) { replacementView = candidate; }
        }
        check(replacementView != nullptr, "same pan ID can receive a new waterfall widget");
        if (!replacementView) { return 1; }
        QCoreApplication::processEvents();
        check(tuneIntents == 0, "queued gesture from a retired waterfall cannot tune a replacement with the same pan ID");
        const int beforeCurrentGesture = tuneIntents;
        emit replacementView->frequencyClicked(14.101000);
        check(tuneIntents == beforeCurrentGesture + 1, "current waterfall still sends its own receive-tuning intent");
        const int beforeReuse = tuneIntents;
        auto reusedModelGesture = std::unique_ptr<QThread>(QThread::create([replacementView] {
            emit replacementView->frequencyClicked(14.102000);
        }));
        reusedModelGesture->start(); reusedModelGesture->wait();
        const QPointer<SpectrumWidget> retiredReplacement(replacementView);
        emit firstRadio.radioModel().panadapterRemoved(replacement.panId());
        emit firstRadio.radioModel().panadapterAdded(&replacement);
        firstView = nullptr;
        for (auto* candidate : firstPanel.findChildren<SpectrumWidget*>()) {
            if (candidate != retiredView.data() && candidate != retiredReplacement.data()) { firstView = candidate; }
        }
        check(firstView != nullptr, "same pan model can receive a replacement waterfall widget");
        if (!firstView) { return 1; }
        QCoreApplication::processEvents();
        check(tuneIntents == beforeReuse, "reusing the same pan model cannot revive its old widget's queued gesture");
        emit firstView->frequencyClicked(14.103000);
        check(tuneIntents == beforeReuse + 1, "replacement view for the reused model still sends tuning intent");
        emit firstRadio.radioModel().capabilitiesChanged(true, caps);
        emit firstRadio.radioModel().panFeedSpectrumReady(replacement.panStreamId(),
            QVector<float>(256, -104.0f), 0, SpectrumDecodeScale{});
        emit replacement.infoChanged(14.2, 0.2);
        check(firstView->centerMhz() == 14.2 && firstView->bandwidthMhz() == 0.2,
              "current pan still delivers confirmed geometry");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto retained = SpectrumOffscreenTestAccess::corrected(*firstView);
        const double retainedCenter = firstView->centerMhz();
        const double retainedSpan = firstView->bandwidthMhz();
        const float retainedLevel = firstView->refLevel();
        const float retainedRange = firstView->dynamicRange();
        PanadapterModel latePan(QStringLiteral("0x40000001"));
        auto delayedModelUpdate = std::unique_ptr<QThread>(QThread::create([&] {
            emit firstRadio.radioModel().panFeedSpectrumReady(replacement.panStreamId(),
                QVector<float>(256, -54.0f), 0, SpectrumDecodeScale{});
            emit firstRadio.radioModel().capabilitiesChanged(false, caps);
            emit replacement.infoChanged(28.4, 0.1);
            emit replacement.levelChanged(-60.0f, 0.0f);
            emit firstRadio.radioModel().panadapterAdded(&latePan);
            emit firstRadio.radioModel().sliceAdded(fixtureSlice);
            emit firstRadio.radioModel().panadapterRemoved(replacement.panId());
        }));
        delayedModelUpdate->start(); delayedModelUpdate->wait();
        const qsizetype retainedViews = firstPanel.findChildren<SpectrumWidget*>().size();
        firstPanel.detach();
        QCoreApplication::processEvents();
        check(SpectrumOffscreenTestAccess::corrected(*firstView) == retained
              && !firstView->radioOwnsDbmScale(), "detached panel rejects already queued data and capability updates");
        check(firstView->centerMhz() == retainedCenter && firstView->bandwidthMhz() == retainedSpan,
              "detached panel rejects queued pan geometry updates");
        check(firstView->refLevel() == retainedLevel && firstView->dynamicRange() == retainedRange,
              "detached panel rejects queued pan level updates");
        check(firstPanel.findChildren<SpectrumWidget*>().size() == retainedViews,
              "detached panel ignores queued pan creation and removal");
        emit firstRadio.radioModel().panFeedSpectrumReady(firstPan.panStreamId(),
            QVector<float>(256, -64.0f), 0, SpectrumDecodeScale{});
        emit firstRadio.radioModel().capabilitiesChanged(false, caps);
        check(SpectrumOffscreenTestAccess::corrected(*firstView) == retained
              && !firstView->radioOwnsDbmScale(), "detached panel rejects later model data and capability changes");
    }
    return failures ? 1 : 0;
}


static int runStationRadioBar()
{
    int failures = 0;
    const auto check = [&](bool ok, const char* text) {
        std::printf("[%s] %s\n", ok ? "OK" : "FAIL", text); failures += !ok;
    };
    const auto waitFor = [](const auto& predicate) {
        QElapsedTimer timer; timer.start();
        while (!predicate() && timer.elapsed() < 2000) {
            QCoreApplication::processEvents(); QThread::msleep(1);
        }
        return predicate();
    };
    ConnectionPanel discovery;
    discovery.addDemoRadio();
    const auto radios = discovery.automationLocalRadios();
    if (radios.isEmpty()) { return 1; }
    RadioInfo info = radios.first();
    RadioModel primary;
    StationWorkspace workspace(&primary);
    StationWorkspaceTestAccess::add(workspace, info);
    info.address = QHostAddress(QStringLiteral("127.0.0.2"));
    StationWorkspaceTestAccess::add(workspace, info);
    auto& station = StationWorkspaceTestAccess::station(workspace);
    const auto ids = station.sessionIds();
    if (ids.size() != 2) { return 1; }
    auto* first = &station.session(ids.first())->radioModel();
    auto* second = &station.session(ids.last())->radioModel();
    const bool admitted = waitFor([&] {
        return first->controlSessionCurrent(first->controlSession())
            && second->controlSessionCurrent(second->controlSession());
    });
    check(admitted, "both demo control sessions finish admission before menu actions");
    if (!admitted) { return 1; }
    const bool slicesReady = waitFor([&] {
        return first->slices().size() == 1 && second->slices().size() == 1;
    });
    check(slicesReady, "both demo receivers publish their initial slice before isolation checks");
    if (!slicesReady) { return 1; }
    const auto tabs = workspace.receiverTabs();
    check(tabs.size() == 2 && tabs[0].id != tabs[1].id && tabs[0].name != tabs[1].name,
        "same-type radios with identical reported serials have distinct tabs and endpoint labels");
    check(workspace.receiverModel(tabs[0].id) == first && workspace.receiverModel(tabs[1].id) == second,
        "each tab resolves only its owning model");
    RadioTabBar strip;
    strip.setRadios(tabs);
    QString contextId;
    QObject::connect(&strip, &RadioTabBar::radioContextMenuRequested, &strip,
        [&](const QString& id, const QPoint&) { contextId = id; });
    auto* tab = strip.findChild<RadioTab*>(QStringLiteral("radioTab_") + tabs[1].id);
    if (!tab) { return 1; }
    QContextMenuEvent event(QContextMenuEvent::Keyboard, QPoint(2,2), QPoint(2,2));
    QApplication::sendEvent(tab, &event);
    check(contextId == tabs[1].id, "keyboard context menu retains the selected receiver identity");
    auto* menu = workspace.createRadioMenu(tabs[1].id, &workspace);
    auto* add = menu->findChild<QAction*>(QStringLiteral("stationMenuAddSlice"));
    check(add && !add->isEnabled(), "single-slice demo advertises no spare slice slot");
    QString setupId;
    QObject::connect(&workspace, &StationWorkspace::setupRequested, &workspace,
        [&](const QString& id) { setupId = id; });
    auto* setup = menu->findChild<QAction*>(QStringLiteral("stationMenuSetup"));
    if (!setup) { return 1; }
    setup->trigger();
    check(setupId == tabs[1].id, "setup action targets its own radio instance");
    {
        RadioSetupDialog firstSetup(first), secondSetup(second);
        const QList<QPair<QString, QString>> entries{{tabs[0].id, tabs[0].name}, {tabs[1].id, tabs[1].name}};
        firstSetup.setStationRadios(entries, tabs[0].id, true);
        secondSetup.setStationRadios(entries, tabs[1].id, true);
        auto* firstPicker = firstSetup.findChild<QComboBox*>(QStringLiteral("stationSetupRadio"));
        auto* secondPicker = secondSetup.findChild<QComboBox*>(QStringLiteral("stationSetupRadio"));
        check(firstPicker && secondPicker && firstPicker->currentData() != secondPicker->currentData(),
            "same-type radio setup windows keep independent selected identities");
        bool sharedAudioDisabled = false;
        for (auto* tree : secondSetup.findChildren<QTreeWidget*>()) {
            QTreeWidgetItemIterator item(tree);
            while (*item) { if ((*item)->text(0) == QStringLiteral("Audio")) { sharedAudioDisabled = (*item)->isDisabled(); } ++item; }
        }
        check(sharedAudioDisabled, "additional setup cannot change shared PC audio devices");
        auto* stationName = secondSetup.findChild<QLineEdit*>(QStringLiteral("radioSetupStationName"));
        const auto savedName = AppSettings::instance().value("StationName", "");
        check(stationName && !stationName->isEnabled(), "shared station name is read-only in additional setup");
        if (stationName) {
            stationName->setText(QStringLiteral("Must not replace primary station"));
            emit stationName->editingFinished();
            check(AppSettings::instance().value("StationName", "") == savedName,
                "stale editing callback cannot change the primary station identity");
            stationName->setText(savedName.toString());
        }
        const QString preview = qEnvironmentVariable("AETHER_STATION_PREVIEW_DIR");
        if (!preview.isEmpty()) {
            strip.resize(900, 52); strip.show();
            secondSetup.resize(960, 800); secondSetup.show();
            QCoreApplication::processEvents();
            check(strip.grab().save(QDir(preview).filePath(QStringLiteral("station-radio-bar.png"))),
                "offscreen radio bar preview saved");
            check(secondSetup.grab().save(QDir(preview).filePath(QStringLiteral("station-radio-setup.png"))),
                "offscreen radio setup preview saved");
        }
    }
    // A normalized in-memory terminal records slice requests; no wire peer,
    // socket, sound device or transmit operation is involved.
    class SliceTerminal final : public IRadioBackend {
    public:
        int creates = 0;
        int capacity = 2;
        bool live = true;
        QString lastPan;
        RadioCapabilities capabilities() const override {
            RadioCapabilities caps; caps.maxSlices = capacity; caps.maxPanadapters = 1;
            caps.canCreateSlices = true; return caps;
        }
        bool isConnected() const override { return live; }
        void connectRadio(const RadioConnectRequest&) override { live = true; }
        void disconnectRadio() override { live = false; }
        bool createSlice(const QString& pan, double) override { ++creates; lastPan = pan; return true; }
        void setSliceFrequency(int, double) override {}
        void setSliceMode(int, const QString&) override {}
        void setSliceFilter(int, int, int) override {}
        void setSliceAgc(int, const QString&, int) override {}
        void setPanCenter(const QString&, double, PanCenterIntent) override {}
        void setKeying(bool, const TxCoordinator::Operation&, const TxCoordinator::Completion&) override {}
        void invokeExtension(const QString&, const QString&, quint64, const QVariant&) override {}
    };
    second->disconnectFromRadio();
    if (!waitFor([&] { return !second->isConnected(); })) { return 1; }
    auto terminal = std::make_unique<SliceTerminal>();
    auto* sink = terminal.get();
    second->setBackendForTest(std::move(terminal), QStringLiteral("station-test"));
    const QString panId = QStringLiteral("test:owned-pan");
    emit sink->panCenterBandwidthChanged(panId, 14.1, 0.012);
    SliceDelta slice;
    slice.panId = panId; slice.frequency = 14.1; slice.mode = QStringLiteral("USB");
    slice.inUse = true; slice.active = true;
    emit sink->sliceChanged(0, slice);
    auto* currentMenu = workspace.createRadioMenu(tabs[1].id, &workspace);
    auto* currentAdd = currentMenu->findChild<QAction*>(QStringLiteral("stationMenuAddSlice"));
    check(currentAdd && currentAdd->isEnabled(), "spare receive capacity enables Add slice");
    if (!currentAdd) { return 1; }
    currentAdd->trigger();
    check(sink->creates == 1 && sink->lastPan == panId && first->slices().size() == 1,
        "Add slice reaches only the selected receiver and its own pan");
    sink->capacity = 1;
    currentAdd->trigger();
    check(sink->creates == 1, "capacity is rechecked when an already open menu is used");
    sink->capacity = 2;
    second->setBackendForTest(std::make_unique<SliceTerminal>(), QStringLiteral("station-test"));
    auto* replacement = static_cast<SliceTerminal*>(second->backend());
    emit replacement->panCenterBandwidthChanged(panId, 14.1, 0.012);
    emit replacement->sliceChanged(0, slice);
    currentAdd->trigger();
    check(replacement->creates == 0,
        "menu from a replaced backend cannot add a slice to the new session");

    setupId.clear();
    second->disconnectFromRadio();
    if (!waitFor([&] { return !second->isConnected(); })) { return 1; }
    setup->trigger();
    check(setupId.isEmpty(), "open menu refuses setup after its connection is retired");
    check(first->isConnected() && !first->stationTransmitAllowed() && !second->stationTransmitAllowed(),
        "receiver menus do not disconnect the other radio or grant transmit");
    station.removeSession(ids.last());
    check(!workspace.receiverModel(tabs[1].id) && !workspace.activateReceiver(tabs[1].id),
        "removed receiver identity cannot target the surviving radio");
    station.removeSession(ids.first());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return failures ? 1 : 0;
}

static int runStationReconnect()
{
    int failures = 0;
    const auto check = [&](bool ok, const char* text) {
        std::printf("[%s] %s\n", ok ? "OK" : "FAIL", text); failures += !ok;
    };
    const auto waitFor = [](const auto& predicate) {
        QElapsedTimer timer; timer.start();
        while (!predicate() && timer.elapsed() < 2000) {
            QCoreApplication::processEvents(); QThread::msleep(1);
        }
        return predicate();
    };
    // The built-in demo is the subject, not a peer emulating radio firmware.
    // No sockets, AudioEngine or device output is opened by these sessions.
    ConnectionPanel discovery;
    discovery.addDemoRadio();
    const auto radios = discovery.automationLocalRadios();
    if (radios.isEmpty()) { return 1; }
    RadioInfo firstInfo = radios.first();
    RadioInfo secondInfo = firstInfo;
    secondInfo.address = QHostAddress(QStringLiteral("127.0.0.2"));
    RadioModel primary;
    StationWorkspace workspace(&primary);
    auto& station = StationWorkspaceTestAccess::station(workspace);
    StationWorkspaceTestAccess::open(workspace, -1);
    auto* firstForm = StationWorkspaceTestAccess::form(workspace);
    if (!firstForm) { return 1; }
    emit firstForm->connectRequested(firstInfo);
    emit firstForm->connectRequested(firstInfo);
    check(station.sessionIds().size() == 1, "a submitted form cannot create a duplicate receiver from a late reply");
    if (station.sessionIds().size() != 1) { return 1; }
    const int firstId = station.sessionIds().first();
    auto* first = station.session(firstId);
    if (!waitFor([&] { return first->radioModel().isConnected(); })) { return 1; }
    auto* panel = StationWorkspaceTestAccess::panel(workspace, firstId);
    auto* reconnect = panel->findChild<QPushButton*>(QStringLiteral("stationReconnectReceiver"));
    check(reconnect && !reconnect->isEnabled(), "connected receiver disables Reconnect");
    if (!reconnect) { return 1; }
    first->radioModel().disconnectFromRadio();
    const bool recoverable = waitFor([&] { return !first->radioModel().isConnected() && reconnect->isEnabled(); });
    check(recoverable, "disconnected receiver exposes manual recovery");
    if (!recoverable) { return 1; }
    reconnect->click();
    QPointer<ConnectionPanel> retiredForm(StationWorkspaceTestAccess::form(workspace));
    if (!retiredForm) { return 1; }
    QMetaObject::invokeMethod(retiredForm, [retiredForm, firstInfo] {
        if (retiredForm) { emit retiredForm->connectRequested(firstInfo); }
    }, Qt::QueuedConnection);
    StationWorkspaceTestAccess::open(workspace, -1);
    auto* addForm = StationWorkspaceTestAccess::form(workspace);
    QCoreApplication::processEvents();
    check(!first->radioModel().isConnected(), "superseded form cannot reconnect a receiver through a queued reply");
    if (!addForm) { return 1; }
    emit addForm->connectRequested(secondInfo);
    if (station.sessionIds().size() != 2) { return 1; }
    const int secondId = station.sessionIds().last();
    auto* second = station.session(secondId);
    if (!waitFor([&] { return second->radioModel().isConnected(); })) { return 1; }
    auto* monitor = workspace.findChild<QComboBox*>(QStringLiteral("stationMonitorSource"));
    monitor->setCurrentIndex(monitor->findData(secondId + 1));
    emit monitor->activated(monitor->currentIndex());
    reconnect->click();
    auto* form = StationWorkspaceTestAccess::form(workspace);
    if (!form || form == addForm) { return 1; }
    const auto* host = form->findChild<QLineEdit*>(QStringLiteral("connectionManualIp"));
    check(host && host->text() == firstInfo.address.toString(), "reconnect form restores its own receiver address");
    auto* connectButton = form->findChild<QPushButton*>(QStringLiteral("connectionManualConnectButton"));
    emit second->radioModel().connectionStateChanged(true);
    check(connectButton && connectButton->isEnabled(), "another receiver's status cannot acknowledge the current connection form");
    emit form->connectRequested(secondInfo);
    check(!first->radioModel().isConnected() && second->radioModel().isConnected(),
          "reconnect refuses a changed endpoint without disturbing either receiver");
    emit form->connectWithOptionsRequested(firstInfo, QVariantMap{});
    check(waitFor([&] { return first->radioModel().isConnected(); }) && station.sessionIds().size() == 2
          && StationWorkspaceTestAccess::panel(workspace, firstId) == panel,
          "fresh form reconnects the existing session and panel without consuming another slot");
    check(second->radioModel().isConnected() && monitor->currentData().toInt() == secondId + 1
          && !first->radioModel().stationTransmitAllowed(),
          "other receiver, listening selection and receive-only permission survive reconnect");
    first->radioModel().disconnectFromRadio();
    if (!waitFor([&] { return !first->radioModel().isConnected() && reconnect->isEnabled(); })) { return 1; }
    // An injected terminal holds a connect attempt pending. It has no parser,
    // peer, socket or device; failures enter the real model's error slot.
    class PendingConnect final : public IRadioBackend {
    public:
        int attempts = 0;
        QVariantMap lastOptions;
        RadioCapabilities capabilities() const override {
            RadioCapabilities caps;
            caps.connectionRecovery = ConnectionRecovery{ConnectionRecovery::Mode::Manual};
            return caps;
        }
        bool isConnected() const override { return false; }
        void connectRadio(const RadioConnectRequest& request) override { ++attempts; lastOptions = request.params; }
        void disconnectRadio() override {}
        void setKeying(bool, const TxCoordinator::Operation&, const TxCoordinator::Completion&) override {}
        void setSliceFrequency(int, double) override {}
        void setSliceMode(int, const QString&) override {}
        void setSliceFilter(int, int, int) override {}
        void setSliceAgc(int, const QString&, int) override {}
        void setPanCenter(const QString&, double, PanCenterIntent) override {}
        void invokeExtension(const QString&, const QString&, quint64, const QVariant&) override {}
    };
    auto pending = std::make_unique<PendingConnect>();
    auto* terminal = pending.get();
    first->radioModel().setBackendForTest(std::move(pending), firstInfo.family);
    const auto reject = [&] {
        return QMetaObject::invokeMethod(&first->radioModel(), "onConnectionError", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("Injected offline connection refusal")));
    };
    for (int attempt = 1; attempt <= 3; ++attempt) {
        reconnect->click();
        form = StationWorkspaceTestAccess::form(workspace);
        if (!form || !form->isVisible()) { return 1; }
        const QVariantMap options = attempt < 3
            ? QVariantMap{{QStringLiteral("test.attemptTag"), attempt}} : QVariantMap{};
        emit form->connectWithOptionsRequested(firstInfo, options);
        check(terminal->attempts == attempt && first->radioModel().isConnectAttemptInFlight()
              && !reconnect->isEnabled(), "pending attempt disables duplicate manual recovery");
        check(attempt < 3 ? terminal->lastOptions.value(QStringLiteral("test.attemptTag")).toInt() == attempt
                          : !terminal->lastOptions.contains(QStringLiteral("test.attemptTag")),
              "each retry receives only its newly submitted options");
        check(reject() && !first->radioModel().isConnectAttemptInFlight() && reconnect->isEnabled(),
              "terminal error without disconnect immediately restores Reconnect");
    }
    check(second->radioModel().isConnected() && monitor->currentData().toInt() == secondId + 1,
          "failed retries leave the other receiver and listening selection intact");
    station.removeSession(firstId);
    emit form->connectRequested(firstInfo);
    check(station.sessionIds().size() == 1 && second->radioModel().isConnected(),
          "late reply for a removed receiver cannot recreate it or disturb the other session");
    station.removeSession(secondId);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return failures ? 1 : 0;
}

static int runStationTransmitPermission()
{
    // State/terminal recorder only: no protocol parser, peer, socket or device.
    class Backend final : public IRadioBackend {
    public:
        bool connected{true};
        bool canTransmit{true};
        int keyCalls{0};
        RadioCapabilities capabilities() const override {
            RadioCapabilities caps; caps.canTransmit = canTransmit; return caps;
        }
        bool isConnected() const override { return connected; }
        void connectRadio(const RadioConnectRequest&) override {}
        void disconnectRadio() override { connected = false; }
        void setSliceFrequency(int, double) override {}
        void setSliceMode(int, const QString&) override {}
        void setSliceFilter(int, int, int) override {}
        void setSliceAgc(int, const QString&, int) override {}
        void setPanCenter(const QString&, double, PanCenterIntent) override {}
        void setKeying(bool, const TxCoordinator::Operation&, const TxCoordinator::Completion&) override { ++keyCalls; }
        void invokeExtension(const QString&, const QString&, quint64, const QVariant&) override {}
    };
    int failures = 0;
    const auto check = [&](bool ok, const char* text) {
        std::printf("[%s] %s\n", ok ? "OK" : "FAIL", text); failures += !ok;
    };
    RadioModel primary;
    auto owned = std::make_unique<Backend>();
    auto* backend = owned.get();
    primary.setBackendForTest(std::move(owned), QStringLiteral("test"));
    const auto microphoneProducer = primary.registerTxProducer(&primary, true);
    auto microphone = primary.captureTxMedia(microphoneProducer);
    const auto oldMicrophone = microphone;
    {
        StationWorkspace workspace(&primary);
        QObject::connect(&workspace, &StationWorkspace::primaryTransmitPermissionChanged,
                         &primary, [&](bool allowed) {
            microphone = allowed ? primary.captureTxMedia(microphoneProducer) : TxCoordinator::Context{};
        });
        auto* permit = workspace.findChild<QCheckBox*>(QStringLiteral("stationTransmitPrimary"));
        auto* monitor = workspace.findChild<QComboBox*>(QStringLiteral("stationMonitorSource"));
        check(permit && permit->isEnabled() && !permit->isChecked() && !primary.stationTransmitAllowed(),
              "opening station controls requires explicit primary transmit designation");
        if (!permit || !monitor) { return 1; }
        permit->click();
        check(primary.stationTransmitAllowed() && backend->keyCalls == 0,
              "primary designation grants eligibility without keying");
        check(microphone.permitsDispatch(TxCoordinator::monotonicMs())
              && !oldMicrophone.permitsDispatch(TxCoordinator::monotonicMs()),
              "designation refreshes microphone authorization without reviving queued old audio");
        monitor->setCurrentIndex(monitor->findData(-1));
        emit monitor->activated(monitor->currentIndex());
        check(primary.stationTransmitAllowed(), "listening selection cannot move transmit designation");
        backend->canTransmit = false;
        emit primary.capabilitiesChanged(true, backend->capabilities());
        check(!primary.stationTransmitAllowed() && !permit->isEnabled() && !permit->isChecked(),
              "receive-only capability revokes and disables designation");
        backend->canTransmit = true;
        emit primary.capabilitiesChanged(true, backend->capabilities());
        check(permit->isEnabled() && !permit->isChecked(), "capability return cannot auto-designate");
        permit->click();
        backend->connected = false;
        emit primary.connectionStateChanged(false);
        check(!primary.stationTransmitAllowed() && !permit->isChecked() && !permit->isEnabled(),
              "disconnect clears station transmit permission");
        backend->connected = true;
        emit primary.connectionStateChanged(true);
        check(permit->isEnabled() && !primary.stationTransmitAllowed(), "reconnect requires fresh designation");
        permit->click();
    }
    check(!primary.stationTransmitAllowed() && backend->keyCalls == 0,
          "destroying station controls revokes permission without idle transport writes");
    return failures ? 1 : 0;
}

// Exercise rotation and overflow without connecting to a radio.
static int runTabletStationLayout()
{
    qputenv("AETHER_TABLET_UI", "1");
    AppSettings::instance().load();
    int failures = 0;
    const auto check = [&](bool ok, const char* name) {
        std::printf("[%s] %s\n", ok ? "OK" : "FAIL", name);
        failures += !ok;
    };
    RadioModel primary;
    StationWorkspace workspace(&primary);
    RadioSession receiver;
    receiver.setSessionId(1);
    receiver.setLabel(QStringLiteral("Additional receiver"));
    StationReceiverPanel panel(&receiver);
    const QString preview = qEnvironmentVariable("AETHER_STATION_PREVIEW_DIR");
    for (const QSize size : {QSize(820, 1000), QSize(1180, 720)}) {
        workspace.resize(size);
        workspace.show();
        panel.resize(size.width() - 48, 400);
        panel.show();
        QCoreApplication::processEvents();
        check(workspace.width() == size.width(), "station workspace fits requested tablet width");
        check(panel.width() == size.width() - 48, "receiver controls do not force tablet wider");
        auto* scroll = panel.findChild<QScrollArea*>();
        check(scroll && scroll->widget()->width() >= scroll->viewport()->width(),
              "receiver controls retain their width inside a scrollable viewport");
        for (auto* button : workspace.findChildren<QPushButton*>()) {
            if (button->isVisible()) {
                check(button->height() >= 44, "station action meets the 44-point touch target");
            }
        }
        if (!preview.isEmpty()) {
            const QString orientation = size.width() < size.height() ? "portrait" : "landscape";
            check(workspace.grab().save(QDir(preview).filePath("tablet-station-" + orientation + ".png")),
                  "tablet station preview saved");
            check(panel.grab().save(QDir(preview).filePath("tablet-receiver-" + orientation + ".png")),
                  "tablet receiver preview saved");
        }
    }
    return failures ? 1 : 0;
}

int main(int argc, char** argv)
{
    TestSettingsProfile profile("spectrum-confirmed-geometry");
    if (!profile.isValid()) { return 2; }
    QApplication app(argc, argv);
    for (const char* font : {":/fonts/Inter-Regular.ttf", ":/fonts/Inter-Bold.ttf"}) {
        if (QFontDatabase::addApplicationFont(QString::fromLatin1(font)) < 0) { return 1; }
    }
    QApplication::setFont(QFont(QStringLiteral("Inter"), 10));
    if (app.arguments().contains(QStringLiteral("--tablet-station-layout"))) {
        return runTabletStationLayout();
    }
    if (app.arguments().contains(QStringLiteral("--station-radio-bar"))) {
        return runStationRadioBar();
    }
    if (app.arguments().contains(QStringLiteral("--station-reconnect"))) {
        return runStationReconnect();
    }
    if (app.arguments().contains(QStringLiteral("--station-transmit-permission"))) {
        return runStationTransmitPermission();
    }
    if (app.arguments().contains(QStringLiteral("--station-display-isolation"))) {
        return runStationDisplayIsolation();
    }
    if (app.arguments().contains(QStringLiteral("--flex-squelch-bridge-fixture"))) {
        return runFlexSquelchBridgeFixture(app);
    }
    if (app.arguments().contains(QStringLiteral("--pan-pixels-retina"))) {
        return runPanPixelsAtRetinaScale();
    }
    if (app.arguments().contains(QStringLiteral("--trace-bins"))) {
        return runTraceBins();
    }
    int failures = 0;
    const auto check = [&](bool ok, const char* name) {
        std::printf("[%s] %s\n", ok ? "OK" : "FAIL", name);
        failures += !ok;
    };
    {
        auto sqlStorage = std::make_unique<SpectrumWidget>();
        auto& sql = *sqlStorage;
        sql.setSquelchScale(legacyDbmSquelchScale());
        sql.setAutoSquelchEnable(true);
        sql.setSquelchLine(true, 60);
        int suggestions = 0;
        QObject::connect(&sql, &SpectrumWidget::autoSquelchLevelSuggested,
                         &sql, [&](int) { ++suggestions; });
        SpectrumOffscreenTestAccess::sqlBins(sql);
        QImage empty(800, 300, QImage::Format_ARGB32);
        empty.fill(Qt::black);
        check(SpectrumOffscreenTestAccess::sqlImage(sql) != empty,
              "Auto produces visible SQL/floor lines before disable");
        check(suggestions > 0, "eligible Auto produces threshold suggestions");
        sql.setSquelchLine(false, 0);
        sql.setAutoSquelchEnable(true, false);
        check(SpectrumOffscreenTestAccess::sqlImage(sql) == empty,
              "background same-pan estimator hides floor for selected non-Auto slice");
        sql.setSquelchLine(true, 60);
        check(SpectrumOffscreenTestAccess::sqlHideTimerActive(sql),
              "manual line retains its hide timer while a background slice runs Auto");
        sql.setAutoSquelchEnable(false);
        check(SpectrumOffscreenTestAccess::sqlImage(sql) == empty,
              "Auto disable clears both lines immediately without another FFT");
        const int before = suggestions;
        SpectrumOffscreenTestAccess::sqlBins(sql);
        check(suggestions == before, "disabled Auto produces no suggestions");
        sql.setAutoSquelchEnable(true);
        sql.setSquelchLine(true, 60);
        SpectrumOffscreenTestAccess::sqlBins(sql);
        sql.setSquelchScale(std::nullopt);
        sql.setSquelchLine(true, 69);
        check(SpectrumOffscreenTestAccess::sqlImage(sql) == empty,
              "unavailable scale blocks late threshold status and clears floor");
        const int unavailable = suggestions;
        SpectrumOffscreenTestAccess::sqlBins(sql);
        check(suggestions == unavailable, "unavailable scale stops estimator");
    }
    // Deterministic replay of the captured decode-before-release / ingest-after-
    // release ordering, including both shifts and a span change. The production
    // widget must correct inputs before smoothing and keep late apertures valid.
    const auto encodeThenDecode = [](const QVector<float>& physical,
                                     DbmRangeTransition::Range wire,
                                     SpectrumDecodeScale decoder) {
        QVector<float> bins = physical;
        for (float& bin : bins) {
            const float fraction = (wire.maxDbm - bin) / (wire.maxDbm - wire.minDbm);
            bin = decoder.maxDbm - fraction * (decoder.maxDbm - decoder.minDbm);
        }
        return bins;
    };
    const QVector<float> physical(256, -104.0f);
    const DbmRangeTransition::Range original{-165, -35};
    for (const DbmRangeTransition::Range target : {
             DbmRangeTransition::Range{-127.5f, 2.5f}, {-180, -50}, {-170, -90}}) {
        auto traceStorage = std::make_unique<SpectrumWidget>();
        auto& trace = *traceStorage;
        trace.setNoiseFloorEnable(false);
        trace.setDbmRange(original.minDbm, original.maxDbm);
        trace.setEncoderDbmRange(original.minDbm, original.maxDbm, 1);
        const SpectrumDecodeScale old{original.minDbm, original.maxDbm, 1};
        const SpectrumDecodeScale next{target.minDbm, target.maxDbm, 2};
        trace.updateSpectrum(physical, old);
        SpectrumOffscreenTestAccess::release(trace, original, target, 2);
        trace.updateSpectrum(physical, old); // Decoded before mouse-up, queued.
        check(SpectrumOffscreenTestAccess::releasePending(trace),
              "a queued pre-release decode cannot complete a new range transition");
        // A matching reply (including a rounded endpoint) confirms the command,
        // without discarding the guard needed by subsequent old-wire frames.
        trace.setDbmRange(target.minDbm + 0.004f, target.maxDbm + 0.004f);
        check(SpectrumOffscreenTestAccess::releasePending(trace),
              "a matching or rounded reply preserves the FFT frame guard");
        for (int frame = 0; frame < 3; ++frame) {
            trace.updateSpectrum(encodeThenDecode(physical, original, next), next);
            check(std::abs(SpectrumOffscreenTestAccess::trace(trace).front() + 104) < 0.01f,
                  "old-wire frames decoded under the new aperture never enter averaging with a false level");
        }
        trace.updateSpectrum(physical, next);
        check(!SpectrumOffscreenTestAccess::releasePending(trace),
              "an FFT with the requested decode and wire aperture completes the transition");
        trace.updateSpectrum(encodeThenDecode(physical, original, next), next);
        check(std::abs(SpectrumOffscreenTestAccess::trace(trace).front() + 104) < 0.01f,
              "a late old-wire frame remains corrected after target encoding was observed");
        trace.prepareForShutdown();
    }
    for (const bool gainChange : {true, false}) {
        auto traceStorage = std::make_unique<SpectrumWidget>();
        auto& trace = *traceStorage;
        trace.setNoiseFloorEnable(false);
        trace.setDbmRange(-165, -35);
        trace.setEncoderDbmRange(-165, -35, 1);
        trace.updateSpectrum(physical, {-165, -35, 1});
        const DbmRangeTransition::Range target{-127.5f, 2.5f};
        SpectrumOffscreenTestAccess::release(trace, original, target, 2);
        trace.updateSpectrum(physical, {-127.5f, 2.5f, 2});
        if (gainChange) {
            trace.setRfGain(32);
        } else {
            trace.setFrequencyRangeImmediate(14.224, 0.2);
        }
        const QVector<float> changedScene(256, -72);
        trace.updateSpectrum(changedScene, {-127.5f, 2.5f, 2});
        check(std::abs(SpectrumOffscreenTestAccess::corrected(trace).front() + 72) < 0.01f,
              "a changed RF gain or frequency discards prior-aperture comparisons and preserves genuine RF levels");
        trace.prepareForShutdown();
    }
    {
        auto traceStorage = std::make_unique<SpectrumWidget>();
        auto& trace = *traceStorage;
        trace.setNoiseFloorEnable(false);
        trace.setDbmRange(-165, -35);
        trace.setEncoderDbmRange(-165, -35, 1);
        trace.updateSpectrum(physical, {-165, -35, 1});
        const DbmRangeTransition::Range intermediate{-127.5f, 2.5f};
        const SpectrumDecodeScale middle{intermediate.minDbm, intermediate.maxDbm, 2};
        SpectrumOffscreenTestAccess::release(trace, original, intermediate, 2);
        SpectrumOffscreenTestAccess::release(trace, intermediate, original, 3);
        const SpectrumDecodeScale latest{original.minDbm, original.maxDbm, 3};
        trace.setDbmRange(-165, -35); // Unchanged-model successful reply.
        trace.updateSpectrum(encodeThenDecode(physical, original, middle), middle);
        check(SpectrumOffscreenTestAccess::releasePending(trace)
              && std::abs(SpectrumOffscreenTestAccess::trace(trace).front() + 104) < 0.01f,
              "a rapid reversal corrects the original wire aperture decoded under an intermediate request");
        trace.updateSpectrum(physical, {-165, -35, 1});
        check(SpectrumOffscreenTestAccess::releasePending(trace),
              "an A-to-B-to-A reversal cannot complete using an earlier A decode generation");
        trace.updateSpectrum(encodeThenDecode(physical, intermediate, latest), latest);
        check(std::abs(SpectrumOffscreenTestAccess::trace(trace).front() + 104) < 0.01f,
              "an intermediate wire aperture remains a candidate under the latest decoder");
        for (const DbmRangeTransition::Range intermediateEndpoint : {
                 DbmRangeTransition::Range{-165, 2.5f}, {-127.5f, -35}}) {
            trace.updateSpectrum(encodeThenDecode(physical, intermediateEndpoint, latest), latest);
            check(std::abs(SpectrumOffscreenTestAccess::trace(trace).front() + 104) < 0.01f,
                  "an intermediate single-endpoint aperture cannot inject a partial-scale dip into averaging");
        }
        trace.updateSpectrum(physical, latest);
        check(!SpectrumOffscreenTestAccess::releasePending(trace),
              "the latest decoder generation converges after rapid reversals");
        trace.prepareForShutdown();
    }
    {
        auto traceStorage = std::make_unique<SpectrumWidget>();
        auto& trace = *traceStorage;
        trace.resize(1000, 900);
        trace.setDbmRange(-135, -40);
        trace.setEncoderDbmRange(-135, -40);
        trace.setNoiseFloorPosition(25);
        trace.setNoiseFloorEnable(true);
        trace.updateSpectrum(QVector<float>(256, -100));
        const float lockedRef = trace.refLevel();
        // MainWindow retires the local-only auto-floor request synchronously.
        trace.cancelPendingDbmRangeChange();
        trace.setEncoderDbmRange(-160, -60);
        trace.setDbmRange(-160, -60);
        check(std::abs(trace.refLevel() - (-100 + 25.0f)) < .1f,
              "encoder range changes preserve floor placement instead of painting the encoder ceiling");
        trace.setRfGain(32);
        const float before = trace.refLevel();
        trace.updateSpectrum(QVector<float>(256, -95));
        check(near(trace.refLevel(), before) && SpectrumOffscreenTestAccess::acquiring(trace),
              "preamp fast lock cannot bypass the settle window");
        SpectrumOffscreenTestAccess::finishSettleWindow(trace);
        trace.updateSpectrum(QVector<float>(256, -105));
        wait(60);
        trace.updateSpectrum(QVector<float>(256, -104.7f));
        wait(60);
        trace.updateSpectrum(QVector<float>(256, -105.1f));
        std::printf("floor_lock acquiring=%d fresh=%d baseline=%.3f ref=%.3f span=%.3f\n",
            SpectrumOffscreenTestAccess::acquiring(trace), SpectrumOffscreenTestAccess::freshFrames(trace),
            SpectrumOffscreenTestAccess::baseline(trace), trace.refLevel(), trace.dynamicRange());
        check(!SpectrumOffscreenTestAccess::acquiring(trace)
              && SpectrumOffscreenTestAccess::freshFrames(trace) == 0
              && std::abs(trace.refLevel() - -80.0f) < .1f,
              "a settled fresh sequence acquires and snaps exactly once");
        const float settledBaseline = SpectrumOffscreenTestAccess::baseline(trace);
        trace.cancelPendingDbmRangeChange();
        trace.updateSpectrum(QVector<float>(256, -80));
        check(near(SpectrumOffscreenTestAccess::baseline(trace), settledBaseline),
              "the next large transient uses ordinary rejection instead of repeated cold acquisition");
        trace.setNoiseFloorEnable(false);
        trace.setEncoderDbmRange(-178, -88);
        SpectrumOffscreenTestAccess::allowHeadroom(trace);
        int requests = 0;
        float lower = -1, upper = -1;
        QObject::connect(&trace, &SpectrumWidget::radioDbmHeadroomRecoveryRequested,
                         &trace, [&](float lo, float hi) { ++requests; lower = lo; upper = hi; });
        QVector<float> sparse(256, -115);
        sparse[25] = -88.05f;
        trace.updateSpectrum(sparse);
        check(requests == 0,
              "with auto floor off a clipped peak never rewrites the radio's stored ceiling");
        QVector<float> clippedFloor(256, -178);
        clippedFloor[25] = -120;
        SpectrumOffscreenTestAccess::allowHeadroom(trace);
        trace.updateSpectrum(clippedFloor);
        check(requests == 0,
              "with auto floor off a clipped floor never rewrites the radio's stored floor");
        trace.setNoiseFloorEnable(true);
        SpectrumOffscreenTestAccess::allowHeadroom(trace);
        trace.updateSpectrum(sparse);
        check(requests == 1 && lower == 0 && upper == 24,
              "with auto floor on a sparse clipped peak requests upper headroom");
        trace.setPanBinsAbsolute(true);
        SpectrumOffscreenTestAccess::allowHeadroom(trace);
        trace.updateSpectrum(sparse);
        check(requests == 1, "host absolute bins never request pixel-encoder recovery");
        check(lockedRef > -100, "initial local floor lock was established");
    }
    {
        auto traceStorage = std::make_unique<SpectrumWidget>();
        auto& trace = *traceStorage;
        trace.setDbmRange(-100, -16);
        trace.setEncoderDbmRange(-100, -16);
        trace.setNoiseFloorPosition(25);
        trace.setNoiseFloorEnable(true);
        trace.updateSpectrum(QVector<float>(256, -80));
        trace.cancelPendingDbmRangeChange();
        int requests = 0;
        float lower = 0, upper = 0;
        QObject::connect(&trace, &SpectrumWidget::radioDbmHeadroomRecoveryRequested,
                         &trace, [&](float lo, float hi) { ++requests; lower = lo; upper = hi; });
        SpectrumOffscreenTestAccess::allowHeadroom(trace);
        trace.setRfGain(32);
        QVector<float> clipped(256, -100);
        clipped[25] = -40;
        trace.updateSpectrum(clipped);
        check(requests == 0 && !SpectrumOffscreenTestAccess::baselineValid(trace),
              "a preamp settling transient neither expands headroom nor acquires a clipped floor");
        SpectrumOffscreenTestAccess::allowHeadroom(trace);
        trace.updateSpectrum(clipped);
        check(requests == 1 && lower == 24 && upper == 0
              && !SpectrumOffscreenTestAccess::baselineValid(trace),
              "a persistent clipped floor requests recovery after settling without locking to the encoder boundary");
        SpectrumOffscreenTestAccess::finishSettleWindow(trace);
        trace.updateSpectrum(clipped);
        wait(60);
        trace.updateSpectrum(clipped);
        wait(60);
        trace.updateSpectrum(clipped);
        check(!SpectrumOffscreenTestAccess::baselineValid(trace)
              && SpectrumOffscreenTestAccess::acquiring(trace),
              "even stable clipped input cannot complete floor acquisition");
        trace.setEncoderDbmRange(-124, -16);
        SpectrumOffscreenTestAccess::finishSettleWindow(trace);
        trace.updateSpectrum(QVector<float>(256, -110));
        wait(60);
        trace.updateSpectrum(QVector<float>(256, -110));
        wait(60);
        trace.updateSpectrum(QVector<float>(256, -110));
        check(SpectrumOffscreenTestAccess::baselineValid(trace)
              && !SpectrumOffscreenTestAccess::acquiring(trace),
              "unclipped settled input completes floor recovery");
    }
    {
        // A pan another client owns: this session cannot request headroom, so
        // holding the baseline would stall auto floor for the whole session.
        auto traceStorage = std::make_unique<SpectrumWidget>();
        auto& trace = *traceStorage;
        trace.setDbmRange(-100, -16);
        trace.setEncoderDbmRange(-100, -16);
        trace.setNoiseFloorPosition(25);
        trace.setNoiseFloorEnable(true);
        trace.setDbmRangeCommandEligibility([] { return false; });
        check(!SpectrumOffscreenTestAccess::baselineValid(trace),
              "the foreign-pan case starts with no baseline");
        QVector<float> clipped(256, -100);
        clipped[25] = -40;
        SpectrumOffscreenTestAccess::finishSettleWindow(trace);
        trace.updateSpectrum(clipped);
        wait(60);
        trace.updateSpectrum(clipped);
        wait(60);
        trace.updateSpectrum(clipped);
        check(SpectrumOffscreenTestAccess::baselineValid(trace),
              "a clipped floor on a pan another client owns still acquires a baseline");
    }
    {
        // The floor already sits at Flex's -180 dBm limit: no recovery can
        // come, so holding the baseline would stall auto floor for good.
        auto traceStorage = std::make_unique<SpectrumWidget>();
        auto& trace = *traceStorage;
        trace.setDbmRange(-180, -90);
        trace.setEncoderDbmRange(-180, -90);
        trace.setNoiseFloorPosition(25);
        trace.setNoiseFloorEnable(true);
        int requests = 0;
        QObject::connect(&trace, &SpectrumWidget::radioDbmHeadroomRecoveryRequested,
                         &trace, [&](float, float) { ++requests; });
        check(!SpectrumOffscreenTestAccess::baselineValid(trace),
              "the floor-at-limit case starts with no baseline");
        QVector<float> clipped(256, -180);
        clipped[25] = -120;
        SpectrumOffscreenTestAccess::finishSettleWindow(trace);
        SpectrumOffscreenTestAccess::allowHeadroom(trace);
        trace.updateSpectrum(clipped);
        wait(60);
        trace.updateSpectrum(clipped);
        wait(60);
        trace.updateSpectrum(clipped);
        check(requests == 0 && SpectrumOffscreenTestAccess::baselineValid(trace),
              "a floor already at -180 dBm is not held for a recovery that cannot come");
    }
    {
        auto traceStorage = std::make_unique<SpectrumWidget>();
        auto& trace = *traceStorage;
        trace.updateSpectrum(QVector<float>(32,-80));
        trace.updateSpectrum(QVector<float>(32,-20));
        check(std::abs(SpectrumOffscreenTestAccess::trace(trace).front()+59)<.001,
              "undeclared backends retain the existing client EMA");
        trace.setClientFftSmoothingEnabled(false);
        trace.updateSpectrum(QVector<float>(32,-40));
        trace.updateSpectrum(QVector<float>(32,-10));
        check(SpectrumOffscreenTestAccess::trace(trace)==QVector<float>(32,-10),
              "backend-averaged bins reach the displayed trace without a second EMA");
#ifdef AETHER_BACKEND_RTL
        const auto caps=rtl::RtlSdrBackend().capabilities();
        const auto averaging=caps.backendPanAveraging.value_or(BackendPanAveraging{0,false,{},{}});
        auto* menu=trace.overlayMenu();
        menu->setFftAverageDescriptions(averaging.averageDescription,averaging.weightedDescription);
        auto* avg=trace.findChild<QSlider*>("displayFftAvgSlider");
        auto* weighted=trace.findChild<QPushButton*>("displayWeightedAvgBtn");
        check(avg && weighted && avg->accessibleDescription().contains("10 ms")
            && weighted->accessibleDescription().contains("dB levels"),
            "averaging units and domain reach keyboard and screen-reader descriptions");
        menu->setFftAverageDescriptions({},{});
        check(avg && weighted && avg->accessibleDescription().isEmpty()
            && weighted->toolTip().contains("Weights recent"),
            "switching to an undeclared family restores its original wording");
#endif
        trace.observeFrequencyRange(100.0, 0.01875);
        trace.updateSpectrum(QVector<float>(512, -80));
        QVector<float> capture(65536, -90);
        capture[1000] = -15; // Real captured signal far outside this tiny view.
        check(SpectrumOffscreenTestAccess::supplemental(trace, capture, 98.8, 101.2, true) == capture,
            "same-scale FFT coverage retains every offscreen level even with too little overlap for intensity calibration");
        check(SpectrumOffscreenTestAccess::supplemental(trace, capture, 98.8, 101.2, false).isEmpty(),
            "unrelated intensity tiles retain the existing minimum-overlap rule");
        trace.setSpectrumRenderMode(1);
        SpectrumOffscreenTestAccess::prepareHistory(trace);
        trace.updateSpectrum(QVector<float>(512, -20)); // A newer trace than the queued history row.
        const int rowsBefore = SpectrumOffscreenTestAccess::dss(trace).rowCount();
        trace.updateWaterfallRow(QVector<float>(65536, -80), 98.8, 101.2, 1, true);
        const DssRenderer& dss = SpectrumOffscreenTestAccess::dss(trace);
        std::printf("coherent_row before=%d after=%d primary=%.3f supplemental=%.3f\n",
            rowsBefore, dss.rowCount(), dss.rowDataRing(dss.headRing())[300],
            dss.rowSupplementalDataRing(dss.headRing())[300]);
        check(dss.rowCount() == rowsBefore + 1 && dss.rowDataRing(dss.headRing())[300] == -80
            && dss.rowSupplementalDataRing(dss.headRing())[300] == -80,
            "coherent history derives its close view and overhang from the same observation despite a newer live trace");
        capture.fill(-90);
        capture[32512 + 100] = -15;
        capture[32512 + 114] = -20;
        trace.updateWaterfallRow(capture, 98.8, 101.2, 2, true);
        const float* close = dss.rowDataRing(dss.headRing());
        check(near(dss.rowBandwidthMhzAtAge(0), .01875)
            && close[150] == -15 && close[171] == -20 && close[160] == -90,
            "primary close-view density preserves two narrow peaks and their valley beside wide supplemental coverage");
        trace.observeFrequencyRange(101.195, .03);
        trace.updateWaterfallRow(capture, 98.8, 101.2, 3, true);
        const double edgeLow = dss.rowCenterMhzAtAge(0) - dss.rowBandwidthMhzAtAge(0) / 2;
        const double edgeHigh = dss.rowCenterMhzAtAge(0) + dss.rowBandwidthMhzAtAge(0) / 2;
        check(edgeLow <= 101.18 && edgeLow >= 101.18 - 2.4 / 65536
            && near(edgeHigh, 101.2) && near(dss.rowSupplementalBandwidthMhzAtAge(0), 2.4),
            "partial right-edge coverage is stamped only through the last actual capture bin");
        trace.observeFrequencyRange(102, .03);
        trace.updateWaterfallRow(capture, 98.8, 101.2, 4, true);
        check(near(dss.rowCenterMhzAtAge(0), 100) && near(dss.rowBandwidthMhzAtAge(0), 2.4),
            "an out-of-capture view keeps the row's real RF frame instead of labeling absent frequencies");
        trace.observeFrequencyRange(101.195, .03);
        SpectrumOffscreenTestAccess::preview(trace, 101.19, 101.195, .03);
        trace.updateWaterfallRow(capture, 98.8, 101.2, 5, true);
        check(near(dss.rowCenterMhzAtAge(0) + dss.rowBandwidthMhzAtAge(0) / 2, 101.2),
            "a zoom preview preserves explicit coherent RF bounds instead of relabeling a padded preview frame");
    }
    auto widgetStorage = std::make_unique<SpectrumWidget>();
    auto& widget = *widgetStorage;
    widget.resize(1200, 600);
    widget.setBandwidthLimits(0.01, 2.16);
    widget.setPanGeometryConfirmationRequired(true);
    widget.observeFrequencyRange(460.3, 0.125);
    widget.setSliceOverlay(0, 460.3, -5000, 5000, false, true, "FMN");
    double requestedCenter = 0;
    QObject::connect(&widget, &SpectrumWidget::centerChangeRequested,
        &widget, [&](double center) { requestedCenter = center; });
    widget.setFrequencyRange(460.4, 0.25);
    widget.setFrequencyRangeImmediate(460.4, 0.25);
    check(near(widget.centerMhz(), 460.3) && near(widget.bandwidthMhz(), 0.125),
          "local recenter/shortcut preview cannot replace confirmed range");
    mouse(widget, QEvent::MouseButtonPress, 200, 150);
    mouse(widget, QEvent::MouseMove, 300, 150);
    check(requestedCenter < 460.3, "real pan gesture emits intent");
    const double latestIntent = requestedCenter;
    check(near(widget.centerMhz(), 460.3), "pending drag leaves accepted axis intact");
    widget.observeFrequencyRange(460.3, 0.125); // clamp/refusal, no changed geometry
    QVector<float> bins(108, -100.0f);
    bins[40] = -30.0f;
    widget.updateSpectrum(bins);
    check(near(widget.centerMhz(), 460.3)
          && widget.automationDssSnapshot().value("dssInputFftBins").toInt() == 108,
          "refused drag keeps truthful axis AND ingests live FFT");
    widget.observeFrequencyRange(460.289453125, 0.125);
    check(near(widget.centerMhz(), 460.289453125),
          "quantized acceptance applies while pointer remains held");
    mouse(widget, QEvent::MouseButtonRelease, 300, 150);
    check(near(requestedCenter, latestIntent), "release flushes intent rather than stale accepted center");
    check(near(widget.centerMhz(), 460.289453125), "release cannot move accepted axis");
    wait(400);
    check(near(widget.centerMhz(), 460.289453125), "settle timers cannot restore optimistic center");

    double zoomCenter = 0;
    double zoomSpan = 0;
    QObject::connect(&widget, &SpectrumWidget::frequencyRangeChangeRequested,
        &widget, [&](double center, double span) { zoomCenter = center; zoomSpan = span; });
    const QPointF point(400, 150);
    QWheelEvent wheel(point, widget.mapToGlobal(point.toPoint()), {}, QPoint(0, 120),
        Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&widget, &wheel);
    check(zoomSpan > 0 && zoomSpan < 0.125 && near(widget.bandwidthMhz(), 0.125),
          "Ctrl-wheel requests zoom without claiming unaccepted span");
    widget.observeFrequencyRange(460.29, 0.083203125);
    check(near(widget.centerMhz(), 460.29) && near(widget.bandwidthMhz(), 0.083203125),
          "accepted zoom is not rejected by settling-gesture guard");
    const double requestedZoomCenter = zoomCenter;
    const double requestedZoomSpan = zoomSpan;
    wait(400);
    check(near(zoomCenter, requestedZoomCenter) && near(zoomSpan, requestedZoomSpan),
          "zoom settle flushes requested pair without rewriting accepted pair");
    check(near(widget.centerMhz(), 460.29) && near(widget.bandwidthMhz(), 0.083203125),
          "zoom settle keeps confirmed geometry");

    // Drag the actual frequency scale. Release must retain the last request
    // even when a different quantized range arrives before the mouse-up.
    const int scaleY = (widget.spectrumPixelHeight()
        + widget.height() - widget.waterfallPixelHeight()) / 2;
    mouse(widget, QEvent::MouseButtonPress, 400, scaleY);
    mouse(widget, QEvent::MouseMove, 500, scaleY);
    const double scaleCenter = zoomCenter;
    const double scaleSpan = zoomSpan;
    check(!near(scaleSpan, 0.083203125) && near(widget.bandwidthMhz(), 0.083203125),
          "frequency-scale drag requests span while holding accepted geometry");
    widget.observeFrequencyRange(460.291, 0.1);
    mouse(widget, QEvent::MouseButtonRelease, 500, scaleY);
    check(near(zoomCenter, scaleCenter) && near(zoomSpan, scaleSpan),
          "frequency-scale release preserves final requested pair");
    check(near(widget.centerMhz(), 460.291) && near(widget.bandwidthMhz(), 0.1),
          "frequency-scale release preserves accepted geometry");

    // A queued zoom from the old session must not escape after clear/reconnect.
    wait(60);
    QApplication::sendEvent(&widget, &wheel);
    const double beforeClearRequest = zoomSpan;
    widget.clearDisplay();
    widget.observeFrequencyRange(144.5, 0.25);
    wait(400);
    check(near(zoomSpan, beforeClearRequest) && near(widget.centerMhz(), 144.5),
          "disconnect clears pending gesture intents before a new session");

    widget.observeFrequencyRange(460.3, 0.125);
    double edgeCenter = 0;
    QObject::connect(&widget, &SpectrumWidget::edgePanTuneRequested,
        &widget, [&](double center, double) { edgeCenter = center; });
    mouse(widget, QEvent::MouseButtonPress, 585, 150);
    mouse(widget, QEvent::MouseMove, 1190, 150);
    wait(150);
    check(edgeCenter > 460.3 && near(widget.centerMhz(), 460.3),
          "VFO edge pan requests capture movement without moving unaccepted axis");
    widget.observeFrequencyRange(460.301171875, 0.125);
    check(near(widget.centerMhz(), 460.301171875),
          "VFO drag accepts confirmed geometry despite echo hold");
    mouse(widget, QEvent::MouseButtonRelease, 1190, 150);

    int cancelled = 0;
    int released = 0;
    QObject::connect(&widget, &SpectrumWidget::sliceDragCancelled,
        &widget, [&]() { ++cancelled; });
    QObject::connect(&widget, &SpectrumWidget::sliceDragActiveChanged,
        &widget, [&](bool active) { if (!active) { ++released; } });
    widget.observeFrequencyRange(460.3, 0.125);
    mouse(widget, QEvent::MouseButtonPress, 585, 150);
    mouse(widget, QEvent::MouseMove, 1190, 150);
    wait(100);
    widget.clearDisplay();
    const double cancelledEdgeCenter = edgeCenter;
    widget.observeFrequencyRange(144.5, 0.25);
    wait(150);
    check(cancelled == 1 && released == 0,
          "VFO cancellation releases owner hold without normal-release intent");
    check(near(edgeCenter, cancelledEdgeCenter) && near(widget.centerMhz(), 144.5),
          "cancelled VFO edge timer cannot send an old-session request");

    // Same real widget with the policy disabled pins the existing Flex path.
    widget.setPanGeometryConfirmationRequired(false);
    widget.setFrequencyRangeImmediate(460.3, 0.125);
    mouse(widget, QEvent::MouseButtonPress, 200, 150);
    mouse(widget, QEvent::MouseMove, 300, 150);
    const double optimistic = widget.centerMhz();
    check(optimistic < 460.3, "Flex pan retains immediate optimistic preview");
    widget.observeFrequencyRange(460.3, 0.125);
    check(near(widget.centerMhz(), optimistic), "Flex retains stale-echo hold during drag");
    mouse(widget, QEvent::MouseButtonRelease, 300, 150);
#ifdef AETHER_BACKEND_RTL
    // Real marker -> the owner dispatch helper -> RadioModel -> RTL worker.
    // Routing reveal as Range must fail: that request cannot leave capture.
    {
        RadioModel model;
        check(model.rebuildBackendForTest("rtl"), "offscreen RTL model initialized");
        auto& backend = *static_cast<rtl::RtlSdrBackend*>(model.backend());
        auto usb = std::make_shared<test::DeviceState>();
        rtl::RtlCaptureBackendTestAccess::start(backend, std::make_unique<test::InjectedDevice>(usb));
        usb->releaseReadback();
        const auto settle = [&]() {
            for (int i = 0; i < 2500; ++i) {
                usb->block(); QApplication::processEvents(); QThread::msleep(2);
                if (model.slice(0) && rtl::RtlCaptureBackendTestAccess::idle(backend)) { return true; }
            }
            return false;
        };
        check(settle(), "offscreen real FM receiver adopted");
        SliceModel* slice = model.slice(0);
        PanadapterModel* pan = slice ? model.panadapter(slice->panId()) : nullptr;
        if (pan) {
            auto revealStorage = std::make_unique<SpectrumWidget>();
            auto& reveal = *revealStorage;
            reveal.resize(1200, 600);
            reveal.setPanGeometryConfirmationRequired(true);
            int tunes = 0, reveals = 0;
            QObject::connect(&reveal, &SpectrumWidget::frequencyClicked, &model, [&](double) { ++tunes; });
            QObject::connect(&reveal, &SpectrumWidget::offScreenSliceCenterRequested,
                &model, [&](int id) { ++reveals; requestSlicePanCenter(model, id, model.slice(id)->frequency()); });
            QObject::connect(pan, &PanadapterModel::infoChanged, &reveal,
                [&](double center, double span) { reveal.observeFrequencyRange(center, span); });
            struct RevealCase { double center; double span; bool parked; bool doubleClick; };
            for (const RevealCase fixture : {
                    RevealCase{104.0, 2.16, true, true},
                    RevealCase{96.0, 2.16, true, true},
                    RevealCase{104.0, 0.2, true, false},
                    RevealCase{96.0, 0.2, true, true},
                    RevealCase{100.3, 0.2, false, true},
                    RevealCase{99.7, 0.2, false, false}}) {
                const double center = fixture.center;
                model.requestPanCenter(pan->panId(), pan->centerMhz(), fixture.span);
                model.requestPanCenter(pan->panId(), center, -1.0, IRadioBackend::PanCenterIntent::Drag);
                check(settle() && slice->inCapture() != fixture.parked, "offscreen fixture has expected capture membership");
                reveal.observeFrequencyRange(pan->centerMhz(), pan->bandwidthMhz());
                reveal.setSliceOverlay(0, slice->frequency(), -6000, 6000, false, true, "FM");
                reveal.setSliceOverlayInCapture(0, slice->inCapture());
                const QPoint marker = SpectrumOffscreenTestAccess::marker(reveal);
                const int before = reveals;
                mouse(reveal, QEvent::MouseButtonPress, marker.x(), marker.y());
                mouse(reveal, QEvent::MouseButtonRelease, marker.x(), marker.y());
                check(settle() && slice->inCapture() && std::abs(pan->centerMhz() - 100.0) < 0.0006,
                      "indicator press resumes parked RF and centers full view within half bin");
                // Paint after adoption, so the marker has disappeared before
                // the Qt double-click and its trailing release arrive.
                SpectrumOffscreenTestAccess::marker(reveal);
                if (fixture.doubleClick) {
                    mouse(reveal, QEvent::MouseButtonDblClick, marker.x(), marker.y());
                    mouse(reveal, QEvent::MouseButtonRelease, marker.x(), marker.y());
                }
                check(reveals == before + 1 && tunes == 0 && slice->frequency() == 100.0,
                      "full Qt double-click sequence cannot retune after marker disappears");
            }
            model.requestPanCenter(pan->panId(), 104.0, 2.16, IRadioBackend::PanCenterIntent::Drag);
            check(settle() && !slice->inCapture(), "rollback fixture parks receiver");
            const double parkedCenter = pan->centerMhz();
            {
                std::lock_guard lock(usb->mutex); usb->holdReadback = true;
            }
            usb->failWriteAt = usb->writes + 5;
            requestSlicePanCenter(model, 0, 100.0);
            wait(100);
            check(pan->centerMhz() == parkedCenter && !slice->inCapture(),
                  "pending reveal cannot publish requested geometry or membership");
            usb->releaseReadback();
            check(settle() && !slice->inCapture() && pan->centerMhz() == parkedCenter
                && slice->frequency() == 100.0,
                  "failed reveal compensates and preserves accepted RF/view/membership");
            {
                std::lock_guard lock(usb->mutex); usb->holdReadback = true;
            }
            requestSlicePanCenter(model, 0, 100.0);
            wait(50);
            model.requestPanCenter(pan->panId(), 96.0, -1.0, IRadioBackend::PanCenterIntent::Drag);
            usb->releaseReadback();
            check(settle() && !slice->inCapture() && std::abs(pan->centerMhz() - 96.0) < 0.0006,
                  "newer free pan supersedes pending reveal without publishing it");
            requestSlicePanCenter(model, 0, 100.0);
            model.requestPanCenter(pan->panId(), pan->centerMhz(), 0.2);
            check(settle() && slice->inCapture() && std::abs(pan->centerMhz() - 100.0) < 0.0006
                && pan->bandwidthMhz() < 0.201,
                  "zoom coalesces with reveal placement while preserving selected RF");
            reveal.prepareForShutdown();
        }
        backend.disconnectRadio();
    }
#else
    std::printf("[SKIP] RTL-specific backend rows: RTL-SDR is disabled in this build\n");
#endif
    // Independent pan windows keep the historical Range dispatch. Offline
    // command capture and receiver state only: no Flex/Hermes connection.
    {
        FlexBackend flex;
        QStringList commands;
        flex.setSliceCommandSink([&](const QString& command) { commands << command; });
        flex.setPanCenter("0x40000000", 14'200'000, IRadioBackend::PanCenterIntent::Range);
        check(commands == QStringList{"display pan set 0x40000000 center=14.200000"},
              "Flex Range centering dispatch retains its independent pan command");
        hl2::Hl2Backend hermes;
        hl2::Hl2DspReadbackTestAccess::receiver(hermes);
        double center = 0;
        QObject::connect(&hermes, &IRadioBackend::panCenterBandwidthChanged, &hermes,
            [&](const QString&, double mhz, double) { center = mhz; });
        hermes.setPanCenter({}, 14'200'000, IRadioBackend::PanCenterIntent::Range);
        check(center == 14.2 && hl2::Hl2DspReadbackTestAccess::frequency(hermes) == 14'200'000,
              "Hermes Range centering moves its DDC pan while preserving slice RF");
    }
    widget.prepareForShutdown();
    {
        // Band-plan segment `usage` surfaces on hover over the band strip, and
        // only there: a segment without usage, or a hidden strip, shows nothing.
        // (#4667)
        BandPlanManager plans;
        plans.loadPlans();
        plans.setActivePlan(QStringLiteral("IARU Region 1"));
        check(plans.activePlanName() == QStringLiteral("IARU Region 1"),
              "usage hover: IARU Region 1 plan loads from resources");
        auto bandStorage = std::make_unique<SpectrumWidget>();
        auto& band = *bandStorage;
        band.resize(800, 400);
        band.setBandPlanManager(&plans);
        band.setBandPlanFontSize(6);
        band.setFrequencyRangeImmediate(14.110, 0.040);
        band.show();
        // Offscreen there is no QRhi, so PanadapterMessageOverlay shows its
        // "Spectrum renderer unavailable" notice over the band row just after
        // show(), and mouseMoveEvent() yields to any child under the pointer.
        // Wait for it to clear the row.
        for (int i = 0; i < 100 && band.childAt(SpectrumOffscreenTestAccess::bandRow(band, 14.105)); ++i)
            wait(50);
        const auto hoverAt = [&](double mhz) {
            const QPoint p = SpectrumOffscreenTestAccess::bandRow(band, mhz);
            mouse(band, QEvent::MouseMove, p.x(), p.y());
            wait(50);
        };
        const auto tipGone = [&]() {
            wait(500);  // QToolTip::hideText() fades on a timer
            return !QToolTip::isVisible();
        };
        // Control: the pre-existing spot tooltip must work under this harness,
        // or the usage assertions below prove nothing.
        hoverAt(14.100);
        check(QToolTip::isVisible() && QToolTip::text().contains(QStringLiteral("IBP")),
              "usage hover: control — spot tooltip at 14.100 shows");
        QToolTip::hideText();
        wait(500);
        hoverAt(14.105);
        const QString tip = QToolTip::text();
        std::printf("    tooltip at 14.105: %s\n", qPrintable(tip));
        check(QToolTip::isVisible()
                  && tip.contains(QStringLiteral("14.101"))
                  && tip.contains(QStringLiteral("ALL"))
                  && tip.contains(QStringLiteral("automatically controlled data stations")),
              "usage hover: 14.101-14.112 shows its chart usage note");
        hoverAt(14.120);  // 14.112-14.350 ALL, no usage
        check(tipGone(), "usage hover: a segment without usage shows no tooltip");
        band.setBandPlanFontSize(0);
        hoverAt(14.105);
        check(tipGone(), "usage hover: no tooltip while the band strip is Off");
    }
    {
        // The cap holds without a scale factor, so it stays pinned even if the
        // --pan-pixels-retina registration's QT_SCALE_FACTOR is ignored.
        auto wideStorage = std::make_unique<SpectrumWidget>();
        auto& wide = *wideStorage;
        wide.resize(5000, 600);
        if (near(wide.devicePixelRatioF(), 1.0)) {
            check(panXpixelsFor(&wide) == 3840, "pan pixels: a 5000-pixel pan at ratio 1 is capped at 3840");
        } else {
            std::printf("[SKIP] pan pixels: the ratio-1 cap case needs QT_SCALE_FACTOR unset (ratio is %g)\n",
                        wide.devicePixelRatioF());
        }
    }

    {
        // Anchored gesture zoom (the shared applyAnchoredZoom extracted from the
        // trackpad/pinch paths). Under confirmation the new span is requested,
        // not applied locally, so assert the requested span and the clamp.
        auto widgetStorage = std::make_unique<SpectrumWidget>();
        auto& widget = *widgetStorage;
        widget.resize(1000, 400);
        widget.setPanGeometryConfirmationRequired(true);
        widget.setFrequencyRangeImmediate(14.200, 0.200);
        double reqSpan = -1;
        QObject::connect(&widget, &SpectrumWidget::frequencyRangeChangeRequested,
            &widget, [&](double, double span) { reqSpan = span; });
        const QPointF anchor(widget.mapToGlobal(QPoint(widget.width() / 2,
                                                       widget.height() / 2)));
        SpectrumOffscreenTestAccess::anchoredZoom(widget, 0.5, anchor);
        check(near(reqSpan, 0.100),
              "anchored zoom: bandwidth factor 0.5 requests half the span");
        check(near(widget.bandwidthMhz(), 0.200),
              "anchored zoom: under confirmation the span is requested, not applied");
        reqSpan = -1;
        SpectrumOffscreenTestAccess::anchoredZoom(widget, 1.0e6, anchor);  // past max
        check(reqSpan < 0, "anchored zoom past max bandwidth is clamped (no request)");
        SpectrumOffscreenTestAccess::anchoredZoom(widget, 1.0e-9, anchor);  // past min
        check(reqSpan < 0, "anchored zoom past min bandwidth is clamped (no request)");
    }
    {
        // #6883: while FFT frames present, a waterfall row and its scroll ride
        // the next FFT present. If the stream stops right after, the deferred
        // row and its finished scroll must still be presented. One FFT frame,
        // one native row 5 ms later, then silence.
        SpectrumWidget wf;
        wf.resize(1000, 600);
        wf.setFrequencyRangeImmediate(14.100, 0.200);
        wf.show();
        const auto pump = [](int ms) {
            QElapsedTimer clock;
            clock.start();
            while (clock.elapsed() < ms) {
                QApplication::processEvents(QEventLoop::AllEvents, 5);
                QThread::msleep(1);
            }
        };
        pump(150);
        // Offscreen has no QRhi to size the waterfall; give it rows directly.
        SpectrumOffscreenTestAccess::prepareHistory(wf);
        struct PaintCounter : QObject {
            int paints = 0;
            bool eventFilter(QObject*, QEvent* event) override {
                if (event->type() == QEvent::Paint) {
                    ++paints;
                }
                return false;
            }
        } counter;
        wf.setConnectionAnimationVisible(false);
        pump(100);
        wf.installEventFilter(&counter);
        pump(300);
        std::printf("fft_stop_idle_paints=%d\n", counter.paints);
        check(counter.paints == 0, "the final-frame probe starts from an idle widget");
        counter.paints = 0;
        wf.updateSpectrum(QVector<float>(512, -100.0f));
        pump(5);
        wf.updateWaterfallRow(QVector<float>(1024, 100.0f), 14.0, 14.2, 1);
        const int paintsAtRow = counter.paints;
        {
            const QVariantMap atRow = wf.automationDssSnapshot();
            std::printf("fft_stop_at_row width=%d rows=%d history=%d write=%d animating=%d visible=%d\n",
                        atRow.value(QStringLiteral("waterfallWidth")).toInt(),
                        atRow.value(QStringLiteral("waterfallRows")).toInt(),
                        atRow.value(QStringLiteral("waterfallHistoryRows")).toInt(),
                        atRow.value(QStringLiteral("waterfallWriteRow")).toInt(),
                        atRow.value(QStringLiteral("waterfallScrollAnimating")).toBool(),
                        wf.isVisible());
        }
        pump(500);
        const QVariantMap stats = wf.automationDssSnapshot();
        std::printf("fft_stop_final_frame paintsAtRow=%d paintsAfter=%d rows=%d animating=%d\n",
                    paintsAtRow, counter.paints,
                    stats.value(QStringLiteral("waterfallHistoryRows")).toInt(),
                    stats.value(QStringLiteral("waterfallScrollAnimating")).toBool());
        check(stats.value(QStringLiteral("waterfallHistoryRows")).toInt() >= 1,
              "the native row reached the waterfall");
        check(counter.paints > paintsAtRow,
              "a row deferred to the next FFT frame is still presented when the FFT stops");
        wf.removeEventFilter(&counter);
    }
    return failures ? 1 : 0;
}
