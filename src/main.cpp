// anita-adrc firmware composition root.
//
// Task architecture (why MQTT can never hurt control):
//   esp_timer @10 ms   SSR half-wave tick (SsrOutput)
//   control task, prio 5, 100 ms: ADC sampling; every 5th tick (500 ms) the
//                                 ControllerCore runs and commands the duty.
//                                 Watched by the task watchdog.
//   loopTask, prio 1:  WiFi/MQTT state machines, display, button, NVS,
//                      serial CSV + tuning console. May block briefly.
//
// Cross-task traffic: a mutex-guarded Status snapshot (control -> comms) and
// a small pending-command mailbox (comms -> control). The ControllerCore is
// touched exclusively by the control task.
//
// Serial tuning console (docs/tuning-hardware.md): `id duty/off/stop`,
// `mark`, `set b0|wc|wo|pred|kboost|cap|setpoint`, `get`. Identification
// overrides run with the SafetyMonitor fully armed and auto-revert.

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "CommandParser.h"
#include "ControllerCore.h"
#include "Persist.h"
#include "SecretsCompat.h"
#include "SerialConsole.h"
#include "Status.h"
#include "config.h"
#include "hw/DisplayUi.h"
#include "hw/NtcAdc.h"
#include "hw/SsrOutput.h"
#include "hw/UiButton.h"
#include "net/MqttLink.h"
#include "net/WifiManager.h"

using anita::Command;
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
SerialConsole console;

ControllerCore core;  // control task only

// Live-tunable parameter mirror for `get` / MQTT (written by control task).
struct LiveParams {
    float b0 = 0, wc = 0, wo = 0, pred = 0, kBoost = 0, cap = 1.0f, offset = 0;
};

enum class IdMode : uint8_t { None, Duty, Off };

struct Pending {
    float setpointDelta = 0.0f;
    bool hasSetpointAbs = false;
    float setpointAbs = 0.0f;
    bool hasKBoost = false;
    float kBoost = 0.0f;
    bool hasB0 = false;
    float b0 = 0.0f;
    bool hasWc = false;
    float wc = 0.0f;
    bool hasWo = false;
    float wo = 0.0f;
    bool hasPred = false;
    float pred = 0.0f;
    bool hasCap = false;
    float cap = 0.0f;
    bool hasId = false;  // identification request
    IdMode idMode = IdMode::None;
    float idDuty = 0.0f;
    float idSeconds = 0.0f;
};

SemaphoreHandle_t mux;
Status gStatus;
LiveParams gLive;
Pending gPending;
uint32_t gOffsetVersion = 0;  // bumped when the learned offset wants saving
uint32_t gIdEndCount = 0;     // bumped when an identification run finishes

void controlTask(void*) {
    esp_task_wdt_add(nullptr);
    int divider = 0;
    float dutyCap = 1.0f;
    IdMode idMode = IdMode::None;
    float idDuty = 0.0f;
    int idTicksLeft = 0;
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONTROL_TICK_MS));
        esp_task_wdt_reset();

        ntcBoiler.sample();
        ntcGroup.sample();

        if (++divider < CONTROL_DIVIDER) continue;
        divider = 0;
        const float ts = core.config().adrc.ts;

        // Apply queued commands from the comms side.
        xSemaphoreTake(mux, portMAX_DELAY);
        const Pending cmd = gPending;
        gPending = Pending{};
        xSemaphoreGive(mux);
        if (cmd.hasSetpointAbs) core.setSetpoint(cmd.setpointAbs);
        if (cmd.setpointDelta != 0.0f) core.bumpSetpoint(cmd.setpointDelta);
        if (cmd.hasKBoost) core.groupComp().setKBoost(cmd.kBoost);
        if (cmd.hasB0) core.adrc().setB0(cmd.b0);
        if (cmd.hasWc || cmd.hasWo) {
            const auto& p = core.adrc().params();
            core.adrc().setBandwidths(cmd.hasWc ? cmd.wc : p.wc,
                                      cmd.hasWo ? cmd.wo : p.wo);
        }
        if (cmd.hasPred) core.adrc().setPredS(cmd.pred);
        if (cmd.hasCap) dutyCap = cmd.cap;
        if (cmd.hasId) {
            idMode = cmd.idMode;
            idDuty = cmd.idDuty;
            idTicksLeft = static_cast<int>(cmd.idSeconds / ts + 0.5f);
            if (idMode == IdMode::None) idTicksLeft = 0;
        }

        core.setAppliedDuty(ssr.consumeActualDuty());

        CoreInputs in;
        in.boilerC = ntcBoiler.celsius();
        in.groupC = ntcGroup.celsius();
        in.boilerMv = ntcBoiler.millivolts();
        in.groupMv = ntcGroup.millivolts();
        in.readingAgeS = max(ntcBoiler.ageS(), ntcGroup.ageS());
        in.dtS = ts;
        const CoreOutputs out = core.step(in);

        // Identification override: fixed duty with the safety chain armed.
        bool idJustEnded = false;
        float duty = out.duty;
        if (out.fault != Fault::None) {
            if (idMode != IdMode::None) idJustEnded = true;
            idMode = IdMode::None;
            idTicksLeft = 0;
            duty = 0.0f;
            ssr.forceOff();
        } else {
            if (idMode != IdMode::None) {
                duty = (idMode == IdMode::Off) ? 0.0f : idDuty;
                if (--idTicksLeft <= 0) {
                    idMode = IdMode::None;
                    idJustEnded = true;
                }
            }
            duty = min(duty, dutyCap);
            ssr.setDuty(duty);
        }

        const bool offsetDirty = core.groupComp().offsetDirty();
        if (offsetDirty) core.groupComp().clearOffsetDirty();

        xSemaphoreTake(mux, portMAX_DELAY);
        gStatus.boilerC = in.boilerC;
        gStatus.groupC = in.groupC;
        gStatus.setpointC = core.setpoint();
        gStatus.boilerSetC = out.boilerSetC;
        gStatus.duty = duty;  // the duty actually commanded (id/cap included)
        gStatus.z1 = out.z1;
        gStatus.z2 = out.z2;
        gStatus.boostC = out.boostC;
        gStatus.offsetSsC = out.offsetSsC;
        gStatus.state = out.state;
        gStatus.fault = out.fault;
        gStatus.drawActive = out.drawActive;
        gStatus.uptimeMs = millis();
        gLive.b0 = core.adrc().params().b0;
        gLive.wc = core.adrc().params().wc;
        gLive.wo = core.adrc().params().wo;
        gLive.pred = core.adrc().params().predS;
        gLive.kBoost = core.groupComp().params().kBoost;
        gLive.cap = dutyCap;
        gLive.offset = out.offsetSsC;
        if (offsetDirty) ++gOffsetVersion;
        if (idJustEnded) ++gIdEndCount;
        xSemaphoreGive(mux);
    }
}

Status snapshot(LiveParams* live = nullptr, uint32_t* offsetVersion = nullptr,
                uint32_t* idEndCount = nullptr) {
    xSemaphoreTake(mux, portMAX_DELAY);
    const Status st = gStatus;
    if (live) *live = gLive;
    if (offsetVersion) *offsetVersion = gOffsetVersion;
    if (idEndCount) *idEndCount = gIdEndCount;
    xSemaphoreGive(mux);
    return st;
}

void queueSetpointDelta(float d) {
    xSemaphoreTake(mux, portMAX_DELAY);
    gPending.setpointDelta += d;
    xSemaphoreGive(mux);
}

void handleConsoleCommand(const Command& cmd) {
    const uint32_t now = millis();
    xSemaphoreTake(mux, portMAX_DELAY);
    switch (cmd.type) {
        case Command::Type::IdDuty:
            gPending.hasId = true;
            gPending.idMode = IdMode::Duty;
            gPending.idDuty = cmd.value;
            gPending.idSeconds = cmd.seconds;
            break;
        case Command::Type::IdOff:
            gPending.hasId = true;
            gPending.idMode = IdMode::Off;
            gPending.idSeconds = cmd.seconds;
            break;
        case Command::Type::IdStop:
            gPending.hasId = true;
            gPending.idMode = IdMode::None;
            break;
        case Command::Type::Set:
            switch (cmd.param) {
                case Command::Param::B0Gain: gPending.hasB0 = true; gPending.b0 = cmd.value; break;
                case Command::Param::Wc: gPending.hasWc = true; gPending.wc = cmd.value; break;
                case Command::Param::Wo: gPending.hasWo = true; gPending.wo = cmd.value; break;
                case Command::Param::Pred: gPending.hasPred = true; gPending.pred = cmd.value; break;
                case Command::Param::KBoost: gPending.hasKBoost = true; gPending.kBoost = cmd.value; break;
                case Command::Param::Cap: gPending.hasCap = true; gPending.cap = cmd.value; break;
                case Command::Param::Setpoint:
                    gPending.hasSetpointAbs = true;
                    gPending.setpointAbs = cmd.value;
                    break;
                default: break;
            }
            break;
        default:
            break;
    }
    xSemaphoreGive(mux);

    // Echo as #-lines so they land in the capture file, interleaved with CSV.
    switch (cmd.type) {
        case Command::Type::IdDuty:
            Serial.printf("#EVT %lu id_duty d=%.3f t=%.0f\n",
                          static_cast<unsigned long>(now),
                          static_cast<double>(cmd.value),
                          static_cast<double>(cmd.seconds));
            break;
        case Command::Type::IdOff:
            Serial.printf("#EVT %lu id_off t=%.0f\n",
                          static_cast<unsigned long>(now),
                          static_cast<double>(cmd.seconds));
            break;
        case Command::Type::IdStop:
            Serial.printf("#EVT %lu id_stop\n", static_cast<unsigned long>(now));
            break;
        case Command::Type::Mark:
            Serial.printf("#MARK %lu %s\n", static_cast<unsigned long>(now),
                          cmd.text);
            break;
        case Command::Type::Set:
            Serial.printf("#OK %lu set %.4f\n", static_cast<unsigned long>(now),
                          static_cast<double>(cmd.value));
            break;
        case Command::Type::Get: {
            LiveParams live;
            const Status st = snapshot(&live);
            Serial.printf(
                "#PARAMS %lu b0=%.3f wc=%.4f wo=%.4f pred=%.1f kboost=%.2f "
                "cap=%.2f offset=%.2f setpoint=%.1f\n",
                static_cast<unsigned long>(now), static_cast<double>(live.b0),
                static_cast<double>(live.wc), static_cast<double>(live.wo),
                static_cast<double>(live.pred), static_cast<double>(live.kBoost),
                static_cast<double>(live.cap), static_cast<double>(live.offset),
                static_cast<double>(st.setpointC));
            break;
        }
        default:
            break;
    }
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

    console.onCommand = handleConsoleCommand;

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
    static uint32_t savedOffsetVersion = 0, seenIdEnd = 0;
    static float lastSeenSetpoint = -1.0f;

    button.update();
    console.update();
    wifi.update();
    mqtt.update(wifi.connected());
    persist.update();

    const uint32_t now = millis();
    LiveParams live;
    uint32_t offsetVersion = 0, idEndCount = 0;
    const Status st = snapshot(&live, &offsetVersion, &idEndCount);

    if (idEndCount != seenIdEnd) {
        seenIdEnd = idEndCount;
        Serial.printf("#EVT %lu id_end\n", static_cast<unsigned long>(now));
    }

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
        mqtt.publishState(st, live.kBoost);
    }
    if (now - lastCsvMs >= SERIAL_LOG_PERIOD_MS) {
        lastCsvMs = now;
        serialCsv(st);
    }
    delay(10);
}
