// store.h — SD card mount + config persistence (SD JSON, mirrored to NVS).
#pragma once
#include <Arduino.h>

bool storeBeginSD();          // mount SD_MMC (4-bit); updates G.sdPresent/size
void storeLoadConfig();       // SD file -> NVS -> built-in defaults
bool storeSaveConfig();       // write current config to SD (if present) + NVS
String storeConfigToJson();   // serialize the app config to a JSON string
// Parse into G. Outputs on unsafe GPIO pins are dropped; pass droppedOutputs
// to find out how many, so the UI can say so instead of silently losing them.
bool storeConfigFromJson(const String& json, int* droppedOutputs = nullptr);
void storeLog(const String& line);  // append a line to /homehub/log.txt (if SD)
