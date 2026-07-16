#include "SsrModulator.h"

namespace anita {

SsrModulator::SsrModulator(bool pairFullWaves) : pairFullWaves_(pairFullWaves) {}

void SsrModulator::setDuty(float d) {
    if (d < 0.0f) d = 0.0f;
    if (d > 1.0f) d = 1.0f;
    duty_ = d;
    if (duty_ <= 0.0f) acc_ = 0.0f;  // drop stored charge so off means off now
}

bool SsrModulator::tick() {
    ++ticks_;
    bool fire = false;
    acc_ += pairFullWaves_ ? duty_ * 0.5f : duty_;
    if (pendingSecondHalf_) {
        pendingSecondHalf_ = false;
        fire = true;
    } else if (acc_ >= 1.0f) {
        acc_ -= 1.0f;
        fire = true;
        pendingSecondHalf_ = pairFullWaves_;
    }
    if (fire) ++fired_;
    return fire;
}

float SsrModulator::consumeActualDuty() {
    if (ticks_ == 0) return duty_;
    const float actual = static_cast<float>(fired_) / static_cast<float>(ticks_);
    ticks_ = 0;
    fired_ = 0;
    return actual;
}

}  // namespace anita
