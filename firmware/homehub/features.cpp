#include "features.h"
#include "state.h"
#include "config.h"
#include "net.h"
#include "lock.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <esp_task_wdt.h>

SemaphoreHandle_t gLock = nullptr;
void lockInit() { if (!gLock) gLock = xSemaphoreCreateRecursiveMutex(); }

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
// Same lock discipline as nodesTick(): snapshot, probe unlocked, publish.
// The probe is the call that once stalled the whole device -- PROBE_TIMEOUT_MS
// bounds the TCP connect but NOT the DNS lookup ahead of it, so a bad host
// entry can block far longer than 800 ms.
void monitorTick() {
  static uint32_t last = 0;
  static int idx = 0;
  if (WiFi.status() != WL_CONNECTED) return;

  String   host;
  uint16_t port;
  int      myIdx;
  {
    Lock l;
    if (G.hostCount == 0) return;
    uint32_t slice = MONITOR_INTERVAL_MS / (uint32_t)G.hostCount;
    if (slice < 500) slice = 500;
    if (millis() - last < slice) return;
    last = millis();
    if (idx >= G.hostCount) idx = 0;
    myIdx = idx;
    host  = G.hosts[myIdx].host;
    port  = G.hosts[myIdx].port;
    idx   = (idx + 1) % G.hostCount;
  }
  if (host.isEmpty()) return;

  // ---- unlocked: the slow part ----
  WiFiClient c;
  uint32_t t0 = millis();
  bool ok = c.connect(host.c_str(), port, PROBE_TIMEOUT_MS);
  uint32_t dt = millis() - t0;
  c.stop();

  {
    Lock l;
    if (myIdx < G.hostCount && G.hosts[myIdx].host == host) {
      MonHost& h = G.hosts[myIdx];
      h.online = ok;
      if (ok) { h.latencyMs = dt; h.lastSeenMs = millis(); }
    }
  }
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
  if (WiFi.status() != WL_CONNECTED) return;

  // Take a snapshot of the target under the lock, then release it before any
  // network I/O -- a poll can block seconds and must not stall web handlers.
  String url;
  int    myIdx;
  {
    Lock l;
    if (G.nodeCount == 0) return;
    uint32_t slice = NODE_INTERVAL_MS / (uint32_t)G.nodeCount;
    if (slice < 750) slice = 750;
    if (millis() - last < slice) return;
    last = millis();
    if (idx >= G.nodeCount) idx = 0;
    myIdx = idx;
    url   = G.nodes[myIdx].url;
    idx   = (idx + 1) % G.nodeCount;
  }
  if (url.isEmpty()) return;

  // ---- unlocked: the slow part ----
  bool   online = false;
  int    code = 0;
  String snippet;
  HTTPClient http;
  http.setConnectTimeout(1500);
  http.setTimeout(2500);
  if (http.begin(url)) {
    code   = http.GET();
    online = (code > 0 && code < 400);
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
      snippet = sanitizeSnippet(buf, got);
    } else {
      snippet = HTTPClient::errorToString(code);
    }
    http.end();
  } else {
    snippet = "bad url";
  }

  // ---- locked again: publish. The config may have been replaced while we were
  // off the lock, so only write back if this slot is still the same node.
  {
    Lock l;
    if (myIdx < G.nodeCount && G.nodes[myIdx].url == url) {
      Node& n = G.nodes[myIdx];
      n.online = online;
      n.httpCode = code;
      n.snippet = snippet;
      n.lastPollMs = millis();
    }
  }
}

// ---- Background poll task --------------------------------------------------
// monitorTick() and nodesTick() both block on the network -- a node that
// black-holes packets costs ~4 s, and a DNS lookup for a bad host entry is
// unbounded. Running them from loop() meant the web UI and OTA (both serviced
// by loop()) went dead for the duration; on 2026-07-21 that became a permanent
// hang recoverable only by pulling power. They now run here instead, so loop()
// stays responsive no matter how badly a polled host misbehaves.
static void pollTask(void*) {
  // Watched like the loop task: if a poll blocks forever the device still
  // reboots rather than silently going stale. Generous timeout, since a slow
  // poll is normal.
  esp_task_wdt_add(NULL);
  for (;;) {
    esp_task_wdt_reset();
    monitorTick();
    nodesTick();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void pollTaskStart() {
  // Core 0: Arduino's loop() runs on core 1 by default, so polling gets its own
  // core rather than competing with the web server. 8 KB covers HTTPClient +
  // WiFiClient + the String work in sanitizeSnippet().
  xTaskCreatePinnedToCore(pollTask, "netpoll", 8192, nullptr, 1, nullptr, 0);
}
