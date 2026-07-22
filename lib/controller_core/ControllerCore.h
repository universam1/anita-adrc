#pragma once

#include <cstdint>

#include "Adrc.h"
#include "DrawDetector.h"
#include "GroupComp.h"
#include "SafetyMonitor.h"

namespace anita {

enum class State : uint8_t { Boot, Heatup, Regulate, Fault };

const char* stateName(State s);

struct CoreConfig {
    AdrcParams adrc{};
    GroupCompParams groupComp{};
    DrawDetectParams drawDetect{};
    SafetyLimits safety{};

    float setMinC = 85.0f;
    float setMaxC = 98.0f;
    float setStepC = 0.5f;
    float setDefaultC = 93.0f;

    // Boiler more than this below its setpoint => HEATUP (display state; the
    // saturated ADRC provides the full duty either way).
    float heatupBandC = 3.0f;

    // Consecutive plausible samples required to leave BOOT.
    int bootSamples = 3;

    // Bandwidth of the detection-only fast ESO. The control ESO stays slow
    // (its z2 feeds the control law); this one only feeds the DrawDetector's
    // threshold, so its extra noise costs nothing and buys seconds.
    float detWo = 1.5f;
};

struct CoreInputs {
    float boilerC = 0.0f;
    float groupC = 0.0f;
    float boilerMv = 0.0f;
    float groupMv = 0.0f;
    float readingAgeS = 0.0f;
    float dtS = 0.5f;
};

struct CoreOutputs {
    float duty = 0.0f;
    State state = State::Boot;
    Fault fault = Fault::None;
    float boilerSetC = 0.0f;
    float boostC = 0.0f;
    float offsetSsC = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;
    bool drawActive = false;
};

// The complete control behavior, hardware-free: state machine, delta-T group
// compensation, ADRC, draw feedforward, safety. One instance is stepped every
// control period (AdrcParams::ts) by main.cpp on the device and by the
// simulator/tests on the host.
class ControllerCore {
public:
    explicit ControllerCore(const CoreConfig& cfg = {});

    CoreOutputs step(const CoreInputs& in);

    void setSetpoint(float c);
    void bumpSetpoint(float deltaC);  // button: +0.5 / -0.5, clamped
    float setpoint() const { return setpointC_; }

    // Duty actually delivered by the modulator since the last step (feeds
    // both the control ESO and the fast detection ESO).
    void setAppliedDuty(float d) {
        adrc_.setAppliedDuty(d);
        detEso_.setAppliedDuty(d);
    }

    Adrc& adrc() { return adrc_; }
    GroupComp& groupComp() { return groupComp_; }
    SafetyMonitor& safety() { return safety_; }
    const CoreConfig& config() const { return cfg_; }

private:
    bool plausible(const CoreInputs& in) const;

    CoreConfig cfg_;
    Adrc adrc_;
    Adrc detEso_;  // fast detection-only observer (duty output discarded)
    GroupComp groupComp_;
    DrawDetector drawDetect_;
    SafetyMonitor safety_;

    State state_ = State::Boot;
    float setpointC_;
    int bootGoodSamples_ = 0;
    float lastBoilerSetC_ = 0.0f;
    bool regulationReached_ = false;
    float groupHist_[3] = {0.0f, 0.0f, 0.0f};  // for the group rise rate
    int groupHistFill_ = 0;
};

}  // namespace anita
