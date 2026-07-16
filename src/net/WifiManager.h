#pragma once

#include <WiFi.h>

// Non-blocking WiFi keep-alive: kicks off a connection attempt and re-kicks
// on disconnect, rate-limited. Never blocks the calling task.
class WifiManager {
public:
    void begin(const char* ssid, const char* pass);
    void update();
    bool connected() const { return WiFi.status() == WL_CONNECTED; }

private:
    const char* ssid_ = nullptr;
    const char* pass_ = nullptr;
    uint32_t lastAttemptMs_ = 0;
};
