#include "Thermistor.h"

#include <cmath>

namespace anita {

namespace {
constexpr float kKelvinAt0C = 273.15f;
constexpr float kT25K = 25.0f + kKelvinAt0C;
}  // namespace

Thermistor::Thermistor(const NtcConfig& cfg) : cfg_(cfg) {}

float Thermistor::celsiusFromMillivolts(float mv) const {
    // v = Vs * Rs / (Rntc + Rs)  =>  Rntc = Rs * (Vs - v) / v
    if (mv < 1.0f) mv = 1.0f;  // avoid div by zero on a hard rail fault
    const float rNtc = cfg_.rSeries * (cfg_.vSupplyMv - mv) / mv;
    if (rNtc <= 0.0f) return 999.0f;  // shorted: report impossibly hot
    // Beta equation: 1/T = 1/T25 + ln(R/R25)/beta
    const float invT = 1.0f / kT25K + std::log(rNtc / cfg_.r25) / cfg_.beta;
    return 1.0f / invT - kKelvinAt0C + cfg_.offsetC;
}

float Thermistor::millivoltsFromCelsius(float tempC) const {
    const float tK = (tempC - cfg_.offsetC) + kKelvinAt0C;
    const float rNtc = cfg_.r25 * std::exp(cfg_.beta * (1.0f / tK - 1.0f / kT25K));
    return cfg_.vSupplyMv * cfg_.rSeries / (rNtc + cfg_.rSeries);
}

RailFault Thermistor::railFault(float mv) const {
    if (mv < kRailLowMv) return RailFault::Open;
    if (mv > kRailHighMv) return RailFault::Short;
    return RailFault::None;
}

}  // namespace anita
