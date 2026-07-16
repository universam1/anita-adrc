#include <unity.h>

#include <cmath>

#include "Adrc.h"

using namespace anita;

void setUp() {}
void tearDown() {}

// Simple first-order plant: dy/dt = -a*(y - yAmb) + b*u  (b != b0 on purpose)
struct Plant {
    float y = 20.0f;
    float a = 0.005f;
    float b = 0.7f;  // real gain, controller believes b0 = 0.55
    float yAmb = 20.0f;
    void step(float u, float dt) { y += dt * (-a * (y - yAmb) + b * u); }
};

static void test_converges_despite_b0_mismatch() {
    AdrcParams p;
    p.predS = 0.0f;  // pure LADRC on an unlagged plant
    Adrc c(p);
    Plant plant;
    c.reset(plant.y);
    float u = 0.0f;
    for (int i = 0; i < 4000; ++i) {  // 2000 s
        plant.step(u, p.ts);
        u = c.update(93.0f, plant.y);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 93.0f, plant.y);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 93.0f, c.z1());
}

static void test_z2_estimates_disturbance() {
    // At steady state z2 must equal the true net disturbance: -a*(y - yAmb)
    AdrcParams p;
    p.predS = 0.0f;
    Adrc c(p);
    Plant plant;
    c.reset(plant.y);
    float u = 0.0f;
    for (int i = 0; i < 6000; ++i) {
        plant.step(u, p.ts);
        u = c.update(93.0f, plant.y);
    }
    const float trueDisturbance = -plant.a * (plant.y - plant.yAmb);
    // z2 absorbs disturbance AND the b0 mismatch: z2 = f + (b - b0)*u
    const float expected = trueDisturbance + (plant.b - p.b0) * u;
    TEST_ASSERT_FLOAT_WITHIN(0.02f, expected, c.z2());
}

static void test_saturation_no_windup() {
    // Long saturated heat-up must not wind the observer up: right after the
    // output leaves saturation, z1 still tracks y closely.
    AdrcParams p;
    Adrc c(p);
    Plant plant;
    c.reset(plant.y);
    float u = 0.0f;
    bool wasSaturated = false;
    for (int i = 0; i < 4000; ++i) {
        plant.step(u, p.ts);
        u = c.update(93.0f, plant.y);
        if (u >= 0.999f) wasSaturated = true;
        TEST_ASSERT_TRUE(std::fabs(c.z1() - plant.y) < 3.0f);
    }
    TEST_ASSERT_TRUE_MESSAGE(wasSaturated, "test never exercised saturation");
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 93.0f, plant.y);
}

static void test_output_clamped() {
    Adrc c;
    c.reset(20.0f);
    const float uHigh = c.update(98.0f, 20.0f);   // far below setpoint
    TEST_ASSERT_TRUE(uHigh >= 0.0f && uHigh <= 1.0f);
    c.reset(120.0f);
    const float uLow = c.update(85.0f, 120.0f);   // far above setpoint
    TEST_ASSERT_EQUAL_FLOAT(0.0f, uLow);
}

static void test_applied_duty_feedback_used() {
    // If the modulator delivered less than commanded, the ESO must integrate
    // the delivered value: two controllers diverge when one is told so.
    AdrcParams p;
    Adrc a(p), b(p);
    a.reset(90.0f);
    b.reset(90.0f);
    a.update(93.0f, 90.0f);
    b.update(93.0f, 90.0f);
    b.setAppliedDuty(0.0f);  // modulator delivered nothing
    a.update(93.0f, 90.1f);
    b.update(93.0f, 90.1f);
    TEST_ASSERT_TRUE(std::fabs(a.z1() - b.z1()) > 1e-6f);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_converges_despite_b0_mismatch);
    RUN_TEST(test_z2_estimates_disturbance);
    RUN_TEST(test_saturation_no_windup);
    RUN_TEST(test_output_clamped);
    RUN_TEST(test_applied_duty_feedback_used);
    return UNITY_END();
}
