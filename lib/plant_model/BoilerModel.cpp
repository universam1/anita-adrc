#include "BoilerModel.h"

#include <algorithm>
#include <cmath>

namespace anita {

namespace {
constexpr float kCpWaterPerMl = 4.186f;  // J/(ml*K)
}

BoilerModel::BoilerModel(const BoilerModelParams& p, float t0C)
    : p_(p),
      tBrass_(t0C),
      tWater_(t0C),
      tGroup_(t0C),
      tSensBoiler_(t0C),
      tSensGroup_(t0C) {
    const size_t delaySamples =
        std::max<size_t>(1, static_cast<size_t>(p_.delayBoilerS / p_.dtInternalS));
    delayLine_.assign(delaySamples, t0C);
}

void BoilerModel::startDraw(float ml, float seconds) {
    drawRemainingMl_ = ml;
    drawRateMlPerS_ = (seconds > 0.0f) ? ml / seconds : ml;
}

void BoilerModel::integrate(float duty, float dt) {
    float q = 0.0f;  // ml/s currently flowing
    if (drawRemainingMl_ > 0.0f) {
        q = drawRateMlPerS_;
        drawRemainingMl_ -= q * dt;
        if (drawRemainingMl_ < 0.0f) drawRemainingMl_ = 0.0f;
    }

    const float pHeat = p_.pHeaterW * duty;
    const float qBw = p_.kBw * (tBrass_ - tWater_);
    const float qBg = p_.kBg * (tBrass_ - tGroup_);
    const float qAmbB = p_.kAmbB * (tBrass_ - p_.tAmbC);
    const float qAmbG = p_.kAmbG * (tGroup_ - p_.tAmbC);
    // Draw: hot water leaves and is replaced by cold inlet water...
    const float qDraw = q * kCpWaterPerMl * (tWater_ - p_.tInletC);
    // ...and on its way out it dumps part of its heat into the group casting.
    const float qFlowG = q * kCpWaterPerMl * p_.flowEff * (tWater_ - tGroup_);

    tBrass_ += dt * (pHeat - qBw - qBg - qAmbB) / p_.cBrass;
    tWater_ += dt * (qBw - qDraw) / p_.cWater;
    tGroup_ += dt * (qBg - qAmbG + qFlowG) / p_.cGroup;

    // Boiler sensor: transport delay then first-order lag.
    delayLine_[delayIdx_] = tBrass_;
    delayIdx_ = (delayIdx_ + 1) % delayLine_.size();
    const float delayedBrass = delayLine_[delayIdx_];
    tSensBoiler_ += dt * (delayedBrass - tSensBoiler_) / p_.tauNtcBoilerS;
    tSensGroup_ += dt * (tGroup_ - tSensGroup_) / p_.tauNtcGroupS;
}

void BoilerModel::step(float duty, float dtS) {
    duty = std::min(1.0f, std::max(0.0f, duty));
    float remaining = dtS;
    while (remaining > 1e-6f) {
        const float dt = std::min(remaining, p_.dtInternalS);
        integrate(duty, dt);
        remaining -= dt;
    }
}

float BoilerModel::boilerSensorC() const { return tSensBoiler_; }

}  // namespace anita
