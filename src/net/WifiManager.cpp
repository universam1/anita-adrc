#include "WifiManager.h"

namespace {
constexpr uint32_t kRetryMs = 10000;
}

void WifiManager::begin(const char* ssid, const char* pass) {
    ssid_ = ssid;
    pass_ = pass;
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid_, pass_);
    lastAttemptMs_ = millis();
}

void WifiManager::update() {
    if (connected()) return;
    const uint32_t now = millis();
    if (now - lastAttemptMs_ >= kRetryMs) {
        lastAttemptMs_ = now;
        WiFi.disconnect();
        WiFi.begin(ssid_, pass_);
    }
}
