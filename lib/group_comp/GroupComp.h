#pragma once

namespace anita {

// Delta-T-driven group compensation. The user setpoint is the desired GROUP
// temperature; the boiler setpoint is elevated by the measured group deficit:
//
//   boilerSet = groupSet + offsetSs + kBoost * max(0, groupSet - groupTemp)
//
// clamped to a boost headroom and an absolute boiler ceiling (below the
// safety cutoff). Because the deficit is measured every cycle, one law covers
// slow conductive self-heating after power-on, direct heating by pumped
// water (flush / shot), and cool-down — no schedule to learn.
//
// The only adapted parameter is offsetSs, the steady-state boiler<->group
// offset: integral-learned at quiescence (near setpoint, no draw, boost ~0)
// with a time constant much slower than the inner ADRC loop, persisted by
// the caller (NVS on the device).
struct GroupCompParams {
    float kBoost = 2.0f;          // boost degC per degC of group deficit
    float maxBoostC = 15.0f;      // headroom clamp on the boost term
    float minBoostC = -5.0f;      // negative authority when the group overshoots
    float maxBoilerSetC = 101.0f; // absolute ceiling (4 C under the safety trip)
    float offsetInitC = 6.0f;     // starting boiler<->group offset
    float offsetMinC = 0.0f;
    float offsetMaxC = 15.0f;
    float offsetLearnPerS = 0.002f;  // integral rate at quiescence, 1/s
    float quietBoilerBandC = 0.7f;   // |boiler - boilerSet| below this ...
    float quietBoostMaxC = 1.0f;     // ... and boost below this => quiescent
};

class GroupComp {
public:
    explicit GroupComp(const GroupCompParams& p = {});

    // Returns the boiler setpoint for this cycle and (at quiescence) adapts
    // offsetSs toward zero group error.
    float boilerSetpoint(float groupSetC, float groupTempC, float boilerTempC,
                         bool drawActive, float dtS);

    float offsetSs() const { return offsetSs_; }
    void setOffsetSs(float c);  // restore from NVS
    float boostC() const { return boost_; }
    bool offsetDirty() const { return offsetDirty_; }
    void clearOffsetDirty() { offsetDirty_ = false; }

    void setKBoost(float k) { p_.kBoost = k; }
    const GroupCompParams& params() const { return p_; }

private:
    GroupCompParams p_;
    float offsetSs_;
    float boost_ = 0.0f;
    float lastBoilerSet_ = 0.0f;
    float offsetAccum_ = 0.0f;   // accumulate before flagging dirty
    bool offsetDirty_ = false;
};

}  // namespace anita
