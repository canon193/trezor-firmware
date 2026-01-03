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

#ifndef __STMPE811_H__
#define __STMPE811_H__

#include <stdint.h>
#include <stdbool.h>

#include "i2c_bus.h"

// Touch state structure
typedef struct {
    uint16_t TouchDetected;
    uint16_t X;
    uint16_t Y;
    uint16_t Z;
} TS_StateTypeDef;

// Get touch state (position and detection status)
void BSP_TS_GetState(TS_StateTypeDef *TsState);

// Reset and initialize STMPE811
void stmpe811_Reset(i2c_bus_t *i2c_bus);

// Configure touch screen mode
void touch_set_mode(void);

// Check if touch is active (returns 1 if touched)
uint32_t touch_active(void);

// Get raw touch values (for calibration)
void stmpe811_TS_GetXY(uint16_t *X, uint16_t *Y);

// Debug: Read chip ID (should return 0x0811)
uint16_t stmpe811_ReadID(void);

// Debug: Read chip ID with I2C status for debugging
// Returns I2C status, fills chip_id and individual I2C status for each read
// i2c_status values: 0=OK, 1=TIMEOUT, 2=NACK, 3=ERROR
i2c_status_t stmpe811_ReadID_Debug(uint16_t *chip_id, uint8_t *i2c_status_msb, uint8_t *i2c_status_lsb);

// Debug: Read TSC_CTRL register
uint8_t stmpe811_ReadTscCtrl(void);

// Debug: Read FIFO_SIZE register
uint8_t stmpe811_ReadFifoSize(void);

#endif // __STMPE811_H__
