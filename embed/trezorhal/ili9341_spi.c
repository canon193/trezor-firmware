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
 * ILI9341 display controller SPI driver for STM32F429I-DISC1.
 * The ILI9341 controller requires SPI initialization for configuration,
 * while pixel data is transferred via the LTDC RGB interface.
 */

#include STM32_HAL_H

#include "ili9341_spi.h"

// SPI5 configuration for ILI9341
#define LCD_SPIx                      SPI5
#define LCD_SPIx_CLK_ENABLE()         __HAL_RCC_SPI5_CLK_ENABLE()
#define LCD_SPIx_GPIO_PORT            GPIOF
#define LCD_SPIx_AF                   GPIO_AF5_SPI5
#define LCD_SPIx_GPIO_CLK_ENABLE()    __HAL_RCC_GPIOF_CLK_ENABLE()
#define LCD_SPIx_SCK_PIN              GPIO_PIN_7   // PF7
#define LCD_SPIx_MISO_PIN             GPIO_PIN_8   // PF8
#define LCD_SPIx_MOSI_PIN             GPIO_PIN_9   // PF9
#define LCD_SPIx_TIMEOUT              ((uint32_t)0x1000)

// LCD control pins
#define LCD_CS_PIN                    GPIO_PIN_2   // PC2
#define LCD_CS_PORT                   GPIOC
#define LCD_CS_CLK_ENABLE()           __HAL_RCC_GPIOC_CLK_ENABLE()

#define LCD_WRX_PIN                   GPIO_PIN_13  // PD13 (D/C pin)
#define LCD_WRX_PORT                  GPIOD
#define LCD_WRX_CLK_ENABLE()          __HAL_RCC_GPIOD_CLK_ENABLE()

// Control pin macros
#define LCD_CS_LOW()    HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_RESET)
#define LCD_CS_HIGH()   HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET)
#define LCD_WRX_LOW()   HAL_GPIO_WritePin(LCD_WRX_PORT, LCD_WRX_PIN, GPIO_PIN_RESET)
#define LCD_WRX_HIGH()  HAL_GPIO_WritePin(LCD_WRX_PORT, LCD_WRX_PIN, GPIO_PIN_SET)

// ILI9341 LCD commands
#define LCD_SWRESET           0x01
#define LCD_SLEEP_OUT         0x11
#define LCD_DISPLAY_OFF       0x28
#define LCD_DISPLAY_ON        0x29
#define LCD_COLUMN_ADDR       0x2A
#define LCD_PAGE_ADDR         0x2B
#define LCD_GRAM              0x2C
#define LCD_MAC               0x36
#define LCD_PIXEL_FORMAT      0x3A
#define LCD_RGB_INTERFACE     0xB0
#define LCD_FRMCTR1           0xB1
#define LCD_DFC               0xB6
#define LCD_POWER1            0xC0
#define LCD_POWER2            0xC1
#define LCD_VCOM1             0xC5
#define LCD_VCOM2             0xC7
#define LCD_POWERA            0xCB
#define LCD_POWERB            0xCF
#define LCD_PGAMMA            0xE0
#define LCD_NGAMMA            0xE1
#define LCD_DTCA              0xE8
#define LCD_DTCB              0xEA
#define LCD_POWER_SEQ         0xED
#define LCD_3GAMMA_EN         0xF2
#define LCD_INTERFACE         0xF6
#define LCD_PRC               0xF7
#define LCD_GAMMA             0x26

static SPI_HandleTypeDef spi_handle;

static void spi_init(void) {
    if (HAL_SPI_GetState(&spi_handle) == HAL_SPI_STATE_RESET) {
        spi_handle.Instance = LCD_SPIx;
        // SPI baudrate: PCLK2/16 = 90/16 = 5.625 MHz (ILI9341 max is 10 MHz write)
        spi_handle.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
        spi_handle.Init.Direction = SPI_DIRECTION_2LINES;
        spi_handle.Init.CLKPhase = SPI_PHASE_1EDGE;
        spi_handle.Init.CLKPolarity = SPI_POLARITY_LOW;
        spi_handle.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLED;
        spi_handle.Init.CRCPolynomial = 7;
        spi_handle.Init.DataSize = SPI_DATASIZE_8BIT;
        spi_handle.Init.FirstBit = SPI_FIRSTBIT_MSB;
        spi_handle.Init.NSS = SPI_NSS_SOFT;
        spi_handle.Init.TIMode = SPI_TIMODE_DISABLED;
        spi_handle.Init.Mode = SPI_MODE_MASTER;

        HAL_SPI_Init(&spi_handle);
    }
}

static void spi_write(uint8_t value) {
    HAL_SPI_Transmit(&spi_handle, &value, 1, LCD_SPIx_TIMEOUT);
}

static void ili9341_write_reg(uint8_t reg) {
    LCD_WRX_LOW();   // Command mode
    LCD_CS_LOW();
    spi_write(reg);
    LCD_CS_HIGH();
}

static void ili9341_write_data(uint8_t data) {
    LCD_WRX_HIGH();  // Data mode
    LCD_CS_LOW();
    spi_write(data);
    LCD_CS_HIGH();
}

static void ili9341_gpio_init(void) {
    GPIO_InitTypeDef gpio = {0};

    // Enable GPIO clocks
    LCD_CS_CLK_ENABLE();
    LCD_WRX_CLK_ENABLE();
    LCD_SPIx_GPIO_CLK_ENABLE();

    // Configure CS pin (PC2)
    gpio.Pin = LCD_CS_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(LCD_CS_PORT, &gpio);

    // Configure D/C (WRX) pin (PD13)
    gpio.Pin = LCD_WRX_PIN;
    HAL_GPIO_Init(LCD_WRX_PORT, &gpio);

    // Set CS high (deselected)
    LCD_CS_HIGH();

    // Enable SPI5 clock
    LCD_SPIx_CLK_ENABLE();

    // Configure SPI pins: SCK (PF7), MISO (PF8), MOSI (PF9)
    gpio.Pin = LCD_SPIx_SCK_PIN | LCD_SPIx_MISO_PIN | LCD_SPIx_MOSI_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_MEDIUM;
    gpio.Alternate = LCD_SPIx_AF;
    HAL_GPIO_Init(LCD_SPIx_GPIO_PORT, &gpio);

    spi_init();
}

void ili9341_init(void) {
    ili9341_gpio_init();

    ili9341_write_reg(LCD_DISPLAY_OFF);

    // Power control settings
    ili9341_write_reg(0xCA);
    ili9341_write_data(0xC3);
    ili9341_write_data(0x08);
    ili9341_write_data(0x50);

    ili9341_write_reg(LCD_POWERB);
    ili9341_write_data(0x00);
    ili9341_write_data(0xC1);
    ili9341_write_data(0x30);

    ili9341_write_reg(LCD_POWER_SEQ);
    ili9341_write_data(0x64);
    ili9341_write_data(0x03);
    ili9341_write_data(0x12);
    ili9341_write_data(0x81);

    ili9341_write_reg(LCD_DTCA);
    ili9341_write_data(0x85);
    ili9341_write_data(0x00);
    ili9341_write_data(0x78);

    ili9341_write_reg(LCD_POWERA);
    ili9341_write_data(0x39);
    ili9341_write_data(0x2C);
    ili9341_write_data(0x00);
    ili9341_write_data(0x34);
    ili9341_write_data(0x02);

    ili9341_write_reg(LCD_PRC);
    ili9341_write_data(0x20);

    ili9341_write_reg(LCD_DTCB);
    ili9341_write_data(0x00);
    ili9341_write_data(0x00);

    ili9341_write_reg(LCD_FRMCTR1);
    ili9341_write_data(0x00);
    ili9341_write_data(0x1B);

    ili9341_write_reg(LCD_DFC);
    ili9341_write_data(0x0A);
    ili9341_write_data(0xA2);

    ili9341_write_reg(LCD_POWER1);
    ili9341_write_data(0x10);

    ili9341_write_reg(LCD_POWER2);
    ili9341_write_data(0x10);

    ili9341_write_reg(LCD_VCOM1);
    ili9341_write_data(0x45);
    ili9341_write_data(0x15);

    ili9341_write_reg(LCD_VCOM2);
    ili9341_write_data(0x90);

    ili9341_write_reg(LCD_MAC);
    ili9341_write_data(0xC8);

    ili9341_write_reg(LCD_3GAMMA_EN);
    ili9341_write_data(0x00);

    ili9341_write_reg(LCD_RGB_INTERFACE);
    ili9341_write_data(0xC2);

    ili9341_write_reg(LCD_DFC);
    ili9341_write_data(0x0A);
    ili9341_write_data(0xA7);
    ili9341_write_data(0x27);
    ili9341_write_data(0x04);

    // Column address set (0-239)
    ili9341_write_reg(LCD_COLUMN_ADDR);
    ili9341_write_data(0x00);
    ili9341_write_data(0x00);
    ili9341_write_data(0x00);
    ili9341_write_data(0xEF);

    // Page address set (0-319)
    ili9341_write_reg(LCD_PAGE_ADDR);
    ili9341_write_data(0x00);
    ili9341_write_data(0x00);
    ili9341_write_data(0x01);
    ili9341_write_data(0x3F);

    ili9341_write_reg(LCD_INTERFACE);
    ili9341_write_data(0x01);
    ili9341_write_data(0x00);
    ili9341_write_data(0x06);

    ili9341_write_reg(LCD_GRAM);
    HAL_Delay(200);

    ili9341_write_reg(LCD_GAMMA);
    ili9341_write_data(0x01);

    // Positive gamma correction
    ili9341_write_reg(LCD_PGAMMA);
    ili9341_write_data(0x0F);
    ili9341_write_data(0x29);
    ili9341_write_data(0x24);
    ili9341_write_data(0x0C);
    ili9341_write_data(0x0E);
    ili9341_write_data(0x09);
    ili9341_write_data(0x4E);
    ili9341_write_data(0x78);
    ili9341_write_data(0x3C);
    ili9341_write_data(0x09);
    ili9341_write_data(0x13);
    ili9341_write_data(0x05);
    ili9341_write_data(0x17);
    ili9341_write_data(0x11);
    ili9341_write_data(0x00);

    // Negative gamma correction
    ili9341_write_reg(LCD_NGAMMA);
    ili9341_write_data(0x00);
    ili9341_write_data(0x16);
    ili9341_write_data(0x1B);
    ili9341_write_data(0x04);
    ili9341_write_data(0x11);
    ili9341_write_data(0x07);
    ili9341_write_data(0x31);
    ili9341_write_data(0x33);
    ili9341_write_data(0x42);
    ili9341_write_data(0x05);
    ili9341_write_data(0x0C);
    ili9341_write_data(0x0A);
    ili9341_write_data(0x28);
    ili9341_write_data(0x2F);
    ili9341_write_data(0x0F);

    ili9341_write_reg(LCD_SLEEP_OUT);
    HAL_Delay(200);

    ili9341_write_reg(LCD_DISPLAY_ON);
    ili9341_write_reg(LCD_GRAM);
}
