// Frame codec tests. Everything under test is inline in OtFrame.h, so this
// suite compiles the header standalone - no domain .cpp is involved.
//
// Frame layout (MSB first): P | MSG-TYPE(3) | SPARE(4) | DATA-ID(8) | DATA(16)

#include <unity.h>

#include "domain/OtFrame.h"

namespace {

// Built by hand rather than with ot::buildResponse, so the extraction tests
// below do not just re-assert the builder's own arithmetic.
ot::Frame rawFrame(uint8_t msgType, uint8_t id, uint16_t data) {
    return (static_cast<ot::Frame>(msgType) << 28) |
           (static_cast<ot::Frame>(id) << 16) | data;
}

}  // namespace

// ---- field extraction ------------------------------------------------------

void test_extracts_msg_type_id_and_value() {
    const ot::Frame f = rawFrame(0b001, 24, 0x1580);
    // MsgType is an enum class, so Unity's integer comparisons need the cast.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ot::MsgType::WriteData),
                          static_cast<int>(ot::msgType(f)));
    TEST_ASSERT_EQUAL_UINT8(24, ot::dataId(f));
    TEST_ASSERT_EQUAL_HEX16(0x1580, ot::dataValue(f));
}

void test_parity_bit_is_not_part_of_msg_type() {
    // Setting bit 31 must not bleed into the 3-bit message type.
    const ot::Frame f = rawFrame(0b100, 0, 0) | (1ul << 31);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ot::MsgType::ReadAck),
                          static_cast<int>(ot::msgType(f)));
}

void test_data_id_ignores_the_spare_nibble() {
    const ot::Frame f = rawFrame(0b000, 127, 0) | (0x0Ful << 24);
    TEST_ASSERT_EQUAL_UINT8(127, ot::dataId(f));
}

// ---- parity ----------------------------------------------------------------

void test_parity_counts_set_bits() {
    TEST_ASSERT_FALSE(ot::parity(0x00000000ul));  // zero set bits - even
    TEST_ASSERT_TRUE(ot::parity(0x00000001ul));   // one - odd
    TEST_ASSERT_FALSE(ot::parity(0x00000003ul));  // two - even
    TEST_ASSERT_TRUE(ot::parity(0x00010101ul));   // three - odd
}

void test_built_frames_always_have_even_parity() {
    // The invariant the wire format depends on: buildResponse sets bit 31
    // exactly when needed so the finished frame has an even number of set
    // bits, which is what isValidRequest/the master check.
    const ot::Frame cases[] = {
        ot::buildResponse(ot::MsgType::ReadAck, 0, 0x0000),
        ot::buildResponse(ot::MsgType::ReadAck, 0, 0x0002),
        ot::buildResponse(ot::MsgType::WriteAck, 24, 0x1580),
        ot::buildResponse(ot::MsgType::UnknownDataId, 200, 0),
        ot::buildResponse(ot::MsgType::ReadAck, 127, 0x0101),
    };
    for (ot::Frame f : cases) {
        TEST_ASSERT_FALSE(ot::parity(f));
    }
}

void test_build_response_sets_parity_bit_only_when_needed() {
    // id 0, data 0, ReadAck (0b100) - a single set bit, so parity must be set.
    const ot::Frame odd = ot::buildResponse(ot::MsgType::ReadAck, 0, 0);
    TEST_ASSERT_TRUE(odd & (1ul << 31));

    // Adding one more set bit makes the count even - parity must stay clear.
    const ot::Frame even = ot::buildResponse(ot::MsgType::ReadAck, 0, 1);
    TEST_ASSERT_FALSE(even & (1ul << 31));
}

// ---- f8.8 fixed point ------------------------------------------------------

void test_to_float_decodes_positive_values() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.5f, ot::toFloat(rawFrame(0, 0, 0x1580)));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, ot::toFloat(rawFrame(0, 0, 0x0000)));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, ot::toFloat(rawFrame(0, 0, 0x0100)));
}

void test_to_float_decodes_negative_values() {
    // The real case for this branch is a sub-zero outside temperature.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.0f, ot::toFloat(rawFrame(0, 0, 0xFF00)));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.5f, ot::toFloat(rawFrame(0, 0, 0xFA80)));
}

void test_from_temperature_round_trips() {
    const float values[] = {0.0f, 0.5f, 18.0f, 21.5f, 99.5f};
    for (float v : values) {
        const ot::Frame f = rawFrame(0, 0, ot::fromTemperature(v));
        TEST_ASSERT_FLOAT_WITHIN(0.01f, v, ot::toFloat(f));
    }
}

void test_from_temperature_clamps_to_zero_and_hundred() {
    TEST_ASSERT_EQUAL_UINT16(0, ot::fromTemperature(-0.1f));
    TEST_ASSERT_EQUAL_UINT16(0, ot::fromTemperature(-40.0f));
    TEST_ASSERT_EQUAL_UINT16(100 * 256, ot::fromTemperature(100.0f));
    TEST_ASSERT_EQUAL_UINT16(100 * 256, ot::fromTemperature(250.0f));
}

// Pinned, not endorsed: fromTemperature() clamps at 0, so the firmware can
// never transmit a sub-zero temperature even though toFloat() can receive one.
void test_from_temperature_cannot_express_negatives() {
    const ot::Frame f = rawFrame(0, 0, ot::fromTemperature(-5.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, ot::toFloat(f));
}

// ---- request validation ----------------------------------------------------

void test_valid_request_accepts_master_originated_types() {
    TEST_ASSERT_TRUE(ot::isValidRequest(ot::buildResponse(ot::MsgType::ReadData, 0, 0)));
    TEST_ASSERT_TRUE(ot::isValidRequest(ot::buildResponse(ot::MsgType::WriteData, 16, 0x1400)));
}

void test_valid_request_rejects_slave_originated_types() {
    // A frame the slave itself would send is never a valid inbound request.
    TEST_ASSERT_FALSE(ot::isValidRequest(ot::buildResponse(ot::MsgType::ReadAck, 0, 0)));
    TEST_ASSERT_FALSE(ot::isValidRequest(ot::buildResponse(ot::MsgType::WriteAck, 0, 0)));
    TEST_ASSERT_FALSE(ot::isValidRequest(ot::buildResponse(ot::MsgType::UnknownDataId, 0, 0)));
    TEST_ASSERT_FALSE(ot::isValidRequest(ot::buildResponse(ot::MsgType::InvalidData, 0, 0)));
}

void test_valid_request_rejects_bad_parity() {
    ot::Frame good = ot::buildResponse(ot::MsgType::ReadData, 24, 0x1580);
    TEST_ASSERT_TRUE(ot::isValidRequest(good));
    // Flip one data bit: the parity bit no longer matches the payload.
    TEST_ASSERT_FALSE(ot::isValidRequest(good ^ 1ul));
}

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_extracts_msg_type_id_and_value);
    RUN_TEST(test_parity_bit_is_not_part_of_msg_type);
    RUN_TEST(test_data_id_ignores_the_spare_nibble);
    RUN_TEST(test_parity_counts_set_bits);
    RUN_TEST(test_built_frames_always_have_even_parity);
    RUN_TEST(test_build_response_sets_parity_bit_only_when_needed);
    RUN_TEST(test_to_float_decodes_positive_values);
    RUN_TEST(test_to_float_decodes_negative_values);
    RUN_TEST(test_from_temperature_round_trips);
    RUN_TEST(test_from_temperature_clamps_to_zero_and_hundred);
    RUN_TEST(test_from_temperature_cannot_express_negatives);
    RUN_TEST(test_valid_request_accepts_master_originated_types);
    RUN_TEST(test_valid_request_rejects_slave_originated_types);
    RUN_TEST(test_valid_request_rejects_bad_parity);
    return UNITY_END();
}
