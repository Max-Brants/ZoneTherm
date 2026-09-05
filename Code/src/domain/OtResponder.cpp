#include "OtResponder.h"

namespace OtResponder {

namespace {

// Simulated values (no sensors for these on the board)
constexpr float kChPressureBar = 1.5f;
constexpr float kOtVersion = 2.2f;
constexpr float kMaxChSetpointC = 75.0f;

// Mirror the vendor code the connected thermostats announce in their
// master config (ID 2) so they don't downgrade to generic-boiler behavior.
// Observed from the Honeywell Home modulation units on this system.
constexpr uint8_t kMemberId = 13;

// Product type/version reported for ID 127 (slave version) probes.
constexpr uint16_t kSlaveProductVersion = 0x0101;

// Room temperature / setpoint sanity window used by the old firmware
inline bool plausibleRoomValue(float f) { return f > 0 && f < 35; }

}  // namespace

Result handle(ot::Frame request, const ZoneSnapshot& zone, const Context& ctx) {
    Result r;

    const uint8_t id = ot::dataId(request);
    const uint16_t data = ot::dataValue(request);
    const float f = ot::toFloat(request);
    const bool isWrite = ot::msgType(request) == ot::MsgType::WriteData;

    const bool heatingEnabled = ctx.zoneEnabled && ctx.mode == Season::Heating;
    const bool coolingEnabled = ctx.zoneEnabled && ctx.mode == Season::Cooling;

    switch (id) {
        case ot::DataId::Status: {
            // HB echoes the master status flags from the request; LB is the
            // slave status (OT 2.2 spec, matches OpenTherm::isFault() et al).
            uint16_t status = data & 0xFF00;
            if (zone.fault) status |= 0x01;              // fault indication
            if (ctx.zoneEnabled) {
                if (heatingEnabled) status |= 0x02;      // CH mode
                if (zone.flameOn) status |= 0x08;        // flame status
                if (coolingEnabled) status |= 0x10;      // cooling mode
            }
            r.response = ot::buildResponse(ot::MsgType::ReadAck, id, status);
            r.hasResponse = true;
            break;
        }

        case ot::DataId::MConfigMMemberId: {
            // Master config flags (HB) + master member ID (LB). Ack instead
            // of UNKNOWN-DATAID so vendor thermostats don't downgrade
            // features; the engine logs the reported member ID.
            r.response = ot::buildResponse(ot::MsgType::WriteAck, id, data);
            r.hasResponse = true;
            break;
        }

        case ot::DataId::SConfigSMemberId: {
            // HB = slave configuration flags, LB = member ID. Capability
            // flags must be static: thermostats typically read this once at
            // power-up and cache it, so the cooling bit cannot depend on the
            // current season. No DHW on this system, so those bits stay 0.
            uint16_t cfg = 0;
            // Control type bit (HB bit 1) stays 0 (on/off, not modulating)
            cfg |= 0x04 << 8;                        // cooling supported
            cfg |= kMemberId;
            r.response = ot::buildResponse(ot::MsgType::ReadAck, id, cfg);
            r.hasResponse = true;
            break;
        }

        case ot::DataId::RbpFlags: {
            // No remote boiler parameters offered for transfer
            r.response = ot::buildResponse(ot::MsgType::ReadAck, id, 0);
            r.hasResponse = true;
            break;
        }

        case ot::DataId::MaxTSet: {
            if (isWrite) {
                r.response = ot::buildResponse(ot::MsgType::WriteAck, id, data);
            } else {
                r.response = ot::buildResponse(ot::MsgType::ReadAck, id,
                                               ot::fromTemperature(kMaxChSetpointC));
            }
            r.hasResponse = true;
            break;
        }

        case ot::DataId::Tr: {
            if (plausibleRoomValue(f)) {
                r.delta.setRoomTemp = true;
                r.delta.roomTemp = f;
            }
            r.response = ot::buildResponse(ot::MsgType::WriteAck, id, data);
            r.hasResponse = true;
            break;
        }

        case ot::DataId::TrSet: {
            if (plausibleRoomValue(f)) {
                r.delta.setSetpoint = true;
                r.delta.setpoint = f;
            }
            r.response = ot::buildResponse(ot::MsgType::WriteAck, id, data);
            r.hasResponse = true;
            break;
        }

        case ot::DataId::TSet: {
            // Boiler control setpoint - acknowledge only
            r.response = ot::buildResponse(ot::MsgType::WriteAck, id, data);
            r.hasResponse = true;
            break;
        }

        case ot::DataId::TrOverride: {
            if (!isWrite) {
                // Once the thermostat's own setpoint matches the override
                // (or no override is pending), stop pushing it.
                float override_ = zone.overrideSetpoint;
                if (override_ == zone.setpoint || override_ == 0) {
                    r.delta.clearOverride = true;
                    override_ = 0;
                }
                r.response = ot::buildResponse(ot::MsgType::ReadAck, id,
                                               ot::fromTemperature(override_));
                r.hasResponse = true;
            }
            // Old firmware sent an all-zeros frame for writes here; suppress
            // the response instead (frame 0 is not a valid slave reply).
            break;
        }

        case ot::DataId::CoolingControl: {
            r.delta.setCoolingControl = true;
            r.delta.coolingControl = f;
            r.response = ot::buildResponse(ot::MsgType::WriteAck, id, data);
            r.hasResponse = true;
            break;
        }

        case ot::DataId::Tboiler: {
            if (isWrite) {
                r.delta.setBoilerTemp = true;
                r.delta.boilerTemp = f;
                r.response = ot::buildResponse(ot::MsgType::WriteAck, id, data);
            } else {
                r.response = ot::buildResponse(ot::MsgType::ReadAck, id,
                                               ot::fromTemperature(zone.boilerTemp));
            }
            r.hasResponse = true;
            break;
        }

        case ot::DataId::MaxRelModLevelSetting: {
            r.delta.setModulation = true;
            r.delta.modulation = f;
            r.response = ot::buildResponse(ot::MsgType::WriteAck, id, data);
            r.hasResponse = true;
            break;
        }

        case ot::DataId::RelModLevel: {
            r.response = ot::buildResponse(ot::MsgType::ReadAck, id,
                                           ot::fromTemperature(zone.modulation));
            r.hasResponse = true;
            break;
        }

        case ot::DataId::ChPressure: {
            r.response = ot::buildResponse(ot::MsgType::ReadAck, id,
                                           ot::fromTemperature(kChPressureBar));
            r.hasResponse = true;
            break;
        }

        case ot::DataId::AsfFlags: {
            // HB = application-specific fault flags, LB = OEM fault code.
            // A healthy slave ReadAcks zero rather than replying DATA-INVALID.
            uint16_t flags = 0;
            if (zone.fault) {
                flags = (0x01 << 8) | zone.errorCode;  // service request + code
            }
            r.response = ot::buildResponse(ot::MsgType::ReadAck, id, flags);
            r.hasResponse = true;
            break;
        }

        case ot::DataId::OpenThermVersionSlave: {
            r.response = ot::buildResponse(ot::MsgType::ReadAck, id,
                                           ot::fromTemperature(kOtVersion));
            r.hasResponse = true;
            break;
        }

        case ot::DataId::MasterVersion: {
            // Thermostat reporting its product type/version - acknowledge
            r.response = ot::buildResponse(ot::MsgType::WriteAck, id, data);
            r.hasResponse = true;
            break;
        }

        case ot::DataId::SlaveVersion: {
            r.response = ot::buildResponse(ot::MsgType::ReadAck, id,
                                           kSlaveProductVersion);
            r.hasResponse = true;
            break;
        }

        default: {
            r.response = ot::buildResponse(ot::MsgType::UnknownDataId, id, 0);
            r.hasResponse = true;
            break;
        }
    }

    return r;
}

}  // namespace OtResponder
