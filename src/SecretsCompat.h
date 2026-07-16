#pragma once

// Real credentials live in src/secrets.h (gitignored; template in
// src/secrets.h.example). CI and fresh checkouts build with placeholders.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "src/secrets.h missing - building with placeholder credentials"
#define WIFI_SSID "changeme"
#define WIFI_PASS "changeme"
#define MQTT_HOST "192.0.2.1"
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASS ""
#endif
