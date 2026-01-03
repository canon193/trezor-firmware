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

/*
 * Touch driver for STM32F429I-DISC1 (D001 model).
 * Uses STMPE811 resistive touch controller on I2C3.
 * Ported from Latest Trezor firmware with FSM for event handling.
 */

#include "stmpe811.h"
#include "touch_fsm.h"
#include "i2c_bus.h"

static touch_fsm_t touch_fsm;
static i2c_bus_t *touch_i2c_bus = NULL;
static uint32_t last_touch_state = 0;

void touch_init(void) {
    // Nothing to do here - initialization happens in touch_power_on
}

void touch_power_on(void) {
    // Open I2C bus 0 (I2C3 on D001)
    touch_i2c_bus = i2c_bus_open(0);
    if (touch_i2c_bus == NULL) {
        return;
    }

    // Reset and initialize STMPE811
    stmpe811_Reset(touch_i2c_bus);

    // Configure touch screen mode
    touch_set_mode();

    // Initialize touch FSM
    touch_fsm_init(&touch_fsm);

    last_touch_state = 0;
}

void touch_power_off(void) {
    // STMPE811 doesn't have explicit power control
    // Could put into low power mode if needed
    if (touch_i2c_bus != NULL) {
        i2c_bus_close(touch_i2c_bus);
        touch_i2c_bus = NULL;
    }
}

void touch_sensitivity(uint8_t value) {
    // STMPE811 sensitivity is configured during init
    // This could be used to adjust TSC_CFG register
    (void)value;
}

uint32_t touch_is_detected(void) {
    return touch_active();
}

/* Get the current touch state from hardware */
static uint32_t touch_get_state(void) {
    TS_StateTypeDef ts = {0};
    BSP_TS_GetState(&ts);

    uint32_t xy = touch_pack_xy(ts.X, ts.Y);

    if (ts.TouchDetected) {
        if (!(last_touch_state & (TOUCH_START | TOUCH_MOVE))) {
            // New touch - report START
            last_touch_state = TOUCH_START | xy;
        } else {
            // Continued touch - report MOVE
            last_touch_state = TOUCH_MOVE | xy;
        }
    } else {
        if (last_touch_state & (TOUCH_START | TOUCH_MOVE)) {
            // Touch ended - report END with last coordinates
            last_touch_state = TOUCH_END | (last_touch_state & 0x00FFFFFF);
        } else {
            // No touch
            last_touch_state = 0;
        }
    }

    return last_touch_state;
}

uint32_t touch_read(void) {
    // Get raw touch state from hardware
    uint32_t state = touch_get_state();

    // Process through FSM for proper event handling
    return touch_fsm_get_event(&touch_fsm, state);
}

int touch_read_raw(uint16_t *raw_x, uint16_t *raw_y) {
    if (!touch_active()) {
        return 0;
    }

    stmpe811_TS_GetXY(raw_x, raw_y);
    return 1;
}
