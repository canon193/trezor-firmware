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
 * Ported from Latest Trezor firmware with calibration for D001.
 */

#include STM32_HAL_H

#include "stmpe811.h"
#include "i2c_bus.h"
#include "touch_calib.h"

/* Chip IDs */
#define STMPE811_ID 0x0811

/* Identification registers & System Control */
#define STMPE811_REG_CHP_ID_LSB 0x00
#define STMPE811_REG_CHP_ID_MSB 0x01
#define STMPE811_REG_ID_VER 0x02

/* IO expander functionalities */
#define STMPE811_ADC_FCT 0x01
#define STMPE811_TS_FCT 0x02
#define STMPE811_IO_FCT 0x04
#define STMPE811_TEMPSENS_FCT 0x08

/* General Control Registers */
#define STMPE811_REG_SYS_CTRL1 0x03
#define STMPE811_REG_SYS_CTRL2 0x04

/* Interrupt system Registers */
#define STMPE811_REG_INT_CTRL 0x09
#define STMPE811_REG_INT_EN 0x0A
#define STMPE811_REG_INT_STA 0x0B

/* IO Registers */
#define STMPE811_REG_IO_AF 0x17

/* ADC Registers */
#define STMPE811_REG_ADC_CTRL1 0x20
#define STMPE811_REG_ADC_CTRL2 0x21

/* Touch Screen Registers */
#define STMPE811_REG_TSC_CTRL 0x40
#define STMPE811_REG_TSC_CFG 0x41
#define STMPE811_REG_FIFO_TH 0x4A
#define STMPE811_REG_FIFO_STA 0x4B
#define STMPE811_REG_FIFO_SIZE 0x4C
#define STMPE811_REG_TSC_DATA_NON_INC 0xD7
#define STMPE811_REG_TSC_FRACT_XYZ 0x56
#define STMPE811_REG_TSC_I_DRIVE 0x58

/* Touch Screen Pins definition */
#define STMPE811_PIN_4 0x10
#define STMPE811_PIN_5 0x20
#define STMPE811_PIN_6 0x40
#define STMPE811_PIN_7 0x80
#define STMPE811_TOUCH_IO_ALL (STMPE811_PIN_4 | STMPE811_PIN_5 | STMPE811_PIN_6 | STMPE811_PIN_7)

/* TS registers masks */
#define STMPE811_TS_CTRL_ENABLE 0x01
#define STMPE811_TS_CTRL_STATUS 0x80

/* I2C address */
#define TS_I2C_ADDRESS 0x41

/* I2C timeout */
#define I2C_TIMEOUT 0x3000

static i2c_bus_t *g_i2c_bus = NULL;

/* I2C write single byte */
static void IOE_Write(uint8_t Addr, uint8_t Reg, uint8_t Value) {
    i2c_op_t ops[] = {
        {
            .flags = I2C_FLAG_TX | I2C_FLAG_EMBED,
            .size = 2,
            .data = {Reg, Value},
        },
    };

    i2c_packet_t pkt = {
        .address = TS_I2C_ADDRESS,
        .timeout = I2C_TIMEOUT,
        .op_count = ARRAY_LENGTH(ops),
        .ops = ops,
    };

    i2c_bus_submit_and_wait(g_i2c_bus, &pkt);
}

/* I2C read single byte */
static uint8_t IOE_Read(uint8_t Addr, uint8_t Reg) {
    uint8_t value = 0;

    i2c_op_t ops[] = {
        {
            .flags = I2C_FLAG_TX | I2C_FLAG_EMBED,
            .size = 1,
            .data = {Reg},
        },
        {
            .flags = I2C_FLAG_RX,
            .size = 1,
            .ptr = &value,
        },
    };

    i2c_packet_t pkt = {
        .address = TS_I2C_ADDRESS,
        .timeout = I2C_TIMEOUT,
        .op_count = ARRAY_LENGTH(ops),
        .ops = ops,
    };

    i2c_bus_submit_and_wait(g_i2c_bus, &pkt);

    return value;
}

/* I2C read multiple bytes */
static uint16_t IOE_ReadMultiple(uint8_t Addr, uint8_t Reg, uint8_t *pBuffer, uint16_t Length) {
    i2c_op_t ops[] = {
        {
            .flags = I2C_FLAG_TX | I2C_FLAG_EMBED,
            .size = 1,
            .data = {Reg},
        },
        {
            .flags = I2C_FLAG_RX,
            .size = Length,
            .ptr = pBuffer,
        },
    };

    i2c_packet_t pkt = {
        .address = TS_I2C_ADDRESS,
        .timeout = I2C_TIMEOUT,
        .op_count = ARRAY_LENGTH(ops),
        .ops = ops,
    };

    i2c_status_t status = i2c_bus_submit_and_wait(g_i2c_bus, &pkt);

    return status == I2C_STATUS_OK ? 0 : 1;
}

/* Delay function */
static void IOE_Delay(uint32_t Delay) {
    HAL_Delay(Delay);
}

/* Enable alternate function for touch pins */
static void stmpe811_IO_EnableAF(uint16_t DeviceAddr, uint32_t IO_Pin) {
    uint8_t tmp = IOE_Read(DeviceAddr, STMPE811_REG_IO_AF);
    tmp &= ~(uint8_t)IO_Pin;
    IOE_Write(DeviceAddr, STMPE811_REG_IO_AF, tmp);
}

void touch_set_mode(void) {
    uint8_t mode;

    /* Get current SYS_CTRL2 and disable IO functionality */
    mode = IOE_Read(TS_I2C_ADDRESS, STMPE811_REG_SYS_CTRL2);
    mode &= ~(STMPE811_IO_FCT);
    IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_SYS_CTRL2, mode);

    /* Select TSC pins in TSC alternate mode */
    stmpe811_IO_EnableAF(TS_I2C_ADDRESS, STMPE811_TOUCH_IO_ALL);

    /* Enable TS and ADC functions */
    mode &= ~(STMPE811_TS_FCT | STMPE811_ADC_FCT);
    IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_SYS_CTRL2, mode);

    /* Configure ADC: Sample Time, 12-bit, internal ref */
    IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_ADC_CTRL1, 0x49);
    IOE_Delay(2);

    /* ADC clock speed: 3.25 MHz */
    IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_ADC_CTRL2, 0x01);

    /* Touch screen config:
       - 4 samples averaging
       - 500us touch delay
       - 500us panel driver setting time
    */
    IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_TSC_CFG, 0x9A);

    /* FIFO threshold: single point */
    IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_FIFO_TH, 0x01);

    /* Clear FIFO */
    IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_FIFO_STA, 0x01);
    IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_FIFO_STA, 0x00);

    /* Pressure measurement: Fractional=7, Whole=1 */
    IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_TSC_FRACT_XYZ, 0x01);

    /* Driving capability: 50mA */
    IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_TSC_I_DRIVE, 0x01);

    /* Enable TSC in XYZ acquisition mode */
    IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_TSC_CTRL, 0x01);

    /* Clear all status bits */
    IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_INT_STA, 0xFF);

    IOE_Delay(2);
}

void stmpe811_Reset(i2c_bus_t *i2c_bus) {
    g_i2c_bus = i2c_bus;

    /* Power Down the STMPE811 */
    IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_SYS_CTRL1, 2);

    /* Wait for registers to reset */
    IOE_Delay(10);

    /* Power On - reinitializes all registers */
    IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_SYS_CTRL1, 0);

    IOE_Delay(2);
}

uint32_t touch_active(void) {
    uint8_t state;
    uint8_t ret = 0;

    state = ((IOE_Read(TS_I2C_ADDRESS, STMPE811_REG_TSC_CTRL) &
              (uint8_t)STMPE811_TS_CTRL_STATUS) == (uint8_t)0x80);

    if (state > 0) {
        if (IOE_Read(TS_I2C_ADDRESS, STMPE811_REG_FIFO_SIZE) > 0) {
            ret = 1;
        }
    } else {
        /* Reset FIFO when touch ends */
        IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_FIFO_STA, 0x01);
        IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_FIFO_STA, 0x00);
    }

    return ret;
}

void stmpe811_TS_GetXY(uint16_t *X, uint16_t *Y) {
    uint8_t dataXYZ[4];
    uint32_t uldataXYZ;

    IOE_ReadMultiple(TS_I2C_ADDRESS, STMPE811_REG_TSC_DATA_NON_INC, dataXYZ,
                     sizeof(dataXYZ));

    /* Calculate positions values */
    uldataXYZ = (dataXYZ[0] << 24) | (dataXYZ[1] << 16) | (dataXYZ[2] << 8) |
                (dataXYZ[3] << 0);
    *X = (uldataXYZ >> 20) & 0x00000FFF;
    *Y = (uldataXYZ >> 8) & 0x00000FFF;

    /* Reset FIFO */
    IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_FIFO_STA, 0x01);
    IOE_Write(TS_I2C_ADDRESS, STMPE811_REG_FIFO_STA, 0x00);
}

void BSP_TS_GetState(TS_StateTypeDef *TsState) {
    static bool _detected = false;
    static uint32_t _x = 0, _y = 0;
    uint16_t xDiff, yDiff, raw_x, raw_y;
    uint16_t screen_x, screen_y;

    TsState->TouchDetected = _detected;
    TsState->X = _x;
    TsState->Y = _y;

    bool detected = (IOE_Read(TS_I2C_ADDRESS, STMPE811_REG_TSC_CTRL) &
                     STMPE811_TS_CTRL_STATUS) != 0;

    if (!detected) {
        TsState->TouchDetected = _detected = false;
        return;
    }

    if (IOE_Read(TS_I2C_ADDRESS, STMPE811_REG_FIFO_SIZE) > 0) {
        /* Get raw touch coordinates */
        stmpe811_TS_GetXY(&raw_x, &raw_y);

        /* Apply calibration to convert raw to screen coordinates */
        touch_calib_apply(raw_x, raw_y, &screen_x, &screen_y);

        /* Debounce: only update if movement > 5 pixels */
        xDiff = screen_x > _x ? (screen_x - _x) : (_x - screen_x);
        yDiff = screen_y > _y ? (screen_y - _y) : (_y - screen_y);

        if (xDiff + yDiff > 5) {
            _x = screen_x;
            _y = screen_y;
        }

        _detected = true;

        TsState->X = _x;
        TsState->Y = _y;
    }

    TsState->TouchDetected = _detected;
}
