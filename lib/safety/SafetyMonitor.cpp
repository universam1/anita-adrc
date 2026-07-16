#include "SafetyMonitor.h"

namespace anita {

const char* faultName(Fault f) {
    switch (f) {
        case Fault::None: return "none";
        case Fault::BoilerNtcOpen: return "boiler NTC open";
        case Fault::BoilerNtcShort: return "boiler NTC short";
        case Fault::GroupNtcOpen: return "group NTC open";
        case Fault::GroupNtcShort: return "group NTC short";
        case Fault::OverTemp: return "overtemp";
        case Fault::NoRise: return "no temp rise";
        case Fault::StaleReading: return "stale reading";
    }
    return "?";
}

SafetyMonitor::SafetyMonitor(const SafetyLimits& lim) : lim_(lim) {}

void SafetyMonitor::reset() {
    fault_ = Fault::None;
    windowElapsedS_ = 0.0f;
    windowDutyIntegral_ = 0.0f;
    windowStarted_ = false;
}

Fault SafetyMonitor::step(const SafetyInputs& in) {
    if (fault_ != Fault::None) return fault_;  // latched

    if (in.boilerMv < lim_.railLowMv) fault_ = Fault::BoilerNtcOpen;
    else if (in.boilerMv > lim_.railHighMv) fault_ = Fault::BoilerNtcShort;
    else if (in.groupMv < lim_.railLowMv) fault_ = Fault::GroupNtcOpen;
    else if (in.groupMv > lim_.railHighMv) fault_ = Fault::GroupNtcShort;
    else if (in.boilerC > lim_.overTempC) fault_ = Fault::OverTemp;
    else if (in.readingAgeS > lim_.staleMaxS) fault_ = Fault::StaleReading;
    if (fault_ != Fault::None) return fault_;

    // No-rise implausibility over a rolling window.
    if (!windowStarted_) {
        windowStarted_ = true;
        windowStartTempC_ = in.boilerC;
    }
    windowElapsedS_ += in.dtS;
    windowDutyIntegral_ += in.duty * in.dtS;
    if (windowElapsedS_ >= lim_.noRiseWindowS) {
        const float avgDuty = windowDutyIntegral_ / windowElapsedS_;
        const float deltaC = in.boilerC - windowStartTempC_;
        const bool farBelowSetpoint =
            in.boilerC < in.boilerSetC - lim_.noRiseSetpointMarginC;
        if (avgDuty >= lim_.noRiseDutyAvg && deltaC < lim_.noRiseMinDeltaC &&
            farBelowSetpoint) {
            fault_ = Fault::NoRise;
        }
        windowElapsedS_ = 0.0f;
        windowDutyIntegral_ = 0.0f;
        windowStartTempC_ = in.boilerC;
    }
    return fault_;
}

}  // namespace anita
