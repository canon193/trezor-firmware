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
 * Based on stm32f429i_discovery_sdram.c from STMicroelectronics BSP.
 * Drives the IS42S16400J SDRAM on the STM32F429I-DISC1 board.
 */

#include STM32_HAL_H

#include "sdram.h"

// FMC SDRAM configuration
#define SDRAM_MEMORY_WIDTH       FMC_SDRAM_MEM_BUS_WIDTH_16
#define SDRAM_CAS_LATENCY        FMC_SDRAM_CAS_LATENCY_3
#define SDCLOCK_PERIOD           FMC_SDRAM_CLOCK_PERIOD_2
#define SDRAM_READBURST          FMC_SDRAM_RBURST_DISABLE

// Refresh rate counter: (15.62 us x Freq) - 20 = (15.62 us x 90 MHz) - 20 = 1386
#define REFRESH_COUNT            ((uint32_t)1386)
#define SDRAM_TIMEOUT            ((uint32_t)0xFFFF)

// Mode register defines for IS42S16400J
#define SDRAM_MODEREG_BURST_LENGTH_1             ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL      ((uint16_t)0x0000)
#define SDRAM_MODEREG_CAS_LATENCY_3              ((uint16_t)0x0030)
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD    ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE     ((uint16_t)0x0200)

static SDRAM_HandleTypeDef hsdram;
static FMC_SDRAM_TimingTypeDef timing;
static FMC_SDRAM_CommandTypeDef command;

static void sdram_gpio_init(void) {
    GPIO_InitTypeDef gpio = {0};

    // Enable GPIO clocks
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /*
     * SDRAM pins assignment on STM32F429I-DISC1:
     *
     * PD0  <-> FMC_D2   | PE0  <-> FMC_NBL0  | PF0  <-> FMC_A0    | PG0  <-> FMC_A10
     * PD1  <-> FMC_D3   | PE1  <-> FMC_NBL1  | PF1  <-> FMC_A1    | PG1  <-> FMC_A11
     * PD8  <-> FMC_D13  | PE7  <-> FMC_D4    | PF2  <-> FMC_A2    | PG8  <-> FMC_SDCLK
     * PD9  <-> FMC_D14  | PE8  <-> FMC_D5    | PF3  <-> FMC_A3    | PG15 <-> FMC_NCAS
     * PD10 <-> FMC_D15  | PE9  <-> FMC_D6    | PF4  <-> FMC_A4    |
     * PD14 <-> FMC_D0   | PE10 <-> FMC_D7    | PF5  <-> FMC_A5    |
     * PD15 <-> FMC_D1   | PE11 <-> FMC_D8    | PF11 <-> FMC_NRAS  |
     *                   | PE12 <-> FMC_D9    | PF12 <-> FMC_A6    |
     *                   | PE13 <-> FMC_D10   | PF13 <-> FMC_A7    |
     *                   | PE14 <-> FMC_D11   | PF14 <-> FMC_A8    |
     *                   | PE15 <-> FMC_D12   | PF15 <-> FMC_A9    |
     *
     * PB5 <-> FMC_SDCKE1
     * PB6 <-> FMC_SDNE1
     * PC0 <-> FMC_SDNWE
     */

    // Common GPIO configuration
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Pull = GPIO_NOPULL;
    gpio.Alternate = GPIO_AF12_FMC;

    // GPIOB configuration: PB5 (SDCKE1), PB6 (SDNE1)
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6;
    HAL_GPIO_Init(GPIOB, &gpio);

    // GPIOC configuration: PC0 (SDNWE)
    gpio.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOC, &gpio);

    // GPIOD configuration: PD0, PD1, PD8, PD9, PD10, PD14, PD15
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 |
               GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gpio);

    // GPIOE configuration: PE0, PE1, PE7-PE15
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 |
               GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
               GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gpio);

    // GPIOF configuration: PF0-PF5, PF11-PF15
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
               GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 |
               GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOF, &gpio);

    // GPIOG configuration: PG0, PG1, PG4, PG5, PG8, PG15
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 |
               GPIO_PIN_8 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOG, &gpio);
}

static void sdram_initialization_sequence(void) {
    __IO uint32_t tmpmrd = 0;

    // Step 1: Configure a clock configuration enable command
    command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
    command.AutoRefreshNumber = 1;
    command.ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(&hsdram, &command, SDRAM_TIMEOUT);

    // Step 2: Insert 100 us minimum delay (use 1 ms)
    HAL_Delay(1);

    // Step 3: Configure a PALL (precharge all) command
    command.CommandMode = FMC_SDRAM_CMD_PALL;
    command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
    command.AutoRefreshNumber = 1;
    command.ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(&hsdram, &command, SDRAM_TIMEOUT);

    // Step 4: Configure an Auto Refresh command
    command.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
    command.AutoRefreshNumber = 4;
    command.ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(&hsdram, &command, SDRAM_TIMEOUT);

    // Step 5: Program the external memory mode register
    tmpmrd = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_1 |
             SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
             SDRAM_MODEREG_CAS_LATENCY_3 |
             SDRAM_MODEREG_OPERATING_MODE_STANDARD |
             SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

    command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
    command.AutoRefreshNumber = 1;
    command.ModeRegisterDefinition = tmpmrd;
    HAL_SDRAM_SendCommand(&hsdram, &command, SDRAM_TIMEOUT);

    // Step 6: Set the refresh rate counter
    HAL_SDRAM_ProgramRefreshRate(&hsdram, REFRESH_COUNT);
}

void sdram_init(void) {
    // Check if SDRAM is already initialized (by bootloader)
    // If FMC clock is already enabled, bootloader did the init
    if (__HAL_RCC_FMC_IS_CLK_ENABLED()) {
        // SDRAM already initialized, skip re-init
        return;
    }

    // Enable FMC clock
    __HAL_RCC_FMC_CLK_ENABLE();

    // Initialize GPIO pins
    sdram_gpio_init();

    // SDRAM device configuration
    hsdram.Instance = FMC_SDRAM_DEVICE;

    // Timing configuration for 90 MHz SD clock (180 MHz / 2)
    // TMRD: 2 Clock cycles
    timing.LoadToActiveDelay = 2;
    // TXSR: min=70ns (7 x 11.11ns)
    timing.ExitSelfRefreshDelay = 7;
    // TRAS: min=42ns (4 x 11.11ns) max=120k (ns)
    timing.SelfRefreshTime = 4;
    // TRC: min=70 (7 x 11.11ns)
    timing.RowCycleDelay = 7;
    // TWR: min=1+ 7ns (1+1 x 11.11ns)
    timing.WriteRecoveryTime = 2;
    // TRP: 20ns => 2 x 11.11ns
    timing.RPDelay = 2;
    // TRCD: 20ns => 2 x 11.11ns
    timing.RCDDelay = 2;

    // FMC SDRAM control configuration
    hsdram.Init.SDBank = FMC_SDRAM_BANK2;
    hsdram.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_8;
    hsdram.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
    hsdram.Init.MemoryDataWidth = SDRAM_MEMORY_WIDTH;
    hsdram.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
    hsdram.Init.CASLatency = SDRAM_CAS_LATENCY;
    hsdram.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    hsdram.Init.SDClockPeriod = SDCLOCK_PERIOD;
    hsdram.Init.ReadBurst = SDRAM_READBURST;
    hsdram.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_1;

    // Initialize SDRAM controller
    HAL_SDRAM_Init(&hsdram, &timing);

    // Run SDRAM initialization sequence
    sdram_initialization_sequence();
}
