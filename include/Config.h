#pragma once

// 0 selects a factory-MAC-derived numeric identity. Explicit IDs must be unique.
#ifndef HAZARD_NODE_ID
#define HAZARD_NODE_ID 0u
#endif

// Stage-one Serial development works without a connected radio.
#ifndef HAZARD_ENABLE_LORA
#define HAZARD_ENABLE_LORA 1
#endif
#ifndef HAZARD_ENABLE_WIFI_CONSOLE
#define HAZARD_ENABLE_WIFI_CONSOLE 1
#endif
#ifndef HAZARD_WIFI_PASSWORD
#define HAZARD_WIFI_PASSWORD "RadioSense32"
#endif
#define SERIAL_BAUD_RATE 115200
#define START_DELAY 5000
