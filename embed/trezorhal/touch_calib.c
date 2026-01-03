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

#include STM32_HAL_H

#include <string.h>
#include "touch_calib.h"
#include "touch.h"
#include "flash.h"
#include "display.h"
#include "mini_printf.h"

/* Colors for calibration UI */
#define COLOR_CALIB_BG      RGB16(0x00, 0x00, 0x00)  /* Black background */
#define COLOR_CALIB_TARGET  RGB16(0xFF, 0x00, 0x00)  /* Red crosshair */
#define COLOR_CALIB_DONE    RGB16(0x00, 0xFF, 0x00)  /* Green for completed */
#define COLOR_CALIB_TEXT    RGB16(0xFF, 0xFF, 0xFF)  /* White text */

/* Crosshair size */
#define CROSSHAIR_SIZE      20
#define CROSSHAIR_THICKNESS 2

/* Touch timeout in ms */
#define TOUCH_TIMEOUT_MS    30000

/* Debounce delay in ms */
#define TOUCH_DEBOUNCE_MS   100

/* Minimum samples to average */
#define TOUCH_SAMPLES       8

/* Cached calibration data */
static touch_calib_data_t g_calib_data;
static bool g_calib_loaded = false;
static bool g_calib_valid = false;

/* CRC32 lookup table */
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7D43, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAEF15D4A, 0xD9F66DDC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBBDEC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD706B3,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

/* Calculate CRC32 */
static uint32_t calc_crc32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;

    while (len--) {
        crc = crc32_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

/* Draw a crosshair at the specified position */
static void draw_crosshair(int x, int y, uint16_t color) {
    /* Horizontal line */
    display_bar(x - CROSSHAIR_SIZE, y - CROSSHAIR_THICKNESS/2,
                CROSSHAIR_SIZE * 2, CROSSHAIR_THICKNESS, color);

    /* Vertical line */
    display_bar(x - CROSSHAIR_THICKNESS/2, y - CROSSHAIR_SIZE,
                CROSSHAIR_THICKNESS, CROSSHAIR_SIZE * 2, color);

    /* Center dot */
    display_bar(x - 2, y - 2, 4, 4, color);
}

/* Draw calibration screen with current point highlighted */
static void draw_calib_screen(int point_index, const char *message) {
    /* Clear screen */
    display_bar(0, 0, DISPLAY_RESX, DISPLAY_RESY, COLOR_CALIB_BG);

    /* Define target positions */
    static const struct { int x, y; const char *name; } targets[TOUCH_CALIB_POINTS] = {
        { TOUCH_CALIB_MARGIN, TOUCH_CALIB_MARGIN, "Top-Left" },
        { TOUCH_CALIB_SCREEN_W - TOUCH_CALIB_MARGIN, TOUCH_CALIB_MARGIN, "Top-Right" },
        { TOUCH_CALIB_MARGIN, TOUCH_CALIB_SCREEN_H - TOUCH_CALIB_MARGIN, "Bottom-Left" },
        { TOUCH_CALIB_SCREEN_W - TOUCH_CALIB_MARGIN, TOUCH_CALIB_SCREEN_H - TOUCH_CALIB_MARGIN, "Bottom-Right" },
        { TOUCH_CALIB_SCREEN_W / 2, TOUCH_CALIB_SCREEN_H / 2, "Center" },
    };

    /* Draw all targets with appropriate colors */
    for (int i = 0; i < TOUCH_CALIB_POINTS; i++) {
        uint16_t color;
        if (i < point_index) {
            color = COLOR_CALIB_DONE;  /* Completed points in green */
        } else if (i == point_index) {
            color = COLOR_CALIB_TARGET;  /* Current point in red */
        } else {
            color = RGB16(0x40, 0x40, 0x40);  /* Future points dim */
        }
        draw_crosshair(targets[i].x, targets[i].y, color);
    }

    /* Draw instructions */
    if (point_index < TOUCH_CALIB_POINTS) {
        char text[64];
        mini_snprintf(text, sizeof(text), "Touch %s", targets[point_index].name);
        display_text_center(DISPLAY_RESX / 2, DISPLAY_RESY / 2 + 60, text, -1,
                           FONT_NORMAL, COLOR_CALIB_TEXT, COLOR_CALIB_BG);

        display_text_center(DISPLAY_RESX / 2, DISPLAY_RESY / 2 + 90,
                           "Point", -1, FONT_NORMAL, COLOR_CALIB_TEXT, COLOR_CALIB_BG);
    }

    /* Draw message if provided */
    if (message != NULL) {
        display_text_center(DISPLAY_RESX / 2, DISPLAY_RESY / 2 + 120, message, -1,
                           FONT_NORMAL, COLOR_CALIB_TEXT, COLOR_CALIB_BG);
    }

    /* Progress indicator */
    char progress[16];
    mini_snprintf(progress, sizeof(progress), "%d / %d", point_index, TOUCH_CALIB_POINTS);
    display_text_center(DISPLAY_RESX / 2, DISPLAY_RESY - 30, progress, -1,
                       FONT_NORMAL, COLOR_CALIB_TEXT, COLOR_CALIB_BG);

    display_refresh();
}

/* Wait for touch and get raw coordinates */
static bool wait_for_touch(uint16_t *raw_x, uint16_t *raw_y, uint32_t timeout_ms) {
    uint32_t start = HAL_GetTick();
    uint32_t sum_x = 0, sum_y = 0;
    int samples = 0;

    /* Wait for touch to start */
    while (!touch_is_detected()) {
        if ((HAL_GetTick() - start) > timeout_ms) {
            return false;
        }
        HAL_Delay(10);
    }

    /* Debounce */
    HAL_Delay(TOUCH_DEBOUNCE_MS);

    /* Collect samples while touched */
    while (touch_is_detected() && samples < TOUCH_SAMPLES) {
        uint16_t x, y;
        if (touch_read_raw(&x, &y)) {
            sum_x += x;
            sum_y += y;
            samples++;
        }
        HAL_Delay(20);
    }

    /* Wait for release */
    while (touch_is_detected()) {
        HAL_Delay(10);
    }

    /* Debounce release */
    HAL_Delay(TOUCH_DEBOUNCE_MS);

    if (samples > 0) {
        *raw_x = sum_x / samples;
        *raw_y = sum_y / samples;
        return true;
    }

    return false;
}

/* Calculate calibration parameters from collected points */
static void calculate_calibration(touch_calib_data_t *data) {
    /*
     * Use linear regression to find transformation parameters.
     * For each axis:
     *   screen = (raw - offset) * scale / SCALE_FACTOR
     *
     * We use the corner points to calculate the transformation.
     */

    /* Get corner points */
    touch_calib_point_t *tl = &data->points[CALIB_POINT_TOP_LEFT];
    touch_calib_point_t *tr = &data->points[CALIB_POINT_TOP_RIGHT];
    touch_calib_point_t *bl = &data->points[CALIB_POINT_BOTTOM_LEFT];
    touch_calib_point_t *br = &data->points[CALIB_POINT_BOTTOM_RIGHT];

    /* Calculate average raw values for edges */
    int32_t raw_x_left = ((int32_t)tl->raw_x + bl->raw_x) / 2;
    int32_t raw_x_right = ((int32_t)tr->raw_x + br->raw_x) / 2;
    int32_t raw_y_top = ((int32_t)tl->raw_y + tr->raw_y) / 2;
    int32_t raw_y_bottom = ((int32_t)bl->raw_y + br->raw_y) / 2;

    /* Screen coordinates for edges */
    int32_t screen_x_left = TOUCH_CALIB_MARGIN;
    int32_t screen_x_right = TOUCH_CALIB_SCREEN_W - TOUCH_CALIB_MARGIN;
    int32_t screen_y_top = TOUCH_CALIB_MARGIN;
    int32_t screen_y_bottom = TOUCH_CALIB_SCREEN_H - TOUCH_CALIB_MARGIN;

    /* Check for axis inversion */
    data->x_invert = (raw_x_left > raw_x_right) ? 1 : 0;
    data->y_invert = (raw_y_top > raw_y_bottom) ? 1 : 0;
    data->xy_swap = 0;  /* Not implemented yet */

    /* Handle inversion for calculation */
    if (data->x_invert) {
        int32_t tmp = raw_x_left;
        raw_x_left = raw_x_right;
        raw_x_right = tmp;
    }
    if (data->y_invert) {
        int32_t tmp = raw_y_top;
        raw_y_top = raw_y_bottom;
        raw_y_bottom = tmp;
    }

    /* Calculate scale factors (fixed-point) */
    int32_t raw_x_range = raw_x_right - raw_x_left;
    int32_t raw_y_range = raw_y_bottom - raw_y_top;
    int32_t screen_x_range = screen_x_right - screen_x_left;
    int32_t screen_y_range = screen_y_bottom - screen_y_top;

    if (raw_x_range != 0) {
        data->x_scale = (screen_x_range * TOUCH_CALIB_SCALE_FACTOR) / raw_x_range;
    } else {
        data->x_scale = TOUCH_CALIB_SCALE_FACTOR;
    }

    if (raw_y_range != 0) {
        data->y_scale = (screen_y_range * TOUCH_CALIB_SCALE_FACTOR) / raw_y_range;
    } else {
        data->y_scale = TOUCH_CALIB_SCALE_FACTOR;
    }

    /* Calculate offsets */
    /* screen = (raw - offset) * scale / SCALE_FACTOR */
    /* offset = raw - (screen * SCALE_FACTOR / scale) */
    data->x_offset = raw_x_left - (screen_x_left * TOUCH_CALIB_SCALE_FACTOR / data->x_scale);
    data->y_offset = raw_y_top - (screen_y_top * TOUCH_CALIB_SCALE_FACTOR / data->y_scale);
}

void touch_calib_get_defaults(touch_calib_data_t *data) {
    memset(data, 0, sizeof(touch_calib_data_t));
    data->magic = TOUCH_CALIB_MAGIC;
    data->version = TOUCH_CALIB_VERSION;

    /*
     * Default calibration based on original stmpe811.c values:
     * Y: y -= 360; yr = y / 11; yr = 320 - yr (inverted)
     * X: x = 3870 - x (or 3800); xr = x / 15
     */

    /* These are approximate values derived from the original constants */
    data->x_offset = 150;   /* Raw X at screen X=0 after inversion */
    data->x_scale = (TOUCH_CALIB_SCALE_FACTOR * TOUCH_CALIB_SCREEN_W) / 3600;
    data->y_offset = 360;
    data->y_scale = (TOUCH_CALIB_SCALE_FACTOR * TOUCH_CALIB_SCREEN_H) / 3520;
    data->x_invert = 1;
    data->y_invert = 1;
    data->xy_swap = 0;
}

secbool touch_calib_load(touch_calib_data_t *data) {
    /* Read from flash */
    const void *addr = flash_get_address(FLASH_SECTOR_CALIB, 0, sizeof(touch_calib_data_t));
    if (addr == NULL) {
        return secfalse;
    }

    memcpy(data, addr, sizeof(touch_calib_data_t));

    /* Validate magic and version */
    if (data->magic != TOUCH_CALIB_MAGIC) {
        return secfalse;
    }

    if (data->version != TOUCH_CALIB_VERSION) {
        return secfalse;
    }

    /* Validate CRC */
    uint32_t stored_crc = data->crc32;
    data->crc32 = 0;
    uint32_t calc_crc = calc_crc32(data, sizeof(touch_calib_data_t));
    data->crc32 = stored_crc;

    if (stored_crc != calc_crc) {
        return secfalse;
    }

    return sectrue;
}

secbool touch_calib_save(const touch_calib_data_t *data) {
    touch_calib_data_t save_data;
    memcpy(&save_data, data, sizeof(touch_calib_data_t));

    /* Set magic and version */
    save_data.magic = TOUCH_CALIB_MAGIC;
    save_data.version = TOUCH_CALIB_VERSION;

    /* Calculate CRC (with crc32 field zeroed) */
    save_data.crc32 = 0;
    save_data.crc32 = calc_crc32(&save_data, sizeof(touch_calib_data_t));

    /* Erase sector */
    if (flash_erase(FLASH_SECTOR_CALIB) != sectrue) {
        return secfalse;
    }

    /* Unlock flash for writing */
    if (flash_unlock_write() != sectrue) {
        return secfalse;
    }

    /* Write data word by word */
    const uint32_t *src = (const uint32_t *)&save_data;
    size_t words = sizeof(touch_calib_data_t) / sizeof(uint32_t);

    secbool lock_result;

    for (size_t i = 0; i < words; i++) {
        if (flash_write_word(FLASH_SECTOR_CALIB, i * sizeof(uint32_t), src[i]) != sectrue) {
            lock_result = flash_lock_write();
            (void)lock_result;
            return secfalse;
        }
    }

    /* Handle any remaining bytes */
    size_t remaining = sizeof(touch_calib_data_t) % sizeof(uint32_t);
    if (remaining > 0) {
        uint32_t last_word = 0xFFFFFFFF;
        memcpy(&last_word, (const uint8_t *)&save_data + words * sizeof(uint32_t), remaining);
        if (flash_write_word(FLASH_SECTOR_CALIB, words * sizeof(uint32_t), last_word) != sectrue) {
            lock_result = flash_lock_write();
            (void)lock_result;
            return secfalse;
        }
    }

    lock_result = flash_lock_write();
    (void)lock_result;

    /* Invalidate cache */
    g_calib_loaded = false;

    return sectrue;
}

secbool touch_calib_is_valid(void) {
    if (!g_calib_loaded) {
        g_calib_valid = (touch_calib_load(&g_calib_data) == sectrue);
        g_calib_loaded = true;
    }
    return g_calib_valid ? sectrue : secfalse;
}

secbool touch_calib_apply(uint16_t raw_x, uint16_t raw_y,
                          uint16_t *screen_x, uint16_t *screen_y) {
    touch_calib_data_t *data;

    /* Load calibration if not already loaded */
    if (!g_calib_loaded) {
        g_calib_valid = (touch_calib_load(&g_calib_data) == sectrue);
        g_calib_loaded = true;
    }

    /* Use stored or default calibration */
    if (g_calib_valid) {
        data = &g_calib_data;
    } else {
        /* Use defaults if no valid calibration */
        static touch_calib_data_t defaults;
        static bool defaults_init = false;
        if (!defaults_init) {
            touch_calib_get_defaults(&defaults);
            defaults_init = true;
        }
        data = &defaults;
    }

    /* Apply transformation */
    int32_t rx = raw_x;
    int32_t ry = raw_y;

    /* Apply inversion */
    if (data->x_invert) {
        rx = 4095 - rx;  /* 12-bit ADC */
    }
    if (data->y_invert) {
        ry = 4095 - ry;
    }

    /* Apply offset and scale */
    int32_t sx = ((rx - data->x_offset) * data->x_scale) >> TOUCH_CALIB_SCALE_BITS;
    int32_t sy = ((ry - data->y_offset) * data->y_scale) >> TOUCH_CALIB_SCALE_BITS;

    /* Clamp to screen bounds */
    if (sx < 0) sx = 0;
    if (sx >= TOUCH_CALIB_SCREEN_W) sx = TOUCH_CALIB_SCREEN_W - 1;
    if (sy < 0) sy = 0;
    if (sy >= TOUCH_CALIB_SCREEN_H) sy = TOUCH_CALIB_SCREEN_H - 1;

    *screen_x = (uint16_t)sx;
    *screen_y = (uint16_t)sy;

    return g_calib_valid ? sectrue : secfalse;
}

secbool touch_calib_erase(void) {
    if (flash_erase(FLASH_SECTOR_CALIB) != sectrue) {
        return secfalse;
    }

    g_calib_loaded = false;
    g_calib_valid = false;

    return sectrue;
}

secbool touch_calib_run(void) {
    touch_calib_data_t calib;
    memset(&calib, 0, sizeof(calib));

    /* Define target screen coordinates */
    static const struct { uint16_t x, y; } targets[TOUCH_CALIB_POINTS] = {
        { TOUCH_CALIB_MARGIN, TOUCH_CALIB_MARGIN },
        { TOUCH_CALIB_SCREEN_W - TOUCH_CALIB_MARGIN, TOUCH_CALIB_MARGIN },
        { TOUCH_CALIB_MARGIN, TOUCH_CALIB_SCREEN_H - TOUCH_CALIB_MARGIN },
        { TOUCH_CALIB_SCREEN_W - TOUCH_CALIB_MARGIN, TOUCH_CALIB_SCREEN_H - TOUCH_CALIB_MARGIN },
        { TOUCH_CALIB_SCREEN_W / 2, TOUCH_CALIB_SCREEN_H / 2 },
    };

    /* Collect calibration points */
    for (int i = 0; i < TOUCH_CALIB_POINTS; i++) {
        /* Draw calibration screen */
        draw_calib_screen(i, NULL);

        /* Wait for touch */
        uint16_t raw_x, raw_y;
        if (!wait_for_touch(&raw_x, &raw_y, TOUCH_TIMEOUT_MS)) {
            /* Timeout - show error and abort */
            draw_calib_screen(i, "Timeout!");
            HAL_Delay(2000);
            return secfalse;
        }

        /* Store calibration point */
        calib.points[i].screen_x = targets[i].x;
        calib.points[i].screen_y = targets[i].y;
        calib.points[i].raw_x = raw_x;
        calib.points[i].raw_y = raw_y;

        /* Brief visual feedback */
        draw_calib_screen(i + 1, NULL);
        HAL_Delay(300);
    }

    /* Calculate calibration parameters */
    calculate_calibration(&calib);

    /* Show saving message */
    display_bar(0, 0, DISPLAY_RESX, DISPLAY_RESY, COLOR_CALIB_BG);
    display_text_center(DISPLAY_RESX / 2, DISPLAY_RESY / 2, "Saving...", -1,
                       FONT_NORMAL, COLOR_CALIB_TEXT, COLOR_CALIB_BG);
    display_refresh();

    /* Save to flash */
    if (touch_calib_save(&calib) != sectrue) {
        display_text_center(DISPLAY_RESX / 2, DISPLAY_RESY / 2 + 30, "Save failed!", -1,
                           FONT_NORMAL, COLOR_CALIB_TARGET, COLOR_CALIB_BG);
        display_refresh();
        HAL_Delay(2000);
        return secfalse;
    }

    /* Show success */
    display_bar(0, 0, DISPLAY_RESX, DISPLAY_RESY, COLOR_CALIB_BG);
    display_text_center(DISPLAY_RESX / 2, DISPLAY_RESY / 2, "Calibration", -1,
                       FONT_NORMAL, COLOR_CALIB_DONE, COLOR_CALIB_BG);
    display_text_center(DISPLAY_RESX / 2, DISPLAY_RESY / 2 + 30, "Complete!", -1,
                       FONT_NORMAL, COLOR_CALIB_DONE, COLOR_CALIB_BG);
    display_refresh();
    HAL_Delay(2000);

    return sectrue;
}
