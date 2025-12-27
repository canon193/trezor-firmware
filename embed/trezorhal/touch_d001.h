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
 */

#include "stmpe811.h"

static int touching = 0;
static uint32_t last_xy = 0;

void touch_init(void) {
    // Nothing to do here - initialization happens in touch_power_on
}

void touch_power_on(void) {
    stmpe811_init();
}

void touch_power_off(void) {
    // STMPE811 doesn't have explicit power control
    // Could put into low power mode if needed
}

void touch_sensitivity(uint8_t value) {
    // STMPE811 sensitivity is configured during init
    // This could be used to adjust TSC_CFG register
    (void)value;
}

uint32_t touch_is_detected(void) {
    return stmpe811_is_touched() ? 1 : 0;
}

uint32_t touch_read(void) {
    stmpe811_state_t state;
    stmpe811_get_state(&state);

    if (!state.TouchDetected) {
        if (touching) {
            // Was touching, now released
            touching = 0;
            return TOUCH_END | last_xy;
        }
        return 0;
    }

    // Pack X and Y coordinates
    uint32_t xy = touch_pack_xy(state.X, state.Y);

    if (!touching) {
        // First touch
        touching = 1;
        last_xy = xy;
        return TOUCH_START | xy;
    }

    // Continued touch
    if (xy != last_xy) {
        last_xy = xy;
        return TOUCH_MOVE | xy;
    }

    return 0;  // No change
}

int touch_read_raw(uint16_t *raw_x, uint16_t *raw_y) {
    return stmpe811_get_raw(raw_x, raw_y) ? 1 : 0;
}
