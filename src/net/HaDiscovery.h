#pragma once

#include <PubSubClient.h>

// Publishes retained Home Assistant MQTT discovery configs so the machine
// auto-appears as a device with:
//   sensors:  group temp, boiler temp, duty, state, fault
//   diagnostics: z2 (disturbance estimate), delta-T boost, learned offset
//   number:   setpoint (85..98, step 0.5) and k_boost (live tuning)
//   binary_sensor: draw detected
// All entities share one state topic (MQTT_TOPIC_STATE, JSON) and the
// availability topic (LWT).
namespace HaDiscovery {
void publishAll(PubSubClient& mqtt);
}
