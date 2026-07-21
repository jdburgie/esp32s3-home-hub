// config.h — board pinout and compile-time defaults for esp32s3-home-hub
// Target: Waveshare ESP32-S3-LCD-1.47 (N16R8). LCD is dead and unused.
#pragma once

#define FW_NAME    "homehub"
#define FW_VERSION "0.2.3"

// ---- Verified pinout (Waveshare ESP32-S3-LCD-1.47 wiki) --------------------
// microSD is on the SDMMC peripheral in 4-bit mode.
#define SD_CLK 14
#define SD_CMD 15
#define SD_D0  16
#define SD_D1  18
#define SD_D2  17
#define SD_D3  21

#define PIN_RGB  38   // onboard WS2812 RGB LED
#define PIN_BOOT 0    // BOOT button (also used to force AP mode on long-press)

// ---- Network defaults ------------------------------------------------------
#define AP_SSID       "HomeHub-Setup"   // open AP shown when WiFi unconfigured
#define MDNS_HOSTNAME "homehub"          // -> http://homehub.local/
#define WEB_PORT      80

// Hold BOOT this long (ms) to wipe WiFi creds and return to the config portal.
#define FORCE_AP_HOLD_MS 3000

// ---- Feature limits --------------------------------------------------------
#define MAX_MONITOR_HOSTS 12   // presence monitor entries
#define MAX_NODES         12   // aggregator node entries
#define MAX_OUTPUTS       8    // control-panel GPIO outputs

#define MONITOR_INTERVAL_MS 15000  // how often to re-probe monitored hosts
#define NODE_INTERVAL_MS    20000  // how often to re-poll aggregator nodes
#define PROBE_TIMEOUT_MS    800    // TCP connect timeout for a presence probe

// Config file on the SD card.
#define CONFIG_PATH "/homehub/config.json"

// ---- Safe GPIOs for control outputs ---------------------------------------
// Only these broken-out header pins may be used as control outputs. Everything
// else on this board is spoken for and driving it can wedge the device:
//   0        BOOT button. Driven LOW it looks like a permanently-held button,
//            so checkForceApButton() wipes the WiFi credentials every 3 s and
//            reboots -- an unrecoverable portal loop until NVS is cleared.
//   14-21    microSD (SDMMC 4-bit). Driving these kills config + logging.
//   19,20    native USB D-/D+. Driving these kills serial AND the USB flash
//            path, leaving OTA as the only way back in.
//   38       onboard WS2812.
//   39-42,45,48  LCD (dead panel, but still wired).
static inline bool pinIsSafeOutput(int pin) {
  return (pin >= 1 && pin <= 6) || (pin >= 9 && pin <= 13);
}
