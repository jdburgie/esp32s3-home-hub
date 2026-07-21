#include "store.h"
#include "lock.h"
#include "state.h"
#include "config.h"
#include <FS.h>
#include <SD_MMC.h>
#include <Preferences.h>
#include <ArduinoJson.h>

static Preferences prefs;

// Monotonic config revision, stamped into both copies on every save.
// storeLoadConfig() loads whichever copy has the HIGHER rev, so a stale SD file
// can no longer silently override a newer NVS mirror -- which is exactly what a
// failed SD write used to leave behind.
//
// Deliberately NOT read back from posted JSON (see storeConfigFromJson): a POST
// carrying an old or absent "rev" would otherwise drag the counter backwards and
// hand the next boot to the stale copy.
static uint32_t s_rev = 0;

bool storeBeginSD() {
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
  // 4-bit mode (mode1bit = false). Retry once in 1-bit as a fallback.
  if (!SD_MMC.begin("/sdcard", false)) {
    if (!SD_MMC.begin("/sdcard", true)) {
      G.sdPresent = false;
      return false;
    }
  }
  if (SD_MMC.cardType() == CARD_NONE) { G.sdPresent = false; return false; }
  G.sdPresent = true;
  G.sdSizeMB  = SD_MMC.cardSize() / (1024ULL * 1024ULL);
  G.sdUsedMB  = SD_MMC.usedBytes() / (1024ULL * 1024ULL);
  SD_MMC.mkdir("/homehub");
  return true;
}

String storeConfigToJson() {
  Lock l;  // G is written by the poll task
  JsonDocument doc;
  JsonArray h = doc["hosts"].to<JsonArray>();
  for (int i = 0; i < G.hostCount; i++) {
    JsonObject o = h.add<JsonObject>();
    o["name"] = G.hosts[i].name;
    o["host"] = G.hosts[i].host;
    o["port"] = G.hosts[i].port;
  }
  JsonArray n = doc["nodes"].to<JsonArray>();
  for (int i = 0; i < G.nodeCount; i++) {
    JsonObject o = n.add<JsonObject>();
    o["name"] = G.nodes[i].name;
    o["url"]  = G.nodes[i].url;
  }
  JsonArray ou = doc["outputs"].to<JsonArray>();
  for (int i = 0; i < G.outputCount; i++) {
    JsonObject o = ou.add<JsonObject>();
    o["name"]      = G.outputs[i].name;
    o["pin"]       = G.outputs[i].pin;
    o["activeLow"] = G.outputs[i].activeLow;
    o["state"]     = G.outputs[i].state;  // persisted so outputs survive a reboot
  }
  JsonObject led = doc["led"].to<JsonObject>();
  led["r"] = G.ledR; led["g"] = G.ledG; led["b"] = G.ledB;
  doc["rev"] = s_rev;  // which copy wins at next boot; ignored on the way in

  String out;
  serializeJson(doc, out);
  return out;
}

bool storeConfigFromJson(const String& json, int* droppedOutputs) {
  Lock l;  // replaces G.hosts/G.nodes wholesale -- must exclude the poll task
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  if (droppedOutputs) *droppedOutputs = 0;

  G.hostCount = 0;
  for (JsonObject o : doc["hosts"].as<JsonArray>()) {
    if (G.hostCount >= MAX_MONITOR_HOSTS) break;
    MonHost& m = G.hosts[G.hostCount++];
    m.name = (const char*)(o["name"] | "");
    m.host = (const char*)(o["host"] | "");
    m.port = o["port"] | 80;
    m.online = false; m.lastSeenMs = 0;
  }
  G.nodeCount = 0;
  for (JsonObject o : doc["nodes"].as<JsonArray>()) {
    if (G.nodeCount >= MAX_NODES) break;
    Node& n = G.nodes[G.nodeCount++];
    n.name = (const char*)(o["name"] | "");
    n.url  = (const char*)(o["url"] | "");
    n.online = false; n.httpCode = 0;
  }
  G.outputCount = 0;
  for (JsonObject o : doc["outputs"].as<JsonArray>()) {
    if (G.outputCount >= MAX_OUTPUTS) break;
    // -1 sentinel, not 0: a missing "pin" must not silently become GPIO0, which
    // featuresInit() would then drive LOW and wedge the board. See config.h.
    int pin = o["pin"] | -1;
    if (!pinIsSafeOutput(pin)) {
      Serial.printf("config: dropped output \"%s\" -- GPIO%d is not a safe output pin\n",
                    (const char*)(o["name"] | "?"), pin);
      if (droppedOutputs) (*droppedOutputs)++;
      continue;
    }
    Output& out = G.outputs[G.outputCount++];
    out.name      = (const char*)(o["name"] | "");
    out.pin       = (uint8_t)pin;
    out.activeLow = o["activeLow"] | false;
    // Restore the last known state; featuresInit() drives the pin to match.
    out.state     = o["state"] | false;
  }
  G.ledR = doc["led"]["r"] | 0;
  G.ledG = doc["led"]["g"] | 0;
  G.ledB = doc["led"]["b"] | 0;
  // "rev" is intentionally not read here -- storeSaveConfig() owns the counter.
  return true;
}

// Read just the revision, without disturbing G. Returns false if unparseable,
// which disqualifies that copy entirely.
static bool peekRev(const String& json, uint32_t& rev) {
  if (json.isEmpty()) return false;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  rev = doc["rev"] | 0;
  return true;
}

void storeLoadConfig() {
  // Read BOTH copies, then load whichever has the higher revision. Preferring SD
  // unconditionally (as this used to) means a stale SD file silently beats a
  // newer NVS mirror -- the failure mode seen on 2026-07-21.
  String sdJson, nvsJson;
  uint32_t sdRev = 0, nvsRev = 0;
  bool sdOk = false, nvsOk = false;

  if (G.sdPresent && SD_MMC.exists(CONFIG_PATH)) {
    File f = SD_MMC.open(CONFIG_PATH, FILE_READ);
    if (f) { sdJson = f.readString(); f.close(); }
    sdOk = peekRev(sdJson, sdRev);
  }
  prefs.begin("homehub", true);
  nvsJson = prefs.getString("cfg", "");
  prefs.end();
  nvsOk = peekRev(nvsJson, nvsRev);

  // Ties go to SD so a hand-edited /homehub/config.json (which won't bump rev)
  // is still honoured when the two copies are otherwise in step.
  bool preferSd = sdOk && (!nvsOk || sdRev >= nvsRev);

  const String& first  = preferSd ? sdJson : nvsJson;
  const String& second = preferSd ? nvsJson : sdJson;
  const char*   firstN = preferSd ? "SD" : "NVS";
  const char*   secondN = preferSd ? "NVS" : "SD";

  if (sdOk || nvsOk) {
    Serial.printf("config: SD rev %lu (%s), NVS rev %lu (%s) -> loading %s\n",
                  (unsigned long)sdRev, sdOk ? "ok" : "bad",
                  (unsigned long)nvsRev, nvsOk ? "ok" : "bad", firstN);
  }

  if (!first.isEmpty() && storeConfigFromJson(first)) {
    s_rev = preferSd ? sdRev : nvsRev;
    return;
  }
  if (!second.isEmpty() && storeConfigFromJson(second)) {
    Serial.printf("config: %s copy failed to load, fell back to %s\n", firstN, secondN);
    s_rev = preferSd ? nvsRev : sdRev;
    return;
  }
  // Built-in defaults: empty lists, LED off. Configure via the web UI.
  Serial.println("config: no usable copy, starting empty");
  s_rev = 0;
}

bool storeSaveConfig(bool* nvsOkOut, bool* sdOkOut) {
  s_rev++;  // stamp a new revision so this save outranks both stored copies
  String json = storeConfigToJson();

  // NVS mirror (always) so config survives without an SD card.
  prefs.begin("homehub", false);
  size_t wrote = prefs.putString("cfg", json);
  prefs.end();
  bool nvsOk = (wrote > 0);

  // SD copy (if present). Verify the byte count -- a short write here is what a
  // silent failure looks like, and it used to be reported as success.
  bool sdOk = false;
  if (G.sdPresent) {
    SD_MMC.mkdir("/homehub");
    File f = SD_MMC.open(CONFIG_PATH, FILE_WRITE);
    if (f) {
      size_t w = f.print(json);
      f.flush();
      f.close();
      sdOk = (w == json.length());
      if (!sdOk) Serial.printf("config: SD write short (%u of %u bytes)\n",
                               (unsigned)w, (unsigned)json.length());
    } else {
      Serial.println("config: SD open for write FAILED");
    }
  }
  if (!nvsOk) Serial.println("config: NVS write FAILED");

  if (nvsOkOut) *nvsOkOut = nvsOk;
  if (sdOkOut)  *sdOkOut  = sdOk;
  // True only when everything that should have been written actually was.
  return nvsOk && (!G.sdPresent || sdOk);
}

void storeLog(const String& line) {
  if (!G.sdPresent) return;
  File f = SD_MMC.open("/homehub/log.txt", FILE_APPEND);
  if (!f) return;
  f.printf("%lu %s\n", (unsigned long)millis(), line.c_str());
  f.close();
}
