#include <unity.h>

#include "DrawDetector.h"

using namespace anita;

void setUp() {}
void tearDown() {}

namespace {
constexpr float kDt = 0.5f;
constexpr float kIdleZ2 = -0.07f;   // typical idle disturbance (heat loss)
constexpr float kIdleSlope = 0.0f;  // group is flat at steady state

void settle(DrawDetector& d, float seconds) {
    for (float t = 0.0f; t < seconds; t += kDt)
        d.step(kIdleZ2, kIdleSlope, kDt);
}
}  // namespace

static void test_idle_never_triggers() {
    DrawDetector d;
    for (int i = 0; i < 4000; ++i)
        TEST_ASSERT_FALSE(d.step(kIdleZ2, kIdleSlope, kDt));
}

static void test_detects_draw_via_z2_and_recovers() {
    DrawDetector d;
    settle(d, 60.0f);
    // Draw: fast-observer z2 drops well below baseline
    float detectedAfter = -1.0f;
    for (float t = 0.0f; t < 30.0f; t += kDt) {
        if (d.step(kIdleZ2 - 0.15f, kIdleSlope, kDt) && detectedAfter < 0.0f)
            detectedAfter = t;
    }
    TEST_ASSERT_TRUE_MESSAGE(detectedAfter >= 0.0f, "draw not detected");
    TEST_ASSERT_TRUE(detectedAfter <= 5.0f);  // debounce only, no extra lag
    // Draw ends: signals return to idle, detector must release
    for (float t = 0.0f; t < 20.0f; t += kDt) d.step(kIdleZ2, kIdleSlope, kDt);
    TEST_ASSERT_FALSE(d.active());
}

static void test_detects_draw_via_group_rise_alone() {
    // Small espresso: boiler z2 barely moves, but the group heats fast.
    DrawDetector d;
    settle(d, 60.0f);
    float detectedAfter = -1.0f;
    for (float t = 0.0f; t < 30.0f; t += kDt) {
        if (d.step(kIdleZ2 - 0.01f, 0.12f, kDt) && detectedAfter < 0.0f)
            detectedAfter = t;
    }
    TEST_ASSERT_TRUE_MESSAGE(detectedAfter >= 0.0f, "group rise not detected");
    TEST_ASSERT_TRUE(detectedAfter <= 5.0f);
    // Detector holds while the group is still hot-flowing, releases after
    for (float t = 0.0f; t < 20.0f; t += kDt) d.step(kIdleZ2, kIdleSlope, kDt);
    TEST_ASSERT_FALSE(d.active());
}

static void test_slow_group_warmup_ignored() {
    // Normal conductive group warm-up (~0.01-0.02 K/s) must never trigger.
    DrawDetector d;
    settle(d, 60.0f);
    for (int i = 0; i < 600; ++i)
        TEST_ASSERT_FALSE(d.step(kIdleZ2, 0.02f, kDt));
}

static void test_small_wiggle_ignored() {
    DrawDetector d;
    settle(d, 60.0f);
    // Wiggles smaller than the thresholds: never triggers
    for (int i = 0; i < 200; ++i) {
        TEST_ASSERT_FALSE(d.step(kIdleZ2 - 0.05f, 0.02f, kDt));
        TEST_ASSERT_FALSE(d.step(kIdleZ2 + 0.01f, -0.01f, kDt));
    }
}

static void test_max_duration_cap() {
    // A stuck-low z2 (false trigger) may hold the feedforward at most maxDrawS.
    DrawDetector d;
    settle(d, 60.0f);
    float activeS = 0.0f;
    for (float t = 0.0f; t < 300.0f; t += kDt) {
        if (d.step(kIdleZ2 - 0.2f, kIdleSlope, kDt)) activeS += kDt;
    }
    TEST_ASSERT_TRUE(activeS <= d.params().maxDrawS + 5.0f);
}

static void test_disabled_gate() {
    DrawDetector d;
    // Huge transients while disabled (warm-up): must stay quiet
    for (int i = 0; i < 400; ++i)
        TEST_ASSERT_FALSE(d.step(-0.6f, 0.3f, kDt, false));
    // Once enabled, the baselines re-seed at the current level: no immediate
    // false trigger from the level itself
    for (int i = 0; i < 100; ++i)
        TEST_ASSERT_FALSE(d.step(kIdleZ2, kIdleSlope, kDt, true));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_never_triggers);
    RUN_TEST(test_detects_draw_via_z2_and_recovers);
    RUN_TEST(test_detects_draw_via_group_rise_alone);
    RUN_TEST(test_slow_group_warmup_ignored);
    RUN_TEST(test_small_wiggle_ignored);
    RUN_TEST(test_max_duration_cap);
    RUN_TEST(test_disabled_gate);
    return UNITY_END();
}
