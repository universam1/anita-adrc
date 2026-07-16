#include <unity.h>

#include "SafetyMonitor.h"

using namespace anita;

void setUp() {}
void tearDown() {}

static SafetyInputs healthy() {
    SafetyInputs in;
    in.boilerMv = 1800.0f;
    in.groupMv = 1500.0f;
    in.boilerC = 95.0f;
    in.boilerSetC = 99.0f;
    in.duty = 0.15f;
    in.readingAgeS = 0.1f;
    in.dtS = 0.5f;
    return in;
}

static void test_healthy_stays_clear() {
    SafetyMonitor s;
    auto in = healthy();
    for (int i = 0; i < 1000; ++i) {
        TEST_ASSERT_EQUAL(static_cast<int>(Fault::None),
                          static_cast<int>(s.step(in)));
    }
}

static void test_rail_faults_per_channel() {
    {
        SafetyMonitor s;
        auto in = healthy();
        in.boilerMv = 10.0f;
        TEST_ASSERT_EQUAL(static_cast<int>(Fault::BoilerNtcOpen),
                          static_cast<int>(s.step(in)));
    }
    {
        SafetyMonitor s;
        auto in = healthy();
        in.boilerMv = 3200.0f;
        TEST_ASSERT_EQUAL(static_cast<int>(Fault::BoilerNtcShort),
                          static_cast<int>(s.step(in)));
    }
    {
        SafetyMonitor s;
        auto in = healthy();
        in.groupMv = 10.0f;
        TEST_ASSERT_EQUAL(static_cast<int>(Fault::GroupNtcOpen),
                          static_cast<int>(s.step(in)));
    }
    {
        SafetyMonitor s;
        auto in = healthy();
        in.groupMv = 3200.0f;
        TEST_ASSERT_EQUAL(static_cast<int>(Fault::GroupNtcShort),
                          static_cast<int>(s.step(in)));
    }
}

static void test_overtemp_and_latching() {
    SafetyMonitor s;
    auto in = healthy();
    in.boilerC = 106.0f;
    TEST_ASSERT_EQUAL(static_cast<int>(Fault::OverTemp),
                      static_cast<int>(s.step(in)));
    // Latched: healthy inputs do not clear it
    TEST_ASSERT_EQUAL(static_cast<int>(Fault::OverTemp),
                      static_cast<int>(s.step(healthy())));
    s.reset();
    TEST_ASSERT_EQUAL(static_cast<int>(Fault::None),
                      static_cast<int>(s.step(healthy())));
}

static void test_stale_reading() {
    SafetyMonitor s;
    auto in = healthy();
    in.readingAgeS = 3.0f;
    TEST_ASSERT_EQUAL(static_cast<int>(Fault::StaleReading),
                      static_cast<int>(s.step(in)));
}

static void test_no_rise_trips() {
    // Full duty for >60 s, temperature flat, far below setpoint: NTC fell off
    // the boiler or the SSR is stuck. Must trip.
    SafetyMonitor s;
    auto in = healthy();
    in.boilerC = 40.0f;
    in.boilerSetC = 99.0f;
    in.duty = 1.0f;
    Fault f = Fault::None;
    for (int i = 0; i < 140; ++i) f = s.step(in);  // 70 s
    TEST_ASSERT_EQUAL(static_cast<int>(Fault::NoRise), static_cast<int>(f));
}

static void test_no_rise_does_not_trip_when_heating() {
    // Same duty but the boiler is actually rising: fine.
    SafetyMonitor s;
    auto in = healthy();
    in.boilerC = 40.0f;
    in.boilerSetC = 99.0f;
    in.duty = 1.0f;
    Fault f = Fault::None;
    for (int i = 0; i < 400; ++i) {
        in.boilerC += 0.25f;  // 0.5 K/s
        f = s.step(in);
        if (in.boilerC > 95.0f) break;
    }
    TEST_ASSERT_EQUAL(static_cast<int>(Fault::None), static_cast<int>(f));
}

static void test_no_rise_does_not_trip_at_setpoint() {
    // Sitting at setpoint with moderate duty (draw recovery): no trip even if
    // temperature is flat.
    SafetyMonitor s;
    auto in = healthy();
    in.boilerC = 98.0f;
    in.boilerSetC = 99.0f;
    in.duty = 0.6f;
    Fault f = Fault::None;
    for (int i = 0; i < 400; ++i) f = s.step(in);
    TEST_ASSERT_EQUAL(static_cast<int>(Fault::None), static_cast<int>(f));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_healthy_stays_clear);
    RUN_TEST(test_rail_faults_per_channel);
    RUN_TEST(test_overtemp_and_latching);
    RUN_TEST(test_stale_reading);
    RUN_TEST(test_no_rise_trips);
    RUN_TEST(test_no_rise_does_not_trip_when_heating);
    RUN_TEST(test_no_rise_does_not_trip_at_setpoint);
    return UNITY_END();
}
