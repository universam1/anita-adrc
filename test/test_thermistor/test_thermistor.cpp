#include <unity.h>

#include <initializer_list>

#include "Thermistor.h"

using namespace anita;

void setUp() {}
void tearDown() {}

static void test_round_trip() {
    Thermistor t;
    for (float c : {5.0f, 25.0f, 60.0f, 85.0f, 93.0f, 98.0f, 105.0f}) {
        const float mv = t.millivoltsFromCelsius(c);
        TEST_ASSERT_FLOAT_WITHIN(0.05f, c, t.celsiusFromMillivolts(mv));
    }
}

static void test_expected_divider_points() {
    Thermistor t;
    // 100k NTC high-side with 10k to GND on 3.3 V:
    // 25 C => node at Vs * 10k/110k = 300 mV
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 300.0f, t.millivoltsFromCelsius(25.0f));
    // ~95 C => NTC ~8 kOhm => ~1.83 V (inside the C3 ADC's calibrated band)
    const float mv95 = t.millivoltsFromCelsius(95.0f);
    TEST_ASSERT_TRUE(mv95 > 1700.0f && mv95 < 1950.0f);
}

static void test_rail_faults() {
    Thermistor t;
    TEST_ASSERT_EQUAL(static_cast<int>(RailFault::Open),
                      static_cast<int>(t.railFault(10.0f)));
    TEST_ASSERT_EQUAL(static_cast<int>(RailFault::Short),
                      static_cast<int>(t.railFault(3250.0f)));
    TEST_ASSERT_EQUAL(static_cast<int>(RailFault::None),
                      static_cast<int>(t.railFault(300.0f)));
    // Whole operating band 5..105 C stays clear of both rails
    for (float c = 5.0f; c <= 105.0f; c += 5.0f) {
        TEST_ASSERT_EQUAL(static_cast<int>(RailFault::None),
                          static_cast<int>(t.railFault(t.millivoltsFromCelsius(c))));
    }
}

static void test_calibration_offset() {
    NtcConfig cfg;
    cfg.offsetC = 1.5f;
    Thermistor t(cfg);
    Thermistor ref;
    const float mv = ref.millivoltsFromCelsius(93.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 94.5f, t.celsiusFromMillivolts(mv));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip);
    RUN_TEST(test_expected_divider_points);
    RUN_TEST(test_rail_faults);
    RUN_TEST(test_calibration_offset);
    return UNITY_END();
}
