#include <unity.h>

#include <cstring>

#include "CommandParser.h"

using namespace anita;
using Type = Command::Type;
using Param = Command::Param;

void setUp() {}
void tearDown() {}

static void test_empty_and_whitespace() {
    TEST_ASSERT_EQUAL(static_cast<int>(Type::None),
                      static_cast<int>(CommandParser::parse("").type));
    TEST_ASSERT_EQUAL(static_cast<int>(Type::None),
                      static_cast<int>(CommandParser::parse("  \r\n").type));
    TEST_ASSERT_EQUAL(static_cast<int>(Type::None),
                      static_cast<int>(CommandParser::parse(nullptr).type));
}

static void test_id_duty() {
    const Command c = CommandParser::parse("id duty 0.4 180");
    TEST_ASSERT_EQUAL(static_cast<int>(Type::IdDuty), static_cast<int>(c.type));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.4f, c.value);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 180.0f, c.seconds);
}

static void test_id_off_and_stop() {
    const Command off = CommandParser::parse("id off 300\r\n");
    TEST_ASSERT_EQUAL(static_cast<int>(Type::IdOff), static_cast<int>(off.type));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 300.0f, off.seconds);
    TEST_ASSERT_EQUAL(static_cast<int>(Type::IdStop),
                      static_cast<int>(CommandParser::parse("id stop").type));
}

static void test_id_bounds() {
    TEST_ASSERT_EQUAL(static_cast<int>(Type::Invalid),
                      static_cast<int>(CommandParser::parse("id duty 1.5 60").type));
    TEST_ASSERT_EQUAL(static_cast<int>(Type::Invalid),
                      static_cast<int>(CommandParser::parse("id duty 0.4 0").type));
    TEST_ASSERT_EQUAL(static_cast<int>(Type::Invalid),
                      static_cast<int>(CommandParser::parse("id duty 0.4 9999").type));
    TEST_ASSERT_EQUAL(static_cast<int>(Type::Invalid),
                      static_cast<int>(CommandParser::parse("id duty abc 60").type));
    TEST_ASSERT_EQUAL(static_cast<int>(Type::Invalid),
                      static_cast<int>(CommandParser::parse("id").type));
}

static void test_set_params() {
    struct {
        const char* line;
        Param param;
        float value;
    } cases[] = {
        {"set b0 0.6", Param::B0Gain, 0.6f},
        {"set wc 0.05", Param::Wc, 0.05f},
        {"set wo 0.2", Param::Wo, 0.2f},
        {"set pred 15", Param::Pred, 15.0f},
        {"set kboost 2.5", Param::KBoost, 2.5f},
        {"set cap 0.3", Param::Cap, 0.3f},
        {"set setpoint 92.5", Param::Setpoint, 92.5f},
    };
    for (const auto& tc : cases) {
        const Command c = CommandParser::parse(tc.line);
        TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Type::Set),
                                  static_cast<int>(c.type), tc.line);
        TEST_ASSERT_EQUAL(static_cast<int>(tc.param), static_cast<int>(c.param));
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, tc.value, c.value);
    }
}

static void test_set_rejects_out_of_range_and_unknown() {
    TEST_ASSERT_EQUAL(static_cast<int>(Type::Invalid),
                      static_cast<int>(CommandParser::parse("set cap 1.5").type));
    TEST_ASSERT_EQUAL(static_cast<int>(Type::Invalid),
                      static_cast<int>(CommandParser::parse("set setpoint 120").type));
    TEST_ASSERT_EQUAL(static_cast<int>(Type::Invalid),
                      static_cast<int>(CommandParser::parse("set foo 1").type));
    TEST_ASSERT_EQUAL(static_cast<int>(Type::Invalid),
                      static_cast<int>(CommandParser::parse("set b0").type));
}

static void test_mark_and_get() {
    const Command m = CommandParser::parse("mark espresso double shot");
    TEST_ASSERT_EQUAL(static_cast<int>(Type::Mark), static_cast<int>(m.type));
    TEST_ASSERT_EQUAL_STRING("espresso double shot", m.text);
    TEST_ASSERT_EQUAL(static_cast<int>(Type::Invalid),
                      static_cast<int>(CommandParser::parse("mark").type));
    TEST_ASSERT_EQUAL(static_cast<int>(Type::Get),
                      static_cast<int>(CommandParser::parse("get").type));
}

static void test_unknown_command_has_error() {
    const Command c = CommandParser::parse("frobnicate 42");
    TEST_ASSERT_EQUAL(static_cast<int>(Type::Invalid), static_cast<int>(c.type));
    TEST_ASSERT_NOT_NULL(c.error);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_and_whitespace);
    RUN_TEST(test_id_duty);
    RUN_TEST(test_id_off_and_stop);
    RUN_TEST(test_id_bounds);
    RUN_TEST(test_set_params);
    RUN_TEST(test_set_rejects_out_of_range_and_unknown);
    RUN_TEST(test_mark_and_get);
    RUN_TEST(test_unknown_command_has_error);
    return UNITY_END();
}
