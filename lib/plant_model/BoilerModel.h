#pragma once

#include <cstddef>
#include <vector>

namespace anita {

// Three-mass thermal model of the Lelit Anita boiler for host simulation:
//
//   brass shell (heated by the element)
//     <-> water (250 ml)          via kBw
//     <-> group head casting      via kBg
//   both brass and group lose to ambient.
//
// Water draws are rate-based: startDraw(ml, seconds) continuously replaces
// hot boiler water with cold inlet water, and the outflowing hot water heats
// the group on its way through (flow-coupling with effectiveness flowEff).
//
// Sensors: shell-clamped NTCs modelled as first-order lags on their mass
// temperature plus a transport delay on the boiler channel.
struct BoilerModelParams {
    // Heat capacities, J/K
    float cBrass = 760.0f;   // ~2.0 kg brass * 380
    float cWater = 1046.0f;  // 0.25 l * 4186
    float cGroup = 500.0f;   // ~1.3 kg group casting

    // Couplings / losses, W/K
    float kBw = 30.0f;    // brass <-> water
    float kBg = 6.0f;     // brass <-> group (bolted-on group)
    float kAmbB = 1.2f;   // boiler to ambient
    float kAmbG = 0.5f;   // group to ambient

    float pHeaterW = 1000.0f;
    float tAmbC = 20.0f;
    float tInletC = 20.0f;

    // Sensor dynamics
    float tauNtcBoilerS = 8.0f;
    float tauNtcGroupS = 15.0f;
    float delayBoilerS = 2.0f;  // transport delay on the boiler channel

    float flowEff = 0.3f;  // fraction of (Twater - Tgroup) given to the group
                           // by water flowing through it during a draw

    float dtInternalS = 0.05f;  // integration step
};

class BoilerModel {
public:
    explicit BoilerModel(const BoilerModelParams& p = {}, float t0C = 20.0f);

    // Advance the model by dtS seconds with the given heater duty [0,1].
    void step(float duty, float dtS);

    // Start replacing `ml` of hot water with cold inlet water over `seconds`.
    void startDraw(float ml, float seconds);
    bool drawActive() const { return drawRemainingMl_ > 0.0f; }

    // What the firmware would see:
    float boilerSensorC() const;  // lagged + delayed
    float groupSensorC() const { return tSensGroup_; }

    // Ground truth for assertions/plots:
    float brassC() const { return tBrass_; }
    float waterC() const { return tWater_; }
    float groupC() const { return tGroup_; }

    const BoilerModelParams& params() const { return p_; }

private:
    void integrate(float duty, float dt);

    BoilerModelParams p_;
    float tBrass_, tWater_, tGroup_;
    float tSensBoiler_, tSensGroup_;

    float drawRemainingMl_ = 0.0f;
    float drawRateMlPerS_ = 0.0f;

    std::vector<float> delayLine_;  // ring buffer for the boiler sensor delay
    size_t delayIdx_ = 0;
};

}  // namespace anita
