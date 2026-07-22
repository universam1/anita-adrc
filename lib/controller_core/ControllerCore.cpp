#include "ControllerCore.h"

#include "Thermistor.h"

namespace anita {

const char* stateName(State s) {
    switch (s) {
        case State::Boot: return "boot";
        case State::Heatup: return "heat";
        case State::Regulate: return "reg";
        case State::Fault: return "FAULT";
    }
    return "?";
}

namespace {
AdrcParams detectionParams(const CoreConfig& cfg) {
    AdrcParams p = cfg.adrc;
    p.wo = cfg.detWo;
    return p;
}
}  // namespace

ControllerCore::ControllerCore(const CoreConfig& cfg)
    : cfg_(cfg),
      adrc_(cfg.adrc),
      detEso_(detectionParams(cfg)),
      groupComp_(cfg.groupComp),
      drawDetect_(cfg.drawDetect),
      safety_(cfg.safety),
      setpointC_(cfg.setDefaultC) {}

void ControllerCore::setSetpoint(float c) {
    if (c < cfg_.setMinC) c = cfg_.setMinC;
    if (c > cfg_.setMaxC) c = cfg_.setMaxC;
    setpointC_ = c;
}

void ControllerCore::bumpSetpoint(float deltaC) {
    setSetpoint(setpointC_ + deltaC);
}

bool ControllerCore::plausible(const CoreInputs& in) const {
    return in.boilerMv > Thermistor::kRailLowMv &&
           in.boilerMv < Thermistor::kRailHighMv &&
           in.groupMv > Thermistor::kRailLowMv &&
           in.groupMv < Thermistor::kRailHighMv && in.boilerC > -5.0f &&
           in.boilerC < 150.0f;
}

CoreOutputs ControllerCore::step(const CoreInputs& in) {
    CoreOutputs out;

    if (state_ == State::Boot) {
        bootGoodSamples_ = plausible(in) ? bootGoodSamples_ + 1 : 0;
        if (bootGoodSamples_ >= cfg_.bootSamples) {
            adrc_.reset(in.boilerC);
            detEso_.reset(in.boilerC);
            state_ = State::Heatup;
        } else {
            out.state = State::Boot;
            return out;  // duty 0 until the sensors prove themselves
        }
    }

    // Group rise rate over the last 2*Ts (draw signature: flow heats the
    // group within seconds).
    groupHist_[0] = groupHist_[1];
    groupHist_[1] = groupHist_[2];
    groupHist_[2] = in.groupC;
    if (groupHistFill_ < 3) ++groupHistFill_;
    const float groupSlope =
        (groupHistFill_ >= 3 && in.dtS > 0.0f)
            ? (groupHist_[2] - groupHist_[0]) / (2.0f * in.dtS)
            : 0.0f;

    // Group compensation first: it produces this cycle's boiler setpoint.
    const bool drawWasActive = drawDetect_.active();
    const float boilerSet = groupComp_.boilerSetpoint(
        setpointC_, in.groupC, in.boilerC, drawWasActive, in.dtS);
    lastBoilerSetC_ = boilerSet;

    // Safety before actuation.
    SafetyInputs sin;
    sin.boilerMv = in.boilerMv;
    sin.groupMv = in.groupMv;
    sin.boilerC = in.boilerC;
    sin.boilerSetC = boilerSet;
    sin.readingAgeS = in.readingAgeS;
    sin.dtS = in.dtS;

    // Safety must judge the duty that was actually DELIVERED last interval
    // (set via setAppliedDuty) — not the controller's intent. During an
    // identification override or a duty cap the two differ, and NoRise would
    // otherwise false-trip while the heater is deliberately held off.
    const float deliveredDuty = adrc_.appliedDuty();

    // ADRC runs from the moment BOOT completes; far from setpoint it simply
    // saturates at full duty (the ESO keeps converging => bumpless handover).
    float duty = adrc_.update(boilerSet, in.boilerC);

    // The fast detection observer sees the same measurements; only its z2 is
    // consumed (as the DrawDetector's trigger signal).
    detEso_.update(boilerSet, in.boilerC);

    // Draw detection on the fast disturbance estimate + the group rise rate;
    // feed duty forward to full power during the draw, unless the boiler is
    // already above target. Enabled only once regulation has been reached —
    // the warm-up transients would otherwise false-trigger.
    if (in.boilerC >= boilerSet - cfg_.heatupBandC) regulationReached_ = true;
    const bool drawActive = drawDetect_.step(detEso_.z2(), groupSlope, in.dtS,
                                             regulationReached_);
    if (drawActive && in.boilerC < boilerSet + 0.5f) {
        duty = cfg_.adrc.uMax;
        setAppliedDuty(duty);  // keep both ESOs honest about the override
    }

    sin.duty = deliveredDuty;
    const Fault fault = safety_.step(sin);
    if (fault != Fault::None) {
        state_ = State::Fault;
        out.duty = 0.0f;
    } else {
        state_ = (in.boilerC < boilerSet - cfg_.heatupBandC) ? State::Heatup
                                                             : State::Regulate;
        out.duty = duty;
    }

    out.state = state_;
    out.fault = fault;
    out.boilerSetC = boilerSet;
    out.boostC = groupComp_.boostC();
    out.offsetSsC = groupComp_.offsetSs();
    out.z1 = adrc_.z1();
    out.z2 = adrc_.z2();
    out.drawActive = drawActive;
    return out;
}

}  // namespace anita
