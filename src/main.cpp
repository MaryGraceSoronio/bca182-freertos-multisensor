/**
 * main.cpp - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Target : STM32 Blue Pill (STM32F103C8T6), Wokwi simulation
 * Framework: STM32Cube (HAL + CMSIS) via PlatformIO
 *
 * Structure required by section 41 of the laboratory specification:
 *
 *     hardware initialization
 *             |
 *     FreeRTOS object creation
 *             |
 *     task creation
 *             |
 *     scheduler-driven operation
 */

#include "stm32f1xx_hal.h"

int main(void)
{
    HAL_Init();

    while (1) {
    }

    return 0;
}
