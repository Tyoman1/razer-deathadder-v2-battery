/*---------------------------------------------------------*\
| BatteryProbe.exe v2                                        |
|                                                            |
|  HID topology inspector + battery reader for               |
|  Razer DeathAdder V2 Pro.                                  |
|                                                            |
|  Phase 1: enumerate all HID collections with full          |
|           details (VID, PID, Interface, Usage Page,        |
|           Usage, report sizes).                            |
|                                                            |
|  Phase 2: attempt battery read using the same approach     |
|           as RazeCLI rawhid backend.                       |
|                                                            |
|  This file is part of the                                 |
|  razer-deathadder-v2-battery project.                      |
|                                                            |
|  Repository:                                               |
|  https://github.com/Tyoman1/razer-deathadder-v2-battery    |
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
static constexpr WORD  RAZER_VID        = 0x1532;
static constexpr WORD  PID_WIRED        = 0x007C;
static constexpr WORD  PID_WIRELESS     = 0x007D;
static constexpr int   REPORT_SIZE      = 90;
static constexpr int   IO_DELAY_MS      = 100;
static constexpr int   READ_TIMEOUT_MS  = 1000;
static constexpr int   MAX_RETRIES      = 5;
static constexpr DWORD RETRY_BUSY_MS    = 300;
static constexpr BYTE  TRANSACTION_ID   = 0x3F;

/*---------------------------------------------------------*\
| HID device entry — full details                           |
\*---------------------------------------------------------*/
struct HidEntry
{
    std::string path;
    WORD        vid;
    WORD        pid;
    int         interface_number;

    /* HID descriptor properties */
    USAGE       usage_page;
    USAGE       usage;
    ULONG       input_len;
    ULONG       output_len;
    ULONG       feature_len;

    /* Parsed from path string */
    std::string instance_id;

    std::string UsagePageName() const
    {
        switch (usage_page)
        {
            case 0x01: return "Generic Desktop Controls";
            case 0x02: return "Simulation Controls";
            case 0x05: return "Game Controls";
            case 0x07: return "Keyboard";
            case 0x08: return "LED";
            case 0x0C: return "Consumer";
            case 0xFF00: return "Vendor-defined (page FF00)";
            case 0xFFFFFF00: return "Vendor-defined (page FFFFF00)";
            default: {
                char buf[32];
                sprintf_s(buf, "0x%04X", usage_page);
                return buf;
            }
        }
    }

    std::string UsageName() const
    {
        if (usage_page == 0x01)
        {
            switch (usage)
            {
                case 0x01: return "Pointer";
                case 0x02: return "Mouse";
                case 0x04: return "Joystick";
                case 0x05: return "Game Pad";
                case 0x06: return "Keyboard";
                case 0x07: return "Keypad";
                case 0x08: return "Multi-axis Controller";
                case 0x80: return "System Control";
                case 0x90: return "D-pad";
                default: { char buf[32]; sprintf_s(buf, "0x%04X", usage); return buf; }
            }
        }
        if (usage_page == 0x07)
        {
            switch (usage)
            {
                case 0x01: return "Keyboard";
                default: { char buf[32]; sprintf_s(buf, "0x%04X", usage); return buf; }
            }
        }
        if (usage_page == 0x0C)
        {
            switch (usage)
            {
                case 0x01: return "Consumer Control";
                default: { char buf[32]; sprintf_s(buf, "0x%04X", usage); return buf; }
            }
        }
        if (usage_page == 0x08)
        {
            if (usage == 0x01) return "LED (RGB?)";
            { char buf[32]; sprintf_s(buf, "0x%04X", usage); return buf; }
        }
        if ((usage_page & 0xFF000000) == 0xFF000000)
        {
            char buf[32];
            sprintf_s(buf, "Vendor 0x%04X", usage);
            return buf;
        }
        { char buf[32]; sprintf_s(buf, "0x%04X", usage); return buf; }
    }

    bool IsMouse() const
    {
        return (usage_page == 0x01 && usage == 0x02);
    }

    bool IsKeyboard() const
    {
        return (usage_page == 0x01 && usage == 0x06) ||
               (usage_page == 0x07 && usage == 0x01);
    }

    bool IsVendorCollection() const
    {
        /* Razer vendor-specific collections often have usage_page >= 0xFF00 */
        return (usage_page >= 0xFF00) ||
               (interface_number >= 2 && !IsMouse() && !IsKeyboard());
    }

    bool IsRazerBatteryCandidate() const
    {
        /* Battery is typically on a vendor-specific collection
           that is NOT the mouse or keyboard input path.
           On Interface 2, but with proper Usage check. */
        return (usage_page >= 0xFF00 || 
                (interface_number == 2 && !IsMouse() && !IsKeyboard())) &&
               input_len >= REPORT_SIZE;
    }
};

/*---------------------------------------------------------*\
| HID enumeration — full detail                             |
\*---------------------------------------------------------*/
static std::vector<HidEntry> EnumerateHidDevices()
{
    std::vector<HidEntry> result;

    GUID hid_guid;
    HidD_GetHidGuid(&hid_guid);

    HDEVINFO dev_info = SetupDiGetClassDevsW(
        &hid_guid, NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (dev_info == INVALID_HANDLE_VALUE)
    {
        printf("ERROR: SetupDiGetClassDevsW failed (0x%lx)\n", GetLastError());
        return result;
    }

    SP_DEVICE_INTERFACE_DATA dev_iface = { sizeof(SP_DEVICE_INTERFACE_DATA) };

    for (DWORD idx = 0;
         SetupDiEnumDeviceInterfaces(dev_info, NULL, &hid_guid, idx, &dev_iface);
         idx++)
    {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(dev_info, &dev_iface, NULL, 0, &required, NULL);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) continue;

        std::vector<BYTE> buffer(required);
        auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(dev_info, &dev_iface,
                                               detail, (DWORD)buffer.size(),
                                               &required, NULL))
            continue;

        std::wstring device_path = detail->DevicePath;

        /* Parse VID, PID, MI from path */
        WORD vid = 0, pid = 0;
        int mi = -1;
        const wchar_t* p = device_path.c_str();

        const wchar_t* vid_pos = wcsstr(p, L"vid_");
        if (vid_pos) { wchar_t vs[16]={0}; wcsncpy_s(vs, vid_pos+4, 4); vid = (WORD)wcstol(vs,NULL,16); }

        const wchar_t* pid_pos = wcsstr(p, L"pid_");
        if (pid_pos) { wchar_t ps[16]={0}; wcsncpy_s(ps, pid_pos+4, 4); pid = (WORD)wcstol(ps,NULL,16); }

        const wchar_t* mi_pos = wcsstr(p, L"mi_");
        if (mi_pos) { wchar_t ms[16]={0}; wcsncpy_s(ms, mi_pos+3, 2); mi = (int)wcstol(ms,NULL,16); }

        /* Filter: only Razer DeathAdder V2 Pro */
        if (vid != RAZER_VID) continue;
        if (pid != PID_WIRED && pid != PID_WIRELESS) continue;

        /* Convert path to ANSI for CreateFile */
        char path_ansi[512] = { 0 };
        WideCharToMultiByte(CP_ACP, 0, device_path.c_str(), -1,
                            path_ansi, (int)sizeof(path_ansi)-1, NULL, NULL);

        HidEntry entry;
        entry.path = path_ansi;
        entry.vid = vid;
        entry.pid = pid;
        entry.interface_number = mi;
        entry.usage_page = 0;
        entry.usage = 0;
        entry.input_len = 0;
        entry.output_len = 0;
        entry.feature_len = 0;

        /* Open the device briefly to read HID properties */
        HANDLE h = CreateFileA(
            path_ansi,
            0,  /* dwDesiredAccess = 0 — no read/write, just to query */
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, 0, NULL);

        if (h != INVALID_HANDLE_VALUE)
        {
            HIDD_ATTRIBUTES attr = { sizeof(HIDD_ATTRIBUTES) };
            if (HidD_GetAttributes(h, &attr))
            {
                entry.vid = attr.VendorID;
                entry.pid = attr.ProductID;
            }

            /* Get preparsed data for usage page/usage */
            PHIDP_PREPARSED_DATA preparsed = NULL;
            if (HidD_GetPreparsedData(h, &preparsed) && preparsed)
            {
                HIDP_CAPS caps;
                if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS)
                {
                    entry.usage_page = caps.UsagePage;
                    entry.usage = caps.Usage;
                    entry.input_len = caps.InputReportByteLength;
                    entry.output_len = caps.OutputReportByteLength;
                    entry.feature_len = caps.FeatureReportByteLength;
                }
                HidD_FreePreparsedData(preparsed);
            }

            /* Get the HID device's instance ID from the device info */
            SP_DEVINFO_DATA dev_info_data = { sizeof(SP_DEVINFO_DATA) };
            if (SetupDiEnumDeviceInfo(dev_info, idx, &dev_info_data))
            {
                WCHAR instance_id[256] = { 0 };
                if (CM_Get_Device_IDW(dev_info_data.DevInst, instance_id,
                                      (ULONG)(sizeof(instance_id)/sizeof(WCHAR)), 0) == CR_SUCCESS)
                {
                    char id_ansi[256] = { 0 };
                    WideCharToMultiByte(CP_ACP, 0, instance_id, -1,
                                        id_ansi, (int)sizeof(id_ansi)-1, NULL, NULL);
                    entry.instance_id = id_ansi;
                }
            }

            CloseHandle(h);
        }

        result.push_back(entry);
    }

    SetupDiDestroyDeviceInfoList(dev_info);
    return result;
}

/*---------------------------------------------------------*\
| Razer HID report helpers                                   |
\*---------------------------------------------------------*/
struct RazerReport
{
    BYTE data[REPORT_SIZE];
    RazerReport() { memset(data, 0, sizeof(data)); }

    void SetTransactionId(BYTE tid) { data[1] = tid; }
    void SetCommand(BYTE cmd_class, BYTE cmd_id, BYTE data_size = 2)
    {
        data[4] = 0; data[5] = data_size; data[6] = cmd_class; data[7] = cmd_id;
    }

    BYTE ComputeCrc() const
    {
        BYTE crc = 0;
        for (int i = 2; i < 88; i++) crc ^= data[i];
        return crc;
    }
    void Finalize() { data[88] = ComputeCrc(); data[89] = 0; }
    bool VerifyCrc() const { return data[88] == ComputeCrc(); }
};

static void BuildHidBuffer(BYTE* out_buf, const RazerReport& report)
{
    out_buf[0] = 0; /* Report ID = 0 */
    memcpy(out_buf + 1, report.data, REPORT_SIZE);
}

/*---------------------------------------------------------*\
| Battery query via output report + input report            |
| (RazeCLI's approach)                                      |
\*---------------------------------------------------------*/
static bool TryReadBatteryViaWriteRead(HANDLE h, int& percent, bool& charging, bool& charging_known)
{
    percent = 0;
    charging = false;
    charging_known = false;

    /* --- Battery level --- */
    for (int retry = 0; retry < MAX_RETRIES; retry++)
    {
        RazerReport req;
        req.SetTransactionId(TRANSACTION_ID);
        req.SetCommand(0x07, 0x80, 2);
        req.Finalize();

        BYTE buf[91];
        BuildHidBuffer(buf, req);

        OVERLAPPED ov = { 0 };
        ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!ov.hEvent) return false;

        DWORD written = 0;
        BOOL ok = WriteFile(h, buf, sizeof(buf), &written, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
            ok = GetOverlappedResult(h, &ov, &written, TRUE);
        CloseHandle(ov.hEvent);

        if (!ok || written != sizeof(buf))
        {
            Sleep(RETRY_BUSY_MS);
            continue;
        }

        Sleep(IO_DELAY_MS);

        /* Read response */
        BYTE resp_buf[91] = { 0 };
        resp_buf[0] = 0;

        OVERLAPPED ov2 = { 0 };
        ov2.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!ov2.hEvent) return false;

        DWORD read = 0;
        ok = ReadFile(h, resp_buf, sizeof(resp_buf), &read, &ov2);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
        {
            DWORD wait = WaitForSingleObject(ov2.hEvent, READ_TIMEOUT_MS);
            if (wait == WAIT_TIMEOUT) { CancelIo(h); CloseHandle(ov2.hEvent); continue; }
            ok = GetOverlappedResult(h, &ov2, &read, TRUE);
        }
        CloseHandle(ov2.hEvent);

        if (!ok || read < REPORT_SIZE + 1) continue;

        RazerReport resp;
        memcpy(resp.data, resp_buf + 1, REPORT_SIZE);

        if (!resp.VerifyCrc()) continue;

        BYTE status = resp.data[0];
        if (status == 0x01) { Sleep(RETRY_BUSY_MS); continue; }
        if (status != 0x02) { Sleep(200); continue; }

        BYTE raw = resp.data[9];
        percent = (static_cast<int>(raw) * 100) / 255;
        if (percent > 100) percent = 100;
        break;
    }

    /* --- Charging status --- */
    for (int retry = 0; retry < 3; retry++)
    {
        RazerReport req;
        req.SetTransactionId(TRANSACTION_ID);
        req.SetCommand(0x07, 0x84, 2);
        req.Finalize();

        BYTE buf[91];
        BuildHidBuffer(buf, req);

        OVERLAPPED ov = { 0 };
        ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!ov.hEvent) break;

        DWORD written = 0;
        BOOL ok = WriteFile(h, buf, sizeof(buf), &written, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
            ok = GetOverlappedResult(h, &ov, &written, TRUE);
        CloseHandle(ov.hEvent);
        if (!ok || written != sizeof(buf)) { Sleep(RETRY_BUSY_MS); continue; }

        Sleep(IO_DELAY_MS);

        BYTE resp_buf[91] = { 0 };
        resp_buf[0] = 0;
        OVERLAPPED ov2 = { 0 };
        ov2.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!ov2.hEvent) break;

        DWORD read = 0;
        ok = ReadFile(h, resp_buf, sizeof(resp_buf), &read, &ov2);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
        {
            DWORD wait = WaitForSingleObject(ov2.hEvent, READ_TIMEOUT_MS);
            if (wait == WAIT_TIMEOUT) { CancelIo(h); CloseHandle(ov2.hEvent); continue; }
            ok = GetOverlappedResult(h, &ov2, &read, TRUE);
        }
        CloseHandle(ov2.hEvent);
        if (!ok || read < REPORT_SIZE + 1) continue;

        RazerReport resp;
        memcpy(resp.data, resp_buf + 1, REPORT_SIZE);
        if (!resp.VerifyCrc()) continue;
        if (resp.data[0] == 0x02)
        {
            charging = (resp.data[9] != 0);
            charging_known = true;
            break;
        }
    }

    return true;
}

/*---------------------------------------------------------*\
| Battery query via feature report (HidD_GetFeature)        |
\*---------------------------------------------------------*/
static bool TryReadBatteryViaFeature(HANDLE h, int& percent, bool& charging, bool& charging_known)
{
    percent = 0;
    charging = false;
    charging_known = false;

    /* --- Battery level --- */
    for (int retry = 0; retry < MAX_RETRIES; retry++)
    {
        RazerReport req;
        req.SetTransactionId(TRANSACTION_ID);
        req.SetCommand(0x07, 0x80, 2);
        req.Finalize();

        BYTE out_buf[91] = { 0 };
        BuildHidBuffer(out_buf, req);

        if (!HidD_SetFeature(h, out_buf, sizeof(out_buf)))
        {
            DWORD err = GetLastError();
            printf("    HidD_SetFeature failed: 0x%lx\n", err);
            Sleep(RETRY_BUSY_MS);
            continue;
        }

        Sleep(IO_DELAY_MS);

        BYTE in_buf[91] = { 0 };
        in_buf[0] = 0;
        if (!HidD_GetFeature(h, in_buf, sizeof(in_buf)))
        {
            printf("    HidD_GetFeature failed: 0x%lx\n", GetLastError());
            continue;
        }

        RazerReport resp;
        memcpy(resp.data, in_buf + 1, REPORT_SIZE);
        if (!resp.VerifyCrc()) { printf("    CRC mismatch\n"); continue; }

        BYTE status = resp.data[0];
        if (status == 0x01) { Sleep(RETRY_BUSY_MS); continue; }
        if (status != 0x02) { Sleep(200); continue; }

        BYTE raw = resp.data[9];
        percent = (static_cast<int>(raw) * 100) / 255;
        if (percent > 100) percent = 100;
        printf("    Battery raw=%d -> %d%%\n", raw, percent);
        break;
    }

    /* --- Charging --- */
    for (int retry = 0; retry < 3; retry++)
    {
        RazerReport req;
        req.SetTransactionId(TRANSACTION_ID);
        req.SetCommand(0x07, 0x84, 2);
        req.Finalize();

        BYTE out_buf[91] = { 0 };
        BuildHidBuffer(out_buf, req);

        if (!HidD_SetFeature(h, out_buf, sizeof(out_buf))) { Sleep(RETRY_BUSY_MS); continue; }
        Sleep(IO_DELAY_MS);

        BYTE in_buf[91] = { 0 };
        in_buf[0] = 0;
        if (!HidD_GetFeature(h, in_buf, sizeof(in_buf))) continue;

        RazerReport resp;
        memcpy(resp.data, in_buf + 1, REPORT_SIZE);
        if (!resp.VerifyCrc()) continue;
        if (resp.data[0] == 0x02)
        {
            charging = (resp.data[9] != 0);
            charging_known = true;
            break;
        }
    }
    return true;
}

/*---------------------------------------------------------*\
| Access method — try every possible way                    |
\*---------------------------------------------------------*/
struct AccessResult
{
    bool        opened;
    HANDLE      handle;
    std::string method_name;
    DWORD       last_error;
};

static AccessResult TryOpenDevice(const char* path, DWORD desired_access)
{
    HANDLE h = CreateFileA(
        path,
        desired_access,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING,
        (desired_access != 0) ? FILE_FLAG_OVERLAPPED : 0,
        NULL);

    AccessResult result;
    result.handle = h;
    result.last_error = GetLastError();
    result.opened = (h != INVALID_HANDLE_VALUE);

    switch (desired_access)
    {
        case GENERIC_READ | GENERIC_WRITE:
            result.method_name = "Read+Write";
            break;
        case GENERIC_READ:
            result.method_name = "Read-only";
            break;
        case 0:
            result.method_name = "dwAccess=0 (query only)";
            break;
        default:
            result.method_name = "Other";
            break;
    }
    return result;
}

static void PrintAccessResult(const char* label, const AccessResult& result)
{
    printf("    %s: %s", label, result.method_name.c_str());
    if (result.opened)
        printf(" ✅ Opened\n");
    else
        printf(" ❌ 0x%lx\n", result.last_error);
}

/*---------------------------------------------------------*\
| Main                                                      |
\*---------------------------------------------------------*/
int main()
{
    printf("=== BatteryProbe v2: Razer DeathAdder V2 Pro HID Topology ===\n");
    printf("VID: 0x%04X | PIDs: 0x%04X (wired), 0x%04X (wireless)\n\n",
           RAZER_VID, PID_WIRED, PID_WIRELESS);

    /* Phase 1: enumerate all HID collections */
    auto devices = EnumerateHidDevices();

    if (devices.empty())
    {
        printf("ERROR: No Razer DeathAdder V2 Pro found.\n");
        printf("Check device is connected.\n");
        return 1;
    }

    printf("Found %zu HID collection(s):\n\n", devices.size());

    printf("%-3s  %-6s  %-3s  %-8s  %-6s  %-25s  %-25s  %-5s  %-5s  %-5s\n",
           "#", "PID", "MI", "InLen", "OutLen", "Usage Page", "Usage", "Feat",
           "Mouse?", "Batt?");
    printf("%-3s  %-6s  %-3s  %-8s  %-6s  %-25s  %-25s  %-5s  %-5s  %-5s\n",
           "---", "------", "---", "-------", "------",
           "-------------------------", "-------------------------",
           "-----", "------", "------");

    int candidate_idx = -1;

    for (size_t i = 0; i < devices.size(); i++)
    {
        const auto& d = devices[i];
        bool mouse = d.IsMouse();
        bool kbd = d.IsKeyboard();
        bool batt_candidate = d.IsRazerBatteryCandidate();
        if (batt_candidate && candidate_idx < 0) candidate_idx = (int)i;

        char pid_str[16];
        sprintf_s(pid_str, "0x%04X", d.pid);

        printf("%-3zu  %-6s  %-3d  %-7lu  %-6lu  %-25s  %-25s  %-5lu  %-5s  %-5s\n",
               i, pid_str, d.interface_number,
               d.input_len, d.output_len,
               d.UsagePageName().c_str(),
               d.UsageName().c_str(),
               d.feature_len,
               mouse ? "MOUSE" : (kbd ? "KBD" : ""),
               batt_candidate ? "CAND" : "");
    }

    /* Phase 2: try accessing a battery candidate */
    if (candidate_idx < 0)
    {
        printf("\nNo battery candidate found.\n");
        printf("(No HID collection with vendor usage page or Interface 2 with 90-byte reports)\n");
    }
    else
    {
        const auto& target = devices[candidate_idx];
        printf("\n=== Phase 2: Battery Read Attempt ===\n");
        printf("Selected candidate [%d]:\n", candidate_idx);
        printf("  Path:   %s\n", target.path.c_str());
        printf("  PID:    0x%04X\n", target.pid);
        printf("  MI/IF:  %d\n", target.interface_number);
        printf("  Usage:  %s / %s\n", target.UsagePageName().c_str(), target.UsageName().c_str());
        printf("  InLen:  %lu  OutLen:  %lu  FeatLen:  %lu\n\n",
               target.input_len, target.output_len, target.feature_len);

        /* Try all three access modes */
        printf("--- Open attempts ---\n");
        auto rw   = TryOpenDevice(target.path.c_str(), GENERIC_READ | GENERIC_WRITE);
        auto ro   = TryOpenDevice(target.path.c_str(), GENERIC_READ);
        auto zero = TryOpenDevice(target.path.c_str(), 0);

        PrintAccessResult("  ", rw);
        PrintAccessResult("  ", ro);
        PrintAccessResult("  ", zero);

        printf("\n--- Battery query ---\n");

        /* Try feature report with dwAccess=0 handle first (if opened) */
        if (zero.opened)
        {
            printf("  Trying feature report (dwAccess=0)...\n");
            int pct = 0; bool chg = false, chg_known = false;
            if (TryReadBatteryViaFeature(zero.handle, pct, chg, chg_known))
            {
                printf("\n  ✅ Battery via FEATURE REPORT:\n");
                printf("     Level: %d%%\n", pct);
                if (chg_known) printf("     Charging: %s\n", chg ? "Yes" : "No");
            }
            else
            {
                printf("  ❌ Feature report failed\n");
            }
            CloseHandle(zero.handle);
        }

        /* Try write/read with R+W handle */
        if (rw.opened)
        {
            printf("  Trying WriteFile/ReadFile (Read+Write)...\n");
            int pct = 0; bool chg = false, chg_known = false;
            if (TryReadBatteryViaWriteRead(rw.handle, pct, chg, chg_known))
            {
                printf("\n  ✅ Battery via WRITEFILE/READFILE:\n");
                printf("     Level: %d%%\n", pct);
                if (chg_known) printf("     Charging: %s\n", chg ? "Yes" : "No");
            }
            else
            {
                printf("  ❌ WriteFile/ReadFile failed\n");
            }
            CloseHandle(rw.handle);
        }

        /* Try feature report with zero handle even if it failed */
        if (!zero.opened)
        {
            printf("  (no zero-access handle available)\n");
        }

        if (!rw.opened && !zero.opened)
        {
            printf("\n  ❌ Could not open device with any access method.\n");
            printf("     All attempts failed. The device may be:\n");
            printf("     - held exclusively by another driver (Synapse, Windows HID driver)\n");
            printf("     - require a different access approach\n");
        }
    }

    printf("\n=== Done ===\n");
    return 0;
}