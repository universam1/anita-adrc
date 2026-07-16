#pragma once

#include <PubSubClient.h>
#include <WiFiClient.h>
#include <functional>

#include "Status.h"

// PubSubClient wrapper. Runs entirely in the low-priority comms task, so a
// blocking connect() can never stall the control loop; reconnects are
// rate-limited and the socket timeout is short. On (re)connect it publishes
// the HA discovery configs (retained) and re-subscribes.
class MqttLink {
public:
    std::function<void(float)> onSetpoint;
    std::function<void(float)> onKBoost;

    void begin();
    void update(bool wifiUp);
    void publishState(const Status& st, float kBoost);
    bool connected() { return mqtt_.connected(); }

private:
    void handleMessage(char* topic, uint8_t* payload, unsigned int len);

    WiFiClient net_;
    PubSubClient mqtt_{net_};
    uint32_t lastAttemptMs_ = 0;
};
