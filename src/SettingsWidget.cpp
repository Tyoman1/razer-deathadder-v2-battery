#include "SettingsWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QDateTime>
#include <QFrame>

SettingsWidget::SettingsWidget(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    /* ── Header ── */
    device_label_ = new QLabel("<b>Razer DeathAdder V2 Pro</b>", this);
    device_label_->setStyleSheet("font-size: 14px;");
    layout->addWidget(device_label_);

    /* Separator */
    QFrame* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    layout->addWidget(sep);

    /* ── Info rows ── */
    connection_label_ = new QLabel("Connection: —", this);
    battery_label_    = new QLabel("Battery: —", this);
    charging_label_   = new QLabel("Charging: —", this);
    updated_label_    = new QLabel("Last update: —", this);
    status_label_     = new QLabel("", this);
    status_label_->setStyleSheet("color: gray;");

    layout->addWidget(connection_label_);
    layout->addWidget(battery_label_);
    layout->addWidget(charging_label_);
    layout->addWidget(updated_label_);
    layout->addWidget(status_label_);

    /* ── Refresh button ── */
    QHBoxLayout* btn_layout = new QHBoxLayout();
    btn_layout->addStretch();
    refresh_btn_ = new QPushButton("Refresh", this);
    refresh_btn_->setFixedWidth(120);
    btn_layout->addWidget(refresh_btn_);
    layout->addLayout(btn_layout);

    connect(refresh_btn_, &QPushButton::clicked, this, &SettingsWidget::refreshRequested);

    layout->addStretch();
}

SettingsWidget::~SettingsWidget()
{
}

void SettingsWidget::SetBatteryState(const BatteryState& state)
{
    connection_label_->setText(
        QString("Connection: %1").arg(state.wired ? "Wired" : "Wireless"));

    if (state.available)
    {
        if (state.percent >= 100)
            battery_label_->setText("Battery: 100% (Fully charged)");
        else
            battery_label_->setText(QString("Battery: %1%").arg(state.percent));
    }
    else if (state.last_known)
    {
        battery_label_->setText(
            QString("Battery: %1% (Last known)").arg(state.percent));
    }
    else
    {
        battery_label_->setText("Battery: unavailable");
    }

    if (state.charging_known)
        charging_label_->setText(
            QString("Charging: %1").arg(state.charging ? "Yes" : "No"));
    else
        charging_label_->setText("Charging: Unknown");

    updated_label_->setText(
        QString("Last update: %1")
            .arg(QDateTime::currentDateTime().toString("HH:mm:ss")));

    status_label_->setText("");
}

void SettingsWidget::SetDeviceUnavailable()
{
    connection_label_->setText("Connection: —");
    battery_label_->setText("Battery: unavailable");
    charging_label_->setText("Charging: —");
    status_label_->setText("Device not responding. Last values shown if available.");
}

void SettingsWidget::SetDeviceNotFound()
{
    connection_label_->setText("Connection: —");
    battery_label_->setText("Battery: —");
    charging_label_->setText("Charging: —");
    updated_label_->setText("Last update: —");
    status_label_->setText("Razer DeathAdder V2 Pro not found.");
}