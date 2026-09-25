#include "BatteryPlugin.h"
#include "SettingsWidget.h"
#include "DeathAdderV2ProBatteryProvider.h"

#include <QImage>
#include <QString>

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

    /* Polling timer */
    poll_timer_ = new QTimer(this);
    connect(poll_timer_, &QTimer::timeout, this, &BatteryPlugin::OnPollTimer);

    /* Try to start provider and do first read */
    if (provider_->Start())
    {
        DoBatteryRead();
        poll_timer_->start(poll_interval_sec_ * 1000);
    }
    else
    {
        widget_->SetDeviceNotFound();
        if (log_enabled_ && api_)
            api_->LogEntry(__FILE__, __LINE__, LOG_WARNING,
                           "%s", "Razer DeathAdder V2 Pro not found");
    }
}

QWidget* BatteryPlugin::GetWidget()
{
    return widget_;
}

QMenu* BatteryPlugin::GetTrayMenu()
{
    return nullptr;
}

void BatteryPlugin::Unload()
{
    if (poll_timer_)
    {
        poll_timer_->stop();
        poll_timer_ = nullptr;
    }

    if (provider_)
    {
        provider_->Stop();
        delete provider_;
        provider_ = nullptr;
    }

    widget_ = nullptr; /* OpenRGB owns the widget */
    api_    = nullptr;
}

/*---------------------------------------------------------*\
| Polling & Refresh                                         |
\*---------------------------------------------------------*/
void BatteryPlugin::OnPollTimer()
{
    if (refresh_in_progress_) return;
    DoBatteryRead();
}

void BatteryPlugin::OnManualRefresh()
{
    if (refresh_in_progress_) return;
    DoBatteryRead();
}

void BatteryPlugin::DoBatteryRead()
{
    if (!provider_ || !widget_) return;

    refresh_in_progress_ = true;

    /* If provider stopped (device removed), try to restart */
    if (!provider_->IsRunning())
    {
        if (!provider_->Start())
        {
            last_state_.available = false;
            last_state_.last_known = false;
            widget_->SetDeviceNotFound();
            if (poll_timer_) poll_timer_->stop();
            refresh_in_progress_ = false;
            return;
        }
        /* Restart polling */
        if (poll_timer_) poll_timer_->start(poll_interval_sec_ * 1000);
    }

    try
    {
        BatteryState state = provider_->ReadBattery();

        if (state.available)
        {
            /* Remember for last-known fallback */
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
            /* Device didn't respond — show last known if we have it */
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
    }
    catch (...)
    {
        if (last_state_.last_known)
        {
            widget_->SetBatteryState(last_state_);
            widget_->SetDeviceUnavailable();
        }
        else
        {
            widget_->SetDeviceNotFound();
        }
    }

    refresh_in_progress_ = false;
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