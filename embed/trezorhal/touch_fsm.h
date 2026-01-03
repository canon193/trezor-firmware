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

#ifndef TREZORHAL_TOUCH_FSM_H
#define TREZORHAL_TOUCH_FSM_H

#include <stdint.h>
#include <stdbool.h>

// Touch finite state machine structure
// Handles touch event reconciliation and missed event recovery
typedef struct {
  // Time (in ticks) when the FSM was last updated
  uint32_t update_ticks;
  // Last reported touch state
  uint32_t state;
  // Set if the touch controller is currently touched
  bool pressed;
  // Previously reported x-coordinate
  uint16_t last_x;
  // Previously reported y-coordinate
  uint16_t last_y;
} touch_fsm_t;

// Initializes touch finite state machine
void touch_fsm_init(touch_fsm_t* fsm);

// Checks if touch_fsm_get_event() would return a non-zero event
bool touch_fsm_event_ready(touch_fsm_t* fsm, uint32_t touch_state);

// Processes the new state of the touch panel and returns the event
//
// `touch_state` is the current state of the touch panel with format:
// TOUCH_START/TOUCH_MOVE/TOUCH_END | packed_xy
//
// Returns the processed event or 0 if no event
uint32_t touch_fsm_get_event(touch_fsm_t* fsm, uint32_t touch_state);

#endif  // TREZORHAL_TOUCH_FSM_H
