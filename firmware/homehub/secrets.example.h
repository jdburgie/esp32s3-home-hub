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
// HTTP Basic auth. Digest was tried first and does not work here: WebServer
// keeps a single server nonce, and the dashboard's concurrent requests
// invalidate it faster than the browser can answer, producing an endless login
// prompt. See the comment on requireAuth() in web.cpp.
//
// So the password is base64 on every request, and the device has no TLS --
// treat this as keeping other people on the LAN out of your relays, not as
// protection against someone capturing packets. Use a password unique to this
// device.
#define WEB_USER     "admin"
#define WEB_PASSWORD "change-me-too"
