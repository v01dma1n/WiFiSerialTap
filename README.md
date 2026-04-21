> *Vibe-coded with Claude Opus 4.6*

# WiFiSerialTap — ESP32-S3 USB Inline Serial Sniffer

A wireless serial debugging tool that requires zero wiring to the
target. Just plug the target's USB cable into the tap — WiFiSerialTap
enumerates the USB-UART bridge, reads the serial stream as a USB host,
and relays it over WiFi in real time.

```
┌─────────────┐       ┌──────────────────┐       ┌──────────────┐
│ Power       │  5V   │   WiFiSerialTap  │  USB  │ Target DUT   │
│ (PC/charger)│──────▶│   ESP32-S3       │◀─────▶│ (any board   │
│             │       │   USB Host +     │       │  with CP210x,│
│             │       │   WiFi Relay     │       │  CH340, FTDI)│
└─────────────┘       └────────┬─────────┘       └──────────────┘
                               │ WiFi
                               ▼
                       ┌────────────────┐
                       │ Dev workstation│
                       │ telnet IP:23   │
                       └────────────────┘
```

## What Makes This Different

Every existing wireless serial bridge — esp-link, ESP32-UART-Bridge,
MorphStick, and dozens of others — requires you to tap UART wires
directly: solder or clip onto TX, RX, and GND on the target board. That
means opening enclosures, identifying pins, and running jumper wires.

WiFiSerialTap takes a fundamentally different approach: it operates as a
**USB host**. The ESP32-S3's USB OTG peripheral enumerates the target's
USB-UART bridge chip (CP210x, CH340, FTDI) — the same chip that your PC
normally talks to. The target board doesn't know or care that it's not
plugged into a computer.

This means:

- **No wiring to the target.** Plug in a USB cable and you're done.
- **Works with any board** that has a standard USB-UART bridge. Arduino,
  ESP32, STM32, Raspberry Pi Pico — if it shows up as a serial port on
  your PC, it works with WiFiSerialTap.
- **Non-invasive.** No soldering, no modified firmware on the target, no
  exposed debug headers needed.
- **Reusable across projects.** One tool, any target.

## Hardware

- **ESP32-S3 dev board** with two USB ports (one for UART/flashing, one
  for USB OTG host)
- **USB power source** — PC, charger, or battery pack to power both the
  S3 and the target

### Wiring

```
Power source USB ──── VBUS ──┬──── ESP32-S3 5V/VIN
                     GND  ──┼──── ESP32-S3 GND
                            │
                            └──── (also powers target via the S3's 5V rail)

Target USB cable ────────── ESP32-S3 USB OTG port (labeled "USB")
```

The S3's USB OTG port does not supply VBUS by default — the target must
be powered through the S3's 5V rail or an external source.

The UART port on the S3 is used for flashing and the debug console.

### Enclosure

A 3D-printable enclosure is available on OnShape:

[WiFiSerialTap Enclosure](https://cad.onshape.com/documents/1a81ed11f778a8ae66ed4351/w/8b706b69c2fdf03546665b0f/e/0b6b5d467c1a0489c680da45)

<img src="photos/10.%20WiFiSerialTap%20-%203D%20Printed%20Case.jpg" width="400"><br><sub>3D Printed Case</sub>

<img src="photos/20.%20WiFiSerialTap%20-%20Soldering%20USB%20wires%20to%20connectors.jpg" width="400"><br><sub>Soldering USB wires to connectors</sub>

<img src="photos/30.%20WiFiSerialTap%20-%20USB%20Ports%20Installed.jpg" width="400"><br><sub>USB Ports Installed</sub>

<img src="photos/40.%20WiFiSerialTap%20-%20Ready%20to%20close%20the%20enclosure.jpg" width="400"><br><sub>Ready to close the enclosure</sub>

## Build

Requires ESP-IDF v5.4+ and ~5 GB free disk space.

The OTA authentication key is passed as an environment variable — it
never exists in any file in the repository:

```bash
. <path-to-esp-idf>/export.sh
cd <project-directory>
idf.py set-target esp32s3
WST_OTA_KEY=<your-secret> idf.py build
idf.py -p <port> erase-flash flash monitor
```

If `flash` fails with a connection error, force download mode: hold
**BOOT**, tap **RST**, release **BOOT**, then retry flashing.

For convenience, export the key for the session:

```bash
export WST_OTA_KEY=<your-secret>
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

| Partition | Type   | Size   | Purpose                         |
|-----------|--------|--------|---------------------------------|
| nvs       | data   | 24 KB  | WiFi credentials, settings      |
| otadata   | data   | 8 KB   | OTA boot slot tracking          |
| phy_init  | data   | 4 KB   | RF calibration                  |
| ota_0     | app    | 3 MB   | Firmware slot A                 |
| ota_1     | app    | 3 MB   | Firmware slot B                 |
| storage   | data   | ~10 MB | Reserved for future use         |

## First Boot — WiFi Provisioning

On first boot (or after NVS erase), the device starts a SoftAP:

- **SSID:** `WiFiSerialTap`
- **Password:** `wstconfig`

Connect your phone or laptop to that AP, then provision with your real
WiFi credentials:

```bash
. <path-to-esp-idf>/export.sh
python3 $IDF_PATH/tools/esp_prov/esp_prov.py \
  --transport softap \
  --ssid <your-wifi-ssid> \
  --passphrase <your-wifi-password>
```

If `esp_prov.py` complains about missing `google` module:

```bash
pip install protobuf --break-system-packages
```

After provisioning succeeds, the device connects to your WiFi, prints
the assigned IP in the console, and starts the Telnet server on port 23.
Credentials persist in NVS across reboots and firmware updates — no
re-provisioning needed.

**Note:** The ESP32-S3 radio is 2.4 GHz only. If your router uses band
steering (single SSID for 2.4 GHz and 5 GHz), you may need to disable
Smart Connect in the router admin panel.

## Usage

<img src="photos/50.%20WiFiSerialTap%20-%20Connected%20to%20DUT.jpg" width="400"><br><sub>Connected to DUT</sub>

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

Or with `nc`:

```bash
nc <device-ip> 23
```

Target's serial output streams in real-time. Data you type into the
Telnet session is forwarded back to the target (reverse channel).

### Timestamped Logging

To add timestamps to every line and save to a log file:

```bash
sudo apt install moreutils   # one-time install for the 'ts' command
nc <device-ip> 23 | ts '[%Y-%m-%d %H:%M:%S]' | tee ~/wst_session.log
```

This gives timestamped output on screen and saves it to
`~/wst_session.log` simultaneously. Example output:

```
[2026-04-15 22:41:03] WiFi connected
[2026-04-15 22:41:03] IP address: 192.168.1.207
[2026-04-15 22:41:03] Starting Listener.
```

<img src="photos/60.%20WiFiSerialTap%20-%20Telnet%20Output.png" width="400"><br><sub>Telnet Output</sub>

A convenience script `wst_log.sh` is included:

```bash
chmod +x wst_log.sh
./wst_log.sh <device-ip>
```

## OTA Firmware Update

The device runs an HTTP server on port 8080 for over-the-air updates.
Updates are authenticated with the `X-OTA-Key` header — the same key
set via `WST_OTA_KEY` at build time.

### Check current version

```bash
curl http://<device-ip>:8080/status
```

Returns JSON with app name, version, active partition, and IDF version.

### Push new firmware

```bash
WST_OTA_KEY=<your-secret> idf.py build
curl -H "X-OTA-Key: <your-secret>" \
     http://<device-ip>:8080/ota \
     --data-binary @build/wifi_serial_tap.bin
```

The device validates the image, writes it to the inactive OTA slot,
switches the boot partition, and reboots automatically. WiFi credentials
are preserved — no re-provisioning needed.

A request without the correct key returns `403 Forbidden`.

## Re-provisioning

To clear stored WiFi credentials and enter provisioning mode again:

```bash
idf.py -p <port> erase-flash flash
```

## Configuration

| Setting          | Default | Location                  |
|------------------|---------|---------------------------|
| Serial baud rate | 115200  | `wst_usb.cpp` line_coding |
| Telnet port      | 23      | `wst_main.h`              |
| OTA port         | 8080    | `wst_main.h`              |
| SoftAP SSID      | WiFiSerialTap | `wst_main.h`        |
| SoftAP password  | wstconfig | `wst_main.h`            |
| OTA key          | (env var) | `WST_OTA_KEY` at build  |

## Architecture

```
src/main/
├── main.c              Entry point, data routing
├── wst_wifi.c          SoftAP provisioning + STA connect
├── wst_usb.cpp         USB host CDC/VCP driver (C++)
├── wst_telnet.c        Single-client TCP server
├── wst_ota.c           HTTP OTA push endpoint with auth
└── wst_main.h          Shared types and declarations
```

- **Provisioning** — ESP-IDF `wifi_prov_mgr` with SoftAP transport.
  Credentials stored in NVS.
- **USB host** — dedicated FreeRTOS task. Waits for any supported VCP
  device, opens at 115200/8N1, streams via callback. Handles
  hot-plug/unplug automatically.
- **Telnet** — raw POSIX socket, non-blocking, single client. New
  connections replace the previous one.
- **OTA** — HTTP POST on port 8080, authenticated with a shared key
  injected at build time via environment variable. Writes to the
  inactive OTA partition and reboots.
- **Data flow** — USB RX → UART console mirror + Telnet send.
  Telnet RX → USB TX (reverse channel).

## License

MIT
