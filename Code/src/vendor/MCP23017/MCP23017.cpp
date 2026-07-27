#include "MCP23017.h"

namespace {
    constexpr int I2C_TIMEOUT_MS = 1000;

    inline void bitWrite(uint8_t& value, uint8_t bit, bool set) {
        if (set) {
            value |= (1 << bit);
        } else {
            value &= ~(1 << bit);
        }
    }
}

MCP23017::MCP23017(i2c_master_bus_handle_t bus, uint8_t address) : _bus(bus), _deviceAddr(address) {
    i2c_device_config_t devConfig = {};
    devConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    devConfig.device_address = address;
    devConfig.scl_speed_hz = 100000;
    i2c_master_bus_add_device(_bus, &devConfig, &_device);
}

MCP23017::~MCP23017() {
    if (_device) {
        i2c_master_bus_rm_device(_device);
    }
}

void MCP23017::init() {
    // Byte mode (IOCON.BANK = 0, the default) with sequential operation enabled.
    writeRegister(MCP23017Register::IOCON, 0b00100000);
    // Pull-ups enabled on all pins; only effective for pins later configured as inputs.
    writeRegister(MCP23017Register::GPPU_A, 0xFF, 0xFF);
}

void MCP23017::portMode(MCP23017Port port, uint8_t directions, uint8_t pullups, uint8_t inverted) {
    writeRegister(MCP23017Register::IODIR_A + port, directions);
    writeRegister(MCP23017Register::GPPU_A + port, pullups);
    writeRegister(MCP23017Register::IPOL_A + port, inverted);
}

void MCP23017::pinMode(uint8_t pin, uint8_t mode, bool inverted) {
    MCP23017Port port = pin < 8 ? MCP23017Port::A : MCP23017Port::B;
    uint8_t bit = pin % 8;

    uint8_t iodir = readRegister(MCP23017Register::IODIR_A + port);
    bitWrite(iodir, bit, mode == MCP23017_INPUT || mode == MCP23017_INPUT_PULLUP);
    writeRegister(MCP23017Register::IODIR_A + port, iodir);

    uint8_t pullup = readRegister(MCP23017Register::GPPU_A + port);
    bitWrite(pullup, bit, mode == MCP23017_INPUT_PULLUP);
    writeRegister(MCP23017Register::GPPU_A + port, pullup);

    uint8_t polarity = readRegister(MCP23017Register::IPOL_A + port);
    bitWrite(polarity, bit, inverted);
    writeRegister(MCP23017Register::IPOL_A + port, polarity);
}

void MCP23017::digitalWrite(uint8_t pin, uint8_t state) {
    MCP23017Port port = pin < 8 ? MCP23017Port::A : MCP23017Port::B;
    uint8_t bit = pin % 8;

    uint8_t gpio = readRegister(MCP23017Register::GPIO_A + port);
    bitWrite(gpio, bit, state != 0);
    writeRegister(MCP23017Register::GPIO_A + port, gpio);
}

uint8_t MCP23017::digitalRead(uint8_t pin) {
    MCP23017Port port = pin < 8 ? MCP23017Port::A : MCP23017Port::B;
    uint8_t bit = pin % 8;
    uint8_t gpio = readRegister(MCP23017Register::GPIO_A + port);
    return (gpio >> bit) & 0x01;
}

void MCP23017::writePort(MCP23017Port port, uint8_t value) {
    writeRegister(MCP23017Register::GPIO_A + port, value);
}

void MCP23017::write(uint16_t value) {
    writeRegister(MCP23017Register::GPIO_A, static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8));
}

uint8_t MCP23017::readPort(MCP23017Port port) {
    return readRegister(MCP23017Register::GPIO_A + port);
}

uint16_t MCP23017::read() {
    uint8_t a, b;
    readRegister(MCP23017Register::GPIO_A, a, b);
    return (static_cast<uint16_t>(b) << 8) | a;
}

void MCP23017::writeRegister(MCP23017Register reg, uint8_t value) {
    uint8_t buf[2] = { static_cast<uint8_t>(reg), value };
    i2c_master_transmit(_device, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

void MCP23017::writeRegister(MCP23017Register reg, uint8_t portA, uint8_t portB) {
    uint8_t buf[3] = { static_cast<uint8_t>(reg), portA, portB };
    i2c_master_transmit(_device, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

uint8_t MCP23017::readRegister(MCP23017Register reg) {
    uint8_t regAddr = static_cast<uint8_t>(reg);
    uint8_t value = 0;
    i2c_master_transmit_receive(_device, &regAddr, 1, &value, 1, I2C_TIMEOUT_MS);
    return value;
}

void MCP23017::readRegister(MCP23017Register reg, uint8_t& portA, uint8_t& portB) {
    uint8_t regAddr = static_cast<uint8_t>(reg);
    uint8_t buf[2] = {0, 0};
    i2c_master_transmit_receive(_device, &regAddr, 1, buf, 2, I2C_TIMEOUT_MS);
    portA = buf[0];
    portB = buf[1];
}

#ifdef _MCP23017_INTERRUPT_SUPPORT_

void MCP23017::interruptMode(MCP23017InterruptMode intMode) {
    uint8_t iocon = readRegister(MCP23017Register::IOCON);
    if (intMode == MCP23017InterruptMode::Or) {
        iocon |= static_cast<uint8_t>(MCP23017InterruptMode::Or);
    } else {
        iocon &= ~static_cast<uint8_t>(MCP23017InterruptMode::Or);
    }
    writeRegister(MCP23017Register::IOCON, iocon);
}

void MCP23017::interrupt(MCP23017Port port, uint8_t mode) {
    writeRegister(MCP23017Register::GPINTEN_A + port, 0xFF);
    if (mode == 0 /* CHANGE */) {
        writeRegister(MCP23017Register::INTCON_A + port, 0x00);
    } else {
        writeRegister(MCP23017Register::INTCON_A + port, 0xFF);
        writeRegister(MCP23017Register::DEFVAL_A + port, mode == 1 /* FALLING */ ? 0xFF : 0x00);
    }
}

void MCP23017::disableInterrupt(MCP23017Port port) {
    writeRegister(MCP23017Register::GPINTEN_A + port, 0x00);
}

void MCP23017::interruptedBy(uint8_t& portA, uint8_t& portB) {
    readRegister(MCP23017Register::INTF_A, portA, portB);
}

void MCP23017::clearInterrupts() {
    uint8_t portA, portB;
    clearInterrupts(portA, portB);
}

void MCP23017::clearInterrupts(uint8_t& portA, uint8_t& portB) {
    readRegister(MCP23017Register::INTCAP_A, portA, portB);
}

#endif
