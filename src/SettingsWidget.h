#pragma once

#include <QObject>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

#include "OpenRGBPluginInterface.h"
#include "DeathAdderV2ProBatteryProvider.h"

/* Small settings widget with battery readout */
class SettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsWidget(QWidget* parent = nullptr);
    ~SettingsWidget() override;

    void SetBatteryState(const BatteryState& state);
    void SetDeviceUnavailable();
    void SetDeviceNotFound();

signals:
    void refreshRequested();

private:
    QLabel* device_label_  = nullptr;
    QLabel* connection_label_ = nullptr;
    QLabel* battery_label_ = nullptr;
    QLabel* charging_label_ = nullptr;
    QLabel* updated_label_ = nullptr;
    QLabel* status_label_  = nullptr;
    QPushButton* refresh_btn_ = nullptr;
};