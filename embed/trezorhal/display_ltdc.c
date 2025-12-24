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
 * LTDC display driver for STM32F429I-DISC1.
 * Drives the ILI9341 240x320 LCD via LTDC RGB interface.
 */

#include STM32_HAL_H

#include <string.h>

#include "display_ltdc.h"
#include "ili9341_spi.h"
#include "sdram.h"

// Display resolution
#define DISPLAY_RESX 240
#define DISPLAY_RESY 320

// Frame buffer in external SDRAM
#define FRAME_BUFFER_ADDR  SDRAM_DEVICE_ADDR
#define FRAME_BUFFER_SIZE  (DISPLAY_RESX * DISPLAY_RESY * 2)  // RGB565

static LTDC_HandleTypeDef ltdc_handle;

static void ltdc_gpio_init(void) {
    GPIO_InitTypeDef gpio = {0};

    // Enable GPIO clocks
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /*
     * LTDC GPIO pin assignment for STM32F429I-DISC1:
     *
     * LCD_TFT R2 <-> PC10   LCD_TFT G2 <-> PA06   LCD_TFT B2 <-> PD06
     * LCD_TFT R3 <-> PB00   LCD_TFT G3 <-> PG10   LCD_TFT B3 <-> PG11
     * LCD_TFT R4 <-> PA11   LCD_TFT G4 <-> PB10   LCD_TFT B4 <-> PG12
     * LCD_TFT R5 <-> PA12   LCD_TFT G5 <-> PB11   LCD_TFT B5 <-> PA03
     * LCD_TFT R6 <-> PB01   LCD_TFT G6 <-> PC07   LCD_TFT B6 <-> PB08
     * LCD_TFT R7 <-> PG06   LCD_TFT G7 <-> PD03   LCD_TFT B7 <-> PB09
     *
     * LCD_TFT HSYNC <-> PC06
     * LCD_TFT VSYNC <-> PA04
     * LCD_TFT CLK   <-> PG07
     * LCD_TFT DE    <-> PF10
     */

    // Common GPIO configuration for AF14 (LTDC)
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF14_LTDC;

    // GPIOA: PA3 (B5), PA4 (VSYNC), PA6 (G2), PA11 (R4), PA12 (R5)
    gpio.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_6 | GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOA, &gpio);

    // GPIOB: PB8 (B6), PB9 (B7), PB10 (G4), PB11 (G5) - AF14
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
    HAL_GPIO_Init(GPIOB, &gpio);

    // GPIOC: PC6 (HSYNC), PC7 (G6), PC10 (R2)
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOC, &gpio);

    // GPIOD: PD3 (G7), PD6 (B2)
    gpio.Pin = GPIO_PIN_3 | GPIO_PIN_6;
    HAL_GPIO_Init(GPIOD, &gpio);

    // GPIOF: PF10 (DE)
    gpio.Pin = GPIO_PIN_10;
    HAL_GPIO_Init(GPIOF, &gpio);

    // GPIOG: PG6 (R7), PG7 (CLK), PG11 (B3)
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_11;
    HAL_GPIO_Init(GPIOG, &gpio);

    // GPIOB: PB0 (R3), PB1 (R6) - AF9 (different alternate function)
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    gpio.Alternate = GPIO_AF9_LTDC;
    HAL_GPIO_Init(GPIOB, &gpio);

    // GPIOG: PG10 (G3), PG12 (B4) - AF9
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOG, &gpio);
}

static void ltdc_clock_config(void) {
    RCC_PeriphCLKInitTypeDef periph_clk = {0};

    /*
     * PLLSAI configuration for LTDC pixel clock:
     * PLLSAI_VCO Input = HSE / PLL_M = 8 MHz / 4 = 2 MHz
     * PLLSAI_VCO Output = PLLSAI_VCO Input * PLLSAIN = 2 * 96 = 192 MHz
     * PLLLCDCLK = PLLSAI_VCO Output / PLLSAIR = 192 / 4 = 48 MHz
     * LTDC clock = PLLLCDCLK / RCC_PLLSAIDIVR_8 = 48 / 8 = 6 MHz
     *
     * Note: We use PLLSAIN=96 (not 192) because our PLLM=4 gives 2 MHz input,
     * whereas the reference used PLLM=8 giving 1 MHz input.
     */
    periph_clk.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    periph_clk.PLLSAI.PLLSAIN = 96;
    periph_clk.PLLSAI.PLLSAIR = 4;
    periph_clk.PLLSAIDivR = RCC_PLLSAIDIVR_8;
    HAL_RCCEx_PeriphCLKConfig(&periph_clk);
}

static void ltdc_layer_init(uint32_t fb_addr) {
    LTDC_LayerCfgTypeDef layer = {0};

    layer.WindowX0 = 0;
    layer.WindowX1 = DISPLAY_RESX;
    layer.WindowY0 = 0;
    layer.WindowY1 = DISPLAY_RESY;
    layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
    layer.FBStartAdress = fb_addr;
    layer.Alpha = 255;
    layer.Alpha0 = 0;
    layer.Backcolor.Blue = 0;
    layer.Backcolor.Green = 0;
    layer.Backcolor.Red = 0;
    layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
    layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
    layer.ImageWidth = DISPLAY_RESX;
    layer.ImageHeight = DISPLAY_RESY;

    HAL_LTDC_ConfigLayer(&ltdc_handle, &layer, 1);  // Layer 1

    // Enable dithering for better color gradients
    HAL_LTDC_EnableDither(&ltdc_handle);
}

void display_ltdc_init(void) {
    // Enable LTDC and DMA2D clocks
    __HAL_RCC_LTDC_CLK_ENABLE();
    __HAL_RCC_DMA2D_CLK_ENABLE();

    // Initialize GPIO pins for LTDC
    ltdc_gpio_init();

    // Configure PLLSAI for LTDC pixel clock
    ltdc_clock_config();

    // Initialize ILI9341 display controller via SPI
    ili9341_init();

    // Configure LTDC
    ltdc_handle.Instance = LTDC;

    /*
     * LTDC timing configuration for ILI9341 (from datasheet):
     *   HSYNC = 10 (9+1)
     *   HBP = 20 (29-10+1)
     *   Active Width = 240 (269-20-10+1)
     *   HFP = 10 (279-240-20-10+1)
     *
     *   VSYNC = 2 (1+1)
     *   VBP = 2 (3-2+1)
     *   Active Height = 320 (323-2-2+1)
     *   VFP = 4 (327-320-2-2+1)
     */
    ltdc_handle.Init.HorizontalSync = ILI9341_HSYNC;
    ltdc_handle.Init.VerticalSync = ILI9341_VSYNC;
    ltdc_handle.Init.AccumulatedHBP = ILI9341_HBP;
    ltdc_handle.Init.AccumulatedVBP = ILI9341_VBP;
    ltdc_handle.Init.AccumulatedActiveW = 269;
    ltdc_handle.Init.AccumulatedActiveH = 323;
    ltdc_handle.Init.TotalWidth = 279;
    ltdc_handle.Init.TotalHeigh = 327;

    // Background color (black)
    ltdc_handle.Init.Backcolor.Red = 0;
    ltdc_handle.Init.Backcolor.Green = 0;
    ltdc_handle.Init.Backcolor.Blue = 0;

    // Signal polarity
    ltdc_handle.Init.HSPolarity = LTDC_HSPOLARITY_AL;
    ltdc_handle.Init.VSPolarity = LTDC_VSPOLARITY_AL;
    ltdc_handle.Init.DEPolarity = LTDC_DEPOLARITY_AL;
    ltdc_handle.Init.PCPolarity = LTDC_PCPOLARITY_IPC;

    HAL_LTDC_Init(&ltdc_handle);

    // Initialize layer with framebuffer in SDRAM
    ltdc_layer_init(FRAME_BUFFER_ADDR);

    // Clear framebuffer to black
    memset((void *)FRAME_BUFFER_ADDR, 0, FRAME_BUFFER_SIZE);
}

uint16_t *display_get_framebuffer(void) {
    return (uint16_t *)FRAME_BUFFER_ADDR;
}
