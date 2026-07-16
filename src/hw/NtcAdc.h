#pragma once

#include <Arduino.h>

#include "Thermistor.h"

// One NTC channel: calibrated millivolt reads (eFuse curve via
// analogReadMilliVolts), 16x oversampling, median-of-5 spike rejection and an
// IIR smoother. sample() is called at the 100 ms control-task cadence.
class NtcAdc {
public:
    NtcAdc(int pin, const anita::NtcConfig& cfg = {});

    void begin();
    void sample();

    float millivolts() const { return filteredMv_; }
    float celsius() const { return ntc_.celsiusFromMillivolts(filteredMv_); }
    float ageS() const;

private:
    float readOversampledMv() const;

    int pin_;
    anita::Thermistor ntc_;
    float median_[5] = {};
    int medianIdx_ = 0;
    int medianFill_ = 0;
    float filteredMv_ = 0.0f;
    bool primed_ = false;
    uint32_t lastSampleMs_ = 0;
};
