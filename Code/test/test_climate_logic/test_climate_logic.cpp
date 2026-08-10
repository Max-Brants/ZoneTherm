// The single valve-decision function. This is the code that decides whether
// heat actually flows into a room, so the boundary cases below are the point
// of the whole suite - a one-character change to a comparison here is a
// change to how the house behaves.

#include <unity.h>

#include "domain/ClimateLogic.h"

namespace {

constexpr float kHysteresis = 0.30f;  // the shipped default, 0.15 K either side
constexpr bool kOpen = true;
constexpr bool kClosed = false;

ZoneSnapshot zoneAt(float roomTemp, float setpoint = 20.0f) {
    ZoneSnapshot z;
    z.roomTemp = roomTemp;
    z.setpoint = setpoint;
    return z;
}

bool wanted(const ZoneSnapshot& z, Season mode, bool currentlyOpen,
            bool enabled = true) {
    return ClimateLogic::valveWanted(z, enabled, mode, kHysteresis, currentlyOpen);
}

}  // namespace

// ---- heating ---------------------------------------------------------------

void test_heating_opens_when_clearly_below_setpoint() {
    TEST_ASSERT_TRUE(wanted(zoneAt(18.0f), Season::Heating, kClosed));
}

void test_heating_closes_when_clearly_above_setpoint() {
    TEST_ASSERT_FALSE(wanted(zoneAt(22.0f), Season::Heating, kOpen));
}

void test_heating_holds_inside_the_band() {
    // Inside the band the answer is "whatever it already was" - that is what
    // stops the valve chattering around the setpoint.
    const ZoneSnapshot z = zoneAt(20.0f);
    TEST_ASSERT_TRUE(wanted(z, Season::Heating, kOpen));
    TEST_ASSERT_FALSE(wanted(z, Season::Heating, kClosed));
}

void test_heating_band_edges_are_exclusive() {
    // The comparisons are strict, so a room sitting exactly on an edge holds
    // rather than switching.
    const ZoneSnapshot low = zoneAt(20.0f - kHysteresis / 2.0f);   // 19.85
    TEST_ASSERT_FALSE(wanted(low, Season::Heating, kClosed));
    TEST_ASSERT_TRUE(wanted(low, Season::Heating, kOpen));

    const ZoneSnapshot high = zoneAt(20.0f + kHysteresis / 2.0f);  // 20.15
    TEST_ASSERT_TRUE(wanted(high, Season::Heating, kOpen));
    TEST_ASSERT_FALSE(wanted(high, Season::Heating, kClosed));
}

void test_heating_full_cycle_across_the_band() {
    bool open = false;
    open = wanted(zoneAt(19.0f), Season::Heating, open);
    TEST_ASSERT_TRUE(open);                                    // cold - opens
    open = wanted(zoneAt(20.0f), Season::Heating, open);
    TEST_ASSERT_TRUE(open);                                    // warming - holds
    open = wanted(zoneAt(20.5f), Season::Heating, open);
    TEST_ASSERT_FALSE(open);                                   // satisfied - closes
    open = wanted(zoneAt(20.0f), Season::Heating, open);
    TEST_ASSERT_FALSE(open);                                   // cooling - holds
    open = wanted(zoneAt(19.5f), Season::Heating, open);
    TEST_ASSERT_TRUE(open);                                    // cold again - opens
}

// ---- cooling (mirrored) ----------------------------------------------------

void test_cooling_opens_when_clearly_above_setpoint() {
    TEST_ASSERT_TRUE(wanted(zoneAt(24.0f, 22.0f), Season::Cooling, kClosed));
}

void test_cooling_closes_when_clearly_below_setpoint() {
    TEST_ASSERT_FALSE(wanted(zoneAt(20.0f, 22.0f), Season::Cooling, kOpen));
}

void test_cooling_holds_inside_the_band() {
    const ZoneSnapshot z = zoneAt(22.0f, 22.0f);
    TEST_ASSERT_TRUE(wanted(z, Season::Cooling, kOpen));
    TEST_ASSERT_FALSE(wanted(z, Season::Cooling, kClosed));
}

void test_cooling_is_the_mirror_of_heating() {
    // Same room, same setpoint, opposite season - opposite answer.
    const ZoneSnapshot cold = zoneAt(18.0f);
    TEST_ASSERT_TRUE(wanted(cold, Season::Heating, kClosed));
    TEST_ASSERT_FALSE(wanted(cold, Season::Cooling, kOpen));

    const ZoneSnapshot hot = zoneAt(22.0f);
    TEST_ASSERT_FALSE(wanted(hot, Season::Heating, kOpen));
    TEST_ASSERT_TRUE(wanted(hot, Season::Cooling, kClosed));
}

// ---- fail-safe paths -------------------------------------------------------

void test_disabled_zone_never_opens() {
    // Not "holds" - closes, whatever the temperature and whatever the valve
    // was doing a second ago.
    TEST_ASSERT_FALSE(wanted(zoneAt(5.0f), Season::Heating, kOpen, false));
    TEST_ASSERT_FALSE(wanted(zoneAt(30.0f), Season::Cooling, kOpen, false));
}

void test_zone_with_no_reading_never_opens() {
    // roomTemp == 0 means the thermostat has never reported. The README's
    // "there is no fail-open" guarantee lives on this line.
    TEST_ASSERT_FALSE(wanted(zoneAt(0.0f), Season::Heating, kClosed));
    TEST_ASSERT_FALSE(wanted(zoneAt(0.0f), Season::Heating, kOpen));
    TEST_ASSERT_FALSE(wanted(zoneAt(-1.0f), Season::Cooling, kOpen));
}

// Pinned, not endorsed: staleness is not a control input. ZoneState carries
// lastRequestMs, but valveWanted never sees it - it only reaches the web UI's
// "Active" badge (ZoneJson.cpp). So a thermostat that is unplugged mid-call
// leaves its last roomTemp/setpoint in place and the valve latched open
// indefinitely. Candidate follow-up: pass an age into this function and close
// the valve past a timeout.
void test_stale_zone_holds_its_last_position() {
    ZoneSnapshot z = zoneAt(19.0f);
    z.lastRequestMs = 1;  // one millisecond after boot, never updated since
    TEST_ASSERT_TRUE(wanted(z, Season::Heating, kOpen));
}

// ---- hysteresis width ------------------------------------------------------

void test_zero_hysteresis_still_decides() {
    const ZoneSnapshot z = zoneAt(19.9f);
    TEST_ASSERT_TRUE(ClimateLogic::valveWanted(z, true, Season::Heating, 0.0f, kClosed));
    TEST_ASSERT_FALSE(ClimateLogic::valveWanted(zoneAt(20.1f), true, Season::Heating,
                                                0.0f, kOpen));
}

void test_wider_hysteresis_widens_the_hold_region() {
    const ZoneSnapshot z = zoneAt(19.6f);  // 0.4 K below setpoint
    // Inside a 1.0 K band (+/- 0.5) this holds; inside a 0.3 K band it opens.
    TEST_ASSERT_FALSE(ClimateLogic::valveWanted(z, true, Season::Heating, 1.0f, kClosed));
    TEST_ASSERT_TRUE(ClimateLogic::valveWanted(z, true, Season::Heating, 0.3f, kClosed));
}

// ---- action string ---------------------------------------------------------

void test_action_string_matrix() {
    TEST_ASSERT_EQUAL_STRING("off", ClimateLogic::actionString(false, Season::Heating, false));
    TEST_ASSERT_EQUAL_STRING("off", ClimateLogic::actionString(false, Season::Cooling, true));
    TEST_ASSERT_EQUAL_STRING("idle", ClimateLogic::actionString(true, Season::Heating, false));
    TEST_ASSERT_EQUAL_STRING("idle", ClimateLogic::actionString(true, Season::Cooling, false));
    TEST_ASSERT_EQUAL_STRING("heating", ClimateLogic::actionString(true, Season::Heating, true));
    TEST_ASSERT_EQUAL_STRING("cooling", ClimateLogic::actionString(true, Season::Cooling, true));
}

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_heating_opens_when_clearly_below_setpoint);
    RUN_TEST(test_heating_closes_when_clearly_above_setpoint);
    RUN_TEST(test_heating_holds_inside_the_band);
    RUN_TEST(test_heating_band_edges_are_exclusive);
    RUN_TEST(test_heating_full_cycle_across_the_band);
    RUN_TEST(test_cooling_opens_when_clearly_above_setpoint);
    RUN_TEST(test_cooling_closes_when_clearly_below_setpoint);
    RUN_TEST(test_cooling_holds_inside_the_band);
    RUN_TEST(test_cooling_is_the_mirror_of_heating);
    RUN_TEST(test_disabled_zone_never_opens);
    RUN_TEST(test_zone_with_no_reading_never_opens);
    RUN_TEST(test_stale_zone_holds_its_last_position);
    RUN_TEST(test_zero_hysteresis_still_decides);
    RUN_TEST(test_wider_hysteresis_widens_the_hold_region);
    RUN_TEST(test_action_string_matrix);
    return UNITY_END();
}
