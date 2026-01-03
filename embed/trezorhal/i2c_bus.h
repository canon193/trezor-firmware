/*
 * This file is part of the Trezor project, https://trezor.io/
 *
 * Copyright (c) SatoshiLabs
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TREZORHAL_I2C_BUS_H
#define TREZORHAL_I2C_BUS_H

#include <stdint.h>
#include <stdbool.h>

// I2C bus abstraction
typedef struct i2c_bus i2c_bus_t;

// I2C packet status
typedef enum {
  I2C_STATUS_OK = 0,       // Packet completed successfully
  I2C_STATUS_PENDING = 1,  // Packet is pending
  I2C_STATUS_TIMEOUT = 4,  // Timeout occurred
  I2C_STATUS_NACK = 5,     // Device did not acknowledge
  I2C_STATUS_ERROR = 6,    // General error
} i2c_status_t;

// I2C operation flags
#define I2C_FLAG_TX     0x0004  // Transmit data
#define I2C_FLAG_RX     0x0008  // Receive data
#define I2C_FLAG_EMBED  0x0010  // Embedded data (no reference)

// I2C operation (single transfer)
typedef struct {
  // I2C_FLAG_xxx
  uint16_t flags;
  // Number of bytes to transfer
  uint16_t size;
  // Data to read or write
  union {
    // Pointer to data (I2C_FLAG_EMBED is not set)
    void* ptr;
    // Embedded data (I2C_FLAG_EMBED is set)
    uint8_t data[4];
  };
} i2c_op_t;

// I2C packet (series of I2C operations)
typedef struct {
  // I2C device address (7-bit address)
  uint8_t address;
  // Timeout in milliseconds
  uint16_t timeout;
  // Number of operations
  uint8_t op_count;
  // Pointer to array of operations
  i2c_op_t* ops;
} i2c_packet_t;

// Acquires I2C bus reference by index
// For D001: index 0 = I2C3
i2c_bus_t* i2c_bus_open(uint8_t bus_index);

// Closes I2C bus handle
void i2c_bus_close(i2c_bus_t* bus);

// Submits I2C packet and waits for completion
i2c_status_t i2c_bus_submit_and_wait(i2c_bus_t* bus, i2c_packet_t* packet);

// Helper macro for array length
#ifndef ARRAY_LENGTH
#define ARRAY_LENGTH(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#endif  // TREZORHAL_I2C_BUS_H
