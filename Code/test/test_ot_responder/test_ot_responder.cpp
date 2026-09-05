// OpenTherm slave responder tests.
//
// OtResponder::handle is side-effect free by design: request frame + zone
// snapshot in, response frame + sparse delta out. That is what makes this
// suite possible without any hardware, and these tests are the reason to keep
// it that way.

#include <unity.h>

#include "domain/OtResponder.h"

namespace {

// Requests are built with the same helper the slave uses for responses - the
// bit layout and parity rule are identical in both directions.
ot::Frame request(ot::MsgType type, uint8_t id, uint16_t data) {
    return ot::buildResponse(type, id, data);
}

ot::Frame readReq(uint8_t id, uint16_t data = 0) {
    return request(ot::MsgType::ReadData, id, data);
}

ot::Frame writeReq(uint8_t id, uint16_t data) {
    return request(ot::MsgType::WriteData, id, data);
}

ot::Frame writeTemp(uint8_t id, float celsius) {
    return writeReq(id, ot::fromTemperature(celsius));
}

OtResponder::Context ctxFor(bool enabled, Season mode) {
    OtResponder::Context ctx;
    ctx.zoneEnabled = enabled;
    ctx.mode = mode;
    return ctx;
}

int typeOf(ot::Frame f) { return static_cast<int>(ot::msgType(f)); }

}  // namespace

// ---- Status (ID 0) ---------------------------------------------------------

void test_status_echoes_master_flags_in_the_high_byte() {
    const ZoneSnapshot zone;
    const auto r = OtResponder::handle(readReq(ot::DataId::Status, 0xAB00),
                                       zone, ctxFor(true, Season::Heating));
    TEST_ASSERT_TRUE(r.hasResponse);
    TEST_ASSERT_EQUAL_HEX16(0xAB00, ot::dataValue(r.response) & 0xFF00);
}

void test_status_sets_ch_bit_when_heating_and_enabled() {
    const ZoneSnapshot zone;
    const auto r = OtResponder::handle(readReq(ot::DataId::Status), zone,
                                       ctxFor(true, Season::Heating));
    const uint16_t lb = ot::dataValue(r.response) & 0xFF;
    TEST_ASSERT_EQUAL_HEX16(0x02, lb & 0x02);  // CH mode
    TEST_ASSERT_EQUAL_HEX16(0x00, lb & 0x10);  // not cooling
}

void test_status_sets_cooling_bit_when_cooling_and_enabled() {
    const ZoneSnapshot zone;
    const auto r = OtResponder::handle(readReq(ot::DataId::Status), zone,
                                       ctxFor(true, Season::Cooling));
    const uint16_t lb = ot::dataValue(r.response) & 0xFF;
    TEST_ASSERT_EQUAL_HEX16(0x10, lb & 0x10);  // cooling mode
    TEST_ASSERT_EQUAL_HEX16(0x00, lb & 0x02);  // not CH
}

void test_status_clears_mode_bits_when_zone_disabled() {
    ZoneSnapshot zone;
    zone.flameOn = true;  // must not leak out either
    const auto r = OtResponder::handle(readReq(ot::DataId::Status), zone,
                                       ctxFor(false, Season::Heating));
    TEST_ASSERT_EQUAL_HEX16(0x00, ot::dataValue(r.response) & 0xFF);
}

void test_status_reports_fault_even_when_zone_disabled() {
    ZoneSnapshot zone;
    zone.fault = true;
    const auto r = OtResponder::handle(readReq(ot::DataId::Status), zone,
                                       ctxFor(false, Season::Heating));
    TEST_ASSERT_EQUAL_HEX16(0x01, ot::dataValue(r.response) & 0x01);
}

void test_status_reports_flame_when_enabled() {
    ZoneSnapshot zone;
    zone.flameOn = true;
    const auto r = OtResponder::handle(readReq(ot::DataId::Status), zone,
                                       ctxFor(true, Season::Heating));
    TEST_ASSERT_EQUAL_HEX16(0x08, ot::dataValue(r.response) & 0x08);
}

// ---- Identity (IDs 2 and 3) ------------------------------------------------

void test_master_config_is_acked_not_rejected() {
    // ID 2 carries the *master's* config and member ID. The slave must ack it
    // rather than reply UNKNOWN-DATAID, or vendor thermostats downgrade to
    // generic-boiler behavior. The value is echoed, not interpreted.
    const auto r = OtResponder::handle(writeReq(ot::DataId::MConfigMMemberId, 0x0013),
                                       ZoneSnapshot{}, ctxFor(true, Season::Heating));
    TEST_ASSERT_TRUE(r.hasResponse);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ot::MsgType::WriteAck), typeOf(r.response));
    TEST_ASSERT_EQUAL_HEX16(0x0013, ot::dataValue(r.response));
    TEST_ASSERT_TRUE(r.delta.empty());
}

void test_slave_config_reports_member_id_13_and_cooling_support() {
    // Regression guard: member ID 13 is what stops the Honeywell Home units
    // treating this controller as a generic boiler. Changing it changes how
    // every connected thermostat behaves.
    const auto r = OtResponder::handle(readReq(ot::DataId::SConfigSMemberId),
                                       ZoneSnapshot{}, ctxFor(true, Season::Heating));
    const uint16_t value = ot::dataValue(r.response);
    TEST_ASSERT_EQUAL_UINT8(13, value & 0xFF);
    TEST_ASSERT_EQUAL_HEX16(0x04, (value >> 8) & 0x04);  // cooling supported
}

void test_slave_config_flags_do_not_depend_on_season() {
    // Thermostats read ID 3 once at power-up and cache it, so the advertised
    // capability flags must be identical in both seasons.
    const auto heating = OtResponder::handle(readReq(ot::DataId::SConfigSMemberId),
                                             ZoneSnapshot{}, ctxFor(true, Season::Heating));
    const auto cooling = OtResponder::handle(readReq(ot::DataId::SConfigSMemberId),
                                             ZoneSnapshot{}, ctxFor(false, Season::Cooling));
    TEST_ASSERT_EQUAL_HEX32(heating.response, cooling.response);
}

void test_slave_version_reports_a_product_version() {
    const auto r = OtResponder::handle(readReq(ot::DataId::SlaveVersion),
                                       ZoneSnapshot{}, ctxFor(true, Season::Heating));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ot::MsgType::ReadAck), typeOf(r.response));
    TEST_ASSERT_EQUAL_HEX16(0x0101, ot::dataValue(r.response));
}

// ---- Room temperature and setpoint capture ---------------------------------

void test_room_temp_write_is_captured() {
    const auto r = OtResponder::handle(writeTemp(ot::DataId::Tr, 21.5f),
                                       ZoneSnapshot{}, ctxFor(true, Season::Heating));
    TEST_ASSERT_TRUE(r.delta.setRoomTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.5f, r.delta.roomTemp);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ot::MsgType::WriteAck), typeOf(r.response));
}

void test_room_temp_write_captures_nothing_else() {
    // The sparse-delta contract: one field written means one flag set.
    const auto r = OtResponder::handle(writeTemp(ot::DataId::Tr, 21.5f),
                                       ZoneSnapshot{}, ctxFor(true, Season::Heating));
    TEST_ASSERT_FALSE(r.delta.setSetpoint);
    TEST_ASSERT_FALSE(r.delta.setBoilerTemp);
    TEST_ASSERT_FALSE(r.delta.setModulation);
    TEST_ASSERT_FALSE(r.delta.setCoolingControl);
    TEST_ASSERT_FALSE(r.delta.clearOverride);
}

void test_implausible_room_temp_is_rejected() {
    // The gate is 0 < f < 35. Values outside it leave the zone untouched but
    // are still acked, so the thermostat does not retry forever.
    const float rejected[] = {0.0f, 35.0f, 40.0f};
    for (float t : rejected) {
        const auto r = OtResponder::handle(writeTemp(ot::DataId::Tr, t),
                                           ZoneSnapshot{}, ctxFor(true, Season::Heating));
        TEST_ASSERT_FALSE(r.delta.setRoomTemp);
        TEST_ASSERT_TRUE(r.delta.empty());
        TEST_ASSERT_TRUE(r.hasResponse);
    }
}

// Pinned, not endorsed: roomTemp uses 0 as the "never reported" sentinel
// (ClimateLogic keeps the valve shut while roomTemp <= 0), and the gate above
// rejects a genuine 0 degC reading for the same reason. The two halves are
// consistent on purpose - change one and you must change the other.
void test_zero_room_temp_is_indistinguishable_from_no_reading() {
    const auto r = OtResponder::handle(writeTemp(ot::DataId::Tr, 0.0f),
                                       ZoneSnapshot{}, ctxFor(true, Season::Heating));
    TEST_ASSERT_FALSE(r.delta.setRoomTemp);
}

void test_setpoint_write_is_captured() {
    const auto r = OtResponder::handle(writeTemp(ot::DataId::TrSet, 20.0f),
                                       ZoneSnapshot{}, ctxFor(true, Season::Heating));
    TEST_ASSERT_TRUE(r.delta.setSetpoint);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, r.delta.setpoint);
    TEST_ASSERT_FALSE(r.delta.setRoomTemp);
}

void test_implausible_setpoint_is_rejected() {
    const auto r = OtResponder::handle(writeTemp(ot::DataId::TrSet, 60.0f),
                                       ZoneSnapshot{}, ctxFor(true, Season::Heating));
    TEST_ASSERT_FALSE(r.delta.setSetpoint);
}

// ---- Setpoint override (ID 9) ----------------------------------------------

void test_override_read_returns_the_pending_setpoint() {
    ZoneSnapshot zone;
    zone.setpoint = 18.0f;
    zone.overrideSetpoint = 22.0f;  // pushed from HA / the web UI
    const auto r = OtResponder::handle(readReq(ot::DataId::TrOverride), zone,
                                       ctxFor(true, Season::Heating));
    TEST_ASSERT_TRUE(r.hasResponse);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.0f, ot::toFloat(r.response));
    TEST_ASSERT_FALSE(r.delta.clearOverride);  // keep pushing until it lands
}

void test_override_clears_once_the_thermostat_agrees() {
    ZoneSnapshot zone;
    zone.setpoint = 22.0f;          // the thermostat has adopted it
    zone.overrideSetpoint = 22.0f;
    const auto r = OtResponder::handle(readReq(ot::DataId::TrOverride), zone,
                                       ctxFor(true, Season::Heating));
    TEST_ASSERT_TRUE(r.delta.clearOverride);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, ot::toFloat(r.response));
}

void test_override_read_with_nothing_pending_reports_zero() {
    ZoneSnapshot zone;
    zone.setpoint = 18.0f;
    zone.overrideSetpoint = 0.0f;
    const auto r = OtResponder::handle(readReq(ot::DataId::TrOverride), zone,
                                       ctxFor(true, Season::Heating));
    TEST_ASSERT_TRUE(r.delta.clearOverride);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, ot::toFloat(r.response));
}

void test_override_write_is_not_answered() {
    // Frame 0 is not a valid slave reply, so a write here is left unanswered
    // rather than acked with an all-zeros frame like the old firmware did.
    const auto r = OtResponder::handle(writeTemp(ot::DataId::TrOverride, 21.0f),
                                       ZoneSnapshot{}, ctxFor(true, Season::Heating));
    TEST_ASSERT_FALSE(r.hasResponse);
}

// ---- Telemetry captured from the thermostat --------------------------------

void test_boiler_temp_write_is_captured_and_read_is_served() {
    const auto written = OtResponder::handle(writeTemp(ot::DataId::Tboiler, 55.0f),
                                             ZoneSnapshot{}, ctxFor(true, Season::Heating));
    TEST_ASSERT_TRUE(written.delta.setBoilerTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 55.0f, written.delta.boilerTemp);

    ZoneSnapshot zone;
    zone.boilerTemp = 55.0f;
    const auto read = OtResponder::handle(readReq(ot::DataId::Tboiler), zone,
                                          ctxFor(true, Season::Heating));
    TEST_ASSERT_TRUE(read.delta.empty());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 55.0f, ot::toFloat(read.response));
}

void test_max_modulation_setting_is_captured_as_modulation() {
    // The captured value comes from ID 14 (the master's max modulation
    // setting); ID 17 is a read the slave answers from stored state.
    const auto r = OtResponder::handle(writeTemp(ot::DataId::MaxRelModLevelSetting, 42.0f),
                                       ZoneSnapshot{}, ctxFor(true, Season::Heating));
    TEST_ASSERT_TRUE(r.delta.setModulation);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.0f, r.delta.modulation);
}

void test_rel_mod_level_read_serves_stored_modulation() {
    ZoneSnapshot zone;
    zone.modulation = 42.0f;
    const auto r = OtResponder::handle(readReq(ot::DataId::RelModLevel), zone,
                                       ctxFor(true, Season::Heating));
    TEST_ASSERT_TRUE(r.delta.empty());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.0f, ot::toFloat(r.response));
}

void test_cooling_control_is_captured_without_a_plausibility_gate() {
    // Unlike Tr/TrSet this value is a percentage, so the 0..35 room-value
    // window deliberately does not apply.
    const auto r = OtResponder::handle(writeTemp(ot::DataId::CoolingControl, 80.0f),
                                       ZoneSnapshot{}, ctxFor(true, Season::Cooling));
    TEST_ASSERT_TRUE(r.delta.setCoolingControl);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, r.delta.coolingControl);
}

// ---- Fault reporting (ID 5) ------------------------------------------------

void test_asf_flags_report_zero_when_healthy() {
    const auto r = OtResponder::handle(readReq(ot::DataId::AsfFlags), ZoneSnapshot{},
                                       ctxFor(true, Season::Heating));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ot::MsgType::ReadAck), typeOf(r.response));
    TEST_ASSERT_EQUAL_HEX16(0x0000, ot::dataValue(r.response));
}

void test_asf_flags_report_service_request_and_oem_code_on_fault() {
    ZoneSnapshot zone;
    zone.fault = true;
    zone.errorCode = 0x2A;
    const auto r = OtResponder::handle(readReq(ot::DataId::AsfFlags), zone,
                                       ctxFor(true, Season::Heating));
    TEST_ASSERT_EQUAL_HEX16(0x012A, ot::dataValue(r.response));
}

// ---- Static / simulated values ---------------------------------------------

void test_max_ch_setpoint_read_reports_the_simulated_ceiling() {
    const auto r = OtResponder::handle(readReq(ot::DataId::MaxTSet), ZoneSnapshot{},
                                       ctxFor(true, Season::Heating));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.0f, ot::toFloat(r.response));
}

void test_max_ch_setpoint_write_is_echoed_back() {
    const auto r = OtResponder::handle(writeTemp(ot::DataId::MaxTSet, 60.0f),
                                       ZoneSnapshot{}, ctxFor(true, Season::Heating));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ot::MsgType::WriteAck), typeOf(r.response));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, ot::toFloat(r.response));
    TEST_ASSERT_TRUE(r.delta.empty());
}

void test_ch_pressure_reports_a_plausible_simulated_value() {
    const auto r = OtResponder::handle(readReq(ot::DataId::ChPressure), ZoneSnapshot{},
                                       ctxFor(true, Season::Heating));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.5f, ot::toFloat(r.response));
}

void test_ot_version_reports_2_2() {
    const auto r = OtResponder::handle(readReq(ot::DataId::OpenThermVersionSlave),
                                       ZoneSnapshot{}, ctxFor(true, Season::Heating));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.2f, ot::toFloat(r.response));
}

void test_rbp_flags_offer_nothing() {
    const auto r = OtResponder::handle(readReq(ot::DataId::RbpFlags), ZoneSnapshot{},
                                       ctxFor(true, Season::Heating));
    TEST_ASSERT_EQUAL_HEX16(0x0000, ot::dataValue(r.response));
}

// ---- Unknown IDs -----------------------------------------------------------

void test_unknown_data_id_is_answered_as_unknown() {
    const auto r = OtResponder::handle(readReq(200), ZoneSnapshot{},
                                       ctxFor(true, Season::Heating));
    TEST_ASSERT_TRUE(r.hasResponse);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ot::MsgType::UnknownDataId), typeOf(r.response));
    TEST_ASSERT_EQUAL_UINT8(200, ot::dataId(r.response));
    TEST_ASSERT_TRUE(r.delta.empty());
}

// ---- Every reply must be a legal frame -------------------------------------

void test_all_responses_carry_valid_parity() {
    // A reply with wrong parity is dropped by the master, which looks like a
    // dead channel. Sweep every data ID the responder can see.
    ZoneSnapshot zone;
    zone.fault = true;
    zone.errorCode = 0x2A;
    zone.overrideSetpoint = 21.0f;
    const ot::MsgType types[] = {ot::MsgType::ReadData, ot::MsgType::WriteData};
    for (int id = 0; id <= 255; id++) {
        for (ot::MsgType type : types) {
            const auto r = OtResponder::handle(
                request(type, static_cast<uint8_t>(id), 0x1234), zone,
                ctxFor(true, Season::Heating));
            if (r.hasResponse) {
                TEST_ASSERT_FALSE(ot::parity(r.response));
                TEST_ASSERT_EQUAL_UINT8(id, ot::dataId(r.response));
            }
        }
    }
}

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_status_echoes_master_flags_in_the_high_byte);
    RUN_TEST(test_status_sets_ch_bit_when_heating_and_enabled);
    RUN_TEST(test_status_sets_cooling_bit_when_cooling_and_enabled);
    RUN_TEST(test_status_clears_mode_bits_when_zone_disabled);
    RUN_TEST(test_status_reports_fault_even_when_zone_disabled);
    RUN_TEST(test_status_reports_flame_when_enabled);
    RUN_TEST(test_master_config_is_acked_not_rejected);
    RUN_TEST(test_slave_config_reports_member_id_13_and_cooling_support);
    RUN_TEST(test_slave_config_flags_do_not_depend_on_season);
    RUN_TEST(test_slave_version_reports_a_product_version);
    RUN_TEST(test_room_temp_write_is_captured);
    RUN_TEST(test_room_temp_write_captures_nothing_else);
    RUN_TEST(test_implausible_room_temp_is_rejected);
    RUN_TEST(test_zero_room_temp_is_indistinguishable_from_no_reading);
    RUN_TEST(test_setpoint_write_is_captured);
    RUN_TEST(test_implausible_setpoint_is_rejected);
    RUN_TEST(test_override_read_returns_the_pending_setpoint);
    RUN_TEST(test_override_clears_once_the_thermostat_agrees);
    RUN_TEST(test_override_read_with_nothing_pending_reports_zero);
    RUN_TEST(test_override_write_is_not_answered);
    RUN_TEST(test_boiler_temp_write_is_captured_and_read_is_served);
    RUN_TEST(test_max_modulation_setting_is_captured_as_modulation);
    RUN_TEST(test_rel_mod_level_read_serves_stored_modulation);
    RUN_TEST(test_cooling_control_is_captured_without_a_plausibility_gate);
    RUN_TEST(test_asf_flags_report_zero_when_healthy);
    RUN_TEST(test_asf_flags_report_service_request_and_oem_code_on_fault);
    RUN_TEST(test_max_ch_setpoint_read_reports_the_simulated_ceiling);
    RUN_TEST(test_max_ch_setpoint_write_is_echoed_back);
    RUN_TEST(test_ch_pressure_reports_a_plausible_simulated_value);
    RUN_TEST(test_ot_version_reports_2_2);
    RUN_TEST(test_rbp_flags_offer_nothing);
    RUN_TEST(test_unknown_data_id_is_answered_as_unknown);
    RUN_TEST(test_all_responses_carry_valid_parity);
    return UNITY_END();
}
