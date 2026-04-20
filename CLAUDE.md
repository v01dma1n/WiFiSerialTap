# CLAUDE.md — WiFiSerialTap Project Context

<!-- Do NOT include IP addresses, hostnames, credentials, network
     names, or any environment-specific details in this file. 
     Keep it architectural, no local paths etc. -->

## What This Is

WiFiSerialTap is an ESP32-S3-based USB inline serial sniffer. It sits
between a power source and a target device's USB cable, enumerates the
target's USB-UART bridge chip (CP210x, CH340, FTDI) as a USB host, and
relays the serial data stream over WiFi via a Telnet server. It also
mirrors all data to the UART console.

The owner is Irek (GitHub: v01dma1n). The project lives at
`~/projects/OpenSource/WiFiSerialTap`.

Vibe-coded with Claude Opus 4.6.

## Hardware Setup

- **ESP32-S3 dev board** with two USB ports (UART for flashing, USB OTG
  for host mode)
- Power source USB provides VBUS/GND to both the S3 and the target
- Target's USB cable plugs into the S3's USB OTG port
- The S3's USB OTG port does NOT supply VBUS — the target must be
  powered externally (via the S3's 5V pin or separate supply)
- Target board (Soyabell) uses a **CP210x** USB-UART bridge
  (VID `10c4`, PID `ea60`)

## Build Environment

- **Framework:** ESP-IDF v5.4.2 (NOT Arduino)
- **Target:** ESP32-S3 (`idf.py set-target esp32s3`)
- **IDE:** VS Code with ESP-IDF extension, or command line
- **ESP-IDF location:** `~/esp/esp-idf`
- **Flash via:** `/dev/ttyACM0` (USB-Serial-JTAG, not ttyUSB0)
- **C++ exceptions required:** the VCP component uses try/catch
  (`CONFIG_COMPILER_CXX_EXCEPTIONS=y`)

### Build Command

```bash
. ~/esp/esp-idf/export.sh
WST_OTA_KEY=<secret> idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

If flash fails: hold BOOT, tap RST, release BOOT → forces download mode.

### OTA Key

The OTA authentication key is injected via environment variable
`WST_OTA_KEY` at build time. It is NEVER stored in any file in the repo.
CMakeLists.txt will `FATAL_ERROR` if the variable is not set. This was
a deliberate security decision — `.gitignore` alone was deemed too weak
after a past credential leak incident.

## Project Structure

```
WiFiSerialTap/
├── CMakeLists.txt              Top-level ESP-IDF project
├── sdkconfig.defaults          Minimal SDK config
├── partitions.csv              Custom 16MB partition table with OTA
├── README.md
├── CLAUDE.md                   This file
├── wst_log.sh                  Host-side timestamped logging script
├── .gitignore
└── src/main/
    ├── CMakeLists.txt          Component registration + OTA key injection
    ├── idf_component.yml       Managed component dependencies
    ├── wst_main.h              Constants, shared types, declarations
    ├── main.c                  Entry point, data routing
    ├── wst_wifi.c              SoftAP provisioning + STA connection
    ├── wst_usb.cpp             USB host CDC/VCP driver (C++)
    ├── wst_telnet.c            Single-client TCP server
    └── wst_ota.c               HTTP OTA push endpoint
```

## Architecture & Data Flow

```
USB RX (target serial) → on_usb_rx callback
  ├── fwrite to stdout (UART console mirror)
  └── wst_telnet_send (network relay)

Telnet RX (host typing) → reverse_channel_task
  └── wst_usb_send (forwarded to target)
```

### Module Details

- **wst_wifi.c** — Uses ESP-IDF `wifi_prov_mgr` with SoftAP transport.
  On first boot, starts AP "WiFiSerialTap" / "wstconfig". Provisioned
  via `esp_prov.py` or the ESP SoftAP Prov phone app. Credentials
  persist in NVS. ESP32-S3 is 2.4 GHz only — routers with band
  steering may need Smart Connect disabled.

- **wst_usb.cpp** — C++ file (VCP API uses classes). Registers FT23x,
  CP210x, CH34x drivers. Runs a FreeRTOS task that loops: wait for
  device → open → configure 115200/8N1 → stream via callback → wait
  for disconnect → repeat. Public API functions use `extern "C"`.
  Uses `esp_usb` namespace (`using namespace esp_usb;`).

- **wst_telnet.c** — POSIX sockets, non-blocking, single client. New
  connections kick the previous one. Uses `inet_ntoa()` for logging
  (NOT `IPSTR`/`IP2STR` which don't work with raw `sockaddr_in`).

- **wst_ota.c** — HTTP server on port 8080. POST `/ota` (requires
  `X-OTA-Key` header), GET `/status` (unauthenticated, returns JSON).
  Writes to inactive OTA slot and reboots.

## Partition Table (16 MB flash)

```
nvs       24 KB    WiFi credentials, settings
otadata    8 KB    OTA boot slot tracking
phy_init   4 KB    RF calibration
ota_0      3 MB    Firmware slot A
ota_1      3 MB    Firmware slot B
storage   10 MB    Reserved (spiffs)
```

Binary is ~1 MB. Previous 1 MB single-partition layout was 98% full.

## Managed Components (idf_component.yml)

```yaml
dependencies:
  idf: ">=5.4"
  usb_host_cdc_acm: "*"
  usb_host_vcp: "^1"
  usb_host_ch34x_vcp: "^2"
  usb_host_cp210x_vcp: "^2"
  usb_host_ftdi_vcp: "^2"
  mdns: "*"
```

These download into `managed_components/` on first build. The individual
VCP driver headers (`usb/vcp_ch34x.hpp`, etc.) come from these separate
components, not from `usb_host_vcp` itself.

## Known Gotchas & Lessons Learned

1. **File timestamps from Claude downloads** can be in UTC while the
   local system is in a different timezone, causing ninja to loop
   infinitely ("manifest still dirty after 100 tries"). Fix: `touch`
   all downloaded files before building.

2. **Unknown Kconfig symbols in sdkconfig.defaults** cause the same
   infinite CMake loop. Keep sdkconfig.defaults minimal — only symbols
   that actually exist in the Kconfig tree.

3. **`WiFiClient::connected()` is not const** in the ESP32 Arduino
   core. Any wrapper calling it cannot be `const` either.

4. **BaseAccessPointManager form handler** (from v01dma1n/ESP32WiFi
   library) uses `strncpy(..., MAX_PREF_STRING_LEN - 1)` — all
   form field buffers must be at least `MAX_PREF_STRING_LEN` (64
   bytes) or you get stack overflow crashes.

5. **`xEventGroupWaitBits` with `pdTRUE` (clear on exit)** clears the
   bits before you can check them afterward. Use `pdFALSE` if you
   need to inspect the bits after the wait returns.

6. **ESP-IDF v5.4 moved `mdns` to the component registry** — it's no
   longer bundled. Must be added to `idf_component.yml`.

7. **The VCP component needs C++ exceptions** — without
   `CONFIG_COMPILER_CXX_EXCEPTIONS=y`, the build fails on try/catch
   in the VCP library code.

8. **Backslash at end of C comment lines** triggers `-Werror=comment`
   because the compiler treats the next line as part of the comment.

9. **`IPSTR`/`IP2STR` macros** don't work cleanly with POSIX
   `sockaddr_in`. Use `inet_ntoa()` instead.

10. **WiFi provisioning tool path** in ESP-IDF v5.4 is
    `$IDF_PATH/tools/esp_prov/esp_prov.py` (in a subdirectory, not
    at `$IDF_PATH/tools/esp_prov.py`). Requires `pip install protobuf`.

11. **ESP32-S3 USB OTG port doesn't supply VBUS** — target devices
    need external power.

12. **First IDF build takes 5-15 minutes** — compiles entire framework.
    Install `ccache` for faster rebuilds. Subsequent builds are
    incremental (seconds).

## Prior Version: ESP32-WROOM-32 (Arduino)

An earlier version of WiFiSerialTap exists for ESP32-WROOM-32 using the
Arduino framework and the v01dma1n/ESP32WiFi library. That version taps
the target's UART TX pin directly (GPIO 16) instead of going through
USB. The source files are `WiFiSerialTap.ino`, `WSTConfig.h`,
`WSTPreferences.h`, `WSTAccessPointManager.h`. It uses
ESPAsyncWebServer + AsyncTCP for the captive portal.

## Related Projects

- **Soyabell** — Irek's ESP32 breakout board (github.com/v01dma1n/Soyabell)
- **v01dma1n/ESP32WiFi** — WiFi connection + captive portal library used
  in the WROOM-32 version
