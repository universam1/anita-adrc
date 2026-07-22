#pragma once

#include "Thermistor.h"

// ---------------------------------------------------------------------------
// Hardware — 01Space ESP32-C3-0.42LCD board
// ---------------------------------------------------------------------------
// OLED: SSD1306 72x40 on I2C, SDA=5 SCL=6 (board-fixed).
// ADC: only ADC1 (GPIO0..4) is usable while WiFi is active.
constexpr int PIN_I2C_SDA = 5;
constexpr int PIN_I2C_SCL = 6;
constexpr int PIN_BUTTON = 9;      // boot button, active low, needs pullup
constexpr int PIN_NTC_BOILER = 3;  // ADC1_CH3
constexpr int PIN_NTC_GROUP = 4;   // ADC1_CH4

// Per-channel NTC curves. Defaults are the generic Beta model; after the
// observer calibration run (docs/tuning-hardware.md step 0) paste the
// Steinhart-Hart coefficients printed by tools/calibrate.py here.
inline anita::NtcConfig ntcBoilerConfig() {
    anita::NtcConfig cfg;
    // cfg.useSteinhartHart = true;
    // cfg.shA = ...f; cfg.shB = ...f; cfg.shC = ...f;
    return cfg;
}
inline anita::NtcConfig ntcGroupConfig() {
    anita::NtcConfig cfg;
    // cfg.useSteinhartHart = true;
    // cfg.shA = ...f; cfg.shB = ...f; cfg.shC = ...f;
    return cfg;
}

// SSR drive is ACTIVE-LOW current sinking: the SSR input LED hangs from 3V3
// and this pin sinks it to fire. Floating / Hi-Z / HIGH are all safe OFF
// states (reset, flashing, bootloader, crash), with no external resistor.
constexpr int PIN_SSR = 10;
constexpr int SSR_ON_LEVEL = LOW;
constexpr int SSR_OFF_LEVEL = HIGH;

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
constexpr uint32_t SSR_TICK_US = 10000;       // one half-wave @ 50 Hz mains
constexpr uint32_t CONTROL_TICK_MS = 100;     // ADC sampling cadence
constexpr int CONTROL_DIVIDER = 5;            // ADRC every 5th tick = 500 ms
constexpr uint32_t DISPLAY_PERIOD_MS = 500;
constexpr uint32_t MQTT_STATE_PERIOD_MS = 2000;
constexpr uint32_t SERIAL_LOG_PERIOD_MS = 500;
constexpr uint32_t NVS_QUIET_MS = 5000;       // setpoint write-back delay
constexpr uint32_t WDT_TIMEOUT_S = 10;

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------
constexpr uint32_t BTN_DEBOUNCE_MS = 30;
constexpr uint32_t BTN_LONG_MS = 600;   // hold this long => long press (-0.5C)
constexpr float BTN_SHORT_STEP_C = 0.5f;
constexpr float BTN_LONG_STEP_C = -0.5f;

// ---------------------------------------------------------------------------
// MQTT / Home Assistant
// ---------------------------------------------------------------------------
#define MQTT_BASE "anita"
#define MQTT_TOPIC_STATE MQTT_BASE "/state"
#define MQTT_TOPIC_STATUS MQTT_BASE "/status"  // LWT: online/offline
#define MQTT_TOPIC_SET_SETPOINT MQTT_BASE "/set/setpoint"
#define MQTT_TOPIC_SET_KBOOST MQTT_BASE "/set/kboost"
#define HA_DISCOVERY_PREFIX "homeassistant"
#define DEVICE_ID "anita-adrc"
#define DEVICE_NAME "Lelit Anita ADRC"
