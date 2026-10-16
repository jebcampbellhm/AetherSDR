#include "DigitalMonitoringDialog.h"
#include "models/DigitalMonitoringModel.h"
#include "core/ThemeManager.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
namespace AetherSDR {
DigitalMonitoringDialog::DigitalMonitoringDialog(RadioModel* radio, QWidget* parent)
    : CanonWindow(tr("Digital Monitoring"), parent, Kind::Workspace)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setGeometryKey(QStringLiteral("digitalMonitoring/geometry"));
    setLaunchSize(QSize(720, 500));
    ThemeManager::instance().applyStyleSheet(bodyWidget(),
        "QLabel, QCheckBox { color: {{color.canon.ink}}; background: transparent; }"
        "QComboBox, QSpinBox { color: {{color.canon.ink}}; background: {{color.canon.nested}}; padding: 5px; }"
        "QPushButton { color: {{color.canon.ink}}; background: {{color.canon.nested}}; border: 1px solid {{color.canon.lineHi}}; border-radius: 6px; padding: 8px 12px; }"
        "QPushButton:disabled { color: {{color.canon.muted}}; }"
        "QPushButton:focus { border-color: {{color.canon.cyan}}; }");
    auto* model = new DigitalMonitoringModel(radio, this);
    auto* layout = new QVBoxLayout(bodyWidget());
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);
    layout->addWidget(new QLabel(tr("Digital Monitoring"), this));
    auto* explanation = new QLabel(tr("Capture selected digital portions and split them into decoder audio. Two RX slots remain reserved for independent listening and waterfalls."), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    QList<QCheckBox*> bands, modes;
    auto addChoices = [&](const QStringList& names, const QStringList& defaults, QList<QCheckBox*>& boxes) {
        auto* row = new QHBoxLayout;
        for (const auto& name : names) {
            auto* box = new QCheckBox(name, this);
            box->setChecked(defaults.contains(name));
            boxes.append(box); row->addWidget(box);
        }
        layout->addLayout(row);
    };
    addChoices({"80m","40m","30m","20m","17m","15m","12m","10m"}, {"40m","30m","20m","10m"}, bands);
    addChoices({"FT8","WSPR","FT4"}, {"FT8","WSPR"}, modes);
    auto* rate = new QComboBox(this);
    rate->addItem(tr("12 kHz"), 12000); rate->addItem(tr("24 kHz"), 24000); rate->setCurrentIndex(1);
    rate->setAccessibleName(tr("Receiver IQ rate"));
    auto* budget = new QSpinBox(this); budget->setRange(2, 13); budget->setValue(13);
    budget->setAccessibleName(tr("Total RX budget"));
    auto* settings = new QHBoxLayout;
    settings->addWidget(new QLabel(tr("Receiver IQ rate"), this)); settings->addWidget(rate);
    settings->addWidget(new QLabel(tr("Total RX budget"), this)); settings->addWidget(budget);
    layout->addLayout(settings);
    auto* note = new QLabel(tr("Rate must match the receiver setting. This window does not change it. Reduce the budget for slots used by other clients or onboard decoders."), this);
    note->setWordWrap(true); layout->addWidget(note);
    auto* preview = new QLabel(this); preview->setWordWrap(true); layout->addWidget(preview);
    auto* status = new QLabel(this); status->setWordWrap(true); layout->addWidget(status);
    auto* failure = new QLabel(this); failure->setWordWrap(true); layout->addWidget(failure);
    auto* timing = new QLabel(tr("Timing unqualified: timed decoding and reporting are not enabled. Closing this window leaves monitoring running; use Stop to release captures."), this);
    timing->setWordWrap(true); layout->addWidget(timing);
    auto* actions = new QHBoxLayout;
    auto* start = new QPushButton(tr("Start monitoring"), this);
    auto* stop = new QPushButton(tr("Stop"), this);
    start->setEnabled(false); stop->setEnabled(false);
    actions->addWidget(start); actions->addWidget(stop); actions->addStretch(); layout->addLayout(actions);
    auto options = [=] {
        QStringList selectedBands, selectedModes;
        for (auto* b : bands) if (b->isChecked()) selectedBands.append(b->text());
        for (auto* m : modes) if (m->isChecked()) selectedModes.append(m->text());
        return QVariantMap{{"bands", selectedBands}, {"modes", selectedModes},
                           {"rate", rate->currentData()}, {"budget", budget->value()}};
    };
    auto plan = [=] {
        start->setEnabled(false); failure->clear();
        if (model->available()) model->request("plan", options());
        else preview->setText(tr("Connect a receiver that supports digital monitoring."));
    };
    for (auto* b : bands + modes) connect(b, &QCheckBox::toggled, this, plan);
    connect(rate, &QComboBox::currentIndexChanged, this, plan);
    connect(budget, &QSpinBox::valueChanged, this, plan);
    connect(model, &DigitalMonitoringModel::error, this, [=](const QString& message) { failure->setText(message); });
    connect(model, &DigitalMonitoringModel::result, this, [=](const QString& verb, const QVariantMap& value) {
        if (verb == "plan") {
            preview->setText(tr("%1 captures + %2 reserved listeners; %3 digital outputs. %4")
                .arg(value.value("captures").toInt()).arg(value.value("reserved").toInt())
                .arg(value.value("outputs").toInt()).arg(value.value("error").toString()));
            start->setEnabled(model->available() && value.value("fits").toBool());
        } else if (verb == "status") {
            const int active = value.value("active").toInt(), pending = value.value("pending").toInt();
            status->setText(tr("Active captures: %1 | Starting: %2 | Output samples: %3")
                .arg(active).arg(pending).arg(value.value("samples").toULongLong()));
            stop->setEnabled(active + pending > 0);
            if (!value.value("error").toString().isEmpty()) failure->setText(value.value("error").toString());
        } else { model->request("status"); }
    });
    connect(start, &QPushButton::clicked, this, [=] { failure->clear(); model->request("start", options()); });
    connect(stop, &QPushButton::clicked, this, [=] { model->request("stop"); failure->clear(); });
    connect(model, &DigitalMonitoringModel::availabilityChanged, this, [=] {
        stop->setEnabled(false); status->clear(); plan();
    });
    auto* poll = new QTimer(this); poll->setInterval(1000);
    connect(poll, &QTimer::timeout, this, [=] { if (model->available()) model->request("status"); });
    poll->start(); plan();
    if (model->available()) model->request("status");
}
}
