// secrets.example.h — copy to secrets.h and edit. secrets.h is gitignored.
//
//   cp firmware/homehub/secrets.example.h firmware/homehub/secrets.h
//
// Without a secrets.h the firmware still builds and OTA still works, but it
// runs unauthenticated: anyone on your LAN can reflash the device. The boot
// log says so loudly.
#pragma once

// Password required to push firmware over the air. Once this is flashed, every
// later OTA upload needs it:
//   arduino-cli upload -p homehub.local --protocol network \
//     --upload-field password=<this> ...
#define OTA_PASSWORD "change-me"

// Login for the dashboard itself. Without WEB_PASSWORD the UI is open to
// anyone on the LAN -- including the Control tab, which drives GPIO outputs.
// The boot log says so loudly.
//
// Digest auth, so the password is not sent in the clear. The device speaks
// plain HTTP (no TLS on an ESP32 in practice), so this protects the credential
// but not the traffic -- treat it as keeping honest devices out, not as
// hardening against someone already capturing packets on your LAN.
#define WEB_USER     "admin"
#define WEB_PASSWORD "change-me-too"
