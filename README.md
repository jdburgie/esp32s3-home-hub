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

Node titles link through to the node itself. The running firmware version is
shown as a badge in the header — the quickest check that an OTA landed.

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

The config is written to **both** SD and NVS, each stamped with a monotonic
`rev`. At boot the copy with the higher `rev` wins, so a stale SD file cannot
override a newer NVS mirror. Ties go to SD, so hand-editing `config.json` still
works when the two are in step. `rev` appears in the Settings JSON but is ignored
on save — the firmware owns the counter. The boot log names both revisions and
the winner:

```
config: SD rev 2 (ok), NVS rev 2 (ok) -> loading SD
config: 1 hosts, 2 nodes, 0 outputs
```

Saves are verified by byte count and reported honestly: the Settings tab says so
if the SD write fails, rather than always claiming "saved".

## Config JSON

`hosts` and `nodes` are easy to conflate — they take **different address forms**:

| | Purpose | Address |
|---|---|---|
| `hosts` (Presence tab) | Is it reachable? Raw TCP connect | **Bare IP/hostname** — `192.168.12.50` |
| `nodes` (Nodes tab) | What does it say? HTTP GET | **Full URL** — `http://192.168.12.50` |

Putting a URL in a `hosts` entry makes it permanently unreachable — the probe
tries to DNS-resolve the whole string. Presence dots: green = up, red = was up
and is now down, **grey = never once succeeded** (usually a misconfiguration).

## Build

Arduino-ESP32 core (v3.x). See [JOURNAL.md](JOURNAL.md) for the exact FQBN,
board options, and library list.

```
arduino-cli compile --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc firmware/homehub
arduino-cli upload  --fqbn esp32:esp32:esp32s3 -p COM22 firmware/homehub
```

`app3M_fat9M_16MB` = "16M Flash (3MB APP/9.9MB FATFS)" — two 3 MB OTA app slots
plus a 9.9 MB FAT partition. v0.2.2 uses 38% of the app slot.

> An earlier version of this file said `PartitionScheme=default_16MB`. **That
> value does not exist** on the esp32 core (verified against 3.3.10 — the FQBN
> is rejected outright). Use the scheme above. Run
> `arduino-cli board details --fqbn esp32:esp32:esp32s3` to list valid values.

Only external library: **ArduinoJson** by Benoît Blanchon (`bblanchon`), v7 API.
Not to be confused with **Arduino_JSON**, a different library with a confusingly
similar name — if the sketchbook has that one, it will not satisfy this include.
Everything else (WiFi, WebServer, SD_MMC, Preferences, HTTPClient,
`neopixelWrite`) ships with the core.

### OTA password

Copy `firmware/homehub/secrets.example.h` to `secrets.h` (gitignored) and set
`OTA_PASSWORD`. Without it the build still works, but OTA is unauthenticated —
anyone on the LAN can reflash a board that drives relays. Once flashed, later
OTA pushes need `--upload-field password=<yours>`.

## Control outputs

Only GPIOs **1-6** and **9-13** may be used as control outputs; the config
parser drops anything else and the Settings tab tells you when it did. The rest
of the board is spoken for — SD (14-21), native USB (19/20), WS2812 (38), LCD
(39-42/45/48), and BOOT (0). GPIO0 is the dangerous one: driven LOW it reads as
a permanently-held BOOT button, so the 3 s force-AP timer wipes the WiFi
credentials and reboots, forever.
