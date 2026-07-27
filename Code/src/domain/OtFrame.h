// Pure OpenTherm frame codec. No IDF/FreeRTOS dependencies - host-compilable.
//
// Frame structure (32 bits):
//   P MSG-TYPE SPARE DATA-ID  DATA-VALUE
//   0 000      0000  00000000 00000000 00000000
//
// Logic mirrors the vendored OpenTherm library's static helpers so the
// domain layer can build/parse frames without dragging in the driver.

#pragma once

#include <cstdint>

namespace ot {

using Frame = uint32_t;

enum class MsgType : uint8_t {
    // Master to slave
    ReadData = 0b000,
    WriteData = 0b001,
    InvalidData = 0b010,
    Reserved = 0b011,
    // Slave to master
    ReadAck = 0b100,
    WriteAck = 0b101,
    DataInvalid = 0b110,
    UnknownDataId = 0b111,
};

// Data IDs the responder handles (subset of the OpenTherm 2.2 table).
enum DataId : uint8_t {
    Status = 0,
    TSet = 1,
    MConfigMMemberId = 2,
    SConfigSMemberId = 3,
    AsfFlags = 5,
    RbpFlags = 6,
    CoolingControl = 7,
    TrOverride = 9,
    MaxRelModLevelSetting = 14,
    TrSet = 16,
    RelModLevel = 17,
    ChPressure = 18,
    Tr = 24,
    Tboiler = 25,
    Toutside = 27,
    MaxTSet = 57,
    OpenThermVersionSlave = 125,
    MasterVersion = 126,
    SlaveVersion = 127,
};

inline bool parity(Frame frame) {  // odd parity
    uint8_t p = 0;
    while (frame > 0) {
        if (frame & 1) p++;
        frame >>= 1;
    }
    return (p & 1);
}

inline MsgType msgType(Frame frame) {
    return static_cast<MsgType>((frame >> 28) & 7);
}

inline uint8_t dataId(Frame frame) {
    return static_cast<uint8_t>((frame >> 16) & 0xFF);
}

inline uint16_t dataValue(Frame frame) {
    return static_cast<uint16_t>(frame & 0xFFFF);
}

// f8.8 fixed point -> float
inline float toFloat(Frame frame) {
    const uint16_t u88 = dataValue(frame);
    return (u88 & 0x8000) ? -static_cast<float>(0x10000L - u88) / 256.0f
                          : u88 / 256.0f;
}

// float -> f8.8 fixed point, clamped to [0, 100] like the vendored lib
inline uint16_t fromTemperature(float temperature) {
    if (temperature < 0) temperature = 0;
    if (temperature > 100) temperature = 100;
    return static_cast<uint16_t>(temperature * 256);
}

inline Frame buildResponse(MsgType type, uint8_t id, uint16_t data) {
    Frame response = data;
    response |= static_cast<Frame>(type) << 28;
    response |= static_cast<Frame>(id) << 16;
    if (parity(response)) response |= (1ul << 31);
    return response;
}

inline bool isValidRequest(Frame request) {
    if (parity(request)) return false;
    const uint8_t type = (request << 1) >> 29;
    return type == static_cast<uint8_t>(MsgType::ReadData) ||
           type == static_cast<uint8_t>(MsgType::WriteData);
}

}  // namespace ot
