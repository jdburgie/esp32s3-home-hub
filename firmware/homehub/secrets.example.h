// secrets.example.h — copy to secrets.h and edit. secrets.h is gitignored.
//
//   cp firmware/homehub/secrets.example.h firmware/homehub/secrets.h
//
// The dashboard is intentionally open to devices on the trusted local network.
// This file only protects OTA firmware uploads.
#pragma once

// Password required to push firmware over the air. Once this is flashed, every
// later OTA upload needs it:
//   arduino-cli upload -p homehub.local --protocol network \
//     --upload-field password=<this> ...
#define OTA_PASSWORD "change-me"
