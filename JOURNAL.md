# JOURNAL — esp32s3-home-hub

## ▶ PICK UP HERE (as of 2026-07-21, end of session)

**Device state:** live, healthy, running **v0.2.2** — the current committed
firmware. Address is now **`192.168.12.131`** (new DHCP lease after the reflash;
it was `.188`). `homehub.local` resolves correctly. MAC `a0:85:e3:ef:f6:98`,
RSSI −36, heap ~238 KB, SD mounted (15103 MB).

**OTA works.** Verified end-to-end twice, with password auth
(`Authenticating (PBKDF2-HMAC-SHA256)... OK`), device rebooting into the new
image each time. USB is no longer needed for updates:
```
arduino-cli upload -p 192.168.12.131 --protocol network \
  --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc \
  --upload-field 'password=...' firmware/homehub
```
The password contains a `*` — **single-quote it** or zsh fails with "no matches
found" before it ever contacts the board.

### Open question: config reverted once, unexplained

After the USB flash, the config came back as an **older** copy — the presence
host back to its broken URL form, and the `sprinkler` .51 node missing entirely.
Not the config that had just been saved, and not the one before it either.

Re-applied it and rebooted via OTA: **it persisted correctly**. So whatever
happened has not reproduced, and the cause is unknown. Do not assume it is fixed.

Two real weaknesses in the code make this class of bug invisible, and are worth
fixing regardless:
- `apiConfigPost` **ignores the return value of `storeSaveConfig()`**
  (`web.cpp`), so the UI replies "saved" even if the SD write failed. Same in
  `apiOutput` and `apiLed`.
- `storeLoadConfig()` prefers the **SD file over the NVS mirror** (`store.cpp`).
  A stale SD copy therefore silently overrides a newer NVS config — exactly the
  symptom seen. Consider a version/timestamp field, or preferring NVS.

Next time config appears to revert, **capture the serial boot log** — it prints
`SD: …` and `config: N hosts, N nodes, N outputs`, which would settle it.

### Next steps
- [ ] Fix the two persistence weaknesses above.
- [ ] `sprinkler` at `192.168.12.51` returns "connection refused" — something is
      at that IP with nothing on port 80. That is `esp32-sprinkler-controller`;
      may be a known state, not investigated.
- [ ] Non-blocking node polling (see the review notes below).
- [ ] Web UI still has no auth (OTA now does).

## 2026-07-21 — Live device probed from the Mac; config bug fixed; OTA found dead

Device was already back on WiFi (portal reconfigured out-of-band). Probed it over
HTTP from the Mac rather than serial — the JSON API makes this easy and it should
be the first move next time instead of assuming the journal's last-known state.

**Presence monitor was permanently broken by a config typo.** The one monitored
host had `"host": "http://192.168.12.50"` — a **URL**. `monitorTick()` does a raw
`WiFiClient::connect(host, port)`, which needs a bare IP/hostname, so it tried to
DNS-resolve the whole string and always failed → `online:false, since:-1`, even
though the node aggregator reached that same box fine (`code:200`). Nodes take
URLs, hosts take bare IPs; easy to conflate in the Settings JSON. Fixed live via
`POST /api/config` → now `online:true, latency 9ms`.

**That typo was also stalling the whole device.** Measured 12 requests 2 s apart
before the fix: one took **4.1 s just to TCP-connect**, another 0.77 s, rest
10–150 ms. A stalled *connect* means `loop()` wasn't running to service the
accept backlog. Cause: the doomed DNS lookup blocks, and `PROBE_TIMEOUT_MS` (800)
applies to the TCP connect, **not** the DNS phase, so the lwIP DNS timeout runs
unbounded — every 15 s. After the fix, same test: **12/12 under 115 ms**, no
outliers. Confirms the blocking-`loop()` concern from the v0.2.2 review with real
numbers, and shows one bad config entry can degrade the entire device.

**~~OTA is not listening.~~ RETRACTED — this was a bad test.** Earlier in this
session I concluded OTA was dead because `nc -z <ip> 3232` reported "connection
refused" 3/3, and I speculated the flashed `merged.bin` predated the OTA code.
**Both wrong.**

`nc -z` tests **TCP**. **ArduinoOTA listens on UDP 3232** — the espota protocol
sends a UDP invitation, then the *device* dials back to the host's TCP port. A
perfectly healthy ArduinoOTA will always show "refused" to a TCP probe on 3232.

The only valid test is an actual OTA push. When run, it authenticated and
flashed on the first attempt. **Never conclude a service is down from a TCP
probe without checking the protocol it actually uses.**

**Node state at time of probe:** `epaper` .50 up (200), `sprinkler2` .52 up (200),
`sprinkler` **.51 down — "connection refused"**, i.e. something answers at that IP
but nothing is on port 80. That's `esp32-sprinkler-controller`; may be a known
state, not investigated here.

**USB-flashed v0.2.2, app slot only — NVS survived.** Board plugged into the Mac,
enumerated as `/dev/cu.usbmodem2101`. Verified before writing anything:
- `read-mac` → `a0:85:e3:ef:f6:98`, right board.
- Dumped the **on-chip partition table** at 0x8000 and decoded it. It is exactly
  `app3M_fat9M_16MB`: `nvs` @0x9000, `otadata` @0xe000, `app0` @0x10000 (3145728 B
  — matches the compiler's "Maximum is 3145728"), `app1` @0x310000, `ffat`
  @0x610000, `coredump` @0xff0000. This **settles the partition question with
  hardware evidence**, not inference.
- Built `partitions.bin` was **byte-identical** (sha256) to the on-chip table.
- `otadata` seq0=1, seq1=0 → active slot `(1-1)%2 = app0`, so 0x10000 is live.

Then `esptool write-flash 0x10000 homehub.ino.bin` at default baud (115200) —
**`Hash of data verified`**, no reset error. Device rebooted, rejoined WiFi on
its own, confirming NVS at 0x9000 was untouched. It took a **new DHCP lease
(.131)**, which briefly looked like a failed boot — check mDNS before panicking.

Do **not** write `homehub.ino.merged.bin` (16 MB) at 0x0; that is the artifact
that wiped NVS last time. The app-only write at 0x10000 is the safe path.

**Config reverted once after that flash — cause unknown.** See PICK UP HERE.
Backup of the pre-fix config: `/tmp/hub-config-before.json` (ephemeral — the
device's own SD copy at `/homehub/config.json` is the real one).

## 2026-07-20 (later still) — v0.2.2: code review, output-pin safety, OTA password

Read the whole firmware on a machine with no toolchain (so **nothing here is
compile-verified** — do that on the PC before the OTA push).

**Latent soft-brick found and fixed.** `storeConfigFromJson()` defaulted a
missing `pin` to **0**, and validated nothing. An `outputs` entry with no `pin`
(or any bad pin) meant `featuresInit()` did `pinMode(0, OUTPUT); digitalWrite(0,
LOW)` → `checkForceApButton()` read GPIO0 as a permanently-held button → after
3 s it wiped the WiFi creds and rebooted → no creds → AP portal. The bad output
is still on the SD card, so re-entering WiFi just repeats it. Recovery needs
pulling the SD *and* clearing NVS. Never triggered (current config is 1 host /
3 nodes, no outputs) but v0.2.1 persisting output state made it more reachable.
Now: `pinIsSafeOutput()` in config.h allowlists **1-6, 9-13**; bad entries are
dropped with a serial line, and the Settings tab reports the count instead of
silently losing them.

**OTA password added.** Was unauthenticated — anyone on the LAN could reflash a
board that drives relays. Now `secrets.h` (gitignored, `secrets.example.h`
committed) defines `OTA_PASSWORD`. No secrets.h → still builds, still OTAs, but
prints a loud boot warning rather than failing quietly.

**Reviewed, not fixed** (none blocking, roughly in priority order):
- `nodesTick()` blocks `loop()` up to ~4 s (1500 ms connect + 2500 ms read) on
  one unreachable node. Starves `webHandle()` and `ArduinoOTA.handle()` — can
  time out an OTA push mid-upload, which matters now that OTA is the safe path.
- `http.getString()` reads the whole body into RAM before truncating to 160
  chars. A large node response can exhaust the heap. Wants a bounded read.
- Every GPIO toggle and LED change rewrites the full config blob to NVS. Fine at
  human rates, bad if anything ever automates toggling.
- `WiFi.scanNetworks()` runs in pure `WIFI_AP` mode in the portal. Works on this
  core version; `WIFI_AP_STA` is safer if it ever returns empty.
- Removing an output from config leaves its pin still driven at the last level.
- No mDNS re-registration after a WiFi drop/reconnect.
- Web UI still has no auth (separate from OTA).

---

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
      **90% full**. Flash with `PartitionScheme=app3M_fat9M_16MB` (3 MB app
      slots) for headroom; drops to ~40%.
      *(Corrected 2026-07-21: this line originally said `default_16MB`, which is
      not a real value — the core rejects the FQBN. It was clearly never run.)*
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
- [x] OTA password (v0.2.2) — needs `secrets.h` created before the build.
- [x] Validate control-output GPIO pins (v0.2.2).
- [x] Compile-check v0.2.2 — passes on the Mac, with and without `secrets.h`.
      1201091 bytes = **38%** of the 3 MB app slot; 54176 bytes static RAM (16%).
- [ ] Later: optional ICMP ping instead of TCP-connect probe; auth on the UI;
      non-blocking node polling (see v0.2.2 review notes).

### Build / flash (arduino-cli)
```
# core (once):  arduino-cli core install esp32:esp32
# lib  (once):  arduino-cli lib install ArduinoJson
arduino-cli compile --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc firmware/homehub
arduino-cli upload  --fqbn esp32:esp32:esp32s3 -p COM22 firmware/homehub
```
Serial is native USB — no external adapter. 115200 baud.

### Mac toolchain (set up 2026-07-21)
The Mac can now build this — no need to go to the PC except to flash over USB.
- `brew install arduino-cli` (1.5.1). It reads the Arduino IDE's existing
  `~/Library/Arduino15`, so **esp32 3.3.10 was already there** — no core install.
- `arduino-cli lib install ArduinoJson` → 7.4.3, same as the PC.
- **Gotcha:** the sketchbook already had `Arduino_JSON` (Arduino's own, `JSONVar`
  API). That is *not* `ArduinoJson` (bblanchon, `JsonDocument`) and does not
  satisfy the include. Both are installed now; they coexist fine.
- macOS has no COM ports — the board enumerates as `/dev/cu.usbmodem*`. USB
  flashing from the Mac is untested; OTA is the intended path anyway.
