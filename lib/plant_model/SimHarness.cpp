#include "SimHarness.h"

namespace anita {

void SimHarness::run(float seconds,
                     const std::function<void(const Snapshot&)>& onControlStep) {
    const float ts = core_.config().adrc.ts;
    const float endS = tS_ + seconds;
    while (tS_ < endS - 1e-6f) {
        // Plant advances one half-wave with the SSR state chosen last tick.
        model_.step(ssrOn_ ? 1.0f : 0.0f, kTickS);
        tS_ += kTickS;
        sinceControlS_ += kTickS;

        if (sinceControlS_ >= ts - 1e-6f) {
            sinceControlS_ = 0.0f;
            core_.setAppliedDuty(mod_.consumeActualDuty());

            CoreInputs in;
            in.boilerC = model_.boilerSensorC();
            in.groupC = model_.groupSensorC();
            in.boilerMv = ntc_.millivoltsFromCelsius(in.boilerC);
            in.groupMv = ntc_.millivoltsFromCelsius(in.groupC);
            in.readingAgeS = 0.0f;
            in.dtS = ts;
            CoreOutputs out = core_.step(in);
            if (forceActive_ && out.fault == Fault::None) out.duty = forcedDuty_;
            mod_.setDuty(out.duty);

            snap_.tS = tS_;
            snap_.boilerSensC = in.boilerC;
            snap_.groupSensC = in.groupC;
            snap_.brassC = model_.brassC();
            snap_.waterC = model_.waterC();
            snap_.groupC = model_.groupC();
            snap_.out = out;
            if (onControlStep) onControlStep(snap_);
        }

        ssrOn_ = mod_.tick();
    }
}

}  // namespace anita
