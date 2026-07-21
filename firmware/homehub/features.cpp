#include "features.h"
#include "state.h"
#include "config.h"
#include "net.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>

void controlApplyLed() { neopixelWrite(PIN_RGB, G.ledR, G.ledG, G.ledB); }

void featuresInit() {
  for (int i = 0; i < G.outputCount; i++) {
    Output& o = G.outputs[i];
    pinMode(o.pin, OUTPUT);
    bool level = o.activeLow ? !o.state : o.state;
    digitalWrite(o.pin, level ? HIGH : LOW);
  }
  controlApplyLed();
}

void controlSetOutput(int idx, bool state) {
  if (idx < 0 || idx >= G.outputCount) return;
  Output& o = G.outputs[idx];
  o.state = state;
  bool level = o.activeLow ? !state : state;
  digitalWrite(o.pin, level ? HIGH : LOW);
}

// ---- Presence monitor (feature 3): one host per slice, round-robin --------
void monitorTick() {
  static uint32_t last = 0;
  static int idx = 0;
  if (G.hostCount == 0 || WiFi.status() != WL_CONNECTED) return;
  uint32_t slice = MONITOR_INTERVAL_MS / (uint32_t)G.hostCount;
  if (slice < 500) slice = 500;
  if (millis() - last < slice) return;
  last = millis();

  if (idx >= G.hostCount) idx = 0;
  MonHost& h = G.hosts[idx];
  WiFiClient c;
  uint32_t t0 = millis();
  bool ok = c.connect(h.host.c_str(), h.port, PROBE_TIMEOUT_MS);
  uint32_t dt = millis() - t0;
  c.stop();
  h.online = ok;
  if (ok) { h.latencyMs = dt; h.lastSeenMs = millis(); }
  idx = (idx + 1) % G.hostCount;
}

// ---- Node aggregator (feature 2): one node per slice, round-robin ---------
void nodesTick() {
  static uint32_t last = 0;
  static int idx = 0;
  if (G.nodeCount == 0 || WiFi.status() != WL_CONNECTED) return;
  uint32_t slice = NODE_INTERVAL_MS / (uint32_t)G.nodeCount;
  if (slice < 750) slice = 750;
  if (millis() - last < slice) return;
  last = millis();

  if (idx >= G.nodeCount) idx = 0;
  Node& n = G.nodes[idx];
  if (n.url.length()) {
    HTTPClient http;
    http.setConnectTimeout(1500);
    http.setTimeout(2500);
    if (http.begin(n.url)) {
      int code = http.GET();
      n.httpCode = code;
      n.online = (code > 0 && code < 400);
      if (code > 0) {
        String body = http.getString();
        body.trim();
        n.snippet = body.substring(0, 160);
      } else {
        n.snippet = HTTPClient::errorToString(code);
      }
      http.end();
    } else {
      n.online = false; n.httpCode = 0; n.snippet = "bad url";
    }
    n.lastPollMs = millis();
  }
  idx = (idx + 1) % G.nodeCount;
}
