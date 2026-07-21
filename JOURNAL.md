# JOURNAL — esp32s3-home-hub

## 2026-07-20 (later) — Bring-up: flashed, on WiFi, v0.2.1

Got the board flashed and running. Hardware works: SD mounts (15103 MB of a
16 GB card), WiFi joins, AP portal works, dashboard serves, mDNS resolves.
Reached `homehub.local` / 192.168.12.188 and drove the JSON API from the PC.

**Hard-won flashing lessons (this board's native USB is fragile):**
- `arduino-cli upload` bumps to **921600 baud and the write silently fails** —
  esptool prints "Changed" then jumps straight to the reset with no "Writing"
  step, and reports only a confusing reset error. The firmware never lands.
  **Flash with standalone esptool at default baud instead.**
- The final `Hard resetting via RTS pin` often errors with "device does not
  exist" because the USB CDC re-enumerates as the chip resets. Cosmetic *if*
  the write actually completed — always confirm `Hash of data verified`.
- **Writing `merged.bin` at 0x0 overwrites the whole 16 MB, including NVS —
  it wipes the stored WiFi credentials.** Did this and lost them. Config on the
  SD card survived (validating the SD+NVS dual-write), but the hub dropped back
  to the setup portal. **Use OTA, or write only the app at 0x10000.**
- The USB CDC **wedges after a serial session** (port shows OK but esptool gets
  PermissionError, or VID_303A vanishes entirely). Only a physical unplug/replug
  restores it. The BOOT button does *not* reset the board or re-enumerate USB.
- `netsh wlan show networks` is an unreliable way to spot the AP — heavily
  cached and often blank while the AP is definitely up. Trust the serial log.

**Bugs found on hardware and fixed:**
- **WS2812 R/G channels swapped** — user reported "pure red is green". These
  LEDs take data green-first. All writes now go through `rgbLedWrite()` (one
  place, documented) which swaps R/G. Blue unaffected.
- **Status LED invisible** — colours were 24/255. Raised; user genuinely could
  not see the board was doing anything.
- **`WiFi.macAddress()` before WiFi init returns all zeroes.** `WiFi.mode()` is
  *not* enough to populate it (tried, still zeroes). Fixed with `esp_read_mac()`.
- Settings buttons were ambiguous — "Reload" read as a device reset. Renamed to
  **Revert** and added help text saying neither button reboots.
- `.gitignore` had `/build/` (root-anchored) so arduino-cli's build dir inside
  the sketch folder got committed. Now `build/`.

**Added:** ArduinoOTA (v0.2.0) so updates go over WiFi and never touch NVS.
**Added:** output on/off states persist across reboot (v0.2.1, user's call —
they want a relay that was on to come back on).

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
- [x] microSD inserted — 16 GB, mounts fine (15103 MB).
- [x] Flashed to COM22; serial @115200 confirms clean boot.
- [x] AP portal works; WiFi configured; `homehub.local` reachable (192.168.12.188).
- [x] Git remote `jdburgie/esp32s3-home-hub` created and pushed.
- [x] Persist output states across reboot (v0.2.1).
- [ ] **Re-do WiFi setup via the portal** — creds were wiped by the merged.bin
      flash. Node/host config survived on SD.
- [ ] **Then OTA-push v0.2.1** (no USB): `arduino-cli upload -p homehub.local
      --protocol network` or espota. Verify OTA path works end to end.
- [ ] Confirm on hardware: LED now shows correct colours (red==red) and is visible.
- [ ] Add real nodes/hosts via Settings; confirm aggregator + presence work.
      User had 1 host / 3 nodes configured before the wipe — re-check they're intact.
- [ ] Later: optional ICMP ping instead of TCP-connect probe; auth on the UI;
      OTA password.

### Build / flash (arduino-cli)
```
# core (once):  arduino-cli core install esp32:esp32
# lib  (once):  arduino-cli lib install ArduinoJson
arduino-cli compile --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default_16MB firmware/homehub
arduino-cli upload  --fqbn esp32:esp32:esp32s3 -p COM22 firmware/homehub
```
Serial is native USB — no external adapter. 115200 baud.
