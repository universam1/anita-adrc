#include "MqttLink.h"

#include <ArduinoJson.h>
#include <cstdlib>
#include <cstring>

#include "HaDiscovery.h"
#include "config.h"

#include "SecretsCompat.h"

namespace {
constexpr uint32_t kRetryMs = 5000;
}

void MqttLink::begin() {
    mqtt_.setServer(MQTT_HOST, MQTT_PORT);
    mqtt_.setSocketTimeout(2);
    mqtt_.setBufferSize(1024);  // HA discovery payloads exceed the 256 default
    mqtt_.setCallback([this](char* t, uint8_t* p, unsigned int l) {
        handleMessage(t, p, l);
    });
}

void MqttLink::update(bool wifiUp) {
    if (!wifiUp) return;
    if (mqtt_.connected()) {
        mqtt_.loop();
        return;
    }
    const uint32_t now = millis();
    if (now - lastAttemptMs_ < kRetryMs) return;
    lastAttemptMs_ = now;

    const char* user = MQTT_USER;
    const bool ok =
        std::strlen(user) > 0
            ? mqtt_.connect(DEVICE_ID, MQTT_USER, MQTT_PASS, MQTT_TOPIC_STATUS,
                            0, true, "offline")
            : mqtt_.connect(DEVICE_ID, MQTT_TOPIC_STATUS, 0, true, "offline");
    if (ok) {
        mqtt_.publish(MQTT_TOPIC_STATUS, "online", true);
        mqtt_.subscribe(MQTT_TOPIC_SET_SETPOINT);
        mqtt_.subscribe(MQTT_TOPIC_SET_KBOOST);
        HaDiscovery::publishAll(mqtt_);
    }
}

void MqttLink::handleMessage(char* topic, uint8_t* payload, unsigned int len) {
    char buf[32];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    std::memcpy(buf, payload, len);
    buf[len] = '\0';
    const float v = std::strtof(buf, nullptr);

    if (std::strcmp(topic, MQTT_TOPIC_SET_SETPOINT) == 0) {
        if (onSetpoint) onSetpoint(v);
    } else if (std::strcmp(topic, MQTT_TOPIC_SET_KBOOST) == 0) {
        if (onKBoost) onKBoost(v);
    }
}

void MqttLink::publishState(const Status& st, float kBoost) {
    if (!mqtt_.connected()) return;
    JsonDocument doc;
    doc["boiler"] = serialized(String(st.boilerC, 2));
    doc["group"] = serialized(String(st.groupC, 2));
    doc["set"] = serialized(String(st.setpointC, 1));
    doc["boiler_set"] = serialized(String(st.boilerSetC, 2));
    doc["duty"] = serialized(String(st.duty, 3));
    doc["z2"] = serialized(String(st.z2, 4));
    doc["boost"] = serialized(String(st.boostC, 2));
    doc["offset"] = serialized(String(st.offsetSsC, 2));
    doc["kboost"] = serialized(String(kBoost, 2));
    doc["state"] = anita::stateName(st.state);
    doc["fault"] = anita::faultName(st.fault);
    doc["draw"] = st.drawActive;
    doc["uptime_s"] = st.uptimeMs / 1000;

    char out[512];
    serializeJson(doc, out, sizeof(out));
    mqtt_.publish(MQTT_TOPIC_STATE, out);
}
