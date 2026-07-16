#include <unity.h>

#include "DrawDetector.h"

using namespace anita;

void setUp() {}
void tearDown() {}

namespace {
constexpr float kDt = 0.5f;
constexpr float kIdleZ2 = -0.07f;  // typical idle disturbance (heat loss)

void settle(DrawDetector& d, float seconds) {
    for (float t = 0.0f; t < seconds; t += kDt) d.step(kIdleZ2, kDt);
}
}  // namespace

static void test_idle_never_triggers() {
    DrawDetector d;
    for (int i = 0; i < 4000; ++i) TEST_ASSERT_FALSE(d.step(kIdleZ2, kDt));
}

static void test_detects_draw_and_recovers() {
    DrawDetector d;
    settle(d, 60.0f);
    // Draw: z2 drops well below baseline
    float detectedAfter = -1.0f;
    for (float t = 0.0f; t < 30.0f; t += kDt) {
        if (d.step(kIdleZ2 - 0.15f, kDt) && detectedAfter < 0.0f)
            detectedAfter = t;
    }
    TEST_ASSERT_TRUE_MESSAGE(detectedAfter >= 0.0f, "draw not detected");
    TEST_ASSERT_TRUE(detectedAfter <= 5.0f);  // debounce only, no extra lag
    // Draw ends: z2 returns to idle, detector must release
    for (float t = 0.0f; t < 20.0f; t += kDt) d.step(kIdleZ2, kDt);
    TEST_ASSERT_FALSE(d.active());
}

static void test_small_wiggle_ignored() {
    DrawDetector d;
    settle(d, 60.0f);
    // Wiggle smaller than onDelta: never triggers
    for (int i = 0; i < 200; ++i) {
        TEST_ASSERT_FALSE(d.step(kIdleZ2 - 0.03f, kDt));
        TEST_ASSERT_FALSE(d.step(kIdleZ2 + 0.01f, kDt));
    }
}

static void test_max_duration_cap() {
    // A stuck-low z2 (false trigger) may hold the feedforward at most maxDrawS.
    DrawDetector d;
    settle(d, 60.0f);
    float activeS = 0.0f;
    for (float t = 0.0f; t < 300.0f; t += kDt) {
        if (d.step(kIdleZ2 - 0.2f, kDt)) activeS += kDt;
    }
    TEST_ASSERT_TRUE(activeS <= d.params().maxDrawS + 5.0f);
}

static void test_disabled_gate() {
    DrawDetector d;
    // Huge negative z2 while disabled (warm-up transient): must stay quiet
    for (int i = 0; i < 400; ++i)
        TEST_ASSERT_FALSE(d.step(-0.6f, kDt, false));
    // Once enabled, the baseline re-seeds at the current level: no immediate
    // false trigger from the level itself
    for (int i = 0; i < 100; ++i) TEST_ASSERT_FALSE(d.step(-0.07f, kDt, true));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_never_triggers);
    RUN_TEST(test_detects_draw_and_recovers);
    RUN_TEST(test_small_wiggle_ignored);
    RUN_TEST(test_max_duration_cap);
    RUN_TEST(test_disabled_gate);
    return UNITY_END();
}
