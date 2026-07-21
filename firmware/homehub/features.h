// features.h — presence monitor (3), node aggregator (2), control panel (4).
#pragma once
#include <Arduino.h>

void featuresInit();        // set up output pins + apply saved LED colour
void monitorTick();         // round-robin TCP presence probes (feature 3)
void nodesTick();           // round-robin HTTP polls of LAN nodes (feature 2)
void controlSetOutput(int idx, bool state);  // drive a GPIO output (feature 4)
void controlApplyLed();     // push G.ledR/G/B to the onboard WS2812
// Single place that talks to the WS2812. This board's LED takes data
// green-first, so R and G are swapped on the way out -- verified on hardware
// 2026-07-20 (sending r=255 lit the LED green). Blue is unaffected.
void rgbLedWrite(uint8_t r, uint8_t g, uint8_t b);
