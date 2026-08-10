// Valve sets: which valves a thermostat drives, and which of them end up open.
// ClimateLogic decides for one room; this decides for the manifold. The cases
// that matter are the ones the old 1:1 mapping could not express at all - a
// thermostat owning three valves, or none - plus the upgrade guard that says a
// default install still behaves exactly like the firmware it replaced.

#include <unity.h>

#include <initializer_list>

#include "domain/ValvePlan.h"

namespace {

constexpr float kHysteresis = 0.30f;  // the shipped default, 0.15 K either side

ZoneSnapshot zoneAt(float roomTemp, float setpoint = 20.0f) {
    ZoneSnapshot z;
    z.roomTemp = roomTemp;
    z.setpoint = setpoint;
    return z;
}

// Every zone parked well inside its band, so a test only has to move the one
// zone it cares about.
ValvePlan::Snapshots allSatisfied() {
    ValvePlan::Snapshots s{};
    for (int i = 0; i < kNumZones; i++) s[i] = zoneAt(20.0f);
    return s;
}

// The legacy mapping: zone i drives valve i, every zone enabled.
ValvePlan::Bindings oneToOne() {
    ValvePlan::Bindings b{};
    for (int i = 0; i < kNumZones; i++) {
        b[i].enabled = true;
        b[i].valveMask = static_cast<uint8_t>(1u << i);
    }
    return b;
}

uint8_t valves(std::initializer_list<int> numbers) {
    uint8_t mask = 0;
    for (int n : numbers) mask = static_cast<uint8_t>(mask | (1u << (n - 1)));
    return mask;
}

uint8_t solve(const ValvePlan::Snapshots& s, const ValvePlan::Bindings& b,
              ValvePlan::Demand& demand, Season mode = Season::Heating) {
    return ValvePlan::solve(s, b, mode, kHysteresis, demand);
}

}  // namespace

// ---- the upgrade guard -----------------------------------------------------

void test_default_bindings_reproduce_the_legacy_mapping() {
    // An install upgraded from a pre-valve-set firmware comes up with these
    // bindings. If this test ever fails, somebody's house rewired itself.
    ValvePlan::Snapshots s = allSatisfied();
    s[2] = zoneAt(18.0f);  // only zone 3 is cold
    ValvePlan::Demand demand{};

    TEST_ASSERT_EQUAL_HEX8(valves({3}), solve(s, oneToOne(), demand));
}

// ---- one thermostat, several valves ----------------------------------------

void test_one_thermostat_opens_all_its_valves() {
    ValvePlan::Snapshots s = allSatisfied();
    s[0] = zoneAt(18.0f);
    ValvePlan::Bindings b{};
    b[0] = {true, valves({1, 2, 3})};
    ValvePlan::Demand demand{};

    TEST_ASSERT_EQUAL_HEX8(valves({1, 2, 3}), solve(s, b, demand));
}

void test_a_valve_set_closes_as_one() {
    ValvePlan::Bindings b{};
    b[0] = {true, valves({1, 2, 3})};
    ValvePlan::Demand demand{};

    ValvePlan::Snapshots cold = allSatisfied();
    cold[0] = zoneAt(18.0f);
    TEST_ASSERT_EQUAL_HEX8(valves({1, 2, 3}), solve(cold, b, demand));

    ValvePlan::Snapshots hot = allSatisfied();
    hot[0] = zoneAt(22.0f);
    TEST_ASSERT_EQUAL_HEX8(0x00, solve(hot, b, demand));
}

void test_disjoint_thermostats_do_not_disturb_each_other() {
    ValvePlan::Snapshots s = allSatisfied();
    s[0] = zoneAt(18.0f);   // calling
    s[1] = zoneAt(22.0f);   // satisfied
    ValvePlan::Bindings b{};
    b[0] = {true, valves({1, 2, 3})};
    b[1] = {true, valves({4, 5})};
    ValvePlan::Demand demand{};

    TEST_ASSERT_EQUAL_HEX8(valves({1, 2, 3}), solve(s, b, demand));
}

// ---- the edges the 1:1 mapping could not express ---------------------------

void test_zone_with_no_valves_contributes_nothing_but_still_latches() {
    // It keeps answering its thermostat and keeps tracking the band; it just
    // has nothing to open. Assigning it a valve later must not produce a
    // spurious transient, which is why the latch still has to run.
    ValvePlan::Snapshots s = allSatisfied();
    s[0] = zoneAt(18.0f);
    ValvePlan::Bindings b{};
    b[0] = {true, 0};
    ValvePlan::Demand demand{};

    TEST_ASSERT_EQUAL_HEX8(0x00, solve(s, b, demand));
    TEST_ASSERT_TRUE(demand[0]);
}

void test_unassigned_valve_never_opens() {
    ValvePlan::Snapshots s{};
    for (int i = 0; i < kNumZones; i++) s[i] = zoneAt(10.0f);  // everyone cold
    ValvePlan::Bindings b = oneToOne();
    b[6].valveMask = 0;  // nobody claims V7
    ValvePlan::Demand demand{};

    const uint8_t open = solve(s, b, demand);
    TEST_ASSERT_EQUAL_HEX8(valves({1, 2, 3, 4, 5, 6}), open);
    TEST_ASSERT_FALSE(ValvePlan::anyOpen(valves({7}), open));
}

// ---- the latch -------------------------------------------------------------

void test_latch_holds_the_set_open_inside_the_band() {
    // This is what the expander read-back used to do, and could not keep doing
    // once a zone owns more than one valve.
    ValvePlan::Bindings b{};
    b[0] = {true, valves({1, 2, 3})};
    ValvePlan::Demand demand{};

    ValvePlan::Snapshots cold = allSatisfied();
    cold[0] = zoneAt(19.0f);
    TEST_ASSERT_EQUAL_HEX8(valves({1, 2, 3}), solve(cold, b, demand));

    // Warmed into the band: holds, rather than chattering.
    TEST_ASSERT_EQUAL_HEX8(valves({1, 2, 3}), solve(allSatisfied(), b, demand));
    TEST_ASSERT_EQUAL_HEX8(valves({1, 2, 3}), solve(allSatisfied(), b, demand));
}

void test_latch_holds_for_a_zone_that_owns_no_valves() {
    // The case the old read-back got wrong: with no bit on the expander to read
    // back, currentlyOpen was always false and the zone could never hold.
    ValvePlan::Bindings b{};
    b[0] = {true, 0};
    ValvePlan::Demand demand{};

    ValvePlan::Snapshots cold = allSatisfied();
    cold[0] = zoneAt(19.0f);
    solve(cold, b, demand);
    TEST_ASSERT_TRUE(demand[0]);

    solve(allSatisfied(), b, demand);  // in-band
    TEST_ASSERT_TRUE(demand[0]);
}

void test_disabling_a_zone_drops_its_whole_set() {
    ValvePlan::Snapshots cold = allSatisfied();
    cold[0] = zoneAt(18.0f);
    ValvePlan::Bindings b{};
    b[0] = {true, valves({1, 2, 3})};
    ValvePlan::Demand demand{};

    TEST_ASSERT_EQUAL_HEX8(valves({1, 2, 3}), solve(cold, b, demand));

    b[0].enabled = false;
    TEST_ASSERT_EQUAL_HEX8(0x00, solve(cold, b, demand));
    TEST_ASSERT_FALSE(demand[0]);
}

void test_overlapping_masks_are_ored_never_dropped() {
    // Exclusivity is enforced at the API boundary, not here. If a store is ever
    // hand-edited into an overlap, the safe reading is "any owner may open it".
    ValvePlan::Snapshots s = allSatisfied();
    s[0] = zoneAt(18.0f);   // calling
    s[1] = zoneAt(22.0f);   // satisfied, but shares V1
    ValvePlan::Bindings b{};
    b[0] = {true, valves({1})};
    b[1] = {true, valves({1, 2})};
    ValvePlan::Demand demand{};

    TEST_ASSERT_EQUAL_HEX8(valves({1}), solve(s, b, demand));
}

// ---- exclusivity helpers ---------------------------------------------------

void test_exclusive_accepts_disjoint_and_empty_sets() {
    ValvePlan::Masks masks{};
    TEST_ASSERT_TRUE(ValvePlan::exclusive(masks));  // nobody owns anything

    masks[0] = valves({1, 2, 3});
    masks[1] = valves({4, 5});
    masks[2] = 0;
    TEST_ASSERT_TRUE(ValvePlan::exclusive(masks));
}

void test_exclusive_rejects_a_shared_valve() {
    ValvePlan::Masks masks{};
    masks[0] = valves({1, 2});
    masks[3] = valves({2});
    TEST_ASSERT_FALSE(ValvePlan::exclusive(masks));
}

void test_the_default_layout_is_exclusive() {
    ValvePlan::Masks masks{};
    for (int i = 0; i < kNumZones; i++) masks[i] = static_cast<uint8_t>(1u << i);
    TEST_ASSERT_TRUE(ValvePlan::exclusive(masks));
    TEST_ASSERT_EQUAL_HEX8(0x00, ValvePlan::unassigned(masks));
}

void test_drop_overlaps_lets_the_lower_zone_win() {
    ValvePlan::Masks masks{};
    masks[0] = valves({1, 2});
    masks[1] = valves({2, 3});

    TEST_ASSERT_TRUE(ValvePlan::dropOverlaps(masks));
    TEST_ASSERT_EQUAL_HEX8(valves({1, 2}), masks[0]);
    TEST_ASSERT_EQUAL_HEX8(valves({3}), masks[1]);
    TEST_ASSERT_TRUE(ValvePlan::exclusive(masks));
}

void test_drop_overlaps_leaves_a_clean_config_alone() {
    ValvePlan::Masks masks{};
    masks[0] = valves({1, 2});
    masks[1] = valves({3});
    TEST_ASSERT_FALSE(ValvePlan::dropOverlaps(masks));
    TEST_ASSERT_EQUAL_HEX8(valves({1, 2}), masks[0]);
    TEST_ASSERT_EQUAL_HEX8(valves({3}), masks[1]);
}

void test_unassigned_reports_only_the_free_valves() {
    ValvePlan::Masks masks{};
    TEST_ASSERT_EQUAL_HEX8(kAllValvesMask, ValvePlan::unassigned(masks));

    masks[0] = valves({1, 2, 3});
    masks[1] = valves({5});
    TEST_ASSERT_EQUAL_HEX8(valves({4, 6, 7}), ValvePlan::unassigned(masks));
}

void test_any_open_needs_an_owned_valve_to_be_open() {
    TEST_ASSERT_TRUE(ValvePlan::anyOpen(valves({1, 2, 3}), valves({3})));
    TEST_ASSERT_FALSE(ValvePlan::anyOpen(valves({1, 2, 3}), valves({4})));
    TEST_ASSERT_FALSE(ValvePlan::anyOpen(0, kAllValvesMask));  // owns nothing
}

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_default_bindings_reproduce_the_legacy_mapping);
    RUN_TEST(test_one_thermostat_opens_all_its_valves);
    RUN_TEST(test_a_valve_set_closes_as_one);
    RUN_TEST(test_disjoint_thermostats_do_not_disturb_each_other);
    RUN_TEST(test_zone_with_no_valves_contributes_nothing_but_still_latches);
    RUN_TEST(test_unassigned_valve_never_opens);
    RUN_TEST(test_latch_holds_the_set_open_inside_the_band);
    RUN_TEST(test_latch_holds_for_a_zone_that_owns_no_valves);
    RUN_TEST(test_disabling_a_zone_drops_its_whole_set);
    RUN_TEST(test_overlapping_masks_are_ored_never_dropped);
    RUN_TEST(test_exclusive_accepts_disjoint_and_empty_sets);
    RUN_TEST(test_exclusive_rejects_a_shared_valve);
    RUN_TEST(test_the_default_layout_is_exclusive);
    RUN_TEST(test_drop_overlaps_lets_the_lower_zone_win);
    RUN_TEST(test_drop_overlaps_leaves_a_clean_config_alone);
    RUN_TEST(test_unassigned_reports_only_the_free_valves);
    RUN_TEST(test_any_open_needs_an_owned_valve_to_be_open);
    return UNITY_END();
}
