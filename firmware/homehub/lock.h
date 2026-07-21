// lock.h — one recursive mutex guarding the shared config/results in AppState G.
//
// Since v0.4.0 the network polling runs in its own task, so G.hosts[]/G.nodes[]
// are touched by two tasks at once: the poll task writes probe results while a
// web handler may be serialising them, or replacing the whole array from a
// POSTed config. Unguarded, a config replace during a render reads freed
// Strings.
//
// Recursive because the call graph nests -- apiConfigPost holds the lock and
// calls storeSaveConfig(), which takes it again via storeConfigToJson().
//
// Hold it only around memory access, NEVER across network I/O: a node poll can
// block ~4 s, and holding the lock through that would stall the web server
// exactly the way moving polling off loop() was meant to prevent.
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern SemaphoreHandle_t gLock;

void lockInit();

// Scope guard: takes the lock for the enclosing block, releases on any exit.
struct Lock {
  Lock()  { if (gLock) xSemaphoreTakeRecursive(gLock, portMAX_DELAY); }
  ~Lock() { if (gLock) xSemaphoreGiveRecursive(gLock); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;
};
