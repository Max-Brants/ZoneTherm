// ZoneRegistry owns all shared zone state behind one mutex and hands out
// value snapshots. These tests cover the two invariants that matter to
// correctness rather than to concurrency: the sparse-delta merge and the
// dirty-mask handshake that drives prompt MQTT publishes.
//
// Note on bounds: ZoneRegistry indexes std::array with operator[] and does not
// range-check. Every caller validates the zone index upstream (ApiRoutes,
// MqttService, OtEngine), so there is deliberately no out-of-range test here -
// it would be undefined behavior, not a defended path.

#include <unity.h>

#include "domain/ZoneRegistry.h"

namespace {

OtResponder::Delta roomTempDelta(float value) {
    OtResponder::Delta d;
    d.setRoomTemp = true;
    d.roomTemp = value;
    return d;
}

OtResponder::Delta setpointDelta(float value) {
    OtResponder::Delta d;
    d.setSetpoint = true;
    d.setpoint = value;
    return d;
}

}  // namespace

// ---- delta semantics -------------------------------------------------------

void test_delta_is_empty_until_a_flag_is_set() {
    OtResponder::Delta d;
    TEST_ASSERT_TRUE(d.empty());
    d.setModulation = true;
    TEST_ASSERT_FALSE(d.empty());
}

void test_apply_delta_writes_only_the_flagged_field() {
    // This is the whole reason Delta is sparse rather than a full ZoneState
    // copy: a concurrent command write between snapshot and apply must not be
    // clobbered by stale fields.
    ZoneRegistry zones;
    zones.setOverrideSetpoint(0, 23.0f);
    zones.applyDelta(0, roomTempDelta(21.5f));

    const ZoneSnapshot z = zones.snapshot(0);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.5f, z.roomTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.0f, z.overrideSetpoint);  // untouched
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, z.setpoint);          // still default
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, z.boilerTemp);
}

void test_apply_empty_delta_changes_nothing() {
    ZoneRegistry zones;
    zones.applyDelta(0, roomTempDelta(21.5f));
    zones.consumeDirtyMask();

    zones.applyDelta(0, OtResponder::Delta{});
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.5f, zones.snapshot(0).roomTemp);
    TEST_ASSERT_EQUAL_UINT8(0, zones.consumeDirtyMask());
}

void test_apply_delta_touches_only_the_named_zone() {
    ZoneRegistry zones;
    zones.applyDelta(3, roomTempDelta(21.5f));
    for (int i = 0; i < kNumZones; i++) {
        const float expected = (i == 3) ? 21.5f : 0.0f;
        TEST_ASSERT_FLOAT_WITHIN(0.01f, expected, zones.snapshot(i).roomTemp);
    }
}

void test_clear_override_zeroes_the_pending_push() {
    ZoneRegistry zones;
    zones.setOverrideSetpoint(0, 23.0f);

    OtResponder::Delta d;
    d.clearOverride = true;
    zones.applyDelta(0, d);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, zones.snapshot(0).overrideSetpoint);
}

void test_telemetry_fields_each_land_in_their_own_slot() {
    ZoneRegistry zones;
    OtResponder::Delta d;
    d.setBoilerTemp = true;    d.boilerTemp = 55.0f;
    d.setOutsideTemp = true;   d.outsideTemp = 8.5f;
    d.setModulation = true;    d.modulation = 42.0f;
    d.setCoolingControl = true; d.coolingControl = 80.0f;
    zones.applyDelta(0, d);

    const ZoneSnapshot z = zones.snapshot(0);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 55.0f, z.boilerTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.5f, z.outsideTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.0f, z.modulation);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, z.coolingControl);
}

// ---- dirty mask ------------------------------------------------------------

void test_setpoint_change_marks_the_zone_dirty() {
    // The dial on the wall was turned - publish promptly instead of waiting
    // for the 30 s cadence.
    ZoneRegistry zones;
    zones.applyDelta(2, setpointDelta(21.0f));
    TEST_ASSERT_EQUAL_UINT8(1u << 2, zones.consumeDirtyMask());
}

void test_unchanged_setpoint_does_not_mark_dirty() {
    // Thermostats re-send TrSet roughly every second; republishing on each
    // one would flood the broker.
    ZoneRegistry zones;
    zones.applyDelta(2, setpointDelta(21.0f));
    zones.consumeDirtyMask();

    zones.applyDelta(2, setpointDelta(21.0f));
    TEST_ASSERT_EQUAL_UINT8(0, zones.consumeDirtyMask());
}

void test_consume_dirty_mask_clears_it() {
    ZoneRegistry zones;
    zones.markDirty(0);
    zones.markDirty(6);
    TEST_ASSERT_EQUAL_UINT8((1u << 0) | (1u << 6), zones.consumeDirtyMask());
    TEST_ASSERT_EQUAL_UINT8(0, zones.consumeDirtyMask());
}

void test_dirty_bits_accumulate_between_consumes() {
    ZoneRegistry zones;
    zones.markDirty(1);
    zones.applyDelta(4, setpointDelta(19.0f));
    zones.setOverrideSetpoint(5, 22.0f);
    TEST_ASSERT_EQUAL_UINT8((1u << 1) | (1u << 4) | (1u << 5),
                            zones.consumeDirtyMask());
}

// ---- setpoint override clamp -----------------------------------------------

void test_override_accepts_the_inclusive_range() {
    ZoneRegistry zones;
    zones.setOverrideSetpoint(0, 5.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, zones.snapshot(0).overrideSetpoint);
    zones.setOverrideSetpoint(0, 30.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, zones.snapshot(0).overrideSetpoint);
}

void test_override_ignores_values_outside_the_range() {
    // Out-of-range commands are dropped, not clamped - the previous override
    // must survive an absurd MQTT payload.
    ZoneRegistry zones;
    zones.setOverrideSetpoint(0, 21.0f);
    zones.setOverrideSetpoint(0, 4.9f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.0f, zones.snapshot(0).overrideSetpoint);
    zones.setOverrideSetpoint(0, 30.1f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.0f, zones.snapshot(0).overrideSetpoint);
}

void test_rejected_override_does_not_mark_dirty() {
    ZoneRegistry zones;
    zones.setOverrideSetpoint(0, 99.0f);
    TEST_ASSERT_EQUAL_UINT8(0, zones.consumeDirtyMask());
}

// ---- diagnostics counters --------------------------------------------------

void test_note_request_counts_and_stamps() {
    ZoneRegistry zones;
    zones.noteRequest(0, 1234);
    zones.noteRequest(0, 5678);

    const ZoneSnapshot z = zones.snapshot(0);
    TEST_ASSERT_EQUAL_UINT32(2, z.totalRequests);
    TEST_ASSERT_EQUAL_UINT32(0, z.failedRequests);
    TEST_ASSERT_EQUAL_UINT32(5678, z.lastRequestMs);
}

void test_failed_request_does_not_stamp_last_seen() {
    // A failed decode still means the thermostat is talking, but only
    // noteRequest advances lastRequestMs - so a channel producing nothing but
    // garbage reads as "Inactive" in the UI.
    ZoneRegistry zones;
    zones.noteRequest(0, 1000);
    zones.noteFailedRequest(0);

    const ZoneSnapshot z = zones.snapshot(0);
    TEST_ASSERT_EQUAL_UINT32(1, z.totalRequests);
    TEST_ASSERT_EQUAL_UINT32(1, z.failedRequests);
    TEST_ASSERT_EQUAL_UINT32(1000, z.lastRequestMs);
}

void test_reset_diagnostics_clears_counters_and_fault() {
    ZoneRegistry zones;
    zones.noteRequest(0, 1000);
    zones.noteFailedRequest(0);
    zones.resetDiagnostics(0);

    const ZoneSnapshot z = zones.snapshot(0);
    TEST_ASSERT_EQUAL_UINT32(0, z.totalRequests);
    TEST_ASSERT_EQUAL_UINT32(0, z.failedRequests);
    TEST_ASSERT_FALSE(z.fault);
    TEST_ASSERT_EQUAL_UINT8(0, z.errorCode);
}

void test_reset_diagnostics_keeps_climate_values() {
    // Clearing counters must not throw away the room temperature - that would
    // shut the valve via the roomTemp <= 0 fail-safe.
    ZoneRegistry zones;
    zones.applyDelta(0, roomTempDelta(21.5f));
    zones.resetDiagnostics(0);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.5f, zones.snapshot(0).roomTemp);
}

// ---- snapshots -------------------------------------------------------------

void test_snapshot_is_a_copy_not_a_reference() {
    ZoneRegistry zones;
    ZoneSnapshot before = zones.snapshot(0);
    zones.applyDelta(0, roomTempDelta(21.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, before.roomTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.5f, zones.snapshot(0).roomTemp);
}

void test_snapshot_all_returns_every_zone() {
    ZoneRegistry zones;
    for (int i = 0; i < kNumZones; i++) {
        zones.applyDelta(i, roomTempDelta(static_cast<float>(18 + i)));
    }
    const auto all = zones.snapshotAll();
    TEST_ASSERT_EQUAL_INT(kNumZones, static_cast<int>(all.size()));
    for (int i = 0; i < kNumZones; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.01f, static_cast<float>(18 + i), all[i].roomTemp);
    }
}

void test_fresh_zones_start_closed_and_unreported() {
    // The defaults ClimateLogic's fail-safe depends on.
    ZoneRegistry zones;
    const ZoneSnapshot z = zones.snapshot(0);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, z.roomTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, z.setpoint);
    TEST_ASSERT_EQUAL_UINT32(0, z.lastRequestMs);
}

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_delta_is_empty_until_a_flag_is_set);
    RUN_TEST(test_apply_delta_writes_only_the_flagged_field);
    RUN_TEST(test_apply_empty_delta_changes_nothing);
    RUN_TEST(test_apply_delta_touches_only_the_named_zone);
    RUN_TEST(test_clear_override_zeroes_the_pending_push);
    RUN_TEST(test_telemetry_fields_each_land_in_their_own_slot);
    RUN_TEST(test_setpoint_change_marks_the_zone_dirty);
    RUN_TEST(test_unchanged_setpoint_does_not_mark_dirty);
    RUN_TEST(test_consume_dirty_mask_clears_it);
    RUN_TEST(test_dirty_bits_accumulate_between_consumes);
    RUN_TEST(test_override_accepts_the_inclusive_range);
    RUN_TEST(test_override_ignores_values_outside_the_range);
    RUN_TEST(test_rejected_override_does_not_mark_dirty);
    RUN_TEST(test_note_request_counts_and_stamps);
    RUN_TEST(test_failed_request_does_not_stamp_last_seen);
    RUN_TEST(test_reset_diagnostics_clears_counters_and_fault);
    RUN_TEST(test_reset_diagnostics_keeps_climate_values);
    RUN_TEST(test_snapshot_is_a_copy_not_a_reference);
    RUN_TEST(test_snapshot_all_returns_every_zone);
    RUN_TEST(test_fresh_zones_start_closed_and_unreported);
    return UNITY_END();
}
