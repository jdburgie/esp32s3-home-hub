#include "store.h"
#include "state.h"
#include "config.h"
#include <FS.h>
#include <SD_MMC.h>
#include <Preferences.h>
#include <ArduinoJson.h>

static Preferences prefs;

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
  }
  JsonObject led = doc["led"].to<JsonObject>();
  led["r"] = G.ledR; led["g"] = G.ledG; led["b"] = G.ledB;

  String out;
  serializeJson(doc, out);
  return out;
}

bool storeConfigFromJson(const String& json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;

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
    Output& out = G.outputs[G.outputCount++];
    out.name      = (const char*)(o["name"] | "");
    out.pin       = o["pin"] | 0;
    out.activeLow = o["activeLow"] | false;
    out.state     = false;
  }
  G.ledR = doc["led"]["r"] | 0;
  G.ledG = doc["led"]["g"] | 0;
  G.ledB = doc["led"]["b"] | 0;
  return true;
}

void storeLoadConfig() {
  // 1) SD file
  if (G.sdPresent && SD_MMC.exists(CONFIG_PATH)) {
    File f = SD_MMC.open(CONFIG_PATH, FILE_READ);
    if (f) {
      String json = f.readString();
      f.close();
      if (storeConfigFromJson(json)) return;
    }
  }
  // 2) NVS mirror
  prefs.begin("homehub", true);
  String json = prefs.getString("cfg", "");
  prefs.end();
  if (json.length() && storeConfigFromJson(json)) return;

  // 3) Built-in defaults: empty lists, LED off. Configure via the web UI.
}

bool storeSaveConfig() {
  String json = storeConfigToJson();
  // NVS mirror (always) so config survives without an SD card.
  prefs.begin("homehub", false);
  prefs.putString("cfg", json);
  prefs.end();
  // SD copy (if present)
  if (G.sdPresent) {
    SD_MMC.mkdir("/homehub");
    File f = SD_MMC.open(CONFIG_PATH, FILE_WRITE);
    if (f) { f.print(json); f.close(); return true; }
    return false;
  }
  return true;
}

void storeLog(const String& line) {
  if (!G.sdPresent) return;
  File f = SD_MMC.open("/homehub/log.txt", FILE_APPEND);
  if (!f) return;
  f.printf("%lu %s\n", (unsigned long)millis(), line.c_str());
  f.close();
}
