#include "NtcAdc.h"

#include <algorithm>

NtcAdc::NtcAdc(int pin, const anita::NtcConfig& cfg) : pin_(pin), ntc_(cfg) {}

void NtcAdc::begin() {
    pinMode(pin_, INPUT);
    analogSetPinAttenuation(pin_, ADC_11db);  // full 0..~2.5 V usable band
    // Prime the filter so the first control cycles see a plausible value.
    filteredMv_ = readOversampledMv();
    for (auto& m : median_) m = filteredMv_;
    medianFill_ = 5;
    primed_ = true;
    lastSampleMs_ = millis();
}

float NtcAdc::readOversampledMv() const {
    uint32_t acc = 0;
    for (int i = 0; i < 16; ++i) acc += analogReadMilliVolts(pin_);
    return static_cast<float>(acc) / 16.0f;
}

void NtcAdc::sample() {
    const float mv = readOversampledMv();
    median_[medianIdx_] = mv;
    medianIdx_ = (medianIdx_ + 1) % 5;
    if (medianFill_ < 5) ++medianFill_;

    float sorted[5];
    std::copy(median_, median_ + 5, sorted);
    std::sort(sorted, sorted + 5);
    const float med = sorted[2];

    constexpr float kAlpha = 0.2f;
    filteredMv_ = primed_ ? filteredMv_ + kAlpha * (med - filteredMv_) : med;
    lastSampleMs_ = millis();
}

float NtcAdc::ageS() const {
    return static_cast<float>(millis() - lastSampleMs_) / 1000.0f;
}
