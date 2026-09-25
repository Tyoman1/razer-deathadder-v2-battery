#include "BatteryPlugin.h"
#include "SettingsWidget.h"
#include "DeathAdderV2ProBatteryProvider.h"

#include <QImage>
#include <QString>
#include <QtConcurrent>

/* Log level mirrors */
enum { LOG_ERROR = 0, LOG_WARNING = 1, LOG_INFO = 2 };

BatteryPlugin::BatteryPlugin() {}
BatteryPlugin::~BatteryPlugin() {}

/*---------------------------------------------------------*\
| Plugin Info                                               |
\*---------------------------------------------------------*/
OpenRGBPluginInfo BatteryPlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;
    info.Name            = "Razer Device Battery";
    info.Description     = "Displays Razer DeathAdder V2 Pro battery level via HID feature report";
    info.Version         = "1.0.0";
    info.Commit          = "";
    info.URL             = "https://github.com/Tyoman1/razer-deathadder-v2-battery";
    info.Icon            = QImage();
    info.Location        = OPENRGB_PLUGIN_LOCATION_SETTINGS;
    info.Label           = "Battery Monitor";
    info.TabIconString   = "";
    info.TabIcon         = QImage();
    info.ProtocolVersion = 1;
    return info;
}

unsigned int BatteryPlugin::GetPluginAPIVersion()
{
    return OPENRGB_PLUGIN_API_VERSION;
}

/*---------------------------------------------------------*\
| Load / Unload                                             |
\*---------------------------------------------------------*/
void BatteryPlugin::Load(OpenRGBPluginAPIInterface* plugin_api_ptr)
{
    api_ = plugin_api_ptr;

    LoadSettings();

    /* Create provider */
    provider_ = new DeathAdderV2ProBatteryProvider();

    /* Create UI */
    widget_ = new SettingsWidget();
    connect(widget_, &SettingsWidget::refreshRequested, this, &BatteryPlugin::OnManualRefresh);

    /* ── Tray menu ── */
    tray_menu_ = new QMenu("Battery Monitor");

    tray_device_action_    = tray_menu_->addAction("Razer DeathAdder V2 Pro");
    tray_device_action_->setEnabled(false);

    tray_battery_action_   = tray_menu_->addAction("Battery: unavailable");
    tray_battery_action_->setEnabled(false);

    tray_connection_action_ = tray_menu_->addAction("Connection: Unknown");
    tray_connection_action_->setEnabled(false);

    tray_charging_action_  = tray_menu_->addAction("Charging: Unknown");
    tray_charging_action_->setEnabled(false);

    tray_menu_->addSeparator();

    tray_refresh_action_   = tray_menu_->addAction("Refresh battery");
    connect(tray_refresh_action_, &QAction::triggered,
            this, &BatteryPlugin::OnManualRefresh);

    /* ── Polling timer (runs in GUI thread, HID I/O deferred to thread pool) ── */
    poll_timer_ = new QTimer(this);
    connect(poll_timer_, &QTimer::timeout, this, &BatteryPlugin::OnPollTimer);

    /* Try initial discovery */
    if (provider_->Start())
    {
        ScheduleNextPoll();
    }
    else
    {
        widget_->SetDeviceNotFound();
        if (log_enabled_ && api_)
            api_->LogEntry(__FILE__, __LINE__, LOG_WARNING,
                           "%s", "Razer DeathAdder V2 Pro not found at startup");
        /* Keep polling — device may appear later */
        ScheduleNextPoll();
    }
}

QWidget* BatteryPlugin::GetWidget()
{
    return widget_;
}

QMenu* BatteryPlugin::GetTrayMenu()
{
    return tray_menu_;
}

void BatteryPlugin::Unload()
{
    /* Cancel any in-flight async read */
    if (future_watcher_)
    {
        future_watcher_->cancel();
        future_watcher_->waitForFinished();
        delete future_watcher_;
        future_watcher_ = nullptr;
    }

    if (poll_timer_)
    {
        poll_timer_->stop();
        delete poll_timer_;
        poll_timer_ = nullptr;
    }

    if (provider_)
    {
        provider_->Stop();
        delete provider_;
        provider_ = nullptr;
    }

    /* Tray menu: OpenRGB owns the returned QMenu and its QActions */
    tray_menu_        = nullptr;
    tray_device_action_ = nullptr;
    tray_battery_action_ = nullptr;
    tray_connection_action_ = nullptr;
    tray_charging_action_  = nullptr;
    tray_refresh_action_   = nullptr;

    refresh_in_progress_ = false;
    widget_ = nullptr; /* OpenRGB owns the widget */
    api_    = nullptr;
}

/*---------------------------------------------------------*\
| Polling & Refresh                                         |
\*---------------------------------------------------------*/
void BatteryPlugin::ScheduleNextPoll()
{
    if (poll_timer_)
        poll_timer_->start(poll_interval_sec_ * 1000);
}

void BatteryPlugin::OnPollTimer()
{
    if (refresh_in_progress_) return;

    /* If provider is not running (device absent), try to start it */
    if (!provider_->IsRunning())
    {
        if (!provider_->Start())
        {
            /* Still not found — try again next tick */
            return;
        }
        /* Re-connected; keep going to do first read */
    }

    refresh_in_progress_ = true;

    if (tray_refresh_action_)
        tray_refresh_action_->setText("Refresh battery (updating...)");

    /* Defer HID I/O to a thread-pool worker */
    future_watcher_ = new QFutureWatcher<BatteryState>(this);
    connect(future_watcher_, &QFutureWatcher<BatteryState>::finished,
            this, [this]()
    {
        BatteryState state = future_watcher_->result();
        OnBatteryResult(state);
        future_watcher_->deleteLater();
        future_watcher_ = nullptr;
    });

    future_watcher_->setFuture(
        QtConcurrent::run([provider = provider_]()
        {
            return provider->ReadBattery();
        }));
}

void BatteryPlugin::OnManualRefresh()
{
    if (refresh_in_progress_) return;
    OnPollTimer();
}

void BatteryPlugin::OnBatteryResult(const BatteryState& state)
{
    refresh_in_progress_ = false;

    if (tray_refresh_action_)
        tray_refresh_action_->setText("Refresh battery");

    if (!widget_) return;

    if (state.available)
    {
        last_state_ = state;
        last_state_.last_known = true;
        widget_->SetBatteryState(state);

        if (log_enabled_ && api_)
            api_->LogEntry(__FILE__, __LINE__, LOG_INFO,
                           "DeathAdder V2 Pro battery: %d%% charging=%s",
                           state.percent,
                           state.charging ? "yes" : "no");
    }
    else
    {
        /* Device didn't respond */
        if (last_state_.last_known)
        {
            widget_->SetBatteryState(last_state_);
            widget_->SetDeviceUnavailable();
        }
        else
        {
            widget_->SetDeviceNotFound();
        }

        if (log_enabled_ && api_)
            api_->LogEntry(__FILE__, __LINE__, LOG_WARNING,
                           "%s", "DeathAdder V2 Pro battery read failed");
    }

    UpdateTrayMenu(state);

    /* Keep polling — device may reappear */
    ScheduleNextPoll();
}

/*---------------------------------------------------------*\
| Tray menu update                                           |
\*---------------------------------------------------------*/
void BatteryPlugin::UpdateTrayMenu(const BatteryState& state)
{
    if (!tray_battery_action_) return;

    if (state.available)
    {
        tray_battery_action_->setText(
            QString("Battery: %1%").arg(state.percent));

        tray_connection_action_->setText(
            QString("Connection: %1").arg(state.wired ? "Wired" : "Wireless"));

        if (state.charging_known)
            tray_charging_action_->setText(
                QString("Charging: %1").arg(state.charging ? "Yes" : "No"));
        else
            tray_charging_action_->setText("Charging: Unknown");
    }
    else if (last_state_.last_known)
    {
        tray_battery_action_->setText(
            QString("Battery: %1% (last known)").arg(last_state_.percent));

        tray_connection_action_->setText("Connection: Unknown");
        tray_charging_action_->setText("Charging: Unknown");
    }
    else
    {
        tray_battery_action_->setText("Battery: unavailable");
        tray_connection_action_->setText("Connection: Unknown");
        tray_charging_action_->setText("Charging: Unknown");
    }
}

/*---------------------------------------------------------*\
| Settings                                                  |
\*---------------------------------------------------------*/
void BatteryPlugin::LoadSettings()
{
    if (!api_) return;

    try
    {
        nlohmann::json settings = api_->GetSettings("OpenRGBBatteryPlugin");

        if (settings.contains("poll_interval_sec") && settings["poll_interval_sec"].is_number_integer())
        {
            int v = settings["poll_interval_sec"].get<int>();
            if (v >= 30 && v <= 600) poll_interval_sec_ = v;
        }

        if (settings.contains("log_enabled") && settings["log_enabled"].is_boolean())
            log_enabled_ = settings["log_enabled"].get<bool>();
    }
    catch (...)
    {
        /* Use defaults */
    }
}

void BatteryPlugin::SaveSettings()
{
    if (!api_) return;

    try
    {
        nlohmann::json settings;
        settings["poll_interval_sec"] = poll_interval_sec_;
        settings["log_enabled"]       = log_enabled_;
        api_->SetSettings("OpenRGBBatteryPlugin", settings);
        api_->SaveSettings();
    }
    catch (...)
    {
    }
}