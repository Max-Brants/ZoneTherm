// StringUtils is header-only and sits on the HTTP request-parsing path:
// every query parameter and form field in ApiRoutes goes through urlDecode
// and trim before it reaches config or a setpoint.

#include <unity.h>

#include "util/StringUtils.h"

// ---- urlDecode -------------------------------------------------------------

void test_url_decode_passes_plain_text_through() {
    TEST_ASSERT_EQUAL_STRING("Living Room", StringUtils::urlDecode("Living Room").c_str());
    TEST_ASSERT_EQUAL_STRING("", StringUtils::urlDecode("").c_str());
}

void test_url_decode_turns_plus_into_space() {
    // Form bodies are application/x-www-form-urlencoded, where '+' is a space.
    TEST_ASSERT_EQUAL_STRING("Living Room", StringUtils::urlDecode("Living+Room").c_str());
}

void test_url_decode_handles_percent_escapes() {
    TEST_ASSERT_EQUAL_STRING("A", StringUtils::urlDecode("%41").c_str());
    TEST_ASSERT_EQUAL_STRING("a/b", StringUtils::urlDecode("a%2Fb").c_str());
    TEST_ASSERT_EQUAL_STRING("100%", StringUtils::urlDecode("100%25").c_str());
    TEST_ASSERT_EQUAL_STRING("Bad&Room", StringUtils::urlDecode("Bad%26Room").c_str());
}

void test_url_decode_accepts_lowercase_hex() {
    TEST_ASSERT_EQUAL_STRING("a/b", StringUtils::urlDecode("a%2fb").c_str());
}

void test_url_decode_escape_at_end_of_string_still_decodes() {
    // The bound is `i + 2 < size`, which is exactly enough for an escape that
    // ends the string - an off-by-one here would silently mangle the last
    // character of every form field.
    TEST_ASSERT_EQUAL_STRING("room A", StringUtils::urlDecode("room+%41").c_str());
}

void test_url_decode_leaves_truncated_escapes_alone() {
    // Malformed input must not read past the end or drop characters.
    TEST_ASSERT_EQUAL_STRING("a%4", StringUtils::urlDecode("a%4").c_str());
    TEST_ASSERT_EQUAL_STRING("%", StringUtils::urlDecode("%").c_str());
    TEST_ASSERT_EQUAL_STRING("ab%", StringUtils::urlDecode("ab%").c_str());
}

// ---- trim ------------------------------------------------------------------

void test_trim_removes_surrounding_whitespace() {
    TEST_ASSERT_EQUAL_STRING("Kitchen", StringUtils::trim("  Kitchen  ").c_str());
    TEST_ASSERT_EQUAL_STRING("Kitchen", StringUtils::trim("\t\r\nKitchen\n").c_str());
}

void test_trim_keeps_interior_whitespace() {
    TEST_ASSERT_EQUAL_STRING("Living Room", StringUtils::trim(" Living Room ").c_str());
}

void test_trim_collapses_blank_input_to_empty() {
    // ApiRoutes relies on this: a whitespace-only zone name must fail the
    // `name.empty()` check rather than being stored.
    TEST_ASSERT_EQUAL_STRING("", StringUtils::trim("   ").c_str());
    TEST_ASSERT_EQUAL_STRING("", StringUtils::trim("").c_str());
    TEST_ASSERT_EQUAL_STRING("", StringUtils::trim("\t\n").c_str());
}

// ---- numeric parsing -------------------------------------------------------

void test_to_float_parses_setpoint_payloads() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 21.5f, StringUtils::toFloat("21.5"));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -5.0f, StringUtils::toFloat("-5"));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 21.5f, StringUtils::toFloat("  21.5  "));
}

void test_to_float_returns_zero_on_garbage() {
    // These wrap strtof rather than std::stof precisely because the codebase
    // builds with exceptions disabled - a throw here would abort the device.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, StringUtils::toFloat("abc"));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, StringUtils::toFloat(""));
}

void test_to_int_parses_and_fails_soft() {
    TEST_ASSERT_EQUAL_INT(1883, StringUtils::toInt("1883"));
    TEST_ASSERT_EQUAL_INT(42, StringUtils::toInt("42abc"));
    TEST_ASSERT_EQUAL_INT(0, StringUtils::toInt("abc"));
    TEST_ASSERT_EQUAL_INT(0, StringUtils::toInt(""));
}

// ---- formatting ------------------------------------------------------------

void test_to_string_float_defaults_to_two_decimals() {
    // MQTT and JSON payloads were tuned for this formatting.
    TEST_ASSERT_EQUAL_STRING("21.50", StringUtils::toString(21.5f).c_str());
    TEST_ASSERT_EQUAL_STRING("21.5", StringUtils::toString(21.5f, 1).c_str());
    // Deliberately not asserting an exact-half at 0 decimals: printf rounds
    // half-to-even, which is a libc detail, not this function's contract.
    TEST_ASSERT_EQUAL_STRING("21.4", StringUtils::toString(21.44f, 1).c_str());
}

void test_to_string_integers() {
    TEST_ASSERT_EQUAL_STRING("7", StringUtils::toString(7).c_str());
    TEST_ASSERT_EQUAL_STRING("0", StringUtils::toString(0).c_str());
}

// ---- prefix / case helpers -------------------------------------------------

void test_starts_and_ends_with() {
    TEST_ASSERT_TRUE(StringUtils::startsWith("zonetherm/ab12", "zonetherm/"));
    TEST_ASSERT_FALSE(StringUtils::startsWith("zone", "zonetherm/"));
    TEST_ASSERT_TRUE(StringUtils::endsWith("thermostat/1/command", "/command"));
    TEST_ASSERT_FALSE(StringUtils::endsWith("cmd", "/command"));
}

void test_case_conversion() {
    TEST_ASSERT_EQUAL_STRING("HEATING", StringUtils::toUpper("Heating").c_str());
    TEST_ASSERT_EQUAL_STRING("heating", StringUtils::toLower("HEATING").c_str());
}

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_url_decode_passes_plain_text_through);
    RUN_TEST(test_url_decode_turns_plus_into_space);
    RUN_TEST(test_url_decode_handles_percent_escapes);
    RUN_TEST(test_url_decode_accepts_lowercase_hex);
    RUN_TEST(test_url_decode_escape_at_end_of_string_still_decodes);
    RUN_TEST(test_url_decode_leaves_truncated_escapes_alone);
    RUN_TEST(test_trim_removes_surrounding_whitespace);
    RUN_TEST(test_trim_keeps_interior_whitespace);
    RUN_TEST(test_trim_collapses_blank_input_to_empty);
    RUN_TEST(test_to_float_parses_setpoint_payloads);
    RUN_TEST(test_to_float_returns_zero_on_garbage);
    RUN_TEST(test_to_int_parses_and_fails_soft);
    RUN_TEST(test_to_string_float_defaults_to_two_decimals);
    RUN_TEST(test_to_string_integers);
    RUN_TEST(test_starts_and_ends_with);
    RUN_TEST(test_case_conversion);
    return UNITY_END();
}
