// state.h — in-memory data model shared across modules.
#pragma once
#include <Arduino.h>
#include "config.h"

struct MonHost {
  String   name;
  String   host;        // IP or hostname
  uint16_t port = 80;   // TCP port used for the presence probe
  bool     online = false;
  uint32_t latencyMs = 0;
  uint32_t lastSeenMs = 0;  // millis() of last successful probe (0 = never)
};

struct Node {
  String name;
  String url;           // full URL to GET, e.g. http://192.168.12.50/status
  bool   online = false;
  int    httpCode = 0;
  String snippet;       // first chunk of the response body (trimmed)
  uint32_t lastPollMs = 0;
};

struct Output {
  String  name;
  uint8_t pin = 0;
  bool    state = false;
  bool    activeLow = false;
};

struct AppState {
  // Presence monitor (feature 3)
  MonHost hosts[MAX_MONITOR_HOSTS];
  int     hostCount = 0;

  // Node aggregator (feature 2)
  Node    nodes[MAX_NODES];
  int     nodeCount = 0;

  // Control panel (feature 4)
  Output  outputs[MAX_OUTPUTS];
  int     outputCount = 0;
  uint8_t ledR = 0, ledG = 0, ledB = 0;  // user-set RGB LED colour

  // Runtime
  bool     sdPresent = false;
  uint64_t sdSizeMB = 0;
  uint64_t sdUsedMB = 0;
  bool     apMode = false;   // true while running the setup portal
};

extern AppState G;
