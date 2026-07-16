#pragma once

#include <functional>

#include "BoilerModel.h"
#include "ControllerCore.h"
#include "SsrModulator.h"
#include "Thermistor.h"

namespace anita {

// Closed loop of ControllerCore + SsrModulator + BoilerModel, stepped at the
// modulator's 10 ms half-wave tick with the controller running every
// AdrcParams::ts. Shared by the simulator binary and the regression tests so
// both exercise the exact code path the device runs.
class SimHarness {
public:
    struct Snapshot {
        float tS = 0.0f;
        float boilerSensC = 0.0f, groupSensC = 0.0f;
        float brassC = 0.0f, waterC = 0.0f, groupC = 0.0f;
        CoreOutputs out{};
    };

    SimHarness(const CoreConfig& cfg = {}, const BoilerModelParams& plant = {},
               float t0C = 20.0f)
        : core_(cfg), model_(plant, t0C), ntc_() {}

    ControllerCore& core() { return core_; }
    BoilerModel& model() { return model_; }

    // Run for `seconds`, invoking onControlStep (if set) after every control
    // period with the latest snapshot.
    void run(float seconds, const std::function<void(const Snapshot&)>& onControlStep = {});

    const Snapshot& last() const { return snap_; }

private:
    static constexpr float kTickS = 0.01f;  // one half-wave at 50 Hz

    ControllerCore core_;
    BoilerModel model_;
    SsrModulator mod_;
    Thermistor ntc_;
    Snapshot snap_{};
    float tS_ = 0.0f;
    float sinceControlS_ = 1e9f;  // force a control step on the first tick
    bool ssrOn_ = false;
};

}  // namespace anita
