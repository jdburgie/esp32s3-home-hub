# JOURNAL — esp32s3-home-hub

## 2026-07-20 — Repo bootstrapped, v0.1 firmware foundation

**Board (read before writing anything):** Waveshare ESP32-S3-LCD-1.47, on **COM22**
(native USB-Serial/JTAG, VID 303A / PID 1001).
- ESP32-S3 (QFN56) rev v0.2, dual-core 240 MHz + LP core
- **16 MB** flash (Winbond W25Q128, `ef:4018`), **8 MB** octal PSRAM (AP_3v3) → N16R8
- **MAC `a0:85:e3:ef:f6:98`**
- Was running the stock Waveshare Arduino factory demo (`project_name:
  arduino-lib-builder`, built Jan 6 2025, IDF v5.3.2). Arduino OTA partition
  layout with a 10 MB FAT partition. Nothing worth keeping — safe to overwrite.
- **1.47" LCD is physically damaged** → headless; the browser is the display.

**Verified pinout (Waveshare wiki):**
- microSD = **SDMMC 4-bit**: CLK=14, CMD=15, D0=16, D1=18, D2=17, D3=21
- RGB LED (WS2812) = **GPIO38** (drivable now via core `neopixelWrite()`, no lib)
- BOOT button = **GPIO0** (used for force-AP)
- LCD (unused): MOSI45/SCLK40/CS42/DC41/RST39/BL48
- Safe broken-out header GPIOs for control outputs: **1-6, 9-13** (avoid SD/LED/USB pins)

**What was built (v0.1 firmware, `firmware/homehub/`):**
- `config.h` — pins + defaults. `state.h` — shared `AppState G`.
- `net.*` — WiFi STA connect, creds in NVS (`wifi` ns), **AP config portal** with
  captive DNS (SSID `HomeHub-Setup` → http://192.168.4.1/, scan + save + reboot).
  LED = amber connecting / green online / red fail / blue portal.
- `store.*` — SD_MMC 4-bit mount, config as JSON at `/homehub/config.json`,
  mirrored to NVS (`homehub` ns) so it runs with no card. `storeLog()` → SD.
- `features.*` — (3) presence monitor: round-robin TCP-connect probes;
  (2) node aggregator: round-robin HTTPClient GET, keeps code + 160-char snippet;
  (4) control: GPIO output toggles + WS2812 colour.
- `web.*` — dashboard SPA (Nodes / Presence / Control / Settings tabs) + JSON API
  (`/api/status|monitor|nodes|control|output|led|config`). Settings tab edits the
  raw config JSON.
- `homehub.ino` — orchestration; AP fallback; BOOT-held (boot or 3 s runtime) → portal.

**Design choices:**
- Presence probe is **TCP-connect**, not ICMP ping — no extra library, but a host
  with no open TCP port reads as "down". Default probe port 80; per-host override
  in config. (Could add ESP32Ping later for true ICMP.)
- Config kept **project-separate** — no other repo's IPs hardcoded. Add the weather
  station / e-paper / sprinkler URLs via the Settings tab at runtime.
- Only external lib: **ArduinoJson** (v7 API). Rest ships with the core.

### TODO / next session
- [x] **Compile check** — PASSES on esp32:esp32 3.3.10 + ArduinoJson 7.4.3.
      Note: with no `PartitionScheme` it defaulted to the 1.25 MB app slot →
      **90% full**. Flash with `PartitionScheme=default_16MB` (3 MB app slots) for
      headroom; drops to ~40%.
- [ ] User to insert the ~8 GB microSD (FAT32).
- [ ] First flash to COM22, watch serial @115200, confirm AP portal appears.
- [ ] Join `HomeHub-Setup`, configure WiFi, confirm `homehub.local` reachable.
- [ ] Add real nodes/hosts via Settings; confirm aggregator + presence work.
- [ ] Decide git remote (GitHub `jdburgie/esp32s3-home-hub`?) and push.
- [ ] Later: persist output states across reboot; optional ICMP ping; auth on the UI.

### Build / flash (arduino-cli)
```
# core (once):  arduino-cli core install esp32:esp32
# lib  (once):  arduino-cli lib install ArduinoJson
arduino-cli compile --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default_16MB firmware/homehub
arduino-cli upload  --fqbn esp32:esp32:esp32s3 -p COM22 firmware/homehub
```
Serial is native USB — no external adapter. 115200 baud.
