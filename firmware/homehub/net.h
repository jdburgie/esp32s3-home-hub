// net.h — WiFi STA connect, credential storage, and the AP setup portal.
#pragma once
#include <Arduino.h>

bool netHasCreds();
bool netConnectSTA(uint32_t timeoutMs);  // true if it joined WiFi
void netClearCreds();                    // wipe stored SSID/pass (forces portal)
void netStartPortal();                   // bring up AP + captive config portal
void netHandlePortal();                  // call in loop() while in AP mode
void ledStatus(uint8_t r, uint8_t g, uint8_t b);  // drive onboard WS2812
