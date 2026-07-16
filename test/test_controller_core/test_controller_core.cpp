// Closed-loop regression tests: ControllerCore + SsrModulator against the
// three-mass BoilerModel, i.e. the exact code path the device runs. The
// asserted numbers are the tuning contract — if a parameter change regresses
// heat-up overshoot, steady-state band or draw recovery, CI fails.

#include <unity.h>

#include <algorithm>
#include <cmath>

#include "SimHarness.h"

using namespace anita;

void setUp() {}
void tearDown() {}

namespace {
constexpr float kGroupSet = 93.0f;

struct Metrics {
    float peakBoilerSens = -1e9f;
    float peakBrass = -1e9f;
    float peakGroup = -1e9f;
    float minGroup = 1e9f;
    float tGroupReady = -1.0f;  // first time group_sens >= set - 1
    bool sawFault = false;
    bool sawDraw = false;
    float lastT = 0.0f;
};

void collect(Metrics& m, const SimHarness::Snapshot& s) {
    m.peakBoilerSens = std::max(m.peakBoilerSens, s.boilerSensC);
    m.peakBrass = std::max(m.peakBrass, s.brassC);
    m.peakGroup = std::max(m.peakGroup, s.groupSensC);
    m.minGroup = std::min(m.minGroup, s.groupSensC);
    if (m.tGroupReady < 0.0f && s.groupSensC >= kGroupSet - 1.0f)
        m.tGroupReady = s.tS;
    if (s.out.fault != Fault::None) m.sawFault = true;
    if (s.out.drawActive) m.sawDraw = true;
    m.lastT = s.tS;
}
}  // namespace

static void test_cold_start_heatup() {
    SimHarness h;
    Metrics m;
    float steadyBand = 0.0f;
    h.run(2400.0f, [&](const SimHarness::Snapshot& s) {
        collect(m, s);
        if (s.tS > 2100.0f)
            steadyBand = std::max(steadyBand, std::fabs(s.groupSensC - kGroupSet));
    });

    TEST_ASSERT_FALSE(m.sawFault);
    TEST_ASSERT_TRUE_MESSAGE(m.tGroupReady > 0.0f, "group never got ready");
    TEST_ASSERT_TRUE_MESSAGE(m.tGroupReady < 600.0f, "warm-up too slow");
    // Group overshoot < 1 C
    TEST_ASSERT_TRUE(m.peakGroup < kGroupSet + 1.0f);
    // Boiler never near the 105 C trip; brass (ground truth) bounded too
    TEST_ASSERT_TRUE(m.peakBoilerSens < 103.0f);
    TEST_ASSERT_TRUE(m.peakBrass < 104.0f);
    // Steady state: group within +-0.2 C over the last 5 minutes
    TEST_ASSERT_TRUE(steadyBand <= 0.2f);
}

static void test_boost_speeds_up_warmup() {
    SimHarness withBoost;
    Metrics mb;
    withBoost.run(2400.0f, [&](const auto& s) { collect(mb, s); });

    CoreConfig noBoostCfg;
    noBoostCfg.groupComp.kBoost = 0.0f;
    SimHarness noBoost(noBoostCfg);
    Metrics mn;
    noBoost.run(2400.0f, [&](const auto& s) { collect(mn, s); });

    TEST_ASSERT_FALSE(mb.sawFault);
    TEST_ASSERT_FALSE(mn.sawFault);
    // The delta-T boost must shorten time-to-ready measurably...
    TEST_ASSERT_TRUE(mb.tGroupReady < mn.tGroupReady - 20.0f);
    // ...without costing group overshoot
    TEST_ASSERT_TRUE(mb.peakGroup <= mn.peakGroup + 0.1f);
}

static void test_single_espresso_draw() {
    SimHarness h(CoreConfig{}, BoilerModelParams{}, 90.0f);
    h.run(900.0f);  // settle at setpoint

    h.model().startDraw(30.0f, 30.0f);
    Metrics m;
    float lastOutsideBand = 0.0f;
    h.run(330.0f, [&](const auto& s) {
        collect(m, s);
        if (s.tS > 930.0f && std::fabs(s.groupSensC - kGroupSet) > 0.5f)
            lastOutsideBand = s.tS;
    });

    TEST_ASSERT_FALSE(m.sawFault);
    // A single espresso barely moves the group (flow-heating compensates)
    TEST_ASSERT_TRUE(m.minGroup > kGroupSet - 1.5f);
    // Recovered to +-0.5 C within 60 s of draw end
    TEST_ASSERT_TRUE(lastOutsideBand <= 930.0f + 60.0f);
}

static void test_max_draw_two_cups() {
    SimHarness h(CoreConfig{}, BoilerModelParams{}, 90.0f);
    h.run(900.0f);

    h.model().startDraw(250.0f, 30.0f);  // full boiler volume in 30 s
    Metrics m;
    float dutySatAt = -1.0f;
    float lastOutside1C = 0.0f;
    h.run(600.0f, [&](const auto& s) {
        collect(m, s);
        if (dutySatAt < 0.0f && s.out.duty >= 0.99f) dutySatAt = s.tS;
        if (s.tS > 930.0f && std::fabs(s.groupSensC - kGroupSet) > 1.0f)
            lastOutside1C = s.tS;
    });

    TEST_ASSERT_FALSE(m.sawFault);
    // 1000 W cannot hold the setpoint: duty must slam to 100% promptly
    TEST_ASSERT_TRUE_MESSAGE(dutySatAt > 0.0f, "duty never saturated");
    TEST_ASSERT_TRUE(dutySatAt <= 900.0f + 15.0f);
    TEST_ASSERT_TRUE_MESSAGE(m.sawDraw, "draw never detected");
    // Clean recovery without windup: back within 1 C of the group setpoint
    // in bounded time, never near the safety trip
    TEST_ASSERT_TRUE(lastOutside1C <= 930.0f + 180.0f);
    TEST_ASSERT_TRUE(m.peakBoilerSens < 104.0f);
    // Group overshoot after recovery bounded (< 1 C)
    TEST_ASSERT_TRUE(m.peakGroup < kGroupSet + 1.0f);
}

static void test_warmup_flush() {
    SimHarness h;  // cold start
    float falseDraws = 0.0f;
    h.run(300.0f, [&](const auto& s) {
        if (s.out.drawActive) falseDraws += 1.0f;
    });
    TEST_ASSERT_EQUAL_FLOAT(0.0f, falseDraws);  // warm-up must not trigger

    h.model().startDraw(60.0f, 15.0f);  // flush into an empty cup
    Metrics m;
    float boostAfterFlush = 1e9f;
    h.run(600.0f, [&](const auto& s) {
        collect(m, s);
        boostAfterFlush = s.out.boostC;
    });

    TEST_ASSERT_FALSE(m.sawFault);
    // The flush heats the group directly; the measured deficit and therefore
    // the boost must have decayed essentially to zero by the end
    TEST_ASSERT_TRUE(boostAfterFlush < 1.0f);
    TEST_ASSERT_TRUE(m.peakBoilerSens < 104.5f);
}

static void test_fault_forces_duty_zero() {
    SimHarness h(CoreConfig{}, BoilerModelParams{}, 90.0f);
    h.run(600.0f);

    // Inject an overtemp by stepping the core directly with a hot reading
    ControllerCore& core = h.core();
    Thermistor ntc;
    CoreInputs in;
    in.boilerC = 108.0f;
    in.groupC = 90.0f;
    in.boilerMv = ntc.millivoltsFromCelsius(108.0f);
    in.groupMv = ntc.millivoltsFromCelsius(90.0f);
    in.readingAgeS = 0.0f;
    in.dtS = 0.5f;
    CoreOutputs out = core.step(in);
    TEST_ASSERT_EQUAL(static_cast<int>(Fault::OverTemp),
                      static_cast<int>(out.fault));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.duty);

    // Latched: a healthy reading afterwards still keeps the heater off
    in.boilerC = 95.0f;
    in.boilerMv = ntc.millivoltsFromCelsius(95.0f);
    out = core.step(in);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out.duty);
    TEST_ASSERT_EQUAL(static_cast<int>(State::Fault),
                      static_cast<int>(out.state));
}

static void test_setpoint_clamping() {
    ControllerCore core;
    core.setSetpoint(120.0f);
    TEST_ASSERT_EQUAL_FLOAT(98.0f, core.setpoint());
    core.setSetpoint(50.0f);
    TEST_ASSERT_EQUAL_FLOAT(85.0f, core.setpoint());
    core.setSetpoint(93.0f);
    core.bumpSetpoint(0.5f);
    TEST_ASSERT_EQUAL_FLOAT(93.5f, core.setpoint());
    core.bumpSetpoint(-0.5f);
    core.bumpSetpoint(-0.5f);
    TEST_ASSERT_EQUAL_FLOAT(92.5f, core.setpoint());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_cold_start_heatup);
    RUN_TEST(test_boost_speeds_up_warmup);
    RUN_TEST(test_single_espresso_draw);
    RUN_TEST(test_max_draw_two_cups);
    RUN_TEST(test_warmup_flush);
    RUN_TEST(test_fault_forces_duty_zero);
    RUN_TEST(test_setpoint_clamping);
    return UNITY_END();
}
