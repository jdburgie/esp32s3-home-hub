# esp32s3-home-hub

A headless LAN web dashboard running on a **Waveshare ESP32-S3-LCD-1.47** whose
1.47" LCD is physically damaged. The screen is gone, so the browser on your
phone/laptop is the display instead. The board is otherwise fully intact
(ESP32-S3 N16R8: 16 MB flash, 8 MB PSRAM, WiFi/BLE, native USB-C, WS2812 LED,
microSD slot).

Access it at `http://homehub.local/` (mDNS) on your home network.

## Features

1. **Node dashboard (aggregator)** — polls other devices on your LAN (weather
   station, e-paper display, sprinkler controllers, …) and shows their status on
   one page.
2. **Presence / network monitor** — probes a configurable list of hosts and
   shows which are online, with latency and last-seen.
3. **Control panel** — toggle GPIO outputs (relays, etc.) and drive the onboard
   RGB LED from the browser. No display needed.

## First-boot behaviour (AP config portal)

If no WiFi credentials are stored, the hub boots into **AP mode**:

- SSID: `HomeHub-Setup` (open) — join it from a phone.
- Browse to `http://192.168.4.1/` — pick your network, enter the password, save.
- The hub reboots and joins your WiFi as `homehub.local`.

Hold the **BOOT** button (GPIO0) for ~3 s at any time to wipe WiFi and force the
portal again.

## Hardware pinout (verified — Waveshare ESP32-S3-LCD-1.47 wiki)

| Function        | GPIO |
|-----------------|------|
| microSD CLK     | 14   |
| microSD CMD     | 15   |
| microSD D0      | 16   |
| microSD D1      | 18   |
| microSD D2      | 17   |
| microSD D3      | 21   |
| RGB LED (WS2812)| 38   |
| BOOT button     | 0    |

SD uses the **SDMMC** peripheral (4-bit), not SPI. LCD pins (45/40/42/41/39/48)
are unused — the panel is dead.

## Storage

- **WiFi credentials + core settings** → NVS (`Preferences`), so the hub keeps
  working even with no SD card.
- **App config + logs** → microSD (FAT), at `/homehub/config.json`. ~8 GB card is
  plenty. If no card is present, config falls back to NVS and logging is skipped.

## Build

Arduino-ESP32 core (v3.x). See [JOURNAL.md](JOURNAL.md) for the exact FQBN,
board options, and library list.

```
arduino-cli compile --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default_16MB firmware/homehub
arduino-cli upload  --fqbn esp32:esp32:esp32s3 -p COM22 firmware/homehub
```

Only external library: **ArduinoJson**. Everything else (WiFi, WebServer,
SD_MMC, Preferences, HTTPClient, `neopixelWrite`) ships with the core.
