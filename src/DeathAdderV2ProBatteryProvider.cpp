#include "DeathAdderV2ProBatteryProvider.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <cfgmgr32.h>

#include <cstdio>
#include <cstring>
#include <vector>

#pragma comment(lib, "setupapi")
#pragma comment(lib, "hid")

/*---------------------------------------------------------*\
| Constants                                                 |
\*---------------------------------------------------------*/
static constexpr WORD  RAZER_VID        = 0x1532;
static constexpr WORD  PID_WIRED        = 0x007C;
static constexpr WORD  PID_WIRELESS     = 0x007D;
static constexpr int   TARGET_MI        = 0;        /* Mouse HID interface */
static constexpr int   REPORT_SIZE      = 90;
static constexpr int   FULL_BUF_SIZE    = 91;       /* Report ID (1) + 90 */
static constexpr BYTE  TRANSACTION_ID   = 0x3F;
static constexpr int   MAX_RETRIES      = 3;
static constexpr int   IO_DELAY_MS      = 100;

/*---------------------------------------------------------*\
| Constructor / Destructor                                   |
\*---------------------------------------------------------*/
DeathAdderV2ProBatteryProvider::DeathAdderV2ProBatteryProvider()
{
}

DeathAdderV2ProBatteryProvider::~DeathAdderV2ProBatteryProvider()
{
    Stop();
}

/*---------------------------------------------------------*\
| Device discovery                                           |
|                                                            |
|  Finds the HID path for MI=0, FeatLen>=91,                |
|  VID=0x1532, PID=0x007C or 0x007D.                        |
|  Prefers wired (0x007C) when both are present.             |
\*---------------------------------------------------------*/
bool DeathAdderV2ProBatteryProvider::FindDevicePath(bool prefer_wired,
                                                      std::string& out_path,
                                                      bool& out_wired)
{
    out_path.clear();
    out_wired = false;

    GUID hid_guid;
    HidD_GetHidGuid(&hid_guid);

    HDEVINFO dev_info = SetupDiGetClassDevsW(&hid_guid, NULL, NULL,
                                              DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (dev_info == INVALID_HANDLE_VALUE)
        return false;

    /* Keep best matches */
    std::string wired_path, wireless_path;

    SP_DEVICE_INTERFACE_DATA dev_iface = { sizeof(SP_DEVICE_INTERFACE_DATA) };
    for (DWORD idx = 0;
         SetupDiEnumDeviceInterfaces(dev_info, NULL, &hid_guid, idx, &dev_iface);
         idx++)
    {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(dev_info, &dev_iface, NULL, 0, &required, NULL);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) continue;

        std::vector<BYTE> buf(required);
        auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(dev_info, &dev_iface,
                                               detail, (DWORD)buf.size(),
                                               &required, NULL))
            continue;

        std::wstring path_w = detail->DevicePath;

        /* Parse VID, PID, MI */
        WORD vid = 0, pid = 0;
        int mi = -1;

        const wchar_t* s = path_w.c_str();
        const wchar_t* p;

        p = wcsstr(s, L"vid_");
        if (p) { wchar_t v[16]={0}; wcsncpy_s(v, p+4, 4); vid = (WORD)wcstol(v,NULL,16); }

        p = wcsstr(s, L"pid_");
        if (p) { wchar_t v[16]={0}; wcsncpy_s(v, p+4, 4); pid = (WORD)wcstol(v,NULL,16); }

        p = wcsstr(s, L"mi_");
        if (p) { wchar_t v[16]={0}; wcsncpy_s(v, p+3, 2); mi = (int)wcstol(v,NULL,16); }

        /* Filter: Razer DeathAdder V2 Pro, MI=0 */
        if (vid != RAZER_VID) continue;
        if (pid != PID_WIRED && pid != PID_WIRELESS) continue;
        if (mi != TARGET_MI) continue;

        /* Verify FeatLen >= 91 */
        char path_a[512] = {0};
        WideCharToMultiByte(CP_ACP, 0, path_w.c_str(), -1,
                            path_a, (int)sizeof(path_a)-1, NULL, NULL);

        HANDLE h = CreateFileA(path_a, 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;

        PHIDP_PREPARSED_DATA pp = NULL;
        ULONG feat = 0;
        if (HidD_GetPreparsedData(h, &pp) && pp)
        {
            HIDP_CAPS caps;
            if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS)
                feat = caps.FeatureReportByteLength;
            HidD_FreePreparsedData(pp);
        }
        CloseHandle(h);

        if (feat < (ULONG)FULL_BUF_SIZE) continue;

        if (pid == PID_WIRED)
            wired_path = path_a;
        else
            wireless_path = path_a;
    }

    SetupDiDestroyDeviceInfoList(dev_info);

    /* Priority: prefer_wired → wired, else wireless */
    if (prefer_wired && !wired_path.empty())
    {
        out_path = wired_path;
        out_wired = true;
        return true;
    }
    if (!wireless_path.empty())
    {
        out_path = wireless_path;
        out_wired = false;
        return true;
    }
    if (!wired_path.empty())
    {
        out_path = wired_path;
        out_wired = true;
        return true;
    }

    return false;
}

/*---------------------------------------------------------*\
| Razer HID report helpers                                   |
\*---------------------------------------------------------*/
uint8_t DeathAdderV2ProBatteryProvider::ComputeCrc(const uint8_t* d)
{
    uint8_t crc = 0;
    for (int i = 2; i < 88; i++) crc ^= d[i];
    return crc;
}

void DeathAdderV2ProBatteryProvider::BuildReport(uint8_t* out_90,
                                                   uint8_t tid,
                                                   uint8_t cmd_class,
                                                   uint8_t cmd_id,
                                                   uint8_t data_size)
{
    memset(out_90, 0, REPORT_SIZE);
    out_90[1] = tid;
    out_90[4] = 0;
    out_90[5] = data_size;
    out_90[6] = cmd_class;
    out_90[7] = cmd_id;
    out_90[88] = ComputeCrc(out_90);
    out_90[89] = 0;
}

int DeathAdderV2ProBatteryProvider::RawToPercent(uint8_t raw)
{
    int pct = (static_cast<int>(raw) * 100) / 255;
    if (pct > 100) pct = 100;
    return pct;
}

/*---------------------------------------------------------*\
| Start / Stop                                               |
\*---------------------------------------------------------*/
bool DeathAdderV2ProBatteryProvider::Start()
{
    Stop();

    std::string path;
    bool w = false;

    /* Try wireless first (dongle is usually always present),
       fall back to wired */
    if (FindDevicePath(false, path, w))
    {
        HANDLE h = CreateFileA(path.c_str(), 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE)
        {
            hid_handle_ = reinterpret_cast<void*>(h);
            wired_ = w;
            running_ = true;
            return true;
        }
    }

    /* Try wired */
    if (FindDevicePath(true, path, w))
    {
        HANDLE h = CreateFileA(path.c_str(), 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE)
        {
            hid_handle_ = reinterpret_cast<void*>(h);
            wired_ = w;
            running_ = true;
            return true;
        }
    }

    return false;
}

void DeathAdderV2ProBatteryProvider::Stop()
{
    if (hid_handle_)
    {
        CloseHandle(reinterpret_cast<HANDLE>(hid_handle_));
        hid_handle_ = nullptr;
    }
    running_ = false;
}

bool DeathAdderV2ProBatteryProvider::IsRunning() const
{
    return running_;
}

/*---------------------------------------------------------*\
| Read battery                                               |
\*---------------------------------------------------------*/
BatteryState DeathAdderV2ProBatteryProvider::ReadBattery()
{
    BatteryState state;
    state.wired = wired_;
    state.available = false;

    if (!hid_handle_)
    {
        /* Device may have been replugged — try re-opening */
        if (!Start())
            return state;
    }

    HANDLE h = reinterpret_cast<HANDLE>(hid_handle_);

    /* ── Battery level ── */
    uint8_t req[REPORT_SIZE];
    BuildReport(req, TRANSACTION_ID, 0x07, 0x80, 2);

    uint8_t out_buf[FULL_BUF_SIZE] = {0};
    out_buf[0] = 0; /* Report ID */
    memcpy(out_buf + 1, req, REPORT_SIZE);

    bool device_gone = false;

    for (int retry = 0; retry < MAX_RETRIES; retry++)
    {
        if (!HidD_SetFeature(h, out_buf, FULL_BUF_SIZE))
        {
            DWORD err = GetLastError();
            if (err == ERROR_DEVICE_NOT_CONNECTED ||
                err == ERROR_FILE_NOT_FOUND ||
                err == ERROR_GEN_FAILURE)
            {
                device_gone = true;
            }
            Sleep(IO_DELAY_MS);
            continue;
        }

        Sleep(IO_DELAY_MS);

        uint8_t in_buf[FULL_BUF_SIZE] = {0};
        in_buf[0] = 0;

        if (!HidD_GetFeature(h, in_buf, FULL_BUF_SIZE))
        {
            DWORD err = GetLastError();
            if (err == ERROR_DEVICE_NOT_CONNECTED ||
                err == ERROR_FILE_NOT_FOUND ||
                err == ERROR_GEN_FAILURE)
            {
                device_gone = true;
            }
            continue;
        }

        uint8_t resp[REPORT_SIZE];
        memcpy(resp, in_buf + 1, REPORT_SIZE);

        /* CRC check */
        uint8_t expected_crc = ComputeCrc(resp);
        if (resp[88] != expected_crc)
            continue;

        /* Status check: 0x02 = success */
        if (resp[0] != 0x02)
        {
            if (resp[0] == 0x01) { Sleep(200); continue; } /* busy */
            continue;
        }

        uint8_t raw = resp[9];
        state.percent = RawToPercent(raw);
        state.available = true;
        break;
    }

    if (device_gone)
    {
        /* Device disconnected — close stale handle so Start() can re-open */
        Stop();
        return state;
    }

    if (!state.available)
        return state;

    /* ── Charging status ── */
    BuildReport(req, TRANSACTION_ID, 0x07, 0x84, 2);
    memcpy(out_buf + 1, req, REPORT_SIZE);

    for (int retry = 0; retry < 3; retry++)
    {
        if (!HidD_SetFeature(h, out_buf, FULL_BUF_SIZE))
        {
            Sleep(IO_DELAY_MS);
            continue;
        }
        Sleep(IO_DELAY_MS);

        uint8_t in_buf[FULL_BUF_SIZE] = {0};
        in_buf[0] = 0;

        if (!HidD_GetFeature(h, in_buf, FULL_BUF_SIZE))
            continue;

        uint8_t resp[REPORT_SIZE];
        memcpy(resp, in_buf + 1, REPORT_SIZE);
        if (resp[88] != ComputeCrc(resp)) continue;
        if (resp[0] == 0x02)
        {
            state.charging = (resp[9] != 0);
            state.charging_known = true;
            break;
        }
    }

    return state;
}