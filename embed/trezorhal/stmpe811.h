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

// Touch state structure
typedef struct {
    bool TouchDetected;
    uint16_t X;
    uint16_t Y;
} stmpe811_state_t;

// Initialize STMPE811 touch controller
void stmpe811_init(void);

// Check if touch is active
bool stmpe811_is_touched(void);

// Get touch state (position and detection status)
void stmpe811_get_state(stmpe811_state_t *state);

#endif // __STMPE811_H__
