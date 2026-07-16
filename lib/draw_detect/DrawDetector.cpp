#include "DrawDetector.h"

namespace anita {

DrawDetector::DrawDetector(const DrawDetectParams& p) : p_(p) {}

bool DrawDetector::step(float z2, float dtS, bool enabled) {
    if (!enabled) {
        active_ = false;
        lockout_ = false;
        baselineInit_ = false;
        onTimerS_ = 0.0f;
        offTimerS_ = 0.0f;
        return false;
    }
    if (!baselineInit_) {
        baseline_ = z2;
        baselineInit_ = true;
    }

    if (!active_) {
        // Track the idle disturbance level slowly.
        const float alpha = dtS / (p_.baselineTauS + dtS);
        baseline_ += alpha * (z2 - baseline_);

        if (lockout_) {
            if (z2 > baseline_ - p_.offDeltaKps) lockout_ = false;
            return false;
        }
        if (z2 < baseline_ - p_.onDeltaKps) {
            onTimerS_ += dtS;
            if (onTimerS_ >= p_.onDebounceS) {
                active_ = true;
                activeForS_ = 0.0f;
                offTimerS_ = 0.0f;
            }
        } else {
            onTimerS_ = 0.0f;
        }
    } else {
        activeForS_ += dtS;
        if (z2 > baseline_ - p_.offDeltaKps) {
            offTimerS_ += dtS;
        } else {
            offTimerS_ = 0.0f;
        }
        if (activeForS_ >= p_.maxDrawS) {
            active_ = false;
            lockout_ = true;  // demand a clean return to baseline first
            onTimerS_ = 0.0f;
        } else if (offTimerS_ >= p_.offDebounceS) {
            active_ = false;
            onTimerS_ = 0.0f;
        }
    }
    return active_;
}

}  // namespace anita
