#include "features.h"
#include "state.h"
#include "config.h"
#include "net.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>

// Note the deliberate r/g swap in the call below -- see rgbLedWrite() in features.h.
void rgbLedWrite(uint8_t r, uint8_t g, uint8_t b) { neopixelWrite(PIN_RGB, g, r, b); }

void controlApplyLed() { rgbLedWrite(G.ledR, G.ledG, G.ledB); }

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

// A node serves whatever it likes -- gzip, images, arbitrary bytes. Raw control
// characters and invalid UTF-8 in the snippet make /api/nodes emit malformed
// JSON, and a browser's strict JSON.parse then throws and blanks the ENTIRE
// Nodes tab, not just the offending entry. (Seen 2026-07-22: a device serving
// gzip put a 1f 8b header into the snippet and emptied the whole tab.)
// So keep printable ASCII only, and call it out when the body isn't text.
static String sanitizeSnippet(const uint8_t* buf, size_t len) {
  String out;
  out.reserve(len + 1);
  size_t dropped = 0;
  for (size_t i = 0; i < len; i++) {
    uint8_t c = buf[i];
    if (c == '\t' || c == '\n' || c == '\r') out += ' ';
    else if (c >= 0x20 && c <= 0x7e)         out += (char)c;
    else                                      dropped++;
  }
  // Mostly unprintable means it isn't text at all; a mangled preview of a gzip
  // stream helps nobody, so say what it is instead.
  if (len && dropped * 4 > len) return "(binary response, " + String((unsigned)len) + "+ bytes)";
  out.trim();
  return out;
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
        // Read only a bounded prefix off the stream. http.getString() pulls the
        // whole body into RAM first, which a large page can exhaust.
        uint8_t buf[NODE_SNIPPET_MAX];
        size_t got = 0;
        WiFiClient* s = http.getStreamPtr();
        uint32_t t0 = millis();
        while (s && got < sizeof(buf) && millis() - t0 < 1000) {
          int avail = s->available();
          if (avail <= 0) {
            if (!http.connected()) break;
            delay(5);
            continue;
          }
          int r = s->read(buf + got, sizeof(buf) - got);
          if (r <= 0) break;
          got += (size_t)r;
        }
        n.snippet = sanitizeSnippet(buf, got);
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
