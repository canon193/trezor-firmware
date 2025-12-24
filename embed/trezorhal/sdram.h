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

#ifndef __SDRAM_H__
#define __SDRAM_H__

#include <stdint.h>

// SDRAM device address (FMC SDRAM Bank 2)
#define SDRAM_DEVICE_ADDR 0xD0000000

// SDRAM size in bytes (8 MB for IS42S16400J)
#define SDRAM_DEVICE_SIZE 0x00800000

// SDRAM status
#define SDRAM_OK    ((uint8_t)0x00)
#define SDRAM_ERROR ((uint8_t)0x01)

// Initialize SDRAM peripheral
void sdram_init(void);

#endif // __SDRAM_H__
