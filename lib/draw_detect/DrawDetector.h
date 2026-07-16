#pragma once

namespace anita {

// Detects water draws from the ESO's total-disturbance estimate z2 (K/s).
// A draw pulls cold water into the boiler, which shows up as a sharp negative
// step in z2 within seconds — well before the lagged shell NTC moves much.
//
// Detection is relative to a slowly-tracked baseline (idle z2 is already
// negative: it carries the ambient heat loss), so no absolute calibration is
// needed. The baseline freezes while a draw is active.
struct DrawDetectParams {
    float onDeltaKps = 0.05f;    // z2 below baseline by this much => draw
    float offDeltaKps = 0.03f;   // z2 back within this of baseline => ended
    float onDebounceS = 1.0f;
    float offDebounceS = 4.0f;
    float maxDrawS = 90.0f;      // cap: bounds damage from a false trigger
    float baselineTauS = 180.0f; // slow EMA of idle z2
};

class DrawDetector {
public:
    explicit DrawDetector(const DrawDetectParams& p = {});

    // `enabled` gates detection (the core enables it once regulation has been
    // reached — during initial warm-up z2 is a large transient and would
    // false-trigger). While disabled the detector is inactive and the
    // baseline re-seeds on the next enabled step.
    bool step(float z2, float dtS, bool enabled = true);
    bool active() const { return active_; }
    float baseline() const { return baseline_; }
    const DrawDetectParams& params() const { return p_; }

private:
    DrawDetectParams p_;
    bool active_ = false;
    bool lockout_ = false;  // after a maxDrawS timeout: no re-trigger until
                            // z2 has returned to baseline (stuck-low guard)
    bool baselineInit_ = false;
    float baseline_ = 0.0f;
    float onTimerS_ = 0.0f;
    float offTimerS_ = 0.0f;
    float activeForS_ = 0.0f;
};

}  // namespace anita
