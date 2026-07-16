// anita-adrc firmware composition root.
//
// Task architecture (why MQTT can never hurt control):
//   esp_timer @10 ms   SSR half-wave tick (SsrOutput)
//   control task, prio 5, 100 ms: ADC sampling; every 5th tick (500 ms) the
//                                 ControllerCore runs and commands the duty.
//                                 Watched by the task watchdog.
//   loopTask, prio 1:  WiFi/MQTT state machines, display, button, NVS,
//                      serial CSV. Allowed to block briefly; control isn't.
//
// Cross-task traffic: a mutex-guarded Status snapshot (control -> comms) and
// a small pending-command mailbox (comms -> control). The ControllerCore is
// touched exclusively by the control task.

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "ControllerCore.h"
#include "Persist.h"
#include "SecretsCompat.h"
#include "Status.h"
#include "config.h"
#include "hw/DisplayUi.h"
#include "hw/NtcAdc.h"
#include "hw/SsrOutput.h"
#include "hw/UiButton.h"
#include "net/MqttLink.h"
#include "net/WifiManager.h"

using anita::ControllerCore;
using anita::CoreInputs;
using anita::CoreOutputs;
using anita::Fault;

namespace {

NtcAdc ntcBoiler(PIN_NTC_BOILER);
NtcAdc ntcGroup(PIN_NTC_GROUP);
SsrOutput ssr;
UiButton button;
DisplayUi display;
WifiManager wifi;
MqttLink mqtt;
Persist persist;

ControllerCore core;  // control task only

SemaphoreHandle_t mux;
Status gStatus;
uint32_t gOffsetVersion = 0;  // bumped when the learned offset wants saving
float gKBoost = 0.0f;

struct Pending {
    float setpointDelta = 0.0f;
    bool hasSetpointAbs = false;
    float setpointAbs = 0.0f;
    bool hasKBoost = false;
    float kBoost = 0.0f;
} gPending;

void controlTask(void*) {
    esp_task_wdt_add(nullptr);
    int divider = 0;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONTROL_TICK_MS));
        esp_task_wdt_reset();

        ntcBoiler.sample();
        ntcGroup.sample();

        if (++divider < CONTROL_DIVIDER) continue;
        divider = 0;

        // Apply queued commands from the comms side.
        xSemaphoreTake(mux, portMAX_DELAY);
        const Pending cmd = gPending;
        gPending = Pending{};
        xSemaphoreGive(mux);
        if (cmd.hasSetpointAbs) core.setSetpoint(cmd.setpointAbs);
        if (cmd.setpointDelta != 0.0f) core.bumpSetpoint(cmd.setpointDelta);
        if (cmd.hasKBoost) core.groupComp().setKBoost(cmd.kBoost);

        core.setAppliedDuty(ssr.consumeActualDuty());

        CoreInputs in;
        in.boilerC = ntcBoiler.celsius();
        in.groupC = ntcGroup.celsius();
        in.boilerMv = ntcBoiler.millivolts();
        in.groupMv = ntcGroup.millivolts();
        in.readingAgeS = max(ntcBoiler.ageS(), ntcGroup.ageS());
        in.dtS = core.config().adrc.ts;
        const CoreOutputs out = core.step(in);

        if (out.fault != Fault::None) {
            ssr.forceOff();
        } else {
            ssr.setDuty(out.duty);
        }

        const bool offsetDirty = core.groupComp().offsetDirty();
        if (offsetDirty) core.groupComp().clearOffsetDirty();

        xSemaphoreTake(mux, portMAX_DELAY);
        gStatus.boilerC = in.boilerC;
        gStatus.groupC = in.groupC;
        gStatus.setpointC = core.setpoint();
        gStatus.boilerSetC = out.boilerSetC;
        gStatus.duty = out.duty;
        gStatus.z1 = out.z1;
        gStatus.z2 = out.z2;
        gStatus.boostC = out.boostC;
        gStatus.offsetSsC = out.offsetSsC;
        gStatus.state = out.state;
        gStatus.fault = out.fault;
        gStatus.drawActive = out.drawActive;
        gStatus.uptimeMs = millis();
        gKBoost = core.groupComp().params().kBoost;
        if (offsetDirty) ++gOffsetVersion;
        xSemaphoreGive(mux);
    }
}

Status snapshot(uint32_t* offsetVersion = nullptr, float* kBoost = nullptr) {
    xSemaphoreTake(mux, portMAX_DELAY);
    const Status st = gStatus;
    if (offsetVersion) *offsetVersion = gOffsetVersion;
    if (kBoost) *kBoost = gKBoost;
    xSemaphoreGive(mux);
    return st;
}

void queueSetpointDelta(float d) {
    xSemaphoreTake(mux, portMAX_DELAY);
    gPending.setpointDelta += d;
    xSemaphoreGive(mux);
}

void serialCsv(const Status& st) {
    static bool headerDone = false;
    if (!headerDone) {
        headerDone = true;
        Serial.println(
            "millis,boiler,group,set,boiler_set,duty,z1,z2,boost,offset,"
            "state,draw");
    }
    Serial.printf("%lu,%.2f,%.2f,%.1f,%.2f,%.3f,%.2f,%.4f,%.2f,%.2f,%s,%d\n",
                  static_cast<unsigned long>(st.uptimeMs), st.boilerC,
                  st.groupC, st.setpointC, st.boilerSetC, st.duty, st.z1,
                  st.z2, st.boostC, st.offsetSsC, anita::stateName(st.state),
                  st.drawActive ? 1 : 0);
}

}  // namespace

void setup() {
    ssr.begin();  // park the SSR safely OFF before anything else
    Serial.begin(115200);

    mux = xSemaphoreCreateMutex();

    persist.begin();
    core.setSetpoint(persist.loadSetpoint(core.config().setDefaultC));
    core.groupComp().setOffsetSs(
        persist.loadOffset(core.config().groupComp.offsetInitC));

    ntcBoiler.begin();
    ntcGroup.begin();
    display.begin();

    button.begin(PIN_BUTTON);
    button.onShort = [] { queueSetpointDelta(BTN_SHORT_STEP_C); };
    button.onLong = [] { queueSetpointDelta(BTN_LONG_STEP_C); };

    wifi.begin(WIFI_SSID, WIFI_PASS);
    mqtt.begin();
    mqtt.onSetpoint = [](float v) {
        xSemaphoreTake(mux, portMAX_DELAY);
        gPending.hasSetpointAbs = true;
        gPending.setpointAbs = v;
        xSemaphoreGive(mux);
    };
    mqtt.onKBoost = [](float v) {
        xSemaphoreTake(mux, portMAX_DELAY);
        gPending.hasKBoost = true;
        gPending.kBoost = v;
        xSemaphoreGive(mux);
    };

    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    xTaskCreate(controlTask, "control", 4096, nullptr, 5, nullptr);
}

void loop() {
    static uint32_t lastDisplayMs = 0, lastMqttMs = 0, lastCsvMs = 0;
    static uint32_t savedOffsetVersion = 0;
    static float lastSeenSetpoint = -1.0f;

    button.update();
    wifi.update();
    mqtt.update(wifi.connected());
    persist.update();

    const uint32_t now = millis();
    uint32_t offsetVersion = 0;
    float kBoost = 0.0f;
    const Status st = snapshot(&offsetVersion, &kBoost);

    // Setpoint changed (button or MQTT): schedule the debounced NVS save.
    if (st.setpointC != lastSeenSetpoint && st.state != anita::State::Boot) {
        if (lastSeenSetpoint >= 0.0f) persist.noteSetpointChanged(st.setpointC);
        lastSeenSetpoint = st.setpointC;
    }
    if (offsetVersion != savedOffsetVersion) {
        savedOffsetVersion = offsetVersion;
        persist.saveOffset(st.offsetSsC);
    }

    if (now - lastDisplayMs >= DISPLAY_PERIOD_MS) {
        lastDisplayMs = now;
        display.render(st);
    }
    if (now - lastMqttMs >= MQTT_STATE_PERIOD_MS) {
        lastMqttMs = now;
        mqtt.publishState(st, kBoost);
    }
    if (now - lastCsvMs >= SERIAL_LOG_PERIOD_MS) {
        lastCsvMs = now;
        serialCsv(st);
    }
    delay(10);
}
