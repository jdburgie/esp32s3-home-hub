// homehub.ino — Waveshare ESP32-S3-LCD-1.47 repurposed as a headless LAN
// dashboard: node aggregator, presence monitor, and a browser control panel.
// The 1.47" LCD is dead and unused; the web UI is the display.
//
// Boots into an AP config portal when WiFi is unconfigured (or BOOT held).
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "config.h"
#include "state.h"
#include "store.h"
#include "net.h"
#include "web.h"
#include "features.h"

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
  ledStatus(0, 0, 8);  // faint blue while booting
  Serial.printf("\n%s v%s  mac=%s\n", FW_NAME, FW_VERSION, WiFi.macAddress().c_str());

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
    featuresInit();
    webBegin();
    storeLog("boot: online " + WiFi.localIP().toString());
    Serial.println("dashboard up");
  } else {
    Serial.printf("starting AP portal: SSID \"%s\" -> http://192.168.4.1/\n", AP_SSID);
    netStartPortal();
  }
}

void loop() {
  if (G.apMode) { netHandlePortal(); return; }
  webHandle();
  monitorTick();
  nodesTick();
  checkForceApButton();
  delay(2);
}
