// features.h — presence monitor (3), node aggregator (2), control panel (4).
#pragma once
#include <Arduino.h>

void featuresInit();        // set up output pins + apply saved LED colour
void monitorTick();         // round-robin TCP presence probes (feature 3)
void nodesTick();           // round-robin HTTP polls of LAN nodes (feature 2)
void controlSetOutput(int idx, bool state);  // drive a GPIO output (feature 4)
void controlApplyLed();     // push G.ledR/G/B to the onboard WS2812
