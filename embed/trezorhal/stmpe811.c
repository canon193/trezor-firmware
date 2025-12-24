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
 * STMPE811 resistive touch controller driver for STM32F429I-DISC1.
 * Uses I2C3 (PA8=SCL, PC9=SDA) to communicate with the touch controller.
 */

#include STM32_HAL_H

#include "stmpe811.h"

// STMPE811 I2C address (7-bit, shifted for HAL)
#define STMPE811_I2C_ADDR           (0x41 << 1)
#define I2C_TIMEOUT                 1000

// STMPE811 registers
#define STMPE811_REG_CHIP_ID        0x00
#define STMPE811_REG_SYS_CTRL1      0x03
#define STMPE811_REG_SYS_CTRL2      0x04
#define STMPE811_REG_INT_CTRL       0x09
#define STMPE811_REG_INT_EN         0x0A
#define STMPE811_REG_INT_STA        0x0B
#define STMPE811_REG_IO_AF          0x17
#define STMPE811_REG_ADC_CTRL1      0x20
#define STMPE811_REG_ADC_CTRL2      0x21
#define STMPE811_REG_TSC_CTRL       0x40
#define STMPE811_REG_TSC_CFG        0x41
#define STMPE811_REG_FIFO_TH        0x4A
#define STMPE811_REG_FIFO_STA       0x4B
#define STMPE811_REG_FIFO_SIZE      0x4C
#define STMPE811_REG_TSC_DATA_XYZ   0x52
#define STMPE811_REG_TSC_FRACT_XYZ  0x56
#define STMPE811_REG_TSC_DATA_INC   0x57
#define STMPE811_REG_TSC_DATA_NON_INC 0xD7
#define STMPE811_REG_TSC_I_DRIVE    0x58

// STMPE811 bit definitions
#define STMPE811_TS_CTRL_STATUS     0x80
#define STMPE811_TS_CTRL_ENABLE     0x01

// Touch IO pins (for alternate function)
#define STMPE811_TOUCH_IO_ALL       0xF0  // Pins 4-7 used for touch

static I2C_HandleTypeDef i2c_handle;

static void i2c_gpio_init(void) {
    GPIO_InitTypeDef gpio = {0};

    // Enable GPIO clocks
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /*
     * I2C3 GPIO configuration on STM32F429I-DISC1:
     * PA8 = I2C3_SCL (AF4)
     * PC9 = I2C3_SDA (AF4)
     */

    // Configure PA8 for I2C3_SCL
    gpio.Pin = GPIO_PIN_8;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF4_I2C3;
    HAL_GPIO_Init(GPIOA, &gpio);

    // Configure PC9 for I2C3_SDA
    gpio.Pin = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOC, &gpio);
}

static uint8_t stmpe811_read_reg(uint8_t reg) {
    uint8_t value = 0;
    HAL_I2C_Mem_Read(&i2c_handle, STMPE811_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                     &value, 1, I2C_TIMEOUT);
    return value;
}

static void stmpe811_write_reg(uint8_t reg, uint8_t value) {
    HAL_I2C_Mem_Write(&i2c_handle, STMPE811_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                      &value, 1, I2C_TIMEOUT);
}

static void stmpe811_read_multiple(uint8_t reg, uint8_t *data, uint16_t len) {
    HAL_I2C_Mem_Read(&i2c_handle, STMPE811_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                     data, len, I2C_TIMEOUT);
}

void stmpe811_init(void) {
    // Initialize I2C GPIO
    i2c_gpio_init();

    // Enable I2C3 clock
    __HAL_RCC_I2C3_CLK_ENABLE();

    // Configure I2C3
    i2c_handle.Instance = I2C3;
    i2c_handle.Init.ClockSpeed = 400000;  // 400 kHz
    i2c_handle.Init.DutyCycle = I2C_DUTYCYCLE_2;
    i2c_handle.Init.OwnAddress1 = 0;
    i2c_handle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    i2c_handle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    i2c_handle.Init.OwnAddress2 = 0;
    i2c_handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    i2c_handle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    HAL_I2C_Init(&i2c_handle);

    // Software reset the STMPE811
    stmpe811_write_reg(STMPE811_REG_SYS_CTRL1, 0x02);  // Soft reset
    HAL_Delay(10);
    stmpe811_write_reg(STMPE811_REG_SYS_CTRL1, 0x00);  // Exit reset
    HAL_Delay(2);

    // Disable IO function to allow TS function
    uint8_t mode = stmpe811_read_reg(STMPE811_REG_SYS_CTRL2);
    mode &= ~0x04;  // Clear IO_FCT bit
    stmpe811_write_reg(STMPE811_REG_SYS_CTRL2, mode);

    // Enable touch screen pins alternate function
    uint8_t af = stmpe811_read_reg(STMPE811_REG_IO_AF);
    af &= ~STMPE811_TOUCH_IO_ALL;  // Enable AF for touch pins
    stmpe811_write_reg(STMPE811_REG_IO_AF, af);

    // Enable TS and ADC functions
    mode &= ~0x03;  // Clear TS_FCT and ADC_FCT bits
    stmpe811_write_reg(STMPE811_REG_SYS_CTRL2, mode);

    // Configure ADC
    stmpe811_write_reg(STMPE811_REG_ADC_CTRL1, 0x49);  // Sample time, 12-bit, internal ref
    HAL_Delay(2);
    stmpe811_write_reg(STMPE811_REG_ADC_CTRL2, 0x01);  // ADC clock 3.25 MHz

    // Configure touch screen
    // 4 samples averaging, 500us touch delay, 500us panel delay
    stmpe811_write_reg(STMPE811_REG_TSC_CFG, 0x9A);

    // FIFO threshold = 1 (single point)
    stmpe811_write_reg(STMPE811_REG_FIFO_TH, 0x01);

    // Clear FIFO
    stmpe811_write_reg(STMPE811_REG_FIFO_STA, 0x01);
    stmpe811_write_reg(STMPE811_REG_FIFO_STA, 0x00);

    // Fractional part for XYZ
    stmpe811_write_reg(STMPE811_REG_TSC_FRACT_XYZ, 0x01);

    // Drive limit 50mA
    stmpe811_write_reg(STMPE811_REG_TSC_I_DRIVE, 0x01);

    // Enable TSC (XYZ mode, no window tracking)
    stmpe811_write_reg(STMPE811_REG_TSC_CTRL, 0x01);

    // Clear interrupt status
    stmpe811_write_reg(STMPE811_REG_INT_STA, 0xFF);

    HAL_Delay(2);
}

bool stmpe811_is_touched(void) {
    uint8_t ctrl = stmpe811_read_reg(STMPE811_REG_TSC_CTRL);
    if (ctrl & STMPE811_TS_CTRL_STATUS) {
        uint8_t fifo_size = stmpe811_read_reg(STMPE811_REG_FIFO_SIZE);
        return fifo_size > 0;
    }
    // Reset FIFO when not touched
    stmpe811_write_reg(STMPE811_REG_FIFO_STA, 0x01);
    stmpe811_write_reg(STMPE811_REG_FIFO_STA, 0x00);
    return false;
}

void stmpe811_get_state(stmpe811_state_t *state) {
    static uint16_t last_x = 0, last_y = 0;
    static bool last_detected = false;

    state->TouchDetected = last_detected;
    state->X = last_x;
    state->Y = last_y;

    uint8_t ctrl = stmpe811_read_reg(STMPE811_REG_TSC_CTRL);
    bool detected = (ctrl & STMPE811_TS_CTRL_STATUS) != 0;

    if (!detected) {
        state->TouchDetected = last_detected = false;
        return;
    }

    uint8_t fifo_size = stmpe811_read_reg(STMPE811_REG_FIFO_SIZE);
    if (fifo_size > 0) {
        uint8_t data[4];
        stmpe811_read_multiple(STMPE811_REG_TSC_DATA_NON_INC, data, 4);

        // Extract X and Y from packed data
        uint32_t raw = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
        uint16_t raw_x = (raw >> 20) & 0xFFF;
        uint16_t raw_y = (raw >> 8) & 0xFFF;

        // Calibration for STM32F429I-DISC1 touchscreen
        // Y calibration
        int16_t y = raw_y - 360;
        y = y / 11;
        if (y < 0) y = 0;
        if (y > 319) y = 319;
        y = 319 - y;  // Invert Y

        // X calibration
        int16_t x;
        if (raw_x <= 3000) {
            x = 3870 - raw_x;
        } else {
            x = 3800 - raw_x;
        }
        x = x / 15;
        if (x < 0) x = 0;
        if (x > 239) x = 239;

        // Apply threshold filter
        uint16_t xDiff = (x > last_x) ? (x - last_x) : (last_x - x);
        uint16_t yDiff = (y > last_y) ? (y - last_y) : (last_y - y);
        if (xDiff + yDiff > 5) {
            last_x = x;
            last_y = y;
        }

        last_detected = true;
        state->X = last_x;
        state->Y = last_y;

        // Clear FIFO
        stmpe811_write_reg(STMPE811_REG_FIFO_STA, 0x01);
        stmpe811_write_reg(STMPE811_REG_FIFO_STA, 0x00);
    }

    state->TouchDetected = last_detected;
}
