# OpenRGB Battery Monitor Plugin

OpenRGB plugin for monitoring **Razer DeathAdder V2 Pro** battery level.

**Repository:** https://github.com/Tyoman1/razer-deathadder-v2-battery

## Features

- Displays battery level (0–100%)
- Displays charging status
- Detects wired/wireless connection
- Auto-polling (default 60s)
- Manual Refresh button
- Last-known value on device sleep

## Supported Devices

| Device | VID | PID Wired | PID Wireless | Status |
|---|---|---|---|---|
| Razer DeathAdder V2 Pro | `0x1532` | `0x007C` | `0x007D` | ✅ Tested |

## How it works

- Queries battery via Windows HID **feature report**
- Uses `dwAccess=0` + `HidD_SetFeature`/`HidD_GetFeature` (query-only; does not use GENERIC_READ/WRITE)
- Works on HID Interface 0 (Mouse), FeatureReportByteLength=91
- **Does not modify** RGB, DPI, device settings or firmware
- **Does not require** administrator rights
- **Does not require** Razer Synapse
- **Does not conflict** with OpenRGB RGB control (same interface, different access mode)

## Requirements

- OpenRGB (any version with Plugin API v5 support)
- Windows 10/11 x64
- Razer DeathAdder V2 Pro (wired or wireless/dongle)

## Installation

1. Build from CI or download artifact
2. Place `OpenRGBBatteryPlugin.dll` in OpenRGB `plugins/` directory
3. Restart OpenRGB
4. Plugin appears as "Battery Monitor" tab in Settings

## Build

### CI

`.github/workflows/build.yml` — builds automatically on push to `main`.

### Local

```powershell
cmake -S . -B build -A x64 -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build --config Release
```

Requires: Windows, MSVC 2022, Qt 6.8.3 `win64_msvc2022_64`.

## Artifacts

CI artifact `OpenRGBBatteryPlugin-1.0.0` contains:

| File | Description |
|---|---|
| `OpenRGBBatteryPlugin.dll` | OpenRGB plugin |
| `OpenRGBBatteryPlugin.sha256.txt` | SHA-256 checksum |
| `BUILD_REPORT.md` | Build report |
| `OpenRGBBatteryPlugin-1.0.0-source.zip` | Source archive |
| `BatteryProbe.exe` | Standalone HID diagnostic tool |

## BatteryProbe

`tools/BatteryProbe/BatteryProbe.exe` is a standalone diagnostic tool for testing HID access.
It enumerates all HID collections and attempts battery read via multiple methods.
Useful for debugging or adding support for new devices.

## Limitations

- Supports only Razer DeathAdder V2 Pro (not a universal battery reader)
- Does **not** control RGB or manage lighting profiles
- Does **not** replace or interact with OpenRGB Wake Plugin
- Bluetooth (PID 0x008E) not tested

## Security

- No network access
- No process execution
- No registry writes
- No kernel drivers
- Read-only HID access

## License

Based on the original OpenRGB plugin SDK (GPL-2.0-or-later).  
Protocol research references: OpenRazer (GPL-2.0), razer-battery (MIT), Exo.