#pragma once

#include <cstdint>

namespace anita {

// Constant On-Time PFM modulator for a zero-cross SSR, implemented as a
// first-order sigma-delta accumulator ticked once per mains half-wave (10 ms
// at 50 Hz).
//
// At low duty this emits isolated single half-waves at a variable repetition
// rate (classic COT-PFM — minimal energy quanta, evenly spread, ideal for the
// idle phase). At 50% it degenerates into strict on/off alternation, and at
// high duty into dense interleaving. Long-run resolution is limited only by
// runtime, not by a fixed burst window.
//
// Optional pairing mode fires full waves (two consecutive half-waves) to keep
// mains current symmetric (EN 61000-3 politeness) at the cost of a 20 ms
// energy quantum.
class SsrModulator {
public:
    explicit SsrModulator(bool pairFullWaves = false);

    void setDuty(float d);        // clamped to [0, 1]
    float duty() const { return duty_; }

    // Call exactly once per half-wave tick; returns true if the SSR should
    // conduct for this half-wave.
    bool tick();

    // Average duty actually delivered since the last call (fired / ticks);
    // resets the window. Feed this back to the ESO so the observer sees what
    // really happened. Returns the commanded duty if no ticks elapsed.
    float consumeActualDuty();

private:
    float duty_ = 0.0f;
    float acc_ = 0.0f;
    bool pairFullWaves_;
    bool pendingSecondHalf_ = false;
    uint32_t ticks_ = 0;
    uint32_t fired_ = 0;
};

}  // namespace anita
