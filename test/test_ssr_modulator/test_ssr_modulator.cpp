#include <unity.h>

#include <cmath>
#include <vector>

#include "SsrModulator.h"

using namespace anita;

void setUp() {}
void tearDown() {}

static std::vector<bool> pattern(SsrModulator& m, int n) {
    std::vector<bool> v;
    v.reserve(n);
    for (int i = 0; i < n; ++i) v.push_back(m.tick());
    return v;
}

static void test_off_and_full() {
    SsrModulator m;
    m.setDuty(0.0f);
    for (bool b : pattern(m, 100)) TEST_ASSERT_FALSE(b);
    m.setDuty(1.0f);
    for (bool b : pattern(m, 100)) TEST_ASSERT_TRUE(b);
}

static void test_half_is_strict_alternation() {
    SsrModulator m;
    m.setDuty(0.5f);
    auto v = pattern(m, 100);
    for (size_t i = 1; i < v.size(); ++i) TEST_ASSERT_TRUE(v[i] != v[i - 1]);
}

static void test_low_duty_is_cot_pfm() {
    // At 10% duty the modulator must emit isolated single half-waves with 9
    // off-ticks between them — constant on-time, variable frequency.
    SsrModulator m;
    m.setDuty(0.1f);
    auto v = pattern(m, 500);
    int fired = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i]) {
            ++fired;
            if (i + 1 < v.size()) TEST_ASSERT_FALSE(v[i + 1]);  // never adjacent
        }
    }
    TEST_ASSERT_INT_WITHIN(1, 50, fired);
}

static void test_long_run_accuracy() {
    for (float d : {0.03f, 0.13f, 0.37f, 0.5f, 0.72f, 0.91f}) {
        SsrModulator m;
        m.setDuty(d);
        int fired = 0;
        const int n = 20000;
        for (int i = 0; i < n; ++i) fired += m.tick() ? 1 : 0;
        TEST_ASSERT_FLOAT_WITHIN(0.002f, d, static_cast<float>(fired) / n);
    }
}

static void test_actual_duty_feedback() {
    SsrModulator m;
    m.setDuty(0.25f);
    for (int i = 0; i < 50; ++i) m.tick();
    TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.25f, m.consumeActualDuty());
    // Window resets after consumption
    m.setDuty(1.0f);
    for (int i = 0; i < 10; ++i) m.tick();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, m.consumeActualDuty());
}

static void test_paired_full_waves() {
    // Pairing mode: pulses come as two consecutive half-waves (20 ms quantum,
    // mains-symmetric) while long-run duty stays accurate.
    SsrModulator m(true);
    m.setDuty(0.2f);
    auto v = pattern(m, 1000);
    int fired = 0;
    size_t i = 0;
    while (i + 1 < v.size()) {  // a pair split by the window end is fine
        if (v[i]) {
            TEST_ASSERT_TRUE_MESSAGE(v[i + 1], "pulse not paired");
            fired += 2;
            i += 2;
            if (i + 1 < v.size()) TEST_ASSERT_FALSE(v[i]);
        } else {
            ++i;
        }
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.2f, static_cast<float>(fired) / v.size());
}

static void test_charge_conserved_across_async_duty_changes() {
    // The ADRC updates the duty faster than the PFM completes a pulse
    // interval at low duty. The accumulator must carry charge across those
    // asynchronous command changes: total fired half-waves == integral of
    // the commanded duty, +-1 quantum, with no loss at command boundaries.
    SsrModulator m;
    const float duties[] = {0.13f, 0.02f, 0.31f, 0.05f, 0.5f, 0.01f, 0.27f};
    float commandedIntegral = 0.0f;
    int fired = 0;
    int di = 0;
    for (int tick = 0; tick < 7000; ++tick) {
        if (tick % 7 == 0) {  // change command mid-"cycle", deliberately
            m.setDuty(duties[di % 7]);
            ++di;
        }
        commandedIntegral += m.duty();
        fired += m.tick() ? 1 : 0;
    }
    TEST_ASSERT_FLOAT_WITHIN(1.0f, commandedIntegral,
                             static_cast<float>(fired));
}

static void test_per_window_error_bounded() {
    // At 2% duty one pulse fires per 50-tick control window at most: the
    // delivered-vs-commanded error within ANY single window must stay within
    // one half-wave (the quantization bound documented in docs/adrc.md).
    SsrModulator m;
    m.setDuty(0.02f);
    for (int window = 0; window < 200; ++window) {
        int fired = 0;
        for (int i = 0; i < 50; ++i) fired += m.tick() ? 1 : 0;
        const float delivered = fired / 50.0f;
        TEST_ASSERT_TRUE(std::fabs(delivered - 0.02f) <= 1.0f / 50.0f + 1e-6f);
    }
}

static void test_duty_zero_kills_stored_charge() {
    SsrModulator m;
    m.setDuty(0.9f);
    for (int i = 0; i < 3; ++i) m.tick();
    m.setDuty(0.0f);
    for (bool b : pattern(m, 50)) TEST_ASSERT_FALSE(b);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_off_and_full);
    RUN_TEST(test_half_is_strict_alternation);
    RUN_TEST(test_low_duty_is_cot_pfm);
    RUN_TEST(test_long_run_accuracy);
    RUN_TEST(test_actual_duty_feedback);
    RUN_TEST(test_paired_full_waves);
    RUN_TEST(test_charge_conserved_across_async_duty_changes);
    RUN_TEST(test_per_window_error_bounded);
    RUN_TEST(test_duty_zero_kills_stored_charge);
    return UNITY_END();
}
