#pragma once

#include <string>

#include <QObject>
#include <QTimer>
#include <QMenu>
#include <QAction>
#include <QFutureWatcher>

#include "OpenRGBPluginInterface.h"
#include "DeathAdderV2ProBatteryProvider.h"

class SettingsWidget;

class BatteryPlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID FILE "OpenRGBBatteryPlugin.json")
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    BatteryPlugin();
    ~BatteryPlugin() override;

    OpenRGBPluginInfo   GetPluginInfo()       override;
    unsigned int        GetPluginAPIVersion() override;

    void                Load(OpenRGBPluginAPIInterface* plugin_api_ptr) override;
    QWidget*            GetWidget()           override;
    QMenu*              GetTrayMenu()         override;
    void                Unload()              override;

    void                OnProfileAboutToLoad() override {}
    void                OnProfileLoad(nlohmann::json) override {}
    nlohmann::json      OnProfileSave()       override { return nlohmann::json::object(); }
    unsigned char*      OnSDKCommand(unsigned int, unsigned char*, unsigned int*) override { return nullptr; }

    void                ProfileManagerUpdated(unsigned int) override {}
    void                ResourceManagerUpdated(unsigned int) override {}
    void                SettingsManagerUpdated(unsigned int) override {}

private slots:
    void OnPollTimer();
    void OnManualRefresh();

private:
    void ScheduleNextPoll();
    void OnBatteryResult(const BatteryState& state);
    void UpdateTrayMenu(const BatteryState& state);
    void SaveSettings();
    void LoadSettings();

    OpenRGBPluginAPIInterface*      api_        = nullptr;
    SettingsWidget*                 widget_     = nullptr;
    DeathAdderV2ProBatteryProvider* provider_   = nullptr;
    QTimer*                         poll_timer_ = nullptr;
    bool                            refresh_in_progress_ = false;

    QFutureWatcher<BatteryState>*   future_watcher_ = nullptr;

    /* Tray menu */
    QMenu*          tray_menu_            = nullptr;
    QAction*        tray_device_action_    = nullptr;
    QAction*        tray_battery_action_   = nullptr;
    QAction*        tray_connection_action_ = nullptr;
    QAction*        tray_charging_action_  = nullptr;
    QAction*        tray_refresh_action_   = nullptr;

    BatteryState    last_state_;

    int             poll_interval_sec_ = 60;
    bool            log_enabled_       = true;
};