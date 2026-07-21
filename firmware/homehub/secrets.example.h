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
