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
 * I2C Bus driver for STM32F429I-DISC1 (D001)
 * Uses polling mode with HAL
 * Configured for I2C3: PA8=SCL, PC9=SDA
 *
 * IMPORTANT: This driver correctly handles Repeated START conditions
 * required for I2C register reads (TX register addr + RX data without STOP).
 */

#include STM32_HAL_H

#include <string.h>
#include "i2c_bus.h"

// I2C3 configuration for D001
#define I2C_INSTANCE        I2C3
#define I2C_BUS_SPEED       200000  // 200 kHz

// I2C bus structure
struct i2c_bus {
    I2C_HandleTypeDef handle;
    bool initialized;
};

// Single I2C bus instance for D001
static struct i2c_bus g_i2c_bus_instance;

// Unlock I2C bus by generating clock pulses if SDA is stuck low
// This matches the Latest firmware behavior
static void i2c_bus_unlock(void) {
    GPIO_InitTypeDef gpio = {0};

    // Configure SCL and SDA as GPIO outputs temporarily
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    // Set both high first
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);  // SCL
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_SET);  // SDA

    gpio.Pin = GPIO_PIN_8;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOC, &gpio);

    // If SDA is low, generate clock pulses to free it
    int clock_count = 16;
    while ((HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_9) == GPIO_PIN_RESET) &&
           (clock_count-- > 0)) {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);
        HAL_Delay(1);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
        HAL_Delay(1);
    }

    // Generate STOP condition: SDA low->high while SCL high
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_SET);
    HAL_Delay(1);
}

static void i2c_gpio_init(void) {
    GPIO_InitTypeDef gpio = {0};

    // Enable GPIO clocks
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    // Try to unlock bus if it's stuck
    i2c_bus_unlock();

    /*
     * I2C3 GPIO configuration on STM32F429I-DISC1:
     * PA8 = I2C3_SCL (AF4)
     * PC9 = I2C3_SDA (AF4)
     */

    // Configure PA8 for I2C3_SCL
    gpio.Pin = GPIO_PIN_8;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;  // Match Latest: GPIO_SPEED_FREQ_LOW
    gpio.Alternate = GPIO_AF4_I2C3;
    HAL_GPIO_Init(GPIOA, &gpio);

    // Configure PC9 for I2C3_SDA
    gpio.Pin = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOC, &gpio);
}

static void i2c_reset(struct i2c_bus* bus) {
    // Disable I2C first
    __HAL_I2C_DISABLE(&bus->handle);

    // Reset I2C peripheral
    __HAL_RCC_I2C3_FORCE_RESET();
    HAL_Delay(1);  // Small delay for reset to take effect
    __HAL_RCC_I2C3_RELEASE_RESET();
    HAL_Delay(1);

    // Clear HAL state
    bus->handle.ErrorCode = HAL_I2C_ERROR_NONE;
    bus->handle.State = HAL_I2C_STATE_RESET;
    bus->handle.Mode = HAL_I2C_MODE_NONE;

    // Reconfigure I2C
    bus->handle.Instance = I2C_INSTANCE;
    bus->handle.Init.ClockSpeed = I2C_BUS_SPEED;
    bus->handle.Init.DutyCycle = I2C_DUTYCYCLE_2;
    bus->handle.Init.OwnAddress1 = 0;
    bus->handle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    bus->handle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    bus->handle.Init.OwnAddress2 = 0;
    bus->handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    bus->handle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    HAL_I2C_Init(&bus->handle);
}

static bool i2c_init(struct i2c_bus* bus) {
    // Always do full initialization - peripheral may be in unknown state
    // after jumping from bootloader to firmware

    // Enable I2C3 clock first
    __HAL_RCC_I2C3_CLK_ENABLE();

    // Force reset I2C peripheral to ensure clean state
    __HAL_RCC_I2C3_FORCE_RESET();
    HAL_Delay(1);
    __HAL_RCC_I2C3_RELEASE_RESET();
    HAL_Delay(1);

    // Clear any previous HAL state
    memset(&bus->handle, 0, sizeof(bus->handle));

    // Initialize GPIO (includes bus unlock)
    i2c_gpio_init();

    // Configure I2C3
    bus->handle.Instance = I2C_INSTANCE;
    bus->handle.Init.ClockSpeed = I2C_BUS_SPEED;
    bus->handle.Init.DutyCycle = I2C_DUTYCYCLE_2;
    bus->handle.Init.OwnAddress1 = 0;
    bus->handle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    bus->handle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    bus->handle.Init.OwnAddress2 = 0;
    bus->handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    bus->handle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&bus->handle) != HAL_OK) {
        return false;
    }

    bus->initialized = true;
    return true;
}

i2c_bus_t* i2c_bus_open(uint8_t bus_index) {
    // Only bus 0 (I2C3) is supported on D001
    if (bus_index != 0) {
        return NULL;
    }

    struct i2c_bus* bus = &g_i2c_bus_instance;

    if (!i2c_init(bus)) {
        return NULL;
    }

    return bus;
}

void i2c_bus_close(i2c_bus_t* bus) {
    // Nothing to do - keep bus initialized
    (void)bus;
}

// Wait for I2C bus to be idle with timeout
static bool i2c_wait_idle(I2C_HandleTypeDef* hi2c, uint32_t timeout_ms) {
    uint32_t start = HAL_GetTick();
    while (__HAL_I2C_GET_FLAG(hi2c, I2C_FLAG_BUSY)) {
        if ((HAL_GetTick() - start) > timeout_ms) {
            return false;
        }
    }
    return true;
}

/*
 * Submit I2C packet and wait for completion.
 *
 * This function handles the common I2C patterns:
 * 1. TX only: Write register and value
 * 2. TX + RX: Write register address, then read data (uses Repeated START)
 * 3. RX only: Read data
 *
 * The key fix is detecting TX followed by RX and using HAL_I2C_Mem_Read()
 * which properly generates a Repeated START condition instead of STOP + START.
 */
i2c_status_t i2c_bus_submit_and_wait(i2c_bus_t* bus, i2c_packet_t* packet) {
    if (bus == NULL || packet == NULL) {
        return I2C_STATUS_ERROR;
    }

    I2C_HandleTypeDef* hi2c = &bus->handle;
    uint16_t addr = packet->address << 1;  // HAL expects 8-bit address
    uint32_t timeout = packet->timeout > 0 ? packet->timeout : 1000;
    HAL_StatusTypeDef status;

    // Ensure HAL is in READY state
    if (hi2c->State != HAL_I2C_STATE_READY) {
        // Force HAL state to READY
        hi2c->ErrorCode = HAL_I2C_ERROR_NONE;
        hi2c->State = HAL_I2C_STATE_READY;
        hi2c->Mode = HAL_I2C_MODE_NONE;
    }

    // Wait for bus to be idle (with timeout)
    if (!i2c_wait_idle(hi2c, 10)) {
        // Bus stuck busy - try reset
        i2c_reset(bus);
        if (!i2c_wait_idle(hi2c, 10)) {
            return I2C_STATUS_ERROR;
        }
    }

    int i = 0;
    while (i < packet->op_count) {
        i2c_op_t* op = &packet->ops[i];
        uint8_t* data;

        // Clear any pending error flags and ensure HAL state is ready
        __HAL_I2C_CLEAR_FLAG(hi2c, I2C_FLAG_AF);
        hi2c->ErrorCode = HAL_I2C_ERROR_NONE;
        hi2c->State = HAL_I2C_STATE_READY;

        // Get data pointer
        if (op->flags & I2C_FLAG_EMBED) {
            data = op->data;
        } else {
            data = (uint8_t*)op->ptr;
        }

        /*
         * Check for TX + RX pattern (register read)
         * This is the critical case that requires Repeated START
         */
        if ((op->flags & I2C_FLAG_TX) &&
            (i + 1 < packet->op_count) &&
            (packet->ops[i + 1].flags & I2C_FLAG_RX)) {

            // TX followed by RX - use HAL_I2C_Mem_Read for proper Repeated START
            i2c_op_t* rx_op = &packet->ops[i + 1];
            uint8_t* rx_data;
            uint8_t reg_addr = data[0];  // First byte is register address

            if (rx_op->flags & I2C_FLAG_EMBED) {
                rx_data = rx_op->data;
            } else {
                rx_data = (uint8_t*)rx_op->ptr;
            }

            // HAL_I2C_Mem_Read generates: START-ADDR(W)-REG-REPEATED_START-ADDR(R)-DATA-STOP
            status = HAL_I2C_Mem_Read(hi2c, addr, reg_addr, I2C_MEMADD_SIZE_8BIT,
                                      rx_data, rx_op->size, timeout);

            if (status == HAL_TIMEOUT) {
                i2c_reset(bus);
                return I2C_STATUS_TIMEOUT;
            } else if (status != HAL_OK) {
                if (__HAL_I2C_GET_FLAG(hi2c, I2C_FLAG_AF)) {
                    __HAL_I2C_CLEAR_FLAG(hi2c, I2C_FLAG_AF);
                    i2c_reset(bus);
                    return I2C_STATUS_NACK;
                }
                i2c_reset(bus);
                return I2C_STATUS_ERROR;
            }

            // Skip both TX and RX operations
            i += 2;
            continue;
        }

        /*
         * TX only - typically writing register + value
         */
        if (op->flags & I2C_FLAG_TX) {
            if (op->size == 2) {
                // Common case: register + value - use HAL_I2C_Mem_Write
                status = HAL_I2C_Mem_Write(hi2c, addr, data[0], I2C_MEMADD_SIZE_8BIT,
                                          &data[1], 1, timeout);
            } else {
                // General case
                status = HAL_I2C_Master_Transmit(hi2c, addr, data, op->size, timeout);
            }
        }
        /*
         * RX only
         */
        else if (op->flags & I2C_FLAG_RX) {
            status = HAL_I2C_Master_Receive(hi2c, addr, data, op->size, timeout);
        }
        else {
            // No TX or RX flag - error
            return I2C_STATUS_ERROR;
        }

        if (status == HAL_TIMEOUT) {
            i2c_reset(bus);
            return I2C_STATUS_TIMEOUT;
        } else if (status != HAL_OK) {
            if (__HAL_I2C_GET_FLAG(hi2c, I2C_FLAG_AF)) {
                __HAL_I2C_CLEAR_FLAG(hi2c, I2C_FLAG_AF);
                i2c_reset(bus);
                return I2C_STATUS_NACK;
            }
            i2c_reset(bus);
            return I2C_STATUS_ERROR;
        }

        i++;
    }

    return I2C_STATUS_OK;
}
