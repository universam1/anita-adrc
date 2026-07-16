#include "GroupComp.h"

#include <cmath>

namespace anita {

namespace {
float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
}  // namespace

GroupComp::GroupComp(const GroupCompParams& p) : p_(p), offsetSs_(p.offsetInitC) {}

void GroupComp::setOffsetSs(float c) {
    offsetSs_ = clampf(c, p_.offsetMinC, p_.offsetMaxC);
}

float GroupComp::boilerSetpoint(float groupSetC, float groupTempC,
                                float boilerTempC, bool drawActive, float dtS) {
    const float deficit = groupSetC - groupTempC;
    boost_ = clampf(p_.kBoost * deficit, p_.minBoostC, p_.maxBoostC);

    // Adapt the steady-state offset only at quiescence: boiler settled on its
    // setpoint, no draw in progress, boost essentially decayed. Rate is ~2-3
    // orders of magnitude slower than the inner loop, so it cannot oscillate
    // against it.
    const bool quiescent = !drawActive &&
                           std::fabs(boilerTempC - lastBoilerSet_) < p_.quietBoilerBandC &&
                           std::fabs(boost_) < p_.quietBoostMaxC;
    if (quiescent && dtS > 0.0f) {
        const float before = offsetSs_;
        offsetSs_ = clampf(offsetSs_ + p_.offsetLearnPerS * deficit * dtS,
                           p_.offsetMinC, p_.offsetMaxC);
        offsetAccum_ += std::fabs(offsetSs_ - before);
        if (offsetAccum_ > 0.25f) {  // persist in coarse steps, not per cycle
            offsetDirty_ = true;
            offsetAccum_ = 0.0f;
        }
    }

    lastBoilerSet_ = clampf(groupSetC + offsetSs_ + boost_,
                            0.0f, p_.maxBoilerSetC);
    return lastBoilerSet_;
}

}  // namespace anita
