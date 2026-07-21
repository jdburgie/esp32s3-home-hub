// homehub.ino — Waveshare ESP32-S3-LCD-1.47 repurposed as a headless LAN
// dashboard: node aggregator, presence monitor, and a browser control panel.
// The 1.47" LCD is dead and unused; the web UI is the display.
//
// Boots into an AP config portal when WiFi is unconfigured (or BOOT held).
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <esp_mac.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "state.h"
#include "store.h"
#include "net.h"
#include "web.h"
#include "features.h"
// Optional, gitignored. Defines OTA_PASSWORD. See secrets.example.h.
#if __has_include("secrets.h")
  #include "secrets.h"
#endif

AppState G;  // the one global state instance

// BOOT (GPIO0) is active-low. Held at power-on -> wipe WiFi and show portal.
static bool bootHeldAtStart() {
  uint32_t t0 = millis();
  while (millis() - t0 < 800) {
    if (digitalRead(PIN_BOOT) != LOW) return false;
    delay(20);
  }
  return true;
}

// Held for FORCE_AP_HOLD_MS during normal running -> forget WiFi and reboot.
static void checkForceApButton() {
  static uint32_t downSince = 0;
  if (digitalRead(PIN_BOOT) == LOW) {
    if (downSince == 0) downSince = millis();
    else if (millis() - downSince > FORCE_AP_HOLD_MS) {
      ledStatus(0, 0, 24);
      netClearCreds();
      delay(300);
      ESP.restart();
    }
  } else {
    downSince = 0;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  ledStatus(0, 0, 40);  // blue while booting
  // Read the MAC from eFuse directly. WiFi.macAddress() returns all zeroes here
  // because the WiFi stack isn't up yet, and WiFi.mode() alone isn't enough.
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  Serial.printf("\n%s v%s  mac=%02X:%02X:%02X:%02X:%02X:%02X\n", FW_NAME, FW_VERSION,
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  if (storeBeginSD()) Serial.printf("SD: %llu MB (used %llu MB)\n", G.sdSizeMB, G.sdUsedMB);
  else                Serial.println("SD: no card");
  storeLoadConfig();
  Serial.printf("config: %d hosts, %d nodes, %d outputs\n", G.hostCount, G.nodeCount, G.outputCount);

  bool forcePortal = bootHeldAtStart();
  if (forcePortal) { Serial.println("BOOT held -> forcing setup portal"); netClearCreds(); }

  if (!forcePortal && netHasCreds() && netConnectSTA(20000)) {
    Serial.printf("WiFi ok  ip=%s  rssi=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    if (MDNS.begin(MDNS_HOSTNAME)) {
      MDNS.addService("http", "tcp", WEB_PORT);
      Serial.printf("mDNS: http://%s.local/\n", MDNS_HOSTNAME);
    }
    // OTA: this board's native-USB CDC wedges easily, so updates go over WiFi.
    ArduinoOTA.setHostname(MDNS_HOSTNAME);
#ifdef OTA_PASSWORD
    ArduinoOTA.setPassword(OTA_PASSWORD);
#else
    // OTA can flash arbitrary firmware onto a board that drives relays, so an
    // unauthenticated one is a real hole -- say so rather than failing quietly.
    Serial.println("WARNING: OTA is UNAUTHENTICATED - anyone on this LAN can reflash this device.");
    Serial.println("         Create firmware/homehub/secrets.h (see secrets.example.h) to fix.");
#endif
    // Flashing blocks loop(), so feed the watchdog from OTA's own progress
    // callback -- otherwise a slow upload trips WDT_TIMEOUT_MS and reboots
    // the board mid-update.
    ArduinoOTA.onProgress([](unsigned int, unsigned int) { esp_task_wdt_reset(); });
    ArduinoOTA.onStart([]() { ledStatus(90, 55, 0); Serial.println("OTA start"); });
    ArduinoOTA.onEnd([]()   { ledStatus(0, 80, 0);  Serial.println("OTA done");  });
    ArduinoOTA.onError([](ota_error_t e) { ledStatus(120, 0, 0); Serial.printf("OTA error %u\n", e); });
    ArduinoOTA.begin();
    Serial.println("OTA ready");

    featuresInit();
    webBegin();
    storeLog("boot: online " + WiFi.localIP().toString());
    Serial.println("dashboard up");
  } else {
    Serial.printf("starting AP portal: SSID \"%s\" -> http://192.168.4.1/\n", AP_SSID);
    netStartPortal();
  }

  // Watch the loop task. Both remote surfaces (web UI and OTA) are serviced
  // from loop(), so a wedged loop is unrecoverable without physical access --
  // reboot instead. Arduino may have already started the TWDT, hence the
  // reconfigure fallback.
  esp_task_wdt_config_t wdt = {
    .timeout_ms = WDT_TIMEOUT_MS,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  esp_err_t werr = esp_task_wdt_init(&wdt);
  if (werr == ESP_ERR_INVALID_STATE) werr = esp_task_wdt_reconfigure(&wdt);
  if (werr == ESP_OK && esp_task_wdt_add(NULL) == ESP_OK) {
    Serial.printf("watchdog armed (%d ms)\n", WDT_TIMEOUT_MS);
  } else {
    Serial.printf("WARNING: watchdog NOT armed (err %d) - a hung loop will not self-recover\n", werr);
  }
}

void loop() {
  esp_task_wdt_reset();  // must also run on the AP-portal path below
  if (G.apMode) { netHandlePortal(); return; }
  ArduinoOTA.handle();
  webHandle();
  monitorTick();
  nodesTick();
  checkForceApButton();
  delay(2);
}
