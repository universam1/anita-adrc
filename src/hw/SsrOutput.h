#pragma once

#include <Arduino.h>
#include <esp_timer.h>

#include "SsrModulator.h"

// Drives the SSR pin from a free-running 10 ms esp_timer (one mains half-wave
// at 50 Hz). The zero-cross SSR aligns actual conduction to zero crossings;
// this tick only quantizes intent. Drive is ACTIVE-LOW current-sinking, so
// HIGH and floating are both safe OFF states — the pin is parked HIGH before
// pinMode() so the SSR cannot fire during boot.
class SsrOutput {
public:
    void begin();
    void setDuty(float d);       // thread-safe; from the control task
    float consumeActualDuty();   // delivered duty since last call, for the ESO
    void forceOff();             // fault path: duty 0 and pin off immediately

private:
    static void onTick(void* self);

    anita::SsrModulator mod_;
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    esp_timer_handle_t timer_ = nullptr;
};
