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
 * Display driver for STM32F429I-DISC1 (D001 model).
 * Uses LTDC with framebuffer in external SDRAM.
 */

#include STM32_HAL_H

#include "sdram.h"
#include "display_ltdc.h"

#define LED_PWM_TIM_PERIOD (10000)

// Framebuffer in SDRAM (initialized directly to avoid runtime assignment issues)
static uint16_t *framebuffer = (uint16_t *)0xD0000000;

// Pixel window tracking for PIXELDATA macro
static struct {
    struct { uint16_t x, y; } start;
    struct { uint16_t x, y; } end;
    struct { uint16_t x, y; } pos;
} PIXELWINDOW;

// Write a pixel and advance position
static inline void PIXELDATA(uint16_t color) {
    if (framebuffer == NULL) return;

    // Write pixel to framebuffer (row-major, 240 pixels per row)
    framebuffer[PIXELWINDOW.pos.y * DISPLAY_RESX + PIXELWINDOW.pos.x] = color;

    // Advance position within window
    PIXELWINDOW.pos.x++;
    if (PIXELWINDOW.pos.x > PIXELWINDOW.end.x) {
        PIXELWINDOW.pos.x = PIXELWINDOW.start.x;
        PIXELWINDOW.pos.y++;
    }
}

static void display_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    PIXELWINDOW.start.x = x0;
    PIXELWINDOW.start.y = y0;
    PIXELWINDOW.end.x = x1;
    PIXELWINDOW.end.y = y1;
    PIXELWINDOW.pos.x = x0;
    PIXELWINDOW.pos.y = y0;
}

static void display_set_orientation(int degrees) {
    // LTDC handles orientation through layer configuration
    // For now, only support 0 degrees (native orientation)
    // The ILI9341 on STM32F429I-DISC1 is in fixed orientation
    (void)degrees;
    display_set_window(0, 0, DISPLAY_RESX - 1, DISPLAY_RESY - 1);
}

static void display_set_backlight(int val) {
    TIM1->CCR1 = LED_PWM_TIM_PERIOD * val / 255;
}

static void display_backlight_init(void) {
    GPIO_InitTypeDef GPIO_InitStructure;

    // LCD_PWM/PA7 (backlight control) - but on DISC1 it might be different
    // STM32F429I-DISC1 doesn't have a standard backlight PWM, using TIM1 CH1
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStructure.Pull = GPIO_NOPULL;
    GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStructure.Alternate = GPIO_AF1_TIM1;
    GPIO_InitStructure.Pin = GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStructure);

    // Enable PWM timer
    TIM_HandleTypeDef TIM1_Handle;
    TIM1_Handle.Instance = TIM1;
    TIM1_Handle.Init.Period = LED_PWM_TIM_PERIOD - 1;
    TIM1_Handle.Init.Prescaler = SystemCoreClock / 1000000 - 1;
    TIM1_Handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    TIM1_Handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    TIM1_Handle.Init.RepetitionCounter = 0;
    HAL_TIM_PWM_Init(&TIM1_Handle);

    TIM_OC_InitTypeDef TIM_OC_InitStructure;
    TIM_OC_InitStructure.Pulse = 0;
    TIM_OC_InitStructure.OCMode = TIM_OCMODE_PWM2;
    TIM_OC_InitStructure.OCPolarity = TIM_OCPOLARITY_HIGH;
    TIM_OC_InitStructure.OCFastMode = TIM_OCFAST_DISABLE;
    TIM_OC_InitStructure.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    TIM_OC_InitStructure.OCIdleState = TIM_OCIDLESTATE_SET;
    TIM_OC_InitStructure.OCNIdleState = TIM_OCNIDLESTATE_SET;
    HAL_TIM_PWM_ConfigChannel(&TIM1_Handle, &TIM_OC_InitStructure, TIM_CHANNEL_1);

    HAL_TIM_PWM_Start(&TIM1_Handle, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&TIM1_Handle, TIM_CHANNEL_1);
}

void display_init(void) {
    // Initialize SDRAM first (required for framebuffer)
    sdram_init();

    // Initialize backlight PWM
    display_backlight_init();
    display_backlight(0);

    // Initialize LTDC and ILI9341
    display_ltdc_init();

    // Get framebuffer pointer
    framebuffer = display_get_framebuffer();

    // Initialize pixel window to full screen
    display_set_window(0, 0, DISPLAY_RESX - 1, DISPLAY_RESY - 1);
}

void display_refresh(void) {
    // LTDC automatically refreshes from framebuffer
    // No explicit refresh needed, but we can add a sync point here
    // if tearing becomes an issue
}

void display_save(const char *prefix) {
    // Screenshot functionality - not implemented for this target
    (void)prefix;
}

static void __attribute__((unused)) display_sleep(void) {
    // Put display to sleep - could send ILI9341 sleep command via SPI
    // For now, just turn off backlight
    display_set_backlight(0);
}

static void __attribute__((unused)) display_unsleep(void) {
    // Wake display - ILI9341 should already be awake from init
    // Just restore backlight
}
