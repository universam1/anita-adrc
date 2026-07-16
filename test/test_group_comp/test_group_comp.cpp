#include <unity.h>

#include "GroupComp.h"

using namespace anita;

void setUp() {}
void tearDown() {}

static void test_boost_proportional_to_deficit() {
    GroupComp g;
    const auto& p = g.params();
    // Group 4 C below target: boost = kBoost * 4
    g.boilerSetpoint(93.0f, 89.0f, 95.0f, false, 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, p.kBoost * 4.0f, g.boostC());
}

static void test_boost_clamped_high_and_ceiling() {
    GroupComp g;
    const auto& p = g.params();
    // Cold group: huge deficit, boost hits its clamp, setpoint its ceiling
    const float set = g.boilerSetpoint(93.0f, 20.0f, 40.0f, false, 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, p.maxBoostC, g.boostC());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, p.maxBoilerSetC, set);
}

static void test_negative_boost_on_group_overshoot() {
    GroupComp g;
    const auto& p = g.params();
    g.boilerSetpoint(93.0f, 96.0f, 99.0f, false, 0.5f);  // group 3 C hot
    TEST_ASSERT_TRUE(g.boostC() < 0.0f);
    TEST_ASSERT_TRUE(g.boostC() >= p.minBoostC - 0.01f);
}

static void test_offset_learns_only_at_quiescence() {
    GroupComp g;
    const float initial = g.offsetSs();

    // Not quiescent: boiler far from its setpoint -> no learning
    for (int i = 0; i < 1000; ++i)
        g.boilerSetpoint(93.0f, 92.5f, 60.0f, false, 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, initial, g.offsetSs());

    // Not quiescent: draw active -> no learning
    float set = g.boilerSetpoint(93.0f, 92.5f, 60.0f, false, 0.5f);
    for (int i = 0; i < 1000; ++i)
        set = g.boilerSetpoint(93.0f, 92.5f, set, true, 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, initial, g.offsetSs());

    // Quiescent with a persistent group deficit -> offset creeps up
    set = g.boilerSetpoint(93.0f, 92.7f, set, false, 0.5f);
    for (int i = 0; i < 2000; ++i)  // 1000 s
        set = g.boilerSetpoint(93.0f, 92.7f, set, false, 0.5f);
    TEST_ASSERT_TRUE(g.offsetSs() > initial + 0.2f);
}

static void test_offset_learning_is_slow() {
    // The outer integrator must be orders slower than the inner loop: after a
    // full minute of quiescent 0.5 C error it may move only marginally.
    GroupComp g;
    const float initial = g.offsetSs();
    float set = g.boilerSetpoint(93.0f, 92.5f, 99.0f, false, 0.5f);
    for (int i = 0; i < 120; ++i)
        set = g.boilerSetpoint(93.0f, 92.5f, set, false, 0.5f);
    TEST_ASSERT_TRUE(g.offsetSs() - initial < 0.1f);
}

static void test_offset_restore_clamped() {
    GroupComp g;
    g.setOffsetSs(99.0f);  // corrupt NVS value
    TEST_ASSERT_TRUE(g.offsetSs() <= g.params().offsetMaxC);
    g.setOffsetSs(-4.0f);
    TEST_ASSERT_TRUE(g.offsetSs() >= g.params().offsetMinC);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_boost_proportional_to_deficit);
    RUN_TEST(test_boost_clamped_high_and_ceiling);
    RUN_TEST(test_negative_boost_on_group_overshoot);
    RUN_TEST(test_offset_learns_only_at_quiescence);
    RUN_TEST(test_offset_learning_is_slow);
    RUN_TEST(test_offset_restore_clamped);
    return UNITY_END();
}
