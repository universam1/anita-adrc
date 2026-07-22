#include "DrawDetector.h"

namespace anita {

DrawDetector::DrawDetector(const DrawDetectParams& p) : p_(p) {}

bool DrawDetector::step(float z2, float groupSlope, float dtS, bool enabled) {
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
        groupBaseline_ = groupSlope;
        baselineInit_ = true;
    }

    const bool z2Hit = z2 < baseline_ - p_.onDeltaKps;
    const bool groupHit = groupSlope > groupBaseline_ + p_.groupRiseKps;
    const bool z2Clear = z2 > baseline_ - p_.offDeltaKps;
    const bool groupClear =
        groupSlope < groupBaseline_ + 0.5f * p_.groupRiseKps;

    if (!active_) {
        // Track the idle levels slowly while no draw is in progress.
        const float alpha = dtS / (p_.baselineTauS + dtS);
        baseline_ += alpha * (z2 - baseline_);
        groupBaseline_ += alpha * (groupSlope - groupBaseline_);

        if (lockout_) {
            if (z2Clear && groupClear) lockout_ = false;
            return false;
        }
        if (z2Hit || groupHit) {
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
        if (z2Clear && groupClear) {
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
