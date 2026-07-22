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
    const float lnR = std::log(rNtc);
    float invT;
    if (cfg_.useSteinhartHart) {
        invT = cfg_.shA + cfg_.shB * lnR + cfg_.shC * lnR * lnR * lnR;
    } else {
        // Beta equation: 1/T = 1/T25 + ln(R/R25)/beta
        invT = 1.0f / kT25K + (lnR - std::log(cfg_.r25)) / cfg_.beta;
    }
    return 1.0f / invT - kKelvinAt0C + cfg_.offsetC;
}

float Thermistor::millivoltsFromCelsius(float tempC) const {
    const float tK = (tempC - cfg_.offsetC) + kKelvinAt0C;
    float rNtc;
    if (cfg_.useSteinhartHart) {
        // Invert 1/T = A + B*lnR + C*ln^3 R by bisection on lnR.
        const float target = 1.0f / tK;
        float lo = std::log(100.0f), hi = std::log(10e6f);  // 100 Ohm..10 MOhm
        for (int i = 0; i < 60; ++i) {
            const float mid = 0.5f * (lo + hi);
            const float v = cfg_.shA + cfg_.shB * mid + cfg_.shC * mid * mid * mid;
            // invT increases with lnR for an NTC (B, C > 0)
            if (v < target) lo = mid; else hi = mid;
        }
        rNtc = std::exp(0.5f * (lo + hi));
    } else {
        rNtc = cfg_.r25 * std::exp(cfg_.beta * (1.0f / tK - 1.0f / kT25K));
    }
    return cfg_.vSupplyMv * cfg_.rSeries / (rNtc + cfg_.rSeries);
}

RailFault Thermistor::railFault(float mv) const {
    if (mv < kRailLowMv) return RailFault::Open;
    if (mv > kRailHighMv) return RailFault::Short;
    return RailFault::None;
}

}  // namespace anita
