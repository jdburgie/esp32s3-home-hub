// store.h — SD card mount + config persistence (SD JSON, mirrored to NVS).
#pragma once
#include <Arduino.h>

bool storeBeginSD();          // mount SD_MMC (4-bit); updates G.sdPresent/size
void storeLoadConfig();       // SD file -> NVS -> built-in defaults
bool storeSaveConfig();       // write current config to SD (if present) + NVS
String storeConfigToJson();   // serialize the app config to a JSON string
bool storeConfigFromJson(const String& json);  // parse into G
void storeLog(const String& line);  // append a line to /homehub/log.txt (if SD)
