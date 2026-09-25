/*---------------------------------------------------------*\
| BatteryProbe.exe                                           |
|                                                            |
|  Standalone test executable for Razer DeathAdder V2 Pro    |
|  battery level reading via HID.                            |
|                                                            |
|  Tests that battery query can coexist with OpenRGB.        |
|  No RGB, no profile loading, no device state changes.      |
|                                                            |
|  Usage: BatteryProbe.exe                                    |
|                                                            |
|  This file is part of the openrgb-wake-plugin-hardened      |
|  project (https://github.com/Tyoman1/openrgb-wake-plugin-   |
|  hardened).                                                 |
\*---------------------------------------------------------*/

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <cfgmgr32.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "setupapi")
#pragma comment(lib, "hid")

/*---------------------------------------------------------*\
| Constants                                                 |
\*---------------------------------------------------------*/
static constexpr WORD  RAZER_VID           = 0x1532;
static constexpr WORD  PID_WIRED           = 0x007C;
static constexpr WORD  PID_WIRELESS        = 0x007D;
static constexpr int   TARGET_INTERFACE    = 2;
static constexpr BYTE  TRANSACTION_ID      = 0x3F;
static constexpr int   REPORT_SIZE         = 90;
static constexpr int   IO_DELAY_MS         = 100;
static constexpr int   READ_TIMEOUT_MS     = 1000;
static constexpr int   MAX_RETRIES         = 5;
static constexpr DWORD RETRY_BUSY_MS       = 300;

/*---------------------------------------------------------*\
| Razer HID report helpers                                   |
\*---------------------------------------------------------*/
struct RazerReport
{
    BYTE data[REPORT_SIZE];

    RazerReport()
    {
        memset(data, 0, sizeof(data));
    }

    void SetTransactionId(BYTE tid)
    {
        data[1] = tid;
    }

    void SetCommand(BYTE cmd_class, BYTE cmd_id, BYTE data_size = 2)
    {
        data[4] = 0;              /* protocol_type */
        data[5] = data_size;
        data[6] = cmd_class;
        data[7] = cmd_id;
    }

    BYTE ComputeCrc() const
    {
        BYTE crc = 0;
        for (int i = 2; i < 88; i++)
        {
            crc ^= data[i];
        }
        return crc;
    }

    void Finalize()
    {
        data[88] = ComputeCrc();
        data[89] = 0;  /* reserved */
    }

    bool VerifyCrc() const
    {
        return data[88] == ComputeCrc();
    }
};

/* Build a 91-byte buffer for hid write/read (Report ID + 90 data bytes) */
static void BuildHidBuffer(BYTE* out_buf, const RazerReport& report)
{
    out_buf[0] = 0;  /* Report ID = 0 */
    memcpy(out_buf + 1, report.data, REPORT_SIZE);
}

/*---------------------------------------------------------*\
| Device discovery                                           |
\*---------------------------------------------------------*/
struct DeviceInfo
{
    std::string path;
    WORD        pid;
    bool        is_wireless;
    int         interface_number;
};

static bool FindRazerDevice(std::vector<DeviceInfo>& devices)
{
    devices.clear();

    /* Enumerate all HID devices */
    GUID hid_guid;
    HidD_GetHidGuid(&hid_guid);

    HDEVINFO dev_info = SetupDiGetClassDevsW(
        &hid_guid, NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (dev_info == INVALID_HANDLE_VALUE)
    {
        printf("ERROR: SetupDiGetClassDevsW failed (0x%lx)\n", GetLastError());
        return false;
    }

    SP_DEVICE_INTERFACE_DATA dev_iface = { sizeof(SP_DEVICE_INTERFACE_DATA) };

    for (DWORD idx = 0;
         SetupDiEnumDeviceInterfaces(dev_info, NULL, &hid_guid, idx, &dev_iface);
         idx++)
    {
        /* Get required size */
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(dev_info, &dev_iface, NULL, 0, &required, NULL);

        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            continue;

        std::vector<BYTE> buffer(required);
        auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(dev_info, &dev_iface,
                                               detail, (DWORD)buffer.size(),
                                               &required, NULL))
        {
            continue;
        }

        std::wstring device_path = detail->DevicePath;

        /* Extract VID, PID, Interface from the path */
        WORD vid = 0, pid = 0;
        int iface = -1;

        /* Typical path: \\\\?\\hid#vid_1532&pid_007c&mi_02#... */
        wchar_t vid_str[16] = { 0 }, pid_str[16] = { 0 }, mi_str[16] = { 0 };

        const wchar_t* p = device_path.c_str();

        /* Find vid_ */
        const wchar_t* vid_pos = wcsstr(p, L"vid_");
        if (vid_pos) { wcsncpy_s(vid_str, vid_pos + 4, 4); vid = (WORD)wcstol(vid_str, NULL, 16); }

        /* Find pid_ */
        const wchar_t* pid_pos = wcsstr(p, L"pid_");
        if (pid_pos) { wcsncpy_s(pid_str, pid_pos + 4, 4); pid = (WORD)wcstol(pid_str, NULL, 16); }

        /* Find mi_ (interface) */
        const wchar_t* mi_pos = wcsstr(p, L"mi_");
        if (mi_pos) { wcsncpy_s(mi_str, mi_pos + 3, 2); iface = (int)wcstol(mi_str, NULL, 16); }

        if (vid != RAZER_VID)
            continue;
        if (pid != PID_WIRED && pid != PID_WIRELESS)
            continue;

        /* Convert path to narrow for CreateFile */
        char path_ansi[512] = { 0 };
        WideCharToMultiByte(CP_ACP, 0, device_path.c_str(), -1,
                            path_ansi, (int)sizeof(path_ansi) - 1, NULL, NULL);

        DeviceInfo dev;
        dev.path = path_ansi;
        dev.pid = pid;
        dev.is_wireless = (pid == PID_WIRELESS);
        dev.interface_number = iface;
        devices.push_back(dev);
    }

    SetupDiDestroyDeviceInfoList(dev_info);
    return !devices.empty();
}

/*---------------------------------------------------------*\
| HID I/O                                                   |
\*---------------------------------------------------------*/
static HANDLE OpenDevice(const char* path)
{
    HANDLE h = CreateFileA(
        path,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,  /* overlapped for timeout support */
        NULL);

    return h;
}

static bool SendReport(HANDLE h, const RazerReport& report)
{
    BYTE buf[91];
    BuildHidBuffer(buf, report);

    OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) return false;

    DWORD written = 0;
    bool ok = WriteFile(h, buf, sizeof(buf), &written, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING)
    {
        ok = GetOverlappedResult(h, &ov, &written, TRUE);
    }

    CloseHandle(ov.hEvent);
    return ok && written == sizeof(buf);
}

static bool ReadReport(HANDLE h, RazerReport& report)
{
    BYTE buf[91] = { 0 };
    buf[0] = 0;  /* Report ID */

    OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) return false;

    DWORD read = 0;
    bool ok = ReadFile(h, buf, sizeof(buf), &read, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING)
    {
        /* Wait with timeout */
        DWORD wait = WaitForSingleObject(ov.hEvent, READ_TIMEOUT_MS);
        if (wait == WAIT_TIMEOUT)
        {
            CancelIo(h);
            CloseHandle(ov.hEvent);
            return false;
        }
        ok = GetOverlappedResult(h, &ov, &read, TRUE);
    }

    CloseHandle(ov.hEvent);

    if (!ok || read < REPORT_SIZE + 1)
        return false;

    memcpy(report.data, buf + 1, REPORT_SIZE);
    return true;
}

/*---------------------------------------------------------*\
| Battery query                                             |
\*---------------------------------------------------------*/
static bool QueryBattery(HANDLE h, int& percent, bool& charging, bool& charging_known)
{
    percent = 0;
    charging = false;
    charging_known = false;

    /* Try battery level */
    for (int retry = 0; retry < MAX_RETRIES; retry++)
    {
        RazerReport req;
        req.SetTransactionId(TRANSACTION_ID);
        req.SetCommand(0x07, 0x80, 2);  /* CLASS_POWER, CMD_GET_BATTERY */
        req.Finalize();

        if (!SendReport(h, req))
        {
            printf("  [retry %d] Send failed (0x%lx)\n", retry + 1, GetLastError());
            Sleep(RETRY_BUSY_MS);
            continue;
        }

        Sleep(IO_DELAY_MS);

        RazerReport resp;
        if (!ReadReport(h, resp))
        {
            printf("  [retry %d] Read timeout\n", retry + 1);
            continue;
        }

        if (!resp.VerifyCrc())
        {
            printf("  [retry %d] CRC mismatch\n", retry + 1);
            continue;
        }

        BYTE status = resp.data[0];
        if (status == 0x01) /* busy */
        {
            printf("  [retry %d] Device busy, waiting...\n", retry + 1);
            Sleep(RETRY_BUSY_MS);
            continue;
        }

        if (status != 0x02) /* not success */
        {
            printf("  [retry %d] Unexpected status: 0x%02x\n", retry + 1, status);
            Sleep(200);
            continue;
        }

        BYTE raw = resp.data[9];
        percent = (static_cast<int>(raw) * 100) / 255;
        if (percent > 100) percent = 100;

        printf("  Battery raw value: %d (0x%02x) -> %d%%\n", raw, raw, percent);
        break;
    }

    /* Try charging status */
    charging_known = false;
    for (int retry = 0; retry < 3; retry++)
    {
        RazerReport req;
        req.SetTransactionId(TRANSACTION_ID);
        req.SetCommand(0x07, 0x84, 2);  /* CLASS_POWER, CMD_IS_CHARGING */
        req.Finalize();

        if (!SendReport(h, req))
        {
            Sleep(RETRY_BUSY_MS);
            continue;
        }

        Sleep(IO_DELAY_MS);

        RazerReport resp;
        if (!ReadReport(h, resp))
            continue;

        if (!resp.VerifyCrc())
            continue;

        if (resp.data[0] == 0x02)
        {
            charging = (resp.data[9] != 0);
            charging_known = true;
            printf("  Charging status: %s (raw=0x%02x)\n",
                   charging ? "YES" : "NO", resp.data[9]);
        }
        break;
    }

    return true;
}

/*---------------------------------------------------------*\
| Main                                                      |
\*---------------------------------------------------------*/
int main()
{
    printf("=== BatteryProbe: Razer DeathAdder V2 Pro ===\n\n");

    /* 1. Find device */
    std::vector<DeviceInfo> devices;
    if (!FindRazerDevice(devices) || devices.empty())
    {
        printf("ERROR: No Razer DeathAdder V2 Pro found (VID 0x%04x, PIDs 0x%04x/0x%04x)\n",
               RAZER_VID, PID_WIRED, PID_WIRELESS);
        printf("Check device is connected.\n");
        return 1;
    }

    printf("Found %zu Razer device(s):\n\n", devices.size());
    for (size_t i = 0; i < devices.size(); i++)
    {
        printf("  [%zu] PID 0x%04x (%s)  Interface %d\n",
               i, devices[i].pid,
               devices[i].is_wireless ? "Wireless/Dongle" : "Wired",
               devices[i].interface_number);
    }

    /* 2. Try all devices with interface 2 (battery interface) */
    int chosen = -1;
    for (size_t i = 0; i < devices.size(); i++)
    {
        if (devices[i].interface_number == TARGET_INTERFACE)
        {
            printf("\nTrying device [%zu]: PID 0x%04x (%s)  Interface %d\n",
                   i, devices[i].pid,
                   devices[i].is_wireless ? "Wireless/Dongle" : "Wired",
                   devices[i].interface_number);

            HANDLE h = OpenDevice(devices[i].path.c_str());
            if (h == INVALID_HANDLE_VALUE)
            {
                DWORD err = GetLastError();
                printf("  Cannot open (R+W): 0x%lx", err);
                if (err == ERROR_ACCESS_DENIED)
                {
                    /* Try read-only */
                    printf(" -> trying read-only...\n");
                    h = CreateFileA(
                        devices[i].path.c_str(),
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
                    if (h != INVALID_HANDLE_VALUE)
                        printf("  Opened read-only.\n");
                }
                else
                {
                    printf("\n");
                    continue;
                }
            }

            if (h == INVALID_HANDLE_VALUE)
            {
                DWORD err = GetLastError();
                printf("  Cannot open: 0x%lx", err);
                if (err == ERROR_ACCESS_DENIED)
                    printf(" (ACCESS_DENIED — Razer Synapse?)");
                else if (err == ERROR_FILE_NOT_FOUND ||
                         err == ERROR_DEVICE_NOT_CONNECTED)
                    printf(" (device disconnected)");
                printf("\n");
                continue;
            }

            /* Opened successfully — try battery query */
            printf("  Opened OK. Querying battery...\n");
            int percent = 0;
            bool charging = false;
            bool charging_known = false;

            if (QueryBattery(h, percent, charging, charging_known))
            {
                chosen = (int)i;
                CloseHandle(h);

                printf("\n=== RESULTS ===\n");
                printf("Connection: %s\n",
                       devices[chosen].pid == PID_WIRELESS ? "Wireless" : "Wired");
                printf("Battery:    %d%%\n", percent);
                if (charging_known)
                    printf("Charging:   %s\n", charging ? "Yes" : "No");
                else
                    printf("Charging:   Unknown\n");
                printf("Interface:  %d\n", devices[chosen].interface_number);
                printf("PID:        0x%04x\n", devices[chosen].pid);
                printf("Status:     OK\n");
                break;
            }
            else
            {
                printf("  Battery query failed on this interface.\n");
            }

            CloseHandle(h);
        }
    }

    if (chosen < 0)
    {
        printf("\nERROR: Could not read battery from any Interface %d device.\n",
               TARGET_INTERFACE);
        printf("  Possible causes:\n");
        printf("  - Razer Synapse is running (close from system tray)\n");
        printf("  - Device is sleeping\n");
        printf("  - Access denied (try running as Administrator)\n");
        return 1;
    }
    printf("\nDone.\n");
    return 0;
}