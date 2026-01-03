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

#ifndef TREZORHAL_TOUCH_CALIB_H
#define TREZORHAL_TOUCH_CALIB_H

#include <stdint.h>
#include <stdbool.h>
#include "secbool.h"

/*
 * Touch Screen Calibration for STM32F429I-DISC1 (D001)
 *
 * Calibration process:
 * 1. User touches 5 points: top-left, top-right, bottom-left, bottom-right, center
 * 2. Raw touch values are collected for each point
 * 3. Linear transformation parameters are calculated
 * 4. Parameters are stored to flash sector 3 (unused sector)
 *
 * Transformation formula:
 *   screen_x = (raw_x - x_offset) * x_scale / SCALE_FACTOR
 *   screen_y = (raw_y - y_offset) * y_scale / SCALE_FACTOR
 *
 * Where SCALE_FACTOR is used for fixed-point math (avoiding floats)
 */

/* Magic number to identify valid calibration data */
#define TOUCH_CALIB_MAGIC       0x43414C42  /* "CALB" */

/* Calibration data version */
#define TOUCH_CALIB_VERSION     1

/* Scale factor for fixed-point math (16.16 format) */
#define TOUCH_CALIB_SCALE_BITS  16
#define TOUCH_CALIB_SCALE_FACTOR (1 << TOUCH_CALIB_SCALE_BITS)

/* Display dimensions */
#define TOUCH_CALIB_SCREEN_W    240
#define TOUCH_CALIB_SCREEN_H    320

/* Calibration point margin from screen edge */
#define TOUCH_CALIB_MARGIN      20

/* Number of calibration points */
#define TOUCH_CALIB_POINTS      5

/* Calibration point indices */
#define CALIB_POINT_TOP_LEFT      0
#define CALIB_POINT_TOP_RIGHT     1
#define CALIB_POINT_BOTTOM_LEFT   2
#define CALIB_POINT_BOTTOM_RIGHT  3
#define CALIB_POINT_CENTER        4

/* Flash sector for calibration data (sector 3 is unused) */
#define FLASH_SECTOR_CALIB      3

/* Calibration point coordinates (screen coordinates) */
typedef struct {
    uint16_t screen_x;      /* Expected screen X coordinate */
    uint16_t screen_y;      /* Expected screen Y coordinate */
    uint16_t raw_x;         /* Measured raw X value */
    uint16_t raw_y;         /* Measured raw Y value */
} touch_calib_point_t;

/* Calibration data stored in flash */
typedef struct {
    uint32_t magic;         /* TOUCH_CALIB_MAGIC */
    uint32_t version;       /* TOUCH_CALIB_VERSION */

    /* Transformation parameters (fixed-point 16.16) */
    int32_t x_offset;       /* X offset (raw value at screen 0) */
    int32_t x_scale;        /* X scale factor */
    int32_t y_offset;       /* Y offset (raw value at screen 0) */
    int32_t y_scale;        /* Y scale factor */

    /* Axis inversion flags */
    uint8_t x_invert;       /* 1 if X axis is inverted */
    uint8_t y_invert;       /* 1 if Y axis is inverted */
    uint8_t xy_swap;        /* 1 if X and Y are swapped */
    uint8_t reserved;

    /* Original calibration points for reference */
    touch_calib_point_t points[TOUCH_CALIB_POINTS];

    /* CRC32 for data integrity */
    uint32_t crc32;
} touch_calib_data_t;

/*
 * Run the touch calibration process
 *
 * Displays calibration UI, collects touch points, calculates
 * transformation parameters, and stores to flash.
 *
 * Returns: sectrue on success, secfalse on failure or user cancel
 */
secbool touch_calib_run(void);

/*
 * Load calibration data from flash
 *
 * Reads calibration data from flash and validates it.
 *
 * data: Pointer to calibration data structure to fill
 * Returns: sectrue if valid calibration found, secfalse otherwise
 */
secbool touch_calib_load(touch_calib_data_t *data);

/*
 * Save calibration data to flash
 *
 * Erases calibration sector and writes new data.
 *
 * data: Pointer to calibration data to save
 * Returns: sectrue on success, secfalse on failure
 */
secbool touch_calib_save(const touch_calib_data_t *data);

/*
 * Check if valid calibration exists
 *
 * Returns: sectrue if valid calibration data exists in flash
 */
secbool touch_calib_is_valid(void);

/*
 * Apply calibration to raw touch coordinates
 *
 * Transforms raw touch values to screen coordinates using
 * the stored calibration parameters.
 *
 * raw_x, raw_y: Raw touch values from STMPE811
 * screen_x, screen_y: Output screen coordinates
 * Returns: sectrue if calibration was applied, secfalse if using defaults
 */
secbool touch_calib_apply(uint16_t raw_x, uint16_t raw_y,
                          uint16_t *screen_x, uint16_t *screen_y);

/*
 * Get default calibration data (hardcoded values)
 *
 * Returns the default calibration parameters matching the
 * original hardcoded values in stmpe811.c
 */
void touch_calib_get_defaults(touch_calib_data_t *data);

/*
 * Erase calibration data from flash
 *
 * Returns: sectrue on success
 */
secbool touch_calib_erase(void);

#endif /* TREZORHAL_TOUCH_CALIB_H */
