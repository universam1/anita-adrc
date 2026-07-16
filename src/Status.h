#pragma once

#include <cstdint>

#include "ControllerCore.h"

// Snapshot published by the control task, consumed by display/MQTT/serial.
// Copy under the shared mutex; never hold references across tasks.
struct Status {
    float boilerC = 0.0f;
    float groupC = 0.0f;
    float setpointC = 0.0f;
    float boilerSetC = 0.0f;
    float duty = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;
    float boostC = 0.0f;
    float offsetSsC = 0.0f;
    anita::State state = anita::State::Boot;
    anita::Fault fault = anita::Fault::None;
    bool drawActive = false;
    uint32_t uptimeMs = 0;
};
