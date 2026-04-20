> *Vibe-coded with Claude Opus 4.6*

# WiFiSerialTap — ESP32-S3 USB Inline Serial Sniffer

A wireless debugging tool that sits inline on a USB cable, enumerates the
target's USB-UART bridge (CP210x, CH340, FTDI), and relays its serial
output over WiFi via a Telnet server. Non-invasive, reusable — just plug
the target's USB cable into the tap.

```
┌─────────────┐       ┌──────────────────┐       ┌─────────────┐
│ Power       │  5V   │   ESP32-S3       │  USB  │ Target DUT  │
│ (PC/charger)│──────▶│   USB Host +     │◀─────▶│ (Soyabell,  │
│             │       │   WiFi Relay     │       │  any CDC)   │
└─────────────┘       └────────┬─────────┘       └─────────────┘
                               │ WiFi
                               ▼
                       ┌────────────────┐
                       │ Ubuntu host    │
                       │ telnet IP 23   │
                       └────────────────┘
```

## Hardware

- **ESP32-S3 dev board** with two USB ports (one for UART/flashing, one
  for USB OTG host)
- **Input USB-C breakout** — power source side (VBUS + GND pass-through)
- **Output USB-C breakout** — target device side (USB cable plugs into
  S3's USB OTG port directly)

### Wiring

```
Power source USB ──── VBUS ──┬──── ESP32-S3 5V/VIN
                     GND  ──┼──── ESP32-S3 GND
                            │
                            └──── (also powers target via the S3's 5V rail)

Target USB cable ────────── ESP32-S3 USB OTG port (labeled "USB")
```

The UART port on the S3 is used for flashing and the debug console.

## Build

Requires ESP-IDF v5.4+ and ~5 GB free disk space.

The OTA authentication key is passed as an environment variable — it
never exists in any file in the repository:

```bash
. ~/esp/esp-idf/export.sh
cd ~/projects/OpenSource/WiFiSerialTap
idf.py set-target esp32s3
WST_OTA_KEY=your-secret-key idf.py build
idf.py -p /dev/ttyACM0 erase-flash flash monitor
```

If `flash` fails with a connection error, force download mode: hold
**BOOT**, tap **RST**, release **BOOT**, then retry flashing.

For convenience, export the key for the session:

```bash
export WST_OTA_KEY=your-secret-key
idf.py build
```

The build will fail with a clear error if `WST_OTA_KEY` is not set.

### Dependencies

Pulled automatically by the component manager on first build into
`managed_components/`:

- `espressif/usb_host_cdc_acm`
- `espressif/usb_host_vcp`
- `espressif/usb_host_cp210x_vcp`
- `espressif/usb_host_ch34x_vcp`
- `espressif/usb_host_ftdi_vcp`
- `espressif/mdns`

### Partition Layout (16 MB flash)

| Partition | Type   | Size  | Purpose                         |
|-----------|--------|-------|---------------------------------|
| nvs       | data   | 24 KB | WiFi credentials, settings      |
| otadata   | data   | 8 KB  | OTA boot slot tracking          |
| phy_init  | data   | 4 KB  | RF calibration                  |
| ota_0     | app    | 3 MB  | Firmware slot A                 |
| ota_1     | app    | 3 MB  | Firmware slot B                 |
| storage   | data   | ~10 MB| Reserved for future use         |

## First Boot — WiFi Provisioning

On first boot (or after NVS erase), the device starts a SoftAP:

- **SSID:** `WiFiSerialTap`
- **Password:** `wstconfig`

Connect your phone or laptop to that AP, then provision with your real
WiFi credentials:

```bash
. ~/esp/esp-idf/export.sh
python3 $IDF_PATH/tools/esp_prov/esp_prov.py \
  --transport softap \
  --ssid YOUR_WIFI_SSID \
  --passphrase YOUR_WIFI_PASSWORD
```

If `esp_prov.py` complains about missing `google` module:

```bash
pip install protobuf --break-system-packages
```

After provisioning succeeds, the device connects to your WiFi, prints
the assigned IP in the console, and starts the Telnet server on port 23.
Credentials persist in NVS across reboots — no re-provisioning needed.

**Note:** The ESP32-S3 radio is 2.4 GHz only. If your router uses band
steering (single SSID for 2.4 GHz and 5 GHz), you may need to disable
Smart Connect / band steering in the router admin panel so the ESP32 can
associate.

## Usage

Power the target and plug its USB cable into the S3's USB OTG port. The
console should show:

```
waiting for USB VCP device...
USB VCP device opened, configuring 115200/8N1
device ready, streaming data
```

Then connect from any host on your WiFi:

```bash
telnet <device-ip> 23
```

Or if you prefer `nc`:

```bash
nc <device-ip> 23
```

Target's serial output streams in real-time. Data you type into the
Telnet session is forwarded back to the target (reverse channel).

### Timestamped Logging

To add timestamps to every line and save to a log file:

```bash
sudo apt install moreutils   # one-time install for the 'ts' command
nc <device-ip> 23 | ts '[%Y-%m-%d %H:%M:%S]' | tee ~/log/wst_session.log
```

This gives timestamped output on screen and saves it to
`~/log/wst_session.log` simultaneously. Example output:

```
[2026-04-15 22:41:03] WiFi connected
[2026-04-15 22:41:03] IP address: 192.168.1.207
[2026-04-15 22:41:03] Starting Listener.
```

For a shorter timestamp format, use `ts '[%H:%M:%S]'` instead.

## OTA Firmware Update

The device runs an HTTP server on port 8080 for over-the-air updates.
Updates are authenticated with the `X-OTA-Key` header.

### Check current version

```bash
curl http://<device-ip>:8080/status
```

Returns JSON with app name, version, active partition, and IDF version.

### Push new firmware

```bash
WST_OTA_KEY=your-secret-key idf.py build
curl -H "X-OTA-Key: your-secret-key" \
     http://<device-ip>:8080/ota \
     --data-binary @build/wifi_serial_tap.bin
```

The device validates the image, writes it to the inactive OTA slot,
switches the boot partition, and reboots automatically. WiFi credentials
are preserved — no re-provisioning needed.

A request without the correct `X-OTA-Key` header returns `403 Forbidden`.

## Re-provisioning

To clear stored WiFi credentials and enter provisioning mode again:

```bash
idf.py -p /dev/ttyACM0 erase-flash flash
```

## Configuration

- **Serial baud rate:** defaults to 115200/8N1. To change, edit the
  `line_coding` struct in `src/main/wst_usb.cpp`.
- **Telnet port:** default is 23. Change `WST_TELNET_PORT` in
  `src/main/wst_main.h`.
- **OTA port:** default is 8080. Change `WST_OTA_PORT` in
  `src/main/wst_main.h`.
- **SoftAP credentials:** change `WST_DEVICE_NAME` and
  `WST_PROV_AP_PASSWORD` in `src/main/wst_main.h`.

## Architecture

```
src/main/
├── main.c              Entry point, data routing
├── wst_wifi.c          SoftAP provisioning + STA connect
├── wst_usb.cpp         USB host CDC/VCP driver (C++)
├── wst_telnet.c        Single-client TCP server
├── wst_ota.c           HTTP OTA push endpoint
└── wst_main.h          Shared types and declarations
```

- **Provisioning** — uses ESP-IDF `wifi_prov_mgr` with SoftAP transport.
  Credentials go straight into NVS.
- **USB host** — runs on a dedicated FreeRTOS task. Waits for any
  supported VCP device, opens it at 115200/8N1, and streams received
  data to a callback.
- **Telnet** — raw POSIX socket, non-blocking, single client. New
  connections replace the previous one.
- **OTA** — HTTP POST endpoint on port 8080, authenticated with a
  shared key injected at build time via environment variable. Writes
  to the inactive OTA partition and reboots.
- **Data flow** — USB RX → console mirror + telnet send. Telnet RX →
  USB TX (reverse channel).
