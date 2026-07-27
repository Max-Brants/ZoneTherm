#pragma once

// Ported from blemasle/MCP23017 (Arduino/Wire) to ESP-IDF's native
// driver/i2c_master.h. Register map and port/pin logic are unchanged from
// upstream - only the I2C transport at the bottom (writeRegister/readRegister)
// was rewritten.

#include <cstdint>
#include "driver/i2c_master.h"

#define _MCP23017_INTERRUPT_SUPPORT_ ///< Enables support for MCP23017 interrupts.

enum class MCP23017Port : uint8_t
{
	A = 0,
	B = 1
};

/**
 * Controls if the two interrupt pins mirror each other.
 * See "3.6 Interrupt Logic".
 */
enum class MCP23017InterruptMode : uint8_t
{
	Separated = 0,	///< Interrupt pins are kept independent
	Or = 0b01000000	///< Interrupt pins are mirrored
};

/**
 * Registers addresses.
 * The library use addresses for IOCON.BANK = 0.
 * See "3.2.1 Byte mode and Sequential mode".
 */
enum class MCP23017Register : uint8_t
{
	IODIR_A		= 0x00, 		///< Controls the direction of the data I/O for port A.
	IODIR_B		= 0x01,			///< Controls the direction of the data I/O for port B.
	IPOL_A		= 0x02,			///< Configures the polarity on the corresponding GPIO_ port bits for port A.
	IPOL_B		= 0x03,			///< Configures the polarity on the corresponding GPIO_ port bits for port B.
	GPINTEN_A	= 0x04,			///< Controls the interrupt-on-change for each pin of port A.
	GPINTEN_B	= 0x05,			///< Controls the interrupt-on-change for each pin of port B.
	DEFVAL_A	= 0x06,			///< Controls the default comparaison value for interrupt-on-change for port A.
	DEFVAL_B	= 0x07,			///< Controls the default comparaison value for interrupt-on-change for port B.
	INTCON_A	= 0x08,			///< Controls how the associated pin value is compared for the interrupt-on-change for port A.
	INTCON_B	= 0x09,			///< Controls how the associated pin value is compared for the interrupt-on-change for port B.
	IOCON		= 0x0A,			///< Controls the device.
	GPPU_A		= 0x0C,			///< Controls the pull-up resistors for the port A pins.
	GPPU_B		= 0x0D,			///< Controls the pull-up resistors for the port B pins.
	INTF_A		= 0x0E,			///< Reflects the interrupt condition on the port A pins.
	INTF_B		= 0x0F,			///< Reflects the interrupt condition on the port B pins.
	INTCAP_A	= 0x10,			///< Captures the port A value at the time the interrupt occured.
	INTCAP_B	= 0x11,			///< Captures the port B value at the time the interrupt occured.
	GPIO_A		= 0x12,			///< Reflects the value on the port A.
	GPIO_B		= 0x13,			///< Reflects the value on the port B.
	OLAT_A		= 0x14,			///< Provides access to the port A output latches.
	OLAT_B		= 0x15,			///< Provides access to the port B output latches.
};

inline MCP23017Register operator+(MCP23017Register a, MCP23017Port b) {
	return static_cast<MCP23017Register>(static_cast<uint8_t>(a) + static_cast<uint8_t>(b));
};

// Arduino pinMode()-style constants, kept for parity with the upstream API.
constexpr uint8_t MCP23017_OUTPUT = 0;
constexpr uint8_t MCP23017_INPUT = 1;
constexpr uint8_t MCP23017_INPUT_PULLUP = 2;

class MCP23017
{
private:
	i2c_master_bus_handle_t _bus;
	i2c_master_dev_handle_t _device = nullptr;
	uint8_t _deviceAddr;

public:
	/**
	 * Instantiates a new instance to interact with a MCP23017 at the specified address
	 * on the given I2C master bus.
	 */
	MCP23017(i2c_master_bus_handle_t bus, uint8_t address);
	~MCP23017();

	/**
	 * Initializes the chip with the default configuration.
	 * Enables Byte mode (IOCON.BANK = 0 and IOCON.SEQOP = 1).
	 * Enables pull-up resistors for all pins. This will only be effective for input pins.
	 *
	 * See "3.2.1 Byte mode and Sequential mode".
	 */
	void init();
	/**
	 * Controls the pins direction on a whole port at once.
	 *
	 * 1 = Pin is configured as an input.
	 * 0 = Pin is configured as an output.
	 *
	 * See "3.5.1 I/O Direction register".
	 */
	void portMode(MCP23017Port port, uint8_t directions, uint8_t pullups = 0xFF, uint8_t inverted = 0x00);
	/**
	 * Controls a single pin direction.
	 * Pin 0-7 for port A, 8-15 fo port B.
	 *
	 * 1 = Pin is configured as an input.
	 * 0 = Pin is configured as an output.
	 *
	 * See "3.5.1 I/O Direction register".
	 */
	void pinMode(uint8_t pin, uint8_t mode, bool inverted = false);

	/**
	 * Writes a single pin state.
	 * Pin 0-7 for port A, 8-15 for port B.
	 */
	void digitalWrite(uint8_t pin, uint8_t state);
	/**
	 * Reads a single pin state.
	 * Pin 0-7 for port A, 8-15 for port B.
	 */
	uint8_t digitalRead(uint8_t pin);

	/**
	 * Writes pins state to a whole port.
	 */
	void writePort(MCP23017Port port, uint8_t value);
	/**
	 * Writes pins state to both ports.
	 */
	void write(uint16_t value);

	/**
	 * Reads pins state for a whole port.
	 */
	uint8_t readPort(MCP23017Port port);
	/**
	 * Reads pins state for both ports.
	 */
	uint16_t read();

	/**
	 * Writes a single register value.
	 */
	void writeRegister(MCP23017Register reg, uint8_t value);
	/**
	 * Writes values to a register pair.
	 */
	void writeRegister(MCP23017Register reg, uint8_t portA, uint8_t portB);
	/**
	 * Reads a single register value.
	 */
	uint8_t readRegister(MCP23017Register reg);
	/**
	 * Reads the values from a register pair.
	 */
	void readRegister(MCP23017Register reg, uint8_t& portA, uint8_t& portB);

#ifdef _MCP23017_INTERRUPT_SUPPORT_

	/**
	 * Controls how the interrupt pins act with each other.
	 */
	void interruptMode(MCP23017InterruptMode intMode);
	/**
	 * Configures interrupt registers using an Arduino-like API.
	 * mode can be one of CHANGE(0), FALLING(1) or RISING(2).
	 */
	void interrupt(MCP23017Port port, uint8_t mode);
	/**
	 * Disable interrupts for the specified port.
	 */
	void disableInterrupt(MCP23017Port port);
	/**
	 * Reads which pin caused the interrupt.
	 */
	void interruptedBy(uint8_t& portA, uint8_t& portB);
	/**
	 * Clears interrupts on both ports.
	 */
	void clearInterrupts();
	/**
	 * Clear interrupts on both ports. Returns port values at the time the interrupt occured.
	 */
	void clearInterrupts(uint8_t& portA, uint8_t& portB);

#endif
};
