#include "Adrc.h"

namespace anita {

Adrc::Adrc(const AdrcParams& p) : p_(p) {}

void Adrc::reset(float y0) {
    z1_ = y0;
    z2_ = 0.0f;
    uApplied_ = 0.0f;
}

float Adrc::clampU(float u) const {
    if (u < p_.uMin) return p_.uMin;
    if (u > p_.uMax) return p_.uMax;
    return u;
}

float Adrc::update(float setpoint, float y) {
    // LESO, forward Euler, driven by the duty actually applied last interval.
    const float e = y - z1_;
    const float beta1 = 2.0f * p_.wo;
    const float beta2 = p_.wo * p_.wo;
    z1_ += p_.ts * (z2_ + p_.b0 * uApplied_ + beta1 * e);
    z2_ += p_.ts * (beta2 * e);

    // Control law: cancel the estimated total disturbance, close the loop
    // with a proportional gain on the lag-compensated (predicted) output.
    const float yPred = z1_ + p_.predS * (z2_ + p_.b0 * uApplied_);
    const float u0 = p_.wc * (setpoint - yPred);
    const float u = clampU((u0 - z2_) / p_.b0);
    uApplied_ = u;  // default; modulator may correct via setAppliedDuty()
    return u;
}

}  // namespace anita
