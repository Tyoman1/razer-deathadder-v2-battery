#pragma once

#include <string>
#include <cstdint>

/*---------------------------------------------------------*\
| BatteryState — результат чтения батареи                    |
\*---------------------------------------------------------*/
struct BatteryState
{
    bool    available      = false;
    int     percent        = 0;
    bool    charging       = false;
    bool    charging_known = false;
    bool    wired          = false;
    bool    last_known     = false;
};

/*---------------------------------------------------------*\
| DeathAdderV2ProBatteryProvider                             |
|                                                            |
|  Read-only HID feature-report access to the battery of     |
|  a Razer DeathAdder V2 Pro mouse.                          |
|                                                            |
|  Uses dwAccess=0 + HidD_SetFeature/HidD_GetFeature         |
|  on HID Interface 0 (Mouse collection) with                |
|  FeatureReportByteLength=91.   No GENERIC_READ/WRITE       |
|  needed.                                                    |
\*---------------------------------------------------------*/
class DeathAdderV2ProBatteryProvider
{
public:
    DeathAdderV2ProBatteryProvider();
    ~DeathAdderV2ProBatteryProvider();

    /* Start/stop the provider; opens/closes the HID handle */
    bool    Start();
    void    Stop();

    /* Read battery state from the device */
    BatteryState ReadBattery();

    /* Check if the device handle is currently open */
    bool    IsRunning() const;

private:
    /* Platform-specific handle */
    void*   hid_handle_ = nullptr;
    bool    running_    = false;
    bool    wired_      = false;

    /* Find the correct HID device path for the given PID */
    bool    FindDevicePath(bool prefer_wired, std::string& out_path, bool& out_wired);

    /* Razer 90-byte report helpers */
    static void    BuildReport(uint8_t* out_90, uint8_t transaction_id,
                               uint8_t cmd_class, uint8_t cmd_id, uint8_t data_size);
    static uint8_t ComputeCrc(const uint8_t* data_90);
    static int     RawToPercent(uint8_t raw);
};