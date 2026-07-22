#pragma once

namespace anita {

// Detects water draws from two independent signatures, either one triggers:
//
//  1. z2 of a fast detection-only ESO (K/s): a draw pulls cold water into
//     the boiler — a sharp negative step in z2, seconds before the lagged
//     shell NTC visibly moves. (The CONTROL ESO stays slow for clean duty;
//     the detection observer's noisier z2 only ever meets a threshold.)
//  2. group temperature rise rate (K/s): hot water flowing through the
//     group heats it within seconds — strongest exactly when the boiler
//     signature is weakest (small espresso draws).
//
// Both signals are compared against their own slowly-tracked baselines, so
// no absolute calibration is needed. Baselines freeze while a draw is
// active.
struct DrawDetectParams {
    float onDeltaKps = 0.05f;    // det-z2 below baseline by this => draw
    float offDeltaKps = 0.04f;   // det-z2 back within this => ended
    float groupRiseKps = 0.04f;  // group slope above baseline by this => draw
    float onDebounceS = 1.0f;
    float offDebounceS = 4.0f;
    float maxDrawS = 90.0f;      // cap: bounds damage from a false trigger
    float baselineTauS = 180.0f; // slow EMA of the idle signals
};

class DrawDetector {
public:
    explicit DrawDetector(const DrawDetectParams& p = {});

    // `enabled` gates detection (the core enables it once regulation has been
    // reached — during initial warm-up both signals are large transients and
    // would false-trigger). While disabled the detector is inactive and the
    // baselines re-seed on the next enabled step.
    bool step(float z2, float groupSlope, float dtS, bool enabled = true);
    bool active() const { return active_; }
    float baseline() const { return baseline_; }
    const DrawDetectParams& params() const { return p_; }

private:
    DrawDetectParams p_;
    bool active_ = false;
    bool lockout_ = false;  // after a maxDrawS timeout: no re-trigger until
                            // both signals returned to baseline (stuck guard)
    bool baselineInit_ = false;
    float baseline_ = 0.0f;       // idle det-z2
    float groupBaseline_ = 0.0f;  // idle group slope
    float onTimerS_ = 0.0f;
    float offTimerS_ = 0.0f;
    float activeForS_ = 0.0f;
};

}  // namespace anita
