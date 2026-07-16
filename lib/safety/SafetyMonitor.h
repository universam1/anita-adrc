#pragma once

#include <cstdint>

namespace anita {

enum class Fault : uint8_t {
    None = 0,
    BoilerNtcOpen,
    BoilerNtcShort,
    GroupNtcOpen,
    GroupNtcShort,
    OverTemp,
    NoRise,        // heater on but boiler temp not rising: NTC fell off / SSR stuck
    StaleReading,  // ADC pipeline stopped delivering fresh samples
};

const char* faultName(Fault f);

struct SafetyLimits {
    float overTempC = 105.0f;      // boiler shell hard limit
    float railLowMv = 50.0f;       // below: NTC open (node pulled to GND)
    float railHighMv = 3000.0f;    // above: NTC short (node at supply)
    float noRiseDutyAvg = 0.5f;    // implausibility trips when avg duty >= this ...
    float noRiseWindowS = 60.0f;   // ... over this window ...
    float noRiseMinDeltaC = 1.0f;  // ... yet the boiler rose less than this ...
    float noRiseSetpointMarginC = 5.0f;  // ... while still far below setpoint
    float staleMaxS = 2.0f;
};

struct SafetyInputs {
    float boilerMv = 0.0f;
    float groupMv = 0.0f;
    float boilerC = 0.0f;
    float boilerSetC = 0.0f;
    float duty = 0.0f;         // duty applied since the last step
    float readingAgeS = 0.0f;  // age of the newest ADC sample
    float dtS = 0.5f;
};

// Latching fault detector. Pure logic — the caller owns forcing the SSR off.
// A latched fault clears only via reset() (i.e. a deliberate power cycle).
class SafetyMonitor {
public:
    explicit SafetyMonitor(const SafetyLimits& lim = {});

    Fault step(const SafetyInputs& in);
    Fault fault() const { return fault_; }
    void reset();

private:
    SafetyLimits lim_;
    Fault fault_ = Fault::None;

    // no-rise window bookkeeping
    float windowElapsedS_ = 0.0f;
    float windowDutyIntegral_ = 0.0f;  // integral of duty*dt over the window
    float windowStartTempC_ = 0.0f;
    bool windowStarted_ = false;
};

}  // namespace anita
