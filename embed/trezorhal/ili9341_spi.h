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

#ifndef __ILI9341_SPI_H__
#define __ILI9341_SPI_H__

#include <stdint.h>

// ILI9341 LTDC timing constants for 240x320 display
#define ILI9341_HSYNC ((uint32_t)9)   // Horizontal synchronization
#define ILI9341_HBP   ((uint32_t)29)  // Horizontal back porch
#define ILI9341_HFP   ((uint32_t)2)   // Horizontal front porch
#define ILI9341_VSYNC ((uint32_t)1)   // Vertical synchronization
#define ILI9341_VBP   ((uint32_t)3)   // Vertical back porch
#define ILI9341_VFP   ((uint32_t)2)   // Vertical front porch

// Initialize ILI9341 display controller via SPI
void ili9341_init(void);

#endif // __ILI9341_SPI_H__
