#pragma once

namespace anita {

// First-order linear ADRC (LADRC) with a 2nd-order linear extended state
// observer (LESO). Plant abstraction: dy/dt = f(t) + b0*u, where f lumps heat
// loss, water draws, model error and sensor-lag effects; the ESO estimates
// f as z2 and the control law cancels it.
//
// Bandwidth parameterization: kp = wc, beta1 = 2*wo, beta2 = wo^2.
struct AdrcParams {
    float b0 = 0.55f;   // (K/s) / duty — from physics: P / (Cwater + Cbrass)
    float wc = 0.04f;   // controller bandwidth, rad/s
    float wo = 0.16f;   // observer bandwidth, rad/s (3..5 x wc)
    float ts = 0.5f;    // sample time, s
    float uMin = 0.0f;
    float uMax = 1.0f;
    // Prediction horizon compensating the shell-NTC lag: the P term acts on
    // y_pred = z1 + predS * dy/dt (the ESO provides dy/dt = z2 + b0*u for
    // free). Zero-bias at steady state (dy/dt ~ 0); during a fast ramp it
    // backs the duty off early enough that the lagged sensor cannot drag the
    // real shell temperature into an overshoot.
    float predS = 20.0f;
};

class Adrc {
public:
    explicit Adrc(const AdrcParams& p = {});

    // Initialize observer at a known output (bumpless start).
    void reset(float y0);

    // One control step. y is the measured output; returns saturated u.
    // The ESO is propagated with the duty that was actually applied during
    // the previous interval (see setAppliedDuty), which is the anti-windup:
    // saturation mismatch lands honestly in z2 instead of winding up.
    float update(float setpoint, float y);

    // Report the duty really delivered by the modulator since the last
    // update (quantization means it can differ slightly from the command).
    void setAppliedDuty(float u) { uApplied_ = clampU(u); }

    float z1() const { return z1_; }
    float z2() const { return z2_; }

    // Live tuning (MQTT / serial).
    void setB0(float b0) { p_.b0 = b0; }
    void setBandwidths(float wc, float wo) { p_.wc = wc; p_.wo = wo; }
    const AdrcParams& params() const { return p_; }

private:
    float clampU(float u) const;

    AdrcParams p_;
    float z1_ = 0.0f;
    float z2_ = 0.0f;
    float uApplied_ = 0.0f;
};

}  // namespace anita
