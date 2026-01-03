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
 * Uses polling mode with HAL for simplicity
 * Configured for I2C3: PA8=SCL, PC9=SDA
 */

#include STM32_HAL_H

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

static bool i2c_init(struct i2c_bus* bus) {
    if (bus->initialized) {
        return true;
    }

    // Initialize GPIO
    i2c_gpio_init();

    // Enable I2C3 clock
    __HAL_RCC_I2C3_CLK_ENABLE();

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

i2c_status_t i2c_bus_submit_and_wait(i2c_bus_t* bus, i2c_packet_t* packet) {
    if (bus == NULL || packet == NULL) {
        return I2C_STATUS_ERROR;
    }

    I2C_HandleTypeDef* hi2c = &bus->handle;
    uint8_t addr = packet->address << 1;  // HAL expects 8-bit address
    uint32_t timeout = packet->timeout > 0 ? packet->timeout : 1000;

    for (int i = 0; i < packet->op_count; i++) {
        i2c_op_t* op = &packet->ops[i];
        uint8_t* data;
        HAL_StatusTypeDef status;

        // Get data pointer
        if (op->flags & I2C_FLAG_EMBED) {
            data = op->data;
        } else {
            data = (uint8_t*)op->ptr;
        }

        if (op->flags & I2C_FLAG_RX) {
            // Receive data
            status = HAL_I2C_Master_Receive(hi2c, addr, data, op->size, timeout);
        } else {
            // Transmit data (I2C_FLAG_TX or default)
            status = HAL_I2C_Master_Transmit(hi2c, addr, data, op->size, timeout);
        }

        if (status == HAL_TIMEOUT) {
            return I2C_STATUS_TIMEOUT;
        } else if (status != HAL_OK) {
            // Check for NACK
            if (__HAL_I2C_GET_FLAG(hi2c, I2C_FLAG_AF)) {
                __HAL_I2C_CLEAR_FLAG(hi2c, I2C_FLAG_AF);
                return I2C_STATUS_NACK;
            }
            return I2C_STATUS_ERROR;
        }
    }

    return I2C_STATUS_OK;
}
